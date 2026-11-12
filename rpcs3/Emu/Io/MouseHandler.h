#pragma once

#include <cstring>
#include "Utilities/StrFmt.h"
#include "util/init_mutex.hpp"
#include "Emu/system_config_types.h"

#include <retro_atomic.h>

// C, with C's casts
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <rthreads/retro_cas_record.h>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

// TODO: HLE info (constants, structs, etc.) should not be available here

enum
{
	// is_supported
	CELL_MOUSE_INFO_TABLET_NOT_SUPPORTED = 0,
	CELL_MOUSE_INFO_TABLET_SUPPORTED     = 1,

	// mode
	CELL_MOUSE_INFO_TABLET_MOUSE_MODE    = 1,
	CELL_MOUSE_INFO_TABLET_TABLET_MODE   = 2,
};

enum MousePortStatus
{
	CELL_MOUSE_STATUS_DISCONNECTED = 0x00000000,
	CELL_MOUSE_STATUS_CONNECTED    = 0x00000001,
};

enum MouseDataUpdate
{
	CELL_MOUSE_DATA_UPDATE = 1,
	CELL_MOUSE_DATA_NON    = 0,
};

enum MouseButtonCodes
{
	CELL_MOUSE_BUTTON_1 = 0x00000001,
	CELL_MOUSE_BUTTON_2 = 0x00000002,
	CELL_MOUSE_BUTTON_3 = 0x00000004,
	CELL_MOUSE_BUTTON_4 = 0x00000008,
	CELL_MOUSE_BUTTON_5 = 0x00000010,
	CELL_MOUSE_BUTTON_6 = 0x00000020,
	CELL_MOUSE_BUTTON_7 = 0x00000040,
	CELL_MOUSE_BUTTON_8 = 0x00000080,
};

enum
{
	CELL_MOUSE_INFO_INTERCEPTED = 1
};

static inline MouseButtonCodes get_mouse_button_code(int i)
{
	switch (i)
	{
	case 0:	return CELL_MOUSE_BUTTON_1;
	case 1:	return CELL_MOUSE_BUTTON_2;
	case 2:	return CELL_MOUSE_BUTTON_3;
	case 3:	return CELL_MOUSE_BUTTON_4;
	case 4:	return CELL_MOUSE_BUTTON_5;
	case 5:	return CELL_MOUSE_BUTTON_6;
	case 6:	return CELL_MOUSE_BUTTON_7;
	case 7:	return CELL_MOUSE_BUTTON_8;
	default: fmt::throw_exception("get_mouse_button_code: Invalid index %d", i);
	}
}

static const u32 MOUSE_MAX_DATA_LIST_NUM = 8;
static const u32 MOUSE_MAX_CODES = 64;

struct MouseData
{
	u8 update = 0;
	u8 buttons = 0;
	s8 x_axis = 0;
	s8 y_axis = 0;
	s8 wheel = 0;
	s8 tilt = 0;

	s32 pixel_x = 0;
	s32 pixel_y = 0;
};

struct Mouse
{
	f32 x_pos = 0.0f;
	f32 y_pos = 0.0f;
	s32 x_pos_pixels = 0;
	s32 y_pos_pixels = 0;
	u8 buttons = 0; // actual mouse button positions
};

// A mouse as the game's side has it: where it is, what it holds, and the
// data waiting for cellMouseGetData, oldest first. Plain data: one
// retro_cas_record a port, read whole and changed whole with no lock.
struct mouse_port
{
	u32 applied = 0; // how many of the frontend's events it has had so far
	Mouse mouse{};
	u32 list_count = 0;
	MouseData list[MOUSE_MAX_DATA_LIST_NUM + 1]{};
};

// The mouse alone, for those that read no data (cellGem, the arcade board,
// the USB guns): a record of its own, a few words to read, which takes the
// frontend's events by itself. The same events applied the same way: it is
// where the port's mouse is once the port has had them too.
struct mouse_view
{
	u32 applied = 0;
	Mouse mouse{};
};

// The PS3's mice. No lock: cellMouse, cellGem, the arcade board and the USB
// guns read and change a port through read_port and change_port, from
// whichever of the game's threads; what the frontend's thread has for a port
// since it was last read (a handler's apply_pending) is applied on the way,
// to the copy, so that no thread has to hand anything to another.
class MouseHandlerBase
{
public:
	// cellMouseInit takes up to seven
	static constexpr u32 max_ports = 7;

protected:
	retro_cas_record_t m_port[max_ports];
	mouse_port m_port_slots[max_ports][RETRO_CAS_RECORD_SLOTS];
	retro_cas_record_t m_view[max_ports];
	mouse_view m_view_slots[max_ports][RETRO_CAS_RECORD_SLOTS];

	// How many events the frontend's thread has made for each port so far:
	// a handler's, written by that thread alone. A port or a view that has
	// applied as many has nothing to take, and apply_pending is not called.
	retro_atomic_int_t m_head[max_ports];

