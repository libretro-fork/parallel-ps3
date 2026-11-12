#include "stdafx.h"
#include "instance.h"
#include "Emu/RSX/GSFrameBase.h"

#include "Emu/system_config.h"

namespace vk
{
	// Supported extensions
	supported_extensions::supported_extensions(enumeration_class _class, const char* layer_name, VkPhysicalDevice pdev)
	{
		u32 count;
		if (_class == enumeration_class::instance)
		{
			if (vkEnumerateInstanceExtensionProperties(layer_name, &count, nullptr) != VK_SUCCESS)
				return;
		}
		else
		{
			ensure(pdev);
			if (vkEnumerateDeviceExtensionProperties(pdev, layer_name, &count, nullptr) != VK_SUCCESS)
				return;
		}

		m_vk_exts.resize(count);
		if (_class == enumeration_class::instance)
		{
			vkEnumerateInstanceExtensionProperties(layer_name, &count, m_vk_exts.data());
		}
		else
		{
			vkEnumerateDeviceExtensionProperties(pdev, layer_name, &count, m_vk_exts.data());
		}
	}

	bool supported_extensions::is_supported(std::string_view ext) const
	{
		return std::any_of(m_vk_exts.cbegin(), m_vk_exts.cend(), [&](const VkExtensionProperties& p) { return p.extensionName == ext; });
	}

	// Instance
	instance::~instance()
	{
		if (m_instance)
		{
			destroy();
		}
	}

	void instance::destroy()
	{
		if (!m_instance) return;

		if (m_debugger)
		{
			_vkDestroyDebugReportCallback(m_instance, m_debugger, nullptr);
			m_debugger = nullptr;
		}

		if (m_surface)
		{
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			m_surface = VK_NULL_HANDLE;
		}

		if (m_owned)
			vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
		gpus.clear();
	}

	void instance::adopt(VkInstance handle)
	{
		m_instance = handle;
		m_owned = false;
		load_vulkan_instance(handle);
		// The frontend asks for the Vulkan version the core's application
		// info names (1.2), so the physical device queries need no extension.
		extensions_loaded = true;
	}

	void instance::enable_debugging()
	{
		if (!g_cfg.video.debug_output) return;

		PFN_vkDebugReportCallbackEXT callback = vk::dbgFunc;

		_vkCreateDebugReportCallback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT"));
		_vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT"));

		VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
		dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		dbgCreateInfo.pfnCallback = callback;
		dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

		CHECK_RESULT(_vkCreateDebugReportCallback(m_instance, &dbgCreateInfo, NULL, &m_debugger));
	}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
	bool instance::create_wrapped(const VkApplicationInfo* app, VkInstance (*wrapper)(void* opaque, const VkInstanceCreateInfo* info), void* opaque)
	{
		m_wrap_app = app;
		m_wrapper = wrapper;
		m_wrapper_opaque = opaque;
		const bool ok = create(app && app->pApplicationName ? app->pApplicationName : "RPCS3");
		m_wrap_app = nullptr;
		m_wrapper = nullptr;
		m_wrapper_opaque = nullptr;
		return ok;
	}

