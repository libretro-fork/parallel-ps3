#pragma once

#include "VulkanAPI.h"
#include "util/types.hpp"

#include <vector>

// The libretro core's hardware Vulkan context.
//
// Without it, the core draws on a device of its own and copies every finished
// frame back to system memory for the frontend. With it, the frontend creates
// the VkInstance, the core creates the VkDevice on it during context
// negotiation - RPCS3's own device setup, with whatever the frontend needs on
// top - and each finished frame is handed to the frontend as an image on that
// device: no copy through the CPU.
//
// Nothing in here knows about libretro's headers; the core fills it in from
// its negotiation callbacks and reads frames out of it in retro_run.
namespace vk
{
	class instance;
	class swapchain_base;
}

namespace vk::libretro
{
	// On once the frontend has accepted the hardware Vulkan context. The
	// renderer then takes the shared device and swapchain below instead of
	// making its own, and hands frames over as images.
	bool hw_present();
	void set_hw_present(bool on);

	// The device extensions and features the frontend asked for in
	// create_device, merged into what render_device::create asks for itself.
	void set_frontend_requirements(const char* const* extensions, u32 count, const VkPhysicalDeviceFeatures* features);
	void apply_frontend_requirements(std::vector<const char*>& extensions, VkPhysicalDeviceFeatures& features);

	// Version 2 of the context negotiation: the instance and the device are
	// made by the frontend's wrappers from what RPCS3 asks for, so that the
	// frontend adds what it needs itself (extensions, features in the pNext
	// chain, queues of its own). Set for the length of create_instance and
	// create_device2; nullptr otherwise, and then vkCreateInstance and
	// vkCreateDevice are called as ever.
	using instance_wrapper = VkInstance (*)(void* opaque, const VkInstanceCreateInfo* info);
	using device_wrapper = VkDevice (*)(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info);
	void set_device_wrapper(device_wrapper wrapper, void* opaque);
	// VK_NULL_HANDLE with no wrapper set: the caller then creates it itself
	bool create_device_wrapped(VkPhysicalDevice gpu, const VkDeviceCreateInfo* info, VkDevice* device);

	// The device handed to the frontend is the frontend's: it destroys it after
	// destroy_device, so render_device::destroy must leave it alone.
	void set_frontend_device(VkDevice dev);
	bool is_frontend_device(VkDevice dev);

	// The frontend submits to the same graphics queue from its own thread, and
	// a VkQueue must be externally synchronized: every submit of the renderer,
	// and every wait for the device to go idle, takes the frontend's lock.
	void set_queue_lock(void (*lock)(void*), void (*unlock)(void*), void* handle);
	void lock_queue();
	void unlock_queue();

	// The instance the frontend created, adopted rather than owned, and the
	// swapchain - and with it the render_device - built during negotiation.
	vk::instance& shared_instance();
	void set_shared_swapchain(vk::swapchain_base* swapchain);
	vk::swapchain_base* shared_swapchain();

	// Frame handoff. The renderer calls frame_ready once a frame's commands
	// have finished on the GPU; the core takes the oldest waiting image in
	// retro_run, so each frame is shown once and in order. A few wait at
	// most; beyond that the oldest go back to the swapchain unseen.
	void frame_ready(u32 image_index);
	bool take_frame(u32& image_index);
	void drop_pending_frames();


	// Bumped whenever the swapchain's images are made anew (a resize): the
	// indices the core holds then name images that are gone, and the one the
	// frontend last got must not be shown again.
	u32 image_generation();
	void new_image_generation();
}
