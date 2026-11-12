// The plain-data queues that were std::queue and std::deque, on
// libretro-common's retro_deque (queues/retro_deque.h):
//
// - a Skylander portal's queued statuses, through the portal's own code: a
//   figure put on it and taken off reports the statuses in the order the
//   std::queue gave them, deactivating keeps the last and drops the rest,
//   and its queue stays the size it was made;
// - a P2P stream socket's backlog in a savestate: the bytes a std::deque of
//   the same ids made, and the same ids read back, in order;
// - a P2P datagram socket's received packets, against the std::queue of
//   address and vector they were, and in a savestate - which leaves them
//   queued;
// - RSX's queued flips, against the std::deque in the flip info, and the
//   host GPU write jobs through RSXDMAWriter's own code;
// - the SPU cache read back, in the order the std::deque gave it;
// - cellMic's sample ring against a model, and eight microphones nobody
//   starts taking no buffers;
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
#include "Emu/RSX/Host/RSXDMAWriter.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Cell/SPUAnalyser.h"
#include "Utilities/File.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/Modules/cellMic.h"
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

	// A P2P datagram socket's received packets: each a block of its own,
	// against the std::queue of address and vector it was
	{
		typedef std::pair<sys_net_sockaddr_in_p2p, std::vector<u8>> as_pair;
		p2p_packets_t q{};
		std::deque<as_pair> model;
		usz wrong = 0;

		for (u32 step = 0; step < 50000; step++)
		{
			if ((rnd() & 3) != 0 && model.size() < 100)
			{
				sys_net_sockaddr_in_p2p addr{};
				addr.sin_len = 16;
				addr.sin_family = 2;
				addr.sin_port = static_cast<u16>(rnd());
				addr.sin_addr = rnd();
				addr.sin_vport = static_cast<u16>(rnd());
				std::vector<u8> bytes(rnd() % 5 == 0 ? 0 : rnd() % 1400);
				for (u8& b : bytes)
					b = static_cast<u8>(rnd());
				p2p_packets_add(q, addr, bytes.data(), ::size32(bytes));
				model.emplace_back(addr, std::move(bytes));
			}
			else if (!model.empty())
			{
				const p2p_packet& packet = *p2p_packets_front(&q);
				wrong += packet.size != model.front().second.size();
				wrong += std::memcmp(&packet.addr, &model.front().first, sizeof(packet.addr)) != 0;
				wrong += packet.size == model.front().second.size() && packet.size && std::memcmp(packet.bytes, model.front().second.data(), packet.size) != 0;
				p2p_packets_drop_front(q);
				model.pop_front();
			}
			wrong += p2p_packets_count(&q) != model.size();
		}
		CHECK(wrong == 0);

		// A savestate with packets waiting: the bytes the std::deque made,
		// and the packets are still queued afterwards
		while (model.size() < 12)
		{
			sys_net_sockaddr_in_p2p addr{};
			addr.sin_vport = static_cast<u16>(model.size());
			std::vector<u8> bytes(model.size() == 5 ? 0 : 3 + model.size() * 97);
			for (u8& b : bytes)
				b = static_cast<u8>(rnd());
			p2p_packets_add(q, addr, bytes.data(), ::size32(bytes));
			model.emplace_back(addr, std::move(bytes));
		}
		const usz queued = p2p_packets_count(&q);
		utils::serial saved, expected;
		p2p_packets_save(saved, q);
		expected(model);
		CHECK(saved.data.size() == expected.data.size());
		CHECK(saved.data == expected.data);
		CHECK(p2p_packets_count(&q) == queued && queued >= 12);

		// Read back: the same packets, in order
		utils::serial in;
		in.set_reading_state(std::move(saved.data));
		p2p_packets_t back{};
		p2p_packets_load(in, back);
		CHECK(p2p_packets_count(&back) == model.size());
		wrong = 0;
		for (usz i = 0; i < p2p_packets_count(&back) && i < model.size(); i++)
		{
			const p2p_packet& packet = *p2p_packets_at(&back, i);
			wrong += packet.size != model[i].second.size() || std::memcmp(&packet.addr, &model[i].first, sizeof(packet.addr)) != 0;
			wrong += packet.size == model[i].second.size() && packet.size && std::memcmp(packet.bytes, model[i].second.data(), packet.size) != 0;
		}
		CHECK(wrong == 0);

		p2p_packets_end(q);
		p2p_packets_end(back);
		CHECK(p2p_packets_empty(&q) && p2p_packets_limit(&q) == 0);
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

	// RSX's queued flips: taking a buffer's flip drops the ones queued
	// before it, as the std::deque in the flip info did
	{
		rsx::flip_buffers_t q{};
		rsx::display_flip_info_t info{};
		std::deque<u32> model;
		usz wrong = 0;

		CHECK(rsx::flip_buffers_init(&q, 8));
		CHECK(!rsx::flip_buffers_take(q, info, 3));

		for (u32 step = 0; step < 100000; step++)
		{
			const u32 buffer = rnd() & 7;

			if (rnd() & 1)
			{
				rsx::flip_buffers_queue(q, buffer);
				model.push_back(buffer);
			}
			else
			{
				bool expected = false;
				while (!model.empty())
				{
					const u32 index = model.front();
					model.pop_front();
					if (index == buffer)
					{
						expected = true;
						break;
					}
				}
				info.buffer = 99;
				const bool got = rsx::flip_buffers_take(q, info, buffer);
				wrong += got != expected || info.buffer != (expected ? buffer : 99u);
			}
			wrong += rsx::flip_buffers_count(&q) != model.size();
		}
		CHECK(wrong == 0);
		rsx::flip_buffers_free(&q);
	}

	// The host GPU write jobs (RSXDMAWriter), through its own code: done in
	// the order queued, stopping at one its handler turns down and taking
	// it up again later; dropped when there is no handler
	{
		rsx::host_gpu_context_t ctx{};
		rsx::RSXDMAWriter writer(&ctx);
		std::vector<usz> done;
		usz refuse_at = 5;
		int marks[400];

		writer.register_handler({1, [&](const volatile rsx::host_gpu_context_t*, const rsx::host_gpu_write_op_t* job)
		{
			const usz n = static_cast<usz>(static_cast<int*>(job->userdata) - marks);
			if (n == refuse_at)
				return false;
			done.push_back(n);
			return true;
		}});

		for (usz n = 0; n < 400; n++) // more than it was made for: it grows, in order
			writer.enqueue({n % 7 == 3 ? 2 : 1, &marks[n]}); // class 2 has no handler: skipped

		writer.update();
		CHECK(!done.empty() && done.back() == 4);
		writer.update();
		CHECK(done.back() == 4); // still turned down: nothing passes it
		refuse_at = umax;
		writer.update();
		usz expected = 0, wrong = 0;
		for (usz n = 0; n < 400; n++)
		{
			if (n % 7 == 3)
				continue;
			wrong += expected >= done.size() || done[expected] != n;
			expected++;
		}
		CHECK(wrong == 0 && done.size() == expected);

		// No handler at all: the jobs are dropped
		writer.deregister_handler(1);
		writer.enqueue({1, &marks[0]});
		writer.update();
		writer.register_handler({1, [&](const volatile rsx::host_gpu_context_t*, const rsx::host_gpu_write_op_t*) { done.clear(); return true; }});
		writer.update();
		CHECK(done.size() == expected);
	}

	// The SPU cache read back (spu_cache::get): a vector where it was a
	// std::deque filled from the front - the last program written first
	{
		const std::string path = fs::get_temp_dir() + "test_plain_queues_spu_cache.dat";
		fs::remove_file(path);
		{
			spu_cache cache(path);
			CHECK(!!cache);
			for (u32 n = 0; n < 50; n++)
			{
				spu_program program;
				program.entry_point = 0x100 + n * 0x40;
				program.lower_bound = program.entry_point;
				program.data.assign(4 + n % 5, 0x40802000 + n); // any words but zero
				cache.add(program);
			}
		}
		{
			spu_cache cache(path);
			const std::vector<spu_program> programs = cache.get();
			CHECK(programs.size() == 50);
			usz wrong = 0;
			for (u32 i = 0; i < programs.size(); i++)
			{
				const u32 n = 49 - i;
				wrong += programs[i].entry_point != 0x100 + n * 0x40 || programs[i].data.size() != 4 + n % 5 || programs[i].data[0] != 0x40802000 + n;
			}
			CHECK(wrong == 0);
		}
		fs::remove_file(path);
	}

	// cellMic's sample ring (mic_ringbuf): it reads what was written, in
	// order; a write that does not fit drops the oldest; and it has no
	// memory until it is made - a microphone nobody starts takes none
	{
		mic_ringbuf<64> ring;
		std::deque<u8> model;
		usz wrong = 0;
		u8 block[64], got[64];

		CHECK(!ring.is_made() && !ring.has_data());
		CHECK(ring.read_bytes(got, 16) == 0);
		ring.make();
		CHECK(ring.is_made() && !ring.has_data());

		for (u32 step = 0; step < 200000; step++)
		{
			const u32 n = rnd() % 65;

			if (rnd() & 1)
			{
				for (u32 i = 0; i < n; i++)
					block[i] = static_cast<u8>(rnd());
				ring.write_bytes(block, n);
				for (u32 i = 0; i < n; i++)
					model.push_back(block[i]);
				while (model.size() > 64)
					model.pop_front();
			}
			else
			{
				const u32 read = ring.read_bytes(got, n);
				wrong += read != std::min<usz>(n, model.size());
				for (u32 i = 0; i < read && !model.empty(); i++)
				{
					wrong += got[i] != model.front();
					model.pop_front();
				}
			}
			wrong += ring.has_data() != !model.empty();
		}
		CHECK(wrong == 0);

		// Eight microphones as cellMic keeps them, made and made again as
		// cellMicInit does: no sample buffers
		std::array<microphone_device, CELL_MAX_MICS> mics{};
		mics = {};
		mics[0] = microphone_device(microphone_handler::singstar);
		usz with_buffers = 0;
		for (const microphone_device& mic : mics)
			with_buffers += mic.has_buffers();
		CHECK(with_buffers == 0);

		// Started: it has them, before any sample comes
		CHECK(mics[0].start_microphone() == CELL_OK);
		CHECK(mics[0].has_buffers() && !mics[1].has_buffers());
		CHECK(!mics[0].has_data());
	}

	std::printf("test_plain_queues: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
