// RPCN's messages, made by np2_structs_gen.py from np2_structs.proto: do not
// edit.

#include "np2_structs.h"

namespace np2_structs
{
	std::size_t uint8::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_value)
			n += pb::tag_size(1) + pb::varint_size(m_value);
		m_cached_size = n;
		return n;
	}

	void uint8::write(pb::writer& w) const
	{
		if (m_value)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_value);
		}
	}

	bool uint8::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_value = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool uint8::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void uint8::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool uint8::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t uint16::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_value)
			n += pb::tag_size(1) + pb::varint_size(m_value);
		m_cached_size = n;
		return n;
	}

	void uint16::write(pb::writer& w) const
	{
		if (m_value)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_value);
		}
	}

	bool uint16::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_value = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool uint16::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void uint16::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool uint16::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SignalingAddr::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_ip.empty())
			n += pb::len_size(1, m_ip.size());
		if (m_has_port)
			n += pb::len_size(2, m_port.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SignalingAddr::write(pb::writer& w) const
	{
		if (!m_ip.empty())
			w.bytes(1, m_ip.get());
		if (m_has_port)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_port.cached_size());
			m_port.write(w);
		}
	}

	bool SignalingAddr::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_ip.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_port = true;
					if (!m_port.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SignalingAddr::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SignalingAddr::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SignalingAddr::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingSignalingInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_npid.empty())
			n += pb::len_size(1, m_npid.size());
		if (m_has_addr)
			n += pb::len_size(2, m_addr.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void MatchingSignalingInfo::write(pb::writer& w) const
	{
		if (!m_npid.empty())
			w.bytes(1, m_npid.get());
		if (m_has_addr)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_addr.cached_size());
			m_addr.write(w);
		}
	}

	bool MatchingSignalingInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_addr = true;
					if (!m_addr.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingSignalingInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingSignalingInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingSignalingInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t Matching2SignalingInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_member_id)
			n += pb::len_size(1, m_member_id.ByteSizeLong());
		if (m_has_addr)
			n += pb::len_size(2, m_addr.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void Matching2SignalingInfo::write(pb::writer& w) const
	{
		if (m_has_member_id)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_member_id.cached_size());
			m_member_id.write(w);
		}
		if (m_has_addr)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_addr.cached_size());
			m_addr.write(w);
		}
	}

	bool Matching2SignalingInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_member_id = true;
					if (!m_member_id.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_addr = true;
					if (!m_addr.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool Matching2SignalingInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void Matching2SignalingInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool Matching2SignalingInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t BinAttr::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_id)
			n += pb::len_size(1, m_id.ByteSizeLong());
		if (!m_data.empty())
			n += pb::len_size(2, m_data.size());
		m_cached_size = n;
		return n;
	}

	void BinAttr::write(pb::writer& w) const
	{
		if (m_has_id)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_id.cached_size());
			m_id.write(w);
		}
		if (!m_data.empty())
			w.bytes(2, m_data.get());
	}

	bool BinAttr::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_id = true;
					if (!m_id.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool BinAttr::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void BinAttr::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool BinAttr::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t IntAttr::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_id)
			n += pb::len_size(1, m_id.ByteSizeLong());
		if (m_num)
			n += pb::tag_size(2) + pb::varint_size(m_num);
		m_cached_size = n;
		return n;
	}

	void IntAttr::write(pb::writer& w) const
	{
		if (m_has_id)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_id.cached_size());
			m_id.write(w);
		}
		if (m_num)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_num);
		}
	}

	bool IntAttr::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_id = true;
					if (!m_id.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_num = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool IntAttr::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void IntAttr::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool IntAttr::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMemberBinAttrInternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_updatedate)
			n += pb::tag_size(1) + pb::varint_size(m_updatedate);
		if (m_has_data)
			n += pb::len_size(2, m_data.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomMemberBinAttrInternal::write(pb::writer& w) const
	{
		if (m_updatedate)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_updatedate);
		}
		if (m_has_data)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_data.cached_size());
			m_data.write(w);
		}
	}

	bool RoomMemberBinAttrInternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_updatedate = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_data = true;
					if (!m_data.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMemberBinAttrInternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMemberBinAttrInternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMemberBinAttrInternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t BinAttrInternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_updatedate)
			n += pb::tag_size(1) + pb::varint_size(m_updatedate);
		if (m_has_updatememberid)
			n += pb::len_size(2, m_updatememberid.ByteSizeLong());
		if (m_has_data)
			n += pb::len_size(3, m_data.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void BinAttrInternal::write(pb::writer& w) const
	{
		if (m_updatedate)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_updatedate);
		}
		if (m_has_updatememberid)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_updatememberid.cached_size());
			m_updatememberid.write(w);
		}
		if (m_has_data)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_data.cached_size());
			m_data.write(w);
		}
	}

	bool BinAttrInternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_updatedate = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_updatememberid = true;
					if (!m_updatememberid.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_data = true;
					if (!m_data.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool BinAttrInternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void BinAttrInternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool BinAttrInternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t OptParam::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_type)
			n += pb::len_size(1, m_type.ByteSizeLong());
		if (m_has_flag)
			n += pb::len_size(2, m_flag.ByteSizeLong());
		if (m_has_hubmemberid)
			n += pb::len_size(3, m_hubmemberid.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void OptParam::write(pb::writer& w) const
	{
		if (m_has_type)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_type.cached_size());
			m_type.write(w);
		}
		if (m_has_flag)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_flag.cached_size());
			m_flag.write(w);
		}
		if (m_has_hubmemberid)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_hubmemberid.cached_size());
			m_hubmemberid.write(w);
		}
	}

	bool OptParam::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_type = true;
					if (!m_type.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_flag = true;
					if (!m_flag.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_hubmemberid = true;
					if (!m_hubmemberid.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool OptParam::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void OptParam::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool OptParam::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GroupConfig::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_slotnum)
			n += pb::tag_size(1) + pb::varint_size(m_slotnum);
		if (!m_label.empty())
			n += pb::len_size(2, m_label.size());
		if (m_withpassword)
			n += pb::tag_size(3) + pb::varint_size((m_withpassword ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void GroupConfig::write(pb::writer& w) const
	{
		if (m_slotnum)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_slotnum);
		}
		if (!m_label.empty())
			w.bytes(2, m_label.get());
		if (m_withpassword)
		{
			w.tag(3, pb::wire_varint);
			w.varint((m_withpassword ? 1u : 0u));
		}
	}

	bool GroupConfig::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotnum = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_label.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withpassword = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GroupConfig::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GroupConfig::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GroupConfig::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t UserInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_npid.empty())
			n += pb::len_size(1, m_npid.size());
		if (!m_onlinename.empty())
			n += pb::len_size(2, m_onlinename.size());
		if (!m_avatarurl.empty())
			n += pb::len_size(3, m_avatarurl.size());
		m_cached_size = n;
		return n;
	}

	void UserInfo::write(pb::writer& w) const
	{
		if (!m_npid.empty())
			w.bytes(1, m_npid.get());
		if (!m_onlinename.empty())
			w.bytes(2, m_onlinename.get());
		if (!m_avatarurl.empty())
			w.bytes(3, m_avatarurl.get());
	}

	bool UserInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_onlinename.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_avatarurl.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool UserInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void UserInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool UserInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomGroup::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_groupid)
			n += pb::len_size(1, m_groupid.ByteSizeLong());
		if (m_withpassword)
			n += pb::tag_size(2) + pb::varint_size((m_withpassword ? 1u : 0u));
		if (!m_label.empty())
			n += pb::len_size(3, m_label.size());
		if (m_slotnum)
			n += pb::tag_size(4) + pb::varint_size(m_slotnum);
		if (m_curgroupmembernum)
			n += pb::tag_size(5) + pb::varint_size(m_curgroupmembernum);
		m_cached_size = n;
		return n;
	}

	void RoomGroup::write(pb::writer& w) const
	{
		if (m_has_groupid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_groupid.cached_size());
			m_groupid.write(w);
		}
		if (m_withpassword)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_withpassword ? 1u : 0u));
		}
		if (!m_label.empty())
			w.bytes(3, m_label.get());
		if (m_slotnum)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_slotnum);
		}
		if (m_curgroupmembernum)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_curgroupmembernum);
		}
	}

	bool RoomGroup::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_groupid = true;
					if (!m_groupid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withpassword = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_label.lend(s);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotnum = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_curgroupmembernum = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomGroup::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomGroup::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomGroup::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMemberDataInternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_userinfo)
			n += pb::len_size(1, m_userinfo.ByteSizeLong());
		if (m_joindate)
			n += pb::tag_size(2) + pb::varint_size(m_joindate);
		if (m_memberid)
			n += pb::tag_size(3) + pb::varint_size(m_memberid);
		if (m_has_teamid)
			n += pb::len_size(4, m_teamid.ByteSizeLong());
		if (m_has_roomgroup)
			n += pb::len_size(5, m_roomgroup.ByteSizeLong());
		if (m_has_nattype)
			n += pb::len_size(6, m_nattype.ByteSizeLong());
		if (m_flagattr)
			n += pb::tag_size(7) + pb::varint_size(m_flagattr);
		for (const auto& v : m_roommemberbinattrinternal)
			n += pb::len_size(8, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomMemberDataInternal::write(pb::writer& w) const
	{
		if (m_has_userinfo)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_userinfo.cached_size());
			m_userinfo.write(w);
		}
		if (m_joindate)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_joindate);
		}
		if (m_memberid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_memberid);
		}
		if (m_has_teamid)
		{
			w.tag(4, pb::wire_len);
			w.varint(m_teamid.cached_size());
			m_teamid.write(w);
		}
		if (m_has_roomgroup)
		{
			w.tag(5, pb::wire_len);
			w.varint(m_roomgroup.cached_size());
			m_roomgroup.write(w);
		}
		if (m_has_nattype)
		{
			w.tag(6, pb::wire_len);
			w.varint(m_nattype.cached_size());
			m_nattype.write(w);
		}
		if (m_flagattr)
		{
			w.tag(7, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_roommemberbinattrinternal)
		{
			w.tag(8, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool RoomMemberDataInternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_userinfo = true;
					if (!m_userinfo.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_joindate = v;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_memberid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_teamid = true;
					if (!m_teamid.merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_roomgroup = true;
					if (!m_roomgroup.merge(sub))
						return false;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_nattype = true;
					if (!m_nattype.merge(sub))
						return false;
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roommemberbinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMemberDataInternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMemberDataInternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMemberDataInternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomDataInternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_serverid)
			n += pb::len_size(1, m_serverid.ByteSizeLong());
		if (m_worldid)
			n += pb::tag_size(2) + pb::varint_size(m_worldid);
		if (m_lobbyid)
			n += pb::tag_size(3) + pb::varint_size(m_lobbyid);
		if (m_roomid)
			n += pb::tag_size(4) + pb::varint_size(m_roomid);
		if (m_passwordslotmask)
			n += pb::tag_size(5) + pb::varint_size(m_passwordslotmask);
		if (m_maxslot)
			n += pb::tag_size(6) + pb::varint_size(m_maxslot);
		for (const auto& v : m_memberlist)
			n += pb::len_size(7, v.ByteSizeLong());
		if (m_has_ownerid)
			n += pb::len_size(8, m_ownerid.ByteSizeLong());
		for (const auto& v : m_roomgroup)
			n += pb::len_size(9, v.ByteSizeLong());
		if (m_flagattr)
			n += pb::tag_size(10) + pb::varint_size(m_flagattr);
		for (const auto& v : m_roombinattrinternal)
			n += pb::len_size(11, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomDataInternal::write(pb::writer& w) const
	{
		if (m_has_serverid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_serverid.cached_size());
			m_serverid.write(w);
		}
		if (m_worldid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_worldid);
		}
		if (m_lobbyid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_lobbyid);
		}
		if (m_roomid)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_passwordslotmask)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_passwordslotmask);
		}
		if (m_maxslot)
		{
			w.tag(6, pb::wire_varint);
			w.varint(m_maxslot);
		}
		for (const auto& v : m_memberlist)
		{
			w.tag(7, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_has_ownerid)
		{
			w.tag(8, pb::wire_len);
			w.varint(m_ownerid.cached_size());
			m_ownerid.write(w);
		}
		for (const auto& v : m_roomgroup)
		{
			w.tag(9, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_flagattr)
		{
			w.tag(10, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_roombinattrinternal)
		{
			w.tag(11, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool RoomDataInternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_serverid = true;
					if (!m_serverid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_worldid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lobbyid = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_passwordslotmask = v;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_maxslot = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_memberlist.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_ownerid = true;
					if (!m_ownerid.merge(sub))
						return false;
					continue;
				}
				break;
			case 9:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomgroup.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 10:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 11:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomDataInternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomDataInternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomDataInternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomDataExternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_serverid)
			n += pb::len_size(1, m_serverid.ByteSizeLong());
		if (m_worldid)
			n += pb::tag_size(2) + pb::varint_size(m_worldid);
		if (m_has_publicslotnum)
			n += pb::len_size(3, m_publicslotnum.ByteSizeLong());
		if (m_has_privateslotnum)
			n += pb::len_size(4, m_privateslotnum.ByteSizeLong());
		if (m_lobbyid)
			n += pb::tag_size(5) + pb::varint_size(m_lobbyid);
		if (m_roomid)
			n += pb::tag_size(6) + pb::varint_size(m_roomid);
		if (m_has_openpublicslotnum)
			n += pb::len_size(7, m_openpublicslotnum.ByteSizeLong());
		if (m_has_maxslot)
			n += pb::len_size(8, m_maxslot.ByteSizeLong());
		if (m_has_openprivateslotnum)
			n += pb::len_size(9, m_openprivateslotnum.ByteSizeLong());
		if (m_has_curmembernum)
			n += pb::len_size(10, m_curmembernum.ByteSizeLong());
		if (m_passwordslotmask)
			n += pb::tag_size(11) + pb::varint_size(m_passwordslotmask);
		if (m_has_owner)
			n += pb::len_size(12, m_owner.ByteSizeLong());
		for (const auto& v : m_roomgroup)
			n += pb::len_size(13, v.ByteSizeLong());
		if (m_flagattr)
			n += pb::tag_size(14) + pb::varint_size(m_flagattr);
		for (const auto& v : m_roomsearchableintattrexternal)
			n += pb::len_size(15, v.ByteSizeLong());
		for (const auto& v : m_roomsearchablebinattrexternal)
			n += pb::len_size(16, v.ByteSizeLong());
		for (const auto& v : m_roombinattrexternal)
			n += pb::len_size(17, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomDataExternal::write(pb::writer& w) const
	{
		if (m_has_serverid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_serverid.cached_size());
			m_serverid.write(w);
		}
		if (m_worldid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_worldid);
		}
		if (m_has_publicslotnum)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_publicslotnum.cached_size());
			m_publicslotnum.write(w);
		}
		if (m_has_privateslotnum)
		{
			w.tag(4, pb::wire_len);
			w.varint(m_privateslotnum.cached_size());
			m_privateslotnum.write(w);
		}
		if (m_lobbyid)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_lobbyid);
		}
		if (m_roomid)
		{
			w.tag(6, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_has_openpublicslotnum)
		{
			w.tag(7, pb::wire_len);
			w.varint(m_openpublicslotnum.cached_size());
			m_openpublicslotnum.write(w);
		}
		if (m_has_maxslot)
		{
			w.tag(8, pb::wire_len);
			w.varint(m_maxslot.cached_size());
			m_maxslot.write(w);
		}
		if (m_has_openprivateslotnum)
		{
			w.tag(9, pb::wire_len);
			w.varint(m_openprivateslotnum.cached_size());
			m_openprivateslotnum.write(w);
		}
		if (m_has_curmembernum)
		{
			w.tag(10, pb::wire_len);
			w.varint(m_curmembernum.cached_size());
			m_curmembernum.write(w);
		}
		if (m_passwordslotmask)
		{
			w.tag(11, pb::wire_varint);
			w.varint(m_passwordslotmask);
		}
		if (m_has_owner)
		{
			w.tag(12, pb::wire_len);
			w.varint(m_owner.cached_size());
			m_owner.write(w);
		}
		for (const auto& v : m_roomgroup)
		{
			w.tag(13, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_flagattr)
		{
			w.tag(14, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_roomsearchableintattrexternal)
		{
			w.tag(15, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roomsearchablebinattrexternal)
		{
			w.tag(16, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roombinattrexternal)
		{
			w.tag(17, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool RoomDataExternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_serverid = true;
					if (!m_serverid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_worldid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_publicslotnum = true;
					if (!m_publicslotnum.merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_privateslotnum = true;
					if (!m_privateslotnum.merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lobbyid = v;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_openpublicslotnum = true;
					if (!m_openpublicslotnum.merge(sub))
						return false;
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_maxslot = true;
					if (!m_maxslot.merge(sub))
						return false;
					continue;
				}
				break;
			case 9:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_openprivateslotnum = true;
					if (!m_openprivateslotnum.merge(sub))
						return false;
					continue;
				}
				break;
			case 10:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_curmembernum = true;
					if (!m_curmembernum.merge(sub))
						return false;
					continue;
				}
				break;
			case 11:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_passwordslotmask = v;
					continue;
				}
				break;
			case 12:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_owner = true;
					if (!m_owner.merge(sub))
						return false;
					continue;
				}
				break;
			case 13:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomgroup.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 14:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 15:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchableintattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 16:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchablebinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 17:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomDataExternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomDataExternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomDataExternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t IntSearchFilter::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_searchoperator)
			n += pb::len_size(1, m_searchoperator.ByteSizeLong());
		if (m_has_attr)
			n += pb::len_size(2, m_attr.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void IntSearchFilter::write(pb::writer& w) const
	{
		if (m_has_searchoperator)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_searchoperator.cached_size());
			m_searchoperator.write(w);
		}
		if (m_has_attr)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_attr.cached_size());
			m_attr.write(w);
		}
	}

	bool IntSearchFilter::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_searchoperator = true;
					if (!m_searchoperator.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_attr = true;
					if (!m_attr.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool IntSearchFilter::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void IntSearchFilter::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool IntSearchFilter::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t BinSearchFilter::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_searchoperator)
			n += pb::len_size(1, m_searchoperator.ByteSizeLong());
		if (m_has_attr)
			n += pb::len_size(2, m_attr.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void BinSearchFilter::write(pb::writer& w) const
	{
		if (m_has_searchoperator)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_searchoperator.cached_size());
			m_searchoperator.write(w);
		}
		if (m_has_attr)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_attr.cached_size());
			m_attr.write(w);
		}
	}

	bool BinSearchFilter::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_searchoperator = true;
					if (!m_searchoperator.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_attr = true;
					if (!m_attr.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool BinSearchFilter::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void BinSearchFilter::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool BinSearchFilter::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t PresenceOptionData::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_data.empty())
			n += pb::len_size(1, m_data.size());
		if (m_len)
			n += pb::tag_size(2) + pb::varint_size(m_len);
		m_cached_size = n;
		return n;
	}

	void PresenceOptionData::write(pb::writer& w) const
	{
		if (!m_data.empty())
			w.bytes(1, m_data.get());
		if (m_len)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_len);
		}
	}

	bool PresenceOptionData::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_len = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool PresenceOptionData::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void PresenceOptionData::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool PresenceOptionData::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomGroupPasswordConfig::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_groupid)
			n += pb::len_size(1, m_groupid.ByteSizeLong());
		if (m_withpassword)
			n += pb::tag_size(2) + pb::varint_size((m_withpassword ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void RoomGroupPasswordConfig::write(pb::writer& w) const
	{
		if (m_has_groupid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_groupid.cached_size());
			m_groupid.write(w);
		}
		if (m_withpassword)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_withpassword ? 1u : 0u));
		}
	}

	bool RoomGroupPasswordConfig::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_groupid = true;
					if (!m_groupid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withpassword = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomGroupPasswordConfig::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomGroupPasswordConfig::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomGroupPasswordConfig::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SearchRoomRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_option)
			n += pb::tag_size(1) + pb::varint_size(pb::from_int32(m_option));
		if (m_worldid)
			n += pb::tag_size(2) + pb::varint_size(m_worldid);
		if (m_lobbyid)
			n += pb::tag_size(3) + pb::varint_size(m_lobbyid);
		if (m_rangefilter_startindex)
			n += pb::tag_size(4) + pb::varint_size(m_rangefilter_startindex);
		if (m_rangefilter_max)
			n += pb::tag_size(5) + pb::varint_size(m_rangefilter_max);
		if (m_flagfilter)
			n += pb::tag_size(6) + pb::varint_size(m_flagfilter);
		if (m_flagattr)
			n += pb::tag_size(7) + pb::varint_size(m_flagattr);
		for (const auto& v : m_intfilter)
			n += pb::len_size(8, v.ByteSizeLong());
		for (const auto& v : m_binfilter)
			n += pb::len_size(9, v.ByteSizeLong());
		for (const auto& v : m_attrid)
			n += pb::len_size(10, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SearchRoomRequest::write(pb::writer& w) const
	{
		if (m_option)
		{
			w.tag(1, pb::wire_varint);
			w.varint(pb::from_int32(m_option));
		}
		if (m_worldid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_worldid);
		}
		if (m_lobbyid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_lobbyid);
		}
		if (m_rangefilter_startindex)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_rangefilter_startindex);
		}
		if (m_rangefilter_max)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_rangefilter_max);
		}
		if (m_flagfilter)
		{
			w.tag(6, pb::wire_varint);
			w.varint(m_flagfilter);
		}
		if (m_flagattr)
		{
			w.tag(7, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_intfilter)
		{
			w.tag(8, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_binfilter)
		{
			w.tag(9, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_attrid)
		{
			w.tag(10, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SearchRoomRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_option = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_worldid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lobbyid = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_rangefilter_startindex = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_rangefilter_max = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagfilter = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_intfilter.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 9:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_binfilter.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 10:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrid.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SearchRoomRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SearchRoomRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SearchRoomRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SearchRoomResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_startindex)
			n += pb::tag_size(1) + pb::varint_size(m_startindex);
		if (m_total)
			n += pb::tag_size(2) + pb::varint_size(m_total);
		for (const auto& v : m_rooms)
			n += pb::len_size(3, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SearchRoomResponse::write(pb::writer& w) const
	{
		if (m_startindex)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_startindex);
		}
		if (m_total)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_total);
		}
		for (const auto& v : m_rooms)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SearchRoomResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_startindex = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_total = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_rooms.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SearchRoomResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SearchRoomResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SearchRoomResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t CreateJoinRoomRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_worldid)
			n += pb::tag_size(1) + pb::varint_size(m_worldid);
		if (m_lobbyid)
			n += pb::tag_size(2) + pb::varint_size(m_lobbyid);
		if (m_maxslot)
			n += pb::tag_size(3) + pb::varint_size(m_maxslot);
		if (m_flagattr)
			n += pb::tag_size(4) + pb::varint_size(m_flagattr);
		for (const auto& v : m_roombinattrinternal)
			n += pb::len_size(5, v.ByteSizeLong());
		for (const auto& v : m_roomsearchableintattrexternal)
			n += pb::len_size(6, v.ByteSizeLong());
		for (const auto& v : m_roomsearchablebinattrexternal)
			n += pb::len_size(7, v.ByteSizeLong());
		for (const auto& v : m_roombinattrexternal)
			n += pb::len_size(8, v.ByteSizeLong());
		if (!m_roompassword.empty())
			n += pb::len_size(9, m_roompassword.size());
		for (const auto& v : m_groupconfig)
			n += pb::len_size(10, v.ByteSizeLong());
		if (m_passwordslotmask)
			n += pb::tag_size(11) + pb::varint_size(m_passwordslotmask);
		for (const auto& v : m_alloweduser)
			n += pb::len_size(12, v.size());
		for (const auto& v : m_blockeduser)
			n += pb::len_size(13, v.size());
		if (!m_joinroomgrouplabel.empty())
			n += pb::len_size(14, m_joinroomgrouplabel.size());
		for (const auto& v : m_roommemberbinattrinternal)
			n += pb::len_size(15, v.ByteSizeLong());
		if (m_has_teamid)
			n += pb::len_size(16, m_teamid.ByteSizeLong());
		if (m_has_sigoptparam)
			n += pb::len_size(17, m_sigoptparam.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void CreateJoinRoomRequest::write(pb::writer& w) const
	{
		if (m_worldid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_worldid);
		}
		if (m_lobbyid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_lobbyid);
		}
		if (m_maxslot)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_maxslot);
		}
		if (m_flagattr)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_roombinattrinternal)
		{
			w.tag(5, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roomsearchableintattrexternal)
		{
			w.tag(6, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roomsearchablebinattrexternal)
		{
			w.tag(7, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roombinattrexternal)
		{
			w.tag(8, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (!m_roompassword.empty())
			w.bytes(9, m_roompassword.get());
		for (const auto& v : m_groupconfig)
		{
			w.tag(10, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_passwordslotmask)
		{
			w.tag(11, pb::wire_varint);
			w.varint(m_passwordslotmask);
		}
		for (const auto& v : m_alloweduser)
			w.bytes(12, v.get());
		for (const auto& v : m_blockeduser)
			w.bytes(13, v.get());
		if (!m_joinroomgrouplabel.empty())
			w.bytes(14, m_joinroomgrouplabel.get());
		for (const auto& v : m_roommemberbinattrinternal)
		{
			w.tag(15, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_has_teamid)
		{
			w.tag(16, pb::wire_len);
			w.varint(m_teamid.cached_size());
			m_teamid.write(w);
		}
		if (m_has_sigoptparam)
		{
			w.tag(17, pb::wire_len);
			w.varint(m_sigoptparam.cached_size());
			m_sigoptparam.write(w);
		}
	}

	bool CreateJoinRoomRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_worldid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lobbyid = v;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_maxslot = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchableintattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchablebinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 9:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_roompassword.lend(s);
					continue;
				}
				break;
			case 10:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_groupconfig.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 11:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_passwordslotmask = v;
					continue;
				}
				break;
			case 12:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_alloweduser.emplace_back().lend(s);
					continue;
				}
				break;
			case 13:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_blockeduser.emplace_back().lend(s);
					continue;
				}
				break;
			case 14:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_joinroomgrouplabel.lend(s);
					continue;
				}
				break;
			case 15:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roommemberbinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 16:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_teamid = true;
					if (!m_teamid.merge(sub))
						return false;
					continue;
				}
				break;
			case 17:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_sigoptparam = true;
					if (!m_sigoptparam.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool CreateJoinRoomRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void CreateJoinRoomRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool CreateJoinRoomRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t CreateRoomResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_internal)
			n += pb::len_size(1, m_internal.ByteSizeLong());
		if (m_has_opt_param)
			n += pb::len_size(2, m_opt_param.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void CreateRoomResponse::write(pb::writer& w) const
	{
		if (m_has_internal)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_internal.cached_size());
			m_internal.write(w);
		}
		if (m_has_opt_param)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_opt_param.cached_size());
			m_opt_param.write(w);
		}
	}

	bool CreateRoomResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_internal = true;
					if (!m_internal.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_opt_param = true;
					if (!m_opt_param.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool CreateRoomResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void CreateRoomResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool CreateRoomResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t JoinRoomRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (!m_roompassword.empty())
			n += pb::len_size(2, m_roompassword.size());
		if (!m_joinroomgrouplabel.empty())
			n += pb::len_size(3, m_joinroomgrouplabel.size());
		for (const auto& v : m_roommemberbinattrinternal)
			n += pb::len_size(4, v.ByteSizeLong());
		if (m_has_optdata)
			n += pb::len_size(5, m_optdata.ByteSizeLong());
		if (m_has_teamid)
			n += pb::len_size(6, m_teamid.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void JoinRoomRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (!m_roompassword.empty())
			w.bytes(2, m_roompassword.get());
		if (!m_joinroomgrouplabel.empty())
			w.bytes(3, m_joinroomgrouplabel.get());
		for (const auto& v : m_roommemberbinattrinternal)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_has_optdata)
		{
			w.tag(5, pb::wire_len);
			w.varint(m_optdata.cached_size());
			m_optdata.write(w);
		}
		if (m_has_teamid)
		{
			w.tag(6, pb::wire_len);
			w.varint(m_teamid.cached_size());
			m_teamid.write(w);
		}
	}

	bool JoinRoomRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_roompassword.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_joinroomgrouplabel.lend(s);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roommemberbinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_optdata = true;
					if (!m_optdata.merge(sub))
						return false;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_teamid = true;
					if (!m_teamid.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool JoinRoomRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void JoinRoomRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool JoinRoomRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t JoinRoomResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_room_data)
			n += pb::len_size(1, m_room_data.ByteSizeLong());
		for (const auto& v : m_signaling_data)
			n += pb::len_size(2, v.ByteSizeLong());
		if (m_has_opt_param)
			n += pb::len_size(3, m_opt_param.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void JoinRoomResponse::write(pb::writer& w) const
	{
		if (m_has_room_data)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_room_data.cached_size());
			m_room_data.write(w);
		}
		for (const auto& v : m_signaling_data)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_has_opt_param)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_opt_param.cached_size());
			m_opt_param.write(w);
		}
	}

	bool JoinRoomResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_room_data = true;
					if (!m_room_data.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_signaling_data.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_opt_param = true;
					if (!m_opt_param.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool JoinRoomResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void JoinRoomResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool JoinRoomResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t LeaveRoomRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (m_has_optdata)
			n += pb::len_size(2, m_optdata.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void LeaveRoomRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_has_optdata)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_optdata.cached_size());
			m_optdata.write(w);
		}
	}

	bool LeaveRoomRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_optdata = true;
					if (!m_optdata.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool LeaveRoomRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void LeaveRoomRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool LeaveRoomRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomDataExternalListRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_roomids.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_roomids)
				p += pb::varint_size(v);
			m_roomids_packed = p;
			n += pb::len_size(1, p);
		}
		for (const auto& v : m_attrids)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomDataExternalListRequest::write(pb::writer& w) const
	{
		if (!m_roomids.empty())
		{
			w.tag(1, pb::wire_len);
			w.varint(m_roomids_packed);
			for (const auto v : m_roomids)
				w.varint(v);
		}
		for (const auto& v : m_attrids)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomDataExternalListRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomids.push_back(v);
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_roomids.push_back(v);
					}
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrids.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomDataExternalListRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomDataExternalListRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomDataExternalListRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomDataExternalListResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_rooms)
			n += pb::len_size(1, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomDataExternalListResponse::write(pb::writer& w) const
	{
		for (const auto& v : m_rooms)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomDataExternalListResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_rooms.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomDataExternalListResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomDataExternalListResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomDataExternalListResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetRoomDataExternalRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		for (const auto& v : m_roomsearchableintattrexternal)
			n += pb::len_size(2, v.ByteSizeLong());
		for (const auto& v : m_roomsearchablebinattrexternal)
			n += pb::len_size(3, v.ByteSizeLong());
		for (const auto& v : m_roombinattrexternal)
			n += pb::len_size(4, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SetRoomDataExternalRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		for (const auto& v : m_roomsearchableintattrexternal)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roomsearchablebinattrexternal)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_roombinattrexternal)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SetRoomDataExternalRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchableintattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roomsearchablebinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrexternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetRoomDataExternalRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetRoomDataExternalRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetRoomDataExternalRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetRoomDataInternalRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (m_flagfilter)
			n += pb::tag_size(2) + pb::varint_size(m_flagfilter);
		if (m_flagattr)
			n += pb::tag_size(3) + pb::varint_size(m_flagattr);
		for (const auto& v : m_roombinattrinternal)
			n += pb::len_size(4, v.ByteSizeLong());
		for (const auto& v : m_passwordconfig)
			n += pb::len_size(5, v.ByteSizeLong());
		if (!m_passwordslotmask.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_passwordslotmask)
				p += pb::varint_size(v);
			m_passwordslotmask_packed = p;
			n += pb::len_size(6, p);
		}
		for (const auto& v : m_ownerprivilegerank)
			n += pb::len_size(7, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SetRoomDataInternalRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_flagfilter)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_flagfilter);
		}
		if (m_flagattr)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_flagattr);
		}
		for (const auto& v : m_roombinattrinternal)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_passwordconfig)
		{
			w.tag(5, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (!m_passwordslotmask.empty())
		{
			w.tag(6, pb::wire_len);
			w.varint(m_passwordslotmask_packed);
			for (const auto v : m_passwordslotmask)
				w.varint(v);
		}
		for (const auto& v : m_ownerprivilegerank)
		{
			w.tag(7, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SetRoomDataInternalRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagfilter = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_flagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roombinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_passwordconfig.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_passwordslotmask.push_back(v);
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_passwordslotmask.push_back(v);
					}
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_ownerprivilegerank.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetRoomDataInternalRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetRoomDataInternalRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetRoomDataInternalRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomMemberDataInternalRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (m_has_memberid)
			n += pb::len_size(2, m_memberid.ByteSizeLong());
		for (const auto& v : m_attrid)
			n += pb::len_size(3, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomMemberDataInternalRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_has_memberid)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_memberid.cached_size());
			m_memberid.write(w);
		}
		for (const auto& v : m_attrid)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomMemberDataInternalRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_memberid = true;
					if (!m_memberid.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrid.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomMemberDataInternalRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomMemberDataInternalRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomMemberDataInternalRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetRoomMemberDataInternalRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (m_has_memberid)
			n += pb::len_size(2, m_memberid.ByteSizeLong());
		if (m_has_teamid)
			n += pb::len_size(3, m_teamid.ByteSizeLong());
		for (const auto& v : m_roommemberbinattrinternal)
			n += pb::len_size(4, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SetRoomMemberDataInternalRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_has_memberid)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_memberid.cached_size());
			m_memberid.write(w);
		}
		if (m_has_teamid)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_teamid.cached_size());
			m_teamid.write(w);
		}
		for (const auto& v : m_roommemberbinattrinternal)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SetRoomMemberDataInternalRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_memberid = true;
					if (!m_memberid.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_teamid = true;
					if (!m_teamid.merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_roommemberbinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetRoomMemberDataInternalRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetRoomMemberDataInternalRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetRoomMemberDataInternalRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetUserInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_serverid)
			n += pb::len_size(1, m_serverid.ByteSizeLong());
		for (const auto& v : m_userbinattr)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SetUserInfo::write(pb::writer& w) const
	{
		if (m_has_serverid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_serverid.cached_size());
			m_serverid.write(w);
		}
		for (const auto& v : m_userbinattr)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SetUserInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_serverid = true;
					if (!m_serverid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_userbinattr.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetUserInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetUserInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetUserInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomDataInternalRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		for (const auto& v : m_attrid)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomDataInternalRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		for (const auto& v : m_attrid)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomDataInternalRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrid.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomDataInternalRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomDataInternalRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomDataInternalRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMemberUpdateInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_roommemberdatainternal)
			n += pb::len_size(1, m_roommemberdatainternal.ByteSizeLong());
		if (m_has_eventcause)
			n += pb::len_size(2, m_eventcause.ByteSizeLong());
		if (m_has_optdata)
			n += pb::len_size(3, m_optdata.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomMemberUpdateInfo::write(pb::writer& w) const
	{
		if (m_has_roommemberdatainternal)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_roommemberdatainternal.cached_size());
			m_roommemberdatainternal.write(w);
		}
		if (m_has_eventcause)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_eventcause.cached_size());
			m_eventcause.write(w);
		}
		if (m_has_optdata)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_optdata.cached_size());
			m_optdata.write(w);
		}
	}

	bool RoomMemberUpdateInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_roommemberdatainternal = true;
					if (!m_roommemberdatainternal.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_eventcause = true;
					if (!m_eventcause.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_optdata = true;
					if (!m_optdata.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMemberUpdateInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMemberUpdateInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMemberUpdateInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t NotificationUserJoinedRoom::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_room_id)
			n += pb::tag_size(1) + pb::varint_size(m_room_id);
		if (m_has_update_info)
			n += pb::len_size(2, m_update_info.ByteSizeLong());
		if (m_has_signaling)
			n += pb::len_size(3, m_signaling.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void NotificationUserJoinedRoom::write(pb::writer& w) const
	{
		if (m_room_id)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_room_id);
		}
		if (m_has_update_info)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_update_info.cached_size());
			m_update_info.write(w);
		}
		if (m_has_signaling)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_signaling.cached_size());
			m_signaling.write(w);
		}
	}

	bool NotificationUserJoinedRoom::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_room_id = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_update_info = true;
					if (!m_update_info.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_signaling = true;
					if (!m_signaling.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool NotificationUserJoinedRoom::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void NotificationUserJoinedRoom::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool NotificationUserJoinedRoom::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomUpdateInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_eventcause)
			n += pb::len_size(1, m_eventcause.ByteSizeLong());
		if (m_errorcode)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_errorcode));
		if (m_has_optdata)
			n += pb::len_size(3, m_optdata.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomUpdateInfo::write(pb::writer& w) const
	{
		if (m_has_eventcause)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_eventcause.cached_size());
			m_eventcause.write(w);
		}
		if (m_errorcode)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_errorcode));
		}
		if (m_has_optdata)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_optdata.cached_size());
			m_optdata.write(w);
		}
	}

	bool RoomUpdateInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_eventcause = true;
					if (!m_eventcause.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_errorcode = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_optdata = true;
					if (!m_optdata.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomUpdateInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomUpdateInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomUpdateInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomDataInternalUpdateInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_newroomdatainternal)
			n += pb::len_size(1, m_newroomdatainternal.ByteSizeLong());
		if (m_prevflagattr)
			n += pb::tag_size(2) + pb::varint_size(m_prevflagattr);
		if (m_prevroompasswordslotmask)
			n += pb::tag_size(3) + pb::varint_size(m_prevroompasswordslotmask);
		if (!m_newroomgroup.empty())
			n += pb::len_size(4, m_newroomgroup.size());
		for (const auto& v : m_newroombinattrinternal)
			n += pb::len_size(5, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomDataInternalUpdateInfo::write(pb::writer& w) const
	{
		if (m_has_newroomdatainternal)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_newroomdatainternal.cached_size());
			m_newroomdatainternal.write(w);
		}
		if (m_prevflagattr)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_prevflagattr);
		}
		if (m_prevroompasswordslotmask)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_prevroompasswordslotmask);
		}
		if (!m_newroomgroup.empty())
			w.bytes(4, m_newroomgroup.get());
		for (const auto& v : m_newroombinattrinternal)
		{
			w.tag(5, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool RoomDataInternalUpdateInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_newroomdatainternal = true;
					if (!m_newroomdatainternal.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_prevflagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_prevroompasswordslotmask = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_newroomgroup.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_newroombinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomDataInternalUpdateInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomDataInternalUpdateInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomDataInternalUpdateInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMemberDataInternalUpdateInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_newroommemberdatainternal)
			n += pb::len_size(1, m_newroommemberdatainternal.ByteSizeLong());
		if (m_prevflagattr)
			n += pb::tag_size(2) + pb::varint_size(m_prevflagattr);
		if (m_has_prevteamid)
			n += pb::len_size(3, m_prevteamid.ByteSizeLong());
		for (const auto& v : m_newroommemberbinattrinternal)
			n += pb::len_size(4, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomMemberDataInternalUpdateInfo::write(pb::writer& w) const
	{
		if (m_has_newroommemberdatainternal)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_newroommemberdatainternal.cached_size());
			m_newroommemberdatainternal.write(w);
		}
		if (m_prevflagattr)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_prevflagattr);
		}
		if (m_has_prevteamid)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_prevteamid.cached_size());
			m_prevteamid.write(w);
		}
		for (const auto& v : m_newroommemberbinattrinternal)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool RoomMemberDataInternalUpdateInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_newroommemberdatainternal = true;
					if (!m_newroommemberdatainternal.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_prevflagattr = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_prevteamid = true;
					if (!m_prevteamid.merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_newroommemberbinattrinternal.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMemberDataInternalUpdateInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMemberDataInternalUpdateInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMemberDataInternalUpdateInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetPingInfoResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_serverid)
			n += pb::len_size(1, m_serverid.ByteSizeLong());
		if (m_worldid)
			n += pb::tag_size(2) + pb::varint_size(m_worldid);
		if (m_roomid)
			n += pb::tag_size(3) + pb::varint_size(m_roomid);
		if (m_rtt)
			n += pb::tag_size(4) + pb::varint_size(m_rtt);
		m_cached_size = n;
		return n;
	}

	void GetPingInfoResponse::write(pb::writer& w) const
	{
		if (m_has_serverid)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_serverid.cached_size());
			m_serverid.write(w);
		}
		if (m_worldid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_worldid);
		}
		if (m_roomid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_rtt)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_rtt);
		}
	}

	bool GetPingInfoResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_serverid = true;
					if (!m_serverid.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_worldid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_rtt = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetPingInfoResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetPingInfoResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetPingInfoResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SendRoomMessageRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_roomid)
			n += pb::tag_size(1) + pb::varint_size(m_roomid);
		if (m_has_casttype)
			n += pb::len_size(2, m_casttype.ByteSizeLong());
		for (const auto& v : m_dst)
			n += pb::len_size(3, v.ByteSizeLong());
		if (!m_msg.empty())
			n += pb::len_size(4, m_msg.size());
		if (m_has_option)
			n += pb::len_size(5, m_option.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SendRoomMessageRequest::write(pb::writer& w) const
	{
		if (m_roomid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_roomid);
		}
		if (m_has_casttype)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_casttype.cached_size());
			m_casttype.write(w);
		}
		for (const auto& v : m_dst)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (!m_msg.empty())
			w.bytes(4, m_msg.get());
		if (m_has_option)
		{
			w.tag(5, pb::wire_len);
			w.varint(m_option.cached_size());
			m_option.write(w);
		}
	}

	bool SendRoomMessageRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_roomid = v;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_casttype = true;
					if (!m_casttype.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_dst.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_msg.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_option = true;
					if (!m_option.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SendRoomMessageRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SendRoomMessageRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SendRoomMessageRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMessageInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_filtered)
			n += pb::tag_size(1) + pb::varint_size((m_filtered ? 1u : 0u));
		if (m_has_casttype)
			n += pb::len_size(2, m_casttype.ByteSizeLong());
		for (const auto& v : m_dst)
			n += pb::len_size(3, v.ByteSizeLong());
		if (m_has_srcmember)
			n += pb::len_size(4, m_srcmember.ByteSizeLong());
		if (!m_msg.empty())
			n += pb::len_size(5, m_msg.size());
		m_cached_size = n;
		return n;
	}

	void RoomMessageInfo::write(pb::writer& w) const
	{
		if (m_filtered)
		{
			w.tag(1, pb::wire_varint);
			w.varint((m_filtered ? 1u : 0u));
		}
		if (m_has_casttype)
		{
			w.tag(2, pb::wire_len);
			w.varint(m_casttype.cached_size());
			m_casttype.write(w);
		}
		for (const auto& v : m_dst)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_has_srcmember)
		{
			w.tag(4, pb::wire_len);
			w.varint(m_srcmember.cached_size());
			m_srcmember.write(w);
		}
		if (!m_msg.empty())
			w.bytes(5, m_msg.get());
	}

	bool RoomMessageInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_filtered = (v != 0);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_casttype = true;
					if (!m_casttype.merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_dst.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_srcmember = true;
					if (!m_srcmember.merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_msg.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMessageInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMessageInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMessageInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MessageDetails::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_communicationid.empty())
			n += pb::len_size(1, m_communicationid.size());
		if (m_msgid)
			n += pb::tag_size(2) + pb::varint_size(m_msgid);
		if (m_has_maintype)
			n += pb::len_size(3, m_maintype.ByteSizeLong());
		if (m_has_subtype)
			n += pb::len_size(4, m_subtype.ByteSizeLong());
		if (m_msgfeatures)
			n += pb::tag_size(5) + pb::varint_size(m_msgfeatures);
		if (!m_subject.empty())
			n += pb::len_size(6, m_subject.size());
		if (!m_body.empty())
			n += pb::len_size(7, m_body.size());
		if (!m_data.empty())
			n += pb::len_size(8, m_data.size());
		m_cached_size = n;
		return n;
	}

	void MessageDetails::write(pb::writer& w) const
	{
		if (!m_communicationid.empty())
			w.bytes(1, m_communicationid.get());
		if (m_msgid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_msgid);
		}
		if (m_has_maintype)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_maintype.cached_size());
			m_maintype.write(w);
		}
		if (m_has_subtype)
		{
			w.tag(4, pb::wire_len);
			w.varint(m_subtype.cached_size());
			m_subtype.write(w);
		}
		if (m_msgfeatures)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_msgfeatures);
		}
		if (!m_subject.empty())
			w.bytes(6, m_subject.get());
		if (!m_body.empty())
			w.bytes(7, m_body.get());
		if (!m_data.empty())
			w.bytes(8, m_data.get());
	}

	bool MessageDetails::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_communicationid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_msgid = v;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_maintype = true;
					if (!m_maintype.merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_subtype = true;
					if (!m_subtype.merge(sub))
						return false;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_msgfeatures = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_subject.lend(s);
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_body.lend(s);
					continue;
				}
				break;
			case 8:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MessageDetails::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MessageDetails::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MessageDetails::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SendMessageRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_message.empty())
			n += pb::len_size(1, m_message.size());
		for (const auto& v : m_npids)
			n += pb::len_size(2, v.size());
		m_cached_size = n;
		return n;
	}

	void SendMessageRequest::write(pb::writer& w) const
	{
		if (!m_message.empty())
			w.bytes(1, m_message.get());
		for (const auto& v : m_npids)
			w.bytes(2, v.get());
	}

	bool SendMessageRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_message.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npids.emplace_back().lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SendMessageRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SendMessageRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SendMessageRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t BoardInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_ranklimit)
			n += pb::tag_size(1) + pb::varint_size(m_ranklimit);
		if (m_updatemode)
			n += pb::tag_size(2) + pb::varint_size(m_updatemode);
		if (m_sortmode)
			n += pb::tag_size(3) + pb::varint_size(m_sortmode);
		if (m_uploadnumlimit)
			n += pb::tag_size(4) + pb::varint_size(m_uploadnumlimit);
		if (m_uploadsizelimit)
			n += pb::tag_size(5) + pb::varint_size(m_uploadsizelimit);
		m_cached_size = n;
		return n;
	}

	void BoardInfo::write(pb::writer& w) const
	{
		if (m_ranklimit)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_ranklimit);
		}
		if (m_updatemode)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_updatemode);
		}
		if (m_sortmode)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_sortmode);
		}
		if (m_uploadnumlimit)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_uploadnumlimit);
		}
		if (m_uploadsizelimit)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_uploadsizelimit);
		}
	}

	bool BoardInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_ranklimit = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_updatemode = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_sortmode = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_uploadnumlimit = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_uploadsizelimit = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool BoardInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void BoardInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool BoardInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RecordScoreRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		if (m_pcid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_pcid));
		if (m_score)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int64(m_score));
		if (!m_comment.empty())
			n += pb::len_size(4, m_comment.size());
		if (!m_data.empty())
			n += pb::len_size(5, m_data.size());
		m_cached_size = n;
		return n;
	}

	void RecordScoreRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		if (m_pcid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_pcid));
		}
		if (m_score)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int64(m_score));
		}
		if (!m_comment.empty())
			w.bytes(4, m_comment.get());
		if (!m_data.empty())
			w.bytes(5, m_data.get());
	}

	bool RecordScoreRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_pcid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_score = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_comment.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RecordScoreRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RecordScoreRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RecordScoreRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetScoreRangeRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		if (m_startrank)
			n += pb::tag_size(2) + pb::varint_size(m_startrank);
		if (m_numranks)
			n += pb::tag_size(3) + pb::varint_size(m_numranks);
		if (m_withcomment)
			n += pb::tag_size(4) + pb::varint_size((m_withcomment ? 1u : 0u));
		if (m_withgameinfo)
			n += pb::tag_size(5) + pb::varint_size((m_withgameinfo ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void GetScoreRangeRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		if (m_startrank)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_startrank);
		}
		if (m_numranks)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_numranks);
		}
		if (m_withcomment)
		{
			w.tag(4, pb::wire_varint);
			w.varint((m_withcomment ? 1u : 0u));
		}
		if (m_withgameinfo)
		{
			w.tag(5, pb::wire_varint);
			w.varint((m_withgameinfo ? 1u : 0u));
		}
	}

	bool GetScoreRangeRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_startrank = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_numranks = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withcomment = (v != 0);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withgameinfo = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetScoreRangeRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetScoreRangeRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetScoreRangeRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t ScoreNpIdPcId::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_npid.empty())
			n += pb::len_size(1, m_npid.size());
		if (m_pcid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_pcid));
		m_cached_size = n;
		return n;
	}

	void ScoreNpIdPcId::write(pb::writer& w) const
	{
		if (!m_npid.empty())
			w.bytes(1, m_npid.get());
		if (m_pcid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_pcid));
		}
	}

	bool ScoreNpIdPcId::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_pcid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool ScoreNpIdPcId::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void ScoreNpIdPcId::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool ScoreNpIdPcId::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetScoreNpIdRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		for (const auto& v : m_npids)
			n += pb::len_size(2, v.ByteSizeLong());
		if (m_withcomment)
			n += pb::tag_size(3) + pb::varint_size((m_withcomment ? 1u : 0u));
		if (m_withgameinfo)
			n += pb::tag_size(4) + pb::varint_size((m_withgameinfo ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void GetScoreNpIdRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		for (const auto& v : m_npids)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_withcomment)
		{
			w.tag(3, pb::wire_varint);
			w.varint((m_withcomment ? 1u : 0u));
		}
		if (m_withgameinfo)
		{
			w.tag(4, pb::wire_varint);
			w.varint((m_withgameinfo ? 1u : 0u));
		}
	}

	bool GetScoreNpIdRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_npids.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withcomment = (v != 0);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withgameinfo = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetScoreNpIdRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetScoreNpIdRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetScoreNpIdRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetScoreFriendsRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		if (m_include_self)
			n += pb::tag_size(2) + pb::varint_size((m_include_self ? 1u : 0u));
		if (m_max)
			n += pb::tag_size(3) + pb::varint_size(m_max);
		if (m_withcomment)
			n += pb::tag_size(4) + pb::varint_size((m_withcomment ? 1u : 0u));
		if (m_withgameinfo)
			n += pb::tag_size(5) + pb::varint_size((m_withgameinfo ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void GetScoreFriendsRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		if (m_include_self)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_include_self ? 1u : 0u));
		}
		if (m_max)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_max);
		}
		if (m_withcomment)
		{
			w.tag(4, pb::wire_varint);
			w.varint((m_withcomment ? 1u : 0u));
		}
		if (m_withgameinfo)
		{
			w.tag(5, pb::wire_varint);
			w.varint((m_withgameinfo ? 1u : 0u));
		}
	}

	bool GetScoreFriendsRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_include_self = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_max = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withcomment = (v != 0);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_withgameinfo = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetScoreFriendsRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetScoreFriendsRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetScoreFriendsRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t ScoreRankData::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_npid.empty())
			n += pb::len_size(1, m_npid.size());
		if (!m_onlinename.empty())
			n += pb::len_size(2, m_onlinename.size());
		if (m_pcid)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int32(m_pcid));
		if (m_rank)
			n += pb::tag_size(4) + pb::varint_size(m_rank);
		if (m_score)
			n += pb::tag_size(5) + pb::varint_size(pb::from_int64(m_score));
		if (m_hasgamedata)
			n += pb::tag_size(6) + pb::varint_size((m_hasgamedata ? 1u : 0u));
		if (m_recorddate)
			n += pb::tag_size(7) + pb::varint_size(m_recorddate);
		m_cached_size = n;
		return n;
	}

	void ScoreRankData::write(pb::writer& w) const
	{
		if (!m_npid.empty())
			w.bytes(1, m_npid.get());
		if (!m_onlinename.empty())
			w.bytes(2, m_onlinename.get());
		if (m_pcid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int32(m_pcid));
		}
		if (m_rank)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_rank);
		}
		if (m_score)
		{
			w.tag(5, pb::wire_varint);
			w.varint(pb::from_int64(m_score));
		}
		if (m_hasgamedata)
		{
			w.tag(6, pb::wire_varint);
			w.varint((m_hasgamedata ? 1u : 0u));
		}
		if (m_recorddate)
		{
			w.tag(7, pb::wire_varint);
			w.varint(m_recorddate);
		}
	}

	bool ScoreRankData::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_onlinename.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_pcid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_rank = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_score = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_hasgamedata = (v != 0);
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_recorddate = v;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool ScoreRankData::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void ScoreRankData::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool ScoreRankData::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t ScoreInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_data.empty())
			n += pb::len_size(1, m_data.size());
		m_cached_size = n;
		return n;
	}

	void ScoreInfo::write(pb::writer& w) const
	{
		if (!m_data.empty())
			w.bytes(1, m_data.get());
	}

	bool ScoreInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool ScoreInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void ScoreInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool ScoreInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetScoreResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_rankarray)
			n += pb::len_size(1, v.ByteSizeLong());
		for (const auto& v : m_commentarray)
			n += pb::len_size(2, v.size());
		for (const auto& v : m_infoarray)
			n += pb::len_size(3, v.ByteSizeLong());
		if (m_lastsortdate)
			n += pb::tag_size(4) + pb::varint_size(m_lastsortdate);
		if (m_totalrecord)
			n += pb::tag_size(5) + pb::varint_size(m_totalrecord);
		m_cached_size = n;
		return n;
	}

	void GetScoreResponse::write(pb::writer& w) const
	{
		for (const auto& v : m_rankarray)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_commentarray)
			w.bytes(2, v.get());
		for (const auto& v : m_infoarray)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_lastsortdate)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_lastsortdate);
		}
		if (m_totalrecord)
		{
			w.tag(5, pb::wire_varint);
			w.varint(m_totalrecord);
		}
	}

	bool GetScoreResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_rankarray.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_commentarray.emplace_back().lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_infoarray.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lastsortdate = v;
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_totalrecord = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetScoreResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetScoreResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetScoreResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RecordScoreGameDataRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		if (m_pcid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_pcid));
		if (m_score)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int64(m_score));
		m_cached_size = n;
		return n;
	}

	void RecordScoreGameDataRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		if (m_pcid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_pcid));
		}
		if (m_score)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int64(m_score));
		}
	}

	bool RecordScoreGameDataRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_pcid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_score = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RecordScoreGameDataRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RecordScoreGameDataRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RecordScoreGameDataRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetScoreGameDataRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_boardid)
			n += pb::tag_size(1) + pb::varint_size(m_boardid);
		if (!m_npid.empty())
			n += pb::len_size(2, m_npid.size());
		if (m_pcid)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int32(m_pcid));
		m_cached_size = n;
		return n;
	}

	void GetScoreGameDataRequest::write(pb::writer& w) const
	{
		if (m_boardid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_boardid);
		}
		if (!m_npid.empty())
			w.bytes(2, m_npid.get());
		if (m_pcid)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int32(m_pcid));
		}
	}

	bool GetScoreGameDataRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_boardid = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_pcid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetScoreGameDataRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetScoreGameDataRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetScoreGameDataRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusUser::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_vuser)
			n += pb::tag_size(1) + pb::varint_size((m_vuser ? 1u : 0u));
		if (!m_npid.empty())
			n += pb::len_size(2, m_npid.size());
		m_cached_size = n;
		return n;
	}

	void TusUser::write(pb::writer& w) const
	{
		if (m_vuser)
		{
			w.tag(1, pb::wire_varint);
			w.varint((m_vuser ? 1u : 0u));
		}
		if (!m_npid.empty())
			w.bytes(2, m_npid.get());
	}

	bool TusUser::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_vuser = (v != 0);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_npid.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusUser::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusUser::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusUser::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusVariable::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_ownerid.empty())
			n += pb::len_size(1, m_ownerid.size());
		if (m_hasdata)
			n += pb::tag_size(2) + pb::varint_size((m_hasdata ? 1u : 0u));
		if (m_lastchangeddate)
			n += pb::tag_size(3) + pb::varint_size(m_lastchangeddate);
		if (!m_lastchangedauthorid.empty())
			n += pb::len_size(4, m_lastchangedauthorid.size());
		if (m_variable)
			n += pb::tag_size(5) + pb::varint_size(pb::from_int64(m_variable));
		if (m_oldvariable)
			n += pb::tag_size(6) + pb::varint_size(pb::from_int64(m_oldvariable));
		m_cached_size = n;
		return n;
	}

	void TusVariable::write(pb::writer& w) const
	{
		if (!m_ownerid.empty())
			w.bytes(1, m_ownerid.get());
		if (m_hasdata)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_hasdata ? 1u : 0u));
		}
		if (m_lastchangeddate)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_lastchangeddate);
		}
		if (!m_lastchangedauthorid.empty())
			w.bytes(4, m_lastchangedauthorid.get());
		if (m_variable)
		{
			w.tag(5, pb::wire_varint);
			w.varint(pb::from_int64(m_variable));
		}
		if (m_oldvariable)
		{
			w.tag(6, pb::wire_varint);
			w.varint(pb::from_int64(m_oldvariable));
		}
	}

	bool TusVariable::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_ownerid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_hasdata = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lastchangeddate = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_lastchangedauthorid.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_variable = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_oldvariable = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusVariable::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusVariable::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusVariable::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusVarResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_vars)
			n += pb::len_size(1, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void TusVarResponse::write(pb::writer& w) const
	{
		for (const auto& v : m_vars)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool TusVarResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_vars.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusVarResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusVarResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusVarResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusSetMultiSlotVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (!m_slotidarray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_slotidarray)
				p += pb::varint_size(pb::from_int32(v));
			m_slotidarray_packed = p;
			n += pb::len_size(2, p);
		}
		if (!m_variablearray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_variablearray)
				p += pb::varint_size(pb::from_int64(v));
			m_variablearray_packed = p;
			n += pb::len_size(3, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusSetMultiSlotVariableRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (!m_slotidarray.empty())
		{
			w.tag(2, pb::wire_len);
			w.varint(m_slotidarray_packed);
			for (const auto v : m_slotidarray)
				w.varint(pb::from_int32(v));
		}
		if (!m_variablearray.empty())
		{
			w.tag(3, pb::wire_len);
			w.varint(m_variablearray_packed);
			for (const auto v : m_variablearray)
				w.varint(pb::from_int64(v));
		}
	}

	bool TusSetMultiSlotVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					}
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_variablearray.push_back(static_cast<std::int64_t>(v));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_variablearray.push_back(static_cast<std::int64_t>(v));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusSetMultiSlotVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusSetMultiSlotVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusSetMultiSlotVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetMultiSlotVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (!m_slotidarray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_slotidarray)
				p += pb::varint_size(pb::from_int32(v));
			m_slotidarray_packed = p;
			n += pb::len_size(2, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusGetMultiSlotVariableRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (!m_slotidarray.empty())
		{
			w.tag(2, pb::wire_len);
			w.varint(m_slotidarray_packed);
			for (const auto v : m_slotidarray)
				w.varint(pb::from_int32(v));
		}
	}

	bool TusGetMultiSlotVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetMultiSlotVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetMultiSlotVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetMultiSlotVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetMultiUserVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_users)
			n += pb::len_size(1, v.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		m_cached_size = n;
		return n;
	}

	void TusGetMultiUserVariableRequest::write(pb::writer& w) const
	{
		for (const auto& v : m_users)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
	}

	bool TusGetMultiUserVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_users.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetMultiUserVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetMultiUserVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetMultiUserVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetFriendsVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_slotid)
			n += pb::tag_size(1) + pb::varint_size(pb::from_int32(m_slotid));
		if (m_includeself)
			n += pb::tag_size(2) + pb::varint_size((m_includeself ? 1u : 0u));
		if (m_sorttype)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int32(m_sorttype));
		if (m_arraynum)
			n += pb::tag_size(4) + pb::varint_size(m_arraynum);
		m_cached_size = n;
		return n;
	}

	void TusGetFriendsVariableRequest::write(pb::writer& w) const
	{
		if (m_slotid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
		if (m_includeself)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_includeself ? 1u : 0u));
		}
		if (m_sorttype)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int32(m_sorttype));
		}
		if (m_arraynum)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_arraynum);
		}
	}

	bool TusGetFriendsVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_includeself = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_sorttype = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_arraynum = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetFriendsVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetFriendsVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetFriendsVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusAddAndGetVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		if (m_invariable)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int64(m_invariable));
		if (!m_islastchangeddate.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_islastchangeddate)
				p += pb::varint_size(v);
			m_islastchangeddate_packed = p;
			n += pb::len_size(4, p);
		}
		if (!m_islastchangedauthorid.empty())
			n += pb::len_size(5, m_islastchangedauthorid.size());
		m_cached_size = n;
		return n;
	}

	void TusAddAndGetVariableRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
		if (m_invariable)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int64(m_invariable));
		}
		if (!m_islastchangeddate.empty())
		{
			w.tag(4, pb::wire_len);
			w.varint(m_islastchangeddate_packed);
			for (const auto v : m_islastchangeddate)
				w.varint(v);
		}
		if (!m_islastchangedauthorid.empty())
			w.bytes(5, m_islastchangedauthorid.get());
	}

	bool TusAddAndGetVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_invariable = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_islastchangeddate.push_back(v);
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_islastchangeddate.push_back(v);
					}
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_islastchangedauthorid.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusAddAndGetVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusAddAndGetVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusAddAndGetVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusTryAndSetVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		if (m_opetype)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int32(m_opetype));
		if (m_variable)
			n += pb::tag_size(4) + pb::varint_size(pb::from_int64(m_variable));
		if (!m_islastchangeddate.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_islastchangeddate)
				p += pb::varint_size(v);
			m_islastchangeddate_packed = p;
			n += pb::len_size(5, p);
		}
		if (!m_islastchangedauthorid.empty())
			n += pb::len_size(6, m_islastchangedauthorid.size());
		if (!m_comparevalue.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_comparevalue)
				p += pb::varint_size(pb::from_int64(v));
			m_comparevalue_packed = p;
			n += pb::len_size(7, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusTryAndSetVariableRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
		if (m_opetype)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int32(m_opetype));
		}
		if (m_variable)
		{
			w.tag(4, pb::wire_varint);
			w.varint(pb::from_int64(m_variable));
		}
		if (!m_islastchangeddate.empty())
		{
			w.tag(5, pb::wire_len);
			w.varint(m_islastchangeddate_packed);
			for (const auto v : m_islastchangeddate)
				w.varint(v);
		}
		if (!m_islastchangedauthorid.empty())
			w.bytes(6, m_islastchangedauthorid.get());
		if (!m_comparevalue.empty())
		{
			w.tag(7, pb::wire_len);
			w.varint(m_comparevalue_packed);
			for (const auto v : m_comparevalue)
				w.varint(pb::from_int64(v));
		}
	}

	bool TusTryAndSetVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_opetype = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_variable = static_cast<std::int64_t>(v);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_islastchangeddate.push_back(v);
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_islastchangeddate.push_back(v);
					}
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_islastchangedauthorid.lend(s);
					continue;
				}
				break;
			case 7:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_comparevalue.push_back(static_cast<std::int64_t>(v));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_comparevalue.push_back(static_cast<std::int64_t>(v));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusTryAndSetVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusTryAndSetVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusTryAndSetVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusDeleteMultiSlotVariableRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (!m_slotidarray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_slotidarray)
				p += pb::varint_size(pb::from_int32(v));
			m_slotidarray_packed = p;
			n += pb::len_size(2, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusDeleteMultiSlotVariableRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (!m_slotidarray.empty())
		{
			w.tag(2, pb::wire_len);
			w.varint(m_slotidarray_packed);
			for (const auto v : m_slotidarray)
				w.varint(pb::from_int32(v));
		}
	}

	bool TusDeleteMultiSlotVariableRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusDeleteMultiSlotVariableRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusDeleteMultiSlotVariableRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusDeleteMultiSlotVariableRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusSetDataRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		if (!m_data.empty())
			n += pb::len_size(3, m_data.size());
		if (!m_info.empty())
			n += pb::len_size(4, m_info.size());
		if (!m_islastchangeddate.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_islastchangeddate)
				p += pb::varint_size(v);
			m_islastchangeddate_packed = p;
			n += pb::len_size(5, p);
		}
		if (!m_islastchangedauthorid.empty())
			n += pb::len_size(6, m_islastchangedauthorid.size());
		m_cached_size = n;
		return n;
	}

	void TusSetDataRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
		if (!m_data.empty())
			w.bytes(3, m_data.get());
		if (!m_info.empty())
			w.bytes(4, m_info.get());
		if (!m_islastchangeddate.empty())
		{
			w.tag(5, pb::wire_len);
			w.varint(m_islastchangeddate_packed);
			for (const auto v : m_islastchangeddate)
				w.varint(v);
		}
		if (!m_islastchangedauthorid.empty())
			w.bytes(6, m_islastchangedauthorid.get());
	}

	bool TusSetDataRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_info.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_islastchangeddate.push_back(v);
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_islastchangeddate.push_back(v);
					}
					continue;
				}
				break;
			case 6:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_islastchangedauthorid.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusSetDataRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusSetDataRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusSetDataRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusDataStatus::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_ownerid.empty())
			n += pb::len_size(1, m_ownerid.size());
		if (m_hasdata)
			n += pb::tag_size(2) + pb::varint_size((m_hasdata ? 1u : 0u));
		if (m_lastchangeddate)
			n += pb::tag_size(3) + pb::varint_size(m_lastchangeddate);
		if (!m_lastchangedauthorid.empty())
			n += pb::len_size(4, m_lastchangedauthorid.size());
		if (!m_info.empty())
			n += pb::len_size(5, m_info.size());
		m_cached_size = n;
		return n;
	}

	void TusDataStatus::write(pb::writer& w) const
	{
		if (!m_ownerid.empty())
			w.bytes(1, m_ownerid.get());
		if (m_hasdata)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_hasdata ? 1u : 0u));
		}
		if (m_lastchangeddate)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_lastchangeddate);
		}
		if (!m_lastchangedauthorid.empty())
			w.bytes(4, m_lastchangedauthorid.get());
		if (!m_info.empty())
			w.bytes(5, m_info.get());
	}

	bool TusDataStatus::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_ownerid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_hasdata = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_lastchangeddate = v;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_lastchangedauthorid.lend(s);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_info.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusDataStatus::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusDataStatus::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusDataStatus::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusData::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_status)
			n += pb::len_size(1, m_status.ByteSizeLong());
		if (!m_data.empty())
			n += pb::len_size(2, m_data.size());
		m_cached_size = n;
		return n;
	}

	void TusData::write(pb::writer& w) const
	{
		if (m_has_status)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_status.cached_size());
			m_status.write(w);
		}
		if (!m_data.empty())
			w.bytes(2, m_data.get());
	}

	bool TusData::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_status = true;
					if (!m_status.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusData::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusData::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusData::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusDataStatusResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_status)
			n += pb::len_size(1, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void TusDataStatusResponse::write(pb::writer& w) const
	{
		for (const auto& v : m_status)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool TusDataStatusResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_status.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusDataStatusResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusDataStatusResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusDataStatusResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetDataRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		m_cached_size = n;
		return n;
	}

	void TusGetDataRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
	}

	bool TusGetDataRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetDataRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetDataRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetDataRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetMultiSlotDataStatusRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (!m_slotidarray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_slotidarray)
				p += pb::varint_size(pb::from_int32(v));
			m_slotidarray_packed = p;
			n += pb::len_size(2, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusGetMultiSlotDataStatusRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (!m_slotidarray.empty())
		{
			w.tag(2, pb::wire_len);
			w.varint(m_slotidarray_packed);
			for (const auto v : m_slotidarray)
				w.varint(pb::from_int32(v));
		}
	}

	bool TusGetMultiSlotDataStatusRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetMultiSlotDataStatusRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetMultiSlotDataStatusRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetMultiSlotDataStatusRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetMultiUserDataStatusRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_users)
			n += pb::len_size(1, v.ByteSizeLong());
		if (m_slotid)
			n += pb::tag_size(2) + pb::varint_size(pb::from_int32(m_slotid));
		m_cached_size = n;
		return n;
	}

	void TusGetMultiUserDataStatusRequest::write(pb::writer& w) const
	{
		for (const auto& v : m_users)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_slotid)
		{
			w.tag(2, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
	}

	bool TusGetMultiUserDataStatusRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_users.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetMultiUserDataStatusRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetMultiUserDataStatusRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetMultiUserDataStatusRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusGetFriendsDataStatusRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_slotid)
			n += pb::tag_size(1) + pb::varint_size(pb::from_int32(m_slotid));
		if (m_includeself)
			n += pb::tag_size(2) + pb::varint_size((m_includeself ? 1u : 0u));
		if (m_sorttype)
			n += pb::tag_size(3) + pb::varint_size(pb::from_int32(m_sorttype));
		if (m_arraynum)
			n += pb::tag_size(4) + pb::varint_size(m_arraynum);
		m_cached_size = n;
		return n;
	}

	void TusGetFriendsDataStatusRequest::write(pb::writer& w) const
	{
		if (m_slotid)
		{
			w.tag(1, pb::wire_varint);
			w.varint(pb::from_int32(m_slotid));
		}
		if (m_includeself)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_includeself ? 1u : 0u));
		}
		if (m_sorttype)
		{
			w.tag(3, pb::wire_varint);
			w.varint(pb::from_int32(m_sorttype));
		}
		if (m_arraynum)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_arraynum);
		}
	}

	bool TusGetFriendsDataStatusRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotid = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_includeself = (v != 0);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_sorttype = static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_arraynum = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusGetFriendsDataStatusRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusGetFriendsDataStatusRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusGetFriendsDataStatusRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t TusDeleteMultiSlotDataRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_user)
			n += pb::len_size(1, m_user.ByteSizeLong());
		if (!m_slotidarray.empty())
		{
			std::size_t p = 0;
			for (const auto v : m_slotidarray)
				p += pb::varint_size(pb::from_int32(v));
			m_slotidarray_packed = p;
			n += pb::len_size(2, p);
		}
		m_cached_size = n;
		return n;
	}

	void TusDeleteMultiSlotDataRequest::write(pb::writer& w) const
	{
		if (m_has_user)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_user.cached_size());
			m_user.write(w);
		}
		if (!m_slotidarray.empty())
		{
			w.tag(2, pb::wire_len);
			w.varint(m_slotidarray_packed);
			for (const auto v : m_slotidarray)
				w.varint(pb::from_int32(v));
		}
	}

	bool TusDeleteMultiSlotDataRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_user = true;
					if (!m_user.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					continue;
				}
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader p(s.data(), s.size());
					while (!p.done())
					{
						std::uint64_t v;
						if (!p.varint(v))
							return false;
						m_slotidarray.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
					}
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool TusDeleteMultiSlotDataRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void TusDeleteMultiSlotDataRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool TusDeleteMultiSlotDataRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetPresenceRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_title.empty())
			n += pb::len_size(1, m_title.size());
		if (!m_status.empty())
			n += pb::len_size(2, m_status.size());
		if (!m_comment.empty())
			n += pb::len_size(3, m_comment.size());
		if (!m_data.empty())
			n += pb::len_size(4, m_data.size());
		m_cached_size = n;
		return n;
	}

	void SetPresenceRequest::write(pb::writer& w) const
	{
		if (!m_title.empty())
			w.bytes(1, m_title.get());
		if (!m_status.empty())
			w.bytes(2, m_status.get());
		if (!m_comment.empty())
			w.bytes(3, m_comment.get());
		if (!m_data.empty())
			w.bytes(4, m_data.get());
	}

	bool SetPresenceRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_title.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_status.lend(s);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_comment.lend(s);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetPresenceRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetPresenceRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetPresenceRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingSearchCondition::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_attr_type)
			n += pb::tag_size(1) + pb::varint_size(m_attr_type);
		if (m_attr_id)
			n += pb::tag_size(2) + pb::varint_size(m_attr_id);
		if (m_comp_op)
			n += pb::tag_size(3) + pb::varint_size(m_comp_op);
		if (m_comp_value)
			n += pb::tag_size(4) + pb::varint_size(m_comp_value);
		m_cached_size = n;
		return n;
	}

	void MatchingSearchCondition::write(pb::writer& w) const
	{
		if (m_attr_type)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_attr_type);
		}
		if (m_attr_id)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_attr_id);
		}
		if (m_comp_op)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_comp_op);
		}
		if (m_comp_value)
		{
			w.tag(4, pb::wire_varint);
			w.varint(m_comp_value);
		}
	}

	bool MatchingSearchCondition::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_attr_type = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_attr_id = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_comp_op = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_comp_value = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingSearchCondition::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingSearchCondition::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingSearchCondition::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingAttr::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_attr_type)
			n += pb::tag_size(1) + pb::varint_size(m_attr_type);
		if (m_attr_id)
			n += pb::tag_size(2) + pb::varint_size(m_attr_id);
		if (m_num)
			n += pb::tag_size(3) + pb::varint_size(m_num);
		if (!m_data.empty())
			n += pb::len_size(4, m_data.size());
		m_cached_size = n;
		return n;
	}

	void MatchingAttr::write(pb::writer& w) const
	{
		if (m_attr_type)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_attr_type);
		}
		if (m_attr_id)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_attr_id);
		}
		if (m_num)
		{
			w.tag(3, pb::wire_varint);
			w.varint(m_num);
		}
		if (!m_data.empty())
			w.bytes(4, m_data.get());
	}

	bool MatchingAttr::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_attr_type = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_attr_id = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_num = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_data.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingAttr::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingAttr::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingAttr::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t CreateRoomGUIRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_total_slots)
			n += pb::tag_size(1) + pb::varint_size(m_total_slots);
		if (m_private_slots)
			n += pb::tag_size(2) + pb::varint_size(m_private_slots);
		if (m_privilege_grant)
			n += pb::tag_size(3) + pb::varint_size((m_privilege_grant ? 1u : 0u));
		if (m_stealth)
			n += pb::tag_size(4) + pb::varint_size((m_stealth ? 1u : 0u));
		for (const auto& v : m_game_attrs)
			n += pb::len_size(5, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void CreateRoomGUIRequest::write(pb::writer& w) const
	{
		if (m_total_slots)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_total_slots);
		}
		if (m_private_slots)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_private_slots);
		}
		if (m_privilege_grant)
		{
			w.tag(3, pb::wire_varint);
			w.varint((m_privilege_grant ? 1u : 0u));
		}
		if (m_stealth)
		{
			w.tag(4, pb::wire_varint);
			w.varint((m_stealth ? 1u : 0u));
		}
		for (const auto& v : m_game_attrs)
		{
			w.tag(5, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool CreateRoomGUIRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_total_slots = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_private_slots = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_privilege_grant = (v != 0);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_stealth = (v != 0);
					continue;
				}
				break;
			case 5:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_game_attrs.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool CreateRoomGUIRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void CreateRoomGUIRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool CreateRoomGUIRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GUIUserInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_info)
			n += pb::len_size(1, m_info.ByteSizeLong());
		if (m_owner)
			n += pb::tag_size(2) + pb::varint_size((m_owner ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void GUIUserInfo::write(pb::writer& w) const
	{
		if (m_has_info)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_info.cached_size());
			m_info.write(w);
		}
		if (m_owner)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_owner ? 1u : 0u));
		}
	}

	bool GUIUserInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_info = true;
					if (!m_info.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_owner = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GUIUserInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GUIUserInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GUIUserInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingRoomStatus::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_id.empty())
			n += pb::len_size(1, m_id.size());
		for (const auto& v : m_members)
			n += pb::len_size(2, v.ByteSizeLong());
		if (!m_kick_actor.empty())
			n += pb::len_size(3, m_kick_actor.size());
		if (!m_opt.empty())
			n += pb::len_size(4, m_opt.size());
		m_cached_size = n;
		return n;
	}

	void MatchingRoomStatus::write(pb::writer& w) const
	{
		if (!m_id.empty())
			w.bytes(1, m_id.get());
		for (const auto& v : m_members)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (!m_kick_actor.empty())
			w.bytes(3, m_kick_actor.get());
		if (!m_opt.empty())
			w.bytes(4, m_opt.get());
	}

	bool MatchingRoomStatus::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_id.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_members.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s) || !pb::utf8_valid(s))
						return false;
					m_kick_actor.lend(s);
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_opt.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingRoomStatus::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingRoomStatus::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingRoomStatus::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomListGUIRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_range_start)
			n += pb::tag_size(1) + pb::varint_size(m_range_start);
		if (m_range_max)
			n += pb::tag_size(2) + pb::varint_size(m_range_max);
		for (const auto& v : m_conds)
			n += pb::len_size(3, v.ByteSizeLong());
		for (const auto& v : m_attrs)
			n += pb::len_size(4, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomListGUIRequest::write(pb::writer& w) const
	{
		if (m_range_start)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_range_start);
		}
		if (m_range_max)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_range_max);
		}
		for (const auto& v : m_conds)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_attrs)
		{
			w.tag(4, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomListGUIRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_range_start = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_range_max = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_conds.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 4:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrs.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomListGUIRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomListGUIRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomListGUIRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingRoom::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_id.empty())
			n += pb::len_size(1, m_id.size());
		for (const auto& v : m_attr)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void MatchingRoom::write(pb::writer& w) const
	{
		if (!m_id.empty())
			w.bytes(1, m_id.get());
		for (const auto& v : m_attr)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool MatchingRoom::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_id.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attr.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingRoom::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingRoom::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingRoom::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingRoomList::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_start)
			n += pb::tag_size(1) + pb::varint_size(m_start);
		if (m_total)
			n += pb::tag_size(2) + pb::varint_size(m_total);
		for (const auto& v : m_rooms)
			n += pb::len_size(3, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void MatchingRoomList::write(pb::writer& w) const
	{
		if (m_start)
		{
			w.tag(1, pb::wire_varint);
			w.varint(m_start);
		}
		if (m_total)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_total);
		}
		for (const auto& v : m_rooms)
		{
			w.tag(3, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool MatchingRoomList::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_start = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_total = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_rooms.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingRoomList::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingRoomList::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingRoomList::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingGuiRoomId::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_id.empty())
			n += pb::len_size(1, m_id.size());
		m_cached_size = n;
		return n;
	}

	void MatchingGuiRoomId::write(pb::writer& w) const
	{
		if (!m_id.empty())
			w.bytes(1, m_id.get());
	}

	bool MatchingGuiRoomId::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_id.lend(s);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingGuiRoomId::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingGuiRoomId::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingGuiRoomId::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SetRoomSearchFlagGUI::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (!m_roomid.empty())
			n += pb::len_size(1, m_roomid.size());
		if (m_stealth)
			n += pb::tag_size(2) + pb::varint_size((m_stealth ? 1u : 0u));
		m_cached_size = n;
		return n;
	}

	void SetRoomSearchFlagGUI::write(pb::writer& w) const
	{
		if (!m_roomid.empty())
			w.bytes(1, m_roomid.get());
		if (m_stealth)
		{
			w.tag(2, pb::wire_varint);
			w.varint((m_stealth ? 1u : 0u));
		}
	}

	bool SetRoomSearchFlagGUI::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					m_roomid.lend(s);
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_stealth = (v != 0);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SetRoomSearchFlagGUI::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SetRoomSearchFlagGUI::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SetRoomSearchFlagGUI::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t QuickMatchGUIRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_conds)
			n += pb::len_size(1, v.ByteSizeLong());
		if (m_available_num)
			n += pb::tag_size(2) + pb::varint_size(m_available_num);
		m_cached_size = n;
		return n;
	}

	void QuickMatchGUIRequest::write(pb::writer& w) const
	{
		for (const auto& v : m_conds)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		if (m_available_num)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_available_num);
		}
	}

	bool QuickMatchGUIRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_conds.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_available_num = static_cast<std::uint32_t>(v);
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool QuickMatchGUIRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void QuickMatchGUIRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool QuickMatchGUIRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t SearchJoinRoomGUIRequest::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_conds)
			n += pb::len_size(1, v.ByteSizeLong());
		for (const auto& v : m_attrs)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void SearchJoinRoomGUIRequest::write(pb::writer& w) const
	{
		for (const auto& v : m_conds)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
		for (const auto& v : m_attrs)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool SearchJoinRoomGUIRequest::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_conds.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attrs.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool SearchJoinRoomGUIRequest::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void SearchJoinRoomGUIRequest::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool SearchJoinRoomGUIRequest::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t MatchingSearchJoinRoomInfo::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_room)
			n += pb::len_size(1, m_room.ByteSizeLong());
		for (const auto& v : m_attr)
			n += pb::len_size(2, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void MatchingSearchJoinRoomInfo::write(pb::writer& w) const
	{
		if (m_has_room)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_room.cached_size());
			m_room.write(w);
		}
		for (const auto& v : m_attr)
		{
			w.tag(2, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool MatchingSearchJoinRoomInfo::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_room = true;
					if (!m_room.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_attr.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool MatchingSearchJoinRoomInfo::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void MatchingSearchJoinRoomInfo::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool MatchingSearchJoinRoomInfo::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t RoomMemberDataExternal::ByteSizeLong() const
	{
		std::size_t n = 0;
		if (m_has_userinfo)
			n += pb::len_size(1, m_userinfo.ByteSizeLong());
		if (m_joindate)
			n += pb::tag_size(2) + pb::varint_size(m_joindate);
		if (m_has_role)
			n += pb::len_size(3, m_role.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void RoomMemberDataExternal::write(pb::writer& w) const
	{
		if (m_has_userinfo)
		{
			w.tag(1, pb::wire_len);
			w.varint(m_userinfo.cached_size());
			m_userinfo.write(w);
		}
		if (m_joindate)
		{
			w.tag(2, pb::wire_varint);
			w.varint(m_joindate);
		}
		if (m_has_role)
		{
			w.tag(3, pb::wire_len);
			w.varint(m_role.cached_size());
			m_role.write(w);
		}
	}

	bool RoomMemberDataExternal::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_userinfo = true;
					if (!m_userinfo.merge(sub))
						return false;
					continue;
				}
				break;
			case 2:
				if (wt == pb::wire_varint)
				{
					std::uint64_t v;
					if (!r.varint(v))
						return false;
					m_joindate = v;
					continue;
				}
				break;
			case 3:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					m_has_role = true;
					if (!m_role.merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool RoomMemberDataExternal::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void RoomMemberDataExternal::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool RoomMemberDataExternal::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}

	std::size_t GetRoomMemberDataExternalListResponse::ByteSizeLong() const
	{
		std::size_t n = 0;
		for (const auto& v : m_members)
			n += pb::len_size(1, v.ByteSizeLong());
		m_cached_size = n;
		return n;
	}

	void GetRoomMemberDataExternalListResponse::write(pb::writer& w) const
	{
		for (const auto& v : m_members)
		{
			w.tag(1, pb::wire_len);
			w.varint(v.cached_size());
			v.write(w);
		}
	}

	bool GetRoomMemberDataExternalListResponse::merge(pb::reader& r)
	{
		std::uint32_t f, wt;
		while (!r.done())
		{
			if (!r.tag(f, wt))
				return false;
			switch (f)
			{
			case 1:
				if (wt == pb::wire_len)
				{
					std::string_view s;
					if (!r.len(s))
						return false;
					pb::reader sub(s.data(), s.size());
					if (!m_members.emplace_back().merge(sub))
						return false;
					continue;
				}
				break;
			default:
				break;
			}
			if (!r.skip(f, wt))
				return false;
		}
		return true;
	}

	bool GetRoomMemberDataExternalListResponse::ParseFromArray(const void* data, std::size_t size)
	{
		*this = {};
		pb::reader r(data, size);
		return merge(r);
	}

	void GetRoomMemberDataExternalListResponse::SerializeWithCachedSizesToArray(void* out) const
	{
		pb::writer w(out);
		write(w);
	}

	bool GetRoomMemberDataExternalListResponse::SerializeToString(std::string* out) const
	{
		out->resize(ByteSizeLong());
		SerializeWithCachedSizesToArray(out->data());
		return true;
	}
} // namespace np2_structs
