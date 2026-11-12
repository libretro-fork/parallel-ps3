/* cellAudio's mixer (Emu/Cell/Modules/cellAudio_mix.c) against the code it
 * replaces, kept here as the reference: every frame mixed one at a time,
 * with the port's level looked at before each.
 *
 * - the same block, bit for bit, for ports of one, two and eight channels
 *   into blocks of two, six and eight, with each of the three downmixes,
 *   for volumes that stand, that are on their way up or down and arrive in
 *   the block, and that do not arrive; samples at any address, blocks of
 *   any length; onto a block that already holds other ports;
 * - the level is left where the reference leaves it, and "arrived" is said
 *   when the reference settles it;
 * - what a block costs against the reference (AUDIO_BENCH=1).
 *
 * Usage: test_audio_mix */

#define _POSIX_C_SOURCE 199309L /* clock_gettime, for the stopwatch */

#include "../../Emu/Cell/Modules/cellAudio_mix.h"
#include "test_common.h"

#include <time.h> /* not pacing: the benchmark's stopwatch */

#define MAX_FRAMES 256

/* --- the reference: cell_audio_thread::mix's loops ---------------------- */

static float ref_be(const unsigned char *p)
{
   union { uint32_t u; float f; } v;
   v.u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
   return v.f;
}

/* port.level and port.level_set {value, inc}; the compare-and-swap that
 * settles level_set is counted */
struct ref_port { float level; float value; float inc; int settled; };

static float ref_step_volume(struct ref_port *port, float master_volume)
{
   if (port->inc != 0.0f)
   {
      const int dec = port->inc < 0.0f;
      port->level += port->inc;

      if ((!dec && port->value - port->level <= 0.0f) || (dec && port->value - port->level >= 0.0f))
      {
         port->level = port->value;
         port->inc   = 0.0f;
         port->settled++;
      }
   }

   return port->level * master_volume;
}

static void ref_mix(float *out_buffer, unsigned out_channels, const unsigned char *buf,
      unsigned num_channels, int downmix, unsigned frames, float master_volume, struct ref_port *port)
{
   static const float minus_3db = 0.707f;
   const unsigned out_buffer_sz = out_channels * frames;
   unsigned out, in;
   float m;

   if (num_channels == 2)
   {
      for (out = 0, in = 0; out < out_buffer_sz; out += out_channels, in += 2)
      {
         float left, right;
         m     = ref_step_volume(port, master_volume);
         left  = ref_be(buf + (in + 0) * 4) * m;
         right = ref_be(buf + (in + 1) * 4) * m;
         out_buffer[out + 0] += left;
         out_buffer[out + 1] += right;
      }
   }
   else if (num_channels == 8)
   {
      for (out = 0, in = 0; out < out_buffer_sz; out += out_channels, in += 8)
      {
         float left, right, center, low_freq, side_left, side_right, rear_left, rear_right;
         m          = ref_step_volume(port, master_volume);
         left       = ref_be(buf + (in + 0) * 4) * m;
         right      = ref_be(buf + (in + 1) * 4) * m;
         center     = ref_be(buf + (in + 2) * 4) * m;
         low_freq   = ref_be(buf + (in + 3) * 4) * m;
         side_left  = ref_be(buf + (in + 4) * 4) * m;
         side_right = ref_be(buf + (in + 5) * 4) * m;
         rear_left  = ref_be(buf + (in + 6) * 4) * m;
         rear_right = ref_be(buf + (in + 7) * 4) * m;

         if (downmix == CELL_AUDIO_MIX_STEREO)
         {
            const float mid = center * 0.5f;
            out_buffer[out + 0] += left * minus_3db + mid + side_left * 0.5f + rear_left * 0.5f;
            out_buffer[out + 1] += right * minus_3db + mid + side_right * 0.5f + rear_right * 0.5f;
         }
         else if (downmix == CELL_AUDIO_MIX_5_1)
         {
            out_buffer[out + 0] += left;
            out_buffer[out + 1] += right;
            if (out_channels >= 6)
            {
               out_buffer[out + 2] += center;
               out_buffer[out + 3] += low_freq;
               if (out_channels == 6)
               {
                  out_buffer[out + 4] += side_left + rear_left;
                  out_buffer[out + 5] += side_right + rear_right;
               }
               else
               {
                  out_buffer[out + 6] += side_left + rear_left;
                  out_buffer[out + 7] += side_right + rear_right;
               }
            }
         }
         else
         {
            out_buffer[out + 0] += left;
            out_buffer[out + 1] += right;
            if (out_channels >= 6)
            {
               out_buffer[out + 2] += center;
               out_buffer[out + 3] += low_freq;
               if (out_channels == 6)
               {
                  out_buffer[out + 4] += side_left;
                  out_buffer[out + 5] += side_right;
               }
               else
               {
                  out_buffer[out + 4] += rear_left;
                  out_buffer[out + 5] += rear_right;
                  out_buffer[out + 6] += side_left;
                  out_buffer[out + 7] += side_right;
               }
            }
         }
      }
   }
   else
   {
      for (out = 0, in = 0; out < out_buffer_sz; out += out_channels, in++)
      {
         float sample;
         m      = ref_step_volume(port, master_volume);
         sample = ref_be(buf + in * 4) * m;
         out_buffer[out + 0] += sample;
         out_buffer[out + 1] += sample;
      }
   }
}

