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

// Ronin @feature 18/02/2026 DX9: Hardware instancing for rigid mesh batching

#pragma once

#include <d3d9.h>
#include "WWLib/always.h"
#include "WW3D2/shader.h"   // Ronin @bugfix 27/06/2026 DX9 P2: ShaderClass stored per single-rigid record (render-state grouping)

class DX8PolygonRendererClass;
class LightEnvironmentClass;
class Matrix3D;
class MeshClass;
class MeshModelClass;
class TextureClass;
class VertexMaterialClass;

// Ronin @feature 16/06/2026 DX9 Rigid parity: texcoord-gen state forwarded to the
// programmable rigid path. Mirrors the FFP "TEXCOORDINDEX source + texture matrix"
// contract. Rows are WW Matrix4x4 rows (column-translation); the VS dots them with
// float4(uv,0,1). A per-draw constant — shared across an instanced batch.
struct RigidTexGen
{
	bool  enabled;    // false => geometry UV0 passed through unchanged (legacy-identical)
	int   sourceMode; // 0 = vertex UV0, 1 = cam-space normal, 2 = cam-space reflection
	float row0[4];    // Matrix4x4 row 0
	float row1[4];    // Matrix4x4 row 1
};

/**
** DX8InstanceManagerClass
**
** Owns the stream1 instance ring holding per-instance world transforms + lighting, the programmable
** rigid shaders, and the deferred single-rigid batch.
**
** Ronin @perf 30/07/2026 §16 DX9: there is no longer a separate "instancing pass". Every eligible rigid
** mesh is collected ONCE by DX8TextureCategoryClass::Render via Collect_Single_Rigid; Flush_Single_Rigid
** then sorts that compact array by full pipeline state + geometry and collapses each maximal run of
** identical records into ONE instanced DrawIndexedPrimitive (auto/dynamic instancing). The old bucket
** pass was a SECOND walk of the per-category render-task linked list and cost a fixed ~60 fps whether or
** not it found anything to batch.
**
** Per-mesh eligibility is the single-rigid gate (allowProgrammableRigidFallback in dx8renderer.cpp):
** not SKIN/SORT/ALIGNED/ORIENTED, alpha override == 1, ObjectScale == 1, single pass, supported mapper.
*/

class DX8InstanceManagerClass
{
public:

	// Ronin @perf 24/06/2026 DX9 P0.5: dedicated ring buffer for the single-rigid path so we append
	// per-mesh instance data with NOOVERWRITE (one DISCARD per frame) instead of a per-mesh DISCARD on
	// a 1-instance VB. Sized well above worst-case single-rigid draws/frame so it won't wrap mid-frame.
	enum { SINGLE_RIGID_RING_INSTANCES = 8192 };

	// Ronin @bugfix 18/02/2026 DX9: Maximum cached vertex declarations for different FVFs.
	enum { MAX_CACHED_DECLS = 16 };

	DX8InstanceManagerClass();
	~DX8InstanceManagerClass();

	/**
	** One-time initialization. Call after device creation.
	** Creates the instance vertex buffer on stream 1 and loads the instancing shaders.
	** Returns true if hardware instancing is available and initialized successfully.
	*/
	bool Init();

