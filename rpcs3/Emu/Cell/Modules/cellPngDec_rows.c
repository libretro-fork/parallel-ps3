/* cellPngDec's pixel conversions, as libpng 1.6 does them; see
 * cellPngDec_rows.h. C89. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cellPngDec_rows.h"

static const uint8_t pdr_dx[7] = { 8, 8, 4, 4, 2, 2, 1 };

/* libpng's png_combine_row masks: whether pixel x (of 8) of a row is
 * pass p's own (S) or drawn by its blocky display (B) */
#define PDR_S_COPY(p, x) ((((p) < 4 ? (0x80088822u >> ((3 - (p)) * 8 + (7 - (x)))) \
   : (0xaa55ff00u >> ((7 - (p)) * 8 + (7 - (x)))))) & 1u)
#define PDR_B_COPY(p, x) ((((p) < 4 ? (0xff0fff33u >> ((3 - (p)) * 8 + (7 - (x)))) \
   : (0xff55ff00u >> ((7 - (p)) * 8 + (7 - (x)))))) & 1u)

/* ---- RGB to gray's coefficients and gamma, as libpng 1.6.59 sets them
 * up for png_set_rgb_to_gray with the default coefficients and no
 * png_set_gamma. Its fixed point (100000 is 1.0) and its floating-point
 * arithmetic, step for step, so the tables come out the same: from
 * png.c (png_muldiv, png_reciprocal, png_reciprocal2, png_XYZ_from_xy,
 * png_set_rgb_coefficients, png_build_gamma_table and the table
 * builders), pngrtran.c (png_init_gamma_values) and pngrutil.c (which
 * chunk's gamma counts). Copyright (c) 2018-2026 Cosmin Truta, 1998-2018
 * Glenn Randers-Pehrson, 1996-1997 Andreas Dilger, 1995-1996 Guy Eric
 * Schalnat, Group 42, Inc.; under the PNG Reference Library License
 * version 2. ---- */

#define PDR_FP_1        100000
#define PDR_SRGB_GAMMA  45455  /* PNG_GAMMA_sRGB_INVERSE */
#define PDR_MAX_GAMMA_8 11     /* PNG_MAX_GAMMA_8 */

static int pdr_muldiv(int32_t *res, int32_t a, int32_t times, int32_t divisor)
{
   double r;
   if (!divisor)
      return 0;
   if (!a || !times)
   {
      *res = 0;
      return 1;
   }
   r = a;
   r *= times;
   r /= divisor;
   r = floor(r + .5);
   if (r <= 2147483647. && r >= -2147483648.)
   {
      *res = (int32_t)r;
      return 1;
   }
   return 0;
}

static int32_t pdr_reciprocal(int32_t a)
{
   double r;
   if (!a)
      return 0;
   r = floor(1E10 / a + .5);
   return r <= 2147483647. && r >= -2147483648. ? (int32_t)r : 0;
}

static int32_t pdr_reciprocal2(int32_t a, int32_t b)
{
   double r;
   if (!a || !b)
      return 0;
   r = 1E15 / a;
   r /= b;
   r = floor(r + .5);
   return r <= 2147483647. && r >= -2147483648. ? (int32_t)r : 0;
}

static int pdr_gamma_significant(int32_t g)
{
   return g < PDR_FP_1 - 5000 || g > PDR_FP_1 + 5000; /* PNG_GAMMA_THRESHOLD_FIXED */
}

static int32_t pdr_fp_sub(int32_t a, int32_t b, int *error)
{
   if (b > 0)
   {
      if (-0x7fffffff + b <= a)
         return a - b;
   }
   else if (b < 0)
   {
      if (0x7fffffff + b >= a)
         return a - b;
   }
   else
      return a;
   *error = 1;
   return PDR_FP_1 / 2;
}

/* white, red, green, blue: x, y */
struct pdr_xy { int32_t wx, wy, rx, ry, gx, gy, bx, by; };

/* The Y of each primary (png_XYZ_from_xy); 0 when the values cannot be
 * used */
