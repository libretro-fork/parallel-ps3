/* The renderer's OpenGL contexts for a frontend without
 * RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE: see
 * libretro_gl_fallback.h. C89.
 *
 * Every window system function is looked up at run time among the
 * frontend's own libraries - opengl32.dll's on Windows, EGL's or GLX's
 * (RTLD_DEFAULT) elsewhere - so the core links none of them and needs none
 * of their headers: the few types and constants are declared here. */

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* RTLD_DEFAULT */
#endif
#define LR_GL_FALLBACK_UNIX 1
#endif

#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(LR_GL_FALLBACK_UNIX)
#include <dlfcn.h>
#endif

#include "libretro_gl_fallback.h"

#define LR_FALLBACK_CONTEXTS 16

/* Highest first: the renderer's capability check reads its extension list
 * from these contexts, and a 4.3 one on Mesa hides direct state access,
 * which 4.5 made core */
static const int lr_versions[][2] = { { 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 } };
#define LR_VERSIONS ((int)(sizeof(lr_versions) / sizeof(lr_versions[0])))

struct retro_hw_shared_context
{
   void         *ctx;
   unsigned long pbuffer; /* GLX: its own 1x1 drawable */
};

static struct retro_hw_shared_context lr_made[LR_FALLBACK_CONTEXTS];
static int lr_made_count;

#ifdef _WIN32
/* ---- WGL ---- */

typedef HGLRC (WINAPI *lr_wgl_create_attribs_t)(HDC, HGLRC, const int *);
typedef PROC  (WINAPI *lr_wgl_get_proc_t)(LPCSTR);
typedef HDC   (WINAPI *lr_wgl_current_dc_t)(void);
typedef HGLRC (WINAPI *lr_wgl_current_context_t)(void);
typedef BOOL  (WINAPI *lr_wgl_make_current_t)(HDC, HGLRC);
typedef BOOL  (WINAPI *lr_wgl_delete_t)(HGLRC);

#define LR_WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define LR_WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define LR_WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define LR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001

static lr_wgl_create_attribs_t  lr_wgl_create_attribs;
static lr_wgl_current_dc_t      lr_wgl_current_dc;
static lr_wgl_current_context_t lr_wgl_current_context;
static lr_wgl_make_current_t    lr_wgl_make_current;
static lr_wgl_delete_t          lr_wgl_delete;
static HDC   lr_wgl_dc;
static HGLRC lr_wgl_main;

static int lr_wgl_init(void)
{
   HMODULE gl32 = GetModuleHandleA("opengl32.dll");
   lr_wgl_get_proc_t get_proc;
   if (!gl32)
      return 0;
   get_proc               = (lr_wgl_get_proc_t)GetProcAddress(gl32, "wglGetProcAddress");
   lr_wgl_current_dc      = (lr_wgl_current_dc_t)GetProcAddress(gl32, "wglGetCurrentDC");
   lr_wgl_current_context = (lr_wgl_current_context_t)GetProcAddress(gl32, "wglGetCurrentContext");
   lr_wgl_make_current    = (lr_wgl_make_current_t)GetProcAddress(gl32, "wglMakeCurrent");
   lr_wgl_delete          = (lr_wgl_delete_t)GetProcAddress(gl32, "wglDeleteContext");
   if (!get_proc || !lr_wgl_current_dc || !lr_wgl_current_context || !lr_wgl_make_current || !lr_wgl_delete)
      return 0;
   lr_wgl_dc   = lr_wgl_current_dc();
   lr_wgl_main = lr_wgl_current_context();
   /* A context's function: looked up with the frontend's current */
   lr_wgl_create_attribs = (lr_wgl_create_attribs_t)get_proc("wglCreateContextAttribsARB");
   return lr_wgl_dc && lr_wgl_main && lr_wgl_create_attribs;
}

static void *lr_wgl_create(unsigned long *pbuffer)
{
   HGLRC ctx = NULL;
   int i;
   (void)pbuffer;
   /* Shared with the frontend's context while that is current on no
    * thread: a busy one is refused (ERROR_BUSY) by some drivers */
   if (!lr_wgl_make_current(NULL, NULL))
      return NULL;
   for (i = 0; i < LR_VERSIONS && !ctx; i++)
   {
      int attribs[7];
      attribs[0] = LR_WGL_CONTEXT_MAJOR_VERSION_ARB;
      attribs[1] = lr_versions[i][0];
      attribs[2] = LR_WGL_CONTEXT_MINOR_VERSION_ARB;
      attribs[3] = lr_versions[i][1];
      attribs[4] = LR_WGL_CONTEXT_PROFILE_MASK_ARB;
      attribs[5] = LR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
      attribs[6] = 0;
      ctx = lr_wgl_create_attribs(lr_wgl_dc, lr_wgl_main, attribs);
   }
   lr_wgl_make_current(lr_wgl_dc, lr_wgl_main);
   return ctx;
}

