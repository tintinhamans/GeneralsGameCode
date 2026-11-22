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

// Ronin @feature 28/10/2026 DX9: RmlUi's renderer on the D3D9 device. Ported from tintinhamans' GeneralsGameCode fork
// (arctic/go/feat/rmlui): DX8's viewport scissor became D3D9's scissor rect, and the projection takes D3D9's half pixel.
#include "W3DDevice/GameClient/RmlUi/RmlUiRenderInterface.h"

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameClient/Display.h"
#include "GameClient/Image.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"
#include "WW3D2/assetmgr.h"

#include <ctype.h>
#include <string.h>
#include <vector>

// stb_image's one implementation in the game, for PNG and JPG pictures in Data/UI.
#pragma warning(push)
#pragma warning(disable : 4189 4101 4706 4244 4456 4457)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>
#pragma warning(pop)

namespace
{
	struct RmlDxVertex
	{
		float x, y, z;
		DWORD color;
		float u, v;
	};
	const DWORD kRmlFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

	// Each state the UI draws set, with its value; beginFrame reads them first and endFrame puts them back.
	const D3DRENDERSTATETYPE kRenderStates[] = {
		D3DRS_LIGHTING, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_SHADEMODE,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_SPECULARENABLE, D3DRS_SCISSORTESTENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_COLORVERTEX };
	const DWORD kRenderValues[] = {
		FALSE, D3DZB_FALSE, FALSE, D3DCULL_NONE, D3DFILL_SOLID, D3DSHADE_GOURAUD,
		TRUE, D3DBLEND_ONE, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD, FALSE,	// premultiplied: src + dst x (1 - src alpha)
		FALSE, FALSE, FALSE, FALSE, FALSE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA, FALSE, 0, TRUE };
	const int kRenderCount = sizeof(kRenderStates) / sizeof(kRenderStates[0]);
	static_assert(sizeof(kRenderValues) / sizeof(kRenderValues[0]) == sizeof(kRenderStates) / sizeof(kRenderStates[0]), "one value per state");

	const D3DTEXTURESTAGESTATETYPE kStageStates[] = {
		D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
		D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTSS_RESULTARG };
	const int kStageCount = sizeof(kStageStates) / sizeof(kStageStates[0]);
	const int kStages = 3;

	const D3DSAMPLERSTATETYPE kSamplerStates[] = {
		D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE };
	const DWORD kSamplerValues[] = { D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, FALSE };
	const int kSamplerCount = sizeof(kSamplerStates) / sizeof(kSamplerStates[0]);
	const int kSamplers = 2;

	struct SavedState
	{
		D3DVIEWPORT9 viewport;
		RECT scissor;
		DWORD render[kRenderCount];
		DWORD stage[kStages][kStageCount];
		DWORD sampler[kSamplers][kSamplerCount];
		IDirect3DBaseTexture9 *texture[kSamplers];
		D3DMATRIX world, view, projection;
		IDirect3DVertexBuffer9 *stream;
		UINT streamOffset, streamStride;
		IDirect3DIndexBuffer9 *indices;
		IDirect3DVertexDeclaration9 *declaration;
		DWORD fvf;
		IDirect3DVertexShader9 *vertexShader;
		IDirect3DPixelShader9 *pixelShader;
	};
	SavedState s_saved;

	void identity(D3DMATRIX &m)
	{
		ZeroMemory(&m, sizeof(m));
		m._11 = m._22 = m._33 = m._44 = 1.0f;
	}

	template <class T> void releaseRef(T *&p)
	{
		if (p)
			p->Release();
		p = nullptr;
	}
}

RmlUiRenderInterface::RmlUiRenderInterface()
	: m_nextGeometry(1), m_nextTexture(1), m_inFrame(false), m_width(0), m_height(0), m_scissorEmpty(false), m_hasTransform(false)
{
	ZeroMemory(m_transform, sizeof(m_transform));
}

RmlUiRenderInterface::~RmlUiRenderInterface()
{
	releaseAll();
}

void RmlUiRenderInterface::releaseAll()
{
	for (std::map<Rml::CompiledGeometryHandle, CompiledGeometry>::iterator it = m_geometry.begin(); it != m_geometry.end(); ++it)
	{
		releaseRef(it->second.vb);
		releaseRef(it->second.ib);
	}
	m_geometry.clear();
	for (std::map<Rml::TextureHandle, LoadedTexture>::iterator it = m_textures.begin(); it != m_textures.end(); ++it)
	{
		releaseRef(it->second.texture);
		REF_PTR_RELEASE(it->second.engine);	// Ronin @feature 08/11/2026 DX9Ex
	}
	m_textures.clear();
}

