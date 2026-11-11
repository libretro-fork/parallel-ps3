/* Music and video files on libretro-common: what cellSearch lists (a
 * file's codecs, rate, bit rates and length) and what cellMusicDecode
 * plays (any of audio_transfer's formats, folded to stereo and
 * resampled to 48 kHz). The file is mapped where it lies, or read once
 * when it cannot be. C89. */

#ifndef MEDIA_DECODE_H
#define MEDIA_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the codecs a listing tells apart, the core's own numbering */
enum
{
   MD_CODEC_NONE = 0,
   MD_CODEC_OTHER,
   /* audio */
   MD_CODEC_MP3,
   MD_CODEC_AAC,
   MD_CODEC_AC3,
   MD_CODEC_WMA,
   MD_CODEC_ATRAC3,
   MD_CODEC_ATRAC3P,
   MD_CODEC_LPCM,
   MD_CODEC_FLAC,
   MD_CODEC_VORBIS,
   MD_CODEC_OPUS,
   MD_CODEC_MP2,
   /* video */
   MD_CODEC_MPEG1,
   MD_CODEC_MPEG2,
   MD_CODEC_MPEG4,
   MD_CODEC_AVC
};

/* the rate cellMusicDecode plays at */
#define MD_RATE 48000

/* A whole file, read-only: mapped, or read into memory of its own */
struct md_file
{
   const uint8_t *data;
   size_t         len;
   void          *stream;
   uint8_t       *owned;
};

bool md_file_open(struct md_file *f, const char *path);
void md_file_close(struct md_file *f);

struct md_info
{
   int      audio_codec;
   int      video_codec;
   int32_t  audio_bitrate;   /* bits a second, 0 unknown */
   int32_t  video_bitrate;
   int32_t  sample_rate;
   int64_t  duration_us;     /* 0 unknown */
};

/* A music file (@video false: its first stream audio) or a video file
 * (its first stream video): false when it is not one, or not one of
 * the formats read here. */
bool md_probe(const uint8_t *data, size_t len, bool video, struct md_info *info);

/* Playback: a decoder made once, opened over each track in turn */
struct md_audio;

struct md_audio *md_audio_new(void);
void             md_audio_free(struct md_audio *a);

/* A track, read where it lies until md_audio_close() */
bool     md_audio_open(struct md_audio *a, const uint8_t *data, size_t len);
void     md_audio_close(struct md_audio *a);

/* The track's length in 48 kHz frames, 0 when it does not say */
uint64_t md_audio_frames(const struct md_audio *a);

/* Up to @max 48 kHz stereo frames (interleaved floats) into @out, with
 * @ms the track time of the first; 0 at the end of the track or on an
 * error (md_audio_failed() tells which). @max is at least
 * MD_READ_MIN. */
#define MD_READ_MIN 32768
size_t   md_audio_read(struct md_audio *a, float *out, size_t max, uint64_t *ms);
bool     md_audio_failed(const struct md_audio *a);

#ifdef __cplusplus
}
#endif

#endif
