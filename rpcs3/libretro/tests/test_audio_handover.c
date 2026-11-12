/* The game's audio to the frontend (libretro/libretro_audio_out.c) against
 * the code it replaces, kept here as the reference: what libretro_audio.cpp
 * did each retro_run, 512 frames at a time with the speakers worked out for
 * every piece. Both are run on the same stream of the game's samples and
 * the frontend's side records everything it is handed:
 *
 * - the same samples, the same channel count and speakers, the same amount
 *   every run, for floats to a frontend that takes them, floats to one that
 *   takes s16 only, s16 from the game, the classic stereo callback, 5.1 and
 *   7.1, a layout whose speakers are not as many as the channels, NTSC's
 *   800.8 frames a run, 50 and 30 runs a second;
 * - silence, the same amount, when a pull gives nothing or less than asked;
 * - one pull and one call to the frontend a run where the reference made
 *   two (at 60 runs a second);
 * - the same with the game lending its samples where they lie: the same
 *   output, the frontend handed the game's own memory, every loan given
 *   back once, and a pull instead whenever nothing is lent;
 * - what a run costs against the reference (AUDIO_BENCH=1).
 *
 * Usage: test_audio_handover */

#define _POSIX_C_SOURCE 199309L /* clock_gettime, for the stopwatch */

#include "../libretro_audio_out.h"
#include "test_common.h"

#include <audio/conversion/float_to_s16.h>

#include <stdlib.h>
#include <time.h> /* not pacing: the benchmark's stopwatch */

/* --- the frontend: everything handed over, as s16 or float ------------- */

#define REC_SAMPLES (1 << 22)

struct recording
{
   float *f32;
   int16_t *s16;
   size_t f32_count, s16_count;
   size_t calls, frames;
   unsigned channels, speakers;
   unsigned format_changes;
   const void *last_data; /* what the frontend was last handed */
};

static struct recording rec_new, rec_ref;
static struct recording *rec;
static int rec_on = 1;

static void rec_format(unsigned channels, unsigned speakers)
{
   if (channels != rec->channels || speakers != rec->speakers)
      rec->format_changes = rec->format_changes * 31 + channels * 4099 + speakers + (unsigned)rec->frames;
   rec->channels = channels;
   rec->speakers = speakers;
}

static size_t RETRO_CALLCONV take_stereo(const int16_t *data, size_t frames)
{
   rec->calls++;
   rec->last_data = data;
   if (rec_on)
   {
      rec_format(2, 3);
      memcpy(rec->s16 + rec->s16_count, data, frames * 2 * sizeof(int16_t));
      rec->s16_count += frames * 2;
   }
   rec->frames += frames;
   return frames;
}

static size_t RETRO_CALLCONV take_multi_s16(const int16_t *data, size_t frames, unsigned channels, unsigned speakers)
{
   rec->calls++;
   rec->last_data = data;
   if (rec_on)
   {
      rec_format(channels, speakers);
      memcpy(rec->s16 + rec->s16_count, data, frames * channels * sizeof(int16_t));
      rec->s16_count += frames * channels;
   }
   rec->frames += frames;
   return frames;
}

static size_t RETRO_CALLCONV take_multi_float(const float *data, size_t frames, unsigned channels, unsigned speakers)
{
   rec->calls++;
   rec->last_data = data;
   if (rec_on)
   {
      rec_format(channels, speakers);
      memcpy(rec->f32 + rec->f32_count, data, frames * channels * sizeof(float));
      rec->f32_count += frames * channels;
   }
   rec->frames += frames;
   return frames;
}

/* --- the game: a stream whose every sample says where it is ------------ */

struct game
{
   unsigned long produced; /* samples so far */
   unsigned long pulls;
   unsigned channels, speakers;
   int is_float;
   unsigned give_num, give_den; /* gives give_num of every give_den pulls in full */
   unsigned short_by;           /* the others: this many frames short (or nothing) */
   int stopped;
};

static float sample_f32(unsigned long n) { return (float)((long)(n % 20001) - 10000) / 10000.0f; }
static int16_t sample_s16(unsigned long n) { return (int16_t)((long)(n % 60001) - 30000); }

static size_t game_frames(struct game *g, size_t max_frames)
{
   size_t frames = max_frames;
   g->pulls++;
   if (g->stopped)
      return 0;
   if (g->give_den && (g->pulls % g->give_den) >= g->give_num)
      frames = g->short_by && max_frames > g->short_by ? max_frames - g->short_by : 0;
   return frames;
}

