#pragma once

#include "Emu/Io/music_handler_base.h"

#include <bit>

class null_music_handler final : public music_handler_base
{
public:
	null_music_handler() : music_handler_base() {}

	void stop() override { set_state(0); } // CELL_MUSIC_PB_STATUS_STOP
	void pause() override { set_state(2); } // CELL_MUSIC_PB_STATUS_PAUSE
	void play(const std::string& /*path*/, bool automatic = false) override { set_state(1, automatic); } // CELL_MUSIC_PB_STATUS_PLAY
	void fast_forward(const std::string& /*path*/) override { set_state(3); } // CELL_MUSIC_PB_STATUS_FASTFORWARD
	void fast_reverse(const std::string& /*path*/) override { set_state(4); } // CELL_MUSIC_PB_STATUS_FASTREVERSE
	void set_volume(f32 volume) override { retro_atomic_store_release_int(&m_volume, std::bit_cast<int>(volume)); }
	f32 get_volume() const override { return std::bit_cast<f32>(retro_atomic_load_acquire_int(&m_volume)); }

private:
	mutable retro_atomic_int_t m_volume{}; // the bits of an f32
};
