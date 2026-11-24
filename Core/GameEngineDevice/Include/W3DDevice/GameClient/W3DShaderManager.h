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

// FILE: W3DShaderManager.h /////////////////////////////////////////////////////////
//
// Custom shader system that allows more options and easier device validation than
// possible with W3D2.
//
// Author: Mark Wilczynski, August 2001
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

// Ronin @build 18/10/2025 Include DX8-to-DX9 compatibility layer first
#include "WW3D2/dx8todx9.h"
#include <map>  // Ronin @bugfix 16/11/2025: Required for std::map in shader declaration storage


#include "WW3D2/texture.h"
enum FilterTypes CPP_11(: Int);
enum FilterModes CPP_11(: Int);
enum CustomScenePassModes CPP_11(: Int);
enum StaticGameLODLevel CPP_11(: Int);
enum ChipsetType CPP_11(: Int);
enum CpuType CPP_11(: Int);
enum GraphicsVenderID CPP_11(: Int);

class TextureClass;	///forward reference
/** System for managing complex rendering settings which are either not handled by
	WW3D2 or need custom paths depending on the video card.  This system will determine
	the proper shader given video card limitations and also allow the app to query the
	hardware for specific features.
*/
class W3DShaderManager
{
public:

	//put any custom shaders (not going through W3D) in here.
	enum ShaderTypes
	{	ST_INVALID,			//invalid shader type.
		ST_TERRAIN_BASE,	//shader to apply base terrain texture only
		ST_TERRAIN_BASE_NOISE1,	//shader to apply base texture and cloud/noise 1.
		ST_TERRAIN_BASE_NOISE2,	//shader to apply base texture and cloud/noise 2.
		ST_TERRAIN_BASE_NOISE12,//shader to apply base texture and both cloud/noise
		// @feature Ronin 27/04/2026 Splat S20-A2c: per-material weighted splat shader.
		// Samples a stack of BGRA8 weight atlas pages produced by
		// WorldHeightMap::ensurePerMaterialWeightAtlasTextures (S20-A2a) and accumulates weighted material colors in the PS.
		// See docs/Terrain_Splat_Map_Design.md S20 A2-c.
		ST_TERRAIN_PER_MATERIAL,
		ST_SHROUD_TEXTURE,		//shader to apply shroud texture projection.
		ST_MASK_TEXTURE,		//shader to apply alpha mask texture projection.
		ST_ROAD_BASE,	//shader to apply base terrain texture only
		ST_ROAD_BASE_NOISE1,	//shader to apply base texture and cloud/noise 1.
		ST_ROAD_BASE_NOISE2,	//shader to apply base texture and cloud/noise 2.
		ST_ROAD_BASE_NOISE12,//shader to apply base texture and both cloud/noise
		ST_CLOUD_TEXTURE,			//shader to project clouds.
		ST_FLAT_TERRAIN_BASE,	//shader to apply base terrain texture only
		ST_FLAT_TERRAIN_BASE_NOISE1,	//shader to apply base texture and cloud/noise 1.
		ST_FLAT_TERRAIN_BASE_NOISE2,	//shader to apply base texture and cloud/noise 2.
		ST_FLAT_TERRAIN_BASE_NOISE12,//shader to apply base texture and both cloud/noise
		ST_FLAT_SHROUD_TEXTURE,		//shader to apply shroud texture projection.
		ST_MAX
	};


	W3DShaderManager();	///<constructor
	static void init();	///<determine optimal shaders for current device.
	static void shutdown();	///<release resources used by shaders
	static void updateCloud();	///<update the cloud position once every render frame.
	// @feature Ronin 16/05/2026 DX9: expose the live terrain cloud projection
	// state so rigid-mesh shaders can sample the same moving cloud field.
	static void getCloudMapState(float* pScale, float* pOffsetX, float* pOffsetY);
	// Ronin @bugfix 19/09/2026 DX9: the same world->shroud mapping setShroudTex builds for the fixed-function overlay, as
	// plain numbers, so the programmable rigid shaders can sample the shroud themselves instead of taking a second,
	// depth-compared pass that blackened alpha meshes at the fog edge. FALSE (and zeroes) when the map has no shroud.
	static Int getShroudMapState(float* pOffsetX, float* pOffsetY, float* pScaleX, float* pScaleY);
	static TextureClass* getShroudTexture(void);

	// Info for a render to texture surface for special effects.
	static Bool m_renderingToTexture;
	static IDirect3DSurface8* m_oldRenderSurface;	///<previous render target
	static IDirect3DTexture8* m_renderTexture;		///<texture into which rendering will be redirected.
	static IDirect3DSurface8* m_newRenderSurface;	///<new render target inside m_renderTexture
	static IDirect3DSurface8* m_oldDepthSurface;	///<previous depth buffer surface (backbuffer's, may be MSAA)
	static IDirect3DSurface8* m_rttDepthSurface;	///<dedicated non-MSAA depth surface for RTT pass // @bugfix Ronin 04/03/2026

	// Ronin @bugfix 16/11/2026: Retrieve stored vertex declaration for a shader
	static IDirect3DVertexDeclaration9* GetShaderDeclaration(void* shaderHandle);

