#include "stdafx.h"
#include "libretro_pad_handler.h"
#include "libretro_input.h"
#include "Input/pad_thread.h"
#include "Input/product_info.h"
#include "Emu/Cell/timers.hpp"

LibretroPadHandler::LibretroPadHandler()
    : PadHandlerBase(pad_handler::keyboard)  // Use keyboard type as base since we're a custom handler
{
    m_name_string = "Libretro";
    m_max_devices = LIBRETRO_MAX_PADS;
    m_trigger_threshold = 0;
    m_thumb_threshold = 0;

    // Libretro handles all input, no special hardware features
    b_has_led = false;
    b_has_rgb = false;
    b_has_player_led = false;
    b_has_battery = false;
    b_has_battery_led = false;
    b_has_deadzones = true;
    b_has_rumble = true;   // through the frontend's rumble interface
    b_has_motion = false;
    b_has_config = false;
    b_has_pressure_intensity_button = false;
    b_has_analog_limiter_button = false;

    // Button mapping from libretro to RPCS3
    button_list = {
        { static_cast<u32>(LibretroButton::B),      "Cross" },
        { static_cast<u32>(LibretroButton::A),      "Circle" },
        { static_cast<u32>(LibretroButton::Y),      "Square" },
        { static_cast<u32>(LibretroButton::X),      "Triangle" },
        { static_cast<u32>(LibretroButton::Start),  "Start" },
        { static_cast<u32>(LibretroButton::Select), "Select" },
        { static_cast<u32>(LibretroButton::Up),     "Up" },
        { static_cast<u32>(LibretroButton::Down),   "Down" },
        { static_cast<u32>(LibretroButton::Left),   "Left" },
        { static_cast<u32>(LibretroButton::Right),  "Right" },
        { static_cast<u32>(LibretroButton::L1),     "L1" },
        { static_cast<u32>(LibretroButton::R1),     "R1" },
        { static_cast<u32>(LibretroButton::L2),     "L2" },
        { static_cast<u32>(LibretroButton::R2),     "R2" },
        { static_cast<u32>(LibretroButton::L3),     "L3" },
        { static_cast<u32>(LibretroButton::R3),     "R3" },
        { static_cast<u32>(LibretroButton::LSXNeg), "LS X-" },
        { static_cast<u32>(LibretroButton::LSXPos), "LS X+" },
        { static_cast<u32>(LibretroButton::LSYNeg), "LS Y-" },
        { static_cast<u32>(LibretroButton::LSYPos), "LS Y+" },
        { static_cast<u32>(LibretroButton::RSXNeg), "RS X-" },
        { static_cast<u32>(LibretroButton::RSXPos), "RS X+" },
        { static_cast<u32>(LibretroButton::RSYNeg), "RS Y-" },
        { static_cast<u32>(LibretroButton::RSYPos), "RS Y+" },
    };
}

bool LibretroPadHandler::Init()
{
    m_is_init = true;
    return true;
}

std::vector<pad_list_entry> LibretroPadHandler::list_devices()
{
    std::vector<pad_list_entry> devices;
    for (unsigned i = 0; i < LIBRETRO_MAX_PADS; i++)
    {
        devices.emplace_back(fmt::format("Libretro Pad %d", i + 1), false);
    }
    return devices;
}

