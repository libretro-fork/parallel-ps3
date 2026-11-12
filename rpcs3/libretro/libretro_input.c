/* See libretro_input.h. C89. */

#include <string.h>

#include "libretro_input.h"

static struct LibretroInputState s_input_states[LIBRETRO_MAX_PADS];
/* No gun until the first poll */
#define LR_NO_GUN { 0, 0, true, false, false, false, false, 0 }
static struct LibretroGunState s_gun_states[LIBRETRO_MAX_GUNS] = {
   LR_NO_GUN, LR_NO_GUN, LR_NO_GUN, LR_NO_GUN
};
static unsigned s_device_types[LIBRETRO_MAX_PADS];
static bool s_usio_shooter;
static bool s_initialized;
static bool s_bitmask_supported;
static bool s_guns_read;
static bool s_guns_polled;

/* What a port there is not reads as: nothing pressed, no gun */
static const struct LibretroInputState s_no_pad;
static const struct LibretroGunState s_no_gun = LR_NO_GUN;

static retro_set_rumble_state_t s_rumble_cb;
/* What each port's motors were last set to, so the frontend is called on a
 * change only - retro_run asks every frame. */
static uint16_t s_rumble_sent[LIBRETRO_MAX_PADS][2];

/* The pressure-sensitive buttons of a DualShock 3. Select, Start, L3 and R3
 * are switches on the pad too. */
static const unsigned char s_pressure_ids[12] = {
   RETRO_DEVICE_ID_JOYPAD_B,    RETRO_DEVICE_ID_JOYPAD_Y,
   RETRO_DEVICE_ID_JOYPAD_A,    RETRO_DEVICE_ID_JOYPAD_X,
   RETRO_DEVICE_ID_JOYPAD_UP,   RETRO_DEVICE_ID_JOYPAD_DOWN,
   RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
   RETRO_DEVICE_ID_JOYPAD_L,    RETRO_DEVICE_ID_JOYPAD_R,
   RETRO_DEVICE_ID_JOYPAD_L2,   RETRO_DEVICE_ID_JOYPAD_R2
};

void libretro_input_init(void)
{
   unsigned port;

   memset(s_input_states, 0, sizeof(s_input_states));
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
   {
      s_input_states[port].connected = true;
      s_device_types[port]           = RETRO_DEVICE_JOYPAD;
   }
   for (port = 0; port < LIBRETRO_MAX_GUNS; port++)
      s_gun_states[port] = s_no_gun;

   s_guns_read   = false;
   s_guns_polled = false;
   s_initialized = true;
}

void libretro_input_set_bitmask_supported(bool supported)
{
   s_bitmask_supported = supported;
}

void libretro_input_deinit(void)
{
   s_initialized = false;
}

void libretro_input_poll(retro_input_state_t input_state_cb)
{
   unsigned port, i;

   if (!s_initialized || !input_state_cb)
      return;

   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
   {
      struct LibretroInputState *state = &s_input_states[port];

      if (s_device_types[port] == RETRO_DEVICE_NONE)
      {
         state->connected = false;
         continue;
      }

      state->connected = true;

      /* The sixteen buttons in one call where the frontend has that */
      if (s_bitmask_supported)
      {
         unsigned mask = (uint16_t)input_state_cb(port, RETRO_DEVICE_JOYPAD, 0,
               RETRO_DEVICE_ID_JOYPAD_MASK);
         for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
            state->buttons[i] = (int16_t)((mask >> i) & 1);
      }
      else
      {
         for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
            state->buttons[i] = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, i);
      }

      /* How far each pressure-sensitive button is pressed, where the
       * frontend has it as analog (a trigger, mostly): the game gets the
       * pressure, not pressed or not. */
      for (i = 0; i < sizeof(s_pressure_ids); i++)
      {
         unsigned id = s_pressure_ids[i];
         state->pressure[id] = input_state_cb(port, RETRO_DEVICE_ANALOG,
               RETRO_DEVICE_INDEX_ANALOG_BUTTON, id);
      }

      state->analog[0] = input_state_cb(port, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
      state->analog[1] = input_state_cb(port, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
      state->analog[2] = input_state_cb(port, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
      state->analog[3] = input_state_cb(port, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
   }

   /* Light guns, while something reads them */
   if (!s_guns_read && !s_usio_shooter)
   {
      if (s_guns_polled)
      {
         for (port = 0; port < LIBRETRO_MAX_GUNS; port++)
            s_gun_states[port] = s_no_gun;
         s_guns_polled = false;
      }
      return;
   }

   s_guns_polled = true;

   /* RetroArch answers these for every port, from a real gun or from the
    * mouse it binds as one, whatever device the port is set to. */
   for (port = 0; port < LIBRETRO_MAX_GUNS; port++)
   {
      struct LibretroGunState *gun = &s_gun_states[port];

      gun->offscreen = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
            RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) != 0;
      if (!gun->offscreen)
      {
         gun->x = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
               RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
         gun->y = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
               RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
      }
      gun->trigger = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
            RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) != 0;
      gun->aux_a   = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
            RETRO_DEVICE_ID_LIGHTGUN_AUX_A) != 0;
      gun->aux_b   = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
            RETRO_DEVICE_ID_LIGHTGUN_AUX_B) != 0;
      gun->start   = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0,
            RETRO_DEVICE_ID_LIGHTGUN_START) != 0;
      gun->wheel   = (int8_t)(
              (input_state_cb(port, RETRO_DEVICE_MOUSE, 0,
                    RETRO_DEVICE_ID_MOUSE_WHEELUP) ? 1 : 0)
            - (input_state_cb(port, RETRO_DEVICE_MOUSE, 0,
                    RETRO_DEVICE_ID_MOUSE_WHEELDOWN) ? 1 : 0));

      /* The arcade board reads, in shooter mode, D-Pad Left and Right as the
       * gun's triggers and Square as its start (usio.cpp, the original
       * mouse mapping of RPCS3 PR #19673) */
      if (s_usio_shooter && s_device_types[port] != RETRO_DEVICE_NONE)
      {
         struct LibretroInputState *state = &s_input_states[port];
         if (gun->trigger)
            state->buttons[RETRO_DEVICE_ID_JOYPAD_LEFT]  = 1;
         if (gun->aux_a)
            state->buttons[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1;
         if (gun->start)
            state->buttons[RETRO_DEVICE_ID_JOYPAD_Y]     = 1;
      }
   }
}

