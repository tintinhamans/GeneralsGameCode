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
// Custom elements registered once by RmlUiManager::init():
//  - <gametext key="GUI:Label"/>  resolves through TheGameText->fetch() so RML
//    markup uses the same localized CSF strings as the .wnd version.
//  - <mappedimage name="Foo"/>    resolves an INI MappedImage to its atlas
//    page and pixel rect, then injects a plain <img rect="L T R B"/> child so
//    it shows only that image's sub-region. RmlUi's built-in <img> already
//    supports the "rect" attribute for atlas cropping, so this only needs a
//    plain Element (not the internal image element type) to compute it.
//  - <mappreview map="Name"/>     same idea, driving getMapPreviewImage()
//    instead of a static MappedImage lookup: that function both generates
//    (on first use) and registers the map's preview .tga into
//    TheMappedImageCollection under its own derived name, then returns the
//    Image* -- see GameClient/MapUtil.h and SkirmishGameOptionsMenu.cpp's
//    positionStartSpots(). Start-position markers are NOT drawn by this
//    element; they come from GameSetupData::m_options.m_startPositionMarkers,
//    laid out by the screen's own data-for markup over this element (see
//    Assets/UI/GameSetup.rcss).
//  - <scrolllog>                  a scrolling text pane that follows its newest line (chat, status
//    feed) -- see RmlScrollLogElement.
//  - <video source="Name" fit="contain"/>  draws a movie the game decodes into a VideoBuffer and
//    publishes under Name -- see RmlVideoElement.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

class VideoBuffer;
struct IDirect3DTexture8;

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
	virtual void OnUpdate() override;

private:
	void refresh();

	// Rebuilt on update, after the parser has added any children (e.g. a <select> copying an option).
	bool m_dirty = false;
};

//-------------------------------------------------------------------------------------------------
class RmlMappedImageElement : public Rml::Element
{
public:
	explicit RmlMappedImageElement(const Rml::String &tag);
	virtual ~RmlMappedImageElement() override;

protected:
	virtual void OnAttributeChange(const Rml::ElementAttributes &changed_attributes) override;
	virtual void OnUpdate() override;

private:
	void refresh();

	// Rebuilt on update, after the parser has added any children (e.g. a <select> copying an option).
	bool m_dirty = false;
};

//-------------------------------------------------------------------------------------------------
class RmlMapPreviewElement : public Rml::Element
{
public:
	explicit RmlMapPreviewElement(const Rml::String &tag);
	virtual ~RmlMapPreviewElement() override;

protected:
	virtual void OnAttributeChange(const Rml::ElementAttributes &changed_attributes) override;
	virtual void OnUpdate() override;

private:
	void refresh();
	void fitToAspect(float aspect);

	// Rebuilt on update, after the parser has added any children (e.g. a <select> copying an option).
	bool m_dirty = false;
};

//-------------------------------------------------------------------------------------------------
// Scrolling log pane (chat, status feed): when its content grows it jumps to the new bottom, unless
// the user has scrolled up to read older lines. Screens just bind the lines; no per-screen scrolling.
// Checked every update against the previous layout's scroll height, so the jump lands a frame after
// the data change, once the new line has been laid out.
class RmlScrollLogElement : public Rml::Element
{
public:
	explicit RmlScrollLogElement(const Rml::String &tag) : Rml::Element(tag) {}

protected:
	virtual void OnUpdate() override
	{
		Rml::Element::OnUpdate();

		const float height = GetScrollHeight();
		if (height != m_lastHeight)
		{
			m_lastHeight = height;
			if (m_following)
				SetScrollTop(height);
		}
		else
		{
			m_following = GetScrollTop() + GetClientHeight() >= height - 1.0f;
		}
	}

private:
	float m_lastHeight = 0.0f;
	bool m_following = true; // false once the user scrolled away from the bottom
};

//-------------------------------------------------------------------------------------------------
// Movie frame: shows the VideoBuffer published under its "source" attribute, the texture the owner's
// stream renders every frame into, so decoding and timing stay with the owner (the load screens decode
// for their .wnd view the same way). fit: "fill" stretches (the .wnd way), "contain" letterboxes
// and "cover" crops, both keeping the movie's aspect. Only the visible part of the power-of-two
// buffer texture is sampled. Nothing is drawn while no buffer is published.
class RmlVideoElement : public Rml::Element
{
public:
	explicit RmlVideoElement(const Rml::String &tag);
	virtual ~RmlVideoElement() override;

	// null clears it. An owner clears or replaces its buffer before deleting it.
	static void setSource(const Rml::String &name, VideoBuffer *buffer);

protected:
	virtual void OnRender() override;
	virtual void OnResize() override;
	virtual void OnAttributeChange(const Rml::ElementAttributes &changed_attributes) override;
	virtual void OnPropertyChange(const Rml::PropertyIdSet &changed_properties) override;

private:
	void bind(VideoBuffer *buffer);
	void buildGeometry();

	VideoBuffer *m_buffer = nullptr; ///< only compared once bound; everything read from it is cached here
	IDirect3DTexture8 *m_d3dTexture = nullptr; ///< our own reference
	Rml::Vector2i m_textureSize;
	Rml::Vector2f m_videoSize;
	Rml::CallbackTexture m_texture;
	Rml::Geometry m_geometry;
	bool m_geometryDirty = true;
};
