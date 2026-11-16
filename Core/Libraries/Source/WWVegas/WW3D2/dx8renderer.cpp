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
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8renderer.cpp                        $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 111                                                         $*
 *                                                                                             *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define ENABLE_CATEGORY_LOG
//#define ENABLE_STRIPING

#include "dx8renderer.h"
#include "dx8wrapper.h"
#include "dx8polygonrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "dx8caps.h"
#include "dx8rendererdebugger.h"
#include "dx8instancing.h"
#include "dx8skinning.h"
#include "WWDebug/wwdebug.h"
#include "WWDebug/wwprofile.h"
#include "WWDebug/wwmemlog.h"
#include "rinfo.h"
#include "statistics.h"
#include "meshmdl.h"
#include "WWMath/vp.h"
#include "decalmsh.h"
#include "matpass.h"
#include "camera.h"
#include "stripoptimizer.h"
#include "meshgeometry.h"
#include "lightenvironment.h"
#include <algorithm>
#include <vector>
#include <stdlib.h>

/*
** Global Instance of the DX8MeshRender
*/
DX8MeshRendererClass TheDX8MeshRenderer;
bool DX8TextureCategoryClass::m_gForceMultiply = false; // Forces opaque materials to use the multiply blend - pseudo transparent effect.  jba.
// ----------------------------------------------------------------------------

static DynamicVectorClass<Vector3>				_TempVertexBuffer;
static DynamicVectorClass<Vector3>				_TempNormalBuffer;

static MultiListClass<MeshModelClass>			_RegisteredMeshList;
static TextureCategoryList							texture_category_delete_list;
static FVFCategoryList								fvf_category_container_delete_list;

// Meshes with a material pass that cannot be instanced. Its depth test of EQUAL needs the base
// pass drawn the same way, so these meshes draw fixed function throughout.
static std::vector<MeshClass *>					_FixedFunctionPassMeshes;
static bool												_FixedFunctionPassMeshesSorted = true;

// Base pass fragments drawn instanced in the current flush, whose material passes must be too.
typedef std::pair<MeshClass *, DX8PolygonRendererClass *> InstancedFragment;
static std::vector<InstancedFragment>			_InstancedFragments;
static bool												_InstancedFragmentsSorted = true;

static bool Has_Fixed_Function_Pass(MeshClass * mesh)
{
	if (!_FixedFunctionPassMeshesSorted)
	{
		std::sort(_FixedFunctionPassMeshes.begin(), _FixedFunctionPassMeshes.end());
		_FixedFunctionPassMeshesSorted = true;
	}
	return std::binary_search(_FixedFunctionPassMeshes.begin(), _FixedFunctionPassMeshes.end(), mesh);
}

static bool Is_Instanced_Fragment(MeshClass * mesh, DX8PolygonRendererClass * renderer)
{
	if (!_InstancedFragmentsSorted)
	{
		std::sort(_InstancedFragments.begin(), _InstancedFragments.end());
		_InstancedFragmentsSorted = true;
	}
	return std::binary_search(_InstancedFragments.begin(), _InstancedFragments.end(), InstancedFragment(mesh, renderer));
}

// helper data structure
class PolyRemover : public MultiListObjectClass
{
public:
	DX8TextureCategoryClass *	src;
	DX8TextureCategoryClass *	dest;
	DX8PolygonRendererClass *  pr;
};

typedef MultiListClass<PolyRemover>			PolyRemoverList;
typedef MultiListIterator<PolyRemover>		PolyRemoverListIterator;

#define VERTEX_BUFFER_OVERFLOW	0xffff		//'Generals' flag to signal when a mesh didn't fit in streaming vertex buffer.

/**
** PolyRenderTaskClass
** This is a record of a polyrendere that needs to be rendered
** for this frame.  Since MeshClass instances can share meshmodels
** (and therefore their dx8 polygon renderers) this record contains
** a pointer to the polygon renderer and the MeshClass instance that
** it is being rendered for.
*/
class PolyRenderTaskClass : public AutoPoolClass<PolyRenderTaskClass, 256>
{
public:
	PolyRenderTaskClass(DX8PolygonRendererClass * p_renderer,MeshClass * p_mesh) :
		Renderer(p_renderer),
		Mesh(p_mesh),
		NextVisible(nullptr)
	{
		WWASSERT(Renderer != nullptr);
		WWASSERT(Mesh != nullptr);
		Mesh->Add_Ref();
	}

	~PolyRenderTaskClass()
	{
		Mesh->Release_Ref();
	}

	DX8PolygonRendererClass *	Peek_Polygon_Renderer()							{ return Renderer; }
	MeshClass *						Peek_Mesh()											{ return Mesh; }

	PolyRenderTaskClass *		Get_Next_Visible()									{ return NextVisible; }
	void								Set_Next_Visible(PolyRenderTaskClass * prtc)		{ NextVisible = prtc; }

protected:

	DX8PolygonRendererClass *	Renderer;
	MeshClass *						Mesh;
	PolyRenderTaskClass *		NextVisible;

};

DEFINE_AUTO_POOL(PolyRenderTaskClass, 256);

/**
** MatPassTaskClass
** This is the record of a material pass that needs to be rendered on
** a particular mesh.  These are linked into the FVF container which
** contains the mesh model.  They are also pooled to remove memory
** allocation overhead.
*/
class MatPassTaskClass : public AutoPoolClass<MatPassTaskClass, 256>
{
public:
	MatPassTaskClass(MaterialPassClass * pass,MeshClass * mesh) :
		MaterialPass(pass),
		Mesh(mesh),
		NextVisible(nullptr)
	{
		WWASSERT(MaterialPass != nullptr);
		WWASSERT(Mesh != nullptr);
		MaterialPass->Add_Ref();
		Mesh->Add_Ref();
	}

	~MatPassTaskClass()
	{
		MaterialPass->Release_Ref();
		Mesh->Release_Ref();
	}

	MaterialPassClass *	Peek_Material_Pass()							{ return MaterialPass; }
	MeshClass *				Peek_Mesh()										{ return Mesh; }

	MatPassTaskClass *	Get_Next_Visible()								{ return NextVisible; }
	void						Set_Next_Visible(MatPassTaskClass * mpr)		{ NextVisible = mpr; }

private:

	MaterialPassClass *	MaterialPass;
	MeshClass *				Mesh;
	MatPassTaskClass *	NextVisible;
};

DEFINE_AUTO_POOL(MatPassTaskClass, 256);


// ----------------------------------------------------------------------------


inline static bool Equal_Material(const VertexMaterialClass* mat1,const VertexMaterialClass* mat2)
{
	int crc0 = mat1 ? mat1->Get_CRC() : 0;
	int crc1 = mat2 ? mat2->Get_CRC() : 0;
	return (crc0 == crc1);
}


DX8TextureCategoryClass::DX8TextureCategoryClass(
	DX8FVFCategoryContainer* container_,
	TextureClass** texs,
	ShaderClass shd,
	VertexMaterialClass* mat,
	int pass_)
	:
	pass(pass_),
	shader(shd),
	render_task_head(nullptr),
	bloom_task_head(nullptr),
	material(mat),
	container(container_)
{
	WWASSERT(pass>=0);
	WWASSERT(pass<DX8FVFCategoryContainer::MAX_PASSES);

	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a)
	{
		textures[a]=nullptr;
		REF_PTR_SET(textures[a],texs[a]);
	}

	if (material) material->Add_Ref();
}

DX8TextureCategoryClass::~DX8TextureCategoryClass()
{
	// Unregistering the mesh where polygon renderers are connected to kills all polygon renderers
	while (DX8PolygonRendererClass* p_renderer=PolygonRendererList.Get_Head()) {
		TheDX8MeshRenderer.Unregister_Mesh_Type(p_renderer->Get_Mesh_Model_Class());
	}
	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a)
	{
		REF_PTR_RELEASE(textures[a]);
	}

	REF_PTR_RELEASE(material);

	DEBUG_ASSERTCRASH(render_task_head == nullptr, ("~DX8TextureCategoryClass: Leaking render tasks"));
	DEBUG_ASSERTCRASH(bloom_task_head == nullptr, ("~DX8TextureCategoryClass: Leaking bloom tasks"));
}

// W3D "Add" blending, with or without alpha, is what the bloom pass glows
bool DX8TextureCategoryClass::Is_Additive() const
{
	if (shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ONE)
	{
		return false;
	}
	return shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE || shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA;
}

// A glow mask on the texture replays the category into the bloom target, drawing the mask alone
bool DX8TextureCategoryClass::Is_Emissive_Glow() const
{
	return DX8MeshRendererClass::Get_Bloom_Emissive_Intensity() > 0.0f && textures[0] != nullptr &&
		textures[0]->Peek_Emissive_Map() != nullptr && !Is_Additive();
}

void DX8TextureCategoryClass::Add_Render_Task(DX8PolygonRendererClass * p_renderer,MeshClass * p_mesh)
{
	PolyRenderTaskClass * new_prt = new PolyRenderTaskClass(p_renderer,p_mesh);
	new_prt->Set_Next_Visible(render_task_head);
	render_task_head = new_prt;

	container->Add_Visible_Texture_Category(this,pass);
}

void DX8TextureCategoryClass::Add_Polygon_Renderer(DX8PolygonRendererClass* p_renderer,DX8PolygonRendererClass* add_after_this)
{
	WWASSERT(p_renderer!=nullptr);
	WWASSERT(!PolygonRendererList.Contains(p_renderer));

	if (add_after_this != nullptr) {
		bool res = PolygonRendererList.Add_After(p_renderer,add_after_this,false);
		WWASSERT(res);
	} else {
		PolygonRendererList.Add(p_renderer);
	}

	p_renderer->Set_Texture_Category(this);
}

void DX8TextureCategoryClass::Remove_Polygon_Renderer(DX8PolygonRendererClass* p_renderer)
{
	PolygonRendererList.Remove(p_renderer);
	p_renderer->Set_Texture_Category(nullptr);
	if (PolygonRendererList.Peek_Head() == nullptr) {
		container->Remove_Texture_Category(this);
		texture_category_delete_list.Add_Tail(this);
	}
}


void DX8FVFCategoryContainer::Remove_Texture_Category(DX8TextureCategoryClass* tex_category)
{
	unsigned pass=0;
	for (;pass<passes;++pass) {
		texture_category_list[pass].Remove(tex_category);
	}
	for (pass=0; pass<passes; pass++) {
		// If any of the texture category lists has anything in it, no need to delete this container
		if (texture_category_list[pass].Peek_Head() != nullptr) return;
	}
	fvf_category_container_delete_list.Add_Tail(this);
}

// Per-polygon culling draws fixed function, so a culled pass cannot follow an instanced base.
static bool Is_Window_Pass(MaterialPassClass * pass)
{
	return DX8InstancingClass::Is_Instanced_Material_Pass(pass) && (pass->Get_Cull_Volume() == nullptr || !MaterialPassClass::Is_Per_Polygon_Culling_Enabled());
}

void DX8FVFCategoryContainer::Add_Visible_Material_Pass(MaterialPassClass * pass,MeshClass * mesh)
{
	if (!Is_Window_Pass(pass))
	{
		_FixedFunctionPassMeshes.push_back(mesh);
		_FixedFunctionPassMeshesSorted = false;
	}

	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (visible_matpass_head == nullptr) {
		WWASSERT(visible_matpass_tail == nullptr);
		visible_matpass_head = new_mpr;
	} else {
		WWASSERT(visible_matpass_tail != nullptr);
		visible_matpass_tail->Set_Next_Visible(new_mpr);
	}

	visible_matpass_tail = new_mpr;
	AnythingToRender=true;
}

static std::vector<MatPassTaskClass *>			_MaterialPassWindow;

static bool Fragment_Renderer_Less(const InstancedFragment & a, const InstancedFragment & b)
{
	return (a.second != b.second) ? (a.second < b.second) : (a.first < b.first);
}

// A whitelisted pass on a mesh drawn with its own transform can join a window. Anything else is
// drawn by the mesh as before, after the window it would otherwise overtake.
static bool Allows_Material_Pass_Window(MaterialPassClass * pass, MeshClass * mesh)
{
	if (!Is_Window_Pass(pass) || mesh->Has_Material_Pass_Override())
	{
		return false;
	}
	return !mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN);
}

// Why Allows_Material_Pass_Window turned a pass away, for the render log.
static int Lone_Pass_Path(MaterialPassClass * pass, MeshClass * mesh)
{
	if (!Is_Window_Pass(pass))
	{
		return DX8InstancingStatsStruct::PASS_PATH_NOT_WINDOW_PASS;
	}
	if (mesh->Has_Material_Pass_Override())
	{
		return DX8InstancingStatsStruct::PASS_PATH_OVERRIDE;
	}
	return DX8InstancingStatsStruct::PASS_PATH_SKIN;
}

void DX8FVFCategoryContainer::Render_Instanced_Material_Passes()
{
	MatPassTaskClass * mpr = visible_matpass_head;
	MatPassTaskClass * last_mpr = nullptr;
	bool renderTasksRemaining=false;

	while (mpr != nullptr) {
		MeshClass * mesh = mpr->Peek_Mesh();

		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)
		{
			last_mpr = mpr;
			mpr = mpr->Get_Next_Visible();
			renderTasksRemaining = true;
			continue;
		}

		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();
		if (last_mpr == nullptr) {
			visible_matpass_head = next_mpr;
		} else {
			last_mpr->Set_Next_Visible(next_mpr);
		}

		if (Allows_Material_Pass_Window(mpr->Peek_Material_Pass(), mesh))
		{
			DX8MeshRendererClass::Record_Material_Pass_Path(DX8InstancingStatsStruct::PASS_PATH_WINDOW);
			_MaterialPassWindow.push_back(mpr);
		}
		else
		{
			DX8MeshRendererClass::Record_Material_Pass_Path(Lone_Pass_Path(mpr->Peek_Material_Pass(), mesh));
			Render_Material_Pass_Window();
			mesh->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
			delete mpr;
		}
		mpr = next_mpr;
	}
	Render_Material_Pass_Window();

	visible_matpass_tail = renderTasksRemaining ? last_mpr : nullptr;
}

