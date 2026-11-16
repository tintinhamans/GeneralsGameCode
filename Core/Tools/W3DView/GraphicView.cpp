/*
**	Command & Conquer Renegade(tm)
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

// GraphicView.cpp : implementation file
//

#include "StdAfx.h"
#include "W3DView.h"
#include "GraphicView.h"
#include "WW3D2/ww3d.h"
#include "Globals.h"
#include "W3DViewDoc.h"
#include <process.h>
#include "WWMath/quat.h"
#include "MainFrm.h"
#include "Utils.h"
#include "mmsystem.h"
#include "WW3D2/light.h"
#include "ViewerAssetMgr.h"
#include "WWLib/rcfile.h"
#include "WW3D2/part_emt.h"
#include "WW3D2/part_buf.h"
#include "WW3D2/hlod.h"
#include "ViewerScene.h"
#include "ScreenCursor.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/coltest.h"
#include "WWLib/MPU.h"
#include "WW3D2/dazzle.h"
#include "WWAudio/SoundScene.h"
#include "WWAudio/WWAudio.h"
#include "WW3D2/metalmap.h"
#include "WW3D2/dx8wrapper.h"
#include "WWMath/matrix3.h"
#include "WW3D2/shader.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/htree.h"
#include "WW3D2/camera.h"
#include "WWMath/aabox.h"
#include "WWMath/wwmath.h"
#include "W3DDarkMode.h"
#include "MaterialViewer/MaterialViewerFrame.h"

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


struct NameLabel {
	const char *name;
	Vector3      worldPos;
};

// Shared renderer: draws a list of (name, worldPos) labels as a D3D alpha-blended fullscreen
// quad so text is composited in the back buffer before Present — no post-Present flicker.
static void Render_Name_Labels (CGraphicView *view, CameraClass *camera,
	const NameLabel *labels, int numLabels, DWORD labelColor)
{
	if (view == nullptr || camera == nullptr || labels == nullptr || numLabels <= 0)
		return;

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8 ();
	if (dev == nullptr)
		return;

	RECT clientRect;
	view->GetClientRect (&clientRect);
	int viewW = clientRect.right - clientRect.left;
	int viewH = clientRect.bottom - clientRect.top;
	if (viewW <= 0 || viewH <= 0)
		return;

	HDC memDC = ::CreateCompatibleDC (nullptr);
	BITMAPINFO bmi;
	::ZeroMemory (&bmi, sizeof (bmi));
	bmi.bmiHeader.biSize        = sizeof (BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = viewW;
	bmi.bmiHeader.biHeight      = -viewH;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *dibBits = nullptr;
	HBITMAP dib = ::CreateDIBSection (memDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
	if (dib == nullptr || dibBits == nullptr) {
		if (dib) ::DeleteObject (dib);
		::DeleteDC (memDC);
		return;
	}
	HBITMAP oldBmp = (HBITMAP)::SelectObject (memDC, dib);

	DWORD *pixels = (DWORD *)dibBits;
	int pixelCount = viewW * viewH;
	::ZeroMemory (dibBits, pixelCount * 4);

	::SetTextColor (memDC, RGB (255, 255, 255));
	::SetBkMode (memDC, TRANSPARENT);
	HFONT oldFont = (HFONT)::SelectObject (memDC, (HFONT)::GetStockObject (DEFAULT_GUI_FONT));

	for (int i = 0; i < numLabels; ++i) {
		Vector3 ndc;
		CameraClass::ProjectionResType res = camera->Project (ndc, labels[i].worldPos);
		if (res == CameraClass::OUTSIDE_NEAR_CLIP || res == CameraClass::OUTSIDE_FAR_CLIP)
			continue;

		int sx = (int)((ndc.X * 0.5f + 0.5f) * (float)viewW) + 4;
		int sy = (int)((0.5f - ndc.Y * 0.5f) * (float)viewH) - 8;
		::TextOutA (memDC, sx, sy, labels[i].name, (int)strlen (labels[i].name));
	}

	::SelectObject (memDC, oldFont);
	::GdiFlush ();

	for (int p = 0; p < pixelCount; ++p) {
		BYTE lum = (BYTE)(pixels[p] & 0xFF);
		pixels[p] = ((DWORD)lum << 24) | 0x00FFFFFF;
	}

	IDirect3DTexture8 *tex = nullptr;
#if defined(BUILD_WITH_D3D9)
	// D3D9Ex has no managed pool, and a dynamic texture locks in the default one
	HRESULT texResult = dev->CreateTexture (viewW, viewH, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
#else
	HRESULT texResult = dev->CreateTexture (viewW, viewH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
#endif
	if (SUCCEEDED (texResult) && tex != nullptr) {
		D3DLOCKED_RECT lr;
		if (SUCCEEDED (tex->LockRect (0, &lr, nullptr, 0))) {
			BYTE *dst = (BYTE *)lr.pBits;
			const BYTE *src = (const BYTE *)dibBits;
			for (int y = 0; y < viewH; ++y)
				::memcpy (dst + y * lr.Pitch, src + y * viewW * 4, viewW * 4);
			tex->UnlockRect (0);

			struct TLVertex { float x, y, z, rhw; DWORD color; float u, v; };
			const DWORD FVF_TLVERTEX = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
			TLVertex verts[4] = {
				{ 0.0f,         0.0f,         0.0f, 1.0f, labelColor, 0.0f, 0.0f },
				{ (float)viewW, 0.0f,         0.0f, 1.0f, labelColor, 1.0f, 0.0f },
				{ 0.0f,         (float)viewH, 0.0f, 1.0f, labelColor, 0.0f, 1.0f },
				{ (float)viewW, (float)viewH, 0.0f, 1.0f, labelColor, 1.0f, 1.0f },
			};

			DWORD oldAlphaBlend, oldSrcBlend, oldDestBlend, oldZEnable, oldZWrite, oldCull, oldLighting, oldFog;
			dev->GetRenderState (D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
			dev->GetRenderState (D3DRS_SRCBLEND,         &oldSrcBlend);
			dev->GetRenderState (D3DRS_DESTBLEND,        &oldDestBlend);
			dev->GetRenderState (D3DRS_ZENABLE,          &oldZEnable);
			dev->GetRenderState (D3DRS_ZWRITEENABLE,     &oldZWrite);
			dev->GetRenderState (D3DRS_CULLMODE,         &oldCull);
			dev->GetRenderState (D3DRS_LIGHTING,         &oldLighting);
			dev->GetRenderState (D3DRS_FOGENABLE,        &oldFog);

			DWORD oldColorOp, oldColorArg1, oldColorArg2, oldAlphaOp, oldAlphaArg1;
			dev->GetTextureStageState (0, D3DTSS_COLOROP,   &oldColorOp);
			dev->GetTextureStageState (0, D3DTSS_COLORARG1, &oldColorArg1);
			dev->GetTextureStageState (0, D3DTSS_COLORARG2, &oldColorArg2);
			dev->GetTextureStageState (0, D3DTSS_ALPHAOP,   &oldAlphaOp);
			dev->GetTextureStageState (0, D3DTSS_ALPHAARG1, &oldAlphaArg1);

			DWORD oldMinFilter, oldMagFilter, oldMipFilter;
			VIEW_GET_FILTER (dev, MINFILTER, &oldMinFilter);
			VIEW_GET_FILTER (dev, MAGFILTER, &oldMagFilter);
			VIEW_GET_FILTER (dev, MIPFILTER, &oldMipFilter);

			IDirect3DBaseTexture8 *oldTex = nullptr;
			dev->GetTexture (0, &oldTex);
			DWORD oldVS = 0;
			VIEW_GET_FVF (dev, &oldVS);

			dev->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState (D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
			dev->SetRenderState (D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
			dev->SetRenderState (D3DRS_ZENABLE,          D3DZB_FALSE);
			dev->SetRenderState (D3DRS_ZWRITEENABLE,     FALSE);
			dev->SetRenderState (D3DRS_CULLMODE,         D3DCULL_NONE);
			dev->SetRenderState (D3DRS_LIGHTING,         FALSE);
			dev->SetRenderState (D3DRS_FOGENABLE,        FALSE);

			dev->SetTextureStageState (0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
			dev->SetTextureStageState (0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			dev->SetTextureStageState (0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			dev->SetTextureStageState (0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
			dev->SetTextureStageState (0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			VIEW_SET_FILTER (dev, MINFILTER, D3DTEXF_POINT);
			VIEW_SET_FILTER (dev, MAGFILTER, D3DTEXF_POINT);
			VIEW_SET_FILTER (dev, MIPFILTER, D3DTEXF_NONE);

			dev->SetTexture (0, tex);
			DX8_SET_FVF (dev, FVF_TLVERTEX);
			dev->DrawPrimitiveUP (D3DPT_TRIANGLESTRIP, 2, verts, sizeof (TLVertex));

			DX8_SET_FVF (dev, oldVS);
			dev->SetTexture (0, oldTex);
			if (oldTex) oldTex->Release ();

			dev->SetTextureStageState (0, D3DTSS_COLOROP,   oldColorOp);
			dev->SetTextureStageState (0, D3DTSS_COLORARG1, oldColorArg1);
			dev->SetTextureStageState (0, D3DTSS_COLORARG2, oldColorArg2);
			dev->SetTextureStageState (0, D3DTSS_ALPHAOP,   oldAlphaOp);
			dev->SetTextureStageState (0, D3DTSS_ALPHAARG1, oldAlphaArg1);
			VIEW_SET_FILTER (dev, MINFILTER, oldMinFilter);
			VIEW_SET_FILTER (dev, MAGFILTER, oldMagFilter);
			VIEW_SET_FILTER (dev, MIPFILTER, oldMipFilter);

			dev->SetRenderState (D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
			dev->SetRenderState (D3DRS_SRCBLEND,         oldSrcBlend);
			dev->SetRenderState (D3DRS_DESTBLEND,        oldDestBlend);
			dev->SetRenderState (D3DRS_ZENABLE,          oldZEnable);
			dev->SetRenderState (D3DRS_ZWRITEENABLE,     oldZWrite);
			dev->SetRenderState (D3DRS_CULLMODE,         oldCull);
			dev->SetRenderState (D3DRS_LIGHTING,         oldLighting);
			dev->SetRenderState (D3DRS_FOGENABLE,        oldFog);

			DX8Wrapper::Invalidate_Cached_Render_States ();
		}
		tex->Release ();
	}

	::SelectObject (memDC, oldBmp);
	::DeleteObject (dib);
	::DeleteDC (memDC);
}

static void Render_SubObj_Name_Overlays (CGraphicView *view, CW3DViewDoc *doc, CameraClass *camera)
{
	if (!doc->GetShowSubObjNames ())
		return;

	RenderObjClass *robj = doc->GetDisplayedObject ();
	if (robj == nullptr)
		return;

	int numSubs = robj->Get_Num_Sub_Objects ();
	if (numSubs <= 0)
		return;

	NameLabel *labels = new NameLabel[numSubs];
	int count = 0;

	for (int i = 0; i < numSubs; ++i) {
		RenderObjClass *sub = robj->Get_Sub_Object (i);
		if (sub == nullptr)
			continue;
		labels[count].name     = sub->Get_Name ();
		labels[count].worldPos = sub->Get_Transform ().Get_Translation ();
		++count;
		sub->Release_Ref ();
	}

	Render_Name_Labels (view, camera, labels, count, 0xFFFFFF00); // yellow
	delete[] labels;
}

static void Render_Bone_Name_Overlays (CGraphicView *view, CW3DViewDoc *doc, CameraClass *camera)
{
	if (!doc->GetShowBoneNames ())
		return;

	RenderObjClass *robj = doc->GetDisplayedObject ();
	if (robj == nullptr)
		return;

	int numBones = robj->Get_Num_Bones ();
	if (numBones <= 0)
		return;

	NameLabel *labels = new NameLabel[numBones];
	int count = 0;

	for (int i = 0; i < numBones; ++i) {
		const char *boneName = robj->Get_Bone_Name (i);
		if (boneName == nullptr || boneName[0] == '\0')
			continue;
		labels[count].name     = boneName;
		labels[count].worldPos = robj->Get_Bone_Transform (i).Get_Translation ();
		++count;
	}

	Render_Name_Labels (view, camera, labels, count, 0xFF00FF00); // green, matches bone diamonds
	delete[] labels;
}


// TheSuperHackers @feature Tria 18/04/2026 Render bone overlays as green wireframe diamonds
// with colored XYZ axis lines overlaid on the 3D model.
struct BoneVertex
{
	float x, y, z;
	DWORD color;
};

static void Render_Bone_Overlays (CW3DViewDoc *doc)
{
	if (doc == nullptr || !doc->GetShowBones ())
		return;

	RenderObjClass *robj = doc->GetDisplayedObject ();
	if (robj == nullptr)
		return;

	int numBones = robj->Get_Num_Bones ();
	if (numBones <= 0)
		return;

	// Set up overlay render state: always-on-top, no depth, no texture
	ShaderClass shader = ShaderClass::_PresetOpaqueSolidShader;
	shader.Set_Depth_Compare (ShaderClass::PASS_ALWAYS);
	shader.Set_Depth_Mask (ShaderClass::DEPTH_WRITE_DISABLE);
	DX8Wrapper::Set_Shader (shader);
	DX8Wrapper::Set_Texture (0, nullptr);

	VertexMaterialClass *vm = VertexMaterialClass::Get_Preset (VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material (vm);
	REF_PTR_RELEASE (vm);

	Matrix3D identity(true);
	DX8Wrapper::Set_Transform (D3DTS_WORLD, identity);
	DX8Wrapper::Apply_Render_State_Changes ();

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8 ();
	if (dev == nullptr)
		return;

	DX8_SET_FVF (dev, D3DFVF_XYZ | D3DFVF_DIFFUSE);

	const DWORD colorGreen = 0xFF00FF00;
	const DWORD colorRed   = 0xFFFF0000;
	const DWORD colorBlue  = 0xFF4444FF;
	const float diamondSize = doc->GetBoneDiamondSize ();
	const float axisLength  = diamondSize * 3.3f;

	// Max vertices per bone: 24 line endpoints (12 diamond edges) + 6 axis endpoints (3 axes)
	// = 30 vertices per bone
	const int kMaxVerts = 30;
	BoneVertex *verts = new BoneVertex[numBones * kMaxVerts];
	int vertCount = 0;

	for (int i = 0; i < numBones; i++) {
		const Matrix3D &tm = robj->Get_Bone_Transform (i);
		Vector3 pos = tm.Get_Translation ();
		Vector3 ax = tm.Get_X_Vector ();
		Vector3 ay = tm.Get_Y_Vector ();
		Vector3 az = tm.Get_Z_Vector ();

		// Normalize axes for consistent diamond size
		float lenX = ax.Length ();
		float lenY = ay.Length ();
		float lenZ = az.Length ();
		if (lenX > 0.0001f) ax *= (1.0f / lenX);
		if (lenY > 0.0001f) ay *= (1.0f / lenY);
		if (lenZ > 0.0001f) az *= (1.0f / lenZ);

		// Diamond (octahedron) - 6 pole points
		Vector3 px = pos + ax * diamondSize;
		Vector3 nx = pos - ax * diamondSize;
		Vector3 py = pos + ay * diamondSize;
		Vector3 ny = pos - ay * diamondSize;
		Vector3 pz = pos + az * diamondSize;
		Vector3 nz = pos - az * diamondSize;

		// 12 edges of the octahedron as line pairs
		// Top half (pz to equator)
		BoneVertex *v = &verts[vertCount];
		#define BONE_LINE(A, B, C) \
			v->x = (A).X; v->y = (A).Y; v->z = (A).Z; v->color = (C); v++; \
			v->x = (B).X; v->y = (B).Y; v->z = (B).Z; v->color = (C); v++;

		BONE_LINE(pz, px, colorGreen);
		BONE_LINE(pz, py, colorGreen);
		BONE_LINE(pz, nx, colorGreen);
		BONE_LINE(pz, ny, colorGreen);
		// Bottom half (nz to equator)
		BONE_LINE(nz, px, colorGreen);
		BONE_LINE(nz, py, colorGreen);
		BONE_LINE(nz, nx, colorGreen);
		BONE_LINE(nz, ny, colorGreen);
		// Equator ring
		BONE_LINE(px, py, colorGreen);
		BONE_LINE(py, nx, colorGreen);
		BONE_LINE(nx, ny, colorGreen);
		BONE_LINE(ny, px, colorGreen);

		// Axis lines
		if (doc->GetShowBonePivots ()) {
			Vector3 axEnd = pos + ax * axisLength;
			Vector3 ayEnd = pos + ay * axisLength;
			Vector3 azEnd = pos + az * axisLength;
			BONE_LINE(pos, axEnd, colorRed);
			BONE_LINE(pos, ayEnd, colorGreen);
			BONE_LINE(pos, azEnd, colorBlue);
		}

		#undef BONE_LINE

		vertCount = (int)(v - verts);
	}

	if (vertCount > 0) {
		dev->DrawPrimitiveUP (D3DPT_LINELIST, vertCount / 2, verts, sizeof(BoneVertex));
	}

	delete[] verts;
}


// TheSuperHackers @feature Tria 23/04/2026 Render a highlight overlay for the currently selected
// bone or sub-object. Bones get a cyan diamond + axes + label. Sub-objects get a magenta diamond +
// axes + label showing their pivot position and orientation.
static void Render_Selected_Item_Overlay (CGraphicView *view, CW3DViewDoc *doc, CameraClass *camera)
{
	if (doc == nullptr)
		return;

	ASSET_TYPE selType = doc->GetSelectedItemType ();
	if (selType != TypeBone && selType != TypeMesh)
		return;

	const StringClass &selName = doc->GetSelectedItemName ();
	if (selName.Is_Empty ())
		return;

	RenderObjClass *robj = doc->GetDisplayedObject ();
	if (robj == nullptr)
		return;

	// Resolve the transform for the selected item
	Matrix3D tm;
	bool found = false;

	if (selType == TypeBone) {
		int boneIdx = robj->Get_Bone_Index (selName);
		if (boneIdx >= 0) {
			tm = robj->Get_Bone_Transform (boneIdx);
			found = true;
		}
	} else {
		// TypeMesh — sub-object pivot.
		// Sub-object names are stored as short names ("TURRET"), but the render object
		// stores them as "HIERARCHY.TURRET". Try both.
		RenderObjClass *sub = robj->Get_Sub_Object_By_Name (selName, nullptr);
		if (sub == nullptr) {
			// Try with hierarchy prefix stripped (search by iterating)
			int numSubs = robj->Get_Num_Sub_Objects ();
			for (int s = 0; s < numSubs && sub == nullptr; ++s) {
				RenderObjClass *candidate = robj->Get_Sub_Object (s);
				if (candidate == nullptr)
					continue;
				const char *fullName = candidate->Get_Name ();
				const char *dot = ::strchr (fullName, '.');
				const char *shortName = (dot != nullptr) ? dot + 1 : fullName;
				if (selName == shortName)
					sub = candidate;
				else
					candidate->Release_Ref ();
			}
		}
		if (sub != nullptr) {
			tm = sub->Get_Transform ();
			sub->Release_Ref ();
			found = true;
		} else {
			// Fallback: look up as a bone (sub-objects can also be bones)
			int boneIdx = robj->Get_Bone_Index (selName);
			if (boneIdx >= 0) {
				tm = robj->Get_Bone_Transform (boneIdx);
				found = true;
			}
		}
	}

	if (!found)
		return;

	Vector3 pos = tm.Get_Translation ();
	Vector3 ax  = tm.Get_X_Vector ();
	Vector3 ay  = tm.Get_Y_Vector ();
	Vector3 az  = tm.Get_Z_Vector ();

	// Normalize axes
	float lenX = ax.Length (); if (lenX > 0.0001f) ax *= (1.0f / lenX);
	float lenY = ay.Length (); if (lenY > 0.0001f) ay *= (1.0f / lenY);
	float lenZ = az.Length (); if (lenZ > 0.0001f) az *= (1.0f / lenZ);

	const float dsize      = doc->GetBoneDiamondSize () * 1.5f; // slightly larger than regular bones
	const float axisLength = dsize * 3.3f;

	// Selected bone = cyan diamond; selected sub-object = magenta diamond
	const DWORD colorDiamond = (selType == TypeBone) ? 0xFF00FFFF : 0xFFFF00FF;
	const DWORD colorRed     = 0xFFFF4444;
	const DWORD colorGreen2  = 0xFF44FF44;
	const DWORD colorBlue    = 0xFF4444FF;

	// Diamond pole points
	Vector3 px = pos + ax * dsize;
	Vector3 nx = pos - ax * dsize;
	Vector3 py = pos + ay * dsize;
	Vector3 ny = pos - ay * dsize;
	Vector3 pz = pos + az * dsize;
	Vector3 nz = pos - az * dsize;

	// 12 diamond edges + 3 axis pairs = 30 verts max
	BoneVertex verts[30];
	int vc = 0;

	#define SEL_LINE(A, B, C) \
		verts[vc].x = (A).X; verts[vc].y = (A).Y; verts[vc].z = (A).Z; verts[vc].color = (C); ++vc; \
		verts[vc].x = (B).X; verts[vc].y = (B).Y; verts[vc].z = (B).Z; verts[vc].color = (C); ++vc;

	SEL_LINE(pz, px, colorDiamond);  SEL_LINE(pz, py, colorDiamond);
	SEL_LINE(pz, nx, colorDiamond);  SEL_LINE(pz, ny, colorDiamond);
	SEL_LINE(nz, px, colorDiamond);  SEL_LINE(nz, py, colorDiamond);
	SEL_LINE(nz, nx, colorDiamond);  SEL_LINE(nz, ny, colorDiamond);
	SEL_LINE(px, py, colorDiamond);  SEL_LINE(py, nx, colorDiamond);
	SEL_LINE(nx, ny, colorDiamond);  SEL_LINE(ny, px, colorDiamond);

	// Axis lines — only for sub-objects (bones use diamond only)
	if (selType == TypeMesh) {
		SEL_LINE(pos, pos + ax * axisLength, colorRed);
		SEL_LINE(pos, pos + ay * axisLength, colorGreen2);
		SEL_LINE(pos, pos + az * axisLength, colorBlue);
	}

	#undef SEL_LINE

	// Render using the same depth-always shader as bone overlays
	ShaderClass shader = ShaderClass::_PresetOpaqueSolidShader;
	shader.Set_Depth_Compare (ShaderClass::PASS_ALWAYS);
	shader.Set_Depth_Mask (ShaderClass::DEPTH_WRITE_DISABLE);
	DX8Wrapper::Set_Shader (shader);
	DX8Wrapper::Set_Texture (0, nullptr);

	VertexMaterialClass *vm = VertexMaterialClass::Get_Preset (VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material (vm);
	REF_PTR_RELEASE (vm);

	Matrix3D identity (true);
	DX8Wrapper::Set_Transform (D3DTS_WORLD, identity);
	DX8Wrapper::Apply_Render_State_Changes ();

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8 ();
	if (dev != nullptr) {
		DX8_SET_FVF (dev, D3DFVF_XYZ | D3DFVF_DIFFUSE);
		dev->DrawPrimitiveUP (D3DPT_LINELIST, vc / 2, verts, sizeof (BoneVertex));
	}

	// Name label above the pivot — same color as the diamond
	NameLabel label;
	label.name     = (const char *)selName;
	label.worldPos = pos;
	Render_Name_Labels (view, camera, &label, 1, colorDiamond);
}


/////////////////////////////////////////////////////////////////////////
//  Local Prototypes
/////////////////////////////////////////////////////////////////////////
void CALLBACK fnTimerCallback (UINT, UINT, DWORD, DWORD, DWORD);


IMPLEMENT_DYNCREATE(CGraphicView, CView)

// TheSuperHackers @feature Tria Session-only toggle + colour for the backface tint pass.
bool CGraphicView::s_ShowBackfaceTint = false;
Vector3 CGraphicView::s_BackfaceTintColor (0.63f, 0.38f, 0.78f);	// default purple

// Second pass: draw the normally-culled back faces flat-tinted with the tint colour.
// Cull is inverted via ShaderClass; the colour comes from a flat ambient on untextured
// geometry. No-op unless the toggle is on. Shared by the main viewport and the Material
// Viewer preview.
void CGraphicView::Render_Backface_Tint_Pass (SceneClass *scene, CameraClass *camera)
{
	if (!s_ShowBackfaceTint || scene == nullptr) {
		return;
	}

	Vector3 saved_ambient = scene->Get_Ambient_Light ();
	ShaderClass::Force_Backface_Tint (true);
	WW3D::Enable_Texturing (false);
	scene->Set_Ambient_Light (s_BackfaceTintColor);
	WW3D::Render (scene, camera, FALSE, FALSE);
	scene->Set_Ambient_Light (saved_ambient);
	WW3D::Enable_Texturing (true);
	ShaderClass::Force_Backface_Tint (false);
}

COLORREF CGraphicView::Tint_Color_To_ColorRef (const Vector3 &c)
{
	return RGB ((int)(c.X * 255.0f + 0.5f),
		(int)(c.Y * 255.0f + 0.5f), (int)(c.Z * 255.0f + 0.5f));
}

Vector3 CGraphicView::ColorRef_To_Tint_Color (COLORREF c)
{
	return Vector3 (GetRValue (c) / 255.0f, GetGValue (c) / 255.0f, GetBValue (c) / 255.0f);
}

// TheSuperHackers @feature Tria Ground plane: a checkered quad at ground_z plus
// a soft elliptical blob shadow under the object's footprint (fixed-function
// DX8 has no real shadow casting; a blob matches what the game itself ships).
// Runs inside the render bracket BEFORE the scene render on purpose: the plane
// writes depth first, so the object's opaque parts occlude it correctly, and
// WW3D's sorted translucent flush afterwards blends the object's alpha/additive
// "glow" over the ground. Shared by the main viewport and the Material Viewer
// preview pane.
// Per-vertex lighting for the ground checker: base albedo modulated by the
// scene ambient plus the scene light's diffuse. The plane's normal is +Z, so
// N.L is simply the normalized vertical reach towards the (point) light.
static DWORD Lit_Ground_Color (const Vector3 &base, float x, float y, float z,
	const Vector3 &ambient, bool has_light, const Vector3 &light_pos, const Vector3 &light_diffuse)
{
	Vector3 intensity = ambient;
	if (has_light) {
		Vector3 dir = light_pos - Vector3 (x, y, z);
		float len = dir.Length ();
		if (len > 0.001F) {
			float ndotl = dir.Z / len;
			if (ndotl > 0.0F) {
				intensity += light_diffuse * ndotl;
			}
		}
	}
	int r = (int)(base.X * intensity.X * 255.0F + 0.5F);
	int g = (int)(base.Y * intensity.Y * 255.0F + 0.5F);
	int b = (int)(base.Z * intensity.Z * 255.0F + 0.5F);
	r = (r > 255) ? 255 : r;
	g = (g > 255) ? 255 : g;
	b = (b > 255) ? 255 : b;
	return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void CGraphicView::Render_Ground (CameraClass *camera, RenderObjClass *obj, const SphereClass &sphere, float ground_z, const Vector3 &ambient, LightClass *light)
{
	if (camera == nullptr) {
		return;
	}

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8 ();
	if (dev == nullptr) {
		return;
	}

	// Size/centre from the displayed object; sane fallback while nothing is loaded.
	SphereClass bounds = (obj != nullptr) ? obj->Get_Bounding_Sphere () : sphere;
	float radius = (bounds.Radius > 0.01F) ? bounds.Radius : 10.0F;
	Vector3 foot_center = bounds.Center;
	float foot_x = radius * 0.5F;
	float foot_y = radius * 0.5F;
	if (obj != nullptr) {
		const AABoxClass &box = obj->Get_Bounding_Box ();
		foot_center = box.Center;
		foot_x = box.Extent.X;
		foot_y = box.Extent.Y;
	}

	// The scene render has not run yet this frame, so push this camera's
	// viewport + view/projection to the device first (WW3D::Render re-applies
	// them right after, so this affects only these two draws).
	camera->Apply ();

	ShaderClass shader = ShaderClass::_PresetOpaqueSolidShader;
	DX8Wrapper::Set_Shader (shader);
	DX8Wrapper::Set_Texture (0, nullptr);

	VertexMaterialClass *vm = VertexMaterialClass::Get_Preset (VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material (vm);
	if (vm != nullptr) {
		vm->Release_Ref ();
	}

	Matrix3D identity (true);
	DX8Wrapper::Set_Transform (D3DTS_WORLD, identity);
	DX8Wrapper::Apply_Render_State_Changes ();

	struct GroundVertex { float x, y, z; DWORD color; };

	// --- Checker plane: CELLS x CELLS cells over a square of 3x the bounding
	// radius. Two albedo tones per theme, lit per vertex by the scene's ambient
	// + scene light (Gouraud across each cell gives the light a soft pool). ---
	const int CELLS = 16;
	const bool dark = W3DDarkMode::IsDark ();
	const Vector3 base_a = dark ? Vector3 (0.34F, 0.35F, 0.37F) : Vector3 (0.72F, 0.73F, 0.75F);
	const Vector3 base_b = dark ? Vector3 (0.26F, 0.27F, 0.29F) : Vector3 (0.60F, 0.61F, 0.63F);

	Vector3 light_pos (0, 0, 0);
	Vector3 light_diffuse (0, 0, 0);
	const bool has_light = (light != nullptr);
	if (has_light) {
		light->Get_Diffuse (&light_diffuse);
		light_pos = light->Get_Transform ().Get_Translation ();
	}

	float half = radius * 3.0F;
	float cell = (half * 2.0F) / CELLS;
	GroundVertex plane_verts[CELLS * CELLS * 6];
	int v = 0;
	for (int cy = 0; cy < CELLS; cy++) {
		for (int cx = 0; cx < CELLS; cx++) {
			float x0 = foot_center.X - half + cx * cell;
			float y0 = foot_center.Y - half + cy * cell;
			float x1 = x0 + cell;
			float y1 = y0 + cell;
			const Vector3 &base = ((cx ^ cy) & 1) ? base_a : base_b;
			DWORD c00 = Lit_Ground_Color (base, x0, y0, ground_z, ambient, has_light, light_pos, light_diffuse);
			DWORD c10 = Lit_Ground_Color (base, x1, y0, ground_z, ambient, has_light, light_pos, light_diffuse);
			DWORD c11 = Lit_Ground_Color (base, x1, y1, ground_z, ambient, has_light, light_pos, light_diffuse);
			DWORD c01 = Lit_Ground_Color (base, x0, y1, ground_z, ambient, has_light, light_pos, light_diffuse);
			GroundVertex quad[6] = {
				{ x0, y0, ground_z, c00 }, { x1, y0, ground_z, c10 }, { x1, y1, ground_z, c11 },
				{ x0, y0, ground_z, c00 }, { x1, y1, ground_z, c11 }, { x0, y1, ground_z, c01 },
			};
			for (int i = 0; i < 6; i++) {
				plane_verts[v++] = quad[i];
			}
		}
	}

	DX8_SET_FVF (dev, D3DFVF_XYZ | D3DFVF_DIFFUSE);
	dev->DrawPrimitiveUP (D3DPT_TRIANGLELIST, CELLS * CELLS * 2, plane_verts, sizeof (GroundVertex));

	// --- Blob shadow: elliptical fan over the object's XY footprint, dark
	// centre fading to nothing at the rim; alpha-blended, no depth write,
	// nudged above the plane to avoid z-fighting. ---
	ShaderClass blob_shader = ShaderClass::_PresetAlphaSolidShader;
	blob_shader.Set_Depth_Mask (ShaderClass::DEPTH_WRITE_DISABLE);
	DX8Wrapper::Set_Shader (blob_shader);
	DX8Wrapper::Apply_Render_State_Changes ();

	const int SEGMENTS = 24;
	float blob_z = ground_z + max (0.002F * radius, 0.001F);
	float blob_x = foot_x * 1.2F;
	float blob_y = foot_y * 1.2F;
	GroundVertex fan_verts[SEGMENTS + 2];
	fan_verts[0] = { foot_center.X, foot_center.Y, blob_z, 0x8C000000 };
	for (int i = 0; i <= SEGMENTS; i++) {
		float angle = (float)i * (2.0F * WWMATH_PI / SEGMENTS);
		fan_verts[i + 1] = {
			foot_center.X + WWMath::Cos (angle) * blob_x,
			foot_center.Y + WWMath::Sin (angle) * blob_y,
			blob_z, 0x00000000 };
	}
	dev->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, SEGMENTS, fan_verts, sizeof (GroundVertex));

	DX8Wrapper::Invalidate_Cached_Render_States ();
}

// Turning the plane on re-derives its height from the currently displayed
// object, so it always appears directly underneath what is on screen.
void CGraphicView::Set_Show_Ground (bool onoff)
{
	m_bShowGround = onoff;
	if (onoff) {
		Reset_Ground_To_Object ();
	}
}

void CGraphicView::Reset_Ground_To_Object (void)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument ();
	m_GroundZ = Ground_Default_Z ((doc != nullptr) ? doc->GetDisplayedObject () : nullptr);
}

bool CGraphicView::Get_Ground_Z_Range (float &z_min, float &z_max) const
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument ();
	RenderObjClass *obj = (doc != nullptr) ? doc->GetDisplayedObject () : nullptr;
	if (obj == nullptr) {
		return false;
	}
	Ground_Z_Range (obj, z_min, z_max);
	return true;
}

// Ground height defaults to the object's bounding-box bottom (0 when null).
float CGraphicView::Ground_Default_Z (RenderObjClass *obj)
{
	if (obj == nullptr) {
		return 0.0F;
	}
	const AABoxClass &box = obj->Get_Bounding_Box ();
	return box.Center.Z - box.Extent.Z;
}

// Slider range: one full span (twice the half-height, floored at the bounding
// radius) either side of the bounding-box bottom, so the default lands
// mid-slider. Callers must ensure obj is non-null.
void CGraphicView::Ground_Z_Range (RenderObjClass *obj, float &z_min, float &z_max)
{
	const AABoxClass &box = obj->Get_Bounding_Box ();
	float bottom = box.Center.Z - box.Extent.Z;
	float span = box.Extent.Z * 2.0F;
	float radius = obj->Get_Bounding_Sphere ().Radius;
	if (span < radius) {
		span = radius;
	}
	if (span < 1.0F) {
		span = 1.0F;
	}
	z_min = bottom - span;
	z_max = bottom + span;
}


////////////////////////////////////////////////////////////////////////////
//
//  CGraphicView
//
////////////////////////////////////////////////////////////////////////////
CGraphicView::CGraphicView ()
    : m_bInitialized (FALSE),
      m_TimerID (0),
      m_TimerPeriod (0),
      m_bMouseDown (FALSE),
      m_bRMouseDown (FALSE),
      m_bMMouseDown (FALSE),
      m_AltOnMDown (false),
      m_bActive (TRUE),
      m_animationSpeed (1.0F),
      m_dwLastFrameUpdate (0),
		m_iWindowed (1),
      m_animationState (AnimInvalid),
      m_objectRotation (NoRotation),
		m_LightRotation (NoRotation),
		m_bLightMeshInScene (false),
		m_ParticleCountUpdate (0),
		m_CameraBonePosX (false),
		m_UpdateCounter (0),
      m_allowedCameraRotation (FreeRotation),
		m_InvertCameraY (true),
		m_bShowGround (false),
		m_GroundZ (0.0F),
		m_ObjectCenter (0.0f, 0.0f, 0.0f)
{
    // Get the windowed mode from the registry
    CString string_windowed = theApp.GetProfileString ("Config", "Windowed", "1");
	 m_iWindowed = ::atoi ((LPCTSTR)string_windowed);

	 // Restore the persisted "invert vertical camera movement" preference.
	 // Default ON (1) for a fresh profile — inverted vertical is the expected feel.
	 m_InvertCameraY = (theApp.GetProfileInt ("Config", "InvertCameraY", 1) != 0);
}

////////////////////////////////////////////////////////////////////////////
//
//  Set_Invert_Camera_Y
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_Invert_Camera_Y (bool onoff)
{
	m_InvertCameraY = onoff;
	theApp.WriteProfileInt ("Config", "InvertCameraY", onoff ? 1 : 0);
}


////////////////////////////////////////////////////////////////////////////
//
//  ~CGraphicView
//
////////////////////////////////////////////////////////////////////////////
CGraphicView::~CGraphicView ()
{
}



BEGIN_MESSAGE_MAP(CGraphicView, CView)
	//{{AFX_MSG_MAP(CGraphicView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_GETMINMAXINFO()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()



////////////////////////////////////////////////////////////////////////////
//
//  OnDraw
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnDraw (CDC* pDC)
{
	// Get the document to display
    CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

    // Are we in a valid state?
    if (!pDC->IsPrinting ())
    {
    }
}


#ifdef RTS_DEBUG
void CGraphicView::AssertValid() const
{
	CView::AssertValid();
}

void CGraphicView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif //RTS_DEBUG



////////////////////////////////////////////////////////////////////////////
//
//  PreCreateWindow
//
////////////////////////////////////////////////////////////////////////////
int
CGraphicView::OnCreate (LPCREATESTRUCT lpCreateStruct)
{
	// Allow the base class to process this message
    if (CView::OnCreate(lpCreateStruct) == -1)
		return -1;

    m_dwLastFrameUpdate = timeGetTime ();//::GetTickCount ();
	return 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  InitializeGraphicView
//
////////////////////////////////////////////////////////////////////////////
BOOL
CGraphicView::InitializeGraphicView ()
{
	// Assume failure
	BOOL bReturn = FALSE;
	if (g_iDeviceIndex < 0) {
		return FALSE;
	}

	m_bInitialized = FALSE;

	// Initialize the rendering engine with the information from
	// this window.
	RECT rect;
	GetClientRect (&rect);

	int cx = rect.right-rect.left;
	int cy = rect.bottom-rect.top;
	if (m_iWindowed == 0) {
		cx = g_iWidth;
		cy = g_iHeight;
		((CW3DViewDoc *)GetDocument())->Show_Cursor (true);
	} else {
		((CW3DViewDoc *)GetDocument())->Show_Cursor (false);
	}

	bReturn = (WW3D::Set_Render_Device (g_iDeviceIndex,
													cx,
													cy,
													g_iBitsPerPixel,
													m_iWindowed) == WW3D_ERROR_OK);

    ASSERT (bReturn);
    if (bReturn && (m_pCamera == nullptr))
    {
        // Instantiate a new camera class
	    m_pCamera.Assign_No_Add_Ref (new CameraClass ());
        bReturn = (m_pCamera != nullptr);

        // Were we successful in creating a camera?
        ASSERT (m_pCamera);
        if (m_pCamera)
        {
            // Create a transformation matrix
            Matrix3D transform (1);
	        transform.Translate (Vector3 (0.0F, 0.0F, 35.0F));

	        // Point the camera in this direction (I think)
            m_pCamera->Set_Transform (transform);
        }

		  //
		  //	Attach the 'listener' to the camera
		  //
		  WWAudioClass::Get_Instance ()->Get_Sound_Scene ()->Attach_Listener_To_Obj (m_pCamera.Peek());
    }

	// TheSuperHackers @bugfix Tria 17/04/2026 Only reset FOV and load assets when device is valid.
	if (bReturn) {
		Reset_FOV ();
	}

	 if (bReturn && m_pLightMesh == nullptr)
	 {
		ResourceFileClass light_mesh_file (nullptr, "Light.w3d");
		WW3DAssetManager::Get_Instance()->Load_3D_Assets (light_mesh_file);

		m_pLightMesh.Assign_No_Add_Ref (WW3DAssetManager::Get_Instance()->Create_Render_Obj ("LIGHT"));
		ASSERT (m_pLightMesh != nullptr);
		m_bLightMeshInScene = false;
	 }


    // Remember whether or not we are initialized
    m_bInitialized = bReturn;

    if (m_bInitialized && (m_TimerID == 0))
    {
		// Kick off a timer that we can use to update
		// the display (kinda like a game loop iterator)
		// TheSuperHackers @feature Tria 25/04/2026 Uncap viewport FPS by driving the
		// render at the system's minimum timer period (typically 1ms) instead of a
		// 16ms / ~60 FPS floor. timeBeginPeriod ensures the requested resolution is
		// actually granted by the OS.
		TIMECAPS caps = { 0 };
		::timeGetDevCaps (&caps, sizeof (TIMECAPS));
		UINT freq = caps.wPeriodMin > 0 ? caps.wPeriodMin : 1U;
		::timeBeginPeriod (freq);
		m_TimerPeriod = freq;
		m_TimerID = (UINT)::timeSetEvent (freq,
													 freq,
													 fnTimerCallback,
													 (DWORD)m_hWnd,
													 TIME_PERIODIC);
    }

	// Return the TRUE/FALSE result code
	return bReturn;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSize
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnSize
(
    UINT nType,
    int cx,
    int cy
)
{
	// Allow the base class to process this message
    CView::OnSize (nType, cx, cy);

	if (m_bInitialized) {

		if (m_iWindowed == 0) {
			cx = g_iWidth;
			cy = g_iHeight;
		}

		// TheSuperHackers @bugfix Tria 18/04/2026 Prevents crash from setting D3D device to zero dimensions.
		if (cx < 1 || cy < 1)
			return;

		// Change the resolution of the rendering device to
		// match that of the view's current dimensions
		if (m_iWindowed == 1) {
			WW3D::Set_Device_Resolution (cx, cy, g_iBitsPerPixel, m_iWindowed);
		}

		// Force a repaint of the screen
		Reset_FOV ();
		RepaintView ();

		// Keep the clip rect in sync if a drag is in progress.
		if (m_bMouseDown || m_bRMouseDown || m_bMMouseDown)
		{
			Clip_Cursor_To_View ();
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDestroy
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnDestroy ()
{
	// Allow the base class to process this message
	CView::OnDestroy ();

	//
	//	Remove the listener from the camera
	//
	WWAudioClass::Get_Instance ()->Get_Sound_Scene ()->Attach_Listener_To_Obj (nullptr);

	//
	// Free the camera object
	//
	m_pCamera.Clear();
	m_pLightMesh.Clear();

	// Is there an update thread running?
	if (m_TimerID == 0) {

		// Stop the timer
		::timeKillEvent ((UINT)m_TimerID);
		m_TimerID = 0;
		if (m_TimerPeriod != 0) {
			::timeEndPeriod (m_TimerPeriod);
			m_TimerPeriod = 0;
		}
	}

	// Cache this information in the registry
	TCHAR temp_string[10];
	::itoa (m_iWindowed, temp_string, 10);
	theApp.WriteProfileString ("Config", "Windowed", temp_string);

	// We are no longer initialized
	m_bInitialized = FALSE;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnInitialUpdate
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnInitialUpdate ()
{
	// Allow the base class to process this message
    CView::OnInitialUpdate ();

	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
	if (doc)
	{
		// Ask the document to initialize the scene (if it hasn't
		// already done so)
		doc->InitScene ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Lowest_LOD
//
////////////////////////////////////////////////////////////////////////////
void
Set_Lowest_LOD (RenderObjClass *render_obj)
{
	if (render_obj != nullptr) {
		for (int index = 0; index < render_obj->Get_Num_Sub_Objects (); index ++) {
			RenderObjClass *psub_obj = render_obj->Get_Sub_Object (index);
			if (psub_obj != nullptr) {
				Set_Lowest_LOD (psub_obj);
			}
			REF_PTR_RELEASE (psub_obj);
		}

		//
		// Switcht this LOD to its lowest level
		//
		if (render_obj->Class_ID () == RenderObjClass::CLASSID_HLOD) {
			((HLodClass *)render_obj)->Set_LOD_Level (0);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Allow_Update
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Allow_Update (bool onoff)
{
	if (onoff) {
		m_UpdateCounter --;
	} else {
		m_UpdateCounter ++;
	}
}

////////////////////////////////////////////////////////////////////////////
//
//  RepaintView
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::RepaintView
(
	BOOL bUpdateAnimation,
	DWORD ticks_to_use
)
{
	//
	//	Simple check to avoid re-entrance
	//
	static bool _already_painting = false;
	if (_already_painting)
		return;
	_already_painting = true;

	 //
	 // Are we in a valid state?
	 //
	 CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
	 if (doc->Is_Initialized () && doc->GetScene () && m_UpdateCounter == 0) {

		// Only update the frame if the animation is
		// supposed to be playing
		int cur_ticks = timeGetTime();
		int ticks_elapsed = cur_ticks - m_dwLastFrameUpdate;
		m_dwLastFrameUpdate = cur_ticks;

		// Update the W3D frame times according to our elapsed tick count
		if (ticks_to_use == 0)
		{
			WW3D::Update_Logic_Frame_Time(ticks_elapsed * m_animationSpeed);
			WW3D::Sync(WW3D::Get_Fractional_Sync_Milliseconds() >= WWSyncMilliseconds);
		}
		else
		{
			WW3D::Update_Logic_Frame_Time(ticks_to_use);
			WW3D::Sync(true);
		}

		// Do we need to update the current animation?
		if ((m_animationState == AnimPlaying) &&
			bUpdateAnimation)
		{
			float animationSpeed = ((float)ticks_elapsed) / 1000.00F;
			animationSpeed = (animationSpeed * m_animationSpeed);
			doc->UpdateFrame (animationSpeed);
		}

		// Perform the object rotation if necessary
		if ((m_objectRotation != NoRotation) &&
			(bUpdateAnimation == TRUE))
		{
			Rotate_Object ();
		}

		// Perform the light rotation if necessary
		if ((m_LightRotation != NoRotation) &&
			(bUpdateAnimation == TRUE))
		{
			Rotate_Light ();
		}

		// A click on the view cube turns the camera over a few frames
		Update_View_Cube_Snap (doc);

		// Reset the current lod to be the lowest possible LOD...
		RenderObjClass *prender_obj = doc->GetDisplayedObject ();
		if ((prender_obj != nullptr) &&
			 (doc->GetScene ()->Are_LODs_Switching ()))
		{
			Set_Lowest_LOD (prender_obj);
		}

		// Update the metal map
		// assuming object is at origin
		MetalMapManagerClass *metal=_TheAssetMgr->Peek_Metal_Map_Manager();
		if (metal)
		{
			LightClass *pscene_light = doc->GetSceneLight();
			Vector3 ambient,diffuse,l,v;
			ambient=doc->GetScene()->Get_Ambient_Light();
			pscene_light->Get_Diffuse(&diffuse);
			l=pscene_light->Get_Position();
			l.Normalize();
			v=m_pCamera->Get_Position();
			v.Normalize();
			metal->Update_Lighting(ambient,diffuse,l,v);
			metal->Update_Textures();
		}

		//
		//	Render the background BMP
		//
		WW3D::Begin_Render (TRUE, TRUE, doc->GetBackgroundColor ());
		WW3D::Render (doc->Get2DScene (), doc->Get2DCamera (), FALSE, FALSE);

		//
		// Render the background scene
		//
		if (doc->GetBackgroundObjectName ().GetLength () > 0) {
			WW3D::Render (doc->GetBackObjectScene (), doc->GetBackObjectCamera (), FALSE, FALSE);
		}

		//
		// TheSuperHackers @feature Tria Ground plane under the displayed object
		// (Ground menu). Drawn before the scene so its depth is in place; see
		// Render_Ground for the ordering rationale.
		//
		if (m_bShowGround) {
			Render_Ground (m_pCamera.Peek (), doc->GetDisplayedObject (), m_ViewedSphere, m_GroundZ,
				doc->GetScene ()->Get_Ambient_Light (), doc->GetSceneLight ());
		}

		//
		// Render the main scene
		//
		DWORD pt_high = 0L;

		// Wait for all previous rendering to complete before starting benchmark.
		DWORD profile_time = ::Get_CPU_Clock (pt_high);

		WW3D::Render (doc->GetScene (), m_pCamera.Peek(), FALSE, FALSE);

		// TheSuperHackers @feature Tria Second pass: tint the normally-culled back
		// faces (gated by the View > W3D Shaders menu toggle).
		Render_Backface_Tint_Pass (doc->GetScene (), m_pCamera.Peek ());

		// Wait for all rendering to complete before stopping benchmark.
		DWORD milliseconds = (::Get_CPU_Clock (pt_high) - profile_time) / 1000;

		// TheSuperHackers @feature Tria 18/04/2026 Render bone overlays after main scene.
		Render_Bone_Overlays (doc);

		//
		// Render the cursor
		//
		WW3D::Render (doc->GetCursorScene (), doc->Get2DCamera (), FALSE, FALSE);

		// Render the dazzles
		doc->Render_Dazzles(m_pCamera.Peek());

		// TheSuperHackers @feature Tria 22/04/2026 Blit sub-object name labels onto the back buffer
		// before Present, so the text is part of the frame and doesn't flicker.
		Render_SubObj_Name_Overlays (this, doc, m_pCamera.Peek ());
		Render_Bone_Name_Overlays (this, doc, m_pCamera.Peek ());

		// TheSuperHackers @feature Tria 23/04/2026 Highlight selected bone or sub-object in viewport.
		Render_Selected_Item_Overlay (this, doc, m_pCamera.Peek ());

		// View cube and the 7 key statistics, over everything else
		m_ViewCube.Render (m_hWnd, m_pCamera->Get_Transform ());
		if (ViewportStats::Is_Visible ())
		{
			ViewportStats::Render (prender_obj);
		}

		// Finish out the rendering process
		WW3D::End_Render ();

		//
		//	Let the audio class think
		//
		WWAudioClass::Get_Instance ()->On_Frame_Update (WW3D::Get_Logic_Frame_Time_Milliseconds());

		//
		//	Update the count of particles and polys in the status bar
		//
		if ((cur_ticks - m_ParticleCountUpdate > 250)) {
			m_ParticleCountUpdate = cur_ticks;
			doc->Update_Particle_Count ();

			int polys = (prender_obj != nullptr) ? prender_obj->Get_Num_Polys () : 0;
			int verts = ViewportStats::Count_Vertices (prender_obj);
			((CMainFrame *)::AfxGetMainWnd ())->UpdatePolygonCount (polys, verts, polys);
		}

		//
		//	Update the frame time in the status bar
		//
		((CMainFrame *)::AfxGetMainWnd ())->Update_Frame_Time (milliseconds);
	}

	// TheSuperHackers @feature W3D Material Viewer: render its preview pane on the
	// same cadence, outside the main Begin_Render/End_Render bracket (no-op when
	// closed). While the viewer is open the main render above is paused
	// (Allow_Update), so this must run even when that block is skipped.
	CMaterialViewerFrame::RenderActivePreview ();

	_already_painting = false;
}


////////////////////////////////////////////////////////////////////////////
//
//  UpdateDisplay
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::UpdateDisplay ()
{
	// Get the document to display
    CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

    // Are we in a valid state?
    /*if (m_bInitialized && doc->GetScene ())
    {
        RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
        if (pCRenderObj)
        {
            Matrix3D transform = pCRenderObj->Get_Transform ();
            transform.Rotate_X (0.05F);
            transform.Rotate_Y (0.05F);
            transform.Rotate_Z (0.05F);

            pCRenderObj->Set_Transform (transform);
        }

		// Render the current view inside the frame
        WW3D::Begin_Render (TRUE, TRUE, Vector3 (0.2,0.4,0.6));
		WW3D::Render (doc->GetScene (), m_pCamera.Peek(), FALSE, FALSE);
		WW3D::End_Render ();
    } */
}


