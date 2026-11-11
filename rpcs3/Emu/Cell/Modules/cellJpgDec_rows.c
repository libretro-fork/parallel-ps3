/* cellJpgDec's decode: see cellJpgDec_rows.h. C89. */

#include <string.h>

#include "cellJpgDec_rows.h"

/* Where the rows go, set up once the size is known */
struct jdr_state
{
   uint8_t *out;
   size_t   bytes_per_line;
   size_t   stride;       /* from one row to the next */
   size_t   line;         /* bytes written of each */
   size_t   line_pixels;  /* whole pixels of them */
   unsigned height;
   unsigned n;           /* bytes a pixel, 0: nothing written */
   int      space;
   bool     flip;
   bool     in_place;    /* RGBA rows decoded where they go */
};

unsigned jdr_components(int space)
{
   switch (space)
   {
      case JDR_CS_RGBA:
      case JDR_CS_ARGB:
         return 4;
      case JDR_CS_RGB:
         return 3;
      default:
         break;
   }
   return 0;
}

static uint8_t *jdr_dst(const struct jdr_state *s, unsigned y)
{
   return s->out + s->stride * (s->flip ? s->height - 1 - y : y);
}

static bool jdr_size(void *user, unsigned width, unsigned height)
{
   struct jdr_state *s = (struct jdr_state*)user;
   size_t row_bytes    = (size_t)width * s->n;
   bool   padded       = s->bytes_per_line > row_bytes || s->flip;

   s->height      = height;
   s->stride      = padded ? s->bytes_per_line : row_bytes;
   s->line        = padded && s->bytes_per_line < row_bytes
      ? s->bytes_per_line : row_bytes;
   s->line_pixels = s->n ? s->line / s->n : 0;
   /* The decoder writes width * 4 bytes, 4-byte aligned */
   s->in_place    = s->space == JDR_CS_RGBA && s->line == row_bytes
      && !(((size_t)s->out | s->stride) & 3);
   return true;
}

static uint8_t *jdr_row(void *user, unsigned y)
{
   struct jdr_state *s = (struct jdr_state*)user;
   return s->in_place ? jdr_dst(s, y) : NULL;
}

static void jdr_done(void *user, unsigned y, const uint8_t *px)
{
   struct jdr_state *s = (struct jdr_state*)user;
   uint8_t *d;
   size_t   x, rest;

   if (s->in_place || !s->n)
      return;

   d = jdr_dst(s, y);
   switch (s->space)
   {
      case JDR_CS_RGBA:
         memcpy(d, px, s->line);
         return;
      case JDR_CS_RGB:
         for (x = 0; x < s->line_pixels; x++, d += 3, px += 4)
         {
            d[0] = px[0];
            d[1] = px[1];
            d[2] = px[2];
         }
         break;
      case JDR_CS_ARGB:
         for (x = 0; x < s->line_pixels; x++, d += 4, px += 4)
         {
            d[0] = px[3];
            d[1] = px[0];
            d[2] = px[1];
            d[3] = px[2];
         }
         break;
      default:
         return;
   }

   /* A line cut inside a pixel takes that pixel's first bytes */
   rest = s->line - s->line_pixels * s->n;
   if (rest)
   {
      uint8_t p[4];
      if (s->space == JDR_CS_ARGB)
      {
         p[0] = px[3];
         p[1] = px[0];
         p[2] = px[1];
      }
      else
      {
         p[0] = px[0];
         p[1] = px[1];
         p[2] = px[2];
      }
      memcpy(d, p, rest);
   }
}

bool jdr_decode(rjpeg_t *dec, const uint8_t *jpg, size_t len,
      uint8_t *out, size_t bytes_per_line, bool bottom_to_top, int space,
      unsigned *width, unsigned *height)
{
   struct jdr_state  s;
   struct rjpeg_rows rows;

   memset(&s, 0, sizeof(s));
   s.out            = out;
   s.bytes_per_line = bytes_per_line;
   s.n              = jdr_components(space);
   s.space          = space;
   s.flip           = bottom_to_top;

   rows.size = jdr_size;
   rows.row  = jdr_row;
   rows.done = jdr_done;
   rows.user = &s;

   /* R, G, B, A in memory */
   return rjpeg_decode_rows(dec, jpg, len, true, &rows, width, height);
}