// Draws the window one pass at a time, in the order the passes first appear, which keeps each
// mesh's own passes in order. A fragment whose base pass was instanced is instanced too.
void DX8FVFCategoryContainer::Render_Material_Pass_Window()
{
	std::vector<MatPassTaskClass *> & window = _MaterialPassWindow;
	if (window.empty())
	{
		return;
	}
	DX8MeshRendererClass::Record_Material_Pass_Window();

	static std::vector<MaterialPassClass *> passes;
	static std::vector<InstancedFragment> instanced;
	static std::vector<InstancedFragment> fixed;
	static std::vector<MeshClass *> meshes;
	static std::vector<DX8PolygonRendererClass *> renderers;
	static std::vector<int> counts;

	for (size_t i=0;i<window.size();++i)
	{
		MaterialPassClass * pass = window[i]->Peek_Material_Pass();
		if (std::find(passes.begin(), passes.end(), pass) == passes.end())
		{
			passes.push_back(pass);
		}
	}

	for (size_t p=0;p<passes.size();++p)
	{
		MaterialPassClass * pass = passes[p];
		for (size_t i=0;i<window.size();++i)
		{
			if (window[i]->Peek_Material_Pass() != pass)
			{
				continue;
			}
			MeshClass * mesh = window[i]->Peek_Mesh();
			DX8PolygonRendererListIterator it(&mesh->Peek_Model()->PolygonRendererList);
			for (;!it.Is_Done();it.Next())
			{
				DX8PolygonRendererClass * renderer = it.Peek_Obj();
				if (renderer->Get_Pass() == 0)
				{
					InstancedFragment fragment(mesh, renderer);
					(Is_Instanced_Fragment(mesh, renderer) ? instanced : fixed).push_back(fragment);
				}
			}
		}

		pass->Install_Materials();
		DX8Wrapper::Set_Index_Buffer(index_buffer,0);

		int instanced_calls = 0;
		if (!instanced.empty())
		{
			std::sort(instanced.begin(), instanced.end(), Fragment_Renderer_Less);
			size_t start = 0;
			for (size_t i=1;i<=instanced.size();++i)
			{
				if (i == instanced.size() || instanced[i].second != instanced[start].second)
				{
					renderers.push_back(instanced[start].second);
					counts.push_back((int)(i - start));
					start = i;
				}
				meshes.push_back(instanced[i - 1].first);
			}

			// A pass spans every category of the container, so its groups go in batches one call can take.
			size_t group = 0;
			size_t first = 0;
			while (group < renderers.size())
			{
				size_t end = group;
				int batch = 0;
				while (end < renderers.size() && (end == group || batch + counts[end] <= DX8InstancingClass::MAX_INSTANCES))
				{
					batch += counts[end];
					++end;
				}
				if (DX8InstancingClass::Draw_Material_Pass_Groups(pass, &renderers[group], &counts[group], (int)(end - group), &meshes[first], FVF))
				{
					instanced_calls += (int)(end - group);
				}
				else
				{
					fixed.insert(fixed.end(), instanced.begin() + first, instanced.begin() + first + batch);
				}
				first += batch;
				group = end;
			}
		}

		// The window's passes take no lighting, and setting a mesh's light environment here would mark the
		// material changed when its ambient differs, which reapplies it and drops the pass's texgen.
		for (size_t i=0;i<fixed.size();++i)
		{
			MeshClass * mesh = fixed[i].first;
			DX8Wrapper::Set_Transform(D3DTS_WORLD,mesh->Get_Transform());
			pass->Install_Polygon_Materials(fixed[i].second, mesh);
			fixed[i].second->Render(mesh->Get_Base_Vertex_Offset());
		}

		pass->UnInstall_Materials();
		DX8MeshRendererClass::Record_Material_Pass(pass, instanced_calls + (int)fixed.size());

		instanced.clear();
		fixed.clear();
		meshes.clear();
		renderers.clear();
		counts.clear();
	}

	for (size_t i=0;i<window.size();++i)
	{
		delete window[i];
	}
	window.clear();
	passes.clear();
}

void DX8FVFCategoryContainer::Render_Procedural_Material_Passes()
{
	if (DX8InstancingClass::Get_Pass() == DX8InstancingClass::PASS_LIT && !sorting && Bind_Static_Buffers())
	{
		Render_Instanced_Material_Passes();
		return;
	}
	const int path = (DX8InstancingClass::Get_Pass() != DX8InstancingClass::PASS_LIT) ? DX8InstancingStatsStruct::PASS_PATH_NOT_LIT
		: (sorting ? DX8InstancingStatsStruct::PASS_PATH_SORTING : DX8InstancingStatsStruct::PASS_PATH_UNBOUND);

	// additional passes
	MatPassTaskClass * mpr = visible_matpass_head;
	MatPassTaskClass * last_mpr = nullptr;
   	bool renderTasksRemaining=false;

	while (mpr != nullptr) {
		SNAPSHOT_SAY(("Render_Procedural_Material_Pass"));

   		MeshClass * mesh = mpr->Peek_Mesh();

   		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
   		{	//skip this mesh so it gets rendered later after vertices are filled in.
	        last_mpr = mpr;
   			mpr = mpr->Get_Next_Visible();
   			renderTasksRemaining = true;
   			continue;
   		}

		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		DX8MeshRendererClass::Record_Material_Pass_Path(path);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();

		// remove from list, then delete
		if (last_mpr == nullptr) {
			visible_matpass_head = next_mpr;
		} else {
	       last_mpr->Set_Next_Visible(next_mpr);
	    }

		delete mpr;
		mpr = next_mpr;
	}

	visible_matpass_tail = renderTasksRemaining ? last_mpr : nullptr;
}

void DX8RigidFVFCategoryContainer::Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh)
{
	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (delayed_matpass_head == nullptr) {
		WWASSERT(delayed_matpass_tail == nullptr);
		delayed_matpass_head = new_mpr;
	} else {
		WWASSERT(delayed_matpass_tail != nullptr);
		delayed_matpass_tail->Set_Next_Visible(new_mpr);
	}

	delayed_matpass_tail = new_mpr;
	AnyDelayedPassesToRender=true;
}

void DX8RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes()
{
	if (!Any_Delayed_Passes_To_Render()) return;
	AnyDelayedPassesToRender=false;

	DX8Wrapper::Set_Vertex_Buffer(vertex_buffer);
	DX8Wrapper::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("DX8RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes()"));

	// additional passes
	MatPassTaskClass * mpr = delayed_matpass_head;
	while (mpr != nullptr) {

		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		DX8MeshRendererClass::Record_Material_Pass_Path(DX8InstancingStatsStruct::PASS_PATH_DELAYED);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();

		delete mpr;
		mpr = next_mpr;
	}

	delayed_matpass_head = delayed_matpass_tail = nullptr;
}


void DX8TextureCategoryClass::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("	DX8TextureCategoryClass");

	StringClass work2(255,true);
	for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;++stage) {
		work2.Format("\n	texture[%d]: %x (%s)", stage, textures[stage], textures[stage] ? textures[stage]->Get_Name() : "-");
		work+=work2;
	}
	work2.Format("\n	material: %x (%s)\n	shader: %x", material, material ? material->Get_Name() : "-", shader);
	work+=work2;
	RENDER_LOG(("%s", work.str()));

	RENDER_LOG(("\t%8s %8s %6s %6s %6s %5s %s",
		"idx_cnt",
		"poly_cnt",
		"i_offs",
		"min_vi",
		"vi_rng",
		"ident",
		"name"));

	DX8PolygonRendererListIterator it(&PolygonRendererList);
	while (!it.Is_Done()) {

		DX8PolygonRendererClass* p_renderer = it.Peek_Obj();

		PolyRenderTaskClass * prtc=render_task_head;
		while (prtc) {
			if (prtc->Peek_Polygon_Renderer()==p_renderer) break;
			prtc = prtc->Get_Next_Visible();
		}

		if (prtc != nullptr) {
			RENDER_LOG(("+"));
			p_renderer->Log();
		} else {
			if (!only_visible) {
				RENDER_LOG(("-"));
				p_renderer->Log();
			}
		}
		it.Next();
	}
#endif
}

// ----------------------------------------------------------------------------

DX8FVFCategoryContainer::DX8FVFCategoryContainer(unsigned FVF_,bool sorting_)
	:
	FVF(FVF_),
	sorting(sorting_),
	visible_matpass_head(nullptr),
	visible_matpass_tail(nullptr),
	index_buffer(nullptr),
	used_indices(0),
	passes(MAX_PASSES),
	uv_coordinate_channels(0),
	AnythingToRender(false),
	AnyDelayedPassesToRender(false)
{
	if ((FVF&D3DFVF_TEX1)==D3DFVF_TEX1) uv_coordinate_channels=1;
	if ((FVF&D3DFVF_TEX2)==D3DFVF_TEX2) uv_coordinate_channels=2;
	if ((FVF&D3DFVF_TEX3)==D3DFVF_TEX3) uv_coordinate_channels=3;
	if ((FVF&D3DFVF_TEX4)==D3DFVF_TEX4) uv_coordinate_channels=4;
	if ((FVF&D3DFVF_TEX5)==D3DFVF_TEX5) uv_coordinate_channels=5;
	if ((FVF&D3DFVF_TEX6)==D3DFVF_TEX6) uv_coordinate_channels=6;
	if ((FVF&D3DFVF_TEX7)==D3DFVF_TEX7) uv_coordinate_channels=7;
	if ((FVF&D3DFVF_TEX8)==D3DFVF_TEX8) uv_coordinate_channels=8;
}

// ----------------------------------------------------------------------------

DX8FVFCategoryContainer::~DX8FVFCategoryContainer()
{
	REF_PTR_RELEASE(index_buffer);

	for (unsigned p=0;p<passes;++p) {
		while (DX8TextureCategoryClass * tex = texture_category_list[p].Remove_Head()) {
			delete tex;
		}
	}
}

// ----------------------------------------------------------------------------

DX8TextureCategoryClass* DX8FVFCategoryContainer::Find_Matching_Texture_Category(
	TextureClass* texture,
	unsigned pass,
	unsigned stage,
	DX8TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'texture' on given pass and stage.
	DX8TextureCategoryClass* dest_tex_category=nullptr;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (dest_it.Peek_Obj()->Peek_Texture(stage)==texture) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++) {
				if (stage!=s) {
					all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));
				}
			}
			if (all_textures_same &&
				Equal_Material(dest_tex_category->Peek_Material(),ref_category->Peek_Material()) &&
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return nullptr;
}

DX8TextureCategoryClass* DX8FVFCategoryContainer::Find_Matching_Texture_Category(
		VertexMaterialClass* vmat,
		unsigned pass,
		DX8TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'vmat' on given pass
	DX8TextureCategoryClass* dest_tex_category=nullptr;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (Equal_Material(dest_it.Peek_Obj()->Peek_Material(),vmat)) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++)
				all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));
			if (all_textures_same &&
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return nullptr;
}

void DX8FVFCategoryContainer::Change_Polygon_Renderer_Texture(
	DX8PolygonRendererList& polygon_renderer_list,
	TextureClass* texture,
	TextureClass* new_texture,
	unsigned pass,
	unsigned stage)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (texture==new_texture) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		DX8TextureCategoryClass* src_tex_category=src_it.Peek_Obj();
		if (src_tex_category->Peek_Texture(stage)==texture) {
			foundtexture=true;
			DX8PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				DX8PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				DX8TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();

				if (prc==src_tex_category) {
					DX8TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_texture,pass,stage,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}
						tmp_textures[stage]=new_texture;

						DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(src_tex_category->Peek_Material()),
							pass);

						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);
				}
				poly_it.Next();
			}
		}
		else
			// quit loop if we've got a texture change
			if (foundtexture) break;
		src_it.Next();
	}

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);
		prli.Remove_Current_Object();
		delete rem;
	}
}

void DX8FVFCategoryContainer::Change_Polygon_Renderer_Material(
		DX8PolygonRendererList& polygon_renderer_list,
		VertexMaterialClass* vmat,
		VertexMaterialClass* new_vmat,
		unsigned pass)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (vmat==new_vmat) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		DX8TextureCategoryClass* src_tex_category=src_it.Peek_Obj();
		if (src_tex_category->Peek_Material()==vmat) {
			DX8PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				DX8PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				DX8TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();
				if (prc==src_tex_category) {
					foundtexture=true;
					DX8TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_vmat,pass,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}

						DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(new_vmat),
							pass);

						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);
				}
				poly_it.Next();
			}
		}
		else
			if (foundtexture) break;
		src_it.Next();
	}

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);
		prli.Remove_Current_Object();
		delete rem;
	}
}

// ----------------------------------------------------------------------------

