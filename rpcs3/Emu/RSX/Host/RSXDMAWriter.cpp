#include "stdafx.h"
#include "RSXDMAWriter.h"

#include "Utilities//Thread.h"
#include <util/asm.hpp>

namespace rsx
{
	void RSXDMAWriter::update()
	{
		if (m_dispatch_handlers.empty())
		{
			host_gpu_write_ops_clear(&m_job_queue);
			return;
		}

		while (!host_gpu_write_ops_empty(&m_job_queue))
		{
			const auto job = *host_gpu_write_ops_front(&m_job_queue);

			if (const auto dispatch = m_dispatch_handlers.find(job.dispatch_class);
				dispatch == m_dispatch_handlers.end() || dispatch->second.handler(m_host_context_ptr, &job))
			{
				// No handler registered, or callback consumed the job
				host_gpu_write_ops_pop_front(&m_job_queue);
				continue;
			}

			// Dispatcher found and rejected the job. Stop, we'll try again later.
			break;
		}
	}

	void RSXDMAWriter::register_handler(host_dispatch_handler_t handler)
	{
		m_dispatch_handlers[handler.dispatch_class] = handler;
	}

	void RSXDMAWriter::deregister_handler(int dispatch_class)
	{
		m_dispatch_handlers.erase(dispatch_class);
	}

	void RSXDMAWriter::enqueue(const host_gpu_write_op_t& request)
	{
		ensure(host_gpu_write_ops_push_back_growing(&m_job_queue, &request));
	}

	void RSXDMAWriter::drain_label_queue()
	{
		if (!m_host_context_ptr)
		{
			return;
		}

		// FIXME: This is a busy wait, consider yield to improve responsiveness on weak devices.
		while (!m_host_context_ptr->in_flight_commands_completed())
		{
			utils::pause();

			if (thread_ctrl::state() == thread_state::aborting)
			{
				break;
			}
		}
	}
}