void libretro_input_set_guns_read(bool read)
{
   s_guns_read = read;
}

bool libretro_input_guns_polled(void)
{
   return s_guns_polled;
}

void libretro_input_set_controller(unsigned port, unsigned device)
{
   if (port < LIBRETRO_MAX_PADS)
   {
      s_device_types[port]           = device;
      s_input_states[port].connected = (device != RETRO_DEVICE_NONE);
   }
}

const struct LibretroInputState *libretro_input_get_state(unsigned port)
{
   return port < LIBRETRO_MAX_PADS ? &s_input_states[port] : &s_no_pad;
}

const struct LibretroGunState *libretro_input_get_gun(unsigned port)
{
   return port < LIBRETRO_MAX_GUNS ? &s_gun_states[port] : &s_no_gun;
}

void libretro_input_set_usio_shooter(bool enabled)
{
   s_usio_shooter = enabled;
}

unsigned libretro_input_port_for_player(unsigned player)
{
   /* Ports 1-2 and 3-4 trade places: player 3 (index 2) is RetroArch's
    * port 1 */
   if (s_usio_shooter && player < 4)
      return player ^ 2;

   return player;
}

/* RetroPad buttons as the DualShock 3's, and what the System 357/369 arcade
 * board reads some of them as (usio_config.h defaults) */
#define LR_PAD_DESCRIPTORS(port) \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Cross" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Square" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select / Arcade Test" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Circle" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Triangle" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L1 / Arcade Card" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R1" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "L2 / Arcade Coin" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "R2 / Arcade Service" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "L3" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "R3" }, \
   { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" }, \
   { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" }, \
   { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" }, \
   { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" }

void libretro_input_set_descriptors(retro_environment_t environ_cb)
{
   static const struct retro_input_descriptor desc[] = {
      LR_PAD_DESCRIPTORS(0),
      LR_PAD_DESCRIPTORS(1),
      { 0, 0, 0, 0, NULL }
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
}

void libretro_input_set_controller_info(retro_environment_t environ_cb)
{
   /* The RetroPad is whatever controller the user has: RetroArch maps it */
   static const struct retro_controller_description controllers[] = {
      { "RetroPad", RETRO_DEVICE_JOYPAD },
      { "None",     RETRO_DEVICE_NONE }
   };
   static const struct retro_controller_info ports[LIBRETRO_MAX_PADS + 1] = {
      { controllers, 2 }, { controllers, 2 }, { controllers, 2 },
      { controllers, 2 }, { controllers, 2 }, { controllers, 2 },
      { controllers, 2 }, { NULL, 0 }
   };

   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

bool libretro_input_init_rumble(retro_environment_t environ_cb)
{
   /* A refusal keeps what an earlier call got. RetroArch calls
    * retro_set_environment again whenever it reads the system info - first
    * with a callback that answers almost nothing, then with its own while
    * ignoring every request - and dropping the interface there left the core
    * without rumble for good: games asked, nothing vibrated (NNshi). */
   struct retro_rumble_interface rumble;

   rumble.set_rumble_state = NULL;
   if (     environ_cb
         && environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble)
         && rumble.set_rumble_state)
      s_rumble_cb = rumble.set_rumble_state;
   return s_rumble_cb != NULL;
}

void libretro_input_set_rumble(unsigned port, uint16_t strong, uint16_t weak)
{
   uint16_t *sent;

   if (!s_rumble_cb || port >= LIBRETRO_MAX_PADS)
      return;

   /* A motor the frontend refused is asked again at the next call */
   sent = s_rumble_sent[port];
   if (sent[0] != strong && s_rumble_cb(port, RETRO_RUMBLE_STRONG, strong))
      sent[0] = strong;
   if (sent[1] != weak && s_rumble_cb(port, RETRO_RUMBLE_WEAK, weak))
      sent[1] = weak;
}

void libretro_input_stop_rumble(void)
{
   unsigned port;
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      libretro_input_set_rumble(port, 0, 0);
}