unsigned DX8FVFCategoryContainer::Define_FVF(MeshModelClass* mmc,bool enable_lighting)
{
	if ((!!mmc->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
		return dynamic_fvf_type;
	}

	unsigned fvf=D3DFVF_XYZ;

	int tex_coord_count=mmc->Get_UV_Array_Count();

	if (mmc->Get_Color_Array(0,false)) {
		fvf|=D3DFVF_DIFFUSE;
	}
	if (mmc->Get_Color_Array(1,false)) {
		fvf|=D3DFVF_SPECULAR;
	}

	switch (tex_coord_count) {
	default:
	case 0:
		break;
	case 1: fvf|=D3DFVF_TEX1; break;
	case 2: fvf|=D3DFVF_TEX2; break;
	case 3: fvf|=D3DFVF_TEX3; break;
	case 4: fvf|=D3DFVF_TEX4; break;
	case 5: fvf|=D3DFVF_TEX5; break;
	case 6: fvf|=D3DFVF_TEX6; break;
	case 7: fvf|=D3DFVF_TEX7; break;
	case 8: fvf|=D3DFVF_TEX8; break;
	}

	if (!mmc->Needs_Vertex_Normals()) {  //enable_lighting || mmc->Get_Flag(MeshModelClass::PRELIT_MASK)) {
		return fvf;
	}

	fvf|=D3DFVF_NORMAL;	// Realtime-lit
	return fvf;
}

// ----------------------------------------------------------------------------

DX8RigidFVFCategoryContainer::DX8RigidFVFCategoryContainer(unsigned FVF,bool sorting_)
	:
	DX8FVFCategoryContainer(FVF,sorting_),
	vertex_buffer(nullptr),
	used_vertices(0),
	delayed_matpass_head(nullptr),
	delayed_matpass_tail(nullptr)
{
}

// ----------------------------------------------------------------------------

DX8RigidFVFCategoryContainer::~DX8RigidFVFCategoryContainer()
{
	REF_PTR_RELEASE(vertex_buffer);
}

// ----------------------------------------------------------------------------

void DX8RigidFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	RENDER_LOG(("DX8RigidFVFCategoryContainer --------------"));
	if (vertex_buffer) {
		StringClass fvfname(255,true);
		vertex_buffer->FVF_Info().Get_FVF_Name(fvfname);
		RENDER_LOG(("VB size (used/total): %d/%d FVF: %s",used_vertices,vertex_buffer->Get_Vertex_Count(),fvfname.str()));
	}
	else {
		RENDER_LOG(("EMPTY VB"));
	}
	if (index_buffer) {
		RENDER_LOG(("IB size (used/total): %d/%d",used_indices,index_buffer->Get_Index_Count()));
	}
	else {
		RENDER_LOG(("EMPTY IB"));
	}

	for (unsigned p=0;p<passes;++p) {
		RENDER_LOG(("Pass: %d",p));

		TextureCategoryListIterator it(&texture_category_list[p]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------
//
// Generic render function for rigid meshes
//
// ----------------------------------------------------------------------------

bool DX8RigidFVFCategoryContainer::Bind_Static_Buffers()
{
	DX8Wrapper::Set_Vertex_Buffer(vertex_buffer);
	DX8Wrapper::Set_Index_Buffer(index_buffer,0);
	return true;
}

// the same copy the sorting renderer makes when it flushes, for one fragment, drawn at once
void DX8RigidFVFCategoryContainer::Draw_Copied(DX8PolygonRendererClass* renderer, unsigned base_vertex_offset)
{
	WWASSERT(sorting);
	SortingVertexBufferClass* src_vb=static_cast<SortingVertexBufferClass*>(vertex_buffer);
	SortingIndexBufferClass* src_ib=static_cast<SortingIndexBufferClass*>(index_buffer);

	// the indices reference the mesh's own vertices, so the copy starts where those begin
	const unsigned min_vertex=renderer->Get_Min_Vertex_Index();
	const unsigned vertex_count=renderer->Get_Vertex_Index_Range();
	const unsigned index_count=renderer->Get_Index_Count();
	const VertexFormatXYZNDUV2* src_verts=src_vb->VertexBuffer+base_vertex_offset+min_vertex;
	const unsigned short* src_indices=src_ib->index_buffer+renderer->Get_Index_Offset();

	DynamicVBAccessClass vb(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb);
		memcpy(lock.Get_Formatted_Vertex_Array(),src_verts,sizeof(VertexFormatXYZNDUV2)*vertex_count);
	}

	DynamicIBAccessClass ib(BUFFER_TYPE_DYNAMIC_DX8,index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&ib);
		unsigned short* dst_indices=lock.Get_Index_Array();
		for (unsigned i=0;i<index_count;++i) {
			dst_indices[i]=(unsigned short)(src_indices[i]-min_vertex);
		}
	}

	DX8Wrapper::Set_Vertex_Buffer(vb);
	DX8Wrapper::Set_Index_Buffer(ib,0);
	DX8Wrapper::Draw_Triangles(0,index_count/3,0,vertex_count);
}

void DX8RigidFVFCategoryContainer::Render()
{
	if (!Anything_To_Render()) return;
	AnythingToRender=false;

	DX8Wrapper::Set_Vertex_Buffer(vertex_buffer);
	DX8Wrapper::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("DX8RigidFVFCategoryContainer::Render()"));
	for (unsigned p=0;p<passes;++p) {
		SNAPSHOT_SAY(("Pass: %d",p));
		while (DX8TextureCategoryClass * tex = visible_texture_category_list[p].Remove_Head()) {
			tex->Render();
		}
	}

	Render_Procedural_Material_Passes();
}

// ----------------------------------------------------------------------------

bool DX8RigidFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!vertex_buffer) return true;	// No VB created - mesh will fit as a new vb will be created when inserting
	unsigned required_vertices=mmc->Get_Vertex_Count();
	unsigned available_vertices=vertex_buffer->Get_Vertex_Count()-used_vertices;
	unsigned required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	}
	unsigned required_indices=required_polygons*3*mmc->Get_Pass_Count();
	unsigned available_indices=index_buffer->Get_Index_Count()-used_indices;
	if (
		required_vertices<=available_vertices &&
		(required_indices)<=available_indices) {
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------

class Vertex_Split_Table
{
	MeshModelClass* mmc;
	bool npatch_enable;
	unsigned polygon_count;
	TriIndex* polygon_array;

	bool allocated_polygon_array;

public:
	Vertex_Split_Table(MeshModelClass* mmc_)
		:
		mmc(mmc_),
		npatch_enable(false),
		allocated_polygon_array(false)
	{
		if (DX8Wrapper::Get_Current_Caps()->Support_NPatches() && mmc->Needs_Vertex_Normals()) {
			if (mmc->Get_Flag(MeshGeometryClass::ALLOW_NPATCHES)) {
				npatch_enable=true;
			}
		}

		const GapFillerClass* gap_filler=mmc->Get_Gap_Filler();
		polygon_count=mmc->Get_Polygon_Count();
		if (gap_filler) polygon_count+=gap_filler->Get_Polygon_Count();
//		if (mmc->Get_Gap_Filler_Polygon_Count()) {
			allocated_polygon_array=true;
			polygon_array=W3DNEWARRAY TriIndex[polygon_count];
			memcpy(
				polygon_array,
				mmc->Get_Polygon_Array(),
				mmc->Get_Polygon_Count()*sizeof(TriIndex));
			if (gap_filler) {
				memcpy(
					polygon_array+mmc->Get_Polygon_Count(),
					gap_filler->Get_Polygon_Array(),
					gap_filler->Get_Polygon_Count()*sizeof(TriIndex));
			}
//		}
//		else {
//			polygon_array=const_cast<TriIndex*>(mmc->Get_Polygon_Array());
//		}

	}

	~Vertex_Split_Table()
	{
		if (allocated_polygon_array) {
			delete[] polygon_array;
		}
	}

	const Vector3* Get_Vertex_Array() const
	{
		return mmc->Get_Vertex_Array();
	}

	const Vector3* Get_Vertex_Normal_Array() const
	{
		return mmc->Get_Vertex_Normal_Array();
	}

	const unsigned* Get_Color_Array(unsigned index) const
	{
		return mmc->Get_Color_Array(index,false);
	}

	const Vector2* Get_UV_Array(unsigned uv_array_index) const
	{
		return mmc->Get_UV_Array_By_Index(uv_array_index);
	}

	unsigned Get_Vertex_Count() const
	{
		return mmc->Get_Vertex_Count();
	}

	unsigned Get_Polygon_Count() const
	{
		return polygon_count;
	}

	unsigned Get_Pass_Count() const
	{
		return mmc->Get_Pass_Count();
	}

	TextureClass* Peek_Texture(unsigned idx,unsigned pass,unsigned stage)
	{
		if (mmc->Has_Texture_Array(pass,stage)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Texture_Array(pass,stage)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Texture(idx,pass,stage);
		}
		return mmc->Peek_Single_Texture(pass,stage);
	}

	VertexMaterialClass* Peek_Material(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Material_Array(pass)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Material_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Material(mmc->Get_Polygon_Array()[idx][0],pass);
		}
		return mmc->Peek_Single_Material(pass);
	}

	ShaderClass Peek_Shader(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Shader_Array(pass)) {
			ShaderClass shader;

			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				shader=mmc->Get_Gap_Filler()->Get_Shader_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			else shader=mmc->Get_Shader(idx,pass);

			if (npatch_enable) {
				shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
			}

			return shader;
		}
		if (!npatch_enable) return mmc->Get_Single_Shader(pass);
		ShaderClass shader=mmc->Get_Single_Shader(pass);
		shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
		return shader;

	}

	MeshModelClass* Get_Mesh_Model_Class()
	{
		return mmc;
	}

	unsigned short* Get_Polygon_Array(unsigned pass)
	{
		return (unsigned short*)polygon_array;
	}
};

// ----------------------------------------------------------------------------

void DX8RigidFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc_)
{
	WWASSERT(Check_If_Mesh_Fits(mmc_));

	Vertex_Split_Table split_table(mmc_);
	int needed_vertices=split_table.Get_Vertex_Count();

	/*
	** This FVFCategoryContainer doesn't have a vertex buffer yet so allocate one big
	** enough to contain this mesh.
	*/
	if (!vertex_buffer) {
		int vb_size=4000;
		if (vb_size<needed_vertices) vb_size=needed_vertices;
		if (sorting) {
			vertex_buffer=NEW_REF(SortingVertexBufferClass,(vb_size));
			WWASSERT(vertex_buffer->FVF_Info().Get_FVF()==FVF);	// Only one sorting FVF type!
		}
		else {
			vertex_buffer=NEW_REF(DX8VertexBufferClass,(
				FVF,
				vb_size,
				(DX8Wrapper::Get_Current_Caps()->Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DX8VertexBufferClass::USAGE_NPATCHES : DX8VertexBufferClass::USAGE_DEFAULT));
		}
	}

	/*
	** Append this mesh's vertices to the vertex buffer.
	*/

	VertexBufferClass::AppendLockClass l(vertex_buffer,used_vertices,split_table.Get_Vertex_Count());
	const FVFInfoClass fi=vertex_buffer->FVF_Info();
	unsigned char *vb=(unsigned char*) l.Get_Vertex_Array();
	unsigned int i;
	const Vector3 *locs=split_table.Get_Vertex_Array();
	const Vector3 *norms=split_table.Get_Vertex_Normal_Array();
	const unsigned *diffuse=split_table.Get_Color_Array(0);
	const unsigned *specular=split_table.Get_Color_Array(1);
	for (i=0; i<split_table.Get_Vertex_Count(); i++)
	{
		*(Vector3*)(vb+fi.Get_Location_Offset())=locs[i];

		if ((FVF&D3DFVF_NORMAL)==D3DFVF_NORMAL && norms) {
			*(Vector3*)(vb+fi.Get_Normal_Offset())=norms[i];
		}

		if ((FVF&D3DFVF_DIFFUSE)==D3DFVF_DIFFUSE) {
			if (diffuse) {
				*(unsigned int*)(vb+fi.Get_Diffuse_Offset())=diffuse[i];
			} else {
				*(unsigned int*)(vb+fi.Get_Diffuse_Offset()) = 0xFFFFFFFF;
			}
		}

		if ((FVF&D3DFVF_SPECULAR)==D3DFVF_SPECULAR) {
			if (specular) {
				*(unsigned int*)(vb+fi.Get_Specular_Offset())=specular[i];
			} else {
				*(unsigned int*)(vb+fi.Get_Specular_Offset()) = 0xFFFFFFFF;
			}
		}

		vb+=fi.Get_FVF_Size();
	}


	/*
	** Append the UV coordinates to the vertex buffer
	*/
	int uvcount = 0;
	if ((FVF&D3DFVF_TEX1) == D3DFVF_TEX1) {
		uvcount = 1;
	}
	if ((FVF&D3DFVF_TEX2) == D3DFVF_TEX2) {
		uvcount = 2;
	}
	if ((FVF&D3DFVF_TEX3) == D3DFVF_TEX3) {
		uvcount = 3;
	}
	if ((FVF&D3DFVF_TEX4) == D3DFVF_TEX4) {
		uvcount = 4;
	}
	if ((FVF&D3DFVF_TEX5) == D3DFVF_TEX5) {
		uvcount = 5;
	}
	if ((FVF&D3DFVF_TEX6) == D3DFVF_TEX6) {
		uvcount = 6;
	}
	if ((FVF&D3DFVF_TEX7) == D3DFVF_TEX7) {
		uvcount = 7;
	}
	if ((FVF&D3DFVF_TEX8) == D3DFVF_TEX8) {
		uvcount = 8;
	}

	for (int j=0; j<uvcount; j++) {
		unsigned char *vb=(unsigned char*) l.Get_Vertex_Array();
		const Vector2*uvs=split_table.Get_UV_Array(j);
		if (uvs) {
			for (i=0; i<split_table.Get_Vertex_Count(); i++)
			{
				*(Vector2*)(vb+fi.Get_Tex_Offset(j))=uvs[i];
				vb+=fi.Get_FVF_Size();
			}
		}
	}

	Generate_Texture_Categories(split_table,used_vertices);

	used_vertices+=needed_vertices;//vertex_count;
}

void DX8FVFCategoryContainer::Insert_To_Texture_Category(
	Vertex_Split_Table& split_table,
	TextureClass** texs,
	VertexMaterialClass* mat,
	ShaderClass shader,
	int pass,
	unsigned vertex_offset)
{
	/*
	** Try to find a DX8TextureCategoryClass in this FVF container which matches the
	** given textures(one per stage), material and shader combination.
	*/
	bool fit_in_existing_category = false;
	TextureCategoryListIterator it(&texture_category_list[pass]);
	while (!it.Is_Done()) {
		DX8TextureCategoryClass * tex_category=it.Peek_Obj();
		// Compare all stage's textures
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (tex_category->Peek_Texture(stage) == texs[stage]);
		}
		if (all_textures_same && Equal_Material(tex_category->Peek_Material(),mat) && tex_category->Get_Shader()==shader) {
			used_indices+=tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);
			fit_in_existing_category = true;
			break;
		}
		it.Next();
	}

	if (!fit_in_existing_category) {

		DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(this,texs,shader,mat,pass);
		used_indices+=new_tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);

		/*
		** Add the texture category object into the list, immediately after any existing
		** texture category object which uses the same texture.  This will result in
		** the list always having matching texture categories next to each other.
		*/
		bool found_similar_category = false;
		TextureCategoryListIterator it(&texture_category_list[pass]);
		while (!it.Is_Done()) {
			// Categorize according to first stage's texture for now
			if (it.Peek_Obj()->Peek_Texture(0) == texs[0]) {
				texture_category_list[pass].Add_After(new_tex_category,it.Peek_Obj());
				found_similar_category = true;
				break;
			}
			it.Next();
		}

		if (!found_similar_category) {
			texture_category_list[pass].Add_Tail(new_tex_category);
		}
	}
}

