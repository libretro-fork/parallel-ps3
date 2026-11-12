#include "stdafx.h"
#include "VKLibretro.h"
#include "vkutils/instance.h"
#include "vkutils/swapchain.h"

#include <retro_atomic.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace vk::libretro
{
	namespace
	{
		bool s_hw_present = false;

		std::vector<std::string> s_extensions;
		VkPhysicalDeviceFeatures s_features{};

		VkDevice s_frontend_device = VK_NULL_HANDLE;

		void (*s_lock)(void*) = nullptr;
		void (*s_unlock)(void*) = nullptr;
		void* s_lock_handle = nullptr;

		vk::swapchain_base* s_swapchain = nullptr;

		// The renderer's thread finishes frames, retro_run takes them, and
		// neither takes a lock or allocates for it: what they share is in
		// retro_atomics.
		//
		// The finished frames waiting, oldest first, in one word: their count
		// (0 to 2) in the low two bits, then an image index in each of the
		// next two bytes
		retro_atomic_int_t s_ready;

		struct handoff_init
		{
			handoff_init()
			{
				retro_atomic_int_init(&s_ready, 0);
			}
		} s_handoff_init;

		constexpr int ready_count(int q) { return q & 3; }
		constexpr u32 ready_at(int q, int i) { return static_cast<u32>((q >> (2 + 8 * i)) & 0xff); }
		constexpr int ready_of(int count, u32 first, u32 second)
		{
			return count | static_cast<int>(first << 2) | static_cast<int>(second << 10);
		}
		atomic_t<u32> s_generation{0};
	}

	bool hw_present() { return s_hw_present; }
	void set_hw_present(bool on) { s_hw_present = on; }

	void set_frontend_requirements(const char* const* extensions, u32 count, const VkPhysicalDeviceFeatures* features)
	{
		s_extensions.clear();
		for (u32 i = 0; i < count; ++i)
			if (extensions[i])
				s_extensions.emplace_back(extensions[i]);
		s_features = features ? *features : VkPhysicalDeviceFeatures{};
	}

	void apply_frontend_requirements(std::vector<const char*>& extensions, VkPhysicalDeviceFeatures& features)
	{
		if (!s_hw_present)
			return;

		for (const auto& ext : s_extensions)
		{
			const bool have = std::any_of(extensions.cbegin(), extensions.cend(), [&](const char* e) { return ext == e; });
			if (!have)
				extensions.push_back(ext.c_str());
		}

		// VkPhysicalDeviceFeatures is nothing but VkBool32s, so OR them.
		static_assert(sizeof(VkPhysicalDeviceFeatures) % sizeof(VkBool32) == 0);
		auto* dst = reinterpret_cast<VkBool32*>(&features);
		const auto* src = reinterpret_cast<const VkBool32*>(&s_features);
		for (usz i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); ++i)
			dst[i] = dst[i] || src[i];
	}

	// The frontend's thread's alone, within one create_device2
	static device_wrapper s_device_wrapper = nullptr;
	static void* s_device_wrapper_opaque = nullptr;

	void set_device_wrapper(device_wrapper wrapper, void* opaque)
	{
		s_device_wrapper = wrapper;
		s_device_wrapper_opaque = opaque;
	}

	bool create_device_wrapped(VkPhysicalDevice gpu, const VkDeviceCreateInfo* info, VkDevice* device)
	{
		if (!s_device_wrapper)
			return false;

		*device = s_device_wrapper(gpu, s_device_wrapper_opaque, info);
		return true;
	}

	void set_frontend_device(VkDevice dev) { s_frontend_device = dev; }
	bool is_frontend_device(VkDevice dev) { return dev && dev == s_frontend_device; }

	void set_queue_lock(void (*lock)(void*), void (*unlock)(void*), void* handle)
	{
		// Both or neither: an older frontend leaves them null.
		const bool usable = lock && unlock;
		s_lock = usable ? lock : nullptr;
		s_unlock = usable ? unlock : nullptr;
		s_lock_handle = handle;
	}

	void lock_queue()
	{
		if (s_lock)
			s_lock(s_lock_handle);
	}

	void unlock_queue()
	{
		if (s_unlock)
			s_unlock(s_lock_handle);
	}

	vk::instance& shared_instance()
	{
		static vk::instance s_instance;
		return s_instance;
	}

	void set_shared_swapchain(vk::swapchain_base* swapchain) { s_swapchain = swapchain; }
	vk::swapchain_base* shared_swapchain() { return s_swapchain; }

	// Finished frames wait in order and each is handed over once (take_frame
	// takes the oldest). Keeping only the newest, as before, threw a frame away
	// whenever two finished between frontend frames and repeated one whenever
	// none did - the emulator's frame times vary by a few percent, the
	// frontend's do not, and that difference showed as judder (NNshi: 117-123
	// fps through a 120 Hz frontend). A short queue absorbs it. Beyond
	// kMaxQueued the oldest go back unseen, which bounds the latency it adds -
	// and keeps images free while retro_run is not running at all (boot,
	// shutdown), when nothing takes frames: left waiting there, they held every
	// image and each flip waited five seconds for one (Wipeout HD).
	constexpr usz kMaxQueued = 2;

	void frame_ready(u32 image_index)
	{
		ensure(image_index <= 0xff);

		// Queued at the back; past kMaxQueued the oldest is overtaken
		u32 overtaken = 0;
		bool overtook = false;
		for (;;)
		{
			const int q = retro_atomic_load_acquire_int(&s_ready);
			const int count = ready_count(q);
			overtook = count == static_cast<int>(kMaxQueued);
			overtaken = ready_at(q, 0);
			const int next = count == 0 ? ready_of(1, image_index, 0)
				: count == 1 ? ready_of(2, ready_at(q, 0), image_index)
				: ready_of(2, ready_at(q, 1), image_index);
			if (retro_atomic_cas_int(&s_ready, q, next))
				break;
		}

		if (overtook)
			if (auto* swapchain = dynamic_cast<vk::swapchain_LIBRETRO*>(s_swapchain))
				swapchain->release_image(overtaken);

		// Nothing waits here for the frontend. When it stops calling
		// retro_run (paused, in its menu), no VBLANK comes, and a flip waits
		// for the next one as on a PS3: the game holds within a frame on the
		// emulated hardware, not on a clock.
	}

	bool take_frame(u32& image_index)
	{
		// The oldest frame waiting, taken by retro_run without waiting
		for (;;)
		{
			const int q = retro_atomic_load_acquire_int(&s_ready);
			const int count = ready_count(q);
			if (count == 0)
				return false;
			image_index = ready_at(q, 0);
			const int next = count == 1 ? 0 : ready_of(1, ready_at(q, 1), 0);
			if (retro_atomic_cas_int(&s_ready, q, next))
				return true;
		}
	}

	void drop_pending_frames()
	{
		retro_atomic_store_release_int(&s_ready, 0);
	}

	u32 image_generation() { return s_generation; }

	void new_image_generation()
	{
		drop_pending_frames();
		s_generation++;
	}
}
