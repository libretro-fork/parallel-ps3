// The Vulkan renderer's shaders through libretro-common's rslc, which is
// what compiles them: every GLSL source the renderer makes, gathered without
// a game at the one place they all pass (spirv::compile_glsl_to_spv, which
// this test wraps).
//
// - the core's compile takes every one of them;
// - the Vulkan driver takes the module (vkCreateShaderModule);
// - the module runs (shader_runner.c): a fragment stage draws, a vertex
//   stage puts out its position and outputs, a compute stage runs a group.
//
// With SHADER_CORPUS_GLSLANG=<a glslang executable> each source is also
// compiled by glslang, which the core used before rslc and no longer has,
// and the two are held to each other:
//
// - rslc's module has the interface glslang's has: the same bindings, sets,
//   locations and built-ins by name, and every block member at the same
//   offset, with the same array and matrix strides - what the renderer's
//   buffers and descriptor sets are laid out to;
// - over all of them its modules are within 2% of the size of glslang's;
// - run on the driver over the same buffers, textures and inputs, filled
//   three ways, the two modules make the same, byte for byte: what a
//   fragment stage draws into every target, depth and stencil, what a
//   vertex stage puts out (captured by transform feedback), what a compute
//   stage leaves in every buffer and image.
//
// What it does not show: the stages it cannot run here - the fragment
// stages built for barycentric inputs where the driver has none, the shader
// interpreter's (they run the program in their buffers), and the ones that
// read an attachment.
//
// Gathered: the shader interpreter's vertex and fragment stages for its
// option sets; every compute task (byte shuffles, depth-stencil gather and
// scatter, float conversion, deswizzle, tile copies, the aggregator, the
// upscaler's two); the multisample resolve tasks and passes; the overlay,
// clear and video-out passes; and what the fragment and vertex program
// decompilers make of a game's own programs, which the test writes as the
// console's microcode (fragment_programs, vertex_programs). Needs a
// Vulkan driver (a software one will do); without one it says so and
// passes.
// SHADER_CORPUS_DIR=<dir> writes each source and its modules there, and a
// source the core's compile refuses as refused.glsl.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"

// The compiler the renderer calls, under another name: the test's own stands
// where it was
#define compile_glsl_to_spv compile_glsl_to_spv_core
#define compile_glsl_lent compile_glsl_lent_core
#define build_end build_end_core
#include "../../Emu/RSX/Program/SPIRVCommon.cpp"
#undef compile_glsl_to_spv
#undef compile_glsl_lent
#undef build_end

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
#include "Emu/RSX/Program/RSXFragmentProgram.h"
#include "Emu/RSX/Program/RSXVertexProgram.h"
#include "Emu/RSX/Program/ProgramStateCache.h"
#include "Emu/RSX/Program/Assembler/FPOpcodes.h"
#include "Emu/system_config.h"
#include "Utilities/File.h"
#include "Emu/IdManager.h"
#include "Emu/RSX/rsx_cache.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <unistd.h>
#include <map>
#include <set>
#include <tuple>

#include "shader_runner.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct gathered
{
	std::string source;
	std::vector<u32> compiled;
	::glsl::program_domain domain;
	bool ok;
};

static std::vector<gathered> s_corpus;

namespace vk
{
	extern driver_vendor g_driver_vendor;
}

static usz s_lent = 0, s_compiles = 0;

namespace spirv
{
	bool compile_glsl_to_spv(std::vector<u32>& spv, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		const bool ok = compile_glsl_to_spv_core(spv, shader, domain, rules);
		s_corpus.push_back({shader, spv, domain, ok});
		if (!ok && std::getenv("SHADER_CORPUS_DIR"))
			fs::write_file(std::string(std::getenv("SHADER_CORPUS_DIR")) + "/refused.glsl", fs::rewrite, shader);
		return ok;
	}

	// What a shader is compiled through: the module lent from the compiling
	// thread's block
	bool compile_glsl_lent(const u32*& words, usz& count, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		const bool ok = compile_glsl_lent_core(words, count, shader, domain, rules);
		// Lent from the thread's block, where the compiler wrote it: not a
		// copy, not an allocation
		if (ok)
		{
			const u8* at = reinterpret_cast<const u8*>(words);
			const u8* block = this_thread().memory.results();
			s_lent += at >= block && at + count * 4 <= block + compile_memory::result_size;
			s_compiles++;
		}
		s_corpus.push_back({shader, std::vector<u32>(words, words + count), domain, ok});
		if (!ok && std::getenv("SHADER_CORPUS_DIR"))
			fs::write_file(std::string(std::getenv("SHADER_CORPUS_DIR")) + "/refused.glsl", fs::rewrite, shader);
		return ok;
	}

