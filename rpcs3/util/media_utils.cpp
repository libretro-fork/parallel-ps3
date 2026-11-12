#include "stdafx.h"
#include "media_utils.h"
#include "media_decode.h"
#include "media_encode.h"
#include "Emu/emu_callbacks.h"

#include <formats/rtags.h>

#include <random>
#include <thread>


LOG_CHANNEL(media_log, "Media");

namespace utils
{
	template <typename T>
	static inline void write_byteswapped(const void* src, void* dst)
	{
		*reinterpret_cast<T*>(dst) = *reinterpret_cast<const be_t<T>*>(src);
	}

	template <typename T>
	static inline void copy_samples(const u8* src, u8* dst, usz sample_count, bool swap_endianness)
	{
		if (swap_endianness)
		{
			for (usz i = 0; i < sample_count; i++)
			{
				write_byteswapped<T>(src + i * sizeof(T), dst + i * sizeof(T));
			}
		}
		else
		{
			std::memcpy(dst, src, sample_count * sizeof(T));
		}
	}

	template <typename T>
	T media_info::get_metadata([[maybe_unused]] const std::string& key, const T& def) const
	{
		static_assert("unimplemented");
		return def;
	}

	template <>
	std::string media_info::get_metadata(const std::string& key, const std::string& def) const
	{
		if (metadata.contains(key))
		{
			return ::at32(metadata, key);
		}

		return def;
	}

	template <>
	s64 media_info::get_metadata(const std::string& key, const s64& def) const
	{
		if (metadata.contains(key))
		{
			s64 result{};
			if (try_to_int64(&result, ::at32(metadata, key), smin, smax))
			{
				return result;
			}
		}

		return def;
	}

	std::pair<bool, media_info> get_media_info(const std::string& path, s32 av_media_type)
	{
		media_info info{};
		info.path = path;

		if (av_media_type == -1) // Let's use this for image info
		{
			const bool success = g_emu_callbacks.get_image_info(path, info.sub_type, info.width, info.height, info.orientation);
			if (!success) media_log.error("get_media_info: failed to get image info for '%s'", path);
			return { success, std::move(info) };
		}

		md_file file;
		if (!md_file_open(&file, path.c_str()))
		{
			media_log.notice("get_media_info: could not open file '%s'", path);
			return { false, std::move(info) };
		}

		md_info mi;
		if (!md_probe(file.data, file.len, av_media_type == 0, &mi))
		{
			md_file_close(&file);
			media_log.notice("get_media_info: '%s' is not a %s file read here", path, av_media_type == 0 ? "video" : "music");
			return { false, std::move(info) };
		}

		info.audio_codec = mi.audio_codec;
		info.video_codec = mi.video_codec;
		info.audio_bitrate_bps = mi.audio_bitrate;
		info.video_bitrate_bps = mi.video_bitrate;
		info.sample_rate = mi.sample_rate;
		info.duration_us = mi.duration_us;

		// The tags, under the keys FFmpeg gave them
		static const struct { rtags_field field; const char* key; } tags[] =
		{
			{ RTAGS_TITLE, "title" }, { RTAGS_ARTIST, "artist" }, { RTAGS_ALBUM, "album" },
			{ RTAGS_GENRE, "genre" }, { RTAGS_TRACK, "track" }, { RTAGS_DISC, "disc" },
			{ RTAGS_DATE, "date" },
		};
		char value[1024];
		for (const auto& tag : tags)
		{
			if (rtags_get(file.data, file.len, tag.field, value, sizeof(value)))
			{
				info.metadata[tag.key] = value;
			}
		}

		md_file_close(&file);
		return { true, std::move(info) };
	}

	audio_decoder::audio_decoder()
		: m_md(md_audio_new()), m_pcm(MD_READ_MIN * 2)
	{
	}

	audio_decoder::~audio_decoder()
	{
		stop();
		md_audio_free(m_md);
		media_timestamps_free(&timestamps_ms);
	}

	void audio_decoder::set_context(music_selection_context&& context)
	{
		m_context = std::move(context);
	}

	void audio_decoder::set_swap_endianness(bool swapped)
	{
		m_swap_endianness = swapped;
	}

