#pragma once

#include <formats/rxml.h>

#include <memory>
#include <string>
#include <string_view>

namespace fs
{
	class file;
}

// A node of a parsed document: an element, or the document itself, whose
// only child is the root element (as pugixml's document node was)
struct rXmlNode
{
	rXmlNode();
	explicit rXmlNode(rxml_node_t* element);
	std::shared_ptr<rXmlNode> GetChild(std::string_view name);
	std::shared_ptr<rXmlNode> GetChildren();
	std::shared_ptr<rXmlNode> GetNext();
	std::string GetName();
	std::string GetAttribute(std::string_view name);
	std::string GetNodeContent();

	rxml_node_t* handle{}; // The element; null for the document
	rxml_node_t* root{};   // For the document: its root element
};

struct rXmlDocument
{
	rXmlDocument();
	rXmlDocument(const rXmlDocument& other) = delete;
	rXmlDocument &operator=(const rXmlDocument& other) = delete;
	virtual ~rXmlDocument();

	bool Read(std::string_view data);

	// The whole file, read straight into the buffer the document keeps
	bool Read(const fs::file& file);

	virtual std::shared_ptr<rXmlNode> GetRoot();

	rxml_document_t* handle{};
};