	// A stage built by calls has no source of its own: built_source gives it
	// the GLSL it stands for, and its stage, to hold it to
	bool build_end(rslc_builder* builder, std::vector<u32>& spv)
	{
		const bool ok = build_end_core(builder, spv);
		s_corpus.push_back({std::string(), spv, ::glsl::glsl_invalid_program, ok});
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

static usz s_refused = 0, s_interface_differs = 0, s_driver_refused = 0, s_reference_failed = 0;
static usz s_variants = 0, s_first_variant = umax;
static usz s_reference_words = 0, s_rslc_words = 0;
static usz s_ran = 0, s_ran_vertex = 0, s_ran_compute = 0, s_run_differs = 0, s_not_run = 0;
static bool s_can_run = false;
static std::map<std::string, u32> s_not_run_why;

// The vertex stage the runner draws a fragment stage with, through the core's compile
static int compile_for_runner(void*, const char* glsl, size_t len, uint32_t** words, size_t* count)
{
	std::vector<u32> spv;
	std::string source(glsl, len);
	if (!spirv::compile_glsl_to_spv_core(spv, source, ::glsl::program_domain::glsl_vertex_program, ::glsl::glsl_rules_vulkan))
		return 0;
	*words = static_cast<uint32_t*>(std::malloc(spv.size() * 4));
	std::memcpy(*words, spv.data(), spv.size() * 4);
	*count = spv.size();
	return 1;
}

// Every source through the glslang executable given: a file in and a module
// out for each, as many at a time as there are processors
static std::vector<std::vector<u32>> s_reference;

static void reference_compile_all(const char* tool)
{
	const std::string dir = fmt::format("%s/shader_corpus_%u", std::filesystem::temp_directory_path().string(), static_cast<u32>(getpid()));
	std::filesystem::create_directories(dir);
	s_reference.resize(s_corpus.size());

	for (usz i = 0; i < s_corpus.size(); i++)
	{
		const gathered& g = s_corpus[i];
		const char* ext = g.domain == ::glsl::glsl_vertex_program ? "vert" : g.domain == ::glsl::glsl_fragment_program ? "frag" : "comp";
		fs::write_file(fmt::format("%s/%u.%s", dir, static_cast<u32>(i), ext), fs::rewrite, g.source);
	}

	const std::string command = fmt::format("cd '%s' && ls | xargs -P \"$(nproc)\" -I{} '%s' -V --target-env vulkan1.2 -o {}.spv {} > /dev/null 2>&1", dir, tool);
	[[maybe_unused]] const int status = std::system(command.c_str());

	for (usz i = 0; i < s_corpus.size(); i++)
	{
		const gathered& g = s_corpus[i];
		const char* ext = g.domain == ::glsl::glsl_vertex_program ? "vert" : g.domain == ::glsl::glsl_fragment_program ? "frag" : "comp";
		if (std::FILE* m = std::fopen(fmt::format("%s/%u.%s.spv", dir, static_cast<u32>(i), ext).c_str(), "rb"))
		{
			std::fseek(m, 0, SEEK_END);
			const long size = std::ftell(m);
			std::fseek(m, 0, SEEK_SET);
			s_reference[i].resize(size > 0 ? size / 4 : 0);
			if (std::fread(s_reference[i].data(), 4, s_reference[i].size(), m) != s_reference[i].size())
				s_reference[i].clear();
			std::fclose(m);
		}
	}

	std::filesystem::remove_all(dir);
}

// One gathered source: taken by the driver, held to glslang's where there is
// one, and run
static void check_source(usz index, const gathered& g, const char* dir, const char* tool)
{
	const char* ext = g.domain == ::glsl::glsl_vertex_program ? "vert" : g.domain == ::glsl::glsl_fragment_program ? "frag" : "comp";

	if (dir)
		fs::write_file(fmt::format("%s/%03u.%s", dir, index, ext), fs::rewrite, g.source);

	if (!g.ok)
	{
		std::fprintf(stderr, "source %u (%s): the core's compile refuses it\n", static_cast<u32>(index), ext);
		s_refused++;
		return;
	}

	if (dir)
		fs::file(fmt::format("%s/%03u.%s.rslc.spv", dir, index, ext), fs::rewrite).write(g.compiled.data(), g.compiled.size() * 4);

	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = g.compiled.size() * sizeof(u32);
	info.pCode = g.compiled.data();
	VkShaderModule module = VK_NULL_HANDLE;

	if (vkCreateShaderModule(*vk::g_render_device, &info, nullptr, &module) != VK_SUCCESS || !module)
	{
		s_driver_refused++;
		return;
	}
	vkDestroyShaderModule(*vk::g_render_device, module, nullptr);

	// glslang's module of it, where there is a glslang
	std::vector<u32> reference;
	if (tool)
	{
		reference = s_reference[index];
		if (reference.empty())
		{
			std::fprintf(stderr, "source %u (%s): the reference compiler refuses it\n", static_cast<u32>(index), ext);
			s_reference_failed++;
			return;
		}

		if (dir)
			fs::file(fmt::format("%s/%03u.%s.ref.spv", dir, index, ext), fs::rewrite).write(reference.data(), reference.size() * 4);

		s_reference_words += reference.size();
		s_rslc_words += g.compiled.size();

		const interface_t theirs = interface_of(reference.data(), reference.size());
		const interface_t mine = interface_of(g.compiled.data(), g.compiled.size());

		if (theirs != mine)
		{
			s_interface_differs++;
			for (const auto& [name, member, what, value] : theirs)
				if (!mine.count({name, member, what, value}))
					std::fprintf(stderr, "source %u (%s): glslang has %s%s%s decoration %u = %u, rslc does not\n", static_cast<u32>(index), ext, name.c_str(), member.empty() ? "" : ".", member.c_str(), what, value);
			for (const auto& [name, member, what, value] : mine)
				if (!theirs.count({name, member, what, value}))
					std::fprintf(stderr, "source %u (%s): rslc has %s%s%s decoration %u = %u, glslang does not\n", static_cast<u32>(index), ext, name.c_str(), member.empty() ? "" : ".", member.c_str(), what, value);
		}
	}

	// Run: against glslang's module, or on its own. On its own a run shows
	// only that the module runs, and the programs as this driver gets them
	// are enough for that
	if (s_can_run && (tool || index < s_first_variant))
	{
		char why[200]{};
		const std::vector<u32>& first = tool ? reference : g.compiled;
		const shader_run_verdict v = g.domain == ::glsl::glsl_fragment_program
			? shader_run_fragment(first.data(), first.size(), g.compiled.data(), g.compiled.size(), compile_for_runner, nullptr, why, sizeof(why))
			: g.domain == ::glsl::glsl_vertex_program ? shader_run_vertex(first.data(), first.size(), g.compiled.data(), g.compiled.size(), why, sizeof(why))
			: shader_run_compute(first.data(), first.size(), g.compiled.data(), g.compiled.size(), why, sizeof(why));
		if (v == SHADER_RUN_SAME)
			(g.domain == ::glsl::glsl_fragment_program ? s_ran : g.domain == ::glsl::glsl_vertex_program ? s_ran_vertex : s_ran_compute)++;
		else if (v == SHADER_RUN_NOT_RUN)
		{
			s_not_run++;
			s_not_run_why[std::string(ext) + ": " + why]++;
		}
		else
		{
			s_run_differs++;
			std::fprintf(stderr, "source %u (%s): %s\n", static_cast<u32>(index), ext, why);
		}
	}
}

static void compile_now(::glsl::program_domain domain, const std::string& source)
{
	vk::glsl::shader shader;
	shader.create(domain, source);
	CHECK(shader.compile() != VK_NULL_HANDLE);
	// The text is let go of once it is compiled
	CHECK(shader.get_source().capacity() < 32);
	shader.destroy();
}

template <typename T, typename... Args>
static void compute_source(Args&&... args)
{
	T task(std::forward<Args>(args)...);
	task.create();
	compile_now(::glsl::program_domain::glsl_compute_program, task.m_src);
}

// A compute task that builds its stage by calls, held to the GLSL it was
// written as: the built module is, word for word, what that GLSL compiles
// to, and goes through everything a gathered source does under that text
static usz s_built = 0;

template <typename T, typename Text>
static void built_compute_source(Text&& text)
{
	const usz before = s_corpus.size();
	T task;
	CHECK(s_corpus.size() == before + 1 && s_corpus.back().source.empty() && !task.m_spirv.empty());
	if (s_corpus.size() != before + 1)
		return;

	gathered& g = s_corpus.back();
	g.source = text(task);
	g.domain = ::glsl::glsl_compute_program;

	std::vector<u32> parsed;
	std::string source = g.source;
	CHECK(spirv::compile_glsl_to_spv_core(parsed, source, g.domain, ::glsl::glsl_rules_vulkan));
	CHECK(parsed == g.compiled);
	s_built++;

	// and the renderer takes it as it takes a written one
	vk::glsl::shader shader;
	shader.create(g.domain, task.m_spirv);
	CHECK(shader.compile() != VK_NULL_HANDLE);
	shader.destroy();
}

template <typename T>
static void overlay_source()
{
	T pass;
	compile_now(::glsl::program_domain::glsl_vertex_program, pass.vs_src);
	compile_now(::glsl::program_domain::glsl_fragment_program, pass.fs_src);
}

// A game's own fragment programs, written here as the console's microcode:
// what the decompiler makes of them is the bulk of what a game compiles
struct fp_inst
{
	OPDEST d{};
	SRC0 a{};
	SRC1 b{};
	SRC2 c{};
	bool literal = false;

	fp_inst(u32 opcode, u32 dest = 0, u32 mask = 0xf)
	{
		d.opcode = opcode & 0x3f;
		b.opcode_hi = opcode >> 6;
		d.dest_reg = dest;
		d.write_mask = mask;
		a.exec_if_lt = a.exec_if_eq = a.exec_if_gr = 1;
		a.swizzle_y = a.cond_swizzle_y = b.swizzle_y = c.swizzle_y = 1;
		a.swizzle_z = a.cond_swizzle_z = b.swizzle_z = c.swizzle_z = 2;
		a.swizzle_w = a.cond_swizzle_w = b.swizzle_w = c.swizzle_w = 3;
	}

	// Operand n reads a temporary, an input (one per instruction) or the
	// literal that follows the instruction
	fp_inst& temp(int n, u32 index, bool half = false)
	{
		SRC_Common s{ .HEX = word(n) };
		s.reg_type = RSX_FP_REGISTER_TYPE_TEMP;
		s.tmp_reg_index = index;
		s.fp16 = half;
		word(n) = s.HEX;
		return *this;
	}

	fp_inst& input(int n, u32 attr)
	{
		SRC_Common s{ .HEX = word(n) };
		s.reg_type = RSX_FP_REGISTER_TYPE_INPUT;
		word(n) = s.HEX;
		d.src_attr_reg_num = attr;
		return *this;
	}

	fp_inst& constant(int n)
	{
		SRC_Common s{ .HEX = word(n) };
		s.reg_type = RSX_FP_REGISTER_TYPE_CONSTANT;
		word(n) = s.HEX;
		literal = true;
		return *this;
	}

	fp_inst& swizzle(int n, u32 x, u32 y, u32 z, u32 w)
	{
		SRC_Common s{ .HEX = word(n) };
		s.swizzle_x = x, s.swizzle_y = y, s.swizzle_z = z, s.swizzle_w = w;
		word(n) = s.HEX;
		return *this;
	}

	u32& word(int n) { return n == 0 ? a.HEX : n == 1 ? b.HEX : c.HEX; }
};

// Whether the programs are built for the renderer interpolating varyings
// itself, which it does where the driver has barycentric inputs
static bool s_interpolate = false;

struct fp_program
{
	std::vector<u32> words;
	RSXFragmentProgram prog;

	fp_program()
	{
		prog.ctrl = CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS | (s_interpolate ? u32{RSX_SHADER_CONTROL_ATTRIBUTE_INTERPOLATION} : 0u);
		prog.mrt_buffers_count = 1;
		for (u32 i = 0; i < 16; i++)
			prog.texture_state.set_dimension(rsx::texture_dimension_extended::texture_dimension_2d, i);
	}

	static u32 stored(u32 host)
	{
		return ((host & 0x00FF00FF) << 8) | ((host & 0xFF00FF00) >> 8);
	}

	u32 pc() const { return ::size32(words) / 4; }

	u32 add(const fp_inst& i)
	{
		const u32 at = pc();
		for (u32 w : {i.d.HEX, i.a.HEX, i.b.HEX, i.c.HEX})
			words.push_back(stored(w));
		if (i.literal)
			for (f32 f : {0.25f, 0.5f, 2.f, 1.f})
				words.push_back(stored(std::bit_cast<u32>(f)));
		return at;
	}

	// Where a branch at `at` goes, known once its body is written
	void patch(u32 at, u32 else_pc, u32 end_pc)
	{
		SRC1 b{ .HEX = stored(words[at * 4 + 2]) };
		b.else_offset = else_pc << 2;
		words[at * 4 + 2] = stored(b.HEX);
		words[at * 4 + 3] = stored(end_pc << 2);
	}

	void run()
	{
		OPDEST d{ .HEX = stored(words[last * 4]) };
		d.end = 1;
		words[last * 4] = stored(d.HEX);
		prog.data = RSXFragmentProgram::data_storage_helper(words.data());
		prog.ucode_length = ::size32(words) * 4;
		prog.total_length = prog.ucode_length;
		prog.valid = true;

		VKFragmentProgram out;
		out.Decompile(prog);
		out.Compile();
		CHECK(out.handle != VK_NULL_HANDLE);
	}

	u32 last = 0;
	u32 end(const fp_inst& i) { return last = add(i); }
};

// One program around a sampled texture, a literal and a varying: `shape`
// sets the program up, `last` changes the instruction that writes the colour
template <typename Shape, typename Last>
static void fragment_variant(Shape&& shape, Last&& last)
{
	fp_program p;
	shape(p);
	p.add(fp_inst(RSX_FP_OPCODE_MOV, 1).input(0, 4));
	fp_inst tex(RSX_FP_OPCODE_TEX, 2);
	tex.temp(0, 1);
	p.add(tex);
	p.add(fp_inst(RSX_FP_OPCODE_MOV, 3).constant(0));
	fp_inst i(RSX_FP_OPCODE_MAD, 0);
	i.temp(0, 2).temp(1, 3).temp(2, 1);
	last(i);
	p.end(i);
	p.run();
}

static void fragment_programs()
{
	using namespace rsx::assembler;
	const auto plain = [](fp_program&) {};
	const auto same = [](fp_inst&) {};

	// Each instruction the hardware has, alone: its operands a varying, a
	// literal and a temporary
	for (u32 opcode = RSX_FP_OPCODE_MOV; opcode <= RSX_FP_OPCODE_FENCB; opcode++)
	{
		const auto op = static_cast<FP_opcode>(opcode);
		if (!FP::is_instruction_valid(op) || opcode == 0x30 || opcode == 0x32 || opcode == 0x3f)
			continue;

		fp_program p;
		p.add(fp_inst(RSX_FP_OPCODE_MOV, 1).input(0, 4));
		p.add(fp_inst(RSX_FP_OPCODE_MOV, 2).constant(0));
		fp_inst i(opcode, 0);
		i.temp(0, 1).temp(1, 2).temp(2, 1);
		i.d.tex_num = opcode & 3;
		p.end(i);
		p.run();
	}

	// What an instruction can ask of its result and its operands: the
	// destination's width, precision, clamp and write mask, a scale, and each
	// operand negated, made absolute, reordered or narrowed
	for (u32 prec = 0; prec < 4; prec++)
		fragment_variant(plain, [&](fp_inst& i) { i.d.prec = prec; });
	for (u32 mask = 1; mask < 15; mask += 3)
		fragment_variant(plain, [&](fp_inst& i) { i.d.write_mask = mask; });
	for (u32 scale = 1; scale < 8; scale++)
		fragment_variant(plain, [&](fp_inst& i) { i.b.scale = scale; });
	for (u32 mod = 1; mod < 6; mod++)
		fragment_variant(plain, [&](fp_inst& i) { i.b.src0_prec_mod = mod; i.b.src1_prec_mod = 5 - mod; i.b.src2_prec_mod = mod; });
	fragment_variant(plain, [](fp_inst& i) { i.d.saturate = 1; });
	fragment_variant(plain, [](fp_inst& i) { i.d.fp16 = 1; i.d.dest_reg = 5; });
	fragment_variant(plain, [](fp_inst& i) { i.a.neg = 1; i.b.abs = 1; i.c.neg = 1; i.c.abs = 1; });
	fragment_variant(plain, [](fp_inst& i) { i.a.abs = 1; i.b.neg = 1; });
	fragment_variant(plain, [](fp_inst& i) { i.swizzle(0, 3, 2, 1, 0).swizzle(1, 0, 0, 0, 0).swizzle(2, 1, 1, 3, 3); });
	fragment_variant(plain, [](fp_inst& i) { i.temp(0, 2, true).temp(2, 7, true); });

	// The colour written as 16-bit, with depth, and to each count of targets
	fragment_variant([](fp_program& p) { p.prog.ctrl &= ~CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS; }, same);
	fragment_variant([](fp_program& p) { p.prog.ctrl |= CELL_GCM_SHADER_CONTROL_DEPTH_EXPORT; },
		[](fp_inst& i) { i.d.dest_reg = 1; });
	for (u32 targets = 2; targets <= 4; targets++)
	{
		fragment_variant([&](fp_program& p) { p.prog.mrt_buffers_count = targets; },
			[&](fp_inst& i) { i.d.dest_reg = targets; });
		fragment_variant([&](fp_program& p) { p.prog.mrt_buffers_count = targets; p.prog.ctrl &= ~CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS; },
			[&](fp_inst& i) { i.d.dest_reg = targets * 2; i.d.fp16 = 1; });
	}

	// Each thing the renderer's state adds to a program, alone and all at once
	u32 every = 0;
	for (u32 bit : {u32{RSX_SHADER_CONTROL_USES_KIL}, u32{RSX_SHADER_CONTROL_FLAT_SHADING}, u32{RSX_SHADER_CONTROL_INSTANCED_CONSTANTS},
		u32{RSX_SHADER_CONTROL_8BIT_FRAMEBUFFER}, u32{RSX_SHADER_CONTROL_SRGB_FRAMEBUFFER}, u32{RSX_SHADER_CONTROL_TEXTURE_ALPHA_KILL},
		u32{RSX_SHADER_CONTROL_ALPHA_TEST}, u32{RSX_SHADER_CONTROL_POLYGON_STIPPLE}, u32{RSX_SHADER_CONTROL_ALPHA_TO_COVERAGE},
		u32{RSX_SHADER_CONTROL_DISABLE_EARLY_Z}, u32{RSX_SHADER_CONTROL_TEXTURE_FORMAT_CONVERT}, u32{RSX_SHADER_CONTROL_EMULATE_DEPTH_COMPARE},
		u32{RSX_SHADER_CONTROL_ROP_MULTISAMPLED}, u32{RSX_SHADER_CONTROL_ROP_OUTPUT_REMAP}, u32{RSX_SHADER_CONTROL_PROGRAMMABLE_BLENDING}})
	{
		every |= bit;
		fragment_variant([&](fp_program& p) { p.prog.ctrl |= bit; }, same);
	}
	fragment_variant([&](fp_program& p) { p.prog.ctrl |= every; p.prog.mrt_buffers_count = 4; }, same);
	fragment_variant([&](fp_program& p) { p.prog.ctrl |= RSX_SHADER_CONTROL_PROGRAMMABLE_BLENDING | RSX_SHADER_CONTROL_ROP_MULTISAMPLED | RSX_SHADER_CONTROL_EMULATE_DEPTH_COMPARE; p.prog.mrt_buffers_count = 3; }, same);
	fragment_variant([](fp_program& p) { p.prog.two_sided_lighting = true; }, [](fp_inst& i) { i.input(2, 1); });

	// Each varying, as it comes and with the texture coordinate overrides
	for (u32 attr = 0; attr < 15; attr++)
	{
		fragment_variant(plain, [&](fp_inst& i) { i.input(2, attr); });
	}
	fragment_variant([](fp_program& p) { p.prog.texcoord_control_mask = 0x3ff; }, [](fp_inst& i) { i.input(2, 5); });
	fragment_variant([](fp_program& p) { p.prog.texcoord_control_mask = 0x3ff; }, [](fp_inst& i) { i.input(2, 5).swizzle(2, 0, 1, 0, 1); });
	fragment_variant([](fp_program& p) { p.prog.texcoord_control_mask = 0x3ff0000; }, [](fp_inst& i) { i.input(2, 6); });
	fragment_variant([](fp_program& p) { p.prog.texcoord_control_mask = 0x3ff03ff; }, [](fp_inst& i) { i.input(2, 6); });
	fragment_variant(plain, [](fp_inst& i) { i.input(2, 7); i.c.perspective_corr = 1; });
	fragment_variant([](fp_program& p) { p.prog.texcoord_control_mask = 0x3ff; }, [](fp_inst& i) { i.input(2, 7); i.c.perspective_corr = 1; });

	// Each kind of texture under each way of sampling it
	for (u32 dimension = 0; dimension < 4; dimension++)
	{
		for (u32 kind = 0; kind < 4; kind++)
		{
			// A multisampled texture is a render target: two dimensions
			if (kind == 3 && dimension != 1)
				continue;
			// and no depth texture has three
			if (kind == 1 && dimension == 3)
				continue;
			for (u32 opcode : {u32{RSX_FP_OPCODE_TEX}, u32{RSX_FP_OPCODE_TXP}, u32{RSX_FP_OPCODE_TXD}, u32{RSX_FP_OPCODE_TXL}, u32{RSX_FP_OPCODE_TXB}})
			{
				fp_program p;
				p.prog.texture_state.set_dimension(rsx::texture_dimension_extended{static_cast<u8>(dimension)}, 3);
				if (kind == 1)
					p.prog.texture_state.shadow_textures = 1 << 3;
				if (kind == 2)
					p.prog.texture_state.redirected_textures = 1 << 3;
				if (kind == 3)
					p.prog.texture_state.multisampled_textures = 1 << 3;
				p.add(fp_inst(RSX_FP_OPCODE_MOV, 1).input(0, 4));
				p.add(fp_inst(RSX_FP_OPCODE_MOV, 2).constant(0));
				fp_inst i(opcode, 0);
				i.temp(0, 1).temp(1, 2).temp(2, 1);
				i.d.tex_num = 3;
				i.d.exp_tex = kind == 0 && opcode == RSX_FP_OPCODE_TXP;
				p.end(i);
				p.run();
			}
		}
	}

	// The condition registers: one instruction sets them, others run or not
	// on them, a fragment is killed on them
	for (u32 reg = 0; reg < 2; reg++)
	{
		fp_program p;
		p.prog.ctrl |= RSX_SHADER_CONTROL_USES_KIL;
		fp_inst set(RSX_FP_OPCODE_MOV, 1);
		set.input(0, 5);
		set.d.set_cond = 1;
		set.a.cond_mod_reg_index = reg;
		p.add(set);
		fp_inst test(RSX_FP_OPCODE_SLT, 2);
		test.temp(0, 1).constant(1);
		test.d.set_cond = 1, test.d.no_dest = 1;
		test.a.cond_mod_reg_index = reg;
		p.add(test);
		fp_inst kil(RSX_FP_OPCODE_KIL);
		kil.a.exec_if_gr = kil.a.exec_if_eq = 0;
		kil.a.cond_reg_index = reg;
		kil.a.cond_swizzle_x = kil.a.cond_swizzle_y = kil.a.cond_swizzle_z = kil.a.cond_swizzle_w = 3;
		p.add(kil);
		fp_inst i(RSX_FP_OPCODE_MOV, 0);
		i.temp(0, 1);
		i.a.exec_if_lt = 0;
		i.a.cond_reg_index = reg;
		i.a.cond_swizzle_x = 1, i.a.cond_swizzle_y = 0;
		p.add(i);
		fp_inst last(RSX_FP_OPCODE_ADD, 0, 0x7);
		last.temp(0, 0).temp(1, 1);
		last.a.exec_if_eq = 0;
		last.a.cond_reg_index = reg;
		p.end(last);
		p.run();
	}

	// Branches: if, if and else, one inside another, the two loops, a break
	for (u32 shape = 0; shape < 6; shape++)
	{
		fp_program p;
		fp_inst set(RSX_FP_OPCODE_MOV, 1);
		set.input(0, 4);
		set.d.set_cond = 1;
		p.add(set);
		p.add(fp_inst(RSX_FP_OPCODE_MOV, 0).constant(0));

		if (shape < 3)
		{
			fp_inst branch(RSX_FP_OPCODE_IFE);
			branch.a.exec_if_gr = 0;
			const u32 at = p.add(branch);
			p.add(fp_inst(RSX_FP_OPCODE_ADD, 0).temp(0, 0).temp(1, 1));
			u32 otherwise = p.pc();
			if (shape == 2)
			{
				fp_inst inner(RSX_FP_OPCODE_IFE);
				inner.a.exec_if_lt = 0;
				inner.a.cond_swizzle_x = 2;
				const u32 inner_at = p.add(inner);
				p.add(fp_inst(RSX_FP_OPCODE_MUL, 0).temp(0, 0).temp(1, 1));
				p.patch(inner_at, p.pc(), p.pc());
				otherwise = p.pc();
			}
			if (shape >= 1)
				p.add(fp_inst(RSX_FP_OPCODE_MAX, 0).temp(0, 0).temp(1, 1));
			p.patch(at, otherwise, p.pc());
		}
		else
		{
			fp_inst loop(shape == 3 ? RSX_FP_OPCODE_REP : RSX_FP_OPCODE_LOOP);
			loop.b.HEX = 0;
			loop.b.rep_count = 4, loop.b.init_counter = shape == 3 ? 0 : 1, loop.b.increment = 1;
			loop.b.opcode_hi = 1;
			const u32 at = p.add(loop);
			p.add(fp_inst(RSX_FP_OPCODE_ADD, 0).temp(0, 0).temp(1, 1));
			if (shape == 5)
			{
				fp_inst brk(RSX_FP_OPCODE_BRK);
				brk.a.exec_if_gr = brk.a.exec_if_eq = 0;
				p.add(brk);
				p.add(fp_inst(RSX_FP_OPCODE_MUL, 0).temp(0, 0).temp(1, 1));
			}
			SRC2 end{};
			end.end_offset = p.pc() << 2;
			p.words[at * 4 + 3] = fp_program::stored(end.HEX);
		}

		p.end(fp_inst(RSX_FP_OPCODE_MIN, 0).temp(0, 0).temp(1, 1));
		p.run();
	}

	// And under each precision setting
	for (auto level : {gpu_preset_level::low, gpu_preset_level::ultra})
	{
		g_cfg.video.shader_precision.set(level);
		fragment_variant(plain, same);
		fragment_variant(plain, [](fp_inst& i) { i.d.fp16 = 1; i.d.prec = 1; i.temp(0, 2, true); });
		fragment_variant([](fp_program& p) { p.prog.ctrl &= ~CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS; p.prog.ctrl |= RSX_SHADER_CONTROL_8BIT_FRAMEBUFFER; }, same);
	}
	g_cfg.video.shader_precision.set(gpu_preset_level::high);
}

// And its vertex programs
struct vp_inst
{
	D0 d0{};
	D1 d1{};
	D2 d2{};
	D3 d3{};
	SRC s[3]{};

	vp_inst(u32 vec, u32 sca = RSX_SCA_OPCODE_NOP)
	{
		d1.vec_opcode = vec;
		d1.sca_opcode = sca;
		d0.cond = 7;
		d0.mask_y = 1, d0.mask_z = 2, d0.mask_w = 3;
		d0.dst_tmp = 0x3f;
		d3.sca_dst_tmp = 0x3f;
		d3.dst = 0x1f;
		d3.vec_writemask_x = d3.vec_writemask_y = d3.vec_writemask_z = d3.vec_writemask_w = 1;
		d3.sca_writemask_x = d3.sca_writemask_y = d3.sca_writemask_z = d3.sca_writemask_w = 1;
		for (SRC& src : s)
		{
			src.reg_type = RSX_VP_REGISTER_TYPE_TEMP;
			src.swz_y = 1, src.swz_z = 2, src.swz_w = 3;
		}
	}

	// The result goes to an output register, a temporary, or both
	vp_inst& out(u32 index)
	{
		d3.dst = index;
		d0.vec_result = d1.vec_opcode != RSX_VEC_OPCODE_NOP;
		return *this;
	}

	vp_inst& tmp(u32 index)
	{
		(d1.vec_opcode != RSX_VEC_OPCODE_NOP ? d0.dst_tmp : d3.sca_dst_tmp) = index;
		return *this;
	}

	vp_inst& temp(int n, u32 index)
	{
		s[n].reg_type = RSX_VP_REGISTER_TYPE_TEMP;
		s[n].tmp_src = index;
		return *this;
	}

	vp_inst& input(int n, u32 attr)
	{
		s[n].reg_type = RSX_VP_REGISTER_TYPE_INPUT;
		d1.input_src = attr;
		return *this;
	}

	vp_inst& constant(int n, u32 index)
	{
		s[n].reg_type = RSX_VP_REGISTER_TYPE_CONSTANT;
		d1.const_src = index;
		return *this;
	}

	vp_inst& jump(u32 to)
	{
		d0.iaddrh2 = (to >> 9) & 1;
		pending_iaddrh = (to >> 3) & 0x3f;
		pending_iaddrl = to & 7;
		is_jump = true;
		return *this;
	}

	u32 pending_iaddrh = 0, pending_iaddrl = 0;
	bool is_jump = false;
};

struct vp_program
{
	std::vector<u32> words;
	RSXVertexProgram prog;

	vp_program()
	{
		prog.output_mask = 0xffffffff;
		for (u32 i = 0; i < 4; i++)
			prog.texture_state.set_dimension(rsx::texture_dimension_extended::texture_dimension_2d, i);
	}

	u32 pc() const { return ::size32(words) / 4; }

	void add(vp_inst i, bool end = false)
	{
		i.d2.src0l = i.s[0].src0l;
		i.d1.src0h = i.s[0].src0h;
		i.d2.src1 = i.s[1].src1;
		i.d3.src2l = i.s[2].src2l;
		i.d2.src2h = i.s[2].src2h;
		if (i.is_jump)
		{
			// The address shares its bits with the third operand
			i.d2.iaddrh = i.pending_iaddrh;
			i.d3.iaddrl = i.pending_iaddrl;
		}
		i.d3.end = end;
		for (u32 w : {i.d0.HEX, i.d1.HEX, i.d2.HEX, i.d3.HEX})
			words.push_back(w);
	}

	void run()
	{
		words.resize(rsx::max_vertex_program_instructions * 4);
		program_hash_util::vertex_program_utils::analyse_vertex_program(words.data(), 0, prog);

		VKVertexProgram out;
		out.Decompile(prog);
		out.Compile();
		CHECK(out.handle != VK_NULL_HANDLE);
	}
};

// One program: a position from the first input and a constant, then `body`,
// then the instruction that ends it
template <typename Shape, typename Body>
static void vertex_variant(Shape&& shape, Body&& body)
{
	vp_program p;
	shape(p);
	p.add(vp_inst(RSX_VEC_OPCODE_MOV).tmp(1).input(0, 0));
	p.add(vp_inst(RSX_VEC_OPCODE_MOV).tmp(2).constant(0, 4));
	body(p);
	p.add(vp_inst(RSX_VEC_OPCODE_MAD).out(0).temp(0, 1).temp(1, 2).temp(2, 1), true);
	p.run();
}

static void vertex_programs()
{
	const auto plain = [](vp_program&) {};

	// Each vector and each scalar instruction, alone
	for (u32 opcode = RSX_VEC_OPCODE_MOV; opcode <= RSX_VEC_OPCODE_SSG; opcode++)
		vertex_variant(plain, [&](vp_program& p) { p.add(vp_inst(opcode).tmp(3).out(1).temp(0, 1).temp(1, 2).temp(2, 1)); });
	for (u32 opcode : {u32{RSX_SCA_OPCODE_MOV}, u32{RSX_SCA_OPCODE_RCP}, u32{RSX_SCA_OPCODE_RCC}, u32{RSX_SCA_OPCODE_RSQ},
		u32{RSX_SCA_OPCODE_EXP}, u32{RSX_SCA_OPCODE_LOG}, u32{RSX_SCA_OPCODE_LIT}, u32{RSX_SCA_OPCODE_LG2}, u32{RSX_SCA_OPCODE_EX2},
		u32{RSX_SCA_OPCODE_SIN}, u32{RSX_SCA_OPCODE_COS}})
	{
		vertex_variant(plain, [&](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_NOP, opcode).tmp(3).temp(2, 1)); });
		vertex_variant(plain, [&](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_NOP, opcode).out(2).temp(2, 1)); });
	}

	// A vector and a scalar instruction in one word
	vertex_variant(plain, [](vp_program& p)
	{
		vp_inst i(RSX_VEC_OPCODE_ADD, RSX_SCA_OPCODE_RSQ);
		i.d0.dst_tmp = 3, i.d3.sca_dst_tmp = 4;
		p.add(i.temp(0, 1).temp(2, 2));
	});

	// The texture fetch, from each kind of texture
	for (u32 dimension = 0; dimension < 4; dimension++)
	{
		for (u32 multisampled = 0; multisampled < 2; multisampled++)
		{
			if (multisampled && dimension != 1)
				continue;
			vertex_variant([&](vp_program& p)
			{
				p.prog.texture_state.set_dimension(rsx::texture_dimension_extended{static_cast<u8>(dimension)}, 2);
				p.prog.texture_state.multisampled_textures = multisampled << 2;
			}, [](vp_program& p)
			{
				vp_inst i(RSX_VEC_OPCODE_TXL);
				i.tmp(3).temp(0, 1);
				p.add(i);
				// The unit's number sits in the second operand's bits
				D2 d2{ .HEX = p.words[p.words.size() - 2] };
				d2.tex_num = 2;
				p.words[p.words.size() - 2] = d2.HEX;
			});
		}
	}

	// Each input and each output
	for (u32 index = 0; index < 16; index++)
		vertex_variant(plain, [&](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_MOV).out(index).input(0, index)); });
	vertex_variant([](vp_program& p) { p.prog.output_mask = 0x5; },
		[](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_MOV).out(3).input(0, 3)); p.add(vp_inst(RSX_VEC_OPCODE_MOV).out(7).input(0, 8)); });

	// What an instruction can ask: a clamp, a write mask, operands negated,
	// made absolute or reordered, and a constant found through the address
	// register
	vertex_variant(plain, [](vp_program& p) { vp_inst i(RSX_VEC_OPCODE_ADD); i.d0.staturate = 1; p.add(i.tmp(3).temp(0, 1).temp(2, 2)); });
	vertex_variant(plain, [](vp_program& p) { vp_inst i(RSX_VEC_OPCODE_ADD); i.d3.vec_writemask_y = i.d3.vec_writemask_w = 0; p.add(i.tmp(3).out(4).temp(0, 1).temp(2, 2)); });
	vertex_variant(plain, [](vp_program& p) { vp_inst i(RSX_VEC_OPCODE_MAD); i.d0.src0_abs = i.d0.src2_abs = 1; i.s[1].neg = i.s[2].neg = 1; p.add(i.tmp(3).temp(0, 1).temp(1, 2).temp(2, 1)); });
	vertex_variant(plain, [](vp_program& p) { vp_inst i(RSX_VEC_OPCODE_MUL); i.d0.src1_abs = 1; i.s[0].swz_x = 3, i.s[0].swz_w = 0; i.s[1].swz_y = i.s[1].swz_z = i.s[1].swz_w = 0; p.add(i.tmp(3).temp(0, 1).temp(1, 2)); });
	for (u32 reg = 0; reg < 2; reg++)
	{
		vertex_variant(plain, [&](vp_program& p)
		{
			vp_inst arl(RSX_VEC_OPCODE_ARL);
			arl.d0.dst_tmp = reg;
			p.add(arl.temp(0, 1));
			vp_inst i(RSX_VEC_OPCODE_MOV);
			i.d3.index_const = 1;
			i.d0.addr_reg_sel_1 = reg;
			i.d0.addr_swz = reg * 2;
			p.add(i.tmp(3).constant(0, 20));
		});
	}
	for (u32 index : {u32{0}, u32{255}, u32{467}})
		vertex_variant(plain, [&](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_MOV).tmp(3).constant(0, index)); });
	vertex_variant([](vp_program& p) { p.prog.ctrl |= RSX_SHADER_CONTROL_INSTANCED_CONSTANTS; },
		[](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_MOV).tmp(3).constant(0, 9)); });
	vertex_variant([](vp_program& p) { p.prog.ctrl |= RSX_SHADER_CONTROL_FLAT_SHADING; },
		[](vp_program& p) { p.add(vp_inst(RSX_VEC_OPCODE_MOV).out(1).input(0, 3)); p.add(vp_inst(RSX_VEC_OPCODE_MOV).out(3).input(0, 3)); });

	// The condition registers: set by one instruction, tested by another
	for (u32 cond = 0; cond < 7; cond++)
	{
		vertex_variant(plain, [&](vp_program& p)
		{
			vp_inst set(RSX_VEC_OPCODE_MOV);
			set.d0.cond_update_enable_0 = set.d0.cond_update_enable_1 = 1;
			set.d0.cond_reg_sel_1 = cond & 1;
			p.add(set.tmp(3).temp(0, 1));
			vp_inst i(RSX_VEC_OPCODE_ADD);
			i.d0.cond_test_enable = 1;
			i.d0.cond = cond;
			i.d0.cond_reg_sel_1 = cond & 1;
			i.d0.mask_x = cond & 3, i.d0.mask_w = 1;
			p.add(i.tmp(3).temp(0, 1).temp(2, 2));
		});
	}

	// Branches: on a condition and on the program's switches. Not the calls
	// (CAL, CLI, CLB): what the decompiler writes for those does not compile
	// with either compiler - its own "$ifcond" placeholder left in the
	// source, or a return without its semicolon
	for (u32 opcode : {u32{RSX_SCA_OPCODE_BRI}, u32{RSX_SCA_OPCODE_BRB}})
	{
		for (u32 taken = 0; taken < 2; taken++)
		{
			vp_program p;
			vp_inst set(RSX_VEC_OPCODE_MOV);
			set.d0.cond_update_enable_0 = set.d0.cond_update_enable_1 = 1;
			p.add(set.tmp(1).input(0, 0));
			p.add(vp_inst(RSX_VEC_OPCODE_MOV).tmp(2).constant(0, 4));

			vp_inst branch(RSX_VEC_OPCODE_NOP, opcode);
			if (opcode == RSX_SCA_OPCODE_BRI)
			{
				branch.d0.cond_test_enable = 1;
				branch.d0.cond = taken ? 1 : 4;
			}
			// over one instruction
			branch.jump(4);
			p.add(branch);
			if (opcode == RSX_SCA_OPCODE_BRB)
			{
				D3 d3{ .HEX = p.words.back() };
				d3.branch_index = 3 + taken;
				d3.brb_cond_true = taken;
				p.words.back() = d3.HEX;
			}
			p.add(vp_inst(RSX_VEC_OPCODE_ADD).tmp(2).temp(0, 1).temp(2, 2));
			p.add(vp_inst(RSX_VEC_OPCODE_MAD).out(0).temp(0, 1).temp(1, 2).temp(2, 1), true);
			p.run();
		}
	}
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