	void audio_decoder::clear()
	{
		media_log.notice("audio_decoder: Clear data...");

		track_fully_decoded = 0;
		track_fully_consumed = 0;
		has_error = false;
		m_size = 0;
		media_timestamps_clear(&timestamps_ms);
		data.clear();
	}

	void audio_decoder::stop()
	{
		media_log.notice("audio_decoder: Stop decoding...");

		if (m_thread)
		{
			auto& thread = *m_thread;
			thread = thread_state::aborting;
			track_fully_consumed = 1;
			track_fully_consumed.notify_one();
			thread();
			m_thread.reset();
		}

		clear();
	}

	void audio_decoder::decode()
	{
		stop();

		media_log.notice("audio_decoder: %d entries in playlist. Start decoding...", m_context.playlist.size());

		const auto decode_track = [this](const std::string& path)
		{
			media_log.notice("audio_decoder: decoding %s", path);

			md_file file;
			if (!m_md || !md_file_open(&file, path.c_str()))
			{
				media_log.error("audio_decoder: Could not open file '%s'", path);
				has_error = true;
				return;
			}

			// Any format audio_transfer plays, folded to stereo, at 48 kHz
			if (!md_audio_open(m_md, file.data, file.len))
			{
				media_log.error("audio_decoder: '%s' is not in a format that can be played", path);
				md_file_close(&file);
				has_error = true;
				return;
			}

			constexpr u64 bytes_per_frame = 2 * sizeof(f32);

			// A whole track ends up in our data, so reserve it in one go instead of letting it grow piecewise.
			if (const u64 frames = md_audio_frames(m_md))
			{
				constexpr u64 max_reserve = 256 * 1024 * 1024; // Don't trust the header blindly
				std::scoped_lock lock(m_mtx);
				data.reserve(std::min<u64>(frames * bytes_per_frame, max_reserve));
			}

			while (thread_ctrl::state() != thread_state::aborting)
			{
				u64 timestamp_ms = 0;
				const usz frames = md_audio_read(m_md, m_pcm.data(), m_pcm.size() / 2, &timestamp_ms);
				if (!frames)
				{
					break;
				}

				const u64 size = frames * bytes_per_frame;
				u8* out = reinterpret_cast<u8*>(m_pcm.data());

				// The format is float 32bit per channel. Swap it in place, so we can append the buffer as-is.
				if (m_swap_endianness)
				{
					copy_samples<f32>(out, out, frames * 2, true);
				}

				{
					std::scoped_lock lock(m_mtx);
					data.insert(data.cend(), out, out + size);

					const media_timestamp stamp{m_size, static_cast<u64>(timestamp_ms)};
					ensure(media_timestamps_push_back_growing(&timestamps_ms, &stamp));
					m_size += size;
				}

				media_log.trace("audio_decoder: decoded frame_count=%d size=%d timestamp_ms=%d", frames, size, timestamp_ms);
			}

			if (md_audio_failed(m_md))
			{
				media_log.error("audio_decoder: Decoding error in '%s'", path);
				has_error = true;
			}

			md_audio_close(m_md);
			md_file_close(&file);
		};

		m_thread = std::make_unique<named_thread<std::function<void()>>>("Music Decode Thread", [this, decode_track]()
		{
			for (const std::string& track : m_context.playlist)
			{
				media_log.notice("audio_decoder: playlist entry: %s", track);
			}

			if (m_context.playlist.empty())
			{
				media_log.error("audio_decoder: Can not play empty playlist");
				has_error = true;
				return;
			}

			if (m_context.context_option == CELL_SEARCH_CONTEXTOPTION_SHUFFLE && m_context.playlist.size() > 1)
			{
				// Shuffle once if necessary
				media_log.notice("audio_decoder: shuffling initial playlist...");
				// Seeded from the clock: std::random_device keeps its state
				// on the stack, 5 KiB, past the core's 4 KiB stack frame
				auto engine = std::default_random_engine{static_cast<std::default_random_engine::result_type>(std::chrono::steady_clock::now().time_since_epoch().count())};
				std::shuffle(std::begin(m_context.playlist), std::end(m_context.playlist), engine);
			}

			while (thread_ctrl::state() != thread_state::aborting)
			{
				media_log.notice("audio_decoder: about to decode: %s (index=%d)", ::at32(m_context.playlist, m_context.current_track), m_context.current_track);

				decode_track(::at32(m_context.playlist, m_context.current_track));
				track_fully_decoded = 1;

				if (has_error)
				{
					media_log.notice("audio_decoder: stopping with error...");
					break;
				}

				// Let's only decode one track at a time. Wait for the consumer to finish reading the track.
				media_log.notice("audio_decoder: waiting until track is consumed...");

				while (thread_ctrl::state() != thread_state::aborting && !track_fully_consumed)
				{
					thread_ctrl::wait_on(track_fully_consumed, 0);
				}
				track_fully_consumed = 0;
			}

			media_log.notice("audio_decoder: finished playlist");
		});
	}

