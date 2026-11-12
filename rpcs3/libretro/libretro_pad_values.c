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

/* [x | y << 8]: x' | y' << 8 */
static uint16_t lr_squircle[65536];
static int lr_squircle_filled;

void lr_pad_squircle_fill(lr_pad_squircle_t convert)
{
   unsigned x, y;

   if (lr_squircle_filled || !convert)
      return;

   for (y = 0; y < 256; y++)
   {
      for (x = 0; x < 256; x++)
      {
         uint16_t cx = (uint16_t)x;
         uint16_t cy = (uint16_t)y;
         convert(&cx, &cy);
         lr_squircle[x | (y << 8)] = (uint16_t)((cx & 0xff) | ((cy & 0xff) << 8));
      }
   }

   lr_squircle_filled = 1;
}

void lr_pad_squircle(uint8_t *x, uint8_t *y)
{
   if (lr_squircle_filled)
   {
      const unsigned v = lr_squircle[*x | ((unsigned)*y << 8)];
      *x = (uint8_t)(v & 0xff);
      *y = (uint8_t)(v >> 8);
   }
}

void lr_pad_frame_read(const int16_t *pressure, const int16_t *pressed,
      const int16_t *stick, const uint8_t *order, struct lr_pad_frame *out)
{
   struct lr_pad_values now;
   unsigned i;

   lr_pad_values_read(pressure, pressed, stick, &now);

   for (i = 0; i < LR_PAD_FRAME_BUTTONS; i++)
   {
      const unsigned id = order[i];
      out->button[i] = (uint8_t)(id < LR_PAD_BUTTONS ? now.button[id] : 0);
   }

   lr_pad_squircle(&now.axis[0], &now.axis[1]);
   lr_pad_squircle(&now.axis[2], &now.axis[3]);

   for (i = 0; i < LR_PAD_AXES; i++)
      out->axis[i] = now.axis[i];
}
