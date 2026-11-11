/* cellJpgDec's decode (rpcs3/Emu/Cell/Modules/cellJpgDec_rows.c): JPEGs
 * that stb_image_write writes, gray and RGB, 4:2:0 and 4:4:4, decoded
 * with jdr_decode into a game's buffer in each colour space the module
 * writes (RGBA, RGB, ARGB, and one it writes nothing for), top to bottom
 * and bottom to top, at the line pitches games use (packed, wider than a
 * row, and narrower: cut inside a pixel), into aligned and unaligned
 * buffers - and compared byte for byte with what the module wrote
 * before, a picture decoded whole by rjpeg_process_image and laid out
 * row by row (the old code, below). No byte outside the rows is
 * written.
 *
 * And the memory: decoding them all again with the same decoder takes
 * no allocation, where the old way took several for each image; the
 * allocations are counted by wrapping malloc and calloc at link time.
 *
 * Usage: test_jpg_rows */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <formats/image.h>
#include <formats/rjpeg.h>

#include "stb_image_write.h"

#include "../../Emu/Cell/Modules/cellJpgDec_rows.h"

static int      fails;
static unsigned cases;

#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); fails++; } } while (0)

/* Allocations, counted: linked with --wrap=malloc,--wrap=calloc */
static unsigned long allocs;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__wrap_malloc(size_t n) { allocs++; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t m) { allocs++; return __real_calloc(n, m); }

struct mem
{
   uint8_t *p;
   size_t   n, cap;
};

static void put_cb(void *ctx, void *data, int size)
{
   struct mem *m = (struct mem*)ctx;
   if (m->n + (size_t)size > m->cap)
   {
      m->cap = (m->n + (size_t)size) * 2 + 64;
      m->p   = (uint8_t*)realloc(m->p, m->cap);
   }
   memcpy(m->p + m->n, data, (size_t)size);
   m->n += (size_t)size;
}

/* ---- the old code: the picture decoded whole, then laid out ---- */

static uint8_t *old_image(const struct mem *m, unsigned *w, unsigned *h)
{
   void    *px  = NULL;
   int      ret = IMAGE_PROCESS_ERROR;
   rjpeg_t *d   = rjpeg_alloc();
   if (d && rjpeg_set_buf_ptr(d, m->p, m->n))
   {
      rjpeg_set_out_rgba(d, true);
      do
         ret = rjpeg_process_image(d, &px, m->n, w, h, true);
      while (ret == IMAGE_PROCESS_NEXT);
   }
   rjpeg_free(d);
   if (ret != IMAGE_PROCESS_END)
   {
      free(px);
      return NULL;
   }
   return (uint8_t*)px;
}

static void old_pixel(int space, uint8_t *d, const uint8_t *s)
{
   if (space == JDR_CS_ARGB)
   {
      d[0] = s[3];
      d[1] = s[0];
      d[2] = s[1];
      d[3] = s[2];
   }
   else
      memcpy(d, s, space == JDR_CS_RGB ? 3 : 4);
}

/* cellJpgDecDecodeData's put_rows, as it was */
static void old_layout(const uint8_t *image, unsigned width, unsigned height,
      uint8_t *data, size_t bytes_per_line, int flip, int space)
{
   size_t n = jdr_components(space), row_bytes, stride, line, line_pixels, x;
   unsigned y;
   int padded;
   if (!n)
      return;
   row_bytes   = (size_t)width * n;
   padded      = bytes_per_line > row_bytes || flip;
   stride      = padded ? bytes_per_line : row_bytes;
   line        = padded ? (bytes_per_line < row_bytes ? bytes_per_line : row_bytes) : row_bytes;
   line_pixels = line / n;
   for (y = 0; y < height; y++)
   {
      const uint8_t *src = image + (size_t)width * 4 * (flip ? height - y - 1 : y);
      uint8_t       *dst = data + stride * y;
      size_t         rest;
      for (x = 0; x < line_pixels; x++)
         old_pixel(space, dst + x * n, src + x * 4);
      if ((rest = line - line_pixels * n))
      {
         uint8_t px[4];
         old_pixel(space, px, src + line_pixels * 4);
         memcpy(dst + line_pixels * n, px, rest);
      }
   }
}

/* ---- the cases ---- */

static void picture(uint8_t *p, int w, int h, int comp, unsigned seed)
{
   int x, y, c;
   uint32_t r = seed * 2654435761u + 7;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
         for (c = 0; c < comp; c++)
         {
            r = r * 1103515245u + 12345u;
            p[((size_t)y * w + x) * comp + c] = (uint8_t)(
                  (x * (3 + c) + y * (5 - c) + c * 60) * 2 + ((r >> 16) & 15));
         }
}

#define N_FILES 16

static struct mem files[N_FILES];
static unsigned   n_files;

