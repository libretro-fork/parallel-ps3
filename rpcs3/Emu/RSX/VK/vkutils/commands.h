#pragma once

#include "../VulkanAPI.h"
#include "device.h"
#include "sync.h"

#include <span>

#include <retro_atomic.h>

namespace vk
{
	class command_pool
	{
		vk::render_device* owner = nullptr;
		VkCommandPool pool       = nullptr;
		u32 queue_family         = 0;

	public:
		command_pool()  = default;
		~command_pool() = default;

		void create(vk::render_device& dev, u32 queue_family);
		void destroy();

		vk::render_device& get_owner() const;
		u32 get_queue_family() const;

		operator VkCommandPool() const;
	};

	struct queue_submit_t
	{
		VkQueue queue = VK_NULL_HANDLE;
		fence* pfence = nullptr;
		VkCommandBuffer commands = VK_NULL_HANDLE;
		std::array<VkSemaphore, 4> wait_semaphores {};
		std::array<VkSemaphore, 4> signal_semaphores {};
		std::array<VkPipelineStageFlags, 4> wait_stages {};
		u32 wait_semaphores_count = 0;
		u32 signal_semaphores_count = 0;

		queue_submit_t() = default;
		queue_submit_t(VkQueue queue_, vk::fence* fence_)
			: queue(queue_), pfence(fence_) {}

		queue_submit_t(const queue_submit_t& other)
		{
			std::memcpy(static_cast<void*>(this), &other, sizeof(queue_submit_t));
		}

		inline queue_submit_t& wait_on(VkSemaphore semaphore, VkPipelineStageFlags stage)
		{
			::at32(wait_semaphores, wait_semaphores_count) = semaphore;
			wait_stages[wait_semaphores_count++] = stage;
			return *this;
		}

		inline queue_submit_t& queue_signal(VkSemaphore semaphore)
		{
			::at32(signal_semaphores, signal_semaphores_count++) = semaphore;
			return *this;
		}
	};

	class command_buffer
	{
	protected:
		bool is_open = false;
		// Submitted and not yet seen to be done. Read by whoever polls the
		// buffer, with no lock (command_buffer_chunk).
		retro_atomic_int_t is_pending{};
		fence* m_submit_fence = nullptr;

		command_pool* pool = nullptr;
		VkCommandBuffer commands = nullptr;

		// State cache
		mutable std::array<VkDescriptorSet, 2> m_bound_descriptor_sets {{ VK_NULL_HANDLE }};
		mutable std::array<VkPipeline, 2> m_bound_pipelines{{ VK_NULL_HANDLE }};

		void clear_state_cache();

	public:
		enum access_type_hint
		{
			flush_only, // Only to be submitted/opened/closed via command flush
			all         // Auxiliary, can be submitted/opened/closed at any time
		}
		access_hint = flush_only;

		enum command_buffer_data_flag : u32
		{
			cb_has_occlusion_task = 0x01,
			cb_has_blit_transfer  = 0x02,
			cb_has_dma_transfer   = 0x04,
			cb_has_open_query     = 0x08,
			cb_load_occluson_task = 0x10,
			cb_has_conditional_render = 0x20,
			cb_reload_dynamic_state   = 0x40
		};
		u32 flags = 0;

	public:
		command_buffer() = default;
		~command_buffer() = default;

		// Copied as it was (a vector of them grows): the pending flag by
		// its value
		command_buffer(const command_buffer& other)
		{
			*this = other;
		}

		command_buffer& operator=(const command_buffer& other)
		{
			is_open = other.is_open;
			retro_atomic_store_release_int(&is_pending, retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t*>(&other.is_pending)));
			m_submit_fence = other.m_submit_fence;
			pool = other.pool;
			commands = other.commands;
			m_bound_descriptor_sets = other.m_bound_descriptor_sets;
			m_bound_pipelines = other.m_bound_pipelines;
			access_hint = other.access_hint;
			flags = other.flags;
			return *this;
		}

		void create(command_pool& cmd_pool);
		void destroy();
		void reset();

		void begin();
		void end();
		void submit(queue_submit_t& submit_info, VkBool32 flush = VK_FALSE);

		// Abstractions with caching
		void bind_pipeline(VkPipeline pipeline, VkPipelineBindPoint bind_point) const;
		void bind_descriptor_sets(const std::span<VkDescriptorSet>& sets, VkPipelineBindPoint bind_point, VkPipelineLayout pipe_layout) const;
		void bind_descriptor_sets(const std::span<VkDescriptorSet>& sets, const std::span<u32>& dynamic_offsets, VkPipelineBindPoint bind_point, VkPipelineLayout pipe_layout) const;

		// Properties
		command_pool& get_command_pool() const { return *pool; }
		u32 get_queue_family() const { return pool->get_queue_family(); }
		bool is_recording() const { return is_open; }

		void clear_flags() { flags = 0; }
		void set_flag(command_buffer_data_flag flag) { flags |= flag; }

		operator VkCommandBuffer() const
		{
			return commands;
		}
	};
}
