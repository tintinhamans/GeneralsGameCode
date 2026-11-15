/*
**	Command & Conquer Generals Zero Hour(tm)
**	DX9 shadow-map path — §29 phases 0-1.
*/

// Ronin @feature 12/08/2026 DX9: §29 phases 0-1. Probe first — the depth format decides whether
// we get hardware PCF or must filter an R32F map by hand.

#include "WW3D2/dx8todx9.h"
#include "WWLib/always.h"
#include "WWDebug/wwdebug.h"
#include "Lib/BaseType.h"
#include <math.h>
#include <string.h>
#include "d3dx9math.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/texture.h"
#include "WW3D2/statistics.h"
#include "WWMath/aabox.h"
#include "WWMath/frustum.h"
#include "WWMath/matrix3d.h"
#include "WW3D2/camera.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/scene.h"
#include "WW3D2/hlod.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "WW3D2/dx8instancing.h"
#include "W3DDevice/GameClient/R3DShadowMapState.h"
#include "W3DDevice/GameClient/R3DShadowMap.h"
// Ronin @perf 19/08/2026 DX9: §29j.7 static caster bake
#include <vector>
#include "WW3D2/rendobj.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshgeometry.h"
#include "WW3D2/meshmdl.h"		// MeshModelClass; meshgeometry.h only declares the base
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8fvf.h"
#include "WW3D2/shader.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "GameClient/Drawable.h"
#include "Common/KindOf.h"
#include "GameClient/GameClient.h"		// Ronin @bugfix 13/09/2026 DX9: §29j.7 — TheGameClient->findDrawableByID

Bool TheUseShadowMaps = TRUE;
Bool TheShowShadowMapDebug = FALSE;
// Set TRUE to run stencil volumes alongside the map — for comparison only; objects get shadowed twice.
Bool TheKeepShadowVolumesWithMaps = FALSE;


ShadowMapMode	R3DShadowMap::m_mode		= SHADOWMAP_UNAVAILABLE;
Int				R3DShadowMap::m_resolution	= 0;
// Ronin @feature 23/08/2026 DX9: §29i.2. Defaults are the SHIPPED values (High), so nothing moves
// until applyQuality runs.
Real			R3DShadowMap::m_maxShadowDistance = 1200.0f;
Int				R3DShadowMap::m_quality		= R3DShadowMap::SHADOWQ_HIGH;
Int				R3DShadowMap::m_splitCount	= 2;
TextureClass	*R3DShadowMap::m_colorTarget[R3DShadowMap::MAX_SHADOW_SPLITS] = { NULL };
ZTextureClass	*R3DShadowMap::m_depthTarget[R3DShadowMap::MAX_SHADOW_SPLITS] = { NULL };
IDirect3DTexture9	*R3DShadowMap::m_accumTex[2]  = { NULL, NULL };
IDirect3DSurface9	*R3DShadowMap::m_accumSurf[2] = { NULL, NULL };
IDirect3DTexture9	*R3DShadowMap::m_treeAccumTex[2]  = { NULL, NULL };
IDirect3DSurface9	*R3DShadowMap::m_treeAccumSurf[2] = { NULL, NULL };
IDirect3DTexture9	*R3DShadowMap::m_movingMaskTex[R3DShadowMap::MAX_SHADOW_SPLITS] = { NULL };
unsigned char		R3DShadowMap::m_movingMaskGrid[R3DShadowMap::MOVING_MASK_RES * R3DShadowMap::MOVING_MASK_RES] = { 0 };
Int					R3DShadowMap::m_movingMaskSplit = -1;
Int				R3DShadowMap::m_accumIndex	= 0;
Int				R3DShadowMap::m_accumW		= 0;
Int				R3DShadowMap::m_accumH		= 0;

// Ronin @feature 03/09/2026 DX9: §29j.13h. Screen-space accumulation pair.
void R3DShadowMap::releaseAccumTargets(void)
{
	for (int i = 0; i < 2; ++i)
	{
		if (m_accumSurf[i] != NULL) { m_accumSurf[i]->Release(); m_accumSurf[i] = NULL; }
		if (m_accumTex[i]  != NULL) { m_accumTex[i]->Release();  m_accumTex[i]  = NULL; }
		if (m_treeAccumSurf[i] != NULL) { m_treeAccumSurf[i]->Release(); m_treeAccumSurf[i] = NULL; }
		if (m_treeAccumTex[i]  != NULL) { m_treeAccumTex[i]->Release();  m_treeAccumTex[i]  = NULL; }
	}
	m_accumW = 0;
	m_accumH = 0;
}

// Made lazily and re-made on a resolution change or a device reset: these are raw D3D objects in
// POOL_DEFAULT, so nothing else recreates them. Failure is non-fatal — NULL means no accumulation.
void R3DShadowMap::ensureAccumTargets(void)
{
	int w = 0, h = 0, bits = 0;
	bool windowed = false;
	WW3D::Get_Render_Target_Resolution(w, h, bits, windowed);
	if (w <= 0 || h <= 0)
		return;

	// Ronin @bugfix 07/09/2026 DX9: §29j.13m. Latch a failed format probe — the early-out below needs
	// non-NULL textures, so without this CheckDeviceFormat ran every frame on hardware with no float RT.
	static Bool s_noFloatRT = FALSE;
	if (s_noFloatRT)
		return;

	if (m_accumTex[0] != NULL && m_accumTex[1] != NULL && m_accumW == w && m_accumH == h)
		return;

	releaseAccumTargets();

	IDirect3DDevice9 *accumDev = DX8Wrapper::_Get_D3D_Device8();
	if (accumDev == NULL)
		return;

	// Ronin @bugfix 04/09/2026 DX9: §29j.13h. FLOAT, never A8R8G8B8 — the EMA increment rounds to zero
	// at 8 bits and never converges. Four channels: the FFP composite leaves G and B undefined otherwise.
	// Ronin @perf 06/09/2026 DX9: §29j.13m. 16-bit first, no 8-bit fallback. Doc §9.12b.
	D3DFORMAT accumFmt = D3DFMT_UNKNOWN;
	{
		IDirect3D9 *d3d = NULL;
		if (SUCCEEDED(accumDev->GetDirect3D(&d3d)) && d3d != NULL)
		{
			D3DDEVICE_CREATION_PARAMETERS cp;
			D3DDISPLAYMODE dm;
			if (SUCCEEDED(accumDev->GetCreationParameters(&cp)) &&
				SUCCEEDED(accumDev->GetDisplayMode(0, &dm)))
			{
				if (SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, dm.Format,
													 D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
													 D3DFMT_A16B16G16R16F)))
					accumFmt = D3DFMT_A16B16G16R16F;
				else if (SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, dm.Format,
														  D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
														  D3DFMT_A32B32G32R32F)))
					accumFmt = D3DFMT_A32B32G32R32F;
			}
			d3d->Release();
		}
	}
	if (accumFmt == D3DFMT_UNKNOWN)
	{
		s_noFloatRT = TRUE;		// latch it; the early-out below cannot, textures stay NULL
		return;
	}

	for (int i = 0; i < 2; ++i)
	{
		if (FAILED(accumDev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, accumFmt,
										   D3DPOOL_DEFAULT, &m_accumTex[i], NULL)) ||
			FAILED(m_accumTex[i]->GetSurfaceLevel(0, &m_accumSurf[i])) ||
			FAILED(accumDev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, accumFmt,
										   D3DPOOL_DEFAULT, &m_treeAccumTex[i], NULL)) ||
			FAILED(m_treeAccumTex[i]->GetSurfaceLevel(0, &m_treeAccumSurf[i])))
		{
#ifdef DEBUG_LOGGING
			WWDEBUG_SAY(("[SHADOWACCUM] %dx%d fmt=%d creation FAILED — no temporal accumulation",
						 w, h, (int)accumFmt));
#endif
			releaseAccumTargets();
			return;
		}
	}

	m_accumW = w;
	m_accumH = h;
}


Matrix4x4		R3DShadowMap::m_lightViewProj[R3DShadowMap::MAX_SHADOW_SPLITS];
Real			R3DShadowMap::m_lastRadius[R3DShadowMap::MAX_SHADOW_SPLITS] = { 0.0f };
Real			R3DShadowMap::m_lastSunCot	= 2.0f;	// ~27 deg sun until updateLightMatrices runs
Real			R3DShadowMap::m_lastDepthHalf[R3DShadowMap::MAX_SHADOW_SPLITS] = { 0.0f };
Real			R3DShadowMap::m_lastRequired = 0.0f;	// §29j.13f — fit before the extent hold
Real			R3DShadowMap::m_lastBoxX	= 0.0f;
Real			R3DShadowMap::m_lastBoxY	= 0.0f;
Real			R3DShadowMap::m_lastBareBoxX = 0.0f;
Real			R3DShadowMap::m_lastBareBoxY = 0.0f;
Real			R3DShadowMap::m_lastExtentX = 0.0f;
Real			R3DShadowMap::m_lastExtentY = 0.0f;
Real			R3DShadowMap::m_frameMaxReceiverZ = -1.0e30f;
Real			R3DShadowMap::m_heldHeadroom = 400.0f;
// Ronin @perf 14/09/2026 DX9: §29i.3. Near map's own held headroom, unfloored. Starts high like the far one.
Real			R3DShadowMap::m_heldNearHeadroom = 400.0f;

CameraClass		*R3DShadowMap::m_lightCamera	= NULL;
IDirect3DVertexShader9	*R3DShadowMap::m_terrainShadowVS = NULL;
IDirect3DPixelShader9	*R3DShadowMap::m_terrainShadowPS = NULL;

// Ronin @feature 14/08/2026 §29 DX9: read a compiled shader blob. Mirrors
// DX8InstanceManagerClass::Load_Pixel_Shader_From_File; duplicated because that one is private and
// WW3D2 is a lower layer than this file.
static DWORD *loadShaderBlob(const char *path, DWORD *outSize)
{
	*outSize = 0;

	HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
#ifdef DEBUG_LOGGING
		WWDEBUG_SAY(("[SHADOWMAP] could not open %s (error %d)", path, GetLastError()));
#endif
		return NULL;
	}

	const DWORD fileSize = GetFileSize(hFile, NULL);
	if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize < sizeof(DWORD))
	{
		CloseHandle(hFile);
		return NULL;
	}

	DWORD *blob = (DWORD *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fileSize);
	if (blob == NULL)
	{
		CloseHandle(hFile);
		return NULL;
	}

	DWORD bytesRead = 0;
	const BOOL ok = ReadFile(hFile, blob, fileSize, &bytesRead, NULL);
	CloseHandle(hFile);

	if (!ok || bytesRead != fileSize)
	{
		HeapFree(GetProcessHeap(), 0, blob);
		return NULL;
	}

	*outSize = fileSize;
	return blob;
}

// ATI Fetch4-capable depth-texture formats. Not in the D3DFORMAT enum; they are FOURCC probes.
#define FOURCC_DF16 ((D3DFORMAT)MAKEFOURCC('D','F','1','6'))
#define FOURCC_DF24 ((D3DFORMAT)MAKEFOURCC('D','F','2','4'))

Bool R3DShadowMap::isAvailable(void)
{
	return m_mode != SHADOWMAP_UNAVAILABLE && m_colorTarget[0] != NULL;
}

