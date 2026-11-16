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

#include "../StdAfx.h"
#include "MaterialViewerFrame.h"

#include "MaterialPanel.h"
#include "MaterialPreviewPane.h"
#include "W3dChunkTree.h"
#include "W3dMaterialParser.h"
#include "W3dMaterialWriter.h"
#include "../GraphicView.h"
#include "../resource.h"
#include "../Utils.h"
#include "../W3DDarkMode.h"
#include "../W3DViewDoc.h"

#include "WW3D2/assetmgr.h"
#include "WW3D2/dx8wrapper.h"
#include "WWLib/ffactory.h"
#include "WW3D2/hanim.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/shader.h"
#if defined(BUILD_WITH_D3D9)
#include "WW3D2/formconv.h"
#else
#include <d3dx8tex.h>
#endif
#include <map>
#include <utility>

CMaterialViewerFrame *CMaterialViewerFrame::_TheInstance = nullptr;

namespace
{
	const int PREVIEW_ID = 1;
	const int PLACEHOLDER_ID = 2;

	// The shader toggle icons sit in the main toolbar strip, after the file buttons
	const int TOOLBAR_ICON_WIDTH = 16;
	const int TOOLBAR_ICON_HEIGHT = 15;
	const int TOOLBAR_STRIP_ICONS = 17;
	const int TOOLBAR_FIRST_SHADER_ICON = 10;
	const UINT TOOLBAR_BUTTONS[] =
	{
		IDM_TOGGLE_ALPHA,
		IDM_SHADER_ADDITIVE,
		IDM_SHADER_ALPHA_TEST,
		IDM_SHADER_ALPHA_BLEND,
		IDM_SHADER_ALPHA_BLEND_TEST,
		IDM_SHADER_DOUBLE_SIDED,
	};
	const int TOOLBAR_BUTTON_COUNT = sizeof(TOOLBAR_BUTTONS) / sizeof(TOOLBAR_BUTTONS[0]);

	// Grey is the strip's transparent colour, as in every MFC toolbar bitmap
	void Load_Toolbar_Strip(CImageList &images, UINT bitmap_id)
	{
		images.Create(TOOLBAR_ICON_WIDTH, TOOLBAR_ICON_HEIGHT, ILC_COLOR24 | ILC_MASK, TOOLBAR_STRIP_ICONS, 0);
		HBITMAP bitmap = ::LoadBitmap(::AfxGetResourceHandle(), MAKEINTRESOURCE(bitmap_id));
		if (bitmap != nullptr) {
			::ImageList_AddMasked(images.GetSafeHandle(), bitmap, RGB(192, 192, 192));
			::DeleteObject(bitmap);
		}
	}

	// Ground-height slider (Qt QSlider): strip width left of the preview, and
	// tick count (position resolution) across the pane's z range.
	const int GROUND_SLIDER_WIDTH = 26;
	const int GROUND_SLIDER_STEPS = 1000;

	// Debounce timer for the live material preview: coalesce a burst of edits
	// (dragging a spin box, typing) into a single in-memory apply.
	const UINT_PTR LIVE_PREVIEW_TIMER = 0xA1;
	const UINT LIVE_PREVIEW_DELAY_MS = 200;

	// Preview pane takes this share of the client width; the panel gets the rest.
	const float PREVIEW_WIDTH_RATIO = 0.55F;

	// Floor for the file-tab strip across the top; the actual height comes from
	// the Qt tab bar's style/font (GetTabBarPreferredHeight).
	const int MIN_TAB_BAR_HEIGHT = 26;

	// Tab-bar interaction deferred out of the Qt signal stack (see the thunks).
	// wparam = tab index, lparam = 0 for select / 1 for close-button.
	const UINT WM_MATVIEWER_TAB_ACTION = WM_USER + 0x41;

	// Longest edge of the texture thumbnails shown in the panel's Textures tab.
	const int TEXTURE_PREVIEW_MAX_DIM = 96;

	// Sub-object name = the part after the last '.'; document mesh names are
	// "Container.Mesh" and the container carries the unit's state suffix
	// (AVCOMMAND / AVCOMMAND_D / ...), so batch edit matches on this tail.
	const char *Bare_Mesh_Name(const std::string &name)
	{
		size_t dot = name.find_last_of('.');
		return (dot == std::string::npos) ? name.c_str() : name.c_str() + dot + 1;
	}

	bool Mesh_Names_Match(const std::string &a, const std::string &b)
	{
		return ::_stricmp(Bare_Mesh_Name(a), Bare_Mesh_Name(b)) == 0;
	}

	// The material writer patches structs IN PLACE (fixed-size arrays); it never
	// adds or removes textures / shaders / vertex materials / passes. So a batch
	// copy is only safe when the two meshes have the SAME structure — identical
	// counts of passes, vertex materials, shaders, and textures. Copying across a
	// structural mismatch would write values the file can't represent, and the
	// writer's post-write validation would (correctly) reject the whole file.
	// State variants of the same model normally match; genuinely different meshes
	// that merely share a sub-name do not, and are skipped rather than corrupted.
	bool Mesh_Structures_Compatible(const W3dMaterialViewer::MeshMaterialData &a,
		const W3dMaterialViewer::MeshMaterialData &b)
	{
		return a.prelit == b.prelit
			&& !a.prelit	// prelit meshes are read-only in the writer
			&& a.passCount == b.passCount
			&& a.vertexMaterials.size() == b.vertexMaterials.size()
			&& a.shaders.size() == b.shaders.size()
			&& a.textures.size() == b.textures.size()
			&& a.passes.size() == b.passes.size();
	}

	// Records a changed field name into `changes` (deduped, since the same edit
	// is diffed once per target file). Null skips reporting.
	void Note_Change(std::vector<std::string> *changes, const char *field)
	{
		if (changes == nullptr) {
			return;
		}
		for (const std::string &existing : *changes) {
			if (existing == field) {
				return;
			}
		}
		changes->push_back(field);
	}

	// Applies ONLY the shader fields that differ between `base` and `edited` onto
	// `dst`. A field the user left alone stays whatever the target file has, so a
	// mesh in another state file keeps its own unrelated shader settings. Records
	// each changed field's friendly name into `changes`. Returns true if any
	// field was changed.
	bool Apply_Shader_Deltas(const W3dMaterialViewer::ShaderData &base,
		const W3dMaterialViewer::ShaderData &edited, W3dMaterialViewer::ShaderData &dst,
		std::vector<std::string> *changes)
	{
		bool changed = false;
		#define DELTA(field, label) if (edited.field != base.field) \
			{ dst.field = edited.field; changed = true; Note_Change(changes, label); }
		DELTA(depthCompare, "Depth Compare") DELTA(depthMask, "Depth Mask")
		DELTA(destBlend, "Dest Blend") DELTA(priGradient, "Primary Gradient")
		DELTA(secGradient, "Secondary Gradient") DELTA(srcBlend, "Src Blend")
		DELTA(texturing, "Texturing") DELTA(detailColorFunc, "Detail Color Func")
		DELTA(detailAlphaFunc, "Detail Alpha Func") DELTA(alphaTest, "Alpha Test")
		DELTA(postDetailColorFunc, "Post-Detail Color Func")
		DELTA(postDetailAlphaFunc, "Post-Detail Alpha Func")
		#undef DELTA
		return changed;
	}

	// Applies only the vertex-material fields the user changed (colours, opacity,
	// shininess, attributes/mapping, mapper args). The material name is never
	// touched.
	bool Apply_VertexMaterial_Deltas(const W3dMaterialViewer::VertexMaterialData &base,
		const W3dMaterialViewer::VertexMaterialData &edited, W3dMaterialViewer::VertexMaterialData &dst,
		std::vector<std::string> *changes)
	{
		bool changed = false;
		#define DELTA(field, label) if (edited.field != base.field) \
			{ dst.field = edited.field; changed = true; Note_Change(changes, label); }
		DELTA(attributes, "Mapping / Attributes") DELTA(shininess, "Shininess")
		DELTA(opacity, "Opacity") DELTA(translucency, "Translucency")
		DELTA(mapperArgs0, "Mapper Args (stage 0)") DELTA(mapperArgs1, "Mapper Args (stage 1)")
		#undef DELTA
		#define DELTA_RGB(field, label) if (::memcmp(edited.field, base.field, sizeof(edited.field)) != 0) \
			{ ::memcpy(dst.field, edited.field, sizeof(dst.field)); changed = true; Note_Change(changes, label); }
		DELTA_RGB(ambient, "Ambient Color") DELTA_RGB(diffuse, "Diffuse Color")
		DELTA_RGB(specular, "Specular Color") DELTA_RGB(emissive, "Emissive Color")
		#undef DELTA_RGB
		return changed;
	}

	// Applies only the texture SETTINGS the user changed (attributes incl. clamp
	// bits, anim type, frame count/rate). The texture *name* (which file the mesh
	// points at) is never touched, so each state variant keeps its own textures.
	bool Apply_Texture_Deltas(const W3dMaterialViewer::TextureData &base,
		const W3dMaterialViewer::TextureData &edited, W3dMaterialViewer::TextureData &dst,
		std::vector<std::string> *changes)
	{
		bool changed = false;
		#define DELTA(field, label) if (edited.field != base.field) \
			{ dst.field = edited.field; changed = true; Note_Change(changes, label); }
		DELTA(attributes, "Texture Settings (clamp / hint / LOD)") DELTA(animType, "Anim Type")
		DELTA(frameCount, "Frame Count") DELTA(frameRate, "Frame Rate")
		#undef DELTA
		// Ensure a texture-info chunk exists to hold any changed settings.
		if (changed && edited.hasInfo) {
			dst.hasInfo = true;
		}
		return changed;
	}

	// Applies the user's edits (baseline vs edited) onto one structurally-matching
	// target mesh, field by field, leaving unchanged fields as the target had
	// them, and records the changed field names in `changes`. Returns true if
	// anything was applied. Requires all three meshes to be structurally
	// compatible (same pass/material/shader/texture counts) — the baseline and
	// edited always are (same file); the target is checked by the caller via
	// Mesh_Structures_Compatible.
	bool Apply_Mesh_Deltas(const W3dMaterialViewer::MeshMaterialData &base,
		const W3dMaterialViewer::MeshMaterialData &edited,
		W3dMaterialViewer::MeshMaterialData &dst,
		std::vector<std::string> *changes)
	{
		bool changed = false;
		for (size_t i = 0; i < edited.shaders.size() && i < base.shaders.size()
				&& i < dst.shaders.size(); i++) {
			changed |= Apply_Shader_Deltas(base.shaders[i], edited.shaders[i], dst.shaders[i], changes);
		}
		for (size_t i = 0; i < edited.vertexMaterials.size() && i < base.vertexMaterials.size()
				&& i < dst.vertexMaterials.size(); i++) {
			changed |= Apply_VertexMaterial_Deltas(base.vertexMaterials[i],
				edited.vertexMaterials[i], dst.vertexMaterials[i], changes);
		}
		for (size_t i = 0; i < edited.textures.size() && i < base.textures.size()
				&& i < dst.textures.size(); i++) {
			changed |= Apply_Texture_Deltas(base.textures[i], edited.textures[i], dst.textures[i], changes);
		}
		return changed;
	}

