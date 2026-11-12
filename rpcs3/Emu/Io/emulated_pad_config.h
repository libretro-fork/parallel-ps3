#pragma once

#include "Utilities/Config.h"
#include "Utilities/mutex.h"
#include "pad_types.h"
#include "MouseHandler.h"

// C, with C's casts
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <rthreads/retro_seqlock.h>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

#include <array>
#include <map>
#include <set>
#include <vector>

LOG_CHANNEL(cfg_log, "CFG");

template <typename T>
class cfg_pad_btn : public cfg::_enum<pad_button>
{
public:
	cfg_pad_btn(cfg::node* owner, const std::string& name, T id, pad_button value, bool dynamic = false)
		: cfg::_enum<pad_button>(owner, name, value, dynamic)
		, m_btn_id(id)
	{};

	T btn_id() const
	{
		return m_btn_id;
	}

private:
	T m_btn_id;
};

template <typename T>
struct emulated_pad_config : cfg::node
{
public:
	using cfg::node::node;

	pad_button get_pad_button(T id)
	{
		std::lock_guard lock(m_mutex);

		if (const cfg_pad_btn<T>* item = get_button(id))
		{
			return item->get();
		}

		return pad_button::pad_button_max_enum;
	}

	pad_button default_pad_button(T id)
	{
		std::lock_guard lock(m_mutex);

		if (const cfg_pad_btn<T>* item = get_button(id))
		{
			return item->get_default();
		}

		return pad_button::pad_button_max_enum;
	}

	void set_button(T id, pad_button btn_id)
	{
		std::lock_guard lock(m_mutex);

		if (cfg_pad_btn<T>* item = get_button(id))
		{
			item->set(btn_id);
		}
	}

	void init_buttons()
	{
		std::lock_guard lock(m_mutex);

		for (const auto& n : get_nodes())
		{
			if (auto* pbtn = dynamic_cast<cfg_pad_btn<T>*>(n))
				init_button(pbtn);
		}

		publish_table();
	}

	void clear_buttons()
	{
		std::lock_guard lock(m_mutex);

		buttons.clear();
		publish_table();
	}

	struct input_value
	{
	public:
		T btn = {};
		pad_button pad_btn = pad_button::pad_button_max_enum;
		u16 value = 0;
		u16 max_value = 255;
		bool pressed = false;

		u8 to_8bit() const
		{
			if (max_value == 255) return static_cast<u8>(value & 0x00FF);
			return static_cast<u8>(std::clamp(255.0f * (static_cast<f32>(std::min(value, max_value)) / ensure(max_value)), 0.0f, 255.0f));
		}

		u16 to_10bit() const
		{
			if (max_value == 1023) return value & 0x03FF;
			return static_cast<u16>(std::clamp(1023.0f * (static_cast<f32>(std::min(value, max_value)) / ensure(max_value)), 0.0f, 1023.0f));
		}
	};

	// @func(const input_value&, bool& abort) for each of this controller's
	// buttons on the pad's. The callback is called as what it is - no
	// std::function made of it for the poll, which put whatever it holds on
	// to on the heap each time - and the pad is borrowed for the call.
	template <typename F>
	void handle_input(const std::shared_ptr<Pad>& pad, bool press_only, F&& func) const
	{
		if (!pad || pad->is_copilot())
			return;

		// The mapping as it is, whole, for this poll
		button_table table;
		read_table(table);

		// What the frontend's thread last handed over, read whole without a lock
		pad_external pad_ext;
		pad->read_external(pad_ext);

		for (const ButtonExternal& button : pad_ext.button_span())
		{
			if (button.m_pressed || !press_only)
			{
				if (handle_input_internal(func, table, button.m_offset, button.m_outKeyCode, button.m_value, 255, button.m_pressed, true))
				{
					return;
				}
			}
		}

		for (const AnalogStickExternal& stick : pad_ext.sticks)
		{
			if (handle_input_internal(func, table, stick.m_offset, get_axis_keycode(stick.m_offset, stick.m_value), stick.m_value, 255, true, true))
			{
				return;
			}
		}

		for (const AnalogSensor& sensor : pad->m_sensors)
		{
			if (handle_input_internal(func, table, sensor.m_offset, sensor.m_keyCode, sensor.m_value, 1023, true, false))
			{
				return;
			}
		}
	}

