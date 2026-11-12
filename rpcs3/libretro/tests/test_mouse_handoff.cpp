// The light guns on their way to the PS3 mice (libretro_mouse_handler.cpp):
// retro_run puts each gun's moves, presses and wheel turns on a ring, and
// whoever reads the mice under the handler's lock replays them first.
//
// - what the game reads is what the guns did: the move to where the gun
//   points and the trigger pressed, as MouseHandlerBase makes them;
// - retro_run hands them over while the game holds the mouse lock: it does
//   not wait for it;
// - a ring that fills while nothing reads the mice: the mouse catches up
//   with where the gun is and what it holds;
// - a gun moving on retro_run's thread and the game reading on its own: the
//   mouse only ever moves on, never back.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "libretro_mouse_handler.h"
#include "libretro_input.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::atomic<int> s_x{0}, s_y{0}, s_trigger{0};

static int16_t RETRO_CALLCONV gun_state(unsigned port, unsigned device, unsigned, unsigned id)
{
	if (port != 0 || device != RETRO_DEVICE_LIGHTGUN)
		return 0;
	switch (id)
	{
	case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X: return static_cast<int16_t>(s_x.load());
	case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y: return static_cast<int16_t>(s_y.load());
	case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER: return static_cast<int16_t>(s_trigger.load());
	default: return 0;
	}
}

static void frame(LibretroMouseHandler& mice)
{
	libretro_input_poll(gun_state);
	mice.update();
}

int main()
{
	libretro_input_init();
	LibretroMouseHandler mice;
	mice.Init(2);

	// What the guns did, read under the lock
	s_x = 100;
	s_y = -200;
	s_trigger = 1;
	frame(mice);
	{
		std::lock_guard lock(mice.mutex);
		mice.take_pending();
		const Mouse& m = mice.GetMice()[0];
		CHECK(m.x_pos_pixels == 100 + 0x7fff && m.y_pos_pixels == -200 + 0x7fff);
		CHECK(m.buttons == CELL_MOUSE_BUTTON_1);
		CHECK(mice.GetDataList(0).size() == 2); // the move, then the press
	}

	// The game holding the lock: retro_run does not wait
	{
		std::atomic<bool> held{false}, release{false};
		std::thread game([&]
		{
			std::lock_guard lock(mice.mutex);
			held = true;
			while (!release)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		});
		while (!held)
			std::this_thread::yield();
		std::atomic<bool> done{false};
		std::thread run([&] { s_x = 300; s_trigger = 0; frame(mice); done = true; });
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!done && std::chrono::steady_clock::now() < until)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		CHECK(done);
		release = true;
		run.join();
		game.join();
		std::lock_guard lock(mice.mutex);
		mice.take_pending();
		CHECK(mice.GetMice()[0].x_pos_pixels == 300 + 0x7fff);
		CHECK(mice.GetMice()[0].buttons == 0);
	}

	// Nothing reads the mice for long enough to fill the ring
	for (int i = 0; i < 3000; i++)
	{
		s_x = i % 1000;
		s_trigger = i & 1;
		frame(mice);
	}
	s_x = 1234;
	s_y = 55;
	s_trigger = 1;
	frame(mice);
	{
		std::lock_guard lock(mice.mutex);
		mice.take_pending();
		const Mouse& m = mice.GetMice()[0];
		CHECK(m.x_pos_pixels == 1234 + 0x7fff && m.y_pos_pixels == 55 + 0x7fff);
		CHECK(m.buttons == CELL_MOUSE_BUTTON_1);
		CHECK(mice.GetDataList(0).size() <= MOUSE_MAX_DATA_LIST_NUM + 1);
	}

	// Two threads: the mouse only moves on
	{
		std::atomic<bool> stop{false};
		u32 backwards = 0, reads = 0;
		std::thread game([&]
		{
			s32 last = -1;
			while (!stop)
			{
				std::lock_guard lock(mice.mutex);
				mice.take_pending();
				const s32 x = mice.GetMice()[0].x_pos_pixels;
				if (x < last)
					backwards++;
				last = x;
				reads++;
			}
		});
		for (int i = 0; i < 20000; i++)
		{
			s_x = -0x7000 + i;
			frame(mice);
		}
		stop = true;
		game.join();
		CHECK(backwards == 0);
		CHECK(reads > 0);
		std::printf("test_mouse_handoff: 20000 moves while the game read %u times, %u backwards\n", reads, backwards);
	}

	std::printf("test_mouse_handoff: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
