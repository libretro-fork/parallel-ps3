#pragma once

#include "lv2_socket.h"

#include <cstdlib>
#include <cstring>
#include <queues/retro_deque.h>

// A packet received for a P2P datagram socket, waiting to be read: plain
// data, its bytes a block of its own that whoever takes the packet out of
// the queue frees (p2p_packets_drop_front)
struct p2p_packet
{
	sys_net_sockaddr_in_p2p addr;
	u8* bytes;
	u32 size;
};

RETRO_DEQUE_DEFINE(p2p_packets, p2p_packet);

// Queues a copy of @size bytes from @addr
inline void p2p_packets_add(p2p_packets_t& packets, const sys_net_sockaddr_in_p2p& addr, const u8* bytes, u32 size)
{
	p2p_packet packet{addr, static_cast<u8*>(std::malloc(size ? size : 1)), size};
	ensure(packet.bytes);
	std::memcpy(packet.bytes, bytes, size);
	ensure(p2p_packets_push_back_growing(&packets, &packet));
}

inline void p2p_packets_drop_front(p2p_packets_t& packets)
{
	std::free(p2p_packets_front(&packets)->bytes);
	p2p_packets_pop_front(&packets);
}

inline void p2p_packets_end(p2p_packets_t& packets)
{
	while (!p2p_packets_empty(&packets))
	{
		p2p_packets_drop_front(packets);
	}

	p2p_packets_free(&packets);
}

// In a savestate as the std::deque<std::pair<sys_net_sockaddr_in_p2p,
// std::vector<u8>>> they were: how many, then for each its address, its
// size and its bytes. Saving leaves the queue as it is.
inline void p2p_packets_save(utils::serial& ar, const p2p_packets_t& packets)
{
	const usz count = p2p_packets_count(&packets);
	ar.serialize_vle(count);

	for (usz i = 0; i < count; i++)
	{
		const p2p_packet& packet = *p2p_packets_at(&packets, i);
		ar(packet.addr);
		ar.serialize_vle(usz{packet.size});

		if (packet.size)
		{
			ar.raw_serialize(packet.bytes, packet.size);
		}
	}
}

inline void p2p_packets_load(utils::serial& ar, p2p_packets_t& packets)
{
	usz count = 0;

	if (!ar.deserialize_vle<28>(count))
	{
		return;
	}

	for (usz i = 0; i < count && ar.is_valid(); i++)
	{
		p2p_packet packet{};
		usz size = 0;
		ar(packet.addr);

		if (!ar.deserialize_vle<28>(size))
		{
			return;
		}

		packet.size = static_cast<u32>(size);
		packet.bytes = static_cast<u8*>(std::malloc(size ? size : 1));
		ensure(packet.bytes);

		if (size)
		{
			ar.raw_serialize(packet.bytes, size);
		}

		ensure(p2p_packets_push_back_growing(&packets, &packet));
	}
}

class lv2_socket_p2p : public lv2_socket
{
public:
	lv2_socket_p2p(lv2_socket_family family, lv2_socket_type type, lv2_ip_protocol protocol);
	lv2_socket_p2p(utils::serial& ar, lv2_socket_type type);
	~lv2_socket_p2p() noexcept override;
	void save(utils::serial& ar);

	std::tuple<bool, s32, shared_ptr<lv2_socket>, sys_net_sockaddr> accept(bool is_lock = true) override;
	s32 bind(const sys_net_sockaddr& addr) override;

	std::optional<s32> connect(const sys_net_sockaddr& addr) override;
	s32 connect_followup() override;

	std::pair<s32, sys_net_sockaddr> getpeername() override;
	std::pair<s32, sys_net_sockaddr> getsockname() override;

	std::tuple<s32, sockopt_data, u32> getsockopt(s32 level, s32 optname, u32 len) override;
	s32 setsockopt(s32 level, s32 optname, const std::vector<u8>& optval) override;

	s32 listen(s32 backlog) override;

	std::optional<std::tuple<s32, std::vector<u8>, sys_net_sockaddr>> recvfrom(s32 flags, u32 len, bool is_lock = true) override;
	std::optional<s32> sendto(s32 flags, const std::vector<u8>& buf, std::optional<sys_net_sockaddr> opt_sn_addr, bool is_lock = true) override;
	std::optional<s32> sendmsg(s32 flags, const sys_net_msghdr& msg, bool is_lock = true) override;

	void close() override;
	s32 shutdown(s32 how) override;

	void poll(sys_net_pollfd& sn_pfd, pollfd& native_pfd) override;
	std::tuple<bool, bool, bool> select(bs_t<poll_t> selected, pollfd& native_pfd) override;
	void get_sockinfo(sys_net_sockinfo_t& info) override;

	void handle_new_data(const sys_net_sockaddr_in_p2p& p2p_addr, const u8* p2p_data, u32 p2p_size);

protected:
	virtual bs_t<poll_t> get_pending_events() const;

	// Port(actual bound port) and Virtual Port(indicated by u16 at the start of the packet)
	u16 port = 3658, vport = 0;
	u32 bound_addr = 0;
	// Queue containing received packets from network_thread for SYS_NET_SOCK_DGRAM_P2P sockets
	p2p_packets_t data{};
	// List of sock options
	std::map<u64, sockopt_cache> sockopts;
};
