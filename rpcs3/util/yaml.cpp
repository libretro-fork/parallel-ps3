#include "util/yaml.hpp"
#include "util/types.hpp"
#include "Utilities/StrFmt.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <type_traits>

const char* yaml_node::type_name() const
{
	if (is_map())
		return "Map";
	if (is_seq())
		return "Sequence";
	if (is_scalar())
		return "Scalar";
	return "Null";
}

yaml_node yaml_node::operator[](std::size_t index) const
{
	int c = ryaml_first_child(m_y, m_n);
	while (c >= 0 && index--)
		c = ryaml_next_sibling(m_y, c);
	return {m_y, c};
}

std::string yaml_node::location() const
{
	std::size_t line = 0, col = 0;
	if (!ryaml_location(m_y, m_n, &line, &col))
		return "unknown";
	return fmt::format("line %d, column %d", line, col);
}

void yaml_doc::parse(std::string_view text)
{
	std::size_t line = 0, col = 0;
	m_y = ryaml_parse_ex(text.data(), text.size(), &line, &col);
	if (!m_y)
		m_error = line ? fmt::format("YAML: cannot read line %d, column %d", line, col) : "YAML: out of memory";
}

yaml_doc::yaml_doc(std::string&& text)
	: m_text(std::move(text))
{
	parse(m_text);
}

yaml_doc::yaml_doc(std::string_view text)
{
	parse(text);
}

yaml_doc::~yaml_doc()
{
	ryaml_free(m_y);
}

template <typename T>
T get_yaml_node_value(const yaml_node& node, std::string& error_message)
{
	if (!node.is_scalar())
	{
		error_message = fmt::format("expected a scalar, found %s", node.type_name());
		return {};
	}

	const std::string_view s = node.scalar();

	if constexpr (std::is_same_v<T, std::string>)
	{
		return std::string(s);
	}
	else if constexpr (std::is_same_v<T, bool>)
	{
		// YAML's spellings: true/false, yes/no, on/off, in any case
		std::string lower(s);
		for (char& c : lower)
			c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		if (lower == "true" || lower == "yes" || lower == "on" || lower == "y")
			return true;
		if (lower == "false" || lower == "no" || lower == "off" || lower == "n")
			return false;
		error_message = fmt::format("bad conversion: '%s' is not a boolean", s);
		return false;
	}
	else if constexpr (std::is_integral_v<T>)
	{
		// Decimal or 0x hex, the sign where the type has one, all of it
		std::string_view digits = s;
		bool negative = false;
		if (!digits.empty() && (digits[0] == '-' || digits[0] == '+'))
		{
			negative = digits[0] == '-';
			digits.remove_prefix(1);
		}
		int base = 10;
		if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
		{
			base = 16;
			digits.remove_prefix(2);
		}
		u64 magnitude = 0;
		const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), magnitude, base);
		bool ok = !digits.empty() && ec == std::errc{} && end == digits.data() + digits.size();
		T value{};
		if (ok && negative)
		{
			if constexpr (std::is_signed_v<T>)
			{
				ok = magnitude <= static_cast<u64>(std::numeric_limits<T>::max()) + 1;
				value = static_cast<T>(0 - magnitude);
			}
			else
			{
				ok = magnitude == 0;
			}
		}
		else if (ok)
		{
			ok = magnitude <= static_cast<u64>(std::numeric_limits<T>::max());
			value = static_cast<T>(magnitude);
		}
		if (!ok)
		{
			error_message = fmt::format("bad conversion: '%s' is not a %s", s, std::is_signed_v<T> ? "signed integer in range" : "unsigned integer in range");
			return {};
		}
		return value;
	}
	else
	{
		static_assert(std::is_floating_point_v<T>);
		// YAML's spellings of infinity and NaN, then the number, all of it
		if (s == ".inf" || s == ".Inf" || s == ".INF" || s == "+.inf" || s == "+.Inf" || s == "+.INF")
			return std::numeric_limits<T>::infinity();
		if (s == "-.inf" || s == "-.Inf" || s == "-.INF")
			return -std::numeric_limits<T>::infinity();
		if (s == ".nan" || s == ".NaN" || s == ".NAN")
			return std::numeric_limits<T>::quiet_NaN();
		const std::string str(s);
		char* end = nullptr;
		const double v = std::strtod(str.c_str(), &end);
		if (str.empty() || end != str.c_str() + str.size())
		{
			error_message = fmt::format("bad conversion: '%s' is not a number", s);
			return {};
		}
		return static_cast<T>(v);
	}
}

yaml_writer& yaml_writer::value_f64(double v)
{
	char buf[32];
	const auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
	return value(ec == std::errc{} ? std::string_view(buf, end - buf) : std::string_view("0"));
}

template bool get_yaml_node_value<bool>(const yaml_node&, std::string&);
template u8 get_yaml_node_value<u8>(const yaml_node&, std::string&);
template s8 get_yaml_node_value<s8>(const yaml_node&, std::string&);
template u16 get_yaml_node_value<u16>(const yaml_node&, std::string&);
template s16 get_yaml_node_value<s16>(const yaml_node&, std::string&);
template u32 get_yaml_node_value<u32>(const yaml_node&, std::string&);
template s32 get_yaml_node_value<s32>(const yaml_node&, std::string&);
template u64 get_yaml_node_value<u64>(const yaml_node&, std::string&);
template s64 get_yaml_node_value<s64>(const yaml_node&, std::string&);
template f32 get_yaml_node_value<f32>(const yaml_node&, std::string&);
template f64 get_yaml_node_value<f64>(const yaml_node&, std::string&);
template std::string get_yaml_node_value<std::string>(const yaml_node&, std::string&);
