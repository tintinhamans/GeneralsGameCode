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

#include "W3DDevice/GameClient/RmlUi/RmlUiRenderInterface.h"

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GlobalData.h"
#include "GameClient/Image.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"

// stb_image is header-only; this is the one translation unit that instantiates its
// implementation for the whole GeneralsMD device library (PNG/JPG decode for Data/UI images).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Vertex format for 2D UI geometry, transformed by an orthographic projection (see
// setOrthoProjection). Using transformed (non-RHW) vertices lets RenderGeometry's
// per-draw translation ride on D3DTS_WORLD instead of touching vertex buffers.
struct RmlDxVertex
{
	float x, y, z;
	DWORD color;
	float u, v;
};
static const DWORD kRmlFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

RmlUiRenderInterface::RmlUiRenderInterface() {}

RmlUiRenderInterface::~RmlUiRenderInterface()
{
	onDeviceLost();
}

void RmlUiRenderInterface::onDeviceCreated()
{
	// Nothing to pre-allocate; geometry/textures are created on demand.
}

void RmlUiRenderInterface::onDeviceLost()
{
	for (GeometryMap::iterator it = m_geometry.begin(); it != m_geometry.end(); ++it)
	{
		if (it->second.vb) it->second.vb->Release();
		if (it->second.ib) it->second.ib->Release();
	}
	m_geometry.clear();

	for (TextureMap::iterator it = m_textures.begin(); it != m_textures.end(); ++it)
	{
		if (it->second.ownsRelease && it->second.d3dTexture)
			it->second.d3dTexture->Release();
	}
	m_textures.clear();
}

//-------------------------------------------------------------------------------------------------
Rml::CompiledGeometryHandle RmlUiRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev || vertices.empty() || indices.empty())
		return 0;

	CompiledGeometry geom;
	geom.numVertices = (int)vertices.size();
	geom.numIndices = (int)indices.size();

	if (FAILED(dev->CreateVertexBuffer(geom.numVertices * sizeof(RmlDxVertex), D3DUSAGE_WRITEONLY, kRmlFvf, D3DPOOL_MANAGED, &geom.vb)))
		return 0;

	RmlDxVertex *dst = nullptr;
	if (SUCCEEDED(geom.vb->Lock(0, 0, (BYTE **)&dst, 0)))
	{
		for (int i = 0; i < geom.numVertices; ++i)
		{
			const Rml::Vertex &v = vertices[i];
			dst[i].x = v.position.x;
			dst[i].y = v.position.y;
			dst[i].z = 0.0f;
			// D3DCOLOR is ARGB; RmlUi vertex colour is premultiplied RGBA.
			dst[i].color = D3DCOLOR_ARGB(v.colour.alpha, v.colour.red, v.colour.green, v.colour.blue);
			dst[i].u = v.tex_coord.x;
			dst[i].v = v.tex_coord.y;
		}
		geom.vb->Unlock();
	}

	if (FAILED(dev->CreateIndexBuffer(geom.numIndices * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &geom.ib)))
	{
		geom.vb->Release();
		return 0;
	}

	WORD *idst = nullptr;
	if (SUCCEEDED(geom.ib->Lock(0, 0, (BYTE **)&idst, 0)))
	{
		for (int i = 0; i < geom.numIndices; ++i)
			idst[i] = (WORD)indices[i];
		geom.ib->Unlock();
	}

	Rml::CompiledGeometryHandle handle = m_nextGeometryHandle++;
	m_geometry[handle] = geom;
	return handle;
}

void RmlUiRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	GeometryMap::iterator it = m_geometry.find(geometry);
	if (it == m_geometry.end())
		return;
	if (it->second.vb) it->second.vb->Release();
	if (it->second.ib) it->second.ib->Release();
	m_geometry.erase(it);
}

