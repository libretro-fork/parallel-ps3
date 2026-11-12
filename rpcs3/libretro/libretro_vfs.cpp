#include "stdafx.h"
#include <retro_atomic.h>
#include "libretro_vfs.h"
#include "Utilities/File.h"
#include <cctype>
#include <cstring>
#include <cstdarg>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h> // _stat64, _S_IFREG: not brought in by libstdc++'s headers
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace libretro_vfs
{
	static const struct retro_vfs_interface* s_vfs_interface = nullptr;

	// Counted on the game's threads, read for the log; zero as static
	// storage starts
	static retro_atomic_size_t s_vfs_open_count;
	static retro_atomic_size_t s_vfs_read_bytes;
	static retro_atomic_size_t s_vfs_write_bytes;
	static retro_atomic_size_t s_native_fallback_count;

	uint64_t get_vfs_open_count() { return retro_atomic_load_acquire_size(&s_vfs_open_count); }
	uint64_t get_vfs_read_bytes() { return retro_atomic_load_acquire_size(&s_vfs_read_bytes); }
	uint64_t get_vfs_write_bytes() { return retro_atomic_load_acquire_size(&s_vfs_write_bytes); }
	uint64_t get_native_fallback_count() { return retro_atomic_load_acquire_size(&s_native_fallback_count); }

	void reset_vfs_stats()
	{
		retro_atomic_store_release_size(&s_vfs_open_count, 0);
		retro_atomic_store_release_size(&s_vfs_read_bytes, 0);
		retro_atomic_store_release_size(&s_vfs_write_bytes, 0);
		retro_atomic_store_release_size(&s_native_fallback_count, 0);
	}

	void set_vfs_interface(const struct retro_vfs_interface* vfs_interface)
	{
		s_vfs_interface = vfs_interface;
	}

	const struct retro_vfs_interface* get_vfs_interface()
	{
		return s_vfs_interface;
	}

	bool is_vfs_available()
	{
		return s_vfs_interface != nullptr;
	}

	static unsigned mode_to_vfs_flags(const char* mode)
	{
		unsigned flags = 0;

		if (!mode || !*mode)
			return RETRO_VFS_FILE_ACCESS_READ;

		switch (mode[0])
		{
		case 'r':
			flags = RETRO_VFS_FILE_ACCESS_READ;
			if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
				flags |= RETRO_VFS_FILE_ACCESS_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
			break;
		case 'w':
			flags = RETRO_VFS_FILE_ACCESS_WRITE;
			if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
				flags |= RETRO_VFS_FILE_ACCESS_READ | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
			break;
		case 'a':
			flags = RETRO_VFS_FILE_ACCESS_WRITE;
			if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
				flags |= RETRO_VFS_FILE_ACCESS_READ;
			break;
		default:
			flags = RETRO_VFS_FILE_ACCESS_READ;
			break;
		}

		return flags;
	}

	vfs_file::vfs_file(const std::string& path, const char* mode)
		: m_path(path)
		, m_vfs_handle(nullptr)
		, m_native_handle(nullptr)
		, m_use_vfs(false)
	{
		if (is_vfs_available() && s_vfs_interface->open)
		{
			unsigned vfs_mode = mode_to_vfs_flags(mode);
			unsigned hints = RETRO_VFS_FILE_ACCESS_HINT_NONE;

			m_vfs_handle = s_vfs_interface->open(path.c_str(), vfs_mode, hints);
			if (m_vfs_handle)
			{
				m_use_vfs = true;
				retro_atomic_fetch_add_size(&s_vfs_open_count, 1);
				return;
			}
		}

		m_native_handle = std::fopen(path.c_str(), mode);
		if (m_native_handle)
			retro_atomic_fetch_add_size(&s_native_fallback_count, 1);
	}

	vfs_file::~vfs_file()
	{
		close();
	}

	vfs_file::vfs_file(vfs_file&& other) noexcept
		: m_path(std::move(other.m_path))
		, m_vfs_handle(other.m_vfs_handle)
		, m_native_handle(other.m_native_handle)
		, m_use_vfs(other.m_use_vfs)
	{
		other.m_vfs_handle = nullptr;
		other.m_native_handle = nullptr;
		other.m_use_vfs = false;
	}

	vfs_file& vfs_file::operator=(vfs_file&& other) noexcept
	{
		if (this != &other)
		{
			close();
			m_path = std::move(other.m_path);
			m_vfs_handle = other.m_vfs_handle;
			m_native_handle = other.m_native_handle;
			m_use_vfs = other.m_use_vfs;

			other.m_vfs_handle = nullptr;
			other.m_native_handle = nullptr;
			other.m_use_vfs = false;
		}
		return *this;
	}

	bool vfs_file::is_open() const
	{
		return m_use_vfs ? (m_vfs_handle != nullptr) : (m_native_handle != nullptr);
	}

	int64_t vfs_file::size() const
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->size)
		{
			return s_vfs_interface->size(m_vfs_handle);
		}
		else if (m_native_handle)
		{
			long current = std::ftell(m_native_handle);
			std::fseek(m_native_handle, 0, SEEK_END);
			long length = std::ftell(m_native_handle);
			std::fseek(m_native_handle, current, SEEK_SET);
			return static_cast<int64_t>(length);
		}

		return -1;
	}

	int64_t vfs_file::tell() const
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->tell)
		{
			return s_vfs_interface->tell(m_vfs_handle);
		}
		else if (m_native_handle)
		{
			return static_cast<int64_t>(std::ftell(m_native_handle));
		}

		return -1;
	}

	int64_t vfs_file::seek(int64_t offset, int whence)
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->seek)
		{
			int vfs_whence;
			switch (whence)
			{
			case SEEK_SET: vfs_whence = RETRO_VFS_SEEK_POSITION_START; break;
			case SEEK_CUR: vfs_whence = RETRO_VFS_SEEK_POSITION_CURRENT; break;
			case SEEK_END: vfs_whence = RETRO_VFS_SEEK_POSITION_END; break;
			default: vfs_whence = RETRO_VFS_SEEK_POSITION_START; break;
			}
			return s_vfs_interface->seek(m_vfs_handle, offset, vfs_whence);
		}
		else if (m_native_handle)
		{
			if (std::fseek(m_native_handle, static_cast<long>(offset), whence) == 0)
				return std::ftell(m_native_handle);
		}

		return -1;
	}

	int64_t vfs_file::read(void* buffer, uint64_t len)
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->read)
		{
			int64_t result = s_vfs_interface->read(m_vfs_handle, buffer, len);
			if (result > 0)
				retro_atomic_fetch_add_size(&s_vfs_read_bytes, static_cast<usz>(result));
			return result;
		}
		else if (m_native_handle)
		{
			size_t bytes_read = std::fread(buffer, 1, static_cast<size_t>(len), m_native_handle);
			return static_cast<int64_t>(bytes_read);
		}

		return -1;
	}

	int64_t vfs_file::write(const void* buffer, uint64_t len)
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->write)
		{
			int64_t result = s_vfs_interface->write(m_vfs_handle, buffer, len);
			if (result > 0)
				retro_atomic_fetch_add_size(&s_vfs_write_bytes, static_cast<usz>(result));
			return result;
		}
		else if (m_native_handle)
		{
			size_t bytes_written = std::fwrite(buffer, 1, static_cast<size_t>(len), m_native_handle);
			return static_cast<int64_t>(bytes_written);
		}

		return -1;
	}

	int vfs_file::flush()
	{
		if (!is_open())
			return -1;

		if (m_use_vfs && s_vfs_interface->flush)
		{
			return s_vfs_interface->flush(m_vfs_handle);
		}
		else if (m_native_handle)
		{
			return std::fflush(m_native_handle);
		}

		return -1;
	}

	void vfs_file::close()
	{
		if (m_use_vfs && m_vfs_handle)
		{
			if (s_vfs_interface->close)
				s_vfs_interface->close(m_vfs_handle);
			m_vfs_handle = nullptr;
		}
		else if (m_native_handle)
		{
			std::fclose(m_native_handle);
			m_native_handle = nullptr;
		}

		m_use_vfs = false;
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

	bool vfs_is_file(const std::string& path)
	{
		if (is_vfs_available() && s_vfs_interface->stat)
		{
			int32_t size = 0;
			int result = s_vfs_interface->stat(path.c_str(), &size);
			return (result & RETRO_VFS_STAT_IS_VALID) && !(result & RETRO_VFS_STAT_IS_DIRECTORY);
		}

#ifdef _WIN32
		struct _stat64 st;
		if (_stat64(path.c_str(), &st) == 0)
			return (st.st_mode & _S_IFREG) != 0;
#else
		struct stat st;
		if (stat(path.c_str(), &st) == 0)
			return S_ISREG(st.st_mode);
#endif

		return false;
	}

	bool vfs_is_dir(const std::string& path)
	{
		if (is_vfs_available() && s_vfs_interface->stat)
		{
			int32_t size = 0;
			int result = s_vfs_interface->stat(path.c_str(), &size);
			return (result & RETRO_VFS_STAT_IS_VALID) && (result & RETRO_VFS_STAT_IS_DIRECTORY);
		}

#ifdef _WIN32
		struct _stat64 st;
		if (_stat64(path.c_str(), &st) == 0)
			return (st.st_mode & _S_IFDIR) != 0;
#else
		struct stat st;
		if (stat(path.c_str(), &st) == 0)
			return S_ISDIR(st.st_mode);
#endif

		return false;
	}

	bool vfs_remove(const std::string& path)
	{
		if (is_vfs_available() && s_vfs_interface->remove)
		{
			return s_vfs_interface->remove(path.c_str()) == 0;
		}

		return std::remove(path.c_str()) == 0;
	}

	bool vfs_rename(const std::string& old_path, const std::string& new_path)
	{
		if (is_vfs_available() && s_vfs_interface->rename)
		{
			return s_vfs_interface->rename(old_path.c_str(), new_path.c_str()) == 0;
		}

		return std::rename(old_path.c_str(), new_path.c_str()) == 0;
	}

	bool vfs_mkdir(const std::string& path)
	{
		if (is_vfs_available() && s_vfs_interface->mkdir)
		{
			auto vfs_mkdir_func = s_vfs_interface->mkdir;
			int result = vfs_mkdir_func(path.c_str());
			return result == 0 || result == -2;
		}

#ifdef _WIN32
		return _mkdir(path.c_str()) == 0;
#else
		return mkdir(path.c_str(), 0755) == 0;
#endif
	}

	// VFS-backed file_base implementation for integration with fs::file
	class vfs_file_base final : public fs::file_base
	{
		struct retro_vfs_file_handle* m_handle;
		std::string m_path;
		mutable uint64_t m_pos;
		// fs::append: every write goes to the end, as O_APPEND does natively.
		const bool m_append;
		// The frontend's handle has one position shared by seek, read and
		// write, while fs::file is used from several threads at once (the SPU
		// cache is appended to by every SPU worker); each operation takes the
		// lock so that its seek and its read or write stay together.
		std::mutex m_mutex;

	public:
		vfs_file_base(struct retro_vfs_file_handle* handle, const std::string& path, bool append)
			: m_handle(handle)
			, m_path(path)
			, m_pos(0)
			, m_append(append)
		{
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
			std::lock_guard lock(m_mutex);
			if (m_handle && s_vfs_interface && s_vfs_interface->truncate)
			{
				return s_vfs_interface->truncate(m_handle, length) >= 0;
			}
			return false;
		}

		u64 read(void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->read)
				return 0;

			std::lock_guard lock(m_mutex);

			// Seek to current position first
			if (s_vfs_interface->seek)
			{
				s_vfs_interface->seek(m_handle, m_pos, RETRO_VFS_SEEK_POSITION_START);
			}

			int64_t result = s_vfs_interface->read(m_handle, buffer, count);
			if (result > 0)
			{
				m_pos += result;
				retro_atomic_fetch_add_size(&s_vfs_read_bytes, static_cast<usz>(result));
			}
			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 read_at(u64 offset, void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->read)
				return 0;

			std::lock_guard lock(m_mutex);

			// Seek to offset
			if (s_vfs_interface->seek)
			{
				s_vfs_interface->seek(m_handle, offset, RETRO_VFS_SEEK_POSITION_START);
			}

			int64_t result = s_vfs_interface->read(m_handle, buffer, count);
			if (result > 0)
			{
				retro_atomic_fetch_add_size(&s_vfs_read_bytes, static_cast<usz>(result));
			}
			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 write(const void* buffer, u64 count) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->write)
				return 0;

			std::lock_guard lock(m_mutex);

			// Seek to current position first (the end in append mode; the
			// frontend's size() is the size at open, so ask tell() instead)
			if (m_append && s_vfs_interface->seek && s_vfs_interface->tell)
			{
				s_vfs_interface->seek(m_handle, 0, RETRO_VFS_SEEK_POSITION_END);
				const int64_t end = s_vfs_interface->tell(m_handle);
				if (end >= 0)
					m_pos = static_cast<u64>(end);
			}
			else if (s_vfs_interface->seek)
			{
				s_vfs_interface->seek(m_handle, m_pos, RETRO_VFS_SEEK_POSITION_START);
			}

			int64_t result = s_vfs_interface->write(m_handle, buffer, count);
			if (result > 0)
			{
				m_pos += result;
				retro_atomic_fetch_add_size(&s_vfs_write_bytes, static_cast<usz>(result));
			}
			return result > 0 ? static_cast<u64>(result) : 0;
		}

		u64 seek(s64 offset, fs::seek_mode whence) override
		{
			if (!m_handle || !s_vfs_interface || !s_vfs_interface->seek)
				return m_pos;

			std::lock_guard lock(m_mutex);

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
					m_pos = static_cast<u64>(offset);
					break;
				case fs::seek_cur:
					m_pos = static_cast<u64>(static_cast<int64_t>(m_pos) + offset);
					break;
				case fs::seek_end:
					if (s_vfs_interface->tell)
					{
						const int64_t end = s_vfs_interface->tell(m_handle);
						if (end >= 0)
							m_pos = static_cast<u64>(end);
					}
					break;
				}
			}
			return m_pos;
		}

		u64 size() override
		{
			if (!m_handle || !s_vfs_interface)
				return 0;

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

		retro_atomic_fetch_add_size(&s_vfs_open_count, 1);
		return std::make_unique<vfs_file_base>(handle, path, (mode & VFS_MODE_APPEND) != 0);
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
			// In parentheses: the mkdir macro above (Windows) would take it otherwise
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
