// cellRec's queues (rpcs3/util/video_sink.h): frames and audio handed from
// one thread to another through slots and two retro_spsc rings, no lock.
//
// - a producer and a consumer on their own threads: every frame and every
//   block arrives once, in order, with what was put in it (built a second
//   time under ThreadSanitizer, which finds a race the handover lets
//   through);
// - frame buffers go round: once each slot has had one, the producer only
//   ever gets back buffers it gave, so a recording allocates none;
// - a full set of slots drops the frame, or waits for the consumer when
//   asked to, until has_error;
// - a clear drops what was handed over before it, a block held by the
//   consumer included, and the slots come back;
// - audio is cut into blocks of 256, each stamped with its own time.

#include "util/video_sink.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>
#include <vector>

// ensure()'s failure, which the core gets from Utilities/StrFmt.cpp
namespace fmt
{
	[[noreturn]] void raw_verify_error(std::source_location loc, const char8_t* msg, usz)
	{
		std::fprintf(stderr, "verification failed at %s:%u: %s\n", loc.file_name(), loc.line(), reinterpret_cast<const char*>(msg));
		std::abort();
	}

	[[noreturn]] void raw_verify_error(std::source_location loc, std::source_location, const char8_t* msg, usz object)
	{
		raw_verify_error(loc, msg, object);
	}
}

static int fails;

#define CHECK(c) \
	do \
	{ \
		if (!(c)) \
		{ \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
			fails++; \
		} \
	} while (0)

struct sink : utils::video_sink
{
	void stop(bool) override {}
	void pause(bool) override {}
	void resume() override {}
	void clear() { clear_queues(); }
};

static constexpr u32 frame_bytes = 64;

static std::vector<u8> make_frame(u32 n)
{
	std::vector<u8> v(frame_bytes);
	for (u32 i = 0; i < frame_bytes; i++)
		v[i] = static_cast<u8>(n * 7 + i);
	return v;
}

static bool frame_is(const utils::video_sink::encoder_frame& f, u32 n)
{
	if (f.data.size() != frame_bytes || f.timestamp_ms != n || f.width != n + 1 || f.height != n + 2 || f.pitch != frame_bytes)
		return false;
	for (u32 i = 0; i < frame_bytes; i++)
		if (f.data[i] != static_cast<u8>(n * 7 + i))
			return false;
	return true;
}

// Every frame and block once, in order, intact, across two threads; the
// producer's buffers only ever the ones it gave once the slots are full
static void threads()
{
	static constexpr u32 frames = 20000;
	static constexpr u32 blocks = 20000;
	sink s;

	std::thread producer([&]
	{
		std::vector<u8> buf;
		std::set<const u8*> given;
		u32 fresh = 0;

		for (u32 n = 0; n < frames; n++)
		{
			std::vector<u8> v = make_frame(n);
			buf.swap(v);
			if (buf.capacity() && given.insert(buf.data()).second)
				fresh++;
			while (!s.add_frame(buf, frame_bytes, n + 1, n + 2, 0, n))
				std::this_thread::yield();
			// What came back is a buffer the producer gave before, or empty
			if (!buf.empty())
				CHECK(given.count(buf.data()));
		}

		for (u32 n = 0; n < blocks; n++)
		{
			f32 pcm[2 * 3];
			for (u32 i = 0; i < 6; i++)
				pcm[i] = static_cast<f32>(n) + i;
			while (!s.add_audio_samples(reinterpret_cast<const u8*>(pcm), 3, 2, n * 100))
				std::this_thread::yield();
		}
		(void)fresh;
	});

	utils::video_sink::encoder_frame f;
	u32 got = 0;
	while (got < frames)
	{
		if (!s.pop_frame(f))
		{
			std::this_thread::yield();
			continue;
		}
		if (!frame_is(f, got))
		{
			std::printf("FAIL frame %u arrived wrong\n", got);
			fails++;
			break;
		}
		got++;
	}

	u32 got_blocks = 0;
	while (got_blocks < blocks)
	{
		const utils::video_sink::encoder_sample* b = s.peek_sample();
		if (!b)
		{
			std::this_thread::yield();
			continue;
		}
		bool ok = b->sample_count == 3 && b->channels == 2 && b->timestamp_us == got_blocks * 100ull;
		for (u32 i = 0; ok && i < 6; i++)
			ok = b->data[i] == static_cast<f32>(got_blocks) + i;
		s.release_sample();
		if (!ok)
		{
			std::printf("FAIL block %u arrived wrong\n", got_blocks);
			fails++;
			break;
		}
		got_blocks++;
	}

	producer.join();
	CHECK(!s.pop_frame(f));
	CHECK(!s.peek_sample());
}

// After every slot has had a buffer, frames go round without one made
static void recycling()
{
	sink s;
	std::set<const u8*> made;
	utils::video_sink::encoder_frame f;
	std::vector<u8> buf;

	for (u32 n = 0; n < 1000; n++)
	{
		if (buf.empty())
		{
			buf = make_frame(n);
			made.insert(buf.data());
		}
		else
		{
			const u8* p = buf.data();
			std::vector<u8> v = make_frame(n);
			std::memcpy(buf.data(), v.data(), frame_bytes);
			CHECK(buf.data() == p);
		}
		const u8* given = buf.data();
		CHECK(s.add_frame(buf, frame_bytes, n + 1, n + 2, 0, n));
		// The buffer went into the slot, not a copy of it
		CHECK(buf.empty() || buf.data() != given);
		// The consumer keeps its buffer in f and swaps it back in each time
		if (n % 3 == 2)
			while (s.pop_frame(f))
				CHECK(made.count(f.data.data()));
	}

	// The slots and the consumer hold a buffer each at most
	CHECK(made.size() <= utils::video_sink::frame_slots + 2);
}

