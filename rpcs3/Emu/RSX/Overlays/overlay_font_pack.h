/* The overlays' glyph atlas: a block of code points drawn by
 * libretro-common's rtt into one 8-bit texture, each glyph in a cell of
 * its own with the box, offsets and advance to place it with. The layout
 * is the one stb_truetype's packer made with RPCS3's settings (rows
 * filled left to right, one texel of padding, the glyphs drawn
 * oversampled and box-filtered for bilinear sampling at fractional
 * positions), so the overlays place text as before.
 *
 * Plain C, so the CI test (rpcs3/libretro/tests/test_font.c) builds the
 * same code against stb_truetype. */

#ifndef RPCS3_OVERLAY_FONT_PACK_H
#define RPCS3_OVERLAY_FONT_PACK_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <retro_inline.h>
#include <gfx/font/rtt.h>

/* C casts, as this is C, in the C++ that includes it too */
#if defined(__cplusplus) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

/* How far rtt may flatten a curve from its true shape, in pixels, for
 * RPCS3's fonts (rtt_set_flatness): finer than rtt's default, as the
 * overlays and cellFont draw small text, where a straying edge shows. At
 * this, glyphs come out closer to their true coverage than stb_truetype
 * drew them, and are still drawn in about half its time. */
#define RPCS3_FONT_FLATNESS 0.1f

/* Up to this much oversampling */
#define FONT_PACK_MAX_OVERSAMPLE 8

typedef struct font_packed_char
{
   /* The glyph's cell in the atlas, in texels */
   uint16_t x0, y0, x1, y1;
   /* Where the cell goes from the pen, in pixels, and how far the pen
    * moves on */
   float xoff, yoff, xadvance, xoff2, yoff2;
} font_packed_char;

/* A glyph to draw: the screen box and its texture coordinates */
typedef struct font_quad
{
   float x0, y0, s0, t0;
   float x1, y1, s1, t1;
} font_quad;

/* A box filter @k texels wide along each row, as the cell is drawn at
 * @k times the size */
static INLINE void font_pack_prefilter_h(uint8_t *px, int w, int h, int stride,
      int k)
{
   int j;
   for (j = 0; j < h; j++, px += stride)
   {
      uint8_t  held[FONT_PACK_MAX_OVERSAMPLE] = {0};
      unsigned total                          = 0;
      int      i;
      for (i = 0; i <= w - k; i++)
      {
         total += (unsigned)(px[i] - held[i & (FONT_PACK_MAX_OVERSAMPLE - 1)]);
         held[(i + k) & (FONT_PACK_MAX_OVERSAMPLE - 1)] = px[i];
         px[i] = (uint8_t)(total / (unsigned)k);
      }
      for (; i < w; i++)
      {
         total -= held[i & (FONT_PACK_MAX_OVERSAMPLE - 1)];
         px[i]  = (uint8_t)(total / (unsigned)k);
      }
   }
}

/* The same down each column */
static INLINE void font_pack_prefilter_v(uint8_t *px, int w, int h, int stride,
      int k)
{
   int j;
   for (j = 0; j < w; j++, px++)
   {
      uint8_t  held[FONT_PACK_MAX_OVERSAMPLE] = {0};
      unsigned total                          = 0;
      int      i;
      for (i = 0; i <= h - k; i++)
      {
         total += (unsigned)(px[i * stride] - held[i & (FONT_PACK_MAX_OVERSAMPLE - 1)]);
         held[(i + k) & (FONT_PACK_MAX_OVERSAMPLE - 1)] = px[i * stride];
         px[i * stride] = (uint8_t)(total / (unsigned)k);
      }
      for (; i < h; i++)
      {
         total         -= held[i & (FONT_PACK_MAX_OVERSAMPLE - 1)];
         px[i * stride] = (uint8_t)(total / (unsigned)k);
      }
   }
}

/* Draws code points @first to @first + @count - 1 of @f into @pixels, a
 * @pw x @ph atlas, at @pixel_height pixels from the font's ascent to its
 * descent, oversampled @over times (1 to 8), and fills @out with where
 * each one is. A code point the font lacks gets its missing glyph,
 * drawn once and shared by the others it lacks.
 * @sc is the rasterizer's memory, kept by the caller. Returns 0 when the
 * glyphs did not all fit; those that fit are drawn and the rest of @out
 * is left as it was. */