TextureBaseClass *R3DShadowMap::peekShadowTexture(void)
{
	// On HW_DEPTH the depth texture IS the shadow map; the colour target is a bound dummy.
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. Split 0 is the NEAR map; the far one is published separately.
	return (m_mode == SHADOWMAP_HW_DEPTH) ? (TextureBaseClass *)m_depthTarget[0]
										  : (TextureBaseClass *)m_colorTarget[0];
}


//=============================================================================================
// Phase 0 — capability probe
//=============================================================================================

static Bool checkShadowFormat(D3DFORMAT adapterFormat, DWORD usage, D3DRESOURCETYPE type,
							  D3DFORMAT format, const char *label)
{
	const D3DCAPS9 &caps = DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps();
	const Bool ok = SUCCEEDED(DX8Wrapper::_Get_D3D8()->CheckDeviceFormat(
		caps.AdapterOrdinal, caps.DeviceType, adapterFormat, usage, type, format));
#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] %-30s : %s", label, ok ? "YES" : "no"));
#endif
	return ok;
}

ShadowMapMode R3DShadowMap::probeCapabilities(void)
{
	D3DDISPLAYMODE mode;
	const D3DCAPS9 &caps = DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps();

	if (FAILED(DX8Wrapper::_Get_D3D8()->GetAdapterDisplayMode(caps.AdapterOrdinal, &mode)))
	{
#ifdef DEBUG_LOGGING
		WWDEBUG_SAY(("[SHADOWMAP] GetAdapterDisplayMode FAILED — staying on stencil volumes"));
#endif
		m_mode = SHADOWMAP_UNAVAILABLE;
		return m_mode;
	}

#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] ---- capability probe (adapter fmt 0x%08X) ----", mode.Format));
#endif

	// NVIDIA-style hardware shadow maps: a DEPTH format usable as a texture, PCF free on sample.
	const Bool d16  = checkShadowFormat(mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
										D3DFMT_D16, "D16 as texture (HW PCF)");
	const Bool d24  = checkShadowFormat(mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
										D3DFMT_D24X8, "D24X8 as texture (HW PCF)");
	const Bool df16 = checkShadowFormat(mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
										FOURCC_DF16, "DF16 (ATI Fetch4)");
	const Bool df24 = checkShadowFormat(mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
										FOURCC_DF24, "DF24 (ATI Fetch4)");
	// Ronin @bugfix 24/08/2026 DX9: §29a. The probe asked about D16/D24X8 and then init() created D24S8 —
	// a format it had never tested. Ask about the format we ACTUALLY create, and require it.
	const Bool d24s8 = checkShadowFormat(mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
										 D3DFMT_D24S8, "D24S8 as texture (what init creates)");
	// Probed for the future INTZ/manual-compare path, NOT selectable — see below.
	const Bool r32f = checkShadowFormat(mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
										D3DFMT_R32F, "R32F render target");
	(void)d16; (void)d24; (void)df16; (void)df24; (void)r32f;	// logged for the vendor split, unused here

	// Not needed here — probed now so §26b.3 (WBOIT) is costed without a second build.
	checkShadowFormat(mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
					  D3DFMT_A16B16G16R16F, "A16B16G16R16F RT (WBOIT)");

#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] max texture %ux%u, PS version 0x%08X",
		caps.MaxTextureWidth, caps.MaxTextureHeight, caps.PixelShaderVersion));

	// Ronin @diagnostic 07/09/2026 DX9: §29j.13n. Decides the TREE receiver's shape before it is written.
	// MRT needs 2+ targets AND independent bit depths: the backbuffer is A8R8G8B8 while the accum buffer
	// must be float (§9.12). Without both, trees need a second pass instead.
	WWDEBUG_SAY(("[SHADOWMAP] MRT: NumSimultaneousRTs=%u independentBitDepths=%s postPSBlend=%s",
		caps.NumSimultaneousRTs,
		(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS) ? "YES" : "no",
		(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING) ? "YES" : "no"));
#endif

	// Ronin @bugfix 24/08/2026 DX9: §29a. D24S8 or nothing. SHADOWMAP_R32F is NOT selected: it is declared
	// but has no receiver — both shaders do a hardware tex2Dproj compare that a colour target cannot
	// service — so picking it rendered GARBAGE on exactly the cards that most needed a fallback. Falling
	// to UNAVAILABLE instead makes init() fail, applyQuality clear TheUseShadowMaps, and DoShadows run the
	// stencil volumes, which is the graceful degradation §29a asked for and which already works.
	m_mode = d24s8 ? SHADOWMAP_HW_DEPTH : SHADOWMAP_UNAVAILABLE;

#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] mode = %s",
		m_mode == SHADOWMAP_HW_DEPTH ? "HW_DEPTH" :
		m_mode == SHADOWMAP_R32F     ? "R32F"     : "UNAVAILABLE"));
#endif

	return m_mode;
}

//=============================================================================================
// Phase 1 — target, light matrices, depth-pass bind
//=============================================================================================

Bool R3DShadowMap::init(Int resolution)
{
	shutdown();

	if (m_mode == SHADOWMAP_UNAVAILABLE)
		return FALSE;

	// Ronin @feature 13/09/2026 DX9: §29i.3. A non-power-of-two size needs the card to allow one; otherwise fall to 2048.
	const DWORD texCaps = DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps().TextureCaps;
	const Bool  isPow2  = ((resolution & (resolution - 1)) == 0);
	if (!isPow2 && (texCaps & D3DPTEXTURECAPS_POW2) && !(texCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL))
		resolution = 2048;
	m_resolution = resolution;

	// Both targets go through TextureClass/ZTextureClass, so DX8TextureManagerClass recreates
	// them across device reset with the render-target flag intact (§27c.1, verified).
	// Ronin @bugfix 09/09/2026 DX9: §29i.3 step 2. ONE MATCHED PAIR PER SPLIT — a shared colour target
	// makes the second split's bind a silent no-op, so its depth texture is never written.
	// Ronin @feature 13/09/2026 DX9: §29i.3. Built here, NOT via Create_Render_Target — it rounds up to a power of two
	// (dx8wrapper.cpp:4294), so 3072 would become a 4096 texture while every lookup assumes 3072.
	for (Int i = 0; i < m_splitCount && i < MAX_SHADOW_SPLITS; ++i)
	{
		m_colorTarget[i] = NEW_REF(TextureClass, (resolution, resolution, WW3D_FORMAT_X8R8G8B8, MIP_LEVELS_1,
												 TextureClass::POOL_DEFAULT, true));
		if (m_colorTarget[i]->Peek_D3D_Base_Texture() == NULL)
			REF_PTR_RELEASE(m_colorTarget[i]);
		m_depthTarget[i] = NEW_REF(ZTextureClass, (resolution, resolution, WW3D_ZFORMAT_D24S8, MIP_LEVELS_1,
												  TextureClass::POOL_DEFAULT));
	}

	if (m_colorTarget[0] == NULL)
	{
#ifdef DEBUG_LOGGING
		WWDEBUG_SAY(("[SHADOWMAP] %dx%d target creation FAILED", resolution, resolution));
#endif
		m_mode = SHADOWMAP_UNAVAILABLE;
		return FALSE;
	}

	// Ronin @feature 13/09/2026 DX9: §29i.3. Moving-caster masks, one per split. Non-fatal on failure: the receiver then
	// samples NULL, which reads as black — no mask.
	{
		IDirect3DDevice9 *maskDev = DX8Wrapper::_Get_D3D_Device8();
		DWORD maskUsage = 0;	// Ronin @feature 08/11/2026 DX9Ex: a lockable default-pool texture on 9Ex
		const D3DPOOL maskPool = DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, &maskUsage);
		for (Int i = 0; i < m_splitCount && i < MAX_SHADOW_SPLITS && maskDev != NULL; ++i)
		{
			if (FAILED(maskDev->CreateTexture(MOVING_MASK_RES, MOVING_MASK_RES, 1, maskUsage, D3DFMT_A8R8G8B8,
											  maskPool, &m_movingMaskTex[i], NULL)))
				m_movingMaskTex[i] = NULL;
		}
	}
	// The light's ORTHO camera. Phase (b) will render the whole scene with this same camera.
	m_lightCamera = NEW_REF(CameraClass, ());
	m_lightCamera->Set_Projection_Type(CameraClass::ORTHO);

	for (Int i = 0; i < MAX_SHADOW_SPLITS; ++i)
		m_lightViewProj[i].Make_Identity();

	// Terrain receiver shaders. Non-fatal: without them terrain simply gets no shadows (§29h).
	{
		IDirect3DDevice9 *shDev = DX8Wrapper::_Get_D3D_Device8();
		DWORD blobSize = 0;
		DWORD *blob = loadShaderBlob("shaders\\TerrainShadow.vso", &blobSize);
		if (blob != NULL)
		{
			if (FAILED(shDev->CreateVertexShader(blob, &m_terrainShadowVS)))
				m_terrainShadowVS = NULL;
			HeapFree(GetProcessHeap(), 0, blob);
		}

		blob = loadShaderBlob("shaders\\TerrainShadow.pso", &blobSize);
		if (blob != NULL)
		{
			if (FAILED(shDev->CreatePixelShader(blob, &m_terrainShadowPS)))
				m_terrainShadowPS = NULL;
			HeapFree(GetProcessHeap(), 0, blob);
		}

#ifdef DEBUG_LOGGING
		WWDEBUG_SAY(("[SHADOWMAP] terrain receiver shaders: vs=%s ps=%s",
			m_terrainShadowVS ? "ok" : "MISSING", m_terrainShadowPS ? "ok" : "MISSING"));
#endif
	}

#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] created %dx%d", resolution, resolution));
#endif
	return TRUE;
}

void R3DShadowMap::shutdown(void)
{
	for (Int i = 0; i < MAX_SHADOW_SPLITS; ++i)
	{
		REF_PTR_RELEASE(m_colorTarget[i]);
		REF_PTR_RELEASE(m_depthTarget[i]);
		if (m_movingMaskTex[i] != NULL) { m_movingMaskTex[i]->Release(); m_movingMaskTex[i] = NULL; }
	}

	releaseAccumTargets();
	REF_PTR_RELEASE(m_lightCamera);


	if (m_terrainShadowVS != NULL) { m_terrainShadowVS->Release(); m_terrainShadowVS = NULL; }
	if (m_terrainShadowPS != NULL) { m_terrainShadowPS->Release(); m_terrainShadowPS = NULL; }

	for (Int i = 0; i < MAX_SHADOW_SPLITS; ++i)
		m_lastRadius[i] = 0.0f;
}

void R3DShadowMap::releaseResources(void)
{
	// Ronin @bugfix 08/10/2026 DX9: unpublish first, as applyQuality does. The receivers keep raw pointers; after a device
	// reset the water mirror (drawn before the next update) bound a freed depth texture and crashed in SetTexture.
	DX8InstanceManagerClass::Set_Shadow_Map(NULL, NULL, 0.0f, 0.0f, NULL, 0.0f);
	TheTerrainShadowPass.enabled   = FALSE;
	TheTerrainShadowPass.active    = FALSE;
	TheTerrainShadowPass.shadowTex = NULL;
	TheTerrainShadowPass.vs        = NULL;
	TheTerrainShadowPass.ps        = NULL;
	invalidateStaticCasters();
	shutdown();
}

