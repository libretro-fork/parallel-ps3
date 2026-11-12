#pragma once

#include <util/types.hpp>
#include <util/logs.hpp>
#include <deque>
#include <unordered_map>
#include <queues/retro_deque.h>

template <typename T>
class named_thread;

namespace rsx
{
	enum class surface_antialiasing : u8;

	struct surface_scaling_config_t;

	struct framebuffer_dimensions_t
	{
		u16 width;
		u16 height;
		u8 samples_x;
		u8 samples_y;

		inline u32 samples_total() const
		{
			return static_cast<u32>(width) * height * samples_x * samples_y;
		}

		inline bool operator > (const framebuffer_dimensions_t& that) const
		{
			return samples_total() > that.samples_total();
		}

		std::string to_string(bool skip_aa_suffix = false) const;

		static framebuffer_dimensions_t make(u16 width, u16 height, rsx::surface_antialiasing aa);
	};

	struct framebuffer_statistics_t
	{
		std::unordered_map<rsx::surface_antialiasing, framebuffer_dimensions_t> data;

		// Replace the existing data with this input if it is greater than what is already known
		void add(u16 width, u16 height, rsx::surface_antialiasing aa);

		// Returns a formatted string representing the statistics collected over the frame.
		std::string to_string(const surface_scaling_config_t& scaling_config, bool squash) const;
	};

	struct frame_statistics_t
	{
		u32 draw_calls;
		u32 submit_count;

		s64 setup_time;
		s64 vertex_upload_time;
		s64 textures_upload_time;
		s64 draw_exec_time;
		s64 flip_time;

		u32 vertex_cache_request_count;
		u32 vertex_cache_miss_count;

		u32 program_cache_lookups_total;
		u32 program_cache_lookups_ellided;

		framebuffer_statistics_t framebuffer_stats;
	};

	struct frame_time_t
	{
		u64 preempt_count;
		u64 timestamp;
		u64 tsc;
	};

	struct display_flip_info_t
	{
		u32 buffer;
		bool skip_frame;
		bool emu_flip;
		bool in_progress;
		frame_statistics_t stats;
	};

	// The buffers of the flips queued and not yet shown, in order (plain
	// data: rsx::thread makes it and frees it)
	RETRO_DEQUE_DEFINE(flip_buffers, u32);

	inline void flip_buffers_queue(flip_buffers_t& queue, u32 buffer)
	{
		ensure(flip_buffers_push_back_growing(&queue, &buffer));
	}

	// Takes the queued flips up to and including @buffer's; false if it was not queued
	inline bool flip_buffers_take(flip_buffers_t& queue, display_flip_info_t& info, u32 buffer)
	{
		if (flip_buffers_empty(&queue))
		{
			return false;
		}

		do
		{
			const u32 index = *flip_buffers_front(&queue);
			flip_buffers_pop_front(&queue);

			if (index == buffer)
			{
				info.buffer = buffer;
				return true;
			}
		} while (!flip_buffers_empty(&queue));

		// Need to observe this happening in the wild
		rsx_log.error("Display queue was discarded while not empty!");
		return false;
	}

	class vblank_thread
	{
		std::shared_ptr<named_thread<std::function<void()>>> m_thread;

	public:
		vblank_thread() = default;
		vblank_thread(const vblank_thread&) = delete;

		void set_thread(std::shared_ptr<named_thread<std::function<void()>>> thread);

		vblank_thread& operator=(thread_state);
		vblank_thread& operator=(const vblank_thread&) = delete;
	};
}
