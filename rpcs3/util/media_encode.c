/* What cellRec records, on libretro-common: see media_encode.h. C89. */

#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include <gfx/scaler/scaler.h>
#include <formats/image_yuv_blit.h>
#include <formats/rmpeg4_encode.h>
#include <formats/rh264_encode.h>
#include <formats/rjpeg_encode.h>
#include <formats/raac_encode.h>
#include <formats/rmp4_mux.h>
#include <audio/conversion/float_to_ulaw.h>

#include "media_encode.h"

/* PCM goes into the file in packets of this many frames, as AAC does */
#define ME_PACKET RAAC_ENCODE_FRAME

struct me_encoder
{
   struct me_params  p;
   RFILE            *file;
   rmp4_mux_t       *mux;
   bool              failed;

   /* video */
   rmpeg4_encoder_t *mpeg4;
   rh264_encoder_t  *h264;
   rjpeg_encoder_t  *jpeg;
   unsigned          jpeg_quality;
   uint8_t          *vout;          /* one encoded frame */
   size_t            vout_cap;
   uint8_t          *yuv;           /* the frame at the recording's size, 4:2:0 */
   uint8_t          *pixels;        /* scaled, four bytes a pixel */
   int64_t           last_pts;
   bool              any_frame;

   /* shrinking: a box average to under twice the size, then the scaler */
   uint8_t          *box;
   size_t            box_cap;
   unsigned          box_fx, box_fy;
   struct scaler_ctx scaler;
   unsigned          scaler_w, scaler_h;
   bool              scaler_ready;

   /* audio */
   raac_encoder_t   *aac;
   float            *pcm;           /* a packet of frames being filled */
   size_t            pcm_fill;
   uint8_t          *aout;          /* one encoded packet */
   size_t            aout_cap;
   int64_t           audio_frames;  /* frames packed so far */
   int64_t           aac_packets;   /* packets out of the encoder */
};

/* ---------------------------------------------------------------- */
/* the file                                                         */
/* ---------------------------------------------------------------- */

static bool me_write(void *user, const void *data, size_t len)
{
   return filestream_write((RFILE*)user, data, (int64_t)len) == (int64_t)len;
}

static bool me_seek(void *user, uint64_t offset)
{
   return filestream_seek((RFILE*)user, (int64_t)offset,
         RETRO_VFS_SEEK_POSITION_START) == 0;
}

static void me_free(struct me_encoder *e)
{
   if (e->mpeg4)
      rmpeg4_encoder_free(e->mpeg4);
   if (e->h264)
      rh264_encoder_free(e->h264);
   if (e->aac)
      raac_encoder_free(e->aac);
   if (e->scaler_ready)
      scaler_ctx_gen_reset(&e->scaler);
   free(e->jpeg);
   free(e->vout);
   free(e->yuv);
   free(e->pixels);
   free(e->box);
   free(e->pcm);
   free(e->aout);
   free(e);
}

struct me_encoder *me_open(const struct me_params *p)
{
   struct me_encoder *e;
   rmp4_mux_track_t   t[2];
   struct rmp4_mux_io io;
   uint8_t            config[256];
   size_t             config_len = 0;
   uint8_t            asc[8];
   size_t             asc_len = 0;
   size_t             luma;

   if (!p || !p->path || !p->width || !p->height || (p->width | p->height) & 1
         || p->width > 4096 || p->height > 4096 || !p->fps || !p->max_seconds
         || p->video_codec < ME_VIDEO_MPEG4 || p->video_codec > ME_VIDEO_MJPEG
         || p->audio_codec < ME_AUDIO_AAC || p->audio_codec > ME_AUDIO_PCM_F32BE
         || (p->channels != 1 && p->channels != 2)
         || p->sample_rate < 8000 || p->sample_rate > 96000)
      return NULL;
   if (!(e = (struct me_encoder*)calloc(1, sizeof(*e))))
      return NULL;
   e->p      = *p;
   e->p.path = NULL;   /* not kept */
   luma      = (size_t)p->width * p->height;

