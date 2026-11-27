#pragma once

// A std::map<u32, T>'s face over util/pc_slots.c for the SPU analyser's
// tables by instruction address: the same order, the same lasting
// references, nothing taken out singly.

#include "util/types.hpp"
#include "util/pc_slots.h"

#include <iterator>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class pc_map
{
public:
	struct entry
	{
		const u32 first;
		T second;
	};

private:
	static_assert(alignof(entry) <= PC_SLOTS_ALIGN);

	pc_slots_t m_t{};

	template <bool Const>
	class iter
	{
		friend class pc_map;

		const pc_slots_t* m_t = nullptr;
		entry* m_e = nullptr; // Null at the end

		iter(const pc_slots_t* t, void* e) noexcept
			: m_t(t), m_e(static_cast<entry*>(e))
		{
		}

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = entry;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<Const, const entry*, entry*>;
		using reference = std::conditional_t<Const, const entry&, entry&>;

		iter() noexcept = default;

		template <bool C2> requires (Const && !C2)
		iter(const iter<C2>& r) noexcept
			: m_t(r.m_t), m_e(r.m_e)
		{
		}

		reference operator*() const noexcept { return *m_e; }
		pointer operator->() const noexcept { return m_e; }

		iter& operator++() noexcept
		{
			m_e = static_cast<entry*>(pc_slots_from(m_t, m_e->first + 4));
			return *this;
		}

		iter operator++(int) noexcept
		{
			iter r = *this;
			++*this;
			return r;
		}

		iter& operator--() noexcept
		{
			m_e = static_cast<entry*>(pc_slots_before(m_t, m_e ? m_e->first : PC_SLOTS_LAST + 4));
			return *this;
		}

		iter operator--(int) noexcept
		{
			iter r = *this;
			--*this;
			return r;
		}

		template <bool C2>
		bool operator==(const iter<C2>& r) const noexcept { return m_e == r.m_e; }

		template <bool> friend class iter;
	};

public:
	using key_type = u32;
	using mapped_type = T;
	using value_type = entry;
	using iterator = iter<false>;
	using const_iterator = iter<true>;

	pc_map() noexcept
	{
		pc_slots_init(&m_t, sizeof(value_type));
	}

	pc_map(const pc_map&) = delete;
	pc_map& operator=(const pc_map&) = delete;

	~pc_map()
	{
		clear();
		pc_slots_free(&m_t);
	}

	iterator begin() noexcept { return {&m_t, pc_slots_from(&m_t, 0)}; }
	iterator end() noexcept { return {&m_t, nullptr}; }
	const_iterator begin() const noexcept { return {&m_t, pc_slots_from(&m_t, 0)}; }
	const_iterator end() const noexcept { return {&m_t, nullptr}; }
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }

	iterator find(u32 pc) noexcept { return {&m_t, pc_slots_find(&m_t, pc)}; }
	const_iterator find(u32 pc) const noexcept { return {&m_t, pc_slots_find(&m_t, pc)}; }

	// The first at or after the address
	iterator lower_bound(u32 pc) noexcept { return {&m_t, pc_slots_from(&m_t, pc)}; }
	const_iterator lower_bound(u32 pc) const noexcept { return {&m_t, pc_slots_from(&m_t, pc)}; }

	// The first after the address
	iterator upper_bound(u32 pc) noexcept { return {&m_t, pc < PC_SLOTS_LAST ? pc_slots_from(&m_t, pc + 1) : nullptr}; }
	const_iterator upper_bound(u32 pc) const noexcept { return {&m_t, pc < PC_SLOTS_LAST ? pc_slots_from(&m_t, pc + 1) : nullptr}; }

	usz count(u32 pc) const noexcept { return pc_slots_find(&m_t, pc) ? 1 : 0; }
	bool contains(u32 pc) const noexcept { return pc_slots_find(&m_t, pc) != nullptr; }

	usz size() const noexcept { return m_t.count; }
	bool empty() const noexcept { return m_t.count == 0; }

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(u32 pc, Args&&... args)
	{
		if (void* found = pc_slots_find(&m_t, pc))
		{
			return {iterator{&m_t, found}, false};
		}

		void* mem = ensure(pc_slots_add(&m_t, pc));
		::new (mem) value_type{pc, T(std::forward<Args>(args)...)};
		return {iterator{&m_t, mem}, true};
	}

	T& operator[](u32 pc)
	{
		return try_emplace(pc).first->second;
	}

	T& at(u32 pc)
	{
		return static_cast<value_type*>(ensure(pc_slots_find(&m_t, pc)))->second;
	}

	const T& at(u32 pc) const
	{
		return static_cast<const value_type*>(ensure(pc_slots_find(&m_t, pc)))->second;
	}

	// Every object destroyed; the table's memory is kept
	void clear() noexcept
	{
		for (void* e = pc_slots_from(&m_t, 0); e; )
		{
			value_type* const v = static_cast<value_type*>(e);
			e = pc_slots_from(&m_t, v->first + 4);
			std::destroy_at(v);
		}

		pc_slots_clear(&m_t);
	}
};
