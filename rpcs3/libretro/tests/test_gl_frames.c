/* The OpenGL renderer's frames to the frontend (libretro/libretro_gl_frames.c),
 * on Mesa's software OpenGL (llvmpipe): two contexts sharing their textures,
 * each current on its own thread, as the renderer's and the frontend's are.
 *
 * The renderer draws each frame in two steps - all of it in one colour, then
 * its two halves in the frame's own colour - and the frontend copies every
 * frame it takes into its own framebuffer and reads it back:
 *
 * - every frame shown is whole: both halves the frame's colour, no frame
 *   half drawn, none mixed with the next one drawn over it;
 * - every frame shown is newer than the last, at the size it was drawn
 *   (the size changes now and then);
 * - neither thread waits for the other on the CPU: the GPU waits on fences;
 * - the teardown frees the frames' textures, and starts over.
 *
 * This is the protocol on a software rasteriser, not on a GPU's driver.
 *
 * Usage: test_gl_frames (skips, saying so, when there is no EGL with
 * surfaceless contexts) */

#include <pthread.h>
#include <sched.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <retro_atomic.h>

#include "../libretro_gl_frames.h"
#include "test_common.h"

#define FRAMES 600

static EGLDisplay dpy;
static EGLContext show_ctx, draw_ctx;
static retro_atomic_int_t done;

