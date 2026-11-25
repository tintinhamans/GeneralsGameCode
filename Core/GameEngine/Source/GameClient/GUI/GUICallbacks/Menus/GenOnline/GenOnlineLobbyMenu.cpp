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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// Author: Chris Huybregts, November 2001
// Description: WOL Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GenOnlineChatWrap.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/GenOnlineUserSection.h"

#include "Common/GameEngine.h"
#include "Common/GOUIColors.h"
#include "Common/GameState.h"
#include "Common/MiniLog.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/version.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameClient.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GUI/ChatCompletionSets.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/Display.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GenOnlineLobbyGameDetails.h"

#include "GameLogic/GameLogic.h"

#include "GameClient/LanguageFilter.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

void GenOnlineRefreshGameList( Bool forceRefresh = FALSE );
void GenOnlineRefreshPlayerList( Bool forceRefresh = FALSE );

#ifdef DEBUG_LOGGING
#define PERF_TEST
static LogClass s_perfLog("Perf.txt");
#define PERF_LOG(x) s_perfLog.log x
#else // DEBUG_LOGGING
#define PERF_LOG(x)
#endif // DEBUG_LOGGING

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = nullptr;
static Bool raiseMessageBoxes = false;
static UnsignedInt s_lobbyMenuGeneration = 0;
static time_t gameListRefreshTime = 0;
static const time_t gameListRefreshInterval = 4000;
static time_t playerListRefreshTime = 0;
static const time_t playerListRefreshInterval = 4000;

void GenOnlineSetUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id);
static void doSliderTrack(GameWindow *control, Int val);
extern Bool DontShowMainMenu;
enum { COLUMN_PLAYERNAME = 1 };

// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonRefreshID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType buttonBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonChatID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyPlayersID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyChatID = NAMEKEY_INVALID;
static NameKeyType comboLobbyGroupRoomsID = NAMEKEY_INVALID;
//static NameKeyType // sliderChatAdjustID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLLobby = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonHost = nullptr;
static GameWindow *buttonRefresh = nullptr;
static GameWindow *buttonJoin = nullptr;
static GameWindow *buttonBuddy = nullptr;
static GameWindow *buttonChat = nullptr;
static GameWindow *textEntryChat = nullptr;
static NameKeyType textEntrySearchID = NAMEKEY_INVALID;
static GameWindow *textEntrySearch = nullptr;

// Read by the lobby list population in LobbyUtils.cpp.
UnicodeString theLobbySearchText;
static GameWindow *listboxLobbyPlayers = nullptr;
static GameWindow *listboxLobbyChat = nullptr;
static GameWindow *comboLobbyGroupRooms = nullptr;
// GenOnline @tweak arcticdolphin 22/11/2026 The game list filter has its own combo box next to the search field.
static NameKeyType comboLobbyFilterID = NAMEKEY_INVALID;
static GameWindow *comboLobbyFilter = nullptr;
static GameWindow *parent = nullptr;

static ChatTabCycler s_lobbyChatCycler;
static std::vector<std::wstring> s_lobbyRecentChatters;

static Int groupRoomToJoin = 0;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;

// Preserve rejected messages while the server enforces the limit.
static std::deque<std::chrono::steady_clock::time_point> s_lobbyChatMessageTimes;

static Bool s_lobbyMenuOpen = FALSE;

static bool LobbyChatRateLimitAllowsSend()
{
	using namespace std::chrono;

	const auto now = steady_clock::now();
	const auto window = seconds(9);
	while (!s_lobbyChatMessageTimes.empty() && now - s_lobbyChatMessageTimes.front() >= window)
	{
		s_lobbyChatMessageTimes.pop_front();
	}

	if (s_lobbyChatMessageTimes.size() >= 3)
	{
		ShowChatRateLimitNotice(
			"Rate limit: Please wait before sending another message.",
			"room");
		return false;
	}

	s_lobbyChatMessageTimes.push_back(now);
	return true;
}

#if defined(RTS_DEBUG)
Bool g_genOnlineFakeCRC = FALSE;
Bool g_genOnlineDebugSlots = FALSE;
#endif

std::list<PeerResponse> TheGenOnlineLobbyQueuedUTMs;

// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus();
}
extern int isThreadHosting;

static const wchar_t* netRelaySetting()
{
	extern int g_relayOverride;
	if (g_relayOverride == 1)
	{
		return L"force (relay only)";
	}
	GenOnlineServicesManager* manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		return L"auto (service setting unavailable)";
	}
	return GnsNetworkConfig::ResolveRelayOnly(manager->GetServiceConfig().connection.relay_only, -1)
		? L"auto (service requires relay)" : L"auto (service allows direct connections)";
}

static const wchar_t* netGnsIceSetting()
{
	extern int g_iceImplementationOverride;
	if (g_iceImplementationOverride == 1)
	{
		return L"native";
	}
	if (g_iceImplementationOverride == 2)
	{
		return L"webrtc";
	}
	GenOnlineServicesManager* manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr)
	{
		return L"auto (service setting unavailable)";
	}
	switch (GnsNetworkConfig::ResolveIceImplementation(manager->GetServiceConfig().connection.ice_engine, -1))
	{
	case 0: return L"auto (service selects the library default)";
	case 1: return L"auto (service selects native)";
	default: return L"auto (service selects webrtc)";
	}
}

// The transport setting. The service reads the offered plugin transports when it plans a pair.
static UnicodeString netTransportSetting()
{
	extern int g_transportOverride;
	if (g_transportOverride == 1)
	{
		return UnicodeString(L"gns only");
	}
	const std::string pluginTransport = GenOnlinePluginHost::Transport::GetName();
	if (pluginTransport.empty())
	{
		return UnicodeString(L"auto (no plugin transport available)");
	}
	UnicodeString setting;
	setting.format(L"auto (offers %hs)", pluginTransport.c_str());
	return setting;
}

static UnicodeString netTransportChoices()
{
	const std::string pluginTransport = GenOnlinePluginHost::Transport::GetName();
	if (pluginTransport.empty())
	{
		return UnicodeString(L"gns");
	}
	UnicodeString choices;
	choices.format(L"gns, %hs", pluginTransport.c_str());
	return choices;
}

// GNS and the plugin transport both take it through g_relayOverride when the next lobby mesh starts.
static void applyNetRelaySetting(int wanted)
{
	extern int g_relayOverride;
	g_relayOverride = wanted;
}

void GenOnlineHandleNetSlashCommand(AsciiString subcommand, AsciiString value, GameWindow* listbox)
{
	extern int g_iceImplementationOverride;
	// The websocket tick sends SetPeerTransports again when this changes the offered list.
	extern int g_transportOverride;

	const Color netColor = GOUIColor(GOUICOLOR_HILITE, GameMakeColor(255, 255, 0, 255));
	UnicodeString line;

	const PeerNetCommand command = ParsePeerNetCommand(subcommand.str(), value.str());
	switch (command.action)
	{
	case PeerNetAction::Reset:
		applyNetRelaySetting(-1);
		g_iceImplementationOverride = -1;
		g_transportOverride = -1;
		line.format(L"Next lobby: transport %ls; relay %ls; GNS ICE %ls. Existing connections are unchanged.",
			netTransportSetting().str(), netRelaySetting(), netGnsIceSetting());
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::TransportUsage:
	{
		const std::string pluginTransport = GenOnlinePluginHost::Transport::GetName();
		const bool bPluginName = !command.word.empty() && command.word == pluginTransport;
		line.format(L"%lsNext lobby transport: %ls. Available transports: %ls. Usage: /net transport auto | gns",
			bPluginName ? L"Forcing a plugin transport is not supported. " : L"",
			netTransportSetting().str(), netTransportChoices().str());
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;
	}

	case PeerNetAction::SetTransport:
		g_transportOverride = command.value;
		line.format(L"New connections: transport %ls. Existing connections are unchanged.%ls", netTransportSetting().str(),
			(command.value == 1) ? L" Use /net transport auto to undo this." : L"");
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::RelayUsage:
		line.format(L"Next lobby relay: %ls. Usage: /net relay auto | force", netRelaySetting());
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::SetRelay:
		applyNetRelaySetting(command.value);
		line.format(L"Next lobby: relay %ls. Existing connections are unchanged.%ls", netRelaySetting(),
			(command.value == 1) ? L" Use /net relay auto to undo this." : L"");
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::GnsIceUsage:
		line.format(L"Next lobby GNS ICE: %ls. Usage: /net gns ice auto | native | webrtc", netGnsIceSetting());
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::SetGnsIce:
		g_iceImplementationOverride = command.value;
		line.format(L"Next lobby GNS ICE: %ls. Existing connections are unchanged.%ls", netGnsIceSetting(),
			(command.value == -1) ? L"" : L" Use /net gns ice auto to undo this.");
		GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		return;

	case PeerNetAction::Usage:
		GadgetListBoxAddEntryText(listbox, UnicodeString(L"Usage: /net, /net transport auto | gns, /net relay auto | force, /net gns ice auto | native | webrtc, /net reset"), netColor, -1, -1);
		return;

	case PeerNetAction::Status:
		break;
	}

	line.format(L"Next lobby: transport %ls; relay %ls; GNS ICE %ls. Existing connections are unchanged.",
		netTransportSetting().str(), netRelaySetting(), netGnsIceSetting());
	GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);

	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pMesh == nullptr || pMesh->GetAllConnections().empty())
	{
		GadgetListBoxAddEntryText(listbox, UnicodeString(L"You are not connected to any players. Join or host a lobby to see how you reach each player."), netColor, -1, -1);
	}
	else
	{
		for (auto& kvPair : pMesh->GetAllConnections())
		{
			PlayerConnection& conn = kvPair.second;

			std::wstring strName = L"Unknown player";
			if (pLobbyInterface != nullptr)
			{
				for (const LobbyMemberEntry& member : pLobbyInterface->GetCurrentLobby().members)
				{
					if (member.user_id == kvPair.first)
					{
						strName = from_utf8(member.display_name);
						break;
					}
				}
			}

			const std::string strRoute = conn.GetRouteLabel();
			if (conn.GetState() == EConnectionState::CONNECTED_DIRECT)
			{
				line.format(L"%ls: %hs, ping %d ms", strName.c_str(), strRoute.c_str(), conn.GetLatency());
			}
			else
			{
				line.format(L"%ls: %hs", strName.c_str(), strRoute.c_str());
			}
			GadgetListBoxAddEntryText(listbox, line, netColor, -1, -1);
		}
	}

	GadgetListBoxAddEntryText(listbox, UnicodeString(L"Change with /net transport auto | gns, /net relay auto | force and /net gns ice auto | native | webrtc. /net reset clears every override."), netColor, -1, -1);
}

