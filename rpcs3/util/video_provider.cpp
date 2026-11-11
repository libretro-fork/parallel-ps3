#include "stdafx.h"
#include "video_provider.h"
#include "Emu/RSX/Overlays/overlay_message.h"
#include "Emu/Cell/timers.hpp"
#include "Emu/system_config.h"

#include "util/media_encode.h"

#include <thread>

LOG_CHANNEL(media_log, "Media");

atomic_t<recording_mode> g_recording_mode = recording_mode::stopped;

template <>
void fmt_class_string<recording_mode>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](recording_mode value)
	{
		switch (value)
		{
		case recording_mode::stopped: return "stopped";
		case recording_mode::rpcs3: return "rpcs3";
		case recording_mode::cell: return "cell";
		}

		return unknown;
	});
}

namespace utils
{
	video_provider::sink_use::sink_use(video_provider& o, retro_atomic_int_t& u)
		: owner(o), users(u)
	{
		// Counted in before the pointer is read: set_video_sink() either sees
		// the count, or this sees the pointer it published
		retro_atomic_fetch_add_seq_cst_int(&users, 1);
		sink = static_cast<video_sink*>(retro_atomic_load_acquire_ptr(&owner.m_sink));
	}

	video_provider::sink_use::~sink_use()
	{
		retro_atomic_fetch_sub_int(&users, 1);
	}

	video_provider::video_provider()
	{
		retro_atomic_ptr_init(&m_sink, nullptr);
		retro_atomic_int_init(&m_video_users, 0);
		retro_atomic_int_init(&m_audio_users, 0);
		retro_atomic_size_init(&m_generation, 0);
		retro_atomic_size_init(&m_pause_time_us, 0);
	}

	video_provider::~video_provider()
	{
		g_recording_mode = recording_mode::stopped;
	}

	bool video_provider::set_video_sink(std::shared_ptr<video_sink> sink, recording_mode type)
	{
		media_log.notice("video_provider: setting new video sink. sink=%d, type=%s", !!sink, type);

		if (type == recording_mode::stopped)
		{
			// Prevent misuse. type is supposed to be a valid state.
			media_log.error("video_provider: cannot set video sink with type %s", type);
			return false;
		}

		if (m_video_sink)
		{
			// cell has preference
			if (m_type == recording_mode::cell && m_type != type)
			{
				media_log.warning("video_provider: cannot set video sink with type %s if type %s is active", type, m_type);
				return false;
			}
		}

		const std::shared_ptr<video_sink> old = m_video_sink;
		const recording_mode old_type = m_type;

		m_type = sink ? type : recording_mode::stopped;
		m_video_sink = sink;
		// A new sink, or none: the times handed over start again
		if (sink != old || !sink)
			retro_atomic_fetch_add_size(&m_generation, 1);
		retro_atomic_store_release_ptr(&m_sink, sink.get());
		m_active = (m_type != recording_mode::stopped);

		if (!m_active)
		{
			m_start_time_us.store(umax);
		}

		if (old && (old_type != type || old != sink))
		{
			// A sink replaced goes out of use first: the renderer and
			// cellAudio may still be handing it a frame or a block they took
			// up before the new one was published. This is the PPU thread's;
			// neither of theirs waits. One kept is stopped as it is: its
			// queues clear without stopping either side.
			if (old != sink)
			{
				while (retro_atomic_fetch_add_seq_cst_int(&m_video_users, 0) != 0 ||
					retro_atomic_fetch_add_seq_cst_int(&m_audio_users, 0) != 0)
				{
					std::this_thread::yield();
				}
			}

			media_log.warning("video_provider: stopping current video sink of type %s", old_type);
			old->stop();
		}

		return true;
	}

	void video_provider::set_pause_time_us(usz pause_time_us)
	{
		retro_atomic_store_release_size(&m_pause_time_us, pause_time_us);
	}

