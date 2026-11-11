/* What cellRec records (util/media_encode, on libretro-common's
 * rmpeg4_encode, rh264_encode, rjpeg_encode, raac_encode, float_to_ulaw,
 * rmp4_mux, the scaler and image_yuv_420_from_bytes), read back by
 * FFmpeg - the library it replaced:
 *
 *   test_record DIR            writes DIR/rec_*.mp4, one for each of
 *                              cellRec's video codecs and audio formats,
 *                              from each input cellRec takes: frames
 *                              larger than the recording (RGBA, BGRA,
 *                              shrunk by the box average, then the
 *                              scaler), at its size (ARGB) and YUV 4:2:0
 *                              planes; a moving pattern, with a frame
 *                              skipped, and two tones with a gap of
 *                              silence. Each must open in md_probe with
 *                              its codecs and length, and me_open must
 *                              refuse what it does not take.
 *   test_record --compare DIR  reads FFmpeg's decode of each
 *                              (rec_check.sh: rec_*.probe, .pts, .yuv,
 *                              .f32) and checks the codecs FFmpeg names,
 *                              every frame written there at its time
 *                              (the skipped one's left out), each within
 *                              a PSNR
 *                              of the pattern at the recording's size,
 *                              and the audio within an SNR of what went
 *                              in (big-endian float PCM exact).
 * C89. */

#include <math.h>

#include "test_common.h"

#include "../../util/media_encode.h"
#include "../../util/media_decode.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FRAMES   20
#define SKIPPED  7       /* the frame time no frame is given for */
#define RATE     48000
#define TONE     24000   /* frames of each tone; a gap between them */
#define GAP      4096

struct rec
{
   const char *name;
   int         video, audio, input;
   unsigned    width, height;     /* the recording's */
   unsigned    in_w, in_h;        /* the frames given */
   uint32_t    video_bps, audio_bps;
   const char *ffmpeg_video, *ffmpeg_audio;
   double      psnr, snr;         /* at least */
};

static const struct rec recs[] = {
   /* cellRec's MPEG4 SMALL at 768k with AAC 96k, the game's frames at
    * twice the size: the box average alone */
   { "mpeg4_aac",  ME_VIDEO_MPEG4, ME_AUDIO_AAC,  ME_PIXEL_RGBA, 320, 240, 640, 480,
      768000, 96000, "mpeg4", "aac", 45, 40 },
   /* AVC MIDDLE 16:9 with u-law, the renderer's 720p BGRA frames: the
    * box average, then the scaler */
   { "h264_ulaw",  ME_VIDEO_H264,  ME_AUDIO_ULAW, ME_PIXEL_BGRA, 480, 272, 1280, 720,
      1024000, 384000, "h264", "pcm_mulaw", 42, 35 },
   /* MJPEG MIDDLE with float PCM, ARGB at the recording's size */
   { "mjpeg_pcm",  ME_VIDEO_MJPEG, ME_AUDIO_PCM_F32BE, ME_PIXEL_ARGB, 368, 272, 368, 272,
      5000000, 1536000, "mjpeg", "pcm_f32be", 33, 200 },
   /* AVC with AAC 64k from YUV 4:2:0 planes */
   { "h264_aac",   ME_VIDEO_H264,  ME_AUDIO_AAC,  ME_PIXEL_YUV420P, 320, 240, 320, 240,
      768000, 64000, "h264", "aac", 43, 38 }
};

/* the pattern, 0..255, at a point of the picture (0..1) and a time */
static double pattern(double u, double v, int t, int c)
{
   return 128 + 90 * sin(2 * M_PI * (u * (1.5 + c * 0.5) + v * (1.0 + c * 0.25)) + t * 0.3 + c);
}

static uint8_t clamp8(double x)
{
   return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : floor(x + 0.5));
}

/* BT.601 limited range, as the encoders take it */
static void rgb_to_yuv(double r, double g, double b, double *y, double *cb, double *cr)
{
   double l = 0.299 * r + 0.587 * g + 0.114 * b;
   *y  = 16 + l * 219 / 255;
   *cb = 128 + (b - l) / 1.772 * 224 / 255;
   *cr = 128 + (r - l) / 1.402 * 224 / 255;
}

