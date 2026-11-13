// fs::file over the frontend's VFS, used from several threads at once on one
// handle, as the emulator uses it (the SPU cache is appended to by every SPU
// worker):
//  - with a frontend that reads and writes at an offset (VFS v6), nothing
//    seeks the frontend's handle and nothing waits on another thread;
//  - with an older frontend, or one whose read_at says it has none for the
//    file, the same reads and appends come out right through seek and the
//    file's lock, as before.
//
// The frontend here is a small one over file descriptors that counts its
// calls. Built by `make check` from the core's own objects and archives, the
// same link as the core's with this main.

#include "stdafx.h"
#include "libretro.h"
#include "libretro_vfs.h"
#include "Utilities/File.h"
#include "util/logs.hpp"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/PPUModule.h"
#include "Utilities/lf_u64_map.h"

#include <malloc.h>

#include <retro_atomic.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <time.h> // not pacing: the benchmark's stopwatch

#ifdef _WIN32

int main()
{
	std::printf("test_vfs_positional: skipped (the test's frontend is POSIX)\n");
	return 0;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static int failures = 0;

LOG_CHANNEL(test_log, "TEST");

extern const usz ppu_export_slots;
extern u64 ppu_export_key(std::string_view module_name, u32 fnid);

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct retro_vfs_file_handle
{
	int fd;
	char path[512];
};

static retro_atomic_size_t s_seeks;
static retro_atomic_size_t s_plain_io;
static retro_atomic_size_t s_at_io;
static bool s_at_refused = false;

static const char* fe_get_path(retro_vfs_file_handle* h) { return h->path; }

static retro_vfs_file_handle* fe_open(const char* path, unsigned mode, unsigned)
{
	int flags = O_RDONLY;
	if (mode & RETRO_VFS_FILE_ACCESS_WRITE)
	{
		flags = (mode & RETRO_VFS_FILE_ACCESS_READ) ? O_RDWR : O_WRONLY;
		flags |= O_CREAT;
		if (!(mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING))
			flags |= O_TRUNC;
	}
	const int fd = open(path, flags, 0644);
	if (fd < 0)
		return nullptr;
	retro_vfs_file_handle* h = new retro_vfs_file_handle;
	h->fd = fd;
	std::snprintf(h->path, sizeof(h->path), "%s", path);
	return h;
}

static int fe_close(retro_vfs_file_handle* h)
{
	close(h->fd);
	delete h;
	return 0;
}

static int64_t fe_size(retro_vfs_file_handle* h)
{
	struct stat st;
	return fstat(h->fd, &st) == 0 ? static_cast<int64_t>(st.st_size) : -1;
}

static int64_t fe_tell(retro_vfs_file_handle* h) { return lseek(h->fd, 0, SEEK_CUR); }

static int64_t fe_seek(retro_vfs_file_handle* h, int64_t offset, int whence)
{
	retro_atomic_inc_size(&s_seeks);
	const int w = whence == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR
		: whence == RETRO_VFS_SEEK_POSITION_END ? SEEK_END : SEEK_SET;
	return lseek(h->fd, offset, w) < 0 ? -1 : 0;
}

static int64_t fe_read(retro_vfs_file_handle* h, void* s, uint64_t len)
{
	retro_atomic_inc_size(&s_plain_io);
	return read(h->fd, s, len);
}

static int64_t fe_write(retro_vfs_file_handle* h, const void* s, uint64_t len)
{
	retro_atomic_inc_size(&s_plain_io);
	return write(h->fd, s, len);
}

static int fe_flush(retro_vfs_file_handle*) { return 0; }
static int fe_remove(const char* path) { return unlink(path); }
static int fe_rename(const char* a, const char* b) { return rename(a, b); }
static int64_t fe_truncate(retro_vfs_file_handle* h, int64_t len) { return ftruncate(h->fd, len); }

static int fe_stat(const char* path, int32_t* size)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	if (size)
		*size = static_cast<int32_t>(st.st_size);
	return S_ISDIR(st.st_mode) ? (RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY) : RETRO_VFS_STAT_IS_VALID;
}

static int fe_mkdir(const char* path) { return mkdir(path, 0755); }

static bool s_bench = false; // the stopwatch's frontend: answers at once, counts nothing

