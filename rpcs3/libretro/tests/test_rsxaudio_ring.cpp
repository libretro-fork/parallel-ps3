// sys_rsxaudio's audio on its way to the backend (Emu/Cell/lv2/
// sys_rsxaudio.cpp, the path a game takes with its own libaudio): the
// lock-free ring cellAudio uses (libretro_audio_ring) where there was a ring
// that copied under a 16-byte compare-and-swap, and the backend's callback
// converting straight out of the ring when it has nothing else to do to the
// samples.
//
// - what the callback hands the backend is what it handed it before: the
//   samples in order, soft-clipped, for float and for 16-bit output, with
//   the volume applied when it is not at rest (the copying path) - checked
//   against the same conversion done on the samples that went in;
// - round the ring's wrap many times, nothing lost or repeated;
// - asked for more than is waiting, it gives what there is;
// - a flush from the writer's side (the backend starting) drops what was
//   waiting and nothing after it;
// - with RSXAUDIO_BENCH=1 it is timed: a callback's worth in
//   and out, against the ring it replaced.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/Cell/lv2/sys_rsxaudio.h"
#include "Emu/Audio/AudioBackend.h"
#include "Utilities/simple_ringbuf.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The n-th sample of the stream: mostly in range, now and then loud enough to be clipped
static f32 sample_of(u64 n)
{
	const f32 v = static_cast<f32>(static_cast<s32>((n * 2654435761u) & 0xffff) - 32768) / 32768.0f;
	return n % 37 == 0 ? v * 1.6f : v * 0.8f;
}

struct rsxaudio_backend_test
{
	typedef rsxaudio_backend_thread::callback_config config;

	static void setup(rsxaudio_backend_thread& thread, usz ring_bytes)
	{
		lr_audio_ring_free(&thread.ringbuf);
		CHECK(lr_audio_ring_init(&thread.ringbuf, ring_bytes));
		thread.callback_tmp_buf.resize(8192);
	}

	static lr_audio_ring& ring(rsxaudio_backend_thread& thread) { return thread.ringbuf; }

	static u32 fill(rsxaudio_backend_thread& thread, const config& cfg, u32 bytes, void* out)
	{
		return thread.fill_from_ring(cfg, bytes, out);
	}

	static config stereo(bool s16, u16 volume)
	{
		config cfg{};
		cfg.ready = true;
		cfg.input_ch_cnt = 2;
		cfg.output_channel_layout = static_cast<u8>(audio_channel_layout::stereo);
		cfg.convert_to_s16 = s16;
		cfg.target_volume = volume;
		cfg.current_volume = volume;
		cfg.initial_volume = volume;
		return cfg;
	}
};

typedef rsxaudio_backend_test T;

// Pushes samples @from.. as the data thread does, in blocks; the next sample's number
static u64 push(lr_audio_ring& ring, u64 from, u32 samples)
{
	std::vector<f32> block(samples);
	for (u32 i = 0; i < samples; i++)
		block[i] = sample_of(from + i);
	CHECK(lr_audio_ring_push(&ring, block.data(), samples * sizeof(f32), false) == samples * sizeof(f32));
	return from + samples;
}

