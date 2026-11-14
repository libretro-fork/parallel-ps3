#include "stdafx.h"

#include "Emu/IdManager.h"

#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Memory/vm_reservation.h"

#include "util/asm.hpp"

#include "sys_mutex.h"

LOG_CHANNEL(sys_mutex);

lv2_mutex::lv2_mutex(utils::serial& ar)
	: protocol(ar)
	, recursive(ar)
	, adaptive(ar)
	, key(ar)
	, name(ar)
{
	retro_atomic_store_release_int(&lock_count, static_cast<int>(ar.pop<u32>()));
	control_data_t data{};
	ar(data.owner);

	// For backwards compatibility
	data.owner >>= 1;
	const retro_u128_t word = word_of(data);
	retro_atomic_128_init(&control, word);
}

std::function<void(void*)> lv2_mutex::load(utils::serial& ar)
{
	return load_func(make_shared<lv2_mutex>(stx::exact_t<utils::serial&>(ar)));
}

void lv2_mutex::save(utils::serial& ar)
{
	ar(protocol, recursive, adaptive, key, name, static_cast<u32>(retro_atomic_load_acquire_int(&lock_count)), control_owner() << 1);
}

error_code sys_mutex_create(ppu_thread& ppu, vm::ptr<u32> mutex_id, vm::ptr<sys_mutex_attribute_t> attr)
{
	ppu.state += cpu_flag::wait;

	sys_mutex.trace("sys_mutex_create(mutex_id=*0x%x, attr=*0x%x)", mutex_id, attr);

	if (!mutex_id || !attr)
	{
		return CELL_EFAULT;
	}

	const auto _attr = *attr;

	const u64 ipc_key = lv2_obj::get_key(_attr);

	if (ipc_key)
	{
		sys_mutex.warning("sys_mutex_create(mutex_id=*0x%x, attr=*0x%x): IPC=0x%016x", mutex_id, attr, ipc_key);
	}

	switch (_attr.protocol)
	{
	case SYS_SYNC_FIFO: break;
	case SYS_SYNC_PRIORITY: break;
	case SYS_SYNC_PRIORITY_INHERIT:
		sys_mutex.warning("sys_mutex_create(): SYS_SYNC_PRIORITY_INHERIT");
		break;
	default:
	{
		sys_mutex.error("sys_mutex_create(): unknown protocol (0x%x)", _attr.protocol);
		return CELL_EINVAL;
	}
	}

	switch (_attr.recursive)
	{
	case SYS_SYNC_RECURSIVE: break;
	case SYS_SYNC_NOT_RECURSIVE: break;
	default:
	{
		sys_mutex.error("sys_mutex_create(): unknown recursive (0x%x)", _attr.recursive);
		return CELL_EINVAL;
	}
	}

	if (_attr.adaptive != SYS_SYNC_NOT_ADAPTIVE)
	{
		sys_mutex.todo("sys_mutex_create(): unexpected adaptive (0x%x)", _attr.adaptive);
	}

	if (auto error = lv2_obj::create<lv2_mutex>(_attr.pshared, ipc_key, _attr.flags, [&]()
	{
		return make_shared<lv2_mutex>(
			_attr.protocol,
			_attr.recursive,
			_attr.adaptive,
			ipc_key,
			_attr.name_u64);
	}))
	{
		return error;
	}

	ppu.check_state();
	*mutex_id = idm::last_id<lv2_mutex>();
	return CELL_OK;
}

error_code sys_mutex_destroy(ppu_thread& ppu, u32 mutex_id)
{
	ppu.state += cpu_flag::wait;

	sys_mutex.trace("sys_mutex_destroy(mutex_id=0x%x)", mutex_id);

	const auto mutex = idm::withdraw<lv2_obj, lv2_mutex>(mutex_id, [](lv2_mutex& mutex) -> CellError
	{
		std::lock_guard lock(mutex.mutex);

		if (mutex.control_owner())
		{
			return CELL_EBUSY;
		}

		if (mutex.cond_count)
		{
			return CELL_EPERM;
		}

		lv2_obj::on_id_destroy(mutex, mutex.key);
		return {};
	});

	if (!mutex)
	{
		return CELL_ESRCH;
	}

	if (mutex->key)
	{
		sys_mutex.warning("sys_mutex_destroy(mutex_id=0x%x): IPC=0x%016x", mutex_id, mutex->key);
	}

	if (mutex.ret)
	{
		return mutex.ret;
	}

	return CELL_OK;
}

