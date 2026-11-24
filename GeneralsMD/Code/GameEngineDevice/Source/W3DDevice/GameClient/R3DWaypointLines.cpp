/*
**	Command & Conquer Generals Zero Hour(tm)
**	DX9 port: waypoint ribbons - the waypoint, rally point and enemy-path lines as flat bands on the ground.
*/

// Ronin @feature 09/11/2026 DX9: waypoint ribbons (docs/Waypoints_Work.md). See R3DWaypointLines.h.
#include "WW3D2/dx8todx9.h"
#include "WWLib/always.h"
#include "Lib/BaseType.h"
#include "WWMath/vector3.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/shader.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/R3DShadowMapState.h"
#include "W3DDevice/GameClient/R3DWaypointLines.h"
#include <d3dx9.h>
#include <math.h>
#include "W3DDevice/GameClient/W3DShaderManager.h"	// Ronin @feature 24/11/2026 DX9: readShaderFile

static IDirect3DVertexShader9	*s_vs = NULL;
static IDirect3DPixelShader9	*s_ps = NULL;
static Bool						s_triedShaders = FALSE;
static const float				WIDTH = 5.0f;	// the band's width, world units
static const float				ALPHA = 1.0f;	// overall opacity of everything it draws
static const float				RINGS = 0.35f;	// the rings' opacity; 0 = none (G2 has none)
static const float				FLOW  = 24.0f;	// the arrows' speed towards the destination, units/s; 0 = still

static const float LIFT      = 0.8f;	// over the ground: depth-tested against it without fighting it
static const float STEP      = 6.0f;	// the ground is sampled this often along a segment (its cells are 10)
static const float RAISED    = 4.0f;	// a node this far over the ground is on a bridge: the band keeps its height
static const float RING      = 4.5f;	// a waypoint's ring; the destination's the same (09/11: subtle, no bigger end)

// What drawWaypoints handed over this frame.
enum { MAX_PATHS = 256, MAX_POINTS = 4096, MAX_SAMPLES = 1024, MAX_RINGS = 512 };
struct GatheredPath { Int first; Int count; Int style; };
static GatheredPath	s_paths[MAX_PATHS];
static Vector3		s_points[MAX_POINTS];
static Int			s_pathCount  = 0;
static Int			s_pointCount = 0;

// t: x across the band -1..1 (a ring: -1..1 across its square), y the distance along the path, z the path's length,
// w 1 on a ring.
struct RibbonVertex { float x, y, z; DWORD color; float tx, ty, tz, tw; };
static const DWORD RIBBON_FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0);
static RibbonVertex	s_strip[2 * MAX_SAMPLES];
static RibbonVertex	s_ringVerts[MAX_RINGS * 24];

// The path being built: its nodes without repeats, and which stand on a bridge.
static Vector3		s_node[MAX_POINTS];
static Bool			s_nodeRaised[MAX_POINTS];
static Int			s_nodeCount = 0;

// The device states this pass changes, and what they were.
static const D3DRENDERSTATETYPE RIBBON_STATES[] = {
	D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
	D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE,
	D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS };
enum { RIBBON_STATE_COUNT = sizeof(RIBBON_STATES) / sizeof(RIBBON_STATES[0]) };

static void ensureShaders(IDirect3DDevice9 *dev)
{
	if (s_triedShaders)
		return;
	s_triedShaders = TRUE;
	DWORD *blob = W3DShaderManager::readShaderFile("shaders\\WaypointRibbon.vso");
	if (blob != NULL)
	{
		if (FAILED(dev->CreateVertexShader(blob, &s_vs)))
			s_vs = NULL;
		HeapFree(GetProcessHeap(), 0, blob);
	}
	blob = W3DShaderManager::readShaderFile("shaders\\WaypointRibbon.pso");
	if (blob != NULL)
	{
		if (FAILED(dev->CreatePixelShader(blob, &s_ps)))
			s_ps = NULL;
		HeapFree(GetProcessHeap(), 0, blob);
	}
}

