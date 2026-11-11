/* Music and video files on libretro-common: see media_decode.h. C89. */

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <streams/file_stream.h>
#include <features/features_cpu.h>
#include <audio/audio_resampler.h>
#include <formats/audio.h>
#include <formats/rmp4.h>
#include <formats/rwebm.h>
#include <formats/rmpeg1_ps.h>
#include <formats/rcontainer.h>

#include "media_decode.h"

/* ---------------------------------------------------------------- */
/* the file                                                         */
/* ---------------------------------------------------------------- */

bool md_file_open(struct md_file *f, const char *path)
{
   RFILE  *s;
   int64_t len = 0;
   memset(f, 0, sizeof(*f));
   if (!path || !(s = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
               RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS)))
      return false;
   f->stream = s;
   if ((f->data = filestream_get_mapped_ptr(s, &len)) != NULL && len > 0)
   {
      f->len = (size_t)len;
      return true;
   }
   /* no mapping (a frontend's VFS, a platform without): read it once */
   f->data = NULL;
   len     = filestream_get_size(s);
   if (len <= 0 || (uint64_t)len > (size_t)-1 || !(f->owned = (uint8_t*)malloc((size_t)len)))
   {
      md_file_close(f);
      return false;
   }
   if (filestream_read(s, f->owned, len) != len)
   {
      md_file_close(f);
      return false;
   }
   f->data = f->owned;
   f->len  = (size_t)len;
   return true;
}

void md_file_close(struct md_file *f)
{
   if (f->stream)
      filestream_close((RFILE*)f->stream);
   free(f->owned);
   memset(f, 0, sizeof(*f));
}

/* ---------------------------------------------------------------- */
/* music                                                            */
/* ---------------------------------------------------------------- */

static bool md_is_mp4(const uint8_t *p, size_t n)
{
   return n >= 8 && (!memcmp(p + 4, "ftyp", 4) || !memcmp(p + 4, "moov", 4)
         || !memcmp(p + 4, "mdat", 4) || !memcmp(p + 4, "free", 4)
         || !memcmp(p + 4, "wide", 4));
}

static uint32_t md_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

/* ATRAC: ATRAC3plus (or plain ATRAC3), by the WAVE format tag or the
 * OpenMG header's codec */
static int md_atrac_codec(const uint8_t *p, size_t n)
{
   if (n >= 12 && !memcmp(p, "RIFF", 4))
   {
      size_t c = 12;
      while (c + 10 <= n)
      {
         size_t len = md_le32(p + c + 4);
         if (!memcmp(p + c, "fmt ", 4))
            return (p[c + 8] | (p[c + 9] << 8)) == 0x0270 ? MD_CODEC_ATRAC3 : MD_CODEC_ATRAC3P;
         if (len > n - c - 8)
            break;
         c += 8 + len + (len & 1);
      }
      return MD_CODEC_OTHER;
   }
   {
      size_t off = 0;
      if (n >= 10 && !memcmp(p, "ea3", 3))
         off = 10 + (((size_t)(p[6] & 0x7f) << 21) | ((size_t)(p[7] & 0x7f) << 14)
               | ((size_t)(p[8] & 0x7f) << 7) | (p[9] & 0x7f));
      if (off <= n && n - off > 32)
         return p[off + 32] == 0 ? MD_CODEC_ATRAC3 : MD_CODEC_ATRAC3P;
   }
   return MD_CODEC_OTHER;
}

static int md_audio_codec(enum audio_type_enum type, const uint8_t *p, size_t n)
{
   switch (type)
   {
      case AUDIO_TYPE_WAV:    return MD_CODEC_LPCM;
      case AUDIO_TYPE_FLAC:   return MD_CODEC_FLAC;
      case AUDIO_TYPE_VORBIS: return MD_CODEC_VORBIS;
      case AUDIO_TYPE_OPUS:   return MD_CODEC_OPUS;
      case AUDIO_TYPE_AAC:    return MD_CODEC_AAC;
      case AUDIO_TYPE_AC3:    return MD_CODEC_AC3;
      case AUDIO_TYPE_WMA:    return MD_CODEC_WMA;
      case AUDIO_TYPE_ATRAC:  return md_atrac_codec(p, n);
      case AUDIO_TYPE_MP3:
         /* layer II is MPEG audio's other codec */
         return n >= 2 && ((p[1] >> 1) & 3) == 2 ? MD_CODEC_MP2 : MD_CODEC_MP3;
      default:
         return MD_CODEC_OTHER;
   }
}

