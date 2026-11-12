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

#include <retro_atomic.h>

#include <cstdio>
#include <cstring>
#include <thread>

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

static int64_t fe_read_at(retro_vfs_file_handle* h, void* s, uint64_t len, int64_t offset)
{
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

	libretro_vfs::set_vfs_interface(nullptr, 0);
	rmdir(dir);

	std::printf("test_vfs_positional: %s\n", failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}

#endif