// Builds a D3D8 orthographic projection (left-handed, off-center) mapping pixel x in
// [left,right] to NDC [-1,1] and pixel y in [top,bottom] to NDC [+1,-1] (D3D's viewport
// mapping flips y back for us). Passing the *scissor rect's* absolute pixel bounds here,
// together with a viewport shrunk to that same rect, reproduces DX8 scissoring: content
// outside the rect is clipped by the viewport, while everything inside keeps its original
// absolute screen position (this is the "compensate the projection" trick DX8 needs since
// it has no native scissor rect).
void RmlUiRenderInterface::setOrthoProjection(int left, int right, int top, int bottom)
{
	const float l = (float)left, r = (float)right, t = (float)top, b = (float)bottom;
	D3DMATRIX m;
	ZeroMemory(&m, sizeof(m));
	m._11 = 2.0f / (r - l);
	m._22 = 2.0f / (t - b);
	m._33 = 1.0f;
	m._41 = (l + r) / (l - r);
	m._42 = (t + b) / (b - t);
	m._43 = 0.0f;
	m._44 = 1.0f;
	DX8Wrapper::_Get_D3D_Device8()->SetTransform(D3DTS_PROJECTION, &m);
}

void RmlUiRenderInterface::beginFrame(int contextWidth, int contextHeight)
{
	m_contextWidth = contextWidth;
	m_contextHeight = contextHeight;
	m_scissorEnabled = false;

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();

	dev->GetViewport(&m_saved.viewport);
	dev->GetRenderState(D3DRS_LIGHTING, &m_saved.lighting);
	dev->GetRenderState(D3DRS_ZENABLE, &m_saved.zEnable);
	dev->GetRenderState(D3DRS_CULLMODE, &m_saved.cullMode);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_saved.alphaBlend);
	dev->GetRenderState(D3DRS_SRCBLEND, &m_saved.srcBlend);
	dev->GetRenderState(D3DRS_DESTBLEND, &m_saved.destBlend);
	dev->GetRenderState(D3DRS_ALPHATESTENABLE, &m_saved.alphaTest);
	dev->GetRenderState(D3DRS_FOGENABLE, &m_saved.fogEnable);
	dev->GetRenderState(D3DRS_STENCILENABLE, &m_saved.stencilEnable);

	dev->GetTextureStageState(0, D3DTSS_COLOROP, &m_saved.colorOp0);
	dev->GetTextureStageState(0, D3DTSS_COLORARG1, &m_saved.colorArg1_0);
	dev->GetTextureStageState(0, D3DTSS_COLORARG2, &m_saved.colorArg2_0);
	dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &m_saved.alphaOp0);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &m_saved.alphaArg1_0);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG2, &m_saved.alphaArg2_0);
	dev->GetTextureStageState(0, D3DTSS_MAGFILTER, &m_saved.magFilter0);
	dev->GetTextureStageState(0, D3DTSS_MINFILTER, &m_saved.minFilter0);
	dev->GetTextureStageState(0, D3DTSS_ADDRESSU, &m_saved.addressU0);
	dev->GetTextureStageState(0, D3DTSS_ADDRESSV, &m_saved.addressV0);

	dev->GetTexture(0, &m_saved.texture0); // AddRef'd by D3D

	dev->GetTransform(D3DTS_WORLD, &m_saved.world);
	dev->GetTransform(D3DTS_VIEW, &m_saved.view);
	dev->GetTransform(D3DTS_PROJECTION, &m_saved.projection);

	dev->GetStreamSource(0, &m_saved.streamVb, &m_saved.streamStride); // AddRef'd
	dev->GetIndices(&m_saved.indexBuffer, &m_saved.baseVertexIndex); // AddRef'd; D3D8 writes both outputs
	dev->GetVertexShader(&m_saved.fvf);

	// Set up the fixed-function pipeline for premultiplied-alpha 2D UI rendering.
	dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	// Premultiplied alpha: dst = src*1 + dst*(1-srcAlpha)
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	dev->SetVertexShader(kRmlFvf);

	D3DMATRIX identity;
	ZeroMemory(&identity, sizeof(identity));
	identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
	dev->SetTransform(D3DTS_WORLD, &identity);
	dev->SetTransform(D3DTS_VIEW, &identity);
	setOrthoProjection(0, contextWidth, 0, contextHeight);

	D3DVIEWPORT8 vp = { 0, 0, (DWORD)contextWidth, (DWORD)contextHeight, 0.0f, 1.0f };
	dev->SetViewport(&vp);
}

