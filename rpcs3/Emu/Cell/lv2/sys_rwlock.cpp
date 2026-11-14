#include "stdafx.h"
#include "sys_rwlock.h"

#include "Emu/IdManager.h"

#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/PPUThread.h"

#include "util/asm.hpp"

LOG_CHANNEL(sys_rwlock);

lv2_rwlock::lv2_rwlock(utils::serial& ar)
	: protocol(ar)
	, key(ar)
	, name(ar)
{
	retro_atomic_store_release_64(&owner, ar.pop<s64>());
}

std::function<void(void*)> lv2_rwlock::load(utils::serial& ar)
{
	return load_func(make_shared<lv2_rwlock>(stx::exact_t<utils::serial&>(ar)));
}

void lv2_rwlock::save(utils::serial& ar)
{
	USING_SERIALIZATION_VERSION(lv2_sync);
	ar(protocol, key, name, static_cast<s64>(retro_atomic_load_acquire_64(&owner)));
}

error_code sys_rwlock_create(ppu_thread& ppu, vm::ptr<u32> rw_lock_id, vm::ptr<sys_rwlock_attribute_t> attr)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.warning("sys_rwlock_create(rw_lock_id=*0x%x, attr=*0x%x)", rw_lock_id, attr);

	if (!rw_lock_id || !attr)
	{
		return CELL_EFAULT;
	}

	const auto _attr = *attr;

	const u32 protocol = _attr.protocol;

	if (protocol != SYS_SYNC_FIFO && protocol != SYS_SYNC_PRIORITY)
	{
		sys_rwlock.error("sys_rwlock_create(): unknown protocol (0x%x)", protocol);
		return CELL_EINVAL;
	}

	const u64 ipc_key = lv2_obj::get_key(_attr);

	if (auto error = lv2_obj::create<lv2_rwlock>(_attr.pshared, ipc_key, _attr.flags, [&]
	{
		return make_shared<lv2_rwlock>(protocol, ipc_key, _attr.name_u64);
	}))
	{
		return error;
	}

	ppu.check_state();
	*rw_lock_id = idm::last_id<lv2_rwlock>();
	return CELL_OK;
}

