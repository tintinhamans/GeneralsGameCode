package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import static com.generalsonline.stats.PipeTestSupport.await;
import static com.generalsonline.stats.PipeTestSupport.fake;
import static com.generalsonline.stats.PipeTestSupport.withHello;

import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/** The public API of StatsPipe: builder validation, chat limits, the connection loop on a fake link, executor and state wiring. */
class StatsPipeTest {
    private static byte[] allFrames() throws IOException {
        return TestMessages.outboundStream(TestMessages.all());
    }

    // -- Builder validation.

    @Test
    void theBuilderRejectsBadValuesAtOnce() {
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().pid(-1));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().pipe(" "));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().pipe(null));
        assertThrows(NullPointerException.class, () -> StatsPipe.builder().listener(null));
        assertThrows(NullPointerException.class, () -> StatsPipe.builder().executor(null));
        assertThrows(NullPointerException.class, () -> StatsPipe.builder().state(null));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().reconnectBackoff(Duration.ZERO, Duration.ofSeconds(1)));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().reconnectBackoff(Duration.ofSeconds(2), Duration.ofSeconds(1)));
    }

    @Test
    void aPidAndAPipeAreAlternatives() {
        String message = assertThrows(IllegalStateException.class, () -> StatsPipe.builder().pid(5).pipe("\\\\.\\pipe\\x").build()).getMessage();
        assertTrue(message.contains("alternatives"), message);
        StatsPipe.builder().pid(5).build();
        StatsPipe.builder().pipe("\\\\.\\pipe\\x").build();
        StatsPipe.builder().build();
    }

    @Test
    void aPipeStartsOnlyOnce() {
        StatsPipe pipe = fake().build();
        pipe.start();
        try {
            assertThrows(IllegalStateException.class, pipe::start);
        } finally {
            pipe.close();
        }
        assertThrows(IllegalStateException.class, pipe::start, "a closed pipe cannot start");
        pipe.close();
    }

    // -- Chat validation.

    @Test
    void chatTextRules() {
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.requireSendable(""));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.requireSendable("   "));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.requireSendable(null));
        StatsPipe.requireSendable("a".repeat(1024));
        String tooLong = assertThrows(IllegalArgumentException.class, () -> StatsPipe.requireSendable("a".repeat(1025))).getMessage();
        assertTrue(tooLong.contains("1025 bytes"), tooLong);
        // The limit is in UTF-8 bytes, not characters: 400 three-byte characters are 1200 bytes.
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.requireSendable("中".repeat(400)));
        StatsPipe.requireSendable("中".repeat(341));
    }

    @Test
    void sendChatRejectsBadArgumentsBeforeTouchingThePipe() throws IOException {
        StatsPipe pipe = fake().build();
        assertThrows(IllegalArgumentException.class, () -> pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, ""));
        assertThrows(IllegalArgumentException.class, () -> pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, "x".repeat(1025)));
        assertThrows(IllegalArgumentException.class, () -> pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, "x", ChatAudience.CHAT_AUDIENCE_OTHER));
        assertThrows(IllegalArgumentException.class, () -> pipe.sendChat(ChatChannel.UNRECOGNIZED, "x"));
        assertThrows(NullPointerException.class, () -> pipe.sendChat(null, "x"));
        assertThrows(NullPointerException.class, () -> pipe.sendChat(ChatChannel.CHAT_CHANNEL_ROOM, "x", null));
        assertFalse(pipe.sendChat(ChatChannel.CHAT_CHANNEL_ROOM, "valid but not connected"), "not connected: false, nothing thrown");
    }

    @Test
    void sendChatWritesAChatSendFrameWhileConnected() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        RecordingListener listener = new RecordingListener();
        try (StatsPipe pipe = fake(link).listener(listener).build().start()) {
            await("connection", pipe::isConnected);
            assertTrue(pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, "gg wp 中", ChatAudience.CHAT_AUDIENCE_ALLIES));
            assertTrue(pipe.sendChat(ChatChannel.CHAT_CHANNEL_ROOM, "hello"));

            assertEquals(2, link.written.size());
            RawFrame first = FrameCodec.Decoder.inbound().push(link.written.get(0)).get(0);
            assertEquals(FrameType.CHAT_SEND.id(), first.type());
            ChatSend sent = ChatSend.parseFrom(first.body());
            assertEquals(ChatChannel.CHAT_CHANNEL_MATCH, sent.getChannel());
            assertEquals("gg wp 中", sent.getText());
            assertEquals(ChatAudience.CHAT_AUDIENCE_ALLIES, sent.getAudience());
            ChatSend second = ChatSend.parseFrom(FrameCodec.Decoder.inbound().push(link.written.get(1)).get(0).body());
            assertEquals(ChatAudience.CHAT_AUDIENCE_EVERYONE, second.getAudience(), "the short form sends to everyone");
        }
    }

    // -- The connection loop.

    @Test
    void everyFrameTypeReachesTheListenerAndTheStateThroughTheRealLoop() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(allFrames());
        link.endOfStream();
        RecordingListener listener = new RecordingListener();
        MatchState state = new MatchState();

        StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000), "without reconnect the reader ends with the connection");

        assertEquals(List.of("connected fake-pipe-1", "disconnected"), listener.lifecycle);
        assertEquals(List.of(TestMessages.all()), listener.messages, "all ten frame types, in order, the Hello first");
        assertEquals(TestMessages.all().length, listener.frameTypes.size());
        assertEquals("123456789", state.matchId(), "the state was fed too");
        assertEquals(1, state.players().size());
        assertEquals(1, state.chat().size());
        assertFalse(pipe.isConnected());
        assertTrue(pipe.connectedPipe().isEmpty());
        assertTrue(listener.errors.isEmpty(), listener.errors.toString());
    }

    @Test
    void theStateIsUpdatedBeforeTheListenerSeesTheSameFrame() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello(TestMessages.matchInfo()));
        link.endOfStream();
        MatchState state = new MatchState();
        List<String> seen = Collections.synchronizedList(new ArrayList<>());
        StatsListener listener = new StatsListener() {
            @Override
            public void onMatchInfo(com.generalsonline.stats.proto.MatchInfo message) {
                seen.add(state.matchId());
            }
        };
        StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(List.of("123456789"), seen);
    }

    @Test
    void theFirstFreePipeWinsAndItsPidIsKnown() throws Exception {
        PipeTestSupport.FakeLink free = new PipeTestSupport.FakeLink();
        List<String> tried = Collections.synchronizedList(new ArrayList<>());
        RecordingListener listener = new RecordingListener();
        StatsPipe.Builder builder = StatsPipe.builder().listener(listener).reconnect(false)
            .candidates(() -> List.of(PipeDiscovery.pipePath(900), PipeDiscovery.pipePath(300), PipeDiscovery.pipePath(42)))
            .opener(path -> {
                tried.add(path);
                if (path.endsWith("-900")) {
                    throw new IOException("All pipe instances are busy");
                }
                return free;
            });
        try (StatsPipe pipe = builder.build().start()) {
            await("connection", pipe::isConnected);
            assertEquals(List.of(PipeDiscovery.pipePath(900), PipeDiscovery.pipePath(300)), tried, "the busy highest pid was skipped, the search stopped at the first free one");
            assertEquals(300, pipe.connectedPid().getAsInt());
            assertEquals(PipeDiscovery.pipePath(300), pipe.connectedPipe().get());
        }
    }

    @Test
    void withReconnectTheNextConnectionFollowsTheLostOne() throws Exception {
        PipeTestSupport.FakeLink first = new PipeTestSupport.FakeLink();
        PipeTestSupport.FakeLink second = new PipeTestSupport.FakeLink();
        first.endOfStream();
        second.give(withHello(TestMessages.uiState()));
        RecordingListener listener = new RecordingListener();
        try (StatsPipe pipe = fake(first, second).listener(listener).reconnectBackoff(Duration.ofMillis(1), Duration.ofMillis(5)).build().start()) {
            await("the second connection's frames", () -> listener.messages.size() == 2);
            assertEquals(List.of("connected fake-pipe-1", "disconnected", "connected fake-pipe-1"), listener.lifecycle);
            assertTrue(pipe.isConnected());
        }
    }

    @Test
    void whileNoGameRunsThePipeWaitsAndCloseStopsIt() throws Exception {
        StatsPipe pipe = StatsPipe.builder().candidates(List::of).reconnectBackoff(Duration.ofMillis(1), Duration.ofMillis(5)).build().start();
        Thread.sleep(50);
        assertFalse(pipe.isConnected());
        pipe.close();
        assertTrue(pipe.awaitStopped(10_000), "close ends the waiting reader");
    }

    @Test
    void aBadLengthDisconnectsWithTheFramingErrorAsTheCause() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(new byte[17]);
        RecordingListener listener = new RecordingListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(List.of("connected fake-pipe-1", "disconnected bad frame length 0"), listener.lifecycle);
    }

    @Test
    void aThrowingListenerCostsOnlyItsFrame() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(allFrames());
        link.endOfStream();
        RecordingListener listener = new RecordingListener() {
            @Override
            public void onUiState(com.generalsonline.stats.proto.UiState message) {
                throw new IllegalStateException("listener bug");
            }
        };
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(1, listener.errors.size());
        assertEquals("listener bug", listener.errors.get(0).getMessage());
        assertEquals(TestMessages.all().length - 1, listener.messages.size(), "every other frame arrived");
        assertEquals("disconnected", listener.lifecycle.get(1), "the connection was not treated as lost");
    }

    @Test
    void aThrowingStateListenerIsReportedToTheListener() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello(TestMessages.camera()));
        link.endOfStream();
        MatchState state = new MatchState();
        state.addListener(new StateListener() {
            @Override
            public void cameraChanged(com.generalsonline.stats.proto.Camera camera) {
                throw new IllegalStateException("state listener bug");
            }
        });
        RecordingListener listener = new RecordingListener();
        StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(1, listener.errors.size());
        assertEquals("state listener bug", listener.errors.get(0).getMessage());
        assertEquals(2, listener.messages.size(), "the listener still got the Hello and the camera frame");
    }

    // -- Executor.

    @Test
    void callbacksRunOnTheExecutorInOrderAndOffTheReaderThread() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(allFrames());
        link.endOfStream();
        ExecutorService ui = Executors.newSingleThreadExecutor(r -> new Thread(r, "ui-test"));
        try {
            RecordingListener listener = new RecordingListener();
            MatchState state = new MatchState();
            Map<String, Boolean> stateThreads = new ConcurrentHashMap<>();
            state.addListener(new StateListener() {
                @Override
                public void matchChanged(com.generalsonline.stats.proto.MatchInfo match) {
                    stateThreads.put(Thread.currentThread().getName(), true);
                }
            });
            StatsPipe pipe = fake(link).listener(listener).state(state).executor(ui).reconnect(false).build().start();
            assertTrue(pipe.awaitStopped(10_000));
            ui.shutdown();
            assertTrue(ui.awaitTermination(10, TimeUnit.SECONDS));

            assertEquals(List.of(TestMessages.all()), listener.messages);
            assertEquals(List.of("connected fake-pipe-1", "disconnected"), listener.lifecycle);
            assertEquals(Collections.singleton("ui-test"), new java.util.HashSet<>(listener.threads), "every callback ran on the executor's thread");
            assertEquals(Map.of("ui-test", true), stateThreads, "state change callbacks too");
        } finally {
            ui.shutdownNow();
        }
    }

    @Test
    void withoutAnExecutorCallbacksRunOnTheReaderThread() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello(TestMessages.uiState()));
        link.endOfStream();
        RecordingListener listener = new RecordingListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(Collections.singleton("stats-pipe-reader"), new java.util.HashSet<>(listener.threads));
    }

    @Test
    void aRejectingExecutorDropsTheTasksWithoutKillingTheReader() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(allFrames());
        link.endOfStream();
        RecordingListener listener = new RecordingListener();
        StatsPipe pipe = fake(link).listener(listener).executor(task -> {
            throw new java.util.concurrent.RejectedExecutionException("shut down");
        }).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000), "the reader ended normally");
        assertTrue(listener.messages.isEmpty());
        assertInstanceOf(StatsPipe.class, pipe);
    }
}
