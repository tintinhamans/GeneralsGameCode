/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// TheSuperHackers @feature W3DView material viewer 3D preview pane.
// Renders its own scene into an additional D3D8 swap chain, so the preview is
// independent of the main CGraphicView while sharing the single device.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "WWMath/vector3.h"
#include "WWMath/quat.h"
#include "WWMath/matrix3d.h"
#include "WWMath/sphere.h"
#include "WW3D2/dx8wrapper.h"
#include "../ViewportOverlay.h"

class RenderObjClass;
class SimpleSceneClass;
class CameraClass;
class LightClass;
class TextureClass;

namespace W3dMaterialViewer { struct MaterialDocument; }

struct MaterialPreviewLamp
{
	Vector3 position;
	Vector3 color;
	float radius;
};

struct MaterialPreviewLampTexel
{
	Vector3 color;
	Vector3 alphaColor;
};

// Implements DX8_CleanupHook because the pane owns non-managed device resources
// (additional swap chain + depth surface). D3D8 Reset fails while they are
// alive, leaving the device corrupted; the hook releases them before a reset.
class CMaterialPreviewPane : public CWnd, public DX8_CleanupHook
{
public:
	CMaterialPreviewPane();
	virtual ~CMaterialPreviewPane();

	BOOL Create(CWnd *parent, const RECT &rect, UINT id);

	// Creates a render object by name from the shared asset manager and frames
	// the camera on it. Returns false if the asset does not exist, or is a skinned
	// mesh on its own.
	bool LoadModel(const char *name);

	// Removes the current render object and releases its ref (so the asset
	// manager can drop the prototype before a reload).
	void UnloadModel();

	// Applies the edited material document onto the *live* preview render object
	// in memory, without touching the .w3d file. Vertex-material colors/opacity/
	// shininess and shader blend/flags are stamped onto per-pass materials that
	// are first cloned, so the shared asset-manager prototype (used by the main
	// viewport) is never mutated. Meshes are matched to the document by name.
	void ApplyLiveMaterials(const W3dMaterialViewer::MaterialDocument &document);

	// Renders one frame into the swap chain. Must be called from the main
	// thread, outside the main view's Begin_Render/End_Render bracket.
	void Render();

	// Outlines the named sub-object's bounding box in the preview, so the mesh
	// being edited stays visible while the whole object is displayed (View >
	// Show Full Object). Null/empty clears the outline.
	void Set_Highlight_Mesh(const char *name);

	// View > Tint Selected Mesh: highlight the selected sub-object by tinting
	// its actual triangles with the selection colour instead of drawing the
	// bounding box (which is still the fallback for skins/non-meshes).
	void Set_Tint_Highlight(bool on) { m_TintHighlight = on; }
	bool Get_Tint_Highlight() const { return m_TintHighlight; }

	// How Ctrl+left-drag moves the scene light (Light menu). FREE_ROAM is the
	// main viewport's trackball rotation about the orbit centre; PER_FACE
	// places the light on the surface normal of the point under the cursor.
	enum LightPlacementMode
	{
		LIGHT_FREE_ROAM = 0,
		LIGHT_PER_FACE = 1,
	};
	void Set_Light_Placement_Mode(LightPlacementMode mode) { m_LightPlacementMode = mode; }
	LightPlacementMode Get_Light_Placement_Mode() const { return m_LightPlacementMode; }

	// Ground plane (View > Show Ground Plane): a simple checkered quad drawn
	// below the object with a soft blob shadow under its footprint. Drawn before
	// the scene render, so the object's translucent/additive parts (flushed last
	// by WW3D) blend over it — additive "glow" falls onto the ground for free.
	void Set_Ground_Visible(bool visible) { m_GroundVisible = visible; }
	bool Is_Ground_Visible() const { return m_GroundVisible; }
	void Set_Ground_Z(float z) { m_GroundZ = z; }
	float Get_Ground_Z() const { return m_GroundZ; }
	// Height-slider bounds derived from the loaded object's bounding box
	// (bottom +/- one span, so the default sits mid-slider); false while no
	// model is loaded.
	bool Get_Ground_Z_Range(float &z_min, float &z_max) const;
	// Snaps the plane back under the loaded object's bounding box (Ground >
	// Reset Height).
	void Reset_Ground_To_Object();

