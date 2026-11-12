// cellGem (the PS Move) without its lock (Emu/Cell/Modules/cellGem.cpp):
// what it keeps is one record (gem_shared, retro_cas_record) that the game's
// calls, the Gem thread and the tracker read whole and change whole.
//
// - No change is lost: four threads clear the status flags of one controller
//   a bit at a time while others set another controller's filtering and
//   colour, and stand-ins for the Gem thread (connections, calibration) and
//   the tracker (the sphere's radius) change the record as those do. Every
//   bit ends cleared and every other field as its thread left it.
// - A check and a change are one step: of four threads calibrating a
//   controller at once, one starts the calibration and three are told it is
//   busy, every time.
// - A reader gets one whole record: a colour set as three equal parts is
//   never read with unequal ones.
// - While cellGemInit is under way cellGem is not initialized for the other
//   calls.
// - What a read costs against the reader's side of the lock (GEM_BENCH=1).
//
// The module is compiled into this test (its state is private to its source
// file); the rest is the core's own objects and archives, the same link as
// the core's with this main. Built by `make check`.

#include "../../Emu/Cell/Modules/cellGem.cpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <time.h> // not pacing: the benchmark's stopwatch

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

using gem_shared = gem_config::gem_shared;

static s32 code(error_code error)
{
	return static_cast<s32>(error);
}

static gem_shared now()
{
	gem_config::gem_copy copy;
	retro_cas_record_read(&g_fxo->get<gem_config>().shared, &copy.gs);
	return copy.gs;
}

