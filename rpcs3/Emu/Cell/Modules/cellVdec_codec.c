/* cellVdec's decoders. See cellVdec_codec.h. */

#include <stdlib.h>
#include <string.h>

#include <formats/rmpeg1_video.h>
#include <formats/rmpeg4_video.h>
#include <formats/rh264.h>
#include <formats/image_yuv_blit.h>

#include "cellVdec_codec.h"

/* Each unit's tag, coding type and skip mode wait here under a serial of
 * the unit's own, which is what the decoder carries through its
 * reordering; a decoder holds a few pictures at most */
#define VDC_UNITS 128

struct vdc_unit
{
   uint64_t serial;
   uint64_t tag;
   int      type;      /* AVC: the first slice's, read here */
   int      skip;
};

struct vdc
{
   int codec;
   rmpeg1_video_t *m2;
   rmpeg4_video_t *m4;
   rh264_video    *avc;

   struct vdc_unit unit[VDC_UNITS];
   uint64_t        serial;

   /* the unit being fed */
   const uint8_t *au;
   size_t         au_len, au_done;
   bool           drain, drained;
   int            avc_ready;        /* rh264 has a picture for vdc_next */

   /* MPEG-4: the frame rate from the time between pictures */
   uint32_t last_time;
   unsigned time_delta;
   bool     have_time;

   unsigned avc_w, avc_h;           /* rh264's last picture's size */

   unsigned errors;
};

struct vdc *vdc_new(int codec)
{
   struct vdc *d;
   if (codec != VDC_MPEG2 && codec != VDC_AVC && codec != VDC_MPEG4)
      return NULL;
   if (!(d = (struct vdc*)calloc(1, sizeof(*d))))
      return NULL;
   d->codec = codec;
   if (codec == VDC_MPEG2)
      d->m2 = rmpeg1_video_init();
   else if (codec == VDC_MPEG4)
      d->m4 = rmpeg4_video_init();
   else
      d->avc = rh264_video_open();
   if (!d->m2 && !d->m4 && !d->avc)
   {
      free(d);
      return NULL;
   }
   return d;
}

void vdc_free(struct vdc *d)
{
   if (!d)
      return;
   rmpeg1_video_free(d->m2);
   rmpeg4_video_free(d->m4);
   if (d->avc)
      rh264_video_close(d->avc);
   free(d);
}

void vdc_start(struct vdc *d)
{
   if (!d)
      return;
   if (d->m2)
      rmpeg1_video_reset(d->m2);
   if (d->m4)
      rmpeg4_video_reset(d->m4);
   if (d->avc)
   {
      /* a new decoder is the one way to drop what rh264 holds; this is
       * at a sequence's start, not during it */
      rh264_video_close(d->avc);
      d->avc = rh264_video_open();
   }
   d->au        = NULL;
   d->au_len    = d->au_done = 0;
   d->drain     = d->drained = false;
   d->avc_ready = 0;
   d->have_time = false;
   d->time_delta = 0;
}

/* The coding type of an AVC unit's first slice: its slice_type, the
 * second Exp-Golomb code of the slice header */
static int avc_type(const uint8_t *p, size_t len)
{
   size_t i;
   for (i = 0; i + 4 < len; i++)
   {
      if (p[i] || p[i + 1] || p[i + 2] != 1)
         continue;
      {
         unsigned nut = p[i + 3] & 0x1f;
         uint8_t  rbsp[16];
         size_t   n = 0, k = i + 4, zeros = 0;
         unsigned bit = 0, v, lead, st;
         int      field;
         if (nut != 1 && nut != 5)
            continue;
         /* the start of the header, emulation prevention taken out */
         while (k < len && n < sizeof(rbsp))
         {
            if (zeros >= 2 && p[k] == 3)
            {
               zeros = 0;
               k++;
               continue;
            }
            zeros = p[k] ? 0 : zeros + 1;
            rbsp[n++] = p[k++];
         }
         /* first_mb_in_slice, then slice_type */
         for (field = 0; field < 2; field++)
         {
            for (lead = 0; bit < n * 8 && !((rbsp[bit / 8] >> (7 - bit % 8)) & 1); bit++)
               lead++;
            bit++;
            for (v = 1; lead--; bit++)
               v = (v << 1) | (bit < n * 8 ? (rbsp[bit / 8] >> (7 - bit % 8)) & 1 : 0u);
            st = v - 1;
         }
         switch (st % 5)
         {
            case 2: case 4: return VDC_PIC_I;    /* I, SI */
            case 0: case 3: return VDC_PIC_P;    /* P, SP */
            default:        return VDC_PIC_B;
         }
      }
   }
   return VDC_PIC_UNKNOWN;
}

