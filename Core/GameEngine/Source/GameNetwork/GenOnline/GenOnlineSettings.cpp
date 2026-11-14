#include "GameNetwork/GenOnline/GenOnlineSettings.h"
#include "Common/OptionPreferences.h"
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
#define SETTINGS_KEY_CAMERA_LEGACY_MOVE_SPEED_RATIO "move_speed_ratio"
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
#define SETTINGS_KEY_RENDER_LEGACY_DRAW_STATS_OVERLAY "stats_overlay"
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

#define SETTINGS_KEY_DEBUG "debug"
#define SETTINGS_KEY_DEBUG_VERBOSE_LOGGING "verbose_logging"

#define SETTINGS_KEY_NETWORK "network"
#define SETTINGS_KEY_NETWORK_LEGACY_HTTP_VERSION "http_version"
#define SETTINGS_KEY_NETWORK_USE_ALTERNATIVE_ENDPOINT "use_alternative_endpoint"
#define SETTINGS_KEY_NETWORK_SERVER "server"
#define SETTINGS_KEY_NETWORK_SERVER_MODE "mode"
#define SETTINGS_KEY_NETWORK_SERVER_ADDRESS "address"
#define SETTINGS_KEY_NETWORK_SERVER_LEGACY_REALTIME_PROTOCOL "realtime_protocol"
#define SETTINGS_KEY_NETWORK_PROXY "proxy"
#define SETTINGS_KEY_NETWORK_PROXY_MODE "mode"
#define SETTINGS_KEY_NETWORK_PROXY_ADDRESS "address"
#define SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN "windows_login"
#define SETTINGS_KEY_NETWORK_LAST_ROUTE "last_route"
#define LEGACY_ALTERNATIVE_ROUTE_HOST "api-ru.playgenerals.online"

#define SETTINGS_KEY_PLUGINS "plugins"
#define SETTINGS_KEY_PLUGINS_ANTICHEAT "anticheat"
#define SETTINGS_KEY_PLUGINS_TRANSPORT "transport"
#define SETTINGS_KEY_PLUGINS_SETTINGS "settings"

#define SETTINGS_KEY_MAPS "maps"
#define SETTINGS_KEY_MAPS_FAVOURITES "favourites"

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

// The folder of the game executable with a trailing separator, empty when it cannot be resolved.
static std::wstring ExecutableDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::wstring();
        }

        if (length < buffer.size())
        {
            const std::wstring path(buffer.data(), length);
            const size_t separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator + 1);
        }

        buffer.resize(buffer.size() * 2);
    }
}