	// Ronin @feature 12/08/2026 DX9: §29 phase 2. WW3D2 cannot include GameEngineDevice, so the
	// shadow map is pushed DOWN once per frame rather than pulled up.
	static void Set_Shadow_Map(IDirect3DBaseTexture9* tex, const float* lightViewProjT,
	                           float texelOffset, float depthBias,
	                           const float* lightTravelDir, float texelWorldSize);
	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. FAR cascade, its own call so every existing caller
	// of Set_Shadow_Map is untouched. tex == NULL means one cascade and the receiver ignores the rest.
	static void Set_Shadow_Map_Far(IDirect3DBaseTexture9* tex, const float* lightViewProjT,
	                               float depthBias, float texelWorldSize);
	// Ronin @feature 10/10/2026 DX9: lighting part 3. Round Flush_Single_Rigid's draws R3DLocalLights binds a second target and
	// RigidInstance_ps writes the sun's shade there (COLOR1): a lamp can then light what the sun's shadow hides.
	typedef void (*SunShadeHookFunc)(void);
	static void Set_Sun_Shade_Hooks(SunShadeHookFunc begin, SunShadeHookFunc end) { s_SunShadeBegin = begin; s_SunShadeEnd = end; }
	static SunShadeHookFunc s_SunShadeBegin;
	static SunShadeHookFunc s_SunShadeEnd;
	// Ronin @feature 10/10/2026 DX9: lighting part 3, step 2. What a sun shadow leaves on this map (rgb) and how far to follow it (w; 0 =
	// the old 0.45) - RigidInstance_ps c24. Pushed down by R3DLocalLights, as the shadow map is.
	static void Set_Shadow_Shade(const float* rgbw);
	// Ronin @feature 12/10/2026 DX9: lighting step 5. The map's sky cube for ReflectiveRigid_ps (s6) and how to use it (c6.x: 0 = the
	// art's picture, 1 = the cube, 2 = sky in every pane). A reference is held: pass NULL before the device goes.
	static void Set_Sky_Reflection(IDirect3DCubeTexture9* cube, float mode);
	// Ronin @feature 18/10/2026 DX9: reflections of the ground. The lit ground map for ReflectiveRigid_ps (s7) and its c12..c15
	// (16 floats; NULL = all 0). A reference is held: pass NULL before the device goes.
	static void Set_Reflection_Ground(IDirect3DTexture9* groundMap, const float* c12to15);
	// Ronin @feature 18/10/2026 DX9: screen-space reflections. TAA's last frame for ReflectiveRigid_ps (picture s8, depth s9, c16..c21)
	// and the frame before (s10, c69..c74), 24 floats each, NULLs = off. Borrowed, never held: a default-pool reference blocks a reset.
	static void Set_Screen_Reflection(IDirect3DTexture9* prevColour, IDirect3DTexture9* prevDepth, const float* c16to21,
	                                  IDirect3DTexture9* olderColour, const float* c69to74);
	// Ronin @feature 18/10/2026 DX9: the reflections' change mask. Round each draw with screen-space reflections R3DTaa binds it as
	// target 1 (begin returns true when it did); ReflectiveRigid_ps marks there where the reflection changed (COLOR1).
	typedef bool (*ReflectMaskBeginFunc)(void);
	static void Set_Reflect_Mask_Hooks(ReflectMaskBeginFunc begin, SunShadeHookFunc end) { s_ReflectMaskBegin = begin; s_ReflectMaskEnd = end; }
	static ReflectMaskBeginFunc s_ReflectMaskBegin;
	static SunShadeHookFunc     s_ReflectMaskEnd;


	/**
	** Shutdown and release all D3D resources.
	*/
	void Shutdown();

	/**
	** Returns true if hardware instancing is supported and initialized.
	*/
	bool Is_Available() const { return m_available; }

	/**
	** Enable or disable instancing at runtime (e.g., from a settings menu).
	*/
	void Set_Enabled(bool enabled) { m_enabled = enabled; }
	bool Is_Enabled() const { return m_enabled && m_available; }

	// Ronin @feature DX9: the reflective env pass-0, drawn INLINE (one mesh, one draw). This is the only
	// rigid draw that cannot be batched: it needs m_reflectivePS plus per-draw camera position and view
	// matrix in PS constants, which the shared Flush_Single_Rigid pipeline (fixed m_instancePS) cannot
	// supply, and it must land BEFORE its pass-1 overlay, which is batched to container end.
	// Ronin @cleanup 01/08/2026 §16 DX9: was Draw_Single_Rigid(..., baseVertexOffset, ..., reflective).
	// All non-reflective callers are gone (they collect into the batch), so the flag and the never-read
	// baseVertexOffset went with them. Returns false for strips and when ReflectiveRigid.pso is missing;
	// the caller then falls through to the legacy env path.
	// Ronin @perf 13/11/2026 DX9: now the fallback - a panel whose other pass the batch draws goes there too (Collect_Reflective_Rigid).
	bool Draw_Reflective_Rigid(
		DX8PolygonRendererClass* renderer,
		DWORD geometryFVF,
		LightEnvironmentClass* lightEnv,
		VertexMaterialClass* material,
		TextureClass* diffuseTexture,
		const Matrix3D& worldTransform,
		const RigidTexGen& texGen,
		// Ronin @feature 12/11/2026 DX9: a faded panel (Set_Collect_Fade): its alpha body pass, composited in this draw (ReflectiveRigidBody.pso).
		TextureClass* bodyTexture = nullptr,
		VertexMaterialClass* bodyMaterial = nullptr);

