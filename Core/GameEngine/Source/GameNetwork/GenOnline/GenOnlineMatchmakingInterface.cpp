#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeMatchmaking.h"

#include <algorithm>

GenOnlineMatchmakingInterface::GenOnlineMatchmakingInterface()
{

}

// A playlist of the realtime hub in the shape the quick match menu reads.
static PlaylistEntry PlaylistEntryFromHub(const GenOnlineRealtime::Playlist& playlist)
{
	PlaylistEntry playlistEntry;
	playlistEntry.PlaylistID = (uint16_t)playlist.id;
	playlistEntry.Name = playlist.name;
	playlistEntry.MinPlayers = playlist.min_players;
	playlistEntry.DesiredPlayers = playlist.desired_players;
	playlistEntry.MinSelectedMaps = playlist.min_selected_maps;
	playlistEntry.AllowTeams = playlist.allow_teams;
	playlistEntry.TeamSize = playlist.team_size.isNull ? -1 : playlist.team_size.value;
	playlistEntry.AllowArmySelection = playlist.allow_army_selection;
	playlistEntry.GracePeriodAtMinPlayersMSec = (uint16_t)(std::clamp)(GenOnlineRealtime::SecondsToMilliseconds(playlist.grace_period_s), (int64_t)0, (int64_t)UINT16_MAX);
	for (const GenOnlineRealtime::PlaylistMap& map : playlist.maps)
	{
		PlaylistMapEntry mapEntry;
		mapEntry.Name = map.name;
		mapEntry.Path = utf8_to_local(map.path); // used as a local file path
		mapEntry.Custom = map.custom;
		mapEntry.Index = map.index;
		playlistEntry.Maps.push_back(mapEntry);
	}
	return playlistEntry;
}

void GenOnlineMatchmakingInterface::RetrievePlaylists(std::function<void(std::vector<PlaylistEntry>)> callbackOnComplete)
{
	InvokeRealtimeHub(GenOnlineRealtime::GetPlaylistsRequest(), [this, callbackOnComplete](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineRealtime::GetPlaylistsResult result;
			if (!outcome.Data(result))
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] GetPlaylists failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
				return;
			}
			std::vector<PlaylistEntry> vecPlaylists;
			for (const GenOnlineRealtime::Playlist& playlist : result.playlists)
			{
				vecPlaylists.push_back(PlaylistEntryFromHub(playlist));
			}
			m_vecCachedPlaylists = vecPlaylists;
			if (callbackOnComplete != nullptr)
			{
				callbackOnComplete(vecPlaylists);
			}
		});
}

void GenOnlineMatchmakingInterface::WidenSearch()
{
	InvokeRealtimeHub(GenOnlineRealtime::WidenSearchRequest(), [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			if (!outcome.Succeeded())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] WidenSearch failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
			}
		});
}

void GenOnlineMatchmakingInterface::StartMatchmaking(uint16_t playlistID, std::vector<int> vecSelectedMapIndexes, std::function<void(bool)> fnCallback)
{
	// The menu selects positions in the playlist map list, the hub takes the playlist map indexes.
	// The service takes the build identity and the anticheat id from the session.
	std::vector<int> vecPlaylistMapIndexes;
	for (const PlaylistMapEntry& mapEntry : GetCachedPlaylistFromID(playlistID).Maps)
	{
		vecPlaylistMapIndexes.push_back(mapEntry.Index);
	}

	GenOnlineRealtime::JoinQueueRequest request;
	request.playlist_id = playlistID;
	if (!GenOnlineRealtime::TranslateMapIndexes(vecSelectedMapIndexes, vecPlaylistMapIndexes, request.map_indexes))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] JoinQueue: a selected map is not in playlist %d", (int)playlistID);
		if (fnCallback != nullptr)
		{
			fnCallback(false);
		}
		return;
	}

	InvokeRealtimeHub(request, [fnCallback](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			// A session that is already queued keeps searching, so the menu shows the search.
			const bool bQueued = outcome.Succeeded() || outcome.Code() == "matchmaking.already_queued";
			if (!bQueued)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] JoinQueue failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
				GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				if (pLobbyInterface != nullptr)
				{
					pLobbyInterface->InvokeMatchmakingMessageCallback(to_utf8(GenOnlineRealtimeBridge::FailureText(outcome).c_str()));
				}
			}
			if (fnCallback != nullptr)
			{
				fnCallback(bQueued);
			}
		});
}

void GenOnlineMatchmakingInterface::CancelMatchmaking()
{
	InvokeRealtimeHub(GenOnlineRealtime::LeaveQueueRequest(), [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			// A session that is not queued answers matchmaking.not_queued.
			if (!outcome.Succeeded() && outcome.Code() != "matchmaking.not_queued")
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] LeaveQueue failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
			}
		});

	// also reset host migration flags on lobby
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->LeaveCurrentLobby();

		pLobbyInterface->ResetHostMigrationFlags();
	}
}