void LibretroPadHandler::init_config(cfg_pad* cfg)
{
    if (!cfg) return;

    // Set default button mappings
    cfg->ls_left.def  = button_list.at(static_cast<u32>(LibretroButton::LSXNeg));
    cfg->ls_down.def  = button_list.at(static_cast<u32>(LibretroButton::LSYPos));
    cfg->ls_right.def = button_list.at(static_cast<u32>(LibretroButton::LSXPos));
    cfg->ls_up.def    = button_list.at(static_cast<u32>(LibretroButton::LSYNeg));
    cfg->rs_left.def  = button_list.at(static_cast<u32>(LibretroButton::RSXNeg));
    cfg->rs_down.def  = button_list.at(static_cast<u32>(LibretroButton::RSYPos));
    cfg->rs_right.def = button_list.at(static_cast<u32>(LibretroButton::RSXPos));
    cfg->rs_up.def    = button_list.at(static_cast<u32>(LibretroButton::RSYNeg));
    cfg->start.def    = button_list.at(static_cast<u32>(LibretroButton::Start));
    cfg->select.def   = button_list.at(static_cast<u32>(LibretroButton::Select));
    cfg->ps.def       = "";  // PS button not mapped by default
    cfg->square.def   = button_list.at(static_cast<u32>(LibretroButton::Y));
    cfg->cross.def    = button_list.at(static_cast<u32>(LibretroButton::B));
    cfg->circle.def   = button_list.at(static_cast<u32>(LibretroButton::A));
    cfg->triangle.def = button_list.at(static_cast<u32>(LibretroButton::X));
    cfg->left.def     = button_list.at(static_cast<u32>(LibretroButton::Left));
    cfg->down.def     = button_list.at(static_cast<u32>(LibretroButton::Down));
    cfg->right.def    = button_list.at(static_cast<u32>(LibretroButton::Right));
    cfg->up.def       = button_list.at(static_cast<u32>(LibretroButton::Up));
    cfg->r1.def       = button_list.at(static_cast<u32>(LibretroButton::R1));
    cfg->r2.def       = button_list.at(static_cast<u32>(LibretroButton::R2));
    cfg->r3.def       = button_list.at(static_cast<u32>(LibretroButton::R3));
    cfg->l1.def       = button_list.at(static_cast<u32>(LibretroButton::L1));
    cfg->l2.def       = button_list.at(static_cast<u32>(LibretroButton::L2));
    cfg->l3.def       = button_list.at(static_cast<u32>(LibretroButton::L3));

    cfg->pressure_intensity_button.def = "";
    cfg->analog_limiter_button.def = "";

    // Apply defaults
    cfg->from_default();
}

bool LibretroPadHandler::bindPadToDevice(std::shared_ptr<Pad> pad)
{
    if (!pad)
        return false;

    // Create a device for this pad
    auto device = std::make_shared<PadDevice>();
    m_bindings.emplace_back(pad, device, nullptr);

    // Describe it to games as standalone's gamepad handlers do by default: a
    // PlayStation 3 controller, with that product's capabilities. Left as it
    // was, it was a pad of no known make without the analog stick and sensor
    // capabilities, and a game looking for the pad that rumbles found none
    // (OutRun asked for port -1, NNshi).
    {
        u32 capabilities = CELL_PAD_CAPABILITY_PS3_CONFORMITY | CELL_PAD_CAPABILITY_PRESS_MODE |
            CELL_PAD_CAPABILITY_HP_ANALOG_STICK | CELL_PAD_CAPABILITY_ACTUATOR | CELL_PAD_CAPABILITY_SENSOR_MODE;
        u32 profile = 0;
        for (const input::product_info& product : input::get_products_by_class(CELL_PAD_PCLASS_TYPE_STANDARD))
        {
            if (product.vendor_id == input::vendor_id::sony_corp && product.product_id == input::product_id::playstation_3_controller)
            {
                capabilities = product.capabilites;
                profile = product.pclass_profile;
            }
        }
        pad->Init(CELL_PAD_STATUS_DISCONNECTED, capabilities, CELL_PAD_DEV_TYPE_STANDARD, CELL_PAD_PCLASS_TYPE_STANDARD,
            profile, input::vendor_id::sony_corp, input::product_id::playstation_3_controller, 50);
    }

    // CRITICAL: Initialize the pad's buttons and sticks
    // Without this, m_buttons is empty and process() has nothing to update
    pad->m_buttons.clear();
    pad->m_buttons.reserve(17);

    // Button constructor: Button(u32 offset, std::vector<std::set<u32>> key_combos, u32 outKeyCode)
    // We pass empty set for key_codes since we handle input directly in process()

    // Digital buttons - DIGITAL1 group (directly from cellPad)
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_UP);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_DOWN);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_LEFT);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_RIGHT);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_SELECT);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_START);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_L3);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL1, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_R3);

    // Digital buttons - DIGITAL2 group
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_CROSS);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_CIRCLE);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_SQUARE);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_TRIANGLE);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_L1);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_R1);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_L2);
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_R2);

    // PS button (optional)
    pad->m_buttons.emplace_back(CELL_PAD_BTN_OFFSET_DIGITAL2, std::vector<std::set<u32>>{}, CELL_PAD_CTRL_PS);

    // Initialize analog sticks (center position = 128, default value)
    pad->m_sticks[0].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X;
    pad->m_sticks[1].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y;
    pad->m_sticks[2].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X;
    pad->m_sticks[3].m_offset = CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y;

    // Set pad as connected
    retro_atomic_fetch_or_int(&pad->m_port_status, CELL_PAD_STATUS_CONNECTED);
    return true;
}

