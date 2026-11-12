// The game's audio on its way to the frontend (libretro_audio.cpp): each
// retro_run pulls what is due from cellAudio's ring, the write callback,
// and hands it over.
//
// - a frontend that takes floats (multi-channel, batch_float) gets the
//   game's floats as they are, bit for bit, in order across runs;
// - one that takes s16 gets them converted, in order, stereo through the
//   plain batch callback;
// - cellAudio's ring is lent, not copied: the frontend is handed pointers
//   into the ring's own memory, the samples in order across the wrap and
//   across a flush (nothing written before it is heard after it), and
//   silence when the ring has less than is due;
// - while cellAudio's thread opens, closes and sets the callback over and
//   over, a run never waits for it: no run takes a millisecond.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "libretro_audio.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The game's samples: a value per sample that says where it is in the stream
static u32 s_produced = 0;
static float sample_value(u32 n) { return static_cast<float>(static_cast<int>(n % 20000) - 10000) / 16384.0f; }

static u32 produce(u32 bytes, void* out)
{
	float* f = static_cast<float*>(out);
	for (u32 i = 0; i < bytes / sizeof(float); i++)
		f[i] = sample_value(s_produced++);
	return bytes;
}

static std::vector<float> s_got_f32;
static std::vector<s16> s_got_s16;
static usz s_got_channels = 0;
// Where the frontend's floats came from: inside this memory, or not
static const u8* s_lend_begin = nullptr;
static const u8* s_lend_end = nullptr;
static usz s_from_ring = 0, s_not_from_ring = 0;
static usz s_batch_calls = 0;

static size_t RETRO_CALLCONV take_f32(const float* data, size_t frames, unsigned channels, unsigned)
{
	s_got_channels = channels;
	const u8* p = reinterpret_cast<const u8*>(data);
	if (p >= s_lend_begin && p + frames * channels * sizeof(float) <= s_lend_end)
		s_from_ring++;
	else
		s_not_from_ring++;
	s_got_f32.insert(s_got_f32.end(), data, data + frames * channels);
	return frames;
}

static size_t RETRO_CALLCONV take_multi_s16(const int16_t* data, size_t frames, unsigned channels, unsigned)
{
	s_got_channels = channels;
	s_got_s16.insert(s_got_s16.end(), data, data + frames * channels);
	return frames;
}

static size_t RETRO_CALLCONV take_s16(const int16_t* data, size_t frames)
{
	s_batch_calls++;
	s_got_s16.insert(s_got_s16.end(), data, data + frames * 2);
	return frames;
}

static bool s_offer_float = true;

static bool RETRO_CALLCONV environ_multi(unsigned cmd, void* data)
{
	if (cmd != RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI)
		return false;
	auto* cb = static_cast<retro_audio_sample_multi_callback*>(data);
	cb->batch_int16 = take_multi_s16;
	cb->batch_float = s_offer_float ? take_f32 : nullptr;
	return true;
}

static bool RETRO_CALLCONV environ_none(unsigned, void*) { return false; }

// Runs back to back, as fast as they come: the audio is paced by the runs,
// not by the time between them
static void run_for(int runs, double fps = 60.0)
{
	for (int i = 0; i < runs; i++)
		libretro_audio_process(take_s16, fps);
}

