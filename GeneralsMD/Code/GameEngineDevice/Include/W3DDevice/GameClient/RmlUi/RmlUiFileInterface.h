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
// through the same embedded-Data/loose-override resolution as everything else.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/FileInterface.h>

class File;

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
};