// A full set of slots drops, or waits until the consumer frees one
static void full()
{
	sink s;
	std::vector<u8> buf;
	utils::video_sink::encoder_frame f;

	for (u32 n = 0; n < utils::video_sink::frame_slots; n++)
	{
		buf = make_frame(n);
		CHECK(s.add_frame(buf, frame_bytes, n + 1, n + 2, 0, n));
	}
	buf = make_frame(99);
	CHECK(!s.add_frame(buf, frame_bytes, 100, 101, 0, 99));

	// Waiting: the consumer frees one later and the frame goes in
	std::thread consumer([&]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		utils::video_sink::encoder_frame g;
		CHECK(s.pop_frame(g));
		CHECK(frame_is(g, 0));
	});
	buf = make_frame(32);
	CHECK(s.add_frame(buf, frame_bytes, 33, 34, 0, 32, true));
	consumer.join();

	for (u32 n = 1; n <= 32; n++)
	{
		CHECK(s.pop_frame(f));
		CHECK(frame_is(f, n));
	}
	CHECK(!s.pop_frame(f));

	// Waiting ends with an error
	for (u32 n = 0; n < utils::video_sink::frame_slots; n++)
	{
		buf = make_frame(n);
		CHECK(s.add_frame(buf, frame_bytes, n + 1, n + 2, 0, n));
	}
	std::thread failer([&]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		s.has_error = true;
	});
	buf = make_frame(0);
	CHECK(!s.add_frame(buf, frame_bytes, 1, 2, 0, 0, true));
	failer.join();

	// Audio the same
	sink a;
	f32 pcm[2] = {};
	for (u32 n = 0; n < utils::video_sink::sample_slots; n++)
		CHECK(a.add_audio_samples(reinterpret_cast<const u8*>(pcm), 1, 2, n));
	CHECK(!a.add_audio_samples(reinterpret_cast<const u8*>(pcm), 1, 2, 0));
}

// A clear drops what was handed over, the consumer's held block too
static void clear()
{
	sink s;
	std::vector<u8> buf;
	utils::video_sink::encoder_frame f;
	f32 pcm[2] = {1, 2};

	for (u32 n = 0; n < utils::video_sink::frame_slots; n++)
	{
		buf = make_frame(n);
		CHECK(s.add_frame(buf, frame_bytes, n + 1, n + 2, 0, n));
	}
	for (u32 n = 0; n < utils::video_sink::sample_slots; n++)
		CHECK(s.add_audio_samples(reinterpret_cast<const u8*>(pcm), 1, 2, n));
	CHECK(s.peek_sample() != nullptr);

	s.clear();
	CHECK(!s.pop_frame(f));
	CHECK(s.peek_sample() == nullptr);

	// Every slot is free again
	for (u32 n = 0; n < utils::video_sink::frame_slots; n++)
	{
		buf = make_frame(n + 500);
		CHECK(s.add_frame(buf, frame_bytes, n + 501, n + 502, 0, n + 500));
	}
	for (u32 n = 0; n < utils::video_sink::sample_slots; n++)
		CHECK(s.add_audio_samples(reinterpret_cast<const u8*>(pcm), 1, 2, n + 500));
	for (u32 n = 0; n < utils::video_sink::frame_slots; n++)
	{
		CHECK(s.pop_frame(f));
		CHECK(frame_is(f, n + 500));
	}
	for (u32 n = 0; n < utils::video_sink::sample_slots; n++)
	{
		const utils::video_sink::encoder_sample* b = s.peek_sample();
		CHECK(b && b->timestamp_us == n + 500);
		s.release_sample();
	}
}

// Audio in blocks of 256, each with its own time
static void blocks()
{
	sink s;
	std::vector<f32> pcm(600 * 2);
	for (usz i = 0; i < pcm.size(); i++)
		pcm[i] = static_cast<f32>(i);

	CHECK(s.add_audio_samples(reinterpret_cast<const u8*>(pcm.data()), 600, 2, 1000000));

	const u32 counts[3] = {256, 256, 88};
	u32 done = 0;
	for (u32 k = 0; k < 3; k++)
	{
		const utils::video_sink::encoder_sample* b = s.peek_sample();
		CHECK(b != nullptr);
		if (!b)
			return;
		CHECK(b->sample_count == counts[k]);
		CHECK(b->channels == 2);
		CHECK(b->timestamp_us == 1000000 + done * 1000000ull / 48000);
		CHECK(std::memcmp(b->data.data(), &pcm[done * 2], counts[k] * 2 * sizeof(f32)) == 0);
		// Peeking again gives the same block until it is released
		CHECK(s.peek_sample() == b);
		s.release_sample();
		done += counts[k];
	}
	CHECK(s.peek_sample() == nullptr);

	// More channels than a slot holds
	CHECK(!s.add_audio_samples(reinterpret_cast<const u8*>(pcm.data()), 1, 9, 0));
}

int main()
{
	threads();
	recycling();
	full();
	clear();
	blocks();

	std::printf("video_sink: %s\n", fails ? "FAIL" : "all ok");
	return fails ? 1 : 0;
}
