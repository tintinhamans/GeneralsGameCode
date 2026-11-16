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

// TheSuperHackers @feature W3DView material viewer.
// Plain-types mirror of the raw .w3d material chunk data. This header is shared
// between the chunk parser (WW3D side) and the Qt material panel, so it must not
// include any WW3D, MFC, or Qt headers.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace W3dMaterialViewer
{

// Mirrors W3dTextureInfoStruct plus the texture name.
struct TextureData
{
	std::string	name;
	bool		hasInfo = false;		// W3D_CHUNK_TEXTURE_INFO is optional
	uint16_t	attributes = 0;			// W3DTEXTURE_* bits (publish, clamp, hint, ...)
	uint16_t	animType = 0;			// W3DTEXTURE_ANIM_*
	uint32_t	frameCount = 1;
	float		frameRate = 15.0f;

	// Small thumbnail decoded on the W3DView side (file factory + D3DX), so the
	// Qt panel never has to understand .dds/.tga. BGRA byte order (little-endian
	// ARGB32), previewWidth * previewHeight * 4 bytes; empty if unresolved.
	std::string	resolvedPath;			// where the texture was found, for tooltips
	int			previewWidth = 0;
	int			previewHeight = 0;
	std::vector<uint8_t> previewPixels;
};

// Mirrors W3dVertexMaterialStruct plus name and mapper args strings.
struct VertexMaterialData
{
	std::string	name;
	uint32_t	attributes = 0;			// W3DVERTMAT_* bits (mapping types, specular-to-diffuse, ...)
	uint8_t		ambient[3] = { 255, 255, 255 };
	uint8_t		diffuse[3] = { 255, 255, 255 };
	uint8_t		specular[3] = { 0, 0, 0 };
	uint8_t		emissive[3] = { 0, 0, 0 };
	float		shininess = 1.0f;
	float		opacity = 1.0f;
	float		translucency = 0.0f;
	std::string	mapperArgs0;			// W3D_CHUNK_VERTEX_MAPPER_ARGS0, verbatim
	std::string	mapperArgs1;			// W3D_CHUNK_VERTEX_MAPPER_ARGS1, verbatim
};

// Mirrors W3dShaderStruct (obsolete members omitted).
struct ShaderData
{
	uint8_t		depthCompare = 3;		// W3DSHADER_DEPTHCOMPARE_PASS_LEQUAL
	uint8_t		depthMask = 1;			// W3DSHADER_DEPTHMASK_WRITE_ENABLE
	uint8_t		destBlend = 0;			// W3DSHADER_DESTBLENDFUNC_ZERO
	uint8_t		priGradient = 1;		// W3DSHADER_PRIGRADIENT_MODULATE
	uint8_t		secGradient = 0;
	uint8_t		srcBlend = 1;			// W3DSHADER_SRCBLENDFUNC_ONE
	uint8_t		texturing = 0;
	uint8_t		detailColorFunc = 0;
	uint8_t		detailAlphaFunc = 0;
	uint8_t		alphaTest = 0;
	uint8_t		postDetailColorFunc = 0;
	uint8_t		postDetailAlphaFunc = 0;
};

// One W3D_CHUNK_MATERIAL_PASS. Indices refer to the per-mesh tables below;
// -1 means not present.
struct PassData
{
	int			vertexMaterialIndex = -1;
	int			shaderIndex = -1;
	int			stageTextureIndex[2] = { -1, -1 };

	// Every distinct index of a per-vertex or per-triangle id array, in file order.
	std::vector<int>	vertexMaterialIds;
	std::vector<int>	shaderIds;
	std::vector<int>	stageTextureIds[2];

	// Index of the stage's distinct texcoord array within the mesh; -1 means none.
	int			stageUvSet[2] = { -1, -1 };

	bool		hasDCG = false;			// per-vertex diffuse colour and alpha
	bool		hasDIG = false;			// per-vertex diffuse illumination
	bool		hasSCG = false;			// per-vertex specular colour
};

// Material data of one W3D_CHUNK_MESH.
struct MeshMaterialData
{
	std::string	meshName;				// "Container.Mesh"
	uint32_t	attributes = 0;			// W3D_MESH_FLAG_* bits (geometry type, hidden, collision, ...)
	uint32_t	numTris = 0;
	uint32_t	numVertices = 0;
	std::string	userText;				// W3D_CHUNK_MESH_USER_TEXT, the Max object properties
	uint32_t	surfaceType = 13;		// SURFACE_TYPE_DEFAULT, from the first triangle
	int32_t		sortLevel = 0;
	bool		prelit = false;			// material chunks came from a W3D_CHUNK_PRELIT_* wrapper
	int			passCount = 0;
	std::vector<VertexMaterialData>	vertexMaterials;
	std::vector<ShaderData>			shaders;
	std::vector<TextureData>		textures;
	std::vector<PassData>			passes;
};

struct MaterialDocument
{
	std::string	filePath;
	// Actual on-disk location (factory opens resolve a bare filename through
	// the search paths). Empty when unknown; edit mode requires it to save.
	std::string	resolvedDiskPath;
	std::string	topLevelName;			// HLod name if present, else first mesh's name
	std::vector<MeshMaterialData>	meshes;
};

} // namespace W3dMaterialViewer