	// What cellMouseGetInfo tells of the mice: set when they are set up
	retro_atomic_int_t m_max_connect;
	retro_atomic_int_t m_now_connect;
	retro_atomic_int_t m_count;  // how many mice there are (ports with a record in use)
	retro_atomic_int_t m_flags;  // CELL_MOUSE_INFO_INTERCEPTED
	retro_atomic_int_t m_null;   // the handler has no mice at all
	retro_atomic_int_t m_status[max_ports];
	retro_atomic_int_t m_mode[max_ports];
	retro_atomic_int_t m_tablet_is_supported[max_ports];
	retro_atomic_int_t m_ids[max_ports]; // vendor | product << 16

	// What the frontend's thread has for @port past copy.applied, applied to
	// @copy, and copy.applied moved on. Writes nothing else.
	virtual void apply_pending(u32 /*port*/, mouse_port& /*copy*/) {}

	// For Init: @max_connect mice, the first @connected of them connected,
	// each starting from nothing with the frontend's events up to
	// @applied[port] behind it
	void set_ports(u32 max_connect, u32 connected, bool null_handler, const u32* applied);

public:
	virtual void Init(const u32 max_connect) = 0;
	virtual ~MouseHandlerBase() = default;

	SAVESTATE_INIT_POS(18);

	MouseHandlerBase();
	MouseHandlerBase(const MouseHandlerBase&) = delete;
	MouseHandlerBase(utils::serial* ar);
	MouseHandlerBase(utils::serial& ar) : MouseHandlerBase(&ar) {}
	void save(utils::serial& ar);

	// A button, a wheel turn or a move as the mouse and its data take it
	static void button(mouse_port& port, u8 button, bool pressed);
	static void scroll(mouse_port& port, s8 x, s8 y);
	static void move(mouse_port& port, s32 x_pos_new, s32 y_pos_new, s32 x_max, s32 y_max, bool is_relative = false, s32 x_delta = 0, s32 y_delta = 0);

	// The mouse at @port as it is now, the frontend's events applied. False
	// when there is no such mouse or it is not connected.
	bool read_port(u32 port, mouse_port& out);

	// The mouse alone, for the guns: false when there is no such mouse, and
	// a mouse at rest when it is there but not connected.
	bool read_mouse(u32 port, Mouse& out);

	// The same, then @f on it (f(mouse_port&)), as one change of the port:
	// made again if another thread changed the port meanwhile, so @f touches
	// only the copy and the caller's own output.
	template <typename F>
	bool change_port(u32 port, F&& f)
	{
		if (!connected(port))
			return false;

		for (;;)
		{
			mouse_port copy, before;
			const retro_cas_record_word_t version = retro_cas_record_read_versioned(&m_port[port], &copy);
			std::memcpy(static_cast<void*>(&before), &copy, sizeof(before));

			if (copy.applied != static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port])))
				apply_pending(port, copy);
			f(copy);

			if (!std::memcmp(&before, &copy, sizeof(before)) || retro_cas_record_replace(&m_port[port], version, &copy))
				return true;
		}
	}

	// cellMouseGetData and cellMouseGetDataList: up to @max of the port's
	// data taken off its list into @out, oldest first; with @drop the list is
	// emptied and none is given. How many were given; -1 when there is no
	// such mouse.
	s32 take_data(u32 port, MouseData* out, u32 max, bool drop);

	void SetIntercepted(bool intercepted);

	u32 max_connect() const { return static_cast<u32>(retro_atomic_load_acquire_int(&m_max_connect)); }
	u32 now_connect() const { return static_cast<u32>(retro_atomic_load_acquire_int(&m_now_connect)); }
	u32 count() const { return static_cast<u32>(retro_atomic_load_acquire_int(&m_count)); }
	u32 info() const { return static_cast<u32>(retro_atomic_load_acquire_int(&m_flags)); }
	bool is_null_handler() const { return retro_atomic_load_acquire_int(&m_null) != 0; }
	u32 status(u32 port) const { return port < max_ports ? static_cast<u32>(retro_atomic_load_acquire_int(&m_status[port])) : 0; }
	u32 mode(u32 port) const { return port < max_ports ? static_cast<u32>(retro_atomic_load_acquire_int(&m_mode[port])) : 0; }
	void set_mode(u32 port, u32 mode) { if (port < max_ports) retro_atomic_store_release_int(&m_mode[port], static_cast<int>(mode)); }
	u32 tablet_is_supported(u32 port) const { return port < max_ports ? static_cast<u32>(retro_atomic_load_acquire_int(&m_tablet_is_supported[port])) : 0; }
	u16 vendor_id(u32 port) const { return port < max_ports ? static_cast<u16>(retro_atomic_load_acquire_int(&m_ids[port])) : 0; }
	u16 product_id(u32 port) const { return port < max_ports ? static_cast<u16>(static_cast<u32>(retro_atomic_load_acquire_int(&m_ids[port])) >> 16) : 0; }

	// There is a mouse at @port and it is connected
	bool connected(u32 port) const;

	stx::init_mutex init;

	mouse_handler type = mouse_handler::null;
};