	bool instance::create(const char* app_name, bool fast)
	{
		// No loader, no driver
		if (!load_vulkan())
		{
			return false;
		}

		// Initialize a vulkan instance
		VkApplicationInfo app = {};

		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.pApplicationName = app_name;
		app.applicationVersion = 0;
		app.pEngineName = app_name;
		app.engineVersion = 0;
		app.apiVersion = VK_API_VERSION_1_2;

		// Set up instance information

		std::vector<const char*> extensions;
		std::vector<const char*> layers;
		const void* next_info = nullptr;

#ifdef __APPLE__
		// Declare MVK variables here to ensure the lifetime within the entire scope
		const VkBool32 setting_true = VK_TRUE;
		const int32_t setting_fast_math = g_cfg.video.disable_msl_fast_math.get() ? MVK_CONFIG_FAST_MATH_NEVER : MVK_CONFIG_FAST_MATH_ON_DEMAND;

		std::vector<VkLayerSettingEXT> mvk_settings;
		VkLayerSettingsCreateInfoEXT mvk_layer_settings_create_info{};
#endif

		if (!fast)
		{
			extensions_loaded = true;
			supported_extensions support(supported_extensions::instance);

			if (support.is_supported(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
				extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

#ifdef __APPLE__
			extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
			if (support.is_supported(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME))
			{
				extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);

				mvk_settings.push_back(VkLayerSettingEXT{ kMVKMoltenVKDriverLayerName, "MVK_CONFIG_RESUME_LOST_DEVICE", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_true });
				mvk_settings.push_back(VkLayerSettingEXT{ kMVKMoltenVKDriverLayerName, "MVK_CONFIG_FAST_MATH_ENABLED", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &setting_fast_math });

				mvk_layer_settings_create_info.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
				mvk_layer_settings_create_info.pNext = next_info;
				mvk_layer_settings_create_info.settingCount = static_cast<uint32_t>(mvk_settings.size());
				mvk_layer_settings_create_info.pSettings = mvk_settings.data();

				next_info = &mvk_layer_settings_create_info;
			}
#endif

			if (g_cfg.video.renderdoc_compatiblity && support.is_supported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}

			// No surface: the frontend presents
			if (g_cfg.video.debug_output)
				layers.push_back("VK_LAYER_KHRONOS_validation");
		}
#ifdef __APPLE__
		// MoltenVK's ICD will not be detected without these extensions enabled.
		else
		{
			extensions_loaded = true;
			extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
		}
#endif

		VkInstanceCreateInfo instance_info = {};
		instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_info.pApplicationInfo = &app;
		instance_info.enabledLayerCount = static_cast<u32>(layers.size());
		instance_info.ppEnabledLayerNames = layers.data();
#ifdef __APPLE__
		instance_info.enabledExtensionCount = static_cast<u32>(extensions.size());
		instance_info.ppEnabledExtensionNames = extensions.data();
#else
		instance_info.enabledExtensionCount = fast ? 0 : static_cast<u32>(extensions.size());
		instance_info.ppEnabledExtensionNames = fast ? nullptr : extensions.data();
#endif
		instance_info.pNext = next_info;
#ifdef __APPLE__
		instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

		if (m_wrapper)
		{
			// The application as the core named it to the frontend
			if (m_wrap_app)
				instance_info.pApplicationInfo = m_wrap_app;

			m_instance = m_wrapper(m_wrapper_opaque, &instance_info);

			if (!m_instance && instance_info.enabledLayerCount)
			{
				// Without the validation layer rather than without a picture
				rsx_log.error("Could not initialize layer VK_LAYER_KHRONOS_validation");
				instance_info.enabledLayerCount = 0;
				instance_info.ppEnabledLayerNames = nullptr;
				m_instance = m_wrapper(m_wrapper_opaque, &instance_info);
			}

			if (!m_instance)
				return false;

			// The frontend's to destroy
			m_owned = false;
		}
		else
		if (VkResult result = vkCreateInstance(&instance_info, nullptr, &m_instance); result != VK_SUCCESS)
		{
			if (result == VK_ERROR_LAYER_NOT_PRESENT)
			{
				rsx_log.fatal("Could not initialize layer VK_LAYER_KHRONOS_validation");
			}

			return false;
		}

		load_vulkan_instance(m_instance);
		return true;
	}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	void instance::bind()
	{
		// Register some global states
		if (m_debugger)
		{
			_vkDestroyDebugReportCallback(m_instance, m_debugger, nullptr);
			m_debugger = nullptr;
		}

		enable_debugging();
	}

	std::vector<physical_device>& instance::enumerate_devices()
	{
		u32 num_gpus;
		// This may fail on unsupported drivers, so just assume no devices
		if (vkEnumeratePhysicalDevices(m_instance, &num_gpus, nullptr) != VK_SUCCESS)
			return gpus;

		if (gpus.size() != num_gpus)
		{
			std::vector<VkPhysicalDevice> pdevs(num_gpus);
			gpus.resize(num_gpus);

			CHECK_RESULT(vkEnumeratePhysicalDevices(m_instance, &num_gpus, pdevs.data()));

			for (u32 i = 0; i < num_gpus; ++i)
				gpus[i].create(m_instance, pdevs[i], extensions_loaded);
		}

		auto priority = [](const physical_device& dev)
		{
			switch (dev.get_type())
			{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				return 0;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
				return 1;
			default:
				return 2;
			}
		};

		std::stable_sort(gpus.begin(), gpus.end(),
			[&](const physical_device& a, const physical_device& b)
			{
				return priority(a) < priority(b);
			});

		return gpus;
	}

	swapchain_base* instance::create_swapchain(display_handle_t window_handle, vk::physical_device& dev)
	{
		// The core makes an instance of its own only when the frontend gave it
		// no hardware context, and then no surface either: nothing is
		// presented in that mode - the finished frame leaves through
		// GSFrameBase::present_frame - so the swapchain is the headless one.
		{
			rsx_log.notice("Vulkan: no surface to present to, using the headless swapchain");

			// Same choice the surface path makes below, minus the present
			// queue, which is the one thing there is no use for here.
			u32 graphics_queue_idx = umax;
			u32 transfer_queue_idx = umax;
			for (u32 i = 0, queues = dev.get_queue_count(); i < queues; ++i)
			{
				const auto flags = dev.get_queue_properties(i).queueFlags;
				if (graphics_queue_idx == umax && (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
					graphics_queue_idx = i;
				else if (transfer_queue_idx == umax && (flags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) == (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
					transfer_queue_idx = i;
			}

			if (graphics_queue_idx == umax)
			{
				rsx_log.fatal("Failed to find a suitable graphics queue");
				return nullptr;
			}

			auto swapchain = new swapchain_LIBRETRO(dev, graphics_queue_idx, graphics_queue_idx, transfer_queue_idx);
			swapchain->create(window_handle);
			return swapchain;
		}
	}
}
