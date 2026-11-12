#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/system_config.h"
#include "Emu/Cell/PPUModule.h"
#include "Emu/Cell/lv2/sys_process.h"
#include "Emu/Cell/timers.hpp"
#include "Emu/Io/pad_types.h"
#include "Emu/RSX/Overlays/overlay_debug_overlay.h"
#include "Input/pad_thread.h"
#include "Input/product_info.h"
#include "cellPad.h"

error_code sys_config_start(ppu_thread& ppu);
error_code sys_config_stop(ppu_thread& ppu);

extern bool is_input_allowed();

LOG_CHANNEL(cellPad);

template<>
void fmt_class_string<CellPadError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto error)
	{
		switch (error)
		{
			STR_CASE(CELL_PAD_ERROR_FATAL);
			STR_CASE(CELL_PAD_ERROR_INVALID_PARAMETER);
			STR_CASE(CELL_PAD_ERROR_ALREADY_INITIALIZED);
			STR_CASE(CELL_PAD_ERROR_UNINITIALIZED);
			STR_CASE(CELL_PAD_ERROR_RESOURCE_ALLOCATION_FAILED);
			STR_CASE(CELL_PAD_ERROR_DATA_READ_FAILED);
			STR_CASE(CELL_PAD_ERROR_NO_DEVICE);
			STR_CASE(CELL_PAD_ERROR_UNSUPPORTED_GAMEPAD);
			STR_CASE(CELL_PAD_ERROR_TOO_MANY_DEVICES);
			STR_CASE(CELL_PAD_ERROR_EBUSY);
		}

		return unknown;
	});
}

template<>
void fmt_class_string<CellPadFilterError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto error)
	{
		switch (error)
		{
			STR_CASE(CELL_PADFILTER_ERROR_INVALID_PARAMETER);
		}

		return unknown;
	});
}

extern void sys_io_serialize(utils::serial& ar);

// A port's pad. Its game side (pad_guest_state, Pad::m_guest) is read whole
// with retro_cas_record_read and changed whole between retro_cas_record_begin
// and retro_cas_record_commit: no lock. What is done to the copy between the
// two is done again when another thread changed the port meanwhile, so it
// touches only the copy and the caller's own output.
static Pad& port_pad(u32 port_no)
{
	return *pad::get_pad_thread()->GetPads()[port_no];
}

// The device a port has, for the game: a custom controller's once one was
// registered there, the pad's own (set when the pads are made) otherwise
struct port_device
{
	u32 capability;
	u32 type;
	u32 class_type;
	u32 class_profile;
	u16 vendor_id;
	u16 product_id;
};

static port_device device_of(const Pad& pad, const pad_guest_state& state)
{
	if (state.ldd_identity)
	{
		static const input::product_info product = input::get_product_info(input::product_type::playstation_3_controller);
		return {CELL_PAD_CAPABILITY_PS3_CONFORMITY, CELL_PAD_DEV_TYPE_LDD, CELL_PAD_PCLASS_TYPE_STANDARD, product.pclass_profile, product.vendor_id, product.product_id};
	}

	return {pad.m_device_capability, pad.m_device_type, pad.m_class_type, pad.m_class_profile, pad.m_vendor_id, pad.m_product_id};
}

// The savestate keeps the ports' settings and reported devices as the two
// arrays pad_info used to hold
pad_info::pad_info(utils::serial& ar)
	: max_connect(ar.pop<u32>())
{
	const auto port_setting = ar.pop<std::array<u32, CELL_PAD_MAX_PORT_NUM>>();
	const auto reported_info = ar.pop<std::array<pad_data_internal, CELL_PAD_MAX_PORT_NUM>>();

	if (const auto handler = pad::g_pad_thread.load())
	{
		for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
		{
			const auto& pad = handler->GetPads()[i];

			if (!pad)
				continue;

			retro_cas_record_txn_t txn;
			do
			{
				pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad->m_guest, &txn));
				state->port_setting = port_setting[i];
				state->reported = reported_info[i];
			}
			while (!retro_cas_record_commit(&pad->m_guest, &txn));
		}
	}

	sys_io_serialize(ar);
}

void pad_info::save(utils::serial& ar)
{
	USING_SERIALIZATION_VERSION(sys_io);

	std::array<u32, CELL_PAD_MAX_PORT_NUM> port_setting{};
	std::array<pad_data_internal, CELL_PAD_MAX_PORT_NUM> reported_info{};

	if (const auto handler = pad::g_pad_thread.load())
	{
		for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
		{
			if (const auto& pad = handler->GetPads()[i])
			{
				pad_guest_state state;
				retro_cas_record_read(&pad->m_guest, &state);
				port_setting[i] = state.port_setting;
				reported_info[i] = state.reported;
			}
		}
	}

	ar(initialized(), port_setting, reported_info);

	sys_io_serialize(ar);
}

