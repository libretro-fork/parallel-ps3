#include "stdafx.h"
#include "libretro_sw_frames.h"
#include "libretro_gl_frames.h"
#include <retro_atomic.h>

#include "libretro_video.h"
#include "libretro_core.h"
#include "Emu/system_config.h"

#include "Emu/RSX/GL/OpenGL.h"
#include "Emu/RSX/GL/glutils/common.h"

#include <mutex>
#include <vector>
#include <algorithm>
#include <thread>
#include <functional>

// The versions to ask for when building the RSX thread's own context, highest
// first. Not one hardcoded version: this is the context RPCS3's capability
// check reads its extension list from, and an extension promoted into core is
// no longer required to be advertised by name. A 4.3 context on Mesa therefore
// reports no direct state access at all - which reached a user as
// "GL_ARB_direct_state_access ... is required but not supported by your GPU" on
// a Radeon RX 6600. 4.5 is the real floor for this backend; the entry below it
// is there so that a refusal comes from RPCS3's own check, which names what is
// missing, rather than from a context that was never created.
struct shared_context_version { int major, minor; };
static constexpr shared_context_version kSharedContextVersions[] = {
    {4, 6}, {4, 5}, {4, 4}, {4, 3},
};

#ifdef _WIN32
#include <Windows.h>
#include <GL/gl.h>
#include <glext.h>

// WGL extension function pointers for context creation
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB_ptr = nullptr;

// WGL_ARB_create_context attributes
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

// Saved context info from RetroArch's main thread
static HDC s_main_hdc = nullptr;
static HGLRC s_main_hglrc = nullptr;
static std::vector<HGLRC> s_shared_contexts;
static std::vector<HGLRC> s_available_contexts;  // Pre-created shared contexts pool
static std::mutex s_context_pool_mutex;
#endif

#if defined(__unix__) && !defined(__APPLE__)
#include <EGL/egl.h>
#include <GL/glx.h>

// RetroArch's GL context is only ever current on the frontend's video thread,
// but RSX (and the shader compiler threads) draw on their own threads. Capture
// the frontend's state in context_reset and create contexts that share its
// objects, mirroring what the WGL path above does on Windows. Without this the
// RSX thread has no current context at all, glGetIntegerv(GL_NUM_EXTENSIONS)
// returns 0, and gl::capabilities::initialize() reports every extension as
// unsupported - which reaches the user as "GL_ARB_texture_buffer_object is
// required but not supported by your GPU" on a GPU that supports it.
//
// Two window systems, because the frontend picks one and the core does not get
// a say. RetroArch on Wayland gives us an EGL context; on X11 it gives us a GLX
// one, and then eglGetCurrentContext has nothing to report - the case this file
// used to fall out of into a context-less RSX thread. Whichever one answers is
// the one used; they are never both live.
enum class shared_context_backend
{
    none,
    egl,
    glx,
};
static shared_context_backend s_context_backend = shared_context_backend::none;

static EGLDisplay s_egl_display = EGL_NO_DISPLAY;
static EGLContext s_egl_main_context = EGL_NO_CONTEXT;
static EGLConfig s_egl_config = nullptr;
static bool s_egl_have_config = false;
static std::vector<EGLContext> s_shared_egl_contexts;
// Contexts retired by delete_context(). They are destroyed later, from
// context_destroy() - see the comment there.
static std::vector<EGLContext> s_retired_egl_contexts;
static std::mutex s_egl_context_mutex;

