/* The stb_image replacement, against stb_image itself (2.30, the version
 * RPCS3 pins): PNGs and JPEGs that stb_image_write writes and GIFs
 * written here, decoded by libretro-common's rpng, rjpeg and rgif as the
 * overlays, cellJpgDec and cellGifDec now decode them, and by stb_image
 * as they did before, compared pixel for pixel.
 *
 * Two differences are expected and checked as such: stb_image fills a
 * GIF's background from its BGR-ordered table, so red and blue come
 * out swapped there (rgif gives the colour the file names); and in a
 * 4:2:2 JPEG stb_image weighs the last chroma column the wrong way round
 * (stb_image_write never writes 4:2:2, so it does not come up here).
 *
 * Usage: test_images */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <formats/image.h>
#include <formats/rgif.h>
#include <formats/rjpeg.h>
#include <formats/rpng.h>

#include "stb_image.h"
#include "stb_image_write.h"

static int      fails;
static unsigned cases;

#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); fails++; } } while (0)

struct mem
{
   uint8_t *p;
   size_t   n, cap;
};

static void put(struct mem *m, const void *d, size_t n)
{
   if (m->n + n > m->cap)
   {
      m->cap = (m->n + n) * 2 + 64;
      m->p   = (uint8_t*)realloc(m->p, m->cap);
   }
   memcpy(m->p + m->n, d, n);
   m->n += n;
}

static void put_cb(void *ctx, void *data, int size)
{
   put((struct mem*)ctx, data, (size_t)size);
}

/* ---- the decoders, called as the core calls them ---- */

static uint8_t *ours_png(const struct mem *m, unsigned *w, unsigned *h)
{
   void   *px  = NULL;
   int     ret = IMAGE_PROCESS_ERROR;
   rpng_t *d   = rpng_alloc();
   if (d && rpng_set_buf_ptr(d, m->p, m->n) && rpng_start(d))
   {
      while (rpng_iterate_image(d)) { }
      if (rpng_is_valid(d))
         do
            ret = rpng_process_image(d, &px, m->n, w, h, true);
         while (ret == IMAGE_PROCESS_NEXT);
   }
   rpng_free(d);
   if (ret != IMAGE_PROCESS_END)
   {
      free(px);
      return NULL;
   }
   return (uint8_t*)px;
}

static uint8_t *ours_jpeg(const struct mem *m, unsigned *w, unsigned *h)
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

static uint8_t *ours_gif(const struct mem *m, unsigned *w, unsigned *h)
{
   void   *px = NULL;
   rgif_t *d  = rgif_new();
   int     ret = IMAGE_PROCESS_ERROR;
   if (d && rgif_set_buf_ptr(d, m->p, m->n))
      ret = rgif_process_image(d, &px, m->n, w, h, true);
   rgif_free(d);
   return ret == IMAGE_PROCESS_END ? (uint8_t*)px : NULL;
}

/* stb_image's answer and ours, pixel for pixel; @bg_swap allows stb's
 * swapped background */
static void compare(const struct mem *m, uint8_t *(*ours)(const struct mem*, unsigned*, unsigned*),
      int bg_swap, const char *what)
{
   int      sw = 0, sh = 0, sc = 0;
   unsigned w = 0, h = 0;
   uint8_t *ref = stbi_load_from_memory(m->p, (int)m->n, &sw, &sh, &sc, 4);
   uint8_t *got = ours(m, &w, &h);
   char     msg[200];

   cases++;
   if (!ref || !got)
   {
      sprintf(msg, "%s: stb_image %s, ours %s", what, ref ? "decodes" : "refuses",
            got ? "decodes" : "refuses");
      CHECK(!ref == !got, msg);
   }
   else
   {
      size_t i, n = (size_t)w * h;
      int    same = (int)w == sw && (int)h == sh;
      for (i = 0; same && i < n; i++)
      {
         const uint8_t *a = ref + i * 4, *b = got + i * 4;
         if (memcmp(a, b, 4) && !(bg_swap && b[3] == 255 && a[3] == 255
                  && a[0] == b[2] && a[1] == b[1] && a[2] == b[0]))
            same = 0;
      }
      sprintf(msg, "%s: the pixels", what);
      CHECK(same, msg);
   }
   stbi_image_free(ref);
   free(got);
}