void RmlUiRenderInterface::endFrame()
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();

	dev->SetViewport(&m_saved.viewport);
	dev->SetRenderState(D3DRS_LIGHTING, m_saved.lighting);
	dev->SetRenderState(D3DRS_ZENABLE, m_saved.zEnable);
	dev->SetRenderState(D3DRS_CULLMODE, m_saved.cullMode);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, m_saved.alphaBlend);
	dev->SetRenderState(D3DRS_SRCBLEND, m_saved.srcBlend);
	dev->SetRenderState(D3DRS_DESTBLEND, m_saved.destBlend);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, m_saved.alphaTest);
	dev->SetRenderState(D3DRS_FOGENABLE, m_saved.fogEnable);
	dev->SetRenderState(D3DRS_STENCILENABLE, m_saved.stencilEnable);

	dev->SetTextureStageState(0, D3DTSS_COLOROP, m_saved.colorOp0);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, m_saved.colorArg1_0);
	dev->SetTextureStageState(0, D3DTSS_COLORARG2, m_saved.colorArg2_0);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, m_saved.alphaOp0);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, m_saved.alphaArg1_0);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, m_saved.alphaArg2_0);
	dev->SetTextureStageState(0, D3DTSS_MAGFILTER, m_saved.magFilter0);
	dev->SetTextureStageState(0, D3DTSS_MINFILTER, m_saved.minFilter0);
	dev->SetTextureStageState(0, D3DTSS_ADDRESSU, m_saved.addressU0);
	dev->SetTextureStageState(0, D3DTSS_ADDRESSV, m_saved.addressV0);

	dev->SetTexture(0, m_saved.texture0);
	if (m_saved.texture0) m_saved.texture0->Release(); // drop the ref GetTexture added

	dev->SetTransform(D3DTS_WORLD, &m_saved.world);
	dev->SetTransform(D3DTS_VIEW, &m_saved.view);
	dev->SetTransform(D3DTS_PROJECTION, &m_saved.projection);

	dev->SetStreamSource(0, m_saved.streamVb, m_saved.streamStride);
	if (m_saved.streamVb) m_saved.streamVb->Release();
	dev->SetIndices(m_saved.indexBuffer, m_saved.baseVertexIndex);
	if (m_saved.indexBuffer) m_saved.indexBuffer->Release();
	dev->SetVertexShader(m_saved.fvf);
}

void RmlUiRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	GeometryMap::iterator git = m_geometry.find(geometry);
	if (git == m_geometry.end())
		return;

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();

	// Translation rides on D3DTS_WORLD; beginFrame() already set up the projection/viewport
	// (full context, or the scissor rect via SetScissorRegion) and every other piece of state.
	D3DMATRIX world;
	ZeroMemory(&world, sizeof(world));
	world._11 = world._22 = world._33 = world._44 = 1.0f;
	world._41 = translation.x;
	world._42 = translation.y;
	dev->SetTransform(D3DTS_WORLD, &world);

	if (texture != 0)
	{
		TextureMap::iterator tit = m_textures.find(texture);
		dev->SetTexture(0, tit != m_textures.end() ? tit->second.d3dTexture : nullptr);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}
	else
	{
		dev->SetTexture(0, nullptr);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	}

	CompiledGeometry &geom = git->second;
	dev->SetStreamSource(0, geom.vb, sizeof(RmlDxVertex));
	dev->SetIndices(geom.ib, 0);
	dev->SetVertexShader(kRmlFvf);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, geom.numVertices, 0, geom.numIndices / 3);
}