void show_debug_overlay(const CellPadData& data, const Pad& pad, const pad_guest_state& state)
{
	const u32 setting = state.port_setting;
	const CellPadData& raw = state.data;
	// What the game last asked of the motors: large | small << 8
	const u32 motors = static_cast<u32>(retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t*>(&pad.m_rumble_motors)));
	const u16 raw_d1 = raw.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
	const u16 raw_d2 = raw.button[CELL_PAD_BTN_OFFSET_DIGITAL2];
	const u16 d1 = data.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
	const u16 d2 = data.button[CELL_PAD_BTN_OFFSET_DIGITAL2];

	std::string text = fmt::format(
		">        Name:     Raw   Value   Pressure\n"
		">\n"
		">         Len:   %13d\n"
		">     Digital:   %5s   %5s\n"
		">       Press:   %5s   %5s\n"
		">      Sensor:   %5s   %5s\n"
		"> Large Motor:   %5d   %5d\n"
		"> Small Motor:   %5d   %5d\n"
		">\n"
		">   Digital 1:  0x%04x  0x%04x\n"
		">   Digital 2:  0x%04x  0x%04x\n"
		">\n"
		">    D-Pad Up:   %5d   %5d   %5d\n"
		">  D-Pad Down:   %5d   %5d   %5d\n"
		">  D-Pad Left:   %5d   %5d   %5d\n"
		"> D-Pad Right:   %5d   %5d   %5d\n"
		">       Cross:   %5d   %5d   %5d\n"
		">      Square:   %5d   %5d   %5d\n"
		">      Circle:   %5d   %5d   %5d\n"
		">    Triangle:   %5d   %5d   %5d\n"
		">       Start:   %5d   %5d\n"
		">      Select:   %5d   %5d\n"
		">          PS:   %5d   %5d\n"
		">          L1:   %5d   %5d   %5d\n"
		">          L2:   %5d   %5d   %5d\n"
		">          L3:   %5d   %5d\n"
		">          R1:   %5d   %5d   %5d\n"
		">          R2:   %5d   %5d   %5d\n"
		">          R3:   %5d   %5d\n"
		">        LS X:   %5d   %5d\n"
		">        LS Y:   %5d   %5d\n"
		">        RS X:   %5d   %5d\n"
		">        RS Y:   %5d   %5d\n"
		">\n"
		">    Sensor X:   %5d   %5d\n"
		">    Sensor Y:   %5d   %5d\n"
		">    Sensor Z:   %5d   %5d\n"
		">    Sensor G:   %5d   %5d\n"
		">\n"
		">         PID:  0x%04x\n"
		">         VID:  0x%04x\n"
		"> Device Type:  0x%08x\n"
		">  Class Type:  0x%08x\n"
		,
		data.len,
		"on", data.len >= CELL_PAD_LEN_CHANGE_DEFAULT ? "on" : "off",
		(setting & CELL_PAD_SETTING_PRESS_ON) ? "on" : "off", data.len >= CELL_PAD_LEN_CHANGE_PRESS_ON ? "on" : "off",
		(setting & CELL_PAD_SETTING_SENSOR_ON) ? "on" : "off", data.len >= CELL_PAD_LEN_CHANGE_SENSOR_ON ? "on" : "off",
		motors & 0xff, pad.m_vibrate_motors[0].adjusted_value,
		motors >> 8, pad.m_vibrate_motors[1].adjusted_value,
		raw_d1, d1,
		raw_d2, d2,
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_UP],       !!(d1 & CELL_PAD_CTRL_UP),       data.button[CELL_PAD_BTN_OFFSET_PRESS_UP],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_DOWN],     !!(d1 & CELL_PAD_CTRL_DOWN),     data.button[CELL_PAD_BTN_OFFSET_PRESS_DOWN],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_LEFT],     !!(d1 & CELL_PAD_CTRL_LEFT),     data.button[CELL_PAD_BTN_OFFSET_PRESS_LEFT],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_RIGHT],    !!(d1 & CELL_PAD_CTRL_RIGHT),    data.button[CELL_PAD_BTN_OFFSET_PRESS_RIGHT],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_CROSS],    !!(d2 & CELL_PAD_CTRL_CROSS),    data.button[CELL_PAD_BTN_OFFSET_PRESS_CROSS],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_SQUARE],   !!(d2 & CELL_PAD_CTRL_SQUARE),   data.button[CELL_PAD_BTN_OFFSET_PRESS_SQUARE],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_CIRCLE],   !!(d2 & CELL_PAD_CTRL_CIRCLE),   data.button[CELL_PAD_BTN_OFFSET_PRESS_CIRCLE],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE], !!(d2 & CELL_PAD_CTRL_TRIANGLE), data.button[CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE],
		!!(raw_d1 & CELL_PAD_CTRL_START),               !!(d1 & CELL_PAD_CTRL_START),
		!!(raw_d1 & CELL_PAD_CTRL_SELECT),              !!(d1 & CELL_PAD_CTRL_SELECT),
		!!(raw_d1 & CELL_PAD_CTRL_PS),                  !!(d1 & CELL_PAD_CTRL_PS),
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_L1],       !!(d2 & CELL_PAD_CTRL_L1),       data.button[CELL_PAD_BTN_OFFSET_PRESS_L1],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_L2],       !!(d2 & CELL_PAD_CTRL_L2),       data.button[CELL_PAD_BTN_OFFSET_PRESS_L2],
		!!(raw_d1 & CELL_PAD_CTRL_L3),                  !!(d1 & CELL_PAD_CTRL_L3),
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_R1],       !!(d2 & CELL_PAD_CTRL_R1),       data.button[CELL_PAD_BTN_OFFSET_PRESS_R1],
		raw.button[CELL_PAD_BTN_OFFSET_PRESS_R2],       !!(d2 & CELL_PAD_CTRL_R2),       data.button[CELL_PAD_BTN_OFFSET_PRESS_R2],
		!!(raw_d1 & CELL_PAD_CTRL_R3),                  !!(d1 & CELL_PAD_CTRL_R3),
		raw.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X],  data.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X],
		raw.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y],  data.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y],
		raw.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X], data.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X],
		raw.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y], data.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y],
		raw.button[CELL_PAD_BTN_OFFSET_SENSOR_X],       data.button[CELL_PAD_BTN_OFFSET_SENSOR_X],
		raw.button[CELL_PAD_BTN_OFFSET_SENSOR_Y],       data.button[CELL_PAD_BTN_OFFSET_SENSOR_Y],
		raw.button[CELL_PAD_BTN_OFFSET_SENSOR_Z],       data.button[CELL_PAD_BTN_OFFSET_SENSOR_Z],
		raw.button[CELL_PAD_BTN_OFFSET_SENSOR_G],       data.button[CELL_PAD_BTN_OFFSET_SENSOR_G],
		pad.m_product_id,
		pad.m_vendor_id,
		pad.m_device_type,
		pad.m_class_type
	);

	rsx::overlays::set_debug_overlay_text(std::move(text));
}

extern void send_sys_io_connect_event(usz index, u32 state);

// The port's connection as the game is told of it (sys_io's events): takes
// no lock, so it never has to be tried again - always true
bool cellPad_NotifyStateChange(usz index, u64 /*state*/, bool /*locked*/, bool /*is_blocking*/ = true)
{
	auto info = g_fxo->try_get<pad_info>();

	if (!info)
	{
		return true;
	}

	if (index >= info->get_max_connect())
	{
		return true;
	}

	Pad& pad = port_pad(static_cast<u32>(index));

	if (pad.is_fake_pad)
	{
		return true;
	}

	for (;;)
	{
		retro_cas_record_txn_t txn;
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));
		pad_data_internal& reported_info = state->reported;
		const u32 old_status = reported_info.port_status;

		// Ignore sent status for now, use the latest instead
		// NOTE 1: The state's CONNECTED bit should currently be identical to the current
		//         m_port_status CONNECTED bit when called from our pad handlers.
		// NOTE 2: Make sure to propagate all other status bits to the reported status.
		const u32 new_status = pad.port_status();

		if (~(old_status ^ new_status) & CELL_PAD_STATUS_CONNECTED)
		{
			// old and new have the same connection status
			retro_cas_record_abort(&pad.m_guest, &txn);
			return true;
		}

		const port_device device = device_of(pad, *state);

		reported_info.port_status = new_status | CELL_PAD_STATUS_ASSIGN_CHANGES;
		reported_info.device_capability = device.capability;
		reported_info.device_type = device.type;
		reported_info.pclass_type = device.class_type;
		reported_info.pclass_profile = device.class_profile;

		if (device.vendor_id == 0 || device.product_id == 0)
		{
			// Fallback to defaults
			const std::vector<input::product_info> input_products = input::get_products_by_class(device.class_type);
			const input::product_info& product = ::at32(input_products, 0);
			reported_info.vendor_id = product.vendor_id;
			reported_info.product_id = product.product_id;
		}
		else
		{
			reported_info.vendor_id = device.vendor_id;
			reported_info.product_id = device.product_id;
		}

		if (retro_cas_record_commit(&pad.m_guest, &txn))
			return true;
	}
}