	// Decodes a small BGRA thumbnail for the texture using the engine's own
	// loader (_Create_DX8_Surface resolves through the file factory and falls
	// back from .tga to .dds), so the Qt panel never parses image formats.
	void Build_Texture_Preview(W3dMaterialViewer::TextureData &texture)
	{
		if (texture.name.empty()) {
			return;
		}

		IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
		if (device == nullptr) {
			return;
		}

		// Record where the texture was actually found (for the tooltip); mirror
		// the engine's .tga -> .dds fallback. Skip decoding entirely when the
		// file is missing so the panel can say so instead of showing the
		// missing-texture pattern.
		{
			file_auto_ptr file(_TheFileFactory, texture.name.c_str());
			if (file->Is_Available()) {
				texture.resolvedPath = file->File_Name();
			} else {
				std::string dds_name = texture.name;
				size_t ext = dds_name.rfind('.');
				if (ext != std::string::npos) {
					dds_name.replace(ext, std::string::npos, ".dds");
					file_auto_ptr dds_file(_TheFileFactory, dds_name.c_str());
					if (dds_file->Is_Available()) {
						texture.resolvedPath = dds_file->File_Name();
					}
				}
			}
			if (texture.resolvedPath.empty()) {
				return;
			}
		}

		IDirect3DSurface8 *source = DX8Wrapper::_Create_DX8_Surface(texture.name.c_str());
		if (source == nullptr) {
			return;
		}

		D3DSURFACE_DESC desc;
		if (SUCCEEDED(source->GetDesc(&desc)) && desc.Width > 0 && desc.Height > 0) {

			int width = (int)desc.Width;
			int height = (int)desc.Height;
			if (width >= height) {
				height = max(1, height * TEXTURE_PREVIEW_MAX_DIM / width);
				width = TEXTURE_PREVIEW_MAX_DIM;
			} else {
				width = max(1, width * TEXTURE_PREVIEW_MAX_DIM / height);
				height = TEXTURE_PREVIEW_MAX_DIM;
			}

			IDirect3DSurface8 *dest = nullptr;
			if (SUCCEEDED(DX8_CREATE_IMAGE_SURFACE(device, width, height, D3DFMT_A8R8G8B8, &dest))) {
#if defined(BUILD_WITH_D3D9)
				if (SUCCEEDED(::Load_Surface_From_Surface(dest, nullptr, source, nullptr))) {
#else
				if (SUCCEEDED(::D3DXLoadSurfaceFromSurface(dest, nullptr, nullptr,
						source, nullptr, nullptr, D3DX_FILTER_TRIANGLE, 0))) {
#endif
					D3DLOCKED_RECT locked;
					if (SUCCEEDED(dest->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
						texture.previewPixels.resize((size_t)width * height * 4);
						for (int y = 0; y < height; y++) {
							memcpy(&texture.previewPixels[(size_t)y * width * 4],
								(const uint8_t *)locked.pBits + (size_t)y * locked.Pitch,
								(size_t)width * 4);
						}
						dest->UnlockRect();
						texture.previewWidth = width;
						texture.previewHeight = height;
					}
				}
				dest->Release();
			}
		}

		source->Release();
	}

	void Fill_Texture_Previews(W3dMaterialViewer::MaterialDocument &document)
	{
		// Meshes in one file usually share textures; decode each name only once.
		std::map<std::string, const W3dMaterialViewer::TextureData *> cache;

		for (size_t m = 0; m < document.meshes.size(); m++) {
			std::vector<W3dMaterialViewer::TextureData> &textures = document.meshes[m].textures;
			for (size_t t = 0; t < textures.size(); t++) {
				W3dMaterialViewer::TextureData &texture = textures[t];
				std::map<std::string, const W3dMaterialViewer::TextureData *>::const_iterator it = cache.find(texture.name);
				if (it != cache.end()) {
					texture.resolvedPath = it->second->resolvedPath;
					texture.previewWidth = it->second->previewWidth;
					texture.previewHeight = it->second->previewHeight;
					texture.previewPixels = it->second->previewPixels;
				} else {
					Build_Texture_Preview(texture);
					cache[texture.name] = &texture;
				}
			}
		}
	}
}

BEGIN_MESSAGE_MAP(CMaterialViewerFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
	ON_COMMAND(IDM_MATVIEWER_OPEN, OnFileOpen)
	ON_COMMAND(IDM_MATVIEWER_CLOSE, OnFileClose)
	ON_COMMAND(IDM_MATVIEWER_SAVE, OnFileSave)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_SAVE, OnUpdateFileSave)
	ON_COMMAND(IDM_MATVIEWER_BACKUP, OnToggleBackup)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_BACKUP, OnUpdateToggleBackup)
	ON_COMMAND(IDM_MATVIEWER_UNDO, OnEditUndo)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_UNDO, OnUpdateEditUndo)
	ON_COMMAND(IDM_MATVIEWER_REDO, OnEditRedo)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_REDO, OnUpdateEditRedo)
	ON_COMMAND(IDM_MATVIEWER_REVERT, OnEditRevert)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_REVERT, OnUpdateEditRevert)
	ON_COMMAND(IDM_MATVIEWER_BATCH_EDIT, OnBatchEdit)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_BATCH_EDIT, OnUpdateBatchEdit)
	ON_COMMAND(IDM_MATVIEWER_BATCH_SELECT, OnBatchSelect)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_BATCH_SELECT, OnUpdateBatchSelect)
	ON_COMMAND(IDM_MATVIEWER_SHOW_FULL, OnShowFullObject)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_SHOW_FULL, OnUpdateShowFullObject)
	ON_COMMAND(IDM_MATVIEWER_TINT_SELECTION, OnTintSelection)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_TINT_SELECTION, OnUpdateTintSelection)
	ON_COMMAND(IDM_TOGGLE_ALPHA, OnToggleAlpha)
	ON_UPDATE_COMMAND_UI(IDM_TOGGLE_ALPHA, OnUpdateToggleAlpha)
	ON_COMMAND(IDM_SHADER_ADDITIVE, OnShaderAdditive)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ADDITIVE, OnUpdateShaderAdditive)
	ON_COMMAND(IDM_SHADER_ALPHA_TEST, OnShaderAlphaTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_TEST, OnUpdateShaderAlphaTest)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND, OnShaderAlphaBlend)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND, OnUpdateShaderAlphaBlend)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND_TEST, OnShaderAlphaBlendTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND_TEST, OnUpdateShaderAlphaBlendTest)
	ON_COMMAND(IDM_SHADER_DOUBLE_SIDED, OnShaderDoubleSided)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_DOUBLE_SIDED, OnUpdateShaderDoubleSided)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT, OnShaderBackfaceTint)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_BACKFACE_TINT, OnUpdateShaderBackfaceTint)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT_COLOR, OnShaderBackfaceTintColor)
	ON_COMMAND(IDM_MATVIEWER_LIGHT_FREEROAM, OnLightFreeRoam)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_LIGHT_FREEROAM, OnUpdateLightFreeRoam)
	ON_COMMAND(IDM_MATVIEWER_LIGHT_PERFACE, OnLightPerFace)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_LIGHT_PERFACE, OnUpdateLightPerFace)
	ON_COMMAND(IDM_MATVIEWER_GROUND, OnShowGround)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_GROUND, OnUpdateShowGround)
	ON_COMMAND(IDM_MATVIEWER_GROUND_RESET, OnGroundReset)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_GROUND_RESET, OnUpdateGroundReset)
	ON_WM_DROPFILES()
	ON_MESSAGE(WM_MATVIEWER_TAB_ACTION, OnTabAction)
	ON_MESSAGE(WM_W3DVIEW_THEME_CHANGED, OnThemeChanged)
END_MESSAGE_MAP()

// TheSuperHackers @feature W3D Shaders menu (same global ShaderClass filters
// as the main frame's View > W3D Shaders — the toggles affect both viewports
// since the renderer is shared).
void CMaterialViewerFrame::OnToggleAlpha()
{
	bool alpha = !ShaderClass::Is_Alpha_Blending_Forced();
	ShaderClass::Force_Alpha_Blending(alpha);
	::AfxGetApp()->WriteProfileInt("Config", "ForceAlphaBlend", alpha ? 1 : 0);
}

