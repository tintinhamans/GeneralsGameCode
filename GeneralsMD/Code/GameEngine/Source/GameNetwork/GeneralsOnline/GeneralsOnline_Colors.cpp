#include "PreRTS.h"

#include "GameNetwork/GeneralsOnline/GeneralsOnline_Colors.h"

// Palette is intentionally kept to soft/muted tones (no bare neon primaries) so every
// NGMP/GeneralsOnline screen reads consistently against the shell's dark background.
// Related roles share a hue family: gold = highlighted/important, blue = owner/admin,
// pink = buddy, gray = muted/disabled, so the same concept always looks the same everywhere.
Color GeneralsOnlineColor[GOCOLOR_MAX] =
{
	GameMakeColor(214, 221, 230, 255),	// GOCOLOR_SYSTEM
	GameMakeColor(250, 188,  95, 255),	// GOCOLOR_WARNING
	GameMakeColor(255, 130, 130, 255),	// GOCOLOR_ERROR
	GameMakeColor(137, 219, 151, 255),	// GOCOLOR_SUCCESS
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_DEFAULT
	GameMakeColor(255, 226,  84, 255),	// GOCOLOR_CURRENTROOM
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_ROOM
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_PLAYER_NORMAL
	GameMakeColor(130, 200, 255, 255),	// GOCOLOR_PLAYER_OWNER
	GameMakeColor(255, 154, 205, 255),	// GOCOLOR_PLAYER_BUDDY
	GameMakeColor(255, 226,  84, 255),	// GOCOLOR_PLAYER_SELF
	GameMakeColor(150, 150, 150, 255),	// GOCOLOR_PLAYER_IGNORED
	GameMakeColor(100, 130, 150, 255),	// GOCOLOR_PLAYER_OFFLINE
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_CHAT_NORMAL
	GameMakeColor(130, 200, 255, 255),	// GOCOLOR_CHAT_OWNER
	GameMakeColor(200, 190, 230, 255),	// GOCOLOR_CHAT_EMOTE
	GameMakeColor(174, 211, 247, 255),	// GOCOLOR_CHAT_OWNER_EMOTE
	GameMakeColor(140, 190, 255, 255),	// GOCOLOR_CHAT_PRIVATE
	GameMakeColor(170, 210, 255, 255),	// GOCOLOR_CHAT_PRIVATE_EMOTE
	GameMakeColor(200, 170, 255, 255),	// GOCOLOR_CHAT_PRIVATE_OWNER
	GameMakeColor(215, 190, 255, 255),	// GOCOLOR_CHAT_PRIVATE_OWNER_EMOTE
	GameMakeColor(130, 200, 255, 255),	// GOCOLOR_ADMIN
	GameMakeColor(180, 180, 180, 255),	// GOCOLOR_NAME_CHANGE
	GameMakeColor(255, 100, 100, 255),	// GOCOLOR_BLOCKED
	GameMakeColor(255, 226,  84, 255),	// GOCOLOR_MAP_SELECTED
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_MAP_UNSELECTED
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_MOTD
	GameMakeColor(255, 226,  84, 255),	// GOCOLOR_MOTD_HEADING
	GameMakeColor(255, 255, 255, 255),	// GOCOLOR_LOBBY_NORMAL
	GameMakeColor(191, 198, 205, 255),	// GOCOLOR_LOBBY_ALTERNATE
	GameMakeColor(150, 150, 150, 255),	// GOCOLOR_LOBBY_CRC_MISMATCH
	GameMakeColor(205, 133, 168, 255),	// GOCOLOR_LOBBY_BUDDY_LOCKED
};
