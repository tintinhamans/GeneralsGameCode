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

/* $Header: /Commando/Code/ww3d2/mesh.h 16    11/07/01 5:50p Jani_p $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando / G 3D engine                                       *
 *                                                                                             *
 *                    File Name : MESH.h                                                       *
 *                                                                                             *
 *                   Programmer : Greg Hjelstrom                                               *
 *                                                                                             *
 *                   Start Date : 06/11/97                                                     *
 *                                                                                             *
 *                  Last Update : June 11, 1997 [GH]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "WW3D2/rendobj.h"
#include "WWLib/bittype.h"
#include "WW3D2/w3derr.h"
#include "WW3D2/lightenvironment.h"	//added for 'Generals'

class MeshBuilderClass;
class HModelClass;
class AuxMeshDataClass;
class MeshLoadInfoClass;
class W3DMeshClass;
class MaterialInfoClass;
class ChunkLoadClass;
class ChunkSaveClass;
class RenderInfoClass;
class MeshModelClass;
class DecalMeshClass;
class MaterialPassClass;
class IndexBufferClass;
struct W3dMeshHeaderStruct;
struct W3dTexCoordStruct;
class TextureClass;
class VertexMaterialClass;
struct VertexFormatXYZNDUV2;

/**
** MeshClass -- Render3DObject for rendering meshes.
*/
class MeshClass : public RenderObjClass
{
	W3DMPO_CODE(MeshClass)
public:

	MeshClass();
	MeshClass(const MeshClass & src);
	MeshClass & operator = (const MeshClass &);
	virtual ~MeshClass() override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface
	/////////////////////////////////////////////////////////////////////////////
	virtual RenderObjClass *	Clone() const override;
	virtual int						Class_ID() const override { return CLASSID_MESH; }
	virtual const char *			Get_Name() const override;
	virtual void					Set_Name(const char * name) override;
	virtual int						Get_Num_Polys() const override;
	virtual void					Render(RenderInfoClass & rinfo) override;
	void								Render_Material_Pass(MaterialPassClass * pass,IndexBufferClass * ib);
	virtual void					Special_Render(SpecialRenderInfoClass & rinfo) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Collision Detection
	/////////////////////////////////////////////////////////////////////////////
	virtual bool					Cast_Ray(RayCollisionTestClass & raytest) override;
	virtual bool					Cast_AABox(AABoxCollisionTestClass & boxtest) override;
	virtual bool					Cast_OBBox(OBBoxCollisionTestClass & boxtest) override;
	virtual bool					Intersect_AABox(AABoxIntersectionTestClass & boxtest) override;
	virtual bool					Intersect_OBBox(OBBoxIntersectionTestClass & boxtest) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Bounding Volumes
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const override;
   virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & box) const override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Attributes, Options, Properties, etc
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Scale(float scale) override;
	virtual void					Scale(float scalex, float scaley, float scalez) override;
	virtual MaterialInfoClass * Get_Material_Info() override;

   virtual int						Get_Sort_Level() const override;
   virtual void					Set_Sort_Level(int level) override;


	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Decals
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Create_Decal(DecalGeneratorClass * generator) override;
	virtual void					Delete_Decal(uint32 decal_id) override;

	/////////////////////////////////////////////////////////////////////////////
	// MeshClass Interface
	/////////////////////////////////////////////////////////////////////////////
	WW3DErrorType					Init(const MeshBuilderClass & builder,MaterialInfoClass * matinfo,const char * name,const char * hmodelname);
	WW3DErrorType					Load_W3D(ChunkLoadClass & cload);
	void								Generate_Culling_Tree();
	MeshModelClass *				Get_Model();
	MeshModelClass *				Peek_Model();
	uint32							Get_W3D_Flags();
	const char *					Get_User_Text() const;
	int								Get_Draw_Call_Count() const;

	bool								Contains(const Vector3 &point);

	void								Get_Deformed_Vertices(Vector3 *dst_vert, Vector3 *dst_norm);
	void								Get_Deformed_Vertices(Vector3 *dst_vert);

	void								Set_Lighting_Environment(LightEnvironmentClass * light_env) { if (light_env) {m_localLightEnv=*light_env;LightEnvironment = &m_localLightEnv;} else {LightEnvironment = nullptr;} }
	LightEnvironmentClass *		Get_Lighting_Environment() { return LightEnvironment; }
	float	Get_Alpha_Override() { return m_alphaOverride;}

	void								Set_Next_Visible_Skin(MeshClass * next_visible) { NextVisibleSkin = next_visible; }
	MeshClass *						Peek_Next_Visible_Skin() { return NextVisibleSkin; }

	void								Set_Base_Vertex_Offset(int base) { BaseVertexOffset = base; }
	int								Get_Base_Vertex_Offset() { return BaseVertexOffset; }

	// Do old .w3d mesh files get fog turned on or off?
	static bool						Legacy_Meshes_Fogged;

	void								Replace_Texture(TextureClass* texture,TextureClass* new_texture);
	void								Replace_VertexMaterial(VertexMaterialClass* vmat,VertexMaterialClass* new_vmat);

	void								Make_Unique(bool force_meshmdl_clone = false);
	unsigned							Get_Debug_Id() const { return  MeshDebugId; }

	void								Set_Debugger_Disable(bool b) { IsDisabledByDebugger=b; }
	bool								Is_Disabled_By_Debugger() const { return IsDisabledByDebugger; }

	// Ronin @perf 20/08/2026 DX9: §29j.8. Per-mesh gate for the static shadow bake — a building's bulk
	// is baked while its flag or turret keeps drawing normally.
	void								Set_Baked_Shadow_Caster(bool b) { BakedShadowCaster = b; }
	bool								Is_Baked_Shadow_Caster() const { return BakedShadowCaster; }
	// Sticky: a mesh proven to move is never baked again. The bit dies with the mesh, so no list.
	void								Set_Shadow_Caster_Mover(bool b) { ShadowCasterMover = b; }
	bool								Is_Shadow_Caster_Mover() const { return ShadowCasterMover; }
	static void						Skip_Baked_Shadow_Casters(bool b) { s_SkipBakedShadowCasters = b; }
	static bool						s_SkipBakedShadowCasters;
	// Ronin @bugfix 19/09/2026 DX9: a mover draws in the depth pass every frame, but the moving-caster mask is marked per
	// OBJECT and a flag's parent structure is a static caster — so the terrain EMA kept history under a shadow that moves,
	// and the flag/dish/turret trailed. The mesh marks itself instead. Set by R3DShadowMap; WW3D2 must not know about it.
	typedef void					(*ShadowMoverNoteFunc)(float cx, float cy, float cz, float radius);
	static void						Set_Shadow_Mover_Note_Func(ShadowMoverNoteFunc fn) { s_ShadowMoverNote = fn; }
	static ShadowMoverNoteFunc		s_ShadowMoverNote;
	// Ronin @bugfix 19/09/2026 DX9: TRUE when this mesh's last base draw sampled the shroud in its own pixel shader (the
	// programmable rigid paths). Such a mesh must NOT also take the fixed-function shroud overlay, or it darkens twice.
	void								Set_Shroud_Applied_In_Shader(bool b) { ShroudAppliedInShader = b; }
	bool								Is_Shroud_Applied_In_Shader() const { return ShroudAppliedInShader; }

	// Ronin @perf 26/08/2026 DX9: §29i.5. TRUE only inside the shadow depth-pass scene render.
	// Separate from the flag above on purpose: that one means "skip baked casters", this one means
	// "colour writes are off", and rigid instancing eligibility keys on the latter.
	static void						Set_In_Shadow_Depth_Pass(bool b) { s_InShadowDepthPass = b; }
	static bool						s_InShadowDepthPass;

	// Ronin @feature 24/09/2026 DX9: TAA motion vectors. Every mesh drawn outside the shadow depth pass reports
	// itself; TAA keeps its previous world transform and re-draws the ones that moved. docs/AntiAliasing_Work.md §14.
	typedef void					(*TaaMeshNoteFunc)(MeshClass *mesh);
	static void						Set_Taa_Mesh_Note_Func(TaaMeshNoteFunc fn) { s_TaaMeshNote = fn; }
	static TaaMeshNoteFunc			s_TaaMeshNote;

	// Ronin @feature 09/10/2026 DX9: lighting part 2 - a mesh that only looks like a light (a beam) reports itself. The callback
	// returns 0 = not one (kept in LightFixture, never asked again), 1 = one, draw it, 2 = one a real light replaces: skip.
	typedef int						(*LightFixtureNoteFunc)(MeshClass *mesh);
	static void						Set_Light_Fixture_Note_Func(LightFixtureNoteFunc fn) { s_LightFixtureNote = fn; }
	static LightFixtureNoteFunc	s_LightFixtureNote;
	unsigned char					Get_Light_Fixture() const { return LightFixture; }

	// Ronin @feature 11/10/2026 DX9: lamp shadow maps - every mesh that is drawn reports itself, so R3DLocalLights knows what stands in
	// each lamp's light (docs/Lighting_Work.md 6b). The callback itself sorts out which pass this is.
	typedef void					(*LightCasterNoteFunc)(MeshClass *mesh);
	static void						Set_Light_Caster_Note_Func(LightCasterNoteFunc fn) { s_LightCasterNote = fn; }
	static LightCasterNoteFunc	s_LightCasterNote;