void CMaterialViewerFrame::OnUpdateToggleAlpha(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Is_Alpha_Blending_Forced() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAdditive()
{
	ShaderClass::Set_Filter_Additive(!ShaderClass::Get_Filter_Additive());
}

void CMaterialViewerFrame::OnUpdateShaderAdditive(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Additive() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaTest()
{
	ShaderClass::Set_Filter_Alpha_Test(!ShaderClass::Get_Filter_Alpha_Test());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaTest(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Test() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaBlend()
{
	ShaderClass::Set_Filter_Alpha_Blend(!ShaderClass::Get_Filter_Alpha_Blend());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaBlend(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Blend() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaBlendTest()
{
	ShaderClass::Set_Filter_Alpha_Blend_Test(!ShaderClass::Get_Filter_Alpha_Blend_Test());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaBlendTest(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Blend_Test() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderDoubleSided()
{
	ShaderClass::Force_Double_Sided(!ShaderClass::Is_Double_Sided_Forced());
}

void CMaterialViewerFrame::OnUpdateShaderDoubleSided(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Is_Double_Sided_Forced() ? 1 : 0);
}

// TheSuperHackers @feature Tria The Back Faces Tinted toggle is a viewer-wide setting
// applied by CGraphicView's second render pass; mirror the command here so the menu
// item is enabled (not greyed) and its check stays in sync while the Material Viewer
// is the active frame.
void CMaterialViewerFrame::OnShaderBackfaceTint()
{
	CGraphicView::Set_Show_Backface_Tint(!CGraphicView::Is_Show_Backface_Tint());
}

void CMaterialViewerFrame::OnUpdateShaderBackfaceTint(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(CGraphicView::Is_Show_Backface_Tint() ? 1 : 0);
}

// TheSuperHackers @feature Light menu: how Ctrl+left-drag moves the preview's
// scene light. Free Roam = the main viewport's trackball rotation; Per Face =
// place the light on the surface point under the cursor. Persisted in the
// profile; menu uses check marks (SetCheck) to match the surrounding items.
void CMaterialViewerFrame::OnLightFreeRoam()
{
	if (m_Preview != nullptr) {
		m_Preview->Set_Light_Placement_Mode(CMaterialPreviewPane::LIGHT_FREE_ROAM);
	}
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerLightPerFace", 0);
}

void CMaterialViewerFrame::OnUpdateLightFreeRoam(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr
		&& m_Preview->Get_Light_Placement_Mode() == CMaterialPreviewPane::LIGHT_FREE_ROAM) ? 1 : 0);
}

void CMaterialViewerFrame::OnLightPerFace()
{
	if (m_Preview != nullptr) {
		m_Preview->Set_Light_Placement_Mode(CMaterialPreviewPane::LIGHT_PER_FACE);
	}
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerLightPerFace", 1);
}

void CMaterialViewerFrame::OnUpdateLightPerFace(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr
		&& m_Preview->Get_Light_Placement_Mode() == CMaterialPreviewPane::LIGHT_PER_FACE) ? 1 : 0);
}

// TheSuperHackers @feature Tria Ground plane: a simple checkered ground with a
// blob shadow under the object, drawn by the preview pane below the model. The
// vertical slider on the preview's left edge moves the plane's height and
// appears together with the toggle.
void CMaterialViewerFrame::OnShowGround()
{
	if (m_Preview == nullptr) {
		return;
	}
	m_Preview->Set_Ground_Visible(!m_Preview->Is_Ground_Visible());
	SyncGroundSlider();
	ReassertLayout();
}

void CMaterialViewerFrame::OnUpdateShowGround(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr && m_Preview->Is_Ground_Visible()) ? 1 : 0);
}

void CMaterialViewerFrame::OnGroundReset()
{
	if (m_Preview == nullptr) {
		return;
	}
	m_Preview->Reset_Ground_To_Object();
	SyncGroundSlider();
}

void CMaterialViewerFrame::OnUpdateGroundReset(CCmdUI *cmd_ui)
{
	cmd_ui->Enable((m_Preview != nullptr && m_Preview->Is_Ground_Visible()) ? 1 : 0);
}

// Pushes the pane's current ground height onto the Qt slider (clamped into the
// current object's range so thumb and plane agree). Called when the toggle
// turns on and after every model change (which re-derives the range).
// Pushes the pane's current ground height onto the Qt slider (clamped into the
// current object's range so thumb and plane agree). Called when the toggle
// turns on and after every model change (which re-derives the range).
void CMaterialViewerFrame::SyncGroundSlider()
{
#ifdef W3DVIEW_HAS_QT
	if (m_Preview == nullptr || m_GroundSliderWnd == nullptr) {
		return;
	}

	int pos = GROUND_SLIDER_STEPS / 2;
	float z_min = 0.0F, z_max = 0.0F;
	if (m_Preview->Get_Ground_Z_Range(z_min, z_max) && z_max > z_min) {
		float z = m_Preview->Get_Ground_Z();
		z = (z < z_min) ? z_min : (z > z_max) ? z_max : z;
		m_Preview->Set_Ground_Z(z);
		pos = (int)((z_max - z) / (z_max - z_min) * GROUND_SLIDER_STEPS + 0.5F);
	}
	W3dMaterialViewer::SetGroundSliderPos(pos);
#endif
}

// User moved the Qt height slider: map its position (top = high) onto the
// pane's range. Fires synchronously on the UI thread from the Qt bridge.
void CMaterialViewerFrame::OnGroundSliderChanged(int pos)
{
	if (m_Preview == nullptr) {
		return;
	}
	float z_min = 0.0F, z_max = 0.0F;
	if (m_Preview->Get_Ground_Z_Range(z_min, z_max)) {
		float frac = (float)pos / (float)GROUND_SLIDER_STEPS;
		m_Preview->Set_Ground_Z(z_max - frac * (z_max - z_min));
	}
}

void CMaterialViewerFrame::GroundSliderChangedThunk(int pos)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnGroundSliderChanged(pos);
	}
}

// TheSuperHackers @feature Tria Pick the back-face tint colour via the Qt colour
// picker (to match the rest of the Material Viewer UI). Converts between the viewer's
// 0..1 float Vector3 and Win32's 0..255 COLORREF.
void CMaterialViewerFrame::OnShaderBackfaceTintColor()
{
	COLORREF initial = CGraphicView::Tint_Color_To_ColorRef(CGraphicView::Get_Backface_Tint_Color());

	COLORREF chosen = initial;
	if (W3dMaterialViewer::PickColor(initial, chosen)) {
		CGraphicView::Set_Backface_Tint_Color(CGraphicView::ColorRef_To_Tint_Color(chosen));
	}
}

CMaterialViewerFrame::CMaterialViewerFrame()
	: m_Preview(nullptr),
	  m_PanelWnd(nullptr),
	  m_TabBarWnd(nullptr),
	  m_GroundSliderWnd(nullptr),
	  m_AnimBarWnd(nullptr),
	  m_AnimBarVisible(false),
	  m_LastAnimFrame(-1),
	  m_LastAnimPlaying(false),
	  m_ActiveTab(-1),
	  m_LivePreviewPending(false),
	  m_BatchEdit(false),
	  m_BatchSelect(false),
	  m_RestoringSelection(false)
{
}

MaterialViewerTab &
CMaterialViewerFrame::Active()
{
	static MaterialViewerTab s_scratch;
	if (m_ActiveTab < 0 || m_ActiveTab >= (int)m_Tabs.size()) {
		s_scratch = MaterialViewerTab();
		return s_scratch;
	}
	return m_Tabs[m_ActiveTab];
}

CMaterialViewerFrame::~CMaterialViewerFrame()
{
}

void
CMaterialViewerFrame::ShowViewer()
{
	if (_TheInstance == nullptr) {
		CMaterialViewerFrame *frame = new CMaterialViewerFrame;
		if (frame->Create(nullptr,
				"W3D Material Viewer",
				WS_OVERLAPPEDWINDOW,
				rectDefault,
				::AfxGetMainWnd(),
				MAKEINTRESOURCE(IDR_MATERIAL_VIEWER_MENU)) == FALSE) {
			delete frame;
			return;
		}
		_TheInstance = frame;
		frame->ShowWindow(SW_SHOW);
		frame->UpdateWindow();
	} else {
		if (_TheInstance->IsIconic()) {
			_TheInstance->ShowWindow(SW_RESTORE);
		}
		_TheInstance->SetForegroundWindow();
	}
}

void
CMaterialViewerFrame::ShowViewerForAsset(const char *objName, const char *sourceFile, const char *meshName)
{
	ShowViewer();
	if (_TheInstance == nullptr || objName == nullptr || objName[0] == '\0') {
		return;
	}

	CMaterialViewerFrame *frame = _TheInstance;

	MaterialViewerTab tab;
	bool parsed = false;
	if (sourceFile != nullptr && sourceFile[0] != '\0') {
		parsed = W3dMaterialViewer::ParseMaterialDocumentFromFactory(sourceFile, tab.document);
	}

	// If the file (or, for source-less assets, the same top-level object) is
	// already open in a tab, switch to it instead of opening a duplicate.
	int existing = -1;
	if (parsed) {
		tab.sourceFilePath = tab.document.resolvedDiskPath;
		existing = frame->FindTab(tab.sourceFilePath.c_str());
	} else {
		for (size_t i = 0; i < frame->m_Tabs.size(); i++) {
			if (frame->m_Tabs[i].sourceFilePath.empty()
					&& ::_stricmp(frame->m_Tabs[i].topLevelName.c_str(), objName) == 0) {
				existing = (int)i;
				break;
			}
		}
	}
	if (existing >= 0) {
		if (existing != frame->m_ActiveTab) {
			if (!frame->PromptSaveIfDirty()) {
				return;
			}
			frame->ActivateTab(existing);
		}
		if (meshName != nullptr && meshName[0] != '\0') {
			frame->Active().currentMeshName = meshName;
#ifdef W3DVIEW_HAS_QT
			W3dMaterialViewer::SelectPanelMesh(meshName);
#endif
			frame->UpdatePreviewModel();
		}
		return;
	}

	if (parsed) {
		Fill_Texture_Previews(tab.document);
	}

	// The full object is the parsed HLod when available, else the clicked asset.
	tab.topLevelName = (parsed && !tab.document.topLevelName.empty())
		? tab.document.topLevelName : objName;
	if (meshName != nullptr && meshName[0] != '\0') {
		tab.currentMeshName = meshName;
	} else if (parsed && !tab.document.meshes.empty()) {
		tab.currentMeshName = tab.document.meshes.front().meshName;
	} else {
		tab.currentMeshName = objName;
	}

	if (!frame->PromptSaveIfDirty()) {
		return;
	}
	frame->AppendTabAndActivate(tab);
}

void
CMaterialViewerFrame::RenderActivePreview()
{
	if (_TheInstance != nullptr) {
#ifdef W3DVIEW_HAS_QT
		W3dMaterialViewer::PumpQtEvents();
#endif
		if (_TheInstance->m_Preview != nullptr) {
			_TheInstance->m_Preview->Render();
			// Mirror playback into the animation bar (cheap; change-detected).
			_TheInstance->PushAnimBarState();
		}
	}
}

bool
CMaterialViewerFrame::HasQtPanel()
{
	return (_TheInstance != nullptr) && (_TheInstance->m_PanelWnd != nullptr);
}

void
CMaterialViewerFrame::NotifyThemeChanged()
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_W3DVIEW_THEME_CHANGED, W3DDarkMode::IsDark() ? 1 : 0, 0);
	}
}

