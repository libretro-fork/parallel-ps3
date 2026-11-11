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
 * The header (jdr_header, cellJpgDecReadHeader's): every file's size as
 * the decoder finds it, also with an Exif segment longer than 255 bytes
 * before the JFIF one, fill bytes before a marker, and a progressive
 * frame; files cut anywhere before the frame header's end refused
 * without a byte read past them; and how many the module's old walk
 * refused (it read the segment lengths wrong).
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

/* cellJpgDecReadHeader's walk, as it was: 0 when it refused the file */
static int old_header(const uint8_t *b, size_t n, unsigned *w, unsigned *h)
{
   size_t i = 4, bl;
   if (n < 10 || b[0] != 0xFF || b[1] != 0xD8 || b[2] != 0xFF || b[3] != 0xE0
         || memcmp(b + 6, "JFIF", 4))
      return 0;
   bl = b[4] * 0xFF + b[5];
   for (;;)
   {
      unsigned next;
      i += bl;
      if (i + 8 >= n || b[i] != 0xFF)
         return 0;
      next = b[i + 1];
      if (next == 0xC0)
         break;
      i += 2;
      bl = b[i] * 0xFF + next;
   }
   *w = b[i + 7] * 0x100u + b[i + 8];
   *h = b[i + 5] * 0x100u + b[i + 6];
   return 1;
}

/* The file with @ins put in at @at */
static uint8_t *splice(const struct mem *m, size_t at, const uint8_t *ins, size_t n, size_t *len)
{
   uint8_t *p = (uint8_t*)malloc(m->n + n);
   memcpy(p, m->p, at);
   memcpy(p + at, ins, n);
   memcpy(p + at + n, m->p + at, m->n - at);
   *len = m->n + n;
   return p;
}

static void header_case(const uint8_t *p, size_t n, unsigned w, unsigned h,
      unsigned comps, const char *what)
{
   unsigned hw = 0, hh = 0, hc = 0;
   size_t   cut, sof_end = 0, i;
   char     msg[160];

   cases++;
   sprintf(msg, "%s: the header gives %ux%u, %u components", what, w, h, comps);
   CHECK(jdr_header(p, n, &hw, &hh, &hc) && hw == w && hh == h && hc == comps, msg);

   /* The end of the frame header: the first SOFn segment's end */
   for (i = 2; i + 3 < n; i++)
      if (p[i] == 0xFF && (p[i + 1] == 0xC0 || p[i + 1] == 0xC2))
      {
         sof_end = i + 2 + ((size_t)p[i + 2] << 8 | p[i + 3]);
         break;
      }
   /* Cut before it: refused, read within (each cut its own allocation) */
   for (cut = 0; cut < sof_end; cut++)
   {
      uint8_t *c = (uint8_t*)malloc(cut ? cut : 1);
      memcpy(c, p, cut);
      if (jdr_header(c, cut, &hw, &hh, &hc))
      {
         sprintf(msg, "%s: cut at %u of %u refused", what, (unsigned)cut, (unsigned)sof_end);
         CHECK(0, msg);
      }
      free(c);
   }
}

static void headers(void)
{
   static const uint8_t not_jpeg[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
   uint8_t  exif[4 + 300];
   unsigned f, old_refused = 0, hw, hh, hc;

   /* An Exif APP1 of 300 bytes: a length whose high byte is not 0 */
   memset(exif, 0x45, sizeof(exif));
   exif[0] = 0xFF;
   exif[1] = 0xE1;
   exif[2] = (uint8_t)((sizeof(exif) - 2) >> 8);
   exif[3] = (uint8_t)(sizeof(exif) - 2);

   for (f = 0; f < n_files; f++)
   {
      unsigned w = 0, h = 0, ow, oh, comps;
      uint8_t *image = old_image(&files[f], &w, &h), *p;
      size_t   n, i;
      char     what[96];
      free(image);
      comps = 0;
      /* stb_image_write: gray files have one component, RGB three */
      for (i = 2; i + 9 < files[f].n; i++)
         if (files[f].p[i] == 0xFF && files[f].p[i + 1] == 0xC0)
         {
            comps = files[f].p[i + 9];
            break;
         }
      CHECK(comps == 1 || comps == 3, "a frame header in the file");

      sprintf(what, "file %u (%ux%u)", f, w, h);
      header_case(files[f].p, files[f].n, w, h, comps, what);
      if (!old_header(files[f].p, files[f].n, &ow, &oh) || ow != w || oh != h)
         old_refused++;

      /* Exif before JFIF */
      p = splice(&files[f], 2, exif, sizeof(exif), &n);
      sprintf(what, "file %u with Exif first", f);
      header_case(p, n, w, h, comps, what);
      free(p);

      /* Fill bytes before the marker after JFIF's APP0 */
      {
         static const uint8_t fill[] = { 0xFF, 0xFF, 0xFF };
         size_t at = 4 + ((size_t)files[f].p[4] << 8 | files[f].p[5]);
         p = splice(&files[f], at, fill, sizeof(fill), &n);
         sprintf(what, "file %u with fill bytes", f);
         header_case(p, n, w, h, comps, what);
         free(p);
      }

      /* Progressive: the frame marker SOF2 */
      p = (uint8_t*)malloc(files[f].n);
      memcpy(p, files[f].p, files[f].n);
      for (i = 2; i + 1 < files[f].n; i++)
         if (p[i] == 0xFF && p[i + 1] == 0xC0)
         {
            p[i + 1] = 0xC2;
            break;
         }
      sprintf(what, "file %u as progressive", f);
      header_case(p, files[f].n, w, h, comps, what);
      free(p);
   }

   CHECK(!jdr_header(not_jpeg, sizeof(not_jpeg), &hw, &hh, &hc), "a PNG is refused");
   {
      /* A frame header too short for its fields, at the file's end */
      static const uint8_t short_sof[] = { 0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x05, 0x08, 0x00, 0x10 };
      uint8_t *c = (uint8_t*)malloc(sizeof(short_sof));
      memcpy(c, short_sof, sizeof(short_sof));
      CHECK(!jdr_header(c, sizeof(short_sof), &hw, &hh, &hc), "a short frame header is refused");
      free(c);
   }
   printf("test_jpg_rows: the old header walk refused %u of %u files, now none\n",
         old_refused, n_files);
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
   headers();
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
