#pragma once

#include "util/types.hpp"
#include "util/atomic.hpp"
#include <vector>

// Upstream RPCS3's simple_ringbuf, kept here only for the tests to compare
// against: the core no longer has it (its users are on libretro-common's
// retro_spsc and libretro_audio_ring).
// Single reader/writer simple ringbuffer.
class reference_ringbuf
{
public:

	reference_ringbuf(u64 size = 0);
	virtual ~reference_ringbuf();

	reference_ringbuf(const reference_ringbuf& other);
	reference_ringbuf& operator=(const reference_ringbuf& other);

	// Thread unsafe functions.
	reference_ringbuf(reference_ringbuf&& other);
	reference_ringbuf& operator=(reference_ringbuf&& other);
	void set_buf_size(u64 size);

	// Helper functions
	u64 get_free_size() const;
	u64 get_used_size() const;
	u64 get_total_size() const;

	// Writer functions
	u64 push(const void* data, u64 size, bool force = false);
	void writer_flush(u64 cnt = umax);

	// Reader functions
	u64 pop(void* data, u64 size, bool force = false);
	void reader_flush(u64 cnt = umax);

private:

	struct ctr_state
	{
		alignas(sizeof(u64) * 2)
		u64 read_ptr = 0;
		u64 write_ptr = 0;

		auto operator<=>(const ctr_state& other) const = default;
	};

	static_assert(sizeof(ctr_state) == sizeof(u64) * 2);

	atomic_t<ctr_state> rw_ptr{};
	std::vector<u8> buf{};

	u64 get_free_size(ctr_state val) const;
	u64 get_used_size(ctr_state val) const;
};
