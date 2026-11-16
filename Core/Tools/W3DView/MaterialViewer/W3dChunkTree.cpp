/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../StdAfx.h"
#include "W3dChunkTree.h"

#include <cstdio>
#include <cstring>

namespace W3dMaterialViewer
{

namespace
{

const uint32_t CHUNK_HEADER_SIZE = 8;
const uint32_t SUB_CHUNK_FLAG = 0x80000000u;

// Parses [begin, end) as a sequence of chunks. Returns false when the headers
// do not tile the range exactly; the caller then keeps the range as raw bytes.
bool Parse_Range(const uint8_t *bytes, size_t begin, size_t end, std::vector<ChunkNode> &out)
{
	size_t offset = begin;

	while (offset < end) {
		if (offset + CHUNK_HEADER_SIZE > end) {
			return false;
		}

		uint32_t id;
		uint32_t raw_size;
		memcpy(&id, bytes + offset, sizeof(id));
		memcpy(&raw_size, bytes + offset + sizeof(id), sizeof(raw_size));

		size_t payload_size = raw_size & ~SUB_CHUNK_FLAG;
		size_t payload_begin = offset + CHUNK_HEADER_SIZE;
		if (payload_begin + payload_size > end) {
			return false;
		}

		ChunkNode node;
		node.id = id;
		node.hasSubChunks = (raw_size & SUB_CHUNK_FLAG) != 0;

		bool parsed_children = false;
		if (node.hasSubChunks) {
			parsed_children = Parse_Range(bytes, payload_begin, payload_begin + payload_size, node.children);
			if (!parsed_children) {
				node.children.clear();
			}
		}
		if (!parsed_children) {
			node.data.assign(bytes + payload_begin, bytes + payload_begin + payload_size);
		}

		out.push_back(std::move(node));
		offset = payload_begin + payload_size;
	}

	return true;
}

size_t Payload_Size(const ChunkNode &node)
{
	if (node.children.empty()) {
		return node.data.size();
	}
	size_t size = 0;
	for (const ChunkNode &child : node.children) {
		size += CHUNK_HEADER_SIZE + Payload_Size(child);
	}
	return size;
}

void Write_Node(const ChunkNode &node, std::vector<uint8_t> &out)
{
	uint32_t payload_size = (uint32_t)Payload_Size(node);
	uint32_t raw_size = payload_size | (node.hasSubChunks ? SUB_CHUNK_FLAG : 0);

	size_t header_pos = out.size();
	out.resize(header_pos + CHUNK_HEADER_SIZE);
	memcpy(&out[header_pos], &node.id, sizeof(node.id));
	memcpy(&out[header_pos + sizeof(node.id)], &raw_size, sizeof(raw_size));

	if (node.children.empty()) {
		out.insert(out.end(), node.data.begin(), node.data.end());
	} else {
		for (const ChunkNode &child : node.children) {
			Write_Node(child, out);
		}
	}
}

} // namespace

bool LoadChunkTree(const std::vector<uint8_t> &bytes, std::vector<ChunkNode> &roots)
{
	roots.clear();
	if (bytes.empty()) {
		return false;
	}
	return Parse_Range(bytes.data(), 0, bytes.size(), roots);
}

void WriteChunkTree(const std::vector<ChunkNode> &roots, std::vector<uint8_t> &out)
{
	out.clear();
	for (const ChunkNode &node : roots) {
		Write_Node(node, out);
	}
}

bool ReadFileBytes(const char *path, std::vector<uint8_t> &bytes)
{
	bytes.clear();

	FILE *file = fopen(path, "rb");
	if (file == nullptr) {
		return false;
	}

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	bool ok = (size >= 0);
	if (ok && size > 0) {
		bytes.resize((size_t)size);
		ok = (fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
	}

	fclose(file);
	if (!ok) {
		bytes.clear();
	}
	return ok;
}

bool ChunkTreeRoundTripsIdentically(const char *path, std::string &errorMessage)
{
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(path, bytes) || bytes.empty()) {
		errorMessage = "The file could not be read.";
		return false;
	}

	std::vector<ChunkNode> roots;
	if (!LoadChunkTree(bytes, roots)) {
		errorMessage = "The file's chunk structure is malformed.";
		return false;
	}

	std::vector<uint8_t> rewritten;
	WriteChunkTree(roots, rewritten);

	if (rewritten.size() != bytes.size() ||
			memcmp(rewritten.data(), bytes.data(), bytes.size()) != 0) {
		errorMessage = "The file uses a chunk layout this editor cannot reproduce exactly.";
		return false;
	}

	return true;
}

ChunkNode *FindChild(ChunkNode &parent, uint32_t id)
{
	for (ChunkNode &child : parent.children) {
		if (child.id == id) {
			return &child;
		}
	}
	return nullptr;
}

} // namespace W3dMaterialViewer
