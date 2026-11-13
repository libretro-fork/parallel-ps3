#include <util/types.hpp>

#include <functional>
#include <utility>

namespace vk
{
	// Something to be deleted once the GPU is done with it: a pointer and the
	// function that deletes it (a plain function - nothing to allocate or copy)
	class disposable_t
	{
		using deleter_t = void (*)(void*);

		void* ptr;
		deleter_t deleter;

		disposable_t(void* ptr_, deleter_t deleter_) :
			ptr(ptr_), deleter(deleter_) {}
	public:

		disposable_t() = delete;
		disposable_t(const disposable_t&) = delete;

		disposable_t(disposable_t&& other) noexcept :
			ptr(std::exchange(other.ptr, nullptr)),
			deleter(other.deleter)
		{}

		~disposable_t()
		{
			if (ptr)
			{
				deleter(ptr);
				ptr = nullptr;
			}
		}

		// Hands the object over: this no longer deletes it, the taker does
		// by calling the function on the pointer
		void* take(deleter_t& deleter_out)
		{
			deleter_out = deleter;
			return std::exchange(ptr, nullptr);
		}

		template <typename T>
		static disposable_t make(T* raw)
		{
			return disposable_t(raw, [](void* raw)
			{
				delete static_cast<T*>(raw);
			});
		}
	};

	struct garbage_collector
	{
		virtual void dispose(vk::disposable_t& object) = 0;

		virtual void add_exit_callback(std::function<void()> callback) = 0;

		template<typename T>
		void dispose(std::unique_ptr<T>& object)
		{
			auto ptr = vk::disposable_t::make(object.release());
			dispose(ptr);
		}
	};

	garbage_collector* get_gc();
}
