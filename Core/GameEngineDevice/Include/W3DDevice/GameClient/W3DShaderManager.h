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

#include "WW3D2/dx8compat.h"
#include "WW3D2/texture.h"
#include "WWMath/vector4.h"
#include <vector>
enum FilterTypes CPP_11(: Int);
enum FilterModes CPP_11(: Int);
enum CustomScenePassModes CPP_11(: Int);
enum StaticGameLODLevel CPP_11(: Int);
enum ChipsetType CPP_11(: Int);
enum GraphicsVenderID CPP_11(: Int);

class TextureClass;	///forward reference
class MaterialPassClass;	///forward reference
class AABoxClass;	///forward reference
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
		ST_SHADOW_DEPTH,		//shader to write caster depth into the shadow map.
		ST_SHADOW_MULTIPLY,		//second pass multiplying the shadow map into drawn geometry.
		ST_SPECULAR,			//second pass adding a per-pixel sun highlight to drawn geometry.
		ST_POINT_LIGHTS,		//second pass adding dynamic point lights to geometry lit without them.
		ST_MAX
	};


	W3DShaderManager();	///<constructor
	static void init();	///<determine optimal shaders for current device.
	static void shutdown();	///<release resources used by shaders
	static void updateCloud();	///<update the cloud position once every render frame.

	static ChipsetType getChipset();	///<return current device chipset.
	static GraphicsVenderID getCurrentVendor() {return m_currentVendor;}	///<return current card vendor.
	static __int64 getCurrentDriverVersion() {return m_driverVersion; }	///<return current driver version.
	static Int getShaderPasses(ShaderTypes shader);	///<rendering passes required for shader
	static Int setShader(ShaderTypes shader, Int pass);	///<enable specific shader pass.
	static Int setShroudTex(Int stage);	///<Set shroud in a texture stage.
	static void resetShader(ShaderTypes shader);	///<make sure W3D2 gets restored to normal
	/// The shader texture slot the terrain's normal atlas goes in.
	enum { TERRAIN_NORMAL_TEXTURE = 4 };
	/// The shader texture slot the terrain's height atlas goes in, or null for the legacy blend.
	enum { TERRAIN_HEIGHT_TEXTURE = 5 };
#if defined(RTS_ZEROHOUR)
	enum { TERRAIN_EMISSIVE_TEXTURE = 6 };
	static void setTerrainEmissiveScale(Real scale);
	static Bool setTerrainEmissive();