// The on-disk shader cache is one file, a pack: a pipeline stored comes
// back with its programs, in the session that stored it and - its fragment
// program then lent from the pack as it was read, not copied - in a later
// one; storing what is there writes nothing; a cache of files, as it was
// kept, is moved into the pack and its pipeline files go.
namespace shaders_cache_test
{
	struct storage
	{
		static u64 fnv(const void* data, usz size, u64 seed)
		{
			u64 h = 0xcbf29ce484222325ull ^ seed;
			for (usz i = 0; i < size; i++)
				h = (h ^ static_cast<const u8*>(data)[i]) * 0x100000001b3ull;
			return h;
		}

		u64 get_hash(const RSXVertexProgram& vp) const { return fnv(vp.data.data(), vp.data.size() * 4, 1); }
		u64 get_hash(const RSXFragmentProgram& fp) const { return fnv(fp.get_data(), fp.ucode_length, 2); }
		u64 get_hash(const u64& pipeline) const { return pipeline * 0x9e3779b97f4a7c15ull; }
	};

	struct cache : rsx::shaders_cache<u64, storage>
	{
		using base = rsx::shaders_cache<u64, storage>;
		using base::pipeline_data;
		using base::root_path;
		using base::m_pack;
		using base::m_pack_file;
		using base::open_pack;
		using base::pipelines;
		using base::read_pipeline;
		using base::unpack;
		using base::pack;
		using base::import_files;
		using base::pipelines_path;