// When keyboard focus is inside the Qt panel, hand keyboard messages straight
// to the focused window instead of letting the frame translate them. The frame
// owns an accelerator table (arrows / PgUp / PgDn / Ctrl+combos for the main
// window) and CFrameWnd also does dialog-style key routing; both would eat the
// editing keys the Qt text fields and spin boxes need (Delete, Home/End,
// arrows, Ctrl+A, Backspace, ...), so the user could only append text.
BOOL
CMaterialViewerFrame::PreTranslateMessage(MSG *msg)
{
	// The File/Edit menu shortcuts (Ctrl+O/S/Z, Ctrl+Shift+Z) are advertised in
	// the menu but the frame has no accelerator table, and the Qt hand-off below
	// would otherwise swallow them. Handle these viewer-level commands here so
	// they work regardless of whether focus is in the Qt panel or the preview.
	if (msg->message == WM_KEYDOWN && (::GetKeyState(VK_CONTROL) & 0x8000)) {
		const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

		// Ctrl+Tab / Ctrl+Shift+Tab cycle through the file tabs (wrapping).
		// Goes through OnTabChanged so unsaved edits get the same
		// Save/Discard/Cancel prompt as a tab-strip click.
		if (msg->wParam == VK_TAB && m_Tabs.size() > 1) {
			const int count = (int)m_Tabs.size();
			OnTabChanged(shift ? (m_ActiveTab - 1 + count) % count
				: (m_ActiveTab + 1) % count);
			return TRUE;
		}

		UINT command = 0;
		switch (msg->wParam) {
			case 'S': command = IDM_MATVIEWER_SAVE; break;
			case 'O': command = IDM_MATVIEWER_OPEN; break;
			case 'Z': command = shift ? IDM_MATVIEWER_REDO : IDM_MATVIEWER_UNDO; break;
		}
		if (command != 0) {
			SendMessage(WM_COMMAND, MAKEWPARAM(command, 0), 0);
			return TRUE;
		}
	}

	// Shift+letter view toggles (Shift+F = Show Full Object, Shift+G = Show
	// Ground Plane). Checked BEFORE the Qt hand-off below, but only when focus
	// is NOT in a text-entry widget — so it still fires when the panel merely
	// has focus (e.g. right after a tab switch, where focus lands on the panel
	// but not an editable field), while a user typing an uppercase letter into a
	// line edit / spin box is unaffected. Ignore auto-repeat (bit 30 of lParam)
	// so a held key doesn't flip the toggle rapidly, and require Shift w/o Ctrl.
	if (msg->message == WM_KEYDOWN
			&& (::GetKeyState(VK_SHIFT) & 0x8000)
			&& !(::GetKeyState(VK_CONTROL) & 0x8000)
			&& (msg->lParam & (1 << 30)) == 0
#ifdef W3DVIEW_HAS_QT
			&& !W3dMaterialViewer::PanelFocusIsTextEntry()
#endif
			) {
		UINT command = 0;
		switch (msg->wParam) {
			case 'F': command = IDM_MATVIEWER_SHOW_FULL; break;
			case 'G': command = IDM_MATVIEWER_GROUND; break;
		}
		if (command != 0) {
			SendMessage(WM_COMMAND, MAKEWPARAM(command, 0), 0);
			return TRUE;
		}
	}

	// 7 toggles the poly and vertex totals, unless it is being typed into the panel
	if (ViewportStats::Is_Toggle_Key(msg)
#ifdef W3DVIEW_HAS_QT
			&& !W3dMaterialViewer::PanelFocusIsTextEntry()
#endif
			) {
		ViewportStats::Toggle();
		return TRUE;
	}

	if (msg->message >= WM_KEYFIRST && msg->message <= WM_KEYLAST) {
#ifdef W3DVIEW_HAS_QT
		HWND panel = m_PanelWnd;
		if (panel != nullptr && ::IsWindow(panel)) {
			HWND focus = ::GetFocus();
			if (focus != nullptr && (focus == panel || ::IsChild(panel, focus))) {
				// Dispatch to Qt's wndproc directly; skip the frame's
				// accelerator / IsDialogMessage handling entirely.
				::TranslateMessage(msg);
				::DispatchMessage(msg);
				return TRUE;
			}
		}
#endif
	}

	return CFrameWnd::PreTranslateMessage(msg);
}

int
CMaterialViewerFrame::OnCreate(LPCREATESTRUCT create_struct)
{
	if (CFrameWnd::OnCreate(create_struct) == -1) {
		return -1;
	}

	CreateToolBar();

	m_Preview = new CMaterialPreviewPane;
	m_Preview->Create(this, CRect(0, 0, 10, 10), PREVIEW_ID);

	m_Preview->Set_Light_Placement_Mode(
		::AfxGetApp()->GetProfileInt("Config", "MatViewerLightPerFace", 1) != 0
			? CMaterialPreviewPane::LIGHT_PER_FACE
			: CMaterialPreviewPane::LIGHT_FREE_ROAM);

	// Restore the "backup before change" preference (default on).
	W3dMaterialViewer::Set_Backup_Before_Save(
		::AfxGetApp()->GetProfileInt("Config", "MatViewerBackupBeforeSave", 1) != 0);

	// Restore the selection-highlight style (default: bounding box).
	m_Preview->Set_Tint_Highlight(
		::AfxGetApp()->GetProfileInt("Config", "MatViewerTintSelection", 0) != 0);

#ifdef W3DVIEW_HAS_QT
	m_PanelWnd = W3dMaterialViewer::CreatePanel(m_hWnd);
	m_TabBarWnd = W3dMaterialViewer::CreateTabBar(m_hWnd);
	if (m_TabBarWnd != nullptr) {
		W3dMaterialViewer::SetTabBarChangedCallback(&CMaterialViewerFrame::TabChangedThunk);
		W3dMaterialViewer::SetTabBarCloseRequestedCallback(&CMaterialViewerFrame::TabCloseRequestedThunk);
		::ShowWindow(m_TabBarWnd, SW_HIDE);	// shown once the first tab opens
	}
	// Ground-height slider: a Qt QSlider so it follows the panel's dark theme
	// (the Win32 trackbar has no dark mode). Thin vertical strip left of the
	// preview, shown only while Ground > Show Ground Plane is on.
	m_GroundSliderWnd = W3dMaterialViewer::CreateGroundSlider(m_hWnd, GROUND_SLIDER_STEPS);
	if (m_GroundSliderWnd != nullptr) {
		W3dMaterialViewer::SetGroundSliderChangedCallback(&CMaterialViewerFrame::GroundSliderChangedThunk);
		W3dMaterialViewer::SetGroundSliderPos(GROUND_SLIDER_STEPS / 2);
		::ShowWindow(m_GroundSliderWnd, SW_HIDE);	// shown with the toggle
	}

	// Animation bar: Qt playback strip docked under the preview, shown only
	// when the previewed model's hierarchy has animations (RefreshAnimBar).
	m_AnimBarWnd = W3dMaterialViewer::CreateAnimBar(m_hWnd);
	if (m_AnimBarWnd != nullptr) {
		W3dMaterialViewer::SetAnimBarAnimSelectedCallback(&CMaterialViewerFrame::AnimSelectedThunk);
		W3dMaterialViewer::SetAnimBarPlayPauseCallback(&CMaterialViewerFrame::AnimPlayPauseThunk);
		W3dMaterialViewer::SetAnimBarStopCallback(&CMaterialViewerFrame::AnimStopThunk);
		W3dMaterialViewer::SetAnimBarSeekCallback(&CMaterialViewerFrame::AnimSeekThunk);
		::ShowWindow(m_AnimBarWnd, SW_HIDE);	// shown when animations exist
	}

	W3dMaterialViewer::SetPanelMeshSelectedCallback(&CMaterialViewerFrame::PanelMeshSelectedThunk);
	W3dMaterialViewer::SetPanelEditGateCallback(&CMaterialViewerFrame::EditGateThunk);
	W3dMaterialViewer::SetPanelSaveCallback(&CMaterialViewerFrame::SaveThunk);
	W3dMaterialViewer::SetPanelRevertCallback(&CMaterialViewerFrame::RevertThunk);
	W3dMaterialViewer::SetPanelDirtyChangedCallback(&CMaterialViewerFrame::DirtyChangedThunk);
	W3dMaterialViewer::SetPanelResolveTextureCallback(&CMaterialViewerFrame::ResolveTextureThunk);
	W3dMaterialViewer::SetPanelLivePreviewCallback(&CMaterialViewerFrame::LivePreviewThunk);
#endif

	if (m_PanelWnd == nullptr) {
		m_PlaceholderText.Create(
			"This build does not include the Qt material panel.",
			WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
			CRect(0, 0, 10, 10), this, PLACEHOLDER_ID);
	}

	// Pause the main viewport for the viewer's lifetime: both viewports share
	// the one D3D8 device, and running them in lockstep has proven fragile.
	// OnDestroy unpauses and refreshes (device reset) the main view.
	{
		CW3DViewDoc *doc = ::GetCurrentDocument();
		CGraphicView *view = (doc != nullptr) ? doc->GetGraphicView() : nullptr;
		if (view != nullptr) {
			view->Allow_Update(false);
		}
	}

	// Accept .w3d files dropped anywhere on the window (each opens as a tab).
	DragAcceptFiles(TRUE);

	W3DDarkMode::ApplyToWindow(m_hWnd);
	ApplyPanelTheme();

	// Same follow-up as InitInstance does for the main frame: non-client
	// pixels painted before the dark-mode subclasses were installed (the
	// menubar's bottom line) stay stale without a forced frame redraw.
	SetWindowPos(nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	RedrawWindow(nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	return 0;
}

void
CMaterialViewerFrame::CreateToolBar()
{
	if (!m_ToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY)) {
		return;
	}

	m_ToolBar.SetButtons(TOOLBAR_BUTTONS, TOOLBAR_BUTTON_COUNT);
	m_ToolBar.SetSizes(CSize(TOOLBAR_ICON_WIDTH + 7, TOOLBAR_ICON_HEIGHT + 7),
		CSize(TOOLBAR_ICON_WIDTH, TOOLBAR_ICON_HEIGHT));
	for (int i = 0; i < TOOLBAR_BUTTON_COUNT; i++) {
		m_ToolBar.SetButtonInfo(i, TOOLBAR_BUTTONS[i], TBBS_BUTTON, TOOLBAR_FIRST_SHADER_ICON + i);
	}

	Load_Toolbar_Strip(m_ToolBarImagesLight, IDR_MAINFRAME);
	Load_Toolbar_Strip(m_ToolBarImagesDark, IDR_MAINFRAME_DM);
	ApplyToolBarTheme();
}

void
CMaterialViewerFrame::ApplyToolBarTheme()
{
	if (!::IsWindow(m_ToolBar.m_hWnd)) {
		return;
	}

	CImageList &images = W3DDarkMode::IsDark() ? m_ToolBarImagesDark : m_ToolBarImagesLight;
	if (images.GetSafeHandle() != nullptr) {
		m_ToolBar.GetToolBarCtrl().SetImageList(&images);
	}
	m_ToolBar.Invalidate();
}

void
CMaterialViewerFrame::OnClose()
{
	// Guard unsaved edits before the window (and its document) goes away.
	if (!PromptSaveIfDirty()) {
		return;
	}

	// Hand activation back to the owner before the window dies. Otherwise
	// Windows picks the next window in the Alt+Tab order, which can shove the
	// main frame to the background or even minimize it.
	CWnd *owner = GetOwner();
	if (owner != nullptr && ::IsWindow(owner->m_hWnd)) {
		owner->SetForegroundWindow();
	}
	CFrameWnd::OnClose();
}

void
CMaterialViewerFrame::OnDestroy()
{
	KillTimer(LIVE_PREVIEW_TIMER);	// no late live-preview fire after teardown

#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr) {
		W3dMaterialViewer::SetTabBarChangedCallback(nullptr);
		W3dMaterialViewer::SetTabBarCloseRequestedCallback(nullptr);
		W3dMaterialViewer::DestroyTabBar();
		m_TabBarWnd = nullptr;
	}
	if (m_GroundSliderWnd != nullptr) {
		W3dMaterialViewer::SetGroundSliderChangedCallback(nullptr);
		W3dMaterialViewer::DestroyGroundSlider();
		m_GroundSliderWnd = nullptr;
	}
	if (m_AnimBarWnd != nullptr) {
		W3dMaterialViewer::SetAnimBarAnimSelectedCallback(nullptr);
		W3dMaterialViewer::SetAnimBarPlayPauseCallback(nullptr);
		W3dMaterialViewer::SetAnimBarStopCallback(nullptr);
		W3dMaterialViewer::SetAnimBarSeekCallback(nullptr);
		W3dMaterialViewer::DestroyAnimBar();
		m_AnimBarWnd = nullptr;
	}
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::DestroyPanel();
		m_PanelWnd = nullptr;
	}