Bool GenOnlineHandleLobbySlashCommands(UnicodeString uText, Bool *wasRateLimited);

Bool GenOnlineLobbyMenuSubmitChatLine( UnicodeString text )
{
	text.trim();
	if (!s_lobbyMenuOpen || text.isEmpty())
		return FALSE;

	Bool wasRateLimited = FALSE;
	if (GenOnlineHandleLobbySlashCommands(text, &wasRateLimited))
		return !wasRateLimited;

	if (!LobbyChatRateLimitAllowsSend())
		return FALSE;

	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRoomsInterface == nullptr)
		return FALSE;

	pRoomsInterface->SendChatMessageToCurrentRoom(text, false);
	return TRUE;
}

Bool GenOnlineHandleLobbySlashCommands(UnicodeString uText, Bool *wasRateLimited)
{
	if (wasRateLimited != nullptr)
		*wasRateLimited = FALSE;

	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		return TRUE; // was a slash command
	}
	else if (token == "me" && uText.getLength()>4)
	{
		if (!LobbyChatRateLimitAllowsSend())
		{
			if (wasRateLimited != nullptr)
				*wasRateLimited = TRUE;
			return TRUE;
		}

		UnicodeString msg = UnicodeString(uText.str() + 4); // skip the /me
		GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
		if (pRoomsInterface != nullptr)
		{
			pRoomsInterface->SendChatMessageToCurrentRoom(msg, true);
		}
		return TRUE; // was a slash command
	}
	else if (token == "help" || token == "commands")
	{
		const Color helpColor = GOUIColor(GOUICOLOR_HELP, GameMakeColor(127, 127, 127, 255));
		GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/me <message> - Send an emote."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/name <value> - Changes your display name - Example: /name General Granger. You can also use /nick."), helpColor, -1, -1);
		// GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/refresh - Refresh the game and player lists."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/net - Show your connection settings. /net transport, /net relay, /net gns ice and /net reset change them."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/support - Open the Generals Online Discord."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"/help - Show these commands. You can also use /commands."), helpColor, -1, -1);
		return TRUE; // was a slash command
	}
	else if (token == "support")
	{
		ShellExecuteA(NULL, "open", "https://discord.generals.online", NULL, NULL, SW_SHOWNORMAL);
		return TRUE; // was a slash command
	}
	else if ((token == "name" && uText.getLength() > 6) || (token == "nick" && uText.getLength() > 6))
	{
		UnicodeString newName(uText.str() + 6); // skip the /name or nick

		if (newName.getLength() < 3 || newName.getLength() > 16)
		{
			GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"Your new name must be between 3 and 16 characters."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
		}
		else
		{
			std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();
			if (pWS != nullptr)
			{
				pWS->SendData_ChangeName(newName);
			}
		}
		
		return TRUE; // was a slash command
	}
	else if (token == "net")
	{
		AsciiString subcommand;
		AsciiString value;
		remainder.nextToken(&subcommand);
		// The value is the rest of the line because /net gns ice takes two words.
		value = remainder;
		value.trim();
		subcommand.toLower();
		value.toLower();
		GenOnlineHandleNetSlashCommand(subcommand, value, listboxLobbyChat);
		return TRUE; // was a slash command
	}
	else if (token == "refresh")
	{
		// Added 2/19/03 added the game refresh
		GenOnlineRefreshGameList(TRUE);
		GenOnlineRefreshPlayerList(TRUE);
		return TRUE; // was a slash command
	}
#if defined(RTS_DEBUG)
	else if (token == "fakecrc")
	{
		g_genOnlineFakeCRC = !g_genOnlineFakeCRC;
		return TRUE; // was a slash command
	}
	else if (token == "slots")
	{
		g_genOnlineDebugSlots = !g_genOnlineDebugSlots;
		return TRUE; // was a slash command
	}
#endif

	GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"Unknown command: Use /help to see all commands."), GameSpyColor[GSCOLOR_CHAT_NORMAL], -1, -1);
	return TRUE; // was a slash command
}

static Bool s_tryingToHostOrJoin = FALSE;
void GenOnlineSetLobbyAttemptHostJoin(Bool start)
{
	s_tryingToHostOrJoin = start;
}

// Tooltips -------------------------------------------------------------------------------

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);//TheGameText->fetch("TOOLTIP:PlayersInLobby") );
		return;
	}

	UnicodeString uName = GadgetListBoxGetText(window, row, COLUMN_PLAYERNAME);

	// TODO_GENONLINE: Duplicate names break this lookup, look players up by id or only allow unique names.
	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pRoomsInterface != nullptr && pAuthInterface != nullptr && pStatsInterface != nullptr && pSocialInterface != nullptr)
	{
		int profileID = (int)GadgetListBoxGetItemData(listboxLobbyPlayers, row, 0);
		NetworkRoomMember* roomMember = pRoomsInterface->GetRoomMemberFromID(profileID);

		// TODO_GENONLINE: This call is async, so further popups should wait until it returns.
		if (col > 0)
		{
			if (roomMember != nullptr)
			{
				// new
				const int64_t userID = roomMember->user_id;
				const UnsignedInt generation = s_lobbyMenuGeneration;
				pStatsInterface->findPlayerStatsByID(userID, [=](bool bSuccess, PSPlayerStats stats)
					{
						if (generation != s_lobbyMenuGeneration)
							return;

						if (!bSuccess)
						{
							TheMouse->setCursorTooltip(UnicodeString(L"Error: 1"), -1, NULL, 1.5f);
						}
						else
						{
							UnicodeString tooltip = UnicodeString::TheEmptyString;
							if (userID == pAuthInterface->GetUserID())
							{
								tooltip.format(TheGameText->fetch("TOOLTIP:LocalPlayer"), uName.str());
							}
							else
							{
								// not us
								bool bIsFriend = pSocialInterface->IsUserFriend(userID);
								if (bIsFriend)
								{
									// buddy
									tooltip.format(TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:GOFriendPlayer", L"Your friend %s"), uName.str());
								}
								else
								{
									// non-buddy profiled player
									tooltip.format(TheGameText->fetch("TOOLTIP:ProfiledPlayer"), uName.str());

									// NOTE: Removed non-profiled generic player, this doesn't exist on Generals Online, everyone has a profile
								}
							}

							bool bIgnored = pSocialInterface->IsUserIgnored(userID);
							if (bIgnored)
							{
								tooltip.concat(TheGameText->fetch("TOOLTIP:IgnoredModifier"));
							}

							// ELO data
							UnicodeString tmp;
							tmp.format(L"\n\nOverall Elo Rating: %d (in %d matches)", stats.elo_rating, stats.elo_num_matches);
							tooltip.concat(tmp);
#if defined(WORLD_SERIES_CLIENT)
							tmp.format(L"\nWS Elo Rating: %d", stats.monthly_elo_rating);
#else
							tmp.format(L"\nMonthly Elo Rating: %d", stats.monthly_elo_rating);
#endif
							tooltip.concat(tmp);
							Int rankPoints = GenOnlineCalculateRank(stats);
							Int rank = 0;
							Int i = 0;
							if (TheRankPointValues != nullptr)
							{
								while (i + 1 < MAX_RANKS && rankPoints >= TheRankPointValues->m_ranks[i + 1])
									++i;
							}
							rank = i;

							// determine favorite side
							Int mostGames = 0;
							Int favorite = 0;
							for (auto it = stats.games.begin(); it != stats.games.end(); ++it)
							{
								if (it->second >= mostGames)
								{
									mostGames = it->second;
									favorite = it->first;
								}
							}

							AsciiString sideName = "GUI:RandomSide";
							if (mostGames > 0)
							{
								if (favorite > 1) // cant be civilian or observer
								{
									const PlayerTemplate* fac = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);
									if (fac)
									{
										sideName.format("SIDE:%s", fac->getSide().str());
									}
								}
							}
							AsciiString rankName;
							rankName.format("GUI:GSRank%d", rank);
							
							tmp.clear();
							tmp.format(L"\n\nFavorite Side: %ls\nRank: %ls", TheGameText->fetch(sideName).str(), TheGameText->fetch(rankName).str());
							tooltip.concat(tmp);

							int totalWins = 0;
							int totalLosses = 0;
							int totalDC = 0;
							int totalWinsInRow = 0;
							int totalLossesInRow = 0;
							int totalDCInRow = 0;
							int maxWinsInRow = 0;
							int maxLossesInRow = 0;
							int maxDCInRow = 0;

							for (int i = 0; i < stats.wins.size(); ++i) { totalWins += stats.wins[i]; }
							for (int i = 0; i < stats.losses.size(); ++i) { totalLosses += stats.losses[i]; }
							for (int i = 0; i < stats.discons.size(); ++i) { totalDC += stats.discons[i]; }

                            totalWinsInRow = stats.winsInARow;
                            totalLossesInRow = stats.lossesInARow;
                            totalDCInRow = stats.disconsInARow;

                            maxWinsInRow = stats.maxWinsInARow;
                            maxLossesInRow = stats.maxLossesInARow;

							tmp.clear();
							tmp.format(L"\n\nTotal Wins: %d\nTotal Losses: %d\nTotal Disconnects: %d\n\nCurrent Win Streak: %d\nCurrent Loss Streak: %d\nCurrent Disconnect Streak: %d\n\nLongest Win Streak: %d\nLongest Loss Streak: %d\nLongest Disconnect Streak: %d",
								totalWins,
								totalLosses,
								totalDC,
								totalWinsInRow,
								totalLossesInRow,
								totalDCInRow,
								maxWinsInRow,
								maxLossesInRow,
								maxDCInRow);
							tooltip.concat(tmp);

							if (pRoomsInterface != nullptr && pAuthInterface != nullptr)
							{
								NetworkRoomMember* localMember = pRoomsInterface->GetRoomMemberFromID(pAuthInterface->GetUserID());
								if (localMember != nullptr && localMember->m_bIsAdmin)
								{
									UnicodeString idLine;
									idLine.format(L"\n\nUser ID: %lld", userID);
									tooltip.concat(idLine);
								}
							}

							TheMouse->setCursorTooltip(tooltip, -1, NULL, 1.5f); // the text and width are the only params used.  the others are the default values.
						}
					}, EStatsRequestPolicy::RESPECT_CACHE_ALLOW_REQUEST);
			}
			else
			{
				TheMouse->setCursorTooltip(UnicodeString(L"Error: 1"), -1, NULL, 1.5f); // the text and width are the only params used.  the others are the default values.
			}



		}
	}


	return;

}