		storage m_fake;

		cache(const std::string& root)
			: base(m_fake, "test", "v1")
		{
			// Wherever the emulator would have put it, here
			object_pack_close(&m_pack);
			m_pack_file.close();
			root_path = root;
			open_pack();
		}
	};

	constexpr u32 count = 40;

	static RSXVertexProgram vertex(u32 i)
	{
		RSXVertexProgram vp{};
		for (u32 k = 0; k < i + 3; k++)
			vp.data.push_back(0x1000 * i + k);
		vp.ctrl = i;
		return vp;
	}

	static std::vector<u8> fragment_bytes(u32 i)
	{
		std::vector<u8> bytes(8 * (i + 1));
		for (usz k = 0; k < bytes.size(); k++)
			bytes[k] = static_cast<u8>(i * 7 + k);
		return bytes;
	}

	// Every pipeline of the cache, each once, with the programs it was stored with
	static void verify(cache& c, u32 expected, bool lent)
	{
		const auto records = c.pipelines();
		CHECK(records.size() == expected);

		std::set<u64> seen;
		u32 wrong = 0, borrowed = 0;

		for (const object_pack_entry* record : records)
		{
			cache::pipeline_data pdata{};
			if (!c.read_pipeline(record, pdata))
			{
				wrong++;
				continue;
			}

			auto [pipeline, vp, fp] = c.unpack(pdata);
			const u32 i = static_cast<u32>(pipeline);
			seen.insert(pipeline);

			const auto bytes = fragment_bytes(i);
			wrong += i >= count || vp.data != vertex(i).data || vp.ctrl != i;
			wrong += fp.ucode_length != bytes.size() || !fp.get_data() || std::memcmp(fp.get_data(), bytes.data(), bytes.size()) != 0;

			const u8* at = static_cast<const u8*>(fp.get_data());
			borrowed += c.m_pack.image && at >= c.m_pack.image && at < c.m_pack.image + c.m_pack.lent_len;
		}

		CHECK(wrong == 0);
		CHECK(seen.size() == expected);
		CHECK(borrowed == (lent ? expected : 0));
	}

