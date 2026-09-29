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

// FILE: RmlUiFileInterface.h /////////////////////////////////////////////////
// Rml::FileInterface over TheFileSystem, so .rml/.rcss/font/image loads go
// through the same embedded-Data/loose-override resolution as everything else. Also appends a
// generated sprite sheet (one @spritesheet per texture page, every INI MappedImage a sprite) to
// UI/common.rcss as it is opened, so every document that links it can name engine art in RCSS
// decorators (RmlUi has no @import, and this needs no per-document <link>).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <set>
#include <string>

class File;

// The shared stylesheet every RmlUi document links; see above.
static const char *const RmlCommonRcssPath = "UI/common.rcss";

//-------------------------------------------------------------------------------------------------
class RmlUiFileInterface : public Rml::FileInterface
{
public:
	virtual Rml::FileHandle Open(const Rml::String &path) override;
	virtual void Close(Rml::FileHandle file) override;
	virtual size_t Read(void *buffer, size_t size, Rml::FileHandle file) override;
	virtual bool Seek(Rml::FileHandle file, long offset, int origin) override;
	virtual size_t Tell(Rml::FileHandle file) override;
	virtual size_t Length(Rml::FileHandle file) override;

private:
	struct MemFile
	{
		std::string data;
		size_t pos = 0;
	};

	std::string m_mappedImagesRcss; // built on first use, once the collection is loaded
	std::set<MemFile *> m_memFiles; // open virtual handles (real handles are File *)
};
