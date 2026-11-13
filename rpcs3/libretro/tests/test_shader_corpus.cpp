// The Vulkan renderer's shaders without glslang: every GLSL source the
// renderer makes, gathered without a game at the one place they all pass
// (spirv::compile_glsl_to_spv, which this test stands in for), goes through
// both glslang - still what the core uses - and libretro-common's rslc,
// which is to replace it.
//
// - rslc compiles every one of them, but for the fourteen that use
//   multisample images and samplers (the resolve tasks and passes), which
//   it does not have yet: those are counted, and nothing else is excused;
// - its module has the interface glslang's has: the same bindings, sets,
//   locations and built-ins by name, and every block member at the same
//   offset, with the same array and matrix strides - what the renderer's
//   buffers and descriptor sets are laid out to;
// - the Vulkan driver takes its module (vkCreateShaderModule).
//
// What it does not show: that the two modules compute the same.
//
// Gathered: the shader interpreter's vertex and fragment stages for its
// option sets; every compute task (byte shuffles, depth-stencil gather and
// scatter, float conversion, deswizzle, tile copies, the aggregator, the
// upscaler's two); the multisample resolve tasks and passes; the overlay,
// clear and video-out passes. Not gathered yet: what the fragment and
// vertex program decompilers make for a game's own programs. Needs a
// Vulkan driver (a software one will do); without one it says so and
// passes.
// SHADER_CORPUS_DIR=<dir> writes each source and both modules there.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"

// The compiler the renderer calls, under another name: the test's own stands
// where it was
#define compile_glsl_to_spv compile_glsl_to_spv_reference
#include "../../Emu/RSX/Program/SPIRVCommon.cpp"
#undef compile_glsl_to_spv

#define private public
#define protected public
#include "Emu/RSX/VK/VKShaderInterpreter.h"
#include "Emu/RSX/VK/VKCompute.h"
#include "Emu/RSX/VK/VKResolveHelper.h"
#include "Emu/RSX/VK/VKOverlays.h"
#include "Emu/RSX/VK/upscalers/fsr_pass.h"
#undef private
#undef protected
#include "Emu/RSX/VK/VKHelpers.h"
#include "Emu/RSX/VK/VKVertexProgram.h"
#include "Emu/RSX/VK/VKFragmentProgram.h"
#include "Emu/RSX/VK/vkutils/instance.h"
#include "Emu/RSX/VK/vkutils/device.h"
#include "Emu/RSX/Program/ShaderInterpreter.h"
#include "Emu/system_config.h"
#include "Utilities/File.h"
#include "Emu/IdManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <set>
#include <tuple>

#include <gfx/shaders/rslc.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct gathered
{
	std::string source;
	std::vector<u32> reference;
	::glsl::program_domain domain;
	bool reference_ok;
};

static std::vector<gathered> s_corpus;

namespace spirv
{
	bool compile_glsl_to_spv(std::vector<u32>& spv, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		const bool ok = compile_glsl_to_spv_reference(spv, shader, domain, rules);
		s_corpus.push_back({shader, spv, domain, ok});
		return ok;
	}
}

// A module's interface: what is bound, placed and laid out where, by name
typedef std::set<std::tuple<std::string, std::string, u32, u32>> interface_t;

static interface_t interface_of(const u32* w, usz n)
{
	std::map<u32, std::string> names;
	std::map<std::pair<u32, u32>, std::string> members;
	interface_t out;

	const auto text = [](const u32* at) { return std::string(reinterpret_cast<const char*>(at)); };

	for (usz pass = 0; pass < 2; pass++)
	{
		for (usz i = 5; i < n && (w[i] >> 16); i += w[i] >> 16)
		{
			const u32 op = w[i] & 0xffff, len = w[i] >> 16;

			if (pass == 0)
			{
				if (op == 5) // OpName
					names[w[i + 1]] = text(&w[i + 2]);
				else if (op == 6) // OpMemberName
					members[{w[i + 1], w[i + 2]}] = text(&w[i + 3]);
				continue;
			}

			if (op == 71 && len >= 4) // OpDecorate: Location 30, Binding 33, DescriptorSet 34, BuiltIn 11, Index 32, ArrayStride 6
			{
				const u32 what = w[i + 2];
				if ((what == 30 || what == 33 || what == 34 || what == 11 || what == 32) && names.count(w[i + 1]) && !names[w[i + 1]].empty())
					out.emplace(names[w[i + 1]], "", what, w[i + 3]);
			}
			else if (op == 72 && len >= 5) // OpMemberDecorate: Offset 35, MatrixStride 7, ArrayStride 6
			{
				const u32 what = w[i + 3];
				if (what == 35 || what == 7 || what == 6)
					out.emplace(names[w[i + 1]], members[{w[i + 1], w[i + 2]}], what, w[i + 4]);
			}
		}
	}

	return out;
}

static usz s_rslc_refused = 0, s_rslc_not_yet = 0, s_interface_differs = 0, s_driver_refused = 0;
static usz s_reference_words = 0, s_rslc_words = 0;

