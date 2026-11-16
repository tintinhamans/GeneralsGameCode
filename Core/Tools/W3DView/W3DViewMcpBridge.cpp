/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#include "StdAfx.h"
#include "W3DViewMcpBridge.h"

#include "DataTreeView.h"
#include "GraphicView.h"
#include "MainFrm.h"
#include "MaterialViewer/W3dMaterialParser.h"
#include "Utils.h"
#include "ViewportOverlay.h"
#include "W3DViewDoc.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/hanim.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/surfaceclass.h"
#include "WWLib/ffactory.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using namespace W3dMaterialViewer;

const DWORD MAX_REQUEST_BYTES = 65536;

bool Read_File_Bytes(const std::wstring &path, std::string &contents)
{
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD size = ::GetFileSize(file, nullptr);
	bool ok = size != INVALID_FILE_SIZE && size <= MAX_REQUEST_BYTES;
	if (ok && size > 0) {
		contents.resize(size);
		DWORD bytes_read = 0;
		ok = ::ReadFile(file, &contents[0], size, &bytes_read, nullptr) != FALSE && bytes_read == size;
	}

	::CloseHandle(file);
	return ok;
}

bool Write_File_Bytes(const std::wstring &path, const std::string &contents)
{
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD bytes_written = 0;
	bool ok = ::WriteFile(file, contents.data(), (DWORD)contents.size(), &bytes_written, nullptr) != FALSE
		&& bytes_written == contents.size();

	::CloseHandle(file);
	return ok;
}

// The tool's strings are ANSI, so anything outside printable ASCII leaves as a \u escape.
void Append_String(std::string &out, const std::string &text)
{
	std::wstring wide(text.size(), L'\0');
	int count = 0;
	if (!text.empty()) {
		count = ::MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), &wide[0], (int)wide.size());
	}

	out += '"';
	for (int i = 0; i < count; i++) {
		wchar_t character = wide[i];
		if (character == L'"' || character == L'\\') {
			out += '\\';
			out += (char)character;
		} else if (character < 0x20 || character > 0x7E) {
			char buffer[8];
			::sprintf_s(buffer, "\\u%04x", (unsigned int)character);
			out += buffer;
		} else {
			out += (char)character;
		}
	}
	out += '"';
}

void Add_Key(std::string &out, const char *key)
{
	out += '"';
	out += key;
	out += "\":";
}

void Add_String(std::string &out, const char *key, const std::string &value)
{
	Add_Key(out, key);
	Append_String(out, value);
	out += ',';
}

void Add_Int(std::string &out, const char *key, long long value)
{
	char buffer[32];
	::sprintf_s(buffer, "%lld,", value);
	Add_Key(out, key);
	out += buffer;
}

void Add_Float(std::string &out, const char *key, float value)
{
	char buffer[32];
	::sprintf_s(buffer, "%.9g,", ::_finite(value) ? (double)value : 0.0);
	Add_Key(out, key);
	out += buffer;
}

void Add_Bool(std::string &out, const char *key, bool value)
{
	Add_Key(out, key);
	out += value ? "true," : "false,";
}

// Drops the trailing comma of the last member and closes the object or array.
void Close(std::string &out, char bracket)
{
	if (!out.empty() && out[out.size() - 1] == ',') {
		out.erase(out.size() - 1);
	}
	out += bracket;
	out += ',';
}

void Add_Color(std::string &out, const char *key, const uint8_t *color)
{
	char buffer[32];
	::sprintf_s(buffer, "[%u,%u,%u],", (unsigned int)color[0], (unsigned int)color[1], (unsigned int)color[2]);
	Add_Key(out, key);
	out += buffer;
}

void Add_Int_Array(std::string &out, const char *key, const std::vector<int> &values)
{
	Add_Key(out, key);
	out += '[';
	for (size_t i = 0; i < values.size(); i++) {
		char buffer[16];
		::sprintf_s(buffer, "%d,", values[i]);
		out += buffer;
	}
	Close(out, ']');
}

