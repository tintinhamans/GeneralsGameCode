#include "GameNetwork/GenOnline/GenOnlineSettings.h"
#include "GameNetwork/GenOnline/GenOnlineClientFile.h"
#include "Common/CommandLine.h"
#include "../json.hpp"
#include "../GenOnlineLobbyInterface.h"
#include "../GenOnlineInit.h"
#include <algorithm>
#include <iterator>
#include "GameNetwork/GenOnline/CredentialStore.h"
#include "GameNetwork/GenOnline/AntiCheat/GenOnlineAntiCheat.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "Common/GlobalData.h"

#define SETTINGS_KEY_CAMERA "camera"
#define SETTINGS_KEY_CAMERA_MIN_HEIGHT "min_height"
#define SETTINGS_KEY_CAMERA_MAX_HEIGHT_WHEN_LOBBY_HOST "max_height_only_when_lobby_host"
#define SETTINGS_KEY_CAMERA_ROTATE_FACTOR "rotate_factor"
#define SETTINGS_KEY_CAMERA_PITCH_FACTOR "pitch_factor"
#define SETTINGS_KEY_CAMERA_ZOOM_FACTOR "zoom_factor"

#define SETTINGS_KEY_MEDIA "media"
#define SETTINGS_KEY_MEDIA_LEGACY "legacy"

#define SETTINGS_KEY_OBSERVER "observer"
#define SETTINGS_KEY_OBSERVER_STATS_FONT_SIZE "stats_font_size"
#define SETTINGS_KEY_OBSERVER_NOTIFICATION_FONT_SIZE "notification_font_size"
#define SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_USAGE "notification_special_power_usage"
#define SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_PURCHASE "notification_special_power_purchase"
#define SETTINGS_KEY_OBSERVER_NOTIFICATION_MILESTONE "notification_milestone"

#define SETTINGS_KEY_RENDER "render"
#define SETTINGS_KEY_RENDER_LIMIT_FRAMERATE "limit_framerate"
#define SETTINGS_KEY_RENDER_FRAMERATE_LIMIT_FPS_VAL "fps_limit"
#define SETTINGS_KEY_RENDER_ADAPTIVE_QUALITY "adaptive_quality"
#define SETTINGS_KEY_UI "ui"
#define SETTINGS_KEY_UI_STYLE "style"

#define SETTINGS_KEY_CHAT "chat"
#define SETTINGS_KEY_CHAT_LIFE_SECONDS "duration_seconds_until_fade_out"

#define SETTINGS_KEY_SOCIAL "social"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_MENUS "notification_friend_comes_online_menus"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_GAMEPLAY "notification_friend_comes_online_gameplay"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_MENUS "notification_friend_goes_offline_menus"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_GAMEPLAY "notification_friend_goes_offline_gameplay"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_MENUS "notification_player_accepts_request_menus"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_GAMEPLAY "notification_player_accepts_request_gameplay"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_MENUS "notification_player_sends_request_menus"
#define SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_GAMEPLAY "notification_player_sends_request_gameplay"
#define SETTINGS_KEY_SOCIAL_PRESENCE_STATUS "presence_status"

#define SETTINGS_KEY_DEBUG "debug"
#define SETTINGS_KEY_STATS "stats"
#define SETTINGS_KEY_STATS_EXPORT "export"
#define SETTINGS_KEY_LOCAL_API "local_api"
#define SETTINGS_KEY_LOCAL_API_ENABLED "enabled"
#define SETTINGS_KEY_DEBUG_ENABLED "enabled"

#define SETTINGS_KEY_NETWORK "network"
#define SETTINGS_KEY_NETWORK_SERVER "server"
#define SETTINGS_KEY_NETWORK_SERVER_MODE "mode"
#define SETTINGS_KEY_NETWORK_SERVER_ADDRESS "address"
#define SETTINGS_KEY_NETWORK_PROXY "proxy"
#define SETTINGS_KEY_NETWORK_PROXY_MODE "mode"
#define SETTINGS_KEY_NETWORK_PROXY_ADDRESS "address"
#define SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN "windows_login"

#define SETTINGS_KEY_PLUGINS "plugins"
#define SETTINGS_KEY_PLUGINS_ANTICHEAT "anticheat"
#define SETTINGS_KEY_PLUGINS_TRANSPORT "transport"
#define SETTINGS_KEY_PLUGINS_SETTINGS "settings"

#define SETTINGS_KEY_MAPS "maps"
#define SETTINGS_KEY_MAPS_FAVOURITES "favourites"

#define SETTINGS_KEY_DATA "data"
#define SETTINGS_KEY_DATA_INCLUDE "include"
#define SETTINGS_KEY_DATA_EXCLUDE "exclude"

GenOnlineSettings::GenOnlineSettings()
{
	
}

float GenOnlineSettings::DetermineCameraMaxHeight()
{
	// Are we in a lobby? use it's settings
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();

	if (pLobbyInterface != nullptr)
	{
		if (pLobbyInterface->IsInLobby())
		{
			LobbyEntry& theLobby = pLobbyInterface->GetCurrentLobby();
			return (float)theLobby.max_cam_height;
		}
	}

	return (float)GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM;
}

std::string GenOnlineSettings::GetAnticheatPlugin() const
{
    // The client runs one anticheat. An empty list means the built-in GenOnline Anti-Cheat.
    if (m_Plugins_Anticheat.empty())
    {
        return std::string();
    }
    if (m_Plugins_Anticheat.size() > 1)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] plugins.anticheat lists %d providers, using the first one", (int)m_Plugins_Anticheat.size());
    }

    // GenOnline Anti-Cheat is built into the game and loads no plugin folder.
    const std::string& choice = m_Plugins_Anticheat.front();
    if (choice == GENONLINE_ANTICHEAT_PLUGIN_NAME)
    {
        return std::string();
    }
    return choice;
}

