/* The game's audio to the frontend, a frame's worth each retro_run
 * (libretro_audio_out.h). */

#include "libretro_audio_out.h"

#include <string.h>

#include <audio/conversion/float_to_s16.h>

#define SPK_FL  RETRO_AUDIO_SPEAKER_FRONT_LEFT
#define SPK_FR  RETRO_AUDIO_SPEAKER_FRONT_RIGHT
#define SPK_FC  RETRO_AUDIO_SPEAKER_FRONT_CENTER
#define SPK_LFE RETRO_AUDIO_SPEAKER_LOW_FREQUENCY
#define SPK_BL  RETRO_AUDIO_SPEAKER_BACK_LEFT
#define SPK_BR  RETRO_AUDIO_SPEAKER_BACK_RIGHT
#define SPK_SL  RETRO_AUDIO_SPEAKER_SIDE_LEFT
#define SPK_SR  RETRO_AUDIO_SPEAKER_SIDE_RIGHT

#define LR_AUDIO_OUT_SAMPLES (LR_AUDIO_OUT_FRAMES * LR_AUDIO_OUT_CHANNELS)

/* retro_run's alone. The game's samples as the pull gives them, and as s16
 * for a frontend that takes no floats; each handed over from a 64-byte
 * boundary of its storage. */
#define LR_ALIGN 64
static char lr_raw_store[LR_AUDIO_OUT_SAMPLES * sizeof(float) + LR_ALIGN - 1];
static char lr_s16_store[LR_AUDIO_OUT_SAMPLES * sizeof(int16_t) + LR_ALIGN - 1];
#define LR_ALIGNED(store) ((void*)(((uintptr_t)(store) + LR_ALIGN - 1) & ~(uintptr_t)(LR_ALIGN - 1)))

static retro_audio_sample_batch_multi_int16_t lr_multi_s16;
static retro_audio_sample_batch_multi_float_t lr_multi_float;

static double lr_owed;              /* the fraction of a frame carried to the next run */
static unsigned lr_channels = 2;    /* the format last handed over, for silence */
static unsigned lr_speakers = SPK_FL | SPK_FR;
static unsigned lr_layout;          /* the layout's speakers they were worked out from */

void lr_audio_out_set_multi(retro_audio_sample_batch_multi_int16_t multi_s16,
      retro_audio_sample_batch_multi_float_t multi_float)
{
   lr_multi_s16   = multi_s16;
   lr_multi_float = multi_s16 ? multi_float : NULL;
}

unsigned lr_audio_out_speakers(unsigned speakers, unsigned channels)
{
   unsigned bits  = speakers;
   unsigned count = 0;

   for (; bits; bits &= bits - 1)
      count++;

   if (speakers && count == channels)
      return speakers;

   switch (channels)
   {
      case 1:
         return SPK_FC;
      case 6:
         return SPK_FL | SPK_FR | SPK_FC | SPK_LFE | SPK_SL | SPK_SR;
      case 8:
         return SPK_FL | SPK_FR | SPK_FC | SPK_LFE | SPK_BL | SPK_BR | SPK_SL | SPK_SR;
      default:
         break;
   }
   return SPK_FL | SPK_FR;
}

void lr_audio_out_run_lent(retro_audio_sample_batch_t stereo_s16, lr_audio_pull_t pull,
      lr_audio_borrow_t borrow, lr_audio_return_t give_back, void *ctx, double frame_rate)
{
   size_t remaining;
   void *raw      = LR_ALIGNED(lr_raw_store);
   int16_t *conv  = (int16_t*)LR_ALIGNED(lr_s16_store);

   if (!stereo_s16 || !pull || frame_rate <= 0.0)
      return;

   lr_owed  += LR_AUDIO_OUT_RATE / frame_rate;
   remaining = (size_t)lr_owed;
   lr_owed  -= (double)remaining;

   while (remaining > 0)
   {
      struct lr_audio_format format;
      const int16_t *s16;
      const void *src = raw;
      size_t want     = remaining < LR_AUDIO_OUT_FRAMES ? remaining : LR_AUDIO_OUT_FRAMES;
      size_t frames   = 0;
      size_t lent     = 0;

      format.channels = 2;
      format.speakers = 0;
      format.is_float = 0;

      /* All of it where it lies, or none of it */
      if (borrow && give_back)
      {
         lent = borrow(ctx, &src, want, &format);

         if (lent && (lent != want || !src || format.channels == 0
                  || format.channels > LR_AUDIO_OUT_CHANNELS))
         {
            give_back(ctx, 0);
            lent = 0;
         }

         frames = lent;
      }

      if (!lent)
      {
         src             = raw;
         format.channels = 2;
         format.speakers = 0;
         format.is_float = 0;
         frames          = pull(ctx, raw, want, &format);

         if (frames > want || format.channels == 0 || format.channels > LR_AUDIO_OUT_CHANNELS)
            frames = 0;
      }

      if (frames)
      {
         /* The speakers are worked out when the format changes, not a run */
         if (format.channels != lr_channels || format.speakers != lr_layout)
         {
            lr_speakers = lr_audio_out_speakers(format.speakers, format.channels);
            lr_layout   = format.speakers;
            lr_channels = format.channels;
         }
      }
      else
         format.is_float = 0; /* silence, in the format last handed over */

      if (frames < want)
      {
         /* Not playing, short, or set up again just now: silence for the
          * rest (a float's zero is all bits zero, as an s16's). Only what
          * was pulled comes short. */
         size_t sample = format.is_float ? sizeof(float) : sizeof(int16_t);
         memset((char*)raw + frames * lr_channels * sample, 0,
               (want - frames) * lr_channels * sample);
         frames = want;
      }

      if (format.is_float && lr_multi_float)
      {
         /* As the game made them: no conversion */
         lr_multi_float((const float*)src, frames, lr_channels, lr_speakers);
      }
      else
      {
         s16 = (const int16_t*)src;

         if (format.is_float)
         {
            convert_float_to_s16(conv, (const float*)src, frames * lr_channels);
            s16 = conv;
         }

         if (lr_multi_s16)
            lr_multi_s16(s16, frames, lr_channels, lr_speakers);
         else if (lr_channels == 2)
            stereo_s16(s16, frames);
      }

      if (lent)
         give_back(ctx, lent);

      remaining -= frames;
   }
}

void lr_audio_out_run(retro_audio_sample_batch_t stereo_s16, lr_audio_pull_t pull,
      void *ctx, double frame_rate)
{
   lr_audio_out_run_lent(stereo_s16, pull, NULL, NULL, ctx, frame_rate);
}