	// Ronin @feature DX9: true once ReflectiveRigid.pso loaded; the reflective rigid branch checks this
	// before routing an env-reflect pass to the per-pixel path (else it stays on legacy).
	bool Has_Reflective_PS() const { return m_reflectivePS != nullptr; }
	// Ronin @feature 12/11/2026 DX9: true once ReflectiveRigidBody.pso loaded (and the plain one) - a faded reflective panel is drawn whole.
	bool Has_Reflective_Body_PS() const { return m_reflectivePS != nullptr && m_reflectiveBodyPS != nullptr; }

	// Ronin @perf 07/11/2026 DX9: additive model parts (rocket flames, muzzle flashes) - sorted meshes with an additive, no-depth-write
	// pass - collected instead of pooled, and drawn instanced by Flush_Additive_Parts after the sorting pool. Off = the pool, as before.
	bool Collect_Additive_Part(
		DX8PolygonRendererClass* renderer,
		MeshClass* mesh,
		LightEnvironmentClass* lightEnv,
		VertexMaterialClass* material,
		TextureClass* texture,
		const ShaderClass& shader,
		const Matrix3D& worldTransform);
	void Flush_Additive_Parts();
	void Set_Additive_Parts(bool on) { m_additiveParts = on; }
	bool Get_Additive_Parts() const { return m_additiveParts; }
	unsigned Get_Last_Frame_Additive_Parts() const { return m_lastFrameAdditiveParts; }	// parts collected
	unsigned Get_Last_Frame_Additive_Draws() const { return m_lastFrameAdditiveDraws; }	// instanced draws issued
	bool Has_Additive_PS() const { return m_additivePS != nullptr; }


	// Ronin @perf 24/06/2026 DX9 P1: deferred single-rigid batch. The renderer collects eligible
	// non-instanced rigid meshes per texture-category via Collect_Single_Rigid, then issues them all
	// in Flush_Single_Rigid: one ring lock, programmable state set ONCE, one FFP restore. This kills
	// the per-mesh lock + FFP<->programmable thrash that made inline Draw_Single_Rigid slow.
	bool Collect_Single_Rigid(
		DX8PolygonRendererClass* renderer,
		DWORD geometryFVF,
		LightEnvironmentClass* lightEnv,
		VertexMaterialClass* material,
		TextureClass* diffuseTexture,
		const Matrix3D& worldTransform,
		const RigidTexGen& texGen,
		const ShaderClass& shader,
		int baseVertex = 0);	// Ronin @feature 05/11/2026 DX9: a skin's place in the shared skin buffer; 0 for a rigid mesh

	// Ronin @perf 13/11/2026 DX9: a reflective panel's reflection pass into this batch; while Set_Collect_Fade is on, with its body.
	// False = the caller draws it inline (Draw_Reflective_Rigid).
	bool Collect_Reflective_Rigid(
		DX8PolygonRendererClass* renderer,
		DWORD geometryFVF,
		LightEnvironmentClass* lightEnv,
		VertexMaterialClass* material,
		TextureClass* envTexture,
		const Matrix3D& worldTransform,
		const ShaderClass& shader,
		TextureClass* bodyTexture,
		VertexMaterialClass* bodyMaterial);

	void Flush_Single_Rigid();
	// Ronin @feature 12/11/2026 DX9: while set, a collected record is faded - fade 0..1 (1 = gone), drawn with this alpha blend; the caller clears it.
	void Set_Collect_Fade(float fade, const ShaderClass& shader) { m_collectFade = fade; m_collectFadeShader = shader; m_collectFadeOn = true; }
	void Clear_Collect_Fade() { m_collectFadeOn = false; }
	// Ronin @perf 14/11/2026 DX9: set round the faded objects' pass (W3DScene), whose depth goes down first: a faded record's colour lands
	// only where it is nearest, so a texture's faded records sort like opaque ones and the same part of every building shares a draw.
	static bool s_FadedOrderFree;
	void Reset_Single_Rigid_Collection() { m_pendingSingleRigidCount = 0; }
	unsigned Get_Pending_Single_Rigid_Count() const { return m_pendingSingleRigidCount; }

	// Ronin @perf 30/07/2026 §16 DX9: the container-level instancing API (Can_Collect_Instanced_Group /
	// Collect_Instanced_Group / Flush_Instanced / Reset_Instanced_Collection) and the staging buffer that
	// fed it (Reset_Collection / Add_Instance / Get_Collected_Count) are GONE. Instancing now happens
	// inside Flush_Single_Rigid, over records the ONE per-mesh walk already collected.

