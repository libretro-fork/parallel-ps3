// The pads' buttons and sticks on their way from the frontend's thread to
// the game's (Emu/Io/pad_types.h, Input/pad_thread.cpp):
//
// - retro_run hands them over (pad_thread::apply_copilots) with no lock:
//   there is no pad lock any more (the library tests check that none comes
//   back);
// - a game's thread reading them (Pad::read_external) while retro_run
//   writes them over and over always gets one whole handover, never half
//   of one and half of the next;
// - what is read is what was handed over: buttons, sticks, a copilot's
//   buttons merged in;
// - the pad's status, changed by retro_run and by the game at once (an LDD
//   controller), loses neither's change;
// - while a dialog intercepts the pads, the input it ignores stays ignored:
//   retro_run letting go of ignore_input (no button held, nothing
//   intercepting) never undoes the dialog setting it again meanwhile;
// - rumble the game asks for runs as long as it asks: one not renewed for 3 s
//   of runs - counted in retro_runs, not by the host's clock - stops, and a
//   new request starts it again;
// - reading the frontend's buttons into the pads each retro_run
//   (LibretroPadHandler::process) allocates nothing;
// - nothing of the frontend's precision is lost that a DualShock 3 has: a
//   trigger or button the frontend reports half pressed reaches the game
//   half pressed (128), not fully (255); one it reports as a button only is
//   255; a stick's full range is the pad's full range, 0 to 255 with 128 at
//   rest, pushed all the way left or up being 0 (it was 1), and a small tilt
//   moves it (the first 1 % was lost).
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Input/pad_thread.h"
#include "Emu/Io/pad_types.h"
#include "libretro/libretro_input.h"
#include "libretro/libretro_pad_handler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

static int failures = 0;

static uint16_t s_strong[LIBRETRO_MAX_PADS];

static bool set_rumble_state(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
	if (port < LIBRETRO_MAX_PADS && effect == RETRO_RUMBLE_STRONG)
		s_strong[port] = strength;
	return true;
}

static bool environment(unsigned cmd, void* data)
{
	if (cmd != RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE)
		return false;
	static_cast<retro_rumble_interface*>(data)->set_rumble_state = set_rumble_state;
	return true;
}

// The frontend's input, as the test sets it
static int16_t s_pressure[16]; // RETRO_DEVICE_INDEX_ANALOG_BUTTON, 0..0x7fff
static bool s_digital[16];
static int16_t s_stick[4];     // left x, left y, right x, right y

static int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port != 0)
		return 0;
	if (device == RETRO_DEVICE_JOYPAD)
		return id < 16 ? (s_digital[id] || s_pressure[id] > 0) : 0;
	if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
		return id < 16 ? s_pressure[id] : 0;
	if (device == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT)
		return s_stick[index * 2 + (id == RETRO_DEVICE_ID_ANALOG_Y)];
	return 0;
}

static u16 button_value(Pad& pad, u32 offset, u32 code)
{
	for (const Button& b : pad.m_buttons)
		if (b.m_offset == offset && b.m_outKeyCode == code)
			return b.m_value;
	return 0xffff;
}

static u16 stick_value(Pad& pad, u32 offset)
{
	for (const AnalogStick& st : pad.m_sticks)
		if (st.m_offset == offset)
			return st.m_value;
	return 0xffff;
}

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

static void set_buttons(Pad& pad, u16 value, bool pressed)
{
	for (Button& b : pad.m_buttons)
	{
		b.m_value = value;
		b.m_pressed = pressed;
	}
	for (AnalogStick& s : pad.m_sticks)
		s.m_value = value & 0xff;
}

