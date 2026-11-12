#include "stdafx.h"
#include "Emu/NP/np_dnshook.h"

#include "Emu/system_config.h"
#include "Utilities/StrUtil.h"
#include "util/logs.hpp"

#include <regex>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <sys/socket.h>
#include <arpa/inet.h>
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
#endif

LOG_CHANNEL(dnshook_log, "DnsHook");

namespace np
{
	dnshook::dnshook()
	{
		// Init switch map for dns
		const std::string swap_list = g_cfg.net.swap_list.to_string();
		const auto swaps = fmt::split_sv(swap_list, {"&&"});
		for (usz i = 0; i < swaps.size(); i++)
		{
			auto host_and_ip = fmt::split(swaps[i], {"="});
			if (host_and_ip.size() != 2)
			{
				dnshook_log.error("Pattern <%s> contains none or more than one '='", swaps[i]);
				continue;
			}

			in_addr conv;
			if (!inet_pton(AF_INET, host_and_ip[1].c_str(), &conv))
			{
				dnshook_log.error("IP(%s) provided for %s in the switch list is invalid!", host_and_ip[1], host_and_ip[0]);
			}
			else
			{
				m_redirs.push_back({std::move(host_and_ip[0]), conv.s_addr});
			}
		}
	}

	dnshook::~dnshook()
	{
		while (!dns_replies_empty(&m_dns_replies))
		{
			std::free(dns_replies_front(&m_dns_replies)->bytes);
			dns_replies_pop_front(&m_dns_replies);
		}

		dns_replies_free(&m_dns_replies);
		dns_socks_free(&m_dns_spylist);
	}

	usz dnshook::find_spy(s32 sock) const
	{
		const usz count = dns_socks_count(&m_dns_spylist);

		for (usz i = 0; i < count; i++)
		{
			if (*dns_socks_at(&m_dns_spylist, i) == sock)
			{
				return i;
			}
		}

		return count;
	}

	usz dnshook::find_reply(s32 sock) const
	{
		const usz count = dns_replies_count(&m_dns_replies);

		for (usz i = 0; i < count; i++)
		{
			if (dns_replies_at(&m_dns_replies, i)->sock == sock)
			{
				return i;
			}
		}

		return count;
	}

	void dnshook::add_dns_spy(u32 sock)
	{
		std::lock_guard lock(mutex);
		const s32 s = static_cast<s32>(sock);

		if (find_spy(s) == dns_socks_count(&m_dns_spylist))
		{
			ensure(dns_socks_push_back_growing(&m_dns_spylist, &s));
		}
	}

	void dnshook::remove_dns_spy(u32 sock)
	{
		std::lock_guard lock(mutex);
		const s32 s = static_cast<s32>(sock);

		if (const usz i = find_spy(s); i != dns_socks_count(&m_dns_spylist))
		{
			dns_socks_erase(&m_dns_spylist, i);
		}

		// And the replies nobody read
		for (usz i = find_reply(s); i != dns_replies_count(&m_dns_replies); i = find_reply(s))
		{
			std::free(dns_replies_at(&m_dns_replies, i)->bytes);
			dns_replies_erase(&m_dns_replies, i);
		}
	}

	bool dnshook::is_dns(u32 sock)
	{
		std::lock_guard lock(mutex);
		return find_spy(static_cast<s32>(sock)) != dns_socks_count(&m_dns_spylist);
	}

	bool dnshook::is_dns_queue(u32 sock)
	{
		std::lock_guard lock(mutex);
		return find_reply(static_cast<s32>(sock)) != dns_replies_count(&m_dns_replies);
	}

	u32 dnshook::take_dns_packet(u32 sock, u8* out, u32 capacity)
	{
		std::lock_guard lock(mutex);
		const usz i = find_reply(static_cast<s32>(sock));
		ensure(i != dns_replies_count(&m_dns_replies));

		const dns_reply reply = *dns_replies_at(&m_dns_replies, i);
		ensure(reply.size <= capacity);
		dns_replies_erase(&m_dns_replies, i);

		// Copied once, into the caller's buffer
		std::memcpy(out, reply.bytes, reply.size);
		std::free(reply.bytes);
		return reply.size;
	}

