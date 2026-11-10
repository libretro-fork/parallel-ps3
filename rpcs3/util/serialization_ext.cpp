#include "util/types.hpp"
#include "util/logs.hpp"
#include "util/asm.hpp"
#include "util/sysinfo.hpp"
#include "util/endian.hpp"
#include "Utilities/lockless.h"
#include "Utilities/File.h"
#include "Utilities/StrFmt.h"
#include "serialization_ext.hpp"

#include <encodings/deflate.h>
#include <encodings/rzstd.h>

LOG_CHANNEL(sys_log, "SYS");

template <>
void fmt_class_string<utils::serial>::format(std::string& out, u64 arg)
{
	const utils::serial& ar = get_object(arg);

	be_t<u64> sample64 = 0;
	const usz read_size = std::min<usz>(ar.data.size(), sizeof(sample64));
	std::memcpy(&sample64, ar.data.data() + ar.data.size() - read_size, read_size);

	fmt::append(out, "{ %s, 0x%x/0x%x, memory=0x%x, sample64=0x%016x }", ar.is_writing() ? "writing" : "reading", ar.pos, ar.data_offset + ar.data.size(), ar.data.size(), sample64);
}

// Room enough for compressing @size bytes at once in most cases (zlib's
// compressBound plus the gzip wrapper); the writers grow past it if not
static constexpr usz deflate_bound(usz size)
{
	return size + (size >> 12) + (size >> 14) + (size >> 25) + 13 + 18;
}