// Set while repopulating the combo box, because setting the selection re-sends GCM_SELECTED as if the user had picked it.
static Bool s_populatingLobbyCombo = FALSE;

static Int FindRoomIndexByID(const std::vector<NetworkRoom>& rooms, Int roomID)
{
	for (Int roomIndex = 0; roomIndex < (Int)rooms.size(); ++roomIndex)
	{
		if (rooms[roomIndex].GetRoomID() == roomID)
		{
			return roomIndex;
		}
	}

	return -1;
}

static Bool RoomHasLaterSibling(const std::vector<NetworkRoom>& rooms, Int roomIndex)
{
	const Int parentRoomID = rooms[roomIndex].GetParentRoomID();
	for (Int siblingIndex = roomIndex + 1; siblingIndex < (Int)rooms.size(); ++siblingIndex)
	{
		if (rooms[siblingIndex].GetParentRoomID() == parentRoomID)
		{
			return TRUE;
		}
	}

	return FALSE;
}

static UnicodeString FormatRoomLabel(const std::vector<NetworkRoom>& rooms, Int roomIndex)
{
	UnicodeString label;
	const NetworkRoom& room = rooms[roomIndex];
	std::vector<Int> ancestors;
	Int parentRoomID = room.GetParentRoomID();

	while (parentRoomID >= 0 && ancestors.size() < rooms.size())
	{
		const Int parentIndex = FindRoomIndexByID(rooms, parentRoomID);
		if (parentIndex < 0)
		{
			break;
		}

		ancestors.push_back(parentIndex);
		parentRoomID = rooms[parentIndex].GetParentRoomID();
	}

	std::reverse(ancestors.begin(), ancestors.end());
	for (size_t ancestorIndex = 1; ancestorIndex < ancestors.size(); ++ancestorIndex)
	{
		label.concat(RoomHasLaterSibling(rooms, ancestors[ancestorIndex]) ? L"\u2502  " : L"   ");
	}

	if (!ancestors.empty())
	{
		label.concat(RoomHasLaterSibling(rooms, roomIndex) ? L"\u251C\u2500 " : L"\u2514\u2500 ");
	}
	label.concat(room.GetRoomDisplayName());
	return label;
}

static void PopulateLobbyFilterComboBox(GameWindow* comboBox)
{
	if (comboBox == nullptr)
		return;

	s_populatingLobbyCombo = TRUE;
	GadgetComboBoxReset(comboBox);

	Int idx;
	Int selectedRoomIdx = -1;
	UnicodeString selectedRoomName;

	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRoomsInterface != nullptr)
	{
		const std::vector<NetworkRoom> rooms = pRoomsInterface->GetGroupRooms();
		const Int currentRoomIndex = pRoomsInterface->GetCurrentRoomIndex();
		const Int numRooms = (Int)rooms.size();
		for (Int i = 0; i < numRooms; ++i)
		{
			const UnicodeString roomLabel = FormatRoomLabel(rooms, i);
			idx = GadgetComboBoxAddEntry(comboBox, roomLabel,
				GameSpyColor[i == currentRoomIndex ? GSCOLOR_CURRENTROOM : GSCOLOR_ROOM]);
			// Room entries use values below the negative separator value.
			GadgetComboBoxSetItemData(comboBox, idx, (void*)(intptr_t)(-(i + 2)));

			if (i == currentRoomIndex)
			{
				selectedRoomIdx = idx;
				selectedRoomName = rooms[i].GetRoomDisplayName();
			}
		}

	}

	GadgetComboBoxSetSelectedPos(comboBox, selectedRoomIdx);
	if (selectedRoomIdx >= 0)
	{
		GadgetComboBoxSetText(comboBox, selectedRoomName);
	}
	s_populatingLobbyCombo = FALSE;
}

// GenOnline @tweak arcticdolphin 22/11/2026 The game list filter combo, one entry per filter with the active one selected.
static void PopulateGameFilterComboBox(GameWindow* comboBox)
{
	if (comboBox == nullptr)
		return;

	extern LobbyGameModeFilter theLobbyFilter;
	static const struct
	{
		const char* key;
		const wchar_t* label;
		LobbyGameModeFilter filter;
	} filterEntries[] =
	{
		{ "GUI:GOFilterAll",		L"All games",		LOBBY_FILTER_ALL },
		{ "GUI:GOFilterJoinable",	L"Joinable",		LOBBY_FILTER_JOINABLE },
		{ "GUI:GOFilter1v1",		L"1v1",				LOBBY_FILTER_1V1 },
		{ "GUI:GOFilterTeam",		L"Team games",		LOBBY_FILTER_TEAM },
		{ "GUI:GOFilterFFA",		L"FFA",				LOBBY_FILTER_FFA },
		{ "GUI:GOFilterAOD",		L"AOD",				LOBBY_FILTER_AOD },
		{ "GUI:GOFilterFriends",	L"Friends' games",	LOBBY_FILTER_BUDDIES },
	};

	s_populatingLobbyCombo = TRUE;
	GadgetComboBoxReset(comboBox);
	Int selected = 0;
	for (const auto& filterEntry : filterEntries)
	{
		const UnicodeString label = TheGameText->FETCH_OR_SUBSTITUTE(filterEntry.key, filterEntry.label);
		const Int idx = GadgetComboBoxAddEntry(comboBox, label, GameSpyColor[GSCOLOR_DEFAULT]);
		GadgetComboBoxSetItemData(comboBox, idx, (void*)(intptr_t)filterEntry.filter);
		if (filterEntry.filter == theLobbyFilter)
			selected = idx;
	}
	GadgetComboBoxSetSelectedPos(comboBox, selected);
	s_populatingLobbyCombo = FALSE;
}

static void RememberRoomChatAuthor(uint32_t key)
{
	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pRoomsInterface == nullptr || pAuthInterface == nullptr)
		return;

	std::string messageID;
	int64_t authorID = 0;
	if (!pRoomsInterface->FindChatMessage(key, messageID, authorID) || authorID == pAuthInterface->GetUserID())
		return;

	const NetworkRoomMember* pMember = pRoomsInterface->GetRoomMemberFromID(authorID);
	if (pMember == nullptr)
		return;

	AddRecentChatter(s_lobbyRecentChatters, from_utf8(pMember->display_name), pAuthInterface->GetDisplayNameW());
}

// A chat message line keeps its key as item data, so the row is found again after older rows scroll out.
static void AddRoomChatMessageLine(UnicodeString strMessage, Color color, uint32_t key)
{
	RememberRoomChatAuthor(key);
	// GenOnline @bugfix arcticdolphin 22/11/2026 A word wider than the chat list is broken so the line wraps fully.
	const Int row = GadgetListBoxAddEntryText(listboxLobbyChat, GenOnlineBreakLongWords(listboxLobbyChat, strMessage), color, -1, -1);
	if (row >= 0)
	{
		GadgetListBoxSetItemData(listboxLobbyChat, reinterpret_cast<void*>(static_cast<uintptr_t>(key)), row, 0);
	}
}

static void RemoveRoomChatMessageLine(uint32_t key)
{
	if (listboxLobbyChat == nullptr)
	{
		return;
	}
	const Int numEntries = GadgetListBoxGetNumEntries(listboxLobbyChat);
	for (Int row = 0; row < numEntries; ++row)
	{
		if (reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(listboxLobbyChat, row, 0)) == key)
		{
			TheWindowManager->winSendSystemMsg(listboxLobbyChat, GLM_DEL_ENTRY, static_cast<WindowMsgData>(row), 0);
			return;
		}
	}
}

static void HandleNetworkRoomChanged(int roomIndex, bool effectiveRoomChanged)
{
	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRoomsInterface == nullptr)
		return;

	const std::vector<NetworkRoom>& rooms = pRoomsInterface->GetGroupRooms();
	if (roomIndex < 0 || roomIndex >= (int)rooms.size())
		return;

	if (effectiveRoomChanged)
	{
		GadgetListBoxReset(listboxLobbyChat);
		s_lobbyRecentChatters.clear();
		GenOnlineRefreshPlayerList(TRUE);
	}

	UnicodeString msg;
	msg.format(TheGameText->fetch("GUI:LobbyJoined"), rooms[roomIndex].GetRoomDisplayName().str());
	GadgetListBoxAddEntryText(listboxLobbyChat, msg, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);

	GenOnlineRefreshGameList(TRUE);
	PopulateLobbyFilterComboBox(comboLobbyGroupRooms);
	PopulateGameFilterComboBox(comboLobbyFilter);
}

