/* The RSX blit engine's scaled and converting copy (rsx_scale, on
 * libretro-common's scaler), against what swscale - the library it
 * replaced - wrote from the same pixels (fixtures/rsx_scale/, made with
 * FFmpeg 6.1's swscale):
 *   - R5G6B5 (big endian) to A8R8G8B8 at one size: the same bytes;
 *   - A8R8G8B8 scaled up to nearest samples: the same colour bytes,
 *     alpha within 1 (swscale rounds alpha through its own path; an odd
 *     source width and height put no output centre on a tie); scaled
 *     down, the nearest samples themselves, which swscale's are not;
 *   - A8R8G8B8 scaled up bilinear: within 2 of swscale's bilinear;
 *   - A8R8G8B8 to R5G6B5: each channel's top bits, where swscale, which
 *     goes through YUV with its colour shared by neighbouring pixels,
 *     strays by up to 16 levels on this noise (printed);
 * and on its own:
 *   - scaled with a conversion, each pixel is the conversion of the
 *     sample nearest its centre;
 *   - with a slice shorter than the source, the rows under it are never
 *     read and the output above them is the whole image's;
 *   - a second copy of the same size and formats takes no memory.
 * C89. */

#include "test_common.h"

#include "../../Emu/RSX/Utils/rsx_scale.h"

#define W  15
#define H  9
#define UW 37
#define UH 22
#define DW 7
#define DH 4

static unsigned allocations;

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
void *__wrap_malloc(size_t n)
{
   allocations++;
   return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t m)
{
   allocations++;
   return __real_calloc(n, m);
}
void *__wrap_realloc(void *p, size_t n)
{
   allocations++;
   return __real_realloc(p, n);
}

static uint8_t *fixture(const char *dir, const char *name, size_t want)
{
   char     path[512];
   size_t   len = 0;
   uint8_t *p;
   sprintf(path, "%.400s/rsx_scale/%s", dir, name);
   p = test_read_file(path, &len);
   if (!p || len != want)
   {
      fprintf(stderr, "test_rsx_scale: %s missing or %u bytes, not %u\n", path,
            (unsigned)len, (unsigned)want);
      exit(1);
   }
   return p;
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

/* the colour bytes the same, alpha within 1 */
static int same_point(const uint8_t *ours, int pitch, const uint8_t *sws, int w, int h)
{
   int x, y;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         const uint8_t *a = ours + y * pitch + x * 4, *b = sws + (y * w + x) * 4;
         if (a[1] != b[1] || a[2] != b[2] || a[3] != b[3] || a[0] - b[0] > 1 || b[0] - a[0] > 1)
            return 0;
      }
   return 1;
}