static void game_fill(struct game *g, void *out, size_t frames)
{
   size_t i, n = frames * g->channels;
   if (g->is_float)
      for (i = 0; i < n; i++)
         ((float*)out)[i] = sample_f32(g->produced++);
   else
      for (i = 0; i < n; i++)
         ((int16_t*)out)[i] = sample_s16(g->produced++);
}

static size_t pull_new(void *ctx, void *out, size_t max_frames, struct lr_audio_format *format)
{
   struct game *g = (struct game*)ctx;
   size_t frames  = game_frames(g, max_frames);
   if (frames)
   {
      game_fill(g, out, frames);
      format->channels = g->channels;
      format->speakers = g->speakers;
      format->is_float = g->is_float;
   }
   return frames;
}

/* --- the game lending its samples where they lie ------------------------ */

#define LEND_SAMPLES (1 << 21)
static float *lend_f32;     /* the stream, sample n at [n] */
static int16_t *lend_s16;
static unsigned long lend_borrows, lend_returns, lend_outstanding, lend_refused;
static unsigned lend_every; /* lends every this-many-th time it is asked; 1: always */
static int lend_ok = 1;     /* every hand-over read the lent memory itself */

static size_t borrow_new(void *ctx, const void **ptr, size_t frames, struct lr_audio_format *format)
{
   struct game *g = (struct game*)ctx;
   lend_borrows++;
   if (g->stopped || (lend_every > 1 && lend_borrows % lend_every) || lend_outstanding)
   {
      lend_refused++;
      return 0;
   }
   if (g->produced + frames * g->channels > LEND_SAMPLES)
      return 0;
   *ptr = g->is_float ? (const void*)(lend_f32 + g->produced) : (const void*)(lend_s16 + g->produced);
   format->channels = g->channels;
   format->speakers = g->speakers;
   format->is_float = g->is_float;
   lend_outstanding = frames;
   return frames;
}

static void return_new(void *ctx, size_t frames)
{
   struct game *g = (struct game*)ctx;
   lend_returns++;
   if (frames != lend_outstanding)
      lend_ok = 0;
   /* The frontend was handed the lent memory itself, unless it had to be
    * converted to s16 first */
   if (frames && rec->last_data != (g->is_float ? (const void*)(lend_f32 + g->produced) : (const void*)(lend_s16 + g->produced))
         && !(g->is_float && rec->s16_count))
      lend_ok = 0;
   g->produced     += frames * g->channels;
   lend_outstanding = 0;
}

static int lending; /* compare() runs the new code with the lending game */

/* --- the reference: libretro_audio.cpp's libretro_audio_process -------- */

#define REF_BATCH 512

static retro_audio_sample_batch_multi_int16_t ref_multi_s16;
static retro_audio_sample_batch_multi_float_t ref_multi_float;
static double ref_owed;
static unsigned ref_channels = 2, ref_layout;
static union { float f32[REF_BATCH * 8]; int16_t s16[REF_BATCH * 8]; } ref_raw;
static int16_t ref_s16[REF_BATCH * 8];

static unsigned ref_popcount(unsigned v) { unsigned c = 0; for (; v; v &= v - 1) c++; return c; }

/* speaker_mask(): the layout's speakers when they are as many as the
 * channels, else those of the automatic layout for the channel count */
static unsigned ref_speaker_mask(unsigned layout, unsigned channels)
{
   if (layout && ref_popcount(layout) == channels)
      return layout;
   switch (channels)
   {
      case 6: return 0x001 | 0x002 | 0x004 | 0x008 | 0x200 | 0x400;
      case 8: return 0x001 | 0x002 | 0x004 | 0x008 | 0x010 | 0x020 | 0x200 | 0x400;
      case 1: return 0x004;
      default: break;
   }
   return 0x001 | 0x002;
}

