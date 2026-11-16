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

#include "StdAfx.h"

#include "ViewportOverlay.h"

#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/shader.h"
#include "WW3D2/vertmaterial.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace
{

// Cube layout, in pixels
const float CUBE_HALF = 26.0F;
const float CUBE_INSET = 58.0F;

// Face coordinate where the edge and corner regions begin
const float EDGE_SPLIT = 0.55F;

const DWORD SNAP_MS = 180;
const int DRAG_START_PIXELS = 3;
const int ORBIT_GAIN = 3;

const int LABEL_CELL = 64;
const int ATLAS_WIDTH = 512;

const int STATS_WIDTH = 256;
const int STATS_HEIGHT = 64;
const int STATS_LINE_HEIGHT = 20;
const float STATS_LEFT = 10.0F;
const float STATS_TOP = 8.0F;
const DWORD STATS_REFRESH_MS = 250;

const DWORD COLOR_HOVER = 0xFF5096EB;
const DWORD COLOR_EDGE = 0xFF464646;
const DWORD COLOR_LABEL = 0xFF1E1E1E;
const DWORD COLOR_STATS = 0xFFFFE15A;
const DWORD COLOR_SHADOW = 0xFF000000;

// right and up are the label's axes, as seen by a camera facing the side
struct CubeFace
{
	Vector3		normal;
	Vector3		right;
	Vector3		up;
	const char	*label;
};

// Front is +X, matching Camera > Front
const CubeFace FACES[6] =
{
	{ Vector3(1.0F, 0.0F, 0.0F),	Vector3(0.0F, 1.0F, 0.0F),	Vector3(0.0F, 0.0F, 1.0F),	"Front" },
	{ Vector3(-1.0F, 0.0F, 0.0F),	Vector3(0.0F, -1.0F, 0.0F),	Vector3(0.0F, 0.0F, 1.0F),	"Back" },
	{ Vector3(0.0F, -1.0F, 0.0F),	Vector3(1.0F, 0.0F, 0.0F),	Vector3(0.0F, 0.0F, 1.0F),	"Left" },
	{ Vector3(0.0F, 1.0F, 0.0F),	Vector3(-1.0F, 0.0F, 0.0F),	Vector3(0.0F, 0.0F, 1.0F),	"Right" },
	{ Vector3(0.0F, 0.0F, 1.0F),	Vector3(0.0F, 1.0F, 0.0F),	Vector3(-1.0F, 0.0F, 0.0F),	"Top" },
	{ Vector3(0.0F, 0.0F, -1.0F),	Vector3(0.0F, 1.0F, 0.0F),	Vector3(1.0F, 0.0F, 0.0F),	"Bottom" },
};

// The cube is drawn orthographically, so each face is a parallelogram and maps a label exactly
struct CubeView
{
	Vector3	right;
	Vector3	up;
	Vector3	back;
	float	cx;
	float	cy;
};

struct OverlayVertex
{
	float	x, y, z, rhw;
	DWORD	color;
	float	u, v;
};

const DWORD OVERLAY_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

const D3DRENDERSTATETYPE RENDER_STATES[] =
{
	D3DRS_ALPHABLENDENABLE,
	D3DRS_SRCBLEND,
	D3DRS_DESTBLEND,
	D3DRS_ZENABLE,
	D3DRS_ZWRITEENABLE,
	D3DRS_CULLMODE,
	D3DRS_LIGHTING,
	D3DRS_FOGENABLE,
	D3DRS_ALPHATESTENABLE,
	D3DRS_FILLMODE,
};

const DWORD RENDER_VALUES[] =
{
	TRUE,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DZB_FALSE,
	FALSE,
	D3DCULL_NONE,
	FALSE,
	FALSE,
	FALSE,
	D3DFILL_SOLID,
};

const D3DTEXTURESTAGESTATETYPE STAGE_STATES[] =
{
	D3DTSS_COLOROP,
	D3DTSS_COLORARG1,
	D3DTSS_COLORARG2,
	D3DTSS_ALPHAOP,
	D3DTSS_ALPHAARG1,
	D3DTSS_ALPHAARG2,
};

const int RENDER_STATE_COUNT = sizeof(RENDER_STATES) / sizeof(RENDER_STATES[0]);
const int STAGE_STATE_COUNT = sizeof(STAGE_STATES) / sizeof(STAGE_STATES[0]);

// The device states the wrapper's cache does not put back by itself
struct OverlayStates
{
	DWORD					render[RENDER_STATE_COUNT];
	DWORD					stage[STAGE_STATE_COUNT];
	DWORD					min_filter;
	DWORD					mag_filter;
	DWORD					mip_filter;
	DWORD					fvf;
	IDirect3DBaseTexture8	*texture;
};

void Begin_Overlay(IDirect3DDevice8 *dev, OverlayStates &saved)
{
	// A wrapper pass first, so no shader or texture mapper from the scene is left bound
	ShaderClass shader = ShaderClass::_PresetOpaqueSolidShader;
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Set_Texture(0, nullptr);
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(material);
	REF_PTR_RELEASE(material);
	DX8Wrapper::Apply_Render_State_Changes();

	for (int i = 0; i < RENDER_STATE_COUNT; i++)
	{
		saved.render[i] = RENDER_VALUES[i];
		dev->GetRenderState(RENDER_STATES[i], &saved.render[i]);
		dev->SetRenderState(RENDER_STATES[i], RENDER_VALUES[i]);
	}

	for (int i = 0; i < STAGE_STATE_COUNT; i++)
	{
		saved.stage[i] = 0;
		dev->GetTextureStageState(0, STAGE_STATES[i], &saved.stage[i]);
	}

	saved.min_filter = D3DTEXF_LINEAR;
	saved.mag_filter = D3DTEXF_LINEAR;
	saved.mip_filter = D3DTEXF_NONE;
	VIEW_GET_FILTER(dev, MINFILTER, &saved.min_filter);
	VIEW_GET_FILTER(dev, MAGFILTER, &saved.mag_filter);
	VIEW_GET_FILTER(dev, MIPFILTER, &saved.mip_filter);
	VIEW_SET_FILTER(dev, MINFILTER, D3DTEXF_LINEAR);
	VIEW_SET_FILTER(dev, MAGFILTER, D3DTEXF_LINEAR);
	VIEW_SET_FILTER(dev, MIPFILTER, D3DTEXF_NONE);

	saved.fvf = 0;
	VIEW_GET_FVF(dev, &saved.fvf);
	DX8_SET_FVF(dev, OVERLAY_FVF);

	saved.texture = nullptr;
	dev->GetTexture(0, &saved.texture);

	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

// Vertex colour alone, or vertex colour with the texture's alpha as coverage
void Set_Overlay_Texture(IDirect3DDevice8 *dev, IDirect3DTexture8 *texture)
{
	dev->SetTexture(0, texture);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, (texture != nullptr) ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
}

void End_Overlay(IDirect3DDevice8 *dev, OverlayStates &saved)
{
	dev->SetTexture(0, saved.texture);
	if (saved.texture != nullptr)
	{
		saved.texture->Release();
		saved.texture = nullptr;
	}

	if (saved.fvf != 0)
	{
		DX8_SET_FVF(dev, saved.fvf);
	}

	VIEW_SET_FILTER(dev, MINFILTER, saved.min_filter);
	VIEW_SET_FILTER(dev, MAGFILTER, saved.mag_filter);
	VIEW_SET_FILTER(dev, MIPFILTER, saved.mip_filter);

	for (int i = 0; i < STAGE_STATE_COUNT; i++)
	{
		dev->SetTextureStageState(0, STAGE_STATES[i], saved.stage[i]);
	}

	for (int i = 0; i < RENDER_STATE_COUNT; i++)
	{
		dev->SetRenderState(RENDER_STATES[i], saved.render[i]);
	}

	DX8Wrapper::Invalidate_Cached_Render_States();
}

// White pixels whose alpha is the glyph coverage
bool Rasterize_Text(int width, int height, const char *const *text, const RECT *rects, int count,
	int font_height, UINT format, std::vector<DWORD> &pixels)
{
	HDC dc = ::CreateCompatibleDC(nullptr);
	if (dc == nullptr)
	{
		return false;
	}

	BITMAPINFO info;
	::ZeroMemory(&info, sizeof(info));
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (bitmap == nullptr || bits == nullptr)
	{
		if (bitmap != nullptr)
		{
			::DeleteObject(bitmap);
		}
		::DeleteDC(dc);
		return false;
	}

	const int pixel_count = width * height;
	::ZeroMemory(bits, pixel_count * 4);

	// Greyscale antialiasing, because ClearType's colour fringes are not a coverage value
	HFONT font = ::CreateFontA(-font_height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

	HGDIOBJ old_bitmap = ::SelectObject(dc, bitmap);
	HGDIOBJ old_font = ::SelectObject(dc, (font != nullptr) ? (HGDIOBJ)font : ::GetStockObject(DEFAULT_GUI_FONT));
	::SetTextColor(dc, RGB(255, 255, 255));
	::SetBkMode(dc, TRANSPARENT);

	for (int i = 0; i < count; i++)
	{
		RECT rect = rects[i];
		::DrawTextA(dc, text[i], -1, &rect, format);
	}
	::GdiFlush();

	pixels.resize(pixel_count);
	const DWORD *source = (const DWORD *)bits;
	for (int p = 0; p < pixel_count; p++)
	{
		const DWORD coverage = source[p] & 0xFF;
		pixels[p] = (coverage << 24) | 0x00FFFFFF;
	}

	::SelectObject(dc, old_font);
	::SelectObject(dc, old_bitmap);
	if (font != nullptr)
	{
		::DeleteObject(font);
	}
	::DeleteObject(bitmap);
	::DeleteDC(dc);
	return true;
}

// Made per frame, so nothing has to survive a device reset
IDirect3DTexture8 *Create_Overlay_Texture(IDirect3DDevice8 *dev, int width, int height, const DWORD *pixels)
{
	IDirect3DTexture8 *texture = nullptr;
#if defined(BUILD_WITH_D3D9)
	// D3D9Ex has no managed pool, and a dynamic texture locks in the default one
	HRESULT result = dev->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr);
#else
	HRESULT result = dev->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture);
#endif
	if (FAILED(result) || texture == nullptr)
	{
		return nullptr;
	}

	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
	{
		texture->Release();
		return nullptr;
	}

	for (int y = 0; y < height; y++)
	{
		::memcpy((BYTE *)locked.pBits + y * locked.Pitch, pixels + y * width, width * 4);
	}
	texture->UnlockRect(0);
	return texture;
}

void Set_Vertex(OverlayVertex &vertex, float x, float y, DWORD color, float u, float v)
{
	// Half a pixel back, so texel centres land on pixel centres
	vertex.x = x - 0.5F;
	vertex.y = y - 0.5F;
	vertex.z = 0.0F;
	vertex.rhw = 1.0F;
	vertex.color = color;
	vertex.u = u;
	vertex.v = v;
}

// Corners run top left, top right, bottom left, bottom right
void Add_Quad(OverlayVertex *vertices, int &count, const float *px, const float *py, DWORD color,
	float u0, float v0, float u1, float v1)
{
	const float us[4] = { u0, u1, u0, u1 };
	const float vs[4] = { v0, v0, v1, v1 };
	const int order[6] = { 0, 1, 2, 2, 1, 3 };
	for (int i = 0; i < 6; i++)
	{
		const int corner = order[i];
		Set_Vertex(vertices[count++], px[corner], py[corner], color, us[corner], vs[corner]);
	}
}

bool Get_Cube_View(HWND hwnd, const Matrix3D &camera, CubeView &view)
{
	RECT rect;
	if (!::GetClientRect(hwnd, &rect))
	{
		return false;
	}
	if ((float)rect.right < CUBE_INSET * 2.0F || (float)rect.bottom < CUBE_INSET * 2.0F)
	{
		return false;
	}

	view.right = camera.Get_X_Vector();
	view.up = camera.Get_Y_Vector();
	view.back = camera.Get_Z_Vector();
	view.cx = (float)rect.right - CUBE_INSET;
	view.cy = CUBE_INSET;
	return true;
}

void Project(const CubeView &view, const Vector3 &point, float &x, float &y)
{
	x = view.cx + CUBE_HALF * Vector3::Dot_Product(point, view.right);
	y = view.cy - CUBE_HALF * Vector3::Dot_Product(point, view.up);
}

bool Is_Face_Visible(const CubeView &view, const CubeFace &face)
{
	return Vector3::Dot_Product(face.normal, view.back) > 0.05F;
}

// a and b run -1 to 1 along the face's right and up axes
void Face_Corners(const CubeView &view, const CubeFace &face, float a0, float a1, float b0, float b1,
	float *px, float *py)
{
	Project(view, face.normal + face.right * a0 + face.up * b1, px[0], py[0]);
	Project(view, face.normal + face.right * a1 + face.up * b1, px[1], py[1]);
	Project(view, face.normal + face.right * a0 + face.up * b0, px[2], py[2]);
	Project(view, face.normal + face.right * a1 + face.up * b0, px[3], py[3]);
}

int Region_Index(float coordinate)
{
	if (coordinate < -EDGE_SPLIT)
	{
		return -1;
	}
	if (coordinate > EDGE_SPLIT)
	{
		return 1;
	}
	return 0;
}

void Region_Range(float index, float &low, float &high)
{
	if (index < -0.5F)
	{
		low = -1.0F;
		high = -EDGE_SPLIT;
	}
	else if (index > 0.5F)
	{
		low = EDGE_SPLIT;
		high = 1.0F;
	}
	else
	{
		low = -EDGE_SPLIT;
		high = EDGE_SPLIT;
	}
}

// Looks at center from the region's side. Straight up and down keep Front at the bottom and top.
Matrix3D Build_Snap_Transform(const Vector3 &region, const Vector3 &center, float distance)
{
	Vector3 back = region;
	back.Normalize();

	Vector3 up_hint(0.0F, 0.0F, 1.0F);
	if (fabs(back.Z) > 0.999F)
	{
		up_hint = (back.Z > 0.0F) ? Vector3(-1.0F, 0.0F, 0.0F) : Vector3(1.0F, 0.0F, 0.0F);
	}

	Vector3 right;
	Vector3::Normalized_Cross_Product(up_hint, back, &right);
	Vector3 up;
	Vector3::Cross_Product(back, right, &up);
	return Matrix3D(right, up, back, center + back * distance);
}

const std::vector<DWORD> &Get_Label_Atlas()
{
	static std::vector<DWORD> atlas;
	if (atlas.empty())
	{
		const char *text[6];
		RECT rects[6];
		for (int i = 0; i < 6; i++)
		{
			text[i] = FACES[i].label;
			::SetRect(&rects[i], i * LABEL_CELL, 0, (i + 1) * LABEL_CELL, LABEL_CELL);
		}
		Rasterize_Text(ATLAS_WIDTH, LABEL_CELL, text, rects, 6, 15, DT_CENTER | DT_VCENTER | DT_SINGLELINE, atlas);
	}
	return atlas;
}

void Format_Count(const char *name, int value, char *out, size_t size)
{
	char digits[16];
	const int length = ::sprintf(digits, "%d", value);

	// Thousands separators
	char grouped[24];
	int written = 0;
	for (int i = 0; i < length; i++)
	{
		if (i > 0 && ((length - i) % 3) == 0)
		{
			grouped[written++] = ',';
		}
		grouped[written++] = digits[i];
	}
	grouped[written] = '\0';

	::_snprintf(out, size, "%s: %s", name, grouped);
	out[size - 1] = '\0';
}

bool s_StatsLoaded = false;
bool s_StatsVisible = false;

struct StatsCache
{
	RenderObjClass		*obj;
	DWORD				ticks;
	int					polys;
	int					verts;
	std::vector<DWORD>	pixels;
};

StatsCache s_Stats = { nullptr, 0, -1, -1 };

}	// namespace