std::vector<std::string> GenOnlineSettings::GetTransportPlugins() const
{
    std::vector<std::string> transports = m_Plugins_Transport;

    // An external anticheat plugin also offers its transport. A plugin without a transport table keeps its other roles.
    const std::string anticheatPlugin = GetAnticheatPlugin();
    if (!anticheatPlugin.empty() && std::find(transports.begin(), transports.end(), anticheatPlugin) == transports.end())
    {
        transports.push_back(anticheatPlugin);
    }
    return transports;
}

// Reads a list of plugin names. Entries that are not strings are dropped with one log line.
static std::vector<std::string> ReadPluginList(const nlohmann::json& list, const char* key)
{
    std::vector<std::string> names;
    for (const nlohmann::json& entry : list)
    {
        if (entry.is_string())
        {
            names.push_back(entry.get<std::string>());
        }
        else
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Ignoring an entry of plugins.%s that is not a plugin name", key);
        }
    }
    return names;
}

// Reads a list of strings, entries that are not non-empty strings are dropped with one log line.
static std::vector<std::string> ReadStringList(const nlohmann::json& list, const char* key)
{
	std::vector<std::string> values;
	for (const nlohmann::json& entry : list)
	{
		if (entry.is_string() && !entry.get<std::string>().empty())
		{
			values.push_back(entry.get<std::string>());
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Ignoring an entry of data.%s that is not a path", key);
		}
	}
	return values;
}

static bool IsPresenceStatus(const std::string& status)
{
	return status == "online" || status == "idle" || status == "do_not_disturb" || status == "invisible";
}

// The keys settings.client.json holds and the type each one must have. A key of another type makes the whole file unusable.
static const std::vector<GenOnlineClientFile::KnownKey>& ClientFileKeys()
{
	using GenOnlineClientFile::ValueType;
	static const std::vector<GenOnlineClientFile::KnownKey> keys = {
		{ SETTINGS_KEY_CAMERA, ValueType::Object },
		{ SETTINGS_KEY_CAMERA "." SETTINGS_KEY_CAMERA_MIN_HEIGHT, ValueType::Number },
		{ SETTINGS_KEY_CAMERA "." SETTINGS_KEY_CAMERA_MAX_HEIGHT_WHEN_LOBBY_HOST, ValueType::Number },
		{ SETTINGS_KEY_CAMERA "." SETTINGS_KEY_CAMERA_ROTATE_FACTOR, ValueType::Number },
		{ SETTINGS_KEY_CAMERA "." SETTINGS_KEY_CAMERA_PITCH_FACTOR, ValueType::Number },
		{ SETTINGS_KEY_CAMERA "." SETTINGS_KEY_CAMERA_ZOOM_FACTOR, ValueType::Number },
		{ SETTINGS_KEY_MEDIA, ValueType::Object },
		{ SETTINGS_KEY_MEDIA "." SETTINGS_KEY_MEDIA_LEGACY, ValueType::Boolean },
		{ SETTINGS_KEY_OBSERVER, ValueType::Object },
		{ SETTINGS_KEY_OBSERVER "." SETTINGS_KEY_OBSERVER_STATS_FONT_SIZE, ValueType::Number },
		{ SETTINGS_KEY_OBSERVER "." SETTINGS_KEY_OBSERVER_NOTIFICATION_FONT_SIZE, ValueType::Number },
		{ SETTINGS_KEY_OBSERVER "." SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_USAGE, ValueType::Boolean },
		{ SETTINGS_KEY_OBSERVER "." SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_PURCHASE, ValueType::Boolean },
		{ SETTINGS_KEY_OBSERVER "." SETTINGS_KEY_OBSERVER_NOTIFICATION_MILESTONE, ValueType::Boolean },
		{ SETTINGS_KEY_RENDER, ValueType::Object },
		{ SETTINGS_KEY_RENDER "." SETTINGS_KEY_RENDER_LIMIT_FRAMERATE, ValueType::Boolean },
		{ SETTINGS_KEY_RENDER "." SETTINGS_KEY_RENDER_FRAMERATE_LIMIT_FPS_VAL, ValueType::Number },
		{ SETTINGS_KEY_RENDER "." SETTINGS_KEY_RENDER_ADAPTIVE_QUALITY, ValueType::Boolean },
		{ SETTINGS_KEY_UI, ValueType::Object },
		{ SETTINGS_KEY_UI "." SETTINGS_KEY_UI_STYLE, ValueType::String },
		{ SETTINGS_KEY_CHAT, ValueType::Object },
		{ SETTINGS_KEY_CHAT "." SETTINGS_KEY_CHAT_LIFE_SECONDS, ValueType::Number },
		{ SETTINGS_KEY_SOCIAL, ValueType::Object },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_MENUS, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_GAMEPLAY, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_MENUS, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_GAMEPLAY, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_MENUS, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_GAMEPLAY, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_MENUS, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_GAMEPLAY, ValueType::Boolean },
		{ SETTINGS_KEY_SOCIAL "." SETTINGS_KEY_SOCIAL_PRESENCE_STATUS, ValueType::String },
		{ SETTINGS_KEY_LOCAL_API, ValueType::Object },
		{ SETTINGS_KEY_LOCAL_API "." SETTINGS_KEY_LOCAL_API_ENABLED, ValueType::Boolean },
		{ SETTINGS_KEY_DEBUG, ValueType::Object },
		{ SETTINGS_KEY_DEBUG "." SETTINGS_KEY_DEBUG_ENABLED, ValueType::Boolean },
		{ SETTINGS_KEY_NETWORK, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER "." SETTINGS_KEY_NETWORK_SERVER_MODE, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER "." SETTINGS_KEY_NETWORK_SERVER_ADDRESS, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_MODE, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_ADDRESS, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN, ValueType::Boolean },
		{ SETTINGS_KEY_PLUGINS, ValueType::Object },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_ANTICHEAT, ValueType::Array },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_TRANSPORT, ValueType::Array },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_SETTINGS, ValueType::Object },
		{ SETTINGS_KEY_DATA, ValueType::Object },
		{ SETTINGS_KEY_DATA "." SETTINGS_KEY_DATA_INCLUDE, ValueType::Array },
		{ SETTINGS_KEY_DATA "." SETTINGS_KEY_DATA_EXCLUDE, ValueType::Array },
		{ SETTINGS_KEY_MAPS, ValueType::Object },
		{ SETTINGS_KEY_MAPS "." SETTINGS_KEY_MAPS_FAVOURITES, ValueType::Array },
	};
	return keys;
}