static void picture(uint8_t *p, int w, int h, int comp, unsigned seed)
{
   int x, y, c;
   uint32_t r = seed * 2654435761u + 7;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
         for (c = 0; c < comp; c++)
         {
            r = r * 1103515245u + 12345u;
            /* smooth with some noise, and an alpha that varies */
            p[((size_t)y * w + x) * comp + c] = (uint8_t)(
                  (x * (3 + c) + y * (5 - c) + c * 60) * 2 + ((r >> 16) & 15));
         }
}

static void pngs_and_jpegs(void)
{
   static const int sizes[][2] = { { 1, 1 }, { 7, 5 }, { 16, 16 }, { 33, 17 }, { 250, 131 } };
   unsigned s;
   int      comp;
   char     what[96];

   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
      for (comp = 1; comp <= 4; comp++)
      {
         int      w = sizes[s][0], h = sizes[s][1], q;
         uint8_t *img = (uint8_t*)malloc((size_t)w * h * comp);
         struct mem m;

         picture(img, w, h, comp, s * 4 + (unsigned)comp);
         memset(&m, 0, sizeof(m));
         stbi_write_png_to_func(put_cb, &m, w, h, comp, img, w * comp);
         sprintf(what, "PNG %dx%d, %d channels", w, h, comp);
         compare(&m, ours_png, 0, what);
         free(m.p);

         /* JPEG: gray or RGB; 4:2:0 below quality 90, 4:4:4 from it */
         if (comp == 1 || comp == 3)
            for (q = 50; q <= 95; q += 45)
            {
               memset(&m, 0, sizeof(m));
               stbi_write_jpg_to_func(put_cb, &m, w, h, comp, img, q);
               sprintf(what, "JPEG %dx%d, %d channels, quality %d", w, h, comp, q);
               compare(&m, ours_jpeg, 0, what);
               free(m.p);
            }
         free(img);
      }
}

/* ---- GIFs, written here ---- */

struct lzw
{
   struct mem *o;
   uint8_t     block[255];
   unsigned    block_n, nbits, min, cs, next, avail;
   uint32_t    bits;
   int         first;
   int16_t    *dict;
};

static void lzw_byte(struct lzw *z, unsigned b)
{
   z->block[z->block_n++] = (uint8_t)b;
   if (z->block_n == 255)
   {
      uint8_t n = 255;
      put(z->o, &n, 1);
      put(z->o, z->block, 255);
      z->block_n = 0;
   }
}

static void lzw_bits(struct lzw *z, unsigned code)
{
   z->bits  |= (uint32_t)code << z->nbits;
   z->nbits += z->cs;
   while (z->nbits >= 8)
   {
      lzw_byte(z, z->bits & 0xff);
      z->bits  >>= 8;
      z->nbits  -= 8;
   }
}

static void lzw_reset(struct lzw *z)
{
   z->cs    = z->min + 1;
   z->next  = (1u << z->min) + 2;
   z->avail = z->next;
   z->first = 1;
   memset(z->dict, 0xff, sizeof(int16_t) * 4096 * 256);
}

static void lzw_code(struct lzw *z, unsigned code)
{
   lzw_bits(z, code);
   if (!z->first)
      z->avail++;
   z->first = 0;
   if ((z->avail & ((1u << z->cs) - 1)) == 0 && z->avail <= 0x0fff)
      z->cs++;
}

static void lzw_encode(struct mem *o, const uint8_t *idx, size_t n, unsigned min)
{
   struct lzw z;
   size_t     i;
   unsigned   w;
   uint8_t    b;

   memset(&z, 0, sizeof(z));
   z.o    = o;
   z.min  = min;
   z.dict = (int16_t*)malloc(sizeof(int16_t) * 4096 * 256);
   b      = (uint8_t)min;
   put(o, &b, 1);
   lzw_reset(&z);
   lzw_bits(&z, 1u << min);
   w = idx[0];
   for (i = 1; i < n; i++)
   {
      unsigned k = idx[i];
      int16_t  c = z.dict[w * 256 + k];
      if (c >= 0)
      {
         w = (unsigned)c;
         continue;
      }
      lzw_code(&z, w);
      if (z.next < 4096)
      {
         z.dict[w * 256 + k] = (int16_t)z.next++;
         if (z.next == 4096)
         {
            lzw_bits(&z, 1u << min);
            lzw_reset(&z);
         }
      }
      w = k;
   }
   lzw_code(&z, w);
   lzw_bits(&z, (1u << min) + 1);
   if (z.nbits)
      lzw_byte(&z, z.bits & 0xff);
   if (z.block_n)
   {
      b = (uint8_t)z.block_n;
      put(o, &b, 1);
      put(o, z.block, z.block_n);
   }
   b = 0;
   put(o, &b, 1);
   free(z.dict);
}

