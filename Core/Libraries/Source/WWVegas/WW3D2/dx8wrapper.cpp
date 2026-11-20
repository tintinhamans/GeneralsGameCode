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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8wrapper.cpp                         $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 170                                                         $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 06/27/02 KM Shader system updates																				*
 * 08/05/02 KM Texture class redesign
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define CREATE_DX8_MULTI_THREADED
//#define CREATE_DX8_FPU_PRESERVE
#define WW3D_DEVTYPE D3DDEVTYPE_HAL

#if !defined(WINVER) || WINVER < 0x0500
#undef WINVER
#define WINVER 0x0500 // Required to access GetMonitorInfo in VC6.
#endif

// Ronin @debug 25/11/2025: Verbose FVF logging disabled by default (causes FPS < 1)
// Uncomment to enable detailed FVF/declaration tracking for debugging
// #define ENABLE_VERBOSE_FVF_LOGGING

// Ronin 19/10/2025 Include DX8->DX9 compatibility layer first
#include <d3d9.h>  // Native DX9
#include <d3dx9.h> // D3DX9 helper functions
#include <stdio.h> // For sprintf in DXGetErrorString9A replacement

//#include <dxerr9.h>  // Ronin @build 19/01/2026 DX9: error string helper
// Inline replacement for DXGetErrorString9A (from legacy dxerr9.lib)
const char* DXGetErrorString9A(HRESULT hr)
{
	switch (hr) {
	case D3D_OK: return "D3D_OK";
	case D3DERR_DEVICELOST: return "D3DERR_DEVICELOST";
	case D3DERR_INVALIDCALL: return "D3DERR_INVALIDCALL";
	case D3DERR_NOTAVAILABLE: return "D3DERR_NOTAVAILABLE";
	case D3DERR_OUTOFVIDEOMEMORY: return "D3DERR_OUTOFVIDEOMEMORY";
	case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
	default: {
		static char buf[32];
		sprintf(buf, "HRESULT=0x%08X", (unsigned)hr);
		return buf;
	}
	}
}

#include "dx8wrapper.h"
#include "dx8fvf.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8renderer.h"
#include "dx8instancing.h"
#include "ww3d.h"
#include "camera.h"
#include "WWLib/wwstring.h"
#include "WWMath/matrix4.h"
#include "vertmaterial.h"
#include "rddesc.h"
#include "lightenvironment.h"
#include "statistics.h"
#include "WWLib/registry.h"
#include "boxrobj.h"
#include "pointgr.h"
#include "render2d.h"
#include "sortingrenderer.h"
#include "shattersystem.h"
#include "light.h"
#include "assetmgr.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "WWLib/thread.h"
// Ronin 19/10/2025 Removed direct include of d3dx8core.h - not available with DX9 SDK
#include "WWDebug/wwprofile.h"
#include "WWLib/ffactory.h"
#include "dx8caps.h"
#include "dx8convert.h"
#include "formconv.h"
#include "dx8texman.h"
#include "WWLib/bound.h"
#include "DbgHelpGuard.h"
#include <Utility/bit_adapter.h>

#include "shdlib.h"

const int DEFAULT_RESOLUTION_WIDTH = 640;
const int DEFAULT_RESOLUTION_HEIGHT = 480;
const int DEFAULT_BIT_DEPTH = 32;
const int DEFAULT_TEXTURE_BIT_DEPTH = 16;

DX8FrameStatistics DX8Wrapper::FrameStatistics;
static DX8FrameStatistics LastFrameStatistics;

bool DX8Wrapper_IsWindowed = true;

// FPU_PRESERVE
int DX8Wrapper_PreserveFPU = 0;

// Ronin @debug 06/11/2025: Frame counter - must be declared before debug tracking code
unsigned long DX8Wrapper::FrameCount = 0;

/***********************************************************************************
**
** DX8Wrapper Static Variables
**
***********************************************************************************/

static HWND						_Hwnd															= nullptr;
bool								DX8Wrapper::IsInitted									= false;
bool								DX8Wrapper::_EnableTriangleDraw						= true;

int								DX8Wrapper::CurRenderDevice							= -1;
int								DX8Wrapper::ResolutionWidth							= DEFAULT_RESOLUTION_WIDTH;
int								DX8Wrapper::ResolutionHeight							= DEFAULT_RESOLUTION_HEIGHT;
int								DX8Wrapper::BitDepth										= DEFAULT_BIT_DEPTH;
int								DX8Wrapper::TextureBitDepth							= DEFAULT_TEXTURE_BIT_DEPTH;
bool								DX8Wrapper::IsWindowed									= false;
D3DFORMAT					DX8Wrapper::DisplayFormat	= D3DFMT_UNKNOWN;

// shader system additions KJM v
DWORD								DX8Wrapper::Vertex_Shader								= 0;
DWORD								DX8Wrapper::Pixel_Shader								= 0;

Vector4							DX8Wrapper::Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
Vector4							DX8Wrapper::Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

LightEnvironmentClass*		DX8Wrapper::Light_Environment							= nullptr;

DWORD								DX8Wrapper::Vertex_Processing_Behavior				= 0;
ZTextureClass*					DX8Wrapper::Shadow_Map[MAX_SHADOW_MAPS];

Vector3							DX8Wrapper::Ambient_Color;
// shader system additions KJM ^

bool								DX8Wrapper::world_identity;
unsigned							DX8Wrapper::RenderStates[256];
unsigned							DX8Wrapper::TextureStageStates[MAX_TEXTURE_STAGES][33];
unsigned	            DX8Wrapper::SamplerStates[MAX_TEXTURE_STAGES][14];
IDirect3DBaseTexture8* DX8Wrapper::Textures[MAX_TEXTURE_STAGES];
int								DX8Wrapper::AntiAliasingLevel = 0;
RenderStateStruct				DX8Wrapper::render_state;
unsigned							DX8Wrapper::render_state_changed;

bool								DX8Wrapper::FogEnable									= false;
D3DCOLOR							DX8Wrapper::FogColor										= 0;

IDirect3D8 *					DX8Wrapper::D3DInterface								= nullptr;
IDirect3DDevice8 *			DX8Wrapper::D3DDevice									= nullptr;
bool								DX8Wrapper::ExRequested								= false;	// Ronin @feature 08/11/2026 DX9Ex
bool								DX8Wrapper::IsEx										= false;
IDirect3DSurface8 *			DX8Wrapper::CurrentRenderTarget						= nullptr;
IDirect3DSurface8 *			DX8Wrapper::CurrentDepthBuffer						= nullptr;
IDirect3DSurface8 *			DX8Wrapper::DefaultRenderTarget						= nullptr;
IDirect3DSurface8 *			DX8Wrapper::DefaultDepthBuffer						= nullptr;
bool								DX8Wrapper::IsRenderToTexture							= false;
// Ronin @feature 13/09/2026 DX9: SSAO step 1. INTZ — a depth-stencil format NVIDIA and AMD let a shader read.
#define D3DFMT_INTZ_FOURCC ((D3DFORMAT)MAKEFOURCC('I','N','T','Z'))
IDirect3DTexture9 *			DX8Wrapper::SceneDepthTexture							= nullptr;
IDirect3DSurface9 *			DX8Wrapper::SceneDepthSurface							= nullptr;
IDirect3DSurface9 *			DX8Wrapper::SceneDepthSaved								= nullptr;
int								DX8Wrapper::SceneDepthSupport							= -1;
bool								DX8Wrapper::SceneDepthWritten							= false;
bool								DX8Wrapper::SceneDepthSuspended							= false;
// Ronin @feature 06/10/2026 DX9: SSAO under MSAA. NULL - a colour target that takes no memory, for the depth prepass.
#define D3DFMT_NULL_FOURCC ((D3DFORMAT)MAKEFOURCC('N','U','L','L'))
IDirect3DSurface9 *			DX8Wrapper::ScenePrepassColor							= nullptr;
IDirect3DSurface9 *			DX8Wrapper::ScenePrepassSavedColor					= nullptr;
IDirect3DSurface9 *			DX8Wrapper::ScenePrepassSavedDepth					= nullptr;
IDirect3DSurface9 *			DX8Wrapper::SceneDepthHeld								= nullptr;
int								DX8Wrapper::ScenePrepassTarget							= 0;
int								DX8Wrapper::NullTargetSupport							= -1;
bool								DX8Wrapper::SceneDepthFromPrepass						= false;

unsigned							DX8Wrapper::_MainThreadID								= 0;
bool								DX8Wrapper::CurrentDX8LightEnables[MAX_LIGHTS];
bool								DX8Wrapper::IsDeviceLost;
bool								DX8Wrapper::DeviceResetFailed = false;
int								DX8Wrapper::ZBias;
float								DX8Wrapper::ZNear;
float								DX8Wrapper::ZFar;
D3DMATRIX						DX8Wrapper::ProjectionMatrix;
D3DMATRIX						DX8Wrapper::DX8Transforms[D3DTS_WORLD+1];

DX8Caps*							DX8Wrapper::CurrentCaps = nullptr;

// Hack test... this disables rendering of batches of too few polygons.
unsigned							DX8Wrapper::DrawPolygonLowBoundLimit=0;

D3DADAPTER_IDENTIFIER9		DX8Wrapper::CurrentAdapterIdentifier;

bool								_DX8SingleThreaded										= false;

// Ronin @bugfix 09/11/2025: Track BeginScene/EndScene pairing to prevent INVALIDCALL errors
static bool s_inScene = false;

// Ronin @feature 27/11/2025: Vertex declaration cache instance
VertexDeclCache* DX8Wrapper::DeclCache = nullptr;

static D3DPRESENT_PARAMETERS								_PresentParameters;
static DynamicVectorClass<StringClass>					_RenderDeviceNameTable;
static DynamicVectorClass<StringClass>					_RenderDeviceShortNameTable;
static DynamicVectorClass<RenderDeviceDescClass>	_RenderDeviceDescriptionTable;


typedef IDirect3D9* (WINAPI *Direct3DCreate8Type) (UINT SDKVersion);
Direct3DCreate8Type	Direct3DCreate8Ptr = nullptr;
typedef HRESULT (WINAPI *Direct3DCreate9ExType) (UINT SDKVersion, IDirect3D9Ex **ppD3D);	// Ronin @feature 08/11/2026 DX9Ex
HINSTANCE D3D8Lib = nullptr;

DX8_CleanupHook	 *DX8Wrapper::m_pCleanupHook=nullptr;
#ifdef EXTENDED_STATS
DX8_Stats	 DX8Wrapper::stats;
#endif

#ifdef _DEBUG
static bool Is_Engine_Owned_Decl(IDirect3DVertexDeclaration9* decl)
{
	if (!decl) return false;

	// Check VertexDeclCache allocations (if created)
	// Note: DeclCache is a DX8Wrapper static pointer.
	if (DX8Wrapper::DeclCache) {
		// Today, DeclCache only recognizes a small set of FVFs, but that’s fine.
		// If a decl pointer matches any cached entry, treat it as "ours".
		const UINT knownFvfs[] = {
			(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1), // Water tracks (per VertexDeclCache::IsKnownFvf)
		};

		for (UINT fvf : knownFvfs) {
			const DeclEntry* e = DX8Wrapper::DeclCache->GetOrCreateDecl(fvf);
			if (e && e->decl == decl) {
				return true;
			}
		}
	}

	// Add other “singletons” we create elsewhere here if needed (optional).
	return false;
}
#endif



// Ronin @bugfix 19/11/2025: Convert FVF code to D3D9 Vertex Declaration
static D3DVERTEXELEMENT9* FVFToDeclaration(DWORD fvf)
{
	// Maximum 16 elements + D3DDECL_END()
	static D3DVERTEXELEMENT9 decl[17];
	int elementIndex = 0;
	WORD offset = 0;

	// Position (always present if FVF != 0)
	if (fvf & D3DFVF_XYZ) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 };
		offset += 12; // 3 floats
	}
	else if (fvf & D3DFVF_XYZRHW) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 };
		offset += 16; // 4 floats
	}

	// Normal
	if (fvf & D3DFVF_NORMAL) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 };
		offset += 12;
	}

	// Diffuse color
	if (fvf & D3DFVF_DIFFUSE) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 };
		offset += 4;
	}

	// Specular color
	if (fvf & D3DFVF_SPECULAR) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1 };
		offset += 4;
	}

	// Texture coordinates
	int texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (int i = 0; i < texCount; i++) {
		decl[elementIndex++] = { 0, offset, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, (BYTE)i };
		offset += 8; // 2 floats per UV set
	}

	// End marker
	decl[elementIndex] = D3DDECL_END();

	return decl;
}


#ifdef WWDEBUG
// @bugfix Ronin 23/01/2026 DX9: Debug draw context pointer must be stable (avoid dangling pointers).
static thread_local char g_debugDrawContextBuf[256] = {};
static thread_local const char* g_debugDrawContext = nullptr;

void DX8Wrapper::Set_Debug_Draw_Context(const char* label)
{
	if (!label) {
		g_debugDrawContextBuf[0] = '\0';
		g_debugDrawContext = nullptr;
		return;
	}

	strncpy_s(g_debugDrawContextBuf, sizeof(g_debugDrawContextBuf), label, _TRUNCATE);
	g_debugDrawContext = g_debugDrawContextBuf;
}

void DX8Wrapper::Clear_Debug_Draw_Context()
{
	g_debugDrawContextBuf[0] = '\0';
	g_debugDrawContext = nullptr;
}

const char* DX8Wrapper::Get_Debug_Draw_Context()
{
	return g_debugDrawContext;
}
#endif

#ifdef WWDEBUG
struct PipelineStateHistory {
	DWORD lastFVF = 0;
	IDirect3DVertexDeclaration9* lastDecl = nullptr;
	const char* lastSetFVFCaller = nullptr;
	const char* lastSetDeclCaller = nullptr;
	bool loggedConflictOnce = false;
};
static PipelineStateHistory g_stateHistory;
#endif

#ifdef _DEBUG
// @bugfix Ronin 21/01/2026 DX9: Ensure the device IB matches wrapper's expected IB immediately before DIP.
// @refactor Ronin 08/02/2026 DX9: Stripped verbose logging; fail-only diagnostics remain.
static void Ensure_Device_IB_Matches_Wrapper_Expected(const char* where)
{
	IDirect3DDevice9* dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;

	RenderStateStruct rs;
	DX8Wrapper::Get_Render_State(rs);

	// ===========
	//  Ensure IB
	// ===========
	IDirect3DIndexBuffer9* expectedIB = nullptr;
	if (rs.index_buffer &&
		(rs.index_buffer_type == BUFFER_TYPE_DX8 || rs.index_buffer_type == BUFFER_TYPE_DYNAMIC_DX8)) {
		auto* dx8ib = static_cast<DX8IndexBufferClass*>(const_cast<IndexBufferClass*>(rs.index_buffer));
		expectedIB = static_cast<IDirect3DIndexBuffer9*>(dx8ib->Get_DX8_Index_Buffer());
	}

	IDirect3DIndexBuffer9* boundIB = nullptr;
	dev->GetIndices(&boundIB);

	if (boundIB != expectedIB) {
		DX8CALL(SetIndices(expectedIB));

		WWDEBUG_SAY((
			"IA ENSURE(IB) [Frame %lu] where=%s expected=%p bound=%p type=%u",
			DX8Wrapper::FrameCount,
			where ? where : "?",
			expectedIB,
			boundIB,
			(unsigned)rs.index_buffer_type));
	}

	if (boundIB) boundIB->Release();

	// ==============
	// Ensure Stream0
	// ==============
	IDirect3DVertexBuffer9* expectedVB0 = nullptr;
	UINT expectedOffset0 = 0;
	UINT expectedStride0 = 0;
	bool expectVBPointerMatch = true;

	if (rs.vertex_buffers[0] && rs.vertex_buffer_types[0] == BUFFER_TYPE_DX8) {
		auto* vb0 = static_cast<DX8VertexBufferClass*>(rs.vertex_buffers[0]);
		expectedVB0 = vb0->Get_DX8_Vertex_Buffer();
		expectedStride0 = (UINT)vb0->FVF_Info().Get_FVF_Size();
		expectedOffset0 = 0;
	}
	else if (rs.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8) {
		if (rs.vba_fvf != 0) {
			FVFInfoClass fi(rs.vba_fvf);
			expectedStride0 = (UINT)fi.Get_FVF_Size();
			expectVBPointerMatch = false;
		}
	}

	IDirect3DVertexBuffer9* boundVB0 = nullptr;
	UINT boundOff0 = 0, boundStride0 = 0;
	dev->GetStreamSource(0, &boundVB0, &boundOff0, &boundStride0);

	if (expectedStride0 != 0) {
		const bool strideMismatch = (boundStride0 != expectedStride0);
		const bool vbMismatch = expectVBPointerMatch && (boundVB0 != expectedVB0);

		if (strideMismatch || vbMismatch) {
			IDirect3DVertexBuffer9* vbToSet = expectVBPointerMatch ? expectedVB0 : boundVB0;
			DX8Wrapper::Force_Stream0(vbToSet, expectedOffset0, expectedStride0);

			WWDEBUG_SAY((
				"IA ENSURE(VB0) [Frame %lu] where=%s expStr=%u boundStr=%u",
				DX8Wrapper::FrameCount,
				where ? where : "?",
				(unsigned)expectedStride0,
				(unsigned)boundStride0));
		}
	}

	if (boundVB0) boundVB0->Release();

	// ==============
	// Ensure Texture0 for SKIN context
	// ==============
	const char* ctx = DX8Wrapper::Get_Debug_Draw_Context();
	const bool isSkinCtx = (ctx != nullptr) && (strstr(ctx, "SKIN ") == ctx);

	if (isSkinCtx) {
		IDirect3DBaseTexture9* devT0 = nullptr;
		dev->GetTexture(0, &devT0);

		TextureBaseClass* expectedObj = const_cast<TextureBaseClass*>(rs.Textures[0]);
		IDirect3DBaseTexture9* expectedT0 = expectedObj ? expectedObj->Peek_D3D_Base_Texture() : nullptr;

		if (devT0 != expectedT0) {
			if (expectedObj) {
				expectedObj->Apply(0);
			}
			else {
				TextureBaseClass::Apply_Null(0);
			}
		}

		if (devT0) devT0->Release();
	}
}
#endif // _DEBUG

#ifdef _DEBUG

#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

namespace DX8WrapperLayoutBinding
{
	bool g_layoutBindingAllowed = false;

	void Report_LayoutBindingViolation(const char* api, const char* callsite)
	{
		WWDEBUG_SAY(("IA LAYOUT BIND VIOLATION: API=%s callsite=%s",
			api ? api : "(null)",
			callsite ? callsite : "(null)"));
	}
}

#endif



#ifdef _DEBUG
// @debug Ronin 23/01/2026 DX9: Detect "decl bound while wrapper expects FVF mode" (mode mismatch).
// @refactor Ronin 08/02/2026 DX9: Stripped verbose logging; edge-trigger fail-only.
static void Track_Decl_Bound_While_Wrapper_Expects_FVF(const char* where)
{
	IDirect3DDevice9* dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;

	// Once-per-frame to avoid spam
	static unsigned long s_lastFrameChecked = 0;
	if (DX8Wrapper::FrameCount == s_lastFrameChecked) return;
	s_lastFrameChecked = DX8Wrapper::FrameCount;

	RenderStateStruct rs;
	DX8Wrapper::Get_Render_State(rs);

	const bool wrapperThinksDecl = (rs.currentDecl != nullptr) || (rs.expectedDecl != nullptr);
	const DWORD wrapperFVF = (rs.currentFVF != 0) ? rs.currentFVF : rs.expectedFVF;
	const bool wrapperExpectsFVF = (!wrapperThinksDecl && wrapperFVF != 0);

	IDirect3DVertexDeclaration9* devDecl = nullptr;
	dev->GetVertexDeclaration(&devDecl);

	const bool bad = wrapperExpectsFVF && (devDecl != nullptr);

	static bool s_wasBad = false;
	if (bad && !s_wasBad) {
		WWDEBUG_SAY(("IA MODE MISMATCH [Frame %lu] where=%s wrapperFVF=0x%08X devDecl=%p",
			DX8Wrapper::FrameCount,
			where ? where : "?",
			(unsigned)wrapperFVF,
			devDecl));
	}
	s_wasBad = bad;

	if (devDecl) devDecl->Release();
}
#endif // _DEBUG

// @feature Ronin 01/03/2026 DX9: Query and configure multisampling for antialiasing.
// @feature Ronin 01/03/2026 DX9: Query and configure multisampling for antialiasing.
// Raw AA values are sample counts: 0 / 2 / 4 / 8.
static void Configure_Multisampling(D3DPRESENT_PARAMETERS& pp, IDirect3D9* d3d, UINT adapter, D3DDEVTYPE devType)
{
	pp.MultiSampleType = D3DMULTISAMPLE_NONE;
	pp.MultiSampleQuality = 0;

	if (!d3d) {
		return;
	}

	const int aaLevel = DX8Wrapper::Get_Anti_Aliasing_Level();
	if (aaLevel <= 0) {
		return;
	}

	const D3DMULTISAMPLE_TYPE* types = nullptr;
	int typeCount = 0;

	static const D3DMULTISAMPLE_TYPE types2x[] = {
		D3DMULTISAMPLE_2_SAMPLES,
	};

	static const D3DMULTISAMPLE_TYPE types4x[] = {
		D3DMULTISAMPLE_4_SAMPLES,
		D3DMULTISAMPLE_2_SAMPLES,
	};

	static const D3DMULTISAMPLE_TYPE types8x[] = {
		D3DMULTISAMPLE_8_SAMPLES,
		D3DMULTISAMPLE_4_SAMPLES,
		D3DMULTISAMPLE_2_SAMPLES,
	};

	switch (aaLevel) {
	case 2:
		types = types2x;
		typeCount = sizeof(types2x) / sizeof(types2x[0]);
		break;

	case 4:
		types = types4x;
		typeCount = sizeof(types4x) / sizeof(types4x[0]);
		break;

	case 8:
	default:
		types = types8x;
		typeCount = sizeof(types8x) / sizeof(types8x[0]);
		break;
	}

	for (int i = 0; i < typeCount; ++i) {
		const D3DMULTISAMPLE_TYPE msType = types[i];

		DWORD backBufferQualityLevels = 0;
		HRESULT hr = d3d->CheckDeviceMultiSampleType(
			adapter,
			devType,
			pp.BackBufferFormat,
			pp.Windowed,
			msType,
			&backBufferQualityLevels);

		if (FAILED(hr) || backBufferQualityLevels == 0) {
			continue;
		}

		DWORD depthQualityLevels = 0;
		hr = d3d->CheckDeviceMultiSampleType(
			adapter,
			devType,
			pp.AutoDepthStencilFormat,
			pp.Windowed,
			msType,
			&depthQualityLevels);

		if (FAILED(hr) || depthQualityLevels == 0) {
			continue;
		}

		const DWORD qualityLevels = (backBufferQualityLevels < depthQualityLevels)
			? backBufferQualityLevels
			: depthQualityLevels;

		if (qualityLevels == 0) {
			continue;
		}

		pp.MultiSampleType = msType;
		pp.MultiSampleQuality = qualityLevels - 1;
		pp.SwapEffect = D3DSWAPEFFECT_DISCARD;

		WWDEBUG_SAY(("MSAA: Enabled request=%d samples=%d quality=%d",
			aaLevel,
			(int)msType,
			(int)pp.MultiSampleQuality));

		return;
	}

	WWDEBUG_SAY(("MSAA: Requested mode=%d but no supported multisample mode was found",
		aaLevel));
}

/***********************************************************************************
**
** DX8Wrapper Implementation
**
***********************************************************************************/

// Ronin @bugfix 09/01/2026 DX9: Force stream0 binding with explicit stride (keeps wrapper tracking coherent)
void DX8Wrapper::Force_Stream0(IDirect3DVertexBuffer9* vb, UINT offset, UINT stride)
{
	IDirect3DDevice9* dev = _Get_D3D_Device8();
	if (!dev) return;

	dev->SetStreamSource(0, vb, offset, stride);
	DX8_RECORD_DX8_CALLS();

	// Keep wrapper bookkeeping consistent (stream0 only).
	// NOTE: render_state.vertex_buffers[] tracks engine buffers, not raw D3D vbs,
	// so do not touch it here. This function exists specifically to avoid stale stride.
}

static const char* Get_DX8_Error_Code_Name(HRESULT res)
{
#define CASE_LABEL(x) case x: return #x;

	switch (res)
	{
	CASE_LABEL(D3D_OK)
	CASE_LABEL(D3DERR_WRONGTEXTUREFORMAT)
	CASE_LABEL(D3DERR_UNSUPPORTEDCOLOROPERATION)
	CASE_LABEL(D3DERR_UNSUPPORTEDCOLORARG)
	CASE_LABEL(D3DERR_UNSUPPORTEDALPHAOPERATION)
	CASE_LABEL(D3DERR_UNSUPPORTEDALPHAARG)
	CASE_LABEL(D3DERR_TOOMANYOPERATIONS)
	CASE_LABEL(D3DERR_CONFLICTINGTEXTUREFILTER)
	CASE_LABEL(D3DERR_UNSUPPORTEDFACTORVALUE)
	CASE_LABEL(D3DERR_CONFLICTINGRENDERSTATE)
	CASE_LABEL(D3DERR_UNSUPPORTEDTEXTUREFILTER)
	CASE_LABEL(D3DERR_CONFLICTINGTEXTUREPALETTE)
	CASE_LABEL(D3DERR_DRIVERINTERNALERROR)
	CASE_LABEL(D3DERR_NOTFOUND)
	CASE_LABEL(D3DERR_MOREDATA)
	CASE_LABEL(D3DERR_DEVICELOST)
	CASE_LABEL(D3DERR_DEVICENOTRESET)
	CASE_LABEL(D3DERR_NOTAVAILABLE)
	CASE_LABEL(D3DERR_OUTOFVIDEOMEMORY)
	CASE_LABEL(D3DERR_INVALIDDEVICE)
	CASE_LABEL(D3DERR_INVALIDCALL)
	CASE_LABEL(D3DERR_DRIVERINVALIDCALL)
	CASE_LABEL(E_OUTOFMEMORY)
	CASE_LABEL(E_INVALIDARG)
	CASE_LABEL(E_NOTIMPL)
	CASE_LABEL(E_FAIL)
	}

#undef CASE_LABEL

	return "Unknown";
}

void Log_DX8_ErrorCode(unsigned res)
{
	// Ronin @build 24/10/2025 DX9: D3DXGetErrorStringA removed, use DXGetErrorString9A
	const char* errorString = DXGetErrorString9A(res);
	if (errorString) {
		WWDEBUG_SAY(("%s", errorString));
	}

	WWASSERT(0);
}

void Non_Fatal_Log_DX8_ErrorCode(unsigned res,const char * file,int line)
{
	// Ronin @build 24/10/2025 DX9: D3DXGetErrorStringA removed, use DXGetErrorString9A
	const char* errorString = DXGetErrorString9A(res);
	if (errorString) {
		WWDEBUG_SAY(("DX8 Error: %s, File: %s, Line: %d", errorString, file, line));
	}
}

// TheSuperHackers @info helmutbuhler 14/04/2025
// Helper function that moves x and y such that the inner rect fits into the outer rect.
// If the inner rect already is in the outer rect, then this does nothing.
// If the inner rect is larger than the outer rect, then the inner rect will be aligned to the top left of the outer rect.
void MoveRectIntoOtherRect(const RECT& inner, const RECT& outer, int* x, int* y)
{
	int dx = 0;
	if (inner.right > outer.right)
		dx = outer.right-inner.right;
	if (inner.left < outer.left)
		dx = outer.left-inner.left;

	int dy = 0;
	if (inner.bottom > outer.bottom)
		dy = outer.bottom-inner.bottom;
	if (inner.top < outer.top)
		dy = outer.top-inner.top;

	*x += dx;
	*y += dy;
}


