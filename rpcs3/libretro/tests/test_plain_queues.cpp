// The plain-data queues that were std::queue and std::deque, on
// libretro-common's retro_deque (queues/retro_deque.h):
//
// - a Skylander portal's queued statuses, through the portal's own code: a
//   figure put on it and taken off reports the statuses in the order the
//   std::queue gave them, deactivating keeps the last and drops the rest,
//   and its queue stays the size it was made;
// - a P2P stream socket's backlog in a savestate: the bytes a std::deque of
//   the same ids made, and the same ids read back, in order;
// - the patterns the others use (sys_usbd's events, the USB figures'
//   replies, RSX's two histories), each against the std:: container it
//   was, a few hundred thousand steps: same contents at every step.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/Io/Skylander.h"
#include "Emu/Io/Infinity.h"
#include "Emu/Io/Dimensions.h"
#include "Emu/Io/KamenRider.h"
#include "Emu/Cell/lv2/sys_net/lv2_socket_p2ps.h"
#include "Emu/RSX/RSXThread.h"
#include "util/serialization.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <queue>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct portal : sky_portal
{
	usz limit(u8 slot) const { return sky_statuses_limit(&skylanders[slot].queued_status); }
	usz queued(u8 slot) const { return sky_statuses_count(&skylanders[slot].queued_status); }
};

// The two status bits of @slot in a status reply
static u8 status_of(portal& p, u8 slot)
{
	u8 reply[0x20];
	p.get_status(reply);
	const u16 all = static_cast<u16>(reply[1] | (reply[2] << 8));
	return (all >> (slot * 2)) & 3;
}

static u32 s_rand = 0x2545F491;

static u32 rnd()
{
	s_rand ^= s_rand << 13;
	s_rand ^= s_rand >> 17;
	s_rand ^= s_rand << 5;
	return s_rand;
}