	// Ronin @diagnostic §16 DX9: the [INST] HUD now reports the MERGED flush, not a bucket pass.
	//   Records        - single-rigid records considered (compact array walk, no list traversal)
	//   Draw_Calls     - runs collapsed into ONE instanced draw
	//   Meshes         - records drawn inside those runs
	//   Individual     - records drawn on their own (run below threshold)
	//   Light_Breaks   - normal-mapped runs cut short ONLY because the next record's per-pixel light set
	//                    differs (PS lights are per-DRAW constants c3..c11). Watch this to decide whether
	//                    merging normal-mapped records is earning its keep.
	unsigned Get_Last_Frame_Instanced_Records() const { return m_lastFrameInstancedRecords; }
	unsigned Get_Last_Frame_Instanced_Draw_Calls() const { return m_lastFrameInstancedDrawCalls; }
	unsigned Get_Last_Frame_Instanced_Meshes() const { return m_lastFrameInstancedMeshes; }
	unsigned Get_Last_Frame_Instanced_Individual_Draws() const { return m_lastFrameInstancedIndividualDraws; }
	unsigned Get_Last_Frame_Instanced_Light_Breaks() const { return m_lastFrameInstancedLightBreaks; }
	unsigned Get_Last_Frame_Instanced_Normal_Mapped() const { return m_lastFrameInstancedNormalMapped; }
	unsigned Get_Last_Frame_Instanced_Normal_Mapped_Merged() const { return m_lastFrameInstancedNormalMappedMerged; }
	unsigned Get_Last_Frame_Instanced_Flushes() const { return m_lastFrameInstancedFlushes; }
	// Ronin @diagnostic 13/11/2026 DX9: reflective env pass-0 draws this frame - inline (Draw_Reflective_Rigid) and the batch's
	// (Collect_Reflective_Rigid), whose records count in Records/Meshes/Individual too.
	unsigned Get_Last_Frame_Reflective_Draws() const { return m_lastFrameReflectiveDraws; }
	// Ronin @diagnostic 13/11/2026 DX9: the panel calls this once per game frame; the counters then stop rolling on WW3D's frame count.
	void Roll_Stats_Now();

	void Begin_Frame_Statistics();
	void End_Frame_Statistics();
	void Release_Resources();

private:

	/**
	** Per-instance data written to stream 1.
	** Transform rows (TEXCOORD1..3) come first, followed by the per-instance
	** lighting payload (TEXCOORD4..12): ambient (rgb + numLights in .w) and up to
	** four directional lights (direction + diffuse) matching the shared rigid
	** lighting builder's lightenv branch.
	*/
	struct InstanceData
	{
		float row0[4]; // world matrix row 0 (m00, m01, m02, m03)  TEXCOORD1
		float row1[4]; // world matrix row 1 (m10, m11, m12, m13)  TEXCOORD2
		float row2[4]; // world matrix row 2 (m20, m21, m22, m23)  TEXCOORD3
		float ambient[4];       // rgb = equivalent ambient, w = numLights   TEXCOORD4
		float lightDir0[4];     // TEXCOORD5
		float lightDiffuse0[4]; // TEXCOORD6
		float lightDir1[4];     // TEXCOORD7
		float lightDiffuse1[4]; // TEXCOORD8
		float lightDir2[4];     // TEXCOORD9
		float lightDiffuse2[4]; // TEXCOORD10
		float lightDir3[4];     // TEXCOORD11
		float lightDiffuse3[4]; // TEXCOORD12
	};

	static_assert(sizeof(InstanceData) == 192, "InstanceData must be 192 bytes (3 transform + 9 lighting float4)");

	// Ronin @bugfix 18/02/2026 DX9: Cached vertex declaration entry keyed by FVF.
	struct CachedDecl
	{
		DWORD fvf;
		IDirect3DVertexDeclaration9* decl;
	};

	bool m_available;        // Hardware supports instancing
	bool m_enabled;          // User has instancing enabled
	
	IDirect3DVertexBuffer9* m_singleRigidVB;     // Ronin @perf 24/06/2026 DX9 P0.5: instance ring, shared by Flush_Single_Rigid + Draw_Reflective_Rigid
	unsigned                m_singleRigidCursor; // Ronin @perf 24/06/2026 DX9 P0.5: next free instance slot in the ring