// One gathered source through rslc: compiled, compared, handed to the driver
static void through_rslc(usz index, const gathered& g, const char* dir)
{
	char err[512]{};
	rslc_opts opts{};
	rslc_result result{};
	opts.dst = RSLC_DST_SPIRV;
	opts.err = err;
	opts.err_cap = sizeof(err);

	const rslc_stage stage = g.domain == ::glsl::glsl_vertex_program ? RSLC_STAGE_VERTEX : g.domain == ::glsl::glsl_fragment_program ? RSLC_STAGE_FRAGMENT : RSLC_STAGE_COMPUTE;
	const char* ext = stage == RSLC_STAGE_VERTEX ? "vert" : stage == RSLC_STAGE_FRAGMENT ? "frag" : "comp";

	if (dir)
	{
		fs::write_file(fmt::format("%s/%03u.%s", dir, index, ext), fs::rewrite, g.source);
		fs::file(fmt::format("%s/%03u.%s.ref.spv", dir, index, ext), fs::rewrite).write(g.reference.data(), g.reference.size() * 4);
	}

	if (!rslc_compile(g.source.data(), g.source.size(), stage, &opts, &result))
	{
		// What rslc does not have yet, each a type the multisample resolve
		// tasks and passes use: counted apart, and nothing else is let through
		for (const char* known : {"got image2DMS", "got sampler2DMS", "got usampler2DMS", "unknown layout qualifier rg16f"})
		{
			if (std::strstr(err, known))
			{
				s_rslc_not_yet++;
				return;
			}
		}

		std::fprintf(stderr, "source %u (%s): rslc refuses it: %s\n", static_cast<u32>(index), ext, err);
		s_rslc_refused++;
		return;
	}

	if (dir)
		fs::file(fmt::format("%s/%03u.%s.rslc.spv", dir, index, ext), fs::rewrite).write(result.spirv, result.spirv_len * 4);

	s_reference_words += g.reference.size();
	s_rslc_words += result.spirv_len;

	const interface_t reference = interface_of(g.reference.data(), g.reference.size());
	const interface_t mine = interface_of(result.spirv, result.spirv_len);

	if (reference != mine)
	{
		s_interface_differs++;
		for (const auto& [name, member, what, value] : reference)
			if (!mine.count({name, member, what, value}))
				std::fprintf(stderr, "source %u (%s): glslang has %s%s%s decoration %u = %u, rslc does not\n", static_cast<u32>(index), ext, name.c_str(), member.empty() ? "" : ".", member.c_str(), what, value);
		for (const auto& [name, member, what, value] : mine)
			if (!reference.count({name, member, what, value}))
				std::fprintf(stderr, "source %u (%s): rslc has %s%s%s decoration %u = %u, glslang does not\n", static_cast<u32>(index), ext, name.c_str(), member.empty() ? "" : ".", member.c_str(), what, value);
	}

	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = result.spirv_len * sizeof(u32);
	info.pCode = result.spirv;
	VkShaderModule module = VK_NULL_HANDLE;

	if (vkCreateShaderModule(*vk::g_render_device, &info, nullptr, &module) != VK_SUCCESS || !module)
		s_driver_refused++;
	else
		vkDestroyShaderModule(*vk::g_render_device, module, nullptr);

	rslc_result_free(&result, &opts);
}

static void compile_now(::glsl::program_domain domain, const std::string& source)
{
	vk::glsl::shader shader;
	shader.create(domain, source);
	CHECK(shader.compile() != VK_NULL_HANDLE);
	shader.destroy();
}

template <typename T, typename... Args>
static void compute_source(Args&&... args)
{
	T task(std::forward<Args>(args)...);
	task.create();
	compile_now(::glsl::program_domain::glsl_compute_program, task.m_src);
}

template <typename T>
static void overlay_source()
{
	T pass;
	compile_now(::glsl::program_domain::glsl_vertex_program, pass.vs_src);
	compile_now(::glsl::program_domain::glsl_fragment_program, pass.fs_src);
}

struct shader_corpus_test
{
	// The shader interpreter, each of its builds
	static void interpreter(const vk::render_device& device)
	{
		vk::shader_interpreter interpreter;
		interpreter.init(device);
		for (u64 opt : {u64{0}, u64{program_common::interpreter::COMPILER_OPT_ENABLE_INSTANCING}, u64{program_common::interpreter::COMPILER_OPT_ENABLE_VTX_TEXTURES}})
			CHECK(!!interpreter.build_vs(opt));
		for (u64 opt = 0; opt < 256; opt += 17)
			CHECK(!!interpreter.build_fs(opt));
		CHECK(!!interpreter.build_fs(0xff));
		interpreter.destroy();
	}
};

