#include "stdafx.h"
#include "sys_time.h"

#include "sys_process.h"
#include "Emu/system_config.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/timers.hpp"
#include "util/tsc.hpp"

#include "util/sysinfo.hpp"

u64 g_timebase_offs{};
static u64 systemtime_offset;

#include <algorithm>
#include <retro_atomic.h>

// The guest's clock, slaved to retro_run. Each retro_run grants the guest one
// frame of time (libretro_guest_clock_grant). Within it the clock runs at the
// host's rate, smoothly, as the PS3's timebase does - audio periods, timers
// and the game's own timing see time pass inside a frame as on the console -
// but it never passes what has been granted: with no retro_run it stops (the
// frontend paused), and with retro_run faster than real time it is pulled
// forward to the frame granted before (fast-forward). Over any stretch the
// guest has had exactly the frames retro_run asked for. The PPU, SPU and RSX
// threads are not stopped: only their time is bounded. In host TSC ticks:
// the guest's TSC is tsc - lag, held within [floor, grant]; grant 0 until
// the first retro_run, the host's own until then. One writer (retro_run),
// read by every thread through a sequence count.
static retro_atomic_int_t s_clock_seq;
static retro_atomic_size_t s_clock_lag;
static retro_atomic_size_t s_clock_floor;
static retro_atomic_size_t s_clock_grant;
// How fast the guest's time has been passing against the host's, 16.16
// (retro_run's pace: 1 at the declared rate, 4 in a 4x fast-forward), and
// the host's TSC at the last grant; retro_run's own
static retro_atomic_int_t s_clock_speed;
static u64 s_clock_last_grant_tsc;

struct clock_slave
{
	u64 lag;
	u64 floor;
	u64 grant;
};

static clock_slave read_clock_slave()
{
	for (;;)
	{
		const int seq = retro_atomic_load_acquire_int(&s_clock_seq);
		if (seq & 1)
			continue;
		clock_slave c;
		c.lag   = retro_atomic_load_relaxed_size(&s_clock_lag);
		c.floor = retro_atomic_load_relaxed_size(&s_clock_floor);
		c.grant = retro_atomic_load_relaxed_size(&s_clock_grant);
		retro_atomic_thread_fence_acquire();
		if (retro_atomic_load_relaxed_int(&s_clock_seq) == seq)
			return c;
	}
}

static u64 slaved_tsc(u64 tsc)
{
	const clock_slave c = read_clock_slave();
	if (!c.grant)
		return tsc;
	return std::clamp<u64>(tsc - c.lag, c.floor, c.grant);
}

// retro_run's thread: one more frame of the guest's time
void libretro_guest_clock_grant(double frame_seconds)
{
	const u64 freq = utils::get_tsc_freq();
	if (!freq || !(frame_seconds > 0))
		return;
	const u64 frame = static_cast<u64>(frame_seconds * static_cast<double>(freq));
	const u64 tsc   = utils::get_tsc();
	u64 lag   = retro_atomic_load_relaxed_size(&s_clock_lag);
	u64 grant = retro_atomic_load_relaxed_size(&s_clock_grant);
	u64 floor;

	int speed = 1 << 16;
	if (!grant)
	{
		// Continuous with the host's clock the guest had until now
		lag   = 0;
		floor = tsc;
	}
	else
	{
		// Never behind the frames asked for: from the end of the last, on
		// at the host's rate - on from where it stopped when it stopped there
		// (paused), pulled forward to it when behind (fast-forward)
		floor = grant;
		lag   = tsc - floor;
		// A frame of the guest's time for the host's time since the last
		const u64 host = std::max<u64>(tsc - s_clock_last_grant_tsc, 1);
		speed = static_cast<int>(std::clamp<u64>((frame << 16) / host, 1 << 12, 64 << 16));
	}
	s_clock_last_grant_tsc = tsc;
	retro_atomic_store_release_int(&s_clock_speed, speed);
	grant = floor + frame;

	const int seq = retro_atomic_load_relaxed_int(&s_clock_seq);
	retro_atomic_store_relaxed_int(&s_clock_seq, seq + 1);
	retro_atomic_thread_fence_release();
	retro_atomic_store_relaxed_size(&s_clock_lag, lag);
	retro_atomic_store_relaxed_size(&s_clock_floor, floor);
	retro_atomic_store_relaxed_size(&s_clock_grant, grant);
	retro_atomic_store_release_int(&s_clock_seq, seq + 2);
}