ViewCubeClass::ViewCubeClass() :
	m_Pressed(false),
	m_Dragging(false),
	m_PressRegion(0.0F, 0.0F, 0.0F),
	m_Snapping(false),
	m_SnapStarted(false),
	m_SnapStartTicks(0),
	m_SnapRegion(0.0F, 0.0F, 0.0F),
	m_SnapFrom(true),
	m_SnapTo(true),
	m_SnapDistance(0.0F)
{
	m_PressPoint.x = 0;
	m_PressPoint.y = 0;
	m_LastPoint.x = 0;
	m_LastPoint.y = 0;
}

Matrix3D ViewCubeClass::Get_Snap_Transform(const Vector3 &region, const Vector3 &center, float distance)
{
	return Build_Snap_Transform(region, center, distance);
}

bool ViewCubeClass::Hit_Test(HWND hwnd, const Matrix3D &camera, POINT point, Vector3 &region) const
{
	CubeView view;
	if (!Get_Cube_View(hwnd, camera, view))
	{
		return false;
	}

	for (int i = 0; i < 6; i++)
	{
		const CubeFace &face = FACES[i];
		if (!Is_Face_Visible(view, face))
		{
			continue;
		}

		// Solve point = centre + a * right + b * up in screen space
		float cx, cy;
		Project(view, face.normal, cx, cy);
		const float rx = CUBE_HALF * Vector3::Dot_Product(face.right, view.right);
		const float ry = -CUBE_HALF * Vector3::Dot_Product(face.right, view.up);
		const float ux = CUBE_HALF * Vector3::Dot_Product(face.up, view.right);
		const float uy = -CUBE_HALF * Vector3::Dot_Product(face.up, view.up);
		const float det = rx * uy - ry * ux;
		if (fabs(det) < 0.001F)
		{
			continue;
		}

		const float dx = (float)point.x - cx;
		const float dy = (float)point.y - cy;
		const float a = (dx * uy - dy * ux) / det;
		const float b = (rx * dy - ry * dx) / det;
		if (fabs(a) > 1.0F || fabs(b) > 1.0F)
		{
			continue;
		}

		region = face.normal + face.right * (float)Region_Index(a) + face.up * (float)Region_Index(b);
		return true;
	}
	return false;
}

