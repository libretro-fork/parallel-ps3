#pragma once

#include "util/types.hpp"

#include <retro_atomic.h>

namespace input
{
	enum class active_mouse_and_keyboard : u32
	{
		pad,
		emulated
	};
	// An active_mouse_and_keyboard
	extern retro_atomic_int_t g_active_mouse_and_keyboard;

	extern retro_atomic_int_t g_pads_intercepted;
	extern retro_atomic_int_t g_keyboards_intercepted;
	extern retro_atomic_int_t g_mice_intercepted;

	void SetIntercepted(bool pads_intercepted, bool keyboards_intercepted, bool mice_intercepted, const char* func = __builtin_FUNCTION());
	void SetIntercepted(bool all_intercepted, const char* func = __builtin_FUNCTION());

	void set_mouse_and_keyboard(active_mouse_and_keyboard device);
	void toggle_mouse_and_keyboard();
}
