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

// FILE: W3DShaderManager.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DShaderManager.cpp
//
// Created:   Mark Wilczynski, August 2001
//
// Desc:      Perform tests on currently selected WW3D/D3D device to determine
//			  which of our rendering features are supported.  The system allows
//			  setting up a few custom shaders that are selected based on video
//			  card features.
//
//			  To add a new shader to the system:
//			  0) Add your shader to the ShaderTypes enum
//			  1) Create shader using W3DShaderInterface
//			  2) Repeat step 1 for any alternate shaders
//			  3) Create list of alternate shaders sorted by order of preference.
//				 The first shader which passes hardware validation will be selected.
//			  4) Add list from step 3) to MasterShaderList[].
//
//-----------------------------------------------------------------------------

#include "WW3D2/dx8wrapper.h"
#include "WW3D2/assetmgr.h"
#include "Lib/BaseType.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DEmbeddedShaders.h"
#include "W3DDevice/GameClient/W3DSlopeMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DShadowMap.h"
#include "W3DDevice/GameClient/W3DGroundNoise.h"
#include "W3DDevice/GameClient/W3DSkyClouds.h"
#include "GameClient/View.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/Display.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/texturefilter.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/formconv.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8fvf.h"
#include "WW3D2/dx8instancing.h"
#include "WW3D2/dx8skinning.h"
#include "WW3D2/pointgr.h"
#include "WW3D2/dx8polygonrenderer.h"
#include "WW3D2/matpass.h"
#include "WW3D2/texture.h"
#include "WWLib/ffactory.h"
#include "WWMath/aabox.h"
#include <vector>
#include <map>
#include <float.h>


// Turn this on to turn off pixel shaders. jba[4/3/2003]
#define do_not_DISABLE_PIXEL_SHADERS 1

/** Interface definition for custom shaders we define in our app.  These shaders can perform more complex
	operations than those allowed in the WW3D2 shader system.
*/
class W3DShaderInterface
{
public:
	Int getNumPasses() {return m_numPasses;};	///<return number of passes needed for this shader
	virtual Int set(Int pass) {return TRUE;};		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset() {
		ShaderClass::Invalidate();
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);};
	virtual Int init() = 0;			///<perform any one time initialization and validation
	virtual Int shutdown() { return TRUE;};			///<release resources used by shader
protected:
	Int m_numPasses;						///<number of passes to complete shader
};

//this table will contain custom versions of each shader tuned for specific video card and user options.
static W3DFilterInterface *W3DFilters[FT_MAX];
static W3DShaderInterface *W3DShaders[W3DShaderManager::ST_MAX];
static Int W3DShadersPassCount[W3DShaderManager::ST_MAX];	//number of passes for each of the above shaders
TextureClass *W3DShaderManager::m_Textures[8];
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader;
FilterTypes W3DShaderManager::m_currentFilter=FT_NULL_FILTER; ///< Last filter that was set.
Int W3DShaderManager::m_currentShaderPass;
ChipsetType W3DShaderManager::m_currentChipset;
GraphicsVenderID W3DShaderManager::m_currentVendor;
__int64 W3DShaderManager::m_driverVersion;

Bool W3DShaderManager::m_renderingToTexture = false;
IDirect3DSurface8 *W3DShaderManager::m_oldRenderSurface=nullptr;	///<previous render target
IDirect3DTexture8 *W3DShaderManager::m_renderTexture=nullptr;		///<texture into which rendering will be redirected.
IDirect3DSurface8 *W3DShaderManager::m_newRenderSurface=nullptr;	///<new render target inside m_renderTexture
IDirect3DSurface8 *W3DShaderManager::m_oldDepthSurface=nullptr;	///<previous depth buffer surface
/*===========================================================================================*/
/*=========      Screen Shaders	=============================================================*/
/*===========================================================================================*/

class ScreenDefaultFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
};

ScreenDefaultFilter screenDefaultFilter;

///Default filter that just renders screen to off-screen texture and then copies it the the screen.
///Useful because we added some full-time unit effects (microwave tank smudge) to Generals MD that need access
///to the background as a texture.  This filter makes that texture always available for these effects.
W3DFilterInterface *ScreenDefaultFilterList[]=
{
	&screenDefaultFilter,
	nullptr
};

Int ScreenDefaultFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return FALSE;
	}

	//Can render to texture, but we don't know if it can read and write to the same texture.
	//Since there is no D3D caps bit to tell you this, we will just hard-code some specific
	//cards that we know should work.

	Int res;

	if ((res=W3DShaderManager::getChipset()) != DC_UNKNOWN)
	{
		if ( res >=	DC_GEFORCE2)
		{
			//Check if their driver is newer than what we tested for this vendor
/*			if (TheGameLODManager)
			{
				if (TheGameLODManager->getTestedDriverVersion(W3DShaderManager::getCurrentVendor()) < W3DShaderManager::getCurrentDriverVersion())
					return FALSE;
			}*/
		}
	}

	W3DFilters[FT_VIEW_DEFAULT]=&screenDefaultFilter;

	return TRUE;
}

Bool ScreenDefaultFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	// TheSuperHackers @bugfix Disable Render To Texture redirection for the default filter
	// When MSAA is forced by Nvidia driver profile depth buffer is multisampled internally.
	// Rendering to non-MSAA texture with this depth buffer corrupts depth testing producing black screen
	// The smudge system has its own Copy path that works without Render To Texture.
	return FALSE;
}

Bool ScreenDefaultFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenDefaultFilter::set(FilterModes mode)
{
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	return true;
}

void ScreenDefaultFilter::reset()
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.

Int ScreenBWFilter::m_fadeFrames;
Int ScreenBWFilter::m_curFadeFrame;
Real ScreenBWFilter::m_curFadeValue;
Int ScreenBWFilter::m_fadeDirection;

ScreenBWFilter screenBWFilter;
ScreenBWFilterDOT3 screenBWFilterDOT3;	//slower version for older cards without pixel shaders.

///List of different BW shader implementations in order of preference
W3DFilterInterface *ScreenBWFilterList[]=
{
	&screenBWFilter,
	&screenBWFilterDOT3,	//slower version for older cards without pixel shaders.
	nullptr
};

Int ScreenBWFilter::init()
{
	Int res;
	HRESULT hr;

	m_dwBWPixelShader = 0;
	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//shader decleration
			DWORD Declaration[]=
			{
				(D3DVSD_STREAM(0)),
				(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
				(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
				(D3DVSD_REG(2, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_END())
			};

			//Monochrome pixel shader.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\monochrome.pso", &Declaration[0], 0, false, &m_dwBWPixelShader);
			if (FAILED(hr))
				return FALSE;

			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;

			return TRUE;
		}
	}
	return FALSE;
}

Bool ScreenBWFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilter::set(FilterModes mode)
{

	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_Pixel_Shader(m_dwBWPixelShader);
		const Vector4 luminance_weights(0.3f, 0.59f, 0.11f, 1.0f);
		DX8Wrapper::Set_Pixel_Shader_Constant(0, &luminance_weights, 1);

		Vector4	color(1.0f,1.0f,1.0f,1.0f);	//multiply color

		if (mode == FM_VIEW_BW_BLACK_AND_WHITE)
		{	//back & white mode
			color.X=1.0f;
			color.Y=1.0f;
			color.Z=1.0f;
		}
		if (mode == FM_VIEW_BW_RED_AND_WHITE)
		{	//red is on
			color.X = 1.0f;
			color.Y = 0.0f;
			color.Z = 0.0f;
			//inverse red is on
			//red is on
//			color.X = 0.0f;
//			color.Y = 1.0f;
//			color.Z = 1.0f;
		}
		if (mode == FM_VIEW_BW_GREEN_AND_WHITE)
		{
			color.X = 0.0f;
			color.Y = 1.0f;
			color.Z = 0.0f;
		}

		DX8Wrapper::Set_Pixel_Shader_Constant(1, &color, 1);
		const Vector4 fade_level(m_curFadeValue, m_curFadeValue, m_curFadeValue, 1.0f);
		DX8Wrapper::Set_Pixel_Shader_Constant(2, &fade_level, 1);
/*		const Vector4 grey_level(150.0f/255.0f, 150.0f/255.0f, 150.0f/255.0f, 0.0f);
		DX8Wrapper::Set_Pixel_Shader_Constant(2, &grey_level, 1);
		const Vector4 luminance_scale((765.0f/450.0f)/3, (765.0f/450.0f)/3, (765.0f/450.0f)/3, 1.0f);
		DX8Wrapper::Set_Pixel_Shader_Constant(3, &luminance_scale, 1);
		const Vector4 half_intensity(0.5f, 0.5f, 0.5f, 0);
		DX8Wrapper::Set_Pixel_Shader_Constant(4, &half_intensity, 1);
		const Vector4 shadow_tint((60.0f)/255.0f, (60.0f)/255.0f, (60.0f)/255.0f, 0);
		DX8Wrapper::Set_Pixel_Shader_Constant(5, &shadow_tint, 1);
		const Vector4 highlight_tint((157.0f)/255.0f, (157.0f)/255.0f, (157.0f)/255.0f, 0);
		DX8Wrapper::Set_Pixel_Shader_Constant(6, &highlight_tint, 1);
		const Vector4 midtone_tint((30.0f)/255.0f, (30.0f)/255.0f, (30.0f)/255.0f, 0);
		DX8Wrapper::Set_Pixel_Shader_Constant(7, &midtone_tint, 1);
*/
		return true;
	}
	return false;
}

void ScreenBWFilter::reset()
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilter::shutdown()
{
	if (m_dwBWPixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBWPixelShader);

	m_dwBWPixelShader=0;

	return TRUE;
}

/**Alternate version of the above filter which does not require pixel shaders - good for older cards*/
Int ScreenBWFilterDOT3::init()
{
	Int res;

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilterDOT3;
			return TRUE;
	}
	return FALSE;
}

Bool ScreenBWFilterDOT3::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilterDOT3::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();

	DWORD currentFade=(((Int)((1.0f-m_curFadeValue) * 255.0f))<<24) | 0x00ffffff;	//store alpha value

	v[0].color = currentFade;
	v[1].color = currentFade;
	v[2].color = currentFade;
	v[3].color = currentFade;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	//Draw B&W version first
	if (DX8Wrapper::Get_Current_Caps()->Support_Dot3())
	{	//Override W3D states with customizations for grayscale
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x80A5CA8E);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG0, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
	}
	else
	{	//doesn't have DOT3 blend mode so fake it another way.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x60606060);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	}

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	//Draw normal view blended by current fade level
	ShaderClass::Invalidate();	//reset DOT3 blend from above.
	ShaderClass shader=ShaderClass::_PresetAlphaShader;
	shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	//replace texture alpha with vertex alpha
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilterDOT3::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		return true;
	}
	return false;
}

void ScreenBWFilterDOT3::reset()
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilterDOT3::shutdown()
{
	return TRUE;
}

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades screen between 2 different views of the scene with both being visible at once.

Int ScreenCrossFadeFilter::m_fadeFrames;
Int ScreenCrossFadeFilter::m_curFadeFrame;
Real ScreenCrossFadeFilter::m_curFadeValue;
Int ScreenCrossFadeFilter::m_fadeDirection;
TextureClass *ScreenCrossFadeFilter::m_fadePatternTexture=nullptr;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;

ScreenCrossFadeFilter screenCrossFadeFilter;

///List of different BW shader implementations in order of preference
///@todo: Add a version that doesn't require pixel shader
W3DFilterInterface *ScreenCrossFadeFilterList[]=
{
	&screenCrossFadeFilter,
	nullptr
};

Int ScreenCrossFadeFilter::init()
{
	if (!TheDisplay)
		return FALSE;	//effect is useless without a view so no point initializing for the WB, etc.

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture())
		// Have to be able to render to texture.
		return FALSE;

	//Load an alpha mask texture that will mix foreground/background views.
	m_fadePatternTexture=WW3DAssetManager::Get_Instance()->Get_Texture("exmask_g.tga");
	if (!m_fadePatternTexture)
		return FALSE;
	m_fadePatternTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	W3DFilters[FT_VIEW_CROSSFADE]=&screenCrossFadeFilter;

	return TRUE;
}

Bool ScreenCrossFadeFilter::updateFadeLevel()
{
	if (m_fadeDirection > 0)
	{	//turning effect on
		m_curFadeFrame++;
		Int fade = m_curFadeFrame;

		if (fade<m_fadeFrames)
		{
			m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
		}
		else
		{
			m_curFadeFrame = 0;
			m_curFadeValue = 1.0f;
			m_fadeDirection = 0;
			return false;
		}
	}
	else
	if (m_fadeDirection < 0)
	{	//turning effect off
		Int fade = m_curFadeFrame;
		if (fade<m_fadeFrames)
		{
			m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			m_curFadeFrame++;
		}
		else
		{	m_curFadeValue = 0.0f;
			TheTacticalView->setViewFilterMode(FM_NULL_MODE);
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
			m_curFadeFrame = 0;
			m_fadeDirection = 0;
			return false;
		}
	}
	return true;
}

Bool ScreenCrossFadeFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (updateFadeLevel())
	{	//if fade has not completed
		W3DShaderManager::startRenderToTexture();
		scenePassMode=SCENE_PASS_ALPHA_MASK;
		skipRender = false;
		m_skipRender=true;	//tell the postRender function not to draw into framebuffer yet.
		return true;
	}
	//fade must have completed
	return true;
}

Bool ScreenCrossFadeFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex;

	if (m_skipRender)
	{
		//don't render anything to frame buffer because we still need to draw the new scene
		//that we're fading into.  Okay to render on the next call.
		m_skipRender = false;
		doExtraRender = TRUE;
		tex =	W3DShaderManager::endRenderToTexture();
		return true;
	}

	tex=W3DShaderManager::getRenderTexture();

	DEBUG_ASSERTCRASH(tex, ("Require last rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
		float	u1;
		float	v1;
	} v[4];

	Int xpos, ypos, width, height;
	Real radius = 0.0f;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	if (mode == FM_VIEW_CROSSFADE_CIRCLE)
	{	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1,m_fadePatternTexture->Peek_D3D_Texture());
		//Use the current fade level to scale the mask texture, for other modes the texture
		//comes pre-scaled so doesn't require uv scaling.
		radius = (1.0f-m_curFadeValue)*2.0f;
		if (radius <= 0)
			radius = 0.01f;
		radius = 0.5f/radius;
	}

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

/*	Real radius = (1.0f-m_curFadeValue);
	if (radius <= 0)
		radius = 0.01f;
	radius = 25.0f-radius*24.75f;
*/
	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[0].u1 = 0.5f+radius;	v[0].v1 = 0.5f+radius;
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[1].u1 = 0.5f+radius;	v[1].v1 = 0.5f-radius;
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[2].u1 = 0.5f-radius;	v[2].v1 = 0.5f+radius;
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[3].u1 = 0.5f-radius;	v[3].v1 = 0.5f-radius;

	DWORD diffuse = 0xffffffff;//((Int)((m_curFadeValue) * 255.0f) << 24) | 0x00ffffff;	//store alpha value in vertex diffuse

	v[0].color = diffuse;
	v[1].color = diffuse;
	v[2].color = diffuse;
	v[3].color = diffuse;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);

//		m_pDev->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
//		m_pDev->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_POINT);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenCrossFadeFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

		if (mode == FM_VIEW_CROSSFADE_CIRCLE)
		{	//cross-fading using circle mask stored in stage 1
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_MIPFILTER, D3DTEXF_NONE);
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);

		return true;
	}
	return false;
}

void ScreenCrossFadeFilter::reset()
{
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenCrossFadeFilter::shutdown()
{
	REF_PTR_RELEASE(m_fadePatternTexture);

	return TRUE;
}

/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.

ScreenMotionBlurFilter screenMotionBlurFilter;

Coord3D ScreenMotionBlurFilter::m_zoomToPos;
Bool ScreenMotionBlurFilter::m_zoomToValid = false;

ScreenMotionBlurFilter::ScreenMotionBlurFilter():
m_decrement(false),
m_maxCount(0),
m_lastFrame(0),
m_skipRender(false)
{
}
///List of different motion blur implementations in order of preference
W3DFilterInterface *ScreenMotionBlurFilterList[]=
{
	&screenMotionBlurFilter,
	nullptr
};

Int ScreenMotionBlurFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}
	W3DFilters[FT_VIEW_MOTION_BLUR_FILTER]=this;
	return true;
}

Bool ScreenMotionBlurFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = m_skipRender;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenMotionBlurFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	Bool continueEffect = true;
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;


	if (m_additive) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ONE);
	} else {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Apply_Render_State_Changes();
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	Coord2D center;
	center.x = 0.5f;
	center.y = 0.5f;
	Bool pan = false;
	if (mode>=FM_VIEW_MB_PAN_ALPHA) {
		Real len = sqrt(scrollDelta.x*scrollDelta.x + scrollDelta.y*scrollDelta.y);
		//center.x += 0.5f * (scrollDelta.x/len);
		center.y -= 0.5f; // * (scrollDelta.y/len);
		m_decrement = false;
		m_maxCount = (len*200*m_panFactor/(Real)DEFAULT_PAN_FACTOR);
		if (m_maxCount<m_panFactor/2)
			m_maxCount = m_panFactor/2;
		if (m_maxCount>m_panFactor)
			m_maxCount=m_panFactor;
		pan = true;
		m_priorDelta = scrollDelta;
	} else if (mode == FM_VIEW_MB_END_PAN_ALPHA) {
		Real len = sqrt(m_priorDelta.x*m_priorDelta.x + m_priorDelta.y*m_priorDelta.y);
		center.x += 0.5f * (m_priorDelta.x/len);
		center.y -= 0.5f * (m_priorDelta.y/len);
		m_decrement = false;
		m_maxCount--;
		if (m_maxCount<2) {
			continueEffect = false;
		}
		pan = true;
	}


	m_skipRender = false;
	if (!pan && m_lastFrame != TheGameLogic->getFrame()) {
		if (m_decrement) {
			m_maxCount-=COUNT_STEP;
			if (m_maxCount<1) {
				m_decrement = false;
				continueEffect = false;
			}	else {
				m_skipRender = true;
			}
		} else {
			m_maxCount+=COUNT_STEP;
			if (m_maxCount>=MAX_COUNT) {
				m_decrement = true;
				if (m_doZoomTo && m_zoomToValid) {
					TheTacticalView->lookAt(&m_zoomToPos);
				} else {
					continueEffect = false;
				}
			}	else {
				m_skipRender = true;
			}
		}
	}
	Int	 i, j;
	if (!pan) {
		for (i=0; i<4; i++) {
			Real factor = 1.0f - (m_maxCount/(Real)MAX_COUNT)*0.90f;
			factor = sqrt(factor);
			v[i].u = ((v[i].u-center.x)*factor) + center.x;
			v[i].v = ((v[i].v-center.y)*factor) + center.y;
		}
	}
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);

	DX8Wrapper::Apply_Render_State_Changes();
	{
		Int limit = m_maxCount;
		if (m_maxCount>30) limit = 30;
		for (j=0; j<limit; j++) {
			for (i=0; i<4; i++) {
				Real factor = 0.99f;
				if (m_additive) factor = 0.98f;
				Int alpha = 0x15;
				if (m_additive) {
					alpha = 0x09;
					if (m_maxCount>limit) {
						alpha += (m_maxCount-limit)/5;
					}
					if (m_maxCount==MAX_COUNT) alpha += 60;
				}
				v[i].color = (alpha<<24)|0x00ffffff; //
				if (pan) {
					v[i].u = ((v[i].u-center.x)*(factor+.006)) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				} else {
					v[i].u = ((v[i].u-center.x)*factor) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				}
			}
			pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

		}
	}
	m_lastFrame = TheGameLogic->getFrame();
	if (pan){
		m_skipRender = false;
	}
	reset();
	if (!continueEffect) {
		m_zoomToValid = false;
	}
	return continueEffect;
}

Bool ScreenMotionBlurFilter::setup(FilterModes mode)
{

	m_additive = false;

	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_SATURATE ||
			mode == FM_VIEW_MB_OUT_SATURATE) {
		m_additive = true;
	}

	m_doZoomTo = false;
	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_AND_OUT_ALPHA ) {
		m_doZoomTo = true;
	}
	if (mode >= FM_VIEW_MB_PAN_ALPHA)	{
		m_panFactor = (int)mode - FM_VIEW_MB_PAN_ALPHA;
		if (m_panFactor<1) m_panFactor = DEFAULT_PAN_FACTOR;
	}
	m_skipRender = false;
	if (mode != FM_VIEW_MB_END_PAN_ALPHA)
		m_maxCount = 0;
	m_decrement = false;
	m_skipRender = false;
	switch (mode) {
		case FM_VIEW_MB_OUT_SATURATE:
		case FM_VIEW_MB_OUT_ALPHA:
			m_maxCount = MAX_COUNT;
			m_decrement = TRUE;
			break;
	}
	return true;
}

Int ScreenMotionBlurFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface motion blurred

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	}
	return TRUE;
}

void ScreenMotionBlurFilter::reset()
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenMotionBlurFilter::shutdown()
{
	return TRUE;
}

/*===========================================================================================*/
/*=========      Shroud Shaders	=============================================================*/
/*===========================================================================================*/

///Shroud layer rendering shader
class ShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} shroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *ShroudShaderList[]=
{
	&shroudTextureShader,
	nullptr
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int ShroudTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_SHROUD_TEXTURE]=&shroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int ShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Texture(stage, W3DShaderManager::getShaderTexture(0));	//shroud always stored in texture 0

	if (stage == 0)
	{
#if defined(RTS_DEBUG)
	if (TheGlobalData && TheGlobalData->m_fogOfWarOn)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaSpriteShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#else
	DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#endif
	}
	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_EQUAL);

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		D3DMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		Set_D3DMATRIX_Translation(offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		Set_D3DMATRIX_Scaling(scale, width, height, 1);
		curView = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
	}
	m_stageOfSet=stage;
	return TRUE;
}

void ShroudTextureShader::reset()
{
	DX8Wrapper::Set_Texture(m_stageOfSet,nullptr);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXCOORDINDEX, m_stageOfSet);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

///Shroud layer rendering shader
class FlatShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} flatShroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *FlatShroudShaderList[]=
{
	&flatShroudTextureShader,
	nullptr
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int FlatShroudTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_FLAT_SHROUD_TEXTURE]=&flatShroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int FlatShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	if (stage < 2)
		DX8Wrapper::Set_Texture(stage, W3DShaderManager::getShaderTexture(stage));
	else	//stages larger than 1 are not supported by W3D so set them directly
		DX8Wrapper::Set_DX8_Texture(stage, W3DShaderManager::getShaderTexture(stage)->Peek_D3D_Texture());

	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	//DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		D3DMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		Set_D3DMATRIX_Translation(offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		Set_D3DMATRIX_Scaling(scale, width, height, 1);
		curView = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
	}
	m_stageOfSet=stage;
	return TRUE;
}

void FlatShroudTextureShader::reset()
{
	if (m_stageOfSet < MAX_TEXTURE_STAGES)
		DX8Wrapper::Set_Texture(m_stageOfSet,nullptr);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXCOORDINDEX, m_stageOfSet);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

///Mask layer rendering shader
class MaskTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
} maskTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *MaskShaderList[]=
{
	&maskTextureShader,
	nullptr
};

Int MaskTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_MASK_TEXTURE]=&maskTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_MASK_TEXTURE]=1;

	return TRUE;
}

Int MaskTextureShader::set(Int pass)
{
	Real fadeLevel=ScreenCrossFadeFilter::getCurrentFadeValue();

	//Use the current fade level to scale the mask texture
	Real radius = (1.0f-fadeLevel)*2.0f;
	if (radius <= 0)
		radius = 0.01f;
	radius = 0.5f/radius;

	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

	//For now we're always going to project the texture coming from the crossfade effect
	DX8Wrapper::Set_Texture(0, ScreenCrossFadeFilter::getCurrentMaskTexture());
	ShaderClass shader=ShaderClass::_PresetOpaqueShader;
	shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();

	D3DMATRIX curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	D3DMATRIX inv;
	float det;

	//Get inverse view matrix so we can transform camera space points back to world space
	Invert_D3DMATRIX(inv, &det, curView);

	D3DMATRIX scale,offset,offsetTextureCenter;
	Coord3D centerPos;
	centerPos.zero();

	//Find center of projection (this should be returned from some other filter, etc. but
	//for now assume terrain location at center of screen.
	if (TheTacticalView)
	{	Int xpos,ypos;

		TheTacticalView->getOrigin(&xpos,&ypos);

		ICoord2D screenPos;
		screenPos.x=(Real)TheTacticalView->getWidth()*0.5f;
		screenPos.y=(Real)TheTacticalView->getHeight()*0.5f;
		TheTacticalView->screenToTerrain(&screenPos,&centerPos);
	}

	Set_D3DMATRIX_Translation(offset, -centerPos.x, -centerPos.y,0);

	Set_D3DMATRIX_Translation(offsetTextureCenter, 0.5f, 0.5f, 0);	//shift coordinates so center of projection falls at uv 0.5,0.5

	Real worldTexelWidth=(1.0f-fadeLevel)*25.0f;	//9 worked well for circle but weird shape requires more stretch to cover.
	Real worldTexelHeight=(1.0f-fadeLevel)*25.0f;

	///@todo: Fix this to work with non 128x128 textures.
	if (worldTexelWidth != 0 && worldTexelHeight != 0)
	{
		Real widthScale = 1.0f/(worldTexelWidth*128.0f);
		Real heightScale = 1.0f/(worldTexelHeight*128.0f);
		Set_D3DMATRIX_Scaling(scale, widthScale, heightScale, 1);
		curView = ((inv * offset) * scale)*offsetTextureCenter;
	}
	else
	{
		Set_D3DMATRIX_Scaling(scale, 0, 0, 1);	//scaling by 0 will set uv coordinates to 0,0
		curView = ((inv * offset) * scale);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);

	return TRUE;
}