	static ChipsetType getChipset();	///<return current device chipset.
	static GraphicsVenderID getCurrentVendor() {return m_currentVendor;}	///<return current card vendor.
	static __int64 getCurrentDriverVersion() {return m_driverVersion; }	///<return current driver version.
	static Int getShaderPasses(ShaderTypes shader);	///<rendering passes required for shader
	static Int setShader(ShaderTypes shader, Int pass);	///<enable specific shader pass.
	static Int setShroudTex(Int stage);	///<Set shroud in a texture stage.
	static void resetShader(ShaderTypes shader);	///<make sure W3D2 gets restored to normal
	///Specify all textures (up to 8) which can be accessed by the shaders.
	static void setTexture(Int stage,TextureClass* texture) {m_Textures[stage]=texture;}
	///Return current texture available to shaders.
	static TextureClass *getShaderTexture(Int stage) { return m_Textures[stage];}	///<returns currently selected texture for given stage
	///Return last activated shader.
	static ShaderTypes getCurrentShader() {return m_currentShader;}
	/// Loads a .vso file and creates a vertex shader for it
	static HRESULT LoadAndCreateD3DShader(const char* strFilePath, const D3DVERTEXELEMENT9* pDeclaration, DWORD Usage, Bool ShaderType, void** pHandle);
	// Ronin @feature 24/11/2026 DX9: a compiled shader's bytes through the game's file system (the shaders folder or a .big),
	// as EA's own come from ShadersZH.big. nullptr when missing or empty; the caller frees it with HeapFree.
	static DWORD *readShaderFile(const char *path, DWORD *outSize = nullptr);

	static Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex);
	static StaticGameLODLevel getGPUPerformanceIndex();
	static Real GetCPUBenchTime();

	// Filter methods
	static Bool filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode); ///< Set up at start of render.  Only applies to screen filter shaders.
	static Bool filterPostRender(FilterTypes filter, FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender); ///< Called after render.  Only applies to screen filter shaders.
	static Bool filterSetup(FilterTypes filter, FilterModes mode);

	// Support routines for filter methods.
	static Bool canRenderToTexture() { return (m_oldRenderSurface && m_newRenderSurface);}
	static void startRenderToTexture(); ///< Sets render target to texture.
	static IDirect3DTexture8 * endRenderToTexture(); ///< Ends render to texture, & returns texture.
	static IDirect3DTexture8 * getRenderTexture();	///< returns last used render target texture
	static Bool isRenderingToTexture() {return m_renderingToTexture; }
	// Ronin @feature 12/10/2026 DX9: HDR stage 1a. Makes the redirect texture again in another format (D3DFMT_UNKNOWN = the frame
	// buffer's own). FALSE, and the old texture kept, if it cannot be made or the scene is being redirected right now.
	static Bool remakeRenderTexture(D3DFORMAT format);
	static void drawViewport(Int color);	///<draws 2 triangles covering the current tactical viewport


protected:

	// Ronin @bugfix 16/11/2025: DX9 vertex declaration storage
	// Maps shader handles to their required vertex declarations
	static std::map<void*, IDirect3DVertexDeclaration9*> m_shaderDeclarations;

	static TextureClass *m_Textures[16];	///textures assigned to each of the possible stages
	static ChipsetType m_currentChipset;	///<last video card chipset that was detected.
	static GraphicsVenderID m_currentVendor;	///<last video card vendor
	static __int64 m_driverVersion;			///<driver version of last chipset.
	static ShaderTypes m_currentShader;	///<last shader that was set.
	static Int m_currentShaderPass;		///<pass of last shader that was set.

	static FilterTypes m_currentFilter; ///< Last filter that was set.
	// Info for a render to texture surface for special effects.
};

class W3DFilterInterface
{
public:
	virtual Int init() = 0;			///<perform any one time initialization and validation
	virtual Int shutdown() { return TRUE;};			///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) {skipRender=false; return false;} ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender){return false;} ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode){return false;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) = 0;		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset() = 0;
};


/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.
class ScreenMotionBlurFilter : public W3DFilterInterface
{
public:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override; ///< Called when the filter is started, one time before the first prerender.
	ScreenMotionBlurFilter();

	static void setZoomToPos(const Coord3D *pos) {m_zoomToPos = *pos; m_zoomToValid = true;}

protected:
	enum {MAX_COUNT = 60,
				MAX_LIMIT = 30,
				COUNT_STEP = 5,
				DEFAULT_PAN_FACTOR = 30};
	Int m_maxCount;
	Int m_lastFrame;
	Bool m_decrement;
	Bool m_skipRender;
	Bool m_additive;
	Bool m_doZoomTo;
	Coord2D m_priorDelta;
	Int m_panFactor;


	static Coord3D m_zoomToPos;
	static Bool m_zoomToValid;
} ;

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.
class ScreenBWFilter : public W3DFilterInterface
{
	IDirect3DPixelShader9* m_dwBWPixelShader;		///<D3D handle to pixel shader which tints texture to black & white. //Ronin @build DX9 Update
	IDirect3DVertexDeclaration9* m_dwBWVertexDecl;  // Ronin @bugfix 03/11/2025: Store vertex declaration for device reset recreation

public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
	static void setFadeParameters(Int fadeFrames, Int direction)
	{
		m_curFadeFrame = 0;
		m_fadeFrames = fadeFrames;
		m_fadeDirection = direction;
	}
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	static Int m_fadeFrames;
	static Int m_fadeDirection;
	static Int m_curFadeFrame;
	static Real m_curFadeValue;
};

class ScreenBWFilterDOT3 : public ScreenBWFilter
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
};

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades between 2 different rendered frames.
class ScreenCrossFadeFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
	static void setFadeParameters(Int fadeFrames, Int direction)
	{
		m_curFadeFrame = 0;
		m_fadeFrames = fadeFrames;
		m_fadeDirection = direction;
	}
	static Real getCurrentFadeValue()	{ return m_curFadeValue;}
	static TextureClass *getCurrentMaskTexture() { return m_fadePatternTexture;}
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Bool updateFadeLevel();		///<updated current state of fade and return true if not finished.
	static Int m_fadeFrames;
	static Int m_fadeDirection;
	static Int m_curFadeFrame;
	static Real m_curFadeValue;
	static Bool m_skipRender;
	static TextureClass *m_fadePatternTexture;	///<shape/pattern of the fade
};