bool DX8Wrapper::Init(void * hwnd, bool lite)
{
	WWASSERT(!IsInitted);

	// zero memory
	memset(Textures,0,sizeof(IDirect3DBaseTexture8*)*MAX_TEXTURE_STAGES);
	memset(RenderStates,0,sizeof(unsigned)*256);
	memset(TextureStageStates, 0, sizeof(TextureStageStates));
	memset(SamplerStates, 0, sizeof(SamplerStates));

	memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);

	// @bugfix Ronin 15/01/2026 DX9: Do not memset RenderStateStruct (non-POD; has ctor/dtor and ref-counted members)
	// memset(&render_state,0,sizeof(RenderStateStruct));
	render_state = RenderStateStruct();

	memset(Shadow_Map,0,sizeof(ZTextureClass*)*MAX_SHADOW_MAPS);

	/*
	** Initialize all variables!
	*/
	_Hwnd = (HWND)hwnd;
	_MainThreadID=ThreadClass::_Get_Current_Thread_ID();
	WWDEBUG_SAY(("DX8Wrapper main thread: 0x%x",_MainThreadID));
	CurRenderDevice = -1;
	ResolutionWidth = DEFAULT_RESOLUTION_WIDTH;
	ResolutionHeight = DEFAULT_RESOLUTION_HEIGHT;
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );
	BitDepth = DEFAULT_BIT_DEPTH;
	IsWindowed = false;
	DX8Wrapper_IsWindowed = false;

	// @refactor Ronin 26/03/2026 DX9: Keep wrapper light-enable cache aligned with MAX_LIGHTS.
	for (unsigned light = 0; light < MAX_LIGHTS; ++light) {
		CurrentDX8LightEnables[light] = false;
	}

	//old_vertex_shader; TODO
	//old_sr_shader;
	//current_shader;

	//world_identity;
	//CurrentFogColor;

	D3DInterface = nullptr;
	D3DDevice = nullptr;

	WWDEBUG_SAY(("Reset DX8Wrapper statistics"));
	Reset_Statistics();

	Invalidate_Cached_Render_States();

	if (!lite) {
		D3D8Lib = LoadLibrary("D3D9.DLL");

		if (D3D8Lib == nullptr) return false;	// Return false at this point if init failed

		Direct3DCreate8Ptr = (Direct3DCreate8Type) GetProcAddress(D3D8Lib, "Direct3DCreate9");
		if (Direct3DCreate8Ptr == nullptr)
		{
			Shutdown();
			return false;
		}

		/*
		** Create the D3D interface object
		*/
		WWDEBUG_SAY(("Create Direct3D8"));
		{
			// TheSuperHackers @bugfix xezon 13/06/2025 Front load the system dbghelp.dll to prevent
			// the graphics driver from potentially loading the old game dbghelp.dll and then crashing the game process.
			DbgHelpGuard dbgHelpGuard;

			// Ronin @feature 08/11/2026 DX9Ex: the Ex interface when asked for and present; else the plain one, as before.
			IsEx = false;
			Direct3DCreate9ExType createEx = ExRequested ? (Direct3DCreate9ExType) GetProcAddress(D3D8Lib, "Direct3DCreate9Ex") : nullptr;
			IDirect3D9Ex *d3dEx = nullptr;
			if (createEx != nullptr && SUCCEEDED(createEx(D3D_SDK_VERSION, &d3dEx)) && d3dEx != nullptr)
			{
				D3DInterface = d3dEx;
				IsEx = true;
			}
			else
			{
				if (ExRequested)
				{
					WWDEBUG_SAY(("DX9Ex: Direct3DCreate9Ex unavailable or failed - plain Direct3D 9"));
				}
				D3DInterface = Direct3DCreate8Ptr(D3D_SDK_VERSION);		// TODO: handle failure cases...
			}
		}
		if (D3DInterface == nullptr) {
			WWDEBUG_SAY(("ERROR: Direct3DCreate9 returned NULL! D3D_SDK_VERSION=%d", D3D_SDK_VERSION));
			WWDEBUG_SAY(("Check: 1) Is DirectX 9 runtime installed? 2) Graphics driver issue?"));
			Shutdown();
			return(false);
		}
		IsInitted = true;

		/*
		** Enumerate the available devices
		*/
		WWDEBUG_SAY(("Enumerate devices"));
		Enumerate_Devices();
		WWDEBUG_SAY(("DX8Wrapper Init completed"));
	}

	return(true);
}

void DX8Wrapper::Shutdown()
{
	if (D3DDevice) {

		Set_Render_Target ((IDirect3DSurface8 *)nullptr);
		Release_Device();
	}

	if (D3DInterface) {
		D3DInterface->Release();
		D3DInterface=nullptr;

	}

	for (size_t i = 0; i < ARRAY_SIZE(Textures); ++i)
	{
		if (Textures[i])
		{
			Textures[i]->Release();
			Textures[i] = nullptr;
		}
	}

	// Ronin @refactor 27/02/2026 DX9: Removed dead code — second D3DInterface release block
	// was unreachable because D3DInterface was already nulled above.

	if (D3D8Lib) {
		FreeLibrary(D3D8Lib);
		D3D8Lib = nullptr;
	}

	_RenderDeviceNameTable.Clear();
	_RenderDeviceShortNameTable.Clear();
	_RenderDeviceDescriptionTable.Clear();

	DX8Caps::Shutdown();
	IsInitted = false;
}

void DX8Wrapper::Do_Onetime_Device_Dependent_Inits()
{
	/*
	** Set Global render states (some of which depend on caps)
	*/
	Compute_Caps(D3DFormat_To_WW3DFormat(DisplayFormat));

   /*
	** Initialize any other subsystems inside of WW3D
	*/
	MissingTexture::_Init();
	TextureFilterClass::_Init_Filters((TextureFilterClass::TextureFilterMode)WW3D::Get_Texture_Filter());
	TheDX8MeshRenderer.Init();
	SHD_INIT;
	BoxRenderObjClass::Init();
	VertexMaterialClass::Init();
	PointGroupClass::_Init(); // This needs the VertexMaterialClass to be initted
	ShatterSystem::Init();
	TextureLoader::Init();

	Set_Default_Global_Render_States();

	Init_Decl_Cache(D3DDevice);

	// Ronin @feature 18/02/2026 DX9: Initialize hardware instancing after device and mesh renderer are ready.
	TheDX8InstanceManager.Init();
}

// Ronin @feature 27/11/2025: Initialize vertex declaration cache
void DX8Wrapper::Init_Decl_Cache(IDirect3DDevice9* device)
{
	if (!device) {
		WWDEBUG_SAY(("Init_Decl_Cache: NULL device!"));
		return;
	}

	if (DeclCache) {
		WWDEBUG_SAY(("DeclCache already initialized!"));
		return;
	}

	DeclCache = new VertexDeclCache(device);
	WWDEBUG_SAY(("Vertex declaration cache initialized"));
}

void DX8Wrapper::Set_Default_Global_Render_States()
{
	DX8_THREAD_ASSERT();
	const D3DCAPS9& caps = Get_Current_Caps()->Get_DX8_Caps();

	// ========== DEPTH/STENCIL STATES ==========
	Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	Set_DX8_Render_State(D3DRS_DEPTHBIAS, 0);
	Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, 0);

	// ========== ALPHA BLENDING STATES ==========
	Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	Set_DX8_Render_State(D3DRS_BLENDOP, D3DBLENDOP_ADD);

	Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_ALPHAREF, 0);
	Set_DX8_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESSEQUAL);

	// ========== CULLING/SHADING ==========
	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	Set_DX8_Render_State(D3DRS_DITHERENABLE, FALSE);

	// ========== LIGHTING ==========
	Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, TRUE);
	Set_DX8_Render_State(D3DRS_SPECULARENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

	// ========== FOG ==========
	Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_RANGEFOGENABLE,
		(caps.RasterCaps & D3DPRASTERCAPS_FOGRANGE) ? TRUE : FALSE);
	Set_DX8_Render_State(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	Set_DX8_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);

	// ========== STENCIL ==========
	Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	Set_DX8_Render_State(D3DRS_STENCILREF, 0);
	Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0xffffffff);

	// ========== MISC STATES ==========
	Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0);
	Set_DX8_Render_State(D3DRS_CLIPPING, TRUE);
	Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000f);

	// ========== BUMP MAPPING (Only needed if bump mapping is used) ==========
		 // Ronin @refactor 13/11/2025: Moved to shader-specific setup, not global defaults

	//Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLSCALE, F2DW(1.0f));
	//Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLOFFSET, F2DW(0.0f));
	//Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT00, F2DW(1.0f));
	//Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT01, F2DW(0.0f));
	//Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT10, F2DW(0.0f));
  //Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT11, F2DW(1.0f));

	// ========== CRITICAL: PIXEL SHADER CLEANUP ==========
	// Ronin @bugfix 07/11/2025: DX9 requires explicit pixel shader NULL
	IDirect3DDevice9* pDev = DX8Wrapper::_Get_D3D_Device8();
	if (pDev) {
		DX8CALL(SetPixelShader(NULL));
		DX8CALL(SetVertexShader(NULL));      // Clear vertex shader
		//pDev->SetVertexDeclaration(NULL);  // Clear vertex declaration
	}

	// ========== CRITICAL: FIXED-FUNCTION TEXTURE STAGE SETUP ==========
	// Ronin @bugfix 07/11/2025: DX9 needs explicit fixed-function config
	int maxStages = CurrentCaps->Get_Max_Textures_Per_Pass();

	// Stage 0: Standard modulate with vertex color
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	// Stages 1+: Disable
	for (int i = 1; i < maxStages; i++) {
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXCOORDINDEX, i);
		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	}

	// ========== CRITICAL: SAMPLER STATE DEFAULTS ==========
	// Ronin @bugfix 07/11/2025: DX9 sampler states need explicit setup
	for (int i = 0; i < maxStages; i++) {
		Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		Set_DX8_Sampler_State(i, D3DSAMP_BORDERCOLOR, 0);

		Set_DX8_Sampler_State(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		Set_DX8_Sampler_State(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		Set_DX8_Sampler_State(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		Set_DX8_Sampler_State(i, D3DSAMP_MAXANISOTROPY, 1);
	}

	// @feature Ronin 01/03/2026 DX9: Enable multisampling antialiasing if device was created with MSAA
	if (_PresentParameters.MultiSampleType != D3DMULTISAMPLE_NONE) {
		Set_DX8_Render_State(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}

#ifdef _DEBUG
	//WWDEBUG_SAY(("Render state reset complete (PS=NULL, Stages configured, Samplers set)"));
#endif

//	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	// Set dither mode here?
}

//**********************************************************************************************
// Resets render states between rendering passes to prevent state leakage
// Ronin @build 07/11/2025: DX9 requires explicit state reset between passes
void DX8Wrapper::Reset_Pass_Render_States()
{
	DX8_THREAD_ASSERT();

	// ========== ALPHA BLENDING ==========
	Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);

	// ========== ALPHA TESTING ==========
	Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_ALPHAREF, 0);

	// Ronin @bugfix 17/01/2026: Keep pass reset consistent with global defaults (avoids subtle foliage/alpha-test diffs)
	Set_DX8_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESSEQUAL);

	// ========== DEPTH/STENCIL ==========
	Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	Set_DX8_Render_State(D3DRS_DEPTHBIAS, 0);
	Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, 0);

	Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);

	// ========== CULLING ==========
	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);

	// ========== PIXEL SHADER CLEANUP ==========
	// Ronin @bugfix 07/11/2025: DX9 requires explicit NULL
	IDirect3DDevice9* pDev = DX8Wrapper::_Get_D3D_Device8();
	if (pDev) {
		DX8CALL(SetPixelShader(NULL));
		DX8_RECORD_DX8_CALLS();
	}

	// ========== TEXTURE STAGE RESET ==========
		// Ronin @bugfix 16/12/2025: Clear all texture stages between passes
	int maxStages = CurrentCaps->Get_Max_Textures_Per_Pass();
	for (int i = 0; i < maxStages; i++) {
		Set_DX8_Texture(i, nullptr);
	}

	// Stage 0: Standard modulate
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	// Ronin @bugfix 13/11/2025: Reset texture coordinate selection
	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	// Stages 1+: Disable
	for (int i = 1; i < maxStages; i++) {
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}

	// ========== SAMPLER STATE RESET ==========
	// Ronin @bugfix 07/11/2025: DX9 sampler states must be reset
	for (int i = 0; i < maxStages; i++) {
		Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	}

#ifdef _DEBUG
	//WWDEBUG_SAY(("Pass render state reset complete"));
#endif
}

// Ronin @bugfix 14/09/2026 DX9: for stages a caller cleared with raw SetTexture(stage, nullptr). Both caches are set to
// match the device (nothing bound), so the next Set_Texture always reaches it.
void DX8Wrapper::Invalidate_Texture_State(unsigned firstStage, unsigned stageCount)
{
	for (unsigned stage = firstStage; stage < firstStage + stageCount && stage < MAX_TEXTURE_STAGES; ++stage)
	{
		if (Textures[stage] != nullptr)
		{
			Textures[stage]->Release();
			Textures[stage] = nullptr;
		}
		REF_PTR_RELEASE(render_state.Textures[stage]);
		render_state_changed |= (TEXTURE0_CHANGED << stage);
	}
}

void DX8Wrapper::Invalidate_Cached_Render_States()
{
	render_state_changed=0;

	// Ronin @performance 19/02/2026 DX9: Invalidate render state and TSS caches in bulk.
	// The sentinel value 0x12345678 ensures no cached comparison will match a real D3D value.
	{
		unsigned* rs = RenderStates;
		const int rsCount = sizeof(RenderStates) / sizeof(unsigned);
		for (int a = 0; a < rsCount; ++a) {
			rs[a] = 0x12345678;
		}
	}

	IDirect3DDevice9* pDev = _Get_D3D_Device8();

	for (int a = 0; a < MAX_TEXTURE_STAGES; ++a)
	{
		for (int b = 0; b < 33; ++b) {
			TextureStageStates[a][b] = 0x12345678;
		}
		for (int b = 0; b < 14; ++b) {
			SamplerStates[a][b] = 0x12345678;
		}

		if (pDev) {
			pDev->SetTexture(a, nullptr);
			DX8_RECORD_DX8_CALLS();
		}
		if (Textures[a] != nullptr) {
			Textures[a]->Release();
			Textures[a] = nullptr;
		}
	}

	// Ronin @bugfix 06/11/2025: DX9 requires explicit pixel shader cleanup during state invalidation
	//IDirect3DDevice9* pDev = _Get_D3D_Device8();
	if (pDev) {
		DX8CALL(SetPixelShader(NULL));
		DX8CALL(SetVertexShader(NULL));

		// Update wrapper tracking to match device state
		render_state.currentVS = nullptr;
		render_state.currentPS = nullptr;
	}

	ShaderClass::Invalidate();

	//Need to explicitly set render_state texture pointers to null. MW
	Release_Render_State();

	// (gth) clear the matrix shadows too
	memset(&DX8Transforms, 0, sizeof(DX8Transforms));
}

void DX8Wrapper::Do_Onetime_Device_Dependent_Shutdowns()
{
	/*
	** Shutdown ww3d systems
	*/
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);
	for (i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) REF_PTR_RELEASE(render_state.Textures[i]);

	// Ronin @feature 18/02/2026 DX9: Shutdown hardware instancing before releasing device resources.
	TheDX8InstanceManager.Shutdown();

		TextureLoader::Deinit();
	SortingRendererClass::Deinit();
	DynamicVBAccessClass::_Deinit();
	DynamicIBAccessClass::_Deinit();
	ShatterSystem::Shutdown();
	PointGroupClass::_Shutdown();
	VertexMaterialClass::Shutdown();
	BoxRenderObjClass::Shutdown();
	SHD_SHUTDOWN;
	TheDX8MeshRenderer.Shutdown();
	MissingTexture::_Deinit();

	delete CurrentCaps;
	CurrentCaps=nullptr;

}

// Ronin @feature 27/11/2025: Cleanup vertex declaration cache
void DX8Wrapper::Shutdown_Decl_Cache()
{
	if (DeclCache) {
		delete DeclCache;
		DeclCache = nullptr;
		WWDEBUG_SAY(("Vertex declaration cache destroyed"));
	}
}

// Ronin @feature 08/11/2026 DX9Ex: CreateDeviceEx / ResetEx take the fullscreen display mode spelled out; windowed, none.
static D3DDISPLAYMODEEX *Ex_Display_Mode(const D3DPRESENT_PARAMETERS &pp, D3DDISPLAYMODEEX &mode)
{
	if (pp.Windowed) return nullptr;
	::ZeroMemory(&mode, sizeof(mode));
	mode.Size = sizeof(mode);
	mode.Width = pp.BackBufferWidth;
	mode.Height = pp.BackBufferHeight;
	mode.RefreshRate = pp.FullScreen_RefreshRateInHz;
	mode.Format = pp.BackBufferFormat;
	mode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
	return &mode;
}

static HRESULT Create_Device_Plain_Or_Ex(IDirect3D9 *d3d, bool ex, UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags,
	D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
	if (!ex) return d3d->CreateDevice(adapter, type, hwnd, flags, pp, out);
	D3DDISPLAYMODEEX mode;
	IDirect3DDevice9Ex *dev = nullptr;
	const HRESULT hr = ((IDirect3D9Ex *)d3d)->CreateDeviceEx(adapter, type, hwnd, flags, pp, Ex_Display_Mode(*pp, mode), &dev);
	*out = dev;
	return hr;
}

D3DPOOL DX8Wrapper::_Get_Managed_Pool(D3DPOOL pool, DWORD *usage)
{
	if (pool != D3DPOOL_MANAGED || !IsEx) return pool;
	if (usage != nullptr) *usage |= D3DUSAGE_DYNAMIC;
	return D3DPOOL_DEFAULT;
}

bool DX8Wrapper::Create_Device()
{
	WWASSERT(D3DDevice==nullptr);	// for now, once you've created a device, you're stuck with it!
	DeviceResetFailed = false;

	D3DCAPS9 caps;
	if
	(
		FAILED
		(
			D3DInterface->GetDeviceCaps
			(
				CurRenderDevice,
				WW3D_DEVTYPE,
				&caps
			)
		)
	)
	{
		return false;
	}

	::ZeroMemory(&CurrentAdapterIdentifier, sizeof(D3DADAPTER_IDENTIFIER9));

	if
	(
		FAILED
		(
			D3DInterface->GetAdapterIdentifier
			(
				CurRenderDevice,
				0,  // Ronin @build 27/10/2025 DX9: D3DENUM_NO_WHQL_LEVEL removed - use 0 instead 
				&CurrentAdapterIdentifier
			)
			)
	)
	{
		return false;
	}

// Ronin @bugfix 09/11/2025: DX9 should prefer hardware vertex processing for performance
// Use MIXED for compatibility, or HARDWARE for max performance
// NOTE: D3DCREATE_PUREDEVICE disabled - causes GetRenderState() to return garbage
// and prevents proper state debugging. Re-enable only for final Release builds.

	Vertex_Processing_Behavior = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ?
		D3DCREATE_MIXED_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	// enable this when all 'get' dx calls are removed KJM
	/*if (caps.DevCaps&D3DDEVCAPS_PUREDEVICE)
	{
		Vertex_Processing_Behavior|=D3DCREATE_PUREDEVICE;
	}*/

#ifdef CREATE_DX8_MULTI_THREADED
	Vertex_Processing_Behavior|=D3DCREATE_MULTITHREADED;
	_DX8SingleThreaded=false;
#else
	_DX8SingleThreaded=true;
#endif

	if (DX8Wrapper_PreserveFPU)
		Vertex_Processing_Behavior |= D3DCREATE_FPU_PRESERVE;

#ifdef CREATE_DX8_FPU_PRESERVE
	Vertex_Processing_Behavior|=D3DCREATE_FPU_PRESERVE;
#endif

	// TheSuperHackers @bugfix xezon 13/06/2025 Front load the system dbghelp.dll to prevent
	// the graphics driver from potentially loading the old game dbghelp.dll and then crashing the game process.
	DbgHelpGuard dbgHelpGuard;

	// @feature Ronin 01/03/2026 DX9: Multisampling configuration calling.
	Configure_Multisampling(_PresentParameters, D3DInterface, CurRenderDevice, WW3D_DEVTYPE);

	HRESULT hr=Create_Device_Plain_Or_Ex	// Ronin @feature 08/11/2026 DX9Ex
	(
		D3DInterface,
		IsEx,
		CurRenderDevice,
		WW3D_DEVTYPE,
		_Hwnd,
		Vertex_Processing_Behavior,
		&_PresentParameters,
		&D3DDevice
	);

	if (FAILED(hr))
	{
		// The device selection may fail because the device lied that it supports 32 bit zbuffer with 16 bit
		// display. This happens at least on Voodoo2.

		if ((_PresentParameters.BackBufferFormat==D3DFMT_R5G6B5 ||
			_PresentParameters.BackBufferFormat==D3DFMT_X1R5G5B5 ||
			_PresentParameters.BackBufferFormat==D3DFMT_A1R5G5B5) &&
			(_PresentParameters.AutoDepthStencilFormat==D3DFMT_D32 ||
			_PresentParameters.AutoDepthStencilFormat==D3DFMT_D24S8 ||
			_PresentParameters.AutoDepthStencilFormat==D3DFMT_D24X8))
		{
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D16;
			hr = Create_Device_Plain_Or_Ex	// Ronin @feature 08/11/2026 DX9Ex
			(
				D3DInterface,
				IsEx,
				CurRenderDevice,
				WW3D_DEVTYPE,
				_Hwnd,
				Vertex_Processing_Behavior,
				&_PresentParameters,
				&D3DDevice
			);

			if (FAILED(hr))
			{
				return false;
			}
        }
		else
		{
				return false;
		}
	}

	dbgHelpGuard.deactivate();

	/*
	** Initialize all subsystems
	*/
	Do_Onetime_Device_Dependent_Inits();
	return true;
}

bool DX8Wrapper::Reset_Device(bool reload_assets)
{
	WWDEBUG_SAY(("Resetting device."));
	DX8_THREAD_ASSERT();
	if (!IsInitted || D3DDevice == nullptr) {
		WWDEBUG_SAY(("Device reset failed"));
		return false;
	}

		// Release all non-MANAGED stuff
		WW3D::_Invalidate_Textures();

		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i)
		{
			Set_Vertex_Buffer (nullptr,i);
		}
		Set_Index_Buffer (nullptr, 0);
		// Ronin @bugfix 08/10/2026 DX9: the dynamic VB's D3D reference survives Set_Vertex_Buffer(nullptr); it failed the reset.
		if (render_state.vba_d3d_vb) {
			render_state.vba_d3d_vb->Release();
			render_state.vba_d3d_vb = nullptr;
		}
		if (m_pCleanupHook) {
			m_pCleanupHook->ReleaseResources();
		}

		// Ronin @bugfix 27/02/2026 DX9: Release D3DPOOL_DEFAULT instancing resources before Reset
		TheDX8InstanceManager.Release_Resources();
		// Ronin @feature 13/09/2026 DX9: SSAO step 1. The INTZ scene depth is D3DPOOL_DEFAULT too; Begin rebuilds it.
		Release_Scene_Depth();

		DynamicVBAccessClass::_Deinit();
		DynamicIBAccessClass::_Deinit();
		DX8TextureManagerClass::Release_Textures();
		SHD_SHUTDOWN_SHADERS;

		// Reset frame count to reflect the flipping chain being reset by Reset()
		FrameCount = 0;

		memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
		memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);

		// @feature Ronin 01/03/2026 DX9: Re-apply MSAA configuration before Reset to ensure
		// the present parameters reflect the desired multisampling level.
		Configure_Multisampling(_PresentParameters, D3DInterface, CurRenderDevice, WW3D_DEVTYPE);

		HRESULT hr=_Get_D3D_Device8()->TestCooperativeLevel();
		if (hr != D3DERR_DEVICELOST )
		{
			if (IsEx)	// Ronin @feature 08/11/2026 DX9Ex: ResetEx takes the fullscreen mode; default-pool resources survive it
			{
				D3DDISPLAYMODEEX mode;
				hr = ((IDirect3DDevice9Ex *)_Get_D3D_Device8())->ResetEx(&_PresentParameters, Ex_Display_Mode(_PresentParameters, mode));
			}
			else
			{
				DX8CALL_HRES(Reset(&_PresentParameters),hr)
			}
			if (hr != D3D_OK)
				return false;	//reset failed.
		}

		if (reload_assets)
		{
			DX8TextureManagerClass::Recreate_Textures();
			if (m_pCleanupHook) {
				m_pCleanupHook->ReAcquireResources();
			}
		}

		// Ronin @bugfix 27/02/2026 DX9: Reinitialize instancing resources after Reset
		TheDX8InstanceManager.Init();

		Invalidate_Cached_Render_States();
		Set_Default_Global_Render_States();
		SHD_INIT_SHADERS;
		WWDEBUG_SAY(("Device reset completed"));
		return true;
	}

// Unbinds on the device because Set_Vertex_Buffer and Set_Index_Buffer are deferred.
void DX8Wrapper::Unbind_Device_Buffers()
{
	for (unsigned stream = 0; stream < MAX_VERTEX_STREAMS; ++stream)
	{
		DX8CALL(SetStreamSource(stream, nullptr, 0));
	}
	DX8CALL(SetIndices(nullptr, 0));
}

void DX8Wrapper::Release_Device()
{
	if (D3DDevice) {
		// Ronin @feature 13/09/2026 DX9: SSAO step 1. Before the device goes; the next device is probed afresh.
		Release_Scene_Depth();
		SceneDepthSupport = -1;
		NullTargetSupport = -1;

		if (!DeviceResetFailed)
		{
		for (int a=0;a<MAX_TEXTURE_STAGES;++a)
		{
			//release references to any textures that were used in last rendering call
			DX8CALL(SetTexture(a,nullptr));
		}

		DX8CALL(SetStreamSource(0, nullptr, 0, 0));	//release reference count on last rendered vertex buffer
		DX8CALL(SetIndices(nullptr));	//release reference count on last rendered index buffer


		/*
		** Release the current vertex and index buffers
		*/
		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i)
		{
			if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
			REF_PTR_RELEASE(render_state.vertex_buffers[i]);
		}
		if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.index_buffer);

		Shutdown_Decl_Cache();

		/*
		** Shutdown all subsystems
		*/
		Do_Onetime_Device_Dependent_Shutdowns();

		/*
		** Release the device
		*/

		D3DDevice->Release();
		D3DDevice=nullptr;
	}
}