void R3DShadowMap::reacquireResources(void)
{
	if (m_resolution > 0)
		init(m_resolution);
}

// Ronin @feature 23/08/2026 DX9: §29i.2 quality ladder.
// HAZARD: init() calls shutdown(), which releases the targets — so both receivers are pushed to NULL
// FIRST, then the target is rebuilt, and the bake is dirtied because its map is gone.
// RESOLUTION AND MAP COUNT are the levers, measured: PCF is not a tier. Doc §6.
Bool R3DShadowMap::applyQuality(void)
{
	Int level = (TheGlobalData != NULL) ? TheGlobalData->m_shadowMapQuality : (Int)SHADOWQ_HIGH;
	if (level < SHADOWQ_OFF)    level = SHADOWQ_OFF;
	if (level >= SHADOWQ_COUNT) level = SHADOWQ_COUNT - 1;

	// Ronin @feature 13/09/2026 DX9: §29i.3. Normal one 2048 (1024 measured the same cost), High two 2048, Ultra two 3072 —
	// two 4096 measured +9.4 ms. init drops 3072 to 2048 on a card that needs power-of-two sizes. Doc §8.8.
	static const Int  s_res[SHADOWQ_COUNT]    = {    0,    2048,    2048,    3072 };
	static const Int  s_splits[SHADOWQ_COUNT] = {    1,       1,       2,       2 };
	static const Real s_dist[SHADOWQ_COUNT]   = { 0.0f, 1200.0f, 1200.0f, 1200.0f };

	m_quality           = level;
	m_splitCount        = s_splits[level];
	m_maxShadowDistance = s_dist[level];
	TheUseShadowMaps    = (level != SHADOWQ_OFF);

	DX8InstanceManagerClass::Set_Shadow_Map(NULL, NULL, 0.0f, 0.0f, NULL, 0.0f);
	TheTerrainShadowPass.enabled   = FALSE;
	TheTerrainShadowPass.active    = FALSE;
	TheTerrainShadowPass.shadowTex = NULL;
	TheTerrainShadowPass.vs        = NULL;
	TheTerrainShadowPass.ps        = NULL;
	invalidateStaticCasters();

	if (level == SHADOWQ_OFF)
	{
		releaseResources();
		m_resolution = 0;		// keeps reacquireResources a no-op while the path is off
#ifdef DEBUG_LOGGING
		WWDEBUG_SAY(("[SHADOWMAP] quality = Off — stencil volumes and decals back in charge"));
#endif
		return FALSE;
	}

	const Bool ok = init(s_res[level]);
	// Ronin @bugfix 23/08/2026 DX9: the flag must not claim the path is live when the probe said
	// UNAVAILABLE or the target failed. DoShadows checks isAvailable() too, but a flag that lies is
	// how the next caller gets it wrong.
	TheUseShadowMaps = ok;
#ifdef DEBUG_LOGGING
	WWDEBUG_SAY(("[SHADOWMAP] quality = %d, resolution %d, max shadow distance %.0f, init %s",
				 level, s_res[level], m_maxShadowDistance, ok ? "ok" : "FAILED"));
#endif
	return ok;
}

// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. `split` selects the held-extent lattice; fitNear/fitFar
// are the ground band along the view ray. Split 0 over 0..maxShadowDistance is the fit that shipped.
// Ronin @bugfix 13/09/2026 DX9: §29i.3. screenFrac fits only the central fraction of the screen; 1 = the whole view.
void R3DShadowMap::updateLightMatrices(const FrustumClass &cameraFrustum, Int split, Real fitNear, Real fitFar, Real screenFrac)
{
	if (split < 0) split = 0;
	if (split >= MAX_SHADOW_SPLITS) split = MAX_SHADOW_SPLITS - 1;

	// Fit to the CAMERA FRUSTUM, not getMaximumVisibleBox — that is a map-sized particle-cull volume
	// whose centre sits ~170 units in the air. Corners 0-3 near, 4-7 far (frustum.h:65-72).
	// Project the corner rays onto the GROUND; a fraction of zfar is not a shadow range. Doc §9.2.
	Real groundZ = 0.0f;
	if (TheTerrainRenderObject != NULL)
	{
		const Vector3 camPos = cameraFrustum.CameraTransform.Get_Translation();
		groundZ = TheTerrainRenderObject->getHeightMapHeight(camPos.X, camPos.Y, NULL);
	}

	// Safety net for rays that point at or above the horizon and never meet the ground.
	// Ronin @feature 23/08/2026 DX9: §29i.2. Was a local const 1200.0f; now driven by the quality
	// level, because this is the only lever on depth-pass draw count (§29j.7).
	const Real MAX_SHADOW_DISTANCE = m_maxShadowDistance;

	// Ronin @feature 31/08/2026 DX9: §29i.3 step 1. The fit is a BAND along the view ray, which is
	// exactly what one cascade split is. Footprint corners reach 397..1196 from the eye (§29j.11).
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. The band comes from the caller now, one per split.
	// Clamped here so a bad band can never invert or outrun the quality tier's shadow distance.
	Real SHADOW_FIT_NEAR = (fitNear > 0.0f) ? fitNear : 0.0f;
	Real SHADOW_FIT_FAR  = (fitFar < MAX_SHADOW_DISTANCE) ? fitFar : MAX_SHADOW_DISTANCE;
	if (SHADOW_FIT_FAR <= SHADOW_FIT_NEAR)
		SHADOW_FIT_FAR = MAX_SHADOW_DISTANCE;

	// Ronin @bugfix 17/08/2026 DX9: §29h-7. Iterate the ground plane — project, sample terrain over the
	// footprint, drop to the LOWEST ground found, project again. One sample under the camera is wrong
	// whenever it sits over high ground looking at low. Converges, never undershoots. Doc §2.
	const int FIT_PASSES     = 3;
	const int HEIGHT_SAMPLES = 5;	// 25 lookups per pass, free next to the pass itself

	Vector3 pts[4];
	Real planeZ     = groundZ;
	Real minGroundZ = groundZ;
	Real maxGroundZ = groundZ;

	// Ronin @bugfix 13/09/2026 DX9: §29i.3. Pull the corner rays toward the screen CENTRE by screenFrac. Scaling the near
	// and far corners about their own plane centres keeps each ray on the same point of the screen.
	Real frac = (screenFrac > 0.05f) ? screenFrac : 0.05f;
	if (frac > 1.0f) frac = 1.0f;
	Vector3 nearCtr(0.0f, 0.0f, 0.0f);
	Vector3 farCtr(0.0f, 0.0f, 0.0f);
	for (int c = 0; c < 4; ++c)
	{
		nearCtr += cameraFrustum.Corners[c];
		farCtr  += cameraFrustum.Corners[c + 4];
	}
	nearCtr *= 0.25f;
	farCtr  *= 0.25f;

	for (int pass = 0; pass < FIT_PASSES; ++pass)
	{
		// GROUND POINTS ONLY. The near corners sit at the camera EYE; averaging them in put the centre
		// 263 units above terrain. A shadow map fits the visible GROUND — height is the clip planes'
		// job, not the XY fit's.
		for (int i = 0; i < 4; ++i)
		{
			const Vector3 nearC = nearCtr + (cameraFrustum.Corners[i] - nearCtr) * frac;
			const Vector3 ray   = (farCtr + (cameraFrustum.Corners[i + 4] - farCtr) * frac) - nearC;

			Real t = 1.0f;
			if (fabsf(ray.Z) > 1e-4f)
			{
				const Real tHit = (planeZ - nearC.Z) / ray.Z;
				if (tHit > 0.0f && tHit < t)
					t = tHit;
			}

			Vector3 hit   = nearC + ray * t;
			Vector3 delta = hit - nearC;
			const Real len = delta.Length();
			// Ronin @feature 31/08/2026 DX9: §29i.3 step 1. Clamp BOTH ends, not just the far one — a
			// cascade split is the ground band between two distances along the view ray, and the far
			// clamp alone always started the footprint at the eye. The len guard is new and required:
			// the near branch divides by len, which the old far-only branch could never reach with
			// len == 0. With SHADOW_FIT_NEAR = 0 the near branch is dead and this is bit-identical.
			if (len > 1e-4f)
			{
				if (len > SHADOW_FIT_FAR)
					hit = nearC + delta * (SHADOW_FIT_FAR / len);
				else if (len < SHADOW_FIT_NEAR)
					hit = nearC + delta * (SHADOW_FIT_NEAR / len);
			}

			// Force to the plane. The two UPPER rays are near-horizontal at this pitch and can end up
			// mid-air, which is what put the fitted centre 170 units up.
			hit.Z  = planeZ;
			pts[i] = hit;
		}

		if (TheTerrainRenderObject == NULL)
			break;

		// Sample the real terrain height range over this footprint. It is BOTH the receiver VOLUME
		// bound (§29h-7: a flat quad is a plane, not a volume) and the next pass's ground plane.
		Real minX = pts[0].X, maxX = pts[0].X, minY = pts[0].Y, maxY = pts[0].Y;
		for (int i = 1; i < 4; ++i)
		{
			if (pts[i].X < minX) minX = pts[i].X;
			if (pts[i].X > maxX) maxX = pts[i].X;
			if (pts[i].Y < minY) minY = pts[i].Y;
			if (pts[i].Y > maxY) maxY = pts[i].Y;
		}

		for (int iy = 0; iy < HEIGHT_SAMPLES; ++iy)
		{
			for (int ix = 0; ix < HEIGHT_SAMPLES; ++ix)
			{
				const Real fx = (Real)ix / (Real)(HEIGHT_SAMPLES - 1);
				const Real fy = (Real)iy / (Real)(HEIGHT_SAMPLES - 1);
				const Real h  = TheTerrainRenderObject->getHeightMapHeight(
									minX + (maxX - minX) * fx,
									minY + (maxY - minY) * fy, NULL);
				if (h < minGroundZ) minGroundZ = h;
				if (h > maxGroundZ) maxGroundZ = h;
			}
		}

		planeZ = minGroundZ;	// next pass reaches down to the lowest ground actually in view
	}

	// Head-room for what STANDS on the terrain — buildings and trees receive too.
	// Ronin @perf 06/09/2026 DX9: §29j.13k. Adaptive, was a flat 400. Bounded [200,400] so it can never
	// cover less than before. Grow at once, shrink 5%/frame. Input is one frame stale by construction.
	const Real HEADROOM_MIN = 200.0f;
	const Real HEADROOM_MAX = 400.0f;
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. SPLIT 0 ONLY. Headroom is a frame property, and
	// m_frameMaxReceiverZ is CONSUMED here — a later split would read it reset and collapse to MIN.
	if (split == 0)
	{
		Real needed = HEADROOM_MIN;
		if (m_frameMaxReceiverZ > -1.0e29f)
			needed = m_frameMaxReceiverZ - maxGroundZ;	// maxGroundZ is still bare terrain here
		// Ronin @perf 14/09/2026 DX9: §29i.3. NEAR map: the real height, no 200 floor — it predates the light-space caster cull, and
		// a receiver above the near volume still reads the far map. Nothing seen yet: needed is still the floor.
		Real nearNeeded = needed;
		if (nearNeeded < 0.0f)          nearNeeded = 0.0f;
		if (nearNeeded > HEADROOM_MAX)  nearNeeded = HEADROOM_MAX;
		if (nearNeeded > m_heldNearHeadroom)
			m_heldNearHeadroom = nearNeeded;
		else
			m_heldNearHeadroom += (nearNeeded - m_heldNearHeadroom) * 0.05f;
		if (needed < HEADROOM_MIN) needed = HEADROOM_MIN;
		if (needed > HEADROOM_MAX) needed = HEADROOM_MAX;
		if (needed > m_heldHeadroom)
			m_heldHeadroom = needed;
		else
			m_heldHeadroom += (needed - m_heldHeadroom) * 0.05f;
		m_frameMaxReceiverZ = -1.0e30f;					// consumed; re-accumulated next frame
	}
	const Real RECEIVER_HEADROOM = (split == 0 && m_splitCount > 1) ? m_heldNearHeadroom : m_heldHeadroom;
	maxGroundZ += RECEIVER_HEADROOM;

	// Ronin @bugfix 14/09/2026 DX9: §29h-7 sideways. A tall receiver at the screen's BOTTOM edge shows its roof over ground
	// below the screen, so the ground outline extruded upward misses it. Trace the corner rays to the volume's TOP as well.
	// Ronin @bugfix 14/09/2026 DX9: §29i.3. Same pulled-in rays as the ground fit. The near map fits ONLY the two outlines, the
	// slab its rays can see; the far map keeps the columns too.
	Vector3 topPts[4];
	const Bool nearHullOnly = (frac < 1.0f);
	for (int i = 0; i < 4; ++i)
	{
		const Vector3 nearC = nearCtr + (cameraFrustum.Corners[i] - nearCtr) * frac;
		const Vector3 ray   = (farCtr + (cameraFrustum.Corners[i + 4] - farCtr) * frac) - nearC;
		Real t = 1.0f;
		if (fabsf(ray.Z) > 1e-4f)
		{
			// Camera below the top plane, or a ray that never comes down to it: the near corner is the bound.
			const Real tHit = (maxGroundZ - nearC.Z) / ray.Z;
			t = (tHit < 0.0f) ? 0.0f : ((tHit < 1.0f) ? tHit : 1.0f);
		}
		Vector3 hit = nearC + ray * t;
		const Vector3 delta = hit - nearC;
		const Real len = delta.Length();
		if (len > SHADOW_FIT_FAR && len > 1e-4f)
			hit = nearC + delta * (SHADOW_FIT_FAR / len);
		hit.Z = maxGroundZ;
		topPts[i] = hit;
	}

	// Ronin @perf 23/08/2026 DX9: §29i.3/§29j.10. Light-space BOX, not a bounding sphere — the sphere
	// wrapped trapezoid AND height range in one radius, and that radius set the texel (measured r=576
	// vs box 278x253, 2.07x thrown away). Now XY sets the texel, Z sets the depth.
	// TRADE: rotation invariance — half-extents quantise to EXTENT_STEP, so yaw STEPS the texel size.

	// Sun direction FIRST — the fit is done in light space now, so the axes are needed before the extents.
	// W3DShadow.cpp:114-118 NEGATES m_terrainLightPos to place the source, so light TRAVELS along it.
	Vector3 lightDir(TheGlobalData->m_terrainLightPos[0].x,
					 TheGlobalData->m_terrainLightPos[0].y,
					 TheGlobalData->m_terrainLightPos[0].z);
	if (lightDir.Length2() < 1e-6f)
		lightDir.Set(0.0f, 0.0f, -1.0f);
	lightDir.Normalize();

	// Ronin @feature 17/08/2026 DX9: §29h-6. Publish cot(sun elevation) for getDepthBias(). Clamped both
	// ways: cot goes to infinity at the horizon, and during load the sun briefly reads straight down
	// (§29e), which would otherwise hand the bias a zero.
	{
		const Real sinElev = fabsf(lightDir.Z);			// lightDir is normalised
		Real c2 = 1.0f - sinElev * sinElev;
		if (c2 < 0.0f) c2 = 0.0f;
		Real cot = (sinElev > 1e-3f) ? (sqrtf(c2) / sinElev) : 4.0f;
		if (cot < 0.25f) cot = 0.25f;					// overhead sun still needs a floor
		if (cot > 4.0f)  cot = 4.0f;					// ~14 deg elevation; below that, clamp
		m_lastSunCot = cot;
	}

	if (m_lightCamera == NULL)
		return;

	// Orientation only, so we can fit and snap along the light's own axes.
	Matrix3D lightXform;
	lightXform.Look_At_Dir(Vector3(0.0f, 0.0f, 0.0f), lightDir, 0.0f);
	// Ronin @perf 14/09/2026 DX9: §29i.3. Every map rolls to the angle whose box is SMALLEST. Camera screen-right measured no gain
	// (exact 0.41 vs rot 0.33): the sun shears the fitted area in the light plane, so the angle is searched, not derived.
	Real lightRoll = 0.0f;
	{
		// The box's own point set at roll 0 — ground outline, column tops (far map only), top outline — as the corner loop below.
		const Vector3 baseX = lightXform.Get_X_Vector();
		const Vector3 baseY = lightXform.Get_Y_Vector();
		Real bpx[12], bpy[12];
		int  np = 0;
		for (int i = 0; i < 4; ++i)
		{
			const Vector3 g(pts[i].X, pts[i].Y, minGroundZ);
			bpx[np] = Vector3::Dot_Product(g, baseX);
			bpy[np] = Vector3::Dot_Product(g, baseY);
			++np;
			if (!nearHullOnly)
			{
				const Vector3 colTop(pts[i].X, pts[i].Y, maxGroundZ);
				bpx[np] = Vector3::Dot_Product(colTop, baseX);
				bpy[np] = Vector3::Dot_Product(colTop, baseY);
				++np;
			}
			bpx[np] = Vector3::Dot_Product(topPts[i], baseX);
			bpy[np] = Vector3::Dot_Product(topPts[i], baseY);
			++np;
		}
		const int  ROLL_STEPS = 72;						// 1.25 deg over a quarter turn — a square box repeats every 90
		const Real ROLL_STEP  = 1.5707963f / (Real)ROLL_STEPS;
		static int s_heldRollStep[MAX_SHADOW_SPLITS] = { 0 };
		Real bestSize = 1e30f;
		Real heldSize = 1e30f;
		int  bestStep = 0;
		for (int a = 0; a < ROLL_STEPS; ++a)
		{
			const Real ca = cosf((Real)a * ROLL_STEP);
			const Real sa = sinf((Real)a * ROLL_STEP);
			Real loA = 1e30f, hiA = -1e30f, loB = 1e30f, hiB = -1e30f;
			for (int k = 0; k < np; ++k)
			{
				const Real va =  ca * bpx[k] + sa * bpy[k];
				const Real vb = -sa * bpx[k] + ca * bpy[k];
				if (va < loA) loA = va;
				if (va > hiA) hiA = va;
				if (vb < loB) loB = vb;
				if (vb > hiB) hiB = vb;
			}
			const Real size = ((hiA - loA) > (hiB - loB)) ? (hiA - loA) : (hiB - loB);
			if (size < bestSize)
			{
				bestSize = size;
				bestStep = a;
			}
			if (a == s_heldRollStep[split])
				heldSize = size;
		}
		// Hold the angle unless another is 2% smaller — a new roll re-registers the whole map, so it must not flip between near-equal minima.
		if (bestSize < heldSize * 0.98f)
			s_heldRollStep[split] = bestStep;
		// The search's X is cos(a)*X + sin(a)*Y; Look_At_Dir's is cos(roll)*X - sin(roll)*Y (matrix3d.cpp:406, matrix3d.h:901).
		lightRoll = -(Real)s_heldRollStep[split] * ROLL_STEP;
		lightXform.Look_At_Dir(Vector3(0.0f, 0.0f, 0.0f), lightDir, lightRoll);
	}
	const Vector3 lightX = lightXform.Get_X_Vector();
	const Vector3 lightY = lightXform.Get_Y_Vector();
	const Vector3 lightZ = lightXform.Get_Z_Vector();

	// The eight corners of the ground volume, in light space.
	// Ronin @diagnostic 06/09/2026 DX9: §29j.13k. Third Z per corner fits a parallel "bare" box (headroom
	// removed) in the same loop, so the readout can price the headroom per axis.
	const Real bareMaxZ = maxGroundZ - RECEIVER_HEADROOM;
	Real loX = 1e30f, hiX = -1e30f, loY = 1e30f, hiY = -1e30f, loZ = 1e30f, hiZ = -1e30f;
	Real bLoX = 1e30f, bHiX = -1e30f, bLoY = 1e30f, bHiY = -1e30f;
	for (int i = 0; i < 4; ++i)
	{
		const Vector3 corner[3] = { Vector3(pts[i].X, pts[i].Y, minGroundZ),
									Vector3(pts[i].X, pts[i].Y, maxGroundZ),
									Vector3(pts[i].X, pts[i].Y, bareMaxZ) };
		for (int c = 0; c < 3; ++c)
		{
			const Real px = Vector3::Dot_Product(corner[c], lightX);
			const Real py = Vector3::Dot_Product(corner[c], lightY);
			// Ronin @bugfix 14/09/2026 DX9: §29i.3. Corners 0 and 1 are the REAL box; the near map drops 1, the column top.
			if (c == 0 || (c == 1 && !nearHullOnly))
			{
				const Real pz = Vector3::Dot_Product(corner[c], lightZ);
				if (px < loX) loX = px;
				if (px > hiX) hiX = px;
				if (py < loY) loY = py;
				if (py > hiY) hiY = py;
				if (pz < loZ) loZ = pz;
				if (pz > hiZ) hiZ = pz;
			}
			if (c != 1)			// corners 0 and 2 are the same box WITHOUT the headroom
			{
				if (px < bLoX) bLoX = px;
				if (px > bHiX) bHiX = px;
				if (py < bLoY) bLoY = py;
				if (py > bHiY) bHiY = py;
			}
		}
	}

	// Ronin @bugfix 14/09/2026 DX9: §29h-7 sideways. The top outline's corners — what covers a tall receiver at the bottom edge.
	for (int i = 0; i < 4; ++i)
	{
		const Real px = Vector3::Dot_Product(topPts[i], lightX);
		const Real py = Vector3::Dot_Product(topPts[i], lightY);
		const Real pz = Vector3::Dot_Product(topPts[i], lightZ);
		if (px < loX) loX = px;
		if (px > hiX) hiX = px;
		if (py < loY) loY = py;
		if (py > hiY) hiY = py;
		if (pz < loZ) loZ = pz;
		if (pz > hiZ) hiZ = pz;
	}

	// Quantise, or zooming re-scales the map every frame and the shadows swim again.
	// Ronin @perf 06/09/2026 DX9: §29j.13k. Both axes quantised and held independently.

	const Real EXTENT_STEP = 16.0f;
	// Ronin @bugfix 08/09/2026 DX9: §29j.13p. Lateral margin for casters at the footprint edge — the
	// power-of-two round-up was supplying this by accident. Doc §8.6.
	// Ronin @perf 14/09/2026 DX9: §29i.3. NEAR map: none. Under an ortho light a shadow inside the box comes from geometry inside
	// it, and the seam band already sends edge pixels to the far map. 16 was 5% of the near texel.
	// Ronin @perf 14/09/2026 DX9: §29i.3. FAR / single map 48 -> 16: the top outline now covers the tall receivers 48 was hiding.
	const Real CASTER_MARGIN = (split == 0 && m_splitCount > 1) ? 0.0f : 16.0f;
	const Real halfX = ceilf(((hiX - loX) * 0.5f + CASTER_MARGIN) / EXTENT_STEP) * EXTENT_STEP;
	const Real halfY = ceilf(((hiY - loY) * 0.5f + CASTER_MARGIN) / EXTENT_STEP) * EXTENT_STEP;
	const Real required = (halfX > halfY) ? halfX : halfY;


	if (required <= 0.0f)
		return;

	// Ronin @perf 06/09/2026 DX9: §29j.13k. One held value per AXIS.
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. One PAIR PER SPLIT — sharing one pair makes the near
	// split hold the far split's extent, which is the entire gain thrown away.
	static Real s_heldExtentX[MAX_SHADOW_SPLITS] = { 0.0f };
	static Real s_heldExtentY[MAX_SHADOW_SPLITS] = { 0.0f };
	// Ronin @perf 14/09/2026 DX9: §29i.3. EXACT size, replaces §29j.13f's power-of-two step (near measured 0.50 -> 0.36, no popping).
	// Grow at once with 4% slack, shrink only below 85%, so a pan never re-registers the map. Every map, far included.
	{
		const Real GROW_SLACK   = 1.04f;
		const Real SHRINK_BELOW = 0.85f;
		if (halfX > s_heldExtentX[split] || halfX < s_heldExtentX[split] * SHRINK_BELOW)
			s_heldExtentX[split] = ceilf(halfX * GROW_SLACK / EXTENT_STEP) * EXTENT_STEP;
		if (halfY > s_heldExtentY[split] || halfY < s_heldExtentY[split] * SHRINK_BELOW)
			s_heldExtentY[split] = ceilf(halfY * GROW_SLACK / EXTENT_STEP) * EXTENT_STEP;
	}
	const Real extentX = s_heldExtentX[split];
	const Real extentY = s_heldExtentY[split];

	m_lastExtentX = extentX;
	m_lastExtentY = extentY;

	// Ronin @perf 06/09/2026 DX9: §29j.13k. max(X,Y) is the scalar every single-value consumer (bias, texel readout) takes.
	const Real extent = (extentX > extentY) ? extentX : extentY;
	m_lastRadius[split] = extent;

	// Ronin @diagnostic 06/09/2026 DX9: §29j.13k. Fit numbers for the [SHADOW] readout.
	m_lastRequired = required;
	m_lastBoxX     = (hiX - loX) * 0.5f;
	m_lastBoxY     = (hiY - loY) * 0.5f;
	m_lastBareBoxX = (bHiX - bLoX) * 0.5f;
	m_lastBareBoxY = (bHiY - bLoY) * 0.5f;

	// Depth only has to clear the fitted volume plus any caster standing above it along the light. This is
	// now INDEPENDENT of texel size — the whole point — so it can be generous for free.
	const Real depthHalf = ((hiZ - loZ) * 0.5f) + extent * 2.0f;
	m_lastDepthHalf[split] = depthHalf;

	// Box centre in light space. Z is NOT snapped: only the two axes carrying texels matter.
	Real cx = (loX + hiX) * 0.5f;
	Real cy = (loY + hiY) * 0.5f;
	const Real cz = (loZ + hiZ) * 0.5f;

	// TEXEL SNAP: quantise the centre along the light's X/Y to whole shadow texels. Without this the texel
	// grid slides under the geometry and every shadow edge crawls as the camera moves.
	// Ronin @perf 06/09/2026 DX9: §29j.13k. One texel size PER AXIS. Each axis is independently
	// power-of-two, so each axis's lattice still nests across a change and §29j.13f's rule survives whole.
	const Real texelX = (extentX * 2.0f) / (Real)m_resolution;
	const Real texelY = (extentY * 2.0f) / (Real)m_resolution;

	// Ronin @feature 07/09/2026 DX9: §29j.13o. LATTICE JITTER. The quantisation is in the MAP, not the
	// lookup: a canopy is rasterised as a texel-resolution coverage mask, so sub-texel sway changes
	// nothing until a texel flips. Moving the grid's phase each frame makes it rasterise differently and
	// the EMA integrates the coverage. §29j.13g measured this working ("it does break the hold-and-jump")
	// and rejected it for shimmering — with no temporal filter to resolve it. Gated on the accumulation
	// existing, because without the resolve it IS just shimmer.
	Real jitterX = 0.0f, jitterY = 0.0f;
	if (m_accumTex[0] != NULL)
	{
		static UnsignedInt s_jitterFrame = 0;
		++s_jitterFrame;
		// OFF until RIGID receivers have accumulation. Measured 07/09/2026: terrain resolves this to
		// barely-visible at 0.125 (EMA attenuates ~5.7x, as predicted), but rigid has no EMA and sees it
		// raw — trading stepping for shimmer on units. Set to 0.5 the day rigid accumulates. §29j.13o.
		const Real JITTER_TEXELS = 0.0f;
		const Real jf = (Real)s_jitterFrame;
		jitterX = ((jf * 0.6180340f) - floorf(jf * 0.6180340f) - 0.5f) * JITTER_TEXELS;
		jitterY = ((jf * 0.7548776f) - floorf(jf * 0.7548776f) - 0.5f) * JITTER_TEXELS;
	}
	cx = (floorf(cx / texelX) + jitterX) * texelX;
	cy = (floorf(cy / texelY) + jitterY) * texelY;
	const Vector3 snappedCenter = lightX * cx + lightY * cy + lightZ * cz;


	// Pull back far enough that the entire depth range sits in front of the near plane.
	// Pull back far enough that the entire depth range sits in front of the near plane.
	const Real pullBack = depthHalf + 1.0f;
	const Vector3 eye = snappedCenter - lightDir * pullBack;
	// Ronin @perf 14/09/2026 DX9: §29i.3. Same roll as the fit, or the map renders on axes the box was not measured on.
	lightXform.Look_At_Dir(eye, lightDir, lightRoll);


	m_lightCamera->Set_Transform(lightXform);
	m_lightCamera->Set_Projection_Type(CameraClass::ORTHO);
	// Ronin @perf 06/09/2026 DX9: §29j.13k. PER-AXIS. Set_View_Plane always took separate X and Y — it
	// was being handed the same number twice. Measured aspect 1.69:1, so X was paying 2.80x zoomed in.
	m_lightCamera->Set_View_Plane(Vector2(-extentX, -extentY), Vector2(extentX, extentY));
	m_lightCamera->Set_Clip_Planes(0.1f, pullBack + depthHalf * 2.0f);


	// Ronin @feature 15/08/2026 DX9: §29i.2. Publish the fitted footprint. Snapped centre, so anything
	// drawing over it rides the same texel grid the map does and cannot crawl relative to it.
	TheTerrainShadowPass.fitCentre[0] = snappedCenter.X;
	TheTerrainShadowPass.fitCentre[1] = snappedCenter.Y;
	TheTerrainShadowPass.fitCentre[2] = snappedCenter.Z;
	// Ronin @feature 13/09/2026 DX9: §29i.3 step 2. The same axes cx/cy were measured in, so a cull can test the
	// box in light space instead of treating a light-space extent as a world-space width.
	TheTerrainShadowPass.lightAxisX[0] = lightX.X;
	TheTerrainShadowPass.lightAxisX[1] = lightX.Y;
	TheTerrainShadowPass.lightAxisX[2] = lightX.Z;
	TheTerrainShadowPass.lightAxisY[0] = lightY.X;
	TheTerrainShadowPass.lightAxisY[1] = lightY.Y;
	TheTerrainShadowPass.lightAxisY[2] = lightY.Z;
	TheTerrainShadowPass.fitExtentX    = extentX;
	TheTerrainShadowPass.fitExtentY    = extentY;

	// Cache light view-projection for the RECEIVER (phase 2). D3D flavour: z maps to [0,1].
	Matrix4x4 proj;
	m_lightCamera->Get_D3D_Projection_Matrix(&proj);
	Matrix4x4::Multiply(proj, m_lightCamera->Get_View_Matrix(), &m_lightViewProj[split]);
}


