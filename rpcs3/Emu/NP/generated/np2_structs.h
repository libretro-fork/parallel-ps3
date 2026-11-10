#pragma once

// RPCN's messages, made by np2_structs_gen.py from np2_structs.proto: do not
// edit. Read and written by Emu/NP/pb_wire.h. A message read with
// ParseFromArray borrows its strings and bytes from the buffer it was read
// from, which must outlive it; one filled by its setters owns them.

#include "Emu/NP/pb_wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace np2_structs
{
	class uint8 final
	{
	public:
		std::uint32_t value() const { return m_value; }
		void set_value(std::uint32_t v) { m_value = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_value = {};
		mutable std::size_t m_cached_size = 0;
	};

	class uint16 final
	{
	public:
		std::uint32_t value() const { return m_value; }
		void set_value(std::uint32_t v) { m_value = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_value = {};
		mutable std::size_t m_cached_size = 0;
	};

	class SignalingAddr final
	{
	public:
		std::string_view ip() const { return m_ip.get(); }
		void set_ip(std::string_view v) { m_ip.set(v); }
		void set_ip(std::string&& v) { m_ip.set(std::move(v)); }
		void set_ip(const char* v) { m_ip.set(v); }
		void set_ip(const void* p, std::size_t n) { m_ip.set(p, n); }
		bool has_port() const { return m_has_port; }
		const uint16& port() const { return m_port; }
		uint16* mutable_port() { m_has_port = true; return &m_port; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_ip;
		uint16 m_port;
		bool m_has_port = false;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingSignalingInfo final
	{
	public:
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }
		bool has_addr() const { return m_has_addr; }
		const SignalingAddr& addr() const { return m_addr; }
		SignalingAddr* mutable_addr() { m_has_addr = true; return &m_addr; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_npid;
		SignalingAddr m_addr;
		bool m_has_addr = false;
		mutable std::size_t m_cached_size = 0;
	};

	class Matching2SignalingInfo final
	{
	public:
		bool has_member_id() const { return m_has_member_id; }
		const uint16& member_id() const { return m_member_id; }
		uint16* mutable_member_id() { m_has_member_id = true; return &m_member_id; }
		bool has_addr() const { return m_has_addr; }
		const SignalingAddr& addr() const { return m_addr; }
		SignalingAddr* mutable_addr() { m_has_addr = true; return &m_addr; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_member_id;
		bool m_has_member_id = false;
		SignalingAddr m_addr;
		bool m_has_addr = false;
		mutable std::size_t m_cached_size = 0;
	};

	class BinAttr final
	{
	public:
		bool has_id() const { return m_has_id; }
		const uint16& id() const { return m_id; }
		uint16* mutable_id() { m_has_id = true; return &m_id; }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_id;
		bool m_has_id = false;
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class IntAttr final
	{
	public:
		bool has_id() const { return m_has_id; }
		const uint16& id() const { return m_id; }
		uint16* mutable_id() { m_has_id = true; return &m_id; }
		std::uint32_t num() const { return m_num; }
		void set_num(std::uint32_t v) { m_num = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_id;
		bool m_has_id = false;
		std::uint32_t m_num = {};
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMemberBinAttrInternal final
	{
	public:
		std::uint64_t updatedate() const { return m_updatedate; }
		void set_updatedate(std::uint64_t v) { m_updatedate = v; }
		bool has_data() const { return m_has_data; }
		const BinAttr& data() const { return m_data; }
		BinAttr* mutable_data() { m_has_data = true; return &m_data; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_updatedate = {};
		BinAttr m_data;
		bool m_has_data = false;
		mutable std::size_t m_cached_size = 0;
	};

	class BinAttrInternal final
	{
	public:
		std::uint64_t updatedate() const { return m_updatedate; }
		void set_updatedate(std::uint64_t v) { m_updatedate = v; }
		bool has_updatememberid() const { return m_has_updatememberid; }
		const uint16& updatememberid() const { return m_updatememberid; }
		uint16* mutable_updatememberid() { m_has_updatememberid = true; return &m_updatememberid; }
		bool has_data() const { return m_has_data; }
		const BinAttr& data() const { return m_data; }
		BinAttr* mutable_data() { m_has_data = true; return &m_data; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_updatedate = {};
		uint16 m_updatememberid;
		bool m_has_updatememberid = false;
		BinAttr m_data;
		bool m_has_data = false;
		mutable std::size_t m_cached_size = 0;
	};

	class OptParam final
	{
	public:
		bool has_type() const { return m_has_type; }
		const uint8& type() const { return m_type; }
		uint8* mutable_type() { m_has_type = true; return &m_type; }
		bool has_flag() const { return m_has_flag; }
		const uint8& flag() const { return m_flag; }
		uint8* mutable_flag() { m_has_flag = true; return &m_flag; }
		bool has_hubmemberid() const { return m_has_hubmemberid; }
		const uint16& hubmemberid() const { return m_hubmemberid; }
		uint16* mutable_hubmemberid() { m_has_hubmemberid = true; return &m_hubmemberid; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_type;
		bool m_has_type = false;
		uint8 m_flag;
		bool m_has_flag = false;
		uint16 m_hubmemberid;
		bool m_has_hubmemberid = false;
		mutable std::size_t m_cached_size = 0;
	};

	class GroupConfig final
	{
	public:
		std::uint32_t slotnum() const { return m_slotnum; }
		void set_slotnum(std::uint32_t v) { m_slotnum = v; }
		std::string_view label() const { return m_label.get(); }
		void set_label(std::string_view v) { m_label.set(v); }
		void set_label(std::string&& v) { m_label.set(std::move(v)); }
		void set_label(const char* v) { m_label.set(v); }
		void set_label(const void* p, std::size_t n) { m_label.set(p, n); }
		bool withpassword() const { return m_withpassword; }
		void set_withpassword(bool v) { m_withpassword = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_slotnum = {};
		pb::text m_label;
		bool m_withpassword = {};
		mutable std::size_t m_cached_size = 0;
	};

	class UserInfo final
	{
	public:
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }
		std::string_view onlinename() const { return m_onlinename.get(); }
		void set_onlinename(std::string_view v) { m_onlinename.set(v); }
		void set_onlinename(std::string&& v) { m_onlinename.set(std::move(v)); }
		void set_onlinename(const char* v) { m_onlinename.set(v); }
		void set_onlinename(const void* p, std::size_t n) { m_onlinename.set(p, n); }
		std::string_view avatarurl() const { return m_avatarurl.get(); }
		void set_avatarurl(std::string_view v) { m_avatarurl.set(v); }
		void set_avatarurl(std::string&& v) { m_avatarurl.set(std::move(v)); }
		void set_avatarurl(const char* v) { m_avatarurl.set(v); }
		void set_avatarurl(const void* p, std::size_t n) { m_avatarurl.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_npid;
		pb::text m_onlinename;
		pb::text m_avatarurl;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomGroup final
	{
	public:
		bool has_groupid() const { return m_has_groupid; }
		const uint8& groupid() const { return m_groupid; }
		uint8* mutable_groupid() { m_has_groupid = true; return &m_groupid; }
		bool withpassword() const { return m_withpassword; }
		void set_withpassword(bool v) { m_withpassword = v; }
		std::string_view label() const { return m_label.get(); }
		void set_label(std::string_view v) { m_label.set(v); }
		void set_label(std::string&& v) { m_label.set(std::move(v)); }
		void set_label(const char* v) { m_label.set(v); }
		void set_label(const void* p, std::size_t n) { m_label.set(p, n); }
		std::uint32_t slotnum() const { return m_slotnum; }
		void set_slotnum(std::uint32_t v) { m_slotnum = v; }
		std::uint32_t curgroupmembernum() const { return m_curgroupmembernum; }
		void set_curgroupmembernum(std::uint32_t v) { m_curgroupmembernum = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_groupid;
		bool m_has_groupid = false;
		bool m_withpassword = {};
		pb::text m_label;
		std::uint32_t m_slotnum = {};
		std::uint32_t m_curgroupmembernum = {};
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMemberDataInternal final
	{
	public:
		bool has_userinfo() const { return m_has_userinfo; }
		const UserInfo& userinfo() const { return m_userinfo; }
		UserInfo* mutable_userinfo() { m_has_userinfo = true; return &m_userinfo; }
		std::uint64_t joindate() const { return m_joindate; }
		void set_joindate(std::uint64_t v) { m_joindate = v; }
		std::uint32_t memberid() const { return m_memberid; }
		void set_memberid(std::uint32_t v) { m_memberid = v; }
		bool has_teamid() const { return m_has_teamid; }
		const uint8& teamid() const { return m_teamid; }
		uint8* mutable_teamid() { m_has_teamid = true; return &m_teamid; }
		bool has_roomgroup() const { return m_has_roomgroup; }
		const RoomGroup& roomgroup() const { return m_roomgroup; }
		RoomGroup* mutable_roomgroup() { m_has_roomgroup = true; return &m_roomgroup; }
		bool has_nattype() const { return m_has_nattype; }
		const uint8& nattype() const { return m_nattype; }
		uint8* mutable_nattype() { m_has_nattype = true; return &m_nattype; }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<RoomMemberBinAttrInternal>& roommemberbinattrinternal() const { return m_roommemberbinattrinternal; }
		std::vector<RoomMemberBinAttrInternal>* mutable_roommemberbinattrinternal() { return &m_roommemberbinattrinternal; }
		int roommemberbinattrinternal_size() const { return static_cast<int>(m_roommemberbinattrinternal.size()); }
		const RoomMemberBinAttrInternal& roommemberbinattrinternal(int i) const { return m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		RoomMemberBinAttrInternal* mutable_roommemberbinattrinternal(int i) { return &m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		RoomMemberBinAttrInternal* add_roommemberbinattrinternal() { return &m_roommemberbinattrinternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		UserInfo m_userinfo;
		bool m_has_userinfo = false;
		std::uint64_t m_joindate = {};
		std::uint32_t m_memberid = {};
		uint8 m_teamid;
		bool m_has_teamid = false;
		RoomGroup m_roomgroup;
		bool m_has_roomgroup = false;
		uint8 m_nattype;
		bool m_has_nattype = false;
		std::uint32_t m_flagattr = {};
		std::vector<RoomMemberBinAttrInternal> m_roommemberbinattrinternal;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomDataInternal final
	{
	public:
		bool has_serverid() const { return m_has_serverid; }
		const uint16& serverid() const { return m_serverid; }
		uint16* mutable_serverid() { m_has_serverid = true; return &m_serverid; }
		std::uint32_t worldid() const { return m_worldid; }
		void set_worldid(std::uint32_t v) { m_worldid = v; }
		std::uint64_t lobbyid() const { return m_lobbyid; }
		void set_lobbyid(std::uint64_t v) { m_lobbyid = v; }
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		std::uint64_t passwordslotmask() const { return m_passwordslotmask; }
		void set_passwordslotmask(std::uint64_t v) { m_passwordslotmask = v; }
		std::uint32_t maxslot() const { return m_maxslot; }
		void set_maxslot(std::uint32_t v) { m_maxslot = v; }
		const std::vector<RoomMemberDataInternal>& memberlist() const { return m_memberlist; }
		std::vector<RoomMemberDataInternal>* mutable_memberlist() { return &m_memberlist; }
		int memberlist_size() const { return static_cast<int>(m_memberlist.size()); }
		const RoomMemberDataInternal& memberlist(int i) const { return m_memberlist[static_cast<std::size_t>(i)]; }
		RoomMemberDataInternal* mutable_memberlist(int i) { return &m_memberlist[static_cast<std::size_t>(i)]; }
		RoomMemberDataInternal* add_memberlist() { return &m_memberlist.emplace_back(); }
		bool has_ownerid() const { return m_has_ownerid; }
		const uint16& ownerid() const { return m_ownerid; }
		uint16* mutable_ownerid() { m_has_ownerid = true; return &m_ownerid; }
		const std::vector<RoomGroup>& roomgroup() const { return m_roomgroup; }
		std::vector<RoomGroup>* mutable_roomgroup() { return &m_roomgroup; }
		int roomgroup_size() const { return static_cast<int>(m_roomgroup.size()); }
		const RoomGroup& roomgroup(int i) const { return m_roomgroup[static_cast<std::size_t>(i)]; }
		RoomGroup* mutable_roomgroup(int i) { return &m_roomgroup[static_cast<std::size_t>(i)]; }
		RoomGroup* add_roomgroup() { return &m_roomgroup.emplace_back(); }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<BinAttrInternal>& roombinattrinternal() const { return m_roombinattrinternal; }
		std::vector<BinAttrInternal>* mutable_roombinattrinternal() { return &m_roombinattrinternal; }
		int roombinattrinternal_size() const { return static_cast<int>(m_roombinattrinternal.size()); }
		const BinAttrInternal& roombinattrinternal(int i) const { return m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttrInternal* mutable_roombinattrinternal(int i) { return &m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttrInternal* add_roombinattrinternal() { return &m_roombinattrinternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_serverid;
		bool m_has_serverid = false;
		std::uint32_t m_worldid = {};
		std::uint64_t m_lobbyid = {};
		std::uint64_t m_roomid = {};
		std::uint64_t m_passwordslotmask = {};
		std::uint32_t m_maxslot = {};
		std::vector<RoomMemberDataInternal> m_memberlist;
		uint16 m_ownerid;
		bool m_has_ownerid = false;
		std::vector<RoomGroup> m_roomgroup;
		std::uint32_t m_flagattr = {};
		std::vector<BinAttrInternal> m_roombinattrinternal;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomDataExternal final
	{
	public:
		bool has_serverid() const { return m_has_serverid; }
		const uint16& serverid() const { return m_serverid; }
		uint16* mutable_serverid() { m_has_serverid = true; return &m_serverid; }
		std::uint32_t worldid() const { return m_worldid; }
		void set_worldid(std::uint32_t v) { m_worldid = v; }
		bool has_publicslotnum() const { return m_has_publicslotnum; }
		const uint16& publicslotnum() const { return m_publicslotnum; }
		uint16* mutable_publicslotnum() { m_has_publicslotnum = true; return &m_publicslotnum; }
		bool has_privateslotnum() const { return m_has_privateslotnum; }
		const uint16& privateslotnum() const { return m_privateslotnum; }
		uint16* mutable_privateslotnum() { m_has_privateslotnum = true; return &m_privateslotnum; }
		std::uint64_t lobbyid() const { return m_lobbyid; }
		void set_lobbyid(std::uint64_t v) { m_lobbyid = v; }
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		bool has_openpublicslotnum() const { return m_has_openpublicslotnum; }
		const uint16& openpublicslotnum() const { return m_openpublicslotnum; }
		uint16* mutable_openpublicslotnum() { m_has_openpublicslotnum = true; return &m_openpublicslotnum; }
		bool has_maxslot() const { return m_has_maxslot; }
		const uint16& maxslot() const { return m_maxslot; }
		uint16* mutable_maxslot() { m_has_maxslot = true; return &m_maxslot; }
		bool has_openprivateslotnum() const { return m_has_openprivateslotnum; }
		const uint16& openprivateslotnum() const { return m_openprivateslotnum; }
		uint16* mutable_openprivateslotnum() { m_has_openprivateslotnum = true; return &m_openprivateslotnum; }
		bool has_curmembernum() const { return m_has_curmembernum; }
		const uint16& curmembernum() const { return m_curmembernum; }
		uint16* mutable_curmembernum() { m_has_curmembernum = true; return &m_curmembernum; }
		std::uint64_t passwordslotmask() const { return m_passwordslotmask; }
		void set_passwordslotmask(std::uint64_t v) { m_passwordslotmask = v; }
		bool has_owner() const { return m_has_owner; }
		const UserInfo& owner() const { return m_owner; }
		UserInfo* mutable_owner() { m_has_owner = true; return &m_owner; }
		const std::vector<RoomGroup>& roomgroup() const { return m_roomgroup; }
		std::vector<RoomGroup>* mutable_roomgroup() { return &m_roomgroup; }
		int roomgroup_size() const { return static_cast<int>(m_roomgroup.size()); }
		const RoomGroup& roomgroup(int i) const { return m_roomgroup[static_cast<std::size_t>(i)]; }
		RoomGroup* mutable_roomgroup(int i) { return &m_roomgroup[static_cast<std::size_t>(i)]; }
		RoomGroup* add_roomgroup() { return &m_roomgroup.emplace_back(); }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<IntAttr>& roomsearchableintattrexternal() const { return m_roomsearchableintattrexternal; }
		std::vector<IntAttr>* mutable_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal; }
		int roomsearchableintattrexternal_size() const { return static_cast<int>(m_roomsearchableintattrexternal.size()); }
		const IntAttr& roomsearchableintattrexternal(int i) const { return m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* mutable_roomsearchableintattrexternal(int i) { return &m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* add_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roomsearchablebinattrexternal() const { return m_roomsearchablebinattrexternal; }
		std::vector<BinAttr>* mutable_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal; }
		int roomsearchablebinattrexternal_size() const { return static_cast<int>(m_roomsearchablebinattrexternal.size()); }
		const BinAttr& roomsearchablebinattrexternal(int i) const { return m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roomsearchablebinattrexternal(int i) { return &m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roombinattrexternal() const { return m_roombinattrexternal; }
		std::vector<BinAttr>* mutable_roombinattrexternal() { return &m_roombinattrexternal; }
		int roombinattrexternal_size() const { return static_cast<int>(m_roombinattrexternal.size()); }
		const BinAttr& roombinattrexternal(int i) const { return m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roombinattrexternal(int i) { return &m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roombinattrexternal() { return &m_roombinattrexternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_serverid;
		bool m_has_serverid = false;
		std::uint32_t m_worldid = {};
		uint16 m_publicslotnum;
		bool m_has_publicslotnum = false;
		uint16 m_privateslotnum;
		bool m_has_privateslotnum = false;
		std::uint64_t m_lobbyid = {};
		std::uint64_t m_roomid = {};
		uint16 m_openpublicslotnum;
		bool m_has_openpublicslotnum = false;
		uint16 m_maxslot;
		bool m_has_maxslot = false;
		uint16 m_openprivateslotnum;
		bool m_has_openprivateslotnum = false;
		uint16 m_curmembernum;
		bool m_has_curmembernum = false;
		std::uint64_t m_passwordslotmask = {};
		UserInfo m_owner;
		bool m_has_owner = false;
		std::vector<RoomGroup> m_roomgroup;
		std::uint32_t m_flagattr = {};
		std::vector<IntAttr> m_roomsearchableintattrexternal;
		std::vector<BinAttr> m_roomsearchablebinattrexternal;
		std::vector<BinAttr> m_roombinattrexternal;
		mutable std::size_t m_cached_size = 0;
	};

	class IntSearchFilter final
	{
	public:
		bool has_searchoperator() const { return m_has_searchoperator; }
		const uint8& searchoperator() const { return m_searchoperator; }
		uint8* mutable_searchoperator() { m_has_searchoperator = true; return &m_searchoperator; }
		bool has_attr() const { return m_has_attr; }
		const IntAttr& attr() const { return m_attr; }
		IntAttr* mutable_attr() { m_has_attr = true; return &m_attr; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_searchoperator;
		bool m_has_searchoperator = false;
		IntAttr m_attr;
		bool m_has_attr = false;
		mutable std::size_t m_cached_size = 0;
	};

	class BinSearchFilter final
	{
	public:
		bool has_searchoperator() const { return m_has_searchoperator; }
		const uint8& searchoperator() const { return m_searchoperator; }
		uint8* mutable_searchoperator() { m_has_searchoperator = true; return &m_searchoperator; }
		bool has_attr() const { return m_has_attr; }
		const BinAttr& attr() const { return m_attr; }
		BinAttr* mutable_attr() { m_has_attr = true; return &m_attr; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_searchoperator;
		bool m_has_searchoperator = false;
		BinAttr m_attr;
		bool m_has_attr = false;
		mutable std::size_t m_cached_size = 0;
	};

	class PresenceOptionData final
	{
	public:
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }
		std::uint32_t len() const { return m_len; }
		void set_len(std::uint32_t v) { m_len = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_data;
		std::uint32_t m_len = {};
		mutable std::size_t m_cached_size = 0;
	};

	class RoomGroupPasswordConfig final
	{
	public:
		bool has_groupid() const { return m_has_groupid; }
		const uint8& groupid() const { return m_groupid; }
		uint8* mutable_groupid() { m_has_groupid = true; return &m_groupid; }
		bool withpassword() const { return m_withpassword; }
		void set_withpassword(bool v) { m_withpassword = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_groupid;
		bool m_has_groupid = false;
		bool m_withpassword = {};
		mutable std::size_t m_cached_size = 0;
	};

	class SearchRoomRequest final
	{
	public:
		std::int32_t option() const { return m_option; }
		void set_option(std::int32_t v) { m_option = v; }
		std::uint32_t worldid() const { return m_worldid; }
		void set_worldid(std::uint32_t v) { m_worldid = v; }
		std::uint64_t lobbyid() const { return m_lobbyid; }
		void set_lobbyid(std::uint64_t v) { m_lobbyid = v; }
		std::uint32_t rangefilter_startindex() const { return m_rangefilter_startindex; }
		void set_rangefilter_startindex(std::uint32_t v) { m_rangefilter_startindex = v; }
		std::uint32_t rangefilter_max() const { return m_rangefilter_max; }
		void set_rangefilter_max(std::uint32_t v) { m_rangefilter_max = v; }
		std::uint32_t flagfilter() const { return m_flagfilter; }
		void set_flagfilter(std::uint32_t v) { m_flagfilter = v; }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<IntSearchFilter>& intfilter() const { return m_intfilter; }
		std::vector<IntSearchFilter>* mutable_intfilter() { return &m_intfilter; }
		int intfilter_size() const { return static_cast<int>(m_intfilter.size()); }
		const IntSearchFilter& intfilter(int i) const { return m_intfilter[static_cast<std::size_t>(i)]; }
		IntSearchFilter* mutable_intfilter(int i) { return &m_intfilter[static_cast<std::size_t>(i)]; }
		IntSearchFilter* add_intfilter() { return &m_intfilter.emplace_back(); }
		const std::vector<BinSearchFilter>& binfilter() const { return m_binfilter; }
		std::vector<BinSearchFilter>* mutable_binfilter() { return &m_binfilter; }
		int binfilter_size() const { return static_cast<int>(m_binfilter.size()); }
		const BinSearchFilter& binfilter(int i) const { return m_binfilter[static_cast<std::size_t>(i)]; }
		BinSearchFilter* mutable_binfilter(int i) { return &m_binfilter[static_cast<std::size_t>(i)]; }
		BinSearchFilter* add_binfilter() { return &m_binfilter.emplace_back(); }
		const std::vector<uint16>& attrid() const { return m_attrid; }
		std::vector<uint16>* mutable_attrid() { return &m_attrid; }
		int attrid_size() const { return static_cast<int>(m_attrid.size()); }
		const uint16& attrid(int i) const { return m_attrid[static_cast<std::size_t>(i)]; }
		uint16* mutable_attrid(int i) { return &m_attrid[static_cast<std::size_t>(i)]; }
		uint16* add_attrid() { return &m_attrid.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::int32_t m_option = {};
		std::uint32_t m_worldid = {};
		std::uint64_t m_lobbyid = {};
		std::uint32_t m_rangefilter_startindex = {};
		std::uint32_t m_rangefilter_max = {};
		std::uint32_t m_flagfilter = {};
		std::uint32_t m_flagattr = {};
		std::vector<IntSearchFilter> m_intfilter;
		std::vector<BinSearchFilter> m_binfilter;
		std::vector<uint16> m_attrid;
		mutable std::size_t m_cached_size = 0;
	};

	class SearchRoomResponse final
	{
	public:
		std::uint32_t startindex() const { return m_startindex; }
		void set_startindex(std::uint32_t v) { m_startindex = v; }
		std::uint32_t total() const { return m_total; }
		void set_total(std::uint32_t v) { m_total = v; }
		const std::vector<RoomDataExternal>& rooms() const { return m_rooms; }
		std::vector<RoomDataExternal>* mutable_rooms() { return &m_rooms; }
		int rooms_size() const { return static_cast<int>(m_rooms.size()); }
		const RoomDataExternal& rooms(int i) const { return m_rooms[static_cast<std::size_t>(i)]; }
		RoomDataExternal* mutable_rooms(int i) { return &m_rooms[static_cast<std::size_t>(i)]; }
		RoomDataExternal* add_rooms() { return &m_rooms.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_startindex = {};
		std::uint32_t m_total = {};
		std::vector<RoomDataExternal> m_rooms;
		mutable std::size_t m_cached_size = 0;
	};

	class CreateJoinRoomRequest final
	{
	public:
		std::uint32_t worldid() const { return m_worldid; }
		void set_worldid(std::uint32_t v) { m_worldid = v; }
		std::uint64_t lobbyid() const { return m_lobbyid; }
		void set_lobbyid(std::uint64_t v) { m_lobbyid = v; }
		std::uint32_t maxslot() const { return m_maxslot; }
		void set_maxslot(std::uint32_t v) { m_maxslot = v; }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<BinAttr>& roombinattrinternal() const { return m_roombinattrinternal; }
		std::vector<BinAttr>* mutable_roombinattrinternal() { return &m_roombinattrinternal; }
		int roombinattrinternal_size() const { return static_cast<int>(m_roombinattrinternal.size()); }
		const BinAttr& roombinattrinternal(int i) const { return m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roombinattrinternal(int i) { return &m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roombinattrinternal() { return &m_roombinattrinternal.emplace_back(); }
		const std::vector<IntAttr>& roomsearchableintattrexternal() const { return m_roomsearchableintattrexternal; }
		std::vector<IntAttr>* mutable_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal; }
		int roomsearchableintattrexternal_size() const { return static_cast<int>(m_roomsearchableintattrexternal.size()); }
		const IntAttr& roomsearchableintattrexternal(int i) const { return m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* mutable_roomsearchableintattrexternal(int i) { return &m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* add_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roomsearchablebinattrexternal() const { return m_roomsearchablebinattrexternal; }
		std::vector<BinAttr>* mutable_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal; }
		int roomsearchablebinattrexternal_size() const { return static_cast<int>(m_roomsearchablebinattrexternal.size()); }
		const BinAttr& roomsearchablebinattrexternal(int i) const { return m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roomsearchablebinattrexternal(int i) { return &m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roombinattrexternal() const { return m_roombinattrexternal; }
		std::vector<BinAttr>* mutable_roombinattrexternal() { return &m_roombinattrexternal; }
		int roombinattrexternal_size() const { return static_cast<int>(m_roombinattrexternal.size()); }
		const BinAttr& roombinattrexternal(int i) const { return m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roombinattrexternal(int i) { return &m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roombinattrexternal() { return &m_roombinattrexternal.emplace_back(); }
		std::string_view roompassword() const { return m_roompassword.get(); }
		void set_roompassword(std::string_view v) { m_roompassword.set(v); }
		void set_roompassword(std::string&& v) { m_roompassword.set(std::move(v)); }
		void set_roompassword(const char* v) { m_roompassword.set(v); }
		void set_roompassword(const void* p, std::size_t n) { m_roompassword.set(p, n); }
		const std::vector<GroupConfig>& groupconfig() const { return m_groupconfig; }
		std::vector<GroupConfig>* mutable_groupconfig() { return &m_groupconfig; }
		int groupconfig_size() const { return static_cast<int>(m_groupconfig.size()); }
		const GroupConfig& groupconfig(int i) const { return m_groupconfig[static_cast<std::size_t>(i)]; }
		GroupConfig* mutable_groupconfig(int i) { return &m_groupconfig[static_cast<std::size_t>(i)]; }
		GroupConfig* add_groupconfig() { return &m_groupconfig.emplace_back(); }
		std::uint64_t passwordslotmask() const { return m_passwordslotmask; }
		void set_passwordslotmask(std::uint64_t v) { m_passwordslotmask = v; }
		const std::vector<pb::text>& alloweduser() const { return m_alloweduser; }
		std::vector<pb::text>* mutable_alloweduser() { return &m_alloweduser; }
		int alloweduser_size() const { return static_cast<int>(m_alloweduser.size()); }
		std::string_view alloweduser(int i) const { return m_alloweduser[static_cast<std::size_t>(i)].get(); }
		void add_alloweduser(std::string_view v) { m_alloweduser.emplace_back().set(v); }
		void add_alloweduser(std::string&& v) { m_alloweduser.emplace_back().set(std::move(v)); }
		void add_alloweduser(const char* v) { m_alloweduser.emplace_back().set(v); }
		void add_alloweduser(const void* p, std::size_t n) { m_alloweduser.emplace_back().set(p, n); }
		const std::vector<pb::text>& blockeduser() const { return m_blockeduser; }
		std::vector<pb::text>* mutable_blockeduser() { return &m_blockeduser; }
		int blockeduser_size() const { return static_cast<int>(m_blockeduser.size()); }
		std::string_view blockeduser(int i) const { return m_blockeduser[static_cast<std::size_t>(i)].get(); }
		void add_blockeduser(std::string_view v) { m_blockeduser.emplace_back().set(v); }
		void add_blockeduser(std::string&& v) { m_blockeduser.emplace_back().set(std::move(v)); }
		void add_blockeduser(const char* v) { m_blockeduser.emplace_back().set(v); }
		void add_blockeduser(const void* p, std::size_t n) { m_blockeduser.emplace_back().set(p, n); }
		std::string_view joinroomgrouplabel() const { return m_joinroomgrouplabel.get(); }
		void set_joinroomgrouplabel(std::string_view v) { m_joinroomgrouplabel.set(v); }
		void set_joinroomgrouplabel(std::string&& v) { m_joinroomgrouplabel.set(std::move(v)); }
		void set_joinroomgrouplabel(const char* v) { m_joinroomgrouplabel.set(v); }
		void set_joinroomgrouplabel(const void* p, std::size_t n) { m_joinroomgrouplabel.set(p, n); }
		const std::vector<BinAttr>& roommemberbinattrinternal() const { return m_roommemberbinattrinternal; }
		std::vector<BinAttr>* mutable_roommemberbinattrinternal() { return &m_roommemberbinattrinternal; }
		int roommemberbinattrinternal_size() const { return static_cast<int>(m_roommemberbinattrinternal.size()); }
		const BinAttr& roommemberbinattrinternal(int i) const { return m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roommemberbinattrinternal(int i) { return &m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roommemberbinattrinternal() { return &m_roommemberbinattrinternal.emplace_back(); }
		bool has_teamid() const { return m_has_teamid; }
		const uint8& teamid() const { return m_teamid; }
		uint8* mutable_teamid() { m_has_teamid = true; return &m_teamid; }
		bool has_sigoptparam() const { return m_has_sigoptparam; }
		const OptParam& sigoptparam() const { return m_sigoptparam; }
		OptParam* mutable_sigoptparam() { m_has_sigoptparam = true; return &m_sigoptparam; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_worldid = {};
		std::uint64_t m_lobbyid = {};
		std::uint32_t m_maxslot = {};
		std::uint32_t m_flagattr = {};
		std::vector<BinAttr> m_roombinattrinternal;
		std::vector<IntAttr> m_roomsearchableintattrexternal;
		std::vector<BinAttr> m_roomsearchablebinattrexternal;
		std::vector<BinAttr> m_roombinattrexternal;
		pb::text m_roompassword;
		std::vector<GroupConfig> m_groupconfig;
		std::uint64_t m_passwordslotmask = {};
		std::vector<pb::text> m_alloweduser;
		std::vector<pb::text> m_blockeduser;
		pb::text m_joinroomgrouplabel;
		std::vector<BinAttr> m_roommemberbinattrinternal;
		uint8 m_teamid;
		bool m_has_teamid = false;
		OptParam m_sigoptparam;
		bool m_has_sigoptparam = false;
		mutable std::size_t m_cached_size = 0;
	};

	class CreateRoomResponse final
	{
	public:
		bool has_internal() const { return m_has_internal; }
		const RoomDataInternal& internal() const { return m_internal; }
		RoomDataInternal* mutable_internal() { m_has_internal = true; return &m_internal; }
		bool has_opt_param() const { return m_has_opt_param; }
		const OptParam& opt_param() const { return m_opt_param; }
		OptParam* mutable_opt_param() { m_has_opt_param = true; return &m_opt_param; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		RoomDataInternal m_internal;
		bool m_has_internal = false;
		OptParam m_opt_param;
		bool m_has_opt_param = false;
		mutable std::size_t m_cached_size = 0;
	};

	class JoinRoomRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		std::string_view roompassword() const { return m_roompassword.get(); }
		void set_roompassword(std::string_view v) { m_roompassword.set(v); }
		void set_roompassword(std::string&& v) { m_roompassword.set(std::move(v)); }
		void set_roompassword(const char* v) { m_roompassword.set(v); }
		void set_roompassword(const void* p, std::size_t n) { m_roompassword.set(p, n); }
		std::string_view joinroomgrouplabel() const { return m_joinroomgrouplabel.get(); }
		void set_joinroomgrouplabel(std::string_view v) { m_joinroomgrouplabel.set(v); }
		void set_joinroomgrouplabel(std::string&& v) { m_joinroomgrouplabel.set(std::move(v)); }
		void set_joinroomgrouplabel(const char* v) { m_joinroomgrouplabel.set(v); }
		void set_joinroomgrouplabel(const void* p, std::size_t n) { m_joinroomgrouplabel.set(p, n); }
		const std::vector<BinAttr>& roommemberbinattrinternal() const { return m_roommemberbinattrinternal; }
		std::vector<BinAttr>* mutable_roommemberbinattrinternal() { return &m_roommemberbinattrinternal; }
		int roommemberbinattrinternal_size() const { return static_cast<int>(m_roommemberbinattrinternal.size()); }
		const BinAttr& roommemberbinattrinternal(int i) const { return m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roommemberbinattrinternal(int i) { return &m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roommemberbinattrinternal() { return &m_roommemberbinattrinternal.emplace_back(); }
		bool has_optdata() const { return m_has_optdata; }
		const PresenceOptionData& optdata() const { return m_optdata; }
		PresenceOptionData* mutable_optdata() { m_has_optdata = true; return &m_optdata; }
		bool has_teamid() const { return m_has_teamid; }
		const uint8& teamid() const { return m_teamid; }
		uint8* mutable_teamid() { m_has_teamid = true; return &m_teamid; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		pb::text m_roompassword;
		pb::text m_joinroomgrouplabel;
		std::vector<BinAttr> m_roommemberbinattrinternal;
		PresenceOptionData m_optdata;
		bool m_has_optdata = false;
		uint8 m_teamid;
		bool m_has_teamid = false;
		mutable std::size_t m_cached_size = 0;
	};

	class JoinRoomResponse final
	{
	public:
		bool has_room_data() const { return m_has_room_data; }
		const RoomDataInternal& room_data() const { return m_room_data; }
		RoomDataInternal* mutable_room_data() { m_has_room_data = true; return &m_room_data; }
		const std::vector<Matching2SignalingInfo>& signaling_data() const { return m_signaling_data; }
		std::vector<Matching2SignalingInfo>* mutable_signaling_data() { return &m_signaling_data; }
		int signaling_data_size() const { return static_cast<int>(m_signaling_data.size()); }
		const Matching2SignalingInfo& signaling_data(int i) const { return m_signaling_data[static_cast<std::size_t>(i)]; }
		Matching2SignalingInfo* mutable_signaling_data(int i) { return &m_signaling_data[static_cast<std::size_t>(i)]; }
		Matching2SignalingInfo* add_signaling_data() { return &m_signaling_data.emplace_back(); }
		bool has_opt_param() const { return m_has_opt_param; }
		const OptParam& opt_param() const { return m_opt_param; }
		OptParam* mutable_opt_param() { m_has_opt_param = true; return &m_opt_param; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		RoomDataInternal m_room_data;
		bool m_has_room_data = false;
		std::vector<Matching2SignalingInfo> m_signaling_data;
		OptParam m_opt_param;
		bool m_has_opt_param = false;
		mutable std::size_t m_cached_size = 0;
	};

	class LeaveRoomRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		bool has_optdata() const { return m_has_optdata; }
		const PresenceOptionData& optdata() const { return m_optdata; }
		PresenceOptionData* mutable_optdata() { m_has_optdata = true; return &m_optdata; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		PresenceOptionData m_optdata;
		bool m_has_optdata = false;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomDataExternalListRequest final
	{
	public:
		const std::vector<std::uint64_t>& roomids() const { return m_roomids; }
		std::vector<std::uint64_t>* mutable_roomids() { return &m_roomids; }
		int roomids_size() const { return static_cast<int>(m_roomids.size()); }
		std::uint64_t roomids(int i) const { return m_roomids[static_cast<std::size_t>(i)]; }
		void add_roomids(std::uint64_t v) { m_roomids.push_back(v); }
		const std::vector<uint16>& attrids() const { return m_attrids; }
		std::vector<uint16>* mutable_attrids() { return &m_attrids; }
		int attrids_size() const { return static_cast<int>(m_attrids.size()); }
		const uint16& attrids(int i) const { return m_attrids[static_cast<std::size_t>(i)]; }
		uint16* mutable_attrids(int i) { return &m_attrids[static_cast<std::size_t>(i)]; }
		uint16* add_attrids() { return &m_attrids.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<std::uint64_t> m_roomids;
		mutable std::size_t m_roomids_packed = 0;
		std::vector<uint16> m_attrids;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomDataExternalListResponse final
	{
	public:
		const std::vector<RoomDataExternal>& rooms() const { return m_rooms; }
		std::vector<RoomDataExternal>* mutable_rooms() { return &m_rooms; }
		int rooms_size() const { return static_cast<int>(m_rooms.size()); }
		const RoomDataExternal& rooms(int i) const { return m_rooms[static_cast<std::size_t>(i)]; }
		RoomDataExternal* mutable_rooms(int i) { return &m_rooms[static_cast<std::size_t>(i)]; }
		RoomDataExternal* add_rooms() { return &m_rooms.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<RoomDataExternal> m_rooms;
		mutable std::size_t m_cached_size = 0;
	};

	class SetRoomDataExternalRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		const std::vector<IntAttr>& roomsearchableintattrexternal() const { return m_roomsearchableintattrexternal; }
		std::vector<IntAttr>* mutable_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal; }
		int roomsearchableintattrexternal_size() const { return static_cast<int>(m_roomsearchableintattrexternal.size()); }
		const IntAttr& roomsearchableintattrexternal(int i) const { return m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* mutable_roomsearchableintattrexternal(int i) { return &m_roomsearchableintattrexternal[static_cast<std::size_t>(i)]; }
		IntAttr* add_roomsearchableintattrexternal() { return &m_roomsearchableintattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roomsearchablebinattrexternal() const { return m_roomsearchablebinattrexternal; }
		std::vector<BinAttr>* mutable_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal; }
		int roomsearchablebinattrexternal_size() const { return static_cast<int>(m_roomsearchablebinattrexternal.size()); }
		const BinAttr& roomsearchablebinattrexternal(int i) const { return m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roomsearchablebinattrexternal(int i) { return &m_roomsearchablebinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roomsearchablebinattrexternal() { return &m_roomsearchablebinattrexternal.emplace_back(); }
		const std::vector<BinAttr>& roombinattrexternal() const { return m_roombinattrexternal; }
		std::vector<BinAttr>* mutable_roombinattrexternal() { return &m_roombinattrexternal; }
		int roombinattrexternal_size() const { return static_cast<int>(m_roombinattrexternal.size()); }
		const BinAttr& roombinattrexternal(int i) const { return m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roombinattrexternal(int i) { return &m_roombinattrexternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roombinattrexternal() { return &m_roombinattrexternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		std::vector<IntAttr> m_roomsearchableintattrexternal;
		std::vector<BinAttr> m_roomsearchablebinattrexternal;
		std::vector<BinAttr> m_roombinattrexternal;
		mutable std::size_t m_cached_size = 0;
	};

	class SetRoomDataInternalRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		std::uint32_t flagfilter() const { return m_flagfilter; }
		void set_flagfilter(std::uint32_t v) { m_flagfilter = v; }
		std::uint32_t flagattr() const { return m_flagattr; }
		void set_flagattr(std::uint32_t v) { m_flagattr = v; }
		const std::vector<BinAttr>& roombinattrinternal() const { return m_roombinattrinternal; }
		std::vector<BinAttr>* mutable_roombinattrinternal() { return &m_roombinattrinternal; }
		int roombinattrinternal_size() const { return static_cast<int>(m_roombinattrinternal.size()); }
		const BinAttr& roombinattrinternal(int i) const { return m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roombinattrinternal(int i) { return &m_roombinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roombinattrinternal() { return &m_roombinattrinternal.emplace_back(); }
		const std::vector<RoomGroupPasswordConfig>& passwordconfig() const { return m_passwordconfig; }
		std::vector<RoomGroupPasswordConfig>* mutable_passwordconfig() { return &m_passwordconfig; }
		int passwordconfig_size() const { return static_cast<int>(m_passwordconfig.size()); }
		const RoomGroupPasswordConfig& passwordconfig(int i) const { return m_passwordconfig[static_cast<std::size_t>(i)]; }
		RoomGroupPasswordConfig* mutable_passwordconfig(int i) { return &m_passwordconfig[static_cast<std::size_t>(i)]; }
		RoomGroupPasswordConfig* add_passwordconfig() { return &m_passwordconfig.emplace_back(); }
		const std::vector<std::uint64_t>& passwordslotmask() const { return m_passwordslotmask; }
		std::vector<std::uint64_t>* mutable_passwordslotmask() { return &m_passwordslotmask; }
		int passwordslotmask_size() const { return static_cast<int>(m_passwordslotmask.size()); }
		std::uint64_t passwordslotmask(int i) const { return m_passwordslotmask[static_cast<std::size_t>(i)]; }
		void add_passwordslotmask(std::uint64_t v) { m_passwordslotmask.push_back(v); }
		const std::vector<uint16>& ownerprivilegerank() const { return m_ownerprivilegerank; }
		std::vector<uint16>* mutable_ownerprivilegerank() { return &m_ownerprivilegerank; }
		int ownerprivilegerank_size() const { return static_cast<int>(m_ownerprivilegerank.size()); }
		const uint16& ownerprivilegerank(int i) const { return m_ownerprivilegerank[static_cast<std::size_t>(i)]; }
		uint16* mutable_ownerprivilegerank(int i) { return &m_ownerprivilegerank[static_cast<std::size_t>(i)]; }
		uint16* add_ownerprivilegerank() { return &m_ownerprivilegerank.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		std::uint32_t m_flagfilter = {};
		std::uint32_t m_flagattr = {};
		std::vector<BinAttr> m_roombinattrinternal;
		std::vector<RoomGroupPasswordConfig> m_passwordconfig;
		std::vector<std::uint64_t> m_passwordslotmask;
		mutable std::size_t m_passwordslotmask_packed = 0;
		std::vector<uint16> m_ownerprivilegerank;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomMemberDataInternalRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		bool has_memberid() const { return m_has_memberid; }
		const uint16& memberid() const { return m_memberid; }
		uint16* mutable_memberid() { m_has_memberid = true; return &m_memberid; }
		const std::vector<uint16>& attrid() const { return m_attrid; }
		std::vector<uint16>* mutable_attrid() { return &m_attrid; }
		int attrid_size() const { return static_cast<int>(m_attrid.size()); }
		const uint16& attrid(int i) const { return m_attrid[static_cast<std::size_t>(i)]; }
		uint16* mutable_attrid(int i) { return &m_attrid[static_cast<std::size_t>(i)]; }
		uint16* add_attrid() { return &m_attrid.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		uint16 m_memberid;
		bool m_has_memberid = false;
		std::vector<uint16> m_attrid;
		mutable std::size_t m_cached_size = 0;
	};

	class SetRoomMemberDataInternalRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		bool has_memberid() const { return m_has_memberid; }
		const uint16& memberid() const { return m_memberid; }
		uint16* mutable_memberid() { m_has_memberid = true; return &m_memberid; }
		bool has_teamid() const { return m_has_teamid; }
		const uint8& teamid() const { return m_teamid; }
		uint8* mutable_teamid() { m_has_teamid = true; return &m_teamid; }
		const std::vector<BinAttr>& roommemberbinattrinternal() const { return m_roommemberbinattrinternal; }
		std::vector<BinAttr>* mutable_roommemberbinattrinternal() { return &m_roommemberbinattrinternal; }
		int roommemberbinattrinternal_size() const { return static_cast<int>(m_roommemberbinattrinternal.size()); }
		const BinAttr& roommemberbinattrinternal(int i) const { return m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_roommemberbinattrinternal(int i) { return &m_roommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		BinAttr* add_roommemberbinattrinternal() { return &m_roommemberbinattrinternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		uint16 m_memberid;
		bool m_has_memberid = false;
		uint8 m_teamid;
		bool m_has_teamid = false;
		std::vector<BinAttr> m_roommemberbinattrinternal;
		mutable std::size_t m_cached_size = 0;
	};

	class SetUserInfo final
	{
	public:
		bool has_serverid() const { return m_has_serverid; }
		const uint16& serverid() const { return m_serverid; }
		uint16* mutable_serverid() { m_has_serverid = true; return &m_serverid; }
		const std::vector<BinAttr>& userbinattr() const { return m_userbinattr; }
		std::vector<BinAttr>* mutable_userbinattr() { return &m_userbinattr; }
		int userbinattr_size() const { return static_cast<int>(m_userbinattr.size()); }
		const BinAttr& userbinattr(int i) const { return m_userbinattr[static_cast<std::size_t>(i)]; }
		BinAttr* mutable_userbinattr(int i) { return &m_userbinattr[static_cast<std::size_t>(i)]; }
		BinAttr* add_userbinattr() { return &m_userbinattr.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_serverid;
		bool m_has_serverid = false;
		std::vector<BinAttr> m_userbinattr;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomDataInternalRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		const std::vector<uint16>& attrid() const { return m_attrid; }
		std::vector<uint16>* mutable_attrid() { return &m_attrid; }
		int attrid_size() const { return static_cast<int>(m_attrid.size()); }
		const uint16& attrid(int i) const { return m_attrid[static_cast<std::size_t>(i)]; }
		uint16* mutable_attrid(int i) { return &m_attrid[static_cast<std::size_t>(i)]; }
		uint16* add_attrid() { return &m_attrid.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		std::vector<uint16> m_attrid;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMemberUpdateInfo final
	{
	public:
		bool has_roommemberdatainternal() const { return m_has_roommemberdatainternal; }
		const RoomMemberDataInternal& roommemberdatainternal() const { return m_roommemberdatainternal; }
		RoomMemberDataInternal* mutable_roommemberdatainternal() { m_has_roommemberdatainternal = true; return &m_roommemberdatainternal; }
		bool has_eventcause() const { return m_has_eventcause; }
		const uint8& eventcause() const { return m_eventcause; }
		uint8* mutable_eventcause() { m_has_eventcause = true; return &m_eventcause; }
		bool has_optdata() const { return m_has_optdata; }
		const PresenceOptionData& optdata() const { return m_optdata; }
		PresenceOptionData* mutable_optdata() { m_has_optdata = true; return &m_optdata; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		RoomMemberDataInternal m_roommemberdatainternal;
		bool m_has_roommemberdatainternal = false;
		uint8 m_eventcause;
		bool m_has_eventcause = false;
		PresenceOptionData m_optdata;
		bool m_has_optdata = false;
		mutable std::size_t m_cached_size = 0;
	};

	class NotificationUserJoinedRoom final
	{
	public:
		std::uint64_t room_id() const { return m_room_id; }
		void set_room_id(std::uint64_t v) { m_room_id = v; }
		bool has_update_info() const { return m_has_update_info; }
		const RoomMemberUpdateInfo& update_info() const { return m_update_info; }
		RoomMemberUpdateInfo* mutable_update_info() { m_has_update_info = true; return &m_update_info; }
		bool has_signaling() const { return m_has_signaling; }
		const SignalingAddr& signaling() const { return m_signaling; }
		SignalingAddr* mutable_signaling() { m_has_signaling = true; return &m_signaling; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_room_id = {};
		RoomMemberUpdateInfo m_update_info;
		bool m_has_update_info = false;
		SignalingAddr m_signaling;
		bool m_has_signaling = false;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomUpdateInfo final
	{
	public:
		bool has_eventcause() const { return m_has_eventcause; }
		const uint8& eventcause() const { return m_eventcause; }
		uint8* mutable_eventcause() { m_has_eventcause = true; return &m_eventcause; }
		std::int32_t errorcode() const { return m_errorcode; }
		void set_errorcode(std::int32_t v) { m_errorcode = v; }
		bool has_optdata() const { return m_has_optdata; }
		const PresenceOptionData& optdata() const { return m_optdata; }
		PresenceOptionData* mutable_optdata() { m_has_optdata = true; return &m_optdata; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint8 m_eventcause;
		bool m_has_eventcause = false;
		std::int32_t m_errorcode = {};
		PresenceOptionData m_optdata;
		bool m_has_optdata = false;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomDataInternalUpdateInfo final
	{
	public:
		bool has_newroomdatainternal() const { return m_has_newroomdatainternal; }
		const RoomDataInternal& newroomdatainternal() const { return m_newroomdatainternal; }
		RoomDataInternal* mutable_newroomdatainternal() { m_has_newroomdatainternal = true; return &m_newroomdatainternal; }
		std::uint32_t prevflagattr() const { return m_prevflagattr; }
		void set_prevflagattr(std::uint32_t v) { m_prevflagattr = v; }
		std::uint64_t prevroompasswordslotmask() const { return m_prevroompasswordslotmask; }
		void set_prevroompasswordslotmask(std::uint64_t v) { m_prevroompasswordslotmask = v; }
		std::string_view newroomgroup() const { return m_newroomgroup.get(); }
		void set_newroomgroup(std::string_view v) { m_newroomgroup.set(v); }
		void set_newroomgroup(std::string&& v) { m_newroomgroup.set(std::move(v)); }
		void set_newroomgroup(const char* v) { m_newroomgroup.set(v); }
		void set_newroomgroup(const void* p, std::size_t n) { m_newroomgroup.set(p, n); }
		const std::vector<uint16>& newroombinattrinternal() const { return m_newroombinattrinternal; }
		std::vector<uint16>* mutable_newroombinattrinternal() { return &m_newroombinattrinternal; }
		int newroombinattrinternal_size() const { return static_cast<int>(m_newroombinattrinternal.size()); }
		const uint16& newroombinattrinternal(int i) const { return m_newroombinattrinternal[static_cast<std::size_t>(i)]; }
		uint16* mutable_newroombinattrinternal(int i) { return &m_newroombinattrinternal[static_cast<std::size_t>(i)]; }
		uint16* add_newroombinattrinternal() { return &m_newroombinattrinternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		RoomDataInternal m_newroomdatainternal;
		bool m_has_newroomdatainternal = false;
		std::uint32_t m_prevflagattr = {};
		std::uint64_t m_prevroompasswordslotmask = {};
		pb::text m_newroomgroup;
		std::vector<uint16> m_newroombinattrinternal;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMemberDataInternalUpdateInfo final
	{
	public:
		bool has_newroommemberdatainternal() const { return m_has_newroommemberdatainternal; }
		const RoomMemberDataInternal& newroommemberdatainternal() const { return m_newroommemberdatainternal; }
		RoomMemberDataInternal* mutable_newroommemberdatainternal() { m_has_newroommemberdatainternal = true; return &m_newroommemberdatainternal; }
		std::uint32_t prevflagattr() const { return m_prevflagattr; }
		void set_prevflagattr(std::uint32_t v) { m_prevflagattr = v; }
		bool has_prevteamid() const { return m_has_prevteamid; }
		const uint8& prevteamid() const { return m_prevteamid; }
		uint8* mutable_prevteamid() { m_has_prevteamid = true; return &m_prevteamid; }
		const std::vector<uint16>& newroommemberbinattrinternal() const { return m_newroommemberbinattrinternal; }
		std::vector<uint16>* mutable_newroommemberbinattrinternal() { return &m_newroommemberbinattrinternal; }
		int newroommemberbinattrinternal_size() const { return static_cast<int>(m_newroommemberbinattrinternal.size()); }
		const uint16& newroommemberbinattrinternal(int i) const { return m_newroommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		uint16* mutable_newroommemberbinattrinternal(int i) { return &m_newroommemberbinattrinternal[static_cast<std::size_t>(i)]; }
		uint16* add_newroommemberbinattrinternal() { return &m_newroommemberbinattrinternal.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		RoomMemberDataInternal m_newroommemberdatainternal;
		bool m_has_newroommemberdatainternal = false;
		std::uint32_t m_prevflagattr = {};
		uint8 m_prevteamid;
		bool m_has_prevteamid = false;
		std::vector<uint16> m_newroommemberbinattrinternal;
		mutable std::size_t m_cached_size = 0;
	};

	class GetPingInfoResponse final
	{
	public:
		bool has_serverid() const { return m_has_serverid; }
		const uint16& serverid() const { return m_serverid; }
		uint16* mutable_serverid() { m_has_serverid = true; return &m_serverid; }
		std::uint32_t worldid() const { return m_worldid; }
		void set_worldid(std::uint32_t v) { m_worldid = v; }
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		std::uint32_t rtt() const { return m_rtt; }
		void set_rtt(std::uint32_t v) { m_rtt = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		uint16 m_serverid;
		bool m_has_serverid = false;
		std::uint32_t m_worldid = {};
		std::uint64_t m_roomid = {};
		std::uint32_t m_rtt = {};
		mutable std::size_t m_cached_size = 0;
	};

	class SendRoomMessageRequest final
	{
	public:
		std::uint64_t roomid() const { return m_roomid; }
		void set_roomid(std::uint64_t v) { m_roomid = v; }
		bool has_casttype() const { return m_has_casttype; }
		const uint8& casttype() const { return m_casttype; }
		uint8* mutable_casttype() { m_has_casttype = true; return &m_casttype; }
		const std::vector<uint16>& dst() const { return m_dst; }
		std::vector<uint16>* mutable_dst() { return &m_dst; }
		int dst_size() const { return static_cast<int>(m_dst.size()); }
		const uint16& dst(int i) const { return m_dst[static_cast<std::size_t>(i)]; }
		uint16* mutable_dst(int i) { return &m_dst[static_cast<std::size_t>(i)]; }
		uint16* add_dst() { return &m_dst.emplace_back(); }
		std::string_view msg() const { return m_msg.get(); }
		void set_msg(std::string_view v) { m_msg.set(v); }
		void set_msg(std::string&& v) { m_msg.set(std::move(v)); }
		void set_msg(const char* v) { m_msg.set(v); }
		void set_msg(const void* p, std::size_t n) { m_msg.set(p, n); }
		bool has_option() const { return m_has_option; }
		const uint8& option() const { return m_option; }
		uint8* mutable_option() { m_has_option = true; return &m_option; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint64_t m_roomid = {};
		uint8 m_casttype;
		bool m_has_casttype = false;
		std::vector<uint16> m_dst;
		pb::text m_msg;
		uint8 m_option;
		bool m_has_option = false;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMessageInfo final
	{
	public:
		bool filtered() const { return m_filtered; }
		void set_filtered(bool v) { m_filtered = v; }
		bool has_casttype() const { return m_has_casttype; }
		const uint8& casttype() const { return m_casttype; }
		uint8* mutable_casttype() { m_has_casttype = true; return &m_casttype; }
		const std::vector<uint16>& dst() const { return m_dst; }
		std::vector<uint16>* mutable_dst() { return &m_dst; }
		int dst_size() const { return static_cast<int>(m_dst.size()); }
		const uint16& dst(int i) const { return m_dst[static_cast<std::size_t>(i)]; }
		uint16* mutable_dst(int i) { return &m_dst[static_cast<std::size_t>(i)]; }
		uint16* add_dst() { return &m_dst.emplace_back(); }
		bool has_srcmember() const { return m_has_srcmember; }
		const UserInfo& srcmember() const { return m_srcmember; }
		UserInfo* mutable_srcmember() { m_has_srcmember = true; return &m_srcmember; }
		std::string_view msg() const { return m_msg.get(); }
		void set_msg(std::string_view v) { m_msg.set(v); }
		void set_msg(std::string&& v) { m_msg.set(std::move(v)); }
		void set_msg(const char* v) { m_msg.set(v); }
		void set_msg(const void* p, std::size_t n) { m_msg.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		bool m_filtered = {};
		uint8 m_casttype;
		bool m_has_casttype = false;
		std::vector<uint16> m_dst;
		UserInfo m_srcmember;
		bool m_has_srcmember = false;
		pb::text m_msg;
		mutable std::size_t m_cached_size = 0;
	};

	class MessageDetails final
	{
	public:
		std::string_view communicationid() const { return m_communicationid.get(); }
		void set_communicationid(std::string_view v) { m_communicationid.set(v); }
		void set_communicationid(std::string&& v) { m_communicationid.set(std::move(v)); }
		void set_communicationid(const char* v) { m_communicationid.set(v); }
		void set_communicationid(const void* p, std::size_t n) { m_communicationid.set(p, n); }
		std::uint64_t msgid() const { return m_msgid; }
		void set_msgid(std::uint64_t v) { m_msgid = v; }
		bool has_maintype() const { return m_has_maintype; }
		const uint16& maintype() const { return m_maintype; }
		uint16* mutable_maintype() { m_has_maintype = true; return &m_maintype; }
		bool has_subtype() const { return m_has_subtype; }
		const uint16& subtype() const { return m_subtype; }
		uint16* mutable_subtype() { m_has_subtype = true; return &m_subtype; }
		std::uint32_t msgfeatures() const { return m_msgfeatures; }
		void set_msgfeatures(std::uint32_t v) { m_msgfeatures = v; }
		std::string_view subject() const { return m_subject.get(); }
		void set_subject(std::string_view v) { m_subject.set(v); }
		void set_subject(std::string&& v) { m_subject.set(std::move(v)); }
		void set_subject(const char* v) { m_subject.set(v); }
		void set_subject(const void* p, std::size_t n) { m_subject.set(p, n); }
		std::string_view body() const { return m_body.get(); }
		void set_body(std::string_view v) { m_body.set(v); }
		void set_body(std::string&& v) { m_body.set(std::move(v)); }
		void set_body(const char* v) { m_body.set(v); }
		void set_body(const void* p, std::size_t n) { m_body.set(p, n); }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_communicationid;
		std::uint64_t m_msgid = {};
		uint16 m_maintype;
		bool m_has_maintype = false;
		uint16 m_subtype;
		bool m_has_subtype = false;
		std::uint32_t m_msgfeatures = {};
		pb::text m_subject;
		pb::text m_body;
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class SendMessageRequest final
	{
	public:
		std::string_view message() const { return m_message.get(); }
		void set_message(std::string_view v) { m_message.set(v); }
		void set_message(std::string&& v) { m_message.set(std::move(v)); }
		void set_message(const char* v) { m_message.set(v); }
		void set_message(const void* p, std::size_t n) { m_message.set(p, n); }
		const std::vector<pb::text>& npids() const { return m_npids; }
		std::vector<pb::text>* mutable_npids() { return &m_npids; }
		int npids_size() const { return static_cast<int>(m_npids.size()); }
		std::string_view npids(int i) const { return m_npids[static_cast<std::size_t>(i)].get(); }
		void add_npids(std::string_view v) { m_npids.emplace_back().set(v); }
		void add_npids(std::string&& v) { m_npids.emplace_back().set(std::move(v)); }
		void add_npids(const char* v) { m_npids.emplace_back().set(v); }
		void add_npids(const void* p, std::size_t n) { m_npids.emplace_back().set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_message;
		std::vector<pb::text> m_npids;
		mutable std::size_t m_cached_size = 0;
	};

	class BoardInfo final
	{
	public:
		std::uint32_t ranklimit() const { return m_ranklimit; }
		void set_ranklimit(std::uint32_t v) { m_ranklimit = v; }
		std::uint32_t updatemode() const { return m_updatemode; }
		void set_updatemode(std::uint32_t v) { m_updatemode = v; }
		std::uint32_t sortmode() const { return m_sortmode; }
		void set_sortmode(std::uint32_t v) { m_sortmode = v; }
		std::uint32_t uploadnumlimit() const { return m_uploadnumlimit; }
		void set_uploadnumlimit(std::uint32_t v) { m_uploadnumlimit = v; }
		std::uint32_t uploadsizelimit() const { return m_uploadsizelimit; }
		void set_uploadsizelimit(std::uint32_t v) { m_uploadsizelimit = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_ranklimit = {};
		std::uint32_t m_updatemode = {};
		std::uint32_t m_sortmode = {};
		std::uint32_t m_uploadnumlimit = {};
		std::uint32_t m_uploadsizelimit = {};
		mutable std::size_t m_cached_size = 0;
	};

	class RecordScoreRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		std::int32_t pcid() const { return m_pcid; }
		void set_pcid(std::int32_t v) { m_pcid = v; }
		std::int64_t score() const { return m_score; }
		void set_score(std::int64_t v) { m_score = v; }
		std::string_view comment() const { return m_comment.get(); }
		void set_comment(std::string_view v) { m_comment.set(v); }
		void set_comment(std::string&& v) { m_comment.set(std::move(v)); }
		void set_comment(const char* v) { m_comment.set(v); }
		void set_comment(const void* p, std::size_t n) { m_comment.set(p, n); }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		std::int32_t m_pcid = {};
		std::int64_t m_score = {};
		pb::text m_comment;
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class GetScoreRangeRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		std::uint32_t startrank() const { return m_startrank; }
		void set_startrank(std::uint32_t v) { m_startrank = v; }
		std::uint32_t numranks() const { return m_numranks; }
		void set_numranks(std::uint32_t v) { m_numranks = v; }
		bool withcomment() const { return m_withcomment; }
		void set_withcomment(bool v) { m_withcomment = v; }
		bool withgameinfo() const { return m_withgameinfo; }
		void set_withgameinfo(bool v) { m_withgameinfo = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		std::uint32_t m_startrank = {};
		std::uint32_t m_numranks = {};
		bool m_withcomment = {};
		bool m_withgameinfo = {};
		mutable std::size_t m_cached_size = 0;
	};

	class ScoreNpIdPcId final
	{
	public:
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }
		std::int32_t pcid() const { return m_pcid; }
		void set_pcid(std::int32_t v) { m_pcid = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_npid;
		std::int32_t m_pcid = {};
		mutable std::size_t m_cached_size = 0;
	};

	class GetScoreNpIdRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		const std::vector<ScoreNpIdPcId>& npids() const { return m_npids; }
		std::vector<ScoreNpIdPcId>* mutable_npids() { return &m_npids; }
		int npids_size() const { return static_cast<int>(m_npids.size()); }
		const ScoreNpIdPcId& npids(int i) const { return m_npids[static_cast<std::size_t>(i)]; }
		ScoreNpIdPcId* mutable_npids(int i) { return &m_npids[static_cast<std::size_t>(i)]; }
		ScoreNpIdPcId* add_npids() { return &m_npids.emplace_back(); }
		bool withcomment() const { return m_withcomment; }
		void set_withcomment(bool v) { m_withcomment = v; }
		bool withgameinfo() const { return m_withgameinfo; }
		void set_withgameinfo(bool v) { m_withgameinfo = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		std::vector<ScoreNpIdPcId> m_npids;
		bool m_withcomment = {};
		bool m_withgameinfo = {};
		mutable std::size_t m_cached_size = 0;
	};

	class GetScoreFriendsRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		bool include_self() const { return m_include_self; }
		void set_include_self(bool v) { m_include_self = v; }
		std::uint32_t max() const { return m_max; }
		void set_max(std::uint32_t v) { m_max = v; }
		bool withcomment() const { return m_withcomment; }
		void set_withcomment(bool v) { m_withcomment = v; }
		bool withgameinfo() const { return m_withgameinfo; }
		void set_withgameinfo(bool v) { m_withgameinfo = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		bool m_include_self = {};
		std::uint32_t m_max = {};
		bool m_withcomment = {};
		bool m_withgameinfo = {};
		mutable std::size_t m_cached_size = 0;
	};

	class ScoreRankData final
	{
	public:
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }
		std::string_view onlinename() const { return m_onlinename.get(); }
		void set_onlinename(std::string_view v) { m_onlinename.set(v); }
		void set_onlinename(std::string&& v) { m_onlinename.set(std::move(v)); }
		void set_onlinename(const char* v) { m_onlinename.set(v); }
		void set_onlinename(const void* p, std::size_t n) { m_onlinename.set(p, n); }
		std::int32_t pcid() const { return m_pcid; }
		void set_pcid(std::int32_t v) { m_pcid = v; }
		std::uint32_t rank() const { return m_rank; }
		void set_rank(std::uint32_t v) { m_rank = v; }
		std::int64_t score() const { return m_score; }
		void set_score(std::int64_t v) { m_score = v; }
		bool hasgamedata() const { return m_hasgamedata; }
		void set_hasgamedata(bool v) { m_hasgamedata = v; }
		std::uint64_t recorddate() const { return m_recorddate; }
		void set_recorddate(std::uint64_t v) { m_recorddate = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_npid;
		pb::text m_onlinename;
		std::int32_t m_pcid = {};
		std::uint32_t m_rank = {};
		std::int64_t m_score = {};
		bool m_hasgamedata = {};
		std::uint64_t m_recorddate = {};
		mutable std::size_t m_cached_size = 0;
	};

	class ScoreInfo final
	{
	public:
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class GetScoreResponse final
	{
	public:
		const std::vector<ScoreRankData>& rankarray() const { return m_rankarray; }
		std::vector<ScoreRankData>* mutable_rankarray() { return &m_rankarray; }
		int rankarray_size() const { return static_cast<int>(m_rankarray.size()); }
		const ScoreRankData& rankarray(int i) const { return m_rankarray[static_cast<std::size_t>(i)]; }
		ScoreRankData* mutable_rankarray(int i) { return &m_rankarray[static_cast<std::size_t>(i)]; }
		ScoreRankData* add_rankarray() { return &m_rankarray.emplace_back(); }
		const std::vector<pb::text>& commentarray() const { return m_commentarray; }
		std::vector<pb::text>* mutable_commentarray() { return &m_commentarray; }
		int commentarray_size() const { return static_cast<int>(m_commentarray.size()); }
		std::string_view commentarray(int i) const { return m_commentarray[static_cast<std::size_t>(i)].get(); }
		void add_commentarray(std::string_view v) { m_commentarray.emplace_back().set(v); }
		void add_commentarray(std::string&& v) { m_commentarray.emplace_back().set(std::move(v)); }
		void add_commentarray(const char* v) { m_commentarray.emplace_back().set(v); }
		void add_commentarray(const void* p, std::size_t n) { m_commentarray.emplace_back().set(p, n); }
		const std::vector<ScoreInfo>& infoarray() const { return m_infoarray; }
		std::vector<ScoreInfo>* mutable_infoarray() { return &m_infoarray; }
		int infoarray_size() const { return static_cast<int>(m_infoarray.size()); }
		const ScoreInfo& infoarray(int i) const { return m_infoarray[static_cast<std::size_t>(i)]; }
		ScoreInfo* mutable_infoarray(int i) { return &m_infoarray[static_cast<std::size_t>(i)]; }
		ScoreInfo* add_infoarray() { return &m_infoarray.emplace_back(); }
		std::uint64_t lastsortdate() const { return m_lastsortdate; }
		void set_lastsortdate(std::uint64_t v) { m_lastsortdate = v; }
		std::uint32_t totalrecord() const { return m_totalrecord; }
		void set_totalrecord(std::uint32_t v) { m_totalrecord = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<ScoreRankData> m_rankarray;
		std::vector<pb::text> m_commentarray;
		std::vector<ScoreInfo> m_infoarray;
		std::uint64_t m_lastsortdate = {};
		std::uint32_t m_totalrecord = {};
		mutable std::size_t m_cached_size = 0;
	};

	class RecordScoreGameDataRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		std::int32_t pcid() const { return m_pcid; }
		void set_pcid(std::int32_t v) { m_pcid = v; }
		std::int64_t score() const { return m_score; }
		void set_score(std::int64_t v) { m_score = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		std::int32_t m_pcid = {};
		std::int64_t m_score = {};
		mutable std::size_t m_cached_size = 0;
	};

	class GetScoreGameDataRequest final
	{
	public:
		std::uint32_t boardid() const { return m_boardid; }
		void set_boardid(std::uint32_t v) { m_boardid = v; }
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }
		std::int32_t pcid() const { return m_pcid; }
		void set_pcid(std::int32_t v) { m_pcid = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_boardid = {};
		pb::text m_npid;
		std::int32_t m_pcid = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusUser final
	{
	public:
		bool vuser() const { return m_vuser; }
		void set_vuser(bool v) { m_vuser = v; }
		std::string_view npid() const { return m_npid.get(); }
		void set_npid(std::string_view v) { m_npid.set(v); }
		void set_npid(std::string&& v) { m_npid.set(std::move(v)); }
		void set_npid(const char* v) { m_npid.set(v); }
		void set_npid(const void* p, std::size_t n) { m_npid.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		bool m_vuser = {};
		pb::text m_npid;
		mutable std::size_t m_cached_size = 0;
	};

	class TusVariable final
	{
	public:
		std::string_view ownerid() const { return m_ownerid.get(); }
		void set_ownerid(std::string_view v) { m_ownerid.set(v); }
		void set_ownerid(std::string&& v) { m_ownerid.set(std::move(v)); }
		void set_ownerid(const char* v) { m_ownerid.set(v); }
		void set_ownerid(const void* p, std::size_t n) { m_ownerid.set(p, n); }
		bool hasdata() const { return m_hasdata; }
		void set_hasdata(bool v) { m_hasdata = v; }
		std::uint64_t lastchangeddate() const { return m_lastchangeddate; }
		void set_lastchangeddate(std::uint64_t v) { m_lastchangeddate = v; }
		std::string_view lastchangedauthorid() const { return m_lastchangedauthorid.get(); }
		void set_lastchangedauthorid(std::string_view v) { m_lastchangedauthorid.set(v); }
		void set_lastchangedauthorid(std::string&& v) { m_lastchangedauthorid.set(std::move(v)); }
		void set_lastchangedauthorid(const char* v) { m_lastchangedauthorid.set(v); }
		void set_lastchangedauthorid(const void* p, std::size_t n) { m_lastchangedauthorid.set(p, n); }
		std::int64_t variable() const { return m_variable; }
		void set_variable(std::int64_t v) { m_variable = v; }
		std::int64_t oldvariable() const { return m_oldvariable; }
		void set_oldvariable(std::int64_t v) { m_oldvariable = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_ownerid;
		bool m_hasdata = {};
		std::uint64_t m_lastchangeddate = {};
		pb::text m_lastchangedauthorid;
		std::int64_t m_variable = {};
		std::int64_t m_oldvariable = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusVarResponse final
	{
	public:
		const std::vector<TusVariable>& vars() const { return m_vars; }
		std::vector<TusVariable>* mutable_vars() { return &m_vars; }
		int vars_size() const { return static_cast<int>(m_vars.size()); }
		const TusVariable& vars(int i) const { return m_vars[static_cast<std::size_t>(i)]; }
		TusVariable* mutable_vars(int i) { return &m_vars[static_cast<std::size_t>(i)]; }
		TusVariable* add_vars() { return &m_vars.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<TusVariable> m_vars;
		mutable std::size_t m_cached_size = 0;
	};

	class TusSetMultiSlotVariableRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		const std::vector<std::int32_t>& slotidarray() const { return m_slotidarray; }
		std::vector<std::int32_t>* mutable_slotidarray() { return &m_slotidarray; }
		int slotidarray_size() const { return static_cast<int>(m_slotidarray.size()); }
		std::int32_t slotidarray(int i) const { return m_slotidarray[static_cast<std::size_t>(i)]; }
		void add_slotidarray(std::int32_t v) { m_slotidarray.push_back(v); }
		const std::vector<std::int64_t>& variablearray() const { return m_variablearray; }
		std::vector<std::int64_t>* mutable_variablearray() { return &m_variablearray; }
		int variablearray_size() const { return static_cast<int>(m_variablearray.size()); }
		std::int64_t variablearray(int i) const { return m_variablearray[static_cast<std::size_t>(i)]; }
		void add_variablearray(std::int64_t v) { m_variablearray.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::vector<std::int32_t> m_slotidarray;
		mutable std::size_t m_slotidarray_packed = 0;
		std::vector<std::int64_t> m_variablearray;
		mutable std::size_t m_variablearray_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetMultiSlotVariableRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		const std::vector<std::int32_t>& slotidarray() const { return m_slotidarray; }
		std::vector<std::int32_t>* mutable_slotidarray() { return &m_slotidarray; }
		int slotidarray_size() const { return static_cast<int>(m_slotidarray.size()); }
		std::int32_t slotidarray(int i) const { return m_slotidarray[static_cast<std::size_t>(i)]; }
		void add_slotidarray(std::int32_t v) { m_slotidarray.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::vector<std::int32_t> m_slotidarray;
		mutable std::size_t m_slotidarray_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetMultiUserVariableRequest final
	{
	public:
		const std::vector<TusUser>& users() const { return m_users; }
		std::vector<TusUser>* mutable_users() { return &m_users; }
		int users_size() const { return static_cast<int>(m_users.size()); }
		const TusUser& users(int i) const { return m_users[static_cast<std::size_t>(i)]; }
		TusUser* mutable_users(int i) { return &m_users[static_cast<std::size_t>(i)]; }
		TusUser* add_users() { return &m_users.emplace_back(); }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<TusUser> m_users;
		std::int32_t m_slotid = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetFriendsVariableRequest final
	{
	public:
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }
		bool includeself() const { return m_includeself; }
		void set_includeself(bool v) { m_includeself = v; }
		std::int32_t sorttype() const { return m_sorttype; }
		void set_sorttype(std::int32_t v) { m_sorttype = v; }
		std::uint32_t arraynum() const { return m_arraynum; }
		void set_arraynum(std::uint32_t v) { m_arraynum = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::int32_t m_slotid = {};
		bool m_includeself = {};
		std::int32_t m_sorttype = {};
		std::uint32_t m_arraynum = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusAddAndGetVariableRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }
		std::int64_t invariable() const { return m_invariable; }
		void set_invariable(std::int64_t v) { m_invariable = v; }
		const std::vector<std::uint64_t>& islastchangeddate() const { return m_islastchangeddate; }
		std::vector<std::uint64_t>* mutable_islastchangeddate() { return &m_islastchangeddate; }
		int islastchangeddate_size() const { return static_cast<int>(m_islastchangeddate.size()); }
		std::uint64_t islastchangeddate(int i) const { return m_islastchangeddate[static_cast<std::size_t>(i)]; }
		void add_islastchangeddate(std::uint64_t v) { m_islastchangeddate.push_back(v); }
		std::string_view islastchangedauthorid() const { return m_islastchangedauthorid.get(); }
		void set_islastchangedauthorid(std::string_view v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(std::string&& v) { m_islastchangedauthorid.set(std::move(v)); }
		void set_islastchangedauthorid(const char* v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(const void* p, std::size_t n) { m_islastchangedauthorid.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::int32_t m_slotid = {};
		std::int64_t m_invariable = {};
		std::vector<std::uint64_t> m_islastchangeddate;
		mutable std::size_t m_islastchangeddate_packed = 0;
		pb::text m_islastchangedauthorid;
		mutable std::size_t m_cached_size = 0;
	};

	class TusTryAndSetVariableRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }
		std::int32_t opetype() const { return m_opetype; }
		void set_opetype(std::int32_t v) { m_opetype = v; }
		std::int64_t variable() const { return m_variable; }
		void set_variable(std::int64_t v) { m_variable = v; }
		const std::vector<std::uint64_t>& islastchangeddate() const { return m_islastchangeddate; }
		std::vector<std::uint64_t>* mutable_islastchangeddate() { return &m_islastchangeddate; }
		int islastchangeddate_size() const { return static_cast<int>(m_islastchangeddate.size()); }
		std::uint64_t islastchangeddate(int i) const { return m_islastchangeddate[static_cast<std::size_t>(i)]; }
		void add_islastchangeddate(std::uint64_t v) { m_islastchangeddate.push_back(v); }
		std::string_view islastchangedauthorid() const { return m_islastchangedauthorid.get(); }
		void set_islastchangedauthorid(std::string_view v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(std::string&& v) { m_islastchangedauthorid.set(std::move(v)); }
		void set_islastchangedauthorid(const char* v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(const void* p, std::size_t n) { m_islastchangedauthorid.set(p, n); }
		const std::vector<std::int64_t>& comparevalue() const { return m_comparevalue; }
		std::vector<std::int64_t>* mutable_comparevalue() { return &m_comparevalue; }
		int comparevalue_size() const { return static_cast<int>(m_comparevalue.size()); }
		std::int64_t comparevalue(int i) const { return m_comparevalue[static_cast<std::size_t>(i)]; }
		void add_comparevalue(std::int64_t v) { m_comparevalue.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::int32_t m_slotid = {};
		std::int32_t m_opetype = {};
		std::int64_t m_variable = {};
		std::vector<std::uint64_t> m_islastchangeddate;
		mutable std::size_t m_islastchangeddate_packed = 0;
		pb::text m_islastchangedauthorid;
		std::vector<std::int64_t> m_comparevalue;
		mutable std::size_t m_comparevalue_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class TusDeleteMultiSlotVariableRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		const std::vector<std::int32_t>& slotidarray() const { return m_slotidarray; }
		std::vector<std::int32_t>* mutable_slotidarray() { return &m_slotidarray; }
		int slotidarray_size() const { return static_cast<int>(m_slotidarray.size()); }
		std::int32_t slotidarray(int i) const { return m_slotidarray[static_cast<std::size_t>(i)]; }
		void add_slotidarray(std::int32_t v) { m_slotidarray.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::vector<std::int32_t> m_slotidarray;
		mutable std::size_t m_slotidarray_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class TusSetDataRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }
		std::string_view info() const { return m_info.get(); }
		void set_info(std::string_view v) { m_info.set(v); }
		void set_info(std::string&& v) { m_info.set(std::move(v)); }
		void set_info(const char* v) { m_info.set(v); }
		void set_info(const void* p, std::size_t n) { m_info.set(p, n); }
		const std::vector<std::uint64_t>& islastchangeddate() const { return m_islastchangeddate; }
		std::vector<std::uint64_t>* mutable_islastchangeddate() { return &m_islastchangeddate; }
		int islastchangeddate_size() const { return static_cast<int>(m_islastchangeddate.size()); }
		std::uint64_t islastchangeddate(int i) const { return m_islastchangeddate[static_cast<std::size_t>(i)]; }
		void add_islastchangeddate(std::uint64_t v) { m_islastchangeddate.push_back(v); }
		std::string_view islastchangedauthorid() const { return m_islastchangedauthorid.get(); }
		void set_islastchangedauthorid(std::string_view v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(std::string&& v) { m_islastchangedauthorid.set(std::move(v)); }
		void set_islastchangedauthorid(const char* v) { m_islastchangedauthorid.set(v); }
		void set_islastchangedauthorid(const void* p, std::size_t n) { m_islastchangedauthorid.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::int32_t m_slotid = {};
		pb::text m_data;
		pb::text m_info;
		std::vector<std::uint64_t> m_islastchangeddate;
		mutable std::size_t m_islastchangeddate_packed = 0;
		pb::text m_islastchangedauthorid;
		mutable std::size_t m_cached_size = 0;
	};

	class TusDataStatus final
	{
	public:
		std::string_view ownerid() const { return m_ownerid.get(); }
		void set_ownerid(std::string_view v) { m_ownerid.set(v); }
		void set_ownerid(std::string&& v) { m_ownerid.set(std::move(v)); }
		void set_ownerid(const char* v) { m_ownerid.set(v); }
		void set_ownerid(const void* p, std::size_t n) { m_ownerid.set(p, n); }
		bool hasdata() const { return m_hasdata; }
		void set_hasdata(bool v) { m_hasdata = v; }
		std::uint64_t lastchangeddate() const { return m_lastchangeddate; }
		void set_lastchangeddate(std::uint64_t v) { m_lastchangeddate = v; }
		std::string_view lastchangedauthorid() const { return m_lastchangedauthorid.get(); }
		void set_lastchangedauthorid(std::string_view v) { m_lastchangedauthorid.set(v); }
		void set_lastchangedauthorid(std::string&& v) { m_lastchangedauthorid.set(std::move(v)); }
		void set_lastchangedauthorid(const char* v) { m_lastchangedauthorid.set(v); }
		void set_lastchangedauthorid(const void* p, std::size_t n) { m_lastchangedauthorid.set(p, n); }
		std::string_view info() const { return m_info.get(); }
		void set_info(std::string_view v) { m_info.set(v); }
		void set_info(std::string&& v) { m_info.set(std::move(v)); }
		void set_info(const char* v) { m_info.set(v); }
		void set_info(const void* p, std::size_t n) { m_info.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_ownerid;
		bool m_hasdata = {};
		std::uint64_t m_lastchangeddate = {};
		pb::text m_lastchangedauthorid;
		pb::text m_info;
		mutable std::size_t m_cached_size = 0;
	};

	class TusData final
	{
	public:
		bool has_status() const { return m_has_status; }
		const TusDataStatus& status() const { return m_status; }
		TusDataStatus* mutable_status() { m_has_status = true; return &m_status; }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusDataStatus m_status;
		bool m_has_status = false;
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class TusDataStatusResponse final
	{
	public:
		const std::vector<TusDataStatus>& status() const { return m_status; }
		std::vector<TusDataStatus>* mutable_status() { return &m_status; }
		int status_size() const { return static_cast<int>(m_status.size()); }
		const TusDataStatus& status(int i) const { return m_status[static_cast<std::size_t>(i)]; }
		TusDataStatus* mutable_status(int i) { return &m_status[static_cast<std::size_t>(i)]; }
		TusDataStatus* add_status() { return &m_status.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<TusDataStatus> m_status;
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetDataRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::int32_t m_slotid = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetMultiSlotDataStatusRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		const std::vector<std::int32_t>& slotidarray() const { return m_slotidarray; }
		std::vector<std::int32_t>* mutable_slotidarray() { return &m_slotidarray; }
		int slotidarray_size() const { return static_cast<int>(m_slotidarray.size()); }
		std::int32_t slotidarray(int i) const { return m_slotidarray[static_cast<std::size_t>(i)]; }
		void add_slotidarray(std::int32_t v) { m_slotidarray.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::vector<std::int32_t> m_slotidarray;
		mutable std::size_t m_slotidarray_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetMultiUserDataStatusRequest final
	{
	public:
		const std::vector<TusUser>& users() const { return m_users; }
		std::vector<TusUser>* mutable_users() { return &m_users; }
		int users_size() const { return static_cast<int>(m_users.size()); }
		const TusUser& users(int i) const { return m_users[static_cast<std::size_t>(i)]; }
		TusUser* mutable_users(int i) { return &m_users[static_cast<std::size_t>(i)]; }
		TusUser* add_users() { return &m_users.emplace_back(); }
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<TusUser> m_users;
		std::int32_t m_slotid = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusGetFriendsDataStatusRequest final
	{
	public:
		std::int32_t slotid() const { return m_slotid; }
		void set_slotid(std::int32_t v) { m_slotid = v; }
		bool includeself() const { return m_includeself; }
		void set_includeself(bool v) { m_includeself = v; }
		std::int32_t sorttype() const { return m_sorttype; }
		void set_sorttype(std::int32_t v) { m_sorttype = v; }
		std::uint32_t arraynum() const { return m_arraynum; }
		void set_arraynum(std::uint32_t v) { m_arraynum = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::int32_t m_slotid = {};
		bool m_includeself = {};
		std::int32_t m_sorttype = {};
		std::uint32_t m_arraynum = {};
		mutable std::size_t m_cached_size = 0;
	};

	class TusDeleteMultiSlotDataRequest final
	{
	public:
		bool has_user() const { return m_has_user; }
		const TusUser& user() const { return m_user; }
		TusUser* mutable_user() { m_has_user = true; return &m_user; }
		const std::vector<std::int32_t>& slotidarray() const { return m_slotidarray; }
		std::vector<std::int32_t>* mutable_slotidarray() { return &m_slotidarray; }
		int slotidarray_size() const { return static_cast<int>(m_slotidarray.size()); }
		std::int32_t slotidarray(int i) const { return m_slotidarray[static_cast<std::size_t>(i)]; }
		void add_slotidarray(std::int32_t v) { m_slotidarray.push_back(v); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		TusUser m_user;
		bool m_has_user = false;
		std::vector<std::int32_t> m_slotidarray;
		mutable std::size_t m_slotidarray_packed = 0;
		mutable std::size_t m_cached_size = 0;
	};

	class SetPresenceRequest final
	{
	public:
		std::string_view title() const { return m_title.get(); }
		void set_title(std::string_view v) { m_title.set(v); }
		void set_title(std::string&& v) { m_title.set(std::move(v)); }
		void set_title(const char* v) { m_title.set(v); }
		void set_title(const void* p, std::size_t n) { m_title.set(p, n); }
		std::string_view status() const { return m_status.get(); }
		void set_status(std::string_view v) { m_status.set(v); }
		void set_status(std::string&& v) { m_status.set(std::move(v)); }
		void set_status(const char* v) { m_status.set(v); }
		void set_status(const void* p, std::size_t n) { m_status.set(p, n); }
		std::string_view comment() const { return m_comment.get(); }
		void set_comment(std::string_view v) { m_comment.set(v); }
		void set_comment(std::string&& v) { m_comment.set(std::move(v)); }
		void set_comment(const char* v) { m_comment.set(v); }
		void set_comment(const void* p, std::size_t n) { m_comment.set(p, n); }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_title;
		pb::text m_status;
		pb::text m_comment;
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingSearchCondition final
	{
	public:
		std::uint32_t attr_type() const { return m_attr_type; }
		void set_attr_type(std::uint32_t v) { m_attr_type = v; }
		std::uint32_t attr_id() const { return m_attr_id; }
		void set_attr_id(std::uint32_t v) { m_attr_id = v; }
		std::uint32_t comp_op() const { return m_comp_op; }
		void set_comp_op(std::uint32_t v) { m_comp_op = v; }
		std::uint32_t comp_value() const { return m_comp_value; }
		void set_comp_value(std::uint32_t v) { m_comp_value = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_attr_type = {};
		std::uint32_t m_attr_id = {};
		std::uint32_t m_comp_op = {};
		std::uint32_t m_comp_value = {};
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingAttr final
	{
	public:
		std::uint32_t attr_type() const { return m_attr_type; }
		void set_attr_type(std::uint32_t v) { m_attr_type = v; }
		std::uint32_t attr_id() const { return m_attr_id; }
		void set_attr_id(std::uint32_t v) { m_attr_id = v; }
		std::uint32_t num() const { return m_num; }
		void set_num(std::uint32_t v) { m_num = v; }
		std::string_view data() const { return m_data.get(); }
		void set_data(std::string_view v) { m_data.set(v); }
		void set_data(std::string&& v) { m_data.set(std::move(v)); }
		void set_data(const char* v) { m_data.set(v); }
		void set_data(const void* p, std::size_t n) { m_data.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_attr_type = {};
		std::uint32_t m_attr_id = {};
		std::uint32_t m_num = {};
		pb::text m_data;
		mutable std::size_t m_cached_size = 0;
	};

	class CreateRoomGUIRequest final
	{
	public:
		std::uint32_t total_slots() const { return m_total_slots; }
		void set_total_slots(std::uint32_t v) { m_total_slots = v; }
		std::uint32_t private_slots() const { return m_private_slots; }
		void set_private_slots(std::uint32_t v) { m_private_slots = v; }
		bool privilege_grant() const { return m_privilege_grant; }
		void set_privilege_grant(bool v) { m_privilege_grant = v; }
		bool stealth() const { return m_stealth; }
		void set_stealth(bool v) { m_stealth = v; }
		const std::vector<MatchingAttr>& game_attrs() const { return m_game_attrs; }
		std::vector<MatchingAttr>* mutable_game_attrs() { return &m_game_attrs; }
		int game_attrs_size() const { return static_cast<int>(m_game_attrs.size()); }
		const MatchingAttr& game_attrs(int i) const { return m_game_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* mutable_game_attrs(int i) { return &m_game_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* add_game_attrs() { return &m_game_attrs.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_total_slots = {};
		std::uint32_t m_private_slots = {};
		bool m_privilege_grant = {};
		bool m_stealth = {};
		std::vector<MatchingAttr> m_game_attrs;
		mutable std::size_t m_cached_size = 0;
	};

	class GUIUserInfo final
	{
	public:
		bool has_info() const { return m_has_info; }
		const UserInfo& info() const { return m_info; }
		UserInfo* mutable_info() { m_has_info = true; return &m_info; }
		bool owner() const { return m_owner; }
		void set_owner(bool v) { m_owner = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		UserInfo m_info;
		bool m_has_info = false;
		bool m_owner = {};
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingRoomStatus final
	{
	public:
		std::string_view id() const { return m_id.get(); }
		void set_id(std::string_view v) { m_id.set(v); }
		void set_id(std::string&& v) { m_id.set(std::move(v)); }
		void set_id(const char* v) { m_id.set(v); }
		void set_id(const void* p, std::size_t n) { m_id.set(p, n); }
		const std::vector<GUIUserInfo>& members() const { return m_members; }
		std::vector<GUIUserInfo>* mutable_members() { return &m_members; }
		int members_size() const { return static_cast<int>(m_members.size()); }
		const GUIUserInfo& members(int i) const { return m_members[static_cast<std::size_t>(i)]; }
		GUIUserInfo* mutable_members(int i) { return &m_members[static_cast<std::size_t>(i)]; }
		GUIUserInfo* add_members() { return &m_members.emplace_back(); }
		std::string_view kick_actor() const { return m_kick_actor.get(); }
		void set_kick_actor(std::string_view v) { m_kick_actor.set(v); }
		void set_kick_actor(std::string&& v) { m_kick_actor.set(std::move(v)); }
		void set_kick_actor(const char* v) { m_kick_actor.set(v); }
		void set_kick_actor(const void* p, std::size_t n) { m_kick_actor.set(p, n); }
		std::string_view opt() const { return m_opt.get(); }
		void set_opt(std::string_view v) { m_opt.set(v); }
		void set_opt(std::string&& v) { m_opt.set(std::move(v)); }
		void set_opt(const char* v) { m_opt.set(v); }
		void set_opt(const void* p, std::size_t n) { m_opt.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_id;
		std::vector<GUIUserInfo> m_members;
		pb::text m_kick_actor;
		pb::text m_opt;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomListGUIRequest final
	{
	public:
		std::uint32_t range_start() const { return m_range_start; }
		void set_range_start(std::uint32_t v) { m_range_start = v; }
		std::uint32_t range_max() const { return m_range_max; }
		void set_range_max(std::uint32_t v) { m_range_max = v; }
		const std::vector<MatchingSearchCondition>& conds() const { return m_conds; }
		std::vector<MatchingSearchCondition>* mutable_conds() { return &m_conds; }
		int conds_size() const { return static_cast<int>(m_conds.size()); }
		const MatchingSearchCondition& conds(int i) const { return m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* mutable_conds(int i) { return &m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* add_conds() { return &m_conds.emplace_back(); }
		const std::vector<MatchingAttr>& attrs() const { return m_attrs; }
		std::vector<MatchingAttr>* mutable_attrs() { return &m_attrs; }
		int attrs_size() const { return static_cast<int>(m_attrs.size()); }
		const MatchingAttr& attrs(int i) const { return m_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* mutable_attrs(int i) { return &m_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* add_attrs() { return &m_attrs.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_range_start = {};
		std::uint32_t m_range_max = {};
		std::vector<MatchingSearchCondition> m_conds;
		std::vector<MatchingAttr> m_attrs;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingRoom final
	{
	public:
		std::string_view id() const { return m_id.get(); }
		void set_id(std::string_view v) { m_id.set(v); }
		void set_id(std::string&& v) { m_id.set(std::move(v)); }
		void set_id(const char* v) { m_id.set(v); }
		void set_id(const void* p, std::size_t n) { m_id.set(p, n); }
		const std::vector<MatchingAttr>& attr() const { return m_attr; }
		std::vector<MatchingAttr>* mutable_attr() { return &m_attr; }
		int attr_size() const { return static_cast<int>(m_attr.size()); }
		const MatchingAttr& attr(int i) const { return m_attr[static_cast<std::size_t>(i)]; }
		MatchingAttr* mutable_attr(int i) { return &m_attr[static_cast<std::size_t>(i)]; }
		MatchingAttr* add_attr() { return &m_attr.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_id;
		std::vector<MatchingAttr> m_attr;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingRoomList final
	{
	public:
		std::uint32_t start() const { return m_start; }
		void set_start(std::uint32_t v) { m_start = v; }
		std::uint32_t total() const { return m_total; }
		void set_total(std::uint32_t v) { m_total = v; }
		const std::vector<MatchingRoom>& rooms() const { return m_rooms; }
		std::vector<MatchingRoom>* mutable_rooms() { return &m_rooms; }
		int rooms_size() const { return static_cast<int>(m_rooms.size()); }
		const MatchingRoom& rooms(int i) const { return m_rooms[static_cast<std::size_t>(i)]; }
		MatchingRoom* mutable_rooms(int i) { return &m_rooms[static_cast<std::size_t>(i)]; }
		MatchingRoom* add_rooms() { return &m_rooms.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::uint32_t m_start = {};
		std::uint32_t m_total = {};
		std::vector<MatchingRoom> m_rooms;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingGuiRoomId final
	{
	public:
		std::string_view id() const { return m_id.get(); }
		void set_id(std::string_view v) { m_id.set(v); }
		void set_id(std::string&& v) { m_id.set(std::move(v)); }
		void set_id(const char* v) { m_id.set(v); }
		void set_id(const void* p, std::size_t n) { m_id.set(p, n); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_id;
		mutable std::size_t m_cached_size = 0;
	};

	class SetRoomSearchFlagGUI final
	{
	public:
		std::string_view roomid() const { return m_roomid.get(); }
		void set_roomid(std::string_view v) { m_roomid.set(v); }
		void set_roomid(std::string&& v) { m_roomid.set(std::move(v)); }
		void set_roomid(const char* v) { m_roomid.set(v); }
		void set_roomid(const void* p, std::size_t n) { m_roomid.set(p, n); }
		bool stealth() const { return m_stealth; }
		void set_stealth(bool v) { m_stealth = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		pb::text m_roomid;
		bool m_stealth = {};
		mutable std::size_t m_cached_size = 0;
	};

	class QuickMatchGUIRequest final
	{
	public:
		const std::vector<MatchingSearchCondition>& conds() const { return m_conds; }
		std::vector<MatchingSearchCondition>* mutable_conds() { return &m_conds; }
		int conds_size() const { return static_cast<int>(m_conds.size()); }
		const MatchingSearchCondition& conds(int i) const { return m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* mutable_conds(int i) { return &m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* add_conds() { return &m_conds.emplace_back(); }
		std::uint32_t available_num() const { return m_available_num; }
		void set_available_num(std::uint32_t v) { m_available_num = v; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<MatchingSearchCondition> m_conds;
		std::uint32_t m_available_num = {};
		mutable std::size_t m_cached_size = 0;
	};

	class SearchJoinRoomGUIRequest final
	{
	public:
		const std::vector<MatchingSearchCondition>& conds() const { return m_conds; }
		std::vector<MatchingSearchCondition>* mutable_conds() { return &m_conds; }
		int conds_size() const { return static_cast<int>(m_conds.size()); }
		const MatchingSearchCondition& conds(int i) const { return m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* mutable_conds(int i) { return &m_conds[static_cast<std::size_t>(i)]; }
		MatchingSearchCondition* add_conds() { return &m_conds.emplace_back(); }
		const std::vector<MatchingAttr>& attrs() const { return m_attrs; }
		std::vector<MatchingAttr>* mutable_attrs() { return &m_attrs; }
		int attrs_size() const { return static_cast<int>(m_attrs.size()); }
		const MatchingAttr& attrs(int i) const { return m_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* mutable_attrs(int i) { return &m_attrs[static_cast<std::size_t>(i)]; }
		MatchingAttr* add_attrs() { return &m_attrs.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<MatchingSearchCondition> m_conds;
		std::vector<MatchingAttr> m_attrs;
		mutable std::size_t m_cached_size = 0;
	};

	class MatchingSearchJoinRoomInfo final
	{
	public:
		bool has_room() const { return m_has_room; }
		const MatchingRoomStatus& room() const { return m_room; }
		MatchingRoomStatus* mutable_room() { m_has_room = true; return &m_room; }
		const std::vector<MatchingAttr>& attr() const { return m_attr; }
		std::vector<MatchingAttr>* mutable_attr() { return &m_attr; }
		int attr_size() const { return static_cast<int>(m_attr.size()); }
		const MatchingAttr& attr(int i) const { return m_attr[static_cast<std::size_t>(i)]; }
		MatchingAttr* mutable_attr(int i) { return &m_attr[static_cast<std::size_t>(i)]; }
		MatchingAttr* add_attr() { return &m_attr.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		MatchingRoomStatus m_room;
		bool m_has_room = false;
		std::vector<MatchingAttr> m_attr;
		mutable std::size_t m_cached_size = 0;
	};

	class RoomMemberDataExternal final
	{
	public:
		bool has_userinfo() const { return m_has_userinfo; }
		const UserInfo& userinfo() const { return m_userinfo; }
		UserInfo* mutable_userinfo() { m_has_userinfo = true; return &m_userinfo; }
		std::uint64_t joindate() const { return m_joindate; }
		void set_joindate(std::uint64_t v) { m_joindate = v; }
		bool has_role() const { return m_has_role; }
		const uint8& role() const { return m_role; }
		uint8* mutable_role() { m_has_role = true; return &m_role; }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		UserInfo m_userinfo;
		bool m_has_userinfo = false;
		std::uint64_t m_joindate = {};
		uint8 m_role;
		bool m_has_role = false;
		mutable std::size_t m_cached_size = 0;
	};

	class GetRoomMemberDataExternalListResponse final
	{
	public:
		const std::vector<RoomMemberDataExternal>& members() const { return m_members; }
		std::vector<RoomMemberDataExternal>* mutable_members() { return &m_members; }
		int members_size() const { return static_cast<int>(m_members.size()); }
		const RoomMemberDataExternal& members(int i) const { return m_members[static_cast<std::size_t>(i)]; }
		RoomMemberDataExternal* mutable_members(int i) { return &m_members[static_cast<std::size_t>(i)]; }
		RoomMemberDataExternal* add_members() { return &m_members.emplace_back(); }

		// Reads a message; false when it is not one. Strings and bytes point
		// into @data.
		bool ParseFromArray(const void* data, std::size_t size);
		// The size the message is written in, kept for writing it
		std::size_t ByteSizeLong() const;
		// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()
		void SerializeWithCachedSizesToArray(void* out) const;
		bool SerializeToString(std::string* out) const;

		bool merge(pb::reader& r);
		void write(pb::writer& w) const;
		std::size_t cached_size() const { return m_cached_size; }

	private:
		std::vector<RoomMemberDataExternal> m_members;
		mutable std::size_t m_cached_size = 0;
	};
} // namespace np2_structs
