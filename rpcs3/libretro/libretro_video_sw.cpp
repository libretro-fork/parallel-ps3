#include "stdafx.h"
#include "libretro_sw_frames.h"

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
}

u64 libretro_sw_frames_presented()
{
	return sw_frames_presented();
}

bool libretro_take_software_frame(const void** data, u32* width, u32* height, u32* pitch)
{
	// The newest finished frame, without a lock (libretro_sw_frames.c). It stays
	// whole until the next take: the frontend may show it again until then.
	unsigned w = 0, h = 0;
	size_t p = 0;
	if (!sw_frames_take(data, &w, &h, &p))
		return false;
	*width = w;
	*height = h;
	*pitch = static_cast<u32>(p);
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

	// A frame made here (a recording's or a screenshot's): copied into the
	// buffer retro_run takes frames from (libretro_sw_frames.c). The renderer's
	// own readback writes straight into it (VKGSRender::libretro_deliver_readback).
	const usz size = usz{pitch} * height;
	if (data.size() < size)
		return;
	if (u8* dst = sw_frames_begin(size))
	{
		std::memcpy(dst, data.data(), size);
		sw_frames_end(width, height, pitch);
	}
}

void LibretroGSFrame::take_screenshot(std::vector<u8>&&, u32, u32, bool) {}
