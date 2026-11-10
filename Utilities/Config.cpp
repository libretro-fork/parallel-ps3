#include "stdafx.h"
#include "Config.h"
#include "util/types.hpp"
#include "util/yaml.hpp"
#include "util/cctype.hpp"

#include <charconv>

LOG_CHANNEL(cfg_log, "CFG");

template <>
void fmt_class_string<cfg::node>::format(std::string& out, u64 arg)
{
 	out += get_object(arg).to_string();
}

namespace cfg
{
	u32 _base::id_counter = 0;

	_base::_base(type _type)
		: m_type(_type), m_id(id_counter++)
	{
		if (_type != type::node)
		{
			cfg_log.fatal("Invalid root node");
		}
	}

	_base::_base(type _type, node* owner, std::string name, bool dynamic)
		: m_type(_type), m_parent(owner), m_dynamic(dynamic), m_name(std::move(name)), m_id(id_counter++)
	{
		for (const auto& node : owner->m_nodes)
		{
			if (node->get_name() == m_name)
			{
				cfg_log.fatal("Node already exists: %s", m_name);
			}
		}

		owner->m_nodes.emplace_back(this);
	}

	bool _base::from_string(std::string_view /*value*/, bool /*dynamic*/)
	{
		cfg_log.fatal("cfg::_base::from_string() purecall");
		return false;
	}

	bool _base::from_list(std::vector<std::string>&&)
	{
		cfg_log.fatal("cfg::_base::from_list() purecall");
		return false;
	}

	bool _base::save(std::string_view cfg_name) const
	{
		if (fs::pending_file cfg_file(cfg_name); !!cfg_file.file)
		{
			cfg_file.file.write(to_string());
			return cfg_file.commit();
		}

		return false;
	}

	// Emit YAML
	static void encode(yaml_writer& out, const class _base& rhs);

	// Incrementally load config entries from a YAML node.
	// The config value is preserved if the corresponding YAML node doesn't exist.
	[[nodiscard]] static bool decode(const yaml_node& data, class _base& rhs, bool dynamic, bool strict);
}

std::vector<std::string> cfg::make_int_range(s64 min, s64 max)
{
	return {std::to_string(min), std::to_string(max)};
}

bool try_to_int64(s64* out, std::string_view value, s64 min, s64 max, std::string_view name)
{
	if (value.empty())
	{
		if (out) cfg_log.error("cfg::try_to_int64('%s'): called with an empty string", name);
		return false;
	}

	s64 result;
	const char* start = value.data();
	const char* end = start + value.size();
	int base = 10;
	int sign = +1;

	if (start[0] == '-')
	{
		sign = -1;
		start += 1;
	}

	if (start[0] == '0' && value.size() >= 2 && (start[1] == 'x' || start[1] == 'X'))
	{
		// Limited hex support
		base = 16;
		start += 2;
	}

	const auto ret = std::from_chars(start, end, result, base);

	if (ret.ec != std::errc() || ret.ptr != end || (start[0] == '-' && sign < 0))
	{
		if (out) cfg_log.error("cfg::try_to_int64('%s', '%s'): invalid integer", value, name);
		return false;
	}

	result *= sign;

	if (result < min || result > max)
	{
		if (out) cfg_log.error("cfg::try_to_int64('%s', '%s'): out of bounds (val=%d, min=%d, max=%d)", value, name, result, min, max);
		return false;
	}

	if (out) *out = result;
	return true;
}

std::vector<std::string> cfg::make_uint_range(u64 min, u64 max)
{
	return {std::to_string(min), std::to_string(max)};
}

bool try_to_uint64(u64* out, std::string_view value, u64 min, u64 max, std::string_view name)
{
	if (value.empty())
	{
		if (out) cfg_log.error("cfg::try_to_uint64('%s'): called with an empty string", name);
		return false;
	}

	u64 result;
	const char* start = value.data();
	const char* end = start + value.size();
	int base = 10;

	if (start[0] == '0' && value.size() >= 2 && (start[1] == 'x' || start[1] == 'X'))
	{
		// Limited hex support
		base = 16;
		start += 2;
	}

	const auto ret = std::from_chars(start, end, result, base);

	if (ret.ec != std::errc() || ret.ptr != end)
	{
		if (out) cfg_log.error("cfg::try_to_uint64('%s', '%s'): invalid integer", value, name);
		return false;
	}

	if (result < min || result > max)
	{
		if (out) cfg_log.error("cfg::try_to_uint64('%s', '%s'): out of bounds (val=%u, min=%u, max=%u)", value, name, result, min, max);
		return false;
	}

	if (out) *out = result;
	return true;
}

