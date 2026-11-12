#include "stdafx.h"

#include "Emu/Io/pad_config.h"
#include "Emu/System.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

cfg_input_configurations g_cfg_input_configs;
std::string g_input_config_override;

[[noreturn]] void report_fatal_error(std::string_view text, bool is_html, bool include_help_text)
{
	(void)is_html;
	(void)include_help_text;

	std::fprintf(stderr, "RPCS3(libretro) fatal error: %.*s\n", static_cast<int>(text.size()), text.data());
	std::fflush(stderr);
	throw std::runtime_error(std::string(text));
}

// The emulator's waits for its own state (a stop to finish) and for the game
// (to take an exit request, which it is given a second of): standalone sleeps
// the given time between looks. Here a look comes at each change of the
// emulator's state and at each retro_run, so the game's time is counted in
// the frontend's frames, and nothing sleeps. 0 is a loop doing work in steps,
// not a wait.
void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
	if (!wrapped_op)
		return;

	for (;;)
	{
		const u32 events = g_libretro_events;
		if (wrapped_op())
			return;
		if (repeat_duration_ms > 0)
			g_libretro_events.wait(events);
	}
}
