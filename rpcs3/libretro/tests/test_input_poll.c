/* What a retro_run reads from the frontend's input devices
 * (libretro/libretro_input.c):
 *
 * - only what something reads afterwards: a pad's buttons, pressures and
 *   sticks, a gun and its port's mouse wheel. No keyboard, no pointer, no
 *   mouse movement or buttons: every call counted, by device;
 * - each value where the handlers look for it, with the buttons as one mask
 *   or one by one;
 * - a port set to no device is not asked about; a port there is not reads as
 *   a pad at rest and no gun; no gun before the first poll;
 * - the arcade shooter mode: ports 1-2 and 3-4 trade places, the gun's
 *   triggers and start press the pad buttons the board reads;
 * - the motors: the frontend hears of changes only, a motor it refused is
 *   asked again, a later refusal of the interface keeps the earlier one;
 * - the button names and the ports' devices handed to the frontend.
 *
 * Usage: test_input_poll */

#include "../libretro_input.h"
#include "test_common.h"

static unsigned s_calls[8];
static unsigned s_total;
static unsigned s_gun_offscreen_port = 99;

/* Every answer its own, from what was asked */
static int16_t answer(unsigned port, unsigned device, unsigned index, unsigned id)
{
   return (int16_t)(1000 * (port + 1) + 100 * index + id + 7 * device);
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   s_total++;
   if (device < 8)
      s_calls[device]++;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return (int16_t)(0x5a5a + port);
         return (int16_t)(((0x5a5a + port) >> id) & 1);
      case RETRO_DEVICE_ANALOG:
         return answer(port, device, index, id);
      case RETRO_DEVICE_LIGHTGUN:
         switch (id)
         {
            case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN: return port == s_gun_offscreen_port;
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:     return (int16_t)(-3000 - (int)port);
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:     return (int16_t)(4000 + port);
            case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:      return (port & 1) == 0;
            case RETRO_DEVICE_ID_LIGHTGUN_AUX_A:        return (port & 1) == 1;
            case RETRO_DEVICE_ID_LIGHTGUN_AUX_B:        return port == 2;
            case RETRO_DEVICE_ID_LIGHTGUN_START:        return port == 3;
            default: break;
         }
         TEST_CHECK(!"a gun id nothing reads");
         return 0;
      case RETRO_DEVICE_MOUSE:
         /* Only the wheel, for the guns' ports */
         TEST_CHECK(port < LIBRETRO_MAX_GUNS);
         if (id == RETRO_DEVICE_ID_MOUSE_WHEELUP)
            return port == 1;
         if (id == RETRO_DEVICE_ID_MOUSE_WHEELDOWN)
            return port == 2;
         TEST_CHECK(!"a mouse id nothing reads");
         return 0;
      default:
         break;
   }
   TEST_CHECK(!"a device nothing reads");
   return 0;
}

static void poll(void)
{
   memset(s_calls, 0, sizeof(s_calls));
   s_total = 0;
   libretro_input_poll(input_state);
}

static void check_pad(unsigned port)
{
   const struct LibretroInputState *pad = libretro_input_get_state(port);
   unsigned id;

   TEST_CHECK(pad->connected);
   for (id = 0; id < 16; id++)
   {
      const int pressure_sensitive = id != RETRO_DEVICE_ID_JOYPAD_SELECT
         && id != RETRO_DEVICE_ID_JOYPAD_START
         && id != RETRO_DEVICE_ID_JOYPAD_L3 && id != RETRO_DEVICE_ID_JOYPAD_R3;
      TEST_CHECK(pad->buttons[id] == (int16_t)(((0x5a5a + port) >> id) & 1));
      TEST_CHECK(pad->pressure[id] == (pressure_sensitive
            ? answer(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, id) : 0));
   }
   TEST_CHECK(pad->analog[0] == answer(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X));
   TEST_CHECK(pad->analog[1] == answer(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y));
   TEST_CHECK(pad->analog[2] == answer(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
   TEST_CHECK(pad->analog[3] == answer(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));
}

/* The motors */
static bool s_have_rumble = true;
static unsigned s_refuse_effect = 99;
static unsigned s_rumble_calls;
static uint16_t s_motor[LIBRETRO_MAX_PADS][2];

static bool set_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
   s_rumble_calls++;
   if ((unsigned)effect == s_refuse_effect)
      return false;
   s_motor[port][effect == RETRO_RUMBLE_WEAK] = strength;
   return true;
}

static const struct retro_input_descriptor *s_desc;
static const struct retro_controller_info *s_ports;

static bool environment(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
         if (!s_have_rumble)
            return false;
         ((struct retro_rumble_interface*)data)->set_rumble_state = set_rumble;
         return true;
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
         s_desc = (const struct retro_input_descriptor*)data;
         return true;
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         s_ports = (const struct retro_controller_info*)data;
         return true;
      default:
         break;
   }
   return false;
}

