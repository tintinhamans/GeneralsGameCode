#pragma once
#include <curl/curl.h>
#include <mutex>
#include <string>
#include <vector>
#include "GameNetwork/GeneralsOnline/ServiceEndpointPolicy.h"

enum EHTTPVersion
{
    HTTP_VERSION_AUTO,
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
    HTTP_VERSION_2_0,
    HTTP_VERSION_3_0
};

// One layer of data_packs in settings.json: BIG paths in priority order, the first one wins
struct GenOnlineDataPackLayer
{
	bool m_enabled = true;
	std::vector<std::string> m_includePaths;
};

class GenOnlineSettings
{
public:
	GenOnlineSettings();

	float Camera_MoveSpeedRatio() const { return m_Camera_MoveSpeedRatio; }
	float Camera_GetMinHeight() const { return m_Camera_MinHeight; }
	float Camera_GetMaxHeight_WhenLobbyHost() const { return m_Camera_MaxHeight_LobbyHost; }

	float DetermineCameraMaxHeight();

	void Save_Camera_MaxHeight_WhenLobbyHost(float maxHeight)
	{
		if (maxHeight >= GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM && maxHeight <= GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM)
		{
			m_Camera_MaxHeight_LobbyHost = maxHeight;
			Save();
		}
	}


	void Graphics_SetFPS(int fpsLimit, bool bLimitFramerate)
	{
		m_Render_FramerateLimit_FPSVal = fpsLimit;
		m_Render_LimitFramerate = bLimitFramerate;
		Save();
	}

	bool Graphics_DrawStatsOverlay() const { return m_Render_DrawStatsOverlay; }
	bool Graphics_LimitFramerate() const { return m_Render_LimitFramerate; }
	int Graphics_GetFPSLimit() const
	{
		if (!m_Render_LimitFramerate)
		{
			return 1000000;
		}

		return m_Render_FramerateLimit_FPSVal;
	}

	bool Graphics_AdaptiveQuality() const { return m_Render_AdaptiveQuality; }
	std::string GetAnticheatPlugin() const;
	bool Social_Notifications_FriendComesOnline_Menus() { return m_Social_Notification_FriendComesOnline_Menus; }
	bool Social_Notifications_FriendComesOnline_Gameplay() { return m_Social_Notification_FriendComesOnline_Gameplay; }
	bool Social_Notifications_FriendGoesOffline_Menus() { return m_Social_Notification_FriendGoesOffline_Menus; }
	bool Social_Notifications_FriendGoesOffline_Gameplay() { return m_Social_Notification_FriendGoesOffline_Gameplay; }
	bool Social_Notifications_PlayerAcceptsRequest_Menus() { return m_Social_Notification_PlayerAcceptsRequest_Menus; }
	bool Social_Notifications_PlayerAcceptsRequest_Gameplay() { return m_Social_Notification_PlayerAcceptsRequest_Gameplay; }
    bool Social_Notifications_PlayerSendsRequest_Menus() { return m_Social_Notification_PlayerSendsRequest_Menus; }
    bool Social_Notifications_PlayerSendsRequest_Gameplay() { return m_Social_Notification_PlayerSendsRequest_Gameplay; }


	bool Debug_VerboseLogging() const { return m_bVerbose; }

	int GetChatLifeSeconds() const { return std::max<int>(m_Chat_LifeSeconds, 10); }

	void Initialize()
	{
		m_bInitialized = true;
		Load();
	}

	bool DataPacks_UseCommunityPatch() const { return m_DataPacks_UseCommunityPatch; }

	bool Network_UseAlternativeEndpoint() const { return m_Network_UseAlternativeEndpoint; }
	EHTTPVersion Network_GetHTTPVersion() const { return m_Network_HTTPVersion; }
	int Network_GetHTTPVersionForCurl() const
	{
		switch (m_Network_HTTPVersion)
		{
			case HTTP_VERSION_AUTO: return CURL_HTTP_VERSION_NONE;
			case HTTP_VERSION_1_0: return CURL_HTTP_VERSION_1_0;
			case HTTP_VERSION_1_1: return CURL_HTTP_VERSION_1_1;
			case HTTP_VERSION_2_0: return CURL_HTTP_VERSION_2_0;
			case HTTP_VERSION_3_0: return CURL_HTTP_VERSION_3;
		}

		return CURL_HTTP_VERSION_NONE;
	}