void GenOnlineSettings::Load(void)
{
	ServiceConnection::ConfigureCredentialStore(&GenOnlineAuthInterface::GetCredentialsFilePath);
	ServiceConnection::SetCredentialLogSink([](const std::string& line) { NetworkLog(ELogVerbosity::LOG_RELEASE, "%s", line.c_str()); });

	m_ClientFilePath = GenOnlineClientFile::DefaultPath();
	m_bClientFileWritable = false;
	m_PreservedJSON = "{}";

	GenOnlineClientFile::ReadResult clientFile;
	if (m_ClientFilePath.empty())
	{
		clientFile.status = GenOnlineClientFile::ReadStatus::Unusable;
		clientFile.reason = "the local application data folder cannot be resolved";
	}
	else
	{
		clientFile = GenOnlineClientFile::Read(m_ClientFilePath, ClientFileKeys());
	}

	// A missing settings.client.json is created with the built-in defaults, which the first save below writes into it.
	if (clientFile.status == GenOnlineClientFile::ReadStatus::Missing)
	{
		nlohmann::json created = nlohmann::json::object();
		created[GenOnlineClientFile::SchemaVersionKey] = GenOnlineClientFile::SchemaVersion;
		if (GenOnlineClientFile::CreateNew(m_ClientFilePath, created.dump(2)))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Created settings.client.json with the built-in defaults");
			clientFile.status = GenOnlineClientFile::ReadStatus::Ok;
			clientFile.document = created;
			clientFile.schemaVersion = GenOnlineClientFile::SchemaVersion;
		}
	}

	// The client never writes a settings.client.json it cannot use, the settings then only last for this session.
	switch (clientFile.status)
	{
	case GenOnlineClientFile::ReadStatus::Missing:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] settings.client.json cannot be created, using the built-in defaults without saving");
		return;
	case GenOnlineClientFile::ReadStatus::Unusable:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] settings.client.json is not usable (%s), using the defaults this session and leaving the file untouched", clientFile.reason.c_str());
		return;
	case GenOnlineClientFile::ReadStatus::Newer:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] settings.client.json has schema_version %d, newer than %d, reading it without saving", clientFile.schemaVersion, GenOnlineClientFile::SchemaVersion);
		break;
	case GenOnlineClientFile::ReadStatus::Ok:
		break;
	}

	bool bWritable = clientFile.status == GenOnlineClientFile::ReadStatus::Ok;

	// A launch with a settings overlay never saves settings.client.json, so the overlay values cannot reach the file.
	for (const std::string& overlayValue : CommandLine::getSettingsOverlays())
	{
		bWritable = false;

		nlohmann::json overlay;
		std::string reason;
		if (GenOnlineClientFile::ParseOverlay(overlayValue, overlay, reason))
		{
			clientFile.document = GenOnlineClientFile::MergeOverlay(clientFile.document, overlay);
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Applied a settings overlay, settings.client.json is not saved this session");
		}
		else
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Ignored a settings overlay (%s), settings.client.json is not saved this session", reason.c_str());
		}
	}

	bool bApplyDefaults = false;

	{
		const nlohmann::json& jsonSettings = clientFile.document;
		try
		{
			if (jsonSettings.contains(SETTINGS_KEY_CAMERA))
			{
				auto cameraSettings = jsonSettings[SETTINGS_KEY_CAMERA];

				if (cameraSettings.contains(SETTINGS_KEY_CAMERA_MIN_HEIGHT))
				{
					m_Camera_MinHeight = std::max<float>(static_cast<float>(cameraSettings[SETTINGS_KEY_CAMERA_MIN_HEIGHT]), m_Camera_MinHeight_default);
				}

				if (cameraSettings.contains(SETTINGS_KEY_CAMERA_MAX_HEIGHT_WHEN_LOBBY_HOST))
				{
					m_Camera_MaxHeight_LobbyHost = std::clamp<float>(static_cast<float>(cameraSettings[SETTINGS_KEY_CAMERA_MAX_HEIGHT_WHEN_LOBBY_HOST]), GENONLINE_MIN_LOBBY_CAMERA_ZOOM, GENONLINE_MAX_LOBBY_CAMERA_ZOOM);
				}

				if (cameraSettings.contains(SETTINGS_KEY_CAMERA_ROTATE_FACTOR))
				{
					m_Camera_RotateFactor = GenOnlineClientOptions::ClampCameraFactor(cameraSettings[SETTINGS_KEY_CAMERA_ROTATE_FACTOR].get<double>());
				}

				if (cameraSettings.contains(SETTINGS_KEY_CAMERA_PITCH_FACTOR))
				{
					m_Camera_PitchFactor = GenOnlineClientOptions::ClampCameraFactor(cameraSettings[SETTINGS_KEY_CAMERA_PITCH_FACTOR].get<double>());
				}

				if (cameraSettings.contains(SETTINGS_KEY_CAMERA_ZOOM_FACTOR))
				{
					m_Camera_ZoomFactor = GenOnlineClientOptions::ClampCameraFactor(cameraSettings[SETTINGS_KEY_CAMERA_ZOOM_FACTOR].get<double>());
				}
			}

			if (jsonSettings.contains(SETTINGS_KEY_MEDIA))
			{
				const auto mediaSettings = jsonSettings[SETTINGS_KEY_MEDIA];

				if (mediaSettings.contains(SETTINGS_KEY_MEDIA_LEGACY))
				{
					m_Media_Legacy = mediaSettings[SETTINGS_KEY_MEDIA_LEGACY].get<bool>();
				}
			}

			if (jsonSettings.contains(SETTINGS_KEY_OBSERVER))
			{
				const auto observerSettings = jsonSettings[SETTINGS_KEY_OBSERVER];

				if (observerSettings.contains(SETTINGS_KEY_OBSERVER_STATS_FONT_SIZE))
				{
					m_Observer_StatsFontSize = GenOnlineClientOptions::ClampObserverFontSize(observerSettings[SETTINGS_KEY_OBSERVER_STATS_FONT_SIZE].get<double>());
				}

				if (observerSettings.contains(SETTINGS_KEY_OBSERVER_NOTIFICATION_FONT_SIZE))
				{
					m_Observer_NotificationFontSize = GenOnlineClientOptions::ClampObserverFontSize(observerSettings[SETTINGS_KEY_OBSERVER_NOTIFICATION_FONT_SIZE].get<double>());
				}

				if (observerSettings.contains(SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_USAGE))
				{
					m_Observer_NotificationSpecialPowerUsage = observerSettings[SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_USAGE].get<bool>();
				}

				if (observerSettings.contains(SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_PURCHASE))
				{
					m_Observer_NotificationSpecialPowerPurchase = observerSettings[SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_PURCHASE].get<bool>();
				}

				if (observerSettings.contains(SETTINGS_KEY_OBSERVER_NOTIFICATION_MILESTONE))
				{
					m_Observer_NotificationMilestone = observerSettings[SETTINGS_KEY_OBSERVER_NOTIFICATION_MILESTONE].get<bool>();
				}
			}

			if (jsonSettings.contains(SETTINGS_KEY_RENDER))
			{
				auto renderSettings = jsonSettings[SETTINGS_KEY_RENDER];

				if (renderSettings.contains(SETTINGS_KEY_RENDER_LIMIT_FRAMERATE))
				{
					m_Render_LimitFramerate = renderSettings[SETTINGS_KEY_RENDER_LIMIT_FRAMERATE];
				}

				if (renderSettings.contains(SETTINGS_KEY_RENDER_FRAMERATE_LIMIT_FPS_VAL))
				{
					m_Render_FramerateLimit_FPSVal = renderSettings[SETTINGS_KEY_RENDER_FRAMERATE_LIMIT_FPS_VAL];
				}

				if (renderSettings.contains(SETTINGS_KEY_RENDER_ADAPTIVE_QUALITY))
				{
					m_Render_AdaptiveQuality = renderSettings[SETTINGS_KEY_RENDER_ADAPTIVE_QUALITY];
				}
			}

			// an unknown style falls back to refit, the save then writes it
			if (jsonSettings.contains(SETTINGS_KEY_UI) && jsonSettings[SETTINGS_KEY_UI].is_object())
			{
				const auto uiSettings = jsonSettings[SETTINGS_KEY_UI];
				if (uiSettings.contains(SETTINGS_KEY_UI_STYLE) && uiSettings[SETTINGS_KEY_UI_STYLE].is_string())
				{
					const std::string style = uiSettings[SETTINGS_KEY_UI_STYLE].get<std::string>();
					m_UI_Style = (style == "retail" || style == "overhaul") ? style : std::string("refit");
				}
			}

            if (jsonSettings.contains(SETTINGS_KEY_NETWORK))
            {
                // Any network value that is unknown or malformed goes back to its default and is logged, one line per field.
                // The load ends with a save when the file may be written, so the file does not keep the invalid value. Addresses can hold a secret, so their old value is not logged.
                auto resetField = [](const std::string& path, const nlohmann::json& old, bool secret)
                {
                    const std::string oldText = secret ? std::string("hidden") : old.is_string() ? old.get<std::string>().substr(0, 64) : std::string(old.type_name());
                    NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] %s is not valid (%s), reset to its default", path.c_str(), oldText.c_str());
                };
                auto choice = [&](const nlohmann::json& section, const std::string& path, const char* key, std::string& value, std::initializer_list<const char*> allowed)
                {
                    if (!section.contains(key))
                    {
                        return;
                    }
                    const auto& found = section[key];
                    if (found.is_string() && std::find_if(allowed.begin(), allowed.end(), [&](const char* name) { return found.get<std::string>() == name; }) != allowed.end())
                    {
                        value = found.get<std::string>();
                        return;
                    }
                    resetField(path + "." + key, found, false);
                };
                // a section that is present but not an object goes back to its defaults
                auto section = [&](const nlohmann::json& parent, const std::string& path, const char* key) -> const nlohmann::json*
                {
                    if (!parent.contains(key))
                    {
                        return nullptr;
                    }
                    if (!parent[key].is_object())
                    {
                        resetField(path + "." + key, parent[key], false);
                        return nullptr;
                    }
                    return &parent[key];
                };

                if (!jsonSettings[SETTINGS_KEY_NETWORK].is_object())
                {
                    resetField(SETTINGS_KEY_NETWORK, jsonSettings[SETTINGS_KEY_NETWORK], false);
                }
                else
                {
                    const auto& networkSettings = jsonSettings[SETTINGS_KEY_NETWORK];

                    if (const auto* serverSettings = section(networkSettings, SETTINGS_KEY_NETWORK, SETTINGS_KEY_NETWORK_SERVER))
                    {
                        const std::string path = std::string(SETTINGS_KEY_NETWORK) + "." + SETTINGS_KEY_NETWORK_SERVER;
                        choice(*serverSettings, path, SETTINGS_KEY_NETWORK_SERVER_MODE, m_ConnectionSettings.serverMode, { "automatic", "custom" });
                        if (serverSettings->contains(SETTINGS_KEY_NETWORK_SERVER_ADDRESS))
                        {
                            const auto& found = (*serverSettings)[SETTINGS_KEY_NETWORK_SERVER_ADDRESS];
                            std::string endpointError;
                            if (found.is_string() && (found.get<std::string>().empty() ||
                                ServiceConnection::NormalizeCustomEndpoint(found.get<std::string>(), m_ConnectionSettings.serverAddress, endpointError)))
                            {
                                // an empty address stays empty
                            }
                            else
                            {
                                resetField(path + "." + SETTINGS_KEY_NETWORK_SERVER_ADDRESS, found, true);
                                m_ConnectionSettings.serverAddress.clear();
                            }
                        }
                    }

                    if (const auto* proxySettings = section(networkSettings, SETTINGS_KEY_NETWORK, SETTINGS_KEY_NETWORK_PROXY))
                    {
                        const std::string path = std::string(SETTINGS_KEY_NETWORK) + "." + SETTINGS_KEY_NETWORK_PROXY;
                        choice(*proxySettings, path, SETTINGS_KEY_NETWORK_PROXY_MODE, m_ConnectionSettings.proxyMode, { "automatic", "disabled", "custom" });
                        if (proxySettings->contains(SETTINGS_KEY_NETWORK_PROXY_ADDRESS))
                        {
                            const auto& found = (*proxySettings)[SETTINGS_KEY_NETWORK_PROXY_ADDRESS];
                            std::string proxyError;
                            if (found.is_string() && (found.get<std::string>().empty() || ServiceConnection::ValidateProxyURL(found.get<std::string>(), proxyError)))
                            {
                                m_ConnectionSettings.proxyURL = found.get<std::string>();
                            }
                            else
                            {
                                resetField(path + "." + SETTINGS_KEY_NETWORK_PROXY_ADDRESS, found, true);
                            }
                        }
                        if (proxySettings->contains(SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN))
                        {
                            const auto& found = (*proxySettings)[SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN];
                            if (found.is_boolean())
                            {
                                m_ConnectionSettings.windowsLogin = found.get<bool>();
                            }
                            else
                            {
                                resetField(path + "." + SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN, found, false);
                            }
                        }
                    }
                }
            }

			if (jsonSettings.contains(SETTINGS_KEY_STATS))
			{
				const auto& statsSettings = jsonSettings[SETTINGS_KEY_STATS];

				if (statsSettings.is_object() && statsSettings.contains(SETTINGS_KEY_STATS_EXPORT) && statsSettings[SETTINGS_KEY_STATS_EXPORT].is_boolean())
				{
					m_bStatsExport = statsSettings[SETTINGS_KEY_STATS_EXPORT];
				}
			}

			if (jsonSettings.contains(SETTINGS_KEY_LOCAL_API))
			{
				const auto& localApiSettings = jsonSettings[SETTINGS_KEY_LOCAL_API];

				if (localApiSettings.is_object() && localApiSettings.contains(SETTINGS_KEY_LOCAL_API_ENABLED) && localApiSettings[SETTINGS_KEY_LOCAL_API_ENABLED].is_boolean())
				{
					m_bLocalApiEnabled = localApiSettings[SETTINGS_KEY_LOCAL_API_ENABLED];
				}
			}

			if (jsonSettings.contains(SETTINGS_KEY_DEBUG))
			{
				auto debugSettings = jsonSettings[SETTINGS_KEY_DEBUG];

				if (debugSettings.contains(SETTINGS_KEY_DEBUG_ENABLED))
				{
					m_bDebugMode = debugSettings[SETTINGS_KEY_DEBUG_ENABLED];
				}
			}


			if (jsonSettings.contains(SETTINGS_KEY_CHAT))
			{
				auto chatSettings = jsonSettings[SETTINGS_KEY_CHAT];

				if (chatSettings.contains(SETTINGS_KEY_CHAT_LIFE_SECONDS))
				{
					m_Chat_LifeSeconds = chatSettings[SETTINGS_KEY_CHAT_LIFE_SECONDS];
				}
			}

            if (jsonSettings.contains(SETTINGS_KEY_SOCIAL))
            {
                auto socialSettings = jsonSettings[SETTINGS_KEY_SOCIAL];

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_MENUS))
                {
					m_Social_Notification_FriendComesOnline_Menus = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_MENUS];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_GAMEPLAY))
                {
					m_Social_Notification_FriendComesOnline_Gameplay = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_GAMEPLAY];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_MENUS))
                {
					m_Social_Notification_FriendGoesOffline_Menus = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_MENUS];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_GAMEPLAY))
                {
					m_Social_Notification_FriendGoesOffline_Gameplay = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_GAMEPLAY];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_MENUS))
                {
                    m_Social_Notification_PlayerAcceptsRequest_Menus = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_MENUS];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_GAMEPLAY))
                {
                    m_Social_Notification_PlayerAcceptsRequest_Gameplay = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_GAMEPLAY];
                }
                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_MENUS))
                {
                    m_Social_Notification_PlayerSendsRequest_Menus = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_MENUS];
                }

                if (socialSettings.contains(SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_GAMEPLAY))
                {
                    m_Social_Notification_PlayerSendsRequest_Gameplay = socialSettings[SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_GAMEPLAY];
                }

				if (socialSettings.contains(SETTINGS_KEY_SOCIAL_PRESENCE_STATUS))
				{
					const std::string status = socialSettings[SETTINGS_KEY_SOCIAL_PRESENCE_STATUS];
					if (IsPresenceStatus(status))
					{
						m_Social_PresenceStatus = status;
					}
				}
            }

			m_Plugins_Transport.clear();
			if (jsonSettings.contains(SETTINGS_KEY_PLUGINS))
			{
				auto pluginSettings = jsonSettings[SETTINGS_KEY_PLUGINS];

				if (pluginSettings.contains(SETTINGS_KEY_PLUGINS_ANTICHEAT))
				{
					m_Plugins_Anticheat = ReadPluginList(pluginSettings[SETTINGS_KEY_PLUGINS_ANTICHEAT], SETTINGS_KEY_PLUGINS_ANTICHEAT);
				}
				if (pluginSettings.contains(SETTINGS_KEY_PLUGINS_TRANSPORT))
				{
					m_Plugins_Transport = ReadPluginList(pluginSettings[SETTINGS_KEY_PLUGINS_TRANSPORT], SETTINGS_KEY_PLUGINS_TRANSPORT);
				}
			}

			// The launcher owns the data lists. The game reads them and never writes them.
			m_bDataInclude = false;
			m_Data_Include.clear();
			m_Data_Exclude.clear();
			if (jsonSettings.contains(SETTINGS_KEY_DATA) && jsonSettings[SETTINGS_KEY_DATA].is_object())
			{
				const nlohmann::json& dataSettings = jsonSettings[SETTINGS_KEY_DATA];
				if (dataSettings.contains(SETTINGS_KEY_DATA_INCLUDE) && dataSettings[SETTINGS_KEY_DATA_INCLUDE].is_array())
				{
					m_bDataInclude = true;
					m_Data_Include = ReadStringList(dataSettings[SETTINGS_KEY_DATA_INCLUDE], SETTINGS_KEY_DATA_INCLUDE);
				}
				if (dataSettings.contains(SETTINGS_KEY_DATA_EXCLUDE) && dataSettings[SETTINGS_KEY_DATA_EXCLUDE].is_array())
				{
					m_Data_Exclude = ReadStringList(dataSettings[SETTINGS_KEY_DATA_EXCLUDE], SETTINGS_KEY_DATA_EXCLUDE);
				}
			}

            if (jsonSettings.contains(SETTINGS_KEY_MAPS))
            {
                auto mapSettings = jsonSettings[SETTINGS_KEY_MAPS];

                // GenOnline @feature arcticdolphin 08/11/2026 Map select favourites, entries that are not strings are dropped.
                if (mapSettings.contains(SETTINGS_KEY_MAPS_FAVOURITES) && mapSettings[SETTINGS_KEY_MAPS_FAVOURITES].is_array())
                {
                    m_Maps_Favourites.clear();
                    for (const auto& favourite : mapSettings[SETTINGS_KEY_MAPS_FAVOURITES])
                    {
                        if (favourite.is_string() && !favourite.get<std::string>().empty())
                        {
                            AsciiString lowered(favourite.get<std::string>().c_str());
                            lowered.toLower();
                            m_Maps_Favourites.insert(std::string(lowered.str()));
                        }
                    }
                }
            }
		}
		catch (...)
		{
			// a value of the wrong type
			bApplyDefaults = true;
		}
	}

	if (bApplyDefaults)
	{
		m_Camera_MinHeight = m_Camera_MinHeight_default;
		m_Camera_MaxHeight_LobbyHost = GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM;
		m_bDebugMode = false;
		m_bStatsExport = false;
		m_Render_LimitFramerate = true;
		m_Render_FramerateLimit_FPSVal = 60;
		m_Render_AdaptiveQuality = true;
		m_Chat_LifeSeconds = 30;

        m_Social_Notification_FriendComesOnline_Menus = true;
        m_Social_Notification_FriendComesOnline_Gameplay = true;
        m_Social_Notification_FriendGoesOffline_Menus = true;
        m_Social_Notification_FriendGoesOffline_Gameplay = true;
        m_Social_PresenceStatus = "online";

		m_Camera_RotateFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Camera_PitchFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Camera_ZoomFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Media_Legacy = GenOnlineClientOptions::LegacyMediaDefault;
		m_Observer_StatsFontSize = GenOnlineClientOptions::ObserverStatsFontSizeDefault;
		m_Observer_NotificationFontSize = GenOnlineClientOptions::ObserverNotificationFontSizeDefault;
		m_Observer_NotificationSpecialPowerUsage = GenOnlineClientOptions::ObserverNotificationDefault;
		m_Observer_NotificationSpecialPowerPurchase = GenOnlineClientOptions::ObserverNotificationDefault;
		m_Observer_NotificationMilestone = GenOnlineClientOptions::ObserverNotificationDefault;

		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] settings.client.json is not usable (a value cannot be read), using the defaults this session and leaving the file untouched");
		return;
	}

	// The first save writes every setting, which fills the keys the file does not have yet.
	m_PreservedJSON = "{}";
	m_bClientFileWritable = bWritable;

	// The save writes keys that are new or missing and skips a file that already holds every value.
	Save();
}

