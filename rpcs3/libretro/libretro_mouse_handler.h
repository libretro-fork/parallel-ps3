#pragma once

#include "Emu/Io/MouseHandler.h"

#include <retro_atomic.h>
#include <retro_spsc.h>

#include <array>

// The PS3 mice, driven by RetroArch's light guns: one mouse per port, at the
// point the gun aims at, its trigger, second trigger and third button as the
// mouse's buttons 1 to 3 and the port's mouse wheel as its wheel. A gun is
// what the System 357 arcade shooters read through the USIO board, and
// RetroArch also makes one of the mouse, so a mouse works the same way.
//
// Standalone RPCS3 has one mouse connected by default (its basic handler);
// this is its raw handler's layout, a mouse per player, which the USIO board
// needs for two guns.
//
// retro_run never takes the handler's lock: it puts the guns' moves, presses
// and wheel turns on a lock-free ring per mouse (retro_spsc), and whoever
// next reads the mice under the lock - cellMouse, cellGem, the USIO board,
// the guns - first replays them through MouseHandlerBase (take_pending), so
// the game gets the events it got before. Should a ring fill while nothing
// reads the mice, the mouse catches up with where the gun is and what it
// holds once something does.
class LibretroMouseHandler final : public MouseHandlerBase
{
public:
    LibretroMouseHandler();
    explicit LibretroMouseHandler(utils::serial* ar);
    explicit LibretroMouseHandler(utils::serial& ar) : LibretroMouseHandler(&ar) {}
    ~LibretroMouseHandler() override;

    void Init(const u32 max_connect) override;

    // Hand the guns polled for this frame to the mice; retro_run, after polling
    void update();

    // Under the lock: the events retro_run left on the rings
    void take_pending() override;

    static constexpr u32 max_guns = 4;

private:
    void init_rings();

    struct last_state
    {
        s32 x = -1;
        s32 y = -1;
        u8 buttons = 0;
    };

    // retro_run's alone
    std::array<last_state, max_guns> m_last{};
    u32 m_seen_inits = 0;

    // A move to (x, y), a button pressed or released, or a wheel turn
    struct event
    {
        u8 kind = 0;
        u8 button = 0;
        u8 pressed = 0;
        s8 wheel = 0;
        s32 x = 0;
        s32 y = 0;
    };

    std::array<retro_spsc_t, max_guns> m_events{};

    // Where each gun is and what it holds, for a mouse whose ring filled:
    // x | y << 16 (0..0xfffe each), the buttons, and whether events were lost
    std::array<retro_atomic_int_t, max_guns> m_latest_pos;
    std::array<retro_atomic_int_t, max_guns> m_latest_buttons;
    std::array<retro_atomic_int_t, max_guns> m_lost;

    // The mice Init set up, for retro_run, and how often it did
    retro_atomic_int_t m_ready;
    retro_atomic_int_t m_inits;

    void push(u32 index, const event& e);
};

// Update the running game's mice, if it has any; retro_run, after polling
void libretro_mouse_update();