extern void pad_state_notify_state_change(usz index, u32 state)
{
	send_sys_io_connect_event(index, state);
}

error_code cellPadInit(ppu_thread& ppu, u32 max_connect)
{
	cellPad.warning("cellPadInit(max_connect=%d)", max_connect);

	auto& config = g_fxo->get<pad_info>();

	if (config.max_connect)
		return CELL_PAD_ERROR_ALREADY_INITIALIZED;

	if (max_connect == 0 || max_connect > CELL_MAX_PADS)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	// Taken, for one of two threads at it; not initialized yet for the
	// other calls until the ports are set up
	if (!config.max_connect.compare_and_swap_test(0, pad_info::starting))
		return CELL_PAD_ERROR_ALREADY_INITIALIZED;

	sys_config_start(ppu);

	const auto handler = pad::get_pad_thread();
	const auto& pads = handler->GetPads();

	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
	{
		retro_cas_record_txn_t txn;
		do
		{
			pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pads[i]->m_guest, &txn));
			state->port_setting = CELL_PAD_SETTING_PRESS_OFF | CELL_PAD_SETTING_SENSOR_OFF;
			state->reported = {};
		}
		while (!retro_cas_record_commit(&pads[i]->m_guest, &txn));
	}

	config.max_connect = max_connect;

	for (usz i = 0; i < config.get_max_connect(); ++i)
	{
		if (!pads[i]->is_fake_pad && pads[i]->is_connected())
		{
			send_sys_io_connect_event(i, CELL_PAD_STATUS_CONNECTED);
		}
	}

	return CELL_OK;
}

error_code cellPadEnd(ppu_thread& ppu)
{
	cellPad.notice("cellPadEnd()");

	auto& config = g_fxo->get<pad_info>();

	// One of two threads at it ends it; while a cellPadInit is under way
	// there is nothing to end yet
	for (;;)
	{
		const u32 max_connect = config.initialized();

		if (!max_connect)
			return CELL_PAD_ERROR_UNINITIALIZED;

		if (config.max_connect.compare_and_swap_test(max_connect, 0))
			break;
	}

	sys_config_stop(ppu);
	return CELL_OK;
}

// Set 'buffer_cleared' to force a resend of everything
// might as well also reset everything in our pad 'buffer' to nothing as well
void clear_pad_buffer(pad_guest_state& state)
{
	state.buffer_cleared = 1;
	state.data = {};
	state.data.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X] = 128;
	state.data.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y] = 128;
	state.data.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X] = 128;
	state.data.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y] = 128;
	state.data.button[CELL_PAD_BTN_OFFSET_SENSOR_X] = DEFAULT_MOTION_X;
	state.data.button[CELL_PAD_BTN_OFFSET_SENSOR_Y] = DEFAULT_MOTION_Y;
	state.data.button[CELL_PAD_BTN_OFFSET_SENSOR_Z] = DEFAULT_MOTION_Z;
	state.data.button[CELL_PAD_BTN_OFFSET_SENSOR_G] = DEFAULT_MOTION_G;
}

// Whether the game has the port's device: the port is no fake one, the game
// was told of the connection and it still stands
static bool port_has_device(const Pad& pad, const pad_guest_state& state)
{
	return !pad.is_fake_pad && (state.reported.port_status & CELL_PAD_STATUS_CONNECTED) && pad.is_connected();
}

error_code cellPadClearBuf(u32 port_no)
{
	cellPad.trace("cellPadClearBuf(port_no=%d)", port_no);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	Pad& pad = port_pad(port_no);

	for (;;)
	{
		retro_cas_record_txn_t txn;
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));

		if (!port_has_device(pad, *state))
		{
			retro_cas_record_abort(&pad.m_guest, &txn);
			return not_an_error(CELL_PAD_ERROR_NO_DEVICE);
		}

		clear_pad_buffer(*state);

		if (retro_cas_record_commit(&pad.m_guest, &txn))
			return CELL_OK;
	}
}