//=============================================================================================
// §29j.8 — static caster geometry cache, PER MESH
//=============================================================================================

// Ronin @perf 20/08/2026 DX9: §29j.8. Static meshes baked once into world-space position-only buffers,
// drawn in a few calls. World space => camera independent, so panning never invalidates it. PER MESH,
// not per object: a building's bulk bakes while its flag or turret keeps drawing normally.
#define CASTER_MAX_CHUNKS		32
#define CASTER_CHUNK_VERTS		60000
#define CASTER_CHUNK_IDX		60000		// index count is unsigned short too
#define CASTER_MAX_BAKED		8192		// meshes, not objects
#define CASTER_VERIFY_PER_FRAME	256
#define CASTER_POS_EPSILON		0.01f		// world units
#define CASTER_AXIS_EPSILON		0.001f

struct StaticCasterChunk
{
	DX8VertexBufferClass	*vb;
	DX8IndexBufferClass		*ib;
	int						vertCount;
	int						polyCount;
};

static StaticCasterChunk			s_casterChunk[CASTER_MAX_CHUNKS];
static int							s_casterChunkCount = 0;
static Bool							s_casterCacheDirty = TRUE;
static std::vector<Vector3>			s_bakeVerts;		// scratch for the chunk being built
static std::vector<unsigned short>	s_bakeIndices;

// Baked mesh records for the motion check. A reference is held, so the pointer cannot dangle.
static MeshClass	*s_bakedMesh[CASTER_MAX_BAKED];
static Vector3		s_bakedPos[CASTER_MAX_BAKED];
static Vector3		s_bakedAxis[CASTER_MAX_BAKED];
static int			s_bakedCount = 0;
static int			s_verifyCursor = 0;
// Ronin @bugfix 13/09/2026 DX9: §29j.7. IDs, not pointers, of baked drawables that have a game object — the ones fog, stealth
// or scripts can hide. Checked every frame by ensureStaticCasters.
static std::vector<DrawableID>	s_bakedDrawables;

static void releaseCasterChunks(void)
{
	for (int i = 0; i < s_casterChunkCount; ++i)
	{
		REF_PTR_RELEASE(s_casterChunk[i].vb);
		REF_PTR_RELEASE(s_casterChunk[i].ib);
	}
	s_casterChunkCount = 0;

	for (int i = 0; i < s_bakedCount; ++i)
	{
		s_bakedMesh[i]->Set_Baked_Shadow_Caster(false);
		REF_PTR_RELEASE(s_bakedMesh[i]);
	}
	s_bakedCount = 0;
	s_verifyCursor = 0;
	s_bakedDrawables.clear();
}

