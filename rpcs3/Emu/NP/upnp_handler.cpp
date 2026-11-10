#include "stdafx.h"
#include "upnp_handler.h"
#include "util/logs.hpp"

#include <net/net_http.h>

LOG_CHANNEL(upnp_log, "UPNP");

// How long a request to the gateway may take
static constexpr int upnp_timeout_ms = 2000;

upnp_handler::~upnp_handler()
{
	std::lock_guard lock(m_mutex);

	for (const auto& [protocol, prot_bindings] : m_bindings)
	{
		for (const auto& [internal_port, external_port] : prot_bindings)
		{
			remove_port_redir_external(external_port, protocol);
		}
	}

	m_active = false;

	// The connections and DNS entries net_http keeps
	net_http_deinit();
}

void upnp_handler::upnp_enable()
{
	std::lock_guard lock(m_mutex);

	m_cfg.load();

	net_http_init();

	const std::string dev_url = m_cfg.get_device_url();

	if (!dev_url.empty())
	{
		if (net_upnp_gateway_from_url(&m_gateway, dev_url.c_str(), upnp_timeout_ms))
		{
			upnp_log.notice("Saved UPNP(%s) enabled", dev_url);
			m_active = true;
			return;
		}

		upnp_log.error("Saved UPNP(%s) isn't available anymore", dev_url);
	}

	upnp_log.notice("Starting UPNP search");

	if (!net_upnp_discover(&m_gateway, upnp_timeout_ms))
	{
		upnp_log.error("No UPNP IGD device was found");
		return;
	}

	upnp_log.notice("Found UPnP device type:%s at %s", m_gateway.service_type, m_gateway.desc_url);

	m_cfg.set_device_url(m_gateway.desc_url);
	m_cfg.save();

	m_active = true;
}

void upnp_handler::add_port_redir(const std::string& addr, u16 internal_port, std::string_view protocol)
{
	if (!m_active)
		return;

	const std::string protocol_str(protocol);
	const bool udp = protocol_str == "UDP";

	std::lock_guard lock(m_mutex);

	if (m_bindings[protocol_str].contains(internal_port))
		return;

	const u32 max_port = std::min(static_cast<u32>(internal_port) + 100, 0xFFFFu);

	// The mapping goes to this host's address on the route to the gateway,
	// which is the address the caller found for it
	for (u32 external_port = internal_port; external_port <= max_port; external_port++)
	{
		if (net_upnp_add_port_mapping_ext(&m_gateway, external_port, internal_port, udp, "RPCS3", upnp_timeout_ms))
		{
			m_bindings[protocol_str][static_cast<u16>(internal_port)] = static_cast<u16>(external_port);
			upnp_log.notice("Successfully bound %s:%d(%s) to IGD:%d", addr, internal_port, protocol, external_port);
			return;
		}

		// need more testing, may vary per router, etc, for now assume port conflict silently
	}

	upnp_log.error("Failed to bind %s:%d(%s) to IGD:(%d=>%d)", addr, internal_port, protocol, internal_port, max_port);
}

void upnp_handler::remove_port_redir(u16 internal_port, std::string_view protocol)
{
	if (!m_active)
		return;

	std::lock_guard lock(m_mutex);

	const std::string protocol_str(protocol);

	if (!m_bindings.contains(protocol_str) || !::at32(m_bindings, protocol_str).contains(internal_port))
	{
		upnp_log.error("tried to unbind port mapping %d to IGD(%s) but it isn't bound", internal_port, protocol);
		return;
	}

	const u16 external_port = ::at32(::at32(m_bindings, protocol_str), internal_port);

	remove_port_redir_external(external_port, protocol);

	ensure(::at32(m_bindings, protocol_str).erase(internal_port));
	upnp_log.notice("Successfully deleted port mapping %d to IGD:%d(%s)", internal_port, external_port, protocol);
}

void upnp_handler::remove_port_redir_external(u16 external_port, std::string_view protocol, bool verbose)
{
	if (!net_upnp_delete_port_mapping(&m_gateway, external_port, protocol == "UDP", upnp_timeout_ms) && verbose)
		upnp_log.error("Failed to delete port mapping IGD:%d(%s)", external_port, protocol);
}

bool upnp_handler::is_active() const
{
	return m_active;
}
