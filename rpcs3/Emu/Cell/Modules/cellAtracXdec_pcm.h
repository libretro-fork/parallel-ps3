/* cellAtracXdec's output: a frame decoded by libretro-common's ratrac3p
 * (2048 samples a channel, interleaved floats, the channels in the
 * order the stream's channel blocks carry them) put into the game's
 * buffer as the LLE decoder writes it - the channels in its order, the
 * samples big endian in the word size the game asked for, clipped as
 * it clips them. C89. */

#ifndef CELLATRACXDEC_PCM_H
#define CELLATRACXDEC_PCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the stream's channel configuration (1 to 7) to its channel count */
unsigned atxpcm_channels(unsigned ch_config_idx);

/**
 * atxpcm_convert:
 * @out           : the game's buffer, @samples * channels words
 * @pcm           : the decoder's frame
 * @ch_config_idx : the stream's channel configuration, 1 to 7
 * @word          : 2, 3 or 4 (16, 24 or 32-bit integers), 0x84 (float)
 * @samples       : samples a channel to write
 */
void atxpcm_convert(uint8_t *out, const float *pcm, unsigned ch_config_idx,
      unsigned word, unsigned samples);

#ifdef __cplusplus
}
#endif

#endif
