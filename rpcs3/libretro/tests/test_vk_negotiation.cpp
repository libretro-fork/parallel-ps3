// The Vulkan context negotiation (libretro_core.cpp), with this test as the
// frontend:
//
// - a frontend that says it has version 2 is given version 2, with
//   create_instance and create_device2; one that says version 1, or does not
//   know the question, is given version 1, with create_device still there;
// - version 2, on a Vulkan driver when there is one (a software one will do):
//   the instance is made by the frontend's wrapper, once, for the
//   application the core named (Vulkan 1.2), and the wrapper's additions
//   hold; the device is made by the frontend's wrapper, once, on the GPU the
//   core reports, with the extension the frontend added; the core calls
//   vkCreateInstance and vkCreateDevice itself not at all;
// - version 1 on the same driver: the instance is the frontend's, the device
//   the core's own vkCreateDevice with the frontend's extension among them.
//
// Without a Vulkan driver the two last parts are skipped, and said so.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main.

#include "stdafx.h"
#include "Emu/RSX/VK/VulkanAPI.h"
#include "libretro_core.h"

#include <libretro_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static unsigned s_frontend_version = 0; // 0: does not know the question

static bool environment(unsigned cmd, void* data)
{
	if (cmd == RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT && s_frontend_version)
	{
		auto* iface = static_cast<retro_hw_render_context_negotiation_interface*>(data);
		if (iface->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
			return false;
		iface->interface_version = s_frontend_version;
		return true;
	}
	return false;
}

// The loader's own entry points, as a frontend has them: not the core's
static PFN_vkGetInstanceProcAddr s_gipa = nullptr;
static PFN_vkCreateInstance s_create_instance = nullptr;
static PFN_vkCreateDevice s_create_device = nullptr;
static PFN_vkDestroyDevice s_destroy_device = nullptr;
static PFN_vkEnumerateDeviceExtensionProperties s_device_extensions = nullptr;

// What the frontend wants of the device beyond what the core asks for
static const char* const s_frontend_extension = VK_KHR_MAINTENANCE1_EXTENSION_NAME;

static unsigned s_instance_wrapped = 0, s_device_wrapped = 0;
static unsigned s_instances_made = 0, s_devices_made = 0;
static u32 s_api_version = 0;
static bool s_frontend_extension_on = false;
static VkInstance s_instance = VK_NULL_HANDLE;
static VkPhysicalDevice s_wrapped_gpu = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static int s_opaque;

static VkInstance instance_wrapper(void* opaque, const VkInstanceCreateInfo* info)
{
	CHECK(opaque == &s_opaque);
	s_instance_wrapped++;
	s_api_version = info->pApplicationInfo ? info->pApplicationInfo->apiVersion : 0;
	for (u32 i = 0; i < info->enabledExtensionCount; i++)
		CHECK(std::strcmp(info->ppEnabledExtensionNames[i], VK_KHR_SURFACE_EXTENSION_NAME) != 0); // the frontend's business

	VkInstance instance = VK_NULL_HANDLE;
	if (s_create_instance(info, nullptr, &instance) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	s_instances_made++;
	s_instance = instance;
	return instance;
}

static bool has_extension(const VkDeviceCreateInfo* info, const char* name)
{
	for (u32 i = 0; i < info->enabledExtensionCount; i++)
		if (!std::strcmp(info->ppEnabledExtensionNames[i], name))
			return true;
	return false;
}

static VkDevice device_wrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info)
{
	CHECK(opaque == &s_opaque);
	s_device_wrapped++;
	s_wrapped_gpu = gpu;

	// The frontend's own extension added to the core's
	VkDeviceCreateInfo mine = *info;
	std::vector<const char*> extensions(info->ppEnabledExtensionNames, info->ppEnabledExtensionNames + info->enabledExtensionCount);
	if (!has_extension(info, s_frontend_extension))
		extensions.push_back(s_frontend_extension);
	mine.enabledExtensionCount = static_cast<u32>(extensions.size());
	mine.ppEnabledExtensionNames = extensions.data();

	VkDevice device = VK_NULL_HANDLE;
	if (s_create_device(gpu, &mine, nullptr, &device) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	s_devices_made++;
	s_frontend_extension_on = true;
	s_device = device;
	return device;
}

// Version 1's frontend sees the core's vkCreateDevice through the loader it
// hands over
static VKAPI_ATTR VkResult VKAPI_CALL counting_create_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device)
{
	s_devices_made++;
	s_frontend_extension_on = has_extension(info, s_frontend_extension);
	const VkResult result = s_create_device(gpu, info, allocator, device);
	s_device = result == VK_SUCCESS ? *device : VK_NULL_HANDLE;
	return result;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL counting_gipa(VkInstance instance, const char* name)
{
	if (!std::strcmp(name, "vkCreateDevice"))
		return reinterpret_cast<PFN_vkVoidFunction>(counting_create_device);
	return s_gipa(instance, name);
}

static bool find_loader()
{
#ifdef _WIN32
	return false;
#else
	void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!lib)
		return false;
	s_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
	if (!s_gipa)
		return false;
	s_create_instance = reinterpret_cast<PFN_vkCreateInstance>(s_gipa(VK_NULL_HANDLE, "vkCreateInstance"));

	// A driver behind it?
	VkInstanceCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	VkInstance probe = VK_NULL_HANDLE;
	if (!s_create_instance || s_create_instance(&info, nullptr, &probe) != VK_SUCCESS)
		return false;
	const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(s_gipa(probe, "vkEnumeratePhysicalDevices"));
	const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(s_gipa(probe, "vkDestroyInstance"));
	u32 gpus = 0;
	const bool found = enumerate && enumerate(probe, &gpus, nullptr) == VK_SUCCESS && gpus;
	if (destroy)
		destroy(probe, nullptr);
	return found;
#endif
}

static void fetch_instance_functions(VkInstance instance)
{
	s_create_device = reinterpret_cast<PFN_vkCreateDevice>(s_gipa(instance, "vkCreateDevice"));
	s_destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(s_gipa(instance, "vkDestroyDevice"));
	s_device_extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(s_gipa(instance, "vkEnumerateDeviceExtensionProperties"));
}

int main(int argc, char** argv)
{
	// Which version the core gives
	s_frontend_version = 2;
	const auto* iface = libretro_vulkan_negotiation(environment);
	CHECK(iface && iface->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN);
	CHECK(iface->interface_version == 2);
	CHECK(iface->get_application_info && iface->create_device && iface->create_instance && iface->create_device2);

	s_frontend_version = 3; // a later frontend: still the version the core has
	CHECK(libretro_vulkan_negotiation(environment)->interface_version == 2);

	s_frontend_version = 1;
	iface = libretro_vulkan_negotiation(environment);
	CHECK(iface->interface_version == 1 && iface->create_device);

	s_frontend_version = 0;
	CHECK(libretro_vulkan_negotiation(environment)->interface_version == 1);
	CHECK(libretro_vulkan_negotiation(nullptr)->interface_version == 1);

	const VkApplicationInfo* app = iface->get_application_info();
	CHECK(app && app->apiVersion >= VK_API_VERSION_1_2);

	if (!find_loader())
	{
		std::printf("test_vk_negotiation: no Vulkan driver here: the instance and the device were not made; %s\n", failures ? "FAIL" : "ok");
		std::fflush(stdout);
		std::_Exit(failures ? 1 : 0);
	}

	// One version a process: the device the core makes lives as long as the
	// frontend's context does. "1" as the argument for version 1.
	const bool v1 = argc > 1 && !std::strcmp(argv[1], "1");
	retro_vulkan_context context{};

	if (!v1)
	{
		s_frontend_version = 2;
		iface = libretro_vulkan_negotiation(environment);

		const VkInstance instance = iface->create_instance(s_gipa, app, instance_wrapper, &s_opaque);
		CHECK(instance != VK_NULL_HANDLE && instance == s_instance);
		CHECK(s_instance_wrapped >= 1 && s_instances_made == 1);
		CHECK(s_api_version >= VK_API_VERSION_1_2);
		if (!instance)
			std::_Exit(1);
		fetch_instance_functions(instance);

		CHECK(iface->create_device2(&context, instance, VK_NULL_HANDLE, VK_NULL_HANDLE, s_gipa, device_wrapper, &s_opaque));
		CHECK(s_device_wrapped == 1 && s_devices_made == 1);
		CHECK(context.device != VK_NULL_HANDLE && context.device == s_device);
		CHECK(context.gpu != VK_NULL_HANDLE && context.gpu == s_wrapped_gpu);
		CHECK(context.queue != VK_NULL_HANDLE && context.presentation_queue != VK_NULL_HANDLE);
		CHECK(s_frontend_extension_on);
	}
	else
	{
		s_frontend_version = 1;
		iface = libretro_vulkan_negotiation(environment);

		// The frontend's own instance, from the core's application info
		VkInstanceCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		info.pApplicationInfo = app;
		VkInstance instance = VK_NULL_HANDLE;
		CHECK(s_create_instance(&info, nullptr, &instance) == VK_SUCCESS);
		if (!instance)
			std::_Exit(1);
		fetch_instance_functions(instance);

		const char* required[] = {s_frontend_extension};
		VkPhysicalDeviceFeatures features{};
		CHECK(iface->create_device(&context, instance, VK_NULL_HANDLE, VK_NULL_HANDLE, counting_gipa, required, 1, nullptr, 0, &features));
		CHECK(s_devices_made == 1 && s_device_wrapped == 0);
		CHECK(context.device != VK_NULL_HANDLE && context.device == s_device);
		CHECK(context.queue != VK_NULL_HANDLE);
		CHECK(s_frontend_extension_on);
	}

	std::printf("test_vk_negotiation: version %d: instance through the frontend's wrapper %u time(s), device through it %u time(s), devices made %u, the frontend's extension %s; %s\n",
		v1 ? 1 : 2, s_instance_wrapped, s_device_wrapped, s_devices_made, s_frontend_extension_on ? "on" : "off", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	// The device is the frontend's and the emulator's objects want an
	// emulator's shutdown: neither is here
	std::_Exit(failures ? 1 : 0);
}