static int64_t fe_read_at(retro_vfs_file_handle* h, void* s, uint64_t len, int64_t offset)
{
	if (s_bench)
		return static_cast<int64_t>(len);
	if (s_at_refused)
		return -1;
	if (len)
		retro_atomic_inc_size(&s_at_io);
	return pread(h->fd, s, len, offset);
}

static int64_t fe_write_at(retro_vfs_file_handle* h, const void* s, uint64_t len, int64_t offset)
{
	if (s_at_refused)
		return -1;
	retro_atomic_inc_size(&s_at_io);
	return pwrite(h->fd, s, len, offset);
}

static retro_vfs_interface s_iface;

static u8 pattern(u64 offset)
{
	return static_cast<u8>((offset * 2654435761u) >> 13);
}

enum : u32 { file_size = 1u << 20, threads = 4, reads = 20000, records = 5000 };

struct record
{
	u32 thread;
	u32 seq;
	u8 fill[24];
};

static retro_atomic_size_t s_wrong;

static void reader(const fs::file* f, u32 seed)
{
	u8 buf[256];
	u32 x = seed;
	for (u32 i = 0; i < reads; i++)
	{
		x = x * 1664525u + 1013904223u;
		const u64 off = (x >> 4) % (file_size - sizeof(buf));
		if (f->read_at(off, buf, sizeof(buf)) != sizeof(buf))
		{
			retro_atomic_inc_size(&s_wrong);
			continue;
		}
		for (u32 k = 0; k < sizeof(buf); k++)
		{
			if (buf[k] != pattern(off + k))
			{
				retro_atomic_inc_size(&s_wrong);
				break;
			}
		}
	}
}

static void appender(const fs::file* f, u32 id)
{
	record r;
	std::memset(r.fill, static_cast<int>('a' + id), sizeof(r.fill));
	r.thread = id;
	for (u32 i = 0; i < records; i++)
	{
		r.seq = i;
		if (f->write(&r, sizeof(r)) != sizeof(r))
			retro_atomic_inc_size(&s_wrong);
	}
}

