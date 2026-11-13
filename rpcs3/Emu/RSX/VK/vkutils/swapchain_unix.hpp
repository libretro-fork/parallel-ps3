#pragma once

#include "swapchain_core.h"
#include "util/logs.hpp"


namespace vk
{
	[[maybe_unused]] static
	VkSurfaceKHR make_WSI_surface(VkInstance vk_instance, display_handle_t window_handle, WSI_config* config)
	{
		VkSurfaceKHR result = VK_NULL_HANDLE;

		std::visit([&](auto&& p)
		{
			using T = std::decay_t<decltype(p)>;
			static_assert(std::conditional_t<true, std::false_type, T>::value, "Unhandled window_handle type in std::variant");
		}, window_handle);

		return ensure(result, "Failed to initialize Vulkan display surface");
	}
}
