#pragma once

#include "sys_sync.h"

#include "Emu/Memory/vm_ptr.h"

#include "Emu/Cell/PPUThread.h"

struct sys_mutex_attribute_t
{
	be_t<u32> protocol; // SYS_SYNC_FIFO, SYS_SYNC_PRIORITY or SYS_SYNC_PRIORITY_INHERIT
	be_t<u32> recursive; // SYS_SYNC_RECURSIVE or SYS_SYNC_NOT_RECURSIVE
	be_t<u32> pshared;
	be_t<u32> adaptive;
	be_t<u64> ipc_key;
	be_t<s32> flags;
	be_t<u32> pad;

	union
	{
		nse_t<u64, 1> name_u64;
		char name[sizeof(u64)];
	};
};

class ppu_thread;

struct lv2_mutex final : lv2_obj
{
	static const u32 id_base = 0x85000000;

	const lv2_protocol protocol;
	const u32 recursive;
	const u32 adaptive;
	const u64 key;
	const u64 name;

	u32 cond_count = 0; // Condition Variables
	shared_mutex mutex;
	retro_atomic_int_t lock_count{}; // Recursive Locks (a u32)

	struct alignas(16) control_data_t
	{
		u32 owner{};
		u32 reserved{};
		ppu_thread* sq{};
	};

	// The owner and the queue, changed together: a 128-bit word. Its low
	// half's low 32 bits are the owner, its high half the queue's head.
	retro_atomic_128_t control{};

	static_assert(sizeof(control_data_t) == sizeof(retro_u128_t));
	static_assert(offsetof(control_data_t, owner) == 0 && offsetof(control_data_t, sq) == 8);
	static_assert(std::endian::native == std::endian::little, "control_owner() reads the low half's low 32 bits");

	static control_data_t control_of(retro_u128_t word)
	{
		return std::bit_cast<control_data_t>(word);
	}

	static retro_u128_t word_of(const control_data_t& data)
	{
		return std::bit_cast<retro_u128_t>(data);
	}

	// One field alone: a 64-bit load of its half
	u32 control_owner()
	{
		return static_cast<u32>(retro_atomic_load_acquire_128_lo(&control));
	}

	ppu_thread* control_sq()
	{
		return reinterpret_cast<ppu_thread*>(static_cast<uptr>(retro_atomic_load_acquire_128_hi(&control)));
	}

	lv2_mutex(u32 protocol, u32 recursive,u32 adaptive, u64 key, u64 name) noexcept
		: protocol{static_cast<u8>(protocol)}
		, recursive(recursive)
		, adaptive(adaptive)
		, key(key)
		, name(name)
	{
	}

	lv2_mutex(utils::serial& ar);
	static std::function<void(void*)> load(utils::serial& ar);
	void save(utils::serial& ar);

	template <typename T>
	CellError try_lock(T& cpu)
	{
		const retro_u128_t seen = retro_atomic_load_acquire_128(&control);
		const control_data_t it = control_of(seen);

		if (!it.owner)
		{
			auto store = it;
			store.owner = cpu.id;
			if (!retro_atomic_cas_128(&control, seen, word_of(store)))
			{
				return CELL_EBUSY;
			}

			return {};
		}

		if (it.owner == cpu.id)
		{
			// Recursive locking
			if (recursive == SYS_SYNC_RECURSIVE)
			{
				if (static_cast<u32>(retro_atomic_load_acquire_int(&lock_count)) == 0xffffffffu)
				{
					return CELL_EKRESOURCE;
				}

				retro_atomic_fetch_add_int(&lock_count, 1);
				return {};
			}

			return CELL_EDEADLK;
		}

		return CELL_EBUSY;
	}

	template <typename T>
	bool try_own(T& cpu)
	{
		const auto own = [&](control_data_t& data)
		{
			if (data.owner)
			{
				cpu.prio.atomic_op([tag = retro_atomic_fetch_add_64(&g_priority_order_tag, 1) + 1](std::common_type_t<decltype(T::prio)>& prio)
				{
					prio.order = tag;
				});

				cpu.next_cpu = data.sq;
				data.sq = &cpu;
				return false;
			}
			else
			{
				data.owner = cpu.id;
				return true;
			}
		};

		for (;;)
		{
			const retro_u128_t seen = retro_atomic_load_acquire_128(&control);
			control_data_t data = control_of(seen);
			const bool owned = own(data);

			if (retro_atomic_cas_128(&control, seen, word_of(data)))
			{
				if (owned)
				{
					cpu.next_cpu = nullptr;
				}

				return owned;
			}
		}
	}

	template <typename T>
	CellError try_unlock(T& cpu)
	{
		const retro_u128_t seen = retro_atomic_load_acquire_128(&control);
		const control_data_t it = control_of(seen);

		if (it.owner != cpu.id)
		{
			return CELL_EPERM;
		}

		if (retro_atomic_load_acquire_int(&lock_count))
		{
			retro_atomic_fetch_add_int(&lock_count, -1);
			return {};
		}

		if (!it.sq)
		{
			auto store = it;
			store.owner = 0;

			if (retro_atomic_cas_128(&control, seen, word_of(store)))
			{
				return {};
			}
		}

		return CELL_EBUSY;
	}

	template <typename T>
	T* reown()
	{
		T* res{};

		const auto change = [&](control_data_t& data)
		{
			res = nullptr;

			if (auto sq = static_cast<T*>(data.sq))
			{
				res = schedule<T>(data.sq, protocol, false);

				if (sq == data.sq)
				{
					if (cpu_flag::again - res->state)
					{
						// The owner alone, whatever the queue is now
						for (retro_u128_t seen = retro_atomic_load_acquire_128(&control);;)
						{
							control_data_t owned = control_of(seen);
							owned.owner = res->id;

							if (retro_atomic_cas_seen_128(&control, &seen, word_of(owned)))
							{
								break;
							}
						}
					}

					return false;
				}

				data.owner = res->id;
				return true;
			}
			else
			{
				data.owner = 0;
				return true;
			}
		};

		for (;;)
		{
			const retro_u128_t seen = retro_atomic_load_acquire_128(&control);
			control_data_t data = control_of(seen);

			if (!change(data) || retro_atomic_cas_128(&control, seen, word_of(data)))
			{
				break;
			}
		}

		if (res && cpu_flag::again - res->state)
		{
			// Detach manually (fetch_op can fail, so avoid side-effects  on the first node in this case)
			res->next_cpu = nullptr;
		}

		return res;
	}
};


// Syscalls

error_code sys_mutex_create(ppu_thread& ppu, vm::ptr<u32> mutex_id, vm::ptr<sys_mutex_attribute_t> attr);
error_code sys_mutex_destroy(ppu_thread& ppu, u32 mutex_id);
error_code sys_mutex_lock(ppu_thread& ppu, u32 mutex_id, u64 timeout);
error_code sys_mutex_trylock(ppu_thread& ppu, u32 mutex_id);
error_code sys_mutex_unlock(ppu_thread& ppu, u32 mutex_id);
