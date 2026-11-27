#pragma once
#include <util/types.hpp>
#include <util/bless.hpp>
#include <span>
#include <tuple>
#include <type_traits>

namespace rsx
{
	template <typename T>
	concept SpanLike = requires(T t)
	{
		{ t.data() } -> std::convertible_to<const void*>;
		{ t.size_bytes() } -> std::convertible_to<usz>;
	};

	template <typename T>
	concept Integral = std::is_integral_v<T> || std::is_same_v<T, u128>;

	class io_buffer
	{
		mutable void* m_ptr = nullptr;
		mutable usz m_size = 0;

		// The allocator, for a buffer whose memory is taken when it is first
		// asked for: the caller's own callable, called through a plain
		// function. It is not kept: it has to outlive the buffer, as the
		// callers' do (a lambda declared before the buffer). It was a
		// std::function, built and copied for every texture uploaded, and
		// allocated on the heap for a lambda with more than two captures
		using allocator_fn = std::tuple<void*, usz>(*)(void* self, usz size);
		void* m_allocator_self = nullptr;
		allocator_fn m_allocator = nullptr;
		mutable usz m_allocation_size = 0u;

	public:
		io_buffer() = default;

		template <SpanLike T>
		io_buffer(const T& container)
		{
			m_ptr = const_cast<void*>(reinterpret_cast<const void*>(container.data()));
			m_size = container.size_bytes();
		}

		template <typename F>
			requires std::is_invocable_v<F&, usz>
		io_buffer(F& allocator)
			: m_allocator_self(const_cast<void*>(static_cast<const void*>(&allocator)))
			, m_allocator(+[](void* self, usz size) -> std::tuple<void*, usz>
			{
				return (*static_cast<F*>(self))(size);
			})
		{}

		// Not from a temporary: nothing would be left to call
		template <typename F>
			requires std::is_invocable_v<F&, usz>
		io_buffer(F&& allocator) = delete;

		template <Integral T>
		io_buffer(void* ptr, T size)
			: m_ptr(ptr), m_size(size)
		{}

		template <Integral T>
		io_buffer(const void* ptr, T size)
			: m_ptr(const_cast<void*>(ptr)), m_size(size)
		{}

		void reserve(usz size) const
		{
			m_allocation_size = size;
		}

		std::pair<void*, usz> raw() const
		{
			return { m_ptr, m_size };
		}

		template <Integral T = u8>
		T* data() const
		{
			if (!m_ptr && m_allocator)
			{
				std::tie(m_ptr, m_size) = m_allocator(m_allocator_self, m_allocation_size);
			}

			return static_cast<T*>(m_ptr);
		}

		template <Integral T = usz>
		T size() const
		{
			return static_cast<T>(m_size);
		}

		template<typename T>
		std::span<T> as_span() const
		{
			auto bytes = data();
			ensure(is_naturally_aligned<T>(), "IO buffer span cast requires naturally aligned pointers.");
			return { utils::bless<T>(bytes), m_size / sizeof(T) };
		}

		template<typename T>
		bool is_naturally_aligned() const
		{
			return ((reinterpret_cast<uintptr_t>(data()) & (alignof(T) - 1)) == 0) &&
				(m_size % sizeof(T)) == 0;
		}

		bool empty() const
		{
			return m_size == 0;
		}
	};
}