// Every GLX 1.3 entry point has to be called through a pointer of our own, and
// this is not defensiveness - it is the only way that works here. This file
// includes RPCS3's OpenGL.h, which is GLEW, and GLEW's glxew.h #defines the
// whole GLX 1.3 API to its own function pointers (glXGetCurrentDisplay becomes
// GLXEW_GET_FUN(__glewXGetCurrentDisplay), and so on). Those pointers are null
// until glxewInit() has run, and nothing in this core runs it - so the call
// that looks like glXGetCurrentDisplay() is a jump to address 0. That is
// exactly the crash the first GLX build produced: "Segfault executing location
// 0000000000000000", on the frontend's thread, in context_reset.
//
// The GLX 1.2 functions are real symbols and can be called by name; anything
// 1.3 or newer goes through here, resolved with glXGetProcAddressARB, which is
// itself a real symbol and works before any context is current.
using glXCreateContextAttribsARB_t = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
using glXGetCurrentDisplay_t = Display* (*)();
using glXQueryContext_t = int (*)(Display*, GLXContext, int, int*);
using glXChooseFBConfig_t = GLXFBConfig* (*)(Display*, int, const int*, int*);
using glXGetFBConfigAttrib_t = int (*)(Display*, GLXFBConfig, int, int*);
using glXCreatePbuffer_t = GLXPbuffer (*)(Display*, GLXFBConfig, const int*);
using glXDestroyPbuffer_t = void (*)(Display*, GLXPbuffer);
using glXMakeContextCurrent_t = Bool (*)(Display*, GLXDrawable, GLXDrawable, GLXContext);

static glXCreateContextAttribsARB_t s_glXCreateContextAttribsARB = nullptr;
static glXGetCurrentDisplay_t s_glXGetCurrentDisplay = nullptr;
static glXQueryContext_t s_glXQueryContext = nullptr;
static glXChooseFBConfig_t s_glXChooseFBConfig = nullptr;
static glXGetFBConfigAttrib_t s_glXGetFBConfigAttrib = nullptr;
static glXCreatePbuffer_t s_glXCreatePbuffer = nullptr;
static glXDestroyPbuffer_t s_glXDestroyPbuffer = nullptr;
static glXMakeContextCurrent_t s_glXMakeContextCurrent = nullptr;

#endif

LOG_CHANNEL(libretro_video_log, "LibretroVideo");

#if defined(__unix__) && !defined(__APPLE__)
template <typename T>
static bool glx_resolve(T& fn, const char* name)
{
	fn = reinterpret_cast<T>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
	if (!fn)
		libretro_video_log.error("GLX: %s is missing from this GLX implementation.", name);
	return fn != nullptr;
}

// Resolves the set once. Everything here is GLX 1.3, which has been the
// baseline since 1998 - a failure means something is very wrong with the GLX
// on this machine, and the caller falls back to a context-less RSX thread with
// the reason in the log rather than jumping through a null pointer.
static bool glx_resolve_entry_points()
{
	static int s_resolved = -1;
	if (s_resolved >= 0)
		return s_resolved != 0;

	bool ok = true;
	ok &= glx_resolve(s_glXGetCurrentDisplay, "glXGetCurrentDisplay");
	ok &= glx_resolve(s_glXQueryContext, "glXQueryContext");
	ok &= glx_resolve(s_glXChooseFBConfig, "glXChooseFBConfig");
	ok &= glx_resolve(s_glXGetFBConfigAttrib, "glXGetFBConfigAttrib");
	ok &= glx_resolve(s_glXCreatePbuffer, "glXCreatePbuffer");
	ok &= glx_resolve(s_glXDestroyPbuffer, "glXDestroyPbuffer");
	ok &= glx_resolve(s_glXMakeContextCurrent, "glXMakeContextCurrent");
	s_resolved = ok ? 1 : 0;
	return ok;
}

static Display* s_glx_display = nullptr;
static GLXContext s_glx_main_context = nullptr;
static GLXFBConfig s_glx_fbconfig = nullptr;
static bool s_glx_have_config = false;
// GLX has no surfaceless make-current, so every context needs a drawable of its
// own: the frontend's window belongs to the video thread, and a drawable may be
// current to one thread at a time - which also rules out one shared pbuffer,
// because RSX is not the only thread that asks for a context (the pipe compiler
// takes one per thread). A 1x1 pbuffer each costs nothing; RSX renders into
// FBOs and never draws to it.
struct glx_thread_context
{
    GLXContext ctx = nullptr;
    GLXPbuffer pbuffer = 0;
};
static std::vector<glx_thread_context> s_shared_glx_contexts;
static std::vector<glx_thread_context> s_retired_glx_contexts;
static std::mutex s_glx_context_mutex;