// The frame's own pass: not the water's mirror, not the sun's depth map.
static Bool mainPass(void)
{
	return (!ShaderClass::Is_Backface_Culling_Inverted() && !TheTerrainShadowPass.inDepthPass) ? TRUE : FALSE;
}

static float groundAt(float x, float y)
{
	return (TheTerrainLogic != NULL) ? TheTerrainLogic->getGroundHeight(x, y) : 0.0f;
}

static DWORD styleColor(Int style)
{
	return (style == R3DWaypointLines::STYLE_ENEMY) ? D3DCOLOR_ARGB(255, 255, 140, 26) : D3DCOLOR_ARGB(255, 64, 216, 255);
}

// Seconds for the arrows and the rings' pulse, on the real clock (they move while the game is paused, as a cursor does).
// Wrapped at 10 minutes so a float keeps its precision.
static float secondsNow(void)
{
	static LARGE_INTEGER freq = {}, start = {};
	LARGE_INTEGER now;
	if (freq.QuadPart == 0)
	{
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&start);
	}
	QueryPerformanceCounter(&now);
	return (float)fmod((double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart, 600.0);
}

static void putVertex(RibbonVertex &o, float x, float y, float z, DWORD color, float tx, float ty, float tz, float tw)
{
	o.x = x; o.y = y; o.z = z; o.color = color;
	o.tx = tx; o.ty = ty; o.tz = tz; o.tw = tw;
}

// The band: every STEP units along each segment a pair of vertices across it, each at the ground's height under it (a
// segment between two bridge nodes keeps their height). Corners are mitred so the edges stay straight. Leaves the
// path's nodes in s_node for the rings; returns the vertices written to s_strip.
static Int buildStrip(const Vector3 *p, Int count, DWORD color, float halfWidth)
{
	s_nodeCount = 0;
	for (Int i = 0; i < count && s_nodeCount < MAX_SAMPLES / 2; ++i)
	{
		if (s_nodeCount > 0)
		{
			const float dx = p[i].X - s_node[s_nodeCount - 1].X;
			const float dy = p[i].Y - s_node[s_nodeCount - 1].Y;
			if (dx * dx + dy * dy < 0.01f)
				continue;		// a repeat has no direction
		}
		s_nodeRaised[s_nodeCount] = (p[i].Z > groundAt(p[i].X, p[i].Y) + RAISED) ? TRUE : FALSE;
		s_node[s_nodeCount++] = p[i];
	}
	const Int n = s_nodeCount;
	if (n < 2)
		return 0;

	static float segNX[MAX_SAMPLES], segNY[MAX_SAMPLES], segLen[MAX_SAMPLES];
	float total = 0.0f;
	Int samples = 1;
	for (Int k = 0; k + 1 < n; ++k)
	{
		const float dx = s_node[k + 1].X - s_node[k].X;
		const float dy = s_node[k + 1].Y - s_node[k].Y;
		const float len = sqrtf(dx * dx + dy * dy);
		segLen[k] = len;
		segNX[k]  = -dy / len;		// the band's sideways, left to right
		segNY[k]  = dx / len;
		total    += len;
		samples  += (Int)ceilf(len / STEP);
	}
	const float step = (samples > MAX_SAMPLES) ? STEP * (float)samples / (float)(MAX_SAMPLES - n) : STEP;	// a long path: coarser

	Int v = 0;
	float along = 0.0f;
	for (Int k = 0; k + 1 < n; ++k)
	{
		Int pieces = (Int)ceilf(segLen[k] / step);
		if (pieces < 1)
			pieces = 1;
		const Bool bridge = (s_nodeRaised[k] && s_nodeRaised[k + 1]) ? TRUE : FALSE;
		const Int last = (k + 2 == n) ? pieces : pieces - 1;	// the next segment writes the corner they share
		for (Int j = 0; j <= last && v + 2 <= 2 * MAX_SAMPLES; ++j)
		{
			const float t  = (float)j / (float)pieces;
			const float cx = s_node[k].X + (s_node[k + 1].X - s_node[k].X) * t;
			const float cy = s_node[k].Y + (s_node[k + 1].Y - s_node[k].Y) * t;
			float nx = segNX[k];
			float ny = segNY[k];
			if (j == 0 && k > 0)
			{
				float mx = segNX[k - 1] + segNX[k];
				float my = segNY[k - 1] + segNY[k];
				const float ml = sqrtf(mx * mx + my * my);
				if (ml > 0.05f)		// a reversal has no mitre: the band turns on the spot, under its ring
				{
					mx /= ml;
					my /= ml;
					const float c = mx * segNX[k] + my * segNY[k];	// the cosine of half the turn
					const float scale = 1.0f / ((c > 0.5f) ? c : 0.5f);	// at most twice as wide, past 120 degrees
					nx = mx * scale;
					ny = my * scale;
				}
			}
			const float ox = nx * halfWidth;
			const float oy = ny * halfWidth;
			float zl, zr;
			if (bridge)
				zl = zr = s_node[k].Z + (s_node[k + 1].Z - s_node[k].Z) * t + LIFT;
			else
			{
				zl = groundAt(cx - ox, cy - oy) + LIFT;
				zr = groundAt(cx + ox, cy + oy) + LIFT;
			}
			const float at = along + segLen[k] * t;
			putVertex(s_strip[v++], cx - ox, cy - oy, zl, color, -1.0f, at, total, 0.0f);
			putVertex(s_strip[v++], cx + ox, cy + oy, zr, color,  1.0f, at, total, 0.0f);
		}
		along += segLen[k];
	}
	return v;
}