void MaskTextureShader::reset()
{
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

/*===========================================================================================*/
/*=========      Shadow Map Depth Shader	=================================================*/
/*===========================================================================================*/

// The sun the specular pass lights with, set once a frame by the scene.
static Vector3 SpecularToSun(0.0f, 0.0f, 1.0f);
static Vector3 SpecularColor(0.0f, 0.0f, 0.0f);
static Real SpecularPower = 24.0f;
static Bool SpecularDebug = FALSE;
static Int SpecularPassCount = 0;

// The bump detail the specular pass shades, set once a frame by the scene.
static Vector3 SpecularSunDiffuse(0.0f, 0.0f, 0.0f);
static Bool BumpEnabled = FALSE;
static Real BumpAmbient = 0.0f;
static Real BumpHeight = 0.0f;
static Real BumpNormalMapStrength = 1.0f;
static Bool BumpSupported = FALSE;
static Int BumpDerivedCount = 0;
static Int BumpNormalMapCount = 0;

// The glow masks the specular pass adds, set once a frame by the scene.
static Real EmissiveIntensity = 0.0f;
static Int EmissiveMapCount = 0;
#if defined(RTS_ZEROHOUR)
static Real TerrainEmissiveScale = 1.0f;

void W3DShaderManager::setTerrainEmissiveScale(Real scale)
{
	TerrainEmissiveScale = scale;
}
#endif

static Real Bind_Terrain_Emissive(Bool terrain)
{
#if defined(BUILD_WITH_D3D9) && defined(RTS_ZEROHOUR)
	TextureClass *atlas = terrain ? W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_EMISSIVE_TEXTURE) : nullptr;
	const Bool enabled = atlas != nullptr && atlas->Peek_D3D_Texture() != nullptr && EmissiveIntensity > 0.0f;
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	device->SetTexture(15, enabled ? atlas->Peek_D3D_Texture() : nullptr);
	device->SetSamplerState(15, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(15, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(15, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(15, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(15, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	return enabled ? EmissiveIntensity * TerrainEmissiveScale : 0.0f;
#else
	(void)terrain;
	return 0.0f;
#endif
}

// The point lights draws may add, set once a frame by the scene. Each draw names its own.
static std::vector<W3DShaderManager::PixelLight> PixelLights;
static Int PixelLightCount = 0;

// A key per light that sorts neighbours on the ground next to each other.
static std::vector<UnsignedInt> PixelLightOrder;
static const Real PIXEL_LIGHT_ORDER_CELL = 16.0f;

// Past a handful of lights, those no wider than a cell sit in a ground grid by their centre and the rest in a list.
static const Int PIXEL_LIGHT_GRID_MIN_LIGHTS = 32;
static const Int PIXEL_LIGHT_GRID_MAX_SIDE = 128;
static const Real PIXEL_LIGHT_GRID_MIN_CELL = 64.0f;
static std::vector<Int> PixelLightGridStarts;
static std::vector<Int> PixelLightGridLights;
static std::vector<Int> PixelLightGridWide;
static Int PixelLightGridWidth = 0;
static Int PixelLightGridHeight = 0;
static Real PixelLightGridCell = 0.0f;
static Real PixelLightGridMinX = 0.0f;
static Real PixelLightGridMinY = 0.0f;

static Int Pixel_Light_Grid_Column(Real x)
{
	return (Int)min(max((x - PixelLightGridMinX) / PixelLightGridCell, 0.0f), (Real)(PixelLightGridWidth - 1));
}

static Int Pixel_Light_Grid_Row(Real y)
{
	return (Int)min(max((y - PixelLightGridMinY) / PixelLightGridCell, 0.0f), (Real)(PixelLightGridHeight - 1));
}

static void Build_Pixel_Light_Grid()
{
	PixelLightGridWidth = 0;
	PixelLightGridHeight = 0;
	PixelLightGridWide.clear();
	if (PixelLightCount < PIXEL_LIGHT_GRID_MIN_LIGHTS)
	{
		return;
	}

	Real minX = PixelLights[0].position.X;
	Real minY = PixelLights[0].position.Y;
	Real maxX = minX;
	Real maxY = minY;
	for (Int i = 1; i < PixelLightCount; i++)
	{
		minX = min(minX, PixelLights[i].position.X);
		minY = min(minY, PixelLights[i].position.Y);
		maxX = max(maxX, PixelLights[i].position.X);
		maxY = max(maxY, PixelLights[i].position.Y);
	}
	PixelLightGridMinX = minX;
	PixelLightGridMinY = minY;
	PixelLightGridCell = max(PIXEL_LIGHT_GRID_MIN_CELL, max(maxX - minX, maxY - minY) / (Real)PIXEL_LIGHT_GRID_MAX_SIDE);
	PixelLightGridWidth = (Int)((maxX - minX) / PixelLightGridCell) + 1;
	PixelLightGridHeight = (Int)((maxY - minY) / PixelLightGridCell) + 1;

	// Counted first, so each cell's lights lie together in one array.
	static std::vector<Int> cells;
	cells.resize(PixelLightCount);
	PixelLightGridStarts.assign(PixelLightGridWidth * PixelLightGridHeight + 1, 0);
	for (Int i = 0; i < PixelLightCount; i++)
	{
		const W3DShaderManager::PixelLight &light = PixelLights[i];
		if (light.outerRadius > PixelLightGridCell)
		{
			cells[i] = -1;
			PixelLightGridWide.push_back(i);
			continue;
		}
		cells[i] = Pixel_Light_Grid_Row(light.position.Y) * PixelLightGridWidth + Pixel_Light_Grid_Column(light.position.X);
		PixelLightGridStarts[cells[i] + 1]++;
	}
	for (size_t cell = 1; cell < PixelLightGridStarts.size(); cell++)
	{
		PixelLightGridStarts[cell] += PixelLightGridStarts[cell - 1];
	}
	static std::vector<Int> next;
	next.assign(PixelLightGridStarts.begin(), PixelLightGridStarts.end());
	PixelLightGridLights.resize(PixelLightCount);
	for (Int i = 0; i < PixelLightCount; i++)
	{
		if (cells[i] >= 0)
		{
			PixelLightGridLights[next[cells[i]]++] = i;
		}
	}
}

// Moves the low 16 bits apart, leaving a zero between each.
static UnsignedInt Spread_Bits(UnsignedInt value)
{
	value &= 0x0000ffff;
	value = (value | (value << 8)) & 0x00ff00ff;
	value = (value | (value << 4)) & 0x0f0f0f0f;
	value = (value | (value << 2)) & 0x33333333;
	value = (value | (value << 1)) & 0x55555555;
	return value;
}
enum { PIXEL_LIGHT_REGISTERS = W3DShaderManager::MAX_PIXEL_LIGHTS * 2 + (W3DShaderManager::MAX_PIXEL_LIGHTS + 3) / 4 };
static Bool TerrainPixelLightsLoaded = FALSE;
static Bool UnitPixelLightsLoaded = FALSE;
static Bool RoadPixelLightsLoaded = FALSE;
static Bool FlatPixelLightsLoaded = FALSE;
static Bool PointLightPassLoaded = FALSE;

// The ground shader in use without and with the lights, which setDrawPixelLights picks between draw by draw.
static DWORD DrawUnlitShader = 0;
static DWORD DrawLitShader = 0;

// The terrain's seabed hex tiling, set once a frame by the terrain.
static TextureClass *SeabedClassMap = nullptr;
static TextureClass *SeabedWaterMask = nullptr;
static TextureClass *SeabedPainted = nullptr;
static Vector4 SeabedConstants[W3DShaderManager::SEABED_CONSTANTS];
static Bool TerrainSeabedLoaded = FALSE;

// The terrain shaders setDrawTerrain picks between: the unlit one bound, and the seabed variants without and with the lights.
static DWORD DrawGroundShader = 0;
static DWORD DrawSeabedUnlitShader = 0;
static DWORD DrawSeabedLitShader = 0;

// Where terrainshadow.hlsl reads the seabed, past the two point lights its seabed variants keep.
#define SEABED_REGISTER 14
#define SEABED_CLASS_MAP_SAMPLER 8
#define SEABED_WATER_MASK_SAMPLER 9
#define SEABED_PAINTED_SAMPLER 13
#define SEABED_PAINTED_SEED_SAMPLER 14

// Where heightblend.hlsli reads the height atlas, and its constants in the terrain and road shaders.
#define HEIGHT_ATLAS_SAMPLER 10
#define TERRAIN_HEIGHT_BLEND_REGISTER 3
#define ROAD_HEIGHT_BLEND_REGISTER 1

// Whether the terrain draws only through shaders that blend by height, and whether road draws are blend tiles.
static Bool TerrainHeightBlendLoaded = FALSE;
static Bool RoadHeightBlendTiles = FALSE;

static void Unbind_Height_Atlas()
{
#if defined(BUILD_WITH_D3D9)
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(HEIGHT_ATLAS_SAMPLER, nullptr);
#endif
}

// Binds the heights or nothing and returns their constants; a sharpness under 1 would spill the blend into unblended cells.
static Vector4 Bind_Height_Blend(TextureClass *heights)
{
#if defined(BUILD_WITH_D3D9)
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (heights == nullptr || heights->Peek_D3D_Texture() == nullptr)
	{
		device->SetTexture(HEIGHT_ATLAS_SAMPLER, nullptr);
		return Vector4(0.0f, 1.0f, 0.5f, 0.0f);
	}

	device->SetTexture(HEIGHT_ATLAS_SAMPLER, heights->Peek_D3D_Texture());
	device->SetSamplerState(HEIGHT_ATLAS_SAMPLER, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(HEIGHT_ATLAS_SAMPLER, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(HEIGHT_ATLAS_SAMPLER, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(HEIGHT_ATLAS_SAMPLER, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(HEIGHT_ATLAS_SAMPLER, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	return Vector4(4.0f * max(TheGlobalData->m_terrainHeightBlendStrength, 0.0f),
		max(TheGlobalData->m_terrainHeightBlendSharpness, 1.0f), 0.5f, 0.0f);
#else
	(void)heights;
	return Vector4(0.0f, 1.0f, 0.5f, 0.0f);
#endif
}

// The lights of the object whose specular pass is installed.
struct SpecularPassLights
{
	std::vector<Int> lights;
	Int count;
	Bool lightsOnly;	///< no highlight, bumps or glow
	Bool receivesShadow;	///< the pass also does the shadow receiver's work
};
static const SpecularPassLights *CurrentSpecularLights = nullptr;
static D3DMATRIX SpecularLightView;

// Bisects per-pixel light faults without a rebuild. CONTRA_PIXELLIGHTS=0 leaves every light
// to the vertex lighting, 1 draws them per pixel on the terrain only, 2 on units as well.
enum { PIXEL_LIGHTS_OFF = 0, PIXEL_LIGHTS_TERRAIN = 1, PIXEL_LIGHTS_ALL = 2 };

static Int Get_Pixel_Light_Mode()
{
	const char *value = getenv("CONTRA_PIXELLIGHTS");
	return (value != nullptr) ? atoi(value) : PIXEL_LIGHTS_ALL;
}

// Whether any lit shader carries its lights-only twin.
static Bool LightsDebugAvailable = FALSE;

#if defined(BUILD_WITH_D3D9)
struct AdditionalLightShaders
{
	AdditionalLightShaders() : normal(nullptr), debug(nullptr) {}
	IDirect3DPixelShader9 *normal;
	IDirect3DPixelShader9 *debug;
};
static std::map<DWORD, AdditionalLightShaders> AdditionalShaders;

static void Attach_Additional_Light_Shaders(const char *path, DWORD handle)
{
	std::map<DWORD, AdditionalLightShaders>::iterator old = AdditionalShaders.find(handle);
	if (old != AdditionalShaders.end())
	{
		Set_D3D9_Shader_Name(old->second.normal, nullptr);
		SAFE_RELEASE(old->second.normal);
		Set_D3D9_Shader_Name(old->second.debug, nullptr);
		SAFE_RELEASE(old->second.debug);
		AdditionalShaders.erase(old);
	}
	const size_t length = strlen(path);
	if (length < 4)
	{
		return;
	}
	AdditionalLightShaders shaders;
	for (Int debug = 0; debug < 2; debug++)
	{
		char extraPath[_MAX_PATH];
		snprintf(extraPath, sizeof(extraPath), "%.*sextralights%s.pso", (Int)length - 4, path, debug ? "debug" : "");
		const void *data = nullptr;
		DWORD size = 0;
		if (!Find_Embedded_Shader(extraPath, data, size))
		{
			break;
		}
		IDirect3DPixelShader9 **shader = debug ? &shaders.debug : &shaders.normal;
		DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader((const DWORD *)data, shader);
		Set_D3D9_Shader_Name(*shader, extraPath);
	}
	if (shaders.normal != nullptr)
	{
		AdditionalShaders[handle] = shaders;
	}
	else
	{
		Set_D3D9_Shader_Name(shaders.debug, nullptr);
		SAFE_RELEASE(shaders.debug);
	}
}

static std::vector<Int> AdditionalLightIndices;
static Int AdditionalLightSlots = 0;
static Int AdditionalLightRegister = 0;
static D3DMATRIX AdditionalLightView;
static Bool AdditionalLightsInView = FALSE;
static DWORD AdditionalLightShader = 0;
static const D3DRENDERSTATETYPE AdditionalLightStates[] = { D3DRS_ALPHABLENDENABLE, D3DRS_DESTBLEND, D3DRS_ZWRITEENABLE, D3DRS_FOGCOLOR, D3DRS_BLENDOP, D3DRS_SRCBLEND };
static DWORD SavedLightStates[ARRAY_SIZE(AdditionalLightStates)];
static D3DVIEWPORT9 AdditionalLightViewport;
static D3DMATRIX AdditionalLightWorldToClip;
static Bool AdditionalLightScissor = FALSE;
static size_t AdditionalLightNext = 0;
#endif

// DynamicLightsDebug draws every lightable surface through its lit shader, so unlit ground and objects go black too.
static Bool Get_Lights_Debug()
{
#if RTS_ZEROHOUR
	return LightsDebugAvailable && TheGlobalData != nullptr && TheGlobalData->m_dynamicLightsDebug &&
		TheGlobalData->m_useDynamicLights && TheGlobalData->m_usePixelLights;
#else
	return FALSE;
#endif
}

// Names the lights-only twin the build compiles for a lit pixel shader.
static Bool Get_Lights_Debug_Path(const char *path, char *debugPath, size_t size)
{
	const size_t length = strlen(path);
	if (length < 4 || _stricmp(path + length - 4, ".pso") != 0 || strstr(path, "lightsdebug") != nullptr)
	{
		return FALSE;
	}
	snprintf(debugPath, size, "%.*slightsdebug.pso", (int)(length - 4), path);
	return TRUE;
}

#if defined(BUILD_WITH_D3D9)
// Hangs a lit shader's lights-only twin on its handle, which binds it while the debug view is on.
static void Attach_Lights_Debug_Twin(const char *path, DWORD handle)
{
	char debugPath[_MAX_PATH];
	const void *data = nullptr;
	DWORD size = 0;
	if (!Get_Lights_Debug_Path(path, debugPath, sizeof(debugPath)) || !Find_Embedded_Shader(debugPath, data, size))
	{
		return;
	}

	IDirect3DPixelShader9 *shader = nullptr;
	if (SUCCEEDED(DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader((const DWORD *)data, &shader)))
	{
		Attach_D3D9_Debug_Pixel_Shader(handle, shader);
		LightsDebugAvailable = TRUE;
	}
	else
	{
		RENDER_LOG(("LoadAndCreateD3DShader: could not create %s", debugPath));
	}
}
#endif

// Switches the lit shaders to their twins or back as the frame starts, and rebinds whatever was bound.
static void Update_Lights_Debug()
{
#if defined(BUILD_WITH_D3D9)
	if (Set_D3D9_Debug_Pixel_Shaders(Get_Lights_Debug() != FALSE))
	{
		DX8Wrapper::Set_Pixel_Shader(0);
	}
#endif
}

// Packs the given lights, or the first count without indices, as pointlights.hlsli reads them in a shader
// with the given slots. A view takes them into camera space, and none leaves them in world space.
static Int Pack_Pixel_Lights(Vector4 *constants, Int slots, const Int *indices, Int count, const D3DMATRIX *view)
{
	memset(constants, 0, sizeof(Vector4) * PIXEL_LIGHT_REGISTERS);

	count = min(count, slots);
	for (Int i = 0; i < count; i++)
	{
		const Int index = (indices != nullptr) ? indices[i] : i;
		if (index < 0 || index >= PixelLightCount)
		{
			continue;
		}
		const W3DShaderManager::PixelLight &light = PixelLights[index];

		Vector3 position = light.position;
		if (view != nullptr)
		{
			const Vector3 world = position;
			position.X = world.X * view->m[0][0] + world.Y * view->m[1][0] + world.Z * view->m[2][0] + view->m[3][0];
			position.Y = world.X * view->m[0][1] + world.Y * view->m[1][1] + world.Z * view->m[2][1] + view->m[3][1];
			position.Z = world.X * view->m[0][2] + world.Y * view->m[1][2] + world.Z * view->m[2][2] + view->m[3][2];
		}

		// Full strength inside the inner radius, falling to nothing at the outer one.
		const Real scale = 1.0f / (light.outerRadius - light.innerRadius);
		constants[i * 2].Set(position.X, position.Y, position.Z, scale);
		constants[i * 2 + 1].Set(light.diffuse.X, light.diffuse.Y, light.diffuse.Z, 1.0f + light.innerRadius * scale);
		(&constants[slots * 2 + i / 4].X)[i % 4] = light.ambientScale;
	}

	return slots * 2 + (slots + 3) / 4;
}

#if defined(BUILD_WITH_D3D9)
static bool Pixel_Light_Screen_Rect(const W3DShaderManager::PixelLight &light, const D3DMATRIX &transform,
	const D3DVIEWPORT9 &viewport, RECT &rectangle)
{
	rectangle.left = viewport.X;
	rectangle.top = viewport.Y;
	rectangle.right = viewport.X + viewport.Width;
	rectangle.bottom = viewport.Y + viewport.Height;
	double left = DBL_MAX;
	double top = DBL_MAX;
	double right = -DBL_MAX;
	double bottom = -DBL_MAX;
	for (Int corner = 0; corner < 8; corner++)
	{
		const double x = light.position.X + ((corner & 1) ? (double)light.outerRadius : -(double)light.outerRadius);
		const double y = light.position.Y + ((corner & 2) ? (double)light.outerRadius : -(double)light.outerRadius);
		const double z = light.position.Z + ((corner & 4) ? (double)light.outerRadius : -(double)light.outerRadius);
		const double clipX = x * transform.m[0][0] + y * transform.m[1][0] + z * transform.m[2][0] + transform.m[3][0];
		const double clipY = x * transform.m[0][1] + y * transform.m[1][1] + z * transform.m[2][1] + transform.m[3][1];
		const double clipZ = x * transform.m[0][2] + y * transform.m[1][2] + z * transform.m[2][2] + transform.m[3][2];
		const double clipW = x * transform.m[0][3] + y * transform.m[1][3] + z * transform.m[2][3] + transform.m[3][3];
		// A box crossing the near plane keeps the full viewport because its projection is unbounded.
		if (!_finite(clipX) || !_finite(clipY) || !_finite(clipZ) || !_finite(clipW) || clipZ <= 0.0 || clipW <= 1e-6)
		{
			return true;
		}
		const double pixelX = viewport.X + (clipX / clipW + 1.0) * viewport.Width * 0.5;
		const double pixelY = viewport.Y + (1.0 - clipY / clipW) * viewport.Height * 0.5;
		left = min(left, pixelX);
		top = min(top, pixelY);
		right = max(right, pixelX);
		bottom = max(bottom, pixelY);
	}
	// Two pixels cover raster rounding and the shader's derivative helper pixels.
	rectangle.left = (LONG)max((double)viewport.X, min((double)rectangle.right, floor(left) - 2.0));
	rectangle.top = (LONG)max((double)viewport.Y, min((double)rectangle.bottom, floor(top) - 2.0));
	rectangle.right = (LONG)min((double)rectangle.right, max((double)viewport.X, ceil(right) + 2.0));
	rectangle.bottom = (LONG)min((double)rectangle.bottom, max((double)viewport.Y, ceil(bottom) + 2.0));
	return rectangle.left < rectangle.right && rectangle.top < rectangle.bottom;
}

static bool Additional_Light_Screen_Rect(size_t first, RECT &rectangle)
{
	rectangle.left = AdditionalLightViewport.X + AdditionalLightViewport.Width;
	rectangle.top = AdditionalLightViewport.Y + AdditionalLightViewport.Height;
	rectangle.right = AdditionalLightViewport.X;
	rectangle.bottom = AdditionalLightViewport.Y;
	const size_t end = min(first + (size_t)AdditionalLightSlots, AdditionalLightIndices.size());
	for (size_t i = first; i < end; i++)
	{
		const Int index = AdditionalLightIndices[i];
		RECT lightRectangle;
		if (index >= 0 && index < PixelLightCount &&
			Pixel_Light_Screen_Rect(PixelLights[index], AdditionalLightWorldToClip, AdditionalLightViewport, lightRectangle))
		{
			rectangle.left = min(rectangle.left, lightRectangle.left);
			rectangle.top = min(rectangle.top, lightRectangle.top);
			rectangle.right = max(rectangle.right, lightRectangle.right);
			rectangle.bottom = max(rectangle.bottom, lightRectangle.bottom);
		}
	}
	return rectangle.left < rectangle.right && rectangle.top < rectangle.bottom;
}

static bool Draw_Additional_Light_Pass(unsigned pass)
{
	IDirect3DDevice9 *device = DX8Wrapper::_Get_D3D_Device8();
	const Int registers = AdditionalLightSlots * 2 + (AdditionalLightSlots + 3) / 4;
	if (pass == 1)
	{
		// Direct device writes leave the wrapper's record of the base pass intact, and the device goes back to it afterwards.
		if (Peek_D3D9_Pixel_Shader(DX8Wrapper::Get_Pixel_Shader()) == nullptr)
		{
			return false;
		}
		for (Int i = 0; i < ARRAY_SIZE(AdditionalLightStates); i++)
		{
			SavedLightStates[i] = DX8Wrapper::Get_Device_Render_State(AdditionalLightStates[i]);
		}
		AdditionalLightScissor = (DX8Wrapper::Get_Device_Render_State(D3DRS_COLORWRITEENABLE) & D3DCOLORWRITEENABLE_ALPHA) == 0;
		if (AdditionalLightScissor)
		{
			D3DMATRIX view, projection;
			DX8Wrapper::Get_Viewport(AdditionalLightViewport);
			DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projection);
			if (!AdditionalLightsInView)
			{
				DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
			}
			AdditionalLightWorldToClip = (AdditionalLightsInView ? AdditionalLightView : view) * projection;
		}
		AdditionalLightNext = AdditionalLightSlots;
		// Opaque draws leave their source blend factor unspecified.
		if (!SavedLightStates[0])
		{
			device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		}
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		device->SetRenderState(D3DRS_FOGCOLOR, 0);
		device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		const AdditionalLightShaders &shaders = AdditionalShaders.find(AdditionalLightShader)->second;
		device->SetPixelShader(Get_Lights_Debug() && shaders.debug != nullptr ? shaders.debug : shaders.normal);
	}
	size_t first = AdditionalLightNext;
	RECT rectangle;
	while (first < AdditionalLightIndices.size() && AdditionalLightScissor && !Additional_Light_Screen_Rect(first, rectangle))
	{
		first += AdditionalLightSlots;
	}
	if (first >= AdditionalLightIndices.size())
	{
		device->SetPixelShader(Peek_D3D9_Pixel_Shader(DX8Wrapper::Get_Pixel_Shader()));
		device->SetPixelShaderConstantF(AdditionalLightRegister, &DX8Wrapper::Peek_Pixel_Shader_Constants(AdditionalLightRegister)->X, registers);
		for (Int i = 0; i < ARRAY_SIZE(AdditionalLightStates); i++)
		{
			device->SetRenderState(AdditionalLightStates[i], SavedLightStates[i]);
		}
		// No other draw scissors, so off is the state to leave behind.
		if (AdditionalLightScissor)
		{
			device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		}
		return false;
	}
	if (AdditionalLightScissor)
	{
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, SUCCEEDED(device->SetScissorRect(&rectangle)) ? TRUE : FALSE);
	}
	AdditionalLightNext = first + AdditionalLightSlots;
	Vector4 constants[PIXEL_LIGHT_REGISTERS];
	Pack_Pixel_Lights(constants, AdditionalLightSlots, &AdditionalLightIndices[first],
		(Int)(AdditionalLightIndices.size() - first), AdditionalLightsInView ? &AdditionalLightView : nullptr);
	device->SetPixelShaderConstantF(AdditionalLightRegister, &constants[0].X, registers);
	return true;
}
#endif

#if defined(BUILD_WITH_D3D9)
static bool Pixel_Light_Nearer_First(Int a, Int b)
{
	const UnsignedInt keyA = (a >= 0 && a < (Int)PixelLightOrder.size()) ? PixelLightOrder[a] : 0xffffffff;
	const UnsignedInt keyB = (b >= 0 && b < (Int)PixelLightOrder.size()) ? PixelLightOrder[b] : 0xffffffff;
	return keyA < keyB;
}
#endif

static void Set_Additional_Pixel_Lights(DWORD shader, const Int *indices, Int count, Int slots, Int firstRegister, const D3DMATRIX *view)
{
#if defined(BUILD_WITH_D3D9)
	DX8Wrapper::Set_Additional_Pass_Callback(nullptr);
	Bool enabled = FALSE;
#if defined(RTS_ZEROHOUR)
	enabled = TheGlobalData->m_usePathTracing;
#endif
	if (!enabled || count <= slots || AdditionalShaders.find(shader) == AdditionalShaders.end())
	{
		return;
	}
	AdditionalLightIndices.resize(count);
	for (Int i = 0; i < count; i++)
	{
		AdditionalLightIndices[i] = indices != nullptr ? indices[i] : i;
	}

	// Each extra pass scissors to its lights, so neighbours share a pass and it shades a small area.
	std::sort(AdditionalLightIndices.begin() + slots, AdditionalLightIndices.end(), Pixel_Light_Nearer_First);
	AdditionalLightSlots = slots;
	AdditionalLightRegister = firstRegister;
	AdditionalLightShader = shader;
	AdditionalLightsInView = view != nullptr;
	if (view != nullptr)
	{
		AdditionalLightView = *view;
	}
	DX8Wrapper::Set_Additional_Pass_Callback(Draw_Additional_Light_Pass);
#endif
}

// Every ground shader reads its world-space lights from c5.
#define GROUND_POINT_LIGHT_REGISTER 5

// Hands draw-by-draw light choice to a ground shader just set, whose unlit variant is bound. A zero
// unlit shader leaves the lit one bound, for passes that only add the lights.
static void Begin_Draw_Pixel_Lights(DWORD unlit, DWORD lit)
{
	Set_Additional_Pixel_Lights(0, nullptr, 0, 0, 0, nullptr);
	DrawUnlitShader = unlit;
	DrawLitShader = lit;
}

static void End_Draw_Pixel_Lights()
{
	Set_Additional_Pixel_Lights(0, nullptr, 0, 0, 0, nullptr);
	DrawUnlitShader = 0;
	DrawLitShader = 0;
	DrawGroundShader = 0;
	DrawSeabedUnlitShader = 0;
	DrawSeabedLitShader = 0;
}

// The terrain normal maps, set once a frame by the scene.
static Bool TerrainBumpEnabled = FALSE;
static Bool TerrainBumpSupported = FALSE;
static Real TerrainBumpStrength = 1.0f;
static Bool TerrainBumpDebug = FALSE;
static Int TerrainBumpCount = 0;
static Bool RoadBumpLoaded = FALSE;
static Bool RoadDerivedLoaded = FALSE;
static Real RoadBumpHeight = 0.0f;
static Int RoadBumpCount = 0;

// The sun's glint on the ground, set once a frame by the scene and the terrain.
static Bool TerrainGlintEnabled = FALSE;
static Real TerrainGlintIntensity = 0.0f;
static Real TerrainGlintGloss = 1.0f;
static Real TerrainGlintAlbedo = 0.0f;
static TextureClass *TerrainGlintNormals = nullptr;
static Vector4 TerrainGlintMapping(0.0f, 0.0f, 0.0f, 0.0f);
static TextureClass *TerrainGlintMaterials = nullptr;
static Real TerrainGlintStrengthScale = 1.0f;
static Real TerrainGlintGlossScale = 1.0f;
static Bool TerrainGlintLoaded = FALSE;
static Bool RoadGlintLoaded = FALSE;

// Where terrainglint.hlsli reads the normals and its constants, the terrain shaders each texture's glint, and the ground shaders the sun.
#define GLINT_NORMAL_SAMPLER 11
#define GLINT_MATERIAL_SAMPLER 12
#define GLINT_REGISTER 23
#define TERRAIN_SUN_REGISTER 1
#define ROAD_SUN_REGISTER 2

// Bisects glint faults without a rebuild. CONTRA_TERRAINGLINT=0 loads none of the glint-only shaders.
static Bool Get_Terrain_Glint_Allowed()
{
	const char *value = getenv("CONTRA_TERRAINGLINT");
	return (value == nullptr) || atoi(value) != 0;
}

// Terrain and roads glint together or not at all, or blend tiles would show as patches. The mirror stays flat.
static Bool Terrain_Glint_Wanted()
{
	return TerrainGlintEnabled && TerrainGlintIntensity > 0.0f && TerrainGlintLoaded && RoadGlintLoaded &&
		TerrainGlintNormals != nullptr && TerrainGlintNormals->Peek_D3D_Texture() != nullptr &&
		TerrainGlintMaterials != nullptr && TerrainGlintMaterials->Peek_D3D_Texture() != nullptr &&
		!ShaderClass::Is_Backface_Culling_Inverted();
}

// The vertex lighting's first light is the sun, which the bump and the glint redo per pixel.
static void Set_Terrain_Sun(Int reg)
{
	const Coord3D &lightPos = TheGlobalData->m_terrainLightPos[0];
	Vector3 toSun(-lightPos.x, -lightPos.y, -lightPos.z);
	toSun.Normalize();
	const RGBColor &sunColor = TheGlobalData->m_terrainDiffuse[0];
	Vector4 sunDirection(toSun.X, toSun.Y, toSun.Z, TerrainBumpStrength);
	Vector4 sunDiffuse(sunColor.red, sunColor.green, sunColor.blue, TerrainBumpDebug ? 1.0f : 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(reg, &sunDirection, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(reg + 1, &sunDiffuse, 1);
}

static void Unbind_Glint_Maps()
{
#if defined(BUILD_WITH_D3D9)
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(GLINT_NORMAL_SAMPLER, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(GLINT_MATERIAL_SAMPLER, nullptr);
#endif
}

// Past the fixed-function stages, the maps only a pixel shader reads.
static void Bind_Glint_Map(DWORD sampler, TextureClass *map, D3DTEXTUREFILTERTYPE filter)
{
#if defined(BUILD_WITH_D3D9)
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	device->SetTexture(sampler, map->Peek_D3D_Texture());
	device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(sampler, D3DSAMP_MINFILTER, filter);
	device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, filter);
	device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
#else
	(void)sampler;
	(void)map;
	(void)filter;
#endif
}

// Every ground shader with the world position reads the sun and the glint's constants, so they go up even when
// off, with no strength and a gloss of at least 1, as Glint needs. The terrain's materials scale both.
static void Bind_Terrain_Glint(Int sunRegister, Bool on, Bool materials)
{
#if defined(BUILD_WITH_D3D9)
	Set_Terrain_Sun(sunRegister);

	D3DMATRIX view;
	D3DMATRIX inv;
	float det;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	Invert_D3DMATRIX(inv, &det, view);

	// The albedo's share weighs the colour by its luminance.
	const Real intensity = on ? TerrainGlintIntensity * (materials ? TerrainGlintStrengthScale : 1.0f) : 0.0f;
	const Real gloss = materials ? TerrainGlintGlossScale : TerrainGlintGloss;
	const Real albedo = WWMath::Clamp(TerrainGlintAlbedo, 0.0f, 1.0f);
	Vector4 constants[3];
	constants[0].Set(inv.m[3][0], inv.m[3][1], inv.m[3][2], max(gloss, 1.0f));
	constants[1].Set(intensity * albedo * 0.3f, intensity * albedo * 0.59f, intensity * albedo * 0.11f, intensity * (1.0f - albedo));
	constants[2] = TerrainGlintMapping;
	DX8Wrapper::Set_Pixel_Shader_Constant(GLINT_REGISTER, constants, 3);

	if (!on)
	{
		Unbind_Glint_Maps();
		return;
	}

	Bind_Glint_Map(GLINT_NORMAL_SAMPLER, TerrainGlintNormals, D3DTEXF_LINEAR);
	if (materials)
	{
		Bind_Glint_Map(GLINT_MATERIAL_SAMPLER, TerrainGlintMaterials, D3DTEXF_POINT);
	}
#else
	(void)sunRegister;
	(void)on;
	(void)materials;
#endif
}

static void Release_Ground_Variants(DWORD shaders[2][3])
{
	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			if (shaders[s][i])
			{
				DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), shaders[s][i]);
			}
			shaders[s][i]=0;
		}
	}
}

#if defined(BUILD_WITH_D3D9)

// Loads a ground shader's variants, such as roadglint, by shadow and noise count, and says whether all of them loaded.
static Bool Load_Ground_Variants(const char *variant, Bool shadowMap, Bool packed, DWORD shaders[2][3])
{
	static const char *const noiseNames[3] = { "", "noise", "noise2" };
	Bool complete = TRUE;
	for (Int s=0; s<(shadowMap ? 2 : 1); s++)
	{
		for (Int i=0; i<3; i++)
		{
			char file[64];
			snprintf(file, sizeof(file), "shaders\\%s%s%s.pso", variant, noiseNames[i], s == 0 ? "noshadow" : (packed ? "packed" : ""));
			if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &shaders[s][i])))
			{
				shaders[s][i]=0;
				complete = FALSE;
			}
		}
	}
	return complete;
}

// The instancing and skinning modules build their own declarations, so the one loaded here only has to be valid.
static HRESULT Load_Vertex_Shading_Shader(const char *file, DWORD *handle)
{
	DWORD declaration[] =
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0, D3DVSDT_FLOAT3),
		D3DVSD_END()
	};
	return W3DShaderManager::LoadAndCreateD3DShader(file, declaration, 0, true, handle);
}

///Writes caster depth into the shadow map. Only the D3D9 backend has the shader model for it.
///
///Casters draw with their own textures and shaders, and this overrides the state those
///shaders apply, so a cutout or blended mesh casts its shape rather than a solid block.
class ShadowDepthShader : public W3DShaderInterface
{
public:
	ShadowDepthShader() : m_dwPixelShader(0), m_dwInstanceShader(0), m_dwSkinShader(0) {}

	virtual Int set(Int pass) override;
	virtual Int init() override;
	virtual void reset() override;
	virtual Int shutdown() override;

	void applyOverride(const ShaderClass &shader);

protected:

	DWORD m_dwPixelShader;	///<packed path only; the hardware path writes depth with no shader.
	DWORD m_dwInstanceShader;	///<draws groups of identical casters in one call; the pass runs without it.
	DWORD m_dwSkinShader;	///<deforms skinned casters on the GPU; the pass runs without it.
} shadowDepthShader;

W3DShaderInterface *ShadowDepthShaderList[]=
{
	&shadowDepthShader,
	nullptr
};

static void Apply_Shadow_Depth_Override(const ShaderClass &shader)
{
	shadowDepthShader.applyOverride(shader);
}

// Matches the reference ShaderClass uses for its own alpha test, so a shadow's edge lines
// up with the cutout the player sees.
static const DWORD SHADOW_DEPTH_ALPHA_REFERENCE = 0x60;

Int ShadowDepthShader::init()
{
	// The map already checked the device for shader model 2, so it decides for both.
	if (TheW3DShadowMap == nullptr || !TheW3DShadowMap->isAvailable())
	{
		return FALSE;
	}

	const Bool packed = (TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED);
	if (packed)
	{
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\shadowdepthpacked.pso",
				nullptr, 0, false, &m_dwPixelShader)))
		{
			return FALSE;
		}
	}

	if (FAILED(Load_Vertex_Shading_Shader(packed ? "shaders\\instancedepthpacked.vso" : "shaders\\instancedepth.vso", &m_dwInstanceShader)))
	{
		m_dwInstanceShader = 0;
	}
	if (FAILED(Load_Vertex_Shading_Shader(packed ? "shaders\\skindepthpacked.vso" : "shaders\\skindepth.vso", &m_dwSkinShader)))
	{
		m_dwSkinShader = 0;
	}

	W3DShaders[W3DShaderManager::ST_SHADOW_DEPTH]=&shadowDepthShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHADOW_DEPTH]=1;

	return TRUE;
}