bool ViewCubeClass::On_LButton_Down(HWND hwnd, const Matrix3D &camera, POINT point)
{
	m_Pressed = false;
	m_Dragging = false;

	Vector3 region;
	if (!Hit_Test(hwnd, camera, point, region))
	{
		return false;
	}

	m_Pressed = true;
	m_PressPoint = point;
	m_LastPoint = point;
	m_PressRegion = region;
	m_Snapping = false;
	return true;
}

bool ViewCubeClass::On_Mouse_Move(POINT point, int &orbit_dx, int &orbit_dy)
{
	orbit_dx = 0;
	orbit_dy = 0;
	if (!m_Pressed)
	{
		return false;
	}

	// The button went up while another window had the mouse
	if ((::GetKeyState(VK_LBUTTON) & 0x8000) == 0)
	{
		m_Pressed = false;
		m_Dragging = false;
		return false;
	}

	if (!m_Dragging)
	{
		const int move_x = point.x - m_PressPoint.x;
		const int move_y = point.y - m_PressPoint.y;
		if (move_x * move_x + move_y * move_y < DRAG_START_PIXELS * DRAG_START_PIXELS)
		{
			return true;
		}
		m_Dragging = true;
	}

	orbit_dx = (m_LastPoint.x - point.x) * ORBIT_GAIN;
	orbit_dy = (m_LastPoint.y - point.y) * ORBIT_GAIN;
	m_LastPoint = point;
	return true;
}

