/* cellAudio's ring to retro_run (libretro/libretro_audio_ring.c):
 *
 * - what is written is what is borrowed, in order, in one piece across the
 *   wrap on a mirrored ring, for frames of 8 and of 24 bytes (5.1 floats,
 *   which no power of two is a multiple of), at an address a float can be
 *   read from;
 * - a write that does not fit: none of it, or with force as much as fits;
 *   the ring holds what it was set up for and no more;
 * - a flush: nothing written before it is read after it, what was borrowed
 *   before it stays as it was until given back, and the writer has all its
 *   room again at once - before the reader has been back (for one flush;
 *   after more, once it has);
 * - the reader's first look marks the ring active, a flush unmarks it;
 * - the copy out (for a ring that could not be mirrored) gives the same
 *   bytes;
 * - two threads, the writer flushing now and then: the reader's numbers
 *   only ever go up, and none it reads was written before a flush it came
 *   after. (Built a second time with ThreadSanitizer.)
 *
 * Usage: test_audio_ring */

#include "../libretro_audio_ring.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>

static void put_u32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get_u32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void frames_in_order(size_t frame, size_t limit_frames)
{
   struct lr_audio_ring ring;
   unsigned char block[24 * 256];
   uint32_t next_in = 0, next_out = 0;
   size_t i, round;
   int one_piece = 1, aligned = 1;

   TEST_CHECK(lr_audio_ring_init(&ring, frame * limit_frames));
   TEST_CHECK(lr_audio_ring_used(&ring) == 0);

   for (round = 0; round < 20000; round++)
   {
      /* 1 to 256 frames in, each sample its number */
      size_t n = 1 + (round * 37) % 256;
      const void *ptr;
      size_t got, take;

      for (i = 0; i < n * frame / 4; i++)
         put_u32(block + i * 4, next_in + (uint32_t)i);
      if (lr_audio_ring_push(&ring, block, n * frame, false) == n * frame)
         next_in += (uint32_t)(n * frame / 4);
      else
         TEST_CHECK(lr_audio_ring_used(&ring) + n * frame > frame * limit_frames);

      got = lr_audio_ring_borrow(&ring, &ptr);
      if (ring.q.mirror && got != lr_audio_ring_used(&ring))
         one_piece = 0;
      if (((size_t)ptr & 3) != 0)
         aligned = 0;

      /* Some of it out, whole frames */
      take = (got / frame) * ((round % 3) + 1) / 3 * frame;
      for (i = 0; i < take / 4; i++)
         if (get_u32((const unsigned char*)ptr + i * 4) != next_out + (uint32_t)i)
            break;
      TEST_CHECK(i == take / 4);
      next_out += (uint32_t)(take / 4);
      lr_audio_ring_consume(&ring, take);
   }

   TEST_CHECK(one_piece);
   TEST_CHECK(aligned);
   TEST_CHECK(next_out > 100000);
   printf("audio ring: %lu-byte frames: %lu samples through in order, %s\n", (unsigned long)frame,
         (unsigned long)next_out, ring.q.mirror ? "in one piece across the wrap" : "not mirrored here");
   lr_audio_ring_free(&ring);
}

/* --- two threads -------------------------------------------------------- */

static struct lr_audio_ring shared;
static retro_atomic_int_t writer_done;
/* The lowest number written after the writer's last flush, stored once
 * the flush is made: a reader that loads it and then looks has that flush
 * behind it, so it sees nothing below */
static retro_atomic_size_t floor_after_flush;

static void *writer(void *arg)
{
   uint32_t next = 1;
   unsigned char block[8 * 64];
   size_t round, i;
   (void)arg;

   for (round = 0; round < 100000; round++)
   {
      size_t n = 1 + round % 64;
      for (i = 0; i < n * 2; i++)
         put_u32(block + i * 4, next + (uint32_t)i);
      /* Waits for room, so the reader has most of it to read - but not
       * for long: a ring left full is what the next flush drops */
      for (i = 0; i < 200 && !lr_audio_ring_push(&shared, block, n * 8, false); i++)
         sched_yield();
      if (i < 200)
         next += (uint32_t)(n * 2);
      if (round % 997 == 996)
      {
         lr_audio_ring_flush(&shared);
         retro_atomic_store_release_size(&floor_after_flush, next);
      }
   }
   retro_atomic_store_release_int(&writer_done, 1);
   return NULL;
}