int main()
{
	// Two pads, as pad_thread::Init makes them (that wants the frontend for
	// its overlay message, so they are made here)
	pad_thread pads(nullptr, nullptr, "");
	pads.GetPads()[0] = std::make_shared<Pad>(pad_handler::null, 0, 0, CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_STANDARD);
	pads.GetPads()[1] = std::make_shared<Pad>(pad_handler::null, 1, 0, CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_STANDARD);
	auto& pad = pads.GetPads()[0];
	auto& copilot = pads.GetPads()[1];
	CHECK(pad && copilot);
	if (!pad || !copilot)
		return 1;

	pad->m_buttons.clear();
	pad->m_buttons.resize(16);
	for (u32 i = 0; i < 16; i++)
	{
		pad->m_buttons[i].m_offset = CELL_PAD_BTN_OFFSET_DIGITAL1 + (i >= 8);
		pad->m_buttons[i].m_outKeyCode = 1u << (i % 8);
	}
	for (u32 i = 0; i < 4; i++)
		pad->m_sticks[i].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X + i;
	retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_CONNECTED);

	// What is handed over is what is read
	set_buttons(*pad, 200, true);
	pads.apply_copilots();
	pad_external ext;
	pad->read_external(ext);
	CHECK(ext.button_count == 16);
	CHECK(ext.buttons[3].m_pressed && ext.buttons[3].m_value == 200 && ext.buttons[3].m_outKeyCode == (1u << 3));
	CHECK(ext.buttons[9].m_offset == CELL_PAD_BTN_OFFSET_DIGITAL2);
	CHECK(ext.sticks[2].m_offset == CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X + 2u && ext.sticks[2].m_value == 200);

	// A copilot's press merged in
	set_buttons(*pad, 0, false);
	copilot->m_buttons.resize(1);
	copilot->m_buttons[0].m_offset = CELL_PAD_BTN_OFFSET_DIGITAL1;
	copilot->m_buttons[0].m_outKeyCode = 1u << 5;
	copilot->m_buttons[0].m_pressed = true;
	copilot->m_buttons[0].m_value = 255;
	retro_atomic_fetch_or_int(&copilot->m_port_status, CELL_PAD_STATUS_CONNECTED);
	pad->copilots.push_back(copilot);
	pads.apply_copilots();
	pad->read_external(ext);
	CHECK(ext.buttons[5].m_pressed && ext.buttons[5].m_value == 255);
	CHECK(!ext.buttons[4].m_pressed);
	pad->copilots.clear();
	pads.apply_copilots(); // without the copilot's press

	// One whole handover at a time: every button of a read has the same value
	{
		std::atomic<bool> stop{false};
		std::thread run([&]
		{
			for (u16 v = 0; !stop; v = (v + 1) % 1000)
			{
				set_buttons(*pad, v, (v & 1) != 0);
				pads.apply_copilots();
			}
		});
		u32 torn = 0;
		for (int i = 0; i < 200000; i++)
		{
			pad->read_external(ext);
			for (u32 b = 1; b < ext.button_count; b++)
				if (ext.buttons[b].m_value != ext.buttons[0].m_value || ext.buttons[b].m_pressed != ext.buttons[0].m_pressed)
				{
					torn++;
					break;
				}
		}
		stop = true;
		run.join();
		CHECK(torn == 0);
		std::printf("test_pad_handoff: 200000 reads while retro_run writes, %u torn\n", torn);
	}

	// The status: retro_run connecting and disconnecting, the game flagging
	// changes, at once
	{
		retro_atomic_store_release_int(&pad->m_port_status, 0);
		std::thread game([&]
		{
			for (int i = 0; i < 200000; i++)
			{
				retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_ASSIGN_CHANGES);
				retro_atomic_fetch_and_int(&pad->m_port_status, ~CELL_PAD_STATUS_ASSIGN_CHANGES);
			}
			retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_ASSIGN_CHANGES);
		});
		for (int i = 0; i < 200000; i++)
		{
			retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_CONNECTED);
			retro_atomic_fetch_and_int(&pad->m_port_status, ~CELL_PAD_STATUS_CONNECTED);
		}
		retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_CONNECTED);
		game.join();
		CHECK(pad->port_status() == (CELL_PAD_STATUS_CONNECTED | CELL_PAD_STATUS_ASSIGN_CHANGES));
	}

	// A dialog intercepting the pads over and over while retro_run runs
	{
		// Every pad connected with many buttons, none held: retro_run looks
		// at all of them between reading the state and letting go, which
		// leaves the dialog room to intercept again in between
		for (auto& p : pads.GetPads())
		{
			if (!p)
				p = std::make_shared<Pad>(pad_handler::null, 7, 0, CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_STANDARD);
			retro_atomic_fetch_or_int(&p->m_port_status, CELL_PAD_STATUS_CONNECTED);
			p->m_buttons.resize(512);
		}
		set_buttons(*pad, 0, false);
		for (auto& p : pads.GetPads())
			for (Button& b : p->m_buttons)
				b.m_pressed = false;

		std::atomic<bool> stop{false};
		std::thread run([&]
		{
			while (!stop)
				pads.frontend_update(1);
		});
		u32 lost = 0;
		for (int i = 0; i < 20000; i++)
		{
			pads.SetIntercepted(true);
			for (int k = 0; k < 200; k++)
				if (!pads.GetInfo().ignore_input())
				{
					lost++;
					break;
				}
			pads.SetIntercepted(false);
		}
		stop = true;
		run.join();
		CHECK(lost == 0);
		CHECK(!(pads.GetInfo().system_info() & CELL_PAD_INFO_INTERCEPTED));
		std::printf("test_pad_handoff: 20000 interceptions while retro_run let go of ignore_input, %u lost\n", lost);
	}

	// Rumble counted in runs: 30 fps, so 90 runs is 3 s
	{
		LibretroPadHandler handler;
		CHECK(libretro_input_init_rumble(environment));
		handler.set_frame_rate(30.0);
		auto rumbled = std::make_shared<Pad>(pad_handler::null, 0, 0, CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_STANDARD);
		pads.GetPads()[0] = rumbled;
		CHECK(handler.bindPadToDevice(rumbled));
		const unsigned port = libretro_input_port_for_player(0);

		handler.process();
		CHECK(s_strong[port] == 0);

		// A thousand frames of input: nothing allocated
		t_allocations = 0;
		t_counting = true;
		for (int i = 0; i < 1000; i++)
			handler.process();
		t_counting = false;
		CHECK(t_allocations == 0);
		std::printf("test_pad_handoff: 1000 frames of input read, %llu allocations\n", static_cast<unsigned long long>(t_allocations));

		// The frontend's precision, kept
		{
			libretro_input_init();
			const auto frame = [&] { libretro_input_poll(input_state); handler.process(); };

			// A trigger half pressed, a button the frontend has as a button only
			s_pressure[RETRO_DEVICE_ID_JOYPAD_R2] = 0x4000;
			s_digital[RETRO_DEVICE_ID_JOYPAD_B] = true;
			frame();
			const u16 r2 = button_value(*rumbled, CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_R2);
			const u16 cross = button_value(*rumbled, CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS);
			const u16 l2 = button_value(*rumbled, CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_L2);
			CHECK(r2 == 128);
			CHECK(cross == 255);
			CHECK(l2 == 0);

			// Barely pressed is pressed, fully is 255
			s_pressure[RETRO_DEVICE_ID_JOYPAD_R2] = 1;
			frame();
			CHECK(button_value(*rumbled, CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_R2) == 1);
			s_pressure[RETRO_DEVICE_ID_JOYPAD_R2] = 0x7fff;
			frame();
			CHECK(button_value(*rumbled, CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_R2) == 255);
			s_pressure[RETRO_DEVICE_ID_JOYPAD_R2] = 0;
			s_digital[RETRO_DEVICE_ID_JOYPAD_B] = false;

			// One axis at a time (the other at rest): full range to full range
			const auto left_x = [&](int16_t v)
			{
				s_stick[0] = v;
				frame();
				return stick_value(*rumbled, CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X);
			};
			const u16 rest = left_x(0), full_left = left_x(-32768), full_right = left_x(32767);
			const u16 small_tilt = left_x(400), small_left = left_x(-400);
			CHECK(rest == 128);
			CHECK(full_left == 0);
			CHECK(full_right == 255);
			CHECK(small_tilt > 128);
			CHECK(small_left < 128);
			u32 backwards = 0, reached = 0;
			bool seen[256] = {};
			u16 last = 0;
			for (int v = -32768; v <= 32767; v += 16)
			{
				const u16 x = left_x(static_cast<int16_t>(v));
				if (x < last)
					backwards++;
				last = x;
				if (x < 256 && !seen[x])
				{
					seen[x] = true;
					reached++;
				}
			}
			CHECK(backwards == 0);
			CHECK(reached == 256);
			s_stick[0] = 0;
			frame();
			std::printf("test_pad_handoff: half-pressed R2 reads %u, a button-only Cross %u; the stick reads %u at rest, %u full left, %u full right, %u at a 1%% tilt, %u of 256 values reached\n",
				r2, cross, rest, full_left, full_right, small_tilt, reached);
		}

		// Against the code this replaced, kept here as the reference: for
		// each of a pad's buttons the nested switch on its offset and code,
		// and RPCS3's squircle worked out for each stick each frame. Every
		// position of both sticks (256 by 256, the right stick's mirrored),
		// with the buttons changing along.
		{
			libretro_input_init();
			u32 wrong_buttons = 0, wrong_sticks = 0;

			const auto reference_button = [](const lr_pad_values& now, u32 offset, u32 code) -> u16
			{
				switch (offset)
				{
				case CELL_PAD_BTN_OFFSET_DIGITAL1:
					switch (code)
					{
					case CELL_PAD_CTRL_UP:     return now.button[RETRO_DEVICE_ID_JOYPAD_UP];
					case CELL_PAD_CTRL_DOWN:   return now.button[RETRO_DEVICE_ID_JOYPAD_DOWN];
					case CELL_PAD_CTRL_LEFT:   return now.button[RETRO_DEVICE_ID_JOYPAD_LEFT];
					case CELL_PAD_CTRL_RIGHT:  return now.button[RETRO_DEVICE_ID_JOYPAD_RIGHT];
					case CELL_PAD_CTRL_SELECT: return now.button[RETRO_DEVICE_ID_JOYPAD_SELECT];
					case CELL_PAD_CTRL_START:  return now.button[RETRO_DEVICE_ID_JOYPAD_START];
					case CELL_PAD_CTRL_L3:     return now.button[RETRO_DEVICE_ID_JOYPAD_L3];
					case CELL_PAD_CTRL_R3:     return now.button[RETRO_DEVICE_ID_JOYPAD_R3];
					default: break;
					}
					break;
				case CELL_PAD_BTN_OFFSET_DIGITAL2:
					switch (code)
					{
					case CELL_PAD_CTRL_CROSS:    return now.button[RETRO_DEVICE_ID_JOYPAD_B];
					case CELL_PAD_CTRL_CIRCLE:   return now.button[RETRO_DEVICE_ID_JOYPAD_A];
					case CELL_PAD_CTRL_SQUARE:   return now.button[RETRO_DEVICE_ID_JOYPAD_Y];
					case CELL_PAD_CTRL_TRIANGLE: return now.button[RETRO_DEVICE_ID_JOYPAD_X];
					case CELL_PAD_CTRL_L1:       return now.button[RETRO_DEVICE_ID_JOYPAD_L];
					case CELL_PAD_CTRL_R1:       return now.button[RETRO_DEVICE_ID_JOYPAD_R];
					case CELL_PAD_CTRL_L2:       return now.button[RETRO_DEVICE_ID_JOYPAD_L2];
					case CELL_PAD_CTRL_R2:       return now.button[RETRO_DEVICE_ID_JOYPAD_R2];
					default: break;
					}
					break;
				default:
					break;
				}
				return 0;
			};

			u32 n = 0;
			for (int y = 0; y < 256; y++)
			{
				for (int x = 0; x < 256; x++, n++)
				{
					// The stick values that read as exactly x and y
					s_stick[0] = static_cast<int16_t>(x * 257 - 32768);
					s_stick[1] = static_cast<int16_t>(y * 257 - 32768);
					s_stick[2] = static_cast<int16_t>((255 - y) * 257 - 32768);
					s_stick[3] = static_cast<int16_t>(x * 257 - 32768);
					for (int b = 0; b < 16; b++)
					{
						s_digital[b] = ((n * 2654435761u) >> (b + 8)) & 1;
						s_pressure[b] = (n + b) % 5 == 0 ? static_cast<int16_t>((n * 40503u + b * 977u) & 0x7fff) : 0;
					}

					libretro_input_poll(input_state);
					handler.process();

					const LibretroInputState* state = libretro_input_get_state(libretro_input_port_for_player(0));
					lr_pad_values now;
					lr_pad_values_read(state->pressure, state->buttons, state->analog, &now);

					for (const Button& button : rumbled->m_buttons)
					{
						const u16 value = reference_button(now, button.m_offset, button.m_outKeyCode);
						wrong_buttons += button.m_value != value || button.m_pressed != (value > 0);
					}

					u16 lx = now.axis[0], ly = now.axis[1], rx = now.axis[2], ry = now.axis[3];
					PadHandlerBase::ConvertToSquirclePoint(lx, ly, 4000);
					PadHandlerBase::ConvertToSquirclePoint(rx, ry, 4000);
					wrong_sticks += lx != stick_value(*rumbled, CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X) || ly != stick_value(*rumbled, CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y)
						|| rx != stick_value(*rumbled, CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X) || ry != stick_value(*rumbled, CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y);
					if (n == 0)
						CHECK(now.axis[0] == 0 && now.axis[1] == 0 && now.axis[2] == 255);
				}
			}

			CHECK(wrong_buttons == 0);
			CHECK(wrong_sticks == 0);
			std::printf("test_pad_handoff: 65536 stick positions and buttons against the code replaced: %u buttons and %u sticks differ\n", wrong_buttons, wrong_sticks);

			for (int b = 0; b < 16; b++)
			{
				s_digital[b] = false;
				s_pressure[b] = 0;
			}
			s_stick[0] = s_stick[1] = s_stick[2] = s_stick[3] = 0;
			libretro_input_poll(input_state);
			handler.process();
		}

		// PAD_BENCH=1: the time of a frame's read, for comparing builds
		if (std::getenv("PAD_BENCH"))
		{
			s_stick[0] = 12345;
			s_stick[3] = -4321;
			s_digital[RETRO_DEVICE_ID_JOYPAD_B] = true;
			libretro_input_poll(input_state);
			const auto from = std::chrono::steady_clock::now();
			for (int i = 0; i < 2000000; i++)
				handler.process();
			const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - from).count();
			std::printf("test_pad_handoff: %.1f ns a frame's read of one pad\n", ns / 2000000.0);
			// And with the sticks somewhere else every frame
			const auto from2 = std::chrono::steady_clock::now();
			for (int i = 0; i < 2000000; i++)
			{
				LibretroInputState* st = const_cast<LibretroInputState*>(libretro_input_get_state(libretro_input_port_for_player(0)));
				st->analog[0] = static_cast<int16_t>(i * 37);
				st->analog[1] = static_cast<int16_t>(i * 101);
				st->analog[2] = static_cast<int16_t>(i * 13);
				st->analog[3] = static_cast<int16_t>(i * 71);
				handler.process();
			}
			const double ns2 = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - from2).count();
			std::printf("test_pad_handoff: %.1f ns with both sticks moving every frame\n", ns2 / 2000000.0);
			s_stick[0] = s_stick[3] = 0;
			s_digital[RETRO_DEVICE_ID_JOYPAD_B] = false;
			libretro_input_poll(input_state);
			handler.process();
		}

		pads.SetRumble(0, 255, 0);
		u32 runs = 0;
		for (; runs < 1000; runs++)
		{
			handler.process();
			if (s_strong[port] == 0)
				break;
		}
		CHECK(runs == 91);

		// Asked again: it starts again, and lasts as long again
		pads.SetRumble(0, 255, 0);
		handler.process();
		CHECK(s_strong[port] != 0);
		for (u32 i = 0; i < 60; i++)
		{
			if (i == 30)
				pads.SetRumble(0, 255, 0); // renewed half way
			handler.process();
		}
		CHECK(s_strong[port] != 0);
		for (u32 i = 0; i < 100; i++)
			handler.process();
		CHECK(s_strong[port] == 0);
		std::printf("test_pad_handoff: rumble stopped %u runs after the game last asked (3 s at 30 fps)\n", runs);
	}

	std::printf("test_pad_handoff: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
