/* cellAtracXdec's decode (libretro-common's ratrac3p, then
 * cellAtracXdec_pcm into the game's buffer) against what the core's
 * FFmpeg path gave for the same frames (fixtures/atracx/): the bundled
 * FFmpeg 8.1.1's ATRAC3plus decoder, then RPCS3's map from FFmpeg's
 * channel order to the LLE decoder's. The frames are each channel
 * configuration, 1 to 8 channels, built from the channel blocks of
 * RetroArch's ATRAC3plus test streams so every channel carries a
 * different signal; the reference holds the first 256 and the last 256
 * samples of the two frames, in the LLE's order:
 *   - float output: within 3e-7 of FFmpeg's, clipped as the LLE clips;
 *   - 16, 24 and 32-bit output: within what that difference makes at
 *     each width (1, 2 and 512), from FFmpeg's samples converted as
 *     the module converted them;
 * which also holds every channel where the module put it. Then frames
 * games handed the decoder, which it once refused, the last of each
 * against FFmpeg's in the LLE's order: eight of the 5.1 sound of a
 * game's opening movie (stereo, mono, stereo, mono blocks, the second
 * channel of the first block taking every tone band from the first),
 * and two games' music frames with a tone frequency at the top of its
 * range, each followed by itself. And on its own: a
 * frame that does not parse gives -1, and nothing is written for it.
 * C89. */

#include <math.h>

#include "test_common.h"

#include <formats/ratrac3p.h>
#include "../../Emu/Cell/Modules/cellAtracXdec_pcm.h"

static const unsigned frame_len[8] = { 0, 1072, 1872, 2944, 4008, 5896, 6960, 7776 };

static uint8_t *fixture(const char *dir, const char *name, size_t want)
{
   char     path[512];
   size_t   len = 0;
   uint8_t *p;
   sprintf(path, "%.400s/atracx/%s", dir, name);
   p = test_read_file(path, &len);
   if (!p || len != want)
   {
      fprintf(stderr, "test_atracx: %s missing or %u bytes, not %u\n", path,
            (unsigned)len, (unsigned)want);
      exit(1);
   }
   return p;
}

static float le_float(const uint8_t *p)
{
   uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
              | ((uint32_t)p[3] << 24);
   float    f;
   memcpy(&f, &u, 4);
   return f;
}

static long be_word(const uint8_t *p, unsigned bytes)
{
   if (bytes == 2)
      return (int16_t)((p[0] << 8) | p[1]);
   return (long)(int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | p[3]);
}

/* the module's conversion of a float sample, as it was written */
static long convert(float v, unsigned word)
{
   switch (word)
   {
      case 2:
         return v >= 1.0f ? 32767 : v <= -1.0f ? -32768 : (long)floor(v * 32768.0f);
      case 3:
         return v >= 1.0f ? 0x007fffffL : v <= -1.0f ? 0x00800000L
            : (long)(((unsigned long)(long)floor(v * 8388608.0f)) & 0x00ffffffUL);
      default:
         return v >= 1.0f ? 2147483647L : v <= -1.0f ? -2147483647L - 1
            : (long)floor(v * 2147483648.0);
   }
}

/* Frames a game handed the decoder: all decode, and the last is
 * FFmpeg's in the LLE's order, within tol of each channel's peak (the
 * channels differ in level, so one in the wrong place is far outside
 * that). */
static void game_frames(const char *dir, const char *name, unsigned ba,
      unsigned n, unsigned cfg, double tol)
{
   static float   pcm[RATRAC3P_SAMPLES * 8];
   static uint8_t out[RATRAC3P_SAMPLES * 8 * 4];
   char        file[64];
   unsigned    nch = atxpcm_channels(cfg), f, i, ch;
   uint8_t    *frames, *ref;
   ratrac3p_t *d   = ratrac3p_new(nch, ba);
   double      worst = 0, peak[8];

   sprintf(file, "frames_%.40s.raw", name);
   frames = fixture(dir, file, (size_t)ba * n);
   sprintf(file, "ffmpeg_%.40s.f32", name);
   ref    = fixture(dir, file, (size_t)RATRAC3P_SAMPLES * nch * 4);
   TEST_CHECK(d != NULL);
   for (f = 0; d && f < n; f++)
      TEST_CHECK(ratrac3p_decode_float(d, frames + (size_t)f * ba, ba, pcm)
            == RATRAC3P_SAMPLES);
   atxpcm_convert(out, pcm, cfg, 0x84, RATRAC3P_SAMPLES);
   for (ch = 0; ch < nch; ch++)
      peak[ch] = 0;
   for (i = 0; i < RATRAC3P_SAMPLES * nch; i++)
   {
      float    v, r = le_float(ref + (size_t)i * 4);
      uint32_t u    = (uint32_t)be_word(out + (size_t)i * 4, 4);
      double   dd;
      memcpy(&v, &u, 4);
      /* clipped as the LLE clips */
      if (r >= 0.99999994f)
         r = 0.99999994f;
      if (r <= -1.0f)
         r = -1.0f;
      dd = fabs((double)v - r);
      if (dd > worst)
         worst = dd;
      if (fabs(r) > peak[i % nch])
         peak[i % nch] = fabs(r);
   }
   printf("test_atracx: a game's frames (%s): float within %.1e of FFmpeg's\n",
         name, worst);
   for (ch = 0; ch < nch; ch++)
      TEST_CHECK(peak[ch] > 0 && worst < peak[ch] * tol);
   ratrac3p_free(d);
   free(frames);
   free(ref);
}

