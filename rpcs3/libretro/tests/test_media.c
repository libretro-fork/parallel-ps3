/* What cellSearch lists and cellMusicDecode plays (util/media_decode, on
 * libretro-common's audio_transfer, rtags, rmp4, rwebm and rmpeg1_ps)
 * against FFmpeg - the library they replaced - on files FFmpeg wrote
 * (fixtures/media/, made by media_fixtures_gen.sh):
 *   - music: MP3 at 44.1 kHz, AAC in M4A, WMA2, FLAC 5.1, WAVE mono at
 *     22.05 kHz, Vorbis at 32 kHz, each found by its bytes, with the
 *     codec, rate and length ffprobe gives (a quarter of a second; the
 *     MP3's without the padding FFmpeg's estimate counts), the tags it
 *     gives (title, artist, album, genre, track, disc, date; UTF-8), and
 *     played as utils::audio_decoder played it through FFmpeg - 48 kHz
 *     stereo, the 5.1 folded - the same length and signal as FFmpeg's
 *     decode and resampling (the resamplers differ, so by the error
 *     against it);
 *   - video: MPEG-4 Part 2 and AVC in MP4, AVC in Matroska, MPEG-1 and
 *     MPEG-2 program streams with MPEG audio or AC-3, XVID with MP3 and
 *     AVC with AC-3 in AVI, WMV2 with WMA2 in ASF, with the codecs
 *     and length ffprobe gives; a video file is not music and a music
 *     file is not video;
 *   - every prefix of each (in steps) probed and played inside it.
 * C89. */

#include <math.h>

#include "test_common.h"

#include <formats/rtags.h>
#include "../../util/media_decode.h"

struct music
{
   const char *file;
   int         codec;
   int         rate;
   double      snr;   /* dB against FFmpeg's, at least */
   bool        disc;  /* WAVE's INFO has no disc */
   bool        same_length;
};

static const struct music musics[] = {
   { "music.mp3",  MD_CODEC_MP3,    44100, 50, true,  true  },
   { "music.m4a",  MD_CODEC_AAC,    48000, 70, true,  true  },
   /* rwma plays WMA gapless; FFmpeg drops two frames at the start, one
    * of them audio, so the lengths differ: the same samples between */
   { "music.wma",  MD_CODEC_WMA,    44100, 50, true,  false },
   { "music.flac", MD_CODEC_FLAC,   48000, 70, true,  true  },
   { "music.wav",  MD_CODEC_LPCM,   22050, 50, false, true  },
   { "music.ogg",  MD_CODEC_VORBIS, 32000, 50, true,  true  }
};

struct video
{
   const char *file;
   int         video, audio;
   double      seconds;   /* ffprobe's */
};

static const struct video videos[] = {
   { "video_mpeg4.mp4",     MD_CODEC_MPEG4, MD_CODEC_AAC, 0.520 },
   { "video_avc.mp4",       MD_CODEC_AVC,   MD_CODEC_AAC, 0.520 },
   { "video_avc.mkv",       MD_CODEC_AVC,   MD_CODEC_MP3, 0.523 },
   { "video_mpeg1.mpg",     MD_CODEC_MPEG1, MD_CODEC_MP2, 0.504 },
   { "video_mpeg2.mpg",     MD_CODEC_MPEG2, MD_CODEC_MP2, 0.504 },
   { "video_mpeg2_ac3.vob", MD_CODEC_MPEG2, MD_CODEC_AC3, 0.384 },
   { "video_xvid.avi",      MD_CODEC_MPEG4, MD_CODEC_MP3, 0.560 },
   { "video_avc.avi",       MD_CODEC_AVC,   MD_CODEC_AC3, 0.608 },
   /* WMV2 is no codec cellSearch names */
   { "video_wmv2.wmv",      MD_CODEC_OTHER, MD_CODEC_WMA, 0.606 }
};

