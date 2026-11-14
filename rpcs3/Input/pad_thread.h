#pragma once

#include "util/types.hpp"
#include <retro_atomic.h>
#include "Emu/Io/pad_types.h"
#include "Emu/Io/pad_config.h"
#include "Emu/Io/pad_config_types.h"
#include "Utilities/mutex.h"

#include <map>
#include <mutex>
#include <string_view>
#include <string>

class PadHandlerBase;

class pad_thread
{
public:
	pad_thread(void* curthread, void* curwindow, std::string_view title_id); // void * instead of QThread * and QWindow * because of include in emucore
	pad_thread(const pad_thread&) = delete;
	pad_thread& operator=(const pad_thread&) = delete;
	~pad_thread();

	void operator()();

	PadInfo& GetInfo() { return m_info; }
	std::array<std::shared_ptr<Pad>, CELL_PAD_MAX_PORT_NUM>& GetPads() { return m_pads; }
	void SetRumble(u32 pad, u8 large_motor, u8 small_motor);
	void SetIntercepted(bool intercepted);

	s32 AddLddPad();
	// False when no custom controller is registered at @handle (any more)
	bool UnregisterLddPad(u32 handle);

	void open_home_menu();

	std::map<pad_handler, std::shared_ptr<PadHandlerBase>>& get_handlers() { return m_handlers; }

	static std::shared_ptr<PadHandlerBase> GetHandler(pad_handler type);
	static void InitPadConfig(cfg_pad& cfg, pad_handler type, std::shared_ptr<PadHandlerBase>& handler);

	static auto constexpr thread_name = "Pad Thread"sv;

	void Init();

protected:
	void InitLddPad(u32 handle, const u32* port_status);

	// List of all handlers
	std::map<pad_handler, std::shared_ptr<PadHandlerBase>> m_handlers;

	// Used for pad_handler::keyboard
	void* m_curthread = nullptr;
	void* m_curwindow = nullptr;

	PadInfo m_info;
	std::array<std::shared_ptr<Pad>, CELL_PAD_MAX_PORT_NUM> m_pads{};
	std::array<bool, CELL_PAD_MAX_PORT_NUM> m_pads_connected{};

	// The custom (LDD) controllers registered, counted by the game's threads
	// and read by retro_run
	retro_atomic_int_t num_ldd_pad;
	void apply_ldd(u32 handle, bool notify);

public:
	void apply_copilots();
	// The libretro core drives its handler from retro_run instead of running
	// this thread; this is the rest of what the thread's loop does after the
	// handlers were processed.
	void frontend_update(u32 connected_devices);

private:
	void update_pad_states();
	void update_ignore_input();

	u32 m_mask_start_press_to_resume = 0;
	u64 m_track_start_press_begin_timestamp = 0;
	bool m_resume_emulation_flag = false;
	bool m_ps_button_pressed = false;
	retro_atomic_int_t m_home_menu_open{};
};

namespace pad
{
	extern retro_atomic_ptr_t g_pad_thread; // a pad_thread*
	extern std::string g_title_id;
	extern retro_atomic_int_t g_enabled;
	extern retro_atomic_int_t g_reset;
	extern retro_atomic_int_t g_started;
	extern retro_atomic_int_t g_home_menu_requested;

	static inline class pad_thread* get_pad_thread(bool relaxed = false)
	{
		const auto handler = static_cast<pad_thread*>(retro_atomic_load_acquire_ptr(&g_pad_thread));

		return relaxed ? handler : ensure(handler);
	}

	static inline void set_enabled(bool enabled)
	{
		retro_atomic_store_release_int(&g_enabled, enabled);
	}

	static inline void reset(std::string_view title_id)
	{
		g_title_id = title_id;
		retro_atomic_store_release_int(&g_reset, 1);
	}

	static inline void SetIntercepted(bool intercepted)
	{
		const auto handler = get_pad_thread();
		handler->SetIntercepted(intercepted);
	}
}
