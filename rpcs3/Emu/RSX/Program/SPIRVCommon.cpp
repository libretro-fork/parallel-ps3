#include "stdafx.h"

#include "SPIRVCommon.h"
#include "Emu/RSX/Program/GLSLTypes.h"

#include <gfx/shaders/rslc.h>
#include <gfx/shaders/rslc_build.h>

#include <cstdlib>
#include <cstring>
#include <memory>

// The renderer's GLSL goes to SPIR-V through libretro-common's rslc.
//
// A thread that compiles keeps one block for it: rslc's working state takes
// the first part and the module it hands back the second, so a compile takes
// nothing from the heap once the thread has compiled before. The block is as
// large as rslc lets a compile grow by default; a source that needs more -
// none the renderer makes does - is compiled again from the heap.
namespace
{
	struct compile_memory
	{
		static constexpr usz arena_size = RSLC_ARENA_CAP_DEFAULT;
		static constexpr usz result_size = 8u << 20;

		std::unique_ptr<u8[]> block{new u8[arena_size + result_size]};
		usz used = 0;

		u8* results() const { return block.get() + arena_size; }

		static void* take(void* ud, size_t len)
		{
			compile_memory* m = static_cast<compile_memory*>(ud);
			len = (len + 15) & ~usz{15};
			if (len > result_size - m->used)
				return std::malloc(len);
			void* at = m->results() + m->used;
			m->used += len;
			return at;
		}

		static void give(void* ud, void* ptr)
		{
			const compile_memory* m = static_cast<compile_memory*>(ud);
			const u8* at = static_cast<const u8*>(ptr);
			if (at < m->results() || at >= m->results() + result_size)
				std::free(ptr);
		}
	};

	// The thread's block and rslc's options. The options stay where they are
	// for as long as a builder made with them is open.
	struct compile_state
	{
		compile_memory memory;
		rslc_opts opts{};
		char err[256]{};

		// The module last made, lent to the caller until the next is begun
		rslc_result last{};
	};

	compile_state& this_thread()
	{
		thread_local std::unique_ptr<compile_state> s;
		if (!s)
			s = std::make_unique<compile_state>();
		return *s;
	}
}

namespace spirv
{
	// The options set to work in the block, from its start
	static rslc_opts& prepare()
	{
		compile_state& s = this_thread();

		// What was lent goes back: the block is used from its start again
		if (s.last.spirv)
		{
			rslc_result_free(&s.last, &s.opts);
			s.last = {};
		}

		s.memory.used = 0;
		s.err[0] = 0;
		s.opts = {};
		s.opts.dst = RSLC_DST_SPIRV;
		s.opts.err = s.err;
		s.opts.err_cap = sizeof(s.err);
		s.opts.alloc = &compile_memory::take;
		s.opts.free_fn = &compile_memory::give;
		s.opts.ud = &s.memory;
		s.opts.arena = s.memory.block.get();
		s.opts.arena_len = compile_memory::arena_size;
		return s.opts;
	}

	static bool stage_of(::glsl::program_domain domain, rslc_stage& stage)
	{
		switch (domain)
		{
		case ::glsl::glsl_vertex_program:
			stage = RSLC_STAGE_VERTEX;
			return true;
		case ::glsl::glsl_fragment_program:
			stage = RSLC_STAGE_FRAGMENT;
			return true;
		case ::glsl::glsl_compute_program:
			stage = RSLC_STAGE_COMPUTE;
			return true;
		default:
			rsx_log.fatal("Unexpected domain %d", +domain);
			return false;
		}
	}

	bool compile_glsl_lent(const u32*& words, usz& count, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		ensure(rules == ::glsl::glsl_rules_vulkan);

		words = nullptr;
		count = 0;

		rslc_stage stage;
		if (!stage_of(domain, stage))
			return false;

		rslc_opts& opts = prepare();
		rslc_result result{};
		bool ok = rslc_compile(shader.data(), shader.size(), stage, &opts, &result);

		if (!ok && std::strstr(opts.err, "arena"))
		{
			// More working state than the block holds
			opts.arena = nullptr;
			opts.arena_len = 0;
			opts.arena_cap = usz{16} * RSLC_ARENA_CAP_DEFAULT;
			opts.err[0] = 0;
			ok = rslc_compile(shader.data(), shader.size(), stage, &opts, &result);
		}

		if (!ok)
		{
			rsx_log.error("%s", opts.err);
			return false;
		}

		// Where rslc left it, in this thread's block
		this_thread().last = result;
		words = result.spirv;
		count = result.spirv_len;
		return true;
	}

	bool compile_glsl_to_spv(std::vector<u32>& spv, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		const u32* words;
		usz count;
		if (!compile_glsl_lent(words, count, shader, domain, rules))
			return false;

		spv.assign(words, words + count);
		return true;
	}

	rslc_builder* build_begin(::glsl::program_domain domain)
	{
		rslc_stage stage;
		if (!stage_of(domain, stage))
			return nullptr;

		return rslc_b_begin(&prepare(), stage);
	}

	bool build_end(rslc_builder* builder, std::vector<u32>& spv)
	{
		if (!builder)
			return false;

		// The options the builder was begun with: this thread's
		rslc_opts& opts = this_thread().opts;
		rslc_result result{};
		if (!rslc_b_end(builder, &result))
		{
			rsx_log.error("%s", opts.err);
			return false;
		}

		spv.assign(result.spirv, result.spirv + result.spirv_len);
		rslc_result_free(&result, &opts);
		return true;
	}
}
