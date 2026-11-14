#pragma once

#include "util/types.hpp"
#include <retro_atomic.h>

namespace rsx
{
	namespace overlays
	{
		void show_shader_compile_notification();
		std::shared_ptr<retro_atomic_int_t> show_ppu_compile_notification();
	}
}
