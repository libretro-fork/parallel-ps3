/* cellAtracXdec's output. See cellAtracXdec_pcm.h. */

#include <math.h>
#include <string.h>

#include "cellAtracXdec_pcm.h"

/* Where each of the stream's channels - in the order of its channel
 * blocks, as ratrac3p gives them - goes in the game's buffer, whose
 * order is the LLE decoder's: Front Left, Center, Front Right, Rear
 * Left, Rear Right, Rear Center, LFE for up to seven channels, and
 * Front Left, Front Right, Center, LFE, Rear Left, Rear Right, Side
 * Left, Side Right for eight. These are where RPCS3's FFmpeg path put
 * them - FFmpeg's channel order, then RPCS3's map from it to the LLE's
 * - found by decoding streams with a different signal in each channel
 * both ways (test_atracx checks them). */
static const uint8_t atx_ch_map[7][8] = {
   { 0 },
   { 0, 1 },
   { 0, 2, 1 },
   { 0, 2, 1, 3 },
   { 0, 2, 1, 5, 3, 4 },
   { 0, 2, 1, 5, 6, 3, 4 },
   { 0, 1, 2, 4, 5, 6, 7, 3 }
};

unsigned atxpcm_channels(unsigned ch_config_idx)
{
   return ch_config_idx <= 4 ? ch_config_idx : ch_config_idx + 1;
}

static void put16(uint8_t *p, int v)
{
   p[0] = (uint8_t)(v >> 8);
   p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v >> 24);
   p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);
   p[3] = (uint8_t)v;
}

void atxpcm_convert(uint8_t *out, const float *pcm, unsigned ch_config_idx,
      unsigned word, unsigned samples)
{
   unsigned       nch = atxpcm_channels(ch_config_idx), ch, i;
   const uint8_t *map;
   if (ch_config_idx < 1 || ch_config_idx > 7)
      return;
   map = atx_ch_map[ch_config_idx - 1];
   for (ch = 0; ch < nch; ch++)
   {
      const float *s = pcm + ch;
      unsigned     o = map[ch];
      for (i = 0; i < samples; i++, s += nch, o += nch)
      {
         float v = *s;
         switch (word)
         {
            case 0x84:
            {
               uint32_t bits;
               /* the largest float under 1.0 at most, as the LLE does */
               if (v >= 0.99999994f)
                  bits = 0x3f7fffffu;
               else
               {
                  if (v <= -1.0f)
                     v = -1.0f;
                  memcpy(&bits, &v, 4);
               }
               put32(out + (size_t)o * 4, bits);
               break;
            }
            case 2:
               put16(out + (size_t)o * 2, v >= 1.0f ? 32767 : v <= -1.0f ? -32768
                     : (int)floor((double)(v * 32768.0f)));
               break;
            case 3:
               put32(out + (size_t)o * 4, v >= 1.0f ? 0x007fffffu : v <= -1.0f ? 0x00800000u
                     : (uint32_t)(long)floor((double)(v * 8388608.0f)) & 0x00ffffffu);
               break;
            default:
               put32(out + (size_t)o * 4, v >= 1.0f ? 0x7fffffffu : v <= -1.0f ? 0x80000000u
                     : (uint32_t)(int32_t)floor((double)(v * 2147483648.0f)));
               break;
         }
      }
   }
}
