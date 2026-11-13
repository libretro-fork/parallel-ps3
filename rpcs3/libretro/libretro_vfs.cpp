#include "stdafx.h"
#include <retro_atomic.h>
#include "libretro_vfs.h"
#include "Utilities/File.h"
#include <cctype>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h> // _stat64, _S_IFREG: not brought in by libstdc++'s headers
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace libretro_vfs
{
	static const struct retro_vfs_interface* s_vfs_interface = nullptr;
	// The version the frontend gave: its members past that are not there
	static unsigned s_vfs_version = 0;

	void set_vfs_interface(const struct retro_vfs_interface* vfs_interface, unsigned version)
	{
		s_vfs_interface = vfs_interface;
		s_vfs_version = vfs_interface ? version : 0;
	}

	bool is_vfs_available()
	{
		return s_vfs_interface != nullptr;
	}

	bool vfs_stat(const std::string& path, int64_t* size_out)
	{
		if (is_vfs_available() && s_vfs_interface->stat)
		{
			int32_t size32 = 0;
			int result = s_vfs_interface->stat(path.c_str(), &size32);
			if (result > 0)
			{
				if (size_out)
					*size_out = static_cast<int64_t>(size32);
				return true;
			}
		}

#ifdef _WIN32
		struct _stat64 st;
		if (_stat64(path.c_str(), &st) == 0)
		{
			if (size_out)
				*size_out = st.st_size;
			return true;
		}
#else
		struct stat st;
		if (stat(path.c_str(), &st) == 0)
		{
			if (size_out)
				*size_out = st.st_size;
			return true;
		}
#endif

		return false;
	}

	// VFS-backed file_base implementation for integration with fs::file
	class vfs_file_base final : public fs::file_base
	{
		struct retro_vfs_file_handle* m_handle;
		// Where the next read or write without an offset goes
		retro_atomic_64_t m_pos;
		// The file's end (m_at only): the size at open, moved by this
		// handle's writes
		retro_atomic_64_t m_end;
		// fs::append: every write goes to the end, as O_APPEND does natively.
		const bool m_append;
		// The frontend reads and writes this file at an offset (VFS v6):
		// nothing here touches the handle's own position, and nothing locks.
		const bool m_at;
		// Without that (an older frontend, or a platform where it has no
		// positional I/O) the handle has one position shared by seek, read
		// and write, while fs::file is used from several threads at once (the
		// SPU cache is appended to by every SPU worker): no way round a lock
		// there, so that an operation's seek and its read or write stay
		// together.
		std::mutex m_mutex;

		void raise_end(s64 end)
		{
			s64 old = retro_atomic_load_acquire_64(&m_end);
			while (old < end && !retro_atomic_cas_64(&m_end, old, end))
				old = retro_atomic_load_acquire_64(&m_end);
		}

	public:
		vfs_file_base(struct retro_vfs_file_handle* handle, bool append, bool at)
			: m_handle(handle)
			, m_append(append)
			, m_at(at)
		{
			s64 end = 0;
			if (at && s_vfs_interface->size)
				end = s_vfs_interface->size(handle);
			retro_atomic_64_init(&m_pos, 0);
			retro_atomic_64_init(&m_end, end > 0 ? end : 0);
		}

		~vfs_file_base() override
		{
			if (m_handle && s_vfs_interface && s_vfs_interface->close)
			{
				s_vfs_interface->close(m_handle);
				m_handle = nullptr;
			}
		}

		fs::stat_t get_stat() override
		{
			fs::stat_t info{};
			info.is_directory = false;
			info.is_writable = true;
			info.size = size();
			info.atime = 0;
			info.mtime = 0;
			info.ctime = 0;
			return info;
		}

		void sync() override
		{
			if (m_handle && s_vfs_interface && s_vfs_interface->flush)
			{
				s_vfs_interface->flush(m_handle);
			}
		}

		bool trunc(u64 length) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->truncate)
				return false;

			if (m_at)
			{
				if (s_vfs_interface->truncate(m_handle, length) < 0)
					return false;
				retro_atomic_store_release_64(&m_end, static_cast<s64>(length));
				return true;
			}

			std::lock_guard lock(m_mutex);
			return s_vfs_interface->truncate(m_handle, length) >= 0;
		}

		u64 read(void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->read)
				return 0;

			int64_t result;

			if (m_at)
			{
				// Read where the position is, then move the position on with a
				// compare-and-swap: if another thread moved it meanwhile - one
				// reading the same file on, or seeking it - the read is done
				// again from where it now is. So threads reading one file on
				// from its position each get a part of their own, with no
				// lock held round them (sys_fs_read holds none), and the
				// position never passes the end of the file.
				while (true)
				{
					const s64 pos = retro_atomic_load_acquire_64(&m_pos);

					result = s_vfs_interface->read_at(m_handle, buffer, count, pos);

					if (retro_atomic_cas_64(&m_pos, pos, pos + (result > 0 ? result : 0)))
						break;
				}
			}
			else
			{
				std::lock_guard lock(m_mutex);
				const s64 pos = retro_atomic_load_relaxed_64(&m_pos);

				// Seek to current position first
				if (s_vfs_interface->seek)
				{
					s_vfs_interface->seek(m_handle, pos, RETRO_VFS_SEEK_POSITION_START);
				}

				result = s_vfs_interface->read(m_handle, buffer, count);
				if (result > 0)
					retro_atomic_store_relaxed_64(&m_pos, pos + result);
			}

			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 read_at(u64 offset, void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->read)
				return 0;

			int64_t result;

			if (m_at)
			{
				result = s_vfs_interface->read_at(m_handle, buffer, count, static_cast<s64>(offset));
			}
			else
			{
				std::lock_guard lock(m_mutex);

				// Seek to offset
				if (s_vfs_interface->seek)
				{
					s_vfs_interface->seek(m_handle, offset, RETRO_VFS_SEEK_POSITION_START);
				}

				result = s_vfs_interface->read(m_handle, buffer, count);
			}

			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 write(const void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->write)
				return 0;

			int64_t result;

			if (m_at)
			{
				// An append takes its place at the end with one add, so two
				// threads' appends neither overlap nor tear
				const s64 pos = m_append
					? retro_atomic_fetch_add_64(&m_end, static_cast<s64>(count))
					: retro_atomic_load_acquire_64(&m_pos);

				result = s_vfs_interface->write_at(m_handle, buffer, count, pos);
				if (result > 0)
				{
					if (m_append)
						retro_atomic_store_release_64(&m_pos, pos + result);
					else
					{
						retro_atomic_fetch_add_64(&m_pos, result);
						raise_end(pos + result);
					}
				}
			}
			else
			{
				std::lock_guard lock(m_mutex);
				s64 pos = retro_atomic_load_relaxed_64(&m_pos);

				// Seek to current position first (the end in append mode; the
				// frontend's size() is the size at open, so ask tell() instead)
				if (m_append && s_vfs_interface->seek && s_vfs_interface->tell)
				{
					s_vfs_interface->seek(m_handle, 0, RETRO_VFS_SEEK_POSITION_END);
					const int64_t end = s_vfs_interface->tell(m_handle);
					if (end >= 0)
						pos = end;
				}
				else if (s_vfs_interface->seek)
				{
					s_vfs_interface->seek(m_handle, pos, RETRO_VFS_SEEK_POSITION_START);
				}

				result = s_vfs_interface->write(m_handle, buffer, count);
				retro_atomic_store_relaxed_64(&m_pos, result > 0 ? pos + result : pos);
			}

			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 write_at(u64 offset, const void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->write)
				return 0;

			int64_t result;

			if (m_at)
			{
				result = s_vfs_interface->write_at(m_handle, buffer, count, static_cast<s64>(offset));
				if (result > 0)
					raise_end(static_cast<s64>(offset) + result);
			}
			else
			{
				// An older frontend: through the handle's position, under the
				// file's lock, which is then put back where it was
				std::lock_guard lock(m_mutex);

				if (!s_vfs_interface->seek)
					return 0;

				s_vfs_interface->seek(m_handle, offset, RETRO_VFS_SEEK_POSITION_START);
				result = s_vfs_interface->write(m_handle, buffer, count);
				s_vfs_interface->seek(m_handle, retro_atomic_load_relaxed_64(&m_pos), RETRO_VFS_SEEK_POSITION_START);
			}

			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 seek(s64 offset, fs::seek_mode whence) override
		{
			if (!m_handle || !s_vfs_interface)
				return static_cast<u64>(retro_atomic_load_acquire_64(&m_pos));

			if (m_at)
			{
				// Only the number moves: the handle's position is not used
				s64 pos;
				switch (whence)
				{
				case fs::seek_cur: pos = retro_atomic_load_acquire_64(&m_pos) + offset; break;
				case fs::seek_end: pos = retro_atomic_load_acquire_64(&m_end) + offset; break;
				default:           pos = offset; break;
				}
				if (pos < 0)
					return static_cast<u64>(retro_atomic_load_acquire_64(&m_pos));
				retro_atomic_store_release_64(&m_pos, pos);
				return static_cast<u64>(pos);
			}

			if (!s_vfs_interface->seek)
				return static_cast<u64>(retro_atomic_load_acquire_64(&m_pos));

			std::lock_guard lock(m_mutex);
			s64 pos = retro_atomic_load_relaxed_64(&m_pos);

			int vfs_whence;
			switch (whence)
			{
			case fs::seek_set: vfs_whence = RETRO_VFS_SEEK_POSITION_START; break;
			case fs::seek_cur: vfs_whence = RETRO_VFS_SEEK_POSITION_CURRENT; break;
			case fs::seek_end: vfs_whence = RETRO_VFS_SEEK_POSITION_END; break;
			default: vfs_whence = RETRO_VFS_SEEK_POSITION_START; break;
			}

			int64_t result = s_vfs_interface->seek(m_handle, offset, vfs_whence);
			// VFS seek returns 0 on success, not the new position
			if (result == 0)
			{
				// Calculate new position based on whence
				switch (whence)
				{
				case fs::seek_set:
					pos = offset;
					break;
				case fs::seek_cur:
					pos += offset;
					break;
				case fs::seek_end:
					if (s_vfs_interface->tell)
					{
						const int64_t end = s_vfs_interface->tell(m_handle);
						if (end >= 0)
							pos = end;
					}
					break;
				}
				retro_atomic_store_relaxed_64(&m_pos, pos);
			}
			return static_cast<u64>(pos);
		}

		u64 size() override
		{
			if (!m_handle || !s_vfs_interface)
				return 0;

			if (m_at)
				return static_cast<u64>(retro_atomic_load_acquire_64(&m_end));

			// The frontend's size() stays at the size the file had when it
			// was opened; the end is where writes since then have moved it.
			// Every read and write seeks first, so moving the handle is fine.
			if (s_vfs_interface->seek && s_vfs_interface->tell)
			{
				std::lock_guard lock(m_mutex);
				s_vfs_interface->seek(m_handle, 0, RETRO_VFS_SEEK_POSITION_END);
				const int64_t end = s_vfs_interface->tell(m_handle);
				if (end >= 0)
					return static_cast<u64>(end);
			}

			if (!s_vfs_interface->size)
				return 0;

			int64_t result = s_vfs_interface->size(m_handle);
			return result > 0 ? static_cast<u64>(result) : 0;
		}
	};

	std::unique_ptr<fs::file_base> create_vfs_file_base(const std::string& path, unsigned int mode)
	{
		if (!is_vfs_available() || !s_vfs_interface->open)
			return nullptr;

		unsigned vfs_flags = 0;
		if (mode & VFS_MODE_READ)
			vfs_flags |= RETRO_VFS_FILE_ACCESS_READ;
		if (mode & VFS_MODE_WRITE)
			vfs_flags |= RETRO_VFS_FILE_ACCESS_WRITE;
		if (!(mode & VFS_MODE_TRUNC) && (mode & VFS_MODE_WRITE))
			vfs_flags |= RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;

		unsigned hints = RETRO_VFS_FILE_ACCESS_HINT_NONE;
		struct retro_vfs_file_handle* handle = s_vfs_interface->open(path.c_str(), vfs_flags, hints);

		if (!handle)
			return nullptr;

		// Positional I/O where the frontend has it for this file: asked once,
		// with a read of nothing, or a write of nothing to a file that is
		// only written (-1 where it has none)
		u8 none = 0;
		const bool at = s_vfs_version >= 6
			&& s_vfs_interface->read_at && s_vfs_interface->write_at
			&& ((mode & VFS_MODE_READ)
				? s_vfs_interface->read_at(handle, &none, 0, 0)
				: s_vfs_interface->write_at(handle, &none, 0, 0)) == 0;

		return std::make_unique<vfs_file_base>(handle, (mode & VFS_MODE_APPEND) != 0, at);
	}
}