/* frame @t as the game gives it */
static void make_frame(const struct rec *r, int t, uint8_t *buf)
{
   unsigned x, y, w = r->in_w, h = r->in_h;
   if (r->input == ME_PIXEL_YUV420P)
   {
      uint8_t *cb = buf + w * h, *cr = cb + (w / 2) * (h / 2);
      for (y = 0; y < h; y++)
         for (x = 0; x < w; x++)
         {
            double Y, U, V;
            rgb_to_yuv(pattern((x + 0.5) / w, (y + 0.5) / h, t, 0),
                  pattern((x + 0.5) / w, (y + 0.5) / h, t, 1),
                  pattern((x + 0.5) / w, (y + 0.5) / h, t, 2), &Y, &U, &V);
            buf[y * w + x] = clamp8(Y);
            if (!(x & 1) && !(y & 1))
            {
               rgb_to_yuv(pattern((x + 1.0) / w, (y + 1.0) / h, t, 0),
                     pattern((x + 1.0) / w, (y + 1.0) / h, t, 1),
                     pattern((x + 1.0) / w, (y + 1.0) / h, t, 2), &Y, &U, &V);
               cb[(y / 2) * (w / 2) + x / 2] = clamp8(U);
               cr[(y / 2) * (w / 2) + x / 2] = clamp8(V);
            }
         }
      return;
   }
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         uint8_t *p = buf + ((size_t)y * w + x) * 4;
         uint8_t  c[3];
         int      k;
         for (k = 0; k < 3; k++)
            c[k] = clamp8(pattern((x + 0.5) / w, (y + 0.5) / h, t, k));
         switch (r->input)
         {
            case ME_PIXEL_RGBA: p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 0x11; break;
            case ME_PIXEL_BGRA: p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; p[3] = 0x22; break;
            default:            p[0] = 0x33; p[1] = c[0]; p[2] = c[1]; p[3] = c[2]; break;
         }
      }
}

/* frame @i of the audio, channel @c */
static float tone(long i, int c)
{
   if (i < TONE)
      return (float)(0.4 * sin(2 * M_PI * (c ? 660 : 440) * i / RATE));
   if (i < TONE + GAP)
      return 0;
   i -= TONE + GAP;
   return (float)(0.3 * sin(2 * M_PI * (c ? 330 : 880) * i / RATE));
}

#define AUDIO_FRAMES (TONE * 2 + GAP)

static void path_of(char *path, const char *dir, const char *name, const char *ext)
{
   sprintf(path, "%.400s/rec_%.32s%.8s", dir, name, ext);
}