// One cellPadGetData of a port, on @state: a copy of the port's game side,
// the caller's until it puts it in the port's place. data->len says whether
// that is needed: with CELL_PAD_LEN_NO_CHANGE nothing of @state was changed.
static void pad_get_data(const Pad& pad_ref, pad_guest_state& state, CellPadData* data, bool get_periph_data = false)
{
	const Pad* const pad = &pad_ref;
	const PadInfo& rinfo = pad::get_pad_thread()->GetInfo();

	if (rinfo.system_info() & CELL_PAD_INFO_INTERCEPTED)
	{
		data->len = CELL_PAD_LEN_NO_CHANGE;
		return;
	}

	const u32 setting = state.port_setting;
	const port_device device = device_of(pad_ref, state);
	bool btnChanged = false;

	CellPadData& output = state.data;

	if (rinfo.ignore_input() || !is_input_allowed())
	{
		// Needed for Hotline Miami and Ninja Gaiden Sigma after dialogs were closed and buttons are still pressed.
		// Gran Turismo 6 would keep registering the Start button during OSK Dialogs if this wasn't cleared and if we'd return with len as CELL_PAD_LEN_NO_CHANGE.
		clear_pad_buffer(state);
	}
	else if (state.ldd)
	{
		if (setting & CELL_PAD_SETTING_SENSOR_ON)
			data->len = CELL_PAD_LEN_CHANGE_SENSOR_ON;
		else
			data->len = (setting & CELL_PAD_SETTING_PRESS_ON) ? CELL_PAD_LEN_CHANGE_PRESS_ON : CELL_PAD_LEN_CHANGE_DEFAULT;
		std::memcpy(data->button, state.ldd_data.button, data->len * sizeof(u16));
		return;
	}
	else
	{
		const u16 d1_initial = output.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
		const u16 d2_initial = output.button[CELL_PAD_BTN_OFFSET_DIGITAL2];

		// Check if this pad is configured as a skateboard which ignores sticks and pressure button values.
		// Curiously it maps infrared on the press value of the face buttons for some reason.
		const bool use_piggyback = device.class_type == CELL_PAD_PCLASS_TYPE_SKATEBOARD;

		const auto set_value = [&btnChanged, use_piggyback, pad, &device](auto& value, u16 new_value, bool force_processing = false, u16 old_max_value = 255, u16 new_max_value = 255)
		{
			if (use_piggyback)
			{
				if (!force_processing)
					return;

				// Some piggyback values need to be scaled down on emulated devices
				if (old_max_value != new_max_value)
				{
					if (device.class_type == CELL_PAD_PCLASS_TYPE_SKATEBOARD && pad->m_pad_handler != pad_handler::skateboard)
					{
						new_value = static_cast<u16>(new_max_value * std::clamp(new_value / static_cast<f32>(old_max_value), 0.0f, 1.0f));
					}
				}
			}

			if (value != new_value)
			{
				btnChanged = true;
				value = new_value;
			}
		};

		// What the frontend's thread last handed over, read whole without a lock
		pad_external pad_ext;
		pad->read_external(pad_ext);

		for (const ButtonExternal& button : pad_ext.button_span())
		{
			// here we check btns, and set pad accordingly,
			// if something changed, set btnChanged

			switch (button.m_offset)
			{
			case CELL_PAD_BTN_OFFSET_DIGITAL1:
			{
				if (button.m_pressed)
					output.button[CELL_PAD_BTN_OFFSET_DIGITAL1] |= button.m_outKeyCode;
				else
					output.button[CELL_PAD_BTN_OFFSET_DIGITAL1] &= ~button.m_outKeyCode;

				switch (button.m_outKeyCode)
				{
				case CELL_PAD_CTRL_LEFT:  set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_LEFT],  button.m_value); break;
				case CELL_PAD_CTRL_DOWN:  set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_DOWN],  button.m_value); break;
				case CELL_PAD_CTRL_RIGHT: set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_RIGHT], button.m_value); break;
				case CELL_PAD_CTRL_UP:    set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_UP],    button.m_value); break;
				// These aren't pressure btns
				case CELL_PAD_CTRL_R3:
				case CELL_PAD_CTRL_L3:
				case CELL_PAD_CTRL_START:
				case CELL_PAD_CTRL_SELECT:
				default: break;
				}
				break;
			}
			case CELL_PAD_BTN_OFFSET_DIGITAL2:
			{
				if (button.m_pressed)
					output.button[CELL_PAD_BTN_OFFSET_DIGITAL2] |= button.m_outKeyCode;
				else
					output.button[CELL_PAD_BTN_OFFSET_DIGITAL2] &= ~button.m_outKeyCode;

				switch (button.m_outKeyCode)
				{
				case CELL_PAD_CTRL_SQUARE:   set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_SQUARE], button.m_value); break;
				case CELL_PAD_CTRL_CROSS:    set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_CROSS], button.m_value); break;
				case CELL_PAD_CTRL_CIRCLE:   set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_CIRCLE], button.m_value); break;
				case CELL_PAD_CTRL_TRIANGLE: set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE], button.m_value); break;
				case CELL_PAD_CTRL_R1:       set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_R1], button.m_value); break;
				case CELL_PAD_CTRL_L1:       set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_L1], button.m_value); break;
				case CELL_PAD_CTRL_R2:       set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_R2], button.m_value); break;
				case CELL_PAD_CTRL_L2:       set_value(output.button[CELL_PAD_BTN_OFFSET_PRESS_L2], button.m_value); break;
				default: break;
				}
				break;
			}
			case CELL_PAD_BTN_OFFSET_PRESS_PIGGYBACK:
			{
				switch (button.m_outKeyCode)
				{
				case CELL_PAD_CTRL_PRESS_RIGHT:
				case CELL_PAD_CTRL_PRESS_LEFT:
				case CELL_PAD_CTRL_PRESS_UP:
				case CELL_PAD_CTRL_PRESS_DOWN:
				case CELL_PAD_CTRL_PRESS_L1:
				case CELL_PAD_CTRL_PRESS_R1:
				case CELL_PAD_CTRL_PRESS_L2:
				case CELL_PAD_CTRL_PRESS_R2:
					set_value(output.button[button.m_outKeyCode], button.m_value, true);
					break;
				case CELL_PAD_CTRL_PRESS_TRIANGLE:
				case CELL_PAD_CTRL_PRESS_CIRCLE:
				case CELL_PAD_CTRL_PRESS_CROSS:
				case CELL_PAD_CTRL_PRESS_SQUARE:
					set_value(output.button[button.m_outKeyCode], button.m_value, true, 255, 63); // Infrared on RIDE Skateboard
					break;
				default: break;
				}
				break;
			}
			default:
				break;
			}
		}

		for (const AnalogStickExternal& stick : pad_ext.sticks)
		{
			switch (stick.m_offset)
			{
			case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X:
			case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y:
			case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X:
			case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y:
				set_value(output.button[stick.m_offset], stick.m_value);
				break;
			default:
				break;
			}
		}

		if (setting & CELL_PAD_SETTING_SENSOR_ON)
		{
			for (const AnalogSensor& sensor : pad->m_sensors)
			{
				switch (sensor.m_offset)
				{
				case CELL_PAD_BTN_OFFSET_SENSOR_X:
				case CELL_PAD_BTN_OFFSET_SENSOR_Y:
				case CELL_PAD_BTN_OFFSET_SENSOR_Z:
				case CELL_PAD_BTN_OFFSET_SENSOR_G:
					set_value(output.button[sensor.m_offset], sensor.m_value, true);
					break;
				default:
					break;
				}
			}
		}

		if (d1_initial != output.button[CELL_PAD_BTN_OFFSET_DIGITAL1] || d2_initial != output.button[CELL_PAD_BTN_OFFSET_DIGITAL2])
		{
			btnChanged = true;
		}
	}

	if (setting & CELL_PAD_SETTING_SENSOR_ON)
	{
		// report back new data every ~10 ms even if the input doesn't change
		// this is observed behaviour when using a Dualshock 3 controller.
		// 10 ms of the PS3's clock, which follows retro_run (it was the
		// host's: the reports came at the host's pace while fast-forwarding
		// and went on through a pause).
		const u64 now = get_guest_system_time();

		if (btnChanged || state.buffer_cleared || now - state.sensor_time >= 10'000)
		{
			data->len = CELL_PAD_LEN_CHANGE_SENSOR_ON;
			state.sensor_time = now;
		}
		else
		{
			data->len = CELL_PAD_LEN_NO_CHANGE;
		}
	}
	else if (btnChanged || state.buffer_cleared)
	{
		// only give back valid data if a controller state changed
		data->len = (setting & CELL_PAD_SETTING_PRESS_ON) ? CELL_PAD_LEN_CHANGE_PRESS_ON : CELL_PAD_LEN_CHANGE_DEFAULT;
	}
	else
	{
		// report no state changes
		data->len = CELL_PAD_LEN_NO_CHANGE;
	}

	state.buffer_cleared = 0;

	// only update parts of the output struct depending on the controller setting
	if (data->len > CELL_PAD_LEN_NO_CHANGE)
	{
		data->button[0] = 0x0; // always 0
		// bits 15-8 reserved, 7-4 = 0x7, 3-0: data->len/2;
		data->button[1] = (0x7 << 4) | std::min(data->len / 2, 15);

		if (setting & CELL_PAD_SETTING_PRESS_ON)
		{
			constexpr u32 copy_size = (static_cast<u32>(CELL_PAD_LEN_CHANGE_PRESS_ON) - static_cast<u32>(CELL_PAD_BTN_OFFSET_DIGITAL1)) * sizeof(u16);
			std::memcpy(&data->button[CELL_PAD_BTN_OFFSET_DIGITAL1], &output.button[CELL_PAD_BTN_OFFSET_DIGITAL1], copy_size);
		}
		else
		{
			constexpr u32 copy_size = (static_cast<u32>(CELL_PAD_LEN_CHANGE_DEFAULT) - static_cast<u32>(CELL_PAD_BTN_OFFSET_DIGITAL1)) * sizeof(u16);
			std::memcpy(&data->button[CELL_PAD_BTN_OFFSET_DIGITAL1], &output.button[CELL_PAD_BTN_OFFSET_DIGITAL1], copy_size);

			// Clear area if setting is not used
			constexpr u32 area_size = (CELL_PAD_LEN_CHANGE_PRESS_ON - CELL_PAD_LEN_CHANGE_DEFAULT) * sizeof(u16);
			std::memset(&data->button[CELL_PAD_LEN_CHANGE_DEFAULT], 0, area_size);
		}

		if (data->len == CELL_PAD_LEN_CHANGE_SENSOR_ON)
		{
			constexpr u32 copy_size = (static_cast<u32>(CELL_PAD_LEN_CHANGE_SENSOR_ON) - static_cast<u32>(CELL_PAD_BTN_OFFSET_SENSOR_X)) * sizeof(u16);
			std::memcpy(&data->button[CELL_PAD_BTN_OFFSET_SENSOR_X], &output.button[CELL_PAD_BTN_OFFSET_SENSOR_X], copy_size);
		}
	}

	if (!get_periph_data || data->len <= CELL_PAD_LEN_CHANGE_SENSOR_ON)
	{
		return;
	}

	const auto get_pressure_value = [setting](u16 val, u16 min, u16 max) -> u16
	{
		if (setting & CELL_PAD_SETTING_PRESS_ON)
		{
			return std::clamp(val, min, max);
		}

		if (val > 0)
		{
			return max;
		}

		return 0;
	};

	// TODO: support for 'unique' controllers, which goes in offsets 24+ in padData (CELL_PAD_PCLASS_BTN_OFFSET)
	// TODO: update data->len accordingly

	switch (device.class_profile)
	{
	default:
	case CELL_PAD_PCLASS_TYPE_STANDARD:
	case CELL_PAD_PCLASS_TYPE_NAVIGATION:
	case CELL_PAD_PCLASS_TYPE_SKATEBOARD:
	{
		break;
	}
	case CELL_PAD_PCLASS_TYPE_GUITAR:
	{
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_1]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_2]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_3]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_4]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_5]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_STRUM_UP]    = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_STRUM_DOWN]  = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_WHAMMYBAR]   = 0x80; // 0x80 – 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_H1]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_H2]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_H3]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_H4]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_FRET_H5]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_5WAY_EFFECT] = 0x0019; // One of 5 values: 0x0019, 0x004C, 0x007F (or 0x0096), 0x00B2, 0x00E5 (or 0x00E2)
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_GUITAR_TILT_SENS]   = get_pressure_value(0, 0x0, 0xFF);
		break;
	}
	case CELL_PAD_PCLASS_TYPE_DRUM:
	{
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_SNARE]     = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_TOM]       = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_TOM2]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_TOM_FLOOR] = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_KICK]      = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_CYM_HiHAT] = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_CYM_CRASH] = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_CYM_RIDE]  = get_pressure_value(0, 0x0, 0xFF);
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DRUM_KICK2]     = get_pressure_value(0, 0x0, 0xFF);
		break;
	}
	case CELL_PAD_PCLASS_TYPE_DJ:
	{
		// First deck
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_MIXER_ATTACK]     = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_MIXER_CROSSFADER] = 0;    // 0x0 - 0x3FF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_MIXER_DSP_DIAL]   = 0;    // 0x0 - 0x3FF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK1_STREAM1]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK1_STREAM2]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK1_STREAM3]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK1_PLATTER]    = 0x80; // 0x0 - 0xFF (neutral: 0x80)

		// Second deck
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK2_STREAM1]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK2_STREAM2]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK2_STREAM3]    = 0;    // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DJ_DECK2_PLATTER]    = 0x80; // 0x0 - 0xFF (neutral: 0x80)
		break;
	}
	case CELL_PAD_PCLASS_TYPE_DANCEMAT:
	{
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_CIRCLE]   = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_CROSS]    = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_TRIANGLE] = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_SQUARE]   = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_RIGHT]    = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_LEFT]     = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_UP]       = 0; // 0x0 or 0xFF
		data->button[CELL_PAD_PCLASS_BTN_OFFSET_DANCEMAT_DOWN]     = 0; // 0x0 or 0xFF
		break;
	}
	}
}