// A ring flat on the ground: a 3 x 3 grid, each vertex at the ground's height (a ring is wider than the ground is flat).
static void buildRing(RibbonVertex *out, const Vector3 &c, float radius, DWORD color, Bool raised)
{
	RibbonVertex g[9];
	for (Int gy = 0; gy < 3; ++gy)
	{
		for (Int gx = 0; gx < 3; ++gx)
		{
			const float a = (float)gx - 1.0f;
			const float b = (float)gy - 1.0f;
			const float x = c.X + a * radius;
			const float y = c.Y + b * radius;
			const float z = (raised ? c.Z : groundAt(x, y)) + LIFT + 0.05f;	// over the band
			putVertex(g[gy * 3 + gx], x, y, z, color, a, b, 0.0f, 1.0f);
		}
	}
	static const Int idx[24] = { 0, 1, 4,  0, 4, 3,  1, 2, 5,  1, 5, 4,  3, 4, 7,  3, 7, 6,  4, 5, 8,  4, 8, 7 };
	for (Int i = 0; i < 24; ++i)
		out[i] = g[idx[i]];
}

void R3DWaypointLines::beginGather(void)
{
	if (!mainPass())
		return;
	s_pathCount  = 0;
	s_pointCount = 0;
}

void R3DWaypointLines::addPath(const Vector3 *points, Int count, Int style)
{
	if (!mainPass() || points == NULL || count < 2 || s_pathCount >= MAX_PATHS || s_pointCount + count > MAX_POINTS)
		return;
	GatheredPath &gp = s_paths[s_pathCount++];
	gp.first = s_pointCount;
	gp.count = count;
	gp.style = style;
	for (Int i = 0; i < count; ++i)
		s_points[s_pointCount++] = points[i];
}