//-------------------------------------------------------------------------------------------------
Rml::CompiledGeometryHandle RmlUiRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev || vertices.empty() || indices.empty())
		return 0;

	CompiledGeometry geom = { nullptr, nullptr, (int)vertices.size(), (int)indices.size() };
	const bool wide = geom.numVertices > 0xFFFF;

	// Ronin @feature 08/11/2026 DX9Ex: these buffers and textures take the default pool on 9Ex (no managed pool there).
	if (FAILED(dev->CreateVertexBuffer(geom.numVertices * sizeof(RmlDxVertex), D3DUSAGE_WRITEONLY, kRmlFvf,
			DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, nullptr), &geom.vb, nullptr)))
		return 0;
	RmlDxVertex *dst = nullptr;
	if (SUCCEEDED(geom.vb->Lock(0, 0, (void **)&dst, 0)))
	{
		for (int i = 0; i < geom.numVertices; ++i)
		{
			const Rml::Vertex &v = vertices[i];
			dst[i].x = v.position.x;
			dst[i].y = v.position.y;
			dst[i].z = 0.0f;
			dst[i].color = D3DCOLOR_ARGB(v.colour.alpha, v.colour.red, v.colour.green, v.colour.blue);	// premultiplied
			dst[i].u = v.tex_coord.x;
			dst[i].v = v.tex_coord.y;
		}
		geom.vb->Unlock();
	}

	const UINT indexSize = wide ? sizeof(DWORD) : sizeof(WORD);
	if (FAILED(dev->CreateIndexBuffer(geom.numIndices * indexSize, D3DUSAGE_WRITEONLY, wide ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
			DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, nullptr), &geom.ib, nullptr)))
	{
		releaseRef(geom.vb);
		return 0;
	}
	void *idst = nullptr;
	if (SUCCEEDED(geom.ib->Lock(0, 0, &idst, 0)))
	{
		for (int i = 0; i < geom.numIndices; ++i)
		{
			if (wide)
				((DWORD *)idst)[i] = (DWORD)indices[i];
			else
				((WORD *)idst)[i] = (WORD)indices[i];
		}
		geom.ib->Unlock();
	}

	const Rml::CompiledGeometryHandle handle = m_nextGeometry++;
	m_geometry[handle] = geom;
	return handle;
}

void RmlUiRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	std::map<Rml::CompiledGeometryHandle, CompiledGeometry>::iterator it = m_geometry.find(geometry);
	if (it == m_geometry.end())
		return;
	releaseRef(it->second.vb);
	releaseRef(it->second.ib);
	m_geometry.erase(it);
}

//-------------------------------------------------------------------------------------------------
void RmlUiRenderInterface::beginFrame(int width, int height)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	m_inFrame = dev != nullptr && width > 0 && height > 0;
	if (!m_inFrame)
		return;
	m_width = width;
	m_height = height;
	m_scissorEmpty = false;
	m_hasTransform = false;

	dev->GetViewport(&s_saved.viewport);
	dev->GetScissorRect(&s_saved.scissor);
	for (int i = 0; i < kRenderCount; ++i)
		dev->GetRenderState(kRenderStates[i], &s_saved.render[i]);
	for (int s = 0; s < kStages; ++s)
		for (int i = 0; i < kStageCount; ++i)
			dev->GetTextureStageState(s, kStageStates[i], &s_saved.stage[s][i]);
	for (int s = 0; s < kSamplers; ++s)
	{
		for (int i = 0; i < kSamplerCount; ++i)
			dev->GetSamplerState(s, kSamplerStates[i], &s_saved.sampler[s][i]);
		dev->GetTexture(s, &s_saved.texture[s]);	// AddRef'd
	}
	dev->GetTransform(D3DTS_WORLD, &s_saved.world);
	dev->GetTransform(D3DTS_VIEW, &s_saved.view);
	dev->GetTransform(D3DTS_PROJECTION, &s_saved.projection);
	dev->GetStreamSource(0, &s_saved.stream, &s_saved.streamOffset, &s_saved.streamStride);	// AddRef'd
	dev->GetIndices(&s_saved.indices);								// AddRef'd
	dev->GetVertexDeclaration(&s_saved.declaration);				// AddRef'd
	dev->GetFVF(&s_saved.fvf);
	dev->GetVertexShader(&s_saved.vertexShader);					// AddRef'd
	dev->GetPixelShader(&s_saved.pixelShader);						// AddRef'd

	dev->SetVertexShader(nullptr);
	dev->SetPixelShader(nullptr);
	dev->SetFVF(kRmlFvf);
	for (int i = 0; i < kRenderCount; ++i)
		dev->SetRenderState(kRenderStates[i], kRenderValues[i]);
	for (int s = 0; s < kStages; ++s)
	{
		dev->SetTextureStageState(s, D3DTSS_COLOROP, D3DTOP_DISABLE);
		dev->SetTextureStageState(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		dev->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, 0);
		dev->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		dev->SetTextureStageState(s, D3DTSS_RESULTARG, D3DTA_CURRENT);
	}
	for (int s = 0; s < kSamplers; ++s)
	{
		for (int i = 0; i < kSamplerCount; ++i)
			dev->SetSamplerState(s, kSamplerStates[i], kSamplerValues[i]);
		dev->SetTexture(s, nullptr);
	}

	// Pixels to clip space, half a pixel up and left: D3D9 samples at pixel centres, so texels land on pixels and text stays sharp.
	D3DMATRIX m;
	identity(m);
	dev->SetTransform(D3DTS_WORLD, &m);
	dev->SetTransform(D3DTS_VIEW, &m);
	ZeroMemory(&m, sizeof(m));
	m._11 = 2.0f / width;
	m._22 = -2.0f / height;
	m._33 = 1.0f / 20000.0f;	// z = 0 lands mid range; a 3D transform keeps within +-10000
	m._41 = -1.0f - 1.0f / width;
	m._42 = 1.0f + 1.0f / height;
	m._43 = 0.5f;
	m._44 = 1.0f;
	dev->SetTransform(D3DTS_PROJECTION, &m);

	D3DVIEWPORT9 vp = { 0, 0, (DWORD)width, (DWORD)height, 0.0f, 1.0f };
	dev->SetViewport(&vp);
}

