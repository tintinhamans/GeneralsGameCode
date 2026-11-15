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
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include <d3d9.h>  // Native DX9

#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "Renderer.h"
#include "vertmaterial.h"
#include "texture.h"
#include "statistics.h"
#include "dx8instancing.h"	// Ronin @perf 07/11/2026 DX9: Flush_Additive_Parts
#include <WWDebug/wwprofile.h>
#include <algorithm>
#include <list>

// Ronin @diagnostic 06/11/2026 DX9: the [SORT] row's numbers; the clock reads only while the row is on and shown.
SortingRendererStats TheSortingStats = {};
static LARGE_INTEGER Sort_Start()
{
	LARGE_INTEGER t = {};
	if (TheSortingStats.enabled)
		QueryPerformanceCounter(&t);
	return t;
}
static void Sort_Lap(double &ms, LARGE_INTEGER &from)
{
	if (!TheSortingStats.enabled || from.QuadPart == 0)
		return;
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	ms += (double)(now.QuadPart - from.QuadPart) * 1000.0 / (double)freq.QuadPart;
	from = now;
}

 // Ronin @bugfix 19/01/2026 Sorting guard: reduce debug spam - only log pipeline diffs on validation failure.
static bool Should_Log_Sorting_Guard_Failure(const char* where, unsigned* outCount = nullptr)
{
	// Best-effort: dedupe by pointer identity of string literals
	struct Slot { const char* where; unsigned count; };
	static Slot s_slots[64] = {};
	static unsigned s_used = 0;

	for (unsigned i = 0; i < s_used; ++i) {
		if (s_slots[i].where == where) {
			s_slots[i].count++;
			if (outCount) *outCount = s_slots[i].count;
			// log first few, then every 128th
			return (s_slots[i].count <= 5) || ((s_slots[i].count % 128) == 0);
		}
	}

	if (s_used < 64) {
		s_slots[s_used++] = { where, 1 };
		if (outCount) *outCount = 1;
		return true;
	}

	// Table full: still log occasionally
	static unsigned s_fallback = 0;
	s_fallback++;
	if (outCount) *outCount = s_fallback;
	return (s_fallback <= 5) || ((s_fallback % 128) == 0);
}

 // Ronin @bugfix 2/12/2025: RAII guard for sorting renderer state isolation
 // Ronin @bugfix Ronin 19/01/2026 Sorting guard: capture pipeline snapshot; clear wrapper tracking before reconstructing IA to avoid wrapper/device mode mismatch
class ScopedSortingStateGuard {
	IDirect3DDevice9* m_dev;

	// IA state to restore
	IDirect3DVertexDeclaration9* m_savedDecl = nullptr;
	DWORD m_savedFVF = 0;
	IDirect3DVertexBuffer9* m_savedVB = nullptr;
	UINT m_savedVBOffset = 0;
	UINT m_savedVBStride = 0;
	IDirect3DIndexBuffer9* m_savedIB = nullptr;

	// Transforms and viewport only (safe to restore via wrapper/device)
	Matrix4x4 m_savedWorld;
	Matrix4x4 m_savedView;
	Matrix4x4 m_savedProjection;
	D3DVIEWPORT9 m_savedViewport;

#ifdef _DEBUG
	const char* m_where;
	PipelineStateSnapshot* m_snapshot = nullptr;
#endif

public:
	ScopedSortingStateGuard(IDirect3DDevice9* dev, const char* where)
		: m_dev(dev)
#ifdef _DEBUG
		, m_where(where)
#endif
	{
		if (!m_dev) return;

/*#ifdef _DEBUG
		// Capture snapshot but DON'T spam log; validate only on failure in dtor.
		m_snapshot = DX8Wrapper::Capture_Pipeline_State(where);
		WWDEBUG_SAY(("CAPTURING PIPELINE STATE"));
#endif*/

		// Capture IA state
		//m_dev->GetVertexDeclaration(&m_savedDecl);
		m_dev->GetFVF(&m_savedFVF);
		m_dev->GetStreamSource(0, &m_savedVB, &m_savedVBOffset, &m_savedVBStride);
		m_dev->GetIndices(&m_savedIB);

		// Capture transforms
		DX8Wrapper::Get_Transform(D3DTS_WORLD, m_savedWorld);
		DX8Wrapper::Get_Transform(D3DTS_VIEW, m_savedView);
		DX8Wrapper::Get_Transform(D3DTS_PROJECTION, m_savedProjection);

		// Capture viewport
		m_dev->GetViewport(&m_savedViewport);
	}