/* An MP4 whose first track is not audio is a video, not music */
static bool md_mp4_first_audio(const uint8_t *p, size_t n)
{
   rmp4_t           *m = rmp4_open_memory(p, n);
   const rmp4_track *t;
   bool              audio;
   if (!m)
      return false;
   t     = rmp4_get_track(m, 0);
   audio = t && t->type == RMP4_TRACK_AUDIO;
   rmp4_close(m);
   return audio;
}

static bool md_probe_video(const uint8_t *data, size_t len, struct md_info *info);

static bool md_probe_audio(const uint8_t *data, size_t len, struct md_info *info)
{
   size_t               skip = 0;
   enum audio_type_enum type = audio_transfer_sniff_type(data, len, &skip);
   void                *ctx;
   unsigned             rate = 0, ch = 0;
   uint64_t             total = 0;
   bool                 ok;

   if (type == AUDIO_TYPE_NONE)
      return false;
   {
      /* a video's audio, found by the raw-stream scan, is not music */
      struct md_info v;
      memset(&v, 0, sizeof(v));
      if (md_probe_video(data, len, &v))
         return false;
   }
   if (type == AUDIO_TYPE_AAC && md_is_mp4(data + skip, len - skip)
         && !md_mp4_first_audio(data + skip, len - skip))
      return false;
   if (!(ctx = audio_transfer_new(type)))
      return false;
   audio_transfer_set_buffer_ptr(ctx, type, (void*)(data + skip), len - skip);
   ok = audio_transfer_start(ctx, type)
     && audio_transfer_info(ctx, type, &ch, &rate, &total) && rate && ch;
   audio_transfer_free(ctx, type);
   if (!ok)
      return false;

   info->audio_codec = md_audio_codec(type, data + skip, len - skip);
   info->sample_rate = (int32_t)rate;
   if (total)
   {
      info->duration_us   = (int64_t)(total * 1000000 / rate);
      if (info->duration_us > 0)
         info->audio_bitrate = (int32_t)((uint64_t)(len - skip) * 8 * 1000000
               / (uint64_t)info->duration_us);
   }
   return true;
}

/* ---------------------------------------------------------------- */
/* video                                                            */
/* ---------------------------------------------------------------- */

static int md_fourcc_video(const char *id, unsigned object_type)
{
   if (!strcmp(id, "avc1") || !strcmp(id, "avc3"))
      return MD_CODEC_AVC;
   /* mp4v carries any MPEG video, or JPEG: the esds says which */
   if (!strcmp(id, "mp4v"))
      return object_type == 0x20 || !object_type ? MD_CODEC_MPEG4
           : object_type >= 0x60 && object_type <= 0x65 ? MD_CODEC_MPEG2
           : object_type == 0x6A ? MD_CODEC_MPEG1 : MD_CODEC_OTHER;
   if (!strcmp(id, "mp2v") || !strcmp(id, "m2v1"))
      return MD_CODEC_MPEG2;
   return MD_CODEC_OTHER;
}

static int md_fourcc_audio(const char *id)
{
   if (!strcmp(id, "mp4a"))
      return MD_CODEC_AAC;
   if (!strcmp(id, "ac-3"))
      return MD_CODEC_AC3;
   if (!strcmp(id, ".mp3"))
      return MD_CODEC_MP3;
   if (!strcmp(id, "lpcm") || !strcmp(id, "sowt") || !strcmp(id, "twos"))
      return MD_CODEC_LPCM;
   return MD_CODEC_OTHER;
}

/* a track's bit rate: its samples' bytes over the movie's length */
static int32_t md_mp4_bitrate(const rmp4_t *m, int track, int64_t duration_us)
{
   uint32_t count = 0, i;
   uint64_t bytes = 0;
   if (duration_us <= 0 || !rmp4_track_pts(m, track, &count))
      return 0;
   for (i = 0; i < count; i++)
   {
      size_t b = 0, e = 0;
      if (rmp4_track_sample_span(m, track, i, &b, &e) && e > b)
         bytes += e - b;
   }
   return (int32_t)(bytes * 8 * 1000000 / (uint64_t)duration_us);
}