void GenOnlineSettings::Social_SetPresenceStatus(const std::string& status)
{
	if (!IsPresenceStatus(status) || status == m_Social_PresenceStatus)
	{
		return;
	}
	m_Social_PresenceStatus = status;
	Save();
}

void GenOnlineSettings::Observer_SetStatsFontSize(int fontSize)
{
	const int clamped = GenOnlineClientOptions::ClampObserverFontSize(fontSize);
	if (clamped == m_Observer_StatsFontSize)
	{
		return;
	}
	m_Observer_StatsFontSize = clamped;
	Save();
}

void GenOnlineSettings::Observer_SetNotificationFontSize(int fontSize)
{
	const int clamped = GenOnlineClientOptions::ClampObserverFontSize(fontSize);
	if (clamped == m_Observer_NotificationFontSize)
	{
		return;
	}
	m_Observer_NotificationFontSize = clamped;
	Save();
}

// Copies the values of after that differ from before into target, and removes from target the keys that after no longer has.
// Objects are compared key by key, any other value is replaced whole.
static void ApplyChangedSettings(const nlohmann::json& before, const nlohmann::json& after, nlohmann::json& target)
{
	if (!target.is_object())
	{
		target = nlohmann::json::object();
	}
	for (auto it = after.begin(); it != after.end(); ++it)
	{
		const bool bHadKey = before.is_object() && before.contains(it.key());
		if (it.value().is_object() && bHadKey && before[it.key()].is_object())
		{
			ApplyChangedSettings(before[it.key()], it.value(), target[it.key()]);
		}
		else if (!bHadKey || before[it.key()] != it.value())
		{
			target[it.key()] = it.value();
		}
	}
	if (before.is_object())
	{
		for (auto it = before.begin(); it != before.end(); ++it)
		{
			if (!after.contains(it.key()))
			{
				target.erase(it.key());
			}
		}
	}
}

