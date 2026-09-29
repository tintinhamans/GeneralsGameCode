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

// FILE: OnlineGameSetupData.cpp /////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/OnlineGameSetupData.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

//-------------------------------------------------------------------------------------------------
OnlineGameSetupConnectionInfo OnlineGameSetupData::computeConnectionInfo( NGMP_OnlineServices_LobbyInterface *pLobbyInterface, NGMPGame *game, NGMPGameSlot *slot )
{
	OnlineGameSetupConnectionInfo info;

	if( !pLobbyInterface || !game || !slot )
		return info;

	int64_t localPlayerID = -1;
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if( pAuthInterface != nullptr )
		localPlayerID = pAuthInterface->GetUserID();

	info.m_isLocalPlayer = ( localPlayerID == slot->m_userID );

	LobbyMemberEntry member = pLobbyInterface->GetRoomMemberFromID( slot->m_userID );
	info.m_region = member.region;

	if( !info.m_isLocalPlayer && NGMP_OnlineServicesManager::GetNetworkMesh() != nullptr )
	{
		PlayerConnection* pConnection = NGMP_OnlineServicesManager::GetNetworkMesh()->GetConnectionForUser( slot->m_userID );
		if( pConnection != nullptr )
		{
			info.m_connectionType = pConnection->GetConnectionType();
			info.m_state = pConnection->GetState();
			info.m_isConnected = ( info.m_state == EConnectionState::CONNECTED_DIRECT );
			info.m_score = pConnection->ComputeConnectionScore();
			info.m_latencyMs = pConnection->GetLatency();
			info.m_jitterMs = pConnection->GetJitter();
			float rawQuality = pConnection->GetConnectionQuality();
			info.m_qualityPct = ( rawQuality >= 0.0f ) ? static_cast<Int>( rawQuality * 100.0f ) : -1;
		}
	}

	return info;
}

//-------------------------------------------------------------------------------------------------
OnlineGameSetupData OnlineGameSetupData::build( NGMPGame *game )
{
	OnlineGameSetupData data;

	if( !game )
		return data;

	GameSetupData setup = GameSetupData::build( game, game->getAllowObservers() );
	data.m_options = setup.m_options;
	data.m_isHost = game->amIHost();
	data.m_gameName = game->getGameName();
	data.m_useStats = game->getUseStats();
	data.m_limitArmies = game->oldFactionsOnly();

	// GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES is always on for this fork (see
	// WOLGameSetupMenuInit()/WOLDisplayGameOptions()), so the stats-match lockout never triggers.
	data.m_cashAndSuperweaponsEnabled = data.m_isHost;

	// Mirrors WOLDisplayGameOptions()'s TextEntryMapDisplay text exactly, including its
	// double reverseFind() call.
	const GameSlot *localSlot = game->getLocalSlotNum() >= 0 ? game->getConstSlot( game->getLocalSlotNum() ) : NULL;
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( game->getMap() ) : NULL;
	if( md && localSlot && localSlot->hasMap() )
	{
		data.m_mapDisplayText = md->m_displayName;
	}
	else
	{
		AsciiString s = game->getMap();
		if( s.reverseFind( '\\' ) )
			s = s.reverseFind( '\\' ) + 1;
		data.m_mapDisplayText.translate( s );
	}

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

	data.m_slots.resize( MAX_SLOTS );
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		OnlineGameSetupSlotRow &row = data.m_slots[i];
		row.m_base = setup.m_slots[i];
		row.m_isHostSlot = ( i == 0 );

		NGMPGameSlot *slot = game->getGameSpySlot( i );
		if( !slot )
			continue;

		row.m_accepted = slot->isAccepted();
		row.m_hasMap = slot->hasMap();

		if( slot->isHuman() && pLobbyInterface != nullptr )
			row.m_connection = computeConnectionInfo( pLobbyInterface, game, slot );
	}

	return data;
}