typedef void   (GLAPIENTRY *clear_color_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (GLAPIENTRY *clear_fn)(GLbitfield);
typedef void   (GLAPIENTRY *enable_fn)(GLenum);
typedef void   (GLAPIENTRY *scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void   (GLAPIENTRY *viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void   (GLAPIENTRY *read_pixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (GLAPIENTRY *get_error_fn)(void);

static clear_color_fn p_clear_color;
static clear_fn       p_clear;
static enable_fn      p_enable, p_disable;
static scissor_fn     p_scissor;
static viewport_fn    p_viewport;
static read_pixels_fn p_read_pixels;
static get_error_fn   p_get_error;
static void (GLAPIENTRY *p_bind_framebuffer)(GLenum, GLuint);

#define PROC(type, name) (type)eglGetProcAddress(name)

static void load_procs(void)
{
   struct lr_gl_frames_procs p;
   memset(&p, 0, sizeof(p));
   p.gen_textures             = PROC(void (GLAPIENTRY*)(GLsizei, GLuint*), "glGenTextures");
   p.delete_textures          = PROC(void (GLAPIENTRY*)(GLsizei, const GLuint*), "glDeleteTextures");
   p.bind_texture             = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindTexture");
   p.tex_image_2d             = PROC(void (GLAPIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*), "glTexImage2D");
   p.tex_parameteri           = PROC(void (GLAPIENTRY*)(GLenum, GLenum, GLint), "glTexParameteri");
   p.gen_framebuffers         = PROC(void (GLAPIENTRY*)(GLsizei, GLuint*), "glGenFramebuffers");
   p.delete_framebuffers      = PROC(void (GLAPIENTRY*)(GLsizei, const GLuint*), "glDeleteFramebuffers");
   p.bind_framebuffer         = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindFramebuffer");
   p.framebuffer_texture_2d   = PROC(void (GLAPIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint), "glFramebufferTexture2D");
   p.gen_renderbuffers        = PROC(void (GLAPIENTRY*)(GLsizei, GLuint*), "glGenRenderbuffers");
   p.delete_renderbuffers     = PROC(void (GLAPIENTRY*)(GLsizei, const GLuint*), "glDeleteRenderbuffers");
   p.bind_renderbuffer        = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindRenderbuffer");
   p.renderbuffer_storage     = PROC(void (GLAPIENTRY*)(GLenum, GLenum, GLsizei, GLsizei), "glRenderbufferStorage");
   p.framebuffer_renderbuffer = PROC(void (GLAPIENTRY*)(GLenum, GLenum, GLenum, GLuint), "glFramebufferRenderbuffer");
   p.blit_framebuffer         = PROC(void (GLAPIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum), "glBlitFramebuffer");
   p.fence_sync               = PROC(GLsync (GLAPIENTRY*)(GLenum, GLbitfield), "glFenceSync");
   p.wait_sync                = PROC(void (GLAPIENTRY*)(GLsync, GLbitfield, GLuint64), "glWaitSync");
   p.delete_sync              = PROC(void (GLAPIENTRY*)(GLsync), "glDeleteSync");
   p.flush                    = PROC(void (GLAPIENTRY*)(void), "glFlush");
   lr_gl_frames_set_procs(&p);

   p_clear_color = PROC(clear_color_fn, "glClearColor");
   p_clear       = PROC(clear_fn, "glClear");
   p_enable      = PROC(enable_fn, "glEnable");
   p_disable     = PROC(enable_fn, "glDisable");
   p_scissor     = PROC(scissor_fn, "glScissor");
   p_viewport    = PROC(viewport_fn, "glViewport");
   p_read_pixels = PROC(read_pixels_fn, "glReadPixels");
   p_get_error   = PROC(get_error_fn, "glGetError");
   p_bind_framebuffer = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindFramebuffer");
}

/* Frame n's colour: n in red and green, so a frame is told from the next */
static void frame_color(unsigned n, float *r, float *g)
{
   *r = (float)(n & 0xff) / 255.0f;
   *g = (float)((n >> 8) & 0xff) / 255.0f;
}

static void *renderer(void *arg)
{
   unsigned n;
   (void)arg;
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, draw_ctx);
   for (n = 1; n <= FRAMES; n++)
   {
      int w = (n % 97 < 2) ? 96 : 64, h = (n % 97 < 2) ? 48 : 32;
      float r, g;
      GLuint fbo = lr_gl_frames_draw_fbo(w, h);
      if (!fbo)
         abort();
      frame_color(n, &r, &g);
      /* lr_gl_frames_draw_fbo leaves no framebuffer bound: bind it, as the
       * renderer does before it presents */
      p_bind_framebuffer(GL_FRAMEBUFFER, fbo);
      p_viewport(0, 0, w, h);
      /* All of it blue, then each half the frame's colour */
      p_clear_color(0.0f, 0.0f, 1.0f, 1.0f);
      p_clear(GL_COLOR_BUFFER_BIT);
      p_enable(GL_SCISSOR_TEST);
      p_clear_color(r, g, 0.0f, 1.0f);
      p_scissor(0, 0, w / 2, h);
      p_clear(GL_COLOR_BUFFER_BIT);
      p_scissor(w / 2, 0, w - w / 2, h);
      p_clear(GL_COLOR_BUFFER_BIT);
      p_disable(GL_SCISSOR_TEST);
      p_bind_framebuffer(GL_FRAMEBUFFER, 0);
      lr_gl_frames_publish();
   }
   retro_atomic_store_release_int(&done, 1);
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   return NULL;
}

int main(void)
{
   static const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
   static const EGLint ctx_attribs[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
   PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
   EGLConfig config;
   EGLint count = 0;
   pthread_t t;
   GLuint dst_tex = 0, dst_fbo = 0;
   unsigned shown = 0, last = 0, torn = 0, older = 0, wrong_size = 0;
   int w_end = 0, h_end = 0;

   get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
   dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)
         || !eglBindAPI(EGL_OPENGL_API)
         || !eglChooseConfig(dpy, config_attribs, &config, 1, &count) || count < 1)
   {
      printf("gl frames: no surfaceless EGL here, skipped\n");
      return 0;
   }
   show_ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
   draw_ctx = eglCreateContext(dpy, config, show_ctx, ctx_attribs);
   if (show_ctx == EGL_NO_CONTEXT || draw_ctx == EGL_NO_CONTEXT
         || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, show_ctx))
   {
      printf("gl frames: no OpenGL 3.3 core context here, skipped\n");
      return 0;
   }
   load_procs();

   /* The frontend's framebuffer */
   {
      void (GLAPIENTRY *gen_textures)(GLsizei, GLuint*) = PROC(void (GLAPIENTRY*)(GLsizei, GLuint*), "glGenTextures");
      void (GLAPIENTRY *bind_texture)(GLenum, GLuint) = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindTexture");
      void (GLAPIENTRY *tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) =
         PROC(void (GLAPIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*), "glTexImage2D");
      void (GLAPIENTRY *gen_framebuffers)(GLsizei, GLuint*) = PROC(void (GLAPIENTRY*)(GLsizei, GLuint*), "glGenFramebuffers");
      void (GLAPIENTRY *bind_framebuffer)(GLenum, GLuint) = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindFramebuffer");
      void (GLAPIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint) =
         PROC(void (GLAPIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint), "glFramebufferTexture2D");
      gen_textures(1, &dst_tex);
      bind_texture(GL_TEXTURE_2D, dst_tex);
      tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gen_framebuffers(1, &dst_fbo);
      bind_framebuffer(GL_FRAMEBUFFER, dst_fbo);
      framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_tex, 0);
      bind_framebuffer(GL_FRAMEBUFFER, 0);
   }

   retro_atomic_int_init(&done, 0);
   pthread_create(&t, NULL, renderer, NULL);
   for (;;)
   {
      int finished = retro_atomic_load_acquire_int(&done);
      int w = 0, h = 0;
      unsigned char left[4], right[4];
      unsigned n_left, n_right;
      void (GLAPIENTRY *bind_framebuffer)(GLenum, GLuint) = PROC(void (GLAPIENTRY*)(GLenum, GLuint), "glBindFramebuffer");

      if (!lr_gl_frames_blit(dst_fbo, &w, &h))
      {
         if (finished)
            break;
         sched_yield();
         continue;
      }

      /* Read back: the test's own check, a CPU wait the core never does */
      bind_framebuffer(GL_READ_FRAMEBUFFER, dst_fbo);
      p_read_pixels(1, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
      p_read_pixels(w - 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
      bind_framebuffer(GL_READ_FRAMEBUFFER, 0);

      n_left  = (unsigned)left[0] | ((unsigned)left[1] << 8);
      n_right = (unsigned)right[0] | ((unsigned)right[1] << 8);
      if (left[2] || right[2] || n_left != n_right || !n_left)
         torn++;
      else
      {
         if (n_left <= last)
            older++;
         if ((n_left % 97 < 2) != (w == 96))
            wrong_size++;
         last = n_left;
      }
      shown++;
   }
   pthread_join(t, NULL);

   TEST_CHECK(torn == 0);
   TEST_CHECK(older == 0);
   TEST_CHECK(wrong_size == 0);
   TEST_CHECK(shown > 0);
   TEST_CHECK(last == FRAMES);
   printf("gl frames: %u of %u frames shown, %u torn, %u older than the last, %u at the wrong size\n",
         shown, FRAMES, torn, older, wrong_size);

   /* As the core tears down: the renderer's context, then everything else
    * from the frontend's - and again, to start over */
   {
      GLboolean (GLAPIENTRY *is_texture)(GLuint) = PROC(GLboolean (GLAPIENTRY*)(GLuint), "glIsTexture");
      GLuint name;
      unsigned before = 0, after = 0;
      for (name = 1; name < 64; name++)
         before += is_texture(name) ? 1 : 0;
      eglDestroyContext(dpy, draw_ctx);
      lr_gl_frames_destroy();
      for (name = 1; name < 64; name++)
         after += is_texture(name) ? 1 : 0;
      /* the three frames' and the frontend's own; then the frontend's alone */
      TEST_CHECK(before == 4);
      TEST_CHECK(after == 1);
      printf("gl frames: %u textures before the teardown, %u after\n", before, after);
   }
   TEST_CHECK(!lr_gl_frames_blit(dst_fbo, &w_end, &h_end));
   lr_gl_frames_destroy();
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(dpy, show_ctx);
   eglTerminate(dpy);

   if (test_failures)
      printf("gl frames: FAIL\n");
   return test_failures ? 1 : 0;
}
