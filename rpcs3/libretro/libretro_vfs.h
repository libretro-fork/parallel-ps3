#pragma once

#include "libretro.h"
#include <string>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include "util/shared_ptr.hpp"

// Forward declaration for fs::file_base
namespace fs { struct file_base; struct device_base; }

namespace libretro_vfs
{
	// version: what the frontend wrote back to required_interface_version
	void set_vfs_interface(const struct retro_vfs_interface* vfs_interface, unsigned version);
	bool is_vfs_available();

	// A file's size through the frontend, or natively without it
	bool vfs_stat(const std::string& path, int64_t* size_out);

	// Whether path is a frontend URI (scheme://...), such as the saf:// paths
	// RetroArch hands out for Android's Storage Access Framework, which only
	// the frontend's VFS can reach
	bool is_uri(std::string_view path);

	// An fs::device_base over the frontend's VFS (stat, directories, mkdir,
	// remove, rename and files), which fs::get_virtual_device returns for
	// URIs. nullptr without the VFS
	stx::shared_ptr<fs::device_base> get_uri_device();

	// Create a VFS-backed file_base for use with fs::file
	// Returns nullptr if VFS is not available or open fails
	std::unique_ptr<fs::file_base> create_vfs_file_base(const std::string& path, unsigned int mode);

	// Mode flags matching fs:: namespace
	constexpr unsigned int VFS_MODE_READ   = 1 << 0;
	constexpr unsigned int VFS_MODE_WRITE  = 1 << 1;
	constexpr unsigned int VFS_MODE_APPEND = 1 << 2;
	constexpr unsigned int VFS_MODE_CREATE = 1 << 3;
	constexpr unsigned int VFS_MODE_TRUNC  = 1 << 4;
	constexpr unsigned int VFS_MODE_EXCL   = 1 << 5;
}
