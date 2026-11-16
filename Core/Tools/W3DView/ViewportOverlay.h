/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// 2D overlays shared by the main viewport and the Material Viewer preview

#pragma once

#include "WWMath/vector3.h"
#include "WWMath/matrix3d.h"
#include "WWMath/quat.h"
#include "WW3D2/dx8wrapper.h"

class RenderObjClass;

// D3D9 moved the filter states to the sampler and split the FVF from the vertex shader
#if defined(BUILD_WITH_D3D9)
#define VIEW_GET_FILTER(dev, state, out)		(dev)->GetSamplerState (0, D3DSAMP_##state, out)
#define VIEW_SET_FILTER(dev, state, value)	(dev)->SetSamplerState (0, D3DSAMP_##state, value)
#define VIEW_GET_FVF(dev, out)					(dev)->GetFVF (out)
#else
#define VIEW_GET_FILTER(dev, state, out)		(dev)->GetTextureStageState (0, D3DTSS_##state, out)
#define VIEW_SET_FILTER(dev, state, value)	(dev)->SetTextureStageState (0, D3DTSS_##state, value)
#define VIEW_GET_FVF(dev, out)					(dev)->GetVertexShader (out)
#endif

// 3ds Max style cube at the top right of a viewport. A click on a face, edge or
// corner turns the camera to that side, and a drag on the cube orbits.
class ViewCubeClass
{
public:
	ViewCubeClass();

	// Draws inside the render bracket, after the scene. Highlights the region under the cursor.
	void Render(HWND hwnd, const Matrix3D &camera);

	// Mouse events in client coordinates. Each returns true when the cube took the event.
	bool On_LButton_Down(HWND hwnd, const Matrix3D &camera, POINT point);
	bool On_Mouse_Move(POINT point, int &orbit_dx, int &orbit_dy);
	bool On_LButton_Up();

	// Steps a running turn about center and writes the new camera transform. Returns false when idle.
	bool Update_Snap(const Vector3 &center, const Matrix3D &camera, Matrix3D &transform);

	// Camera transform a click on region ends at. region is the sum of the face normals it touches.
	static Matrix3D Get_Snap_Transform(const Vector3 &region, const Vector3 &center, float distance);

private:
	// region gets the sum of the face normals the point touches
	bool Hit_Test(HWND hwnd, const Matrix3D &camera, POINT point, Vector3 &region) const;

	bool		m_Pressed;
	bool		m_Dragging;
	POINT		m_PressPoint;
	POINT		m_LastPoint;
	Vector3		m_PressRegion;

	bool		m_Snapping;
	bool		m_SnapStarted;
	DWORD		m_SnapStartTicks;
	Vector3		m_SnapRegion;
	Quaternion	m_SnapFrom;
	Quaternion	m_SnapTo;
	float		m_SnapDistance;
};

// Poly and vertex totals at the top left of a viewport, toggled with the 7 key
namespace ViewportStats
{
	bool Is_Visible();
	void Toggle();

	// A plain 7 press, without modifiers or auto-repeat, while no edit box has the focus
	bool Is_Toggle_Key(const MSG *msg);

	int Count_Vertices(RenderObjClass *obj);

	// Draws inside the render bracket, after the scene
	void Render(RenderObjClass *obj);
}