	~ScopedSortingStateGuard() {
		if (!m_dev) return;

		// CRITICAL: Clear wrapper state tracking BEFORE restoring
		// This ensures next pass rebinds its intended layout
		DX8Wrapper::Clear_Current_Decl();
		DX8Wrapper::Clear_Current_FVF();

		// Restore transforms and viewport via wrapper/device.
		DX8Wrapper::Set_Transform(D3DTS_PROJECTION, m_savedProjection);
		DX8Wrapper::Set_Transform(D3DTS_VIEW, m_savedView);
		DX8Wrapper::Set_Transform(D3DTS_WORLD, m_savedWorld);
		m_dev->SetViewport(&m_savedViewport);

		// Restore stream0 + IB (device-side)
		m_dev->SetStreamSource(0, m_savedVB, m_savedVBOffset, m_savedVBStride);
		if (m_savedVB) {
			m_savedVB->Release();
			m_savedVB = nullptr;
		}

		m_dev->SetIndices(m_savedIB);
		if (m_savedIB) {
			m_savedIB->Release();
			m_savedIB = nullptr;
		}

		// DO NOT restore FVF/Decl - let next pass set what it needs
		// The Clear_Current_* calls above ensure clean state
		/*
		// Release the saved declaration reference if we have one
		if (m_savedDecl != nullptr) {
			m_savedDecl->Release();
			m_savedDecl = nullptr;
		}*/

#ifdef _DEBUG
		if (m_snapshot) {
			const bool ok = DX8Wrapper::Validate_Pipeline_State_Restored(m_snapshot, m_where);
			if (!ok) {
				unsigned c = 0;
				if (Should_Log_Sorting_Guard_Failure(m_where, &c)) {
					WWDEBUG_SAY(("[SORTING GUARD] Pipeline restore FAILED at %s (count=%u)", m_where, c));
				}
			}
			delete m_snapshot;
			m_snapshot = nullptr;
		}
#endif
	}
};



bool SortingRendererClass::_EnableTriangleDraw=true;
// Ronin @feature 13/11/2026 DX9: the next insert's opacity (dx8renderer sets it round Render_Sorted for a faded mesh).
static float s_insertOpacity = 1.0f;
void SortingRendererClass::Set_Insert_Opacity(float opacity) { s_insertOpacity = opacity; }
bool TheSortedMeshWhole = true;	// Ronin @feature 14/11/2026 DX9: a lit mesh draws as one run in the pool; `meshsort` (A/B)
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
	float key;	// Ronin @feature 14/11/2026 DX9: the depth it sorts by while TheSortedMeshWhole is on - its mesh's mean for a lit mesh
};

bool operator <(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z < r.z; }
bool operator <=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z <= r.z; }
bool operator >(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z > r.z; }
bool operator >=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z >= r.z; }
bool operator ==(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z == r.z; }
// ----------------------------------------------------------------------------
static
void InsertionSort(TempIndexStruct *begin, TempIndexStruct *end)
{
	for (TempIndexStruct *iter = begin + 1; iter < end; ++iter) {
		TempIndexStruct val = iter[0];
		TempIndexStruct *insert = iter;
		while (insert != begin && insert[-1] > val) {
			insert[0] = insert[-1];
			insert -= 1;
		}
		insert[0] = val;
	}
}

// ----------------------------------------------------------------------------
static
void Sort(TempIndexStruct *begin, TempIndexStruct *end)
{
	const int diff = end - begin;
	if (diff <= 16) {
		// Insertion sort has less overhead for small arrays
		InsertionSort(begin, end);
	} else {
		// Choose the median of begin, mid, and (end - 1) as the partitioning element.
		// Rearrange so that *(begin + 1) <= *begin <= *(end - 1).  These will be guard
		// elements.
		TempIndexStruct *mid = begin + diff/2;
		std::swap(mid[0], begin[1]);
		if (begin[1] > end[-1]) {
			std::swap(begin[1], end[-1]);
		}
		if (begin[0] > end[-1]) {
			std::swap(begin[0], end[-1]);
		}
		if (begin[1] > begin[0]) {
			std::swap(begin[1], begin[0]);
		}

		// *begin is now the partitioning element
		TempIndexStruct *begin1 = begin + 1;	// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *end1 = end - 1;			// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *left = begin + 1;
		TempIndexStruct *right = end - 1;
		for (;;) {
#if 0		// TODO: Temp fix until I find out who is passing me NaN.
			do ++left; while (left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right[0] > begin[0]);	// Scan down to find element <= than partition
#else
			do ++left; while (left < end1 && left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right > begin1 && right[0] > begin[0]);	// Scan down to find element <= than partition
#endif
			if (right < left) break;									// Pointers crossed.  Partitioning completed.
			std::swap(left[0], right[0]);							// Exchange elements.
		}
		std::swap(begin[0], right[0]);							// Insert partition element

		// Sort the smaller subarray first then the larger
		if (right - begin > end - (right + 1)) {
			Sort(right + 1, end);
			Sort(begin, right);
		} else {
			Sort(begin, right);
			Sort(right + 1, end);
		}
	}
}

// ----------------------------------------------------------------------------

class SortingNodeStruct
{
	W3DMPO_CODE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb
	float fade_opacity;					// Ronin @feature 13/11/2026 DX9: a faded mesh's opacity, 1 = unfaded (Set_Insert_Opacity)
};

