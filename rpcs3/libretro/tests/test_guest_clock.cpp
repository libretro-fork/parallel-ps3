// The guest's clock slaved to retro_run (Emu/Cell/lv2/sys_time.cpp): each
// retro_run grants one frame of the guest's time, which runs smoothly at
// the host's rate within it:
//
// - without retro_run the guest's clock stops within a frame of the last
//   one (the frontend paused), and goes on from there, not jumping ahead;
// - retro_run faster than real time pulls it forward: every frame asked for
//   is had (fast-forward);
// - at the frontend's pace it keeps the frames asked for;
// - inside a frame it moves, as the PS3's timebase does;
// - it never goes backwards on any thread, grants and all.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/Cell/timers.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static constexpr double kFrame = 1.0 / 60.0;
static constexpr double kTb = 80000000.0; // the timebase's ticks a second

static double tb_seconds(u64 from, u64 to) { return static_cast<double>(to - from) / kTb; }

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main()
{
	// Before the first retro_run: the host's
	{
		const u64 a = get_timebased_time();
		sleep_ms(20);
		const u64 b = get_timebased_time();
		CHECK(tb_seconds(a, b) >= 0.019);
	}

	// Paused: one frame, then nothing more
	libretro_guest_clock_grant(kFrame);
	const u64 paused_from = get_timebased_time();
	const u64 slaved_from = get_slaved_system_time();
	sleep_ms(120);
	const u64 paused_to = get_timebased_time();
	const double paused = tb_seconds(paused_from, paused_to);
	CHECK(paused <= kFrame + 0.001);
	CHECK(get_slaved_system_time() - slaved_from <= 17000);

	// On from where it stopped, not ahead by the pause
	libretro_guest_clock_grant(kFrame);
	{
		const u64 now = get_timebased_time();
		CHECK(tb_seconds(paused_to, now) <= kFrame + 0.001);
	}

	// Fast-forward: ten frames asked for at once, ten frames had
	const u64 ff_from = get_timebased_time();
	for (int i = 0; i < 10; i++)
		libretro_guest_clock_grant(kFrame);
	const double ff = tb_seconds(ff_from, get_timebased_time());
	CHECK(ff >= 8.9 * kFrame);
	CHECK(ff <= 11.1 * kFrame);

	// Inside a frame it moves
	{
		libretro_guest_clock_grant(kFrame);
		const u64 a = get_timebased_time();
		sleep_ms(5);
		const u64 b = get_timebased_time();
		CHECK(b > a);
	}

	// Never backwards, on any thread, while retro_run grants at an uneven pace
	std::atomic<bool> stop{false};
	std::atomic<u32> backwards{0};
	std::atomic<u64> reads{0};
	std::vector<std::thread> readers;
	for (int t = 0; t < 4; t++)
	{
		readers.emplace_back([&]
		{
			u64 last = get_timebased_time();
			u64 n = 0;
			while (!stop)
			{
				const u64 now = get_timebased_time();
				if (now < last)
					backwards++;
				last = now;
				n++;
			}
			reads += n;
		});
	}
	const u64 paced_from = get_timebased_time();
	for (int i = 0; i < 60; i++)
	{
		libretro_guest_clock_grant(kFrame);
		sleep_ms(i % 7 == 0 ? 30 : (i % 3 == 0 ? 2 : 16));
	}
	const double paced = tb_seconds(paced_from, get_timebased_time());
	stop = true;
	for (auto& th : readers)
		th.join();
	CHECK(backwards == 0);
	// Sixty frames asked for: no more than those, no fewer than all but one
	CHECK(paced <= 61.0 * kFrame);
	CHECK(paced >= 58.9 * kFrame);

	std::printf("test_guest_clock: paused 120 ms, guest moved %.1f ms; 10 frames at once, %.1f had; "
		"60 at an uneven pace, %.1f had; %llu reads on 4 threads, %u backwards\n",
		paused * 1000.0, ff / kFrame, paced / kFrame, static_cast<unsigned long long>(reads.load()), backwards.load());
	std::printf("test_guest_clock: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
