#pragma once
#include "Emu/RSX/VK/vkutils/sync.h"
#include "vkutils/garbage_collector.h"
#include "vkutils/query_pool.hpp"
#include "vkutils/sampler.h"

#include "Utilities/mutex.h"
#include "Utilities/lf_item_pool.h"

#include <retro_atomic.h>

#include <memory>

namespace vk
{
	u64 get_event_id();
	u64 current_event_id();
	u64 last_completed_event_id();
	void on_event_completed(u64 event_id);

	class resource_manager : public garbage_collector
	{
	private:
		sampler_pool_t m_sampler_pool;

		// What is to be deleted once the GPU is done with it, with no lock.
		//
		// Any thread disposes of an object: a record with the event the object
		// was in use for is pushed on m_incoming with a compare-and-swap. The
		// driver thread, told that an event has completed, takes everything
		// pushed in one exchange, puts it after what it kept from last time
		// (m_pending, oldest first) and deletes, in the order they were
		// disposed of, the objects whose event has completed. Records come
		// from and go back to a pool, so that disposing of an object allocates
		// nothing once the pool has filled.
		struct disposal_t
		{
			disposal_t* next;
			u64 eid;
			void* object;
			void (*deleter)(void*);
			const gpu_debug_marker* marker; // The object, when it is one of those
		};

		retro_atomic_ptr_t m_incoming{}; // Newest first
		retro_atomic_ptr_t m_pending{};  // Oldest first; the driver thread's, but for a dump of the markers
		lf_item_pool m_spare_disposals{};
		static constexpr usz max_spare_disposals = 4096;

		// Events whose objects are to outlive them by one event: eid by eid % size
		static constexpr usz held_events = 4096;
		retro_atomic_64_t m_held_events[held_events]{};

		std::vector<std::function<void()>> m_exit_handlers;

		void push(void* object, void (*deleter)(void*), const gpu_debug_marker* marker)
		{
			auto record = static_cast<disposal_t*>(lf_item_pool_take(&m_spare_disposals, 0, 0));
			if (!record)
			{
				record = new disposal_t;
			}

			record->eid = current_event_id();
			record->object = object;
			record->deleter = deleter;
			record->marker = marker;

			while (true)
			{
				void* head = retro_atomic_load_acquire_ptr(&m_incoming);
				record->next = static_cast<disposal_t*>(head);

				if (retro_atomic_cas_ptr(&m_incoming, head, record))
				{
					return;
				}
			}
		}

		// Everything pushed since the last call, oldest first
		disposal_t* take_incoming()
		{
			auto taken = static_cast<disposal_t*>(retro_atomic_exchange_ptr(&m_incoming, nullptr));
			disposal_t* oldest_first = nullptr;

			while (taken)
			{
				disposal_t* next = taken->next;
				taken->next = oldest_first;
				oldest_first = taken;
				taken = next;
			}

			return oldest_first;
		}

		void destroy_disposal(disposal_t* record)
		{
			record->deleter(record->object);

			if (!lf_item_pool_put(&m_spare_disposals, 0, 0, 0, record))
			{
				delete record;
			}
		}

		bool is_held(u64 eid) const
		{
			return static_cast<u64>(retro_atomic_load_acquire_64(const_cast<retro_atomic_64_t*>(&m_held_events[eid % held_events]))) == eid + 1;
		}

		// Deletes what is due - everything when @all - and keeps the rest in m_pending
		void collect(u64 completed_eid, bool all)
		{
			// What was kept, then what came since: oldest first
			auto kept = static_cast<disposal_t*>(retro_atomic_exchange_ptr(&m_pending, nullptr));
			disposal_t* taken = take_incoming();

			while (true)
			{
				disposal_t* head = nullptr;
				disposal_t** tail = &head;

				for (disposal_t* list : { kept, taken })
				{
					while (list)
					{
						disposal_t* next = list->next;

						// An event that is held keeps its objects until the next one is done
						if (all || list->eid + (is_held(list->eid) ? 1 : 0) <= completed_eid)
						{
							destroy_disposal(list);
						}
						else
						{
							list->next = nullptr;
							*tail = list;
							tail = &list->next;
						}

						list = next;
					}
				}

				if (!head || retro_atomic_cas_ptr(&m_pending, nullptr, head))
				{
					return;
				}

				// A dump of the markers borrowed the kept list and put it
				// back meanwhile: it goes in front of what is kept now
				kept = static_cast<disposal_t*>(retro_atomic_exchange_ptr(&m_pending, nullptr));
				taken = head;
			}
		}

	public:

		resource_manager()
		{
			// Without the pool every record is allocated and freed
			lf_item_pool_init(&m_spare_disposals, max_spare_disposals);
		}
		~resource_manager()
		{
			collect(0, true);

			while (void* spare = lf_item_pool_take_any(&m_spare_disposals))
			{
				delete static_cast<disposal_t*>(spare);
			}

			lf_item_pool_free(&m_spare_disposals);
		}