// TRUE when plugins/<name>/<name>.dll exists next to the game executable.
static bool IsPluginInstalled(const char* szPlugin)
{
    const std::wstring exeDirectory = ExecutableDirectory();
    if (exeDirectory.empty())
    {
        return false;
    }
    const std::string name = szPlugin;
    const std::wstring wideName(name.begin(), name.end());
    std::error_code ec;
    return std::filesystem::exists(exeDirectory + L"plugins\\" + wideName + L"\\" + wideName + L".dll", ec);
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

    // Easy Anti-Cheat through the EOS plugin always runs EOS P2P too.
    const bool bEosAnticheat = std::find(m_Plugins_Anticheat.begin(), m_Plugins_Anticheat.end(), GENONLINE_EOS_PLUGIN_NAME) != m_Plugins_Anticheat.end();
    if (bEosAnticheat && std::find(transports.begin(), transports.end(), GENONLINE_EOS_PLUGIN_NAME) == transports.end())
    {
        transports.push_back(GENONLINE_EOS_PLUGIN_NAME);
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

// The old right mouse scroll speed was ratio * Options.ini scroll speed * (screen width / 2) pixels. The scroll now measures the drag at 800x600
// and uses the 1.6 horizontal speed factor of GameData.ini, so the same speed needs a scroll speed of
// old scroll speed * ratio * (width / 2) / (1.6 * 400), within the range of the options slider.
static void MigrateMoveSpeedRatioToOptions(float ratio)
{
	OptionPreferences options;
	Int width = 0;
	Int height = 0;
	options.getResolution(&width, &height);
	const auto scrollFactor = options.find("ScrollFactor");
	const Int oldSlider = scrollFactor == options.end() ? 50 : (std::max)(1, atoi(scrollFactor->second.str()));
	const Int newSlider = (std::max)(1, static_cast<Int>(oldSlider * ratio * (width / 2.0f) / (1.6f * 400.0f) + 0.5f));
	AsciiString text;
	text.format("%d", newSlider);
	options["ScrollFactor"] = text;
	options.write();
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Moved camera.move_speed_ratio=%.2f to Options.ini ScrollFactor: %d -> %d at %dx%d", ratio, oldSlider, newSlider, width, height);
}

// The keys client.json holds and the type each one must have. A key of another type makes the whole file unusable.
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
		{ SETTINGS_KEY_DEBUG, ValueType::Object },
		{ SETTINGS_KEY_DEBUG "." SETTINGS_KEY_DEBUG_VERBOSE_LOGGING, ValueType::Boolean },
		{ SETTINGS_KEY_NETWORK, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER "." SETTINGS_KEY_NETWORK_SERVER_MODE, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_SERVER "." SETTINGS_KEY_NETWORK_SERVER_ADDRESS, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY, ValueType::Object },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_MODE, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_ADDRESS, ValueType::String },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_PROXY "." SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN, ValueType::Boolean },
		{ SETTINGS_KEY_NETWORK "." SETTINGS_KEY_NETWORK_LAST_ROUTE, ValueType::String },
		{ SETTINGS_KEY_PLUGINS, ValueType::Object },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_ANTICHEAT, ValueType::Array },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_TRANSPORT, ValueType::Array },
		{ SETTINGS_KEY_PLUGINS "." SETTINGS_KEY_PLUGINS_SETTINGS, ValueType::Object },
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

	// A missing client.json is created with the built-in defaults, which the first save below writes into it.
	if (clientFile.status == GenOnlineClientFile::ReadStatus::Missing)
	{
		nlohmann::json created = nlohmann::json::object();
		created[GenOnlineClientFile::SchemaVersionKey] = GenOnlineClientFile::SchemaVersion;
		if (GenOnlineClientFile::CreateNew(m_ClientFilePath, created.dump(2)))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Created client.json with the built-in defaults");
			clientFile.status = GenOnlineClientFile::ReadStatus::Ok;
			clientFile.document = created;
			clientFile.schemaVersion = GenOnlineClientFile::SchemaVersion;
		}
	}

	// The client never writes a client.json it cannot use, the settings then only last for this session.
	switch (clientFile.status)
	{
	case GenOnlineClientFile::ReadStatus::Missing:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] client.json cannot be created, using the built-in defaults without saving");
		return;
	case GenOnlineClientFile::ReadStatus::Unusable:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] client.json is not usable (%s), using the defaults this session and leaving the file untouched", clientFile.reason.c_str());
		return;
	case GenOnlineClientFile::ReadStatus::Newer:
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] client.json has schema_version %d, newer than %d, reading it without saving", clientFile.schemaVersion, GenOnlineClientFile::SchemaVersion);
		break;
	case GenOnlineClientFile::ReadStatus::Ok:
		break;
	}

	const bool bWritable = clientFile.status == GenOnlineClientFile::ReadStatus::Ok;
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

				// the right mouse scroll speed is the Options.ini scroll speed now, an old ratio is converted once
				if (bWritable && cameraSettings.contains(SETTINGS_KEY_CAMERA_LEGACY_MOVE_SPEED_RATIO) && cameraSettings[SETTINGS_KEY_CAMERA_LEGACY_MOVE_SPEED_RATIO].is_number())
				{
					MigrateMoveSpeedRatioToOptions(std::clamp<float>(static_cast<float>(cameraSettings[SETTINGS_KEY_CAMERA_LEGACY_MOVE_SPEED_RATIO]), 0.05f, 25.f));
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

                    if (networkSettings.contains(SETTINGS_KEY_NETWORK_LAST_ROUTE))
                    {
                        const auto& found = networkSettings[SETTINGS_KEY_NETWORK_LAST_ROUTE];
                        if (found.is_string() && found.get<std::string>().size() <= 253 &&
                            found.get<std::string>().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == std::string::npos)
                        {
                            m_LastRoute = found.get<std::string>();
                        }
                        else
                        {
                            resetField(std::string(SETTINGS_KEY_NETWORK) + "." + SETTINGS_KEY_NETWORK_LAST_ROUTE, found, false);
                        }
                    }
                    else if (networkSettings.contains(SETTINGS_KEY_NETWORK_USE_ALTERNATIVE_ENDPOINT) &&
                        networkSettings[SETTINGS_KEY_NETWORK_USE_ALTERNATIVE_ENDPOINT].is_boolean() &&
                        networkSettings[SETTINGS_KEY_NETWORK_USE_ALTERNATIVE_ENDPOINT].get<bool>())
                    {
                        // the old switch of the released client now only prefers that route
                        m_LastRoute = LEGACY_ALTERNATIVE_ROUTE_HOST;
                    }
                }
            }

			if (jsonSettings.contains(SETTINGS_KEY_DEBUG))
			{
				auto debugSettings = jsonSettings[SETTINGS_KEY_DEBUG];

				if (debugSettings.contains(SETTINGS_KEY_DEBUG_VERBOSE_LOGGING))
				{
					m_bVerbose = debugSettings[SETTINGS_KEY_DEBUG_VERBOSE_LOGGING];
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
            }

			// Without plugins.transport the EOS plugin carries EOS P2P when it is installed.
			bool bTransportSet = false;
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
					bTransportSet = true;
				}
			}
			if (!bTransportSet)
			{
				m_Plugins_Transport.clear();
				if (IsPluginInstalled(GENONLINE_EOS_PLUGIN_NAME))
				{
					m_Plugins_Transport.push_back(GENONLINE_EOS_PLUGIN_NAME);
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
		m_bVerbose = false;
		m_Render_LimitFramerate = true;
		m_Render_FramerateLimit_FPSVal = 60;
		m_Render_AdaptiveQuality = true;
		m_Chat_LifeSeconds = 30;

        m_Social_Notification_FriendComesOnline_Menus = true;
        m_Social_Notification_FriendComesOnline_Gameplay = true;
        m_Social_Notification_FriendGoesOffline_Menus = true;
        m_Social_Notification_FriendGoesOffline_Gameplay = true;

		m_Camera_RotateFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Camera_PitchFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Camera_ZoomFactor = GenOnlineClientOptions::CameraFactorDefault;
		m_Media_Legacy = GenOnlineClientOptions::LegacyMediaDefault;
		m_Observer_StatsFontSize = GenOnlineClientOptions::ObserverStatsFontSizeDefault;
		m_Observer_NotificationFontSize = GenOnlineClientOptions::ObserverNotificationFontSizeDefault;
		m_Observer_NotificationSpecialPowerUsage = GenOnlineClientOptions::ObserverNotificationDefault;
		m_Observer_NotificationSpecialPowerPurchase = GenOnlineClientOptions::ObserverNotificationDefault;
		m_Observer_NotificationMilestone = GenOnlineClientOptions::ObserverNotificationDefault;

		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] client.json is not usable (a value cannot be read), using the defaults this session and leaving the file untouched");
		return;
	}

	// The first save writes every setting, which fills the keys the file does not have yet.
	m_PreservedJSON = "{}";
	m_bClientFileWritable = bWritable;

	// The save writes keys that are new or missing and skips a file that already holds every value.
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

