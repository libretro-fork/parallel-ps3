/* cellVdec's decoders (cellVdec_codec, on libretro-common's rh264,
 * rmpeg1_video and rmpeg4_video) against FFmpeg - the library they
 * replaced - on streams FFmpeg encoded (fixtures/vdec/: 64x48 testsrc2,
 * 12 pictures with two B pictures between references, each access unit
 * with its PTS as a transport stream gave it):
 *   - every picture comes out, in display order, with the tag of the
 *     access unit it came from (its PTS the next one up), and its coding
 *     type: I, then two B pictures before each P (and one before the
 *     last, which closes the stream);
 *   - its samples are FFmpeg's: the same bytes for AVC, within 1 for
 *     MPEG-2 and MPEG-4 Part 2 (their inverse transforms may differ by
 *     what IEEE 1180 allows);
 *   - the frame rate is the stream's, 25;
 *   - B-skip drops the B pictures and PB-skip all but the I;
 * and the conversions a game asks for, against swscale's with the
 * flags the module gave it: RGBA and ARGB within 3, UYVY the same.
 * C89. */

#include "test_common.h"

#include <encodings/deflate.h>
#include "../../Emu/Cell/Modules/cellVdec_codec.h"

#define W      64
#define H      48
#define FRAMES 12
#define PIC    (W * H * 3 / 2)

static uint8_t *fixture(const char *dir, const char *name, size_t *len)
{
   char     path[512];
   uint8_t *p;
   sprintf(path, "%.400s/vdec/%s", dir, name);
   p = test_read_file(path, len);
   if (!p)
   {
      fprintf(stderr, "test_vdec: %s missing\n", path);
      exit(1);
   }
   return p;
}

static uint8_t *gunzip(const char *dir, const char *name, size_t want)
{
   size_t   in_len = 0, taken = 0, wrote = 0;
   uint8_t *in  = fixture(dir, name, &in_len);
   uint8_t *out = (uint8_t*)malloc(want + 1);
   void    *z   = rinflate_new(16 + 15);
   int      r;
   rinflate_set_in(z, in, in_len);
   rinflate_set_out(z, out, want + 1);
   r = rinflate_process(z, &taken, &wrote);
   rinflate_free(z);
   free(in);
   if (r != RDEFLATE_PROCESS_END || wrote != want)
   {
      fprintf(stderr, "test_vdec: %s does not inflate to %u bytes\n", name, (unsigned)want);
      exit(1);
   }
   return out;
}

static int max_diff(const uint8_t *a, const uint8_t *b, size_t n)
{
   size_t i;
   int    worst = 0;
   for (i = 0; i < n; i++)
   {
      int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
      if (d > worst)
         worst = d;
   }
   return worst;
}

static uint32_t le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Decodes a fixture's units with @skip; returns the pictures, checking
 * each against FFmpeg's (@ref, display order) when @ref is given */
static unsigned run(int codec, const uint8_t *aus, size_t len, const uint8_t *ref,
      int tolerance, int skip, int *worst, struct vdc_pic *keep)
{
   struct vdc    *d = vdc_new(codec);
   struct vdc_pic pic;
   uint32_t       pts[FRAMES], last_pts = 0;
   unsigned       n = 0, outs = 0, i;
   size_t         at = 0;

   memset(&pic, 0, sizeof(pic));
   *worst = 0;
   TEST_CHECK(d != NULL);
   if (!d)
      return 0;
   vdc_start(d);
   for (;;)
   {
      int more = at + 8 <= len && n < FRAMES;
      if (more)
      {
         uint32_t size = le32(aus + at);
         pts[n] = le32(aus + at + 4);
         vdc_feed(d, aus + at + 8, size, n, skip);
         at += 8 + size;
         n++;
      }
      else
         vdc_feed(d, NULL, 0, 0, VDC_SKIP_NONE);
      while (vdc_next(d, &pic))
      {
         int diff;
         TEST_CHECK(pic.width == W && pic.height == H && pic.tag < n);
         if (pic.tag >= n)
            continue;
         /* display order: each picture's PTS above the last */
         TEST_CHECK(!outs || pts[pic.tag] > last_pts);
         last_pts = pts[pic.tag];
         if (ref)
         {
            /* the picture FFmpeg showed at that place */
            unsigned place = 0;
            for (i = 0; i < n; i++)
               place += pts[i] < pts[pic.tag];
            diff = max_diff(pic.mem, ref + (size_t)place * PIC, PIC);
            if (diff > *worst)
               *worst = diff;
            TEST_CHECK(diff <= tolerance);
            TEST_CHECK(pic.type == (place == 0 ? VDC_PIC_I
                     : place % 3 == 0 || place == FRAMES - 1 ? VDC_PIC_P : VDC_PIC_B));
         }
         if (skip == VDC_SKIP_B)
            TEST_CHECK(pic.type != VDC_PIC_B);
         if (skip == VDC_SKIP_PB)
            TEST_CHECK(pic.type == VDC_PIC_I);
         if (keep && outs == 0)
         {
            keep->width  = pic.width;
            keep->height = pic.height;
            memcpy(keep->mem, pic.mem, PIC);
         }
         outs++;
      }
      if (!more)
         break;
   }
   {
      unsigned num = 0, den = 0;
      TEST_CHECK(vdc_framerate(d, &num, &den) == 0 && num == 25 * den);
   }
   TEST_CHECK(vdc_picture_size(d) == PIC && vdc_errors(d) == 0);
   vdc_pic_free(&pic);
   vdc_free(d);
   return outs;
}

