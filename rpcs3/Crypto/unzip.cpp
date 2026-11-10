#include "stdafx.h"
#include "unzip.h"

#include <encodings/deflate.h>

#include "util/serialization_ext.hpp"

std::vector<u8> unzip(const void* src, usz size)
{
	if (!src || !size) [[unlikely]]
	{
		return {};
	}

	std::vector<u8> out(size * 6);

	void* const zs = rinflate_new(16 + 15);

	if (!zs)
	{
		return {};
	}

	const u8* const in = static_cast<const u8*>(src);
	usz consumed = 0;
	usz written = 0;

	while (consumed < size)
	{
		size_t taken = 0;
		size_t wrote = 0;

		rinflate_set_in(zs, in + consumed, size - consumed);
		rinflate_set_out(zs, out.data() + written, out.size() - written);
		const int res = rinflate_process(zs, &taken, &wrote);

		consumed += taken;
		written += wrote;

		if (res == RDEFLATE_PROCESS_ERROR)
		{
			// What came out before the error, as before
			rinflate_free(zs);
			out.resize(written);
			return out;
		}

		if (res == RDEFLATE_PROCESS_END)
		{
			break;
		}

		if (written == out.size())
		{
			out.resize(out.size() + 65536);
		}
	}

	out.resize(written);

	rinflate_free(zs);

	return out;
}

bool unzip(const void* src, usz size, fs::file& out)
{
	if (!src || !size || !out)
	{
		return false;
	}

	bool is_valid = false;

	constexpr usz BUFSIZE = 32ULL * 1024ULL;
	std::vector<u8> tempbuf(BUFSIZE);

	void* const zs = rinflate_new(15);

	if (!zs)
	{
		return false;
	}

	rinflate_set_in(zs, static_cast<const u8*>(src), size);

	// The zlib stream decoded a buffer at a time, written out as it fills
	while (true)
	{
		size_t wrote = 0;

		rinflate_set_out(zs, tempbuf.data(), BUFSIZE);
		const int res = rinflate_process(zs, nullptr, &wrote);

		if (wrote && out.write(tempbuf.data(), wrote) != wrote)
		{
			break;
		}

		if (res == RDEFLATE_PROCESS_END)
		{
			is_valid = true;
			break;
		}

		// An error, or the input ran out before the end of the stream
		if (res == RDEFLATE_PROCESS_ERROR || wrote < BUFSIZE)
		{
			break;
		}
	}

	rinflate_free(zs);

	return is_valid;
}

bool zip(const void* src, usz size, fs::file& out, bool multi_thread_it)
{
	if (!src || !size || !out)
	{
		return false;
	}

	utils::serial compressor;
	compressor.set_expect_little_data(!multi_thread_it || size < 0x40'0000);
	compressor.m_file_handler = make_compressed_serialization_file_handler(out);

	std::string_view buffer_view{static_cast<const char*>(src), size};

	while (!buffer_view.empty())
	{
		if (!compressor.m_file_handler->is_valid())
		{
			return false;
		}

		const std::string_view slice = buffer_view.substr(0, 0x50'0000);

		compressor(slice);
		compressor.breathe();
		buffer_view = buffer_view.substr(slice.size());
	}

	compressor.m_file_handler->finalize(compressor);

	if (!compressor.m_file_handler->is_valid())
	{
		return false;
	}

	return true;
}
