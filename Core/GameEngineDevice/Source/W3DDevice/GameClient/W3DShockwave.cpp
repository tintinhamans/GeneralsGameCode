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

// W3DShockwave.cpp ///////////////////////////////////////////////////////////////////////////////
// Expanding rings that bend a copy of the finished scene
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "WWLib/always.h"
#include "W3DDevice/GameClient/W3DShockwave.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GlobalData.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8fvf.h"
#include "WW3D2/formconv.h"
#include "WW3D2/shader.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/sortingrenderer.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/ww3d.h"

W3DShockwaveManager *TheW3DShockwaves = nullptr;

// StrengthInPixels counts pixels on a screen this many lines tall, so it looks the same at any resolution.
static const Real REFERENCE_SCREEN_LINES = 1080.0f;

// A water ring sits this high above the surface so it clears water that writes depth.
static const Real WATER_RING_LIFT = 0.5f;

// Where a world position lands in the scene copy, and its clip-space w.
static Vector3 Scene_UV(const Vector3 &world, const D3DMATRIX &viewProjection, const Vector4 &screenMap)
{
	const D3DMATRIX &m = viewProjection;
	const Real x = world.X * m.m[0][0] + world.Y * m.m[1][0] + world.Z * m.m[2][0] + m.m[3][0];
	const Real y = world.X * m.m[0][1] + world.Y * m.m[1][1] + world.Z * m.m[2][1] + m.m[3][1];
	const Real w = world.X * m.m[0][3] + world.Y * m.m[1][3] + world.Z * m.m[2][3] + m.m[3][3];
	const Real safeW = (fabs(w) > 1e-6f) ? w : 1e-6f;
	return Vector3(x / safeW * screenMap.X + screenMap.Z, y / safeW * screenMap.Y + screenMap.W, w);
}

W3DShockwaveManager::W3DShockwaveManager()
	: m_count(0),
	  m_sceneCopy(nullptr),
	  m_shader(0),
	  m_loaded(FALSE)
{
}

W3DShockwaveManager::~W3DShockwaveManager()
{
	ReleaseResources();
}

// The shader goes too, so a device made anew gets one of its own.
void W3DShockwaveManager::ReleaseResources()
{
	if (m_sceneCopy != nullptr)
	{
		m_sceneCopy->Release();
		m_sceneCopy = nullptr;
	}

	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device != nullptr)
	{
		DX8_DELETE_PIXEL_SHADER(device, m_shader);
	}
	m_shader = 0;
	m_loaded = FALSE;
}

void W3DShockwaveManager::add(const Coord3D &position, Real radius, Real width, Real strength, Bool strengthInPixels, UnsignedInt durationMs, Int rings, Bool onWater)
{
	if (radius <= 0.0f || width <= 0.0f || durationMs == 0)
	{
		return;
	}

	// A full list gives up its oldest ring.
	if (m_count == MAX_SHOCKWAVES)
	{
		for (Int i = 1; i < m_count; i++)
		{
			m_shockwaves[i - 1] = m_shockwaves[i];
		}
		m_count--;
	}

	Shockwave &shockwave = m_shockwaves[m_count++];
	shockwave.position = position;
	shockwave.radius = radius;
	shockwave.width = width;
	shockwave.strength = strength;
	shockwave.strengthInPixels = strengthInPixels;
	shockwave.startMs = WW3D::Get_Sync_Time();
	shockwave.durationMs = durationMs;
	shockwave.rings = (rings > 1) ? rings : 1;
	shockwave.onWater = onWater;
}

