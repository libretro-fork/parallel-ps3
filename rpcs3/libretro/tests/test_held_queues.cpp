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
// - the DNS hook's made-up replies (Emu/NP/np_dnshook.cpp), through its
//   own code: each socket reads its own replies in order, a socket no
//   longer watched takes its unread replies with it.
//
// - the overlay's messages (Emu/RSX/Overlays/overlay_message.cpp), through
//   its own code: queued, shown three at a time, let go when done with,
//   and freed with the overlay - every message accounted for.
//
// sys_timer.cpp is compiled into this test (the timer thread is private to
// it); the rest is the core's own objects and archives, the same link as
// the core's with this main. Built by `make check`.

#include "../../Emu/Cell/lv2/sys_timer.cpp"
#include "Emu/Cell/Modules/cellVoice.h"
#include "Emu/NP/np_dnshook.h"
#include "Emu/RSX/Overlays/overlay_message.h"
#include "Emu/emu_callbacks.h"
#include "Emu/system_config.h"
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

	// The DNS hook's made-up replies (Emu/NP/np_dnshook.cpp), through its
	// own code: one list of replies where there was a map of queues
	{
		g_cfg.net.swap_list.from_string("*.example.org=10.1.2.3");
		np::dnshook hook;

		// A query for @host, as a game's resolver sends it
		const auto query = [](u16 id, std::string_view host)
		{
			std::vector<u8> q{static_cast<u8>(id >> 8), static_cast<u8>(id), 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
			usz start = 0;
			while (start <= host.size())
			{
				const usz dot = std::min(host.find('.', start), host.size());
				q.push_back(static_cast<u8>(dot - start));
				q.insert(q.end(), host.begin() + start, host.begin() + dot);
				start = dot + 1;
			}
			q.insert(q.end(), {0, 0, 1, 0, 1});
			return q;
		};

		// A reply read as recvfrom reads it
		const auto take = [&](u32 sock)
		{
			std::vector<u8> out(512);
			out.resize(hook.take_dns_packet(sock, out.data(), 512));
			return out;
		};

		CHECK(!hook.is_dns(5));
		hook.add_dns_spy(5);
		hook.add_dns_spy(9);
		hook.add_dns_spy(5); // twice: still one
		CHECK(hook.is_dns(5) && hook.is_dns(9) && !hook.is_dns(6));
		CHECK(!hook.is_dns_queue(5) && !hook.is_dns_queue(9));

		// Not one of ours: no reply made
		const auto other = query(1, "www.elsewhere.net");
		CHECK(hook.analyze_dns_packet(5, other.data(), ::size32(other)) == -1);
		CHECK(!hook.is_dns_queue(5));

		// Replies on two sockets, in turns: each socket reads its own, in order
		for (u16 id = 100; id < 140; id++)
		{
			const auto q = query(id, "a.example.org");
			CHECK(hook.analyze_dns_packet(id & 1 ? 5 : 9, q.data(), ::size32(q)) == static_cast<s32>(q.size()));
		}
		CHECK(hook.is_dns_queue(5) && hook.is_dns_queue(9));
		usz wrong = 0;
		for (u16 id = 101; id < 121; id += 2)
		{
			const std::vector<u8> reply = take(5);
			wrong += reply.size() < 16 || reply[0] != (id >> 8) || reply[1] != (id & 0xff);
			wrong += reply.size() < 4 || std::memcmp(reply.data() + reply.size() - 4, "\x0a\x01\x02\x03", 4) != 0;
		}
		for (u16 id = 100; id < 140; id += 2)
		{
			const std::vector<u8> reply = take(9);
			wrong += reply.size() < 16 || reply[0] != (id >> 8) || reply[1] != (id & 0xff);
		}
		CHECK(wrong == 0);
		CHECK(hook.is_dns_queue(5) && !hook.is_dns_queue(9));

		// A reply is the query with the reply flags, one answer counted,
		// and the answer after it: byte for byte what the vector held
		{
			const auto q = query(0x1234, "x.example.org");
			CHECK(hook.analyze_dns_packet(9, q.data(), ::size32(q)) > 0);
			std::vector<u8> expected = q;
			expected[2] |= 0x80; // response
			expected[3] |= 0x80; // recursion available
			expected[7] = 1;     // one answer
			expected.insert(expected.end(), {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 0x3B, 0, 4, 10, 1, 2, 3});
			CHECK(take(9) == expected);
		}

		// A socket no longer watched: its unread replies go with it, the
		// other's stay
		{
			const auto q = query(500, "b.example.org");
			CHECK(hook.analyze_dns_packet(9, q.data(), ::size32(q)) > 0);
		}
		hook.remove_dns_spy(5);
		CHECK(!hook.is_dns(5) && !hook.is_dns_queue(5) && hook.is_dns(9) && hook.is_dns_queue(9));
		const std::vector<u8> last = take(9);
		CHECK(last.size() > 2 && last[0] == (500 >> 8) && last[1] == (500 & 0xff));

		// Answered without having been watched: watched from then on, as the map made it
		{
			const auto q = query(7, "c.example.org");
			CHECK(!hook.is_dns(33));
			CHECK(hook.analyze_dns_packet(33, q.data(), ::size32(q)) > 0);
			CHECK(hook.is_dns(33) && hook.is_dns_queue(33));
		}
		// (left unread: the hook's destructor frees it)
	}

	// The overlay's messages (Emu/RSX/Overlays/overlay_message.cpp),
	// through its own code. Each message holds the counter it was queued
	// with, so the counter's use count says whether the message is alive.
	{
		// No font to find here: the text is measured with none
		g_emu_callbacks.get_font_dirs = []() -> std::vector<std::string> { return {}; };

		typedef std::shared_ptr<atomic_t<u32>> counter;
		std::vector<counter> refs;
		for (u32 i = 0; i < 6; i++)
			refs.emplace_back(std::make_shared<atomic_t<u32>>(1));

		{
			rsx::overlays::message overlay;

			for (u32 i = 0; i < 5; i++)
				overlay.queue_message(fmt::format("message %u", i), umax, refs[i]);
			for (u32 i = 0; i < 5; i++)
				CHECK(refs[i].use_count() == 2);

			// The same text again: not queued twice
			overlay.queue_message(std::string("message 2"), umax, refs[5]);
			CHECK(refs[5].use_count() == 1);

			// Three go on screen, in the order queued; the other two wait
			overlay.update(0);
			for (u32 i = 0; i < 5; i++)
				CHECK(refs[i].use_count() == 2);

			// One on screen is done with (its counter at 0): it goes, the
			// rest stay, and its text can be queued again
			*refs[1] = 0;
			overlay.update(0);
			CHECK(refs[1].use_count() == 1);
			CHECK(refs[0].use_count() == 2 && refs[2].use_count() == 2 && refs[3].use_count() == 2 && refs[4].use_count() == 2);
			*refs[5] = 1;
			overlay.queue_message(std::string("message 1"), umax, refs[5]);
			CHECK(refs[5].use_count() == 2);

			// All done with: each update lets the visible ones go and
			// brings the waiting ones on, until none is left
			for (auto& ref : refs)
				*ref = 0;
			for (u32 i = 0; i < 4; i++)
				overlay.update(0);
			usz alive = 0;
			for (auto& ref : refs)
				alive += ref.use_count() != 1;
			CHECK(alive == 0);

			// Left queued and on screen when the overlay goes: freed with it
			for (u32 i = 0; i < 5; i++)
			{
				*refs[i] = 1;
				overlay.queue_message(fmt::format("last %u", i), umax, refs[i]);
			}
			overlay.update(0);
			for (u32 i = 0; i < 5; i++)
				CHECK(refs[i].use_count() == 2);
		}

		usz alive = 0;
		for (auto& ref : refs)
			alive += ref.use_count() != 1;
		CHECK(alive == 0);
	}

	std::printf("test_held_queues: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