#endif
	/// ST_SHADOW_MULTIPLY pass that also multiplies in the cloud map, for receivers without their own clouds.
	enum { SHADOW_MULTIPLY_PASS_CLOUDS = 1 };
	///Specify all textures (up to 8) which can be accessed by the shaders.
	static void setTexture(Int stage,TextureClass* texture) {m_Textures[stage]=texture;}
	///Return current texture available to shaders.
	static TextureClass *getShaderTexture(Int stage) { return m_Textures[stage];}	///<returns currently selected texture for given stage
	///Return last activated shader.
	static ShaderTypes getCurrentShader() {return m_currentShader;}
	/// Loads a .vso file and creates a vertex shader for it
	static HRESULT LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, DWORD* pHandle);

	/// Sets the sun the specular pass lights with, once a frame. toSun is in world space.
	/// debug tints what the pass covers and shows the highlight 8x in magenta.
	static void setSpecularLight(const Vector3 &toSun, const Vector3 &color, Real intensity, Real power, Bool debug);
	/// Sets the bump detail the specular pass shades, once a frame. height is the rise, in world
	/// units, of full brightness on textures without a normal map, and 0 leaves them flat.
	static void setSurfaceBumps(Bool enabled, const Vector3 &ambient, Real height, Real normalMapStrength);
	/// Sets how brightly the specular pass adds _emi glow masks, once a frame. 0 turns them off.
	static void setEmissive(Real intensity);

	/// A dynamic point light the shaders add per pixel, in world space.
	struct PixelLight
	{
		PixelLight() : position(0.0f, 0.0f, 0.0f), innerRadius(0.0f), outerRadius(0.0f),
			diffuse(0.0f, 0.0f, 0.0f), ambientScale(0.0f), terrainOnly(FALSE), objectsOnly(FALSE),
			splitAtGround(FALSE), groundHeight(0.0f) {}
		Vector3 position;
		Real innerRadius;	///< full strength inside this
		Real outerRadius;	///< nothing past this
		Vector3 diffuse;
		Real ambientScale;	///< ambient colour as a fraction of the diffuse
		Bool terrainOnly;	///< lights the ground and nothing standing on it
		Bool objectsOnly;	///< the ground gets its light from a laser's terrain pass or a fitted ground light
		Bool splitAtGround;	///< the depth pass can part ground from objects at groundHeight
		Real groundHeight;
	};
	/// Each lighting pass takes eight lights, or two under standing water.
	enum { MAX_PIXEL_LIGHTS = 8, SEABED_PIXEL_LIGHTS = 2, MAX_UNIT_PIXEL_LIGHTS = 8, MAX_PIXEL_LIGHT_CANDIDATES = 64 };
	/// Sets the lights that may be drawn per pixel this frame, most important first. Draws name theirs by index.
	static void setPixelLights(const PixelLight *lights, Int count);
	static Int getPixelLightCount();
	static const PixelLight &getPixelLight(Int index);
	/// Lists in rising order every light that could reach a ground rectangle, for the caller to test against its own shape.
	static void gatherPixelLights(Real minX, Real minY, Real maxX, Real maxY, std::vector<Int> &lights);
	/// Lights the draws that follow in batches under the terrain, road, flat terrain
	/// or point light shader in use. Null indices take the first count except object-only lights.
	static void setDrawPixelLights(const Int *indices, Int count);
	/// The registers the terrain's seabed hex tiling reads, as terrainshadow.hlsl lays them out.
	enum { SEABED_CONSTANTS = 5 };
	/// Sets, once a frame, the atlas slot lookup, standing water mask, painted stochastic terrain and
	/// SEABED_CONSTANTS registers the terrain's seabed hex tiling reads, or turns it off with nulls.
	static void setTerrainSeabed(TextureClass *classMap, TextureClass *waterMask, TextureClass *painted, const Vector4 *constants);
	/// Whether the terrain can hex-tile its textures under standing water.
	static Bool supportsTerrainSeabed();
	/// Lights a terrain draw as setDrawPixelLights does, through the seabed shaders when it has standing water.
	static void setDrawTerrain(const Int *indices, Int count, Bool seabed);
	/// The first lights, nearest the middle of the view, that reach the box, as many as one ground draw takes.
	static Int pickPixelLights(const AABoxClass &box, Int *lights);
	static Int pickPixelLights(const AABoxClass &box, std::vector<Int> &lights);
	static void filterPixelLights(const AABoxClass &box, std::vector<Int> &lights);
	/// Whether any surface draws point lights per pixel, so the scene has to pick them.
	static Bool supportsPixelLights();
	/// Whether the terrain draws point lights per pixel, so lights handed over must leave its vertex lighting.
	static Bool supportsTerrainPixelLights();
	/// Whether the specular pass draws point lights per pixel, so its meshes must go without fixed-function ones.
	static Bool supportsUnitPixelLights();
	/// Sets whether the terrain shaders read the normal atlas in TERRAIN_NORMAL_TEXTURE, and how strongly.
	/// debug shows only the bump's shading, on grey. roadHeight is the rise, in world units, of full
	/// brightness on road textures without a normal map, and 0 leaves them flat.
	static void setTerrainBumps(Bool enabled, Real strength, Bool debug, Real roadHeight);
	/// Whether the terrain shaders will read the normal atlas, so the terrain should build and hand it over.
	static Bool wantsTerrainNormalAtlas();
	/// Sets the sun's glint on the ground, once a frame. gloss sharpens it, and albedo is how far it
	/// follows the ground's brightness, from 0 not at all to 1 fully. An intensity of 0 turns it off.
	static void setTerrainGlint(Bool enabled, Real intensity, Real gloss, Real albedo);
	/// Whether the terrain and road shaders will glint, so the terrain should hand over its normals.
	static Bool wantsTerrainGlint();
	/// The gloss of terrain textures without their own.
	static Real getTerrainGlintGloss();
	/// Sets, once a frame, the glint's normals with their world xy mapping and getTerrainGlintMap's materials and scales, or turns it off with null.
	static void setTerrainGlintMaps(TextureClass *normals, const Vector4 &mapping, TextureClass *materials, Real strengthScale, Real glossScale);
	/// Sets a terrain stage's filters from the player's anisotropy or the mod's settings, whose mip is linear with bilinearMipLinear.
	static void setTerrainTextureFilter(Int stage, Bool bilinearMipLinear);
	/// Whether the terrain shaders can blend by height, so the terrain should hand over its height atlas.
	static Bool supportsTerrainHeightBlend();
	/// Sets whether road shader draws are blend tiles, which blend by the heights in TERRAIN_HEIGHT_TEXTURE.
	static void setRoadHeightBlend(Bool blendTiles);
	/// How many terrain draws used the normal atlas since the last call.
	static Int takeTerrainBumpCount();
	/// How many road and blend tile draws read a normal map since the last call.
	static Int takeRoadBumpCount();
	/// How many mesh draws the specular pass ran on, and how many polygon groups of those it
	/// bumped from brightness or from a normal map, or lit with a glow mask, since the last call.
	static void takeSpecularCounts(Int &meshes, Int &derived, Int &normalMapped, Int &emissive);
	/// The pass objects push for a per-pixel sun highlight, bumps and glow, or null when all are off or unsupported.
	static MaterialPassClass *getSpecularPass();
	/// The same pass for one object this frame, also adding the given lights. lightsOnly leaves out the
	/// highlight, bumps and glow, and gives null without lights. receivesShadow asks it to do the shadow
	/// receiver pass's work as well, which only canSpecularReceiveShadow allows, and comes back true where it does.
	static MaterialPassClass *getSpecularPass(const Int *lights, Int lightCount, Bool lightsOnly, Bool &receivesShadow);
	/// Whether the specular pass can take in the shadow receiver's work: its shaders loaded and the map's shadow colour grey.
	static Bool canSpecularReceiveShadow();
	/// The pass every specular pass above shares its vertex processing with, or null when it is unsupported.
	static const MaterialPassClass *getSpecularPassKey();
	/// Binds the texture, and its normal map and glow mask, for geometry drawn through ST_SPECULAR outside the mesh renderer.
	static void setSpecularTexture(TextureClass *texture);
	/// Overrides, until ST_SPECULAR is next set, the rise of full brightness on textures without a normal map.
	static void setSpecularBumpHeight(Real height);
	/// Whether the device runs ps_2_a shaders, which have gradients and 512 instruction slots.
	static Bool supportsPixelShader2a();
	/// The <name>_nrm.dds beside a texture, or null when there is none. Not reference counted.
	static TextureClass *findNormalMap(TextureClass *texture);
	static TextureClass *findEmissiveMap(TextureClass *texture);

	static Bool testMinimumRequirements(ChipsetType *videoChipType, MemValueType *numRAM);
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
	/// Copies the bound render target into copy, resolving multisampling and recreating copy when the target changes shape.
	/// An area copies those target pixels alone, and leaves the rest of copy as it was.
	static Bool copyRenderTarget(IDirect3DTexture8 *&copy, const RECT *area = nullptr);
	/// Scale in xy and offset in zw from clip space to the texel centres of a width by height copy of the render target.
	static Vector4 getClipToTargetMapping(Real width, Real height);
	/// Target pixels per screen pixel, above 1 while the scene renders larger than the screen for supersampling.
	static Real getScreenToTargetScale();
	/// Draws a quad over the viewport through identity transforms, its uv mapped from clip space by clipToTarget.
	static void drawClipQuad(const Vector4 &clipToTarget);
	/// Binds the terrain's cloud map to stage and gives the mapping from camera space onto it, where objects take cloud shadows.
	static Bool bindCloudMap(Int stage, D3DMATRIX &cameraToCloud);
	static Bool isRenderingToTexture() {return m_renderingToTexture; }
	static void drawViewport(Int color);	///<draws 2 triangles covering the current tactical viewport


protected:
	static TextureClass *m_Textures[8];	///textures assigned to each of the possible stages
	static ChipsetType m_currentChipset;	///<last video card chipset that was detected.
	static GraphicsVenderID m_currentVendor;	///<last video card vendor
	static __int64 m_driverVersion;			///<driver version of last chipset.
	static ShaderTypes m_currentShader;	///<last shader that was set.
	static Int m_currentShaderPass;		///<pass of last shader that was set.

	static FilterTypes m_currentFilter; ///< Last filter that was set.
	// Info for a render to texture surface for special effects.
	static Bool m_renderingToTexture;
	static IDirect3DSurface8 *m_oldRenderSurface;	///<previous render target
	static IDirect3DTexture8 *m_renderTexture;		///<texture into which rendering will be redirected.
	static IDirect3DSurface8 *m_newRenderSurface;	///<new render target inside m_renderTexture
	static IDirect3DSurface8 *m_oldDepthSurface;	///<previous depth buffer surface


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
	DWORD	m_dwBWPixelShader;		///<D3D handle to pixel shader which tints texture to black & white.
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