static const char *const rankNames[] = {
	"Private",
	"Corporal",
	"Sergeant",
	"Lieutenant",
	"Captain",
	"Major",
	"Colonel",
	"General",
	"Brigadier",
	"Commander",
};
static_assert(ARRAY_SIZE(rankNames) == MAX_RANKS, "Incorrect array size");


const Image* GenOnlineLookupSmallRankImage(Int side, Int rankPoints)
{
	if (rankPoints == 0 || !TheRankPointValues)
		return nullptr;

	Int rank = 0;
	Int i = 0;
	while (i + 1 < MAX_RANKS && rankPoints >= TheRankPointValues->m_ranks[i + 1])
		++i;
	rank = i;

	if (rank < 0 || rank >= 10)
		return nullptr;

	AsciiString sideStr = "N";
	switch(side)
	{
		case 2:  //USA
		case 5:  //Super Weapon
		case 6:  //Laser
		case 7:  //Air Force
			sideStr = "USA";
			break;

		case 3:  //China
		case 8:  //Tank
		case 9:  //Infantry
		case 10: //Nuke
			sideStr = "CHA";
			break;

		case 4:  //GLA
		case 11: //Toxin
		case 12: //Demolition
		case 13: //Stealth
			sideStr = "GLA";
			break;
	}

	AsciiString fullImageName;
	fullImageName.format("%s-%s", rankNames[rank], sideStr.str());
	const Image *img = TheMappedImageCollection->findImageByName(fullImageName);
	DEBUG_ASSERTLOG(img, ("*** Could not load small rank image '%s' from TheMappedImageCollection!", fullImageName.str()));
	return img;
}

// Lobby player list: rebuilt only when the roster changes; stats are fetched only for visible rows.

struct LobbyPlayerRow
{
	int64_t     userID = 0;
	Bool        isAdmin = FALSE;
	Bool        isFriend = FALSE;
	Bool        isIgnored = FALSE;
	Bool        iconResolved = FALSE; // icon painted from fresh stats
	std::string displayName;
	std::string sortKey; // lowercase display name
};

static std::vector<LobbyPlayerRow> s_lobbyPlayerRows;   // index == row
static std::string s_lobbyRosterSignature;              // rebuild when it changes

// Last resolved (rank points, favorite side) per user; avoids icon blink on a cache miss.
static std::unordered_map<int64_t, std::pair<Int, Int>> s_lastKnownRankByUser;

// One batch stats request at a time; overlapping requests supersede each other's responses.
static Bool s_statsBatchInFlight = FALSE;
static UnsignedInt s_statsBatchStartTime = 0;
static UnsignedInt s_statsBatchGeneration = 0; // bumped on lobby init
// Users requested this lobby visit.
static std::unordered_set<int64_t> s_statsRequestedUserIDs;
static const UnsignedInt STATS_BATCH_WATCHDOG_MS = 30000; // recover from a lost response

static Int s_lastVisibleTop = -1;
static Int s_lastVisibleBottom = -1;
static const Int VISIBLE_STATS_BUFFER = 8; // prefetch rows around the viewport

static UnsignedInt s_lastPlayerListRebuild = 0;
static const UnsignedInt PLAYERLIST_MIN_REBUILD_MS = 1000; // floor between full rebuilds

//-------------------------------------------------------------------------------------------------
static Int LobbyRankIconExtent()
{
	const Image* preorderImg = TheMappedImageCollection->findImageByName("OfficersClubsmall");
	Int w = (preorderImg) ? preorderImg->getImageWidth() : 10;
	if (listboxLobbyPlayers != nullptr)
		w = min(GadgetListBoxGetColumnWidth(listboxLobbyPlayers, 0), w);
	return w;
}

//-------------------------------------------------------------------------------------------------
/** Rank icon from cached stats, falling back to the last resolved value. */
static const Image* ResolveRankIconForUser(int64_t userID, GenOnlineStatsInterface* pStatsInterface)
{
	Int rankPoints = 0;
	Int favoriteSide = 0;
	Bool bResolved = FALSE;

	PSPlayerStats stats = PSPlayerStats();
	if (pStatsInterface != nullptr
		&& pStatsInterface->getPlayerStatsFromCache(userID, &stats)
		&& stats.id != 0)
	{
		rankPoints = GenOnlineCalculateRank(stats);
		favoriteSide = GenOnlineGetFavoriteSide(stats);
		s_lastKnownRankByUser[userID] = std::make_pair(rankPoints, favoriteSide);
		bResolved = TRUE;
	}

	if (!bResolved)
	{
		auto it = s_lastKnownRankByUser.find(userID);
		if (it != s_lastKnownRankByUser.end())
		{
			rankPoints = it->second.first;
			favoriteSide = it->second.second;
		}
	}

	return GenOnlineLookupSmallRankImage(favoriteSide, rankPoints);
}