static bool md_probe_mp4(const uint8_t *data, size_t len, struct md_info *info)
{
   rmp4_t           *m = rmp4_open_memory(data, len);
   const rmp4_track *t;
   int               i, n;
   if (!m)
      return false;
   if (!(t = rmp4_get_track(m, 0)) || t->type != RMP4_TRACK_VIDEO)
   {
      rmp4_close(m);
      return false;
   }
   info->duration_us   = rmp4_duration_ns(m) / 1000;
   info->video_codec   = md_fourcc_video(t->codec_id, t->object_type);
   info->video_bitrate = md_mp4_bitrate(m, 0, info->duration_us);
   n = rmp4_num_tracks(m);
   for (i = 1; i < n; i++)
      if ((t = rmp4_get_track(m, i)) && t->type == RMP4_TRACK_AUDIO)
      {
         info->audio_codec   = md_fourcc_audio(t->codec_id);
         info->sample_rate   = (int32_t)t->sample_rate;
         info->audio_bitrate = md_mp4_bitrate(m, i, info->duration_us);
         break;
      }
   rmp4_close(m);
   return true;
}

static bool md_prefix(const char *s, const char *p)
{
   return !strncmp(s, p, strlen(p));
}

static bool md_probe_mkv(const uint8_t *data, size_t len, struct md_info *info)
{
   rwebm_t           *m = rwebm_open_memory(data, len);
   const rwebm_track *t;
   int                i, n;
   if (!m)
      return false;
   if (!(t = rwebm_get_track(m, 0)) || t->type != RWEBM_TRACK_VIDEO)
   {
      rwebm_close(m);
      return false;
   }
   info->duration_us = rwebm_duration_ns(m) / 1000;
   info->video_codec = md_prefix(t->codec_id, "V_MPEG4/ISO/AVC") ? MD_CODEC_AVC
      : md_prefix(t->codec_id, "V_MPEG4/ISO/") ? MD_CODEC_MPEG4
      : md_prefix(t->codec_id, "V_MPEG2") ? MD_CODEC_MPEG2
      : md_prefix(t->codec_id, "V_MPEG1") ? MD_CODEC_MPEG1 : MD_CODEC_OTHER;
   n = rwebm_num_tracks(m);
   for (i = 1; i < n; i++)
      if ((t = rwebm_get_track(m, i)) && t->type == RWEBM_TRACK_AUDIO)
      {
         info->audio_codec = md_prefix(t->codec_id, "A_AAC") ? MD_CODEC_AAC
            : md_prefix(t->codec_id, "A_AC3") ? MD_CODEC_AC3
            : md_prefix(t->codec_id, "A_MPEG/L3") ? MD_CODEC_MP3
            : md_prefix(t->codec_id, "A_MPEG/L2") ? MD_CODEC_MP2
            : md_prefix(t->codec_id, "A_VORBIS") ? MD_CODEC_VORBIS
            : md_prefix(t->codec_id, "A_OPUS") ? MD_CODEC_OPUS
            : md_prefix(t->codec_id, "A_FLAC") ? MD_CODEC_FLAC
            : md_prefix(t->codec_id, "A_PCM") ? MD_CODEC_LPCM : MD_CODEC_OTHER;
         info->sample_rate = (int32_t)t->sample_rate;
         break;
      }
   rwebm_close(m);
   return true;
}

/* An MPEG program stream: the codecs from the packets at its start, the
 * length from the presentation times at its start and end */
#define MD_PS_SPAN (512 * 1024)

