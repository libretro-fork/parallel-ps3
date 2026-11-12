#pragma once

#include "Utilities/StrUtil.h"
#include "Utilities/Thread.h"
#include "Utilities/mutex.h"
#include "util/video_sink.h"
#include "Emu/Cell/Modules/cellMusic.h"

#include <unordered_map>
#include <deque>
#include <mutex>
#include <queues/retro_deque.h>

// A decoded block: where it starts in the decoder's data, and its time
struct media_timestamp
{
	u64 first;  // byte offset
	u64 second; // milliseconds
};

RETRO_DEQUE_DEFINE(media_timestamps, media_timestamp);

struct md_audio;

namespace utils
{
	struct media_info
	{
		std::string path;
		std::string sub_type; // The sub type if available (png, jpg...)

		s32 audio_codec = 0; // MD_CODEC_* (util/media_decode.h), 0 = none
		s32 video_codec = 0; // MD_CODEC_* (util/media_decode.h), 0 = none
		s32 audio_bitrate_bps = 0; // Bit rate in bit/s
		s32 video_bitrate_bps = 0; // Bit rate in bit/s
		s32 sample_rate = 0; // Samples per second
		s64 duration_us = 0; // in microseconds
		s32 width = 0;  // Width if available
		s32 height = 0; // Height if available
		s32 orientation = 0; // Orientation if available (= CellSearchOrientation)

		std::unordered_map<std::string, std::string> metadata;

		// Convenience function for metadata
		template <typename T>
		T get_metadata(const std::string& key, const T& def) const;
	};

	// av_media_type: 0 a video file, 1 a music file, -1 an image
	std::pair<bool, media_info> get_media_info(const std::string& path, s32 av_media_type);

	template <typename D>
	void parse_metadata(D& dst, const utils::media_info& mi, const std::string& key, const std::string& def, usz max_length)
	{
		std::string value = mi.get_metadata<std::string>(key, def);
		if (value.size() > max_length)
		{
			value.resize(max_length);
		}
		strcpy_trunc(dst, value);
	};

	class audio_decoder
	{
	public:
		audio_decoder();
		~audio_decoder();

		void set_context(music_selection_context&& context);
		void set_swap_endianness(bool swapped);
		void clear();
		void stop();
		void decode();
		u32 set_next_index(bool next);

		shared_mutex m_mtx;
		static constexpr s32 sample_rate = 48000;
		std::vector<u8> data;
		atomic_t<u64> m_size = 0;
		atomic_t<u32> track_fully_decoded{0};
		atomic_t<u32> track_fully_consumed{0};
		atomic_t<bool> has_error{false};
		media_timestamps_t timestamps_ms{}; // where in data each decoded block starts, and its time

	private:
		bool m_swap_endianness = false;
		music_selection_context m_context{};
		md_audio* m_md = nullptr; // the decoder, opened over each track in turn
		std::vector<f32> m_pcm;   // what it hands over, before it goes into data
		std::unique_ptr<named_thread<std::function<void()>>> m_thread;
	};

	// What cellRec records: util/media_encode on its own thread, fed from
	// video_sink's queues
	class video_encoder : public utils::video_sink
	{
	public:
		video_encoder();
		~video_encoder();

		struct frame_format
		{
			s32 pixel_format = 0; // ME_PIXEL_* (util/media_encode.h)
			u32 width = 0;
			u32 height = 0;
			u32 pitch = 0;

			std::string to_string() const
			{
				return fmt::format("{ pixel_format=%d, width=%d, height=%d, pitch=%d }", pixel_format, width, height, pitch);
			}
		};

		// The longest recording: the file's index is sized for it
		static constexpr u32 max_seconds = 4 * 60 * 60;

		std::string path() const;
		s64 last_video_pts() const;

		void set_path(const std::string& path);
		void set_framerate(u32 framerate);
		void set_video_bitrate(u32 bitrate);
		void set_output_format(frame_format format);
		void set_video_codec(s32 codec); // ME_VIDEO_*
		void set_gop_size(s32 gop_size);
		void set_sample_rate(u32 sample_rate);
		void set_audio_channels(u32 channels);
		void set_audio_bitrate(u32 bitrate);
		void set_audio_codec(s32 codec); // ME_AUDIO_*
		void pause(bool flush = true) override;
		void stop(bool flush = true) override;
		void resume() override;
		void encode();

	private:
		std::string m_path;
		s64 m_last_audio_pts = 0;
		s64 m_last_video_pts = 0;

		// Thread control
		std::unique_ptr<named_thread<std::function<void()>>> m_thread;
		atomic_t<bool> m_running = false;

		// Video parameters
		u32 m_video_bitrate_bps = 0;
		s32 m_video_codec = 0; // ME_VIDEO_MPEG4
		s32 m_gop_size = 12;
		frame_format m_out_format{};

		// Audio parameters
		u32 m_channels = 2;
		u32 m_audio_bitrate_bps = 320000;
		s32 m_audio_codec = 0; // ME_AUDIO_AAC
	};
}