	recording_mode video_provider::check_mode(video_sink& sink)
	{
		if (sink.has_error)
		{
			g_recording_mode = recording_mode::stopped;

			if (g_cfg.misc.show_capture_hints)
			{
				rsx::overlays::queue_message(localized_string_id::RECORDING_ABORTED);
			}
		}

		if (g_recording_mode == recording_mode::stopped)
		{
			m_active = false;
		}

		return g_recording_mode;
	}

	bool video_provider::can_consume_frame()
	{
		if (!m_active)
		{
			return false;
		}

		const sink_use use(*this, m_video_users);

		if (!use.sink || !use.sink->use_internal_video)
		{
			return false;
		}

		if (const usz gen = retro_atomic_load_acquire_size(&m_generation); gen != m_video_generation)
		{
			m_video_generation = gen;
			m_last_video_pts_incoming = -1;
		}

		const usz pause_time_us = retro_atomic_load_acquire_size(&m_pause_time_us);
		const usz elapsed_us = get_system_time() - m_start_time_us;
		ensure(elapsed_us >= pause_time_us);

		const usz timestamp_ms = (elapsed_us - pause_time_us) / 1000;
		const s64 pts = use.sink->get_pts(timestamp_ms);
		return pts > m_last_video_pts_incoming;
	}

	void video_provider::present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra)
	{
		if (!m_active)
		{
			return;
		}

		const sink_use use(*this, m_video_users);

		if (!use.sink || check_mode(*use.sink) == recording_mode::stopped)
		{
			return;
		}

		if (const usz gen = retro_atomic_load_acquire_size(&m_generation); gen != m_video_generation)
		{
			m_video_generation = gen;
			m_last_video_pts_incoming = -1;
		}

		const u64 current_time_us = get_system_time();

		if (m_start_time_us.compare_and_swap_test(umax, current_time_us))
		{
			media_log.notice("video_provider: start time = %d", current_time_us);
		}

		// Calculate presentation timestamp.
		const usz pause_time_us = retro_atomic_load_acquire_size(&m_pause_time_us);
		const usz elapsed_us = current_time_us - m_start_time_us;
		ensure(elapsed_us >= pause_time_us);

		const usz timestamp_ms = (elapsed_us - pause_time_us) / 1000;
		const s64 pts = use.sink->get_pts(timestamp_ms);

		// We can just skip this frame if it has the same timestamp.
		if (pts <= m_last_video_pts_incoming)
		{
			return;
		}

		if (use.sink->add_frame(data, pitch, width, height, is_bgra ? ME_PIXEL_BGRA : ME_PIXEL_RGBA, timestamp_ms))
		{
			m_last_video_pts_incoming = pts;
		}
	}

	void video_provider::present_samples(const u8* buf, u32 sample_count, u16 channels)
	{
		if (!buf || !sample_count || !channels || !m_active)
		{
			return;
		}

		const sink_use use(*this, m_audio_users);

		if (!use.sink || !use.sink->use_internal_audio)
		{
			return;
		}

		if (check_mode(*use.sink) == recording_mode::stopped)
		{
			return;
		}

		if (const usz gen = retro_atomic_load_acquire_size(&m_generation); gen != m_audio_generation)
		{
			m_audio_generation = gen;
			m_last_audio_pts_incoming = -1;
		}

		const u64 current_time_us = get_system_time();

		if (m_start_time_us.compare_and_swap_test(umax, current_time_us))
		{
			media_log.notice("video_provider: start time = %d", current_time_us);
		}

		// Calculate presentation timestamp.
		const usz pause_time_us = retro_atomic_load_acquire_size(&m_pause_time_us);
		const usz elapsed_us = current_time_us - m_start_time_us;
		ensure(elapsed_us >= pause_time_us);

		const usz timestamp_us = elapsed_us - pause_time_us;
		const s64 pts = use.sink->get_audio_pts(timestamp_us);

		// We can just skip this sample if it has the same timestamp.
		if (pts <= m_last_audio_pts_incoming)
		{
			return;
		}

		if (use.sink->add_audio_samples(buf, sample_count, channels, timestamp_us))
		{
			m_last_audio_pts_incoming = pts;
		}
	}
}