// Set once around the whole depth pass rather than per caster.
Int ShadowDepthShader::set(Int pass)
{
	DX8Wrapper::Set_Apply_Hook(&Apply_Shadow_Depth_Override);
	if (m_dwInstanceShader != 0)
	{
		DX8InstancingClass::Begin_Shadow_Depth_Pass(Peek_D3D9_Vertex_Shader(m_dwInstanceShader));
	}
	if (m_dwSkinShader != 0)
	{
		DX8SkinningClass::Begin_Shadow_Depth_Pass(Peek_D3D9_Vertex_Shader(m_dwSkinShader));
	}
	return TRUE;
}

void ShadowDepthShader::applyOverride(const ShaderClass &shader)
{
	// The sun winds triangles opposite to the camera, so each shader's cull mode is dropped.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);

	const ShaderClass::SrcBlendFuncType src = shader.Get_Src_Blend_Func();

	// Alpha decides the shape only where the mesh's own shader cuts or blends by it. Many
	// opaque textures carry masks in alpha, so testing those would punch holes in shadows.
	Bool cutout = FALSE;
	Bool inverted = (src == ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);

	// Additive and multiplied passes are glows and effects, which cast nothing, even when they fade by alpha.
	const Bool casts = DX8MeshRendererClass::Casts_Shadow(shader);
	if (casts && shader.Uses_Alpha())
	{
		cutout = TRUE;
	}

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, casts);

	// Pushes steep caster surfaces back further than flat ones, which is what keeps them
	// from shadowing themselves without detaching shadows from their bases.
	const float constantBias = TheW3DShadowMap->getCasterDepthBias();
	const float slopeBias = W3DShadowMap::getCasterSlopeBias();
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS, *(const DWORD *)&constantBias);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, *(const DWORD *)&slopeBias);

	if (m_dwPixelShader == 0)
	{
		// Only depth is read back, so colour writes are wasted bandwidth.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, cutout);

		if (cutout)
		{
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF, inverted ? 0xff - SHADOW_DEPTH_ALPHA_REFERENCE : SHADOW_DEPTH_ALPHA_REFERENCE);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAFUNC, inverted ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);
		}
		return;
	}

	// The packed shader writes depth into colour, so its output alpha is not the mesh's
	// and it cuts by the texture itself. Stage 1 hands it the sun view-space position,
	// whose depth maps linearly through the projection's z row because it is orthographic.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);

	D3DMATRIX identity;
	Set_D3DMATRIX_Identity(identity);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, identity);

	const Matrix4x4 &projection = TheW3DShadowMap->getSunProjection();
	Vector4 depthRow(projection[2][2], projection[2][3], 0.0f, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(0, &depthRow, 1);

	// x is the cutoff, negative for none, and y selects inverted alpha.
	Vector4 cutoff(cutout ? (Real)SHADOW_DEPTH_ALPHA_REFERENCE / 255.0f : -1.0f, inverted ? 1.0f : 0.0f, 0.0f, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(1, &cutoff, 1);

	DX8Wrapper::Set_Pixel_Shader(m_dwPixelShader);
}

void ShadowDepthShader::reset()
{
	DX8Wrapper::Set_Apply_Hook(nullptr);
	DX8InstancingClass::End_Pass();
	DX8SkinningClass::End_Pass();
	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000f);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS, 0);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, 0);

	if (m_dwPixelShader != 0)
	{
		DX8Wrapper::Set_Pixel_Shader(0);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 1);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	}

	// The override changed state behind every shader's back, so they all reapply in full.
	ShaderClass::Invalidate();
}

Int ShadowDepthShader::shutdown()
{
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();

	if (device != nullptr)
	{
		DX8_DELETE_PIXEL_SHADER(device, m_dwPixelShader);
		DX8_DELETE_VERTEX_SHADER(device, m_dwInstanceShader);
		DX8_DELETE_VERTEX_SHADER(device, m_dwSkinShader);
	}

	m_dwPixelShader = 0;
	m_dwInstanceShader = 0;
	m_dwSkinShader = 0;

	// Cleared so a failed init after a device reset leaves the pass disabled.
	W3DShaders[W3DShaderManager::ST_SHADOW_DEPTH]=nullptr;
	W3DShadersPassCount[W3DShaderManager::ST_SHADOW_DEPTH]=0;

	return TRUE;
}

// CONTRA_UNITCLOUDS=0 keeps cloud shadows off objects without a rebuild.
static Bool Get_Unit_Clouds_Enabled()
{
	const char *value = getenv("CONTRA_UNITCLOUDS");
	return (value != nullptr) ? atoi(value) != 0 : TRUE;
}

static const Bool UnitCloudsEnabled = Get_Unit_Clouds_Enabled();

static Bool bindCloudReceiver(Int stage);
static void unbindCloudReceiver(Int stage);

///Multiplies the shadow map into geometry that has already drawn, for fixed-function receivers.
class ShadowMultiplyShader : public W3DShaderInterface
{
public:
	ShadowMultiplyShader() : m_dwPixelShader(0), m_dwCloudPixelShader(0), m_cloudBound(FALSE) {}

	virtual Int set(Int pass) override;
	virtual Int init() override;
	virtual void reset() override;
	virtual Int shutdown() override;

protected:

	DWORD m_dwPixelShader;
	DWORD m_dwCloudPixelShader;
	Bool m_cloudBound;
} shadowMultiplyShader;

W3DShaderInterface *ShadowMultiplyShaderList[]=
{
	&shadowMultiplyShader,
	nullptr
};

Int ShadowMultiplyShader::init()
{
	if (TheW3DShadowMap == nullptr || !TheW3DShadowMap->isAvailable())
	{
		return FALSE;
	}

	const Bool packed = (TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED);
	const char *file = packed ? "shaders\\shadowmultiplypacked.pso" : "shaders\\shadowmultiply.pso";

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwPixelShader)))
	{
		return FALSE;
	}

	// Without the cloud variant, receivers fall back to the shadow alone.
	const char *cloudFile = packed ? "shaders\\shadowmultiplycloudpacked.pso" : "shaders\\shadowmultiplycloud.pso";
	if (!UnitCloudsEnabled ||
		FAILED(W3DShaderManager::LoadAndCreateD3DShader(cloudFile, nullptr, 0, false, &m_dwCloudPixelShader)))
	{
		m_dwCloudPixelShader = 0;
	}

	W3DShaders[W3DShaderManager::ST_SHADOW_MULTIPLY]=&shadowMultiplyShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHADOW_MULTIPLY]=1;

	return TRUE;
}

// Expects the receiver's geometry and transforms bound and applied, as for the shroud pass.
Int ShadowMultiplyShader::set(Int pass)
{
	if (TheW3DShadowMap == nullptr || !TheW3DShadowMap->bindReceiver(0))
	{
		return FALSE;
	}

	// The geometry is redrawn at the same depth, so EQUAL limits the pass to pixels the
	// first draw wrote and leaves alpha-tested holes alone.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);

	// Fog would pull the factor toward the fog colour and tint the shadow.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);

	m_cloudBound = pass == W3DShaderManager::SHADOW_MULTIPLY_PASS_CLOUDS && m_dwCloudPixelShader != 0 &&
		bindCloudReceiver(1);

	DX8Wrapper::Set_Pixel_Shader(m_cloudBound ? m_dwCloudPixelShader : m_dwPixelShader);

	return TRUE;
}

void ShadowMultiplyShader::reset()
{
	DX8Wrapper::Set_Pixel_Shader(0);

	if (TheW3DShadowMap != nullptr)
	{
		TheW3DShadowMap->unbindReceiver(0);
	}

	if (m_cloudBound)
	{
		unbindCloudReceiver(1);
		m_cloudBound = FALSE;
	}

	// Z, blend and fog are ShaderClass state, so the next shader set restores them in full.
	ShaderClass::Invalidate();
}

Int ShadowMultiplyShader::shutdown()
{
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();

	if (device != nullptr)
	{
		DX8_DELETE_PIXEL_SHADER(device, m_dwPixelShader);
		DX8_DELETE_PIXEL_SHADER(device, m_dwCloudPixelShader);
	}

	m_dwPixelShader = 0;
	m_dwCloudPixelShader = 0;

	W3DShaders[W3DShaderManager::ST_SHADOW_MULTIPLY]=nullptr;
	W3DShadersPassCount[W3DShaderManager::ST_SHADOW_MULTIPLY]=0;

	return TRUE;
}

///Adds a per-pixel sun highlight and bump shading over geometry that has already drawn.
class SpecularShader : public W3DShaderInterface
{
public:
	enum Bump
	{
		BUMP_NONE,
		BUMP_DERIVED,		///<height taken from the texture's brightness
		BUMP_NORMAL_MAP,	///<the texture's own _nrm normal map
		BUMP_COUNT
	};

	SpecularShader() : m_shadowed(FALSE), m_lit(FALSE), m_lightsOnly(FALSE), m_receiving(FALSE), m_clouds(FALSE), m_bumpHeight(0.0f)
	{
		for (Int i = 0; i < BUMP_COUNT; i++)
		{
			m_dwShadowedShaders[i] = 0;
			m_dwUnshadowedShaders[i] = 0;
			m_dwLitShadowedShaders[i] = 0;
			m_dwLitUnshadowedShaders[i] = 0;
			m_dwReceiveShaders[i] = 0;
		}
	}

	virtual Int set(Int pass) override;
	virtual Int init() override;
	virtual void reset() override;
	virtual Int shutdown() override;

	/// Binds one polygon group's texture, and its normal map and shader when it is bumped.
	void setTexture(TextureClass *texture, MeshModelClass *model = nullptr, const AABoxClass *worldBounds = nullptr, Bool emissionEnabled = TRUE, VertexMaterialClass *material = nullptr);
	/// Sets the rise of full brightness the derived bumps take, for the rest of the pass.
	void setBumpHeight(Real height);

protected:

	DWORD m_dwShadowedShaders[BUMP_COUNT];		///<take the sun out in its shadow
	DWORD m_dwUnshadowedShaders[BUMP_COUNT];	///<for when the shadow map is off or holds no depth
	DWORD m_dwLitShadowedShaders[BUMP_COUNT];	///<the same two, also adding the point lights
	DWORD m_dwLitUnshadowedShaders[BUMP_COUNT];
	DWORD m_dwReceiveShaders[BUMP_COUNT];		///<the unlit shadowed ones also doing the shadow receiver's work
	Bool m_shadowed;							///<which of the two the current pass uses
	Bool m_lit;									///<whether it adds its object's point lights
	Bool m_lightsOnly;							///<whether it adds nothing else
	Bool m_receiving;							///<whether it does the shadow receiver's work
	Bool m_clouds;								///<whether that work includes the cloud map, bound on SPECULAR_CLOUD_STAGE
	Real m_bumpHeight;							///<the derived bumps' rise of full brightness in the current pass

	const DWORD *pickShaders() const;
} specularShader;

W3DShaderInterface *SpecularShaderList[]=
{
	&specularShader,
	nullptr
};

// Stages the pass generates texcoords on. Each holds one set, in stage order, so the shader
// finds the normal, position and shadow coordinates on the matching registers.
#define SPECULAR_NORMAL_STAGE	1
#define SPECULAR_POSITION_STAGE	2
#define SPECULAR_SHADOW_STAGE	3
// The normal and glow maps come last and read the mesh's UVs from TEXCOORD0.
#define SPECULAR_NORMAL_MAP_STAGE	4
#define SPECULAR_EMISSIVE_STAGE		5
// The cloud map takes no texcoords, since the shader finds its UVs from the position.
#define SPECULAR_CLOUD_STAGE		6

static Bool SpecularReceiveLoaded = FALSE;

// CONTRA_SPECULARRECEIVE=0 keeps the shadow receiver as a pass of its own without a rebuild.
static Bool Get_Specular_Receive_Enabled()
{
	const char *value = getenv("CONTRA_SPECULARRECEIVE");
	return (value != nullptr) ? atoi(value) != 0 : TRUE;
}

static Bool bindCloudTexture(Int stage, D3DMATRIX &cameraToCloud);

// The bumped shaders take screen-space derivatives, which only ps_2_a and up have.
static Bool Supports_Pixel_Shader_2_a(const DX8Caps *caps)
{
	const D3DCAPS8 &d3dCaps = caps->Get_DX8_Caps();
	return (d3dCaps.PS20Caps.Caps & D3DPS20CAPS_GRADIENTINSTRUCTIONS) != 0 &&
		d3dCaps.PS20Caps.NumTemps >= 22 && d3dCaps.PS20Caps.NumInstructionSlots >= 512;
}

Int SpecularShader::init()
{
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || caps->Get_Pixel_Shader_Major_Version() < 2)
	{
		return FALSE;
	}

	static const char *const unshadowedFiles[BUMP_COUNT] =
	{
		"shaders\\specularnoshadow.pso", "shaders\\specularderivednoshadow.pso", "shaders\\specularnormalnoshadow.pso"
	};
	static const char *const shadowedFiles[BUMP_COUNT] =
	{
		"shaders\\specular.pso", "shaders\\specularderived.pso", "shaders\\specularnormal.pso"
	};
	static const char *const packedFiles[BUMP_COUNT] =
	{
		"shaders\\specularpacked.pso", "shaders\\specularderivedpacked.pso", "shaders\\specularnormalpacked.pso"
	};

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(unshadowedFiles[BUMP_NONE],
			nullptr, 0, false, &m_dwUnshadowedShaders[BUMP_NONE])))
	{
		return FALSE;
	}

	// Without a shadowed variant the pass still draws, just through shadows too.
	const Bool shadowMap = (TheW3DShadowMap != nullptr && TheW3DShadowMap->isAvailable());
	const char *const *shadowedSet = (shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED)
		? packedFiles : shadowedFiles;
	const Int bumpCount = Supports_Pixel_Shader_2_a(caps) ? BUMP_COUNT : BUMP_NONE + 1;

	for (Int bump = BUMP_NONE; bump < bumpCount; bump++)
	{
		if (bump != BUMP_NONE && FAILED(W3DShaderManager::LoadAndCreateD3DShader(unshadowedFiles[bump],
				nullptr, 0, false, &m_dwUnshadowedShaders[bump])))
		{
			m_dwUnshadowedShaders[bump] = 0;
		}
		if (shadowMap && FAILED(W3DShaderManager::LoadAndCreateD3DShader(shadowedSet[bump],
				nullptr, 0, false, &m_dwShadowedShaders[bump])))
		{
			m_dwShadowedShaders[bump] = 0;
		}
	}

	BumpSupported = (m_dwUnshadowedShaders[BUMP_DERIVED] != 0 || m_dwUnshadowedShaders[BUMP_NORMAL_MAP] != 0);

	// The point lights replace the mesh's fixed-function ones, so every variant has to be there or none is used.
	static const char *const litUnshadowedFiles[BUMP_COUNT] =
	{
		"shaders\\specularlitnoshadow.pso", "shaders\\specularlitderivednoshadow.pso", "shaders\\specularlitnormalnoshadow.pso"
	};
	static const char *const litShadowedFiles[BUMP_COUNT] =
	{
		"shaders\\specularlit.pso", "shaders\\specularlitderived.pso", "shaders\\specularlitnormal.pso"
	};
	static const char *const litPackedFiles[BUMP_COUNT] =
	{
		"shaders\\specularlitpacked.pso", "shaders\\specularlitderivedpacked.pso", "shaders\\specularlitnormalpacked.pso"
	};
	const char *const *litShadowedSet = (shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED)
		? litPackedFiles : litShadowedFiles;

	UnitPixelLightsLoaded = FALSE;
	if (Get_Pixel_Light_Mode() >= PIXEL_LIGHTS_ALL && Supports_Pixel_Shader_2_a(caps))
	{
		UnitPixelLightsLoaded = TRUE;
		for (Int bump = BUMP_NONE; bump < BUMP_COUNT; bump++)
		{
			if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(litUnshadowedFiles[bump],
					nullptr, 0, false, &m_dwLitUnshadowedShaders[bump])))
			{
				m_dwLitUnshadowedShaders[bump] = 0;
				UnitPixelLightsLoaded = FALSE;
			}
			if (m_dwShadowedShaders[BUMP_NONE] != 0 && FAILED(W3DShaderManager::LoadAndCreateD3DShader(litShadowedSet[bump],
					nullptr, 0, false, &m_dwLitShadowedShaders[bump])))
			{
				m_dwLitShadowedShaders[bump] = 0;
				UnitPixelLightsLoaded = FALSE;
			}
		}
	}

	// Every variant the pass may pick must be there, or the receiver keeps its own pass.
	static const char *const receiveFiles[2][BUMP_COUNT] =
	{
		{ "shaders\\specularreceive.pso", "shaders\\specularreceivederived.pso", "shaders\\specularreceivenormal.pso" },
		{ "shaders\\specularreceivepacked.pso", "shaders\\specularreceivederivedpacked.pso", "shaders\\specularreceivenormalpacked.pso" }
	};
	SpecularReceiveLoaded = FALSE;
	if (shadowMap && m_dwShadowedShaders[BUMP_NONE] != 0 && Supports_Pixel_Shader_2_a(caps) && Get_Specular_Receive_Enabled())
	{
		const Int packed = (TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED) ? 1 : 0;
		SpecularReceiveLoaded = TRUE;
		for (Int bump = BUMP_NONE; bump < BUMP_COUNT; bump++)
		{
			if (m_dwShadowedShaders[bump] != 0 && FAILED(W3DShaderManager::LoadAndCreateD3DShader(receiveFiles[packed][bump],
					nullptr, 0, false, &m_dwReceiveShaders[bump])))
			{
				SpecularReceiveLoaded = FALSE;
			}
		}
	}

	W3DShaders[W3DShaderManager::ST_SPECULAR]=&specularShader;
	W3DShadersPassCount[W3DShaderManager::ST_SPECULAR]=1;

	return TRUE;
}

static void Set_Camera_Space_Texcoord(Int stage, DWORD source)
{
	D3DMATRIX identity;
	Set_D3DMATRIX_Identity(identity);
	DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), identity);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, source);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
}

