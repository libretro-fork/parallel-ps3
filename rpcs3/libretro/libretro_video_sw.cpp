#include "stdafx.h"

#include "libretro_video.h"
#include "libretro_core.h"
#include "Emu/system_config.h"

#include <atomic>
#include <mutex>
#include <vector>

// The video unit for platforms with no desktop OpenGL to render into - Android
// being the one that matters. libretro_video.cpp is the OpenGL one and is not
// compiled there; this replaces it.
//
// There is no context, no shared texture and no blit. RPCS3 draws with Vulkan
// and finishes each frame into system memory through GSFrameBase::present_frame,
// which retro_run then hands to the frontend. Everything below that exists only
// to serve the OpenGL path is a stub, and says so.

namespace
{
	std::mutex s_sw_frame_mutex;
	std::vector<u8> s_sw_frame;
	u32 s_sw_frame_pitch = 0;
	u32 s_sw_frame_width = 0;
	u32 s_sw_frame_height = 0;
	bool s_sw_frame_is_new = false;

	// The frame retro_run last handed to the frontend. The frontend reads it
	// after the lock is gone - and again, from its frame cache, whenever it
	// redraws a duplicated or paused frame - so it must stay alive until the
	// next take, not only until the renderer delivers another one.
	std::vector<u8> s_sw_frame_shown;

	// Every frame the renderer handed over, for the stall report in retro_run.
	std::atomic<u64> s_sw_frames_presented{0};
}

u64 libretro_sw_frames_presented()
{
	return s_sw_frames_presented.load();
}

bool libretro_take_software_frame(const void** data, u32* width, u32* height, u32* pitch)
{
	std::lock_guard lock(s_sw_frame_mutex);
	if (!s_sw_frame_is_new || s_sw_frame.empty())
		return false;
	s_sw_frame_is_new = false;
	// Swapped out rather than pointed into: present_frame() replaces
	// s_sw_frame on the RSX thread, which would free the pixels while the
	// frontend is still copying them.
	s_sw_frame_shown.swap(s_sw_frame);
	*data = s_sw_frame_shown.data();
	*width = s_sw_frame_width;
	*height = s_sw_frame_height;
	*pitch = s_sw_frame_pitch;
	return true;
}

// --- the OpenGL interface, answered without OpenGL -------------------------

void libretro_video_init(retro_hw_get_current_framebuffer_t, retro_hw_get_proc_address_t) {}
void libretro_video_deinit() {}
uintptr_t libretro_get_current_framebuffer() { return 0; }
void* libretro_get_proc_address(const char*) { return nullptr; }
void libretro_wait_for_present_fence() {}
void libretro_signal_frame_ready() {}
bool libretro_has_new_frame() { return false; }
void libretro_mark_frame_presented() {}
void libretro_cleanup_gl_state() {}
int libretro_get_fbo_width() { return 1280; }
int libretro_get_fbo_height() { return 720; }
unsigned int libretro_get_rsx_fbo() { return 0; }
unsigned int libretro_get_shared_texture() { return 0; }
int libretro_get_shared_texture_width() { return 1280; }
int libretro_get_shared_texture_height() { return 720; }
void libretro_ensure_render_size(int, int) {}
void libretro_blit_to_frontend() {}

// --- LibretroGSFrame -------------------------------------------------------

LibretroGSFrame::LibretroGSFrame() = default;
LibretroGSFrame::~LibretroGSFrame() = default;

void LibretroGSFrame::close() {}
void LibretroGSFrame::reset() {}
bool LibretroGSFrame::shown() { return m_shown; }
void LibretroGSFrame::hide() { m_shown = false; }
void LibretroGSFrame::show() { m_shown = true; }
void LibretroGSFrame::toggle_fullscreen() {}

// Vulkan makes and owns its own device; there is no drawing context here for
// anyone to hand around.
void LibretroGSFrame::delete_context(draw_context_t) {}
draw_context_t LibretroGSFrame::make_context() { return nullptr; }
void LibretroGSFrame::set_current(draw_context_t) {}
void LibretroGSFrame::flip(draw_context_t, bool) {}

void LibretroGSFrame::update_dimensions_from_fbo() {}

// With the Vulkan hardware context the renderer sizes the swapchain image the
// frontend is handed after this window, and sets it to the picture it presents
// (VKPresent, libretro_set_vk_frame_size), resolution scale included. Until
// the first picture, 720p at the Resolution Scale setting, so the compilation
// progress drawn before it is not blown up from 1280x720 (libretro_video.cpp).
extern bool g_libretro_vulkan_hw;
static std::atomic<int> s_vk_frame_width{0};
static std::atomic<int> s_vk_frame_height{0};

static int libretro_vk_size(const std::atomic<int>& reported, int base)
{
	const int size = reported.load();
	return size ? size : static_cast<int>(static_cast<u64>(base) * g_cfg.video.resolution_scale_percent / 100);
}

void libretro_set_vk_frame_size(int width, int height)
{
	if (width > 0 && height > 0)
	{
		s_vk_frame_width = width;
		s_vk_frame_height = height;
	}
}

int LibretroGSFrame::client_width() { return g_libretro_vulkan_hw ? libretro_vk_size(s_vk_frame_width, 1280) : m_width; }
int LibretroGSFrame::client_height() { return g_libretro_vulkan_hw ? libretro_vk_size(s_vk_frame_height, 720) : m_height; }

void LibretroGSFrame::set_dimensions(int w, int h)
{
	m_width = w;
	m_height = h;
}

f64 LibretroGSFrame::client_display_rate() { return 60.0; }
bool LibretroGSFrame::has_alpha() { return false; }

display_handle_t LibretroGSFrame::handle() const
{
	// Nothing to present to. The Vulkan backend only reaches for this when it
	// is building a swapchain against a window, which it is not doing here.
	return {};
}

bool LibretroGSFrame::can_consume_frame() const { return true; }

void LibretroGSFrame::present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const
{
	// XRGB8888 in libretro terms is B,G,R,X in memory, which is what
	// VK_FORMAT_B8G8R8A8_UNORM already gives. Anything else would need
	// swizzling, and rather than do it wrong quietly, drop the frame and say so
	// once - the renderer is not configured the way this path expects.
	if (!is_bgra)
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			rsx_log.error("libretro: software present got a non-BGRA frame, dropping it");
		}
		return;
	}

	s_sw_frames_presented++;

	std::lock_guard lock(s_sw_frame_mutex);
	s_sw_frame = std::move(data);
	s_sw_frame_pitch = pitch;
	s_sw_frame_width = width;
	s_sw_frame_height = height;
	s_sw_frame_is_new = true;
}

void LibretroGSFrame::take_screenshot(std::vector<u8>&&, u32, u32, bool) {}
