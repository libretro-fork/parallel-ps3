/* The blit engine's scaled and converting copy (NV3089 in software):
 * an image of 16-bit R5G6B5 or 32-bit A8R8G8B8 pixels, both stored big
 * endian as the RSX keeps them, scaled to another size and converted to
 * the other format by libretro-common's scaler, nearest or bilinear.
 * Only the first @src_slice_h rows of the source are read; the rows
 * under them stand in for the rest, which are never shown.
 *
 * The scaler and the buffers that put the pixels in the host's byte
 * order are kept in a struct rsx_scale between copies, so a copy of the
 * size and formats of the last takes no memory. C89; the header keeps
 * the scaler's own out of the C++ that includes it. */

#ifndef RSX_SCALE_H
#define RSX_SCALE_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rsx_scale_format
{
   RSX_SCALE_R5G6B5 = 0,   /* 2 bytes, big endian */
   RSX_SCALE_A8R8G8B8      /* 4 bytes: A, R, G, B */
};

struct rsx_scale;

/* NULL when out of memory */
struct rsx_scale *rsx_scale_new(void);

/* returns false if the scaler could not be set up (out of memory), the
 * destination then left as it was */
bool rsx_scale_image(struct rsx_scale *s,
      uint8_t *dst, enum rsx_scale_format dst_format,
      int dst_width, int dst_height, int dst_pitch,
      const uint8_t *src, enum rsx_scale_format src_format,
      int src_width, int src_height, int src_pitch, int src_slice_h,
      bool bilinear);

void rsx_scale_free(struct rsx_scale *s);

#ifdef __cplusplus
}
#endif

#endif
