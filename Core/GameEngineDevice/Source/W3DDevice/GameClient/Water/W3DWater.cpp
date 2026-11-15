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

// FILE: W3DWater.cpp /////////////////////////////////////////////////////////////////////////////
// Created:   Mark Wilczynski, June 2001
// Desc:      Draw reflective water surface.  Also handles drawing of waves/ripples
//			  on the surface.
///////////////////////////////////////////////////////////////////////////////////////////////////

#define SCROLL_UV

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
// Ronin @build 18/10/2025 Include DX8-to-DX9 compatibility layer first
#include "WW3D2/dx8todx9.h"
#include <cmath>  // For fabsf, sqrt, sin, cos, acos, etc.
#include <vector>	// Ronin @feature 02/10/2026 DX9: phase 4 - drawRiverWater's flow per cross-section

#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/R3DShadowMapState.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/texture.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/scene.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/Renderer.h"
#include "WW3D2/light.h"
#include "d3dx9math.h"
#include "WWLib/simplevec.h"
#include "WW3D2/mesh.h"
#include "WW3D2/matinfo.h"

#include "Common/FramePacer.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"

#include "GameClient/Color.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "WW3D2/statistics.h"   // Ronin @diagnostic 02/08/2026: per-subsystem draw attribution ([DRAW])
#include "W3DDevice/GameClient/R3DWaterSea.h"	// Ronin @feature 29/09/2026 DX9: the WaterSea binding and look
#include "W3DDevice/GameClient/R3DWaterSeaIni.h"	// Ronin @feature 04/10/2026 DX9: the water ini, read once in init
#include "W3DDevice/GameClient/R3DWaterMirror.h"	// Ronin @bugfix 01/10/2026 DX9: the mirror plane, for the tree cull
#include "W3DDevice/GameClient/R3DTaa.h"	// Ronin @bugfix 30/09/2026 DX9: the jitter the reflection lookup takes out
// Ronin @feature 03/10/2026 DX9: phase 5 - units that float ride the swell (R3DWater_RideSwell)
#include "W3DDevice/GameClient/R3DWaterFloat.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"


#define MIPMAP_BUMP_TEXTURE

// DEFINES ////////////////////////////////////////////////////////////////////////////////////////
#define SKYPLANE_SIZE	(384.0f*MAP_XY_FACTOR)
#define SKYPLANE_HEIGHT	(30.0f)

#define SKYBODY_TEXTURE	"TSMoonLarg.tga"
#define SKYBODY_SIZE	45.0f		//extent or radius of sky body

#define SKYBODY_X	150.0f	//location of skybody
#define SKYBODY_Y	550.0f	//location of skybody

/* in the bay
#define SKYBODY_X	120.0f			//location of skybody
#define SKYBODY_Y	75.0f			//location of skybody
*/

#define SKYBODY_HEIGHT	SKYPLANE_HEIGHT	//altitude of sky body (z-buffer disabled, so can equal sky height).

//GeForce3 water system defines
#define PATCH_SIZE 15		//number of vertices on patch edge.  Large patches may waste vertices off edge of screen.
#define PATCH_UV_TILES	42	//number of times the bump map texture is tiled across patch (must be integer!).
#define PATCH_SCALE (4.0f * MAP_XY_FACTOR)	//horizontal scale factor. Adjust this and size to get desired vertex density.
#define SEA_REFLECTION_SIZE 256		//dimensions of reflection texture

#define SEA_BUMP_SCALE		(0.06f)		//scales the du/dv offsets stored in bump map (~ amount to perturb)
#define BUMP_SIZE (50.f)
#define REFLECTION_FACTOR 0.1f

#define PATCH_WIDTH (PATCH_SIZE-1)	//internal defines
#define PATCH_UV_SCALE	((Real)PATCH_UV_TILES/(Real)PATCH_WIDTH)

//3D Grid Mesh Water defines.
#define WATER_MESH_OPACITY		0.5f
#define WATER_MESH_X_VERTICES	128
#define WATER_MESH_Y_VERTICES	128
#define WATER_MESH_SPACING	MAP_XY_FACTOR	//same as terrain

#ifdef USE_MESH_NORMALS
#define WATER_MESH_FVF	DX8_FVF_XYZNDUV2
typedef VertexFormatXYZNDUV2 MaterMeshVertexFormat;
#else
#define WATER_MESH_FVF	DX8_FVF_XYZDUV2
typedef VertexFormatXYZDUV2 MaterMeshVertexFormat;
#endif

#define DRAW_WATER_WAKES
/// @todo: Fix clipping of objects that intersect the mirror surface
//#define CLIP_GEOMETRY_TO_PLANE	// this enables clipping of objects that intersect the mirror surfaces

// Some shader combinations that can be useful in rendering water:

// Modulate stage0 with stage1 texture.  Also modulate stage 0 with vertex color.
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA,ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, 	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_DETAILBLEND, ShaderClass::DETAILALPHA_DISABLE) )

// Just a z-buffer fill, nothing is written to the color buffer.
#define SC_ZFILL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_DISABLE, ShaderClass::SRCBLEND_ZERO, \
	ShaderClass::DSTBLEND_ONE, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

// No texturing, just vertex color with vertex alpha
#define SC_ZFILL_BLENDx ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, \
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Modulate blended with vertex alpha modulation
#define SC_ZFILL_MODULATE_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_ALPHA_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_OPAQUE_TEXONLY ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ONE, ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_BLEND3 ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass zFillAlphaShader(SC_ZFILL_BLEND3);
static ShaderClass blendStagesShader(SC_DETAIL_BLEND);

WaterRenderObjClass *TheWaterRenderObj=nullptr; ///<global water rendering object
// Ronin @feature 15/09/2026 DX9: water on/off switches for the debug panel (W3DWater.h).
// Ronin @feature 28/09/2026 DX9: + seaPlane (WaterType 2). The look knobs moved to R3DWaterSeaTuning.h on 29/09.
WaterDebugFlags TheWaterDebug = { FALSE, FALSE, FALSE, FALSE };
// Ronin @diagnostic 28/09/2026 DX9: [WATER] panel row (W3DWater.h).
WaterDebugStats TheWaterStats = {};

// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - count a water draw and whether `expected` (and expectedVS) was bound:
// the shaders go on with raw device calls, so loaded is not proof of use.
static void countWaterDraw(UnsignedInt &draws, UnsignedInt &psDraws, IDirect3DPixelShader9 *expected,
	IDirect3DVertexShader9 *expectedVS = nullptr)
{
	if (!TheWaterStats.enabled)
		return;
	++draws;
	if (expected == nullptr)
		return;
	IDirect3DPixelShader9 *bound = nullptr;
	if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetPixelShader(&bound)))
		return;
	if (bound != nullptr)
		bound->Release();	// GetPixelShader adds a reference
	if (bound != expected)
		return;
	if (expectedVS != nullptr)
	{
		IDirect3DVertexShader9 *boundVS = nullptr;
		if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetVertexShader(&boundVS)))
			return;
		if (boundVS != nullptr)
			boundVS->Release();	// GetVertexShader adds a reference
		if (boundVS != expectedVS)
			return;
	}
	++psDraws;
}

static Int getRiverVertexDiffuse(W3DShroud *shroud, Real x, Real y, Real shadeR, Real shadeG, Real shadeB, Int diffuse)
{
	if (!shroud)
		return diffuse;

	Int cellX = (Int)(x / shroud->getCellWidth());
	Int cellY = (Int)(y / shroud->getCellHeight());
	W3DShroudLevel level = shroud->getShroudLevel(cellX, cellY);
	Real shroudScale = (Real)level / 255.0f;
	return GameMakeColor(
		(Int)(shadeR * shroudScale),
		(Int)(shadeG * shroudScale),
		(Int)(shadeB * shroudScale),
		// Ronin @bugfix 11/10/2026 DX9: the alpha is not the shroud's. Scaled by it (how the PS was handed the shroud, 05/10) a
		// river under the fog of war went see-through: the riverbed showed, not the water.
		(diffuse >> 24) & 0xff);
}

void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}


#define DONUT_SIDES	90
#define INNER_RADIUS 200.0f
#define OUTER_RADIUS 250.0f
#define TEXTURE_REPEAT_COUNT 16
#define DONUT_HEIGHT	15.0f
//#define DO_FLAT_DONUT
#define AMP_SCALE	(30.0f/120.0f)
#define WAVE_FREQ	0.3f
#define AMP_SCALE2	(10.0f/120.0f)
#define NOISE_FREQ	(2.0f*PI/WAVE_FREQ)

#define NOISE_REPEAT_FACTOR ((float)(1.0f/(16.0f)))


static Bool wireframeForDebug = 0;

void WaterRenderObjClass::setupJbaWaterShader()
{
	if (!TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	m_riverTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);


//	Setting *setting=&m_settings[m_tod];


	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_ADD );
	if (!m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	DX8Wrapper::Set_DX8_Texture(3,m_riverAlphaEdge->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Sampler_State(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Sampler_State(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXCOORDINDEX, 1);

	Bool doSparkles = true;

	if (m_riverWaterPixelShader && doSparkles) {
		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();
		DX8Wrapper::Set_DX8_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();
		DX8Wrapper::Set_DX8_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());

		DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		D3DXMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, &curView);
		D3DXMATRIX scale;
		D3DXMatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		D3DXMATRIX destMatrix = inv * scale;
		D3DXMatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, destMatrix);

	}

	// Ronin @bugfix 10/03/2026 DX9: Route water sampler writes through wrapper so sampler cache stays coherent.
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);


	if (m_riverWaterPixelShader){
		//DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(0,
		//	D3DXVECTOR4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f), 1);

		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_riverWaterPixelShader);
	}
}




//-------------------------------------------------------------------------------------------------
/** Destructor. Releases w3d assets. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::~WaterRenderObjClass()
{
	REF_PTR_RELEASE(m_meshVertexMaterialClass);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	REF_PTR_RELEASE(m_meshLight);
	REF_PTR_RELEASE(m_alphaClippingTexture);
	REF_PTR_RELEASE (m_skyBox);

	REF_PTR_RELEASE (m_riverTexture);
	REF_PTR_RELEASE (m_whiteTexture);
	REF_PTR_RELEASE (m_waterNoiseTexture);
	REF_PTR_RELEASE (m_riverAlphaEdge);
	REF_PTR_RELEASE (m_waterSparklesTexture);
	REF_PTR_RELEASE (m_waterNormalTexture);	// Ronin @feature 28/09/2026 DX9: WaterSea surface normals
	REF_PTR_RELEASE (m_waterNormalLake);	// Ronin @feature 02/10/2026 DX9: phase 4 - the lake's
	REF_PTR_RELEASE (m_waterNormalRiver);	// Ronin @feature 02/10/2026 DX9: phase 4 - the river's
	SAFE_RELEASE(m_heightTexture);			// Ronin @feature 29/09/2026 DX9: phase 1 - terrain heights
	Int i;

	for(i=0; i<TIME_OF_DAY_COUNT; i++)
	{	REF_PTR_RELEASE(m_settings[i].skyTexture);
		REF_PTR_RELEASE(m_settings[i].waterTexture);
	}

	i=NUM_BUMP_FRAMES;
	while (i--)
	{	SAFE_RELEASE( m_pBumpTexture[i]);
		SAFE_RELEASE( m_pBumpTexture2[i]);
	}

	delete [] m_meshData;
	m_meshData = nullptr;
	m_meshDataSize = 0;

	//Release strings allocated inside global water settings.
	for  (i=0; i<TIME_OF_DAY_COUNT; i++)
	{	WaterSettings[i].m_skyTextureFile.clear();
		WaterSettings[i].m_waterTextureFile.clear();
	}
	deleteInstance((WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer());
	TheWaterTransparency = nullptr;
	ReleaseResources();

	delete m_waterTrackSystem;
}

//-------------------------------------------------------------------------------------------------
/** Constructor. Just nulls out some variables. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::WaterRenderObjClass()
{
	memset( &m_settings, 0, sizeof( m_settings ) );
	m_dx=0;
	m_dy=0;
	m_indexBuffer=nullptr;
	m_waterTrackSystem = nullptr;
	m_doWaterGrid = FALSE;
	m_meshVertexMaterialClass=nullptr;
	m_meshLight=nullptr;
	m_vertexMaterialClass=nullptr;
	m_alphaClippingTexture=nullptr;
	m_useCloudLayer=true;
	m_waterType = WATER_TYPE_0_TRANSLUCENT;
	m_tod=TIME_OF_DAY_AFTERNOON;
	m_pReflectionTexture=nullptr;
	m_pReflectionDepth=nullptr;
	m_skyBox=nullptr;
	m_vertexBufferD3D=nullptr;
	m_indexBufferD3D=nullptr;
	m_vertexBufferD3DOffset=0;

	m_dwWavePixelShader=0;
	m_wavePSViews=nullptr;			// Ronin @feature 02/10/2026 DX9: phase 4 - the WaterSea variants
	m_wavePSRiver=nullptr;
	m_wavePSRiverViews=nullptr;
	m_bindKind=WATER_KIND_SEA;
	m_dwWaveVertexShader=0;
	m_meshData=nullptr;
	m_meshDataSize = 0;
	m_meshInMotion = FALSE;
	m_gridOrigin=Vector2(0,0);
	m_gridDirectionX=Vector2(1.0f,0.0f);
	m_gridDirectionY=Vector2(1.0f,0.0f);

	m_gridCellSize=WATER_MESH_SPACING;
	m_gridCellsX=WATER_MESH_X_VERTICES;
	m_gridCellsY=WATER_MESH_Y_VERTICES;
	m_gridWidth = m_gridCellsX * m_gridCellSize;
	m_gridHeight = m_gridCellsY * m_gridCellSize;

	Int i=NUM_BUMP_FRAMES;
	while (i--)
		m_pBumpTexture[i]=nullptr;

	m_riverVOrigin=0;
	m_riverTexture=nullptr;
	m_whiteTexture=nullptr;
	m_waterNoiseTexture=nullptr;
	m_riverAlphaEdge=nullptr;
	m_waterPixelShader=0;		///<D3D handle to pixel shader.
	m_riverWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_trapezoidWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_waterSparklesTexture=nullptr;
	m_waterNormalTexture=nullptr;	// Ronin @feature 28/09/2026 DX9: WaterSea surface normals
	m_waterNormalLake=nullptr;		// Ronin @feature 02/10/2026 DX9: phase 4 - the lake's
	m_waterNormalRiver=nullptr;		// Ronin @feature 02/10/2026 DX9: phase 4 - the river's
	m_heightTexture=nullptr;		// Ronin @feature 29/09/2026 DX9: phase 1 - terrain heights, built per map
	m_heightTexW=0;
	m_heightTexH=0;
	m_heightBorder=0;
	m_heightVTF=FALSE;				// Ronin @feature 29/09/2026 DX9: phase 2
	m_refractionTexture=nullptr;	// Ronin @feature 29/09/2026 DX9: phase 3
	m_refractionW=0;
	m_refractionH=0;
	m_refractionOK=FALSE;
	m_standingIsSea=FALSE;			// Ronin @bugfix 01/10/2026 DX9: set per polygon in renderWater
	m_swellCellNow=0.0f;			// Ronin @bugfix 03/10/2026 DX9: set per polygon in drawTrapezoidWater
	m_swellCellSea=0.0f;			// Ronin @feature 03/10/2026 DX9: phase 5 - 0 = none drawn yet: the profile's `swellcell`
	m_swellCellLake=0.0f;
	m_wakeTexture=nullptr;			// Ronin @feature 03/10/2026 DX9: phase 5 - wakes
	m_wakeVS=nullptr;
	m_wakePS=nullptr;
	m_wakeFormat=-1;				// not asked yet
	m_wakeScale=0.0f;
	m_wakeOffsetU=0.0f;
	m_wakeOffsetV=0.0f;
	clearWakes();
	m_riverXOffset=0;
	m_riverYOffset=0;
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding sphere used in frustum culling*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.
	Vector3	ObjSpaceCenter(0,0,0);
//	Vector3	ObjSpaceRadius(m_dx,m_dy,0);
	Vector3	ObjSpaceRadius(50000,50000,0);

	sphere.Init(ObjSpaceCenter,ObjSpaceRadius.Length());
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding box used in collision detection*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.

	Vector3	ObjSpaceCenter(0,0,0);
	Vector3	ObjSpaceExtents(50000,50000,0.001f*m_dy);	//since mirror is a plane, it has no thickness. Set to m_dy/1000.

	box.Init(ObjSpaceCenter,ObjSpaceExtents);
}

//-------------------------------------------------------------------------------------------------
/** returns the class id, so the scene can tell what kind of render object it has. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::Class_ID() const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
/** Not used, but required virtual method. */
//-------------------------------------------------------------------------------------------------
RenderObjClass *	 WaterRenderObjClass::Clone() const
{
	assert(false);
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Copies raw bits from pBumpSrc (a regular grayscale texture) into a D3D
	*   bump-map format. */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::initBumpMap(LPDIRECT3DTEXTURE8 *pTex, TextureClass *pBumpSource)
{
    SurfaceClass::SurfaceDescription    d3dsd;
	SurfaceClass * surf;
    D3DLOCKED_RECT     d3dlr;
	DWORD dwSrcPitch;
	BYTE* pSrc;
	Int numLevels;

#ifdef MIPMAP_BUMP_TEXTURE

	pBumpSource->Get_Level_Description(d3dsd);

	if (Get_Bytes_Per_Pixel(d3dsd.Format) != 4)
	{
		// LORENZEN WAS BUGGED BY THIS,
		//		DEBUG_CRASH(("WaterRenderObjClass::Invalid BumpMap format - Was it compressed?") );
		return S_OK;
	}

	if (pBumpSource->Peek_D3D_Texture())
	{
		numLevels=pBumpSource->Peek_D3D_Texture()->GetLevelCount();
	}
	else
		return S_OK;

	pTex[0]=DX8Wrapper::_Create_DX8_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,MIP_LEVELS_ALL,D3DPOOL_MANAGED,false);

	for (Int level=0; level < numLevels; level++)
	{
		surf=pBumpSource->Get_Surface_Level(level);
		surf->Get_Description(d3dsd);
		pSrc=(unsigned char *)surf->Lock((int *)&dwSrcPitch);

		pTex[0]->LockRect( level, &d3dlr, nullptr, 0 );
		DWORD dwDstPitch = (DWORD)d3dlr.Pitch;
		BYTE* pDst       = (BYTE*)d3dlr.pBits;

		for( DWORD y=0; y<d3dsd.Height; y++ )
		{
			BYTE* pDstT  = pDst;
			BYTE* pSrcB0 = (BYTE*)pSrc;
			BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
			BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

			if( y == d3dsd.Height-1 )  // Don't go past the last line
				pSrcB1 = pSrcB0;
			if( y == 0 )               // Don't go before first line
				pSrcB2 = pSrcB0;

			for( DWORD x=0; x<d3dsd.Width; x++ )
			{
				LONG v00 = 256-*(pSrcB0+0); // Get the current pixel
				LONG v01 = 256-*(pSrcB0+4); // and the pixel to the right
				LONG vM1 = 256-*(pSrcB0-4); // and the pixel to the left
				LONG v10 = 256-*(pSrcB1+0); // and the pixel one line below.
				LONG v1M = 256-*(pSrcB2+0); // and the pixel one line above.

				LONG iDu = (vM1-v01); // The delta-u bump value
				LONG iDv = (v1M-v10); // The delta-v bump value

				if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
				{
					iDu = vM1-v00;                 // Choose greater of 1st order diffs
					if( iDu < v00-v01 )
						iDu = v00-v01;
				}

				// The luminance bump value (land masses are less shiny)
				WORD uL = ( v00>1 ) ? 63 : 127;

				switch( D3DFMT_V8U8)//m_BumpMapFormat )
				{
					case D3DFMT_V8U8:
						*pDstT++ = (BYTE)iDu;
						*pDstT++ = (BYTE)iDv;
						break;

					case D3DFMT_L6V5U5:
						*(WORD*)pDstT  = (WORD)( ( (iDu>>3) & 0x1f ) <<  0 );
						*(WORD*)pDstT |= (WORD)( ( (iDv>>3) & 0x1f ) <<  5 );
						*(WORD*)pDstT |= (WORD)( ( ( uL>>2) & 0x3f ) << 10 );
						pDstT += 2;
						break;

					case D3DFMT_X8L8V8U8:
						*pDstT++ = (BYTE)iDu;
						*pDstT++ = (BYTE)iDv;
						*pDstT++ = (BYTE)uL;
						*pDstT++ = (BYTE)0L;
						break;
				}

				// Move one pixel to the left (src is 32-bpp)
				pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
			}

			// Move to the next line
			pSrc += dwSrcPitch;    pDst += dwDstPitch;
		}

		pTex[0]->UnlockRect(level);
		surf->Unlock();
		REF_PTR_RELEASE (surf);
	}

#else
	surf=pBumpSource->Get_Surface_Level();
	surf->Get_Description(d3dsd);
	pSrc=(unsigned char *)surf->Lock((int *)&dwSrcPitch);

    // Create the bumpmap's surface and texture objects
	m_pBumpTexture[i]=DX8Wrapper::_Create_DX8_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,TextureClass::MIP_LEVELS_1,D3DPOOL_MANAGED,false);

    // Fill the bits of the new texture surface with bits from
    // a private format.

    m_pBumpTexture[i]->LockRect( 0, &d3dlr, 0, 0 );
    DWORD dwDstPitch = (DWORD)d3dlr.Pitch;
    BYTE* pDst       = (BYTE*)d3dlr.pBits;

    for( DWORD y=0; y<d3dsd.Height; y++ )
    {
        BYTE* pDstT  = pDst;
        BYTE* pSrcB0 = (BYTE*)pSrc;
        BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
        BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

        if( y == d3dsd.Height-1 )  // Don't go past the last line
            pSrcB1 = pSrcB0;
        if( y == 0 )               // Don't go before first line
            pSrcB2 = pSrcB0;

        for( DWORD x=0; x<d3dsd.Width; x++ )
        {
            LONG v00 = 256-*(pSrcB0+0); // Get the current pixel
            LONG v01 = 256-*(pSrcB0+4); // and the pixel to the right
            LONG vM1 = 256-*(pSrcB0-4); // and the pixel to the left
            LONG v10 = 256-*(pSrcB1+0); // and the pixel one line below.
            LONG v1M = 256-*(pSrcB2+0); // and the pixel one line above.

            LONG iDu = (vM1-v01); // The delta-u bump value
            LONG iDv = (v1M-v10); // The delta-v bump value

            if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
            {
                iDu = vM1-v00;                 // Choose greater of 1st order diffs
                if( iDu < v00-v01 )
                    iDu = v00-v01;
            }

            // The luminance bump value (land masses are less shiny)
            WORD uL = ( v00>1 ) ? 63 : 127;

            switch( D3DFMT_V8U8)//m_BumpMapFormat )
            {
                case D3DFMT_V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    break;

                case D3DFMT_L6V5U5:
                    *(WORD*)pDstT  = (WORD)( ( (iDu>>3) & 0x1f ) <<  0 );
                    *(WORD*)pDstT |= (WORD)( ( (iDv>>3) & 0x1f ) <<  5 );
                    *(WORD*)pDstT |= (WORD)( ( ( uL>>2) & 0x3f ) << 10 );
                    pDstT += 2;
                    break;

                case D3DFMT_X8L8V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    *pDstT++ = (BYTE)uL;
                    *pDstT++ = (BYTE)0L;
                    break;
            }

            // Move one pixel to the left (src is 32-bpp)
            pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
        }

        // Move to the next line
        pSrc += dwSrcPitch;    pDst += dwDstPitch;
    }

    m_pBumpTexture[i]->UnlockRect(0);
    surf->Unlock();