	IDirect3DVertexShader9* m_instanceVS;        // Instancing vertex shader (with COLOR0)
	IDirect3DVertexShader9* m_instanceVSNoColor; // Instancing vertex shader (no COLOR0)
	IDirect3DPixelShader9* m_instancePS;         // Ronin @feature 08/03/2026 DX9: Minimal pixel shader to bypass FFP pixel combiners on AMD
	IDirect3DVertexShader9* m_rigidVS;           // Ronin @feature 23/05/2026 DX9 R3: Non-instanced rigid fallback VS (with COLOR0)
	IDirect3DVertexShader9* m_rigidVSNoColor;    // Ronin @feature 23/05/2026 DX9 R3: Non-instanced rigid fallback VS (no COLOR0)
	IDirect3DPixelShader9* m_reflectivePS;       // Ronin @feature DX9: per-pixel env reflection PS (ReflectiveRigid.pso) for reflective pass-0
	IDirect3DPixelShader9* m_reflectiveBodyPS;   // Ronin @feature 12/11/2026 DX9: ReflectiveRigidBody.pso - a faded panel, its body in the same draw
	IDirect3DPixelShader9* m_additivePS;         // Ronin @perf 07/11/2026 DX9: AdditivePart.pso - texture x lit colour, for the additive parts

	// Ronin @bugfix 18/02/2026 DX9: Per-FVF declaration cache (replaces single m_instanceDecl)
	CachedDecl m_declCache[MAX_CACHED_DECLS];
	unsigned   m_declCacheCount;

	// Ronin @feature 23/05/2026 DX9 R3: Separate declaration cache for the
	// non-instanced programmable rigid fallback. Unlike the instanced path, these
	// declarations contain only stream 0 geometry elements.
	CachedDecl m_geometryDeclCache[MAX_CACHED_DECLS];
	unsigned   m_geometryDeclCacheCount;

	// Ronin @perf 24/06/2026 DX9 P1: deferred single-rigid batch state (see Collect/Flush_Single_Rigid).
	struct PendingSingleRigid {
		InstanceData             inst;      // transform rows now; per-instance lighting filled at flush
		LightEnvironmentClass*   lightEnv;  // per-mesh light env (Extract_Instance_Lighting at flush)
		DX8PolygonRendererClass* renderer;  // geometry to draw (its own index range)
		RigidTexGen              texGen;    // per-mesh (tread UV offset varies within a category)
		TextureClass*            diffuse;   // per-record: a container flush spans many texture-categories
		VertexMaterialClass*     material;  // per-record: VS material constants swapped per distinct material
		ShaderClass              shader;    // per-record: blend/z/alpha-test/cull render state, re-applied per group
		bool                     shroud;    // per-record: false = its light env already carries the fog (a ghost object)
		int                      baseVertex; // Ronin @feature 05/11/2026 DX9: a skin's first vertex in the skin buffer (rigid: 0)
		float                    fade;       // Ronin @feature 12/11/2026 DX9: 0 = opaque, 1 = gone; rides light 0's spare w to the VS
		unsigned char            kind;       // Ronin @perf 13/11/2026 DX9: 0 a rigid mesh, 1 a reflection, 2 a faded panel (Collect_Reflective_Rigid)
		TextureClass*            bodyTexture;  // ... a faded panel's body pass: its texture and material
		VertexMaterialClass*     bodyMaterial;
	};

	// Ronin @feature 12/11/2026 DX9: Set_Collect_Fade's fade and alpha blend for the records collected while it is on.
	float                m_collectFade = 0.0f;
	ShaderClass          m_collectFadeShader;
	bool                 m_collectFadeOn = false;

	enum { MAX_PENDING_SINGLE_RIGID = 4096 };
	PendingSingleRigid   m_pendingSingleRigid[MAX_PENDING_SINGLE_RIGID];

	// Ronin @perf §16 DX9: draw order for one flush — indices into m_pendingSingleRigid. We sort THIS,
	// never the 192-byte records. Rebuilt per flush by Build_Single_Rigid_Order.
	unsigned             m_srOrder[MAX_PENDING_SINGLE_RIGID];

	unsigned             m_pendingSingleRigidCount;
	// Container-constant: every mesh in a DX8RigidFVFCategoryContainer shares one FVF (one decl, one VB).
	DWORD                m_srFVF;
	bool                 m_srSkinBatch;	// Ronin @bugfix 05/11/2026 DX9: the batch is a skin container's (its colour channel is empty)

