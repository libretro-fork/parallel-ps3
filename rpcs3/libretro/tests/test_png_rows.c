/* The libpng replacement in cellPngDec, against libpng itself: every PNG
 * colour type and bit depth, with and without tRNS, plain and Adam7,
 * written by libpng, decoded for every output the game can ask for -
 * once by libpng with the calls cellPngDecSetParameter made, once by
 * rpng_rows with cellPngDec_rows.c's conversions - and compared byte for
 * byte: the row size, the pixels read row by row (cellPngDecDecodeData),
 * and the progressive reader's row callbacks, row numbers and blocky
 * display (cellPngDecExtDecodeData with a display callback). Then RGB
 * turned gray in files with colour chunks (gAMA, sRGB, cHRM, mDCV, cICP,
 * sBIT, in the orders that decide which counts), where libpng weighs the
 * channels by the chromaticities and mixes in linear light.
 *
 * libpng is the oracle here only; the core does not link it. It is
 * libpng 1.6.59, the version RPCS3 built with, from source: the Makefile
 * fetches it (the colour rules changed in 1.6.4x and 1.6.5x).
 *
 * Usage: test_png_rows */

#include <setjmp.h>
#include <png.h>

#include <encodings/crc32.h>
#include <formats/rpng_rows.h>
#include "../../Emu/Cell/Modules/cellPngDec_rows.h"

#include "test_common.h"

struct mem
{
   uint8_t *p;
   size_t   n, cap;
};

static void mem_write(png_structp png, png_bytep d, png_size_t n)
{
   struct mem *m = (struct mem*)png_get_io_ptr(png);
   if (m->n + n > m->cap)
   {
      m->cap = (m->n + n) * 2;
      m->p   = (uint8_t*)realloc(m->p, m->cap);
   }
   memcpy(m->p + m->n, d, n);
   m->n += n;
}

static void mem_flush(png_structp png) { (void)png; }

struct reader
{
   const uint8_t *p;
   size_t         n, at;
};

static void mem_read(png_structp png, png_bytep d, png_size_t n)
{
   struct reader *r = (struct reader*)png_get_io_ptr(png);
   if (r->at + n > r->n)
      png_error(png, "read past the end");
   memcpy(d, r->p + r->at, n);
   r->at += n;
}

static void quiet(png_structp png, png_const_charp msg) { (void)png; (void)msg; }

static void fail_jump(png_structp png, png_const_charp msg)
{
   (void)msg;
   longjmp(png_jmpbuf(png), 1);
}

static int make_png(struct mem *m, uint32_t w, uint32_t h, int depth, int ct,
      int interlace, int trns, uint32_t seed)
{
   png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, fail_jump, quiet);
   png_infop   inf = png_create_info_struct(png);
   int         ch  = ct == 2 ? 3 : ct == 4 ? 2 : ct == 6 ? 4 : 1;
   size_t      rb  = ((size_t)w * depth * ch + 7) / 8;
   uint8_t    *img = (uint8_t*)malloc(rb * h + 1);
   png_bytep  *rows = (png_bytep*)malloc(sizeof(png_bytep) * h);
   uint32_t    i, x = seed;

   m->n = 0;
   for (i = 0; i < rb * h; i++)
   {
      x = x * 1103515245u + 12345u;
      img[i] = (uint8_t)(x >> 16);
   }
   /* make some pixels equal to the tRNS key, and some gray in RGB */
   if (rb * h > 8)
      for (i = 0; i < rb * h / 3; i += 7)
         img[i] = 0;
   /* and some gray pixels of any value in RGB, every fifth */
   if ((ct == 2 || ct == 6) && depth >= 8)
   {
      size_t bps = (size_t)depth / 8, px = bps * ch, k;
      for (k = 0; k + px <= rb * h; k += px * 5)
      {
         memcpy(img + k + bps, img + k, bps);
         memcpy(img + k + bps * 2, img + k, bps);
      }
   }
   for (i = 0; i < h; i++)
      rows[i] = img + i * rb;
   if (setjmp(png_jmpbuf(png)))
   {
      png_destroy_write_struct(&png, &inf);
      free(img);
      free(rows);
      return 0;
   }
   png_set_write_fn(png, m, mem_write, mem_flush);
   png_set_IHDR(png, inf, w, h, depth, ct, interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
   if (ct == 3)
   {
      png_color pal[256];
      int n = 1 << depth;
      for (i = 0; i < 256; i++)
      {
         pal[i].red = (png_byte)(i * 3); pal[i].green = (png_byte)(255 - i); pal[i].blue = (png_byte)(i * 7);
      }
      png_set_PLTE(png, inf, pal, n);
      if (trns)
      {
         png_byte a[3] = { 0, 0x40, 0x80 };
         png_set_tRNS(png, inf, a, n > 4 ? 3 : 1, NULL);
      }
   }
   else if (trns && (ct == 0 || ct == 2))
   {
      png_color_16 key;
      memset(&key, 0, sizeof(key));
      png_set_tRNS(png, inf, NULL, 0, &key);
   }
   png_write_info(png, inf);
   png_write_image(png, rows);
   png_write_end(png, inf);
   png_destroy_write_struct(&png, &inf);
   free(img);
   free(rows);
   return 1;
}

