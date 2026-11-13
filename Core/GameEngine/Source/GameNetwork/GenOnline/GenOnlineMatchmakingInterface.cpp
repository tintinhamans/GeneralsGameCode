#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/json.hpp"
#include "GameNetwork/GenOnline/JsonHelpers.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
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
	playlistEntry.GracePeriodAtMinPlayersMSec = (uint16_t)(std::clamp)(playlist.grace_period_ms, (int64_t)0, (int64_t)UINT16_MAX);
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
	if (GenOnlineAuthInterface::UsesRealtimeHub())
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
		return;
	}

	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Matchmaking/Playlists");
	std::map<std::string, std::string> mapHeaders;

	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			// TODO_GENONLINE: Error handling
			try
			{
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);

				std::vector<PlaylistEntry> vecPlaylists;
				for (const auto& playlistEntryIter : jsonObject["playlists"])
				{
					try
					{
						PlaylistEntry playlistEntry;
						JsonGetRequired(playlistEntryIter, "PlaylistID", playlistEntry.PlaylistID);
						JsonGetOptional(playlistEntryIter, "Name", playlistEntry.Name);
						JsonGetOptional(playlistEntryIter, "MinPlayers", playlistEntry.MinPlayers);
						JsonGetOptional(playlistEntryIter, "DesiredPlayers", playlistEntry.DesiredPlayers);
						JsonGetOptional(playlistEntryIter, "MinSelectedMaps", playlistEntry.MinSelectedMaps);
						JsonGetOptional(playlistEntryIter, "AllowTeams", playlistEntry.AllowTeams);
						JsonGetOptional(playlistEntryIter, "TeamSize", playlistEntry.TeamSize);
						JsonGetOptional(playlistEntryIter, "AllowArmySelection", playlistEntry.AllowArmySelection);
						JsonGetOptional(playlistEntryIter, "GracePeriodAtMinPlayersMSec", playlistEntry.GracePeriodAtMinPlayersMSec);

						// maps
						for (const auto& mapEntryIter : playlistEntryIter.at("Maps"))
						{
							PlaylistMapEntry mapEntry;

							JsonGetOptional(mapEntryIter, "Name", mapEntry.Name);
							JsonGetRequired(mapEntryIter, "Path", mapEntry.Path);
							mapEntry.Path = utf8_to_local(mapEntry.Path); // used as a local file path
							JsonGetOptional(mapEntryIter, "Custom", mapEntry.Custom);

							playlistEntry.Maps.push_back(mapEntry);
						}

						vecPlaylists.push_back(playlistEntry);
					}
					catch (...)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[GenOnline] Skipping malformed playlist entry");
					}
				}

				// cache
				m_vecCachedPlaylists = vecPlaylists;

				// invoke callback
				if (callbackOnComplete != nullptr)
				{
					callbackOnComplete(vecPlaylists);
				}
			}
			catch (...)
			{

			}

		});
}

void GenOnlineMatchmakingInterface::WidenSearch()
{
	if (GenOnlineAuthInterface::UsesRealtimeHub())
	{
		InvokeRealtimeHub(GenOnlineRealtime::WidenSearchRequest(), [](const GenOnlineRealtime::InvokeOutcome& outcome)
			{
				if (!outcome.Succeeded())
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] WidenSearch failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
				}
			});
		return;
	}

	std::map<std::string, std::string> mapHeaders;
	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Matchmaking/Widen");
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			
		});
}

void GenOnlineMatchmakingInterface::StartMatchmaking(uint16_t playlistID, std::vector<int> vecSelectedMapIndexes, std::function<void(bool)> fnCallback)
{
	if (GenOnlineAuthInterface::UsesRealtimeHub())
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
				if (!outcome.Succeeded())
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
					fnCallback(outcome.Succeeded());
				}
			});
		return;
	}

	nlohmann::json j;
	j["playlist"] = playlistID;
	j["maps"] = vecSelectedMapIndexes;
	j["exe_crc"] = TheGlobalData->m_exeCRC;
	j["ini_crc"] = TheGlobalData->m_iniCRC;
	j["anticheat_id"] = GenOnlinePluginHost::GetAnticheatIdentifier();

	std::map<std::string, std::string> mapHeaders;
	std::string strPostData = j.dump();
	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Matchmaking");
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPUTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			try
			{
				if (bSuccess && statusCode == 201)
				{
					
				}
				else
				{
					// TODO_QUICKMATCH
				}

				if (fnCallback != nullptr)
				{
					fnCallback(bSuccess);
				}
			}
			catch (...)
			{

			}
		});
}

void GenOnlineMatchmakingInterface::CancelMatchmaking()
{
	if (GenOnlineAuthInterface::UsesRealtimeHub())
	{
		InvokeRealtimeHub(GenOnlineRealtime::LeaveQueueRequest(), [](const GenOnlineRealtime::InvokeOutcome& outcome)
			{
				// A session that is not queued answers matchmaking.not_queued.
				if (!outcome.Succeeded() && outcome.Code() != "matchmaking.not_queued")
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[Realtime] LeaveQueue failed: %s", outcome.delivered ? outcome.Code().c_str() : outcome.transportError.c_str());
				}
			});
	}
	else
	{
		nlohmann::json j;

		std::map<std::string, std::string> mapHeaders;
		std::string strPostData = j.dump();
		std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("Matchmaking");
		GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
			{
		
			});
	}

	// also reset host migration flags on lobby
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->LeaveCurrentLobby();

		pLobbyInterface->ResetHostMigrationFlags();
	}
}
