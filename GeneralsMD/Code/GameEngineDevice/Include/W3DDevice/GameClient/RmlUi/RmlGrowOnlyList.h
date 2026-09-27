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

// FILE: RmlGrowOnlyList.h ///////////////////////////////////////////////////////
// Reusable storage helper for an RmlUi data-for bound list that must never shrink
// while its document is open. Shrinking the array RmlUi::Vector a data-for is bound
// to (e.g. clear()+rebuild with fewer rows) races RmlUi's own dirty-variable update:
// nested data-attr-*/data-class-* views on a row already queued for removal can be
// refreshed before the data-for structural view removes the row, reading an index
// past the array's new size ("Data array index out of bounds" / "Could not get
// value from data variable"). See rmlui_check's grow/shrink scenarios.
//
// Fix: row storage only ever grows (push_back), never shrinks. A row no longer
// present in the source data is marked unused in place instead of erased, so every
// RmlUi row element the data-for already created keeps a valid backing index for
// the lifetime of the document. Pair this with a data-if on an INNER element keyed
// off RowT::used (data-for must stay on an outer wrapper -- RmlUi doesn't allow two
// structural data views on one element) -- same idiom RmlSkirmishMapSelectScreen
// already used for its fixed-size start_markers array before this helper existed.
//
// RowT requirements: default-constructible, copy-assignable, and has a public
// "bool used" member (registered like any other struct member, e.g.
// structHandle.RegisterMember("used", &RowT::used)).
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/Types.h>

//-------------------------------------------------------------------------------------------------
template <typename RowT>
class RmlGrowOnlyList
{
public:
	// Wraps an existing Rml::Vector<RowT> -- typically a field already bound via
	// DataModelConstructor::Bind(), so the bound address never changes.
	explicit RmlGrowOnlyList(Rml::Vector<RowT> &storage) : m_rows(storage) {}

	// Call once before repopulating from fresh source data.
	void beginUpdate() { m_liveCount = 0; }

	// Returns the next live row (growing storage if every existing slot is already claimed this
	// update), reset to RowT's defaults and marked used. Caller fills in the rest of the fields.
	RowT &next()
	{
		if (m_liveCount >= (int)m_rows.size())
			m_rows.push_back(RowT());

		RowT &row = m_rows[m_liveCount++];
		row = RowT(); // clear whatever a previous occupant of this slot left behind
		row.used = true;
		return row;
	}

	// Call once after repopulating: marks every slot next() didn't touch this round unused, without
	// removing it, so the bound array's size never decreases.
	void endUpdate()
	{
		for (int i = m_liveCount; i < (int)m_rows.size(); ++i)
			m_rows[i].used = false;
	}

	// Number of rows next() produced this update (i.e. the logical/visible count, as opposed to
	// m_rows.size() which only ever grows). Use this for index bounds checks against selections.
	int liveCount() const { return m_liveCount; }

private:
	Rml::Vector<RowT> &m_rows;
	int m_liveCount = 0;
};
