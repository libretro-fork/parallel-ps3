/* What the frontend's input devices report, read once each retro_run and
 * kept for the pad and mouse handlers; the pads' motors, the other way.
 *
 * Read and written on retro_run's thread only. C89; no C++ behind it. */

#ifndef LIBRETRO_INPUT_H
#define LIBRETRO_INPUT_H

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Controller ports */
#define LIBRETRO_MAX_PADS 7

/* Ports whose light gun is read; the USIO arcade board has four players */
#define LIBRETRO_MAX_GUNS 4

/* A port's pad */
struct LibretroInputState
{
   int16_t buttons[16];  /* RETRO_DEVICE_ID_JOYPAD_*: pressed or not */
   int16_t pressure[16]; /* the same as analog buttons, 0..0x7fff; 0 where
                          * the frontend has none */
   int16_t analog[4];    /* left X, left Y, right X, right Y */
   bool connected;
};

/* A port's light gun, as RetroArch reports it (RETRO_DEVICE_LIGHTGUN); the
 * mouse handler hands it to the game as that player's PS3 mouse */
struct LibretroGunState
{
   int16_t x;      /* -0x7fff (left edge) .. 0x7fff (right edge) */
   int16_t y;      /* -0x7fff (top edge) .. 0x7fff (bottom edge) */
   bool offscreen; /* aimed off the screen, or no gun */
   bool trigger;
   bool aux_a;
   bool aux_b;
   bool start;
   int8_t wheel;   /* the port's mouse wheel, +1 up, -1 down */
};

/* Every port a connected pad with nothing pressed, no gun */
void libretro_input_init(void);

/* Polls do nothing until the next libretro_input_init */
void libretro_input_deinit(void);

/* Reads the pads and the guns from the frontend */
void libretro_input_poll(retro_input_state_t input_state_cb);

/* The frontend answers RETRO_DEVICE_ID_JOYPAD_MASK: one call for a pad's
 * sixteen buttons */
void libretro_input_set_bitmask_supported(bool supported);

/* A port's device, from retro_set_controller_port_device */
void libretro_input_set_controller(unsigned port, unsigned device);

/* A port's pad, as last polled; a pad at rest for a port there is not.
 * Borrowed: good until the next poll */
const struct LibretroInputState *libretro_input_get_state(unsigned port);

/* A port's light gun, as last polled; no gun for a port there is not */
const struct LibretroGunState *libretro_input_get_gun(unsigned port);

/* System 357 shooter games (USIO Mode "Shooter Games") read the arcade
 * board's players 3 and 4 as their P1 and P2. When on, RetroArch's ports 1-2
 * and 3-4 trade places for pads and guns alike, and a gun's trigger, second
 * trigger and start also press D-Pad Left, D-Pad Right and Square, the pad
 * buttons the board reads as those in that mode. */
void libretro_input_set_usio_shooter(bool enabled);

/* The RetroArch port that drives RPCS3's pad and mouse slot `player` */
unsigned libretro_input_port_for_player(unsigned player);

/* The names RetroArch shows for the buttons */
void libretro_input_set_descriptors(retro_environment_t environ_cb);

/* The devices a port can be set to */
void libretro_input_set_controller_info(retro_environment_t environ_cb);

/* Gets the frontend's rumble interface; false when there is none. A refusal
 * keeps what an earlier call got. */
bool libretro_input_init_rumble(retro_environment_t environ_cb);

/* Sets a port's motors (0..65535); only changes reach the frontend */
void libretro_input_set_rumble(unsigned port, uint16_t strong, uint16_t weak);

/* Stops every port's motors */
void libretro_input_stop_rumble(void);

#ifdef __cplusplus
}
#endif

#endif
