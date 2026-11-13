#include "stdafx.h"

#include "SPIRVCommon.h"
#include "Emu/RSX/Program/GLSLTypes.h"

#include <gfx/shaders/rslc.h>

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
}

namespace spirv
{
	bool compile_glsl_to_spv(std::vector<u32>& spv, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules)
	{
		ensure(rules == ::glsl::glsl_rules_vulkan);

		rslc_stage stage;
		switch (domain)
		{
		case ::glsl::glsl_vertex_program:
			stage = RSLC_STAGE_VERTEX;
			break;
		case ::glsl::glsl_fragment_program:
			stage = RSLC_STAGE_FRAGMENT;
			break;
		case ::glsl::glsl_compute_program:
			stage = RSLC_STAGE_COMPUTE;
			break;
		default:
			rsx_log.fatal("Unexpected domain %d", +domain);
			return false;
		}

		thread_local std::unique_ptr<compile_memory> memory;
		if (!memory)
			memory = std::make_unique<compile_memory>();
		memory->used = 0;

		char err[256]{};
		rslc_opts opts{};
		opts.dst = RSLC_DST_SPIRV;
		opts.err = err;
		opts.err_cap = sizeof(err);
		opts.alloc = &compile_memory::take;
		opts.free_fn = &compile_memory::give;
		opts.ud = memory.get();
		opts.arena = memory->block.get();
		opts.arena_len = compile_memory::arena_size;

		rslc_result result{};
		bool ok = rslc_compile(shader.data(), shader.size(), stage, &opts, &result);

		if (!ok && std::strstr(err, "arena"))
		{
			// More working state than the block holds
			opts.arena = nullptr;
			opts.arena_len = 0;
			opts.arena_cap = usz{16} * RSLC_ARENA_CAP_DEFAULT;
			err[0] = 0;
			ok = rslc_compile(shader.data(), shader.size(), stage, &opts, &result);
		}

		if (!ok)
		{
			rsx_log.error("%s", err);
			return false;
		}

		spv.assign(result.spirv, result.spirv + result.spirv_len);
		rslc_result_free(&result, &opts);
		return true;
	}
}
