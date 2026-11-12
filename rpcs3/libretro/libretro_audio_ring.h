/* cellAudio's audio on its way to retro_run: a ring one thread writes and
 * one reads, lock-free (libretro-common's retro_spsc), that lends what it
 * holds where it lies. C89.
 *
 * - The reader borrows: it is given a pointer into the ring and hands that
 *   to the frontend, with no copy of its own. The ring's memory is mapped
 *   twice back to back where the platform can (retro_spsc_init_mirrored),
 *   so what is waiting is one piece even across the wrap; where it cannot,
 *   a piece that stops at the wrap is copied out instead (lr_audio_ring_read).
 * - The writer drops what is waiting (a flush, when the emulator pauses)
 *   without touching the reader's side: it leaves a mark, and the reader
 *   skips to it before it next looks. What a reader has borrowed stays as
 *   it is until it gives it back.
 * - "Active" is the reader telling the writer it has come for audio, which
 *   cellAudio waits for before it fills the ring. */

#ifndef LIBRETRO_AUDIO_RING_H
#define LIBRETRO_AUDIO_RING_H

#include <stddef.h>

#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include <retro_spsc.h>

RETRO_BEGIN_DECLS

struct lr_audio_ring
{
   retro_spsc_t q;
   size_t limit;                /* the bytes it holds for the writer */
   size_t produced;             /* the writer's: bytes written so far */
   size_t flushed;              /* the writer's: the mark it last left */
   size_t consumed;             /* the reader's: bytes read or skipped so far */
   retro_atomic_size_t mark;    /* writer to reader: all written before this is dropped */
   retro_atomic_int_t active;   /* reader to writer: it has come for audio */
};

/* Set up to hold @bytes; before either thread uses it. False when there is
 * no memory. The storage is at least twice @bytes: what one flush dropped
 * and the reader has not skipped yet takes no room from the writer. (Only
 * the reader can give storage back. After a second flush with the reader
 * still away the writer has what is left until the reader looks again -
 * cellAudio fills the ring only once the reader has come.) */
bool lr_audio_ring_init(struct lr_audio_ring *ring, size_t bytes);

/* After both threads have stopped using it. Safe on one never set up. */
void lr_audio_ring_free(struct lr_audio_ring *ring);

/* --- the writer's ------------------------------------------------------ */

/* The bytes waiting for the reader: written, not flushed, not yet read. */
size_t lr_audio_ring_used(const struct lr_audio_ring *ring);

/* @bytes of @data, all of them or none when they do not fit; with @force as
 * many as fit. The bytes written. */
size_t lr_audio_ring_push(struct lr_audio_ring *ring, const void *data,
      size_t bytes, bool force);

/* Drops what is waiting, and the reader's "active". */
void lr_audio_ring_flush(struct lr_audio_ring *ring);

/* Whether the reader has come for audio since the last flush. */
bool lr_audio_ring_active(const struct lr_audio_ring *ring);

/* --- the reader's ------------------------------------------------------ */

/* What is waiting, where it lies: *@ptr and the bytes there in one piece
 * (all of them on a mirrored ring). Nothing moves; give back what was used
 * with lr_audio_ring_consume. Marks the ring active. */
size_t lr_audio_ring_borrow(struct lr_audio_ring *ring, const void **ptr);

/* @bytes of what was borrowed are done with. */
void lr_audio_ring_consume(struct lr_audio_ring *ring, size_t bytes);

/* Up to @bytes copied out to @out, across the wrap; the bytes copied.
 * Marks the ring active. */
size_t lr_audio_ring_read(struct lr_audio_ring *ring, void *out, size_t bytes);

RETRO_END_DECLS

#endif
