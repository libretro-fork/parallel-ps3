#include "stdafx.h"
#include "MouseHandler.h"
#include "Emu/System.h"
#include "util/serialization.hpp"

#include <algorithm>

MouseHandlerBase::MouseHandlerBase()
{
	for (u32 i = 0; i < max_ports; i++)
	{
		retro_cas_record_init(&m_port[i], m_port_slots[i], sizeof(mouse_port));
		retro_cas_record_init(&m_view[i], m_view_slots[i], sizeof(mouse_view));
		retro_atomic_int_init(&m_head[i], 0);
		retro_atomic_int_init(&m_status[i], 0);
		retro_atomic_int_init(&m_mode[i], 0);
		retro_atomic_int_init(&m_tablet_is_supported[i], 0);
		retro_atomic_int_init(&m_ids[i], 0);
	}

	retro_atomic_int_init(&m_max_connect, 0);
	retro_atomic_int_init(&m_now_connect, 0);
	retro_atomic_int_init(&m_count, 0);
	retro_atomic_int_init(&m_flags, 0);
	retro_atomic_int_init(&m_null, 0);
}

MouseHandlerBase::MouseHandlerBase(utils::serial* ar)
	: MouseHandlerBase()
{
	if (!ar)
	{
		return;
	}

	const u32 max_connect = ar->pop<u32>();
	retro_atomic_store_release_int(&m_max_connect, static_cast<int>(max_connect));

	if (max_connect)
	{
		Emu.PostponeInitCode([this, max_connect]()
		{
			Init(max_connect);
			auto lk = init.init();
		});
	}
}

void MouseHandlerBase::save(utils::serial& ar)
{
	const auto inited = init.access();
	ar(inited ? max_connect() : 0);
}

void MouseHandlerBase::set_ports(u32 max_connect, u32 connected, bool null_handler, const u32* applied)
{
	const u32 count = null_handler ? 0 : std::min<u32>(max_connect, max_ports);

	for (u32 i = 0; i < max_ports; i++)
	{
		retro_cas_record_txn_t txn;
		do
		{
			mouse_port* port = static_cast<mouse_port*>(retro_cas_record_begin(&m_port[i], &txn));
			*port = {};
			port->applied = applied ? applied[i] : 0;
		}
		while (!retro_cas_record_commit(&m_port[i], &txn));

		do
		{
			mouse_view* view = static_cast<mouse_view*>(retro_cas_record_begin(&m_view[i], &txn));
			*view = {};
			view->applied = applied ? applied[i] : 0;
		}
		while (!retro_cas_record_commit(&m_view[i], &txn));

		const bool here = i < count;
		retro_atomic_store_release_int(&m_status[i], here && i < connected ? CELL_MOUSE_STATUS_CONNECTED : CELL_MOUSE_STATUS_DISCONNECTED);
		retro_atomic_store_release_int(&m_mode[i], here ? CELL_MOUSE_INFO_TABLET_MOUSE_MODE : 0);
		retro_atomic_store_release_int(&m_tablet_is_supported[i], here ? CELL_MOUSE_INFO_TABLET_NOT_SUPPORTED : 0);
		retro_atomic_store_release_int(&m_ids[i], here ? 0x1234 | (0x1234 << 16) : 0);
	}

	retro_atomic_store_release_int(&m_flags, 0);
	retro_atomic_store_release_int(&m_null, null_handler);
	retro_atomic_store_release_int(&m_max_connect, static_cast<int>(max_connect));
	retro_atomic_store_release_int(&m_now_connect, static_cast<int>(null_handler ? 0 : std::min<u32>(connected, count)));
	// Last: from here the mice are there for the other threads
	retro_atomic_store_release_int(&m_count, static_cast<int>(count));
}

bool MouseHandlerBase::connected(u32 port) const
{
	return port < count() && status(port) == CELL_MOUSE_STATUS_CONNECTED;
}

bool MouseHandlerBase::read_port(u32 port, mouse_port& out)
{
	if (!connected(port))
		return false;

	for (;;)
	{
		const retro_cas_record_word_t version = retro_cas_record_read_versioned(&m_port[port], &out);

		// Nothing new from the frontend: nothing shared is written
		if (out.applied == static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port])))
			return true;

		const u32 applied = out.applied;
		apply_pending(port, out);

		if (out.applied == applied || retro_cas_record_replace(&m_port[port], version, &out))
			return true;
	}
}

bool MouseHandlerBase::read_mouse(u32 port, Mouse& out)
{
	if (port >= count())
		return false;

	if (status(port) != CELL_MOUSE_STATUS_CONNECTED)
	{
		out = {};
		return true;
	}

	for (;;)
	{
		mouse_view view;
		const retro_cas_record_word_t version = retro_cas_record_read_versioned(&m_view[port], &view);

		if (view.applied != static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port])))
		{
			// The events, as a port takes them; the data they make is the port's
			mouse_port copy;
			copy.applied = view.applied;
			copy.mouse = view.mouse;
			apply_pending(port, copy);

			if (copy.applied != view.applied)
			{
				view.applied = copy.applied;
				view.mouse = copy.mouse;

				if (!retro_cas_record_replace(&m_view[port], version, &view))
					continue;
			}
		}

		out = view.mouse;
		return true;
	}
}