		void destroy()
		{
			flush();

			// Run the on-exit callbacks
			for (const auto& callback : m_exit_handlers)
			{
				callback();
			}
		}

		void flush()
		{
			collect(0, true);

			m_sampler_pool.clear();
		}

		vk::sampler* get_sampler(const vk::render_device& dev, vk::sampler* previous,
			VkSamplerAddressMode clamp_u, VkSamplerAddressMode clamp_v, VkSamplerAddressMode clamp_w,
			VkBool32 unnormalized_coordinates, float mipLodBias, float max_anisotropy, float min_lod, float max_lod,
			VkFilter min_filter, VkFilter mag_filter, VkSamplerMipmapMode mipmap_mode, const vk::border_color_t& border_color,
			VkBool32 depth_compare = VK_FALSE, VkCompareOp depth_compare_mode = VK_COMPARE_OP_NEVER)
		{
			const auto key = m_sampler_pool.compute_storage_key(
				clamp_u, clamp_v, clamp_w,
				unnormalized_coordinates, mipLodBias, max_anisotropy, min_lod, max_lod,
				min_filter, mag_filter, mipmap_mode, border_color,
				depth_compare, depth_compare_mode);

			if (previous)
			{
				auto as_cached_object = static_cast<cached_sampler_object_t*>(previous);
				ensure(as_cached_object->has_refs());
				as_cached_object->release();
			}

			if (const auto found = m_sampler_pool.find(key))
			{
				found->add_ref();
				return found;
			}

			auto result = std::make_unique<cached_sampler_object_t>(
				dev, clamp_u, clamp_v, clamp_w, unnormalized_coordinates,
				mipLodBias, max_anisotropy, min_lod, max_lod,
				min_filter, mag_filter, mipmap_mode, border_color,
				depth_compare, depth_compare_mode);

			auto ret = m_sampler_pool.emplace(key, result);
			ret->add_ref();
			return ret;
		}

		void add_exit_callback(std::function<void()> callback) override
		{
			m_exit_handlers.push_back(callback);
		}

		void dispose(vk::disposable_t& disposable) override
		{
			void (*deleter)(void*) = nullptr;
			if (void* object = disposable.take(deleter))
			{
				push(object, deleter, nullptr);
			}
		}

		inline void dispose(std::unique_ptr<vk::gpu_debug_marker>& object)
		{
			// Special case as we may need to read these out.
			// FIXME: We can manage these markers better and remove this exception.
			if (auto marker = object.release())
			{
				push(marker, [](void* raw) { delete static_cast<vk::gpu_debug_marker*>(raw); }, marker);
			}
		}

		template<typename T>
		inline void dispose(std::unique_ptr<T>& object)
		{
			auto ptr = vk::disposable_t::make(object.release());
			dispose(ptr);
		}

		// What has been disposed of in the current event outlives it by one
		// event (as does what is disposed of in it from here on)
		void push_down_current_scope()
		{
			const u64 eid = current_event_id();
			retro_atomic_store_release_64(&m_held_events[eid % held_events], eid + 1);
		}

		void eid_completed(u64 eid)
		{
			collect(eid, false);
		}

		void trim();

		// For the dump when the device is lost. The lists are borrowed while
		// they are read, so that the driver thread cannot delete a marker
		// that is being looked at; they are put back as they were.
		std::vector<const gpu_debug_marker*> gather_debug_markers()
		{
			std::vector<const gpu_debug_marker*> result;

			auto kept = static_cast<disposal_t*>(retro_atomic_exchange_ptr(&m_pending, nullptr));
			disposal_t* taken = take_incoming();

			for (disposal_t* list : { kept, taken })
			{
				for (; list; list = list->next)
				{
					if (list->marker)
					{
						result.push_back(list->marker);
					}
				}
			}

			// One list, oldest first, back where the driver thread looks
			// first. If it has kept something of its own meanwhile, this
			// goes in front: collect() does that when its own put fails,
			// so here the same is done from this side.
			disposal_t* head = kept ? kept : taken;
			if (kept && taken)
			{
				disposal_t* last = kept;
				while (last->next) last = last->next;
				last->next = taken;
			}

			while (head && !retro_atomic_cas_ptr(&m_pending, nullptr, head))
			{
				if (auto newer = static_cast<disposal_t*>(retro_atomic_exchange_ptr(&m_pending, nullptr)))
				{
					disposal_t* last = head;
					while (last->next) last = last->next;
					last->next = newer;
				}
			}

			return result;
		}
	};

	struct vmm_allocation_t
	{
		u64 size;
		u32 type_index;
		vmm_allocation_pool pool;
	};

	resource_manager* get_resource_manager();
}