// Commit the scratch arrays as one static VB/IB pair.
static void flushCasterChunk(void)
{
	if (!s_bakeIndices.empty() && s_casterChunkCount < CASTER_MAX_CHUNKS)
	{
		StaticCasterChunk &c = s_casterChunk[s_casterChunkCount];
		c.vertCount = (int)s_bakeVerts.size();
		c.polyCount = (int)(s_bakeIndices.size() / 3);
		c.vb = NEW_REF(DX8VertexBufferClass,
					   (DX8_FVF_XYZ, (unsigned short)c.vertCount, DX8VertexBufferClass::USAGE_DEFAULT));
		c.ib = NEW_REF(DX8IndexBufferClass,
					   ((unsigned short)(c.polyCount * 3), DX8IndexBufferClass::USAGE_DEFAULT));
		{
			DX8VertexBufferClass::WriteLockClass vl(c.vb);
			memcpy(vl.Get_Vertex_Array(), &s_bakeVerts[0], c.vertCount * sizeof(Vector3));
		}
		{
			DX8IndexBufferClass::WriteLockClass il(c.ib);
			memcpy(il.Get_Index_Array(), &s_bakeIndices[0],
				   s_bakeIndices.size() * sizeof(unsigned short));
		}
		++s_casterChunkCount;
	}
	s_bakeVerts.clear();
	s_bakeIndices.clear();
}

// Ronin @bugfix 23/08/2026 DX9: §29j.8. A baked chunk is POSITION ONLY (DX8_FVF_XYZ) and is drawn
// untextured through _PresetOpaqueShader, so an alpha-tested or blended mesh loses its holes and
// casts its full quad — a chain-link fence shadows as a solid wall. There is no UV in the chunk to
// fix it with, so these meshes stay OFF the bake and keep their own draw in the depth pass, where
// Set_Shader applies ALPHATESTENABLE and the texture is bound (alpha test still discards with
// COLORWRITEENABLE=0). Costs one draw per such mesh; a fence is not worth a wrong silhouette.
static Bool casterSilhouetteNeedsAlpha(MeshModelClass *model)
{
	const int passes = model->Get_Pass_Count();
	const int pcount = model->Get_Polygon_Count();
	for (int pass = 0; pass < passes; ++pass)
	{
		for (int p = 0; p < pcount; ++p)
		{
			const ShaderClass s = model->Get_Shader(p, pass);
			if (s.Get_Alpha_Test() != ShaderClass::ALPHATEST_DISABLE ||
				s.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO)
				return TRUE;
		}
	}
	return FALSE;
}

// One mesh, transformed to world space. Splits a chunk before either 16-bit limit overflows.
static void bakeCasterMesh(MeshClass *mesh)
{
	if (s_bakedCount >= CASTER_MAX_BAKED)
		return;

	MeshModelClass *model = mesh->Peek_Model();
	if (model == NULL)
		return;

	// Ronin @bugfix 15/11/2026 DX9: a skin (the cloth flags) moves through its bones while its transform stays put, so the
	// sweep never saw it move and it was baked from bone-space vertices: never baked - drawn every frame, as a mover.
	if (model->Get_Flag(MeshGeometryClass::SKIN))
	{
		mesh->Set_Shadow_Caster_Mover(true);
		return;
	}

	const int vcount = model->Get_Vertex_Count();
	const int pcount = model->Get_Polygon_Count();
	// Ronin @bugfix 30/10/2026 DX9: the index count too - a 24k-triangle mesh wrapped the chunk's 16-bit index count and
	// overran its buffer (heap corruption). A mesh that big keeps its own draw in the depth pass.
	if (vcount <= 0 || pcount <= 0 || vcount > CASTER_CHUNK_VERTS || pcount * 3 > CASTER_CHUNK_IDX)
		return;

	if (casterSilhouetteNeedsAlpha(model))
		return;

	if ((int)s_bakeVerts.size() + vcount > CASTER_CHUNK_VERTS ||
		(int)s_bakeIndices.size() + pcount * 3 > CASTER_CHUNK_IDX)
		flushCasterChunk();

	const unsigned short base = (unsigned short)s_bakeVerts.size();
	const Matrix3D &xform = mesh->Get_Transform();
	const Vector3 *src = model->Get_Vertex_Array();

	for (int i = 0; i < vcount; ++i)
	{
		Vector3 world;
		Matrix3D::Transform_Vector(xform, src[i], &world);
		s_bakeVerts.push_back(world);
	}

	const TriIndex *poly = model->Get_Polygon_Array();
	for (int p = 0; p < pcount; ++p)
	{
		s_bakeIndices.push_back((unsigned short)(base + poly[p].I));
		s_bakeIndices.push_back((unsigned short)(base + poly[p].J));
		s_bakeIndices.push_back((unsigned short)(base + poly[p].K));
	}

	mesh->Add_Ref();
	s_bakedMesh[s_bakedCount] = mesh;
	s_bakedPos[s_bakedCount]  = xform.Get_Translation();
	s_bakedAxis[s_bakedCount] = xform.Get_X_Vector();
	++s_bakedCount;
	mesh->Set_Baked_Shadow_Caster(true);		// MeshClass::Render now skips it in the depth pass
}

// Recurse a caster. Get_Sub_Object add-refs, so release.
static void bakeCasterObject(RenderObjClass *robj)
{
	if (robj == NULL || robj->Is_Not_Hidden_At_All() == false)
		return;

	if (robj->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh = (MeshClass *)robj;
		if (!mesh->Is_Shadow_Caster_Mover())	// proven mover: stays on the normal path
			bakeCasterMesh(mesh);
	}

	const int n = robj->Get_Num_Sub_Objects();
	for (int i = 0; i < n; ++i)
	{
		RenderObjClass *sub = robj->Get_Sub_Object(i);
		bakeCasterObject(sub);
		REF_PTR_RELEASE(sub);
	}
}

// Round-robin, bounded per frame. Compares DIFFERENCES of same-magnitude values, so there is no
// precision trap: the old signature summed absolute world coordinates and landed near 64000, where one
// float step already exceeded the epsilon and every building read as a mover.
static void verifyCasterMotion(void)
{
	// Ronin @bugfix 20/08/2026 DX9: §29j.8. Rebuild ONCE per completed sweep, never once per mover.
	// Dirtying on first sight meant a full rebuild — scene walk, geometry extraction, new VB/IB — for
	// every single mover found, and each rebuild resets the cursor and renumbers the array, so the sweep
	// restarted from zero and the storm never converged. Symptom: draw counts look perfectly normal
	// while the frame time stays high, because none of that work lands in a draw bucket.
	static Bool s_sweepFoundMover = FALSE;

	if (s_bakedCount == 0)
		return;

	for (int n = 0; n < CASTER_VERIFY_PER_FRAME && n < s_bakedCount; ++n)
	{
		if (s_verifyCursor >= s_bakedCount)
		{
			s_verifyCursor = 0;
			if (s_sweepFoundMover)
			{
				s_sweepFoundMover = FALSE;
				s_casterCacheDirty = TRUE;
				return;
			}
		}
		const int i = s_verifyCursor++;

		const Matrix3D &m = s_bakedMesh[i]->Get_Transform();
		if ((m.Get_Translation() - s_bakedPos[i]).Length2() <= CASTER_POS_EPSILON * CASTER_POS_EPSILON &&
			(m.Get_X_Vector()    - s_bakedAxis[i]).Length2() <= CASTER_AXIS_EPSILON * CASTER_AXIS_EPSILON)
			continue;

		s_bakedMesh[i]->Set_Shadow_Caster_Mover(true);
		s_sweepFoundMover = TRUE;
	}
}

// Ronin @perf 19/08/2026 DX9: §29j.7. Terrain has no drawable but is not a CLASSID_MESH, so it is NOT
// static and stays on the normal path.
Bool R3DShadowMap::isStaticCaster(RenderObjClass *robj)
{
	if (robj == NULL)
		return FALSE;

	DrawableInfo *info = (DrawableInfo *)robj->Get_User_Data();
	if (info == NULL || info->m_drawable == NULL)
		return FALSE;						// terrain, engine helpers: not baked

	Drawable *d = info->m_drawable;
	if (d->getObject() == NULL)
	{
		// Ronin @bugfix 18/09/2026 DX9: object-less USUALLY means map scenery, which is baked and never marked in the moving
		// mask. The build cursor's preview is object-less too and moves every frame: baked, its shadow stayed where the
		// preview had been, and the terrain EMA kept its history — the trail behind a preview dragged quickly.
		return !d->testDrawableStatus(DRAWABLE_STATUS_CURSOR_PREVIEW);
	}

	return d->isKindOf(KINDOF_STRUCTURE) || d->isKindOf(KINDOF_IMMOBILE);
}

// Ronin @feature 13/09/2026 DX9: §29i.3. Start a split's moving-caster mask. Cleared every split every frame — the
// light box moves with the camera, so a grid kept across frames would sit in the wrong place.
void R3DShadowMap::beginMovingMask(Int split)
{
	m_movingMaskSplit = split;
	memset(m_movingMaskGrid, 0, sizeof(m_movingMaskGrid));
}

// Ronin @feature 13/09/2026 DX9: §29i.3. Project a moving caster's bounding sphere through THIS split's light camera
// — the same mapping the receiver's shadowUV uses — and fill its disc, dilated one texel for bilinear sampling.
void R3DShadowMap::noteMovingCaster(Real cx, Real cy, Real cz, Real radius)
{
	if (m_movingMaskSplit < 0 || m_lightCamera == NULL)
		return;

	Vector3 ndc;
	if (m_lightCamera->Project(ndc, Vector3(cx, cy, cz)) == CameraClass::OUTSIDE_NEAR_CLIP)
		return;					// in front of the near plane: not in this split's map at all

	Vector2 vpMin, vpMax;
	m_lightCamera->Get_View_Plane(vpMin, vpMax);
	const Real halfW = (vpMax.X - vpMin.X) * 0.5f;
	const Real halfH = (vpMax.Y - vpMin.Y) * 0.5f;
	if (halfW <= 0.0f || halfH <= 0.0f)
		return;

	// NDC to the receiver's UV, in texels: u = x*0.5+0.5, v = -y*0.5+0.5, exactly as TerrainShadow_ps builds shadowUV.
	const Real res = (Real)MOVING_MASK_RES;
	const Real u  = (ndc.X * 0.5f + 0.5f) * res;
	const Real v  = (-ndc.Y * 0.5f + 0.5f) * res;
	const Real ru = (radius / (2.0f * halfW)) * res + 1.0f;
	const Real rv = (radius / (2.0f * halfH)) * res + 1.0f;

	Int x0 = (Int)floorf(u - ru);
	Int x1 = (Int)ceilf(u + ru);
	Int y0 = (Int)floorf(v - rv);
	Int y1 = (Int)ceilf(v + rv);
	if (x1 < 0 || y1 < 0 || x0 >= MOVING_MASK_RES || y0 >= MOVING_MASK_RES)
		return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= MOVING_MASK_RES) x1 = MOVING_MASK_RES - 1;
	if (y1 >= MOVING_MASK_RES) y1 = MOVING_MASK_RES - 1;

	for (Int y = y0; y <= y1; ++y)
	{
		const Real dy = ((Real)y + 0.5f - v) / rv;
		for (Int x = x0; x <= x1; ++x)
		{
			const Real dx = ((Real)x + 0.5f - u) / ru;
			if (dx * dx + dy * dy <= 1.0f)
				m_movingMaskGrid[y * MOVING_MASK_RES + x] = 255;
		}
	}
}