static void md_ps_walk(rmpeg1_ps_t *ps, const uint8_t *p, size_t n,
      struct md_info *info, uint64_t *first, uint64_t *last, bool head)
{
   rmpeg1_ps_packet_t pkt;
   rmpeg1_ps_reset(ps);
   if (!rmpeg1_ps_borrow(ps, p, n))
      return;
   while (rmpeg1_ps_next(ps, &pkt))
   {
      /* the length: the earliest and latest times of any stream */
      if ((pkt.type == RMPEG1_PS_VIDEO || pkt.type == RMPEG1_PS_AUDIO
               || pkt.type == RMPEG1_PS_PRIVATE_1) && pkt.pts != RMPEG1_PS_NO_PTS)
      {
         if (head && (*first == RMPEG1_PS_NO_PTS || pkt.pts < *first))
            *first = pkt.pts;
         if (*last == RMPEG1_PS_NO_PTS || pkt.pts > *last)
            *last = pkt.pts;
      }
      if (!head)
         continue;
      if (pkt.type == RMPEG1_PS_VIDEO && info->video_codec == MD_CODEC_NONE)
      {
         /* a sequence extension after the sequence header: MPEG-2 */
         size_t i;
         int    seq = 0;
         for (i = 0; i + 4 < pkt.size; i++)
            if (!pkt.data[i] && !pkt.data[i + 1] && pkt.data[i + 2] == 1)
            {
               if (pkt.data[i + 3] == 0xB3)
                  seq = 1;
               else if (pkt.data[i + 3] == 0xB5 && seq && (pkt.data[i + 4] >> 4) == 1)
               {
                  seq = 2;
                  break;
               }
            }
         if (seq)
            info->video_codec = seq == 2 ? MD_CODEC_MPEG2 : MD_CODEC_MPEG1;
      }
      else if (pkt.type == RMPEG1_PS_AUDIO && info->audio_codec == MD_CODEC_NONE)
         info->audio_codec = pkt.size >= 2 && pkt.data[0] == 0xFF
            && ((pkt.data[1] >> 1) & 3) == 1 ? MD_CODEC_MP3 : MD_CODEC_MP2;
      else if (pkt.type == RMPEG1_PS_PRIVATE_1 && info->audio_codec == MD_CODEC_NONE && pkt.size)
      {
         unsigned sub = pkt.data[0];
         info->audio_codec = (sub & 0xF8) == 0x80 ? MD_CODEC_AC3
            : (sub & 0xF8) == 0xA0 ? MD_CODEC_LPCM : MD_CODEC_OTHER;
      }
   }
   if (head && rmpeg1_ps_mux_rate(ps))
      info->video_bitrate = (int32_t)((uint64_t)rmpeg1_ps_mux_rate(ps) * 400);
}

static bool md_probe_ps(const uint8_t *data, size_t len, struct md_info *info)
{
   rmpeg1_ps_t *ps = rmpeg1_ps_init(0);
   uint64_t     first = RMPEG1_PS_NO_PTS, last = RMPEG1_PS_NO_PTS;
   size_t       span = len < MD_PS_SPAN ? len : MD_PS_SPAN;
   if (!ps)
      return false;
   md_ps_walk(ps, data, span, info, &first, &last, true);
   if (len > span)
      md_ps_walk(ps, data + len - span, span, info, &first, &last, false);
   rmpeg1_ps_free(ps);
   if (info->video_codec == MD_CODEC_NONE)
      return false;
   if (first != RMPEG1_PS_NO_PTS && last > first)
      info->duration_us = (int64_t)((last - first) * 1000 / 90);
   return true;
}