namespace libretro_vfs
{
	bool is_uri(std::string_view path)
	{
		// RFC 3986: a letter, then letters, digits, '+', '-' or '.'; drive
		// letters ("C:/") have no "//" after the colon
		const usz colon = path.find("://");
		if (colon == umax || colon == 0 || !std::isalpha(static_cast<unsigned char>(path[0])))
			return false;
		for (usz i = 1; i < colon; i++)
		{
			const auto c = static_cast<unsigned char>(path[i]);
			if (!std::isalnum(c) && c != '+' && c != '-' && c != '.')
				return false;
		}
		return true;
	}

	// The path without trailing delimiters, which RPCS3 often puts after a
	// directory and the frontend's VFS may not take
	static std::string uri_trim(const std::string& path)
	{
		usz end = path.find_last_not_of("/\\");
		const usz root = path.find("://") + 3;
		return path.substr(0, end == umax || end < root ? root : end + 1);
	}

	// stat, with the size of a file from an open handle: the VFS stat's size
	// is 32-bit, and PS3 game files can be bigger
	static bool uri_stat(const std::string& path, fs::stat_t& info)
	{
		info = {};
		if (!is_vfs_available() || !s_vfs_interface->stat)
		{
			fs::g_tls_error = fs::error::noent;
			return false;
		}

		int32_t size32 = 0;
		const int result = s_vfs_interface->stat(uri_trim(path).c_str(), &size32);
		if (!(result & RETRO_VFS_STAT_IS_VALID))
		{
			fs::g_tls_error = fs::error::noent;
			return false;
		}

		info.is_directory = (result & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
		info.is_writable = true;
		if (!info.is_directory)
		{
			info.size = static_cast<u32>(size32);
			if (auto* handle = s_vfs_interface->open(path.c_str(), RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE))
			{
				const int64_t size = s_vfs_interface->size(handle);
				if (size >= 0)
					info.size = static_cast<u64>(size);
				s_vfs_interface->close(handle);
			}
		}
		return true;
	}

	class uri_dir final : public fs::dir_base
	{
		std::string m_path;
		struct retro_vfs_dir_handle* m_handle = nullptr;
		// "." and "..", which native listings start with, first
		int m_dots = 0;

	public:
		explicit uri_dir(std::string path)
			: m_path(std::move(path))
		{
		}

		~uri_dir() override
		{
			if (m_handle)
				s_vfs_interface->closedir(m_handle);
		}

		bool open()
		{
			m_handle = s_vfs_interface->opendir(m_path.c_str(), true);
			return m_handle != nullptr;
		}

		bool read(fs::dir_entry& info) override
		{
			if (m_dots < 2)
			{
				info = {};
				info.name = m_dots++ ? ".." : ".";
				info.is_directory = true;
				info.is_writable = true;
				return true;
			}

			while (m_handle && s_vfs_interface->readdir(m_handle))
			{
				const char* name = s_vfs_interface->dirent_get_name(m_handle);
				if (!name || !std::strcmp(name, ".") || !std::strcmp(name, ".."))
					continue;

				fs::stat_t st{};
				if (s_vfs_interface->dirent_is_dir(m_handle))
				{
					st.is_directory = true;
					st.is_writable = true;
				}
				else if (!uri_stat(m_path + '/' + name, st))
				{
					continue;
				}

				static_cast<fs::stat_t&>(info) = st;
				info.name = name;
				return true;
			}

			return false;
		}

		void rewind() override
		{
			if (m_handle)
				s_vfs_interface->closedir(m_handle);
			m_handle = nullptr;
			m_dots = 0;
			open();
		}
	};

	class uri_device final : public fs::device_base
	{
	public:
		bool stat(const std::string& path, fs::stat_t& info) override
		{
			return uri_stat(path, info);
		}

		bool statfs(const std::string& path, fs::device_stat& info) override
		{
			fs::stat_t st;
			if (!uri_stat(path, st))
				return false;

			// The frontend's VFS doesn't tell free space
			info = {.block_size = 4096, .total_size = umax, .total_free = umax, .avail_free = umax};
			return true;
		}

		bool remove_dir(const std::string& path) override
		{
			return remove(path);
		}

		bool create_dir(const std::string& path) override
		{
			const int result = (s_vfs_interface->mkdir)(uri_trim(path).c_str());
			if (result == -2)
				fs::g_tls_error = fs::error::exist;
			else if (result != 0)
				fs::g_tls_error = fs::error::noent;
			return result == 0;
		}

		bool rename(const std::string& from, const std::string& to) override
		{
			if (s_vfs_interface->rename(uri_trim(from).c_str(), uri_trim(to).c_str()) == 0)
				return true;
			fs::g_tls_error = fs::error::noent;
			return false;
		}

		bool remove(const std::string& path) override
		{
			if (s_vfs_interface->remove(uri_trim(path).c_str()) == 0)
				return true;
			fs::g_tls_error = fs::error::noent;
			return false;
		}

		std::unique_ptr<fs::file_base> open(const std::string& path, bs_t<fs::open_mode> mode) override
		{
			// As the native open: the frontend's has neither "only a new file"
			// nor "only an existing one"
			if (mode & (fs::excl + fs::write))
			{
				const bool exists = vfs_stat(path, nullptr);
				if ((mode & fs::excl) && exists)
				{
					fs::g_tls_error = fs::error::exist;
					return nullptr;
				}
				if ((mode & fs::write) && !(mode & fs::create) && !exists)
				{
					fs::g_tls_error = fs::error::noent;
					return nullptr;
				}
			}

			unsigned vfs_mode = 0;
			if (mode & fs::read)   vfs_mode |= VFS_MODE_READ;
			if (mode & fs::write)  vfs_mode |= VFS_MODE_WRITE;
			if (mode & fs::append) vfs_mode |= VFS_MODE_APPEND;
			if (mode & fs::create) vfs_mode |= VFS_MODE_CREATE;
			if (mode & fs::trunc)  vfs_mode |= VFS_MODE_TRUNC;
			if (mode & fs::excl)   vfs_mode |= VFS_MODE_EXCL;

			auto file = create_vfs_file_base(path, vfs_mode);
			if (!file)
				fs::g_tls_error = fs::error::noent;
			return file;
		}

		std::unique_ptr<fs::dir_base> open_dir(const std::string& path) override
		{
			auto dir = std::make_unique<uri_dir>(uri_trim(path));
			if (!dir->open())
			{
				fs::g_tls_error = fs::error::noent;
				return nullptr;
			}
			return dir;
		}
	};

	stx::shared_ptr<fs::device_base> get_uri_device()
	{
		if (!is_vfs_available() || !s_vfs_interface->opendir)
			return {};

		static const stx::shared_ptr<fs::device_base> device = stx::make_shared<uri_device>();
		return device;
	}
}