// One pass of everything with the frontend as it is set up now
static void pass(const char* name, const std::string& dir)
{
	const std::string data = dir + "/data.bin";
	const std::string log = dir + "/append.bin";

	retro_atomic_store_release_size(&s_wrong, 0);

	// The file, written in sequence
	{
		fs::file f(data, fs::rewrite);
		CHECK(!!f);
		u8 buf[4096];
		for (u64 off = 0; off < file_size; off += sizeof(buf))
		{
			for (u32 k = 0; k < sizeof(buf); k++)
				buf[k] = pattern(off + k);
			CHECK(f.write(buf, sizeof(buf)) == sizeof(buf));
		}
		CHECK(f.size() == file_size);
	}

	// Read at offsets by four threads on one handle while this thread reads
	// it in sequence
	{
		fs::file f(data);
		CHECK(!!f);
		CHECK(f.size() == file_size);

		std::thread t[threads];
		for (u32 i = 0; i < threads; i++)
			t[i] = std::thread(reader, &f, i + 1);

		u8 buf[4096];
		u64 seq_wrong = 0;
		for (u64 off = 0; off < file_size; off += sizeof(buf))
		{
			if (f.read(buf, sizeof(buf)) != sizeof(buf))
			{
				seq_wrong++;
				continue;
			}
			for (u32 k = 0; k < sizeof(buf); k++)
			{
				if (buf[k] != pattern(off + k))
				{
					seq_wrong++;
					break;
				}
			}
		}
		for (u32 i = 0; i < threads; i++)
			t[i].join();

		CHECK(seq_wrong == 0);
		CHECK(f.read(buf, 1) == 0); // at the end

		// Positions
		CHECK(f.seek(100) == 100);
		CHECK(f.seek(28, fs::seek_cur) == 128);
		CHECK(f.read(buf, 4) == 4 && buf[0] == pattern(128) && buf[3] == pattern(131));
		CHECK(f.seek(-4, fs::seek_end) == file_size - 4);
		CHECK(f.read(buf, 16) == 4 && buf[0] == pattern(file_size - 4));
	}

	// Appended to by four threads on one handle
	{
		fs::file f(log, fs::rewrite + fs::append);
		CHECK(!!f);

		std::thread t[threads];
		for (u32 i = 0; i < threads; i++)
			t[i] = std::thread(appender, &f, i);
		for (u32 i = 0; i < threads; i++)
			t[i].join();

		CHECK(f.size() == u64{threads} * records * sizeof(record));
	}

	// Every record whole, each thread's in its order
	{
		fs::file f(log);
		CHECK(!!f);
		CHECK(f.size() == u64{threads} * records * sizeof(record));

		u32 next[threads] = {};
		u64 torn = 0, out_of_order = 0;
		record r;
		while (f.read(&r, sizeof(r)) == sizeof(r))
		{
			bool whole = r.thread < threads;
			for (u32 k = 0; whole && k < sizeof(r.fill); k++)
				whole = r.fill[k] == 'a' + r.thread;
			if (!whole)
			{
				torn++;
				continue;
			}
			if (r.seq != next[r.thread]++)
				out_of_order++;
		}
		CHECK(torn == 0);
		CHECK(out_of_order == 0);
		for (u32 i = 0; i < threads; i++)
			CHECK(next[i] == records);
	}

	// A write in the middle leaves the size; one past the end moves it;
	// trunc sets it
	{
		fs::file f(data, fs::read + fs::write);
		CHECK(!!f);
		u8 four[4] = {1, 2, 3, 4};
		CHECK(f.seek(1000) == 1000);
		CHECK(f.write(four, 4) == 4);
		CHECK(f.size() == file_size);
		CHECK(f.seek(0, fs::seek_end) == file_size);
		CHECK(f.write(four, 4) == 4);
		CHECK(f.size() == file_size + 4);
		u8 back[4] = {};
		CHECK(f.read_at(1000, back, 4) == 4 && !std::memcmp(back, four, 4));
		CHECK(f.read_at(file_size, back, 4) == 4 && !std::memcmp(back, four, 4));
		CHECK(f.trunc(512));
		CHECK(f.size() == 512);
		CHECK(f.read_at(500, back, 100) == 12);
	}

	CHECK(retro_atomic_load_acquire_size(&s_wrong) == 0);

	std::printf("test_vfs_positional: %s: %u reads at offsets and %u appends on %u threads, %zu wrong; frontend seeks %zu, reads/writes at its position %zu, at an offset %zu\n",
		name, threads * reads, threads * records, threads,
		retro_atomic_load_acquire_size(&s_wrong),
		retro_atomic_load_acquire_size(&s_seeks),
		retro_atomic_load_acquire_size(&s_plain_io),
		retro_atomic_load_acquire_size(&s_at_io));

	fs::remove_file(data);
	fs::remove_file(log);
}

static void reset_counts()
{
	retro_atomic_store_release_size(&s_seeks, 0);
	retro_atomic_store_release_size(&s_plain_io, 0);
	retro_atomic_store_release_size(&s_at_io, 0);
}