// Expects the mesh bound and transformed. Its texture arrives per polygon group, through
// W3DSpecularMaterialPassClass::Install_Polygon_Materials.
Int SpecularShader::set(Int pass)
{
	// A pending material apply would otherwise land on the draw and put the stages back to passthrough.
	DX8Wrapper::Apply_Render_State_Changes();

	m_shadowed = (m_dwShadowedShaders[BUMP_NONE] != 0 && TheW3DShadowMap != nullptr &&
		TheW3DShadowMap->bindReceiver(SPECULAR_SHADOW_STAGE));

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	Set_Camera_Space_Texcoord(SPECULAR_NORMAL_STAGE, D3DTSS_TCI_CAMERASPACENORMAL);
	Set_Camera_Space_Texcoord(SPECULAR_POSITION_STAGE, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_NORMAL_MAP_STAGE, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_NORMAL_MAP_STAGE, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_EMISSIVE_STAGE, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_EMISSIVE_STAGE, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	// The mesh is redrawn at the same depth, so EQUAL limits the pass to pixels the first
	// draw wrote. It adds its colour to them after scaling them by its alpha.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);

	// The shader lights in camera space, so the sun is turned into it with the view's rotation.
	D3DMATRIX view;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	SpecularLightView = view;
	Vector3 toSun(
		SpecularToSun.X * view.m[0][0] + SpecularToSun.Y * view.m[1][0] + SpecularToSun.Z * view.m[2][0],
		SpecularToSun.X * view.m[0][1] + SpecularToSun.Y * view.m[1][1] + SpecularToSun.Z * view.m[2][1],
		SpecularToSun.X * view.m[0][2] + SpecularToSun.Y * view.m[1][2] + SpecularToSun.Z * view.m[2][2]);
	toSun.Normalize();

	const SpecularPassLights *passLights = CurrentSpecularLights;
	m_lightsOnly = (passLights != nullptr && passLights->lightsOnly);
	const Real highlightScale = m_lightsOnly ? 0.0f : 1.0f;

	Vector4 sunDirection(toSun.X, toSun.Y, toSun.Z, 0.0f);
	Vector4 sunColor(SpecularColor.X * highlightScale, SpecularColor.Y * highlightScale, SpecularColor.Z * highlightScale, 0.0f);
	Vector4 gloss(SpecularPower, (SpecularDebug && !m_lightsOnly) ? 1.0f : 0.0f, 0.0f, 0.0f);
	m_bumpHeight = BumpHeight;
	Vector4 bump(m_bumpHeight, BumpNormalMapStrength, BumpAmbient, 0.0f);
	Vector4 sunDiffuse(SpecularSunDiffuse.X, SpecularSunDiffuse.Y, SpecularSunDiffuse.Z, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(1, &sunDirection, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(2, &sunColor, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(3, &gloss, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(5, &sunDiffuse, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(6, &bump, 1);

	m_lit = (UnitPixelLightsLoaded && passLights != nullptr && (passLights->count > 0 || Get_Lights_Debug()));
	if (m_lit)
	{
		Vector4 constants[PIXEL_LIGHT_REGISTERS];
		const Int registers = Pack_Pixel_Lights(constants, W3DShaderManager::MAX_UNIT_PIXEL_LIGHTS,
			passLights->lights.empty() ? nullptr : &passLights->lights[0], passLights->count, &view);
		DX8Wrapper::Set_Pixel_Shader_Constant(8, constants, registers);
	}

	// The receiver's cloud map is read through the same camera-space mapping its texgen used.
	m_receiving = m_shadowed && !m_lit && SpecularReceiveLoaded && passLights != nullptr && passLights->receivesShadow;
	m_clouds = FALSE;
	D3DMATRIX cameraToCloud;
	if (m_receiving && UnitCloudsEnabled && bindCloudTexture(SPECULAR_CLOUD_STAGE, cameraToCloud))
	{
		m_clouds = TRUE;
		Vector4 cloudU(cameraToCloud.m[0][0], cameraToCloud.m[1][0], cameraToCloud.m[2][0], cameraToCloud.m[3][0]);
		Vector4 cloudV(cameraToCloud.m[0][1], cameraToCloud.m[1][1], cameraToCloud.m[2][1], cameraToCloud.m[3][1]);
		DX8Wrapper::Set_Pixel_Shader_Constant(26, &cloudU, 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(27, &cloudV, 1);
	}

	DX8Wrapper::Set_Pixel_Shader(pickShaders()[BUMP_NONE]);
	++SpecularPassCount;
	return TRUE;
}

const DWORD *SpecularShader::pickShaders() const
{
	if (m_receiving)
	{
		return m_dwReceiveShaders;
	}
	return m_lit
		? (m_shadowed ? m_dwLitShadowedShaders : m_dwLitUnshadowedShaders)
		: (m_shadowed ? m_dwShadowedShaders : m_dwUnshadowedShaders);
}

// Loads <name><suffix> beside the texture, or returns null. The caller releases it.
static TextureClass *Load_Companion_Texture(TextureClass *texture, const char *suffix)
{
	StringClass name(texture->Get_Texture_Name());
	const char *dot = strrchr(name.Peek_Buffer(), '.');
	if (dot != nullptr)
	{
		const Int start = (Int)(dot - name.Peek_Buffer());
		name.Erase(start, name.Get_Length() - start);
	}

	// A missing file would load as the missing-texture placeholder, so it is checked first.
	if (name.Is_Empty())
	{
		return nullptr;
	}
	name += suffix;
	file_auto_ptr file(_TheFileFactory, name.Peek_Buffer());
	if (!file->Is_Available())
	{
		return nullptr;
	}
	return WW3DAssetManager::Get_Instance()->Get_Texture(name.Peek_Buffer());
}

// Looks for <name>_nrm.dds beside the texture, once per texture.
static TextureClass *Find_Normal_Map(TextureClass *texture)
{
	if (!texture->Is_Normal_Map_Checked())
	{
		TextureClass *normalMap = Load_Companion_Texture(texture, "_nrm.dds");
		texture->Set_Normal_Map(normalMap);
		REF_PTR_RELEASE(normalMap);
	}
	return texture->Peek_Normal_Map();
}

// Looks for <name>_emi.dds beside the texture, once per texture.
static TextureClass *Find_Emissive_Map(TextureClass *texture)
{
	if (!texture->Is_Emissive_Map_Checked())
	{
		TextureClass *emissiveMap = Load_Companion_Texture(texture, "_emi.dds");
		texture->Set_Emissive_Map(emissiveMap);
		REF_PTR_RELEASE(emissiveMap);
	}
	return texture->Peek_Emissive_Map();
}

void SpecularShader::setTexture(TextureClass *texture, MeshModelClass *model, const AABoxClass *worldBounds, Bool emissionEnabled, VertexMaterialClass *material)
{
	DX8Wrapper::Set_Texture(0, texture);

	Vector3 diffuse(1.0f, 1.0f, 1.0f);
	Real ambient = BumpAmbient;
	if (material != nullptr)
	{
		Vector3 color;
		if (!material->Get_Lighting())
		{
			diffuse.Set(0.0f, 0.0f, 0.0f);
		}
		else if (material->Get_Diffuse_Color_Source() == VertexMaterialClass::MATERIAL)
		{
			material->Get_Diffuse(&diffuse);
		}
		if (material->Get_Ambient_Color_Source() == VertexMaterialClass::MATERIAL)
		{
			material->Get_Ambient(&color);
			ambient *= color.X * 0.3f + color.Y * 0.59f + color.Z * 0.11f;
		}
		if (material->Get_Emissive_Color_Source() == VertexMaterialClass::MATERIAL)
		{
			material->Get_Emissive(&color);
			ambient += color.X * 0.3f + color.Y * 0.59f + color.Z * 0.11f;
		}
	}
	Vector4 sunDiffuse(SpecularSunDiffuse.X * diffuse.X, SpecularSunDiffuse.Y * diffuse.Y, SpecularSunDiffuse.Z * diffuse.Z, 0.0f);
	Vector4 materialBump(m_bumpHeight, BumpNormalMapStrength, ambient, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(5, &sunDiffuse, 1);
	DX8Wrapper::Set_Pixel_Shader_Constant(6, &materialBump, 1);

	const DWORD *shaders = pickShaders();

	// A texture's first draws go flat while its slope map waits for the next frame's build.
	Int bump = BUMP_NONE;
	TextureClass *bumpMap = nullptr;
	Real slopeScaleU = 0.0f;
	Real slopeScaleV = 0.0f;
	if (BumpEnabled && texture != nullptr && !m_lightsOnly)
	{
		bumpMap = Find_Normal_Map(texture);
		if (bumpMap != nullptr)
		{
			bump = BUMP_NORMAL_MAP;
		}
		else if (m_bumpHeight > 0.0f && shaders[BUMP_DERIVED] != 0)
		{
			bumpMap = W3DSlopeMap::find(texture, slopeScaleU, slopeScaleV);
			bump = (bumpMap != nullptr) ? BUMP_DERIVED : BUMP_NONE;
		}
	}
	if (shaders[bump] == 0)
	{
		bump = BUMP_NONE;
	}

	DX8Wrapper::Set_Texture(SPECULAR_NORMAL_MAP_STAGE, (bump != BUMP_NONE) ? bumpMap : nullptr);
	DX8Wrapper::Set_Pixel_Shader(shaders[bump]);
	const Int *indices = CurrentSpecularLights != nullptr && !CurrentSpecularLights->lights.empty()
		? &CurrentSpecularLights->lights[0] : nullptr;
	Int count = m_lit ? CurrentSpecularLights->count : 0;
	if (count > W3DShaderManager::MAX_UNIT_PIXEL_LIGHTS)
	{
		static std::vector<Int> meshLights;
		if (model != nullptr && !model->Get_Flag(MeshGeometryClass::SKIN) && (worldBounds != nullptr || !DX8Wrapper::Is_Instanced_Drawing()))
		{
			AABoxClass bounds;
			if (worldBounds != nullptr)
			{
				bounds = *worldBounds;
			}
			else
			{
				Matrix4x4 world;
				DX8Wrapper::Get_Transform(D3DTS_WORLD, world);
				Matrix3D transform;
				for (Int row = 0; row < 3; row++)
				{
					transform[row] = world[row];
				}
				model->Get_Bounding_Box(&bounds);
				bounds.Transform(transform);
			}
			meshLights = CurrentSpecularLights->lights;
			W3DShaderManager::filterPixelLights(bounds, meshLights);
			indices = meshLights.empty() ? nullptr : &meshLights[0];
			count = (Int)meshLights.size();
		}
		Vector4 constants[PIXEL_LIGHT_REGISTERS];
		const Int registers = Pack_Pixel_Lights(constants, W3DShaderManager::MAX_UNIT_PIXEL_LIGHTS, indices, count, &SpecularLightView);
		DX8Wrapper::Set_Pixel_Shader_Constant(8, constants, registers);
	}
	Set_Additional_Pixel_Lights(shaders[bump], indices, count, W3DShaderManager::MAX_UNIT_PIXEL_LIGHTS, 8, &SpecularLightView);

	TextureClass *emissiveMap = (emissionEnabled && EmissiveIntensity > 0.0f && texture != nullptr && !m_lightsOnly) ? Find_Emissive_Map(texture) : nullptr;
	DX8Wrapper::Set_Texture(SPECULAR_EMISSIVE_STAGE, emissiveMap);

	Vector4 textureInfo((emissiveMap != nullptr) ? EmissiveIntensity : 0.0f, slopeScaleU, slopeScaleV, m_clouds ? 1.0f : 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(7, &textureInfo, 1);
	if (emissiveMap != nullptr)
	{
		++EmissiveMapCount;
	}

	if (bump == BUMP_DERIVED)
	{
		++BumpDerivedCount;
	}
	else if (bump == BUMP_NORMAL_MAP)
	{
		++BumpNormalMapCount;
	}
}

void SpecularShader::setBumpHeight(Real height)
{
	m_bumpHeight = height;
	Vector4 bump(m_bumpHeight, BumpNormalMapStrength, BumpAmbient, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(6, &bump, 1);
}

void SpecularShader::reset()
{
	Set_Additional_Pixel_Lights(0, nullptr, 0, 0, 0, nullptr);
	DX8Wrapper::Set_Pixel_Shader(0);
	DX8Wrapper::Set_Texture(SPECULAR_NORMAL_MAP_STAGE, nullptr);
	DX8Wrapper::Set_Texture(SPECULAR_EMISSIVE_STAGE, nullptr);
	if (m_clouds)
	{
		DX8Wrapper::Set_Texture(SPECULAR_CLOUD_STAGE, nullptr);
	}

	if (m_shadowed && TheW3DShadowMap != nullptr)
	{
		TheW3DShadowMap->unbindReceiver(SPECULAR_SHADOW_STAGE);
	}
	m_shadowed = FALSE;
	m_lit = FALSE;
	m_lightsOnly = FALSE;
	m_receiving = FALSE;
	m_clouds = FALSE;

	for (Int stage = SPECULAR_NORMAL_STAGE; stage <= SPECULAR_POSITION_STAGE; stage++)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | stage);
	}
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_NORMAL_MAP_STAGE, D3DTSS_TEXCOORDINDEX,
		D3DTSS_TCI_PASSTHRU | SPECULAR_NORMAL_MAP_STAGE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(SPECULAR_EMISSIVE_STAGE, D3DTSS_TEXCOORDINDEX,
		D3DTSS_TCI_PASSTHRU | SPECULAR_EMISSIVE_STAGE);

	// Z, blend and fog are ShaderClass state, so the next shader set restores them in full.
	ShaderClass::Invalidate();
}

Int SpecularShader::shutdown()
{
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();

	for (Int bump = BUMP_NONE; bump < BUMP_COUNT; bump++)
	{
		if (device != nullptr)
		{
			DX8_DELETE_PIXEL_SHADER(device, m_dwShadowedShaders[bump]);
			DX8_DELETE_PIXEL_SHADER(device, m_dwUnshadowedShaders[bump]);
			DX8_DELETE_PIXEL_SHADER(device, m_dwLitShadowedShaders[bump]);
			DX8_DELETE_PIXEL_SHADER(device, m_dwLitUnshadowedShaders[bump]);
			DX8_DELETE_PIXEL_SHADER(device, m_dwReceiveShaders[bump]);
		}
		m_dwShadowedShaders[bump] = 0;
		m_dwUnshadowedShaders[bump] = 0;
		m_dwLitShadowedShaders[bump] = 0;
		m_dwLitUnshadowedShaders[bump] = 0;
		m_dwReceiveShaders[bump] = 0;
	}

	BumpSupported = FALSE;
	UnitPixelLightsLoaded = FALSE;
	SpecularReceiveLoaded = FALSE;

	W3DShaders[W3DShaderManager::ST_SPECULAR]=nullptr;
	W3DShadersPassCount[W3DShaderManager::ST_SPECULAR]=0;

	return TRUE;
}

static void Set_Terrain_World_Position(Int stage);

///Adds dynamic point lights over fixed-function geometry that has already drawn, one light set per draw.
class PointLightPassShader : public W3DShaderInterface
{
public:
	PointLightPassShader() : m_dwPixelShader(0) {}

	virtual Int set(Int pass) override;
	virtual Int init() override;
	virtual void reset() override;
	virtual Int shutdown() override;

protected:

	DWORD m_dwPixelShader;
} pointLightPassShader;

W3DShaderInterface *PointLightPassShaderList[]=
{
	&pointLightPassShader,
	nullptr
};

// The geometry's texture on stage 0 and its world position on stage 1.
#define POINT_LIGHT_POSITION_STAGE 1

Int PointLightPassShader::init()
{
	PointLightPassLoaded = FALSE;
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || Get_Pixel_Light_Mode() < PIXEL_LIGHTS_TERRAIN)
	{
		return FALSE;
	}

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\pointlightpass.pso", nullptr, 0, false, &m_dwPixelShader)))
	{
		m_dwPixelShader = 0;
		return FALSE;
	}

	PointLightPassLoaded = TRUE;
	W3DShaders[W3DShaderManager::ST_POINT_LIGHTS]=&pointLightPassShader;
	W3DShadersPassCount[W3DShaderManager::ST_POINT_LIGHTS]=1;

	return TRUE;
}

// Expects the geometry bound and transformed, and its texture set on stage 0 draw by draw.
Int PointLightPassShader::set(Int pass)
{
	if (PixelLightCount == 0 && !Get_Lights_Debug())
	{
		return FALSE;
	}

	// The geometry is redrawn at the same depth, so EQUAL limits the pass to pixels the first
	// draw wrote. It adds its light where the texture is opaque, and replaces what is there in the debug view.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, Get_Lights_Debug() ? D3DBLEND_ZERO : D3DBLEND_ONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	Set_Terrain_World_Position(POINT_LIGHT_POSITION_STAGE);

	// Two-sided geometry turns its normal towards the eye, which sits where the view's inverse puts the origin.
	D3DMATRIX view;
	D3DMATRIX inverse;
	float det;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	Invert_D3DMATRIX(inverse, &det, view);
	Vector4 eye(inverse.m[3][0], inverse.m[3][1], inverse.m[3][2], 1.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(1, &eye, 1);

	Begin_Draw_Pixel_Lights(0, m_dwPixelShader);
	DX8Wrapper::Set_Pixel_Shader(m_dwPixelShader);
	return TRUE;
}

void PointLightPassShader::reset()
{
	End_Draw_Pixel_Lights();
	DX8Wrapper::Set_Pixel_Shader(0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(POINT_LIGHT_POSITION_STAGE, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(POINT_LIGHT_POSITION_STAGE, D3DTSS_TEXCOORDINDEX,
		D3DTSS_TCI_PASSTHRU | POINT_LIGHT_POSITION_STAGE);

	// Z, blend and fog are ShaderClass state, so the next shader set restores them in full.
	ShaderClass::Invalidate();
}

Int PointLightPassShader::shutdown()
{
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device != nullptr)
	{
		DX8_DELETE_PIXEL_SHADER(device, m_dwPixelShader);
	}
	m_dwPixelShader = 0;
	PointLightPassLoaded = FALSE;

	W3DShaders[W3DShaderManager::ST_POINT_LIGHTS]=nullptr;
	W3DShadersPassCount[W3DShaderManager::ST_POINT_LIGHTS]=0;

	return TRUE;
}

#endif	// BUILD_WITH_D3D9

///Adds the specular pass to one mesh, with that mesh's own texture on stage 0.
class W3DSpecularMaterialPassClass : public MaterialPassClass
{
public:

	W3DSpecularMaterialPassClass(const MaterialPassClass *key = nullptr, Bool receivesShadow = FALSE) : m_key(key != nullptr ? key : this)
	{
		m_lights.count = 0;
		m_lights.lightsOnly = FALSE;
		m_lights.receivesShadow = receivesShadow;
		Enable_On_Sorted_Meshes(false);
	}

	/// Makes this a copy of key for one object, adding that object's lights and the shadow receiver's work.
	void setObjectLights(const MaterialPassClass *key, const Int *lights, Int count, Bool lightsOnly, Bool receivesShadow)
	{
		m_key = key;
		m_lights.count = count;
		m_lights.lights.resize(count);
		for (Int i = 0; i < m_lights.count; i++)
		{
			m_lights.lights[i] = lights[i];
		}
		m_lights.lightsOnly = lightsOnly;
		m_lights.receivesShadow = receivesShadow;
	}

	virtual const MaterialPassClass *Peek_Vertex_Shading_Key() const override
	{
		return m_key;
	}

	virtual void Install_Materials() const override
	{
		CurrentSpecularLights = &m_lights;
		W3DShaderManager::setShader(W3DShaderManager::ST_SPECULAR, 0);
	}

	virtual void UnInstall_Materials() const override
	{
		W3DShaderManager::resetShader(W3DShaderManager::ST_SPECULAR);
		CurrentSpecularLights = nullptr;
	}

	virtual void Install_Polygon_Materials(DX8PolygonRendererClass *renderer, MeshClass *mesh = nullptr) const override
	{
		DX8TextureCategoryClass *category = renderer->Get_Texture_Category();
		TextureClass *texture = (category != nullptr) ? category->Peek_Texture(0) : nullptr;
#if defined(BUILD_WITH_D3D9)
		specularShader.setTexture(texture, renderer->Get_Mesh_Model_Class(), mesh != nullptr ? &mesh->Get_Bounding_Box() : nullptr,
			mesh == nullptr || !mesh->Is_Emission_Disabled(), category != nullptr ? (VertexMaterialClass *)category->Peek_Material() : nullptr);
#else
		DX8Wrapper::Set_Texture(0, texture);
#endif
	}

private:

	const MaterialPassClass *m_key;
	SpecularPassLights m_lights;
};

static W3DSpecularMaterialPassClass SpecularMaterialPass;

// Draws as the shared pass does, so instancing takes it as that pass, while doing the shadow receiver's work too.
static W3DSpecularMaterialPassClass SpecularReceiveMaterialPass(&SpecularMaterialPass, TRUE);

// Passes carrying one object's lights, handed out again from the start each frame. A pass stays
// valid until the render that took it has flushed.
static std::vector<W3DSpecularMaterialPassClass *> ObjectSpecularPasses;
static size_t ObjectSpecularPassesUsed = 0;

void W3DShaderManager::setSpecularLight(const Vector3 &toSun, const Vector3 &color, Real intensity, Real power, Bool debug)
{
	SpecularToSun = toSun;
	SpecularToSun.Normalize();
	SpecularColor = color * intensity;
	SpecularSunDiffuse = color;
	SpecularPower = power;
	SpecularDebug = debug;
}

void W3DShaderManager::setSurfaceBumps(Bool enabled, const Vector3 &ambient, Real height, Real normalMapStrength)
{
	BumpEnabled = enabled;
	BumpAmbient = ambient.X * 0.3f + ambient.Y * 0.59f + ambient.Z * 0.11f;
	BumpHeight = height;
	BumpNormalMapStrength = normalMapStrength;
}

void W3DShaderManager::setEmissive(Real intensity)
{
	EmissiveIntensity = intensity;
	DX8MeshRendererClass::Set_Bloom_Emissive_Intensity(intensity);
}

void W3DShaderManager::setPixelLights(const PixelLight *lights, Int count)
{
	PixelLightCount = count;
	PixelLights.resize(count);
	Real minX = 0.0f;
	Real minY = 0.0f;
	for (Int i = 0; i < PixelLightCount; i++)
	{
		PixelLights[i] = lights[i];
		minX = (i == 0) ? lights[i].position.X : min(minX, lights[i].position.X);
		minY = (i == 0) ? lights[i].position.Y : min(minY, lights[i].position.Y);
	}

	// Interleaving the bits of each light's ground cell gives a key that keeps nearby cells together.
	PixelLightOrder.resize(count);
	for (Int i = 0; i < PixelLightCount; i++)
	{
		const UnsignedInt cellX = (UnsignedInt)min((lights[i].position.X - minX) / PIXEL_LIGHT_ORDER_CELL, 65535.0f);
		const UnsignedInt cellY = (UnsignedInt)min((lights[i].position.Y - minY) / PIXEL_LIGHT_ORDER_CELL, 65535.0f);
		PixelLightOrder[i] = Spread_Bits(cellX) | (Spread_Bits(cellY) << 1);
	}
	Build_Pixel_Light_Grid();
	ObjectSpecularPassesUsed = 0;
	Update_Lights_Debug();
}

Int W3DShaderManager::getPixelLightCount()
{
	return PixelLightCount;
}

const W3DShaderManager::PixelLight &W3DShaderManager::getPixelLight(Int index)
{
	return PixelLights[index];
}

void W3DShaderManager::gatherPixelLights(Real minX, Real minY, Real maxX, Real maxY, std::vector<Int> &lights)
{
	lights.clear();
	if (PixelLightGridWidth == 0)
	{
		for (Int i = 0; i < PixelLightCount; i++)
		{
			lights.push_back(i);
		}
		return;
	}

	// A light in the grid reaches no further than a cell, so the box grows by one cell each way.
	const Real reach = PixelLightGridCell;
	const Real gridMaxX = PixelLightGridMinX + PixelLightGridWidth * PixelLightGridCell;
	const Real gridMaxY = PixelLightGridMinY + PixelLightGridHeight * PixelLightGridCell;
	if (maxX + reach >= PixelLightGridMinX && minX - reach <= gridMaxX && maxY + reach >= PixelLightGridMinY && minY - reach <= gridMaxY)
	{
		const Int x0 = Pixel_Light_Grid_Column(minX - reach);
		const Int x1 = Pixel_Light_Grid_Column(maxX + reach);
		const Int y0 = Pixel_Light_Grid_Row(minY - reach);
		const Int y1 = Pixel_Light_Grid_Row(maxY + reach);
		for (Int y = y0; y <= y1; y++)
		{
			const Int first = PixelLightGridStarts[y * PixelLightGridWidth + x0];
			const Int last = PixelLightGridStarts[y * PixelLightGridWidth + x1 + 1];
			lights.insert(lights.end(), PixelLightGridLights.begin() + first, PixelLightGridLights.begin() + last);
		}
	}
	lights.insert(lights.end(), PixelLightGridWide.begin(), PixelLightGridWide.end());
	std::sort(lights.begin(), lights.end());
}

void W3DShaderManager::setDrawPixelLights(const Int *indices, Int count)
{
	Set_Additional_Pixel_Lights(0, nullptr, 0, 0, 0, nullptr);
	if (DrawLitShader == 0)
	{
		return;
	}

	std::vector<Int> groundLights;
	if (indices == nullptr)
	{
		count = min(count, PixelLightCount);
		for (Int i = 0; i < count; i++)
		{
			if (!PixelLights[i].objectsOnly)
			{
				groundLights.push_back(i);
			}
		}
		count = (Int)groundLights.size();
		indices = groundLights.empty() ? nullptr : &groundLights[0];
	}
	if (count <= 0 && DrawUnlitShader != 0 && !Get_Lights_Debug())
	{
		DX8Wrapper::Set_Pixel_Shader(DrawUnlitShader);
		return;
	}

	Vector4 constants[PIXEL_LIGHT_REGISTERS];
	const Int registers = Pack_Pixel_Lights(constants, MAX_PIXEL_LIGHTS, indices, count, nullptr);
	DX8Wrapper::Set_Pixel_Shader_Constant(GROUND_POINT_LIGHT_REGISTER, constants, registers);
	DX8Wrapper::Set_Pixel_Shader(DrawLitShader);
	Set_Additional_Pixel_Lights(DrawLitShader, indices, count, MAX_PIXEL_LIGHTS, GROUND_POINT_LIGHT_REGISTER, nullptr);
}

void W3DShaderManager::setTerrainSeabed(TextureClass *classMap, TextureClass *waterMask, TextureClass *painted, const Vector4 *constants)
{
	SeabedClassMap = (constants != nullptr) ? classMap : nullptr;
	SeabedWaterMask = (constants != nullptr) ? waterMask : nullptr;
	SeabedPainted = (constants != nullptr) ? painted : nullptr;
	for (Int i = 0; i < SEABED_CONSTANTS && constants != nullptr; i++)
	{
		SeabedConstants[i] = constants[i];
	}
}

Bool W3DShaderManager::supportsTerrainSeabed()
{
	return TerrainSeabedLoaded;
}

void W3DShaderManager::setDrawTerrain(const Int *indices, Int count, Bool seabed)
{
	Set_Additional_Pixel_Lights(0, nullptr, 0, 0, 0, nullptr);
	if (seabed && DrawSeabedUnlitShader != 0)
	{
		// Point lights for other draws overwrite the seabed's registers, so they go back each draw.
		DX8Wrapper::Set_Pixel_Shader_Constant(SEABED_REGISTER, SeabedConstants, SEABED_CONSTANTS);
		if ((count > 0 || Get_Lights_Debug()) && DrawLitShader != 0 && DrawSeabedLitShader != 0)
		{
			Vector4 constants[PIXEL_LIGHT_REGISTERS];
			const Int registers = Pack_Pixel_Lights(constants, SEABED_PIXEL_LIGHTS, indices, count, nullptr);
			DX8Wrapper::Set_Pixel_Shader_Constant(GROUND_POINT_LIGHT_REGISTER, constants, registers);
			DX8Wrapper::Set_Pixel_Shader(DrawSeabedLitShader);
			Set_Additional_Pixel_Lights(DrawSeabedLitShader, indices, count, SEABED_PIXEL_LIGHTS, GROUND_POINT_LIGHT_REGISTER, nullptr);
		}
		else
		{
			DX8Wrapper::Set_Pixel_Shader(DrawSeabedUnlitShader);
		}
		return;
	}

	if (DrawLitShader != 0)
	{
		setDrawPixelLights(indices, count);
	}
	else if (DrawSeabedUnlitShader != 0)
	{
		DX8Wrapper::Set_Pixel_Shader(DrawGroundShader);
	}
}

void W3DShaderManager::filterPixelLights(const AABoxClass &box, std::vector<Int> &lights)
{
	size_t kept = 0;
	for (size_t i = 0; i < lights.size(); i++)
	{
		const PixelLight &light = PixelLights[lights[i]];
		const Real dx = max((Real)fabs(light.position.X - box.Center.X) - box.Extent.X, 0.0f);
		const Real dy = max((Real)fabs(light.position.Y - box.Center.Y) - box.Extent.Y, 0.0f);
		const Real dz = max((Real)fabs(light.position.Z - box.Center.Z) - box.Extent.Z, 0.0f);
		if (dx * dx + dy * dy + dz * dz < light.outerRadius * light.outerRadius)
		{
			lights[kept++] = lights[i];
		}
	}
	lights.resize(kept);
}

Int W3DShaderManager::pickPixelLights(const AABoxClass &box, std::vector<Int> &lights)
{
	static std::vector<Int> nearby;
	gatherPixelLights(box.Center.X - box.Extent.X, box.Center.Y - box.Extent.Y, box.Center.X + box.Extent.X, box.Center.Y + box.Extent.Y, nearby);
	lights.clear();
	for (size_t i = 0; i < nearby.size(); i++)
	{
		const Int index = nearby[i];
		const PixelLight &light = PixelLights[index];
		if (light.objectsOnly)
		{
			continue;
		}
		const Real dx = max((Real)fabs(light.position.X - box.Center.X) - box.Extent.X, 0.0f);
		const Real dy = max((Real)fabs(light.position.Y - box.Center.Y) - box.Extent.Y, 0.0f);
		const Real dz = max((Real)fabs(light.position.Z - box.Center.Z) - box.Extent.Z, 0.0f);
		if (dx * dx + dy * dy + dz * dz < light.outerRadius * light.outerRadius)
		{
			lights.push_back(index);
		}
	}
	return (Int)lights.size();
}

Int W3DShaderManager::pickPixelLights(const AABoxClass &box, Int *lights)
{
	static std::vector<Int> nearby;
	gatherPixelLights(box.Center.X - box.Extent.X, box.Center.Y - box.Extent.Y, box.Center.X + box.Extent.X, box.Center.Y + box.Extent.Y, nearby);
	Int count = 0;
	for (size_t i = 0; i < nearby.size() && count < MAX_PIXEL_LIGHTS; i++)
	{
		const Int index = nearby[i];
		const PixelLight &light = PixelLights[index];
		if (light.objectsOnly)
		{
			continue;
		}
		const Real dx = max((Real)fabs(light.position.X - box.Center.X) - box.Extent.X, 0.0f);
		const Real dy = max((Real)fabs(light.position.Y - box.Center.Y) - box.Extent.Y, 0.0f);
		const Real dz = max((Real)fabs(light.position.Z - box.Center.Z) - box.Extent.Z, 0.0f);
		if (dx * dx + dy * dy + dz * dz < light.outerRadius * light.outerRadius)
		{
			lights[count++] = index;
		}
	}
	return count;
}

Bool W3DShaderManager::supportsPixelLights()
{
	return TerrainPixelLightsLoaded || UnitPixelLightsLoaded || RoadPixelLightsLoaded || FlatPixelLightsLoaded || PointLightPassLoaded;
}

Bool W3DShaderManager::supportsTerrainPixelLights()
{
	return TerrainPixelLightsLoaded;
}

Bool W3DShaderManager::supportsUnitPixelLights()
{
	return UnitPixelLightsLoaded;
}

// Returns the device caps when the player picked anisotropic filtering, else null.
static const D3DCAPS8 *Anisotropic_Caps()
{
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (WW3D::Get_Texture_Filter() != TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC || caps == nullptr)
	{
		return nullptr;
	}
	return &caps->Get_DX8_Caps();
}

// The player's anisotropy wins over the given filters per capability, as TextureFilterClass decides stage 0.
static void Set_Stage_Filter(Int stage, DWORD minFilter, DWORD magFilter, DWORD mipFilter)
{
	const D3DCAPS8 *anisotropicCaps = Anisotropic_Caps();
	if (anisotropicCaps != nullptr)
	{
		const D3DCAPS8 &d3dCaps = *anisotropicCaps;
		if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)
		{
			minFilter = D3DTEXF_ANISOTROPIC;
		}
		else if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR)
		{
			minFilter = D3DTEXF_LINEAR;
		}
		if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC)
		{
			magFilter = D3DTEXF_ANISOTROPIC;
		}
		else if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR)
		{
			magFilter = D3DTEXF_LINEAR;
		}
		mipFilter = (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) ? D3DTEXF_LINEAR : D3DTEXF_POINT;

		const DWORD level = (DWORD)max(1, min(WW3D::Get_Anisotropy_Level(), (Int)d3dCaps.MaxAnisotropy));
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAXANISOTROPY, level);
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, minFilter);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, magFilter);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, mipFilter);
}

void W3DShaderManager::setTerrainTextureFilter(Int stage, Bool bilinearMipLinear)
{
	const Bool linear = TheGlobalData && (TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex);
	const Bool trilinear = TheGlobalData && TheGlobalData->m_trilinearTerrainTex;
	const DWORD filter = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
	Set_Stage_Filter(stage, filter, filter, (trilinear || bilinearMipLinear) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

Bool W3DShaderManager::supportsTerrainHeightBlend()
{
	return TerrainHeightBlendLoaded;
}

void W3DShaderManager::setRoadHeightBlend(Bool blendTiles)
{
	RoadHeightBlendTiles = blendTiles;
}

void W3DShaderManager::setTerrainBumps(Bool enabled, Real strength, Bool debug, Real roadHeight)
{
	TerrainBumpEnabled = enabled;
	TerrainBumpStrength = strength;
	TerrainBumpDebug = debug;
	RoadBumpHeight = roadHeight;
}

Bool W3DShaderManager::wantsTerrainNormalAtlas()
{
	return TerrainBumpEnabled && TerrainBumpSupported;
}

void W3DShaderManager::setTerrainGlint(Bool enabled, Real intensity, Real gloss, Real albedo)
{
	TerrainGlintEnabled = enabled;
	TerrainGlintIntensity = intensity;
	TerrainGlintGloss = gloss;
	TerrainGlintAlbedo = albedo;
}

Bool W3DShaderManager::wantsTerrainGlint()
{
	return TerrainGlintEnabled && TerrainGlintIntensity > 0.0f && TerrainGlintLoaded && RoadGlintLoaded;
}

Real W3DShaderManager::getTerrainGlintGloss()
{
	return TerrainGlintGloss;
}

void W3DShaderManager::setTerrainGlintMaps(TextureClass *normals, const Vector4 &mapping, TextureClass *materials, Real strengthScale, Real glossScale)
{
	TerrainGlintNormals = normals;
	TerrainGlintMapping = mapping;
	TerrainGlintMaterials = materials;
	TerrainGlintStrengthScale = strengthScale;
	TerrainGlintGlossScale = glossScale;
}

Bool W3DShaderManager::supportsPixelShader2a()
{
#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	return caps != nullptr && Supports_Pixel_Shader_2_a(caps);
#else
	return FALSE;
#endif
}

TextureClass *W3DShaderManager::findNormalMap(TextureClass *texture)
{
#if defined(BUILD_WITH_D3D9)
	return (texture != nullptr) ? Find_Normal_Map(texture) : nullptr;
#else
	return nullptr;
#endif
}

TextureClass *W3DShaderManager::findEmissiveMap(TextureClass *texture)
{
#if defined(BUILD_WITH_D3D9)
	return texture != nullptr ? Find_Emissive_Map(texture) : nullptr;
#else
	return nullptr;
#endif
}

Int W3DShaderManager::takeTerrainBumpCount()
{
	const Int count = TerrainBumpCount;
	TerrainBumpCount = 0;
	return count;
}

Int W3DShaderManager::takeRoadBumpCount()
{
	const Int count = RoadBumpCount;
	RoadBumpCount = 0;
	return count;
}

void W3DShaderManager::takeSpecularCounts(Int &meshes, Int &derived, Int &normalMapped, Int &emissive)
{
	meshes = SpecularPassCount;
	derived = BumpDerivedCount;
	normalMapped = BumpNormalMapCount;
	emissive = EmissiveMapCount;
	SpecularPassCount = 0;
	BumpDerivedCount = 0;
	BumpNormalMapCount = 0;
	EmissiveMapCount = 0;
}

MaterialPassClass *W3DShaderManager::getSpecularPass()
{
	const Bool bumps = (BumpEnabled && BumpSupported);
	if (W3DShadersPassCount[ST_SPECULAR] == 0 || (SpecularColor.Length2() <= 0.0f && !bumps && EmissiveIntensity <= 0.0f))
	{
		return nullptr;
	}
	return &SpecularMaterialPass;
}

MaterialPassClass *W3DShaderManager::getSpecularPass(const Int *lights, Int lightCount, Bool lightsOnly, Bool &receivesShadow)
{
	if (W3DShadersPassCount[ST_SPECULAR] == 0 || !UnitPixelLightsLoaded || (lightCount <= 0 && !Get_Lights_Debug()))
	{
		if (lightsOnly || getSpecularPass() == nullptr)
		{
			receivesShadow = FALSE;
			return nullptr;
		}
		return receivesShadow ? &SpecularReceiveMaterialPass : &SpecularMaterialPass;
	}

	// The lit shaders have no constants left for the receiver's work.
	receivesShadow = FALSE;
	if (ObjectSpecularPassesUsed == ObjectSpecularPasses.size())
	{
		ObjectSpecularPasses.push_back(NEW_REF(W3DSpecularMaterialPassClass, ()));
	}
	W3DSpecularMaterialPassClass *pass = ObjectSpecularPasses[ObjectSpecularPassesUsed++];
	pass->setObjectLights(&SpecularMaterialPass, lights, lightCount, lightsOnly, FALSE);
	return pass;
}

Bool W3DShaderManager::canSpecularReceiveShadow()
{
#if defined(BUILD_WITH_D3D9)
	if (!SpecularReceiveLoaded || TheW3DShadowMap == nullptr)
	{
		return FALSE;
	}
	// The pass scales by one value, so a tinted shadow keeps the receiver's own pass.
	const Vector4 color = TheW3DShadowMap->getReceiverColor();
	const Real tolerance = 0.5f / 255.0f;
	return fabs(color.X - color.Y) <= tolerance && fabs(color.Y - color.Z) <= tolerance;
#else
	return FALSE;
#endif
}

const MaterialPassClass *W3DShaderManager::getSpecularPassKey()
{
	return (W3DShadersPassCount[ST_SPECULAR] != 0) ? &SpecularMaterialPass : nullptr;
}

void W3DShaderManager::setSpecularTexture(TextureClass *texture)
{
#if defined(BUILD_WITH_D3D9)
	specularShader.setTexture(texture);
#else
	DX8Wrapper::Set_Texture(0, texture);
#endif
}

void W3DShaderManager::setSpecularBumpHeight(Real height)
{
#if defined(BUILD_WITH_D3D9)
	specularShader.setBumpHeight(height);
#else
	(void)height;
#endif
}

/*===========================================================================================*/
/*=========      Terrain Shaders	=========================================================*/
/*===========================================================================================*/

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class TerrainShader2Stage : public W3DShaderInterface
{
public:
	float m_xSlidePerSecond ;	 ///< How far the clouds move per second.
	float m_ySlidePerSecond ;	 ///< How far the clouds move per second.
	float m_xOffset;
	float m_yOffset;

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.

	void updateCloud();
	void updateNoise1 (D3DMATRIX *destMatrix,D3DMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise1 (i.e clouds)
	void updateNoise2 (D3DMATRIX *destMatrix,D3DMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise2 (i.e lightmap)
} terrainShader2Stage;

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class FlatTerrainShader2Stage : public W3DShaderInterface
{
public:
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
} flatTerrainShader2Stage;

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class FlatTerrainShaderPixelShader : public W3DShaderInterface
{
public:
	DWORD					m_dwBasePixelShader;	///<handle to terrain D3D pixel shader
	DWORD					m_dwBaseNoise1PixelShader;	///<handle to terrain/single noise D3D pixel shader
	DWORD					m_dwBaseNoise2PixelShader;	///<handle to terrain/double noise D3D pixel shader
	DWORD					m_dwBase0PixelShader;	///<handle to terrain only pixel shader
	DWORD					m_dwLitPixelShader[4];	///<the same four adding the point lights, by texture count
	Int						m_lightStage;	///<stage the world position is generated on, or -1
	FlatTerrainShaderPixelShader() : m_lightStage(-1) {}
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int shutdown() override;			///<release resources used by shader
	void initPixelLights();
} flatTerrainShaderPixelShader;

///8 stage terrain shader which only works on certain Nvidia cards.
class TerrainShader8Stage : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init() override;			///<perform any one time initialization and validation
} terrainShader8Stage;

//Offsets into constant register pool used by vertex shader
#define CV_WORLDVIEWPROJ_0	0	//4 vectors for transform of world->clip space.

///Pixel shader based terrain shader - fastest method for the newest cards.
class TerrainShaderPixelShader : public W3DShaderInterface
{
public:
	TerrainShaderPixelShader() : m_shadowStage(-1), m_bumpStage(-1), m_lightStage(-1), m_seabedStage(-1), m_glintStage(-1)
	{
		m_dwPlainPixelShader[0] = m_dwPlainPixelShader[1] = m_dwPlainPixelShader[2] = 0;
#if defined(RTS_ZEROHOUR)
		m_dwEmissivePixelShader[0] = m_dwEmissivePixelShader[1] = 0;
#endif
	}

#if defined(RTS_ZEROHOUR)
	DWORD m_dwEmissivePixelShader[2];
	Bool setEmissive();
#endif

private:
	DWORD					m_dwBasePixelShader;	///<handle to terrain D3D pixel shader
	DWORD					m_dwBaseNoise1PixelShader;	///<handle to terrain/single noise D3D pixel shader
	DWORD					m_dwBaseNoise2PixelShader;	///<handle to terrain/double noise D3D pixel shader
	DWORD					m_dwPlainPixelShader[3];	///<the same three in HLSL, blending by height and reading W3DGroundNoise as the second map, or 0 for the legacy ones
	DWORD					m_dwShadowPixelShader[3];	///<the same three, also receiving the shadow map, indexed by noise texture count
	Int						m_shadowStage;	///<stage the shadow map is bound to, or -1
	DWORD					m_dwBumpPixelShader[2][3];	///<the same three with the normal atlas, unshadowed then shadowed
	Int						m_bumpStage;	///<stage the world position is generated on, with the normal atlas on the next, or -1
	DWORD					m_dwLitPixelShader[2][2][3];	///<the same again adding the point lights, by bump, shadow and noise count
	Int						m_lightStage;	///<stage the world position is generated on for unbumped point lights, or -1
	DWORD					m_dwSeabedPixelShader[2][2][2][3];	///<every variant again hex-tiling the seabed, by lights, bump, shadow and noise count
	Int						m_seabedStage;	///<stage the world position is generated on for the unbumped seabed, or -1
	DWORD					m_dwGlintPixelShader[2][3];	///<the plain and shadowed ones adding the sun's glint, unshadowed then shadowed
	Int						m_glintStage;	///<stage the world position is generated on for the unbumped glint, or -1

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;			///<release resources used by shader

	void initShadowReceiver();
	Bool setShadowReceiver(Int noiseCount);
	void initBump();
	Bool setBump(Int noiseCount, Bool shadowed);
	void initGlint();
	Bool setGlint(Int noiseCount, Bool shadowed, Bool bumped);
	void initPixelLights();
	Bool setPixelLights(Int noiseCount, Bool shadowed, Bool bumped, Bool positioned, DWORD unlit);
	void initSeabed();
	void setSeabed(Int noiseCount, Bool shadowed, Bool bumped, Bool positioned, DWORD unlit);
} terrainShaderPixelShader;

///List of different terrain shader implementations in order of preference
W3DShaderInterface *TerrainShaderList[]=
{
	&terrainShaderPixelShader,
	&terrainShader8Stage,
	&terrainShader2Stage,
	nullptr
};

///List of different terrain shader implementations in order of preference
W3DShaderInterface *FlatTerrainShaderList[]=
{
	&flatTerrainShaderPixelShader,
	&flatTerrainShader2Stage,
	nullptr
};

Int TerrainShader2Stage::init()
{
	//initialize settings for uv animated clouds
	m_xSlidePerSecond = -0.02f;
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_xOffset = 0;
	m_yOffset = 0;

	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=2;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=3;

	return TRUE;
}

void TerrainShader2Stage::reset()
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}

void TerrainShader2Stage::updateCloud()
{
	const float frame_time = WW3D::Get_Logic_Frame_Time_Seconds();
	m_xOffset += m_xSlidePerSecond * frame_time;
	m_yOffset += m_ySlidePerSecond * frame_time;

	// This moves offsets towards zero when smaller -1.0 or larger 1.0
	m_xOffset -= (Int)m_xOffset;
	m_yOffset -= (Int)m_yOffset;
}

void TerrainShader2Stage::updateNoise1(D3DMATRIX *destMatrix,D3DMATRIX *curViewInverse, Bool doUpdate)
{
	// The HQ sky's map covers the ground around the camera, and BaseHeightMapRenderObjClass::cloudMapTexture binds it on the same test.
	if (TheW3DSkyClouds != nullptr && TheW3DSkyClouds->isActive())
	{
		TheW3DSkyClouds->getTextureMatrix(*destMatrix, *curViewInverse);
		return;
	}

	#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */

	D3DMATRIX scale;

	Set_D3DMATRIX_Scaling(scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;

	D3DMATRIX offset;
	Set_D3DMATRIX_Translation(offset, m_xOffset, m_yOffset,0);
	*destMatrix *= offset;
}

void TerrainShader2Stage::updateNoise2(D3DMATRIX *destMatrix,D3DMATRIX *curViewInverse, Bool doUpdate)
{

	D3DMATRIX scale;

	Set_D3DMATRIX_Scaling(scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;
}

Int TerrainShader2Stage::set(Int pass)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	W3DShaderManager::setTerrainTextureFilter(0, FALSE);
	W3DShaderManager::setTerrainTextureFilter(1, TRUE);

	switch (pass)
	{
		case 0:
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
			break;
		case 1:
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 1 );
			// Blend the result using the alpha. (came from diffuse mod texture)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
			// Disable stage 2.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			break;
		case 2:
			// Noise/cloud pass
			D3DMATRIX curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			//blend into frame buffer
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ZERO);

			D3DMATRIX inv;
			float det;
			Invert_D3DMATRIX(inv, &det, curView);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				updateNoise1(&curView,&inv);	//update curView with texture matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				//setup noise pass
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());

				updateNoise2(&curView,&inv);
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			}
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					updateNoise1(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					updateNoise2(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
			}
			break;
	}

	return TRUE;
}

Int TerrainShader8Stage::init()
{
	ChipsetType res;

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (terrainShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_TNT && res <= DC_GEFORCE2)
	{
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=1;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=2;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=2;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=2;
		return TRUE;
	}

	return FALSE;
}

Int TerrainShader8Stage::set(Int pass)
{
	if (pass == 0)
	{
		//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
		DX8Wrapper::Apply_Render_State_Changes();

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

		W3DShaderManager::setTerrainTextureFilter(0, FALSE);
		W3DShaderManager::setTerrainTextureFilter(1, TRUE);

		DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_DIFFUSE | D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(2, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, 2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG2, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(3, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, 3);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG1, D3DTA_DIFFUSE | 0 | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(4, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_TEXCOORDINDEX, 4);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		DX8Wrapper::Set_DX8_Texture(5, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLOROP, D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_TEXCOORDINDEX, 5);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAOP,   D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(6, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_TEXCOORDINDEX, 6);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(7, nullptr);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_TEXCOORDINDEX, 7);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	}
	else
	{	//setup cloud noise/pass
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		DX8Wrapper::Invalidate_Cached_Render_States();

		terrainShader2Stage.set(2);
	}
	return TRUE;
}

void TerrainShader8Stage::reset()
{
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int TerrainShaderPixelShader::shutdown()
{
	if (m_dwBasePixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBasePixelShader);

	if (m_dwBaseNoise1PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBaseNoise1PixelShader);

	if (m_dwBaseNoise2PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBaseNoise2PixelShader);

	m_dwBasePixelShader=0;
	m_dwBaseNoise1PixelShader=0;
	m_dwBaseNoise2PixelShader=0;

	for (Int i=0; i<3; i++)
	{
		if (m_dwPlainPixelShader[i])
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwPlainPixelShader[i]);
		}
		m_dwPlainPixelShader[i]=0;
	}
	TerrainHeightBlendLoaded = FALSE;
#if defined(RTS_ZEROHOUR)
	for (Int i=0; i<2; i++)
	{
		if (m_dwEmissivePixelShader[i] != 0)
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwEmissivePixelShader[i]);
			m_dwEmissivePixelShader[i] = 0;
		}
	}