// Ronin @feature 13/09/2026 DX9: §29i.3. Copy the grid into this split's texture. MANAGED pool, so the lock writes
// system memory only — none of §7's dynamic-buffer stall.
void R3DShadowMap::uploadMovingMask(Int split)
{
	m_movingMaskSplit = -1;
	if (split < 0 || split >= MAX_SHADOW_SPLITS || m_movingMaskTex[split] == NULL)
		return;

	// Ronin @bugfix 08/11/2026 DX9Ex: on 9Ex the mask is a dynamic default-pool texture; a plain lock waits for the GPU
	// still reading last frame's. Every texel is rewritten below, so DISCARD hands out fresh memory instead.
	D3DLOCKED_RECT lr;
	if (FAILED(m_movingMaskTex[split]->LockRect(0, &lr, NULL, DX8Wrapper::Is_Ex() ? D3DLOCK_DISCARD : 0)))
		return;
	for (Int y = 0; y < MOVING_MASK_RES; ++y)
	{
		DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
		const unsigned char *src = &m_movingMaskGrid[y * MOVING_MASK_RES];
		for (Int x = 0; x < MOVING_MASK_RES; ++x)
		{
			const DWORD m = (DWORD)src[x];
			row[x] = 0xFF000000 | (m << 16) | (m << 8) | m;
		}
	}
	m_movingMaskTex[split]->UnlockRect(0);
}

// Flag only — releasing buffers mid-frame could pull them from under a bound draw.
void R3DShadowMap::invalidateStaticCasters(void)
{
	s_casterCacheDirty = TRUE;
}

void R3DShadowMap::ensureStaticCasters(SceneClass *scene)
{
	// Ronin @bugfix 13/09/2026 DX9: §29j.7. A baked building that has since become invisible to the player — shrouded,
	// stealthed, hidden — would keep casting from the bake. Rebuild without it; an excluded one cannot trigger this again.
	if (!s_casterCacheDirty && TheGameClient != NULL)
	{
		for (size_t i = 0; i < s_bakedDrawables.size(); ++i)
		{
			Drawable *draw = TheGameClient->findDrawableByID(s_bakedDrawables[i]);
			if (draw != NULL && (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud()))
			{
				s_casterCacheDirty = TRUE;
				break;
			}
		}
	}

	if (s_casterCacheDirty)
	{
		rebuildStaticCasters(scene);
		s_casterCacheDirty = FALSE;
		return;					// verify on a LATER frame — s_bakedMesh must never be stale here
	}
	verifyCasterMotion();
}

// Rebuilt only when the static caster SET changes, never on camera movement.
void R3DShadowMap::rebuildStaticCasters(SceneClass *scene)
{
	releaseCasterChunks();
	s_bakeVerts.clear();
	s_bakeIndices.clear();

	if (scene != NULL)
	{
		SceneIterator *it = scene->Create_Iterator(false);
		if (it != NULL)
		{
			for (it->First(); !it->Is_Done(); it->Next())
			{
				RenderObjClass *robj = it->Current_Item();
				if (!isStaticCaster(robj))
					continue;
				// Ronin @bugfix 13/09/2026 DX9: §29j.7. Never bake what the player cannot see: it stays on the normal path, where
				// Visibility_Check hides it (W3DScene.cpp:557). Baked, an unscouted enemy base cast its shadows through the shroud.
				Drawable *draw = ((DrawableInfo *)robj->Get_User_Data())->m_drawable;
				if (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud())
					continue;
				bakeCasterObject(robj);
				if (draw->getObject() != NULL)
					s_bakedDrawables.push_back(draw->getID());
			}
			scene->Destroy_Iterator(it);
		}
	}
	flushCasterChunk();

#ifdef DEBUG_LOGGING
	int verts = 0, polys = 0;
	for (int i = 0; i < s_casterChunkCount; ++i)
	{
		verts += s_casterChunk[i].vertCount;
		polys += s_casterChunk[i].polyCount;
	}
	WWDEBUG_SAY(("[SHADOWMAP] static casters baked: %d chunks, %d verts, %d polys, %d meshes",
				 s_casterChunkCount, verts, polys, s_bakedCount));
#endif
}

// Draw the bake into the depth map. FFP, position only, world = identity.
void R3DShadowMap::drawStaticCasters(void)
{
	if (s_casterChunkCount == 0)
		return;

	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0, NULL);
	DX8Wrapper::Set_Material(NULL);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(true));

	for (int i = 0; i < s_casterChunkCount; ++i)
	{
		DX8Wrapper::Set_Index_Buffer(s_casterChunk[i].ib, 0);
		DX8Wrapper::Set_Vertex_Buffer(s_casterChunk[i].vb);
		// Ronin @bugfix 19/09/2026 DX9: bind our own layout. These chunks are position-only (DX8_FVF_XYZ) and
		// Apply_Render_State_Changes keeps the LAST bound FVF (dx8wrapper.cpp:3403), so the bake drew with whatever
		// stride the previous draw left. Inside the loop: every chunk changes the vertex buffer.
		DX8Wrapper::BindLayoutFVF(s_casterChunk[i].vb->FVF_Info().Get_FVF(), "R3DShadowMap::drawStaticCasters");
		DX8Wrapper::Draw_Triangles(0, s_casterChunk[i].polyCount, 0, s_casterChunk[i].vertCount);
	}
}