bool try_to_uint128(u128* out, std::string_view value, std::string_view name)
{
	if (value.empty())
	{
		if (out) cfg_log.error("cfg::try_to_uint128('%s'): called with an empty string", name);
		return false;
	}

	u64 result_low = 0, result_high = 0;
	const char* start_high64 = value.data();
	const char* end = value.data() + value.size();

	if (start_high64[0] == '0' && value.size() >= 2 && (start_high64[1] == 'x' || start_high64[1] == 'X'))
	{
		// Hex support
		start_high64 += 2;
	}

	const char* start_low64 = end - std::min<usz>(end - start_high64, 16);

	// Hexadecimal-only
	constexpr int base = 16;

	auto ret = std::from_chars(start_low64, end, result_low, base);

	if (ret.ec != std::errc() || ret.ptr != end)
	{
		if (out) cfg_log.error("cfg::try_to_uint128('%s', '%s'): invalid integer", value, name);
		return false;
	}

	if (start_high64 == start_low64)
	{
		if (out) *out = result_low;
		return true;
	}

	ret = std::from_chars(start_high64, start_low64, result_high, base);

	if (ret.ec != std::errc() || ret.ptr != start_low64)
	{
		if (out) cfg_log.error("cfg::try_to_uint128('%s', '%s'): invalid integer", value, name);
		return false;
	}

	if (out) *out = result_low + (u128{result_high} << 64);
	return true;
}

std::vector<std::string> cfg::make_float_range(f64 min, f64 max)
{
	return {std::to_string(min), std::to_string(max)};
}

bool try_to_float(f64* out, std::string_view value, f64 min, f64 max, std::string_view name)
{
	if (value.empty())
	{
		if (out) cfg_log.error("cfg::try_to_float('%s'): called with an empty string", name);
		return false;
	}

	// std::from_chars float is yet to be implemented on Xcode it seems
	// And strtod doesn't support ranged view so we need to ensure it meets a null terminator
	const std::string str = std::string{value};

	char* end_check{};
	const double result = std::strtod(str.data(), &end_check);

	if (end_check != str.data() + str.size())
	{
		if (out) cfg_log.error("cfg::try_to_float('%s', '%s'): invalid float", value, name);
		return false;
	}

	if (result < min || result > max)
	{
		if (out) cfg_log.error("cfg::try_to_float('%s', '%s'): out of bounds (val=%f, min=%f, max=%f)", value, name, result, min, max);
		return false;
	}

	if (out) *out = result;
	return true;
}

bool try_to_string(std::string* out, f64 value, std::string_view name)
{
	std::array<char, 32> str{};

	if (auto [ptr, ec] = std::to_chars(str.data(), str.data() + str.size(), value, std::chars_format::fixed); ec == std::errc())
	{
		if (out) *out = std::string(str.data(), ptr);
		return true;
	}
	else
	{
		if (out) cfg_log.error("cfg::try_to_string('%s'): could not convert value '%f' to string. error='%s'", name, value, std::make_error_code(ec).message());
		return false;
	}
}