void DX8Wrapper::Enumerate_Devices()
{
	DX8_Assert();

	int adapter_count = D3DInterface->GetAdapterCount();
	for (int adapter_index=0; adapter_index<adapter_count; adapter_index++) {

		D3DADAPTER_IDENTIFIER9 id;
		::ZeroMemory(&id, sizeof(D3DADAPTER_IDENTIFIER9));
		// Ronin @build 27/10/2025 DX9: D3DENUM_NO_WHQL_LEVEL removed - use 0 instead
		HRESULT res = D3DInterface->GetAdapterIdentifier(adapter_index, 0, &id);

		if (res == D3D_OK) {

			/*
			** Set up the render device description
			** TODO: Fill in more fields of the render device description?  (need some lookup tables)
			*/
			RenderDeviceDescClass desc;
			desc.set_device_name(id.Description);
			desc.set_driver_name(id.Driver);

			char buf[64];
			sprintf(buf,"%d.%d.%d.%d", //"%04x.%04x.%04x.%04x",
				HIWORD(id.DriverVersion.HighPart),
				LOWORD(id.DriverVersion.HighPart),
				HIWORD(id.DriverVersion.LowPart),
				LOWORD(id.DriverVersion.LowPart));

			desc.set_driver_version(buf);

			D3DInterface->GetDeviceCaps(adapter_index,WW3D_DEVTYPE,&desc.Caps);
           		// Ronin @build 28/10/2025 DX9: D3DENUM_NO_WHQL_LEVEL removed - use 0 instead
			D3DInterface->GetAdapterIdentifier(adapter_index, 0, &desc.AdapterIdentifier);
			DX8Caps dx8caps(D3DInterface,desc.Caps,WW3D_FORMAT_UNKNOWN,desc.AdapterIdentifier);

			/*
			** Enumerate the resolutions
			*/
			desc.reset_resolution_list();
			// Ronin @build 28/10/2025 DX9: GetAdapterModeCount now requires D3DFORMAT parameter
			// We enumerate all common display formats
			D3DFORMAT display_formats[] = {
				D3DFMT_X8R8G8B8,  // 32-bit
				D3DFMT_A8R8G8B8,  // 32-bit with alpha
				D3DFMT_R5G6B5,    // 16-bit
				D3DFMT_X1R5G5B5   // 16-bit
			};

			for (int fmt_idx = 0; fmt_idx < 4; fmt_idx++) {
				D3DFORMAT current_format = display_formats[fmt_idx];
				int mode_count = D3DInterface->GetAdapterModeCount(adapter_index, current_format);

				for (int mode_index = 0; mode_index < mode_count; mode_index++) {
					D3DDISPLAYMODE d3dmode;
					::ZeroMemory(&d3dmode, sizeof(D3DDISPLAYMODE));

					//HRESULT res = D3DInterface->EnumAdapterModes(adapter_index, display_formats[fmt], mode_index, &d3dmode);
					HRESULT res = D3DInterface->EnumAdapterModes(adapter_index, current_format, mode_index, &d3dmode);

					if (res == D3D_OK) {
						int bits = 0;
						switch (d3dmode.Format)
						{
						case D3DFMT_R8G8B8:
						case D3DFMT_A8R8G8B8:
						case D3DFMT_X8R8G8B8:		bits = 32; break;

						case D3DFMT_R5G6B5:
						case D3DFMT_X1R5G5B5:		bits = 16; break;
						}

						// Some cards fail in certain modes, DX8Caps keeps list of those.
						if (!dx8caps.Is_Valid_Display_Format(d3dmode.Width, d3dmode.Height, D3DFormat_To_WW3DFormat(d3dmode.Format))) {
							bits = 0;
						}

						/*
						** If we recognize the format, add it to the list
						** TODO: should we handle more formats?  will any cards report more than 24 or 16 bit?
						*/
						if (bits != 0) {
							desc.add_resolution(d3dmode.Width, d3dmode.Height, bits);
						}
					}
				}
			}

			// IML: If the device has one or more valid resolutions add it to the device list.
			// NOTE: Testing has shown that there are drivers with zero resolutions.
			if (desc.Enumerate_Resolutions().Count() > 0) {

				/*
				** Set up the device name
				*/
				StringClass device_name(id.Description,true);
				_RenderDeviceNameTable.Add(device_name);
				_RenderDeviceShortNameTable.Add(device_name);	// for now, just add the same name to the "pretty name table"

				/*
				** Add the render device to our table
				*/
				_RenderDeviceDescriptionTable.Add(desc);
			}
		}
	}
}

bool DX8Wrapper::Set_Any_Render_Device()
{
	// Try fullscreen first
	int dev_number = 0;
	for (; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,0,false)) {
			return true;
		}
	}

	// Then windowed
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,1,false)) {
			return true;
		}
	}

	return false;
}

bool DX8Wrapper::Set_Render_Device
(
	const char * dev_name,
	int width,
	int height,
	int bits,
	int windowed,
	bool resize_window
)
{
	for ( int dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if ( strcmp( dev_name, _RenderDeviceNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}

		if ( strcmp( dev_name, _RenderDeviceShortNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}
	}
	return false;
}

void DX8Wrapper::Get_Format_Name(unsigned int format, StringClass *tex_format)
{
		*tex_format="Unknown";
		switch (format) {
		case D3DFMT_A8R8G8B8: *tex_format="D3DFMT_A8R8G8B8"; break;
		case D3DFMT_R8G8B8: *tex_format="D3DFMT_R8G8B8"; break;
		case D3DFMT_A4R4G4B4: *tex_format="D3DFMT_A4R4G4B4"; break;
		case D3DFMT_A1R5G5B5: *tex_format="D3DFMT_A1R5G5B5"; break;
		case D3DFMT_R5G6B5: *tex_format="D3DFMT_R5G6B5"; break;
		case D3DFMT_L8: *tex_format="D3DFMT_L8"; break;
		case D3DFMT_A8: *tex_format="D3DFMT_A8"; break;
		case D3DFMT_P8: *tex_format="D3DFMT_P8"; break;
		case D3DFMT_X8R8G8B8: *tex_format="D3DFMT_X8R8G8B8"; break;
		case D3DFMT_X1R5G5B5: *tex_format="D3DFMT_X1R5G5B5"; break;
		case D3DFMT_R3G3B2: *tex_format="D3DFMT_R3G3B2"; break;
		case D3DFMT_A8R3G3B2: *tex_format="D3DFMT_A8R3G3B2"; break;
		case D3DFMT_X4R4G4B4: *tex_format="D3DFMT_X4R4G4B4"; break;
		case D3DFMT_A8P8: *tex_format="D3DFMT_A8P8"; break;
		case D3DFMT_A8L8: *tex_format="D3DFMT_A8L8"; break;
		case D3DFMT_A4L4: *tex_format="D3DFMT_A4L4"; break;
		case D3DFMT_V8U8: *tex_format="D3DFMT_V8U8"; break;
		case D3DFMT_L6V5U5: *tex_format="D3DFMT_L6V5U5"; break;
		case D3DFMT_X8L8V8U8: *tex_format="D3DFMT_X8L8V8U8"; break;
		case D3DFMT_Q8W8V8U8: *tex_format="D3DFMT_Q8W8V8U8"; break;
		case D3DFMT_V16U16: *tex_format="D3DFMT_V16U16"; break;
		case D3DFMT_W11V11U10: *tex_format="D3DFMT_W11V11U10"; break;
		case D3DFMT_UYVY: *tex_format="D3DFMT_UYVY"; break;
		case D3DFMT_YUY2: *tex_format="D3DFMT_YUY2"; break;
		case D3DFMT_DXT1: *tex_format="D3DFMT_DXT1"; break;
		case D3DFMT_DXT2: *tex_format="D3DFMT_DXT2"; break;
		case D3DFMT_DXT3: *tex_format="D3DFMT_DXT3"; break;
		case D3DFMT_DXT4: *tex_format="D3DFMT_DXT4"; break;
		case D3DFMT_DXT5: *tex_format="D3DFMT_DXT5"; break;
		case D3DFMT_D16_LOCKABLE: *tex_format="D3DFMT_D16_LOCKABLE"; break;
		case D3DFMT_D32: *tex_format="D3DFMT_D32"; break;
		case D3DFMT_D15S1: *tex_format="D3DFMT_D15S1"; break;
		case D3DFMT_D24S8: *tex_format="D3DFMT_D24S8"; break;
		case D3DFMT_D16: *tex_format="D3DFMT_D16"; break;
		case D3DFMT_D24X8: *tex_format="D3DFMT_D24X8"; break;
		case D3DFMT_D24X4S4: *tex_format="D3DFMT_D24X4S4"; break;
		default:	break;
		}
}

void DX8Wrapper::Resize_And_Position_Window()
{
	// Get the current dimensions of the 'render area' of the window
	RECT rect = { 0 };
	::GetClientRect (_Hwnd, &rect);

	// Is the window the correct size for this resolution?
	if ((rect.right-rect.left) != ResolutionWidth ||
			(rect.bottom-rect.top) != ResolutionHeight) {

		// Calculate what the main window's bounding rectangle should be to
		// accommodate this resolution
		rect.left = 0;
		rect.top = 0;
		rect.right = ResolutionWidth;
		rect.bottom = ResolutionHeight;
		DWORD dwstyle = ::GetWindowLong (_Hwnd, GWL_STYLE);
		AdjustWindowRect (&rect, dwstyle, FALSE);
		int width = rect.right-rect.left;
		int height = rect.bottom-rect.top;

		// Resize the window to fit this resolution
		if (!IsWindowed)
		{
#if defined(GENONLINE_WINDOWED_FULLSCREEN)
            // Assume hwnd is your application?s window handle
            LONG_PTR style = GetWindowLongPtr(_Hwnd, GWL_STYLE);

            // Remove all overlapped-window styles and add WS_POPUP
            style &= ~(WS_OVERLAPPEDWINDOW);
            style |= WS_POPUP;

            // Apply the new style
            SetWindowLongPtr(_Hwnd, GWL_STYLE, style);

            // Move window to (0,0) with the monitor?s resolution:
            SetWindowPos(
                _Hwnd,
                HWND_TOP,
                0,
                0,
                width,
                height,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER
            );
#else
            ::SetWindowPos(_Hwnd, HWND_TOPMOST, 0, 0, width, height, SWP_NOSIZE | SWP_NOMOVE);
#endif

			DEBUG_LOG(("Window resized to w:%d h:%d", width, height));
		}
		else
		{
			// TheSuperHackers @feature helmutbuhler 14/04/2025
			// Center the window in the workarea of the monitor it is on.
			MONITORINFO mi = {sizeof(MONITORINFO)};
			GetMonitorInfo(MonitorFromWindow(_Hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
			int left = (mi.rcWork.left + mi.rcWork.right - width) / 2;
			int top  = (mi.rcWork.top + mi.rcWork.bottom - height) / 2;

			// TheSuperHackers @feature helmutbuhler 14/04/2025
			// Move the window to try fit it into the monitor area, if one of its dimensions is larger than the work area.
			// Otherwise align the window to the top left edges, if it is even larger than the monitor area.
			RECT rectClient;
			rectClient.left = left - rect.left;
			rectClient.top = top - rect.top;
			rectClient.right = rectClient.left + ResolutionWidth;
			rectClient.bottom = rectClient.top + ResolutionHeight;
			MoveRectIntoOtherRect(rectClient, mi.rcMonitor, &left, &top);

			::SetWindowPos (_Hwnd, nullptr, left, top, width, height, SWP_NOZORDER);

			DEBUG_LOG(("Window positioned to x:%d y:%d, resized to w:%d h:%d", left, top, width, height));
		}
	}
}

bool DX8Wrapper::Set_Render_Device(int dev, int width, int height, int bits, int windowed,
								   bool resize_window,bool reset_device, bool restore_assets)
{
	WWASSERT(IsInitted);
	WWASSERT(dev >= -1);
	WWASSERT(dev < _RenderDeviceNameTable.Count());

	/*
	** If user has never selected a render device, start out with device 0
	*/
	if ((CurRenderDevice == -1) && (dev == -1)) {
		CurRenderDevice = 0;
	}
	else if (dev != -1) {
		CurRenderDevice = dev;
	}

	/*
	** If user doesn't want to change res, set the res variables to match the
	** current resolution
	*/
	if (width != -1)		ResolutionWidth = width;
	if (height != -1)		ResolutionHeight = height;

	if (bits != -1)		BitDepth = bits;
	if (windowed != -1)	IsWindowed = (windowed != 0);
	DX8Wrapper_IsWindowed = IsWindowed;

	WWDEBUG_SAY(("Attempting Set_Render_Device: name: %s (%s:%s), width: %d, height: %d, windowed: %d",
		_RenderDeviceNameTable[CurRenderDevice].str(),_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Name(),
		_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Version(),ResolutionWidth,ResolutionHeight,(IsWindowed ? 1 : 0)));

#ifdef _WIN32
	// PWG 4/13/2000 - changed so that if you say to resize the window it resizes
	// regardless of whether its windowed or not as OpenGL resizes its self around
	// the caption and edges of the window type you provide, so its important to
	// push the client area to be the size you really want.
	// if ( resize_window && windowed ) {
	if (resize_window) {
		Resize_And_Position_Window();
	}
#endif
	//must be either resetting existing device or creating a new one.
	WWASSERT(reset_device || D3DDevice == nullptr);

	/*
	** Initialize values for D3DPRESENT_PARAMETERS members.
	*/
	::ZeroMemory(&_PresentParameters, sizeof(D3DPRESENT_PARAMETERS));

	_PresentParameters.BackBufferWidth = ResolutionWidth;
	_PresentParameters.BackBufferHeight = ResolutionHeight;
	_PresentParameters.BackBufferCount = IsWindowed ? 1 : 2;

	//I changed this to discard all the time (even when full-screen) since that the most efficient. 07-16-03 MW:
	_PresentParameters.SwapEffect = IsWindowed ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
	_PresentParameters.hDeviceWindow = _Hwnd;
#if defined(GENONLINE_WINDOWED_FULLSCREEN)
    _PresentParameters.Windowed = TRUE; // always windowed now
#else
	_PresentParameters.Windowed = IsWindowed;
#endif

	_PresentParameters.EnableAutoDepthStencil = TRUE;				// Driver will attempt to match Z-buffer depth
	_PresentParameters.Flags=0;											// We're not going to lock the backbuffer

	_PresentParameters.PresentationInterval = IsWindowed ?
		D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE;  // Ronin @build 27/10/2025 DX9: Renamed from FullScreen_PresentationInterval
	_PresentParameters.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	/*
	** Set up the buffer formats.  Several issues here:
	** - if in windowed mode, the backbuffer must use the current display format.
	** - the depth buffer must use
	*/
	if (IsWindowed) {

		D3DDISPLAYMODE desktop_mode;
		::ZeroMemory(&desktop_mode, sizeof(D3DDISPLAYMODE));
		D3DInterface->GetAdapterDisplayMode( CurRenderDevice, &desktop_mode );

		DisplayFormat=_PresentParameters.BackBufferFormat = desktop_mode.Format;

		// In windowed mode, define the bitdepth from desktop mode (as it can't be changed)
		switch (_PresentParameters.BackBufferFormat) {
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8R8G8B8:
		case D3DFMT_R8G8B8: BitDepth=32; break;
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_R5G6B5: BitDepth=16; break;
		case D3DFMT_L8:
		case D3DFMT_A8:
		case D3DFMT_P8: BitDepth=8; break;
		default:
			// Unknown backbuffer format probably means the device can't do windowed
			return false;
		}

		if (BitDepth==32 && D3DInterface->CheckDeviceType(0,D3DDEVTYPE_HAL,desktop_mode.Format,D3DFMT_A8R8G8B8, TRUE) == D3D_OK)
		{
			//promote 32-bit modes to include destination alpha
			_PresentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
		}

		/*
		** Find a appropriate Z buffer
		*/
		if (!Find_Z_Mode(DisplayFormat,_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat))
		{
			// If opening 32 bit mode failed, try 16 bit, even if the desktop happens to be 32 bit
			if (BitDepth==32) {
				BitDepth=16;
				_PresentParameters.BackBufferFormat=D3DFMT_R5G6B5;
				if (!Find_Z_Mode(_PresentParameters.BackBufferFormat,_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat)) {
					_PresentParameters.AutoDepthStencilFormat=D3DFMT_UNKNOWN;
				}
			}
			else {
				_PresentParameters.AutoDepthStencilFormat=D3DFMT_UNKNOWN;
			}
		}

	}
	else {

		/*
		** Try to find a mode that matches the user's desired bit-depth.
		*/
		Find_Color_And_Z_Mode(ResolutionWidth,ResolutionHeight,BitDepth,&DisplayFormat,
			&_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat);
	}

	/*
	** Set default for depth stencil format if auto Z buffer failed.
	*/
	if (_PresentParameters.AutoDepthStencilFormat==D3DFMT_UNKNOWN) {
		if (BitDepth==32) {
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D32;
		}
		else {
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D16;
		}
	}

	StringClass displayFormat;
	StringClass backbufferFormat;

	Get_Format_Name(DisplayFormat,&displayFormat);
	Get_Format_Name(_PresentParameters.BackBufferFormat,&backbufferFormat);

	WWDEBUG_SAY(("Using Display/BackBuffer Formats: %s/%s",displayFormat.str(),backbufferFormat.str()));

	bool ret;

	if (reset_device)
	{
		WWDEBUG_SAY(("DX8Wrapper::Set_Render_Device is resetting the device."));
		ret = Reset_Device(restore_assets);	//reset device without restoring data - we're likely switching out of the app.
	}
	else
		ret = Create_Device();

	WWDEBUG_SAY(("Reset/Create_Device done, reset_device=%d, restore_assets=%d", reset_device, restore_assets));

	if (ret)
	{
		Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );
	}

// Ronin @debug 09/11/2025-08/02/2016 Refactored: Check if device starts in a clean state
#ifdef _DEBUG
	if (D3DDevice) {
		HRESULT testBegin = _Get_D3D_Device8()->BeginScene();
		if (SUCCEEDED(testBegin)) {
			_Get_D3D_Device8()->EndScene();
			s_inScene = false;
		}
		else if (testBegin == D3DERR_INVALIDCALL) {
			WWDEBUG_SAY(("Device already in scene after creation!"));
			s_inScene = true;
		}
	}
#endif

	return ret;
}

bool DX8Wrapper::Set_Next_Render_Device()
{
	int new_dev = (CurRenderDevice + 1) % _RenderDeviceNameTable.Count();
	return Set_Render_Device(new_dev);
}

bool DX8Wrapper::Toggle_Windowed()
{
#ifdef WW3D_DX8
	// State OK?
	assert (IsInitted);
	if (IsInitted) {

		// Get information about the current render device's resolutions
		const RenderDeviceDescClass &render_device = Get_Render_Device_Desc ();
		const DynamicVectorClass<ResolutionDescClass> &resolutions = render_device.Enumerate_Resolutions ();

		// Loop through all the resolutions supported by the current device.
		// If we aren't currently running under one of these resolutions,
		// then we should probably		 to the closest resolution before
		// toggling the windowed state.
		int curr_res = -1;
		for (int res = 0;
		     (res < resolutions.Count ()) && (curr_res == -1);
			  res ++) {

			// Is this the resolution we are looking for?
			if ((resolutions[res].Width == ResolutionWidth) &&
				 (resolutions[res].Height == ResolutionHeight) &&
				 (resolutions[res].BitDepth == BitDepth)) {
				curr_res = res;
			}
		}

		if (curr_res == -1) {

			// We don't match any of the standard resolutions,
			// so set the first resolution and toggle the windowed state.
			return Set_Device_Resolution (resolutions[0].Width,
								 resolutions[0].Height,
								 resolutions[0].BitDepth,
								 !IsWindowed, true);
		}
		else {

			// Toggle the windowed state
			return Set_Device_Resolution (-1, -1, -1, !IsWindowed, true);
		}
	}
#endif //WW3D_DX8

	return false;
}

void DX8Wrapper::Set_Swap_Interval(int swap)
{
	switch (swap) {
	case 0: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; break; // Ronin @build 27/10/2025 DX9: Renamed
	case 1: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE; break; // Ronin @build 27/10/2025 DX9: Renamed
	case 2: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_TWO; break; // Ronin @build 27/10/2025 DX9: Renamed
	case 3: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_THREE; break; // Ronin @build 27/10/2025 DX9: Renamed
	default: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE; break; // Ronin @build 27/10/2025 DX9: Renamed
	}

	WWDEBUG_SAY(("DX8Wrapper::Set_Swap_Interval is resetting the device."));
	Reset_Device();
}

int DX8Wrapper::Get_Swap_Interval()
{
	return _PresentParameters.PresentationInterval; // Ronin @build 27/10/2025 DX9: Renamed
}

bool DX8Wrapper::Has_Stencil()
{
	bool has_stencil = (_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24S8 ||
						_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24X4S4);
	return has_stencil;
}

int DX8Wrapper::Get_Render_Device_Count()
{
	return _RenderDeviceNameTable.Count();

}
int DX8Wrapper::Get_Render_Device()
{
	assert(IsInitted);
	return CurRenderDevice;
}

const RenderDeviceDescClass & DX8Wrapper::Get_Render_Device_Desc(int deviceidx)
{
	WWASSERT(IsInitted);

	if ((deviceidx == -1) && (CurRenderDevice == -1)) {
		CurRenderDevice = 0;
	}

	// if the device index is -1 then we want the current device
	if (deviceidx == -1) {
		WWASSERT(CurRenderDevice >= 0);
		WWASSERT(CurRenderDevice < _RenderDeviceNameTable.Count());
		return _RenderDeviceDescriptionTable[CurRenderDevice];
	}

	// We can only ask for multiple device information if the devices
	// have been detected.
	WWASSERT(deviceidx >= 0);
	WWASSERT(deviceidx < _RenderDeviceNameTable.Count());
	return _RenderDeviceDescriptionTable[deviceidx];
}

const char * DX8Wrapper::Get_Render_Device_Name(int device_index)
{
	device_index = device_index % _RenderDeviceShortNameTable.Count();
	return _RenderDeviceShortNameTable[device_index];
}

bool DX8Wrapper::Set_Device_Resolution(int width,int height,int bits,int windowed, bool resize_window)
{
	if (D3DDevice != nullptr) {

		if (width != -1) {
			_PresentParameters.BackBufferWidth = ResolutionWidth = width;
		}
		if (height != -1) {
			_PresentParameters.BackBufferHeight = ResolutionHeight = height;
		}
		if (resize_window)
		{
			Resize_And_Position_Window();
		}
#pragma message("TODO: support changing windowed status and changing the bit depth")
		WWDEBUG_SAY(("DX8Wrapper::Set_Device_Resolution is resetting the device."));
		return Reset_Device();
	}
	else {
		return false;
	}
}

void DX8Wrapper::Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	set_w = ResolutionWidth;
	set_h = ResolutionHeight;
	set_bits = BitDepth;
	set_windowed = IsWindowed;
}

void DX8Wrapper::Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	if (CurrentRenderTarget != nullptr) {
		D3DSURFACE_DESC info;
		CurrentRenderTarget->GetDesc (&info);

		set_w				= info.Width;
		set_h				= info.Height;
		set_bits			= BitDepth;		// should we get the actual bit depth of the target?
		set_windowed	= IsWindowed;	// this doesn't really make sense for render targets (shouldn't matter)...

	}
	else {
		Get_Device_Resolution (set_w, set_h, set_bits, set_windowed);
	}
}

bool DX8Wrapper::Registry_Save_Render_Device( const char * sub_key )
{
	int	width, height, depth;
	bool	windowed;
	Get_Device_Resolution(width, height, depth, windowed);
	return Registry_Save_Render_Device(sub_key, CurRenderDevice, ResolutionWidth, ResolutionHeight, BitDepth, IsWindowed, TextureBitDepth);
}

bool DX8Wrapper::Registry_Save_Render_Device( const char *sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth)
{
	RegistryClass * registry = W3DNEW RegistryClass( sub_key );
	WWASSERT( registry );

	if ( !registry->Is_Valid() ) {
		delete registry;
		WWDEBUG_SAY(( "Error getting Registry" ));
		return false;
	}

	registry->Set_String( VALUE_NAME_RENDER_DEVICE_NAME,
		_RenderDeviceShortNameTable[device] );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WIDTH,	width );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, height );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, depth );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, windowed );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, texture_depth );

	delete registry;
	return true;
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, bool resize_window )
{
	char	name[ 200 ];
	int	width,height,depth,windowed;

	if (	Registry_Load_Render_Device(	sub_key,
													name,
													sizeof(name),
													width,
													height,
													depth,
													windowed,
													TextureBitDepth) &&
			(*name != 0))
	{
		WWDEBUG_SAY(( "Device %s (%d X %d) %d bit windowed:%d", name,width,height,depth,windowed));

		if (TextureBitDepth==16 || TextureBitDepth==32) {
//			WWDEBUG_SAY(( "Texture depth %d", TextureBitDepth));
		}
		else {
			WWDEBUG_SAY(( "Invalid texture depth %d, switching to 16 bits", TextureBitDepth));
			TextureBitDepth=16;
		}

		if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) != true) {
			if (depth==16) depth=32;
			else depth=16;
			if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
				return true;
			}
			if (depth==16) depth=32;
			else depth=16;
			// we'll test resolutions down, so if start is 640, increase to begin with...
			if (width==640) {
				width=1024;
				height=768;
			}
			for(;;) {
				if (width>2048) {
					width=2048;
					height=1536;
				}
				else if (width>1920) {
					width=1920;
					height=1440;
				}
				else if (width>1600) {
					width=1600;
					height=1200;
				}
				else if (width>1280) {
					width=1280;
					height=1024;
				}
				else if (width>1024) {
					width=1024;
					height=768;
				}
				else if (width>800) {
					width=800;
					height=600;
				}
				else if (width!=640) {
					width=640;
					height=480;
				}
				else {
					return Set_Any_Render_Device();
				}
				for (int i=0;i<2;++i) {
					if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
						return true;
					}
					if (depth==16) depth=32;
					else depth=16;
				}
			}
		}

		return true;
	}

	WWDEBUG_SAY(( "Error getting Registry" ));

	return Set_Any_Render_Device();
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, char *device, int device_len, int &width, int &height, int &depth, int &windowed, int &texture_depth)
{
	RegistryClass registry( sub_key );

	if ( registry.Is_Valid() ) {
		registry.Get_String( VALUE_NAME_RENDER_DEVICE_NAME,
			device, device_len);

		width =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WIDTH, -1 );
		height =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, -1 );
		depth =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, -1 );
		windowed =	registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, -1 );
		texture_depth = registry.Get_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, -1 );
		return true;
	}
	*device=0;
	width=-1;
	height=-1;
	depth=-1;
	windowed=-1;
	texture_depth=-1;
	return false;
}


bool DX8Wrapper::Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,D3DFORMAT * set_colorbuffer,D3DFORMAT * set_backbuffer,D3DFORMAT * set_zmode)
{
	static D3DFORMAT _formats16[] =
	{
		D3DFMT_R5G6B5,
		D3DFMT_X1R5G5B5,
		D3DFMT_A1R5G5B5
	};

	static D3DFORMAT _formats32[] =
	{
		D3DFMT_A8R8G8B8,
		D3DFMT_X8R8G8B8,
		D3DFMT_R8G8B8,
	};

	/*
	** Select the table that we're going to use to search for a valid backbuffer format
	*/
	D3DFORMAT * format_table = nullptr;
	int format_count = 0;

	if (BitDepth == 16) {
		format_table = _formats16;
		format_count = sizeof(_formats16) / sizeof(D3DFORMAT);
	}
	else {
		format_table = _formats32;
		format_count = sizeof(_formats32) / sizeof(D3DFORMAT);
	}

	/*
	** now search for a valid format
	*/
	bool found = false;
	unsigned int mode = 0;

	int format_index=0;
	for (; format_index < format_count; format_index++) {
		found |= Find_Color_Mode(format_table[format_index],resx,resy,&mode);
		if (found) break;
	}

	if (!found) {
		return false;
	}
	else {
		*set_backbuffer=*set_colorbuffer = format_table[format_index];
	}

	if (bitdepth==32 && *set_colorbuffer == D3DFMT_X8R8G8B8 && D3DInterface->CheckDeviceType(0,D3DDEVTYPE_HAL,*set_colorbuffer,D3DFMT_A8R8G8B8, TRUE) == D3D_OK)
	{
		//promote 32-bit modes to include destination alpha when supported
		*set_backbuffer = D3DFMT_A8R8G8B8;
	}

	/*
	** We found a backbuffer format, now find a zbuffer format
	*/
	return Find_Z_Mode(*set_colorbuffer,*set_backbuffer, set_zmode);
};