int main()
{
	void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	const auto gipa = lib ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr")) : nullptr;
	if (!gipa)
	{
		std::printf("test_shader_corpus: no Vulkan loader here: nothing gathered; ok\n");
		return 0;
	}

	g_fxo->reset();
	vk::load_vulkan(gipa);
	vk::instance instance;
	if (!instance.create("corpus", true))
	{
		std::printf("test_shader_corpus: no Vulkan instance: nothing gathered; ok\n");
		return 0;
	}
	instance.bind();
	auto& gpus = instance.enumerate_devices();
	if (gpus.empty())
	{
		std::printf("test_shader_corpus: no Vulkan device: nothing gathered; ok\n");
		return 0;
	}

	vk::physical_device& gpu = gpus[0];
	u32 graphics = umax;
	for (u32 i = 0; i < gpu.get_queue_count(); i++)
		if (graphics == umax && (gpu.get_queue_properties(i).queueFlags & VK_QUEUE_GRAPHICS_BIT))
			graphics = i;
	CHECK(graphics != umax);

	vk::render_device device;
	device.create(gpu, graphics, umax, umax);
	vk::set_current_renderer(device);
	spirv::initialize_compiler_context();

	shader_corpus_test::interpreter(device);

	// The compute tasks
	compute_source<vk::cs_shuffle_16>();
	compute_source<vk::cs_shuffle_32>();
	compute_source<vk::cs_shuffle_32_16>();
	compute_source<vk::cs_shuffle_d24x8_f32>();
	compute_source<vk::cs_shuffle_se_f32_d24x8>();
	compute_source<vk::cs_shuffle_se_d24x8>();
	compute_source<vk::cs_gather_d24x8<true>>();
	compute_source<vk::cs_gather_d24x8<false>>();
	compute_source<vk::cs_gather_d32x8<true, true>>();
	compute_source<vk::cs_gather_d32x8<false, false>>();
	compute_source<vk::cs_gather_d32x8<true, false>>();
	compute_source<vk::cs_gather_d32x8<false, true>>();
	compute_source<vk::cs_scatter_d24x8>();
	compute_source<vk::cs_scatter_d32x8<false>>();
	compute_source<vk::cs_scatter_d32x8<true>>();
	compute_source<vk::cs_fconvert_task<f16, f32>>();
	compute_source<vk::cs_fconvert_task<f32, f16>>();
	compute_source<vk::cs_fconvert_task<f32, f16, false, true>>();
	compute_source<vk::cs_aggregator>();
	compute_source<vk::cs_tile_memcpy<vk::RSX_detiler_op::decode>>();
	compute_source<vk::cs_tile_memcpy<vk::RSX_detiler_op::encode>>();
	compute_source<vk::cs_deswizzle_3d<u8, u8, false>>();
	compute_source<vk::cs_deswizzle_3d<u16, u16, true>>();
	compute_source<vk::cs_deswizzle_3d<u16, u32, false>>();
	compute_source<vk::cs_deswizzle_3d<u32, u16, true>>();
	compute_source<vk::cs_deswizzle_3d<u32, u32, true>>();
	compute_source<vk::cs_deswizzle_3d<u32, u32, false>>();

	// Multisample resolve and its reverse, colour by compute and depth by a pass
	for (const char* prefix : {"rgba8", "rgba16f", "rg16f", "r32f", "rgba32f"})
	{
		compute_source<vk::cs_resolve_task>(std::string(prefix));
		compute_source<vk::cs_unresolve_task>(std::string(prefix));
	}
	compute_source<vk::cs_resolve_task>(std::string("rgba8"), true);
	overlay_source<vk::depthonly_resolve>();
	overlay_source<vk::depthonly_unresolve>();
	overlay_source<vk::stencilonly_resolve>();
	overlay_source<vk::stencilonly_unresolve>();
	overlay_source<vk::depthstencil_resolve_EXT>();
	overlay_source<vk::depthstencil_unresolve_EXT>();

	// The passes drawn over the picture, and the upscaler's
	overlay_source<vk::ui_overlay_renderer>();
	overlay_source<vk::attachment_clear_pass>();
	overlay_source<vk::stencil_clear_pass>();
	overlay_source<vk::video_out_calibration_pass>();
	compute_source<vk::FidelityFX::easu_pass>();
	compute_source<vk::FidelityFX::rcas_pass>();

	usz reference_failed = 0;
	for (const gathered& g : s_corpus)
		reference_failed += !g.reference_ok;

	CHECK(reference_failed == 0);
	CHECK(s_corpus.size() >= 79);

	const char* dir = std::getenv("SHADER_CORPUS_DIR");
	for (usz i = 0; i < s_corpus.size(); i++)
		through_rslc(i, s_corpus[i], dir);

	CHECK(s_rslc_refused == 0);
	CHECK(s_rslc_not_yet == 14); // down to none before the renderer switches; a rise is a source newly lost
	CHECK(s_interface_differs == 0);
	CHECK(s_driver_refused == 0);

	std::printf("test_shader_corpus: %u wait for multisample images and samplers in rslc\n", static_cast<u32>(s_rslc_not_yet));
	std::printf("test_shader_corpus: rslc refused %u, differs in interface or layout on %u, the driver refused %u of its modules; %u words where glslang makes %u\n",
		static_cast<u32>(s_rslc_refused), static_cast<u32>(s_interface_differs), static_cast<u32>(s_driver_refused), static_cast<u32>(s_rslc_words), static_cast<u32>(s_reference_words));
	std::printf("test_shader_corpus: %u sources gathered, %u refused by the reference compiler\n", ::size32(s_corpus), static_cast<u32>(reference_failed));
	std::printf("test_shader_corpus: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
