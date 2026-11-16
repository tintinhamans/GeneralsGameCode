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

// TheSuperHackers @feature W3DView material viewer edit mode.
// Generic .w3d chunk tree for structure-preserving rewrites. Chunks are only
// descended into when their header's sub-chunk flag (size MSB) says they
// contain chunks — everything else is kept as an opaque byte payload, so a
// load/write round trip of an untouched file reproduces it byte-identically.
// That identity is the editor's anti-corruption guarantee: files that do not
// round-trip are refused for editing.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace W3dMaterialViewer
{

struct ChunkNode
{
	uint32_t				id = 0;
	// Original header MSB ("contains sub-chunks"), written back verbatim.
	bool					hasSubChunks = false;
	// Raw payload; used when `children` is empty (data chunks, and containers
	// whose contents failed to parse and are copied through untouched).
	std::vector<uint8_t>	data;
	std::vector<ChunkNode>	children;
};

// Parses a whole .w3d byte image into a chunk tree. Returns false when the
// top-level chunk headers do not tile the file exactly (malformed file).
// Malformed *nested* containers do not fail the load; they degrade to opaque
// payloads and are copied through verbatim.
bool LoadChunkTree(const std::vector<uint8_t> &bytes, std::vector<ChunkNode> &roots);

// Serializes the tree; chunk sizes are recomputed bottom-up so payload edits
// of any length propagate correctly to all ancestor headers.
void WriteChunkTree(const std::vector<ChunkNode> &roots, std::vector<uint8_t> &out);

bool ReadFileBytes(const char *path, std::vector<uint8_t> &bytes);

// The edit-mode gate: true when load + write reproduces the file bytes
// exactly. `errorMessage` receives a reason on failure.
bool ChunkTreeRoundTripsIdentically(const char *path, std::string &errorMessage);

// First direct child with the given chunk id; null when absent.
ChunkNode *FindChild(ChunkNode &parent, uint32_t id);

} // namespace W3dMaterialViewer
