#include "GameNetwork/GenOnline/GenOnlineGame.h"
#include "GameLogic/VictoryConditions.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/FileTransfer.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "Common/RandomValue.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/WifiLowLatency.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GenOnline/GenOnlineTransport.h"
#include "Common/GlobalData.h"
#include "GameClient/View.h"
#include "../GenOnlineDefines.h"

int FRAME_GROUPING_CAP = 32;

GenOnlineGameSlot::GenOnlineGameSlot()
{
	GameSlot();
	m_profileID = 0;
	m_wins = 0;
	m_losses = 0;
	m_rankPoints = 0;
	m_favoriteSide = 0;
	m_pingInt = 0;
	m_profileID = 0;
	m_pingStr.clear();
}


GenOnlineGame::GenOnlineGame()
{
	m_isQM = FALSE;

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	cleanUpSlotPointers();

	setLocalIP(0);

	m_ladderIP.clear();
	m_ladderPort = 0;

	enterGame(); // this is done on join in the GS impl, and must be called before setMap

	setMap(pLobbyInterface->GetCurrentLobbyMapPath());

	// init
	//init();

	UpdateSlotsFromCurrentLobby();
}

GenOnlineGame::~GenOnlineGame()
{
	// Force camera to update from config
    TheTacticalView->setDefaultView(DEG_TO_RADF(TheGlobalData->m_cameraPitch),
        DEG_TO_RADF(TheGlobalData->m_cameraYaw),
        1.0f, true);
}

void GenOnlineGame::SyncWithLobby(LobbyEntry& lobby)
{
	// The lobby builds the map path from map_official.
	AsciiString asciiMap(lobby.map_path.c_str());
	asciiMap.toLower();
	setMap(asciiMap);

	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
	if (it != TheMapCache->end())
	{
		getGameSpySlot(0)->setMapAvailability(TRUE);
		setMapCRC(it->second.m_CRC);
		setMapSize(it->second.m_filesize);
	}
	else
	{
		getGameSpySlot(0)->setMapAvailability(FALSE);
	}

	// superweapon
	setSuperweaponRestriction(lobby.limit_superweapons);

	// vanilla teams
	setOldFactionsOnly(lobby.vanilla_teams);

	// stats
	setUseStats(lobby.track_stats);

	// rng seed
	setSeed(lobby.rng_seed);

	// observers
	setAllowObservers(lobby.allow_observers);

	setHasPassword(lobby.passworded);

	setExeCRC(lobby.exe_crc);
	setIniCRC(lobby.ini_crc);

	UnicodeString lobbyName = UnicodeString(from_utf8(lobby.name).c_str());
	setGameName(lobbyName);

	// starting cash
	Money startingCash;
	startingCash.deposit(lobby.starting_cash, FALSE);
	setStartingCash(startingCash);

}