void R3DWaypointLines::drawGathered(void)
{
	const Int pathCount = s_pathCount;
	s_pathCount = 0;		// used up: a frame that gathers nothing draws nothing
	if (pathCount == 0)
		return;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == NULL)
		return;
	IDirect3DSurface9 *rt1 = NULL;
	if (SUCCEEDED(dev->GetRenderTarget(1, &rt1)) && rt1 != NULL)
	{
		rt1->Release();		// a second target bound: not the frame's own pass
		return;
	}
	ensureShaders(dev);
	if (s_vs == NULL || s_ps == NULL)
		return;

	// The camera the scene was just drawn with (TAA's jitter in it) is on the device once the wrapper's pending changes are.
	DX8Wrapper::Apply_Render_State_Changes();
	D3DMATRIX view, proj;
	dev->GetTransform(D3DTS_VIEW, &view);
	dev->GetTransform(D3DTS_PROJECTION, &proj);
	D3DXMATRIX vp, vpT;
	D3DXMatrixMultiply(&vp, (const D3DXMATRIX *)&view, (const D3DXMATRIX *)&proj);
	D3DXMatrixTranspose(&vpT, &vp);

	DWORD saved[RIBBON_STATE_COUNT];
	for (Int i = 0; i < RIBBON_STATE_COUNT; ++i)
		dev->GetRenderState(RIBBON_STATES[i], &saved[i]);

	DX8Wrapper::BindLayoutFVF(RIBBON_FVF, "R3DWaypointLines");
	dev->SetVertexShader(s_vs);		// AFTER the layout bind - BindLayoutFVF clears the vertex shader
	dev->SetPixelShader(s_ps);
	dev->SetVertexShaderConstantF(248, (const float *)&vpT, 4);
	const float flow[4]  = { secondsNow(), FLOW, 16.0f, ALPHA };	// time, arrows' speed and spacing, overall alpha
	const float shape[4] = { 0.22f, 0.65f, 24.0f, 10.0f };			// fill, rims, the end's glow and the start's fade (units)
	const float ring[4]  = { RINGS, 0.0f, 0.0f, 0.0f };
	dev->SetPixelShaderConstantF(206, flow, 1);
	dev->SetPixelShaderConstantF(207, shape, 1);
	dev->SetPixelShaderConstantF(208, ring, 1);

	// Over the ground, under what stands on it: depth tested, never written; colour only, not alpha.
	const float zero = 0.0f;
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	dev->SetRenderState(D3DRS_DEPTHBIAS, *(const DWORD *)&zero);
	dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *(const DWORD *)&zero);

	Int rings = 0;
	for (Int i = 0; i < pathCount; ++i)
	{
		const GatheredPath &gp = s_paths[i];
		const DWORD color = styleColor(gp.style);
		const Int verts = buildStrip(&s_points[gp.first], gp.count, color, 0.5f * WIDTH);
		if (verts < 4)
			continue;
		dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, verts - 2, s_strip, sizeof(RibbonVertex));
		// a waypoint path rings every node past the unit; a rally line only its end
		const Int firstRing = (gp.style == STYLE_RALLY) ? s_nodeCount - 1 : 1;
		for (Int k = firstRing; k < s_nodeCount && rings < MAX_RINGS && RINGS > 0.0f; ++k)
		{
			buildRing(&s_ringVerts[rings * 24], s_node[k], RING, color, s_nodeRaised[k]);
			++rings;
		}
	}
	if (rings > 0)
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, rings * 8, s_ringVerts, sizeof(RibbonVertex));

	dev->SetVertexShader(NULL);
	dev->SetPixelShader(NULL);
	for (Int i = 0; i < RIBBON_STATE_COUNT; ++i)
		dev->SetRenderState(RIBBON_STATES[i], saved[i]);
	// The wrapper's caches no longer match the device (its stream too, after the UP draws); its next draws set their own.
	DX8Wrapper::Invalidate_Cached_Render_States();
}

void R3DWaypointLines::shutdown(void)
{
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

// FALSE only once its shaders failed to load: drawWaypoints then draws 2003's lines and pucks.
Bool R3DWaypointLines::getEnabled(void)
{
	return (!s_triedShaders || (s_vs != NULL && s_ps != NULL)) ? TRUE : FALSE;
}