// Ronin @feature 12/08/2026 DX9: §29 phase 1b. Mirrors the water reflection pass exactly
// (W3DWater.cpp:1485-1521) — a full scene render with an alternate camera, NOT hand-picked objects.
void R3DShadowMap::updateRenderTargetTexture(CameraClass *sceneCam, SceneClass *scene)
{
	// Ronin @bugfix 14/08/2026 DX9: §29h-4.2. Every early return here means NO map this frame, so the
	// flags must go down — otherwise the terrain pass keeps sampling a stale texture and the decals
	// stay suppressed with nothing casting in their place.
	if (!isAvailable() || sceneCam == NULL || scene == NULL)
	{
		TheTerrainShadowPass.enabled = FALSE;
		TheTerrainShadowPass.active  = FALSE;
		return;
	}

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. The fit moved INTO the depth pass, once per split.
	// Nothing between here and beginDepthPass reads it, and each split must publish before it renders.

	// Ronin @feature 03/09/2026 DX9: §29j.13h. Flip the accumulation pair. Reprojection makes the weight camera-independent.
	ensureAccumTargets();
	m_accumIndex ^= 1;

	ensureStaticCasters(scene);		// §29j.7 — bake on first use
	if (m_lightCamera == NULL)
	{
		TheTerrainShadowPass.enabled = FALSE;
		TheTerrainShadowPass.active  = FALSE;
		return;
	}

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. beginDepthPass moved INTO the split loop — each split
	// binds and clears its own target. Same guard, same early-out; isAvailable is all the old call tested.
	if (!isAvailable())
	{
		TheTerrainShadowPass.enabled = FALSE;
		TheTerrainShadowPass.active  = FALSE;
		return;
	}

	// Depth-only: the map needs Z, never colour. Debug mode keeps colour so we can SEE the light view.
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	DWORD oldColorWrite = 0xFFFFFFFF;
	if (dev && !TheShowShadowMapDebug)
	{
		dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0);
	}

	{
		// Ronin @diagnostic 12/08/2026 §29 DX9: tag the whole depth pass so its draws stop
		// masquerading as rigid/terrain in [DRAW].
		Debug_Statistics::DrawSubsystemScope smTag(Debug_Statistics::DRAW_SUBSYS_SHADOWMAP, true);
		// Ronin @bugfix 14/08/2026 §29h DX9: this render draws the TERRAIN, which would re-enter
		// renderTerrainShadowPass and bind the shadow map as a texture while it IS the render target.
		TheTerrainShadowPass.enabled = FALSE;
		// Ronin @bugfix 14/08/2026 §29h-4.3 DX9: mark the depth pass — it is a SECOND scene render,
		// and subsystems that latch per-frame camera state must not consume it for the light camera.
		TheTerrainShadowPass.inDepthPass = TRUE;
		// Ronin @bugfix 15/08/2026 §29h-4.3 DX9: publish the MAIN camera so caster culling inside the
		// depth pass tests against the view instead of the stepping light fit.
		TheTerrainShadowPass.sceneCamera = sceneCam;

		// Ronin @bugfix 17/08/2026 §29h-6 DX9: NO rasteriser bias — four sites zero
		// D3DRS_SLOPESCALEDEPTHBIAS from inside the scene render, so it cannot survive. The bias lives on
		// the RECEIVER instead (getDepthBias). Doc §10.
		{
			const float zeroBias = 0.0f;
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, *(const DWORD *)&zeroBias);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS,           *(const DWORD *)&zeroBias);
		}

		// Ronin @perf 19/08/2026 DX9: §29i.5. Caster LOD — measured ~0 here (single-LOD models), kept
		// because it costs nothing and pays on assets that have levels.
		// Ronin @bugfix 19/08/2026 DX9: UNBIND THE SHADOW MAP FIRST — m_depthTarget is this pass's render
		// target, and reading a bound surface is undefined. Handed back down after the pass.
		DX8InstanceManagerClass::Set_Shadow_Map(NULL, NULL, 0.0f, 0.0f, NULL, 0.0f);
		MeshClass::Skip_Baked_Shadow_Casters(true);		// §29j.8 — baked meshes don't draw here
		MeshClass::Set_In_Shadow_Depth_Pass(true);		// §29i.5 — colour writes are off past here
		// Ronin @bugfix 19/09/2026 DX9: a mover mesh marks ITSELF in the mask as it draws. Visibility_Check can only mark
		// per object, and a flag's parent structure is a static caster, so its moving shadow kept full EMA history.
		MeshClass::Set_Shadow_Mover_Note_Func(&R3DShadowMap::noteMovingCaster);
		HLodClass::Set_Force_Lowest_LOD(true);

		// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. One fit + one scene render per split. The fit
		// publishes fitCentre and the light-space box, so the tile cull and Visibility_Check re-cull per split.
		// Ronin @diagnostic 09/09/2026 DX9: §29i.3 step 2. BACKWARDS so the LAST fit is split 0 and the
		// [SHADOW] line shows the NEAR split — the far one is the whole box and tells us nothing.
		for (Int split = m_splitCount - 1; split >= 0; --split)
		{
			// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. Bind and clear THIS split's own depth target
			// before its fit runs — one shared target lets the second split destroy the first.
			if (!beginDepthPass(split))
				continue;
			// Ronin @bugfix 13/09/2026 DX9: §29i.3. Both maps take the full distance band; the NEAR one fits only the central
			// NEAR_SCREEN_PCT of the screen, the FAR one the whole view. At COUNT 1 this is the fit that shipped.
			const Real screenFrac = (split == 0 && m_splitCount > 1) ? ((Real)NEAR_SCREEN_PCT * 0.01f) : 1.0f;
			updateLightMatrices(sceneCam->Get_Frustum(), split, 0.0f, m_maxShadowDistance, screenFrac);
			// Ronin @feature 13/09/2026 DX9: §29i.3. Visibility_Check marks moving casters into this split's mask during the
			// render; the grid is uploaded once the split's draws are done.
			beginMovingMask(split);
			WW3D::Render(scene, m_lightCamera);
			// §29j.7 — the static casters the scene render just skipped, in a few draws.
			drawStaticCasters();
			uploadMovingMask(split);


			// Ronin @bugfix 12/08/2026 §29 DX9: re-Apply AFTER the render, then capture. Reading the
			// device cold returned whatever drew last (particles set VIEW to identity) — the blinking.
			// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. Per split, while m_lightCamera still holds
			// THIS split's transform.
			if (dev)
			{
				m_lightCamera->Apply();
				// Set_Transform(D3DTS_VIEW) only CACHES + dirties (dx8wrapper.h:1801); the device keeps
				// the stale view until this flush. Reading without it gave a half-stale matrix.
				DX8Wrapper::Apply_Render_State_Changes();

				D3DMATRIX viewMat, projMat;
				dev->GetTransform(D3DTS_VIEW, &viewMat);
				dev->GetTransform(D3DTS_PROJECTION, &projMat);
				dev->GetTransform(D3DTS_VIEW, &viewMat);
				dev->GetTransform(D3DTS_PROJECTION, &projMat);

				D3DXMATRIX dxView(viewMat), dxProj(projMat), dxViewProj, dxViewProjT;
				D3DXMatrixMultiply(&dxViewProj, &dxView, &dxProj);
				D3DXMatrixTranspose(&dxViewProjT, &dxViewProj);
				memcpy(&m_lightViewProj[split], &dxViewProjT, sizeof(float) * 16);
			}
		}

		HLodClass::Set_Force_Lowest_LOD(false);
		MeshClass::Skip_Baked_Shadow_Casters(false);
		MeshClass::Set_In_Shadow_Depth_Pass(false);


		if (dev)
		{
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, 0);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS,           0);
		}


		TheTerrainShadowPass.sceneCamera = NULL;
		TheTerrainShadowPass.inDepthPass = FALSE;
	}

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. The capture moved INTO the split loop — sitting here
	// it could only ever record the LAST split, so the far cascade never had a matrix of its own.

	if (dev && !TheShowShadowMapDebug)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, oldColorWrite);

	endDepthPass();

	// Ronin @bugfix 17/08/2026 DX9: §29h-4.4/§29h-6. The sun direction is computed ONCE here and fed to
	// BOTH receivers, instead of being derived after the fact for the terrain only — the rigid path
	// needs it now too, and two copies of a sign convention is how §29e's sign question stayed open.
	Vector3 travel(TheGlobalData->m_terrainLightPos[0].x,
				   TheGlobalData->m_terrainLightPos[0].y,
				   TheGlobalData->m_terrainLightPos[0].z);
	if (travel.Length2() < 1e-6f)
		travel.Set(0.0f, 0.0f, -1.0f);
	travel.Normalize();
	const float travelF[3] = { travel.X, travel.Y, travel.Z };

	// Hand the finished map down to the rigid flush (§29 phase 2).
	TextureBaseClass *smap = peekShadowTexture();
	DX8InstanceManagerClass::Set_Shadow_Map(
		smap ? smap->Peek_D3D_Base_Texture() : NULL,
		(const float *)&m_lightViewProj[0],
		getTexelOffset(),
		getDepthBias(0),
		travelF,
		getTexelWorldSize(0));

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. FAR cascade for the rigid receiver.
	TextureBaseClass *smapFarRigid = (m_splitCount > 1 && m_mode == SHADOWMAP_HW_DEPTH)
									 ? (TextureBaseClass *)m_depthTarget[1] : NULL;
	DX8InstanceManagerClass::Set_Shadow_Map_Far(
		smapFarRigid ? smapFarRigid->Peek_D3D_Base_Texture() : NULL,
		(const float *)&m_lightViewProj[1],
		getDepthBias(1),
		getTexelWorldSize(1));

	// And down to the TERRAIN pass (§29h). Core/HeightMap.cpp cannot include this header — it is
	// compiled for the Generals target too — so the state is pushed into a Core-owned struct.
	TheTerrainShadowPass.enabled     = (smap != NULL && m_terrainShadowVS != NULL &&
										m_terrainShadowPS != NULL);
	// Ronin @feature 14/08/2026 DX9: §29h-4.2. `active` keys off the MAP existing, not the terrain
	// receiver's shaders — rigid receivers (§29f) take the map by a different route, so the decal
	// suppression must not depend on the terrain pass being ready.
	TheTerrainShadowPass.active      = (smap != NULL);
	TheTerrainShadowPass.vs          = m_terrainShadowVS;
	TheTerrainShadowPass.ps          = m_terrainShadowPS;
	TheTerrainShadowPass.shadowTex   = smap ? smap->Peek_D3D_Base_Texture() : NULL;
	// Ronin @feature 03/09/2026 DX9: §29j.13h. 0.94 history weight is an EMA time constant of ~16
	// frames, which is the tree step period measured in §29j.13g — anything shorter just adds one
	// intermediate step to the jump and is invisible, which is exactly what the N=2 attempt proved.
	// Zeroed the moment the camera moves, because there is no reprojection.
	TheTerrainShadowPass.accumPrev   = m_accumTex[1 - m_accumIndex];
	TheTerrainShadowPass.accumCur    = m_accumTex[m_accumIndex];
	TheTerrainShadowPass.accumSurf   = m_accumSurf[m_accumIndex];
	// §29j.13n — tree pair, same ping-pong phase so both receivers agree on which frame is "previous".
	TheTerrainShadowPass.treeAccumPrev = m_treeAccumTex[1 - m_accumIndex];
	TheTerrainShadowPass.treeAccumCur  = m_treeAccumTex[m_accumIndex];
	TheTerrainShadowPass.treeAccumSurf = m_treeAccumSurf[m_accumIndex];
	// Ronin @feature 06/09/2026 DX9: §29j.13l. Reprojection makes the weight camera-independent; the
	// shader zeroes it per pixel where no history exists.
	TheTerrainShadowPass.accumWeight = (m_accumTex[0] != NULL) ? 0.94f : 0.0f;

	TheTerrainShadowPass.depthBias   = getDepthBias(0);
	TheTerrainShadowPass.texelOffset = getTexelOffset();
	TheTerrainShadowPass.texelWorldSize = getTexelWorldSize(0);
	memcpy(TheTerrainShadowPass.lightViewProjT, &m_lightViewProj[0], sizeof(float) * 16);

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. FAR cascade. Only HW_DEPTH has a second depth
	// texture; anything else publishes count 1 and the receiver stays on the near map alone.
	TextureBaseClass *smapFar = (m_splitCount > 1 && m_mode == SHADOWMAP_HW_DEPTH)
								? (TextureBaseClass *)m_depthTarget[1] : NULL;
	TheTerrainShadowPass.shadowTexFar      = smapFar ? smapFar->Peek_D3D_Base_Texture() : NULL;
	TheTerrainShadowPass.depthBiasFar      = getDepthBias(1);
	TheTerrainShadowPass.texelWorldSizeFar = getTexelWorldSize(1);
	TheTerrainShadowPass.cascadeCount      = (TheTerrainShadowPass.shadowTexFar != NULL) ? 2.0f : 1.0f;
	memcpy(TheTerrainShadowPass.lightViewProjFarT, &m_lightViewProj[1], sizeof(float) * 16);
	// Ronin @feature 13/09/2026 DX9: §29i.3. Moving-caster masks, in the same split order as the maps.
	TheTerrainShadowPass.movingMaskNear = m_movingMaskTex[0];
	TheTerrainShadowPass.movingMaskFar  = (m_splitCount > 1) ? m_movingMaskTex[1] : NULL;

	// §29h-4.4: the same direction also drives caster sweeping in W3DTreeBuffer::cull().
	TheTerrainShadowPass.lightTravelDir[0] = travel.X;
	TheTerrainShadowPass.lightTravelDir[1] = travel.Y;
	TheTerrainShadowPass.lightTravelDir[2] = travel.Z;
}

void R3DShadowMap::drawDebugOverlay(void)
{
	if (!TheShowShadowMapDebug || !isAvailable() || m_colorTarget[0] == NULL)
		return;

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == NULL)
		return;

	// Pre-transformed screen quad, same shape the stencil resolve uses
	// (W3DVolumetricShadow.cpp:3419) — no shader, no view/proj dependency.
	struct _TLVERTEX { D3DXVECTOR4 p; DWORD color; float u, v; } q[4];
	const float SIZE = 256.0f;
	const float X0 = 8.0f, Y0 = 360.0f;

	q[0].p = D3DXVECTOR4(X0,        Y0,        0.0f, 1.0f); q[0].u = 0.0f; q[0].v = 0.0f;
	q[1].p = D3DXVECTOR4(X0 + SIZE, Y0,        0.0f, 1.0f); q[1].u = 1.0f; q[1].v = 0.0f;
	q[2].p = D3DXVECTOR4(X0,        Y0 + SIZE, 0.0f, 1.0f); q[2].u = 0.0f; q[2].v = 1.0f;
	q[3].p = D3DXVECTOR4(X0 + SIZE, Y0 + SIZE, 0.0f, 1.0f); q[3].u = 1.0f; q[3].v = 1.0f;
	for (int i = 0; i < 4; ++i)
		q[i].color = 0xFFFFFFFF;

	DX8Wrapper::Invalidate_Cached_Render_States();

	dev->SetTexture(0, m_colorTarget[0]->Peek_D3D_Texture());
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(_TLVERTEX));

	dev->SetTexture(0, NULL);
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Bool R3DShadowMap::beginDepthPass(Int split)
{
	if (!isAvailable())
		return FALSE;
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. Clamp, then refuse a split whose target was never
	// created — init only makes m_splitCount of them.
	if (split < 0) split = 0;
	if (split >= MAX_SHADOW_SPLITS) split = MAX_SHADOW_SPLITS - 1;
	if (m_colorTarget[split] == NULL || m_depthTarget[split] == NULL)
		return FALSE;

	// Ronin @bugfix 09/09/2026 DX9: §29i.3 step 2. BOTH surfaces must be per-split. Set_Render_Target
	// gates the whole bind — SetDepthStencilSurface included — on the COLOUR surface changing.
	DX8Wrapper::Set_Render_Target_With_Z(m_colorTarget[split], m_depthTarget[split]);

	// Ronin @bugfix 16/08/2026 DX9: §29h-11 ROOT CAUSE. This pass runs BEFORE WW3D::Begin_Render, on
	// whatever state the projected-shadow render targets left on the device. With Z off there and the
	// wrapper cache believing it is on, every Set_DX8_Render_State(ZWRITEENABLE,TRUE) inside the scene
	// render is a silent no-op: colour rasterises perfectly, the depth surface keeps the clear value,
	// and the receiver reads "nothing occludes here" on every map. Invalidate so the cache is forced
	// to re-assert against the device (§12a), then state the three Z modes this pass requires.
	DX8Wrapper::Invalidate_Cached_Render_States();
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE,      D3DZB_TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,        D3DCMP_LESSEQUAL);

	// Far plane everywhere = "nothing occludes here". NOTE the 4th arg is dest_alpha, z is 5th.
	DX8Wrapper::Clear(true, true, Vector3(1.0f, 1.0f, 1.0f), 0.0f, 1.0f);
	return TRUE;
}



void R3DShadowMap::endDepthPass(void)
{
	// Restores the back buffer AND the default depth buffer — the same call the water reflection
	// pass uses to come back (W3DWater.cpp:1521).
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);
}
