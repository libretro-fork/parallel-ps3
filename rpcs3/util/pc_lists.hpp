#pragma once

// util/pc_lists.c with the face the SPU analyser's code was written to: a
// map from an address to a vector of values (it was
// std::unordered_map<u32, std::vector<u32>>). An entry is its address
// (first) and its list (second), as a map's pair was; a list has what the
// code used of a vector. What differs: going through the map goes by rising
// address, and nothing moves or is freed as entries come and go.

#include "util/pc_lists.h"
#include "util/types.hpp"

#include <cstring>

// A pc_list_t, used as a vector of u32
class pc_list_view
{
	pc_list_t m_list;

public:
	using value_type = u32;
	using iterator = u32*;
	using const_iterator = const u32*;

	u32* begin() { return m_list.data; }
	u32* end() { return m_list.data + m_list.count; }
	const u32* begin() const { return m_list.data; }
	const u32* end() const { return m_list.data + m_list.count; }
	usz size() const { return m_list.count; }
	bool empty() const { return !m_list.count; }
	u32& operator[](usz i) { return m_list.data[i]; }
	const u32& operator[](usz i) const { return m_list.data[i]; }
	u32& front() { return m_list.data[0]; }
	u32& back() { return m_list.data[m_list.count - 1]; }
	const u32& front() const { return m_list.data[0]; }
	const u32& back() const { return m_list.data[m_list.count - 1]; }

	void push_back(u32 value)
	{
		ensure(pc_list_push(&m_list, value));
	}

	u32& emplace_back(u32 value)
	{
		push_back(value);
		return back();
	}

	void clear()
	{
		m_list.count = 0;
	}

	// The values of [first, last) in place of what is there
	void assign(const u32* first, const u32* last)
	{
		const u32 count = static_cast<u32>(last - first);
		ensure(pc_list_reserve(&m_list, count));
		if (count)
		{
			std::memcpy(m_list.data, first, count * sizeof(u32));
		}
		m_list.count = count;
	}

	// The position after what was taken out
	u32* erase(u32* first, u32* last)
	{
		std::memmove(first, last, (end() - last) * sizeof(u32));
		m_list.count -= static_cast<u32>(last - first);
		return first;
	}

	u32* erase(u32* pos)
	{
		return erase(pos, pos + 1);
	}
};

// A pc_list_entry_t, used as a map's pair
struct pc_lists_entry
{
	u32 first;   // the address
	u32 live_;   // the table's own
	pc_list_view second;
};

static_assert(sizeof(pc_list_view) == sizeof(pc_list_t) && alignof(pc_list_view) == alignof(pc_list_t));
static_assert(sizeof(pc_lists_entry) == sizeof(pc_list_entry_t) && alignof(pc_lists_entry) == alignof(pc_list_entry_t));
static_assert(offsetof(pc_lists_entry, second) == offsetof(pc_list_entry_t, list) && offsetof(pc_lists_entry, live_) == offsetof(pc_list_entry_t, live));

class pc_lists_map
{
	pc_lists_t m_table{};

	static pc_lists_entry* view(pc_list_entry_t* e)
	{
		return static_cast<pc_lists_entry*>(static_cast<void*>(e));
	}

public:
	using key_type = u32;
	using mapped_type = pc_list_view;
	using value_type = pc_lists_entry;

	class iterator
	{
		const pc_lists_t* m_table = nullptr;
		pc_list_entry_t* m_entry = nullptr;
		friend class pc_lists_map;

		iterator(const pc_lists_t* table, pc_list_entry_t* entry) : m_table(table), m_entry(entry) {}

	public:
		iterator() = default;

		pc_lists_entry& operator*() const { return *view(m_entry); }
		pc_lists_entry* operator->() const { return view(m_entry); }

		iterator& operator++()
		{
			m_entry = pc_lists_from(m_table, m_entry->pc + 4);
			return *this;
		}

		iterator operator++(int)
		{
			iterator old = *this;
			++*this;
			return old;
		}

		bool operator==(const iterator& rhs) const { return m_entry == rhs.m_entry; }
		bool operator!=(const iterator& rhs) const { return m_entry != rhs.m_entry; }
	};

	using const_iterator = iterator;

	pc_lists_map() = default;
	pc_lists_map(const pc_lists_map&) = delete;
	pc_lists_map& operator=(const pc_lists_map&) = delete;

	~pc_lists_map()
	{
		pc_lists_free(&m_table);
	}

	// By rising address
	iterator begin() const { return iterator(&m_table, pc_lists_from(&m_table, 0)); }
	iterator end() const { return iterator(&m_table, nullptr); }

	usz size() const { return m_table.count; }
	bool empty() const { return !m_table.count; }

	iterator find(u32 pc) const { return iterator(&m_table, pc_lists_find(&m_table, pc)); }
	usz count(u32 pc) const { return pc_lists_find(&m_table, pc) ? 1 : 0; }
	bool contains(u32 pc) const { return pc_lists_find(&m_table, pc) != nullptr; }

	// The address's list, put in empty where it was not there. The address
	// is one of an SPU's: a multiple of 4 up to the end of local storage
	pc_list_view& operator[](u32 pc)
	{
		return view(ensure(pc_lists_get(&m_table, pc)))->second;
	}

	// As a map's emplace: where the address is there already, nothing
	template <typename Container>
	void emplace(u32 pc, Container&& values)
	{
		if (!pc_lists_find(&m_table, pc))
		{
			(*this)[pc].assign(values.data(), values.data() + values.size());
		}
	}

	usz erase(u32 pc)
	{
		if (pc_list_entry_t* const e = pc_lists_find(&m_table, pc))
		{
			pc_lists_erase(&m_table, e);
			return 1;
		}

		return 0;
	}

	// The entry after the one taken out
	iterator erase(iterator where)
	{
		pc_list_entry_t* const next = pc_lists_from(&m_table, where.m_entry->pc + 4);
		pc_lists_erase(&m_table, where.m_entry);
		return iterator(&m_table, next);
	}

	void clear()
	{
		pc_lists_clear(&m_table);
	}
};