std::shared_ptr<PadDevice> LibretroPadHandler::get_device(const std::string& device)
{
    // Extract port number from device name "Libretro Pad X"
    if (device.find("Libretro Pad") != std::string::npos)
    {
        return std::make_shared<PadDevice>();
    }
    return nullptr;
}

PadHandlerBase::connection LibretroPadHandler::update_connection(const std::shared_ptr<PadDevice>& device)
{
    if (!device)
        return connection::disconnected;

    // Libretro controllers are always connected (frontend handles actual connection)
    return connection::connected;
}

u16 LibretroPadHandler::ConvertAnalogValue(int16_t value)
{
    // Convert from -32768..32767 to 0..255
    // First normalize to 0..65535, then scale to 0..255
    int normalized = static_cast<int>(value) + 32768;
    return static_cast<u16>((normalized * 255) / 65535);
}

// The handler's interface (the settings' preview): the same values in a map
std::unordered_map<u32, u16> LibretroPadHandler::get_button_values(const std::shared_ptr<PadDevice>& device)
{
    std::unordered_map<u32, u16> values;
    if (!device)
        return values;
    button_values_t now{};
    read_button_values(*device, now);
    for (u32 i = 0; i < now.size(); i++)
        values[i] = now[i];
    return values;
}

// retro_run, each frame: the frontend's buttons and sticks into @values, in
// place. As a map, made and thrown away each frame for each pad, they were
// two dozen allocations a pad a frame.
void LibretroPadHandler::read_button_values(const PadDevice& pad_device, button_values_t& values)
{
    const PadDevice* const device = &pad_device;

    // The RetroArch port that drives this player (ports trade places in the
    // arcade shooter mode, see libretro_input_port_for_player)
    unsigned port = libretro_input_port_for_player(device->player_id);
    if (port >= LIBRETRO_MAX_PADS)
        port = 0;

    const auto& state = libretro_input_get_state(port);

    // Digital buttons (0 or 255)
    values[static_cast<u64>(LibretroButton::B)]      = state.buttons[RETRO_DEVICE_ID_JOYPAD_B] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Y)]      = state.buttons[RETRO_DEVICE_ID_JOYPAD_Y] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Select)] = state.buttons[RETRO_DEVICE_ID_JOYPAD_SELECT] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Start)]  = state.buttons[RETRO_DEVICE_ID_JOYPAD_START] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Up)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_UP] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Down)]   = state.buttons[RETRO_DEVICE_ID_JOYPAD_DOWN] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Left)]   = state.buttons[RETRO_DEVICE_ID_JOYPAD_LEFT] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::Right)]  = state.buttons[RETRO_DEVICE_ID_JOYPAD_RIGHT] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::A)]      = state.buttons[RETRO_DEVICE_ID_JOYPAD_A] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::X)]      = state.buttons[RETRO_DEVICE_ID_JOYPAD_X] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::L1)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_L] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::R1)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_R] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::L2)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_L2] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::R2)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_R2] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::L3)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_L3] ? 255 : 0;
    values[static_cast<u64>(LibretroButton::R3)]     = state.buttons[RETRO_DEVICE_ID_JOYPAD_R3] ? 255 : 0;

    // Analog sticks - state.analog[] contains values in range -32768 to 32767
    // Left stick X: analog[0], Y: analog[1]
    // Right stick X: analog[2], Y: analog[3]
    int16_t lsx = state.analog[0];
    int16_t lsy = state.analog[1];
    int16_t rsx = state.analog[2];
    int16_t rsy = state.analog[3];

    // Convert to directional values (0-255 range)
    // Negative X = left, Positive X = right
    // Negative Y = up, Positive Y = down (libretro uses Y-down convention)
    values[static_cast<u64>(LibretroButton::LSXNeg)] = lsx < 0 ? static_cast<u16>((-lsx * 255) / 32768) : 0;
    values[static_cast<u64>(LibretroButton::LSXPos)] = lsx > 0 ? static_cast<u16>((lsx * 255) / 32767) : 0;
    values[static_cast<u64>(LibretroButton::LSYNeg)] = lsy < 0 ? static_cast<u16>((-lsy * 255) / 32768) : 0;
    values[static_cast<u64>(LibretroButton::LSYPos)] = lsy > 0 ? static_cast<u16>((lsy * 255) / 32767) : 0;
    values[static_cast<u64>(LibretroButton::RSXNeg)] = rsx < 0 ? static_cast<u16>((-rsx * 255) / 32768) : 0;
    values[static_cast<u64>(LibretroButton::RSXPos)] = rsx > 0 ? static_cast<u16>((rsx * 255) / 32767) : 0;
    values[static_cast<u64>(LibretroButton::RSYNeg)] = rsy < 0 ? static_cast<u16>((-rsy * 255) / 32768) : 0;
    values[static_cast<u64>(LibretroButton::RSYPos)] = rsy > 0 ? static_cast<u16>((rsy * 255) / 32767) : 0;
}