static int lr_wgl_bind(struct retro_hw_shared_context *c, int current)
{
   if (!current)
      return lr_wgl_make_current(NULL, NULL) ? 1 : 0;
   return lr_wgl_make_current(lr_wgl_dc, (HGLRC)c->ctx) ? 1 : 0;
}

static void lr_wgl_destroy(struct retro_hw_shared_context *c)
{
   lr_wgl_delete((HGLRC)c->ctx);
}

#elif defined(LR_GL_FALLBACK_UNIX)
/* ---- EGL ---- */

#define LR_EGL_CONFIG_ID                       0x3028
#define LR_EGL_NONE                            0x3038
#define LR_EGL_OPENGL_API                      0x30A2
#define LR_EGL_CONTEXT_MAJOR_VERSION           0x3098
#define LR_EGL_CONTEXT_MINOR_VERSION           0x30FB
#define LR_EGL_CONTEXT_OPENGL_PROFILE_MASK     0x30FD
#define LR_EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x0001

typedef void *(*lr_egl_current_display_t)(void);
typedef void *(*lr_egl_current_context_t)(void);
typedef unsigned (*lr_egl_query_context_t)(void *, void *, int, int *);
typedef unsigned (*lr_egl_choose_config_t)(void *, const int *, void **, int, int *);
typedef unsigned (*lr_egl_bind_api_t)(unsigned);
typedef void *(*lr_egl_create_context_t)(void *, void *, void *, const int *);
typedef unsigned (*lr_egl_make_current_t)(void *, void *, void *, void *);
typedef unsigned (*lr_egl_destroy_context_t)(void *, void *);

static lr_egl_bind_api_t        lr_egl_bind_api;
static lr_egl_create_context_t  lr_egl_create_context;
static lr_egl_make_current_t    lr_egl_make_current;
static lr_egl_destroy_context_t lr_egl_destroy_context;
static void *lr_egl_display;
static void *lr_egl_main;
static void *lr_egl_config;

/* ---- GLX ---- */

#define LR_GLX_SCREEN                      0x800C
#define LR_GLX_FBCONFIG_ID                 0x8013
#define LR_GLX_DRAWABLE_TYPE               0x8010
#define LR_GLX_RENDER_TYPE                 0x8011
#define LR_GLX_PBUFFER_BIT                 0x0004
#define LR_GLX_RGBA_BIT                    0x0001
#define LR_GLX_PBUFFER_HEIGHT              0x8040
#define LR_GLX_PBUFFER_WIDTH               0x8041
#define LR_GLX_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define LR_GLX_CONTEXT_MINOR_VERSION_ARB   0x2092
#define LR_GLX_CONTEXT_PROFILE_MASK_ARB    0x9126
#define LR_GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001

typedef int (*lr_x_error_handler_t)(void *, void *);
typedef void *(*lr_glx_current_display_t)(void);
typedef void *(*lr_glx_current_context_t)(void);
typedef int (*lr_glx_query_context_t)(void *, void *, int, int *);
typedef void **(*lr_glx_choose_fbconfig_t)(void *, int, const int *, int *);
typedef int (*lr_glx_get_fbconfig_attrib_t)(void *, void *, int, int *);
typedef void (*(*lr_glx_get_proc_t)(const unsigned char *))(void);
typedef void *(*lr_glx_create_attribs_t)(void *, void *, void *, int, const int *);
typedef unsigned long (*lr_glx_create_pbuffer_t)(void *, void *, const int *);
typedef void (*lr_glx_destroy_pbuffer_t)(void *, unsigned long);
typedef int (*lr_glx_make_context_current_t)(void *, unsigned long, unsigned long, void *);
typedef void (*lr_glx_destroy_context_t)(void *, void *);
typedef int (*lr_x_free_t)(void *);
typedef int (*lr_x_sync_t)(void *, int);
typedef lr_x_error_handler_t (*lr_x_set_error_handler_t)(lr_x_error_handler_t);