// GLX reports context creation failures through the X error handler rather than
// through a return value, so a failed glXCreateContextAttribsARB can come back
// non-null and kill the process later. Install a handler for the length of the
// call and ask the server what it thought.
static bool s_glx_error_flag = false;
static int glx_error_handler(Display*, XErrorEvent*)
{
    s_glx_error_flag = true;
    return 0;
}

#endif

static retro_hw_get_current_framebuffer_t s_get_current_framebuffer = nullptr;
static retro_hw_get_proc_address_t s_get_proc_address = nullptr;
static std::mutex s_video_mutex;
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

// The GL functions, through GLEW, called when they are called: GLEW loads
// them on the renderer's thread after these are handed over
static void LR_GL_APIENTRY lr_gen_textures(GLsizei n, GLuint* t) { glGenTextures(n, t); }
static void LR_GL_APIENTRY lr_delete_textures(GLsizei n, const GLuint* t) { glDeleteTextures(n, t); }
static void LR_GL_APIENTRY lr_bind_texture(GLenum target, GLuint t) { glBindTexture(target, t); }
static void LR_GL_APIENTRY lr_tex_image_2d(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data) { glTexImage2D(target, level, internal, w, h, border, format, type, data); }
static void LR_GL_APIENTRY lr_tex_parameteri(GLenum target, GLenum name, GLint value) { glTexParameteri(target, name, value); }
static void LR_GL_APIENTRY lr_gen_framebuffers(GLsizei n, GLuint* f) { glGenFramebuffers(n, f); }
static void LR_GL_APIENTRY lr_delete_framebuffers(GLsizei n, const GLuint* f) { glDeleteFramebuffers(n, f); }
static void LR_GL_APIENTRY lr_bind_framebuffer(GLenum target, GLuint f) { glBindFramebuffer(target, f); }
static void LR_GL_APIENTRY lr_framebuffer_texture_2d(GLenum target, GLenum attachment, GLenum textarget, GLuint t, GLint level) { glFramebufferTexture2D(target, attachment, textarget, t, level); }
static void LR_GL_APIENTRY lr_gen_renderbuffers(GLsizei n, GLuint* r) { glGenRenderbuffers(n, r); }
static void LR_GL_APIENTRY lr_delete_renderbuffers(GLsizei n, const GLuint* r) { glDeleteRenderbuffers(n, r); }
static void LR_GL_APIENTRY lr_bind_renderbuffer(GLenum target, GLuint r) { glBindRenderbuffer(target, r); }
static void LR_GL_APIENTRY lr_renderbuffer_storage(GLenum target, GLenum format, GLsizei w, GLsizei h) { glRenderbufferStorage(target, format, w, h); }
static void LR_GL_APIENTRY lr_framebuffer_renderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint r) { glFramebufferRenderbuffer(target, attachment, rbtarget, r); }
static void LR_GL_APIENTRY lr_blit_framebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) { glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter); }
static GLsync LR_GL_APIENTRY lr_fence_sync(GLenum condition, GLbitfield flags) { return glFenceSync(condition, flags); }
static void LR_GL_APIENTRY lr_wait_sync(GLsync sync, GLbitfield flags, GLuint64 timeout) { glWaitSync(sync, flags, timeout); }
static void LR_GL_APIENTRY lr_delete_sync(GLsync sync) { glDeleteSync(sync); }
static void LR_GL_APIENTRY lr_flush() { glFlush(); }