#endif

	for (Int i=0; i<3; i++)
	{
		if (m_dwShadowPixelShader[i])
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwShadowPixelShader[i]);
		}
		m_dwShadowPixelShader[i]=0;
	}

	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			if (m_dwBumpPixelShader[s][i])
			{
				DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBumpPixelShader[s][i]);
			}
			m_dwBumpPixelShader[s][i]=0;
		}
	}
	TerrainBumpSupported = FALSE;

	for (Int b=0; b<2; b++)
	{
		for (Int s=0; s<2; s++)
		{
			for (Int i=0; i<3; i++)
			{
				if (m_dwLitPixelShader[b][s][i])
				{
					DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwLitPixelShader[b][s][i]);
				}
				m_dwLitPixelShader[b][s][i]=0;
			}
		}
	}
	TerrainPixelLightsLoaded = FALSE;

	for (Int l=0; l<2; l++)
	{
		for (Int b=0; b<2; b++)
		{
			for (Int s=0; s<2; s++)
			{
				for (Int i=0; i<3; i++)
				{
					if (m_dwSeabedPixelShader[l][b][s][i])
					{
						DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwSeabedPixelShader[l][b][s][i]);
					}
					m_dwSeabedPixelShader[l][b][s][i]=0;
				}
			}
		}
	}
	TerrainSeabedLoaded = FALSE;

	Release_Ground_Variants(m_dwGlintPixelShader);
	TerrainGlintLoaded = FALSE;

	return TRUE;
}

// Loads the shadow receiving variants of one legacy shader, indexed by noise texture count, or none of them.
// A missing variant only turns the shadows off, since the legacy shader still draws.
static void Load_Shadow_Receiver_Shaders(const char *const files[3][2], DWORD shaders[3])
{
	for (Int i=0; i<3; i++)
	{
		shaders[i]=0;
	}

#if defined(BUILD_WITH_D3D9)
	if (TheW3DShadowMap == nullptr || !TheW3DShadowMap->isAvailable())
	{
		return;
	}

	const Bool packed = TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	for (Int i=0; i<3; i++)
	{
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(files[i][packed ? 1 : 0], nullptr, 0, false, &shaders[i])))
		{
			for (Int j=0; j<i; j++)
			{
				DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), shaders[j]);
			}
			for (Int j=0; j<3; j++)
			{
				shaders[j]=0;
			}
			return;
		}
	}
#else
	(void)files;
#endif
}

void TerrainShaderPixelShader::initShadowReceiver()
{
	static const char *const files[3][2] =
	{
		{ "shaders\\terrainshadow.pso",       "shaders\\terrainshadowpacked.pso" },
		{ "shaders\\terrainshadownoise.pso",  "shaders\\terrainshadownoisepacked.pso" },
		{ "shaders\\terrainshadownoise2.pso", "shaders\\terrainshadownoise2packed.pso" }
	};
	Load_Shadow_Receiver_Shaders(files, m_dwShadowPixelShader);
	m_shadowStage = -1;
}

Bool TerrainShaderPixelShader::setShadowReceiver(Int noiseCount)
{
	if (m_dwShadowPixelShader[noiseCount] == 0 || TheW3DShadowMap == nullptr)
	{
		return FALSE;
	}

	// The first stage after the base, blend and noise textures. Fixed-function vertex
	// processing hands out texcoord sets in stage order, so this is the set the shader reads.
	const Int stage = 2 + noiseCount;
	if (!TheW3DShadowMap->bindReceiver(stage))
	{
		return FALSE;
	}

	m_shadowStage = stage;
	DX8Wrapper::Set_Pixel_Shader(m_dwShadowPixelShader[noiseCount]);
	return TRUE;
}

void TerrainShaderPixelShader::initBump()
{
	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			m_dwBumpPixelShader[s][i]=0;
		}
	}
	m_bumpStage = -1;
	TerrainBumpSupported = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps))
	{
		return;
	}

	const char *unshadowedFiles[3] =
	{
		"shaders\\terrainbumpnoshadow.pso", "shaders\\terrainbumpnoisenoshadow.pso", "shaders\\terrainbumpnoise2noshadow.pso"
	};
	const char *shadowedFiles[3][2] =
	{
		{ "shaders\\terrainbump.pso",       "shaders\\terrainbumppacked.pso" },
		{ "shaders\\terrainbumpnoise.pso",  "shaders\\terrainbumpnoisepacked.pso" },
		{ "shaders\\terrainbumpnoise2.pso", "shaders\\terrainbumpnoise2packed.pso" }
	};

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;

	for (Int i=0; i<3; i++)
	{
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(unshadowedFiles[i], nullptr, 0, false, &m_dwBumpPixelShader[0][i])))
		{
			m_dwBumpPixelShader[0][i]=0;
		}
		if (shadowMap && FAILED(W3DShaderManager::LoadAndCreateD3DShader(shadowedFiles[i][packed ? 1 : 0], nullptr, 0, false, &m_dwBumpPixelShader[1][i])))
		{
			m_dwBumpPixelShader[1][i]=0;
		}
		if (m_dwBumpPixelShader[0][i] != 0 || m_dwBumpPixelShader[1][i] != 0)
		{
			TerrainBumpSupported = TRUE;
		}
	}
#endif
}

// World position is camera space taken back through the view, because the terrain has no world transform.
static void Set_Terrain_World_Position(Int stage)
{
	D3DMATRIX view;
	D3DMATRIX inv;
	float det;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	Invert_D3DMATRIX(inv, &det, view);
	DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), inv);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
}

