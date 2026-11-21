# Generals Online part of Core/GameEngine/CMakeLists.txt, included after its source list.

list(REMOVE_ITEM GAMEENGINE_SRC
    Include/GameNetwork/GameSpy/PeerThread.h
    Source/GameNetwork/GameSpy/GSConfig.cpp
    Source/GameNetwork/GameSpy/LadderDefs.cpp
    Source/GameNetwork/GameSpy/PeerDefs.cpp
    Source/GameNetwork/GameSpy/Thread/BuddyThread.cpp
    Source/GameNetwork/GameSpy/Thread/GameResultsThread.cpp
    Source/GameNetwork/GameSpy/Thread/PeerThread.cpp
    Source/GameNetwork/GameSpy/Thread/PersistentStorageThread.cpp
)

list(APPEND GAMEENGINE_SRC
    Include/Common/AdaptiveQuality.h
    Include/Common/AdaptiveQualityModel.h
    Include/Common/AdaptiveQualityPower.h
    Include/Common/GOUIColors.h
    Include/GameClient/GenOnlineMenus.h
    Include/GameClient/GUI/ChatCompletion.h
    Include/GameClient/GUI/ChatCompletionSets.h
    Include/GameClient/GUI/ChatMentions.h
    Include/GameClient/MapCacheJson.h
    Include/GameClient/MapPreviewIcons.h
    Include/GameClient/MapSelectList.h
    Include/GameNetwork/ConnectionStats.h
    Include/GameNetwork/Online/GameSpy/GameSpyMatchmaking.h
    Include/GameNetwork/Online/GameSpy/GameSpyGameHooks.h
    Include/GameNetwork/Online/GameSpy/GameSpyProviderFactory.h
    Include/GameNetwork/Online/GameSpy/GameSpySession.h
    Include/GameNetwork/Online/IOnlineChat.h
    Include/GameNetwork/Online/IOnlineConfig.h
    Include/GameNetwork/Online/IOnlineGameHooks.h
    Include/GameNetwork/Online/IOnlineLadder.h
    Include/GameNetwork/Online/IOnlineLobbyState.h
    Include/GameNetwork/Online/IOnlineMatchmaking.h
    Include/GameNetwork/Online/IOnlinePresence.h
    Include/GameNetwork/Online/IOnlineResults.h
    Include/GameNetwork/Online/IOnlineSession.h
    Include/GameNetwork/Online/IOnlineStats.h
    Include/GameNetwork/Online/OnlineProviderFactory.h
    Include/GameNetwork/Online/OnlineProviderType.h
    Include/GameNetwork/Online/OnlineRpcTypes.h
    Include/GameNetwork/TransportFactory.h
    Include/GameNetwork/UDPTransport.h
    Source/Common/AdaptiveQuality.cpp
    Source/Common/GOUIColors.cpp
    Source/GameClient/GUI/ChatCompletion.cpp
    Source/GameClient/GUI/ChatCompletionSets.cpp
    Source/GameClient/GUI/ChatMentions.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineBuddyOverlay.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineDownloadMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineGameSetupMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineLobbyMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineLoginMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineMapSelectMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineOptionsMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlinePlayerInfoOverlay.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlinePopupHostGame.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlinePopupJoinGame.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineQuickMatchMenu.cpp
    Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineWelcomeMenu.cpp
    Source/GameClient/GUI/MapSelectList.cpp
    Source/GameClient/MapCacheJson.cpp
    Source/GameClient/MapPreviewIcons.cpp
    Source/GameNetwork/ConnectionStats.cpp
    Source/GameNetwork/Online/GameSpy/BuddyThread.cpp
    Source/GameNetwork/Online/GameSpy/GSConfig.cpp
    Source/GameNetwork/Online/GameSpy/GameResultsThread.cpp
    Source/GameNetwork/Online/GameSpy/GameSpyGameHooks.cpp
    Source/GameNetwork/Online/GameSpy/GameSpyMatchmaking.cpp
    Source/GameNetwork/Online/GameSpy/GameSpyProviderFactory.cpp
    Source/GameNetwork/Online/GameSpy/GameSpySession.cpp
    Source/GameNetwork/Online/GameSpy/LadderDefs.cpp
    Source/GameNetwork/Online/GameSpy/PeerDefs.cpp
    Source/GameNetwork/Online/GameSpy/PeerThread.cpp
    Source/GameNetwork/Online/GameSpy/PersistentStorageThread.cpp
    Source/GameNetwork/Online/OnlineProviderFactory.cpp
    Include/GameNetwork/PeerTransport/PeerTransport.h
    Include/GameNetwork/PeerTransport/GnsNetworkConfig.h
    Include/GameNetwork/PeerTransport/GnsPeerNetwork.h
    Include/GameNetwork/PeerTransport/GnsPeerTransport.h
    Include/GameNetwork/PeerTransport/PeerRouteLabel.h
    Include/GameNetwork/PeerTransport/PeerSignalChannel.h
    Include/GameNetwork/PeerTransport/PluginPeerTransport.h
    Source/GameNetwork/PeerTransport/GnsPeerNetwork.cpp
    Source/GameNetwork/PeerTransport/GnsPeerTransport.cpp
    Source/GameNetwork/PeerTransport/PeerRouteLabel.cpp
    Source/GameNetwork/PeerTransport/PeerSignalChannel.cpp
    Source/GameNetwork/PeerTransport/PeerTransportLog.cpp
    Source/GameNetwork/PeerTransport/PluginPeerTransport.cpp
    Source/GameNetwork/TransportFactory.cpp
    Source/GameNetwork/UDPTransport.cpp
    Include/GameNetwork/GenOnline/json.hpp
    Include/GameNetwork/GenOnline/DiscordRichPresence.h
    Include/GameNetwork/GenOnline/Vendor/DiscordRPC/discord_rpc.h
    Include/GameNetwork/GenOnline/NetworkBitstream.h
    Include/GameNetwork/GenOnline/ConnectionRating.h
    Include/GameNetwork/GenOnline/MatchKeys.h
    Include/GameNetwork/GenOnline/NetworkMesh.h
    Include/GameNetwork/GenOnline/NetBytes.h
    Include/GameNetwork/GenOnline/PacketFeedback.h
    Include/GameNetwork/GenOnline/NetworkPacket.h
    Include/GameNetwork/GenOnline/GenOnlineDefines.h
    Include/GameNetwork/GenOnline/GenOnlineGame.h
    Include/GameNetwork/GenOnline/GenOnlineInclude.h
    Include/GameNetwork/GenOnline/GenOnlineInterfaces.h
    Include/GameNetwork/GenOnline/GenOnlineTypes.h
    Include/GameNetwork/GenOnline/GenOnlineAuth.h
    Include/GameNetwork/GenOnline/GenOnlineInit.h
    Include/GameNetwork/GenOnline/GenOnlineLobbyInterface.h
    Include/GameNetwork/GenOnline/GenOnlineMainMenuUtils.h
    Include/GameNetwork/GenOnline/GenOnlineModeration.h
    Include/GameNetwork/GenOnline/GenOnlineRoomsInterface.h
    Include/GameNetwork/GenOnline/GenOnlineStatsInterface.h
    Include/GameNetwork/GenOnline/GenOnlineSocialInterface.h
    Include/GameNetwork/GenOnline/GenOnlineMatchmakingInterface.h
    Include/GameNetwork/GenOnline/GenOnlineClientDescription.h
    Include/GameNetwork/GenOnline/GenOnlineClientFile.h
    Include/GameNetwork/GenOnline/GenOnlineCrashReport.h
    Include/GameNetwork/GenOnline/GenOnlinePackList.h
    Include/GameNetwork/GenOnline/GenOnlinePluginRoles.h
    Include/GameNetwork/GenOnline/GenOnlineSettings.h
    Include/GameNetwork/GenOnline/HTTP/HTTPManager.h
    Include/GameNetwork/GenOnline/HTTP/HTTPRequest.h
    Include/GameNetwork/GenOnline/GenOnlineTransport.h
    Include/GameNetwork/GenOnline/AntiCheat/GenOnlineAntiCheat.h
    Include/GameNetwork/GenOnline/Provider/GenOnlineGameHooks.h
    Include/GameNetwork/GenOnline/Provider/GenOnlineNetworkOverlay.h
    Include/GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h
    Include/GameNetwork/GenOnline/Provider/GenOnlinePluginKeys.h
    Include/GameNetwork/GenOnline/Provider/GenOnlinePluginPlatform.h
    Include/GameNetwork/GenOnline/Provider/GenOnlinePluginTrust.h
    Include/GameNetwork/GenOnline/Provider/GenOnlineProviderFactory.h
    Include/GameNetwork/GenOnline/Provider/GenOnlineResults.h
    Include/GameNetwork/GenOnline/Realtime/RealtimeHubSession.h
    Include/GameNetwork/GenOnline/Realtime/RealtimeMessages.h
    Include/GameNetwork/GenOnline/Realtime/RealtimeProtocol.h
    Include/GameNetwork/GenOnline/Realtime/RealtimeSignIn.h
    Include/GameNetwork/GenOnline/GenOnlineSignIn.h
    Include/GameNetwork/GenOnline/GenOnlineRealtimeBridge.h
    Include/GameNetwork/GenOnline/WifiLowLatency.h
    Source/GameNetwork/GenOnline/CredentialStore.cpp
    Source/GameNetwork/GenOnline/DiscordRichPresence.cpp
    Source/GameNetwork/GenOnline/GenOnlineHelpers.cpp
    Source/GameNetwork/GenOnline/GenOnlineGame.cpp
    Source/GameNetwork/GenOnline/ProxyLogin.cpp
    Source/GameNetwork/GenOnline/ServiceConnectionPolicy.cpp
    Source/GameNetwork/GenOnline/ServiceEndpointPolicy.cpp
    Source/GameNetwork/GenOnline/GenOnlineClientDescription.cpp
    Source/GameNetwork/GenOnline/GenOnlineClientFile.cpp
    Source/GameNetwork/GenOnline/GenOnlinePackList.cpp
    Source/GameNetwork/GenOnline/GenOnlinePluginRoles.cpp
    Source/GameNetwork/GenOnline/GenOnlineSettings.cpp
    Source/GameNetwork/GenOnline/HTTP/HTTPManager.cpp
    Source/GameNetwork/GenOnline/HTTP/HTTPRequest.cpp
    Source/GameNetwork/GenOnline/ConnectionRating.cpp
    Source/GameNetwork/GenOnline/MatchKeys.cpp
    Source/GameNetwork/GenOnline/NetworkMesh.cpp
    Source/GameNetwork/GenOnline/PacketFeedback.cpp
    Source/GameNetwork/GenOnline/GenOnlineInit.cpp
    Source/GameNetwork/GenOnline/GenOnlineAuth.cpp
    Source/GameNetwork/GenOnline/GenOnlineSignIn.cpp
    Source/GameNetwork/GenOnline/GenOnlineRealtimeBridge.cpp
    Source/GameNetwork/GenOnline/GenOnlineLobbyInterface.cpp
    Source/GameNetwork/GenOnline/GenOnlineMainMenuUtils.cpp
    Source/GameNetwork/GenOnline/GenOnlineModeration.cpp
    Source/GameNetwork/GenOnline/GenOnlineRoomsInterface.cpp
    Source/GameNetwork/GenOnline/GenOnlineStatsInterface.cpp
    Source/GameNetwork/GenOnline/GenOnlineSocialInterface.cpp
    Source/GameNetwork/GenOnline/GenOnlineMatchmakingInterface.cpp
    Source/GameNetwork/GenOnline/GenOnlineTransport.cpp
    Source/GameNetwork/GenOnline/NetworkBitstream.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlineGameHooks.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlineNetworkOverlay.cpp
    Source/GameNetwork/GenOnline/AntiCheat/GenOnlineAntiCheat.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlinePluginHost.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlinePluginPlatformWin32.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlinePluginTrust.cpp
    Source/GameNetwork/GenOnline/Provider/GenOnlineProviderFactory.cpp
    Source/GameNetwork/GenOnline/WifiLowLatency.cpp
)