static void set_gl_frames_procs()
{
    lr_gl_frames_procs procs{};
    procs.gen_textures = lr_gen_textures;
    procs.delete_textures = lr_delete_textures;
    procs.bind_texture = lr_bind_texture;
    procs.tex_image_2d = lr_tex_image_2d;
    procs.tex_parameteri = lr_tex_parameteri;
    procs.gen_framebuffers = lr_gen_framebuffers;
    procs.delete_framebuffers = lr_delete_framebuffers;
    procs.bind_framebuffer = lr_bind_framebuffer;
    procs.framebuffer_texture_2d = lr_framebuffer_texture_2d;
    procs.gen_renderbuffers = lr_gen_renderbuffers;
    procs.delete_renderbuffers = lr_delete_renderbuffers;
    procs.bind_renderbuffer = lr_bind_renderbuffer;
    procs.renderbuffer_storage = lr_renderbuffer_storage;
    procs.framebuffer_renderbuffer = lr_framebuffer_renderbuffer;
    procs.blit_framebuffer = lr_blit_framebuffer;
    procs.fence_sync = lr_fence_sync;
    procs.wait_sync = lr_wait_sync;
    procs.delete_sync = lr_delete_sync;
    procs.flush = lr_flush;
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

// Pre-create shared OpenGL contexts while main context is idle
// This must be called on the main thread before the context is used elsewhere
static void precreate_shared_contexts(int count)
{
#ifdef _WIN32
    if (!s_main_hdc || !s_main_hglrc || !wglCreateContextAttribsARB_ptr)
        return;

    // Temporarily unbind main context so we can share with it
    if (!wglMakeCurrent(NULL, NULL))
        return;

    for (int i = 0; i < count; i++)
    {
        HGLRC shared_ctx = nullptr;
        for (const auto& v : kSharedContextVersions)
        {
            int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, v.major,
                WGL_CONTEXT_MINOR_VERSION_ARB, v.minor,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };

            shared_ctx = wglCreateContextAttribsARB_ptr(s_main_hdc, s_main_hglrc, attribs);
            if (shared_ctx)
                break;
        }

        if (shared_ctx)
        {
            std::lock_guard<std::mutex> lock(s_context_pool_mutex);
            s_available_contexts.push_back(shared_ctx);
        }
    }

    // Restore main context
    wglMakeCurrent(s_main_hdc, s_main_hglrc);
#endif
}

#if defined(__unix__) && !defined(__APPLE__)
// Runs on: RetroArch's video thread, from context_reset, with the frontend's
// GLX context current. The X11 half of what the EGL block below does.
static void libretro_glx_init()
{
    if (!glx_resolve_entry_points())
        return;

    s_glx_display = s_glXGetCurrentDisplay();
    s_glx_main_context = glXGetCurrentContext();

    if (!s_glx_display || !s_glx_main_context)
    {
        libretro_video_log.error("GLX: no current display/context on the video thread either; RSX will have no "
                                 "shared context and every GL capability will read as unsupported.");
        return;
    }

    s_glXCreateContextAttribsARB = reinterpret_cast<glXCreateContextAttribsARB_t>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    if (!s_glXCreateContextAttribsARB)
    {
        libretro_video_log.error("GLX: GLX_ARB_create_context is missing; a core profile context cannot be asked for.");
        return;
    }

    int screen = DefaultScreen(s_glx_display);
    int screen_of_context = 0;
    if (s_glXQueryContext(s_glx_display, s_glx_main_context, GLX_SCREEN, &screen_of_context) == Success)
        screen = screen_of_context;

    // The frontend's own config first, so the shared context matches the one
    // its objects were made on. It is only usable here if it can back a
    // pbuffer, which a window config does not have to.
    int fbconfig_id = 0;
    if (s_glXQueryContext(s_glx_display, s_glx_main_context, GLX_FBCONFIG_ID, &fbconfig_id) == Success && fbconfig_id)
    {
        const int cfg_attribs[] = { GLX_FBCONFIG_ID, fbconfig_id, None };
        int num_configs = 0;
        if (GLXFBConfig* cfgs = s_glXChooseFBConfig(s_glx_display, screen, cfg_attribs, &num_configs); cfgs)
        {
            if (num_configs > 0)
            {
                int drawable_type = 0;
                s_glXGetFBConfigAttrib(s_glx_display, cfgs[0], GLX_DRAWABLE_TYPE, &drawable_type);
                if (drawable_type & GLX_PBUFFER_BIT)
                {
                    s_glx_fbconfig = cfgs[0];
                    s_glx_have_config = true;
                }
            }
            XFree(cfgs);
        }
    }

    if (!s_glx_have_config)
    {
        // Any pbuffer-capable RGBA config on the same screen will do. Sharing
        // objects between contexts of different configs is allowed; they only
        // have to be the same renderer, which on one screen they are.
        const int cfg_attribs[] = {
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
            None
        };
        int num_configs = 0;
        if (GLXFBConfig* cfgs = s_glXChooseFBConfig(s_glx_display, screen, cfg_attribs, &num_configs); cfgs)
        {
            if (num_configs > 0)
            {
                s_glx_fbconfig = cfgs[0];
                s_glx_have_config = true;
            }
            XFree(cfgs);
        }
    }

    if (!s_glx_have_config)
    {
        libretro_video_log.error("GLX: no pbuffer-capable framebuffer config; RSX will have no shared context.");
        return;
    }

    s_context_backend = shared_context_backend::glx;
    libretro_video_log.notice("GLX: sharing with the frontend's context (display=%p, context=%p).",
        s_glx_display, s_glx_main_context);
}
#endif

