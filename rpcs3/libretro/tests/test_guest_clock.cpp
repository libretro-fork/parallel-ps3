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
// - it never goes backwards on any thread, grants and all;
// - a host time taken as the guest's (get_guest_system_time(time), which
//   deadlines, VBlank and audio timestamps go through) is the guest's time
//   then, never later than now - also while the guest's clock is ahead of
//   the host's, as it is whenever retro_run comes a little faster than the
//   frame rate the core declares (a 60 Hz display, 59.94 declared). It came
//   out as zero then: every timed wait of the game's ended at once, and
//   Demon's Souls played none of its movies;
// - so a timed wait of the game's: its deadline 50 ms away has 50 ms left
//   (lv2_obj::deadline_left, which lv2_obj::wait_timeout goes by), and a
//   20 ms wait takes 20 ms of the guest's time, with retro_run at the
//   declared rate, a little faster, and in fast-forward;
// - the clock emulated hardware paces itself by (get_hw_clock_time: the RSX
//   audio path's periods, the PS Move's updates) is the guest's: it stops
//   through a pause, and a time of it as the timebase's
//   (convert_to_timebased_time, the RSX audio ring's timestamps) is the
//   timebase then, at every pace.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/Cell/timers.hpp"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Emu/Cell/lv2/sys_rsxaudio.h"
#include "Utilities/Thread.h"

