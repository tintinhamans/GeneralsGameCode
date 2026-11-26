#pragma once

#if !defined(GENONLINE)
#define GENONLINE
#endif

//#define WORLD_SERIES_CLIENT 1

//#define USE_MAULLER_ONEDRIVE_FIX 1
//#define USE_STUBBJAX_TRANSPORT_CONTAIN_FIX 1

// Ronin @feature 20/11/2026 DX9: off - the DX9 renderer filters and anti-aliases itself (TAA, MSAA); GO's dxwrapper
// build turned the game's settings off.
//#define GENONLINE_DISABLE_TEXTURE_FILTERING_AND_AA 1

#define GENONLINE_LOBBY_MAX_PASSWORD_LENGTH 16

#if defined(_DEBUG)
//#define ARTIFICIAL_DELAY_HTTP_REQUESTS 1
#endif

// The GENONLINE_ENVIRONMENT CMake option sets the services of a build when settings.client.json names none. Without it the release tag decides.
#if defined(GENONLINE_BUILD_ENVIRONMENT_LIVE)
#define GENONLINE_BUILD_ENVIRONMENT "live"
#elif defined(GENONLINE_BUILD_ENVIRONMENT_TEST)
#define GENONLINE_BUILD_ENVIRONMENT "test"
#elif defined(GENONLINE_BUILD_ENVIRONMENT_DEV)
#define GENONLINE_BUILD_ENVIRONMENT "dev"
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
void GenOnlineShowNotificationBox(AsciiString nick, UnicodeString message, bool bPlaySound = true);
// A notification that opens the communicator on the conversation with the user when it is clicked.
void GenOnlineShowCommunicatorNotice(long long peerUserID, UnicodeString message);

#define ALLOW_NON_PROFILED_LOGIN 1

#if !_DEBUG || defined(GENONLINE_BUILD_ENVIRONMENT)
#define GENONLINE_ENCRYPT_CREDENTIALS 1
#endif

// annoying game assertions, we'll catch real things in the debugger (or sentry)
#define DISABLE_DEBUG_CRASHING 1



#define GENONLINE_DEFAULT_LOBBY_CAMERA_ZOOM 310
#define GENONLINE_MIN_LOBBY_CAMERA_ZOOM 210
#define GENONLINE_MAX_LOBBY_CAMERA_ZOOM 1000

// The OAuth client id names the game tree.
#if RTS_GENERALS
#define GENONLINE_CLIENT_ID "genonline_generals"
#else
#define GENONLINE_CLIENT_ID "genonline_zerohour"
#endif

extern int FRAME_GROUPING_CAP;


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
