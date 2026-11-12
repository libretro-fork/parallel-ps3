/* The OpenGL renderer's frames to the frontend: see libretro_gl_frames.h.
 * C89. */

#include <string.h>

#include <retro_atomic.h>

#include "libretro_gl_frames.h"

#define GL_FRAMES 3
#define GL_NEW    4 /* in s_ready, beside the slot's index: not taken yet */

struct gl_frame
{
   /* Shared objects: made and sized by the renderer */
   GLuint texture;
   GLuint depth;
   int    tex_width;
   int    tex_height;
   /* The frame in it: its size, and the renderer's fence after drawing it
    * (the frontend's context waits on it before copying) */
   int    width;
   int    height;
   GLsync drawn;
   /* The renderer's context's framebuffer over it */
   GLuint draw_fbo;
   /* The frontend's context's framebuffer over it, the texture it was made
    * for, and the fence after copying it (the renderer's context waits on
    * it before drawing into it again) */
   GLuint show_fbo;
   GLuint show_texture;
   GLsync shown;
};

static struct lr_gl_frames_procs gl;
static struct gl_frame s_frames[GL_FRAMES];

/* The renderer's slot, retro_run's, and the one between them (with GL_NEW
 * when it holds a frame retro_run has not taken). Zero, as static storage
 * starts, is slot 0 between them with nothing new. */
static unsigned s_draw = 1;
static unsigned s_show = 2;
static retro_atomic_int_t s_ready;
static int s_draw_width;
static int s_draw_height;

void lr_gl_frames_set_procs(const struct lr_gl_frames_procs *procs)
{
   gl = *procs;
}

static void size_frame(struct gl_frame *f, int width, int height)
{
   gl.bind_texture(GL_TEXTURE_2D, f->texture);
   gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.bind_texture(GL_TEXTURE_2D, 0);
   gl.bind_renderbuffer(GL_RENDERBUFFER, f->depth);
   gl.renderbuffer_storage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
   gl.bind_renderbuffer(GL_RENDERBUFFER, 0);
   f->tex_width  = width;
   f->tex_height = height;
}

GLuint lr_gl_frames_draw_fbo(int width, int height)
{
   struct gl_frame *f = &s_frames[s_draw];

   if (width <= 0 || height <= 0)
      return 0;

   if (!f->texture)
   {
      gl.gen_textures(1, &f->texture);
      gl.bind_texture(GL_TEXTURE_2D, f->texture);
      gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl.bind_texture(GL_TEXTURE_2D, 0);
      gl.gen_renderbuffers(1, &f->depth);
      size_frame(f, width, height);

      gl.gen_framebuffers(1, &f->draw_fbo);
      gl.bind_framebuffer(GL_FRAMEBUFFER, f->draw_fbo);
      gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f->texture, 0);
      gl.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, f->depth);
      gl.bind_framebuffer(GL_FRAMEBUFFER, 0);
   }
   else if (f->tex_width != width || f->tex_height != height)
      size_frame(f, width, height); /* not the frontend's: it is past this one */

   f->width       = width;
   f->height      = height;
   s_draw_width   = width;
   s_draw_height  = height;
   return f->draw_fbo;
}

int lr_gl_frames_draw_width(void)  { return s_draw_width; }
int lr_gl_frames_draw_height(void) { return s_draw_height; }

void lr_gl_frames_publish(void)
{
   struct gl_frame *f = &s_frames[s_draw];
   int previous;

   if (!f->texture)
      return;

   /* Fenced and flushed, so the frontend's context can wait on it */
   if (f->drawn)
      gl.delete_sync(f->drawn);
   f->drawn = gl.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   gl.flush();

   previous = retro_atomic_exchange_int(&s_ready, (int)s_draw | GL_NEW);
   s_draw   = (unsigned)previous & 3;

   f = &s_frames[s_draw];
   /* A frame the frontend never took */
   if (f->drawn)
   {
      gl.delete_sync(f->drawn);
      f->drawn = NULL;
   }
   /* The next frame draws into this one once the frontend's copy of it is
    * done: the GPU waits, not this thread */
   if (f->shown)
   {
      gl.wait_sync(f->shown, 0, ~(GLuint64)0); /* GL_TIMEOUT_IGNORED, without a C99 constant */
      gl.delete_sync(f->shown);
      f->shown = NULL;
   }
}

bool lr_gl_frames_blit(GLuint dst_fbo, int *width, int *height)
{
   struct gl_frame *f;
   int previous;

   if (!(retro_atomic_load_acquire_int(&s_ready) & GL_NEW))
      return false;

   /* The newest, and the one shown so far back to the renderer, its copy
    * fenced */
   previous = retro_atomic_exchange_int(&s_ready, (int)s_show);
   s_show   = (unsigned)previous & 3;
   f        = &s_frames[s_show];
   if (!f->texture)
      return false;

   if (f->drawn)
   {
      gl.wait_sync(f->drawn, 0, ~(GLuint64)0); /* GL_TIMEOUT_IGNORED, without a C99 constant */
      gl.delete_sync(f->drawn);
      f->drawn = NULL;
   }

   if (!f->show_fbo || f->show_texture != f->texture)
   {
      if (!f->show_fbo)
         gl.gen_framebuffers(1, &f->show_fbo);
      gl.bind_framebuffer(GL_READ_FRAMEBUFFER, f->show_fbo);
      gl.framebuffer_texture_2d(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f->texture, 0);
      f->show_texture = f->texture;
   }

   gl.bind_framebuffer(GL_READ_FRAMEBUFFER, f->show_fbo);
   gl.bind_framebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
   gl.blit_framebuffer(0, 0, f->width, f->height, 0, 0, f->width, f->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   gl.bind_framebuffer(GL_FRAMEBUFFER, 0);

   if (f->shown)
      gl.delete_sync(f->shown);
   f->shown = gl.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   gl.flush();

   *width  = f->width;
   *height = f->height;
   return true;
}

void lr_gl_frames_destroy(void)
{
   unsigned i;
   for (i = 0; i < GL_FRAMES; i++)
   {
      struct gl_frame *f = &s_frames[i];
      if (f->show_fbo)
         gl.delete_framebuffers(1, &f->show_fbo);
      if (f->depth)
         gl.delete_renderbuffers(1, &f->depth);
      if (f->texture)
         gl.delete_textures(1, &f->texture);
      if (f->drawn)
         gl.delete_sync(f->drawn);
      if (f->shown)
         gl.delete_sync(f->shown);
      memset(f, 0, sizeof(*f));
   }
   s_draw        = 1;
   s_show        = 2;
   s_draw_width  = s_draw_height = 0;
   retro_atomic_store_release_int(&s_ready, 0);
}
