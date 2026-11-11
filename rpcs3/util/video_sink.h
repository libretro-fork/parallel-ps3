#pragma once

#include "util/types.hpp"
#include "util/atomic.hpp"

#include <retro_atomic.h>
#include <retro_spsc.h>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace utils
{
	// Frames and audio from one producer to one consumer (the renderer or
	// cellRec to cellRec or the recorder), lock-free: a fixed set of slots,
	// handed over by index through two retro_spsc rings - filled ones to the
	// consumer, free ones back. A frame's buffer is swapped in and out of its
	// slot, not copied, so buffers go round instead of being allocated for
	// each frame; audio is copied into the slot's own storage. Clearing
	// moves the epoch on, and the consumer drops what was handed over
	// before it, so neither side has to stop for it.
	class video_sink
	{
	public:
		struct encoder_frame
		{
			s64 pts = -1; // Optional
			usz timestamp_ms = 0;
			u32 pitch = 0;
			u32 width = 0;
			u32 height = 0;
			s32 pixel_format = 0; // ME_PIXEL_* (util/media_encode.h)
			std::vector<u8> data;
		};

		// The most a slot holds: a cellAudio block of eight channels
		static constexpr u32 max_block_samples = 256;
		static constexpr u32 max_channels = 8;

		struct encoder_sample
		{
			usz timestamp_us = 0;
			u32 sample_count = 0;
			u16 channels = 0;
			std::array<f32, max_block_samples * max_channels> data{};
		};

		video_sink()
		{
			retro_atomic_size_init(&m_epoch, 0);
			init_rings(m_frame_filled, m_frame_free, frame_slots);
			init_rings(m_sample_filled, m_sample_free, sample_slots);
		}

		virtual ~video_sink()
		{
			retro_spsc_free(&m_frame_filled);
			retro_spsc_free(&m_frame_free);
			retro_spsc_free(&m_sample_filled);
			retro_spsc_free(&m_sample_free);
		}

		video_sink(const video_sink&) = delete;
		video_sink& operator=(const video_sink&) = delete;

		virtual void stop(bool flush = true) = 0;
		virtual void pause(bool flush = true) = 0;
		virtual void resume() = 0;

		// Producer: the frame's buffer goes into a free slot and @frame gets
		// the slot's old one back to reuse. false when paused, flushing, or
		// every slot is waiting for the consumer (the frame is dropped) -
		// unless @wait, for a producer off the game's threads handing over a
		// backlog (cellRec's ring at its close), which waits for the consumer
		// to free one, until it fails.
		bool add_frame(std::vector<u8>& frame, u32 pitch, u32 width, u32 height, s32 pixel_format, usz timestamp_ms, bool wait = false)
		{
			// Do not allow new frames while flushing or paused
			if (m_flush || m_paused)
				return false;

			u32 index;
			while (!take(m_frame_free, index))
			{
				if (!wait || has_error)
					return false;
				std::this_thread::yield();
			}

			slot_frame& slot = m_frames[index];
			slot.frame.data.swap(frame);
			slot.frame.pts = -1;
			slot.frame.timestamp_ms = timestamp_ms;
			slot.frame.pitch = pitch;
			slot.frame.width = width;
			slot.frame.height = height;
			slot.frame.pixel_format = pixel_format;
			slot.epoch = retro_atomic_load_acquire_size(&m_epoch);
			give(m_frame_filled, index);
			return true;
		}

		// Producer: @sample_count interleaved float frames, in blocks of up
		// to max_block_samples, each into a free slot; false when paused,
		// flushing, the channels are more than a slot holds, or a block found
		// no free slot (it and the rest are dropped; @wait as add_frame's)
		bool add_audio_samples(const u8* buf, u32 sample_count, u16 channels, usz timestamp_us, bool wait = false)
		{
			// Do not allow new samples while flushing or paused
			if (m_flush || m_paused || !buf || !sample_count || !channels || channels > max_channels)
				return false;

			const usz epoch = retro_atomic_load_acquire_size(&m_epoch);

			for (u32 done = 0; done < sample_count;)
			{
				const u32 count = std::min(sample_count - done, max_block_samples);
				u32 index;

				while (!take(m_sample_free, index))
				{
					if (!wait || has_error)
						return false;
					std::this_thread::yield();
				}

				slot_sample& slot = m_samples[index];
				slot.sample.timestamp_us = timestamp_us + static_cast<usz>(done * 1000000ull / m_sample_rate);
				slot.sample.sample_count = count;
				slot.sample.channels = channels;
				std::memcpy(slot.sample.data.data(), buf + static_cast<usz>(done) * channels * sizeof(f32), static_cast<usz>(count) * channels * sizeof(f32));
				slot.epoch = epoch;
				give(m_sample_filled, index);
				done += count;
			}

			return true;
		}

		// Consumer: the next frame into @out, @out's buffer going back into
		// the slot to be reused; false when there is none
		bool pop_frame(encoder_frame& out)
		{
			u32 index;
			while (take(m_frame_filled, index))
			{
				slot_frame& slot = m_frames[index];
				const bool current = slot.epoch == retro_atomic_load_acquire_size(&m_epoch);

				if (current)
				{
					out.data.swap(slot.frame.data);
					out.pts = slot.frame.pts;
					out.timestamp_ms = slot.frame.timestamp_ms;
					out.pitch = slot.frame.pitch;
					out.width = slot.frame.width;
					out.height = slot.frame.height;
					out.pixel_format = slot.frame.pixel_format;
				}

				give(m_frame_free, index);

				if (current)
					return true;
			}

			return false;
		}

		// Consumer: the next block of samples, valid until release_sample();
		// nullptr when there is none
		const encoder_sample* peek_sample()
		{
			// One held across a clear goes back
			if (m_sample_held != umax && m_samples[m_sample_held].epoch != retro_atomic_load_acquire_size(&m_epoch))
				release_sample();

			while (m_sample_held == umax && take(m_sample_filled, m_sample_held))
			{
				if (m_samples[m_sample_held].epoch != retro_atomic_load_acquire_size(&m_epoch))
				{
					// Handed over before the last clear
					give(m_sample_free, m_sample_held);
					m_sample_held = umax;
				}
			}

			return m_sample_held == umax ? nullptr : &m_samples[m_sample_held].sample;
		}

		void release_sample()
		{
			if (m_sample_held != umax)
			{
				give(m_sample_free, m_sample_held);
				m_sample_held = umax;
			}
		}

		s64 get_pts(usz timestamp_ms) const
		{
			return static_cast<s64>(std::round((timestamp_ms * m_framerate) / 1000.0));
		}

		s64 get_audio_pts(usz timestamp_us) const
		{
			static constexpr f64 us_per_sec = 1000000.0;
			const f64 us_per_block = us_per_sec / (m_sample_rate / static_cast<f64>(m_samples_per_block));
			return static_cast<s64>(std::round(timestamp_us / us_per_block));
		}

		usz get_timestamp_ms(s64 pts) const
		{
			return static_cast<usz>(std::round((pts * 1000) / static_cast<f64>(m_framerate)));
		}

		usz get_audio_timestamp_us(s64 pts) const
		{
			static constexpr f64 us_per_sec = 1000000.0;
			const f64 us_per_block = us_per_sec / (m_sample_rate / static_cast<f64>(m_samples_per_block));
			return static_cast<usz>(pts * us_per_block);
		}

		atomic_t<bool> has_error{false};

		// These two variables should only be set once before we start encoding, so we don't need mutexes or atomics.
		bool use_internal_audio = false; // True if we want to fetch samples from cellAudio
		bool use_internal_video = false; // True if we want to fetch frames from rsx

		// About a second of frames at cellRec's 30 a second (a slot holds a
		// buffer only while it is handed over), and a third of a second of
		// 48 kHz audio in cellAudio's blocks of 256 (8 KiB a slot)
		static constexpr u32 frame_slots = 32;
		static constexpr u32 sample_slots = 64;

	protected:
		// What was handed over before this drops: from any thread, without
		// stopping either side
		void clear_queues()
		{
			retro_atomic_fetch_add_size(&m_epoch, 1);
		}

		atomic_t<bool> m_paused = false;
		atomic_t<bool> m_flush = false;
		u32 m_framerate = 30;
		u32 m_sample_rate = 48000;
		static constexpr u32 m_samples_per_block = 256;

	private:
		struct slot_frame
		{
			encoder_frame frame;
			usz epoch = 0;
		};

		struct slot_sample
		{
			encoder_sample sample;
			usz epoch = 0;
		};

		static void init_rings(retro_spsc_t& filled, retro_spsc_t& free, u32 slots)
		{
			// Room for every index in both: neither ever refuses one
			ensure(retro_spsc_init(&filled, slots * sizeof(u32)));
			ensure(retro_spsc_init(&free, slots * sizeof(u32)));

			for (u32 i = 0; i < slots; i++)
				give(free, i);
		}

		static bool take(retro_spsc_t& ring, u32& index)
		{
			return retro_spsc_read_avail(&ring) >= sizeof(u32) && retro_spsc_read(&ring, &index, sizeof(u32)) == sizeof(u32);
		}

		static void give(retro_spsc_t& ring, u32 index)
		{
			// Never full: it has room for every slot
			retro_spsc_write(&ring, &index, sizeof(u32));
		}

		retro_atomic_size_t m_epoch;

		std::array<slot_frame, frame_slots> m_frames{};
		retro_spsc_t m_frame_filled{}; // producer -> consumer
		retro_spsc_t m_frame_free{};   // consumer -> producer

		std::unique_ptr<std::array<slot_sample, sample_slots>> m_sample_storage = std::make_unique<std::array<slot_sample, sample_slots>>();
		std::array<slot_sample, sample_slots>& m_samples = *m_sample_storage;
		retro_spsc_t m_sample_filled{};
		retro_spsc_t m_sample_free{};
		u32 m_sample_held = umax; // the consumer's, between peek and release
	};
}