std::vector<std::string> GenOnlineSettings::Data_ExcludePaths() const
{
	std::vector<std::string> paths;
	for (const std::string& entry : m_Data_Exclude)
	{
		const std::string prefix = entry.substr(0, 5);
		const bool hasBase = _strnicmp(prefix.c_str(), "game:", 5) == 0 || _strnicmp(prefix.c_str(), "user:", 5) == 0;
		const bool isAbsolute = (entry.size() > 2 && isalpha(static_cast<unsigned char>(entry[0])) && entry[1] == ':' && (entry[2] == '\\' || entry[2] == '/'))
			|| (entry.size() > 1 && (entry[0] == '\\' || entry[0] == '/') && (entry[1] == '\\' || entry[1] == '/'));
		paths.push_back(hasBase || isAbsolute ? entry : "game:" + entry);
	}
	return paths;
}

bool GenOnlineSettings::Save()
{
	if (!m_bInitialized)
	{
		Initialize();
	}

	// Without a settings.client.json this client may write, a change only lasts for this session.
	if (!m_bClientFileWritable)
	{
		return true;
	}

	nlohmann::json root = {
		  {
				SETTINGS_KEY_CAMERA,
				{
					{SETTINGS_KEY_CAMERA_MIN_HEIGHT, m_Camera_MinHeight},
					{SETTINGS_KEY_CAMERA_MAX_HEIGHT_WHEN_LOBBY_HOST, m_Camera_MaxHeight_LobbyHost},
					{SETTINGS_KEY_CAMERA_ROTATE_FACTOR, m_Camera_RotateFactor},
					{SETTINGS_KEY_CAMERA_PITCH_FACTOR, m_Camera_PitchFactor},
					{SETTINGS_KEY_CAMERA_ZOOM_FACTOR, m_Camera_ZoomFactor},
				}
		  },

		{
			SETTINGS_KEY_MEDIA,
				{
					{SETTINGS_KEY_MEDIA_LEGACY, m_Media_Legacy},
				}
		},

		{
			SETTINGS_KEY_OBSERVER,
				{
					{SETTINGS_KEY_OBSERVER_STATS_FONT_SIZE, m_Observer_StatsFontSize},
					{SETTINGS_KEY_OBSERVER_NOTIFICATION_FONT_SIZE, m_Observer_NotificationFontSize},
					{SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_USAGE, m_Observer_NotificationSpecialPowerUsage},
					{SETTINGS_KEY_OBSERVER_NOTIFICATION_SPECIAL_POWER_PURCHASE, m_Observer_NotificationSpecialPowerPurchase},
					{SETTINGS_KEY_OBSERVER_NOTIFICATION_MILESTONE, m_Observer_NotificationMilestone},
				}
		},

		{
			SETTINGS_KEY_RENDER,
				{
					{SETTINGS_KEY_RENDER_LIMIT_FRAMERATE, m_Render_LimitFramerate},
					{SETTINGS_KEY_RENDER_FRAMERATE_LIMIT_FPS_VAL, m_Render_FramerateLimit_FPSVal},
					{SETTINGS_KEY_RENDER_ADAPTIVE_QUALITY, m_Render_AdaptiveQuality},
				}
		},

		{
			SETTINGS_KEY_UI,
				{
					{SETTINGS_KEY_UI_STYLE, m_UI_Style},
				}
		},

        {
            SETTINGS_KEY_NETWORK,
                {
                    {SETTINGS_KEY_NETWORK_SERVER, {
                        {SETTINGS_KEY_NETWORK_SERVER_MODE, m_ConnectionSettings.serverMode},
                        {SETTINGS_KEY_NETWORK_SERVER_ADDRESS, m_ConnectionSettings.serverAddress}}},
                    {SETTINGS_KEY_NETWORK_PROXY, {
                        {SETTINGS_KEY_NETWORK_PROXY_MODE, m_ConnectionSettings.proxyMode},
                        {SETTINGS_KEY_NETWORK_PROXY_ADDRESS, m_ConnectionSettings.proxyURL},
                        {SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN, m_ConnectionSettings.windowsLogin}}}
                }
        },

		{
			SETTINGS_KEY_CHAT,
				{
					{SETTINGS_KEY_CHAT_LIFE_SECONDS, m_Chat_LifeSeconds}
				}
		},

		{
			SETTINGS_KEY_STATS,
				{
					{SETTINGS_KEY_STATS_EXPORT, m_bStatsExport},
				}
		},

		{
			SETTINGS_KEY_LOCAL_API,
				{
					{SETTINGS_KEY_LOCAL_API_ENABLED, m_bLocalApiEnabled},
				}
		},

		{
			SETTINGS_KEY_DEBUG,
				{
					{SETTINGS_KEY_DEBUG_ENABLED, m_bDebugMode},
				}
		},

        {
			SETTINGS_KEY_SOCIAL,
                {
                    {SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_MENUS, m_Social_Notification_FriendComesOnline_Menus},
					{SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_COMES_ONLINE_GAMEPLAY, m_Social_Notification_FriendComesOnline_Gameplay},
					{SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_MENUS, m_Social_Notification_FriendGoesOffline_Menus},
					{SETTINGS_KEY_SOCIAL_NOTIFICATIONS_FRIEND_GOES_OFFLINE_GAMEPLAY, m_Social_Notification_FriendGoesOffline_Gameplay},
					{SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_MENUS, m_Social_Notification_PlayerAcceptsRequest_Menus},
					{SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_ACCEPTS_REQUEST_GAMEPLAY, m_Social_Notification_PlayerAcceptsRequest_Gameplay},
                    {SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_MENUS, m_Social_Notification_PlayerSendsRequest_Menus},
                    {SETTINGS_KEY_SOCIAL_NOTIFICATIONS_PLAYER_SENDS_REQUEST_GAMEPLAY, m_Social_Notification_PlayerSendsRequest_Gameplay},
                    {SETTINGS_KEY_SOCIAL_PRESENCE_STATUS, m_Social_PresenceStatus},
                }
        },

        {
			SETTINGS_KEY_PLUGINS,
                {
                    {SETTINGS_KEY_PLUGINS_ANTICHEAT, m_Plugins_Anticheat},
                    {SETTINGS_KEY_PLUGINS_TRANSPORT, m_Plugins_Transport}
                }
        },

        {
            SETTINGS_KEY_MAPS,
                {
                    {SETTINGS_KEY_MAPS_FAVOURITES, m_Maps_Favourites}
                }
        }
    };
	
	// The launcher and other game instances also write settings.client.json, so the save reads the file again and only writes the
	// settings this client changed since its last save.
	const GenOnlineClientFile::ReadResult current = GenOnlineClientFile::Read(m_ClientFilePath, ClientFileKeys());
	if (current.status != GenOnlineClientFile::ReadStatus::Ok)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] settings.client.json is no longer writable (%s), the change lasts for this session", current.reason.c_str());
		m_bClientFileWritable = false;
		return true;
	}

	const nlohmann::json lastWritten = nlohmann::json::parse(m_PreservedJSON);
	nlohmann::json updated = current.document;
	ApplyChangedSettings(lastWritten, root, updated);
	updated[GenOnlineClientFile::SchemaVersionKey] = GenOnlineClientFile::SchemaVersion;

	if (!GenOnlineClientFile::ShouldSave(current.status, current.document, updated))
	{
		m_PreservedJSON = root.dump();
		return true;
	}

	const std::string strData = updated.dump(2);
	if (!GenOnlineClientFile::WriteExisting(m_ClientFilePath, strData))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Failed to save settings.client.json");
		return false;
	}

	m_PreservedJSON = root.dump();
	return true;
}

