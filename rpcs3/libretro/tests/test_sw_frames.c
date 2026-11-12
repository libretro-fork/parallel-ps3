/* The software frames (libretro/libretro_sw_frames.c): the renderer's
 * readbacks handed to retro_run through a triple buffer, without a lock.
 *
 * - a renderer and a retro_run on two threads: every frame taken is whole
 *   (all of it from one frame), newer than the last, and stays whole until
 *   the next take however many the renderer finishes meanwhile - the
 *   frontend may show it again until then;
 * - nothing taken twice, nothing taken before one is finished;
 * - a buffer grows only when a frame outgrows it.
 *
 * Built twice, under AddressSanitizer and under ThreadSanitizer. */

#include <pthread.h>
#include <sched.h>

#include <retro_atomic.h>

#include "../libretro_sw_frames.h"
#include "test_common.h"

#define FRAMES 20000

static retro_atomic_int_t done;

static void *renderer(void *arg)
{
   unsigned n;
   (void)arg;
   for (n = 1; n <= FRAMES; n++)
   {
      /* Two sizes, the bigger now and then */
      size_t size = (n % 500 < 3) ? 64 * 64 * 4 : 32 * 32 * 4;
      uint8_t *dst = sw_frames_begin(size);
      if (!dst)
         abort();
      memset(dst, (int)(n & 0xff), size);
      sw_frames_end(size == 64 * 64 * 4 ? 64 : 32, size == 64 * 64 * 4 ? 64 : 32, size == 64 * 64 * 4 ? 256 : 128);
   }
   retro_atomic_store_release_int(&done, 1);
   return NULL;
}

int main(void)
{
   pthread_t t;
   const void *data;
   unsigned w, h, taken = 0, torn = 0, changed = 0;
   size_t pitch;
   const uint8_t *shown = NULL;
   size_t shown_size = 0;
   uint8_t shown_value = 0;

   /* Nothing yet */
   TEST_CHECK(!sw_frames_take(&data, &w, &h, &pitch));

   retro_atomic_int_init(&done, 0);
   pthread_create(&t, NULL, renderer, NULL);
   for (;;)
   {
      int finished = retro_atomic_load_acquire_int(&done);
      size_t i;

      /* The frame shown last is still whole */
      if (shown)
         for (i = 0; i < shown_size; i++)
            if (shown[i] != shown_value)
            {
               changed++;
               break;
            }

      if (!sw_frames_take(&data, &w, &h, &pitch))
      {
         if (finished)
            break;
         sched_yield();
         continue;
      }
      shown       = (const uint8_t*)data;
      shown_size  = pitch * h;
      shown_value = shown[0];
      for (i = 1; i < shown_size; i++)
         if (shown[i] != shown_value)
         {
            torn++;
            break;
         }
      TEST_CHECK(pitch == w * 4);
      taken++;
   }
   pthread_join(t, NULL);

   /* The last one, once */
   TEST_CHECK(!sw_frames_take(&data, &w, &h, &pitch));
   TEST_CHECK(shown_value == (FRAMES & 0xff));
   TEST_CHECK(torn == 0 && changed == 0);
   TEST_CHECK(taken > 0);

   printf("sw frames: %u frames taken of %u, %u torn, %u changed while shown\n",
         taken, FRAMES, torn, changed);
   sw_frames_free();
   if (test_failures)
      printf("sw frames: FAIL\n");
   return test_failures ? 1 : 0;
}
