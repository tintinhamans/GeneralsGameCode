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

// Ronin @diagnostic 06/11/2026 DX9: the sorting pool's CPU time, summed over a frame's flushes, for the panel's [SORT] row.
// Timed only while `enabled` (the row on and shown); the panel reads and zeroes it once a frame.
struct SortingRendererStats
{
	enum { MS_INSERT, MS_FLUSH, MS_BUILD, MS_SORT, MS_REGROUP, MS_DRAW, MS_COUNT };
	// Ronin @diagnostic 14/11/2026 DX9: what ended a run of the pool's triangles - a lit mesh, another transform, the shader, the material, a texture.
	enum { BREAK_LIT, BREAK_TRANSFORM, BREAK_SHADER, BREAK_MATERIAL, BREAK_TEXTURE, BREAK_COUNT };
	double		ms[MS_COUNT];	// insert: the sorted-list scan; flush: all of Flush; build/sort/regroup/draw: the pool's steps
	unsigned	nodes;			// pooled nodes
	unsigned	direct;			// sorted nodes drawn from their own buffers
	unsigned	triangles;		// pooled triangles
	unsigned	flushes;
	unsigned	draws;			// Ronin @diagnostic 14/11/2026 DX9: the pool's draws, one per run
	unsigned	breaks[BREAK_COUNT];	// ... and what ended each run but a chunk's last (Classify_Run_Break)
	bool		enabled;
};
extern SortingRendererStats TheSortingStats;
// Ronin @feature 14/11/2026 DX9: a lit mesh in the pool takes one place in the depth order and draws as one run (Flush_Sorting_Pool).
extern bool TheSortedMeshWhole;

class SortingRendererClass
{
	static bool _EnableTriangleDraw;

	static void Flush_Sorting_Pool();
	static void Insert_To_Sorted_List(SortingNodeStruct* state);
	static void Insert_To_Sorting_Pool(SortingNodeStruct* state);

public:
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
	// Ronin @feature 13/11/2026 DX9: the opacity of the triangles inserted next (a faded mesh; 1 = unfaded), kept on their node.
	static void Set_Insert_Opacity(float opacity);

	static void SetMinVertexBufferSize( unsigned val );

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }
};