int main(int argc, char **argv)
{
   const char *dir = argc > 1 ? argv[1] : "fixtures";
   uint8_t *in_argb  = fixture(dir, "in_argb.raw", W * H * 4);
   uint8_t *in_565   = fixture(dir, "in_565.raw", W * H * 2);
   uint8_t *to_argb  = fixture(dir, "sws_565_to_argb.raw", W * H * 4);
   uint8_t *to_565   = fixture(dir, "sws_argb_to_565.raw", W * H * 2);
   uint8_t *up       = fixture(dir, "sws_argb_point_up.raw", UW * UH * 4);
   uint8_t *bil      = fixture(dir, "sws_argb_bilinear_up.raw", UW * UH * 4);
   static uint8_t out[UW * UH * 4], full[UW * UH * 4], cut[W * (H + 8) * 4];
   struct rsx_scale *s = rsx_scale_new();
   int x, y, d;

   TEST_CHECK(s != NULL);
   if (!s)
      return 1;

   /* conversions at one size */
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_A8R8G8B8, W, H, W * 4,
            in_565, RSX_SCALE_R5G6B5, W, H, W * 2, H, false));
   TEST_CHECK(!memcmp(out, to_argb, W * H * 4));
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_R5G6B5, W, H, W * 2,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, false));
   {
      int i, worst = 0;
      for (i = 0; i < W * H; i++)
      {
         const uint8_t *p = in_argb + i * 4;
         unsigned want = ((unsigned)(p[1] >> 3) << 11) | ((unsigned)(p[2] >> 2) << 5) | (p[3] >> 3);
         unsigned sws  = ((unsigned)to_565[i * 2] << 8) | to_565[i * 2 + 1];
         int dr = (int)(sws >> 11) - (int)(want >> 11);
         int db = (int)(sws & 31) - (int)(want & 31);
         TEST_CHECK(((unsigned)out[i * 2] << 8 | out[i * 2 + 1]) == want);
         dr = dr < 0 ? -dr : dr;
         db = db < 0 ? -db : db;
         if (dr > worst) worst = dr;
         if (db > worst) worst = db;
      }
      printf("test_rsx_scale: R5G6B5 from A8R8G8B8 exact; swscale's off by up to %d of 31\n", worst);
   }

   /* nearest samples up and down, a pitch wider than the row */
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_A8R8G8B8, UW, UH, UW * 4,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, false));
   TEST_CHECK(same_point(out, UW * 4, up, UW, UH));
   memset(cut, 0, sizeof(cut));
   TEST_CHECK(rsx_scale_image(s, cut, RSX_SCALE_A8R8G8B8, DW, DH, DW * 4 + 12,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, false));
   /* swscale's own "nearest" downscale is no pick of samples on this
    * noise - not one of its pixels is an input pixel - so the reference
    * is the nearest sample itself */
   for (y = 0; y < DH; y++)
      for (x = 0; x < DW; x++)
      {
         int sx = (2 * x + 1) * W / (2 * DW), sy = (2 * y + 1) * H / (2 * DH);
         TEST_CHECK(!memcmp(cut + y * (DW * 4 + 12) + x * 4, in_argb + (sy * W + sx) * 4, 4));
      }

   /* bilinear */
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_A8R8G8B8, UW, UH, UW * 4,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, true));
   d = max_diff(out, bil, UW * UH * 4);
   printf("test_rsx_scale: bilinear within %d of swscale's\n", d);
   TEST_CHECK(d <= 2);

   /* scaled with a conversion: the nearest sample, converted */
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_A8R8G8B8, UW, UH, UW * 4,
            in_565, RSX_SCALE_R5G6B5, W, H, W * 2, H, false));
   for (y = 0; y < UH; y++)
      for (x = 0; x < UW; x++)
      {
         int sx = (2 * x + 1) * W / (2 * UW), sy = (2 * y + 1) * H / (2 * UH);
         TEST_CHECK(!memcmp(out + (y * UW + x) * 4, to_argb + (sy * W + sx) * 4, 4));
      }
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_R5G6B5, DW, DH, DW * 2,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, false));
   for (y = 0; y < DH; y++)
      for (x = 0; x < DW; x++)
      {
         int sx = (2 * x + 1) * W / (2 * DW), sy = (2 * y + 1) * H / (2 * DH);
         const uint8_t *p = in_argb + (sy * W + sx) * 4;
         unsigned want = ((unsigned)(p[1] >> 3) << 11) | ((unsigned)(p[2] >> 2) << 5) | (p[3] >> 3);
         TEST_CHECK(((unsigned)out[(y * DW + x) * 2] << 8 | out[(y * DW + x) * 2 + 1]) == want);
      }

   /* a slice of 5 rows: the rows under it hold junk that must not show */
   memcpy(cut, in_argb, W * 5 * 4);
   memset(cut + W * 5 * 4, 0x5a, W * 4 * (H + 8 - 5));
   TEST_CHECK(rsx_scale_image(s, full, RSX_SCALE_R5G6B5, UW, UH, UW * 2,
            in_argb, RSX_SCALE_A8R8G8B8, W, H, W * 4, H, false));
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_R5G6B5, UW, UH, UW * 2,
            cut, RSX_SCALE_A8R8G8B8, W, H, W * 4, 5, false));
   for (y = 0; y < UH; y++)
      if ((2 * y + 1) * H / (2 * UH) < 5)
         TEST_CHECK(!memcmp(out + y * UW * 2, full + y * UW * 2, UW * 2));

   /* the same copy again takes no memory */
   allocations = 0;
   TEST_CHECK(rsx_scale_image(s, out, RSX_SCALE_R5G6B5, UW, UH, UW * 2,
            cut, RSX_SCALE_A8R8G8B8, W, H, W * 4, 5, false));
   TEST_CHECK(allocations == 0);

   rsx_scale_free(s);
   free(in_argb);
   free(in_565);
   free(to_argb);
   free(to_565);
   free(up);
   free(bil);
   if (test_failures)
   {
      fprintf(stderr, "test_rsx_scale: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_rsx_scale: ok\n");
   return 0;
}
