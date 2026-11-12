#include "stdafx.h"
#include "libretro_sw_frames.h"
#include "libretro_gl_frames.h"
#include <retro_atomic.h>

#include "libretro_video.h"
#include "libretro_core.h"
#include "Emu/system_config.h"

#include "Emu/RSX/GL/OpenGL.h"
#include "Emu/RSX/GL/glutils/common.h"

#include <vector>
#include <algorithm>
#include <functional>

LOG_CHANNEL(libretro_video_log, "LibretroVideo");

// The renderer's contexts: made through the frontend
// (RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE) in context_reset, in
// its context's share group, before any thread asks for one, and handed out
// and back without a lock - the renderer's thread takes one and each shader
// compiler thread one, from their own threads, at any time. Nothing of a
// window system: the frontend makes them. One more than the renderer and
// RPCS3's most compiler threads (6).
static constexpr int kSharedContexts = 8;
static const retro_hw_shared_context_interface* s_shared_iface = nullptr;
static retro_hw_shared_context* s_shared[kSharedContexts] = {};
static retro_atomic_int_t s_shared_taken[kSharedContexts];
static int s_shared_count = 0;

static retro_hw_get_current_framebuffer_t s_get_current_framebuffer = nullptr;
static retro_hw_get_proc_address_t s_get_proc_address = nullptr;
static bool s_gl_initialized = false;

// The renderer's frames to the frontend: three textures shared between the
// renderer's context and the frontend's, handed over without a lock and
// fenced on the GPU (libretro_gl_frames.c). FBOs are not shared between GL
// contexts, but textures are: the renderer draws through its own FBO over a
// texture, and retro_run blits it into the frontend's FBO through one of its
// own. Before, one texture was drawn into while the frontend copied it.
extern bool g_libretro_vulkan_hw;

// The size the renderer is to draw the next frame at, and whether it has
// drawn since it last handed a frame over (the renderer's thread's)
static int s_draw_width = 1280;
static int s_draw_height = 720;
static bool s_drawn_since_flip = false;

// The GL functions the frames' code calls: the table's, which the frontend
// filled
static void set_gl_frames_procs()
{
    lr_gl_frames_procs procs{};
    procs.gen_textures = glGenTextures;
    procs.delete_textures = glDeleteTextures;
    procs.bind_texture = glBindTexture;
    procs.tex_image_2d = glTexImage2D;
    procs.tex_parameteri = glTexParameteri;
    procs.gen_framebuffers = glGenFramebuffers;
    procs.delete_framebuffers = glDeleteFramebuffers;
    procs.bind_framebuffer = glBindFramebuffer;
    procs.framebuffer_texture_2d = glFramebufferTexture2D;
    procs.gen_renderbuffers = glGenRenderbuffers;
    procs.delete_renderbuffers = glDeleteRenderbuffers;
    procs.bind_renderbuffer = glBindRenderbuffer;
    procs.renderbuffer_storage = glRenderbufferStorage;
    procs.framebuffer_renderbuffer = glFramebufferRenderbuffer;
    procs.blit_framebuffer = glBlitFramebuffer;
    procs.fence_sync = glFenceSync;
    procs.wait_sync = glWaitSync;
    procs.delete_sync = glDeleteSync;
    procs.flush = glFlush;
    lr_gl_frames_set_procs(&procs);
}

// The renderer's thread, at present: the picture's size. The frame is drawn
// at exactly that size and handed to the frontend at it: at the 1280x720 the
// texture was first made at, a title drawn at a higher resolution scale was
// shrunk back into it, and the frontend never saw more than 720p (NNshi).
void libretro_ensure_render_size(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    s_draw_width = width;
    s_draw_height = height;
}

// The renderer's thread: the FBO to present the frame into
unsigned int libretro_get_rsx_fbo()
{
    s_drawn_since_flip = true;
    return lr_gl_frames_draw_fbo(s_draw_width, s_draw_height);
}

// The renderer's thread: the size it draws at (the window it reports)
int libretro_get_shared_texture_width()
{
    return s_draw_width;
}

int libretro_get_shared_texture_height()
{
    return s_draw_height;
}

// retro_run: the newest frame finished into RetroArch's FBO, once the GPU has
// drawn it - the GPU waits on the renderer's fence, not retro_run
bool libretro_blit_to_frontend(unsigned* width, unsigned* height)
{
    if (!s_gl_initialized || !s_get_current_framebuffer)
        return false;

    int w = 0, h = 0;
    if (!lr_gl_frames_blit(static_cast<GLuint>(s_get_current_framebuffer()), &w, &h))
        return false;
    *width = static_cast<unsigned>(w);
    *height = static_cast<unsigned>(h);
    return true;
}

