#pragma once

#include "overlays.h"
#include <cstddef>
#include <retro_atomic.h>
#include "overlay_audio.h"

#include "Emu/IdManager.h"
#include <queues/retro_retire.h>
#include "Utilities/Thread.h"
#include "Utilities/lockless.h"

namespace rsx
{
	namespace overlays
	{
		struct overlay;

		class display_manager
		{
		private:
			retro_atomic_int_t m_uid_ctr{0};

			// The overlays shown, and those taken off whose resources the
			// renderer has yet to free: published whole, with no lock. A
			// change is made to a copy, which is published by compare-and-swap
			// (made again from the newer one if another change got in first);
			// the one replaced goes once no reader is in (retro_retire). The
			// renderer reads them as it draws, never waiting on a change
		public:
			struct lists_t
			{
				mpsc_stack_node_t node{}; // first: retired as it is
				std::vector<std::shared_ptr<overlay>> views;
				std::vector<std::shared_ptr<overlay>> dirty;
			};

		private:
			mutable retro_atomic_ptr_t m_lists{}; // lists_t*, never null
			mutable retro_retire_t m_retire{};

			static void free_lists(mpsc_stack_node_t* node)
			{
				// The node is lists_t's first member: the same address,
				// taken back through void*
				static_assert(offsetof(lists_t, node) == 0);
				delete static_cast<lists_t*>(static_cast<void*>(node));
			}

			// @f(lists_t&) on a copy of the lists published, which is then
			// published in their place
			template <typename F>
			void change(F&& f)
			{
				retro_retire_enter(&m_retire);

				for (;;)
				{
					lists_t* const old = static_cast<lists_t*>(retro_atomic_load_acquire_ptr(&m_lists));
					lists_t* const now = new lists_t(*old);
					f(*now);

					if (retro_atomic_cas_ptr(&m_lists, old, now))
					{
						retro_retire(&m_retire, &old->node, free_lists);
						break;
					}

					delete now;
				}

				retro_retire_leave(&m_retire, free_lists);
			}

			// @f(const lists_t&) on the lists published, with no lock
			template <typename F>
			auto read(F&& f) const -> decltype(f(std::declval<const lists_t&>()))
			{
				retro_retire_enter(&m_retire);
				struct leave_t
				{
					retro_retire_t* r;
					~leave_t() { retro_retire_leave(r, free_lists); }
				} leave{&m_retire};

				return f(*static_cast<const lists_t*>(retro_atomic_load_acquire_ptr(&m_lists)));
			}

			std::unique_ptr<audio_player> m_audio_player;

			// On a copy of the lists: those taken off go to @removed
			static bool remove_type(lists_t& lists, u32 type_id, std::vector<std::shared_ptr<overlay>>& removed);

			static bool remove_uid(lists_t& lists, u32 uid, std::vector<std::shared_ptr<overlay>>& removed);

			void on_overlay_activated(const std::shared_ptr<overlay>& item);

			void on_overlay_removed(const std::shared_ptr<overlay>& item);

		public:
			// Disable default construction to make it conditionally available in g_fxo
			explicit display_manager(int) noexcept;

			~display_manager();

			// Adds an object to the internal list. Optionally removes other objects of the same type.
			// Original handle loses ownership but a usable pointer is returned
			template <typename T>
			std::shared_ptr<T> add(std::shared_ptr<T>& entry, bool remove_existing = true)
			{
				entry->uid = static_cast<u32>(retro_atomic_fetch_add_int(&m_uid_ctr, 1));
				entry->type_index = stx::typeindex<id_manager::typeinfo, T>();

				const std::shared_ptr<overlay> added = std::move(entry);
				bool replaced = false;

				change([&](lists_t& lists)
				{
					replaced = false;

					if (remove_existing)
					{
						for (auto It = lists.views.begin(); It != lists.views.end(); It++)
						{
							if (It->get()->type_index == added->type_index)
							{
								// Replace
								lists.dirty.push_back(std::move(*It));
								*It = added;
								replaced = true;
								return;
							}
						}
					}

					lists.views.push_back(added);
				});

				if (!replaced)
				{
					on_overlay_activated(added);
				}

				return std::static_pointer_cast<T>(added);
			}

			// Allocates object and adds to internal list. Returns pointer to created object
			template <typename T, typename ...Args>
			std::shared_ptr<T> create(Args&&... args)
			{
				auto object = std::make_shared<T>(std::forward<Args>(args)...);
				return add(object);
			}