macro(genonline_core_gameengine_targets)
    target_include_directories(corei_gameengine_public INTERFACE
        Include/GameNetwork/GenOnline/Vendor/
    )

    # Generals Online networking dependencies, provided by vcpkg (see vcpkg.json).
    if(NOT IS_VS6_BUILD)
        find_package(CURL CONFIG REQUIRED)
        find_package(GameNetworkingSockets CONFIG REQUIRED)
        find_package(sentry CONFIG REQUIRED)
        find_package(genonline-plugin-sdk CONFIG REQUIRED)
        include(${CMAKE_SOURCE_DIR}/GenOnline/cmake/monocypher.cmake)

        target_link_libraries(corei_gameengine_public INTERFACE
            CURL::libcurl
            GameNetworkingSockets::GameNetworkingSockets_s
            sentry::sentry
            GenOnline::PluginApi
            bcrypt
            monocypher
        )

        # Lets a build without plugin signing keys load unsigned GenOnline plugins with a warning. A build with a key
        # refuses unsigned plugins anyway. The option is off by default and no preset and no CI workflow turns it on.
        option(GENONLINE_ALLOW_UNSIGNED_PLUGINS "Load unsigned GenOnline plugins in builds without plugin signing keys" OFF)
        if(GENONLINE_ALLOW_UNSIGNED_PLUGINS)
            target_compile_definitions(corei_gameengine_public INTERFACE GENONLINE_ALLOW_UNSIGNED_PLUGINS)
        endif()

        # Release builds send crash reports to the GlitchTip project genonline-client by default. Debug builds never send them.
        # An empty DSN turns crash reports off. The key in the DSN is public by design.
        set(GENONLINE_SENTRY_DSN "https://54558e0eda1648a3994a73de9118c2e2@errors.generals.online/1" CACHE STRING "Crash report DSN of release builds, empty turns crash reports off")
        if(GENONLINE_SENTRY_DSN MATCHES "[\"\\]")
            message(FATAL_ERROR "GENONLINE_SENTRY_DSN must not contain quotes or backslashes")
        endif()
        set(GENONLINE_CRASH_REPORT_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/GenOnlineCrashReportConfig")
        file(CONFIGURE OUTPUT "${GENONLINE_CRASH_REPORT_CONFIG_DIR}/GenOnlineCrashReportConfig.h"
            CONTENT "#pragma once\n#define GENONLINE_SENTRY_DSN \"@GENONLINE_SENTRY_DSN@\"\n"
            @ONLY)
        target_include_directories(corei_gameengine_public INTERFACE "${GENONLINE_CRASH_REPORT_CONFIG_DIR}")
    endif()
endmacro()

# The upstream file creates the targets after this point, so this part runs once it is done.
cmake_language(DEFER CALL genonline_core_gameengine_targets)