bool File_Exists(const std::string &path)
{
	DWORD attributes = ::GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string With_Dds_Extension(const std::string &name)
{
	size_t extension = name.rfind('.');
	if (extension == std::string::npos) {
		return name + ".dds";
	}
	return name.substr(0, extension) + ".dds";
}

// Looks in the viewer's search paths, then beside the model, with the engine's .tga to .dds fallback.
std::string Resolve_Texture(const std::string &name, const std::string &model_directory)
{
	if (name.empty()) {
		return std::string();
	}

	const std::string candidates[2] = { name, With_Dds_Extension(name) };

	for (int i = 0; i < 2; i++) {
		file_auto_ptr file(_TheFileFactory, candidates[i].c_str());
		if (file->Is_Available()) {
			return file->File_Name();
		}
	}

	for (int i = 0; i < 2; i++) {
		std::string path = model_directory + candidates[i];
		if (File_Exists(path)) {
			return path;
		}
	}

	return std::string();
}

std::string Directory_Of(const std::string &path)
{
	size_t slash = path.find_last_of("\\/");
	if (slash == std::string::npos) {
		return std::string();
	}
	return path.substr(0, slash + 1);
}

void Add_Mesh(std::string &out, const MeshMaterialData &mesh, const std::string &model_directory)
{
	out += '{';
	Add_String(out, "name", mesh.meshName);
	Add_Int(out, "attributes", mesh.attributes);
	Add_Int(out, "triangles", mesh.numTris);
	Add_Int(out, "vertices", mesh.numVertices);
	Add_String(out, "userText", mesh.userText);
	Add_Int(out, "surfaceType", mesh.surfaceType);
	Add_Int(out, "sortLevel", mesh.sortLevel);
	Add_Bool(out, "prelit", mesh.prelit);
	Add_Int(out, "passCount", mesh.passCount);

	Add_Key(out, "vertexMaterials");
	out += '[';
	for (size_t i = 0; i < mesh.vertexMaterials.size(); i++) {
		const VertexMaterialData &material = mesh.vertexMaterials[i];
		out += '{';
		Add_String(out, "name", material.name);
		Add_Int(out, "attributes", material.attributes);
		Add_Color(out, "ambient", material.ambient);
		Add_Color(out, "diffuse", material.diffuse);
		Add_Color(out, "specular", material.specular);
		Add_Color(out, "emissive", material.emissive);
		Add_Float(out, "shininess", material.shininess);
		Add_Float(out, "opacity", material.opacity);
		Add_Float(out, "translucency", material.translucency);
		Add_String(out, "mapperArgs0", material.mapperArgs0);
		Add_String(out, "mapperArgs1", material.mapperArgs1);
		Close(out, '}');
	}
	Close(out, ']');

	Add_Key(out, "shaders");
	out += '[';
	for (size_t i = 0; i < mesh.shaders.size(); i++) {
		const ShaderData &shader = mesh.shaders[i];
		out += '{';
		Add_Int(out, "depthCompare", shader.depthCompare);
		Add_Int(out, "depthMask", shader.depthMask);
		Add_Int(out, "destBlend", shader.destBlend);
		Add_Int(out, "priGradient", shader.priGradient);
		Add_Int(out, "secGradient", shader.secGradient);
		Add_Int(out, "srcBlend", shader.srcBlend);
		Add_Int(out, "texturing", shader.texturing);
		Add_Int(out, "detailColorFunc", shader.detailColorFunc);
		Add_Int(out, "detailAlphaFunc", shader.detailAlphaFunc);
		Add_Int(out, "alphaTest", shader.alphaTest);
		Add_Int(out, "postDetailColorFunc", shader.postDetailColorFunc);
		Add_Int(out, "postDetailAlphaFunc", shader.postDetailAlphaFunc);
		Close(out, '}');
	}
	Close(out, ']');

	Add_Key(out, "textures");
	out += '[';
	for (size_t i = 0; i < mesh.textures.size(); i++) {
		const TextureData &texture = mesh.textures[i];
		out += '{';
		Add_String(out, "name", texture.name);
		Add_Bool(out, "hasInfo", texture.hasInfo);
		Add_Int(out, "attributes", texture.attributes);
		Add_Int(out, "animType", texture.animType);
		Add_Int(out, "frameCount", texture.frameCount);
		Add_Float(out, "frameRate", texture.frameRate);
		Add_String(out, "resolvedPath", Resolve_Texture(texture.name, model_directory));
		Close(out, '}');
	}
	Close(out, ']');

	Add_Key(out, "passes");
	out += '[';
	for (size_t i = 0; i < mesh.passes.size(); i++) {
		const PassData &pass = mesh.passes[i];
		out += '{';
		Add_Int_Array(out, "vertexMaterialIds", pass.vertexMaterialIds);
		Add_Int_Array(out, "shaderIds", pass.shaderIds);
		Add_Int_Array(out, "stage0TextureIds", pass.stageTextureIds[0]);
		Add_Int_Array(out, "stage1TextureIds", pass.stageTextureIds[1]);
		Add_Int(out, "stage0UvSet", pass.stageUvSet[0]);
		Add_Int(out, "stage1UvSet", pass.stageUvSet[1]);
		Add_Bool(out, "hasDCG", pass.hasDCG);
		Add_Bool(out, "hasDIG", pass.hasDIG);
		Add_Bool(out, "hasSCG", pass.hasSCG);
		Close(out, '}');
	}
	Close(out, ']');

	Close(out, '}');
}

std::string Make_Error(const char *code, const std::string &message)
{
	std::string out = "{\"ok\":false,\"error\":{";
	Add_String(out, "code", code);
	Add_String(out, "message", message);
	Close(out, '}');
	Close(out, '}');
	out.erase(out.size() - 1);
	return out;
}

std::string Make_Result(const std::string &result)
{
	return "{\"ok\":true,\"result\":" + result + "}";
}

// Fills the displayed object's name and the file it was loaded from; either may come back empty.
void Get_Displayed_Model(std::string &object_name, std::string &source_file)
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	if (document == nullptr || document->GetDisplayedObject() == nullptr) {
		return;
	}

	const char *name = document->GetDisplayedObject()->Get_Name();
	if (name == nullptr || name[0] == '\0') {
		return;
	}
	object_name = name;

	const HashTemplateClass<StringClass, StringClass> &source_map = document->GetAssetSourceFileMap();
	StringClass key(name);
	if (source_map.Exists(key)) {
		source_file = (const char *)source_map.Get(key);
	}
}