// Expects the shadow map bound already when shadowed, since the stages after it are this one's.
Bool TerrainShaderPixelShader::setBump(Int noiseCount, Bool shadowed)
{
	TextureClass *normalAtlas = W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_NORMAL_TEXTURE);
	const DWORD shader = m_dwBumpPixelShader[shadowed ? 1 : 0][noiseCount];
	if (!TerrainBumpEnabled || shader == 0 || normalAtlas == nullptr || normalAtlas->Peek_D3D_Texture() == nullptr)
	{
		return FALSE;
	}

	const Int stage = 2 + noiseCount + (shadowed ? 1 : 0);
	Set_Terrain_World_Position(stage);

	// The atlas is read with the base and blend UVs, so its own texcoords go unused.
	const Int atlasStage = stage + 1;
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(atlasStage, normalAtlas->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(atlasStage, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

	// setGlint sends the sun, which the glint reads too.
	m_bumpStage = stage;
	DX8Wrapper::Set_Pixel_Shader(shader);
	++TerrainBumpCount;
	return TRUE;
}

void TerrainShaderPixelShader::initGlint()
{
	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			m_dwGlintPixelShader[s][i]=0;
		}
	}
	m_glintStage = -1;
	TerrainGlintLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || !Get_Terrain_Glint_Allowed())
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	TerrainGlintLoaded = Load_Ground_Variants("terrainglint", shadowMap, packed, m_dwGlintPixelShader);
#endif
}

// Expects the shadow map and bump already bound, since the stage after them is this one's. The variants
// with the world position all read the sun and the glint's constants, so those go up either way.
Bool TerrainShaderPixelShader::setGlint(Int noiseCount, Bool shadowed, Bool bumped)
{
	const DWORD shader = m_dwGlintPixelShader[shadowed ? 1 : 0][noiseCount];
	const Bool glint = Terrain_Glint_Wanted() && shader != 0;
	Bind_Terrain_Glint(TERRAIN_SUN_REGISTER, glint, TRUE);
	if (!glint)
	{
		return FALSE;
	}

	// Bumped terrain already has the world position, and its shader the glint.
	if (!bumped)
	{
		m_glintStage = 2 + noiseCount + (shadowed ? 1 : 0);
		Set_Terrain_World_Position(m_glintStage);
		DX8Wrapper::Set_Pixel_Shader(shader);
	}
	return TRUE;
}

void TerrainShaderPixelShader::initPixelLights()
{
	for (Int b=0; b<2; b++)
	{
		for (Int s=0; s<2; s++)
		{
			for (Int i=0; i<3; i++)
			{
				m_dwLitPixelShader[b][s][i]=0;
			}
		}
	}
	m_lightStage = -1;
	TerrainPixelLightsLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || Get_Pixel_Light_Mode() < PIXEL_LIGHTS_TERRAIN)
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	static const char *const noiseNames[3] = { "", "noise", "noise2" };

	// Lights handed to the shader leave the vertex lighting, so every variant has to be there or none is used.
	Bool complete = TRUE;
	for (Int b=0; b<2; b++)
	{
		for (Int s=0; s<(shadowMap ? 2 : 1); s++)
		{
			for (Int i=0; i<3; i++)
			{
				char file[64];
				snprintf(file, sizeof(file), "shaders\\terrainlit%s%s%s.pso", b ? "bump" : "", noiseNames[i],
					s == 0 ? "noshadow" : (packed ? "packed" : ""));
				if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwLitPixelShader[b][s][i])))
				{
					m_dwLitPixelShader[b][s][i]=0;
					complete = FALSE;
				}
			}
		}
	}
	TerrainPixelLightsLoaded = complete;
#endif
}

// Expects the shadow map, bump and glint already bound, since the stages after them are this one's. Each
// tile then picks its lights, and so this variant or the unlit one bound now.
Bool TerrainShaderPixelShader::setPixelLights(Int noiseCount, Bool shadowed, Bool bumped, Bool positioned, DWORD unlit)
{
	const DWORD shader = m_dwLitPixelShader[bumped ? 1 : 0][shadowed ? 1 : 0][noiseCount];
	if (!TerrainPixelLightsLoaded || (PixelLightCount == 0 && !Get_Lights_Debug()) || shader == 0 || unlit == 0)
	{
		return FALSE;
	}

	// Bumped or glinting terrain already has the world position.
	if (!positioned)
	{
		m_lightStage = 2 + noiseCount + (shadowed ? 1 : 0);
		Set_Terrain_World_Position(m_lightStage);
	}

	Begin_Draw_Pixel_Lights(unlit, shader);
	return TRUE;
}

void TerrainShaderPixelShader::initSeabed()
{
	for (Int l=0; l<2; l++)
	{
		for (Int b=0; b<2; b++)
		{
			for (Int s=0; s<2; s++)
			{
				for (Int i=0; i<3; i++)
				{
					m_dwSeabedPixelShader[l][b][s][i]=0;
				}
			}
		}
	}
	m_seabedStage = -1;
	TerrainSeabedLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps))
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	static const char *const noiseNames[3] = { "", "noise", "noise2" };

	// Tiles under water pick among all of them, so every variant has to be there or none is used.
	Bool complete = TRUE;
	for (Int l=0; l<2; l++)
	{
		for (Int b=0; b<2; b++)
		{
			for (Int s=0; s<(shadowMap ? 2 : 1); s++)
			{
				for (Int i=0; i<3; i++)
				{
					char file[64];
					snprintf(file, sizeof(file), "shaders\\seabed%s%s%s%s.pso", l ? "lit" : "", b ? "bump" : "", noiseNames[i],
						s == 0 ? "noshadow" : (packed ? "packed" : ""));
					if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwSeabedPixelShader[l][b][s][i])))
					{
						m_dwSeabedPixelShader[l][b][s][i]=0;
						complete = FALSE;
					}
				}
			}
		}
	}
	TerrainSeabedLoaded = complete;
#endif
}

// Expects the shadow map, bump and glint already bound. Draws with standing water then take these variants in setDrawTerrain.
void TerrainShaderPixelShader::setSeabed(Int noiseCount, Bool shadowed, Bool bumped, Bool positioned, DWORD unlit)
{
#if defined(BUILD_WITH_D3D9)
	if (!TerrainSeabedLoaded || SeabedClassMap == nullptr || SeabedWaterMask == nullptr || SeabedPainted == nullptr ||
		SeabedClassMap->Peek_D3D_Texture() == nullptr || SeabedWaterMask->Peek_D3D_Texture() == nullptr ||
		SeabedPainted->Peek_D3D_Texture() == nullptr)
	{
		return;
	}

	// Shadow receivers that arrived after init have no seabed variants, and reset only unbinds the lookups when one was picked.
	const DWORD seabedUnlit = m_dwSeabedPixelShader[0][bumped ? 1 : 0][shadowed ? 1 : 0][noiseCount];
	if (seabedUnlit == 0)
	{
		return;
	}

	// Bumped or glinting terrain already has the world position.
	if (!positioned)
	{
		m_seabedStage = 2 + noiseCount + (shadowed ? 1 : 0);
		Set_Terrain_World_Position(m_seabedStage);
	}

	// Past the fixed-function stages, the lookups only a pixel shader reads.
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	device->SetTexture(SEABED_CLASS_MAP_SAMPLER, SeabedClassMap->Peek_D3D_Texture());
	device->SetSamplerState(SEABED_CLASS_MAP_SAMPLER, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(SEABED_CLASS_MAP_SAMPLER, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(SEABED_CLASS_MAP_SAMPLER, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	device->SetSamplerState(SEABED_CLASS_MAP_SAMPLER, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	device->SetSamplerState(SEABED_CLASS_MAP_SAMPLER, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	// Past the map edge the mask reads empty, as the water's own does.
	device->SetTexture(SEABED_WATER_MASK_SAMPLER, SeabedWaterMask->Peek_D3D_Texture());
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_BORDERCOLOR, 0);
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(SEABED_WATER_MASK_SAMPLER, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	// The paint fades between cells, but each hex cell takes one seed whole, so the seed reads unfiltered.
	for (Int i = 0; i < 2; i++)
	{
		const DWORD sampler = i ? SEABED_PAINTED_SEED_SAMPLER : SEABED_PAINTED_SAMPLER;
		const DWORD filter = i ? D3DTEXF_POINT : D3DTEXF_LINEAR;
		device->SetTexture(sampler, SeabedPainted->Peek_D3D_Texture());
		device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
		device->SetSamplerState(sampler, D3DSAMP_BORDERCOLOR, 0);
		device->SetSamplerState(sampler, D3DSAMP_MINFILTER, filter);
		device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, filter);
		device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	}

	DrawGroundShader = unlit;
	DrawSeabedUnlitShader = seabedUnlit;
	DrawSeabedLitShader = m_dwSeabedPixelShader[1][bumped ? 1 : 0][shadowed ? 1 : 0][noiseCount];
#else
	(void)noiseCount;
	(void)shadowed;
	(void)bumped;
	(void)positioned;
	(void)unlit;
#endif
}

Int TerrainShaderPixelShader::init()
{
	Int res;
#ifdef DISABLE_PIXEL_SHADERS
	return false;
#endif
	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (terrainShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//shader decleration
			DWORD Declaration[]=
			{
				(D3DVSD_STREAM(0)),
				(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
				(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
				(D3DVSD_REG(2, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_REG(3, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_END())
			};

			//base version which doesn't apply any noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrain.pso", &Declaration[0], 0, false, &m_dwBasePixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 1 noise texture.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise.pso", &Declaration[0], 0, false, &m_dwBaseNoise1PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 2 noise textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise2.pso", &Declaration[0], 0, false, &m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			// Every terrain draw takes one of these or a variant with more, so the height blend reaches them all.
			TerrainHeightBlendLoaded = FALSE;
#if defined(BUILD_WITH_D3D9)
			static const char *const plainFiles[3] =
			{
				"shaders\\terrainnoshadow.pso", "shaders\\terrainnoisenoshadow.pso", "shaders\\terrainnoise2noshadow.pso"
			};
			TerrainHeightBlendLoaded = TRUE;
			for (Int i=0; i<3; i++)
			{
				if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(plainFiles[i], nullptr, 0, false, &m_dwPlainPixelShader[i])))
				{
					m_dwPlainPixelShader[i] = 0;
					TerrainHeightBlendLoaded = FALSE;
				}
			}
#endif

			initShadowReceiver();
			initBump();
			initPixelLights();
			initSeabed();
			initGlint();
#if defined(BUILD_WITH_D3D9) && defined(RTS_ZEROHOUR)
			if (Supports_Pixel_Shader_2_a(DX8Wrapper::Get_Current_Caps()))
			{
				W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainemissive.pso", nullptr, 0, false, &m_dwEmissivePixelShader[0]);
				W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainemissiveseabed.pso", nullptr, 0, false, &m_dwEmissivePixelShader[1]);
			}
#endif

			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

#if defined(RTS_ZEROHOUR)
Bool W3DShaderManager::setTerrainEmissive()
{
	return terrainShaderPixelShader.setEmissive();
}

Bool TerrainShaderPixelShader::setEmissive()
{
#if defined(BUILD_WITH_D3D9)
	if (m_dwEmissivePixelShader[0] == 0 || ShaderClass::Is_Backface_Culling_Inverted())
	{
		return FALSE;
	}
	const Real intensity = Bind_Terrain_Emissive(TRUE);
	if (intensity <= 0.0f)
	{
		return FALSE;
	}
	TextureClass *atlas = W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_EMISSIVE_TEXTURE);
	DX8Wrapper::Set_Texture(0, atlas);
	DX8Wrapper::Set_Texture(1, atlas);
	DX8Wrapper::Apply_Render_State_Changes();
	for (Int stage=0; stage<2; stage++)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, stage);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		W3DShaderManager::setTerrainTextureFilter(stage, stage != 0);
	}
	Vector4 height = Bind_Height_Blend(W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_HEIGHT_TEXTURE));
	height.W = intensity;
	DX8Wrapper::Set_Pixel_Shader_Constant(TERRAIN_HEIGHT_BLEND_REGISTER, &height, 1);
	End_Draw_Pixel_Lights();
	if (m_dwEmissivePixelShader[1] != 0)
	{
		setSeabed(0, FALSE, FALSE, FALSE, m_dwEmissivePixelShader[0]);
		if (DrawSeabedUnlitShader != 0)
		{
			DrawSeabedUnlitShader = m_dwEmissivePixelShader[1];
			DrawSeabedLitShader = 0;
		}
	}
	DX8Wrapper::Set_Pixel_Shader(m_dwEmissivePixelShader[0]);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE);
	return TRUE;
#else
	return FALSE;
#endif
}
#endif

Int TerrainShaderPixelShader::set(Int pass)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	//setup base pass
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );

	W3DShaderManager::setTerrainTextureFilter(0, FALSE);
	W3DShaderManager::setTerrainTextureFilter(1, TRUE);

	// The ground noise replaces the light map as the second map behind clouds or white; if it fails, the light map goes.
	const W3DShaderManager::ShaderTypes shader = W3DShaderManager::getCurrentShader();
	const Bool cloudMap = (shader == W3DShaderManager::ST_TERRAIN_BASE_NOISE1 || shader == W3DShaderManager::ST_TERRAIN_BASE_NOISE12);
	Bool lightMap = (shader == W3DShaderManager::ST_TERRAIN_BASE_NOISE2 || shader == W3DShaderManager::ST_TERRAIN_BASE_NOISE12);
	const Bool groundNoise = lightMap && m_dwPlainPixelShader[2] != 0 && W3DGroundNoise::getTexture() != nullptr &&
		(cloudMap || W3DGroundNoise::getWhiteTexture() != nullptr);
	if (m_dwPlainPixelShader[2] != 0 && !groundNoise)
	{
		lightMap = FALSE;
	}
	const Bool twoMaps = groundNoise || (cloudMap && lightMap);

	if (cloudMap || lightMap)
	{
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

		if (twoMaps)
		{	//full shader
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			TextureClass *first = cloudMap ? W3DShaderManager::getShaderTexture(2) : W3DGroundNoise::getWhiteTexture();
			TextureClass *second = groundNoise ? W3DGroundNoise::getTexture() : W3DShaderManager::getShaderTexture(3);
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, first->Peek_D3D_Texture());
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, second->Peek_D3D_Texture());
			DX8Wrapper::Set_Pixel_Shader(groundNoise ? m_dwPlainPixelShader[2] : m_dwBaseNoise2PixelShader);

			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

			terrainShader2Stage.updateNoise1(&curView,&inv);	//update curView with texture matrix
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);

			terrainShader2Stage.updateNoise2(&curView,&inv);	//update curView with texture matrix
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE3, curView);

			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

			// The ground noise takes over the light map's texcoords and filtering.
			if (groundNoise)
			{
				W3DGroundNoise::setupStage(3);
			}
		}
		else
		{	//single noise texture shader
			DX8Wrapper::Set_Pixel_Shader(m_dwBaseNoise1PixelShader);

			if (cloudMap)
			{	//cloud map
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise1(&curView,&inv);	//update curView with texture matrix
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			}
			else
			{	//light map
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise2(&curView,&inv);	//update curView with texture matrix
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			}
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);
		}
	}
	else
	{	//just base texturing
		DX8Wrapper::Set_Pixel_Shader(m_dwBasePixelShader);
	}

	// Swap in the matching variant that also receives the shadow map.
	Int noiseCount = 0;
	if (twoMaps)
	{
		noiseCount = 2;
	}
	else if (cloudMap || lightMap)
	{
		noiseCount = 1;
	}

	// The two-map variants read the second map as the ground noise, so the legacy light map keeps the legacy shader.
	if (twoMaps && !groundNoise)
	{
		return TRUE;
	}

	const Vector4 heightBlend = Bind_Height_Blend(W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_HEIGHT_TEXTURE));
	DX8Wrapper::Set_Pixel_Shader_Constant(TERRAIN_HEIGHT_BLEND_REGISTER, &heightBlend, 1);

	const DWORD legacyShaders[3] = { m_dwBasePixelShader, m_dwBaseNoise1PixelShader, m_dwBaseNoise2PixelShader };
	const DWORD baseShader = m_dwPlainPixelShader[noiseCount] ? m_dwPlainPixelShader[noiseCount] : legacyShaders[noiseCount];
	DX8Wrapper::Set_Pixel_Shader(baseShader);

	const Bool shadowed = setShadowReceiver(noiseCount);
	const Bool bumped = setBump(noiseCount, shadowed);
	const Bool glint = setGlint(noiseCount, shadowed, bumped);

	const DWORD unlit = bumped ? m_dwBumpPixelShader[shadowed ? 1 : 0][noiseCount]
		: glint ? m_dwGlintPixelShader[shadowed ? 1 : 0][noiseCount]
		: (shadowed ? m_dwShadowPixelShader[noiseCount] : baseShader);
	setPixelLights(noiseCount, shadowed, bumped, bumped || glint, unlit);
	setSeabed(noiseCount, shadowed, bumped, bumped || glint, unlit);

	return TRUE;
}

void TerrainShaderPixelShader::reset()
{
	if (TheW3DShadowMap != nullptr && m_shadowStage >= 0)
	{
		TheW3DShadowMap->unbindReceiver(m_shadowStage);
	}
	m_shadowStage = -1;

	if (m_bumpStage >= 0)
	{
		for (Int stage = m_bumpStage; stage <= m_bumpStage + 1; stage++)
		{
			DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|stage);
		}
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(m_bumpStage + 1, nullptr);
	}
	m_bumpStage = -1;

	if (m_lightStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_lightStage);
	}
	m_lightStage = -1;

	if (m_seabedStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_seabedStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_seabedStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_seabedStage);
	}
	m_seabedStage = -1;
	if (DrawSeabedUnlitShader != 0)
	{
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(SEABED_CLASS_MAP_SAMPLER, nullptr);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(SEABED_WATER_MASK_SAMPLER, nullptr);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(SEABED_PAINTED_SAMPLER, nullptr);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(SEABED_PAINTED_SEED_SAMPLER, nullptr);
	}

	if (m_glintStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_glintStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_glintStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_glintStage);
	}
	m_glintStage = -1;
	Unbind_Glint_Maps();

	Bind_Terrain_Emissive(FALSE);
	End_Draw_Pixel_Lights();
	Unbind_Height_Atlas();

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2,nullptr);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3,nullptr);	//release reference to any texture

	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}

///Cloud layer rendering shader - used for objects similar to terrain which only need the cloud layer.
class CloudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int stage) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} cloudTextureShader;

///List of different cloud shader implementations in order of preference
W3DShaderInterface *CloudShaderList[]=
{
	&cloudTextureShader,
	nullptr
};

Int CloudTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_CLOUD_TEXTURE]=&cloudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_CLOUD_TEXTURE]=1;

	return TRUE;
}

/**Setup a certain texture stage to project our cloud texture*/
Int CloudTextureShader::set(Int stage)
{
	D3DMATRIX curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	D3DMATRIX inv;
	float det;

	Invert_D3DMATRIX(inv, &det, curView);

	//Get a texture matrix that applies the current cloud position
	terrainShader2Stage.updateNoise1(&curView,&inv,false);	//update curView with texture matrix

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(stage, W3DShaderManager::getShaderTexture(stage)->Peek_D3D_Texture());

	m_stageOfSet=stage;
	return TRUE;
}

void CloudTextureShader::reset()
{
	//Free reference to texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(m_stageOfSet, nullptr);
	//Turn off texture projection
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_stageOfSet);

	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
}

// Binds the cloud map to stage and gives the mapping from camera space onto it.
static Bool bindCloudTexture(Int stage, D3DMATRIX &cameraToCloud)
{
	TextureClass *cloudTexture = (TheTerrainRenderObject != nullptr) ? TheTerrainRenderObject->getCloudTexture() : nullptr;
	if (cloudTexture == nullptr)
	{
		return FALSE;
	}

	// Applied through the wrapper before the sampler state, as bindWorldReceiver explains.
	DX8Wrapper::Set_Texture(stage, nullptr);
	DX8Wrapper::Set_Texture(stage, cloudTexture);
	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	// The wrapper's view copy is zeroed by every invalidate, so the device's is read.
	D3DMATRIX view;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);

	D3DMATRIX inverseView;
	float det;
	Invert_D3DMATRIX(inverseView, &det, view);

	terrainShader2Stage.updateNoise1(&cameraToCloud, &inverseView, false);
	return TRUE;
}

Bool W3DShaderManager::bindCloudMap(Int stage, D3DMATRIX &cameraToCloud)
{
#if defined(BUILD_WITH_D3D9)
	return UnitCloudsEnabled && bindCloudTexture(stage, cameraToCloud);
#else
	return FALSE;
#endif
}

// Projects the terrain's cloud map onto a fixed-function receiver, the way the terrain samples it.
static Bool bindCloudReceiver(Int stage)
{
	D3DMATRIX textureTransform;
	if (!bindCloudTexture(stage, textureTransform))
	{
		return FALSE;
	}

	DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), textureTransform);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	return TRUE;
}

static void unbindCloudReceiver(Int stage)
{
	DX8Wrapper::Set_Texture(stage, nullptr);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | stage);
}

/*===========================================================================================*/
/*=========      Road Shaders	=========================================================*/
/*===========================================================================================*/
class RoadShaderPixelShader : public W3DShaderInterface
{
	friend class RoadShader2Stage;	//the two-stage path hands its passes over when roads receive shadows.

public:
	RoadShaderPixelShader() : m_shadowStage(-1), m_lightStage(-1), m_normalStage(-1), m_pixelPath(FALSE) {}

private:

	DWORD					m_dwBaseNoise2PixelShader;	///<handle to road/double noise D3D pixel shader
	DWORD					m_dwShadowPixelShader[3];	///<every road mode, also receiving the shadow map, indexed by noise texture count
	DWORD					m_dwPlainPixelShader[3];	///<every road mode without the shadow map, for lit draws' unlit neighbours and the ground noise
	DWORD					m_dwLitPixelShader[2][3];	///<both again adding the point lights, by shadow and noise count
	DWORD					m_dwGlintPixelShader[2][3];	///<both again adding the sun's glint instead, by shadow and noise count
	DWORD					m_dwBumpPixelShader[2][3];	///<the glint variants also reading a normal map
	DWORD					m_dwLitBumpPixelShader[2][3];	///<the lit variants also reading a normal map
	DWORD					m_dwDerivedPixelShader[2][3];	///<the glint variants also bumped from the road texture's brightness
	DWORD					m_dwLitDerivedPixelShader[2][3];	///<the lit variants also bumped from the road texture's brightness
	Int						m_shadowStage;	///<stage the shadow map is bound to, or -1
	Int						m_lightStage;	///<stage the world position is generated on, or -1
	Int						m_normalStage;	///<stage the normal map is bound to, or -1
	Bool					m_pixelPath;	///<whether the current pass draws through the shaders above

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;			///<release resources used by shader

	void initShadowReceiver();
	void initPixelLights();
	void initGlint();
	void initBump();
	Bool setPixelPath();
} roadShaderPixelShader;

class RoadShader2Stage : public W3DShaderInterface
{	friend class RoadShaderPixelShader;	//pixel shader version uses some of the same features.

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;
} roadShader2Stage;

///List of different terrain shader implementations in order of preference
W3DShaderInterface *RoadShaderList[]=
{
	&roadShaderPixelShader,
	&roadShader2Stage,
	nullptr
};

Int RoadShaderPixelShader::shutdown()
{
	if (m_dwBaseNoise2PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBaseNoise2PixelShader);

	m_dwBaseNoise2PixelShader=0;

	for (Int i=0; i<3; i++)
	{
		if (m_dwShadowPixelShader[i])
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwShadowPixelShader[i]);
		}
		m_dwShadowPixelShader[i]=0;
		if (m_dwPlainPixelShader[i])
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwPlainPixelShader[i]);
		}
		m_dwPlainPixelShader[i]=0;
		for (Int s=0; s<2; s++)
		{
			if (m_dwLitPixelShader[s][i])
			{
				DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwLitPixelShader[s][i]);
			}
			m_dwLitPixelShader[s][i]=0;
		}
	}
	RoadPixelLightsLoaded = FALSE;

	Release_Ground_Variants(m_dwGlintPixelShader);
	RoadGlintLoaded = FALSE;

	Release_Ground_Variants(m_dwBumpPixelShader);
	Release_Ground_Variants(m_dwLitBumpPixelShader);
	Release_Ground_Variants(m_dwDerivedPixelShader);
	Release_Ground_Variants(m_dwLitDerivedPixelShader);
	RoadBumpLoaded = FALSE;
	RoadDerivedLoaded = FALSE;

	return TRUE;
}

void RoadShaderPixelShader::initShadowReceiver()
{
	static const char *const files[3][2] =
	{
		{ "shaders\\roadshadow.pso",       "shaders\\roadshadowpacked.pso" },
		{ "shaders\\roadshadownoise.pso",  "shaders\\roadshadownoisepacked.pso" },
		{ "shaders\\roadshadownoise2.pso", "shaders\\roadshadownoise2packed.pso" }
	};
	Load_Shadow_Receiver_Shaders(files, m_dwShadowPixelShader);
	m_shadowStage = -1;
}

void RoadShaderPixelShader::initPixelLights()
{
	for (Int i=0; i<3; i++)
	{
		m_dwPlainPixelShader[i]=0;
		m_dwLitPixelShader[0][i]=0;
		m_dwLitPixelShader[1][i]=0;
	}
	m_lightStage = -1;
	RoadPixelLightsLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	static const char *const noiseNames[3] = { "", "noise", "noise2" };

	// The ground noise draws through these too, so they load on any device that runs them.
	Bool complete = TRUE;
	for (Int i=0; i<3; i++)
	{
		char file[64];
		snprintf(file, sizeof(file), "shaders\\road%snoshadow.pso", noiseNames[i]);
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwPlainPixelShader[i])))
		{
			m_dwPlainPixelShader[i]=0;
			complete = FALSE;
		}
	}

	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || Get_Pixel_Light_Mode() < PIXEL_LIGHTS_TERRAIN)
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;

	// Roads have no vertex lighting to hand lights over from, so a missing variant only leaves its draws unlit.
	for (Int i=0; i<3; i++)
	{
		char file[64];
		for (Int s=0; s<(shadowMap ? 2 : 1); s++)
		{
			snprintf(file, sizeof(file), "shaders\\roadlit%s%s.pso", noiseNames[i], s == 0 ? "noshadow" : (packed ? "packed" : ""));
			if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwLitPixelShader[s][i])))
			{
				m_dwLitPixelShader[s][i]=0;
				complete = FALSE;
			}
		}
	}
	RoadPixelLightsLoaded = complete;
#endif
}

void RoadShaderPixelShader::initGlint()
{
	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			m_dwGlintPixelShader[s][i]=0;
		}
	}
	RoadGlintLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || !Get_Terrain_Glint_Allowed())
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	RoadGlintLoaded = Load_Ground_Variants("roadglint", shadowMap, packed, m_dwGlintPixelShader);
#endif
}

