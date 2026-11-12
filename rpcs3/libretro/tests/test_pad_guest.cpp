// cellPad from several of the game's threads at once, with no lock
// (Emu/Cell/Modules/cellPad.cpp, Input/pad_thread.cpp): a port's game side is
// one record (pad_guest_state, retro_cas_record) and a call is one change of
// it.
//
// - A change of the buttons is given to the game once: of the threads
//   calling cellPadGetData on a port, one gets it and the others get "no
//   change", as when a lock made the calls take turns. Checked for 500
//   changes with four threads calling.
// - cellPadGetData gives everything after cellPadClearBuf, and the length the
//   port's setting asks for.
// - A port's settings changed by two threads at once keep both threads'
//   changes.
// - Custom (LDD) controllers: of more threads registering than there are free
//   ports, each free port goes to exactly one; of two threads unregistering
//   one, one does; registering and unregistering over and over from four
//   threads never gives two threads the same port, and leaves every port
//   free, disconnected and reported so, with the count the frontend's side
//   reads back at none.
// - With the sensors on, a report comes every 10 ms of the PS3's clock with
//   no change of input - not of the host's.
// - What a call pays to have the port to itself, against the pad lock it
//   took before (PAD_BENCH=1).
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"
#include "Emu/Cell/Modules/cellPad.h"
#include "Emu/Cell/timers.hpp"
#include "Emu/Io/pad_config.h"
#include "Emu/Io/pad_types.h"
#include "Input/pad_thread.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <time.h> // not pacing: the benchmark's stopwatch

extern bool cellPad_NotifyStateChange(usz index, u64 state, bool lock = true, bool is_blocking = true);
extern void libretro_guest_clock_grant(double frame_seconds);
error_code cellPadClearBuf(u32 port_no);
error_code cellPadSetPressMode(u32 port_no, u32 mode);
error_code cellPadSetSensorMode(u32 port_no, u32 mode);
error_code cellPadInfoPressMode(u32 port_no);
error_code cellPadLddRegisterController();
error_code cellPadLddGetPortNo(s32 handle);
error_code cellPadLddUnregisterController(s32 handle);

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static s32 code(error_code error)
{
	return static_cast<s32>(error);
}

static void set_buttons(Pad& pad, u16 value)
{
	for (Button& b : pad.m_buttons)
	{
		b.m_pressed = value != 0;
		b.m_value = value;
	}
}

static pad_guest_state guest_of(Pad& pad)
{
	pad_guest_state state;
	retro_cas_record_read(&pad.m_guest, &state);
	return state;
}