static int pdr_y_from_xy(const struct pdr_xy *xy, int32_t *ry, int32_t *gy,
      int32_t *by)
{
   const int32_t lim = PDR_FP_1 + PDR_FP_1 / 10;
   int32_t left, right, den, red_inverse, green_inverse, blue_scale;
   int     error = 0;

   if (xy->rx < 0 || xy->rx > lim || xy->ry < 0 || xy->ry > lim - xy->rx
         || xy->gx < 0 || xy->gx > lim || xy->gy < 0 || xy->gy > lim - xy->gx
         || xy->bx < 0 || xy->bx > lim || xy->by < 0 || xy->by > lim - xy->bx
         || xy->wx < 0 || xy->wx > lim || xy->wy < 5 || xy->wy > lim - xy->wx)
      return 0;

   if (!pdr_muldiv(&left, xy->gx - xy->bx, xy->ry - xy->by, 8)
         || !pdr_muldiv(&right, xy->gy - xy->by, xy->rx - xy->bx, 8))
      return 0;
   den = pdr_fp_sub(left, right, &error);
   if (error)
      return 0;

   if (!pdr_muldiv(&left, xy->gx - xy->bx, xy->wy - xy->by, 8)
         || !pdr_muldiv(&right, xy->gy - xy->by, xy->wx - xy->bx, 8))
      return 0;
   if (!pdr_muldiv(&red_inverse, xy->wy, den, pdr_fp_sub(left, right, &error))
         || error || red_inverse <= xy->wy)
      return 0;

   if (!pdr_muldiv(&left, xy->ry - xy->by, xy->wx - xy->bx, 8)
         || !pdr_muldiv(&right, xy->rx - xy->bx, xy->wy - xy->by, 8))
      return 0;
   if (!pdr_muldiv(&green_inverse, xy->wy, den, pdr_fp_sub(left, right, &error))
         || error || green_inverse <= xy->wy)
      return 0;

   blue_scale = pdr_fp_sub(pdr_fp_sub(pdr_reciprocal(xy->wy),
         pdr_reciprocal(red_inverse), &error), pdr_reciprocal(green_inverse),
         &error);
   if (error || blue_scale <= 0)
      return 0;

   /* png_XYZ_from_xy works out X and Z too, and fails if they overflow;
    * for x, y and z in range they cannot, given the checks above */
   {
      int32_t X;
      if (!pdr_muldiv(&X, xy->rx, PDR_FP_1, red_inverse)
            || !pdr_muldiv(ry, xy->ry, PDR_FP_1, red_inverse)
            || !pdr_muldiv(&X, PDR_FP_1 - xy->rx - xy->ry, PDR_FP_1, red_inverse)
            || !pdr_muldiv(&X, xy->gx, PDR_FP_1, green_inverse)
            || !pdr_muldiv(gy, xy->gy, PDR_FP_1, green_inverse)
            || !pdr_muldiv(&X, PDR_FP_1 - xy->gx - xy->gy, PDR_FP_1, green_inverse)
            || !pdr_muldiv(&X, xy->bx, blue_scale, PDR_FP_1)
            || !pdr_muldiv(by, xy->by, blue_scale, PDR_FP_1)
            || !pdr_muldiv(&X, PDR_FP_1 - xy->bx - xy->by, blue_scale, PDR_FP_1))
         return 0;
   }
   return 1;
}

/* png_set_rgb_coefficients: mDCV's chromaticities, else none for sRGB,
 * else cHRM's, else Rec. 709's */
static void pdr_gray_coefficients(struct pdr *p, const struct rpng_rows_info *info)
{
   struct pdr_xy xy;
   int32_t r, g, b, total;
   int     have = 0;

   if (info->chunks & RPNG_ROWS_MDCV)
   {
      const uint16_t *m = info->mdcv; /* red, green, blue, white; /50000 */
      xy.rx = (int32_t)m[0] << 1; xy.ry = (int32_t)m[1] << 1;
      xy.gx = (int32_t)m[2] << 1; xy.gy = (int32_t)m[3] << 1;
      xy.bx = (int32_t)m[4] << 1; xy.by = (int32_t)m[5] << 1;
      xy.wx = (int32_t)m[6] << 1; xy.wy = (int32_t)m[7] << 1;
      have  = 1;
   }
   else if (!(info->chunks & RPNG_ROWS_SRGB) && (info->chunks & RPNG_ROWS_CHRM))
   {
      const int32_t *c = info->chrm; /* white, red, green, blue */
      xy.wx = c[0]; xy.wy = c[1]; xy.rx = c[2]; xy.ry = c[3];
      xy.gx = c[4]; xy.gy = c[5]; xy.bx = c[6]; xy.by = c[7];
      have  = 1;
   }

   if (!have || !pdr_y_from_xy(&xy, &r, &g, &b))
   {
      p->gray_red   = 6968;
      p->gray_green = 23434;
      return;
   }
   /* what libpng's png_struct starts with, and keeps if the Ys are no use */
   p->gray_red = p->gray_green = 0;
   total = r + g + b;
   if (total > 0
         && r >= 0 && pdr_muldiv(&r, r, 32768, total) && r >= 0 && r <= 32768
         && g >= 0 && pdr_muldiv(&g, g, 32768, total) && g >= 0 && g <= 32768
         && b >= 0 && pdr_muldiv(&b, b, 32768, total) && b >= 0 && b <= 32768
         && r + g + b <= 32769)
   {
      int add = r + g + b > 32768 ? -1 : r + g + b < 32768 ? 1 : 0;
      if (add)
      {
         if (g >= r && g >= b)
            g += add;
         else if (r >= g && r >= b)
            r += add;
         else
            b += add;
      }
      if (r + g + b == 32768) /* else libpng stops on an internal error */
      {
         p->gray_red   = (uint32_t)r;
         p->gray_green = (uint32_t)g;
      }
      else
         p->valid = 0;
   }
}

