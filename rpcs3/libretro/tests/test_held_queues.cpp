// Queues whose elements were not plain data, on libretro-common's
// retro_deque (queues/retro_deque.h):
//
// - the timer thread's list of timers (Emu/Cell/lv2/sys_timer.cpp), each a
//   reference held as plain bytes: a timer added is held once and found,
//   one taken out of the middle is let go and the rest keep their order,
//   against the std::deque of shared_ptr it was; the list is made for every
//   timer there can be and stays that size; ending the thread lets go of
//   all it still holds;
// - cellVoice's queues to notify (Emu/Cell/Modules/cellVoice.h), one list
//   where there was a map of lists: in a savestate the bytes the map made,
//   read back by the map's own code, and the map's bytes read back here.
//
// sys_timer.cpp is compiled into this test (the timer thread is private to
// it); the rest is the core's own objects and archives, the same link as
// the core's with this main. Built by `make check`.

#include "../../Emu/Cell/lv2/sys_timer.cpp"
#include "Emu/Cell/Modules/cellVoice.h"
#include "util/serialization.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <unordered_map>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u32 s_rand = 0x9E3779B9;

static u32 rnd()
{
	s_rand ^= s_rand << 13;
	s_rand ^= s_rand >> 17;
	s_rand ^= s_rand << 5;
	return s_rand;
}

static bool same(const lv2_timer_thread& thread, const std::deque<shared_ptr<lv2_timer>>& model)
{
	if (lv2_timers_count(&thread.timers) != model.size())
		return false;
	for (usz i = 0; i < model.size(); i++)
		if (timer_of(lv2_timers_at(&thread.timers, i)).get() != model[i].get())
			return false;
	return true;
}

int main()
{
	g_fxo->reset();
	g_fxo->init<id_manager::id_map<lv2_obj>>();

	// The timer thread's list
	{
		std::vector<shared_ptr<lv2_timer>> made;
		for (u32 i = 0; i < 64; i++)
		{
			made.emplace_back(idm::make_ptr<lv2_obj, lv2_timer>());
			CHECK(!!made.back());
		}
		const usz held_before = made[0].use_count();

		{
			lv2_timer_thread thread;
			std::deque<shared_ptr<lv2_timer>> model;
			CHECK(lv2_timers_limit(&thread.timers) == lv2_timer::id_count);

			// Added: held once, found where it went
			thread.add(shared_ptr<lv2_timer>(made[0]));
			model.emplace_back(made[0]);
			CHECK(made[0].use_count() == held_before + 2); // the list's and the model's
			CHECK(thread.find(made[0].get()) == 0 && thread.find(made[1].get()) == 1);

			// Taken out again: let go
			timer_drop(lv2_timers_at(&thread.timers, 0));
			lv2_timers_erase(&thread.timers, 0);
			model.clear();
			CHECK(made[0].use_count() == held_before);
			CHECK(thread.find(made[0].get()) == 0 && lv2_timers_empty(&thread.timers));

			// Created and destroyed in any order, as the syscalls do it
			for (u32 step = 0; step < 200000; step++)
			{
				const shared_ptr<lv2_timer>& timer = made[rnd() % made.size()];
				const usz at = thread.find(timer.get());
				const auto it = std::find(model.begin(), model.end(), timer);

				CHECK((at == lv2_timers_count(&thread.timers)) == (it == model.end()));

				if (it == model.end())
				{
					thread.add(shared_ptr<lv2_timer>(timer));
					model.emplace_back(timer);
				}
				else
				{
					CHECK(at == static_cast<usz>(it - model.begin()));
					timer_drop(lv2_timers_at(&thread.timers, at));
					lv2_timers_erase(&thread.timers, at);
					model.erase(it);
				}

				if (!same(thread, model))
				{
					CHECK(!"the list and the std::deque differ");
					break;
				}
			}

			// Each is held once for being in the list, and not otherwise
			usz wrong = 0;
			for (const auto& timer : made)
			{
				const bool in = thread.find(timer.get()) != lv2_timers_count(&thread.timers);
				wrong += timer.use_count() != held_before + (in ? 2 : 0);
			}
			CHECK(wrong == 0);
			CHECK(lv2_timers_limit(&thread.timers) == lv2_timer::id_count);

			model.clear();
			CHECK(!lv2_timers_empty(&thread.timers));
		}

		// The thread ended: everything it held is let go
		usz wrong = 0;
		for (const auto& timer : made)
			wrong += timer.use_count() != held_before;
		CHECK(wrong == 0);
	}

	// cellVoice's queues to notify, in a savestate
	{
		typedef std::unordered_map<u64, std::deque<u64>> as_map;

		// One key: the map's bytes exactly
		{
			voice_manager manager;
			as_map map;
			for (u64 n = 0; n < 9; n++)
			{
				const voice_notify notify{0x1122334455667788ull, n * 1000003 + 5};
				CHECK(voice_notifies_push_back_growing(&manager.queue_keys, &notify));
				map[notify.key].push_back(notify.source);
			}
			utils::serial saved, expected;
			manager.save_queue_keys(saved);
			expected(map);
			CHECK(saved.data == expected.data);

			// None: one byte
			voice_manager none;
			utils::serial saved_none, expected_none;
			none.save_queue_keys(saved_none);
			expected_none(as_map{});
			CHECK(saved_none.data == expected_none.data);
		}

		// Several keys, set in turns: the map's own code reads what was
		// written here, and what the map wrote is read here
		{
			voice_manager manager;
			as_map map;
			for (u64 n = 0; n < 200; n++)
			{
				const voice_notify notify{0x5000 + rnd() % 7, u64{rnd()} << 16 | n};
				CHECK(voice_notifies_push_back_growing(&manager.queue_keys, &notify));
				map[notify.key].push_back(notify.source);
			}

			utils::serial saved;
			manager.save_queue_keys(saved);
			utils::serial in;
			in.set_reading_state(std::move(saved.data));
			as_map read;
			in(read);
			CHECK(read == map);

			utils::serial from_map;
			from_map(map);
			utils::serial in_map;
			in_map.set_reading_state(std::move(from_map.data));
			voice_manager back;
			back.load_queue_keys(in_map);
			as_map again;
			for (usz i = 0; i < voice_notifies_count(&back.queue_keys); i++)
				again[voice_notifies_at(&back.queue_keys, i)->key].push_back(voice_notifies_at(&back.queue_keys, i)->source);
			CHECK(again == map);

			// A key's queue unset (cellVoiceRemoveNotifyEventQueue): its
			// sources go, the others stay in order
			const u64 key = 0x5003;
			const usz had = map.count(key) ? map[key].size() : 0;
			CHECK(manager.remove_queue_key(key) == had && had != 0);
			CHECK(manager.remove_queue_key(key) == 0);
			map.erase(key);
			as_map left;
			for (usz i = 0; i < voice_notifies_count(&manager.queue_keys); i++)
				left[voice_notifies_at(&manager.queue_keys, i)->key].push_back(voice_notifies_at(&manager.queue_keys, i)->source);
			CHECK(left == map);
		}
	}

	std::printf("test_held_queues: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
