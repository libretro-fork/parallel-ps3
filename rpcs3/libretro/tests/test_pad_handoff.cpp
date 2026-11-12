// The pads' buttons and sticks on their way from the frontend's thread to
// the game's (Emu/Io/pad_types.h, Input/pad_thread.cpp):
//
// - retro_run hands them over (pad_thread::apply_copilots) while the game
//   holds the pad lock: it does not wait for it;
// - a game's thread reading them (Pad::read_external) while retro_run
//   writes them over and over always gets one whole handover, never half
//   of one and half of the next;
// - what is read is what was handed over: buttons, sticks, a copilot's
//   buttons merged in;
// - the pad's status, changed by retro_run and by the game at once (an LDD
//   controller), loses neither's change.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Input/pad_thread.h"
#include "Emu/Io/pad_types.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

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

	// The game holding the pad lock: the handover does not wait for it
	{
		std::atomic<bool> held{false}, release{false};
		std::thread game([&]
		{
			std::lock_guard lock(pad::g_pad_mutex);
			held = true;
			while (!release)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		});
		while (!held)
			std::this_thread::yield();

		std::atomic<bool> done{false};
		std::thread run([&] { pads.apply_copilots(); done = true; });
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!done && std::chrono::steady_clock::now() < until)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		CHECK(done);
		release = true;
		run.join();
		game.join();
	}

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

	std::printf("test_pad_handoff: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