/* --- the comparison ------------------------------------------------------ */

static uint32_t rng_state = 0x1234567;
static uint32_t rng(void)
{
   rng_state ^= rng_state << 13;
   rng_state ^= rng_state >> 17;
   rng_state ^= rng_state << 5;
   return rng_state;
}

/* A sample as a game makes them, with the odd one that is not a number,
 * endless or too small to be normal */
static void put_sample(unsigned char *p, unsigned n)
{
   union { uint32_t u; float f; } v;
   switch (n % 97)
   {
      case 13: v.u = 0x7f800000; break;                  /* infinity */
      case 29: v.u = 0x00000123; break;                  /* denormal */
      case 61: v.u = 0x80000000; break;                  /* -0, cellAudio's tag */
      default: v.f = ((float)(int)(rng() % 60001) - 30000.0f) / 20000.0f; break;
   }
   p[0] = (unsigned char)(v.u >> 24);
   p[1] = (unsigned char)(v.u >> 16);
   p[2] = (unsigned char)(v.u >> 8);
   p[3] = (unsigned char)v.u;
}

static unsigned long cases, differing;

static void compare(unsigned in_channels, unsigned out_channels, int down, unsigned frames,
      unsigned misalign, float master, float level, float target, float step)
{
   static unsigned char in_store[MAX_FRAMES * 8 * 4 + 8];
   static float out_new[MAX_FRAMES * 8 + 4], out_ref[MAX_FRAMES * 8 + 4];
   unsigned char *in = in_store + misalign;
   struct cell_audio_mix_level lv;
   struct ref_port port;
   unsigned i;
   int reached;

   for (i = 0; i < frames * in_channels; i++)
      put_sample(in + i * 4, i + (unsigned)cases);

   /* A block that already holds another port's samples; a guard after it */
   for (i = 0; i < MAX_FRAMES * 8 + 4; i++)
      out_new[i] = out_ref[i] = ((float)(int)(rng() % 2001) - 1000.0f) / 4000.0f;

   lv.level     = level;
   lv.target    = target;
   lv.step      = step;
   port.level   = level;
   port.value   = target;
   port.inc     = step;
   port.settled = 0;

   reached = cell_audio_mix_port(out_new, out_channels, in, in_channels,
         (enum cell_audio_mix_down)down, frames, master, &lv);
   ref_mix(out_ref, out_channels, in, in_channels, down, frames, master, &port);

   cases++;
   if (memcmp(out_new, out_ref, sizeof(out_new)) || memcmp(&lv.level, &port.level, sizeof(float))
         || reached != port.settled || (reached && lv.step != 0.0f) || lv.step != port.inc)
      differing++;
}

static double now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