static double now_ns()
{
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main()
{
	rsxaudio_backend_thread thread;
	T::setup(thread, 4096 * sizeof(f32));
	lr_audio_ring& ring = T::ring(thread);

	std::vector<f32> out(4096), expected(4096), work(4096);
	std::vector<s16> out16(4096), expected16(4096);

	// Float out, volume at rest: converted straight out of the ring. Blocks
	// of sizes that do not divide the ring, round it many times
	{
		const T::config cfg = T::stereo(false, T::config::VOL_NOMINAL);
		u64 in = 0, taken = 0, wrong = 0;

		for (u32 step = 0; step < 20000; step++)
		{
			const u32 samples = 2 * (100 + step % 411);
			in = push(ring, in, samples);

			const u32 got = T::fill(thread, cfg, samples * sizeof(f32), out.data());
			CHECK(got == samples * sizeof(f32));

			for (u32 i = 0; i < samples; i++)
				work[i] = sample_of(taken + i);
			AudioBackend::normalize(samples, work.data(), expected.data());
			wrong += std::memcmp(out.data(), expected.data(), samples * sizeof(f32)) != 0;
			taken += samples;
		}
		CHECK(wrong == 0 && taken == in);
		CHECK(lr_audio_ring_used(&ring) == 0);
	}

	// 16-bit out, the same way
	{
		const T::config cfg = T::stereo(true, T::config::VOL_NOMINAL);
		u64 in = 1000, taken = 1000, wrong = 0;

		for (u32 step = 0; step < 5000; step++)
		{
			const u32 samples = 2 * (64 + step % 300);
			in = push(ring, in, samples);

			const u32 got = T::fill(thread, cfg, samples * sizeof(s16), out16.data());
			CHECK(got == samples * sizeof(s16));

			for (u32 i = 0; i < samples; i++)
				work[i] = sample_of(taken + i);
			AudioBackend::convert_to_s16(samples, work.data(), expected16.data());
			wrong += std::memcmp(out16.data(), expected16.data(), samples * sizeof(s16)) != 0;
			taken += samples;
		}
		CHECK(wrong == 0);
	}

	// Volume not at rest: the samples are copied out, scaled, then converted
	{
		const T::config cfg = T::stereo(false, 5000);
		u64 in = 7, taken = 7, wrong = 0;

		for (u32 step = 0; step < 5000; step++)
		{
			const u32 samples = 2 * (64 + step % 300);
			in = push(ring, in, samples);

			const u32 got = T::fill(thread, cfg, samples * sizeof(f32), out.data());
			CHECK(got == samples * sizeof(f32));

			for (u32 i = 0; i < samples; i++)
				work[i] = sample_of(taken + i);
			AudioBackend::apply_volume_static(0.5f, samples, work.data(), work.data());
			AudioBackend::normalize(samples, work.data(), expected.data());
			wrong += std::memcmp(out.data(), expected.data(), samples * sizeof(f32)) != 0;
			taken += samples;
		}
		CHECK(wrong == 0);
	}

	// Asked for more than is waiting: what there is, in order, and then nothing
	{
		const T::config cfg = T::stereo(false, T::config::VOL_NOMINAL);
		push(ring, 50000, 200);
		const u32 got = T::fill(thread, cfg, 512 * sizeof(f32), out.data());
		CHECK(got == 200 * sizeof(f32));
		for (u32 i = 0; i < 200; i++)
			work[i] = sample_of(50000 + i);
		AudioBackend::normalize(200, work.data(), expected.data());
		CHECK(std::memcmp(out.data(), expected.data(), 200 * sizeof(f32)) == 0);

		// Empty: silence for the whole request
		std::fill(out.begin(), out.end(), 1.0f);
		CHECK(T::fill(thread, cfg, 512 * sizeof(f32), out.data()) == 512 * sizeof(f32));
		usz loud = 0;
		for (u32 i = 0; i < 512; i++)
			loud += out[i] != 0.0f;
		CHECK(loud == 0);
	}

	// The backend starting: what was waiting is dropped, what comes after is not
	{
		const T::config cfg = T::stereo(false, T::config::VOL_NOMINAL);
		push(ring, 111, 600);
		lr_audio_ring_flush(&ring);
		CHECK(lr_audio_ring_used(&ring) == 0);
		push(ring, 90000, 300);
		const u32 got = T::fill(thread, cfg, 300 * sizeof(f32), out.data());
		CHECK(got == 300 * sizeof(f32));
		for (u32 i = 0; i < 300; i++)
			work[i] = sample_of(90000 + i);
		AudioBackend::normalize(300, work.data(), expected.data());
		CHECK(std::memcmp(out.data(), expected.data(), 300 * sizeof(f32)) == 0);
	}

	// A callback's worth (256 stereo frames) in and out, against the ring replaced
	if (std::getenv("RSXAUDIO_BENCH"))
	{
		const T::config cfg = T::stereo(false, T::config::VOL_NOMINAL);
		const u32 samples = 512, rounds = 2000000;
		std::vector<f32> block(samples);
		for (u32 i = 0; i < samples; i++)
			block[i] = sample_of(i) * 0.5f;

		double best_old = 1e18, best_new = 1e18;
		for (u32 k = 0; k < 5; k++)
		{
			simple_ringbuf old_ring(4096 * sizeof(f32));
			double t = now_ns();
			for (u32 i = 0; i < rounds; i++)
			{
				old_ring.push(block.data(), samples * sizeof(f32));
				old_ring.pop(work.data(), samples * sizeof(f32), true);
				AudioBackend::normalize(samples, work.data(), out.data());
			}
			best_old = std::min(best_old, (now_ns() - t) / rounds);

			t = now_ns();
			for (u32 i = 0; i < rounds; i++)
			{
				lr_audio_ring_push(&ring, block.data(), samples * sizeof(f32), false);
				T::fill(thread, cfg, samples * sizeof(f32), out.data());
			}
			best_new = std::min(best_new, (now_ns() - t) / rounds);
		}
		std::printf("test_rsxaudio_ring: 256 stereo frames in and out: %.0f ns with the old ring, %.0f ns now\n", best_old, best_new);
	}

	std::printf("test_rsxaudio_ring: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