static lr_glx_create_attribs_t       lr_glx_create_attribs;
static lr_glx_create_pbuffer_t       lr_glx_create_pbuffer;
static lr_glx_destroy_pbuffer_t      lr_glx_destroy_pbuffer;
static lr_glx_make_context_current_t lr_glx_make_context_current;
static lr_glx_destroy_context_t      lr_glx_destroy_context;
static lr_x_sync_t                   lr_x_sync;
static lr_x_set_error_handler_t      lr_x_set_error_handler;
static void *lr_glx_display;
static void *lr_glx_main;
static void *lr_glx_config;

static int lr_unix_backend; /* 1 EGL, 2 GLX */

typedef void (*lr_fn_t)(void);

/* dlsym's answer as a function pointer, without the object-to-function
 * cast ISO C does not have */
static lr_fn_t lr_sym(const char *name)
{
   void   *p = dlsym(RTLD_DEFAULT, name);
   lr_fn_t f;
   memcpy(&f, &p, sizeof(f));
   return f;
}

/* The frontend's context is EGL's when EGL says one is current */
static int lr_egl_init(void)
{
   lr_egl_current_display_t current_display = (lr_egl_current_display_t)lr_sym("eglGetCurrentDisplay");
   lr_egl_current_context_t current_context = (lr_egl_current_context_t)lr_sym("eglGetCurrentContext");
   lr_egl_query_context_t   query_context   = (lr_egl_query_context_t)lr_sym("eglQueryContext");
   lr_egl_choose_config_t   choose_config   = (lr_egl_choose_config_t)lr_sym("eglChooseConfig");
   int config_id = 0;
   int count     = 0;
   int attribs[3];

   lr_egl_bind_api        = (lr_egl_bind_api_t)lr_sym("eglBindAPI");
   lr_egl_create_context  = (lr_egl_create_context_t)lr_sym("eglCreateContext");
   lr_egl_make_current    = (lr_egl_make_current_t)lr_sym("eglMakeCurrent");
   lr_egl_destroy_context = (lr_egl_destroy_context_t)lr_sym("eglDestroyContext");
   if (!current_display || !current_context || !query_context || !choose_config
         || !lr_egl_bind_api || !lr_egl_create_context || !lr_egl_make_current
         || !lr_egl_destroy_context)
      return 0;

   lr_egl_display = current_display();
   lr_egl_main    = current_context();
   if (!lr_egl_display || !lr_egl_main)
      return 0;
   if (!query_context(lr_egl_display, lr_egl_main, LR_EGL_CONFIG_ID, &config_id))
      return 0;
   attribs[0] = LR_EGL_CONFIG_ID;
   attribs[1] = config_id;
   attribs[2] = LR_EGL_NONE;
   return choose_config(lr_egl_display, attribs, &lr_egl_config, 1, &count) && count == 1;
}

static int lr_x_error;
static int lr_x_error_handler(void *display, void *event)
{
   (void)display;
   (void)event;
   lr_x_error = 1;
   return 0;
}

/* A GLX context has no surfaceless make-current: each gets a 1x1 pbuffer,
 * so the config has to be able to back one - the frontend's own, or any
 * RGBA one on its screen (objects are shared across configs of one
 * renderer). */