static uint8_t pdr_gamma_8bit(unsigned v, int32_t g)
{
   if (v > 0 && v < 255)
      return (uint8_t)floor(255 * pow((int)v / 255., g * .00001) + .5);
   return (uint8_t)v;
}

static uint16_t pdr_gamma_16bit(unsigned v, int32_t g)
{
   if (v > 0 && v < 65535)
      return (uint16_t)floor(65535 * pow((int32_t)v / 65535., g * .00001) + .5);
   return (uint16_t)v;
}

static void pdr_table_8(uint8_t *t, int32_t g)
{
   unsigned i;
   if (pdr_gamma_significant(g))
      for (i = 0; i < 256; i++)
         t[i] = pdr_gamma_8bit(i, g);
   else
      for (i = 0; i < 256; i++)
         t[i] = (uint8_t)i;
}

/* png_build_16bit_table, flat: entry [low][high] at low * 256 + high */
static void pdr_table_16(uint16_t *t, unsigned shift, int32_t g)
{
   unsigned num      = 1u << (8 - shift);
   double   fmax     = 1.0 / (((int32_t)1 << (16 - shift)) - 1);
   uint32_t max      = (1u << (16 - shift)) - 1u;
   uint32_t max_by_2 = 1u << (15 - shift);
   int      sig      = pdr_gamma_significant(g);
   unsigned i, j;

   for (i = 0; i < num; i++)
      for (j = 0; j < 256; j++)
      {
         uint32_t ig = (j << (8 - shift)) + i;
         if (sig)
            t[i * 256 + j] = (uint16_t)floor(65535. * pow(ig * fmax, g * .00001) + .5);
         else
         {
            if (shift)
               ig = (ig * 65535u + max_by_2) / max;
            t[i * 256 + j] = (uint16_t)ig;
         }
      }
}

/* png_build_16to8_table: @g is the inverse of the correction */
static void pdr_table_16to8(uint16_t *t, unsigned shift, int32_t g)
{
   unsigned num  = 1u << (8 - shift);
   uint32_t max  = (1u << (16 - shift)) - 1u;
   uint32_t mask = num - 1;
   uint32_t last = 0;
   unsigned i;

   for (i = 0; i < 255; i++)
   {
      uint16_t out   = (uint16_t)(i * 257u);
      uint32_t bound = pdr_gamma_16bit(out + 128u, g);
      bound = (bound * max + 32768u) / 65535u + 1u;
      for (; last < bound; last++)
         t[(last & mask) * 256 + (last >> (8 - shift))] = out;
   }
   for (; last < (num << 8); last++)
      t[(last & mask) * 256 + (last >> (8 - shift))] = 65535u;
}

/* The file's gamma by libpng's precedence, then png_init_gamma_values and
 * png_build_gamma_table for RGB to gray; 0 when out of memory */
static int pdr_gray_gamma(struct pdr *p, const struct rpng_rows_info *info)
{
   uint32_t c = info->chunks;
   int32_t  file = 0, screen, to_linear, from_linear, correction;

   p->gray_gamma = 0;
   /* gAMA counts if nothing set the gamma before it; sRGB sets its own,
    * over gAMA's, unless a cICP came before it and a gamma is set */
   if ((c & RPNG_ROWS_GAMA)
         && (!(c & RPNG_ROWS_SRGB) || info->gama_at < info->srgb_at))
      file = (int32_t)info->gama;
   if ((c & RPNG_ROWS_SRGB) && !((c & RPNG_ROWS_CICP)
            && info->cicp_at < info->srgb_at && file))
      file = PDR_SRGB_GAMMA;

   if (file > 0)
      screen = pdr_reciprocal(file); /* the screen matches the file */
   else
      file = screen = PDR_FP_1;
   if (!pdr_gamma_significant(file) && !pdr_gamma_significant(screen))
      return 1; /* no tables: the plain path */

   p->gray_gamma = 1;
   to_linear     = pdr_reciprocal(file);
   if (screen > 0)
   {
      from_linear = pdr_reciprocal(screen);
      correction  = pdr_reciprocal2(screen, file);
   }
   else
   {
      from_linear = file;
      correction  = PDR_FP_1;
   }

   if (info->depth <= 8)
   {
      pdr_table_8(p->gamma_8[0], correction);
      pdr_table_8(p->gamma_8[1], to_linear);
      pdr_table_8(p->gamma_8[2], from_linear);
   }
   else
   {
      unsigned sig = 0, shift = 0;
      size_t   n;
      if (c & RPNG_ROWS_SBIT)
      {
         sig = info->sbit[0];
         if (info->sbit[1] > sig) sig = info->sbit[1];
         if (info->sbit[2] > sig) sig = info->sbit[2];
      }
      if (sig > 0 && sig < 16)
         shift = 16 - sig;
      if ((p->flags & PDR_STRIP_16) && shift < 16 - PDR_MAX_GAMMA_8)
         shift = 16 - PDR_MAX_GAMMA_8;
      if (shift > 8)
         shift = 8;
      p->gamma_shift = shift;

      n = (size_t)3 << (16 - shift);
      if (n > p->gamma_16_cap)
      {
         uint16_t *t = (uint16_t*)malloc(n * sizeof(*t));
         if (!t)
            return 0;
         free(p->gamma_16);
         p->gamma_16     = t;
         p->gamma_16_cap = n;
      }
      n /= 3;
      if (p->flags & PDR_STRIP_16)
         pdr_table_16to8(p->gamma_16, shift, pdr_reciprocal(correction));
      else
         pdr_table_16(p->gamma_16, shift, correction);
      pdr_table_16(p->gamma_16 + n, shift, to_linear);
      pdr_table_16(p->gamma_16 + n * 2, shift, from_linear);
   }
   return 1;
}

