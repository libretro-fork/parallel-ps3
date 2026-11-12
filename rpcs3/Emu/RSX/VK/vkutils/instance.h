#pragma once

#include "../VulkanAPI.h"
#include "swapchain.h"

#include <algorithm>
#include <vector>

#ifdef __APPLE__
#include <MoltenVK/mvk_vulkan.h>
#include <MoltenVK/mvk_private_api.h>
#endif

namespace vk
{
	class supported_extensions
	{
	private:
		std::vector<VkExtensionProperties> m_vk_exts;

	public:
		enum enumeration_class
		{
			instance = 0,
			device   = 1
		};

		supported_extensions(enumeration_class _class, const char* layer_name = nullptr, VkPhysicalDevice pdev = VK_NULL_HANDLE);

		bool is_supported(std::string_view ext) const;
	};

	class instance
	{
	private:
		std::vector<physical_device> gpus;
		VkInstance m_instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;

		PFN_vkDestroyDebugReportCallbackEXT _vkDestroyDebugReportCallback = nullptr;
		PFN_vkCreateDebugReportCallbackEXT _vkCreateDebugReportCallback = nullptr;
		VkDebugReportCallbackEXT m_debugger = nullptr;

		bool extensions_loaded = false;
		// False for an instance someone else created and destroys - the
		// libretro frontend's, with the core's hardware Vulkan context.
		bool m_owned = true;
#ifdef LIBRETRO_CORE
		const VkApplicationInfo* m_wrap_app = nullptr;
		VkInstance (*m_wrapper)(void* opaque, const VkInstanceCreateInfo* info) = nullptr;
		void* m_wrapper_opaque = nullptr;
#endif

	public:

		instance() = default;

		~instance();

		void destroy();

		void enable_debugging();

		bool create(const char* app_name, bool fast = false);
#ifdef LIBRETRO_CORE
		// As create, but made by the frontend's wrapper (context negotiation
		// v2) for the application the core named, and the frontend's to
		// destroy. vk::load_vulkan(get_instance_proc_addr) comes first.
		VkInstance handle() const { return m_instance; }
		bool create_wrapped(const VkApplicationInfo* app, VkInstance (*wrapper)(void* opaque, const VkInstanceCreateInfo* info), void* opaque);
#endif

		// Use an instance created elsewhere. It is not destroyed with this one.
		void adopt(VkInstance handle);

		void bind();

		std::vector<physical_device>& enumerate_devices();

		swapchain_base* create_swapchain(display_handle_t window_handle, vk::physical_device& dev);
	};
}