int main(void)
{
   frames_in_order(8, 4800);
   frames_in_order(24, 4800);
   frames_in_order(4, 300);

   /* Room: all or nothing, as much as fits with force, never more than it
    * was set up for */
   {
      struct lr_audio_ring ring;
      unsigned char data[1000];
      const void *ptr;
      size_t i;
      for (i = 0; i < sizeof(data); i++)
         data[i] = (unsigned char)i;
      TEST_CHECK(lr_audio_ring_init(&ring, 1000));
      TEST_CHECK(!lr_audio_ring_active(&ring));
      TEST_CHECK(lr_audio_ring_push(&ring, data, 600, false) == 600);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 600, false) == 0);
      TEST_CHECK(lr_audio_ring_used(&ring) == 600);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 600, true) == 400);
      TEST_CHECK(lr_audio_ring_used(&ring) == 1000);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1, true) == 0);

      /* Borrowed, then a flush: the borrowed bytes are as they were, the
       * writer has its room again before the reader is back, and the
       * reader gets none of what was dropped */
      TEST_CHECK(lr_audio_ring_borrow(&ring, &ptr) >= 600 || !ring.q.mirror);
      TEST_CHECK(lr_audio_ring_active(&ring));
      lr_audio_ring_flush(&ring);
      TEST_CHECK(!lr_audio_ring_active(&ring));
      TEST_CHECK(lr_audio_ring_used(&ring) == 0);
      TEST_CHECK(!memcmp(ptr, data, 600));
      for (i = 0; i < sizeof(data); i++)
         data[i] = (unsigned char)(255 - i % 251);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1000, false) == 1000);
      TEST_CHECK(lr_audio_ring_used(&ring) == 1000);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1, true) == 0);
      TEST_CHECK(*(const unsigned char*)ptr == 0); /* still the old byte */
      lr_audio_ring_consume(&ring, 100);           /* gives back some of the old */

      {
         unsigned char out[1000];
         size_t got = lr_audio_ring_read(&ring, out, 1000);
         TEST_CHECK(got == 1000);
         TEST_CHECK(!memcmp(out, data, 1000)); /* only what came after the flush */
         TEST_CHECK(lr_audio_ring_active(&ring));
         TEST_CHECK(lr_audio_ring_read(&ring, out, 1000) == 0);
         TEST_CHECK(lr_audio_ring_used(&ring) == 0);
      }

      /* Flush after flush with the reader away: a full ring's worth fits
       * after the first; after more, what storage is left, and never a
       * byte of what was dropped comes out */
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1000, false) == 1000);
      lr_audio_ring_flush(&ring);
      TEST_CHECK(lr_audio_ring_used(&ring) == 0);
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1000, false) == 1000);
      for (i = 0; i < 50; i++)
      {
         lr_audio_ring_flush(&ring);
         TEST_CHECK(lr_audio_ring_used(&ring) == 0);
         TEST_CHECK(lr_audio_ring_push(&ring, data, 1000, true) <= 1000);
      }
      lr_audio_ring_flush(&ring);
      TEST_CHECK(lr_audio_ring_borrow(&ring, &ptr) == 0);
      /* The reader has been: all the room again */
      TEST_CHECK(lr_audio_ring_push(&ring, data, 1000, false) == 1000);
      TEST_CHECK(lr_audio_ring_borrow(&ring, &ptr) == 1000 || !ring.q.mirror);
      TEST_CHECK(!memcmp(ptr, data, 16));
      lr_audio_ring_free(&ring);
      lr_audio_ring_free(&ring);
   }

   /* The writer on its thread, flushing now and then; the reader here */
   {
      pthread_t t;
      uint32_t last = 0;
      unsigned long reads = 0, backwards = 0, stale = 0, torn = 0;

      TEST_CHECK(lr_audio_ring_init(&shared, 8 * 4096));
      retro_atomic_int_init(&writer_done, 0);
      retro_atomic_size_init(&floor_after_flush, 0);
      pthread_create(&t, NULL, writer, NULL);

      for (;;)
      {
         const void *ptr;
         int done      = retro_atomic_load_acquire_int(&writer_done);
         size_t floor  = retro_atomic_load_acquire_size(&floor_after_flush);
         size_t got    = lr_audio_ring_borrow(&shared, &ptr);
         size_t frames = got / 8, i;

         if (frames > 100)
            frames = 100;

         for (i = 0; i < frames * 2; i++)
         {
            uint32_t v = get_u32((const unsigned char*)ptr + i * 4);
            if (v <= last)
               backwards++;
            else if (i && v != last + 1)
               torn++; /* within one look the numbers run on, unless a flush fell between */
            last = v;
         }
         if (frames && get_u32((const unsigned char*)ptr) < floor)
            stale++;
         lr_audio_ring_consume(&shared, frames * 8);
         reads += frames;

         if (done && !got)
            break;
      }

      pthread_join(t, NULL);
      TEST_CHECK(backwards == 0);
      TEST_CHECK(stale == 0);
      TEST_CHECK(torn == 0);
      TEST_CHECK(reads > 100000);
      printf("audio ring: %lu frames read while the writer flushed 100 times: %lu backwards, %lu from before a flush\n",
            reads, backwards, stale);
      lr_audio_ring_free(&shared);
   }

   printf("audio ring: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