typedef std::list<SortingNodeStruct*> SortingNodeStructList;
static SortingNodeStructList sorted_list;
static SortingNodeStructList unsorted_list;
static SortingNodeStructList clean_list;
static unsigned total_sorting_vertices;

static SortingNodeStruct* Get_Sorting_Struct()
{
	if (!clean_list.empty()) {
		SortingNodeStruct* state = clean_list.front();
		clean_list.pop_front();
		return state;
	}
	return W3DNEW SortingNodeStruct();
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)",
		start_index,polygon_count,min_vertex_index,vertex_count));


	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;
	state->fade_opacity = s_insertOpacity;	// Ronin @feature 13/11/2026 DX9: a faded mesh's opacity (Set_Insert_Opacity)

	if (bounding_sphere.Is_Valid())
	{
		const Matrix4x4 mtx=To_Matrix4x4(state->sorting_state.view)*To_Matrix4x4(state->sorting_state.world);
		Matrix4x4::Transform_Vector(mtx,bounding_sphere.Center,&state->transformed_center);

		LARGE_INTEGER insertStart = Sort_Start();	// Ronin @diagnostic 06/11/2026 DX9: [SORT] the sorted-list scan
		Insert_To_Sorted_List(state);
		Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_INSERT], insertStart);
	}
	else
	{
		// TheSuperHackers @perf stephanmeesters 04/07/2026 Nodes without bounding information do not require sorting.
		state->transformed_center = Vector3(0.0f, 0.0f, 0.0f);
		unsorted_list.push_back(state);
	}

#ifdef DEBUG_CRASHING
	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	unsigned short* indices=nullptr;
	SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
	WWASSERT(index_buffer);
	indices=index_buffer->index_buffer;
	WWASSERT(indices);
	indices+=state->start_index;
	indices+=state->sorting_state.iba_offset;

	for (int i=0;i<state->polygon_count;++i) {
		unsigned short idx1=indices[i*3]-state->min_vertex_index;
		unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
		unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
		WWASSERT(idx1<state->vertex_count);
		WWASSERT(idx2<state->vertex_count);
		WWASSERT(idx3<state->vertex_count);
	}
#endif // DEBUG_CRASHING
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Insert_Triangles(SphereClass(),start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i)
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
	// Ronin @bugfix 08/10/2026 DX9: the copied state also holds a D3D reference on the dynamic VB. Pooled nodes kept
	// it for good, so the buffer outlived DynamicVBAccessClass::_Deinit and every device reset failed.
	if (state->sorting_state.vba_d3d_vb) {
		state->sorting_state.vba_d3d_vb->Release();
		state->sorting_state.vba_d3d_vb = nullptr;
	}
}

static unsigned overlapping_node_count;
static unsigned overlapping_polygon_count;
static unsigned overlapping_vertex_count;
static const unsigned MAX_OVERLAPPING_NODES=4096;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorted_List(SortingNodeStruct *state)
{
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	for (SortingNodeStructList::iterator node = sorted_list.begin(); node != sorted_list.end(); ++node)
	{
		if (state->transformed_center.Z > (*node)->transformed_center.Z) {
			sorted_list.insert(node, state);
			return;
		}
	}

	sorted_list.push_back(state);
}

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		Release_Refs(state);
		delete state;
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_vertex_count+=state->vertex_count;
	overlapping_polygon_count+=state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

// Ronin @diagnostic 02/08/2026 DX9: classify a sorted draw by blend mode for the [DRAW] HUD.
// Additive is commutative, so those draws don't need the depth-sorted pool -- see Windowednew.md §19b.1.
static Debug_Statistics::DrawSubsystem Classify_Sorted_Draw(const ShaderClass& shader)
{
	const ShaderClass::SrcBlendFuncType src = shader.Get_Src_Blend_Func();
	const ShaderClass::DstBlendFuncType dst = shader.Get_Dst_Blend_Func();

	// ONE/ONE is pure additive; SRC_ALPHA/ONE is the premultiplied-style additive particle shaders use.
	if (dst == ShaderClass::DSTBLEND_ONE &&
		(src == ShaderClass::SRCBLEND_ONE || src == ShaderClass::SRCBLEND_SRC_ALPHA)) {
		return Debug_Statistics::DRAW_SUBSYS_SORTED_ADD;
	}
	if (src == ShaderClass::SRCBLEND_SRC_ALPHA && dst == ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA) {
		return Debug_Statistics::DRAW_SUBSYS_SORTED_ALPHA;
	}
	return Debug_Statistics::DRAW_SUBSYS_SORTED;
}

// Ronin @perf 06/11/2026 DX9: from TSH #3557. Exactly what Apply_Render_State sets; a lit material never merges.
static bool Same_Render_State(const RenderStateStruct& a, const RenderStateStruct& b)
{
	if (a.shader.Get_Bits()!=b.shader.Get_Bits() || a.material!=b.material || a.material->Get_Lighting())
		return false;

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) {
		if (a.Textures[i]!=b.Textures[i])
			return false;
	}

	return memcmp(&a.world, &b.world, sizeof(a.world))==0 && memcmp(&a.view, &b.view, sizeof(a.view))==0;
}

