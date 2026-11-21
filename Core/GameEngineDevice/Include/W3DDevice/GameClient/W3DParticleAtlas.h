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

// FILE: W3DParticleAtlas.h //////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "WW3D2/texture.h"
#include "WWLib/ref_ptr.h"
#include "WWLib/wwstring.h"
#include "WWMath/vector4.h"
#include <map>
#include <vector>

// Alpha and additive textures share premultiplied blending; other modes keep their original texels.
class W3DParticleAtlas
{
public:
	W3DParticleAtlas();
	~W3DParticleAtlas();

	enum TextureMode { UNMODIFIED, ALPHA, ADDITIVE, MODE_COUNT };

	/// the page holding the texture's converted art and its rectangle in uv, or null when it has no place; valid only for the frame that asked
	TextureClass *find(TextureClass *source, TextureMode mode, Vector4 &rect);

	/// drops every page, so the next map packs only its own textures
	void reset();

private:
	// Each cell keeps LEVELS mips. Its gutter shrinks to one texel at the last, so filtering never reads a neighbour.
	enum { LEVELS = 5, GUTTER = 1 << (LEVELS - 1), PAGE_SIZE = 2048 + (2048 / 256) * 2 * GUTTER, MAX_PAGES = 4, PACKS_PER_FRAME = 2 };

	struct Shelf
	{
		Int y;
		Int height;
		Int used;
	};

	struct Page
	{
		RefCountPtr<TextureClass> texture;
		std::vector<Shelf> shelves;
		Int bottom;
	};

	struct Location
	{
		TextureClass *page;	///< null when the texture was refused
		Vector4 rect;
		Bool neverFits;		///< refused for its size, so an eviction keeps the refusal
	};

	typedef std::map<StringClass, Location> LocationMap;

	void evictOldestPage();
	Bool hasPackBudget();
	Location pack(const TextureClass &source, TextureMode mode);
	Bool place(Int width, Int height, Int &page, Int &x, Int &y);
	Bool addPage();
	static Bool copyToCell(const TextureClass &source, const TextureClass *page, TextureMode mode, Int left, Int top);

	std::vector<Page> m_pages;
	LocationMap m_locations[MODE_COUNT];
	Int m_packsThisFrame;
	UnsignedInt m_packFrame;
	UnsignedInt m_outOfSpaceFrame;
	Bool m_outOfSpace;
	Bool m_disabled;
};

extern W3DParticleAtlas *TheW3DParticleAtlas;	///< owned by W3DDisplay, so its textures go before the device; null when headless