// The folders set under File > Texture Path, which W3DView searches for textures.
void Add_Texture_Paths(std::string &out)
{
	Add_Key(out, "texturePaths");
	out += '[';
	CW3DViewDoc *document = ::GetCurrentDocument();
	int count = (document != nullptr) ? document->Get_Texture_Path_Count() : 0;
	for (int index = 0; index < count; index++) {
		Append_String(out, (const char *)document->Get_Texture_Path(index));
		out += ',';
	}
	Close(out, ']');
}

std::string Handle_Get_State()
{
	std::string object_name;
	std::string source_file;
	Get_Displayed_Model(object_name, source_file);

	std::string out = "{";
	Add_Int(out, "bridgeVersion", (long long)W3DViewMcpBridge::BRIDGE_VERSION);
	Add_String(out, "displayedObject", object_name);
	Add_String(out, "sourceFile", source_file);

	// The same totals the 7 key draws in the viewport
	CW3DViewDoc *document = ::GetCurrentDocument();
	RenderObjClass *object = (document != nullptr) ? document->GetDisplayedObject() : nullptr;
	Add_Int(out, "polygons", (object != nullptr) ? object->Get_Num_Polys() : 0);
	Add_Int(out, "vertices", ViewportStats::Count_Vertices(object));
	Add_Texture_Paths(out);

	Close(out, '}');
	out.erase(out.size() - 1);
	return Make_Result(out);
}