void RoadShaderPixelShader::initBump()
{
	for (Int s=0; s<2; s++)
	{
		for (Int i=0; i<3; i++)
		{
			m_dwBumpPixelShader[s][i]=0;
			m_dwLitBumpPixelShader[s][i]=0;
			m_dwDerivedPixelShader[s][i]=0;
			m_dwLitDerivedPixelShader[s][i]=0;
		}
	}
	m_normalStage = -1;
	RoadBumpLoaded = FALSE;
	RoadDerivedLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps))
	{
		return;
	}

	// Shadowed variants only go with the shadow receivers they replace, and lit ones with the lit roads they replace.
	const Bool shadowMap = (m_dwShadowPixelShader[0] != 0 && TheW3DShadowMap != nullptr);
	const Bool packed = shadowMap && TheW3DShadowMap->getDepthMode() == W3DShadowMap::DEPTH_MODE_PACKED;
	RoadBumpLoaded = Load_Ground_Variants("roadbump", shadowMap, packed, m_dwBumpPixelShader) &&
		(!RoadPixelLightsLoaded || Load_Ground_Variants("roadlitbump", shadowMap, packed, m_dwLitBumpPixelShader));
	RoadDerivedLoaded = Load_Ground_Variants("roadderived", shadowMap, packed, m_dwDerivedPixelShader) &&
		(!RoadPixelLightsLoaded || Load_Ground_Variants("roadlitderived", shadowMap, packed, m_dwLitDerivedPixelShader));
#endif
}

// Sets up the shaders that receive the shadow map, add the point lights or the glint, or all three. Each
// road draw then picks its lights, and so the lit variant or the unlit one bound here.
Bool RoadShaderPixelShader::setPixelPath()
{
	const W3DShaderManager::ShaderTypes shader = W3DShaderManager::getCurrentShader();
	const Bool cloudMap = (shader == W3DShaderManager::ST_ROAD_BASE_NOISE1 || shader == W3DShaderManager::ST_ROAD_BASE_NOISE12);
	const Bool anyLightMap = (shader == W3DShaderManager::ST_ROAD_BASE_NOISE2 || shader == W3DShaderManager::ST_ROAD_BASE_NOISE12);

	// As on the terrain, the ground noise replaces the light map, and the light map goes when the noise fails.
	const Bool groundCapable = (m_dwPlainPixelShader[0] != 0 && m_dwPlainPixelShader[1] != 0 && m_dwPlainPixelShader[2] != 0);
	const Bool groundNoise = anyLightMap && groundCapable && W3DGroundNoise::getTexture() != nullptr &&
		(cloudMap || W3DGroundNoise::getWhiteTexture() != nullptr);
	const Bool lightMap = anyLightMap && !groundCapable;
	const Int noiseCount = groundNoise ? 2 : (cloudMap ? 1 : 0) + (lightMap ? 1 : 0);

	if (noiseCount == 2 && !groundNoise)
	{
		return FALSE;
	}

	TextureClass *heights = W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_HEIGHT_TEXTURE);
	const Bool heightBlend = RoadHeightBlendTiles && groundCapable && heights != nullptr && heights->Peek_D3D_Texture() != nullptr;
	const Real emissive = Bind_Terrain_Emissive(RoadHeightBlendTiles);

	Bool shadowed = (m_dwShadowPixelShader[noiseCount] != 0 && TheW3DShadowMap != nullptr && TheW3DShadowMap->hasDepth());
	const Bool lightable = (RoadPixelLightsLoaded && (PixelLightCount > 0 || Get_Lights_Debug()));
	const Bool lightMapReplaced = (anyLightMap && groundCapable);
	const Bool glintable = Terrain_Glint_Wanted();

	// Blend tiles read the terrain normal atlas, roads their _nrm.dds or else their brightness, and the mirror stays flat.
	TextureClass *roadTexture = W3DShaderManager::getShaderTexture(0);
	const Bool bumpAllowed = TerrainBumpEnabled && !ShaderClass::Is_Backface_Culling_Inverted();
	TextureClass *normalMap = nullptr;
	if (bumpAllowed && RoadBumpLoaded)
	{
		normalMap = RoadHeightBlendTiles ? W3DShaderManager::getShaderTexture(W3DShaderManager::TERRAIN_NORMAL_TEXTURE)
			: W3DShaderManager::findNormalMap(roadTexture);
	}
	const Bool bumpable = (normalMap != nullptr && normalMap->Peek_D3D_Texture() != nullptr);
	TextureClass *slopeMap = nullptr;
	Real slopeScaleU = 0.0f;
	Real slopeScaleV = 0.0f;
	if (!bumpable && bumpAllowed && RoadDerivedLoaded && !RoadHeightBlendTiles && RoadBumpHeight > 0.0f)
	{
		slopeMap = W3DSlopeMap::find(roadTexture, slopeScaleU, slopeScaleV);
	}
	const Bool derivable = (slopeMap != nullptr && slopeMap->Peek_D3D_Texture() != nullptr);

	if (!shadowed && !lightable && !lightMapReplaced && !heightBlend && !glintable && !bumpable && !derivable && !emissive)
	{
		Bind_Terrain_Emissive(FALSE);
		return FALSE;
	}

	DX8Wrapper::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	// The first stage after the road and noise textures. Fixed-function vertex processing
	// hands out texcoord sets in stage order, so this is the set the shader reads.
	if (shadowed)
	{
		const Int stage = 1 + noiseCount;
		shadowed = TheW3DShadowMap->bindReceiver(stage);
		m_shadowStage = shadowed ? stage : -1;
	}
	const Bool glint = glintable && m_dwGlintPixelShader[shadowed ? 1 : 0][noiseCount] != 0;
	const Bool bump = bumpable && m_dwBumpPixelShader[shadowed ? 1 : 0][noiseCount] != 0 &&
		(!lightable || m_dwLitBumpPixelShader[shadowed ? 1 : 0][noiseCount] != 0);
	const Bool derived = derivable && m_dwDerivedPixelShader[shadowed ? 1 : 0][noiseCount] != 0 &&
		(!lightable || m_dwLitDerivedPixelShader[shadowed ? 1 : 0][noiseCount] != 0);
	if (!shadowed && !lightable && !lightMapReplaced && !heightBlend && !glint && !bump && !derived && !emissive)
	{
		return FALSE;
	}

	// The fourth component is 1 plus glow brightness for terrain blend tiles, or 0 for roads.
	Vector4 heightConstants = Bind_Height_Blend(heightBlend ? heights : nullptr);
	heightConstants.W = RoadHeightBlendTiles ? 1.0f + emissive : 0.0f;
	if (derived)
	{
		// The derived bump leaves alpha alone without the register, so it holds the rise and the slope map's scales.
		heightConstants.Set(RoadBumpHeight, slopeScaleU, slopeScaleV, 0.0f);
	}
	DX8Wrapper::Set_Pixel_Shader_Constant(ROAD_HEIGHT_BLEND_REGISTER, &heightConstants, 1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);	//blend roads into terrain
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

	const DWORD mipFilter = (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) ? D3DTEXF_LINEAR : D3DTEXF_POINT;

	// Blend tiles draw the terrain atlas, so they filter as the terrain does.
	if (RoadHeightBlendTiles)
	{
		W3DShaderManager::setTerrainTextureFilter(0, FALSE);
	}
	else if (Anisotropic_Caps() != nullptr)
	{
		// Anisotropic filtering needs linear mips, or the road bands where its mips change.
		Set_Stage_Filter(0, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR);
	}
	else
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, mipFilter);
	}

	D3DMATRIX curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	D3DMATRIX inv;
	float det;
	Invert_D3DMATRIX(inv, &det, curView);

	// Cloud map first, then light map, matching the order roadnoise2 applies them.
	Int noiseStage = 1;
	for (Int map=0; map<2; map++)
	{
		const Bool isCloud = (map == 0);
		if (isCloud ? !(cloudMap || groundNoise) : !(lightMap || groundNoise))
		{
			continue;
		}

		D3DMATRIX textureTransform = curView;
		if (isCloud)
		{
			terrainShader2Stage.updateNoise1(&textureTransform, &inv, false);
		}
		else
		{
			terrainShader2Stage.updateNoise2(&textureTransform, &inv, false);
		}

		// White stands in for missing clouds, and the ground noise for the light map.
		TextureClass *texture = W3DShaderManager::getShaderTexture(isCloud ? 1 : 2);
		if (groundNoise)
		{
			texture = isCloud ? (cloudMap ? texture : W3DGroundNoise::getWhiteTexture()) : W3DGroundNoise::getTexture();
		}
		DX8Wrapper::Set_Texture(noiseStage, texture);
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + noiseStage), textureTransform);

		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_MIPFILTER, mipFilter);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_MINFILTER, isCloud ? D3DTEXF_LINEAR : D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(noiseStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		if (groundNoise && !isCloud)
		{
			W3DGroundNoise::setupStage(noiseStage);
		}
		noiseStage++;
	}

	// A complete set of lit variants includes the shadowed ones whenever a receiver loaded.
	Bind_Terrain_Glint(ROAD_SUN_REGISTER, glint, FALSE);
	DWORD unlit = glint ? m_dwGlintPixelShader[shadowed ? 1 : 0][noiseCount]
		: (shadowed ? m_dwShadowPixelShader[noiseCount] : m_dwPlainPixelShader[noiseCount]);
	DWORD lit = m_dwLitPixelShader[shadowed ? 1 : 0][noiseCount];
	if (bump)
	{
		unlit = m_dwBumpPixelShader[shadowed ? 1 : 0][noiseCount];
		lit = m_dwLitBumpPixelShader[shadowed ? 1 : 0][noiseCount];
	}
	else if (derived)
	{
		unlit = m_dwDerivedPixelShader[shadowed ? 1 : 0][noiseCount];
		lit = m_dwLitDerivedPixelShader[shadowed ? 1 : 0][noiseCount];
	}
	if (lightable || glint || bump || derived)
	{
		m_lightStage = 1 + noiseCount + (shadowed ? 1 : 0);
		Set_Terrain_World_Position(m_lightStage);
	}
	if (bump || derived)
	{
		// The normal or slope map is read with the road UVs, so its own texcoords go unused. Only roads tile theirs.
		m_normalStage = m_lightStage + 1;
		const DWORD address = RoadHeightBlendTiles ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP;
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(m_normalStage, bump ? normalMap->Peek_D3D_Texture() : slopeMap->Peek_D3D_Texture());
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_normalStage, D3DTSS_TEXCOORDINDEX, 0);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_normalStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_normalStage, D3DTSS_ADDRESSU, address);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_normalStage, D3DTSS_ADDRESSV, address);
		Set_Stage_Filter(m_normalStage, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR);
		++RoadBumpCount;
	}
	if (lightable)
	{
		Begin_Draw_Pixel_Lights(unlit, lit);
	}

	m_pixelPath = TRUE;
	DX8Wrapper::Set_Pixel_Shader(unlit);
	return TRUE;
}

Int RoadShaderPixelShader::init()
{
	Int res;

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (roadShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//shader declaration
			DWORD Declaration[]=
			{
				(D3DVSD_STREAM(0)),
				(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
				(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
				(D3DVSD_REG(2, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_END())
			};

			//version which blends 2 noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\roadnoise2.pso", &Declaration[0], 0, false, &m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			initShadowReceiver();
			initPixelLights();
			initGlint();
			initBump();

			//Only set this shader for use in dual noise mode.  The 2Stage shader will take care of
			//all the other modes.
			W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

Int RoadShaderPixelShader::set(Int pass)
{
	if (setPixelPath())
		return TRUE;

	DX8Wrapper::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);	//blend roads into terrain
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

	D3DMATRIX curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	D3DMATRIX inv;
	float det;
	Invert_D3DMATRIX(inv, &det, curView);

	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
	{	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}
	else
	{	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}
	if (Anisotropic_Caps() != nullptr)
	{
		Set_Stage_Filter(0, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR);
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	// Two output coordinates are used.
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
	DX8Wrapper::Set_Texture(2,W3DShaderManager::getShaderTexture(2));

	DX8Wrapper::Set_Pixel_Shader(m_dwBaseNoise2PixelShader);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	terrainShader2Stage.updateNoise1(&curView,&inv, false);	//get texture projection matrix
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);

	terrainShader2Stage.updateNoise2(&curView,&inv, false);	//get texture projection matrix
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	// Two output coordinates are used.
	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	return TRUE;
}

void RoadShaderPixelShader::reset()
{
	if (TheW3DShadowMap != nullptr && m_shadowStage >= 0)
	{
		TheW3DShadowMap->unbindReceiver(m_shadowStage);
	}
	m_shadowStage = -1;

	// Only stages 0 to 3 go back below, and the world position can sit on stage 4 and the normal map on 5.
	if (m_lightStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_lightStage);
	}
	m_lightStage = -1;
	if (m_normalStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_normalStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_normalStage);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(m_normalStage, nullptr);
	}
	m_normalStage = -1;
	if (m_pixelPath)
	{
		Bind_Terrain_Emissive(FALSE);
		Unbind_Height_Atlas();
		Unbind_Glint_Maps();
	}
	m_pixelPath = FALSE;
	End_Draw_Pixel_Lights();

	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int RoadShader2Stage::init()
{
	//no special device validation needed - anything in our min spec should handle this.
	W3DShaders[W3DShaderManager::ST_ROAD_BASE]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE1]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE1]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE2]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE2]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=2;

	return TRUE;
}

Int RoadShader2Stage::set(Int pass)
{
	// Receiving the shadow map or the point lights needs a pixel shader, which covers every mode in one pass.
	if (pass == 0 && roadShaderPixelShader.setPixelPath())
		return TRUE;

	//First stage always contains base texture.
	DX8Wrapper::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//Force system to apply world/view transforms.
	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

	// Modulate the diffuse color with the texture as lighting comes from diffuse.
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);	//blend roads into terrain

	if (pass == 0)
	{
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

		if (W3DShaderManager::getCurrentShader() >= W3DShaderManager::ST_ROAD_BASE_NOISE1)
		{	//second texture unit will contain a noise pass
			D3DMATRIX curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			D3DMATRIX inv;
			float det;
			Invert_D3DMATRIX(inv, &det, curView);

			if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
			else
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);

			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE12)
			{	//full shader, apply noise 1 in pass 0.
				DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				terrainShader2Stage.updateNoise1(&curView, &inv, false);	//get texture projection matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
			}
			else
			{	//single noise texture shader
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE1)
				{	//cloud map
					DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
					terrainShader2Stage.updateNoise1(&curView, &inv, false);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{	//light map
					DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(2));
					terrainShader2Stage.updateNoise2(&curView,&inv, false);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
			}
		}
		else
		{	//just base texturing
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
		}
	}
	else
	{	//pass 1, apply additional noise pass
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		else
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);

		DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(2));

		terrainShader2Stage.updateNoise2(&curView, &inv, false);	//update curView with texture matrix
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

		//Copy alpha channel into stage 1 but mask out color channel by replacing with white.
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		//Force color channel to white by copying the alpha into RGB
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE|D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1 );

		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_BLENDCURRENTALPHA);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

		//Modulate into existing roads with clouds applied. - only apply where roads are transparent by
		//using road texture as a mask.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_ZERO);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);

		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
	}

	return TRUE;
}

void RoadShader2Stage::reset()
{
	if (roadShaderPixelShader.m_pixelPath)
	{
		roadShaderPixelShader.reset();
		return;
	}

	ShaderClass::Invalidate();

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}

/** List of all custom shader lists - each list in this list contains variations of the same
	shader to allow it to work on different hardware configurations.
*/
#if defined(BUILD_WITH_D3D9)
static DWORD InstancedMainShader = 0;	///<main scene meshes and their passes drawn in groups; the scene draws without it.
static DWORD SkinnedMainShader = 0;	///<main scene skins and their passes deformed on the GPU; the scene draws without it.
#if defined(RTS_ZEROHOUR)
static DWORD ParticleBillboardShader = 0;
#endif
#endif

W3DShaderInterface **MasterShaderList[]=
{
	TerrainShaderList,
	ShroudShaderList,
	FlatShroudShaderList,
	RoadShaderList,
	MaskShaderList,
	CloudShaderList,
	FlatTerrainShaderList,
#if defined(BUILD_WITH_D3D9)
	ShadowDepthShaderList,
	ShadowMultiplyShaderList,
	SpecularShaderList,
	PointLightPassShaderList,
#endif
	nullptr
};

/** List of all custom filter lists - each list in this list contains variations of the same
	filter to allow it to work on different hardware configurations.
*/
W3DFilterInterface **MasterFilterList[]=
{
	ScreenDefaultFilterList,
	ScreenBWFilterList,
	ScreenMotionBlurFilterList,
	ScreenCrossFadeFilterList,
	nullptr
};

// W3DShaderManager::W3DShaderManager =========================================
/** Constructor - just clears some variables */
//=============================================================================
W3DShaderManager::W3DShaderManager()
{
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	m_oldRenderSurface = nullptr;
	m_renderTexture = nullptr;
	m_newRenderSurface = nullptr;
	m_oldDepthSurface = nullptr;
	m_renderingToTexture = false;
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++)
	{	W3DShaders[i]=nullptr;
		W3DShadersPassCount[i]=0;
	}
	for (i=0; i<FT_MAX; i++)
	{	W3DFilters[i]=nullptr;
	}
	for (i=0; i<8; i++)
	{
		m_Textures[i]=nullptr;
	}
	m_currentShader=(W3DShaderManager::ShaderTypes)-1;
}

// W3DShaderManager::init =======================================================
/** Walk through all shaders and find versions suitable for current hardware */
//=============================================================================
void W3DShaderManager::init()
{
	int i,j;

	D3DSURFACE_DESC desc;
	// For now, check & see if we are gf3 or higher on the food chain.

	ChipsetType res=DC_UNKNOWN;
	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		m_currentChipset = res;	//cache the current chipset.

		//Some of our effects require an offscreen render target, so try creating it here.
		HRESULT hr=DX8Wrapper::_Get_D3D_Device8()->GetRenderTarget(DX8_SWAPCHAIN &m_oldRenderSurface);

		if (hr != S_OK || !m_oldRenderSurface)
			return;

		m_oldRenderSurface->GetDesc(&desc);
		
		// TheSuperHackers @bugfix Redirecting rendering to a non-multisampled texture
		// while using a multisampled depth buffer is an API violation in DX8.
		if (desc.MultiSampleType == D3DMULTISAMPLE_NONE)
		{
			hr=DX8_CREATE_TEXTURE(DX8Wrapper::_Get_D3D_Device8(),desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&m_renderTexture);
		}
		else
		{
			// Force failure path to avoid MSAA mismatch
			hr = E_FAIL;
		}

		if (hr != S_OK)
		{
			SAFE_RELEASE(m_oldRenderSurface);
			m_renderTexture = nullptr;
		} else {
			hr = m_renderTexture->GetSurfaceLevel(0, &m_newRenderSurface);
			if (hr != S_OK)
			{
				SAFE_RELEASE(m_renderTexture);
				m_newRenderSurface = nullptr;
			}	else {
				hr = DX8Wrapper::_Get_D3D_Device8()->GetDepthStencilSurface(&m_oldDepthSurface);
				if (hr != S_OK)
				{
					SAFE_RELEASE(m_newRenderSurface);
					SAFE_RELEASE(m_renderTexture);
					m_oldDepthSurface = nullptr;
				}
			}
		}
	}

	W3DShaderInterface **shaders;

	for (i=0; MasterShaderList[i] != nullptr; i++)
	{
		shaders=MasterShaderList[i];
		for (j=0; shaders[j] != nullptr; j++)
		{
			if (shaders[j]->init())
				break;	//found a working shader
		}
	}
#if defined(BUILD_WITH_D3D9)
	if (SUCCEEDED(Load_Vertex_Shading_Shader("shaders\\instancemain.vso", &InstancedMainShader)))
	{
		DX8InstancingClass::Set_Main_Shader(Peek_D3D9_Vertex_Shader(InstancedMainShader));
	}
	else
	{
		InstancedMainShader = 0;
	}
	if (SUCCEEDED(Load_Vertex_Shading_Shader("shaders\\skinmain.vso", &SkinnedMainShader)))
	{
		DX8SkinningClass::Set_Main_Shader(Peek_D3D9_Vertex_Shader(SkinnedMainShader));
	}
	else
	{
		SkinnedMainShader = 0;
	}
#if defined(RTS_ZEROHOUR)
	if (SUCCEEDED(Load_Vertex_Shading_Shader("shaders\\particlebillboard.vso", &ParticleBillboardShader)))
	{
		PointGroupClass::Set_Billboard_Shader(Peek_D3D9_Vertex_Shader(ParticleBillboardShader));
	}
#endif
#endif

	W3DFilterInterface **filters;

	for (i=0; MasterFilterList[i] != nullptr; i++)
	{
		filters=MasterFilterList[i];
		for (j=0; filters[j] != nullptr; j++)
		{
			if (filters[j]->init())
				break;	//found a working shader
		}
	}

	RENDER_LOG(("ShaderManager ChipsetID %d", res));
}

// W3DShaderManager::shutdown =======================================================
/** Any shaders which allocate resources will be allowed to free them */
//=============================================================================
void W3DShaderManager::shutdown()
{
#if defined(BUILD_WITH_D3D9)
	DX8Wrapper::Set_Additional_Pass_Callback(nullptr);
	for (std::map<DWORD, AdditionalLightShaders>::iterator it = AdditionalShaders.begin(); it != AdditionalShaders.end(); ++it)
	{
		Set_D3D9_Shader_Name(it->second.normal, nullptr);
		SAFE_RELEASE(it->second.normal);
		Set_D3D9_Shader_Name(it->second.debug, nullptr);
		SAFE_RELEASE(it->second.debug);
	}
	AdditionalShaders.clear();
#endif
	SAFE_RELEASE(m_newRenderSurface);
	SAFE_RELEASE(m_renderTexture);
	SAFE_RELEASE(m_oldRenderSurface);
	SAFE_RELEASE(m_oldDepthSurface);
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	//release any assets associated with a shader (vertex/pixel shaders, textures, etc.)
	Int i=0;
	for (; i<W3DShaderManager::ST_MAX; i++) {
		if (W3DShaders[i]) {
			W3DShaders[i]->shutdown();
		}
	}

	for (i=0; i < FT_MAX; i++)
	{
		if (W3DFilters[i])
		{
			W3DFilters[i]->shutdown();
		}
	}

	for (size_t pass = 0; pass < ObjectSpecularPasses.size(); pass++)
	{
		REF_PTR_RELEASE(ObjectSpecularPasses[pass]);
	}
	ObjectSpecularPasses.clear();
	ObjectSpecularPassesUsed = 0;

	W3DGroundNoise::releaseResources();
	W3DSlopeMap::releaseResources();

#if defined(BUILD_WITH_D3D9)
#if defined(RTS_ZEROHOUR)
	PointGroupClass::Set_Billboard_Shader(nullptr);
	if (ParticleBillboardShader != 0)
	{
		DX8_DELETE_VERTEX_SHADER(DX8Wrapper::_Get_D3D_Device8(), ParticleBillboardShader);
		ParticleBillboardShader = 0;
	}
#endif
	DX8InstancingClass::Set_Main_Shader(nullptr);
	if (InstancedMainShader != 0)
	{
		DX8_DELETE_VERTEX_SHADER(DX8Wrapper::_Get_D3D_Device8(), InstancedMainShader);
		InstancedMainShader = 0;
	}
	DX8SkinningClass::Set_Main_Shader(nullptr);
	if (SkinnedMainShader != 0)
	{
		DX8_DELETE_VERTEX_SHADER(DX8Wrapper::_Get_D3D_Device8(), SkinnedMainShader);
		SkinnedMainShader = 0;
	}
#endif
}

//=============================================================================
void W3DShaderManager::updateCloud()
{
	terrainShader2Stage.updateCloud();
}

// W3DShaderManager::getShaderPasses =======================================================
/** Return number of renderig passes required in perform the desired shader on current
	hardware.  App will need to re-render the polygons this many times to complete the
	effect.
 */
//=============================================================================
Int W3DShaderManager::getShaderPasses(ShaderTypes shader)
{
	return W3DShadersPassCount[shader];
}

// W3DShaderManager::setShader =======================================================
/** Must call this method before each rendering pass in order to perform proper D3D
	setup for each shader.
 */
//=============================================================================
Int W3DShaderManager::setShader(ShaderTypes shader, Int pass)
{
	if (shader == m_currentShader && pass == m_currentShaderPass)
		return TRUE;	//shader is already set
	m_currentShader=shader;
	m_currentShaderPass = pass;
	if (W3DShaders[shader])
		return W3DShaders[shader]->set(pass);
	return FALSE;
}

// W3DShaderManager::resetShader =======================================================
/** Must call this method after all polygons and rendering passes have been submitted.
	This method allows D3D to reset itself to a default state that doesn't conflict
	with the WW3D2 Shader system.
 */
//=============================================================================
void W3DShaderManager::resetShader(ShaderTypes shader)
{
	if (m_currentShader == ST_INVALID)
		return;	//last shader is already reset.
	if (W3DShaders[shader])
		W3DShaders[shader]->reset();
	m_currentShader = ST_INVALID;
}
// W3DShaderManager::filterPreRender =======================================================
/** Call to view filter shaders before rendering starts.
 */
//=============================================================================
Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (W3DFilters[filter])
	{	Bool result=W3DFilters[filter]->preRender(skipRender,scenePassMode);
		if (result)
			m_currentFilter = filter;
		return result;
	}
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterPostRender(FilterTypes filter, FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->postRender(mode, scrollDelta,doExtraRender);

	m_currentFilter = FT_NULL_FILTER;
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
	static Bool filterSetup(FilterTypes filter, FilterModes mode);
Bool W3DShaderManager::filterSetup(FilterTypes filter, FilterModes mode)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->setup(mode);
	return FALSE;
}

/*Draws 2 triangles covering the viewport given the current render states*/
void W3DShaderManager::drawViewport(Int color)
{
	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();
	const Real targetScale = W3DShaderManager::getScreenToTargetScale();

	//bottom right
	v[0].p = Vector4( (xpos+width)*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( (xpos+width)*targetScale-0.5f, ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos*targetScale-0.5f, (ypos+height)*targetScale-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos*targetScale-0.5f,  ypos*targetScale-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = color;
	v[1].color = color;
	v[2].color = color;
	v[3].color = color;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8_SET_FVF(pDev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Starts rendering to a texture.
 */
//=============================================================================
void W3DShaderManager::startRenderToTexture()
{
	DEBUG_ASSERTCRASH(!m_renderingToTexture, ("Already rendering to texture - cannot nest calls."));

	if (m_renderingToTexture || m_newRenderSurface==nullptr || m_oldDepthSurface==nullptr) return;

	// The flip model rotates the back buffers, so what is bound now is what endRenderToTexture restores.
	IDirect3DSurface8 *currentTarget = nullptr;
	IDirect3DSurface8 *currentDepth = nullptr;
	if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetRenderTarget(DX8_SWAPCHAIN &currentTarget)) ||
		FAILED(DX8Wrapper::_Get_D3D_Device8()->GetDepthStencilSurface(&currentDepth)))
	{
		SAFE_RELEASE(currentTarget);
		return;
	}
	SAFE_RELEASE(m_oldRenderSurface);
	SAFE_RELEASE(m_oldDepthSurface);
	m_oldRenderSurface = currentTarget;
	m_oldDepthSurface = currentDepth;

	HRESULT hr = DX8Wrapper::Set_DX8_Render_Target_Surfaces(m_newRenderSurface,m_oldDepthSurface);

	// TheSuperHackers @bugfix If SetRenderTarget fails (e.g. due to MSAA forced by driver
	// profile causing a depth buffer mismatch that D3DSURFACE_DESC doesn't report), permanently
	// disable RTT to prevent repeated failures and accidental backbuffer clears.
	if (hr != S_OK)
	{
		// Permanently disable RTT
		SAFE_RELEASE(m_newRenderSurface);
		SAFE_RELEASE(m_renderTexture);
		SAFE_RELEASE(m_oldRenderSurface);
		SAFE_RELEASE(m_oldDepthSurface);
		return;
	}

	m_renderingToTexture = true;
	if (TheGlobalData->m_showSoftWaterEdge)
	{	//Soft water edges use frame buffer destination alpha so we must clear it to a known value.
		if (m_currentFilter == FT_VIEW_MOTION_BLUR_FILTER || m_currentFilter == FT_VIEW_CROSSFADE)
		{	//these filters rely on the previous frame being visible so we must be careful about clearing
			//frame buffer.  Only clear the alpha channel
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);	//only clear alpha
			ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;
			shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			DX8Wrapper::Set_Shader(shader);

			VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			DX8Wrapper::Set_Material(vmat);
			REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

			drawViewport(0x00ffffff | (((Int)(TheWaterTransparency->m_minWaterOpacity*255.0f)) <<24));
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE);	//disable writes to alpha
		}
		else	//normal clear that overwrites everything.
			DX8Wrapper::Clear(true, false, Vector3( 0.0f, 0.0f, 0.0f ), TheWaterTransparency->m_minWaterOpacity);
	}
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Ends rendering to a texture.
 */
//=============================================================================
IDirect3DTexture8 *W3DShaderManager::endRenderToTexture()
{
	DEBUG_ASSERTCRASH(m_renderingToTexture, ("Not rendering to texture."));
	if (!m_renderingToTexture) return nullptr;
	HRESULT hr = DX8Wrapper::Set_DX8_Render_Target_Surfaces(m_oldRenderSurface,m_oldDepthSurface);	//restore original render target
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr == S_OK)
	{
		//assume render target texture will be in stage 0.  Most hardware has "conditional" support for
		//non-power-of-2 textures so we must force some required states:
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

		m_renderingToTexture = false;
	}
	return m_renderTexture;
}