// Ronin @diagnostic 14/11/2026 DX9: [SORT] why Same_Render_State said no, first reason in its order: a lit mesh on either side (the rotors),
// another transform (a mesh; particles all draw at the identity), the shader, the material, a texture alone (what an atlas would merge).
static unsigned Classify_Run_Break(const RenderStateStruct& a, const RenderStateStruct& b)
{
	if (a.material->Get_Lighting() || (b.material != nullptr && b.material->Get_Lighting()))
		return SortingRendererStats::BREAK_LIT;
	if (memcmp(&a.world, &b.world, sizeof(a.world)) != 0 || memcmp(&a.view, &b.view, sizeof(a.view)) != 0)
		return SortingRendererStats::BREAK_TRANSFORM;
	if (a.shader.Get_Bits() != b.shader.Get_Bits())
		return SortingRendererStats::BREAK_SHADER;
	if (a.material != b.material)
		return SortingRendererStats::BREAK_MATERIAL;
	return SortingRendererStats::BREAK_TEXTURE;
}

static void Apply_Render_State(RenderStateStruct& render_state)
{
	DX8Wrapper::Set_Shader(render_state.shader);

	DX8Wrapper::Set_Material(render_state.material);

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i)
	{
		DX8Wrapper::Set_Texture(i,render_state.Textures[i]);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW,render_state.view);


	if (!render_state.material->Get_Lighting())
		return;	//no point changing lights if they are ignored.
  //prevLight = render_state.lightsHash;

	if (render_state.LightEnable[0]) {
		DX8Wrapper::Set_DX8_Light(0,&render_state.Lights[0]);
		if (render_state.LightEnable[1]) {
			DX8Wrapper::Set_DX8_Light(1,&render_state.Lights[1]);
			if (render_state.LightEnable[2]) {
				DX8Wrapper::Set_DX8_Light(2,&render_state.Lights[2]);
				if (render_state.LightEnable[3]) {
					DX8Wrapper::Set_DX8_Light(3,&render_state.Lights[3]);
				}
				else {
					DX8Wrapper::Set_DX8_Light(3,nullptr);
				}
			}
			else {
				DX8Wrapper::Set_DX8_Light(2,nullptr);
			}
		}
		else {
			DX8Wrapper::Set_DX8_Light(1,nullptr);
		}
	}
	else {
		DX8Wrapper::Set_DX8_Light(0,nullptr);
	}


}

// ----------------------------------------------------------------------------

// Ronin @feature 13/11/2026 DX9: a faded node's material - its opacity scaled for this run's draw (a lit mesh takes its alpha from it), then
// put back; the vertex alpha was scaled at the copy (an unlit mesh, or one lit from its vertex colours). One of the two reaches the pixel.
static void Apply_Node_Fade(const SortingNodeStruct* state)
{
	VertexMaterialClass* const material = state->sorting_state.material;
	if (state->fade_opacity >= 1.0f || material == nullptr) {
		return;
	}
	const float opacity = material->Get_Opacity();
	material->Set_Opacity(opacity * state->fade_opacity);
	DX8Wrapper::Apply_Render_State_Changes();	// the faded material is on the device; the next run's Set_Material puts the real one back
	material->Set_Opacity(opacity);
}

// Ronin @perf 09/08/2026 §19b.1 DX9: is this node's blend ORDER-INDEPENDENT? Additive (ONE/ONE and
// SRC_ALPHA/ONE) is commutative, so those triangles may be reordered relative to each other. A DEPTH
// WRITE is never commutative and disqualifies a node regardless of blend mode. Reads ShaderClass ONLY,
// deliberately NOT the [DRAW] diagnostic enum: a correctness decision must not depend on instrumentation,
// and this way the batching ports to trees that have no Debug_Statistics draw buckets.
static bool Sorted_Node_Is_Order_Independent(const ShaderClass& shader)
{
	const ShaderClass::SrcBlendFuncType src = shader.Get_Src_Blend_Func();
	const ShaderClass::DstBlendFuncType dst = shader.Get_Dst_Blend_Func();
	const bool additive = (dst == ShaderClass::DSTBLEND_ONE &&
		(src == ShaderClass::SRCBLEND_ONE || src == ShaderClass::SRCBLEND_SRC_ALPHA));
	return additive && (shader.Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_DISABLE);
}

// Ronin @perf 09/08/2026 §19b.1 DX9: ONE switch for the additive-batching experiment. false = pure
// depth order. The correctness argument lives with the block inside Flush_Sorting_Pool.
static const bool BATCH_ADDITIVE_SORTED_DRAWS = true;

// Which pooled nodes are additive (commutative)? Rebuilt every flush. File-scope so the 4KB stays off
// the stack, matching how overlapping_nodes[] is held.
static bool node_is_additive[MAX_OVERLAPPING_NODES];

