#include "stdafx.h"

#include "libretro_mouse_handler.h"
#include "libretro_input.h"

#include "Emu/System.h"
#include "Emu/IdManager.h"

namespace
{
    // RetroArch's range is -0x7fff..0x7fff; RPCS3 takes 0..max
    constexpr s32 gun_range = 0x7fff * 2;

    enum : u8 { event_move = 1, event_button, event_scroll };

    // About four seconds of a gun moving and firing every frame
    constexpr usz ring_events = 512;
}

LibretroMouseHandler::LibretroMouseHandler()
{
    init_rings();
}

LibretroMouseHandler::LibretroMouseHandler(utils::serial* ar)
    : MouseHandlerBase(ar)
{
    init_rings();
}

LibretroMouseHandler::~LibretroMouseHandler()
{
    for (auto& ring : m_events)
        retro_spsc_free(&ring);
}

void LibretroMouseHandler::init_rings()
{
    for (u32 i = 0; i < max_guns; i++)
    {
        ensure(retro_spsc_init(&m_events[i], ring_events * sizeof(event)));
        retro_atomic_int_init(&m_latest_pos[i], 0);
        retro_atomic_int_init(&m_latest_buttons[i], 0);
        retro_atomic_int_init(&m_lost[i], 0);
    }
    retro_atomic_int_init(&m_ready, 0);
    retro_atomic_int_init(&m_inits, 0);
}

void LibretroMouseHandler::Init(const u32 max_connect)
{
    // The USIO board calls this on every poll; as upstream's handlers, only
    // the first call sets the mice up. A savestate brings max_connect back
    // before calling this, with no mice yet.
    if (m_info.max_connect > 0 && !m_mice.empty())
        return;

    m_mice.clear();
    m_mice.resize(max_connect);

    const u32 connected = std::min<u32>(max_connect, max_guns);

    m_info = {};
    m_info.max_connect = max_connect;
    m_info.now_connect = connected;
    for (u32 i = 0; i < max_connect; i++)
    {
        m_info.status[i] = i < connected ? CELL_MOUSE_STATUS_CONNECTED : CELL_MOUSE_STATUS_DISCONNECTED;
        m_info.mode[i] = CELL_MOUSE_INFO_TABLET_MOUSE_MODE;
        m_info.tablet_is_supported[i] = CELL_MOUSE_INFO_TABLET_NOT_SUPPORTED;
        m_info.vendor_id[i] = 0x1234;
        m_info.product_id[i] = 0x1234;
    }

    type = mouse_handler::raw;

    // For retro_run: how many mice to drive, and that it starts over
    retro_atomic_store_release_int(&m_ready, static_cast<int>(connected));
    retro_atomic_fetch_add_int(&m_inits, 1);
}

void LibretroMouseHandler::push(u32 index, const event& e)
{
    if (retro_spsc_write_avail(&m_events[index]) < sizeof(event) ||
        retro_spsc_write(&m_events[index], &e, sizeof(event)) != sizeof(event))
    {
        // Nothing has read the mice for a while: catch up later instead
        retro_atomic_store_release_int(&m_lost[index], 1);
    }
}

void LibretroMouseHandler::update()
{
    // The mice Init set up, without the lock
    const u32 count = static_cast<u32>(retro_atomic_load_acquire_int(&m_ready));

    // Set up again: the mice start from nothing, and so does what retro_run
    // last handed them
    if (const u32 inits = static_cast<u32>(retro_atomic_load_acquire_int(&m_inits)); inits != m_seen_inits)
    {
        m_seen_inits = inits;
        m_last = {};
    }

    for (u32 i = 0; i < count && i < max_guns; i++)
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

        // Where it is now, before the events: a mouse that lost some catches
        // up with this
        retro_atomic_store_release_int(&m_latest_pos[i], static_cast<int>(static_cast<u32>(x) | (static_cast<u32>(y) << 16)));
        retro_atomic_store_release_int(&m_latest_buttons[i], buttons);

        if (x != last.x || y != last.y)
        {
            push(i, event{event_move, 0, 0, 0, x, y});
            last.x = x;
            last.y = y;
        }

        for (const u8 button : {b1, b2, b3})
        {
            if ((buttons ^ last.buttons) & button)
                push(i, event{event_button, button, static_cast<u8>((buttons & button) != 0), 0, 0, 0});
        }
        last.buttons = buttons;

        if (gun.wheel)
            push(i, event{event_scroll, 0, 0, static_cast<s8>(gun.wheel), 0, 0});
    }
}

void LibretroMouseHandler::take_pending()
{
    for (u32 i = 0; i < max_guns; i++)
    {
        event e;
        while (retro_spsc_read_avail(&m_events[i]) >= sizeof(event) &&
            retro_spsc_read(&m_events[i], &e, sizeof(event)) == sizeof(event))
        {
            switch (e.kind)
            {
            case event_move: move_locked(i, e.x, e.y, gun_range, gun_range); break;
            case event_button: button_locked(i, e.button, e.pressed != 0); break;
            case event_scroll: scroll_locked(i, 0, e.wheel); break;
            default: break;
            }
        }

        // Events were lost: the mouse goes where the gun is, with what it holds
        if (retro_atomic_exchange_int(&m_lost[i], 0) && i < m_mice.size())
        {
            const u32 pos = static_cast<u32>(retro_atomic_load_acquire_int(&m_latest_pos[i]));
            const s32 x = static_cast<s32>(pos & 0xffff);
            const s32 y = static_cast<s32>(pos >> 16);
            const u8 buttons = static_cast<u8>(retro_atomic_load_acquire_int(&m_latest_buttons[i]));
            if (m_mice[i].x_pos_pixels != x || m_mice[i].y_pos_pixels != y)
                move_locked(i, x, y, gun_range, gun_range);
            for (u8 bit = 0; bit < 8; bit++)
            {
                const u8 button = static_cast<u8>(1u << bit);
                if ((m_mice[i].buttons ^ buttons) & button)
                    button_locked(i, button, (buttons & button) != 0);
            }
        }
    }
}

void libretro_mouse_update()
{
    if (Emu.IsStopped())
        return;

    if (auto* handler = g_fxo->try_get<MouseHandlerBase>())
    {
        if (auto* mouse = dynamic_cast<LibretroMouseHandler*>(handler))
            mouse->update();
    }
}