static void ref_run(retro_audio_sample_batch_t stereo, struct game *g, double frame_rate)
{
   size_t remaining;
   ref_owed += 48000.0 / frame_rate;
   remaining = (size_t)ref_owed;
   ref_owed -= (double)remaining;

   while (remaining > 0)
   {
      size_t want       = remaining < REF_BATCH ? remaining : REF_BATCH;
      int is_float      = 0;
      unsigned channels = 2, layout = 0, mask;
      size_t frames     = game_frames(g, want);
      size_t i;

      if (frames)
      {
         game_fill(g, &ref_raw, frames);
         is_float     = g->is_float;
         channels     = g->channels;
         layout       = g->speakers;
         ref_channels = channels;
         ref_layout   = layout;
      }
      if (frames < want)
      {
         if (!frames)
         {
            channels = ref_channels;
            layout   = ref_layout;
            is_float = 0;
         }
         if (is_float)
            for (i = frames * channels; i < want * channels; i++)
               ref_raw.f32[i] = 0.0f;
         else
            for (i = frames * channels; i < want * channels; i++)
               ref_raw.s16[i] = 0;
         frames = want;
      }

      mask = ref_speaker_mask(layout, channels);
      if (is_float && ref_multi_s16 && ref_multi_float)
         ref_multi_float(ref_raw.f32, frames, channels, mask);
      else
      {
         const int16_t *s16 = ref_raw.s16;
         if (is_float)
         {
            convert_float_to_s16(ref_s16, ref_raw.f32, frames * channels);
            s16 = ref_s16;
         }
         if (ref_multi_s16)
            ref_multi_s16(s16, frames, channels, mask);
         else if (channels == 2)
            stereo(s16, frames);
      }
      remaining -= frames;
   }
}

/* --- one case, both ways ------------------------------------------------ */

static void compare(const char *name, int multi, int floats_taken, const struct game *setup,
      double frame_rate, int runs, int stop_at)
{
   struct game g_new = *setup, g_ref = *setup;
   int i;
   size_t per_run_new = 0, per_run_ref = 0;
   int same_per_run = 1;

   rec_new.f32_count = rec_new.s16_count = rec_new.calls = rec_new.frames = 0;
   rec_ref.f32_count = rec_ref.s16_count = rec_ref.calls = rec_ref.frames = 0;
   rec_new.channels = rec_ref.channels = rec_new.speakers = rec_ref.speakers = 0;
   rec_new.format_changes = rec_ref.format_changes = 0;

   lr_audio_out_set_multi(multi ? take_multi_s16 : NULL, floats_taken ? take_multi_float : NULL);
   ref_multi_s16   = multi ? take_multi_s16 : NULL;
   ref_multi_float = floats_taken ? take_multi_float : NULL;

   for (i = 0; i < runs; i++)
   {
      size_t before;
      g_new.stopped = g_ref.stopped = stop_at && i >= stop_at;

      rec    = &rec_new;
      before = rec->frames;
      if (lending)
         lr_audio_out_run_lent(take_stereo, pull_new, borrow_new, return_new, &g_new, frame_rate);
      else
         lr_audio_out_run(take_stereo, pull_new, &g_new, frame_rate);
      per_run_new = rec->frames - before;

      rec    = &rec_ref;
      before = rec->frames;
      ref_run(take_stereo, &g_ref, frame_rate);
      per_run_ref = rec->frames - before;

      /* Both carry their own fraction from the case before: a run may
       * differ by the one frame that fraction decides, never more */
      if (per_run_new > per_run_ref + 1 || per_run_ref > per_run_new + 1)
         same_per_run = 0;
   }

   TEST_CHECK(same_per_run);
   TEST_CHECK(rec_new.frames + 1 >= rec_ref.frames && rec_new.frames <= rec_ref.frames + 1);
   TEST_CHECK(rec_new.channels == rec_ref.channels && rec_new.speakers == rec_ref.speakers);

   /* The same samples in the same order, as far as both got */
   {
      size_t n = rec_new.f32_count < rec_ref.f32_count ? rec_new.f32_count : rec_ref.f32_count;
      size_t m = rec_new.s16_count < rec_ref.s16_count ? rec_new.s16_count : rec_ref.s16_count;
      unsigned ch = rec_ref.channels ? rec_ref.channels : 2;
      TEST_CHECK(rec_new.f32_count + ch >= rec_ref.f32_count && rec_new.f32_count <= rec_ref.f32_count + ch);
      TEST_CHECK(rec_new.s16_count + ch >= rec_ref.s16_count && rec_new.s16_count <= rec_ref.s16_count + ch);
      TEST_CHECK(n + m > 0 || (setup->channels != 2 && !multi));
      TEST_CHECK(!memcmp(rec_new.f32, rec_ref.f32, n * sizeof(float)));
      TEST_CHECK(!memcmp(rec_new.s16, rec_ref.s16, m * sizeof(int16_t)));
   }

   printf("audio out: %-34s %7lu frames, %5lu calls and %5lu pulls where the reference made %5lu and %5lu\n",
         name, (unsigned long)rec_new.frames, (unsigned long)rec_new.calls, g_new.pulls,
         (unsigned long)rec_ref.calls, g_ref.pulls);
}