int pdr_color_space(unsigned color_type)
{
   switch (color_type)
   {
      case 0: return PDR_CS_GRAYSCALE;
      case 2: return PDR_CS_RGB;
      case 3: return PDR_CS_PALETTE;
      case 4: return PDR_CS_GRAYSCALE_ALPHA;
      case 6: return PDR_CS_RGBA; /* RGBA and ARGB are not told apart */
   }
   return -1;
}

static int pdr_cs_alpha(int cs)
{
   return cs == PDR_CS_RGBA || cs == PDR_CS_ARGB || cs == PDR_CS_GRAYSCALE_ALPHA;
}

static int pdr_cs_rgb(int cs)
{
   return cs == PDR_CS_ARGB || cs == PDR_CS_RGBA || cs == PDR_CS_RGB;
}

unsigned pdr_flags_for(const struct rpng_rows_info *info, int out_space,
      int alpha_select, unsigned alpha, unsigned out_depth, unsigned *filler,
      int *unsupported)
{
   unsigned flags  = 0;
   int      in_cs  = pdr_color_space(info->color_type);

   *filler      = 0xff;
   *unsupported = 0;

   /* the PS3 strips 16 bits to 8, it had no png_set_scale_16 */
   if (info->depth == 16 && out_depth == 8)
      flags |= PDR_STRIP_16;

   if (in_cs != out_space)
   {
      int in_alpha  = pdr_cs_alpha(in_cs);
      int out_alpha = pdr_cs_alpha(out_space);
      int before    = out_space == PDR_CS_ARGB;

      if (out_alpha && !in_alpha)
      {
         if (alpha_select == PDR_FIX_ALPHA)
         {
            flags  |= PDR_ADD_ALPHA | (before ? PDR_FILLER_BEFORE : 0);
            *filler = alpha;
         }
         else if (info->trns_len)
            flags |= PDR_EXPAND | PDR_EXPAND_TRNS; /* tRNS to alpha */
         else
            flags |= PDR_ADD_ALPHA | (before ? PDR_FILLER_BEFORE : 0);
      }
      else if (in_alpha && !out_alpha)
         flags |= PDR_STRIP_ALPHA;
      else if (out_space == PDR_CS_ARGB && in_cs == PDR_CS_RGBA)
         flags |= PDR_SWAP_ALPHA;

      if (pdr_cs_rgb(out_space))
      {
         if (in_cs == PDR_CS_PALETTE)
            flags |= PDR_EXPAND | PDR_EXPAND_TRNS; /* palette to RGB */
         if ((in_cs == PDR_CS_GRAYSCALE || in_cs == PDR_CS_GRAYSCALE_ALPHA)
               && info->depth < 8)
            flags |= PDR_EXPAND; /* gray 1, 2, 4 to 8 */
      }
      else if (pdr_cs_rgb(in_cs))
         flags |= PDR_RGB_TO_GRAY;
      else
         *unsupported = 1;
   }
   return flags;
}

static unsigned pdr_bits(const struct pdr_format *f)
{
   return (unsigned)f->depth * f->channels;
}

static size_t pdr_rowbytes(const struct pdr_format *f, uint32_t width)
{
   return ((size_t)width * pdr_bits(f) + 7) / 8;
}

/* What the conversions do to the format, as png_read_transform_info */
static int pdr_format_after(const struct rpng_rows_info *info, unsigned flags,
      struct pdr_format *f, unsigned *pixel_bits)
{
   f->color_type = info->color_type;
   f->depth      = info->depth;