void RmlUiRenderInterface::endFrame()
{
	if (!m_inFrame)
		return;
	m_inFrame = false;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev)
		return;

	dev->SetViewport(&s_saved.viewport);
	dev->SetScissorRect(&s_saved.scissor);
	for (int i = 0; i < kRenderCount; ++i)
		dev->SetRenderState(kRenderStates[i], s_saved.render[i]);
	for (int s = 0; s < kStages; ++s)
		for (int i = 0; i < kStageCount; ++i)
			dev->SetTextureStageState(s, kStageStates[i], s_saved.stage[s][i]);
	for (int s = 0; s < kSamplers; ++s)
	{
		for (int i = 0; i < kSamplerCount; ++i)
			dev->SetSamplerState(s, kSamplerStates[i], s_saved.sampler[s][i]);
		dev->SetTexture(s, s_saved.texture[s]);
		releaseRef(s_saved.texture[s]);
	}
	dev->SetTransform(D3DTS_WORLD, &s_saved.world);
	dev->SetTransform(D3DTS_VIEW, &s_saved.view);
	dev->SetTransform(D3DTS_PROJECTION, &s_saved.projection);
	dev->SetStreamSource(0, s_saved.stream, s_saved.streamOffset, s_saved.streamStride);
	releaseRef(s_saved.stream);
	dev->SetIndices(s_saved.indices);
	releaseRef(s_saved.indices);
	// An FVF layout goes back as its FVF, anything else as the declaration itself.
	if (s_saved.fvf != 0)
		dev->SetFVF(s_saved.fvf);
	else
		dev->SetVertexDeclaration(s_saved.declaration);
	releaseRef(s_saved.declaration);
	dev->SetVertexShader(s_saved.vertexShader);
	releaseRef(s_saved.vertexShader);
	dev->SetPixelShader(s_saved.pixelShader);
	releaseRef(s_saved.pixelShader);
}

void RmlUiRenderInterface::drawEngine(void (*draw)(void *), void *context)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!m_inFrame || !dev || !draw)
		return;
	DWORD scissorOn = FALSE;
	RECT scissor;
	dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorOn);
	dev->GetScissorRect(&scissor);
	const bool scissorEmpty = m_scissorEmpty;
	const bool hasTransform = m_hasTransform;
	float transform[4][4];
	memcpy(transform, m_transform, sizeof(transform));

	endFrame();
	draw(context);
	// Ronin @bugfix 22/11/2026 DX9: RmlUi renders inside the display's 2D batch; the draw's last lines (the radar's camera box)
	// stayed queued and painted over every later document. Flush them here, in the document's order.
	if (TheDisplay)
		TheDisplay->flush();
	beginFrame(m_width, m_height);

	m_scissorEmpty = scissorEmpty;
	m_hasTransform = hasTransform;
	memcpy(m_transform, transform, sizeof(m_transform));
	dev->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorOn);
	dev->SetScissorRect(&scissor);
}

void RmlUiRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	std::map<Rml::CompiledGeometryHandle, CompiledGeometry>::iterator git = m_geometry.find(geometry);
	if (!m_inFrame || git == m_geometry.end() || m_scissorEmpty)
		return;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();

	// The translation, then the element's transform (row vectors: v x translate(t) x M).
	D3DMATRIX world;
	if (m_hasTransform)
	{
		memcpy(world.m, m_transform, sizeof(world.m));
		for (int c = 0; c < 4; ++c)
			world.m[3][c] = translation.x * m_transform[0][c] + translation.y * m_transform[1][c] + m_transform[3][c];
	}
	else
	{
		identity(world);
		world._41 = translation.x;
		world._42 = translation.y;
	}
	dev->SetTransform(D3DTS_WORLD, &world);

	std::map<Rml::TextureHandle, LoadedTexture>::iterator tit = m_textures.find(texture);
	if (texture != 0 && tit != m_textures.end())
	{
		IDirect3DTexture9 *tex = tit->second.texture;
		if (tit->second.engine != nullptr)	// Ronin @feature 08/11/2026 DX9Ex: a reset invalidates engine textures; Init reloads
		{
			if (!tit->second.engine->Is_Initialized())
				tit->second.engine->Init();
			tex = tit->second.engine->Peek_D3D_Texture();
		}
		dev->SetTexture(0, tex);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		if (tit->second.premultiplied)
		{
			dev->SetTexture(1, nullptr);
			dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
		else
		{
			// A straight-alpha page: the second stage multiplies the colour by the texture's own alpha.
			dev->SetTexture(1, tex);
			dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
			dev->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
			dev->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
			dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			dev->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		}
	}
	else
	{
		dev->SetTexture(0, nullptr);
		dev->SetTexture(1, nullptr);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}

	const CompiledGeometry &geom = git->second;
	dev->SetStreamSource(0, geom.vb, 0, sizeof(RmlDxVertex));
	dev->SetIndices(geom.ib);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, geom.numVertices, 0, geom.numIndices / 3);
}

//-------------------------------------------------------------------------------------------------
Rml::TextureHandle RmlUiRenderInterface::registerTexture(IDirect3DTexture9 *texture, bool premultiplied)
{
	if (!texture)
		return 0;
	const Rml::TextureHandle handle = m_nextTexture++;
	LoadedTexture loaded = { texture, premultiplied, nullptr };
	m_textures[handle] = loaded;
	return handle;
}

// Ronin @feature 08/11/2026 DX9Ex: see LoadedTexture::engine.
Rml::TextureHandle RmlUiRenderInterface::registerEngineTexture(TextureClass *texture, bool premultiplied)
{
	if (!texture)
		return 0;
	const Rml::TextureHandle handle = m_nextTexture++;
	LoadedTexture loaded = { nullptr, premultiplied, texture };
	m_textures[handle] = loaded;
	return handle;
}

Rml::TextureHandle RmlUiRenderInterface::registerVideoTexture(TextureClass *texture)
{
	if (!texture)
		return 0;
	texture->Add_Ref();
	return registerEngineTexture(texture, true);
}

void RmlUiRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	std::map<Rml::TextureHandle, LoadedTexture>::iterator it = m_textures.find(texture);
	if (it == m_textures.end())
		return;
	releaseRef(it->second.texture);
	REF_PTR_RELEASE(it->second.engine);	// Ronin @feature 08/11/2026 DX9Ex
	m_textures.erase(it);
}

Rml::TextureHandle RmlUiRenderInterface::LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source)
{
	if (source.rfind("mapped:", 0) == 0)
	{
		const Image *image = TheMappedImageCollection ? TheMappedImageCollection->findImageByName(source.substr(7).c_str()) : nullptr;
		return image ? loadEngineTexture(texture_dimensions, Rml::String(image->getFilename().str())) : 0;
	}

	Rml::String ext;
	const size_t dot = source.find_last_of('.');
	if (dot != Rml::String::npos)
		ext = source.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); ++i)
		ext[i] = (char)tolower((unsigned char)ext[i]);
	if (ext == "dds" || ext == "tga")
		return loadEngineTexture(texture_dimensions, source);
	return loadImageFile(texture_dimensions, source);
}

