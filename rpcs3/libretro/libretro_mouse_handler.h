#pragma once

#include "Emu/Io/MouseHandler.h"

#include <retro_atomic.h>

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
// There is no lock. retro_run writes the guns' moves, presses and wheel turns
// to a log per mouse, in atomics, and never waits; whichever of the game's
// threads next reads a mouse - cellMouse, cellGem, the USIO board, the guns -
// applies what the log has past what the mouse already had (apply_pending),
// so the game gets the events it got before. The log keeps the last
// log_events: a mouse nothing read for longer catches up with where the gun
// is and what it holds.
class LibretroMouseHandler final : public MouseHandlerBase
{
public:
    LibretroMouseHandler();
    explicit LibretroMouseHandler(utils::serial* ar);
    explicit LibretroMouseHandler(utils::serial& ar) : LibretroMouseHandler(&ar) {}

    void Init(const u32 max_connect) override;

    // Hand the guns polled for this frame to the mice; retro_run, after polling
    void update();

    static constexpr u32 max_guns = 4;

    // About four seconds of a gun moving and firing every frame
    static constexpr u32 log_events = 512;

protected:
    void apply_pending(u32 port, mouse_port& copy) override;

private:
    void init_log();

    struct last_state
    {
        s32 x = -1;
        s32 y = -1;
        u8 buttons = 0;
    };

    // retro_run's alone
    last_state m_last[max_guns]{};
    u32 m_seen_inits = 0;

    // The log: event n of a mouse is at n % log_events, written before the
    // base's m_head goes past n. kind | button << 8 | pressed << 16 | wheel << 24,
    // and x | y << 16 (0..0xfffe each).
    retro_atomic_int_t m_log_a[max_guns][log_events];
    retro_atomic_int_t m_log_b[max_guns][log_events];

    // Where each gun is and what it holds, for a mouse the log has gone past
    retro_atomic_int_t m_latest_pos[max_guns];
    retro_atomic_int_t m_latest_buttons[max_guns];

    // Init ran (only the first call sets the mice up), and how often
    retro_atomic_int_t m_setup;
    retro_atomic_int_t m_inits;

    void push(u32 index, u32 a, u32 b);
};

// Update the running game's mice, if it has any; retro_run, after polling
void libretro_mouse_update();
