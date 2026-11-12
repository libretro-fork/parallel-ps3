/* The game's audio to the frontend, a frame's worth each retro_run. C89;
 * no allocation, no clock, nothing waited for.
 *
 * Exactly one frame's worth a run: the sample rate over the frames a second
 * the core declares, the fractions carried. The frontend paces the runs, and
 * with them the audio, as it paces the pictures; fast-forward and slow
 * motion take the audio along. While there is nothing to pull (paused, not
 * started yet, being set up) the run hands over silence in the format last
 * handed over, so the frontend's audio keeps its pace too.
 *
 * What is due is handed over in one call, where it fits one piece (1024
 * frames: every rate from 47 frames a second up). It is borrowed from the
 * game's side where that can lend it (lr_audio_out_run_lent) - the frontend
 * then reads the game's own memory - and pulled into a buffer here
 * otherwise. Floats go to a frontend that takes them as the game made them;
 * otherwise they are converted to s16 once, here. */

#ifndef LIBRETRO_AUDIO_OUT_H
#define LIBRETRO_AUDIO_OUT_H

#include <stddef.h>
#include <stdint.h>

#include <retro_common_api.h>
#include <libretro.h>

RETRO_BEGIN_DECLS

#define LR_AUDIO_OUT_FRAMES   1024 /* the most pulled and handed over at once */
#define LR_AUDIO_OUT_CHANNELS 8    /* up to 7.1 */
#define LR_AUDIO_OUT_RATE     48000.0

/* What a pull says of the samples it gave */
struct lr_audio_format
{
   unsigned channels; /* 1 to LR_AUDIO_OUT_CHANNELS */
   unsigned speakers; /* RETRO_AUDIO_SPEAKER_* of the layout; 0 or a count
                       * that is not @channels: the usual ones for @channels */
   int is_float;      /* floats, else s16 */
};

/* Up to @max_frames frames into @out (room for max_frames of
 * LR_AUDIO_OUT_CHANNELS floats, 64-byte aligned); the frames given, 0 for
 * none this run. Must not wait. */
typedef size_t (*lr_audio_pull_t)(void *ctx, void *out, size_t max_frames,
      struct lr_audio_format *format);

/* The frontend's multi-channel output (NULL without): all audio goes
 * through it then. Without it, two channels go to retro_run's classic
 * callback and anything else is dropped. */
void lr_audio_out_set_multi(retro_audio_sample_batch_multi_int16_t multi_s16,
      retro_audio_sample_batch_multi_float_t multi_float);

/* The speakers handed to the frontend for @channels channels of a layout
 * with @speakers: always as many as channels (the frontend refuses a frame
 * whose two disagree). */
unsigned lr_audio_out_speakers(unsigned speakers, unsigned channels);

/* retro_run: one frame's worth, pulled with @pull and handed over. */
void lr_audio_out_run(retro_audio_sample_batch_t stereo_s16, lr_audio_pull_t pull,
      void *ctx, double frame_rate);

/* The game's samples lent where they lie, with no copy on the way: exactly
 * @frames frames at *@ptr, the caller's to read until it gives them back
 * with the return callback, or 0 when that many cannot be lent in one piece
 * (the run then pulls instead, which copies and can come short). Must not
 * wait. */
typedef size_t (*lr_audio_borrow_t)(void *ctx, const void **ptr, size_t frames,
      struct lr_audio_format *format);
typedef void (*lr_audio_return_t)(void *ctx, size_t frames);

/* As lr_audio_out_run, borrowing first: the frontend is handed the game's
 * own memory (floats to a frontend that takes them, s16 as it is), or the
 * conversion to s16 reads from it. */
void lr_audio_out_run_lent(retro_audio_sample_batch_t stereo_s16, lr_audio_pull_t pull,
      lr_audio_borrow_t borrow, lr_audio_return_t give_back, void *ctx, double frame_rate);

RETRO_END_DECLS

#endif
