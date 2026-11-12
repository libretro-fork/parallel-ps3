// The emulator's own waits, in the core: for its state (a stop to finish, a
// boot past loading) and for the game (to take an exit request). Standalone
// RPCS3 sleeps a few milliseconds and looks again; here they wait on an
// event, untimed - a change of the emulator's state, or a retro_run:
//
// - a wait does not look again while nothing happens: no polling;
// - each retro_run lets it look once, so the game's time is counted in the
//   frontend's frames;
// - a change of the emulator's state wakes a wait for it at once, without a
//   retro_run.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/System.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op);

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Until @looks has reached @n, or a few million yields have gone by
static bool reaches(const std::atomic<int>& looks, int n)
{
	for (int i = 0; i < 5'000'000 && looks < n; i++)
		std::this_thread::yield();
	return looks >= n;
}

int main()
{
	// A wait for the game, 16 ms a look in standalone: 60 looks a second
	{
		std::atomic<int> looks{0};
		std::atomic<bool> done{false};
		std::thread waiter([&]
		{
			qt_events_aware_op(16, [&] { return ++looks >= 10; });
			done = true;
		});

		CHECK(reaches(looks, 1));
		// Nothing happening: it does not look again
		for (int i = 0; i < 200000; i++)
			std::this_thread::yield();
		const int idle = looks;
		CHECK(idle == 1);

		// A look a retro_run
		int runs = 0;
		while (looks < 10 && runs < 100)
		{
			const int before = looks;
			libretro_event(); // what retro_run does first
			runs++;
			CHECK(reaches(looks, before + 1));
		}
		waiter.join();
		CHECK(runs == 9);
		std::printf("test_emu_waits: %d look(s) while nothing happened, done after %d retro_runs\n", idle, runs);
	}

	// A wait for the emulator's state: woken by the change itself
	{
		std::atomic<bool> done{false};
		std::thread waiter([&]
		{
			qt_events_aware_op(5, [] { return Emu.IsRunning(); });
			done = true;
		});
		for (int i = 0; i < 200000; i++)
			std::this_thread::yield();
		CHECK(!done);
		Emu.SetTestMode(); // stopped to running
		for (int i = 0; i < 5'000'000 && !done; i++)
			std::this_thread::yield();
		CHECK(done);
		if (!done)
			libretro_event(); // so a failure ends
		waiter.join();
	}

	std::printf("test_emu_waits: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