static void EraseSettingKey(nlohmann::json& document, const char* section, const char* key)
{
	if (document.contains(section) && document[section].is_object())
	{
		document[section].erase(key);
	}
}

void GenOnlineSettings::LoadPackList()
{
	m_bPackList = false;
	m_PackStack.clear();
	m_DataPacks_UserExcludePaths.clear();
	if (!CommandLine::hasPackList())
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack list: no -packs, using the built-in packs");
		return;
	}

#if RTS_GENERALS
	const char* game = "generals";
#else
	const char* game = "generalsmd";
#endif
	const GenOnlinePackList::Result result = GenOnlinePackList::ReadFile(CommandLine::getPackListPath(), game);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack list: %s", CommandLine::getPackListPath());
	for (const std::string& line : result.log)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "%s", line.c_str());
	}
	if (result.status != GenOnlinePackList::Status::Ok)
	{
		return;
	}

	m_bPackList = true;
	m_PackStack = result.stack;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "Pack list: %d packs above retail", static_cast<int>(m_PackStack.size()));
}

bool GenOnlineSettings::Save()
{
	if (!m_bInitialized)
	{
		Initialize();
	}

	// Without a client.json this client may write, a change only lasts for this session.
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
                        {SETTINGS_KEY_NETWORK_PROXY_WINDOWS_LOGIN, m_ConnectionSettings.windowsLogin}}},
                    {SETTINGS_KEY_NETWORK_LAST_ROUTE, m_LastRoute}
                }
        },

		{
			SETTINGS_KEY_CHAT,
				{
					{SETTINGS_KEY_CHAT_LIFE_SECONDS, m_Chat_LifeSeconds}
				}
		},

		{
			SETTINGS_KEY_DEBUG,
				{
					{SETTINGS_KEY_DEBUG_VERBOSE_LOGGING, m_bVerbose},
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
	
	// The launcher and other game instances also write client.json, so the save reads the file again and only writes the
	// settings this client changed since its last save.
	const GenOnlineClientFile::ReadResult current = GenOnlineClientFile::Read(m_ClientFilePath, ClientFileKeys());
	if (current.status != GenOnlineClientFile::ReadStatus::Ok)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] client.json is no longer writable (%s), the change lasts for this session", current.reason.c_str());
		m_bClientFileWritable = false;
		return true;
	}

	const nlohmann::json lastWritten = nlohmann::json::parse(m_PreservedJSON);
	nlohmann::json updated = current.document;
	ApplyChangedSettings(lastWritten, root, updated);
	EraseSettingKey(updated, SETTINGS_KEY_RENDER, SETTINGS_KEY_RENDER_LEGACY_DRAW_STATS_OVERLAY);
	EraseSettingKey(updated, SETTINGS_KEY_CAMERA, SETTINGS_KEY_CAMERA_LEGACY_MOVE_SPEED_RATIO);
	EraseSettingKey(updated, SETTINGS_KEY_NETWORK, SETTINGS_KEY_NETWORK_USE_ALTERNATIVE_ENDPOINT);
	EraseSettingKey(updated, SETTINGS_KEY_NETWORK, SETTINGS_KEY_NETWORK_LEGACY_HTTP_VERSION);
	if (updated.contains(SETTINGS_KEY_NETWORK) && updated[SETTINGS_KEY_NETWORK].is_object())
	{
		EraseSettingKey(updated[SETTINGS_KEY_NETWORK], SETTINGS_KEY_NETWORK_SERVER, SETTINGS_KEY_NETWORK_SERVER_LEGACY_REALTIME_PROTOCOL);
	}
	updated[GenOnlineClientFile::SchemaVersionKey] = GenOnlineClientFile::SchemaVersion;

	if (!GenOnlineClientFile::ShouldSave(current.status, current.document, updated))
	{
		m_PreservedJSON = root.dump();
		return true;
	}

	const std::string strData = updated.dump(2);
	if (!GenOnlineClientFile::WriteExisting(m_ClientFilePath, strData))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Settings] Failed to save client.json");
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
	error = "Could not save connection settings. Check the permissions of client.json.";
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

bool GenOnlineSettings::Network_SaveLastRoute(const std::string& host)
{
	if (host.empty() || host.size() > 253 || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos)
	{
		return false;
	}
	if (!m_bInitialized)
	{
		Initialize();
	}
	std::lock_guard<std::mutex> lock(m_NetworkMutex);
	if (m_LastRoute == host)
	{
		return true;
	}
	const auto previous = m_LastRoute;
	m_LastRoute = host;
	if (Save())
	{
		return true;
	}
	m_LastRoute = previous;
	return false;
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
