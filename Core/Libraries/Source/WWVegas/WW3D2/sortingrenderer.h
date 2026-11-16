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

#pragma once

#include "WWLib/always.h"

class SortingNodeStruct;
class SphereClass;
class ShaderClass;
class Vector3;
struct VertexFormatXYZNDUV2;
struct BillboardInstance;

// Shades particle sprites in a pixel shader, around each of their draws.
class SoftParticleHookClass
{
public:
	enum
	{
		EFFECT_SOFT = 1,	// fade where the sprite nears the scene behind it
		EFFECT_FLAME = 2,	// flicker and heat colouring
		EFFECT_HAZE = 4,	// wobble the scene copy behind the sprite
		EFFECT_ELECTRIC = 8,	// arcs, jitter and strobe
		EFFECT_LASER = 16,	// hot core and travelling pulses, from the beam coordinates in the second uv set
		EFFECT_BEAM = 32,	// a laser draw's beam or a streak, which fades only while shaded
		EFFECT_CRYO = 64,	// ice tint with teeth and frost bands on a beam, splinters and glints on a sprite
		EFFECT_DISRUPT = 128,	// ripple and colour-split the scene copy behind the shape, in a pass of its own before the sorted draws
		EFFECT_MESH = 256,	// a model's mesh, whose texture may tile both ways and whose settings come with it
		EFFECT_MIRROR = 512,	// a planar mirror mesh, shown as the scene behind it bent under its own colour, with the reflection over both
		EFFECT_FLUID = 1024,	// fluid currents, bubbles and luminous edges
		EFFECT_TERRAIN = 2048,	// terrain-conforming particles keep their surface coverage and use border sampling
		EFFECT_SMUDGE = 4096	// shift the scene copy behind a heat sprite, in a pass of its own after the sorted draws
	};

	virtual ~SoftParticleHookClass() {}

	// Called with the draw's render state applied. False leaves the draw fixed-function, and End uncalled.
	// The data is whatever the inserter handed over with the effects, opaque to the renderer.
	virtual bool Begin(const ShaderClass &shader, unsigned effects, const void *effectData) = 0;
	virtual void End() = 0;

	// Whether EFFECT_DISRUPT draws at all. Where it cannot, a shape that would only disrupt draws its own art instead.
	virtual bool Can_Disrupt() = 0;
};

class SortingRendererClass
{
	static bool _EnableTriangleDraw;

	static void Flush_Sorting_Pool();
	static void Flush_Pool_Batch(unsigned first_node,unsigned end_node,unsigned polygon_count,unsigned vertex_count);
	static void Insert_To_Sorting_Pool(SortingNodeStruct* state);
	static const VertexFormatXYZNDUV2* Source_Vertices(const SortingNodeStruct* state);
	static const unsigned short* Source_Indices(const SortingNodeStruct* state);

public:
#if defined(BUILD_WITH_D3D9) && defined(RTS_ZEROHOUR)
	static void Insert_Billboards(const BillboardInstance *instances, unsigned count);
#endif

	static void Insert_Triangles(
		const SphereClass& bounding_sphere,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Insert_Triangles(
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Flush();
	static void Deinit();

	// Triangles inserted with EFFECT_DISRUPT wait here and draw through the hook alone. Flush drops any left undrawn.
	static bool Has_Disruption();
	static void Flush_Disruption();
	static bool Can_Disrupt();

	// For the hook, while a disruption node draws: where a draw with world-space vertices sits, or null to read the world transform, and how much of its mask it keeps.
	static const Vector3 *Peek_Disruption_Center();
	static float Get_Disruption_Strength();

	static void SetMinVertexBufferSize( unsigned val );

	// Rigid translucent meshes keep their own buffers and sort as whole objects.
	static bool Sorts_Meshes_Per_Object();

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }

	// Triangles inserted while effects are set are drawn through the hook with those effects.
	static void Set_Soft_Particle_Hook(SoftParticleHookClass *hook);
	static SoftParticleHookClass *Peek_Soft_Particle_Hook();
	static void Set_Insert_Effects(unsigned effects, const void *effectData, float disruptionStrength = 1.0f);
};
