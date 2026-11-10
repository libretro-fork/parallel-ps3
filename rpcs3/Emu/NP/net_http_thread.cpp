#include "stdafx.h"
#include "net_http_thread.h"
#include "util/logs.hpp"

#include <net/net_http.h>

LOG_CHANNEL(net_tls_log, "TLS");

// The hooks libretro-common's TLS client reports certificate checks
// through (net/net_socket_ssl.h), as RetroArch's network/tls_log.c does
extern "C" void ssl_socket_log_verify_fail(int mode_required, const char* domain, const char* verify_info)
{
	if (mode_required)
		net_tls_log.error("Certificate of %s refused: %s", domain ? domain : "?", verify_info ? verify_info : "");
	else
		net_tls_log.warning("Certificate of %s not verified: %s", domain ? domain : "?", verify_info ? verify_info : "");
}

extern "C" void ssl_socket_log_verify_disabled(const char* domain)
{
	net_tls_log.trace("Certificate of %s not checked", domain ? domain : "?");
}

namespace np
{
	net_http_thread::net_http_thread()
	{
		m_thread = std::make_unique<named_thread<std::function<void()>>>("net_http", [this]()
		{
			net_http_init();

			while (thread_ctrl::state() != thread_state::aborting)
			{
				for (auto&& j : m_queue.pop_all())
				{
					j.work();
					*j.done = 1;
					j.done->notify_one();
				}

				thread_ctrl::wait_on(m_queue);
			}

			// The pooled connections and the DNS cache, on the thread they
			// belong to
			net_http_deinit();
		});
	}

	net_http_thread::~net_http_thread()
	{
		m_stopping = true;

		auto& thread = *m_thread;
		thread = thread_state::aborting;
		m_queue.notify(true);
		thread();
		m_thread.reset();

		// Whatever came in as it stopped
		for (auto&& j : m_queue.pop_all())
		{
			*j.done = 2;
			j.done->notify_one();
		}
	}

	bool net_http_thread::run(std::function<void()> work)
	{
		if (m_stopping)
			return false;

		atomic_t<u32> done = 0;
		m_queue.push(job{std::move(work), &done});

		while (!done)
		{
			done.wait(0);
		}

		return done == 1;
	}
}
