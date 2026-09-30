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

// FILE: RmlInterfaceArea.h ///////////////////////////////////////////////////////
// The interface width option (Options > Display, settings.json ui.interface_width): on a screen wider
// than the chosen shape the interface keeps to a centred band and the shell map shows on both sides.
//
// RCSS has no calc(), so the band is applied here, once for every document: a framed shell screen
// (body.hq, the main menu's body.mm-d, the load screens' body.ld and the social dock) gets side margins
// and the band's width on its body, which moves its frame, header, content and action bar in as one;
// full-bleed art opts out with vw units (LoadScreen.rcss). The control bar keeps its own full-width
// rail and wings and only needs a class (body.iw-169; its 21:9 cap is the default, ControlBar.rcss).
// The margins are symmetric, so right-to-left documents need nothing more.
//
// Header only on RmlUi, so the render harness applies exactly this.
#pragma once

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdio>
#include <string>

namespace RmlInterfaceArea
{

enum Width
{
	WIDTH_FULL = 0,
	WIDTH_21_9 = 1, ///< 43:18, the 3440x1440 shape
	WIDTH_16_9 = 2,
};

// Each side's inset in pixels for a w x h screen; 0 when the screen is not wider than the band.
inline int inset(int width, int w, int h)
{
	float aspect = 0.0f;
	if (width == WIDTH_21_9)
		aspect = 43.0f / 18.0f;
	else if (width == WIDTH_16_9)
		aspect = 16.0f / 9.0f;
	if (aspect <= 0.0f || h <= 0)
		return 0;
	const float band = (float)h * aspect;
	return band + 1.0f < (float)w ? (int)(((float)w - band) * 0.5f) : 0;
}

// A shell screen framed by the template: its whole body moves into the band.
inline bool framed(Rml::ElementDocument *document)
{
	if (document->IsClassSet("hq") || document->IsClassSet("mm-d") || document->IsClassSet("ld"))
		return true;
	const Rml::String &url = document->GetSourceURL();
	static const char dock[] = "SocialDock.rml";
	return url.size() >= sizeof(dock) - 1 && url.compare(url.size() - (sizeof(dock) - 1), sizeof(dock) - 1, dock) == 0;
}

inline void applyToDocument(Rml::ElementDocument *document, int width, int w, int h)
{
	if (!document)
		return;
	if (document->IsClassSet("cb"))
	{
		document->SetClass("iw-169", width == WIDTH_16_9);
		return;
	}
	if (!framed(document))
		return;

	const int px = inset(width, w, h);
	char applied[32];
	std::snprintf(applied, sizeof(applied), "%d/%d", px, w);
	const Rml::Variant *was = document->GetAttribute("data-iw");
	if (was ? was->Get<Rml::String>() == applied : px == 0)
		return;
	document->SetAttribute("data-iw", Rml::String(applied));
	if (px == 0)
	{
		document->RemoveProperty("margin-left");
		document->RemoveProperty("margin-right");
		document->RemoveProperty("width");
		return;
	}
	char value[32];
	std::snprintf(value, sizeof(value), "%dpx", px);
	document->SetProperty("margin-left", value);
	document->SetProperty("margin-right", value);
	std::snprintf(value, sizeof(value), "%dpx", w - 2 * px);
	document->SetProperty("width", value);
}

// Every open document of the context; cheap when nothing changed.
inline void apply(Rml::Context *context, int width)
{
	if (!context)
		return;
	const Rml::Vector2i size = context->GetDimensions();
	for (int i = 0; i < context->GetNumDocuments(); ++i)
		applyToDocument(context->GetDocument(i), width, size.x, size.y);
}

} // namespace RmlInterfaceArea
