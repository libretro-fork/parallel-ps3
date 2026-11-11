/* The blit engine's scaled and converting copy. See rsx_scale.h. */

#include <stdlib.h>
#include <string.h>

#include <retro_endianness.h>
#include <gfx/scaler/scaler.h>

#include "rsx_scale.h"

struct rsx_scale
{
   struct scaler_ctx ctx;
   bool      ready;
   uint8_t  *in, *out;              /* host order copies, when needed */
   size_t    in_size, out_size;
};

struct rsx_scale *rsx_scale_new(void)
{
   return (struct rsx_scale*)calloc(1, sizeof(struct rsx_scale));
}

static bool grow(uint8_t **p, size_t *size, size_t want)
{
   uint8_t *q;
   if (*size >= want)
      return true;
   if (!(q = (uint8_t*)realloc(*p, want)))
      return false;
   *p    = q;
   *size = want;
   return true;
}

/* one row from the RSX's byte order to the host's, for the scaler */
static void row_in(uint8_t *dst, const uint8_t *src, int width, int bpp)
{
   int x;
   if (bpp == 2)
   {
      uint16_t *d = (uint16_t*)dst;
      for (x = 0; x < width; x++, src += 2)
         d[x] = (uint16_t)((src[0] << 8) | src[1]);
   }
   else
   {
      uint32_t *d = (uint32_t*)dst;
      for (x = 0; x < width; x++, src += 4)
         d[x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16)
              | ((uint32_t)src[2] << 8) | src[3];
   }
}

/* and back */
static void row_out(uint8_t *dst, const uint8_t *src, int width, int bpp)
{
   int x;
   if (bpp == 2)
   {
      const uint16_t *s = (const uint16_t*)src;
      for (x = 0; x < width; x++, dst += 2)
      {
         dst[0] = (uint8_t)(s[x] >> 8);
         dst[1] = (uint8_t)s[x];
      }
   }
   else
   {
      const uint32_t *s = (const uint32_t*)src;
      for (x = 0; x < width; x++, dst += 4)
      {
         dst[0] = (uint8_t)(s[x] >> 24);
         dst[1] = (uint8_t)(s[x] >> 16);
         dst[2] = (uint8_t)(s[x] >> 8);
         dst[3] = (uint8_t)s[x];
      }
   }
}

bool rsx_scale_image(struct rsx_scale *s,
      uint8_t *dst, enum rsx_scale_format dst_format,
      int dst_width, int dst_height, int dst_pitch,
      const uint8_t *src, enum rsx_scale_format src_format,
      int src_width, int src_height, int src_pitch, int src_slice_h,
      bool bilinear)
{
   int  ib = src_format == RSX_SCALE_R5G6B5 ? 2 : 4;
   int  ob = dst_format == RSX_SCALE_R5G6B5 ? 2 : 4;
   enum scaler_pix_fmt ifmt = ib == 2 ? SCALER_FMT_RGB565 : SCALER_FMT_ARGB8888;
   enum scaler_pix_fmt ofmt = ob == 2 ? SCALER_FMT_RGB565 : SCALER_FMT_ARGB8888;
   enum scaler_type    type = bilinear ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
   /* A8R8G8B8 to A8R8G8B8 is scaled byte by byte, each byte a channel,
    * whatever the scaler names them: those go as they lie. The rest
    * are put in the host's order around the scaler. */
   bool swap = is_little_endian() && !(ib == 4 && ob == 4);
   const uint8_t *in = src;
   uint8_t       *out = dst;
   int            in_pitch = src_pitch, out_pitch = dst_pitch, y;

   if (!s || dst_width <= 0 || dst_height <= 0 || src_width <= 0 || src_height <= 0)
      return false;
   if (src_slice_h <= 0)
      return true;
   if (src_slice_h > src_height)
      src_slice_h = src_height;

   if (!s->ready || s->ctx.in_width != src_width || s->ctx.in_height != src_height
         || s->ctx.out_width != dst_width || s->ctx.out_height != dst_height
         || s->ctx.in_fmt != ifmt || s->ctx.out_fmt != ofmt || s->ctx.scaler_type != type)
   {
      scaler_ctx_gen_reset(&s->ctx);
      memset(&s->ctx, 0, sizeof(s->ctx));
      s->ctx.in_width    = src_width;
      s->ctx.in_height   = src_height;
      s->ctx.out_width   = dst_width;
      s->ctx.out_height  = dst_height;
      s->ctx.in_fmt      = ifmt;
      s->ctx.out_fmt     = ofmt;
      s->ctx.scaler_type = type;
      s->ready           = scaler_ctx_gen_filter(&s->ctx);
      if (!s->ready)
         return false;
   }

   if (swap || src_slice_h < src_height)
   {
      if (!grow(&s->in, &s->in_size, (size_t)src_width * ib * src_height))
         return false;
      for (y = 0; y < src_height; y++)
      {
         const uint8_t *r = src + (size_t)(y < src_slice_h ? y : src_slice_h - 1) * src_pitch;
         uint8_t       *d = s->in + (size_t)y * src_width * ib;
         if (swap)
            row_in(d, r, src_width, ib);
         else
            memcpy(d, r, (size_t)src_width * ib);
      }
      in       = s->in;
      in_pitch = src_width * ib;
   }
   if (swap)
   {
      if (!grow(&s->out, &s->out_size, (size_t)dst_width * ob * dst_height))
         return false;
      out       = s->out;
      out_pitch = dst_width * ob;
   }

   s->ctx.in_stride  = in_pitch;
   s->ctx.out_stride = out_pitch;
   scaler_ctx_scale(&s->ctx, out, in);

   if (swap)
      for (y = 0; y < dst_height; y++)
         row_out(dst + (size_t)y * dst_pitch, s->out + (size_t)y * out_pitch, dst_width, ob);
   return true;
}

void rsx_scale_free(struct rsx_scale *s)
{
   if (!s)
      return;
   if (s->ready)
      scaler_ctx_gen_reset(&s->ctx);
   free(s->in);
   free(s->out);
   free(s);
}