void vdc_feed(struct vdc *d, const uint8_t *au, size_t len, uint64_t tag, int skip)
{
   struct vdc_unit *u;
   if (!d)
      return;
   d->au        = au;
   d->au_len    = au ? len : 0;
   d->au_done   = 0;
   d->avc_ready = 0;
   if (!au)
   {
      d->drain   = true;
      d->drained = false;
      return;
   }
   d->drain = false;
   d->serial++;
   u         = &d->unit[d->serial % VDC_UNITS];
   u->serial = d->serial;
   u->tag    = tag;
   u->skip   = skip;
   u->type   = d->codec == VDC_AVC ? avc_type(au, len) : VDC_PIC_UNKNOWN;

   if (d->codec == VDC_AVC)
   {
      int r;
      rh264_video_set_skip_nonref(d->avc, skip != VDC_SKIP_NONE);
      rh264_video_set_tag(d->avc, d->serial);
      r = rh264_video_decode(d->avc, au, len);
      if (r < 0)
         d->errors++;
      d->avc_ready = r == 1;
      d->au_done   = len;
   }
   else if (d->codec == VDC_MPEG2)
      rmpeg1_video_set_tag(d->m2, d->serial);
   else
      rmpeg4_video_set_tag(d->m4, d->serial);
}

static unsigned half(unsigned v)
{
   return (v + 1) / 2;
}

/* a picture's planes into @pic, tight */
static bool copy_out(struct vdc_pic *pic, const uint8_t *y, unsigned ys,
      const uint8_t *cb, const uint8_t *cr, unsigned cs, unsigned w, unsigned h)
{
   unsigned cw = half(w), ch = half(h), j;
   size_t   need = (size_t)w * h + 2 * (size_t)cw * ch;
   uint8_t *p;
   if (pic->cap < need)
   {
      uint8_t *m = (uint8_t*)realloc(pic->mem, need);
      if (!m)
         return false;
      pic->mem = m;
      pic->cap = need;
   }
   p = pic->mem;
   for (j = 0; j < h; j++, p += w)
      memcpy(p, y + (size_t)j * ys, w);
   for (j = 0; j < ch; j++, p += cw)
      memcpy(p, cb + (size_t)j * cs, cw);
   for (j = 0; j < ch; j++, p += cw)
      memcpy(p, cr + (size_t)j * cs, cw);
   pic->width  = w;
   pic->height = h;
   return true;
}

/* The unit a picture's serial names, if it is still here */
static const struct vdc_unit *unit_of(const struct vdc *d, uint64_t serial)
{
   const struct vdc_unit *u = &d->unit[serial % VDC_UNITS];
   return u->serial == serial && serial ? u : NULL;
}

static bool dropped(int skip, int type)
{
   return (skip == VDC_SKIP_B && type == VDC_PIC_B)
       || (skip == VDC_SKIP_PB && type != VDC_PIC_I);
}

/* One picture from the decoder, its planes still the decoder's */
typedef struct
{
   const uint8_t *y, *cb, *cr;
   unsigned       ys, cs, w, h;
   int            type, interlaced;
   uint64_t       serial;
} vdc_raw_t;

static int raw_next(struct vdc *d, vdc_raw_t *r)
{
   memset(r, 0, sizeof(*r));
   if (d->codec == VDC_AVC)
   {
      int ys, cs, w, h, cw, ch;
      if (!d->avc_ready)
      {
         if (!d->drain || d->drained)
            return 0;
         if (rh264_video_drain(d->avc) != 0)
         {
            d->drained = true;
            return 0;
         }
      }
      d->avc_ready = 0;
      if (rh264_video_bit_depth(d->avc) != 8)
      {
         d->errors++;
         return 0;
      }
      r->y  = rh264_video_plane(d->avc, 0, &ys, &w, &h);
      r->cb = rh264_video_plane(d->avc, 1, &cs, &cw, &ch);
      r->cr = rh264_video_plane(d->avc, 2, &cs, &cw, &ch);
      if (!r->y || !r->cb || !r->cr || w <= 0 || h <= 0)
         return 0;
      d->avc_w  = (unsigned)w;
      d->avc_h  = (unsigned)h;
      r->ys     = (unsigned)ys;
      r->cs     = (unsigned)cs;
      r->w      = (unsigned)w;
      r->h      = (unsigned)h;
      r->serial = rh264_video_tag(d->avc);
      {
         const struct vdc_unit *u = unit_of(d, r->serial);
         r->type = u ? u->type : VDC_PIC_UNKNOWN;
      }
      return 1;
   }
   if (d->codec == VDC_MPEG4)
   {
      rmpeg4_video_frame_t f;
      int got = 0;
      while (d->au_done < d->au_len)
      {
         size_t used = 0;
         got = rmpeg4_video_decode(d->m4, d->au + d->au_done, d->au_len - d->au_done, &used, &f);
         d->au_done += used;
         if (got)
            break;
         if (!used)
         {
            d->au_done = d->au_len;
            break;
         }
      }
      if (!got && d->drain && !d->drained)
      {
         got = rmpeg4_video_flush(d->m4, &f);
         if (!got)
            d->drained = true;
      }
      if (!got)
         return 0;
      if (d->have_time && f.time != d->last_time)
         d->time_delta = f.time - d->last_time;
      d->last_time = f.time;
      d->have_time = true;
      r->y = f.y; r->cb = f.cb; r->cr = f.cr;
      r->ys = f.y_stride; r->cs = f.c_stride; r->w = f.width; r->h = f.height;
      r->type = f.coding_type == 3 ? VDC_PIC_B : f.coding_type == 1 ? VDC_PIC_I : VDC_PIC_P;
      r->interlaced = f.interlaced;
      r->serial = f.tag;
      return 1;
   }
   /* MPEG-2: the unit is written as the window takes it */
   {
      rmpeg1_video_frame_t f;
      for (;;)
      {
         if (rmpeg1_video_decode(d->m2, &f))
            break;
         if (d->au_done < d->au_len)
         {
            size_t n = rmpeg1_video_write(d->m2, d->au + d->au_done, d->au_len - d->au_done);
            if (!n)
            {
               d->errors++;          /* a unit the window cannot hold */
               d->au_done = d->au_len;
            }
            d->au_done += n;
            continue;
         }
         if (d->drain && !d->drained)
         {
            rmpeg1_video_flush(d->m2);
            d->drained = true;
            continue;
         }
         return 0;
      }
      r->y = f.y; r->cb = f.cb; r->cr = f.cr;
      r->ys = f.y_stride; r->cs = f.c_stride; r->w = f.width; r->h = f.height;
      r->type = f.coding_type == 3 ? VDC_PIC_B : f.coding_type == 2 ? VDC_PIC_P : VDC_PIC_I;
      r->serial = f.tag;
      return 1;
   }
}