	const GenOnlineDataPackLayer& DataPacks_User() const { return m_DataPacks_User; }
	const GenOnlineDataPackLayer& DataPacks_GeneralsOnline() const { return m_DataPacks_GeneralsOnline; }
	const GenOnlineDataPackLayer& DataPacks_CommunityPatch() const { return m_DataPacks_CommunityPatch; }
	const std::vector<std::string>& DataPacks_UserExcludePaths() const { return m_DataPacks_UserExcludePaths; }

	ServiceConnectionSettings Network_GetConnectionSettings() const { std::lock_guard<std::mutex> lock(m_NetworkMutex); return m_ConnectionSettings; }
	std::string Network_GetLastRoute() const { std::lock_guard<std::mutex> lock(m_NetworkMutex); return m_LastRoute; }
	bool Network_SaveLastRoute(const std::string& host);
	bool Network_SaveWindowsLogin(bool windowsLogin);
	bool Network_SaveConnectionSettings(const ServiceConnectionSettings& settings, std::string& error);

private:
	void Load(void);
	bool Save();

private:
	// NOTE: This also works as the default creation (since we just call Save)
	const float m_Camera_MinHeight_default = 100.f;
	float m_Camera_MinHeight = m_Camera_MinHeight_default;

	const float m_Camera_MoveSpeedRatio_default = 1.f;
	float m_Camera_MoveSpeedRatio = m_Camera_MoveSpeedRatio_default;

	float m_Camera_MaxHeight_LobbyHost = GENERALS_ONLINE_DEFAULT_LOBBY_CAMERA_ZOOM;

	bool m_bInitialized = false;

	bool m_bVerbose = false;

	bool m_Render_DrawStatsOverlay = true;
	bool m_Render_LimitFramerate = true;
	int m_Render_FramerateLimit_FPSVal = 60;
	bool m_Render_AdaptiveQuality = true;
	int m_Chat_LifeSeconds = 30;

	bool m_Social_Notification_FriendComesOnline_Menus = true;
	bool m_Social_Notification_FriendComesOnline_Gameplay = true;
	bool m_Social_Notification_FriendGoesOffline_Menus = true;
	bool m_Social_Notification_FriendGoesOffline_Gameplay = true;
	bool m_Social_Notification_PlayerAcceptsRequest_Menus = true;
	bool m_Social_Notification_PlayerAcceptsRequest_Gameplay = true;
	bool m_Social_Notification_PlayerSendsRequest_Menus = true;
	bool m_Social_Notification_PlayerSendsRequest_Gameplay = true;

	// the old community patch switch of the released client is the only data_packs key of this build; the GO core layer is always on
	bool m_DataPacks_UseCommunityPatch = true;

	EHTTPVersion m_Network_HTTPVersion = EHTTPVersion::HTTP_VERSION_AUTO;
	bool m_Network_UseAlternativeEndpoint = false;

	// BIG archives above the retail ones. The layers load in this order, highest priority first. A missing or invalid field keeps its default.
	GenOnlineDataPackLayer m_DataPacks_User;
	std::vector<std::string> m_DataPacks_UserExcludePaths;	// loose files of the game folder, BIGs of the game folders and BIGs of the user layer that are left out
	GenOnlineDataPackLayer m_DataPacks_GeneralsOnline = { true, { "user:GeneralsOnlineGameData/GeneralsOnline_Core.big" } };
	GenOnlineDataPackLayer m_DataPacks_CommunityPatch = { true, { "user:GeneralsOnlineGameData/500_900_CommunityPatch_CoreINI.big" } };

	std::string m_Plugins_Anticheat = std::string();

	mutable std::mutex m_NetworkMutex;
	ServiceConnectionSettings m_ConnectionSettings;
	// host of the service route that worked last
	std::string m_LastRoute;
	// keeps keys this build does not know when a setting is saved mid-session
	std::string m_PreservedJSON = "{}";
	bool m_CanSave = true;
};