/* cellPngDecSetParameter's libpng calls, as the module made them */
static void libpng_params(png_structp png, png_infop inf, int out_space,
      int alpha_select, unsigned alpha, unsigned out_depth)
{
   int in_cs = pdr_color_space(png_get_color_type(png, inf));
   int depth = png_get_bit_depth(png, inf);

   if (depth == 16 && out_depth == 8)
      png_set_strip_16(png);
   if (in_cs != out_space)
   {
      int in_alpha  = in_cs == PDR_CS_RGBA || in_cs == PDR_CS_ARGB || in_cs == PDR_CS_GRAYSCALE_ALPHA;
      int out_alpha = out_space == PDR_CS_RGBA || out_space == PDR_CS_ARGB || out_space == PDR_CS_GRAYSCALE_ALPHA;
      if (out_alpha && !in_alpha)
      {
         if (alpha_select == PDR_FIX_ALPHA)
            png_set_add_alpha(png, alpha, out_space == PDR_CS_ARGB ? PNG_FILLER_BEFORE : PNG_FILLER_AFTER);
         else if (png_get_valid(png, inf, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(png);
         else
            png_set_add_alpha(png, 0xff, out_space == PDR_CS_ARGB ? PNG_FILLER_BEFORE : PNG_FILLER_AFTER);
      }
      else if (in_alpha && !out_alpha)
         png_set_strip_alpha(png);
      else if (out_space == PDR_CS_ARGB && in_cs == PDR_CS_RGBA)
         png_set_swap_alpha(png);

      if (out_space == PDR_CS_ARGB || out_space == PDR_CS_RGBA || out_space == PDR_CS_RGB)
      {
         if (in_cs == PDR_CS_PALETTE)
            png_set_palette_to_rgb(png);
         if ((in_cs == PDR_CS_GRAYSCALE || in_cs == PDR_CS_GRAYSCALE_ALPHA) && depth < 8)
            png_set_expand_gray_1_2_4_to_8(png);
      }
      else if (in_cs == PDR_CS_ARGB || in_cs == PDR_CS_RGBA || in_cs == PDR_CS_RGB)
         png_set_rgb_to_gray(png, PNG_ERROR_ACTION_NONE, PNG_RGB_TO_GRAY_DEFAULT, PNG_RGB_TO_GRAY_DEFAULT);
   }
}

/* libpng, row by row: returns the row size, 0 if it failed */
static size_t libpng_rows(const struct mem *m, int out_space, int alpha_select,
      unsigned alpha, unsigned out_depth, uint8_t *img, uint32_t *h_out)
{
   struct reader r;
   png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, fail_jump, quiet);
   png_infop   inf = png_create_info_struct(png);
   size_t      rb  = 0;
   uint32_t    h, i;
   int         passes, j;

   r.p = m->p; r.n = m->n; r.at = 0;
   if (setjmp(png_jmpbuf(png)))
   {
      png_destroy_read_struct(&png, &inf, NULL);
      return 0;
   }
   png_set_read_fn(png, &r, mem_read);
   png_read_info(png, inf);
   libpng_params(png, inf, out_space, alpha_select, alpha, out_depth);
   passes = png_set_interlace_handling(png);
   png_read_update_info(png, inf);
   rb = png_get_rowbytes(png, inf);
   h  = png_get_image_height(png, inf);
   memset(img, 0, rb * h);
   for (j = 0; j < passes; j++)
      for (i = 0; i < h; i++)
         png_read_row(png, img + i * rb, NULL);
   png_read_end(png, inf);
   png_destroy_read_struct(&png, &inf, NULL);
   *h_out = h;
   return rb;
}

/* the progressive reader's callbacks, logged */
struct prog
{
   png_structp png;
   int         out_space, alpha_select, out_depth;
   unsigned    alpha;
   uint32_t    height;
   uint8_t    *img;
   size_t      rb;
   uint32_t    log[4096][3];
   unsigned    n;
   struct pdr *p;
};

static void prog_row(png_structp png, png_bytep row, png_uint_32 num, int pass)
{
   struct prog *g = (struct prog*)png_get_progressive_ptr(png);
   if (g->n < 4096)
   {
      g->log[g->n][0] = num; g->log[g->n][1] = (uint32_t)pass; g->log[g->n][2] = row != NULL;
      g->n++;
   }
   png_progressive_combine_row(png, g->img + num * g->rb, row);
}

/* the header and the chunks before the image are in: the parameters,
 * as cellPngDecSetParameter sets them before decoding */
static void prog_info(png_structp png, png_infop inf)
{
   struct prog *g = (struct prog*)png_get_progressive_ptr(png);
   libpng_params(png, inf, g->out_space, g->alpha_select, g->alpha, (unsigned)g->out_depth);
   png_set_interlace_handling(png);
   png_read_update_info(png, inf);
   g->rb     = png_get_rowbytes(png, inf);
   g->height = png_get_image_height(png, inf);
   memset(g->img, 0, g->rb * g->height);
}

static size_t libpng_progressive(const struct mem *m, int out_space,
      int alpha_select, unsigned alpha, unsigned out_depth, struct prog *g)
{
   png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, fail_jump, quiet);
   png_infop   inf = png_create_info_struct(png);

   g->n = 0;
   g->rb = 0;
   g->out_space = out_space; g->alpha_select = alpha_select;
   g->alpha = alpha; g->out_depth = (int)out_depth;
   if (setjmp(png_jmpbuf(png)))
   {
      png_destroy_read_struct(&png, &inf, NULL);
      return 0;
   }
   png_set_progressive_read_fn(png, g, prog_info, prog_row, NULL);
   png_process_data(png, inf, m->p, m->n);
   png_destroy_read_struct(&png, &inf, NULL);
   return g->rb;
}