void GenOnlineGame::UpdateSlotsFromCurrentLobby()
{
	// none of this should change while in-game, so ignore

	// NOTE: In progress means game has started, in-game just means in the lobby/fronend...
	if (m_inProgress)
	{
		return;
	}

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	const std::vector<LobbyMemberEntry>& members = pLobbyInterface->GetCurrentLobby().members;

	// A member without a valid slot keeps the previous roster and requests a refresh.
	for (const LobbyMemberEntry& lobbyMember : members)
	{
		if (lobbyMember.user_id != -1 && !lobbyMember.HasValidSlot())
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] UpdateSlotsFromCurrentLobby: user %lld has no valid slot index (%u), keeping the previous roster", lobbyMember.user_id, lobbyMember.m_SlotIndex);
			pLobbyInterface->RequestRosterRefresh();
			return;
		}
	}

	bool claimedSlots[MAX_SLOTS] = {};

	for (const LobbyMemberEntry& pLobbyMember : members)
	{
		if (!pLobbyMember.HasValidSlot())
		{
			continue;
		}

		GenOnlineGameSlot* slot = (GenOnlineGameSlot*)getSlot(pLobbyMember.m_SlotIndex);

		if (slot == nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] UpdateSlotsFromCurrentLobby: getSlot(%u) returned null, skipping", pLobbyMember.m_SlotIndex);
			continue;
		}

		claimedSlots[pLobbyMember.m_SlotIndex] = true;

		// TODO_GENONLINE: Support spectators
		int playerTemplate = -1;
		if (pLobbyMember.side == -1)
		{
			playerTemplate = PLAYERTEMPLATE_RANDOM;
		}
		else
		{
			playerTemplate = pLobbyMember.side;
		}

		// human or AI player
		if (pLobbyMember.m_SlotState != SlotState::SLOT_OPEN && pLobbyMember.m_SlotState != SlotState::SLOT_CLOSED)
		{
			bool bIsAI = (pLobbyMember.m_SlotState == SlotState::SLOT_EASY_AI || pLobbyMember.m_SlotState == SlotState::SLOT_MED_AI|| pLobbyMember.m_SlotState == SlotState::SLOT_BRUTAL_AI);

			// NOTE: Internally generals uses 'local ip' to detect which user is local... we dont have an IP, so just use player index for ip
			slot->setState((SlotState)pLobbyMember.m_SlotState, UnicodeString(from_utf8(pLobbyMember.display_name).c_str()), pLobbyMember.m_SlotIndex);

			slot->setColor(pLobbyMember.color);
			slot->setTeamNumber(pLobbyMember.team);
			slot->setStartPos(pLobbyMember.startpos);
			slot->setPlayerTemplate(playerTemplate);

			if (!bIsAI)
			{
				// ready flag
				if (pLobbyMember.m_bIsReady)
				{
					slot->setAccept();
				}
				else
				{
					slot->unAccept();
				}

				// has map?
				slot->setMapAvailability(pLobbyMember.has_map);

				slot->m_userID = pLobbyMember.user_id;
			}
			else
			{
				slot->setAccept();
				slot->setMapAvailability(true);
				slot->m_userID = -1;
			}
		}
		else
		{
			// handle open/closed
			slot->setState((SlotState)pLobbyMember.m_SlotState);
		}
	}

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!claimedSlots[i])
		{
			GameSlot* slot = getSlot(i);
			if (slot != nullptr)
			{
				slot->setState(SLOT_OPEN);
			}
		}
	}
}


void GenOnlineGame::cleanUpSlotPointers(void)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
		setSlotPointer(i, &m_Slots[i]);
}

GenOnlineGameSlot* GenOnlineGame::getGameSpySlot(Int index)
{
	GameSlot* slot = getSlot(index);
	DEBUG_ASSERTCRASH(slot && (slot == &(m_Slots[index])), ("Bad game slot pointer\n"));
	return (GenOnlineGameSlot*)slot;
}

void GenOnlineGame::init(void)
{
	GameInfo::init();

	UpdateSlotsFromCurrentLobby();
}

void GenOnlineGame::setPingString(AsciiString pingStr)
{
	m_pingStr = pingStr;
	m_pingInt = 0;
}

Bool GenOnlineGame::amIHost(void) const
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	return pLobbyInterface == nullptr ? false : pLobbyInterface->IsHost();
}

void GenOnlineGame::resetAccepted(void)
{
	GameInfo::resetAccepted();
}

Int GenOnlineGame::getLocalSlotNum(void) const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for local game slot while not in game"));
	if (!m_inGame)
		return -1;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface == nullptr)
	{
		return -1;
	}

	Int64 localUserID = pAuthInterface->GetUserID();

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GenOnlineGameSlot* slot = (const GenOnlineGameSlot*)getConstSlot(i);
		if (slot == NULL) {
			continue;
		}
		if (slot->m_userID == localUserID)
		{
			return i;
		}
	}

	return -1;
}

