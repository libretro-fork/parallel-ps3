#include "reference_ringbuf.h"

reference_ringbuf::reference_ringbuf(u64 size)
{
	set_buf_size(size);
}

reference_ringbuf::~reference_ringbuf()
{
	rw_ptr.load(); // Sync
}

reference_ringbuf::reference_ringbuf(const reference_ringbuf& other)
{
	ctr_state old = other.rw_ptr.load();

	for (;;)
	{
		buf = other.buf;
		rw_ptr = old;

		const ctr_state current = other.rw_ptr.load();
		if (old == current)
		{
			break;
		}
		old = current;
	}
}

reference_ringbuf& reference_ringbuf::operator=(const reference_ringbuf& other)
{
	if (this == &other) return *this;

	ctr_state old = other.rw_ptr.load();

	for (;;)
	{
		buf = other.buf;
		rw_ptr = old;

		const ctr_state current = other.rw_ptr.load();
		if (old == current)
		{
			break;
		}
		old = current;
	}

	return *this;
}

reference_ringbuf::reference_ringbuf(reference_ringbuf&& other)
{
	const ctr_state other_rw_ptr = other.rw_ptr.load();
	buf = std::move(other.buf);
	rw_ptr = other_rw_ptr;

	other.rw_ptr.store({});
}

reference_ringbuf& reference_ringbuf::operator=(reference_ringbuf&& other)
{
	if (this == &other) return *this;

	const ctr_state other_rw_ptr = other.rw_ptr.load();
	buf = std::move(other.buf);
	rw_ptr = other_rw_ptr;

	other.rw_ptr.store({});

	return *this;
}

u64 reference_ringbuf::get_free_size() const
{
	return get_free_size(rw_ptr);
}

u64 reference_ringbuf::get_used_size() const
{
	return get_used_size(rw_ptr);
}

u64 reference_ringbuf::get_total_size() const
{
	rw_ptr.load(); // Sync
	return buf.size() - 1;
}

u64 reference_ringbuf::get_free_size(ctr_state val) const
{
	const u64 buf_size = buf.size();
	const u64 rd = val.read_ptr % buf_size;
	const u64 wr = val.write_ptr % buf_size;

	return (wr >= rd ? buf_size + rd - wr : rd - wr) - 1;
}

u64 reference_ringbuf::get_used_size(ctr_state val) const
{
	const u64 buf_size = buf.size();
	const u64 rd = val.read_ptr % buf_size;
	const u64 wr = val.write_ptr % buf_size;

	return wr >= rd ? wr - rd : buf_size + wr - rd;
}

void reference_ringbuf::set_buf_size(u64 size)
{
	ensure(size != umax);

	buf.resize(size + 1);
	rw_ptr.store({});
}

void reference_ringbuf::writer_flush(u64 cnt)
{
	rw_ptr.atomic_op([&](ctr_state& val)
	{
		const u64 used = get_used_size(val);
		if (used == 0) return;

		val.write_ptr += buf.size() - std::min<u64>(used, cnt);
	});
}

void reference_ringbuf::reader_flush(u64 cnt)
{
	rw_ptr.atomic_op([&](ctr_state& val)
	{
		val.read_ptr += std::min(get_used_size(val), cnt);
	});
}

u64 reference_ringbuf::push(const void* data, u64 size, bool force)
{
	ensure(data != nullptr);

	return rw_ptr.atomic_op([&](ctr_state& val) -> u64
	{
		const u64 buf_size  = buf.size();
		const u64 old       = val.write_ptr % buf_size;
		const u64 free_size = get_free_size(val);
		const u64 to_push   = std::min(size, free_size);
		const auto b_data   = static_cast<const u8*>(data);

		if (!to_push || (!force && free_size < size))
		{
			return 0;
		}

		if (old + to_push > buf_size)
		{
			const auto first_write_sz = buf_size - old;
			memcpy(&buf[old], b_data, first_write_sz);
			memcpy(&buf[0], b_data + first_write_sz, to_push - first_write_sz);
		}
		else
		{
			memcpy(&buf[old], b_data, to_push);
		}

		val.write_ptr += to_push;

		return to_push;
	});
}

u64 reference_ringbuf::pop(void* data, u64 size, bool force)
{
	ensure(data != nullptr);

	return rw_ptr.atomic_op([&](ctr_state& val) -> u64
	{
		const u64 buf_size  = buf.size();
		const u64 old       = val.read_ptr % buf_size;
		const u64 used_size = get_used_size(val);
		const u64 to_pop    = std::min(size, used_size);
		const auto b_data   = static_cast<u8*>(data);

		if (!to_pop || (!force && used_size < size))
		{
			return 0;
		}

		if (old + to_pop > buf_size)
		{
			const auto first_read_sz = buf_size - old;
			memcpy(b_data, &buf[old], first_read_sz);
			memcpy(b_data + first_read_sz, &buf[0], to_pop - first_read_sz);
		}
		else
		{
			memcpy(b_data, &buf[old], to_pop);
		}

		val.read_ptr += to_pop;

		return to_pop;
	});
}