static void write_one(const struct rec *r, const char *dir)
{
   struct me_params   p;
   struct me_encoder *e;
   char               path[512];
   size_t             in_bytes = r->input == ME_PIXEL_YUV420P
      ? (size_t)r->in_w * r->in_h * 3 / 2 : (size_t)r->in_w * r->in_h * 4;
   uint8_t           *frame = (uint8_t*)malloc(in_bytes);
   static float       pcm[256 * 2];
   long               i, j;
   int                t;
   struct md_file     f;
   struct md_info     info;

   memset(&p, 0, sizeof(p));
   path_of(path, dir, r->name, ".mp4");
   p.path          = path;
   p.video_codec   = r->video;
   p.width         = r->width;
   p.height        = r->height;
   p.fps           = 30;
   p.video_bitrate = r->video_bps;
   p.gop           = 12;
   p.audio_codec   = r->audio;
   p.sample_rate   = RATE;
   p.channels      = 2;
   p.audio_bitrate = r->audio_bps;
   p.max_seconds   = 60;
   e = me_open(&p);
   TEST_CHECK(e != NULL);
   if (!e || !frame)
   {
      free(frame);
      return;
   }

   /* the audio in cellAudio's blocks of 256, the gap as silence asked
    * for, and the frames between */
   for (i = 0, t = 0; i < AUDIO_FRAMES; i += 256)
   {
      long n = AUDIO_FRAMES - i < 256 ? AUDIO_FRAMES - i : 256;
      if (i >= TONE && i + n <= TONE + GAP)
         TEST_CHECK(me_audio(e, NULL, (size_t)n));
      else
      {
         for (j = 0; j < n; j++)
         {
            pcm[j * 2]     = tone(i + j, 0);
            pcm[j * 2 + 1] = tone(i + j, 1);
         }
         TEST_CHECK(me_audio(e, pcm, (size_t)n));
      }
      if (t < FRAMES && i >= (long)t * (AUDIO_FRAMES / FRAMES))
      {
         if (t != SKIPPED)
         {
            make_frame(r, t, frame);
            TEST_CHECK(me_video(e, frame, (size_t)r->in_w * 4, r->in_w, r->in_h, r->input, t));
            /* the same time again is not shown twice */
            TEST_CHECK(me_video(e, frame, (size_t)r->in_w * 4, r->in_w, r->in_h, r->input, t));
         }
         t++;
      }
   }
   while (t < FRAMES)
   {
      if (t != SKIPPED)
      {
         make_frame(r, t, frame);
         TEST_CHECK(me_video(e, frame, (size_t)r->in_w * 4, r->in_w, r->in_h, r->input, t));
      }
      t++;
   }
   TEST_CHECK(me_close(e));
   free(frame);

   /* what cellSearch would read of it */
   TEST_CHECK(md_file_open(&f, path));
   if (f.data)
   {
      TEST_CHECK(md_probe(f.data, f.len, true, &info));
      printf("test_record: %s: %u bytes, video %d, audio %d, %.3f s\n", r->name,
            (unsigned)f.len, info.video_codec, info.audio_codec, info.duration_us / 1e6);
      TEST_CHECK(info.video_codec == (r->video == ME_VIDEO_MPEG4 ? MD_CODEC_MPEG4
               : r->video == ME_VIDEO_H264 ? MD_CODEC_AVC : MD_CODEC_OTHER));
      TEST_CHECK(r->audio != ME_AUDIO_AAC || info.audio_codec == MD_CODEC_AAC);
      /* 20 frames at 30 a second; the audio a little longer */
      TEST_CHECK(info.duration_us >= 650000 && info.duration_us <= 1200000);
      md_file_close(&f);
   }
}

static void refusals(const char *dir)
{
   struct me_params p;
   char             path[512];
   memset(&p, 0, sizeof(p));
   path_of(path, dir, "refused", ".mp4");
   p.path = path; p.video_codec = ME_VIDEO_MPEG4; p.width = 320; p.height = 240;
   p.fps = 30; p.video_bitrate = 768000; p.audio_codec = ME_AUDIO_AAC;
   p.sample_rate = RATE; p.channels = 2; p.audio_bitrate = 96000; p.max_seconds = 1;
   p.width = 321;
   TEST_CHECK(me_open(&p) == NULL);   /* odd */
   p.width = 320; p.channels = 6;
   TEST_CHECK(me_open(&p) == NULL);
   p.channels = 2; p.video_codec = 9;
   TEST_CHECK(me_open(&p) == NULL);
   p.video_codec = ME_VIDEO_MPEG4;
   path_of(path, dir, "no/such/dir/x", ".mp4");
   TEST_CHECK(me_open(&p) == NULL);
}

/* ---------------------------------------------------------------- */
/* FFmpeg's decode                                                  */
/* ---------------------------------------------------------------- */