// cellPadGetData and cellPadPeriphGetData of a port whose pad is @pad: one
// change of the port, whichever threads call. False when the game has no
// device there. @device: the port's device, when wanted; @shown: the port as
// it was left, when wanted.
bool cellPad_port_get_data(Pad& pad, CellPadData* data, bool get_periph_data, pad_guest_state* shown, u32* pclass_type, u32* pclass_profile)
{
	for (;;)
	{
		// On a copy of the port. Most calls have nothing new for the game
		// and change nothing of the port: those write nothing shared.
		pad_guest_state state;
		const retro_cas_record_word_t version = retro_cas_record_read_versioned(&pad.m_guest, &state);

		if (!port_has_device(pad, state))
			return false;

		const bool custom = state.ldd != 0;

		pad_get_data(pad, state, data, get_periph_data);

		if (pclass_type)
		{
			const port_device device = device_of(pad, state);
			*pclass_type = device.class_type;
			*pclass_profile = device.class_profile;
		}

		if (shown)
			*shown = state;

		// Nothing new for the game, so nothing changed. A custom
		// controller's data is the game's own, given back as it is.
		if (data->len == CELL_PAD_LEN_NO_CHANGE || custom)
			return true;

		// Something new: the copy in the port's place, if the port is
		// still as it was read - else another thread got there first, and
		// the call is made again on the port as that left it
		if (retro_cas_record_replace(&pad.m_guest, version, &state))
			return true;
	}
}

error_code cellPadGetData(u32 port_no, vm::ptr<CellPadData> data)
{
	cellPad.trace("cellPadGetData(port_no=%d, data=*0x%x)", port_no, data);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS || !data)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	Pad& pad = port_pad(port_no);
	const bool overlay = g_cfg.io.pad_debug_overlay && !g_cfg.video.debug_overlay && port_no == 0;
	pad_guest_state shown;

	if (!cellPad_port_get_data(pad, data.get_ptr(), false, overlay ? &shown : nullptr, nullptr, nullptr))
		return not_an_error(CELL_PAD_ERROR_NO_DEVICE);

	if (overlay)
	{
		show_debug_overlay(*data, pad, shown);
	}

	return CELL_OK;
}

