/* cellAudio's mixer (cellAudio_mix.h). */

#include "cellAudio_mix.h"

#include <string.h>

#include <retro_inline.h>

#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64)))
#include <emmintrin.h>
#define MIX_SSE2 1
#endif

/* A big-endian float at any address */
static INLINE float mix_be(const unsigned char *p)
{
   union { unsigned int u; float f; } v;
   v.u = ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16)
       | ((unsigned int)p[2] << 8)  |  (unsigned int)p[3];
   return v.f;
}

/* One step of a volume that is moving; 1 when it arrives */
static INLINE int mix_step(struct cell_audio_mix_level *lv)
{
   const int down = lv->step < 0.0f;

   lv->level += lv->step;

   if ((!down && lv->target - lv->level <= 0.0f) || (down && lv->target - lv->level >= 0.0f))
   {
      lv->level = lv->target;
      lv->step  = 0.0f;
      return 1;
   }

   return 0;
}

/* One frame of a port into one frame of the block, at volume @m */
static INLINE void mix_frame(float *out, unsigned out_channels, const unsigned char *in,
      unsigned in_channels, enum cell_audio_mix_down down, float m)
{
   /* value taken from https://www.dolby.com/us/en/technologies/a-guide-to-dolby-metadata.pdf */
   static const float minus_3db = 0.707f;

   if (in_channels == 2)
   {
      const float left  = mix_be(in + 0) * m;
      const float right = mix_be(in + 4) * m;

      out[0] += left;
      out[1] += right;
   }
   else if (in_channels == 8)
   {
      const float left       = mix_be(in +  0) * m;
      const float right      = mix_be(in +  4) * m;
      const float center     = mix_be(in +  8) * m;
      const float low_freq   = mix_be(in + 12) * m;
      const float side_left  = mix_be(in + 16) * m;
      const float side_right = mix_be(in + 20) * m;
      const float rear_left  = mix_be(in + 24) * m;
      const float rear_right = mix_be(in + 28) * m;

      if (down == CELL_AUDIO_MIX_STEREO)
      {
         /* Don't mix in the lfe as per dolby specification and based on documentation */
         const float mid = center * 0.5f;
         out[0] += left * minus_3db + mid + side_left * 0.5f + rear_left * 0.5f;
         out[1] += right * minus_3db + mid + side_right * 0.5f + rear_right * 0.5f;
      }
      else if (down == CELL_AUDIO_MIX_5_1)
      {
         out[0] += left;
         out[1] += right;

         /* Only mix the surround channels into the output if surround output is configured */
         if (out_channels >= 6)
         {
            out[2] += center;
            out[3] += low_freq;

            if (out_channels == 6)
            {
               out[4] += side_left + rear_left;
               out[5] += side_right + rear_right;
            }
            else
            {
               /* With 7.1 output, 4 and 5 are the rear channels: the side
                * channels are mixed into 6 and 7 */
               out[6] += side_left + rear_left;
               out[7] += side_right + rear_right;
            }
         }
      }
      else
      {
         out[0] += left;
         out[1] += right;

         if (out_channels >= 6)
         {
            out[2] += center;
            out[3] += low_freq;

            if (out_channels == 6)
            {
               out[4] += side_left;
               out[5] += side_right;
            }
            else
            {
               out[4] += rear_left;
               out[5] += rear_right;
               out[6] += side_left;
               out[7] += side_right;
            }
         }
      }
   }
   else
   {
      const float sample = mix_be(in) * m;

      out[0] += sample;
      out[1] += sample;
   }
}

#ifdef MIX_SSE2
/* Four big-endian floats */
static INLINE __m128 mix_be4(const unsigned char *p)
{
   __m128i v = _mm_loadu_si128((const __m128i*)p);
   v = _mm_or_si128(_mm_slli_epi16(v, 8), _mm_srli_epi16(v, 8));
   v = _mm_shufflelo_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
   v = _mm_shufflehi_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
   return _mm_castsi128_ps(v);
}
#endif

