/* The renderer's OpenGL contexts for a frontend without
 * RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE
 * (libretro/libretro_gl_fallback.c): made through the window system behind
 * the frontend's current context, here EGL's:
 *
 * - with the frontend's context current it finds EGL and makes contexts in
 *   that context's share group, leaving the frontend's context current;
 * - one made current on a thread of its own draws into a texture there,
 *   and the frontend's context sees the drawing (the share group);
 * - with no context current there is nothing to go by: none.
 *
 * Usage: test_gl_fallback (skips, saying so, without surfaceless EGL and
 * OpenGL 4.3, as on a machine with no Mesa) */

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "../libretro_gl_fallback.h"
#include "../libretro_gl_frames.h" /* the GL types */
#include "test_common.h"

static const struct retro_hw_shared_context_interface *iface;
static struct retro_hw_shared_context *worker_ctx;
static GLuint texture;
static GLsync fence;
static int worker_ok;

#define PROC(type, name) (type)eglGetProcAddress(name)

static void *worker(void *arg)
{
   void (APIENTRY *gen_textures)(GLsizei, GLuint*) = PROC(void (APIENTRY*)(GLsizei, GLuint*), "glGenTextures");
   void (APIENTRY *bind_texture)(GLenum, GLuint) = PROC(void (APIENTRY*)(GLenum, GLuint), "glBindTexture");
   void (APIENTRY *tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) =
      PROC(void (APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*), "glTexImage2D");
   void (APIENTRY *gen_framebuffers)(GLsizei, GLuint*) = PROC(void (APIENTRY*)(GLsizei, GLuint*), "glGenFramebuffers");
   void (APIENTRY *bind_framebuffer)(GLenum, GLuint) = PROC(void (APIENTRY*)(GLenum, GLuint), "glBindFramebuffer");
   void (APIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint) =
      PROC(void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint), "glFramebufferTexture2D");
   void (APIENTRY *viewport)(GLint, GLint, GLsizei, GLsizei) = PROC(void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei), "glViewport");
   void (APIENTRY *clear_color)(GLfloat, GLfloat, GLfloat, GLfloat) = PROC(void (APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat), "glClearColor");
   void (APIENTRY *clear)(GLbitfield) = PROC(void (APIENTRY*)(GLbitfield), "glClear");
   GLsync (APIENTRY *fence_sync)(GLenum, GLbitfield) = PROC(GLsync (APIENTRY*)(GLenum, GLbitfield), "glFenceSync");
   void (APIENTRY *flush)(void) = PROC(void (APIENTRY*)(void), "glFlush");
   const GLubyte *(APIENTRY *get_string)(GLenum) = PROC(const GLubyte *(APIENTRY*)(GLenum), "glGetString");
   GLuint fbo = 0;
   (void)arg;

   if (!iface->make_current(worker_ctx, true))
      return NULL;
   printf("gl fallback: the worker's context is OpenGL %s\n", (const char*)get_string(GL_VERSION));
   gen_textures(1, &texture);
   bind_texture(GL_TEXTURE_2D, texture);
   tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gen_framebuffers(1, &fbo);
   bind_framebuffer(GL_FRAMEBUFFER, fbo);
   framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
   viewport(0, 0, 16, 16);
   clear_color(0.8f, 0.4f, 0.2f, 1.0f);
   clear(GL_COLOR_BUFFER_BIT);
   bind_framebuffer(GL_FRAMEBUFFER, 0);
   fence = fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   flush();
   worker_ok = iface->make_current(worker_ctx, false);
   return NULL;
}

int main(void)
{
   static const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
   static const EGLint ctx_attribs[] = {
      EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
   PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
   EGLDisplay dpy;
   EGLConfig config;
   EGLContext frontend;
   EGLint count = 0;
   pthread_t t;
   struct retro_hw_shared_context *more[3];
   unsigned char px[4] = { 0, 0, 0, 0 };
   int i;

   get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
   dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)
         || !eglBindAPI(EGL_OPENGL_API)
         || !eglChooseConfig(dpy, config_attribs, &config, 1, &count) || count < 1)
   {
      printf("gl fallback: no surfaceless EGL here, skipped\n");
      return 0;
   }

   /* Nothing current: nothing to go by */
   TEST_CHECK(lr_gl_fallback_interface() == NULL);

   frontend = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
   if (frontend == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, frontend))
   {
      printf("gl fallback: no OpenGL 4.3 core context here, skipped\n");
      return 0;
   }

   /* The frontend's context current, as in context_reset */
   iface = lr_gl_fallback_interface();
   TEST_CHECK(iface != NULL);
   if (!iface)
      return 1;
   TEST_CHECK(iface->interface_version == RETRO_HW_SHARED_CONTEXT_INTERFACE_VERSION);
   worker_ctx = iface->create();
   for (i = 0; i < 3; i++)
      more[i] = iface->create();
   TEST_CHECK(worker_ctx != NULL);
   for (i = 0; i < 3; i++)
      TEST_CHECK(more[i] != NULL && more[i] != worker_ctx);
   TEST_CHECK(eglGetCurrentContext() == frontend);

   /* Drawn on a thread of its own, seen from the frontend's context */
   pthread_create(&t, NULL, worker, NULL);
   pthread_join(t, NULL);
   TEST_CHECK(worker_ok);
   TEST_CHECK(texture != 0);
   if (texture)
   {
      void (APIENTRY *wait_sync)(GLsync, GLbitfield, GLuint64) = PROC(void (APIENTRY*)(GLsync, GLbitfield, GLuint64), "glWaitSync");
      void (APIENTRY *gen_framebuffers)(GLsizei, GLuint*) = PROC(void (APIENTRY*)(GLsizei, GLuint*), "glGenFramebuffers");
      void (APIENTRY *bind_framebuffer)(GLenum, GLuint) = PROC(void (APIENTRY*)(GLenum, GLuint), "glBindFramebuffer");
      void (APIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint) =
         PROC(void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint), "glFramebufferTexture2D");
      void (APIENTRY *read_pixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
         PROC(void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*), "glReadPixels");
      GLuint fbo = 0;
      wait_sync(fence, 0, ~(GLuint64)0);
      gen_framebuffers(1, &fbo);
      bind_framebuffer(GL_READ_FRAMEBUFFER, fbo);
      framebuffer_texture_2d(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
      read_pixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   }
   TEST_CHECK(px[0] == 204 && px[1] == 102 && px[2] == 51);
   printf("gl fallback: the frontend's context sees the worker's texture as %u, %u, %u\n", px[0], px[1], px[2]);

   iface->destroy(worker_ctx);
   for (i = 0; i < 3; i++)
      iface->destroy(more[i]);
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(dpy, frontend);
   eglTerminate(dpy);
   printf("gl fallback: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
