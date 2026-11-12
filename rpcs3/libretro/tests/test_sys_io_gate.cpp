// A pad connected or disconnected on its way to the game (sys_io's event
// queue, Emu/Cell/Modules/sys_io_.cpp): retro_run sends it, and must not
// wait for the game's threads to do so.
//
// - sys_config_stop holds sys_io's mutex while it waits for the game's
//   handler thread to end. A send from the frontend's thread meanwhile
//   returns, with the event in the queue: it does not wait for that mutex.
// - Not started, or stopped: nothing is sent, and nothing waits.
// - The queue is never used after sys_config_stop has gone on to destroy
//   it: a thread sending and one stopping and starting, 200000 times over,
//   and no send is ever under way while the gate is closed.
//
// The module is compiled into this test (its state is private to its source
// file); the rest is the core's own objects and archives, the same link as
// the core's with this main. Built by `make check`.

#include "../../Emu/Cell/Modules/sys_io_.cpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main()
{
	g_fxo->reset();
	g_fxo->init<id_manager::id_map<lv2_obj>>();
	g_fxo->init<libio_sys_config>();
	auto& cfg = g_fxo->get<libio_sys_config>();

	const u32 queue_id = idm::make<lv2_obj, lv2_event_queue>(SYS_SYNC_PRIORITY, SYS_PPU_QUEUE, 0x20, 0, 0);
	CHECK(queue_id != 0);
	const auto queue = idm::get_unlocked<lv2_obj, lv2_event_queue>(queue_id);
	CHECK(!!queue);
	queue->exists++; // as sys_event_queue_create leaves it

	// Not started: nothing sent
	cfg.queue_id = queue_id;
	send_sys_io_connect_event(1, 1);
	CHECK(lv2_events_empty(&queue->events));

	// Started, as sys_config_start leaves it
	cfg.init_ctr = 1;
	sys_io_gate_open(&cfg.gate);

	// A game's thread in sys_config_stop, waiting for the handler thread
	// with the mutex held: retro_run's send goes through
	{
		std::atomic<bool> held{false}, release{false}, sent{false};
		std::thread game([&]
		{
			std::lock_guard lock(cfg.mtx);
			held = true;
			while (!release)
				std::this_thread::yield();
		});

		while (!held)
			std::this_thread::yield();

		std::thread run([&]
		{
			send_sys_io_connect_event(2, 1);
			sent = true;
		});

		for (int i = 0; i < 20'000'000 && !sent; i++)
			std::this_thread::yield();

		CHECK(sent);
		release = true;
		run.join();
		game.join();

		CHECK(lv2_events_count(&queue->events) == 1);
		if (!lv2_events_empty(&queue->events))
		{
			const auto [source, d1, d2, d3] = *lv2_events_front(&queue->events);
			CHECK(source == 0 && d1 == 1 && d2 == 2 && d3 == 1);
		}
	}

	// Stopped: the gate is closed before the queue goes, and nothing is sent
	sys_io_gate_close(&cfg.gate);
	send_sys_io_connect_event(3, 1);
	CHECK(lv2_events_count(&queue->events) == 1);
	CHECK(!sys_io_gate_enter(&cfg.gate));

	// A send under way holds a stop back; none starts once it is closed
	sys_io_gate_open(&cfg.gate);
	CHECK(sys_io_gate_enter(&cfg.gate));
	CHECK(!retro_atomic_cas_int(&cfg.gate, 0, sys_io_gate_closed));
	sys_io_gate_leave(&cfg.gate);
	sys_io_gate_close(&cfg.gate);
	CHECK(!sys_io_gate_enter(&cfg.gate));

	// One thread sending, one stopping and starting
	{
		std::atomic<bool> stop{false};
		std::atomic<int> inside{0};
		std::atomic<u32> wrong{0}, sends{0};
		sys_io_gate_open(&cfg.gate);

		std::thread run([&]
		{
			while (!stop)
			{
				if (sys_io_gate_enter(&cfg.gate))
				{
					inside++;
					sends++;
					inside--;
					sys_io_gate_leave(&cfg.gate);
				}
			}
		});

		for (int i = 0; i < 200000; i++)
		{
			sys_io_gate_close(&cfg.gate);
			// The queue would be destroyed here
			if (inside.load())
				wrong++;
			sys_io_gate_open(&cfg.gate);
			if ((i & 0xff) == 0)
				std::this_thread::yield();
		}

		stop = true;
		run.join();
		CHECK(wrong == 0);
		CHECK(sends > 0);
		std::printf("test_sys_io_gate: 200000 stops and starts against %u sends, %u sends under way at a stop\n", sends.load(), wrong.load());
	}

	std::printf("test_sys_io_gate: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
