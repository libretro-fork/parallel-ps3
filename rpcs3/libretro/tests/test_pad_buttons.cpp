// An emulated controller's buttons from the pad's (Emu/Io/
// emulated_pad_config.h): what the USB controllers, the arcade board and the
// PS Move's stand-ins call for every poll of theirs.
//
// - every pad button reaches the controller's buttons mapped to it, in the
//   order they are declared, with its value and whether it is pressed; a
//   stick reaches the button of its direction, else the one for both;
//   unpressed buttons are left out when asked; a callback can stop the rest;
// - the mapping read while it is made again (a configuration loaded, as at
//   every boot): a poll gets one whole mapping - the old one, none between
//   the two, or the new one - never part of one and part of another;
// - no lock on the way: polls on four threads at once never wait on one
//   another, and a poll made while another thread is inside a callback of
//   its own poll returns (it waited for that one before);
// - what a poll costs (PAD_BENCH=1).
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/Io/emulated_pad_config.h"
#include "Emu/Io/pad_types.h"
#include "Input/pad_thread.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <time.h> // not pacing: the benchmark's stopwatch

static int failures = 0;

// What a poll allocates, on the thread polling
static thread_local bool t_counting = false;
static thread_local usz t_allocations = 0;

void* operator new(std::size_t n)
{
	if (t_counting)
		t_allocations++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

enum class test_btn
{
	a,
	b,
	c,
	d,
	left,
	right,
	any_x,
	e,

	count
};

struct cfg_test final : public emulated_pad_config<test_btn>
{
	cfg_test(node* owner, const std::string& name) : emulated_pad_config(owner, name) {}

	cfg_pad_btn<test_btn> a{ this, "A", test_btn::a, pad_button::cross };
	cfg_pad_btn<test_btn> b{ this, "B", test_btn::b, pad_button::circle };
	cfg_pad_btn<test_btn> c{ this, "C", test_btn::c, pad_button::cross };   // two on Cross
	cfg_pad_btn<test_btn> d{ this, "D", test_btn::d, pad_button::dpad_up };
	cfg_pad_btn<test_btn> left{ this, "Left", test_btn::left, pad_button::ls_left };
	cfg_pad_btn<test_btn> right{ this, "Right", test_btn::right, pad_button::ls_right };
	cfg_pad_btn<test_btn> any_x{ this, "Any X", test_btn::any_x, pad_button::rs_x };
	cfg_pad_btn<test_btn> e{ this, "E", test_btn::e, pad_button::R2 };
};

struct cfg_tests final : public cfg::node
{
	cfg_test player{ this, "Player 1" };
};

static void all_to(cfg_test& cfg, pad_button button)
{
	cfg.a.set(button);
	cfg.b.set(button);
	cfg.c.set(button);
	cfg.d.set(button);
	cfg.left.set(button);
	cfg.right.set(button);
	cfg.any_x.set(button);
	cfg.e.set(button);
}

int main()
{
	// A DualShock 3's buttons and sticks, handed over as retro_run does
	pad_thread pads(nullptr, nullptr, "");
	pads.GetPads()[0] = std::make_shared<Pad>(pad_handler::null, 0, 0, CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_STANDARD);
	std::shared_ptr<Pad> pad = pads.GetPads()[0];
	static const u32 d1[] = {CELL_PAD_CTRL_SELECT, CELL_PAD_CTRL_L3, CELL_PAD_CTRL_R3, CELL_PAD_CTRL_START, CELL_PAD_CTRL_UP, CELL_PAD_CTRL_RIGHT, CELL_PAD_CTRL_DOWN, CELL_PAD_CTRL_LEFT};
	static const u32 d2[] = {CELL_PAD_CTRL_L2, CELL_PAD_CTRL_R2, CELL_PAD_CTRL_L1, CELL_PAD_CTRL_R1, CELL_PAD_CTRL_TRIANGLE, CELL_PAD_CTRL_CIRCLE, CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_SQUARE};
	pad->m_buttons.resize(16);
	for (u32 i = 0; i < 8; i++)
	{
		pad->m_buttons[i].m_offset = CELL_PAD_BTN_OFFSET_DIGITAL1;
		pad->m_buttons[i].m_outKeyCode = d1[i];
		pad->m_buttons[8 + i].m_offset = CELL_PAD_BTN_OFFSET_DIGITAL2;
		pad->m_buttons[8 + i].m_outKeyCode = d2[i];
	}
	pad->m_sticks[0].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X;
	pad->m_sticks[1].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y;
	pad->m_sticks[2].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X;
	pad->m_sticks[3].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y;
	for (AnalogStick& s : pad->m_sticks)
		s.m_value = 128;
	retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_CONNECTED);

	const auto press = [&](u32 offset, u32 keycode, u16 value)
	{
		for (Button& b : pad->m_buttons)
			if (b.m_offset == offset && b.m_outKeyCode == keycode)
			{
				b.m_pressed = value != 0;
				b.m_value = value;
			}
		pads.apply_copilots();
	};

	cfg_tests root;
	cfg_test& cfg = root.player;
	cfg.init_buttons();

	struct seen_t { test_btn btn; pad_button pad_btn; u16 value; bool pressed; };
	std::vector<seen_t> seen;
	seen.reserve(64);
	const auto collect = [&](bool press_only)
	{
		seen.clear();
		cfg.handle_input(pad, press_only, [&](const auto& value, bool&)
		{
			seen.push_back({value.btn, value.pad_btn, value.value, value.pressed});
		});
	};
	const auto count_of = [&](test_btn btn)
	{
		u32 n = 0;
		for (const seen_t& s : seen)
			n += s.btn == btn;
		return n;
	};
	const auto first_of = [&](test_btn btn) -> const seen_t*
	{
		for (const seen_t& s : seen)
			if (s.btn == btn)
				return &s;
		return nullptr;
	};

	// Nothing pressed, pressed ones only: the sticks still report (at rest)
	pads.apply_copilots();
	collect(true);
	CHECK(count_of(test_btn::a) == 0 && count_of(test_btn::b) == 0 && count_of(test_btn::d) == 0 && count_of(test_btn::e) == 0);

	// Cross: both of its buttons, in the order declared, with the value
	press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, 200);
	collect(true);
	CHECK(count_of(test_btn::a) == 1 && count_of(test_btn::c) == 1 && count_of(test_btn::b) == 0);
	if (const seen_t* a = first_of(test_btn::a))
	{
		CHECK(a->pressed && a->value == 200 && a->pad_btn == pad_button::cross);
		const seen_t* c = first_of(test_btn::c);
		CHECK(c && a < c && c->value == 200);
	}

	// Unpressed ones too when asked
	collect(false);
	CHECK(count_of(test_btn::b) == 1 && first_of(test_btn::b) && !first_of(test_btn::b)->pressed);
	CHECK(count_of(test_btn::d) == 1 && count_of(test_btn::e) == 1);

	// A callback stops the rest
	{
		u32 calls = 0;
		cfg.handle_input(pad, false, [&](const auto&, bool& abort) { calls++; abort = true; });
		CHECK(calls == 1);
	}

	// The left stick's X: the button of its direction; the right stick's X:
	// the button for both
	pad->m_sticks[0].m_value = 10;
	pad->m_sticks[2].m_value = 250;
	pads.apply_copilots();
	collect(true);
	CHECK(count_of(test_btn::left) == 1 && count_of(test_btn::right) == 0);
	CHECK(first_of(test_btn::left) && first_of(test_btn::left)->value == 10);
	CHECK(count_of(test_btn::any_x) == 1 && first_of(test_btn::any_x) && first_of(test_btn::any_x)->value == 250);
	pad->m_sticks[0].m_value = 240;
	pads.apply_copilots();
	collect(true);
	CHECK(count_of(test_btn::left) == 0 && count_of(test_btn::right) == 1);
	pad->m_sticks[0].m_value = 128;
	pad->m_sticks[2].m_value = 128;
	press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, 0);

	// No mapping: nothing
	cfg.clear_buttons();
	collect(false);
	CHECK(seen.empty());
	cfg.init_buttons();
	collect(false);
	CHECK(!seen.empty());

	// A poll while another thread is inside a callback of its own poll
	{
		std::atomic<bool> inside{false}, release{false}, done{false};
		press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, 255);
		std::thread holder([&]
		{
			cfg.handle_input(pad, true, [&](const auto&, bool& abort)
			{
				inside = true;
				while (!release)
					std::this_thread::yield();
				abort = true;
			});
		});
		while (!inside)
			std::this_thread::yield();
		std::thread other([&]
		{
			u32 calls = 0;
			cfg.handle_input(pad, true, [&](const auto&, bool&) { calls++; });
			done = calls != 0;
		});
		for (u64 spins = 0; !done && spins < 50'000'000ull; spins++)
			std::this_thread::yield();
		CHECK(done); // it waited for the first one's lock before
		release = true;
		holder.join();
		other.join();
		press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, 0);
	}

	// The mapping made again while four threads poll: all on Cross, then all
	// on Circle, over and over, with both held. A poll sees one of the two
	// whole (eight buttons of one kind) or the moment between (none).
	{
		press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, 255);
		press(CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CIRCLE, 255);
		all_to(cfg, pad_button::cross);
		cfg.clear_buttons();
		cfg.init_buttons();

		enum : u32 { pollers = 4 };
		std::atomic<bool> stop{false};
		std::atomic<u64> polls{0}, mixed{0}, whole{0}, none{0};
		std::thread t[pollers];
		for (u32 i = 0; i < pollers; i++)
		{
			t[i] = std::thread([&]
			{
				while (!stop)
				{
					u32 cross = 0, circle = 0, other = 0;
					cfg.handle_input(pad, true, [&](const auto& value, bool&)
					{
						if (value.pad_btn == pad_button::cross)
							cross++;
						else if (value.pad_btn == pad_button::circle)
							circle++;
						else
							other++;
					});
					if (other || (cross && circle) || (cross + circle != 0 && cross + circle != 8))
						mixed++;
					else if (cross + circle)
						whole++;
					else
						none++;
					polls++;
				}
			});
		}
		for (u32 n = 0; n < 20000; n++)
		{
			all_to(cfg, (n & 1) ? pad_button::cross : pad_button::circle);
			cfg.clear_buttons();
			cfg.init_buttons();
		}
		stop = true;
		for (u32 i = 0; i < pollers; i++)
			t[i].join();
		CHECK(mixed == 0);
		CHECK(whole != 0);
		std::printf("test_pad_buttons: %llu polls on %u threads while the mapping was made again 20000 times: %llu whole, %llu between two, %llu mixed\n",
			static_cast<unsigned long long>(polls.load()), static_cast<u32>(pollers), static_cast<unsigned long long>(whole.load()),
			static_cast<unsigned long long>(none.load()), static_cast<unsigned long long>(mixed.load()));
	}

	// A poll allocates nothing, whatever its callback holds on to. The
	// emulated controllers' callbacks hold three and more things (the PS
	// Move's stand-in its device, its data and its combinations); as a
	// std::function each was put on the heap for every poll, and the pad
	// was passed as a std::shared_ptr of its own, counted up and down.
	{
		cfg.clear_buttons();
		cfg.init_buttons();
		u32 device = 1, combos = 0;
		u64 data = 0, more = 0;
		const long owners = pad.use_count();
		long owners_seen = 0;

		t_allocations = 0;
		t_counting = true;
		for (int i = 0; i < 1000; i++)
		{
			cfg.handle_input(pad, false, [device, &data, &combos, &more, &owners_seen, &pad](const auto& value, bool&)
			{
				data += value.value + device;
				combos++;
				more += value.max_value;
				owners_seen = pad.use_count();
			});
		}
		t_counting = false;

		CHECK(combos > 0);
		CHECK(t_allocations == 0);
		CHECK(owners_seen == owners); // the poll borrows the pad
		std::printf("test_pad_buttons: 1000 polls with a callback holding six things: %llu allocations\n", static_cast<unsigned long long>(t_allocations));
	}

	if (std::getenv("PAD_BENCH"))
	{
		// The usual poll: a few buttons held, pressed ones only
		all_to(cfg, pad_button::cross);
		cfg.b.set(pad_button::circle);
		cfg.d.set(pad_button::dpad_up);
		cfg.e.set(pad_button::R2);
		cfg.left.set(pad_button::ls_left);
		cfg.right.set(pad_button::ls_right);
		cfg.any_x.set(pad_button::rs_x);
		cfg.clear_buttons();
		cfg.init_buttons();
		u32 sink = 0;
		double best = 1e9, best_all = 1e9;
		for (int r = 0; r < 9; r++)
		{
			timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < 500000; i++)
				cfg.handle_input(pad, true, [&](const auto& value, bool&) { sink += value.value; });
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			best = std::min(best, ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 500000);

			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < 500000; i++)
				cfg.handle_input(pad, false, [&](const auto& value, bool&) { sink += value.value; });
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			best_all = std::min(best_all, ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 500000);
		}
		std::printf("test_pad_buttons: a poll of the pressed buttons %.1f ns, of all sixteen %.1f ns (best of 9; %u)\n", best, best_all, sink);
	}

	std::printf("test_pad_buttons: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