// find the resolution mode with at least resx,resy with the highest supported
// refresh rate
bool DX8Wrapper::Find_Color_Mode(D3DFORMAT colorbuffer, int resx, int resy, UINT *mode)
{
	UINT i,j,modemax;
	UINT rx,ry;
	D3DDISPLAYMODE dmode;
	::ZeroMemory(&dmode, sizeof(D3DDISPLAYMODE));

	rx=(unsigned int) resx;
	ry=(unsigned int) resy;

	bool found=false;
	// Ronin @build 28/10/2025 DX9: GetAdapterModeCount requires format parameter
	modemax = D3DInterface->GetAdapterModeCount(D3DADAPTER_DEFAULT, colorbuffer);

	i = 0;

	while (i < modemax && !found)
	{
		// Ronin @build 28/10/2025 DX9: EnumAdapterModes requires format parameter
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, colorbuffer, i, &dmode);
		if (dmode.Width == rx && dmode.Height == ry && dmode.Format == colorbuffer) {
			WWDEBUG_SAY(("Found valid color mode.  Width = %d Height = %d Format = %d", dmode.Width, dmode.Height, dmode.Format));
			found = true;
		}
		i++;
	}

	i--; // this is the first valid mode

	// no match
	if (!found) {
		WWDEBUG_SAY(("Failed to find a valid color mode"));
		return false;
	}

	// go to the highest refresh rate in this mode
	bool stillok = true;

	j = i;
	while (j < modemax && stillok)
	{
		// Ronin @build 28/10/2025 DX9: EnumAdapterModes requires format parameter
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, colorbuffer, j, &dmode);
		if (dmode.Width == rx && dmode.Height == ry && dmode.Format == colorbuffer)
			stillok = true;

		else
			stillok = false;
		j++;
	}

	if (stillok == false) *mode = j - 2;
	else *mode = i;

	return true;

	/*modemax = D3DInterface->GetAdapterModeCount(D3DADAPTER_DEFAULT);

	i=0;

	while (i<modemax && !found)
	{
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, i, &dmode);
		if (dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer) {
			WWDEBUG_SAY(("Found valid color mode.  Width = %d Height = %d Format = %d",dmode.Width,dmode.Height,dmode.Format));
			found=true;
		}
		i++;
	}

	i--; // this is the first valid mode

	// no match
	if (!found) {
		WWDEBUG_SAY(("Failed to find a valid color mode"));
		return false;
	}

	// go to the highest refresh rate in this mode
	bool stillok=true;

	j=i;
	while (j<modemax && stillok)
	{
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, j, &dmode);
		if (dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer)
			stillok=true; else stillok=false;
		j++;
	}
*/
}

// Helper function to find a Z buffer mode for the colorbuffer
// Will look for greatest Z precision
bool DX8Wrapper::Find_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT *zmode)
{
	//MW: Swapped the next 2 tests so that Stencil modes get tested first.
	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24S8))
	{
		*zmode=D3DFMT_D24S8;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24S8"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D32))
	{
		*zmode=D3DFMT_D32;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D32"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24X8))
	{
		*zmode=D3DFMT_D24X8;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24X8"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24X4S4))
	{
		*zmode=D3DFMT_D24X4S4;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24X4S4"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D16))
	{
		*zmode=D3DFMT_D16;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D16"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D15S1))
	{
		*zmode=D3DFMT_D15S1;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D15S1"));
		return true;
	}

	// can't find a match
	WWDEBUG_SAY(("Failed to find a valid zbuffer mode"));
	return false;
}

bool DX8Wrapper::Test_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT zmode)
{
	// See if we have this mode first
	if (FAILED(D3DInterface->CheckDeviceFormat(D3DADAPTER_DEFAULT,WW3D_DEVTYPE,
		colorbuffer,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,zmode)))
	{
		WWDEBUG_SAY(("CheckDeviceFormat failed.  Colorbuffer format = %d  Zbufferformat = %d",colorbuffer,zmode));
		return false;
	}

	// Then see if it matches the color buffer
	if(FAILED(D3DInterface->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, WW3D_DEVTYPE,
		colorbuffer,backbuffer,zmode)))
	{
		WWDEBUG_SAY(("CheckDepthStencilMatch failed.  Colorbuffer format = %d  Backbuffer format = %d Zbufferformat = %d",colorbuffer,backbuffer,zmode));
		return false;
	}
	return true;
}


void DX8Wrapper::Reset_Statistics()
{
	FrameStatistics = DX8FrameStatistics();
	LastFrameStatistics = DX8FrameStatistics();
}

void DX8Wrapper::Begin_Statistics()
{
	FrameStatistics = DX8FrameStatistics();
	// Ronin @feature 18/02/2026 DX9: Reset instancing frame statistics
	TheDX8InstanceManager.Begin_Frame_Statistics();
}

void DX8Wrapper::End_Statistics()
{
	LastFrameStatistics = FrameStatistics;
	// Ronin @feature 18/02/2026 DX9: Capture instancing frame statistics
	TheDX8InstanceManager.End_Frame_Statistics();
}

const DX8FrameStatistics& DX8Wrapper::Get_Last_Frame_Statistics()
{
	return LastFrameStatistics;
}

unsigned long DX8Wrapper::Get_FrameCount() {return FrameCount;}

void DX8_Assert()
{
	WWASSERT(DX8Wrapper::_Get_D3D8());
	DX8_THREAD_ASSERT();
}

void DX8Wrapper::Begin_Scene()
{
	DX8_THREAD_ASSERT();
		
	// Ronin @bugfix 09/11/2025: Handle already-active scene gracefully
	HRESULT hr = _Get_D3D_Device8()->BeginScene();

	if (SUCCEEDED(hr)) {
		// Successfully started a new scene
		s_inScene = true;
		DX8_RECORD_DX8_CALLS();

/*#ifdef _DEBUG
		// VERIFY: State is clean after our cleanup
		UINT postClearStride = 0;
		UINT postClearOffset = 0;  // ← FIXED
		IDirect3DVertexBuffer9* postClearVB = nullptr;

		pDev->GetStreamSource(0, &postClearVB, &postClearOffset, &postClearStride);

		if (postClearStride != 0 || postClearVB != nullptr) {
			WWDEBUG_SAY(("Begin_Scene: Stride STILL %u after clearing! VB=%p",
				postClearStride, postClearVB));
		}
		else {
			WWDEBUG_SAY(("Begin_Scene: Clean state (stride=0, VB=null)"));
		}

		if (postClearVB) postClearVB->Release();
#endif*/


		return;
	}

	// Handle error cases
	if (hr == D3DERR_INVALIDCALL) {
		s_inScene = true;  // Mark that we're in a scene
		DX8_RECORD_DX8_CALLS();
		return;
	}

	// Other error - keep this one for actual errors
	WWDEBUG_SAY(("BeginScene FAILED: 0x%08X (%s)", hr, DXGetErrorString9A(hr)));
	DX8_RECORD_DX8_CALLS();
}

void DX8Wrapper::End_Scene(bool flip_frames)
{
	DX8_THREAD_ASSERT();

	// Ronin @bugfix 09/11/2025: Reset scene tracking on successful EndScene
	HRESULT hr = _Get_D3D_Device8()->EndScene();
	DX8_RECORD_DX8_CALLS();

	if (SUCCEEDED(hr)) {
		s_inScene = false;
	}
	else {
		WWDEBUG_SAY(("EndScene FAILED: 0x%08X (%s)", hr, DXGetErrorString9A(hr)));
		s_inScene = false;
	}

	if (flip_frames) {
		DX8_Assert();

		{
			WWPROFILE("DX8Device::Present()");
			hr = _Get_D3D_Device8()->Present(nullptr, nullptr, nullptr, nullptr);
		}

		DX8_RECORD_DX8_CALLS();

		if (SUCCEEDED(hr)) {
#ifdef EXTENDED_STATS
			if (stats.m_sleepTime) {
				::Sleep(stats.m_sleepTime);
			}
#endif
			IsDeviceLost = false;
			FrameCount++;
		}
		else {
			IsDeviceLost = true;

			// Ronin @performance 10/03/2026 DX9: Match reference flow more closely.
			// Only query cooperative level after Present reports DEVICELOST.
			if (hr == D3DERR_DEVICELOST) {
				HRESULT deviceState = _Get_D3D_Device8()->TestCooperativeLevel();
				DX8_RECORD_DX8_CALLS();

				if (deviceState == D3DERR_DEVICENOTRESET) {
					WWDEBUG_SAY(("Device ready for reset, attempting recovery..."));

					// Reset_Device(true) already recreates assets, invalidates caches,
					// restores default states and reacquires cleanup-hook resources.
					if (Reset_Device(true)) {
						WWDEBUG_SAY(("Device reset successful!"));
						IsDeviceLost = false;
					}
					else {
						WWDEBUG_SAY(("Device reset FAILED!"));
						ThreadClass::Sleep_Ms(500);
					}
				}
				else if (deviceState == D3DERR_DEVICELOST) {
					WWDEBUG_SAY(("Device lost, waiting..."));
					ThreadClass::Sleep_Ms(100);
				}
				else if (FAILED(deviceState)) {
					WWDEBUG_SAY(("TestCooperativeLevel FAILED after Present DEVICELOST: 0x%08X (%s)",
						deviceState, DXGetErrorString9A(deviceState)));
					ThreadClass::Sleep_Ms(100);
				}
			}
			// Ronin @feature 08/11/2026 DX9Ex: a 9Ex device is only lost when the GPU hung (a reset recovers it) or its
			// driver stopped (not recoverable here).
			else if (IsEx && hr == D3DERR_DEVICEHUNG) {
				WWDEBUG_SAY(("Present: D3DERR_DEVICEHUNG - resetting"));
				if (Reset_Device(true)) {
					IsDeviceLost = false;
				}
				else {
					ThreadClass::Sleep_Ms(500);
				}
			}
			else {
				WWDEBUG_SAY(("Present FAILED: 0x%08X (%s)", hr, DXGetErrorString9A(hr)));
			}
		}
	}

	// Each frame, release all of the buffers and textures.
	Set_Vertex_Buffer(nullptr);
	Set_Index_Buffer(nullptr,0);
	for (int i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) Set_Texture(i,nullptr);
	Set_Material(nullptr);
}


void DX8Wrapper::Flip_To_Primary()
{
	// If we are fullscreen and the current frame is odd then we need
	// to force a page flip to ensure that the first buffer in the flipping
	// chain is the one visible.
	if (!IsWindowed) {
		DX8_Assert();

		int numBuffers = (_PresentParameters.BackBufferCount + 1);
		int visibleBuffer = (FrameCount % numBuffers);
		int flipCount = ((numBuffers - visibleBuffer) % numBuffers);
		int resetAttempts = 0;

		while ((flipCount > 0) && (resetAttempts < 3)) {
			HRESULT hr = _Get_D3D_Device8()->TestCooperativeLevel();

			if (FAILED(hr)) {
				WWDEBUG_SAY(("TestCooperativeLevel Failed!"));

				if (D3DERR_DEVICELOST == hr) {
					IsDeviceLost=true;
					WWDEBUG_SAY(("DEVICELOST: Cannot flip to primary."));
					return;
				}
				IsDeviceLost=false;

				if (D3DERR_DEVICENOTRESET == hr) {
					WWDEBUG_SAY(("DEVICENOTRESET"));
					Reset_Device();
					resetAttempts++;
				}
			}
			else {
				WWDEBUG_SAY(("Flipping: %ld", FrameCount));
				hr = _Get_D3D_Device8()->Present(nullptr, nullptr, nullptr, nullptr);
				DX8_RECORD_DX8_CALLS();

				if (SUCCEEDED(hr)) {
					IsDeviceLost=false;
					FrameCount++;
					WWDEBUG_SAY(("Flip to primary succeeded %ld", FrameCount));
				}
				else {
					IsDeviceLost=true;
				}
			}

			--flipCount;
		}
	}
}


//**********************************************************************************************
//! Clear current render device
/*! KM
/* 5/17/02 KM Fixed support for render to texture with depth/stencil buffers
*/
void DX8Wrapper::Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha, float z, unsigned int stencil)
{
	DX8_THREAD_ASSERT();

	// If we try to clear a stencil buffer which is not there, the entire call will fail
	// KJM fixed this to get format from back buffer (incase render to texture is used)
	/*bool has_stencil = (	_PresentParameters.AutoDepthStencilFormat == D3DFMT_D15S1 ||
								_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24S8 ||
								_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24X4S4);*/
	bool has_stencil=false;
	IDirect3DSurface8* depthbuffer;

	_Get_D3D_Device8()->GetDepthStencilSurface(&depthbuffer);
	DX8_RECORD_DX8_CALLS();

	if (depthbuffer)
	{
		D3DSURFACE_DESC desc;
		depthbuffer->GetDesc(&desc);
		// Ronin @feature 13/09/2026 DX9: SSAO step 1. INTZ carries 8 stencil bits too — without it here, every stencil
		// clear during the main pass would be silently dropped.
		has_stencil=
		(
			desc.Format==D3DFMT_D15S1 ||
			desc.Format==D3DFMT_D24S8 ||
			desc.Format==D3DFMT_D24X4S4 ||
			desc.Format==D3DFMT_INTZ_FOURCC
		);

		// release ref
		depthbuffer->Release();
	}

	DWORD flags = 0;
	if (clear_color) flags |= D3DCLEAR_TARGET;
	if (clear_z_stencil) flags |= D3DCLEAR_ZBUFFER;
	if (clear_z_stencil && has_stencil) flags |= D3DCLEAR_STENCIL;
	if (flags)
	{
		DX8CALL(Clear(0, nullptr, flags, Convert_Color(color,dest_alpha), z, stencil));
	}
}

// Ronin @feature 13/09/2026 DX9: SSAO step 1. Can INTZ be a texture on this adapter? Asked once per device, same call
// shape as R3DShadowMap's probe.
bool DX8Wrapper::Is_Scene_Depth_Supported()
{
	if (SceneDepthSupport < 0)
	{
		SceneDepthSupport = 0;
		if (D3DInterface != nullptr && CurrentCaps != nullptr)
		{
			const D3DCAPS9 &caps = CurrentCaps->Get_DX8_Caps();
			D3DDISPLAYMODE mode;
			if (SUCCEEDED(D3DInterface->GetAdapterDisplayMode(caps.AdapterOrdinal, &mode)) &&
				SUCCEEDED(D3DInterface->CheckDeviceFormat(caps.AdapterOrdinal, caps.DeviceType, mode.Format,
					D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_INTZ_FOURCC)))
			{
				SceneDepthSupport = 1;
			}
		}
	}
	return SceneDepthSupport == 1;
}

// Ronin @feature 06/10/2026 DX9: the INTZ texture at this size, made or remade. The swap and the prepass share it.
bool DX8Wrapper::Ensure_Scene_Depth(unsigned width, unsigned height)
{
	if (SceneDepthSurface != nullptr)
	{
		D3DSURFACE_DESC sd;
		if (FAILED(SceneDepthSurface->GetDesc(&sd)) || sd.Width != width || sd.Height != height)
			Release_Scene_Depth();
	}

	if (SceneDepthTexture == nullptr)
	{
		IDirect3DTexture9 *tex = nullptr;
		if (FAILED(D3DDevice->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_INTZ_FOURCC,
				D3DPOOL_DEFAULT, &tex, nullptr)) || tex == nullptr)
		{
			SceneDepthSupport = 0;		// the probe said yes and creation said no - stop retrying every frame
			return false;
		}
		SceneDepthTexture = tex;

		if (FAILED(SceneDepthTexture->GetSurfaceLevel(0, &SceneDepthSurface)) || SceneDepthSurface == nullptr)
		{
			SceneDepthSurface = nullptr;
			Release_Scene_Depth();
			SceneDepthSupport = 0;
			return false;
		}
	}
	return true;
}

// Ronin @feature 13/09/2026 DX9: SSAO step 1. Swap the device's depth-stencil for a same-size INTZ texture and clear it.
// Returns false and changes nothing when unsupported, already bound, or the depth buffer is multisampled.
bool DX8Wrapper::Begin_Scene_Depth()
{
	SceneDepthWritten = false;
	SceneDepthFromPrepass = false;
	if (D3DDevice == nullptr || SceneDepthSaved != nullptr || !Is_Scene_Depth_Supported())
		return false;

	IDirect3DSurface9 *current = nullptr;
	if (FAILED(D3DDevice->GetDepthStencilSurface(&current)) || current == nullptr)
		return false;

	// Ronin @feature 06/10/2026 DX9: INTZ has no multisampled form - under AA the frame keeps its own depth and the prepass fills ours.
	D3DSURFACE_DESC cd;
	if (FAILED(current->GetDesc(&cd)) || cd.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		current->Release();
		return false;
	}

	// Ronin @feature 06/10/2026 DX9: the device's own depth buffer is the size reference, so a resolution change rebuilds ours.
	if (!Ensure_Scene_Depth(cd.Width, cd.Height) || FAILED(D3DDevice->SetDepthStencilSurface(SceneDepthSurface)))
	{
		current->Release();
		return false;
	}
	SceneDepthSaved = current;		// keeps the reference GetDepthStencilSurface took

	// Clear honours the viewport; WW3D::Begin_Render has just set it to the whole target.
	D3DDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
	SceneDepthWritten = true;
	return true;
}

// Ronin @feature 13/09/2026 DX9: SSAO step 1. Give the device its own depth-stencil back. A no-op when Begin declined.
void DX8Wrapper::End_Scene_Depth()
{
	SceneDepthSuspended = false;
	if (SceneDepthSaved == nullptr)
		return;
	if (D3DDevice != nullptr)
		D3DDevice->SetDepthStencilSurface(SceneDepthSaved);
	SceneDepthSaved->Release();
	SceneDepthSaved = nullptr;
}

// Ronin @feature 06/10/2026 DX9: SSAO under MSAA. NULL is a colour target that takes no memory - for depth-only passes.
bool DX8Wrapper::Is_Null_Target_Supported()
{
	if (NullTargetSupport < 0)
	{
		NullTargetSupport = 0;
		if (D3DInterface != nullptr && CurrentCaps != nullptr)
		{
			const D3DCAPS9 &caps = CurrentCaps->Get_DX8_Caps();
			D3DDISPLAYMODE mode;
			if (SUCCEEDED(D3DInterface->GetAdapterDisplayMode(caps.AdapterOrdinal, &mode)) &&
				SUCCEEDED(D3DInterface->CheckDeviceFormat(caps.AdapterOrdinal, caps.DeviceType, mode.Format,
					D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, D3DFMT_NULL_FOURCC)))
			{
				NullTargetSupport = 1;
			}
		}
	}
	return NullTargetSupport == 1;
}

// Ronin @feature 06/10/2026 DX9: SSAO under MSAA (SSAO_Work.md 6). A multisampled frame cannot swap its depth, so a prepass
// fills the same INTZ: bound here with a dummy colour target and cleared. False = not multisampled, or not makeable.
bool DX8Wrapper::Begin_Scene_Depth_Prepass()
{
	if (D3DDevice == nullptr || SceneDepthSaved != nullptr || ScenePrepassSavedColor != nullptr ||
		!Is_Scene_Depth_Supported())
		return false;

	IDirect3DSurface9 *frameDepth = nullptr;
	if (FAILED(D3DDevice->GetDepthStencilSurface(&frameDepth)) || frameDepth == nullptr)
		return false;

	D3DSURFACE_DESC fd;
	if (FAILED(frameDepth->GetDesc(&fd)) || fd.MultiSampleType == D3DMULTISAMPLE_NONE ||
		!Ensure_Scene_Depth(fd.Width, fd.Height))
	{
		frameDepth->Release();
		return false;
	}

	// Ronin @feature 06/10/2026 DX9: Release_Scene_Depth drops this with the texture, so the two are always the same size.
	if (ScenePrepassColor == nullptr)
	{
		ScenePrepassTarget = 0;
		if (Is_Null_Target_Supported() && SUCCEEDED(D3DDevice->CreateRenderTarget(fd.Width, fd.Height,
				D3DFMT_NULL_FOURCC, D3DMULTISAMPLE_NONE, 0, FALSE, &ScenePrepassColor, nullptr)))
			ScenePrepassTarget = 1;
		else if (SUCCEEDED(D3DDevice->CreateRenderTarget(fd.Width, fd.Height,
				D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &ScenePrepassColor, nullptr)))
			ScenePrepassTarget = 2;
		else
			ScenePrepassColor = nullptr;
	}

	IDirect3DSurface9 *frameColor = nullptr;
	if (ScenePrepassColor == nullptr || FAILED(D3DDevice->GetRenderTarget(0, &frameColor)) || frameColor == nullptr)
	{
		frameDepth->Release();
		return false;
	}

	// Ronin @feature 06/10/2026 DX9: depth off first, then the target, then the depth that matches it.
	D3DDevice->SetDepthStencilSurface(nullptr);
	if (FAILED(D3DDevice->SetRenderTarget(0, ScenePrepassColor)) ||
		FAILED(D3DDevice->SetDepthStencilSurface(SceneDepthSurface)))
	{
		D3DDevice->SetDepthStencilSurface(nullptr);
		D3DDevice->SetRenderTarget(0, frameColor);
		D3DDevice->SetDepthStencilSurface(frameDepth);
		frameColor->Release();
		frameDepth->Release();
		return false;
	}
	ScenePrepassSavedColor = frameColor;	// both keep the references the Get calls took
	ScenePrepassSavedDepth = frameDepth;

	SceneDepthWritten = false;
	SceneDepthFromPrepass = false;
	D3DDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
	return true;
}

// Ronin @feature 06/10/2026 DX9: SSAO under MSAA. The solid scene is in; whatever the render still drains writes no depth.
void DX8Wrapper::Freeze_Scene_Depth_Prepass()
{
	if (D3DDevice != nullptr && ScenePrepassSavedColor != nullptr)
		D3DDevice->SetDepthStencilSurface(nullptr);
}

// Ronin @feature 06/10/2026 DX9: SSAO under MSAA. The frame's own pair back, in Begin's order; the INTZ is now readable.
void DX8Wrapper::End_Scene_Depth_Prepass()
{
	if (ScenePrepassSavedColor == nullptr)
		return;
	if (D3DDevice != nullptr)
	{
		D3DDevice->SetDepthStencilSurface(nullptr);
		D3DDevice->SetRenderTarget(0, ScenePrepassSavedColor);
		D3DDevice->SetDepthStencilSurface(ScenePrepassSavedDepth);
		SceneDepthWritten = true;
		SceneDepthFromPrepass = true;
	}
	ScenePrepassSavedColor->Release();
	ScenePrepassSavedColor = nullptr;
	if (ScenePrepassSavedDepth != nullptr)
	{
		ScenePrepassSavedDepth->Release();
		ScenePrepassSavedDepth = nullptr;
	}
}

// Ronin @feature 13/09/2026 DX9: SSAO step 3a. Take the depth-stencil off the device mid-frame so a pass can read ours.
// Ronin @feature 06/10/2026 DX9: on a prepass frame ours is not bound: hold the frame's multisampled one instead.
bool DX8Wrapper::Suspend_Scene_Depth()
{
	if (D3DDevice == nullptr || !SceneDepthWritten || SceneDepthSuspended)
		return false;
	if (SceneDepthSaved == nullptr && !SceneDepthFromPrepass)
		return false;
	if (SceneDepthFromPrepass && FAILED(D3DDevice->GetDepthStencilSurface(&SceneDepthHeld)))
		SceneDepthHeld = nullptr;
	if (FAILED(D3DDevice->SetDepthStencilSurface(nullptr)))
	{
		if (SceneDepthHeld != nullptr)
		{
			SceneDepthHeld->Release();
			SceneDepthHeld = nullptr;
		}
		return false;
	}
	SceneDepthSuspended = true;
	return true;
}

// Ronin @feature 13/09/2026 DX9: SSAO step 3a. Put it back - the rest of the frame keeps testing against it.
void DX8Wrapper::Resume_Scene_Depth()
{
	if (!SceneDepthSuspended)
		return;
	SceneDepthSuspended = false;
	if (SceneDepthFromPrepass)
	{
		if (D3DDevice != nullptr)
			D3DDevice->SetDepthStencilSurface(SceneDepthHeld);
		if (SceneDepthHeld != nullptr)
		{
			SceneDepthHeld->Release();
			SceneDepthHeld = nullptr;
		}
	}
	else if (D3DDevice != nullptr && SceneDepthSurface != nullptr)
		D3DDevice->SetDepthStencilSurface(SceneDepthSurface);
}

// Ronin @feature 13/09/2026 DX9: SSAO step 1. D3DPOOL_DEFAULT - released before every Reset and before the device goes.
void DX8Wrapper::Release_Scene_Depth()
{
	End_Scene_Depth();
	End_Scene_Depth_Prepass();
	if (SceneDepthHeld != nullptr)
	{
		SceneDepthHeld->Release();
		SceneDepthHeld = nullptr;
	}
	if (ScenePrepassColor != nullptr)
	{
		ScenePrepassColor->Release();
		ScenePrepassColor = nullptr;
	}
	ScenePrepassTarget = 0;
	if (SceneDepthSurface != nullptr)
	{
		SceneDepthSurface->Release();
		SceneDepthSurface = nullptr;
	}
	if (SceneDepthTexture != nullptr)
	{
		SceneDepthTexture->Release();
		SceneDepthTexture = nullptr;
	}
	SceneDepthWritten = false;
	SceneDepthFromPrepass = false;
}

