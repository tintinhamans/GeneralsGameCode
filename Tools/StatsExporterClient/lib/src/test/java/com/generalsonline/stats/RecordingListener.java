package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Records everything a listener is called with, in order. Thread safe, because pipe tests record from the reader thread. */
class RecordingListener implements StatsListener {
    final List<Object> messages = Collections.synchronizedList(new ArrayList<>());
    final List<Integer> unknownTypes = Collections.synchronizedList(new ArrayList<>());
    final List<Integer> decodeErrors = Collections.synchronizedList(new ArrayList<>());
    final List<Throwable> errors = Collections.synchronizedList(new ArrayList<>());
    final List<String> lifecycle = Collections.synchronizedList(new ArrayList<>());
    final List<Integer> frameTypes = Collections.synchronizedList(new ArrayList<>());
    final List<Long> dropped = Collections.synchronizedList(new ArrayList<>());
    final List<String> threads = Collections.synchronizedList(new ArrayList<>());

    private void note(Object message) {
        messages.add(message);
        threads.add(Thread.currentThread().getName());
    }

    @Override
    public void onConnected(String pipePath) {
        lifecycle.add("connected " + pipePath);
        threads.add(Thread.currentThread().getName());
    }

    @Override
    public void onDisconnected(Throwable cause) {
        lifecycle.add("disconnected" + (cause == null ? "" : " " + Format.cause(cause)));
        threads.add(Thread.currentThread().getName());
    }

    @Override
    public void onFrame(DecodedFrame frame) {
        frameTypes.add(frame.raw().type());
    }

    @Override
    public void onHello(Hello message) {
        note(message);
    }

    @Override
    public void onResponse(Response message) {
        note(message);
    }

    @Override
    public void onFramesDropped(long count) {
        dropped.add(count);
    }

    @Override
    public void onEvents(Events message) {
        note(message);
    }

    @Override
    public void onCamera(Camera message) {
        note(message);
    }

    @Override
    public void onMatchInfo(MatchInfo message) {
        note(message);
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        note(message);
    }

    @Override
    public void onMatchSummary(MatchSummary message) {
        note(message);
    }

    @Override
    public void onChatMessage(ChatMessage message) {
        note(message);
    }

    @Override
    public void onUiState(UiState message) {
        note(message);
    }

    @Override
    public void onLobbyState(LobbyState message) {
        note(message);
    }

    @Override
    public void unknownFrame(int type, byte[] body) {
        unknownTypes.add(type);
    }

    @Override
    public void onError(int type, Throwable cause) {
        errors.add(cause);
    }

    @Override
    public void decodeError(int type, byte[] body, Exception cause) {
        decodeErrors.add(type);
    }
}