	// Animation playback (driven by the Material Viewer's animation bar). The
	// pane owns a ref on the current animation and re-poses / advances the
	// frame inside Render(); the frame window mirrors the state into the Qt bar
	// after each render. Animations pose the hierarchy, so they only visibly
	// apply when the full object (HLod) is displayed — on a bare mesh
	// Set_Animation is a no-op.
	void Set_Animation_Asset(class HAnimClass *anim);	// takes its own ref; null clears
	class HAnimClass *Peek_Animation_Asset() const { return m_Anim; }
	void Play_Animation(bool play) { m_AnimPlaying = play; }
	bool Is_Animation_Playing() const { return m_AnimPlaying; }
	void Seek_Animation(float frame);
	float Get_Animation_Frame() const { return m_AnimFrame; }

	// The pane's current render object (borrowed pointer, no ref added); used by
	// the frame to read the hierarchy name when listing matching animations.
	RenderObjClass *Peek_Render_Object() const { return m_RenderObj; }

	// Save/restore the full orbit-camera state so each Material Viewer tab can
	// keep its own view. Captures the raw camera transform plus the orbit
	// accumulators (centre / rotation / distance / zoom step) verbatim, so a
	// restore is exact regardless of how the view was reached. Set must run
	// AFTER LoadModel (which re-frames the camera and sets model-appropriate
	// clip planes that are deliberately left alone here).
	void Get_Camera_State(Matrix3D &transform, Vector3 &center,
		Quaternion &rotation, float &distance, float &minZoomAdjust) const;
	void Set_Camera_State(const Matrix3D &transform, const Vector3 &center,
		const Quaternion &rotation, float distance, float minZoomAdjust);

