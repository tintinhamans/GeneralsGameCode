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

// FILE: LanLobbyActions.cpp ////////////////////////////////////////////////////
// See LanLobbyActions.h.

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/LanLobbyActions.h"

#include "Common/UnicodeString.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"

void LanLobbyActions::hostGame()
{
	TheLAN->RequestGameCreate( L"", FALSE );
}

void LanLobbyActions::joinGame( LANGameInfo *game )
{
	if ( game )
		TheLAN->RequestGameJoin( game );
}

void LanLobbyActions::directConnect()
{
	TheLAN->RequestLobbyLeave( false );
}

UnicodeString LanLobbyActions::sanitizeName( const UnicodeString &rawInput )
{
	UnicodeString txtInput = rawInput;

	// Skip leading whitespace (see GEM_UPDATE_TEXT).
	const WideChar *c = txtInput.str();
	while ( c && iswspace(*c) )
		c++;

	if ( c )
		txtInput = UnicodeString(c);
	else
		txtInput = UnicodeString::TheEmptyString;

	txtInput.truncateTo(g_lanPlayerNameLength);

	// strtok delimiters can't be in names.
	if ( !txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L',' )
		txtInput.removeLastChar();

	if ( !txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L':' )
		txtInput.removeLastChar();

	if ( !txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L';' )
		txtInput.removeLastChar();

	return txtInput;
}

void LanLobbyActions::setName( const UnicodeString &sanitizedName, const UnicodeString &defaultName )
{
	if ( !sanitizedName.isEmpty() )
		TheLAN->RequestSetName( sanitizedName );
	else
		TheLAN->RequestSetName( defaultName );
}

UnicodeString LanLobbyActions::sendChatEntry( const UnicodeString &rawInput )
{
	UnicodeString txtInput = rawInput;

	// Trim leading whitespace only (see GEM_EDIT_DONE).
	while ( !txtInput.isEmpty() && iswspace(txtInput.getCharAt(0)) )
		txtInput = UnicodeString(txtInput.str()+1);

	if ( !txtInput.isEmpty() )
		TheLAN->RequestChat( txtInput, LANAPIInterface::LANCHAT_NORMAL );

	return txtInput;
}

UnicodeString LanLobbyActions::sendChatButton( const UnicodeString &rawInput )
{
	UnicodeString txtInput = rawInput;
	txtInput.trim(); // both ends (see ButtonEmote's GBM_SELECTED handler)

	if ( !txtInput.isEmpty() )
		TheLAN->RequestChat( txtInput, LANAPIInterface::LANCHAT_NORMAL );

	return txtInput;
}

UnicodeString LanLobbyActions::sendEmote( const UnicodeString &rawInput )
{
	UnicodeString txtInput = rawInput;
	txtInput.trim();

	if ( !txtInput.isEmpty() )
		TheLAN->RequestChat( txtInput, LANAPIInterface::LANCHAT_EMOTE );

	return txtInput;
}