int main(int argc, char **argv)
{
   const char *dir = argc > 1 ? argv[1] : "fixtures";
   static float   pcm[RATRAC3P_SAMPLES * 8];
   static uint8_t out[RATRAC3P_SAMPLES * 8 * 4];
   unsigned cfg;

   for (cfg = 1; cfg <= 7; cfg++)
   {
      unsigned   nch = atxpcm_channels(cfg), ba = frame_len[cfg], f, w;
      char       name[64];
      uint8_t   *frames, *ref;
      ratrac3p_t *d[4];
      double     worst_f = 0;
      long       worst[5] = { 0, 0, 0, 0, 0 };

      sprintf(name, "frames_cfg%u.raw", cfg);
      frames = fixture(dir, name, (size_t)ba * 2);
      sprintf(name, "ffmpeg_cfg%u.f32", cfg);
      ref = fixture(dir, name, (size_t)512 * nch * 4);

      /* one decoder for each output word, each fed both frames */
      for (w = 0; w < 4; w++)
         TEST_CHECK((d[w] = ratrac3p_new(nch, ba)) != NULL);

      for (f = 0; f < 2; f++)
         for (w = 0; w < 4; w++)
         {
            static const unsigned words[4] = { 0x84, 2, 3, 4 };
            unsigned word = words[w], bytes = word == 2 ? 2 : 4, i, ch;
            TEST_CHECK(ratrac3p_decode_float(d[w], frames + (size_t)f * ba, ba, pcm)
                  == RATRAC3P_SAMPLES);
            memset(out, 0xee, sizeof(out));
            atxpcm_convert(out, pcm, cfg, word, RATRAC3P_SAMPLES);
            for (i = 0; i < 256; i++)
               for (ch = 0; ch < nch; ch++)
               {
                  /* frame 0's first 256 samples, frame 1's last 256 */
                  unsigned s  = f ? RATRAC3P_SAMPLES - 256 + i : i;
                  float    r  = le_float(ref + ((size_t)(f * 256 + i) * nch + ch) * 4);
                  const uint8_t *o = out + ((size_t)s * nch + ch) * bytes;
                  if (word == 0x84)
                  {
                     float    v;
                     uint32_t u = (uint32_t)be_word(o, 4);
                     double   dd;
                     memcpy(&v, &u, 4);
                     if (r >= 0.99999994f)
                        r = 0.99999994f;
                     if (r <= -1.0f)
                        r = -1.0f;
                     dd = fabs((double)v - r);
                     if (dd > worst_f)
                        worst_f = dd;
                  }
                  else
                  {
                     long dd = be_word(o, bytes) - convert(r, word);
                     if (word == 3)
                     {
                        /* 24 bits in a 32-bit word: compare them signed */
                        long a = be_word(o, 4), b = convert(r, word);
                        a = a & 0x800000L ? a - 0x1000000L : a;
                        b = b & 0x800000L ? b - 0x1000000L : b;
                        dd = a - b;
                     }
                     dd = dd < 0 ? -dd : dd;
                     if (dd > worst[word])
                        worst[word] = dd;
                  }
               }
         }

      printf("test_atracx: %u channel%s: float within %.1e of FFmpeg's, 16-bit %ld, 24-bit %ld, 32-bit %ld\n",
            nch, nch > 1 ? "s" : "", worst_f, worst[2], worst[3], worst[4]);
      TEST_CHECK(worst_f <= 3e-7);
      TEST_CHECK(worst[2] <= 1 && worst[3] <= 2 && worst[4] <= 512);

      /* a broken frame: refused, and no samples to write */
      memset(frames, 0xff, ba);
      memset(out, 0xee, sizeof(out));
      TEST_CHECK(ratrac3p_decode_float(d[0], frames, ba, pcm) < 0);
      atxpcm_convert(out, pcm, cfg, 0x84, 0);
      TEST_CHECK(out[0] == 0xee && out[sizeof(out) - 1] == 0xee);

      for (w = 0; w < 4; w++)
         ratrac3p_free(d[w]);
      free(frames);
      free(ref);
   }

   game_frames(dir, "ttt2", 1368, 8, 5, 1e-5);
   game_frames(dir, "t5dr", 688, 2, 2, 1e-5);
   game_frames(dir, "ttt2music", 1368, 2, 2, 1e-5);

   if (test_failures)
   {
      fprintf(stderr, "test_atracx: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_atracx: ok\n");
   return 0;
}
