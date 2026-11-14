#pragma once

#include "sys_event.h"
#include "Emu/Memory/vm_ptr.h"


// Timer State
enum : u32
{
	SYS_TIMER_STATE_STOP = 0,
	SYS_TIMER_STATE_RUN  = 1,
};

struct sys_timer_information_t
{
	be_t<u64> next_expire; // system_time_t
	be_t<u64> period;
	be_t<u32> timer_state;
	be_t<u32> pad;
};

struct lv2_timer : lv2_obj
{
	static const u32 id_base = 0x11000000;

	shared_mutex mutex;
	retro_atomic_int_t state{SYS_TIMER_STATE_STOP}; // a u32

	shared_ptr<lv2_event_queue> port;
	u64 source;
	u64 data1;
	u64 data2;

	retro_atomic_64_t expire{0}; // Next expiration time (a u64)
	retro_atomic_64_t period{0}; // Period (oneshot if 0) (a u64)

	u64 check(u64 _now) noexcept;
	u64 check_unlocked(u64 _now) noexcept;

	lv2_timer() noexcept
		: lv2_obj(1)
	{
	}

	void get_information(sys_timer_information_t& info) const
	{
		if (const u32 _state = static_cast<u32>(retro_atomic_load_acquire_int(&state)); _state == SYS_TIMER_STATE_RUN)
		{
			info.timer_state = _state;
			info.next_expire = static_cast<u64>(retro_atomic_load_acquire_64(&expire));
			info.period      = static_cast<u64>(retro_atomic_load_acquire_64(&period));
		}
		else
		{
			info.timer_state = SYS_TIMER_STATE_STOP;
			info.next_expire = 0;
			info.period      = 0;
		}
	}

	lv2_timer(utils::serial& ar);
	void save(utils::serial& ar);
};

class ppu_thread;

// Syscalls

error_code sys_timer_create(ppu_thread&, vm::ptr<u32> timer_id);
error_code sys_timer_destroy(ppu_thread&, u32 timer_id);
error_code sys_timer_get_information(ppu_thread&, u32 timer_id, vm::ptr<sys_timer_information_t> info);
error_code _sys_timer_start(ppu_thread&, u32 timer_id, u64 basetime, u64 period); // basetime type changed from s64
error_code sys_timer_stop(ppu_thread&, u32 timer_id);
error_code sys_timer_connect_event_queue(ppu_thread&, u32 timer_id, u32 queue_id, u64 name, u64 data1, u64 data2);
error_code sys_timer_disconnect_event_queue(ppu_thread&, u32 timer_id);
error_code sys_timer_sleep(ppu_thread&, u32 sleep_time);
error_code sys_timer_usleep(ppu_thread&, u64 sleep_time);
