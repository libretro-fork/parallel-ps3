#pragma once

#ifdef HAVE_X11
#include <X11/Xutil.h>
#endif

#include "../../display.h"
#include "../VulkanAPI.h"
#include "image.h"
#include "util/logs.hpp"

#include "libretro_images.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vk
{
	struct swapchain_image_WSI
	{
		VkImage value = VK_NULL_HANDLE;
	};

	class swapchain_image_RPCS3 : public image
	{
		std::unique_ptr<buffer> m_dma_buffer;
		u32 m_width = 0;
		u32 m_height = 0;

	public:
		swapchain_image_RPCS3(render_device& dev, const memory_type_mapping& memory_map, u32 width, u32 height);

		void do_dma_transfer(command_buffer& cmd);

		u32 get_required_memory_size() const;

		void* get_pixels();

		void free_pixels();
	};

	class swapchain_base
	{
	protected:
		render_device dev;

		display_handle_t window_handle{};
		u32 m_width = 0;
		u32 m_height = 0;
		VkFormat m_surface_format = VK_FORMAT_B8G8R8A8_UNORM;

		virtual void init_swapchain_images(render_device& dev, u32 count) = 0;

	public:
		swapchain_base(physical_device& gpu, u32 present_queue, u32 graphics_queue, u32 transfer_queue, VkFormat format = VK_FORMAT_B8G8R8A8_UNORM);

		virtual ~swapchain_base() = default;

		virtual void create(display_handle_t& handle) = 0;
		virtual void destroy(bool full = true) = 0;
		virtual bool init() = 0;

		virtual u32 get_swap_image_count() const = 0;
		virtual VkImage get_image(u32 index) = 0;
		virtual VkResult acquire_next_swapchain_image(VkSemaphore semaphore, u64 timeout, u32* result) = 0;
		virtual void end_frame(command_buffer& cmd, u32 index) = 0;
		virtual VkResult present(VkSemaphore semaphore, u32 index) = 0;
		virtual VkImageLayout get_optimal_present_layout() const = 0;

		virtual bool supports_automatic_wm_reports() const
		{
			return false;
		}

		bool init(u32 w, u32 h)
		{
			m_width = w;
			m_height = h;
			return init();
		}

		const vk::render_device& get_device()
		{
			return dev;
		}

		VkFormat get_surface_format() const
		{
			return m_surface_format;
		}

		bool is_headless() const
		{
			return (dev.get_present_queue() == VK_NULL_HANDLE);
		}
	};

	template<typename T>
	class abstract_swapchain_impl : public swapchain_base
	{
	protected:
		std::vector<T> swapchain_images;

	public:
		abstract_swapchain_impl(physical_device& gpu, u32 present_queue, u32 graphics_queue, u32 transfer_queue, VkFormat format = VK_FORMAT_B8G8R8A8_UNORM)
			: swapchain_base(gpu, present_queue, graphics_queue, transfer_queue, format)
		{}

		~abstract_swapchain_impl() override = default;

		u32 get_swap_image_count() const override
		{
			return ::size32(swapchain_images);
		}

		using swapchain_base::init;
	};

	using WSI_swapchain_base = abstract_swapchain_impl<swapchain_image_WSI>;

	class native_swapchain_base : public abstract_swapchain_impl<std::pair<bool, std::unique_ptr<swapchain_image_RPCS3>>>
	{
	public:
		using abstract_swapchain_impl::abstract_swapchain_impl;

		VkResult acquire_next_swapchain_image(VkSemaphore semaphore, u64 timeout, u32* result) override;

		// Clients must implement these methods to render without WSI support
		bool init() override
		{
			fmt::throw_exception("Native swapchain is not implemented yet!");
		}

		void create(display_handle_t& /*window_handle*/) override
		{
			fmt::throw_exception("Native swapchain is not implemented yet!");
		}

		void destroy(bool /*full*/ = true) override
		{
			fmt::throw_exception("Native swapchain is not implemented yet!");
		}

		VkResult present(VkSemaphore /*semaphore*/, u32 /*index*/) override
		{
			fmt::throw_exception("Native swapchain is not implemented yet!");
		}

		// Generic accessors
		void end_frame(command_buffer& cmd, u32 index) override
		{
			swapchain_images[index].second->do_dma_transfer(cmd);
		}

		VkImage get_image(u32 index) override
		{
			return swapchain_images[index].second->value;
		}

		VkImageLayout get_optimal_present_layout() const override
		{
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

	protected:
		void init_swapchain_images(render_device& dev, u32 preferred_count) override;
	};

	// The swapchain of the libretro core. native_swapchain_base does the work
	// already - device-local images the frame is drawn into - but leaves
	// creation and presentation to whoever needs them, throwing "not
	// implemented yet" in the meantime. This is that implementation, for both
	// ways the core gets a frame to the frontend:
	//
	// - Without a hardware context there is nothing to create, and presenting
	//   happens elsewhere, in the readback in VKPresent that hands the pixels to
	//   GSFrameBase::present_frame. An image is free again once presented.
	//
	// - With the hardware Vulkan context (vk::libretro::hw_present) the image
	//   itself goes to the frontend, which samples it in its own frame. It is
	//   held from the flip until the core gives it back (release_image), which
	//   it does once the frontend is done with it - so the images are taken
	//   and returned on different threads.
	//
	// The threads share it without a lock (libretro_images.h): the images of
	// a size are a set, made and published by the renderer's thread, with
	// which are held as retro_atomics in it; retro_run reads the published
	// set and gives images back. A set replaced stays whole until retro_run
	// frees it.
	class swapchain_LIBRETRO : public native_swapchain_base
	{
		struct hw_entry
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageViewCreateInfo view_info{};
		};

		// The images of one size, and which are held
		struct image_set
		{
			lr_retired node{};
			lr_image_slots slots{};
			u32 width = 0;
			u32 height = 0;
			hw_entry entries[LR_MAX_IMAGES]{};
			std::vector<std::unique_ptr<image_view>> views;
			// Once retired: the images themselves, moved out of
			// swapchain_images
			std::vector<std::unique_ptr<swapchain_image_RPCS3>> images;

			~image_set()
			{
				// Views before the images they look at
				views.clear();
				images.clear();
			}

			static void free(void* owner)
			{
				delete static_cast<image_set*>(owner);
			}
		};

		bool m_hw = false;

		// The renderer's thread's: the set it draws into
		std::unique_ptr<image_set> m_current;

		// The set retro_run reads, and whether it is reading it now (a full
		// destroy waits for that)
		retro_atomic_ptr_t m_published;
		retro_atomic_int_t m_readers;

		// Images replaced while the frontend may still show one of them. Until
		// it has a newer frame, RetroArch shows the last image it was given
		// again on every frame without one - and the emulator does not draw any
		// while it restarts (a multi-game disc going into the chosen game) or
		// remakes its images (a resolution change). Destroyed there, the next
		// of those frames read a freed image (NNshi: NVIDIA's driver crashed
		// in retro_run). They go once the core has moved the frontend on.
		retro_atomic_ptr_t m_retired;

		image_set* published() const
		{
			return static_cast<image_set*>(retro_atomic_load_acquire_ptr(const_cast<retro_atomic_ptr_t*>(&m_published)));
		}

		// The renderer's thread: the current set out of use, kept for the
		// frontend while it may show one of its images
		void retire_images()
		{
			retro_atomic_store_release_ptr(&m_published, nullptr);
			while (retro_atomic_load_acquire_int(&m_readers) != 0)
				std::this_thread::yield();

			if (m_current && m_hw)
			{
				for (auto& image : swapchain_images)
					if (image.second)
						m_current->images.emplace_back(std::move(image.second));
				image_set* set = m_current.release();
				set->node.free_cb = &image_set::free;
				set->node.owner = set;
				lr_retired_push(&m_retired, &set->node);
			}
			m_current.reset();
			swapchain_images.clear();
		}

	public:
		swapchain_LIBRETRO(physical_device& gpu, u32 present_queue, u32 graphics_queue, u32 transfer_queue, bool hw = false)
			: native_swapchain_base(gpu, present_queue, graphics_queue, transfer_queue), m_hw(hw)
		{
			retro_atomic_ptr_init(&m_published, nullptr);
			retro_atomic_int_init(&m_readers, 0);
			retro_atomic_ptr_init(&m_retired, nullptr);
		}

		~swapchain_LIBRETRO() override
		{
			retire_images();
			lr_retired_free_all(&m_retired);
		}

		bool init() override
		{
			if (!m_width || !m_height)
			{
				rsx_log.error("Libretro swapchain asked for a %dx%d surface", m_width, m_height);
				return false;
			}

			retire_images();

			// Without a context two is enough to keep one in flight while the
			// other is read. With one, the frontend holds the image it shows
			// and the two or three it is still sampling, up to two finished
			// frames wait for it (vk::libretro::frame_ready), and the renderer
			// draws into the rest.
			init_swapchain_images(dev, m_hw ? LR_MAX_IMAGES : 2);

			auto set = std::make_unique<image_set>();
			lr_image_slots_init(&set->slots, ::size32(swapchain_images));
			set->width = m_width;
			set->height = m_height;

			if (m_hw)
			{
				for (u32 i = 0; i < swapchain_images.size() && i < LR_MAX_IMAGES; i++)
				{
					auto& image = swapchain_images[i];
					set->views.emplace_back(std::make_unique<image_view>(dev, image.second->value, VK_IMAGE_VIEW_TYPE_2D, image.second->format(),
						VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
						VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }));
					set->entries[i].image = image.second->value;
					set->entries[i].view = set->views.back()->value;
					set->entries[i].view_info = set->views.back()->info;
				}
			}

			m_current = std::move(set);
			retro_atomic_store_release_ptr(&m_published, m_current.get());
			return true;
		}

		void create(display_handle_t&) override
		{
			// No window to attach to.
		}

		void destroy(bool full = true) override
		{
			retire_images();
			// Torn down for good: the frontend is past them by then.
			if (full)
				lr_retired_free_all(&m_retired);

			// The device belongs to the swapchain, as with every other one.
			// Left alive, VKGSRender destroyed the instance under it and
			// g_render_device kept pointing at it, and NVIDIA's driver then
			// crashed on a thread of its own at the same address every time
			// the emulator stopped: on unloading the core, and when a
			// multi-game disc restarts into the chosen game (NNshi). With the
			// hardware context the VkDevice is the frontend's: render_device
			// frees what RPCS3 made on it and leaves the handle to the frontend.
			if (full)
				dev.destroy();
		}

		VkResult acquire_next_swapchain_image(VkSemaphore /*semaphore*/, u64 /*timeout*/, u32* result) override
		{
			if (!m_current)
				return VK_NOT_READY;
			const int index = lr_image_slots_acquire(&m_current->slots);
			if (index < 0)
				return VK_NOT_READY;
			*result = static_cast<u32>(index);
			return VK_SUCCESS;
		}

		VkResult present(VkSemaphore /*semaphore*/, u32 index) override
		{
			// Without a context the frame has already left through
			// present_frame; all that remains is to put the image back in
			// circulation, which is what acquire_next_swapchain_image looks at.
			// With one it is on its way to the frontend and stays taken.
			if (!m_hw)
				release_image(index);
			return VK_SUCCESS;
		}

		VkImageLayout get_optimal_present_layout() const override
		{
			// GENERAL is the one layout the frontend may not transition, so the
			// layout the renderer tracks stays true while the frontend reads.
			return m_hw ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		// Either thread: image `index` of the current set free again
		void release_image(u32 index)
		{
			retro_atomic_fetch_add_int(&m_readers, 1);
			if (image_set* set = published())
				lr_image_slots_release(&set->slots, index);
			retro_atomic_fetch_sub_int(&m_readers, 1);
		}

		struct hw_image
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageViewCreateInfo view_info{};
			u32 width = 0;
			u32 height = 0;
		};

		// retro_run: what the frontend needs of image `index` for set_image
		bool get_hw_image(u32 index, hw_image& out) const
		{
			auto& readers = const_cast<retro_atomic_int_t&>(m_readers);
			retro_atomic_fetch_add_int(&readers, 1);
			const image_set* set = published();
			const bool ok = m_hw && set && index < set->slots.count && set->entries[index].view;
			if (ok)
			{
				out.image = set->entries[index].image;
				out.view = set->entries[index].view;
				out.view_info = set->entries[index].view_info;
				out.width = set->width;
				out.height = set->height;
			}
			retro_atomic_fetch_sub_int(&readers, 1);
			return ok;
		}

		// For the flip's wait on a free image (the renderer's thread): which
		// images are held, and a way out when nothing is ever going to give
		// one back
		std::string describe_images() const
		{
			std::string result;
			if (m_current)
				for (u32 i = 0; i < m_current->slots.count; i++)
					result += lr_image_slots_held(&m_current->slots, i) ? 'H' : '-';
			return result;
		}

		// retro_run: once the frontend shows an image made after the retired
		// ones, and has stopped sampling the ones it showed before
		void free_retired()
		{
			lr_retired_free_all(&m_retired);
		}

		bool has_retired() const
		{
			return lr_retired_any(const_cast<retro_atomic_ptr_t*>(&m_retired));
		}

		void release_all_images()
		{
			if (m_current)
				lr_image_slots_release_all(&m_current->slots);
		}
	};

	class swapchain_WSI : public WSI_swapchain_base
	{
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;
		VkColorSpaceKHR m_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkSwapchainKHR m_vk_swapchain = nullptr;

		PFN_vkCreateSwapchainKHR _vkCreateSwapchainKHR = nullptr;
		PFN_vkDestroySwapchainKHR _vkDestroySwapchainKHR = nullptr;
		PFN_vkGetSwapchainImagesKHR _vkGetSwapchainImagesKHR = nullptr;
		PFN_vkAcquireNextImageKHR _vkAcquireNextImageKHR = nullptr;
		PFN_vkQueuePresentKHR _vkQueuePresentKHR = nullptr;

		bool m_wm_reports_flag = false;

	protected:
		void init_swapchain_images(render_device& dev, u32 preferred_count = 0) override;

	public:
		swapchain_WSI(vk::physical_device& gpu, u32 present_queue, u32 graphics_queue, u32 transfer_queue, VkFormat format, VkSurfaceKHR surface, VkColorSpaceKHR color_space, bool force_wm_reporting_off);

		~swapchain_WSI() override = default;

		void create(display_handle_t&) override
		{}

		void destroy(bool = true) override;

		std::pair<VkSurfaceCapabilitiesKHR, bool> init_surface_capabilities();

		using WSI_swapchain_base::init;
		bool init() override;

		bool supports_automatic_wm_reports() const override
		{
			return m_wm_reports_flag;
		}

		VkResult acquire_next_swapchain_image(VkSemaphore semaphore, u64 timeout, u32* result) override
		{
			return vkAcquireNextImageKHR(dev, m_vk_swapchain, timeout, semaphore, VK_NULL_HANDLE, result);
		}

		void end_frame(command_buffer& /*cmd*/, u32 /*index*/) override
		{}

		VkResult present(VkSemaphore semaphore, u32 image) override;

		VkImage get_image(u32 index) override
		{
			return swapchain_images[index].value;
		}

		VkImageLayout get_optimal_present_layout() const override
		{
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
	};

	struct WSI_config
	{
		bool supports_automatic_wm_reports = true;
	};
}
