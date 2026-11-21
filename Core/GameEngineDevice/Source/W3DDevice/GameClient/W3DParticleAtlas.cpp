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

// W3DParticleAtlas.cpp ///////////////////////////////////////////////////////////////////////////
// Shared premultiplied pages for particle textures, following TheSuperHackers' TextureAtlasClass
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "WWLib/always.h"
#include "W3DDevice/GameClient/W3DParticleAtlas.h"
#include "Common/Debug.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/textureloader.h"
#include "WW3D2/ww3d.h"
#include "WWMath/vector2i.h"
#include "WWMath/wwmath.h"
#include <Utility/premultiply_alpha.h>
#include <algorithm>
#include <string.h>

namespace
{

// A gutter texel repeats the opposite edge where the source tiles and the nearest edge where it clamps.
Int Wrap_Or_Clamp(Int coordinate, Int size, Bool wrap)
{
	if (wrap)
	{
		return coordinate & (size - 1);
	}
	return WWMath::Clamp_Int(coordinate, 0, size - 1);
}

// The cell is the art plus its padding on every side, locked at its top left padding texel.
void Fill_Padding(UnsignedByte *cell, Int pitch, Int width, Int height, Int padding, Bool wrapU, Bool wrapV)
{
	for (Int y = padding; y < padding + height; ++y)
	{
		UnsignedInt *line = (UnsignedInt *)(cell + y * pitch);
		for (Int x = 0; x < padding; ++x)
		{
			line[x] = line[padding + Wrap_Or_Clamp(x - padding, width, wrapU)];
			line[padding + width + x] = line[padding + Wrap_Or_Clamp(width + x, width, wrapU)];
		}
	}

	const Int lineBytes = (width + 2 * padding) * sizeof(UnsignedInt);
	for (Int row = 0; row < padding; ++row)
	{
		memcpy(cell + row * pitch, cell + (padding + Wrap_Or_Clamp(row - padding, height, wrapV)) * pitch, lineBytes);
		memcpy(cell + (padding + height + row) * pitch, cell + (padding + Wrap_Or_Clamp(height + row, height, wrapV)) * pitch, lineBytes);
	}
}

}

W3DParticleAtlas *TheW3DParticleAtlas = nullptr;

W3DParticleAtlas::W3DParticleAtlas()
{
	m_packsThisFrame = 0;
	m_packFrame = 0;
	m_outOfSpaceFrame = 0;
	m_outOfSpace = FALSE;
	m_disabled = FALSE;
}

W3DParticleAtlas::~W3DParticleAtlas()
{
	reset();
}

void W3DParticleAtlas::reset()
{
	for (Int i = 0; i < MODE_COUNT; ++i)
	{
		m_locations[i].clear();
	}
	m_pages.clear();
	m_outOfSpace = FALSE;
	m_outOfSpaceFrame = 0;
	m_packsThisFrame = 0;
	m_disabled = FALSE;
}

TextureClass *W3DParticleAtlas::find(TextureClass *source, TextureMode mode, Vector4 &rect)
{
	if (m_disabled || source == nullptr || source->Get_Texture_Name().Is_Empty())
	{
		return nullptr;
	}

	// Locations handed out in the frame that ran out of space may point into the oldest page, so it empties only later.
	if (m_outOfSpace && m_outOfSpaceFrame != WW3D::Get_Frame_Count() && !m_pages.empty())
	{
		evictOldestPage();
	}

	LocationMap &locations = m_locations[mode];
	LocationMap::iterator found = locations.find(source->Get_Texture_Name());
	if (found == locations.end())
	{
		if (!hasPackBudget())
		{
			return nullptr;
		}

		if (!source->Is_Initialized())
		{
			TextureLoader::Request_Foreground_Loading(source);
		}

		// A texture refused for space keeps its refusal until the next eviction.
		found = locations.insert(std::make_pair(source->Get_Texture_Name(), pack(*source, mode))).first;
		++m_packsThisFrame;
	}

	if (found->second.page == nullptr)
	{
		return nullptr;
	}
	rect = found->second.rect;
	return found->second.page;
}