	// DX8_CleanupHook: release device resources before a reset; recreation is
	// lazy (Render recreates the swap chain when it is missing).
	virtual void ReleaseResources();
	virtual void ReAcquireResources();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT create_struct);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnLButtonDown(UINT flags, CPoint point);
	afx_msg void OnLButtonUp(UINT flags, CPoint point);
	afx_msg void OnRButtonDown(UINT flags, CPoint point);
	afx_msg void OnRButtonUp(UINT flags, CPoint point);
	afx_msg void OnMButtonDown(UINT flags, CPoint point);
	afx_msg void OnMButtonUp(UINT flags, CPoint point);
	afx_msg void OnMouseMove(UINT flags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	void Free_Swap_Chain();
	void Create_Swap_Chain();

	// Frames the camera on `sphere` (same model as the main viewport's
	// Reset_Camera_To_Display_Sphere): distance = 3*radius, Look_At down +X.
	void Reset_Camera_To_Sphere(const SphereClass &sphere);

	// 3ds Max-style orbit shared by left-drag and MMB+Alt: pitch about the
	// camera-local X and yaw about world Z through the orbit centre.
	void Orbit_Camera(int deltaX, int deltaY);

	// Movable scene light: holding Ctrl shows the LIGHT gizmo mesh at the
	// scene light. Ctrl+left click/drag moves the light per the Light-menu
	// mode (Rotate_Light = free roam, Position_Light_At_Cursor = per face).
	// Ctrl+right-drag moves it closer/farther (never inside the bounding
	// sphere).
	void Sync_Light_Mesh(bool visible);
	void Rotate_Light(CPoint point);
	void Position_Light_At_Cursor(CPoint point);
	void Adjust_Light_Distance(int deltaY);

	// Draws the highlighted sub-object's bounding box as a wireframe overlay.
	// Must run inside the Begin_Render/End_Render bracket, after the scene.
	void Render_Highlight_Outline();

	// Tint-mode highlight: re-draws the sub-mesh's triangles as a translucent
	// selection-coloured overlay at equal depth. Returns false when the
	// sub-object can't be tinted this way (skin / non-mesh / >64k verts) so the
	// caller falls back to the bounding box.
	bool Render_Highlight_Tint(RenderObjClass *sub);

	// Draws the ground plane + blob shadow. Must run inside the render bracket
	// BEFORE the scene render: the plane writes depth so opaque geometry
	// occludes it, and the translucent flush afterwards blends glow over it.
	void Render_Ground_Plane();

	// Lights the preview as the game lights the surroundings of a material with Specular To Diffuse ticked.
	void Update_Lamps();
	void Render_Lamp_Pools();
	void Render_Lamp_Light();

	// Confines/wraps the cursor to the pane while dragging (infinite drag).
	void Clip_Cursor_To_View();
	bool Wrap_Cursor_In_View(CPoint &point);

	IDirect3DSwapChain8	*m_SwapChain;
	// Pane-sized non-MSAA depth buffer. The device's default depth buffer is
	// sized to the main view (and may be multisampled), so pairing it with our
	// swap chain's back buffer can fail and render nothing.
	IDirect3DSurface8	*m_DepthBuffer;
	SimpleSceneClass	*m_Scene;
	CameraClass			*m_Camera;
	LightClass			*m_Light;
	RenderObjClass		*m_RenderObj;

	// Ctrl-held light gizmo (created lazily from the embedded Light.w3d).
	RenderObjClass		*m_LightMesh;
	bool				m_LightMeshInScene;
	float				m_LightMeshScale;	// last applied gizmo scale
	LightPlacementMode	m_LightPlacementMode;

	// This frame's lamps, and the average colour of each texture they shine through.
	std::vector<MaterialPreviewLamp>	m_Lamps;
	std::map<TextureClass *, MaterialPreviewLampTexel>	m_LampTexels;

	// Name of the currently loaded model. LoadModel re-frames the camera only
	// when this changes, so a post-save reload of the same model keeps the view.
	std::string			m_LoadedName;

	// Sub-object to outline in the full-object view; empty = no outline.
	std::string			m_HighlightMeshName;

	// View > Tint Selected Mesh (see Set_Tint_Highlight).
	bool				m_TintHighlight;

	// Ground plane state. m_GroundZ defaults to the object's bounding-box
	// bottom on every *new* model load (post-save reloads keep the user's
	// height, like the camera).
	bool				m_GroundVisible;
	float				m_GroundZ;

	// Animation playback state (see Set_Animation_Asset).
	class HAnimClass	*m_Anim;
	bool				m_AnimPlaying;
	float				m_AnimFrame;

	// Orbit camera state — mirrors the main viewport (CGraphicView). The orbit
	// centre and accumulated rotation are per-instance (the main view keeps its
	// equivalents as file-scope globals; here they must not collide with those).
	SphereClass			m_ViewedSphere;		// last framed sphere (for reset)
	Vector3				m_SphereCenter;		// orbit centre
	Quaternion			m_Rotation;			// accumulated camera rotation (for pan)
	float				m_CameraDistance;
	float				m_MinZoomAdjust;

	// Mouse-button bookkeeping (matches CGraphicView).
	BOOL				m_bMouseDown;		// left
	BOOL				m_bRMouseDown;		// right
	BOOL				m_bMMouseDown;		// middle
	bool				m_AltOnMDown;		// Alt latched at middle-down
	CPoint				m_LastPoint;
	CPoint				m_MButtonDownPoint;

	ViewCubeClass		m_ViewCube;

	// Set by OnSize; the swap chain is recreated on the next Render() so a
	// drag-resize recreates at most once per rendered frame.
	bool				m_SizeDirty;

	// GetTickCount() of the previous Render(); 0 until the first frame. Used to
	// advance WW3D's global sync clock (WW3D::Sync) so animated UV mappers
	// (linear-offset / grid / rotate / sine / ...) keep moving while the main
	// viewport is paused and only this preview is rendering.
	DWORD				m_LastRenderTicks;
};