	std::optional<u32> dnshook::get_redir(const std::string& hostname)
	{
		for (const auto& [pattern, ip] : m_redirs)
		{
			const std::regex regex_pattern(fmt::replace_all(pattern, {{".", "\\."}, {"*", ".*"}}), std::regex_constants::icase);

			if (std::regex_match(hostname, regex_pattern))
			{
				dnshook_log.trace("Matched '%s' to pattern '%s'", hostname, pattern);
				return ip;
			}
		}

		return std::nullopt;
	}

	s32 dnshook::analyze_dns_packet(s32 s, const u8* buf, u32 len)
	{
		std::lock_guard lock(mutex);

		dnshook_log.trace("DNS REQUEST:\n%s", fmt::buf_to_hexstring(buf, len));

		struct dns_header
		{
			u16 id; // identification number

			u8 rd : 1;     // recursion desired
			u8 tc : 1;     // truncated message
			u8 aa : 1;     // authoritive answer
			u8 opcode : 4; // purpose of message
			u8 qr : 1;     // query/response flag

			u8 rcode : 4; // response code
			u8 cd : 1;    // checking disabled
			u8 ad : 1;    // authenticated data
			u8 z : 1;     // its z! reserved
			u8 ra : 1;    // recursion available

			be_t<u16> q_count;    // number of question entries
			be_t<u16> ans_count;  // number of answer entries
			be_t<u16> auth_count; // number of authority entries
			be_t<u16> add_count;  // number of resource entries
		};

		if (len < sizeof(dns_header))
			return -1;

		const dns_header* dhead = reinterpret_cast<const dns_header*>(buf);
		// We are only looking for queries not truncated(todo?), only handle one dns query at a time(todo?)
		if (dhead->qr != 0 || dhead->tc != 0 || dhead->q_count != 1 || dhead->ans_count != 0 || dhead->auth_count != 0 || dhead->add_count != 0)
			return -1;

		// Get the actual address
		u8 count = 0;
		std::string host{};
		for (u32 i = sizeof(dns_header); (i < len) && buf[i] != 0; i++)
		{
			if (count == 0)
			{
				count = buf[i];
				if (i != sizeof(dns_header))
				{
					host += '.';
				}
			}
			else
			{
				host += static_cast<char>(buf[i]);
				count--;
			}
		}

		dnshook_log.warning("DNS query for %s", host);

		if (auto ip = get_redir(host))
		{
			// design fake packet
			// The query, and one answer after it: written once, into the
			// block the reply waits in
			static constexpr u8 answer[12] =
			{
				0xC0, 0x0C,             // Ref to name in header
				0x00, 0x01,             // IPv4
				0x00, 0x01,             // Class?
				0x00, 0x00, 0x00, 0x3B, // TTL
				0x00, 0x04,             // Size of data
			};

			const dns_reply reply{s, len + ::size32(answer) + 4, static_cast<u8*>(std::malloc(len + sizeof(answer) + 4))};
			ensure(reply.bytes);
			memcpy(reply.bytes, buf, len);
			dns_header* fake_header = reinterpret_cast<dns_header*>(reply.bytes);
			fake_header->qr         = 1;
			fake_header->ra         = 1;
			fake_header->ans_count  = 1;
			memcpy(reply.bytes + len, answer, sizeof(answer));
			u8* ptr_ip = reinterpret_cast<u8*>(&(*ip));
			memcpy(reply.bytes + len + sizeof(answer), ptr_ip, 4); // IP

			dnshook_log.warning("Solving %s to %d.%d.%d.%d", host, ptr_ip[0], ptr_ip[1], ptr_ip[2], ptr_ip[3]);

			// As the map did: a socket answered is a socket watched
			if (find_spy(s) == dns_socks_count(&m_dns_spylist))
			{
				ensure(dns_socks_push_back_growing(&m_dns_spylist, &s));
			}

			ensure(dns_replies_push_back_growing(&m_dns_replies, &reply));
			return len;
		}
		return -1;
	}
} // namespace np
