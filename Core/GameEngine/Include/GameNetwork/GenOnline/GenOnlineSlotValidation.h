#pragma once

// Lobby slot values skip ParseAsciiStringToGameInfo, so they get its bounds here and an out of range value becomes unset.
inline int GenOnlineSlotValueOrUnset(int value, int end)
{
	return (value >= 0 && value < end) ? value : -1;
}