#endif

    return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D vertex buffer with water surface vertices */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::generateVertexBuffer( Int sizeX, Int sizeY, Int vertexSize, Bool doStatic)
{
	m_numVertices=sizeX*sizeY;
	//Assuming dynamic vertex buffer, allocate maximum multiple of required size to allow rendering from
	//different parts of the buffer. 5-15-03: Disabled this since we use DISCARD mode instead to avoid Nvidia Runtime bug. -MW
	//m_numVertices=(65536 / (sizeX*sizeY))*sizeX*sizeY;

	SEA_PATCH_VERTEX* pVertices;

	Setting *setting=&m_settings[m_tod];

	HRESULT hr;

	//default setting for a dynamic vertex buffer
	D3DPOOL pool = D3DPOOL_DEFAULT;
	DWORD usage = D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC;
	DWORD fvf = WATER_MESH_FVF;

	if (doStatic)
	{
		//change settings for a static vertex buffer
		pool = DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, nullptr);	// Ronin @feature 08/11/2026 DX9Ex: default pool on 9Ex
		usage = D3DUSAGE_WRITEONLY;
		fvf=0;// DX8 Docs confusing on this. Say no FVF for vertex shaders. Else DX8_FVF_XYZDUV1;
		m_numVertices=sizeX*sizeY;
	}

	if (m_vertexBufferD3D == nullptr)
	{
		// Create vertex buffer

		if (FAILED(hr=m_pDev->CreateVertexBuffer
		(
			m_numVertices*vertexSize,
			usage,
			fvf,
			pool,
			&m_vertexBufferD3D,
			NULL
		)))
			return hr;
	}

	m_vertexBufferD3DOffset=0;

	if (!doStatic)
		return S_OK;	//only create the buffer, other code will fill it.

	// load results into buffer
	if (FAILED(hr=m_vertexBufferD3D->Lock
	(
		0,
		m_numVertices*sizeof(SEA_PATCH_VERTEX),
		(void**)&pVertices,
		0//D3DLOCK_DISCARD
	)))
		return hr;

	Int x,z;
	for (z=0; z<sizeY; z++)
	{
		for (x=0; x<sizeX; x++)
		{
			pVertices->x=(float)x;
			pVertices->y=m_level;
			pVertices->z=(float)z;

			pVertices->tu=(float)x*PATCH_UV_SCALE;
			pVertices->tv=(float)z*PATCH_UV_SCALE;
			pVertices->c=setting->transparentWaterDiffuse;	//vertex alpha/color
			pVertices++;
		}
	}

	if (FAILED(hr=m_vertexBufferD3D->Unlock())) return hr;

	return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D index buffer with water surface strip indices */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::generateIndexBuffer(Int sizeX, Int sizeY)
{
	HRESULT hr;

	//Will need SizeY-1 strips, each of length SizeX*2 (2 indices per strip segment).
	//Will also need 2 extra indices to connect each strip to next one (except last strip)
	//Total index buffer size = (SizeY-1)*(SizeX*2+2) - 2 (drop the extra 2 indices from last strip)

	m_numIndices=(sizeY-1)*(sizeX*2+2) - 2;

	//old way

	// Create index buffer
	WORD* pIndices;

	if (FAILED(hr=m_pDev->CreateIndexBuffer
	(
		(m_numIndices+2)*sizeof(WORD),
		D3DUSAGE_WRITEONLY,
		D3DFMT_INDEX16,
		DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, nullptr),	// Ronin @feature 08/11/2026 DX9Ex: default pool on 9Ex
		&m_indexBufferD3D,
		NULL
	)))
		return hr;

	if (FAILED(hr=m_indexBufferD3D->Lock
	(
		0,
		m_numIndices*sizeof(WORD),
		(void**)&pIndices,
		0
	)))
		return hr;

	Int i,j,k;

	for (i=0,j=0,k=0; i<m_numIndices; j++)
	{
		for (;k<(sizeX*(j+1)); k++,i+=2)
		{
			pIndices[i]=(UnsignedShort) k+sizeX;
			pIndices[i+1]=(UnsignedShort) k;
		}
		//Generate 4 degenerate triangle to connect current strip to next strip/row of map
		//To do this, we just repeat the last index of first strip and first index of new strip.
		//Any triangles with repeated vertices will be skipped during rendering.
		if (i<m_numIndices) //check if there is at least 1 more strip to go
		{
			pIndices[i]=k-1;
			pIndices[i+1]=k+sizeX;
			i+=2;
		}
	}

	/*Old way
	Int step=1;
	Int psize=(size-1)/step;

	m_numIndices=psize*((psize+1)*2)+(psize*2)-2;


	Int x,z,s_toggle=1;
	for (z=step; z<size; z+=step)
	{
		if (s_toggle)
		{
			for (x=0; x<(size-step); x+=step)
			{
				*pIndices++=(WORD)((z-0)*size+(x));
				*pIndices++=(WORD)((z-step)*size+(x));
			}
				*pIndices++=(WORD)((z-0)*size+(size-1));
			*pIndices++=(WORD)((z-step)*size+(size-1));
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-2];
			*pIndices++=pIndices[-1];
		}
		else
		{
			*pIndices++=(WORD)((z-step)*size+(size-1));
			*pIndices++=(WORD)((z-0)*size+(size-1));
			for (x=size-1; x>0; x-=step)
			{
				*pIndices++=(WORD)((z-step)*size+(x-step));
				*pIndices++=(WORD)((z-0)*size+(x-step));
			}
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-1];
			*pIndices++=pIndices[-1];
		}

		s_toggle=!s_toggle;
	}
*/
	if (FAILED(hr=m_indexBufferD3D->Unlock())) return hr;

	return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Releases all w3d assets, to prepare for Reset device call. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReleaseResources()
{

	REF_PTR_RELEASE(m_indexBuffer);

	REF_PTR_RELEASE(m_pReflectionTexture);
	REF_PTR_RELEASE(m_pReflectionDepth);	// Ronin @bugfix 07/10/2026 DX9: rebuilt by the next mirror
	REF_PTR_RELEASE(m_refractionTexture);	// Ronin @feature 29/09/2026 DX9: phase 3 - rebuilt on the next copy
	m_refractionW = m_refractionH = 0;
	m_refractionOK = FALSE;
	// Ronin @feature 03/10/2026 DX9: phase 5 - the wake target (rebuilt by the next renderWakes) and its shaders
	SAFE_RELEASE(m_wakeTexture);
	SAFE_RELEASE(m_wakeVS);
	SAFE_RELEASE(m_wakePS);
	m_wakeLive = FALSE;
	SAFE_RELEASE(m_vertexBufferD3D);
	SAFE_RELEASE(m_indexBufferD3D);

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReleaseResources();

	if (m_dwWavePixelShader) { m_dwWavePixelShader->Release(); m_dwWavePixelShader = nullptr; }
	// Ronin @feature 02/10/2026 DX9: phase 4 - the WaterSea variants
	if (m_wavePSViews) { m_wavePSViews->Release(); m_wavePSViews = nullptr; }
	if (m_wavePSRiver) { m_wavePSRiver->Release(); m_wavePSRiver = nullptr; }
	if (m_wavePSRiverViews) { m_wavePSRiverViews->Release(); m_wavePSRiverViews = nullptr; }

	if (m_dwWaveVertexShader) { m_dwWaveVertexShader->Release(); m_dwWaveVertexShader = nullptr; }

	if (m_waterPixelShader) { m_waterPixelShader->Release(); m_waterPixelShader = nullptr; }

	if (m_trapezoidWaterPixelShader) { m_trapezoidWaterPixelShader->Release(); m_trapezoidWaterPixelShader = nullptr; }

	if (m_riverWaterPixelShader) { m_riverWaterPixelShader->Release(); m_riverWaterPixelShader = nullptr; }

	m_dwWavePixelShader=0;
	m_dwWaveVertexShader=0;
	m_waterPixelShader = 0;
	m_trapezoidWaterPixelShader=0;
	m_riverWaterPixelShader=0;
	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - "not-tried" until ReAcquireResources reaches the shaders again.
	TheWaterStats.flatPSStage  = WaterDebugStats::PS_NOT_TRIED;
	TheWaterStats.riverPSStage = WaterDebugStats::PS_NOT_TRIED;
	TheWaterStats.seaVSStage   = WaterDebugStats::PS_NOT_TRIED;
	TheWaterStats.seaPSStage   = WaterDebugStats::PS_NOT_TRIED;
}