void GenOnlineGame::startGame(Int gameID)
{
	DEBUG_ASSERTCRASH(m_inGame, ("Starting a game while not in game"));
	DEBUG_LOG(("GenOnlineGame::startGame - game id = %d\n", gameID));
	//DEBUG_ASSERTCRASH(m_transport == NULL, ("m_transport is not NULL when it should be"));
	//DEBUG_ASSERTCRASH(TheNAT == NULL, ("TheNAT is not NULL when it should be"));

	UnsignedInt localIP = 1337; // dont care anymore
	setLocalIP(localIP);

	// fill in GS-specific info
	Int numHumans = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (m_Slots[i].isHuman())
		{
			++numHumans;
			AsciiString gsName;
			gsName.translate(m_Slots[i].getName());

			if (m_isQM)
			{
				// TODO_GENONLINE: Does this matter anymore?
				if (getLocalSlotNum() == i)
					m_Slots[i].setProfileID(0);  // hehe - we know our own.  the rest, they'll tell us.
			}
			else
			{
			}
		}
	}

	//#if defined(_DEBUG) || defined(_INTERNAL)
	if (numHumans < 2)
	{
		launchGame();
		
		
	}
	else
		//#endif defined(_DEBUG) || defined(_INTERNAL)
	{
		launchGame();
		// TODO_GENONLINE: Check whether this still matters, the peers are already connected.
		//TheNAT = NEW NAT();
		//TheNAT->attachSlotList(m_slot, getLocalSlotNum(), m_localIP);
		//TheNAT->establishConnectionPaths();
	}
}

AsciiString GenOnlineGame::generateGameSpyGameResultsPacket(void)
{
	return AsciiString();
}

AsciiString GenOnlineGame::generateLadderGameResultsPacket(void)
{
	// TODO_GENONLINE
	AsciiString results;
	return results;
}

void GenOnlineGame::launchGame(void)
{
	// TODO_GENONLINE: Better way of doing this, plus maybe load from file?
#if defined(RTS_DEBUG)
	TheWritableGlobalData->m_benchmarkTimer = 999999999;
	TheWritableGlobalData->m_debugShowGraphicalFramerate = true;
	TheWritableGlobalData->m_showMetrics = true;
#endif

	//TheWritableGlobalData->m_networkPlayerTimeoutTime = 60000;
	//TheWritableGlobalData->m_networkDisconnectScreenNotifyTime = 2500;

	setGameInProgress(TRUE);

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GenOnlineGameSlot* slot = getGameSpySlot(i);
		if (slot->isHuman())
		{
			// TODO_GENONLINE:
			bool bPreordered = false;
			if (bPreordered)
				markPlayerAsPreorder(i);
		}
	}

	// Set up the game network
	AsciiString user;
	AsciiString userList;
	DEBUG_ASSERTCRASH(TheNetwork == NULL, ("For some reason TheNetwork isn't NULL at the start of this game.  Better look into that."));

	if (TheNetwork != NULL) {
		delete TheNetwork;
		TheNetwork = NULL;
	}

	// TODO_GENONLINE: Check whether this still matters, the peers are already connected.
	
	// Time to initialize TheNetwork for this game.
	TheNetwork = NetworkInterface::createNetwork();
	TheNetwork->init();

	// Wi-Fi tuning for the match, released when TheNetwork is destroyed
	if (GenOnlineServicesManager::GetInstance() != nullptr && GenOnlineServicesManager::GetInstance()->GetServiceConfig().connection.wifi_low_latency)
	{
		WifiLowLatency::Begin();
	}
	
	TheNetwork->requireMatchKeys();
	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	if (pMesh == nullptr || !pMesh->BeginMatchKeys())
	{
		delete TheNetwork;
		TheNetwork = nullptr;
		setGameInProgress(FALSE);
		UnicodeString error;
		error.translate("Could not establish authenticated game connections. This transport may not support player authentication.");
		GSMessageBoxOk(TheGameText->fetch("GUI:Error"), error);
		void GenOnlinePopBackToLobby(void);
		GenOnlinePopBackToLobby();
		return;
	}
	// One spike or lossy spell must not start the match at a huge run ahead.
	TheNetwork->SeedLatencyData(pMesh->getSeedProbeLatency());

	// TODO_GENONLINE: Check whether these values still matter.
	TheNetwork->setLocalAddress(getLocalIP(), 8888);

	RegisterGenOnlineTransport();
	TheNetwork->initTransport(GENONLINE_TRANSPORT_NAME);

	TheNetwork->parseUserList(this);

	if (pMesh != nullptr && TheNetwork->GetConnectionManager() != nullptr)
	{
		// the run ahead of frame 1, decided by the packet router from the lobby round trips
		pMesh->BeginStartRunAhead((int)TheNetwork->GetConnectionManager()->getPacketRouterSlot());
	}

	if (TheGameLogic->isInGame()) {
		TheGameLogic->clearGameData();
	}

	Bool filesOk = DoAnyMapTransfers(this);

	// see if we really have the map.  if not, back out.
	TheMapCache->updateCache();
	if (!filesOk || TheMapCache->findMap(getMap()) == NULL)
	{
		DEBUG_LOG(("After transfer, we didn't really have the map.  Bailing...\n"));
		if (TheNetwork != NULL) {
			delete TheNetwork;
			TheNetwork = NULL;
		}
		GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:CouldNotTransferMap"));

		void GenOnlinePopBackToLobby(void);
		GenOnlinePopBackToLobby();
		return;
	}

	// Force camera to update from config
    TheTacticalView->setDefaultView(DEG_TO_RADF(TheGlobalData->m_cameraPitch),
        DEG_TO_RADF(TheGlobalData->m_cameraYaw),
        1.0f,
		false);


	// shutdown the top, but do not pop it off the stack