#endif

	// Resume the main viewport (paused in OnCreate) and queue a full refresh
	// (F5 path: device reset) so it comes back to a pristine device state.
	{
		CW3DViewDoc *doc = ::GetCurrentDocument();
		CGraphicView *view = (doc != nullptr) ? doc->GetGraphicView() : nullptr;
		if (view != nullptr) {
			view->Allow_Update(true);
		}
		CWnd *main_wnd = ::AfxGetMainWnd();
		if (main_wnd != nullptr && ::IsWindow(main_wnd->m_hWnd)) {
			main_wnd->PostMessage(WM_COMMAND, IDM_REFRESH_VIEWPORT);
		}
	}

	CFrameWnd::OnDestroy();
}

void
CMaterialViewerFrame::PostNcDestroy()
{
	if (_TheInstance == this) {
		_TheInstance = nullptr;
	}
	m_Preview = nullptr;	// destroyed with the window; freed by MFC PostNcDestroy
	CFrameWnd::PostNcDestroy();
}

void
CMaterialViewerFrame::OnSize(UINT type, int cx, int cy)
{
	CFrameWnd::OnSize(type, cx, cy);
	if (cx > 0 && cy > 0) {
		Layout(cx, cy);
	}
}

// The frame's client area is normally fully covered by the panes, but fill
// it with the theme background so any sliver that isn't never shows white.
BOOL
CMaterialViewerFrame::OnEraseBkgnd(CDC *dc)
{
	if (W3DDarkMode::IsDark()) {
		CRect rect;
		GetClientRect(&rect);
		::FillRect(dc->m_hDC, &rect, W3DDarkMode::GetDlgBackgroundBrush());
		return TRUE;
	}

	return CFrameWnd::OnEraseBkgnd(dc);
}

void
CMaterialViewerFrame::ReassertLayout()
{
	CRect rect;
	GetClientRect(&rect);
	if (rect.Width() > 0 && rect.Height() > 0) {
		Layout(rect.Width(), rect.Height());
	}
}

void
CMaterialViewerFrame::Layout(int cx, int cy)
{
	// The frame docks the toolbar along the top; the panes start below it
	int bar_height = 0;
	if (::IsWindow(m_ToolBar.m_hWnd) && m_ToolBar.IsWindowVisible()) {
		CRect bar;
		m_ToolBar.GetWindowRect(&bar);
		bar_height = bar.Height();
	}

	// File-tab strip under the toolbar, shown only while tabs are open.
	int tab_height = 0;
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr && HasTabs()) {
		tab_height = W3dMaterialViewer::GetTabBarPreferredHeight();
		if (tab_height < MIN_TAB_BAR_HEIGHT) {
			tab_height = MIN_TAB_BAR_HEIGHT;
		}
		W3dMaterialViewer::ResizeTabBar(cx, tab_height);
		::MoveWindow(m_TabBarWnd, 0, bar_height, cx, tab_height, TRUE);
	}
#endif

	int body_top = bar_height + tab_height;
	int body_height = cy - body_top;
	int preview_width = (int)(cx * PREVIEW_WIDTH_RATIO);

	// Ground-height slider strip (Qt QSlider) on the preview's left, only while
	// the ground plane is shown; the preview shrinks by the strip's width.
	int slider_width = 0;
#ifdef W3DVIEW_HAS_QT
	if (m_GroundSliderWnd != nullptr) {
		bool visible = (m_Preview != nullptr && m_Preview->Is_Ground_Visible());
		slider_width = visible ? GROUND_SLIDER_WIDTH : 0;
		if (visible) {
			// Resize through Qt first so its logical geometry matches (same
			// dance as the panel/tab bar).
			W3dMaterialViewer::ResizeGroundSlider(slider_width, body_height);
			::MoveWindow(m_GroundSliderWnd, 0, body_top, slider_width, body_height, TRUE);
			::ShowWindow(m_GroundSliderWnd, SW_SHOW);
		} else {
			::ShowWindow(m_GroundSliderWnd, SW_HIDE);
		}
	}
#endif

	// Animation bar docked under the preview (only when the model has
	// animations); the preview shrinks by the bar's height.
	int anim_height = 0;
#ifdef W3DVIEW_HAS_QT
	if (m_AnimBarWnd != nullptr) {
		if (m_AnimBarVisible) {
			anim_height = W3dMaterialViewer::GetAnimBarPreferredHeight();
			W3dMaterialViewer::ResizeAnimBar(preview_width - slider_width, anim_height);
			::MoveWindow(m_AnimBarWnd, slider_width, body_top + body_height - anim_height,
				preview_width - slider_width, anim_height, TRUE);
			::ShowWindow(m_AnimBarWnd, SW_SHOW);
		} else {
			::ShowWindow(m_AnimBarWnd, SW_HIDE);
		}
	}
#endif

	if ((m_Preview != nullptr) && ::IsWindow(m_Preview->m_hWnd)) {
		m_Preview->MoveWindow(slider_width, body_top, preview_width - slider_width, body_height - anim_height);
	}

	if (m_PanelWnd != nullptr) {
#ifdef W3DVIEW_HAS_QT
		// Resize through Qt first so its logical geometry/layout match; the
		// MoveWindow then pins the child's position (Qt may have placed it at
		// a stale pre-reparent position).
		W3dMaterialViewer::ResizePanel(cx - preview_width, body_height);
#endif
		::MoveWindow(m_PanelWnd, preview_width, body_top, cx - preview_width, body_height, TRUE);
	} else if (::IsWindow(m_PlaceholderText.m_hWnd)) {
		m_PlaceholderText.MoveWindow(preview_width, body_top, cx - preview_width, body_height);
	}
}

void
CMaterialViewerFrame::OnFileOpen()
{
	CFileDialog dialog(TRUE, ".w3d", nullptr,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT,
		"Westwood 3D Files (*.w3d)|*.w3d|All Files (*.*)|*.*||", this);

	// Room for many selected paths; the default OPENFILENAME buffer holds one.
	std::vector<char> path_buffer(32768, '\0');
	dialog.m_ofn.lpstrFile = &path_buffer[0];
	dialog.m_ofn.nMaxFile = (DWORD)path_buffer.size();

	if (dialog.DoModal() != IDOK) {
		return;
	}

	POSITION pos = dialog.GetStartPosition();
	while (pos != nullptr) {
		OpenFileInTab(dialog.GetNextPathName(pos));
	}
}

void
CMaterialViewerFrame::OnFileClose()
{
	// Closes the active tab; the window itself goes when the last tab does.
	if (HasTabs()) {
		CloseTab(m_ActiveTab);
	} else {
		PostMessage(WM_CLOSE);
	}
}

// Dropping .w3d files anywhere on the viewer opens each one as a tab (a file
// that is already open just gets its tab activated). Other files are ignored.
void
CMaterialViewerFrame::OnDropFiles(HDROP drop)
{
	UINT count = ::DragQueryFile(drop, 0xFFFFFFFF, nullptr, 0);
	for (UINT i = 0; i < count; i++) {
		char path[MAX_PATH] = { 0 };
		if (::DragQueryFile(drop, i, path, sizeof(path)) == 0) {
			continue;
		}
		const char *ext = ::strrchr(path, '.');
		if (ext == nullptr || ::_stricmp(ext, ".w3d") != 0) {
			continue;
		}
		OpenFileInTab(path);
	}
	::DragFinish(drop);
}

////////////////////////////////////////////////////////////////////////////
//	Tabs
////////////////////////////////////////////////////////////////////////////

int
CMaterialViewerFrame::FindTab(const char *path) const
{
	if (path == nullptr || path[0] == '\0') {
		return -1;
	}
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		if (::_stricmp(m_Tabs[i].sourceFilePath.c_str(), path) == 0) {
			return (int)i;
		}
	}
	return -1;
}

std::string
CMaterialViewerFrame::TabLabel(const MaterialViewerTab &tab)
{
	std::string label;
	if (!tab.sourceFilePath.empty()) {
		const char *slash = ::strrchr(tab.sourceFilePath.c_str(), '\\');
		const char *fwd = ::strrchr(tab.sourceFilePath.c_str(), '/');
		if (fwd > slash) {
			slash = fwd;
		}
		label = slash ? slash + 1 : tab.sourceFilePath.c_str();
	} else if (!tab.topLevelName.empty()) {
		label = tab.topLevelName;
	} else {
		label = "(untitled)";
	}
	if (tab.dirty) {
		label += " *";
	}
	return label;
}

bool
CMaterialViewerFrame::OpenFileInTab(const char *path, const char *meshName)
{
	if (path == nullptr || path[0] == '\0') {
		return false;
	}

	int existing = FindTab(path);
	if (existing >= 0) {
		if (existing != m_ActiveTab) {
			if (!PromptSaveIfDirty()) {
				return false;
			}
			ActivateTab(existing);
		}
		if (meshName != nullptr && meshName[0] != '\0') {
			Active().currentMeshName = meshName;
#ifdef W3DVIEW_HAS_QT
			W3dMaterialViewer::SelectPanelMesh(meshName);
#endif
			UpdatePreviewModel();
		}
		return true;
	}

	// The panel always reflects the actual bytes of the chosen file.
	MaterialViewerTab tab;
	if (W3dMaterialViewer::ParseMaterialDocument(path, tab.document) == false) {
		CString message;
		message.Format("%s\n\nThe file could not be read or contains no meshes.", path);
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	if (!PromptSaveIfDirty()) {
		return false;
	}

	tab.sourceFilePath = path;
	Fill_Texture_Previews(tab.document);
	tab.topLevelName = tab.document.topLevelName;
	if (meshName != nullptr && meshName[0] != '\0') {
		tab.currentMeshName = meshName;
	} else {
		tab.currentMeshName = tab.document.meshes.empty() ? tab.document.topLevelName
			: tab.document.meshes.front().meshName;
	}

	AppendTabAndActivate(tab);
	return true;
}

void
CMaterialViewerFrame::AppendTabAndActivate(MaterialViewerTab &tab)
{
	m_Tabs.push_back(std::move(tab));
	ActivateTab((int)m_Tabs.size() - 1);
}

void
CMaterialViewerFrame::SnapshotActiveTab()
{
	if (m_ActiveTab < 0 || m_ActiveTab >= (int)m_Tabs.size()) {
		return;
	}
	MaterialViewerTab &tab = m_Tabs[m_ActiveTab];
	if (m_Preview != nullptr) {
		m_Preview->Get_Camera_State(tab.cameraTransform, tab.cameraCenter,
			tab.cameraRotation, tab.cameraDistance, tab.cameraMinZoomAdjust);
		tab.cameraValid = true;
	}
}

void
CMaterialViewerFrame::ActivateTab(int index)
{
	if (index < 0 || index >= (int)m_Tabs.size()) {
		return;
	}

	SnapshotActiveTab();
	m_ActiveTab = index;
	MaterialViewerTab &tab = m_Tabs[index];

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		// SetPanelDocument internally re-shows a mesh, which fires the
		// mesh-selected callback (OnPanelMeshSelected) and would overwrite this
		// tab's remembered currentMeshName with the panel's default index before
		// we restore it. Suppress that feedback while we set the document and
		// re-select the remembered mesh.
		m_RestoringSelection = true;
		W3dMaterialViewer::SetPanelDocument(tab.document);
		if (!tab.currentMeshName.empty()) {
			W3dMaterialViewer::SelectPanelMesh(tab.currentMeshName.c_str());
		}
		m_RestoringSelection = false;
	}
#endif

	// Different files can carry identically-named prototypes (Load_3D_Assets
	// silently skips duplicates), so reload this file's assets to guarantee
	// the preview shows ITS bytes. Ends with UpdatePreviewModel; a source-less
	// tab (asset straight from the tree) just re-targets the preview.
	if (!tab.sourceFilePath.empty()) {
		ReloadAssetsForPreview();
	} else {
		UpdatePreviewModel();
	}

	// Restore this tab's saved view. Must come after the model load above,
	// which re-frames the camera whenever the loaded name changes.
	if (tab.cameraValid && m_Preview != nullptr) {
		m_Preview->Set_Camera_State(tab.cameraTransform, tab.cameraCenter,
			tab.cameraRotation, tab.cameraDistance, tab.cameraMinZoomAdjust);
	}

	RebuildTabBar();
	UpdateTitle();
}