const unsigned MAX_ADDED_TYPE_COUNT=64;
struct Textures_Material_And_Shader_Booking_Struct
{
	TextureClass* added_textures[MeshMatDescClass::MAX_TEX_STAGES][MAX_ADDED_TYPE_COUNT];
	VertexMaterialClass* added_materials[MAX_ADDED_TYPE_COUNT];
	ShaderClass added_shaders[MAX_ADDED_TYPE_COUNT];
	unsigned added_type_count;

	Textures_Material_And_Shader_Booking_Struct() : added_type_count(0) {}

	bool Add_Textures_Material_And_Shader(TextureClass** texs, VertexMaterialClass* mat, ShaderClass shd)
	{
		for (unsigned a=0;a<added_type_count;++a) {
			// Compare textures
			bool all_textures_same = true;
			for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
				all_textures_same = all_textures_same && (texs[stage] == added_textures[stage][a]);
			}
			if (all_textures_same && Equal_Material(mat,added_materials[a]) && shd==added_shaders[a]) {
				return false;
			}
		}
		WWASSERT(added_type_count<MAX_ADDED_TYPE_COUNT);
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			added_textures[stage][added_type_count]=texs[stage];
		}
		added_materials[added_type_count]=mat;
		added_shaders[added_type_count]=shd;
		added_type_count++;
		return true;
	}
};

void DX8FVFCategoryContainer::Generate_Texture_Categories(Vertex_Split_Table& split_table,unsigned vertex_offset)
{
	int polygon_count=split_table.Get_Polygon_Count();
	int index_count=polygon_count*3*split_table.Get_Pass_Count();

	/*
	** If we don't have an index buffer yet, allocate one.  Make it hold at least 12000 entries,
	** more if the mesh requires it.
	*/
	if (!index_buffer) {
		int ib_size=12000;
		if (ib_size<index_count) ib_size=index_count;
		if (sorting) {
			index_buffer=NEW_REF(SortingIndexBufferClass,(ib_size));
		}
		else {
			index_buffer=NEW_REF(DX8IndexBufferClass,(
				ib_size,
				(DX8Wrapper::Get_Current_Caps()->Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DX8IndexBufferClass::USAGE_NPATCHES : DX8IndexBufferClass::USAGE_DEFAULT));
		}
	}

	for (unsigned pass=0;pass<split_table.Get_Pass_Count();++pass) {
		Textures_Material_And_Shader_Booking_Struct textures_material_and_shader_booking;

		unsigned old_used_indices=used_indices;

		for (int i=0;i<polygon_count;++i) {
			TextureClass* textures[MeshMatDescClass::MAX_TEX_STAGES];
			// disabled this assert as MAX_TEXTURE_STAGES is now 8, but legacy MeshMat::MAX_TEX_STAGES is still 2
	//		WWASSERT(MAX_TEXTURE_STAGES==MeshMatDescClass::MAX_TEX_STAGES);
			for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;stage++)
			{
				textures[stage]=split_table.Peek_Texture(i,pass,stage);
			}
			VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
			ShaderClass shader=split_table.Peek_Shader(i,pass);
			if (!textures_material_and_shader_booking.Add_Textures_Material_And_Shader(textures,mat,shader)) continue;

			Insert_To_Texture_Category(split_table,textures,mat,shader,pass,vertex_offset);
		}

		int new_inds=used_indices-old_used_indices;
		WWASSERT(new_inds<=polygon_count*3);
	}
}

// ----------------------------------------------------------------------------

// Where a skin model's vertices sit in its container's skinned vertex buffer, and the bones they follow.
struct SkinnedModelRecord
{
	MeshModelClass *						Model;
	DX8SkinFVFCategoryContainer *		Container;
	unsigned									VertexOffset;
	DX8SkinningClass::PaletteStruct	Palette;
};

static std::vector<SkinnedModelRecord>		_SkinnedModels;

static bool Skinned_Model_Less(const SkinnedModelRecord & record, const MeshModelClass * mmc)
{
	return record.Model < mmc;
}

static std::vector<SkinnedModelRecord>::iterator Find_Skinned_Model(const MeshModelClass * mmc)
{
	std::vector<SkinnedModelRecord>::iterator it = std::lower_bound(_SkinnedModels.begin(), _SkinnedModels.end(), mmc, Skinned_Model_Less);
	return (it != _SkinnedModels.end() && it->Model == mmc) ? it : _SkinnedModels.end();
}

static const SkinnedModelRecord * Peek_Skinned_Model(const MeshModelClass * mmc)
{
	std::vector<SkinnedModelRecord>::iterator it = Find_Skinned_Model(mmc);
	return (it != _SkinnedModels.end()) ? &*it : nullptr;
}

static int Find_Palette_Bone(const DX8SkinningClass::PaletteStruct & palette, unsigned short pivot)
{
	for (int bone=0;bone<palette.Count;++bone)
	{
		if (palette.Pivots[bone] == pivot)
		{
			return bone;
		}
	}
	return -1;
}

// False when the model follows more bones than the palette holds.
static bool Build_Palette(MeshModelClass * mmc, DX8SkinningClass::PaletteStruct & palette)
{
	const uint16 * links = mmc->Get_Vertex_Bone_Links();
	palette.Count = 0;
	for (int i=0;i<mmc->Get_Vertex_Count();++i)
	{
		if (Find_Palette_Bone(palette, links[i]) >= 0)
		{
			continue;
		}
		if (palette.Count == DX8SkinningClass::MAX_BONES)
		{
			return false;
		}
		palette.Pivots[palette.Count++] = links[i];
	}
	return true;
}

// The layout of DX8SkinningClass::Get_Vertex_FVF.
struct SkinnedVertexStruct
{
	float			X, Y, Z;
	float			NX, NY, NZ;
	unsigned		Diffuse;
	unsigned		Bone;
	float			U1, V1;
	float			U2, V2;
};

enum { SKINNED_VERTEX_BUFFER_SIZE = 4000 };

DX8SkinFVFCategoryContainer::DX8SkinFVFCategoryContainer(bool sorting)
	:
	DX8FVFCategoryContainer(DX8_FVF_XYZNUV1,sorting),
	VisibleVertexCount(0),
	VisibleSkinHead(nullptr),
	VisibleSkinTail(nullptr),
	SkinnedVertexBuffer(nullptr),
	UsedSkinnedVertices(0)
{
}

// ----------------------------------------------------------------------------

