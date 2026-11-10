#pragma once

#include <unordered_map>

#include <net/net_upnp.h>

#include "upnp_config.h"
#include "net_http_thread.h"
#include "Utilities/mutex.h"

class upnp_handler
{
public:
	// Talks to the gateway on @http, the thread every HTTP transfer runs on
	explicit upnp_handler(np::net_http_thread& http) : m_http(http) {}
	~upnp_handler();

	void upnp_enable();
	void add_port_redir(const std::string& addr, u16 internal_port, std::string_view protocol);
	void remove_port_redir(u16 internal_port, std::string_view protocol);

	bool is_active() const;

private:
	void remove_port_redir_external(u16 external_port, std::string_view protocol, bool verbose = true);

private:
	np::net_http_thread& m_http;
	atomic_t<bool> m_active = false;

	shared_mutex m_mutex;
	cfg_upnp m_cfg;
	net_upnp_gateway m_gateway{};
	std::unordered_map<std::string, std::unordered_map<u16, u16>> m_bindings;
};
