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

// FILE: ChallengeMenuData.cpp ///////////////////////////////////////////////
// See ChallengeMenuData.h. Bodies moved out of ChallengeMenu.cpp; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ChallengeMenuData.h"

#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"

void ChallengeMenuData::open()
{
	const GeneralPersona *generals = TheChallengeGenerals->getChallengeGenerals();

	for( Int i = 0; i < NUM_GENERALS; ++i )
	{
		General &general = m_generals[i];
		general = General();
		general.m_enabled = generals[i].isStartingEnabled();
		general.m_portrait = generals[i].getBioPortraitSmall();
		general.m_name = TheGameText->fetch( generals[i].getBioName() );

		Int templateNum = ThePlayerTemplateStore->getTemplateNumByName( generals[i].getPlayerTemplateName() );
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate( templateNum );
		if( playerTemplate )
		{
			general.m_normal = TheMappedImageCollection->findImageByName( playerTemplate->getMedallionNormal() );
			general.m_selected = TheMappedImageCollection->findImageByName( playerTemplate->getMedallionSelected() );
			general.m_hilite = TheMappedImageCollection->findImageByName( playerTemplate->getMedallionHilite() );
		}
	}

	m_selected = -1;
	m_bioVisible = FALSE;
	m_portrait = nullptr;
	m_portraitLarge = nullptr;
	for( Int i = 0; i < BIO_LINES; ++i )
	{
		m_bioText[i].clear();
		m_bioShown[i].clear();
	}
	m_bioPosition = 0;
	m_bioLength = 0;
	m_gameStarting = FALSE;
	touch();
}

void ChallengeMenuData::showBio( Int general )
{
	if( general < 0 || general >= NUM_GENERALS )
		return;

	m_bioVisible = TRUE;

	const GeneralPersona &persona = TheChallengeGenerals->getChallengeGenerals()[general];
	m_portrait = persona.getBioPortraitSmall();
	m_portraitLarge = persona.getBioPortraitLarge();

	m_bioPosition = 0;
	m_bioText[0] = TheGameText->fetch( persona.getBioName() );
	m_bioText[1] = TheGameText->fetch( persona.getBioRank() );
	m_bioText[2] = TheGameText->fetch( persona.getBioBranch() );
	m_bioText[3] = TheGameText->fetch( persona.getBioStrategy() );

	m_bioLength = 0;
	for( Int i = 0; i < BIO_LINES; ++i )
	{
		m_bioLength += m_bioText[i].getLength();
		m_bioShown[i].clear(); // typeBio() likes it that way
	}
	touch();
}

Bool ChallengeMenuData::typeBio( Int frames )
{
	Bool changed = FALSE;

	for( Int i = 0; i < frames && m_bioPosition < m_bioLength; ++i )
	{
		// find the line the next character belongs to
		Int line = 0;
		Int offset = m_bioPosition;
		while( line < BIO_LINES - 1 && offset >= m_bioText[line].getLength() )
		{
			offset -= m_bioText[line].getLength();
			++line;
		}

		m_bioShown[line].concat( m_bioText[line].getCharAt( offset ) );
		++m_bioPosition;
		changed = TRUE;
	}

	if( changed )
		touch();
	return changed;
}
