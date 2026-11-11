#pragma once
#include <cstdint>

namespace LobbyConnectionPolicy
{
inline bool CanRetry(bool manualLobby, bool inMatch, bool local, bool connected, bool connecting, int64_t sinceLastAttemptMs)
{
    return manualLobby && !inMatch && !local && !connected && !connecting && sinceLastAttemptMs >= 1000;
}
}