	template <typename F>
	void handle_input(const Mouse& mouse, F&& func) const
	{
		button_table table;
		read_table(table);

		for (int i = 0; i < 8; i++)
		{
			const MouseButtonCodes cell_code = get_mouse_button_code(i);
			if ((mouse.buttons & cell_code))
			{
				const pad_button button = static_cast<pad_button>(static_cast<int>(pad_button::mouse_button_1) + i);
				const u32 offset = pad_button_offset(button);
				const u32 keycode = pad_button_keycode(button);

				if (handle_input_internal(func, table, offset, keycode, 255, 255, true, true))
				{
					return;
				}
			}
		}
	}

protected:
	// For the configuration's own threads (loading, saving, the settings):
	// the list of buttons below. A poll takes no lock.
	mutable shared_mutex m_mutex;
	std::vector<cfg_pad_btn<T>*> buttons;

	// The mapping the polls read: each of the controller's buttons with the
	// pad button it is on, as its offset and key code in the pad's data,
	// sorted by those (buttons on the same one in the order declared). Made
	// again whenever the buttons are (publish_table, under m_mutex: one
	// writer), in atomics under a sequence count that is odd meanwhile; a
	// poll copies it whole and copies again if the count moved, so it never
	// waits, holds nothing while the caller's callback runs, and never sees
	// half of one mapping and half of the next. It holds values, not the
	// configuration's nodes: a poll does not read those while a load writes
	// them.
	static constexpr u32 max_table = 64;

	struct table_entry
	{
		u32 offset;
		u32 keycode;
		T btn;
		pad_button pad_btn;
	};

	struct button_table
	{
		u32 count;
		table_entry entries[max_table];
	};

	mutable retro_seqlock_t m_table_seq{};
	mutable retro_atomic_int_t m_table_count{};
	mutable retro_atomic_int_t m_table[max_table][4]{};

	cfg_pad_btn<T>* get_button(T id)
	{
		for (cfg_pad_btn<T>* item : buttons)
		{
			if (item && item->btn_id() == id)
			{
				return item;
			}
		}

		return nullptr;
	}

	void init_button(cfg_pad_btn<T>* pbtn)
	{
		if (!pbtn) return;
		buttons.push_back(pbtn);
	}

	// m_mutex held
	void publish_table()
	{
		button_table table{};
		ensure(buttons.size() <= max_table);

		for (const cfg_pad_btn<T>* item : buttons)
		{
			const pad_button pad_btn = item->get();
			const table_entry entry{pad_button_offset(pad_btn), pad_button_keycode(pad_btn), item->btn_id(), pad_btn};

			// After the ones before it that are on the same pad button
			u32 at = table.count;
			while (at > 0 && (table.entries[at - 1].offset > entry.offset || (table.entries[at - 1].offset == entry.offset && table.entries[at - 1].keycode > entry.keycode)))
			{
				table.entries[at] = table.entries[at - 1];
				at--;
			}
			table.entries[at] = entry;
			table.count++;
		}

		retro_seqlock_write_begin(&m_table_seq);
		retro_atomic_store_relaxed_int(&m_table_count, static_cast<int>(table.count));
		for (u32 i = 0; i < table.count; i++)
		{
			retro_atomic_store_relaxed_int(&m_table[i][0], static_cast<int>(table.entries[i].offset));
			retro_atomic_store_relaxed_int(&m_table[i][1], static_cast<int>(table.entries[i].keycode));
			retro_atomic_store_relaxed_int(&m_table[i][2], static_cast<int>(table.entries[i].btn));
			retro_atomic_store_relaxed_int(&m_table[i][3], static_cast<int>(table.entries[i].pad_btn));
		}
		retro_seqlock_write_end(&m_table_seq);
	}

