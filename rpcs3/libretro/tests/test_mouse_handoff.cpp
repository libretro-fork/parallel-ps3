// The light guns on their way to the PS3 mice (libretro_mouse_handler.cpp,
// Emu/Io/MouseHandler.cpp). There is no lock: retro_run writes each gun's
// moves, presses and wheel turns to a log, and whichever of the game's
// threads reads a mouse applies what the log has for it, to a record.
//
// - what the game reads is what the guns did: the move to where the gun
//   points and the trigger pressed, as MouseHandlerBase makes them;
// - cellMouseGetData's and cellMouseGetDataList's way of taking the data;
// - several of the game's threads taking one mouse's data while retro_run
//   makes it: each piece is taken once, by one of them, and none is lost;
// - a log that goes round while nothing reads the mouse: the mouse catches
//   up with where the gun is and what it holds;
// - a gun moving flat out on retro_run's thread and the game reading on two
//   of its own, one of them away for longer than the log at times: the
//   mouse only ever moves on, never back, and ends where the gun is;
// - the frontend is asked about the guns only while there are mice;
// - only the first Init sets the mice up; a mouse that is not connected, and
//   one that is not there; interception.
//
// MOUSE_BENCH=1 times a read of a mouse with nothing new from the frontend.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "libretro_mouse_handler.h"
#include "libretro_input.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::atomic<int> s_x{0}, s_y{0}, s_trigger{0};

static std::atomic<unsigned> s_gun_calls{0};

