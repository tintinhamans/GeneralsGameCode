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
#include "W3DDevice/GameClient/RmlUi/RmlUiRenderInterface.h"

#include "Common/AsciiString.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "WW3D2/texture.h"
#include "W3DDevice/GameClient/W3DVideoBuffer.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Variant.h>

#include <cstdio>
#include <map>
#include <windows.h>

//-------------------------------------------------------------------------------------------------
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
	// coordinates (x y width height) in the fixed space LoadTexture reports for engine textures.
	const Region2D *uv = image->getUV();
	const float space = (float)RmlEngineTextureSpace;
	int left = (int)(uv->lo.x * space + 0.5f);
	int top = (int)(uv->lo.y * space + 0.5f);
	int right = (int)(uv->hi.x * space + 0.5f);
	int bottom = (int)(uv->hi.y * space + 0.5f);
	RmlInsetTexelRect(left, top, right, bottom, image->getTextureSize()->x, image->getTextureSize()->y);

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
	// The art is this element's first child, so children in the markup (start markers) stay on top
	// of it and are placed on the image itself.
	Rml::Element *art = GetFirstChild();
	if (art && !art->IsClassSet("mp-art"))
		art = nullptr;

	Rml::String mapName = GetAttribute<Rml::String>("map", "");
	// getMapPreviewImage() both generates (first use) and registers the map's preview .tga into
	// TheMappedImageCollection, then returns it -- same call SkirmishGameOptionsMenu.cpp's
	// positionStartSpots() makes for the .wnd map preview window.
	Image *image = mapName.empty() ? nullptr : getMapPreviewImage(AsciiString(mapName.c_str()));
	if (!image)
	{
		if (art)
			RemoveChild(art);
		fitToAspect(0.0f);
		return;
	}

	const Region2D *uv = image->getUV();
	const float space = (float)RmlEngineTextureSpace;
	int left = (int)(uv->lo.x * space + 0.5f);
	int top = (int)(uv->lo.y * space + 0.5f);
	int right = (int)(uv->hi.x * space + 0.5f);
	int bottom = (int)(uv->hi.y * space + 0.5f);
	RmlInsetTexelRect(left, top, right, bottom, image->getTextureSize()->x, image->getTextureSize()->y);

	if (!art)
	{
		Rml::ElementPtr created = GetOwnerDocument()->CreateElement("img");
		created->SetClass("mp-art", true);
		art = GetFirstChild() ? InsertBefore(std::move(created), GetFirstChild()) : AppendChild(std::move(created));
	}

	char rect[64];
	_snprintf_s(rect, sizeof(rect), _TRUNCATE, "%d %d %d %d", left, top, right - left, bottom - top);
	art->SetAttribute("src", Rml::String("/") + image->getFilename().str());
	art->SetAttribute("rect", Rml::String(rect));

	const float width = (uv->hi.x - uv->lo.x) * (float)image->getTextureSize()->x;
	const float height = (uv->hi.y - uv->lo.y) * (float)image->getTextureSize()->y;
	fitToAspect(height > 0.0f ? width / height : 0.0f);
}

// A map's own preview can have any shape and is never stretched. By default it is letterboxed,
// centred in its square frame (percentage margins resolve against the frame's width, which equals its
// height). fit="width" spans the frame's width and takes the art's height (at most square), for a
// frame whose height follows it. fit="fill" keeps the element's own size (full-bleed load screen art).
void RmlMapPreviewElement::fitToAspect(float aspect)
{
	const Rml::String fit = GetAttribute<Rml::String>("fit", "");
	if (fit == "fill" || (aspect <= 0.0f && fit != "width"))
	{
		RemoveProperty("width");
		RemoveProperty("height");
		RemoveProperty("padding-bottom");
		RemoveProperty("margin-left");
		RemoveProperty("margin-top");
		return;
	}

	char value[32];
	if (fit == "width")
	{
		if (aspect <= 0.0f)
			aspect = 1.0f;
		const float widthPct = aspect >= 1.0f ? 100.0f : 100.0f * aspect;
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", widthPct);
		SetProperty("width", value);
		SetProperty("height", "0px");
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", widthPct / aspect); // padding % resolves against the width
		SetProperty("padding-bottom", value);
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", (100.0f - widthPct) * 0.5f);
		SetProperty("margin-left", value);
		RemoveProperty("margin-top");
		return;
	}

	const float widthPct = aspect >= 1.0f ? 100.0f : 100.0f * aspect;
	const float heightPct = aspect >= 1.0f ? 100.0f / aspect : 100.0f;
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", widthPct);
	SetProperty("width", value);
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", heightPct);
	SetProperty("height", value);
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", (100.0f - widthPct) * 0.5f);
	SetProperty("margin-left", value);
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.3f%%", (100.0f - heightPct) * 0.5f);
	SetProperty("margin-top", value);
}

//-------------------------------------------------------------------------------------------------
static std::map<Rml::String, VideoBuffer *> &videoSources()
{
	static std::map<Rml::String, VideoBuffer *> s_sources;
	return s_sources;
}

void RmlVideoElement::setSource(const Rml::String &name, VideoBuffer *buffer)
{
	if (buffer)
		videoSources()[name] = buffer;
	else
		videoSources().erase(name);
}

RmlVideoElement::RmlVideoElement(const Rml::String &tag) : Rml::Element(tag)
{
}