   if (flags & PDR_EXPAND)
   {
      if (f->color_type == 3)
      {
         f->color_type = (info->trns_len && (flags & PDR_EXPAND_TRNS)) ? 6 : 2;
         f->depth      = 8;
      }
      else
      {
         if (info->trns_len && (flags & PDR_EXPAND_TRNS))
            f->color_type |= 4;
         if (f->depth < 8)
            f->depth = 8;
      }
   }
   if ((flags & PDR_STRIP_ALPHA) && (f->color_type & 4))
      f->color_type &= ~4;
   if ((flags & PDR_RGB_TO_GRAY) && (f->color_type & 2) && f->color_type != 3)
      f->color_type &= ~2;
   if ((flags & PDR_STRIP_16) && f->depth == 16)
      f->depth = 8;

   f->channels = f->color_type == 3 ? 1 : (uint8_t)(1 + ((f->color_type & 2) ? 2 : 0)
      + ((f->color_type & 4) ? 1 : 0));

   *pixel_bits = (unsigned)f->depth * f->channels;

   if ((flags & PDR_ADD_ALPHA) && (f->color_type == 0 || f->color_type == 2))
   {
      f->channels++;
      f->color_type |= 4;
      /* libpng counts the channel, but only fills 8 and 16-bit rows: a
       * gray row under 8 bits is written as it is, and fails libpng's
       * size check unless its bytes come out the same */
      if (f->depth < 8)
         return ((size_t)info->width * (*pixel_bits) + 7) / 8
            == ((size_t)info->width * f->depth * f->channels + 7) / 8;
      *pixel_bits = (unsigned)f->depth * f->channels;
   }
   return 1;
}

int pdr_setup(struct pdr *p, const struct rpng_rows_info *info,
      unsigned flags, unsigned filler)
{
   size_t need;

   p->info   = info;
   p->flags  = flags;
   p->filler = (uint16_t)filler;
   p->valid  = pdr_format_after(info, flags, &p->out, &p->pixel_bits);
   p->out_rowbytes = pdr_rowbytes(&p->out, info->width);

   /* only an RGB file is turned gray (a palette one is refused) */
   p->gray_gamma = 0;
   if ((flags & PDR_RGB_TO_GRAY) && (info->color_type & 2)
         && info->color_type != 3)
   {
      pdr_gray_coefficients(p, info);
      if (!pdr_gray_gamma(p, info))
         return 0;
   }

   /* three rows of up to 8 bytes a pixel */
   {
      uint64_t need64 = (uint64_t)info->width * 24 + 64;
      if (need64 != (size_t)need64)
         return 0;
      need = (size_t)need64;
   }
   if (need > p->work_cap)
   {
      uint8_t *w = (uint8_t*)malloc(need);
      if (!w)
         return 0;
      free(p->work);
      p->work     = w;
      p->work_cap = need;
   }
   return 1;
}

void pdr_free(struct pdr *p)
{
   free(p->work);
   free(p->gamma_16);
   p->work         = NULL;
   p->work_cap     = 0;
   p->gamma_16     = NULL;
   p->gamma_16_cap = 0;
}

static unsigned pdr_get_bits(const uint8_t *row, uint32_t x, unsigned bits)
{
   return (row[((size_t)x * bits) / 8] >> (8 - bits - (unsigned)(((size_t)x * bits) % 8)))
      & ((1u << bits) - 1);
}

static void pdr_set_bits(uint8_t *row, size_t x, unsigned bits, unsigned v)
{
   unsigned sh = 8 - bits - (unsigned)((x * bits) % 8);
   uint8_t  m  = (uint8_t)(((1u << bits) - 1) << sh);
   row[(x * bits) / 8] = (uint8_t)((row[(x * bits) / 8] & ~m) | ((v << sh) & m));
}

static void pdr_copy_px(uint8_t *dst, size_t dx, const uint8_t *src, size_t sx,
      unsigned bits)
{
   if (bits < 8)
      pdr_set_bits(dst, dx, bits, pdr_get_bits(src, (uint32_t)sx, bits));
   else
      memcpy(dst + dx * (bits / 8), src + sx * (bits / 8), bits / 8);
}

/* @n pixels from the start of src over dst; the bits past the last pixel
 * in its byte stay as they were, as png_combine_row leaves them */
static void pdr_copy_row(uint8_t *dst, const uint8_t *src, uint32_t n,
      unsigned bits)
{
   size_t   total = (size_t)n * bits;
   size_t   whole = total / 8;
   unsigned rest  = (unsigned)(total % 8);
   memcpy(dst, src, whole);
   if (rest)
   {
      uint8_t m = (uint8_t)(0xff00u >> rest);
      dst[whole] = (uint8_t)((dst[whole] & ~m) | (src[whole] & m));
   }
}

