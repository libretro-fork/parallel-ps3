#include "atomic.hpp"

#if defined(ARCH_ARM64) && (defined(__linux__) || defined(_WIN32)) && !defined(ARM_FEATURE_LSE2)
#include "sysinfo.hpp"

const bool utils::g_atomic_lse2 = utils::has_lse2();
#endif

#ifdef _MSC_VER

#include "emmintrin.h"
#include "immintrin.h"

namespace utils
{
	u128 __vectorcall atomic_load16(const void* ptr)
	{
		return std::bit_cast<u128>(_mm_load_si128(static_cast<const __m128i*>(ptr)));
	}

	void __vectorcall atomic_store16(void* ptr, u128 value)
	{
		_mm_store_si128(static_cast<__m128i*>(ptr), std::bit_cast<__m128i>(value));
	}
}
#endif

#include "tsc.hpp"

#include <algorithm>

#include <rthreads/retro_waitaddr.h>

// atomic_t's wait and notify go to libretro-common's retro_waitaddr: a thread
// parks on the words it names, and a notify with nobody parked in the
// address's bucket is one read-modify-write - no syscall (it was one on
// Linux, every time), no search of a table.

// Fix for silly on-first-use initializer
static bool s_null_wait_cb(const void*, u64, u64){ return true; };

// Callback for wait() function, returns false if wait should return
static thread_local bool(*s_tls_wait_cb)(const void* data, u64 attempts, u64 stamp0) = s_null_wait_cb;

// Callback for wait() function for a second custon condition, commonly passed with timeout
static thread_local bool(*s_tls_one_time_wait_cb)(u64 attempts) = nullptr;

static atomic_t<u64> s_min_tsc{0};

u64 utils::get_unique_tsc()
{
	const u64 stamp0 = utils::get_tsc();

	if (!s_min_tsc.fetch_op([=](u64& tsc)
	{
		if (stamp0 <= tsc)
		{
			// Add 1 if new stamp is too old
			return false;
		}
		else
		{
			// Update last tsc with new stamp otherwise
			tsc = stamp0;
			return true;
		}
	}).second)
	{
		// Add 1 if new stamp is too old
		// Avoid doing it in the atomic operaion because, if it gets here it means there is already much cntention
		// So break the race (at least on x86)
		return s_min_tsc.add_fetch(1);
	}

	return stamp0;
}

SAFE_BUFFERS(void)
atomic_wait_engine::wait(const void* data, u32 old_value, u64 timeout, atomic_wait::info* ext)
{
	if (!s_tls_wait_cb(data, 0, 0))
	{
		return;
	}

	const u64 stamp0 = utils::get_tsc();

	// The words waited on, the first and the list's
	retro_waitaddr_t list[atomic_wait::max_list];
	unsigned count = 1;

	list[0].addr = data;
	list[0].old = old_value;

	if (ext) [[unlikely]]
	{
		for (auto e = ext; e->data && count < atomic_wait::max_list; e++)
		{
			list[count].addr = e->data;
			list[count].old = e->old;
			count++;
		}
	}

	// In microseconds, rounded up; no bound when there is none
	const s64 timeout_us = timeout + 1 ? static_cast<s64>(std::min<u64>(timeout / 1000 + !!(timeout % 1000), u64{umax} >> 1)) : -1;

	const auto unchanged = [&]()
	{
		for (unsigned i = 0; i < count; i++)
		{
			if (static_cast<const atomic_t<u32>*>(list[i].addr)->load() != list[i].old)
			{
				return false;
			}
		}

		return true;
	};

	u64 attempts = 0;

	while (unchanged())
	{
		if (s_tls_one_time_wait_cb)
		{
			if (!s_tls_one_time_wait_cb(attempts))
			{
				break;
			}
		}

		retro_waitaddr_wait(list, count, timeout_us);

		if (!s_tls_wait_cb(data, ++attempts, stamp0))
		{
			break;
		}

		if (timeout + 1)
		{
			if (s_tls_one_time_wait_cb)
			{
				// The condition of the callback overrides timeout escape because it makes little sense to do so when a custom condition is passed
				continue;
			}

			// TODO: reduce timeout instead
			break;
		}
	}

	s_tls_wait_cb(data, -1, stamp0);
	s_tls_one_time_wait_cb = nullptr;
}

void atomic_wait_engine::set_wait_callback(bool(*cb)(const void*, u64, u64))
{
	if (cb)
	{
		s_tls_wait_cb = cb;
	}
	else
	{
		s_tls_wait_cb = s_null_wait_cb;
	}
}

void atomic_wait_engine::set_one_time_use_wait_callback(bool(*cb)(u64 progress))
{
	s_tls_one_time_wait_cb = cb;
}

void atomic_wait_engine::notify_one(const void* data)
{
	retro_waitaddr_notify_one(data);
}

SAFE_BUFFERS(void)
atomic_wait_engine::notify_all(const void* data)
{
	retro_waitaddr_notify_all(data);
}
