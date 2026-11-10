#pragma once

// YAML through libretro-common: ryaml reads, ryaml_writer writes. These are
// the handles RPCS3's code walks and writes documents with; the parsing and
// the layout are libretro-common's.

#include <cstddef>
#include <string>
#include <string_view>

#include <formats/ryaml.h>
#include <formats/ryaml_writer.h>

// A node of a document read by yaml_doc: the tree and the node's handle.
// An absent node (a key not found, an index past the end) is false and
// answers every question as an empty one. An alias ("*name") reads as the
// node it stands for.
class yaml_node
{
	const ryaml_t* m_y = nullptr;
	int m_n = RYAML_NONE;

public:
	yaml_node() = default;
	yaml_node(const ryaml_t* y, int n) : m_y(y), m_n(n) {}

	explicit operator bool() const { return m_y && m_n >= 0; }

	bool is_map() const { return ryaml_is_map(m_y, m_n); }
	bool is_seq() const { return ryaml_is_seq(m_y, m_n); }
	// A scalar with a value; "key:" with nothing after it is null
	bool is_scalar() const { return ryaml_type(m_y, m_n) == RYAML_TYPE_SCALAR && ryaml_has_val(m_y, m_n); }
	bool is_null() const { return !is_map() && !is_seq() && !is_scalar(); }

	// "Map", "Sequence", "Scalar" or "Null", for messages
	const char* type_name() const;

	// The value's bytes, in the document (or the tree's arena); empty for
	// anything but a scalar
	std::string_view scalar() const
	{
		std::size_t len = 0;
		const char* v = ryaml_val(m_y, m_n, &len);
		return v ? std::string_view(v, len) : std::string_view();
	}

	// This node's key in its mapping
	std::string_view key() const
	{
		std::size_t len = 0;
		const char* k = ryaml_key(m_y, m_n, &len);
		return k ? std::string_view(k, len) : std::string_view();
	}

	yaml_node operator[](std::string_view key) const { return {m_y, ryaml_find_child_len(m_y, m_n, key.data(), key.size())}; }
	yaml_node operator[](const char* key) const { return (*this)[std::string_view(key)]; }
	yaml_node operator[](const std::string& key) const { return (*this)[std::string_view(key)]; }
	yaml_node operator[](std::size_t index) const;
	yaml_node operator[](int index) const { return (*this)[static_cast<std::size_t>(index)]; }
	std::size_t size() const { return ryaml_num_children(m_y, m_n); }

	// "line L, column C", or "unknown"
	std::string location() const;

	class iterator
	{
		const ryaml_t* m_y;
		int m_n;

	public:
		iterator(const ryaml_t* y, int n) : m_y(y), m_n(n) {}
		yaml_node operator*() const { return {m_y, m_n}; }
		iterator& operator++() { m_n = ryaml_next_sibling(m_y, m_n); return *this; }
		bool operator!=(const iterator& rhs) const { return m_n != rhs.m_n; }
	};

	// The children: entries of a mapping (each with its key()), items of a
	// sequence
	iterator begin() const { return {m_y, ryaml_first_child(m_y, m_n)}; }
	iterator end() const { return {m_y, RYAML_NONE}; }
};

// A document, read at once. The text is kept (moved in) or borrowed (a view
// the caller keeps alive); scalars point into it, nothing is copied.
class yaml_doc
{
	std::string m_text;
	ryaml_t* m_y = nullptr;
	std::string m_error;

	void parse(std::string_view text);

public:
	explicit yaml_doc(std::string&& text);
	explicit yaml_doc(std::string_view text);
	~yaml_doc();

	yaml_doc(const yaml_doc&) = delete;
	yaml_doc& operator=(const yaml_doc&) = delete;

	// Empty when the document was read
	const std::string& error() const { return m_error; }
	yaml_node root() const { return {m_y, ryaml_root(m_y)}; }
};

// A node's value as T: integers (decimal, or 0x hex, with a sign where T
// has one, in T's range), floating point, or the string itself. Fills
// error_message and returns T{} when the node is not a scalar that reads
// as T.
template <typename T>
T get_yaml_node_value(const yaml_node& node, std::string& error_message);

// The same, or @fallback when the node is absent or does not read as T
template <typename T>
T get_yaml_node_value_or(const yaml_node& node, T fallback)
{
	std::string error;
	const T value = get_yaml_node_value<T>(node, error);
	return error.empty() ? value : fallback;
}

// A document being written, into memory
class yaml_writer
{
	ryaml_writer_t* m_w;

public:
	yaml_writer() : m_w(ryaml_writer_new(nullptr, nullptr)) {}
	~yaml_writer() { ryaml_writer_free(m_w); }

	yaml_writer(const yaml_writer&) = delete;
	yaml_writer& operator=(const yaml_writer&) = delete;

	yaml_writer& begin_map() { ryaml_writer_begin_map(m_w); return *this; }
	yaml_writer& end_map() { ryaml_writer_end_map(m_w); return *this; }
	yaml_writer& begin_seq(bool flow = false) { ryaml_writer_begin_seq(m_w, flow); return *this; }
	yaml_writer& end_seq() { ryaml_writer_end_seq(m_w); return *this; }
	yaml_writer& key(std::string_view k) { ryaml_writer_key(m_w, k.data(), k.size()); return *this; }
	yaml_writer& value(std::string_view v) { ryaml_writer_scalar(m_w, v.data(), v.size()); return *this; }
	yaml_writer& value_bool(bool v) { return value(v ? "true" : "false"); }
	// The shortest digits that read back as the same double
	yaml_writer& value_f64(double v);
	yaml_writer& blank_line() { ryaml_writer_blank_line(m_w); return *this; }

	// The document, in the writer's buffer: valid until the next call. Empty
	// if the calls did not make one.
	std::string_view output() const
	{
		std::size_t len = 0;
		const char* out = ryaml_writer_memory(m_w, &len);
		return out ? std::string_view(out, len) : std::string_view();
	}
};
