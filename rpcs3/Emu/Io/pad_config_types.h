#pragma once

#include "util/types.hpp"

#include <retro_atomic.h>

enum class pad_handler
{
	null,
	keyboard,
	ds3,
	ds4,
	dualsense,
	skateboard,
	move,
#ifdef _WIN32
	xinput,
	mm,
#endif
};

enum class mouse_movement_mode : s32
{
	relative = 0,
	absolute = 1
};

// The pads' state as cellPad reads it. The frontend's thread counts the
// pads and lets go of ignore_input while the game's (a dialog, the overlays)
// intercept them: retro_atomics, with the system info and ignore_input in one
// word so they change together - the intercepting thread sets both, and
// ignore_input is let go only while the word still says not intercepted.
struct PadInfo
{
	static constexpr int ignore_input_bit = 1 << 30;

	retro_atomic_int_t now_connect_;
	retro_atomic_int_t state_; // CELL_PAD_INFO_* | ignore_input_bit

	PadInfo()
	{
		retro_atomic_int_init(&now_connect_, 0);
		retro_atomic_int_init(&state_, 0);
	}

	PadInfo(const PadInfo&) = delete;
	PadInfo& operator=(const PadInfo&) = delete;

	u32 now_connect() const { return static_cast<u32>(retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t*>(&now_connect_))); }
	u32 system_info() const { return static_cast<u32>(retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t*>(&state_)) & ~ignore_input_bit); }
	bool ignore_input() const { return (retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t*>(&state_)) & ignore_input_bit) != 0; }
};