bool ViewCubeClass::On_LButton_Up()
{
	if (!m_Pressed)
	{
		return false;
	}

	if (!m_Dragging)
	{
		m_SnapRegion = m_PressRegion;
		m_Snapping = true;
		m_SnapStarted = false;
	}
	m_Pressed = false;
	m_Dragging = false;
	return true;
}

bool ViewCubeClass::Update_Snap(const Vector3 &center, const Matrix3D &camera, Matrix3D &transform)
{
	if (!m_Snapping)
	{
		return false;
	}

	const DWORD now = ::GetTickCount();
	if (!m_SnapStarted)
	{
		m_SnapStarted = true;
		m_SnapStartTicks = now;
		m_SnapDistance = (camera.Get_Translation() - center).Length();
		m_SnapFrom = Build_Quaternion(camera);
		m_SnapTo = Build_Quaternion(Build_Snap_Transform(m_SnapRegion, center, m_SnapDistance));

		// Take the short way round
		const float dot = m_SnapFrom.X * m_SnapTo.X + m_SnapFrom.Y * m_SnapTo.Y + m_SnapFrom.Z * m_SnapTo.Z + m_SnapFrom.W * m_SnapTo.W;
		if (dot < 0.0F)
		{
			m_SnapTo.X = -m_SnapTo.X;
			m_SnapTo.Y = -m_SnapTo.Y;
			m_SnapTo.Z = -m_SnapTo.Z;
			m_SnapTo.W = -m_SnapTo.W;
		}
	}

	float t = (float)(now - m_SnapStartTicks) / (float)SNAP_MS;
	if (t >= 1.0F)
	{
		transform = Build_Snap_Transform(m_SnapRegion, center, m_SnapDistance);
		m_Snapping = false;
		return true;
	}

	// Ease in and out
	t = t * t * (3.0F - 2.0F * t);

	Quaternion orientation(true);
	::Slerp(orientation, m_SnapFrom, m_SnapTo, t);
	orientation.Normalize();

	Matrix3D turned(orientation, Vector3(0.0F, 0.0F, 0.0F));
	turned.Set_Translation(center + turned.Get_Z_Vector() * m_SnapDistance);
	transform = turned;
	return true;
}