static void make_files(void)
{
   static const int sizes[][2] = { { 1, 1 }, { 7, 5 }, { 33, 17 }, { 250, 131 } };
   unsigned s;
   int comp, q;
   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
      for (comp = 1; comp <= 3; comp += 2)
         for (q = 50; q <= 95; q += 45)
         {
            int      w   = sizes[s][0], h = sizes[s][1];
            uint8_t *img = (uint8_t*)malloc((size_t)w * h * comp);
            picture(img, w, h, comp, s * 4 + (unsigned)comp);
            memset(&files[n_files], 0, sizeof(files[n_files]));
            stbi_write_jpg_to_func(put_cb, &files[n_files], w, h, comp, img, q);
            n_files++;
            free(img);
         }
}

#define CANARY 0xA5

/* One file decoded both ways in one layout */
static void layout_case(rjpeg_t *dec, const struct mem *m, const uint8_t *image,
      unsigned w, unsigned h, int space, int flip, int pitch, unsigned misalign)
{
   size_t n = jdr_components(space) ? jdr_components(space) : 4;
   size_t row_bytes = (size_t)w * n;
   size_t bpl = pitch == 0 ? 0 : pitch == 1 ? row_bytes + 7
      : (row_bytes > 5 ? row_bytes - 5 : 1);
   size_t size = (bpl > row_bytes ? bpl : row_bytes) * h + 64;
   uint8_t *want = (uint8_t*)malloc(size + 8);
   uint8_t *got  = (uint8_t*)malloc(size + 8);
   unsigned gw = 0, gh = 0;
   char what[160];
   int ok;

   memset(want, CANARY, size + 8);
   memset(got, CANARY, size + 8);
   old_layout(image, w, h, want + misalign, bpl, flip, space);
   ok = jdr_decode(dec, m->p, m->n, got + misalign, bpl, flip != 0, space, &gw, &gh);

   cases++;
   sprintf(what, "%ux%u, space %d, %s, pitch %u (row %u), %s buffer", w, h, space,
         flip ? "bottom to top" : "top to bottom", (unsigned)bpl, (unsigned)row_bytes,
         misalign ? "unaligned" : "aligned");
   CHECK(ok && gw == w && gh == h, what);
   CHECK(!memcmp(want, got, size + 8), what);

   free(want);
   free(got);
}

static void layouts(void)
{
   static const int spaces[] = { JDR_CS_RGBA, JDR_CS_RGB, JDR_CS_ARGB, 1 /* GRAYSCALE */ };
   rjpeg_t *dec = rjpeg_alloc();
   unsigned f;
   for (f = 0; f < n_files; f++)
   {
      unsigned w = 0, h = 0, sp, flip, pitch, mis;
      uint8_t *image = old_image(&files[f], &w, &h);
      CHECK(image != NULL, "the file decodes the old way");
      if (!image)
         continue;
      for (sp = 0; sp < sizeof(spaces) / sizeof(spaces[0]); sp++)
         for (flip = 0; flip < 2; flip++)
            for (pitch = 0; pitch < 3; pitch++)
               for (mis = 0; mis < 2; mis++)
                  layout_case(dec, &files[f], image, w, h, spaces[sp], (int)flip, (int)pitch, mis);
      free(image);
   }
   rjpeg_free(dec);
}

/* The decodes after the first round take nothing; the old way's count */
static void memory(void)
{
   static uint8_t out[250 * 131 * 4 + 64];
   rjpeg_t *dec = rjpeg_alloc();
   unsigned long before, old_allocs;
   unsigned round, f, w, h;
   char what[120];

   for (round = 0; round < 3; round++)
   {
      before = allocs;
      for (f = 0; f < n_files; f++)
      {
         CHECK(jdr_decode(dec, files[f].p, files[f].n, out, 0, round & 1,
               round == 2 ? JDR_CS_RGB : JDR_CS_RGBA, &w, &h), "decodes");
      }
      if (round)
      {
         sprintf(what, "round %u over %u files takes no memory (took %lu allocations)",
               round + 1, n_files, allocs - before);
         CHECK(allocs == before, what);
      }
   }
   rjpeg_free(dec);

   before = allocs;
   for (f = 0; f < n_files; f++)
   {
      uint8_t *image = old_image(&files[f], &w, &h);
      free(image);
   }
   old_allocs = allocs - before;
   printf("test_jpg_rows: the old way took %lu allocations for %u files, now none\n",
         old_allocs, n_files);
   CHECK(old_allocs >= n_files, "the allocation count sees the old way's");
}

int main(void)
{
   unsigned f;
   make_files();
   layouts();
   memory();
   for (f = 0; f < n_files; f++)
      free(files[f].p);
   if (fails)
   {
      fprintf(stderr, "test_jpg_rows: %d check(s) failed over %u cases\n", fails, cases);
      return 1;
   }
   printf("test_jpg_rows: ok, %u cases\n", cases);
   return 0;
}
