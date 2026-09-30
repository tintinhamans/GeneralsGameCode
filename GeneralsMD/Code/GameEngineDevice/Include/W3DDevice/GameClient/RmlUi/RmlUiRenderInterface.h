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

// FILE: RmlUiRenderInterface.h ///////////////////////////////////////////////
// Rml::RenderInterface implemented on top of the game's DX8 device (DX8Wrapper).
// All D3D state this touches is snapshotted with Get* and restored with Set*
// afterward, so the engine's own W3D rendering is left exactly as it found it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <d3d8.h>
#include <map>
#include <vector>

// Pixel space that engine textures report to RmlUi; <img rect> for them is uv * this, so crops
// stay right whatever size the texture page actually loaded at.
static const int RmlEngineTextureSpace = 4096;

// Pulls a sprite rect (RmlEngineTextureSpace units) in by half a texel of its atlas page, so linear
// filtering never blends in the neighbouring image's edge texels. Page size is the INI-declared one.
inline void RmlInsetTexelRect(int &left, int &top, int &right, int &bottom, int pageWidth, int pageHeight)
{
	if (pageWidth <= 0 || pageHeight <= 0)
		return;
	int dx = (int)(0.5f * RmlEngineTextureSpace / pageWidth + 0.5f);
	int dy = (int)(0.5f * RmlEngineTextureSpace / pageHeight + 0.5f);
	if (right - left > 2 * dx)
	{
		left += dx;
		right -= dx;
	}
	if (bottom - top > 2 * dy)
	{
		top += dy;
		bottom -= dy;
	}
}

//-------------------------------------------------------------------------------------------------
/** DX8 render backend for RmlUi. One instance is owned by RmlUiManager. */
//-------------------------------------------------------------------------------------------------
class RmlUiRenderInterface : public Rml::RenderInterface
{
public:
	RmlUiRenderInterface();
	virtual ~RmlUiRenderInterface() override;

	// Called once the D3D device exists, and again after a device reset.
	void onDeviceCreated();
	// Called right before Reset(); releases all D3DPOOL_DEFAULT resources we own.
	void onDeviceLost();

	// Brackets one context Update()+Render() call: saves every D3D state we touch, sets up
	// the fixed-function pipeline for premultiplied-alpha 2D UI rendering, and establishes
	// the default (unscissored) orthographic projection over the full context size. endFrame()
	// restores everything so the engine's own W3D rendering sees no side effects.
	void beginFrame(int contextWidth, int contextHeight);
	void endFrame();

	// Rml::RenderInterface -------------------------------------------------------------------
	virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	virtual void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	virtual void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	virtual Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
	virtual void ReleaseTexture(Rml::TextureHandle texture) override;

	virtual void EnableScissorRegion(bool enable) override;
	virtual void SetScissorRegion(Rml::Rectanglei region) override;

	// The CSS transform of the element being drawn, or null for none. It maps (vertex + translation) in
	// window pixels and rides on D3DTS_WORLD with the translation. Scissoring stays in window pixels, as
	// RmlUi expects; RmlUi drops a transformed element from the scissor and asks for a clip mask instead,
	// which this renderer does not implement, so content inside a transformed clipping element is not clipped.
	virtual void SetTransform(const Rml::Matrix4f *transform) override;

	// A texture the engine keeps writing to (a movie's video buffer), drawn as is: video formats have no alpha
	// channel, so it samples opaque and needs no premultiply pass. Takes its own reference.
	Rml::TextureHandle registerVideoTexture(IDirect3DTexture8 *tex);

private:
	struct CompiledGeometry
	{
		IDirect3DVertexBuffer8 *vb = nullptr;
		IDirect3DIndexBuffer8 *ib = nullptr;
		int numVertices = 0;
		int numIndices = 0;
	};

	// Textures created directly from a mapped: source (whole atlas, engine-owned lifetime).
	// We do not release the underlying engine texture; we only drop our reference/UV entry.
	struct LoadedTexture
	{
		IDirect3DTexture8 *d3dTexture = nullptr; // AddRef'd copy we own and must Release
		bool ownsRelease = true;
		bool premultiplied = true; // engine .tga/.dds pages are straight alpha
	};

	Rml::TextureHandle registerTexture(IDirect3DTexture8 *tex, bool ownsRelease, bool premultiplied);
	Rml::TextureHandle loadEngineTexture(Rml::Vector2i &dimensions, const Rml::String &path);
	Rml::TextureHandle loadStbTexture(Rml::Vector2i &dimensions, const Rml::String &path);
	Rml::TextureHandle loadMappedTexture(Rml::Vector2i &dimensions, const Rml::String &mappedName);

	void setOrthoProjection(int left, int right, int top, int bottom);

	typedef std::map<Rml::CompiledGeometryHandle, CompiledGeometry> GeometryMap;
	typedef std::map<Rml::TextureHandle, LoadedTexture> TextureMap;

	GeometryMap m_geometry;
	TextureMap m_textures;
	Rml::CompiledGeometryHandle m_nextGeometryHandle = 1;
	Rml::TextureHandle m_nextTextureHandle = 1;

	bool m_hasTransform = false;
	D3DMATRIX m_transform; ///< the current transform, transposed for D3D's row vectors; valid while m_hasTransform

	bool m_scissorEnabled = false;
	bool m_scissorEmpty = false; // the scissor region has no on-screen part; draws are skipped
	Rml::Rectanglei m_scissorRegion;
	int m_contextWidth = 0;
	int m_contextHeight = 0;

	// Saved D3D state, valid between saveRenderState()/restoreRenderState().
	struct SavedState
	{
		D3DVIEWPORT8 viewport;
		DWORD lighting = 0, zEnable = 0, cullMode = 0, alphaBlend = 0, srcBlend = 0, destBlend = 0;
		DWORD alphaTest = 0, fogEnable = 0, stencilEnable = 0;
		DWORD colorOp0 = 0, colorArg1_0 = 0, colorArg2_0 = 0, alphaOp0 = 0, alphaArg1_0 = 0, alphaArg2_0 = 0;
		DWORD magFilter0 = 0, minFilter0 = 0, addressU0 = 0, addressV0 = 0;
		DWORD renderStates[3] = {};
		DWORD stageStates[3][13] = {};
		IDirect3DBaseTexture8 *texture0 = nullptr;
		IDirect3DBaseTexture8 *texture1 = nullptr;
		D3DMATRIX world, view, projection;
		IDirect3DVertexBuffer8 *streamVb = nullptr;
		UINT streamStride = 0;
		IDirect3DIndexBuffer8 *indexBuffer = nullptr;
		UINT baseVertexIndex = 0;
		DWORD fvf = 0;
	} m_saved;
};
