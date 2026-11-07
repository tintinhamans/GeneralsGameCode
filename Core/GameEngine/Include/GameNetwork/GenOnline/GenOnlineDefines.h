#pragma once

#if !defined(GENONLINE)
#define GENONLINE
#endif

//#define WORLD_SERIES_CLIENT 1

// Community patch gameplay changes are Zero Hour balance
#if RTS_ZEROHOUR
#define GENONLINE_COMMUNITY_PATCH_CHANGES 1
#endif

#define GENONLINE_USE_PLUGINS_INTERFACE

//#define USE_MAULLER_ONEDRIVE_FIX 1
//#define USE_STUBBJAX_TRANSPORT_CONTAIN_FIX 1

#define GENONLINE_VERSION_STRING "100126_QFE2" // NOTE: Format is critical here for Sentry to work

#define GENONLINE_DISABLE_TEXTURE_FILTERING_AND_AA 1

#define GENONLINE_LOBBY_MAX_PASSWORD_LENGTH 16

#if defined(_DEBUG)
//#define ARTIFICIAL_DELAY_HTTP_REQUESTS 1
#endif

#define GENONLINE_DISABLE_AUTO_ACCEPT 1

#if defined(_DEBUG)
//#define USE_DEBUG_ON_LIVE_SERVER 1
#endif

#if !defined(_DEBUG)
//#define USE_TEST_ENV 1
#endif

#define HTTP_UPLOAD_TIMEOUT 600000

#if RTS_ZEROHOUR
#define GENONLINE_GAMETYPE_ZEROHOUR
// INI CRC of unmodified Zero Hour 1.04 data
#define VANILLA_INI_CRC 4272612339
#else
#define GENONLINE_GAMETYPE_GENERALS
#endif

#if defined(_DEBUG)
#define RTS_MULTI_INSTANCE 1
#endif

class AsciiString;
class UnicodeString;
void showNotificationBox(AsciiString nick, UnicodeString message, bool bPlaySound = true);

#define ALLOW_NON_PROFILED_LOGIN 1

#define GENONLINE_ENABLE_MATCH_START_COUNTDOWN

#define GENONLINE_VERSION 1
#define GENONLINE_NET_VERSION 1
#define GENONLINE_SERVICE_VERSION 1

#if !_DEBUG || defined(USE_DEBUG_ON_LIVE_SERVER)
#define GENONLINE_ENCRYPT_CREDENTIALS 1
#endif

// annoying game assertions, we'll catch real things in the debugger (or sentry)
#define DISABLE_DEBUG_CRASHING 1



#define GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM 310
#define GENONLINE_MIN_LOBBY_CAMERA_ZOOM 210
#define GENONLINE_MAX_LOBBY_CAMERA_ZOOM 1000

// 60 Hz logic only has its gameplay adaptations in Zero Hour; Generals keeps retail 30 Hz
#if RTS_ZEROHOUR
#define GENONLINE_HIGH_FPS_SERVER 1
#endif

#if defined(GENONLINE_HIGH_FPS_SERVER)
#define GENONLINE_CLIENT_ID "gen_online_60hz"
#else
#define GENONLINE_CLIENT_ID "gen_online_30hz"
#endif

#if defined(GENONLINE_HIGH_FPS_SERVER)
	#define GENONLINE_HIGH_FPS_LIMIT 60
	#define GENONLINE_HIGH_FPS_FRAME_MULTIPLIER (GENONLINE_HIGH_FPS_LIMIT/30)
	#define GENONLINE_HIGH_FPS_RENDER 1 // This must be defined for high fps server
#else
	#define GENONLINE_HIGH_FPS_LIMIT 30
	#define GENONLINE_HIGH_FPS_FRAME_MULTIPLIER 1

	#define GENONLINE_HIGH_FPS_RENDER 1 // This is optional on 30fps, but will boost/unlock the framerate, similar to gentool
#endif

#if defined(GENONLINE_HIGH_FPS_SERVER)
static int FRAME_GROUPING_CAP = 32;
#else
static int FRAME_GROUPING_CAP = 64;
#endif


#define GENONLINE_USE_LARGER_DMAPOOL 1

#if !_DEBUG
#define GENONLINE_USE_SENTRY 1
#endif

#define GENONLINE_WIDESCREEN 1

#if defined(GENONLINE_WIDESCREEN)
#define DEFAULT_DISPLAY_WIDTH      800
#define DEFAULT_DISPLAY_HEIGHT     600

#define GENONLINE_WIDESCREEN_X_SCALE 1280.f
#define GENONLINE_WIDESCREEN_Y_SCALE 720.f

#define DEFAULT_DISPLAY_WIDTH_SPLASH      800
#define DEFAULT_DISPLAY_HEIGHT_SPLASH     600
#endif

#define GENONLINE_IBRA_STARTING_POS_LOGIC 1



#define GENONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES 1

#define GENONLINE_DISABLE_STD_FROM_CHARS_PARSING 1