/**Returns texture containing the image that was last rendered using any of the effects requiring render target
textures.  Used mostly for cross-fading effects that need an unmodified version of the view before the effect
was applied.  NOTE: This texture does not survive device reset.. so quit effect on reset!*/
IDirect3DTexture8 *W3DShaderManager::getRenderTexture()
{
	return m_renderTexture;
}

Bool W3DShaderManager::copyRenderTarget(IDirect3DTexture8 *&copy, const RECT *area)
{
#if defined(BUILD_WITH_D3D9)
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DSurface8 *target = nullptr;
	if (FAILED(device->GetRenderTarget(0, &target)))
	{
		return FALSE;
	}

	D3DSURFACE_DESC targetDesc;
	target->GetDesc(&targetDesc);
	if (copy != nullptr)
	{
		D3DSURFACE_DESC copyDesc;
		copy->GetLevelDesc(0, &copyDesc);
		if (copyDesc.Width != targetDesc.Width || copyDesc.Height != targetDesc.Height || copyDesc.Format != targetDesc.Format)
		{
			SAFE_RELEASE(copy);
		}
	}
	if (copy == nullptr &&
		FAILED(device->CreateTexture(targetDesc.Width, targetDesc.Height, 1, D3DUSAGE_RENDERTARGET, targetDesc.Format, D3DPOOL_DEFAULT, &copy, nullptr)))
	{
		copy = nullptr;
		target->Release();
		return FALSE;
	}

	// StretchRect also resolves a multisampled target, but only as a whole.
	if (targetDesc.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		area = nullptr;
	}

	IDirect3DSurface8 *copySurface = nullptr;
	Bool copied = FALSE;
	if (SUCCEEDED(copy->GetSurfaceLevel(0, &copySurface)))
	{
		copied = SUCCEEDED(device->StretchRect(target, area, copySurface, area, D3DTEXF_NONE));
		copySurface->Release();
	}
	target->Release();
	return copied;
#else
	(void)copy;
	(void)area;
	return FALSE;
#endif
}

Real W3DShaderManager::getScreenToTargetScale()
{
	int targetWidth, targetHeight, deviceWidth, deviceHeight, bits;
	bool windowed;
	WW3D::Get_Render_Target_Resolution(targetWidth, targetHeight, bits, windowed);
	WW3D::Get_Device_Resolution(deviceWidth, deviceHeight, bits, windowed);
	return (deviceWidth > 0) ? (Real)targetWidth / (Real)deviceWidth : 1.0f;
}

Vector4 W3DShaderManager::getClipToTargetMapping(Real width, Real height)
{
	D3DVIEWPORT8 viewport;
	DX8Wrapper::Get_Viewport(viewport);
	return Vector4(0.5f * viewport.Width / width, -0.5f * viewport.Height / height,
		(viewport.X + 0.5f * viewport.Width + 0.5f) / width, (viewport.Y + 0.5f * viewport.Height + 0.5f) / height);
}

void W3DShaderManager::drawClipQuad(const Vector4 &clipToTarget)
{
	static const Real cornerX[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
	static const Real cornerY[4] = { 1.0f, 1.0f, -1.0f, -1.0f };

	DynamicVBAccessClass vbAccess(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, 4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vbAccess);
		VertexFormatXYZNDUV2 *verts = lock.Get_Formatted_Vertex_Array();
		for (Int i = 0; i < 4; i++)
		{
			verts[i].x = cornerX[i];
			verts[i].y = cornerY[i];
			verts[i].z = 0.0f;
			verts[i].nx = 0.0f;
			verts[i].ny = 0.0f;
			verts[i].nz = 0.0f;
			verts[i].diffuse = 0xffffffff;
			verts[i].u1 = cornerX[i] * clipToTarget.X + clipToTarget.Z;
			verts[i].v1 = cornerY[i] * clipToTarget.Y + clipToTarget.W;
			verts[i].u2 = 0.0f;
			verts[i].v2 = 0.0f;
		}
	}

	DynamicIBAccessClass ibAccess(BUFFER_TYPE_DYNAMIC_DX8, 6);
	{
		DynamicIBAccessClass::WriteLockClass lock(&ibAccess);
		UnsignedShort *indices = lock.Get_Index_Array();
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 2;
		indices[3] = 2;
		indices[4] = 1;
		indices[5] = 3;
	}

	DX8Wrapper::Set_Vertex_Buffer(vbAccess);
	DX8Wrapper::Set_Index_Buffer(ibAccess, 0);
	DX8Wrapper::Draw_Triangles(0, 2, 0, 4);
}

enum GraphicsVenderID CPP_11(: Int)
{
	DC_NVIDIA_VENDOR_ID	= 0x10DE,
	DC_3DFX_VENDOR_ID	= 0x121A,
	DC_ATI_VENDOR_ID	= 0x1002
};

// W3DShaderManager::ChipsetType =======================================================
/** Returns the chipset used by the currently active rendering device.  Can be useful
	for coding around specific driver bugs.
 */
//=============================================================================
ChipsetType W3DShaderManager::getChipset()
{
	//check if globaldata has an override for current chipset
	if (TheGlobalData && TheGlobalData->m_chipSetType != DC_UNKNOWN)
		return (ChipsetType)TheGlobalData->m_chipSetType;

	ChipsetType chip=DC_UNKNOWN;
	IDirect3D8* d3d8Interface=DX8Wrapper::_Get_D3D8();

	if (d3d8Interface && DX8Wrapper::_Get_D3D_Device8())
	{

		D3DADAPTER_IDENTIFIER8 did;
		::ZeroMemory(&did, sizeof(D3DADAPTER_IDENTIFIER8));
	/*	HRESULT res = */ d3d8Interface->GetAdapterIdentifier(0,D3DENUM_NO_WHQL_LEVEL,&did);
		*((LARGE_INTEGER*)&m_driverVersion) = did.DriverVersion;

		if(did.VendorId == DC_NVIDIA_VENDOR_ID)
		{
			m_currentVendor = DC_NVIDIA_VENDOR_ID;

			if (did.DeviceId == 0x20)
				return DC_TNT;

			if (did.DeviceId >= 0x28 && did.DeviceId < 0x100)
				return DC_TNT2;

			if ( (did.DeviceId >= 0x100 && did.DeviceId <= 0x103) ||	//GeForce
				 (did.DeviceId >= 0x110 && did.DeviceId <= 0x113) ||	//GeForce2 MX
						 (did.DeviceId >= 0x150 && did.DeviceId <= 0x153) )	//GeForce2
           		return DC_GEFORCE2;

			if (did.DeviceId >= 0x200 && did.DeviceId < 0x250)
				return DC_GEFORCE3;

			if (did.DeviceId >= 0x250)
				return DC_GEFORCE4;
		}
		else
		if(did.VendorId == DC_3DFX_VENDOR_ID)
		{
			m_currentVendor = DC_3DFX_VENDOR_ID;

			if (did.DeviceId == 0x0002)
				return DC_VOODOO2;
			if (did.DeviceId == 0x0005)
				return DC_VOODOO3;
			if (did.DeviceId == 0x0008)	///@todo: Just guessing on this one - find actual Voodoo4 deviceID.
				return DC_VOODOO4;
			if (did.DeviceId == 0x0009)
				return DC_VOODOO5;
		}
		else
		if(did.VendorId == DC_ATI_VENDOR_ID)
		{
			m_currentVendor = DC_ATI_VENDOR_ID;

			if (did.DeviceId == 0x5144)
				return DC_RADEON;
			if (did.DeviceId == 0x514C)
				return DC_RADEON_8500;
			if (did.DeviceId == 0x4e44)
				return DC_RADEON_9700;
		}

		//None of the vendor specific ID's matched so use generic means to classify the card
		Int maxTextures=DX8Wrapper::Get_Current_Caps()->Get_Max_Simultaneous_Textures();
		Real pixelShaderVersion;

		char buf[256];

		//Convert version to Real
		sprintf(buf,"%d.%d",DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Major_Version(),DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Minor_Version());
		sscanf(buf,"%f",&pixelShaderVersion);

		if (maxTextures >= 4)
		{	if (pixelShaderVersion >= 1.1f)
				chip=DC_GENERIC_PIXEL_SHADER_1_1;
			if (pixelShaderVersion >= 1.4f)
				chip=DC_GENERIC_PIXEL_SHADER_1_4;
			if (maxTextures >= 8 && pixelShaderVersion >= 2.0f)
				chip=DC_GENERIC_PIXEL_SHADER_2_0;
		}
	}

	return chip;
}

//=============================================================================
// WaterRenderObjClass::LoadAndCreateShader
//=============================================================================
/** Loads and creates a D3D pixel or vertex shader.*/
//=============================================================================
HRESULT W3DShaderManager::LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, DWORD* pHandle)
{
	if (getChipset() < DC_GENERIC_PIXEL_SHADER_1_1)
		return E_FAIL;	//don't allow loading any shaders if hardware can't handle it.

	try
	{
		File *file = nullptr;
		HRESULT hr;
		void *pFileData = nullptr;
		const void *pData = nullptr;
		DWORD dwSize = 0;

		// A loose or archived file overrides the copy linked into the executable.
		file = TheFileSystem->openFile(strFilePath, File::READ | File::BINARY);
		if (file != nullptr)
		{
			FileInfo fileInfo;
			TheFileSystem->getFileInfo(AsciiString(strFilePath), &fileInfo);
			dwSize = fileInfo.sizeLow;

			pFileData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
			if (!pFileData)
			{
				file->close();
				RENDER_LOG(("LoadAndCreateD3DShader: out of memory for %s", strFilePath));
				return E_FAIL;
			}

			file->read(pFileData, dwSize);

			file->close();
			file = nullptr;
			pData = pFileData;
		}
		else if (Find_Embedded_Shader(strFilePath, pData, dwSize))
		{
			RENDER_LOG(("LoadAndCreateD3DShader: no file for %s, using the embedded copy", strFilePath));
		}
		else
		{
			RENDER_LOG(("LoadAndCreateD3DShader: could not open %s and none is embedded", strFilePath));
			return E_FAIL;
		}

		const DWORD* pShader = (const DWORD*)pData;

		do
		{
#if defined(BUILD_WITH_D3D9)
			// D3D9 separates the declaration from the shader and hands back COM objects,
			// so both go into the table the DWORD handle indexes.
			if (ShaderType) // SHADERTYPE_VERTEX
			{
				IDirect3DVertexDeclaration9* declaration = nullptr;
				hr = Create_D3D9_Declaration_From_D3D8(pDeclaration, &declaration);
				if (SUCCEEDED(hr))
				{
					IDirect3DVertexShader9* vertex_shader = nullptr;
					hr = DX8Wrapper::_Get_D3D_Device8()->CreateVertexShader(pShader, &vertex_shader);
					if (SUCCEEDED(hr))
					{
						*pHandle = Register_D3D9_Vertex_Shader(vertex_shader, declaration, strFilePath);
						if (*pHandle == 0)
						{
							vertex_shader->Release();
							declaration->Release();
							hr = E_OUTOFMEMORY;
						}
					}
					else
					{
						declaration->Release();
					}
				}
			}
			else // SHADERTYPE_PIXEL
			{
				IDirect3DPixelShader9* pixel_shader = nullptr;
				hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(pShader, &pixel_shader);
				if (SUCCEEDED(hr))
				{
					*pHandle = Register_D3D9_Pixel_Shader(pixel_shader, strFilePath);
					if (*pHandle == 0)
					{
						pixel_shader->Release();
						hr = E_OUTOFMEMORY;
					}
					else
					{
						Attach_Lights_Debug_Twin(strFilePath, *pHandle);
						Attach_Additional_Light_Shaders(strFilePath, *pHandle);
					}
				}
			}
#else
			if (ShaderType) // SHADERTYPE_VERTEX
			{
				hr = DX8Wrapper::_Get_D3D_Device8()->CreateVertexShader(pDeclaration, pShader, pHandle, Usage);
			}
			else // SHADERTYPE_PIXEL
			{
				hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(pShader, pHandle);
			}
#endif

#if defined(BUILD_WITH_D3D9)
			if (FAILED(hr) && pFileData != nullptr && Find_Embedded_Shader(strFilePath, pData, dwSize))
			{
				HeapFree(GetProcessHeap(), 0, pFileData);
				pFileData = nullptr;
				pShader = (const DWORD*)pData;
				RENDER_LOG(("LoadAndCreateD3DShader: rejected file %s, hr=0x%08X, retrying the embedded copy", strFilePath, hr));
				continue;
			}
#endif
			break;
		} while (true);

		if (pFileData)
		{
			HeapFree(GetProcessHeap(), 0, pFileData);
		}

		if (FAILED(hr))
		{
			RENDER_LOG(("LoadAndCreateD3DShader: failed to create %s, hr=0x%08X", strFilePath, hr));
			return E_FAIL;
		}
	}
	catch(...)
	{
		RENDER_LOG(("LoadAndCreateD3DShader: exception loading %s", strFilePath));
		return E_FAIL;
	}

	RENDER_LOG(("LoadAndCreateD3DShader: loaded %s -> handle 0x%08X", strFilePath, *pHandle));
	return S_OK;
}

//For the MP test, we're enforcing high min-spec requirements that need to be verified.
#define MIN_INTEL_CPU_FREQ	1300
#define MIN_AMD_CPU_FREQ	1100
#define MIN_ACCEPTED_FREQUENCY	1300
#define MIN_ACCEPTED_MEMORY	(1024*1024*256)	//256 MB
#define MIN_ACCEPTED_TEXTURE_MEMORY	(1024*1024*30)	//30 MB

/**Hack to give gameengine access to this function*/
Bool testMinimumRequirements(ChipsetType *videoChipType, MemValueType *numRAM)
{
	return W3DShaderManager::testMinimumRequirements(videoChipType,numRAM);
}

Bool W3DShaderManager::testMinimumRequirements(ChipsetType *videoChipType, MemValueType *numRAM)
{
	if (videoChipType)
		*videoChipType = getChipset();

	if (numRAM)
		*numRAM=CPUDetectClass::Get_Total_Physical_Memory();

	return TRUE;
}

/**Try to guess how well the video card will handle the game assuming very fast CPU*/
StaticGameLODLevel W3DShaderManager::getGPUPerformanceIndex()
{
	ChipsetType	chipType;
	StaticGameLODLevel detailSetting=STATIC_GAME_LOD_LOW;	//assume lowest settings for now.

	if ((chipType=getChipset()) != DC_UNKNOWN)
	{	//a known video card so we can make some assumptions
		if (chipType >=	DC_GEFORCE2)
			detailSetting=STATIC_GAME_LOD_LOW;	//these cards need multiple terrain passes.
		if (chipType >= DC_GENERIC_PIXEL_SHADER_1_1)	//these cards can do terrain in single pass.
			detailSetting=STATIC_GAME_LOD_VERY_HIGH;
	}

	return detailSetting;
}

/**We need a hardware independent method to compare different CPU's.  For lack of anything better, we'll
use time to calculate PIE using a slow random number algorithm.*/

/**Used to test function call overhead*/
void add(float *sum,float *addend)
{
	*sum = *sum + *addend;
}

/**Returns seconds needed to run the test*/
Real W3DShaderManager::GetCPUBenchTime()
{
	float ztot, yran, ymult, ymod, x, y, z, pi, prod;
    long int low, ixran, itot, j, iprod;

  	__int64 endTime64,freq64,startTime64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);

    ztot = 0.0;
    low = 1;
    ixran = 1907;
    yran = 5813.0;
    ymult = 1307.0;
    ymod = 5471.0;
    itot = 560000;	//total iterations. This value ends up running at ~30 fps on our P4-2.2Ghz.

    for(j=1; j<=itot; j++)
    {
		iprod = 27611 * ixran;
		ixran = iprod - 74383*(long int)(iprod/74383);
		x = (float)ixran / 74383.0;
		prod = ymult * yran;
		yran = (prod - ymod*(long int)(prod/ymod));
		y = yran / ymod;
		z = x*x + y*y;
		add(&ztot,&z);
		if ( z <= 1.0 )
		{
		  low = low + 1;
		}
	}
	pi = 4.0 * (float)low/(float)itot;

	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	return ((double)(endTime64-startTime64)/(double)(freq64));
}


// W3DShaderManager::setShroudTex =======================================================
/** Puts the shroud texture into a texture stage.
 */
//=============================================================================
Int W3DShaderManager::setShroudTex(Int stage)
{
	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
	{
		DX8Wrapper::Set_Texture(stage, shroud->getShroudTexture());

		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2 );

		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		D3DMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		Set_D3DMATRIX_Translation(offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		Set_D3DMATRIX_Scaling(scale, width, height, 1);
		curView = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
		return TRUE;
	}
	return FALSE;
}



Int FlatTerrainShader2Stage::init()
{
	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=1;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=2;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=2;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=2;

	return TRUE;
}

void FlatTerrainShader2Stage::reset()
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}


Int FlatTerrainShader2Stage::set(Int pass)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	if (TheGlobalData && (TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex)) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}
	W3DShaderManager::setTerrainTextureFilter(1, FALSE);

	switch (pass)
	{
		case 0:

			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			if (W3DShaderManager::getShaderTexture(0)) {
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

				DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

				//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
				//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
				W3DShroud *shroud;
				if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
				{
					D3DMATRIX curView;
					DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

					D3DMATRIX inv;
					float det;
					Invert_D3DMATRIX(inv, &det, curView);

					D3DMATRIX scale,offset;

					//We need to make all world coordinates be relative to the heightmap data origin since that
					//is where the shroud begins.

					float xoffset = 0;
					float yoffset = 0;
					Real width=shroud->getCellWidth();
					Real height=shroud->getCellHeight();

					if (TheTerrainRenderObject->getMap())
					{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
						xoffset = -(float)shroud->getDrawOriginX() + width;
						yoffset = -(float)shroud->getDrawOriginY() + height;
					}

					Set_D3DMATRIX_Translation(offset, xoffset, yoffset,0);

					width = 1.0f/(width*shroud->getTextureWidth());
					height = 1.0f/(height*shroud->getTextureHeight());
					Set_D3DMATRIX_Scaling(scale, width, height, 1);
					curView = (inv * offset) * scale;
					DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0), curView);
				}
			}	else {
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG2 );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
			}
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 0 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
			break;
		case 1:
			// Noise/cloud pass
			D3DMATRIX curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			//blend into frame buffer
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ZERO);

			D3DMATRIX inv;
			float det;
			Invert_D3DMATRIX(inv, &det, curView);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass

				terrainShader2Stage.updateNoise1(&curView,&inv);	//update curView with texture matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				//setup noise pass

				terrainShader2Stage.updateNoise2(&curView,&inv);
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			}
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					terrainShader2Stage.updateNoise1(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					terrainShader2Stage.updateNoise2(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
			}
			break;
	}

	return TRUE;
}






Int FlatTerrainShaderPixelShader::shutdown()
{
	if (m_dwBasePixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBasePixelShader);

	if (m_dwBase0PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBase0PixelShader);

	if (m_dwBaseNoise1PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBaseNoise1PixelShader);

	if (m_dwBaseNoise2PixelShader)
		DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwBaseNoise2PixelShader);

	m_dwBasePixelShader=0;
	m_dwBase0PixelShader=0;
	m_dwBaseNoise1PixelShader=0;
	m_dwBaseNoise2PixelShader=0;

	for (Int i=0; i<4; i++)
	{
		if (m_dwLitPixelShader[i])
		{
			DX8_DELETE_PIXEL_SHADER(DX8Wrapper::_Get_D3D_Device8(), m_dwLitPixelShader[i]);
		}
		m_dwLitPixelShader[i]=0;
	}
	FlatPixelLightsLoaded = FALSE;

	return TRUE;
}

void FlatTerrainShaderPixelShader::initPixelLights()
{
	for (Int i=0; i<4; i++)
	{
		m_dwLitPixelShader[i]=0;
	}
	m_lightStage = -1;
	FlatPixelLightsLoaded = FALSE;

#if defined(BUILD_WITH_D3D9)
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == nullptr || !Supports_Pixel_Shader_2_a(caps) || Get_Pixel_Light_Mode() < PIXEL_LIGHTS_TERRAIN)
	{
		return;
	}

	// Flat terrain has no vertex lighting to hand lights over from, so a missing variant only leaves its draws unlit.
	Bool complete = TRUE;
	for (Int i=0; i<4; i++)
	{
		char file[64];
		snprintf(file, sizeof(file), "shaders\\flatterrainlit%d.pso", i + 1);
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(file, nullptr, 0, false, &m_dwLitPixelShader[i])))
		{
			m_dwLitPixelShader[i]=0;
			complete = FALSE;
		}
	}
	FlatPixelLightsLoaded = complete;
#endif
}

Int FlatTerrainShaderPixelShader::init()
{
	Int res;

#ifdef DISABLE_PIXEL_SHADERS
	return false;
#endif

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if ((res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//shader decleration
			DWORD Declaration[]=
			{
				(D3DVSD_STREAM(0)),
				(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
				(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
				(D3DVSD_REG(2, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_REG(3, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_END())
			};

			//base version which doesn't apply any noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrain.pso", &Declaration[0], 0, false, &m_dwBasePixelShader);
			if (FAILED(hr))
				return FALSE;

			//base version which doesn't apply any shroud textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrain0.pso", &Declaration[0], 0, false, &m_dwBase0PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 1 noise texture.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrainnoise.pso", &Declaration[0], 0, false, &m_dwBaseNoise1PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 2 noise textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrainnoise2.pso", &Declaration[0], 0, false, &m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			initPixelLights();

			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=&flatTerrainShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

Int FlatTerrainShaderPixelShader::set(Int pass)
{
	//setup base pass
	Int curStage = 1;
	// setup terrain [3/31/2003]

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_Texture(0, W3DShaderManager::getShaderTexture(2));
	DX8Wrapper::Set_Texture(1, W3DShaderManager::getShaderTexture(2));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();




	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	W3DShaderManager::setTerrainTextureFilter(curStage, FALSE);

	curStage = 0;

	W3DShroud *shroud = TheTerrainRenderObject->getShroud();
	if (shroud) {

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

		//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
		//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
		{
			D3DMATRIX curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			D3DMATRIX inv;
			float det;
			Invert_D3DMATRIX(inv, &det, curView);

			D3DMATRIX scale,offset;

			//We need to make all world coordinates be relative to the heightmap data origin since that
			//is where the shroud begins.

			float xoffset = 0;
			float yoffset = 0;
			Real width=shroud->getCellWidth();
			Real height=shroud->getCellHeight();

			if (TheTerrainRenderObject->getMap())
			{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
				xoffset = -(float)shroud->getDrawOriginX() + width;
				yoffset = -(float)shroud->getDrawOriginY() + height;
			}

			Set_D3DMATRIX_Translation(offset, xoffset, yoffset,0);

			width = 1.0f/(width*shroud->getTextureWidth());
			height = 1.0f/(height*shroud->getTextureHeight());
			Set_D3DMATRIX_Scaling(scale, width, height, 1);
			curView = (inv * offset) * scale;
			DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), curView);
		}
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, shroud->getShroudTexture()->Peek_D3D_Texture());
		curStage++;
		if (curStage==1) curStage++;
	}

	Bool doNoise1 = (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1 ||
						W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12);
	if (doNoise1) {	 // Cloud pass.
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
		terrainShader2Stage.updateNoise1(&curView,&inv);	//update curView with texture matrix
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), curView);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

		curStage++;
		if (curStage==1) curStage++;
	}

	Bool doNoise2 = (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2 ||
						W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12);
	if (doNoise2)
	{
		D3DMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DMATRIX inv;
		float det;
		Invert_D3DMATRIX(inv, &det, curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
		terrainShader2Stage.updateNoise2(&curView,&inv);	//update curView with texture matrix
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), curView);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

		curStage++;
		if (curStage==1) curStage++;
	}
	if (curStage<2) {
		DX8Wrapper::Set_Pixel_Shader(m_dwBase0PixelShader);
	}	else if (curStage==2) {
		DX8Wrapper::Set_Pixel_Shader(m_dwBasePixelShader);
	}	else if (curStage==3) {
		DX8Wrapper::Set_Pixel_Shader(m_dwBaseNoise1PixelShader);
	}else if (curStage==4) {
		DX8Wrapper::Set_Pixel_Shader(m_dwBaseNoise2PixelShader);
	}

	// Each tile then picks its lights. The world position goes on the first stage past the textures,
	// which is 2 when only the terrain on stage 1 is read.
	if (FlatPixelLightsLoaded && (PixelLightCount > 0 || Get_Lights_Debug()))
	{
		const Int textureCount = (curStage < 2) ? 1 : curStage;
		const DWORD unlitShaders[4] = { m_dwBase0PixelShader, m_dwBasePixelShader, m_dwBaseNoise1PixelShader, m_dwBaseNoise2PixelShader };
		m_lightStage = max(curStage, 2);
		Set_Terrain_World_Position(m_lightStage);
		Begin_Draw_Pixel_Lights(unlitShaders[textureCount - 1], m_dwLitPixelShader[textureCount - 1]);
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
	return TRUE;
}

void FlatTerrainShaderPixelShader::reset()
{
	if (m_lightStage >= 0)
	{
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(m_lightStage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_lightStage);
	}
	m_lightStage = -1;
	End_Draw_Pixel_Lights();

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2,nullptr);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3,nullptr);	//release reference to any texture

	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, nullptr);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, nullptr);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}