	// Statistics — Ronin @diagnostic §16 DX9: all produced by the merged Flush_Single_Rigid.
	// Live counters first, then their end-of-frame snapshots in the SAME order (the two lists are
	// parallel: Roll_Instancing_Stats_Frame copies one to the other, and the ctor mirrors them).
	unsigned m_instancedRecords;              // records considered
	unsigned m_instancedDrawCalls;            // runs collapsed into one instanced draw
	unsigned m_instancedMeshes;               // records drawn inside those runs
	unsigned m_instancedIndividualDraws;      // records drawn on their own
	unsigned m_instancedNormalMapped;         // records drawn with a <diffuse>_NRM bound
	unsigned m_instancedNormalMappedMerged;   // ...of which rode a merged run
	unsigned m_instancedLightBreaks;          // NRM runs cut short by a per-pixel light-set mismatch
	unsigned m_instancedFlushes;
	unsigned m_reflectiveDraws;               // inline reflective env pass-0 draws (outside the batch)
	unsigned m_lastFrameInstancedRecords;
	unsigned m_lastFrameInstancedDrawCalls;
	unsigned m_lastFrameInstancedMeshes;
	unsigned m_lastFrameInstancedIndividualDraws;
	unsigned m_lastFrameInstancedNormalMapped;
	unsigned m_lastFrameInstancedNormalMappedMerged;
	unsigned m_lastFrameInstancedLightBreaks;
	unsigned m_lastFrameInstancedFlushes;
	unsigned m_lastFrameReflectiveDraws;
	unsigned m_instancedStatsFrame;      // last WW3D frame seen — self-contained roll, like the [SR] counters
	void Roll_Instancing_Stats_Frame();  // snapshot+reset ALL instanced counters on frame change
	void Snapshot_Instancing_Stats();    // Ronin @diagnostic 13/11/2026 DX9: the copy and reset both rolls share

	// Ronin @perf 07/11/2026 DX9: the additive parts waiting for Flush_Additive_Parts. Transform and lighting are taken at collect;
	// the geometry is the model's own arrays, copied once per model at the flush.
	struct PendingAdditivePart {
		InstanceData          inst;
		MeshModelClass*       model;
		TextureClass*         texture;
		VertexMaterialClass*  material;
		ShaderClass           shader;
	};
	enum { MAX_PENDING_ADDITIVE = 2048 };
	PendingAdditivePart   m_pendingAdditive[MAX_PENDING_ADDITIVE];
	unsigned              m_additiveOrder[MAX_PENDING_ADDITIVE];
	unsigned              m_pendingAdditiveCount;
	bool                  m_additiveParts;	// the switch (panel `parts`); off = the sorting pool, as before
	unsigned              m_additivePartsCount;
	unsigned              m_additiveDraws;
	unsigned              m_lastFrameAdditiveParts;
	unsigned              m_lastFrameAdditiveDraws;

	// Internal helpers
	bool Create_Instance_VB();
	static void Extract_Instance_Lighting(LightEnvironmentClass* lightEnv, InstanceData& inst);
	IDirect3DVertexDeclaration9* Get_Or_Create_Instance_Decl(DWORD geometryFVF);
	IDirect3DVertexDeclaration9* Get_Or_Create_Geometry_Decl(DWORD geometryFVF);
	bool Load_Instance_Shader();
	bool Load_Vertex_Shader_From_File(const char* shaderPath, IDirect3DVertexShader9** outShader);
	bool Load_Pixel_Shader_From_File(const char* shaderPath, IDirect3DPixelShader9** outShader);

	// Ronin @perf §16 DX9: auto-instancing merge. Build_Single_Rigid_Order sorts each opaque stretch of
	// the pending array into merge runs (blended records are barriers); _Order_Less is the total order on
	// the merge key; _Records_Merge asks "can these two ride one instanced draw?" and derives equality
	// from _Order_Less so the sort and the merge test can never drift apart.
	void Build_Single_Rigid_Order(unsigned count);
	static bool Single_Rigid_Order_Less(const PendingSingleRigid& a, const PendingSingleRigid& b);
	static bool Single_Rigid_Records_Merge(const PendingSingleRigid& a, const PendingSingleRigid& b, bool requireLightMatch);
};

/**
** Global instance manager, created/destroyed alongside TheDX8MeshRenderer.
*/
extern DX8InstanceManagerClass TheDX8InstanceManager;