static void path_of(char *path, const char *dir, const char *name)
{
   sprintf(path, "%.400s/media/%.64s", dir, name);
}

/* the whole track at 48 kHz stereo; its frame count */
static size_t play(struct md_audio *a, const uint8_t *data, size_t len, float *out, size_t cap)
{
   static float chunk[MD_READ_MIN * 2];
   size_t       n = 0, got;
   uint64_t     ms = 0, last_ms = 0;
   if (!md_audio_open(a, data, len))
      return 0;
   while ((got = md_audio_read(a, chunk, MD_READ_MIN, &ms)) != 0)
   {
      TEST_CHECK(ms >= last_ms);
      last_ms = ms;
      if (out && n + got <= cap)
         memcpy(out + n * 2, chunk, got * 2 * sizeof(float));
      n += got;
   }
   md_audio_close(a);
   return n;
}

/* ours at a fractional frame position, channel @c, linearly */
static double at(const float *x, size_t n, double pos, int c)
{
   long   i = (long)floor(pos);
   double t = pos - i;
   if (i < 0 || (size_t)i + 1 >= n)
      return 0;
   return x[(size_t)i * 2 + c] * (1 - t) + x[(size_t)i * 2 + 2 + c] * t;
}

/* the error against FFmpeg's, in dB below its signal, ours put where it
 * lines up best: the decoders' and resamplers' delays differ, by up to
 * two WMA frames (rwma emits the two priming frames FFmpeg drops) and
 * by a fraction of a frame (the resamplers) */
static double snr(const float *ours, size_t n, const int16_t *ref, size_t rn, double *where)
{
   double best = -1e9, lag0 = 0, best_dot = -1e300, f;
   long   lag;
   size_t i;
   /* where: the best correlation */
   for (lag = -6000; lag <= 6000; lag++)
   {
      double dot = 0, eo = 0, er = 0;
      size_t used = 0;
      for (i = 256; i + 256 < rn; i++)
      {
         long j = (long)i + lag;
         int  c;
         if (j < 0 || (size_t)j >= n)
            continue;
         for (c = 0; c < 2; c++)
         {
            double r = ref[i * 2 + c], o = ours[(size_t)j * 2 + c];
            dot += r * o;
            eo  += o * o;
            er  += r * r;
         }
         used++;
      }
      /* normalised, over an overlap of at least half of FFmpeg's */
      if (used < rn / 2 || eo <= 0 || er <= 0)
         continue;
      dot /= sqrt(eo * er);
      if (dot > best_dot)
      {
         best_dot = dot;
         lag0     = lag;
      }
   }
   /* then to a sixteenth of a frame, with every frame */
   for (f = lag0 - 2; f <= lag0 + 2; f += 1.0 / 16)
   {
      double sig = 0, err = 0;
      for (i = 256; i + 256 < rn; i++)
      {
         int c;
         if (i + f < 0 || i + f + 1 >= n)
            continue;
         for (c = 0; c < 2; c++)
         {
            double r = ref[i * 2 + c] / 32768.0, o = at(ours, n, i + f, c);
            sig += r * r;
            err += (o - r) * (o - r);
         }
      }
      if (sig > 0 && 10 * log10(sig / (err + 1e-30)) > best)
      {
         best   = 10 * log10(sig / (err + 1e-30));
         *where = f;
      }
   }
   return best;
}

static void check_tags(const struct md_file *f, const struct music *m)
{
   static const char *const want[RTAGS_FIELDS] = {
      "T\xc3\xaftle\xe2\x98\x83", "\xc3\x84rtist", "Alb\xc3\xbcm", "Rock", "3/12", "1/2", "2004-05-01"
   };
   char v[256];
   int  k;
   for (k = 0; k < RTAGS_FIELDS; k++)
   {
      const char *w = (k == RTAGS_DISC && !m->disc) ? "" : want[k];
      rtags_get(f->data, f->len, (enum rtags_field)k, v, sizeof(v));
      if (strcmp(v, w))
      {
         fprintf(stderr, "test_media: %s: tag %d is \"%s\", ffprobe's \"%s\"\n", m->file, k, v, w);
         test_failures++;
      }
   }
}

