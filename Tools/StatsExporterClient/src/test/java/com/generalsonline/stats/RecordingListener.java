package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

import java.util.ArrayList;
import java.util.List;

/** Records everything a listener is called with, in order. */
final class RecordingListener implements FrameListener {
    final List<Object> messages = new ArrayList<>();
    final List<Integer> unknownTypes = new ArrayList<>();
    final List<Integer> decodeErrors = new ArrayList<>();

    @Override
    public void onEvents(Events message) {
        messages.add(message);
    }

    @Override
    public void onCamera(Camera message) {
        messages.add(message);
    }

    @Override
    public void onMatchInfo(MatchInfo message) {
        messages.add(message);
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        messages.add(message);
    }

    @Override
    public void onMatchSummary(MatchSummary message) {
        messages.add(message);
    }

    @Override
    public void onChatMessage(ChatMessage message) {
        messages.add(message);
    }

    @Override
    public void onUiState(UiState message) {
        messages.add(message);
    }

    @Override
    public void onLobbyState(LobbyState message) {
        messages.add(message);
    }

    @Override
    public void unknownFrame(int type, byte[] body) {
        unknownTypes.add(type);
    }

    @Override
    public void decodeError(int type, byte[] body, Exception cause) {
        decodeErrors.add(type);
    }
}
