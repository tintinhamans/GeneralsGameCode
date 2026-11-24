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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DSmudge.cpp ////////////////////////////////////////////////////////////////////////////////
// Smudge System implementation
// Author: Mark Wilczynski, June 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

// Ronin @build 18/10/2025 Include DX8-to-DX9 compatibility layer first
#include "WW3D2/dx8todx9.h"

#include "Lib/BaseType.h"
#include "WWLib/always.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GameMemory.h"
#include "GameClient/View.h"
#include "GameClient/Display.h"
#include "WW3D2/texture.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/Renderer.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/sortingrenderer.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/assetmgr.h"


SmudgeManager *TheSmudgeManager=nullptr;

W3DSmudgeManager::W3DSmudgeManager()
	: m_distortionPS(nullptr)
	, m_distortionVS(nullptr)
	, m_distortionDecl(nullptr)
	, m_noiseTexture(nullptr)
	, m_useDistortionShader(false)
	, m_resolveTexture(nullptr)
	, m_resolveSurface(nullptr)
{
}

W3DSmudgeManager::~W3DSmudgeManager()
{
	ReleaseResources();
}

void W3DSmudgeManager::init()
{
	SmudgeManager::init();
	ReAcquireResources();
}

void W3DSmudgeManager::reset()
{
	SmudgeManager::reset();	//base
}

void W3DSmudgeManager::ReleaseResources()
{
#ifdef USE_COPY_RECTS
	REF_PTR_RELEASE(m_backgroundTexture);
#endif
	REF_PTR_RELEASE(m_indexBuffer);

	// @feature Ronin 05/03/2026: Release resolve texture
	if (m_resolveSurface) { m_resolveSurface->Release(); m_resolveSurface = nullptr; }
	if (m_resolveTexture) { m_resolveTexture->Release(); m_resolveTexture = nullptr; }

	// @feature Ronin 02/03/2026: Release distortion shader resources
	if (m_distortionPS) { m_distortionPS->Release(); m_distortionPS = nullptr; }
	// @feature Ronin 05/03/2026: Release pass-through vertex shader
	if (m_distortionVS) { m_distortionVS->Release(); m_distortionVS = nullptr; }
	if (m_distortionDecl) { m_distortionDecl->Release(); m_distortionDecl = nullptr; }
	REF_PTR_RELEASE(m_noiseTexture);
	m_useDistortionShader = false; // VSO mudge Shader switch.
}

//Make sure (SMUDGE_DRAW_SIZE * 12) < 65535 because that's the max index buffer size.
#define SMUDGE_DRAW_SIZE	500	//draw at most 50 smudges per call. Tweak value to improve CPU/GPU parallelism.