void W3DShockwaveManager::render(RenderInfoClass &rinfo)
{
#if defined(BUILD_WITH_D3D9)
	const UnsignedInt now = WW3D::Get_Sync_Time();
	Int live = 0;
	for (Int i = 0; i < m_count; i++)
	{
		if (now - m_shockwaves[i].startMs < m_shockwaves[i].durationMs)
		{
			m_shockwaves[live++] = m_shockwaves[i];
		}
	}
	m_count = live;

	if (m_count == 0 || !TheGlobalData->m_useHeatEffects)
	{
		return;
	}

	if (!m_loaded)
	{
		m_loaded = TRUE;
		const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
		if (caps != nullptr && caps->Get_Pixel_Shader_Major_Version() >= 2 &&
			FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\shockwave.pso", nullptr, 0, false, &m_shader)))
		{
			m_shader = 0;
		}
	}
	if (m_shader == 0)
	{
		return;
	}

	// The rings bend whatever is drawn, particles included.
	SortingRendererClass::Flush();
	if (!W3DShaderManager::copyRenderTarget(m_sceneCopy))
	{
		return;
	}

	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	rinfo.Camera.Apply();

	// Apply defers the view to the next state flush, so it is read from the camera, not the device.
	const D3DMATRIX view = To_D3DMATRIX(rinfo.Camera.Get_View_Matrix());
	D3DMATRIX projection;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projection);
	const D3DMATRIX viewProjection = view * projection;

	D3DSURFACE_DESC copyDesc;
	m_sceneCopy->GetLevelDesc(0, &copyDesc);
	const Vector4 screenMap = W3DShaderManager::getClipToTargetMapping((Real)copyDesc.Width, (Real)copyDesc.Height);
	const Real sceneAspect = (Real)copyDesc.Width / (Real)copyDesc.Height;

	Vector3 cameraRight;
	rinfo.Camera.Get_Transform().Get_X_Vector(&cameraRight);

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, identity);
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_Texture(1, nullptr);
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(material);
	REF_PTR_RELEASE(material);
	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaque2DShader);

	DynamicVBAccessClass vbAccess(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, m_count * 4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vbAccess);
		VertexFormatXYZNDUV2 *verts = lock.Get_Formatted_Vertex_Array();
		static const Real corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
		for (Int i = 0; i < m_count; i++)
		{
			const Shockwave &shockwave = m_shockwaves[i];
			const Real halfSize = shockwave.radius + shockwave.width;
			for (Int c = 0; c < 4; c++)
			{
				verts->x = shockwave.position.x + corners[c][0] * halfSize;
				verts->y = shockwave.position.y + corners[c][1] * halfSize;
				verts->z = shockwave.onWater ? shockwave.position.z + WATER_RING_LIFT : shockwave.position.z;
				verts->nx = 0.0f;
				verts->ny = 0.0f;
				verts->nz = 1.0f;
				verts->diffuse = 0xffffffff;
				verts->u1 = corners[c][0];
				verts->v1 = corners[c][1];
				verts->u2 = 0.0f;
				verts->v2 = 0.0f;
				verts++;
			}
		}
	}

	DynamicIBAccessClass ibAccess(BUFFER_TYPE_DYNAMIC_DX8, m_count * 6);
	{
		DynamicIBAccessClass::WriteLockClass lock(&ibAccess);
		UnsignedShort *indices = lock.Get_Index_Array();
		for (Int i = 0; i < m_count; i++)
		{
			const UnsignedShort base = (UnsignedShort)(i * 4);
			indices[i * 6 + 0] = base + 0;
			indices[i * 6 + 1] = base + 1;
			indices[i * 6 + 2] = base + 2;
			indices[i * 6 + 3] = base + 2;
			indices[i * 6 + 4] = base + 1;
			indices[i * 6 + 5] = base + 3;
		}
	}

	DX8Wrapper::Set_Vertex_Buffer(vbAccess);
	DX8Wrapper::Set_Index_Buffer(ibAccess, 0);
	DX8Wrapper::Apply_Render_State_Changes();

	// Air rings ignore depth, and the shore and anything in front hide water rings. No shader restores ZENABLE, so it is put back after.
	DWORD depthTest = D3DZB_TRUE;
	device->GetRenderState(D3DRS_ZENABLE, &depthTest);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);

	device->SetTexture(0, m_sceneCopy);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

	// Clip space comes through stage 1, from camera space by the projection.
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, projection);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT4);

	DX8Wrapper::Set_Pixel_Shader(m_shader);
	DX8Wrapper::Set_Pixel_Shader_Constant(2, &screenMap, 1);

	for (Int i = 0; i < m_count; i++)
	{
		const Shockwave &shockwave = m_shockwaves[i];
		const Real age = (Real)(now - shockwave.startMs) / (Real)shockwave.durationMs;
		const Real halfSize = shockwave.radius + shockwave.width;

		// One world unit at the centre, measured across the screen, turns the strength into scene uv.
		const Vector3 center(shockwave.position.x, shockwave.position.y, shockwave.position.z);
		const Vector3 centerUV = Scene_UV(center, viewProjection, screenMap);
		const Vector3 besideUV = Scene_UV(center + cameraRight, viewProjection, screenMap);
		if (centerUV.Z <= 0.0f)
		{
			continue;
		}
		const Real uvPerUnit = sqrt((besideUV.X - centerUV.X) * (besideUV.X - centerUV.X) + (besideUV.Y - centerUV.Y) * (besideUV.Y - centerUV.Y));

		const Real push = shockwave.strengthInPixels ? shockwave.strength / REFERENCE_SCREEN_LINES : shockwave.strength * uvPerUnit;
		const Real fade = (1.0f - age) * (1.0f - age);
		const Vector4 ring(age * shockwave.radius / halfSize, halfSize / shockwave.width, push * fade, 0.0f);
		// A pixel push is measured in scene heights, with u scaled to match, so it bends the same pixels every way.
		const Real uScale = shockwave.strengthInPixels ? sceneAspect : 1.0f;
		const Vector4 ringCenter(centerUV.X, centerUV.Y, uScale, 1.0f / uScale);
		const Vector4 shape((Real)shockwave.rings, shockwave.rings > 1 ? 1.0f : 0.0f, 0.0f, 0.0f);
		DX8Wrapper::Set_Pixel_Shader_Constant(0, &ring, 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(1, &ringCenter, 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(3, &shape, 1);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, shockwave.onWater ? D3DZB_TRUE : D3DZB_FALSE);
		DX8Wrapper::Draw_Triangles(i * 6, 2, i * 4, 4);
	}

	DX8Wrapper::Set_Pixel_Shader(0);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, depthTest);
	device->SetTexture(0, nullptr);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 1);
	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	DX8Wrapper::Set_Index_Buffer(nullptr, 0);

	// Z, blend, cull and fog are ShaderClass state, so the next shader set restores them in full.
	ShaderClass::Invalidate();
#endif
}