int main(void)
{
   unsigned port, n;
   unsigned calls_idle, calls_mask, calls_each;
   const struct LibretroGunState *gun;

   /* Before anything: no gun, and a poll asks nothing */
   TEST_CHECK(libretro_input_get_gun(0)->offscreen);
   poll();
   TEST_CHECK(s_total == 0);

   libretro_input_init();
   TEST_CHECK(libretro_input_get_gun(0)->offscreen);
   TEST_CHECK(libretro_input_get_state(0)->connected);

   /* Nothing reads the guns (no PS3 mice, as in most games): the frontend
    * is not asked about them, and there is none */
   libretro_input_set_bitmask_supported(true);
   poll();
   calls_idle = s_total;
   TEST_CHECK(s_calls[RETRO_DEVICE_LIGHTGUN] == 0 && s_calls[RETRO_DEVICE_MOUSE] == 0);
   TEST_CHECK(s_total == LIBRETRO_MAX_PADS * 17);
   TEST_CHECK(!libretro_input_guns_polled());
   TEST_CHECK(libretro_input_get_gun(0)->offscreen && !libretro_input_get_gun(0)->trigger);
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      check_pad(port);

   /* Something reads them. The buttons as one mask */
   libretro_input_set_guns_read(true);
   poll();
   TEST_CHECK(libretro_input_guns_polled());
   calls_mask = s_total;
   TEST_CHECK(s_calls[RETRO_DEVICE_JOYPAD]   == LIBRETRO_MAX_PADS);
   TEST_CHECK(s_calls[RETRO_DEVICE_ANALOG]   == LIBRETRO_MAX_PADS * (12 + 4));
   TEST_CHECK(s_calls[RETRO_DEVICE_LIGHTGUN] == LIBRETRO_MAX_GUNS * 7);
   TEST_CHECK(s_calls[RETRO_DEVICE_MOUSE]    == LIBRETRO_MAX_GUNS * 2);
   TEST_CHECK(s_calls[RETRO_DEVICE_KEYBOARD] == 0);
   TEST_CHECK(s_calls[RETRO_DEVICE_POINTER]  == 0);
   TEST_CHECK(s_total == LIBRETRO_MAX_PADS * 17 + LIBRETRO_MAX_GUNS * 9);
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      check_pad(port);

   /* One by one */
   libretro_input_set_bitmask_supported(false);
   poll();
   calls_each = s_total;
   TEST_CHECK(s_calls[RETRO_DEVICE_JOYPAD] == LIBRETRO_MAX_PADS * 16);
   TEST_CHECK(s_total == LIBRETRO_MAX_PADS * 32 + LIBRETRO_MAX_GUNS * 9);
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      check_pad(port);
   libretro_input_set_bitmask_supported(true);

   /* The guns */
   for (port = 0; port < LIBRETRO_MAX_GUNS; port++)
   {
      gun = libretro_input_get_gun(port);
      TEST_CHECK(!gun->offscreen);
      TEST_CHECK(gun->x == (int16_t)(-3000 - (int)port) && gun->y == (int16_t)(4000 + port));
      TEST_CHECK(gun->trigger == ((port & 1) == 0));
      TEST_CHECK(gun->aux_a == ((port & 1) == 1));
      TEST_CHECK(gun->aux_b == (port == 2));
      TEST_CHECK(gun->start == (port == 3));
      TEST_CHECK(gun->wheel == (port == 1 ? 1 : port == 2 ? -1 : 0));
   }

   /* Aimed off the screen: where it was stays, and is not asked for */
   s_gun_offscreen_port = 1;
   poll();
   gun = libretro_input_get_gun(1);
   TEST_CHECK(gun->offscreen && gun->x == -3001 && gun->y == 4001);
   TEST_CHECK(s_calls[RETRO_DEVICE_LIGHTGUN] == LIBRETRO_MAX_GUNS * 7 - 2);
   s_gun_offscreen_port = 99;

   /* A port there is not */
   TEST_CHECK(!libretro_input_get_state(LIBRETRO_MAX_PADS)->connected);
   TEST_CHECK(libretro_input_get_state(LIBRETRO_MAX_PADS)->buttons[0] == 0);
   TEST_CHECK(libretro_input_get_gun(LIBRETRO_MAX_GUNS)->offscreen);
   TEST_CHECK(!libretro_input_get_gun(LIBRETRO_MAX_GUNS)->trigger);

   /* A port set to no device: not asked about, not connected */
   libretro_input_set_controller(2, RETRO_DEVICE_NONE);
   TEST_CHECK(!libretro_input_get_state(2)->connected);
   poll();
   TEST_CHECK(s_calls[RETRO_DEVICE_JOYPAD] == LIBRETRO_MAX_PADS - 1);
   TEST_CHECK(s_calls[RETRO_DEVICE_ANALOG] == (LIBRETRO_MAX_PADS - 1) * 16);
   TEST_CHECK(!libretro_input_get_state(2)->connected);
   libretro_input_set_controller(2, RETRO_DEVICE_JOYPAD);
   TEST_CHECK(libretro_input_get_state(2)->connected);
   libretro_input_set_controller(LIBRETRO_MAX_PADS, RETRO_DEVICE_NONE); /* nothing */

   /* The arcade shooter mode */
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      TEST_CHECK(libretro_input_port_for_player(port) == port);
   libretro_input_set_usio_shooter(true);
   TEST_CHECK(libretro_input_port_for_player(0) == 2 && libretro_input_port_for_player(1) == 3);
   TEST_CHECK(libretro_input_port_for_player(2) == 0 && libretro_input_port_for_player(3) == 1);
   TEST_CHECK(libretro_input_port_for_player(4) == 4 && libretro_input_port_for_player(6) == 6);
   poll();
   for (port = 0; port < LIBRETRO_MAX_GUNS; port++)
   {
      const struct LibretroInputState *pad = libretro_input_get_state(port);
      const unsigned mask = 0x5a5a + port;
      gun = libretro_input_get_gun(port);
      TEST_CHECK(pad->buttons[RETRO_DEVICE_ID_JOYPAD_LEFT]
            == (gun->trigger || ((mask >> RETRO_DEVICE_ID_JOYPAD_LEFT) & 1)));
      TEST_CHECK(pad->buttons[RETRO_DEVICE_ID_JOYPAD_RIGHT]
            == (gun->aux_a || ((mask >> RETRO_DEVICE_ID_JOYPAD_RIGHT) & 1)));
      TEST_CHECK(pad->buttons[RETRO_DEVICE_ID_JOYPAD_Y]
            == (gun->start || ((mask >> RETRO_DEVICE_ID_JOYPAD_Y) & 1)));
   }
   /* 0x5a5a has neither Left (bit 6: set) - so look at one that changes */
   TEST_CHECK(libretro_input_get_state(3)->buttons[RETRO_DEVICE_ID_JOYPAD_Y] == 1);
   libretro_input_set_usio_shooter(false);
   poll();
   TEST_CHECK(libretro_input_get_state(3)->buttons[RETRO_DEVICE_ID_JOYPAD_Y]
         == (int16_t)(((0x5a5a + 3) >> RETRO_DEVICE_ID_JOYPAD_Y) & 1));

   /* After deinit a poll asks nothing */
   libretro_input_deinit();
   poll();
   TEST_CHECK(s_total == 0);
   libretro_input_init();

   /* The motors: nothing without the interface */
   libretro_input_set_rumble(0, 100, 200);
   TEST_CHECK(s_rumble_calls == 0);
   s_have_rumble = false;
   TEST_CHECK(!libretro_input_init_rumble(environment));
   TEST_CHECK(!libretro_input_init_rumble(NULL));
   s_have_rumble = true;
   TEST_CHECK(libretro_input_init_rumble(environment));
   /* A later refusal keeps it */
   s_have_rumble = false;
   TEST_CHECK(libretro_input_init_rumble(environment));
   TEST_CHECK(libretro_input_init_rumble(NULL));

   libretro_input_set_rumble(1, 0x8000, 0xffff);
   TEST_CHECK(s_rumble_calls == 2 && s_motor[1][0] == 0x8000 && s_motor[1][1] == 0xffff);
   for (n = 0; n < 1000; n++)
      libretro_input_set_rumble(1, 0x8000, 0xffff);
   TEST_CHECK(s_rumble_calls == 2);
   libretro_input_set_rumble(1, 0x8000, 0);
   TEST_CHECK(s_rumble_calls == 3 && s_motor[1][1] == 0);
   libretro_input_set_rumble(LIBRETRO_MAX_PADS, 1, 1);
   TEST_CHECK(s_rumble_calls == 3);

   /* A motor the frontend refused is asked again; the other is not */
   s_refuse_effect = RETRO_RUMBLE_STRONG;
   libretro_input_set_rumble(1, 0x4000, 0x1234);
   TEST_CHECK(s_rumble_calls == 5 && s_motor[1][0] == 0x8000 && s_motor[1][1] == 0x1234);
   s_refuse_effect = 99;
   libretro_input_set_rumble(1, 0x4000, 0x1234);
   TEST_CHECK(s_rumble_calls == 6 && s_motor[1][0] == 0x4000);

   libretro_input_set_rumble(5, 7, 9);
   libretro_input_stop_rumble();
   for (port = 0; port < LIBRETRO_MAX_PADS; port++)
      TEST_CHECK(s_motor[port][0] == 0 && s_motor[port][1] == 0);
   TEST_CHECK(s_rumble_calls == 6 + 2 + 4);

   /* What the frontend is told */
   libretro_input_set_descriptors(environment);
   libretro_input_set_controller_info(environment);
   TEST_CHECK(s_desc && s_ports);
   if (s_desc && s_ports)
   {
      for (n = 0; s_desc[n].description; n++)
      {
         TEST_CHECK(s_desc[n].port == n / 20);
         TEST_CHECK(!strcmp(s_desc[n].description, s_desc[n % 20].description));
      }
      TEST_CHECK(n == 40);
      TEST_CHECK(!strcmp(s_desc[0].description, "Cross") && s_desc[0].id == RETRO_DEVICE_ID_JOYPAD_B);
      TEST_CHECK(!strcmp(s_desc[13].description, "R2 / Arcade Service") && s_desc[13].id == RETRO_DEVICE_ID_JOYPAD_R2);
      TEST_CHECK(s_desc[19].device == RETRO_DEVICE_ANALOG && s_desc[19].index == RETRO_DEVICE_INDEX_ANALOG_RIGHT
            && s_desc[19].id == RETRO_DEVICE_ID_ANALOG_Y);
      for (n = 0; s_ports[n].types; n++)
      {
         TEST_CHECK(s_ports[n].num_types == 2);
         TEST_CHECK(s_ports[n].types[0].id == RETRO_DEVICE_JOYPAD && s_ports[n].types[1].id == RETRO_DEVICE_NONE);
      }
      TEST_CHECK(n == LIBRETRO_MAX_PADS);
   }

   /* Nothing reads the guns any more: not asked about, and none left
    * where the last poll had them */
   libretro_input_init();
   libretro_input_set_guns_read(true);
   poll();
   TEST_CHECK(!libretro_input_get_gun(0)->offscreen);
   libretro_input_set_guns_read(false);
   poll();
   TEST_CHECK(s_calls[RETRO_DEVICE_LIGHTGUN] == 0 && s_calls[RETRO_DEVICE_MOUSE] == 0);
   TEST_CHECK(libretro_input_get_gun(0)->offscreen && !libretro_input_get_gun(0)->trigger && libretro_input_get_gun(0)->x == 0);
   /* The arcade shooter mode reads them whatever the mice */
   libretro_input_set_usio_shooter(true);
   poll();
   TEST_CHECK(s_calls[RETRO_DEVICE_LIGHTGUN] == LIBRETRO_MAX_GUNS * 7);
   libretro_input_set_usio_shooter(false);

   printf("test_input_poll: %u frontend calls a run with nothing reading the guns, %u with the PS3's mice set up, %u with the buttons one by one; %s\n",
         calls_idle, calls_mask, calls_each, test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
