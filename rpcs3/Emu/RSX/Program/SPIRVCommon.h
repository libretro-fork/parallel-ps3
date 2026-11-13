#pragma once

namespace glsl
{
	enum program_domain : unsigned char;
	enum glsl_rules : unsigned char;
}

struct rslc_builder;

namespace spirv
{
	// The module where the compiler left it, in the compiling thread's own
	// block: no copy of it is made. Valid until that thread compiles or
	// builds again.
	bool compile_glsl_lent(const u32*& words, usz& count, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules);

	// The same, copied out for a caller that keeps it
	bool compile_glsl_to_spv(std::vector<u32>& spv, std::string& shader, ::glsl::program_domain domain, ::glsl::glsl_rules rules);

	// A stage made by calls, with nothing written as GLSL and nothing parsed:
	// build_begin, then libretro-common's rslc_b_* (gfx/shaders/rslc_build.h)
	// on what it returns, then build_end for the module. One at a time on a
	// thread, and no compile_glsl_to_spv between the two.
	rslc_builder* build_begin(::glsl::program_domain domain);
	bool build_end(rslc_builder* builder, std::vector<u32>& spv);
}