//-------------------------------------------------------------------------------------------------
/** (Re)allocates all W3D assets after a reset.. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReAcquireResources()
{
	HRESULT hr;

	m_indexBuffer=NEW_REF(DX8IndexBufferClass,(6));
	// Fill up the IB
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 2 triangles:
		//	3-----2
		//  |    /|
		//  |  /  |
		//	|/    |
		//  0-----1
		ib[0]=3;
		ib[1]=0;
		ib[2]=2;
		ib[3]=2;
		ib[4]=0;
		ib[5]=1;
	}

	m_pDev=DX8Wrapper::_Get_D3D_Device8();

	//We're using the same grid for either 3D Water Mesh or Pixel/Vertex shader.  Just
	//allocate the right size depending on usage
	if (m_meshData)
	{
		//Create new grid data
		if (FAILED(generateIndexBuffer(m_gridCellsX+1,m_gridCellsY+1)))
			return;
		if (FAILED(generateVertexBuffer(m_gridCellsX+1,m_gridCellsY+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
	else
	if (m_waterType == WATER_TYPE_2_PVSHADER)
	{
		//pixel/vertex shader based water assets.
		if (FAILED(hr=generateIndexBuffer(PATCH_SIZE,PATCH_SIZE)))
			return;

		if (FAILED(hr=generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true)))
			return;

		//shader decleration
		static const D3DVERTEXELEMENT9 Declaration[] =
		{
			{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
			{0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			// Ronin @feature 02/10/2026 DX9: phase 4 - WaterSea_vs reads NORMAL0 (a river's flow); the sea plane has none, so it
			// aliases the uv. Read only for rivers (c8.w), never here.
			{0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
			D3DDECL_END()
		};

		// Ronin @feature 28/09/2026 DX9: SM3 ports of wave.pso/.vso (WaterSea_ps/_vs.hlsl) - DX9 refuses the DX8 vs.1.1.
		// A failure now skips only the sea: it used to return here and skip the rest of ReAcquireResources too.
		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterSea.pso", Declaration, 0, false, (void**)&m_dwWavePixelShader);
		TheWaterStats.seaPSStage = SUCCEEDED(hr) ? WaterDebugStats::PS_OK : WaterDebugStats::PS_CREATE_FAILED;
		TheWaterStats.seaPSHr = hr;
		// Ronin @feature 02/10/2026 DX9: phase 4 - the variants (WaterSea_ps.hlsl's header). Optional: a missing one leaves
		// its handle null and pickWavePS uses the next best; [WATER] psv counts the ones loaded.
		TheWaterStats.psVariants = SUCCEEDED(hr) ? 1 : 0;
		if (SUCCEEDED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterSeaViews.pso", Declaration, 0, false, (void**)&m_wavePSViews)) && m_wavePSViews)
			TheWaterStats.psVariants++;
		if (SUCCEEDED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterSeaRiver.pso", Declaration, 0, false, (void**)&m_wavePSRiver)) && m_wavePSRiver)
			TheWaterStats.psVariants++;
		if (SUCCEEDED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterSeaRiverViews.pso", Declaration, 0, false, (void**)&m_wavePSRiverViews)) && m_wavePSRiverViews)
			TheWaterStats.psVariants++;

		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterSea.vso", Declaration, 0, true, (void**)&m_dwWaveVertexShader);
		TheWaterStats.seaVSStage = SUCCEEDED(hr) ? WaterDebugStats::PS_OK : WaterDebugStats::PS_CREATE_FAILED;
		TheWaterStats.seaVSHr = hr;

		// Ronin @feature 03/10/2026 DX9: phase 5 - wakes: the pair that draws a trail into the wake texture. Optional: without
		// them no wake is drawn and the vanilla sprites stay ([WATER] wake=off).
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterWake.vso", Declaration, 0, true, (void**)&m_wakeVS)))
			m_wakeVS = nullptr;
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\WaterWake.pso", Declaration, 0, false, (void**)&m_wakePS)))
			m_wakePS = nullptr;

		// Create reflection texture
		if (m_dwWavePixelShader && m_dwWaveVertexShader)
			m_pReflectionTexture = DX8Wrapper::Create_Render_Target (SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE);
	}

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReAcquireResources();

	if (W3DShaderManager::getChipset() >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		static const DWORD riverWaterShader[] =
		{
			D3DPS_VERSION(1, 1),
			0x00000042, 0xb00f0000,                         // tex t0
			0x00000042, 0xb00f0001,                         // tex t1
			0x00000042, 0xb00f0002,                         // tex t2
			0x00000042, 0xb00f0003,                         // tex t3
			0x00000005, 0x80070000, 0x90e40000, 0xb0e40000, // mul r0.rgb, v0, t0 ; blend vertex color into t0.
			0x00000001, 0x80080000, 0xb0e40000,             // mov r0.a, t0 ; keep vertex alpha from fading the base water.
			0x00000005, 0x800f0001, 0xb0e40001, 0xb0e40002, // mul r1, t1, t2
			0x00000002, 0x80070001, 0x80e40001, 0xb0e40003, // add r1.rgb, r1, t3
			0x00000005, 0x80070001, 0x80e40001, 0x90ff0000, // mul r1.rgb, r1, v0.a
			0x40000005, 0x80080000, 0x80e40000, 0xb0e40003, // +mul r0.a, r0, t3
			0x00000002, 0x80070000, 0x80e40000, 0x80e40001, // add r0.rgb, r0, r1
			D3DPS_END(),
		};
		hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(riverWaterShader, &m_riverWaterPixelShader);
		// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - whether the shader was created.
		TheWaterStats.riverPSStage = SUCCEEDED(hr) ? WaterDebugStats::PS_OK : WaterDebugStats::PS_CREATE_FAILED;
		TheWaterStats.riverPSHr = hr;

		static const DWORD waterShader[] =
		{
			D3DPS_VERSION(1, 1),
			0x00000042, 0xb00f0000,                         // tex t0
			0x00000042, 0xb00f0001,                         // tex t1
			0x00000043, 0xb00f0002, 0xb0e40001,             // texbem t2, t1 ; use t1 as env map adjustment on t2.
			0x00000005, 0x800f0000, 0x90e40000, 0xb0e40000, // mul r0, v0, t0 ; blend vertex color into t0.
			0x00000005, 0x80070001, 0xb0e40002, 0xa0e40000, // mul r1.rgb, t2, c0 ; reduce t2 (environment mapped reflection) by constant
			0x00000002, 0x80070000, 0x80e40000, 0x80e40001, // add r0.rgb, r0, r1
			D3DPS_END(),
		};
		hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(waterShader, &m_waterPixelShader);

		static const DWORD trapezoidWaterShader[] =
		{
			D3DPS_VERSION(1, 1),
			0x00000042, 0xb00f0000,                                     // tex t0 ; get water texture
			0x00000042, 0xb00f0001,                                     // tex t1 ; get white highlights on black background
			0x00000042, 0xb00f0002,                                     // tex t2 ; get white highlights with more tiling
			0x00000042, 0xb00f0003,                                     // tex t3 ; get black shroud
			0x00000005, 0x800f0000, 0x90e40000, 0xb0e40000,             // mul r0, v0, t0 ; blend vertex color and alpha into base texture.
			0x00000004, 0x80070000, 0xb0e40001, 0xb0e40002, 0x80e40000, // mad r0.rgb, t1, t2, r0 ; blend sparkles and noise
			0x00000005, 0x80070000, 0x80e40000, 0xb0e40003,             // mul r0.rgb, r0, t3 ; blend in black shroud
			D3DPS_END(),
		};
		hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(trapezoidWaterShader, &m_trapezoidWaterPixelShader);
		// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - whether the shader was created.
		TheWaterStats.flatPSStage = SUCCEEDED(hr) ? WaterDebugStats::PS_OK : WaterDebugStats::PS_CREATE_FAILED;
		TheWaterStats.flatPSHr = hr;
	}

	//W3D Invalidate textures after losing the device and since we peek at the textures directly, it won't
	//know to reinit them for us.  Do it here manually:
	if (m_riverTexture && !m_riverTexture->Is_Initialized())
		m_riverTexture->Init();
	if (m_waterNoiseTexture && !m_waterNoiseTexture->Is_Initialized())
		m_waterNoiseTexture->Init();
	if (m_riverAlphaEdge && !m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	if (m_waterSparklesTexture && !m_waterSparklesTexture->Is_Initialized())
		m_waterSparklesTexture->Init();
	if (m_whiteTexture && !m_whiteTexture->Is_Initialized())
	{	m_whiteTexture->Init();
		SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
		int pitch;
		void *pBits = surface->Lock(&pitch);
		const unsigned int bytesPerPixel = surface->Get_Bytes_Per_Pixel();
		surface->Draw_Pixel(0, 0, 0xffffffff, bytesPerPixel, pBits, pitch);
		surface->Unlock();
		REF_PTR_RELEASE(surface);
	}
}

void WaterRenderObjClass::load()
{
	if (m_waterTrackSystem)
		m_waterTrackSystem->loadTracks();

	if (m_waterType == WATER_TYPE_2_PVSHADER)
		loadSeaMap();
}

// Ronin @feature 30/10/2026 DX9: the new water on / off while running: the device-reset path with the other type, then what
// init and load() add for type 2. Back to 0: GameData's level, and the depths freed.
void WaterRenderObjClass::setWaterType(WaterType type)
{
	if (type == m_waterType)
		return;
	ReleaseResources();
	m_waterType = type;
	ReAcquireResources();
	if (type == WATER_TYPE_2_PVSHADER)
	{
		loadSeaTextures();
		loadSeaMap();
	}
	else
	{
		m_level = m_planeDistance = TheGlobalData->m_waterPositionZ;
		SAFE_RELEASE(m_heightTexture);
		m_heightTexW = m_heightTexH = m_heightBorder = 0;
		m_heightVTF = FALSE;
	}
}

// Ronin @feature 28/09/2026 DX9: WaterType 2 port. The caust frames ship in the base Generals Textures.big; the causts set
// (m_pBumpTexture2) was never sampled, so it is not built. Each piece loads once (setWaterType can call this again).
void WaterRenderObjClass::loadSeaTextures()
{
	//geforce3 specific water requires some extra D3D assets
	m_pDev=DX8Wrapper::_Get_D3D_Device8();
	//save previous thumbnail mode
	bool thumbnails_enabled = WW3D::Get_Thumbnail_Enabled();
	WW3D::Set_Thumbnail_Enabled(false);

	//load bump map textures off disk
	TextureClass *pBumpSource;	//temporary textures in a format W3D understands
	Int i;
	i=NUM_BUMP_FRAMES;
	while (i--)
	{
		if (m_pBumpTexture[i] != nullptr)
			continue;
		char bump_name[128];

		sprintf(bump_name,"caust%.2d.tga",i);
		// Ronin @feature 28/09/2026 DX9: 32-bit, uncompressed, unreduced - initBumpMap reads 4 bytes a pixel or skips.
		pBumpSource=WW3DAssetManager::Get_Instance()->Get_Texture(bump_name, MIP_LEVELS_ALL, WW3D_FORMAT_A8R8G8B8, false,
			TextureBaseClass::TEX_REGULAR, false);
		if (pBumpSource == nullptr)
			continue;
		if (!pBumpSource->Is_Initialized())
			pBumpSource->Init();	// textures load lazily; initBumpMap reads the D3D texture now
		initBumpMap(m_pBumpTexture+i, pBumpSource);
		WW3DAssetManager::Get_Instance()->Release_Texture(pBumpSource);
		REF_PTR_RELEASE(pBumpSource);
	}
	//restore previous thumpnail mode
	WW3D::Set_Thumbnail_Enabled(thumbnails_enabled);

	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - how many frames initBumpMap actually built.
	TheWaterStats.bumpFrames = 0;
	for (i = 0; i < NUM_BUMP_FRAMES; i++)
		if (m_pBumpTexture[i] != nullptr)
			TheWaterStats.bumpFrames++;

	// Ronin @feature 28/09/2026 DX9: the WaterSea surface normals (scripts/water/GenWaterNormal.ps1, deployed to
	// Art/Textures). Uncompressed: DXT would band the normals.
	if (m_waterNormalTexture == nullptr)
		m_waterNormalTexture = WW3DAssetManager::Get_Instance()->Get_Texture("WaterNormal.tga", MIP_LEVELS_ALL, WW3D_FORMAT_A8R8G8B8, false);
	if (m_waterNormalTexture != nullptr && !m_waterNormalTexture->Is_Initialized())
		m_waterNormalTexture->Init();
	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - 2 loaded, 1 missing (WW3D's placeholder), 0 none.
	TheWaterStats.normalMap = (m_waterNormalTexture == nullptr) ? 0 : (m_waterNormalTexture->Is_Missing_Texture() ? 1 : 2);
	// Ronin @feature 02/10/2026 DX9: phase 4 - the lake's calm ripples (GenWaterNormal.ps1, its lake recipe). Missing = the
	// lake takes the sea's map (bindWaterSea).
	if (m_waterNormalLake == nullptr)
		m_waterNormalLake = WW3DAssetManager::Get_Instance()->Get_Texture("WaterNormalLake.tga", MIP_LEVELS_ALL, WW3D_FORMAT_A8R8G8B8, false);
	if (m_waterNormalLake != nullptr && !m_waterNormalLake->Is_Initialized())
		m_waterNormalLake->Init();
	TheWaterStats.normalMapLake = (m_waterNormalLake == nullptr) ? 0 : (m_waterNormalLake->Is_Missing_Texture() ? 1 : 2);
	// Ronin @feature 02/10/2026 DX9: phase 4 - the river's streaks (GenWaterNormal.ps1, its river recipe), in river space.
	if (m_waterNormalRiver == nullptr)
		m_waterNormalRiver = WW3DAssetManager::Get_Instance()->Get_Texture("WaterNormalRiver.tga", MIP_LEVELS_ALL, WW3D_FORMAT_A8R8G8B8, false);
	if (m_waterNormalRiver != nullptr && !m_waterNormalRiver->Is_Initialized())
		m_waterNormalRiver->Init();
	TheWaterStats.normalMapRiver = (m_waterNormalRiver == nullptr) ? 0 : (m_waterNormalRiver->Is_Missing_Texture() ? 1 : 2);
}

// Ronin @feature 28/09/2026 DX9: WaterType 2 - the mirror plane sits at the map's own water height (the largest standing
// area, else the largest river; point 0's z as the logic reads it), not GameData's WaterPositionZ.
void WaterRenderObjClass::loadSeaMap()
{
	Real bestArea[2]  = { 0.0f, 0.0f };	// [0] standing water, [1] rivers
	Real bestLevel[2] = { 0.0f, 0.0f };
	for (PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext())
	{
		if (!pTrig->isWaterArea() || pTrig->getNumPoints() < 3)
			continue;
		Real area = 0.0f;	// shoelace
		for (Int k = 0; k < pTrig->getNumPoints(); k++)
		{
			const ICoord3D *a = pTrig->getPoint(k);
			const ICoord3D *b = pTrig->getPoint((k + 1) % pTrig->getNumPoints());
			area += (Real)a->x * (Real)b->y - (Real)b->x * (Real)a->y;
		}
		if (area < 0.0f)
			area = -area;
		const Int kind = pTrig->isRiver() ? 1 : 0;
		if (area > bestArea[kind])
		{
			bestArea[kind]  = area;
			bestLevel[kind] = (Real)pTrig->getPoint(0)->z;
		}
	}
	const Int  pick    = (bestArea[0] > 0.0f) ? 0 : 1;
	const Bool fromMap = (bestArea[pick] > 0.0f);
	const Real level   = fromMap ? bestLevel[pick] : TheGlobalData->m_waterPositionZ;

	m_level = level;
	m_planeDistance = level;
	generateVertexBuffer(PATCH_SIZE, PATCH_SIZE, sizeof(SEA_PATCH_VERTEX), true);	// the patch vertices carry the height
	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - the level used and where it came from.
	TheWaterStats.seaLevel = level;
	TheWaterStats.seaLevelFromMap = fromMap;

	buildHeightTexture();	// Ronin @feature 29/09/2026 DX9: phase 1 - depth under the water
}

//-------------------------------------------------------------------------------------------------
/** Initializes water with dimensions and parent scene.
	* During rendering, we will render a water surface of given dimensions
	* and reflect the parent scene in its surface.  For now, waters are
	* forced to be rectangles. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::init(Real waterLevel, Real dx, Real dy, SceneClass *parentScene, WaterType type)
{
	// Ronin @feature 04/10/2026 DX9: the advanced look, over the built-in values, when the player keeps a water ini
	WaterSea_LoadIni(WaterSea_IniPath().str(), nullptr);
	// Ronin @feature 04/10/2026 DX9: reflections on / off is the player's: Options.ini DX9WaterReflections
	if (TheGlobalData != nullptr)
		TheWaterSeaGlobal.mirror = TheGlobalData->m_waterReflections;

	m_fBumpFrame=0;
	m_fBumpScale=SEA_BUMP_SCALE;

	m_dx=dx;
	m_dy=dy;
	m_level=waterLevel;

	m_LastUpdateTime=timeGetTime();
	m_uScrollPerMs=0.001f;
	m_vScrollPerMs=0.001f;
	m_uOffset=0;
	m_vOffset=0;

	m_parentScene=parentScene;
	m_waterType = type;

	/// Hack for now
	//m_waterType = WATER_TYPE_0_TRANSLUCENT;

	///@todo: calculate a real normal/distance for arbitrary planes.
	m_planeNormal=Vector3(0,0,1);		//water plane normal
	m_planeDistance=m_level;	//water plane distance(always at zero for now)

	m_meshLight=NEW_REF(LightClass,(LightClass::DIRECTIONAL));
	m_meshLight->Set_Ambient(Vector3(0.1f,0.1f,0.1f));
	m_meshLight->Set_Diffuse(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Specular(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Position(Vector3(1000,1000,1000));
	//testLight->Set_Spot_Direction(Vector3(TheGlobalData->m_terrainLightX,TheGlobalData->m_terrainLightY,TheGlobalData->m_terrainLightZ));
	m_meshLight->Set_Spot_Direction(Vector3(-0.57f,-0.57f,-0.57f));

	//Setup material for 3D Mesh water.
	m_meshVertexMaterialClass=NEW_REF(VertexMaterialClass,());
	m_meshVertexMaterialClass->Set_Shininess(20.0);
	m_meshVertexMaterialClass->Set_Ambient(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Diffuse(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Specular(0.5,0.5,0.5);
	m_meshVertexMaterialClass->Set_Opacity(WATER_MESH_OPACITY);
	m_meshVertexMaterialClass->Set_Lighting(true);

	//
	// assign the data from the WaterSettings[] global to the data for this
	// render object (we at present only have one water plane)
	//
	loadSetting( &m_settings[ TIME_OF_DAY_MORNING ], TIME_OF_DAY_MORNING );
	loadSetting( &m_settings[ TIME_OF_DAY_AFTERNOON ], TIME_OF_DAY_AFTERNOON );
	loadSetting( &m_settings[ TIME_OF_DAY_EVENING ], TIME_OF_DAY_EVENING );
	loadSetting( &m_settings[ TIME_OF_DAY_NIGHT ], TIME_OF_DAY_NIGHT );

	Set_Sort_Level(2);	//force water to be drawn after all other non translucent objects in scene.
	Set_Force_Visible(TRUE);	//water is always visible since it's a composite object made of multiple planes all over the map.

	ReAcquireResources();
	if (type == WATER_TYPE_2_PVSHADER)
		loadSeaTextures();

	//Setup material for regular water
	m_vertexMaterialClass=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);



	m_shaderClass = zFillAlphaShader;//ShaderClass::_PresetAlphaShader;ShaderClass::_PresetOpaqueShader;//detailOpaqueShader;
	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);	//water should be visible from both sides

	//Assets used for all types of water
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture(SKYBODY_TEXTURE);

#ifdef CLIP_GEOMETRY_TO_PLANE
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture("alphaclip.tga");
#endif

	m_skyBox = ((W3DAssetManager*)W3DAssetManager::Get_Instance())->Create_Render_Obj( "new_skybox", TheGlobalData->m_skyBoxScale, 0);

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=(MeshClass*) m_skyBox;
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}

		REF_PTR_RELEASE(material);
	}

	m_riverTexture=WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str());

	//For some reason setting a null texture does not result in 0xffffffff for pixel shaders so using explicit "white" texture.
	m_whiteTexture=MSGNEW("TextureClass") TextureClass(1,1,WW3D_FORMAT_A4R4G4B4,MIP_LEVELS_1);
	SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
	int pitch;
	void *pBits = surface->Lock(&pitch);
	const unsigned int bytesPerPixel = surface->Get_Bytes_Per_Pixel();
	surface->Draw_Pixel(0, 0, 0xffffffff, bytesPerPixel, pBits, pitch);
	surface->Unlock();
	REF_PTR_RELEASE(surface);

	m_waterNoiseTexture=WW3DAssetManager::Get_Instance()->Get_Texture("Noise0000.tga");
	m_riverAlphaEdge=WW3DAssetManager::Get_Instance()->Get_Texture("TWAlphaEdge.tga");
	m_waterSparklesTexture=WW3DAssetManager::Get_Instance()->Get_Texture("WaterSurfaceBubbles.tga");
#ifdef DRAW_WATER_WAKES
	m_waterTrackSystem = NEW WaterTracksRenderSystem;
	m_waterTrackSystem->init();
#endif

	return 0;
}

void WaterRenderObjClass::updateMapOverrides()
{
	if (m_riverTexture && TheWaterTransparency->m_standingWaterTexture.compareNoCase(m_riverTexture->Get_Texture_Name()) != 0)
	{
		REF_PTR_RELEASE(m_riverTexture);
		m_riverTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::reset()
{

	// for vertex animated water mesh reset the values
	if( m_meshData)
	{
		Int i, j;
		WaterMeshData *pData;
		Int	mx = m_gridCellsX + 1;
		Int my = m_gridCellsY + 1;

		// go through each mesh point and adjust the height according to the velocity
		for( j = 0, pData = m_meshData; j < (my + 2); j++ )
		{

			for( i = 0; i < (mx + 2); i++ )
			{

				// areset grid values for this cell
				pData->velocity = 0.0f;
				pData->height = 0.0f;
				pData->preferredHeight = 0.0f;
				pData->status = WaterRenderObjClass::AT_REST;

				// on to the next one
				pData++;

			}

		}

		// mesh data is no longer in motion
		m_meshInMotion = FALSE;

	}

	if (m_waterTrackSystem)
		m_waterTrackSystem->reset();
	clearWakes();	// Ronin @feature 03/10/2026 DX9: phase 5 - drawable ids start over
}

void WaterRenderObjClass::enableWaterGrid(Bool state)
{
	m_doWaterGrid = state;

	m_drawingRiver = false;
	m_disableRiver = false;

	if (state && m_meshData == nullptr)
	{
		//water type has changed, must allocate necessary assets for new water.
		//contains the current deformed water surface z(height) values.  With 1 vertex invisible border
		//around surface to speed up normal calculations.
		m_meshDataSize = (m_gridCellsX+1+2)*(m_gridCellsY+1+2);
		m_meshData=NEW WaterMeshData[ m_meshDataSize ];
		memset(m_meshData,0,sizeof(WaterMeshData)*(m_gridCellsX+1+2)*(m_gridCellsY+1+2));
		reset();

		//Release existing grid data
		SAFE_RELEASE(m_vertexBufferD3D);
		SAFE_RELEASE(m_indexBufferD3D);

		//Create new grid data
		if (FAILED(generateIndexBuffer(m_gridCellsX+1,m_gridCellsY+1)))
			return;
		if (FAILED(generateVertexBuffer(m_gridCellsX+1,m_gridCellsY+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
}

// ------------------------------------------------------------------------------------------------
/** Update phase for water if we need it. */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::update()
{
	// Ronin @feature 30/10/2026 DX9: the new water keeps Smooth Water Borders on, whichever preset or menu turned them off.
	if (m_waterType == WATER_TYPE_2_PVSHADER && TheWritableGlobalData != nullptr && !TheGlobalData->m_showSoftWaterEdge)
		TheWritableGlobalData->m_showSoftWaterEdge = TRUE;

	// TheSuperHackers @tweak The water movement time step is now decoupled from the render update.
	const Real timeScale = TheFramePacer->getActualLogicTimeScaleOverFpsRatio();

	{
		constexpr const Real MagicOffset = 0.0125f * 33 / 5000; ///< the work of top Munkees; do not question it

		m_riverVOrigin += 0.002f * timeScale;
		m_riverXOffset += (Real)(MagicOffset * timeScale);
		m_riverYOffset += (Real)(2 * MagicOffset * timeScale);

		// This moves offsets towards zero when smaller -1.0 or larger 1.0
		m_riverXOffset -= (Int)m_riverXOffset;
		m_riverYOffset -= (Int)m_riverYOffset;

		m_fBumpFrame += timeScale;
		if (m_fBumpFrame >= NUM_BUMP_FRAMES)
			m_fBumpFrame = 0.0f;

		// for vertex animated water we need to update the vector field
		if( m_doWaterGrid && m_meshInMotion == TRUE )
		{
			const Real PREFERRED_HEIGHT_FUDGE = 1.0f;		///< this is close enough to at rest
			const Real AT_REST_VELOCITY_FUDGE = 1.0f;		///< when we're close enough to at rest height and velocity we will stop
			const Real WATER_DAMPENING = 0.93f;					///< use with up force of 15.0
			Int i, j;
			Int	mx = m_gridCellsX+1;
			Int my = m_gridCellsY+1;
			WaterMeshData *pData;

			//
			// we will mark the mesh as clean now ... if any of the fields are still in motion
			// they will continue to mark the mesh as dirty so processing continues next frame
			//
			m_meshInMotion = FALSE;

			// go through each mesh point and adjust the height according to the velocity
			for( j = 0, pData = m_meshData; j < (my + 2); j++ )
			{

				for( i = 0; i < (mx + 2); i++ )
				{

					// only pay attention to mesh points that are in motion
					if( BitIsSet( pData->status, WaterRenderObjClass::IN_MOTION ) )
					{

						// DAMPENING to slow the changes down
						pData->velocity *= WATER_DAMPENING;

						// if the height here is below our preferred height, we want to add upward force to counteract it
						if( pData->height < pData->preferredHeight )
							pData->velocity -= TheGlobalData->m_gravity * 3.0f;
						else
							pData->velocity += TheGlobalData->m_gravity * 3.0f;

						// adjust the height at this grid location according to the current velocity
						pData->height = pData->height + pData->velocity;

						//
						// if we are close enough to our preferred height and our velocity is small enough
						// this will be our resting location
						//
						if( fabs( pData->height - pData->preferredHeight ) < PREFERRED_HEIGHT_FUDGE &&
								fabs( pData->velocity ) < AT_REST_VELOCITY_FUDGE )
						{

							BitClear( pData->status, WaterRenderObjClass::IN_MOTION );
							pData->height = pData->preferredHeight;
							pData->velocity = 0.0f;

						}
						else
						{

							// there is still motion in the mesh, we need to process next frame
							m_meshInMotion = TRUE;

						}

					}

					// on to the next one
					pData++;

				}

			}

		}

	}

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::replaceSkyboxTexture(const AsciiString& oldTexName, const AsciiString& newTextName)
{
	W3DAssetManager* assetManager = ((W3DAssetManager*)W3DAssetManager::Get_Instance());

	assetManager->replacePrototypeTexture(m_skyBox, oldTexName.str(), newTextName.str());

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=(MeshClass*) m_skyBox;
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** Adjusts various water/sky rendering settings that depend on time of day. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::setTimeOfDay(TimeOfDay tod)
{
	m_tod=tod;
	if (m_waterType == WATER_TYPE_2_PVSHADER)
		generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true);	//update the water mesh with new lighting/alpha
}

//-------------------------------------------------------------------------------------------------
/**Copies GDF settings dealing with a particular time of day into our own
	* structures.  Also allocates any required W3D assets (textures). */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::loadSetting( Setting *setting, TimeOfDay timeOfDay )
{
	SurfaceClass::SurfaceDescription surfaceDesc;

	// sanity
	DEBUG_ASSERTCRASH( setting, ("WaterRenderObjClass::loadSetting, null setting") );

	// textures
	setting->skyTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_skyTextureFile.str() );
	setting->waterTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_waterTextureFile.str() );

	// texelss per unit
	setting->skyTexelsPerUnit = WaterSettings[ timeOfDay ].m_skyTexelsPerUnit;
	setting->waterTexture->Get_Level_Description( surfaceDesc, 0 );
	setting->skyTexelsPerUnit /= (Real)surfaceDesc.Width;

	// water repeat
	setting->waterRepeatCount = WaterSettings[ timeOfDay ].m_waterRepeatCount;

	// U and V scroll per ms
	setting->uScrollPerMs = WaterSettings[ timeOfDay ].m_uScrollPerMs;
	setting->vScrollPerMs = WaterSettings[ timeOfDay ].m_vScrollPerMs;

	//
	// vertex colors
	//
	// bottom left
	setting->vertex00Diffuse = (WaterSettings[ timeOfDay ].m_vertex00Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex00Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex00Diffuse.blue;
	// top left
	setting->vertex01Diffuse = (WaterSettings[ timeOfDay ].m_vertex01Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex01Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex01Diffuse.blue;
	// bottom right
	setting->vertex10Diffuse = (WaterSettings[ timeOfDay ].m_vertex10Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex10Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex10Diffuse.blue;
	// top right
	setting->vertex11Diffuse = (WaterSettings[ timeOfDay ].m_vertex11Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex11Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex11Diffuse.blue;

	// diffuse water color
	setting->waterDiffuse = (WaterSettings[ timeOfDay ].m_waterDiffuseColor.alpha << 24) |
												  (WaterSettings[ timeOfDay ].m_waterDiffuseColor.red		<< 16) |
													(WaterSettings[ timeOfDay ].m_waterDiffuseColor.green << 8) |
												   WaterSettings[ timeOfDay ].m_waterDiffuseColor.blue;

	// transparent water color
	setting->transparentWaterDiffuse = (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.alpha << 24) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.red	 << 16) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.green << 8) |
																		  WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.blue;

}

// Ronin @bugfix 15/11/2026 DX9: is any of the map's water in view - each water polygon's bounding rectangle at its height,
// clipped as getClippedWaterPlane clips GameData's square. 1 in view, 0 not, -1 the map has no water polygons.
static Int waterAreasInView(CameraClass *cam)
{
	const FrustumClass &frustum = cam->Get_Frustum();
	Int result = -1;
	for (PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext())
	{
		if (!pTrig->isWaterArea() || pTrig->getNumPoints() < 3)
			continue;
		result = 0;
		const ICoord3D *p0 = pTrig->getPoint(0);
		Real x0 = (Real)p0->x, x1 = x0, y0 = (Real)p0->y, y1 = y0;
		for (Int k = 1; k < pTrig->getNumPoints(); k++)
		{
			const ICoord3D *p = pTrig->getPoint(k);
			if ((Real)p->x < x0) x0 = (Real)p->x;
			if ((Real)p->x > x1) x1 = (Real)p->x;
			if ((Real)p->y < y0) y0 = (Real)p->y;
			if ((Real)p->y > y1) y1 = (Real)p->y;
		}
		ClipPolyClass a, b;
		a.Reset();
		a.Add_Vertex(Vector3(x0, y0, (Real)p0->z));
		a.Add_Vertex(Vector3(x0, y1, (Real)p0->z));
		a.Add_Vertex(Vector3(x1, y1, (Real)p0->z));
		a.Add_Vertex(Vector3(x1, y0, (Real)p0->z));
		a.Clip(frustum.Planes[0], b);
		b.Clip(frustum.Planes[1], a);
		a.Clip(frustum.Planes[2], b);
		b.Clip(frustum.Planes[3], a);
		a.Clip(frustum.Planes[4], b);
		b.Clip(frustum.Planes[5], a);
		if (a.Verts.Count() >= 3)
			return 1;
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
/** Our water may use effects that require run-time rendered textures.  These
	*	textures need to be updated before we start rendering to the main screen
	* render target because D3D doesn't multiple render targets. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::updateRenderTargetTextures(CameraClass *cam)
{
	// Ronin @bugfix 01/10/2026 DX9: no mirror without the sea shaders or with `water mirror 0`; its plane is published for
	// the tree cull (R3DWaterMirror.h).
	TheWaterMirror.active = FALSE;
	// Ronin @bugfix 15/11/2026 DX9: the map's water decides, not GameData's square (WaterExtentX/Y, 2000 shipped): past it the
	// mirror stopped while WaterType 2's polygons kept sampling its last image. A map with no water polygons keeps the square.
	const Int areas = waterAreasInView(cam);
	if (m_waterType == WATER_TYPE_2_PVSHADER && m_pReflectionTexture && TheWaterSeaGlobal.mirror &&
		(areas > 0 || (areas < 0 && getClippedWaterPlane(cam, nullptr))) && TheTerrainRenderObject && TheTerrainRenderObject->getMap())
	{
		TheWaterMirror.active = TRUE;
		TheWaterMirror.level  = m_level;
		ensureReflectionTarget();
		ensureReflectionDepth();
		renderMirror(cam);	//generate texture containing reflected scene
	}
}

// Ronin @diagnostic 29/09/2026 DX9: the mirror's size - 256 x 256 (the original, whatever the screen's shape), or the screen's
// size / `mirrorres`. Never larger: without its own depth (07/10) it borrows the screen's. Recreated only on a size change.
void WaterRenderObjClass::ensureReflectionTarget()
{
	Int w = SEA_REFLECTION_SIZE;
	Int h = SEA_REFLECTION_SIZE;
	const Int div = TheWaterSeaGlobal.mirrorRes;
	if (div > 0)
	{
		int sw = 0, sh = 0, bits = 0;
		bool windowed = false;
		WW3D::Get_Device_Resolution(sw, sh, bits, windowed);
		w = (sw / div > 16) ? sw / div : 16;
		h = (sh / div > 16) ? sh / div : 16;
		// Ronin @tweak 30/09/2026 DX9: at most 1024 wide - its pixels are the cost, and a 4K screen at /2 would be 1920
		// Ronin @tweak 08/10/2026 DX9: not at `mirrorres 1` - the full size is asked for there, for `mirrorsoft` to average.
		if (div > 1 && w > 1024)
		{
			h = (h * 1024) / w;
			w = 1024;
		}
	}
	D3DSURFACE_DESC desc;
	IDirect3DTexture9 *cur = m_pReflectionTexture->Peek_D3D_Texture();
	if (cur != nullptr && SUCCEEDED(cur->GetLevelDesc(0, &desc)) && (Int)desc.Width == w && (Int)desc.Height == h)
	{
		TheWaterStats.mirrorW = w;
		TheWaterStats.mirrorH = h;
		return;
	}
	TextureClass *tex = (div > 0)
		? NEW_REF(TextureClass, (w, h, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1, TextureClass::POOL_DEFAULT, true))
		: DX8Wrapper::Create_Render_Target(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE);
	if (tex == nullptr)
		return;
	if (tex->Peek_D3D_Base_Texture() == nullptr)
	{
		REF_PTR_RELEASE(tex);	// a macro with its own if - keep the braces
		return;
	}
	REF_PTR_RELEASE(m_pReflectionTexture);
	m_pReflectionTexture = tex;
	TheWaterStats.mirrorW = w;
	TheWaterStats.mirrorH = h;
}

// Ronin @bugfix 07/10/2026 DX9: the mirror's own depth-stencil, the size of its colour target; remade when that changes.
// Without one (no depth textures on the card) the mirror borrows the screen's depth as before, which MSAA breaks.
void WaterRenderObjClass::ensureReflectionDepth()
{
	D3DSURFACE_DESC cd;
	IDirect3DTexture9 *colour = (m_pReflectionTexture != nullptr) ? m_pReflectionTexture->Peek_D3D_Texture() : nullptr;
	if (colour == nullptr || FAILED(colour->GetLevelDesc(0, &cd)))
	{
		REF_PTR_RELEASE(m_pReflectionDepth);
		return;
	}
	if (m_pReflectionDepth != nullptr && m_pReflectionDepth->Peek_D3D_Base_Texture() != nullptr &&
		(UINT)m_pReflectionDepth->Get_Width() == cd.Width && (UINT)m_pReflectionDepth->Get_Height() == cd.Height)
		return;

	REF_PTR_RELEASE(m_pReflectionDepth);
	m_pReflectionDepth = NEW_REF(ZTextureClass, (cd.Width, cd.Height, WW3D_ZFORMAT_D24S8, MIP_LEVELS_1, TextureClass::POOL_DEFAULT));
	if (m_pReflectionDepth->Peek_D3D_Base_Texture() == nullptr)
	{
		REF_PTR_RELEASE(m_pReflectionDepth);	// a macro with its own if - keep the braces
	}
}

//-------------------------------------------------------------------------------------------------
/** Reflects a point or direction across the horizontal plane at the given height. */
//-------------------------------------------------------------------------------------------------
static Vector3 reflectAcrossWaterPlane(const Vector3& point, Real planeHeight)
{
	const Vector3 waterNormal(0.0f, 0.0f, 1.0f);
	const Real distance = Vector3::Dot_Product(point, waterNormal) - planeHeight;
	return point - (2.0f * distance * waterNormal);
}

//-------------------------------------------------------------------------------------------------
/** Reflects a camera transform across the water plane at the given height. */
//-------------------------------------------------------------------------------------------------
static Matrix3D reflectCameraTransform(const Matrix3D& transform, Real waterLevel)
{
	// The axes are directions, so they reflect across the plane through the origin.
	return Matrix3D(
		reflectAcrossWaterPlane(transform.Get_X_Vector(), 0.0f),
		reflectAcrossWaterPlane(transform.Get_Y_Vector(), 0.0f),
		reflectAcrossWaterPlane(transform.Get_Z_Vector(), 0.0f),
		reflectAcrossWaterPlane(transform.Get_Translation(), waterLevel));
}

//-------------------------------------------------------------------------------------------------
/** Renders the reflected scene into an offscreen texture. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderMirror(CameraClass *cam)
{
#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableWater) {
		return;
	}
#endif
	Matrix3D	OldCameraMatrix=cam->Get_Transform();


	// Ronin @bugfix 07/10/2026 DX9: with its own depth - the screen's is multisampled under MSAA and cannot pair with this.
	DX8Wrapper::Set_Render_Target_With_Z((TextureClass*)m_pReflectionTexture, m_pReflectionDepth);

	// Clear the backbuffer
	if (WW3D::Begin_Render(false,true,Vector3(0.0f,0.0f,0.0f)) == WW3D_ERROR_OK)	//clearing only z-buffer since background always filled with clouds
	{
		Matrix3D	reflectedTransform(reflectCameraTransform(OldCameraMatrix,m_level));
		cam->Set_Transform( reflectedTransform );

		//Force reflected image to be drawn into full texture size - not a viewport inside texture.
		Vector2 vMin,vMax,vOldMax,vOldMin;
		cam->Get_Viewport(vOldMin,vOldMax);
		vMax.X=vMax.Y=1.0f;
		vMin.X=vMin.Y=0.0f;
		cam->Set_Viewport(vMin,vMax);

		// Ronin @bugfix 29/09/2026 DX9: clip the mirror at the water. Nothing did, so the seabed seen from below covered the sky and
		// everything above it. Kept side above the plane; CameraClass::Apply bends the near plane onto it. `water mirrorclip 0` = old.
		const Vector4 waterPlane(0.0f, 0.0f, 1.0f, -m_level);
		cam->Set_Oblique_Near_Plane(TheWaterSeaGlobal.mirrorClip ? &waterPlane : nullptr);
		// Ronin @bugfix 29/09/2026 DX9: `water mirrorfar` (x4): the view's far plane is fitted to what it shows, and the mirror
		// camera sits further from all it reflects (Water_Work.md §6).
		float mirrorNear = 0.0f, mirrorFar = 0.0f;
		cam->Get_Clip_Planes(mirrorNear, mirrorFar);
		cam->Set_Clip_Planes(mirrorNear, mirrorFar * TheWaterSeaGlobal.mirrorFar);

		cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

		//flip the winding order of polygons to draw the reflected back sides.
		ShaderClass::Invert_Backface_Culling(true);

		// Render the scene
		renderSky();
		if (m_tod == TIME_OF_DAY_NIGHT)
			renderSkyBody(&reflectedTransform);

		WW3D::Render(m_parentScene,cam);

		cam->Set_Transform(OldCameraMatrix);	//restore original non-reflected matrix
		cam->Set_Viewport(vOldMin,vOldMax);

		cam->Set_Oblique_Near_Plane(nullptr);	// Ronin @bugfix 29/09/2026 DX9: the main view keeps its own near plane
		cam->Set_Clip_Planes(mirrorNear, mirrorFar);	// and its own far plane

		cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

		ShaderClass::Invert_Backface_Culling(false);

		WW3D::End_Render(false);
	}

	// Change the rendertarget back to the main backbuffer
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)nullptr);
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water.
	*	Algorithm:
	*	Draw reflected scene.
	*	Draw reflected sky layer(s) and bodies.
	*	Clear Zbuffer
	*	Fill Zbuffer by drawing water surface (allows proper sorting into regular scene).
	*	Draw non-reflected scene (done in regular app render loop).
	*
	*	This algorithm doesn't apply to translucent water, which is rendered into a
	*   texture and rendered at end of scene. */
//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(Water)
void WaterRenderObjClass::Render(RenderInfoClass & rinfo)
{
	// Ronin @diagnostic 02/08/2026 DX9: attribute every draw issued below to "water" on the [DRAW] HUD.
	Debug_Statistics::DrawSubsystemScope drawTag(Debug_Statistics::DRAW_SUBSYS_WATER);

	//USE_PERF_TIMER(Water)
	if (TheTerrainRenderObject && !TheTerrainRenderObject->getMap())
		return;	//no map has been loaded yet.

	if (((RTS3DScene *)rinfo.Camera.Get_User_Data())->getCustomPassMode() == SCENE_PASS_ALPHA_MASK ||
		((SceneClass *)rinfo.Camera.Get_User_Data())->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
		return;	//water is not drawn in wireframe or custom scene passes

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableWater) {
		return;
	}
#endif
	// Ronin @feature 15/08/2026 DX9: §29i depth-pass trim. Water has no business in a shadow DEPTH
	// map — it costs ~191 draws there, and because it flushes AFTER the mesh renderer
	// (W3DScene.cpp:879 vs :859) it paints over every caster in the colour target, which is exactly
	// what the debug overlay shows us. Trimming it is wanted anyway; it also tests whether the ship
	// was in the map all along and simply hidden behind water in the overlay.
	if (TheTerrainShadowPass.inDepthPass)
		return;

	if (ShaderClass::Is_Backface_Culling_Inverted())
		return;	//the water object will not reflect in itself, so don't do anything if rendering a mirror.

	//this water type needs to rendered after the rest of scene, so buffer it up for later

	// If static sort lists are enabled and this mesh has a sort level, put it on the list instead
	// of rendering it.
	unsigned int sort_level = (unsigned int)Get_Sort_Level();

	if (WW3D::Are_Static_Sort_Lists_Enabled() && sort_level != SORT_LEVEL_NONE)
	{
		WW3D::Add_To_Static_Sort_List(this, sort_level);
		return;
	}

	switch(m_waterType)
	{
		case WATER_TYPE_0_TRANSLUCENT:
		case WATER_TYPE_3_GRIDMESH:
			// Ronin @feature 15/09/2026 DX9: flat water / water mesh on-off from the debug panel `water` command. All off = normal.
			//Draw the water surface as a bunch of alpha blended tiles covering areas where water is visible
			if (!TheWaterDebug.skipFlat)
				renderWater();
			if ((!m_drawingRiver || m_disableRiver) && !TheWaterDebug.skipMesh) {
				renderWaterMesh();	//Draw water surface as 3D deforming mesh if it's enabled on this map.
			}
			break;

		case WATER_TYPE_2_PVSHADER:
			// Ronin @feature 28/09/2026 DX9: reflective water on the map's own polygons - type 0's geometry drawn with the WaterSea
			// shaders (drawTrapezoidWater / drawRiverWater). `water sea 1` brings back the original infinite plane.
			if (TheWaterDebug.seaPlane)
			{
				m_refractionOK = FALSE;	// Ronin @feature 29/09/2026 DX9: phase 3 - no copy for the sea plane; never a stale one
				m_wakeLive = FALSE;		// Ronin @feature 03/10/2026 DX9: phase 5 - nor wakes
				drawSea(rinfo);	//draw water surface
			}
			else
			{
				if (!TheWaterDebug.skipFlat)
				{
					renderWakes();		// Ronin @feature 03/10/2026 DX9: phase 5 - this frame's wakes, into their texture
					copyRefraction();	// Ronin @feature 29/09/2026 DX9: phase 3 - the frame under the water, for its refraction
					renderWater();
				}
				if ((!m_drawingRiver || m_disableRiver) && !TheWaterDebug.skipMesh)
					renderWaterMesh();	// the dam grid keeps its ps_1_1 look for now
			}
			break;

		case WATER_TYPE_1_FB_REFLECTION:
			{
				//Normal frame buffer reflection water type. Non translucent.  Legacy code we're not using anymore.
				Matrix3D	OldCameraMatrix=rinfo.Camera.Get_Transform();
				Vector3		WaterNormal(0,0,1);	//normal of plane used for reflection
				Vector4		WaterPlane(WaterNormal.X,WaterNormal.Y,WaterNormal.Z,m_level);	//assume distance to origin 0

				//flip the winding order of polygons to draw the reflected back sides.
				ShaderClass::Invert_Backface_Culling(true);

			#ifdef CLIP_GEOMETRY_TO_PLANE
			  // Set a clip plane, so that only objects above the water are reflected
				WaterPlane.W *= -1.0f;	//flip sign of plane distance for D3D use.

			//	DX8Wrapper::Set_DX8_Clip_Plane( 0, &WaterPlane.X );
			//	DX8Wrapper::Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0 );	//turn on first clip plane

				// Alternate Clipping Method using alpha testing hack!
				/**************************************************************************************/

				//get current view matrix
				D3DXMATRIX curView;
				DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

				//get inverse of view matrix(= view to world matrix)
				D3DXMATRIX inv;
				Real det;
				D3DXMatrixInverse(&inv, &det, &curView);

				//create clipping matrix by inserting our plane equation into the 1st column
				D3DXMATRIX clipMatrix;
				D3DXMatrixIdentity(&clipMatrix);
				clipMatrix(0,0)=WaterNormal.X;
				clipMatrix(1,0)=WaterNormal.Y;
				clipMatrix(2,0)=WaterNormal.Z;
				clipMatrix(3,0)=WaterPlane.W+0.5f;
				inv *=clipMatrix;

				// Change texture wrapping mode to 'clamp' for texture stage 1
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

				// Use CameraSpace vertices as input to matrix and use texture wrap mode from stage 1
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION|1);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

				// Set texture generation matrix for stage 1
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, inv);

				// Disable bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);

				// Pass stage 0 texture data untouched(by modulating with white)
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );	//stage 1 texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );	//previous stage texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );	//module with white => does nothing

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );	//stage 1 texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );	//previous stage texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );	//modulate with clipping texture

				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,0x00);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAFUNC,D3DCMP_NOTEQUAL);	//pass pixels who's alpha is not zero
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, true);	//test pixels if transparent(clipped) before rendering.

				// Set clipping texture
				m_alphaClippingTexture->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_Min_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mag_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mip_Mapping(TextureClass::FILTER_TYPE_NONE);

				DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

				//TODO: Will have to make sure that the shader system is not resetting my stage 1 setup
				//while rendering the scene

				/*************************************************************************************/
			#endif

			#if 0	// No longer do simple rendering.
				if (TheGlobalData->m_useWaterPlane)
				{
					Matrix3D	reflectedTransform(reflectCameraTransform(OldCameraMatrix,m_level));
					//@todo : Would it be better to create a new camera or change the transform of the
					//existing one?
					rinfo.Camera.Set_Transform( reflectedTransform );
					rinfo.Camera.Apply();	//force an update of all the camera dependent parameters like frustum clip planes

					if(m_useCloudLayer)
					{
						if (TheGlobalData && TheGlobalData->m_drawEntireTerrain)
							m_skyBox->Render(rinfo);
						else
						{
							renderSky();
							if (m_tod == TIME_OF_DAY_NIGHT)
								renderSkyBody(&reflectedTransform);
						}
					}

					WW3D::Render(m_parentScene,&rinfo.Camera);

					rinfo.Camera.Set_Transform(OldCameraMatrix);	//restore original non-reflected matrix
					rinfo.Camera.Apply();	//force an update of all the camera dependent parameters like frustum clip planes

					//clear the z-buffer to remove changes made by objects inside mirror
					Renderer::Clear(false,true,Vector3(0.1f,0.1f,0.1f));
				}
			#endif

			#ifdef CLIP_GEOMETRY_TO_PLANE
				//restore default culling mode
			//	DX8Wrapper::Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, 0 );	//turn off first clip plane

				//disable texture coordinate generation
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, false);	//disable alpha testing
			#endif

				ShaderClass::Invert_Backface_Culling(false);	//return culling back to normal

				ShaderClass::Invalidate();	//reset shading system so it forces full state set.

				renderWater();
			}
			break;

		default:
			break;
	}

	if (TheGlobalData && TheGlobalData->m_drawSkyBox)
	{
		//center skybox around camera
		Vector3 pos=rinfo.Camera.Get_Position();
		pos.Z = TheGlobalData->m_skyBoxPositionZ;
		m_skyBox->Set_Position(pos);
		m_skyBox->Render(rinfo);
	}

	//Clean up after any pixel shaders.
	//Force render state apply so that the null texture gets applied to D3D, thus releasing shroud reference count.
	DX8Wrapper::Invalidate_Cached_Render_States();

	// Ronin @feature 28/09/2026 DX9: `water waves 0` skips the shore waves (debug panel), Water_Work.md F5.
	// Ronin @feature 29/09/2026 DX9: drawn with the WaterSea shore foam by default; `water oldwaves 0` leaves the foam alone.
	const Bool shoreFoam = (m_waterType == WATER_TYPE_2_PVSHADER && WaterSea_ShoreFoamActive(m_heightTexture != nullptr)) ? TRUE : FALSE;
	if (m_waterTrackSystem && !TheWaterDebug.skipWaves && !shoreFoam)
		m_waterTrackSystem->flush(rinfo);