s32 MouseHandlerBase::take_data(u32 port, MouseData* out, u32 max, bool drop)
{
	if (!connected(port))
		return -1;

	// Read into, not set up first
	union port_copy
	{
		mouse_port p;
		port_copy() {}
	} copy;

	mouse_port& p = copy.p;

	for (;;)
	{
		const retro_cas_record_word_t version = retro_cas_record_read_versioned(&m_port[port], &p);

		if (p.applied != static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port])))
			apply_pending(port, p);
		else if (!p.list_count)
			return 0; // nothing new and nothing waiting: nothing shared is written

		const u32 given = drop ? 0 : std::min<u32>(p.list_count, max);

		for (u32 i = 0; i < given; i++)
			out[i] = p.list[i];

		// cellMouseGetDataList empties the list whatever it gave;
		// cellMouseGetData takes the first
		const u32 taken = drop || max > 1 ? p.list_count : given;

		for (u32 i = taken; i < p.list_count; i++)
			p.list[i - taken] = p.list[i];

		p.list_count -= taken;

		if (retro_cas_record_replace(&m_port[port], version, &p))
			return static_cast<s32>(given);
	}
}

// Room for one more in the port's data: the oldest goes when there are
// already more than MOUSE_MAX_DATA_LIST_NUM
static MouseData& new_data(mouse_port& port)
{
	if (port.list_count > MOUSE_MAX_DATA_LIST_NUM)
	{
		for (u32 i = 1; i < port.list_count; i++)
			port.list[i - 1] = port.list[i];
		port.list_count--;
	}

	MouseData& data = port.list[port.list_count++];
	data = {};
	data.update = CELL_MOUSE_DATA_UPDATE;
	data.buttons = port.mouse.buttons;
	return data;
}

void MouseHandlerBase::button(mouse_port& port, u8 button, bool pressed)
{
	if (pressed)
		port.mouse.buttons |= button;
	else
		port.mouse.buttons &= ~button;

	MouseData& data = new_data(port);
	data.pixel_x = port.mouse.x_pos_pixels;
	data.pixel_y = port.mouse.y_pos_pixels;
}

void MouseHandlerBase::scroll(mouse_port& port, s8 x, s8 y)
{
	MouseData& data = new_data(port);
	data.wheel = y;
	data.tilt = x;
	data.pixel_x = port.mouse.x_pos_pixels;
	data.pixel_y = port.mouse.y_pos_pixels;
}

void MouseHandlerBase::move(mouse_port& port, s32 x_pos_new, s32 y_pos_new, s32 x_max, s32 y_max, bool is_relative, s32 x_delta, s32 y_delta)
{
	Mouse& mouse = port.mouse;
	MouseData& data = new_data(port);

	if (!is_relative)
	{
		// The PS3 expects relative mouse movement, so we have to calculate it with the last absolute position.
		x_delta = x_pos_new - mouse.x_pos_pixels;
		y_delta = y_pos_new - mouse.y_pos_pixels;
	}

	data.x_axis = static_cast<s8>(std::clamp(x_delta, -127, 128));
	data.y_axis = static_cast<s8>(std::clamp(y_delta, -127, 128));
	data.pixel_x = x_pos_new;
	data.pixel_y = y_pos_new;

	mouse.x_pos = x_max == 0 ? 0.0f : std::clamp(x_pos_new / static_cast<f32>(x_max), 0.0f, 1.0f);
	mouse.y_pos = y_max == 0 ? 0.0f : std::clamp(y_pos_new / static_cast<f32>(y_max), 0.0f, 1.0f);
	mouse.x_pos_pixels = x_pos_new;
	mouse.y_pos_pixels = y_pos_new;
}

void MouseHandlerBase::SetIntercepted(bool intercepted)
{
	retro_atomic_store_release_int(&m_flags, intercepted ? CELL_MOUSE_INFO_INTERCEPTED : 0);

	if (intercepted)
	{
		// The mice start from nothing, with the frontend's events so far
		// behind them
		for (u32 i = 0; i < max_ports; i++)
		{
			change_port(i, [](mouse_port& port)
			{
				const u32 applied = port.applied;
				port = {};
				port.applied = applied;
			});

			retro_cas_record_txn_t txn;
			do
			{
				mouse_view* view = static_cast<mouse_view*>(retro_cas_record_begin(&m_view[i], &txn));
				const u32 applied = view->applied;
				*view = {};
				view->applied = applied;
			}
			while (!retro_cas_record_commit(&m_view[i], &txn));
		}
	}
}