static void compare_one(const struct rec *r, const char *dir)
{
   char     path[512], *probe, *times;
   size_t   ylen = 0, alen = 0, plen = 0, tlen = 0, fsize = (size_t)r->width * r->height * 3 / 2;
   uint8_t *yuv;
   float   *pcm;
   unsigned k, frames, x, y;
   int      t;
   double   worst = 1e9, snr_best = -1e9;
   long     lag, best_lag = 0, n;

   path_of(path, dir, r->name, ".probe");
   probe = (char*)test_read_file(path, &plen);
   path_of(path, dir, r->name, ".pts");
   times = (char*)test_read_file(path, &tlen);
   path_of(path, dir, r->name, ".yuv");
   yuv = (uint8_t*)test_read_file(path, &ylen);
   path_of(path, dir, r->name, ".f32");
   pcm = (float*)test_read_file(path, &alen);
   TEST_CHECK(probe && times && yuv && pcm);
   if (!probe || !times || !yuv || !pcm)
      goto done;

   /* each frame shown at the time it was given: frame periods, the
    * skipped one's left a gap */
   {
      const char *c = times;
      int         at = 0, shown = 0, timed = 1;
      while (c < times + tlen)
      {
         double v = atof(c);
         if (at == SKIPPED)
            at++;
         if (fabs(v - at / 30.0) > 0.001)
            timed = 0;
         at++;
         shown++;
         while (c < times + tlen && *c != '\n')
            c++;
         c++;
      }
      TEST_CHECK(timed && shown == FRAMES - 1);
   }
   probe = (char*)realloc(probe, plen + 1);
   probe[plen] = 0;

   {
      char want[64];
      sprintf(want, "codec_name=%s\n", r->ffmpeg_video);
      TEST_CHECK(strstr(probe, want) != NULL);
      sprintf(want, "codec_name=%s\n", r->ffmpeg_audio);
      TEST_CHECK(strstr(probe, want) != NULL);
   }

   /* every frame given, in order, each against the pattern at the
    * recording's size */
   frames = (unsigned)(ylen / fsize);
   TEST_CHECK(frames == FRAMES - 1);
   for (k = 0, t = 0; k < frames && t < FRAMES; k++, t++)
   {
      double err = 0, psnr;
      if (t == SKIPPED)
         t++;
      for (y = 0; y < r->height; y++)
         for (x = 0; x < r->width; x++)
         {
            double Y, U, V, d;
            rgb_to_yuv(pattern((x + 0.5) / r->width, (y + 0.5) / r->height, t, 0),
                  pattern((x + 0.5) / r->width, (y + 0.5) / r->height, t, 1),
                  pattern((x + 0.5) / r->width, (y + 0.5) / r->height, t, 2), &Y, &U, &V);
            d    = yuv[k * fsize + (size_t)y * r->width + x] - Y;
            err += d * d;
         }
      psnr = 10 * log10(255.0 * 255.0 / (err / ((double)r->width * r->height) + 1e-9));
      if (psnr < worst)
         worst = psnr;
   }

   /* the audio where it matches best, which has to be where it went
    * in: AAC's priming packet is the edit list's to skip */
   n = (long)(alen / 8);
   for (lag = 0; lag <= 2048; lag++)
   {
      double sig = 0, err = 0, s;
      long   i;
      if (lag + AUDIO_FRAMES > n + 1024)
         break;
      for (i = 0; i < AUDIO_FRAMES && i + lag < n; i++)
      {
         int c;
         for (c = 0; c < 2; c++)
         {
            double a = tone(i, c), b = pcm[(i + lag) * 2 + c];
            sig += a * a;
            err += (a - b) * (a - b);
         }
      }
      s = err > 0 ? 10 * log10(sig / err) : 200;
      if (s > snr_best)
      {
         snr_best = s;
         best_lag = lag;
      }
   }
   printf("test_record: %s: FFmpeg reads %s and %s, %u frames, worst %.1f dB PSNR; "
         "audio %.1f dB SNR at a delay of %ld\n", r->name, r->ffmpeg_video, r->ffmpeg_audio,
         frames, worst, snr_best, best_lag);
   TEST_CHECK(worst >= r->psnr);
   TEST_CHECK(snr_best >= r->snr);
   TEST_CHECK(best_lag == 0);
   TEST_CHECK(n >= AUDIO_FRAMES);

done:
   free(probe);
   free(times);
   free(yuv);
   free(pcm);
}

int main(int argc, char **argv)
{
   unsigned i;
   bool     compare = argc > 2 && !strcmp(argv[1], "--compare");
   const char *dir = compare ? argv[2] : argc > 1 ? argv[1] : ".";

   for (i = 0; i < sizeof(recs) / sizeof(recs[0]); i++)
   {
      if (compare)
         compare_one(&recs[i], dir);
      else
         write_one(&recs[i], dir);
   }
   if (!compare)
      refusals(dir);

   if (test_failures)
   {
      fprintf(stderr, "test_record: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_record: ok\n");
   return 0;
}