   /* the video encoder, and its decoder configuration */
   switch (p->video_codec)
   {
      case ME_VIDEO_MPEG4:
         {
            rmpeg4_encoder_config_t c;
            memset(&c, 0, sizeof(c));
            c.width   = p->width;
            c.height  = p->height;
            c.fps_num = p->fps;
            c.fps_den = 1;
            c.bitrate = p->video_bitrate;
            c.gop     = p->gop;
            if (!(e->mpeg4 = rmpeg4_encoder_new(&c))
                  || !(config_len = rmpeg4_encoder_header(e->mpeg4, config, sizeof(config))))
               goto error;
            e->vout_cap = rmpeg4_encoder_bound(e->mpeg4);
         }
         break;
      case ME_VIDEO_H264:
         {
            rh264_encoder_config_t c;
            memset(&c, 0, sizeof(c));
            c.width   = p->width;
            c.height  = p->height;
            c.fps_num = p->fps;
            c.fps_den = 1;
            c.bitrate = p->video_bitrate;
            c.gop     = p->gop;
            if (!(e->h264 = rh264_encoder_new(&c))
                  || !(config_len = rh264_encoder_avcc(e->h264, config, sizeof(config))))
               goto error;
            e->vout_cap = rh264_encoder_bound(e->h264);
         }
         break;
      default:
         /* MJPEG: the quality follows the bit rate, from the usual 75 */
         if (!(e->jpeg = (rjpeg_encoder_t*)malloc(sizeof(*e->jpeg))))
            goto error;
         e->jpeg_quality = 75;
         rjpeg_encoder_init(e->jpeg, e->jpeg_quality);
         e->vout_cap = rjpeg_encode_bound(p->width, p->height);
         break;
   }

   /* the audio encoder */
   if (p->audio_codec == ME_AUDIO_AAC)
   {
      raac_encoder_config_t c;
      c.sample_rate = p->sample_rate;
      c.channels    = p->channels;
      c.bitrate     = p->audio_bitrate;
      if (!(e->aac = raac_encoder_new(&c))
            || !(asc_len = raac_encoder_asc(e->aac, asc, sizeof(asc))))
         goto error;
      e->aout_cap = RAAC_ENCODE_MAX_FRAME;
   }
   else
      e->aout_cap = (size_t)ME_PACKET * p->channels * 4;

   e->vout   = (uint8_t*)malloc(e->vout_cap);
   e->yuv    = (uint8_t*)malloc(luma + luma / 2);
   e->pixels = (uint8_t*)malloc(luma * 4);
   e->pcm    = (float*)malloc(sizeof(float) * ME_PACKET * p->channels);
   e->aout   = (uint8_t*)malloc(e->aout_cap);
   if (!e->vout || !e->yuv || !e->pixels || !e->pcm || !e->aout)
      goto error;

   memset(t, 0, sizeof(t));
   t[0].codec       = p->video_codec == ME_VIDEO_MPEG4 ? RMP4_MUX_MPEG4_VIDEO
                    : p->video_codec == ME_VIDEO_H264  ? RMP4_MUX_H264 : RMP4_MUX_MJPEG;
   t[0].timescale   = p->fps;
   t[0].width       = p->width;
   t[0].height      = p->height;
   t[0].config      = config_len ? config : NULL;
   t[0].config_len  = config_len;
   t[0].max_samples = p->fps * p->max_seconds;
   t[0].bitrate     = p->video_bitrate;
   t[1].codec       = p->audio_codec == ME_AUDIO_AAC  ? RMP4_MUX_AAC
                    : p->audio_codec == ME_AUDIO_ULAW ? RMP4_MUX_ULAW : RMP4_MUX_PCM_F32BE;
   t[1].timescale   = p->sample_rate;
   t[1].sample_rate = p->sample_rate;
   t[1].channels    = p->channels;
   t[1].config      = asc_len ? asc : NULL;
   t[1].config_len  = asc_len;
   /* AAC's priming packet, not shown */
   t[1].skip        = asc_len ? ME_PACKET : 0;
   /* a packet a call, and the encoder's last */
   t[1].max_samples = (uint32_t)((uint64_t)p->sample_rate * p->max_seconds / ME_PACKET + 2);
   t[1].bitrate     = p->audio_codec == ME_AUDIO_AAC ? p->audio_bitrate
                    : p->sample_rate * p->channels * (p->audio_codec == ME_AUDIO_ULAW ? 8 : 32);

