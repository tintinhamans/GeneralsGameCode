#pragma once
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "GameNetwork/GenOnline/ServiceEndpointPolicy.h"
#include "GameNetwork/GenOnline/GenOnlinePackList.h"
#include "GameNetwork/GenOnline/GenOnlineClientOptions.h"

class GenOnlineSettings
{
public:
	GenOnlineSettings();

	float Camera_GetMinHeight() const { return m_Camera_MinHeight; }
	float Camera_GetMaxHeight_WhenLobbyHost() const { return m_Camera_MaxHeight_LobbyHost; }

	float DetermineCameraMaxHeight();

	// The keyboard camera rotate and pitch speeds and the keyboard and mouse wheel zoom speeds, value / 100 of camera.rotate_factor,
	// camera.pitch_factor and camera.zoom_factor. The camera code multiplies them by SCROLL_MULTIPLIER.
	float Camera_GetRotateFactor() const { return GenOnlineClientOptions::CameraFactorToReal(m_Camera_RotateFactor); }
	float Camera_GetPitchFactor() const { return GenOnlineClientOptions::CameraFactorToReal(m_Camera_PitchFactor); }
	float Camera_GetZoomFactor() const { return GenOnlineClientOptions::CameraFactorToReal(m_Camera_ZoomFactor); }

	// TRUE for Miles audio with Bink movies instead of OpenAL with FFmpeg movies, in builds that have both.
	bool Media_UseLegacy() const { return m_Media_Legacy; }

	int Observer_GetStatsFontSize() const { return m_Observer_StatsFontSize; }
	int Observer_GetNotificationFontSize() const { return m_Observer_NotificationFontSize; }
	bool Observer_NotificationSpecialPowerUsage() const { return m_Observer_NotificationSpecialPowerUsage; }
	bool Observer_NotificationSpecialPowerPurchase() const { return m_Observer_NotificationSpecialPowerPurchase; }
	bool Observer_NotificationMilestone() const { return m_Observer_NotificationMilestone; }
	// The font sizes are clamped to 0 to 15 and saved to client.json.
	void Observer_SetStatsFontSize(int fontSize);
	void Observer_SetNotificationFontSize(int fontSize);

	void Graphics_SetFPS(int fpsLimit, bool bLimitFramerate)
	{
		m_Render_FramerateLimit_FPSVal = fpsLimit;
		m_Render_LimitFramerate = bLimitFramerate;
		Save();
	}

	void Save_Camera_MaxHeight_WhenLobbyHost(float maxHeight)
	{
		if (maxHeight >= GENONLINE_MIN_LOBBY_CAMERA_ZOOM && maxHeight <= GENONLINE_MAX_LOBBY_CAMERA_ZOOM)
		{
			m_Camera_MaxHeight_LobbyHost = maxHeight;
			Save();
		}
	}

	bool Graphics_LimitFramerate() const { return m_Render_LimitFramerate; }
	bool Graphics_AdaptiveQuality() const { return m_Render_AdaptiveQuality; }
	int Graphics_GetFPSLimit() const
	{
		if (!m_Render_LimitFramerate)
		{
			return 1000000;
		}

		return m_Render_FramerateLimit_FPSVal;
	}

