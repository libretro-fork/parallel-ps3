/* The frontend's pad readings as a DualShock 3's values: each button's
 * pressure and each stick axis, 0 to 255, at the precision the frontend
 * gave them. C89; no allocation, nothing kept.
 *
 * A DualShock 3's buttons and triggers are pressure-sensitive. A button the
 * frontend reports as analog (RETRO_DEVICE_INDEX_ANALOG_BUTTON, 0 to 0x7fff)
 * is its pressure, rounded, and at least 1 while there is any; one it has
 * only as pressed or not is 255 or 0. A stick's 16 bits become the pad's 8
 * in one step, rounded: -0x8000 is 0, 0 is 128, 0x7fff is 255. */

#ifndef LIBRETRO_PAD_VALUES_H
#define LIBRETRO_PAD_VALUES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LR_PAD_BUTTONS 16 /* RETRO_DEVICE_ID_JOYPAD_B .. _R3, by their IDs */
#define LR_PAD_AXES    4  /* left x, left y, right x, right y */

struct lr_pad_values
{
   uint8_t button[LR_PAD_BUTTONS];
   uint8_t axis[LR_PAD_AXES];
};

/* @pressure: each button as the frontend's analog button, 0 where it has
 * none; @pressed: each as pressed (non-zero) or not; @stick: the four
 * axes. All three borrowed for the call: read where the frontend's answers
 * already are, not gathered into a buffer first. */
void lr_pad_values_read(const int16_t *pressure, const int16_t *pressed,
      const int16_t *stick, struct lr_pad_values *out);

#ifdef __cplusplus
}
#endif

#endif
