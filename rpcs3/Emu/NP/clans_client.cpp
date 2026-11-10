#include "stdafx.h"

#include <chrono>
#include <string_view>
#include <util/types.hpp>

#include <encodings/base64.h>
#include <net/net_http.h>
#include <net/net_socket_ssl.h>

#include <Utilities/StrUtil.h>
#include <Utilities/StrFmt.h>

#include "Emu/Cell/Modules/sceNp.h"
#include "Emu/Cell/Modules/sceNpClans.h"
#include "Emu/NP/clans_client.h"
#include "Emu/NP/clans_config.h"
#include "Emu/NP/np_helpers.h"
#include "Emu/system_config.h"

LOG_CHANNEL(clan_log, "clans");

const char* REQ_TYPE_FUNC = "func";
const char* REQ_TYPE_SEC = "sec";

constexpr const char JID_FORMAT[] = "%s@un.br.np.playstation.net";

template <>
void fmt_class_string<clan::ClanRequestType>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto value)
		{
			switch (value)
			{
			case clan::ClanRequestType::FUNC: return "func";
			case clan::ClanRequestType::SEC:  return "sec";
			}

			return unknown;
		});
}

template <>
void fmt_class_string<clan::ClanManagerOperationType>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto value)
		{
			switch (value)
			{
			case clan::ClanManagerOperationType::VIEW:   return "view";
			case clan::ClanManagerOperationType::UPDATE: return "update";
			}

			return unknown;
		});
}

template <>
void fmt_class_string<clan::ClanSearchFilterOperator>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto value)
		{
			switch (value)
			{
			case clan::ClanSearchFilterOperator::Equal:              return "eq";
			case clan::ClanSearchFilterOperator::NotEqual:           return "ne";
			case clan::ClanSearchFilterOperator::GreaterThan:        return "gt";
			case clan::ClanSearchFilterOperator::GreaterThanOrEqual: return "ge";
			case clan::ClanSearchFilterOperator::LessThan:           return "lt";
			case clan::ClanSearchFilterOperator::LessThanOrEqual:    return "le";
			case clan::ClanSearchFilterOperator::Like:               return "lk";
			}

			return unknown;
		});
}

template <>
void fmt_class_string<clan::ClanRequestAction>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto value)
		{
			switch (value)
			{
			case clan::ClanRequestAction::GetClanList:              return "get_clan_list";
			case clan::ClanRequestAction::GetClanInfo:              return "get_clan_info";
			case clan::ClanRequestAction::GetMemberInfo:            return "get_member_info";
			case clan::ClanRequestAction::GetMemberList:            return "get_member_list";
			case clan::ClanRequestAction::GetBlacklist:             return "get_blacklist";
			case clan::ClanRequestAction::RecordBlacklistEntry:     return "record_blacklist_entry";
			case clan::ClanRequestAction::DeleteBlacklistEntry:     return "delete_blacklist_entry";
			case clan::ClanRequestAction::ClanSearch:               return "clan_search";
			case clan::ClanRequestAction::CreateClan:               return "create_clan";
			case clan::ClanRequestAction::DisbandClan:              return "disband_clan";
			case clan::ClanRequestAction::RequestMembership:        return "request_membership";
			case clan::ClanRequestAction::CancelRequestMembership:  return "cancel_request_membership";
			case clan::ClanRequestAction::AcceptMembershipRequest:  return "accept_membership_request";
			case clan::ClanRequestAction::DeclineMembershipRequest: return "decline_membership_request";
			case clan::ClanRequestAction::SendInvitation:           return "send_invitation";
			case clan::ClanRequestAction::CancelInvitation:         return "cancel_invitation";
			case clan::ClanRequestAction::AcceptInvitation:         return "accept_invitation";
			case clan::ClanRequestAction::DeclineInvitation:        return "decline_invitation";
			case clan::ClanRequestAction::UpdateMemberInfo:         return "update_member_info";
			case clan::ClanRequestAction::UpdateClanInfo:           return "update_clan_info";
			case clan::ClanRequestAction::JoinClan:                 return "join_clan";
			case clan::ClanRequestAction::LeaveClan:                return "leave_clan";
			case clan::ClanRequestAction::KickMember:               return "kick_member";
			case clan::ClanRequestAction::ChangeMemberRole:         return "change_member_role";
			case clan::ClanRequestAction::RetrieveAnnouncements:    return "retrieve_announcements";
			case clan::ClanRequestAction::PostAnnouncement:         return "post_announcement";
			case clan::ClanRequestAction::DeleteAnnouncement:       return "delete_announcement";
			}

			return unknown;
		});
}