//	renderWaterMesh();
//	renderWaterWave();
}

//-------------------------------------------------------------------------------------------------
/** Clips the water plane to the current camera frustum and returns a bounding
	* box enclosing the clipped plane.  Returns false if water plane is not visible. */
//-------------------------------------------------------------------------------------------------
Bool WaterRenderObjClass::getClippedWaterPlane(CameraClass *cam, AABoxClass *box)
{
	const FrustumClass & frustum = cam->Get_Frustum();

	ClipPolyClass	ClippedPoly0;
	ClipPolyClass	ClippedPoly1;

	///@todo: generate proper sized polygon
	ClippedPoly0.Reset();
	ClippedPoly0.Add_Vertex(Vector3(0,0,m_level));
	ClippedPoly0.Add_Vertex(Vector3(0,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,0,m_level));

	//clip against all 6 frustum planes
	ClippedPoly0.Clip(frustum.Planes[0],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[1],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[2],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[3],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[4],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[5],ClippedPoly0);

	Int final_vcount = ClippedPoly0.Verts.Count();

	//make sure the polygon is visible
	if (final_vcount >= 3)
	{
		//find axis aligned bounding box around visible polygon
		if (box)
  			box->Init(&(ClippedPoly0.Verts[0]),final_vcount);
		return TRUE;
	}

	return FALSE;	//water plane is not visible
}

//-------------------------------------------------------------------------------------------------
/** Draws the water surface using a custom D3D vertex/pixel shader and a
	* reflection texture.  Only tested to work on GeForce3. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawSea(RenderInfoClass& rinfo)
{
	AABoxClass	seaBox;

	// Ronin @bugfix 28/09/2026 DX9: a sea shader or the reflection target failed to load - draw nothing rather than crash.
	if (!m_dwWaveVertexShader || !m_dwWavePixelShader || !m_pReflectionTexture)
		return;

	if (!getClippedWaterPlane(&rinfo.Camera, &seaBox))
		return;	//the sea is not visible

	D3DXMATRIX matProj, matView, matWW3D;

	//create a transform which will flip the y and z coordinates to fit our system
	memset(&matWW3D, 0, sizeof(D3DMATRIX));
	matWW3D._11 = 1.0f;
	matWW3D._32 = 1.0f;
	matWW3D._23 = 1.0f;
	matWW3D._44 = 1.0f;

	DX8Wrapper::Set_Transform(D3DTS_WORLD, Transform);	//position the water surface
	DX8Wrapper::Set_Texture(0, nullptr);	//we'll be setting our own textures, so reset W3D
	DX8Wrapper::Set_Texture(1, nullptr);	//we'll be setting our own textures, so reset W3D

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	Vector3 camTran;

	rinfo.Camera.Get_Transform().Get_Translation(&camTran);

	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, matView);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, matProj);

	// @refactor Ronin 23/03/2026 DX9: Route safe fixed-function water state writes through DX8Wrapper.
	//default setup from Kenny's demo
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 1);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 2);

	DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 3);

//	m_pDev->SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
//	m_pDev->SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
//	m_pDev->SetTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_POINT );

//	m_pDev->SetTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_POINT );
//	m_pDev->SetTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_POINT );
//	m_pDev->SetTextureStageState( 1, D3DTSS_MIPFILTER, D3DTEXF_NONE );
//end of default setup

	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	// Ronin @bugfix 28/09/2026 DX9: no D3DRS_WRAP0. It interpolates texcoord 0 the short way round, and the bump uv steps
	// exactly 3.0 per vertex (PATCH_UV_SCALE), which it reads as no step at all. The sampler above does the tiling.

	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	DX8Wrapper::Set_DX8_Texture(0, m_pBumpTexture[(Int)m_fBumpFrame]);
#ifdef MIPMAP_BUMP_TEXTURE
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
#endif
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVMAT00, F2DW(m_fBumpScale));
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVMAT01, F2DW(0.0f));
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVMAT10, F2DW(0.0f));
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVMAT11, F2DW(m_fBumpScale));
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLSCALE, F2DW(1.0f));
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLOFFSET, F2DW(0.0f));

	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);

	m_pDev->SetVertexShader(m_dwWaveVertexShader);
	// Ronin @bugfix 28/09/2026 DX9: DX8's SetVertexShader carried the input layout; DX9 needs the declaration bound too.
	m_pDev->SetVertexDeclaration(W3DShaderManager::GetShaderDeclaration(m_dwWaveVertexShader));
	m_pDev->SetPixelShader(pickWavePS(WATER_KIND_SEA));	// Ronin @feature 02/10/2026 DX9: phase 4 - the views variant under `water view`
	// Ronin @feature 29/09/2026 DX9: the WaterSea inputs the polygons use too (R3DWaterSea.h). The sea has no water texture
	// and no river edge, so both take the white texture.
	bindWaterSea(m_whiteTexture->Peek_D3D_Texture(), m_whiteTexture->Peek_D3D_Texture(), 0.0f, FALSE, FALSE);	// no swell on the sea plane's patches

//	Make reflection brighter to compensate for darker coloring on sea floor
//	m_pDev->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_ONE );
//	m_pDev->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Texture(1, m_pReflectionTexture->Peek_D3D_Texture());

//	m_pDev->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME);//LORENZEN

	Int patchX, patchY, startX, startY;

	D3DXMATRIX patchMatrix;
	memset(&patchMatrix, 0, sizeof(D3DXMATRIX));
	patchMatrix._11 = PATCH_SCALE;
	patchMatrix._22 = 1.0f;
	patchMatrix._33 = PATCH_SCALE;
	patchMatrix._44 = 1.0f;

	m_pDev->SetStreamSource(0, m_vertexBufferD3D, 0, sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
	m_pDev->SetIndices(m_indexBufferD3D);

	for (startY = patchY = (seaBox.Center.Y - seaBox.Extent.Y) / (PATCH_WIDTH * PATCH_SCALE); (patchY * PATCH_WIDTH * PATCH_SCALE) < (seaBox.Center.Y + seaBox.Extent.Y); patchY++)
	{
		for (startX = patchX = (seaBox.Center.X - seaBox.Extent.X) / (PATCH_WIDTH * PATCH_SCALE); (patchX * PATCH_WIDTH * PATCH_SCALE) < (seaBox.Center.X + seaBox.Extent.X); patchX++)
		{
			D3DXMATRIX matTempWorld;
			patchMatrix._41 = (float)(patchX * PATCH_WIDTH * PATCH_SCALE);
			patchMatrix._43 = (float)(patchY * PATCH_WIDTH * PATCH_SCALE);
			//convert the default D3D coordinate system into ours
			D3DXMatrixMultiply(&matTempWorld, &patchMatrix, &matWW3D);

			// Ronin @feature 28/09/2026 DX9: WaterSea_vs takes the patch's world matrix (c4-c7) apart from the view-projection
			// (c0-c3, setReflectiveConstants), so the bump tiles in world space. Transposed: one dp4 per component.
			D3DXMatrixTranspose(&matTempWorld, &matTempWorld);
			m_pDev->SetVertexShaderConstantF(4, (const float*)&matTempWorld, 4);

			// Ronin @bugfix 28/09/2026 DX9: a strip of n indices is n-2 triangles; m_numIndices read two past the buffer.
			m_pDev->DrawIndexedPrimitive(
				D3DPT_TRIANGLESTRIP,
				0,
				0,
				m_numVertices,
				0,
				m_numIndices - 2);
			// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - sea patch draws, and whether the sea shaders were bound.
			countWaterDraw(TheWaterStats.seaDraws, TheWaterStats.seaPSDraws, pickWavePS(WATER_KIND_SEA), m_dwWaveVertexShader);
		}
	}
//	m_pDev->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	DX8Wrapper::Set_DX8_Texture(0, nullptr); //release reference to bump texture
	DX8Wrapper::Set_DX8_Texture(1, nullptr); //release reference to reflection texture
	DX8Wrapper::Set_DX8_Texture(2, nullptr); //release reference to reflection texture

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 1);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);

	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_WRAP0, 0);  //turn off texture wrapping

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	//Restore old transforms
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW, matView);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_PROJECTION, matProj);

	m_pDev->SetPixelShader(0);	//turn off pixel shader

	DX8Wrapper::BindLayoutFVF(DX8_FVF_XYZDUV1, "WaterRenderObjClass::drawSea");

	DX8Wrapper::Invalidate_Cached_Render_States();

	if (TheTerrainRenderObject->getShroud())
	{
		W3DShaderManager::setTexture(0, TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
		m_pDev->SetStreamSource(0, m_vertexBufferD3D, 0, sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
		m_pDev->SetIndices(m_indexBufferD3D);
		for (startY = patchY = (seaBox.Center.Y - seaBox.Extent.Y) / (PATCH_WIDTH * PATCH_SCALE); (patchY * PATCH_WIDTH * PATCH_SCALE) < (seaBox.Center.Y + seaBox.Extent.Y); patchY++)
		{
			for (startX = patchX = (seaBox.Center.X - seaBox.Extent.X) / (PATCH_WIDTH * PATCH_SCALE); (patchX * PATCH_WIDTH * PATCH_SCALE) < (seaBox.Center.X + seaBox.Extent.X); patchX++)
			{
				D3DXMATRIX matTemp;
				patchMatrix._41 = (float)(patchX * PATCH_WIDTH * PATCH_SCALE);
				patchMatrix._43 = (float)(patchY * PATCH_WIDTH * PATCH_SCALE);

				D3DXMatrixMultiply(&matTemp, &patchMatrix, &matWW3D);

				DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD, matTemp);

				// Ronin @bugfix 28/09/2026 DX9: a strip of n indices is n-2 triangles; m_numIndices read two past the buffer.
				m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, m_numVertices, 0, m_numIndices - 2);
			}

		}
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
}


#define FEATHER_LAYER_COUNT (5.0f)
#define FEATHER_THICKNESS   (4.0f)

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface.*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWater()
{
	for (PolygonTrigger *pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
		if (pTrig->isWaterArea()) {
			if (pTrig->getNumPoints()>2) {
				if (pTrig->isRiver()) {
					drawRiverWater(pTrig);
					continue;
				}
				// Ronin @bugfix 01/10/2026 DX9: sea or lake, for the WaterSea opacity (bindWaterSea reads it).
				m_standingIsSea = isSeaPolygon(pTrig);
				if (TheWaterStats.enabled)
				{
					if (m_standingIsSea)
						TheWaterStats.seaPolys++;
					else
						TheWaterStats.lakePolys++;
				}
				Int k;
				for (k=1; k<pTrig->getNumPoints()-1; k=k+2) {
					ICoord3D pt3 = *pTrig->getPoint(0);
					ICoord3D pt2 = *pTrig->getPoint(k);
					ICoord3D pt1 = *pTrig->getPoint(k+1);
					ICoord3D pt0 = *pTrig->getPoint(k+1);
					if (k+2<pTrig->getNumPoints()) {
						pt0 = *pTrig->getPoint(k+2);
					}
					Vector3 points[4];
					points[0].Set(pt0.x, pt0.y, pt0.z);
					points[1].Set(pt1.x, pt1.y, pt1.z);
					points[2].Set(pt2.x, pt2.y, pt2.z);
					points[3].Set(pt3.x, pt3.y, pt3.z);

					if ( TheGlobalData->m_featherWater )
					{
						for (int r = 0; r < TheGlobalData->m_featherWater; ++r)
						{
							drawTrapezoidWater(points);
							points[0].Z += (FEATHER_THICKNESS/TheGlobalData->m_featherWater);
						}
					}

					else
						drawTrapezoidWater(points);


				}
			}
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky plane.  Will apply current time-of-day settings including
	* some simple UV scrolling animation. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderSky()
{
	Int timeNow,timeDiff;
	Real fu,fv;

	Setting *setting=&m_settings[m_tod];

	timeNow=timeGetTime();

	timeDiff=timeNow-m_LastUpdateTime;
	m_LastUpdateTime=timeNow;

	m_uOffset += timeDiff * setting->uScrollPerMs * setting->skyTexelsPerUnit;
	m_vOffset += timeDiff * setting->vScrollPerMs * setting->skyTexelsPerUnit;

	//clamp uv coordinate into 0,1 range
	m_uOffset = m_uOffset - (Real)((Int) m_uOffset);
	m_vOffset = m_vOffset - (Real)((Int) m_vOffset);

	fu= m_uOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;
	fv= m_vOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;


	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetOpaqueShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	DX8Wrapper::Set_Shader(m_shader2);

	DX8Wrapper::Set_Texture(0,setting->skyTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYPLANE_SIZE;
			verts[0].y=SKYPLANE_SIZE;
			verts[0].z=SKYPLANE_HEIGHT;
			verts[0].u1=m_uOffset;
			verts[0].v1=fv;
			verts[0].diffuse=setting->vertex01Diffuse;

			verts[1].x=SKYPLANE_SIZE;
			verts[1].y=SKYPLANE_SIZE;
			verts[1].z=SKYPLANE_HEIGHT;
			verts[1].u1=fu;
			verts[1].v1=fv;
			verts[1].diffuse=setting->vertex11Diffuse;

			verts[2].x=SKYPLANE_SIZE;
			verts[2].y=-SKYPLANE_SIZE;
			verts[2].z=SKYPLANE_HEIGHT;
			verts[2].u1=fu;
			verts[2].v1=m_vOffset;
			verts[2].diffuse=setting->vertex10Diffuse;

			verts[3].x=-SKYPLANE_SIZE;
			verts[3].y=-SKYPLANE_SIZE;
			verts[3].z=SKYPLANE_HEIGHT;
			verts[3].u1=m_uOffset;
			verts[3].v1=m_vOffset;
			verts[3].diffuse=setting->vertex00Diffuse;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0, "WaterRenderObjClass::RenderSky");
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	tm.Set_Translation(Vector3(0,0,0));
	Renderer::Set_Transform(RenderTransform_World,tm);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky body.  Used for moon and sun.  We rotate the image
	* so that it always faces the camera.  This removes perspective and helps hide that
	* it's a flat image. */
//-------------------------------------------------------------------------------------------------
///	@todo: Add code to render properly sorted sun sky body.
void WaterRenderObjClass::renderSkyBody(Matrix3D *mat)
{
	Vector3 cPos;

	Vector3 pView,pRight,pUp,pPos(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT);

	mat->Get_Translation(&cPos);

	pView=cPos-pPos;	//billboard to camera
	pView.Normalize();	//particle view direction

	Vector3 WorldUp(0,0,-1);	///@todo: hacked so only works for reflections across xy plane

#ifdef ALLOW_TEMPORARIES
	Vector3 rotAxis=Vector3::Cross_Product(WorldUp,pView);	//get axis of rotation.
	rotAxis.Normalize();
#else
	Vector3 rotAxis;
	Vector3::Normalized_Cross_Product(WorldUp, pView, &rotAxis);
#endif

	Real angle=Vector3::Dot_Product(WorldUp,pView);

	angle = acos(angle);


	Matrix3D tm(1);
	tm.Set(rotAxis,angle);
	tm.Adjust_Translation(Vector3(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT));


	Renderer::Set_Transform(RenderTransform_World,tm);


	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetAlphaShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	DX8Wrapper::Set_Shader(m_shader2);


//	DX8Wrapper::Set_Shader(ShaderClass::/*_PresetAdditiveShader*//*_PresetOpaqueShader*/_PresetAlphaShader);
//	DX8Wrapper::Set_Texture(0,setting->skyBodyTexture);

	DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYBODY_SIZE;
			verts[0].y=SKYBODY_SIZE;
			verts[0].z=0;
			verts[0].u2=0;
			verts[0].v2=1;
			verts[0].diffuse=0xffffffff;

			verts[1].x=SKYBODY_SIZE;
			verts[1].y=SKYBODY_SIZE;
			verts[1].z=0;
			verts[1].u2=1;
			verts[1].v2=1;
			verts[1].diffuse=0xffffffff;

			verts[2].x=SKYBODY_SIZE;
			verts[2].y=-SKYBODY_SIZE;
			verts[2].z=0;
			verts[2].u2=1;
			verts[2].v2=0;
			verts[2].diffuse=0xffffffff;

			verts[3].x=-SKYBODY_SIZE;
			verts[3].y=-SKYBODY_SIZE;
			verts[3].z=0;
			verts[3].u2=0;
			verts[3].v2=0;
			verts[3].diffuse=0xffffffff;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0, "WaterRenderObjClass::renderSkyBody");
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}

//Defines for procedural water animation.
#define WATER_FREQ	(2.0*3.2831/4.0)	//2pi (full cycle) cover 4 units
#define WATER_AMP	(1.0f)
#define	WATER_OFFSET (0.1f)

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface mesh geometry.
	*	This is a work-in-progress!  Do not use this code! */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWaterMesh()
{

	if (!m_doWaterGrid)
		return;	//the water grid is disabled.

	//According to Nvidia there's a D3D bug that happens if you don't start with a
	//new dynamic VB each frame - so we force a DISCARD by overflowing the counter.
	m_vertexBufferD3DOffset = 0xffff;

	Setting *setting=&m_settings[m_tod];

	WaterMeshData *pData;
	Int	mx=m_gridCellsX+1;
	Int my=m_gridCellsY+1;
	Int i,j;

	Real cellSizeX=m_gridCellSize;
	Real cellSizeY=m_gridCellSize;
//	Real	uScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f;
//	Real	vScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f;

	//Old waterRepeatCount settings in INI were based on 128x128 water grid of cellsize=10
	//Scale values to correct size.
	Real	uScale=setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f*0.2f;
	Real	vScale=setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f*0.2f;

	Vector3	nx(cellSizeX*2.0f,0,0);
	Vector3 ny(0,cellSizeY*2.0f,0);
	Vector3 C;

#ifdef DO_WATER_SIMULATION		//Debug code used to create a dummy water animation
	//
	// Mark: If you re-enable this water simulation, you might want to consider moving
	// this code to the update() method of the water render object (Colin)
	//

	static Real PhasePerFrameX=0.1f;
	static Real PhasePerFrameY=0.1f;

	//update the mesh heights for this frame (update buffer is 2 samples wider/taller due to border)
	for (j=0,pData=m_meshData; j<(my+2); j++)
	{
		for (i=0; i<(mx+2); i++)
		{
			//*pData = WATER_AMP * sin(WATER_FREQ*(0.7f*i + 0.7f*j) - PhasePerFrame);

			pData->height=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
//			*pData=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
			pData++;
		}
	}

	PhasePerFrameX -= 0.08f;
	PhasePerFrameY -= 0.1f;
#endif

	MaterMeshVertexFormat *vb;
	if (m_vertexBufferD3DOffset < m_numVertices)
	{
		//we have room in current VB, append new verts
		if (m_vertexBufferD3D->Lock(
			m_vertexBufferD3DOffset * sizeof(MaterMeshVertexFormat),
			mx * my * sizeof(MaterMeshVertexFormat),
			(void**)&vb,
			D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
	}
	else
	{
		//ran out of room in last VB, request a substitute VB.
		if(m_vertexBufferD3D->Lock(0,mx*my*sizeof(MaterMeshVertexFormat), (void**)&vb,D3DLOCK_DISCARD) != D3D_OK)
			return;
		m_vertexBufferD3DOffset=0;	//reset start of page to first vertex
	}
	Int diffuse;
	diffuse = setting->waterDiffuse&0x00ffffff;
	Int alpha = (setting->waterDiffuse & 0xff000000)>>24;
	// Reduce alpha for wave mesh
	alpha -= 0x20;
	diffuse |= alpha<<24;

	//I pulled some of these constants out of the loops for speed:
	Real uvCosScale=0.02*cos(3*m_riverVOrigin);
	Real sinOffset=25*m_riverVOrigin;
	Real originScale=m_riverVOrigin/vScale;
	Real bumpSizeDiv=cellSizeY/BUMP_SIZE;
	Real bumpSizeDiv2=0.3f*cellSizeY/BUMP_SIZE;

	//Data has a 1 vertex padding all around it so we don't need to special-case edges.  Improves performance
	for (j=0,pData=m_meshData+mx+2+1; j<my; j++,pData+=2)	//skip 2 horizontal border samples after each row
	{
		Real y=(float)j*cellSizeY;
		Real v1Offset=m_riverVOrigin+(float)j*vScale + uvCosScale*WWMath::Fast_Sin(sinOffset+y*PI/(8*MAP_XY_FACTOR));
		Real v2Offset=((float)j+originScale)*bumpSizeDiv + (float)j*bumpSizeDiv2;

		for (i=0; i<mx; i++)
		{
			//compute normal by looking at 4 vertex neightbors
#ifdef USE_MESH_NORMALS
			nx.Z=(pData+1)->height - (pData-1)->height;
			ny.Z=(pData+mx+2)->height - (pData-mx-2)->height;
//			nx.Z=*(pData+1)-*(pData-1);
//			ny.Z=*(pData+mx+2)-*(pData-mx-2);
			Vector3::Cross_Product(nx,ny,&C);
			C.Normalize();
			vb->nx = C.X;
			vb->ny = C.X;
			vb->nz = C.X;
#endif
			Real x = (float)i*cellSizeX;
			vb->x=	x;
			vb->y=	y;
			vb->z=  pData->height;//WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ+PhasePerFrame)+cos((float)j*WATER_FREQ+PhasePerFrame));

			vb->diffuse = diffuse;
#ifdef SCROLL_UV
//			vb->diffuse=0x80ffffff;
			vb->u1=(float)i*uScale;
			vb->v1=v1Offset;

			//old slow version
			//vb->v1=m_riverVOrigin+(float)j*vScale + 0.02*cos(3*m_riverVOrigin)*sin(25*m_riverVOrigin+y*PI/(8*MAP_XY_FACTOR));

//			vb->u2=m_initialGridU2+(float)i*uScale2;
//			vb->v2=m_initialGridV2+(float)j*vScale2;
#else
			vb->u1=(float)i*uScale;
			vb->v1=(float)j*vScale;
#endif
			vb->u2=(float)(i)*cellSizeX/BUMP_SIZE;
			vb->v2=v2Offset;
			//old slow code
			//vb->v2=(float)(j+m_riverVOrigin/vScale )*cellSizeY/BUMP_SIZE+ 0.3f*(float)j*cellSizeY/BUMP_SIZE;
			vb++;
			pData++;
		}
	}

	m_vertexBufferD3D->Unlock();

	Renderer::Set_Transform(RenderTransform_World,Transform);	//position the water surface
	DX8Wrapper::Set_Material(m_meshVertexMaterialClass);

	ShaderClass::CullModeType oldCullMode=m_shaderClass.Get_Cull_Mode();

	ShaderClass::DepthMaskType oldDepthMask=m_shaderClass.Get_Depth_Mask();
	m_shaderClass.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//disable writing to z-buffer to prevent particle clipping.

	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_ENABLE);	//water should be visible from both sides

	DX8Wrapper::Set_Shader(m_shaderClass);
#if 1
	setupFlatWaterShader();
#else
	//DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0,setting->waterTexture);
	DX8Wrapper::Set_Texture(1,setting->waterTexture);

	DX8Wrapper::Set_Light(0,*m_meshLight);
	DX8Wrapper::Set_Light(1,nullptr);
	DX8Wrapper::Set_Light(2,nullptr);
	DX8Wrapper::Set_Light(3,nullptr);
/*
	DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0);	//turn off scene ambient
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SPECULARENABLE,TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LOCALVIEWER,TRUE);
*/

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
#endif


//	m_pDev->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);	//used to display grid under map.

	m_pDev->SetIndices(m_indexBufferD3D);
	m_pDev->SetStreamSource(0,m_vertexBufferD3D, m_vertexBufferD3DOffset * sizeof(MaterMeshVertexFormat), sizeof(MaterMeshVertexFormat));

	m_pDev->SetIndices(m_indexBufferD3D);
	m_pDev->SetStreamSource(0, m_vertexBufferD3D, m_vertexBufferD3DOffset * sizeof(MaterMeshVertexFormat), sizeof(MaterMeshVertexFormat));

	// @bugfix Ronin 13/01/2026 Water: use explicit declaration for water mesh inputs (prevents stale FVF/decl ambiguity)
	// WATER_MESH_FVF is either DX8_FVF_XYZNDUV2 (USE_MESH_NORMALS) or DX8_FVF_XYZDUV2.
	if (DX8Wrapper::DeclCache) {
		DX8Wrapper::DeclCache->BindDecl(WATER_MESH_FVF, "WaterRenderObjClass::renderWaterMesh");
	}
	else {
		// Fallback if decl cache isn't available for some reason.
		DX8Wrapper::BindLayoutFVF(WATER_MESH_FVF, "WaterRenderObjClass::renderWaterMesh(fallback-FVF)");
		DX8Wrapper::Apply_Render_State_Changes();
	}

	if (TheTerrainRenderObject->getShroud() && !m_trapezoidWaterPixelShader)
	{
		//we have a shroud to apply and can't do it inside the pixel shader.
		//so do it in stage1
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 1);

		//modulate with shroud texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );	//stage 1 texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );	//previous stage texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

		//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
		//write to the zbuffer.  Change to LESSEQUAL.
		// @refactor Ronin 23/03/2026 DX9: Route water ZFUNC writes through wrapper for cache coherence.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, mx* my, 0, m_numIndices - 2);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
	else
		m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,0,0,mx*my,0,m_numIndices-2);
	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - both branches above; the grid uses the flat-water shader.
	countWaterDraw(TheWaterStats.meshDraws, TheWaterStats.meshPSDraws, m_trapezoidWaterPixelShader);

	Debug_Statistics::Record_DX8_Polys_And_Vertices(m_numIndices-2,mx*my,ShaderClass::_PresetOpaqueShader);

//	m_pDev->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);

	if (m_trapezoidWaterPixelShader) DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(0);

	m_vertexBufferD3DOffset += mx*my;	//advance past vertices already in buffer

	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Set_Texture(1,nullptr);
	ShaderClass::Invalidate();
	m_shaderClass.Set_Cull_Mode(oldCullMode);	//water should be visible from both sides

	// restore shader to old mask
	m_shaderClass.Set_Depth_Mask(oldDepthMask);

	//W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);

}