int main(int argc, char **argv)
{
   static const char *names[3] = { "m2", "avc", "m4" };
   static const int   tol[3]   = { 1, 0, 1 };
   const char *dir = argc > 1 ? argv[1] : "fixtures";
   static uint8_t first[PIC], out[W * H * 4];
   struct vdc_pic keep;
   int codec;

   memset(&keep, 0, sizeof(keep));
   keep.mem = first;

   for (codec = 0; codec < 3; codec++)
   {
      char     name[64];
      size_t   len = 0;
      uint8_t *aus, *ref;
      int      worst, dummy;
      unsigned outs, outs_b, outs_pb;

      sprintf(name, "%s.aus", names[codec]);
      aus = fixture(dir, name, &len);
      sprintf(name, "ffmpeg_%s.yuv.gz", names[codec]);
      ref = gunzip(dir, name, (size_t)FRAMES * PIC);

      outs    = run(codec, aus, len, ref, tol[codec], VDC_SKIP_NONE, &worst, codec == 1 ? &keep : NULL);
      outs_b  = run(codec, aus, len, NULL, 0, VDC_SKIP_B, &dummy, NULL);
      outs_pb = run(codec, aus, len, NULL, 0, VDC_SKIP_PB, &dummy, NULL);
      printf("test_vdec: %s: %u pictures, in display order with their units' tags, within %d of FFmpeg's; "
            "B-skip %u, PB-skip %u\n", names[codec], outs, worst, outs_b, outs_pb);
      TEST_CHECK(outs == FRAMES && outs_b == 5 && outs_pb == 1);
      free(aus);
      free(ref);
   }

   /* the conversions, of the AVC stream's first picture */
   {
      static const struct { const char *file; int format; size_t size; int tol; } conv[3] = {
         { "sws_rgba.raw",    VDC_OUT_RGBA, W * H * 4, 3 },
         { "sws_argb.raw",    VDC_OUT_ARGB, W * H * 4, 3 },
         { "sws_uyvy422.raw", VDC_OUT_UYVY, W * H * 2, 0 }
      };
      int k;
      for (k = 0; k < 3; k++)
      {
         size_t   len = 0;
         uint8_t *ref = fixture(dir, conv[k].file, &len);
         int      d;
         size_t   i;
         TEST_CHECK(len == conv[k].size);
         TEST_CHECK(vdc_convert(out, &keep, conv[k].format, 0x80, false));
         /* swscale's alpha is opaque; the module asks for the game's */
         for (i = 0; conv[k].format != VDC_OUT_UYVY && i < W * H; i++)
         {
            size_t a = conv[k].format == VDC_OUT_RGBA ? i * 4 + 3 : i * 4;
            TEST_CHECK(out[a] == 0x80);
            out[a] = ref[a];
         }
         d = max_diff(out, ref, len);
         printf("test_vdec: %s: within %d of swscale's\n", conv[k].file, d);
         TEST_CHECK(d <= conv[k].tol);
         free(ref);
      }
      TEST_CHECK(vdc_convert(out, &keep, VDC_OUT_YUV420, 0, false) && !memcmp(out, first, PIC));
      TEST_CHECK(!vdc_convert(out, &keep, 7, 0, false));
   }

   if (test_failures)
   {
      fprintf(stderr, "test_vdec: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_vdec: ok\n");
   return 0;
}
