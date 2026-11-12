#pragma once

#include <queues/retro_deque.h>
#include <vector>
#include <optional>

#include "util/types.hpp"
#include "Utilities/mutex.h"

namespace np
{
	// A made-up DNS reply waiting to be read on @sock. Plain data: its
	// bytes are a block of its own, freed by whoever takes the entry out.
	struct dns_reply
	{
		s32 sock;
		u32 size;
		u8* bytes;
	};

	RETRO_DEQUE_DEFINE(dns_replies, dns_reply);
	RETRO_DEQUE_DEFINE(dns_socks, s32);

	class dnshook
	{
	public:
		dnshook();
		~dnshook();
		dnshook(const dnshook&) = delete;
		dnshook& operator=(const dnshook&) = delete;

		void add_dns_spy(u32 sock);
		void remove_dns_spy(u32 sock);

		bool is_dns(u32 sock);
		bool is_dns_queue(u32 sock);

		// The oldest reply waiting on @sock (there is one: is_dns_queue), copied to @out; its size
		u32 take_dns_packet(u32 sock, u8* out, u32 capacity);
		s32 analyze_dns_packet(s32 s, const u8* buf, u32 len);

	private:
		std::optional<u32> get_redir(const std::string& hostname);

	private:
		shared_mutex mutex;
		// The sockets watched, and the replies waiting on them in the order made
		dns_socks_t m_dns_spylist{};
		dns_replies_t m_dns_replies{};

		usz find_spy(s32 sock) const;      // its place in m_dns_spylist, or the count
		usz find_reply(s32 sock) const;    // the first reply waiting on it, or the count
		std::vector<std::pair<std::string, u32>> m_redirs;
	};
} // namespace np