//-------------------------------------------------------------------------------------------------
Rml::TextureHandle RmlUiRenderInterface::registerTexture(IDirect3DTexture8 *tex, bool ownsRelease)
{
	if (!tex)
		return 0;
	Rml::TextureHandle handle = m_nextTextureHandle++;
	LoadedTexture lt;
	lt.d3dTexture = tex;
	lt.ownsRelease = ownsRelease;
	m_textures[handle] = lt;
	return handle;
}

void RmlUiRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	TextureMap::iterator it = m_textures.find(texture);
	if (it == m_textures.end())
		return;
	if (it->second.ownsRelease && it->second.d3dTexture)
		it->second.d3dTexture->Release();
	m_textures.erase(it);
}

Rml::TextureHandle RmlUiRenderInterface::LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source)
{
	// "mapped:Name" resolves an INI MappedImage (e.g. the Generals logo) to its atlas texture.
	if (source.rfind("mapped:", 0) == 0)
		return loadMappedTexture(texture_dimensions, source.substr(7));

	Rml::String ext;
	size_t dot = source.find_last_of('.');
	if (dot != Rml::String::npos)
		ext = source.substr(dot + 1);
	for (auto &c : ext) c = (char)tolower((unsigned char)c);

	if (ext == "dds" || ext == "tga")
		return loadEngineTexture(texture_dimensions, source);

	return loadStbTexture(texture_dimensions, source);
}

Rml::TextureHandle RmlUiRenderInterface::loadEngineTexture(Rml::Vector2i &dimensions, const Rml::String &path)
{
	// Route .dds/.tga through WW3D's own TextureClass loader, which already resolves
	// through TheFileSystem (embedded Data/ archive, loose overrides, and Art/Textures
	// inside the game's .big archives). We take our own AddRef'd D3D texture and let the
	// engine keep owning/caching the TextureClass itself.
	TextureClass *tex = WW3DAssetManager::Get_Instance() ? WW3DAssetManager::Get_Instance()->Get_Texture(path.c_str()) : nullptr;
	if (!tex)
		return 0;

	IDirect3DTexture8 *d3dTex = tex->Peek_D3D_Texture();
	if (!d3dTex)
	{
		tex->Release_Ref();
		return 0;
	}

	d3dTex->AddRef();
	dimensions.x = tex->Get_Width();
	dimensions.y = tex->Get_Height();
	tex->Release_Ref(); // drop the WW3DAssetManager ref we took; the D3D texture keeps its own refcount

	return registerTexture(d3dTex, true);
}

Rml::TextureHandle RmlUiRenderInterface::loadMappedTexture(Rml::Vector2i &dimensions, const Rml::String &mappedName)
{
	// Phase 2 TODO: expose the Image's UV sub-rect (atlas cropping) through an RmlUi
	// decorator/sprite so <img src="mapped:Name"> shows only its sub-region instead of
	// the whole atlas page. For now we just load the underlying texture page.
	if (!TheMappedImageCollection)
		return 0;

	const Image *image = TheMappedImageCollection->findImageByName(AsciiString(mappedName.c_str()));
	if (!image)
		return 0;

	return loadEngineTexture(dimensions, Rml::String(image->getFilename().str()));
}