#include <algorithm>
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
	const u64 hw_from = get_hw_clock_time();
	sleep_ms(120);
	const u64 paused_to = get_timebased_time();
	const double paused = tb_seconds(paused_from, paused_to);
	CHECK(paused <= kFrame + 0.001);
	CHECK(get_slaved_system_time() - slaved_from <= 17000);
	CHECK(get_hw_clock_time() - hw_from <= 17000);

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

	// retro_run a little faster than the declared rate, as on a 60 Hz display
	// with 59.94 declared - and much faster, as in fast-forward: the guest's
	// clock gets ahead of the host's
	u32 bad_conversions = 0, late_conversions = 0;
	u64 worst = 0;
	for (int i = 0; i < 240; i++)
	{
		libretro_guest_clock_grant(i < 120 ? 1.0 / 59.94 : kFrame);
		if (i < 120)
			std::this_thread::sleep_for(std::chrono::microseconds(16000));
		else if (i % 4 == 0)
			sleep_ms(1);

		const u64 before = get_guest_system_time();
		const u64 converted = get_guest_system_time(get_system_time());
		const u64 after = get_guest_system_time();
		// The guest's time at that moment: between the two readings around it
		if (converted > after)
			late_conversions++;
		const u64 off = converted < before ? before - converted : 0;
		worst = std::max(worst, off);
		if (off > 1000)
			bad_conversions++;
	}
	CHECK(late_conversions == 0);
	CHECK(bad_conversions == 0);
	std::printf("test_guest_clock: 240 host times taken as the guest's while it runs ahead: %u later than now, %u off by over 1 ms (worst %llu us)\n",
		late_conversions, bad_conversions, static_cast<unsigned long long>(worst));

	// The game's timed waits, with retro_run at three paces from a thread of
	// its own
	{
		static const struct { const char* name; int frame_us; int sleep_us; } paces[] = {
			{ "at the declared rate", 16683, 16683 },
			{ "a little faster (60 Hz display)", 16683, 16000 },
			{ "fast-forward", 16683, 4000 },
		};
		for (const auto& pace : paces)
		{
			std::atomic<bool> run{true};
			std::thread frontend([&]
			{
				while (run)
				{
					libretro_guest_clock_grant(pace.frame_us / 1000000.0);
					std::this_thread::sleep_for(std::chrono::microseconds(pace.sleep_us));
				}
			});
			sleep_ms(100); // the guest's clock ahead by now, where it gets ahead

			// The hardware's clock as the timebase's: the timebase then
			u32 wrong_stamps = 0;
			for (int i = 0; i < 200; i++)
			{
				const u64 before = get_timebased_time();
				const u64 stamp = convert_to_timebased_time(get_hw_clock_time());
				const u64 after = get_timebased_time();
				// To the microsecond the stamp is made from (80 ticks)
				if (stamp + 160 < before || stamp > after + 160)
					wrong_stamps++;
				std::this_thread::sleep_for(std::chrono::microseconds(300));
			}
			CHECK(wrong_stamps == 0);

			u32 expired_at_once = 0, wrong_left = 0, wrong_wait = 0;
			u64 waited_sum = 0;
			// On one of the emulator's threads, as a game's thread is
			named_thread game("Game", [&]
			{
			for (int i = 0; i < 20; i++)
			{
				// A deadline 50 ms of the guest's time away
				const u64 left = lv2_obj::deadline_left(get_guest_system_time() + 50000);
				if (!left)
					expired_at_once++;
				else if (left < 49000 || left > 50000)
					wrong_left++;

				// A 20 ms sleep: 20 ms of the guest's time
				const u64 from = get_guest_system_time();
				lv2_obj::wait_timeout(20000, nullptr, true, true);
				const u64 waited = get_guest_system_time() - from;
				waited_sum += waited;
				if (waited < 19500 || waited > 40000)
					wrong_wait++;
			}
			});
			game();
			run = false;
			frontend.join();
			CHECK(expired_at_once == 0);
			CHECK(wrong_left == 0);
			CHECK(wrong_wait == 0);
			std::printf("test_guest_clock: retro_run %s: of 20 deadlines 50 ms away %u expired at once, %u had the wrong time left; "
				"20 ms waits took %.1f ms of the guest's time, %u wrong; %u of 200 hardware timestamps off the timebase\n",
				pace.name, expired_at_once, wrong_left, waited_sum / 20000.0, wrong_wait, wrong_stamps);
		}
	}

	// The RSX audio path's periods (rsxaudio_periodic_tmr): its thread waits
	// for the next on the guest's clock, with no timer of the system's. A
	// period comes as the guest's time passes - one for each period of it,
	// with retro_run at the declared rate - and none while retro_run does
	// not come: before, a timer of the host's fired and the period's work
	// ran whether the guest's clock had got there or not. And a waiter with
	// no period to wait for wakes when it is cancelled (the thread stopped).
	{
		rsxaudio_periodic_tmr timer;
		std::atomic<u32> periods{0};
		std::atomic<bool> stop{false};
		constexpr u32 rate = 48000; // a period of 1024 bytes: 21.3 ms

		std::atomic<bool> run{true};
		std::thread frontend([&]
		{
			while (run)
			{
				libretro_guest_clock_grant(kFrame);
				std::this_thread::sleep_for(std::chrono::microseconds(16667));
			}
		});
		sleep_ms(50);

		// The data thread first, with no timer: it waits until there is one.
		// The rate comes from this thread, as a syscall's does, with no lock
		std::thread audio([&]
		{
			while (!stop)
			{
				timer.wait([&]
				{
					periods++;
					timer.vtimer_incr(0, get_hw_clock_time());
				});
			}
		});
		sleep_ms(20);
		timer.request_vtimer(0, rate);

		const u64 from = get_hw_clock_time();
		const u32 periods_from = periods;
		sleep_ms(640);
		const u64 guest_us = get_hw_clock_time() - from;
		const u32 had = periods - periods_from;
		const double expected = guest_us / (1024.0 * 1000000.0 / rate);
		CHECK(had + 2 >= expected && had <= expected + 2);

		// The frontend paused: no retro_run, so no guest time, so no period
		// past the one under way
		run = false;
		frontend.join();
		sleep_ms(30);
		const u32 paused_from = periods;
		sleep_ms(200);
		const u32 while_paused = periods - paused_from;
		CHECK(while_paused <= 1);

		// No period at all to wait for: the wait ends when cancelled
		timer.request_vtimer(0, 0);
		sleep_ms(20);
		stop = true;
		const auto t0 = std::chrono::steady_clock::now();
		timer.cancel_wait();
		audio.join();
		const auto took_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		CHECK(took_ms < 100);

		std::printf("test_guest_clock: RSX audio periods: %u in %.1f ms of the guest's time (%.1f due), %u while paused 200 ms; a cancelled wait ended in %lld ms\n",
			had, guest_us / 1000.0, expected, while_paused, static_cast<long long>(took_ms));
	}

	std::printf("test_guest_clock: paused 120 ms, guest moved %.1f ms; 10 frames at once, %.1f had; "
		"60 at an uneven pace, %.1f had; %llu reads on 4 threads, %u backwards\n",
		paused * 1000.0, ff / kFrame, paced / kFrame, static_cast<unsigned long long>(reads.load()), backwards.load());
	std::printf("test_guest_clock: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
