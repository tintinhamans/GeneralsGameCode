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

#include "W3DDevice/GameClient/RmlUi/RmlUiElements.h"

#include "Common/AsciiString.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"

#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Variant.h>

#include <cstdio>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
static Rml::String unicodeToUtf8(const UnicodeString &str)
{
	const WideChar *wide = str.str();
	if (!wide || !*wide)
		return Rml::String();

	int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return Rml::String();

	Rml::String utf8;
	utf8.resize((size_t)len - 1); // len includes the null terminator
	::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)wide, -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

//-------------------------------------------------------------------------------------------------
RmlGameTextElement::RmlGameTextElement(const Rml::String &tag) : Rml::Element(tag)
{
}

RmlGameTextElement::~RmlGameTextElement()
{
}

void RmlGameTextElement::OnAttributeChange(const Rml::ElementAttributes &changed_attributes)
{
	Rml::Element::OnAttributeChange(changed_attributes);
	if (changed_attributes.find("key") != changed_attributes.end())
		m_dirty = true;
}

void RmlGameTextElement::OnUpdate()
{
	Rml::Element::OnUpdate();
	if (m_dirty)
	{
		m_dirty = false;
		refresh();
	}
}

void RmlGameTextElement::refresh()
{
	Rml::String key = GetAttribute<Rml::String>("key", "");
	if (key.empty() || !TheGameText)
		return;

	UnicodeString text = TheGameText->fetch(AsciiString(key.c_str()));
	SetInnerRML(unicodeToUtf8(text));
}

//-------------------------------------------------------------------------------------------------
RmlMappedImageElement::RmlMappedImageElement(const Rml::String &tag) : Rml::Element(tag)
{
}

RmlMappedImageElement::~RmlMappedImageElement()
{
}

void RmlMappedImageElement::OnAttributeChange(const Rml::ElementAttributes &changed_attributes)
{
	Rml::Element::OnAttributeChange(changed_attributes);
	if (changed_attributes.find("name") != changed_attributes.end())
		m_dirty = true;
}

void RmlMappedImageElement::OnUpdate()
{
	Rml::Element::OnUpdate();
	if (m_dirty)
	{
		m_dirty = false;
		refresh();
	}
}

void RmlMappedImageElement::refresh()
{
	Rml::String name = GetAttribute<Rml::String>("name", "");
	if (name.empty() || !TheMappedImageCollection)
		return;

	const Image *image = TheMappedImageCollection->findImageByName(AsciiString(name.c_str()));
	if (!image)
		return;

	// UV coords are normalized [0,1] over the atlas page; <img rect="..."> wants pixel
	// coordinates (x y width height), so scale by the page's own dimensions.
	const Region2D *uv = image->getUV();
	const ICoord2D *texSize = image->getTextureSize();
	int left = (int)(uv->lo.x * texSize->x);
	int top = (int)(uv->lo.y * texSize->y);
	int right = (int)(uv->hi.x * texSize->x);
	int bottom = (int)(uv->hi.y * texSize->y);

	// Leading '/' stops RmlUi from resolving the texture name against the document folder.
	char rml[512];
	_snprintf_s(rml, sizeof(rml), _TRUNCATE,
		"<img style=\"width:100%%;height:100%%;\" src=\"/%s\" rect=\"%d %d %d %d\"/>",
		image->getFilename().str(), left, top, right - left, bottom - top);

	SetInnerRML(rml);
}

//-------------------------------------------------------------------------------------------------
RmlMapPreviewElement::RmlMapPreviewElement(const Rml::String &tag) : Rml::Element(tag)
{
}

RmlMapPreviewElement::~RmlMapPreviewElement()
{
}

void RmlMapPreviewElement::OnAttributeChange(const Rml::ElementAttributes &changed_attributes)
{
	Rml::Element::OnAttributeChange(changed_attributes);
	if (changed_attributes.find("map") != changed_attributes.end())
		m_dirty = true;
}

void RmlMapPreviewElement::OnUpdate()
{
	Rml::Element::OnUpdate();
	if (m_dirty)
	{
		m_dirty = false;
		refresh();
	}
}

void RmlMapPreviewElement::refresh()
{
	Rml::String mapName = GetAttribute<Rml::String>("map", "");
	if (mapName.empty())
	{
		SetInnerRML("");
		return;
	}

	// getMapPreviewImage() both generates (first use) and registers the map's preview .tga into
	// TheMappedImageCollection, then returns it -- same call SkirmishGameOptionsMenu.cpp's
	// positionStartSpots() makes for the .wnd map preview window.
	Image *image = getMapPreviewImage(AsciiString(mapName.c_str()));
	if (!image)
	{
		SetInnerRML("");
		return;
	}

	const Region2D *uv = image->getUV();
	const ICoord2D *texSize = image->getTextureSize();
	int left = (int)(uv->lo.x * texSize->x);
	int top = (int)(uv->lo.y * texSize->y);
	int right = (int)(uv->hi.x * texSize->x);
	int bottom = (int)(uv->hi.y * texSize->y);

	char rml[512];
	_snprintf_s(rml, sizeof(rml), _TRUNCATE,
		"<img style=\"width:100%%;height:100%%;\" src=\"/%s\" rect=\"%d %d %d %d\"/>",
		image->getFilename().str(), left, top, right - left, bottom - top);

	SetInnerRML(rml);
}