void ViewCubeClass::Render(HWND hwnd, const Matrix3D &camera)
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	CubeView view;
	if (dev == nullptr || !Get_Cube_View(hwnd, camera, view))
	{
		return;
	}

	// The pressed region stays lit until a drag starts; otherwise the one under the cursor
	Vector3 hover(0.0F, 0.0F, 0.0F);
	bool has_hover = false;
	if (m_Pressed)
	{
		hover = m_PressRegion;
		has_hover = !m_Dragging;
	}
	else if (::GetCapture() == nullptr)
	{
		POINT cursor;
		if (::GetCursorPos(&cursor) && ::WindowFromPoint(cursor) == hwnd)
		{
			::ScreenToClient(hwnd, &cursor);
			has_hover = Hit_Test(hwnd, camera, cursor, hover);
		}
	}

	// At most three faces show, each with a fill and a hover patch, a label and four edges
	OverlayVertex fills[3 * 12];
	OverlayVertex labels[3 * 6];
	OverlayVertex edges[3 * 8];
	int fill_count = 0;
	int label_count = 0;
	int edge_count = 0;

	for (int i = 0; i < 6; i++)
	{
		const CubeFace &face = FACES[i];
		if (!Is_Face_Visible(view, face) || fill_count >= 3 * 12)
		{
			continue;
		}

		float px[4];
		float py[4];
		Face_Corners(view, face, -1.0F, 1.0F, -1.0F, 1.0F, px, py);

		// Faces turned away from the camera are darker
		const float shade = 0.72F + 0.28F * Vector3::Dot_Product(face.normal, view.back);
		const DWORD grey = (DWORD)(218.0F * shade);
		Add_Quad(fills, fill_count, px, py, 0xEB000000 | (grey << 16) | (grey << 8) | grey, 0.0F, 0.0F, 0.0F, 0.0F);

		const float u0 = (float)(i * LABEL_CELL) / (float)ATLAS_WIDTH;
		const float u1 = (float)((i + 1) * LABEL_CELL) / (float)ATLAS_WIDTH;
		Add_Quad(labels, label_count, px, py, COLOR_LABEL, u0, 0.0F, u1, 1.0F);

		const int outline[8] = { 0, 1, 1, 3, 3, 2, 2, 0 };
		for (int e = 0; e < 8; e++)
		{
			Set_Vertex(edges[edge_count++], px[outline[e]], py[outline[e]], COLOR_EDGE, 0.0F, 0.0F);
		}

		// An edge or corner region lights up on every face it touches
		if (has_hover && Vector3::Dot_Product(hover, face.normal) > 0.5F)
		{
			float a0, a1, b0, b1;
			Region_Range(Vector3::Dot_Product(hover, face.right), a0, a1);
			Region_Range(Vector3::Dot_Product(hover, face.up), b0, b1);
			Face_Corners(view, face, a0, a1, b0, b1, px, py);
			Add_Quad(fills, fill_count, px, py, COLOR_HOVER, 0.0F, 0.0F, 0.0F, 0.0F);
		}
	}

	if (fill_count == 0)
	{
		return;
	}

	OverlayStates saved;
	Begin_Overlay(dev, saved);

	Set_Overlay_Texture(dev, nullptr);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, fill_count / 3, fills, sizeof(OverlayVertex));
	dev->DrawPrimitiveUP(D3DPT_LINELIST, edge_count / 2, edges, sizeof(OverlayVertex));

	const std::vector<DWORD> &atlas = Get_Label_Atlas();
	if (!atlas.empty())
	{
		IDirect3DTexture8 *texture = Create_Overlay_Texture(dev, ATLAS_WIDTH, LABEL_CELL, &atlas[0]);
		if (texture != nullptr)
		{
			Set_Overlay_Texture(dev, texture);
			dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, label_count / 3, labels, sizeof(OverlayVertex));
			dev->SetTexture(0, nullptr);
			texture->Release();
		}
	}

	End_Overlay(dev, saved);
}