// @feature Ronin 03/03/2026: Generate 256x256 Perlin noise texture procedurally at startup.
// @tweak Ronin 06/03/2026: Increased from 64x64 to 256x256 with lower base frequency (2.0)
// for smooth, organic gradients instead of high-frequency jitter. Single octave only —
// the detail octave was causing visible pixel-scale aliasing that looked like TV static.
static TextureClass* generateProceduralNoiseTexture()
{
	const Int NOISE_SIZE = 256;

	// Permutation table with fixed seed for deterministic results
	UnsignedByte perm[512];
	{
		for (Int i = 0; i < 256; i++)
			perm[i] = (UnsignedByte)i;
		UnsignedInt seed = 0xDEADBEEF;
		for (Int i = 255; i > 0; i--)
		{
			seed = seed * 1664525u + 1013904223u;
			Int j = (seed >> 16) % (i + 1);
			UnsignedByte tmp = perm[i];
			perm[i] = perm[j];
			perm[j] = tmp;
		}
		for (Int i = 0; i < 256; i++)
			perm[256 + i] = perm[i];
	}

	auto fade = [](float t) -> float { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
	auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
	auto grad = [&perm](Int hash, float x, float y) -> float {
		Int h = hash & 3;
		float u = (h < 2) ? x : y;
		float v = (h < 2) ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
		};
	auto perlin2D = [&](float x, float y) -> float {
		Int xi = ((Int)floorf(x)) & 255;
		Int yi = ((Int)floorf(y)) & 255;
		float xf = x - floorf(x);
		float yf = y - floorf(y);
		float u = fade(xf);
		float v = fade(yf);
		Int aa = perm[perm[xi] + yi];
		Int ab = perm[perm[xi] + yi + 1];
		Int ba = perm[perm[xi + 1] + yi];
		Int bb = perm[perm[xi + 1] + yi + 1];
		return lerp(
			lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
			lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u),
			v);
		};

	// Create D3D managed texture directly
	LPDIRECT3DDEVICE8 pDev = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DTexture8* d3dTex = nullptr;
	DWORD noiseUsage = 0;	// Ronin @feature 08/11/2026 DX9Ex: a lockable default-pool texture on 9Ex
	const D3DPOOL noisePool = DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, &noiseUsage);
	HRESULT hr = pDev->CreateTexture(
		NOISE_SIZE, NOISE_SIZE, 1, noiseUsage,
		D3DFMT_A8R8G8B8, noisePool, &d3dTex, nullptr);
	if (FAILED(hr) || !d3dTex)
	{
		DEBUG_LOG(("W3DSmudge: CreateTexture for noise failed (0x%08X)", (unsigned)hr));
		return nullptr;
	}

	D3DLOCKED_RECT lockedRect;
	hr = d3dTex->LockRect(0, &lockedRect, nullptr, 0);
	if (FAILED(hr))
	{
		DEBUG_LOG(("W3DSmudge: LockRect for noise failed (0x%08X)", (unsigned)hr));
		d3dTex->Release();
		return nullptr;
	}

	// @tweak Ronin 06/03/2026: Single low-frequency octave produces smooth, broad gradients.
	// Two octaves with different seeds for independent X/Y channels.
	const float scale1 = 2.0f;  // low frequency — each smudge sees a smooth gradient, not noise

	for (Int y = 0; y < NOISE_SIZE; y++)
	{
		UnsignedByte* row = (UnsignedByte*)lockedRect.pBits + y * lockedRect.Pitch;
		for (Int x = 0; x < NOISE_SIZE; x++)
		{
			float fx = (float)x / (float)NOISE_SIZE;
			float fy = (float)y / (float)NOISE_SIZE;

			// @tweak Ronin 06/03/2026: Single octave only — no detail layer.
			// The detail octave (scale2=8) was the source of the "bubbles" artifact.
			float n = perlin2D(fx * scale1, fy * scale1);

			// @bugfix Ronin 04/03/2026 Use separate noise values for R and G channels
			// to provide independent X/Y distortion offsets.
			float n2 = perlin2D(fx * scale1 + 37.0f, fy * scale1 + 17.0f);

			// Map [-1,1] to [0,255]
			Int valR = (Int)((n * 0.5f + 0.5f) * 255.0f);
			Int valG = (Int)((n2 * 0.5f + 0.5f) * 255.0f);
			if (valR < 0) valR = 0; if (valR > 255) valR = 255;
			if (valG < 0) valG = 0; if (valG > 255) valG = 255;

			row[x * 4 + 0] = (UnsignedByte)valG; // B (unused by shader, fill with G)
			row[x * 4 + 1] = (UnsignedByte)valG; // G — Y distortion
			row[x * 4 + 2] = (UnsignedByte)valR; // R — X distortion
			row[x * 4 + 3] = 255;                 // A
		}
	}

	d3dTex->UnlockRect(0);

	// Wrap in a TextureClass that takes ownership
	// @bugfix Ronin 03/03/2026: TextureClass(IDirect3DBaseTexture8*) calls AddRef internally,
	// so we must Release our local reference to avoid a leak.
	TextureClass* tex = NEW_REF(TextureClass, (d3dTex));
	d3dTex->Release();
	return tex;
}

// @bugfix Ronin 05/03/2026: Helper to load a compiled shader bytecode file (.vso / .pso)
// Returns heap-allocated bytecode on success (caller must HeapFree), nullptr on failure.
static DWORD* loadShaderBytecodeFromFile(const char* baseName, DWORD& outFileSize)
{
	// Ronin @feature 24/11/2026 DX9: read through W3DShaderManager, so the shaders folder and a .big both serve it.
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "shaders\\%s", baseName);
	DWORD* bytecode = W3DShaderManager::readShaderFile(path, &outFileSize);
	if (bytecode == nullptr)
		DEBUG_LOG(("W3DSmudge: Could not read %s", path));
	return bytecode;
}