////////////////////////////////////////////////////////////////////////////
//
//  WindowProc
//
////////////////////////////////////////////////////////////////////////////
LRESULT
CGraphicView::WindowProc
(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
	// While MMB is captured, swallow Alt syskey/syschar messages so Windows
	// doesn't activate the menu bar (which would steal focus, beep, and cause
	// GetKeyState(VK_MENU) to drop mid-drag — breaking Alt+MMB orbit).
	if (m_bMMouseDown && (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && wParam == VK_MENU) {
		return 0;
	}
	if (m_bMMouseDown && message == WM_SYSCHAR) {
		return 0;
	}

	// Is this the repaint message we are expecting?
	if (message == WM_USER+101) {

		//
		//	Force the repaint...
		//
		RepaintView ();
		RemoveProp (m_hWnd, "WaitingToProcess");

	} else if (message == WM_PAINT) {

		// If we are in fullscreen mode, then erase the window background
		if (m_iWindowed == 0) {

			// Get the client rectangle of the window
			RECT rect;
			GetClientRect (&rect);

			// Get the window's DC
			HDC hDC = ::GetDC (m_hWnd);
			if (hDC) {

				// Erase the background
				::FillRect (hDC, &rect, (HBRUSH)(COLOR_WINDOW + 1));
				::ReleaseDC (m_hWnd, hDC);
			}
		}

		RepaintView (FALSE);
		ValidateRect (nullptr);
		return 0;

	} else if (message == WM_KEYDOWN) {

		if ((wParam == VK_CONTROL) && (m_bLightMeshInScene == false)) {
			CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
			m_pLightMesh->Add (doc->GetScene ());
			m_bLightMeshInScene = true;
		}

	} else if (message == WM_KEYUP) {

		if ((wParam == VK_CONTROL) && (m_bLightMeshInScene == true)) {
			CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
			m_pLightMesh->Remove ();
			m_bLightMeshInScene = false;
		}
	}

	// Allow the base class to process this message
	return CView::WindowProc(message, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////
//
//  fnTimerCallback
//
////////////////////////////////////////////////////////////////////////////
void CALLBACK
fnTimerCallback
(
	UINT uID,
	UINT uMsg,
	DWORD dwUser,
	DWORD dw1,
	DWORD dw2
)
{
	HWND hwnd = (HWND)dwUser;
	if (hwnd != nullptr) {

		// Send this event off to the view to process (hackish, but fine for now)
		if ((GetProp (hwnd, "WaitingToProcess") == nullptr) &&
			 (GetProp (hwnd, "Inactive") == nullptr)) {

			SetProp (hwnd, "WaitingToProcess", (HANDLE)1);

			// Send the message to the view so it will be in the
			// same thread (Surrender doesn't seem to be thread-safe)
			::PostMessage (hwnd, WM_USER + 101, 0, 0L);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Clip_Cursor_To_View
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Clip_Cursor_To_View (void)
{
	RECT rect;
	GetClientRect (&rect);
	ClientToScreen (&rect);
	::ClipCursor (&rect);
}


////////////////////////////////////////////////////////////////////////////
//
//  Wrap_Cursor_In_View
//
//  When the cursor reaches within EDGE_MARGIN of a viewport edge during an
//  orbit drag, warp it to the opposite edge so rotation can continue past 360°.
//  Updates `point` and m_lastPoint so the next OnMouseMove delta isn't a jump.
//  Returns true if the cursor was wrapped.
//
////////////////////////////////////////////////////////////////////////////
bool
CGraphicView::Wrap_Cursor_In_View (CPoint &point)
{
	const int EDGE_MARGIN = 4;

	RECT rect;
	GetClientRect (&rect);

	const int w = rect.right  - rect.left;
	const int h = rect.bottom - rect.top;
	if (w <= 2 * EDGE_MARGIN || h <= 2 * EDGE_MARGIN) {
		return false;
	}

	int newX = point.x;
	int newY = point.y;

	if (point.x <= rect.left + EDGE_MARGIN) {
		newX = rect.right - EDGE_MARGIN - 1;
	} else if (point.x >= rect.right - EDGE_MARGIN - 1) {
		newX = rect.left + EDGE_MARGIN + 1;
	}

	if (point.y <= rect.top + EDGE_MARGIN) {
		newY = rect.bottom - EDGE_MARGIN - 1;
	} else if (point.y >= rect.bottom - EDGE_MARGIN - 1) {
		newY = rect.top + EDGE_MARGIN + 1;
	}

	if (newX == point.x && newY == point.y) {
		return false;
	}

	CPoint screen (newX, newY);
	ClientToScreen (&screen);
	::SetCursorPos (screen.x, screen.y);

	point.x = newX;
	point.y = newY;
	m_lastPoint = point;
	return true;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonDown
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnLButtonDown
(
    UINT nFlags,
    CPoint point
)
{
	// The view cube takes the clicks that land on it
	if (!m_bRMouseDown && !m_bMMouseDown && m_pCamera != nullptr &&
		 m_ViewCube.On_LButton_Down (m_hWnd, m_pCamera->Get_Transform (), point))
	{
		SetCapture ();
		return ;
	}

	// Capture all mouse messages
	SetCapture ();
	Clip_Cursor_To_View ();

	// Mouse button is down
	m_bMouseDown = TRUE;
	m_lastPoint = point;

	if (m_bRMouseDown) {
		::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_GRAB)));
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("grab.tga");
	} else {
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("orbit.tga");
	}

	CView::OnLButtonDown (nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonUp
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnLButtonUp
(
    UINT nFlags,
    CPoint point
)
{
	if (m_ViewCube.On_LButton_Up ())
	{
		if (!m_bRMouseDown && !m_bMMouseDown)
		{
			ReleaseCapture ();
		}
		return ;
	}

    // Mouse button is up
    m_bMouseDown = FALSE;

    if (!m_bRMouseDown && !m_bMMouseDown)
    {
        ::ClipCursor (nullptr);
        ReleaseCapture ();
    }

    if (m_bRMouseDown == TRUE)
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_ZOOM)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("zoom.tga");
    }
    else
    {
        ::SetCursor (::LoadCursor (nullptr, MAKEINTRESOURCE (IDC_ARROW)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("cursor.tga");
    }

	// Allow the base class to process this message
    CView::OnLButtonUp (nFlags, point);
}

float minZoomAdjust = 0.0F;
Vector3 sphereCenter;
Quaternion rotation;


////////////////////////////////////////////////////////////////////////////
//
//  Orbit_Camera
//
//  3ds Max-style orbit: Euler deltas scaled to viewport size, with pitch around
//  the camera's local X axis and yaw around the world Z axis, both applied about
//  the orbit center (sphereCenter). Constant sensitivity, no trackball edge slow-down.
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Orbit_Camera (int deltaX, int deltaY, CW3DViewDoc *doc)
{
	// Scale deltas to viewport size: full width sweep = 360° yaw, full height = 180° pitch.
	// This keeps sensitivity constant regardless of viewport size and matches Max's feel.
	RECT rect;
	GetClientRect (&rect);
	const float viewW = float(rect.right  > 1 ? rect.right  : 1);
	const float viewH = float(rect.bottom > 1 ? rect.bottom : 1);

	const float yawRad   = DEG_TO_RADF(  (float)deltaX / viewW * 360.0f );
	float pitchRad = DEG_TO_RADF( -(float)deltaY / viewH * 180.0f );

	// Invert Movement (Y): flip the vertical drag-to-orbit direction.
	if (m_InvertCameraY) {
		pitchRad = -pitchRad;
	}

	Matrix3D transform = m_pCamera->Get_Transform ();

	// Pitch: Rotate_X is a local (camera-space) post-multiply, correct for tilt up/down.
	// It pivots about the camera-space image of the orbit center, so bracket it with a
	// local translate to/from that point.
	if (m_allowedCameraRotation == FreeRotation || m_allowedCameraRotation == OnlyRotateX)
	{
		Matrix3D inverseMatrix;
		transform.Get_Orthogonal_Inverse (inverseMatrix);
		Vector3 to_object;
		inverseMatrix.mulVector3 (sphereCenter, to_object);

		transform.Translate (to_object);
		transform.Rotate_X (pitchRad);
		transform.Translate (-to_object);
	}

	// Yaw: rotate about world Z through the orbit center (sphereCenter), so the camera
	// swings around the object without rolling. This must be a WORLD-space pivot:
	// M' = T(c) * Rz * T(-c) * M. Doing Pre_Rotate_Z alone pivots about the world
	// ORIGIN, which slides the object across the viewport when it isn't at (0,0,0).
	if (m_allowedCameraRotation == FreeRotation || m_allowedCameraRotation == OnlyRotateY)
	{
		transform.Set_Translation (transform.Get_Translation () - sphereCenter);
		transform.Pre_Rotate_Z (yawRad);
		transform.Set_Translation (transform.Get_Translation () + sphereCenter);
	}
	else if (m_allowedCameraRotation == OnlyRotateZ)
	{
		transform.Rotate_Z (yawRad);
	}

	// Keep the quaternion accumulator in sync so pan still works correctly.
	rotation = Build_Quaternion (transform);

	m_pCamera->Set_Transform (transform);
	doc->GetBackObjectCamera ()->Set_Transform (transform);
	doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));
}


////////////////////////////////////////////////////////////////////////////
//
//  Apply_Camera_Transform
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Apply_Camera_Transform (const Matrix3D &transform, CW3DViewDoc *doc)
{
	// Pan reads the camera basis from this accumulator
	rotation = Build_Quaternion (transform);

	m_pCamera->Set_Transform (transform);
	doc->GetBackObjectCamera ()->Set_Transform (transform);
	doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));
}


////////////////////////////////////////////////////////////////////////////
//
//  Update_View_Cube_Snap
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Update_View_Cube_Snap (CW3DViewDoc *doc)
{
	Matrix3D transform;
	if (m_pCamera != nullptr && m_ViewCube.Update_Snap (sphereCenter, m_pCamera->Get_Transform (), transform))
	{
		Apply_Camera_Transform (transform, doc);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Snap_Camera_To_Cube_Region
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Snap_Camera_To_Cube_Region (const Vector3 &region, bool fit)
{
	if (m_pCamera == nullptr)
	{
		return;
	}

	Vector3 center = sphereCenter;
	float distance = (m_pCamera->Get_Position () - center).Length ();
	if (fit)
	{
		// The bounding sphere just touches the narrower side of the view
		const float fov = min (m_pCamera->Get_Horizontal_FOV (), m_pCamera->Get_Vertical_FOV ());
		center = m_ViewedSphere.Center;
		distance = max (1.0F, m_ViewedSphere.Radius / (float)sin (fov * 0.5F));
	}

	Set_Camera_Transform (ViewCubeClass::Get_Snap_Transform (region, center, distance));
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Camera_Transform
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_Camera_Transform (const Matrix3D &transform)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument ();
	if (m_pCamera != nullptr && doc != nullptr)
	{
		Apply_Camera_Transform (transform, doc);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMouseMove
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnMouseMove
(
    UINT nFlags,
    CPoint point
)
{
	int iDeltaX = m_lastPoint.x-point.x;
	int iDeltaY = m_lastPoint.y-point.y;

	// Get the document to display
	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

	// A drag that began on the view cube orbits the camera
	int cubeDeltaX = 0;
	int cubeDeltaY = 0;
	if (m_ViewCube.On_Mouse_Move (point, cubeDeltaX, cubeDeltaY))
	{
		if ((cubeDeltaX != 0 || cubeDeltaY != 0) && m_bInitialized && doc->GetScene ())
		{
			Orbit_Camera (cubeDeltaX, cubeDeltaY, doc);
		}
		return ;
	}

	if (!(nFlags & MK_CONTROL) && m_bLightMeshInScene) {
		m_pLightMesh->Remove ();
		m_bLightMeshInScene = false;
	} else if ((nFlags & MK_CONTROL) && (m_bLightMeshInScene == false)) {
		m_pLightMesh->Add (doc->GetScene ());
		m_bLightMeshInScene = true;
	}

	// Middle mouse + Alt: orbit (3ds Max style). Use the Alt state latched at
	// MMB-down — querying GetKeyState(VK_MENU) here is unreliable because Windows
	// hands keyboard focus to the menu bar when Alt is pressed, so the modifier
	// can read as released mid-drag and the branch falls through to pan.
	if (m_bMMouseDown && m_AltOnMDown)
	{
		if (m_bInitialized && doc->GetScene ())
		{
			RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
			if (pCRenderObj)
			{
				Orbit_Camera (iDeltaX, iDeltaY, doc);
			}
		}

		m_lastPoint = point;
		Wrap_Cursor_In_View (point);
	}
	// Middle mouse only: pan (3ds Max style)
	else if (m_bMMouseDown)
	{
		Matrix3D transform = m_pCamera->Get_Transform ();

		RECT rect;
		GetClientRect (&rect);

		float midPointX = float(rect.right >> 1);
		float midPointY = float(rect.bottom >> 1);

		float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
		float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

		float pointX = ((float)point.x - midPointX) / midPointX;
		float pointY = (midPointY - (float)point.y) / midPointY;

		Vector3 cameraPan = Vector3(-1.00F*m_CameraDistance*(pointX - lastPointX), -1.00F*m_CameraDistance*(pointY - lastPointY), 0.00F);

		transform.Translate (cameraPan);

		Matrix3x3 view = Build_Matrix3 (rotation);
		Vector3 move = view * cameraPan;
		sphereCenter += move;

		m_pCamera->Set_Transform (transform);

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if (m_bMouseDown && m_bRMouseDown)
	{
		// Get the transformation matrix for the camera and its inverse
		Matrix3D transform = m_pCamera->Get_Transform ();

		RECT rect;
		GetClientRect (&rect);

		float midPointX = float(rect.right >> 1);
		float midPointY = float(rect.bottom >> 1);

		float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
		float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

		float pointX = ((float)point.x - midPointX) / midPointX;
		float pointY = (midPointY - (float)point.y) / midPointY;


		Vector3 cameraPan = Vector3(-1.00F*m_CameraDistance*(pointX - lastPointX), -1.00F*m_CameraDistance*(pointY - lastPointY), 0.00F);

		transform.Translate (cameraPan);

		Matrix3x3 view = Build_Matrix3 (rotation);
		Vector3 move = view * cameraPan;
		sphereCenter += move;

		// Move the camera back to get a good view of the object
		m_pCamera->Set_Transform (transform);

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if ((nFlags & MK_CONTROL) && m_bMouseDown)
	{
		LightClass *pSceneLight = doc->GetSceneLight ();
		if ((pSceneLight != nullptr) && (m_pLightMesh != nullptr))
		{
			RECT rect;
			GetClientRect (&rect);

			Vector3 point_in_view;
			Vector3 lastpoint_in_view;

			float midPointX = float(rect.right >> 1);
			float midPointY = float(rect.bottom >> 1);

			float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
			float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

			float pointX = ((float)point.x - midPointX) / midPointX;
			float pointY = (midPointY - (float)point.y) / midPointY;

			Quaternion mouse_motion = Inverse(::Trackball(lastPointX, lastPointY, pointX, pointY, 0.8F));
			Quaternion light_orientation;
			Quaternion camera = Build_Quaternion(m_pCamera->Get_Transform());
			Quaternion cur_light = Build_Quaternion(pSceneLight->Get_Transform());

			light_orientation = camera;
			light_orientation = light_orientation * mouse_motion;
			light_orientation = light_orientation * Inverse(camera);
			light_orientation = light_orientation * cur_light;
			light_orientation.Normalize();

			Vector3 to_center;
			Matrix3D matrix = pSceneLight->Get_Transform();
			Matrix3D::Inverse_Transform_Vector(matrix,sphereCenter,&to_center);

			Matrix3D light_tm(light_orientation, sphereCenter);
			light_tm.Translate(-to_center);

			m_pLightMesh->Set_Transform(light_tm);
			pSceneLight->Set_Transform(light_tm);
		}

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if ((nFlags & MK_CONTROL) && m_bRMouseDown)
	{
		// Get the currently displayed object
		CW3DViewDoc *doc= (CW3DViewDoc *)GetDocument();
		LightClass *pscene_light = doc->GetSceneLight ();
		RenderObjClass *prender_obj = doc->GetDisplayedObject ();
		if ((pscene_light != nullptr) && (prender_obj != nullptr)) {

			// Calculate a light adjustment factor
			CRect rect;
			GetClientRect (&rect);
			float deltay = (float(iDeltaY))/(float(rect.bottom - rect.top));
			float adjustment = deltay * (m_ViewedSphere.Radius * 3.0F);

			// Determine the light's new position based on this factor
			Matrix3D transform = pscene_light->Get_Transform ();
			transform.Translate (Vector3 (0, 0, adjustment));

			// Determine what the distance from the light to the object
			// would be with this new position
			Vector3 light_pos = transform.Get_Translation ();
			Vector3 obj_pos = prender_obj->Get_Position ();
			float distance = (light_pos - obj_pos).Length ();

			// If the new position is acceptable, move the light
			if (distance > m_ViewedSphere.Radius) {
				m_pLightMesh->Set_Transform (transform);
				pscene_light->Set_Transform (transform);
			}
		}

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if (m_bMouseDown)
	{
		// Are we in a valid state?
		if (m_bInitialized && doc->GetScene ())
		{
			RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
			if (pCRenderObj)
			{
				Orbit_Camera (iDeltaX, iDeltaY, doc);
			}
		}

		m_lastPoint = point;
		Wrap_Cursor_In_View (point);
	}
	else if (m_bRMouseDown)
	{
		m_lastPoint = point;

		// Get the transformation matrix for the camera and its inverse
		Matrix3D transform = m_pCamera->Get_Transform ();

		Vector3 distanceVectorZ = transform.Get_Z_Vector ();
		if (iDeltaY != 0)
		{

			// Get the bouding rectangle of the main view
			CRect rect;
			GetClientRect (&rect);

			float deltay = (float(iDeltaY))/(float(rect.bottom - rect.top));
			float adjustment = deltay * m_CameraDistance * 3.0F;

			if ((adjustment < minZoomAdjust) && (adjustment >= 0.00F))
			{
				 adjustment = minZoomAdjust;
			}

			if ((adjustment > -minZoomAdjust) && (adjustment <= 0.00F))
			{
				 adjustment = -minZoomAdjust;
			}

			if ((m_CameraDistance + adjustment) > 0.00F)
			{
				m_CameraDistance += adjustment;
				transform.Translate (Vector3 (0.0F, 0.0F, adjustment));

				// Move the camera back to get a good view of the object
				m_pCamera->Set_Transform (transform);

				// Get the main window of our app
				CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
				if (pCMainWnd != nullptr)
				{
					// Ensure the background camera matches the main camera
					CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
					doc->GetBackObjectCamera ()->Set_Transform (transform);
					doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

					// Update the current object if necessary
					RenderObjClass *prender_obj = doc->GetDisplayedObject ();
					if (prender_obj != nullptr) {

						// Ensure the status bar is updated with the correct poly count
						pCMainWnd->UpdatePolygonCount (prender_obj->Get_Num_Polys (), ViewportStats::Count_Vertices (prender_obj), prender_obj->Get_Num_Polys ());
					}

					// Ensure the status bar is updated with the correct camera distance
					pCMainWnd->UpdateCameraDistance (m_CameraDistance);
				}

			}
		}

		m_lastPoint = point;
	}

	// Allow the base class to process this message
	CView::OnMouseMove (nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Emitter
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Emitter (ParticleEmitterClass &emitter)
{
	// Get some of the emitter settings
	Vector3 velocity = emitter.Get_Start_Velocity ();
	const Vector3 &acceleration = emitter.Get_Acceleration ();
	float lifetime = emitter.Get_Lifetime ();

	// If the velocity is 0, then use the randomizer as the default velocity
	bool use_vel_rand = false;
	if ((velocity.X == 0) && (velocity.Y == 0) && (velocity.Z == 0)) {
		//velocity.Set (emitter.Get_Velocity_Random (), emitter.Get_Velocity_Random (), emitter.Get_Velocity_Random ());
		//use_vel_rand = true;
	}

	// Determine what the max extent covered by a particle will be.
	Vector3 distance = (velocity * lifetime) + ((acceleration * (lifetime * lifetime)) / 2.0F);

	// Do we need to take into account acceleration?
	Vector3 distance_maxima (0, 0, 0);
	if ((acceleration.X != 0) || (acceleration.Y != 0) || (acceleration.Z != 0)) {

		// Determine at what time (for each x,y,z) a maxima will occur.
		Vector3 time_max (0, 0, 0);
		time_max.X = (acceleration.X != 0) ? ((-velocity.X) / acceleration.X) : 0.00F;
		time_max.Y = (acceleration.Y != 0) ? ((-velocity.Y) / acceleration.Y) : 0.00F;
		time_max.Z = (acceleration.Z != 0) ? ((-velocity.Z) / acceleration.Z) : 0.00F;

		// Is there a maxima for the X direction?
		if ((time_max.X >= 0.0F) && (time_max.X < lifetime)) {
			distance_maxima.X = (velocity.X * time_max.X) + ((acceleration.X * (time_max.X * time_max.X)) / 2.0F);
			distance_maxima.X = fabs (distance_maxima.X);
		}

		// Is there a maxima for the Y direction?
		if ((time_max.Y >= 0.0F) && (time_max.Y < lifetime)) {
			distance_maxima.Y = (velocity.Y * time_max.Y) + ((acceleration.Y * (time_max.Y * time_max.Y)) / 2.0F);
			distance_maxima.Y = fabs (distance_maxima.Y);
		}

		// Is there a maxima for the Z direction?
		if ((time_max.Z >= 0.0F) && (time_max.Z < lifetime)) {
			distance_maxima.Z = (velocity.Z * time_max.Z) + ((acceleration.Z * (time_max.Z * time_max.Z)) / 2.0F);
			distance_maxima.Z = fabs (distance_maxima.Z);
		}
	}

	distance.X = fabs (distance.X);
	distance.Y = fabs (distance.Y);
	distance.Z = fabs (distance.Z);

	// Determine what the maximum distance convered in a single direction is
	float max_dist = max (distance.X, distance.Y);
	max_dist = max (max_dist, distance.Z);
	max_dist = max (max_dist, distance_maxima.X);
	max_dist = max (max_dist, distance_maxima.Y);
	max_dist = max (max_dist, distance_maxima.Z);

	Vector3 center = distance / 2.00F;
	center.X = max (center.X, distance_maxima.X / 2.00F);
	center.Y = max (center.Y, distance_maxima.Y / 2.00F);
	center.Z = max (center.Z, distance_maxima.Z / 2.00F);

	if (use_vel_rand) {
		center.Set (0, 0, 0);
	}

	// Build a logical sphere from the emitters settings
	// that should provide a good viewing distance for the emitter.
	SphereClass sphere;
	sphere.Center = center;
	sphere.Radius = max (emitter.Get_Particle_Size () * 5, (max_dist * 3.0F) / 5.0F);

	// View this sphere
	Reset_Camera_To_Display_Sphere (sphere);
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Sphere
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Sphere (SphereClass &sphere)
{
	// Calculate a default camera distance to view this sphere
	m_CameraDistance = sphere.Radius * 3.00F;
	m_CameraDistance = (m_CameraDistance < 1.0F) ? 1.0F : m_CameraDistance;

	// Calculate a transform that is the appropriate distance
	// from the sphere center and is looking at the center
	Matrix3D transform (1);
	transform.Look_At (sphere.Center + Vector3 (m_CameraDistance, 0, 0), sphere.Center, 0);

	// Record some variables for later use
	sphereCenter	= sphere.Center;
	m_ObjectCenter	= sphereCenter;
	minZoomAdjust	= m_CameraDistance / 190.0F;
	rotation			= Build_Quaternion (transform);

	// Move the camera back to get a good view of the object
	m_pCamera->Set_Transform (transform);

	// Make the same adjustment for the scene light
	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
	LightClass *pSceneLight = doc->GetSceneLight ();
	if ((m_pLightMesh != nullptr) && (pSceneLight != nullptr)) {

		// Reposition the light and its 'mesh' as appropriate
		transform.Make_Identity ();
		transform.Set_Translation (sphereCenter);
		transform.Translate (0, 0, 0.7F * m_CameraDistance);
		pSceneLight->Set_Transform (transform);
		m_pLightMesh->Set_Transform (transform);

		// Scale the light's mesh appropriately
		static float last_scale = 1.0F;
		m_pLightMesh->Scale (m_CameraDistance / (14 * last_scale));

		last_scale = m_CameraDistance / 14;
	}

	float max_dist = m_CameraDistance * 60.0F;
	float min_dist = max (0.2F, minZoomAdjust / 2);

	// Set the clipping planes so objects are clipped correctly
	if (doc->Are_Clip_Planes_Manual () == false) {
		m_pCamera->Set_Clip_Planes (min_dist, max_dist);

		// Adjust the fog near clipping plane to the new value, but
		// leave the far clip plane alone (since it is scene dependent
		// not camera dependent).
		float fog_near, fog_far;
		doc->GetScene()->Get_Fog_Range(&fog_near, &fog_far);
		doc->GetScene()->Set_Fog_Range(min_dist, fog_far);
		doc->GetScene()->Recalculate_Fog_Planes();
	}

	// Reset the background camera to match the main camera
	doc->GetBackObjectCamera ()->Set_Transform (transform);
	doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

	// Update the camera distance in the status bar
	CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
	if (pCMainWnd != nullptr) {
		pCMainWnd->UpdateCameraDistance (m_CameraDistance);
		pCMainWnd->UpdateFrameCount (0, 0, 0);
	}

	// Record the sphere we are viewing for later
	m_ViewedSphere = sphere;
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Object
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Object (RenderObjClass &render_object)
{
	// Reset the camera to get a good look at this object's bounding sphere
	SphereClass sp = render_object.Get_Bounding_Sphere ();
	Reset_Camera_To_Display_Sphere (sp);

	// Should we update the camera's position as well?
	int index = render_object.Get_Bone_Index ("CAMERA");
	if (index > 0) {

		// Convert the bone's transform into a camera transform
		Matrix3D	transform = render_object.Get_Bone_Transform (index);
		if (m_CameraBonePosX) {
			Matrix3D tmp = transform;
			Matrix3D cam_transform (Vector3 (0, -1, 0), Vector3 (0, 0, 1), Vector3 (-1, 0, 0), Vector3 (0, 0, 0));
#ifdef ALLOW_TEMPORARIES
			transform = tmp * cam_transform;
#else
			transform.mul(tmp, cam_transform);
#endif
		}

		// Pass the new transform onto the camera
		CameraClass *camera = GetCamera ();
		camera->Set_Transform (transform);
	}

	// Update the polygon count in the main window
	CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
	if (pCMainWnd != nullptr) {
		pCMainWnd->UpdatePolygonCount (render_object.Get_Num_Polys (), ViewportStats::Count_Vertices (&render_object), render_object.Get_Num_Polys ());
	}

	// Load the settings in the default.dat if its in the local directory.
	Load_Default_Dat ();
}


////////////////////////////////////////////////////////////////////////////
//
//  Load_Default_Dat
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Load_Default_Dat ()
{
	// Get the directory where this executable was run from
	TCHAR filename[MAX_PATH];
	::GetModuleFileName (nullptr, filename, sizeof (filename));

	// Strip the filename from the path
	LPTSTR ppath = ::strrchr (filename, '\\');
	if (ppath != nullptr) {
		ppath[0] = 0;
	}

	// Concat the default.dat filename onto the path
	strlcat(filename, "\\default.dat", ARRAY_SIZE(filename));

	// Does the file exist in the directory?
	if (::GetFileAttributes (filename) != 0xFFFFFFFF) {

		// Ask the document to load the settings from this data file
		CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetDocument ();
		if (pCDoc != nullptr) {
			pCDoc->LoadSettings (filename);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnRButtonUp
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnRButtonUp
(
    UINT nFlags,
    CPoint point
)
{
	// Mouse button is up
	m_bRMouseDown = FALSE;

	if (m_bMouseDown) {
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("orbit.tga");
	} else {
		::SetCursor (::LoadCursor (nullptr, MAKEINTRESOURCE (IDC_ARROW)));
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("cursor.tga");
		if (!m_bMMouseDown)
		{
			::ClipCursor (nullptr);
			ReleaseCapture ();
		}
	}

	// Allow the base class to process this message
	CView::OnRButtonUp(nFlags, point);
}

////////////////////////////////////////////////////////////////////////////
//
//  OnRButtonDown
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnRButtonDown
(
    UINT nFlags,
    CPoint point
)
{
    // Capture all mouse messages
    SetCapture ();
    Clip_Cursor_To_View ();

    // Mouse button is down
    m_bRMouseDown = TRUE;
    m_lastPoint = point;

    if (m_bMouseDown)
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_GRAB)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("grab.tga");
    }
    else
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_ZOOM)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("zoom.tga");
    }

	// Allow the base class to process this message
    CView::OnRButtonDown(nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMButtonDown
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnMButtonDown
(
    UINT nFlags,
    CPoint point
)
{
    SetCapture ();
    Clip_Cursor_To_View ();
    m_bMMouseDown = TRUE;
    m_lastPoint = point;
    m_mButtonDownPoint = point;
    m_AltOnMDown = (::GetKeyState (VK_MENU) & 0x8000) != 0;

    ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_GRAB)));
    if (m_AltOnMDown) {
        ((CW3DViewDoc *)GetDocument())->Set_Cursor ("orbit.tga");
    } else {
        ((CW3DViewDoc *)GetDocument())->Set_Cursor ("grab.tga");
    }

    CView::OnMButtonDown (nFlags, point);
    return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMButtonUp
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnMButtonUp
(
    UINT nFlags,
    CPoint point
)
{
    m_bMMouseDown = FALSE;
    m_AltOnMDown = false;

    // If the mouse barely moved with Shift held, treat it as a click and reset the camera
    int dx = point.x - m_mButtonDownPoint.x;
    int dy = point.y - m_mButtonDownPoint.y;
    if ((dx * dx + dy * dy) < 16 && (nFlags & MK_SHIFT))
    {
        CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
        RenderObjClass *prender_obj = doc->GetDisplayedObject ();
        if (prender_obj != nullptr) {
            Reset_Camera_To_Display_Object (*prender_obj);
        } else {
            Reset_Camera_To_Display_Sphere (m_ViewedSphere);
        }
    }

    if (!m_bMouseDown && !m_bRMouseDown) {
        ::SetCursor (::LoadCursor (nullptr, MAKEINTRESOURCE (IDC_ARROW)));
        ((CW3DViewDoc *)GetDocument())->Set_Cursor ("cursor.tga");
        ::ClipCursor (nullptr);
        ReleaseCapture ();
    }

    CView::OnMButtonUp (nFlags, point);
    return ;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnMouseWheel
//
////////////////////////////////////////////////////////////////////////////
BOOL
CGraphicView::OnMouseWheel
(
    UINT nFlags,
    short zDelta,
    CPoint point
)
{
    if (m_bInitialized && m_pCamera != nullptr)
    {
        Matrix3D transform = m_pCamera->Get_Transform ();

        // Positive zDelta = scroll up = zoom in (move toward object).
        float deltay = -(float)zDelta / (float)WHEEL_DELTA * 0.1F;
        float adjustment = deltay * m_CameraDistance * 3.0F;

        if ((adjustment < minZoomAdjust) && (adjustment >= 0.00F))
            adjustment = minZoomAdjust;
        if ((adjustment > -minZoomAdjust) && (adjustment <= 0.00F))
            adjustment = -minZoomAdjust;

        if ((m_CameraDistance + adjustment) > 0.00F)
        {
            m_CameraDistance += adjustment;
            transform.Translate (Vector3 (0.0F, 0.0F, adjustment));
            m_pCamera->Set_Transform (transform);

            CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
            doc->GetBackObjectCamera ()->Set_Transform (transform);
            doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

            CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
            if (pCMainWnd != nullptr) {
                RenderObjClass *prender_obj = doc->GetDisplayedObject ();
                if (prender_obj != nullptr)
                    pCMainWnd->UpdatePolygonCount (prender_obj->Get_Num_Polys (), ViewportStats::Count_Vertices (prender_obj), prender_obj->Get_Num_Polys ());
                pCMainWnd->UpdateCameraDistance (m_CameraDistance);
            }
        }
    }

    return CView::OnMouseWheel (nFlags, zDelta, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  SetAnimationState
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetAnimationState (ANIMATION_STATE animationState)
{
    // Has the state changed?
    if (m_animationState != animationState)
    {
        switch (animationState)
        {
            // We want to stop the animation
            case AnimStopped:
            {
                // Get the document so we can get our current object
                CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
                ASSERT_VALID (doc);

                // Get the currently displayed object
                RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
                if (pCRenderObj)
                {
                    // Reset the animation to frame 0

                    if (doc->GetCurrentAnimation()) {
                    	pCRenderObj->Set_Animation (doc->GetCurrentAnimation (), 0);
                    }
                }

                // Reset the animation to frame 0
                doc->ResetAnimation ();
            }
            break;

            case AnimPlaying:
            {
					CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument ();
					doc->Play_Animation_Sound ();

                // Reset the frame timer
					 m_dwLastFrameUpdate = timeGetTime ();
            }
            break;
        }

        // Save the new state
        m_animationState = animationState;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  SetCameraPos
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetCameraPos (CAMERA_POS cameraPos)
{
    // Get the document so we can get our current object
    CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
    ASSERT_VALID (doc);

    // Get the currently displayed object
    RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
    if (pCRenderObj)
    {
        SphereClass sphere = m_ViewedSphere;

        m_CameraDistance = sphere.Radius * 3.00F;
        m_CameraDistance = (m_CameraDistance < 1.0F) ? 1.0F : m_CameraDistance;
        m_CameraDistance = (m_CameraDistance > 400.0F) ? 400.0F : m_CameraDistance;

        Matrix3D transform (1);

        switch (cameraPos)
        {
            case CameraFront:
            {
                transform.Look_At (sphere.Center + Vector3 (m_CameraDistance, 0.00F, 0.00F), sphere.Center, 0);
            }
            break;

            case CameraBack:
            {
                transform.Look_At (sphere.Center + Vector3 (-m_CameraDistance, 0.00F, 0.00F), sphere.Center, 0);
            }
            break;

            case CameraLeft:
            {
                transform.Look_At (sphere.Center + Vector3 (0.00F, -m_CameraDistance, 0.00F), sphere.Center, 0);
            }
            break;

            case CameraRight:
            {
                transform.Look_At (sphere.Center + Vector3 (0.00F, m_CameraDistance, 0.00F), sphere.Center, 0);
            }
            break;

            case CameraTop:
            {
                transform.Look_At (sphere.Center + Vector3 (0.00F, 0.00F, m_CameraDistance), sphere.Center, 3.1415926535F);
            }
            break;

            case CameraBottom:
            {
                transform.Look_At (sphere.Center + Vector3 (0.00F, 0.00F, -m_CameraDistance), sphere.Center, 3.1415926535F);
            }
            break;
        }

	    // Move the camera back to get a good view of the object
        m_pCamera->Set_Transform (transform);

        // Get the main window of our app
        CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
        if (pCMainWnd != nullptr)
        {
            CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

            doc->GetBackObjectCamera ()->Set_Transform (transform);
            doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

            RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
            if (pCRenderObj)
            {
                pCMainWnd->UpdatePolygonCount (pCRenderObj->Get_Num_Polys (), ViewportStats::Count_Vertices (pCRenderObj), pCRenderObj->Get_Num_Polys ());
            }

            pCMainWnd->UpdateCameraDistance(m_CameraDistance);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  RotateObject
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::RotateObject (OBJECT_ROTATION rotation)
{
    // Is this rotation different?
    if (m_objectRotation != rotation)
    {
        // Save the rotation state
        m_objectRotation = rotation;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  SetAllowedCameraRotation
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetAllowedCameraRotation (CAMERA_ROTATION cameraRotation)
{
    // Store this for later reference
    m_allowedCameraRotation = cameraRotation;
}


////////////////////////////////////////////////////////////////////////////
//
//  ResetObject
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::ResetObject ()
{
    // Get the current document
    CW3DViewDoc *doc = ::GetCurrentDocument ();

    ASSERT (doc);
    if (doc)
    {
        // Get the currently displayed object
        RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
        if (pCRenderObj)
        {
            // Reset the rotation of the object
            pCRenderObj->Set_Transform (Matrix3D(true));
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnGetMinMaxInfo
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnGetMinMaxInfo (MINMAXINFO FAR* lpMMI)
{
	CView::OnGetMinMaxInfo (lpMMI);
}


////////////////////////////////////////////////////////////////////////////
//
//  Rotate_Object
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Rotate_Object ()
{
	// Get the document to display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	// Get the currently displayed object
	RenderObjClass *prender_obj = doc->GetDisplayedObject ();
	if (prender_obj != nullptr)
	{
		// Get the current transform for the object
		Matrix3D transform = prender_obj->Get_Transform ();

		if ((m_objectRotation & RotateX) == RotateX) {
			transform.Rotate_X (0.05F);
		} else if ((m_objectRotation & RotateXBack) == RotateXBack) {
			transform.Rotate_X (-0.05F);
		}

		if ((m_objectRotation & RotateY) == RotateY) {
			transform.Rotate_Y (-0.05F);
		} else if ((m_objectRotation & RotateYBack) == RotateYBack) {
			transform.Rotate_Y (0.05F);
		}

		if ((m_objectRotation & RotateZ) == RotateZ) {
			transform.Rotate_Z (0.05F);
		} else if ((m_objectRotation & RotateZBack) == RotateZBack) {
			transform.Rotate_Z (-0.05F);
		}

		if (!transform.Is_Orthogonal()) {
			transform.Re_Orthogonalize();
		}

		// Set the new transform for the object
		prender_obj->Set_Transform (transform);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Rotate_Light
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Rotate_Light ()
{
	// Get the document to display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	// Get the currently displayed object
	LightClass *pscene_light = doc->GetSceneLight ();
	RenderObjClass *prender_obj = doc->GetDisplayedObject ();
	if ((pscene_light != nullptr) && (prender_obj != nullptr)) {
		Matrix3D rotation_matrix (1);

		// Build a rotation matrix that contains the x,y,z
		// rotations we want to apply to the light
		if ((m_LightRotation & RotateX) == RotateX) {
			rotation_matrix.Rotate_X (0.05F);
		} else if ((m_LightRotation & RotateXBack) == RotateXBack) {
			rotation_matrix.Rotate_X (-0.05F);
		}

		if ((m_LightRotation & RotateY) == RotateY) {
			rotation_matrix.Rotate_Y (-0.05F);
		} else if ((m_LightRotation & RotateYBack) == RotateYBack) {
			rotation_matrix.Rotate_Y (0.05F);
		}

		if ((m_LightRotation & RotateZ) == RotateZ) {
			rotation_matrix.Rotate_Z (0.05F);
		} else if ((m_LightRotation & RotateZBack) == RotateZBack) {
			rotation_matrix.Rotate_Z (-0.05F);
		}

		//
		//	Now, use the rotation matrix to rotate the
		// light 'around' the displayed object (in its coordinate system)
		//
		Matrix3D coord_inv;
		Matrix3D coord_to_obj;
		Matrix3D coord_system = prender_obj->Get_Transform ();
		coord_system.Get_Orthogonal_Inverse (coord_inv);

		Matrix3D transform = pscene_light->Get_Transform ();
		Matrix3D::Multiply (coord_inv, transform, &coord_to_obj);

		Matrix3D::Multiply (coord_system, rotation_matrix, &transform);
		Matrix3D::Multiply (transform, coord_to_obj, &transform);

		// Ensure the matrix hasn't degenerated
		if (!transform.Is_Orthogonal ()) {
			transform.Re_Orthogonalize ();
		}

		// Pass the new transform onto the light
		m_pLightMesh->Set_Transform (transform);
		pscene_light->Set_Transform (transform);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_FOV
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_FOV (double hfov, double vfov, bool force)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	if (force || (doc->Is_FOV_Manual () == false)) {
		m_pCamera->Set_View_Plane (hfov, vfov);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_FOV
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_FOV ()
{
	int cx = 0;
	int cy = 0;

	if (m_iWindowed == 0) {
		cx = g_iWidth;
		cy = g_iHeight;
	} else {
		CRect rect;
		GetClientRect (&rect);
		cx = rect.Width ();
		cy = rect.Height ();
	}

	// update the camera FOV settings
	// take the larger of the two dimensions, give it the
	// full desired FOV, then give the other dimension an
	// FOV proportional to its relative size
	double hfov,vfov;
	if (cy > cx) {

		vfov = (float)DEG_TO_RAD(45.0f);
		hfov = (double)cx / (double)cy * vfov;
	} else  {
		hfov = (float)DEG_TO_RAD(45.0f);
		vfov = (double)cy / (double)cx * hfov;
	}

	// Reset the field of view
	Set_FOV (hfov, vfov);
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Camera_Distance
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_Camera_Distance (float dist)
{
	m_CameraDistance = dist;

	//
	//	Reposition the camera
	//
	Matrix3D new_tm(1);
	new_tm.Look_At (m_ViewedSphere.Center + Vector3 (m_CameraDistance, 0.00F, 0.00F), m_ViewedSphere.Center, 0);
	m_pCamera->Set_Transform (new_tm);

	//
	// Update the status bar
	//
	CMainFrame *main_wnd = (CMainFrame *)::AfxGetMainWnd ();
	if (main_wnd != nullptr) {
		main_wnd->UpdateCameraDistance (m_CameraDistance);
	}
}