const uint8_t *pdr_convert(struct pdr *p, const uint8_t *row, uint32_t width)
{
   const struct rpng_rows_info *info = p->info;
   /* each step reads @a and writes the other work row; the first reads
    * the decoder's row where it is, and with no step it is lent back */
   uint8_t       *w0 = p->work;
   uint8_t       *w1 = p->work + (size_t)width * 8 + 16;
   const uint8_t *a  = row;
   uint8_t       *b  = w0;
   unsigned ct = info->color_type;
   unsigned d  = info->depth;
   unsigned ch = info->channels;
   uint32_t i;

   /* expand */
   if (p->flags & PDR_EXPAND)
   {
      if (ct == 3)
      {
         int alpha = info->trns_len && (p->flags & PDR_EXPAND_TRNS);
         uint8_t *o = b;
         for (i = 0; i < width; i++)
         {
            unsigned idx = d < 8 ? pdr_get_bits(a, i, d) : a[i];
            if (idx < info->palette_len)
            {
               o[0] = info->palette[idx * 3];
               o[1] = info->palette[idx * 3 + 1];
               o[2] = info->palette[idx * 3 + 2];
            }
            else
               o[0] = o[1] = o[2] = 0;
            o += 3;
            if (alpha)
               *o++ = idx < info->trns_len ? info->trns[idx] : 0xff;
         }
         a = b; b = b == w0 ? w1 : w0;
         ct = alpha ? 6 : 2;
         d  = 8;
         ch = alpha ? 4 : 3;
      }
      else
      {
         int      trns = info->trns_len && (p->flags & PDR_EXPAND_TRNS);
         unsigned key  = trns && info->trns_len >= 2
            ? ((unsigned)info->trns[0] << 8) | info->trns[1] : 0;
         if (ct == 0 && d < 8)
         {
            unsigned scale = d == 1 ? 0xff : d == 2 ? 0x55 : 0x11;
            for (i = 0; i < width; i++)
               b[i] = (uint8_t)(pdr_get_bits(a, i, d) * scale);
            key = (key & ((1u << d) - 1)) * scale;
            a = b; b = b == w0 ? w1 : w0;
            d = 8;
         }
         if (trns && (ct == 0 || ct == 2))
         {
            uint8_t *o = b;
            const uint8_t *s = a;
            if (ct == 0)
            {
               for (i = 0; i < width; i++)
               {
                  if (d == 8)
                  {
                     *o++ = *s;
                     *o++ = *s == (key & 0xff) ? 0 : 0xff;
                     s++;
                  }
                  else
                  {
                     int match = s[0] == ((key >> 8) & 0xff) && s[1] == (key & 0xff);
                     *o++ = s[0]; *o++ = s[1];
                     *o++ = match ? 0 : 0xff; *o++ = match ? 0 : 0xff;
                     s += 2;
                  }
               }
               ct = 4;
               ch = 2;
            }
            else
            {
               const uint8_t *k = info->trns;
               for (i = 0; i < width; i++)
               {
                  if (d == 8)
                  {
                     int match = s[0] == k[1] && s[1] == k[3] && s[2] == k[5];
                     o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
                     o[3] = match ? 0 : 0xff;
                     o += 4; s += 3;
                  }
                  else
                  {
                     int match = !memcmp(s, k, 6);
                     memcpy(o, s, 6);
                     o[6] = o[7] = match ? 0 : 0xff;
                     o += 8; s += 6;
                  }
               }
               ct = 6;
               ch = 4;
            }
            a = b; b = b == w0 ? w1 : w0;
         }
      }
   }

   /* strip alpha */
   if ((p->flags & PDR_STRIP_ALPHA) && (ct & 4))
   {
      unsigned bps = d / 8, keep = (ch - 1) * bps;
      uint8_t *o = b;
      const uint8_t *s = a;
      for (i = 0; i < width; i++)
      {
         memcpy(o, s, keep);
         o += keep;
         s += keep + bps;
      }
      a = b; b = b == w0 ? w1 : w0;
      ct &= ~4u;
      ch--;
   }

   /* RGB to gray: png_do_rgb_to_gray */
   if ((p->flags & PDR_RGB_TO_GRAY) && (ct & 2) && ct != 3)
   {
      int            alpha = (ct & 4) != 0;
      uint32_t       rc = p->gray_red, gc = p->gray_green, bc = 32768 - rc - gc;
      uint8_t       *o = b;
      const uint8_t *s = a;
      if (d == 8 && p->gray_gamma)
      {
         const uint8_t *table = p->gamma_8[0];
         const uint8_t *to_1  = p->gamma_8[1];
         const uint8_t *from_1 = p->gamma_8[2];
         for (i = 0; i < width; i++)
         {
            unsigned r = s[0], g = s[1], bl = s[2];
            *o++ = (r == g && r == bl) ? table[r] : from_1[(rc * to_1[r]
                  + gc * to_1[g] + bc * to_1[bl] + 16384) >> 15];
            s += 3;
            if (alpha)
               *o++ = *s++;
         }
      }
      else if (d == 8)
         for (i = 0; i < width; i++)
         {
            unsigned r = s[0], g = s[1], bl = s[2];
            *o++ = (r == g && r == bl) ? (uint8_t)r
               : (uint8_t)((rc * r + gc * g + bc * bl) >> 15);
            s += 3;
            if (alpha)
               *o++ = *s++;
         }
      else if (p->gray_gamma)
      {
         /* table[(v & 0xff) >> shift][v >> 8] */
         unsigned        sh     = p->gamma_shift;
         size_t          n      = (size_t)256 << (8 - sh);
         const uint16_t *table  = p->gamma_16;
         const uint16_t *to_1   = table + n;
         const uint16_t *from_1 = table + n * 2;
#define PDR_G16(t, v) (t)[(((v) & 0xffu) >> sh) * 256 + ((v) >> 8)]
         for (i = 0; i < width; i++)
         {
            unsigned r  = ((unsigned)s[0] << 8) | s[1];
            unsigned g  = ((unsigned)s[2] << 8) | s[3];
            unsigned bl = ((unsigned)s[4] << 8) | s[5];
            unsigned w;
            if (r == g && r == bl)
               w = PDR_G16(table, r);
            else
            {
               unsigned gray = (unsigned)((rc * PDR_G16(to_1, r)
                     + gc * PDR_G16(to_1, g) + bc * PDR_G16(to_1, bl)
                     + 16384) >> 15);
               w = PDR_G16(from_1, gray);
            }
#undef PDR_G16
            *o++ = (uint8_t)(w >> 8);
            *o++ = (uint8_t)w;
            s += 6;
            if (alpha)
            {
               *o++ = *s++;
               *o++ = *s++;
            }
         }
      }
      else
         for (i = 0; i < width; i++)
         {
            uint32_t r  = ((uint32_t)s[0] << 8) | s[1];
            uint32_t g  = ((uint32_t)s[2] << 8) | s[3];
            uint32_t bl = ((uint32_t)s[4] << 8) | s[5];
            unsigned gray = (unsigned)((rc * r + gc * g + bc * bl + 16384) >> 15);
            *o++ = (uint8_t)(gray >> 8);
            *o++ = (uint8_t)gray;
            s += 6;
            if (alpha)
            {
               *o++ = *s++;
               *o++ = *s++;
            }
         }
      a = b; b = b == w0 ? w1 : w0;
      ct &= ~2u;
      ch -= 2;
   }

   /* 16 to 8 bits: the high byte */
   if ((p->flags & PDR_STRIP_16) && d == 16)
   {
      size_t n = (size_t)width * ch, k;
      for (k = 0; k < n; k++)
         b[k] = a[k * 2];
      a = b; b = b == w0 ? w1 : w0;
      d = 8;
   }

   /* a filler for alpha */
   if ((p->flags & PDR_ADD_ALPHA) && (ct == 0 || ct == 2) && d >= 8)
   {
      unsigned bps    = d / 8, px = ch * bps;
      int      before = (p->flags & PDR_FILLER_BEFORE) != 0;
      uint8_t  f[2];
      uint8_t *o = b;
      const uint8_t *s = a;
      if (bps == 2)
      {
         f[0] = (uint8_t)(p->filler >> 8);
         f[1] = (uint8_t)p->filler;
      }
      else
         f[0] = (uint8_t)p->filler;
      for (i = 0; i < width; i++)
      {
         if (before)
         {
            memcpy(o, f, bps);
            memcpy(o + bps, s, px);
         }
         else
         {
            memcpy(o, s, px);
            memcpy(o + px, f, bps);
         }
         o += px + bps;
         s += px;
      }
      a = b; b = b == w0 ? w1 : w0;
      ct |= 4;
      ch++;
   }

   /* alpha first */
   if ((p->flags & PDR_SWAP_ALPHA) && (ct & 4))
   {
      unsigned bps = d / 8, px = ch * bps;
      uint8_t *o = b;
      const uint8_t *s = a;
      for (i = 0; i < width; i++)
      {
         memcpy(o, s + px - bps, bps);
         memcpy(o + bps, s, px - bps);
         o += px;
         s += px;
      }
      a = b; b = b == w0 ? w1 : w0;
   }

   return a;
}

