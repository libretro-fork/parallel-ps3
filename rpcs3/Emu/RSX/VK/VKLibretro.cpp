#include "stdafx.h"
#include "VKLibretro.h"
#include "vkutils/instance.h"
#include "vkutils/swapchain.h"

#include <retro_atomic.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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

		// When retro_run last began or returned (milliseconds of the steady
		// clock), whether one is running now, and whether it is expected to
		// come again
		retro_atomic_int_t s_frontend_attached;
		retro_atomic_int_t s_frontend_in_run;
		retro_atomic_size_t s_frontend_ran_ms;

		// Stepping: the last retro_run came after the frontend had stopped.
		// Each such run lets the game draw one frame (the credit) and shows
		// the frame pushed since (s_pushes past s_step_base). In one word, so
		// the renderer sees both change at once: bit 0 stepping, the credits
		// above it.
		retro_atomic_int_t s_step;
		retro_atomic_size_t s_pushes;
		retro_atomic_size_t s_step_base;

		struct handoff_init
		{
			handoff_init()
			{
				retro_atomic_int_init(&s_ready, 0);
				retro_atomic_int_init(&s_frontend_attached, 0);
				retro_atomic_int_init(&s_frontend_in_run, 0);
				retro_atomic_size_init(&s_frontend_ran_ms, 0);
				retro_atomic_int_init(&s_step, 0);
				retro_atomic_size_init(&s_pushes, 0);
				retro_atomic_size_init(&s_step_base, 0);
			}
		} s_handoff_init;

		usz now_ms()
		{
			return static_cast<usz>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

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

	// How long after a retro_run returned the frontend still counts as
	// running: a frame and a half at 60 a second. Past it, it has stopped
	// taking frames (paused, in its menu), and a retro_run that comes after
	// such a gap is a step: Frame Advance, or the first run after a pause.
	// Counted from the return, not from the start: a step waits in retro_run
	// for its frame, and counted from the start that wait alone made the next
	// run a step too - after a pause the game stayed at a frame per step, 5-10
	// fps, for seconds (NNshi).
	constexpr auto kFrontendRunning = std::chrono::milliseconds(25);

	// How long a step waits for the frame the game draws in it. Below the
	// 100 ms after which the core's watchdog pauses the emulator again.
	constexpr auto kStepWait = std::chrono::milliseconds(90);

	// Whether a finished flip has to wait before the game goes on to its next
	// frame
	static bool frontend_stopped()
	{
		if (!retro_atomic_load_acquire_int(&s_frontend_attached))
			return false;
		if (const int step = retro_atomic_load_acquire_int(&s_step); step & 1)
			return (step >> 1) == 0;
		if (retro_atomic_load_acquire_int(&s_frontend_in_run))
			return false;
		return now_ms() - retro_atomic_load_acquire_size(&s_frontend_ran_ms) > static_cast<usz>(kFrontendRunning.count());
	}

	void frontend_ran()
	{
		const usz now = now_ms();
		const bool stepping = retro_atomic_load_acquire_int(&s_frontend_attached) &&
			now - retro_atomic_load_acquire_size(&s_frontend_ran_ms) > static_cast<usz>(kFrontendRunning.count());
		// one frame of the game for this retro_run, and the frame it draws is
		// the one to show (take_frame)
		retro_atomic_store_release_size(&s_step_base, retro_atomic_load_acquire_size(&s_pushes));
		retro_atomic_store_release_int(&s_step, stepping ? (1 | (1 << 1)) : 0);
		retro_atomic_store_release_size(&s_frontend_ran_ms, now);
		retro_atomic_store_release_int(&s_frontend_in_run, 1);
		retro_atomic_store_release_int(&s_frontend_attached, 1);
	}

	void frontend_returned()
	{
		retro_atomic_store_release_size(&s_frontend_ran_ms, now_ms());
		retro_atomic_store_release_int(&s_frontend_in_run, 0);
	}

	void frontend_detached()
	{
		retro_atomic_store_release_int(&s_step, 0);
		retro_atomic_store_release_int(&s_frontend_in_run, 0);
		retro_atomic_store_release_int(&s_frontend_attached, 0);
	}

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

		retro_atomic_fetch_add_size(&s_pushes, 1);

		// the frame a step let the game draw
		for (int step = retro_atomic_load_acquire_int(&s_step); (step & 1) && (step >> 1) > 0;
			step = retro_atomic_load_acquire_int(&s_step))
		{
			if (retro_atomic_cas_int(&s_step, step, step - (1 << 1)))
				break;
		}

		if (overtook)
			if (auto* swapchain = dynamic_cast<vk::swapchain_LIBRETRO*>(s_swapchain))
				swapchain->release_image(overtaken);

		// The frontend has stopped calling retro_run (paused, in its menu,
		// stepping frames): the game waits here, its frame handed over, for
		// the next one. It used to run on until a watchdog paused it 100 ms
		// later; the frames in between were dropped and Frame Advance let
		// that many through a step (NNshi: 4-6 frames a step, a skip after
		// every pause). A step lets exactly one frame through. This is the
		// renderer's thread; retro_run never waits for it.
		while (frontend_stopped())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	bool take_frame(u32& image_index)
	{
		// A step shows the frame the game draws in it - with the input
		// retro_run has just polled - not one finished before it: wait for
		// that frame (a step only: Frame Advance, or the run after a pause),
		// and take the newest
		const bool stepping = retro_atomic_load_acquire_int(&s_step) & 1;
		if (stepping)
		{
			const auto until = std::chrono::steady_clock::now() + kStepWait;
			const usz base = retro_atomic_load_acquire_size(&s_step_base);
			while (retro_atomic_load_acquire_size(&s_pushes) == base && std::chrono::steady_clock::now() < until)
				std::this_thread::sleep_for(std::chrono::microseconds(200));
		}

		u32 older = 0;
		bool drop_older = false;
		for (;;)
		{
			const int q = retro_atomic_load_acquire_int(&s_ready);
			const int count = ready_count(q);
			if (count == 0)
				return false;
			drop_older = stepping && count == 2;
			image_index = ready_at(q, drop_older ? 1 : 0);
			older = ready_at(q, 0);
			const int next = (count == 1 || drop_older) ? 0 : ready_of(1, ready_at(q, 1), 0);
			if (retro_atomic_cas_int(&s_ready, q, next))
				break;
		}

		if (drop_older)
			if (auto* swapchain = dynamic_cast<vk::swapchain_LIBRETRO*>(s_swapchain))
				swapchain->release_image(older);
		return true;
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