// How long of the host's time to wait for @guest_us of the guest's to pass,
// at the pace retro_run has been coming: a quarter of it in a 4x
// fast-forward, where waiting it out by the host's clock made every sleep of
// the game's four times too long. While the guest's clock stands still at
// the end of its frame (the frontend paused or slow), a millisecond at
// least, until the next look.
u64 libretro_guest_wait_as_host(u64 guest_us)
{
	const int speed = retro_atomic_load_acquire_int(&s_clock_speed);
	if (speed <= 0)
		return guest_us;
	u64 host_us = std::max<u64>((std::min<u64>(guest_us, u64{umax} >> 16) << 16) / static_cast<u64>(speed), 1);
	const clock_slave c = read_clock_slave();
	if (c.grant && utils::get_tsc() - c.lag - c.floor >= c.grant - c.floor)
		host_us = std::max<u64>(host_us, 1000);
	return host_us;
}

// The guest's microseconds, as get_system_time's but slaved: unscaled, for
// measuring how much of the guest's time has passed
u64 get_slaved_system_time()
{
	if (const u64 freq = utils::get_tsc_freq())
	{
		const u64 tsc = slaved_tsc(utils::get_tsc());
		return tsc / freq * 1000000ull + tsc % freq * 1000000ull / freq;
	}
	return get_system_time();
}

// The host's microseconds @time as the guest's: the lag taken off, and never
// past the frame granted, where the guest's clock itself stops. The lag is
// signed: the guest's clock is ahead of the host's whenever retro_run comes
// faster than the declared frame rate (a 60 Hz display with 59.94 declared,
// fast-forward). Taken as unsigned it came out as hours, every host time
// became zero, and every timed wait of the game's ended at once.
static u64 host_to_slaved_time(u64 time)
{
	const u64 freq = utils::get_tsc_freq();
	if (!freq)
		return time;
	const clock_slave c = read_clock_slave();
	if (!c.grant)
		return time;
	const s64 lag = static_cast<s64>(c.lag);
	const s64 sfreq = static_cast<s64>(freq);
	const s64 lag_us = lag / sfreq * 1000000ll + lag % sfreq * 1000000ll / sfreq;
	const u64 grant_us = c.grant / freq * 1000000ull + c.grant % freq * 1000000ull / freq;
	return std::min<u64>(time - static_cast<u64>(lag_us), grant_us);
}

#ifndef __linux__
#include "util/asm.hpp"
#endif

#ifdef _WIN32

#include <Windows.h>

struct time_aux_info_t
{
	u64 perf_freq;
	u64 start_time;
	u64 start_ftime; // time in 100ns units since Epoch
};

// Initialize time-related values
const auto s_time_aux_info = []() -> time_aux_info_t
{
	LARGE_INTEGER freq;
	if (!QueryPerformanceFrequency(&freq))
	{
		MessageBox(nullptr, L"Your hardware doesn't support a high-resolution performance counter", L"Error", MB_OK | MB_ICONERROR);
		return {};
	}

	LARGE_INTEGER start;
	QueryPerformanceCounter(&start); // get time in 1/perf_freq units from RDTSC

	FILETIME ftime;
	GetSystemTimeAsFileTime(&ftime); // get time in 100ns units since January 1, 1601 (UTC)

	time_aux_info_t result;
	result.perf_freq   = freq.QuadPart;
	result.start_time  = start.QuadPart;
	result.start_ftime = (ftime.dwLowDateTime | static_cast<u64>(ftime.dwHighDateTime) << 32) - 116444736000000000;

	return result;
}();

#elif __APPLE__

// XXX only supports a single timer
#if !defined(HAVE_CLOCK_GETTIME)
#define TIMER_ABSTIME -1
// The opengroup spec isn't clear on the mapping from REALTIME to CALENDAR being appropriate or not.
// http://pubs.opengroup.org/onlinepubs/009695299/basedefs/time.h.html
#define CLOCK_REALTIME  1 // #define CALENDAR_CLOCK 1 from mach/clock_types.h
#define CLOCK_MONOTONIC 0 // #define SYSTEM_CLOCK 0