	static void run()
	{
		const std::string root = fmt::format("%s/test_shaders_cache_%u", std::filesystem::temp_directory_path().string(), static_cast<u32>(getpid()));
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);

		std::vector<cache::pipeline_data> stored;
		{
			cache c(root);
			const std::string file = c.pipelines_path() + "/shaders.pack";

			const auto store_all = [&]()
			{
				for (u32 i = 0; i < count; i++)
				{
					const RSXVertexProgram vp = vertex(i);
					auto bytes = fragment_bytes(i);
					RSXFragmentProgram fp{};
					fp.data = bytes.data();
					fp.ucode_length = ::size32(bytes);
					c.store(u64{i}, vp, fp);
					if (stored.size() < count)
						stored.push_back(c.pack(u64{i}, vp, fp));
				}
			};

			store_all();
			const u64 size = fs::file(file).size();
			CHECK(size > count * sizeof(cache::pipeline_data));

			// A file for all of it, and what is there is not written again
			CHECK(!fs::is_dir(root + "/raw"));
			usz files = 0;
			for (auto& e : std::filesystem::recursive_directory_iterator(root))
				files += e.is_regular_file();
			CHECK(files == 1);
			store_all();
			CHECK(fs::file(file).size() == size);

			verify(c, count, false);
		}
		{
			cache c(root);
			verify(c, count, true);
		}