bool ViewportStats::Is_Visible()
{
	if (!s_StatsLoaded)
	{
		s_StatsLoaded = true;
		s_StatsVisible = (::AfxGetApp()->GetProfileInt("Config", "ShowStats", 0) != 0);
	}
	return s_StatsVisible;
}

void ViewportStats::Toggle()
{
	s_StatsVisible = !Is_Visible();
	::AfxGetApp()->WriteProfileInt("Config", "ShowStats", s_StatsVisible ? 1 : 0);
}

bool ViewportStats::Is_Toggle_Key(const MSG *msg)
{
	if (msg == nullptr || msg->message != WM_KEYDOWN)
	{
		return false;
	}
	if (msg->wParam != '7' && msg->wParam != VK_NUMPAD7)
	{
		return false;
	}
	if ((msg->lParam & (1 << 30)) != 0)
	{
		return false;
	}
	if ((::GetKeyState(VK_CONTROL) & 0x8000) || (::GetKeyState(VK_SHIFT) & 0x8000) || (::GetKeyState(VK_MENU) & 0x8000))
	{
		return false;
	}

	// A 7 typed into an edit box is text
	char class_name[32];
	HWND focus = ::GetFocus();
	if (focus != nullptr && ::GetClassNameA(focus, class_name, sizeof(class_name)) > 0)
	{
		if (::_stricmp(class_name, "Edit") == 0 || ::_strnicmp(class_name, "RichEdit", 8) == 0)
		{
			return false;
		}
	}
	return true;
}

