/* The frontend's pad readings as a DualShock 3's values: see
 * libretro_pad_values.h. C89. */

#include "libretro_pad_values.h"

void lr_pad_values_read(const int16_t *pressure, const int16_t *pressed,
      const int16_t *stick, struct lr_pad_values *out)
{
   unsigned i;

   for (i = 0; i < LR_PAD_BUTTONS; i++)
   {
      const int p = pressure[i];
      if (p > 0)
      {
         /* 0x7fff to 255, rounded; any pressure at all is at least 1 */
         const unsigned v = ((unsigned)p * 255u + 16383u) / 32767u;
         out->button[i] = (uint8_t)(v ? v : 1);
      }
      else
         out->button[i] = (uint8_t)(pressed[i] ? 255 : 0);
   }

   for (i = 0; i < LR_PAD_AXES; i++)
   {
      /* -0x8000..0x7fff to 0..255, rounded, in one step */
      const unsigned v = (unsigned)(stick[i] + 32768);
      out->axis[i] = (uint8_t)((v * 255u + 32767u) / 65535u);
   }
}