// the mach kernel uses struct mach_timespec, so struct timespec is loaded from <sys/_types/_timespec.h> for compatability
// struct timespec { time_t tv_sec; long tv_nsec; };

#include <sys/types.h>
#include <sys/_types/_timespec.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>
#undef CPU_STATE_MAX

#define MT_NANO (+1.0E-9)
#define MT_GIGA UINT64_C(1000000000)

// TODO create a list of timers,
static double mt_timebase = 0.0;
static u64 mt_timestart = 0;

static int clock_gettime(int clk_id, struct timespec* tp)
{
	kern_return_t retval = KERN_SUCCESS;
	if (clk_id == TIMER_ABSTIME)
	{
		if (!mt_timestart)
		{
			// only one timer, initilized on the first call to the TIMER
			mach_timebase_info_data_t tb = {0};
			mach_timebase_info(&tb);
			mt_timebase = tb.numer;
			mt_timebase /= tb.denom;
			mt_timestart = mach_absolute_time();
		}

		double diff = (mach_absolute_time() - mt_timestart) * mt_timebase;
		tp->tv_sec  = diff * MT_NANO;
		tp->tv_nsec = diff - (tp->tv_sec * MT_GIGA);
	}
	else // other clk_ids are mapped to the coresponding mach clock_service
	{
		clock_serv_t cclock;
		mach_timespec_t mts;

		host_get_clock_service(mach_host_self(), clk_id, &cclock);
		retval = clock_get_time(cclock, &mts);
		mach_port_deallocate(mach_task_self(), cclock);

		tp->tv_sec  = mts.tv_sec;
		tp->tv_nsec = mts.tv_nsec;
	}

	return retval;
}
#endif

#endif

#ifndef _WIN32

#include <exception>
#include <sys/time.h>

static struct timespec start_time = []()
{
	struct timespec ts;

	if (::clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		// Fatal error
		std::terminate();
	}

	tzset();

	return ts;
}();

#endif

LOG_CHANNEL(sys_time);

static constexpr u64 g_timebase_freq = /*79800000*/ 80000000ull; // 80 Mhz

// Convert time is microseconds to timebased time
u64 convert_to_timebased_time(u64 time)
{
	const u64 result = time * (g_timebase_freq / 1000000ull) * g_cfg.core.clocks_scale / 100u;
	ensure(result >= g_timebase_offs);
	return result - g_timebase_offs;
}

u64 get_timebased_time()
{
	if (u64 freq = utils::get_tsc_freq())
	{
		const u64 tsc = slaved_tsc(utils::get_tsc());

#if _MSC_VER
		const u64 result = static_cast<u64>(u128_from_mul(tsc, g_timebase_freq) / freq) * g_cfg.core.clocks_scale / 100u;
#else
		const u64 result = (tsc / freq * g_timebase_freq + tsc % freq * g_timebase_freq / freq) * g_cfg.core.clocks_scale / 100u;
#endif
		return result - g_timebase_offs;
	}

	while (true)
	{
#ifdef _WIN32
		LARGE_INTEGER count;
		ensure(QueryPerformanceCounter(&count));

		const u64 time = count.QuadPart;
		const u64 freq = s_time_aux_info.perf_freq;

#if _MSC_VER
		const u64 result = static_cast<u64>(u128_from_mul(time * g_cfg.core.clocks_scale, g_timebase_freq) / freq / 100u);
#else
		const u64 result = (time / freq * g_timebase_freq + time % freq * g_timebase_freq / freq) * g_cfg.core.clocks_scale / 100u;
#endif
#else
		struct timespec ts;
		ensure(::clock_gettime(CLOCK_MONOTONIC, &ts) == 0);

		const u64 result = (static_cast<u64>(ts.tv_sec) * g_timebase_freq + static_cast<u64>(ts.tv_nsec) * g_timebase_freq / 1000000000ull) * g_cfg.core.clocks_scale / 100u;
#endif
		if (result) return result - g_timebase_offs;
	}
}

