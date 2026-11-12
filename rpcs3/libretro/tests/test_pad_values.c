/* The frontend's pad readings as a DualShock 3's values
 * (libretro/libretro_pad_values.c), every input there is:
 *
 * - a stick's 65536 positions: 0 at -0x8000, 128 at rest, 255 at 0x7fff,
 *   never backwards, every one of the pad's 256 values reached, the same
 *   distance either way (v and -v-1 mirror), each the nearest of the 256
 *   (exact rounding against the fraction);
 * - a button's 32767 pressures: 1 at the least, 128 at half, 255 at full,
 *   never backwards, the nearest of 255, never 0 while there is any;
 * - a button with no pressure: 255 pressed, 0 not; each button its own;
 * - a pad's frame: the buttons in the pad's order, the sticks through the
 *   squircle's table, which is exactly the function it was filled from.
 *
 * Usage: test_pad_values */

#include "../libretro_pad_values.h"
#include "test_common.h"

/* A stand-in for RPCS3's squircle: any function of both coordinates */
static void test_squircle(uint16_t *x, uint16_t *y)
{
   const unsigned a = *x, b = *y;
   *x = (uint16_t)((a * 7 + b * 3 + 1) & 0xff);
   *y = (uint16_t)((a ^ (b * 5)) & 0xff);
}

static void test_squircle_other(uint16_t *x, uint16_t *y)
{
   *x = 0;
   *y = 0;
}

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

   /* A pad's frame: its buttons in its own order, a button the frontend
    * has none for 0; the sticks through the squircle's table - passed
    * through until it is filled, then exactly what it was filled from,
    * filled once */
   {
      struct lr_pad_frame frame;
      uint8_t order[LR_PAD_FRAME_BUTTONS];
      unsigned x, y, wrong = 0;

      memset(&in, 0, sizeof(in));
      for (i = 0; i < LR_PAD_FRAME_BUTTONS; i++)
         order[i] = (uint8_t)(i < LR_PAD_BUTTONS ? LR_PAD_BUTTONS - 1 - i : LR_PAD_NO_BUTTON);
      for (i = 0; i < LR_PAD_BUTTONS; i++)
         in.pressure[i] = (int16_t)(i ? (i * 2000) : 0);
      in.pressed[0] = 1;
      in.stick[0]   = (int16_t)(40 * 257 - 32768);
      in.stick[1]   = (int16_t)(200 * 257 - 32768);
      in.stick[2]   = (int16_t)(255 * 257 - 32768);
      in.stick[3]   = (int16_t)(0 * 257 - 32768);

      lr_pad_values_read(in.pressure, in.pressed, in.stick, &out);
      lr_pad_frame_read(in.pressure, in.pressed, in.stick, order, &frame);
      for (i = 0; i < LR_PAD_BUTTONS; i++)
         TEST_CHECK(frame.button[i] == out.button[LR_PAD_BUTTONS - 1 - i]);
      TEST_CHECK(frame.button[LR_PAD_BUTTONS] == 0);
      TEST_CHECK(frame.button[LR_PAD_BUTTONS - 1] == 255);
      /* Not filled yet: as they are */
      TEST_CHECK(frame.axis[0] == 40 && frame.axis[1] == 200 && frame.axis[2] == 255 && frame.axis[3] == 0);

      lr_pad_squircle_fill(NULL);
      lr_pad_squircle_fill(test_squircle);
      lr_pad_squircle_fill(test_squircle_other); /* the first stays */

      for (y = 0; y < 256; y++)
      {
         for (x = 0; x < 256; x++)
         {
            uint8_t sx = (uint8_t)x, sy = (uint8_t)y;
            uint16_t ex = (uint16_t)x, ey = (uint16_t)y;
            lr_pad_squircle(&sx, &sy);
            test_squircle(&ex, &ey);
            wrong += sx != ex || sy != ey;
         }
      }
      TEST_CHECK(wrong == 0);

      lr_pad_frame_read(in.pressure, in.pressed, in.stick, order, &frame);
      {
         uint16_t ex = 40, ey = 200, fx = 255, fy = 0;
         test_squircle(&ex, &ey);
         test_squircle(&fx, &fy);
         TEST_CHECK(frame.axis[0] == ex && frame.axis[1] == ey && frame.axis[2] == fx && frame.axis[3] == fy);
      }
   }

   printf("pad values: 65536 stick positions and 32767 pressures, %u of 256 axis values reached; %s\n",
         reached, test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