static void harden(struct md_audio *a, const struct md_file *f)
{
   size_t len;
   for (len = 0; len <= f->len; len += 61)
   {
      uint8_t       *c = (uint8_t*)malloc(len ? len : 1);
      struct md_info info;
      memcpy(c, f->data, len);
      md_probe(c, len, false, &info);
      md_probe(c, len, true, &info);
      if (len % 7 == 0)
         play(a, c, len, NULL, 0);
      free(c);
   }
}

int main(int argc, char **argv)
{
   const char      *dir = argc > 1 ? argv[1] : "fixtures";
   struct md_audio *a   = md_audio_new();
   static float     out[48000 * 2];
   char             path[512];
   unsigned         i;
   struct md_file   f;

   TEST_CHECK(a != NULL);
   path_of(path, dir, "missing.mp3");
   TEST_CHECK(!md_file_open(&f, path));

   for (i = 0; i < sizeof(musics) / sizeof(musics[0]); i++)
   {
      const struct music *m = &musics[i];
      struct md_info      info;
      size_t              rlen = 0, n;
      int16_t            *ref;
      double              s, lag = 0;

      path_of(path, dir, m->file);
      TEST_CHECK(md_file_open(&f, path));
      if (!f.data)
         continue;
      strcat(path, ".s16");
      ref = (int16_t*)test_read_file(path, &rlen);
      TEST_CHECK(ref != NULL);

      TEST_CHECK(md_probe(f.data, f.len, false, &info));
      TEST_CHECK(info.audio_codec == m->codec && info.sample_rate == m->rate
            && info.video_codec == MD_CODEC_NONE);
      TEST_CHECK(fabs(info.duration_us - 250000.0) < 250000 * 0.03);
      check_tags(&f, m);

      n = play(a, f.data, f.len, out, 48000);
      s = ref ? snr(out, n, ref, rlen / 4, &lag) : 0;
      printf("test_media: %s: codec %d at %d Hz, %.3f s; %u frames at 48 kHz (FFmpeg's %u), "
            "%.1f dB from FFmpeg's\n",
            m->file, info.audio_codec, info.sample_rate, info.duration_us / 1e6,
            (unsigned)n, (unsigned)(rlen / 4), s);
      TEST_CHECK(!m->same_length || fabs((double)n - rlen / 4.0) <= rlen / 4.0 * 0.03);
      TEST_CHECK(s >= m->snr);
      TEST_CHECK(!md_probe(f.data, f.len, true, &info));

      harden(a, &f);
      free(ref);
      md_file_close(&f);
   }

   for (i = 0; i < sizeof(videos) / sizeof(videos[0]); i++)
   {
      const struct video *v = &videos[i];
      struct md_info      info;
      path_of(path, dir, v->file);
      TEST_CHECK(md_file_open(&f, path));
      if (!f.data)
         continue;
      TEST_CHECK(md_probe(f.data, f.len, true, &info));
      printf("test_media: %s: video %d, audio %d at %d Hz, %.3f s (ffprobe's %.3f)\n",
            v->file, info.video_codec, info.audio_codec, info.sample_rate,
            info.duration_us / 1e6, v->seconds);
      TEST_CHECK(info.video_codec == v->video && info.audio_codec == v->audio);
      TEST_CHECK(fabs(info.duration_us / 1e6 - v->seconds) <= v->seconds * 0.15);
      TEST_CHECK(!md_probe(f.data, f.len, false, &info));
      harden(a, &f);
      md_file_close(&f);
   }

   md_audio_free(a);
   if (test_failures)
   {
      fprintf(stderr, "test_media: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_media: ok\n");
   return 0;
}
