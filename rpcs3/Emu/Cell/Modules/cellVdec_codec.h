/* cellVdec's decoders: libretro-common's rmpeg1_video (MPEG-2), rh264
 * (AVC) and rmpeg4_video (MPEG-4 Part 2 and DivX) behind the one
 * interface the module drives - an access unit in with a tag (the
 * module's time stamps and user data for it), pictures out in display
 * order with the tag of the unit each came from, its coding type, and
 * its samples copied into a picture buffer of the module's - and the
 * conversion of a picture into the format a game asks for. C89. */

#ifndef CELLVDEC_CODEC_H
#define CELLVDEC_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the codec, as CellVdecCodecType numbers the three */
enum
{
   VDC_MPEG2 = 0,
   VDC_AVC   = 1,
   VDC_MPEG4 = 2
};

/* what a unit's pictures may be dropped for, as CellVdecDecodeMode */
enum
{
   VDC_SKIP_NONE = 0,
   VDC_SKIP_B    = 1,   /* B pictures (non-reference ones in AVC) */
   VDC_SKIP_PB   = 2    /* all but I pictures */
};

/* coding types */
enum
{
   VDC_PIC_UNKNOWN = 0,
   VDC_PIC_I,
   VDC_PIC_P,
   VDC_PIC_B
};

/* A decoded picture, YUV 4:2:0 planar and tight: Y width x height, then
 * Cb and Cr of (width + 1) / 2 x (height + 1) / 2. @mem is the module's,
 * grown by vdc_next() as a picture needs (vdc_pic_free() frees it). */
struct vdc_pic
{
   uint8_t *mem;
   size_t   cap;
   unsigned width, height;
   int      type;
   int      interlaced;
   uint64_t tag;
};

struct vdc;

/* NULL for another codec or when out of memory */
struct vdc *vdc_new(int codec);
void        vdc_free(struct vdc *d);

/* A new sequence: the references and any pictures held for display go */
void vdc_start(struct vdc *d);

/* An access unit, read where it lies until the next vdc_feed(), its
 * pictures tagged @tag and dropped as @skip says; NULL for the end of
 * the stream, which lets out the pictures still held */
void vdc_feed(struct vdc *d, const uint8_t *au, size_t len, uint64_t tag, int skip);

/* The next picture the units fed so far give, copied into @pic: 1, or 0
 * when there is none until more is fed (or none at all) */
int vdc_next(struct vdc *d, struct vdc_pic *pic);

/* The frame rate the stream gives: 0, or -1 when it gives none (yet) */
int vdc_framerate(const struct vdc *d, unsigned *num, unsigned *den);

/* Bytes of a YUV 4:2:0 picture of the stream's size, 0 before one */
size_t vdc_picture_size(const struct vdc *d);

/* Units the decoder could not use */
unsigned vdc_errors(const struct vdc *d);

void vdc_pic_free(struct vdc_pic *pic);

/* the formats a picture is given out in, as CellVdecPicFormatType */
enum
{
   VDC_OUT_ARGB    = 0,
   VDC_OUT_RGBA    = 1,
   VDC_OUT_UYVY    = 2,
   VDC_OUT_YUV420  = 3
};

/* A picture into @dst in @format: ARGB and RGBA with @alpha, in BT.709
 * or BT.601 (broadcast range); UYVY and YUV 4:2:0 as they are.
 * Returns false for a format it does not know, @dst untouched. */
bool vdc_convert(uint8_t *dst, const struct vdc_pic *pic, int format,
      uint8_t alpha, bool bt709);

#ifdef __cplusplus
}
#endif

#endif