protected:

	virtual void					Add_Dependencies_To_List (DynamicVectorClass<StringClass> &file_list, bool textures_only = false) override;

	virtual void					Update_Cached_Bounding_Volumes() const override;

	void								Free();

	void								install_materials(MeshLoadInfoClass * loadinfo);
	void								clone_materials(const MeshClass & srcmesh);

	MeshModelClass *				Model;
	bool								BakedShadowCaster;		// §29j.8 — in the static shadow bake
	bool								ShadowCasterMover;		// §29j.8 — proven to move, never bake
	bool								ShroudAppliedInShader;	// last base draw sampled the shroud in its own PS
	unsigned char					LightFixture = 0;		// Ronin @feature 09/10/2026 DX9: 0 not asked yet, 1 no light fixture, 2 one
	DecalMeshClass *				DecalMesh;

	LightEnvironmentClass *		LightEnvironment;		// cached pointer to the light environment for this mesh
	LightEnvironmentClass     m_localLightEnv;	//added for 'Generals'
	float					m_alphaOverride;	//added for 'Generals' to allow variable alpha on meshes.
	float					m_materialPassEmissiveOverride;	//added for 'Generals' to allow variable emissive on additional passes.
	float					m_materialPassAlphaOverride;	//added for 'Generals' to allow variable alpha on additional render passes.
	int								BaseVertexOffset;		// offset to our first vertex in whatever vb this mesh is in.
	MeshClass *						NextVisibleSkin;		// linked list of visible skins

	unsigned							MeshDebugId;
	bool								IsDisabledByDebugger;

	friend class MeshBuilderClass;
};

inline MeshModelClass * MeshClass::Peek_Model()
{
	return Model;
}


// This utility function recurses throughout the subobjects of a renderobject,
// and for each MeshClass it finds it sets the given MeshModel flag on its
// model. This is useful for stuff like making a RenderObjects' polys sort.
//void Set_MeshModel_Flag(RenderObjClass *robj, MeshModelClass::FlagsType flag, int onoff);
void Set_MeshModel_Flag(RenderObjClass *robj, int flag, int onoff);