inline void WaterRenderObjClass::setGridVertexHeight(Int x, Int y, Real value)
{
	DEBUG_ASSERTCRASH( x < (m_gridCellsX+1) && y < (m_gridCellsY+1), ("Invalid Water Mesh Coordinates") );

	if (m_meshData)
	{
		m_meshData[(y+1)*(m_gridCellsX+1+2)+x+1].height = value;
	}
}

void WaterRenderObjClass::setGridHeightClamps(Real minz, Real maxz)
{
	m_minGridHeight = minz;
	m_maxGridHeight = maxz;
}

void WaterRenderObjClass::addVelocity( Real worldX, Real worldY,
																			 Real zVelocity, Real preferredHeight )
{

	if( m_doWaterGrid)
	{
		Real gx,gy;
		Real minX,maxX,minY,maxY;
		Int x,y;
		WaterMeshData *meshPoint;
		m_disableRiver = true;

		//check if center falls within grid bounds
		if (worldToGridSpace(worldX, worldY, gx, gy))
		{

			//find extents of influence
			minX = floorf(gx - m_gridChangeMaxRange);
			if (minX < 0 )
				minX = 0;	//clamp extent to fall within box
			maxX = ceilf(gx + m_gridChangeMaxRange);
			if (maxX > m_gridCellsX)
				maxX = m_gridCellsX;	//clamp extent to fall within box

			minY = floorf(gy - m_gridChangeMaxRange);
			if (minY < 0 )
				minY = 0;	//clamp extent to fall within box
			maxY = ceilf(gy + m_gridChangeMaxRange);
			if (maxY > m_gridCellsY)
				maxY = m_gridCellsY;	//clamp extent to fall within box

			for (y=minY; y<=maxY; y++)
			{
				for (x=minX; x<=maxX; x++)
				{

					// get the mesh point that we're concerned with
					meshPoint = &m_meshData[ (y + 1) * (m_gridCellsX + 1 + 2) + x + 1 ];

					// we now have a new preferred height
					meshPoint->preferredHeight = preferredHeight;

					//
					// set the velocity of this point based on the distance from the center of the
					// "core" point for this call
					//
					meshPoint->velocity = meshPoint->velocity + zVelocity;

					// this point is now "in motion"
					BitSet( meshPoint->status, WaterRenderObjClass::IN_MOTION );

				}
			}

			//
			// the mesh data is now dirty, we need to pass through the velocity field
			// during an update phase to update the positions
			//
			m_meshInMotion = TRUE;

		}

	}

}

void WaterRenderObjClass::changeGridHeight(Real wx, Real wy, Real delta)
{
	Real gx,gy;
	Real *oldData;
	Real newData;
	Real distance;
	Real minX,maxX,minY,maxY;
	Int x,y;

	//check if center falls within grid bounds
	if (worldToGridSpace(wx, wy, gx, gy))
	{
		//find extents of influence
		minX = floorf(gx - m_gridChangeMaxRange);
		if (minX < 0 )
			minX = 0;	//clamp extent to fall within box
		maxX = ceilf(gx + m_gridChangeMaxRange);
		if (maxX > m_gridCellsX)
			maxX = m_gridCellsX;	//clamp extent to fall within box

		minY = floorf(gy - m_gridChangeMaxRange);
		if (minY < 0 )
			minY = 0;	//clamp extent to fall within box
		maxY = ceilf(gy + m_gridChangeMaxRange);
		if (maxY > m_gridCellsY)
			maxY = m_gridCellsY;	//clamp extent to fall within box

		for (y=minY; y<=maxY; y++)
		{
			for (x=minX; x<=maxX; x++)
			{	oldData = &m_meshData[(y+1)*(m_gridCellsX+1+2)+x+1].height;
				distance = (gx - (Real)x)*(gx - (Real)x) + (gy - (Real)y)*(gy - (Real)y);
				distance = sqrt(distance);
				newData = *oldData + 1.0f/(m_gridChangeAtt0+m_gridChangeAtt1*distance+distance*distance*m_gridChangeAtt2)*delta;
				//Clamp to min/max values
				if (newData < m_minGridHeight)
					newData = m_minGridHeight;
				if (newData > m_maxGridHeight)
					newData = m_maxGridHeight;
				*oldData = newData;
			}
		}
	}
}

void WaterRenderObjClass::setGridChangeAttenuationFactors(Real a, Real b, Real c, Real range)
{
	m_gridChangeAtt0 = a;
	m_gridChangeAtt1 = b;
	m_gridChangeAtt2 = c;
	m_gridChangeMaxRange = range/m_gridCellSize;	//convert range to grid space
}

void WaterRenderObjClass::setGridTransform(Real angle, Real x, Real y, Real z)
{
	m_gridDirectionX = Vector2(1.0f,0.0f);

	m_gridOrigin.X = x;
	m_gridOrigin.Y = y;

	Matrix3D xform(1);
	xform.Rotate_Z(angle);

	m_gridDirectionX.X = xform.Get_X_Vector().X;
	m_gridDirectionX.Y = xform.Get_X_Vector().Y;

	m_gridDirectionY.X = xform.Get_Y_Vector().X;
	m_gridDirectionY.Y = xform.Get_Y_Vector().Y;

	xform.Set_Translation(Vector3(x,y,z));
	Set_Transform(xform);
}

void WaterRenderObjClass::setGridTransform(const Matrix3D *transform )
{

	if( transform )
		Set_Transform( *transform );

}

void WaterRenderObjClass::getGridTransform(Matrix3D *transform )
{

	if( transform )
		*transform = Get_Transform();

}

void WaterRenderObjClass::setGridResolution(Real gridCellsX, Real gridCellsY, Real cellSize)
{
	m_gridCellSize=cellSize;

	if (m_gridCellsX != gridCellsX || m_gridCellsY != gridCellsY)
	{
		//resolution has changed
		m_gridCellsX=gridCellsX;
		m_gridCellsY=gridCellsY;

		if (m_meshData)
		{

			delete [] m_meshData;//free previously allocated grid and allocate new size
			m_meshData = nullptr;	 // must set to null so that we properly re-allocate
			m_meshDataSize = 0;

			Bool enable = m_doWaterGrid;
			enableWaterGrid(true);	// allocates buffers.
			m_doWaterGrid = enable;

		}
	}
}

void WaterRenderObjClass::getGridResolution( Real *gridCellsX, Real *gridCellsY, Real *cellSize )
{

	if( gridCellsX )
		*gridCellsX = m_gridCellsX;
	if( gridCellsY )
		*gridCellsY = m_gridCellsY;
	if( cellSize )
		*cellSize = m_gridCellSize;

}

static Real wobble(Real baseV, Real offset, Bool wobble)
{
	if (!wobble) return 0;
	offset = sin(2*PI*baseV - 3*offset);
	return offset/22;
}

/**Utility function used to query water heights in a manner that works in both RTS and WB.*/
Real WaterRenderObjClass::getWaterHeight(Real x, Real y)
{
	const WaterHandle *waterHandle = nullptr;
	Real waterZ = 0.0f;
	ICoord3D iLoc;

	iLoc.x = REAL_TO_INT_FLOOR( x + 0.5f );
	iLoc.y = REAL_TO_INT_FLOOR( y + 0.5f );
	iLoc.z = 0;

	for( PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext() )
	{

		if( !pTrig->isWaterArea() )
			continue;

		// See if point is in a water area
		if( pTrig->pointInTrigger( iLoc ) )
		{

			if( pTrig->getPoint( 0 )->z >= waterZ )
			{

				waterZ = pTrig->getPoint( 0 )->z;
				waterHandle = pTrig->getWaterHandle();

			}

		}

	}

	if (waterHandle)
		return waterHandle->m_polygon->getPoint( 0 )->z;
	return INVALID_WATER_HEIGHT;	//point not underwater
}

//-------------------------------------------------------------------------------------------------
//Draw a many sided river polygon.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawRiverWater(PolygonTrigger *pTrig)
{
	DX8Wrapper::Invalidate_Cached_Render_States();	///@todo: Figure out why rivers don't draw without reset of all states.

	Int rectangleCount = pTrig->getNumPoints()/2;
	rectangleCount--;

	Real bumpFactor = 5;
	static Bool doWobble = true;

	if (m_disableRiver) return;

	//allocate 2 triangles per side with 3 indices per triangle
	const Int indexCount = (rectangleCount+1)*2*3;
	// TheSuperHackers @bugfix arcticdolphin 12/10/2026 Skip rivers that do not fit the 16 bit dynamic buffers.
	DEBUG_ASSERTCRASH(indexCount <= DynamicIBAccessClass::MAX_INDEX_COUNT, ("River has too many points to draw"));
	if (indexCount > DynamicIBAccessClass::MAX_INDEX_COUNT)
		return;

	m_drawingRiver = true;

	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8,indexCount);
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		for (Int i=0; i<rectangleCount; i++)
		{
			//triangle 1
			curIb[0] = i*2;
			curIb[1] = i*2+1;
			curIb[2] = i*2+3;

			//triangle 2
			curIb[3] = i*2;
			curIb[4] = i*2+3;
			curIb[5] = i*2+2;

			curIb += 6;	//skip the 6 indices we just added.
		}
	}


	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{
			//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	Int innerNdx = pTrig->getRiverStart();
	Int outerNdx = innerNdx+1;

	Real endLen=0;
	Real totalLen=0;
	Int i;
	for (i=0; i<pTrig->getNumPoints()-1; i++) {
		ICoord3D innerPt = *pTrig->getPoint(i);
		ICoord3D outerPt = *pTrig->getPoint(i+1);
		Real dx = innerPt.x-outerPt.x;
		Real dy = innerPt.y-outerPt.y;
		Real curLen = sqrt(dx*dx+dy*dy);
		totalLen += curLen;
		if ( i==innerNdx) {
			endLen = curLen;
		}
	}
	bumpFactor = endLen/BUMP_SIZE;

	Real lengthOfRiver = (totalLen/2)-endLen;
	Real repeatCount = lengthOfRiver / (endLen);

	Real vScale=(Real)repeatCount/(Real)rectangleCount;

#define HEIGHT_TO_USE (0.5f)
	if (innerNdx >= pTrig->getNumPoints()-1) return;

	// Ronin @feature 02/10/2026 DX9: phase 4 - the flow map's input per cross-section, into the vertex normal's xy: downstream,
	// smoothed over the neighbours, x average width / this width (0.5..2) so a narrows runs faster.
	const Bool riverFlow = useReflection();
	const Int sections = pTrig->getNumPoints() / 2;
	std::vector<Real> flowX, flowY;
	if (riverFlow && sections > 1)
	{
		std::vector<Real> cx(sections), cy(sections), width(sections), dx(sections), dy(sections);
		Int a = innerNdx;
		Int b = outerNdx;
		Real widthSum = 0.0f;
		for (Int s = 0; s < sections; s++)
		{
			const ICoord3D *p = pTrig->getPoint(b);
			const ICoord3D *q = pTrig->getPoint(a);
			cx[s] = 0.5f * (Real)(p->x + q->x);
			cy[s] = 0.5f * (Real)(p->y + q->y);
			const Real wx = (Real)(p->x - q->x);
			const Real wy = (Real)(p->y - q->y);
			width[s] = sqrtf(wx * wx + wy * wy);
			widthSum += width[s];
			b++;
			a--;
			if (a < 0) a = pTrig->getNumPoints() - 1;
			if (b >= pTrig->getNumPoints()) b = 0;
		}
		const Real avgWidth = widthSum / (Real)sections;
		for (Int s = 0; s < sections; s++)	// central differences
		{
			const Int lo = (s > 0) ? s - 1 : 0;
			const Int hi = (s < sections - 1) ? s + 1 : sections - 1;
			const Real ex = cx[hi] - cx[lo];
			const Real ey = cy[hi] - cy[lo];
			const Real len = sqrtf(ex * ex + ey * ey);
			dx[s] = (len > 1.0e-3f) ? ex / len : 0.0f;
			dy[s] = (len > 1.0e-3f) ? ey / len : 0.0f;
		}
		flowX.resize(sections);
		flowY.resize(sections);
		for (Int s = 0; s < sections; s++)	// smoothed 1-2-1, then the width factor
		{
			const Int lo = (s > 0) ? s - 1 : 0;
			const Int hi = (s < sections - 1) ? s + 1 : sections - 1;
			Real ex = dx[lo] + 2.0f * dx[s] + dx[hi];
			Real ey = dy[lo] + 2.0f * dy[s] + dy[hi];
			const Real len = sqrtf(ex * ex + ey * ey);
			if (len > 1.0e-3f) { ex /= len; ey /= len; }
			Real f = (width[s] > 1.0f) ? avgWidth / width[s] : 1.0f;
			f = (f < 0.5f) ? 0.5f : (f > 2.0f) ? 2.0f : f;
			flowX[s] = ex * f;
			flowY[s] = ey * f;
		}
	}
	const Bool haveFlow = (flowX.size() == (size_t)sections) ? TRUE : FALSE;
	// Ronin @feature 28/09/2026 DX9: WaterType 2 draws rivers with the reflective WaterSea shaders.
	const Bool reflect = useReflection();
	//allocate 2 vertices per side
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(rectangleCount+1)*2);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real constA=3*m_riverVOrigin;

		// TheSuperHackers @bugfix afc-afc0 14/04/2026 Apply shroud per-vertex to avoid double-darkening
		// at river borders.
		W3DShroud *shroud = (!reflect && TheTerrainRenderObject) ? TheTerrainRenderObject->getShroud() : nullptr;	// Ronin @bugfix 11/10/2026 DX9: not the reflective draw - the WaterSea PS samples it

		for (i=0; i<(pTrig->getNumPoints()/2); i++)
		{
			Real x,y;
			ICoord3D innerPt = *pTrig->getPoint(outerNdx);
			ICoord3D outerPt = *pTrig->getPoint(innerNdx);
			outerNdx++;
			innerNdx--;
			if (innerNdx<0) {
				innerNdx = pTrig->getNumPoints()-1;
			}
			if (outerNdx >= pTrig->getNumPoints()) {
				outerNdx = 0;
			}
			x=innerPt.x;
			y=innerPt.y;

			vb->x=x;
			vb->y=y;

			vb->z=innerPt.z;

			vb->diffuse = getRiverVertexDiffuse(shroud, x, y, shadeR, shadeG, shadeB, diffuse);

			Real wobbleConst=-m_riverVOrigin+vScale*(Real)i + WWMath::Fast_Sin(2*PI*(vScale*(Real)i) - constA)/22.0f;
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=HEIGHT_TO_USE ;
			//old slower version
			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2=wobbleConst;
			vb->u2 = 1.0f;
			vb->nx = haveFlow ? flowX[i] : 0.0f;	// Ronin @feature 02/10/2026 DX9: phase 4 - the flow map's drift (WaterSea_vs)
			vb->ny = haveFlow ? flowY[i] : 0.0f;
			vb->nz = 1.0f;
			vb++;

			x=outerPt.x;
			y=outerPt.y;

			vb->x=x;
			vb->y=y;
			vb->z=outerPt.z;

			vb->diffuse = getRiverVertexDiffuse(shroud, x, y, shadeR, shadeG, shadeB, diffuse);
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=0;
			//old slower version
 			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2 =wobbleConst;
			vb->u2 = 0;
			vb->nx = haveFlow ? flowX[i] : 0.0f;
			vb->ny = haveFlow ? flowY[i] : 0.0f;
			vb->nz = 1.0f;
			vb++;

		}
	}

	Matrix3D tm(1);

	Renderer::Set_Transform(RenderTransform_World,tm);	//position the water surface
	DX8Wrapper::Set_Index_Buffer(ib_access, 0, "1st bind of WaterRenderObjClass::drawRiverWater");
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	if (reflect)
		setupReflectiveWater(TRUE);
	else
	{
		DX8Wrapper::Set_Texture(0,m_riverTexture);	//set to blue
		setupJbaWaterShader();
	}

	//In additive blending we need to use the alpha at the edges of river to darken
	//rgb instead.
	if (TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );

	if (m_riverWaterPixelShader && !reflect) DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_riverWaterPixelShader);
 	DWORD cull;
	DX8Wrapper::_Get_D3D_Device8()->GetRenderState(D3DRS_CULLMODE, &cull);

	// @refactor Ronin 23/03/2026 DX9: Route river fixed-function render-state writes through wrapper for cache coherence.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);

	if (wireframeForDebug) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	}
	// Ronin @bugfix 15/09/2026 DX9: same stale-layout bug as drawTrapezoidWater — rivers use the same dynamic VB path.
	DX8Wrapper::BindLayoutFVF(vb_access.FVF_Info().Get_FVF(), "WaterRenderObjClass::drawRiverWater");
	// Ronin @feature 28/09/2026 DX9: after the layout - BindLayoutFVF clears the vertex shader.
	if (reflect)
		bindReflectiveShaders();
	// Ronin @bugfix 03/10/2026 DX9: the WaterSea PS marks these pixels for TAA's water mask (render target 1 while TAA runs).
	const Bool taaMarked = reflect ? R3DTaa::beginWaterMask() : FALSE;
	DX8Wrapper::Draw_Triangles(0, rectangleCount * 2, 0, (rectangleCount + 1) * 2);
	if (taaMarked)
		R3DTaa::endWaterMask();
	// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - count this draw and whether the water shader was bound for it.
	if (reflect)
		countWaterDraw(TheWaterStats.riverDraws, TheWaterStats.riverPSDraws, pickWavePS(WATER_KIND_RIVER), m_dwWaveVertexShader);
	else
		countWaterDraw(TheWaterStats.riverDraws, TheWaterStats.riverPSDraws, m_riverWaterPixelShader);


	/*
	DX8Wrapper::Set_DX8_Texture(1, nullptr);
	DX8Wrapper::Set_DX8_Texture(2, nullptr);
	DX8Wrapper::Set_DX8_Texture(3, nullptr);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	*/
	if (wireframeForDebug) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	}

	// Ronin @feature 28/09/2026 DX9: reflective draw - back to fixed function, texture references dropped.
	if (reflect)
		unbindReflectiveShaders(vb_access.FVF_Info().Get_FVF());
	else if (m_riverWaterPixelShader) DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(0);

	//restore blend mode to what W3D expects.
	if (TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE );

	DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_CULLMODE, cull);


}

void WaterRenderObjClass::setupFlatWaterShader()
{

	DX8Wrapper::Set_Texture(0,m_riverTexture);
	// Ronin @bugfix 20/09/2026 DX9: force a re-apply, or the water draws with blending OFF and the soft shore edge's
	// destination-alpha feather dies (hard waterline). FlatTerrainShaderPixelShader::set kills ALPHABLENDENABLE with a raw
	// write (W3DShaderManager.cpp:4295); an alpha decal before us (ability radius = SHADOW_ALPHA_DECAL) then leaves
	// _PresetAlphaShader cached, we ask for that same preset and Set_Shader early-outs (dx8wrapper.h:1787).
	// MUST be here, not at the raw writer: ShaderDirty is a global ONE-SHOT, cleared by the next Apply (shader.cpp:417-430).
	ShaderClass::Invalidate();
	if (!TheWaterTransparency->m_additiveBlend)

		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	m_riverTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	//Setup shroud to render in same pass as water
	if (m_trapezoidWaterPixelShader)
	{	if (TheTerrainRenderObject->getShroud())
		{
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			//Use stage 3 to apply the shroud
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 3);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	}
		else
		{
			//Assume no shroud, so stage 3 will be null texture but using actual white because
			//pixel shader on GF4 generates random colors with SetTexture(3,nullptr).
			if (!m_whiteTexture->Is_Initialized())
			{	m_whiteTexture->Init();
				SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
				int pitch;
				void *pBits = surface->Lock(&pitch);
				const unsigned int bytesPerPixel = surface->Get_Bytes_Per_Pixel();
				surface->Draw_Pixel(0, 0, 0xffffffff, bytesPerPixel, pBits, pitch);
				surface->Unlock();
				REF_PTR_RELEASE(surface);
			}
			DX8Wrapper::Set_DX8_Texture(3,m_whiteTexture->Peek_D3D_Texture());
		}
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_ADD );
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 0);

	Bool doSparkles = true;

	if (m_trapezoidWaterPixelShader && doSparkles) {

		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();

		DX8Wrapper::Set_DX8_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();

		DX8Wrapper::Set_DX8_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());

		DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		
		D3DXMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, &curView);
		D3DXMATRIX scale;
		D3DXMatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		D3DXMATRIX destMatrix = inv * scale;
		D3DXMatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, destMatrix);

	}

	// Ronin @bugfix 10/03/2026 DX9: Route water sampler writes through wrapper so sampler cache stays coherent.
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Sampler_State(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);


	if (m_trapezoidWaterPixelShader){
		//DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(0,
		//D3DXVECTOR4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f), 1);

		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_trapezoidWaterPixelShader);
	}
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 28/09/2026 DX9: reflective water (WaterType 2) on the map's own polygons - drawTrapezoidWater and
// drawRiverWater with the WaterSea shaders instead of the ps_1_1 ones. docs/Water_Work.md §6.
//-------------------------------------------------------------------------------------------------
Bool WaterRenderObjClass::useReflection() const
{
	return m_waterType == WATER_TYPE_2_PVSHADER && m_dwWaveVertexShader && m_dwWavePixelShader && m_pReflectionTexture;
}