void DX8Wrapper::Set_Viewport(CONST D3DVIEWPORT8* pViewport)
{
	DX8_THREAD_ASSERT();
	DX8CALL(SetViewport(pViewport));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer. A reference to previous vertex buffer is released and
// this one is assigned the current vertex buffer. The DX8 vertex buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream)
{
	render_state.vba_offset=0;
	render_state.vba_count=0;
	if (render_state.vertex_buffers[stream]) {
		render_state.vertex_buffers[stream]->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.vertex_buffers[stream],const_cast<VertexBufferClass*>(vb));
	if (vb) {
		vb->Add_Engine_Ref();
		render_state.vertex_buffer_types[stream]=vb->Type();
	}
	else {
		render_state.vertex_buffer_types[stream]=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=VERTEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set index buffer. A reference to previous index buffer is released and
// this one is assigned the current index buffer. The DX8 index buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset)
{
	// @debug Ronin 10/01/2026 Add optional caller tag to index buffer binding for tracking IB rebinds
	Set_Index_Buffer(ib, index_base_offset, "UNKNOWN(Set_Index_Buffer)");
	/*
	render_state.iba_offset=0;
	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.index_buffer,const_cast<IndexBufferClass*>(ib));
	render_state.index_base_offset=index_base_offset;
	if (ib) {
		ib->Add_Engine_Ref();
		render_state.index_buffer_type=ib->Type();
	}
	else {
		render_state.index_buffer_type=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=INDEX_BUFFER_CHANGED;*/
}
void DX8Wrapper::Set_Index_Buffer(const IndexBufferClass* ib, unsigned short index_base_offset, const char* callerTag)
{
	render_state.iba_offset = 0;
	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.index_buffer, const_cast<IndexBufferClass*>(ib));
	render_state.index_base_offset = index_base_offset;
	if (ib) {
		ib->Add_Engine_Ref();
		render_state.index_buffer_type = ib->Type();
	}
	else {
		render_state.index_buffer_type = BUFFER_TYPE_INVALID;
	}
	render_state_changed |= INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const DynamicVBAccessClass& vba_)
{
	// Release all streams (only one stream allowed in the legacy pipeline)
	for (int i=1;i<MAX_VERTEX_STREAMS;++i) {
		DX8Wrapper::Set_Vertex_Buffer(nullptr, i);
	}

	if (render_state.vertex_buffers[0]) render_state.vertex_buffers[0]->Release_Engine_Ref();
	DynamicVBAccessClass& vba=const_cast<DynamicVBAccessClass&>(vba_);
	render_state.vertex_buffer_types[0]=vba.Get_Type();
	render_state.vba_offset=vba.VertexBufferOffset;
	render_state.vba_count=vba.Get_Vertex_Count();

	// Ronin @bugfix 14/01/2026 DX9: Track dynamic VB FVF so Apply() IA verification can compute expected stride (prevents false expectedStride=0)
	render_state.vba_fvf = vba.FVF_Info().Get_FVF();

	// Ronin @bugfix 26/01/2026 DX9: Also track the underlying D3D VB for deterministic Apply() binding.
	if (render_state.vba_d3d_vb) {
			render_state.vba_d3d_vb->Release();
			render_state.vba_d3d_vb = nullptr;
	}
	// Only BUFFER_TYPE_DYNAMIC_DX8 has a real D3D VB. Sorting is CPU-side.
	if (render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8) {
			render_state.vba_d3d_vb = vba.Get_D3D_VB();
		if (render_state.vba_d3d_vb) {
				render_state.vba_d3d_vb->AddRef();
		}
	}

	// Ronin @debug 23/01/2026: publish expected layout for IA ENSURE
	render_state.expectedFVF = render_state.vba_fvf;
	if (render_state.expectedDecl) {
		render_state.expectedDecl->Release();
		render_state.expectedDecl = nullptr;
	}

	REF_PTR_SET(render_state.vertex_buffers[0],vba.VertexBuffer);
	render_state.vertex_buffers[0]->Add_Engine_Ref();
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;		// vba_offset changes so index buffer needs to be reset as well.
}

// ----------------------------------------------------------------------------
//
// Set index buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const DynamicIBAccessClass& iba_,unsigned short index_base_offset)
{

	// @debug Ronin 10/01/2026 Add optional caller tag to index buffer binding for tracking IB rebinds
	Set_Index_Buffer(iba_, index_base_offset, "UNKNOWN(Set_Index_Buffer dyn)");
	/*
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();

	DynamicIBAccessClass& iba=const_cast<DynamicIBAccessClass&>(iba_);
	render_state.index_base_offset=index_base_offset;
	render_state.index_buffer_type=iba.Get_Type();
	render_state.iba_offset=iba.IndexBufferOffset;
	REF_PTR_SET(render_state.index_buffer,iba.IndexBuffer);
	render_state.index_buffer->Add_Engine_Ref();
	render_state_changed|=INDEX_BUFFER_CHANGED;*/
}

void DX8Wrapper::Set_Index_Buffer(const DynamicIBAccessClass& iba_, unsigned short index_base_offset, const char* callerTag)
{
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();

	DynamicIBAccessClass& iba = const_cast<DynamicIBAccessClass&>(iba_);
	render_state.index_base_offset = index_base_offset;
	render_state.index_buffer_type = iba.Get_Type();
	render_state.iba_offset = iba.IndexBufferOffset;
	REF_PTR_SET(render_state.index_buffer, iba.IndexBuffer);
	render_state.index_buffer->Add_Engine_Ref();
	render_state_changed |= INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Private function for the special case of rendering polygons from sorting
// index and vertex buffers.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Sorting_IB_VB(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	WWASSERT(render_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || render_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING);
	WWASSERT(render_state.index_buffer_type==BUFFER_TYPE_SORTING || render_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING);

	// Fill dynamic vertex buffer with sorting vertex buffer vertices
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* src = static_cast<SortingVertexBufferClass*>(render_state.vertex_buffers[0])->VertexBuffer;
		VertexFormatXYZNDUV2* dest= lock.Get_Formatted_Vertex_Array();
		src += render_state.vba_offset + render_state.index_base_offset + min_vertex_index;
		unsigned  size = dyn_vb_access.FVF_Info().Get_FVF_Size()*vertex_count/sizeof(unsigned);
		unsigned *dest_u =(unsigned*) dest;
		unsigned *src_u = (unsigned*) src;

		for (unsigned i=0;i<size;++i) {
			*dest_u++=*src_u++;
		}
	}

	// Ronin @build 04/12/2025 DX9: SetStreamSource now requires stride parameter
	DX8CALL(SetStreamSource(
		0,
		static_cast<DX8VertexBufferClass*>(dyn_vb_access.VertexBuffer)->Get_DX8_Vertex_Buffer(),
		0,  // Offset (DX9 addition)
		dyn_vb_access.FVF_Info().Get_FVF_Size()));

	// If using FVF format VB, set the FVF
	// Ronin @build 04/12/2025 DX9: SetVertexShader(fvf) -> SetFVF(fvf)
	unsigned fvf = dyn_vb_access.FVF_Info().Get_FVF();
	if (fvf != 0) {
		Set_FVF(fvf);  // Use wrapper function, NOT direct device call
	}
	DX8_RECORD_VERTEX_BUFFER_CHANGE();

	unsigned index_count=0;
	switch (primitive_type) {
	case D3DPT_TRIANGLELIST: index_count=polygon_count*3; break;
	case D3DPT_TRIANGLESTRIP: index_count=polygon_count+2; break;
	case D3DPT_TRIANGLEFAN: index_count=polygon_count+2; break;
	default: WWASSERT(0); break; // Unsupported primitive type
	}

	// Fill dynamic index buffer
	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8, index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		unsigned short* dest = lock.Get_Index_Array();
		unsigned short* src = static_cast<SortingIndexBufferClass*>(render_state.index_buffer)->index_buffer;
		src += render_state.iba_offset + start_index;

		for (unsigned short i=0;i<index_count;++i) {
			unsigned short index=*src++;
			index-=min_vertex_index;
			WWASSERT(index<vertex_count);
			*dest++=index;
		}
	}

	// Ronin @build 04/12/2025 DX9: SetIndices no longer takes base vertex index
	DX8CALL(SetIndices(
		static_cast<DX8IndexBufferClass*>(dyn_ib_access.IndexBuffer)->Get_DX8_Index_Buffer()));
	DX8_RECORD_INDEX_BUFFER_CHANGE();

	DX8_RECORD_DRAW_CALLS();

	// Ronin @build 04/12/2025 DX9: DrawIndexedPrimitive signature changed
	const int baseVertex = (int)dyn_vb_access.VertexBufferOffset;
	const unsigned drawStartIndex = (unsigned)dyn_ib_access.IndexBufferOffset;

#ifdef WWDEBUG
	//Ensure_Device_IB_Matches_Wrapper_Expected("DX8Wrapper::Draw_Sorting_IB_VB");
#endif

	MAYBE_UNUSED HRESULT hr = D3DDevice->DrawIndexedPrimitive(
		D3DPT_TRIANGLELIST,
		baseVertex,
		0,           // MinVertexIndex
		vertex_count,
		(unsigned short)drawStartIndex,
		polygon_count);

	DX8_RECORD_DX8_CALLS();

#ifdef WWDEBUG
	// @debug Ronin 13/01/2026 Sorting DIP result + conditional diagnostics
	if (FAILED(hr)) {
		WWDEBUG_SAY((
			"[DIP][SORTING][FAIL] hr=0x%08X (%s) prim=%u start=%u primCount=%u baseV=%d vCount=%u",
			(unsigned)hr,
			DXGetErrorString9A(hr),
			(unsigned)primitive_type,
			(unsigned)drawStartIndex,
			(unsigned)polygon_count,
			(int)baseVertex,
			(unsigned)vertex_count));
	}
#endif

	DX8_RECORD_RENDER(polygon_count, vertex_count, render_state.shader);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (DrawPolygonLowBoundLimit && DrawPolygonLowBoundLimit>=polygon_count) return;

	DX8_THREAD_ASSERT();
	SNAPSHOT_SAY(("DX8 - draw"));

	Apply_Render_State_Changes();

	// Debug feature to disable triangle drawing...
	if (!_Is_Triangle_Draw_Enabled()) return;

#ifdef DEBUG_LOGGING
	if (WW3D::Is_Snapshot_Activated()) {
		unsigned long passes=0;
		SNAPSHOT_SAY(("ValidateDevice:"));
		HRESULT res=D3DDevice->ValidateDevice(&passes);
		SNAPSHOT_SAY(("%s",Get_DX8_Error_Code_Name(res)));
	}

	SNAPSHOT_SAY(("DX8 - draw %d polygons (%d vertices)",polygon_count,vertex_count));
#endif

	if (vertex_count<3) {
		min_vertex_index=0;
		switch (render_state.vertex_buffer_types[0]) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_SORTING:
			vertex_count=render_state.vertex_buffers[0]->Get_Vertex_Count()-render_state.index_base_offset-render_state.vba_offset-min_vertex_index;
			break;
		case BUFFER_TYPE_DYNAMIC_DX8:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			vertex_count=render_state.vba_count;
			break;
		}
	}

	switch (render_state.vertex_buffer_types[0]) {
	case BUFFER_TYPE_DX8:
	case BUFFER_TYPE_DYNAMIC_DX8:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			{
/*				if ((start_index+render_state.iba_offset+polygon_count*3) > render_state.index_buffer->Get_Index_Count())
				{	WWASSERT_PRINT(0,"OVERFLOWING INDEX BUFFER");
					///@todo: MUST FIND OUT WHY THIS HAPPENS WITH LOTS OF PARTICLES ON BIG FIGHT!  -MW
					break;
				}*/
				DX8_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
				DX8_RECORD_DRAW_CALLS();

				const unsigned drawStartIndex = start_index + render_state.iba_offset;
				int baseVertex = (int)render_state.index_base_offset;

#ifdef WWDEBUG
				//Ensure_Device_IB_Matches_Wrapper_Expected("DX8Wrapper::Draw");
#endif

				D3DDevice->DrawIndexedPrimitive(
					(D3DPRIMITIVETYPE)primitive_type,
					baseVertex,
					min_vertex_index,
					vertex_count,
					drawStartIndex,
					polygon_count);

				DX8_RECORD_DX8_CALLS(); // mirror DX8CALL behavior (keeps stats sane) || Ronin @feauture 16/02/2026: **Revisit required**

			}
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_SORTING:
	case BUFFER_TYPE_DYNAMIC_SORTING:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			Draw_Sorting_IB_VB(primitive_type,start_index,polygon_count,min_vertex_index,vertex_count);
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_INVALID:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned buffer_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (buffer_type==BUFFER_TYPE_SORTING || buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) {
		SortingRendererClass::Insert_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
	}
	else {
		Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Strip(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLESTRIP,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Apply_Render_State_Changes()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes()"));

	if (!render_state_changed) return;

#ifdef _DEBUG
	const char* apply_ctx = DX8Wrapper::Get_Debug_Draw_Context();
#endif

#ifdef _DEBUG
	//Track_Decl_Bound_While_Wrapper_Expects_FVF("Apply_Render_State_Changes");
#endif

	// === SHADER ===
	if (render_state_changed & SHADER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply shader"));
		render_state.shader.Apply();
	}

	// === TEXTURES ===
	unsigned mask = TEXTURE0_CHANGED;
	for (int i = 0; i < CurrentCaps->Get_Max_Textures_Per_Pass(); ++i, mask <<= 1)
	{
		if (render_state_changed & mask)
		{
			SNAPSHOT_SAY(("DX8 - apply texture %d", i));

			if (render_state.Textures[i])
			{
				render_state.Textures[i]->Apply(i);
			}
			else
			{
				TextureBaseClass::Apply_Null(i);
			}
		}
	}

	// === MATERIAL ===
	if (render_state_changed & MATERIAL_CHANGED)
	{
		SNAPSHOT_SAY(("DX8 - apply material"));
		VertexMaterialClass* material = const_cast<VertexMaterialClass*>(render_state.material);
		if (material)
		{
			material->Apply();
		}
		else {
			VertexMaterialClass::Apply_Null();
		}
	}

	// === LIGHTS ===
	if (render_state_changed & LIGHTS_CHANGED)
	{
		unsigned lmask = LIGHT0_CHANGED;
		for (unsigned index = 0; index < MAX_LIGHTS; ++index, lmask <<= 1) {
			if (render_state_changed & lmask) {
				SNAPSHOT_SAY(("DX8 - apply light %d", index));
				if (render_state.LightEnable[index]) {
#if defined(DEBUG_CRASHING) || defined(DEBUG_LOGGING)
					if ( WW3D::Is_Snapshot_Activated() ) {
						D3DLIGHT8 * light = &(render_state.Lights[index]);
						static const char * _light_types[] = { "Unknown", "Point","Spot", "Directional" };
						WWASSERT((light->Type >= 0) && (light->Type <= 3));

						SNAPSHOT_SAY((" type = %s amb = %4.2f,%4.2f,%4.2f  diff = %4.2f,%4.2f,%4.2f spec = %4.2f, %4.2f, %4.2f",
							_light_types[light->Type],
							light->Ambient.r,light->Ambient.g,light->Ambient.b,
							light->Diffuse.r,light->Diffuse.g,light->Diffuse.b,
							light->Specular.r,light->Specular.g,light->Specular.b ));
						SNAPSHOT_SAY((" pos = %f, %f, %f  dir = %f, %f, %f",
							light->Position.x, light->Position.y, light->Position.z,
							light->Direction.x, light->Direction.y, light->Direction.z ));
					}
#endif

					Set_DX8_Light(index,&render_state.Lights[index]);
				}
				else {
					Set_DX8_Light(index,nullptr);
					SNAPSHOT_SAY((" clearing light to null"));
				}
			}
		}
	}

	// === TRANSFORMS ===
	if (render_state_changed & WORLD_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply world matrix"));
		_Set_DX8_Transform(D3DTS_WORLD, render_state.world);
	}
	if (render_state_changed & VIEW_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply view matrix"));
		_Set_DX8_Transform(D3DTS_VIEW, render_state.view);
	}

	if (render_state_changed & VERTEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply vb change"));

		MAYBE_UNUSED IDirect3DDevice9* dev = _Get_D3D_Device8();
		WWASSERT(dev);

#ifdef _DEBUG
		ALLOW_LAYOUT_BINDING();
#endif

		// Ronin @bugfix 10/03/2026 DX9: Re-assert the active input layout on VB changes.
		// Water and a few other legacy paths still use raw D3D state changes between wrapper calls,
		// so "assume layout already bound" is not safe here.
		const bool useDecl = (render_state.currentDecl != nullptr);

		if (useDecl) {
#ifdef _DEBUG
			ASSERT_LAYOUT_BINDING_ALLOWED_API("Apply_Render_State_Changes::SetVertexDeclaration");
#endif
			DX8CALL(SetVertexDeclaration(render_state.currentDecl));

#ifdef _DEBUG
			ASSERT_LAYOUT_BINDING_ALLOWED_API("Apply_Render_State_Changes::SetFVF");
#endif
			DX8CALL(SetFVF(0));
		}
		else {
#ifdef _DEBUG
			ASSERT_LAYOUT_BINDING_ALLOWED_API("Apply_Render_State_Changes::SetVertexDeclaration");
#endif
			DX8CALL(SetVertexDeclaration(nullptr));
			render_state.currentDecl = nullptr;

			DWORD fvf = render_state.currentFVF;

			if (fvf == 0) {
				if (render_state.vertex_buffers[0] &&
					(render_state.vertex_buffer_types[0] == BUFFER_TYPE_DX8)) {
					DX8VertexBufferClass* vb0 = static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[0]);
					fvf = vb0->FVF_Info().Get_FVF();
				}
			}

			// Ronin @bugfix 17/01/2026: Prefer dynamic VB FVF over unsafe fallback when layout is unknown
			if (fvf == 0) {
				if ((render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8 ||
					render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_SORTING) &&
					render_state.vba_fvf != 0) {
					fvf = render_state.vba_fvf;
				}
			}

			// CRITICAL FIX: Never call SetFVF(0)!
			if (fvf != 0) {

				Set_FVF(fvf);
			}
			else {
				// @bugfix Ronin 17/01/2026: Do not guess a layout here (FVF=0x142 fallback can corrupt IA tracking)
				WWDEBUG_SAY(("Apply: No FVF available; leaving device FVF untouched. owner=%s",
					render_state.layoutOwner ? render_state.layoutOwner : "(null)"));
			}
		}

		// Bind streams with correct stride (layout already decided above)
		for (int s = 0; s < MAX_VERTEX_STREAMS; ++s) {
			if (render_state.vertex_buffers[s]) {
				switch (render_state.vertex_buffer_types[s]) {
				case BUFFER_TYPE_DX8:
				{
					DX8VertexBufferClass* vb = static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[s]);
					const UINT stride = vb->FVF_Info().Get_FVF_Size();
					DX8CALL(SetStreamSource(s, vb->Get_DX8_Vertex_Buffer(), 0, stride));
					WWASSERT(stride != 0);
					DX8_RECORD_VERTEX_BUFFER_CHANGE();
					break;
				}
				case BUFFER_TYPE_DYNAMIC_DX8:
				{
					// Ronin @bugfix 26/01/2026 DX9: Bind dynamic stream from stored D3D VB pointer (no RTTI, no unsafe casts).
					if (render_state.vba_fvf != 0 && render_state.vba_d3d_vb != nullptr) {
						FVFInfoClass fi(render_state.vba_fvf);
						const UINT stride = (UINT)fi.Get_FVF_Size();
						UINT offsetInBytes = (UINT)render_state.vba_offset * stride;
						WWASSERT(stride != 0);
						DX8CALL(SetStreamSource(s, render_state.vba_d3d_vb, offsetInBytes, stride));
						DX8_RECORD_VERTEX_BUFFER_CHANGE();
					}
					else {
						WWDEBUG_SAY(("Apply: Dynamic VB missing vba_fvf or vba_d3d_vb (fvf=0x%08X vb=%p) owner=%s",
							(unsigned)render_state.vba_fvf,
							render_state.vba_d3d_vb,
							render_state.layoutOwner ? render_state.layoutOwner : "(null)"));
					}
					break;
				}
				case BUFFER_TYPE_SORTING:
				case BUFFER_TYPE_DYNAMIC_SORTING:
					break;
				default:
					WWASSERT(0);
					break;
				}
			}
			else {
				DX8CALL(SetStreamSource(s, nullptr, 0, 0));
				DX8_RECORD_VERTEX_BUFFER_CHANGE();
			}
		}

/*#ifdef _DEBUG
		// @refactor Ronin 08/02/2026 DX9: Streamlined IA verify - fail-only.
		{
			IDirect3DVertexBuffer9* devVB0 = nullptr;
			UINT devOff0 = 0, devStride0 = 0;
			dev->GetStreamSource(0, &devVB0, &devOff0, &devStride0);

			UINT expectedStride0 = 0;
			if (render_state.vertex_buffers[0]) {
				if (render_state.vertex_buffer_types[0] == BUFFER_TYPE_DX8) {
					expectedStride0 = static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[0])->FVF_Info().Get_FVF_Size();
				}
				else if (render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8 && render_state.vba_fvf != 0) {
					FVFInfoClass fi(render_state.vba_fvf);
					expectedStride0 = fi.Get_FVF_Size();
				}
			}

			if (expectedStride0 != 0 && devStride0 != expectedStride0) {
				WWDEBUG_SAY(("IA VERIFY: Stream0 stride mismatch expected=%u device=%u type=%u owner=%s",
					(unsigned)expectedStride0, (unsigned)devStride0,
					(unsigned)render_state.vertex_buffer_types[0],
					render_state.layoutOwner ? render_state.layoutOwner : "(null)"));
				WWASSERT(0 && "Stream0 stride mismatch after Apply_Render_State_Changes()");
			}

			if (devVB0) devVB0->Release();
		}
#endif*/
	}

	// === INDEX BUFFER ===
	if (render_state_changed & INDEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply ib change"));
		if (render_state.index_buffer &&
			(render_state.index_buffer_type == BUFFER_TYPE_DX8 ||
				render_state.index_buffer_type == BUFFER_TYPE_DYNAMIC_DX8)) {

			DX8IndexBufferClass* ib = static_cast<DX8IndexBufferClass*>(render_state.index_buffer);
			DX8CALL(SetIndices(ib->Get_DX8_Index_Buffer()));
			DX8_RECORD_INDEX_BUFFER_CHANGE();
		}
		else {
			DX8CALL(SetIndices(nullptr));
			DX8_RECORD_INDEX_BUFFER_CHANGE();
		}
	}

	render_state_changed &= ((unsigned)WORLD_IDENTITY | (unsigned)VIEW_IDENTITY);

	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes() - finished"));
}

IDirect3DTexture8 * DX8Wrapper::_Create_DX8_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture8 *texture = nullptr;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.

	// Render target may return NOTAVAILABLE, in
	// which case we return null.
	if (rendertarget) {
		unsigned ret=D3DXCreateTexture(
			DX8Wrapper::_Get_D3D_Device8(),
			width,
			height,
			mip_level_count,
			D3DUSAGE_RENDERTARGET,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);

		if (ret==D3DERR_NOTAVAILABLE) {
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return nullptr;
		}

		// If ran out of texture ram, try invalidating some textures and mesh cache.
		if (ret==D3DERR_OUTOFVIDEOMEMORY) {
			WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets..."));
			// Free all textures that haven't been used in the last 5 seconds
			TextureClass::Invalidate_Old_Unused_Textures(5000);

			// Invalidate the mesh cache
			WW3D::_Invalidate_Mesh_Cache();

			ret=D3DXCreateTexture(
				DX8Wrapper::_Get_D3D_Device8(),
				width,
				height,
				mip_level_count,
				D3DUSAGE_RENDERTARGET,
				WW3DFormat_To_D3DFormat(format),
				pool,
				&texture);

			if (SUCCEEDED(ret)) {
				WWDEBUG_SAY(("...Render target creation successful."));
			}
			else {
				WWDEBUG_SAY(("...Render target creation failed."));
			}
			if (ret==D3DERR_OUTOFVIDEOMEMORY) {
				Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
				return nullptr;
			}
		}

		DX8_ErrorCode(ret);
		// Just return the texture, no reduction
		// allowed for render targets.
		return texture;
	}

	// Ronin @feature 08/11/2026 DX9Ex: managed on 9Ex becomes a lockable default-pool texture (see _Get_Managed_Pool).
	DWORD managedUsage=0;
	pool=_Get_Managed_Pool(pool, &managedUsage);

	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=D3DXCreateTexture(
		DX8Wrapper::_Get_D3D_Device8(),
		width,
		height,
		mip_level_count,
		managedUsage,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY) {
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets..."));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateTexture(
			DX8Wrapper::_Get_D3D_Device8(),
			width,
			height,
			mip_level_count,
			managedUsage,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);
		if (SUCCEEDED(ret)) {
			WWDEBUG_SAY(("...Texture creation successful."));
		}
		else {
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d",width,height,format_name.str(),mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}

IDirect3DTexture8 * DX8Wrapper::_Create_DX8_Texture
(
	const char *filename,
	MipCountType mip_level_count
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture8 *texture = nullptr;

	// NOTE: If the original image format is not supported as a texture format, it will
	// automatically be converted to an appropriate format.
	// NOTE: It is possible to get the size and format of the original image file from this
	// function as well, so if we later want to second-guess D3DX's format conversion decisions
	// we can do so after this function is called..
	unsigned result = D3DXCreateTextureFromFileExA(
		_Get_D3D_Device8(),
		filename,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		mip_level_count,//create_mipmaps ? 0 : 1,
		Is_Ex() ? D3DUSAGE_DYNAMIC : 0,	// Ronin @feature 08/11/2026 DX9Ex: as _Get_Managed_Pool
		D3DFMT_UNKNOWN,
		_Get_Managed_Pool(D3DPOOL_MANAGED, nullptr),
		D3DX_FILTER_BOX,
		D3DX_FILTER_BOX,
		0,
		nullptr,
		nullptr,
		&texture);

	if (result != D3D_OK) {
		return MissingTexture::_Get_Missing_Texture();
	}

	// Make sure texture wasn't paletted!
	D3DSURFACE_DESC desc;
	texture->GetLevelDesc(0,&desc);
	if (desc.Format==D3DFMT_P8) {
		texture->Release();
		return MissingTexture::_Get_Missing_Texture();
	}
	return texture;
}

IDirect3DTexture8 * DX8Wrapper::_Create_DX8_Texture
(
	IDirect3DSurface8 *surface,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture8 *texture = nullptr;

	D3DSURFACE_DESC surface_desc;
	::ZeroMemory(&surface_desc, sizeof(D3DSURFACE_DESC));
	surface->GetDesc(&surface_desc);

	// This function will create a texture with a different (but similar) format if the surface is
	// not in a supported texture format.
	WW3DFormat format=D3DFormat_To_WW3DFormat(surface_desc.Format);
	texture = _Create_DX8_Texture(surface_desc.Width, surface_desc.Height, format, mip_level_count, pool);

	// Copy the surface to the texture
	IDirect3DSurface8 *tex_surface = nullptr;
	texture->GetSurfaceLevel(0, &tex_surface);
	DX8_ErrorCode(D3DXLoadSurfaceFromSurface(tex_surface, nullptr, nullptr, surface, nullptr, nullptr, D3DX_FILTER_BOX, 0));
	tex_surface->Release();

	// Create mipmaps if needed
	if (mip_level_count!=MIP_LEVELS_1)
	{
		DX8_ErrorCode(D3DXFilterTexture(texture, nullptr, 0, D3DX_FILTER_BOX));
	}

	return texture;

}

/*!
 * KJM create depth stencil texture
 */
IDirect3DTexture8 * DX8Wrapper::_Create_DX8_ZTexture
(
	unsigned int width,
	unsigned int height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture8* texture = nullptr;

	D3DFORMAT zfmt=WW3DZFormat_To_D3DFormat(zformat);

	unsigned ret=DX8Wrapper::_Get_D3D_Device8()->CreateTexture
	(
		width,
		height,
		mip_level_count,
		D3DUSAGE_DEPTHSTENCIL,
		zfmt,
		pool,
		&texture
	, NULL);

	if (ret==D3DERR_NOTAVAILABLE)
	{
		Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
		return nullptr;
	}

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY)
	{
		WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets..."));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=DX8Wrapper::_Get_D3D_Device8()->CreateTexture
		(
			width,
			height,
			mip_level_count,
			D3DUSAGE_DEPTHSTENCIL,
			zfmt,
			pool,
			&texture
		, NULL);

		if (SUCCEEDED(ret))
		{
			WWDEBUG_SAY(("...Render target creation successful."));
		}
		else
		{
			WWDEBUG_SAY(("...Render target creation failed."));
		}
		if (ret==D3DERR_OUTOFVIDEOMEMORY)
		{
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return nullptr;
		}
	}

	DX8_ErrorCode(ret);

	// Ronin @bugfix 08/10/2026 DX9: no AddRef here. ZTextureClass owns the creation reference; a second one was never
	// released, so every depth texture leaked and every device reset failed.

	// Just return the texture, no reduction
	// allowed for render targets.

	return texture;
}

/*!
 * KJM create cube map texture
 */