static void ours_prog_row(void *ud, const uint8_t *row, uint32_t num, unsigned pass)
{
   struct prog *g = (struct prog*)ud;
   if (g->n < 4096)
   {
      g->log[g->n][0] = num; g->log[g->n][1] = pass; g->log[g->n][2] = row != NULL;
      g->n++;
   }
   if (row)
      pdr_combine(g->p, g->img + num * g->rb, row,
            g->p->info->interlace ? (int)pass : -1, 1);
}

/* ours: both readers at once; returns the row size, 0 if not supported */
static size_t ours(const struct mem *m, int out_space, int alpha_select,
      unsigned alpha, unsigned out_depth, uint8_t *img, struct prog *g)
{
   rpng_rows_t *z = rpng_rows_new();
   struct pdr   p;
   struct pdr_push push;
   size_t at = 0, rb = 0;
   int    ev;

   memset(&p, 0, sizeof(p));
   g->n = 0;
   g->p = &p;
   for (;;)
   {
      struct rpng_rows_row row;
      size_t used = 0;
      ev = rpng_rows_step(z, m->p + at, m->n - at, &used, &row);
      at += used;
      if (ev == RPNG_ROWS_HEADER)
      {
         const struct rpng_rows_info *info = rpng_rows_info(z);
         unsigned filler;
         int      unsupported;
         unsigned flags = pdr_flags_for(info, out_space, alpha_select, alpha,
               out_depth, &filler, &unsupported);
         if (!pdr_setup(&p, info, flags, filler) || !p.valid)
            break;
         rb = p.out_rowbytes;
         g->rb = rb;
         memset(img, 0, rb * info->height);
         memset(g->img, 0, rb * info->height);
         pdr_push_begin(&push);
      }
      else if (ev == RPNG_ROWS_ROW)
      {
         const uint8_t *c = pdr_convert(&p, row.data, row.width);
         pdr_scatter(&p, img + (size_t)row.y * rb, c, &row);
         pdr_push_row(&p, &push, c, row.width, ours_prog_row, g);
      }
      else
         break;
   }
   rpng_rows_free(z);
   pdr_free(&p);
   return ev == RPNG_ROWS_END ? rb : 0;
}