pad_preview_values LibretroPadHandler::get_preview_values(const std::unordered_map<u32, u16>& data, const std::vector<std::string>& /*buttons*/)
{
    return {
        data.at(static_cast<u64>(LibretroButton::LSXNeg)) - data.at(static_cast<u64>(LibretroButton::LSXPos)),
        data.at(static_cast<u64>(LibretroButton::LSYPos)) - data.at(static_cast<u64>(LibretroButton::LSYNeg)),
        data.at(static_cast<u64>(LibretroButton::RSXNeg)) - data.at(static_cast<u64>(LibretroButton::RSXPos)),
        data.at(static_cast<u64>(LibretroButton::RSYPos)) - data.at(static_cast<u64>(LibretroButton::RSYNeg)),
        0,
        0
    };
}

// cellPadSetActDirect leaves the game's motor values in the pad
// (pad_thread::SetRumble); hand them to the frontend the way upstream's
// handlers drive a real controller: the large motor's lower range ignored and
// the rest scaled to full, the small one only on or off. A game that stops
// sending loses its rumble after 3 s, as PadHandlerBase::process does - this
// handler has its own process().
void LibretroPadHandler::apply_rumble(Pad& pad, unsigned port)
{
    // What the game last asked for, published by pad_thread::SetRumble on
    // its thread: read here without a lock. A request not renewed for 3 s
    // stops, as PadHandlerBase::process stops it - 3 s of runs, counted
    // here, not of the host's clock.
    const int requests = retro_atomic_load_acquire_int(&pad.m_rumble_requests);
    const int motors = retro_atomic_load_acquire_int(&pad.m_rumble_motors);
    const unsigned slot = port < m_rumble_seen.size() ? port : 0;
    if (requests != m_rumble_seen[slot])
    {
        m_rumble_seen[slot] = requests;
        m_rumble_age[slot] = 0;
    }
    else if (m_rumble_age[slot] <= m_rumble_runs)
    {
        m_rumble_age[slot]++;
    }
    const bool stale = !requests || m_rumble_age[slot] > m_rumble_runs;
    const u8 large = stale ? 0 : static_cast<u8>(motors & 0xff);
    const u8 small = stale ? 0 : static_cast<u8>((motors >> 8) & 0xff);

    const f32 strong = ScaledInput(large, static_cast<f32>(MOTOR_THRESHOLD), 255.0f, 0.0f, 255.0f);
    libretro_input_set_rumble(port, static_cast<u16>(std::clamp(strong, 0.0f, 255.0f) * 257.0f), small ? 0xffff : 0);
}