int main(void)
{
   static const unsigned ins[3]    = { 1, 2, 8 };
   static const unsigned outs[3]   = { 2, 6, 8 };
   static const unsigned lens[5]   = { 256, 255, 1, 2, 3 };
   /* level, target, step: standing; rising and arriving; falling and
    * arriving; rising, not arriving; arriving on the first frame */
   static const float levels[6][3] = {
      { 1.0f, 1.0f, 0.0f },
      { 0.25f, 0.75f, 0.5f / 100.0f },
      { 1.0f, 0.0f, -1.0f / 200.0f },
      { 0.0f, 1.0f, 1.0f / 624.0f },
      { 0.5f, 0.5001f, 0.01f },
      { 0.3f, 0.3f, 0.0f }
   };
   unsigned a, b, c, d, e, mis;

   for (a = 0; a < 3; a++)
      for (b = 0; b < 3; b++)
         for (c = 0; c < 3; c++)
            for (d = 0; d < 5; d++)
               for (e = 0; e < 6; e++)
                  for (mis = 0; mis < 4; mis++)
                     compare(ins[a], outs[b], (int)c, lens[d], mis,
                           e & 1 ? 0.8f : 1.0f, levels[e][0], levels[e][1], levels[e][2]);

   TEST_CHECK(differing == 0);
   printf("audio mix: %lu blocks against the code replaced, %lu differ\n", cases, differing);

   /* The level and the arrival, by themselves */
   {
      static unsigned char in[MAX_FRAMES * 2 * 4];
      static float out[MAX_FRAMES * 2];
      struct cell_audio_mix_level lv;
      lv.level  = 0.0f;
      lv.target = 1.0f;
      lv.step   = 1.0f / 624.0f;
      TEST_CHECK(cell_audio_mix_port(out, 2, in, 2, CELL_AUDIO_MIX_STEREO, 256, 1.0f, &lv) == 0);
      TEST_CHECK(lv.level > 0.40f && lv.level < 0.42f && lv.step != 0.0f);
      TEST_CHECK(cell_audio_mix_port(out, 2, in, 2, CELL_AUDIO_MIX_STEREO, 256, 1.0f, &lv) == 0);
      TEST_CHECK(cell_audio_mix_port(out, 2, in, 2, CELL_AUDIO_MIX_STEREO, 256, 1.0f, &lv) == 1);
      TEST_CHECK(lv.level == 1.0f && lv.step == 0.0f);
      TEST_CHECK(cell_audio_mix_port(out, 2, in, 2, CELL_AUDIO_MIX_STEREO, 256, 1.0f, &lv) == 0);
   }

   if (getenv("AUDIO_BENCH"))
   {
      static unsigned char in[MAX_FRAMES * 8 * 4];
      static float out[MAX_FRAMES * 8];
      static const unsigned bench[3][3] = { { 2, 2, 0 }, { 8, 8, 2 }, { 8, 2, 0 } };
      static const char *names[3] = { "stereo into stereo", "7.1 into 7.1", "7.1 down to stereo" };
      unsigned k, i, pass;
      /* Ordinary samples: the odd ones (denormals above all) cost the
       * machine many times an ordinary one, in either mixer */
      for (i = 0; i < MAX_FRAMES * 8; i++)
         put_sample(in + i * 4, 1);

      for (k = 0; k < 3; k++)
      {
         double best_new = 1e18, best_ref = 1e18;
         for (pass = 0; pass < 9; pass++)
         {
            struct cell_audio_mix_level lv;
            struct ref_port port;
            double t0, t1, t2;
            lv.level = 0.9f; lv.target = 0.9f; lv.step = 0.0f;
            port.level = 0.9f; port.value = 0.9f; port.inc = 0.0f; port.settled = 0;
            t0 = now_ns();
            for (i = 0; i < 20000; i++)
            {
               memset(out, 0, sizeof(float) * 256 * bench[k][1]);
               cell_audio_mix_port(out, bench[k][1], in, bench[k][0], (enum cell_audio_mix_down)bench[k][2], 256, 0.8f, &lv);
            }
            t1 = now_ns();
            for (i = 0; i < 20000; i++)
            {
               memset(out, 0, sizeof(float) * 256 * bench[k][1]);
               ref_mix(out, bench[k][1], in, bench[k][0], (int)bench[k][2], 256, 0.8f, &port);
            }
            t2 = now_ns();
            if ((t1 - t0) / 20000 < best_new) best_new = (t1 - t0) / 20000;
            if ((t2 - t1) / 20000 < best_ref) best_ref = (t2 - t1) / 20000;
         }
         printf("audio mix: a block of 256 frames, %-20s %4.0f ns, the reference %4.0f ns\n", names[k], best_new, best_ref);
      }
   }

   printf("audio mix: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
