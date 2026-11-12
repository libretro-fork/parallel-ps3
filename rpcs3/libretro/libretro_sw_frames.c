/* The software frames: see libretro_sw_frames.h. C89. */

#include <stdlib.h>

#include <retro_atomic.h>

#include "libretro_sw_frames.h"

#define SW_FRAMES 3
#define SW_NEW    4 /* in s_ready, beside the buffer's index: not taken yet */

struct sw_frame
{
   uint8_t *data;
   size_t capacity;
   size_t pitch;
   unsigned width;
   unsigned height;
};

static struct sw_frame s_frames[SW_FRAMES];

/* The renderer's buffer, retro_run's, and the one between them (with
 * SW_NEW when it holds a frame retro_run has not taken). Zero, as static
 * storage starts, is buffer 0 between them with nothing new: no setup, so
 * neither thread has to come first. */
static unsigned s_back   = 1;
static unsigned s_front  = 2;
static retro_atomic_int_t s_ready;
static retro_atomic_size_t s_presented;

uint8_t *sw_frames_begin(size_t size)
{
   struct sw_frame *f = &s_frames[s_back];
   if (f->capacity < size)
   {
      /* Only when the frame outgrows it: a new resolution */
      uint8_t *grown = (uint8_t*)realloc(f->data, size);
      if (!grown)
         return NULL;
      f->data     = grown;
      f->capacity = size;
   }
   return f->data;
}

void sw_frames_end(unsigned width, unsigned height, size_t pitch)
{
   struct sw_frame *f = &s_frames[s_back];
   int previous;
   f->width  = width;
   f->height = height;
   f->pitch  = pitch;
   /* Published, and the one between taken back to fill next */
   previous  = retro_atomic_exchange_int(&s_ready, (int)s_back | SW_NEW);
   s_back    = (unsigned)previous & 3;
   retro_atomic_fetch_add_size(&s_presented, 1);
}

bool sw_frames_take(const void **data, unsigned *width, unsigned *height, size_t *pitch)
{
   struct sw_frame *f;
   int previous;
   if (!(retro_atomic_load_acquire_int(&s_ready) & SW_NEW))
      return false;
   previous = retro_atomic_exchange_int(&s_ready, (int)s_front);
   s_front  = (unsigned)previous & 3;
   f        = &s_frames[s_front];
   if (!f->data)
      return false;
   *data    = f->data;
   *width   = f->width;
   *height  = f->height;
   *pitch   = f->pitch;
   return true;
}

size_t sw_frames_presented(void)
{
   return retro_atomic_load_acquire_size(&s_presented);
}

void sw_frames_free(void)
{
   unsigned i;
   for (i = 0; i < SW_FRAMES; i++)
   {
      free(s_frames[i].data);
      s_frames[i].data     = NULL;
      s_frames[i].capacity = 0;
   }
}