namespace clan
{
	// The request body, written in document order inside its <clan> element
	struct clan_request
	{
		rxml_writer_t* w = rxml_writer_new(nullptr);
		bool has_ticket = false;

		clan_request()
		{
			rxml_write_open(w, "clan");
		}

		~clan_request()
		{
			rxml_writer_free(w);
		}

		clan_request(const clan_request&) = delete;
		clan_request& operator=(const clan_request&) = delete;

		// <name>text</name>
		void element(const char* name, const std::string& text)
		{
			has_ticket |= std::strcmp(name, "ticket") == 0;
			rxml_write_element(w, name, text.c_str());
		}

		void element(const char* name, const char* text)
		{
			element(name, std::string(text));
		}

		// <name>number</name>, in decimal
		template <typename T> requires (!std::is_convertible_v<const T&, std::string_view>)
		void element(const char* name, const T& value)
		{
			element(name, fmt::format("%d", value));
		}

		// The document, closed; null when the writer failed
		const char* finish(usz* size)
		{
			rxml_write_close(w);
			return rxml_writer_data(w, size);
		}
	};

	// The response, owned until the request is done with
	struct clan_response
	{
		rxml_document_t* doc = nullptr;

		clan_response() = default;
		clan_response(const clan_response&) = delete;
		clan_response& operator=(const clan_response&) = delete;

		~clan_response()
		{
			rxml_free_document(doc);
		}

		// The root element, when it is <clan>
		rxml_node_t* clan() const
		{
			rxml_node_t* const root = rxml_root_node(doc);
			return root && root->name && std::strcmp(root->name, "clan") == 0 ? root : nullptr;
		}
	};

	// Null-safe readers of a parsed response: a missing node reads as empty or 0
	static rxml_node_t* xml_child(const rxml_node_t* node, const char* name)
	{
		for (rxml_node_t* child = node ? node->children : nullptr; child; child = child->next)
		{
			if (child->name && std::strcmp(child->name, name) == 0)
				return child;
		}

		return nullptr;
	}

	static rxml_node_t* xml_next(const rxml_node_t* node, const char* name)
	{
		for (rxml_node_t* next = node ? node->next : nullptr; next; next = next->next)
		{
			if (next->name && std::strcmp(next->name, name) == 0)
				return next;
		}

		return nullptr;
	}

	static const char* xml_attr(rxml_node_t* node, const char* name)
	{
		return node ? rxml_node_attrib(node, name) : nullptr;
	}

	static std::string xml_str(const char* value)
	{
		return value ? value : "";
	}

	static std::string xml_text(const rxml_node_t* node)
	{
		return node && node->data ? node->data : "";
	}

	// Decimal, or hexadecimal after 0x, as pugixml's as_uint read it
	static u32 xml_uint(const char* value)
	{
		if (!value)
			return 0;

		while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
			value++;

		const bool hex = value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
		return static_cast<u32>(std::strtoul(hex ? value + 2 : value, nullptr, hex ? 16 : 10));
	}

	static u32 xml_uint(const rxml_node_t* node)
	{
		return xml_uint(node ? node->data : nullptr);
	}

	// Base64 without line breaks, as the clans server takes it
	static std::string base64_string(const void* data, usz size)
	{
		int len = 0;
		char* const encoded = base64(data, ::narrow<int>(size), &len);

		if (!encoded)
			return {};

		std::string out(encoded, static_cast<usz>(len));
		std::free(encoded);
		return out;
	}

	// How long a request to the clans server may take
	static constexpr s64 clan_timeout_ms = 30000;