int ViewportStats::Count_Vertices(RenderObjClass *obj)
{
	if (obj == nullptr)
	{
		return 0;
	}

	if (obj->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshModelClass *model = static_cast<MeshClass *>(obj)->Peek_Model();
		return (model != nullptr) ? model->Get_Vertex_Count() : 0;
	}

	int total = 0;
	const int count = obj->Get_Num_Sub_Objects();
	for (int i = 0; i < count; i++)
	{
		RenderObjClass *sub = obj->Get_Sub_Object(i);
		if (sub != nullptr)
		{
			total += Count_Vertices(sub);
			sub->Release_Ref();
		}
	}
	return total;
}

void ViewportStats::Render(RenderObjClass *obj)
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
	{
		return;
	}

	// Counting walks the whole hierarchy, so it runs a few times a second
	const DWORD now = ::GetTickCount();
	if (obj != s_Stats.obj || now - s_Stats.ticks > STATS_REFRESH_MS || s_Stats.pixels.empty())
	{
		const int polys = (obj != nullptr) ? obj->Get_Num_Polys() : 0;
		const int verts = Count_Vertices(obj);
		if (polys != s_Stats.polys || verts != s_Stats.verts || s_Stats.pixels.empty())
		{
			char poly_text[48];
			char vert_text[48];
			Format_Count("Polys", polys, poly_text, sizeof(poly_text));
			Format_Count("Verts", verts, vert_text, sizeof(vert_text));

			const char *text[2] = { poly_text, vert_text };
			RECT rects[2];
			::SetRect(&rects[0], 0, 0, STATS_WIDTH, STATS_LINE_HEIGHT);
			::SetRect(&rects[1], 0, STATS_LINE_HEIGHT, STATS_WIDTH, STATS_LINE_HEIGHT * 2);
			Rasterize_Text(STATS_WIDTH, STATS_HEIGHT, text, rects, 2, 15, DT_LEFT | DT_TOP | DT_SINGLELINE, s_Stats.pixels);
		}
		s_Stats.obj = obj;
		s_Stats.ticks = now;
		s_Stats.polys = polys;
		s_Stats.verts = verts;
	}

	if (s_Stats.pixels.empty())
	{
		return;
	}

	OverlayStates saved;
	Begin_Overlay(dev, saved);

	IDirect3DTexture8 *texture = Create_Overlay_Texture(dev, STATS_WIDTH, STATS_HEIGHT, &s_Stats.pixels[0]);
	if (texture != nullptr)
	{
		// A dark copy one pixel down and right keeps the text readable on a light background
		OverlayVertex vertices[12];
		int count = 0;
		for (int pass = 0; pass < 2; pass++)
		{
			const float offset = (pass == 0) ? 1.0F : 0.0F;
			const float left = STATS_LEFT + offset;
			const float top = STATS_TOP + offset;
			const float px[4] = { left, left + (float)STATS_WIDTH, left, left + (float)STATS_WIDTH };
			const float py[4] = { top, top, top + (float)STATS_HEIGHT, top + (float)STATS_HEIGHT };
			Add_Quad(vertices, count, px, py, (pass == 0) ? COLOR_SHADOW : COLOR_STATS, 0.0F, 0.0F, 1.0F, 1.0F);
		}

		Set_Overlay_Texture(dev, texture);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices, sizeof(OverlayVertex));
		dev->SetTexture(0, nullptr);
		texture->Release();
	}

	End_Overlay(dev, saved);
}