static int lr_glx_init(void)
{
   lr_glx_current_display_t     current_display = (lr_glx_current_display_t)lr_sym("glXGetCurrentDisplay");
   lr_glx_current_context_t     current_context = (lr_glx_current_context_t)lr_sym("glXGetCurrentContext");
   lr_glx_query_context_t       query_context   = (lr_glx_query_context_t)lr_sym("glXQueryContext");
   lr_glx_choose_fbconfig_t     choose_fbconfig = (lr_glx_choose_fbconfig_t)lr_sym("glXChooseFBConfig");
   lr_glx_get_fbconfig_attrib_t get_attrib      = (lr_glx_get_fbconfig_attrib_t)lr_sym("glXGetFBConfigAttrib");
   lr_glx_get_proc_t            get_proc        = (lr_glx_get_proc_t)lr_sym("glXGetProcAddressARB");
   lr_x_free_t                  x_free          = (lr_x_free_t)lr_sym("XFree");
   int screen = 0, id = 0, count = 0, type = 0;
   void **configs;
   int attribs[5];

   lr_glx_create_pbuffer       = (lr_glx_create_pbuffer_t)lr_sym("glXCreatePbuffer");
   lr_glx_destroy_pbuffer      = (lr_glx_destroy_pbuffer_t)lr_sym("glXDestroyPbuffer");
   lr_glx_make_context_current = (lr_glx_make_context_current_t)lr_sym("glXMakeContextCurrent");
   lr_glx_destroy_context      = (lr_glx_destroy_context_t)lr_sym("glXDestroyContext");
   lr_x_sync                   = (lr_x_sync_t)lr_sym("XSync");
   lr_x_set_error_handler      = (lr_x_set_error_handler_t)lr_sym("XSetErrorHandler");
   if (!current_display || !current_context || !query_context || !choose_fbconfig
         || !get_attrib || !get_proc || !x_free || !lr_glx_create_pbuffer
         || !lr_glx_destroy_pbuffer || !lr_glx_make_context_current
         || !lr_glx_destroy_context || !lr_x_sync || !lr_x_set_error_handler)
      return 0;

   lr_glx_display = current_display();
   lr_glx_main    = current_context();
   lr_glx_create_attribs = (lr_glx_create_attribs_t)get_proc((const unsigned char *)"glXCreateContextAttribsARB");
   if (!lr_glx_display || !lr_glx_main || !lr_glx_create_attribs)
      return 0;
   query_context(lr_glx_display, lr_glx_main, LR_GLX_SCREEN, &screen);

   lr_glx_config = NULL;
   if (query_context(lr_glx_display, lr_glx_main, LR_GLX_FBCONFIG_ID, &id) == 0 && id)
   {
      attribs[0] = LR_GLX_FBCONFIG_ID;
      attribs[1] = id;
      attribs[2] = 0;
      if ((configs = choose_fbconfig(lr_glx_display, screen, attribs, &count)))
      {
         if (count > 0 && get_attrib(lr_glx_display, configs[0], LR_GLX_DRAWABLE_TYPE, &type) == 0
               && (type & LR_GLX_PBUFFER_BIT))
            lr_glx_config = configs[0];
         x_free(configs);
      }
   }
   if (!lr_glx_config)
   {
      attribs[0] = LR_GLX_RENDER_TYPE;
      attribs[1] = LR_GLX_RGBA_BIT;
      attribs[2] = LR_GLX_DRAWABLE_TYPE;
      attribs[3] = LR_GLX_PBUFFER_BIT;
      attribs[4] = 0;
      if ((configs = choose_fbconfig(lr_glx_display, screen, attribs, &count)))
      {
         if (count > 0)
            lr_glx_config = configs[0];
         x_free(configs);
      }
   }
   return lr_glx_config != NULL;
}

static void *lr_unix_create(unsigned long *pbuffer)
{
   int i;
   if (lr_unix_backend == 1)
   {
      lr_egl_bind_api(LR_EGL_OPENGL_API);
      for (i = 0; i < LR_VERSIONS; i++)
      {
         void *ctx;
         int attribs[7];
         attribs[0] = LR_EGL_CONTEXT_MAJOR_VERSION;
         attribs[1] = lr_versions[i][0];
         attribs[2] = LR_EGL_CONTEXT_MINOR_VERSION;
         attribs[3] = lr_versions[i][1];
         attribs[4] = LR_EGL_CONTEXT_OPENGL_PROFILE_MASK;
         attribs[5] = LR_EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
         attribs[6] = LR_EGL_NONE;
         if ((ctx = lr_egl_create_context(lr_egl_display, lr_egl_config, lr_egl_main, attribs)))
            return ctx;
      }
      return NULL;
   }

   /* GLX reports a failed context or pbuffer through the X error handler,
    * not the return value: one installed for the length of each call */
   for (i = 0; i < LR_VERSIONS; i++)
   {
      lr_x_error_handler_t old;
      void *ctx;
      int attribs[7];
      int pb_attribs[5];
      attribs[0] = LR_GLX_CONTEXT_MAJOR_VERSION_ARB;
      attribs[1] = lr_versions[i][0];
      attribs[2] = LR_GLX_CONTEXT_MINOR_VERSION_ARB;
      attribs[3] = lr_versions[i][1];
      attribs[4] = LR_GLX_CONTEXT_PROFILE_MASK_ARB;
      attribs[5] = LR_GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
      attribs[6] = 0;

      lr_x_error = 0;
      old = lr_x_set_error_handler(lr_x_error_handler);
      ctx = lr_glx_create_attribs(lr_glx_display, lr_glx_config, lr_glx_main, 1, attribs);
      lr_x_sync(lr_glx_display, 0);
      lr_x_set_error_handler(old);
      if (!ctx || lr_x_error)
      {
         if (ctx)
            lr_glx_destroy_context(lr_glx_display, ctx);
         continue;
      }

      pb_attribs[0] = LR_GLX_PBUFFER_WIDTH;
      pb_attribs[1] = 1;
      pb_attribs[2] = LR_GLX_PBUFFER_HEIGHT;
      pb_attribs[3] = 1;
      pb_attribs[4] = 0;
      lr_x_error = 0;
      old = lr_x_set_error_handler(lr_x_error_handler);
      *pbuffer = lr_glx_create_pbuffer(lr_glx_display, lr_glx_config, pb_attribs);
      lr_x_sync(lr_glx_display, 0);
      lr_x_set_error_handler(old);
      if (!*pbuffer || lr_x_error)
      {
         lr_glx_destroy_context(lr_glx_display, ctx);
         return NULL;
      }
      return ctx;
   }
   return NULL;
}

