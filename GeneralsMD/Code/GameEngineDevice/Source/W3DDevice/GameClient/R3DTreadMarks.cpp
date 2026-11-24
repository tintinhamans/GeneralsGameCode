/*
**	Command & Conquer Generals Zero Hour(tm)
**	DX9 port: tread relief - the tread and tyre marks shaded as grooves pressed into the ground.
*/

// Ronin @feature 09/11/2026 DX9: tread marks with depth (docs/TreadMarks_Work.md). See R3DTreadMarks.h.
#include "WW3D2/dx8todx9.h"
#include "WWLib/always.h"
#include "Lib/BaseType.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8fvf.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/texture.h"
#include "WW3D2/assetmgr.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/R3DShadowMapState.h"	// Ronin @feature 10/11/2026 DX9: the sun's shadow map
#include "W3DDevice/GameClient/W3DShaderManager.h"	// Ronin @feature 10/11/2026 DX9: the ground's cloud drift
#include "W3DDevice/GameClient/R3DTreadMarks.h"
#include <d3dx9.h>
#include <cstring>

static IDirect3DVertexShader9	*s_vs = NULL;
static IDirect3DPixelShader9	*s_ps = NULL;
static Bool						s_triedShaders = FALSE;
static Bool						s_open     = FALSE;
// Ronin @bugfix 10/11/2026 DX9: the pieces laid this frame, for the TAA - new marks on still ground carry no other evidence.
static Bool  s_markClosed  = TRUE;	// beginRelief ends a frame's tracks: the next track starts a new set
static const float				RELIEF_STRENGTH = 1.0f;		// how strongly the sun shades the grooves (0..3)
static const float				RELIEF_GROOVE   = 0.25f;	// how much darker the rut's floor is (0..1)

// The relief map of each track texture, by name (four in the game); NULL = it has none.
enum { RELIEF_CACHE = 8 };
struct ReliefEntry { char name[64]; TextureClass *relief; };
static ReliefEntry	s_reliefs[RELIEF_CACHE];
static Int			s_reliefCount = 0;

// The device states this pass changes, and what they were.
static const D3DRENDERSTATETYPE RELIEF_STATES[] = {
	D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
	D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE,
	D3DRS_COLORWRITEENABLE };
enum { RELIEF_STATE_COUNT = sizeof(RELIEF_STATES) / sizeof(RELIEF_STATES[0]) };
static DWORD s_savedStates[RELIEF_STATE_COUNT];
static const D3DSAMPLERSTATETYPE RELIEF_SAMPLER_STATES[] = {
	D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER };
enum { RELIEF_SAMPLER_COUNT = sizeof(RELIEF_SAMPLER_STATES) / sizeof(RELIEF_SAMPLER_STATES[0]) };
static DWORD s_savedSamplers[4][RELIEF_SAMPLER_COUNT];	// s0 the relief map; s1, s2 the sun's map; s3 the clouds (Ronin @feature 10/11/2026 DX9)
static TextureClass *s_cloudTex = NULL;					// Ronin @feature 10/11/2026 DX9: the ground's cloud shadow texture (TerrainTex.cpp)

static void ensureShaders(IDirect3DDevice9 *dev)
{
	if (s_triedShaders)
		return;
	s_triedShaders = TRUE;
	DWORD *blob = W3DShaderManager::readShaderFile("shaders\\TreadRelief.vso");
	if (blob != NULL)
	{
		if (FAILED(dev->CreateVertexShader(blob, &s_vs)))
			s_vs = NULL;
		HeapFree(GetProcessHeap(), 0, blob);
	}
	blob = W3DShaderManager::readShaderFile("shaders\\TreadRelief.pso");
	if (blob != NULL)
	{
		if (FAILED(dev->CreatePixelShader(blob, &s_ps)))
			s_ps = NULL;
		HeapFree(GetProcessHeap(), 0, blob);
	}
}

// "EXTnkTrack.tga" -> its relief map "EXTnkTrack_nrm.tga" (Art/Textures), loaded once; NULL when there is none.
// Ronin @feature 10/11/2026 DX9: the 2003 cloud shadows' texture, as the sun shafts take it (R3DLocalLights); NULL when missing.
static TextureClass *cloudTexture(void)
{
	if (s_cloudTex == NULL)
		s_cloudTex = WW3DAssetManager::Get_Instance()->Get_Texture("TSCloudMed.tga");
	if (s_cloudTex != NULL && !s_cloudTex->Is_Initialized())
		s_cloudTex->Init();
	return (s_cloudTex != NULL && !s_cloudTex->Is_Missing_Texture() && s_cloudTex->Peek_D3D_Texture() != NULL) ? s_cloudTex : NULL;
}

static TextureClass *reliefFor(TextureClass *track)
{
	if (track == NULL)
		return NULL;
	const char *name = track->Get_Texture_Name().str();
	for (Int i = 0; i < s_reliefCount; ++i)
		if (_stricmp(s_reliefs[i].name, name) == 0)
			return s_reliefs[i].relief;
	if (s_reliefCount >= RELIEF_CACHE || strlen(name) >= sizeof(s_reliefs[0].name) - 8)
		return NULL;
	ReliefEntry &e = s_reliefs[s_reliefCount++];
	strcpy(e.name, name);
	e.relief = NULL;
	char file[80];
	strcpy(file, name);
	char *dot = strrchr(file, '.');
	if (dot != NULL)
		*dot = 0;
	strcat(file, "_nrm.tga");
	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	TextureClass *tex = (assets != NULL) ? assets->Get_Texture(file) : NULL;
	if (tex != NULL)
	{
		tex->Init();
		if (tex->Is_Missing_Texture())
			REF_PTR_RELEASE(tex);
	}
	e.relief = tex;
	return tex;
}