			// Removes item from list if it matches the uid
			void remove(u32 uid);

			// Removes all objects of this type from the list
			template <typename T>
			void remove()
			{
				const auto type_id = stx::typeindex<id_manager::typeinfo, T>();
				std::vector<std::shared_ptr<overlay>> removed;

				change([&](lists_t& lists)
				{
					removed.clear();
					remove_type(lists, type_id, removed);
				});

				for (const auto& item : removed)
				{
					on_overlay_removed(item);
				}
			}

			// True if any visible elements to draw exist
			bool has_visible() const
			{
				return read([](const lists_t& l) { return !l.views.empty(); });
			}

			// True if any elements have been deleted but their resources may not have been cleaned up
			bool has_dirty() const
			{
				return read([](const lists_t& l) { return !l.dirty.empty(); });
			}

			// The lists published, read while it lives with no lock: those it
			// holds stay alive until it goes, whatever is changed meanwhile
			class reader
			{
				const display_manager& m;
				const lists_t* l;

			public:
				explicit reader(const display_manager& m)
					: m(m)
				{
					retro_retire_enter(&m.m_retire);
					l = static_cast<const lists_t*>(retro_atomic_load_acquire_ptr(&m.m_lists));
				}

				reader(const reader&) = delete;
				reader& operator=(const reader&) = delete;

				~reader()
				{
					retro_retire_leave(&m.m_retire, free_lists);
				}

				const std::vector<std::shared_ptr<overlay>>& views() const { return l->views; }
				const std::vector<std::shared_ptr<overlay>>& dirty() const { return l->dirty; }
			};

			// Deallocate object. Object must first be removed via the remove() functions
			void dispose(const std::vector<u32>& uids);

			// Returns pointer to the object matching the given uid
			std::shared_ptr<overlay> get(u32 uid) const;

			// Returns pointer to the first object matching the given type
			template <typename T>
			std::shared_ptr<T> get() const
			{
				const auto type_id = stx::typeindex<id_manager::typeinfo, T>();

				return read([&](const lists_t& lists) -> std::shared_ptr<T>
				{
					for (const auto& iface : lists.views)
					{
						if (iface->type_index == type_id)
						{
							return std::static_pointer_cast<T>(iface);
						}
					}

					return {};
				});
			}

			// Enable input thread attach to the specified interface
			void attach_thread_input(
				u32 uid,                                                 // The input target
				const std::string_view& name,                            // The name of the target
				std::function<void()> on_input_loop_enter = nullptr,     // [optional] What to do before running the input routine
				std::function<void(s32)> on_input_loop_exit = nullptr,   // [optional] What to do with the result if any
				std::function<s32()> input_loop_override = nullptr);     // [optional] What to do during the input loop. By default calls user_interface::run_input_loop

			void start_audio(const std::string& audio_path);
			void stop_audio();

		private:
			struct overlay_input_thread
			{
				static constexpr auto thread_name = "Overlay Input Thread"sv;
			};

			struct input_thread_context_t
			{
				// Ctor
				input_thread_context_t(
					const std::string_view& name,
					std::shared_ptr<user_interface> iface,
					std::function<void()> on_input_loop_enter,
					std::function<void(s32)> on_input_loop_exit,
					std::function<s32()> input_loop_override)
					: name(name)
					, target(iface)
					, input_loop_prologue(on_input_loop_enter)
					, input_loop_epilogue(on_input_loop_exit)
					, input_loop_override(input_loop_override)
					, prologue_completed(false)
				{}

				// Attributes
				std::string_view name;
				std::shared_ptr<user_interface> target;
				std::function<void()> input_loop_prologue;
				std::function<void(s32)> input_loop_epilogue;
				std::function<s32()> input_loop_override;

				// Runtime stats
				bool prologue_completed;
			};

			// Pushed by any thread; the input thread keeps its own stack of
			// what it has taken, so nothing is pushed back here
			lf_queue<input_thread_context_t> m_input_token_stack;
			retro_atomic_int_t m_input_thread_abort{0}; // 0 or 1
			retro_atomic_int_t m_input_thread_interrupted{0}; // 0 or 1

			std::shared_ptr<named_thread<overlay_input_thread>> m_input_thread;
			void input_thread_loop();
		};
	}
}
