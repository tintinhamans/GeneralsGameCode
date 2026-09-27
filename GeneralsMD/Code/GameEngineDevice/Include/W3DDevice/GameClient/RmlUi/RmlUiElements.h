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

// FILE: RmlUiElements.h //////////////////////////////////////////////////////
// Two custom elements registered once by RmlUiManager::init():
//  - <gametext key="GUI:Label"/>  resolves through TheGameText->fetch() so RML
//    markup uses the same localized CSF strings as the .wnd version.
//  - <mappedimage name="Foo"/>    resolves an INI MappedImage to its atlas
//    page and pixel rect, then injects a plain <img rect="L T R B"/> child so
//    it shows only that image's sub-region. RmlUi's built-in <img> already
//    supports the "rect" attribute for atlas cropping, so this only needs a
//    plain Element (not the internal image element type) to compute it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/Element.h>

// Registered with Rml::ElementInstancerGeneric<T> (see RmlUiManager::registerCustomElements()),
// so no custom instancer class is needed here.

//-------------------------------------------------------------------------------------------------
class RmlGameTextElement : public Rml::Element
{
public:
	explicit RmlGameTextElement(const Rml::String &tag);
	virtual ~RmlGameTextElement() override;

protected:
	virtual void OnAttributeChange(const Rml::ElementAttributes &changed_attributes) override;

private:
	void refresh();
};

//-------------------------------------------------------------------------------------------------
class RmlMappedImageElement : public Rml::Element
{
public:
	explicit RmlMappedImageElement(const Rml::String &tag);
	virtual ~RmlMappedImageElement() override;

protected:
	virtual void OnAttributeChange(const Rml::ElementAttributes &changed_attributes) override;

private:
	void refresh();
};