	u32 audio_decoder::set_next_index(bool next)
	{
		return m_context.step_track(next);
	}

	video_encoder::video_encoder()
		: utils::video_sink()
	{
	}

	video_encoder::~video_encoder()
	{
		stop();
	}

	std::string video_encoder::path() const
	{
		return m_path;
	}

	s64 video_encoder::last_video_pts() const
	{
		return m_last_video_pts;
	}

	void video_encoder::set_path(const std::string& path)
	{
		m_path = path;
	}

	void video_encoder::set_framerate(u32 framerate)
	{
		m_framerate = framerate;
	}

	void video_encoder::set_video_bitrate(u32 bitrate)
	{
		m_video_bitrate_bps = bitrate;
	}

	void video_encoder::set_output_format(video_encoder::frame_format format)
	{
		m_out_format = std::move(format);
	}

	void video_encoder::set_video_codec(s32 codec)
	{
		m_video_codec = codec;
	}

	void video_encoder::set_gop_size(s32 gop_size)
	{
		m_gop_size = gop_size;
	}

	void video_encoder::set_sample_rate(u32 sample_rate)
	{
		m_sample_rate = sample_rate;
	}

	void video_encoder::set_audio_channels(u32 channels)
	{
		m_channels = channels;
	}

	void video_encoder::set_audio_bitrate(u32 bitrate)
	{
		m_audio_bitrate_bps = bitrate;
	}

	void video_encoder::set_audio_codec(s32 codec)
	{
		m_audio_codec = codec;
	}

	void video_encoder::pause(bool flush)
	{
		if (m_thread)
		{
			m_paused = true;
			m_flush = flush;

			if (flush)
			{
				// Let's assume this will finish in a timely manner
				while (m_flush && m_running)
				{
					std::this_thread::sleep_for(1us);
				}
			}
		}
	}

	void video_encoder::stop(bool flush)
	{
		media_log.notice("video_encoder: Stopping video encoder. flush=%d", flush);

		if (m_thread)
		{
			m_flush = flush;

			if (flush)
			{
				// Let's assume this will finish in a timely manner
				while (m_flush && m_running)
				{
					std::this_thread::sleep_for(1ms);
				}
			}

			// Join thread
			m_thread.reset();
		}

		clear_queues();
		has_error = false;
		m_flush = false;
		m_paused = false;
		m_running = false;
	}

	void video_encoder::resume()
	{
		media_log.notice("video_encoder: Resuming video encoder");

		m_flush = false;
		m_paused = false;
	}