static struct prog lp, op;
static uint8_t    *img_a, *img_b;
static unsigned    cases, unsupported;

static const int all_spaces[] = { PDR_CS_GRAYSCALE, PDR_CS_RGB, PDR_CS_PALETTE,
   PDR_CS_GRAYSCALE_ALPHA, PDR_CS_RGBA, PDR_CS_ARGB };
static const int gray_spaces[] = { PDR_CS_GRAYSCALE, PDR_CS_GRAYSCALE_ALPHA };

/* The outputs in @spaces, with either alpha and depth, libpng against ours */
static void compare(const struct mem *m, const char *name, const int *spaces,
      unsigned n_spaces)
{
   unsigned sp, as, od;

   for (sp = 0; sp < n_spaces; sp++)
   for (as = 0; as < 2; as++)
   for (od = 8; od <= 16; od += 8)
   {
      uint32_t h = 0;
      size_t   rb_lib = libpng_rows(m, spaces[sp], (int)as, 0x80, od, img_a, &h);
      size_t   rb_lp  = libpng_progressive(m, spaces[sp], (int)as, 0x80, od, &lp);
      size_t   rb_us  = ours(m, spaces[sp], (int)as, 0x80, od, img_b, &op);
      char     what[200];

      sprintf(what, "%s -> space %d alpha %u depth %u", name, spaces[sp], as, od);
      cases++;
      if (!rb_lib)
      {
         /* libpng failed: so must we */
         if (rb_us)
            fprintf(stderr, "%s: libpng failed, ours did not\n", what);
         TEST_CHECK(!rb_us);
         unsupported++;
         continue;
      }
      if (rb_us != rb_lib || memcmp(img_a, img_b, rb_lib * h))
         fprintf(stderr, "%s: rows differ (row size %u vs %u)\n", what,
               (unsigned)rb_lib, (unsigned)rb_us);
      TEST_CHECK(rb_us == rb_lib && !memcmp(img_a, img_b, rb_lib * h));

      TEST_CHECK(rb_lp == rb_lib);
      if (lp.n != op.n || memcmp(lp.log, op.log, sizeof(lp.log[0]) * lp.n)
            || memcmp(lp.img, op.img, rb_lib * h))
         fprintf(stderr, "%s: progressive differs (%u vs %u callbacks)\n",
               what, lp.n, op.n);
      TEST_CHECK(lp.n == op.n && !memcmp(lp.log, op.log, sizeof(lp.log[0]) * lp.n));
      TEST_CHECK(!memcmp(lp.img, op.img, rb_lib * h));
   }
}

/* ---- colour chunks, written by hand after IHDR so their order is ours */