bool GenOnlineSettings::Network_SaveConnectionSettings(const ServiceConnectionSettings& requested, std::string& error)
{
	error.clear();
	ServiceConnectionSettings settings = requested;
	if ((settings.serverMode != "automatic" && settings.serverMode != "custom") ||
		(settings.proxyMode != "automatic" && settings.proxyMode != "disabled" && settings.proxyMode != "custom"))
	{
		error = "Choose a valid service and proxy mode.";
		return false;
	}
	if (settings.serverMode == "custom")
	{
		if (!ServiceConnection::NormalizeCustomEndpoint(settings.serverAddress, settings.serverAddress, error))
		{
			return false;
		}
	}
	else
	{
		settings.serverAddress.clear();
	}
	if ((settings.proxyMode == "custom" || !settings.proxyURL.empty()) && !ServiceConnection::ValidateProxyURL(settings.proxyURL, error))
	{
		return false;
	}
	if (!m_bInitialized)
	{
		Initialize();
	}
	std::lock_guard<std::mutex> lock(m_NetworkMutex);
	const auto previous = m_ConnectionSettings;
	m_ConnectionSettings = settings;
	if (Save())
	{
		// a changed proxy starts without anything typed for the old one
		const std::string previousProxy = previous.proxyMode == "custom" ? ServiceConnection::CanonicalProxyKey(previous.proxyURL) : std::string();
		const std::string proxy = settings.proxyMode == "custom" ? ServiceConnection::CanonicalProxyKey(settings.proxyURL) : std::string();
		if (previousProxy != proxy)
			ServiceConnection::ClearSessionProxyCredentialsExcept(proxy);
		ServiceConnection::InvalidateProxyPolicyCache();
		return true;
	}
	m_ConnectionSettings = previous;
	error = "Could not save connection settings. Check the permissions of settings.client.json.";
	return false;
}