//-------------------------------------------------------------------------------------------------
/** Repaint visible rows' rank icons from the stats cache (no network). */
static void RefreshVisibleLobbyRowIcons()
{
	if (listboxLobbyPlayers == nullptr || s_lobbyPlayerRows.empty())
		return;

	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();

	const Int rowCount = (Int)s_lobbyPlayerRows.size();
	Int top = GadgetListBoxGetTopVisibleEntry(listboxLobbyPlayers);
	Int bottom = GadgetListBoxGetBottomVisibleEntry(listboxLobbyPlayers);
	if (top < 0) top = 0;
	if (bottom < 0 || bottom >= rowCount) bottom = rowCount - 1;

	const Int firstRow = max(0, top - VISIBLE_STATS_BUFFER);
	const Int lastRow = min(rowCount - 1, bottom + VISIBLE_STATS_BUFFER);
	const Int iconExtent = LobbyRankIconExtent();

	for (Int row = firstRow; row <= lastRow; ++row)
	{
		LobbyPlayerRow& playerRow = s_lobbyPlayerRows[row];

		// fresh-stats icons don't change; skip
		if (playerRow.iconResolved)
			continue;

		const Image* rankImg = ResolveRankIconForUser(playerRow.userID, pStatsInterface);
		GadgetListBoxAddEntryImage(listboxLobbyPlayers, rankImg, row, 0, iconExtent, iconExtent);

		if (pStatsInterface != nullptr && pStatsInterface->HasFreshPlayerStats(playerRow.userID))
			playerRow.iconResolved = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Request stats for stale visible rows; bOnlyUnrequested skips users already requested. */
static void RequestVisibleLobbyStats(Bool bOnlyUnrequested)
{
	if (listboxLobbyPlayers == nullptr || s_lobbyPlayerRows.empty())
		return;

	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (pStatsInterface == nullptr)
		return;

	const Int rowCount = (Int)s_lobbyPlayerRows.size();
	Int top = GadgetListBoxGetTopVisibleEntry(listboxLobbyPlayers);
	Int bottom = GadgetListBoxGetBottomVisibleEntry(listboxLobbyPlayers);
	if (top < 0) top = 0;
	if (bottom < 0 || bottom >= rowCount) bottom = rowCount - 1;

	const Int firstRow = max(0, top - VISIBLE_STATS_BUFFER);
	const Int lastRow = min(rowCount - 1, bottom + VISIBLE_STATS_BUFFER);

	std::vector<int64_t> vecUserStatsToRequest;
	for (Int row = firstRow; row <= lastRow; ++row)
	{
		const int64_t userID = s_lobbyPlayerRows[row].userID;
		if (pStatsInterface->HasFreshPlayerStats(userID))
			continue;
		if (bOnlyUnrequested && s_statsRequestedUserIDs.count(userID) != 0)
			continue;
		vecUserStatsToRequest.push_back(userID);
	}

	if (vecUserStatsToRequest.empty())
		return;

	const UnsignedInt now = timeGetTime();
	if (s_statsBatchInFlight && (now - s_statsBatchStartTime) < STATS_BATCH_WATCHDOG_MS)
		return;

	s_statsRequestedUserIDs.insert(vecUserStatsToRequest.begin(), vecUserStatsToRequest.end());

	s_statsBatchInFlight = TRUE;
	s_statsBatchStartTime = now;
	const UnsignedInt generation = s_statsBatchGeneration;
	pStatsInterface->findPlayerStatsByBatch(vecUserStatsToRequest, [generation](bool /*bSuccess*/)
		{
			// response from a previous lobby visit
			if (generation != s_statsBatchGeneration)
				return;

			s_statsBatchInFlight = FALSE;

			// lobby was left
			if (listboxLobbyPlayers == nullptr)
				return;

			RefreshVisibleLobbyRowIcons();
			RequestVisibleLobbyStats(TRUE);
		});
}

//-------------------------------------------------------------------------------------------------
/** Request stats for visible rows; no-op if the window hasn't moved unless bForce. */
static void EnsureVisibleLobbyStats(Bool bForce)
{
	if (listboxLobbyPlayers == nullptr || s_lobbyPlayerRows.empty())
		return;

	const Int rowCount = (Int)s_lobbyPlayerRows.size();
	Int top = GadgetListBoxGetTopVisibleEntry(listboxLobbyPlayers);
	Int bottom = GadgetListBoxGetBottomVisibleEntry(listboxLobbyPlayers);
	if (top < 0) top = 0;
	if (bottom < 0 || bottom >= rowCount) bottom = rowCount - 1;

	if (!bForce && top == s_lastVisibleTop && bottom == s_lastVisibleBottom)
		return;
	s_lastVisibleTop = top;
	s_lastVisibleBottom = bottom;

	RefreshVisibleLobbyRowIcons();
	RequestVisibleLobbyStats(FALSE);
}

//-------------------------------------------------------------------------------------------------
/** Room roster in display order: name, then admins, then friends. */
static void CollectLobbyPlayerRows(std::vector<LobbyPlayerRow>& outRows)
{
	outRows.clear();

	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pRoomsInterface == nullptr)
		return;

	auto membersMap = pRoomsInterface->GetMembersListForCurrentRoom();
	outRows.reserve(membersMap.size());

	for (auto& [id, member] : membersMap)
	{
		LobbyPlayerRow row;
		row.userID = member.user_id;
		row.displayName = member.display_name;
		row.isAdmin = member.m_bIsAdmin ? TRUE : FALSE;
		row.isFriend = (pSocialInterface != nullptr && pSocialInterface->IsUserFriend(member.user_id)) ? TRUE : FALSE;
		row.isIgnored = (pSocialInterface != nullptr && pSocialInterface->IsUserIgnored(member.user_id)) ? TRUE : FALSE;

		row.sortKey.resize(row.displayName.size());
		std::transform(row.displayName.begin(), row.displayName.end(), row.sortKey.begin(),
			[](unsigned char c) { return std::tolower(c); });

		outRows.emplace_back(std::move(row));
	}

	std::sort(outRows.begin(), outRows.end(),
		[](const LobbyPlayerRow& a, const LobbyPlayerRow& b) { return a.sortKey < b.sortKey; });

	auto afterAdmins = std::stable_partition(outRows.begin(), outRows.end(),
		[](const LobbyPlayerRow& x) { return x.isAdmin != FALSE; });

	std::stable_partition(afterAdmins, outRows.end(),
		[](const LobbyPlayerRow& x) { return x.isFriend != FALSE; });
}

//-------------------------------------------------------------------------------------------------
static std::string BuildLobbyRosterSignature(const std::vector<LobbyPlayerRow>& rows)
{
	std::string sig;
	sig.reserve(rows.size() * 24);
	for (const LobbyPlayerRow& row : rows)
	{
		const int flags = (row.isAdmin ? 1 : 0) | (row.isFriend ? 2 : 0) | (row.isIgnored ? 4 : 0);
		sig += std::to_string(row.userID);
		sig += '/';
		sig += std::to_string(flags);
		sig += '/';
		sig += row.displayName;
		sig += ';';
	}
	return sig;
}

//-------------------------------------------------------------------------------------------------
/** Full listbox rebuild - only call when the roster / ordering changed. */
static void RebuildLobbyPlayerList(const std::vector<LobbyPlayerRow>& rows)
{
	if (listboxLobbyPlayers == nullptr)
		return;

	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const int64_t localUserID = (pAuthInterface != nullptr) ? pAuthInterface->GetUserID() : 0;

	// preserve selection and scroll
	Int selectedIndex = -1;
	GadgetListBoxGetSelected(listboxLobbyPlayers, &selectedIndex);
	const int64_t selectedUserID = (selectedIndex >= 0 && selectedIndex < (Int)s_lobbyPlayerRows.size())
		? s_lobbyPlayerRows[selectedIndex].userID : 0;
	const Int previousTopIndex = GadgetListBoxGetTopVisibleEntry(listboxLobbyPlayers);

	GadgetListBoxReset(listboxLobbyPlayers);
	s_lobbyPlayerRows = rows;

	const Int iconExtent = LobbyRankIconExtent();
	Int indexToSelect = -1;

	// batch: avoids per-row height recompute
	{
		GadgetListBoxBatchAddScope batchScope(listboxLobbyPlayers);
		for (const LobbyPlayerRow& row : s_lobbyPlayerRows)
		{
			Color colorToUse = GameSpyColor[GSCOLOR_PLAYER_NORMAL];
			if (row.isAdmin)
				colorToUse = GameSpyColor[GSCOLOR_PLAYER_OWNER];
			else if (row.isFriend)
				colorToUse = GameSpyColor[GSCOLOR_PLAYER_BUDDY];
			else if (row.isIgnored)
				colorToUse = GameSpyColor[GSCOLOR_PLAYER_IGNORED];
			else if (row.userID == localUserID)
				colorToUse = GameSpyColor[GSCOLOR_PLAYER_SELF];

			const Image* rankImg = ResolveRankIconForUser(row.userID, pStatsInterface);

			Int index = GadgetListBoxAddEntryImage(listboxLobbyPlayers, rankImg, -1, 0, iconExtent, iconExtent);
			GadgetListBoxAddEntryText(listboxLobbyPlayers, UnicodeString(from_utf8(row.displayName).c_str()), colorToUse, index, 1);
			GadgetListBoxSetItemData(listboxLobbyPlayers, (void*)(Int)row.userID, index);

			if (selectedUserID != 0 && row.userID == selectedUserID)
				indexToSelect = index;
		}
	}

	// drop cached icons for departed users
	{
		std::unordered_set<int64_t> present;
		present.reserve(s_lobbyPlayerRows.size());
		for (const LobbyPlayerRow& row : s_lobbyPlayerRows)
			present.insert(row.userID);

		for (auto it = s_lastKnownRankByUser.begin(); it != s_lastKnownRankByUser.end(); )
		{
			if (present.find(it->first) == present.end())
				it = s_lastKnownRankByUser.erase(it);
			else
				++it;
		}
	}

	GadgetListBoxSetTopVisibleEntry(listboxLobbyPlayers, previousTopIndex);
	if (indexToSelect >= 0)
		GadgetListBoxSetSelected(listboxLobbyPlayers, indexToSelect);
	else if (selectedIndex >= 0)
		TheWindowManager->winSetLoneWindow(NULL);

	s_lobbyRosterSignature = BuildLobbyRosterSignature(s_lobbyPlayerRows);
	s_lastVisibleTop = -1;
	s_lastVisibleBottom = -1;
	EnsureVisibleLobbyStats(TRUE);
}

//-------------------------------------------------------------------------------------------------
void GenOnlinePopulateLobbyPlayerListbox()
{
	if (listboxLobbyPlayers == nullptr)
		return;

	std::vector<LobbyPlayerRow> rows;
	CollectLobbyPlayerRows(rows);

	const Bool rosterChanged = (BuildLobbyRosterSignature(rows) != s_lobbyRosterSignature);

	// rebuild is costly; the signature stays stale until one runs, so nothing is lost
	const UnsignedInt now = timeGetTime();
	const Bool rebuildAllowed = (s_lastPlayerListRebuild == 0)
		|| (now - s_lastPlayerListRebuild) >= PLAYERLIST_MIN_REBUILD_MS;

	if (rosterChanged && rebuildAllowed)
	{
		s_lastPlayerListRebuild = now;
		RebuildLobbyPlayerList(rows);
	}
	else
	{
		// no rebuild: just refresh visible stats
		EnsureVisibleLobbyStats(TRUE);
	}
}

void GenOnlineLobbyMenuCreateLobbyCallback(bool bSuccess)
{
	if (!bSuccess)
	{
		const std::string code = GenOnlineLastCreateLobbyError();
		UnicodeString s;
		if (code == "lobby.anticheat_required")
			s = TheGameText->fetchOrSubstitute("GUI:GOCreateFailedAnticheatRequired", L"Anti-cheat is not running. Start the game through the Generals Online launcher.");
		else if (code == "lobby.easy_anticheat_unavailable")
			s = TheGameText->fetchOrSubstitute("GUI:GOCreateFailedEacUnavailable", L"Easy Anti-Cheat must be running to require it.");
		else
			s = TheGameText->fetchOrSubstitute("GUI:GOCreateFailedDefault", L"The game could not be created. Try again.");
		GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), s);
		return;
	}

	buttonPushed = true;
	nextScreen = "Menus/GenOnlineGameOptionsMenu.wnd";
	TheShell->pop();
	//TheGameSpyInfo->markAsStagingRoomHost();
	//TheGameSpyInfo->setGameOptions();
}

// The game a friend invited the local player to, joined once the game list holds it. -1 for none.
static int64_t s_inviteLobbyID = -1;
static UnsignedInt s_inviteRequestedAt = 0;
enum { INVITE_LIST_WAIT_MS = 10000 };

// Joins a game of the list after the CRC check, asking for its password unless a friend invited the local player.
static void JoinListedLobby(const LobbyEntry& Lobby, Bool bInvited)
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	// CRC Check
	if (Lobby.exe_crc != TheGlobalData->m_exeCRC || Lobby.ini_crc != TheGlobalData->m_iniCRC)
	{
		// Only Zero Hour knows the CRC of its unmodified INI files, so Generals shows the plain mismatch.
#if defined(VANILLA_INI_CRC)
		if (TheGlobalData->m_iniCRC != VANILLA_INI_CRC)
		{
			GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), UnicodeString(L"You have modified INI files or a modification."));
		}
		else if (Lobby.ini_crc != VANILLA_INI_CRC)
		{
			GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), UnicodeString(L"The host has modified INI files or a modification."));
		}
		else
#endif
		{
			GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->fetch("GUI:JoinFailedCRCMismatch"));
		}
		return;
	}

	// TODO_GENONLINE: Enforce this on the host too, vanilla game did not...

	pLobbyInterface->SetLobbyTryingToJoin(Lobby);

	// The service lets an invited friend in without the password.
	if (Lobby.passworded && !bInvited)
	{
		GameSpyOpenOverlay(GSOVERLAY_GAMEPASSWORD);
	}
	else
	{
		pLobbyInterface->JoinLobby(Lobby, std::string());

		GenOnlineSetLobbyAttemptHostJoin(TRUE);
	}
}

Bool GenOnlineLobbyMenuIsActive()
{
	return s_lobbyMenuOpen;
}

void GenOnlineLobbyMenuJoinInvite(int64_t lobbyID)
{
	s_inviteLobbyID = lobbyID;
	s_inviteRequestedAt = timeGetTime();
}