// Add an offset to get_timebased_time to avoid leaking PC's uptime into the game
// As if PS3 starts at value 0 (base time) when the game boots
// If none-zero arg is specified it will become the base time (for savestates)
void initialize_timebased_time(u64 timebased_init, bool reset)
{
	g_timebase_offs = 0;

	if (reset)
	{
		// We simply want to zero-out these values
		systemtime_offset = 0;
		return;
	}

	const u64 current = get_timebased_time();
	timebased_init = current - timebased_init;

	g_timebase_offs = timebased_init;
	systemtime_offset = timebased_init / (g_timebase_freq / 1000000);
}

// Returns some relative time in microseconds, don't change this fact
u64 get_system_time()
{
	if (u64 freq = utils::get_tsc_freq())
	{
		const u64 tsc = utils::get_tsc();

#if _MSC_VER
		const u64 result = static_cast<u64>(u128_from_mul(tsc, 1000000ull) / freq);
#else
		const u64 result = (tsc / freq * 1000000ull + tsc % freq * 1000000ull / freq);
#endif
		return result;
	}

	while (true)
	{
#ifdef _WIN32
		LARGE_INTEGER count;
		ensure(QueryPerformanceCounter(&count));

		const u64 time = count.QuadPart;
		const u64 freq = s_time_aux_info.perf_freq;

#if _MSC_VER
		const u64 result = static_cast<u64>(u128_from_mul(time, 1000000ull) / freq);
#else
		const u64 result = time / freq * 1000000ull + (time % freq) * 1000000ull / freq;
#endif
#else
		struct timespec ts;
		ensure(::clock_gettime(CLOCK_MONOTONIC, &ts) == 0);

		const u64 result = static_cast<u64>(ts.tv_sec) * 1000000ull + static_cast<u64>(ts.tv_nsec) / 1000u;
#endif

		if (result) return result;
	}
}

// As get_system_time but obeys Clocks scaling setting
u64 get_guest_system_time(u64 time)
{
	const u64 result = (time != umax ? host_to_slaved_time(time) : get_slaved_system_time()) * g_cfg.core.clocks_scale / 100;
	return result - systemtime_offset;
}

// Functions
error_code sys_time_set_timezone(s32 timezone, s32 summertime)
{
	sys_time.trace("sys_time_set_timezone(timezone=0x%x, summertime=0x%x)", timezone, summertime);

	if (!g_ps3_process_info.has_root_perm())
	{
		return CELL_ENOSYS;
	}

	return CELL_OK;
}

error_code sys_time_get_timezone(vm::ptr<s32> timezone, vm::ptr<s32> summertime)
{
	sys_time.trace("sys_time_get_timezone(timezone=*0x%x, summertime=*0x%x)", timezone, summertime);

#ifdef _WIN32
	TIME_ZONE_INFORMATION tz{};
	switch (GetTimeZoneInformation(&tz))
	{
	case TIME_ZONE_ID_UNKNOWN:
	{
		*timezone = -tz.Bias;
		*summertime = 0;
		break;
	}
	case TIME_ZONE_ID_STANDARD:
	{
		*timezone = -tz.Bias;
		*summertime = -tz.StandardBias;

		if (tz.StandardBias)
		{
			sys_time.error("Unexpected timezone bias (base=%d, std=%d, daylight=%d)", tz.Bias, tz.StandardBias, tz.DaylightBias);
		}
		break;
	}
	case TIME_ZONE_ID_DAYLIGHT:
	{
		*timezone = -tz.Bias;
		*summertime = -tz.DaylightBias;
		break;
	}
	default:
	{
		ensure(0);
	}
	}
#elif __linux__
	*timezone = ::narrow<s16>(-::timezone / 60);
	*summertime = !::daylight ? 0 : []() -> s32
	{
		struct tm test{};
		ensure(&test == localtime_r(&start_time.tv_sec, &test));

		// Check bounds [0,1]
		if (test.tm_isdst & -2)
		{
			sys_time.error("No information for timezone DST bias (timezone=%.2fh, tm_gmtoff=%d)", -::timezone / 3600.0, test.tm_gmtoff);
			return 0;
		}
		else
		{
			return test.tm_isdst ? ::narrow<s16>((test.tm_gmtoff + ::timezone) / 60) : 0;
		}
	}();
#else
	// gettimeofday doesn't return timezone on linux anymore, but this should work on other OSes?
	struct timezone tz{};
	ensure(gettimeofday(nullptr, &tz) == 0);
	*timezone = ::narrow<s16>(-tz.tz_minuteswest);
	*summertime = !tz.tz_dsttime ? 0 : [&]() -> s32
	{
		struct tm test{};
		ensure(&test == localtime_r(&start_time.tv_sec, &test));

		return test.tm_isdst ? ::narrow<s16>(test.tm_gmtoff / 60 + tz.tz_minuteswest) : 0;
	}();
#endif

	return CELL_OK;
}