// An empty path means the displayed model. A bare name resolves through the viewer's search paths.
std::string Handle_Read_Model(const std::string &requested_path)
{
	std::string path = requested_path;
	if (path.empty()) {
		std::string object_name;
		Get_Displayed_Model(object_name, path);
		if (object_name.empty()) {
			return Make_Error("no_model", "W3DView displays no model. Pass a .w3d path.");
		}
		if (path.empty()) {
			return Make_Error("no_source_file", "The displayed object " + object_name + " did not come from a .w3d file.");
		}
	}

	MaterialDocument document;
	bool parsed = false;
	if (path.find_first_of("\\/:") != std::string::npos) {
		parsed = ParseMaterialDocument(path.c_str(), document);
	} else {
		if (path.find('.') == std::string::npos) {
			path += ".w3d";
		}
		parsed = ParseMaterialDocumentFromFactory(path.c_str(), document);
	}

	if (!parsed) {
		return Make_Error("read_failed", "Could not open " + path + ", or it holds no meshes.");
	}

	const std::string model_directory = Directory_Of(document.resolvedDiskPath);

	std::string out = "{";
	Add_String(out, "file", document.filePath);
	Add_String(out, "resolvedPath", document.resolvedDiskPath);
	Add_String(out, "topLevelName", document.topLevelName);
	Add_Texture_Paths(out);
	Add_Key(out, "meshes");
	out += '[';
	for (size_t i = 0; i < document.meshes.size(); i++) {
		Add_Mesh(out, document.meshes[i], model_directory);
	}
	Close(out, ']');
	Close(out, '}');
	out.erase(out.size() - 1);
	return Make_Result(out);
}

// Loads a .w3d file and shows its top-level object through the asset tree, as a click would.
std::string Handle_Open_Model(const std::string &path)
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	CDataTreeView *tree = (document != nullptr) ? document->GetDataTreeView() : nullptr;
	if (tree == nullptr) {
		return Make_Error("not_ready", "W3DView has no document yet.");
	}

	if (!File_Exists(path)) {
		return Make_Error("read_failed", "Could not open " + path + ".");
	}

	document->LoadAssetsFromFile(path.c_str());
	tree->LoadAssetsIntoTree();

	// A file of animations alone has nothing to show, so the viewport keeps its model.
	MaterialDocument parsed;
	if (!ParseMaterialDocument(path.c_str(), parsed)) {
		return Handle_Get_State();
	}

	HTREEITEM item = tree->Find_Asset_Item_By_Name(parsed.topLevelName.c_str());
	if (item == nullptr) {
		return Make_Error("not_in_tree", "Loaded " + path + ", but " + parsed.topLevelName + " is not in the asset tree.");
	}

	tree->GetTreeCtrl().SelectItem(item);
	tree->GetTreeCtrl().EnsureVisible(item);
	return Handle_Get_State();
}

// Every part of the displayed object, with its counts and whether it is hidden.
std::string Handle_List_Meshes()
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	RenderObjClass *displayed = (document != nullptr) ? document->GetDisplayedObject() : nullptr;
	if (displayed == nullptr) {
		return Make_Error("no_model", "W3DView displays no model. Open one first.");
	}

	std::string out = "{";
	Add_String(out, "displayedObject", displayed->Get_Name());
	Add_Key(out, "meshes");
	out += '[';
	int sub_count = displayed->Get_Num_Sub_Objects();
	for (int index = 0; index < sub_count; index++) {
		RenderObjClass *part = displayed->Get_Sub_Object(index);
		if (part == nullptr) {
			continue;
		}
		out += '{';
		Add_String(out, "name", part->Get_Name());
		Add_Bool(out, "hidden", part->Is_Hidden() != 0);
		Add_Int(out, "polygons", part->Get_Num_Polys());
		Add_Int(out, "vertices", ViewportStats::Count_Vertices(part));
		Close(out, '}');
		part->Release_Ref();
	}
	Close(out, ']');
	Close(out, '}');
	out.erase(out.size() - 1);
	return Make_Result(out);
}

// Line 1 is the hidden flag. Each later line is the full name of a part of the displayed object.
std::string Handle_Set_Meshes_Hidden(const std::vector<std::string> &lines)
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	CDataTreeView *tree = (document != nullptr) ? document->GetDataTreeView() : nullptr;
	if (tree == nullptr) {
		return Make_Error("not_ready", "W3DView has no document yet.");
	}

	const bool hidden = lines.size() > 1 && lines[1] == "1";
	for (size_t i = 2; i < lines.size(); i++) {
		if (!lines[i].empty() && !tree->Set_Displayed_Part_Hidden(lines[i].c_str(), hidden)) {
			return Make_Error("not_found", "The displayed object has no part named " + lines[i] + ".");
		}
	}
	return Handle_List_Meshes();
}