bool
CMaterialViewerFrame::CloseTab(int index)
{
	if (index < 0 || index >= (int)m_Tabs.size()) {
		return true;
	}

	// Only the active tab can hold unsaved edits (switching away prompts).
	if (index == m_ActiveTab && !PromptSaveIfDirty()) {
		return false;
	}

	m_Tabs.erase(m_Tabs.begin() + index);

	if (m_Tabs.empty()) {
		m_ActiveTab = -1;
		RebuildTabBar();
		PostMessage(WM_CLOSE);
		return true;
	}

	if (index < m_ActiveTab) {
		// Indices above the erased tab shift down; the active file is unchanged.
		m_ActiveTab--;
		RebuildTabBar();
	} else if (index == m_ActiveTab) {
		// The active tab went away: show its right-hand neighbour (or the new
		// last tab). Invalidate first so ActivateTab does not snapshot the
		// camera into whatever tab now occupies the dead tab's index.
		m_ActiveTab = -1;
		ActivateTab((index < (int)m_Tabs.size()) ? index : (int)m_Tabs.size() - 1);
	} else {
		RebuildTabBar();
	}
	return true;
}

void
CMaterialViewerFrame::RebuildTabBar()
{
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd == nullptr) {
		return;
	}

	std::vector<std::string> labels;
	std::vector<const char *> label_ptrs;
	std::vector<const char *> tip_ptrs;
	labels.reserve(m_Tabs.size());
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		labels.push_back(TabLabel(m_Tabs[i]));
	}
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		label_ptrs.push_back(labels[i].c_str());
		tip_ptrs.push_back(m_Tabs[i].sourceFilePath.c_str());
	}

	W3dMaterialViewer::SetTabBarTabs(
		label_ptrs.empty() ? nullptr : &label_ptrs[0],
		tip_ptrs.empty() ? nullptr : &tip_ptrs[0],
		(int)m_Tabs.size(), m_ActiveTab);

	::ShowWindow(m_TabBarWnd, HasTabs() ? SW_SHOW : SW_HIDE);
	ReassertLayout();
#endif
}

void
CMaterialViewerFrame::OnTabChanged(int index)
{
	if (index == m_ActiveTab || index < 0 || index >= (int)m_Tabs.size()) {
		return;
	}
	if (!PromptSaveIfDirty()) {
		// Cancelled: snap the strip's selection back to the active tab.
#ifdef W3DVIEW_HAS_QT
		W3dMaterialViewer::SetTabBarCurrent(m_ActiveTab);
#endif
		return;
	}
	ActivateTab(index);
}

void
CMaterialViewerFrame::OnTabCloseRequested(int index)
{
	CloseTab(index);
}

LRESULT
CMaterialViewerFrame::OnTabAction(WPARAM wparam, LPARAM lparam)
{
	if (lparam == 0) {
		OnTabChanged((int)wparam);
	} else {
		OnTabCloseRequested((int)wparam);
	}
	return 0;
}

void
CMaterialViewerFrame::TabChangedThunk(int index)
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_MATVIEWER_TAB_ACTION, (WPARAM)index, 0);
	}
}

void
CMaterialViewerFrame::TabCloseRequestedThunk(int index)
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_MATVIEWER_TAB_ACTION, (WPARAM)index, 1);
	}
}

////////////////////////////////////////////////////////////////////////////
//
//	Edit-mode menu commands (File > Save, Edit > Undo/Redo/Revert). These
//	mirror the panel's in-window buttons; each item is enabled only when the
//	corresponding panel action is available.
//
void
CMaterialViewerFrame::OnFileSave()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelSave();
#endif
}

void
CMaterialViewerFrame::OnUpdateFileSave(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanSaveOrRevert());
#else
	cmd_ui->Enable(FALSE);
#endif
}

// File > Backup File Before Change: when checked, the first save of a file makes
// a one-time <file>.bak before overwriting. Persisted to the MFC profile.
void
CMaterialViewerFrame::OnToggleBackup()
{
	bool enabled = !W3dMaterialViewer::Get_Backup_Before_Save();
	W3dMaterialViewer::Set_Backup_Before_Save(enabled);
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerBackupBeforeSave", enabled ? 1 : 0);
}

void
CMaterialViewerFrame::OnUpdateToggleBackup(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(W3dMaterialViewer::Get_Backup_Before_Save() ? 1 : 0);
}

void
CMaterialViewerFrame::OnEditUndo()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelUndo();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditUndo(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanUndo());
#else
	cmd_ui->Enable(FALSE);
#endif
}

void
CMaterialViewerFrame::OnEditRedo()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelRedo();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditRedo(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanRedo());
#else
	cmd_ui->Enable(FALSE);
#endif
}

void
CMaterialViewerFrame::OnEditRevert()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelRevert();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditRevert(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanSaveOrRevert());
#else
	cmd_ui->Enable(FALSE);
#endif
}

// Edit > Batch Edit toggle. When on, a Save propagates the active file's per-mesh
// material edits to every other open tab's file (see BatchPropagate).
void
CMaterialViewerFrame::OnBatchEdit()
{
	m_BatchEdit = !m_BatchEdit;
}

void
CMaterialViewerFrame::OnUpdateBatchEdit(CCmdUI *cmd_ui)
{
	// Only meaningful with more than one file open.
	cmd_ui->Enable(m_Tabs.size() > 1);
	cmd_ui->SetCheck(m_BatchEdit ? 1 : 0);
}

// Edit > Batch Select toggle. When on, picking a mesh in the panel syncs the
// same-sub-named mesh selection into every other tab (see OnPanelMeshSelected).
void
CMaterialViewerFrame::OnBatchSelect()
{
	m_BatchSelect = !m_BatchSelect;
	// Apply immediately to the current selection so turning it on lines the tabs
	// up right away rather than waiting for the next mesh click.
	if (m_BatchSelect && !Active().currentMeshName.empty()) {
		SyncSelectedMeshAcrossTabs(Active().currentMeshName.c_str());
	}
}

void
CMaterialViewerFrame::OnUpdateBatchSelect(CCmdUI *cmd_ui)
{
	cmd_ui->Enable(m_Tabs.size() > 1);
	cmd_ui->SetCheck(m_BatchSelect ? 1 : 0);
}

// Sets every OTHER tab's remembered mesh selection to the mesh whose sub-object
// name (part after the last '.') matches `meshName`, so switching tabs stays on
// the same sub-object. Tabs without a matching mesh keep their own selection.
// Only the remembered name is changed here; ActivateTab re-selects it in the
// panel when that tab is next shown.
void
CMaterialViewerFrame::SyncSelectedMeshAcrossTabs(const char *meshName)
{
	if (meshName == nullptr || meshName[0] == '\0') {
		return;
	}
	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		if (i == m_ActiveTab) {
			continue;
		}
		for (const W3dMaterialViewer::MeshMaterialData &mesh : m_Tabs[i].document.meshes) {
			if (Mesh_Names_Match(mesh.meshName, meshName)) {
				m_Tabs[i].currentMeshName = mesh.meshName;
				break;	// first sub-name match wins
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////
//	Animation bar
////////////////////////////////////////////////////////////////////////////

void
CMaterialViewerFrame::RefreshAnimBar()
{
#ifdef W3DVIEW_HAS_QT
	if (m_AnimBarWnd == nullptr || m_Preview == nullptr) {
		return;
	}

	// Hierarchy the previewed file's object animates with. The pane may be
	// showing a bare mesh (no HTree), so fall back to a throwaway instance of
	// the file's top-level object to read the skeleton name — animations can
	// live on a shared skeleton whose name differs from the container's.
	StringClass hierarchy_name;
	RenderObjClass *obj = m_Preview->Peek_Render_Object();
	const HTreeClass *tree = (obj != nullptr) ? obj->Get_HTree() : nullptr;
	if (tree != nullptr) {
		hierarchy_name = tree->Get_Name();
	} else if (HasTabs() && !Active().topLevelName.empty()) {
		RenderObjClass *tmp = WW3DAssetManager::Get_Instance()->Create_Render_Obj(
			Active().topLevelName.c_str());
		if (tmp != nullptr) {
			const HTreeClass *tmp_tree = tmp->Get_HTree();
			if (tmp_tree != nullptr) {
				hierarchy_name = tmp_tree->Get_Name();
			}
			tmp->Release_Ref();
		}
	}

	std::vector<std::string> names;
	if (!hierarchy_name.Is_Empty()) {
		AssetIterator *iter = WW3DAssetManager::Get_Instance()->Create_HAnim_Iterator();
		if (iter != nullptr) {
			for (iter->First(); iter->Is_Done() == FALSE; iter->Next()) {
				HAnimClass *anim = WW3DAssetManager::Get_Instance()->Get_HAnim(iter->Current_Item_Name());
				if (anim != nullptr) {
					if (::_stricmp(anim->Get_HName(), hierarchy_name.str()) == 0) {
						names.push_back(iter->Current_Item_Name());
					}
					anim->Release_Ref();
				}
			}
			delete iter;
		}
	}

	// Unchanged list: keep the current selection and playback untouched (this
	// runs on every preview-model update, e.g. a mesh click during playback).
	if (names == m_AnimNames) {
		return;
	}
	m_AnimNames.swap(names);

	std::vector<const char *> ptrs;
	for (const std::string &name : m_AnimNames) {
		ptrs.push_back(name.c_str());
	}
	W3dMaterialViewer::SetAnimBarAnims(ptrs.empty() ? nullptr : &ptrs[0],
		(int)m_AnimNames.size(), 0);

	// Select the first animation (paused at frame 0) so Play works immediately;
	// clear the pane's animation when none match.
	HAnimClass *first = m_AnimNames.empty() ? nullptr
		: WW3DAssetManager::Get_Instance()->Get_HAnim(m_AnimNames.front().c_str());
	m_Preview->Play_Animation(false);
	m_Preview->Set_Animation_Asset(first);
	if (first != nullptr) {
		first->Release_Ref();	// the pane holds its own ref
	}
	m_LastAnimFrame = -1;	// force the next PushAnimBarState to update the bar

	bool visible = !m_AnimNames.empty();
	if (visible != m_AnimBarVisible) {
		m_AnimBarVisible = visible;
		ReassertLayout();
	}
#endif
}

void
CMaterialViewerFrame::PushAnimBarState()
{
#ifdef W3DVIEW_HAS_QT
	if (m_AnimBarWnd == nullptr || !m_AnimBarVisible || m_Preview == nullptr) {
		return;
	}
	HAnimClass *anim = m_Preview->Peek_Animation_Asset();
	int frame = (int)m_Preview->Get_Animation_Frame();
	bool playing = m_Preview->Is_Animation_Playing();
	if (frame == m_LastAnimFrame && playing == m_LastAnimPlaying) {
		return;
	}
	m_LastAnimFrame = frame;
	m_LastAnimPlaying = playing;
	W3dMaterialViewer::SetAnimBarState(playing, frame,
		(anim != nullptr) ? anim->Get_Num_Frames() : 0);
#endif
}

void
CMaterialViewerFrame::OnAnimSelected(int index)
{
	if (m_Preview == nullptr || index < 0 || index >= (int)m_AnimNames.size()) {
		return;
	}
	HAnimClass *anim = WW3DAssetManager::Get_Instance()->Get_HAnim(m_AnimNames[index].c_str());
	m_Preview->Set_Animation_Asset(anim);
	if (anim != nullptr) {
		anim->Release_Ref();	// the pane holds its own ref
	}
	m_LastAnimFrame = -1;
}

void
CMaterialViewerFrame::OnAnimPlayPause()
{
	if (m_Preview == nullptr || m_AnimNames.empty()) {
		return;
	}
	// Animations pose the whole hierarchy; a bare mesh has no skeleton, so
	// playing from the single-mesh view switches to the full object first.
	if (HasTabs() && !Active().showFullObject) {
		OnShowFullObject();
	}
	m_Preview->Play_Animation(!m_Preview->Is_Animation_Playing());
	m_LastAnimFrame = -1;
}

void
CMaterialViewerFrame::OnAnimStop()
{
	if (m_Preview == nullptr) {
		return;
	}
	m_Preview->Play_Animation(false);
	m_Preview->Seek_Animation(0.0F);
	m_LastAnimFrame = -1;
}

void
CMaterialViewerFrame::OnAnimSeek(int frame)
{
	if (m_Preview != nullptr) {
		m_Preview->Seek_Animation((float)frame);
	}
}

void CMaterialViewerFrame::AnimSelectedThunk(int index)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnAnimSelected(index);
	}
}

void CMaterialViewerFrame::AnimPlayPauseThunk()
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnAnimPlayPause();
	}
}