// Unlocks a mutex the thread owns: the whole of sys_mutex_unlock but for
// finding the mutex, and what a lock that turned out to have been taken
// through an ID withdrawn meanwhile is undone with
static CellError mutex_unlock(ppu_thread& ppu, lv2_mutex& mutex)
{
	auto result = mutex.try_unlock(ppu);

	if (result == CELL_EBUSY)
	{
		std::lock_guard lock(mutex.mutex);

		if (auto cpu = mutex.reown<ppu_thread>())
		{
			if (cpu->state & cpu_flag::again)
			{
				ppu.state += cpu_flag::again;
				return {};
			}

			mutex.awake(cpu);
		}

		result = {};
	}

	return result;
}

// The mutex ID is looked up with no lock (idm::pin, or a counted reference
// where the thread goes on to sleep). What the lock over all IDs used to
// guarantee - that a mutex cannot be destroyed between being found and
// being locked - is kept this way: sys_mutex_destroy marks the ID while it
// checks that the mutex has no owner; a thread that has just become the
// owner asks whether the ID still names the mutex (idm::still_named, which
// waits for a destroy that is deciding), and if it does not, gives the
// mutex up again and reports that there is no such mutex. Either the
// destroy saw the owner and failed, or the lock is undone.
static bool mutex_lock_stands(ppu_thread& ppu, u32 mutex_id, lv2_mutex& mutex)
{
	if (idm::still_named<lv2_obj, lv2_mutex>(mutex_id, &mutex)) [[likely]]
	{
		return true;
	}

	mutex_unlock(ppu, mutex);
	return false;
}

