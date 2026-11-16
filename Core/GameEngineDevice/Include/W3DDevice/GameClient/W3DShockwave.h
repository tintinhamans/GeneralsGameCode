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

// FILE: W3DShockwave.h /////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "WW3D2/dx8compat.h"

class RenderInfoClass;

// Expanding rings that bend the finished scene, for the blast of large explosions and ripples on water.
class W3DShockwaveManager
{
public:
	W3DShockwaveManager();
	~W3DShockwaveManager();

	/// radius is how far the ring travels, width its thickness and strength how far it bends the scene, all in world units
	/// strengthInPixels measures strength in pixels at 1080 lines instead, so the bend is the same at every zoom
	/// rings is how many waves ride in the band; onWater lets the shore and anything in front hide the ring
	void add(const Coord3D &position, Real radius, Real width, Real strength, Bool strengthInPixels, UnsignedInt durationMs, Int rings, Bool onWater);
	/// draws the live rings over the scene, after the particles
	void render(RenderInfoClass &rinfo);
	void ReleaseResources();	///< drops the scene copy and shader before a device reset; render recreates them

private:
	enum { MAX_SHOCKWAVES = 32 };

	struct Shockwave
	{
		Coord3D position;
		Real radius;
		Real width;
		Real strength;
		Bool strengthInPixels;
		UnsignedInt startMs;
		UnsignedInt durationMs;
		Int rings;
		Bool onWater;
	};

	Shockwave m_shockwaves[MAX_SHOCKWAVES];
	Int m_count;
	IDirect3DTexture8 *m_sceneCopy;
	DWORD m_shader;
	Bool m_loaded;
};

extern W3DShockwaveManager *TheW3DShockwaves;