void CMaterialViewerFrame::AnimStopThunk()
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnAnimStop();
	}
}

void CMaterialViewerFrame::AnimSeekThunk(int frame)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnAnimSeek(frame);
	}
}

////////////////////////////////////////////////////////////////////////////
//
//	Preview model selection: the pane shows the mesh selected in the material
//	panel unless the View > Show Full Object toggle is on.
//
void
CMaterialViewerFrame::UpdatePreviewModel()
{
	if (m_Preview == nullptr) {
		return;
	}

	MaterialViewerTab &tab = Active();

	bool full = (tab.showFullObject && !tab.topLevelName.empty());
	const std::string &name = full ? tab.topLevelName : tab.currentMeshName;

	if (!name.empty() && m_Preview->LoadModel(name.c_str()) == false) {
		// Fall back to the other candidate before giving up silently. A skinned
		// mesh lands here, because the pane only draws it on its full object.
		const std::string &other = full ? tab.currentMeshName : tab.topLevelName;
		if (!other.empty() && m_Preview->LoadModel(other.c_str())) {
			full = !full;
		}
	}

	// A freshly loaded render object comes from the pristine prototype; if the
	// user has unsaved edits, re-stamp them so switching meshes / toggling the
	// full-object view keeps showing the in-progress material changes. The
	// panel's latest edits live in livePreviewDoc (document only updates on
	// save), so prefer it while dirty.
	if (tab.dirty) {
		m_Preview->ApplyLiveMaterials(tab.livePreviewDoc);
	}

	// In the full-object view, outline the selected mesh so the user can see
	// which part of the object the panel is editing.
	m_Preview->Set_Highlight_Mesh((full && !tab.currentMeshName.empty())
		? tab.currentMeshName.c_str() : nullptr);

	// The model (and so the ground plane's default height/range) may have changed.
	SyncGroundSlider();

	// Re-list the animations for the (possibly different) previewed hierarchy.
	RefreshAnimBar();
}

void
CMaterialViewerFrame::OnShowFullObject()
{
	Active().showFullObject = !Active().showFullObject;
	// Animations pose the full hierarchy; leaving the full-object view stops
	// playback (the single mesh has no skeleton to animate).
	if (!Active().showFullObject && m_Preview != nullptr) {
		m_Preview->Play_Animation(false);
	}
	UpdatePreviewModel();
}

void
CMaterialViewerFrame::OnUpdateShowFullObject(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(Active().showFullObject ? 1 : 0);
}

// View > Tint Selected Mesh: highlight the selected sub-object by tinting its
// triangles with the selection colour instead of the bounding box. Persisted.
void
CMaterialViewerFrame::OnTintSelection()
{
	if (m_Preview == nullptr) {
		return;
	}
	bool on = !m_Preview->Get_Tint_Highlight();
	m_Preview->Set_Tint_Highlight(on);
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerTintSelection", on ? 1 : 0);
}

void
CMaterialViewerFrame::OnUpdateTintSelection(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr && m_Preview->Get_Tint_Highlight()) ? 1 : 0);
}

void
CMaterialViewerFrame::OnPanelMeshSelected(const char *meshName)
{
	if (meshName == nullptr || meshName[0] == '\0') {
		return;
	}
	// Ignore the callback that fires while ActivateTab programmatically sets the
	// document/selection — otherwise it would overwrite the tab's remembered mesh
	// (and re-run the cross-tab sync) with the panel's default index.
	if (m_RestoringSelection) {
		return;
	}
	Active().currentMeshName = meshName;
	if (!Active().showFullObject) {
		UpdatePreviewModel();
	} else if (m_Preview != nullptr) {
		// Full-object view: the displayed object is unchanged, only the
		// selection outline moves to the newly selected mesh.
		m_Preview->Set_Highlight_Mesh(meshName);
	}

	// Batch Select: mirror this selection (by sub-object name) into the other
	// tabs so switching tabs stays on the same sub-object.
	if (m_BatchSelect) {
		SyncSelectedMeshAcrossTabs(meshName);
	}
}

void
CMaterialViewerFrame::PanelMeshSelectedThunk(const char *meshName)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelMeshSelected(meshName);
	}
}

////////////////////////////////////////////////////////////////////////////
//	Edit mode
////////////////////////////////////////////////////////////////////////////

void
CMaterialViewerFrame::UpdateTitle()
{
	MaterialViewerTab &tab = Active();

	CString title;
	if (!HasTabs()) {
		title = "W3D Material Viewer";
	} else if (tab.sourceFilePath.empty()) {
		// Asset opened from the tree whose .w3d could not be located on disk.
		title.Format("W3D Material Viewer - %s (source file not found)", tab.topLevelName.c_str());
	} else {
		title.Format("W3D Material Viewer - %s", TabLabel(tab).c_str());
	}
	SetWindowText(title);

	// Keep the active tab's caption (and its dirty '*') in sync too.
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr && m_ActiveTab >= 0 && m_ActiveTab < (int)m_Tabs.size()) {
		W3dMaterialViewer::SetTabBarLabel(m_ActiveTab, TabLabel(tab).c_str());
	}
#endif
}

