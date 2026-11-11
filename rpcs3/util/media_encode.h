/* What cellRec records, on libretro-common: video frames and audio
 * samples into an MP4 file. Video is MPEG-4 Part 2 (rmpeg4_encode),
 * H.264 Constrained Baseline (rh264_encode) or motion JPEG
 * (rjpeg_encode); audio AAC-LC (raac_encode), u-law or big-endian float
 * PCM; the file rmp4_mux's, written through filestream. A frame of any
 * size is scaled to the recording's (gfx/scaler, after a box average
 * when shrinking by 2 or more) and converted to 4:2:0 BT.601
 * (image_yuv_420_from_bytes). Everything is allocated by me_open(),
 * except the scaler's tables, made again when the input size changes.
 * One thread drives an encoder. C89. */

#ifndef MEDIA_ENCODE_H
#define MEDIA_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   ME_VIDEO_MPEG4 = 0,
   ME_VIDEO_H264,
   ME_VIDEO_MJPEG
};

enum
{
   ME_AUDIO_AAC = 0,
   ME_AUDIO_ULAW,
   ME_AUDIO_PCM_F32BE
};

/* A frame's pixels: four bytes in memory order, or 4:2:0 planes */
enum
{
   ME_PIXEL_RGBA = 0,
   ME_PIXEL_BGRA,
   ME_PIXEL_ARGB,
   ME_PIXEL_YUV420P   /* Y, then Cb, then Cr, each row (w + 1) / 2 */
};

struct me_params
{
   const char *path;
   int         video_codec;
   unsigned    width, height;    /* even */
   unsigned    fps;
   uint32_t    video_bitrate;    /* bits a second */
   unsigned    gop;              /* a key frame every gop frames; 0 the codec's */
   int         audio_codec;
   unsigned    sample_rate;      /* 8000 to 96000 */
   unsigned    channels;         /* 1 or 2 */
   uint32_t    audio_bitrate;    /* AAC; PCM's follows from the rate */
   unsigned    max_seconds;      /* the most the file holds: its index is sized for it */
};

struct me_encoder;

/* The file created and its start written; NULL when a parameter is not
 * one this takes, or the file cannot be written */
struct me_encoder *me_open(const struct me_params *p);

/* A frame shown at @pts frame periods from the start, after the last;
 * @pitch in bytes (a YUV420P frame's planes are packed, @pitch unused).
 * A YUV420P frame is the recording's size. false on an error, after
 * which the file only closes. */
bool me_video(struct me_encoder *e, const uint8_t *data, size_t pitch,
      unsigned w, unsigned h, int format, int64_t pts);

/* @frames frames of interleaved floats in -1..1, the channels opened
 * with, straight after the last; NULL for silence */
bool me_audio(struct me_encoder *e, const float *pcm, size_t frames);

/* The audio flushed, the index written, the file closed and @e freed;
 * false when anything failed on the way (a file is still closed) */
bool me_close(struct me_encoder *e);

#ifdef __cplusplus
}
#endif

#endif