// Through WW3D's own loader, so Art/Textures in the archives and loose overrides resolve as for any model. We keep a
// reference to the engine's TextureClass (Ronin @feature 08/11/2026 DX9Ex: not to its D3D texture - see LoadedTexture).
Rml::TextureHandle RmlUiRenderInterface::loadEngineTexture(Rml::Vector2i &dimensions, const Rml::String &path)
{
	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	TextureClass *tex = assets ? assets->Get_Texture(path.c_str(), MIP_LEVELS_1) : nullptr;
	if (!tex)
		return 0;
	tex->Init();	// textures load lazily on first use
	if (tex->Peek_D3D_Texture() == nullptr)
	{
		tex->Release_Ref();
		return 0;
	}
	dimensions.x = RmlEngineTextureSpace;
	dimensions.y = RmlEngineTextureSpace;
	return registerEngineTexture(tex, false);
}

Rml::TextureHandle RmlUiRenderInterface::loadImageFile(Rml::Vector2i &dimensions, const Rml::String &path)
{
	File *file = TheFileSystem ? TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY) : nullptr;
	if (!file)
		return 0;
	std::vector<unsigned char> bytes((size_t)file->size());
	if (!bytes.empty())
		file->read(&bytes[0], (Int)bytes.size());
	file->close();
	if (bytes.empty())
		return 0;

	int w = 0, h = 0, channels = 0;
	unsigned char *pixels = stbi_load_from_memory(&bytes[0], (int)bytes.size(), &w, &h, &channels, 4);
	if (!pixels)
		return 0;
	for (int i = 0; i < w * h; ++i)	// stb gives straight alpha; RmlUi works premultiplied
	{
		const unsigned int a = pixels[i * 4 + 3];
		pixels[i * 4 + 0] = (unsigned char)((pixels[i * 4 + 0] * a + 127) / 255);
		pixels[i * 4 + 1] = (unsigned char)((pixels[i * 4 + 1] * a + 127) / 255);
		pixels[i * 4 + 2] = (unsigned char)((pixels[i * 4 + 2] * a + 127) / 255);
	}
	const Rml::Vector2i size(w, h);
	const Rml::TextureHandle handle = GenerateTexture(Rml::Span<const Rml::byte>((const Rml::byte *)pixels, (size_t)w * h * 4), size);
	stbi_image_free(pixels);
	if (handle != 0)
		dimensions = size;
	return handle;
}

// RmlUi's own textures (font glyphs, PNGs): premultiplied RGBA into a managed A8R8G8B8, which outlives a device reset.
Rml::TextureHandle RmlUiRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev || source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0)
		return 0;
	IDirect3DTexture9 *tex = nullptr;
	DWORD texUsage = 0;
	const D3DPOOL texPool = DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, &texUsage);	// Ronin @feature 08/11/2026 DX9Ex
	if (FAILED(dev->CreateTexture(source_dimensions.x, source_dimensions.y, 1, texUsage, D3DFMT_A8R8G8B8, texPool, &tex, nullptr)))
		return 0;
	D3DLOCKED_RECT rect;
	if (SUCCEEDED(tex->LockRect(0, &rect, nullptr, 0)))
	{
		const unsigned char *src = (const unsigned char *)source.data();
		for (int y = 0; y < source_dimensions.y; ++y)
		{
			DWORD *row = (DWORD *)((unsigned char *)rect.pBits + y * rect.Pitch);
			for (int x = 0; x < source_dimensions.x; ++x)
			{
				const unsigned char *p = src + (y * source_dimensions.x + x) * 4;
				row[x] = D3DCOLOR_ARGB(p[3], p[0], p[1], p[2]);
			}
		}
		tex->UnlockRect(0);
	}
	return registerTexture(tex, true);
}

//-------------------------------------------------------------------------------------------------
void RmlUiRenderInterface::EnableScissorRegion(bool enable)
{
	m_scissorEmpty = false;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (m_inFrame && dev)
		dev->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE);
}

// Clamped to the target: a region with nothing on screen skips its draws instead of handing D3D an empty rect.
void RmlUiRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!m_inFrame || !dev)
		return;
	RECT r;
	r.left = region.Left() < 0 ? 0 : region.Left();
	r.top = region.Top() < 0 ? 0 : region.Top();
	r.right = region.Right() > m_width ? m_width : region.Right();
	r.bottom = region.Bottom() > m_height ? m_height : region.Bottom();
	m_scissorEmpty = r.right <= r.left || r.bottom <= r.top;
	if (!m_scissorEmpty)
		dev->SetScissorRect(&r);
}

// RmlUi's matrices act on column vectors and D3D's on row vectors: D3D's row i is RmlUi's column i.
void RmlUiRenderInterface::SetTransform(const Rml::Matrix4f *transform)
{
	m_hasTransform = transform != nullptr;
	if (!transform)
		return;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m_transform[i][j] = transform->GetColumn(i)[j];
}