// Ronin @feature 29/09/2026 DX9: fill the WaterSea inputs this object owns and bind them (R3DWaterSea.h). water / edge: the
// standing texture and a river's edge fade. standing: the flat polygons - they alone take the swell and the refraction.
void WaterRenderObjClass::bindWaterSea(IDirect3DBaseTexture9 *water, IDirect3DBaseTexture9 *edge, Real edgeScaleU, Bool riverShroud, Bool standing)
{
	if (m_waterNormalTexture != nullptr && !m_waterNormalTexture->Is_Initialized())
		m_waterNormalTexture->Init();

	WaterSeaInputs in;
	in.bump             = m_pBumpTexture[(Int)m_fBumpFrame];
	in.reflection       = m_pReflectionTexture->Peek_D3D_Texture();
	in.mirrorInvW       = 1.0f / (Real)max(1, (Int)m_pReflectionTexture->Get_Width());	// Ronin @feature 08/10/2026 DX9: `mirrorsoft`
	in.mirrorInvH       = 1.0f / (Real)max(1, (Int)m_pReflectionTexture->Get_Height());
	in.water            = water;
	in.edge             = edge;
	in.normals          = (m_waterNormalTexture != nullptr) ? m_waterNormalTexture->Peek_D3D_Texture() : nullptr;
	in.bumpScale        = m_fBumpScale;
	in.bumpTilesPerUnit = PATCH_UV_SCALE / PATCH_SCALE;
	in.edgeScaleU       = edgeScaleU;
	in.clockSeconds     = m_riverVOrigin / 0.06f;	// m_riverVOrigin gains 0.06 a logic second (update)
	// Ronin @feature 29/09/2026 DX9: phase 1 - terrain vertex k sits at world (k - border) x MAP_XY_FACTOR, its texel centre
	// at uv (k + 0.5) / extent; a height byte is MAP_HEIGHT_SCALE world units (HeightMap.cpp's vertex build).
	in.heights          = m_heightTexture;
	in.heightScaleU     = (m_heightTexW > 0) ? 1.0f / (MAP_XY_FACTOR * (Real)m_heightTexW) : 0.0f;
	in.heightScaleV     = (m_heightTexH > 0) ? 1.0f / (MAP_XY_FACTOR * (Real)m_heightTexH) : 0.0f;
	in.heightOffsetU    = (m_heightTexW > 0) ? ((Real)m_heightBorder + 0.5f) / (Real)m_heightTexW : 0.0f;
	in.heightOffsetV    = (m_heightTexH > 0) ? ((Real)m_heightBorder + 0.5f) / (Real)m_heightTexH : 0.0f;
	in.heightUnit       = 255.0f * MAP_HEIGHT_SCALE;
	in.heightsVTF       = m_heightVTF;	// Ronin @feature 29/09/2026 DX9: phase 2
	in.swell            = standing;
	in.swellCell        = standing ? m_swellCellNow : 0.0f;	// Ronin @bugfix 03/10/2026 DX9: this polygon's real grid cell
	// Ronin @bugfix 03/10/2026 DX9: the standing polygons take the shroud in the PS (drawTrapezoidWater skips their second
	// pass), and since 11/10 the rivers. The sea plane keeps its own pass: none for it.
	in.shroud           = nullptr;
	in.shroudOffsetX    = 0.0f;	in.shroudOffsetY = 0.0f;
	in.shroudScaleX     = 0.0f;	in.shroudScaleY  = 0.0f;
	if (standing || riverShroud)
	{
		TextureClass *shroudTex = W3DShaderManager::getShroudTexture();
		if (shroudTex != nullptr && W3DShaderManager::getShroudMapState(&in.shroudOffsetX, &in.shroudOffsetY, &in.shroudScaleX, &in.shroudScaleY))
			in.shroud = shroudTex->Peek_D3D_Texture();
		if (in.shroud == nullptr)
		{
			in.shroudScaleX = 0.0f;	in.shroudScaleY = 0.0f;	// the PS reads a zero scale as no shroud
		}
	}
	// Ronin @feature 02/10/2026 DX9: phase 4 - standing: sea or lake (renderWater); a river; or the sea plane, which is sea.
	in.kind             = standing ? (m_standingIsSea ? WATER_KIND_SEA : WATER_KIND_LAKE)
	                               : (riverShroud ? WATER_KIND_RIVER : WATER_KIND_SEA);
	m_bindKind          = in.kind;	// Ronin @feature 02/10/2026 DX9: phase 4 - bindReflectiveShaders picks its variant from it
	// Ronin @feature 02/10/2026 DX9: phase 4 - the lake and the river take their own ripples when their maps loaded; else the
	// sea's.
	TextureClass *kindNormals = (in.kind == WATER_KIND_LAKE) ? m_waterNormalLake : (in.kind == WATER_KIND_RIVER) ? m_waterNormalRiver : nullptr;
	if (kindNormals != nullptr && !kindNormals->Is_Missing_Texture())
	{
		if (!kindNormals->Is_Initialized())
			kindNormals->Init();
		in.normals = kindNormals->Peek_D3D_Texture();
	}
	// Ronin @feature 29/09/2026 DX9: phase 3 - this frame's copy, when copyRefraction made one.
	const Bool refract  = (standing && m_refractionOK && m_refractionTexture != nullptr) ? TRUE : FALSE;
	in.scene            = refract ? m_refractionTexture->Peek_D3D_Base_Texture() : nullptr;
	in.sceneInvW        = (refract && m_refractionW > 0) ? 1.0f / (Real)m_refractionW : 0.0f;
	in.sceneInvH        = (refract && m_refractionH > 0) ? 1.0f / (Real)m_refractionH : 0.0f;
	float jx = 0.0f, jy = 0.0f;
	R3DTaa::getJitterNDC(&jx, &jy);	// Ronin @bugfix 30/09/2026 DX9: 0 unless the main scene is drawing jittered
	in.jitterX          = jx;
	in.jitterY          = jy;
	// Ronin @feature 03/10/2026 DX9: phase 5 - this frame's wakes, on standing water (renderWakes)
	in.wake             = (standing && m_wakeLive) ? m_wakeTexture : nullptr;
	in.wakeScale        = m_wakeScale;
	in.wakeOffsetU      = m_wakeOffsetU;
	in.wakeOffsetV      = m_wakeOffsetV;
	in.wakeVTF          = (m_wakeFormat == 2) ? TRUE : FALSE;
	WaterSea_Bind(in);
}

// Ronin @feature 29/09/2026 DX9: phase 3 - copy render target 0 just before the flat water draws, for its refraction.
// Once a frame, and only while the shader owns the blend (depth on, not additive).
void WaterRenderObjClass::copyRefraction()
{
	m_refractionOK = FALSE;
	TheWaterStats.refraction = 0;
	if (!TheWaterSeaGlobal.refraction || !useReflection() || !WaterSea_DepthActive(m_heightTexture != nullptr) ||
		TheWaterTransparency->m_additiveBlend)
		return;

	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DSurface9 *rt = nullptr;
	if (dev == nullptr || FAILED(dev->GetRenderTarget(0, &rt)) || rt == nullptr)
		return;
	TheWaterStats.refraction = 1;
	D3DSURFACE_DESC desc;
	if (SUCCEEDED(rt->GetDesc(&desc)))
	{
		if (m_refractionTexture == nullptr || m_refractionW != desc.Width || m_refractionH != desc.Height)
		{
			REF_PTR_RELEASE(m_refractionTexture);
			m_refractionW = m_refractionH = 0;
			m_refractionTexture = NEW_REF(TextureClass, (desc.Width, desc.Height, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1,
				TextureClass::POOL_DEFAULT, true));
			if (m_refractionTexture->Peek_D3D_Base_Texture() == nullptr)
			{
				REF_PTR_RELEASE(m_refractionTexture);	// a macro with its own if - keep the braces
			}
			else
			{
				m_refractionW = desc.Width;
				m_refractionH = desc.Height;
			}
		}
		if (m_refractionTexture != nullptr)
		{
			IDirect3DSurface9 *dst = m_refractionTexture->Get_D3D_Surface_Level();
			if (dst != nullptr)
			{
				m_refractionOK = SUCCEEDED(dev->StretchRect(rt, nullptr, dst, nullptr, D3DTEXF_NONE)) ? TRUE : FALSE;
				dst->Release();
			}
		}
	}
	rt->Release();
	if (m_refractionOK)
		TheWaterStats.refraction = 2;
}

// Ronin @bugfix 01/10/2026 DX9: a standing polygon with a point within 4 cells of the playable area's edge (or past it) is
// sea; the rest are lakes. The playable area runs 0..(extent - 1 - 2 x border) x MAP_XY_FACTOR (the height texture's).
Bool WaterRenderObjClass::isSeaPolygon(PolygonTrigger *pTrig) const
{
	if (pTrig == nullptr || m_heightTexW <= 0 || m_heightTexH <= 0)
		return FALSE;
	const Real maxX   = (Real)(m_heightTexW - 1 - 2 * m_heightBorder) * MAP_XY_FACTOR;
	const Real maxY   = (Real)(m_heightTexH - 1 - 2 * m_heightBorder) * MAP_XY_FACTOR;
	const Real margin = 4.0f * MAP_XY_FACTOR;
	for (Int k = 0; k < pTrig->getNumPoints(); k++)
	{
		const ICoord3D *p = pTrig->getPoint(k);
		if ((Real)p->x <= margin || (Real)p->y <= margin || (Real)p->x >= maxX - margin || (Real)p->y >= maxY - margin)
			return TRUE;
	}
	return FALSE;
}

// Ronin @feature 29/09/2026 DX9: phase 1 - the map's terrain heights as a MANAGED texture (R32F or L8), a texel per
// terrain vertex, rebuilt per map. A terrain change after load (a crater) is not followed.
void WaterRenderObjClass::buildHeightTexture()
{
	SAFE_RELEASE(m_heightTexture);
	m_heightTexW = m_heightTexH = m_heightBorder = 0;
	m_heightVTF = FALSE;
	TheWaterStats.heightW = TheWaterStats.heightH = 0;
	TheWaterStats.heightVTF = FALSE;

	WorldHeightMap *map = (TheTerrainRenderObject != nullptr) ? TheTerrainRenderObject->getMap() : nullptr;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (map == nullptr || map->getDataPtr() == nullptr || dev == nullptr)
		return;
	const Int w = map->getXExtent();
	const Int h = map->getYExtent();
	if (w <= 0 || h <= 0)
		return;

	// Ronin @feature 29/09/2026 DX9: phase 2 - R32F when the vertex shader can sample it and the pixel shader filter it (the
	// swell reads the depth per vertex); else L8, as phase 1 had it. Both hold 0..1, so the pixel shader is the same.
	Bool vtf = FALSE;
	IDirect3D9 *d3d = nullptr;
	D3DDEVICE_CREATION_PARAMETERS cp;
	D3DDISPLAYMODE mode;
	if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d != nullptr)
	{
		if (SUCCEEDED(dev->GetCreationParameters(&cp)) && SUCCEEDED(dev->GetDisplayMode(0, &mode)))
			vtf = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format, D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_R32F)) &&
				SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_R32F));
		d3d->Release();
	}

	IDirect3DTexture9 *tex = nullptr;
	DWORD heightUsage = 0;	// Ronin @feature 08/11/2026 DX9Ex: a lockable default-pool texture on 9Ex
	const D3DPOOL heightPool = DX8Wrapper::_Get_Managed_Pool(D3DPOOL_MANAGED, &heightUsage);
	if (FAILED(dev->CreateTexture(w, h, 1, heightUsage, vtf ? D3DFMT_R32F : D3DFMT_L8, heightPool, &tex, nullptr)) || tex == nullptr)
		return;
	D3DLOCKED_RECT lr;
	if (FAILED(tex->LockRect(0, &lr, nullptr, 0)))
	{
		tex->Release();
		return;
	}
	const UnsignedByte *src = map->getDataPtr();	// row-major, m_data[y * width + x]
	for (Int y = 0; y < h; y++)
	{
		if (vtf)
		{
			float *row = (float *)((UnsignedByte *)lr.pBits + y * lr.Pitch);
			for (Int x = 0; x < w; x++)
				row[x] = (float)src[y * w + x] * (1.0f / 255.0f);
		}
		else
			memcpy((UnsignedByte *)lr.pBits + y * lr.Pitch, src + y * w, w);
	}
	tex->UnlockRect(0);

	m_heightTexture = tex;
	m_heightTexW    = w;
	m_heightTexH    = h;
	m_heightBorder  = map->getBorderSizeInline();
	m_heightVTF     = vtf;
	TheWaterStats.heightW = w;	// Ronin @diagnostic 29/09/2026 DX9: [WATER] row
	TheWaterStats.heightH = h;
	TheWaterStats.heightVTF = vtf;
}

