#pragma once

#include <cstdint>

// Opens the dock tab of the private conversation with a player, and pops its window up when asked. The tab of an incoming whisper opens by itself without the window.
void GenOnlineDockOpenConversation(int64_t peerUserID, bool showWindow);