int main()
{
	// Floats to a frontend that takes them
	{
		CHECK(libretro_audio_negotiate_multi(environ_multi));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));
		backend.SetWriteCallback(produce);
		backend.Play();
		s_produced = 0;
		run_for(10);
		CHECK(s_got_f32.size() == 48000 / 60 * 2 * 10); // exactly a frame's worth a run
		CHECK(s_got_s16.empty());
		CHECK(s_got_channels == 2);
		usz wrong = 0;
		for (usz i = 0; i < s_got_f32.size(); i++)
			wrong += s_got_f32[i] != sample_value(static_cast<u32>(i));
		CHECK(wrong == 0);
		backend.Close();
	}

	// s16 to a frontend with multi-channel output but no floats
	{
		s_offer_float = false;
		s_got_f32.clear();
		s_got_s16.clear();
		CHECK(libretro_audio_negotiate_multi(environ_multi));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));
		backend.SetWriteCallback(produce);
		backend.Play();
		s_produced = 0;
		run_for(10);
		CHECK(s_got_f32.empty());
		CHECK(s_got_s16.size() == 48000 / 60 * 2 * 10);
		usz wrong = 0;
		for (usz i = 0; i < s_got_s16.size(); i++)
			wrong += std::abs(s_got_s16[i] - sample_value(static_cast<u32>(i)) * 32768.0f) > 1.0f;
		CHECK(wrong == 0);
		backend.Close();
	}

	// s16 stereo through the plain batch callback
	{
		s_got_s16.clear();
		CHECK(!libretro_audio_negotiate_multi(environ_none));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));
		backend.SetWriteCallback(produce);
		backend.Play();
		s_produced = 0;
		run_for(5);
		CHECK(s_batch_calls > 0);
		CHECK(s_got_s16.size() == 48000 / 60 * 2 * 5);
		backend.Close();
	}

	// NTSC: 48000 / 59.94 = 800.8 a run, the fractions carried, whatever the
	// time between runs; and silence, the same amount, while not playing
	{
		s_got_s16.clear();
		CHECK(!libretro_audio_negotiate_multi(environ_none));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));
		backend.SetWriteCallback(produce);
		backend.Play();
		const double ntsc = 60.0 * 1000.0 / 1001.0;
		// 5005 runs of 800.8: 4008004 frames, give or take the fraction
		// carried in, and every run 800 or 801
		u32 odd_runs = 0;
		s_got_s16.clear();
		for (int i = 0; i < 5005; i++)
		{
			const usz before = s_got_s16.size();
			libretro_audio_process(take_s16, ntsc);
			const usz got = (s_got_s16.size() - before) / 2;
			odd_runs += got != 800 && got != 801;
		}
		const usz total = s_got_s16.size() / 2;
		CHECK(total + 1 >= 4008004 && total <= 4008004 + 1);
		CHECK(odd_runs == 0);
		backend.Pause();
		s_got_s16.clear();
		run_for(60);
		CHECK(s_got_s16.size() == 48000u * 2);
		bool silent = true;
		for (s16 v : s_got_s16)
			silent = silent && v == 0;
		CHECK(silent);
		backend.Close();
	}

	// cellAudio's ring, lent: the frontend reads the ring's own memory
	{
		s_offer_float = true;
		s_got_f32.clear();
		s_got_s16.clear();
		CHECK(libretro_audio_negotiate_multi(environ_multi));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));

		lr_audio_ring ring;
		CHECK(lr_audio_ring_init(&ring, 4800 * 2 * sizeof(float))); // a tenth of a second
		// The ring's memory, both mappings of it
		s_lend_begin = ring.q.buffer;
		s_lend_end = ring.q.buffer + ring.q.capacity * 2;
		backend.SetLendRing(&ring);
		backend.Play();

		u32 produced = 0;
		const auto fill = [&](u32 frames)
		{
			std::vector<float> block(frames * 2);
			for (float& f : block)
				f = sample_value(produced++);
			CHECK(lr_audio_ring_push(&ring, block.data(), block.size() * sizeof(float), false) == block.size() * sizeof(float));
		};

		// 200 runs, the ring filled before each: round its end many times
		s_from_ring = s_not_from_ring = 0;
		for (int i = 0; i < 200; i++)
		{
			fill(800);
			libretro_audio_process(take_s16, 60.0);
		}
		CHECK(s_got_f32.size() == 200u * 800 * 2);
		usz wrong = 0;
		for (usz i = 0; i < s_got_f32.size(); i++)
			wrong += s_got_f32[i] != sample_value(static_cast<u32>(i));
		CHECK(wrong == 0);
		CHECK(ring.q.mirror ? s_from_ring == 200 && s_not_from_ring == 0 : s_from_ring > 0);
		CHECK(lr_audio_ring_active(&ring));
		std::printf("test_audio_frontend: 200 runs from cellAudio's ring: %zu handed over from the ring's own memory, %zu from a copy\n", s_from_ring, s_not_from_ring);

		// Less in the ring than is due: what is there, then silence
		s_got_f32.clear();
		fill(300);
		libretro_audio_process(take_s16, 60.0);
		CHECK(s_got_f32.size() == 800u * 2);
		wrong = 0;
		for (usz i = 0; i < 600; i++)
			wrong += s_got_f32[i] != sample_value(200u * 1600 + static_cast<u32>(i));
		for (usz i = 600; i < 1600; i++)
			wrong += s_got_f32[i] != 0.0f;
		CHECK(wrong == 0);

		// A flush: what was written before it is not heard after it
		s_got_f32.clear();
		fill(800);
		lr_audio_ring_flush(&ring);
		CHECK(!lr_audio_ring_active(&ring));
		const u32 after_flush = produced;
		fill(800);
		libretro_audio_process(take_s16, 60.0);
		CHECK(s_got_f32.size() == 800u * 2);
		wrong = 0;
		for (usz i = 0; i < s_got_f32.size(); i++)
			wrong += s_got_f32[i] != sample_value(after_flush + static_cast<u32>(i));
		CHECK(wrong == 0);

		// The ring taken away: silence, and the ring is not touched again
		backend.SetLendRing(nullptr);
		fill(800);
		const usz used = lr_audio_ring_used(&ring);
		s_got_f32.clear();
		s_got_s16.clear();
		libretro_audio_process(take_s16, 60.0);
		CHECK(lr_audio_ring_used(&ring) == used);
		CHECK(s_got_f32.empty() && s_got_s16.size() == 800u * 2);

		backend.Close();
		lr_audio_ring_free(&ring);
		s_lend_begin = s_lend_end = nullptr;
	}

	// cellAudio's thread busy with the backend: no run waits
	{
		CHECK(libretro_audio_negotiate_multi(environ_multi));
		LibretroAudioBackend backend;
		CHECK(backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic));
		backend.SetWriteCallback(produce);
		backend.Play();

		std::atomic<bool> stop{false};
		std::thread other([&]
		{
			while (!stop)
			{
				backend.SetWriteCallback(produce);
				backend.Open("", AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::automatic);
				backend.Play();
			}
		});

		double worst_ms = 0;
		for (int i = 0; i < 100; i++)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			s_got_f32.clear();
			const auto t0 = std::chrono::steady_clock::now();
			libretro_audio_process(take_s16, 60.0);
			worst_ms = std::max(worst_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		}
		stop = true;
		other.join();
		backend.Close();
		CHECK(worst_ms < 1.0);
		std::printf("test_audio_frontend: slowest run while cellAudio's thread churns: %.3f ms\n", worst_ms);
	}

	std::printf("test_audio_frontend: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