// @feature Ronin 03/03/2026: Initialize HLSL distortion shader pair with procedural noise.
// @bugfix Ronin 05/03/2026: DX9 requires a paired vs_3_0 when using ps_3_0.
Bool W3DSmudgeManager::initDistortionShader()
{
	LPDIRECT3DDEVICE8 pDev = DX8Wrapper::_Get_D3D_Device8();
	if (!pDev) {
		DEBUG_LOG(("W3DSmudge: initDistortionShader — no D3D device"));
		return FALSE;
	}

	// Require at least PS 2.0 / VS 2.0 capable hardware
	D3DCAPS9 caps;
	pDev->GetDeviceCaps(&caps);
	DEBUG_LOG(("W3DSmudge: PixelShaderVersion = 0x%08X, VertexShaderVersion = 0x%08X",
		(unsigned)caps.PixelShaderVersion, (unsigned)caps.VertexShaderVersion));
	if (caps.PixelShaderVersion < D3DPS_VERSION(2, 0) || caps.VertexShaderVersion < D3DVS_VERSION(2, 0))
	{
		DEBUG_LOG(("W3DSmudge: Shader version too low, falling back to FFP"));
		return FALSE;
	}

	// @bugfix Ronin 05/03/2026: Vertex declaration must match VertexFormatXYZNDUV2 layout
	// Layout: float3 pos (0), float3 normal (12), DWORD diffuse (24), float2 uv0 (28), float2 uv1 (36)
	D3DVERTEXELEMENT9 declElements[] = {
		{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,  0},
		{0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,    0},
		{0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,     0},
		{0, 28, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0},
		{0, 36, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  1},
		D3DDECL_END()
	};

	HRESULT hr = pDev->CreateVertexDeclaration(declElements, &m_distortionDecl);
	if (FAILED(hr)) {
		DEBUG_LOG(("W3DSmudge: Failed to create distortion vertex decl (0x%08X)", (unsigned)hr));
		return FALSE;
	}

	// Load vertex shader (pass-through, transforms view-space pos by projection)
	{
		DWORD fileSize = 0;
		DWORD* bytecode = loadShaderBytecodeFromFile("distortion_vs.vso", fileSize);
		if (!bytecode) {
			DEBUG_LOG(("W3DSmudge: Could not load distortion_vs.vso, falling back to FFP"));
			m_distortionDecl->Release(); m_distortionDecl = nullptr;
			return FALSE;
		}

		hr = pDev->CreateVertexShader(bytecode, &m_distortionVS);
		HeapFree(GetProcessHeap(), 0, bytecode);
		if (FAILED(hr)) {
			DEBUG_LOG(("W3DSmudge: CreateVertexShader failed: 0x%08X", (unsigned)hr));
			m_distortionDecl->Release(); m_distortionDecl = nullptr;
			return FALSE;
		}
	}

	// Load pixel shader (distortion sampling)
	{
		DWORD fileSize = 0;
		DWORD* bytecode = loadShaderBytecodeFromFile("distortion.pso", fileSize);
		if (!bytecode) {
			DEBUG_LOG(("W3DSmudge: Could not load distortion.pso, falling back to FFP"));
			m_distortionVS->Release(); m_distortionVS = nullptr;
			m_distortionDecl->Release(); m_distortionDecl = nullptr;
			return FALSE;
		}

		hr = pDev->CreatePixelShader(bytecode, &m_distortionPS);
		HeapFree(GetProcessHeap(), 0, bytecode);
		if (FAILED(hr)) {
			DEBUG_LOG(("W3DSmudge: CreatePixelShader failed: 0x%08X", (unsigned)hr));
			m_distortionVS->Release(); m_distortionVS = nullptr;
			m_distortionDecl->Release(); m_distortionDecl = nullptr;
			return FALSE;
		}
	}

	// Generate noise texture procedurally — no file dependency
	m_noiseTexture = generateProceduralNoiseTexture();
	if (!m_noiseTexture) {
		DEBUG_LOG(("W3DSmudge: Failed to generate procedural noise texture, falling back to FFP"));
		m_distortionPS->Release(); m_distortionPS = nullptr;
		m_distortionVS->Release(); m_distortionVS = nullptr;
		m_distortionDecl->Release(); m_distortionDecl = nullptr;
		return FALSE;
	}

	// @debug Ronin 03/03/2026: Verify noise texture has a valid D3D resource
	DEBUG_LOG(("W3DSmudge: Noise texture D3D ptr = %p", m_noiseTexture->Peek_D3D_Base_Texture()));

	// Set noise texture to wrap (repeat) + no mipmaps for smooth scrolling
	m_noiseTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	m_noiseTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	m_noiseTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	DEBUG_LOG(("W3DSmudge: Distortion shader pair initialized OK (vs_3_0 + ps_3_0, VS=%p PS=%p)",
		m_distortionVS, m_distortionPS));
	return TRUE;
}