DX8SkinFVFCategoryContainer::~DX8SkinFVFCategoryContainer()
{
	Release_Skinned_Vertices();
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Forget_Skinned_Model(MeshModelClass* mmc)
{
	std::vector<SkinnedModelRecord>::iterator it = Find_Skinned_Model(mmc);
	if (it != _SkinnedModels.end())
	{
		_SkinnedModels.erase(it);
	}
}

void DX8SkinFVFCategoryContainer::Release_Skinned_Vertices()
{
	size_t kept = 0;
	for (size_t i=0;i<_SkinnedModels.size();++i)
	{
		if (_SkinnedModels[i].Container != this)
		{
			_SkinnedModels[kept++] = _SkinnedModels[i];
		}
	}
	_SkinnedModels.resize(kept);
	REF_PTR_RELEASE(SkinnedVertexBuffer);
	UsedSkinnedVertices = 0;
}

bool DX8SkinFVFCategoryContainer::Wants_Skinned_Vertices(MeshModelClass* mmc) const
{
	if (sorting || mmc->Get_Vertex_Count() > 65535 || !DX8SkinningClass::Is_Supported())
	{
		return false;
	}
	DX8SkinningClass::PaletteStruct palette;
	return Build_Palette(mmc, palette);
}

// Copies the model's vertices in the order the CPU writes them, so the same indices address both.
void DX8SkinFVFCategoryContainer::Add_Skinned_Vertices(MeshModelClass* mmc)
{
	if (!Wants_Skinned_Vertices(mmc))
	{
		return;
	}
	SkinnedModelRecord record;
	record.Model = mmc;
	record.Container = this;
	Build_Palette(mmc, record.Palette);

	const int vertex_count = mmc->Get_Vertex_Count();
	if (SkinnedVertexBuffer == nullptr)
	{
		const int size = (vertex_count > SKINNED_VERTEX_BUFFER_SIZE) ? vertex_count : SKINNED_VERTEX_BUFFER_SIZE;
		SkinnedVertexBuffer = NEW_REF(DX8VertexBufferClass,(DX8SkinningClass::Get_Vertex_FVF(), (unsigned short)size));
		WWASSERT(SkinnedVertexBuffer->FVF_Info().Get_FVF_Size() == sizeof(SkinnedVertexStruct));
	}
	if (UsedSkinnedVertices + vertex_count > SkinnedVertexBuffer->Get_Vertex_Count())
	{
		return;
	}

	const Vector3 * locs = mmc->Get_Vertex_Array();
	const Vector3 * norms = mmc->Get_Vertex_Normal_Array();
	const Vector2 * uv0 = mmc->Get_UV_Array_By_Index(0);
	const Vector2 * uv1 = mmc->Get_UV_Array_By_Index(1);
	const unsigned * diffuse = mmc->Get_Color_Array(0,false);
	const uint16 * links = mmc->Get_Vertex_Bone_Links();
	{
		VertexBufferClass::AppendLockClass l(SkinnedVertexBuffer,UsedSkinnedVertices,vertex_count);
		SkinnedVertexStruct * verts = (SkinnedVertexStruct *)l.Get_Vertex_Array();
		for (int v=0;v<vertex_count;++v)
		{
			verts[v].X = locs[v].X;
			verts[v].Y = locs[v].Y;
			verts[v].Z = locs[v].Z;
			verts[v].NX = norms[v].X;
			verts[v].NY = norms[v].Y;
			verts[v].NZ = norms[v].Z;
			verts[v].Diffuse = (diffuse != nullptr) ? diffuse[v] : 0;
			verts[v].Bone = (unsigned)Find_Palette_Bone(record.Palette, links[v]);
			verts[v].U1 = (uv0 != nullptr) ? uv0[v].X : 0.0f;
			verts[v].V1 = (uv0 != nullptr) ? uv0[v].Y : 0.0f;
			verts[v].U2 = (uv1 != nullptr) ? uv1[v].X : 0.0f;
			verts[v].V2 = (uv1 != nullptr) ? uv1[v].Y : 0.0f;
		}
	}
	record.VertexOffset = UsedSkinnedVertices;
	UsedSkinnedVertices += vertex_count;

	std::vector<SkinnedModelRecord>::iterator it = std::lower_bound(_SkinnedModels.begin(), _SkinnedModels.end(), mmc, Skinned_Model_Less);
	if (it != _SkinnedModels.end() && it->Model == mmc)
	{
		*it = record;
	}
	else
	{
		_SkinnedModels.insert(it, record);
	}
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	RENDER_LOG(("DX8SkinFVFCategoryContainer --------------"));

	if (index_buffer) {
		RENDER_LOG(("IB size (used/total): %d/%d",used_indices,index_buffer->Get_Index_Count()));
	}
	else {
		RENDER_LOG(("EMPTY IB"));
	}

	for (unsigned pass=0;pass<passes;++pass) {
		TextureCategoryListIterator it(&texture_category_list[pass]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Render()
{
	SNAPSHOT_SAY(("DX8SkinFVFCategoryContainer::Render()"));
	if (!Anything_To_Render()) {
		SNAPSHOT_SAY(("Nothing to render"));
		return;
	}
	AnythingToRender=false;

	Render_Skinned_Meshes();
	if (VisibleVertexCount == 0) {
		for (unsigned pass=0;pass<passes;++pass) {
			while (visible_texture_category_list[pass].Remove_Head()) {
			}
		}
		clearVisibleSkinList();
		return;
	}

	DX8Wrapper::Set_Vertex_Buffer(nullptr);	// Free up the reference to the current vertex buffer
														// (in case it is the dynamic, which may have to be resized)

	//'Generals' customization to allow more than 65535 vertices
	unsigned int maxVertexCount=VisibleVertexCount;
	if (maxVertexCount > 65535)
	{	//clamp vertex count to maximum size that can be indexed by 16-bit index
		maxVertexCount = 65535;
	}

	DynamicVBAccessClass vb(
		sorting ? BUFFER_TYPE_DYNAMIC_SORTING : BUFFER_TYPE_DYNAMIC_DX8,
		dynamic_fvf_type,
		maxVertexCount);
	SNAPSHOT_SAY(("DynamicVBAccess - %s - %d vertices",sorting ? "sorting" : "non-sorting",VisibleVertexCount));

	unsigned int renderedVertexCount=0;

	MeshClass * mesh = VisibleSkinHead;
	MeshClass * remainingMesh = VisibleSkinHead;
	while (renderedVertexCount < VisibleVertexCount)
	{	mesh = remainingMesh;
		{	DynamicVBAccessClass::WriteLockClass l(&vb);
			VertexFormatXYZNDUV2 * dest_verts = l.Get_Formatted_Vertex_Array();
			unsigned vertex_offset=0;
			remainingMesh = nullptr;

			while (mesh != nullptr) {

				MeshModelClass * mmc = mesh->Peek_Model();
				int mesh_vertex_count=mmc->Get_Vertex_Count();
				//'Generals' mod to deal with cases where not all meshes fit in VB.
				if (vertex_offset+mesh_vertex_count > maxVertexCount || remainingMesh)
				{	//flag mesh so we know it didn't fit in the vertex buffer
					mesh->Set_Base_Vertex_Offset(VERTEX_BUFFER_OVERFLOW);
					if (remainingMesh == nullptr)
						remainingMesh = mesh;	//start of meshes that didn't fit in buffer
					mesh = mesh->Peek_Next_Visible_Skin();	//skip rendering this mesh
					continue;
				}


		// If this assert hits, a skinned mesh has probably been added to the scene more than once.
		// Example: A skinned mesh was added to the scene then it was attached to a bone without being removed from the scene.
		WWASSERT((vertex_offset+mesh_vertex_count)<=VisibleVertexCount);
			DX8_RECORD_SKIN_RENDER(mesh->Get_Num_Polys(),mesh_vertex_count);

				if (_TempVertexBuffer.Length() < mesh_vertex_count) _TempVertexBuffer.Resize(mesh_vertex_count);
				if (_TempNormalBuffer.Length() < mesh_vertex_count) _TempNormalBuffer.Resize(mesh_vertex_count);

				Vector3* loc=&(_TempVertexBuffer[0]);
				Vector3* norm=&(_TempNormalBuffer[0]);
				const Vector2* uv0=mmc->Get_UV_Array_By_Index(0);
				const Vector2* uv1=mmc->Get_UV_Array_By_Index(1);
				const unsigned* diffuse=mmc->Get_Color_Array(0,false);

				VertexFormatXYZNDUV2* verts=dest_verts+vertex_offset;

				mesh->Get_Deformed_Vertices(loc,norm);

				for (int v=0;v<mesh_vertex_count;++v) {
					verts[v].x=(*loc)[0];
					verts[v].y=(*loc)[1];
					verts[v].z=(*loc)[2];
					verts[v].nx=(*norm)[0];
					verts[v].ny=(*norm)[1];
					verts[v].nz=(*norm)[2];
					if (diffuse) {
						verts[v].diffuse=*diffuse++;
					}
					else {
						verts[v].diffuse=0;
					}
					if (uv0) {
						verts[v].u1=(*uv0)[0];
						verts[v].v1=(*uv0)[1];
						uv0++;
					}
					else {
						verts[v].u1=0.0f;
						verts[v].v1=0.0f;
					}
					if (uv1) {
						verts[v].u2=(*uv1)[0];
						verts[v].v2=(*uv1)[1];
						uv1++;
					}
					else {
						verts[v].u2=0.0f;
						verts[v].v2=0.0f;
					}

					loc++;
					norm++;
				}

				mesh->Set_Base_Vertex_Offset(vertex_offset);
				vertex_offset+=mesh_vertex_count;
				renderedVertexCount += mesh_vertex_count;

				mesh = mesh->Peek_Next_Visible_Skin();
			}
		}

		SNAPSHOT_SAY(("Set vb: %x ib: %x",&vb.FVF_Info(),index_buffer));

		DX8Wrapper::Set_Vertex_Buffer(vb);
		DX8Wrapper::Set_Index_Buffer(index_buffer,0);

		//Flush the meshes which fit in the vertex buffer, applying all texture variations
		for (unsigned pass=0;pass<passes;++pass) {
			SNAPSHOT_SAY(("Pass: %d",pass));

			TextureCategoryListIterator it(&visible_texture_category_list[pass]);
			while (!it.Is_Done()) {
				it.Peek_Obj()->Render();
				it.Next();
			}
		}

		Render_Procedural_Material_Passes();
	}

	//remove all the rendered data from queues
	for (unsigned pass=0;pass<passes;++pass) {
		while (DX8TextureCategoryClass * tex = visible_texture_category_list[pass].Remove_Head()) {
		}
	}

	WWASSERT(renderedVertexCount==VisibleVertexCount);

	clearVisibleSkinList();
}

bool DX8SkinFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!index_buffer) return true;	// No IB created - mesh will fit as a new ib will be created when inserting
	int required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	}

	if ((required_polygons*3*mmc->Get_Pass_Count())>index_buffer->Get_Index_Count()-used_indices) {
		return false;
	}
	if (SkinnedVertexBuffer != nullptr && Wants_Skinned_Vertices(mmc)) {
		return mmc->Get_Vertex_Count()<=SkinnedVertexBuffer->Get_Vertex_Count()-UsedSkinnedVertices;
	}
	return true;
}

// Every category a mesh draws in must be one the skin shader reproduces, since all its draws take one path.
bool DX8SkinFVFCategoryContainer::Allows_Skinning(MeshClass * mesh, const MeshClass * const * pass_meshes, int pass_mesh_count)
{
	const SkinnedModelRecord * record = Peek_Skinned_Model(mesh->Peek_Model());
	if (record == nullptr || record->Container != this)
	{
		DX8SkinningClass::Record_Rejection(DX8SkinningClass::REJECT_MODEL);
		return false;
	}
	if (!DX8SkinningClass::Allows_Mesh(mesh))
	{
		DX8SkinningClass::Record_Rejection(DX8SkinningClass::REJECT_MESH);
		return false;
	}
	if (std::binary_search(pass_meshes, pass_meshes + pass_mesh_count, mesh))
	{
		DX8SkinningClass::Record_Rejection(DX8SkinningClass::REJECT_PASS);
		return false;
	}
	DX8PolygonRendererListIterator it(&mesh->Peek_Model()->PolygonRendererList);
	for (;!it.Is_Done();it.Next())
	{
		DX8TextureCategoryClass * category = it.Peek_Obj()->Get_Texture_Category();
		if (!DX8SkinningClass::Allows_Category(category->Get_Shader(), const_cast<VertexMaterialClass *>(category->Peek_Material()), category->Peek_Texture(1) != nullptr))
		{
			DX8SkinningClass::Record_Rejection(DX8SkinningClass::REJECT_CATEGORY);
			return false;
		}
	}
	return true;
}

void DX8SkinFVFCategoryContainer::Render_Skinned_Meshes()
{
	if (SkinnedVertexBuffer == nullptr || DX8SkinningClass::Get_Pass() == DX8VertexShadingClass::PASS_NONE)
	{
		return;
	}

	// A pass the skin shader cannot draw keeps its mesh on the CPU, base pass included.
	static std::vector<MeshClass *> pass_meshes;
	for (MatPassTaskClass * mpr = visible_matpass_head; mpr != nullptr; mpr = mpr->Get_Next_Visible())
	{
		if (!DX8SkinningClass::Allows_Material_Pass(mpr->Peek_Material_Pass()))
		{
			pass_meshes.push_back(mpr->Peek_Mesh());
		}
	}
	std::sort(pass_meshes.begin(), pass_meshes.end());

	// Meshes left to the CPU are marked as not yet in a vertex buffer, which holds back their draws.
	int skinned = 0;
	for (MeshClass * mesh = VisibleSkinHead; mesh != nullptr; mesh = mesh->Peek_Next_Visible_Skin())
	{
		if (Allows_Skinning(mesh, pass_meshes.empty() ? nullptr : &pass_meshes[0], (int)pass_meshes.size()))
		{
			mesh->Set_Base_Vertex_Offset(Peek_Skinned_Model(mesh->Peek_Model())->VertexOffset);
			++skinned;
		}
		else
		{
			mesh->Set_Base_Vertex_Offset(VERTEX_BUFFER_OVERFLOW);
		}
	}
	pass_meshes.clear();

	if (skinned == 0 || !DX8SkinningClass::Begin_Sweep())
	{
		return;
	}
	DX8Wrapper::Set_Vertex_Buffer(SkinnedVertexBuffer);
	DX8Wrapper::Set_Index_Buffer(index_buffer,0);
	for (unsigned pass=0;pass<passes;++pass)
	{
		TextureCategoryListIterator it(&visible_texture_category_list[pass]);
		for (;!it.Is_Done();it.Next())
		{
			it.Peek_Obj()->Render();
		}
	}
	DX8SkinningClass::Begin_Material_Passes();
	Render_Skinned_Material_Passes();
	DX8SkinningClass::End_Sweep();
	DX8SkinningClass::Record_Skinned_Meshes(skinned);

	MeshClass * kept_head = nullptr;
	MeshClass * kept_tail = nullptr;
	unsigned int kept_vertices = 0;
	MeshClass * mesh = VisibleSkinHead;
	while (mesh != nullptr)
	{
		MeshClass * next = mesh->Peek_Next_Visible_Skin();
		mesh->Set_Next_Visible_Skin(nullptr);
		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)
		{
			if (kept_tail == nullptr)
			{
				kept_head = mesh;
			}
			else
			{
				kept_tail->Set_Next_Visible_Skin(mesh);
			}
			kept_tail = mesh;
			kept_vertices += mesh->Peek_Model()->Get_Vertex_Count();
		}
		mesh = next;
	}
	VisibleSkinHead = kept_head;
	VisibleSkinTail = kept_tail;
	VisibleVertexCount = kept_vertices;
}

// CONTRA_SKINPASSRUNS=0 installs each skin's passes mesh by mesh again, for comparing call counts.
static bool Get_Skin_Pass_Runs_Enabled()
{
	const char *value = getenv("CONTRA_SKINPASSRUNS");
	return (value != nullptr) ? atoi(value) != 0 : true;
}

static const bool SkinPassRunsEnabled = Get_Skin_Pass_Runs_Enabled();

// Each pass installs once for all its skins, in the order the passes first appear, so a mesh's own passes keep their order.
void DX8SkinFVFCategoryContainer::Render_Skinned_Material_Passes()
{
	static std::vector<MatPassTaskClass *> tasks;
	static std::vector<MaterialPassClass *> runs;

	MatPassTaskClass * mpr = visible_matpass_head;
	MatPassTaskClass * last_mpr = nullptr;
	bool renderTasksRemaining=false;

	while (mpr != nullptr) {
		MeshClass * mesh = mpr->Peek_Mesh();

		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)
		{
			last_mpr = mpr;
			mpr = mpr->Get_Next_Visible();
			renderTasksRemaining = true;
			continue;
		}

		DX8MeshRendererClass::Record_Material_Pass_Path(DX8InstancingStatsStruct::PASS_PATH_SKIN);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();

		if (last_mpr == nullptr) {
			visible_matpass_head = next_mpr;
		} else {
			last_mpr->Set_Next_Visible(next_mpr);
		}

		if (SkinPassRunsEnabled)
		{
			tasks.push_back(mpr);
			if (std::find(runs.begin(), runs.end(), mpr->Peek_Material_Pass()) == runs.end())
			{
				runs.push_back(mpr->Peek_Material_Pass());
			}
		}
		else
		{
			DX8SkinningClass::Set_Mesh(mesh, Peek_Skinned_Model(mesh->Peek_Model())->Palette, nullptr);
			mesh->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
			delete mpr;
		}
		mpr = next_mpr;
	}

	visible_matpass_tail = renderTasksRemaining ? last_mpr : nullptr;

	// These passes take no lighting, and a mesh's light environment set between draws would drop the pass's texgen.
	for (size_t r=0;r<runs.size();++r)
	{
		MaterialPassClass * pass = runs[r];
		pass->Install_Materials();
		DX8Wrapper::Set_Index_Buffer(index_buffer,0);
		DX8Wrapper::Set_World_Identity();

		for (size_t i=0;i<tasks.size();++i)
		{
			if (tasks[i]->Peek_Material_Pass() != pass)
			{
				continue;
			}
			MeshClass * mesh = tasks[i]->Peek_Mesh();
			DX8SkinningClass::Set_Mesh(mesh, Peek_Skinned_Model(mesh->Peek_Model())->Palette, nullptr);

			DX8PolygonRendererListIterator it(&mesh->Peek_Model()->PolygonRendererList);
			for (;!it.Is_Done();it.Next())
			{
				DX8PolygonRendererClass * renderer = it.Peek_Obj();
				if (renderer->Get_Pass() == 0)
				{
					pass->Install_Polygon_Materials(renderer, mesh);
					renderer->Render(mesh->Get_Base_Vertex_Offset());
				}
			}
		}

		pass->UnInstall_Materials();
	}

	for (size_t i=0;i<tasks.size();++i)
	{
		delete tasks[i];
	}
	tasks.clear();
	runs.clear();
}

void DX8SkinFVFCategoryContainer::clearVisibleSkinList()
{
	while (VisibleSkinHead != nullptr)
	{
		MeshClass* next = VisibleSkinHead->Peek_Next_Visible_Skin();
		VisibleSkinHead->Set_Next_Visible_Skin(nullptr);
		VisibleSkinHead = next;
	}
	VisibleSkinHead = nullptr;
	VisibleSkinTail = nullptr;
	VisibleVertexCount = 0;
}

void DX8SkinFVFCategoryContainer::Add_Visible_Skin(MeshClass * mesh)
{
	if (mesh->Peek_Next_Visible_Skin() != nullptr || mesh == VisibleSkinTail)
	{
		DEBUG_CRASH(("Mesh %s is already a visible skin, and we tried to add it again... please notify Mark W or Steven J immediately!",mesh->Get_Name()));
		return;
	}
	if (VisibleSkinHead == nullptr)
		VisibleSkinTail = mesh;
	mesh->Set_Next_Visible_Skin(VisibleSkinHead);
	VisibleSkinHead = mesh;
	VisibleVertexCount += mesh->Peek_Model()->Get_Vertex_Count();
}


// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Reset()
{
	clearVisibleSkinList();

	for (unsigned pass=0;pass<passes;++pass) {
		while (DX8TextureCategoryClass* texture_category=texture_category_list[pass].Peek_Head()) {
			delete texture_category;
		}
	}

	REF_PTR_RELEASE(index_buffer);
	used_indices=0;
	Release_Skinned_Vertices();
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc)
{
	Vertex_Split_Table split_table(mmc);

	Generate_Texture_Categories(split_table,0);
	Add_Skinned_Vertices(mmc);
}

// ----------------------------------------------------------------------------

