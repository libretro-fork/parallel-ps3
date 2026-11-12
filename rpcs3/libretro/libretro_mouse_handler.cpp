#include "stdafx.h"

#include "libretro_mouse_handler.h"
#include "libretro_input.h"

#include "Emu/System.h"
#include "Emu/IdManager.h"

namespace
{
    // RetroArch's range is -0x7fff..0x7fff; RPCS3 takes 0..max
    constexpr s32 gun_range = 0x7fff * 2;

    enum : u32 { event_move = 1, event_button, event_scroll };
}

// The handler the emulator has, when it is this one: its address, to tell
// it from another kind without asking for its type every run
static retro_atomic_ptr_t s_handler;

LibretroMouseHandler::LibretroMouseHandler()
{
    init_log();
}

LibretroMouseHandler::LibretroMouseHandler(utils::serial* ar)
    : MouseHandlerBase(ar)
{
    init_log();
}

LibretroMouseHandler::~LibretroMouseHandler()
{
    retro_atomic_cas_ptr(&s_handler, static_cast<MouseHandlerBase*>(this), nullptr);
}

void LibretroMouseHandler::init_log()
{
    for (u32 i = 0; i < max_guns; i++)
    {
        for (u32 k = 0; k < log_events; k++)
        {
            retro_atomic_int_init(&m_log_a[i][k], 0);
            retro_atomic_int_init(&m_log_b[i][k], 0);
        }
        retro_atomic_int_init(&m_latest_pos[i], 0);
        retro_atomic_int_init(&m_latest_buttons[i], 0);
    }
    retro_atomic_int_init(&m_setup, 0);
    retro_atomic_int_init(&m_inits, 0);
    retro_atomic_store_release_ptr(&s_handler, static_cast<MouseHandlerBase*>(this));
}

void LibretroMouseHandler::Init(const u32 max_connect)
{
    // The USIO board calls this on every poll, and cellMouseInit may come
    // from another thread meanwhile; as upstream's handlers, only the first
    // call sets the mice up.
    if (retro_atomic_exchange_int(&m_setup, 1))
        return;

    type = mouse_handler::raw;

    // Each mouse starts from nothing, with what the log has so far behind it
    u32 applied[max_ports]{};
    for (u32 i = 0; i < max_guns; i++)
        applied[i] = static_cast<u32>(retro_atomic_load_acquire_int(&m_head[i]));

    set_ports(max_connect, max_guns, false, applied);

    // For retro_run: the mice start over, so it tells them again where the
    // guns are
    retro_atomic_fetch_add_int(&m_inits, 1);
}

void LibretroMouseHandler::push(u32 index, u32 a, u32 b)
{
    // retro_run is the only writer: the entry first, then the head
    const u32 head = static_cast<u32>(retro_atomic_load_acquire_int(&m_head[index]));
    retro_atomic_store_release_int(&m_log_a[index][head % log_events], static_cast<int>(a));
    retro_atomic_store_release_int(&m_log_b[index][head % log_events], static_cast<int>(b));
    retro_atomic_store_release_int(&m_head[index], static_cast<int>(head + 1));
}