struct chunks
{
   uint8_t d[512];
   size_t  n;
};

static void put_chunk(struct chunks *c, const char *type, const uint8_t *d,
      uint32_t n)
{
   uint8_t  *o = c->d + c->n;
   uint32_t  crc;
   o[0] = (uint8_t)(n >> 24); o[1] = (uint8_t)(n >> 16);
   o[2] = (uint8_t)(n >> 8);  o[3] = (uint8_t)n;
   memcpy(o + 4, type, 4);
   memcpy(o + 8, d, n);
   crc = encoding_crc32(0, o + 4, 4 + n);
   o[8 + n] = (uint8_t)(crc >> 24); o[9 + n] = (uint8_t)(crc >> 16);
   o[10 + n] = (uint8_t)(crc >> 8); o[11 + n] = (uint8_t)crc;
   c->n += 12 + n;
}

static void put_be32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void gama(struct chunks *c, uint32_t g)
{
   uint8_t b[4];
   put_be32(b, g);
   put_chunk(c, "gAMA", b, 4);
}

/* white, red, green, blue x and y, times 100000 */
static void chrm(struct chunks *c, const uint32_t v[8])
{
   uint8_t  b[32];
   unsigned i;
   for (i = 0; i < 8; i++)
      put_be32(b + i * 4, v[i]);
   put_chunk(c, "cHRM", b, 32);
}

/* red, green, blue and white x and y, times 50000 */
static void mdcv(struct chunks *c, const uint16_t v[8])
{
   uint8_t  b[24];
   unsigned i;
   for (i = 0; i < 8; i++)
   {
      b[i * 2]     = (uint8_t)(v[i] >> 8);
      b[i * 2 + 1] = (uint8_t)v[i];
   }
   put_be32(b + 16, 10000000);
   put_be32(b + 20, 50);
   put_chunk(c, "mDCV", b, 24);
}

static const uint32_t chrm_srgb[8] = { 31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000 };
static const uint32_t chrm_adobe[8] = { 31270, 32900, 64000, 33000, 21000, 71000, 15000, 6000 };
static const uint32_t chrm_prophoto[8] = { 34570, 35850, 73470, 26530, 15960, 84040, 3660, 100 };
/* white outside the primaries: no colour space */
static const uint32_t chrm_bad[8] = { 90000, 9000, 64000, 33000, 30000, 60000, 15000, 6000 };
/* a white too near y 0 to use */
static const uint32_t chrm_tiny[8] = { 31270, 4, 64000, 33000, 30000, 60000, 15000, 6000 };
static const uint16_t mdcv_p3[8] = { 34000, 16000, 13250, 34500, 7500, 3000, 15635, 16450 };