	// the interface style (retail, refit, overhaul), read once at startup
	const std::string& UI_Style() const { return m_UI_Style; }
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
		LoadPackList();
	}

	// GenOnline @feature arcticdolphin 08/11/2026 Map select favourites, keyed by the lowercased portable map path.
	bool Maps_IsFavourite(const std::string& portableMapPath) const;
	void Maps_SetFavourite(const std::string& portableMapPath, bool bFavourite);

	// TRUE when the packs come from the -packs file, FALSE when the game uses its built-in packs
	bool DataPacks_FromPackList() const { return m_bPackList; }
	// The enabled packs of the -packs file, lowest first
	const std::vector<GenOnlinePackList::Pack>& DataPacks_Stack() const { return m_PackStack; }
	// The game folder excludes in effect before loadMods, always empty because the -packs stack excludes take effect in loadMods with the packs
	const std::vector<std::string>& DataPacks_UserExcludePaths() const { return m_DataPacks_UserExcludePaths; }

	ServiceConnectionSettings Network_GetConnectionSettings() const { std::lock_guard<std::mutex> lock(m_NetworkMutex); return m_ConnectionSettings; }
	std::string Network_GetLastRoute() const { std::lock_guard<std::mutex> lock(m_NetworkMutex); return m_LastRoute; }
	bool Network_SaveLastRoute(const std::string& host);
	bool Network_SaveWindowsLogin(bool windowsLogin);
	bool Network_SaveConnectionSettings(const ServiceConnectionSettings& settings, std::string& error);

private:
	void Load(void);
	void LoadPackList();
	bool Save();

private:
	const float m_Camera_MinHeight_default = 100.f;
	float m_Camera_MinHeight = m_Camera_MinHeight_default;

	float m_Camera_MaxHeight_LobbyHost = GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM;
	int m_Camera_RotateFactor = GenOnlineClientOptions::CameraFactorDefault;
	int m_Camera_PitchFactor = GenOnlineClientOptions::CameraFactorDefault;
	int m_Camera_ZoomFactor = GenOnlineClientOptions::CameraFactorDefault;

	bool m_Media_Legacy = GenOnlineClientOptions::LegacyMediaDefault;

	int m_Observer_StatsFontSize = GenOnlineClientOptions::ObserverStatsFontSizeDefault;
	int m_Observer_NotificationFontSize = GenOnlineClientOptions::ObserverNotificationFontSizeDefault;
	bool m_Observer_NotificationSpecialPowerUsage = GenOnlineClientOptions::ObserverNotificationDefault;
	bool m_Observer_NotificationSpecialPowerPurchase = GenOnlineClientOptions::ObserverNotificationDefault;
	bool m_Observer_NotificationMilestone = GenOnlineClientOptions::ObserverNotificationDefault;

	bool m_bInitialized = false;

	bool m_bVerbose = false;

	bool m_Render_LimitFramerate = true;
	bool m_Render_AdaptiveQuality = true;
	int m_Render_FramerateLimit_FPSVal = 60;
	std::string m_UI_Style = "refit";
	int m_Chat_LifeSeconds = 30;

	bool m_Social_Notification_FriendComesOnline_Menus = true;
	bool m_Social_Notification_FriendComesOnline_Gameplay = true;
	bool m_Social_Notification_FriendGoesOffline_Menus = true;
	bool m_Social_Notification_FriendGoesOffline_Gameplay = true;
	bool m_Social_Notification_PlayerAcceptsRequest_Menus = true;
	bool m_Social_Notification_PlayerAcceptsRequest_Gameplay = true;
	bool m_Social_Notification_PlayerSendsRequest_Menus = true;
	bool m_Social_Notification_PlayerSendsRequest_Gameplay = true;

	std::set<std::string> m_Maps_Favourites;

	// The pack list of -packs, read once at startup. A missing, unusable, newer or other game file leaves the built-in packs.
	bool m_bPackList = false;
	std::vector<GenOnlinePackList::Pack> m_PackStack;
	std::vector<std::string> m_DataPacks_UserExcludePaths;	// always empty, see DataPacks_UserExcludePaths

	std::string m_Plugins_Anticheat = std::string();

	mutable std::mutex m_NetworkMutex;
	ServiceConnectionSettings m_ConnectionSettings;
	std::string m_LastRoute;
	// keeps keys this build does not know when a setting is saved mid-session
	std::string m_PreservedJSON = "{}";
	// %LOCALAPPDATA%\GeneralsOnline\client.json, written only when it exists with a schema_version this client knows
	std::wstring m_ClientFilePath;
	bool m_bClientFileWritable = false;
};
