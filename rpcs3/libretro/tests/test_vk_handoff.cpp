// The Vulkan frame handoff (Emu/RSX/VK/VKLibretro.cpp): the renderer's
// thread hands each finished frame's image to retro_run, which shows it.
//
// - frames come out once each, oldest first, with at most two waiting: a
//   third overtakes the oldest;
// - a renderer and a retro_run on their own threads: every frame taken is
//   newer than the last, none twice;
// - no allocation on the way, steady or overtaking;
// - stepping (the run after the frontend stopped): the run shows the frame
//   the game draws in it, and the renderer waits after that one frame
//   until the next run.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/RSX/VK/VKLibretro.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::atomic<bool> s_counting{false};
static std::atomic<usz> s_allocations{0};

void* operator new(std::size_t n)
{
	if (s_counting)
		s_allocations++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main()
{
	using namespace vk::libretro;
	u32 index = 0;

	// A frontend running: frames in order, a third overtakes the oldest
	frontend_ran();
	frame_ready(1);
	frame_ready(2);
	frame_ready(3);
	CHECK(take_frame(index) && index == 2);
	CHECK(take_frame(index) && index == 3);
	CHECK(!take_frame(index));
	frame_ready(4);
	CHECK(take_frame(index) && index == 4);
	frame_ready(5);
	drop_pending_frames();
	CHECK(!take_frame(index));

	// No allocation, steady or overtaking
	s_allocations = 0;
	s_counting = true;
	for (u32 i = 0; i < 1000; i++)
	{
		frame_ready(i % 4);
		frame_ready((i + 1) % 4);
		frame_ready((i + 2) % 4);
		take_frame(index);
		take_frame(index);
	}
	s_counting = false;
	CHECK(s_allocations == 0);
	drop_pending_frames();

	// Two threads: every frame taken newer than the last
	{
		// A run in progress: the frontend counts as running throughout
		frontend_ran();
		// Image indices go 0..199 over and over, the renderer never more
		// than 50 frames past the last one taken, so each taken index says
		// how far on it is
		std::atomic<bool> done{false};
		std::atomic<u32> taken_seq{0};
		std::thread renderer([&]
		{
			for (u32 i = 1; i <= 200000; i++)
			{
				while (i - taken_seq.load() > 50)
					std::this_thread::yield();
				frame_ready(i % 200);
			}
			done = true;
		});

		u32 seq = 0, taken = 0, wrong = 0;
		for (;;)
		{
			const bool finished = done;
			if (!take_frame(index))
			{
				if (finished)
					break;
				continue;
			}
			const u32 ahead = (index + 200 - seq % 200) % 200;
			if (ahead == 0 || ahead > 50)
				wrong++;
			seq += ahead;
			taken_seq = seq;
			taken++;
		}
		renderer.join();
		CHECK(wrong == 0);
		CHECK(taken > 100);
		std::printf("test_vk_handoff: %u of 200000 frames taken across threads, %u out of order\n", taken, wrong);
	}

	// Stepping
	{
		drop_pending_frames();
		frontend_ran();
		frontend_returned();
		std::this_thread::sleep_for(std::chrono::milliseconds(40)); // the frontend stopped
		frontend_ran();                                              // a step

		std::atomic<int> drawn{0};
		std::thread renderer([&]
		{
			frame_ready(7); // the step's frame: the renderer waits in it
			drawn = 1;
			frame_ready(8); // the next step's
			drawn = 2;
		});

		CHECK(take_frame(index) && index == 7);
		frontend_returned();
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
		CHECK(drawn == 0); // waiting for the next run
		frontend_ran();     // the next step: frame 8 is drawn, and waits
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		CHECK(drawn == 1);
		CHECK(take_frame(index) && index == 8);
		frontend_detached(); // the content closing lets it go
		renderer.join();
		CHECK(drawn == 2);
		drop_pending_frames();
	}

	std::printf("test_vk_handoff: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