	void video_encoder::encode()
	{
		if (m_running)
		{
			// Resume
			resume();
			media_log.success("video_encoder: resuming recording of '%s'", m_path);
			return;
		}

		m_last_audio_pts = 0;
		m_last_video_pts = 0;

		stop();

		if (const std::string dir = fs::get_parent_dir(m_path); !fs::is_dir(dir))
		{
			media_log.error("video_encoder: Could not find directory: '%s' for file '%s'", dir, m_path);
			has_error = true;
			return;
		}

		// The audio is encoded at 48 kHz whatever cellRec asks for, as FFmpeg
		// chose for each of its formats; the samples come at that rate
		m_sample_rate = 48000;

		media_log.success("video_encoder: Starting recording of '%s'", m_path);

		m_thread = std::make_unique<named_thread<std::function<void()>>>("Video Encode Thread", [this, path = m_path]()
		{
			m_running = true;

			me_params params{};
			params.path = path.c_str();
			params.video_codec = m_video_codec;
			params.width = m_out_format.width;
			params.height = m_out_format.height;
			params.fps = m_framerate;
			params.video_bitrate = m_video_bitrate_bps;
			params.gop = static_cast<unsigned>(m_gop_size);
			params.audio_codec = m_audio_codec;
			params.sample_rate = m_sample_rate;
			params.channels = m_channels;
			params.audio_bitrate = m_audio_bitrate_bps;
			params.max_seconds = max_seconds;

			media_log.notice("video_encoder: video codec %d at %dx%d, %d fps, %d bit/s, gop %d; audio codec %d, %d Hz, %d channels, %d bit/s",
				m_video_codec, m_out_format.width, m_out_format.height, m_framerate, m_video_bitrate_bps, m_gop_size,
				m_audio_codec, m_sample_rate, m_channels, m_audio_bitrate_bps);

			me_encoder* const enc = me_open(&params);

			if (!enc)
			{
				media_log.error("video_encoder: Could not start '%s'", path);
				has_error = true;
				m_flush = false;
				m_running = false;
				return;
			}

			s64 last_video_pts = -1;
			u64 audio_written = 0; // frames, silence included

			// Its buffer goes back to the producer with the next frame
			encoder_frame frame_data;

			while ((thread_ctrl::state() != thread_state::aborting || m_flush) && !has_error)
			{
				// Fetch video frame
				const bool got_frame = pop_frame(frame_data);

				if (got_frame)
				{
					// Calculate presentation timestamp.
					const s64 pts = get_pts(frame_data.timestamp_ms);

					// We need to skip this frame if it has the same timestamp.
					if (pts <= last_video_pts)
					{
						media_log.trace("video_encoder: skipping frame. last_pts=%d, pts=%d, timestamp_ms=%d", last_video_pts, pts, frame_data.timestamp_ms);
					}
					else
					{
						const u32 pitch = frame_data.pitch ? frame_data.pitch : frame_data.width * 4;

						if (!me_video(enc, frame_data.data.data(), pitch, frame_data.width, frame_data.height, frame_data.pixel_format, pts))
						{
							media_log.error("video_encoder: Could not write a frame (%dx%d, format %d)", frame_data.width, frame_data.height, frame_data.pixel_format);
							has_error = true;
							break;
						}

						last_video_pts = pts;
						m_last_video_pts = pts;
					}
				}

				// Fetch audio sample
				const encoder_sample* const sample = peek_sample();
				const bool got_sample = sample != nullptr;

				if (got_sample)
				{
					if (sample->channels != m_channels)
					{
						media_log.error("video_encoder: Audio sample channel count %d does not match the recording's %d", sample->channels, m_channels);
						has_error = true;
						break;
					}

					// Where this block starts, in frames
					const s64 pts = get_audio_pts(sample->timestamp_us);
					const u64 start = static_cast<u64>(std::max<s64>(pts, 0)) * m_samples_per_block;

					// We need to skip this block if its time is already written.
					if (start < audio_written)
					{
						media_log.trace("video_encoder: skipping sample. written=%d, pts=%d, timestamp_us=%d", audio_written, pts, sample->timestamp_us);
					}
					else
					{
						// Silence to fill the gap, then the samples
						if ((start > audio_written && !me_audio(enc, nullptr, start - audio_written)) ||
							!me_audio(enc, sample->data.data(), sample->sample_count))
						{
							media_log.error("video_encoder: Could not write audio");
							has_error = true;
							break;
						}

						audio_written = start + sample->sample_count;
						m_last_audio_pts = static_cast<s64>(audio_written / m_samples_per_block);
					}
					release_sample();
				}


				if (!got_frame && !got_sample)
				{
					if (m_flush)
					{
						m_flush = false;

						if (!m_paused)
						{
							// We only stop the thread after a flush if we are not paused
							break;
						}
					}

					// We only actually pause after we process all frames
					const u64 sleeptime_us = m_paused ? 10000 : 1;
					thread_ctrl::wait_for(sleeptime_us);
					continue;
				}
			}

			if (!me_close(enc))
			{
				media_log.error("video_encoder: Could not finish '%s'", path);
			}

			m_flush = false;
			m_running = false;
		});
	}
}