/* AVI and ASF: a video FourCC, an audio WAVE format tag */
static int md_tag_video(uint32_t f)
{
   static const char *const mpeg4[] = { "XVID", "DIVX", "DX50", "FMP4", "MP4V", "3IV2", "XVIX" };
   static const char *const avc[]   = { "H264", "X264", "AVC1", "DAVC" };
   char     t[5];
   unsigned i;
   for (i = 0; i < 4; i++)
   {
      char c = (char)((f >> (8 * i)) & 0xff);
      t[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
   }
   t[4] = '\0';
   for (i = 0; i < sizeof(mpeg4) / sizeof(mpeg4[0]); i++)
      if (!strcmp(t, mpeg4[i]))
         return MD_CODEC_MPEG4;
   for (i = 0; i < sizeof(avc) / sizeof(avc[0]); i++)
      if (!strcmp(t, avc[i]))
         return MD_CODEC_AVC;
   if (!strcmp(t, "MPG2") || !strcmp(t, "MPEG"))
      return MD_CODEC_MPEG2;
   if (!strcmp(t, "MPG1"))
      return MD_CODEC_MPEG1;
   return MD_CODEC_OTHER;
}

static int md_tag_audio(unsigned tag)
{
   switch (tag)
   {
      case 0x0001: case 0x0003: return MD_CODEC_LPCM;
      case 0x0050:              return MD_CODEC_MP2;
      case 0x0055:              return MD_CODEC_MP3;
      case 0x00FF: case 0x1610: case 0x706D: case 0xA106: return MD_CODEC_AAC;
      case 0x2000:              return MD_CODEC_AC3;
      case 0x0160: case 0x0161: case 0x0162: case 0x0163: return MD_CODEC_WMA;
      case 0x0270:              return MD_CODEC_ATRAC3;
      default:                  return MD_CODEC_OTHER;
   }
}

static bool md_probe_container(const uint8_t *data, size_t len, struct md_info *info)
{
   rcontainer_info c;
   unsigned        i;
   if (!rcontainer_probe(data, len, &c) || c.streams[0].type != RCONTAINER_STREAM_VIDEO)
      return false;
   info->duration_us   = c.duration_us;
   info->video_codec   = md_tag_video(c.streams[0].fourcc);
   info->video_bitrate = (int32_t)c.streams[0].bitrate;
   for (i = 1; i < c.num_streams; i++)
      if (c.streams[i].type == RCONTAINER_STREAM_AUDIO)
      {
         info->audio_codec   = md_tag_audio(c.streams[i].format_tag);
         info->audio_bitrate = (int32_t)c.streams[i].bitrate;
         info->sample_rate   = (int32_t)c.streams[i].sample_rate;
         break;
      }
   return true;
}

static bool md_probe_video(const uint8_t *data, size_t len, struct md_info *info)
{
   if (md_probe_container(data, len, info))
      return true;
   if (md_is_mp4(data, len))
      return md_probe_mp4(data, len, info);
   if (len >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3)
      return md_probe_mkv(data, len, info);
   if (len >= 4 && !data[0] && !data[1] && data[2] == 1 && data[3] == 0xBA)
      return md_probe_ps(data, len, info);
   return false;
}

bool md_probe(const uint8_t *data, size_t len, bool video, struct md_info *info)
{
   memset(info, 0, sizeof(*info));
   if (!data || !len)
      return false;
   return video ? md_probe_video(data, len, info) : md_probe_audio(data, len, info);
}

/* ---------------------------------------------------------------- */
/* playback                                                         */
/* ---------------------------------------------------------------- */

#define MD_CHUNK 4096
#define MD_FLUSH 512   /* zeros through the resampler at the end: its tail */

struct md_audio
{
   void                *ctx;
   enum audio_type_enum type;
   unsigned             channels, rate;
   float                matrix[8][2];
   bool                 identity;   /* stereo as it comes */
   void                *rs;         /* sinc, when the rate is not 48 kHz */
   double               ratio;
   uint64_t             src_pos;
   uint64_t             out_frames; /* the stated length at 48 kHz, 0 unknown */
   uint64_t             out_done;
   bool                 ended, flushed, failed;
   float                dec[MD_CHUNK * 8];
   float                st[(MD_CHUNK > MD_FLUSH ? MD_CHUNK : MD_FLUSH) * 2];
};

struct md_audio *md_audio_new(void)
{
   return (struct md_audio*)calloc(1, sizeof(struct md_audio));
}

void md_audio_free(struct md_audio *a)
{
   if (!a)
      return;
   md_audio_close(a);
   free(a);
}

void md_audio_close(struct md_audio *a)
{
   if (a->ctx)
      audio_transfer_free(a->ctx, a->type);
   if (a->rs)
      sinc_resampler.free(a->rs);
   a->ctx = NULL;
   a->rs  = NULL;
}

bool md_audio_open(struct md_audio *a, const uint8_t *data, size_t len)
{
   size_t   skip = 0;
   uint64_t total = 0;
   unsigned c;

   md_audio_close(a);
   a->src_pos = a->out_done = a->out_frames = 0;
   a->ended   = a->flushed = a->failed = false;
   if (!data || !len)
      return false;
   a->type = audio_transfer_sniff_type(data, len, &skip);
   if (a->type == AUDIO_TYPE_NONE || !(a->ctx = audio_transfer_new(a->type)))
      return false;
   audio_transfer_set_buffer_ptr(a->ctx, a->type, (void*)(data + skip), len - skip);
   if (     !audio_transfer_start(a->ctx, a->type)
         || !audio_transfer_info(a->ctx, a->type, &a->channels, &a->rate, &total)
         || !a->channels || a->channels > 8 || a->rate < 1000 || a->rate > 384000
         || !audio_transfer_stereo_matrix_f32(a->ctx, a->type, a->matrix))
   {
      md_audio_close(a);
      return false;
   }
   /* Mono to both sides at -3 dB, as FFmpeg's resampler upmixed it for
    * standalone RPCS3; the rest folds as audio_transfer says, which is
    * FFmpeg's float fold */
   if (a->channels == 1)
      a->matrix[0][0] = a->matrix[0][1] = 0.70710678f;
   a->identity = a->channels == 2;
   for (c = 0; c < a->channels && a->identity; c++)
      if (a->matrix[c][0] != (c == 0 ? 1.0f : 0.0f) || a->matrix[c][1] != (c == 1 ? 1.0f : 0.0f))
         a->identity = false;

   a->ratio = (double)MD_RATE / a->rate;
   if (a->rate != MD_RATE)
   {
      if (!(a->rs = sinc_resampler.init(NULL, a->ratio, RESAMPLER_QUALITY_HIGHER,
                  (resampler_simd_mask_t)cpu_features_get())))
      {
         md_audio_close(a);
         return false;
      }
   }
   if (total)
      a->out_frames = (uint64_t)((double)total * a->ratio + 0.5);
   return true;
}

uint64_t md_audio_frames(const struct md_audio *a)
{
   return a->out_frames;
}

bool md_audio_failed(const struct md_audio *a)
{
   return a->failed;
}

/* @n frames of the decoder's channels in dec into stereo at @dst */
static void md_fold(const struct md_audio *a, float *dst, size_t n)
{
   size_t   i;
   unsigned c;
   for (i = 0; i < n; i++)
   {
      const float *in = a->dec + i * a->channels;
      float        l  = 0.0f, r = 0.0f;
      for (c = 0; c < a->channels; c++)
      {
         l += in[c] * a->matrix[c][0];
         r += in[c] * a->matrix[c][1];
      }
      dst[2 * i]     = l;
      dst[2 * i + 1] = r;
   }
}

/* up to @n source frames, stereo, into @dst: the count, 0 at the end */
static size_t md_decode(struct md_audio *a, float *dst, size_t n)
{
   size_t got = 0;
   int    r;
   if (a->ended)
      return 0;
   r = audio_transfer_read_f32(a->ctx, a->type, a->identity ? dst : a->dec, n, &got);
   if (r == AUDIO_PROCESS_ERROR)
   {
      a->failed = true;
      a->ended  = true;
      return 0;
   }
   if (!got)
   {
      /* the whole file is there, so nothing read is the end */
      a->ended = true;
      return 0;
   }
   if (!a->identity)
      md_fold(a, dst, got);
   a->src_pos += got;
   return got;
}

size_t md_audio_read(struct md_audio *a, float *out, size_t max, uint64_t *ms)
{
   if (!a->ctx || a->failed || max < MD_READ_MIN)
      return 0;
   *ms = a->rate ? a->src_pos * 1000 / a->rate : 0;

   /* 48 kHz: straight into the caller's buffer */
   if (!a->rs)
      return md_decode(a, out, MD_CHUNK);

   for (;;)
   {
      struct resampler_data d;
      size_t                n;
      if (a->flushed)
         return 0;
      /* as much as comes out within @max at the ratio */
      n = (size_t)((double)(max - 64) / a->ratio);
      n = md_decode(a, a->st, n < MD_CHUNK ? n : MD_CHUNK);
      if (a->failed)
         return 0;
      if (!n)
      {
         /* the resampler's tail, once */
         memset(a->st, 0, sizeof(float) * 2 * MD_FLUSH);
         n          = MD_FLUSH;
         a->flushed = true;
      }
      d.data_in       = a->st;
      d.data_out      = out;
      d.input_frames  = n;
      d.output_frames = 0;
      d.ratio         = a->ratio;
      sinc_resampler.process(a->rs, &d);
      /* the zeros through at the end bring out the filter's tail, up
       * to what the decoded frames make at the ratio, no further */
      if (a->ended)
      {
         uint64_t total = (uint64_t)((double)a->src_pos * a->ratio + 0.5);
         if (a->out_done + d.output_frames > total)
            d.output_frames = total > a->out_done ? (size_t)(total - a->out_done) : 0;
      }
      a->out_done += d.output_frames;
      if (d.output_frames)
         return d.output_frames;
   }
}
