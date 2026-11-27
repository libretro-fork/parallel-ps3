#include "stdafx.h"
#include "lv2_socket.h"
#include "network_context.h"

LOG_CHANNEL(sys_net);

lv2_socket::lv2_socket(lv2_socket_family family, lv2_socket_type type, lv2_ip_protocol protocol)
{
	this->family   = family;
	this->type     = type;
	this->protocol = protocol;
}

lv2_socket_family lv2_socket::get_family() const
{
	return family;
}

lv2_socket_type lv2_socket::get_type() const
{
	return type;
}
lv2_ip_protocol lv2_socket::get_protocol() const
{
	return protocol;
}
std::size_t lv2_socket::get_queue_size() const
{
	return queue.size();
}
socket_type lv2_socket::get_socket() const
{
	return native_socket;
}

#ifdef _WIN32
bool lv2_socket::is_connecting() const
{
	return connecting;
}
void lv2_socket::set_connecting(bool connecting)
{
	this->connecting = connecting;
}
#endif

void lv2_socket::set_lv2_id(u32 id)
{
	lv2_id = id;
}

bs_t<lv2_socket::poll_t> lv2_socket::get_events() const
{
	return events.load();
}

void lv2_socket::set_poll_event(bs_t<lv2_socket::poll_t> event)
{
	events += event;
}

void lv2_socket::poll_queue(shared_ptr<ppu_thread> ppu, bs_t<lv2_socket::poll_t> event, std::function<bool(bs_t<lv2_socket::poll_t>)> poll_cb)
{
	set_poll_event(event);
	queue.emplace_back(std::move(ppu), poll_cb);

	// On the network thread, which makes its poll set after this: the
	// socket is polled from its next round
}

u32 lv2_socket::clear_queue(ppu_thread* ppu)
{

	u32 cleared = 0;

	for (auto it = queue.begin(); it != queue.end();)
	{
		if (it->first.get() == ppu)
		{
			it = queue.erase(it);
			cleared++;
			continue;
		}

		it++;
	}

	if (queue.empty())
	{
		events.store({});
	}

	return cleared;
}

void lv2_socket::handle_events(const pollfd& native_pfd, [[maybe_unused]] bool unset_connecting)
{
	bs_t<lv2_socket::poll_t> events_happening{};

	if (native_pfd.revents & (POLLIN | POLLHUP) && events.test_and_reset(lv2_socket::poll_t::read))
		events_happening += lv2_socket::poll_t::read;
	if (native_pfd.revents & POLLOUT && events.test_and_reset(lv2_socket::poll_t::write))
		events_happening += lv2_socket::poll_t::write;
	if (native_pfd.revents & POLLERR && events.test_and_reset(lv2_socket::poll_t::error))
		events_happening += lv2_socket::poll_t::error;

	// A waiter's timeout is its own (lv2_obj::wait_timeout): looked at here
	// only for the events
	if (events_happening)
	{
#ifdef _WIN32
		if (unset_connecting)
			set_connecting(false);
#endif
		for (auto it = queue.begin(); it != queue.end();)
		{
			if (it->second(events_happening))
			{
				it = queue.erase(it);
				continue;
			}

			it++;
		}

		if (queue.empty())
		{
			events.store({});
		}
	}
}

void lv2_socket::queue_wake(ppu_thread* ppu)
{
	switch (type)
	{
	case SYS_NET_SOCK_STREAM:
	case SYS_NET_SOCK_DGRAM:
	case SYS_NET_SOCK_DGRAM_P2P:
	case SYS_NET_SOCK_STREAM_P2P:
		// The network thread's, which polls all of them
		g_fxo->get<network_context>().add_ppu_to_awake(ppu);
		break;
	default:
		break;
	}
}

lv2_socket& lv2_socket::operator=(thread_state s) noexcept
{
	if (s == thread_state::destroying_context)
	{
		close();
	}

	return *this;
}

lv2_socket::~lv2_socket() noexcept
{
}