unsigned DX8TextureCategoryClass::Add_Mesh(
	Vertex_Split_Table& split_table,
	unsigned vertex_offset,
	unsigned index_offset,
	IndexBufferClass* index_buffer,
	unsigned pass)
{
	int poly_count=split_table.Get_Polygon_Count();

	unsigned index_count=0;

	/*
	** Count the polygons in the given mesh in the given pass which match this texture category
	*/
	unsigned polygons=0;

	for (int i=0;i<poly_count;++i) {
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
		}
		VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
		ShaderClass shd=split_table.Peek_Shader(i,pass);

		if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
			polygons++;
		}
	}

	/*
	** Add the indices for the polygons that match into this renderer's dx8 index table.
	*/
	if (polygons) {

		index_count=polygons*3;
#ifndef ENABLE_STRIPING
		bool stripify=false;
#else
		bool stripify=true;
		if (index_buffer->Type()==BUFFER_TYPE_SORTING || index_buffer->Type()==BUFFER_TYPE_DYNAMIC_SORTING) {
			stripify=false;
		}
#endif // ;
		const TriIndex* src_indices=(const TriIndex*)split_table.Get_Polygon_Array(pass);//mmc->Get_Polygon_Array();

		if (stripify) {
			int* triangles=W3DNEWARRAY int[index_count];
			int triangle_index_count=0;
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
				ShaderClass shd=split_table.Peek_Shader(i,pass);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					triangles[triangle_index_count++]=src_indices[i][0]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][1]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][2]+vertex_offset;
				}
			}

			int* strips=StripOptimizerClass::Stripify(triangles, triangle_index_count/3);
			delete[] triangles;
			int* strip=StripOptimizerClass::Combine_Strips(strips+1,strips[0]);
			delete[] strips;

			if (index_count<unsigned(strip[0])) {
				stripify=false;
			}
			else {
				index_count=strip[0];

				DX8PolygonRendererClass* p_renderer=W3DNEW DX8PolygonRendererClass(
					index_count,
					split_table.Get_Mesh_Model_Class(),
					this,
					vertex_offset,
					index_offset,
					true,
					pass);
				PolygonRendererList.Add_Tail(p_renderer);

				{
					IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
					unsigned short* dst_indices=l.Get_Index_Array();

					unsigned short vmin=0xffff;
					unsigned short vmax=0;

					/*
					** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
					*/
					for (unsigned i=0;i<index_count;++i) {
						unsigned short idx;

						idx=(unsigned short)(strip[i+1]);
						vmin=MIN(vmin,idx);
						vmax=MAX(vmax,idx);
						*dst_indices++=idx;
					}

					/*
					** Remember the min and max vertex indices that these polygons used (for optimization)
					*/
					p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
				}
			}
			delete[] strip;
		}

		// Need to check stripify again as it may be changed to false by the previous statement
		if (!stripify ) {
			DX8PolygonRendererClass* p_renderer=W3DNEW DX8PolygonRendererClass(
				index_count,
				split_table.Get_Mesh_Model_Class(),
				this,
				vertex_offset,
				index_offset,
				false,
				pass);
			PolygonRendererList.Add_Tail(p_renderer);

			IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
			unsigned short* dst_indices=l.Get_Index_Array();

			unsigned short vmin=0xffff;
			unsigned short vmax=0;

			/*
			** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
			*/
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
				ShaderClass shd=split_table.Peek_Shader(i,pass);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					unsigned short idx;

					idx=(unsigned short)(src_indices[i][0]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=(unsigned short)(src_indices[i][1]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=(unsigned short)(src_indices[i][2]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d",idx));
				}
			}

			WWASSERT((vmax-vmin)<split_table.Get_Mesh_Model_Class()->Get_Vertex_Count());

			/*
			** Remember the min and max vertex indices that these polygons used (for optimization)
			*/
			p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
			WWASSERT(index_count<=unsigned(split_table.Get_Polygon_Count()*3));
		}
	}

	return index_count;
}

// ----------------------------------------------------------------------------

// An instanced draw shares one fixed-function setup, so texgen, specular and blend tricks rule a category out.
// Without an active pass this measures the lit scene against the rules its instancing will need.
bool DX8TextureCategoryClass::Allows_Instancing() const
{
	if (container->Is_Sorting() || Is_Additive() || m_gForceMultiply)
	{
		return false;
	}
	if (DX8InstancingClass::Get_Pass() != DX8InstancingClass::PASS_NONE)
	{
		return DX8InstancingClass::Allows_Category(shader, material, container->Get_FVF(), textures[1] != nullptr);
	}
	if (shader.Uses_Secondary_Gradient())
	{
		return false;
	}
	if (material != nullptr)
	{
		for (int stage=0;stage<MeshBuilderClass::MAX_STAGES;++stage)
		{
			if (material->Peek_Mapper(stage) != nullptr)
			{
				return false;
			}
		}
	}
	return true;
}

static bool Mesh_Allows_Instancing(MeshClass * mesh)
{
	if (DX8InstancingClass::Get_Pass() == DX8InstancingClass::PASS_LIT && Has_Fixed_Function_Pass(mesh))
	{
		return false;
	}
	return DX8InstancingClass::Allows_Mesh(mesh);
}

static void Record_Eligible_Groups(std::vector<DX8PolygonRendererClass *> & renderers)
{
	std::sort(renderers.begin(), renderers.end());
	size_t start = 0;
	for (size_t i=1;i<=renderers.size();++i)
	{
		if (i == renderers.size() || renderers[i] != renderers[start])
		{
			DX8MeshRendererClass::Record_Eligible_Group((int)(i - start));
			start = i;
		}
	}
	renderers.clear();
}

static bool Task_Renderer_Less(PolyRenderTaskClass * a, PolyRenderTaskClass * b)
{
	return a->Peek_Polygon_Renderer() < b->Peek_Polygon_Renderer();
}

// Draws each large enough group of meshes sharing a polygon renderer in one call, and takes them off the task list.
void DX8TextureCategoryClass::Render_Instanced_Groups(VertexMaterialClass * vmaterial)
{
	static std::vector<PolyRenderTaskClass *> candidates;
	static std::vector<PolyRenderTaskClass *> drawn;
	static std::vector<MeshClass *> meshes;
	static std::vector<DX8PolygonRendererClass *> renderers;
	static std::vector<int> counts;
	static std::vector<PolyRenderTaskClass *> pending;
	static std::vector<PolyRenderTaskClass *> rest;

	for (PolyRenderTaskClass * prt = render_task_head; prt != nullptr; prt = prt->Get_Next_Visible())
	{
		MeshClass * mesh = prt->Peek_Mesh();
		if (mesh->Get_Base_Vertex_Offset() != VERTEX_BUFFER_OVERFLOW && !prt->Peek_Polygon_Renderer()->Is_Strip() && Mesh_Allows_Instancing(mesh))
		{
			candidates.push_back(prt);
		}
	}
	std::sort(candidates.begin(), candidates.end(), Task_Renderer_Less);

	size_t start = 0;
	for (size_t i=1;i<=candidates.size();++i)
	{
		if (i < candidates.size() && candidates[i]->Peek_Polygon_Renderer() == candidates[start]->Peek_Polygon_Renderer())
		{
			continue;
		}
		// Each group shares its first mesh's lights; meshes that cannot join wait for the next group.
		pending.assign(candidates.begin() + start, candidates.begin() + i);
		const size_t drawn_before = drawn.size();
		while ((int)pending.size() >= DX8InstancingClass::MIN_GROUP_SIZE)
		{
			MeshClass * reference = pending[0]->Peek_Mesh();
			const size_t first = meshes.size();
			rest.clear();
			for (size_t j=0;j<pending.size();++j)
			{
				if (j == 0 || DX8InstancingClass::Can_Share_Group(reference, pending[j]->Peek_Mesh()))
				{
					meshes.push_back(pending[j]->Peek_Mesh());
					drawn.push_back(pending[j]);
				}
				else
				{
					rest.push_back(pending[j]);
				}
			}
			const int count = (int)(meshes.size() - first);
			if (count >= DX8InstancingClass::MIN_GROUP_SIZE)
			{
				renderers.push_back(pending[0]->Peek_Polygon_Renderer());
				counts.push_back(count);
			}
			else
			{
				meshes.resize(first);
				drawn.resize(drawn.size() - count);
			}
			pending.swap(rest);
		}
		if ((int)(i - start) >= DX8InstancingClass::MIN_GROUP_SIZE)
		{
			DX8InstancingClass::Add_Rejections(DX8InstancingClass::REJECT_LIGHTS, (int)(i - start - (drawn.size() - drawn_before)));
		}
		start = i;
	}
	candidates.clear();
	pending.clear();
	rest.clear();

	const bool instanced = !renderers.empty() &&
		DX8InstancingClass::Draw_Groups(&renderers[0], &counts[0], (int)renderers.size(), &meshes[0], vmaterial, container->Get_FVF());
	if (instanced)
	{
		for (size_t group=0;group<counts.size();++group)
		{
			DX8MeshRendererClass::Record_Instanced_Group(counts[group]);
		}
		if (DX8InstancingClass::Get_Pass() == DX8InstancingClass::PASS_LIT)
		{
			for (size_t i=0;i<drawn.size();++i)
			{
				_InstancedFragments.push_back(InstancedFragment(drawn[i]->Peek_Mesh(), drawn[i]->Peek_Polygon_Renderer()));
			}
			_InstancedFragmentsSorted = false;
		}
	}
	meshes.clear();
	renderers.clear();
	counts.clear();

	if (!instanced)
	{
		drawn.clear();
		return;
	}
	std::sort(drawn.begin(), drawn.end());

	PolyRenderTaskClass * last_prt = nullptr;
	PolyRenderTaskClass * prt = render_task_head;
	while (prt) {
		PolyRenderTaskClass * next_prt = prt->Get_Next_Visible();
		if (std::binary_search(drawn.begin(), drawn.end(), prt)) {
			if (last_prt == nullptr) {
				render_task_head = next_prt;
			} else {
				last_prt->Set_Next_Visible(next_prt);
			}
			delete prt;
		} else {
			last_prt = prt;
		}
		prt = next_prt;
	}
	drawn.clear();
}

bool DX8MeshRendererClass::Casts_Shadow(const ShaderClass& shader)
{
	const ShaderClass::SrcBlendFuncType src = shader.Get_Src_Blend_Func();
	const ShaderClass::DstBlendFuncType dst = shader.Get_Dst_Blend_Func();
	return src != ShaderClass::SRCBLEND_ZERO && dst != ShaderClass::DSTBLEND_ONE &&
		dst != ShaderClass::DSTBLEND_SRC_COLOR && dst != ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR;
}

void DX8TextureCategoryClass::Render()
{
	// The shadow depth pass writes neither depth nor colour for these, so their draws would do nothing.
	if (DX8MeshRendererClass::Get_Stats_Scene() == DX8InstancingStatsStruct::SCENE_SHADOW_DEPTH &&
		!DX8MeshRendererClass::Casts_Shadow(Get_Shader()))
	{
		Clear_Render_List();
		return;
	}

	#ifdef WWDEBUG
	if (!WW3D::Expose_Prelit()) {
	#endif

		for (unsigned i=0;i<MeshMatDescClass::MAX_TEX_STAGES;++i)
		{
			SNAPSHOT_SAY(("Set_Texture(%d,%s)",i,Peek_Texture(i) ? Peek_Texture(i)->Get_Texture_Name().str() : "null"));
			DX8Wrapper::Set_Texture(i,Peek_Texture(i));
		}

	#ifdef WWDEBUG
	}
	#endif

	SNAPSHOT_SAY(("Set_Material(%s)",Peek_Material() ? Peek_Material()->Get_Name() : "null"));
	VertexMaterialClass *vmaterial=(VertexMaterialClass *)Peek_Material();	//ugly cast from const but we'll restore it after changes so okay. -MW
	DX8Wrapper::Set_Material(vmaterial);

	SNAPSHOT_SAY(("Set_Shader(%x)",Get_Shader().Get_Bits()));
	ShaderClass theShader = Get_Shader();

	//Setup an alpha blend version of this shader just in case it's needed. -MW
	ShaderClass theAlphaShader = theShader;
	theAlphaShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	theAlphaShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	//if we want to allow other translucent polygons behind this mesh, we need to disable z-write but
	//this will cause sorting errors on this mesh.
	//theAlphaShader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);

	DX8Wrapper::Set_Shader(theShader);

	if (m_gForceMultiply && theShader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO) {
		theShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
		theShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
		DX8Wrapper::Set_Shader(theShader);
		//VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		//DX8Wrapper::Set_Material(material);
		//REF_PTR_RELEASE(material);
		DX8Wrapper::Apply_Render_State_Changes();
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
	}


	// finished tasks are kept for the bloom replay instead of being freed
	const bool keepForBloom = DX8MeshRendererClass::Is_Bloom_Capture_Enabled() && (Is_Additive() || Is_Emissive_Glow());

	// instanced groups leave the task list, so they would miss the replay
	static std::vector<DX8PolygonRendererClass *> eligibleRenderers;
	const bool categoryAllowsInstancing = Allows_Instancing() && !keepForBloom;
	if (categoryAllowsInstancing && DX8InstancingClass::Get_Pass() != DX8InstancingClass::PASS_NONE)
	{
		Render_Instanced_Groups(vmaterial);
	}

	bool renderTasksRemaining=false;

	PolyRenderTaskClass * prt = render_task_head;
	PolyRenderTaskClass * last_prt = nullptr;

	while (prt) {

		/*
		** Dig out the parameters for this render task
		*/
		MeshClass * mesh = prt->Peek_Mesh();

		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
		{	//skip this mesh so it gets rendered later after vertices are filled in.
			last_prt = prt;
			prt = prt->Get_Next_Visible();
			renderTasksRemaining = true;
			continue;
		}

		SNAPSHOT_SAY(("mesh = %s",mesh->Get_Name()));

		#ifdef WWDEBUG
		// Debug rendering: if it exists, expose prelighting on this mesh by disabling all base textures.
		if (WW3D::Expose_Prelit()) {
			switch (mesh->Peek_Model()->Get_Flag (MeshGeometryClass::PRELIT_MASK)) {

				unsigned i;

				case MeshGeometryClass::PRELIT_VERTEX:

					// Disable texturing on all stages and passes.
					for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++)
					{
						DX8Wrapper::Set_Texture (i, nullptr);
					}
					break;

				case MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_PASS:

					// Disable texturing on all but the last pass.
					if (pass == mesh->Peek_Model()->Get_Pass_Count() - 1) {
						for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++)
						{
							DX8Wrapper::Set_Texture (i, Peek_Texture (i));
						}
					} else {
						for (i = 0; i < MAX_TEXTURE_STAGES; i++) {
							DX8Wrapper::Set_Texture (i, nullptr);
						}
					}
					break;

				case MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_TEXTURE:

					// Disable texturing on all but the zeroth stage of each pass.
					DX8Wrapper::Set_Texture (0, Peek_Texture (0));
					for (i = 1; i < MeshMatDescClass::MAX_TEX_STAGES; i++)
					{
						DX8Wrapper::Set_Texture (i, nullptr);
					}
					break;

				default:
					for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++)
					{
						DX8Wrapper::Set_Texture (i, Peek_Texture (i));
					}
					break;
			}
		}
		#endif

		Render_Task(prt, vmaterial, theShader, theAlphaShader, false);

		if (!mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN))
		{
			DX8MeshRendererClass::Record_Rigid_Draw();
			if (categoryAllowsInstancing && Mesh_Allows_Instancing(mesh))
			{
				eligibleRenderers.push_back(prt->Peek_Polygon_Renderer());
			}
		}

		/*
		** Move to the next render task.  Note that the delete should be fast because prt's are pooled
		*/
		PolyRenderTaskClass * next_prt = prt->Get_Next_Visible();

		// remove from list, then delete
		if (last_prt == nullptr) {
		   render_task_head = next_prt;
		} else {
		  last_prt->Set_Next_Visible(next_prt);
		}

		if (keepForBloom)
		{
			if (bloom_task_head == nullptr)
			{
				TheDX8MeshRenderer.Add_Bloom_Category(this);
			}
			prt->Set_Next_Visible(bloom_task_head);
			bloom_task_head = prt;
		}
		else
		{
			delete prt;
		}
		prt = next_prt;
	}

	Record_Eligible_Groups(eligibleRenderers);

	if (!renderTasksRemaining)
	{
		WWASSERT(!render_task_head);
		Clear_Render_List();
	}
}

