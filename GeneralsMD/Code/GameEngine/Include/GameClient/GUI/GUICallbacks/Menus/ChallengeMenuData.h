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

// FILE: ChallengeMenuData.h /////////////////////////////////////////////////
// Widget-agnostic state of the Generals Challenge menu (ChallengeMenu.wnd and its RmlUi
// replacement): the twelve general medallions, the selection and the bio being typed out.
// ChallengeMenuActions changes it; the screens only draw it.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UnicodeString.h"
#include "GameClient/ChallengeGenerals.h"

class Image;

struct ChallengeMenuData
{
	static ChallengeMenuData &instance()
	{
		static ChallengeMenuData s_data;
		return s_data;
	}

	static const Int BIO_LINES = 4; ///< name, rank, branch, strategy

	struct General
	{
		Bool m_enabled = FALSE; ///< StartsEnabled; the others have no medallion
		const Image *m_normal = nullptr;
		const Image *m_hilite = nullptr;
		const Image *m_selected = nullptr;
		const Image *m_portrait = nullptr; ///< the small bio portrait, for views that show every general
		UnicodeString m_name; ///< the bio name
		Real m_mapX = 0.0f; ///< home region on the world map (GCBackgroundMinSpec's map area), 0..1 across
		Real m_mapY = 0.0f; ///< and 0..1 down
	};

	void open(); ///< read the generals, clear the selection and the bio
	void touch() { ++m_version; } ///< bump after any change so the view refreshes

	void showBio( Int general ); ///< start typing out the bio of a general; ignores out of range indices
	Bool typeBio( Int frames ); ///< types the next characters; TRUE if anything changed

	General m_generals[NUM_GENERALS];
	Int m_selected = -1; ///< the chosen general, -1 until the first click

	Bool m_bioVisible = FALSE; ///< hidden until a bio was shown
	const Image *m_portrait = nullptr;
	const Image *m_portraitLarge = nullptr; ///< the same general's large portrait
	UnicodeString m_bioText[BIO_LINES]; ///< the full lines
	UnicodeString m_bioShown[BIO_LINES]; ///< what was typed so far
	Int m_bioPosition = 0;
	Int m_bioLength = 0;

	Bool m_gameStarting = FALSE; ///< Play was pressed: the menu closes for the game, not for Back

	UnsignedInt m_version = 0;
};
