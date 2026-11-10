#pragma once

// The Protocol Buffers wire format, as RPCN's messages (np2_structs, made
// from generated/np2_structs.proto by generated/np2_structs_gen.py) are read
// and written: variable-length integers and length-delimited fields.
//
// Reading borrows: a string or bytes field read from a packet points into
// it, so a message read with ParseFromArray is valid while the packet is.
// Writing goes straight into the caller's buffer, sized beforehand.
//
// What is read and written is what the protobuf library did: fields in
// number order, proto3 defaults left out, repeated numbers packed; on
// reading, unknown fields and groups skipped, repeated numbers packed or
// not, a field seen twice taken as protobuf takes it, and a string that
// is not UTF-8 refused.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace pb
{
	enum wire_type : std::uint32_t
	{
		wire_varint = 0,
		wire_fixed64 = 1,
		wire_len = 2,
		wire_group_start = 3,
		wire_group_end = 4,
		wire_fixed32 = 5,
	};

	// A string or bytes field: borrowed from the packet it was read from,
	// or owned when set
	class text
	{
		std::string m_own;
		std::string_view m_view;
		bool m_owned = false;

	public:
		std::string_view get() const { return m_owned ? std::string_view(m_own) : m_view; }
		operator std::string_view() const { return get(); }
		std::size_t size() const { return get().size(); }
		bool empty() const { return get().empty(); }

		void lend(std::string_view v) { m_view = v; m_owned = false; }
		void set(std::string_view v) { m_own.assign(v.data(), v.size()); m_owned = true; }
		void set(std::string&& v) { m_own = std::move(v); m_owned = true; }
		void set(const char* v) { set(std::string_view(v)); }
		void set(const void* p, std::size_t n) { m_own.assign(static_cast<const char*>(p), n); m_owned = true; }
	};

	inline std::size_t varint_size(std::uint64_t v)
	{
		std::size_t n = 1;
		while (v >= 0x80)
		{
			v >>= 7;
			n++;
		}
		return n;
	}

	// A tag's size, for field numbers up to 2^29
	inline std::size_t tag_size(std::uint32_t field)
	{
		return varint_size(static_cast<std::uint64_t>(field) << 3);
	}

	// int32 is written sign-extended to 64 bits, as protobuf does
	inline std::uint64_t from_int32(std::int32_t v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(v)); }
	inline std::uint64_t from_int64(std::int64_t v) { return static_cast<std::uint64_t>(v); }

	inline std::size_t len_size(std::uint32_t field, std::size_t n)
	{
		return tag_size(field) + varint_size(n) + n;
	}

	// RFC 3629 UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF
	inline bool utf8_valid(std::string_view s)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(s.data());
		const auto* end = p + s.size();
		while (p < end)
		{
			const unsigned c = *p;
			if (c < 0x80)
			{
				p++;
				continue;
			}
			std::size_t n;
			unsigned lo = 0x80, hi = 0xBF;
			if (c >= 0xC2 && c <= 0xDF)
				n = 1;
			else if (c >= 0xE0 && c <= 0xEF)
			{
				n = 2;
				if (c == 0xE0)
					lo = 0xA0;
				else if (c == 0xED)
					hi = 0x9F;
			}
			else if (c >= 0xF0 && c <= 0xF4)
			{
				n = 3;
				if (c == 0xF0)
					lo = 0x90;
				else if (c == 0xF4)
					hi = 0x8F;
			}
			else
				return false;
			if (static_cast<std::size_t>(end - p) <= n)
				return false;
			if (p[1] < lo || p[1] > hi)
				return false;
			for (std::size_t i = 2; i <= n; i++)
			{
				if (p[i] < 0x80 || p[i] > 0xBF)
					return false;
			}
			p += n + 1;
		}
		return true;
	}

	class reader
	{
		const unsigned char* m_p;
		const unsigned char* m_end;

	public:
		reader(const void* data, std::size_t size)
			: m_p(static_cast<const unsigned char*>(data)), m_end(m_p + size) {}

		bool done() const { return m_p >= m_end; }

		// Up to ten bytes; more is an error, as in protobuf
		bool varint(std::uint64_t& v)
		{
			v = 0;
			for (unsigned shift = 0; shift < 70; shift += 7)
			{
				if (m_p >= m_end)
					return false;
				const unsigned b = *m_p++;
				v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
				if (!(b & 0x80))
					return true;
			}
			return false;
		}

		// A field's number and wire type; number 0 is an error
		bool tag(std::uint32_t& field, std::uint32_t& wire)
		{
			std::uint64_t t;
			if (!varint(t) || t > 0xFFFFFFFFu)
				return false;
			field = static_cast<std::uint32_t>(t >> 3);
			wire = static_cast<std::uint32_t>(t & 7);
			return field != 0;
		}

		bool len(std::string_view& out)
		{
			std::uint64_t n;
			if (!varint(n) || n > static_cast<std::uint64_t>(m_end - m_p) || n > 0x7FFFFFFFu)
				return false;
			out = std::string_view(reinterpret_cast<const char*>(m_p), static_cast<std::size_t>(n));
			m_p += n;
			return true;
		}

		bool fixed(std::size_t n)
		{
			if (static_cast<std::size_t>(m_end - m_p) < n)
				return false;
			m_p += n;
			return true;
		}

		// Skips a field of wire type @wire whose tag was read: a group up to
		// the end tag with its number
		bool skip(std::uint32_t field, std::uint32_t wire, int depth = 0)
		{
			std::uint64_t v;
			std::string_view s;
			switch (wire)
			{
			case wire_varint: return varint(v);
			case wire_fixed64: return fixed(8);
			case wire_len: return len(s);
			case wire_fixed32: return fixed(4);
			case wire_group_start:
			{
				if (depth >= 100)
					return false;
				std::uint32_t f, w;
				while (tag(f, w))
				{
					if (w == wire_group_end)
						return f == field;
					if (!skip(f, w, depth + 1))
						return false;
				}
				return false;
			}
			default: return false;
			}
		}
	};

	// Writes into a buffer the message's size was taken for
	class writer
	{
		unsigned char* m_p;

	public:
		explicit writer(void* out) : m_p(static_cast<unsigned char*>(out)) {}

		unsigned char* pos() const { return m_p; }

		void varint(std::uint64_t v)
		{
			while (v >= 0x80)
			{
				*m_p++ = static_cast<unsigned char>(v | 0x80);
				v >>= 7;
			}
			*m_p++ = static_cast<unsigned char>(v);
		}

		void tag(std::uint32_t field, std::uint32_t wire)
		{
			varint((static_cast<std::uint64_t>(field) << 3) | wire);
		}

		void bytes(std::uint32_t field, std::string_view s)
		{
			tag(field, wire_len);
			varint(s.size());
			if (!s.empty())
				std::memcpy(m_p, s.data(), s.size());
			m_p += s.size();
		}
	};
} // namespace pb