static INLINE int font_pack_range(const rtt_font_t *f, rtt_scratch_t *sc,
      uint8_t *pixels, int pw, int ph, float pixel_height,
      uint32_t first, int count, int over, font_packed_char *out)
{
   const int   pad    = 1;
   const float recip  = 1.0f / (float)over;
   /* the shift that centres the filtered cell on the glyph */
   const float sub    = -(float)(over - 1) / (2.0f * (float)over);
   int         x      = 0, y = 0, bottom = 0, i, missing = -1;
   int         ascent = 0, descent = 0, line_gap = 0;
   float       scale, sx;

   memset(pixels, 0, (size_t)pw * (size_t)ph);
   rtt_vmetrics(f, &ascent, &descent, &line_gap);
   if (ascent == descent)
      return 0;
   scale = pixel_height / (float)(ascent - descent);
   sx    = scale * (float)over;

   for (i = 0; i < count; i++)
   {
      font_packed_char *b  = &out[i];
      int gi               = rtt_find_glyph(f, first + (uint32_t)i);
      int advance = 0, lsb = 0;
      int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      int has_box;
      int w, h;

      if (gi == 0)
      {
         if (missing >= 0)
         {
            *b = out[missing];
            continue;
         }
         missing = i;
      }

      if ((has_box = rtt_glyph_box(f, gi, &fx0, &fy0, &fx1, &fy1)))
      {
         x0 = (int)floor((float)fx0 * sx);
         y0 = (int)floor((float)-fy1 * sx);
         x1 = (int)ceil((float)fx1 * sx);
         y1 = (int)ceil((float)-fy0 * sx);
      }

      /* the cell, with the padding and the filter's spread */
      w = x1 - x0 + pad + over - 1;
      h = y1 - y0 + pad + over - 1;

      if (x + w > pw - pad)
      {
         x = 0;
         y = bottom;
      }
      if (y + h > ph - pad)
         return 0;

      b->x0 = (uint16_t)(x + pad);
      b->y0 = (uint16_t)(y + pad);
      b->x1 = (uint16_t)(x + w);
      b->y1 = (uint16_t)(y + h);

      if (has_box && x1 > x0 && y1 > y0)
      {
         uint8_t *cell = pixels + b->x0 + (size_t)b->y0 * (size_t)pw;
         rtt_render_glyph(f, sc, cell, x1 - x0, y1 - y0, pw, sx, sx,
               gi, fx0, fy1, 0);
      }
      if (over > 1)
      {
         uint8_t *cell = pixels + b->x0 + (size_t)b->y0 * (size_t)pw;
         font_pack_prefilter_h(cell, w - pad, h - pad, pw, over);
         font_pack_prefilter_v(cell, w - pad, h - pad, pw, over);
      }

      rtt_glyph_hmetrics(f, gi, &advance, &lsb);
      b->xadvance = scale * (float)advance;
      b->xoff     = (float)x0 * recip + sub;
      b->yoff     = (float)y0 * recip + sub;
      b->xoff2    = (float)(x0 + w - pad) * recip + sub;
      b->yoff2    = (float)(y0 + h - pad) * recip + sub;

      x += w;
      if (y + h > bottom)
         bottom = y + h;
   }
   return 1;
}

/* The quad of packed glyph @b with the pen at (*@xpos, *@ypos), which
 * moves on by its advance */
static INLINE void font_packed_quad(const font_packed_char *b, int pw, int ph,
      float *xpos, float *ypos, font_quad *q)
{
   const float ipw = 1.0f / (float)pw;
   const float iph = 1.0f / (float)ph;
   q->x0  = *xpos + b->xoff;
   q->y0  = *ypos + b->yoff;
   q->x1  = *xpos + b->xoff2;
   q->y1  = *ypos + b->yoff2;
   q->s0  = (float)b->x0 * ipw;
   q->t0  = (float)b->y0 * iph;
   q->s1  = (float)b->x1 * ipw;
   q->t1  = (float)b->y1 * iph;
   *xpos += b->xadvance;
}

#if defined(__cplusplus) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