int vdc_next(struct vdc *d, struct vdc_pic *pic)
{
   vdc_raw_t r;
   if (!d || !pic)
      return 0;
   while (raw_next(d, &r))
   {
      const struct vdc_unit *u = unit_of(d, r.serial);
      if (u && dropped(u->skip, r.type))
         continue;
      if (!copy_out(pic, r.y, r.ys, r.cb, r.cr, r.cs, r.w, r.h))
      {
         d->errors++;
         continue;
      }
      pic->type       = r.type;
      pic->interlaced = r.interlaced;
      pic->tag        = u ? u->tag : 0;
      return 1;
   }
   return 0;
}

int vdc_framerate(const struct vdc *d, unsigned *num, unsigned *den)
{
   unsigned n = 0, m = 0;
   if (!d)
      return -1;
   if (d->codec == VDC_MPEG2)
   {
      if (!rmpeg1_video_has_sequence(d->m2))
         return -1;
      rmpeg1_video_framerate(d->m2, &n, &m);
   }
   else if (d->codec == VDC_AVC)
   {
      if (rh264_video_framerate(d->avc, &n, &m) != 0)
         return -1;
   }
   else
   {
      n = rmpeg4_video_time_resolution(d->m4);
      m = d->time_delta;
   }
   if (!n || !m)
      return -1;
   *num = n;
   *den = m;
   return 0;
}

size_t vdc_picture_size(const struct vdc *d)
{
   unsigned w = 0, h = 0;
   if (!d)
      return 0;
   if (d->codec == VDC_MPEG2)
   {
      w = rmpeg1_video_width(d->m2);
      h = rmpeg1_video_height(d->m2);
   }
   else if (d->codec == VDC_MPEG4)
   {
      w = rmpeg4_video_width(d->m4);
      h = rmpeg4_video_height(d->m4);
   }
   else
   {
      w = d->avc_w;
      h = d->avc_h;
   }
   return (size_t)w * h + 2 * (size_t)half(w) * half(h);
}

unsigned vdc_errors(const struct vdc *d)
{
   return d ? d->errors : 0;
}

void vdc_pic_free(struct vdc_pic *pic)
{
   if (!pic)
      return;
   free(pic->mem);
   pic->mem = NULL;
   pic->cap = 0;
}

bool vdc_convert(uint8_t *dst, const struct vdc_pic *pic, int format,
      uint8_t alpha, bool bt709)
{
   unsigned       w = pic->width, h = pic->height, cw = half(w), ch = half(h);
   const uint8_t *y  = pic->mem;
   const uint8_t *cb = y + (size_t)w * h;
   const uint8_t *cr = cb + (size_t)cw * ch;
   switch (format)
   {
      case VDC_OUT_ARGB:
      case VDC_OUT_RGBA:
         image_yuv_420_to_bytes(dst, (size_t)w * 4, y, w, cb, cw, cr, cw, 1, w, h,
               bt709 ? IMAGE_YUV_FLAG_BT709 : 0,
               format == VDC_OUT_ARGB ? IMAGE_YUV_BYTES_ARGB : IMAGE_YUV_BYTES_RGBA, alpha);
         return true;
      case VDC_OUT_UYVY:
         image_yuv_420_to_uyvy(dst, (size_t)half(w) * 4, y, w, cb, cw, cr, cw, w, h);
         return true;
      case VDC_OUT_YUV420:
         memcpy(dst, pic->mem, (size_t)w * h + 2 * (size_t)cw * ch);
         return true;
      default:
         return false;
   }
}