const char *Animation_State_Name(CGraphicView *view)
{
	if (view == nullptr) {
		return "stopped";
	}
	switch (view->GetAnimationState()) {
		case CGraphicView::AnimPlaying:
			return "playing";
		case CGraphicView::AnimPaused:
			return "paused";
		default:
			return "stopped";
	}
}

// The loaded animations made for the displayed object's skeleton, and the one in use.
std::string Handle_List_Animations()
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	RenderObjClass *displayed = (document != nullptr) ? document->GetDisplayedObject() : nullptr;
	if (displayed == nullptr) {
		return Make_Error("no_model", "W3DView displays no model. Open one first.");
	}

	const HTreeClass *htree = displayed->Get_HTree();
	const std::string hierarchy = (htree != nullptr) ? htree->Get_Name() : "";

	std::string out = "{";
	Add_String(out, "displayedObject", displayed->Get_Name());
	Add_String(out, "hierarchy", hierarchy);
	Add_Key(out, "animations");
	out += '[';

	AssetIterator *iterator = WW3DAssetManager::Get_Instance()->Create_HAnim_Iterator();
	for (iterator->First(); !hierarchy.empty() && !iterator->Is_Done(); iterator->Next()) {
		HAnimClass *animation = WW3DAssetManager::Get_Instance()->Get_HAnim(iterator->Current_Item_Name());
		if (animation == nullptr) {
			continue;
		}
		if (::_stricmp(animation->Get_HName(), hierarchy.c_str()) == 0) {
			out += '{';
			Add_String(out, "name", animation->Get_Name());
			Add_Int(out, "frames", animation->Get_Num_Frames());
			Add_Float(out, "frameRate", animation->Get_Frame_Rate());
			Add_Float(out, "seconds", animation->Get_Total_Time());
			Close(out, '}');
		}
		animation->Release_Ref();
	}
	delete iterator;
	Close(out, ']');

	HAnimClass *current = document->GetCurrentAnimation();
	Add_String(out, "currentAnimation", (current != nullptr) ? current->Get_Name() : "");
	Add_Int(out, "currentFrame", (current != nullptr) ? document->GetCurrentFrame() : 0);
	Add_Int(out, "currentFrames", (current != nullptr) ? current->Get_Num_Frames() : 0);
	Add_String(out, "state", Animation_State_Name(document->GetGraphicView()));
	Close(out, '}');
	out.erase(out.size() - 1);
	return Make_Result(out);
}

// Line 1 is an animation name, or empty for the one in use. Line 2 is the frame to stop on.
std::string Handle_Set_Animation_Frame(const std::vector<std::string> &lines)
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	CMainFrame *frame = (CMainFrame *)::AfxGetMainWnd();
	RenderObjClass *displayed = (document != nullptr) ? document->GetDisplayedObject() : nullptr;
	if (displayed == nullptr || frame == nullptr) {
		return Make_Error("no_model", "W3DView displays no model. Open one first.");
	}

	const std::string name = lines.size() > 1 ? lines[1] : std::string();
	HAnimClass *current = document->GetCurrentAnimation();
	if (!name.empty() && (current == nullptr || ::_stricmp(current->Get_Name(), name.c_str()) != 0)) {
		HAnimClass *animation = WW3DAssetManager::Get_Instance()->Get_HAnim(name.c_str());
		if (animation == nullptr) {
			return Make_Error("not_found", "No loaded animation is named " + name + ".");
		}
		animation->Release_Ref();

		document->PlayAnimation(displayed, name.c_str(), false, false);

		// The start that PlayAnimation posts must run before the seek pauses playback.
		frame->Run_Posted_Commands();
		current = document->GetCurrentAnimation();
	}

	if (current == nullptr) {
		return Make_Error("no_animation", "No animation is playing. Name one to start it.");
	}

	const int frame_count = current->Get_Num_Frames();
	const int target = lines.size() > 2 ? ::atoi(lines[2].c_str()) : 0;
	if (target < 0 || target >= frame_count) {
		char range[96];
		::sprintf_s(range, "Frame %d is outside 0 to %d.", target, frame_count - 1);
		return Make_Error("bad_frame", range);
	}

	frame->Go_To_Animation_Frame(target);

	CGraphicView *view = document->GetGraphicView();
	if (view != nullptr) {
		view->RepaintView(FALSE);
	}
	return Handle_List_Animations();
}