// A port as it is reported to the game by the calls that list the ports;
// they also take ASSIGN_CHANGES off the reported status, once
static void port_report(Pad& pad, pad_guest_state& as_it_was)
{
	for (;;)
	{
		retro_cas_record_txn_t txn;
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));
		as_it_was = *state;

		if (!(state->reported.port_status & CELL_PAD_STATUS_ASSIGN_CHANGES))
		{
			retro_cas_record_abort(&pad.m_guest, &txn);
			return;
		}

		state->reported.port_status &= ~CELL_PAD_STATUS_ASSIGN_CHANGES;

		if (retro_cas_record_commit(&pad.m_guest, &txn))
			return;
	}
}

error_code cellPadPeriphGetInfo(vm::ptr<CellPadPeriphInfo> info)
{
	cellPad.trace("cellPadPeriphGetInfo(info=*0x%x)", info);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (!info)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	const auto handler = pad::get_pad_thread();
	const PadInfo& rinfo = handler->GetInfo();

	std::memset(info.get_ptr(), 0, sizeof(CellPadPeriphInfo));

	info->max_connect = max_connect;
	info->system_info = rinfo.system_info();

	u32 now_connect = 0;

	for (u32 i = 0; i < pad_info::max_ports(max_connect); ++i)
	{
		pad_guest_state state;
		port_report(port_pad(i), state);
		const pad_data_internal& reported_info = state.reported;

		info->port_status[i] = reported_info.port_status;
		info->port_setting[i] = state.port_setting;

		if (~reported_info.port_status & CELL_PAD_STATUS_CONNECTED)
		{
			continue;
		}

		info->device_capability[i] = reported_info.device_capability;
		info->device_type[i] = reported_info.device_type;
		info->pclass_type[i] = reported_info.pclass_type;
		info->pclass_profile[i] = reported_info.pclass_profile;

		now_connect++;
	}

	info->now_connect = now_connect;

	return CELL_OK;
}

error_code cellPadPeriphGetData(u32 port_no, vm::ptr<CellPadPeriphData> data)
{
	cellPad.trace("cellPadPeriphGetData(port_no=%d, data=*0x%x)", port_no, data);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	// port_no can only be 0-6 in this function
	if (port_no >= CELL_PAD_MAX_PORT_NUM || !data)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	u32 pclass_type = 0, pclass_profile = 0;

	if (!cellPad_port_get_data(port_pad(port_no), &data->cellpad_data, true, nullptr, &pclass_type, &pclass_profile))
		return not_an_error(CELL_PAD_ERROR_NO_DEVICE);

	data->pclass_type = pclass_type;
	data->pclass_profile = pclass_profile;

	return CELL_OK;
}

error_code cellPadGetRawData(u32 port_no, vm::ptr<CellPadData> data)
{
	cellPad.todo("cellPadGetRawData(port_no=%d, data=*0x%x)", port_no, data);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS || !data)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	Pad& pad = port_pad(port_no);
	pad_guest_state state;
	retro_cas_record_read(&pad.m_guest, &state);

	if (!port_has_device(pad, state))
		return not_an_error(CELL_PAD_ERROR_NO_DEVICE);

	// ?
	// std::memcpy(data.get_ptr(), &state.data, sizeof(CellPadData));

	return CELL_OK;
}

error_code cellPadGetDataExtra(u32 port_no, vm::ptr<u32> device_type, vm::ptr<CellPadData> data)
{
	cellPad.trace("cellPadGetDataExtra(port_no=%d, device_type=*0x%x, data=*0x%x)", port_no, device_type, data);

	// TODO: This is used just to get data from a BD/CEC remote,
	// but if the port isn't a remote, device type is set to CELL_PAD_DEV_TYPE_STANDARD and just regular cellPadGetData is returned

	if (auto err = cellPadGetData(port_no, data))
	{
		return err;
	}

	if (device_type) // no error is returned on NULL
	{
		*device_type = CELL_PAD_DEV_TYPE_STANDARD;
	}

	// Set BD data
	data->button[24] = 0x0;
	data->button[25] = 0x0;

	return CELL_OK;
}

#ifdef LIBRETRO_CORE
static error_code cellPadSetActDirect_impl(u32 port_no, vm::ptr<CellPadActParam> param);

// Calls go to the log with what they asked for and what came of it, so a
// rumble that never arrives shows whether the game asked: the first few of a
// session, then the ones that turn a motor on or fail (games send "off" to
// every port at boot, which would use up a plain budget).
error_code cellPadSetActDirect(u32 port_no, vm::ptr<CellPadActParam> param)
{
	static atomic_t<s32> s_first_left = 8;
	static atomic_t<s32> s_log_left = 48;
	const error_code result = cellPadSetActDirect_impl(port_no, param);
	const bool notable = !param || param->motor[0] || param->motor[1] || static_cast<s32>(result) != CELL_OK;
	if ((s_first_left-- > 0 || notable) && s_log_left-- > 0)
	{
		if (param)
			cellPad.notice("cellPadSetActDirect(port_no=%d, motor=[%d, %d], reserved=[%d %d %d %d %d %d]) -> 0x%x", static_cast<s32>(port_no),
				param->motor[0], param->motor[1], param->reserved[0], param->reserved[1], param->reserved[2],
				param->reserved[3], param->reserved[4], param->reserved[5], static_cast<u32>(static_cast<s32>(result)));
		else
			cellPad.notice("cellPadSetActDirect(port_no=%d, param=null) -> 0x%x", static_cast<s32>(port_no), static_cast<u32>(static_cast<s32>(result)));
	}
	return result;
}

static error_code cellPadSetActDirect_impl(u32 port_no, vm::ptr<CellPadActParam> param)
#else
error_code cellPadSetActDirect(u32 port_no, vm::ptr<CellPadActParam> param)
#endif
{
	cellPad.trace("cellPadSetActDirect(port_no=%d, param=*0x%x)", port_no, param);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS || !param)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	// Note: signed check unlike the usual unsigned check
	if (static_cast<s32>(g_ps3_process_info.sdk_ver) > 0x1FFFFF)
	{
		// make sure reserved bits are 0
		for (int i = 0; i < 6; i++)
		{
			if (param->reserved[i])
				return CELL_PAD_ERROR_INVALID_PARAMETER;
		}
	}

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	const auto handler = pad::get_pad_thread();
	Pad& pad = port_pad(port_no);
	pad_guest_state state;
	retro_cas_record_read(&pad.m_guest, &state);

	if (!port_has_device(pad, state))
		return not_an_error(CELL_PAD_ERROR_NO_DEVICE);

	// TODO: find out if this is checked here or later or at all
	if (!(device_of(pad, state).capability & CELL_PAD_CAPABILITY_ACTUATOR))
		return CELL_PAD_ERROR_UNSUPPORTED_GAMEPAD;

	handler->SetRumble(port_no, param->motor[1], param->motor[0]);

	return CELL_OK;
}