// The game list loads after the screen opens, so an invite waits a few seconds for its game to show up.
static void JoinPendingInvite()
{
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (s_inviteLobbyID < 0 || pLobbyInterface == nullptr || isShuttingDown || buttonPushed || s_tryingToHostOrJoin
		|| pLobbyInterface->IsInLobby() || pLobbyInterface->GetLobbyTryingToJoin().lobbyID != -1)
	{
		return;
	}
	const LobbyEntry lobby = pLobbyInterface->GetLobbyFromID(s_inviteLobbyID);
	if (lobby.lobbyID != -1)
	{
		s_inviteLobbyID = -1;
		JoinListedLobby(lobby, TRUE);
	}
	else if (timeGetTime() - s_inviteRequestedAt > INVITE_LIST_WAIT_MS)
	{
		s_inviteLobbyID = -1;
		GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOInviteGameGone", L"This game is no longer open."));
	}
}

void GenOnlineLobbyMenuJoinLobbyCallback(EJoinLobbyResult result)
{
	GenOnlineSetLobbyAttemptHostJoin(FALSE);
	if (result == EJoinLobbyResult::JoinLobbyResult_Success)
	{
		// Woohoo!  On to our next screen!
		buttonPushed = true;
		nextScreen = "Menus/GenOnlineGameOptionsMenu.wnd";
		TheShell->pop();
	}
	else
	{
		UnicodeString s;

		switch (result)
		{
		case EJoinLobbyResult::JoinLobbyResult_FullRoom:        // The room is full.
			s = TheGameText->fetch("GUI:JoinFailedRoomFull");
			break;

        case EJoinLobbyResult::JoinLobbyResult_AnticheatMismatch:
            s = TheGameText->fetchOrSubstitute("GUI:GOJoinFailedEacRequired", L"This game requires Easy Anti-Cheat.");
            break;

		case EJoinLobbyResult::JoinLobbyResult_BuildMismatch:
			s = TheGameText->fetchOrSubstitute("GUI:GOJoinFailedBuildMismatch", L"This game runs a different game version.");
			break;

		case EJoinLobbyResult::JoinLobbyResult_Banned:
			s = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOJoinFailedKeptOut", L"The host has kept you out of this game.");
			break;

		case EJoinLobbyResult::JoinLobbyResult_BadPassword:     // An incorrect password (or none) was given for a passworded room.
			s = TheGameText->fetch("GUI:JoinFailedBadPassword");
			break;
		default:
			s = TheGameText->fetch("GUI:JoinFailedDefault");
			break;
		}

		GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), s);

	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Lobby Menu */
//-------------------------------------------------------------------------------------------------
void GenOnlineLobbyMenuInit( WindowLayout *layout, void *userData )
{
	s_lobbyMenuOpen = TRUE;
	const UnsignedInt lobbyMenuGeneration = ++s_lobbyMenuGeneration;

	// for safety (and sanity)
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->LeaveCurrentLobby();
	}
	
	nextScreen = nullptr;
	buttonPushed = false;
	isShuttingDown = false;

	GenOnlineSetLobbyAttemptHostJoin(FALSE); // not trying to host or join

	gameListRefreshTime = 0;
	playerListRefreshTime = 0;
	s_statsBatchInFlight = FALSE;
	++s_statsBatchGeneration;
	s_statsRequestedUserIDs.clear();
	s_lastKnownRankByUser.clear();
	s_lobbyPlayerRows.clear();
	s_lobbyRosterSignature.clear();
	s_lastVisibleTop = -1;
	s_lastVisibleBottom = -1;
	s_lastPlayerListRebuild = 0;

	parentWOLLobbyID = TheNameKeyGenerator->nameToKey( "WOLCustomLobby.wnd:WOLLobbyMenuParent" );
	parent = TheWindowManager->winGetWindowFromId(nullptr, parentWOLLobbyID);

	buttonBackID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonBack");
	buttonBack = TheWindowManager->winGetWindowFromId(parent, buttonBackID);

	buttonHostID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonHost");
	buttonHost = TheWindowManager->winGetWindowFromId(parent, buttonHostID);

	buttonRefreshID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonRefresh");
	buttonRefresh = TheWindowManager->winGetWindowFromId(parent, buttonRefreshID);

	buttonJoinID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonJoin");
	buttonJoin = TheWindowManager->winGetWindowFromId(parent, buttonJoinID);
	buttonJoin->winEnable(FALSE);

	buttonBuddyID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonBuddy");
	buttonBuddy = TheWindowManager->winGetWindowFromId(parent, buttonBuddyID);

	buttonChatID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ButtonEmote"); // TODO Rename ButtonEmote to ButtonChat in .wnd file
	buttonChat = TheWindowManager->winGetWindowFromId(parent, buttonChatID);

	textEntryChatID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:TextEntryChat");
	textEntryChat = TheWindowManager->winGetWindowFromId(parent, textEntryChatID);

	textEntrySearchID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:TextEntrySearch");
	textEntrySearch = TheWindowManager->winGetWindowFromId(parent, textEntrySearchID);

	listboxLobbyPlayersID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ListboxPlayers");
	listboxLobbyPlayers = TheWindowManager->winGetWindowFromId(parent, listboxLobbyPlayersID);
	GadgetListBoxRemoveMultiSelect(listboxLobbyPlayers);
	listboxLobbyPlayers->winSetTooltipFunc(playerTooltip);
	SetListBoxRowAnimMode(listboxLobbyPlayers, LIST_ROW_ANIM_ID);
	GenOnlineLobbyGameDetailsInit(parent);

	listboxLobbyChatID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ListboxChat");
	listboxLobbyChat = TheWindowManager->winGetWindowFromId(parent, listboxLobbyChatID);
	SetListBoxRowAnimMode(listboxLobbyChat, LIST_ROW_ANIM_SLOT);

	comboLobbyGroupRoomsID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ComboBoxGroupRooms");
	comboLobbyGroupRooms = TheWindowManager->winGetWindowFromId(parent, comboLobbyGroupRoomsID);
	comboLobbyFilterID = TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:ComboBoxLobbyFilter");
	comboLobbyFilter = TheWindowManager->winGetWindowFromId(parent, comboLobbyFilterID);

	//GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"Welcome to Generals Online for Zero Hour!"), GameMakeColor(255, 194, 15, 255), -1, -1);

	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	GadgetTextEntrySetCompletionEnabled(textEntryChat, TRUE);
	GadgetTextEntrySetText(textEntrySearch, UnicodeString::TheEmptyString);
	theLobbySearchText.clear();
	s_lobbyChatCycler.Reset();

	PopulateLobbyFilterComboBox(comboLobbyGroupRooms);
	extern LobbyGameModeFilter theLobbyFilter;
	theLobbyFilter = LOBBY_FILTER_ALL;
	PopulateGameFilterComboBox(comboLobbyFilter);

	// Show Menu
	layout->hide( FALSE );

	// if we're not in a room, this will join the best available one

	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pLobbyInterface != nullptr && pRoomsInterface != nullptr)
	{
		pLobbyInterface->RegisterForCreateLobbyCallback(GenOnlineLobbyMenuCreateLobbyCallback);

		pLobbyInterface->RegisterForJoinLobbyCallback(GenOnlineLobbyMenuJoinLobbyCallback);

		//GadgetListBoxSetItemData(listboxLobbyChat, (void*)-1, index);

		// TODO_GENONLINE: player list change callbacks

		// register for chat events
		pRoomsInterface->RegisterForChatCallback([](UnicodeString strMessage, Color color)
			{
				GadgetListBoxAddEntryText(listboxLobbyChat, GenOnlineBreakLongWords(listboxLobbyChat, strMessage), color, -1, -1);
			});
		pRoomsInterface->RegisterForChatMessageCallbacks(AddRoomChatMessageLine, RemoveRoomChatMessageLine);

		// register for roster events (throttled path)
		pRoomsInterface->RegisterForRosterNeedsRefreshCallback([]()
				{
					GenOnlineRefreshPlayerList(false);
			});

		pRoomsInterface->RegisterForRoomChangedCallback(HandleNetworkRoomChanged);
	}

	GrabWindowInfo();

	// animate controls
//	TheShell->registerWithAnimateManager(parent, WIN_ANIMATION_SLIDE_TOP, TRUE);
	TheShell->showShellMap(TRUE);
	if (TheGenOnlineGame != nullptr)
	{
		TheGenOnlineGame->reset();
	}

	raiseMessageBoxes = true;

	TheGenOnlineLobbyQueuedUTMs.clear();
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);
	DontShowMainMenu = TRUE;