	void read_table(button_table& table) const
	{
		int seq;
		do
		{
			seq = retro_seqlock_read_begin(&m_table_seq);
			table.count = std::min<u32>(static_cast<u32>(retro_atomic_load_relaxed_int(&m_table_count)), max_table);
			for (u32 i = 0; i < table.count; i++)
			{
				table.entries[i].offset = static_cast<u32>(retro_atomic_load_relaxed_int(&m_table[i][0]));
				table.entries[i].keycode = static_cast<u32>(retro_atomic_load_relaxed_int(&m_table[i][1]));
				table.entries[i].btn = static_cast<T>(retro_atomic_load_relaxed_int(&m_table[i][2]));
				table.entries[i].pad_btn = static_cast<pad_button>(retro_atomic_load_relaxed_int(&m_table[i][3]));
			}
		}
		while (retro_seqlock_read_retry(&m_table_seq, seq));
	}

	// The first of the controller's buttons on a pad button, and how many
	static const table_entry* find_button(const button_table& table, u32 offset, u32 keycode, u32& count)
	{
		u32 lo = 0, hi = table.count;
		while (lo < hi)
		{
			const u32 mid = (lo + hi) / 2;
			const table_entry& e = table.entries[mid];
			if (e.offset < offset || (e.offset == offset && e.keycode < keycode))
				lo = mid + 1;
			else
				hi = mid;
		}

		u32 end = lo;
		while (end < table.count && table.entries[end].offset == offset && table.entries[end].keycode == keycode)
			end++;

		count = end - lo;
		return &table.entries[lo];
	}

	template <typename F>
	bool handle_input_internal(F& func, const button_table& table, u32 offset, u32 keycode, u16 value, u16 max_value, bool pressed, bool check_axis) const
	{
		u32 count = 0;
		const table_entry* btns = find_button(table, offset, keycode, count);
		if (!count)
		{
			if (check_axis)
			{
				switch (offset)
				{
				case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X:
				case CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y:
				case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X:
				case CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y:
					return handle_input_internal(func, table, offset, static_cast<u32>(axis_direction::both), value, max_value, pressed, false);
				default:
					break;
				}
			}
			return false;
		}

		bool abort = false;

		for (u32 i = 0; i < count && !abort; i++)
		{
			const input_value params
			{
				.btn = btns[i].btn,
				.pad_btn = btns[i].pad_btn,
				.value = value,
				.max_value = max_value,
				.pressed = pressed
			};
			func(params, abort);
		}

		return abort;
	}
};

template <typename T, u32 Count>
struct emulated_pads_config : cfg::node
{
	emulated_pads_config(std::string id) : cfg_id(std::move(id))
	{
		for (u32 i = 0; i < Count; i++)
		{
			players.at(i) = std::make_shared<T>(this, fmt::format("Player %d", i + 1));
		}
	}

	shared_mutex m_mutex;
	std::string cfg_id;
	std::array<std::shared_ptr<T>, Count> players;

	bool load()
	{
		m_mutex.lock();

		bool result = false;
		const std::string cfg_name = fmt::format("%s%s.yml", fs::get_config_dir(true), cfg_id);
		cfg_log.notice("Loading %s config: %s", cfg_id, cfg_name);

		from_default();

		for (std::shared_ptr<T>& player : players)
		{
			player->clear_buttons();
		}

		if (fs::file cfg_file{ cfg_name, fs::read })
		{
			if (const std::string content = cfg_file.to_string(); !content.empty())
			{
				result = from_string(content);
			}

			for (std::shared_ptr<T>& player : players)
			{
				player->init_buttons();
			}

			m_mutex.unlock();
		}
		else
		{
			m_mutex.unlock();
			save();
		}

		return result;
	}

	void save()
	{
		std::lock_guard lock(m_mutex);

		const std::string cfg_name = fmt::format("%s%s.yml", fs::get_config_dir(true), cfg_id);
		cfg_log.notice("Saving %s config to '%s'", cfg_id, cfg_name);

		if (!fs::create_path(fs::get_parent_dir(cfg_name)))
		{
			cfg_log.fatal("Failed to create path: %s (%s)", cfg_name, fs::g_tls_error);
		}

		if (!cfg::node::save(cfg_name))
		{
			cfg_log.error("Failed to save %s config to '%s' (error=%s)", cfg_id, cfg_name, fs::g_tls_error);
		}

		for (std::shared_ptr<T>& player : players)
		{
			player->clear_buttons();
			player->init_buttons();
		}
	}
};