/* The colour chunk sets; @depth picks sBIT values that fit */
static unsigned colour_set(unsigned k, struct chunks *c, int depth, const char **name)
{
   c->n = 0;
   switch (k)
   {
      case 0:  *name = "gAMA 1/2.2"; gama(c, 45455); break;
      case 1:  *name = "gAMA 2.2"; gama(c, 220000); break;
      case 2:  *name = "gAMA 1.0"; gama(c, 100000); break;
      case 3:  *name = "gAMA 0.97, not significant"; gama(c, 97000); break;
      case 4:  *name = "gAMA 0.0001"; gama(c, 10); break;
      case 5:  *name = "gAMA 0.00001, its inverse overflows"; gama(c, 1); break;
      case 6:  *name = "gAMA 0"; gama(c, 0); break;
      case 7:  *name = "sRGB"; put_chunk(c, "sRGB", (const uint8_t*)"\0", 1); break;
      case 8:  *name = "cHRM sRGB"; chrm(c, chrm_srgb); break;
      case 9:  *name = "cHRM Adobe, gAMA"; chrm(c, chrm_adobe); gama(c, 45455); break;
      case 10: *name = "cHRM ProPhoto, gAMA 1.8"; gama(c, 55556); chrm(c, chrm_prophoto); break;
      case 11: *name = "cHRM unusable, gAMA"; chrm(c, chrm_bad); gama(c, 45455); break;
      case 12: *name = "cHRM white y 0, gAMA"; chrm(c, chrm_tiny); gama(c, 45455); break;
      case 13: *name = "cHRM and sRGB: sRGB's"; chrm(c, chrm_prophoto);
               put_chunk(c, "sRGB", (const uint8_t*)"\1", 1); break;
      case 14: *name = "mDCV over sRGB"; put_chunk(c, "sRGB", (const uint8_t*)"\0", 1);
               mdcv(c, mdcv_p3); break;
      case 15: *name = "mDCV over cHRM"; chrm(c, chrm_adobe); mdcv(c, mdcv_p3);
               gama(c, 45455); break;
      case 16: *name = "sRGB then gAMA"; put_chunk(c, "sRGB", (const uint8_t*)"\0", 1);
               gama(c, 30000); break;
      case 17: *name = "gAMA then sRGB"; gama(c, 30000);
               put_chunk(c, "sRGB", (const uint8_t*)"\0", 1); break;
      case 18: *name = "cICP, gAMA, sRGB: gAMA's";
               put_chunk(c, "cICP", (const uint8_t*)"\1\15\0\1", 4); gama(c, 30000);
               put_chunk(c, "sRGB", (const uint8_t*)"\0", 1); break;
      case 19: *name = "gAMA, sRGB, cICP: sRGB's"; gama(c, 30000);
               put_chunk(c, "sRGB", (const uint8_t*)"\0", 1);
               put_chunk(c, "cICP", (const uint8_t*)"\1\15\0\1", 4); break;
      case 20: *name = "cICP, sRGB, gAMA: sRGB's";
               put_chunk(c, "cICP", (const uint8_t*)"\1\15\0\1", 4);
               put_chunk(c, "sRGB", (const uint8_t*)"\0", 1); gama(c, 30000); break;
      case 21: *name = "sBIT, gAMA";
               put_chunk(c, "sBIT", depth == 16 ? (const uint8_t*)"\12\14\13\7"
                     : (const uint8_t*)"\5\6\5\3", 4);
               gama(c, 45455); break;
      case 22: *name = "sBIT 3, gAMA";
               put_chunk(c, "sBIT", (const uint8_t*)"\3\2\3\1", 4);
               gama(c, 45455); break;
      case 23: *name = "sBIT 16, gAMA 2.2";
               put_chunk(c, "sBIT", depth == 16 ? (const uint8_t*)"\20\20\20\20"
                     : (const uint8_t*)"\10\10\10\10", 4);
               gama(c, 220000); break;
      case 24: *name = "a second gAMA, a broken sRGB";
               gama(c, 45455); gama(c, 220000);
               put_chunk(c, "sRGB", (const uint8_t*)"\4", 1); break;
      default: return 0;
   }
   return 1;
}

/* sBIT is one byte a channel: RGB files take three of the four */
static void fix_sbit(struct chunks *c, int ct)
{
   size_t at = 0;
   if (ct != 2)
      return;
   while (at < c->n)
   {
      uint32_t n = ((uint32_t)c->d[at] << 24) | ((uint32_t)c->d[at + 1] << 16)
         | ((uint32_t)c->d[at + 2] << 8) | c->d[at + 3];
      if (!memcmp(c->d + at + 4, "sBIT", 4) && n == 4)
      {
         uint8_t v[3];
         size_t  rest = c->n - (at + 16);
         struct chunks t;
         memcpy(v, c->d + at + 8, 3);
         memcpy(t.d, c->d + at + 16, rest);
         c->n = at;
         put_chunk(c, "sBIT", v, 3);
         memcpy(c->d + c->n, t.d, rest);
         c->n += rest;
         return;
      }
      at += 12 + n;
   }
}