// Group an additive run by node. stable_sort keeps each node's triangles in their existing relative
// depth order, which costs nothing and keeps the result deterministic frame to frame.
static bool Additive_Node_Less(const TempIndexStruct& a, const TempIndexStruct& b)
{
	return a.idx < b.idx;
}

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	// Ronin @diagnostic 06/11/2026 DX9: [SORT] this pool's size, and a clock for its steps.
	TheSortingStats.nodes += overlapping_node_count;
	TheSortingStats.triangles += overlapping_polygon_count;
	LARGE_INTEGER step = Sort_Start();

/*#ifdef WWDEBUG
	// Ronin @debug 03/02/2026: Log what state sorting flush applies (to compare with skin path)
	static int s_sortFlushLog = 0;
	if (s_sortFlushLog < 1000) {
		++s_sortFlushLog;

		IDirect3DDevice9* dev = DX8Wrapper::_Get_D3D_Device8();
		if (dev) {
			DWORD zEnable = 0, zWrite = 0, lighting = 0, cullMode = 0;
			DWORD alphaBlend = 0, srcBlend = 0, dstBlend = 0;
			dev->GetRenderState(D3DRS_ZENABLE, &zEnable);
			dev->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
			dev->GetRenderState(D3DRS_LIGHTING, &lighting);
			dev->GetRenderState(D3DRS_CULLMODE, &cullMode);
			dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend);
			dev->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
			dev->GetRenderState(D3DRS_DESTBLEND, &dstBlend);

			WWDEBUG_SAY(("[SORTING-FLUSH] #%d BEFORE: Z=%u ZW=%u Light=%u Cull=%u AB=%u Src=%u Dst=%u nodeCount=%u",
				s_sortFlushLog,
				zEnable, zWrite, lighting, cullMode, alphaBlend, srcBlend, dstBlend,
				overlapping_node_count));
		}
	}
#endif*/

	SNAPSHOT_SAY(("SortingSystem - Flush"));

	// Ronin @bugfix 2/12/2025: State capture BEFORE flush