int main()
{
	g_fxo->reset();
	g_fxo->init<pad_info>();
	auto& config = g_fxo->get<pad_info>();

	// The pads as pad_thread::Init makes them (that wants the frontend for
	// its overlay message, so they are made here): port 1 a DualShock 3,
	// the others free for custom controllers
	pad_thread pads(nullptr, nullptr, "");
	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
	{
		pads.GetPads()[i] = std::make_shared<Pad>(pad_handler::null, i, 0,
			CELL_PAD_CAPABILITY_PS3_CONFORMITY | CELL_PAD_CAPABILITY_PRESS_MODE | CELL_PAD_CAPABILITY_SENSOR_MODE | CELL_PAD_CAPABILITY_ACTUATOR,
			CELL_PAD_DEV_TYPE_STANDARD);
		g_cfg_input.player[i]->handler.set(i ? pad_handler::null : pad_handler::keyboard);
	}

	Pad& pad = *pads.GetPads()[0];
	pad.m_buttons.resize(16);
	for (u32 i = 0; i < 16; i++)
	{
		pad.m_buttons[i].m_offset = CELL_PAD_BTN_OFFSET_DIGITAL1 + (i >= 8);
		pad.m_buttons[i].m_outKeyCode = 1u << (i % 8);
	}
	for (u32 i = 0; i < 4; i++)
		pad.m_sticks[i].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X + i;
	retro_atomic_fetch_or_int(&pad.m_port_status, CELL_PAD_STATUS_CONNECTED);

	// Not initialized, and not while a cellPadInit is under way
	CHECK(code(cellPadClearBuf(0)) == static_cast<s32>(CELL_PAD_ERROR_UNINITIALIZED));
	config.max_connect = pad_info::starting;
	CHECK(config.initialized() == 0 && config.get_max_connect() == 0);
	CHECK(code(cellPadClearBuf(0)) == static_cast<s32>(CELL_PAD_ERROR_UNINITIALIZED));
	config.max_connect = CELL_PAD_MAX_PORT_NUM;
	CHECK(config.initialized() == CELL_PAD_MAX_PORT_NUM);

	// The game is told of the pad
	CellPadData data{};
	CHECK(!cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr)); // not yet
	CHECK(cellPad_NotifyStateChange(0, CELL_PAD_STATUS_CONNECTED));
	CHECK(guest_of(pad).reported.port_status & CELL_PAD_STATUS_CONNECTED);
	CHECK(guest_of(pad).reported.port_status & CELL_PAD_STATUS_ASSIGN_CHANGES);
	CHECK(guest_of(pad).reported.device_capability & CELL_PAD_CAPABILITY_ACTUATOR);

	// Everything the first time, then nothing until something changes
	pads.apply_copilots();
	CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
	CHECK(data.len == CELL_PAD_LEN_CHANGE_DEFAULT);
	CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
	CHECK(data.len == CELL_PAD_LEN_NO_CHANGE);
	set_buttons(pad, 200);
	pads.apply_copilots();
	CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
	CHECK(data.len == CELL_PAD_LEN_CHANGE_DEFAULT);
	CHECK(data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] == 0xff && data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] == 0xff);
	CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
	CHECK(data.len == CELL_PAD_LEN_NO_CHANGE);

	// Everything again after cellPadClearBuf; with the pressures when the
	// port's setting asks for them
	CHECK(code(cellPadClearBuf(0)) == CELL_OK);
	CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
	CHECK(data.len == CELL_PAD_LEN_CHANGE_DEFAULT);
	CHECK(code(cellPadInfoPressMode(0)) == 1);
	CHECK(code(cellPadSetPressMode(0, 1)) == CELL_OK);
	CHECK(code(cellPadClearBuf(0)) == CELL_OK);
	u32 pclass_type = 99, pclass_profile = 99;
	CHECK(cellPad_port_get_data(pad, &data, true, nullptr, &pclass_type, &pclass_profile));
	CHECK(data.len == CELL_PAD_LEN_CHANGE_PRESS_ON);
	CHECK(data.button[CELL_PAD_BTN_OFFSET_PRESS_CROSS] == 200);
	CHECK(pclass_type == pad.m_class_type && pclass_profile == pad.m_class_profile);
	CHECK(code(cellPadSetPressMode(0, 0)) == CELL_OK);
	CHECK(code(cellPadSetPressMode(0, 2)) == static_cast<s32>(CELL_PAD_ERROR_INVALID_PARAMETER));
	CHECK(code(cellPadClearBuf(1)) == static_cast<s32>(CELL_PAD_ERROR_NO_DEVICE));

	// A change of the buttons goes to one of the threads asking
	{
		enum : u32 { callers = 4, changes = 500 };
		std::atomic<bool> stop{false};
		std::atomic<u32> reports{0};
		std::atomic<u32> wrong{0};
		std::atomic<u64> calls[callers]{};
		std::thread t[callers];

		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr)); // whatever is left
		for (u32 i = 0; i < callers; i++)
		{
			t[i] = std::thread([&, i]
			{
				CellPadData mine{};
				while (!stop)
				{
					if (!cellPad_port_get_data(pad, &mine, false, nullptr, nullptr, nullptr))
						wrong++;
					else if (mine.len > CELL_PAD_LEN_NO_CHANGE)
					{
						if (mine.len != CELL_PAD_LEN_CHANGE_DEFAULT)
							wrong++;
						reports++;
					}
					calls[i]++;
				}
			});
		}

		u32 late = 0;
		for (u32 n = 1; n <= changes; n++)
		{
			set_buttons(pad, static_cast<u16>(1 + n % 250));
			pads.apply_copilots();

			// Until a thread got it, then until each has asked a few more times
			u64 spins = 0;
			while (reports < n && ++spins < 2'000'000'000ull)
				std::this_thread::yield();
			u64 seen[callers];
			for (u32 i = 0; i < callers; i++)
				seen[i] = calls[i];
			for (u32 i = 0; i < callers; i++)
				while (calls[i] < seen[i] + 3)
					std::this_thread::yield();
			if (reports != n)
				late++;
		}
		stop = true;
		for (u32 i = 0; i < callers; i++)
			t[i].join();

		CHECK(reports == changes);
		CHECK(late == 0);
		CHECK(wrong == 0);
		std::printf("test_pad_guest: %u changes of the buttons, %u threads asking: %u given to the game, %u given twice or missed\n",
			static_cast<u32>(changes), static_cast<u32>(callers), reports.load(), late);
	}

	// Two threads changing the port's setting, a third clearing its buffer
	{
		enum : u32 { rounds = 50000 };
		std::atomic<u32> wrong{0};
		std::atomic<bool> stop{false};
		std::thread press([&] { for (u32 i = 0; i <= rounds; i++) if (code(cellPadSetPressMode(0, (i & 1) ^ 1)) != CELL_OK) wrong++; });
		std::thread sensor([&] { for (u32 i = 0; i <= rounds; i++) if (code(cellPadSetSensorMode(0, (i & 1) ^ 1)) != CELL_OK) wrong++; });
		std::thread clear([&] { while (!stop) if (code(cellPadClearBuf(0)) != CELL_OK) wrong++; });
		press.join();
		sensor.join();
		stop = true;
		clear.join();
		CHECK(wrong == 0);
		// Both ended on
		const u32 setting = guest_of(pad).port_setting;
		CHECK((setting & CELL_PAD_SETTING_PRESS_ON) && (setting & CELL_PAD_SETTING_SENSOR_ON));
		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
		CHECK(data.len == CELL_PAD_LEN_CHANGE_SENSOR_ON);
	}

	// The sensors on: a report every 10 ms of the PS3's clock, which moves
	// with retro_run and not with the host
	{
		// The PS3's clock runs up to what the last retro_run granted and
		// stops there: let it
		libretro_guest_clock_grant(1.0 / 60.0);
		for (u32 same = 0; same < 100000;)
		{
			const u64 t = get_guest_system_time();
			same = t == get_guest_system_time() ? same + 1 : 0;
		}
		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
		u32 early = 0;
		timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0); // not pacing: 50 ms of the host's time go by
		for (;;)
		{
			CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
			early += data.len != CELL_PAD_LEN_NO_CHANGE;
			clock_gettime(CLOCK_MONOTONIC, &t1); // not pacing: 50 ms of the host's time go by
			if ((t1.tv_sec - t0.tv_sec) * 1000000000ll + (t1.tv_nsec - t0.tv_nsec) > 50000000ll)
				break;
		}
		const u64 before = get_guest_system_time();
		for (u32 i = 0; i < 3; i++)
			libretro_guest_clock_grant(1.0 / 60.0);
		// The clock is the host's between grants, capped at the last grant:
		// let it get there
		u64 spins = 0;
		while (get_guest_system_time() - before < 10'000 && ++spins < 2'000'000'000ull)
			std::this_thread::yield();
		CHECK(get_guest_system_time() - before >= 10'000);
		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
		CHECK(data.len == CELL_PAD_LEN_CHANGE_SENSOR_ON);
		CHECK(cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr));
		CHECK(data.len == CELL_PAD_LEN_NO_CHANGE);
		CHECK(early == 0);
		CHECK(code(cellPadSetSensorMode(0, 0)) == CELL_OK);
		CHECK(code(cellPadSetPressMode(0, 0)) == CELL_OK);
	}

	// Custom controllers: six free ports, eight threads registering
	{
		enum : u32 { free_ports = CELL_PAD_MAX_PORT_NUM - 1, askers = 8 };
		std::atomic<bool> go{false};
		std::atomic<s32> got[askers];
		std::thread t[askers];
		for (u32 i = 0; i < askers; i++)
		{
			t[i] = std::thread([&, i]
			{
				while (!go)
					std::this_thread::yield();
				got[i] = code(cellPadLddRegisterController());
			});
		}
		go = true;
		for (u32 i = 0; i < askers; i++)
			t[i].join();

		u32 owners[CELL_PAD_MAX_PORT_NUM] = {}, refused = 0;
		for (u32 i = 0; i < askers; i++)
		{
			const s32 handle = got[i];
			if (handle >= 1 && handle < static_cast<s32>(CELL_PAD_MAX_PORT_NUM))
				owners[handle]++;
			else
			{
				CHECK(handle == static_cast<s32>(CELL_PAD_ERROR_TOO_MANY_DEVICES));
				refused++;
			}
		}
		CHECK(refused == askers - free_ports);
		for (u32 port = 1; port < CELL_PAD_MAX_PORT_NUM; port++)
		{
			Pad& custom = *pads.GetPads()[port];
			const pad_guest_state state = guest_of(custom);
			CHECK(owners[port] == 1);
			CHECK(state.ldd && state.port_setting == 0);
			CHECK(custom.is_connected());
			CHECK(custom.port_status() & CELL_PAD_STATUS_CUSTOM_CONTROLLER);
			CHECK(state.reported.port_status & CELL_PAD_STATUS_CONNECTED);
			CHECK(state.reported.device_type == CELL_PAD_DEV_TYPE_LDD);
			CHECK(code(cellPadLddGetPortNo(port)) == static_cast<s32>(port));
		}
		CHECK(owners[0] == 0 && !guest_of(pad).ldd);
		pads.frontend_update(1);
		CHECK(pads.GetInfo().now_connect() == 1 + free_ports);

		// What the game inserts for one is what cellPadGetData gives for it
		{
			Pad& custom = *pads.GetPads()[3];
			retro_cas_record_txn_t txn;
			do
			{
				pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&custom.m_guest, &txn));
				state->ldd_data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] = 0x1234;
			}
			while (!retro_cas_record_commit(&custom.m_guest, &txn));
			CHECK(cellPad_port_get_data(custom, &data, false, nullptr, nullptr, nullptr));
			CHECK(data.len == CELL_PAD_LEN_CHANGE_DEFAULT && data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] == 0x1234);
		}

		// Two threads unregistering each: one does
		std::atomic<u32> done{0}, gone{0}, other{0};
		go = false;
		std::thread u[2];
		for (u32 i = 0; i < 2; i++)
		{
			u[i] = std::thread([&]
			{
				while (!go)
					std::this_thread::yield();
				for (u32 port = 1; port < CELL_PAD_MAX_PORT_NUM; port++)
				{
					const s32 result = code(cellPadLddUnregisterController(port));
					if (result == CELL_OK)
						done++;
					else if (result == static_cast<s32>(CELL_PAD_ERROR_NO_DEVICE))
						gone++;
					else
						other++;
				}
			});
		}
		go = true;
		u[0].join();
		u[1].join();
		CHECK(done == free_ports && gone == free_ports && other == 0);
		CHECK(code(cellPadLddGetPortNo(2)) == static_cast<s32>(CELL_PAD_ERROR_FATAL));

		// Over and over from four threads
		enum : u32 { threads = 4, rounds = 5000 };
		std::atomic<u32> owner[CELL_PAD_MAX_PORT_NUM]{};
		std::atomic<u32> shared{0}, failed{0}, registered{0};
		std::thread c[threads];
		for (u32 i = 0; i < threads; i++)
		{
			c[i] = std::thread([&, i]
			{
				for (u32 n = 0; n < rounds; n++)
				{
					const s32 handle = code(cellPadLddRegisterController());
					if (handle < 1 || handle >= static_cast<s32>(CELL_PAD_MAX_PORT_NUM))
					{
						failed++;
						continue;
					}
					if (owner[handle].exchange(i + 1) != 0)
						shared++;
					if (code(cellPadLddGetPortNo(handle)) != handle)
						failed++;
					registered++;
					owner[handle] = 0;
					if (code(cellPadLddUnregisterController(handle)) != CELL_OK)
						failed++;
				}
			});
		}
		for (u32 i = 0; i < threads; i++)
			c[i].join();

		CHECK(shared == 0);
		CHECK(failed == 0);
		CHECK(registered == threads * rounds);
		for (u32 port = 1; port < CELL_PAD_MAX_PORT_NUM; port++)
		{
			Pad& custom = *pads.GetPads()[port];
			const pad_guest_state state = guest_of(custom);
			CHECK(!state.ldd);
			CHECK(!custom.is_connected());
			CHECK(!(state.reported.port_status & CELL_PAD_STATUS_CONNECTED));
		}
		pads.frontend_update(1);
		CHECK(pads.GetInfo().now_connect() == 1);
		std::printf("test_pad_guest: %u custom controllers registered and unregistered by %u threads, %u ports shared, %u calls failed\n",
			registered.load(), static_cast<u32>(threads), shared.load(), failed.load());
	}

	// What a call pays to have the port to itself: the pad lock it took
	// before, taken and given back, against the record read (nothing new
	// for the game, most calls) and read and replaced (something new). And
	// the whole call.
	if (std::getenv("PAD_BENCH"))
	{
		enum { loops = 4000000 };
		// A lock of the kind pad::g_pad_mutex was
		shared_mutex old_pad_lock;
		double lock_ns = 1e9, abort_ns = 1e9, commit_ns = 1e9, call_ns = 1e9;
		const auto since = [](const timespec& a)
		{
			timespec b;
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / loops;
		};
		for (int r = 0; r < 9; r++)
		{
			timespec a;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
			{
				std::lock_guard lock(old_pad_lock);
			}
			lock_ns = std::min(lock_ns, since(a));

			pad_guest_state copy;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
				retro_cas_record_read_versioned(&pad.m_guest, &copy);
			abort_ns = std::min(abort_ns, since(a));

			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
				retro_cas_record_replace(&pad.m_guest, retro_cas_record_read_versioned(&pad.m_guest, &copy), &copy);
			commit_ns = std::min(commit_ns, since(a));

			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
				cellPad_port_get_data(pad, &data, false, nullptr, nullptr, nullptr);
			call_ns = std::min(call_ns, since(a));
		}
		std::printf("test_pad_guest: the pad lock taken and given back %.1f ns; the port's record read %.1f ns, read and replaced %.1f ns; cellPadGetData with nothing new %.1f ns (best of 9)\n",
			lock_ns, abort_ns, commit_ns, call_ns);
	}

	std::printf("test_pad_guest: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	// The emulator's object map wants an emulator's shutdown; there is none here
	std::_Exit(failures ? 1 : 0);
}