error_code sys_mutex_lock(ppu_thread& ppu, u32 mutex_id, u64 timeout)
{
	ppu.state += cpu_flag::wait;

	sys_mutex.trace("sys_mutex_lock(mutex_id=0x%x, timeout=0x%llx)", mutex_id, timeout);

	// Free, as it mostly is: found and taken with no lock and no reference counted
	if (const auto pinned = idm::pin<lv2_obj, lv2_mutex>(mutex_id))
	{
		if (const CellError result = pinned->try_lock(ppu); !result)
		{
			if (!mutex_lock_stands(ppu, mutex_id, *pinned))
			{
				return CELL_ESRCH;
			}

			return CELL_OK;
		}
		else if (result != CELL_EBUSY)
		{
			return result;
		}
	}
	else
	{
		return CELL_ESRCH;
	}

	// Taken: the thread may sleep on it, so it keeps a reference
	struct
	{
		stx::shared_ptr<lv2_mutex> ptr;
		CellError ret{};

		explicit operator bool() const { return !!ptr; }
		lv2_mutex* operator->() const { return ptr.get(); }
	}
	mutex{idm::hold<lv2_obj, lv2_mutex>(mutex_id)};

	if (mutex)
	{
		mutex.ret = [&, notify = lv2_obj::notify_all_t()](lv2_mutex& mutex)
	{
		CellError result = mutex.try_lock(ppu);

		if (result == CELL_EBUSY && !mutex.control_sq())
		{
			// Try busy waiting a bit if advantageous
			for (u32 i = 0, end = lv2_obj::has_ppus_in_running_state() ? 3 : 10; i < end; i++)
			{
				busy_wait(300);
				result = mutex.try_lock(ppu);

				if (!result || mutex.control_sq())
				{
					break;
				}
			}
		}

		if (result == CELL_EBUSY)
		{
			lv2_obj::prepare_for_sleep(ppu);

			ppu.cancel_sleep = 1;

			if (mutex.try_own(ppu) || !mutex.sleep(ppu, timeout))
			{
				result = {};
			}

			if (ppu.cancel_sleep != 1)
			{
				notify.cleanup();
			}

			ppu.cancel_sleep = 0;
		}

		return result;
	}(*mutex.ptr);
	}

	if (!mutex)
	{
		return CELL_ESRCH;
	}

	if (mutex.ret)
	{
		if (mutex.ret != CELL_EBUSY)
		{
			return mutex.ret;
		}
	}
	else
	{
		// The owner now (unless the thread is being stopped to be resumed later)
		if (ppu.state & cpu_flag::again)
		{
			return CELL_OK;
		}

		if (!mutex_lock_stands(ppu, mutex_id, *mutex.ptr))
		{
			return CELL_ESRCH;
		}

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
			std::lock_guard lock(mutex->mutex);

			for (auto cpu = mutex->control_sq(); cpu; cpu = cpu->next_cpu)
			{
				if (cpu == &ppu)
				{
					ppu.state += cpu_flag::again;
					return {};
				}
			}

			break;
		}

		for (usz i = 0; cpu_flag::signal - ppu.state && i < 40; i++)
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

				if (!mutex->control_sq())
				{
					// Waiters queue is empty, so the thread must have been signaled
					mutex->mutex.lock_unlock();
					break;
				}

				std::lock_guard lock(mutex->mutex);

				bool success = false;

				const auto change = [&](lv2_mutex::control_data_t& data)
				{
					success = false;

					ppu_thread* sq = static_cast<ppu_thread*>(data.sq);

					const bool retval = &ppu == sq;

					if (!mutex->unqueue<false>(sq, &ppu))
					{
						return false;
					}

					success = true;

					if (!retval)
					{
						return false;
					}

					data.sq = sq;
					return true;
				};

				for (;;)
				{
					const retro_u128_t seen = retro_atomic_load_acquire_128(&mutex->control);
					lv2_mutex::control_data_t data = lv2_mutex::control_of(seen);

					if (!change(data) || retro_atomic_cas_128(&mutex->control, seen, lv2_mutex::word_of(data)))
					{
						break;
					}
				}

				if (success)
				{
					ppu.next_cpu = nullptr;
					ppu.gpr[3] = CELL_ETIMEDOUT;
				}

				break;
			}
		}
		else
		{
			ppu.state.wait(state);
		}
	}

	// Woken as the owner: the same question as for a lock taken at once
	if (ppu.gpr[3] == CELL_OK && !(ppu.state & cpu_flag::again) && !mutex_lock_stands(ppu, mutex_id, *mutex.ptr))
	{
		return CELL_ESRCH;
	}

	return not_an_error(ppu.gpr[3]);
}

error_code sys_mutex_trylock(ppu_thread& ppu, u32 mutex_id)
{
	ppu.state += cpu_flag::wait;

	sys_mutex.trace("sys_mutex_trylock(mutex_id=0x%x)", mutex_id);

	const auto mutex = idm::pin<lv2_obj, lv2_mutex>(mutex_id);

	if (!mutex)
	{
		return CELL_ESRCH;
	}

	if (const CellError ret = mutex->try_lock(ppu))
	{
		if (ret == CELL_EBUSY)
		{
			return not_an_error(CELL_EBUSY);
		}

		return ret;
	}

	if (!mutex_lock_stands(ppu, mutex_id, *mutex))
	{
		return CELL_ESRCH;
	}

	return CELL_OK;
}

error_code sys_mutex_unlock(ppu_thread& ppu, u32 mutex_id)
{
	ppu.state += cpu_flag::wait;

	sys_mutex.trace("sys_mutex_unlock(mutex_id=0x%x)", mutex_id);

	// No lock, no reference counted. And no question to ask afterwards: a
	// mutex with an owner cannot be destroyed, so an unlock by its owner is
	// over before a destroy can succeed.
	const auto mutex = idm::pin<lv2_obj, lv2_mutex>(mutex_id);

	if (!mutex)
	{
		return CELL_ESRCH;
	}

	CellError ret{};
	{
		lv2_obj::notify_all_t notify;

		// At unlock, we have some time to do other jobs when the thread is unlikely to be in other critical sections
		notify.enqueue_on_top(vm::reservation_notifier_notify(ppu.res_notify, ppu.res_notify_time, true));

		ret = mutex_unlock(ppu, *mutex);

		notify.cleanup();
	}

	if (ret)
	{
		return ret;
	}

	return CELL_OK;
}