// Clean up GL state before returning control to frontend
// Per libretro docs: "Don't leave buffers and global objects bound when calling retro_video_refresh_t"
// PERFORMANCE: Keep this minimal - only unbind what's absolutely necessary
void libretro_cleanup_gl_state()
{
    if (!s_gl_initialized)
        return;

    // CRITICAL: Unbind framebuffer so RetroArch can bind its own for presentation
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Unbind shader program to avoid conflicts with RetroArch's rendering
    glUseProgram(0);

    // NOTE: We intentionally do NOT unbind:
    // - VAO/VBO/buffers: RSX will rebind what it needs, unbinding is expensive
    // - Textures: 32 bind calls per frame (16 units × 2 types) is a huge waste
    // - GL state flags: RSX manages its own state, resetting is redundant
    // The previous implementation was doing 40+ GL calls per frame at 60fps = 2400+ calls/sec
    // which was a major performance bottleneck, especially on some drivers.
}

// context_reset, the frontend's context current: every GL function from the
// frontend, and the renderer's contexts from it
static bool libretro_gl_init(retro_environment_t environ_cb)
{
    #define OPENGL_PROC(p, n) gl##n = reinterpret_cast<p>(s_get_proc_address("gl" #n))
    #define WGL_PROC(p, n)
    #define OPENGL_PROC2(p, n, tn) OPENGL_PROC(p, n)
    #include "Emu/RSX/GL/GLProcTable.h"
    #undef OPENGL_PROC
    #undef OPENGL_PROC2
    #undef WGL_PROC

    s_shared_iface = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE, &s_shared_iface) || !s_shared_iface
        || s_shared_iface->interface_version < RETRO_HW_SHARED_CONTEXT_INTERFACE_VERSION)
    {
        s_shared_iface = nullptr;
        return false;
    }

    s_shared_count = 0;
    for (int i = 0; i < kSharedContexts; i++)
    {
        if (!(s_shared[i] = s_shared_iface->create()))
            break;
        retro_atomic_store_release_int(&s_shared_taken[i], 0);
        s_shared_count++;
    }
    return s_shared_count > 0;
}

// Store the actual FBO dimensions from RetroArch
static int s_fbo_width = 1280;
static int s_fbo_height = 720;

int libretro_get_fbo_width() { return s_fbo_width; }
int libretro_get_fbo_height() { return s_fbo_height; }

// context_reset. False when the frontend gives no contexts for the
// renderer's threads: the renderer cannot run then.
bool libretro_video_init(retro_hw_get_current_framebuffer_t get_fb, retro_hw_get_proc_address_t get_proc,
    retro_environment_t environ_cb)
{
    s_get_current_framebuffer = get_fb;
    s_get_proc_address = get_proc;
    s_gl_initialized = libretro_gl_init(environ_cb);
    if (!s_gl_initialized)
        return false;
    set_gl_frames_procs();

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] > 0 && viewport[3] > 0)
    {
        s_fbo_width = viewport[2];
        s_fbo_height = viewport[3];
    }
    return true;
}

// context_destroy, the frontend's context current, the renderer stopped
void libretro_video_deinit()
{
    // The renderer's frames: the textures and depth buffers it drew into
    // belong to the share group, so they go from the frontend's context
    if (s_gl_initialized)
        lr_gl_frames_destroy();

    for (int i = 0; i < s_shared_count; i++)
    {
        s_shared_iface->destroy(s_shared[i]);
        s_shared[i] = nullptr;
    }
    s_shared_count = 0;
    s_shared_iface = nullptr;
    s_gl_initialized = false;
    s_get_current_framebuffer = nullptr;
    s_get_proc_address = nullptr;
}

uintptr_t libretro_get_current_framebuffer()
{
    if (s_get_current_framebuffer)
        return s_get_current_framebuffer();
    return 0;
}

void* libretro_get_proc_address(const char* sym)
{
    if (s_get_proc_address)
        return reinterpret_cast<void*>(s_get_proc_address(sym));
    return nullptr;
}

// LibretroGSFrame implementation

LibretroGSFrame::LibretroGSFrame()
{
}

LibretroGSFrame::~LibretroGSFrame()
{
    close();
}

void LibretroGSFrame::close()
{
    m_shown = false;
}

void LibretroGSFrame::reset()
{
}

bool LibretroGSFrame::shown()
{
    return m_shown;
}

void LibretroGSFrame::hide()
{
    m_shown = false;
}

void LibretroGSFrame::show()
{
    m_shown = true;
}

void LibretroGSFrame::toggle_fullscreen()
{
    // Fullscreen is handled by the frontend
}

// The thread that used it: let go of it there, and give it back
void LibretroGSFrame::delete_context(draw_context_t ctx)
{
    for (int i = 0; ctx && i < s_shared_count; i++)
    {
        if (reinterpret_cast<draw_context_t>(s_shared[i]) != ctx)
            continue;
        s_shared_iface->make_current(s_shared[i], false);
        retro_atomic_store_release_int(&s_shared_taken[i], 0);
        return;
    }
}