void W3DSmudgeManager::ReAcquireResources()
{
	ReleaseResources();

	SurfaceClass* surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	SurfaceClass::SurfaceDescription surface_desc;

	surface->Get_Description(surface_desc);
	REF_PTR_RELEASE(surface);

#ifdef USE_COPY_RECTS
	m_backgroundTexture = MSGNEW("TextureClass") TextureClass(TheTacticalView->getWidth(), TheTacticalView->getHeight(), surface_desc.Format, MIP_LEVELS_1, TextureClass::POOL_DEFAULT, true);
#endif

	m_backBufferWidth = surface_desc.Width;
	m_backBufferHeight = surface_desc.Height;

	m_indexBuffer = NEW_REF(DX8IndexBufferClass, (SMUDGE_DRAW_SIZE * 4 * 3));	//allocate 4 triangles per smudge, each with 3 indices.

	// Fill up the IB with static vertex indices that will be used for all smudges.
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort* ib = lockIdxBuffer.Get_Index_Array();
		//quad of 4 triangles:
		//	0-----3
		//  |\   /|
		//  |  4  |
		//	|/   \|
		//  1-----2
		Int vbCount = 0;
		for (Int i = 0; i < SMUDGE_DRAW_SIZE; i++)
		{
			//Top
			ib[0] = vbCount;
			ib[1] = vbCount + 4;
			ib[2] = vbCount + 3;
			//Right
			ib[3] = vbCount + 3;
			ib[4] = vbCount + 4;
			ib[5] = vbCount + 2;
			//Bottom
			ib[6] = vbCount + 2;
			ib[7] = vbCount + 4;
			ib[8] = vbCount + 1;
			//Left
			ib[9] = vbCount + 1;
			ib[10] = vbCount + 4;
			ib[11] = vbCount + 0;

			vbCount += 5;
			ib += 12;
		}
	}

	// @feature Ronin 05/03/2026: Create dedicated non-MSAA resolve texture matching backbuffer format.
	m_resolveTexture = nullptr;
	m_resolveSurface = nullptr;
	{
		LPDIRECT3DDEVICE8 pDev = DX8Wrapper::_Get_D3D_Device8();
		if (pDev)
		{
			IDirect3DSurface8* bbSurf = nullptr;
			pDev->GetRenderTarget(0, &bbSurf);
			if (bbSurf)
			{
				D3DSURFACE_DESC bbDesc;
				bbSurf->GetDesc(&bbDesc);
				bbSurf->Release();

				HRESULT hr = pDev->CreateTexture(
					bbDesc.Width, bbDesc.Height, 1,
					D3DUSAGE_RENDERTARGET, bbDesc.Format,
					D3DPOOL_DEFAULT, &m_resolveTexture, nullptr);
				if (SUCCEEDED(hr) && m_resolveTexture)
				{
					m_resolveTexture->GetSurfaceLevel(0, &m_resolveSurface);
					DEBUG_LOG(("W3DSmudge: Created resolve texture %ux%u fmt=%d", bbDesc.Width, bbDesc.Height, bbDesc.Format));
				}
				else
				{
					DEBUG_LOG(("W3DSmudge: Failed to create resolve texture (0x%08X)", (unsigned)hr));
				}
			}
		}
	}

	// @feature Ronin 02/03/2026: Try to init distortion shader (VS+PS pair)
	m_useDistortionShader = initDistortionShader();

	DEBUG_LOG(("W3DSmudge: m_useDistortionShader = %s", m_useDistortionShader ? "TRUE" : "FALSE"));
}