IDirect3DCubeTexture8* DX8Wrapper::_Create_DX8_Cube_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	WWASSERT(width==height);
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DCubeTexture8* texture=nullptr;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.

	// Render target may return NOTAVAILABLE, in
	// which case we return null.
	if (rendertarget)
	{
		unsigned ret=D3DXCreateCubeTexture
		(
			DX8Wrapper::_Get_D3D_Device8(),
			width,
			mip_level_count,
			D3DUSAGE_RENDERTARGET,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);

		if (ret==D3DERR_NOTAVAILABLE)
		{
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return nullptr;
		}

		// If ran out of texture ram, try invalidating some textures and mesh cache.
		if (ret==D3DERR_OUTOFVIDEOMEMORY)
		{
			WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets..."));
			// Free all textures that haven't been used in the last 5 seconds
			TextureClass::Invalidate_Old_Unused_Textures(5000);

			// Invalidate the mesh cache
			WW3D::_Invalidate_Mesh_Cache();

			ret=D3DXCreateCubeTexture
			(
				DX8Wrapper::_Get_D3D_Device8(),
				width,
				mip_level_count,
				D3DUSAGE_RENDERTARGET,
				WW3DFormat_To_D3DFormat(format),
				pool,
				&texture
			);

			if (SUCCEEDED(ret))
			{
				WWDEBUG_SAY(("...Render target creation successful."));
			}
			else
			{
				WWDEBUG_SAY(("...Render target creation failed."));
			}
			if (ret==D3DERR_OUTOFVIDEOMEMORY)
			{
				Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
				return nullptr;
			}
		}

		DX8_ErrorCode(ret);
		// Just return the texture, no reduction
		// allowed for render targets.
		return texture;
	}

	// Ronin @feature 08/11/2026 DX9Ex: managed on 9Ex becomes a lockable default-pool texture (see _Get_Managed_Pool).
	DWORD managedUsage=0;
	pool=_Get_Managed_Pool(pool, &managedUsage);

	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=D3DXCreateCubeTexture
	(
		DX8Wrapper::_Get_D3D_Device8(),
		width,
		mip_level_count,
		managedUsage,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture
	);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY)
	{
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets..."));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateCubeTexture
		(
			DX8Wrapper::_Get_D3D_Device8(),
			width,
			mip_level_count,
			managedUsage,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);
		if (SUCCEEDED(ret))
		{
			WWDEBUG_SAY(("...Texture creation successful."));
		}
		else
		{
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d",width,height,format_name.str(),mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}

/*!
 * KJM create volume texture
 */
IDirect3DVolumeTexture8* DX8Wrapper::_Create_DX8_Volume_Texture
(
	unsigned int width,
	unsigned int height,
	unsigned int depth,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DVolumeTexture8* texture=nullptr;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.


	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	// Ronin @feature 08/11/2026 DX9Ex: managed on 9Ex becomes a lockable default-pool texture (see _Get_Managed_Pool).
	DWORD managedUsage=0;
	pool=_Get_Managed_Pool(pool, &managedUsage);
	unsigned ret=D3DXCreateVolumeTexture
	(
		DX8Wrapper::_Get_D3D_Device8(),
		width,
		height,
		depth,
		mip_level_count,
		managedUsage,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture
	);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY)
	{
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets..."));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateVolumeTexture
		(
			DX8Wrapper::_Get_D3D_Device8(),
			width,
			height,
			depth,
			mip_level_count,
			managedUsage,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);
		if (SUCCEEDED(ret))
		{
			WWDEBUG_SAY(("...Texture creation successful."));
		}
		else
		{
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d",width,height,format_name.str(),mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}


IDirect3DSurface8 * DX8Wrapper::_Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	IDirect3DSurface8 *surface = nullptr;

	// Paletted surfaces not supported!
	WWASSERT(format!=D3DFMT_P8);

	DX8CALL(CreateOffscreenPlainSurface(width, height, WW3DFormat_To_D3DFormat(format), D3DPOOL_SYSTEMMEM, &surface, NULL));

	return surface;
}

IDirect3DSurface8 * DX8Wrapper::_Create_DX8_Surface(const char *filename_)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Note: Since there is no "D3DXCreateSurfaceFromFile" and no "GetSurfaceInfoFromFile" (the
	// latter is supposed to be added to D3DX in a future version), we create a texture from the
	// file (w/o mipmaps), check that its surface is equal to the original file data (which it
	// will not be if the file is not in a texture-supported format or size). If so, copy its
	// surface (we might be able to just get its surface and add a ref to it but I'm not sure so
	// I'm not going to risk it) and release the texture. If not, create a surface according to
	// the file data and use D3DXLoadSurfaceFromFile. This is a horrible hack, but it saves us
	// having to write file loaders. Will fix this when D3DX provides us with the right functions.
	// Create a surface the size of the file image data
	IDirect3DSurface8 *surface = nullptr;

	{

		file_auto_ptr myfile(_TheFileFactory,filename_);
		// If file not found, create a surface with missing texture in it

		if (!myfile->Is_Available()) {
			// If file not found, try the dds format
			// else create a surface with missing texture in it
			char compressed_name[200];
			strlcpy(compressed_name,filename_, sizeof(compressed_name));
			char *ext = strstr(compressed_name, ".");
			if ( ext && (strlen(ext)==4) &&
				  ( (ext[1] == 't') || (ext[1] == 'T') ) &&
				  ( (ext[2] == 'g') || (ext[2] == 'G') ) &&
				  ( (ext[3] == 'a') || (ext[3] == 'A') ) ) {
				ext[1]='d';
				ext[2]='d';
				ext[3]='s';
			}
			file_auto_ptr myfile2(_TheFileFactory,compressed_name);
			if (!myfile2->Is_Available())
				return MissingTexture::_Create_Missing_Surface();
		}
	}

	StringClass filename_string(filename_,true);
	surface=TextureLoader::Load_Surface_Immediate(
		filename_string,
		WW3D_FORMAT_UNKNOWN,
		true);
	return surface;
}


/***********************************************************************************************
 * DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/26/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void DX8Wrapper::_Update_Texture(TextureClass *system, TextureClass *video)
{
	WWASSERT(system);
	WWASSERT(video);
	WWASSERT(system->Get_Pool()==TextureClass::POOL_SYSTEMMEM);
	WWASSERT(video->Get_Pool()==TextureClass::POOL_DEFAULT);
	DX8CALL(UpdateTexture(system->Peek_D3D_Base_Texture(),video->Peek_D3D_Base_Texture()));
}

void DX8Wrapper::Compute_Caps(WW3DFormat display_format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	delete CurrentCaps;
	CurrentCaps=new DX8Caps(_Get_D3D8(),D3DDevice,display_format,Get_Current_Adapter_Identifier());
}


void DX8Wrapper::Set_Light(unsigned index, const D3DLIGHT9* light)
{
	// @bugfix Ronin 26/03/2026 DX9: Guard wrapper light writes against MAX_LIGHTS.
	WWASSERT(index < MAX_LIGHTS);
	if (index >= MAX_LIGHTS) {
		return;
	}

	if (light) {
		render_state.Lights[index]=*light;
		render_state.LightEnable[index]=true;
	}
	else {
		render_state.LightEnable[index]=false;
	}
	render_state_changed|=(LIGHT0_CHANGED<<index);
}

void DX8Wrapper::Set_Light(unsigned index,const LightClass &light)
{
	D3DLIGHT9 dlight;
	Vector3 temp;
	memset(&dlight,0,sizeof(D3DLIGHT9));

	switch (light.Get_Type())
	{
	case LightClass::POINT:
		{
			dlight.Type=D3DLIGHT_POINT;
		}
		break;
	case LightClass::DIRECTIONAL:
		{
			dlight.Type=D3DLIGHT_DIRECTIONAL;
		}
		break;
	case LightClass::SPOT:
		{
			dlight.Type=D3DLIGHT_SPOT;
		}
		break;
	}

	light.Get_Diffuse(&temp);
	temp*=light.Get_Intensity();
	dlight.Diffuse.r=temp.X;
	dlight.Diffuse.g=temp.Y;
	dlight.Diffuse.b=temp.Z;
	dlight.Diffuse.a=1.0f;

	light.Get_Specular(&temp);
	temp*=light.Get_Intensity();
	dlight.Specular.r=temp.X;
	dlight.Specular.g=temp.Y;
	dlight.Specular.b=temp.Z;
	dlight.Specular.a=1.0f;

	light.Get_Ambient(&temp);
	temp*=light.Get_Intensity();
	dlight.Ambient.r=temp.X;
	dlight.Ambient.g=temp.Y;
	dlight.Ambient.b=temp.Z;
	dlight.Ambient.a=1.0f;

	temp=light.Get_Position();
	dlight.Position=To_D3DVECTOR(temp);

	light.Get_Spot_Direction(temp);
	dlight.Direction=To_D3DVECTOR(temp);

	dlight.Range=light.Get_Attenuation_Range();
	dlight.Falloff=light.Get_Spot_Exponent();
	dlight.Theta=light.Get_Spot_Angle();
	dlight.Phi=light.Get_Spot_Angle();

	// Inverse linear light 1/(1+D)
	double a,b;
	light.Get_Far_Attenuation_Range(a,b);
	dlight.Attenuation0=1.0f;
	if (fabs(a-b)<1e-5)
		// if the attenuation range is too small assume uniform with cutoff
		dlight.Attenuation1=0.0f;
	else
		// this will cause the light to drop to half intensity at the first far attenuation
		dlight.Attenuation1=(float) 1.0/a;
	dlight.Attenuation2=0.0f;

	Set_Light(index,&dlight);
}

//**********************************************************************************************
//! Set the light environment. This is a lighting model which used up to four
//! directional lights to produce the lighting.
/*! 5/27/02 KJM Added shader light environment support
*/
void DX8Wrapper::Set_Light_Environment(LightEnvironmentClass* light_env)
{
	// Shader light environment support															*
	Light_Environment=light_env;

	if (light_env)
	{
		int light_count = light_env->Get_Light_Count();
		unsigned int color=Convert_Color(light_env->Get_Equivalent_Ambient(),0.0f);
		if (RenderStates[D3DRS_AMBIENT]!=color)
		{
			Set_DX8_Render_State(D3DRS_AMBIENT,color);
//buggy Radeon 9700 driver doesn't apply new ambient unless the material also changes.
#if 1
			render_state_changed|=MATERIAL_CHANGED;
#endif
		}

		D3DLIGHT9 light;
		int l=0;
		for (;l<light_count;++l) {

			::ZeroMemory(&light, sizeof(D3DLIGHT9));

			light.Type=D3DLIGHT_DIRECTIONAL;
			light.Diffuse=To_D3DCOLORVALUE(light_env->Get_Light_Diffuse(l),0.0f);
			light.Direction=To_D3DVECTOR(-light_env->Get_Light_Direction(l));

			// (gth) TODO: put specular into LightEnvironment?  Much work to be done on lights :-)'
			if (l==0) {
				light.Specular.r = light.Specular.g = light.Specular.b = 1.0f;
			}

			if (light_env->isPointLight(l)) {
				light.Type = D3DLIGHT_POINT;
				light.Diffuse=To_D3DCOLORVALUE(light_env->getPointDiffuse(l),0.0f);
				light.Ambient=To_D3DCOLORVALUE(light_env->getPointAmbient(l),0.0f);
				light.Position=To_D3DVECTOR(light_env->getPointCenter(l));
				light.Range = light_env->getPointOrad(l);

				// @tweak Ronin 26/03/2026 DX9: Stretch effective point-light radii globally.
				// The current DX9 path already uses very soft attenuation (A2=0), so the remaining
				// "too short" look is most likely the hard Range cutoff rather than Attenuation1.
				const double pointLightRadiusScale = 1.15;  //If set in a value that affects other units, we can't batch their meshes for instancing..

				// Inverse linear light 1/(1+D)
				double a,b;
				b = light_env->getPointOrad(l) * pointLightRadiusScale;
				a = light_env->getPointIrad(l) * pointLightRadiusScale;

				//light.Range = (float)b;

//(gth) CNC3 Generals code for the attenuation factors is causing the lights to over-brighten
//I'm changing the Attenuation0 parameter to 1.0 to avoid this problem.
#if 0
				light.Attenuation0=0.01f;
#else
				light.Attenuation0=1.0f;
#endif
				if (fabs(a - b) < 1e-5) {
					// if the attenuation range is too small assume uniform with cutoff
					light.Attenuation1 = 0.0f;
					light.Attenuation2 = 0.0f;
				}
				else {
					// double safeInner = (a < 1.0) ? 1.0 : a;
					const double safeOuterSq = (b < 1.0) ? 1.0 : (b * b);
					const double pointLightQuadraticStrength = 1.75;

					// this will cause the light to drop to half intensity at the first far attenuation
					light.Attenuation1 = (float)(0.1 / a);
					light.Attenuation2 = (float)(pointLightQuadraticStrength / safeOuterSq);
				}
			}

			Set_Light(l,&light);
		}

		// @refactor Ronin 26/03/2026 DX9: Clear all remaining wrapper light slots via MAX_LIGHTS.
		for (; l < (int)MAX_LIGHTS; ++l) {
			Set_Light((unsigned)l, nullptr);
		}
	}
/*	else {
		for (int l=0;l<4;++l) {
			Set_Light(l,nullptr);
		}
	}
*/
}

IDirect3DSurface8 * DX8Wrapper::_Get_DX8_Front_Buffer()
{
	DX8_THREAD_ASSERT();
	D3DDISPLAYMODE mode;

	DX8CALL(GetDisplayMode(D3DADAPTER_DEFAULT, &mode));

	IDirect3DSurface8 * fb=nullptr;

	DX8CALL(CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &fb, NULL));

	DX8CALL(GetFrontBufferData(0, fb));
	return fb;
}

SurfaceClass * DX8Wrapper::_Get_DX8_Back_Buffer(unsigned int num)
{
	DX8_THREAD_ASSERT();

	IDirect3DSurface8 * bb;
	SurfaceClass *surf=nullptr;
	DX8CALL(GetBackBuffer(0, num,D3DBACKBUFFER_TYPE_MONO,&bb)); // Swapchain 0
	if (bb)
	{
		surf=NEW_REF(SurfaceClass,(bb));
		bb->Release();
	}

	return surf;
}

  // @build Ronin 29/10/2025 DX9: Sampler state management (texture filtering moved from texture stage states)
	// DX9 moved these states from SetTextureStageState to SetSamplerState:
	// D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
	// D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAXANISOTROPY

/*void DX8Wrapper::Set_DX8_Sampler_State(unsigned int stage, D3DSAMPLERSTATETYPE type, unsigned int value)
{
	DX8_THREAD_ASSERT();
	if (_Get_D3D_Device8()) {
		_Get_D3D_Device8()->SetSamplerState(stage, type, value);
		DX8_RECORD_DX8_CALLS();
	}
}*/


TextureClass *
DX8Wrapper::Create_Render_Target (int width, int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) {
		D3DDISPLAYMODE mode;
		// Ronin @build 28/10/2025 DX9: GetDisplayMode requires adapter index
		DX8CALL(GetDisplayMode(D3DADAPTER_DEFAULT, &mode));
		format=D3DFormat_To_WW3DFormat(mode.Format);
	}

	// If render target format isn't supported return null
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format)) {
		WWDEBUG_SAY(("DX8Wrapper - Render target format is not supported"));
		return nullptr;
	}

	//
	//	Note: We're going to force the width and height to be powers of two and equal
	//
	const D3DCAPS9& dx8caps=Get_Current_Caps()->Get_DX8_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width) {
		poweroftwosize = height;
	}
	poweroftwosize = std::bit_ceil(static_cast<unsigned int>(poweroftwosize));

	if (poweroftwosize>dx8caps.MaxTextureWidth) {
		poweroftwosize=dx8caps.MaxTextureWidth;
	}
	if (poweroftwosize>dx8caps.MaxTextureHeight) {
		poweroftwosize=dx8caps.MaxTextureHeight;
	}

	width = height = poweroftwosize;

	//
	//	Attempt to create the render target
	//
	TextureClass * tex = NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr)
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!"));
		REF_PTR_RELEASE(tex);
	}

	return tex;
}

//**********************************************************************************************
//! Create render target with associated depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Create_Render_Target
(
	int width,
	int height,
	WW3DFormat format,
	WW3DZFormat zformat,
	TextureClass** target,
	ZTextureClass** depth_buffer
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN)
	{
		*target=nullptr;
		*depth_buffer=nullptr;
		return;
	}

	// If render target format isn't supported return null
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format) ||
		 !Get_Current_Caps()->Support_Depth_Stencil_Format(zformat))
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target with depth format is not supported"));
		return;
	}

	//	Note: We're going to force the width and height to be powers of two and equal
	const D3DCAPS9& dx8caps=Get_Current_Caps()->Get_DX8_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width)
	{
		poweroftwosize = height;
	}
	poweroftwosize = std::bit_ceil(static_cast<unsigned int>(poweroftwosize));

	if (poweroftwosize>dx8caps.MaxTextureWidth)
	{
		poweroftwosize=dx8caps.MaxTextureWidth;
	}

	if (poweroftwosize>dx8caps.MaxTextureHeight)
	{
		poweroftwosize=dx8caps.MaxTextureHeight;
	}

	width = height = poweroftwosize;

	//	Attempt to create the render target
	TextureClass* tex=NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr)
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!"));
		REF_PTR_RELEASE(tex);
	}

	*target=tex;

	// attempt to create the depth stencil buffer
	*depth_buffer=NEW_REF
	(
		ZTextureClass,
		(
			width,
			height,
			zformat,
			MIP_LEVELS_1,
			TextureClass::POOL_DEFAULT
		)
	);
}

/*!
 * Set render target
 * KM Added optional custom z target
 */
void DX8Wrapper::Set_Render_Target_With_Z
(
	TextureClass* texture,
	ZTextureClass* ztexture
)
{
	WWASSERT(texture!=nullptr);
	IDirect3DSurface8 * d3d_surf = texture->Get_D3D_Surface_Level();
	WWASSERT(d3d_surf != nullptr);

	IDirect3DSurface8* d3d_zbuf=nullptr;
	if (ztexture!=nullptr)
	{

		d3d_zbuf=ztexture->Get_D3D_Surface_Level();
		WWASSERT(d3d_zbuf!=nullptr);
		Set_Render_Target(d3d_surf,d3d_zbuf);
		d3d_zbuf->Release();
	}
	else
	{
		Set_Render_Target(d3d_surf,true);
	}
	d3d_surf->Release();

	IsRenderToTexture = true;
}

void
DX8Wrapper::Set_Render_Target(IDirect3DSwapChain8 *swap_chain)
{
	DX8_THREAD_ASSERT();
	WWASSERT (swap_chain != nullptr);

	//
	//	Get the back buffer for the swap chain
	//
	IDirect3DSurface9* render_target = nullptr;
swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &render_target);


	//
	//	Set this back buffer as the render target
	//
	Set_Render_Target (render_target, true);

	// Ronin @bugfix 27/02/2026 DX9: Release the ref from GetBackBuffer (Set_Render_Target AddRef'd its own copy)
	if (render_target) {
		render_target->Release();
		render_target = nullptr;
	}

	//
  	//	Release our hold on the "current" render target
	//
	if (CurrentRenderTarget != nullptr) {
		CurrentRenderTarget->Release();
		CurrentRenderTarget = nullptr;
	}

	IsRenderToTexture = false;
}

void
DX8Wrapper::Set_Render_Target(IDirect3DSurface8 *render_target, bool use_default_depth_buffer)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget)
	{
		// If there is currently a custom render target, default must NOT be null.
		if (CurrentRenderTarget)
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr)
		{
			//DX8CALL(SetRenderTarget (DefaultRenderTarget, DefaultDepthBuffer));
			// Ronin @bugfix 20/09/2026 DX9: UNBIND DEPTH FIRST. DX8's SetRenderTarget(target, z) set both atomically;
			// DX9 split it in two, and D3D9 refuses a render target whose multisample type or size does not match the
			// depth-stencil ALREADY bound. With MSAA on, the frame's depth is multisampled and every render-to-texture
			// target is not, so this bind failed and the pass silently did nothing — shadow maps went dark. NULL first
			// is always legal and makes the pair order-independent.
			DX8CALL(SetDepthStencilSurface(nullptr));
			DX8CALL(SetRenderTarget(0, DefaultRenderTarget));
			if (DefaultDepthBuffer) {
				DX8CALL(SetDepthStencilSurface(DefaultDepthBuffer));
			}
			DefaultRenderTarget->Release ();
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer)
			{
				DefaultDepthBuffer->Release ();
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=nullptr;
		}

	}
	else if (render_target != CurrentRenderTarget)
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr)
		{
//		IDirect3DSurface8 *depth_buffer = nullptr;
			DX8CALL(GetDepthStencilSurface (&DefaultDepthBuffer));
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr)
		{
			// Ronin @build 28/10/2025 DX9: GetRenderTarget signature changed
			DX8CALL(GetRenderTarget(0, &DefaultRenderTarget));
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->AddRef ();

			//
			//	Switch render targets
			//
			if (use_default_depth_buffer)
			{				
				// DX8CALL(SetRenderTarget (CurrentRenderTarget, DefaultDepthBuffer));
				// Ronin @bugfix 20/09/2026 DX9: unbind depth first — see the note in the restore branch above.
				DX8CALL(SetDepthStencilSurface(nullptr));
				DX8CALL(SetRenderTarget(0, CurrentRenderTarget));
				if (DefaultDepthBuffer) {
					DX8CALL(SetDepthStencilSurface(DefaultDepthBuffer));
				}
			}
			else
			{
				//DX8CALL(SetRenderTarget (CurrentRenderTarget, NULL));
				// Ronin @bugfix 20/09/2026 DX9: unbind depth first — see the note in the restore branch above.
				DX8CALL(SetDepthStencilSurface(nullptr));
				DX8CALL(SetRenderTarget(0, CurrentRenderTarget));
			}
		}
	}

	//
	//	Free our hold on the depth buffer
	//
//	if (depth_buffer != nullptr) {
//		depth_buffer->Release ();
//		depth_buffer = nullptr;
//	}

	IsRenderToTexture = false;
}


//**********************************************************************************************
//! Set render target with depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Set_Render_Target
(
	IDirect3DSurface8* render_target,
	IDirect3DSurface8* depth_buffer
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget)
	{
		// If there is currently a custom render target, default must NOT be null.
		if (CurrentRenderTarget)
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr)
		{
			//DX8CALL(SetRenderTarget (DefaultRenderTarget, DefaultDepthBuffer));
			// Ronin @bugfix 20/09/2026 DX9: UNBIND DEPTH FIRST. DX8's SetRenderTarget(target, z) set both atomically;
			// DX9 split it in two, and D3D9 refuses a render target whose multisample type or size does not match the
			// depth-stencil ALREADY bound. With MSAA on, the frame's depth is multisampled and every render-to-texture
			// target is not, so this bind failed and the pass silently did nothing — shadow maps went dark. NULL first
			// is always legal and makes the pair order-independent.
			DX8CALL(SetDepthStencilSurface(nullptr));
			DX8CALL(SetRenderTarget(0, DefaultRenderTarget));
			if (DefaultDepthBuffer) {
				DX8CALL(SetDepthStencilSurface(DefaultDepthBuffer));
			}
			DefaultRenderTarget->Release ();
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer)
			{
				DefaultDepthBuffer->Release ();
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=nullptr;
		}
	}
	else if (render_target != CurrentRenderTarget)
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr)
		{
//		IDirect3DSurface8 *depth_buffer = nullptr;
			DX8CALL(GetDepthStencilSurface (&DefaultDepthBuffer));
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr)
		{
			// Ronin @build 28/10/2025 DX9: GetRenderTarget signature changed
			DX8CALL(GetRenderTarget(0, &DefaultRenderTarget));
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		CurrentDepthBuffer = depth_buffer;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr)
		{
			CurrentRenderTarget->AddRef ();
			CurrentDepthBuffer->AddRef();

			//
			//	Switch render targets
			//
			//DX8CALL(SetRenderTarget (CurrentRenderTarget, CurrentDepthBuffer));
			// Ronin @bugfix 20/09/2026 DX9: unbind depth first — see the note in the restore branch above. THIS is the
			// bind Set_Render_Target_With_Z uses, so this is where MSAA killed the shadow maps.
			DX8CALL(SetDepthStencilSurface(nullptr));
			DX8CALL(SetRenderTarget(0, CurrentRenderTarget));
			if (CurrentDepthBuffer) {
				DX8CALL(SetDepthStencilSurface(CurrentDepthBuffer));
			}
		}
	}

	IsRenderToTexture=true;
}


IDirect3DSwapChain8 *
DX8Wrapper::Create_Additional_Swap_Chain (HWND render_window)
{
	DX8_Assert();

	//
	//	Configure the presentation parameters for a windowed render target
	//
	D3DPRESENT_PARAMETERS params			= { 0 };
	params.BackBufferFormat						= _PresentParameters.BackBufferFormat;
	params.BackBufferCount						= 1;
	params.MultiSampleType						= D3DMULTISAMPLE_NONE;
	params.SwapEffect									= D3DSWAPEFFECT_COPY; // Ronin @build 27/10/2025 DX9: _COPY_VSYNC removed
	params.hDeviceWindow							= render_window;
	params.Windowed										= TRUE;
	params.EnableAutoDepthStencil			= TRUE;
	params.AutoDepthStencilFormat			= _PresentParameters.AutoDepthStencilFormat;
	params.Flags											= 0;
	params.FullScreen_RefreshRateInHz	= D3DPRESENT_RATE_DEFAULT;
	params.PresentationInterval				= D3DPRESENT_INTERVAL_DEFAULT; // Ronin @build 27/10/2025 DX9: Renamed

	//
	//	Create the swap chain
	//
	IDirect3DSwapChain8 *swap_chain = nullptr;
	DX8CALL(CreateAdditionalSwapChain(&params, &swap_chain));
	return swap_chain;
}

void DX8Wrapper::Flush_DX8_Resource_Manager(unsigned int bytes)
{
	DX8_Assert();
	DX8CALL(EvictManagedResources());  // Closest equivalent to ResourceManagerDiscardBytes
}

unsigned int DX8Wrapper::Get_Free_Texture_RAM()
{
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();
	return DX8Wrapper::_Get_D3D_Device8()->GetAvailableTextureMem();
}

// Converts a linear gamma ramp to one that is controlled by:
// Gamma - controls the curvature of the middle of the curve
// Bright - controls the minimum value of the curve
// Contrast - controls the difference between the maximum and the minimum of the curve
void DX8Wrapper::Set_Gamma(float gamma,float bright,float contrast,bool calibrate,bool uselimit)
{
	gamma=Bound(gamma,0.6f,6.0f);
	bright=Bound(bright,-0.5f,0.5f);
	contrast=Bound(contrast,0.5f,2.0f);
	float oo_gamma=1.0f/gamma;

	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	DWORD flag=(calibrate?D3DSGR_CALIBRATE:D3DSGR_NO_CALIBRATION);

	D3DGAMMARAMP ramp;
	float			 limit;

	// IML: I'm not really sure what the intent of the 'limit' variable is. It does not produce useful results for my purposes.
	if (uselimit) {
		limit=(contrast-1)/2*contrast;
	}
	else {
		limit = 0.0f;
	}

	// HY - arrived at this equation after much trial and error.
	for (int i=0; i<256; i++) {
		float in,out;
		in=i/256.0f;
		float x=in-limit;
		x=Bound(x,0.0f,1.0f);
		x=powf(x,oo_gamma);
		out=contrast*x+bright;
		out=Bound(out,0.0f,1.0f);
		ramp.red[i]=(WORD) (out*65535);
		ramp.green[i]=(WORD) (out*65535);
		ramp.blue[i]=(WORD) (out*65535);
	}

	if (Get_Current_Caps()->Support_Gamma())	{
		DX8Wrapper::_Get_D3D_Device8()->SetGammaRamp(0, flag,&ramp); // Swapchain 0
	} else {
		HWND hwnd = GetDesktopWindow();
		HDC hdc = GetDC(hwnd);
		if (hdc)
		{
			SetDeviceGammaRamp (hdc, &ramp);
			ReleaseDC (hwnd, hdc);
		}
	}
}

namespace wrapper
{
void D3DMatrixIdentity(D3DMATRIX* dxm)
{
	memset(dxm, 0, sizeof(*dxm));
	dxm->_11 = 1.0f;
	dxm->_22 = 1.0f;
	dxm->_33 = 1.0f;
	dxm->_44 = 1.0f;
}
} // namespace wrapper

void DX8Wrapper::Set_World_Identity()
{
	if (render_state_changed&(unsigned)WORLD_IDENTITY)
		return;
	wrapper::D3DMatrixIdentity(&render_state.world);
	render_state_changed|=(unsigned)WORLD_CHANGED|(unsigned)WORLD_IDENTITY;
}

void DX8Wrapper::Set_View_Identity()
{
	if (render_state_changed&(unsigned)VIEW_IDENTITY)
		return;
	wrapper::D3DMatrixIdentity(&render_state.view);
	render_state_changed|=(unsigned)VIEW_CHANGED|(unsigned)VIEW_IDENTITY;
}

//**********************************************************************************************
//! Resets render device to default state
/*!
*/
void DX8Wrapper::Apply_Default_State()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Default_State()"));

	// only set states used in game
	Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
//	Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	//Set_DX8_Render_State(D3DRS_LINEPATTERN, 0);
	Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_LASTPIXEL, FALSE);
	Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_ALPHAREF, 0);
	Set_DX8_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_DITHERENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_SPECULARENABLE, FALSE);
//	Set_DX8_Render_State(D3DRS_ZVISIBLE, FALSE);
//	Set_DX8_Render_State(D3DRS_FOGCOLOR, 0);
//	Set_DX8_Render_State(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//	Set_DX8_Render_State(D3DRS_FOGSTART, 0);

//	Set_DX8_Render_State(D3DRS_FOGEND, WWMath::Float_As_Int(1.0f));
//	Set_DX8_Render_State(D3DRS_FOGDENSITY, WWMath::Float_As_Int(1.0f));

	//Set_DX8_Render_State(D3DRS_EDGEANTIALIAS, FALSE);
	Set_DX8_Render_State(D3DRS_DEPTHBIAS, 0);
//	Set_DX8_Render_State(D3DRS_RANGEFOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	Set_DX8_Render_State(D3DRS_STENCILREF, 0);
	Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0);
/*	Set_DX8_Render_State(D3DRS_WRAP0, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP1, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP2, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP3, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP4, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP5, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP6, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP7, D3DWRAP_U| D3DWRAP_V);*/
	Set_DX8_Render_State(D3DRS_CLIPPING, TRUE);
	Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
	//Set_DX8_Render_State(D3DRS_AMBIENT, 0);