/*#ifdef _DEBUG
	PipelineStateSnapshot* preFlushState = CAPTURE_PIPELINE_STATE();
	WWDEBUG_SAY(("CAPTURING BEFORE [SORTING-FLUSH]"));
#endif*/

	// Fill dynamic index buffer with sorting index buffer vertices
	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_polygon_count);

	unsigned vertexAllocCount = overlapping_vertex_count;
	if (DynamicVBAccessClass::Get_Default_Vertex_Count() < DEFAULT_SORTING_VERTEX_COUNT)
		vertexAllocCount = DEFAULT_SORTING_VERTEX_COUNT;	//make sure that we force the DX8 dynamic vertex buffer to maximum size
	if (overlapping_vertex_count > vertexAllocCount)
		vertexAllocCount = overlapping_vertex_count;
	WWASSERT(DEFAULT_SORTING_VERTEX_COUNT == 1 || vertexAllocCount <= DEFAULT_SORTING_VERTEX_COUNT);
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertexAllocCount/*overlapping_vertex_count*/);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* dest_verts=(VertexFormatXYZNDUV2 *)lock.Get_Formatted_Vertex_Array();

		unsigned polygon_array_offset=0;
		unsigned vertex_array_offset=0;
		for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			VertexFormatXYZNDUV2* src_verts=nullptr;
			SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
			WWASSERT(vertex_buffer);
			src_verts=vertex_buffer->VertexBuffer;
			WWASSERT(src_verts);
			src_verts+=state->sorting_state.vba_offset;
			src_verts+=state->sorting_state.index_base_offset;
			src_verts+=state->min_vertex_index;

			// If you have a crash in here and "dest_verts" points to illegal memory area,
			// it is because D3D is in illegal state, and the only known cure is rebooting.
			// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
			// Ronin @feature 13/11/2026 DX9: a faded node's vertices built from the source, alpha scaled (Apply_Node_Fade does its material's) -
			// dest_verts is the locked dynamic buffer, write-combined: never read back from it, one whole write per vertex.
			if (state->fade_opacity < 1.0f) {
				for (unsigned v = 0; v < state->vertex_count; ++v) {
					VertexFormatXYZNDUV2 faded = src_verts[v];
					const unsigned alpha = (unsigned)((float)(faded.diffuse >> 24) * state->fade_opacity + 0.5f);
					faded.diffuse = (faded.diffuse & 0x00FFFFFFu) | (alpha << 24);
					dest_verts[v] = faded;
				}
			} else {
				memcpy(dest_verts, src_verts, sizeof(VertexFormatXYZNDUV2)*state->vertex_count);
			}
			dest_verts += state->vertex_count;

			const Matrix4x4 mtx=To_Matrix4x4(state->sorting_state.view)*To_Matrix4x4(state->sorting_state.world);

			unsigned short* indices=nullptr;
			SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
			WWASSERT(index_buffer);
			indices=index_buffer->index_buffer;
			WWASSERT(indices);
			indices+=state->start_index;
			indices+=state->sorting_state.iba_offset;

			if (mtx[2][0] == 0.0f && mtx[2][1] == 0.0f && mtx[2][3] == 0.0f && mtx[2][2] == 1.0f) {
				// The common case for particle systems.
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			} else {
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (mtx[2][0]*(v1->x + v2->x + v3->x) +
												mtx[2][1]*(v1->y + v2->y + v3->y) +
												mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[2][3];
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			}

			state->min_vertex_index=vertex_array_offset;

			polygon_array_offset+=state->polygon_count;
			vertex_array_offset+=state->vertex_count;
		}
	}

	Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_BUILD], step);	// Ronin @diagnostic 06/11/2026 DX9: [SORT] vertex copy and depths
	// Ronin @feature 14/11/2026 DX9: a lit mesh (a rotor disc) takes one place in the depth order - its triangles' mean depth - and draws
	// as one run, its own triangles still back to front, so the particles either side merge again; 2003 sorts each triangle alone.
	bool wholeMeshes = false;
	if (TheSortedMeshWhole) {
		unsigned first = 0;
		for (unsigned node_id = 0; node_id < overlapping_node_count; ++node_id) {
			const SortingNodeStruct* node = overlapping_nodes[node_id];
			const unsigned count = node->polygon_count;
			const bool whole = node->sorting_state.material != nullptr && node->sorting_state.material->Get_Lighting();
			float mean = 0.0f;
			for (unsigned k = 0; whole && k < count; ++k)
				mean += tis[first + k].z;
			mean = (count > 0) ? mean / (float)count : 0.0f;
			for (unsigned k = 0; k < count; ++k) {
				TempIndexStruct& t = tis[first + k];
				if (!(t.z > -1.0e30f && t.z < 1.0e30f))
					t.z = 0.0f;	// NaN or infinite: std::sort needs a strict order
				t.key = (whole && mean > -1.0e30f && mean < 1.0e30f) ? mean : t.z;
			}
			wholeMeshes = wholeMeshes || whole;
			first += count;
		}
	}
	if (wholeMeshes) {
		std::sort(tis, tis + overlapping_polygon_count, [](const TempIndexStruct& a, const TempIndexStruct& b) {
			if (a.key != b.key) return a.key < b.key;
			if (a.idx != b.idx) return a.idx < b.idx;
			return a.z < b.z;
		});
	} else {
		Sort(tis, tis + overlapping_polygon_count);
	}
	Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_SORT], step);

	// Ronin @perf 09/08/2026 §19b.1 DX9: additive blending (ONE/ONE, SRC_ALPHA/ONE) is COMMUTATIVE, so
	// those triangles need no depth order relative to EACH OTHER. The z-sort above interleaves nodes and
	// the draw loop below emits a draw on every node CHANGE — that is why ~740 particles cost ~470 draws.
	// Within each maximal run of CONSECUTIVE additive triangles, re-sort by node so every node in that run
	// collapses to ONE draw. A run ENDS at any non-additive triangle, so nothing is reordered across an
	// alpha triangle: additive commutes with additive, NOT with alpha-over. Depth order between runs is
	// untouched -> the image is unchanged. Flip BATCH_ADDITIVE_SORTED_DRAWS to false to A/B it.
	if (BATCH_ADDITIVE_SORTED_DRAWS) {
		for (unsigned n = 0; n < overlapping_node_count; ++n) {
			node_is_additive[n] = Sorted_Node_Is_Order_Independent(overlapping_nodes[n]->sorting_state.shader);
		}

		unsigned run_start = 0;
		while (run_start < overlapping_polygon_count) {
			if (!node_is_additive[tis[run_start].idx]) {
				++run_start;
				continue;
			}
			unsigned run_end = run_start + 1;
			while (run_end < overlapping_polygon_count && node_is_additive[tis[run_end].idx]) {
				++run_end;
			}
			if (run_end - run_start > 1) {
				std::stable_sort(tis + run_start, tis + run_end, Additive_Node_Less);
			}
			run_start = run_end;
		}
	}
	Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_REGROUP], step);	// Ronin @diagnostic 06/11/2026 DX9: [SORT] the additive regroup

	// TheSuperHackers @fix stephanmeesters 10/06/2026
	// Split rendering into chunks to prevent a crash when exceeding the 16-bit index buffer limit.
	constexpr const unsigned MAX_INDEX_CHUNK = 65535;
	unsigned chunkOffset = 0;
	while (chunkOffset < overlapping_polygon_count)
	{
		unsigned chunkCount = overlapping_polygon_count - chunkOffset;
		if (chunkCount * 3 > MAX_INDEX_CHUNK) {
			chunkCount = MAX_INDEX_CHUNK / 3;
		}
		const unsigned chunkEnd = chunkOffset + chunkCount;

		DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,chunkCount*3);
		{
			DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
			ShortVectorIStruct* sorted_polygon_index_array=(ShortVectorIStruct*)lock.Get_Index_Array();

			for (unsigned a=0;a<chunkCount;++a) {
				sorted_polygon_index_array[a]=tis[chunkOffset + a].tri;
			}
		}

		// Set index buffer and render!

		DX8Wrapper::Set_Index_Buffer(dyn_ib_access,0); // Override with this buffer (do something to prevent need for this!)
		DX8Wrapper::Set_Vertex_Buffer(dyn_vb_access); // Override with this buffer (do something to prevent need for this!)

   		// Ronin @bugfix 03/12/2025: Bind FVF layout before Apply
		DX8Wrapper::BindLayoutFVF(dyn_vb_access.FVF_Info().Get_FVF(), "SortingRenderer::Flush_Sorting_Pool");

		DX8Wrapper::Apply_Render_State_Changes();