error_code sys_rwlock_destroy(ppu_thread& ppu, u32 rw_lock_id)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.warning("sys_rwlock_destroy(rw_lock_id=0x%x)", rw_lock_id);

	const auto rwlock = idm::withdraw<lv2_obj, lv2_rwlock>(rw_lock_id, [](lv2_rwlock& rw) -> CellError
	{
		if (retro_atomic_load_acquire_64(&rw.owner))
		{
			return CELL_EBUSY;
		}

		lv2_obj::on_id_destroy(rw, rw.key);
		return {};
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (rwlock.ret)
	{
		return rwlock.ret;
	}

	return CELL_OK;
}

error_code sys_rwlock_rlock(ppu_thread& ppu, u32 rw_lock_id, u64 timeout)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_rlock(rw_lock_id=0x%x, timeout=0x%llx)", rw_lock_id, timeout);

	// Found with no lock over the IDs. Taking the lock, or waiting for it, is
	// what sys_rwlock_destroy asks about, so a destroy waits for this to be
	// done before it looks (idm::get_entered).
	const auto rwlock = idm::get_entered<lv2_obj, lv2_rwlock>(rw_lock_id, [&, notify = lv2_obj::notify_all_t()](lv2_rwlock& rwlock)
	{
		const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

		if (val <= 0 && !(val & 1))
		{
			if (retro_atomic_cas_64(&rwlock.owner, val, val - 2))
			{
				return true;
			}
		}

		lv2_obj::prepare_for_sleep(ppu);

		std::lock_guard lock(rwlock.mutex);

		const s64 _old = [&]
		{
			for (;;)
			{
				const s64 seen = retro_atomic_load_acquire_64(&rwlock.owner);
				s64 val = seen;
				if (val <= 0 && !(val & 1))
				{
					val -= 2;
				}
				else
				{
					val |= 1;
				}
		
				if (retro_atomic_cas_64(&rwlock.owner, seen, val))
				{
					return seen;
				}
			}
		}();

		if (_old > 0 || _old & 1)
		{
			rwlock.sleep(ppu, timeout);
			lv2_obj::emplace(rwlock.rq, &ppu);
			return false;
		}

		return true;
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (rwlock.ret)
	{
		return CELL_OK;
	}

	ppu.gpr[3] = CELL_OK;

	while (auto state = +ppu.state)
	{
		if (state & cpu_flag::signal && ppu.state.test_and_reset(cpu_flag::signal))
		{
			break;
		}

		if (is_stopped(state))
		{
			std::lock_guard lock(rwlock->mutex);

			for (auto cpu = +rwlock->rq; cpu; cpu = cpu->next_cpu)
			{
				if (cpu == &ppu)
				{
					ppu.state += cpu_flag::again;
					return {};
				}
			}

			break;
		}

		for (usz i = 0; cpu_flag::signal - ppu.state && i < 50; i++)
		{
			busy_wait(500);
		}

		if (ppu.state & cpu_flag::signal)
 		{
			continue;
		}

		if (timeout)
		{
			if (lv2_obj::wait_timeout(timeout, &ppu))
			{
				// Wait for rescheduling
				if (ppu.check_state())
				{
					continue;
				}

				ppu.state += cpu_flag::wait;

				if (!atomic_storage<ppu_thread*>::load(rwlock->rq))
				{
					// Waiters queue is empty, so the thread must have been signaled
					rwlock->mutex.lock_unlock();
					break;
				}

				std::lock_guard lock(rwlock->mutex);

				if (!rwlock->unqueue(rwlock->rq, &ppu))
				{
					break;
				}

				ppu.gpr[3] = CELL_ETIMEDOUT;
				break;
			}
		}
		else
		{
			ppu.state.wait(state);
		}
	}

	return not_an_error(ppu.gpr[3]);
}

error_code sys_rwlock_tryrlock(ppu_thread& ppu, u32 rw_lock_id)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_tryrlock(rw_lock_id=0x%x)", rw_lock_id);

	// As sys_rwlock_rlock: it takes the lock
	const auto rwlock = idm::get_entered<lv2_obj, lv2_rwlock>(rw_lock_id, [](lv2_rwlock& rwlock)
	{
		bool ok = false;

		for (;;)
		{
			const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

			if (!(val <= 0 && !(val & 1)))
			{
				break;
			}

			if (retro_atomic_cas_64(&rwlock.owner, val, val - 2))
			{
				ok = true;
				break;
			}
		}

		return ok;
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (!rwlock.ret)
	{
		return not_an_error(CELL_EBUSY);
	}

	return CELL_OK;
}

error_code sys_rwlock_runlock(ppu_thread& ppu, u32 rw_lock_id)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_runlock(rw_lock_id=0x%x)", rw_lock_id);

	// Found with no lock over the IDs: giving the lock up takes nothing a
	// destroy asks about
	const auto rwlock = idm::get_held<lv2_obj, lv2_rwlock>(rw_lock_id, [](lv2_rwlock& rwlock)
	{
		const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

		if (val < 0 && !(val & 1))
		{
			if (retro_atomic_cas_64(&rwlock.owner, val, val + 2))
			{
				return true;
			}
		}

		return false;
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	lv2_obj::notify_all_t notify;

	if (rwlock.ret)
	{
		return CELL_OK;
	}
	else
	{
		std::lock_guard lock(rwlock->mutex);

		// Remove one reader
		const s64 _old = [&]
		{
			for (;;)
			{
				const s64 seen = retro_atomic_load_acquire_64(&rwlock->owner);
				s64 val = seen;
				if (val < -1)
				{
					val += 2;
				}
		
				if (retro_atomic_cas_64(&rwlock->owner, seen, val))
				{
					return seen;
				}
			}
		}();

		if (_old >= 0)
		{
			return CELL_EPERM;
		}

		if (_old == -1)
		{
			if (const auto cpu = rwlock->schedule<ppu_thread>(rwlock->wq, rwlock->protocol))
			{
				if (static_cast<ppu_thread*>(cpu)->state & cpu_flag::again)
				{
					ppu.state += cpu_flag::again;
					return {};
				}

				retro_atomic_store_release_64(&rwlock->owner, static_cast<s64>(cpu->id << 1 | !!rwlock->wq | !!rwlock->rq));

				rwlock->awake(cpu);
			}
			else
			{
				retro_atomic_store_release_64(&rwlock->owner, 0);

				ensure(!rwlock->rq);
			}
		}
	}

	return CELL_OK;
}

error_code sys_rwlock_wlock(ppu_thread& ppu, u32 rw_lock_id, u64 timeout)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_wlock(rw_lock_id=0x%x, timeout=0x%llx)", rw_lock_id, timeout);

	// As sys_rwlock_rlock
	const auto rwlock = idm::get_entered<lv2_obj, lv2_rwlock>(rw_lock_id, [&, notify = lv2_obj::notify_all_t()](lv2_rwlock& rwlock) -> s64
	{
		const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

		if (val == 0)
		{
			if (retro_atomic_cas_64(&rwlock.owner, 0, static_cast<s64>(ppu.id << 1)))
			{
				return 0;
			}
		}
		else if (val >> 1 == ppu.id)
		{
			return val;
		}

		lv2_obj::prepare_for_sleep(ppu);

		std::lock_guard lock(rwlock.mutex);

		const s64 _old = [&]
		{
			for (;;)
			{
				const s64 seen = retro_atomic_load_acquire_64(&rwlock.owner);
				s64 val = seen;
				if (val == 0)
				{
					val = ppu.id << 1;
				}
				else
				{
					val |= 1;
				}
		
				if (retro_atomic_cas_64(&rwlock.owner, seen, val))
				{
					return seen;
				}
			}
		}();

		if (_old != 0)
		{
			rwlock.sleep(ppu, timeout);
			lv2_obj::emplace(rwlock.wq, &ppu);
		}

		return _old;
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (rwlock.ret == 0)
	{
		return CELL_OK;
	}

	if (rwlock.ret >> 1 == ppu.id)
	{
		return CELL_EDEADLK;
	}

	ppu.gpr[3] = CELL_OK;

	while (auto state = +ppu.state)
	{
		if (state & cpu_flag::signal && ppu.state.test_and_reset(cpu_flag::signal))
		{
			break;
		}

		if (is_stopped(state))
		{
			std::lock_guard lock(rwlock->mutex);

			for (auto cpu = +rwlock->wq; cpu; cpu = cpu->next_cpu)
			{
				if (cpu == &ppu)
				{
					ppu.state += cpu_flag::again;
					return {};
				}
			}

			break;
		}

		for (usz i = 0; cpu_flag::signal - ppu.state && i < 50; i++)
		{
			busy_wait(500);
		}

		if (ppu.state & cpu_flag::signal)
 		{
			continue;
		}

		if (timeout)
		{
			if (lv2_obj::wait_timeout(timeout, &ppu))
			{
				// Wait for rescheduling
				if (ppu.check_state())
				{
					continue;
				}

				ppu.state += cpu_flag::wait;

				std::lock_guard lock(rwlock->mutex);

				if (!rwlock->unqueue(rwlock->wq, &ppu))
				{
					break;
				}

				// If the last waiter quit the writer sleep queue, wake blocked readers
				if (rwlock->rq && !rwlock->wq && retro_atomic_load_acquire_64(&rwlock->owner) < 0)
				{
					s64 size = 0;

					// Protocol doesn't matter here since they are all enqueued anyways
					while (auto cpu = rwlock->schedule<ppu_thread>(rwlock->rq, SYS_SYNC_FIFO))
					{
						size++;
						rwlock->append(cpu);
					}

					[&]
					{
						for (;;)
						{
							const s64 seen = retro_atomic_load_acquire_64(&rwlock->owner);
							s64 val = seen;
							val -= 2 * size; // Add readers to value
							val &= -2; // Clear wait bit
					
							if (retro_atomic_cas_64(&rwlock->owner, seen, val))
							{
								return seen;
							}
						}
					}();

					lv2_obj::awake_all();
				}
				else if (!rwlock->rq && !rwlock->wq)
				{
					retro_atomic_fetch_and_64(&rwlock->owner, -2);
				}

				ppu.gpr[3] = CELL_ETIMEDOUT;
				break;
			}
		}
		else
		{
			ppu.state.wait(state);
		}
	}

	return not_an_error(ppu.gpr[3]);
}

error_code sys_rwlock_trywlock(ppu_thread& ppu, u32 rw_lock_id)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_trywlock(rw_lock_id=0x%x)", rw_lock_id);

	// As sys_rwlock_rlock: it takes the lock
	const auto rwlock = idm::get_entered<lv2_obj, lv2_rwlock>(rw_lock_id, [&](lv2_rwlock& rwlock)
	{
		const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

		// Return previous value
		// The old value: 0 if it was taken
		for (s64 seen = val;; seen = retro_atomic_load_acquire_64(&rwlock.owner))
		{
			if (seen != 0 || retro_atomic_cas_64(&rwlock.owner, 0, static_cast<s64>(ppu.id << 1)))
			{
				return seen;
			}
		}
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (rwlock.ret != 0)
	{
		if (rwlock.ret >> 1 == ppu.id)
		{
			return CELL_EDEADLK;
		}

		return not_an_error(CELL_EBUSY);
	}

	return CELL_OK;
}

error_code sys_rwlock_wunlock(ppu_thread& ppu, u32 rw_lock_id)
{
	ppu.state += cpu_flag::wait;

	sys_rwlock.trace("sys_rwlock_wunlock(rw_lock_id=0x%x)", rw_lock_id);

	// As sys_rwlock_runlock
	const auto rwlock = idm::get_held<lv2_obj, lv2_rwlock>(rw_lock_id, [&](lv2_rwlock& rwlock)
	{
		const s64 val = retro_atomic_load_acquire_64(&rwlock.owner);

		// Return previous value
		// The old value: ours if it was released
		for (s64 seen = val;; seen = retro_atomic_load_acquire_64(&rwlock.owner))
		{
			if (seen != static_cast<s64>(ppu.id << 1) || retro_atomic_cas_64(&rwlock.owner, seen, 0))
			{
				return seen;
			}
		}
	});

	if (!rwlock)
	{
		return CELL_ESRCH;
	}

	if (rwlock.ret >> 1 != ppu.id)
	{
		return CELL_EPERM;
	}

	if (lv2_obj::notify_all_t notify; rwlock.ret & 1)
	{
		std::lock_guard lock(rwlock->mutex);

		if (auto cpu = rwlock->schedule<ppu_thread>(rwlock->wq, rwlock->protocol))
		{
			if (static_cast<ppu_thread*>(cpu)->state & cpu_flag::again)
			{
				ppu.state += cpu_flag::again;
				return {};
			}

			retro_atomic_store_release_64(&rwlock->owner, static_cast<s64>(cpu->id << 1 | !!rwlock->wq | !!rwlock->rq));

			rwlock->awake(cpu);
		}
		else if (rwlock->rq)
		{
			for (auto cpu = +rwlock->rq; cpu; cpu = cpu->next_cpu)
			{
				if (cpu->state & cpu_flag::again)
				{
					ppu.state += cpu_flag::again;
					return {};
				}
			}

			s64 size = 0;

			// Protocol doesn't matter here since they are all enqueued anyways
			while (auto cpu = rwlock->schedule<ppu_thread>(rwlock->rq, SYS_SYNC_FIFO))
			{
				size++;
				rwlock->append(cpu);
			}

			retro_atomic_store_release_64(&rwlock->owner, -2 * static_cast<s64>(size));
			lv2_obj::awake_all();
		}
		else
		{
			retro_atomic_store_release_64(&rwlock->owner, 0);
		}
	}

	return CELL_OK;
}