//	Set_DX8_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, TRUE);
/*	Set_DX8_Render_State(D3DRS_LOCALVIEWER, TRUE);
	Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, FALSE);
	Set_DX8_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	Set_DX8_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);*/
	//Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, 0);
	//Set_DX8_Render_State(D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSIZE, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MIN, 0);
	//Set_DX8_Render_State(D3DRS_POINTSPRITEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_A, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_B, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_C, 0);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEMASK, 0xffffffff);
	//Set_DX8_Render_State(D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE);
	//Set_DX8_Render_State(D3DRS_PATCHSEGMENTS, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_DEBUGMONITORTOKEN, D3DDMT_ENABLE);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MAX, Float_At_Int(64.0f));
	//Set_DX8_Render_State(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000f);
	//Set_DX8_Render_State(D3DRS_TWEENFACTOR, 0);
	Set_DX8_Render_State(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	//Set_DX8_Render_State(D3DRS_POSITIONORDER, D3DORDER_CUBIC);
	//Set_DX8_Render_State(D3DRS_NORMALORDER, D3DORDER_LINEAR);

	// disable TSS stages
	// 
// Ronin @bugfix 13/11/2025: DX9 fixed-function pipeline requires Stage 0 ENABLED
// Original code disabled ALL stages (including 0), breaking texture rendering.
// Stage 0 must have MODULATE blending for textured primitives to appear.
	
	// Configure Stage 0 for standard fixed-function texturing
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	Set_DX8_Sampler_State(0, D3DSAMP_BORDERCOLOR, 0);
	Set_DX8_Sampler_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	Set_DX8_Sampler_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	Set_DX8_Sampler_State(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	Set_DX8_Sampler_State(0, D3DSAMP_MAXANISOTROPY, 1);

		Set_Texture(0, nullptr);

		// Disable all other stages (1+)
		int i;
		for (i = 1; i < CurrentCaps->Get_Max_Textures_Per_Pass(); i++)
		{
			Set_DX8_Texture_Stage_State(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
			Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

			Set_DX8_Texture_Stage_State(i, D3DTSS_TEXCOORDINDEX, i);
			Set_DX8_Texture_Stage_State(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

			Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			Set_DX8_Sampler_State(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
			Set_DX8_Sampler_State(i, D3DSAMP_BORDERCOLOR, 0);
			Set_DX8_Sampler_State(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			Set_DX8_Sampler_State(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			Set_DX8_Sampler_State(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
			Set_DX8_Sampler_State(i, D3DSAMP_MAXANISOTROPY, 1);

			Set_Texture(i, nullptr);
		}
	

//	DX8Wrapper::Set_Material(nullptr);
	VertexMaterialClass::Apply_Null();

	// @refactor Ronin 26/03/2026 DX9: Clear all wrapper light slots using shared MAX_LIGHTS.
	for (unsigned index = 0; index < MAX_LIGHTS; ++index) {
		SNAPSHOT_SAY(("Clearing light %d to null",index));
		Set_DX8_Light(index,nullptr);
	}

	// set up simple default TSS
	Vector4 vconst[MAX_VERTEX_SHADER_CONSTANTS];
	memset(vconst,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	Set_Vertex_Shader_Constant(0, vconst, MAX_VERTEX_SHADER_CONSTANTS);

	Vector4 pconst[MAX_PIXEL_SHADER_CONSTANTS];
	memset(pconst,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	Set_Pixel_Shader_Constant(0, pconst, MAX_PIXEL_SHADER_CONSTANTS);

	BindLayoutFVF(DX8_FVF_XYZNDUV2, "Apply_default_State");
	Set_Pixel_Shader(0);

	ShaderClass::Invalidate();
}

const char* DX8Wrapper::Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state)
{
	switch (state) {
	case D3DRS_ZENABLE                       : return "D3DRS_ZENABLE";
	case D3DRS_FILLMODE                      : return "D3DRS_FILLMODE";
	case D3DRS_SHADEMODE                     : return "D3DRS_SHADEMODE";
	// Ronin @build 27/10/2025 DX9: D3DRS_LINEPATTERN removed in DX9 - case commented out
	// 	case D3DRS_LINEPATTERN                   : return "D3DRS_LINEPATTERN";
	case D3DRS_ZWRITEENABLE                  : return "D3DRS_ZWRITEENABLE";
	case D3DRS_ALPHATESTENABLE               : return "D3DRS_ALPHATESTENABLE";
	case D3DRS_LASTPIXEL                     : return "D3DRS_LASTPIXEL";
	case D3DRS_SRCBLEND                      : return "D3DRS_SRCBLEND";
	case D3DRS_DESTBLEND                     : return "D3DRS_DESTBLEND";
	case D3DRS_CULLMODE                      : return "D3DRS_CULLMODE";
	case D3DRS_ZFUNC                         : return "D3DRS_ZFUNC";
	case D3DRS_ALPHAREF                      : return "D3DRS_ALPHAREF";
	case D3DRS_ALPHAFUNC                     : return "D3DRS_ALPHAFUNC";
	case D3DRS_DITHERENABLE                  : return "D3DRS_DITHERENABLE";
	case D3DRS_ALPHABLENDENABLE              : return "D3DRS_ALPHABLENDENABLE";
	case D3DRS_FOGENABLE                     : return "D3DRS_FOGENABLE";
	case D3DRS_SPECULARENABLE                : return "D3DRS_SPECULARENABLE";
	// Ronin @build 27/10/2025 DX9: D3DRS_ZVISIBLE removed in DX9 - case commented out
	// 	case D3DRS_ZVISIBLE                      : return "D3DRS_ZVISIBLE";
	case D3DRS_FOGCOLOR                      : return "D3DRS_FOGCOLOR";
	case D3DRS_FOGTABLEMODE                  : return "D3DRS_FOGTABLEMODE";
	case D3DRS_FOGSTART                      : return "D3DRS_FOGSTART";
	case D3DRS_FOGEND                        : return "D3DRS_FOGEND";
	case D3DRS_FOGDENSITY                    : return "D3DRS_FOGDENSITY";
	// Ronin @build 27/10/2025 DX9: D3DRS_EDGEANTIALIAS removed in DX9 - case commented out
	// 	case D3DRS_EDGEANTIALIAS                 : return "D3DRS_EDGEANTIALIAS";
	case D3DRS_DEPTHBIAS                     : return "D3DRS_DEPTHBIAS";
	case D3DRS_RANGEFOGENABLE                : return "D3DRS_RANGEFOGENABLE";
	case D3DRS_STENCILENABLE                 : return "D3DRS_STENCILENABLE";
	case D3DRS_STENCILFAIL                   : return "D3DRS_STENCILFAIL";
	case D3DRS_STENCILZFAIL                  : return "D3DRS_STENCILZFAIL";
	case D3DRS_STENCILPASS                   : return "D3DRS_STENCILPASS";
	case D3DRS_STENCILFUNC                   : return "D3DRS_STENCILFUNC";
	case D3DRS_STENCILREF                    : return "D3DRS_STENCILREF";
	case D3DRS_STENCILMASK                   : return "D3DRS_STENCILMASK";
	case D3DRS_STENCILWRITEMASK              : return "D3DRS_STENCILWRITEMASK";
	case D3DRS_TEXTUREFACTOR                 : return "D3DRS_TEXTUREFACTOR";
	case D3DRS_WRAP0                         : return "D3DRS_WRAP0";
	case D3DRS_WRAP1                         : return "D3DRS_WRAP1";
	case D3DRS_WRAP2                         : return "D3DRS_WRAP2";
	case D3DRS_WRAP3                         : return "D3DRS_WRAP3";
	case D3DRS_WRAP4                         : return "D3DRS_WRAP4";
	case D3DRS_WRAP5                         : return "D3DRS_WRAP5";
	case D3DRS_WRAP6                         : return "D3DRS_WRAP6";
	case D3DRS_WRAP7                         : return "D3DRS_WRAP7";
	case D3DRS_CLIPPING                      : return "D3DRS_CLIPPING";
	case D3DRS_LIGHTING                      : return "D3DRS_LIGHTING";
	case D3DRS_AMBIENT                       : return "D3DRS_AMBIENT";
	case D3DRS_FOGVERTEXMODE                 : return "D3DRS_FOGVERTEXMODE";
	case D3DRS_COLORVERTEX                   : return "D3DRS_COLORVERTEX";
	case D3DRS_LOCALVIEWER                   : return "D3DRS_LOCALVIEWER";
	case D3DRS_NORMALIZENORMALS              : return "D3DRS_NORMALIZENORMALS";
	case D3DRS_DIFFUSEMATERIALSOURCE         : return "D3DRS_DIFFUSEMATERIALSOURCE";
	case D3DRS_SPECULARMATERIALSOURCE        : return "D3DRS_SPECULARMATERIALSOURCE";
	case D3DRS_AMBIENTMATERIALSOURCE         : return "D3DRS_AMBIENTMATERIALSOURCE";
	case D3DRS_EMISSIVEMATERIALSOURCE        : return "D3DRS_EMISSIVEMATERIALSOURCE";
	case D3DRS_VERTEXBLEND                   : return "D3DRS_VERTEXBLEND";
	case D3DRS_CLIPPLANEENABLE               : return "D3DRS_CLIPPLANEENABLE";
	// Ronin @build 27/10/2025 DX9: D3DRS_SOFTWAREVERTEXPROCESSING removed in DX9 - case commented out
	// 	case D3DRS_SOFTWAREVERTEXPROCESSING      : return "D3DRS_SOFTWAREVERTEXPROCESSING";
	case D3DRS_POINTSIZE                     : return "D3DRS_POINTSIZE";
	case D3DRS_POINTSIZE_MIN                 : return "D3DRS_POINTSIZE_MIN";
	case D3DRS_POINTSPRITEENABLE             : return "D3DRS_POINTSPRITEENABLE";
	case D3DRS_POINTSCALEENABLE              : return "D3DRS_POINTSCALEENABLE";
	case D3DRS_POINTSCALE_A                  : return "D3DRS_POINTSCALE_A";
	case D3DRS_POINTSCALE_B                  : return "D3DRS_POINTSCALE_B";
	case D3DRS_POINTSCALE_C                  : return "D3DRS_POINTSCALE_C";
	case D3DRS_MULTISAMPLEANTIALIAS          : return "D3DRS_MULTISAMPLEANTIALIAS";
	case D3DRS_MULTISAMPLEMASK               : return "D3DRS_MULTISAMPLEMASK";
	case D3DRS_PATCHEDGESTYLE                : return "D3DRS_PATCHEDGESTYLE";
	// Ronin @build 27/10/2025 DX9: D3DRS_PATCHSEGMENTS removed in DX9 - case commented out
	// 	case D3DRS_PATCHSEGMENTS                 : return "D3DRS_PATCHSEGMENTS";
	case D3DRS_DEBUGMONITORTOKEN             : return "D3DRS_DEBUGMONITORTOKEN";
	case D3DRS_POINTSIZE_MAX                 : return "D3DRS_POINTSIZE_MAX";
	case D3DRS_INDEXEDVERTEXBLENDENABLE      : return "D3DRS_INDEXEDVERTEXBLENDENABLE";
	case D3DRS_COLORWRITEENABLE              : return "D3DRS_COLORWRITEENABLE";
	case D3DRS_TWEENFACTOR                   : return "D3DRS_TWEENFACTOR";
	case D3DRS_BLENDOP                       : return "D3DRS_BLENDOP";
//	case D3DRS_POSITIONORDER                 : return "D3DRS_POSITIONORDER";
//	case D3DRS_NORMALORDER                   : return "D3DRS_NORMALORDER";
	default											  : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state)
{
	switch (state) {
	case D3DTSS_COLOROP                   : return "D3DTSS_COLOROP";
	case D3DTSS_COLORARG1                 : return "D3DTSS_COLORARG1";
	case D3DTSS_COLORARG2                 : return "D3DTSS_COLORARG2";
	case D3DTSS_ALPHAOP                   : return "D3DTSS_ALPHAOP";
	case D3DTSS_ALPHAARG1                 : return "D3DTSS_ALPHAARG1";
	case D3DTSS_ALPHAARG2                 : return "D3DTSS_ALPHAARG2";
	case D3DTSS_BUMPENVMAT00              : return "D3DTSS_BUMPENVMAT00";
	case D3DTSS_BUMPENVMAT01              : return "D3DTSS_BUMPENVMAT01";
	case D3DTSS_BUMPENVMAT10              : return "D3DTSS_BUMPENVMAT10";
	case D3DTSS_BUMPENVMAT11              : return "D3DTSS_BUMPENVMAT11";
	case D3DTSS_TEXCOORDINDEX             : return "D3DTSS_TEXCOORDINDEX";
	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSU removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSU                  : return "D3DTSS_ADDRESSU";
	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSV removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSV                  : return "D3DTSS_ADDRESSV";
	// Ronin @build 27/10/2025 DX9: D3DTSS_BORDERCOLOR removed in DX9 - case commented out
	// 	case D3DTSS_BORDERCOLOR               : return "D3DTSS_BORDERCOLOR";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MAGFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MAGFILTER                 : return "D3DTSS_MAGFILTER";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MINFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MINFILTER                 : return "D3DTSS_MINFILTER";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MIPFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MIPFILTER                 : return "D3DTSS_MIPFILTER";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MIPMAPLODBIAS removed in DX9 - case commented out
	// 	case D3DTSS_MIPMAPLODBIAS             : return "D3DTSS_MIPMAPLODBIAS";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MAXMIPLEVEL removed in DX9 - case commented out
	// 	case D3DTSS_MAXMIPLEVEL               : return "D3DTSS_MAXMIPLEVEL";
	// Ronin @build 27/10/2025 DX9: D3DTSS_MAXANISOTROPY removed in DX9 - case commented out
	// 	case D3DTSS_MAXANISOTROPY             : return "D3DTSS_MAXANISOTROPY";
	case D3DTSS_BUMPENVLSCALE             : return "D3DTSS_BUMPENVLSCALE";
	case D3DTSS_BUMPENVLOFFSET            : return "D3DTSS_BUMPENVLOFFSET";
	case D3DTSS_TEXTURETRANSFORMFLAGS     : return "D3DTSS_TEXTURETRANSFORMFLAGS";
	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSW removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSW                  : return "D3DTSS_ADDRESSW";
	case D3DTSS_COLORARG0                 : return "D3DTSS_COLORARG0";
	case D3DTSS_ALPHAARG0                 : return "D3DTSS_ALPHAARG0";
	case D3DTSS_RESULTARG                 : return "D3DTSS_RESULTARG";
	default										  : return "UNKNOWN";
	}
}

void DX8Wrapper::Get_DX8_Render_State_Value_Name(StringClass& name, D3DRENDERSTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DRS_ZENABLE:
		name=Get_DX8_ZBuffer_Type_Name(value);
		break;

	case D3DRS_FILLMODE:
		name=Get_DX8_Fill_Mode_Name(value);
		break;

	case D3DRS_SHADEMODE:
		name=Get_DX8_Shade_Mode_Name(value);
		break;

	// Ronin @build 27/10/2025 DX9: D3DRS_LINEPATTERN removed in DX9 - case commented out
	// 	case D3DRS_LINEPATTERN:
	case D3DRS_FOGCOLOR:
	case D3DRS_ALPHAREF:
	case D3DRS_STENCILMASK:
	case D3DRS_STENCILWRITEMASK:
	case D3DRS_TEXTUREFACTOR:
	case D3DRS_AMBIENT:
	case D3DRS_CLIPPLANEENABLE:
	case D3DRS_MULTISAMPLEMASK:
		name.Format("0x%x",value);
		break;

	case D3DRS_ZWRITEENABLE:
	case D3DRS_ALPHATESTENABLE:
	case D3DRS_LASTPIXEL:
	case D3DRS_DITHERENABLE:
	case D3DRS_ALPHABLENDENABLE:
	case D3DRS_FOGENABLE:
	case D3DRS_SPECULARENABLE:
	case D3DRS_STENCILENABLE:
	case D3DRS_RANGEFOGENABLE:
	// Ronin @build 27/10/2025 DX9: D3DRS_EDGEANTIALIAS removed in DX9 - case commented out
	// 	case D3DRS_EDGEANTIALIAS:
	case D3DRS_CLIPPING:
	case D3DRS_LIGHTING:
	case D3DRS_COLORVERTEX:
	case D3DRS_LOCALVIEWER:
	case D3DRS_NORMALIZENORMALS:
	// Ronin @build 27/10/2025 DX9: D3DRS_SOFTWAREVERTEXPROCESSING removed in DX9 - case commented out
	// 	case D3DRS_SOFTWAREVERTEXPROCESSING:
	case D3DRS_POINTSPRITEENABLE:
	case D3DRS_POINTSCALEENABLE:
	case D3DRS_MULTISAMPLEANTIALIAS:
	case D3DRS_INDEXEDVERTEXBLENDENABLE:
		name=value ? "TRUE" : "FALSE";
		break;

	case D3DRS_SRCBLEND:
	case D3DRS_DESTBLEND:
		name=Get_DX8_Blend_Name(value);
		break;

	case D3DRS_CULLMODE:
		name=Get_DX8_Cull_Mode_Name(value);
		break;

	case D3DRS_ZFUNC:
	case D3DRS_ALPHAFUNC:
	case D3DRS_STENCILFUNC:
		name=Get_DX8_Cmp_Func_Name(value);
		break;

	// Ronin @build 27/10/2025 DX9: D3DRS_ZVISIBLE removed in DX9 - case commented out
	// 	case D3DRS_ZVISIBLE:
		name="NOTSUPPORTED";
		break;

	case D3DRS_FOGTABLEMODE:
	case D3DRS_FOGVERTEXMODE:
		name=Get_DX8_Fog_Mode_Name(value);
		break;

	case D3DRS_FOGSTART:
	case D3DRS_FOGEND:
	case D3DRS_FOGDENSITY:
	case D3DRS_POINTSIZE:
	case D3DRS_POINTSIZE_MIN:
	case D3DRS_POINTSCALE_A:
	case D3DRS_POINTSCALE_B:
	case D3DRS_POINTSCALE_C:
	// Ronin @build 27/10/2025 DX9: D3DRS_PATCHSEGMENTS removed in DX9 - case commented out
	// 	case D3DRS_PATCHSEGMENTS:
	case D3DRS_POINTSIZE_MAX:
	case D3DRS_TWEENFACTOR:
		name.Format("%f",std::bit_cast<float>(value));
		break;

	case D3DRS_DEPTHBIAS:
	case D3DRS_STENCILREF:
		name.Format("%d",value);
		break;

	case D3DRS_STENCILFAIL:
	case D3DRS_STENCILZFAIL:
	case D3DRS_STENCILPASS:
		name=Get_DX8_Stencil_Op_Name(value);
		break;

	case D3DRS_WRAP0:
	case D3DRS_WRAP1:
	case D3DRS_WRAP2:
	case D3DRS_WRAP3:
	case D3DRS_WRAP4:
	case D3DRS_WRAP5:
	case D3DRS_WRAP6:
	case D3DRS_WRAP7:
		name="0";
		if (value&D3DWRAP_U) name+="|D3DWRAP_U";
		if (value&D3DWRAP_V) name+="|D3DWRAP_V";
		if (value&D3DWRAP_W) name+="|D3DWRAP_W";
		break;

	case D3DRS_DIFFUSEMATERIALSOURCE:
	case D3DRS_SPECULARMATERIALSOURCE:
	case D3DRS_AMBIENTMATERIALSOURCE:
	case D3DRS_EMISSIVEMATERIALSOURCE:
		name=Get_DX8_Material_Source_Name(value);
		break;

	case D3DRS_VERTEXBLEND:
		name=Get_DX8_Vertex_Blend_Flag_Name(value);
		break;

	case D3DRS_PATCHEDGESTYLE:
		name=Get_DX8_Patch_Edge_Style_Name(value);
		break;

	case D3DRS_DEBUGMONITORTOKEN:
		name=Get_DX8_Debug_Monitor_Token_Name(value);
		break;

	case D3DRS_COLORWRITEENABLE:
		name="0";
		if (value&D3DCOLORWRITEENABLE_RED) name+="|D3DCOLORWRITEENABLE_RED";
		if (value&D3DCOLORWRITEENABLE_GREEN) name+="|D3DCOLORWRITEENABLE_GREEN";
		if (value&D3DCOLORWRITEENABLE_BLUE) name+="|D3DCOLORWRITEENABLE_BLUE";
		if (value&D3DCOLORWRITEENABLE_ALPHA) name+="|D3DCOLORWRITEENABLE_ALPHA";
		break;
	case D3DRS_BLENDOP:
		name=Get_DX8_Blend_Op_Name(value);
		break;
	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

void DX8Wrapper::Get_DX8_Texture_Stage_State_Value_Name(StringClass& name, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DTSS_COLOROP:
	case D3DTSS_ALPHAOP:
		name=Get_DX8_Texture_Op_Name(value);
		break;

	case D3DTSS_COLORARG0:
	case D3DTSS_COLORARG1:
	case D3DTSS_COLORARG2:
	case D3DTSS_ALPHAARG0:
	case D3DTSS_ALPHAARG1:
	case D3DTSS_ALPHAARG2:
	case D3DTSS_RESULTARG:
		name=Get_DX8_Texture_Arg_Name(value);
		break;

	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSU removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSU:
	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSV removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSV:
	// Ronin @build 27/10/2025 DX9: D3DTSS_ADDRESSW removed in DX9 - case commented out
	// 	case D3DTSS_ADDRESSW:
		name=Get_DX8_Texture_Address_Name(value);
		break;

	// Ronin @build 27/10/2025 DX9: D3DTSS_MAGFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MAGFILTER:
	// Ronin @build 27/10/2025 DX9: D3DTSS_MINFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MINFILTER:
	// Ronin @build 27/10/2025 DX9: D3DTSS_MIPFILTER removed in DX9 - case commented out
	// 	case D3DTSS_MIPFILTER:
		name=Get_DX8_Texture_Filter_Name(value);
		break;

	case D3DTSS_TEXTURETRANSFORMFLAGS:
		name=Get_DX8_Texture_Transform_Flag_Name(value);
		break;

	// Floating point values
	// Ronin @build 27/10/2025 DX9: D3DTSS_MIPMAPLODBIAS removed in DX9 - case commented out
	// 	case D3DTSS_MIPMAPLODBIAS:
	case D3DTSS_BUMPENVMAT00:
	case D3DTSS_BUMPENVMAT01:
	case D3DTSS_BUMPENVMAT10:
	case D3DTSS_BUMPENVMAT11:
	case D3DTSS_BUMPENVLSCALE:
	case D3DTSS_BUMPENVLOFFSET:
		name.Format("%f",std::bit_cast<float>(value));
		break;

	case D3DTSS_TEXCOORDINDEX:
		if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACENORMAL) {
			name.Format("D3DTSS_TCI_CAMERASPACENORMAL|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEPOSITION) {
			name.Format("D3DTSS_TCI_CAMERASPACEPOSITION|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) {
			name.Format("D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR|%d",value&0xffff);
		}
		else {
			name.Format("%d",value);
		}
		break;

	// Integer value
	// Ronin @build 27/10/2025 DX9: D3DTSS_MAXMIPLEVEL removed in DX9 - case commented out
	// 	case D3DTSS_MAXMIPLEVEL:
	// Ronin @build 27/10/2025 DX9: D3DTSS_MAXANISOTROPY removed in DX9 - case commented out
	// 	case D3DTSS_MAXANISOTROPY:
		name.Format("%d",value);
		break;
	// Hex values
	// Ronin @build 27/10/2025 DX9: D3DTSS_BORDERCOLOR removed in DX9 - case commented out
	// 	case D3DTSS_BORDERCOLOR:
		name.Format("0x%x",value);
		break;

	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Op_Name(unsigned value)
{
	switch (value) {
	case D3DTOP_DISABLE                      : return "D3DTOP_DISABLE";
	case D3DTOP_SELECTARG1                   : return "D3DTOP_SELECTARG1";
	case D3DTOP_SELECTARG2                   : return "D3DTOP_SELECTARG2";
	case D3DTOP_MODULATE                     : return "D3DTOP_MODULATE";
	case D3DTOP_MODULATE2X                   : return "D3DTOP_MODULATE2X";
	case D3DTOP_MODULATE4X                   : return "D3DTOP_MODULATE4X";
	case D3DTOP_ADD                          : return "D3DTOP_ADD";
	case D3DTOP_ADDSIGNED                    : return "D3DTOP_ADDSIGNED";
	case D3DTOP_ADDSIGNED2X                  : return "D3DTOP_ADDSIGNED2X";
	case D3DTOP_SUBTRACT                     : return "D3DTOP_SUBTRACT";
	case D3DTOP_ADDSMOOTH                    : return "D3DTOP_ADDSMOOTH";
	case D3DTOP_BLENDDIFFUSEALPHA            : return "D3DTOP_BLENDDIFFUSEALPHA";
	case D3DTOP_BLENDTEXTUREALPHA            : return "D3DTOP_BLENDTEXTUREALPHA";
	case D3DTOP_BLENDFACTORALPHA             : return "D3DTOP_BLENDFACTORALPHA";
	case D3DTOP_BLENDTEXTUREALPHAPM          : return "D3DTOP_BLENDTEXTUREALPHAPM";
	case D3DTOP_BLENDCURRENTALPHA            : return "D3DTOP_BLENDCURRENTALPHA";
	case D3DTOP_PREMODULATE                  : return "D3DTOP_PREMODULATE";
	case D3DTOP_MODULATEALPHA_ADDCOLOR       : return "D3DTOP_MODULATEALPHA_ADDCOLOR";
	case D3DTOP_MODULATECOLOR_ADDALPHA       : return "D3DTOP_MODULATECOLOR_ADDALPHA";
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR    : return "D3DTOP_MODULATEINVALPHA_ADDCOLOR";
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA    : return "D3DTOP_MODULATEINVCOLOR_ADDALPHA";
	case D3DTOP_BUMPENVMAP                   : return "D3DTOP_BUMPENVMAP";
	case D3DTOP_BUMPENVMAPLUMINANCE          : return "D3DTOP_BUMPENVMAPLUMINANCE";
	case D3DTOP_DOTPRODUCT3                  : return "D3DTOP_DOTPRODUCT3";
	case D3DTOP_MULTIPLYADD                  : return "D3DTOP_MULTIPLYADD";
	case D3DTOP_LERP                         : return "D3DTOP_LERP";
	default										     : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Arg_Name(unsigned value)
{
	switch (value) {
	case D3DTA_CURRENT			: return "D3DTA_CURRENT";
	case D3DTA_DIFFUSE			: return "D3DTA_DIFFUSE";
	case D3DTA_SELECTMASK		: return "D3DTA_SELECTMASK";
	case D3DTA_SPECULAR			: return "D3DTA_SPECULAR";
	case D3DTA_TEMP				: return "D3DTA_TEMP";
	case D3DTA_TEXTURE			: return "D3DTA_TEXTURE";
	case D3DTA_TFACTOR			: return "D3DTA_TFACTOR";
	case D3DTA_ALPHAREPLICATE	: return "D3DTA_ALPHAREPLICATE";
	case D3DTA_COMPLEMENT		: return "D3DTA_COMPLEMENT";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Filter_Name(unsigned value)
{
	switch (value) {
	case D3DTEXF_NONE				: return "D3DTEXF_NONE";
	case D3DTEXF_POINT			: return "D3DTEXF_POINT";
	case D3DTEXF_LINEAR			: return "D3DTEXF_LINEAR";
	case D3DTEXF_ANISOTROPIC	: return "D3DTEXF_ANISOTROPIC";
	//case D3DTEXF_FLATCUBIC		: return "D3DTEXF_FLATCUBIC"; // Removed in DX9
	//case D3DTEXF_GAUSSIANCUBIC	: return "D3DTEXF_GAUSSIANCUBIC"; // Removed in DX9
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Address_Name(unsigned value)
{
	switch (value) {
	case D3DTADDRESS_WRAP		: return "D3DTADDRESS_WRAP";
	case D3DTADDRESS_MIRROR		: return "D3DTADDRESS_MIRROR";
	case D3DTADDRESS_CLAMP		: return "D3DTADDRESS_CLAMP";
	case D3DTADDRESS_BORDER		: return "D3DTADDRESS_BORDER";
	case D3DTADDRESS_MIRRORONCE: return "D3DTADDRESS_MIRRORONCE";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Transform_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DTTFF_DISABLE			: return "D3DTTFF_DISABLE";
	case D3DTTFF_COUNT1			: return "D3DTTFF_COUNT1";
	case D3DTTFF_COUNT2			: return "D3DTTFF_COUNT2";
	case D3DTTFF_COUNT3			: return "D3DTTFF_COUNT3";
	case D3DTTFF_COUNT4			: return "D3DTTFF_COUNT4";
	case D3DTTFF_PROJECTED		: return "D3DTTFF_PROJECTED";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_ZBuffer_Type_Name(unsigned value)
{
	switch (value) {
	case D3DZB_FALSE				: return "D3DZB_FALSE";
	case D3DZB_TRUE				: return "D3DZB_TRUE";
	case D3DZB_USEW				: return "D3DZB_USEW";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fill_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFILL_POINT			: return "D3DFILL_POINT";
	case D3DFILL_WIREFRAME		: return "D3DFILL_WIREFRAME";
	case D3DFILL_SOLID			: return "D3DFILL_SOLID";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Shade_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DSHADE_FLAT			: return "D3DSHADE_FLAT";
	case D3DSHADE_GOURAUD		: return "D3DSHADE_GOURAUD";
	case D3DSHADE_PHONG			: return "D3DSHADE_PHONG";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Name(unsigned value)
{
	switch (value) {
	case D3DBLEND_ZERO                : return "D3DBLEND_ZERO";
	case D3DBLEND_ONE                 : return "D3DBLEND_ONE";
	case D3DBLEND_SRCCOLOR            : return "D3DBLEND_SRCCOLOR";
	case D3DBLEND_INVSRCCOLOR         : return "D3DBLEND_INVSRCCOLOR";
	case D3DBLEND_SRCALPHA            : return "D3DBLEND_SRCALPHA";
	case D3DBLEND_INVSRCALPHA         : return "D3DBLEND_INVSRCALPHA";
	case D3DBLEND_DESTALPHA           : return "D3DBLEND_DESTALPHA";
	case D3DBLEND_INVDESTALPHA        : return "D3DBLEND_INVDESTALPHA";
	case D3DBLEND_DESTCOLOR           : return "D3DBLEND_DESTCOLOR";
	case D3DBLEND_INVDESTCOLOR        : return "D3DBLEND_INVDESTCOLOR";
	case D3DBLEND_SRCALPHASAT         : return "D3DBLEND_SRCALPHASAT";
	case D3DBLEND_BOTHSRCALPHA        : return "D3DBLEND_BOTHSRCALPHA";
	case D3DBLEND_BOTHINVSRCALPHA     : return "D3DBLEND_BOTHINVSRCALPHA";
	default									 : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cull_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DCULL_NONE				: return "D3DCULL_NONE";
	case D3DCULL_CW				: return "D3DCULL_CW";
	case D3DCULL_CCW				: return "D3DCULL_CCW";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cmp_Func_Name(unsigned value)
{
	switch (value) {
	case D3DCMP_NEVER          : return "D3DCMP_NEVER";
	case D3DCMP_LESS           : return "D3DCMP_LESS";
	case D3DCMP_EQUAL          : return "D3DCMP_EQUAL";
	case D3DCMP_LESSEQUAL      : return "D3DCMP_LESSEQUAL";
	case D3DCMP_GREATER        : return "D3DCMP_GREATER";
	case D3DCMP_NOTEQUAL       : return "D3DCMP_NOTEQUAL";
	case D3DCMP_GREATEREQUAL   : return "D3DCMP_GREATEREQUAL";
	case D3DCMP_ALWAYS         : return "D3DCMP_ALWAYS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fog_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFOG_NONE				: return "D3DFOG_NONE";
	case D3DFOG_EXP				: return "D3DFOG_EXP";
	case D3DFOG_EXP2				: return "D3DFOG_EXP2";
	case D3DFOG_LINEAR			: return "D3DFOG_LINEAR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Stencil_Op_Name(unsigned value)
{
	switch (value) {
	case D3DSTENCILOP_KEEP		: return "D3DSTENCILOP_KEEP";
	case D3DSTENCILOP_ZERO		: return "D3DSTENCILOP_ZERO";
	case D3DSTENCILOP_REPLACE	: return "D3DSTENCILOP_REPLACE";
	case D3DSTENCILOP_INCRSAT	: return "D3DSTENCILOP_INCRSAT";
	case D3DSTENCILOP_DECRSAT	: return "D3DSTENCILOP_DECRSAT";
	case D3DSTENCILOP_INVERT	: return "D3DSTENCILOP_INVERT";
	case D3DSTENCILOP_INCR		: return "D3DSTENCILOP_INCR";
	case D3DSTENCILOP_DECR		: return "D3DSTENCILOP_DECR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Material_Source_Name(unsigned value)
{
	switch (value) {
	case D3DMCS_MATERIAL			: return "D3DMCS_MATERIAL";
	case D3DMCS_COLOR1			: return "D3DMCS_COLOR1";
	case D3DMCS_COLOR2			: return "D3DMCS_COLOR2";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Vertex_Blend_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DVBF_DISABLE			: return "D3DVBF_DISABLE";
	case D3DVBF_1WEIGHTS			: return "D3DVBF_1WEIGHTS";
	case D3DVBF_2WEIGHTS			: return "D3DVBF_2WEIGHTS";
	case D3DVBF_3WEIGHTS			: return "D3DVBF_3WEIGHTS";
	case D3DVBF_TWEENING			: return "D3DVBF_TWEENING";
	case D3DVBF_0WEIGHTS			: return "D3DVBF_0WEIGHTS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Patch_Edge_Style_Name(unsigned value)
{
	switch (value) {
	case D3DPATCHEDGE_DISCRETE	: return "D3DPATCHEDGE_DISCRETE";
   case D3DPATCHEDGE_CONTINUOUS:return "D3DPATCHEDGE_CONTINUOUS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Debug_Monitor_Token_Name(unsigned value)
{
	switch (value) {
	case D3DDMT_ENABLE			: return "D3DDMT_ENABLE";
	case D3DDMT_DISABLE			: return "D3DDMT_DISABLE";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Op_Name(unsigned value)
{
	switch (value) {
	case D3DBLENDOP_ADD			: return "D3DBLENDOP_ADD";
	case D3DBLENDOP_SUBTRACT	: return "D3DBLENDOP_SUBTRACT";
	case D3DBLENDOP_REVSUBTRACT: return "D3DBLENDOP_REVSUBTRACT";
	case D3DBLENDOP_MIN			: return "D3DBLENDOP_MIN";
	case D3DBLENDOP_MAX			: return "D3DBLENDOP_MAX";
	default							: return "UNKNOWN";
	}
}


//============================================================================
// DX8Wrapper::getBackBufferFormat
//============================================================================

WW3DFormat	DX8Wrapper::getBackBufferFormat()
{
	return D3DFormat_To_WW3DFormat( _PresentParameters.BackBufferFormat );
}

/**
 * Debug helper to validate current pipeline state
 */

#ifdef WWDEBUG
bool DX8Wrapper::Validate_Pipeline_State(const char* callerName)
{
	IDirect3DDevice9* pDev = _Get_D3D_Device8();
	if (!pDev) return false;

	// Keep early boot noise out (matches your old behavior)
	if (FrameCount < 45) {
		return true;
	}

	static int totalCalls = 0;
	totalCalls++;

	DWORD fvf = 0;
	IDirect3DVertexDeclaration9* decl = nullptr;
	pDev->GetFVF(&fvf);
	pDev->GetVertexDeclaration(&decl);

	const bool deviceDeclActive = (decl != nullptr);
	const bool deviceFVFActive = (fvf != 0);

	// What WRAPPER thinks
	const bool wrapperThinksDecl = (render_state.currentDecl != nullptr);
	const bool wrapperThinksFVF = (!wrapperThinksDecl && render_state.currentFVF != 0);

	const bool declIsEngineOwned = (deviceDeclActive && DeclCache) ? DeclCache->OwnsDecl(decl) : false;

	const bool suspiciousEngineDeclInFVFMode =
		(deviceDeclActive && wrapperThinksFVF && declIsEngineOwned);

	const bool wrapperDeviceDisagreeOnMode =
		(wrapperThinksDecl != deviceDeclActive);

	const bool bothReportedActive = (deviceDeclActive && deviceFVFActive);

	// =========================
	// NEW: per-callsite aggregation + edge-trigger logging
	// =========================
	enum IssueBits : unsigned
	{
		ISSUE_NONE = 0,
		ISSUE_ENGINE_DECL_IN_FVF_MODE = 1u << 0,
		ISSUE_WRAPPER_DEVICE_MODE_MISMATCH = 1u << 1
	};

	const unsigned issuesNow =
		(suspiciousEngineDeclInFVFMode ? ISSUE_ENGINE_DECL_IN_FVF_MODE : 0u) |
		(wrapperDeviceDisagreeOnMode ? ISSUE_WRAPPER_DEVICE_MODE_MISMATCH : 0u);

	struct Slot {
		const char* caller = nullptr;       // pointer-identity key (string literal expected)
		unsigned long lastFrame = 0;        // last frame we saw this slot updated
		unsigned total = 0;                 // total issue hits for this caller
		unsigned mismatchTotal = 0;         // total WRAPPER_DEVICE_LAYOUT_MODE_MISMATCH hits
		unsigned engineDeclTotal = 0;       // total ENGINE_DECL_PRESENT... hits
		unsigned lastIssues = ISSUE_NONE;   // previous issues bitmask (edge trigger)
	};

	static Slot s_slots[128] = {};
	static unsigned s_used = 0;

	Slot* slot = nullptr;
	for (unsigned i = 0; i < s_used; ++i) {
		if (s_slots[i].caller == callerName) { slot = &s_slots[i]; break; }
	}
	if (!slot) {
		if (s_used < 128) {
			s_slots[s_used].caller = callerName;
			slot = &s_slots[s_used++];
		}
		else {
			// Fallback: bucket 0 if table is full
			slot = &s_slots[0];
		}
	}

	// Reset per-frame "seen" bookkeeping if desired (currently not needed beyond lastFrame)
	slot->lastFrame = FrameCount;

	if (issuesNow != ISSUE_NONE) {
		++slot->total;
		if (issuesNow & ISSUE_WRAPPER_DEVICE_MODE_MISMATCH) ++slot->mismatchTotal;
		if (issuesNow & ISSUE_ENGINE_DECL_IN_FVF_MODE) ++slot->engineDeclTotal;
	}

	const unsigned entered = (issuesNow & ~slot->lastIssues); // edge-trigger per issue-bit
	slot->lastIssues = issuesNow;

	// Log policy:
	// - log on edge-trigger entering mismatch (per caller)
	// - plus a heartbeat every 256 hits for that caller (per issue type)
	const bool enteredModeMismatch = (entered & ISSUE_WRAPPER_DEVICE_MODE_MISMATCH) != 0u;
	const bool enteredEngineDecl = (entered & ISSUE_ENGINE_DECL_IN_FVF_MODE) != 0u;

	const bool heartbeatModeMismatch =
		(issuesNow & ISSUE_WRAPPER_DEVICE_MODE_MISMATCH) && ((slot->mismatchTotal % 256u) == 0u);
	const bool heartbeatEngineDecl =
		(issuesNow & ISSUE_ENGINE_DECL_IN_FVF_MODE) && ((slot->engineDeclTotal % 256u) == 0u);

	const bool shouldLog = enteredModeMismatch || enteredEngineDecl || heartbeatModeMismatch || heartbeatEngineDecl;

	if (shouldLog) {
		const int wrapperDeclSet = wrapperThinksDecl ? 1 : 0;
		const int deviceDeclSet = deviceDeclActive ? 1 : 0;

		const char* wrapperMode =
			wrapperThinksDecl ? "DECL" :
			(wrapperThinksFVF ? "FVF" : "NONE");

		const char* deviceMode =
			deviceDeclActive ? "DECL" :
			(deviceFVFActive ? "FVF" : "NONE");

		WWDEBUG_SAY(("🚨 [Frame %lu] Pipeline Issue (per-caller agg):", FrameCount));
		WWDEBUG_SAY(("   Caller: %s", callerName ? callerName : "Unknown"));
		WWDEBUG_SAY(("   Device:  FVF=0x%08X Decl=%p (deviceDeclSet=%d declOwned=%d bothActive=%d mode=%s)",
			(unsigned)fvf, decl, deviceDeclSet, declIsEngineOwned ? 1 : 0, bothReportedActive ? 1 : 0, deviceMode));
		WWDEBUG_SAY(("   Wrapper: currentFVF=0x%08X currentDecl=%p (wrapperDeclSet=%d mode=%s owner=%s)",
			(unsigned)render_state.currentFVF,
			render_state.currentDecl,
			wrapperDeclSet,
			wrapperMode,
			render_state.layoutOwner ? render_state.layoutOwner : "Unknown/null decl owner"));

		if (issuesNow & ISSUE_ENGINE_DECL_IN_FVF_MODE) {
			WWDEBUG_SAY(("   ⚠️ Type: ENGINE_DECL_PRESENT_WHILE_WRAPPER_IN_FVF_MODE (count=%u)", slot->engineDeclTotal));
		}
		if (issuesNow & ISSUE_WRAPPER_DEVICE_MODE_MISMATCH) {
			WWDEBUG_SAY(("   ⚠️ Type: WRAPPER_DEVICE_LAYOUT_MODE_MISMATCH (count=%u) (wrapperDeclSet=%d deviceDeclSet=%d wrapperMode=%s deviceMode=%s)",
				slot->mismatchTotal, wrapperDeclSet, deviceDeclSet, wrapperMode, deviceMode));
		}
	}
	// =========================
	// Update history while decl is still valid
	// =========================
	// Ronin @bugfix 27/02/2026 DX9: History tracking must run before releasing the queried decl,
	// otherwise g_stateHistory.lastDecl is always stored as null.
	g_stateHistory.lastFVF = fvf;

	// Release old history ref, store new one (transfers our queried ref to history)
	if (g_stateHistory.lastDecl) {
		g_stateHistory.lastDecl->Release();
	}
	g_stateHistory.lastDecl = decl;  // decl is still valid here; history takes ownership of the ref
	if (decl) {
		decl->AddRef();  // history holds its own ref
	}

	// Keep caller tracking meaningful: record based on *effective* mode
	if (deviceDeclActive) g_stateHistory.lastSetDeclCaller = callerName;
	else if (deviceFVFActive) g_stateHistory.lastSetFVFCaller = callerName;

	// Release the ref from GetVertexDeclaration (history now has its own AddRef'd copy)
	if (decl) {
		decl->Release();
		decl = nullptr;
	}

	return true;
}
#endif

void DX8Wrapper::Set_Vertex_Declaration(IDirect3DVertexDeclaration9* decl)
{
	DX8_THREAD_ASSERT();

#ifdef _DEBUG
	ASSERT_LAYOUT_BINDING_ALLOWED();
#endif

	IDirect3DDevice9* pDev = _Get_D3D_Device8();
	if (!pDev) return;

	// Ronin @bugfix 08/12/2025: When binding a non-null decl, clear FVF residue first
	if (decl != nullptr) {
		DWORD currentFVF = 0;
		pDev->GetFVF(&currentFVF);
		if (currentFVF != 0) {
			pDev->SetFVF(0);
			DX8_RECORD_DX8_CALLS();
#ifdef _DEBUG
			WWDEBUG_SAY(("Wrapper: cleared FVF=0x%08X before binding decl=%p", currentFVF, decl));
#endif
		}
	}

	HRESULT hr = pDev->SetVertexDeclaration(decl);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("SetVertexDeclaration(%p) failed: 0x%08X", decl, hr));
		return;
	}
	DX8_RECORD_DX8_CALLS();

	// Track state
	render_state.currentDecl = decl;
	render_state.currentFVF = 0;
	render_state_changed |= VERTEX_BUFFER_CHANGED;

	// @bugfix Ronin 23/01/2026 DX9: Publish expected layout for DIP-time ENSURE
	render_state.expectedFVF = 0;
	if (render_state.expectedDecl) {
		render_state.expectedDecl->Release();
		render_state.expectedDecl = nullptr;
	}
	render_state.expectedDecl = decl;
	if (render_state.expectedDecl) {
		render_state.expectedDecl->AddRef();
	}
}

void DX8Wrapper::BindLayoutFVF(DWORD fvf, const char* owner)
{
#ifdef _DEBUG
	ALLOW_LAYOUT_BINDING();
#endif

	// Ronin @refactor 10/03/2026 DX9: Remove dead duplicate FVF==0 branch.
	if (fvf == 0) {
		WWDEBUG_SAY(("BindLayoutFVF(%s): invalid FVF=0, ignoring", owner ? owner : "?"));
		return;
	}

	IDirect3DDevice9* pDev = _Get_D3D_Device8();
	if (!pDev) return;

	// Fixed-function input layout only:
	// - clear VS + decl
	// - set FVF
	// Pixel shader is intentionally preserved (river/trapezoid use PS with FVF pipeline).
	DX8CALL(SetVertexShader(NULL));
	DX8CALL(SetVertexDeclaration(NULL));

	MAYBE_UNUSED HRESULT hr = pDev->SetFVF(fvf);
	DX8_RECORD_DX8_CALLS();

#ifdef WWDEBUG
	if (FAILED(hr)) {
		WWDEBUG_SAY(("BindLayoutFVF(owner=%s): SetFVF(0x%08X) failed hr=0x%08X", owner ? owner : "?", fvf, hr));
	}
#endif

	render_state.currentFVF = fvf;
	render_state.currentDecl = nullptr;
	render_state.layoutOwner = owner;
}

void DX8Wrapper::BindLayoutDecl(IDirect3DVertexDeclaration9* decl, const char* owner) {

#ifdef _DEBUG
	ASSERT_LAYOUT_BINDING_ALLOWED_API("SetVertexDeclaration");
#endif

	// Idempotency
	if (render_state.currentDecl == decl && render_state.currentFVF == 0) return;

	IDirect3DDevice9* pDev = _Get_D3D_Device8();
	if (!pDev) {
		WWDEBUG_SAY(("BindLayoutDecl: No device available"));
		return;
	}

	// Bind declaration (DX9 ignores FVF when decl is active; GetFVF may remain non-zero)
	HRESULT hr = pDev->SetVertexDeclaration(decl);
	DX8_RECORD_DX8_CALLS();

	if (FAILED(hr)) {
		WWDEBUG_SAY(("BindLayoutDecl: SetVertexDeclaration(%p) failed: 0x%08X", decl, hr));
		return;
	}

	// Track
	render_state.currentDecl = decl;
	render_state.currentFVF = 0;
	render_state.layoutOwner = owner;

#ifdef _DEBUG
	DWORD deviceFVF = 0;
	IDirect3DVertexDeclaration9* deviceDecl = nullptr;
	pDev->GetFVF(&deviceFVF);
	pDev->GetVertexDeclaration(&deviceDecl);

	// In DX9, deviceFVF may be non-zero here; that’s fine (ignored under decl)
	if (deviceDecl != decl) {
		WWDEBUG_SAY(("BindLayoutDecl: Device decl=%p (expected %p)", deviceDecl, decl));
	}
	if (deviceDecl) deviceDecl->Release();
#endif
}


#ifdef _DEBUG

PipelineStateSnapshot* DX8Wrapper::Capture_Pipeline_State(const char* location) {
	IDirect3DDevice9* pDev = DX8Wrapper::_Get_D3D_Device8();  
	if (!pDev) return nullptr;

	PipelineStateSnapshot* snapshot = new PipelineStateSnapshot();
	snapshot->captureLocation = location;

	// Capture FVF and declaration
	pDev->GetFVF(&snapshot->fvf);
	pDev->GetVertexDeclaration(&snapshot->decl);

	// Capture all stream sources
	for (int i = 0; i < 4; i++) {
		pDev->GetStreamSource(i, &snapshot->streams[i].buffer,
			&snapshot->streams[i].offset,
			&snapshot->streams[i].stride);
	}

	// Capture index buffer
	pDev->GetIndices(&snapshot->indexBuffer);

	// Capture transforms - ✅ FIXED
	DX8Wrapper::Get_Transform(D3DTS_WORLD, snapshot->worldTransform);
	DX8Wrapper::Get_Transform(D3DTS_VIEW, snapshot->viewTransform);
	DX8Wrapper::Get_Transform(D3DTS_PROJECTION, snapshot->projectionTransform);

	// Capture viewport
	pDev->GetViewport(&snapshot->viewport);

	//@performance Ronin 21/01/2026 DX9: Capture is intentionally silent; logging is done only on validation failure.
  //WWDEBUG_SAY(("📸 [CAPTURED] Pipeline State at %s:", location));
	//WWDEBUG_SAY(("   FVF: 0x%08X, Decl: %p", snapshot->fvf, snapshot->decl));
	//WWDEBUG_SAY(("   Stream[0]: VB=%p, Offset=%u, Stride=%u",
	//snapshot->streams[0].buffer, snapshot->streams[0].offset, snapshot->streams[0].stride));
	//WWDEBUG_SAY(("   IB: %p", snapshot->indexBuffer));

	return snapshot;
}

// @bugfix Ronin 20/01/2026 Pipeline validation: make restored-state logging fail-only to reduce debug spam.
static bool Should_Log_Pipeline_Validation_Failure(const char* where, unsigned* outCount = nullptr)
{
	// Best-effort dedupe by pointer identity of string literals.
	struct Slot { const char* where; unsigned count; };
	static Slot s_slots[128] = {};
	static unsigned s_used = 0;

	for (unsigned i = 0; i < s_used; ++i) {
		if (s_slots[i].where == where) {
			s_slots[i].count++;
			if (outCount) *outCount = s_slots[i].count;
			// log first few, then every 128th
			return (s_slots[i].count <= 5) || ((s_slots[i].count % 128) == 0);
		}
	}

	if (s_used < 128) {
		s_slots[s_used++] = { where, 1 };
		if (outCount) *outCount = 1;
		return true;
	}

	static unsigned s_fallback = 0;
	s_fallback++;
	if (outCount) *outCount = s_fallback;
	return (s_fallback <= 5) || ((s_fallback % 128) == 0);
}

bool DX8Wrapper::Validate_Pipeline_State_Restored(PipelineStateSnapshot* snapshot, const char* location)
{
#ifdef _DEBUG
	if (!snapshot || !D3DDevice) {
		return true;
	}

	// Capture "after" but DO NOT print it here (print only on failure).
	PipelineStateSnapshot* after = Capture_Pipeline_State(location);
	if (!after) {
		return true;
	}

	// @bugfix Ronin 20/01/2026 DX9: PipelineStateSnapshot has no Matches(); do explicit comparison here.
	bool ok = true;

	if (snapshot->fvf != after->fvf) ok = false;
	if (snapshot->decl != after->decl) ok = false;
	if (snapshot->indexBuffer != after->indexBuffer) ok = false;

	// Compare first 4 streams (PipelineStateSnapshot::streams[4])
	for (int i = 0; i < 4 && ok; ++i) {
		if (snapshot->streams[i].buffer != after->streams[i].buffer) ok = false;
		if (snapshot->streams[i].offset != after->streams[i].offset) ok = false;
		if (snapshot->streams[i].stride != after->streams[i].stride) ok = false;
	}

	// Compare viewport (cheap and useful)
	if (ok) {
		const D3DVIEWPORT9& a = snapshot->viewport;
		const D3DVIEWPORT9& b = after->viewport;
		if (a.X != b.X || a.Y != b.Y || a.Width != b.Width || a.Height != b.Height ||
			a.MinZ != b.MinZ || a.MaxZ != b.MaxZ) {
			ok = false;
		}
	}

	// Compare transforms (Matrix4x4 should be trivially comparable by memory here)
	if (ok) {
		if (memcmp(&snapshot->worldTransform, &after->worldTransform, sizeof(Matrix4x4)) != 0) ok = false;
		if (memcmp(&snapshot->viewTransform, &after->viewTransform, sizeof(Matrix4x4)) != 0) ok = false;
		if (memcmp(&snapshot->projectionTransform, &after->projectionTransform, sizeof(Matrix4x4)) != 0) ok = false;
	}

	if (!ok) {
		unsigned count = 0;
		if (Should_Log_Pipeline_Validation_Failure(location, &count)) {
			WWDEBUG_SAY(("🚫 [VALIDATION FAILED] Pipeline State NOT Restored at %s (count=%u)", location, count));
			Log_Pipeline_State_Diff(snapshot, after);
		}
	}

	delete after;
	return ok;
#else
	(void)snapshot;
	(void)location;
	return true;
#endif
}

void DX8Wrapper::Log_Pipeline_State_Diff(const PipelineStateSnapshot* before, const PipelineStateSnapshot* after)
{
	if (!before || !after) {
		WWDEBUG_SAY(("Log_Pipeline_State_Diff: before=%p after=%p", before, after));
		return;
	}

	WWDEBUG_SAY(("=== PIPELINE STATE DIFF ==="));
	WWDEBUG_SAY(("  before: %s", before->captureLocation ? before->captureLocation : "(null)"));
	WWDEBUG_SAY(("  after : %s", after->captureLocation ? after->captureLocation : "(null)"));

	if (before->fvf != after->fvf) {
		WWDEBUG_SAY(("  FVF: 0x%08X -> 0x%08X", (unsigned)before->fvf, (unsigned)after->fvf));
	}
	if (before->decl != after->decl) {
		WWDEBUG_SAY(("  Decl: %p -> %p", before->decl, after->decl));
	}

	if (before->indexBuffer != after->indexBuffer) {
		WWDEBUG_SAY(("  IB: %p -> %p", before->indexBuffer, after->indexBuffer));
	}

	for (int i = 0; i < 4; ++i) {
		const auto& a = before->streams[i];
		const auto& b = after->streams[i];

		if (a.buffer != b.buffer || a.offset != b.offset || a.stride != b.stride) {
			WWDEBUG_SAY((
				"  Stream[%d]: VB=%p off=%u stride=%u  ->  VB=%p off=%u stride=%u",
				i,
				a.buffer, (unsigned)a.offset, (unsigned)a.stride,
				b.buffer, (unsigned)b.offset, (unsigned)b.stride));
		}
	}

	{
		const D3DVIEWPORT9& a = before->viewport;
		const D3DVIEWPORT9& b = after->viewport;
		if (a.X != b.X || a.Y != b.Y || a.Width != b.Width || a.Height != b.Height ||
			a.MinZ != b.MinZ || a.MaxZ != b.MaxZ) {
			WWDEBUG_SAY((
				"  Viewport: (%u,%u %ux%u z=%f..%f) -> (%u,%u %ux%u z=%f..%f)",
				(unsigned)a.X, (unsigned)a.Y, (unsigned)a.Width, (unsigned)a.Height, a.MinZ, a.MaxZ,
				(unsigned)b.X, (unsigned)b.Y, (unsigned)b.Width, (unsigned)b.Height, b.MinZ, b.MaxZ));
		}
	}

	if (memcmp(&before->worldTransform, &after->worldTransform, sizeof(Matrix4x4)) != 0) {
		WWDEBUG_SAY(("  World transform changed"));
	}
	if (memcmp(&before->viewTransform, &after->viewTransform, sizeof(Matrix4x4)) != 0) {
		WWDEBUG_SAY(("  View transform changed"));
	}
	if (memcmp(&before->projectionTransform, &after->projectionTransform, sizeof(Matrix4x4)) != 0) {
		WWDEBUG_SAY(("  Projection transform changed"));
	}
}

#endif // _DEBUG

void DX8Wrapper::Set_Anti_Aliasing_Level(int level)
{
	if (level <= 0)
		level = 0;
	else if (level <= 2)
		level = 2;
	else if (level <= 4)
		level = 4;
	else
		level = 8;

	AntiAliasingLevel = level;
}