//		TheShell->hideShell();
	// setup the Global Data with the Map and Seed
	TheWritableGlobalData->m_pendingFile = getMap();

	// send a message to the logic for a new game
	GameMessage* msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_INTERNET);

#if defined(GENONLINE)

	if (GenOnlineServicesManager::Settings.Graphics_LimitFramerate())
	{
		TheWritableGlobalData->m_framesPerSecondLimit = GenOnlineServicesManager::Settings.Graphics_GetFPSLimit();
		TheWritableGlobalData->m_useFpsLimit = true;
	}
	else
	{
		TheWritableGlobalData->m_framesPerSecondLimit = 30000; // game does this... it's not great
		TheWritableGlobalData->m_useFpsLimit = false;
	}
	
#endif
	//TheWritableGlobalData->m_useFpsLimit = false;

	// Set the random seed
	InitRandom(getSeed());
	DEBUG_LOG(("InitGameLogicRandom( %d )\n", getSeed()));


    	// Show map name in the match start communicator hint notification
	{
        GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			LobbyEntry& currentLobby = pLobbyInterface->GetCurrentLobby();

			std::string strMapName = currentLobby.map_name; // NOTE: Includes .map (2) etc
			const std::string strExt = ".map";
			size_t pos = strMapName.find(strExt);
			if (pos != std::string::npos) { strMapName.erase(pos, strExt.size()); }

            UnicodeString msg;
            msg.format(L"Map: %s\nPress F5 or INSERT to open the communicator.", from_utf8(strMapName).c_str());
            GenOnlineShowNotificationBox(AsciiString::TheEmptyString, msg, false);
		}

		
	}
}

void GenOnlineGame::reset(void)
{
	GameInfo::reset();
	m_isQM = FALSE;
}

void GenOnlineGame::SetStartPhase(EStartPhase phase)
{
	if (phase != m_startPhase)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Start phase %d -> %d", (int)m_startPhase, (int)phase);
	}

	m_startPhase = phase;
	m_startPhaseTime = NowMs();
}

void GenOnlineGame::AbortStart()
{
	if (m_startPhase != EStartPhase::Starting)
	{
		SetStartPhase(EStartPhase::Idle);
	}
}

void GenOnlineGame::StartCountdown(int64_t durationMs)
{
	SetStartPhase(EStartPhase::Countdown);
	m_countdownEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
}

int64_t GenOnlineGame::GetCountdownRemainingMs() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(m_countdownEnd - std::chrono::steady_clock::now()).count();
}

Bool GenOnlineGame::canKickOnObserversDisabled()
{
	if (getAllowObservers() || TheGameLogic->getGameMode() != GAME_INTERNET || TheGameLogic->getFrame() < 2)
		return FALSE;

	Player *localPlayer = ThePlayerList->getLocalPlayer();
	if (!localPlayer || localPlayer->isPlayerObserver() || !TheVictoryConditions->hasSinglePlayerBeenDefeated(localPlayer))
		return FALSE;

	if (!amIHost())
		return TRUE;

	// Host is only kicked if an alive ally exists
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (!otherPlayer || otherPlayer == localPlayer)
			continue;

		if (otherPlayer->getRelationship(localPlayer->getDefaultTeam()) == ALLIES && !TheVictoryConditions->hasSinglePlayerBeenDefeated(otherPlayer))
			return TRUE;
	}

	return FALSE;
}