int cell_audio_mix_port(float *out, unsigned out_channels, const void *in,
      unsigned in_channels, enum cell_audio_mix_down down, unsigned frames,
      float master, struct cell_audio_mix_level *level)
{
   const unsigned char *src = (const unsigned char*)in;
   const unsigned in_bytes  = in_channels * 4;
   unsigned i               = 0;
   int reached              = 0;
   float m;

   /* While the volume is moving: a frame at a time, each at its own */
   while (i < frames && level->step != 0.0f)
   {
      reached |= mix_step(level);
      mix_frame(out + i * out_channels, out_channels, src + i * in_bytes,
            in_channels, down, level->level * master);
      i++;
   }

   /* It is there: one volume for the rest */
   m = level->level * master;

#ifdef MIX_SSE2
   /* Two channels into two: four samples, two frames, at a time */
   if (in_channels == 2 && out_channels == 2)
   {
      const __m128 vm = _mm_set1_ps(m);

      for (; i + 2 <= frames; i += 2)
      {
         float *o        = out + i * 2;
         const __m128 s  = _mm_mul_ps(mix_be4(src + i * 8), vm);
         _mm_storeu_ps(o, _mm_add_ps(_mm_loadu_ps(o), s));
      }
   }
   /* Eight into eight as they are (7.1 to 7.1): eight samples a frame */
   else if (in_channels == 8 && out_channels == 8 && down == CELL_AUDIO_MIX_7_1)
   {
      const __m128 vm = _mm_set1_ps(m);

      for (; i < frames; i++)
      {
         float *o       = out + i * 8;
         const __m128 a = _mm_mul_ps(mix_be4(src + i * 32), vm);      /* L R C LFE */
         const __m128 b = _mm_mul_ps(mix_be4(src + i * 32 + 16), vm); /* SL SR RL RR */
         /* out 4,5 take the rear pair and 6,7 the side pair */
         const __m128 r = _mm_shuffle_ps(b, b, _MM_SHUFFLE(1, 0, 3, 2));
         _mm_storeu_ps(o,     _mm_add_ps(_mm_loadu_ps(o), a));
         _mm_storeu_ps(o + 4, _mm_add_ps(_mm_loadu_ps(o + 4), r));
      }
   }
   /* Eight down to two (a 7.1 port for stereo output, the usual case):
    * left and right side by side, each term in the order a frame at a
    * time has them */
   else if (in_channels == 8 && down == CELL_AUDIO_MIX_STEREO)
   {
      const __m128 vm   = _mm_set1_ps(m);
      const __m128 k3db = _mm_set1_ps(0.707f);
      const __m128 half = _mm_set1_ps(0.5f);

      for (; i < frames; i++)
      {
         float *o         = out + i * out_channels;
         const __m128 a   = _mm_mul_ps(mix_be4(src + i * 32), vm);      /* L R C LFE */
         const __m128 b   = _mm_mul_ps(mix_be4(src + i * 32 + 16), vm); /* SL SR RL RR */
         /* the centre's half, for both */
         const __m128 mid = _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 2, 2, 2)), half);
         const __m128 bh  = _mm_mul_ps(b, half);                        /* SL/2 SR/2 RL/2 RR/2 */
         __m128 lr        = _mm_add_ps(_mm_mul_ps(a, k3db), mid);       /* L*k + mid, R*k + mid */
         lr               = _mm_add_ps(lr, bh);                         /* + SL/2, + SR/2 */
         lr               = _mm_add_ps(lr, _mm_movehl_ps(bh, bh));      /* + RL/2, + RR/2 */
         lr               = _mm_add_ps(_mm_loadl_pi(_mm_setzero_ps(), (const __m64*)o), lr);
         _mm_storel_pi((__m64*)o, lr);
      }
   }
#endif

   for (; i < frames; i++)
      mix_frame(out + i * out_channels, out_channels, src + i * in_bytes,
            in_channels, down, m);

   return reached;
}