std::wstring Widen(const std::string &text)
{
	std::wstring wide(text.size(), L'\0');
	int count = 0;
	if (!text.empty()) {
		count = ::MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), &wide[0], (int)wide.size());
	}
	wide.resize(count);
	return wide;
}

// Writes the back buffer as a 32-bit top-down BMP.
bool Save_Back_Buffer(const std::wstring &path, unsigned int &width, unsigned int &height)
{
	SurfaceClass *surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	if (surface == nullptr) {
		return false;
	}

	SurfaceClass::SurfaceDescription description;
	surface->Get_Description(description);
	if (description.Format != WW3D_FORMAT_A8R8G8B8 && description.Format != WW3D_FORMAT_X8R8G8B8) {
		surface->Release_Ref();
		return false;
	}

	SurfaceClass *copy = NEW_REF(SurfaceClass, (description.Width, description.Height, description.Format));
	DX8Wrapper::_Copy_DX8_Rects(surface->Peek_D3D_Surface(), nullptr, 0, copy->Peek_D3D_Surface(), nullptr);
	surface->Release_Ref();

	int pitch = 0;
	const char *bits = static_cast<const char *>(copy->Lock(&pitch));
	if (bits == nullptr) {
		copy->Release_Ref();
		return false;
	}

	width = description.Width;
	height = description.Height;
	const size_t row_bytes = (size_t)width * 4;

	BITMAPFILEHEADER file_header;
	BITMAPINFOHEADER info_header;
	::memset(&file_header, 0, sizeof(file_header));
	::memset(&info_header, 0, sizeof(info_header));
	file_header.bfType = 0x4D42;
	file_header.bfOffBits = sizeof(file_header) + sizeof(info_header);
	file_header.bfSize = file_header.bfOffBits + (DWORD)(row_bytes * height);
	info_header.biSize = sizeof(info_header);
	info_header.biWidth = (LONG)width;
	info_header.biHeight = -(LONG)height;
	info_header.biPlanes = 1;
	info_header.biBitCount = 32;
	info_header.biCompression = BI_RGB;

	std::string contents;
	contents.reserve(file_header.bfSize);
	contents.append(reinterpret_cast<const char *>(&file_header), sizeof(file_header));
	contents.append(reinterpret_cast<const char *>(&info_header), sizeof(info_header));
	for (unsigned int y = 0; y < height; y++) {
		contents.append(bits + (size_t)y * pitch, row_bytes);
	}

	copy->Unlock();
	copy->Release_Ref();
	return Write_File_Bytes(path, contents);
}

// Line 1 is the fit flag. Each later line is "x y z path", a view cube region and the BMP to write.
std::string Handle_Capture_Views(const std::vector<std::string> &lines)
{
	CW3DViewDoc *document = ::GetCurrentDocument();
	CGraphicView *view = (document != nullptr) ? document->GetGraphicView() : nullptr;
	if (view == nullptr || view->GetCamera() == nullptr) {
		return Make_Error("not_ready", "W3DView has no viewport yet.");
	}
	if (document->GetDisplayedObject() == nullptr) {
		return Make_Error("no_model", "W3DView displays no model. Open one first.");
	}

	const bool fit = lines.size() > 1 && lines[1] == "1";
	const Matrix3D saved_camera = view->GetCamera()->Get_Transform();

	std::string out = "{";
	Add_Key(out, "views");
	out += '[';

	std::string error;
	for (size_t i = 2; i < lines.size() && error.empty(); i++) {
		int x = 0;
		int y = 0;
		int z = 0;
		int path_offset = 0;
		if (::sscanf_s(lines[i].c_str(), "%d %d %d %n", &x, &y, &z, &path_offset) < 3 || path_offset == 0
				|| (x == 0 && y == 0 && z == 0)) {
			error = Make_Error("bad_request", "Malformed view line: " + lines[i]);
			break;
		}
		const std::string path = lines[i].substr(path_offset);

		view->Snap_Camera_To_Cube_Region(Vector3((float)x, (float)y, (float)z), fit);

		// A flip swap chain cycles its buffers, so every one of them must hold this view.
		for (int frame = 0; frame < 3; frame++) {
			view->RepaintView(FALSE);
		}

		unsigned int width = 0;
		unsigned int height = 0;
		if (!Save_Back_Buffer(Widen(path), width, height)) {
			error = Make_Error("capture_failed", "Could not read the back buffer or write " + path + ".");
			break;
		}

		out += '{';
		Add_String(out, "path", path);
		Add_Int(out, "width", width);
		Add_Int(out, "height", height);
		Close(out, '}');
	}

	view->Set_Camera_Transform(saved_camera);
	view->RepaintView(FALSE);

	if (!error.empty()) {
		return error;
	}

	Close(out, ']');
	Add_String(out, "displayedObject", document->GetDisplayedObject()->Get_Name());
	Close(out, '}');
	out.erase(out.size() - 1);
	return Make_Result(out);
}

