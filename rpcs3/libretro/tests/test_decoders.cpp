// The PPU and SPU decoders a build gives a game (fixup_decoders_without_llvm,
// which Emulator::Load runs on every configuration it loads and the core on
// its options): in a build without LLVM the LLVM recompilers, which stopped a
// game at its first PPU module ("LLVM is not available in this build"), are
// replaced - the PPU interpreter, and ASMJIT for the SPU (the dynamic
// interpreter on a CPU ASMJIT has no SPU recompiler for); in a build with
// LLVM nothing changes. Other choices are left alone either way.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main, and run there.

#include "stdafx.h"
#include "Emu/system_config.h"

#include <cstdio>

extern void fixup_decoders_without_llvm();

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main()
{
	g_cfg.core.ppu_decoder.set(ppu_decoder_type::llvm);
	g_cfg.core.spu_decoder.set(spu_decoder_type::llvm);
	fixup_decoders_without_llvm();

#ifdef LLVM_AVAILABLE
	CHECK(g_cfg.core.ppu_decoder == ppu_decoder_type::llvm);
	CHECK(g_cfg.core.spu_decoder == spu_decoder_type::llvm);
	std::printf("test_decoders: built with LLVM: the LLVM recompilers stay\n");
#else
	CHECK(g_cfg.core.ppu_decoder == ppu_decoder_type::_static);
#if defined(ARCH_X64)
	CHECK(g_cfg.core.spu_decoder == spu_decoder_type::asmjit);
#else
	CHECK(g_cfg.core.spu_decoder == spu_decoder_type::dynamic);
#endif
	std::printf("test_decoders: built without LLVM: the PPU interpreter and %s for the SPU\n",
		g_cfg.core.spu_decoder == spu_decoder_type::asmjit ? "ASMJIT" : "the dynamic interpreter");
#endif

	// The others as they were
	for (const spu_decoder_type spu : {spu_decoder_type::_static, spu_decoder_type::dynamic, spu_decoder_type::asmjit})
	{
		g_cfg.core.ppu_decoder.set(ppu_decoder_type::_static);
		g_cfg.core.spu_decoder.set(spu);
		fixup_decoders_without_llvm();
		CHECK(g_cfg.core.ppu_decoder == ppu_decoder_type::_static);
		CHECK(g_cfg.core.spu_decoder == spu);
	}

	if (failures)
	{
		std::fprintf(stderr, "test_decoders: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_decoders: ok\n");
	return 0;
}