// second draw of the kept tasks into the bloom target, with the container's buffers already bound
void DX8TextureCategoryClass::Render_Bloom()
{
	const bool glow = Is_Emissive_Glow();
	for (unsigned i=0;i<MeshMatDescClass::MAX_TEX_STAGES;++i)
	{
		TextureClass *texture = Peek_Texture(i);
		if (glow)
		{
			texture = (i == 0) ? texture->Peek_Emissive_Map() : nullptr;
		}
		DX8Wrapper::Set_Texture(i,texture);
	}

	VertexMaterialClass *vmaterial=(VertexMaterialClass *)Peek_Material();
	DX8Wrapper::Set_Material(vmaterial);

	ShaderClass theShader = Get_Shader();
	if (glow)
	{
		const ShaderClass::CullModeType cullMode = theShader.Get_Cull_Mode();
		theShader = ShaderClass::_PresetAdditiveShader;
		theShader.Set_Cull_Mode(cullMode);
	}
	DX8Wrapper::Set_Shader(theShader);

	// the mask is scaled by the texture factor in place of the lit vertex colour
	if (glow)
	{
		DX8Wrapper::Apply_Render_State_Changes();
		const float intensity = WWMath::Clamp(DX8MeshRendererClass::Get_Bloom_Emissive_Intensity(), 0.0f, 1.0f);
		const unsigned level = (unsigned)(intensity * 255.0f);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, level, level, level));
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	}

	while (bloom_task_head != nullptr)
	{
		PolyRenderTaskClass* prt = bloom_task_head;
		bloom_task_head = prt->Get_Next_Visible();
		Render_Task(prt, vmaterial, theShader, theShader, true);
		delete prt;
	}

	if (glow)
	{
		ShaderClass::Invalidate();
	}
}

void DX8TextureCategoryClass::Clear_Bloom_List()
{
	while (bloom_task_head != nullptr)
	{
		PolyRenderTaskClass* next = bloom_task_head->Get_Next_Visible();
		delete bloom_task_head;
		bloom_task_head = next;
	}
}

// CONTRA_DEPTHUNSORTED=0 sends blended casters through the sorter in the shadow depth pass again, for comparing call counts.
static bool Get_Depth_Unsorted_Enabled()
{
	const char *value = getenv("CONTRA_DEPTHUNSORTED");
	return (value != nullptr) ? atoi(value) != 0 : true;
}

static const bool DepthUnsortedEnabled = Get_Depth_Unsorted_Enabled();

// draws one visible mesh fragment with the category's state already applied
// the bloom replay of a sorting container cannot go through its buffers, which would only queue the draw
static void Draw_Polygons(DX8FVFCategoryContainer * container, DX8PolygonRendererClass * renderer, MeshClass * mesh, VertexMaterialClass * vmaterial, bool replay)
{
	if (replay && container->Is_Sorting()) {
		static_cast<DX8RigidFVFCategoryContainer*>(container)->Draw_Copied(renderer,mesh->Get_Base_Vertex_Offset());
	} else {
		if (DX8SkinningClass::Is_Sweeping()) {
			DX8SkinningClass::Set_Mesh(mesh,Peek_Skinned_Model(mesh->Peek_Model())->Palette,vmaterial);
		}
		renderer->Render(mesh->Get_Base_Vertex_Offset());
	}
}

// The sorter draws after the category has moved on, so a fading mesh hands it a material of its own.
// Null means the blend or an unlit material gives the fade nothing to act on.
static VertexMaterialClass * Create_Faded_Material(VertexMaterialClass * material, const ShaderClass & shader, MeshClass * mesh)
{
	const float alpha = mesh->Get_Alpha_Override();
	if (alpha == 1.0f || material == nullptr || !material->Get_Lighting())
	{
		return nullptr;
	}

	const ShaderClass::DstBlendFuncType dst = shader.Get_Dst_Blend_Func();
	const bool fadesByAlpha = shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA;
	const bool fadesByColour = shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE &&
		(dst == ShaderClass::DSTBLEND_ONE || dst == ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR);
	if (!fadesByAlpha && !fadesByColour)
	{
		return nullptr;
	}

	VertexMaterialClass * faded = material->Clone();

	// the copy cloned the mappers, and an animated one has to keep advancing with the original
	for (int stage=0;stage<MeshBuilderClass::MAX_STAGES;++stage)
	{
		faded->Set_Mapper(material->Peek_Mapper(stage),stage);
	}

	if (fadesByAlpha)
	{
		faded->Set_Opacity(material->Get_Opacity() * alpha);
	}
	else
	{
		Vector3 diffuse;
		Vector3 emissive;
		material->Get_Diffuse(&diffuse);
		material->Get_Emissive(&emissive);
		faded->Set_Diffuse(diffuse * alpha);
		faded->Set_Emissive(emissive * mesh->Get_Emissive_Override());
	}
	return faded;
}

void DX8TextureCategoryClass::Render_Task(PolyRenderTaskClass * prt, VertexMaterialClass * vmaterial, const ShaderClass & theShader, ShaderClass theAlphaShader, bool replay)
{
	DX8PolygonRendererClass * renderer = prt->Peek_Polygon_Renderer();
	MeshClass * mesh = prt->Peek_Mesh();

	const bool emissionDisabled = mesh->Is_Emission_Disabled();
	if (emissionDisabled && (Is_Additive() || replay))
	{
		return;
	}

	/*
	** If the user is not installing LightEnvironmentClasses, we leave the lighting render
	** states untouched.  This way they can set a couple global lights that affect the entire scene.
	*/
	LightEnvironmentClass * lenv = mesh->Get_Lighting_Environment();
	if (lenv != nullptr) {
		SNAPSHOT_SAY(("LightEnvironment, lights: %d",lenv->Get_Light_Count()));
		DX8Wrapper::Set_Light_Environment(lenv);
	}
	else {
		SNAPSHOT_SAY(("No light environment"));
	}

	/*
	** Support for ALIGNED and ORIENTED camera modes
	*/
	const Matrix3D* world_transform = &mesh->Get_Transform();
	bool identity=mesh->Is_Transform_Identity();
	Matrix3D tmp_world;

	if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ALIGNED)) {
		SNAPSHOT_SAY(("Camera mode ALIGNED"));

		Vector3 mesh_position;
		Vector3 camera_z_vector;

		TheDX8MeshRenderer.Peek_Camera()->Get_Transform().Get_Z_Vector(&camera_z_vector);
		mesh->Get_Transform().Get_Translation(&mesh_position);

		tmp_world.Obj_Look_At(mesh_position,mesh_position + camera_z_vector,0.0f);
		world_transform = &tmp_world;

	} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ORIENTED)) {
		SNAPSHOT_SAY(("Camera mode ORIENTED"));

		Vector3 mesh_position;
		Vector3 camera_position;

		TheDX8MeshRenderer.Peek_Camera()->Get_Transform().Get_Translation(&camera_position);
		mesh->Get_Transform().Get_Translation(&mesh_position);

		tmp_world.Obj_Look_At(mesh_position,camera_position,0.0f);
		world_transform = &tmp_world;

	} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN)) {
		SNAPSHOT_SAY(("Set world identity (for skin)"));

		tmp_world.Make_Identity();
		world_transform = &tmp_world;
		identity=true;
	}


	if (identity) {
		SNAPSHOT_SAY(("Set_World_Identity"));
		DX8Wrapper::Set_World_Identity();
	}
	else {
		SNAPSHOT_SAY(("Set_World_Transform"));
		DX8Wrapper::Set_Transform(D3DTS_WORLD,*world_transform);
	}


//--------------------------------------------------------------------
	if (mesh->Get_ObjectScale() != 1.0f)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, TRUE);
//--------------------------------------------------------------------
	/*
	** Render mesh using either sorting or immediate pipeline
	*/
	//(gth) this if statement's contents are not tabbed to avoid perforce merge problems...
	if (!DX8RendererDebugger::Is_Enabled() || !mesh->Is_Disabled_By_Debugger()) {

	// A blended caster is cut out by alpha test in the shadow depth pass, so its draw order cannot matter there.
	const bool unsortedCaster = DepthUnsortedEnabled && !container->Is_Sorting() &&
		DX8MeshRendererClass::Get_Stats_Scene() == DX8InstancingStatsStruct::SCENE_SHADOW_DEPTH;

	if (!replay && !unsortedCaster && (!!mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
		if (mesh->Peek_Disruption() != nullptr) {
			SortingRendererClass::Set_Insert_Effects(SoftParticleHookClass::EFFECT_DISRUPT,mesh->Peek_Disruption(),mesh->Get_Alpha_Override());
			renderer->Render_Sorted(mesh->Get_Base_Vertex_Offset(),mesh->Get_Bounding_Sphere());
			SortingRendererClass::Set_Insert_Effects(0,nullptr);
		}
		if (!mesh->Is_Disruption_Only() || !SortingRendererClass::Can_Disrupt()) {
			VertexMaterialClass * faded = Create_Faded_Material(vmaterial,theShader,mesh);
			if (faded != nullptr)
			{
				DX8Wrapper::Set_Material(faded);
			}
			SortingRendererClass::Set_Insert_Effects(mesh->Get_Shader_Effects(),mesh->Peek_Shader_Effect_Data());
			renderer->Render_Sorted(mesh->Get_Base_Vertex_Offset(),mesh->Get_Bounding_Sphere());
			SortingRendererClass::Set_Insert_Effects(0,nullptr);
			if (faded != nullptr)
			{
				DX8Wrapper::Set_Material(vmaterial);
				faded->Release_Ref();
			}
		}
	} else if (!mesh->Is_Disruption_Only() || !SortingRendererClass::Can_Disrupt()) {
		//non-transparent mesh that will be rendered immediately.  Okay to adjust the shader/material
		//if necessary
		if (mesh->Get_Alpha_Override() != 1.0 || (mesh->Get_User_Data() && *(int *)mesh->Get_User_Data() == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE))
		{	//mesh has material override of some kind
			//adjust the opacity of this model
			float oldOpacity=vmaterial->Get_Opacity();

			Vector3 oldDiffuse;
			Vector3 oldEmissive;


			Vector2 oldUVOffset;
			unsigned int oldUVOffsetSyncTime;
			vmaterial->Get_Diffuse(&oldDiffuse);

			vmaterial->Get_Emissive(&oldEmissive);


			LinearOffsetTextureMapperClass *oldMapper=(LinearOffsetTextureMapperClass *)vmaterial->Peek_Mapper();
			if ( mesh->Get_User_Data() && *(int *)mesh->Get_User_Data() == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE && oldMapper && oldMapper->Mapper_ID() == TextureMapperClass::MAPPER_ID_LINEAR_OFFSET)
			{	RenderObjClass::Material_Override *matOverride=(RenderObjClass::Material_Override *)mesh->Get_User_Data();
				oldUVOffsetSyncTime = oldMapper->Get_LastUsedSyncTime();
				oldMapper->Set_LastUsedSyncTime(WW3D::Get_Sync_Time());	//make sure zero time passes for the mapper.
				oldMapper->Get_Current_UV_Offset(oldUVOffset);
				oldMapper->Set_Current_UV_Offset(matOverride->customUVOffset);
			}
			else
				oldMapper=nullptr;

			if (mesh->Get_Alpha_Override() != 1.0)
			{
				// an unlit additive layer ignores the material, so the texture factor fades it
				const bool unlitLayer = !mesh->Is_Additive() && Is_Additive() && !theShader.Uses_Primary_Gradient();
				if (unlitLayer)
				{
					theAlphaShader = theShader;
					theAlphaShader.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE);
				}
				if (mesh->Is_Additive())
				{	//additvie blended mesh can't switch to alpha or we will get a black outline.
					//so adjust diffuse color instead.
					//DEBUG_LOG((">>>DX8Renderer: ADDITIVE + ALPHA OVERRIDE - alpha = %f", mesh->Get_Alpha_Override()));
					vmaterial->Set_Diffuse(mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override());

					vmaterial->Set_Emissive(oldEmissive.X * mesh->Get_Emissive_Override(), oldEmissive.Y * mesh->Get_Emissive_Override(), oldEmissive.Z * mesh->Get_Emissive_Override());

					theAlphaShader = theShader;	//keep using additive blending.
				}
				vmaterial->Set_Opacity(mesh->Get_Alpha_Override());
				DX8Wrapper::Set_Shader(theAlphaShader);
				DX8Wrapper::Apply_Render_State_Changes();
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,(int)((float)0x60*mesh->Get_Alpha_Override()));
				if (unlitLayer)
				{
					const unsigned level = (unsigned)(WWMath::Clamp(mesh->Get_Alpha_Override(), 0.0f, 1.0f) * 255.0f);
					DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, level, level, level));
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
				}

				Draw_Polygons(container,renderer,mesh,vmaterial,replay);

				if (unlitLayer)
				{
					ShaderClass::Invalidate();
				}
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,0x60);
				vmaterial->Set_Opacity(oldOpacity);	//restore previous value
				vmaterial->Set_Diffuse(oldDiffuse.X,oldDiffuse.Y,oldDiffuse.Z);

				vmaterial->Set_Emissive(oldEmissive.X, oldEmissive.Y, oldEmissive.Z);

				DX8Wrapper::Set_Shader(theShader);	//restore previous value
			}
			else
				Draw_Polygons(container,renderer,mesh,vmaterial,replay);

			if (oldMapper)	//did we override the uv offset?
			{	oldMapper->Set_LastUsedSyncTime(oldUVOffsetSyncTime);
				oldMapper->Set_Current_UV_Offset(oldUVOffset);
			}
			DX8Wrapper::Set_Material(nullptr);	//force a reset of vertex material since we secretly changed opacity
			DX8Wrapper::Set_Material(vmaterial);	//restore previous material.
		}
		else
			Draw_Polygons(container,renderer,mesh,vmaterial,replay);
	}