int main()
{
	// A Skylander on the portal and off it
	{
		portal p;
		std::array<u8, 0x40 * 0x10> figure{};
		figure[0] = 0x44;

		const u8 slot = p.load_skylander(figure, fs::file{});
		CHECK(slot == 0 && p.queued(slot) == 2);
		CHECK(status_of(p, slot) == 3);
		CHECK(status_of(p, slot) == 1);
		CHECK(status_of(p, slot) == 1 && p.queued(slot) == 0);

		CHECK(p.remove_skylander(slot));
		CHECK(status_of(p, slot) == 2);
		CHECK(status_of(p, slot) == 0);
		CHECK(status_of(p, slot) == 0);
		CHECK(!p.remove_skylander(slot));

		// Deactivated with statuses waiting: the last one is kept (its
		// "present" bit), the rest dropped
		CHECK(p.load_skylander(figure, fs::file{}) == slot && p.queued(slot) == 2);
		p.deactivate();
		CHECK(p.queued(slot) == 0);
		CHECK(status_of(p, slot) == 1);

		// Activated again: every figure present is announced again
		p.activate();
		CHECK(p.queued(slot) == 2);
		CHECK(status_of(p, slot) == 3);
		CHECK(status_of(p, slot) == 1);

		// On and off many times: the queue is the one it was made with
		for (u32 i = 0; i < 1000; i++)
		{
			CHECK(p.remove_skylander(slot));
			CHECK(status_of(p, slot) == 2 && status_of(p, slot) == 0);
			CHECK(p.load_skylander(figure, fs::file{}) == slot);
			CHECK(status_of(p, slot) == 3 && status_of(p, slot) == 1);
		}
		CHECK(p.limit(slot) == 16);

		// Never read: it grows rather than drop one, in order
		CHECK(p.remove_skylander(slot));
		for (u32 i = 0; i < 40; i++)
		{
			CHECK(p.load_skylander(figure, fs::file{}) == slot);
			CHECK(p.remove_skylander(slot));
		}
		CHECK(p.queued(slot) == 162 && p.limit(slot) >= 162);
		CHECK(status_of(p, slot) == 2 && status_of(p, slot) == 0);
		for (u32 i = 0; i < 40; i++)
			CHECK(status_of(p, slot) == 3 && status_of(p, slot) == 1 && status_of(p, slot) == 2 && status_of(p, slot) == 0);
		CHECK(p.queued(slot) == 0);
	}

	// A P2P stream socket's backlog in a savestate
	{
		p2ps_backlog_t backlog{};
		std::deque<s32> as_deque;

		for (s32 id = 0; id < 37; id++)
		{
			const s32 value = id * 7919 - 100000;
			CHECK(p2ps_backlog_push_back_growing(&backlog, &value));
			as_deque.push_back(value);
		}
		for (u32 i = 0; i < 5; i++)
		{
			p2ps_backlog_pop_front(&backlog);
			as_deque.pop_front();
		}

		utils::serial saved;
		p2ps_backlog_save(saved, backlog);
		utils::serial expected;
		expected(as_deque);
		CHECK(saved.data.size() == expected.data.size());
		CHECK(saved.data == expected.data);

		utils::serial in;
		in.set_reading_state(std::move(saved.data));
		p2ps_backlog_t back{};
		p2ps_backlog_load(in, back);
		CHECK(p2ps_backlog_count(&back) == as_deque.size());
		usz wrong = 0;
		for (usz i = 0; i < p2ps_backlog_count(&back) && i < as_deque.size(); i++)
			wrong += *p2ps_backlog_at(&back, i) != as_deque[i];
		CHECK(wrong == 0);

		// An empty one is one byte, and reads back empty
		p2ps_backlog_clear(&backlog);
		utils::serial none;
		p2ps_backlog_save(none, backlog);
		utils::serial expected_none;
		expected_none(std::deque<s32>{});
		CHECK(none.data == expected_none.data);

		p2ps_backlog_free(&backlog);
		p2ps_backlog_free(&back);
	}

	// A queue of replies (the USB figures, sys_usbd): pushed and popped in
	// bursts, against the std::queue it was
	{
		infinity_replies_t q{};
		std::queue<std::array<u8, 32>> model;
		usz wrong = 0;

		CHECK(infinity_replies_init(&q, 16));
		for (u32 step = 0; step < 200000; step++)
		{
			const u32 r = rnd();
			if ((r & 3) != 0 && model.size() < 200)
			{
				std::array<u8, 32> reply;
				for (u8& b : reply)
					b = static_cast<u8>(rnd());
				CHECK(infinity_replies_push_back_growing(&q, &reply));
				model.push(reply);
			}
			else if (!model.empty())
			{
				wrong += *infinity_replies_front(&q) != model.front();
				infinity_replies_pop_front(&q);
				model.pop();
			}
			wrong += infinity_replies_count(&q) != model.size();
			wrong += infinity_replies_empty(&q) != model.empty();
		}
		CHECK(wrong == 0);
		infinity_replies_free(&q);
	}

	// RSX's recovered-command history: the last 20, the oldest looked at
	// and dropped when there are 20
	{
		rsx::rsx_fifo_recoveries_t q{};
		std::queue<rsx::desync_fifo_cmd_info> model;
		usz wrong = 0;

		CHECK(rsx::rsx_fifo_recoveries_init(&q, 20));
		for (u32 step = 0; step < 100000; step++)
		{
			if (rsx::rsx_fifo_recoveries_count(&q) == 20u)
			{
				wrong += model.size() != 20u;
				wrong += rsx::rsx_fifo_recoveries_front(&q)->cmd != model.front().cmd;
				wrong += rsx::rsx_fifo_recoveries_front(&q)->timestamp != model.front().timestamp;
				rsx::rsx_fifo_recoveries_pop_front(&q);
				model.pop();
			}
			const rsx::desync_fifo_cmd_info info{rnd(), u64{rnd()} << 20};
			CHECK(rsx::rsx_fifo_recoveries_push_back(&q, &info));
			model.push(info);
		}
		CHECK(wrong == 0 && rsx::rsx_fifo_recoveries_limit(&q) == 20);
		rsx::rsx_fifo_recoveries_free(&q);
	}

	// RSX's frame times: the last 60, read by index and from the back
	{
		rsx::rsx_frame_times_t q{};
		std::deque<rsx::frame_time_t> model;
		usz wrong = 0;

		CHECK(rsx::rsx_frame_times_init(&q, 64));
		for (u32 step = 0; step < 100000; step++)
		{
			if ((rnd() & 1023) == 0)
			{
				rsx::rsx_frame_times_clear(&q);
				model.clear();
			}
			if (rsx::rsx_frame_times_count(&q) >= 60)
			{
				for (usz i = 0; i < model.size(); i++)
				{
					wrong += rsx::rsx_frame_times_at(&q, i)->timestamp != model[i].timestamp;
					wrong += rsx::rsx_frame_times_at(&q, i)->preempt_count != model[i].preempt_count;
				}
				wrong += rsx::rsx_frame_times_back(&q)->tsc != model.back().tsc;
				rsx::rsx_frame_times_pop_front(&q);
				model.pop_front();
			}
			const rsx::frame_time_t t{rnd() & 7, u64{step} * 16667, u64{rnd()} << 8};
			CHECK(rsx::rsx_frame_times_push_back_growing(&q, &t));
			model.push_back(t);
			wrong += rsx::rsx_frame_times_count(&q) != model.size();
		}
		CHECK(wrong == 0 && rsx::rsx_frame_times_limit(&q) == 64);
		rsx::rsx_frame_times_free(&q);
	}

	std::printf("test_plain_queues: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
