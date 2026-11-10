#pragma once

#include "util/types.hpp"
#include "util/atomic.hpp"
#include "Utilities/lockless.h"
#include "Utilities/Thread.h"

#include <functional>
#include <memory>

namespace np
{
	// libretro-common's net_http keeps its connection pool and DNS cache for
	// the one thread that drives transfers (net/net_http.h). Every HTTP
	// transfer in the core - UPnP's, the clans server's - runs on this
	// thread: a caller hands its work over a lock-free queue and waits on a
	// completion word of its own, and the thread waits on the queue. Nothing
	// takes a lock, and no caller ever waits on another's transfer except
	// as it waits its turn in the queue.
	class net_http_thread
	{
	public:
		net_http_thread();
		~net_http_thread();

		net_http_thread(const net_http_thread&) = delete;
		net_http_thread& operator=(const net_http_thread&) = delete;

		// Runs @work on the thread and returns once it has run; false,
		// without running it, once the thread is stopping. Its owner stops
		// it after the last user (the emulator's threads are gone by then).
		bool run(std::function<void()> work);

	private:
		struct job
		{
			std::function<void()> work;
			atomic_t<u32>* done; // 1: ran, 2: dropped
		};

		lf_queue<job> m_queue;
		atomic_t<bool> m_stopping = false;
		std::unique_ptr<named_thread<std::function<void()>>> m_thread;
	};
}
