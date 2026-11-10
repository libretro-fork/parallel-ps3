/* cellPngDec's pixel conversions, as libpng 1.6 does them; see
 * cellPngDec_rows.h. C89. */

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

/* default libpng coefficients for png_set_rgb_to_gray, /32768 */
#define PDR_RED   6968u
#define PDR_GREEN 23434u
#define PDR_BLUE  (32768u - PDR_RED - PDR_GREEN)

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
   p->work     = NULL;
   p->work_cap = 0;
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
   uint8_t *a  = p->work;
   uint8_t *b  = p->work + (size_t)width * 8 + 16;
   uint8_t *t;
   unsigned ct = info->color_type;
   unsigned d  = info->depth;
   unsigned ch = info->channels;
   uint32_t i;

   memcpy(a, row, rpng_rows_rowbytes(info, width));

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
         t = a; a = b; b = t;
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
            t = a; a = b; b = t;
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
            t = a; a = b; b = t;
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
      t = a; a = b; b = t;
      ct &= ~4u;
      ch--;
   }

   /* RGB to gray */
   if ((p->flags & PDR_RGB_TO_GRAY) && (ct & 2) && ct != 3)
   {
      int      alpha = (ct & 4) != 0;
      uint8_t *o = b;
      const uint8_t *s = a;
      for (i = 0; i < width; i++)
      {
         if (d == 8)
         {
            unsigned r = s[0], g = s[1], bl = s[2];
            *o++ = (r == g && r == bl) ? (uint8_t)r
               : (uint8_t)((PDR_RED * r + PDR_GREEN * g + PDR_BLUE * bl) >> 15);
            s += 3;
            if (alpha)
               *o++ = *s++;
         }
         else
         {
            unsigned r  = ((unsigned)s[0] << 8) | s[1];
            unsigned g  = ((unsigned)s[2] << 8) | s[3];
            unsigned bl = ((unsigned)s[4] << 8) | s[5];
            unsigned gray = (r == g && r == bl) ? r
               : (unsigned)((PDR_RED * (uint32_t)r + PDR_GREEN * (uint32_t)g
                     + PDR_BLUE * (uint32_t)bl + 16384) >> 15);
            *o++ = (uint8_t)(gray >> 8);
            *o++ = (uint8_t)gray;
            s += 6;
            if (alpha)
            {
               *o++ = *s++;
               *o++ = *s++;
            }
         }
      }
      t = a; a = b; b = t;
      ct &= ~2u;
      ch -= 2;
   }

   /* 16 to 8 bits: the high byte */
   if ((p->flags & PDR_STRIP_16) && d == 16)
   {
      size_t n = (size_t)width * ch, k;
      for (k = 0; k < n; k++)
         b[k] = a[k * 2];
      t = a; a = b; b = t;
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
      t = a; a = b; b = t;
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
      t = a; a = b; b = t;
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