   if (!(e->file = filestream_open(p->path, RETRO_VFS_FILE_ACCESS_WRITE,
               RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      goto error;
   io.write = me_write;
   io.seek  = me_seek;
   io.user  = e->file;
   if (!(e->mux = rmp4_mux_open(&io, t, 2)))
   {
      filestream_close(e->file);
      e->file = NULL;
      filestream_delete(p->path);
      goto error;
   }
   return e;

error:
   me_free(e);
   return NULL;
}

/* ---------------------------------------------------------------- */
/* video                                                            */
/* ---------------------------------------------------------------- */

/* @src shrunk by whole factors, each output pixel the average of the
 * fx * fy it covers, channel by channel */
static void me_box(uint8_t *dst, const uint8_t *src, size_t pitch,
      unsigned w, unsigned h, unsigned fx, unsigned fy)
{
   unsigned x, y, i, j, c, n = fx * fy;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++, dst += 4)
         for (c = 0; c < 4; c++)
         {
            unsigned sum = 0;
            for (j = 0; j < fy; j++)
            {
               const uint8_t *s = src + ((size_t)y * fy + j) * pitch + (size_t)x * fx * 4 + c;
               for (i = 0; i < fx; i++)
                  sum += s[i * 4];
            }
            dst[c] = (uint8_t)((sum + n / 2) / n);
         }
}

/* A frame of four-byte pixels to the recording's size, in e->pixels,
 * or in place when it is that size already: the pixels to convert */
static const uint8_t *me_scale(struct me_encoder *e, const uint8_t *data,
      size_t pitch, unsigned w, unsigned h, size_t *out_pitch)
{
   unsigned fx = w / e->p.width, fy = h / e->p.height, bw, bh;
   if (w == e->p.width && h == e->p.height)
   {
      *out_pitch = pitch;
      return data;
   }
   if (!fx)
      fx = 1;
   if (!fy)
      fy = 1;
   bw = w / fx;
   bh = h / fy;
   if (fx > 1 || fy > 1)
   {
      size_t need = (size_t)bw * bh * 4;
      if (need > e->box_cap)
      {
         /* a new input size: not per frame */
         free(e->box);
         if (!(e->box = (uint8_t*)malloc(need)))
         {
            e->box_cap = 0;
            return NULL;
         }
         e->box_cap = need;
      }
      me_box(e->box, data, pitch, bw, bh, fx, fy);
      data  = e->box;
      pitch = (size_t)bw * 4;
   }
   *out_pitch = (size_t)e->p.width * 4;
   if (bw == e->p.width && bh == e->p.height)
   {
      /* the average was the whole of it */
      memcpy(e->pixels, data, (size_t)bw * bh * 4);
      return e->pixels;
   }
   if (!e->scaler_ready || e->scaler_w != bw || e->scaler_h != bh)
   {
      if (e->scaler_ready)
         scaler_ctx_gen_reset(&e->scaler);
      memset(&e->scaler, 0, sizeof(e->scaler));
      /* the four bytes filtered alike, whatever their order */
      e->scaler.in_width    = (int)bw;
      e->scaler.in_height   = (int)bh;
      e->scaler.in_stride   = (int)bw * 4;
      e->scaler.out_width   = (int)e->p.width;
      e->scaler.out_height  = (int)e->p.height;
      e->scaler.out_stride  = (int)e->p.width * 4;
      e->scaler.in_fmt      = SCALER_FMT_ARGB8888;
      e->scaler.out_fmt     = SCALER_FMT_ARGB8888;
      e->scaler.scaler_type = SCALER_TYPE_SINC;
      if (!(e->scaler_ready = scaler_ctx_gen_filter(&e->scaler)))
         return NULL;
      e->scaler_w = bw;
      e->scaler_h = bh;
   }
   e->scaler.in_stride = (int)pitch;
   scaler_ctx_scale(&e->scaler, e->pixels, data);
   return e->pixels;
}

bool me_video(struct me_encoder *e, const uint8_t *data, size_t pitch,
      unsigned w, unsigned h, int format, int64_t pts)
{
   unsigned       W, H, cw;
   const uint8_t *y, *cb, *cr;
   size_t         ys, cs, n = 0;
   bool           key = true;

   if (!e || e->failed)
      return false;
   if (!data || !w || !h || pts < 0 || (e->any_frame && pts <= e->last_pts))
      return true;   /* nothing to show, or not after the last */
   W  = e->p.width;
   H  = e->p.height;
   cw = W / 2;

   if (format == ME_PIXEL_YUV420P)
   {
      if (w != W || h != H)
         return true;   /* only ever the recording's size */
      y  = data;
      cb = data + (size_t)W * H;
      cr = cb + (size_t)cw * (H / 2);
      ys = W;
      cs = cw;
   }
   else
   {
      static const unsigned orders[3] = {
         IMAGE_YUV_BYTES_RGBA, IMAGE_YUV_BYTES_BGRA, IMAGE_YUV_BYTES_ARGB };
      const uint8_t *px;
      size_t         pp;
      if (format < ME_PIXEL_RGBA || format > ME_PIXEL_ARGB || pitch < (size_t)w * 4
            || (pitch & 3) || ((size_t)data & 3))
         return true;
      if (!(px = me_scale(e, data, pitch, w, h, &pp)))
      {
         e->failed = true;
         return false;
      }
      y  = e->yuv;
      cb = e->yuv + (size_t)W * H;
      cr = cb + (size_t)cw * (H / 2);
      ys = W;
      cs = cw;
      image_yuv_420_from_bytes(e->yuv, W, e->yuv + (size_t)W * H, cw,
            e->yuv + (size_t)W * H + (size_t)cw * (H / 2), cw, 1,
            px, pp, W, H, 0, orders[format]);
   }

   if (e->mpeg4)
      n = rmpeg4_encoder_encode(e->mpeg4, e->vout, e->vout_cap, y, ys, cb, cs, cr, cs, &key);
   else if (e->h264)
      n = rh264_encoder_encode(e->h264, e->vout, e->vout_cap, y, ys, cb, cs, cr, cs, false, &key);
   else
   {
      /* the quality steered toward the bit rate's share of a frame */
      size_t target = e->p.video_bitrate / 8 / e->p.fps;
      n = rjpeg_encode(e->jpeg, e->vout, e->vout_cap, W, H, RJPEG_ENCODE_420,
            y, ys, cb, cs, cr, cs);
      if (target && n)
      {
         unsigned q = e->jpeg_quality;
         if (n > target + target / 8)
            q = q > 2 + (n > 2 * target ? 8 : 2) ? q - (n > 2 * target ? 8 : 2) : 2;
         else if (n < target - target / 8)
            q = q + (n * 2 < target ? 8 : 2) < 95 ? q + (n * 2 < target ? 8 : 2) : 95;
         if (q != e->jpeg_quality)
         {
            e->jpeg_quality = q;
            rjpeg_encoder_init(e->jpeg, q);
         }
      }
   }
   /* the first frame shows from the start, whatever its time */
   if (!n || !rmp4_mux_write(e->mux, 0, e->vout, n, e->any_frame ? pts : 0,
            e->any_frame ? pts : 0, key))
   {
      e->failed = true;
      return false;
   }
   e->last_pts  = pts;
   e->any_frame = true;
   return true;
}

/* ---------------------------------------------------------------- */
/* audio                                                            */
/* ---------------------------------------------------------------- */

static bool me_packet(struct me_encoder *e, const float *pcm)
{
   size_t n, i, count = (size_t)ME_PACKET * e->p.channels;
   if (e->aac)
   {
      /* the encoder is a packet behind: its first is the priming one,
       * which the edit list skips */
      if (!(n = raac_encoder_encode_f32(e->aac, pcm, e->aout, e->aout_cap, false)))
         return false;
      if (!rmp4_mux_write(e->mux, 1, e->aout, n,
               e->aac_packets * ME_PACKET, e->aac_packets * ME_PACKET, true))
         return false;
      e->aac_packets++;
      return true;
   }
   if (e->p.audio_codec == ME_AUDIO_ULAW)
   {
      convert_float_to_ulaw(e->aout, pcm, count);
      n = count;
   }
   else
   {
      /* big-endian floats: the bytes of each, swapped on a
       * little-endian host */
      for (i = 0; i < count; i++)
      {
         union { float f; uint32_t u; } v;
         v.f = pcm[i];
         e->aout[i * 4]     = (uint8_t)(v.u >> 24);
         e->aout[i * 4 + 1] = (uint8_t)(v.u >> 16);
         e->aout[i * 4 + 2] = (uint8_t)(v.u >> 8);
         e->aout[i * 4 + 3] = (uint8_t)v.u;
      }
      n = count * 4;
   }
   return rmp4_mux_write(e->mux, 1, e->aout, n, e->audio_frames - ME_PACKET,
         e->audio_frames - ME_PACKET, true);
}

bool me_audio(struct me_encoder *e, const float *pcm, size_t frames)
{
   unsigned ch;
   if (!e || e->failed)
      return false;
   ch = e->p.channels;
   while (frames)
   {
      size_t take = ME_PACKET - e->pcm_fill;
      if (take > frames)
         take = frames;
      if (pcm)
      {
         memcpy(e->pcm + e->pcm_fill * ch, pcm, take * ch * sizeof(float));
         pcm += take * ch;
      }
      else
         memset(e->pcm + e->pcm_fill * ch, 0, take * ch * sizeof(float));
      e->pcm_fill     += take;
      e->audio_frames += (int64_t)take;
      frames          -= take;
      if (e->pcm_fill == ME_PACKET)
      {
         e->pcm_fill = 0;
         if (!me_packet(e, e->pcm))
         {
            e->failed = true;
            return false;
         }
      }
   }
   return true;
}

bool me_close(struct me_encoder *e)
{
   bool ok;
   if (!e)
      return false;
   ok = !e->failed;
   if (ok && e->pcm_fill)
   {
      /* the last packet, filled out with silence */
      size_t left = ME_PACKET - e->pcm_fill;
      ok = me_audio(e, NULL, left);
      e->audio_frames -= (int64_t)left;
   }
   if (ok && e->aac && e->aac_packets)
      ok = me_packet(e, NULL);
   if (e->mux && !rmp4_mux_close(e->mux))
      ok = false;
   if (e->file && filestream_close(e->file) != 0)
      ok = false;
   e->mux  = NULL;
   e->file = NULL;
   me_free(e);
   return ok;
}