		// As the cache was kept: a file a pipeline, a file a program
		const std::string old_root = root + "_files";
		std::filesystem::remove_all(old_root);
		std::filesystem::create_directories(old_root + "/pipelines/test/v1");
		std::filesystem::create_directories(old_root + "/raw");
		for (u32 i = 0; i < 10; i++)
		{
			const cache::pipeline_data& data = stored[i];
			fs::write_file(fmt::format("%s/raw/%llX.vp", old_root, data.vertex_program_hash), fs::rewrite, vertex(i).data);
			fs::write_file(fmt::format("%s/raw/%llX.fp", old_root, data.fragment_program_hash), fs::rewrite, fragment_bytes(i));
			fs::write_file(fmt::format("%s/pipelines/test/v1/%llX+%u.bin", old_root, data.vertex_program_hash, i), fs::rewrite, &data, sizeof(data));
		}
		fs::write_file(old_root + "/pipelines/test/v1/short.bin", fs::rewrite, "no pipeline", 11);
		{
			cache c(old_root);
			c.import_files();
			verify(c, 10, false);
			usz left = 0;
			for (auto& e : std::filesystem::directory_iterator(old_root + "/pipelines/test/v1"))
				left += e.path().extension() == ".bin";
			CHECK(left == 0);
		}
		{
			cache c(old_root);
			c.import_files();
			verify(c, 10, true);
		}