// Textures refused for space are forgotten with the oldest page's, so both pack again when drawn.
void W3DParticleAtlas::evictOldestPage()
{
	TextureClass *evicted = m_pages.front().texture.Peek();
	for (Int i = 0; i < MODE_COUNT; ++i)
	{
		LocationMap::iterator it = m_locations[i].begin();
		while (it != m_locations[i].end())
		{
			if (it->second.page == evicted || (it->second.page == nullptr && !it->second.neverFits))
			{
				m_locations[i].erase(it++);
			}
			else
			{
				++it;
			}
		}
	}

	// The emptied page is reused as the newest.
	std::rotate(m_pages.begin(), m_pages.begin() + 1, m_pages.end());
	m_pages.back().shelves.clear();
	m_pages.back().bottom = 0;
	m_outOfSpace = FALSE;
}

W3DParticleAtlas::Location W3DParticleAtlas::pack(const TextureClass &source, TextureMode mode)
{
	Location refused;
	refused.page = nullptr;
	refused.rect.Set(0.0f, 0.0f, 1.0f, 1.0f);
	refused.neverFits = FALSE;

	// A texture can be initialised without a D3D texture, for example with texturing disabled.
	if (source.Peek_D3D_Texture() == nullptr)
	{
		return refused;
	}

	const Int width = source.Get_Width();
	const Int height = source.Get_Height();
	const Int cellWidth = width + 2 * GUTTER;
	const Int cellHeight = height + 2 * GUTTER;
	if (!WWMath::Is_Power_Of_2(width) || !WWMath::Is_Power_Of_2(height) || width < GUTTER || height < GUTTER ||
		cellWidth > PAGE_SIZE || cellHeight > PAGE_SIZE)
	{
		refused.neverFits = TRUE;
		return refused;
	}

	Int page = 0;
	Int x = 0;
	Int y = 0;
	if (!place(cellWidth, cellHeight, page, x, y) || !copyToCell(source, m_pages[page].texture.Peek(), mode, x + GUTTER, y + GUTTER))
	{
		return refused;
	}

	Location location;
	const Real scale = 1.0f / (Real)PAGE_SIZE;
	location.page = m_pages[page].texture.Peek();
	location.rect.Set((x + GUTTER) * scale, (y + GUTTER) * scale, (x + GUTTER + width) * scale, (y + GUTTER + height) * scale);
	location.neverFits = FALSE;
	return location;
}

// Same-height shelves first, then a new shelf on the first page with room, then a new page.
Bool W3DParticleAtlas::place(Int width, Int height, Int &page, Int &x, Int &y)
{
	for (size_t p = 0; p < m_pages.size(); ++p)
	{
		std::vector<Shelf> &shelves = m_pages[p].shelves;
		for (size_t s = 0; s < shelves.size(); ++s)
		{
			Shelf &candidate = shelves[s];
			if (candidate.height == height && candidate.used + width <= PAGE_SIZE)
			{
				page = (Int)p;
				x = candidate.used;
				y = candidate.y;
				candidate.used += width;
				return TRUE;
			}
		}
	}

	size_t target = 0;
	while (target < m_pages.size() && m_pages[target].bottom + height > PAGE_SIZE)
	{
		++target;
	}
	if (target == m_pages.size() && !addPage())
	{
		m_outOfSpace = TRUE;
		m_outOfSpaceFrame = WW3D::Get_Frame_Count();
		return FALSE;
	}

	Page &owner = m_pages[target];
	Shelf added;
	added.y = owner.bottom;
	added.height = height;
	added.used = width;
	owner.shelves.push_back(added);
	owner.bottom += height;
	page = (Int)target;
	x = 0;
	y = added.y;
	return TRUE;
}

Bool W3DParticleAtlas::addPage()
{
	if (m_pages.size() >= MAX_PAGES)
	{
		return FALSE;
	}

	// The device may give another mip count, format or size than asked for, and then no page can be made.
	IDirect3DTexture8 *texture = DX8Wrapper::_Create_DX8_Texture(PAGE_SIZE, PAGE_SIZE, WW3D_FORMAT_A8R8G8B8, (MipCountType)LEVELS, D3DPOOL_MANAGED, false);
	D3DSURFACE_DESC desc;
	if (texture == nullptr || texture->GetLevelCount() != LEVELS || FAILED(texture->GetLevelDesc(0, &desc)) ||
		desc.Format != D3DFMT_A8R8G8B8 || (Int)desc.Width != PAGE_SIZE || (Int)desc.Height != PAGE_SIZE)
	{
		if (texture != nullptr)
		{
			texture->Release();
		}
		DEBUG_CRASH(("Particle atlas: page has the wrong mip count, format or size, atlas disabled"));
		m_disabled = TRUE;
		return FALSE;
	}

	// Space no entry holds is left unwritten, since filtering never reads past an entry's gutter.
	Page page;
	page.texture.Assign_No_Add_Ref(NEW_REF(TextureClass, (texture)));
	texture->Release();
	page.bottom = 0;

	// Anisotropic taps reach past the one-texel gutter of the last level, so pages filter bilinear or trilinear only.
	TextureFilterClass &filter = page.texture->Get_Filter();
	filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
	filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
	filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_DEFAULT);
	filter.Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	filter.Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);

	m_pages.push_back(page);
	DEBUG_LOG(("Particle atlas: page %d of %d, %dx%d, %d mips", (Int)m_pages.size(), (Int)MAX_PAGES, (Int)PAGE_SIZE, (Int)PAGE_SIZE, (Int)LEVELS));
	return TRUE;
}