// Ronin @feature 28/09/2026 DX9: blend state and material for one reflective draw, then bindWaterSea. The shaders go on in
// bindReflectiveShaders, AFTER the draw's BindLayoutFVF (it clears the vertex shader).
void WaterRenderObjClass::setupReflectiveWater(Bool river)
{
	ShaderClass::Invalidate();	// the same one-shot re-apply setupFlatWaterShader needs, or blending can stay off
	if (!TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Texture(0, nullptr);	// the textures go on raw in bindWaterSea; W3D must not re-apply one over them
	DX8Wrapper::Apply_Render_State_Changes();	// view and projection current before bindWaterSea reads them

	if (!m_riverTexture->Is_Initialized())
		m_riverTexture->Init();
	if (!m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();

	bindWaterSea(m_riverTexture->Peek_D3D_Texture(),
		river ? m_riverAlphaEdge->Peek_D3D_Texture() : m_whiteTexture->Peek_D3D_Texture(),
		river ? 1.0f / HEIGHT_TO_USE : 0.0f, river, !river);	// Ronin @feature 29/09/2026 DX9: the swell on standing water only
}


void WaterRenderObjClass::bindReflectiveShaders()
{
	m_pDev->SetVertexShader(m_dwWaveVertexShader);	// the FVF BindLayoutFVF set serves as its declaration
	m_pDev->SetPixelShader(pickWavePS(m_bindKind));	// Ronin @feature 02/10/2026 DX9: phase 4 - this draw's variant
}

// Ronin @feature 02/10/2026 DX9: phase 4 - the WaterSea variant for a kind: rivers take the river build, `water view` the
// views build. A missing variant falls back to the standing shader (river views -> river -> standing).
IDirect3DPixelShader9 *WaterRenderObjClass::pickWavePS(Int kind) const
{
	const Bool views = (TheWaterSeaGlobal.view != 0) ? TRUE : FALSE;
	if (kind == WATER_KIND_RIVER)
	{
		if (views && m_wavePSRiverViews)
			return m_wavePSRiverViews;
		if (!views && m_wavePSRiver)
			return m_wavePSRiver;
	}
	else if (views && m_wavePSViews)
		return m_wavePSViews;
	return m_dwWavePixelShader;
}

void WaterRenderObjClass::unbindReflectiveShaders(DWORD fvf)
{
	m_pDev->SetPixelShader(nullptr);
	DX8Wrapper::BindLayoutFVF(fvf, "WaterRenderObjClass::unbindReflectiveShaders");	// clears the vertex shader too
	WaterSea_Unbind();	// Ronin @feature 29/09/2026 DX9: drop the WaterSea texture references (R3DWaterSea.h)
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 03/10/2026 DX9: phase 5 - units that float ride the swell. The CPU's copy of the surface: the same waves
// (WaterSea_Waves), clock, depth ramp and trough stop as WaterSea_vs.hlsl. Draw only - nothing here reaches the logic.
//-------------------------------------------------------------------------------------------------

// The standing water over a point: the highest sea or lake polygon there, as getWaterHeight picks it. FALSE for land or
// a river.
Bool WaterRenderObjClass::standingWater(Real x, Real y, Int *kind, Real *level) const
{
	if (!useReflection())
		return FALSE;
	ICoord3D iLoc;
	iLoc.x = REAL_TO_INT_FLOOR(x + 0.5f);
	iLoc.y = REAL_TO_INT_FLOOR(y + 0.5f);
	iLoc.z = 0;
	PolygonTrigger *found = nullptr;
	for (PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext())
	{
		if (!pTrig->isWaterArea() || !pTrig->pointInTrigger(iLoc))
			continue;
		if (found == nullptr || pTrig->getPoint(0)->z >= found->getPoint(0)->z)
			found = pTrig;
	}
	if (found == nullptr || found->isRiver())
		return FALSE;
	*kind  = isSeaPolygon(found) ? WATER_KIND_SEA : WATER_KIND_LAKE;
	*level = (Real)found->getPoint(0)->z;
	return TRUE;
}

// How far the swell lifts the surface above `level` at a point. The waves also move it sideways, so the point's start
// is found first: six steps (py check, Water_Work.md §6).
Real WaterRenderObjClass::swellHeight(Real x, Real y, Int kind, Real level) const
{
	if (TheTerrainRenderObject == nullptr)
		return 0.0f;
	const WaterSeaTuning &t = WaterSea_Profile(kind);
	const Real ground = TheTerrainRenderObject->getHeightMapHeight(x, y, nullptr);
	Real weight = (t.swellDepth > 0.0f) ? (level - ground) / t.swellDepth : 0.0f;
	if (weight <= 0.0f)
		return 0.0f;
	if (weight > 1.0f)
		weight = 1.0f;
	const Real lastCell = (kind == WATER_KIND_SEA) ? m_swellCellSea : m_swellCellLake;
	WaterSeaWaveNow wave[WATERSEA_WAVE_COUNT];
	WaterSea_Waves(kind, (lastCell > 0.0f) ? lastCell : t.swellCell, wave);
	const Real seconds = m_riverVOrigin / 0.06f;	// bindWaterSea's clock
	Real px = x, py = y;
	for (Int step = 0; step < 6; step++)
	{
		Real ox = 0.0f, oy = 0.0f;
		for (Int w = 0; w < WATERSEA_WAVE_COUNT; w++)
		{
			if (!wave[w].moves)
				continue;
			const Real c = cosf(wave[w].k * (wave[w].dx * px + wave[w].dy * py) - wave[w].omega * seconds);
			ox += wave[w].qa * wave[w].dx * c;
			oy += wave[w].qa * wave[w].dy * c;
		}
		px = x - ox * weight;
		py = y - oy * weight;
	}
	Real height = 0.0f;
	for (Int w = 0; w < WATERSEA_WAVE_COUNT; w++)
	{
		if (wave[w].moves)
			height += wave[w].a * sinf(wave[w].k * (wave[w].dx * px + wave[w].dy * py) - wave[w].omega * seconds);
	}
	height *= weight;
	// the VS's trough stop: not below the seabed + 0.5, and never raised by it
	const Real stop = (level < ground + 0.5f) ? level : ground + 0.5f;
	if (level + height < stop)
		height = stop - level;
	return height;
}

// Lift and tilt a floating unit's transform from four points 0.7 of its half size out: their mean and their differences,
// so a hull longer than a wave rides over it. `ride` scales it.
void WaterRenderObjClass::rideSwell(Matrix3D &mtx, Real halfLength, Real halfWidth, Int kind, Real level) const
{
	// Ronin @feature 03/10/2026 DX9: a big hull rides gently: full up to a 50-unit half length (the PT boat is 48), then
	// 50 / it - the ferry (72) 0.7, the battleship (200) 0.25. It bobbed like a patrol boat.
	const Real RIDE_FULL_HALF_LENGTH = 50.0f;
	const Real ride = TheWaterSeaGlobal.ride * ((halfLength > RIDE_FULL_HALF_LENGTH) ? RIDE_FULL_HALF_LENGTH / halfLength : 1.0f);
	const Real cx = mtx.Get_X_Translation();
	const Real cy = mtx.Get_Y_Translation();
	if (ride <= 0.0f)
		return;
	const Vector3 ahead = mtx.Get_X_Vector();
	const Vector3 aside = mtx.Get_Y_Vector();
	const Real aheadLen = sqrtf(ahead.X * ahead.X + ahead.Y * ahead.Y);
	const Real asideLen = sqrtf(aside.X * aside.X + aside.Y * aside.Y);
	if (aheadLen < 0.001f || asideLen < 0.001f)
		return;	// on its nose or its side: leave it
	const Real l  = (halfLength * 0.7f > 1.0f) ? halfLength * 0.7f : 1.0f;
	const Real w  = (halfWidth  * 0.7f > 1.0f) ? halfWidth  * 0.7f : 1.0f;
	const Real ax = ahead.X / aheadLen * l, ay = ahead.Y / aheadLen * l;
	const Real sx = aside.X / asideLen * w, sy = aside.Y / asideLen * w;
	const Real hBow   = swellHeight(cx + ax, cy + ay, kind, level);
	const Real hStern = swellHeight(cx - ax, cy - ay, kind, level);
	const Real hSide  = swellHeight(cx + sx, cy + sy, kind, level);	// the side its Y axis points to
	const Real hOther = swellHeight(cx - sx, cy - sy, kind, level);
	Real pitch = atan2f(hBow - hStern, 2.0f * l) * ride;	// bow up
	Real roll  = atan2f(hSide - hOther, 2.0f * w) * ride;	// its Y side up
	const Real TILT_MAX = 0.35f;
	if (pitch >  TILT_MAX) pitch =  TILT_MAX;
	if (pitch < -TILT_MAX) pitch = -TILT_MAX;
	if (roll  >  TILT_MAX) roll  =  TILT_MAX;
	if (roll  < -TILT_MAX) roll  = -TILT_MAX;
	mtx.Adjust_Z_Translation(0.25f * (hBow + hStern + hSide + hOther) * ride);
	mtx.Rotate_Y(-pitch);	// Matrix3D::Rotate_Y(+) turns the X axis down
	mtx.Rotate_X(roll);		// Matrix3D::Rotate_X(+) turns the Y axis up
}

// Which units float and leave a wake: vehicles, boats, and whatever is flagged OVER_WATER - on the ground layer. Not
// structures, aircraft or projectiles; a unit carried by another leaves no wake.
void R3DWater_RideSwell(const Drawable *draw, Matrix3D &mtx)
{
	if (TheWaterRenderObj == nullptr || draw == nullptr || (TheWaterSeaGlobal.ride <= 0.0f && TheWaterSeaGlobal.wakes <= 0.0f))
		return;
	const Object *obj = draw->getObject();
	if (obj == nullptr || obj->getLayer() != LAYER_GROUND)
		return;
	if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_AIRCRAFT) || obj->isKindOf(KINDOF_PROJECTILE))
		return;
	if (!obj->isKindOf(KINDOF_VEHICLE) && !obj->isKindOf(KINDOF_BOAT) && !draw->getModelConditionFlags().test(MODELCONDITION_OVER_WATER))
		return;
	const GeometryInfo &geom = obj->getGeometryInfo();
	TheWaterRenderObj->floatUnit((UnsignedInt)draw->getID(), mtx, geom.getMajorRadius(), geom.getMinorRadius(),
		(obj->getContainedBy() == nullptr) ? TRUE : FALSE);
}

// A unit in the water this frame: its wake trail grows, then it rides the swell (the trail takes the matrix before the
// ride tilts it).
void WaterRenderObjClass::floatUnit(UnsignedInt drawableID, Matrix3D &mtx, Real halfLength, Real halfWidth, Bool wake)
{
	const Real cx = mtx.Get_X_Translation();
	const Real cy = mtx.Get_Y_Translation();
	Int  kind  = WATER_KIND_SEA;
	Real level = 0.0f;
	if (!standingWater(cx, cy, &kind, &level))
		return;
	if (wake && TheWaterSeaGlobal.wakes > 0.0f && m_wakeVS != nullptr && m_wakePS != nullptr && TheTerrainRenderObject != nullptr)
		noteWake(drawableID, mtx, halfLength, halfWidth, level - TheTerrainRenderObject->getHeightMapHeight(cx, cy, nullptr));
	if (TheWaterSeaGlobal.ride > 0.0f && WaterSea_SwellActive(kind, m_heightVTF))
		rideSwell(mtx, halfLength, halfWidth, kind, level);
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 03/10/2026 DX9: phase 5 - wakes, the trails. A unit in standing water drops a point at its centre every
// WATERSEA_WAKE_STEP units (sooner in a turn); renderWakes draws them. Draw only, as the ride.
//-------------------------------------------------------------------------------------------------

void WaterRenderObjClass::clearWakes()
{
	for (Int k = 0; k < WAKE_TRAILS; k++)
	{
		WakeTrail &t = m_wake[k];
		t.id    = 0;
		t.count = 0;
		t.first = 0;
		t.seen  = 0.0f;
		t.halfLength = t.halfWidth = 0.0f;
		t.lastX = t.lastY = 0.0f;
		t.speed = 0.0f;
		t.lead  = 1.0f;
		t.headX = t.headY = 0.0f;
		t.dirX  = t.markX = 1.0f;
		t.dirY  = t.markY = 0.0f;
		t.headStrength = 0.0f;
		t.idle  = 0.0f;
		t.idleScale = 0.0f;
		t.peak  = 0.0f;
	}
	for (Int k = 0; k < WAKE_RIPPLES; k++)
		m_ripple[k].life = 0.0f;	// Ronin @feature 04/10/2026 DX9: free
	m_rippleNext = 0;
	m_wakeLive = FALSE;
}

// The water draws this unit's wake: it was in the water within the last half second. `water oldwakes 1`: never.
Bool WaterRenderObjClass::ownsWake(UnsignedInt drawableID) const
{
	if (drawableID == 0 || TheWaterSeaGlobal.oldWakes || TheWaterSeaGlobal.wakes <= 0.0f || m_wakeFormat <= 0 ||
		m_wakeVS == nullptr || m_wakePS == nullptr)
		return FALSE;
	const Real now = m_riverVOrigin / 0.06f;
	for (Int k = 0; k < WAKE_TRAILS; k++)
	{
		if (m_wake[k].id == drawableID)
			return (now >= m_wake[k].seen && now - m_wake[k].seen < 0.5f) ? TRUE : FALSE;
	}
	return FALSE;
}

bool R3DWater_OwnsWake(unsigned int drawableID)
{
	return TheWaterRenderObj != nullptr && TheWaterRenderObj->ownsWake(drawableID);
}

// Ronin @feature 04/10/2026 DX9: a ring sets out from a hull's outline - a capsule: the hull's straight part, its
// half beam all round. The oldest ripple makes room.
void WaterRenderObjClass::addRipple(Real x, Real y, Real dirX, Real dirY, Real halfLength, Real halfWidth, Real now, Real life, Real speed, Real height, Real ahead)
{
	if (height <= 0.0f)
		return;
	WakeRipple &r = m_ripple[m_rippleNext];
	m_rippleNext = (m_rippleNext + 1) % WAKE_RIPPLES;
	r.x       = x;
	r.y       = y;
	r.dirX    = dirX;
	r.dirY    = dirY;
	r.halfSeg = (0.8f * halfLength > halfWidth) ? 0.8f * halfLength - halfWidth : 0.0f;
	r.start   = halfWidth - 0.7f * WATERSEA_SURGE_WIDTH;	// Ronin @bugfix 04/10/2026 DX9: its crest sets out ON the outline
	r.time    = now;
	r.life    = life;
	r.speed   = speed;
	r.height  = height;
	r.ahead   = ahead;
}

// One frame of a unit in water `depth` deep. Its speed is smoothed over a quarter second: the transform moves in logic
// steps, so a frame's own movement is all or nothing.
void WaterRenderObjClass::noteWake(UnsignedInt drawableID, const Matrix3D &mtx, Real halfLength, Real halfWidth, Real depth)
{
	const Real WAKE_MIN_DEPTH = 1.0f;	// shallower: wading, no wake
	const Real WAKE_FULL_DEPTH = 4.0f;	// full strength from here down
	if (drawableID == 0 || depth < WAKE_MIN_DEPTH)
		return;
	const Vector3 ahead = mtx.Get_X_Vector();
	const Real aheadLen = sqrtf(ahead.X * ahead.X + ahead.Y * ahead.Y);
	if (aheadLen < 0.001f)
		return;
	const Real dx  = ahead.X / aheadLen;
	const Real dy  = ahead.Y / aheadLen;
	const Real cx  = mtx.Get_X_Translation();
	const Real cy  = mtx.Get_Y_Translation();
	const Real now = m_riverVOrigin / 0.06f;	// bindWaterSea's clock

	// its trail; else a free one; else the one unseen longest
	WakeTrail *trail = nullptr;
	WakeTrail *spare = &m_wake[0];
	for (Int k = 0; k < WAKE_TRAILS && trail == nullptr; k++)
	{
		WakeTrail &t = m_wake[k];
		if (t.id == drawableID)
			trail = &t;
		else if (spare->id != 0 && (t.id == 0 || t.seen < spare->seen))
			spare = &t;
	}
	Bool fresh = FALSE;
	if (trail == nullptr)
	{
		if (spare->id != 0 && now >= spare->seen && now - spare->seen < 0.5f)
			return;	// every trail belongs to a unit still in the water: this one goes without
		trail = spare;
		trail->id   = drawableID;
		trail->seen = now;
		fresh = TRUE;
	}
	const Real dt    = now - trail->seen;
	const Real mx    = cx - trail->lastX;
	const Real my    = cy - trail->lastY;
	const Real moved = sqrtf(mx * mx + my * my);
	// not drawn for half a second (off screen, out of the water), the clock went back, or it jumped: a new trail
	if (fresh || dt < 0.0f || dt > 0.5f || moved > 2.0f * halfLength + 50.0f)
	{
		trail->count = 0;
		trail->first = 0;
		trail->speed = 0.0f;
		trail->lead  = 1.0f;
		trail->lastX = cx;
		trail->lastY = cy;
		// Ronin @feature 04/10/2026 DX9: no surge from before it went out of sight; its rings ease in once its speed is known
		trail->peak = 0.0f;
		trail->idle = 0.0f;
	}
	else if (dt > 1.0e-4f)
	{
		if ((mx * dx + my * dy) * trail->lead < -0.05f)
		{
			trail->lead  = -trail->lead;	// the other end leads now: reversing
			trail->count = 0;
			trail->first = 0;
		}
		trail->speed += (moved / dt - trail->speed) * (1.0f - expf(-dt / 0.25f));
		trail->lastX  = cx;
		trail->lastY  = cy;
	}
	trail->seen       = now;
	trail->halfLength = halfLength;
	trail->halfWidth  = halfWidth;
	// Ronin @bugfix 04/10/2026 DX9: the trail is the centre's path; the leading end is added when it is drawn
	trail->headX      = cx;
	trail->headY      = cy;
	trail->dirX       = dx * trail->lead;
	trail->dirY       = dy * trail->lead;

	// how strong: by its speed (a full wake at a boat's 40), its beam, and the depth it runs in
	Real pace = trail->speed / WATERSEA_WAKE_SPEED;
	if (pace > 1.25f) pace = 1.25f;
	pace = (pace > 0.0f) ? powf(pace, 0.75f) : 0.0f;
	Real size = halfWidth / WATERSEA_WAKE_BEAM;
	if (size < 0.6f) size = 0.6f;
	if (size > 2.0f) size = 2.0f;
	const Real deep = (depth < WAKE_FULL_DEPTH) ? depth / WAKE_FULL_DEPTH : 1.0f;
	trail->headStrength = pace * size * deep;

	// Ronin @feature 04/10/2026 DX9: what a hull does without a wake. A unit that was fast and is down to
	// a third of it sends a surge on ahead - its own wake overtaking it; a unit at rest has rings round it.
	Real share = trail->speed / WATERSEA_WAKE_SPEED;	// of a boat's full speed
	if (share > 1.25f) share = 1.25f;
	if (share > trail->peak)
		trail->peak = share;
	else if (dt > 0.0f && dt < 0.5f && trail->peak > 0.0f)
		trail->peak -= 0.1f * dt;	// an old sprint is forgotten in time
	if (TheWaterSeaGlobal.wakeSurge > 0.0f && trail->peak >= WATERSEA_SURGE_FROM && share < WATERSEA_SURGE_DROP * trail->peak)
	{
		addRipple(cx, cy, trail->dirX, trail->dirY, halfLength, halfWidth, now, WATERSEA_SURGE_LIFE,
			WATERSEA_SURGE_SPEED * trail->peak, WATERSEA_SURGE_HEIGHT * trail->peak * size * deep * TheWaterSeaGlobal.wakeSurge, 1.0f);
		trail->peak = 0.0f;
	}
	// Ronin @bugfix 04/10/2026 DX9: the rings are a train round the unit while it rests (renderWakes), eased in and out
	// over about a second - not one ring every 2.4 s.
	Real rest = 1.0f - share / WATERSEA_RING_REST;
	if (rest < 0.0f) rest = 0.0f;
	if (dt > 0.0f && dt < 0.5f)
		trail->idle += (rest - trail->idle) * (1.0f - expf(-dt / 0.8f));
	trail->idleScale = size * deep;

	Real path = 0.0f;
	if (trail->count > 0)
	{
		const WakePoint &last = trail->pt[(trail->first + trail->count - 1) % WAKE_POINTS];
		const Real px  = trail->headX - last.x;
		const Real py  = trail->headY - last.y;
		const Real run = sqrtf(px * px + py * py);
		// Ronin @bugfix 04/10/2026 DX9: every STEP units, or sooner in a turn - 6 degrees a point keeps the arms round
		const Bool turned = (trail->dirX * trail->markX + trail->dirY * trail->markY < WATERSEA_WAKE_TURN_COS) ? TRUE : FALSE;
		if (run < WATERSEA_WAKE_STEP && !(turned && run >= WATERSEA_WAKE_STEP_TURN))
			return;
		path = last.path + run;
	}
	trail->markX = trail->dirX;
	trail->markY = trail->dirY;
	if (trail->count == WAKE_POINTS)
	{
		trail->first = (trail->first + 1) % WAKE_POINTS;	// full: the oldest goes
		trail->count--;
	}
	WakePoint &p = trail->pt[(trail->first + trail->count) % WAKE_POINTS];
	p.x        = trail->headX;
	p.y        = trail->headY;
	p.time     = now;
	p.path     = path;
	p.strength = trail->headStrength;
	trail->count++;
}

// Ronin @bugfix 03/10/2026 DX9: phase 5 - the swell's vertices go where the camera looks (the grid was even over the
// polygon and capped at 100 x 100). Three helpers for drawTrapezoidWater.

// Ronin @bugfix 03/10/2026 DX9: the budget is one draw's 16-bit index count - cells in u x cells in v - not 100 cells an
// axis: a view 1000 units deep passed 99 cells at `swellcell` 10 and doubled that axis alone.
enum
{
	WATER_SWELL_AXIS_MAX  = 512,	// vertices along one axis
	WATER_SWELL_CELLS_MAX = 10900,	// cells a draw: 65400 indices
};

// One axis of the grid, 0..1 along the polygon: fixed multiples of `cell` across the part in view, a coarse cell each
// side. Returns the count, or 0 past WATER_SWELL_AXIS_MAX (the caller doubles the cell).
static Int waterSwellAxis(Real lo, Real hi, Real cell, Real *pos)
{
	pos[0] = 0.0f;
	pos[1] = 1.0f;
	if (!(hi > 0.0f && lo < 1.0f) || cell >= 1.0f || cell <= 0.0f)
		return 2;	// none of this axis in view, or the polygon is under one cell
	if (lo < 0.0f) lo = 0.0f;
	if (hi > 1.0f) hi = 1.0f;
	const Int k0 = (Int)floorf(lo / cell);
	const Int k1 = (Int)ceilf(hi / cell);
	if (k1 - k0 + 1 > WATER_SWELL_AXIS_MAX - 2)
		return 0;
	Int n = 1;
	for (Int k = k0; k <= k1; k++)
	{
		const Real x = (Real)k * cell;
		if (x > pos[n - 1] + 0.25f * cell && x < 1.0f - 0.25f * cell)
			pos[n++] = x;
	}
	pos[n++] = 1.0f;
	return n;
}

// World xy -> the quad's (u, v): Newton on P = o + U u + V v + u v D, exact in one step for a parallelogram. FALSE when it
// does not land on the point (a folded quad): the caller keeps the whole range.
static Bool waterQuadUV(const Vector3 &o, const Vector3 &U, const Vector3 &V, const Vector3 &D, Real px, Real py, Real *u, Real *v)
{
	Real cu = 0.5f, cv = 0.5f;
	for (Int it = 0; it < 6; it++)
	{
		const Real fx  = o.X + U.X * cu + V.X * cv + cu * cv * D.X - px;
		const Real fy  = o.Y + U.Y * cu + V.Y * cv + cu * cv * D.Y - py;
		const Real a   = U.X + cv * D.X, b = V.X + cu * D.X;
		const Real c   = U.Y + cv * D.Y, d = V.Y + cu * D.Y;
		const Real det = a * d - b * c;
		if (!(fabsf(det) > 1.0e-6f))
			return FALSE;
		cu -= ( d * fx - b * fy) / det;
		cv -= (-c * fx + a * fy) / det;
	}
	const Real rx = o.X + U.X * cu + V.X * cv + cu * cv * D.X - px;
	const Real ry = o.Y + U.Y * cu + V.Y * cv + cu * cv * D.Y - py;
	*u = cu;
	*v = cv;
	return (rx * rx + ry * ry < 4.0f && fabsf(cu) < 1.0e4f && fabsf(cv) < 1.0e4f) ? TRUE : FALSE;	// NaN fails too
}

// The camera's footprint on the plane z = level: where the four screen corners' rays meet it, or their far ends. The
// matrices are the ones WaterSea_Bind draws with.
static Bool waterCameraFootprint(Real level, Real fx[4], Real fy[4])
{
	D3DXMATRIX view, proj, viewProj, inv;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, proj);
	D3DXMatrixMultiply(&viewProj, &view, &proj);
	if (D3DXMatrixInverse(&inv, nullptr, &viewProj) == nullptr)
		return FALSE;
	static const float corner[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
	for (Int c = 0; c < 4; c++)
	{
		const D3DXVECTOR3 n(corner[c][0], corner[c][1], 0.0f), f(corner[c][0], corner[c][1], 1.0f);
		D3DXVECTOR3 wn, wf;
		D3DXVec3TransformCoord(&wn, &n, &inv);
		D3DXVec3TransformCoord(&wf, &f, &inv);
		const float dz = wf.z - wn.z;
		float t = (fabsf(dz) > 1.0e-4f) ? (level - wn.z) / dz : 1.0f;
		if (!(t >= 0.0f && t <= 1.0f))
			t = 1.0f;	// the ray leaves the view before the water: its far end
		fx[c] = wn.x + (wf.x - wn.x) * t;
		fy[c] = wn.y + (wf.y - wn.y) * t;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 03/10/2026 DX9: phase 5 - wakes, the drawing. Once a frame, before the water: the trails as ribbons into
// m_wakeTexture, over a square window on the view. Device state goes on raw and comes back raw.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWakes()
{
	struct WakeVertex
	{
		float x, y, z;
		float across[4];	// distance from the track, the V's half-width, age 0..1, strength (z above: the rim)
		float along[4];		// the left normal xy, distance astern of the stern, the half beam
	};
	const DWORD WAKE_FVF = D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1);

	m_wakeLive = FALSE;
	TheWaterStats.wakeState = 0;
	if (TheWaterSeaGlobal.wakes <= 0.0f || !useReflection() || m_wakeVS == nullptr || m_wakePS == nullptr)
		return;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// asked once: a half-float target the card can blend into and filter (1), and its vertex shader read (2)
	if (m_wakeFormat < 0)
	{
		m_wakeFormat = 0;
		IDirect3D9 *d3d = nullptr;
		D3DDEVICE_CREATION_PARAMETERS cp;
		D3DDISPLAYMODE mode;
		if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d != nullptr)
		{
			if (SUCCEEDED(dev->GetCreationParameters(&cp)) && SUCCEEDED(dev->GetDisplayMode(0, &mode)) &&
				SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
					D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)) &&
				SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
					D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)))
			{
				m_wakeFormat = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
					D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)) ? 2 : 1;
			}
			d3d->Release();
		}
	}
	TheWaterStats.wakeState = m_wakeFormat + 1;
	if (m_wakeFormat <= 0)
		return;

	// age the trails: points past their life go; a trail with none whose unit no longer draws is freed
	const Real now = m_riverVOrigin / 0.06f;
	Int live = 0;
	for (Int k = 0; k < WAKE_TRAILS; k++)
	{
		WakeTrail &t = m_wake[k];
		if (t.id == 0)
			continue;
		if (now < t.seen)
		{
			t.id = 0;	// the clock went back
			continue;
		}
		while (t.count > 0 && now - t.pt[t.first].time > WATERSEA_WAKE_LIFE)
		{
			t.first = (t.first + 1) % WAKE_POINTS;
			t.count--;
		}
		if (t.count == 0 && now - t.seen > 0.5f)
			t.id = 0;
		else if (t.count >= 2)
			live++;
		else if (t.count == 1 && now - t.seen < 0.25f)
		{
			// one point and the leading end: a ribbon once they are apart (a unit at rest keeps one point and no wake)
			const Real hx = t.headX - t.pt[t.first].x;
			const Real hy = t.headY - t.pt[t.first].y;
			if (hx * hx + hy * hy > 0.25f)
				live++;
		}
	}
	// Ronin @feature 04/10/2026 DX9: and the rings: one past its life (or from before the clock went back) is free
	Int rings = 0;
	for (Int k = 0; k < WAKE_RIPPLES; k++)
	{
		WakeRipple &r = m_ripple[k];
		if (r.life > 0.0f && (now < r.time || now - r.time >= r.life))
			r.life = 0.0f;
		if (r.life > 0.0f)
			rings++;
	}
	// Ronin @feature 04/10/2026 DX9: and a unit at rest, still in sight, has its train
	if (TheWaterSeaGlobal.wakeRings > 0.0f)
	{
		for (Int k = 0; k < WAKE_TRAILS; k++)
			if (m_wake[k].id != 0 && m_wake[k].idle > 0.02f && now - m_wake[k].seen < 0.5f)
				rings++;
	}
	if (live == 0 && rings == 0)
		return;

	if (m_wakeTexture == nullptr && FAILED(dev->CreateTexture(WATERSEA_WAKE_SIZE, WATERSEA_WAKE_SIZE, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &m_wakeTexture, nullptr)))
	{
		m_wakeTexture = nullptr;
		m_wakeFormat  = 0;
		return;
	}

	// the window: a square over the view's footprint on the water, WATERSEA_WAKE_WINDOW x 2^n a side so a texel's size
	// only changes with the zoom, and snapped to its texels so a still wake does not shimmer as the camera moves
	const Real cap = WATERSEA_WAKE_WINDOW * 8.0f;
	Real centreX = 0.0f, centreY = 0.0f, span = cap;
	Real fx[4], fy[4];
	if (waterCameraFootprint(m_level, fx, fy))
	{
		Real minX = fx[0], maxX = fx[0], minY = fy[0], maxY = fy[0];
		for (Int c = 1; c < 4; c++)
		{
			if (fx[c] < minX) minX = fx[c];
			if (fx[c] > maxX) maxX = fx[c];
			if (fy[c] < minY) minY = fy[c];
			if (fy[c] > maxY) maxY = fy[c];
		}
		centreX = 0.5f * (minX + maxX);
		centreY = 0.5f * (minY + maxY);
		span    = ((maxX - minX > maxY - minY) ? maxX - minX : maxY - minY) + 64.0f;
		if (span > cap)
		{
			// a view to the horizon: the window keeps the near end (corners 0 and 1 are the screen's bottom)
			const Real nearX = 0.5f * (fx[0] + fx[1]), nearY = 0.5f * (fy[0] + fy[1]);
			const Real farX  = 0.5f * (fx[2] + fx[3]), farY  = 0.5f * (fy[2] + fy[3]);
			const Real len   = sqrtf((farX - nearX) * (farX - nearX) + (farY - nearY) * (farY - nearY));
			if (len > 1.0f)
			{
				centreX = nearX + (farX - nearX) / len * (0.5f * cap - 64.0f);
				centreY = nearY + (farY - nearY) / len * (0.5f * cap - 64.0f);
			}
			span = cap;
		}
	}
	else
	{
		for (Int k = 0; k < WAKE_TRAILS; k++)
			if (m_wake[k].id != 0)
			{
				centreX = m_wake[k].headX;	// no footprint: around a wake
				centreY = m_wake[k].headY;
				break;
			}
		span = WATERSEA_WAKE_WINDOW;
	}
	Real size = WATERSEA_WAKE_WINDOW;
	while (size < span && size < cap)
		size *= 2.0f;
	const Real texel   = size / (Real)WATERSEA_WAKE_SIZE;
	const Real originX = floorf((centreX - 0.5f * size) / texel) * texel;
	const Real originY = floorf((centreY - 0.5f * size) / texel) * texel;

	IDirect3DSurface9 *target = nullptr, *oldTarget = nullptr, *oldDepth = nullptr;
	if (FAILED(m_wakeTexture->GetSurfaceLevel(0, &target)) || target == nullptr)
		return;
	if (FAILED(dev->GetRenderTarget(0, &oldTarget)) || oldTarget == nullptr)
	{
		target->Release();
		return;
	}
	dev->GetDepthStencilSurface(&oldDepth);	// may be none
	D3DVIEWPORT9 oldViewport;
	dev->GetViewport(&oldViewport);
	if (SUCCEEDED(dev->SetRenderTarget(0, target)))
	{
		dev->SetDepthStencilSurface(nullptr);	// the scene's may be multisampled; none is needed

		// additive, no depth, and the outer texel left clear: a clamped lookup past the window then reads no wake
		static const DWORD state[][2] =
		{
			{ D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_ALPHATESTENABLE, FALSE },
			{ D3DRS_ALPHABLENDENABLE, TRUE }, { D3DRS_SRCBLEND, D3DBLEND_ONE }, { D3DRS_DESTBLEND, D3DBLEND_ONE },
			{ D3DRS_BLENDOP, D3DBLENDOP_ADD }, { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE },
			{ D3DRS_FOGENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE }, { D3DRS_COLORWRITEENABLE, 0xF },
			{ D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID }, { D3DRS_CLIPPLANEENABLE, 0 },
			{ D3DRS_SCISSORTESTENABLE, FALSE },
		};
		enum { STATE_COUNT = sizeof(state) / sizeof(state[0]) };
		DWORD oldState[STATE_COUNT];
		for (Int s = 0; s < STATE_COUNT; s++)
		{
			oldState[s] = state[s][1];
			dev->GetRenderState((D3DRENDERSTATETYPE)state[s][0], &oldState[s]);
			dev->SetRenderState((D3DRENDERSTATETYPE)state[s][0], state[s][1]);
		}
		dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);	// the whole target: the scissor is off
		RECT oldScissor = { 0, 0, 0, 0 };
		dev->GetScissorRect(&oldScissor);
		const RECT inner = { 1, 1, WATERSEA_WAKE_SIZE - 1, WATERSEA_WAKE_SIZE - 1 };
		dev->SetScissorRect(&inner);
		dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);	// restored with the rest below

		// world xy -> clip, half a texel in it (D3D9 pixel centres); the shape's constants - widths never under what the
		// texels can hold
		const Real sigFine  = (WATERSEA_WAKE_FINE[0] > 1.6f * texel) ? WATERSEA_WAKE_FINE[0] : 1.6f * texel;
		const Real sigBroad = (WATERSEA_WAKE_BROAD[0] > 2.0f * sigFine) ? WATERSEA_WAKE_BROAD[0] : 2.0f * sigFine;
		const float map[4]     = { 2.0f / size, -2.0f / size,
		                           -2.0f * originX / size - 1.0f - 1.0f / (float)WATERSEA_WAKE_SIZE,
		                            2.0f * originY / size + 1.0f + 1.0f / (float)WATERSEA_WAKE_SIZE };
		// Ronin @bugfix 04/10/2026 DX9: `wakeheight` here: the swell's height and its slope scale together
		const float arm[4]     = { sigFine, WATERSEA_WAKE_FINE[1], sigBroad, WATERSEA_WAKE_BROAD[1] * TheWaterSeaGlobal.wakeHeight };
		const float wash[4]    = { WATERSEA_WAKE_WASH[0], WATERSEA_WAKE_WASH[1], WATERSEA_WAKE_WASH[2], 1.0f / WATERSEA_WAKE_WASH[3] };
		const float feather[4] = { WATERSEA_WAKE_FEATHER[0], WATERSEA_WAKE_FEATHER[1], WATERSEA_WAKE_FEATHER[2], 1.0f / WATERSEA_WAKE_FINE_IN };
		const float ringCfg[4] = { WATERSEA_SURGE_FOAM, WATERSEA_WAKE_APEX, 1.0f / WATERSEA_WAKE_ARM, WATERSEA_WAKE_HOLLOW };
		DX8Wrapper::BindLayoutFVF(WAKE_FVF, "WaterRenderObjClass::renderWakes");
		dev->SetVertexShader(m_wakeVS);		// after the layout bind: it clears the vertex shader
		dev->SetPixelShader(m_wakePS);
		dev->SetVertexShaderConstantF(25, map, 1);
		dev->SetPixelShaderConstantF(56, arm, 1);
		dev->SetPixelShaderConstantF(57, wash, 1);
		dev->SetPixelShaderConstantF(58, feather, 1);
		dev->SetPixelShaderConstantF(59, ringCfg, 1);
		const float shape[4]   = { 2.0f - TheWaterSeaGlobal.wakeSharp, 0.0f, 0.0f, 0.0f };	// Ronin @feature 25/10/2026 DX9: `wakesharp`
		dev->SetPixelShaderConstantF(63, shape, 1);

		// Ronin @bugfix 04/10/2026 DX9: turns. Two strips, left and right of the track; each stops short of where its cross-
		// section meets a neighbour's, or the ribbon folds over itself inside a tight turn (Water_Work.md §6).
		enum { RIBBON_POINTS = WAKE_POINTS + 3 };	// the trail, the centre now, the leading end, the nose
		static WakeVertex verts[2 * RIBBON_POINTS];
		static Real px[RIBBON_POINTS], py[RIBBON_POINTS], pAge[RIBBON_POINTS], pPath[RIBBON_POINTS], pStrength[RIBBON_POINTS];
		static Real pnx[RIBBON_POINTS], pny[RIBBON_POINTS], pArm[RIBBON_POINTS], pLimit[2][RIBBON_POINTS];
		UnsignedInt trails = 0, vertTotal = 0;
		for (Int k = 0; k < WAKE_TRAILS; k++)
		{
			const WakeTrail &t = m_wake[k];
			if (t.id == 0 || t.count == 0)
				continue;
			// the centre's points, oldest first, then the centre as it was last seen
			Int n = 0;
			for (Int i = 0; i < t.count; i++)
			{
				const WakePoint &p = t.pt[(t.first + i) % WAKE_POINTS];
				px[n] = p.x;	py[n] = p.y;	pAge[n] = (now - p.time) / WATERSEA_WAKE_LIFE;
				pPath[n] = p.path;	pStrength[n] = p.strength;
				n++;
			}
			const Real seenAge = (now - t.seen) / WATERSEA_WAKE_LIFE;	// 0 while its unit draws
			const Real hx  = t.headX - px[n - 1];
			const Real hy  = t.headY - py[n - 1];
			const Real run = sqrtf(hx * hx + hy * hy);
			if (run > 0.5f)
			{
				px[n] = t.headX;	py[n] = t.headY;	pAge[n] = seenAge;
				pPath[n] = pPath[n - 1] + run;	pStrength[n] = t.headStrength;
				n++;
			}
			if (n < 2)
				continue;	// at rest
			// the leading end, where the V starts, and a nose ahead of it: the swell fades out there instead of ending in
			// a straight edge across the bow
			const Real toTip    = 0.85f * t.halfLength;
			const Real tipPath  = pPath[n - 1] + toTip;
			px[n] = t.headX + t.dirX * toTip;	py[n] = t.headY + t.dirY * toTip;	pAge[n] = seenAge;
			pPath[n] = tipPath;	pStrength[n] = pStrength[n - 1];
			n++;
			px[n] = t.headX + t.dirX * (toTip + WATERSEA_WAKE_NOSE);	py[n] = t.headY + t.dirY * (toTip + WATERSEA_WAKE_NOSE);
			pAge[n] = seenAge;	pPath[n] = tipPath + WATERSEA_WAKE_NOSE;	pStrength[n] = 0.0f;
			n++;

			// Ronin @bugfix 04/10/2026 DX9: each point's direction = its two segments, each weighted by the OTHER's length: exact on
			// a circle however unevenly the points sit (neighbour to neighbour pinched the ribbon amidships in a turn).
			Real nx = 0.0f, ny = 0.0f;
			for (Int i = 0; i < n; i++)
			{
				const Int  a  = (i > 0) ? i - 1 : 0;
				const Int  b  = (i < n - 1) ? i + 1 : n - 1;
				Real tx = px[b] - px[a];
				Real ty = py[b] - py[a];
				if (i > 0 && i < n - 1)
				{
					const Real inX  = px[i] - px[a],  inY  = py[i] - py[a];
					const Real outX = px[b] - px[i],  outY = py[b] - py[i];
					const Real inL  = sqrtf(inX * inX + inY * inY);
					const Real outL = sqrtf(outX * outX + outY * outY);
					if (inL > 1.0e-3f && outL > 1.0e-3f)
					{
						tx = inX / inL * outL + outX / outL * inL;
						ty = inY / inL * outL + outY / outL * inL;
					}
				}
				const Real tl = sqrtf(tx * tx + ty * ty);
				if (tl > 1.0e-3f)
				{
					nx = -ty / tl;	// the trail's left
					ny =  tx / tl;
				}
				pnx[i] = nx;
				pny[i] = ny;
				const Real behind = tipPath - pPath[i];	// under 0: the nose, where the half-width runs down past 0 - no crest
				pArm[i] = WATERSEA_WAKE_APEX + behind * ((behind >= 0.0f) ? WATERSEA_WAKE_ARM : 1.0f);
				const Real reach = ((pArm[i] > WATERSEA_WAKE_APEX) ? pArm[i] : WATERSEA_WAKE_APEX) + 3.0f * sigBroad;
				pLimit[0][i] = reach;	// left: the arm and its swell's tail
				pLimit[1][i] = reach;	// right
			}
			for (Int i = 0; i < n; i++)
			{
				for (Int j = i - 1; j <= i + 1; j += 2)
				{
					if (j < 0 || j >= n)
						continue;
					// where this cross-section meets the neighbour's: P[i] + s N[i] = P[j] + u N[j]
					const Real cr = pnx[i] * pny[j] - pny[i] * pnx[j];
					if (fabsf(cr) < 1.0e-4f)
						continue;	// parallel: never
					const Real s = ((px[j] - px[i]) * pny[j] - (py[j] - py[i]) * pnx[j]) / cr;
					const Int  side  = (s > 0.0f) ? 0 : 1;
					const Real limit = WATERSEA_WAKE_FOLD * fabsf(s);
					if (limit < pLimit[side][i])
						pLimit[side][i] = limit;
				}
			}
			// away from a tight spot a side opens again only gradually: an arm thins out into a turn, it is not cut
			for (Int side = 0; side < 2; side++)
			{
				for (Int i = 1; i < n; i++)
				{
					const Real open = pLimit[side][i - 1] + WATERSEA_WAKE_OPEN * fabsf(pPath[i] - pPath[i - 1]);
					if (open < pLimit[side][i])
						pLimit[side][i] = open;
				}
				for (Int i = n - 2; i >= 0; i--)
				{
					const Real open = pLimit[side][i + 1] + WATERSEA_WAKE_OPEN * fabsf(pPath[i] - pPath[i + 1]);
					if (open < pLimit[side][i])
						pLimit[side][i] = open;
				}
			}

			const Real hullLen  = 1.7f * t.halfLength;	// the leading end to the trailing one
			const Real halfBeam = (t.halfWidth > 1.0f) ? t.halfWidth : 1.0f;
			for (Int side = 0; side < 2; side++)
			{
				const Real sign = (side == 0) ? 1.0f : -1.0f;
				Int v = 0;
				for (Int i = 0; i < n; i++)
				{
					const Real limit  = (pLimit[side][i] > 0.01f) ? pLimit[side][i] : 0.01f;
					const Real age    = (pAge[i] < 0.0f) ? 0.0f : (pAge[i] > 1.0f) ? 1.0f : pAge[i];
					const Real astern = tipPath - pPath[i] - hullLen;
					WakeVertex &c = verts[v++];	// on the track; its distance carries the side's sign
					c.x = px[i];	c.y = py[i];
					c.z = limit / ((limit < WATERSEA_WAKE_RIM) ? limit : WATERSEA_WAKE_RIM);
					c.across[0] = sign * 1.0e-4f;	c.across[1] = pArm[i];	c.across[2] = age;	c.across[3] = pStrength[i];
					c.along[0]  = pnx[i];	c.along[1] = pny[i];	c.along[2] = astern;	c.along[3] = halfBeam;
					WakeVertex &e = verts[v++];	// the side's edge
					e.x = px[i] + pnx[i] * limit * sign;	e.y = py[i] + pny[i] * limit * sign;
					e.z = 0.0f;
					e.across[0] = sign * limit;	e.across[1] = pArm[i];	e.across[2] = age;	e.across[3] = pStrength[i];
					e.along[0]  = pnx[i];	e.along[1] = pny[i];	e.along[2] = astern;	e.along[3] = halfBeam;
				}
				dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, (UINT)(v - 2), verts, sizeof(WakeVertex));
				vertTotal += (UnsignedInt)v;
			}
			trails++;
		}

		// Ronin @feature 04/10/2026 DX9: the rings and surges: a quad round each, in the hull's frame; the pixel shader finds
		// the distance to the hull's outline (WaterWake_ps.hlsl, the rim under -0.5 says which shape).
		const Real ringWidth = (WATERSEA_SURGE_WIDTH > 2.5f * texel) ? WATERSEA_SURGE_WIDTH : 2.5f * texel;
		UnsignedInt ripples = 0;

		// Ronin @bugfix 04/10/2026 DX9: the rings round a unit at rest: one quad a unit, the train's three phases per draw.
		// The ripples go as the texels grow past what holds a 13-unit wave; the long wave stays.
		if (TheWaterSeaGlobal.wakeRings > 0.0f)
		{
			const Real fullTurn = 2.0f * PI;	// (TWO_PI is a macro here)
			const float waveK[4] = { fullTurn / WATERSEA_RING_FINE[0], fullTurn / WATERSEA_RING_FINE[1], fullTurn / WATERSEA_RING_LONG, 0.0f };
			dev->SetPixelShaderConstantF(61, waveK, 1);
			Real fine = (4.0f - texel) * 0.5f;
			if (fine < 0.0f) fine = 0.0f;
			if (fine > 1.0f) fine = 1.0f;
			for (Int k = 0; k < WAKE_TRAILS; k++)
			{
				const WakeTrail &t = m_wake[k];
				if (t.id == 0 || t.idle <= 0.02f || now - t.seen >= 0.5f)
					continue;
				const Real amount   = t.idle * t.idleScale * TheWaterSeaGlobal.wakeRings * (1.0f - (now - t.seen) * 2.0f);
				const Real halfBeam = (t.halfWidth > 1.0f) ? t.halfWidth : 1.0f;
				const Real halfSeg  = (0.8f * t.halfLength > halfBeam) ? 0.8f * t.halfLength - halfBeam : 0.0f;
				const Real halfY    = halfBeam + WATERSEA_RING_REACH;
				const Real halfX    = halfSeg + halfY;
				// each unit out of step with the others; the phase is the distance its waves have run
				const Real run = WATERSEA_RING_SPEED * now + 7.0f * (Real)(t.id % 16);
				const float phase[4] = { fmodf(waveK[0] * run, fullTurn), fmodf(waveK[1] * run, fullTurn), fmodf(waveK[2] * run, fullTurn),
				                         WATERSEA_RING_HEIGHT[1] * amount };
				dev->SetPixelShaderConstantF(60, phase, 1);
				for (Int c = 0; c < 4; c++)
				{
					const Real lx = (c < 2) ? -halfX : halfX;	// along the heading
					const Real ly = (c & 1) ? halfY : -halfY;	// to its left
					WakeVertex &o = verts[c];
					o.x = t.headX + t.dirX * lx - t.dirY * ly;
					o.y = t.headY + t.dirY * lx + t.dirX * ly;
					o.z = -3.0f;
					o.across[0] = lx;	o.across[1] = ly;	o.across[2] = halfBeam;	o.across[3] = WATERSEA_RING_HEIGHT[0] * 0.625f * amount * fine;
					o.along[0]  = t.dirX;	o.along[1] = t.dirY;	o.along[2] = 1.0f / WATERSEA_RING_REACH;	o.along[3] = halfSeg;
				}
				dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(WakeVertex));
				ripples++;
				vertTotal += 4;
			}
		}
		for (Int k = 0; k < WAKE_RIPPLES; k++)
		{
			const WakeRipple &r = m_ripple[k];
			if (r.life <= 0.0f)
				continue;
			const Real age    = now - r.time;
			const Real young  = 1.0f - age / r.life;
			const Real fadeIn = (age < 0.3f) ? age / 0.3f : 1.0f;	// no pop as it leaves the hull
			const Real height = r.height * young * sqrtf(young) * fadeIn;
			const Real reach  = r.start + r.speed * age;	// its distance from the hull's outline now
			const Real halfY  = reach + 3.0f * ringWidth;
			const Real halfX  = r.halfSeg + halfY;
			for (Int c = 0; c < 4; c++)
			{
				const Real lx = (c < 2) ? -halfX : halfX;	// along the heading
				const Real ly = (c & 1) ? halfY : -halfY;	// to its left
				WakeVertex &o = verts[c];
				o.x = r.x + r.dirX * lx - r.dirY * ly;
				o.y = r.y + r.dirY * lx + r.dirX * ly;
				o.z = -1.0f - r.ahead;
				o.across[0] = lx;	o.across[1] = ly;	o.across[2] = ringWidth;	o.across[3] = height;
				o.along[0]  = r.dirX;	o.along[1] = r.dirY;	o.along[2] = reach;	o.along[3] = r.halfSeg;
			}
			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(WakeVertex));
			ripples++;
			vertTotal += 4;
		}

		dev->SetVertexShader(nullptr);
		dev->SetPixelShader(nullptr);
		dev->SetScissorRect(&oldScissor);
		for (Int s = 0; s < STATE_COUNT; s++)
			dev->SetRenderState((D3DRENDERSTATETYPE)state[s][0], oldState[s]);
		dev->SetRenderTarget(0, oldTarget);
		dev->SetDepthStencilSurface(oldDepth);
		dev->SetViewport(&oldViewport);	// SetRenderTarget reset it to the whole target

		TheWaterStats.wakeRipples = ripples;
		if (trails > 0 || ripples > 0)
		{
			m_wakeLive    = TRUE;
			m_wakeScale   = 1.0f / size;
			m_wakeOffsetU = -originX / size;
			m_wakeOffsetV = -originY / size;
		}
		TheWaterStats.wakeTrails = trails;	// Ronin @diagnostic 03/10/2026 DX9: [WATER] row
		TheWaterStats.wakeVerts  = vertTotal;
		TheWaterStats.wakeTexel  = texel;
	}
	if (oldDepth != nullptr)
		oldDepth->Release();
	oldTarget->Release();
	target->Release();
}