error_code cellPadGetInfo(vm::ptr<CellPadInfo> info)
{
	cellPad.trace("cellPadGetInfo(info=*0x%x)", info);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (!info)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	std::memset(info.get_ptr(), 0, sizeof(CellPadInfo));

	const auto handler = pad::get_pad_thread();
	const PadInfo& rinfo = handler->GetInfo();
	info->max_connect = max_connect;
	info->system_info = rinfo.system_info();

	u32 now_connect = 0;

	for (u32 i = 0; i < pad_info::max_ports(max_connect); ++i)
	{
		pad_guest_state state;
		port_report(port_pad(i), state);
		const pad_data_internal& reported_info = state.reported;

		// TODO: should ASSIGN flags be cleared here?
		const u32 port_status = reported_info.port_status & ~CELL_PAD_STATUS_ASSIGN_CHANGES;
		info->status[i] = port_status;

		if (~port_status & CELL_PAD_STATUS_CONNECTED)
		{
			continue;
		}

		info->vendor_id[i] = reported_info.vendor_id;
		info->product_id[i] = reported_info.product_id;

		now_connect++;
	}

	info->now_connect = now_connect;

	return CELL_OK;
}

error_code cellPadGetInfo2(vm::ptr<CellPadInfo2> info)
{
	cellPad.trace("cellPadGetInfo2(info=*0x%x)", info);

	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (!info)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	std::memset(info.get_ptr(), 0, sizeof(CellPadInfo2));

	const auto handler = pad::get_pad_thread();
	const PadInfo& rinfo = handler->GetInfo();
	info->max_connect = pad_info::max_ports(max_connect); // Here it is forcibly clamped
	info->system_info = rinfo.system_info();

	u32 now_connect = 0;

	for (u32 i = 0; i < pad_info::max_ports(max_connect); ++i)
	{
		Pad& pad = port_pad(i);
		pad_guest_state state;
		port_report(pad, state);
		const pad_data_internal& reported_info = state.reported;

		info->port_status[i] = reported_info.port_status;
		info->port_setting[i] = state.port_setting;

		if (~reported_info.port_status & CELL_PAD_STATUS_CONNECTED)
		{
			continue;
		}

		const port_device device = device_of(pad, state);
		info->device_capability[i] = device.capability;
		info->device_type[i] = device.type;

		now_connect++;
	}

	info->now_connect = now_connect;

	return CELL_OK;
}

// What the calls about one port's device start with. The port's game side
// is read into @state; an error to return, or CELL_OK to go on.
static error_code port_device_call(u32 port_no, bool has_arg, Pad*& pad, pad_guest_state& state)
{
	auto& config = g_fxo->get<pad_info>();
	const u32 max_connect = config.initialized();

	if (!max_connect)
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS || !has_arg)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	if (port_no >= pad_info::max_ports(max_connect))
		return CELL_PAD_ERROR_NO_DEVICE;

	pad = &port_pad(port_no);
	retro_cas_record_read(&pad->m_guest, &state);

	if (!port_has_device(*pad, state))
		return not_an_error(CELL_PAD_ERROR_NO_DEVICE);

	return CELL_OK;
}

error_code cellPadGetCapabilityInfo(u32 port_no, vm::ptr<CellPadCapabilityInfo> info)
{
	cellPad.trace("cellPadGetCapabilityInfo(port_no=%d, data_addr:=0x%x)", port_no, info.addr());

	Pad* pad = nullptr;
	pad_guest_state state;

	if (const error_code error = port_device_call(port_no, !!info, pad, state); error != CELL_OK)
		return error;

	// Should return the same as device capability mask, psl1ght has it backwards in pad->h
	memset(info->info, 0, CELL_PAD_MAX_CAPABILITY_INFO * sizeof(u32));
	info->info[0] = device_of(*pad, state).capability;

	return CELL_OK;
}

// A change of a port's setting: @set on, @clear off
static void port_change_setting(u32 port_no, u32 set, u32 clear)
{
	Pad& pad = port_pad(port_no);
	retro_cas_record_txn_t txn;
	do
	{
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));
		state->port_setting = (state->port_setting | set) & ~clear;
	}
	while (!retro_cas_record_commit(&pad.m_guest, &txn));
}

error_code cellPadSetPortSetting(u32 port_no, u32 port_setting)
{
	cellPad.trace("cellPadSetPortSetting(port_no=%d, port_setting=0x%x)", port_no, port_setting);

	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	// CELL_PAD_ERROR_NO_DEVICE is not returned in this case.
	if (port_no >= CELL_PAD_MAX_PORT_NUM)
		return CELL_OK;

	// Both in one change of the port
	constexpr u32 both = CELL_PAD_SETTING_PRESS_ON | CELL_PAD_SETTING_SENSOR_ON;
	port_change_setting(port_no, port_setting & both, ~port_setting & both);

	// can also return CELL_PAD_ERROR_UNSUPPORTED_GAMEPAD <- Update: seems to be just internal and ignored

	return CELL_OK;
}

error_code cellPadInfoPressMode(u32 port_no)
{
	cellPad.trace("cellPadInfoPressMode(port_no=%d)", port_no);

	Pad* pad = nullptr;
	pad_guest_state state;

	if (const error_code error = port_device_call(port_no, true, pad, state); error != CELL_OK)
		return error;

	return not_an_error((device_of(*pad, state).capability & CELL_PAD_CAPABILITY_PRESS_MODE) ? 1 : 0);
}

error_code cellPadInfoSensorMode(u32 port_no)
{
	cellPad.trace("cellPadInfoSensorMode(port_no=%d)", port_no);

	Pad* pad = nullptr;
	pad_guest_state state;

	if (const error_code error = port_device_call(port_no, true, pad, state); error != CELL_OK)
		return error;

	return not_an_error((device_of(*pad, state).capability & CELL_PAD_CAPABILITY_SENSOR_MODE) ? 1 : 0);
}