// The source's own mips are kept, since rebuilding them from the top level shifts legacy sprites.
Bool W3DParticleAtlas::copyToCell(const TextureClass &source, const TextureClass *page, TextureMode mode, Int left, Int top)
{
	const Int sourceLevels = (Int)source.Peek_D3D_Texture()->GetLevelCount();
	const Int width0 = source.Get_Width();
	const Int height0 = source.Get_Height();
	const Bool wrapU = source.Get_Filter().Get_U_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_REPEAT;
	const Bool wrapV = source.Get_Filter().Get_V_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_REPEAT;

	for (Int level = 0; level < LEVELS; ++level)
	{
		const Int padding = GUTTER >> level;
		const Int width = width0 >> level;
		const Int height = height0 >> level;
		const Int x = left >> level;
		const Int y = top >> level;
		const Bool fromSource = level < sourceLevels;

		// A level the source lacks is scaled down from the cell's level above.
		RefCountPtr<SurfaceClass> pageSurface;
		pageSurface.Assign_No_Add_Ref(page->Get_Surface_Level(level));
		RefCountPtr<SurfaceClass> sourceSurface;
		if (fromSource)
		{
			sourceSurface.Assign_No_Add_Ref(source.Get_Surface_Level(level));
			pageSurface->Copy(x, y, 0, 0, width, height, sourceSurface.Peek());
		}
		else
		{
			sourceSurface.Assign_No_Add_Ref(page->Get_Surface_Level(level - 1));
			pageSurface->Stretch_Copy(x, y, width, height, x * 2, y * 2, width * 2, height * 2, sourceSurface.Peek());
		}

		Int pitch = 0;
		UnsignedByte *cell = (UnsignedByte *)pageSurface->Lock(&pitch, Vector2i(x - padding, y - padding), Vector2i(x + width + padding, y + height + padding));
		if (cell == nullptr)
		{
			return FALSE;
		}

		// A scaled level comes from the level above, which is already converted.
		UnsignedByte *art = cell + padding * pitch + padding * sizeof(UnsignedInt);
		if (fromSource && mode == ALPHA)
		{
			premultiply_alpha(art, pitch, width, height);
		}
		else if (fromSource && mode == ADDITIVE)
		{
			for (Int row = 0; row < height; ++row)
			{
				UnsignedInt *line = (UnsignedInt *)(art + row * pitch);
				for (Int column = 0; column < width; ++column)
				{
					line[column] &= 0x00FFFFFFu;
				}
			}
		}

		Fill_Padding(cell, pitch, width, height, padding, wrapU, wrapV);
		pageSurface->Unlock();
	}

	// The top level's dirty rectangle covers the cell at every level.
	IDirect3DTexture8 *texture = page->Peek_D3D_Texture();
	RECT cell;
	cell.left = left - GUTTER;
	cell.top = top - GUTTER;
	cell.right = left + width0 + GUTTER;
	cell.bottom = top + height0 + GUTTER;
	DX8Wrapper::_Peek_Lockable_Texture(texture)->AddDirtyRect(&cell);
	DX8Wrapper::_Upload_Lockable_Texture(texture, false);
	return TRUE;
}

Bool W3DParticleAtlas::hasPackBudget()
{
	if (m_packFrame != WW3D::Get_Frame_Count())
	{
		m_packFrame = WW3D::Get_Frame_Count();
		m_packsThisFrame = 0;
	}
	return m_packsThisFrame < PACKS_PER_FRAME;
}