//-------------------------------------------------------------------------------------------------
//Draw a 4 sided flat water area.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawTrapezoidWater(Vector3 points[4])
{
	Vector3 origin(points[0]);
	Vector3 uVec1(points[1]);
	Vector3 vVec1(points[3]);
	Vector3 uVec2(points[2]);
	Vector3 vVec2(points[2]);
	uVec2 -= vVec1;
	vVec2	-= uVec1;
	uVec1 -= origin;
	vVec1 -= origin;
	Int uCount = (uVec1.Length()+uVec2.Length()) / (8*MAP_XY_FACTOR);
	if (uCount<1) uCount = 1;
	Int vCount = (vVec1.Length()+vVec2.Length()) / (8*MAP_XY_FACTOR);
	if (vCount<1) vCount = 1;

	if (uCount>50) uCount = 50;
	if (vCount>50) vCount = 50;

	static Bool doWobble = true;

	// Ronin @bugfix 12/01/2026 Water: fix trapezoid quad/triangle count (was computed before u/v increment)
	// After expanding uCount/vCount to include the final row/column, quad count must be (uCount-1)*(vCount-1).
	// Ronin @feature 29/09/2026 DX9: phase 2 - the swell needs vertices: `swellcell` cells in the part in view, inside one
	// draw's budget (WATER_SWELL_CELLS_MAX). This polygon's kind decides (m_standingIsSea).
	const Int standingKind = m_standingIsSea ? WATER_KIND_SEA : WATER_KIND_LAKE;
	// Ronin @bugfix 03/10/2026 DX9: phase 5 - each axis as positions 0..1; even, unless the swell grid fits the camera.
	Real uPos[WATER_SWELL_AXIS_MAX + 1], vPos[WATER_SWELL_AXIS_MAX + 1];
	const Real uLen = 0.5f * (uVec1.Length() + uVec2.Length());
	const Real vLen = 0.5f * (vVec1.Length() + vVec2.Length());
	m_swellCellNow = 0.0f;
	// Ronin @feature 25/10/2026 DX9: dense for a live wake too - it raises the water with the swell off (renderWakes ran first)
	if (useReflection() && WaterSea_MeshActive(standingKind, m_heightVTF, (m_wakeLive && m_wakeFormat == 2) ? TRUE : FALSE) &&
		uLen > 0.0f && vLen > 0.0f)
	{
		// the part of this polygon in view, as a range along each axis, with room for the waves' sideways travel
		const Real    swellCell = WaterSea_Profile(standingKind).swellCell;
		const Vector3 bend      = vVec2 - vVec1;
		Real fx[4], fy[4];
		Real uLo = 1.0e9f, uHi = -1.0e9f, vLo = 1.0e9f, vHi = -1.0e9f;
		Bool fitted = waterCameraFootprint(origin.Z, fx, fy);
		for (Int c = 0; c < 4 && fitted; c++)
		{
			Real u = 0.0f, v = 0.0f;
			fitted = waterQuadUV(origin, uVec1, vVec1, bend, fx[c], fy[c], &u, &v);
			if (u < uLo) uLo = u;
			if (u > uHi) uHi = u;
			if (v < vLo) vLo = v;
			if (v > vHi) vHi = v;
		}
		if (fitted)
		{
			const Real margin = 3.0f * swellCell + 20.0f;
			uLo -= margin / uLen;	uHi += margin / uLen;
			vLo -= margin / vLen;	vHi += margin / vLen;
		}
		else
		{
			uLo = 0.0f;	uHi = 1.0f;	// no footprint: dense over the whole polygon, as far as the budget goes
			vLo = 0.0f;	vHi = 1.0f;
		}
		// both axes at `swellcell`; over the draw's budget the cell doubles on both (square cells, still fixed multiples).
		// It ends: a cell as long as the polygon gives 2 x 2.
		Real scale = 1.0f;
		for (;;)
		{
			uCount = waterSwellAxis(uLo, uHi, scale * swellCell / uLen, uPos);
			vCount = waterSwellAxis(vLo, vHi, scale * swellCell / vLen, vPos);
			if (uCount >= 2 && vCount >= 2 && (uCount - 1) * (vCount - 1) <= WATER_SWELL_CELLS_MAX)
				break;
			scale *= 2.0f;
		}
		// the real cell: the VS only moves the grid by waves it can carry (R3DWaterSea.h), and the [WATER] row shows it
		m_swellCellNow = scale * swellCell;
		if (uCount > 2 && vCount > 2)
		{
			// Ronin @feature 03/10/2026 DX9: phase 5 - in view: floating units ride the waves this grid carries (swellHeight)
			(m_standingIsSea ? m_swellCellSea : m_swellCellLake) = m_swellCellNow;
			if (TheWaterStats.enabled && m_swellCellNow > TheWaterStats.flatCell)
				TheWaterStats.flatCell = m_swellCellNow;
		}
	}
	else
	{
		uCount++;
		vCount++;
		for (Int k = 0; k < uCount; k++)
			uPos[k] = (Real)k / (Real)(uCount - 1);
		for (Int k = 0; k < vCount; k++)
			vPos[k] = (Real)k / (Real)(vCount - 1);
	}

	Int rectangleCount = (uCount - 1) * (vCount - 1);

	Int i, j;
	//allocate 2 triangles per side with 3 indices per triangle

	const int vertexCount = uCount * vCount;
	const int indexCount = rectangleCount * 6;
	if (TheWaterStats.enabled)
		TheWaterStats.flatVerts += (UnsignedInt)vertexCount;	// Ronin @diagnostic 29/09/2026 DX9: [WATER] row

	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8, (unsigned short)indexCount);
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		for (j=0; j<vCount-1; j++)
		{	for (i=0; i<uCount-1; i++)
			{
				//triangle 1
				curIb[0] = (j)*uCount + i;
				curIb[1] = (j+1)*uCount + i+1;
				curIb[2] = (j+1)*uCount + i;

				//triangle 2
				curIb[3] = (j)*uCount + i;
				curIb[4] = (j)*uCount + i+1;
				curIb[5] = (j+1)*uCount + i+1;

				curIb += 6;	//skip the 6 indices we just added.
			}
		}
	}

	Real	waterFactor=150;
	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{
			//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	//DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(rectangleCount+1)*2);
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, (unsigned short)vertexCount);

//#define WAVY_WATER
//#define FEATHER_LAYER_COUNT (3) //LORENZEN
//#define FEATHER_LAYER_THICKNESS (2.5f)
//#define FEATHER_WATER

//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN
	if ( TheGlobalData->m_featherWater )
	{

		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real phase = 0;
		Real mapCoeff = PI/(4*MAP_XY_FACTOR);
		Real wave = 0;
		Real amplitude = 0.5f;

		//The first (high order) byte is the Alpha value for this patch
		// It needs to be set proportional to the number of feather layers
		// this comes from TheGlobalData->m_featherWater, which is a count of layers


		Int Alpha = 0;
		if ( TheGlobalData->m_featherWater == 5) Alpha = 80;
		if ( TheGlobalData->m_featherWater == 4) Alpha = 110;
		if ( TheGlobalData->m_featherWater == 3) Alpha = 140;
		if ( TheGlobalData->m_featherWater == 2) Alpha = 200;
		if ( TheGlobalData->m_featherWater == 1) Alpha = 255;

		//Keep diffuse from lighting calculations but substitute custom alpha
		Int customDiffuse = (diffuse & 0x00ffffff) | (Alpha<< 24);//(0x80 << 16)|(0x90 << 8)|0xa0;

		for (j=0; j<vCount; j++)
		{
			Real dv = vPos[j];	// Ronin @bugfix 03/10/2026 DX9: phase 5 - the axis tables above
			for (i=0; i<uCount; i++)
			{
				Real du = uPos[i];
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;

				// common to all the waving effects
				phase = 25 * m_riverVOrigin + vertex.X * mapCoeff;
				wave = (sin(phase) - 1.0f) * amplitude;

				vb->z = (vertex.Z + wave);
				vb->diffuse = customDiffuse;
				vb->u1 = (vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*wave;
				vb->v1 = (vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*wave;
				vb->u2 = vertex.X/BUMP_SIZE;
				vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}
//#else // STILL THE OLD FLAT WATER
	else

	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		//Pulling some constants out of the inner loops to improve performance -MW
		Real constA=0.02*cos(11*m_riverVOrigin);
		Real constB=0.02*cos(5*m_riverVOrigin);
		Real constC=25*m_riverVOrigin;
		Real ooWaterFactor = 1.0f/waterFactor;
		const Real constD=PI/(4*MAP_XY_FACTOR);

		for (j=0; j<vCount; j++)
		{
			Real dv = vPos[j];	// Ronin @bugfix 03/10/2026 DX9: phase 5 - the axis tables above

			for (i=0; i<uCount; i++)
			{
				Real du = uPos[i];
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;
				vb->z=vertex.Z;

				vb->diffuse= diffuse;
				//Old slower version
 				//vb->u1=(vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.X*PI/(4*MAP_XY_FACTOR));
 				//vb->v1=(vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.Y*PI/(4*MAP_XY_FACTOR));
				vb->u1=vertex.X*ooWaterFactor + constA*WWMath::Fast_Sin(constC+vertex.X*constD);
				vb->v1=vertex.Y*ooWaterFactor + constB*WWMath::Fast_Sin(constC+vertex.Y*constD);
				vb->u2 = vertex.X/BUMP_SIZE;
				//Old slower version
 				//vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->v2 = (vertex.Y+0.3f*vertex.X)/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}

//#endif // OLD VS NEW WATER



	Matrix3D tm(1);

	Renderer::Set_Transform(RenderTransform_World,tm);	//position the water surface
	DX8Wrapper::Set_Index_Buffer(ib_access, 0, "WaterRenderObjClass::drawTrapezoidWater");
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	// Ronin @feature 28/09/2026 DX9: WaterType 2 draws the map's water polygons with the reflective WaterSea shaders.
	const Bool reflect = useReflection();
	if (reflect)
		setupReflectiveWater(FALSE);
	else
		setupFlatWaterShader();// lorenzen sez use the alpha shader

	//If video card supports it and it's enabled, feather the water edge using destination alpha
	// Ronin @feature 29/09/2026 DX9: phase 1 - not when the WaterSea shader has depth: its own alpha is the soft shore.
	if (!(reflect && WaterSea_DepthActive(m_heightTexture != nullptr)) &&
		Renderer::Get_Back_Buffer_Format() == WW3D_FORMAT_A8R8G8B8 && TheGlobalData->m_showSoftWaterEdge && TheWaterTransparency->m_transparentWaterDepth !=0)
	{		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA );
			if (!TheWaterTransparency->m_additiveBlend)
				DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVDESTALPHA );
	}


 	DWORD cull;
	DX8Wrapper::_Get_D3D_Device8()->GetRenderState(D3DRS_CULLMODE, &cull);

	// @refactor Ronin 23/03/2026 DX9: Route trapezoid fixed-function render-state writes through wrapper for cache coherence.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);




//#ifdef FEATHER_WATER // the NEW WATER a'la LORENZEN

//	int layer = 0;//LORENZEN
//	for (layer = 0; layer < FEATHER_LAYER_COUNT; ++layer)//LORENZEN
//#endif // FEATHER_WATER
	{
//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN

		//increment the depth of the water's surface for every vert in the buffer
//#ifdef  FEATHER_WATER
//		VertexFormatXYZNDUV2 *vertBuf = vertexBufferStart;
//		while (vertBuf < vertexBufferStart + vCount * uCount)
//		{
//			vertBuf->z *= FEATHER_LAYER_THICKNESS;
//			++vertBuf;
//		}
//#endif // FEATHER_WATER
//#endif //WAVY_WATER

		// Ronin @bugfix 15/09/2026 DX9: bind the layout explicitly again — Apply_Render_State_Changes re-uses the PREVIOUS draw's
		// FVF/decl on a VB change (dx8wrapper.cpp:3473-3516), which streaked the ocean. Found through the new debug panel.
		DX8Wrapper::BindLayoutFVF(vb_access.FVF_Info().Get_FVF(), "WaterRenderObjClass::drawTrapezoidWater");
		// Ronin @feature 28/09/2026 DX9: after the layout - BindLayoutFVF clears the vertex shader.
		if (reflect)
			bindReflectiveShaders();

		// Ronin @bugfix 03/10/2026 DX9: the WaterSea PS marks these pixels for TAA's water mask (render target 1 while TAA runs).
		// Not under the DESTALPHA blend (`depth` 0): it would scale the mark by the mask's own empty alpha.
		const Bool taaMarked = (reflect && WaterSea_DepthActive(m_heightTexture != nullptr)) ? R3DTaa::beginWaterMask() : FALSE;
		DX8Wrapper::Draw_Triangles(0, rectangleCount * 2, 0, uCount* vCount);//lorenzen thinks this is where to itereate the soft shoreline effect
		if (taaMarked)
			R3DTaa::endWaterMask();
		// Ronin @diagnostic 28/09/2026 DX9: [WATER] row - count this draw and whether the water shader was bound for it.
		if (reflect)
			countWaterDraw(TheWaterStats.flatDraws, TheWaterStats.flatPSDraws, pickWavePS(m_bindKind), m_dwWaveVertexShader);
		else
			countWaterDraw(TheWaterStats.flatDraws, TheWaterStats.flatPSDraws, m_trapezoidWaterPixelShader);

	}




	if (false) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
		m_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE , false);
		DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	(rectangleCount+1)*2);
		m_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE , true);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);
	}

	// Ronin @feature 28/09/2026 DX9: reflective draw - fixed function again for the shroud pass below.
	if (reflect)
		unbindReflectiveShaders(vb_access.FVF_Info().Get_FVF());
	else if (m_riverWaterPixelShader) DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(0);
	//Restore alpha blend to default values since we may have changed them to feather edges.
	if (!TheWaterTransparency->m_additiveBlend)
	{	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
	}
	else
	{
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE );
	}

	if (TheTerrainRenderObject->getShroud())
	{
		// Ronin @bugfix 03/10/2026 DX9: the reflective draw takes no second pass: the WaterSea PS samples the shroud itself, at
		// the displaced surface (bindWaterSea, s9).
		if (reflect)
		{
		}
		else if (m_trapezoidWaterPixelShader)
		{	
			//shroud was applied in stage3 of main pass so just need to restore state here.
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
			DX8Wrapper::Set_DX8_Texture(3, nullptr);	//free possible reference to shroud texture
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
		}
		else
		{
			//do second pass to apply the shroud on water plane for cards that can't do it in main pass.
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			// Ronin @bugfix 28/09/2026 DX9: this grid has uCount*vCount vertices; (rectangleCount+1)*2 is the river's count.
			DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	uCount*vCount);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
		}
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, cull);
}



//-------------------------------------------------------------------------------------------------
//debug version where moon rotates with the camera	(always upright on screen)
//-------------------------------------------------------------------------------------------------
#if 0
void WaterRenderObjClass::renderSkyBody(Matrix3D *mat)
{
	Vector3 vRight,vUp,V0,V1,V2,V3;

	mat->Get_X_Vector(&vRight);
	mat->Get_Y_Vector(&vUp);

	//calculate offsets from quad center to each of the 4 corners
	//	0-----1
	//  |    /|
	//  |  /  |
	//	|/    |
	//  3-----2
	V0=-vRight+vUp;
	V2=vRight+vUp;
	V2=vRight-vUp;
	V3=-vRight-vUp;

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Shader(ShaderClass::/*_PresetAdditiveShader*//*_PresetOpaqueShader*/_PresetAlphaShader);
//	DX8Wrapper::Set_Texture(0,setting->skyBodyTexture);

	DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=SKYBODY_SIZE*V0.X;
			verts[0].y=SKYBODY_SIZE*V0.Y;
			verts[0].z=SKYBODY_SIZE*V0.Z;
			verts[0].u2=0;
			verts[0].v2=1;
			verts[0].diffuse=0xffffffff;

			verts[1].x=SKYBODY_SIZE*V1.X;
			verts[1].y=SKYBODY_SIZE*V1.Y;
			verts[1].z=SKYBODY_SIZE*V1.Z;
			verts[1].u2=1;
			verts[1].v2=1;
			verts[1].diffuse=0xffffffff;

			verts[2].x=SKYBODY_SIZE*V2.X;
			verts[2].y=SKYBODY_SIZE*V2.Y;
			verts[2].z=SKYBODY_SIZE*V2.Z;
			verts[2].u2=1;
			verts[2].v2=0;
			verts[2].diffuse=0xffffffff;

			verts[3].x=SKYBODY_SIZE*V3.X;
			verts[3].y=SKYBODY_SIZE*V3.Y;
			verts[3].z=SKYBODY_SIZE*V3.Z;
			verts[3].u2=0;
			verts[3].v2=0;
			verts[3].diffuse=0xffffffff;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	//set position of skybody in world
//	tm.Set_Translation(Vector3(40,0,0));
	Renderer::Set_Transform(RenderTransform_World,tm);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// grid cells x
	Int cellsX = m_gridCellsX;
	xfer->xferInt( &cellsX );
	if( cellsX != m_gridCellsX )
	{

		DEBUG_CRASH(( "WaterRenderObjClass::xfer - cells X mismatch" ));
		throw SC_INVALID_DATA;

	}

	// grid cells Y
	Int cellsY = m_gridCellsY;
	xfer->xferInt( &cellsY );
	if( cellsY != m_gridCellsY )
	{

		DEBUG_CRASH(( "WaterRenderObjClass::xfer - cells Y mismatch" ));
		throw SC_INVALID_DATA;

	}

	// xfer each of the mesh data points
	for( UnsignedInt i = 0; i < m_meshDataSize; ++i )
	{

		// height
		xfer->xferReal( &m_meshData[ i ].height );

		// velocity
		xfer->xferReal( &m_meshData[ i ].velocity );

		// status
		xfer->xferUnsignedByte( &m_meshData[ i ].status );

		// preferred height
		xfer->xferUnsignedByte( &m_meshData[ i ].preferredHeight );

	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::loadPostProcess()
{

}