/*#ifdef WWDEBUG
	if (s_sortFlushLog <= 1000) {
		IDirect3DDevice9* dev = DX8Wrapper::_Get_D3D_Device8();
		if (dev) {
			DWORD zEnable = 0, zWrite = 0, lighting = 0, cullMode = 0;
			DWORD alphaBlend = 0, srcBlend = 0, dstBlend = 0;
			dev->GetRenderState(D3DRS_ZENABLE, &zEnable);
			dev->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
			dev->GetRenderState(D3DRS_LIGHTING, &lighting);
			dev->GetRenderState(D3DRS_CULLMODE, &cullMode);
			dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend);
			dev->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
			dev->GetRenderState(D3DRS_DESTBLEND, &dstBlend);

			WWDEBUG_SAY(("[SORTING-FLUSH] AFTER Apply: Z=%u ZW=%u Light=%u Cull=%u AB=%u Src=%u Dst=%u",
				zEnable, zWrite, lighting, cullMode, alphaBlend, srcBlend, dstBlend));
		}
	}
#endif*/

		// TheSuperHackers @performance stephanmeesters 18/10/2026 Draw polygons that in the same call when they share the same render state.
		for (unsigned start=chunkOffset;start<chunkEnd;) {
			SortingNodeStruct* state=overlapping_nodes[tis[start].idx];
			unsigned min_vertex=state->min_vertex_index;
			unsigned end_vertex=min_vertex + state->vertex_count;
			unsigned end=start + 1;
			for (;end<chunkEnd;++end) {
				SortingNodeStruct* node=overlapping_nodes[tis[end].idx];
				if (node!=state && !Same_Render_State(state->sorting_state, node->sorting_state))
					break;
				min_vertex=std::min<unsigned>(min_vertex, node->min_vertex_index);
				end_vertex=std::max<unsigned>(end_vertex, node->min_vertex_index + node->vertex_count);
			}
			// Ronin @diagnostic 14/11/2026 DX9: [SORT] this run, and what ended it (Classify_Run_Break); a chunk's end is not counted.
			if (TheSortingStats.enabled) {
				++TheSortingStats.draws;
				if (end < chunkEnd)
					++TheSortingStats.breaks[Classify_Run_Break(state->sorting_state, overlapping_nodes[tis[end].idx]->sorting_state)];
			}

			Apply_Render_State(state->sorting_state);
			Apply_Node_Fade(state);	// Ronin @feature 13/11/2026 DX9: a faded mesh's material alpha, for this run

			// Ronin @diagnostic 06/11/2026 DX9: the [DRAW] bucket by blend; the merged nodes share it.
			Debug_Statistics::Set_Draw_Subsystem(Classify_Sorted_Draw(state->sorting_state.shader));

			DX8Wrapper::Draw_Triangles((start - chunkOffset)*3, end - start, min_vertex, end_vertex - min_vertex);
			start=end;
		}

		chunkOffset += chunkCount;
	}
	Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_DRAW], step);	// Ronin @diagnostic 06/11/2026 DX9: [SORT] index fill, state and draws

	// Release all references and return nodes back to the clean list for the frame...
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.push_front(state);
	}
	overlapping_node_count=0;
	overlapping_polygon_count=0;
	overlapping_vertex_count=0;

	SNAPSHOT_SAY(("SortingSystem - Done flushing"));