void LibretroPadHandler::process()
{
    // Process each bound pad
    for (usz i = 0; i < m_bindings.size(); i++)
    {
        auto& binding = m_bindings[i];
        if (!binding.pad || !binding.device)
            continue;

        // Set player ID for device
        binding.device->player_id = static_cast<u8>(i);

        // Check connection
        const auto status = update_connection(binding.device);
        if (status == connection::connected)
        {
            retro_atomic_fetch_or_int(&binding.pad->m_port_status, CELL_PAD_STATUS_CONNECTED);
        }
        else
        {
            retro_atomic_fetch_and_int(&binding.pad->m_port_status, ~CELL_PAD_STATUS_CONNECTED);
            continue;
        }

        apply_rumble(*binding.pad, libretro_input_port_for_player(static_cast<unsigned>(i)));

        // Get button values from libretro input
        button_values_t button_values{};
        read_button_values(*binding.device, button_values);

        // Update pad button states in m_buttons vector
        for (auto& button : binding.pad->m_buttons)
        {
            u16 value = 0;

            switch (button.m_offset)
            {
            case CELL_PAD_BTN_OFFSET_DIGITAL1:
                switch (button.m_outKeyCode)
                {
                case CELL_PAD_CTRL_UP:     value = button_values[static_cast<u64>(LibretroButton::Up)];     break;
                case CELL_PAD_CTRL_DOWN:   value = button_values[static_cast<u64>(LibretroButton::Down)];   break;
                case CELL_PAD_CTRL_LEFT:   value = button_values[static_cast<u64>(LibretroButton::Left)];   break;
                case CELL_PAD_CTRL_RIGHT:  value = button_values[static_cast<u64>(LibretroButton::Right)];  break;
                case CELL_PAD_CTRL_SELECT: value = button_values[static_cast<u64>(LibretroButton::Select)]; break;
                case CELL_PAD_CTRL_START:  value = button_values[static_cast<u64>(LibretroButton::Start)];  break;
                case CELL_PAD_CTRL_L3:     value = button_values[static_cast<u64>(LibretroButton::L3)];     break;
                case CELL_PAD_CTRL_R3:     value = button_values[static_cast<u64>(LibretroButton::R3)];     break;
                default: break;
                }
                break;
            case CELL_PAD_BTN_OFFSET_DIGITAL2:
                switch (button.m_outKeyCode)
                {
                case CELL_PAD_CTRL_CROSS:    value = button_values[static_cast<u64>(LibretroButton::B)];  break;
                case CELL_PAD_CTRL_CIRCLE:   value = button_values[static_cast<u64>(LibretroButton::A)];  break;
                case CELL_PAD_CTRL_SQUARE:   value = button_values[static_cast<u64>(LibretroButton::Y)];  break;
                case CELL_PAD_CTRL_TRIANGLE: value = button_values[static_cast<u64>(LibretroButton::X)];  break;
                case CELL_PAD_CTRL_L1:       value = button_values[static_cast<u64>(LibretroButton::L1)]; break;
                case CELL_PAD_CTRL_R1:       value = button_values[static_cast<u64>(LibretroButton::R1)]; break;
                case CELL_PAD_CTRL_L2:       value = button_values[static_cast<u64>(LibretroButton::L2)]; break;
                case CELL_PAD_CTRL_R2:       value = button_values[static_cast<u64>(LibretroButton::R2)]; break;
                default: break;
                }
                break;
            default:
                break;
            }

            button.m_value = value;
            button.m_pressed = value > 0;
        }

        // Update analog sticks
        // Convert from directional 0-255 to centered 0-255 format (128 = center)
        u16 lsx_neg = button_values[static_cast<u64>(LibretroButton::LSXNeg)];
        u16 lsx_pos = button_values[static_cast<u64>(LibretroButton::LSXPos)];
        u16 lsy_neg = button_values[static_cast<u64>(LibretroButton::LSYNeg)];
        u16 lsy_pos = button_values[static_cast<u64>(LibretroButton::LSYPos)];
        u16 rsx_neg = button_values[static_cast<u64>(LibretroButton::RSXNeg)];
        u16 rsx_pos = button_values[static_cast<u64>(LibretroButton::RSXPos)];
        u16 rsy_neg = button_values[static_cast<u64>(LibretroButton::RSYNeg)];
        u16 rsy_pos = button_values[static_cast<u64>(LibretroButton::RSYPos)];

        // Convert to centered format: 128 = center, 0 = full left/up, 255 = full right/down
        u16 lx = static_cast<u16>(128 + (lsx_pos / 2) - (lsx_neg / 2));
        u16 ly = static_cast<u16>(128 + (lsy_pos / 2) - (lsy_neg / 2));
        u16 rx = static_cast<u16>(128 + (rsx_pos / 2) - (rsx_neg / 2));
        u16 ry = static_cast<u16>(128 + (rsy_pos / 2) - (rsy_neg / 2));

        // A stick reports a circle, which a DualShock 3 does not: pushed all
        // the way diagonally it gave about 70 % on each axis, and games that
        // run at a full tilt walked instead (Ratchet & Clank, NNshi).
        // Standalone RPCS3 turns the circle into a squircle for every pad
        // handler (convert_stick_values), with a factor of 4000 by default.
        ConvertToSquirclePoint(lx, ly, 4000);
        ConvertToSquirclePoint(rx, ry, 4000);
        const u8 analog_left_x = static_cast<u8>(lx);
        const u8 analog_left_y = static_cast<u8>(ly);
        const u8 analog_right_x = static_cast<u8>(rx);
        const u8 analog_right_y = static_cast<u8>(ry);

        // Update analog stick axes in m_sticks array
        for (auto& stick : binding.pad->m_sticks)
        {
            switch (stick.m_offset)
            {
            case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X:  stick.m_value = analog_left_x;  break;
            case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y:  stick.m_value = analog_left_y;  break;
            case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X: stick.m_value = analog_right_x; break;
            case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y: stick.m_value = analog_right_y; break;
            default: break;
            }
        }
    }
}