RmlVideoElement::~RmlVideoElement()
{
	bind(nullptr);
}

void RmlVideoElement::OnAttributeChange(const Rml::ElementAttributes &changed_attributes)
{
	Rml::Element::OnAttributeChange(changed_attributes);
	if (changed_attributes.find("fit") != changed_attributes.end())
		m_geometryDirty = true;
}

void RmlVideoElement::OnPropertyChange(const Rml::PropertyIdSet &changed_properties)
{
	Rml::Element::OnPropertyChange(changed_properties);
	if (changed_properties.Contains(Rml::PropertyId::Opacity))
		m_geometryDirty = true;
}

void RmlVideoElement::OnResize()
{
	Rml::Element::OnResize();
	m_geometryDirty = true;
}

void RmlVideoElement::bind(VideoBuffer *buffer)
{
	m_texture = Rml::CallbackTexture();
	if (m_d3dTexture)
	{
		m_d3dTexture->Release();
		m_d3dTexture = nullptr;
	}
	m_buffer = buffer;
	m_geometryDirty = true;

	if (!buffer)
		return;

	TextureClass *texture = static_cast<W3DVideoBuffer *>(buffer)->texture();
	IDirect3DTexture8 *d3dTexture = texture ? texture->Peek_D3D_Texture() : nullptr;
	Rml::RenderManager *renderManager = GetRenderManager();
	if (!d3dTexture || !renderManager || buffer->textureWidth() == 0 || buffer->textureHeight() == 0)
		return;

	m_d3dTexture = d3dTexture;

	m_d3dTexture->AddRef();
	m_textureSize = Rml::Vector2i((int)buffer->textureWidth(), (int)buffer->textureHeight());
	m_videoSize = Rml::Vector2f((float)buffer->width(), (float)buffer->height());

	const Rml::Vector2i textureSize = m_textureSize;
	m_texture = renderManager->MakeCallbackTexture([d3dTexture, textureSize](const Rml::CallbackTextureInterface &textureInterface) -> bool {
		RmlUiRenderInterface *renderInterface = static_cast<RmlUiRenderInterface *>(Rml::GetRenderInterface());
		Rml::TextureHandle handle = renderInterface ? renderInterface->registerVideoTexture(d3dTexture) : 0;
		if (!handle)
			return false;
		textureInterface.SetTextureHandle(handle, textureSize);
		return true;
	});
}

void RmlVideoElement::buildGeometry()
{
	m_geometryDirty = false;

	const Rml::Vector2f box = GetBox().GetSize(Rml::BoxArea::Content);
	if (box.x <= 0.0f || box.y <= 0.0f || m_videoSize.x <= 0.0f || m_videoSize.y <= 0.0f)
	{
		m_geometry = Rml::Geometry();
		return;
	}

	// The movie fills only the top left of its power-of-two texture; stop half a texel short of the
	// unused part so filtering never pulls it in.
	Rml::Vector2f uvMax(1.0f, 1.0f);
	if (m_videoSize.x < m_textureSize.x)
		uvMax.x = (m_videoSize.x - 0.5f) / m_textureSize.x;
	if (m_videoSize.y < m_textureSize.y)
		uvMax.y = (m_videoSize.y - 0.5f) / m_textureSize.y;
	Rml::Vector2f uvMin(0.0f, 0.0f);

	Rml::Vector2f origin(0.0f, 0.0f);
	Rml::Vector2f size = box;
	const float videoAspect = m_videoSize.x / m_videoSize.y;
	const float boxAspect = box.x / box.y;
	const Rml::String fit = GetAttribute<Rml::String>("fit", "fill");
	if (fit == "contain")
	{
		if (boxAspect > videoAspect)
			size.x = box.y * videoAspect;
		else
			size.y = box.x / videoAspect;
		origin = (box - size) * 0.5f;
	}
	else if (fit == "cover")
	{
		if (boxAspect > videoAspect)
		{
			const float shown = videoAspect / boxAspect;
			uvMin.y = uvMax.y * (1.0f - shown) * 0.5f;
			uvMax.y = uvMax.y * (1.0f + shown) * 0.5f;
		}
		else
		{
			const float shown = boxAspect / videoAspect;
			uvMin.x = uvMax.x * (1.0f - shown) * 0.5f;
			uvMax.x = uvMax.x * (1.0f + shown) * 0.5f;
		}
	}

	const Rml::ColourbPremultiplied colour = Rml::Colourb(255, 255, 255, 255).ToPremultiplied(GetComputedValues().opacity());
	Rml::Mesh mesh;
	Rml::MeshUtilities::GenerateQuad(mesh, origin, size, colour, uvMin, uvMax);
	m_geometry = GetRenderManager()->MakeGeometry(std::move(mesh));
}

void RmlVideoElement::OnRender()
{
	Rml::Element::OnRender();

	std::map<Rml::String, VideoBuffer *>::const_iterator it = videoSources().find(GetAttribute<Rml::String>("source", ""));
	VideoBuffer *buffer = it != videoSources().end() ? it->second : nullptr;
	if (buffer != m_buffer)
		bind(buffer);
	if (!m_d3dTexture)
		return;

	if (m_geometryDirty)
		buildGeometry();
	if (m_geometry)
		m_geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content), m_texture);
}