Bool R3DTreadMarks::beginRelief(DX8VertexBufferClass *vb, DX8IndexBufferClass *ib, const Matrix3D &world)
{
	s_markClosed = TRUE;	// Ronin @bugfix 10/11/2026 DX9: this frame's tracks are written (flush fills them all before the relief)
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == NULL || vb == NULL || ib == NULL || TheGlobalData == NULL)
		return FALSE;
	ensureShaders(dev);
	if (s_vs == NULL || s_ps == NULL || vb->Get_DX8_Vertex_Buffer() == NULL || ib->Get_DX8_Index_Buffer() == NULL)
		return FALSE;

	// The camera the terrain was just drawn with (TAA's jitter in it) is on the device once the wrapper's pending changes are.
	DX8Wrapper::Apply_Render_State_Changes();
	D3DMATRIX view, proj;
	dev->GetTransform(D3DTS_VIEW, &view);
	dev->GetTransform(D3DTS_PROJECTION, &proj);
	const D3DMATRIX w = To_D3DMATRIX(Matrix4x4(world));
	D3DXMATRIX wvp, wvpT;
	D3DXMatrixMultiply(&wvp, (const D3DXMATRIX *)&w, (const D3DXMATRIX *)&view);
	D3DXMatrixMultiply(&wvp, &wvp, (const D3DXMATRIX *)&proj);
	D3DXMatrixTranspose(&wvpT, &wvp);

	for (Int i = 0; i < RELIEF_STATE_COUNT; ++i)
		dev->GetRenderState(RELIEF_STATES[i], &s_savedStates[i]);
	for (DWORD st = 0; st < 4; ++st)
		for (Int i = 0; i < RELIEF_SAMPLER_COUNT; ++i)
			dev->GetSamplerState(st, RELIEF_SAMPLER_STATES[i], &s_savedSamplers[st][i]);

	DX8Wrapper::BindLayoutFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, "R3DTreadMarks::relief");
	dev->SetVertexShader(s_vs);		// AFTER the layout bind - BindLayoutFVF clears the vertex shader
	dev->SetPixelShader(s_ps);
	dev->SetStreamSource(0, vb->Get_DX8_Vertex_Buffer(), 0, sizeof(VertexFormatXYZDUV1));
	dev->SetIndices(ib->Get_DX8_Index_Buffer());
	dev->SetVertexShaderConstantF(240, (const float *)&wvpT, 4);

	// The sun as the terrain shader takes it (HeightMap.cpp: lightPos is the direction the light TRAVELS).
	const Coord3D &lp = TheGlobalData->m_terrainLightPos[0];
	float sun[4] = { -lp.x, -lp.y, -lp.z, 0.0f };
	const float len = sqrtf(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
	if (len > 1.0e-6f)
		for (Int k = 0; k < 3; ++k)
			sun[k] /= len;
	const RGBColor &dif = TheGlobalData->m_terrainDiffuse[0];
	const RGBColor &amb = TheGlobalData->m_terrainAmbient[0];
	const float sunCol[4] = { dif.red, dif.green, dif.blue, RELIEF_STRENGTH };
	const float ambCol[4] = { amb.red, amb.green, amb.blue, RELIEF_GROOVE };
	dev->SetPixelShaderConstantF(200, sun, 1);
	dev->SetPixelShaderConstantF(201, sunCol, 1);
	dev->SetPixelShaderConstantF(202, ambCol, 1);

	// Ground x 2 x output: 0.5 leaves it as it was. Depth tested as the decal, never written; colour only, not alpha.
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);		// fog would tint the 0.5 that means "unchanged"
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

	// Ronin @feature 10/11/2026 DX9: the sun's shadow map (s1 near, s2 far) as the terrain's receiver gets it: in a shadow the
	// grooves show no sunlit side (TreadRelief_ps sunVisible). No map this frame (shadow maps off) = lit, as before.
	const TerrainShadowPassState &sp = TheTerrainShadowPass;
	const Bool sunMap  = (sp.active && sp.shadowTex != NULL) ? TRUE : FALSE;
	const Bool twoMaps = (sunMap && sp.cascadeCount > 1.5f && sp.shadowTexFar != NULL) ? TRUE : FALSE;
	if (sunMap)
	{
		dev->SetPixelShaderConstantF(204, sp.lightViewProjT, 4);
		dev->SetPixelShaderConstantF(208, twoMaps ? sp.lightViewProjFarT : sp.lightViewProjT, 4);
	}
	const float mapPrm[4]  = { sunMap ? 1.0f : 0.0f, sp.depthBias, sp.texelOffset, twoMaps ? 2.0f : 1.0f };
	const float mapPrm2[4] = { twoMaps ? sp.depthBiasFar : sp.depthBias, sp.texelWorldSize,
							   twoMaps ? sp.texelWorldSizeFar : sp.texelWorldSize, 0.0f };
	dev->SetPixelShaderConstantF(212, mapPrm, 1);
	dev->SetPixelShaderConstantF(213, mapPrm2, 1);
	dev->SetTexture(1, sunMap ? sp.shadowTex : NULL);
	dev->SetTexture(2, sunMap ? (twoMaps ? sp.shadowTexFar : sp.shadowTex) : NULL);
	for (DWORD st = 1; st <= 2; ++st)
	{
		dev->SetSamplerState(st, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);		// a depth texture: the hardware's compare
		dev->SetSamplerState(st, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(st, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		dev->SetSamplerState(st, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(st, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}
	// Ronin @feature 10/11/2026 DX9: the 2003 cloud shadows (s3), mapped as the ground's (W3DShaderManager::getCloudMapState).
	TextureClass *clouds = cloudTexture();
	const Bool cloudsOn = (clouds != NULL && TheGlobalData->m_useCloudMap) ? TRUE : FALSE;
	float cloudScale = 0.0f, cloudX = 0.0f, cloudY = 0.0f;
	W3DShaderManager::getCloudMapState(&cloudScale, &cloudX, &cloudY);
	const float cloudPrm[4] = { cloudsOn ? 1.0f : 0.0f, cloudScale, cloudX, cloudY };
	dev->SetPixelShaderConstantF(214, cloudPrm, 1);
	dev->SetTexture(3, cloudsOn ? clouds->Peek_D3D_Texture() : NULL);
	dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	s_open = TRUE;
	return TRUE;
}

void R3DTreadMarks::drawRelief(TextureClass *trackTexture, Int baseVertex, Int edgeCount)
{
	if (!s_open || edgeCount < 2)
		return;
	TextureClass *relief = reliefFor(trackTexture);
	if (relief == NULL)
		return;
	if (!relief->Is_Initialized())
		relief->Init();		// a device reset invalidates the asset manager's textures; Init loads it again
	IDirect3DTexture9 *tex = relief->Peek_D3D_Texture();
	if (tex == NULL)
		return;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	dev->SetTexture(0, tex);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, baseVertex, 0, edgeCount * 2, 0, (edgeCount - 1) * 2);
}

void R3DTreadMarks::endRelief(void)
{
	if (!s_open)
		return;
	s_open = FALSE;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	dev->SetTexture(0, NULL);
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
	dev->SetTexture(3, NULL);
	dev->SetVertexShader(NULL);
	dev->SetPixelShader(NULL);
	for (Int i = 0; i < RELIEF_STATE_COUNT; ++i)
		dev->SetRenderState(RELIEF_STATES[i], s_savedStates[i]);
	for (DWORD st = 0; st < 4; ++st)
		for (Int i = 0; i < RELIEF_SAMPLER_COUNT; ++i)
			dev->SetSamplerState(st, RELIEF_SAMPLER_STATES[i], s_savedSamplers[st][i]);
	// The wrapper's caches no longer match the device; the decal's own Set_* calls follow. The camera stays: it was applied above.
	DX8Wrapper::Invalidate_Cached_Render_States();
}

void R3DTreadMarks::shutdown(void)
{
	for (Int i = 0; i < s_reliefCount; ++i)
		REF_PTR_RELEASE(s_reliefs[i].relief);
	s_reliefCount = 0;
	REF_PTR_RELEASE(s_cloudTex);	// Ronin @feature 10/11/2026 DX9
	if (s_vs != NULL)
	{
		s_vs->Release();
		s_vs = NULL;
	}
	if (s_ps != NULL)
	{
		s_ps->Release();
		s_ps = NULL;
	}
	s_triedShaders = FALSE;
}

// ---- Ronin @feature 09/11/2026 DX9: rounded tread marks (see the header) ----

enum { ROUND_MAX_STORED = 256 };	// stored: MAX_TRACK_EDGE_COUNT (200) fits

// One edge as TerrainTracksRenderObjClassSystem::flush wrote it in 2003: left u 0, right u 1, the fade in the alpha.
// Ronin @feature 10/11/2026 DX9: a pair with its own U - an empty pair breaks the strip (a turn on the spot).
static VertexFormatXYZDUV1 *roundPutPair(VertexFormatXYZDUV1 *out, const Vector3 &l, float ul, const Vector3 &r, float ur, float v,
	float fade, UnsignedInt diffuseRGB)
{
	const UnsignedInt col = diffuseRGB | ((UnsignedInt)REAL_TO_INT(fade * 255.0f) << 24);
	out->x = l.X; out->y = l.Y; out->z = l.Z; out->u1 = ul; out->v1 = v; out->diffuse = col; ++out;
	out->x = r.X; out->y = r.Y; out->z = r.Z; out->u1 = ur; out->v1 = v; out->diffuse = col; ++out;
	return out;
}

static VertexFormatXYZDUV1 *roundPutEdge(VertexFormatXYZDUV1 *out, const Vector3 &l, const Vector3 &r, float v, float fade,
	UnsignedInt diffuseRGB)
{
	const UnsignedInt col = diffuseRGB | ((UnsignedInt)REAL_TO_INT(fade * 255.0f) << 24);
	out->x = l.X; out->y = l.Y; out->z = l.Z; out->u1 = 0.0f; out->v1 = v; out->diffuse = col; ++out;
	out->x = r.X; out->y = r.Y; out->z = r.Z; out->u1 = 1.0f; out->v1 = v; out->diffuse = col; ++out;
	return out;
}

// ---- Ronin @feature 10/11/2026 DX9: the strip laid like a road (docs/TreadMarks_Work.md §13): the vehicle's centre path, its
// corners rounded with circular arcs, each edge square to it at the full width - both tread lanes stay parallel round a turn. ----

static const float	PATH_DEG       = 3.14159265f / 180.0f;
static const float	PATH_KEEP      = 1.0f;		// x spacing: a stored edge nearer the last kept one merges into it (a creep, a turn)
static const float	PATH_IN_PLACE  = 0.1f;		// x spacing: a shorter step is the vehicle standing (his dump: 0-0.5 standing)
static const float	PATH_GAP       = 2.5f;		// x spacing: a longer step is a jump - the strip breaks
static const float	PATH_ARC_STEP  = 7.5f;		// degrees of arc per edge
static const float	PATH_COS_BREAK = -0.7071f;	// cos 135: a sharper corner (backing up, a turn round) breaks the strip
enum { PATH_MAX_ARC = 18, PATH_MAX_EDGES = ROUND_MAX_STORED * (PATH_MAX_ARC + 4), PATH_MAX_TRACKS = 512, TAA_MARK_MAX = 6144 };

// An edge of the strip: its ends on the path's left and right (facing up, as 2003's), the tread pattern's place in tiles, the fade,
// the stored edge its segment starts at; cut = a run starts here; flip = the vehicle's left is the path's right (backing up).
struct PathEdge
{
	Vector3 l, r;
	float   tile, fade;
	Int     src;
	Bool    cut, flip;
};
static PathEdge s_path[PATH_MAX_EDGES];
static Int      s_pathCount = 0;
static Int      s_pathCuts  = 0;		// runs after the first: two empty pairs each
static float    s_pathHalf  = 0.0f;		// half the track's width

// One run of kept edges, for pathStroke and pathEmit: centres, the ground's rise per unit along the stored edge, segment k (k to k + 1).
struct PathRun
{
	float x[ROUND_MAX_STORED], y[ROUND_MAX_STORED], z[ROUND_MAX_STORED], gx[ROUND_MAX_STORED], gy[ROUND_MAX_STORED];
	float tile[ROUND_MAX_STORED], fade[ROUND_MAX_STORED];
	Int   src[ROUND_MAX_STORED];
	float seg[ROUND_MAX_STORED], dx[ROUND_MAX_STORED], dy[ROUND_MAX_STORED];
	float h;
	Bool  flip;
};
static PathRun s_run;

static float pathDist(const Vector3 &a, const Vector3 &b)
{
	const float dx = b.X - a.X;
	const float dy = b.Y - a.Y;
	return sqrtf(dx * dx + dy * dy);
}

// The cosine of the turn between stored edges a and b (the vehicle's facing).
static float pathTurnCos(const Vector3 *left, const Vector3 *right, Int a, Int b)
{
	const float ax = left[a].X - right[a].X;
	const float ay = left[a].Y - right[a].Y;
	const float bx = left[b].X - right[b].X;
	const float by = left[b].Y - right[b].Y;
	const float la = sqrtf(ax * ax + ay * ay);
	const float lb = sqrtf(bx * bx + by * by);
	return (la > 1.0e-6f && lb > 1.0e-6f) ? (ax * bx + ay * by) / (la * lb) : 1.0f;
}

// An edge at (cx, cy) heading (ux, uy), on segment j at s from its start. pivot: its inner end (side 1 the left, -1 the right) at (qx, qy).
static void pathEmit(float cx, float cy, float ux, float uy, Int j, float s, Bool pivot, float qx, float qy, float side, Bool cut)
{
	if (s_pathCount >= PATH_MAX_EDGES)
		return;
	const PathRun &R = s_run;
	const float f = (R.seg[j] > 1.0e-6f) ? s / R.seg[j] : 0.0f;
	const float zc = R.z[j] + (R.z[j + 1] - R.z[j]) * f;
	const float gx = R.gx[j] + (R.gx[j + 1] - R.gx[j]) * f;
	const float gy = R.gy[j] + (R.gy[j + 1] - R.gy[j]) * f;
	float lx = cx - uy * R.h;	// the path's left is (-uy, ux)
	float ly = cy + ux * R.h;
	float rx = cx + uy * R.h;
	float ry = cy - ux * R.h;
	if (pivot && side > 0.0f)
	{
		lx = qx;
		ly = qy;
	}
	else if (pivot)
	{
		rx = qx;
		ry = qy;
	}
	const Vector3 l(lx, ly, zc + gx * (lx - cx) + gy * (ly - cy));
	const Vector3 r(rx, ry, zc + gx * (rx - cx) + gy * (ry - cy));
	const float tile = R.tile[j] + (R.tile[j + 1] - R.tile[j]) * f;
	const float fade = R.fade[j] + (R.fade[j + 1] - R.fade[j]) * f;
	// V is the tile mirrored, interpolated straight between edges: an edge at each whole tile crossed keeps the pattern from folding
	if (!cut && s_pathCount > 0)
	{
		const PathEdge p = s_path[s_pathCount - 1];
		const float step = (tile >= p.tile) ? 1.0f : -1.0f;
		for (float w = (step > 0.0f) ? floorf(p.tile) + 1.0f : ceilf(p.tile) - 1.0f; (w - tile) * step < -1.0e-4f &&
			s_pathCount < PATH_MAX_EDGES - 1; w += step)
		{
			const float u = (w - p.tile) / (tile - p.tile);
			PathEdge &m = s_path[s_pathCount++];
			m = p;
			m.cut = FALSE;
			m.l = p.l + (l - p.l) * u;
			m.r = p.r + (r - p.r) * u;
			m.tile = w;
			m.fade = p.fade + (fade - p.fade) * u;
		}
	}
	PathEdge &e = s_path[s_pathCount++];
	e.l = l;
	e.r = r;
	e.tile = tile;
	e.fade = fade;
	e.src = R.src[j];
	e.cut = cut;
	e.flip = R.flip;
	if (cut && s_pathCount > 1)
		++s_pathCuts;
}

// One run of kept edges (idx): straight pieces, each corner rounded with the largest circular arc its segments leave room for, every
// edge square to the path. An arc tighter than half the width would run its inner ends backwards: they pivot on one point instead.
static void pathStroke(const Vector3 *left, const Vector3 *right, const float *tile, const float *fade, const Int *idx, Int count, float h)
{
	PathRun &R = s_run;
	R.h = h;
	Int m = 0;
	for (Int k = 0; k < count; ++k)
	{
		const Vector3 &l = left[idx[k]];
		const Vector3 &r = right[idx[k]];
		const float cx = 0.5f * (l.X + r.X);
		const float cy = 0.5f * (l.Y + r.Y);
		if (m > 0 && fabsf(cx - R.x[m - 1]) + fabsf(cy - R.y[m - 1]) < 1.0e-3f)
			continue;	// a point on the last one: no segment to lay
		const float ex = r.X - l.X;
		const float ey = r.Y - l.Y;
		const float e2 = ex * ex + ey * ey;
		const float rise = (e2 > 1.0e-6f) ? (r.Z - l.Z) / e2 : 0.0f;
		R.x[m] = cx;
		R.y[m] = cy;
		R.z[m] = 0.5f * (l.Z + r.Z);
		R.gx[m] = ex * rise;
		R.gy[m] = ey * rise;
		R.tile[m] = tile[idx[k]];
		R.fade[m] = fade[idx[k]];
		R.src[m] = idx[k];
		++m;
	}
	if (m < 2)
		return;
	for (Int k = 0; k + 1 < m; ++k)
	{
		const float ddx = R.x[k + 1] - R.x[k];
		const float ddy = R.y[k + 1] - R.y[k];
		R.seg[k] = sqrtf(ddx * ddx + ddy * ddy);
		R.dx[k] = ddx / R.seg[k];
		R.dy[k] = ddy / R.seg[k];
	}
	const Vector3 vehicleLeft = left[R.src[0]] - right[R.src[0]];
	R.flip = (R.dx[0] * vehicleLeft.Y - R.dy[0] * vehicleLeft.X < 0.0f) ? TRUE : FALSE;

	// The corners: the turn, its side, tan(turn / 2) - an arc of radius r needs r * that of each segment.
	static float ang[ROUND_MAX_STORED], sgn[ROUND_MAX_STORED], need[ROUND_MAX_STORED], t[ROUND_MAX_STORED];
	static float shareA[ROUND_MAX_STORED], shareB[ROUND_MAX_STORED];	// segment j: its start corner's part, its end corner's
	for (Int k = 0; k < m; ++k)
	{
		ang[k] = sgn[k] = need[k] = t[k] = 0.0f;
		if (k == 0 || k == m - 1)
			continue;
		float c = R.dx[k - 1] * R.dx[k] + R.dy[k - 1] * R.dy[k];
		c = (c > 1.0f) ? 1.0f : ((c < -1.0f) ? -1.0f : c);
		ang[k] = acosf(c);
		sgn[k] = (R.dx[k - 1] * R.dy[k] - R.dy[k - 1] * R.dx[k] > 0.0f) ? 1.0f : -1.0f;
		need[k] = (ang[k] > 0.5f * PATH_DEG) ? tanf(0.5f * ang[k]) : 0.0f;
	}
	// Each segment shared by its two corners: by need (both arcs one radius) when that radius clears half the width; else the
	// gentler corner first takes a radius of h (it stays clean) and the sharper one the rest.
	for (Int j = 0; j + 1 < m; ++j)
	{
		const float na = need[j];
		const float nb = need[j + 1];
		shareA[j] = shareB[j] = 0.0f;
		if (na + nb <= 0.0f)
			continue;
		if (na <= 0.0f || nb <= 0.0f || R.seg[j] >= h * (na + nb))
			shareA[j] = R.seg[j] * na / (na + nb);
		else if (na <= nb)
			shareA[j] = (h * na < R.seg[j]) ? h * na : R.seg[j];
		else
			shareA[j] = R.seg[j] - ((h * nb < R.seg[j]) ? h * nb : R.seg[j]);
		shareB[j] = R.seg[j] - shareA[j];
	}
	for (Int k = 1; k + 1 < m; ++k)
		if (need[k] > 0.0f)
			t[k] = (shareB[k - 1] < shareA[k]) ? shareB[k - 1] : shareA[k];

	pathEmit(R.x[0], R.y[0], R.dx[0], R.dy[0], 0, 0.0f, FALSE, 0.0f, 0.0f, 0.0f, TRUE);
	for (Int k = 1; k + 1 < m; ++k)
	{
		const float d1x = R.dx[k - 1];
		const float d1y = R.dy[k - 1];
		const float d2x = R.dx[k];
		const float d2y = R.dy[k];
		if (need[k] <= 0.0f || t[k] <= 1.0e-4f)
		{
			pathEmit(R.x[k], R.y[k], d2x, d2y, k, 0.0f, FALSE, 0.0f, 0.0f, 0.0f, FALSE);
			continue;
		}
		const float rad = t[k] / need[k];
		const float t1x = R.x[k] - d1x * t[k];		// the arc starts t before the corner, round a centre on the inner side
		const float t1y = R.y[k] - d1y * t[k];
		const float ox = t1x - d1y * sgn[k] * rad;
		const float oy = t1y + d1x * sgn[k] * rad;
		Bool pivot = FALSE;
		float qx = 0.0f;
		float qy = 0.0f;
		float after = -1.0f;
		if (rad < 0.999f * h)
		{
			pivot = TRUE;
			const float a = h * need[k];	// the two inner lines meet a before and a after the corner
			if (a < R.seg[k - 1] - t[k - 1] - 1.0e-3f && a < R.seg[k] - t[k + 1] - 1.0e-3f)
			{
				// both reach it: the inner ends stop there - a sharp inner edge, the lanes round it
				const float sc = sgn[k] * h / (1.0f + d1x * d2x + d1y * d2y);
				qx = R.x[k] - (d1y + d2y) * sc;
				qy = R.y[k] + (d1x + d2x) * sc;
				pathEmit(R.x[k] - d1x * a, R.y[k] - d1y * a, d1x, d1y, k - 1, R.seg[k - 1] - a, FALSE, 0.0f, 0.0f, 0.0f, FALSE);
				after = a;
			}
			else
			{
				// they cross behind the corner (a sharp turn on short segments): the inner ends pivot on the arc's first one
				qx = t1x - d1y * sgn[k] * h;
				qy = t1y + d1x * sgn[k] * h;
			}
		}
		Int pieces = (Int)ceilf(ang[k] / (PATH_ARC_STEP * PATH_DEG));
		pieces = (pieces < 2) ? 2 : ((pieces > PATH_MAX_ARC) ? PATH_MAX_ARC : pieces);
		pieces += pieces & 1;	// even: the middle edge sits on the corner's own tile
		const float v0x = t1x - ox;
		const float v0y = t1y - oy;
		for (Int p = 0; p <= pieces; ++p)
		{
			const float f = (float)p / (float)pieces;
			const float ca = cosf(sgn[k] * ang[k] * f);
			const float sa = sinf(sgn[k] * ang[k] * f);
			const float x = 2.0f * t[k] * f - t[k];		// the arc laid over [corner - t, corner + t], for the pattern and the fade
			pathEmit(ox + v0x * ca - v0y * sa, oy + v0x * sa + v0y * ca, d1x * ca - d1y * sa, d1x * sa + d1y * ca,
				(x < 0.0f) ? k - 1 : k, (x < 0.0f) ? R.seg[k - 1] + x : x, pivot, qx, qy, sgn[k], FALSE);
		}
		if (after >= 0.0f)
			pathEmit(R.x[k] + d2x * after, R.y[k] + d2y * after, d2x, d2y, k, after, FALSE, 0.0f, 0.0f, 0.0f, FALSE);	// inner end at the meeting point
		else if (pivot)
			pathEmit(R.x[k] + d2x * t[k], R.y[k] + d2y * t[k], d2x, d2y, k, t[k], FALSE, 0.0f, 0.0f, 0.0f, FALSE);	// full width: an empty quad back out
	}
	pathEmit(R.x[m - 1], R.y[m - 1], R.dx[m - 2], R.dy[m - 2], m - 2, R.seg[m - 2], FALSE, 0.0f, 0.0f, 0.0f, FALSE);
}

// The kept edges and their runs. A gap breaks the strip; so do a corner over 135 degrees, the vehicle's left crossing to the path's
// other side (backing up) and a turn round on the spot. markFrom: the stored edge the last two segments start at.
static Bool pathBuild(const Vector3 *left, const Vector3 *right, const float *v, const float *fade, Int count, float spacing, Int &markFrom)
{
	static Vector3 centre[ROUND_MAX_STORED];
	static float tile[ROUND_MAX_STORED];
	static float along[ROUND_MAX_STORED];	// the centre's way from the oldest edge
	static Bool travel[ROUND_MAX_STORED];	// W3DTerrainTracks flipped V here (an edge laid turning copies it)
	static Int nextTravel[ROUND_MAX_STORED];
	static Int prevTravel[ROUND_MAX_STORED];	// Ronin @feature 10/11/2026 DX9: the travel edge at or before each
	static Int keep[ROUND_MAX_STORED];
	static Bool cut[ROUND_MAX_STORED];
	s_pathCount = 0;
	s_pathCuts = 0;
	float width = 0.0f;
	for (Int i = 0; i < count; ++i)
	{
		centre[i] = (left[i] + right[i]) * 0.5f;
		width += pathDist(left[i], right[i]);
		along[i] = (i == 0) ? 0.0f : along[i - 1] + pathDist(centre[i], centre[i - 1]);
		travel[i] = (i == 0 || fabsf(v[i] - v[i - 1]) > 0.5f) ? TRUE : FALSE;
		prevTravel[i] = (travel[i] || i == 0) ? i : prevTravel[i - 1];
	}
	for (Int i = count - 1, n = count; i >= 0; --i)
	{
		nextTravel[i] = n;
		if (travel[i])
			n = i;
	}
	// The pattern's tile, whole and of V's parity at each travel edge. Between two, an odd number of tiles nearest the way the centre went
	// (a creep or a tight turn lays only edges that copy V): spread by distance, so no stretch of track freezes one texel row.
	Int prior = 0;	// the last travel edge
	tile[0] = v[0];
	for (Int i = 1; i < count; ++i)
	{
		const Int next = nextTravel[i - 1];	// the travel edge after it, if any
		const float span = (next < count) ? along[next] - along[prior] : 0.0f;
		const float tiles = 2.0f * floorf(0.5f * (span / spacing - 1.0f) + 0.5f) + 1.0f;
		if (next >= count)
			tile[i] = tile[prior] + (along[i] - along[prior]) / spacing;	// past the newest travel edge: by distance
		else
			tile[i] = tile[prior] + ((tiles > 1.0f) ? tiles : 1.0f) * ((span > 1.0e-3f) ? (along[i] - along[prior]) / span : 1.0f);
		if (travel[i])
			prior = i;
	}
	const float h = 0.5f * width / (float)count;
	if (h < 0.01f)
		return FALSE;
	s_pathHalf = h;
	const float inPlace = PATH_IN_PLACE * spacing;
	const float keepLen = PATH_KEEP * spacing;
	const float gap = PATH_GAP * spacing;
	Int kept = 1;
	keep[0] = 0;
	cut[0] = TRUE;
	for (Int i = 1; i < count; )
	{
		Int j = i;
		while (j < count && pathDist(centre[j], centre[j - 1]) < inPlace)
			++j;
		if (j > i)
		{
			// edges i to j - 1 stood at edge i - 1, turning. A turn round - over 135 degrees, travel edge to travel edge (or the newest) - takes no
			// arc: the strip ends there and starts again after it. A lesser turn merges into the corner.
			const Int after = (nextTravel[j - 1] < count) ? nextTravel[j - 1] : count - 1;
			if (pathTurnCos(left, right, prevTravel[i - 1], after) < PATH_COS_BREAK)
			{
				if (keep[kept - 1] != i - 1 && pathDist(centre[i - 1], centre[keep[kept - 1]]) > 1.0e-3f)
				{
					keep[kept] = i - 1;
					cut[kept++] = FALSE;
				}
				keep[kept] = j - 1;
				cut[kept++] = TRUE;
			}
			i = j;
			continue;
		}
		const float dl = pathDist(centre[i], centre[keep[kept - 1]]);
		if (dl > gap || (dl > 1.0e-3f && (dl >= keepLen || travel[i])))
		{
			keep[kept] = i;
			cut[kept++] = (dl > gap) ? TRUE : FALSE;
		}
		++i;
	}
	// the newest edge ends the strip: under the vehicle
	const Int last = count - 1;
	if (keep[kept - 1] != last)
	{
		const float dl = pathDist(centre[last], centre[keep[kept - 1]]);
		if (kept > 1 && !cut[kept - 1] && dl < keepLen && !travel[last] && !travel[keep[kept - 1]])
			keep[kept - 1] = last;
		else if (dl > 1.0e-3f)
		{
			keep[kept] = last;
			cut[kept++] = (dl > gap) ? TRUE : FALSE;
		}
	}
	markFrom = keep[(kept >= 3) ? kept - 3 : 0];

	Int start = 0;
	for (Int k = 0; k < kept; ++k)
	{
		const Bool end = (k + 1 >= kept || cut[k + 1]) ? TRUE : FALSE;
		Bool split = FALSE;
		if (!end && k > start)
		{
			const Vector3 a = centre[keep[k]] - centre[keep[k - 1]];
			const Vector3 b = centre[keep[k + 1]] - centre[keep[k]];
			const float la = sqrtf(a.X * a.X + a.Y * a.Y);
			const float lb = sqrtf(b.X * b.X + b.Y * b.Y);
			const Vector3 va = left[keep[k]] - right[keep[k]];
			const Vector3 vb = left[keep[k + 1]] - right[keep[k + 1]];
			const Bool sideA = (a.X * va.Y - a.Y * va.X >= 0.0f) ? TRUE : FALSE;	// the vehicle's left on the path's left
			const Bool sideB = (b.X * vb.Y - b.Y * vb.X >= 0.0f) ? TRUE : FALSE;
			split = (la > 1.0e-6f && lb > 1.0e-6f && ((a.X * b.X + a.Y * b.Y) < PATH_COS_BREAK * la * lb || sideA != sideB)) ? TRUE : FALSE;
		}
		if (end || split)
		{
			pathStroke(left, right, tile, fade, keep + start, k - start + 1, h);
			start = end ? k + 1 : k;
		}
	}
	return TRUE;	// nothing laid (a spin on the spot only) draws nothing
}

// ---- Ronin @bugfix 10/11/2026 DX9: the pieces a track gained this frame (its newest edge is new), for R3DTaa's velocity pass. ----

static Vector3 s_markVerts[TAA_MARK_MAX];	// a triangle list
static Int     s_markCount = 0;
static float   s_newestPrev[PATH_MAX_TRACKS][4];	// each track's newest edge last frame (left x y, right x y)
static float   s_newestCur[PATH_MAX_TRACKS][4];
static Int     s_newestPrevCount = 0;
static Int     s_newestCurCount  = 0;

static Bool taaMarkFresh(const Vector3 &l, const Vector3 &r)
{
	Bool seen = FALSE;
	for (Int i = 0; i < s_newestPrevCount && !seen; ++i)
		seen = (s_newestPrev[i][0] == l.X && s_newestPrev[i][1] == l.Y && s_newestPrev[i][2] == r.X && s_newestPrev[i][3] == r.Y) ? TRUE : FALSE;
	if (s_newestCurCount < PATH_MAX_TRACKS)
	{
		s_newestCur[s_newestCurCount][0] = l.X;
		s_newestCur[s_newestCurCount][1] = l.Y;
		s_newestCur[s_newestCurCount][2] = r.X;
		s_newestCur[s_newestCurCount][3] = r.Y;
		++s_newestCurCount;
	}
	return !seen;
}

static void taaMarkQuad(const Vector3 &l0, const Vector3 &r0, const Vector3 &l1, const Vector3 &r1)
{
	if (s_markCount + 6 > TAA_MARK_MAX)
		return;
	s_markVerts[s_markCount++] = l0;
	s_markVerts[s_markCount++] = r0;
	s_markVerts[s_markCount++] = r1;
	s_markVerts[s_markCount++] = l0;
	s_markVerts[s_markCount++] = r1;
	s_markVerts[s_markCount++] = l1;
}

Bool R3DTreadMarks::hasTaaMarks(void)
{
	return (s_markCount >= 3) ? TRUE : FALSE;
}

Int R3DTreadMarks::takeTaaMarks(const Vector3 **verts)
{
	const Int n = s_markCount;
	*verts = s_markVerts;
	s_markCount = 0;
	return n;
}

// Ronin @feature 10/11/2026 DX9: a run's end at a break (a turn round, backing up, a sharp corner) fades over
// half the width, as 2003 fades a track that starts again: two cut ends laid over each other read as planks, not as a turn.
static void pathFadeBreakEnds(void)
{
	static float along[PATH_MAX_EDGES];
	for (Int s = 0; s < s_pathCount; )
	{
		Int t = s + 1;
		while (t < s_pathCount && !s_path[t].cut)
			++t;
		along[s] = 0.0f;
		for (Int e = s + 1; e < t; ++e)
			along[e] = along[e - 1] + pathDist((s_path[e].l + s_path[e].r) * 0.5f, (s_path[e - 1].l + s_path[e - 1].r) * 0.5f);
		for (Int e = s; e < t; ++e)
		{
			float k = 1.0f;
			if (s > 0 && along[e] < k * s_pathHalf)
				k = along[e] / s_pathHalf;	// it starts at a break
			if (t < s_pathCount && along[t - 1] - along[e] < k * s_pathHalf)
				k = (along[t - 1] - along[e]) / s_pathHalf;	// it ends at one
			s_path[e].fade *= k;
		}
		s = t;
	}
}

Int R3DTreadMarks::writeRoundedStrip(const Vector3 *left, const Vector3 *right, const float *v, const float *fade, Int count,
	float spacing, UnsignedInt diffuseRGB, VertexFormatXYZDUV1 *out, Int maxEdges)
{
	VertexFormatXYZDUV1 *const first = out;
	// Ronin @bugfix 10/11/2026 DX9: the frame's first track: last frame's newest edges are the ones to compare with.
	if (s_markClosed)
	{
		memcpy(s_newestPrev, s_newestCur, sizeof(s_newestCur[0]) * s_newestCurCount);
		s_newestPrevCount = s_newestCurCount;
		s_newestCurCount = 0;
		s_markCount = 0;
		s_markClosed = FALSE;
	}
	const Bool fresh = taaMarkFresh(left[count - 1], right[count - 1]);
	Int markFrom = (count > 3) ? count - 3 : 0;

	if (count <= ROUND_MAX_STORED && pathBuild(left, right, v, fade, count, spacing, markFrom) && s_pathCount + 2 * s_pathCuts <= maxEdges)
	{
		pathFadeBreakEnds();
		Vector3 pr(0.0f, 0.0f, 0.0f);
		for (Int e = 0; e < s_pathCount; ++e)
		{
			const PathEdge &pe = s_path[e];
			if (pe.cut && e > 0)
			{
				out = roundPutPair(out, pr, 1.0f, pr, 1.0f, 0.0f, 0.0f, diffuseRGB);	// two empty pairs: no triangle joins the runs
				out = roundPutPair(out, pe.l, 0.0f, pe.l, 0.0f, 0.0f, 0.0f, diffuseRGB);
			}
			const float f = fmodf(pe.tile, 2.0f);
			out = roundPutPair(out, pe.l, pe.flip ? 1.0f : 0.0f, pe.r, pe.flip ? 0.0f : 1.0f, (f <= 1.0f) ? f : 2.0f - f, pe.fade, diffuseRGB);
			pr = pe.r;
			if (fresh && e > 0 && !pe.cut && s_path[e - 1].src >= markFrom)
				taaMarkQuad(s_path[e - 1].l, s_path[e - 1].r, pe.l, pe.r);
		}
	}
	else
	{
		for (Int i = 0; i < count; ++i)
		{
			out = roundPutEdge(out, left[i], right[i], v[i], fade[i], diffuseRGB);
			if (fresh && i > markFrom)
				taaMarkQuad(left[i - 1], right[i - 1], left[i], right[i]);
		}
	}
	return (Int)((out - first) / 2);
}
