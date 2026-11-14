#pragma once

#include "sys_sync.h"

#include "Emu/Memory/vm_ptr.h"

struct sys_lwmutex_attribute_t
{
	be_t<u32> protocol;
	be_t<u32> recursive;

	union
	{
		nse_t<u64, 1> name_u64;
		char name[sizeof(u64)];
	};
};

enum : u32
{
	lwmutex_free     = 0xffffffffu,
	lwmutex_dead     = 0xfffffffeu,
	lwmutex_reserved = 0xfffffffdu,
};

struct sys_lwmutex_t
{
	struct alignas(8) sync_var_t
	{
		be_t<u32> owner;
		be_t<u32> waiter;
	};

	union
	{
		atomic_t<sync_var_t> lock_var;

		struct
		{
			atomic_be_t<u32> owner;
			atomic_be_t<u32> waiter;
		}
		vars;

		atomic_be_t<u64> all_info;
	};

	be_t<u32> attribute;
	be_t<u32> recursive_count;
	be_t<u32> sleep_queue; // lwmutex pseudo-id
	be_t<u32> pad;
};

struct lv2_lwmutex final : lv2_obj
{
	static const u32 id_base = 0x95000000;

	const lv2_protocol protocol;
	const vm::ptr<sys_lwmutex_t> control;
	const be_t<u64> name;

	shared_mutex mutex;
	atomic_t<s32> lwcond_waiters{0};

	struct alignas(16) control_data_t
	{
		s32 signaled{0};
		u32 reserved{};
		ppu_thread* sq{};
	};

	// The signal and the queue, changed together: a 128-bit word. Its low
	// half's low 32 bits are the signal, its high half the queue's head.
	retro_atomic_128_t lv2_control{};

	static_assert(sizeof(control_data_t) == sizeof(retro_u128_t));
	static_assert(offsetof(control_data_t, signaled) == 0 && offsetof(control_data_t, sq) == 8);
	static_assert(std::endian::native == std::endian::little, "load_signaled() reads the low half's low 32 bits");

	static control_data_t control_of(retro_u128_t word)
	{
		return std::bit_cast<control_data_t>(word);
	}

	static retro_u128_t word_of(const control_data_t& data)
	{
		return std::bit_cast<retro_u128_t>(data);
	}

	s32 load_signaled() const
	{
		return static_cast<s32>(static_cast<u32>(retro_atomic_load_acquire_128_lo(&lv2_control)));
	}

	// Change the word with a compare-and-swap loop. f gets the word to
	// change; when it returns false nothing is stored. Gives the word as it
	// was and whether f's change was stored.
	template <typename F>
	std::pair<control_data_t, bool> change_control(F&& f)
	{
		for (;;)
		{
			const retro_u128_t seen = retro_atomic_load_acquire_128(&lv2_control);
			const control_data_t old = control_of(seen);
			control_data_t data = old;

			if constexpr (std::is_void_v<std::invoke_result_t<F&, control_data_t&>>)
			{
				f(data);
			}
			else if (!f(data))
			{
				return {old, false};
			}

			if (retro_atomic_cas_128(&lv2_control, seen, word_of(data)))
			{
				return {old, true};
			}
		}
	}

	lv2_lwmutex(u32 protocol, vm::ptr<sys_lwmutex_t> control, u64 name) noexcept
		: protocol{static_cast<u8>(protocol)}
		, control(control)
		, name(std::bit_cast<be_t<u64>>(name))
	{
	}

	lv2_lwmutex(utils::serial& ar);
	void save(utils::serial& ar);

	ppu_thread* load_sq() const
	{
		return reinterpret_cast<ppu_thread*>(static_cast<uptr>(retro_atomic_load_acquire_128_hi(&lv2_control)));
	}

	template <typename T>
	s32 try_own(T* cpu, bool wait_only = false)
	{
		const s32 signal = change_control([&](control_data_t& data)
		{
			if (!data.signaled)
			{
				cpu->prio.atomic_op([tag = retro_atomic_fetch_add_64(&g_priority_order_tag, 1) + 1](std::common_type_t<decltype(T::prio)>& prio)
				{
					prio.order = tag;
				});

				cpu->next_cpu = data.sq;
				data.sq = cpu;
			}
			else
			{
				ensure(!wait_only);
				data.signaled = 0;
			}
		}).first.signaled;

		if (signal)
		{
			cpu->next_cpu = nullptr;
		}
		else
		{
			const bool notify = lwcond_waiters.fetch_op([](s32& val)
			{
				if (val + 0u <= 1u << 31)
				{
					// Value was either positive or INT32_MIN
					return false;
				}

				// lwmutex was set to be destroyed, but there are lwcond waiters
				// Turn off the "lwcond_waiters notification" bit as we are adding an lwmutex waiter
				val &= 0x7fff'ffff;
				return true;
			}).second;

			if (notify)
			{
				// Notify lwmutex destroyer (may cause EBUSY to be returned for it)
				lwcond_waiters.notify_all();
			}
		}

		return signal;
	}

	bool try_unlock(bool unlock2)
	{
		if (!load_sq())
		{
			control_data_t old{};
			old.signaled = load_signaled();
			control_data_t store = old;
			store.signaled |= (unlock2 ? s32{smin} : 1);

			if (retro_atomic_cas_128(&lv2_control, word_of(old), word_of(store)))
			{
				return true;
			}
		}

		return false;
	}

	template <typename T>
	T* reown(bool unlock2 = false)
	{
		T* res = nullptr;

		change_control([&](control_data_t& data)
		{
			res = nullptr;

			if (auto sq = static_cast<T*>(data.sq))
			{
				res = schedule<T>(data.sq, protocol, false);

				if (sq == data.sq)
				{
					return false;
				}

				return true;
			}
			else
			{
				data.signaled |= (unlock2 ? s32{smin} : 1);
				return true;
			}
		});

		if (res && cpu_flag::again - res->state)
		{
			// Detach manually (fetch_op can fail, so avoid side-effects on the first node in this case)
			res->next_cpu = nullptr;
		}

		return res;
	}
};

// Aux
class ppu_thread;

// Syscalls

error_code _sys_lwmutex_create(ppu_thread& ppu, vm::ptr<u32> lwmutex_id, u32 protocol, vm::ptr<sys_lwmutex_t> control, s32 has_name, u64 name);
error_code _sys_lwmutex_destroy(ppu_thread& ppu, u32 lwmutex_id);
error_code _sys_lwmutex_lock(ppu_thread& ppu, u32 lwmutex_id, u64 timeout);
error_code _sys_lwmutex_trylock(ppu_thread& ppu, u32 lwmutex_id);
error_code _sys_lwmutex_unlock(ppu_thread& ppu, u32 lwmutex_id);
error_code _sys_lwmutex_unlock2(ppu_thread& ppu, u32 lwmutex_id);