// The panel runs this before entering edit mode: refuse to edit any file we
// cannot reproduce byte-for-byte through the generic chunk tree.
bool
CMaterialViewerFrame::RunEditGate()
{
	if (Active().sourceFilePath.empty()) {
		::MessageBox(m_hWnd,
			"This asset's source .w3d file could not be located on disk, so it cannot be edited.",
			"W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	std::string error;
	if (!W3dMaterialViewer::ChunkTreeRoundTripsIdentically(Active().sourceFilePath.c_str(), error)) {
		CString message;
		message.Format("This file cannot be edited safely:\n\n%s\n\n"
			"Editing is disabled to avoid corrupting it.", error.c_str());
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	return true;
}

bool
CMaterialViewerFrame::SaveDocument(const W3dMaterialViewer::MaterialDocument &document)
{
	// Snapshot the pristine baseline (last saved/loaded state) BEFORE we sync
	// Active().document to the edit below. Batch edit diffs baseline vs edited to
	// propagate only the fields the user actually changed. Copy is cheap relative
	// to a disk save and only taken when batch mode is on with >1 file.
	W3dMaterialViewer::MaterialDocument baseline;
	const bool wantBatch = (m_BatchEdit && m_Tabs.size() > 1);
	if (wantBatch) {
		baseline = Active().document;
	}

	std::string error;
	if (!W3dMaterialViewer::SaveMaterialDocument(document, error)) {
		CString message;
		message.Format("The file could not be saved:\n\n%s", error.c_str());
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	// Keep our working copy in sync with what is now on disk.
	Active().document = document;
	Active().dirty = false;

	ReloadAssetsForPreview();
	UpdateTitle();

	// Batch edit: fan ONLY the changed fields out to the other open files.
	if (wantBatch) {
		int filesTouched = 0;
		int meshesChanged = BatchPropagate(baseline, document, filesTouched);
		CString message;
		if (meshesChanged > 0) {
			message.Format("Batch edit applied.\n\n%d mesh%s updated across %d other file%s.\n\n%s",
				meshesChanged, (meshesChanged == 1) ? "" : "es",
				filesTouched, (filesTouched == 1) ? "" : "s",
				(LPCTSTR)m_BatchReport);
		} else {
			message = "Batch edit: no meshes were updated in the other open files.\n\n";
			message += m_BatchReport;
		}
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONINFORMATION | MB_OK);
	}
	return true;
}

// Applies the just-saved active document's per-mesh edits to every OTHER open
// tab's on-disk file. For each other file: parse it fresh, and for each of its
// meshes whose sub-object name matches one in the saved document AND is
// structurally compatible (same pass/material/shader/texture counts), overwrite
// that mesh's material payload; write the file back when at least one mesh
// changed. SaveMaterialDocument still applies its own round-trip + post-write
// validation gate, so a file it cannot reproduce exactly is never modified.
// Every per-file outcome (updated / partially skipped / rejected with reason) is
// recorded in m_BatchReport for the summary popup. Returns total meshes changed;
// sets `filesTouched` to the count of files actually rewritten.
int
CMaterialViewerFrame::BatchPropagate(const W3dMaterialViewer::MaterialDocument &baseline,
	const W3dMaterialViewer::MaterialDocument &edited, int &filesTouched)
{
	filesTouched = 0;
	int totalMeshesChanged = 0;
	m_BatchReport.Empty();

	// Build the "what changed" section once: the edit is global, so the same
	// MeshName > Property list applies to every target file. Diff each edited
	// mesh against its baseline twin into a throwaway copy (Apply_Mesh_Deltas
	// mutates its dst) purely to harvest the changed-field names.
	CString changesReport;
	for (size_t e = 0; e < edited.meshes.size() && e < baseline.meshes.size(); e++) {
		std::vector<std::string> fields;
		W3dMaterialViewer::MeshMaterialData scratch = edited.meshes[e];
		Apply_Mesh_Deltas(baseline.meshes[e], edited.meshes[e], scratch, &fields);
		for (const std::string &field : fields) {
			changesReport += "  ";
			changesReport += edited.meshes[e].meshName.c_str();
			changesReport += " > ";
			changesReport += field.c_str();
			changesReport += "\n";
		}
	}
	if (!changesReport.IsEmpty()) {
		m_BatchReport += "Changes propagated:\n";
		m_BatchReport += changesReport;
		m_BatchReport += "\nFiles:\n";
	}

	for (int i = 0; i < (int)m_Tabs.size(); i++) {
		if (i == m_ActiveTab) {
			continue;	// the active file was already saved
		}
		const std::string &path = m_Tabs[i].sourceFilePath;

		// Short display name (file name only) for the per-file report line.
		const char *slash = path.empty() ? nullptr : ::strrchr(path.c_str(), '\\');
		const char *fwd = path.empty() ? nullptr : ::strrchr(path.c_str(), '/');
		if (fwd != nullptr && (slash == nullptr || fwd > slash)) {
			slash = fwd;
		}
		CString shortName = path.empty() ? "(no file)"
			: (slash ? slash + 1 : path.c_str());

		if (path.empty()) {
			m_BatchReport += "  - " + shortName + ": no on-disk file, skipped\n";
			continue;
		}

		// Parse a fresh copy from disk so we edit exactly the file's own bytes.
		W3dMaterialViewer::MaterialDocument target;
		if (!W3dMaterialViewer::ParseMaterialDocument(path.c_str(), target)) {
			m_BatchReport += "  - " + shortName + ": could not be parsed, skipped\n";
			continue;
		}

		int changed = 0;
		int incompatible = 0;
		for (W3dMaterialViewer::MeshMaterialData &targetMesh : target.meshes) {
			// Find the edited mesh (and its baseline twin) by sub-name. baseline
			// and edited share order and structure (same file), so index e maps
			// to the same mesh in both.
			for (size_t e = 0; e < edited.meshes.size(); e++) {
				if (!Mesh_Names_Match(targetMesh.meshName, edited.meshes[e].meshName)) {
					continue;
				}
				if (e >= baseline.meshes.size()) {
					break;	// no baseline twin (unusual); nothing to diff against
				}
				// Only apply if the target mesh is structurally compatible with the
				// edited one, so the changed-field indices line up 1:1.
				if (!Mesh_Structures_Compatible(edited.meshes[e], targetMesh)) {
					incompatible++;
					break;
				}
				if (Apply_Mesh_Deltas(baseline.meshes[e], edited.meshes[e], targetMesh, nullptr)) {
					changed++;
				}
				break;	// first matching edited mesh wins
			}
		}
		if (changed == 0) {
			if (incompatible > 0) {
				CString line;
				line.Format("  - %s: %d mesh%s matched by name but differ in structure, skipped\n",
					(LPCTSTR)shortName, incompatible, (incompatible == 1) ? "" : "es");
				m_BatchReport += line;
			} else {
				m_BatchReport += "  - " + shortName + ": no changed meshes matched, skipped\n";
			}
			continue;
		}

		std::string save_error;
		if (W3dMaterialViewer::SaveMaterialDocument(target, save_error)) {
			totalMeshesChanged += changed;
			filesTouched++;

			CString line;
			if (incompatible > 0) {
				line.Format("  - %s: %d mesh%s updated (%d skipped: structure differs)\n",
					(LPCTSTR)shortName, changed, (changed == 1) ? "" : "es", incompatible);
			} else {
				line.Format("  - %s: %d mesh%s updated\n", (LPCTSTR)shortName,
					changed, (changed == 1) ? "" : "es");
			}
			m_BatchReport += line;

			// If this file is an open tab, keep its cached document in sync and
			// drop its stale prototypes so a later switch reloads fresh bytes.
			m_Tabs[i].document = target;
			WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
			if (assets != nullptr) {
				for (const W3dMaterialViewer::MeshMaterialData &mesh : target.meshes) {
					assets->Remove_Prototype(mesh.meshName.c_str());
				}
				if (!target.topLevelName.empty()) {
					assets->Remove_Prototype(target.topLevelName.c_str());
				}
			}
		} else {
			CString line;
			line.Format("  - %s: %d matched but the write was rejected (%s)\n",
				(LPCTSTR)shortName, changed,
				save_error.empty() ? "unknown reason" : save_error.c_str());
			m_BatchReport += line;
		}
	}

	return totalMeshesChanged;
}

void
CMaterialViewerFrame::RevertDocument()
{
	MaterialViewerTab &tab = Active();
	if (tab.sourceFilePath.empty()) {
		return;
	}

	W3dMaterialViewer::MaterialDocument fresh;
	if (!W3dMaterialViewer::ParseMaterialDocument(tab.sourceFilePath.c_str(), fresh)) {
		return;
	}

	tab.document = fresh;
	Fill_Texture_Previews(tab.document);
	tab.dirty = false;

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(tab.document);
	}
#endif

	// The live preview still carries the reverted (edited) materials. Rebuild the
	// render object from the untouched prototype so it returns to the original
	// look; the tab is now clean so UpdatePreviewModel won't re-stamp any edits.
	UpdatePreviewModel();

	UpdateTitle();
}

// Force the asset manager to drop this file's prototypes and textures so the
// next load rebuilds them from the freshly saved bytes (Load_3D_Assets skips
// prototypes whose names already exist). The main viewport is paused while the
// viewer is open, so removing prototypes cannot disturb a live render.
void
CMaterialViewerFrame::ReloadAssetsForPreview()
{
	MaterialViewerTab &tab = Active();
	if (tab.sourceFilePath.empty()) {
		return;
	}

	// Release the preview's current render object first (it holds a ref that
	// would otherwise keep the old prototype's data alive).
	if (m_Preview != nullptr) {
		m_Preview->UnloadModel();
	}

	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	if (assets != nullptr) {
		for (const W3dMaterialViewer::MeshMaterialData &mesh : tab.document.meshes) {
			assets->Remove_Prototype(mesh.meshName.c_str());
		}
		if (!tab.document.topLevelName.empty()) {
			assets->Remove_Prototype(tab.document.topLevelName.c_str());
		}
		assets->Release_Unused_Textures();
	}

	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->LoadAssetsFromFile(tab.sourceFilePath.c_str());
	}

	UpdatePreviewModel();
}

bool
CMaterialViewerFrame::PromptSaveIfDirty()
{
#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd == nullptr || !W3dMaterialViewer::PanelHasUnsavedChanges()) {
		return true;
	}

	int choice = ::MessageBox(m_hWnd,
		"Save the material changes before continuing?",
		"W3D Material Viewer", MB_ICONWARNING | MB_YESNOCANCEL);
	if (choice == IDCANCEL) {
		return false;
	}
	if (choice == IDYES) {
		return W3dMaterialViewer::RequestPanelSave();
	}
	// IDNO: discard by reloading the pristine file.
	RevertDocument();
	return true;
#else
	return true;
#endif
}

bool
CMaterialViewerFrame::EditGateThunk()
{
	return (_TheInstance != nullptr) && _TheInstance->RunEditGate();
}

bool
CMaterialViewerFrame::SaveThunk(const W3dMaterialViewer::MaterialDocument &document)
{
	return (_TheInstance != nullptr) && _TheInstance->SaveDocument(document);
}

void
CMaterialViewerFrame::RevertThunk()
{
	if (_TheInstance != nullptr) {
		_TheInstance->RevertDocument();
	}
}

void
CMaterialViewerFrame::OnPanelDirtyChanged(bool dirty)
{
	Active().dirty = dirty;
	UpdateTitle();
}

void
CMaterialViewerFrame::DirtyChangedThunk(bool dirty)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelDirtyChanged(dirty);
	}
}

// Live-preview: stash the latest edited document and (re)arm the debounce
// timer. Coalescing avoids re-cloning materials on every spin-box tick.
void
CMaterialViewerFrame::OnPanelLivePreview(const W3dMaterialViewer::MaterialDocument &document)
{
	if (m_Preview == nullptr || !::IsWindow(m_hWnd)) {
		return;
	}
	Active().livePreviewDoc = document;
	m_LivePreviewPending = true;
	SetTimer(LIVE_PREVIEW_TIMER, LIVE_PREVIEW_DELAY_MS, nullptr);	// restart
}

void
CMaterialViewerFrame::LivePreviewThunk(const W3dMaterialViewer::MaterialDocument &document)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelLivePreview(document);
	}
}

void
CMaterialViewerFrame::OnTimer(UINT_PTR event_id)
{
	if (event_id == LIVE_PREVIEW_TIMER) {
		KillTimer(LIVE_PREVIEW_TIMER);
		if (m_LivePreviewPending && m_Preview != nullptr) {
			m_LivePreviewPending = false;
			m_Preview->ApplyLiveMaterials(Active().livePreviewDoc);
		}
		return;
	}
	CFrameWnd::OnTimer(event_id);
}

// Re-decodes a single texture thumbnail when its filename is edited. Uses the
// same host-side decoder as the initial parse (D3D device + file factory).
void
CMaterialViewerFrame::ResolveTextureThunk(W3dMaterialViewer::TextureData &texture)
{
	Build_Texture_Preview(texture);
}

void
CMaterialViewerFrame::ApplyPanelTheme()
{
#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::PanelTheme theme;
		theme.dark = W3DDarkMode::IsDark();
		if (theme.dark) {
			theme.background = W3DDarkMode::GetDlgBackgroundColor();
			theme.ctrlBackground = W3DDarkMode::GetCtrlBackgroundColor();
			theme.text = W3DDarkMode::GetTextColor();
			theme.edge = W3DDarkMode::GetEdgeColor();
		}
		W3dMaterialViewer::ApplyPanelTheme(theme);
	}
#endif
}

LRESULT
CMaterialViewerFrame::OnThemeChanged(WPARAM wparam, LPARAM lparam)
{
	W3DDarkMode::ApplyToWindow(m_hWnd);
	ApplyPanelTheme();
	ApplyToolBarTheme();
	if (m_Preview != nullptr && ::IsWindow(m_Preview->m_hWnd)) {
		m_Preview->Invalidate();
	}
	return 0;
}
