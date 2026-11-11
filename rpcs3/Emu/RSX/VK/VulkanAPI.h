#pragma once

// Configure vulkan.h
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#else
#if defined(HAVE_X11)
 #define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(HAVE_WAYLAND)
 #define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4005 )
#endif

// The functions are volk's pointers, loaded at run time (vk::load_vulkan):
// the core links no Vulkan library. VK_NO_PROTOTYPES comes from the build,
// so that every vulkan.h included anywhere leaves the names to volk.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <volk.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Undefine header configuration variables
#undef VK_USE_PLATFORM_WIN32_KHR
#undef VK_USE_PLATFORM_METAL_EXT
#undef VK_USE_PLATFORM_ANDROID_KHR
#undef VK_USE_PLATFORM_XLIB_KHR
#undef VK_USE_PLATFORM_WAYLAND_KHR

#include <util/types.hpp>

#if VK_HEADER_VERSION < 287
constexpr VkDriverId VK_DRIVER_ID_MESA_HONEYKRISP = static_cast<VkDriverId>(26);
#endif

#if VK_HEADER_VERSION < 332
#define VK_EXT_shader_uniform_buffer_unsized_array 1
#define VK_EXT_SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_SPEC_VERSION 1
#define VK_EXT_SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_EXTENSION_NAME "VK_EXT_shader_uniform_buffer_unsized_array"
typedef struct VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT {
	VkStructureType    sType;
	void* pNext;
	VkBool32           shaderUniformBufferUnsizedArray;
} VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT;

constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_FEATURES_EXT = static_cast<VkStructureType>(1000642000);
#endif

#define DECLARE_VK_FUNCTION_HEADER 1
#include "VKProcTable.h"

namespace vk
{
	// The Vulkan loader, opened at run time (vulkan-1.dll, libvulkan.so.1,
	// MoltenVK), unless the frontend's vkGetInstanceProcAddr was given
	bool load_vulkan();
	void load_vulkan(PFN_vkGetInstanceProcAddr get_instance_proc_addr);
	// The instance's functions, after it is created or adopted
	void load_vulkan_instance(VkInstance instance);
}

namespace vk
{
	void init();

	inline VkBool32 to_bool32(bool x) { return x ? VK_TRUE : VK_FALSE; }
}