bool uncompressed_serialization_file_handler::handle_file_op(utils::serial& ar, usz pos, usz size, const void* data)
{
	if (ar.is_writing())
	{
		if (data)
		{
			m_file->seek(pos);
			m_file->write(data, size);
			return true;
		}

		m_file->seek(ar.data_offset);
		m_file->write(ar.data);

		if (pos == umax && size == umax)
		{
			// Request to flush the file to disk
			m_file->sync();
		}

		ar.seek_end();
		ar.data_offset = ar.pos;
		ar.data.clear();
		return true;
	}

	if (!size)
	{
		return true;
	}

	if (pos == 0 && size == umax)
	{
		// Discard loaded data until pos if profitable
		const usz limit = ar.data_offset + ar.data.size();

		if (ar.pos > ar.data_offset && ar.pos < limit)
		{
			const usz may_discard_bytes = ar.pos - ar.data_offset;
			const usz moved_byte_count_on_discard = limit - ar.pos;

			// Cheeck profitability (check recycled memory and std::memmove costs)
			if (may_discard_bytes >= 0x50'0000 || (may_discard_bytes >= 0x20'0000 && moved_byte_count_on_discard / may_discard_bytes < 3))
			{
				ar.data_offset += may_discard_bytes;
				ar.data.erase(ar.data.begin(), ar.data.begin() + may_discard_bytes);

				if (ar.data.capacity() >= 0x200'0000)
				{
					// Discard memory
					ar.data.shrink_to_fit();
				}
			}

			return true;
		}

		// Discard all loaded data
		ar.data_offset = ar.pos;
		ar.data.clear();

		if (ar.data.capacity() >= 0x200'0000)
		{
			// Discard memory
			ar.data.shrink_to_fit();
		}

		return true;
	}

	if (~pos < size - 1)
	{
		// Overflow
		return false;
	}

	if (ar.data.empty())
	{
		// Relocate instead of over-fetch
		ar.data_offset = pos;
	}

	const usz read_pre_buffer = ar.data.empty() ? 0 : utils::sub_saturate<usz>(ar.data_offset, pos);

	if (read_pre_buffer)
	{
		// Read past data
		// Harsh operation on performance, luckily rare and not typically needed
		// Also this may would be disallowed when moving to compressed files
		// This may be a result of wrong usage of breathe() function
		ar.data.resize(ar.data.size() + read_pre_buffer);
		std::memmove(ar.data.data() + read_pre_buffer, ar.data.data(), ar.data.size() - read_pre_buffer);
		ensure(m_file->read_at(pos, ar.data.data(), read_pre_buffer) == read_pre_buffer);
		ar.data_offset -= read_pre_buffer;
	}

	// Adjustment to prevent overflow
	const usz subtrahend = ar.data.empty() ? 0 : 1;
	const usz read_past_buffer = utils::sub_saturate<usz>(pos + (size - subtrahend), ar.data_offset + (ar.data.size() - subtrahend));
	const usz read_limit = utils::sub_saturate<usz>(ar.m_max_data, ar.data_offset); 

	if (read_past_buffer)
	{
		// Read proceeding data
		// More lightweight operation, this is the common operation
		// Allowed to fail, if memory is truly needed an assert would take place later
		const usz old_size = ar.data.size();

		// Try to prefetch data by reading more than requested
		ar.data.resize(std::min<usz>(read_limit, std::max<usz>({ ar.data.capacity(), ar.data.size() + read_past_buffer * 3 / 2, ar.expect_little_data() ? usz{4096} : usz{0x10'0000} })));
		ar.data.resize(m_file->read_at(old_size + ar.data_offset, data ? const_cast<void*>(data) : ar.data.data() + old_size, ar.data.size() - old_size) + old_size);
	}

	return true;
}

usz uncompressed_serialization_file_handler::get_size(const utils::serial& ar, usz recommended) const
{
	if (ar.is_writing())
	{
		return *m_file ? m_file->size() : 0;
	}

	const usz memory_available = ar.data_offset + ar.data.size();

	if (memory_available >= recommended || !*m_file)
	{
		// Avoid calling size() if possible
		return memory_available;
	}

	return std::max<usz>(m_file->size(), memory_available);
}

void uncompressed_serialization_file_handler::finalize(utils::serial& ar)
{
	ar.seek_end();
	handle_file_op(ar, 0, umax, nullptr);
	ar.data = {}; // Deallocate and clear
}

enum : u64
{
	pending_compress_bytes_bound = 0x400'0000,
	pending_data_wait_bit = 1ull << 63,
};

struct compressed_stream_data
{
	void* m_deflate{}; // rdeflate stream, while writing
	void* m_inflate{}; // rinflate stream, while reading

	~compressed_stream_data()
	{
		rdeflate_free(m_deflate);
		rinflate_free(m_inflate);
	}

	lf_queue<std::vector<u8>> m_queued_data_to_process;
	lf_queue<std::vector<u8>> m_queued_data_to_write;
};

void compressed_serialization_file_handler::initialize(utils::serial& ar)
{
	if (!m_stream)
	{
		m_stream = std::make_shared<compressed_stream_data>();
	}

	if (ar.is_writing())
	{
		if (m_write_inited)
		{
			return;
		}

		if (m_read_inited)
		{
			finalize(ar);
		}

		rdeflate_free(m_stream->m_deflate);
		ensure(m_stream->m_deflate = rdeflate_new(ar.expect_little_data() ? 9 : 8, 16 + 15));
		m_write_inited = true;
		m_errored = false;

		if (!ar.expect_little_data())
		{
			m_stream_data_prepare_thread = std::make_unique<named_thread<std::function<void()>>>("CompressedPrepare Thread"sv, [this]() { this->stream_data_prepare_thread_op(); });
			m_file_writer_thread = std::make_unique<named_thread<std::function<void()>>>("CompressedWriter Thread"sv, [this]() { this->file_writer_thread_op(); });
		}
	}
	else
	{
		if (m_read_inited)
		{
			return;
		}

		if (m_write_inited)
		{
			finalize(ar);
		}

		if (m_stream->m_inflate)
		{
			rinflate_reset(m_stream->m_inflate, 16 + 15);
		}
		else
		{
			ensure(m_stream->m_inflate = rinflate_new(16 + 15));
		}

		m_read_inited = true;
		m_errored = false;
	}
}

bool compressed_serialization_file_handler::handle_file_op(utils::serial& ar, usz pos, usz size, const void* data)
{
	if (ar.is_writing())
	{
		initialize(ar);

		if (m_errored)
		{
			return false;
		}

		auto& manager = *m_stream;
		auto& stream_data = manager.m_queued_data_to_process;

		if (data)
		{
			ensure(false);
		}

		// Writing not at the end is forbidden
		ensure(ar.pos == ar.data_offset + ar.data.size());

		if (ar.data.empty())
		{
			if (pos == umax && size == umax && *m_file)
			{
				// Request to flush the file to disk
				m_file->sync();
			}

			return true;
		}

		ar.seek_end();

		if (!m_file_writer_thread)
		{
			// Avoid multi-threading for small files
			blocked_compressed_write(ar.data);
		}
		else
		{
			while (true)
			{
				// Avoid flooding RAM, wait if there is too much pending memory
				const usz new_value = m_pending_bytes.atomic_op([&](usz& v)
				{
					v &= ~pending_data_wait_bit;

					if (v >= pending_compress_bytes_bound)
					{
						v |= pending_data_wait_bit;
					}
					else
					{
						// Overflow detector
						ensure(~v - pending_data_wait_bit > ar.data.size());

						v += ar.data.size();
					}

					return v;
				});

				if (new_value & pending_data_wait_bit)
				{
					m_pending_bytes.wait(new_value);
				}
				else
				{
					break;
				}
			}

			stream_data.push(std::move(ar.data));
		}

		ar.data_offset = ar.pos;
		ar.data.clear();

		if (pos == umax && size == umax && *m_file)
		{
			// Request to flush the file to disk
			m_file->sync();
		}

		return true;
	}

	initialize(ar);

	if (m_errored)
	{
		return false;
	}

	if (!size)
	{
		return true;
	}

	if (pos == 0 && size == umax)
	{
		// Discard loaded data until pos if profitable
		const usz limit = ar.data_offset + ar.data.size();

		if (ar.pos > ar.data_offset && ar.pos < limit)
		{
			const usz may_discard_bytes = ar.pos - ar.data_offset;
			const usz moved_byte_count_on_discard = limit - ar.pos;

			// Cheeck profitability (check recycled memory and std::memmove costs)
			if (may_discard_bytes >= 0x50'0000 || (may_discard_bytes >= 0x20'0000 && moved_byte_count_on_discard / may_discard_bytes < 3))
			{
				ar.data_offset += may_discard_bytes;
				ar.data.erase(ar.data.begin(), ar.data.begin() + may_discard_bytes);

				if (ar.data.capacity() >= 0x200'0000)
				{
					// Discard memory
					ar.data.shrink_to_fit();
				}
			}

			return true;
		}

		// Discard all loaded data
		ar.data_offset += ar.data.size();
		ensure(ar.pos >= ar.data_offset);
		ar.data.clear();

		if (ar.data.capacity() >= 0x200'0000)
		{
			// Discard memory
			ar.data.shrink_to_fit();
		}

		return true;
	}

	if (~pos < size - 1)
	{
		// Overflow
		return false;
	}

	// TODO: Investigate if this optimization is worth an implementation for compressed stream
	// if (ar.data.empty() && pos != ar.pos)
	// {
	// 	// Relocate instead of over-fetch
	// 	ar.seek_pos(pos);
	// }

	const usz read_pre_buffer = utils::sub_saturate<usz>(ar.data_offset, pos);

	if (read_pre_buffer)
	{
		// Not allowed with compressed data for now
		// Unless someone implements mechanism for it
		ensure(false);
	}

	// Adjustment to prevent overflow
	const usz subtrahend = ar.data.empty() ? 0 : 1;
	const usz read_past_buffer = utils::sub_saturate<usz>(pos + (size - subtrahend), ar.data_offset + (ar.data.size() - subtrahend));
	const usz read_limit = utils::sub_saturate<usz>(ar.m_max_data, ar.data_offset); 

	if (read_past_buffer)
	{
		// Read proceeding data
		// More lightweight operation, this is the common operation
		// Allowed to fail, if memory is truly needed an assert would take place later
		const usz old_size = ar.data.size();

		// Try to prefetch data by reading more than requested
		const usz new_size = std::min<usz>(read_limit, std::max<usz>({ ar.data.capacity(), ar.data.size() + read_past_buffer * 3 / 2, ar.expect_little_data() ? usz{4096} : usz{0x10'0000} }));

		if (new_size < old_size)
		{
			// Read limit forbids further reads at this point
			return true;
		}

		ar.data.resize(new_size);
		ar.data.resize(this->read_at(ar, old_size + ar.data_offset, data ? const_cast<void*>(data) : ar.data.data() + old_size, ar.data.size() - old_size) + old_size);
	}

	return true;
}

usz compressed_serialization_file_handler::read_at(utils::serial& ar, usz read_pos, void* data, usz size)
{
	ensure(read_pos == ar.data.size() + ar.data_offset - size);

	if (!size || m_errored)
	{
		return 0;
	}

	initialize(ar);

	void* const zs = m_stream->m_inflate;

	const usz total_to_read = size;
	usz read_size = 0;
	u8* const out_data = static_cast<u8*>(data);

	while (read_size < total_to_read)
	{
		// Drain extracted memory stash (also before first file read)
		if (const usz avail_in = m_stream_data.size() - m_stream_data_index)
		{
			size_t taken = 0;
			size_t wrote = 0;

			rinflate_set_in(zs, m_stream_data.data() + m_stream_data_index, avail_in);
			rinflate_set_out(zs, out_data + read_size, total_to_read - read_size);
			const int res = rinflate_process(zs, &taken, &wrote);

			read_size += wrote;
			m_stream_data_index += taken;

			if (res == RDEFLATE_PROCESS_ERROR)
			{
				m_errored = true;
				m_read_inited = false;
				sys_log.error("Failure of compressed data reading. (read_size=0x%x, avail_in=0x%x, ar=%s)", read_size, avail_in, ar);
				return read_size;
			}

			if (res == RDEFLATE_PROCESS_END)
			{
				// The end of the stream: nothing follows
				break;
			}

			if (read_size >= total_to_read || m_stream_data_index < m_stream_data.size())
			{
				continue;
			}
		}

		const usz add_size = ar.expect_little_data() ? 0x1'0000 : 0x10'0000;
		const usz old_file_buf_size = m_stream_data.size();

		m_stream_data.resize(old_file_buf_size + add_size);
		m_stream_data.resize(old_file_buf_size + m_file->read_at(m_file_read_index, m_stream_data.data() + old_file_buf_size, add_size));

		if (m_stream_data.size() == old_file_buf_size)
		{
			// EOF
			break;
		}

		m_file_read_index += m_stream_data.size() - old_file_buf_size;
	}

	if (m_stream_data.size() - m_stream_data_index <= m_stream_data_index / 5)
	{
		// Shrink to required memory size
		m_stream_data.erase(m_stream_data.begin(), m_stream_data.begin() + m_stream_data_index);

		if (m_stream_data.capacity() >= 0x200'0000)
		{
			// Discard memory
			m_stream_data.shrink_to_fit();
		}

		m_stream_data_index = 0;
	}

	return read_size;
}

void compressed_serialization_file_handler::skip_until(utils::serial& ar)
{
	ensure(!ar.is_writing() && ar.pos >= ar.data_offset);

	if (ar.pos > ar.data_offset)
	{
		handle_file_op(ar, ar.data_offset, ar.pos - ar.data_offset, nullptr);
	}
}

void compressed_serialization_file_handler::finalize(utils::serial& ar)
{
	handle_file_op(ar, 0, umax, nullptr);

	if (!m_stream)
	{
		return;
	}

	auto& stream = *m_stream;

	if (m_read_inited)
	{
		m_read_inited = false;
		return;
	}

	stream.m_queued_data_to_process.push(std::vector<u8>());

	if (m_file_writer_thread)
	{
		// Join here to avoid log messages in the destructor
		(*m_file_writer_thread)();
	}

	m_stream_data_prepare_thread.reset();
	m_file_writer_thread.reset();

	if (void* const zs = stream.m_deflate; zs && !m_errored)
	{
		rdeflate_set_in(zs, nullptr, 0);
		rdeflate_finish(zs);

		m_stream_data.resize(0x10'0000);

		while (true)
		{
			size_t wrote = 0;
			rdeflate_set_out(zs, m_stream_data.data(), m_stream_data.size());
			const int res = rdeflate_process(zs, nullptr, &wrote);

			if (res == RDEFLATE_PROCESS_ERROR)
			{
				break;
			}

			m_file->write(m_stream_data.data(), wrote);

			if (res == RDEFLATE_PROCESS_END)
			{
				break;
			}
		}

		m_stream_data = {};
	}

	rdeflate_free(stream.m_deflate);
	stream.m_deflate = nullptr;

	m_write_inited = false;
	ar.data = {}; // Deallocate and clear

	m_file->sync();
}

void compressed_serialization_file_handler::stream_data_prepare_thread_op()
{
	compressed_stream_data& stream = *m_stream;
	void* const zs = stream.m_deflate;

	while (true)
	{
		stream.m_queued_data_to_process.wait();

		for (auto&& data : stream.m_queued_data_to_process.pop_all())
		{
			if (data.empty())
			{
				// Abort is requested, flush data and exit
				stream.m_queued_data_to_write.push(std::vector<u8>());
				return;
			}

			rdeflate_set_in(zs, data.data(), data.size());

			usz buffer_offset = 0;
			m_stream_data.resize(deflate_bound(data.size()));

			// The input is all taken once the output has room left
			while (true)
			{
				size_t wrote = 0;
				rdeflate_set_out(zs, m_stream_data.data() + buffer_offset, m_stream_data.size() - buffer_offset);

				if (rdeflate_process(zs, nullptr, &wrote) == RDEFLATE_PROCESS_ERROR)
				{
					m_errored = true;

					// Abort
					stream.m_queued_data_to_write.push(std::vector<u8>());
					break;
				}

				buffer_offset += wrote;

				if (buffer_offset < m_stream_data.size())
				{
					break;
				}

				m_stream_data.resize(m_stream_data.size() + (m_stream_data.size() / 16) + 1);
			}

			if (m_errored)
			{
				return;
			}

			// Forward for file write
			const usz queued_size = data.size();

			const usz size_diff = buffer_offset - queued_size;
			const usz new_val = m_pending_bytes.add_fetch(size_diff);
			const usz left = new_val & ~pending_data_wait_bit;

			if (new_val & pending_data_wait_bit && left < pending_compress_bytes_bound && left - size_diff >= pending_compress_bytes_bound && !m_pending_signal)
			{
				// Notification is postponed until data write and memory release
				m_pending_signal = true;
			}

			// Ensure wait bit state has not changed by the update
			ensure(~((new_val - size_diff) ^ new_val) & pending_data_wait_bit);

			if (!buffer_offset)
			{
				if (m_pending_signal)
				{
					m_pending_bytes.notify_all();
				}

				continue;
			}

			m_stream_data.resize(buffer_offset);
			stream.m_queued_data_to_write.push(std::move(m_stream_data));
		}
	}
}

void compressed_serialization_file_handler::file_writer_thread_op()
{
	compressed_stream_data& stream = *m_stream;

	while (true)
	{
		stream.m_queued_data_to_write.wait();

		for (auto&& data : stream.m_queued_data_to_write.pop_all())
		{
			if (data.empty())
			{
				return;
			}

			const usz last_size = data.size();
			m_file->write(data);
			data = {}; // Deallocate before notification

			const usz new_val = m_pending_bytes.sub_fetch(last_size);
			const usz left = new_val & ~pending_data_wait_bit;
			const bool pending_sig = m_pending_signal && m_pending_signal.exchange(false);

			if (pending_sig || (new_val & pending_data_wait_bit && left < pending_compress_bytes_bound && left + last_size >= pending_compress_bytes_bound))
			{
				m_pending_bytes.notify_all();
			}

			// Ensure wait bit state has not changed by the update
			ensure(~((new_val + last_size) ^ new_val) & pending_data_wait_bit);
		}
	}
}

void compressed_serialization_file_handler::blocked_compressed_write(const std::vector<u8>& data)
{
	void* const zs = m_stream->m_deflate;

	rdeflate_set_in(zs, data.data(), data.size());

	m_stream_data.resize(deflate_bound(data.size()));

	// The input is all taken once the output has room left
	while (true)
	{
		size_t wrote = 0;
		rdeflate_set_out(zs, m_stream_data.data(), m_stream_data.size());

		if (rdeflate_process(zs, nullptr, &wrote) == RDEFLATE_PROCESS_ERROR || m_file->write(m_stream_data.data(), wrote) != wrote)
		{
			m_errored = true;
			break;
		}

		if (wrote < m_stream_data.size())
		{
			break;
		}
	}
}

usz compressed_serialization_file_handler::get_size(const utils::serial& ar, usz recommended) const
{
	if (ar.is_writing())
	{
		return *m_file ? m_file->size() : 0;
	}

	const usz memory_available = ar.data_offset + ar.data.size();

	if (memory_available >= recommended || !*m_file)
	{
		// Avoid calling size() if possible
		return memory_available;
	}

	return std::max<usz>(utils::mul_saturate<usz>(m_file->size(), 6), memory_available);
}

struct compressed_zstd_stream_data
{
	// The frame being read, a block at a time; null between frames
	rzstd_blocks_t* m_frame{};

	// What the last block decoded that the reader has not taken yet, where
	// the frame decoded it
	const u8* m_pending{};
	usz m_pending_size = 0;

	lf_queue<std::vector<u8>> m_queued_data_to_process;
	lf_queue<std::vector<u8>> m_queued_data_to_write;

	~compressed_zstd_stream_data()
	{
		rzstd_blocks_free(m_frame);
	}
};

void compressed_zstd_serialization_file_handler::initialize(utils::serial& ar)
{
	if (!m_stream)
	{
		m_stream = std::make_shared<compressed_zstd_stream_data>();
	}

	if (ar.is_writing())
	{
		if (m_write_inited)
		{
			return;
		}

		if (m_read_inited)
		{
			finalize(ar);
		}

		m_write_inited = true;
		m_errored = false;

		m_compression_threads.clear();
		m_file_writer_thread.reset();

		// Make sure at least one thread is free
		// Limit thread count in order to make sure memory limits are under control (TODO: scale with RAM size)
		const usz thread_count = std::min<u32>(std::max<u32>(utils::get_thread_count(), 2) - 1, 32);

		for (usz i = 0; i < thread_count; i++)
		{
			m_compression_threads.emplace_back().m_thread = std::make_unique<named_thread<std::function<void()>>>(fmt::format("CompressedPrepare Thread %d", i + 1), [this]() { this->stream_data_prepare_thread_op(); });
		}

		m_file_writer_thread = std::make_unique<named_thread<std::function<void()>>>("CompressedWriter Thread"sv, [this]() { this->file_writer_thread_op(); });
	}
	else
	{
		if (m_read_inited)
		{
			return;
		}

		if (m_write_inited)
		{
			finalize(ar);
		}

		// Each frame opens when the reader reaches it
		m_read_inited = true;
		m_errored = false;
	}
}

bool compressed_zstd_serialization_file_handler::handle_file_op(utils::serial& ar, usz pos, usz size, const void* data)
{
	if (ar.is_writing())
	{
		initialize(ar);

		if (m_errored)
		{
			return false;
		}

		if (data)
		{
			ensure(false);
		}

		// Writing not at the end is forbidden
		ensure(ar.pos == ar.data_offset + ar.data.size());

		if (ar.data.empty())
		{
			if (pos == umax && size == umax && *m_file)
			{
				// Request to flush the file to disk
				m_file->sync();
			}

			return true;
		}

		ar.seek_end();

		const usz buffer_idx = m_input_buffer_index++ % m_compression_threads.size();
		auto& input = m_compression_threads[buffer_idx].m_input;

		while (input)
		{
			// No waiting support on non-null pointer
			thread_ctrl::wait_for(2'000);
		}

		input.store(stx::make_single_value(std::move(ar.data)));
		input.notify_all();

		ar.data_offset = ar.pos;
		ar.data.clear();

		if (pos == umax && size == umax && *m_file)
		{
			// Request to flush the file to disk
			m_file->sync();
		}

		return true;
	}

	initialize(ar);

	if (m_errored)
	{
		return false;
	}

	if (!size)
	{
		return true;
	}

	if (pos == 0 && size == umax)
	{
		// Discard loaded data until pos if profitable
		const usz limit = ar.data_offset + ar.data.size();

		if (ar.pos > ar.data_offset && ar.pos < limit)
		{
			const usz may_discard_bytes = ar.pos - ar.data_offset;
			const usz moved_byte_count_on_discard = limit - ar.pos;

			// Cheeck profitability (check recycled memory and std::memmove costs)
			if (may_discard_bytes >= 0x50'0000 || (may_discard_bytes >= 0x20'0000 && moved_byte_count_on_discard / may_discard_bytes < 3))
			{
				ar.data_offset += may_discard_bytes;
				ar.data.erase(ar.data.begin(), ar.data.begin() + may_discard_bytes);

				if (ar.data.capacity() >= 0x200'0000)
				{
					// Discard memory
					ar.data.shrink_to_fit();
				}
			}

			return true;
		}

		// Discard all loaded data
		ar.data_offset += ar.data.size();
		ensure(ar.pos >= ar.data_offset);
		ar.data.clear();

		if (ar.data.capacity() >= 0x200'0000)
		{
			// Discard memory
			ar.data.shrink_to_fit();
		}

		return true;
	}

	if (~pos < size - 1)
	{
		// Overflow
		return false;
	}

	// TODO: Investigate if this optimization is worth an implementation for compressed stream
	// if (ar.data.empty() && pos != ar.pos)
	// {
	// 	// Relocate instead of over-fetch
	// 	ar.seek_pos(pos);
	// }

	const usz read_pre_buffer = utils::sub_saturate<usz>(ar.data_offset, pos);

	if (read_pre_buffer)
	{
		// Not allowed with compressed data for now
		// Unless someone implements mechanism for it
		ensure(false);
	}

	// Adjustment to prevent overflow
	const usz subtrahend = ar.data.empty() ? 0 : 1;
	const usz read_past_buffer = utils::sub_saturate<usz>(pos + (size - subtrahend), ar.data_offset + (ar.data.size() - subtrahend));
	const usz read_limit = utils::sub_saturate<usz>(ar.m_max_data, ar.data_offset); 

	if (read_past_buffer)
	{
		// Read proceeding data
		// More lightweight operation, this is the common operation
		// Allowed to fail, if memory is truly needed an assert would take place later
		const usz old_size = ar.data.size();

		// Try to prefetch data by reading more than requested
		const usz new_size = std::min<usz>(read_limit, std::max<usz>({ ar.data.capacity(), ar.data.size() + read_past_buffer * 3 / 2, ar.expect_little_data() ? usz{4096} : usz{0x10'0000} }));

		if (new_size < old_size)
		{
			// Read limit forbids further reads at this point
			return true;
		}

		ar.data.resize(new_size);
		ar.data.resize(this->read_at(ar, old_size + ar.data_offset, data ? const_cast<void*>(data) : ar.data.data() + old_size, ar.data.size() - old_size) + old_size);
	}

	return true;
}

usz compressed_zstd_serialization_file_handler::read_at(utils::serial& ar, usz read_pos, void* data, usz size)
{
	ensure(read_pos == ar.data.size() + ar.data_offset - size);

	if (!size || m_errored)
	{
		return 0;
	}

	initialize(ar);

	compressed_zstd_stream_data& stream = *m_stream;

	const usz total_to_read = size;
	usz read_size = 0;
	u8* const out_data = static_cast<u8*>(data);

	// The file is a run of frames, one per chunk the writer was handed. Each is
	// decoded a block at a time, every block from exactly the bytes the frame
	// asks for next, read straight out of the file buffer
	while (read_size < total_to_read)
	{
		// Hand over what the last block decoded first
		if (stream.m_pending_size)
		{
			const usz n = std::min<usz>(stream.m_pending_size, total_to_read - read_size);
			std::memcpy(out_data + read_size, stream.m_pending, n);
			read_size += n;
			stream.m_pending += n;
			stream.m_pending_size -= n;
			continue;
		}

		const usz avail_in = m_stream_data.size() - m_stream_data_index;
		usz want = 1;

		if (stream.m_frame)
		{
			want = rzstd_blocks_want(stream.m_frame);

			if (!want)
			{
				// The frame is all out, the next one starts here
				rzstd_blocks_free(stream.m_frame);
				stream.m_frame = nullptr;
				continue;
			}
		}

		if (avail_in < want)
		{
			const usz add_size = std::max<usz>(want - avail_in, ar.expect_little_data() ? 0x1'0000 : 0x10'0000);
			const usz old_file_buf_size = m_stream_data.size();

			m_stream_data.resize(old_file_buf_size + add_size);
			m_stream_data.resize(old_file_buf_size + m_file->read_at(m_file_read_index, m_stream_data.data() + old_file_buf_size, add_size));

			if (m_stream_data.size() == old_file_buf_size)
			{
				// EOF
				break;
			}

			m_file_read_index += m_stream_data.size() - old_file_buf_size;
			continue;
		}

		if (!stream.m_frame && !(stream.m_frame = rzstd_blocks_open()))
		{
			m_errored = true;
			sys_log.error("Failure of compressed data reading: out of memory. (read_size=0x%x, ar=%s)", read_size, ar);
			return read_size;
		}

		want = rzstd_blocks_want(stream.m_frame);

		if (avail_in < want)
		{
			continue;
		}

		const u8* block = nullptr;
		size_t block_size = 0;

		if (rzstd_blocks_give(stream.m_frame, m_stream_data.data() + m_stream_data_index, want, &block, &block_size) != RZSTD_PROCESS_NEXT)
		{
			m_errored = true;
			sys_log.error("Failure of compressed data reading. (read_size=0x%x, avail_in=0x%x, ar=%s)", read_size, avail_in, ar);
			return read_size;
		}

		m_stream_data_index += want;
		stream.m_pending = block;
		stream.m_pending_size = block_size;
	}

	if (m_stream_data.size() - m_stream_data_index <= m_stream_data_index / 5)
	{
		// Shrink to required memory size
		m_stream_data.erase(m_stream_data.begin(), m_stream_data.begin() + m_stream_data_index);

		if (m_stream_data.capacity() >= 0x200'0000)
		{
			// Discard memory
			m_stream_data.shrink_to_fit();
		}

		m_stream_data_index = 0;
	}

	return read_size;
}

void compressed_zstd_serialization_file_handler::skip_until(utils::serial& ar)
{
	ensure(!ar.is_writing() && ar.pos >= ar.data_offset);

	if (ar.pos > ar.data_offset)
	{
		handle_file_op(ar, ar.data_offset, ar.pos - ar.data_offset, nullptr);
	}
}

void compressed_zstd_serialization_file_handler::finalize(utils::serial& ar)
{
	handle_file_op(ar, 0, umax, nullptr);

	if (!m_stream)
	{
		return;
	}

	if (m_read_inited)
	{
		rzstd_blocks_free(m_stream->m_frame);
		m_stream->m_frame = nullptr;
		m_stream->m_pending = nullptr;
		m_stream->m_pending_size = 0;
		m_read_inited = false;
		return;
	}

	const stx::shared_ptr<std::vector<u8>> empty_data = stx::make_single<std::vector<u8>>();
	const stx::shared_ptr<std::vector<u8>> null_ptr = stx::null_ptr;

	for (bool has_pending_threads = true; has_pending_threads; thread_ctrl::wait_for(500))
	{
		has_pending_threads = false;

		// Try to notify all in bulk
		for (auto& context : m_compression_threads)
		{
			if (!context.notified && !context.m_input && context.m_input.compare_and_swap_test(null_ptr, empty_data))
			{
				context.notify_pending = true;
			}
		}

		for (auto& context : m_compression_threads)
		{
			if (context.notify_pending)
			{
				context.notified = true;
				context.notify_pending = false;
				context.m_input.notify_all();
			}
		}

		for (auto& context : m_compression_threads)
		{
			// Wait for notification to be sent and received
			// And wait for data to be written to be read by the thread
			if (!context.notified || context.m_input || context.m_output)
			{
				has_pending_threads = true;
			}
		}
	}

	for (usz idx = m_output_buffer_index;;)
	{
		auto& out_cur = m_compression_threads[idx % m_compression_threads.size()].m_output;
		auto& out_next = m_compression_threads[(idx + 1) % m_compression_threads.size()].m_output;

		out_cur.compare_and_swap_test(null_ptr, empty_data);
		out_next.compare_and_swap_test(null_ptr, empty_data);

		if (usz new_val = m_output_buffer_index; idx != new_val)
		{
			// Index was changed inbetween, retry on the next index
			idx = new_val;
			continue;
		}

		// Must be waiting on either of the two
		out_cur.notify_all();

		// Check for single thread
		if (&out_next != &out_cur)
		{
			out_next.notify_all();
		}

		break;
	}

	if (m_file_writer_thread)
	{
		// Join here to avoid log messages in the destructor
		(*m_file_writer_thread)();
	}

	m_compression_threads.clear();
	m_file_writer_thread.reset();

	m_stream_data = {};
	m_write_inited = false;
	ar.data = {}; // Deallocate and clear

	m_file->sync();
}

void compressed_zstd_serialization_file_handler::stream_data_prepare_thread_op()
{
	std::vector<u8> stream_data;

	const stx::shared_ptr<std::vector<u8>> null_ptr = stx::null_ptr;
	const usz thread_index = m_thread_buffer_index++;

	while (true)
	{
		auto& input = m_compression_threads[thread_index].m_input;
		auto& output = m_compression_threads[thread_index].m_output;

		while (!input)
		{
			input.wait(nullptr);
		}

		auto data = input.exchange(stx::null_ptr);
		input.notify_all();

		if (data->empty())
		{
			// Abort is requested, flush data and exit
			break;
		}

		// One frame per chunk. Level 5 compresses within a fraction of a
		// percent of rzstd's top levels, at the speed the old library's
		// level 8 had
		stream_data.resize(::rzstd_compress_bound(data->size()));

		size_t out_size = 0;
		ensure(::rzstd_encode(stream_data.data(), stream_data.size(), data->data(), data->size(), 5, &out_size) == RZSTD_PROCESS_END && out_size);

		if (m_errored)
		{
			break;
		}

		stream_data.resize(out_size);

		const stx::shared_ptr<std::vector<u8>> data_ptr = make_single<std::vector<u8>>(std::move(stream_data));

		while (output || !output.compare_and_swap_test(null_ptr, data_ptr))
		{
			thread_ctrl::wait_for(1000);
		}

		//if (m_output_buffer_index % m_compression_threads.size() == thread_index)
		{
			output.notify_all();
		}
	}
}

void compressed_zstd_serialization_file_handler::file_writer_thread_op()
{
	for (m_output_buffer_index = 0;; m_output_buffer_index++)
	{
		auto& output = m_compression_threads[m_output_buffer_index % m_compression_threads.size()].m_output;

		while (!output)
		{
			output.wait(nullptr);
		}

		auto data = output.exchange(stx::null_ptr);
		output.notify_all();

		if (data->empty())
		{
			break;
		}

		m_file->write(*data);
	}
}

usz compressed_zstd_serialization_file_handler::get_size(const utils::serial& ar, usz recommended) const
{
	if (ar.is_writing())
	{
		return *m_file ? m_file->size() : 0;
	}

	const usz memory_available = ar.data_offset + ar.data.size();

	if (memory_available >= recommended || !*m_file)
	{
		// Avoid calling size() if possible
		return memory_available;
	}

	return recommended;
}

bool null_serialization_file_handler::handle_file_op(utils::serial&, usz, usz, const void*)
{
	return true;
}

void null_serialization_file_handler::finalize(utils::serial&)
{
}
