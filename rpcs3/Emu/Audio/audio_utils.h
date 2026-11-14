#pragma once

#include <retro_atomic.h>

namespace audio
{
	struct audio_fxo
	{
		retro_atomic_int_t audio_muted{};
	};

	f32 get_volume();

	void toggle_mute();
	void change_volume(s32 delta);
}