Rml::TextureHandle RmlUiRenderInterface::loadStbTexture(Rml::Vector2i &dimensions, const Rml::String &path)
{
	File *file = TheFileSystem ? TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY) : nullptr;
	if (!file)
		return 0;

	std::vector<unsigned char> bytes;
	bytes.resize((size_t)file->size());
	if (!bytes.empty())
		file->read(&bytes[0], (Int)bytes.size());
	file->close();

	int w = 0, h = 0, channels = 0;
	unsigned char *pixels = stbi_load_from_memory(bytes.empty() ? nullptr : &bytes[0], (int)bytes.size(), &w, &h, &channels, 4);
	if (!pixels)
		return 0;

	// stb gives straight (non-premultiplied) alpha; RmlUi's GenerateTexture path expects
	// premultiplied, so premultiply here for consistency with the rest of the pipeline.
	for (int i = 0; i < w * h; ++i)
	{
		unsigned char a = pixels[i * 4 + 3];
		pixels[i * 4 + 0] = (unsigned char)((pixels[i * 4 + 0] * a) / 255);
		pixels[i * 4 + 1] = (unsigned char)((pixels[i * 4 + 1] * a) / 255);
		pixels[i * 4 + 2] = (unsigned char)((pixels[i * 4 + 2] * a) / 255);
	}

	Rml::Vector2i dims(w, h);
	Rml::TextureHandle handle = GenerateTexture(Rml::Span<const Rml::byte>((const Rml::byte *)pixels, (size_t)(w * h * 4)), dims);
	stbi_image_free(pixels);
	if (handle != 0)
		dimensions = dims;
	return handle;
}

Rml::TextureHandle RmlUiRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev || source.empty())
		return 0;

	IDirect3DTexture8 *tex = nullptr;
	if (FAILED(dev->CreateTexture(source_dimensions.x, source_dimensions.y, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)))
		return 0;

	D3DLOCKED_RECT rect;
	if (SUCCEEDED(tex->LockRect(0, &rect, nullptr, 0)))
	{
		const unsigned char *src = (const unsigned char *)source.data();
		for (int y = 0; y < source_dimensions.y; ++y)
		{
			unsigned char *dstRow = (unsigned char *)rect.pBits + y * rect.Pitch;
			for (int x = 0; x < source_dimensions.x; ++x)
			{
				const unsigned char *p = src + (y * source_dimensions.x + x) * 4;
				DWORD *out = (DWORD *)(dstRow + x * 4);
				// source is RGBA premultiplied; D3DFMT_A8R8G8B8 is ARGB.
				*out = D3DCOLOR_ARGB(p[3], p[0], p[1], p[2]);
			}
		}
		tex->UnlockRect(0);
	}

	return registerTexture(tex, true);
}

//-------------------------------------------------------------------------------------------------
// DX8 has no native scissor rect. We emulate it by shrinking the viewport to the scissor
// region (D3D clips rasterization to the viewport for free) and rebuilding the orthographic
// projection using the scissor rect's *absolute* pixel bounds instead of the full context
// size. Since the projection maps those absolute bounds to the full NDC range, and the
// viewport then maps NDC back onto exactly that same sub-rect of the screen, geometry lands
// at its original position while everything outside the rect is clipped. Disabling restores
// the full-context viewport/projection set up in beginFrame().
void RmlUiRenderInterface::EnableScissorRegion(bool enable)
{
	m_scissorEnabled = enable;
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev)
		return;

	if (!enable)
	{
		D3DVIEWPORT8 vp = { 0, 0, (DWORD)m_contextWidth, (DWORD)m_contextHeight, 0.0f, 1.0f };
		dev->SetViewport(&vp);
		setOrthoProjection(0, m_contextWidth, 0, m_contextHeight);
	}
}

void RmlUiRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	m_scissorRegion = region;
	if (!m_scissorEnabled)
		return;

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev)
		return;

	int left = region.Left() < 0 ? 0 : region.Left();
	int top = region.Top() < 0 ? 0 : region.Top();
	int width = region.Width() < 1 ? 1 : region.Width();
	int height = region.Height() < 1 ? 1 : region.Height();

	D3DVIEWPORT8 vp = { (DWORD)left, (DWORD)top, (DWORD)width, (DWORD)height, 0.0f, 1.0f };
	dev->SetViewport(&vp);
	setOrthoProjection(left, left + width, top, top + height);
}
