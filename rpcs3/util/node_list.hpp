#pragma once

// A list of owning pointers on util/node_list.c, for C++ code that kept a
// std::list<std::unique_ptr<T>>: the same order, the same stable elements
// (adding or erasing others moves none), no allocation for each one.

#include "util/node_list.h"
#include "util/types.hpp"

#include <memory>
#include <new>
#include <utility>

template <typename T>
class owned_node_list
{
	using ptr_t = std::unique_ptr<T>;
	static_assert(sizeof(ptr_t) == sizeof(void*) && alignof(ptr_t) == alignof(void*));

	node_list_t m_list{};

	static ptr_t& value_of(node_list_node_t* n)
	{
		return *std::launder(static_cast<ptr_t*>(static_cast<void*>(n->room.bytes)));
	}

public:
	class iterator
	{
		node_list_node_t* m_node = nullptr;
		friend class owned_node_list;

	public:
		iterator() = default;
		explicit iterator(node_list_node_t* n) : m_node(n) {}

		ptr_t& operator*() const { return value_of(m_node); }
		ptr_t* operator->() const { return &value_of(m_node); }
		iterator& operator++() { m_node = m_node->next; return *this; }
		iterator operator++(int) { iterator old = *this; m_node = m_node->next; return old; }
		bool operator==(const iterator& rhs) const { return m_node == rhs.m_node; }
		bool operator!=(const iterator& rhs) const { return m_node != rhs.m_node; }
	};

	owned_node_list() = default;
	owned_node_list(const owned_node_list&) = delete;
	owned_node_list& operator=(const owned_node_list&) = delete;

	~owned_node_list()
	{
		clear();
		node_list_free(&m_list);
	}

	iterator begin() { return iterator(m_list.head); }
	iterator end() { return iterator(nullptr); }
	usz size() const { return m_list.count; }
	bool empty() const { return !m_list.count; }
	ptr_t& back() { return value_of(m_list.tail); }

	void push_back(ptr_t&& value)
	{
		node_list_node_t* const n = ensure(node_list_push_back(&m_list));

		new (n->room.bytes) ptr_t(std::move(value));
	}

	// The element after the one erased
	iterator erase(iterator where)
	{
		value_of(where.m_node).~ptr_t();
		return iterator(node_list_erase(&m_list, where.m_node));
	}

	template <typename F>
	void remove_if(F&& pred)
	{
		for (iterator it = begin(); it != end();)
		{
			if (pred(*it))
			{
				it = erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	// The nodes are kept for the next adds
	void clear()
	{
		while (m_list.head)
		{
			erase(iterator(m_list.head));
		}
	}
};