static void colour_cases(struct mem *m)
{
   static const int types[][2] = { { 2, 8 }, { 2, 16 }, { 6, 8 }, { 6, 16 } };
   static const uint32_t sizes[][2] = { { 13, 9 }, { 64, 16 } };
   unsigned t, s, il, k;

   for (t = 0; t < sizeof(types) / sizeof(types[0]); t++)
   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
   for (il = 0; il < 2; il++)
   for (k = 0; ; k++)
   {
      int           ct = types[t][0], depth = types[t][1];
      struct chunks c;
      const char   *set;
      char          name[160];

      if (!colour_set(k, &c, depth, &set))
         break;
      fix_sbit(&c, ct);
      if (!make_png(m, sizes[s][0], sizes[s][1], depth, ct, (int)il, 0,
               t * 131 + s * 17 + il * 7 + k))
      {
         TEST_CHECK(!"libpng could not write the image");
         continue;
      }
      /* after the signature and IHDR */
      if (m->n + c.n > m->cap)
      {
         m->cap = m->n + c.n;
         m->p   = (uint8_t*)realloc(m->p, m->cap);
      }
      memmove(m->p + 33 + c.n, m->p + 33, m->n - 33);
      memcpy(m->p + 33, c.d, c.n);
      m->n += c.n;
      sprintf(name, "type %d depth %d %ux%u %s, %s", ct, depth,
            (unsigned)sizes[s][0], (unsigned)sizes[s][1],
            il ? "Adam7" : "plain", set);
      /* only RGB to gray reads the colour chunks */
      compare(m, name, gray_spaces, 2);
   }
}

int main(void)
{
   static const int types[][2] = {
      { 0, 1 }, { 0, 2 }, { 0, 4 }, { 0, 8 }, { 0, 16 }, { 2, 8 }, { 2, 16 },
      { 3, 1 }, { 3, 2 }, { 3, 4 }, { 3, 8 }, { 4, 8 }, { 4, 16 }, { 6, 8 }, { 6, 16 } };
   static const uint32_t sizes[][2] = { { 1, 1 }, { 3, 2 }, { 5, 3 }, { 13, 9 }, { 20, 17 } };
   struct mem m;
   unsigned t, s, il, tr;

   if (png_access_version_number() != 10659)
   {
      fprintf(stderr, "test_png_rows: the oracle is libpng %s, it must be 1.6.59\n",
            png_get_libpng_ver(NULL));
      return 1;
   }
   img_a  = (uint8_t*)malloc(1 << 20);
   img_b  = (uint8_t*)malloc(1 << 20);
   lp.img = (uint8_t*)malloc(1 << 20);
   op.img = (uint8_t*)malloc(1 << 20);
   memset(&m, 0, sizeof(m));

   for (t = 0; t < sizeof(types) / sizeof(types[0]); t++)
   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
   for (il = 0; il < 2; il++)
   for (tr = 0; tr < 2; tr++)
   {
      int  ct = types[t][0], depth = types[t][1];
      char name[96];
      if (tr && (ct & 4))
         continue;
      if (!make_png(&m, sizes[s][0], sizes[s][1], depth, ct, (int)il, (int)tr, t * 977 + s * 31 + il))
      {
         fprintf(stderr, "libpng could not write type %d depth %d tRNS %u\n", ct, depth, tr);
         TEST_CHECK(!"libpng could not write the image");
         continue;
      }
      sprintf(name, "type %d depth %d %ux%u %s%s", ct, depth,
            (unsigned)sizes[s][0], (unsigned)sizes[s][1],
            il ? "Adam7" : "plain", tr ? " tRNS" : "");
      compare(&m, name, all_spaces, 6);
   }

   colour_cases(&m);

   free(img_a);
   free(img_b);
   free(lp.img);
   free(op.img);
   free(m.p);
   if (test_failures)
   {
      fprintf(stderr, "test_png_rows: %d check(s) failed over %u cases\n", test_failures, cases);
      return 1;
   }
   printf("test_png_rows: ok, %u cases (%u that libpng refuses)\n", cases, unsupported);
   return 0;
}