/*#ifdef _DEBUG
	// @bugfix Ronin 27/02/2026 DX9: Delete captured pipeline snapshot to avoid COM ref leak
	if (preFlushState) {
		delete preFlushState;
		preFlushState = nullptr;
	}
#endif*/
}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	// Ronin @diagnostic 02/08/2026 DX9: particles + translucent land here; per-draw blend split below.
	Debug_Statistics::DrawSubsystemScope drawTag(Debug_Statistics::DRAW_SUBSYS_SORTED);

	WWPROFILE("SortingRenderer::Flush");

	// Ronin @diagnostic 06/11/2026 DX9: [SORT] the whole flush: the direct draws, then the pool.
	LARGE_INTEGER flushStart = Sort_Start();

	IDirect3DDevice9* pDev = DX8Wrapper::_Get_D3D_Device8();



	ScopedSortingStateGuard stateGuard(pDev, "SortingRendererClass::Flush");


	Matrix4x4 old_view;
	Matrix4x4 old_world;
	Renderer::Get_Transform(RenderTransform_View,old_view);
	Renderer::Get_Transform(RenderTransform_World,old_world);

	// TheSuperHackers @perf stephanmeesters 04/07/2026
	// Splice nodes that have no bounding information (Z=0.0) at the correct location into the sorted list.
	SortingNodeStructList::iterator node = sorted_list.begin();
	while (node != sorted_list.end() && (*node)->transformed_center.Z > 0.0f) {
		++node;
	}
	sorted_list.splice(node, unsorted_list);

	while (!sorted_list.empty()) {
		SortingNodeStruct* state = sorted_list.front();
		sorted_list.pop_front();

		if ((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			// Ronin @bugfix 06/12/2025: Guard IA for direct draw path. This prevents non-sorting nodes from polluting IA state
			ScopedSortingStateGuard directDrawGuard(pDev, "SortingRenderer::DirectDraw");

			// Vanilla: apply render state (no VB/IB), then bind VB/IB explicitly, then Apply and draw
			Apply_Render_State(state->sorting_state);

			// Bind original buffers via pointer overloads
			DX8Wrapper::Set_Index_Buffer(
				static_cast<IndexBufferClass*>(state->sorting_state.index_buffer),
				state->sorting_state.index_base_offset);

			DX8Wrapper::Set_Vertex_Buffer(
				static_cast<VertexBufferClass*>(state->sorting_state.vertex_buffers[0]),
				/*stream*/ 0);

			/*
	   // If this draw is fixed-function (common for particles/2D-style), clear decl and set FVF from VB
     if (state->sorting_state.currentDecl == nullptr) {
        IDirect3DDevice9* devFF = pDev;
        devFF->SetVertexDeclaration(NULL);
        const DWORD fvf = state->sorting_state.currentFVF; // should be set in render_state
        devFF->SetFVF(fvf);
			}*/

		 // @bugfix Ronin 14/01/2026 Avoid direct device IA mutations; bind fixed-function layout via wrapper tracking
		 if (state->sorting_state.currentDecl == nullptr) {
			 DWORD fvf = state->sorting_state.currentFVF;

			 // If render_state didn't carry an FVF, derive it from the bound VB (DX8 VB types only)
			 if (fvf == 0 &&
				 state->sorting_state.vertex_buffers[0] &&
				 (state->sorting_state.vertex_buffer_types[0] == BUFFER_TYPE_DX8 ||
					 state->sorting_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8)) {

				 DX8VertexBufferClass* vb0 = static_cast<DX8VertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
				 fvf = vb0->FVF_Info().Get_FVF();
			 }

			 // Last resort fallback (matches Apply_Render_State_Changes fallback)
			 if (fvf == 0) {
				 fvf = (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1); // 0x142
			 }

			 DX8Wrapper::BindLayoutFVF(fvf, "SortingRenderer::DirectDraw");
		 }

			Apply_Node_Fade(state);	// Ronin @feature 13/11/2026 DX9: a faded mesh's material alpha
			DX8Wrapper::Apply_Render_State_Changes();

			Debug_Statistics::Set_Draw_Subsystem(Classify_Sorted_Draw(state->sorting_state.shader));

			DX8Wrapper::Draw_Triangles(state->start_index,
				state->polygon_count,
				state->min_vertex_index,
				state->vertex_count);

			Release_Refs(state);
			clean_list.push_front(state);
			++TheSortingStats.direct;	// Ronin @diagnostic 06/11/2026 DX9: [SORT] a sorted node drawn from its own buffers
		}
	}

	bool old_enable=DX8Wrapper::_Is_Triangle_Draw_Enabled();
	DX8Wrapper::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	DX8Wrapper::_Enable_Triangle_Draw(old_enable);

	// Cleanup
	//DX8Wrapper::Set_Index_Buffer(nullptr, 0, "SortingRendererClass::Flush (cleanup)");
	//DX8Wrapper::Set_Vertex_Buffer(nullptr, 0);
	total_sorting_vertices = 0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	Renderer::Set_Transform(RenderTransform_View,old_view);
	Renderer::Set_Transform(RenderTransform_World,old_world);

	// Ronin @perf 07/11/2026 DX9: the additive parts (`parts`), with the camera back: drawn over everything the pool drew.
	TheDX8InstanceManager.Flush_Additive_Parts();

	Sort_Lap(TheSortingStats.ms[SortingRendererStats::MS_FLUSH], flushStart);	// Ronin @diagnostic 06/11/2026 DX9: [SORT]
	++TheSortingStats.flushes;
}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	//
	//	Flush the sorted list
	//
	while (!sorted_list.empty()) {
		delete sorted_list.front();
		sorted_list.pop_front();
	}

	//
	//	Flush the unsorted list
	//
	while (!unsorted_list.empty()) {
		delete unsorted_list.front();
		unsorted_list.pop_front();
	}

	//
	//	Flush the clean list
	//
	while (!clean_list.empty()) {
		delete clean_list.front();
		clean_list.pop_front();
	}

	delete[] temp_index_array;
	temp_index_array=nullptr;
	temp_index_array_count=0;
}
