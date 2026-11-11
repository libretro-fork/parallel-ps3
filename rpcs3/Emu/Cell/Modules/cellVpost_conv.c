/* cellVpost's picture conversion. See cellVpost_conv.h. */

#include <stdlib.h>
#include <string.h>

#include <formats/image_yuv_blit.h>
#include <gfx/scaler/scaler.h>

#include "cellVpost_conv.h"

struct vpost_conv
{
   struct scaler_ctx ctx;
   bool      ready;
   uint8_t  *rgba;                  /* the picture at its own size */
   size_t    rgba_size;
};

struct vpost_conv *vpost_conv_new(void)
{
   return (struct vpost_conv*)calloc(1, sizeof(struct vpost_conv));
}

void vpost_conv_free(struct vpost_conv *c)
{
   if (!c)
      return;
   if (c->ready)
      scaler_ctx_gen_reset(&c->ctx);
   free(c->rgba);
   free(c);
}

bool vpost_conv_exec(struct vpost_conv *c, uint8_t *out, unsigned out_w, unsigned out_h,
      const uint8_t *in, unsigned in_w, unsigned in_h, bool full_range, bool bt709,
      uint8_t alpha)
{
   const uint8_t *y  = in;
   const uint8_t *cb = in + (size_t)in_w * in_h;
   const uint8_t *cr = cb + (size_t)(in_w / 2) * (in_h / 2);
   unsigned flags    = (full_range ? IMAGE_YUV_FLAG_FULL_RANGE : 0)
                     | (bt709 ? IMAGE_YUV_FLAG_BT709 : 0);
   uint8_t *dst      = out;

   if (!c || !out || !in || !in_w || !in_h || !out_w || !out_h)
      return false;

   if (out_w != in_w || out_h != in_h)
   {
      size_t want = (size_t)in_w * in_h * 4;
      /* the scaler first, so a failure leaves nothing half done */
      if (!c->ready || c->ctx.in_width != (int)in_w || c->ctx.in_height != (int)in_h
            || c->ctx.out_width != (int)out_w || c->ctx.out_height != (int)out_h)
      {
         scaler_ctx_gen_reset(&c->ctx);
         memset(&c->ctx, 0, sizeof(c->ctx));
         /* RGBA scaled as ARGB8888: each byte a channel, filtered the
          * same whatever the scaler names it */
         c->ctx.in_fmt      = SCALER_FMT_ARGB8888;
         c->ctx.out_fmt     = SCALER_FMT_ARGB8888;
         /* bilinear up; down, two taps would skip samples, and the
          * scaler's windowed sinc widens with the ratio */
         c->ctx.scaler_type = out_w < in_w || out_h < in_h
            ? SCALER_TYPE_SINC : SCALER_TYPE_BILINEAR;
         c->ctx.in_width    = (int)in_w;
         c->ctx.in_height   = (int)in_h;
         c->ctx.out_width   = (int)out_w;
         c->ctx.out_height  = (int)out_h;
         if (!(c->ready = scaler_ctx_gen_filter(&c->ctx)))
            return false;
      }
      if (c->rgba_size < want)
      {
         uint8_t *p = (uint8_t*)realloc(c->rgba, want);
         if (!p)
            return false;
         c->rgba      = p;
         c->rgba_size = want;
      }
      dst = c->rgba;
   }

   image_yuv_420_to_bytes(dst, (size_t)in_w * 4, y, in_w, cb, in_w / 2, cr, in_w / 2,
         1, in_w, in_h, flags, IMAGE_YUV_BYTES_RGBA, alpha);

   if (dst != out)
   {
      c->ctx.in_stride  = (int)in_w * 4;
      c->ctx.out_stride = (int)out_w * 4;
      scaler_ctx_scale(&c->ctx, out, dst);
      /* the filter's fixed point can take a constant alpha one down */
      {
         size_t i, n = (size_t)out_w * out_h * 4;
         for (i = 3; i < n; i += 4)
            out[i] = alpha;
      }
   }
   return true;
}