static int lr_unix_bind(struct retro_hw_shared_context *c, int current)
{
   if (lr_unix_backend == 1)
   {
      if (!current)
         return lr_egl_make_current(lr_egl_display, NULL, NULL, NULL) ? 1 : 0;
      lr_egl_bind_api(LR_EGL_OPENGL_API); /* a new thread starts on ES */
      /* Surfaceless: the renderer draws into framebuffers of its own */
      return lr_egl_make_current(lr_egl_display, NULL, NULL, c->ctx) ? 1 : 0;
   }
   if (!current)
      return lr_glx_make_context_current(lr_glx_display, 0, 0, NULL) ? 1 : 0;
   return lr_glx_make_context_current(lr_glx_display, c->pbuffer, c->pbuffer, c->ctx) ? 1 : 0;
}

static void lr_unix_destroy(struct retro_hw_shared_context *c)
{
   if (lr_unix_backend == 1)
   {
      lr_egl_destroy_context(lr_egl_display, c->ctx);
      return;
   }
   lr_glx_destroy_pbuffer(lr_glx_display, c->pbuffer);
   lr_glx_destroy_context(lr_glx_display, c->ctx);
}
#endif

static struct retro_hw_shared_context *RETRO_CALLCONV lr_fallback_create(void)
{
   struct retro_hw_shared_context *c;
   if (lr_made_count >= LR_FALLBACK_CONTEXTS)
      return NULL;
   c = &lr_made[lr_made_count];
   memset(c, 0, sizeof(*c));
#ifdef _WIN32
   c->ctx = lr_wgl_create(&c->pbuffer);
#elif defined(LR_GL_FALLBACK_UNIX)
   c->ctx = lr_unix_create(&c->pbuffer);
#endif
   if (!c->ctx)
      return NULL;
   lr_made_count++;
   return c;
}

static bool RETRO_CALLCONV lr_fallback_make_current(struct retro_hw_shared_context *c, bool current)
{
   if (!c)
      return false;
#ifdef _WIN32
   return lr_wgl_bind(c, current) != 0;
#elif defined(LR_GL_FALLBACK_UNIX)
   return lr_unix_bind(c, current) != 0;
#else
   (void)current;
   return false;
#endif
}

static void RETRO_CALLCONV lr_fallback_destroy(struct retro_hw_shared_context *c)
{
   if (!c || !c->ctx)
      return;
#ifdef _WIN32
   lr_wgl_destroy(c);
#elif defined(LR_GL_FALLBACK_UNIX)
   lr_unix_destroy(c);
#endif
   c->ctx = NULL;
}

static const struct retro_hw_shared_context_interface lr_fallback_iface = {
   RETRO_HW_SHARED_CONTEXT_INTERFACE_VERSION,
   lr_fallback_create,
   lr_fallback_make_current,
   lr_fallback_destroy
};

const struct retro_hw_shared_context_interface *lr_gl_fallback_interface(void)
{
   int i;
   /* Everything made before is gone by now (context_destroy) */
   for (i = 0; i < LR_FALLBACK_CONTEXTS; i++)
      lr_made[i].ctx = NULL;
   lr_made_count = 0;
#ifdef _WIN32
   if (lr_wgl_init())
      return &lr_fallback_iface;
#elif defined(LR_GL_FALLBACK_UNIX)
   lr_unix_backend = 0;
   if (lr_egl_init())
      lr_unix_backend = 1;
   else if (lr_glx_init())
      lr_unix_backend = 2;
   if (lr_unix_backend)
      return &lr_fallback_iface;
#endif
   return NULL;
}
