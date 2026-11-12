#pragma once

#include "libretro.h"

#include <array>
#include <cstdint>
#include <functional>

// Number of controller ports
constexpr unsigned LIBRETRO_MAX_PADS = 7;

// Number of keyboard keys to track
constexpr unsigned LIBRETRO_MAX_KEYS = 320;

// Libretro input state structure for controllers
struct LibretroInputState
{
    std::array<int16_t, 16> buttons{};  // RETRO_DEVICE_ID_JOYPAD_*
    std::array<int16_t, 16> pressure{}; // the same as analog buttons, 0..0x7fff; 0 where the frontend has none
    std::array<int16_t, 4> analog{};    // Left X, Left Y, Right X, Right Y
    bool connected = false;
};

// Libretro mouse state
struct LibretroMouseState
{
    int16_t x = 0;           // Relative X movement
    int16_t y = 0;           // Relative Y movement
    int16_t abs_x = 0;       // Absolute X position (pointer)
    int16_t abs_y = 0;       // Absolute Y position (pointer)
    bool left = false;       // Left button
    bool right = false;      // Right button
    bool middle = false;     // Middle button
    bool button4 = false;    // Button 4
    bool button5 = false;    // Button 5
    int8_t wheel_v = 0;      // Vertical wheel
    int8_t wheel_h = 0;      // Horizontal wheel
};

// A port's light gun, as RetroArch reports it (RETRO_DEVICE_LIGHTGUN); the
// mouse handler hands it to the game as that player's PS3 mouse
struct LibretroGunState
{
    int16_t x = 0;           // -0x7fff (left edge) .. 0x7fff (right edge)
    int16_t y = 0;           // -0x7fff (top edge) .. 0x7fff (bottom edge)
    bool offscreen = true;   // Aimed off the screen, or no gun
    bool trigger = false;
    bool aux_a = false;
    bool aux_b = false;
    bool start = false;
    int8_t wheel = 0;        // The port's mouse wheel, +1 up, -1 down
};

// Ports whose light gun is read; the USIO arcade board has four players
constexpr unsigned LIBRETRO_MAX_GUNS = 4;

// Libretro keyboard state
struct LibretroKeyboardState
{
    std::array<bool, LIBRETRO_MAX_KEYS> keys{};  // Key states indexed by RETROK_*
};

// Initialize libretro input system
void libretro_input_init();

// Deinitialize libretro input system
void libretro_input_deinit();

// Poll input state from libretro frontend
void libretro_input_poll(retro_input_state_t input_state_cb);

// Configure whether the frontend supports RETRO_DEVICE_ID_JOYPAD_MASK bitmask polling
void libretro_input_set_bitmask_supported(bool supported);

// Set controller type for a port
void libretro_input_set_controller(unsigned port, unsigned device);

// Get button state for a specific port and button
bool libretro_input_get_button(unsigned port, unsigned button);

// Get analog axis value for a specific port, stick, and axis
int16_t libretro_input_get_analog(unsigned port, unsigned index, unsigned id);

// Get current input state for a port
const LibretroInputState& libretro_input_get_state(unsigned port);

// Get mouse state
const LibretroMouseState& libretro_input_get_mouse();

// Get a port's light gun
const LibretroGunState& libretro_input_get_gun(unsigned port);

// System 357 shooter games (USIO Mode "Shooter Games") read the arcade
// board's players 3 and 4 as their P1 and P2. When on, RetroArch's ports 1-2
// and 3-4 trade places for pads and guns alike, and a gun's trigger, second
// trigger and start also press D-Pad Left, D-Pad Right and Square, the pad
// buttons the board reads as those in that mode.
void libretro_input_set_usio_shooter(bool enabled);

// The RetroArch port that drives RPCS3's pad and mouse slot `player`
unsigned libretro_input_port_for_player(unsigned player);

// Get keyboard state
const LibretroKeyboardState& libretro_input_get_keyboard();

// Check if a specific key is pressed
bool libretro_input_key_pressed(unsigned keycode);

// Set up default input descriptors for RetroArch
void libretro_input_set_descriptors(retro_environment_t environ_cb);

// Set up controller info for RetroArch
void libretro_input_set_controller_info(retro_environment_t environ_cb);

// Initialize sensor interface for gyro/accelerometer
bool libretro_input_init_sensors(retro_environment_t environ_cb);

// Get the frontend's rumble interface
bool libretro_input_init_rumble(retro_environment_t environ_cb);

// Set a port's motors (0..65535); only changes reach the frontend
void libretro_input_set_rumble(unsigned port, uint16_t strong, uint16_t weak);

// Stop every port's motors
void libretro_input_stop_rumble();

// Poll sensor data (gyro/accelerometer)
void libretro_input_poll_sensors();

// Get gyro data for a port (returns angular velocity in rad/s)
void libretro_input_get_gyro(unsigned port, float& x, float& y, float& z);

// Get accelerometer data for a port (returns acceleration in g)
void libretro_input_get_accel(unsigned port, float& x, float& y, float& z);
