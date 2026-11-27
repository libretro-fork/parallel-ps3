#pragma once

// A std::map<u32, u32>'s face over util/addr_sizes.c: the same order and
// calls, for maps that stay short. Unlike the map's, an iterator or a
// reference holds only until the next pair is added.

#include "util/types.hpp"
#include "util/addr_sizes.h"

#include <iterator>
#include <utility>

class addr_size_map
{
public:
	struct value_type
	{
		u32 first;  // Not const as the map's was: nothing may write it
		u32 second;
	};

	using key_type = u32;
	using mapped_type = u32;
	using iterator = value_type*;
	using const_iterator = const value_type*;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
	static_assert(sizeof(value_type) == sizeof(addr_size_t) && offsetof(value_type, second) == offsetof(addr_size_t, size));

	addr_sizes_t m_l{};

	value_type* data() noexcept { return static_cast<value_type*>(static_cast<void*>(addr_sizes_data(&m_l))); }
	const value_type* data() const noexcept { return static_cast<const value_type*>(static_cast<const void*>(addr_sizes_cdata(&m_l))); }

public:
	addr_size_map() noexcept = default;

	addr_size_map(const addr_size_map& r)
	{
		ensure(addr_sizes_copy(&m_l, &r.m_l));
	}

	addr_size_map(addr_size_map&& r) noexcept
	{
		addr_sizes_move(&m_l, &r.m_l);
	}

	addr_size_map& operator=(const addr_size_map& r)
	{
		ensure(addr_sizes_copy(&m_l, &r.m_l));
		return *this;
	}

	addr_size_map& operator=(addr_size_map&& r) noexcept
	{
		addr_sizes_move(&m_l, &r.m_l);
		return *this;
	}

	~addr_size_map()
	{
		addr_sizes_free(&m_l);
	}

	iterator begin() noexcept { return data(); }
	iterator end() noexcept { return data() + m_l.count; }
	const_iterator begin() const noexcept { return data(); }
	const_iterator end() const noexcept { return data() + m_l.count; }
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }
	reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
	reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
	const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
	const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
	const_reverse_iterator crbegin() const noexcept { return rbegin(); }
	const_reverse_iterator crend() const noexcept { return rend(); }

	usz size() const noexcept { return m_l.count; }
	bool empty() const noexcept { return m_l.count == 0; }

	// The first at or after the address
	iterator lower_bound(u32 addr) noexcept { return data() + addr_sizes_lower(&m_l, addr); }
	const_iterator lower_bound(u32 addr) const noexcept { return data() + addr_sizes_lower(&m_l, addr); }

	// The first after the address
	iterator upper_bound(u32 addr) noexcept { return data() + addr_sizes_upper(&m_l, addr); }
	const_iterator upper_bound(u32 addr) const noexcept { return data() + addr_sizes_upper(&m_l, addr); }

	iterator find(u32 addr) noexcept
	{
		const iterator it = lower_bound(addr);
		return it != end() && it->first == addr ? it : end();
	}

	const_iterator find(u32 addr) const noexcept
	{
		const const_iterator it = lower_bound(addr);
		return it != end() && it->first == addr ? it : end();
	}

	usz count(u32 addr) const noexcept { return find(addr) != end() ? 1 : 0; }
	bool contains(u32 addr) const noexcept { return find(addr) != end(); }

	// Put in where the address was not there; one that was keeps its value
	std::pair<iterator, bool> emplace(u32 addr, u32 value)
	{
		int added = 0;
		const u32 at = addr_sizes_add(&m_l, addr, value, &added);
		ensure(at != umax);
		return {data() + at, added != 0};
	}

	u32& operator[](u32 addr)
	{
		return emplace(addr, 0).first->second;
	}

	void clear() noexcept
	{
		addr_sizes_clear(&m_l);
	}
};