const uint8_t *pdr_expand_pass(struct pdr *p, const uint8_t *row,
      uint32_t width, unsigned pass)
{
   unsigned bits = p->pixel_bits;
   uint8_t *o    = p->work + 2 * ((size_t)p->info->width * 8 + 16);
   uint32_t c, w = p->info->width;
   unsigned dx;

   if (pass >= 6)
      return row;
   dx = pdr_dx[pass];
   for (c = 0; c < w; c++)
   {
      uint32_t i = c / dx;
      if (i >= width)
         i = width - 1;
      pdr_copy_px(o, c, row, i, bits);
   }
   return o;
}

void pdr_combine(const struct pdr *p, uint8_t *dst, const uint8_t *src,
      int pass, int display)
{
   unsigned bits = p->pixel_bits;
   uint32_t c, w = p->info->width;

   if (pass < 0 || pass >= 6)
   {
      pdr_copy_row(dst, src, w, bits);
      return;
   }
   for (c = 0; c < w; c++)
   {
      unsigned x = c & 7;
      if (display ? PDR_B_COPY((unsigned)pass, x) : PDR_S_COPY((unsigned)pass, x))
         pdr_copy_px(dst, c, src, c, bits);
   }
}

void pdr_scatter(const struct pdr *p, uint8_t *dst, const uint8_t *src,
      const struct rpng_rows_row *row)
{
   unsigned bits = p->pixel_bits;
   uint32_t i;

   if (row->dx == 1)
   {
      pdr_copy_row(dst, src, row->width, bits);
      return;
   }
   for (i = 0; i < row->width; i++)
      pdr_copy_px(dst, (size_t)row->x0 + (size_t)i * row->dx, src, i, bits);
}

