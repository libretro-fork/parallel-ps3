#include "stdafx.h"
#include "Utilities/rXml.h"
#include "Utilities/File.h"

#include <cstdlib>
#include <cstring>

rXmlNode::rXmlNode()
{
}

rXmlNode::rXmlNode(rxml_node_t* element)
{
	handle = element;
}

static bool rxml_name_is(const rxml_node_t* node, std::string_view name)
{
	return node->name && std::string_view(node->name) == name;
}

std::shared_ptr<rXmlNode> rXmlNode::GetChild(std::string_view name)
{
	if (!handle)
	{
		if (root && rxml_name_is(root, name))
		{
			return std::make_shared<rXmlNode>(root);
		}

		return nullptr;
	}

	for (rxml_node_t* child = handle->children; child; child = child->next)
	{
		if (rxml_name_is(child, name))
		{
			return std::make_shared<rXmlNode>(child);
		}
	}

	return nullptr;
}

std::shared_ptr<rXmlNode> rXmlNode::GetChildren()
{
	if (rxml_node_t* child = handle ? handle->children : root)
	{
		return std::make_shared<rXmlNode>(child);
	}

	return nullptr;
}

std::shared_ptr<rXmlNode> rXmlNode::GetNext()
{
	if (handle && handle->next)
	{
		return std::make_shared<rXmlNode>(handle->next);
	}

	return nullptr;
}

std::string rXmlNode::GetName()
{
	if (handle && handle->name)
	{
		return handle->name;
	}

	return {};
}

std::string rXmlNode::GetAttribute(std::string_view name)
{
	if (handle)
	{
		for (const rxml_attrib_node* attr = handle->attrib; attr; attr = attr->next)
		{
			if (attr->attrib && std::string_view(attr->attrib) == name)
			{
				return attr->value ? attr->value : "";
			}
		}
	}

	return {};
}

std::string rXmlNode::GetNodeContent()
{
	if (handle && handle->data)
	{
		return handle->data;
	}

	return {};
}

rXmlDocument::rXmlDocument()
{
}

rXmlDocument::~rXmlDocument()
{
	rxml_free_document(handle);
}

// The document takes @buf (malloc'd, @len bytes and a terminator) whatever happens
static rxml_document_t* rxml_parse_buffer(char* buf, usz len)
{
	buf[len] = '\0';
	return rxml_load_document_owned(buf, len);
}

bool rXmlDocument::Read(std::string_view data)
{
	rxml_free_document(handle);
	handle = nullptr;

	char* const buf = static_cast<char*>(std::malloc(data.size() + 1));

	if (!buf)
	{
		return false;
	}

	std::memcpy(buf, data.data(), data.size());
	handle = rxml_parse_buffer(buf, data.size());
	return handle && rxml_root_node(handle);
}

bool rXmlDocument::Read(const fs::file& file)
{
	rxml_free_document(handle);
	handle = nullptr;

	if (!file)
	{
		return false;
	}

	const u64 size = file.size();

	if (size >= usz{umax})
	{
		return false;
	}

	char* const buf = static_cast<char*>(std::malloc(static_cast<usz>(size) + 1));

	if (!buf)
	{
		return false;
	}

	if (file.read_at(0, buf, size) != size)
	{
		std::free(buf);
		return false;
	}

	handle = rxml_parse_buffer(buf, static_cast<usz>(size));
	return handle && rxml_root_node(handle);
}

std::shared_ptr<rXmlNode> rXmlDocument::GetRoot()
{
	if (rxml_node_t* root = rxml_root_node(handle))
	{
		auto doc = std::make_shared<rXmlNode>();
		doc->root = root;
		return doc;
	}

	return nullptr;
}