bool cfg::try_to_enum_value(u64* out, decltype(&fmt_class_string<int>::format) func, std::string_view value, std::string_view name)
{
	ensure(func);

	u64 max = umax;

	for (u64 i = 0;; i++)
	{
		std::string var;
		func(var, i);

		if (var == value)
		{
			if (out) *out = i;
			return true;
		}

		std::string hex;
		fmt_class_string<u64>::format(hex, i);
		if (var == hex)
		{
			break;
		}

		max = i;
	}

	u64 result;
	const char* start = value.data();
	const char* end = start + value.size();
	int base = 10;

	if (value.size() >= 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X'))
	{
		// Limited hex support
		base = 16;
		start += 2;
	}

	const auto ret = std::from_chars(start, end, result, base);

	if (ret.ec != std::errc() || ret.ptr != end)
	{
		if (out) cfg_log.error("cfg::try_to_enum_value('%s', '%s'): invalid enum or integer", value, name);
		return false;
	}

	if (result > max)
	{
		if (out) cfg_log.error("cfg::try_to_enum_value('%s', '%s'): out of bounds(val=%u, min=0, max=%u)", value, name, result, max);
		return false;
	}

	if (out) *out = result;
	return true;
}

std::string cfg::uint128::to_string(u128 value) noexcept
{
	std::string result = "0x";
	result.resize(result.size() + 32);

	for (u32 i = 0; i < 32; i++)
	{
		result[result.size() - 1 - i] = "0123456789ABCDEF"[static_cast<u64>(value >> (i * 4)) % 16];
	}

	return result;
}

std::vector<std::string> cfg::try_to_enum_list(decltype(&fmt_class_string<int>::format) func)
{
	std::vector<std::string> result;

	for (u64 i = 0;; i++)
	{
		std::string var;
		func(var, i);

		std::string hex;
		fmt_class_string<u64>::format(hex, i);
		if (var == hex)
		{
			break;
		}

		result.emplace_back(std::move(var));
	}

	return result;
}

size_t cfg::try_to_enum_size(decltype(&fmt_class_string<int>::format) func)
{
	size_t result = 0;
	for (u64 i = 0;; i++)
	{
		std::string var;
		func(var, i);

		std::string hex;
		fmt_class_string<u64>::format(hex, i);
		if (var == hex)
		{
			break;
		}

		result++;
	}

	return result;
}

void cfg::encode(yaml_writer& out, const cfg::_base& rhs)
{
	switch (rhs.get_type())
	{
	case type::node:
	{
		out.begin_map();
		for (const auto& node : static_cast<const node&>(rhs).get_nodes())
		{
			out.key(node->get_name());
			encode(out, *node);
		}
		out.end_map();
		return;
	}
	case type::set:
	{
		out.begin_seq();
		for (const auto& str : static_cast<const set_entry&>(rhs).get_set())
		{
			out.value(str);
		}
		out.end_seq();
		return;
	}
	case type::map:
	{
		out.begin_map();
		for (const auto& np : static_cast<const map_entry&>(rhs).get_map())
		{
			out.key(np.first);
			out.value(fmt::format("%s", np.second));
		}
		out.end_map();
		return;
	}
	case type::node_map:
	{
		out.begin_map();
		for (const auto& np : static_cast<const node_map_entry&>(rhs).get_map())
		{
			out.key(np.first);
			out.value(fmt::format("%s", np.second));
		}
		out.end_map();
		return;
	}
	case type::log:
	{
		out.begin_map();
		for (const auto& np : static_cast<const log_entry&>(rhs).get_map())
		{
			if (np.second == logs::level::_default) continue;
			out.key(np.first);
			out.value(fmt::format("%s", np.second));
		}
		out.end_map();
		return;
	}
	case type::device:
	{
		out.begin_map();
		for (const auto& [key, info] : static_cast<const device_entry&>(rhs).get_map())
		{
			out.key(key);
			out.begin_map();
			out.key("Path").value(info.path);
			out.key("Serial").value(info.serial);
			out.key("VID").value(info.vid);
			out.key("PID").value(info.pid);
			out.end_map();
		}
		out.end_map();
		return;
	}
	default:
	{
		out.value(rhs.to_string());
		return;
	}
	}
}

bool cfg::decode(const yaml_node& data, cfg::_base& rhs, bool dynamic, bool strict)
{
	if (dynamic && !rhs.get_is_dynamic())
	{
		return true;
	}

	switch (rhs.get_type())
	{
	case type::node:
	{
		if (!data.is_map())
		{
			cfg_log.error("node node is not a map");
			return false;
		}

		bool success = true;

		for (const yaml_node pair : data)
		{
			const std::string_view key = pair.key();

			// Find the key among existing nodes
			const auto& nodes = static_cast<node&>(rhs).get_nodes();
			const auto it = std::find_if(nodes.cbegin(), nodes.cend(), [&key](const auto& node) { return ensure(node)->get_name() == key; });

			if (it == nodes.cend())
			{
				if (strict)
				{
					cfg_log.error("Unknown key found: '%s'", key);
					success = false;
				}
				continue;
			}

			if (!decode(pair, *ensure(*it), dynamic, strict) && strict)
			{
				success = false;
			}
		}

		return success;
	}
	case type::set:
	{
		std::vector<std::string> values;

		// A sequence of scalars, as a list
		bool is_list = data.is_seq();
		for (const yaml_node item : data)
		{
			if (!item.is_scalar())
			{
				is_list = false;
				break;
			}
			values.emplace_back(item.scalar());
		}

		if (is_list)
		{
			if (!rhs.from_list(std::move(values)) && strict)
			{
				return false;
			}
		}

		break;
	}
	case type::map:
	case type::node_map:
	{
		if (!data.is_map())
		{
			cfg_log.error("map node is not a map");
			return false;
		}

		map_of_type<std::string> values;

		for (const yaml_node pair : data)
		{
			if (!pair.is_scalar()) continue;

			values.emplace(pair.key(), pair.scalar());
		}

		static_cast<map_entry&>(rhs).set_map(std::move(values));
		break;
	}
	case type::log:
	{
		if (!data.is_map())
		{
			cfg_log.error("log node is not a map");
			return false;
		}

		map_of_type<logs::level> values;

		for (const yaml_node pair : data)
		{
			if (!pair.is_scalar())
			{
				if (strict)
				{
					cfg_log.error("Value in map is not a scalar. key='%s'", pair.key());
					return false;
				}
				continue;
			}

			u64 value;
			if (!cfg::try_to_enum_value(&value, &fmt_class_string<logs::level>::format, pair.scalar(), pair.key()) && strict)
			{
				return false;
			}

			values.emplace(pair.key(), static_cast<logs::level>(static_cast<int>(value)));
		}

		static_cast<log_entry&>(rhs).set_map(std::move(values));
		break;
	}
	case type::device:
	{
		if (!data.is_map())
		{
			cfg_log.error("device node is not a map");
			return false;
		}

		map_of_type<device_info> values;

		for (const yaml_node pair : data)
		{
			if (!pair.is_map())
			{
				if (strict)
				{
					cfg_log.error("Value in device map is not a map. key='%s'", pair.key());
					return false;
				}
				continue;
			}

			device_info info{};

			for (const yaml_node key_value : pair)
			{
				if (!key_value.is_scalar())
				{
					if (strict)
					{
						cfg_log.error("Value in device map is not a scalar. key='%s'", key_value.key());
						return false;
					}
					continue;
				}

				const std::string_view key = key_value.key();

				if (key == "Path")
					info.path = key_value.scalar();

				if (key == "Serial")
					info.serial = key_value.scalar();

				if (key == "VID")
					info.vid = key_value.scalar();

				if (key == "PID")
					info.pid = key_value.scalar();
			}

			values.emplace(pair.key(), std::move(info));
		}

		static_cast<device_entry&>(rhs).set_map(std::move(values));
		break;
	}
	default:
	{
		if (data.is_scalar())
		{
			if (!rhs.from_string(data.scalar(), dynamic) && strict)
			{
				return false;
			}
		}

		break; // ???
	}
	}

	return true;
}

std::string cfg::node::to_string() const
{
	yaml_writer out;
	cfg::encode(out, *this);

	return std::string(out.output());
}

bool cfg::node::from_string(std::string_view value, bool dynamic)
{
	// Read where it lies: the tree is gone before this returns
	const yaml_doc doc(value);

	if (doc.error().empty())
	{
		return cfg::decode(doc.root(), *this, dynamic, false);
	}

	cfg_log.error("Failed to load node: %s", doc.error());
	return false;
}

void cfg::node::from_default()
{
	for (auto& node : m_nodes)
	{
		node->from_default();
	}
}

void cfg::node::restore_defaults()
{
	for (auto& node : m_nodes)
	{
		node->restore_defaults();
	}
}

bool cfg::node::validate(std::string_view value)
{
	const yaml_doc doc(value);

	if (doc.error().empty())
	{
		return cfg::decode(doc.root(), *this, false, true);
	}

	cfg_log.error("Failed to load node: %s", doc.error());
	return false;
}

bool cfg::_bool::from_string(std::string_view value, bool /*dynamic*/)
{
	if (value.size() != 4 && value.size() != 5)
	{
		return false;
	}

	char copy[5];
	std::transform(value.begin(), value.end(), std::begin(copy), utils::tolower<char>);

	if (value.size() == 5 && std::string_view{copy, 5} == "false")
		m_value = false;
	else if (value.size() == 4 && std::string_view{copy, 4} == "true")
		m_value = true;
	else
		return false;

	return true;
}

std::string cfg::map_entry::get_value(std::string_view key)
{
	if (auto it = m_map.find(key); it != m_map.end())
	{
		return it->second;
	}

	return {};
}

void cfg::map_entry::set_value(std::string key, std::string value)
{
	m_map[std::move(key)] = std::move(value);
}

void cfg::map_entry::set_map(map_of_type<std::string>&& map)
{
	m_map = std::move(map);
}

void cfg::map_entry::erase(std::string_view key)
{
	if (auto it = m_map.find(key); it != m_map.end())
	{
		m_map.erase(it);
	}
}

void cfg::map_entry::from_default()
{
	set_map({});
}

void cfg::log_entry::set_map(map_of_type<logs::level>&& map)
{
	m_map = std::move(map);
}

void cfg::log_entry::from_default()
{
	set_map({});
}

std::pair<u16, u16> cfg::device_info::get_usb_ids() const
{
	auto string_to_hex = [](std::string_view str) -> u16
	{
		u16 value = 0x0000;
		if (!str.empty() && std::from_chars(str.data(), str.data() + str.size(), value, 16).ec != std::errc{})
			cfg_log.error("Failed to parse hex from string \"%s\"", str);
		return value;
	};
	return {string_to_hex(vid), string_to_hex(pid)};
}

void cfg::device_entry::set_map(map_of_type<device_info>&& map)
{
	m_map = std::move(map);
}

void cfg::device_entry::from_default()
{
	m_map = m_default;
}