static void le16(struct mem *o, unsigned v)
{
   uint8_t b[2];
   b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
   put(o, b, 2);
}

/* @var: 0 the whole screen, 1 its own table and a transparent index,
 * 2 part of the screen and a background colour, 3 part of it and none */
static void gif(struct mem *o, unsigned w, unsigned h, unsigned bits,
      int interlace, int var, unsigned seed)
{
   unsigned n = 2u << (bits - 1), i, x0 = 0, y0 = 0, iw = w, ih = h;
   uint8_t  pal[768], b;
   uint8_t *idx, *order;
   size_t   k = 0;
   uint32_t r = seed * 2654435761u + 3;

   if (var >= 2)
   {
      x0 = w / 3; y0 = h / 4;
      iw = w - x0; ih = h - y0;
   }
   idx   = (uint8_t*)malloc((size_t)iw * ih + 1);
   order = (uint8_t*)malloc((size_t)iw * ih + 1);
   for (i = 0; i < iw * ih; i++)
   {
      r = r * 1103515245u + 12345u;
      idx[i] = (uint8_t)(i && (r >> 28) < 10 ? idx[i - 1]
            : (r >> 16) % (bits < 2 ? 4 : n));
   }
   for (i = 0; i < n * 3; i++)
      pal[i] = (uint8_t)(i * 37 + seed * 11 + 5);

   o->n = 0;
   put(o, "GIF89a", 6);
   le16(o, w);
   le16(o, h);
   b = (uint8_t)(var == 1 ? 0 : 0x80 | (bits - 1));
   put(o, &b, 1);
   b = (uint8_t)(var == 2 ? n - 1 : 0);
   put(o, &b, 1);
   b = 0;
   put(o, &b, 1);
   if (var != 1)
      put(o, pal, n * 3);
   if (var == 1 || var == 2)
   {
      put(o, "\x21\xf9\x04\x01\x00\x00", 6);
      b = (uint8_t)(var == 1 ? n / 2 : 0);
      put(o, &b, 1);
      b = 0;
      put(o, &b, 1);
   }
   b = 0x2c;
   put(o, &b, 1);
   le16(o, x0); le16(o, y0); le16(o, iw); le16(o, ih);
   b = (uint8_t)((var == 1 ? 0x80 | (bits - 1) : 0) | (interlace ? 0x40 : 0));
   put(o, &b, 1);
   if (var == 1)
      put(o, pal, n * 3);
   if (interlace)
   {
      static const unsigned start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
      unsigned p, y;
      for (p = 0; p < 4; p++)
         for (y = start[p]; y < ih; y += step[p])
         {
            memcpy(order + k, idx + (size_t)y * iw, iw);
            k += iw;
         }
   }
   else
   {
      memcpy(order, idx, (size_t)iw * ih);
      k = (size_t)iw * ih;
   }
   lzw_encode(o, order, k, bits < 2 ? 2 : bits);
   b = 0x3b;
   put(o, &b, 1);
   free(idx);
   free(order);
}

static void gifs(void)
{
   static const unsigned sizes[][2] = { { 1, 1 }, { 7, 5 }, { 33, 17 }, { 160, 120 } };
   unsigned s, bits, il;
   int      var;
   char     what[96];
   struct mem m;

   memset(&m, 0, sizeof(m));
   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
   for (bits = 1; bits <= 8; bits++)
   for (il = 0; il < 2; il++)
   for (var = 0; var < 4; var++)
   {
      size_t full;
      gif(&m, sizes[s][0], sizes[s][1], bits, (int)il, var, s * 64 + bits * 8 + il * 4 + (unsigned)var);
      sprintf(what, "GIF %ux%u, %u colours, %s, case %d", sizes[s][0],
            sizes[s][1], 2u << (bits - 1), il ? "interlaced" : "plain", var);
      compare(&m, ours_gif, var == 2, what);
      /* cut short: decoded as far as it goes, by both */
      full = m.n;
      m.n  = full * 2 / 3;
      compare(&m, ours_gif, var == 2, what);
      m.n  = full;
   }
   free(m.p);
}

int main(void)
{
   pngs_and_jpegs();
   gifs();
   if (fails)
   {
      fprintf(stderr, "test_images: %d check(s) failed over %u cases\n", fails, cases);
      return 1;
   }
   printf("test_images: ok, %u cases\n", cases);
   return 0;
}
