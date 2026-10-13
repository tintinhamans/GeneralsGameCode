package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

/**
 * Receives decoded frames. All methods have empty defaults, so a reader overrides only what it needs.
 * Called on the single reader thread of {@link StatsPipeClient} (or the caller's thread when replaying a dump).
 */
public interface FrameListener {
    default void onConnected(String pipePath) {
    }

    default void onDisconnected(Throwable cause) {
    }

    default void onMatchInfo(MatchInfo message) {
    }

    default void onStatsSnapshot(StatsSnapshot message) {
    }

    default void onMatchSummary(MatchSummary message) {
    }

    default void onChatMessage(ChatMessage message) {
    }

    default void onUiState(UiState message) {
    }

    default void onLobbyState(LobbyState message) {
    }

    /** A frame type this client does not know; skipped, the stream continues. */
    default void unknownFrame(int type, byte[] body) {
    }

    /** A known frame type whose body did not parse; skipped, the stream continues. */
    default void decodeError(int type, byte[] body, Exception cause) {
    }
}