bool GenOnlineSettings::Network_SaveWindowsLogin(bool windowsLogin)
{
	if (!m_bInitialized)
	{
		Initialize();
	}
	std::lock_guard<std::mutex> lock(m_NetworkMutex);
	if (m_ConnectionSettings.windowsLogin == windowsLogin)
	{
		return true;
	}
	m_ConnectionSettings.windowsLogin = windowsLogin;
	if (!Save())
	{
		m_ConnectionSettings.windowsLogin = !windowsLogin;
		return false;
	}
	ServiceConnection::InvalidateProxyPolicyCache();
	return true;
}

bool GenOnlineSettings::Maps_IsFavourite(const std::string& portableMapPath) const
{
	AsciiString lowered(portableMapPath.c_str());
	lowered.toLower();
	return m_Maps_Favourites.find(std::string(lowered.str())) != m_Maps_Favourites.end();
}

void GenOnlineSettings::Maps_SetFavourite(const std::string& portableMapPath, bool bFavourite)
{
	if (portableMapPath.empty())
	{
		return;
	}

	AsciiString lowered(portableMapPath.c_str());
	lowered.toLower();
	const std::string key(lowered.str());

	const bool bChanged = bFavourite ? m_Maps_Favourites.insert(key).second : (m_Maps_Favourites.erase(key) != 0);
	if (bChanged)
	{
		Save();
	}
}