// Initialize OpenGL function pointers using libretro's get_proc_address callback
static void libretro_gl_init()
{
    if (!s_get_proc_address)
        return;

#ifdef _WIN32
    // Save the main thread's context info for creating shared contexts later
    s_main_hdc = wglGetCurrentDC();
    s_main_hglrc = wglGetCurrentContext();

    // Load wglCreateContextAttribsARB for creating shared contexts
    wglCreateContextAttribsARB_ptr = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        s_get_proc_address("wglCreateContextAttribsARB"));

    // Load GL functions using libretro's callback instead of wglGetProcAddress
    // Use X-macro pattern with GLProcTable.h
    #define OPENGL_PROC(p, n) \
        if (!(gl##n)) { \
            gl##n = reinterpret_cast<p>(s_get_proc_address("gl" #n)); \
            if (!(gl##n)) libretro_video_log.warning("OpenGL: initialization of gl" #n " failed (may be optional)."); \
        }
    #define WGL_PROC(p, n) \
        if (!(wgl##n)) { \
            wgl##n = reinterpret_cast<p>(s_get_proc_address("wgl" #n)); \
        }
    #define OPENGL_PROC2(p, n, tn) OPENGL_PROC(p, n)
    #include "Emu/RSX/GL/GLProcTable.h"
    #undef OPENGL_PROC
    #undef OPENGL_PROC2
    #undef WGL_PROC

    // Pre-create shared contexts for RSX thread and shader compiler threads
    precreate_shared_contexts(10);
#else
    // Runs on RetroArch's video thread from context_reset, i.e. the one place
    // where the frontend's context is guaranteed to be current: grab the
    // handles the RSX thread will need to create shared contexts later.
    s_egl_display = eglGetCurrentDisplay();
    s_egl_main_context = eglGetCurrentContext();

    if (s_egl_display != EGL_NO_DISPLAY && s_egl_main_context != EGL_NO_CONTEXT)
    {
        EGLint config_id = 0;
        if (eglQueryContext(s_egl_display, s_egl_main_context, EGL_CONFIG_ID, &config_id))
        {
            const EGLint cfg_attribs[] = { EGL_CONFIG_ID, config_id, EGL_NONE };
            EGLint num_configs = 0;
            if (eglChooseConfig(s_egl_display, cfg_attribs, &s_egl_config, 1, &num_configs) && num_configs == 1)
            {
                s_egl_have_config = true;
                s_context_backend = shared_context_backend::egl;
            }
        }

        if (!s_egl_have_config)
        {
            libretro_video_log.error("EGL: could not resolve the frontend's config; RSX will have no shared context.");
        }
    }
    else
    {
        // Not an error yet: an X11 frontend has a GLX context, and EGL is
        // supposed to say nothing about it.
        libretro_video_log.notice("EGL: nothing current on the video thread; trying GLX.");
        libretro_glx_init();
    }

    // A context is current on this thread, so GLEW can resolve its entry points
    // here; the pointers it fills in are global and usable from the RSX thread.
    gl::init();
#endif
}

// Store the actual FBO dimensions from RetroArch
static int s_fbo_width = 1280;
static int s_fbo_height = 720;

int libretro_get_fbo_width() { return s_fbo_width; }
int libretro_get_fbo_height() { return s_fbo_height; }

void libretro_video_init(retro_hw_get_current_framebuffer_t get_fb, retro_hw_get_proc_address_t get_proc)
{
    std::lock_guard<std::mutex> lock(s_video_mutex);
    s_get_current_framebuffer = get_fb;
    s_get_proc_address = get_proc;
    set_gl_frames_procs();

    if (!s_gl_initialized)
    {
        libretro_gl_init();
        s_gl_initialized = true;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] > 0 && viewport[3] > 0)
    {
        s_fbo_width = viewport[2];
        s_fbo_height = viewport[3];
    }
}

void libretro_video_deinit()
{
    std::lock_guard<std::mutex> lock(s_video_mutex);
    s_get_current_framebuffer = nullptr;
    s_get_proc_address = nullptr;
    const bool had_gl = s_gl_initialized;

#if defined(__unix__) && !defined(__APPLE__)
    // Now that the frontend is dropping the context, it is safe to destroy the
    // shared contexts retired by delete_context() (see the note there).
    std::lock_guard<std::mutex> egl_lock(s_egl_context_mutex);

    if (s_egl_display != EGL_NO_DISPLAY)
    {
        for (EGLContext ctx : s_retired_egl_contexts)
            eglDestroyContext(s_egl_display, ctx);

        for (EGLContext ctx : s_shared_egl_contexts)
            eglDestroyContext(s_egl_display, ctx);
    }

    s_retired_egl_contexts.clear();
    s_shared_egl_contexts.clear();
    s_egl_display = EGL_NO_DISPLAY;
    s_egl_main_context = EGL_NO_CONTEXT;
    s_egl_have_config = false;

    {
        std::lock_guard<std::mutex> glx_lock(s_glx_context_mutex);
        if (s_glx_display)
        {
            const auto destroy = [](const glx_thread_context& entry)
            {
                if (entry.pbuffer && s_glXDestroyPbuffer)
                    s_glXDestroyPbuffer(s_glx_display, entry.pbuffer);
                if (entry.ctx)
                    glXDestroyContext(s_glx_display, entry.ctx);
            };

            for (const auto& entry : s_retired_glx_contexts)
                destroy(entry);

            for (const auto& entry : s_shared_glx_contexts)
                destroy(entry);
        }

        s_retired_glx_contexts.clear();
        s_shared_glx_contexts.clear();
        s_glx_display = nullptr;
        s_glx_main_context = nullptr;
        s_glx_fbconfig = nullptr;
        s_glx_have_config = false;
    }

    s_context_backend = shared_context_backend::none;
    s_gl_initialized = false;
#endif

    // The renderer's frames: its contexts are gone above, and the textures
    // and depth buffers it drew into belong to the share group, so they go
    // from the frontend's context, current here. They were never freed
    // before.
    if (had_gl)
        lr_gl_frames_destroy();
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

void LibretroGSFrame::delete_context(draw_context_t ctx)
{
#ifdef _WIN32
    if (ctx)
    {
        HGLRC hglrc = reinterpret_cast<HGLRC>(ctx);
        if (hglrc != s_main_hglrc)
        {
            std::lock_guard<std::mutex> lock(s_video_mutex);
            auto it = std::find(s_shared_contexts.begin(), s_shared_contexts.end(), hglrc);
            if (it != s_shared_contexts.end())
            {
                wglDeleteContext(hglrc);
                s_shared_contexts.erase(it);
            }
        }
    }
#else
    if (!ctx || ctx == reinterpret_cast<draw_context_t>(1))
        return;

    if (s_context_backend == shared_context_backend::glx)
    {
        // Retired rather than destroyed, for the reason spelled out below.
        std::lock_guard<std::mutex> lock(s_glx_context_mutex);
        GLXContext glx_ctx = reinterpret_cast<GLXContext>(ctx);
        const auto it = std::find_if(s_shared_glx_contexts.begin(), s_shared_glx_contexts.end(),
            [glx_ctx](const glx_thread_context& entry) { return entry.ctx == glx_ctx; });
        if (it != s_shared_glx_contexts.end())
        {
            s_retired_glx_contexts.push_back(*it);
            s_shared_glx_contexts.erase(it);
        }
        return;
    }

    if (s_egl_display == EGL_NO_DISPLAY)
        return;

    EGLContext egl_ctx = reinterpret_cast<EGLContext>(ctx);

    // Do NOT eglDestroyContext() here. This runs on the RSX thread while
    // Emu.Stop() is tearing the emulator down, and destroying the context from
    // under that thread wedges the shutdown: every emulation thread is gone but
    // the join never completes, leaving RetroArch on a frozen frame. Retire the
    // context instead and destroy it in context_destroy(), which the frontend
    // calls on its own thread once the context is really going away.
    std::lock_guard<std::mutex> lock(s_egl_context_mutex);
    if (auto it = std::find(s_shared_egl_contexts.begin(), s_shared_egl_contexts.end(), egl_ctx);
        it != s_shared_egl_contexts.end())
    {
        s_shared_egl_contexts.erase(it);
        s_retired_egl_contexts.push_back(egl_ctx);
    }
#endif
}

draw_context_t LibretroGSFrame::make_context()
{
#ifdef _WIN32
    // Try to get a pre-created shared context from the pool
    {
        std::lock_guard<std::mutex> lock(s_context_pool_mutex);
        if (!s_available_contexts.empty())
        {
            HGLRC ctx = s_available_contexts.back();
            s_available_contexts.pop_back();
            s_shared_contexts.push_back(ctx);
            m_context = reinterpret_cast<draw_context_t>(ctx);
            return m_context;
        }
    }

    if (!s_main_hdc || !s_main_hglrc)
        return nullptr;

    HGLRC shared_context = nullptr;

    if (wglCreateContextAttribsARB_ptr)
    {
        for (const auto& v : kSharedContextVersions)
        {
            int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, v.major,
                WGL_CONTEXT_MINOR_VERSION_ARB, v.minor,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };

            shared_context = wglCreateContextAttribsARB_ptr(s_main_hdc, s_main_hglrc, attribs);
            if (!shared_context)
                shared_context = wglCreateContextAttribsARB_ptr(s_main_hdc, nullptr, attribs);
            if (shared_context)
                break;
        }
    }

    if (!shared_context)
        shared_context = wglCreateContext(s_main_hdc);

    if (shared_context)
    {
        std::lock_guard<std::mutex> lock(s_context_pool_mutex);
        s_shared_contexts.push_back(shared_context);
        m_context = reinterpret_cast<draw_context_t>(shared_context);
        return m_context;
    }

    return nullptr;
#else
    if (s_context_backend == shared_context_backend::egl && s_egl_have_config)
    {
        eglBindAPI(EGL_OPENGL_API);

        // Highest first: this context reads the extension list RPCS3's
        // capability check is built from, so asking for 4.3 here hides direct
        // state access no matter what the frontend's own context is.
        for (const auto& v : kSharedContextVersions)
        {
            const EGLint attribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, v.major,
                EGL_CONTEXT_MINOR_VERSION, v.minor,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };

            if (EGLContext ctx = eglCreateContext(s_egl_display, s_egl_config, s_egl_main_context, attribs);
                ctx != EGL_NO_CONTEXT)
            {
                libretro_video_log.notice("EGL: RSX context created at GL %d.%d.", v.major, v.minor);
                std::lock_guard<std::mutex> lock(s_egl_context_mutex);
                s_shared_egl_contexts.push_back(ctx);
                m_context = reinterpret_cast<draw_context_t>(ctx);
                return m_context;
            }
        }

        libretro_video_log.error("EGL: eglCreateContext failed for every version (0x%x); falling back to a "
                                 "context-less RSX thread.", eglGetError());
    }
    else if (s_context_backend == shared_context_backend::glx && s_glx_have_config)
    {
        // Held across the whole thing, not just the bookkeeping: the X error
        // handler is process-wide, and this is called from the RSX thread and
        // from every pipe compiler thread. Two of them swapping the handler at
        // once would lose the answer to "did the server accept this context".
        std::lock_guard<std::mutex> lock(s_glx_context_mutex);

        for (const auto& v : kSharedContextVersions)
        {
            const int attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, v.major,
                GLX_CONTEXT_MINOR_VERSION_ARB, v.minor,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                None
            };

            s_glx_error_flag = false;
            XErrorHandler old_handler = XSetErrorHandler(glx_error_handler);
            GLXContext ctx = s_glXCreateContextAttribsARB(s_glx_display, s_glx_fbconfig, s_glx_main_context,
                True, attribs);
            XSync(s_glx_display, False);
            XSetErrorHandler(old_handler);

            if (ctx && !s_glx_error_flag)
            {
                const int pbuffer_attribs[] = { GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None };
                s_glx_error_flag = false;
                old_handler = XSetErrorHandler(glx_error_handler);
                GLXPbuffer pbuffer = s_glXCreatePbuffer(s_glx_display, s_glx_fbconfig, pbuffer_attribs);
                XSync(s_glx_display, False);
                XSetErrorHandler(old_handler);

                if (!pbuffer || s_glx_error_flag)
                {
                    libretro_video_log.error("GLX: the context was created but its 1x1 pbuffer was not; there is "
                                             "nothing to make it current on.");
                    glXDestroyContext(s_glx_display, ctx);
                    break;
                }

                libretro_video_log.notice("GLX: context created at GL %d.%d.", v.major, v.minor);
                s_shared_glx_contexts.push_back({ ctx, pbuffer });
                m_context = reinterpret_cast<draw_context_t>(ctx);
                return m_context;
            }

            if (ctx)
                glXDestroyContext(s_glx_display, ctx);
        }

        libretro_video_log.error("GLX: could not create a shared context at any version; falling back to a "
                                 "context-less RSX thread.");
    }

    m_context = reinterpret_cast<draw_context_t>(1);
    return m_context;
#endif
}