/* png_read_push_finish_row, with interlace handling on: every pass has
 * all the image's rows; passes 1, 3 and 5 are skipped for a narrow image */
static void pdr_finish_row(const struct pdr *p, struct pdr_push *s)
{
   if (++s->row_number < p->info->height)
      return;
   if (!p->info->interlace)
      return;
   s->row_number = 0;
   s->pass++;
   if ((s->pass == 1 && p->info->width < 5)
         || (s->pass == 3 && p->info->width < 3)
         || (s->pass == 5 && p->info->width < 2))
      s->pass++;
   if (s->pass > 7)
      s->pass--;
}

void pdr_push_begin(struct pdr_push *s)
{
   s->row_number = 0;
   s->pass       = 0;
}

#define PDR_HAVE(r) do { fn(userdata, (r), s->row_number, s->pass); pdr_finish_row(p, s); } while (0)

void pdr_push_row(struct pdr *p, struct pdr_push *s, const uint8_t *row,
      uint32_t width, pdr_row_fn fn, void *userdata)
{
   const uint8_t *r;
   int i;

   if (!p->info->interlace)
   {
      PDR_HAVE(row);
      return;
   }

   r = pdr_expand_pass(p, row, width, s->pass);

   switch (s->pass)
   {
      case 0:
         for (i = 0; i < 8 && s->pass == 0; i++)
            PDR_HAVE(r);
         if (s->pass == 2) /* pass 1 might be empty */
            for (i = 0; i < 4 && s->pass == 2; i++)
               PDR_HAVE(NULL);
         if (s->pass == 4 && p->info->height <= 4)
            for (i = 0; i < 2 && s->pass == 4; i++)
               PDR_HAVE(NULL);
         if (s->pass == 6 && p->info->height <= 4)
            PDR_HAVE(NULL);
         break;
      case 1:
         for (i = 0; i < 8 && s->pass == 1; i++)
            PDR_HAVE(r);
         if (s->pass == 2)
            for (i = 0; i < 4 && s->pass == 2; i++)
               PDR_HAVE(NULL);
         break;
      case 2:
         for (i = 0; i < 4 && s->pass == 2; i++)
            PDR_HAVE(r);
         for (i = 0; i < 4 && s->pass == 2; i++)
            PDR_HAVE(NULL);
         if (s->pass == 4) /* pass 3 might be empty */
            for (i = 0; i < 2 && s->pass == 4; i++)
               PDR_HAVE(NULL);
         break;
      case 3:
         for (i = 0; i < 4 && s->pass == 3; i++)
            PDR_HAVE(r);
         if (s->pass == 4)
            for (i = 0; i < 2 && s->pass == 4; i++)
               PDR_HAVE(NULL);
         break;
      case 4:
         for (i = 0; i < 2 && s->pass == 4; i++)
            PDR_HAVE(r);
         for (i = 0; i < 2 && s->pass == 4; i++)
            PDR_HAVE(NULL);
         if (s->pass == 6) /* pass 5 might be empty */
            PDR_HAVE(NULL);
         break;
      case 5:
         for (i = 0; i < 2 && s->pass == 5; i++)
            PDR_HAVE(r);
         if (s->pass == 6)
            PDR_HAVE(NULL);
         break;
      default:
         PDR_HAVE(r);
         if (s->pass != 6)
            break;
         PDR_HAVE(NULL);
         break;
   }
}
