/* cellAudio's audio on its way to retro_run (libretro_audio_ring.h). */

#include "libretro_audio_ring.h"

#include <string.h>

bool lr_audio_ring_init(struct lr_audio_ring *ring, size_t bytes)
{
   memset(ring, 0, sizeof(*ring));
   retro_atomic_size_init(&ring->mark, 0);
   retro_atomic_int_init(&ring->active, 0);

   if (bytes > ((size_t)-1) / 4)
      return false;

   if (!retro_spsc_init_mirrored(&ring->q, bytes ? bytes * 2 : 2))
      return false;

   ring->limit = bytes;
   return true;
}

void lr_audio_ring_free(struct lr_audio_ring *ring)
{
   if (ring->q.buffer)
      retro_spsc_free(&ring->q);
   memset(ring, 0, sizeof(*ring));
}

size_t lr_audio_ring_used(const struct lr_audio_ring *ring)
{
   /* What is in the ring, less what a flush dropped and the reader has not
    * skipped yet: never more than was written since the flush */
   size_t in_ring = retro_spsc_read_avail(&ring->q);
   size_t since   = ring->produced - ring->flushed;
   return in_ring < since ? in_ring : since;
}

size_t lr_audio_ring_push(struct lr_audio_ring *ring, const void *data,
      size_t bytes, bool force)
{
   size_t used  = lr_audio_ring_used(ring);
   size_t room  = ring->limit > used ? ring->limit - used : 0;
   size_t space = retro_spsc_write_avail(&ring->q);
   size_t written;

   if (room > space)
      room = space;

   if (bytes > room)
   {
      if (!force)
         return 0;
      bytes = room;
   }

   if (!bytes)
      return 0;

   written         = retro_spsc_write(&ring->q, data, bytes);
   ring->produced += written;
   return written;
}

void lr_audio_ring_flush(struct lr_audio_ring *ring)
{
   ring->flushed = ring->produced;
   retro_atomic_store_release_size(&ring->mark, ring->flushed);
   retro_atomic_store_release_int(&ring->active, 0);
}

bool lr_audio_ring_active(const struct lr_audio_ring *ring)
{
   return retro_atomic_load_acquire_int((retro_atomic_int_t*)&ring->active) != 0;
}

/* The reader, before it looks: past what the writer dropped. The mark is a
 * count of bytes written; the reader's is of bytes read. Their difference
 * is at most the ring's size, so it is compared as that even where the
 * counts have gone round. */
static void lr_audio_ring_skip_flushed(struct lr_audio_ring *ring)
{
   size_t mark  = retro_atomic_load_acquire_size(&ring->mark);
   size_t ahead = mark - ring->consumed;

   if (ahead && ahead <= ring->q.capacity)
      ring->consumed += retro_spsc_skip(&ring->q, ahead);

   if (!retro_atomic_load_acquire_int(&ring->active))
      retro_atomic_store_release_int(&ring->active, 1);
}

size_t lr_audio_ring_borrow(struct lr_audio_ring *ring, const void **ptr)
{
   lr_audio_ring_skip_flushed(ring);
   return retro_spsc_read_begin(&ring->q, ptr);
}

void lr_audio_ring_consume(struct lr_audio_ring *ring, size_t bytes)
{
   retro_spsc_read_end(&ring->q, bytes);
   ring->consumed += bytes;
}

size_t lr_audio_ring_read(struct lr_audio_ring *ring, void *out, size_t bytes)
{
   size_t got;
   lr_audio_ring_skip_flushed(ring);
   got             = retro_spsc_read(&ring->q, out, bytes);
   ring->consumed += got;
   return got;
}