Bool W3DSmudgeManager::testHardwareSupport()
{
	if (m_hardwareSupportStatus == SMUDGE_SUPPORT_UNKNOWN)
	{
		if (W3DShaderManager::canRenderToTexture())
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;
			DEBUG_LOG(("W3DSmudge: Hardware support = YES (canRenderToTexture)"));
		}
		else
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			DEBUG_LOG(("W3DSmudge: Hardware support = NO"));
		}
	}

	return (SMUDGE_SUPPORT_YES == m_hardwareSupportStatus);
}

void W3DSmudgeManager::render(RenderInfoClass& rinfo)
{
	//Verify that the card supports the effect.
	if (!testHardwareSupport())
		return;

	// TheSuperHackers @performance stephanmeesters 14/08/2026 Early return when we have no smudge sets
	// or if the global smudge set is the only set and contains no smudges.
	if (m_usedSmudgeSetList.empty() || (m_usedSmudgeSetList.size() == 1 && m_usedSmudgeSetList.front()->getUsedSmudgeCount() == 0))
	{
		m_smudgeCountLastFrame = 0;
		return;
	}

	CameraClass& camera = rinfo.Camera;
	Vector3 vsVert;
	Vector4 ssVert;
	Real uvSpanX, uvSpanY;
	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

	// @bugfix Ronin 04/03/2026 Diffuse color must be pure white so the scene texture is not
	// tinted. Only the alpha channel carries opacity for edge falloff.
	// @bugfix Ronin 05/03/2026: Corner vertices need non-zero alpha for the distortion shader
	// to produce visible UV offsets (PS multiplies offset by vertex alpha). Use a small base
	// alpha on corners so distortion fades smoothly from center to edge instead of being zero
	// everywhere except the exact center point.
#define THE_COLOR (0x00ffffff)
#define THE_CORNER_COLOR_SHADER (0x10ffffff)

	UnsignedInt vertexDiffuse[5] = { THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR };

	Matrix4x4 proj;
	Matrix3D view;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	SurfaceClass::SurfaceDescription surface_desc;
#ifdef USE_COPY_RECTS
	SurfaceClass* background = m_backgroundTexture->Get_Surface_Level();
	background->Get_Description(surface_desc);
#else
	D3DSURFACE_DESC D3DDesc;

	// @feature Ronin 05/03/2026: Use dedicated resolve texture instead of W3DShaderManager's RTT.
	if (!m_resolveTexture || !m_resolveSurface)
		return;

	{
		LPDIRECT3DDEVICE8 pDev = DX8Wrapper::_Get_D3D_Device8();
		IDirect3DSurface8* backbufferSurf = nullptr;
		HRESULT hr = pDev->GetRenderTarget(0, &backbufferSurf);
		if (SUCCEEDED(hr) && backbufferSurf)
		{
			// Ronin @bugfix 21/10/2026 DX9: the copy in the target's own format - made once at startup, it stayed 8-bit under HDR's float
			// scene and clipped it at 1: a dimmer patch round every heat haze. Made again when the format or size changes.
			D3DSURFACE_DESC rtDesc, copyDesc;
			if (SUCCEEDED(backbufferSurf->GetDesc(&rtDesc)) && SUCCEEDED(m_resolveTexture->GetLevelDesc(0, &copyDesc)) &&
				(rtDesc.Format != copyDesc.Format || rtDesc.Width != copyDesc.Width || rtDesc.Height != copyDesc.Height))
			{
				IDirect3DTexture8* tex = nullptr;
				IDirect3DSurface8* surf = nullptr;
				if (SUCCEEDED(pDev->CreateTexture(rtDesc.Width, rtDesc.Height, 1, D3DUSAGE_RENDERTARGET, rtDesc.Format, D3DPOOL_DEFAULT,
												  &tex, nullptr)) && tex != nullptr && SUCCEEDED(tex->GetSurfaceLevel(0, &surf)) && surf != nullptr)
				{
					m_resolveSurface->Release();
					m_resolveTexture->Release();
					m_resolveTexture = tex;
					m_resolveSurface = surf;
				}
				else if (tex != nullptr)
				{
					tex->Release();
				}
			}
			hr = pDev->StretchRect(backbufferSurf, nullptr, m_resolveSurface, nullptr, D3DTEXF_NONE);
			backbufferSurf->Release();
			if (FAILED(hr))
			{
				DEBUG_LOG(("W3DSmudge: StretchRect failed (0x%08X)", (unsigned)hr));
				return;
			}
		}
		else
		{
			if (backbufferSurf) backbufferSurf->Release();
			return;
		}
	}

	m_resolveTexture->GetLevelDesc(0, &D3DDesc);
	surface_desc.Width = D3DDesc.Width;
	surface_desc.Height = D3DDesc.Height;

#endif

	Real texClampX = (Real)TheTacticalView->getWidth() / (Real)surface_desc.Width;
	Real texClampY = (Real)TheTacticalView->getHeight() / (Real)surface_desc.Height;

	Real texScaleX = texClampX * 0.5f;
	Real texScaleY = texClampY * 0.5f;

	SmudgeSetDeque::iterator setIt=m_usedSmudgeSetList.begin();	//first set that didn't fit into render batch.
	Int count = 0;

	// make sure background particles have finished drawing.
	SortingRendererClass::Flush();	//draw sorted translucent polys like particles.

	for(; setIt != m_usedSmudgeSetList.end(); ++setIt)
	{
		SmudgeSet* set=*setIt;
		SmudgeDeque::iterator smudgeIt=set->getUsedSmudgeList().begin();

		for (; smudgeIt != set->getUsedSmudgeList().end(); ++smudgeIt)
		{
			Smudge* smudge=*smudgeIt;
			if (!smudge->m_draw)
				continue;

			//Get view-space center
			Matrix3D::Transform_Vector(view,smudge->m_pos,&vsVert);

			//Get 5 view-space vertices
			Smudge::smudgeVertex *verts=smudge->m_verts;

			//Do center vertex outside 'for' loop since it's different.				  
			verts[4].pos = vsVert;

			for (Int i = 0; i < 4; i++)
			{
				verts[i].pos = vsVert + vertex_offsets[i] * smudge->m_size;
				//Ge uv coordinates for each vertex
				ssVert = proj * verts[i].pos;
				Real oow = 1.0f / ssVert.W;
				ssVert *= oow; //returned in camera space which is -1,-1 (bottom-left) to 1,1 (top-right)
				verts[i].uv.Set((ssVert.X + 1.0f) * texScaleX, (1.0f - ssVert.Y) * texScaleY);

				Vector2& thisUV = verts[i].uv;

				// Zero coordinates that fall outside valid texel bounds
				if (thisUV.X > texClampX)
					smudge->m_offset.X = 0;
				else if (thisUV.X < 0)
					smudge->m_offset.X = 0;

				if (thisUV.Y > texClampY)
					smudge->m_offset.Y = 0;
				else if (thisUV.Y < 0)
					smudge->m_offset.Y = 0;
			}

			//Finish center vertex
			//Ge uv coordinates by interpolating corner uv coordinates and applying desired o																						  
			uvSpanX = verts[3].uv.X - verts[0].uv.X;
			uvSpanY = verts[1].uv.Y - verts[0].uv.Y;
			verts[4].uv.X = verts[0].uv.X + uvSpanX * (0.5f + smudge->m_offset.X);
			// @bugfix Ronin 04/03/2026 Was using m_offset.X for Y axis
			verts[4].uv.Y = verts[0].uv.Y + uvSpanY * (0.5f + smudge->m_offset.Y);

			count++;   //increment visible smudge count.
			
		}
	}

	if (!count)
	{
#ifdef USE_COPY_RECTS
		REF_PTR_RELEASE(background);
#endif
		return;
	}

	// @debug Ronin 03/03/2026: Log smudge count once per second to verify render() is reached
	{
		static DWORD s_lastLogTime = 0;
		DWORD now = GetTickCount();
		if (now - s_lastLogTime > 2000) {
			s_lastLogTime = now;
			DEBUG_LOG(("W3DSmudge::render — %d smudges, shader=%s, VS=%p, PS=%p, noise=%p, decl=%p",
				count,
				m_useDistortionShader ? "ON" : "OFF",
				m_distortionVS,
				m_distortionPS,
				m_noiseTexture,
				m_distortionDecl));
		}
	}

#ifdef USE_COPY_RECTS
	SurfaceClass* backBuffer = DX8Wrapper::_Get_DX8_Back_Buffer();
	backBuffer->Get_Description(surface_desc);
	background->Copy(0, 0, 0, 0, surface_desc.Width, surface_desc.Height, backBuffer);
	REF_PTR_RELEASE(background);
	REF_PTR_RELEASE(backBuffer);
#endif

	Matrix4x4 identity(true);
	Renderer::Set_Transform(RenderTransform_World,identity);
	Renderer::Set_Transform(RenderTransform_View,identity);

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0);

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
#ifdef USE_COPY_RECTS
	DX8Wrapper::Set_Texture(0, m_backgroundTexture);