void LibretroGSFrame::set_current(draw_context_t ctx)
{
#ifdef _WIN32
    if (ctx && s_main_hdc)
    {
        HGLRC hglrc = reinterpret_cast<HGLRC>(ctx);
        wglMakeCurrent(s_main_hdc, hglrc);
    }
#else
    // The dummy handle from the fallback path is not a real context.
    if (!ctx || ctx == reinterpret_cast<draw_context_t>(1))
        return;

    if (s_context_backend == shared_context_backend::glx)
    {
        GLXContext glx_ctx = reinterpret_cast<GLXContext>(ctx);
        GLXPbuffer pbuffer = 0;
        {
            std::lock_guard<std::mutex> lock(s_glx_context_mutex);
            for (const auto& entry : s_shared_glx_contexts)
            {
                if (entry.ctx == glx_ctx)
                {
                    pbuffer = entry.pbuffer;
                    break;
                }
            }
        }

        if (!pbuffer)
        {
            libretro_video_log.error("GLX: no drawable for this context; it was never created here.");
            return;
        }

        // Current on this context's own 1x1 pbuffer rather than the frontend's
        // window, which belongs to the video thread. RSX draws into FBOs, so
        // what the drawable is never matters.
        if (!s_glXMakeContextCurrent(s_glx_display, pbuffer, pbuffer, glx_ctx))
        {
            libretro_video_log.error("GLX: glXMakeContextCurrent failed.");
        }
        return;
    }

    if (s_egl_display == EGL_NO_DISPLAY)
        return;

    eglBindAPI(EGL_OPENGL_API);

    // Surfaceless: RSX renders into FBOs and the frontend does the presenting.
    if (!eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, reinterpret_cast<EGLContext>(ctx)))
    {
        libretro_video_log.error("EGL: eglMakeCurrent failed (0x%x).", eglGetError());
    }
#endif
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
#ifdef _WIN32
    return nullptr;
#else
    return {};
#endif
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