//--------------------------------------------------------------------
	if (mesh->Get_ObjectScale() != 1.0f)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, FALSE);
//--------------------------------------------------------------------




	}

}

void DX8TextureCategoryClass::Clear_Render_List()
{
	while (render_task_head != nullptr)
	{
		PolyRenderTaskClass* next = render_task_head->Get_Next_Visible();
		delete render_task_head;
		render_task_head = next;
	}
}


DX8MeshRendererClass::DX8MeshRendererClass()
	:
	camera(nullptr),
	enable_lighting(true),
	texture_category_container_list_skin(nullptr),
	visible_decal_meshes(nullptr)
{
}

DX8MeshRendererClass::~DX8MeshRendererClass()
{
	Shutdown();
}

void DX8MeshRendererClass::Init()
{
	// DMS - Only allocate one if we have not already (leak fix)
	if(!texture_category_container_list_skin)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;
}

void DX8MeshRendererClass::Shutdown()
{
	camera = nullptr;
	visible_decal_meshes = nullptr;
	Invalidate(true);
	Clear_Pending_Delete_Lists();
	_TempVertexBuffer.Clear();	//free memory
	_TempNormalBuffer.Clear();
}

// ----------------------------------------------------------------------------

bool DX8MeshRendererClass::bloom_capture=false;
float DX8MeshRendererClass::bloom_emissive_intensity=0.0f;
int DX8MeshRendererClass::stats_scene=DX8InstancingStatsStruct::SCENE_MAIN;
DX8InstancingStatsStruct DX8MeshRendererClass::instancing_stats;

void DX8MeshRendererClass::Record_Eligible_Group(int draws)
{
	int size_class = 3;
	if (draws == 1)
	{
		size_class = 0;
	}
	else if (draws <= 3)
	{
		size_class = 1;
	}
	else if (draws <= 15)
	{
		size_class = 2;
	}
	instancing_stats.Scenes[stats_scene].EligibleDraws[size_class] += draws;
}

void DX8MeshRendererClass::Record_Instanced_Group(int draws)
{
	DX8InstancingStatsStruct::SceneStruct & scene = instancing_stats.Scenes[stats_scene];
	scene.RigidDraws += draws;
	scene.InstancedCalls++;
	scene.InstancedMeshes += draws;
	Record_Eligible_Group(draws);
}

void DX8MeshRendererClass::Record_Material_Pass(const MaterialPassClass* pass, int draws)
{
	// Copies of a pass count as the pass they copy.
	pass = pass->Peek_Vertex_Shading_Key();
	for (int i=0;i<DX8InstancingStatsStruct::MAX_PASSES;++i)
	{
		if (instancing_stats.Passes[i] == nullptr)
		{
			instancing_stats.Passes[i] = pass;
		}
		if (instancing_stats.Passes[i] == pass)
		{
			instancing_stats.PassDraws[i] += draws;
			return;
		}
	}
	instancing_stats.OtherPassDraws += draws;
}

void DX8MeshRendererClass::Take_Instancing_Stats(DX8InstancingStatsStruct& stats)
{
	stats = instancing_stats;
	memset(&instancing_stats, 0, sizeof(instancing_stats));
}

void DX8MeshRendererClass::Add_Bloom_Category(DX8TextureCategoryClass* category)
{
	bloom_categories.Add(category);
}

void DX8MeshRendererClass::Flush_Bloom()
{
	for (int i=0;i<bloom_categories.Count();++i) {
		DX8TextureCategoryClass* category=bloom_categories[i];
		// skins live in a per flush dynamic buffer that is gone by now
		if (category->Get_Container()->Bind_Static_Buffers()) {
			category->Render_Bloom();
		}
	}
	Clear_Bloom_Lists();

	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	DX8Wrapper::Set_Index_Buffer(nullptr,0);
}

void DX8MeshRendererClass::Clear_Bloom_Lists()
{
	for (int i=0;i<bloom_categories.Count();++i) {
		bloom_categories[i]->Clear_Bloom_List();
	}
	bloom_categories.Delete_All();
}

void DX8MeshRendererClass::Clear_Pending_Delete_Lists()
{
	// the kept tasks point into categories that may be about to go
	Clear_Bloom_Lists();

	while (DX8TextureCategoryClass* category=texture_category_delete_list.Remove_Head()) {
		delete category;
	}
	while (DX8FVFCategoryContainer* container=fvf_category_container_delete_list.Remove_Head()) {
		delete container;
	}
}

// ----------------------------------------------------------------------------

static void Add_Rigid_Mesh_To_Container(FVFCategoryList* container_list,unsigned fvf,MeshModelClass* mmc)
{
	WWASSERT(container_list);
	DX8FVFCategoryContainer * container = nullptr;
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE) &&
		!SortingRendererClass::Sorts_Meshes_Per_Object());

	FVFCategoryListIterator it(container_list);
	while (!it.Is_Done()) {
		container = it.Peek_Obj();
		if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
			container->Add_Mesh(mmc);
			return;
		}
		it.Next();
	}

	container=W3DNEW DX8RigidFVFCategoryContainer(fvf,sorting);
	container_list->Add_Tail(container);
	container->Add_Mesh(mmc);
}

// ----------------------------------------------------------------------------

void DX8MeshRendererClass::Unregister_Mesh_Type(MeshModelClass* mmc)
{
	while (DX8PolygonRendererClass* n=mmc->PolygonRendererList.Remove_Head()) {
		delete n;
	}
	_RegisteredMeshList.Remove(mmc);
	DX8SkinFVFCategoryContainer::Forget_Skinned_Model(mmc);

	// Also remove the gap filler!
	if (mmc->GapFiller) {
		GapFillerClass* gf=mmc->GapFiller;
		mmc->GapFiller=nullptr;
		delete gf;
	}

}


void DX8MeshRendererClass::Register_Mesh_Type(MeshModelClass* mmc)
{
	WWMEMLOG(MEM_GEOMETRY);
#ifdef ENABLE_CATEGORY_LOG
	RENDER_LOG(("Registering mesh: %s (%d polys, %d verts + %d gap polygons)",mmc->Get_Name(),mmc->Get_Polygon_Count(),mmc->Get_Vertex_Count(),mmc->Get_Gap_Filler_Polygon_Count()));
#endif
	bool skin=(mmc->Get_Flag(MeshModelClass::SKIN) && mmc->VertexBoneLink);
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE));

	if (skin) {

		/*
		** This mesh is a skin.  Add it to a DX8SkinFVFCategoryContainer.
		*/
		WWASSERT(texture_category_container_list_skin);

		FVFCategoryListIterator it(texture_category_container_list_skin);
		while (!it.Is_Done()) {
			DX8FVFCategoryContainer * container = it.Peek_Obj();
			if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
				container->Add_Mesh(mmc);
				return;
			}
			it.Next();
		}

		DX8FVFCategoryContainer * new_container=W3DNEW DX8SkinFVFCategoryContainer(sorting);
		texture_category_container_list_skin->Add_Tail(new_container);
		new_container->Add_Mesh(mmc);

	} else {

		/*
		** We should never try to add the same mesh model to the system twice.
		*/
		WWASSERT_PRINT(_RegisteredMeshList.Contains(mmc) == false,("Mesh name: %s",mmc->Get_Name()));

		/*
		** If the previous step didn't add the mesh, then we have to actually process this mesh
		*/
		if (!_RegisteredMeshList.Contains(mmc)) {

			unsigned fvf=DX8FVFCategoryContainer::Define_FVF(mmc,enable_lighting);

			/*
			** Search for an existing FVF Category Container that matches this mesh
			*/
			int i=0;
			for (;i<texture_category_container_lists_rigid.Count();++i) {
				FVFCategoryList * list=texture_category_container_lists_rigid[i];
				WWASSERT(list);
				DX8FVFCategoryContainer * container=list->Peek_Head();
				if (container && container->Get_FVF()!=fvf) continue;

				Add_Rigid_Mesh_To_Container(list,fvf,mmc);
				break;
			}

			if (i==texture_category_container_lists_rigid.Count()) {

				/*
				** We couldn't find an existing FVF category container so we have to add one.  Future
				** meshes that use this FVF will also be able to use this container.
				*/
				FVFCategoryList * new_fvf_category = W3DNEW FVFCategoryList();
				texture_category_container_lists_rigid.Add(new_fvf_category);
				Add_Rigid_Mesh_To_Container(new_fvf_category,fvf,mmc);
			}

			/*
			** Done processing the mesh, add its polygon renderers to the global registered mesh list
			*/
			if (mmc->PolygonRendererList.Is_Empty() == false) {
				_RegisteredMeshList.Add_Tail(mmc);
			}
			else {
				RENDER_LOG(("Error: Register_Mesh_Type failed! file: %s line: %d",__FILE__,__LINE__));
			}
		}
	}
}

static unsigned statistics_requested=0;

void DX8MeshRendererClass::Request_Log_Statistics()
{
	statistics_requested=WW3D::Get_Frame_Count();
}


// ---------------------------------------------------------------------------
//
// Render all meshes that are added to visible lists
//
// ---------------------------------------------------------------------------

static void Render_FVF_Category_Container_List(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render();
		it.Next();
	}
}

static void Render_FVF_Category_Container_List_Delayed_Passes(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render_Delayed_Procedural_Material_Passes();
		it.Next();
	}
}

void DX8MeshRendererClass::Flush()
{
	int i;

	WWPROFILE("DX8MeshRenderer::Flush");
	if (!camera) return;
	Log_Statistics_String(true);

	/*
	** Render the FVF categories.  Note that it is critical that skins be
	** rendered *after* the rigid meshes.  This is caused by the fact that an object may
	** have its base passes disabled and a translucent procedural material pass rendered
	** instead.  In this case, technically we have to delay rendering of the material pass but
	** for skins we just render these passes as we go because we can assume that the
	** bulk of the meshes have already been drawn (there would be extra overhead involved
	** in solving this for skins)
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
	}

	Render_FVF_Category_Container_List(*texture_category_container_list_skin);

	Render_Decal_Meshes();

	/*
	** Render the translucent procedural material passes that were applied to meshes that
	** had their base passes disabled.
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List_Delayed_Passes(*texture_category_container_lists_rigid[i]);
	}

	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	DX8Wrapper::Set_Index_Buffer(nullptr,0);

	_InstancedFragments.clear();
	_InstancedFragmentsSorted = true;
	_FixedFunctionPassMeshes.clear();
	_FixedFunctionPassMeshesSorted = true;
}


void DX8MeshRendererClass::Add_To_Render_List(DecalMeshClass * decalmesh)
{
	WWASSERT(decalmesh != nullptr);
	decalmesh->Set_Next_Visible(visible_decal_meshes);
	visible_decal_meshes = decalmesh;
}

void DX8MeshRendererClass::Render_Decal_Meshes()
{
	DecalMeshClass * decal_mesh = visible_decal_meshes;
	if (!decal_mesh) return;

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZBIAS,8);

	while (decal_mesh != nullptr) {
		decal_mesh->Render();
		decal_mesh = decal_mesh->Peek_Next_Visible();
	}
	visible_decal_meshes = nullptr;

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZBIAS,0);
}

// ----------------------------------------------------------------------------

static void Log_Container_List(FVFCategoryList& container_list,bool only_visible)
{
	FVFCategoryListIterator it(&container_list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Log(only_visible);
		it.Next();
	}
}

void DX8MeshRendererClass::Log_Statistics_String(bool only_visible)
{
	if (statistics_requested!=WW3D::Get_Frame_Count()) return;

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Log_Container_List(*texture_category_container_lists_rigid[i],only_visible);
	}
	Log_Container_List(*texture_category_container_list_skin,only_visible);

}

static void Invalidate_FVF_Category_Container_List(FVFCategoryList& list)
{
	while (DX8FVFCategoryContainer* fvf_category=list.Remove_Head()) {
		delete fvf_category;
	}
}

void DX8MeshRendererClass::Invalidate( bool shutdown)
{
	WWMEMLOG(MEM_RENDERER);
	_RegisteredMeshList.Reset_List();
	Clear_Bloom_Lists();

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
		delete texture_category_container_lists_rigid[i];
	}
	if (texture_category_container_list_skin) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_list_skin);
		delete texture_category_container_list_skin;
		texture_category_container_list_skin=nullptr;
	}

	if (!shutdown)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;

	texture_category_container_lists_rigid.Delete_All();
}