#else
	DX8Wrapper::Set_DX8_Texture(0, m_resolveTexture);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
#endif
	VertexMaterialClass* vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Apply_Render_State_Changes();

	LPDIRECT3DDEVICE8 pDev = DX8Wrapper::_Get_D3D_Device8();

	// @bugfix Ronin 05/03/2026: Always set ALPHAOP to use vertex alpha for edge falloff.
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

	// Animated noise UV scroll (changes each frame for shimmer effect)
	// Animated noise UV scroll (changes each frame for shimmer effect)
	// @tweak Ronin 06/03/2026: Reduced scroll speed by 6x for slow, organic undulation
	// instead of rapid jitter. Real heat haze moves slowly — rising hot air drifts upward
	// at ~0.5m/s, not the 3m/s the previous values implied.
	const float timeSeconds = WW3D::Get_Logic_Frame_Time_Seconds();
	static float s_noiseScrollU = 0.0f;
	static float s_noiseScrollV = 0.0f;
	s_noiseScrollU += 0.005f * timeSeconds;
	s_noiseScrollV += 0.008f * timeSeconds;
	// Keep in [0,1) range
	s_noiseScrollU -= (Int)s_noiseScrollU;
	s_noiseScrollV -= (Int)s_noiseScrollV;

	Int smudgesRemaining=count;
	setIt=m_usedSmudgeSetList.begin();	//first smudge set that needs rendering.
	SmudgeDeque::iterator smudgeIt = (*setIt)->getUsedSmudgeList().begin();	//first smudge that needs rendering.

	while (smudgesRemaining)
	{
		count = smudgesRemaining;

		if (count > SMUDGE_DRAW_SIZE)
			count = SMUDGE_DRAW_SIZE;

		Int smudgesInRenderBatch = 0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, count * 5);
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts = lock.Get_Formatted_Vertex_Array();

			while (setIt != m_usedSmudgeSetList.end())
			{
				SmudgeDeque& smudgeList = (*setIt)->getUsedSmudgeList();

				for(; smudgeIt != smudgeList.end(); ++smudgeIt)
				{
					Smudge* smudge = *smudgeIt;
					if (!smudge->m_draw)
					{
						continue;
					}
					
					Smudge::smudgeVertex *smVerts = smudge->m_verts;

					//Check if we exceeded maximum number of smudges allowed per draw call.														
					if (smudgesInRenderBatch >= count)
					{
						goto flushSmudges;
					}

					//Set center vertex opacity.			 
					vertexDiffuse[4] = ((Int)(smudge->m_opacity * 255.0f) << 24) | THE_COLOR;

					// @tweak Ronin 06/03/2026: Corner alpha at 25% of center for gentle falloff.
					if (m_useDistortionShader) {
						UnsignedInt cornerAlpha = (UnsignedInt)(smudge->m_opacity * 255.0f * 0.25f);
						if (cornerAlpha > 255) cornerAlpha = 255;
						UnsignedInt cornerColor = (cornerAlpha << 24) | 0x00ffffff;
						vertexDiffuse[0] = cornerColor;
						vertexDiffuse[1] = cornerColor;
						vertexDiffuse[2] = cornerColor;
						vertexDiffuse[3] = cornerColor;
					}

					for (Int i = 0; i < 5; i++)
					{
						verts->x = smVerts->pos.X;
						verts->y = smVerts->pos.Y;
						verts->z = smVerts->pos.Z;
						verts->nx = 0;
						verts->ny = 0;
						verts->nz = 0;
						verts->diffuse = vertexDiffuse[i];
						verts->u1 = smVerts->uv.X;
						verts->v1 = smVerts->uv.Y;

						if (m_useDistortionShader) {
							// @tweak Ronin 06/03/2026: UV multiplier reduced from 1.5 to 0.3.
							// Each smudge quad now samples a tiny region of the noise texture,
							// seeing only a smooth gradient — not the full noise pattern.
							// This is the single most important change for visual quality.
							verts->u2 = smVerts->uv.X * 0.3f + s_noiseScrollU;
							verts->v2 = smVerts->uv.Y * 0.3f + s_noiseScrollV;
						}
						else {
							verts->u2 = 0;
							verts->v2 = 0;
						}

						verts++;
						smVerts++;
					}

					smudgesInRenderBatch++;
					
				}

				++setIt;	//advance to next node.

				if (setIt != m_usedSmudgeSetList.end())	//start next batch at beginning of set.
					smudgeIt = (*setIt)->getUsedSmudgeList().begin();
			}
		flushSmudges:
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
		}

		// @bugfix Ronin 05/03/2026 Apply all deferred state, then override with our shader pair.
		DX8Wrapper::Apply_Render_State_Changes();

		// @bugfix Ronin 05/03/2026: DynamicVBAccessClass allocates from a shared dynamic VB
		// at an offset. We must pass this as BaseVertexIndex to DrawIndexedPrimitive,
		// otherwise we read stale vertices from the start of the buffer (producing the
		// vanilla FFP effect instead of our distortion shader output).
		const int baseVertexIndex = vb_access.Get_VB_Offset();


		if (m_useDistortionShader && m_distortionVS && m_distortionPS && m_noiseTexture)
		{
			// @bugfix Ronin 05/03/2026: Bind BOTH vertex and pixel shaders as a matched pair.
			// DX9 requires vs_3_0 + ps_3_0 together — cannot use ps_3_0 with FFP vertex pipeline.
			// This is the exact same pattern as the working RigidInstance instancing shader.
			pDev->SetVertexDeclaration(m_distortionDecl);
			pDev->SetVertexShader(m_distortionVS);
			pDev->SetPixelShader(m_distortionPS);

			// Upload projection matrix to VS constants c0..c3
			// (World=Identity, View=Identity, so VS only needs projection)
			{
				D3DMATRIX projMat;
				pDev->GetTransform(D3DTS_PROJECTION, &projMat);
				D3DXMATRIX dxProjT;
				D3DXMatrixTranspose(&dxProjT, (const D3DXMATRIX*)&projMat);
				pDev->SetVertexShaderConstantF(0, (const float*)&dxProjT, 4);
			}

			// PS constants
			// @tweak Ronin 06/03/2026: Reduced to 0.010 for subtle, barely-perceptible shimmer.
			// At 1080p this gives ~6px max displacement at center, tapering to ~2px at corners.
			// Heat haze should be something you notice after staring, not immediately obvious.
			float c0[4] = { 0.010f, 0.0f, 0.0f, 0.0f };
			pDev->SetPixelShaderConstantF(0, c0, 1);
			float c1[4] = { texClampX, texClampY, 0.0f, 0.0f };
			pDev->SetPixelShaderConstantF(1, c1, 1);

			// Bind noise to sampler 1
			IDirect3DBaseTexture8* noiseD3D = m_noiseTexture->Peek_D3D_Base_Texture();
			pDev->SetTexture(1, noiseD3D);
			DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		}

		// Draw 
		DX8Wrapper::Draw_Triangles(
			baseVertexIndex,            // BaseVertexIndex
			0,                          // MinVertexIndex
			smudgesInRenderBatch * 5,   // NumVertices
			0,                          // StartIndex
			smudgesInRenderBatch * 4);  // PrimitiveCount

		smudgesRemaining -= smudgesInRenderBatch;
	}

	// Cleanup — restore fixed-function pipeline
	if (m_useDistortionShader && m_distortionVS && m_distortionPS)
	{
		pDev->SetVertexShader(nullptr);
		pDev->SetPixelShader(nullptr);
		pDev->SetVertexDeclaration(nullptr);
		pDev->SetTexture(1, nullptr);
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	DX8Wrapper::Invalidate_Cached_Render_States();
}
