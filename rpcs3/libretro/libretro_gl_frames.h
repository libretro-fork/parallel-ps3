/* The OpenGL renderer's frames on their way to the frontend, in textures
 * shared between the renderer's context and the frontend's: three of them,
 * the renderer drawing into one, retro_run showing another, the third the
 * newest finished - handed between the two threads with one atomic exchange
 * each, and between the two contexts with fences the GPU waits on, so
 * neither thread waits for the other and the renderer never draws into a
 * texture the frontend has yet to copy. A texture is resized only when the
 * renderer draws a frame of another size into it.
 *
 * The GL functions are the caller's (lr_gl_frames_procs): the renderer's in
 * the core, EGL's in the test. One renderer thread, one retro_run thread. C89. */

#ifndef LIBRETRO_GL_FRAMES_H
#define LIBRETRO_GL_FRAMES_H

#include <stddef.h>

/* The GL types from Khronos' header, which declares no functions: the
 * functions are the caller's (lr_gl_frames_procs) */
#include <glcorearb.h>
#define LR_GL_APIENTRY APIENTRY

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lr_gl_frames_procs
{
   void   (LR_GL_APIENTRY *gen_textures)(GLsizei, GLuint*);
   void   (LR_GL_APIENTRY *delete_textures)(GLsizei, const GLuint*);
   void   (LR_GL_APIENTRY *bind_texture)(GLenum, GLuint);
   void   (LR_GL_APIENTRY *tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
   void   (LR_GL_APIENTRY *tex_parameteri)(GLenum, GLenum, GLint);
   void   (LR_GL_APIENTRY *gen_framebuffers)(GLsizei, GLuint*);
   void   (LR_GL_APIENTRY *delete_framebuffers)(GLsizei, const GLuint*);
   void   (LR_GL_APIENTRY *bind_framebuffer)(GLenum, GLuint);
   void   (LR_GL_APIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint);
   void   (LR_GL_APIENTRY *gen_renderbuffers)(GLsizei, GLuint*);
   void   (LR_GL_APIENTRY *delete_renderbuffers)(GLsizei, const GLuint*);
   void   (LR_GL_APIENTRY *bind_renderbuffer)(GLenum, GLuint);
   void   (LR_GL_APIENTRY *renderbuffer_storage)(GLenum, GLenum, GLsizei, GLsizei);
   void   (LR_GL_APIENTRY *framebuffer_renderbuffer)(GLenum, GLenum, GLenum, GLuint);
   void   (LR_GL_APIENTRY *blit_framebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
   GLsync (LR_GL_APIENTRY *fence_sync)(GLenum, GLbitfield);
   void   (LR_GL_APIENTRY *wait_sync)(GLsync, GLbitfield, GLuint64);
   void   (LR_GL_APIENTRY *delete_sync)(GLsync);
   void   (LR_GL_APIENTRY *flush)(void);
};

/* Either thread, once, before anything else */
void lr_gl_frames_set_procs(const struct lr_gl_frames_procs *procs);

/* The renderer's thread, its context current: the framebuffer to draw the
 * next frame into, @width x @height, with a depth/stencil buffer; 0 when it
 * cannot be had */
GLuint lr_gl_frames_draw_fbo(int width, int height);

/* The renderer's size for the next frame (its window's), without making
 * anything; 0 before the first lr_gl_frames_draw_fbo */
int lr_gl_frames_draw_width(void);
int lr_gl_frames_draw_height(void);

/* The renderer's thread: the frame drawn since lr_gl_frames_draw_fbo,
 * finished - fenced, flushed and handed over */
void lr_gl_frames_publish(void);

/* retro_run, the frontend's context current: the newest frame finished
 * since the last take blitted into @dst_fbo at its own size, once the GPU
 * has finished drawing it; false, with nothing blitted, when there is none.
 * *@width and *@height say the size blitted. */
bool lr_gl_frames_blit(GLuint dst_fbo, int *width, int *height);

/* Everything, from the frontend's context, current, once the renderer has
 * stopped: the textures, depth buffers and fences (objects of the share
 * group), and this context's framebuffers. The renderer's framebuffers go with
 * its context; destroy that first, or with it current call this before.
 * Afterwards it starts over as at first. */
void lr_gl_frames_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