	struct clan_request_ctx
	{
		// TODO: this was arbitrarily chosen -- see if there's a real amount
		static constexpr u32 SCE_NP_CLANS_MAX_CTX_NUM = 16;

		static constexpr u32 id_base = 0xA001;
		static constexpr u32 id_step = 1;
		static constexpr u32 id_count = SCE_NP_CLANS_MAX_CTX_NUM;
		SAVESTATE_INIT_POS(55);
	};

	clans_client::clans_client()
	{
		g_cfg_clans.load();
	}

	clans_client::~clans_client()
	{
		idm::clear<clan_request_ctx>();
	}

	SceNpClansError clans_client::create_request(s32& req_id)
	{
		if (!g_cfg.net.clans_enabled)
		{
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;
		}

		const s32 id = idm::make<clan_request_ctx>();

		if (id == id_manager::id_traits<clan_request_ctx>::invalid)
		{
			return SceNpClansError::SCE_NP_CLANS_ERROR_EXCEEDS_MAX;
		}

		const auto ctx = idm::get_unlocked<clan_request_ctx>(id);
		if (!ctx)
		{
			idm::remove<clan_request_ctx>(id);
			return SceNpClansError::SCE_NP_CLANS_ERROR_NOT_INITIALIZED;
		}

		req_id = id;

		return SceNpClansError::SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::destroy_request(u32 req_id)
	{
		if (idm::remove<clan_request_ctx>(req_id))
			return SceNpClansError::SCE_NP_CLANS_SUCCESS;

		return SceNpClansError::SCE_NP_CLANS_ERROR_INVALID_ARGUMENT;
	}

	SceNpClansError clans_client::send_request(u32 req_id, ClanRequestAction action, ClanManagerOperationType op_type, clan_request& xml_body, clan_response& out_response)
	{
		auto ctx = idm::get_unlocked<clan_request_ctx>(req_id);

		if (!ctx)
			return SCE_NP_CLANS_ERROR_NOT_INITIALIZED;

		const ClanRequestType req_type = xml_body.has_ticket ? ClanRequestType::SEC : ClanRequestType::FUNC;

		const std::string host = g_cfg_clans.get_host();
		const bool https = g_cfg_clans.get_use_https();
		const std::string url = fmt::format("%s://%s/clan_manager_%s/%s/%s", https ? "https" : "http", host, op_type, req_type, action);

		usz xml_size = 0;
		const char* const xml = xml_body.finish(&xml_size);

		if (!xml)
		{
			clan_log.error("Failed to write the request");
			return SCE_NP_CLANS_ERROR_INVALID_ARGUMENT;
		}

		// POSTed on the thread every HTTP transfer runs on; the body read
		// where finish() left it
		char* response = nullptr;
		std::string failure;

		const bool ran = g_fxo->get<named_thread<np::np_handler>>().get_http().run([&]()
		{
			http_connection_t* const conn = net_http_connection_new(url.c_str(), "POST", nullptr);

			if (!conn)
			{
				failure = "bad URL";
				return;
			}

			// The form type curl sent a POST body as
			net_http_connection_set_content(conn, "application/x-www-form-urlencoded", xml_size, xml);

			// WARN: no certificate is checked, as before
			ssl_socket_retro_config tls{};
			tls.flags = SSL_RETRO_NO_VERIFY;
			if (https)
				net_http_connection_set_tls(conn, &tls);

			while (!net_http_connection_iterate(conn))
			{
			}

			http_t* const http = net_http_connection_done(conn) ? net_http_new(conn) : nullptr;

			if (!http)
			{
				failure = "could not start the request";
				net_http_connection_free(conn);
				return;
			}

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(clan_timeout_ms);

			while (!net_http_update(http, nullptr, nullptr))
			{
				const auto now = std::chrono::steady_clock::now();

				if (now >= deadline)
				{
					failure = "timed out";
					break;
				}

				const s64 left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1;
				net_http_wait(http, static_cast<int>(std::min<s64>(left, 100)));
			}

			if (failure.empty())
			{
				// A transfer that never got an answer; an HTTP error status
				// is an answer
				int code = 0;

				if (const char* const why = net_http_failure(http, &code))
				{
					failure = fmt::format("%s (%d)", why, code);
				}
				else if (net_http_status(http) <= 0)
				{
					failure = "no answer";
				}
				else
				{
					// Whatever the status, the body is the answer, as curl
					// handed it on
					usz len = 0;
					u8* const data = net_http_data(http, &len, true);

					if (data && (response = static_cast<char*>(std::realloc(data, len + 1))))
						response[len] = '\0';
					else if (data)
						std::free(data);
					else if ((response = static_cast<char*>(std::malloc(1))))
						response[0] = '\0';
				}
			}

			net_http_delete(http);
			net_http_connection_free(conn);
		});

		if (!ran || !response)
		{
			clan_log.error("Request to %s failed: %s", url, ran ? failure : "the network thread is stopping");
			return SCE_NP_CLANS_ERROR_BAD_REQUEST;
		}

		const std::unique_ptr<char, decltype(&std::free)> response_buffer(response, &std::free);

		rxml_parse_error_t xml_err{};
		out_response.doc = rxml_load_document_string_opts(response_buffer.get(), RXML_OPT_STRICT_EOF, &xml_err);
		if (!out_response.doc)
		{
			clan_log.error("XML parsing failed at line %u, column %u", xml_err.line, xml_err.col);
			return SCE_NP_CLANS_ERROR_BAD_RESPONSE;
		}

		rxml_node_t* const clan_result = out_response.clan();
		if (!clan_result)
			return SCE_NP_CLANS_ERROR_BAD_RESPONSE;

		const char* const result = xml_attr(clan_result, "result");
		if (!result)
			return SCE_NP_CLANS_ERROR_BAD_RESPONSE;

		const std::string result_str = result;
		if (result_str != "00")
			return static_cast<SceNpClansError>(0x80022800 | std::stoul(result_str, nullptr, 16));

		return SCE_NP_CLANS_SUCCESS;
	}

	std::string clans_client::get_clan_ticket(np::np_handler& nph)
	{
		// Pretend we failed to get a ticket if the emulator isn't
		// connected to RPCN.
		if (nph.get_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE)
			return "";

		const auto& npid = nph.get_npid();

		const char* service_id = CLANS_SERVICE_ID;
		const unsigned char* cookie = nullptr;
		const u32 cookie_size = 0;
		const char* entitlement_id = CLANS_ENTITLEMENT_ID;
		const u32 consumed_count = 0;

		// Use the cached ticket if available
		np::ticket clan_ticket;
		if (!nph.get_clan_ticket_ready())
		{
			// If not cached, request a new ticket
			nph.req_ticket(0x00020001, &npid, service_id, cookie, cookie_size, entitlement_id, consumed_count);
		}

		clan_ticket = nph.get_clan_ticket();
		if (clan_ticket.empty())
		{
			clan_log.error("Failed to get clan ticket");
			return "";
		}

		std::string ticket_str = base64_string(clan_ticket.data(), clan_ticket.size());

		if (ticket_str.empty())
		{
			clan_log.error("Failed to encode clan ticket to base64");
			return "";
		}

		return ticket_str;
	}

#pragma region Outgoing API Requests
	SceNpClansError clans_client::get_clan_list(np::np_handler& nph, u32 req_id, const SceNpClansPagingRequest& paging, std::vector<SceNpClansEntry>& clan_list, SceNpClansPagingResult& page_result)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;

		doc.element("ticket", ticket);
		doc.element("start", paging.startPos);
		doc.element("max", paging.max);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::GetClanList, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const list = xml_child(clan_result, "list");

		const char* const results = xml_attr(list, "results");
		const uint32_t results_count = xml_uint(results);

		const char* const total = xml_attr(list, "total");
		const uint32_t total_count = xml_uint(total);

		int i = 0;
		for (rxml_node_t* info = xml_child(list, "info"); info; info = xml_next(info, "info"), i++)
		{
			const char* const id = xml_attr(info, "id");
			const uint32_t clan_id = xml_uint(id);

			rxml_node_t* const name = xml_child(info, "name");
			const std::string name_str = xml_text(name);

			rxml_node_t* const tag = xml_child(info, "tag");
			const std::string tag_str = xml_text(tag);

			rxml_node_t* const role = xml_child(info, "role");
			const int32_t role_int = xml_uint(role);

			rxml_node_t* const status = xml_child(info, "status");
			const uint32_t status_int = xml_uint(status);

			rxml_node_t* const onlinename = xml_child(info, "onlinename");
			const std::string onlinename_str = xml_text(onlinename);

			rxml_node_t* const members = xml_child(info, "members");
			const uint32_t members_int = xml_uint(members);

			SceNpClansEntry entry = SceNpClansEntry{
				.info = SceNpClansClanBasicInfo{
					.clanId = clan_id,
					.numMembers = members_int,
					.name = "",
					.tag = "",
					.reserved = {0, 0},
				},
				.role = static_cast<SceNpClansMemberRole>(role_int),
				.status = static_cast<SceNpClansMemberStatus>(status_int)};

			strcpy_trunc(entry.info.name, name_str);
			strcpy_trunc(entry.info.tag, tag_str);

			::at32(clan_list, i) = std::move(entry);
			i++;
		}

		page_result = SceNpClansPagingResult{
			.count = results_count,
			.total = total_count};

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::get_clan_info(u32 req_id, SceNpClanId clan_id, SceNpClansClanInfo& clan_info)
	{
		clan_request doc;
		doc.element("id", clan_id);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::GetClanInfo, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const info = xml_child(clan_result, "info");

		const std::string name_str = xml_text(xml_child(info, "name"));
		const std::string tag_str = xml_text(xml_child(info, "tag"));
		const uint32_t members_int = xml_uint(xml_child(info, "members"));
		const std::string date_created_str = xml_text(xml_child(info, "date-created"));
		const std::string description_str = xml_text(xml_child(info, "description"));

		clan_info = SceNpClansClanInfo{
			.info = SceNpClansClanBasicInfo{
				.clanId = clan_id,
				.numMembers = members_int,
				.name = "",
				.tag = "",
			},
			.updatable = SceNpClansUpdatableClanInfo{
				.description = "",
			}};

		strcpy_trunc(clan_info.info.name, name_str);
		strcpy_trunc(clan_info.info.tag, tag_str);
		strcpy_trunc(clan_info.updatable.description, description_str);

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::get_member_info(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id, SceNpClansMemberEntry& mem_info)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::GetMemberInfo, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const member_info = xml_child(clan_result, "info");

		const char* const member_jid = xml_attr(member_info, "jid");
		const std::string member_jid_str = xml_str(member_jid);
		const std::string member_username = fmt::split(member_jid_str, {"@"})[0];

		SceNpId member_npid;
		if (strncmp(member_username.c_str(), nph.get_npid().handle.data, 16) == 0)
		{
			member_npid = nph.get_npid();
		}
		else
		{
			np::string_to_npid(member_username, member_npid);
		}

		rxml_node_t* const role = xml_child(member_info, "role");
		const uint32_t role_int = xml_uint(role);

		rxml_node_t* const status = xml_child(member_info, "status");
		const uint32_t status_int = xml_uint(status);

		rxml_node_t* const description = xml_child(member_info, "description");
		const std::string description_str = xml_text(description);

		mem_info = SceNpClansMemberEntry
		{
			.npid = std::move(member_npid),
			.role = static_cast<SceNpClansMemberRole>(role_int),
			.status = static_cast<SceNpClansMemberStatus>(status_int),
			.updatable = SceNpClansUpdatableMemberInfo{
				.description = "",
			}
		};

		strcpy_trunc(mem_info.updatable.description, description_str);

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::get_member_list(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansPagingRequest& paging, SceNpClansMemberStatus /*status*/, std::vector<SceNpClansMemberEntry>& mem_list, SceNpClansPagingResult& page_result)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);
		doc.element("start", paging.startPos);
		doc.element("max", paging.max);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::GetMemberList, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const list = xml_child(clan_result, "list");

		const char* const results = xml_attr(list, "results");
		const uint32_t results_count = xml_uint(results);

		const char* const total = xml_attr(list, "total");
		const uint32_t total_count = xml_uint(total);

		int i = 0;
		for (rxml_node_t* member_info = xml_child(list, "info"); member_info; member_info = xml_next(member_info, "info"))
		{
			const std::string member_jid = xml_str(xml_attr(member_info, "jid"));
			const std::string member_username = fmt::split(member_jid, {"@"})[0];

			SceNpId member_npid;
			if (strncmp(member_username.c_str(), nph.get_npid().handle.data, SCE_NET_NP_ONLINEID_MAX_LENGTH) == 0)
			{
				member_npid = nph.get_npid();
			}
			else
			{
				np::string_to_npid(member_username, member_npid);
			}

			const uint32_t role_int = xml_uint(xml_child(member_info, "role"));
			const uint32_t status_int = xml_uint(xml_child(member_info, "status"));
			const std::string description_str = xml_text(xml_child(member_info, "description"));

			SceNpClansMemberEntry entry = SceNpClansMemberEntry
			{
				.npid = std::move(member_npid),
				.role = static_cast<SceNpClansMemberRole>(role_int),
				.status = static_cast<SceNpClansMemberStatus>(status_int),
			};

			strcpy_trunc(entry.updatable.description, description_str);

			::at32(mem_list, i) = std::move(entry);
			i++;
		}

		page_result = SceNpClansPagingResult
		{
			.count = results_count,
			.total = total_count
		};

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::get_blacklist(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansPagingRequest& paging, std::vector<SceNpClansBlacklistEntry>& bl, SceNpClansPagingResult& page_result)
	{
		const std::string ticket = get_clan_ticket(nph);

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);
		doc.element("start", paging.startPos);
		doc.element("max", paging.max);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::GetBlacklist, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const list = xml_child(clan_result, "list");

		const char* const results = xml_attr(list, "results");
		const uint32_t results_count = xml_uint(results);

		const char* const total = xml_attr(list, "total");
		const uint32_t total_count = xml_uint(total);

		int i = 0;
		for (rxml_node_t* member = xml_child(list, "entry"); member; member = xml_next(member, "entry"))
		{
			rxml_node_t* const member_jid = xml_child(member, "jid");
			const std::string member_jid_str = xml_text(member_jid);
			const std::string member_username = fmt::split(member_jid_str, {"@"})[0];

			SceNpId member_npid = {};
			if (strncmp(member_username.c_str(), nph.get_npid().handle.data, SCE_NET_NP_ONLINEID_MAX_LENGTH) == 0)
			{
				member_npid = nph.get_npid();
			}
			else
			{
				np::string_to_npid(member_username, member_npid);
			}

			SceNpClansBlacklistEntry entry = SceNpClansBlacklistEntry
			{
				.entry = std::move(member_npid),
			};

			::at32(bl, i) = std::move(entry);
			i++;
		}

		page_result = SceNpClansPagingResult
		{
			.count = results_count,
			.total = total_count
		};

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::add_blacklist_entry(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, ClanRequestAction::RecordBlacklistEntry, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::remove_blacklist_entry(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id)
	{
		std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, ClanRequestAction::DeleteBlacklistEntry, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::clan_search(u32 req_id, const SceNpClansPagingRequest& paging, const SceNpClansSearchableName& search, std::vector<SceNpClansClanBasicInfo>& clan_list, SceNpClansPagingResult& page_result)
	{
		clan_request doc;
		doc.element("start", paging.startPos);
		doc.element("max", paging.max);

		const std::string op_name = fmt::format("%s", static_cast<ClanSearchFilterOperator>(static_cast<s32>(search.nameSearchOp)));

		rxml_write_open(doc.w, "filter");
		rxml_write_open(doc.w, "name");
		rxml_write_attrib(doc.w, "op", op_name.c_str());
		rxml_write_attrib(doc.w, "value", search.name);
		rxml_write_close(doc.w);
		rxml_write_close(doc.w);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::ClanSearch, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const list = xml_child(clan_result, "list");

		const char* const results = xml_attr(list, "results");
		const uint32_t results_count = xml_uint(results);

		const char* const total = xml_attr(list, "total");
		const uint32_t total_count = xml_uint(total);

		int i = 0;
		for (rxml_node_t* node = xml_child(list, "info"); node; node = xml_next(node, "info"))
		{
			const uint32_t clan_id = xml_uint(xml_attr(node, "id"));
			const std::string name_str = xml_text(xml_child(node, "name"));
			const std::string tag_str = xml_text(xml_child(node, "tag"));
			const uint32_t members_int = xml_uint(xml_child(node, "members"));

			SceNpClansClanBasicInfo entry = SceNpClansClanBasicInfo
			{
				.clanId = clan_id,
				.numMembers = members_int,
				.name = "",
				.tag = "",
				.reserved = {0, 0},
			};

			strcpy_trunc(entry.name, name_str);
			strcpy_trunc(entry.tag, tag_str);

			::at32(clan_list, i) = std::move(entry);
			i++;
		}

		page_result = SceNpClansPagingResult
		{
			.count = results_count,
			.total = total_count
		};

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::create_clan(np::np_handler& nph, u32 req_id, std::string_view name, std::string_view tag, vm::ptr<SceNpClanId> clan_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);

		doc.element("name", std::string(name));
		doc.element("tag", std::string(tag));

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::CreateClan, ClanManagerOperationType::UPDATE, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		const char* const id = xml_attr(clan_result, "id");
		const uint32_t clan_id_int = xml_uint(id);

		*clan_id = clan_id_int;

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::disband_dlan(np::np_handler& nph, u32 req_id, SceNpClanId clan_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::DisbandClan, ClanManagerOperationType::UPDATE, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::request_membership(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, SceNpClansMessage& /*message*/)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		return send_request(req_id, ClanRequestAction::RequestMembership, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::cancel_request_membership(np::np_handler& nph, u32 req_id, SceNpClanId clan_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		return send_request(req_id, ClanRequestAction::CancelRequestMembership, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::send_membership_response(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id, SceNpClansMessage& /*message*/, b8 allow)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, allow ? ClanRequestAction::AcceptMembershipRequest : ClanRequestAction::DeclineMembershipRequest, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::send_invitation(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id, SceNpClansMessage& /*message*/)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, ClanRequestAction::SendInvitation, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::cancel_invitation(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, ClanRequestAction::CancelInvitation, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::send_invitation_response(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, SceNpClansMessage& /*message*/, b8 accept)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		return send_request(req_id, accept ? ClanRequestAction::AcceptInvitation : ClanRequestAction::DeclineInvitation, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::update_member_info(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansUpdatableMemberInfo& info)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		doc.element("onlinename", np::npid_to_string(nph.get_npid()));

		doc.element("description", info.description);

		const u32 bin_data_size = static_cast<u32>(info.binData1Size);
		if (bin_data_size > sizeof(info.binAttr1))
			return SCE_NP_CLANS_ERROR_INVALID_ARGUMENT;

		const std::string bin_attr_1 = base64_string(info.binAttr1, bin_data_size);

		if (bin_attr_1.empty() && bin_data_size)
			return SCE_NP_CLANS_ERROR_INVALID_ARGUMENT;

		doc.element("bin-attr1", bin_attr_1.c_str());

		doc.element("allow-msg", static_cast<uint32_t>(info.allowMsg));

		doc.element("size", info.binData1Size);

		clan_response response;
		return send_request(req_id, ClanRequestAction::UpdateMemberInfo, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::update_clan_info(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansUpdatableClanInfo& info)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		// TODO: implement binary and integer attributes (not implemented in server yet)

		doc.element("description", info.description);

		clan_response response;
		return send_request(req_id, ClanRequestAction::UpdateClanInfo, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::join_clan(np::np_handler& nph, u32 req_id, SceNpClanId clan_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		return send_request(req_id, ClanRequestAction::JoinClan, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::leave_clan(np::np_handler& nph, u32 req_id, SceNpClanId clan_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		clan_response response;
		return send_request(req_id, ClanRequestAction::LeaveClan, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::kick_member(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id, SceNpClansMessage& /*message*/)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		clan_response response;
		return send_request(req_id, ClanRequestAction::KickMember, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::change_member_role(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpId& np_id, SceNpClansMemberRole role)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		const std::string jid_str = fmt::format(JID_FORMAT, np::npid_to_string(np_id));
		doc.element("jid", jid_str);

		doc.element("role", static_cast<uint32_t>(role));

		clan_response response;
		return send_request(req_id, ClanRequestAction::ChangeMemberRole, ClanManagerOperationType::UPDATE, doc, response);
	}

	SceNpClansError clans_client::retrieve_announcements(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansPagingRequest& paging, std::vector<SceNpClansMessageEntry>& announcements, SceNpClansPagingResult& page_result)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);
		doc.element("start", paging.startPos);
		doc.element("max", paging.max);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::RetrieveAnnouncements, ClanManagerOperationType::VIEW, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const list = xml_child(clan_result, "list");

		const char* const results = xml_attr(list, "results");
		const uint32_t results_count = xml_uint(results);

		const char* const total = xml_attr(list, "total");
		const uint32_t total_count = xml_uint(total);

		int i = 0;
		for (rxml_node_t* node = xml_child(list, "msg-info"); node; node = xml_next(node, "msg-info"))
		{
			const char* const id = xml_attr(node, "id");
			const uint32_t msg_id = xml_uint(id);

			const std::string subject_str = xml_text(xml_child(node, "subject"));
			const std::string msg_str = xml_text(xml_child(node, "msg"));
			const std::string author_jid = xml_text(xml_child(node, "jid"));
			const std::string msg_date = xml_text(xml_child(node, "msg-date"));

			SceNpId author_npid;
			const std::string author_username = fmt::split(author_jid, {"@"})[0];

			if (strncmp(author_username.c_str(), nph.get_npid().handle.data, SCE_NET_NP_ONLINEID_MAX_LENGTH) == 0)
			{
				author_npid = nph.get_npid();
			}
			else
			{
				np::string_to_npid(author_username, author_npid);
			}

			// TODO: implement `binData` and `fromId`

			SceNpClansMessageEntry entry = SceNpClansMessageEntry
			{
				.mId = msg_id,
				.message = SceNpClansMessage {
					.subject = "",
					.body = "",
				},
				.npid = std::move(author_npid),
				.postedBy = clan_id,
			};

			strcpy_trunc(entry.message.subject, subject_str);
			strcpy_trunc(entry.message.body, msg_str);

			::at32(announcements, i) = std::move(entry);
			i++;
		}

		page_result = SceNpClansPagingResult
		{
			.count = results_count,
			.total = total_count
		};

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::post_announcement(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, const SceNpClansMessage& announcement, const SceNpClansMessageData& /*data*/, u32 duration, SceNpClansMessageId& msg_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);

		doc.element("subject", announcement.subject);

		doc.element("msg", announcement.body);

		doc.element("expire-date", duration);

		clan_response response;
		const SceNpClansError clan_res = send_request(req_id, ClanRequestAction::PostAnnouncement, ClanManagerOperationType::UPDATE, doc, response);

		if (clan_res != SCE_NP_CLANS_SUCCESS)
			return clan_res;

		rxml_node_t* const clan_result = response.clan();
		rxml_node_t* const msg_id_node = xml_child(clan_result, "id");
		msg_id = xml_uint(msg_id_node);

		return SCE_NP_CLANS_SUCCESS;
	}

	SceNpClansError clans_client::delete_announcement(np::np_handler& nph, u32 req_id, SceNpClanId clan_id, SceNpClansMessageId announcement_id)
	{
		const std::string ticket = get_clan_ticket(nph);
		if (ticket.empty())
			return SCE_NP_CLANS_ERROR_SERVICE_UNAVAILABLE;

		clan_request doc;
		doc.element("ticket", ticket);
		doc.element("id", clan_id);
		doc.element("msg-id", announcement_id);

		clan_response response;
		return send_request(req_id, ClanRequestAction::DeleteAnnouncement, ClanManagerOperationType::UPDATE, doc, response);
	}
}
#pragma endregion
