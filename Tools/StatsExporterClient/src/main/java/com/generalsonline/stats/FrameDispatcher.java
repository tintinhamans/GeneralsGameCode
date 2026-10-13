package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.InvalidProtocolBufferException;

/** Decodes a frame body to its message and calls the matching listener method. */
public final class FrameDispatcher {
    private FrameDispatcher() {
    }

    public static void dispatch(RawFrame frame, FrameListener listener) {
        FrameType type = FrameType.outboundFromId(frame.type());
        if (type == null) {
            listener.unknownFrame(frame.type(), frame.body());
            return;
        }

        // Parse first so an exception thrown by the listener is never taken for a decode error.
        try {
            switch (type) {
                case MATCH_INFO -> {
                    MatchInfo message = MatchInfo.parseFrom(frame.body());
                    listener.onMatchInfo(message);
                }
                case STATS_SNAPSHOT -> {
                    StatsSnapshot message = StatsSnapshot.parseFrom(frame.body());
                    listener.onStatsSnapshot(message);
                }
                case MATCH_SUMMARY -> {
                    MatchSummary message = MatchSummary.parseFrom(frame.body());
                    listener.onMatchSummary(message);
                }
                case CHAT_MESSAGE -> {
                    ChatMessage message = ChatMessage.parseFrom(frame.body());
                    listener.onChatMessage(message);
                }
                case UI_STATE -> {
                    UiState message = UiState.parseFrom(frame.body());
                    listener.onUiState(message);
                }
                case LOBBY_STATE -> {
                    LobbyState message = LobbyState.parseFrom(frame.body());
                    listener.onLobbyState(message);
                }
                default -> listener.unknownFrame(frame.type(), frame.body());
            }
        } catch (InvalidProtocolBufferException e) {
            listener.decodeError(frame.type(), frame.body(), e);
        }
    }
}