static double now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* The game's side for the stopwatch: a copy out of a buffer, as cellAudio's
 * ring gives its samples */
static float bench_src[LR_AUDIO_OUT_FRAMES * 8];

static size_t pull_bench(void *ctx, void *out, size_t max_frames, struct lr_audio_format *format)
{
   struct game *g = (struct game*)ctx;
   memcpy(out, bench_src, max_frames * g->channels * sizeof(float));
   format->channels = g->channels;
   format->speakers = g->speakers;
   format->is_float = 1;
   return max_frames;
}

static void ref_bench_run(struct game *g, double frame_rate)
{
   /* ref_run with the same copy for the game's side */
   size_t remaining;
   ref_owed += 48000.0 / frame_rate;
   remaining = (size_t)ref_owed;
   ref_owed -= (double)remaining;
   while (remaining > 0)
   {
      size_t want = remaining < REF_BATCH ? remaining : REF_BATCH;
      unsigned mask;
      memcpy(&ref_raw, bench_src, want * g->channels * sizeof(float));
      ref_channels = g->channels;
      ref_layout   = g->speakers;
      mask = ref_speaker_mask(g->speakers, g->channels);
      if (ref_multi_float)
         ref_multi_float(ref_raw.f32, want, g->channels, mask);
      else
      {
         convert_float_to_s16(ref_s16, ref_raw.f32, want * g->channels);
         ref_multi_s16(ref_s16, want, g->channels, mask);
      }
      remaining -= want;
   }
}

