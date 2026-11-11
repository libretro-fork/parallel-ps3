/* cellVpost's picture conversion (cellVpost_conv, on libretro-common's
 * image_yuv_blit and scaler), against what swscale - the library it
 * replaced, with the flags the module gave it - wrote from the same
 * picture (fixtures/vpost/, FFmpeg 6.1):
 *   - YUV 4:2:0 to RGBA at one size, BT.601 broadcast range and BT.709
 *     full range: within 3;
 *   - scaled up and down: no farther on average from a careful scaling
 *     of the same picture (swscale's Lanczos with its chroma
 *     interpolated at full resolution, ref_*.raw) than swscale's own
 *     bilinear was - on this picture a little nearer scaled up and five
 *     times nearer scaled down, where swscale's skipped samples;
 * and on its own:
 *   - every pixel's alpha is the game's, scaled or not;
 *   - a second picture of the same sizes takes no memory.
 * C89. */

#include "test_common.h"

#include "../../Emu/Cell/Modules/cellVpost_conv.h"

#define W 32
#define H 16

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
   sprintf(path, "%.400s/vpost/%s", dir, name);
   p = test_read_file(path, &len);
   if (!p || len != want)
   {
      fprintf(stderr, "test_vpost: %s missing or %u bytes, not %u\n", path,
            (unsigned)len, (unsigned)want);
      exit(1);
   }
   return p;
}

/* the colour channels' largest and mean difference; the alpha bytes all
 * @alpha */
static void compare(const uint8_t *a, const uint8_t *b, size_t pixels, uint8_t alpha,
      int *worst, double *mean, int *alpha_ok)
{
   size_t i;
   long   sum = 0;
   *worst    = 0;
   *alpha_ok = 1;
   for (i = 0; i < pixels * 4; i++)
   {
      int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
      if ((i & 3) == 3)
      {
         if (a[i] != alpha)
            *alpha_ok = 0;
         continue;
      }
      sum += d;
      if (d > *worst)
         *worst = d;
   }
   *mean = (double)sum / (double)(pixels * 3);
}

int main(int argc, char **argv)
{
   const char *dir = argc > 1 ? argv[1] : "fixtures";
   uint8_t *in      = fixture(dir, "in_yuv420.raw", W * H * 3 / 2);
   uint8_t *same    = fixture(dir, "sws_rgba.raw", W * H * 4);
   uint8_t *up      = fixture(dir, "sws_rgba_up.raw", 2 * W * 2 * H * 4);
   uint8_t *down    = fixture(dir, "sws_rgba_down.raw", 20 * 10 * 4);
   uint8_t *full709 = fixture(dir, "sws_rgba_full709.raw", W * H * 4);
   uint8_t *ref_up  = fixture(dir, "ref_rgba_up.raw", 2 * W * 2 * H * 4);
   uint8_t *ref_down = fixture(dir, "ref_rgba_down.raw", 20 * 10 * 4);
   static uint8_t out[2 * W * 2 * H * 4];
   struct vpost_conv *c = vpost_conv_new();
   int    worst, alpha_ok;
   double mean;

   TEST_CHECK(c != NULL);
   if (!c)
      return 1;

   TEST_CHECK(vpost_conv_exec(c, out, W, H, in, W, H, false, false, 0x40));
   compare(out, same, W * H, 0x40, &worst, &mean, &alpha_ok);
   printf("test_vpost: BT.601 broadcast, one size: within %d of swscale\n", worst);
   TEST_CHECK(worst <= 3 && alpha_ok);

   TEST_CHECK(vpost_conv_exec(c, out, W, H, in, W, H, true, true, 0xff));
   compare(out, full709, W * H, 0xff, &worst, &mean, &alpha_ok);
   printf("test_vpost: BT.709 full, one size: within %d of swscale\n", worst);
   TEST_CHECK(worst <= 3 && alpha_ok);

   {
      double ours, sws;
      int    dummy;
      TEST_CHECK(vpost_conv_exec(c, out, 2 * W, 2 * H, in, W, H, false, false, 0x80));
      compare(out, ref_up, 4 * W * H, 0x80, &worst, &ours, &alpha_ok);
      TEST_CHECK(alpha_ok);
      compare(up, ref_up, 4 * W * H, 0xff, &worst, &sws, &dummy);
      printf("test_vpost: scaled up, from the careful scaling: %.2f on average, swscale %.2f\n",
            ours, sws);
      TEST_CHECK(ours <= sws);

      TEST_CHECK(vpost_conv_exec(c, out, 20, 10, in, W, H, false, false, 0x80));
      compare(out, ref_down, 200, 0x80, &worst, &ours, &alpha_ok);
      TEST_CHECK(alpha_ok);
      compare(down, ref_down, 200, 0xff, &worst, &sws, &dummy);
      printf("test_vpost: scaled down, from the careful scaling: %.2f on average, swscale %.2f\n",
            ours, sws);
      TEST_CHECK(ours <= sws);
   }

   /* the same sizes again take no memory */
   allocations = 0;
   TEST_CHECK(vpost_conv_exec(c, out, 20, 10, in, W, H, false, false, 0x80));
   TEST_CHECK(vpost_conv_exec(c, out, W, H, in, W, H, false, false, 0x80));
   TEST_CHECK(allocations == 0);

   vpost_conv_free(c);
   free(in);
   free(same);
   free(up);
   free(down);
   free(full709);
   free(ref_up);
   free(ref_down);
   if (test_failures)
   {
      fprintf(stderr, "test_vpost: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_vpost: ok\n");
   return 0;
}
