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
#include "W3dMaterialWriter.h"

#include "W3dChunkTree.h"
#include "W3dMaterialParser.h"

#include "WW3D2/w3d_file.h"

#include <cstdio>
#include <cstring>

namespace W3dMaterialViewer
{

// File > Backup File Before Change. Default on (a one-time <file>.bak is made
// before the first save overwrites the original).
static bool g_BackupBeforeSave = true;

void Set_Backup_Before_Save(bool enabled) { g_BackupBeforeSave = enabled; }
bool Get_Backup_Before_Save() { return g_BackupBeforeSave; }

namespace
{

// Reads the payload into the struct, lets `apply` edit it, writes it back.
// Skips (returns false) when the payload is shorter than the struct; extra
// payload bytes beyond the struct are preserved.
template<typename T, typename ApplyFn>
bool Patch_Struct(ChunkNode &node, ApplyFn apply)
{
	if (node.data.size() < sizeof(T) || !node.children.empty()) {
		return false;
	}
	T value;
	memcpy(&value, node.data.data(), sizeof(T));
	apply(value);
	memcpy(node.data.data(), &value, sizeof(T));
	return true;
}

void Set_String_Payload(ChunkNode &node, const std::string &text)
{
	node.children.clear();
	node.data.assign(text.begin(), text.end());
	node.data.push_back('\0');
}

void Apply_Texture_Info(W3dTextureInfoStruct &info, const TextureData &texture)
{
	info.Attributes = texture.attributes;
	info.AnimType = texture.animType;
	info.FrameCount = texture.frameCount;
	info.FrameRate = texture.frameRate;
}

void Apply_Vertex_Material(ChunkNode &node, const VertexMaterialData &material)
{
	ChunkNode *info = FindChild(node, W3D_CHUNK_VERTEX_MATERIAL_INFO);
	if (info != nullptr) {
		Patch_Struct<W3dVertexMaterialStruct>(*info, [&](W3dVertexMaterialStruct &value) {
			value.Attributes = material.attributes;
			value.Ambient.Set(material.ambient[0], material.ambient[1], material.ambient[2]);
			value.Diffuse.Set(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
			value.Specular.Set(material.specular[0], material.specular[1], material.specular[2]);
			value.Emissive.Set(material.emissive[0], material.emissive[1], material.emissive[2]);
			value.Shininess = material.shininess;
			value.Opacity = material.opacity;
			value.Translucency = material.translucency;
		});
	}

	// Mapper args: replace in place when the chunk exists; append the chunk
	// when args were added to a material that had none.
	const struct { uint32_t id; const std::string *text; } args[] = {
		{ W3D_CHUNK_VERTEX_MAPPER_ARGS0, &material.mapperArgs0 },
		{ W3D_CHUNK_VERTEX_MAPPER_ARGS1, &material.mapperArgs1 },
	};
	for (const auto &arg : args) {
		ChunkNode *chunk = FindChild(node, arg.id);
		if (chunk != nullptr) {
			Set_String_Payload(*chunk, *arg.text);
		} else if (!arg.text->empty()) {
			ChunkNode added;
			added.id = arg.id;
			node.children.push_back(added);
			Set_String_Payload(node.children.back(), *arg.text);
		}
	}
}

void Apply_Texture(ChunkNode &node, const TextureData &texture)
{
	ChunkNode *name = FindChild(node, W3D_CHUNK_TEXTURE_NAME);
	if (name != nullptr && !texture.name.empty()) {
		Set_String_Payload(*name, texture.name);
	}

	ChunkNode *info = FindChild(node, W3D_CHUNK_TEXTURE_INFO);
	if (info != nullptr) {
		Patch_Struct<W3dTextureInfoStruct>(*info, [&](W3dTextureInfoStruct &value) {
			Apply_Texture_Info(value, texture);
		});
	} else if (texture.hasInfo) {
		// The chunk is optional; the editor materializes it when texture
		// settings are first changed.
		W3dTextureInfoStruct value;
		memset(&value, 0, sizeof(value));
		Apply_Texture_Info(value, texture);
		ChunkNode added;
		added.id = W3D_CHUNK_TEXTURE_INFO;
		added.data.resize(sizeof(value));
		memcpy(added.data.data(), &value, sizeof(value));
		node.children.push_back(std::move(added));
	}
}

// Applies indexed sub-chunks of `parent` (e.g. VERTEX_MATERIAL entries inside
// VERTEX_MATERIALS) against the matching document vector.
template<typename T, typename ApplyFn>
void Apply_Wrapped_List(ChunkNode &parent, uint32_t entry_id, const std::vector<T> &entries, ApplyFn apply)
{
	size_t index = 0;
	for (ChunkNode &child : parent.children) {
		if (child.id == entry_id && index < entries.size()) {
			apply(child, entries[index]);
			index++;
		}
	}
}

bool Apply_Mesh(ChunkNode &mesh_node, const MeshMaterialData &mesh, std::string &errorMessage)
{
	ChunkNode *header = FindChild(mesh_node, W3D_CHUNK_MESH_HEADER3);
	if (header == nullptr || header->data.size() < sizeof(W3dMeshHeader3Struct)) {
		errorMessage = "Mesh '" + mesh.meshName + "' has no readable header.";
		return false;
	}

	// The document and the file list meshes in the same order; the name check
	// guards against ever writing into the wrong mesh.
	{
		W3dMeshHeader3Struct value;
		memcpy(&value, header->data.data(), sizeof(value));
		value.MeshName[W3D_NAME_LEN - 1] = '\0';
		value.ContainerName[W3D_NAME_LEN - 1] = '\0';
		std::string name = value.ContainerName;
		if (!name.empty()) {
			name += ".";
		}
		name += value.MeshName;
		if (_stricmp(name.c_str(), mesh.meshName.c_str()) != 0) {
			errorMessage = "Mesh order mismatch: expected '" + mesh.meshName + "', found '" + name + "'.";
			return false;
		}
	}

	// Prelit meshes keep several material variants in wrapper chunks; editing
	// only one variant would desync them, so they are read-only in v1.
	if (mesh.prelit) {
		return true;
	}

	Patch_Struct<W3dMeshHeader3Struct>(*header, [&](W3dMeshHeader3Struct &value) {
		value.SortLevel = mesh.sortLevel;
	});

	// Surface type is stored per triangle; the editor exposes one value for
	// the whole mesh (matching the Max exporter), so stamp every triangle.
	ChunkNode *triangles = FindChild(mesh_node, W3D_CHUNK_TRIANGLES);
	if (triangles != nullptr && triangles->children.empty()) {
		size_t count = triangles->data.size() / sizeof(W3dTriStruct);
		for (size_t i = 0; i < count; i++) {
			W3dTriStruct tri;
			memcpy(&tri, triangles->data.data() + i * sizeof(tri), sizeof(tri));
			tri.Attributes = mesh.surfaceType;
			memcpy(triangles->data.data() + i * sizeof(tri), &tri, sizeof(tri));
		}
	}

	ChunkNode *shaders = FindChild(mesh_node, W3D_CHUNK_SHADERS);
	if (shaders != nullptr && shaders->children.empty()) {
		size_t count = shaders->data.size() / sizeof(W3dShaderStruct);
		for (size_t i = 0; i < count && i < mesh.shaders.size(); i++) {
			const ShaderData &shader = mesh.shaders[i];
			W3dShaderStruct value;
			memcpy(&value, shaders->data.data() + i * sizeof(value), sizeof(value));
			// Obsolete members (ColorMask, FogFunc, ShaderPreset, pad) are
			// deliberately left as found.
			value.DepthCompare = shader.depthCompare;
			value.DepthMask = shader.depthMask;
			value.DestBlend = shader.destBlend;
			value.PriGradient = shader.priGradient;
			value.SecGradient = shader.secGradient;
			value.SrcBlend = shader.srcBlend;
			value.Texturing = shader.texturing;
			value.DetailColorFunc = shader.detailColorFunc;
			value.DetailAlphaFunc = shader.detailAlphaFunc;
			value.AlphaTest = shader.alphaTest;
			value.PostDetailColorFunc = shader.postDetailColorFunc;
			value.PostDetailAlphaFunc = shader.postDetailAlphaFunc;
			memcpy(shaders->data.data() + i * sizeof(value), &value, sizeof(value));
		}
	}

	ChunkNode *materials = FindChild(mesh_node, W3D_CHUNK_VERTEX_MATERIALS);
	if (materials != nullptr) {
		Apply_Wrapped_List(*materials, W3D_CHUNK_VERTEX_MATERIAL, mesh.vertexMaterials,
			[](ChunkNode &child, const VertexMaterialData &material) {
				Apply_Vertex_Material(child, material);
			});
	}

	ChunkNode *textures = FindChild(mesh_node, W3D_CHUNK_TEXTURES);
	if (textures != nullptr) {
		Apply_Wrapped_List(*textures, W3D_CHUNK_TEXTURE, mesh.textures,
			[](ChunkNode &child, const TextureData &texture) {
				Apply_Texture(child, texture);
			});
	}

	return true;
}

bool Apply_Document(std::vector<ChunkNode> &roots, const MaterialDocument &document, std::string &errorMessage)
{
	size_t mesh_index = 0;
	for (ChunkNode &node : roots) {
		if (node.id != W3D_CHUNK_MESH) {
			continue;
		}
		if (mesh_index >= document.meshes.size()) {
			errorMessage = "The file contains more meshes than the loaded document.";
			return false;
		}
		if (!Apply_Mesh(node, document.meshes[mesh_index], errorMessage)) {
			return false;
		}
		mesh_index++;
	}

	if (mesh_index != document.meshes.size()) {
		errorMessage = "The file contains fewer meshes than the loaded document.";
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////
//	Post-write validation: the saved bytes must parse back to the document
//////////////////////////////////////////////////////////////////////////////

bool Materials_Match(const VertexMaterialData &a, const VertexMaterialData &b)
{
	return a.attributes == b.attributes
		&& memcmp(a.ambient, b.ambient, sizeof(a.ambient)) == 0
		&& memcmp(a.diffuse, b.diffuse, sizeof(a.diffuse)) == 0
		&& memcmp(a.specular, b.specular, sizeof(a.specular)) == 0
		&& memcmp(a.emissive, b.emissive, sizeof(a.emissive)) == 0
		&& a.shininess == b.shininess
		&& a.opacity == b.opacity
		&& a.translucency == b.translucency
		&& a.mapperArgs0 == b.mapperArgs0
		&& a.mapperArgs1 == b.mapperArgs1;
}

bool Shaders_Match(const ShaderData &a, const ShaderData &b)
{
	return a.depthCompare == b.depthCompare
		&& a.depthMask == b.depthMask
		&& a.destBlend == b.destBlend
		&& a.priGradient == b.priGradient
		&& a.secGradient == b.secGradient
		&& a.srcBlend == b.srcBlend
		&& a.texturing == b.texturing
		&& a.detailColorFunc == b.detailColorFunc
		&& a.detailAlphaFunc == b.detailAlphaFunc
		&& a.alphaTest == b.alphaTest
		&& a.postDetailColorFunc == b.postDetailColorFunc
		&& a.postDetailAlphaFunc == b.postDetailAlphaFunc;
}

bool Textures_Match(const TextureData &a, const TextureData &b)
{
	if (a.name != b.name || a.hasInfo != b.hasInfo) {
		return false;
	}
	if (a.hasInfo &&
			(a.attributes != b.attributes || a.animType != b.animType ||
			 a.frameCount != b.frameCount || a.frameRate != b.frameRate)) {
		return false;
	}
	return true;
}

bool Documents_Match(const MaterialDocument &edited, const MaterialDocument &reparsed, std::string *mismatch = nullptr)
{
	auto note = [&](const std::string &why) { if (mismatch != nullptr) *mismatch = why; };

	if (edited.meshes.size() != reparsed.meshes.size()) {
		note("mesh count " + std::to_string(edited.meshes.size()) + " vs reparsed "
			+ std::to_string(reparsed.meshes.size()));
		return false;
	}

	for (size_t m = 0; m < edited.meshes.size(); m++) {
		const MeshMaterialData &a = edited.meshes[m];
		const MeshMaterialData &b = reparsed.meshes[m];

		if (a.prelit) {
			// Untouched by the writer; nothing to verify.
			continue;
		}

		if (_stricmp(a.meshName.c_str(), b.meshName.c_str()) != 0) {
			note("mesh[" + std::to_string(m) + "] name '" + a.meshName + "' vs '" + b.meshName + "'");
			return false;
		}
		if (a.surfaceType != b.surfaceType || a.sortLevel != b.sortLevel
				|| a.vertexMaterials.size() != b.vertexMaterials.size()
				|| a.shaders.size() != b.shaders.size()
				|| a.textures.size() != b.textures.size()) {
			char buf[256];
			_snprintf(buf, sizeof(buf), "mesh '%s': surf %u/%u sort %d/%d vmat %zu/%zu shader %zu/%zu tex %zu/%zu",
				a.meshName.c_str(), a.surfaceType, b.surfaceType, a.sortLevel, b.sortLevel,
				a.vertexMaterials.size(), b.vertexMaterials.size(), a.shaders.size(), b.shaders.size(),
				a.textures.size(), b.textures.size());
			note(buf);
			return false;
		}
		for (size_t i = 0; i < a.vertexMaterials.size(); i++) {
			if (!Materials_Match(a.vertexMaterials[i], b.vertexMaterials[i])) {
				note("mesh '" + a.meshName + "' vertex material " + std::to_string(i) + " differs");
				return false;
			}
		}
		for (size_t i = 0; i < a.shaders.size(); i++) {
			if (!Shaders_Match(a.shaders[i], b.shaders[i])) {
				note("mesh '" + a.meshName + "' shader " + std::to_string(i) + " differs");
				return false;
			}
		}
		for (size_t i = 0; i < a.textures.size(); i++) {
			if (!Textures_Match(a.textures[i], b.textures[i])) {
				note("mesh '" + a.meshName + "' texture " + std::to_string(i)
					+ " differs (edited '" + a.textures[i].name + "' hasInfo " + (a.textures[i].hasInfo ? "1" : "0")
					+ " vs reparsed '" + b.textures[i].name + "' hasInfo " + (b.textures[i].hasInfo ? "1" : "0") + ")");
				return false;
			}
		}
	}

	return true;
}

bool Write_File_Bytes(const char *path, const std::vector<uint8_t> &bytes)
{
	FILE *file = fopen(path, "wb");
	if (file == nullptr) {
		return false;
	}
	bool ok = (fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
	if (fclose(file) != 0) {
		ok = false;
	}
	return ok;
}

} // namespace

bool SaveMaterialDocument(const MaterialDocument &document, std::string &errorMessage)
{
	const std::string &path = document.resolvedDiskPath;
	if (path.empty()) {
		errorMessage = "The file's on-disk location is unknown; it cannot be saved.";
		return false;
	}

	// Integrity gate: refuse to touch a file we cannot reproduce exactly.
	std::vector<uint8_t> original;
	if (!ReadFileBytes(path.c_str(), original) || original.empty()) {
		errorMessage = "The file could not be read.";
		return false;
	}
	std::vector<ChunkNode> roots;
	if (!LoadChunkTree(original, roots)) {
		errorMessage = "The file's chunk structure is malformed.";
		return false;
	}
	{
		std::vector<uint8_t> rewritten;
		WriteChunkTree(roots, rewritten);
		if (rewritten.size() != original.size() ||
				memcmp(rewritten.data(), original.data(), original.size()) != 0) {
			errorMessage = "The file uses a chunk layout this editor cannot reproduce exactly; saving is disabled to avoid corrupting it.";
			return false;
		}
	}

	if (!Apply_Document(roots, document, errorMessage)) {
		return false;
	}

	std::vector<uint8_t> edited;
	WriteChunkTree(roots, edited);

	// Stage to a temp file next to the original, then verify the temp parses
	// back to exactly the edited document before the original is replaced.
	std::string tmp_path = path + ".tmp";
	if (!Write_File_Bytes(tmp_path.c_str(), edited)) {
		remove(tmp_path.c_str());
		errorMessage = "The temporary file could not be written (is the folder writable?).";
		return false;
	}

	{
		MaterialDocument reparsed;
		std::string mismatch;
		if (!ParseMaterialDocument(tmp_path.c_str(), reparsed)
				|| !Documents_Match(document, reparsed, &mismatch)) {
			remove(tmp_path.c_str());
			errorMessage = "Validation failed: the written file did not parse back to the edited values";
			if (!mismatch.empty()) {
				errorMessage += " [" + mismatch + "]";
			}
			errorMessage += ". The original file was not modified.";
			return false;
		}
	}

	// One-time pristine backup; later saves keep the original .bak. Skipped when
	// File > Backup File Before Change is turned off.
	if (g_BackupBeforeSave) {
		std::string bak_path = path + ".bak";
		if (::GetFileAttributesA(bak_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			::CopyFileA(path.c_str(), bak_path.c_str(), TRUE);
		}
	}

	if (!::ReplaceFileA(path.c_str(), tmp_path.c_str(), nullptr, 0, nullptr, nullptr)) {
		if (!::MoveFileExA(tmp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			remove(tmp_path.c_str());
			errorMessage = "The edited file could not replace the original (is it read-only or in use?).";
			return false;
		}
	}

	return true;
}

} // namespace W3dMaterialViewer