int main(void)
{
   struct game g;
   const double ntsc = 60.0 * 1000.0 / 1001.0;

   rec_new.f32 = (float*)malloc(REC_SAMPLES * sizeof(float));
   rec_new.s16 = (int16_t*)malloc(REC_SAMPLES * sizeof(int16_t));
   rec_ref.f32 = (float*)malloc(REC_SAMPLES * sizeof(float));
   rec_ref.s16 = (int16_t*)malloc(REC_SAMPLES * sizeof(int16_t));
   TEST_CHECK(rec_new.f32 && rec_new.s16 && rec_ref.f32 && rec_ref.s16);
   convert_float_to_s16_init_simd();

   memset(&g, 0, sizeof(g));
   g.channels = 2;
   g.is_float = 1;

   compare("floats, stereo, 60", 1, 1, &g, 60.0, 600, 0);
   TEST_CHECK(rec_new.calls == 600 && rec_ref.calls == 1200); /* one call a run for two */
   TEST_CHECK(rec_new.f32_count == 600 * 800 * 2 && rec_new.s16_count == 0);
   TEST_CHECK(rec_new.speakers == (RETRO_AUDIO_SPEAKER_FRONT_LEFT | RETRO_AUDIO_SPEAKER_FRONT_RIGHT));

   compare("floats to s16, multi, 60", 1, 0, &g, 60.0, 600, 0);
   TEST_CHECK(rec_new.s16_count == 600 * 800 * 2 && rec_new.f32_count == 0);

   compare("floats to s16, classic stereo", 0, 0, &g, 60.0, 600, 0);
   TEST_CHECK(rec_new.s16_count == 600 * 800 * 2);

   compare("NTSC, 800.8 a run", 0, 0, &g, ntsc, 1001, 0);
   TEST_CHECK(rec_new.frames + 1 >= 801600 && rec_new.frames <= 801600 + 1);

   compare("50 runs a second", 1, 1, &g, 50.0, 500, 0);
   compare("30 runs a second", 1, 1, &g, 30.0, 300, 0); /* 1600 a run: two pieces */

   g.is_float = 0;
   compare("s16 from the game", 1, 1, &g, 60.0, 600, 0);
   TEST_CHECK(rec_new.s16_count == 600 * 800 * 2 && rec_new.f32_count == 0);
   g.is_float = 1;

   g.channels = 6;
   g.speakers = RETRO_AUDIO_SPEAKER_FRONT_LEFT | RETRO_AUDIO_SPEAKER_FRONT_RIGHT | RETRO_AUDIO_SPEAKER_FRONT_CENTER
      | RETRO_AUDIO_SPEAKER_LOW_FREQUENCY | RETRO_AUDIO_SPEAKER_SIDE_LEFT | RETRO_AUDIO_SPEAKER_SIDE_RIGHT;
   compare("5.1 floats", 1, 1, &g, 60.0, 300, 0);
   TEST_CHECK(rec_new.channels == 6 && rec_new.speakers == g.speakers);
   /* The same channel count with other speakers: the layout handed over
    * follows */
   g.speakers = 0x03f;
   compare("5.1 with back speakers", 1, 1, &g, 60.0, 300, 0);
   TEST_CHECK(rec_new.speakers == 0x03f);
   compare("5.1 without multi-channel: dropped", 0, 0, &g, 60.0, 60, 0);
   TEST_CHECK(rec_new.calls == 0 && rec_ref.calls == 0);

   g.channels = 8;
   g.speakers = 0; /* a layout that names no speakers */
   compare("7.1, the usual speakers", 1, 1, &g, 60.0, 300, 0);
   TEST_CHECK(rec_new.channels == 8 && rec_new.speakers == 0x63f);

   /* A stereo game under a 5.1 layout: as many speakers as channels */
   g.channels = 2;
   g.speakers = 0x60f;
   compare("stereo under a 5.1 layout", 1, 1, &g, 60.0, 300, 0);
   TEST_CHECK(rec_new.speakers == 3);
   g.speakers = 0;

   /* Nothing to pull from the 300th run on: silence, the same amount */
   compare("stopped after 300 runs", 1, 1, &g, 60.0, 600, 300);
   TEST_CHECK(rec_new.frames == 600 * 800);
   TEST_CHECK(rec_new.s16_count == 300 * 800 * 2); /* silence goes out as s16 */
   {
      size_t i, loud = 0;
      for (i = 0; i < rec_new.s16_count; i++)
         loud += rec_new.s16[i] != 0;
      TEST_CHECK(loud == 0);
   }

   /* The reference pulls twice a run, so a pull that comes short now and
    * then lands on different pulls; with every pull short the two agree:
    * the rest of each piece is silence */
   g.give_den = 1;
   g.give_num = 0;
   g.short_by = 0;
   compare("every pull gives nothing", 1, 1, &g, 60.0, 120, 0);
   TEST_CHECK(rec_new.frames == 120 * 800);
   g.give_den = 0;

   /* A pull 100 frames short, alone: the frames, then silence to a frame's
    * worth, in the game's format */
   {
      struct game s;
      size_t i, loud = 0;
      memset(&s, 0, sizeof(s));
      s.channels = 2;
      s.is_float = 1;
      s.give_den = 1;
      s.give_num = 0;
      s.short_by = 100;
      rec = &rec_new;
      rec->f32_count = rec->s16_count = rec->frames = rec->calls = 0;
      lr_audio_out_set_multi(take_multi_s16, take_multi_float);
      lr_audio_out_run(take_stereo, pull_new, &s, 60.0);
      TEST_CHECK(rec->calls == 1 && rec->frames == 800 && rec->f32_count == 1600);
      for (i = 0; i < 1400; i++)
         TEST_CHECK(rec->f32[i] == sample_f32((unsigned long)i));
      for (i = 1400; i < 1600; i++)
         loud += rec->f32[i] != 0.0f;
      TEST_CHECK(loud == 0);
   }

   /* A pull that claims more than was asked, or a channel count there is no
    * room for: nothing of it is handed over */
   TEST_CHECK(lr_audio_out_speakers(0, 2) == 3 && lr_audio_out_speakers(0, 1) == 4);
   TEST_CHECK(lr_audio_out_speakers(0x60f, 6) == 0x60f && lr_audio_out_speakers(0x60f, 2) == 3);

   /* The same again with the game lending its samples: the same output,
    * read from the game's own memory, every loan given back once */
   {
      unsigned long n;
      lend_f32 = (float*)malloc(LEND_SAMPLES * sizeof(float));
      lend_s16 = (int16_t*)malloc(LEND_SAMPLES * sizeof(int16_t));
      TEST_CHECK(lend_f32 && lend_s16);
      for (n = 0; n < LEND_SAMPLES; n++)
      {
         lend_f32[n] = sample_f32(n);
         lend_s16[n] = sample_s16(n);
      }
      lending = 1;

      memset(&g, 0, sizeof(g));
      g.channels = 2;
      g.is_float = 1;
      lend_every = 1;
      lend_borrows = lend_returns = lend_refused = 0;
      compare("lent: floats, stereo, 60", 1, 1, &g, 60.0, 600, 0);
      TEST_CHECK(rec_new.calls == 600 && rec_new.f32_count == 600 * 800 * 2);
      TEST_CHECK(lend_borrows == 600 && lend_returns == 600 && lend_refused == 0);

      compare("lent: floats to s16", 1, 0, &g, 60.0, 600, 0);
      TEST_CHECK(rec_new.s16_count == 600 * 800 * 2);
      compare("lent: classic stereo", 0, 0, &g, 60.0, 600, 0);
      compare("lent: 30 runs a second", 1, 1, &g, 30.0, 300, 0);
      compare("lent: NTSC", 1, 1, &g, ntsc, 600, 0);

      g.is_float = 0;
      compare("lent: s16 from the game", 1, 1, &g, 60.0, 600, 0);
      TEST_CHECK(rec_new.s16_count == 600 * 800 * 2);
      g.is_float = 1;

      g.channels = 6;
      g.speakers = 0x60f;
      compare("lent: 5.1 floats", 1, 1, &g, 60.0, 300, 0);
      TEST_CHECK(rec_new.channels == 6 && rec_new.speakers == 0x60f);
      g.channels = 2;
      g.speakers = 0;

      /* Lent only every third time: the rest is pulled, the stream goes on */
      lend_every = 3;
      lend_borrows = lend_returns = lend_refused = 0;
      compare("lent: one time in three", 1, 1, &g, 60.0, 600, 0);
      TEST_CHECK(lend_refused == 400 && lend_returns == 200);
      lend_every = 1;

      compare("lent: stopped after 300 runs", 1, 1, &g, 60.0, 600, 300);
      TEST_CHECK(rec_new.frames == 600 * 800);

      TEST_CHECK(lend_ok);
      TEST_CHECK(lend_outstanding == 0);
      lending = 0;
   }

   if (getenv("AUDIO_BENCH"))
   {
      const int runs = 2000000;
      int i, pass;
      double best_new[2] = {1e18, 1e18}, best_ref[2] = {1e18, 1e18};
      struct game b;
      memset(&b, 0, sizeof(b));
      b.channels = 2;
      for (i = 0; i < (int)(sizeof(bench_src) / sizeof(bench_src[0])); i++)
         bench_src[i] = sample_f32((unsigned long)i);
      rec_on = 0;

      for (pass = 0; pass < 14; pass++)
      {
         const int floats = pass & 1;
         double t0, t1, t2;
         lr_audio_out_set_multi(take_multi_s16, floats ? take_multi_float : NULL);
         ref_multi_s16   = take_multi_s16;
         ref_multi_float = floats ? take_multi_float : NULL;
         rec = &rec_new;
         t0  = now_ns();
         for (i = 0; i < runs / 10; i++)
            lr_audio_out_run(take_stereo, pull_bench, &b, 60.0);
         t1  = now_ns();
         rec = &rec_ref;
         for (i = 0; i < runs / 10; i++)
            ref_bench_run(&b, 60.0);
         t2  = now_ns();
         if ((t1 - t0) / (runs / 10) < best_new[floats]) best_new[floats] = (t1 - t0) / (runs / 10);
         if ((t2 - t1) / (runs / 10) < best_ref[floats]) best_ref[floats] = (t2 - t1) / (runs / 10);
      }
      {
         double best_lent = 1e18;
         lr_audio_out_set_multi(take_multi_s16, take_multi_float);
         rec = &rec_new;
         b.is_float = 1;
         for (pass = 0; pass < 7; pass++)
         {
            double t0 = now_ns(), t1;
            for (i = 0; i < runs / 10; i++)
            {
               b.produced = 0;
               lr_audio_out_run_lent(take_stereo, pull_bench, borrow_new, return_new, &b, 60.0);
            }
            t1 = now_ns();
            if ((t1 - t0) / (runs / 10) < best_lent) best_lent = (t1 - t0) / (runs / 10);
         }
         printf("audio out: a run of 800 stereo frames, floats lent:  %.0f ns\n", best_lent);
      }
      printf("audio out: a run of 800 stereo frames, floats taken: %.0f ns, the reference %.0f ns\n", best_new[1], best_ref[1]);
      printf("audio out: a run of 800 stereo frames, to s16:       %.0f ns, the reference %.0f ns\n", best_new[0], best_ref[0]);
   }

   free(lend_f32);
   free(lend_s16);
   free(rec_new.f32);
   free(rec_new.s16);
   free(rec_ref.f32);
   free(rec_ref.s16);
   printf("audio out: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