// Any thread: one of the contexts made in context_reset nobody has, or none
// when all are taken
draw_context_t LibretroGSFrame::make_context()
{
    for (int i = 0; i < s_shared_count; i++)
    {
        if (retro_atomic_cas_int(&s_shared_taken[i], 0, 1))
        {
            m_context = reinterpret_cast<draw_context_t>(s_shared[i]);
            return m_context;
        }
    }
    return nullptr;
}

void LibretroGSFrame::set_current(draw_context_t ctx)
{
    if (ctx && s_shared_iface)
        s_shared_iface->make_current(reinterpret_cast<retro_hw_shared_context*>(ctx), true);
}

void LibretroGSFrame::flip(draw_context_t ctx, bool skip_frame)
{
    (void)ctx;
    if (skip_frame)
        return;

    // The frame presented since the last flip, handed over; a flip with
    // nothing presented hands over nothing
    if (s_drawn_since_flip)
    {
        s_drawn_since_flip = false;
        lr_gl_frames_publish();
    }
}

void LibretroGSFrame::update_dimensions_from_fbo()
{
    // This is intentionally empty - we use the fixed dimensions from retro_get_system_av_info()
    // RetroArch handles scaling from our base resolution (1280x720) to the actual window size
    // Querying the viewport here would get RSX's internal viewport, not RetroArch's window size
}

void LibretroGSFrame::set_dimensions(int w, int h)
{
    if (w > 0 && h > 0)
    {
        m_width = w;
        m_height = h;
    }
}

// With the Vulkan hardware context the frontend is handed the swapchain image,
// which the renderer sizes after the window this frame reports. There is no
// shared GL texture to take the size from, so the renderer sets it to the
// picture it presents (VKPresent, libretro_set_vk_frame_size): at the 1280x720
// it started with, a title drawn at 200% was shrunk back to 720p (NNshi).
// Until the first picture says otherwise, the size is 720p at the Resolution
// Scale setting: what is drawn before the game's first flip - the shader and
// PPU compilation progress - was drawn at 1280x720 and blown up, blurry, at
// 200% and up (NNshi).
// The size the renderer last presented, width | height << 16, in one word so
// the two never come from different pictures; 0, as static storage starts,
// for none yet
static retro_atomic_int_t s_vk_frame_size;

static int libretro_vk_start_size(int base)
{
    return static_cast<int>(static_cast<u64>(base) * g_cfg.video.resolution_scale_percent / 100);
}

void libretro_set_vk_frame_size(int width, int height)
{
    if (width > 0 && height > 0 && width <= 0xffff && height <= 0x7fff)
        retro_atomic_store_release_int(&s_vk_frame_size, width | (height << 16));
}

int LibretroGSFrame::client_width()
{
    if (g_libretro_vulkan_hw)
    {
        const int size = retro_atomic_load_acquire_int(&s_vk_frame_size);
        return size ? (size & 0xffff) : libretro_vk_start_size(1280);
    }
    // Use shared texture size - RSX renders at game native resolution
    // RetroArch handles scaling from our output to window size
    return libretro_get_shared_texture_width();
}

int LibretroGSFrame::client_height()
{
    if (g_libretro_vulkan_hw)
    {
        const int size = retro_atomic_load_acquire_int(&s_vk_frame_size);
        return size ? (size >> 16) : libretro_vk_start_size(720);
    }
    // Use shared texture size - RSX renders at game native resolution
    // RetroArch handles scaling from our output to window size
    return libretro_get_shared_texture_height();
}

f64 LibretroGSFrame::client_display_rate()
{
    // Standard 60Hz, frontend may override
    return 60.0;
}

bool LibretroGSFrame::has_alpha()
{
    return false;
}

display_handle_t LibretroGSFrame::handle() const
{
    return {};
}

bool LibretroGSFrame::can_consume_frame() const
{
    return true;
}

// The most recent frame handed over by the renderer, when the core is running
// without a hardware context. Written on the RSX thread, read in retro_run, so
// it is swapped under a lock rather than copied.
namespace
{
}

void LibretroGSFrame::present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const
{
    // XRGB8888 in libretro terms is B,G,R,X in memory, which is what
    // VK_FORMAT_B8G8R8A8_UNORM already gives us. Anything else would need
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

void LibretroGSFrame::take_screenshot(std::vector<u8>&& sshot_data, u32 sshot_width, u32 sshot_height, bool is_bgra)
{
    // Screenshots can be handled by the frontend
    (void)sshot_data;
    (void)sshot_width;
    (void)sshot_height;
    (void)is_bgra;
}
