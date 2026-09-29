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

#include "W3DDevice/GameClient/RmlUi/RmlUiFileInterface.h"

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameClient/Image.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiRenderInterface.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <string.h>

// One @spritesheet per texture page; sprite rects use the fixed RmlEngineTextureSpace that
// RmlUiRenderInterface::LoadTexture reports for engine textures (rect = uv * space). The leading
// '/' in src keeps RmlUi from joining it onto the stylesheet folder. Rotated and raw-texture
// images can't be expressed as a plain rect, so they are skipped.
static std::string buildMappedImagesRcss()
{
	if (!TheMappedImageCollection)
		return std::string();

	std::map<std::string, std::string> sheets; // lowercased texture file -> sprite lines
	std::map<std::string, std::string> sheetFiles; // lowercased texture file -> texture file
	std::set<std::string> seen;
	const float space = (float)RmlEngineTextureSpace;

	for (unsigned i = 0; Image *image = TheMappedImageCollection->Enum(i); ++i)
	{
		if (image->getStatus() & (IMAGE_STATUS_ROTATED_90_CLOCKWISE | IMAGE_STATUS_RAW_TEXTURE))
			continue;

		std::string name = image->getName().str();
		std::string file = image->getFilename().str();
		if (name.empty() || file.empty())
			continue;

		bool valid = !isdigit((unsigned char)name[0]);
		for (size_t c = 0; valid && c < name.size(); ++c)
			valid = isalnum((unsigned char)name[c]) || name[c] == '_' || name[c] == '-';
		if (!valid || !seen.insert(name).second)
			continue;

		const Region2D *uv = image->getUV();
		int left = (int)(uv->lo.x * space + 0.5f);
		int top = (int)(uv->lo.y * space + 0.5f);
		int width = (int)(uv->hi.x * space + 0.5f) - left;
		int height = (int)(uv->hi.y * space + 0.5f) - top;
		if (width <= 0 || height <= 0)
			continue;

		std::string key = file;
		for (size_t c = 0; c < key.size(); ++c)
			key[c] = (char)tolower((unsigned char)key[c]);
		sheetFiles.emplace(key, file);

		char line[256];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "\t%s: %dpx %dpx %dpx %dpx;\n", name.c_str(), left, top, width, height);
		sheets[key] += line;
	}

	std::string rcss;
	int index = 0;
	for (std::map<std::string, std::string>::const_iterator it = sheets.begin(); it != sheets.end(); ++it, ++index)
	{
		char head[512];
		_snprintf_s(head, sizeof(head), _TRUNCATE, "@spritesheet mapped-%d\n{\n\tsrc: /%s;\n", index, sheetFiles[it->first].c_str());
		rcss += head;
		rcss += it->second;
		rcss += "}\n";
	}
	return rcss;
}

Rml::FileHandle RmlUiFileInterface::Open(const Rml::String &path)
{
	if (TheFileSystem && _stricmp(path.c_str(), RmlCommonRcssPath) == 0)
	{
		File *real = TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
		if (!real)
			return 0;
		MemFile *mem = new MemFile();
		mem->data.resize((size_t)real->size());
		if (!mem->data.empty())
			real->read(&mem->data[0], (Int)mem->data.size());
		real->close();
		if (m_mappedImagesRcss.empty())
			m_mappedImagesRcss = buildMappedImagesRcss();
		mem->data += "\n";
		mem->data += m_mappedImagesRcss;
		m_memFiles.insert(mem);
		return (Rml::FileHandle)mem;
	}

	if (!TheFileSystem)
		return 0;
	File *file = TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
	return (Rml::FileHandle)file;
}

void RmlUiFileInterface::Close(Rml::FileHandle file)
{
	MemFile *mem = (MemFile *)file;
	if (m_memFiles.erase(mem))
	{
		delete mem;
		return;
	}
	if (file)
		((File *)file)->close();
}

size_t RmlUiFileInterface::Read(void *buffer, size_t size, Rml::FileHandle file)
{
	if (!file)
		return 0;
	MemFile *mem = (MemFile *)file;
	if (m_memFiles.count(mem))
	{
		size_t count = mem->pos < mem->data.size() ? mem->data.size() - mem->pos : 0;
		if (count > size)
			count = size;
		memcpy(buffer, mem->data.data() + mem->pos, count);
		mem->pos += count;
		return count;
	}
	Int result = ((File *)file)->read(buffer, (Int)size);
	return result > 0 ? (size_t)result : 0;
}

bool RmlUiFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	if (!file)
		return false;
	MemFile *mem = (MemFile *)file;
	if (m_memFiles.count(mem))
	{
		long base = origin == SEEK_CUR ? (long)mem->pos : origin == SEEK_END ? (long)mem->data.size() : 0;
		long target = base + offset;
		if (target < 0)
			return false;
		mem->pos = (size_t)target;
		return true;
	}
	// SEEK_SET/SEEK_CUR/SEEK_END (0/1/2) line up with File::START/CURRENT/END.
	((File *)file)->seek((Int)offset, (File::seekMode)origin);
	return true;
}

size_t RmlUiFileInterface::Tell(Rml::FileHandle file)
{
	if (!file)
		return 0;
	MemFile *mem = (MemFile *)file;
	if (m_memFiles.count(mem))
		return mem->pos;
	return (size_t)((File *)file)->seek(0, File::CURRENT);
}

size_t RmlUiFileInterface::Length(Rml::FileHandle file)
{
	if (!file)
		return 0;
	MemFile *mem = (MemFile *)file;
	if (m_memFiles.count(mem))
		return mem->data.size();
	return (size_t)((File *)file)->size();
}