// cellPadSetPressMode and cellPadSetSensorMode: the port's @setting bit on
// or off, if its device has @capability
static error_code port_set_mode(u32 port_no, u32 mode, u32 capability, u32 setting)
{
	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (port_no >= CELL_MAX_PADS || mode > 1)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	// CELL_PAD_ERROR_NO_DEVICE is not returned in this case.
	if (port_no >= CELL_PAD_MAX_PORT_NUM)
		return CELL_OK;

	Pad& pad = port_pad(port_no);

	for (;;)
	{
		retro_cas_record_txn_t txn;
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));

		// TODO: find out if this is checked here or later or at all
		if (!(device_of(pad, *state).capability & capability))
		{
			retro_cas_record_abort(&pad.m_guest, &txn);
			return CELL_PAD_ERROR_UNSUPPORTED_GAMEPAD;
		}

		if (mode)
			state->port_setting |= setting;
		else
			state->port_setting &= ~setting;

		if (retro_cas_record_commit(&pad.m_guest, &txn))
			return CELL_OK;
	}
}

error_code cellPadSetPressMode(u32 port_no, u32 mode)
{
	cellPad.trace("cellPadSetPressMode(port_no=%d, mode=%d)", port_no, mode);

	return port_set_mode(port_no, mode, CELL_PAD_CAPABILITY_PRESS_MODE, CELL_PAD_SETTING_PRESS_ON);
}

error_code cellPadSetSensorMode(u32 port_no, u32 mode)
{
	cellPad.trace("cellPadSetSensorMode(port_no=%d, mode=%d)", port_no, mode);

	return port_set_mode(port_no, mode, CELL_PAD_CAPABILITY_SENSOR_MODE, CELL_PAD_SETTING_SENSOR_ON);
}

error_code cellPadLddRegisterController()
{
	cellPad.warning("cellPadLddRegisterController()");

	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	const auto handler = pad::get_pad_thread();

	// A free port taken for this caller alone, its setting cleared with it,
	// and the game told of the connection
	const s32 handle = handler->AddLddPad();

	if (handle < 0)
		return CELL_PAD_ERROR_TOO_MANY_DEVICES;

	return not_an_error(handle);
}

error_code cellPadLddDataInsert(s32 handle, vm::ptr<CellPadData> data)
{
	cellPad.trace("cellPadLddDataInsert(handle=%d, data=*0x%x)", handle, data);

	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (handle < 0 || static_cast<u32>(handle) >= CELL_PAD_MAX_PORT_NUM || !data) // data == NULL stalls on decr
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	Pad& pad = port_pad(handle);

	for (;;)
	{
		retro_cas_record_txn_t txn;
		pad_guest_state* state = static_cast<pad_guest_state*>(retro_cas_record_begin(&pad.m_guest, &txn));

		if (!state->ldd)
		{
			retro_cas_record_abort(&pad.m_guest, &txn);
			return CELL_PAD_ERROR_NO_DEVICE;
		}

		state->ldd_data = *data;

		if (retro_cas_record_commit(&pad.m_guest, &txn))
			return CELL_OK;
	}
}

error_code cellPadLddGetPortNo(s32 handle)
{
	cellPad.trace("cellPadLddGetPortNo(handle=%d)", handle);

	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (handle < 0 || static_cast<u32>(handle) >= CELL_PAD_MAX_PORT_NUM)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	pad_guest_state state;
	retro_cas_record_read(&port_pad(handle).m_guest, &state);

	if (!state.ldd)
		return CELL_PAD_ERROR_FATAL; // might be incorrect

	// Other possible return values: CELL_PAD_ERROR_EBUSY, CELL_EBUSY
	return not_an_error(handle); // handle is port
}

error_code cellPadLddUnregisterController(s32 handle)
{
	cellPad.warning("cellPadLddUnregisterController(handle=%d)", handle);

	auto& config = g_fxo->get<pad_info>();

	if (!config.initialized())
		return CELL_PAD_ERROR_UNINITIALIZED;

	if (handle < 0 || static_cast<u32>(handle) >= CELL_PAD_MAX_PORT_NUM)
		return CELL_PAD_ERROR_INVALID_PARAMETER;

	// False when no custom controller is registered there, or another
	// thread unregistered it first; the game is told of the disconnection
	if (!pad::get_pad_thread()->UnregisterLddPad(handle))
		return CELL_PAD_ERROR_NO_DEVICE;

	return CELL_OK;
}

error_code cellPadFilterIIRInit(vm::ptr<CellPadFilterIIRSos> pSos, s32 cutoff)
{
	cellPad.todo("cellPadFilterIIRInit(pSos=*0x%x, cutoff=%d)", pSos, cutoff);

	if (!pSos) // TODO: does this check for cutoff > 2 ?
	{
		return CELL_PADFILTER_ERROR_INVALID_PARAMETER;
	}

	return CELL_OK;
}

u32 cellPadFilterIIRFilter(vm::ptr<CellPadFilterIIRSos> pSos, u32 filterIn)
{
	cellPad.todo("cellPadFilterIIRFilter(pSos=*0x%x, filterIn=%d)", pSos, filterIn);

	// TODO: apply filter

	return std::clamp(filterIn, 0u, 1023u);
}

s32 sys_io_3733EA3C(u32 port_no, vm::ptr<u32> device_type, vm::ptr<CellPadData> data)
{
	// Used by the ps1 emulator built into the firmware
	// Seems to call the same function that getdataextra does
	cellPad.trace("sys_io_3733EA3C(port_no=%d, device_type=*0x%x, data=*0x%x)", port_no, device_type, data);
	return cellPadGetDataExtra(port_no, device_type, data);
}

void cellPad_init()
{
	REG_FUNC(sys_io, cellPadInit);
	REG_FUNC(sys_io, cellPadEnd);
	REG_FUNC(sys_io, cellPadClearBuf);
	REG_FUNC(sys_io, cellPadGetData);
	REG_FUNC(sys_io, cellPadGetRawData); //
	REG_FUNC(sys_io, cellPadGetDataExtra);
	REG_FUNC(sys_io, cellPadSetActDirect);
	REG_FUNC(sys_io, cellPadGetInfo); //
	REG_FUNC(sys_io, cellPadGetInfo2);
	REG_FUNC(sys_io, cellPadPeriphGetInfo);
	REG_FUNC(sys_io, cellPadPeriphGetData);
	REG_FUNC(sys_io, cellPadSetPortSetting);
	REG_FUNC(sys_io, cellPadInfoPressMode); //
	REG_FUNC(sys_io, cellPadInfoSensorMode); //
	REG_FUNC(sys_io, cellPadSetPressMode); //
	REG_FUNC(sys_io, cellPadSetSensorMode); //
	REG_FUNC(sys_io, cellPadGetCapabilityInfo); //
	REG_FUNC(sys_io, cellPadLddRegisterController);
	REG_FUNC(sys_io, cellPadLddDataInsert);
	REG_FUNC(sys_io, cellPadLddGetPortNo);
	REG_FUNC(sys_io, cellPadLddUnregisterController);

	REG_FUNC(sys_io, cellPadFilterIIRInit);
	REG_FUNC(sys_io, cellPadFilterIIRFilter);

	REG_FNID(sys_io, 0x3733EA3C, sys_io_3733EA3C);
}