		std::filesystem::remove_all(root);
		std::filesystem::remove_all(old_root);
	}
}

int main()
{
	shaders_cache_test::run();

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

	const bool can_run = shader_run_start(gpu, graphics) != 0;
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
	built_compute_source<vk::cs_aggregator>([](const vk::cs_aggregator& task)
	{
		return fmt::format(
			"#version 450\n"
			"layout(local_size_x = %u, local_size_y = 1, local_size_z = 1) in;\n\n"
			"layout(set=0, binding=0, std430) readonly buffer ssbo0{ uint src[]; };\n"
			"layout(set=0, binding=1, std430) writeonly buffer ssbo1{ uint result; };\n\n"
			"void main()\n"
			"{\n"
			"	if (gl_GlobalInvocationID.x < src.length())\n"
			"	{\n"
			"		atomicAdd(result, src[gl_GlobalInvocationID.x]);\n"
			"	}\n"
			"}\n", task.optimal_group_size);
	});
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

	// The fragment programs both ways: with the varyings as the driver
	// interpolates them (which a driver without barycentric inputs can also
	// run), and as the renderer does it itself
	fragment_programs();
	s_interpolate = true;
	fragment_programs();
	s_interpolate = false;
	vertex_programs();

	// The decompilers write differently by what the driver is and has: the
	// programs again as other GPUs get them, so that what this driver does
	// not make them write is compiled, compared and run too
	{
		vk::gpu_shader_types_support& types = const_cast<vk::gpu_shader_types_support&>(device.get_shader_types_support());
		vk::gpu_formats_support& formats = const_cast<vk::gpu_formats_support&>(device.get_formats_support());
		const vk::gpu_shader_types_support had_types = types;
		const vk::gpu_formats_support had_formats = formats;
		const vk::driver_vendor had_vendor = vk::g_driver_vendor;

		s_first_variant = s_corpus.size();
		struct kind { vk::driver_vendor vendor; bool float64, float16, d24; gpu_preset_level precision; };
		for (const kind& k : {
			kind{vk::driver_vendor::NVIDIA, true, true, true, gpu_preset_level::high},  // invariance asked for, depth in doubles, low-precision compares
			kind{vk::driver_vendor::AMD, false, false, false, gpu_preset_level::high},  // no doubles, depth compare emulated
			kind{vk::driver_vendor::NVIDIA, true, true, true, gpu_preset_level::low},   // half floats as they are
			kind{vk::driver_vendor::INTEL, true, false, false, gpu_preset_level::ultra}})
		{
			vk::g_driver_vendor = k.vendor;
			types.allow_float64 = k.float64;
			types.allow_float16 = k.float16;
			formats.d24_unorm_s8 = k.d24;
			g_cfg.video.shader_precision.set(k.precision);
			const usz before = s_corpus.size();
			fragment_programs();
			g_cfg.video.shader_precision.set(k.precision);
			vertex_programs();
			g_cfg.video.shader_precision.set(k.precision);
			shader_corpus_test::interpreter(device);
			s_variants += s_corpus.size() - before;
		}

		vk::g_driver_vendor = had_vendor;
		types = had_types;
		formats = had_formats;
		g_cfg.video.shader_precision.set(gpu_preset_level::high);
	}

	CHECK(s_corpus.size() >= 79);

	const char* dir = std::getenv("SHADER_CORPUS_DIR");
	const char* tool = std::getenv("SHADER_CORPUS_GLSLANG");
	if (tool && !*tool)
		tool = nullptr;
	s_can_run = can_run;
	if (tool)
		reference_compile_all(tool);
	for (usz i = 0; i < s_corpus.size(); i++)
		check_source(i, s_corpus[i], dir, tool);

	CHECK(s_refused == 0);
	CHECK(s_driver_refused == 0);
	CHECK(s_run_differs == 0);
	CHECK(!s_can_run || (s_ran >= 200 && s_ran_vertex >= 95 && s_ran_compute >= 40));
	CHECK(!s_can_run || !tool || (s_ran >= 1000 && s_ran_vertex >= 450));
	CHECK(s_built >= 1);
	CHECK(s_variants >= 1000);
	// Every module the renderer compiled came lent from the thread's block
	CHECK(s_compiles >= 1000 && s_lent == s_compiles);
	std::printf("test_shader_corpus: %u sources gathered, %u of them stages built by calls and the same as their GLSL compiles to; the core's compile refused %u, the driver refused %u of its modules\n",
		::size32(s_corpus), static_cast<u32>(s_built), static_cast<u32>(s_refused), static_cast<u32>(s_driver_refused));

	if (tool)
	{
		// and its modules are no larger than glslang's by more than a fiftieth
		CHECK(s_reference_failed == 0);
		CHECK(s_interface_differs == 0);
		CHECK(s_rslc_words * 50 <= s_reference_words * 51);
		std::printf("test_shader_corpus: against glslang, which refused %u: %u differ in interface or layout; %u words where glslang makes %u\n",
			static_cast<u32>(s_reference_failed), static_cast<u32>(s_interface_differs), static_cast<u32>(s_rslc_words), static_cast<u32>(s_reference_words));
		std::printf("test_shader_corpus: run from both compilers, %u fragment stages drew the same, %u vertex stages put out the same and %u compute stages left the same; %u differed, %u were not run\n",
			static_cast<u32>(s_ran), static_cast<u32>(s_ran_vertex), static_cast<u32>(s_ran_compute), static_cast<u32>(s_run_differs), static_cast<u32>(s_not_run));
	}
	else
	{
		std::printf("test_shader_corpus: no glslang to hold them to (SHADER_CORPUS_GLSLANG): %u fragment, %u vertex and %u compute stages ran; %u did not run the same twice, %u were not run\n",
			static_cast<u32>(s_ran), static_cast<u32>(s_ran_vertex), static_cast<u32>(s_ran_compute), static_cast<u32>(s_run_differs), static_cast<u32>(s_not_run));
	}

	for (const auto& [why, count] : s_not_run_why)
		std::printf("test_shader_corpus:   %u not run: %s\n", count, why.c_str());
	std::printf("test_shader_corpus: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
