/* The OpenGL renderer's frames on their way to the frontend, in textures
 * shared between the renderer's context and the frontend's: three of them,
 * the renderer drawing into one, retro_run showing another, the third the
 * newest finished - handed between the two threads with one atomic exchange
 * each, and between the two contexts with fences the GPU waits on, so
 * neither thread waits for the other and the renderer never draws into a
 * texture the frontend has yet to copy. A texture is resized only when the
 * renderer draws a frame of another size into it.
 *
 * The GL functions are the caller's (lr_gl_frames_procs): GLEW's in the core,
 * EGL's in the test. One renderer thread, one retro_run thread. C89. */

#ifndef LIBRETRO_GL_FRAMES_H
#define LIBRETRO_GL_FRAMES_H

#include <stddef.h>

#include <GL/glew.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lr_gl_frames_procs
{
   void   (GLAPIENTRY *gen_textures)(GLsizei, GLuint*);
   void   (GLAPIENTRY *delete_textures)(GLsizei, const GLuint*);
   void   (GLAPIENTRY *bind_texture)(GLenum, GLuint);
   void   (GLAPIENTRY *tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
   void   (GLAPIENTRY *tex_parameteri)(GLenum, GLenum, GLint);
   void   (GLAPIENTRY *gen_framebuffers)(GLsizei, GLuint*);
   void   (GLAPIENTRY *delete_framebuffers)(GLsizei, const GLuint*);
   void   (GLAPIENTRY *bind_framebuffer)(GLenum, GLuint);
   void   (GLAPIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint);
   void   (GLAPIENTRY *gen_renderbuffers)(GLsizei, GLuint*);
   void   (GLAPIENTRY *delete_renderbuffers)(GLsizei, const GLuint*);
   void   (GLAPIENTRY *bind_renderbuffer)(GLenum, GLuint);
   void   (GLAPIENTRY *renderbuffer_storage)(GLenum, GLenum, GLsizei, GLsizei);
   void   (GLAPIENTRY *framebuffer_renderbuffer)(GLenum, GLenum, GLenum, GLuint);
   void   (GLAPIENTRY *blit_framebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
   GLsync (GLAPIENTRY *fence_sync)(GLenum, GLbitfield);
   void   (GLAPIENTRY *wait_sync)(GLsync, GLbitfield, GLuint64);
   void   (GLAPIENTRY *delete_sync)(GLsync);
   void   (GLAPIENTRY *flush)(void);
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

/* Every frame published, for retro_run's stall report */
size_t lr_gl_frames_published(void);

/* Each context's objects, with its context current; the textures go with the
 * renderer's. Neither thread may be using them. */
void lr_gl_frames_destroy_draw(void);
void lr_gl_frames_destroy_show(void);

#ifdef __cplusplus
}
#endif

#endif