static int16_t RETRO_CALLCONV gun_state(unsigned port, unsigned device, unsigned, unsigned id)
{
	if (device == RETRO_DEVICE_LIGHTGUN)
		s_gun_calls++;

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

	// Before Init there are no mice
	{
		mouse_port p;
		Mouse m;
		CHECK(!mice.read_port(0, p));
		CHECK(!mice.read_mouse(0, m));
		CHECK(mice.take_data(0, nullptr, 0, true) == -1);
	}

	// No mice yet: the frontend is not asked about the guns
	s_gun_calls = 0;
	frame(mice);
	frame(mice);
	CHECK(s_gun_calls == 0);

	mice.Init(7);

	// The run the mice appear in tells the input they are read; the guns are
	// asked about from the next, and the mice start from where they are then
	s_x = 500;
	frame(mice);
	CHECK(s_gun_calls == 0);
	{
		mouse_port p;
		CHECK(mice.read_port(0, p) && p.mouse.x_pos_pixels == 0 && p.list_count == 0);
	}
	frame(mice);
	CHECK(s_gun_calls > 0);
	{
		mouse_port p;
		CHECK(mice.read_port(0, p) && p.mouse.x_pos_pixels == 500 + 0x7fff && p.list_count == 1);
		CHECK(mice.take_data(0, nullptr, 0, true) == 0);
	}
	CHECK(mice.max_connect() == 7 && mice.now_connect() == 4 && mice.count() == 7);
	CHECK(mice.status(3) == CELL_MOUSE_STATUS_CONNECTED && mice.status(4) == CELL_MOUSE_STATUS_DISCONNECTED);
	CHECK(mice.vendor_id(0) == 0x1234 && mice.product_id(0) == 0x1234 && mice.vendor_id(7) == 0);

	// Only the first Init sets the mice up
	mice.Init(2);
	CHECK(mice.max_connect() == 7);

	// A mouse that is there but not connected, and one that is not there
	{
		mouse_port p;
		Mouse m;
		m.buttons = 0xff;
		CHECK(!mice.read_port(5, p));
		CHECK(mice.read_mouse(5, m) && m.buttons == 0 && m.x_pos_pixels == 0);
		CHECK(!mice.read_mouse(7, m));
		CHECK(mice.take_data(5, nullptr, 0, true) == -1);
	}

	// What the guns did
	s_x = 100;
	s_y = -200;
	s_trigger = 1;
	frame(mice);
	{
		mouse_port p;
		CHECK(mice.read_port(0, p));
		CHECK(p.mouse.x_pos_pixels == 100 + 0x7fff && p.mouse.y_pos_pixels == -200 + 0x7fff);
		CHECK(p.mouse.buttons == CELL_MOUSE_BUTTON_1);
		CHECK(p.list_count == 2);
		CHECK(p.list[0].buttons == 0 && p.list[0].pixel_x == 100 + 0x7fff);
		CHECK(p.list[1].buttons == CELL_MOUSE_BUTTON_1);

		// Read again: the same, nothing applied twice
		mouse_port q;
		CHECK(mice.read_port(0, q) && q.list_count == 2 && q.applied == p.applied);

		// The mouse alone is the same mouse
		Mouse m;
		CHECK(mice.read_mouse(0, m) && m.x_pos_pixels == p.mouse.x_pos_pixels && m.y_pos_pixels == p.mouse.y_pos_pixels);
		CHECK(m.buttons == p.mouse.buttons && m.x_pos == p.mouse.x_pos && m.y_pos == p.mouse.y_pos);
		CHECK(m.x_pos > 0.5f && m.x_pos < 0.51f && m.y_pos < 0.5f);

		// cellMouseGetData takes the oldest
		MouseData d;
		CHECK(mice.take_data(0, &d, 1, false) == 1 && d.buttons == 0);
		CHECK(mice.take_data(0, &d, 1, false) == 1 && d.buttons == CELL_MOUSE_BUTTON_1);
		CHECK(mice.take_data(0, &d, 1, false) == 0);
	}

	// cellMouseGetDataList: the oldest eight of nine, and the list is empty
	// after; dropped, it gives none
	{
		for (int i = 0; i < 12; i++)
		{
			s_x = 101 + i;
			frame(mice);
		}
		mouse_port p;
		CHECK(mice.read_port(0, p) && p.list_count == MOUSE_MAX_DATA_LIST_NUM + 1);
		CHECK(p.list[MOUSE_MAX_DATA_LIST_NUM].pixel_x == 112 + 0x7fff); // the newest kept

		MouseData list[MOUSE_MAX_DATA_LIST_NUM];
		CHECK(mice.take_data(0, list, MOUSE_MAX_DATA_LIST_NUM, false) == static_cast<s32>(MOUSE_MAX_DATA_LIST_NUM));
		CHECK(list[0].pixel_x == 104 + 0x7fff && list[0].x_axis == 1);
		CHECK(mice.read_port(0, p) && p.list_count == 0);

		s_x = 200;
		frame(mice);
		CHECK(mice.take_data(0, list, MOUSE_MAX_DATA_LIST_NUM, true) == 0);
		CHECK(mice.read_port(0, p) && p.list_count == 0 && p.mouse.x_pos_pixels == 200 + 0x7fff);
	}

	// Four of the game's threads take the mouse's data while retro_run makes
	// it, a move a frame, never far enough ahead for the list to drop any:
	// each move is taken once
	{
		constexpr int moves = 20000;
		constexpr s32 first = 1000 + 0x7fff;
		std::vector<std::atomic<u8>> taken_by(moves);
		std::atomic<int> taken{0};
		std::atomic<bool> stop{false};
		std::vector<std::thread> game;

		for (int t = 0; t < 4; t++)
		{
			game.emplace_back([&]
			{
				while (!stop)
				{
					MouseData d;
					if (mice.take_data(0, &d, 1, false) == 1)
					{
						const s32 n = d.pixel_x - first;
						if (n >= 0 && n < moves)
							taken_by[n]++;
						taken++;
					}
					else
					{
						std::this_thread::yield();
					}
				}
			});
		}

		for (int i = 0; i < moves; i++)
		{
			while (i - taken.load() > 6)
				std::this_thread::yield();
			s_x = 1000 + i;
			frame(mice);
		}

		while (taken.load() < moves)
			std::this_thread::yield();
		stop = true;
		for (std::thread& t : game)
			t.join();

		int once = 0, twice = 0, never = 0;
		for (int i = 0; i < moves; i++)
		{
			const u8 n = taken_by[i].load();
			once += n == 1;
			twice += n > 1;
			never += n == 0;
		}
		CHECK(once == moves && twice == 0 && never == 0);
		CHECK(taken.load() == moves);
		std::printf("test_mouse_handoff: %d moves taken by four threads: %d once, %d more than once, %d never\n", moves, once, twice, never);
	}

	// Nothing reads the mouse while its log goes round several times
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
		mouse_port p;
		CHECK(mice.read_port(0, p));
		CHECK(p.mouse.x_pos_pixels == 1234 + 0x7fff && p.mouse.y_pos_pixels == 55 + 0x7fff);
		CHECK(p.mouse.buttons == CELL_MOUSE_BUTTON_1);
		// Caught up, not replayed from a log that no longer has it all: one
		// move (the trigger was held before, and is now)
		CHECK(p.list_count == 1 && p.list[0].pixel_x == 1234 + 0x7fff);

		// And goes on from there
		s_x = 1235;
		s_trigger = 0;
		frame(mice);
		CHECK(mice.read_port(0, p));
		CHECK(p.mouse.x_pos_pixels == 1235 + 0x7fff && p.mouse.buttons == 0);
	}

	// The gun flat out, two of the game's threads reading: the mouse only
	// moves on, whether it follows the log or has to catch up
	{
		std::atomic<bool> stop{false};
		std::atomic<u32> backwards{0}, reads{0};

		// From the left, and read there before the threads start
		s_x = -0x7000;
		frame(mice);
		{
			Mouse m;
			mouse_port p;
			CHECK(mice.read_mouse(0, m) && mice.read_port(0, p) && m.x_pos_pixels == p.mouse.x_pos_pixels);
		}
		std::vector<std::thread> game;

		for (int t = 0; t < 2; t++)
		{
			game.emplace_back([&, t]
			{
				s32 last = -1;
				u32 n = 0;
				while (!stop)
				{
					// One reads the mouse alone, the other the port with its
					// data: each takes the log by itself
					Mouse m;
					mouse_port p;
					if (t == 0 ? !mice.read_mouse(0, m) : !mice.read_port(0, p))
						backwards++;
					const s32 x = t == 0 ? m.x_pos_pixels : p.mouse.x_pos_pixels;
					if (x < last)
						backwards++;
					last = x;
					reads++;
					// The port's now and then stays away for longer than the log
					if (t == 1 && (++n & 0x3ff) == 0)
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			});
		}

		for (int i = 0; i < 60000; i++)
		{
			s_x = -0x7000 + i;
			frame(mice);
		}
		stop = true;
		for (std::thread& t : game)
			t.join();

		Mouse m;
		CHECK(mice.read_mouse(0, m) && m.x_pos_pixels == -0x7000 + 59999 + 0x7fff);
		CHECK(backwards == 0);
		CHECK(reads > 0);
		std::printf("test_mouse_handoff: 60000 moves while the game read %u times, %u backwards\n", reads.load(), backwards.load());
	}

	// Intercepted: the mouse starts from nothing, and is told so
	{
		mice.SetIntercepted(true);
		CHECK(mice.info() == CELL_MOUSE_INFO_INTERCEPTED);
		mouse_port p;
		CHECK(mice.read_port(0, p) && p.mouse.x_pos_pixels == 0 && p.mouse.buttons == 0 && p.list_count == 0);
		Mouse m;
		CHECK(mice.read_mouse(0, m) && m.x_pos_pixels == 0 && m.buttons == 0);
		mice.SetIntercepted(false);
		CHECK(mice.info() == 0);
		s_x = 77;
		frame(mice);
		CHECK(mice.read_port(0, p) && p.mouse.x_pos_pixels == 77 + 0x7fff);
	}

	if (std::getenv("MOUSE_BENCH"))
	{
		constexpr int n = 20000000;
		Mouse m;
		u32 sum = 0;

		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++)
		{
			mice.read_mouse(0, m);
			sum += m.x_pos_pixels;
		}
		auto t1 = std::chrono::steady_clock::now();

		// What it was: the handler's lock, the four rings looked at, the mouse
		std::mutex lock;
		std::atomic<u32> ring[8]{};
		Mouse held = m;
		for (int i = 0; i < n; i++)
		{
			std::lock_guard guard(lock);
			for (auto& r : ring)
				sum += r.load(std::memory_order_acquire);
			m = held;
			sum += m.x_pos_pixels;
		}
		auto t2 = std::chrono::steady_clock::now();

		MouseData d;
		for (int i = 0; i < n; i++)
			sum += mice.take_data(0, &d, 1, false);
		auto t3 = std::chrono::steady_clock::now();

		std::printf("test_mouse_handoff: cellMouseGetData's take with nothing waiting %.1f ns\n",
			std::chrono::duration<double, std::nano>(t3 - t2).count() / n);
		std::printf("test_mouse_handoff: a mouse read %.1f ns; under a lock with the rings looked at %.1f ns (%u)\n",
			std::chrono::duration<double, std::nano>(t1 - t0).count() / n,
			std::chrono::duration<double, std::nano>(t2 - t1).count() / n, sum);
	}

	std::printf("test_mouse_handoff: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
