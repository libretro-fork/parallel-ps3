// retro_run with no game - a boot that failed, or never could: it hands the
// frontend a frame every run (a dupe), which is how the frontend counts
// them, and asks it to close the content, rather than returning with
// nothing and leaving the frontend on no picture.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "libretro.h"

#include <cstdio>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int s_frames = 0;
static int s_shutdowns = 0;

static void video(const void*, unsigned, unsigned, size_t) { s_frames++; }

static bool environment(unsigned cmd, void*)
{
	if (cmd == RETRO_ENVIRONMENT_SHUTDOWN)
	{
		s_shutdowns++;
		return true;
	}
	return false;
}

int main()
{
	retro_set_environment(environment);
	retro_set_video_refresh(video);

	// No game: not loaded, nothing pending
	for (int i = 0; i < 10; i++)
		retro_run();
	CHECK(s_frames == 10);
	CHECK(s_shutdowns >= 1);

	std::printf("test_failed_boot: %d runs without a game, %d frames handed over, close asked %d times; %s\n",
		10, s_frames, s_shutdowns, failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