// upon entry, retrieve room list

	GenOnlineRoomsInterface* pRoomsInterfaceOuter = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRoomsInterfaceOuter != nullptr)
	{
		pRoomsInterfaceOuter->GetRoomList([=](bool success)
			{
				if (lobbyMenuGeneration != s_lobbyMenuGeneration || buttonPushed || isShuttingDown || listboxLobbyChat == nullptr)
				{
					return;
				}

				const std::vector<NetworkRoom>& rooms = pRoomsInterfaceOuter->GetGroupRooms();
				if (!success || rooms.empty())
				{
					GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"\t ERROR: No rooms are available. Try logging in again."), GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), -1, -1);
					return;
				}

				pRoomsInterfaceOuter->JoinRoom(0);
			});
	}

	GenOnlineUserSectionShow(parent);

	// Update the communicator button anytime we get notifications
    GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		// notifiactions callback
		pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
			{
				GameWindow* button = TheWindowManager->winGetWindowFromId(nullptr, buttonBuddyID);
				if (button != nullptr)
				{
					UnicodeString buttonText;
                    if (numNotifications > 0)
                    {
                        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
                    }
                    else
                    {
                        buttonText.format(L"%s", TheGameText->fetch("GUI:Buddies").str());
                    }
					button->winSetText(buttonText);
				}
			});
	}

	// And also initialize it
    if (buttonBuddy != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
    {
        UnicodeString buttonText;
        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
        buttonBuddy->winSetText(buttonText);
    }
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != nullptr) );

	if (nextScreen != nullptr)
	{
		TheShell->push(nextScreen);
	}

	nextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void GenOnlineLobbyMenuShutdown( WindowLayout *layout, void *userData )
{
	GenOnlineUserSectionHide();
	s_lobbyMenuOpen = FALSE;
	++s_lobbyMenuGeneration;
	GenOnlineCloseRightClickMenus();
	buttonBuddy = nullptr;

	GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged(nullptr);
	}

	GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (pRoomsInterface != nullptr)
	{
		pRoomsInterface->DeregisterForChatCallback();
		pRoomsInterface->DeregisterForRosterNeedsRefreshCallback();
		pRoomsInterface->DeregisterForRoomChangedCallback();
	}

	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->DeregisterForCreateLobbyCallback();
		pLobbyInterface->DeregisterForJoinLobbyCallback();
		pLobbyInterface->DeregisterForSearchForLobbiesCallback();
	}

	CustomMatchPreferences pref;
	if (GetGameInfoListBox())
	{
		pref.setUsesLongGameList(FALSE);
	}
	else
	{
		pref.setUsesLongGameList(TRUE);
	}
	pref.write();

	ReleaseWindowInfo();

	GenOnlineLobbyGameDetailsShutdown();
	listboxLobbyChat = nullptr;
	listboxLobbyPlayers = nullptr;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	DontShowMainMenu = FALSE;

	RaiseGSMessageBox();
	TheTransitionHandler->reverse("WOLCustomLobbyFade");

}

static void fillPlayerInfo(const PeerResponse *resp, PlayerInfo *info)
{
	info->m_name			= resp->nick.c_str();
	info->m_profileID	= resp->player.profileID;
	info->m_flags			= resp->player.flags;
	info->m_wins			= resp->player.wins;
	info->m_losses		= resp->player.losses;
	info->m_locale		= resp->locale.c_str();
	info->m_rankPoints= resp->player.rankPoints;
	info->m_side			= resp->player.side;
	info->m_preorder	= resp->player.preorder;
}

#ifdef PERF_TEST
static const char* getMessageString(Int t)
{
	switch(t)
	{
		case PeerResponse::PEERRESPONSE_LOGIN:
			return "login";
		case PeerResponse::PEERRESPONSE_DISCONNECT:
			return "disconnect";
		case PeerResponse::PEERRESPONSE_MESSAGE:
			return "message";
		case PeerResponse::PEERRESPONSE_GROUPROOM:
			return "group room";
		case PeerResponse::PEERRESPONSE_STAGINGROOM:
			return "staging room";
		case PeerResponse::PEERRESPONSE_STAGINGROOMPLAYERINFO:
			return "staging room player info";
		case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
			return "group room join";
		case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
			return "staging room create";
		case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
			return "staging room join";
		case PeerResponse::PEERRESPONSE_PLAYERJOIN:
			return "player join";
		case PeerResponse::PEERRESPONSE_PLAYERLEFT:
			return "player part";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDNICK:
			return "player nick";
		case PeerResponse::PEERRESPONSE_PLAYERINFO:
			return "player info";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
			return "player flags";
		case PeerResponse::PEERRESPONSE_ROOMUTM:
			return "room UTM";
		case PeerResponse::PEERRESPONSE_PLAYERUTM:
			return "player UTM";
		case PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS:
			return "QM status";
		case PeerResponse::PEERRESPONSE_GAMESTART:
			return "game start";
		case PeerResponse::PEERRESPONSE_FAILEDTOHOST:
			return "host failure";
	}
	return "unknown";
}
#endif // PERF_TEST

//-------------------------------------------------------------------------------------------------
/** GenOnlineRefreshGameList
		The Bool is used to force refresh if the refresh button was hit.*/
//-------------------------------------------------------------------------------------------------
void GenOnlineRefreshGameList( Bool forceRefresh )
{
	// TODO_GENONLINE: rate limit this like before
	//RefreshGameListBoxes();

	Int refreshInterval = gameListRefreshInterval;

	if (forceRefresh || ((gameListRefreshTime == 0) || ((gameListRefreshTime + refreshInterval) <= timeGetTime())))
	{
		RefreshGameListBoxes();
		GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			pLobbyInterface->ConsumeLobbyListDirtyFlag();
		}
		gameListRefreshTime = timeGetTime();
	} else {
		//DEBUG_LOG(("gameListRefreshTime: %d refreshInterval: %d", gameListRefreshTime, refreshInterval));
	}
}
//-------------------------------------------------------------------------------------------------
/** GenOnlineRefreshPlayerList
		The Bool is used to force refresh if the refresh button was hit.*/
//-------------------------------------------------------------------------------------------------
void GenOnlineRefreshPlayerList( Bool forceRefresh )
{
		Int refreshInterval = playerListRefreshInterval;

		if (forceRefresh ||((playerListRefreshTime == 0) || ((playerListRefreshTime + refreshInterval) <= timeGetTime())))
		{
				GenOnlinePopulateLobbyPlayerListbox();
				playerListRefreshTime = timeGetTime();
		}
		else
		{
				// fetches only if the visible window moved
				EnsureVisibleLobbyStats(FALSE);
		}
}

void GenOnlineLobbyExitState()
{
	if (s_tryingToHostOrJoin)
		return;

	// Leave any group room, then pop off the screen
	auto pOnlineServicesManager = GenOnlineServicesManager::GetInstance();
	if (pOnlineServicesManager != nullptr)
	{
		GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
		if (pRoomsInterface != nullptr)
		{
			pRoomsInterface->LeaveRoom();
		}
	}

	GenOnlineSetLobbyAttemptHostJoin(TRUE); // pretend, since we don't want to queue up another action
	buttonPushed = true;

	if (pOnlineServicesManager == nullptr || pOnlineServicesManager->IsPendingFullTeardown()) // go back to the front end
	{
		nextScreen = nullptr;
	}
	else // user backed out, go back to welcome menu
	{
		nextScreen = "Menus/GenOnlineWelcomeMenu.wnd";
	}
	
	TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu update method */
//-------------------------------------------------------------------------------------------------
void GenOnlineLobbyMenuUpdate( WindowLayout * layout, void *userData)
{
	// need to exit?
	if (GenOnlineServicesManager::GetInstance() != nullptr && GenOnlineServicesManager::GetInstance()->IsPendingFullTeardown())
	{
		if (!s_tryingToHostOrJoin)
		{
			s_tryingToHostOrJoin = false;
			GenOnlineLobbyExitState();
			TearDownGenOnline();
		}		

		return;
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("WOLCustomLobbyFade");
			TheWindowManager->winSetFocus(textEntryChat);
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_ENTERED_FROM_GAME);
	}


	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}
	
	// do we need to update?
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr && pLobbyInterface->IsLobbyListDirty() && !isShuttingDown && !buttonPushed && !pLobbyInterface->IsInLobby() && pLobbyInterface->GetLobbyTryingToJoin().lobbyID == -1)
	{
		const bool bShouldAutoRefresh = true;

		if (bShouldAutoRefresh)
		{
			GenOnlineRefreshGameList(false);
		}
		else
		{
			GadgetListBoxAddEntryText(listboxLobbyChat, UnicodeString(L"Your lobby list is outdated. Hit refresh to see the latest servers."), GOUIColor(GOUICOLOR_NOTICE, GameMakeColor(255, 194, 15, 255)), -1, -1);
		}

	}

	GenOnlineHandleBuddyResponses();
	JoinPendingInvite();

}

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineLobbyMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																							(WindowMsgData)buttonBack, buttonBackID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}


static void BuildLobbyChatCompletionContext(ChatCompletionContext& context)
{
	static ChatCommandRegistry s_commands;
	static Bool s_commandsBuilt = FALSE;
	if (!s_commandsBuilt)
	{
		BuildLobbyChatCommands(s_commands);
		s_commandsBuilt = TRUE;
	}
	context.commands = &s_commands;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	const std::wstring self = (pAuthInterface != nullptr) ? pAuthInterface->GetDisplayNameW() : std::wstring();

	// Recent chatters come first, so the completion limit never drops them in a large channel.
	AppendChatNames(context.names, s_lobbyRecentChatters, self);
	for (const LobbyPlayerRow& row : s_lobbyPlayerRows)
	{
		AppendChatName(context.names, from_utf8(row.displayName), self);
	}
}

static void UpdateLobbyChatCompletionHint()
{
	s_lobbyChatCycler.Reset();
	ChatCompletionContext context;
	BuildLobbyChatCompletionContext(context);
	const std::wstring text = GadgetTextEntryGetText(textEntryChat).str();
	GadgetTextEntrySetCompletionHint(textEntryChat, UnicodeString(ChatCompletionHint(text, context).c_str()));
}

