/* The frontend's pad readings as a DualShock 3's values
 * (libretro/libretro_pad_values.c), every input there is:
 *
 * - a stick's 65536 positions: 0 at -0x8000, 128 at rest, 255 at 0x7fff,
 *   never backwards, every one of the pad's 256 values reached, the same
 *   distance either way (v and -v-1 mirror), each the nearest of the 256
 *   (exact rounding against the fraction);
 * - a button's 32767 pressures: 1 at the least, 128 at half, 255 at full,
 *   never backwards, the nearest of 255, never 0 while there is any;
 * - a button with no pressure: 255 pressed, 0 not; each button its own.
 *
 * Usage: test_pad_values */

#include "../libretro_pad_values.h"
#include "test_common.h"

int main(void)
{
   struct { int16_t pressure[LR_PAD_BUTTONS]; int16_t pressed[LR_PAD_BUTTONS]; int16_t stick[LR_PAD_AXES]; } in;
   struct lr_pad_values out;
   int v, i;
   unsigned last = 0, reached = 0;
   char seen[256];

   memset(&in, 0, sizeof(in));
   memset(seen, 0, sizeof(seen));

   for (v = -32768; v <= 32767; v++)
   {
      struct lr_pad_values mirror;
      /* the nearest of 0..255 to (v + 32768) * 255 / 65535 */
      const double exact = ((double)v + 32768.0) * 255.0 / 65535.0;
      in.stick[0] = (int16_t)v;
      in.stick[3] = (int16_t)(-v - 1);
      lr_pad_values_read(in.pressure, in.pressed, in.stick, &out);
      mirror = out;
      TEST_CHECK(out.axis[0] >= last);
      last = out.axis[0];
      TEST_CHECK((double)out.axis[0] - exact <= 0.5 && exact - (double)out.axis[0] <= 0.5);
      TEST_CHECK(out.axis[0] + mirror.axis[3] == 255);
      if (!seen[out.axis[0]])
      {
         seen[out.axis[0]] = 1;
         reached++;
      }
   }
   TEST_CHECK(reached == 256);
   in.stick[0] = -32768; lr_pad_values_read(in.pressure, in.pressed, in.stick, &out); TEST_CHECK(out.axis[0] == 0);
   in.stick[0] = 0;      lr_pad_values_read(in.pressure, in.pressed, in.stick, &out); TEST_CHECK(out.axis[0] == 128);
   in.stick[0] = 32767;  lr_pad_values_read(in.pressure, in.pressed, in.stick, &out); TEST_CHECK(out.axis[0] == 255);

   last = 0;
   for (v = 1; v <= 32767; v++)
   {
      const double exact = (double)v * 255.0 / 32767.0;
      in.pressure[13] = (int16_t)v;
      lr_pad_values_read(in.pressure, in.pressed, in.stick, &out);
      TEST_CHECK(out.button[13] >= 1);
      TEST_CHECK(out.button[13] >= last);
      last = out.button[13];
      TEST_CHECK(out.button[13] == 1 || ((double)out.button[13] - exact <= 0.5 && exact - (double)out.button[13] <= 0.5));
   }
   in.pressure[13] = 0x4000; lr_pad_values_read(in.pressure, in.pressed, in.stick, &out); TEST_CHECK(out.button[13] == 128);
   in.pressure[13] = 0x7fff; lr_pad_values_read(in.pressure, in.pressed, in.stick, &out); TEST_CHECK(out.button[13] == 255);
   in.pressure[13] = 0;

   for (i = 0; i < LR_PAD_BUTTONS; i++)
   {
      int j;
      memset(in.pressed, 0, sizeof(in.pressed));
      in.pressed[i] = 1;
      lr_pad_values_read(in.pressure, in.pressed, in.stick, &out);
      for (j = 0; j < LR_PAD_BUTTONS; j++)
         TEST_CHECK(out.button[j] == (j == i ? 255 : 0));
   }

   printf("pad values: 65536 stick positions and 32767 pressures, %u of 256 axis values reached; %s\n",
         reached, test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