int main()
{
	char dir[] = "/tmp/test_vfs_positional.XXXXXX";
	if (!mkdtemp(dir))
	{
		std::perror("mkdtemp");
		return 1;
	}

	s_iface.get_path = fe_get_path;
	s_iface.open = fe_open;
	s_iface.close = fe_close;
	s_iface.size = fe_size;
	s_iface.tell = fe_tell;
	s_iface.seek = fe_seek;
	s_iface.read = fe_read;
	s_iface.write = fe_write;
	s_iface.flush = fe_flush;
	s_iface.remove = fe_remove;
	s_iface.rename = fe_rename;
	s_iface.truncate = fe_truncate;
	s_iface.stat = fe_stat;
	s_iface.mkdir = fe_mkdir;
	s_iface.read_at = fe_read_at;
	s_iface.write_at = fe_write_at;

	// A frontend with VFS v6: the handle's position is never used
	libretro_vfs::set_vfs_interface(&s_iface, 6);
	reset_counts();
	pass("VFS v6", dir);
	CHECK(retro_atomic_load_acquire_size(&s_seeks) == 0);
	CHECK(retro_atomic_load_acquire_size(&s_plain_io) == 0);
	CHECK(retro_atomic_load_acquire_size(&s_at_io) != 0);

	// An older frontend: the members past its version are not read, whatever
	// is in them
	libretro_vfs::set_vfs_interface(&s_iface, 3);
	reset_counts();
	pass("VFS v3", dir);
	CHECK(retro_atomic_load_acquire_size(&s_at_io) == 0);
	CHECK(retro_atomic_load_acquire_size(&s_plain_io) != 0);

	// VFS v6 on a platform where the frontend has no positional I/O
	s_at_refused = true;
	libretro_vfs::set_vfs_interface(&s_iface, 6);
	reset_counts();
	pass("VFS v6 without positional I/O", dir);
	CHECK(retro_atomic_load_acquire_size(&s_at_io) == 0);
	CHECK(retro_atomic_load_acquire_size(&s_plain_io) != 0);

	// VFS_BENCH=1: what a read at an offset costs in the core itself, with
	// four threads each on a file of its own - nothing of theirs is shared,
	// so whatever they wait for one another over is the core's
	if (std::getenv("VFS_BENCH"))
	{
		s_at_refused = false;
		libretro_vfs::set_vfs_interface(&s_iface, 6);
		constexpr u32 threads = 4, reads = 4000000;
		std::string paths[threads];
		fs::file files[threads];
		for (u32 i = 0; i < threads; i++)
		{
			paths[i] = std::string(dir) + "/bench" + std::to_string(i);
			files[i] = fs::file(paths[i], fs::rewrite);
			CHECK(!!files[i]);
			files[i].close();
			files[i] = fs::file(paths[i], fs::read);
			CHECK(!!files[i]);
		}
		s_bench = true;
		double best = 1e18;
		for (int round = 0; round < 5; round++)
		{
			std::thread t[threads];
			timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (u32 i = 0; i < threads; i++)
				t[i] = std::thread([&files, i]
				{
					u8 buf[16];
					u64 sum = 0;
					for (u32 n = 0; n < reads; n++)
						sum += files[i].read_at(n & 0xfff, buf, sizeof(buf));
					if (sum != u64{reads} * sizeof(buf))
						std::abort();
				});
			for (u32 i = 0; i < threads; i++)
				t[i].join();
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			best = std::min(best, ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (double{reads} * threads));
		}
		s_bench = false;
		for (u32 i = 0; i < threads; i++)
		{
			files[i].close();
			fs::remove_file(paths[i]);
		}
		std::printf("test_vfs_positional: a read at an offset, four threads on four files: %.1f ns of the machine's time each\n", best);
	}

	// The log. With the ceiling at fatal - what the core sets unless its
	// "Detailed Log" option is on - nothing but a fatal error passes a
	// channel's check, so nothing else is formatted; and what the emulator
	// does to the levels when it boots (configure_logs) does not undo that.
	{
		logs::set_ceiling(logs::level::fatal);
		CHECK(!test_log.notice);
		CHECK(!test_log.error);
		CHECK(!!test_log.fatal);
		logs::reset();
		CHECK(!test_log.notice);
		CHECK(!!test_log.fatal);
		logs::set_level("TEST", logs::level::trace);
		CHECK(!test_log.notice);
		CHECK(!test_log.error);
		CHECK(!!test_log.fatal);

		// An error a game's call returns is not written up either: no
		// string made of it, nothing kept for it, however many there are
		{
			const auto used = []() { return static_cast<s64>(mallinfo2().uordblks); };
			s64 sum = 0;
			const s64 before = used();
			timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a); // not pacing: the benchmark's stopwatch
			for (u32 i = 0; i < 20000; i++)
				sum += error_code{CELL_EINVAL, "argument %u of the call", i};
			clock_gettime(CLOCK_MONOTONIC, &b); // not pacing: the benchmark's stopwatch
			const s64 after = used();
			std::printf("test_vfs_positional: an error returned by a call, the log off: %.1f ns, %lld bytes kept for 20000\n",
				((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 20000, static_cast<long long>(after - before));
			CHECK(sum == s64{20000} * static_cast<s32>(CELL_EINVAL));
			CHECK(after - before < 4096);
		}

		// With the option on: one plain file, complete, and no compressed
		// second copy beside it
		const std::string path = std::string(dir) + "/detailed.log";
		{
			std::unique_ptr<logs::listener> file = logs::make_file_listener(path, 1024 * 1024);
			logs::set_ceiling(logs::level::trace);
			CHECK(!!test_log.notice);
			for (u32 i = 0; i < 2000; i++)
				test_log.notice("line %u of the log", i);
			logs::listener::remove(file.get());
		}
		logs::set_ceiling(logs::level::fatal);

		CHECK(!fs::is_file(path + ".gz"));
		std::string text;
		CHECK(fs::file(path).read(text, fs::file(path).size()));
		usz at = 0;
		u32 found = 0;
		for (u32 i = 0; i < 2000; i++)
		{
			char line[64];
			std::snprintf(line, sizeof(line), "line %u of the log\n", i);
			const usz pos = text.find(line, at);
			if (pos == umax)
				break;
			at = pos;
			found++;
		}
		CHECK(found == 2000);
		fs::remove_file(path);
		fs::remove_file(path + ".gz");
	}

	// atomic_t's wait and notify, which go through libretro-common's
	// retro_waitaddr: a thread asleep on a word is woken by a notify of it, by
	// notify_one and by notify_all; a wait with a bound comes back when the
	// bound is up, not before; and a notify with nobody waiting is no syscall
	// (it was one, every time, on Linux: 130 ns here).
	{
		static atomic_t<u32> word{0};
		static atomic_t<u32> asleep{0};
		static atomic_t<u32> home{0};

		const auto stopwatch = []()
		{
			timespec t;
			clock_gettime(CLOCK_MONOTONIC, &t); // not pacing: the benchmark's stopwatch
			return t.tv_sec * 1e9 + t.tv_nsec;
		};

		for (u32 round = 0; round < 2; round++)
		{
			word = 0;
			asleep = 0;
			home = 0;

			std::thread sleepers[3];
			for (auto& t : sleepers)
			{
				t = std::thread([]
				{
					asleep++;
					while (word == 0)
						word.wait(0);
					home++;
				});
			}

			while (asleep != 3)
				std::this_thread::yield();

			// Nothing changed: nobody home, however long they are given
			for (u32 i = 0; i < 20000; i++)
				std::this_thread::yield();
			CHECK(home == 0);

			word = 1;
			if (round == 0)
				word.notify_all();
			else
				for (u32 i = 0; i < 3; i++)
					word.notify_one();

			for (auto& t : sleepers)
				t.join();
			CHECK(home == 3);
		}

		word = 0;
		const double before = stopwatch();
		word.wait(0, atomic_wait_timeout{20'000'000});
		const double waited = stopwatch() - before;
		CHECK(waited >= 19e6 && waited < 2e9);

		const double a = stopwatch();
		for (u32 i = 0; i < 1000000; i++)
		{
			word++;
			word.notify_one();
		}
		const double each = (stopwatch() - a) / 1e6;
		std::printf("test_vfs_positional: a store and a notify with nobody waiting: %.1f ns\n", each);
		CHECK(each < 80);
	}

	// Exported functions are looked up with no lock, by a 64-bit key made of
	// the module's name and the function's id: every function the emulator's
	// own modules register has a key of its own, and they fill well under
	// half of the map (a game's and the firmware's modules add theirs)
	{
		ppu_module_manager::initialize_modules();

		lf_u64_map keys{};
		CHECK(lf_u64_map_init(&keys, ppu_export_slots));
		u64 count = 0, clashes = 0;

		for (const auto& [name, mod] : ppu_module_manager::get())
		{
			for (const auto& [fnid, func] : mod->functions)
			{
				count++;
				clashes += lf_u64_map_add(&keys, ppu_export_key(name, fnid), count) != count;
			}
		}

		std::printf("test_vfs_positional: %llu functions registered by %zu modules, %llu keys clash\n",
			static_cast<unsigned long long>(count), ppu_module_manager::get().size(), static_cast<unsigned long long>(clashes));
		CHECK(count > 1000);
		CHECK(clashes == 0);
		CHECK(count * 4 < ppu_export_slots);
		lf_u64_map_free(&keys);
	}

	libretro_vfs::set_vfs_interface(nullptr, 0);
	rmdir(dir);

	std::printf("test_vfs_positional: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}

#endif