error_code sys_time_get_current_time(vm::ptr<s64> sec, vm::ptr<s64> nsec)
{
	sys_time.trace("sys_time_get_current_time(sec=*0x%x, nsec=*0x%x)", sec, nsec);

	if (!sec)
	{
		return CELL_EFAULT;
	}

#ifdef _WIN32
	LARGE_INTEGER count;
	ensure(QueryPerformanceCounter(&count));

	const u64 diff_base = count.QuadPart - s_time_aux_info.start_time;

	// Get time difference in nanoseconds (using 128 bit accumulator)
	const u64 diff_sl = diff_base * 1000000000ull;
	const u64 diff_sh = utils::umulh64(diff_base, 1000000000ull);
	const u64 diff = utils::udiv128(diff_sh, diff_sl, s_time_aux_info.perf_freq);

	// get time since Epoch in nanoseconds
	const u64 time = s_time_aux_info.start_ftime * 100u + (diff * g_cfg.core.clocks_scale / 100u);

	// scale to seconds, and add the console time offset (which might be negative)
	*sec = (time / 1000000000ull) + g_cfg.sys.console_time_offset;

	if (!nsec)
	{
		return CELL_EFAULT;
	}

	*nsec = time % 1000000000ull;
#else
	struct timespec ts;
	ensure(::clock_gettime(CLOCK_REALTIME, &ts) == 0);

	if (g_cfg.core.clocks_scale == 100)
	{
		// get the seconds from the system clock, and add the console time offset (which might be negative)
		*sec  = ts.tv_sec + g_cfg.sys.console_time_offset;

		if (!nsec)
		{
			return CELL_EFAULT;
		}

		*nsec = ts.tv_nsec;
		return CELL_OK;
	}

	u64 tv_sec = ts.tv_sec, stv_sec = start_time.tv_sec;
	u64 tv_nsec = ts.tv_nsec, stv_nsec = start_time.tv_nsec;

	// Substruct time since Epoch and since start time
	tv_sec -= stv_sec;

	if (tv_nsec < stv_nsec)
	{
		// Correct value if borrow encountered
		tv_sec -= 1;
		tv_nsec = 1'000'000'000ull - (stv_nsec - tv_nsec);
	}
	else
	{
		tv_nsec -= stv_nsec;
	}

	// Scale nanocseconds
	tv_nsec = stv_nsec + (tv_nsec * g_cfg.core.clocks_scale / 100);

	// Scale seconds and add from nanoseconds / 1'000'000'000, and add the console time offset (which might be negative)
	*sec = stv_sec + (tv_sec * g_cfg.core.clocks_scale / 100u) + (tv_nsec / 1000000000ull) + g_cfg.sys.console_time_offset;

	if (!nsec)
	{
		return CELL_EFAULT;
	}

	// Set nanoseconds
	*nsec = tv_nsec % 1000000000ull;
#endif

	return CELL_OK;
}

error_code sys_time_set_current_time(s64 sec, s64 nsec)
{
	sys_time.trace("sys_time_set_current_time(sec=0x%x, nsec=0x%x)", sec, nsec);

	if (!g_ps3_process_info.has_root_perm())
	{
		return CELL_ENOSYS;
	}

	return CELL_OK;
}

u64 sys_time_get_timebase_frequency()
{
	sys_time.trace("sys_time_get_timebase_frequency()");

	return g_timebase_freq;
}

error_code sys_time_get_rtc(vm::ptr<u64> rtc)
{
	sys_time.todo("sys_time_get_rtc(rtc=*0x%x)", rtc);

	return CELL_OK;
}
