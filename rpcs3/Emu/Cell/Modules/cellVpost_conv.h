/* cellVpost's picture conversion: a YUV 4:2:0 planar picture (Y, then
 * Cb and Cr at a quarter its size each) to interleaved RGBA with the
 * alpha the game asks for, in the matrix and range it names, scaled
 * bilinear to the output size. libretro-common's image_yuv_blit
 * converts, its scaler scales; a picture of the output size is
 * converted straight into the game's buffer. The scaler and the buffer
 * a scaled picture is converted into are kept from one picture to the
 * next, so a picture of the size of the last takes no memory. C89. */

#ifndef CELLVPOST_CONV_H
#define CELLVPOST_CONV_H

#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vpost_conv;

/* NULL when out of memory */
struct vpost_conv *vpost_conv_new(void);
void               vpost_conv_free(struct vpost_conv *c);

/**
 * vpost_conv_exec:
 * @out, @out_w, @out_h  : the RGBA picture, out_w * 4 bytes a row
 * @in, @in_w, @in_h     : the YUV 4:2:0 planar picture
 * @full_range           : Y and C over 0..255, not 16..235 / 16..240
 * @bt709                : BT.709's matrix, not BT.601's
 * @alpha                : every pixel's alpha
 *
 * Returns false when the scaler could not be set up (out of memory),
 * @out then left as it was.
 */
bool vpost_conv_exec(struct vpost_conv *c, uint8_t *out, unsigned out_w, unsigned out_h,
      const uint8_t *in, unsigned in_w, unsigned in_h, bool full_range, bool bt709,
      uint8_t alpha);

#ifdef __cplusplus
}
#endif

#endif
