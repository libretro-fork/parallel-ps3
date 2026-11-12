// An lv2 event queue's events (Emu/Cell/lv2/sys_event.h): a retro_deque
// made with the queue, where there was a std::deque that took a block from
// the heap for every sixteen events sent and gave one back for every
// sixteen taken.
//
// - events come out in the order they went in, round the ring many times;
// - the queue holds as many as its size and refuses the next (EBUSY);
// - sending and taking allocate nothing: 100000 events through a queue;
// - a queue drained is empty, and takes events again;
// - a savestate has the queue as it had it with the deque, byte for byte,
//   and a queue read back from one has the same events.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/lv2/sys_event.h"
#include "util/serialization.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <new>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

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

static lv2_event event_of(u64 n)
{
	return std::make_tuple(n, n * 3 + 1, ~n, n << 20);
}

// As sys_event_queue_receive takes one
static bool take(lv2_event_queue& queue, lv2_event& out)
{
	std::lock_guard lock(queue.mutex);
	if (lv2_events_empty(&queue.events))
		return false;
	out = *lv2_events_front(&queue.events);
	lv2_events_pop_front(&queue.events);
	return true;
}

int main()
{
	g_fxo->reset();
	g_fxo->init<id_manager::id_map<lv2_obj>>();

	const u32 id = idm::make<lv2_obj, lv2_event_queue>(SYS_SYNC_PRIORITY, SYS_PPU_QUEUE, 127, 0x1234, 0);
	const auto queue = idm::get_unlocked<lv2_obj, lv2_event_queue>(id);
	CHECK(!!queue);
	queue->exists++; // as sys_event_queue_create leaves it

	// As many as its size, and no more
	for (u64 n = 0; n < 127; n++)
		CHECK(queue->send(event_of(n)) == CellError{});
	CHECK(lv2_events_count(&queue->events) == 127);
	CHECK(queue->send(event_of(999)) == CELL_EBUSY);
	CHECK(lv2_events_count(&queue->events) == 127);

	// In the order they went in
	lv2_event got;
	for (u64 n = 0; n < 127; n++)
	{
		CHECK(take(*queue, got) && got == event_of(n));
	}
	CHECK(!take(*queue, got));

	// 100000 events through it, a few waiting at a time, round the ring:
	// in order, and nothing allocated
	{
		u64 sent = 0, taken = 0, wrong = 0;
		t_allocations = 0;
		t_counting = true;
		while (taken < 100000)
		{
			const u64 burst = 1 + sent % 37;
			for (u64 i = 0; i < burst && lv2_events_count(&queue->events) < 127; i++)
				if (queue->send(event_of(sent)) == CellError{})
					sent++;
			const u64 back = 1 + taken % 29;
			for (u64 i = 0; i < back && take(*queue, got); i++)
			{
				wrong += got != event_of(taken);
				taken++;
			}
		}
		t_counting = false;
		CHECK(wrong == 0);
		CHECK(t_allocations == 0);
		std::printf("test_lv2_events: %llu events through a queue, %llu out of order, %llu allocations\n",
			static_cast<unsigned long long>(taken), static_cast<unsigned long long>(wrong), static_cast<unsigned long long>(t_allocations));

		while (take(*queue, got))
			taken++;
		CHECK(taken == sent);
	}

	// Drained (sys_event_queue_drain), then used again
	for (u64 n = 0; n < 50; n++)
		CHECK(queue->send(event_of(n)) == CellError{});
	{
		std::lock_guard lock(queue->mutex);
		lv2_events_clear(&queue->events);
	}
	CHECK(lv2_events_empty(&queue->events) && !take(*queue, got));
	for (u64 n = 500; n < 540; n++)
		CHECK(queue->send(event_of(n)) == CellError{});

	// A savestate: the bytes a std::deque of the same events made
	{
		utils::serial saved;
		queue->save(saved);

		std::deque<lv2_event> as_deque;
		for (u64 n = 500; n < 540; n++)
			as_deque.emplace_back(event_of(n));
		utils::serial expected;
		expected(queue->protocol, queue->type, queue->size, queue->name, queue->key, as_deque);

		CHECK(saved.data.size() == expected.data.size());
		CHECK(saved.data == expected.data);

		// And read back: a queue made from the savestate has the same events
		utils::serial in;
		in.set_reading_state(std::move(saved.data));
		lv2_event_queue back{stx::exact_t<utils::serial&>(in)};
		CHECK(back.protocol == queue->protocol && back.type == queue->type && back.size == queue->size);
		CHECK(back.name == queue->name && back.key == queue->key);
		CHECK(lv2_events_count(&back.events) == 40);
		u64 wrong = 0;
		for (u64 n = 0; n < lv2_events_count(&back.events); n++)
			wrong += *lv2_events_at(&back.events, n) != event_of(500 + n);
		CHECK(wrong == 0);
	}

	// A queue of one event: it holds one, not the two its block would
	{
		const u32 small_id = idm::make<lv2_obj, lv2_event_queue>(SYS_SYNC_PRIORITY, SYS_PPU_QUEUE, 1, 0, 0);
		const auto small = idm::get_unlocked<lv2_obj, lv2_event_queue>(small_id);
		small->exists++;
		CHECK(small->send(event_of(7)) == CellError{});
		CHECK(small->send(event_of(8)) == CELL_EBUSY);
		CHECK(take(*small, got) && got == event_of(7));
		CHECK(small->send(event_of(8)) == CellError{});
		CHECK(take(*small, got) && got == event_of(8) && !take(*small, got));
	}

	std::printf("test_lv2_events: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	// The emulator's object map wants an emulator's shutdown; there is none here
	std::_Exit(failures ? 1 : 0);
}