// The request file holds the command on its first line and its arguments on the lines after.
std::string Handle_Request_Text(const std::string &request)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= request.size()) {
		size_t end = request.find('\n', start);
		if (end == std::string::npos) {
			end = request.size();
		}
		std::string line = request.substr(start, end - start);
		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1);
		}
		lines.push_back(line);
		start = end + 1;
	}

	const std::string &command = lines[0];
	const std::string argument = lines.size() > 1 ? lines[1] : std::string();

	if (command == "get_state") {
		return Handle_Get_State();
	}
	if (command == "read_model") {
		return Handle_Read_Model(argument);
	}
	if (command == "open_model") {
		return Handle_Open_Model(argument);
	}
	if (command == "capture_views") {
		return Handle_Capture_Views(lines);
	}
	if (command == "list_animations") {
		return Handle_List_Animations();
	}
	if (command == "set_animation_frame") {
		return Handle_Set_Animation_Frame(lines);
	}
	if (command == "list_meshes") {
		return Handle_List_Meshes();
	}
	if (command == "set_meshes_hidden") {
		return Handle_Set_Meshes_Hidden(lines);
	}
	return Make_Error("unknown_command", "Unknown command " + command + ".");
}

} // namespace

namespace W3DViewMcpBridge
{

void Attach(HWND window)
{
	if (window != nullptr) {
		::SetPropW(window, WINDOW_PROPERTY, reinterpret_cast<HANDLE>(BRIDGE_VERSION));
	}
}

void Detach(HWND window)
{
	if (window != nullptr) {
		::RemovePropW(window, WINDOW_PROPERTY);
	}
}

BOOL IsEnabled(HWND window)
{
	return window != nullptr && ::GetPropW(window, WINDOW_PROPERTY) != nullptr;
}

BOOL IsRequest(const COPYDATASTRUCT *copy_data)
{
	return copy_data != nullptr
		&& copy_data->dwData == REQUEST_MAGIC
		&& copy_data->lpData != nullptr
		&& copy_data->cbData >= sizeof(wchar_t)
		&& copy_data->cbData <= 65536
		&& copy_data->cbData % sizeof(wchar_t) == 0;
}

BOOL HandleRequest(const COPYDATASTRUCT *copy_data)
{
	if (!IsRequest(copy_data)) {
		return FALSE;
	}

	const wchar_t *path_value = static_cast<const wchar_t *>(copy_data->lpData);
	size_t character_count = copy_data->cbData / sizeof(wchar_t);
	std::wstring request_path(path_value, character_count);
	size_t terminator = request_path.find(L'\0');
	if (terminator != std::wstring::npos) {
		request_path.resize(terminator);
	}

	std::string request;
	if (request_path.empty() || !Read_File_Bytes(request_path, request)) {
		return FALSE;
	}

	return Write_File_Bytes(request_path + L".response", Handle_Request_Text(request)) ? TRUE : FALSE;
}

} // namespace W3DViewMcpBridge