static void CompleteLobbyChat(Bool reverse)
{
	ChatCompletionContext context;
	BuildLobbyChatCompletionContext(context);
	std::wstring text = GadgetTextEntryGetText(textEntryChat).str();
	if (!s_lobbyChatCycler.Complete(text, context, reverse != FALSE))
		return;

	GadgetTextEntrySetText(textEntryChat, UnicodeString(text.c_str()));
	GadgetTextEntrySetCompletionHint(textEntryChat, UnicodeString(ChatCompletionHint(text, context).c_str()));
}

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GenOnlineLobbyMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	static NameKeyType buttonGameListTypeToggleID = NAMEKEY_INVALID;

	switch( msg )
	{


		//---------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				buttonGameListTypeToggleID = NAMEKEY("WOLCustomLobby.wnd:ButtonGameListToggle");
//				sliderChatAdjustID = NAMEKEY("WOLCustomLobby.wnd:SliderChatAdjust");

				break;
			}

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				break;
			}

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}

		//---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if ( controlID == GetGameListBoxID() )
				{
					int rowSelected = mData2;
					int64_t lobbyID = rowSelected >= 0 ? ResolveGameListLobbyID((Int)GadgetListBoxGetItemData(control, rowSelected, 0)) : 0;
					if( lobbyID >= 0 )
					{
						buttonJoin->winEnable(TRUE);
					}
					else
					{
						buttonJoin->winEnable(FALSE);
					}
					if (GetGameInfoListBox())
					{
						RefreshGameInfoListBox(GetGameListBox(), GetGameInfoListBox());
					}
					GenOnlineLobbyGameDetailsUpdate();
				}

				break;
			}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (HandleSortButton((NameKeyType)controlID))
					break;

				// If we back out, just bail - we haven't gotten far enough to need to log out
				if ( controlID == buttonBackID )
				{
					GenOnlineLobbyExitState();

				}
				else if ( controlID == buttonRefreshID )
				{
					// Added 2/17/03 added the game refresh button
					GenOnlineRefreshGameList(TRUE);
					GenOnlineRefreshPlayerList(TRUE);
				}
				else if ( controlID == buttonHostID )
				{
					if (s_tryingToHostOrJoin)
						break;

					GenOnlineSetLobbyAttemptHostJoin( TRUE );
					TheGenOnlineLobbyQueuedUTMs.clear();
					GameSpyOpenOverlay(GSOVERLAY_GAMEOPTIONS);
				}
				else if ( controlID == buttonJoinID )
				{
					GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					Int selected;
					GadgetListBoxGetSelected(GetGameListBox(), &selected);
					if (selected >= 0)
					{
						int64_t selectedID = ResolveGameListLobbyID((Int)GadgetListBoxGetItemData(GetGameListBox(), selected));
						if (selectedID >= 0)
						{
							auto Lobby = pLobbyInterface->GetLobbyFromID(selectedID);

							if (Lobby.lobbyID == -1) // -1 is invalid
							{
								break;
							}

							JoinListedLobby(Lobby, FALSE);
						}
					}
					else
					{
						GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:NoGameSelected"), NULL);
					}
				}
				else if ( controlID == buttonBuddyID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );
				}
				else if ( controlID == buttonGameListTypeToggleID )
				{
					ToggleGameListType();
				}
				else if ( controlID == buttonChatID )
				{
					// read the user's input
					UnicodeString txtInput;
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					txtInput.trim();
					if (txtInput.isEmpty())
					{
						GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
						break;
					}

					if (!LobbyChatRateLimitAllowsSend())
					{
						break;
					}

					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					s_lobbyChatCycler.Reset();
					GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
					if (pRoomsInterface != nullptr)
					{
						pRoomsInterface->SendChatMessageToCurrentRoom(txtInput, false);
					}
				}

				break;
			}

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if (s_tryingToHostOrJoin || s_populatingLobbyCombo)
					break;

				GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();

				extern LobbyGameModeFilter theLobbyFilter;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == comboLobbyGroupRoomsID )
				{
					Int pos = -1;
					GadgetComboBoxGetSelectedPos(comboLobbyGroupRooms, &pos);
					if (pos >= 0)
					{
						Int itemData = (Int)(intptr_t)GadgetComboBoxGetItemData(comboLobbyGroupRooms, pos);
						if (itemData < 0)
						{
							if (pRoomsInterface != nullptr)
							{
								const Int roomIndex = -itemData - 2;
								const std::vector<NetworkRoom>& rooms = pRoomsInterface->GetGroupRooms();
								if (roomIndex >= 0 && roomIndex < (Int)rooms.size()
									&& roomIndex != pRoomsInterface->GetCurrentRoomIndex())
								{
									theLobbyFilter = LOBBY_FILTER_ALL;
									pRoomsInterface->JoinRoom(roomIndex);
									PopulateGameFilterComboBox(comboLobbyFilter);
								}
							}
							PopulateLobbyFilterComboBox(comboLobbyGroupRooms);
						}
					}
				}
				else if( controlID == comboLobbyFilterID )
				{
					Int pos = -1;
					GadgetComboBoxGetSelectedPos(comboLobbyFilter, &pos);
					if (pos >= 0)
					{
						theLobbyFilter = (LobbyGameModeFilter)(Int)(intptr_t)GadgetComboBoxGetItemData(comboLobbyFilter, pos);
						GenOnlineRefreshGameList(TRUE);
					}
				}
			}
			break;

		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if (controlID == GetGameListBoxID())
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonJoinID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonJoinID );
					}
				}
				break;
			}

		//---------------------------------------------------------------------------------------------
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == listboxLobbyPlayersID)
				{
					RightClickStruct* rc = (RightClickStruct*)mData2;
					WindowLayout* rcLayout = NULL;
					GameWindow* rcMenu;
					if (rc->pos < 0)
					{
						GadgetListBoxSetSelected(control, -1);
						break;
					}

					// TODO_GENONLINE: Duplicate names break this lookup, look players up by id or only allow unique names.
					GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
					GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
					GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
					GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
					if (pRoomsInterface != nullptr && pAuthInterface != nullptr && pStatsInterface != nullptr && pSocialInterface != nullptr)
					{

						int profileID = (int)GadgetListBoxGetItemData(listboxLobbyPlayers, rc->pos, 0);
						NetworkRoomMember* roomMember = pRoomsInterface->GetRoomMemberFromID(profileID);

						if (rc->pos >= 0)
						{
							if (roomMember != nullptr)
							{
								AsciiString aName = AsciiString(roomMember->display_name.c_str());
								int64_t localuserID = pAuthInterface->GetUserID();

								Bool isBuddy = pSocialInterface->IsUserFriend(profileID);
								if (profileID <= 0)
									rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCNoProfileMenu.wnd"));
								else
								{
									if (profileID == localuserID)
									{
										rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCLocalPlayerMenu.wnd"));
									}
									else if (isBuddy)
									{
										rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCBuddiesMenu.wnd"));
									}
									else
										rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/GenOnlineRCNonBuddiesMenu.wnd"));
								}
								if (!rcLayout)
									break;

								GadgetListBoxSetSelected(control, rc->pos);

								rcMenu = rcLayout->getFirstWindow();
								rcMenu->winGetLayout()->runInit();
								rcMenu->winBringToTop();
								rcMenu->winHide(FALSE);
								GenOnlineSetUnignoreText(rcLayout, aName, profileID);
								ICoord2D rcSize, rcPos;
								rcMenu->winGetSize(&rcSize.x, &rcSize.y);
								rcPos.x = rc->mouseX;
								rcPos.y = rc->mouseY;
								if (rc->mouseX + rcSize.x > TheDisplay->getWidth())
									rcPos.x = TheDisplay->getWidth() - rcSize.x;
								if (rc->mouseY + rcSize.y > TheDisplay->getHeight())
									rcPos.y = TheDisplay->getHeight() - rcSize.y;
								rcMenu->winSetPosition(rcPos.x, rcPos.y);

								GameSpyRCMenuData* rcData = NEW GameSpyRCMenuData;
								rcData->m_id = profileID;
								rcData->m_nick = aName;
								rcData->m_itemType = (isBuddy) ? ITEM_BUDDY : ITEM_NONBUDDY;
								rcMenu->winSetUserData((void*)rcData);
								TheWindowManager->winSetLoneWindow(rcMenu);
							}
							else if (controlID == GetGameListBoxID())
							{
								break;
							}
						}
					}
				}
				else if (controlID == listboxLobbyChatID)
				{
					RightClickStruct* rc = (RightClickStruct*)mData2;
					GenOnlineRoomsInterface* pRoomsInterface = GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
					if (rc->pos < 0 || pRoomsInterface == nullptr)
						break;
					const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(control, rc->pos, 0)));
					std::string messageID;
					int64_t authorID = 0;
					pRoomsInterface->FindChatMessage(key, messageID, authorID);
					const NetworkRoomMember* author = pRoomsInterface->GetRoomMemberFromID(authorID);
					GenOnlineOpenMessageMenu(control, rc->pos, rc->mouseX, rc->mouseY, messageID.c_str(), authorID, author != nullptr ? author->display_name.c_str() : "");
				}
				break;
		}

		//---------------------------------------------------------------------------------------------
		case GEM_UPDATE_TEXT:
			{
				if ((GameWindow *)mData1 == textEntrySearch)
				{
					theLobbySearchText = GadgetTextEntryGetText(textEntrySearch);
					GenOnlineRefreshGameList(TRUE);
					break;
				}

				if ((GameWindow *)mData1 != textEntryChat)
					return MSG_IGNORED;

				UpdateLobbyChatCompletionHint();
				break;
			}

		//---------------------------------------------------------------------------------------------
		case GEM_TAB_COMPLETE:
			{
				if ((GameWindow *)mData1 != textEntryChat)
					return MSG_IGNORED;

				CompleteLobbyChat(mData2 != 0);
				break;
			}

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
			{
				if (buttonPushed || (GameWindow *)mData1 == textEntrySearch)
					break;

				s_lobbyChatCycler.Reset();

				// read the user's input
				UnicodeString txtInput;
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				txtInput.trim();
				if (txtInput.isEmpty())
				{
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					break;
				}

				Bool wasRateLimited = FALSE;
				if (GenOnlineHandleLobbySlashCommands(txtInput, &wasRateLimited))
				{
					if (!wasRateLimited)
						GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				}
				else
				{
					if (!LobbyChatRateLimitAllowsSend())
					{
						break;
					}

					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();
					if (pWS != nullptr)
					{
						pWS->SendData_RoomChatMessage(txtInput, false);
					}
				}
				break;
			}

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}