int main()
{
	g_fxo->reset();
	g_fxo->init<gem_config>();
	auto& gem = g_fxo->get<gem_config>();

	// Not initialized, and not while a cellGemInit is under way
	CHECK(code(cellGemCalibrate(0)) == static_cast<s32>(CELL_GEM_ERROR_UNINITIALIZED));
	gem.state = gem_config_data::starting;
	CHECK(!gem.initialized());
	CHECK(code(cellGemCalibrate(0)) == static_cast<s32>(CELL_GEM_ERROR_UNINITIALIZED));
	CHECK(code(cellGemFilterState(0, 1)) == static_cast<s32>(CELL_GEM_ERROR_UNINITIALIZED));
	gem.state = 1;
	CHECK(gem.initialized());
	CHECK(code(cellGemCalibrate(CELL_GEM_MAX_NUM)) == static_cast<s32>(CELL_GEM_ERROR_INVALID_PARAMETER));

	// One call, one change
	CHECK(code(cellGemFilterState(1, 1)) == CELL_OK);
	CHECK(now().controllers[1].enabled_filtering && !now().controllers[0].enabled_filtering);
	CHECK(code(cellGemForceRGB(2, 0.5f, 0.25f, 1.0f)) == CELL_OK);
	CHECK(now().controllers[2].sphere_rgb.g == 0.25f && !now().controllers[2].enabled_tracking);
	CHECK(code(cellGemCalibrate(0)) == CELL_OK);
	CHECK(now().controllers[0].is_calibrating);
	CHECK(code(cellGemCalibrate(0)) == static_cast<s32>(CELL_EBUSY));
	CHECK(code(cellGemInvalidateCalibration(0)) == CELL_OK);
	CHECK(!now().controllers[0].is_calibrating);

	// No change lost
	{
		enum : u32 { clearers = 4, rounds = 300 };
		std::atomic<u32> wrong{0};
		std::atomic<bool> stop{false};
		u64 left = 0;

		std::thread filter([&] { for (u32 i = 0; i <= 20000; i++) if (code(cellGemFilterState(1, (i & 1) ^ 1)) != CELL_OK) wrong++; });
		std::thread colour([&] { for (u32 i = 0; i <= 20000; i++) { const f32 v = (i % 200) / 200.0f; if (code(cellGemForceRGB(2, v, v, v)) != CELL_OK) wrong++; } });
		// As the Gem thread does ten times a second
		std::thread gem_thread([&]
		{
			while (!stop)
				gem.change([&](gem_shared& gs)
				{
					gem.update_connections(gs);
					gem.update_calibration_status(gs);
					return true;
				});
		});
		// As the tracker does after each camera frame
		std::thread tracker([&]
		{
			for (u32 i = 1; i <= 20000; i++)
				gem.change([i](gem_shared& gs)
				{
					gs.controllers[3].radius_valid = true;
					gs.controllers[3].radius = static_cast<f32>(i);
					gs.controllers[3].distance_mm = static_cast<f32>(i) * 2.0f;
					return true;
				});
		});
		// A reader: the colour is whole
		std::atomic<u64> reads{0}, torn{0};
		std::thread reader([&]
		{
			while (!stop)
			{
				const gem_shared gs = now();
				const auto& rgb = gs.controllers[2].sphere_rgb;
				if (gs.controllers[3].distance_mm != gs.controllers[3].radius * 2.0f && gs.controllers[3].radius_valid)
					torn++;
				if (!(rgb.r == 0.5f && rgb.g == 0.25f) && (rgb.r != rgb.g || rgb.g != rgb.b))
					torn++;
				reads++;
			}
		});

		for (u32 round = 0; round < rounds; round++)
		{
			// All sixty-four flags set, then cleared a bit at a time by four threads
			gem.change([](gem_shared& gs)
			{
				gs.controllers[0].calibration_status_flags = ~0ull;
				return true;
			});
			std::thread t[clearers];
			for (u32 i = 0; i < clearers; i++)
				t[i] = std::thread([&, i]
				{
					for (u32 bit = i; bit < 64; bit += clearers)
						if (code(cellGemClearStatusFlags(0, 1ull << bit)) != CELL_OK)
							wrong++;
				});
			for (u32 i = 0; i < clearers; i++)
				t[i].join();
			left |= now().controllers[0].calibration_status_flags;
		}

		filter.join();
		colour.join();
		tracker.join();
		stop = true;
		gem_thread.join();
		reader.join();

		const gem_shared gs = now();
		CHECK(left == 0);
		CHECK(wrong == 0);
		CHECK(torn == 0 && reads != 0);
		CHECK(gs.controllers[1].enabled_filtering);
		CHECK(gs.controllers[2].sphere_rgb.r == 0.0f && gs.controllers[2].sphere_rgb.b == 0.0f);
		CHECK(gs.controllers[3].radius == 20000.0f && gs.controllers[3].distance_mm == 40000.0f);
		std::printf("test_gem_shared: %u rounds of 64 flags cleared by %u threads among four other writers: flags left 0x%llx; %llu reads, %llu torn\n",
			static_cast<u32>(rounds), static_cast<u32>(clearers), static_cast<unsigned long long>(left),
			static_cast<unsigned long long>(reads.load()), static_cast<unsigned long long>(torn.load()));
	}

	// Four threads calibrating one controller at once: one starts it
	{
		enum : u32 { callers = 4, rounds = 2000 };
		u32 bad_rounds = 0;
		std::atomic<u32> round_no{0}, started{0}, busy{0}, other{0}, finished{0};
		std::atomic<bool> stop{false};
		std::thread t[callers];
		for (u32 i = 0; i < callers; i++)
			t[i] = std::thread([&]
			{
				u32 seen = 0;
				while (!stop)
				{
					const u32 n = round_no;
					if (n == seen)
					{
						std::this_thread::yield();
						continue;
					}
					seen = n;
					const s32 result = code(cellGemCalibrate(0));
					if (result == CELL_OK)
						started++;
					else if (result == static_cast<s32>(CELL_EBUSY))
						busy++;
					else
						other++;
					finished++;
				}
			});
		for (u32 round = 1; round <= rounds; round++)
		{
			CHECK(code(cellGemInvalidateCalibration(0)) == CELL_OK);
			started = 0;
			busy = 0;
			finished = 0;
			round_no = round;
			while (finished < callers)
				std::this_thread::yield();
			if (started != 1 || busy != callers - 1)
				bad_rounds++;
		}
		stop = true;
		for (u32 i = 0; i < callers; i++)
			t[i].join();
		CHECK(bad_rounds == 0);
		CHECK(other == 0);
		std::printf("test_gem_shared: %u rounds of %u threads calibrating at once: %u with more or fewer than one starting it\n",
			static_cast<u32>(rounds), static_cast<u32>(callers), bad_rounds);
	}

	if (std::getenv("GEM_BENCH"))
	{
		enum { loops = 4000000 };
		shared_mutex old_lock; // a lock of the kind gem.mtx was
		double lock_ns = 1e9, read_ns = 1e9, change_ns = 1e9;
		u64 sink = 0;
		const auto since = [](const timespec& a)
		{
			timespec b;
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / loops;
		};
		for (int r = 0; r < 9; r++)
		{
			timespec a;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
			{
				reader_lock lock(old_lock);
				sink += gem.shared_slots[0].controllers[0].hue;
			}
			lock_ns = std::min(lock_ns, since(a));

			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
			{
				gem_config::gem_copy copy;
				retro_cas_record_read(&gem.shared, &copy.gs);
				sink += copy.gs.controllers[0].hue;
			}
			read_ns = std::min(read_ns, since(a));

			// A call that may change a controller and does not (cellGemGetState, mostly)
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (int i = 0; i < loops; i++)
				sink += gem.change_controller(0, [](gem_shared& gs) { return gs.controllers[0].hue; });
			change_ns = std::min(change_ns, since(a));
		}
		std::printf("test_gem_shared: the reader's side of the lock taken and given back %.1f ns; the record read %.1f ns (%zu bytes); read, and one controller looked over for a change %.1f ns (best of 9; %llu)\n",
			lock_ns, read_ns, sizeof(gem_shared), change_ns, static_cast<unsigned long long>(sink));
	}

	std::printf("test_gem_shared: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	// The emulator's objects want an emulator's shutdown; there is none here
	std::_Exit(failures ? 1 : 0);
}