void LibretroMouseHandler::update()
{
    const u32 mice = count();

    // The frontend is asked about the guns only while there are mice for
    // them. The run they appear in has no gun read yet: the mice start with
    // the next, from where the guns are then.
    libretro_input_set_guns_read(mice != 0);

    if (!mice || !libretro_input_guns_polled())
    {
        // And when they are asked about again, the mice are told anew where
        // they are
        m_asked = false;
        return;
    }

    if (!m_asked)
    {
        m_asked = true;
        for (last_state& last : m_last)
            last = {};
    }

    // Set up again: the mice start from nothing, and so does what retro_run
    // last handed them
    if (const u32 inits = static_cast<u32>(retro_atomic_load_acquire_int(&m_inits)); inits != m_seen_inits)
    {
        m_seen_inits = inits;
        for (last_state& last : m_last)
            last = {};
    }

    for (u32 i = 0; i < mice && i < max_guns; i++)
    {
        const LibretroGunState& gun = *libretro_input_get_gun(libretro_input_port_for_player(i));
        last_state& last = m_last[i];

        // Off the screen the mouse stays where it last was, so a shot fired
        // there (to reload) lands at the edge it left by
        const s32 x = gun.x + 0x7fff;
        const s32 y = gun.y + 0x7fff;

        constexpr u8 b1 = static_cast<u8>(CELL_MOUSE_BUTTON_1);
        constexpr u8 b2 = static_cast<u8>(CELL_MOUSE_BUTTON_2);
        constexpr u8 b3 = static_cast<u8>(CELL_MOUSE_BUTTON_3);
        const u8 buttons = static_cast<u8>((gun.trigger ? b1 : 0) | (gun.aux_a ? b2 : 0) | (gun.aux_b ? b3 : 0));

        // Where it is now, before the events: a mouse the log has gone past
        // catches up with this
        const u32 pos = static_cast<u32>(x) | (static_cast<u32>(y) << 16);
        retro_atomic_store_release_int(&m_latest_pos[i], static_cast<int>(pos));
        retro_atomic_store_release_int(&m_latest_buttons[i], buttons);

        if (x != last.x || y != last.y)
        {
            push(i, event_move, pos);
            last.x = x;
            last.y = y;
        }

        for (const u8 button : {b1, b2, b3})
        {
            if ((buttons ^ last.buttons) & button)
                push(i, event_button | (static_cast<u32>(button) << 8) | ((buttons & button) ? 1u << 16 : 0), 0);
        }
        last.buttons = buttons;

        if (gun.wheel)
            push(i, event_scroll | (static_cast<u32>(static_cast<u8>(gun.wheel)) << 24), 0);
    }
}

void LibretroMouseHandler::apply_pending(u32 port, mouse_port& copy)
{
    if (port >= max_guns)
        return;

    u32 k = copy.applied;
    u32 head = static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port]));
    bool lost = head - k > log_events;

    while (!lost && k != head)
    {
        const u32 a = static_cast<u32>(retro_atomic_load_acquire_int(&m_log_a[port][k % log_events]));
        const u32 b = static_cast<u32>(retro_atomic_load_acquire_int(&m_log_b[port][k % log_events]));

        // Entry k is written over once the head is log_events past it: what
        // was read is entry k only if it is not there yet
        if (static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port])) - k >= log_events)
        {
            lost = true;
            break;
        }

        const s32 x = static_cast<s32>(b & 0xffff);
        const s32 y = static_cast<s32>(b >> 16);
        const u8 which = static_cast<u8>(a >> 8);
        const bool pressed = (a >> 16) & 1;

        switch (a & 0xff)
        {
        case event_move:
            if (copy.mouse.x_pos_pixels != x || copy.mouse.y_pos_pixels != y)
                move(copy, x, y, gun_range, gun_range);
            break;
        case event_button:
            if (!(copy.mouse.buttons & which) != !pressed)
                button(copy, which, pressed);
            break;
        case event_scroll:
            scroll(copy, 0, static_cast<s8>(a >> 24));
            break;
        default:
            break;
        }

        k++;
    }

    if (lost)
    {
        // Nothing read this mouse for a while: it goes where the gun is,
        // with what it holds. The head first, so that nothing up to it is
        // missing from what is read after.
        head = static_cast<u32>(retro_atomic_load_acquire_int(&m_head[port]));
        const u32 pos = static_cast<u32>(retro_atomic_load_acquire_int(&m_latest_pos[port]));
        const u8 buttons = static_cast<u8>(retro_atomic_load_acquire_int(&m_latest_buttons[port]));
        const s32 x = static_cast<s32>(pos & 0xffff);
        const s32 y = static_cast<s32>(pos >> 16);

        if (copy.mouse.x_pos_pixels != x || copy.mouse.y_pos_pixels != y)
            move(copy, x, y, gun_range, gun_range);

        for (u8 bit = 0; bit < 8; bit++)
        {
            const u8 button = static_cast<u8>(1u << bit);
            if ((copy.mouse.buttons ^ buttons) & button)
                MouseHandlerBase::button(copy, button, (buttons & button) != 0);
        }

        k = head;
    }

    copy.applied = k;
}

void libretro_mouse_update()
{
    MouseHandlerBase* handler = Emu.IsStopped() ? nullptr : g_fxo->try_get<MouseHandlerBase>();

    if (handler && handler == retro_atomic_load_acquire_ptr(&s_handler))
    {
        static_cast<LibretroMouseHandler*>(handler)->update();
        return;
    }

    // No mice: nothing reads the guns
    libretro_input_set_guns_read(false);
}
