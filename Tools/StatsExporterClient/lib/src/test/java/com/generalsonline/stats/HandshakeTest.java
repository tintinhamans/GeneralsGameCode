package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static com.generalsonline.stats.PipeTestSupport.fake;
import static com.generalsonline.stats.PipeTestSupport.withHello;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Hello;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

/** The Hello handshake, protocol version refusal, the Hello timeout, gap detection and the clock estimate, on a fake pipe. */
class HandshakeTest {
    /** Frames with explicit sequence numbers, so a gap or a wrap can be scripted. */
    private static byte[] frames(Object... messageAndSequence) throws IOException {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        for (int i = 0; i < messageAndSequence.length; i += 2) {
            stream.write(TestMessages.outbound((Message) messageAndSequence[i], (Long) messageAndSequence[i + 1]));
        }
        return stream.toByteArray();
    }

    /** Remembers the cause the pipe reported when a connection ended. */
    private static final class CauseListener extends RecordingListener {
        final AtomicReference<Throwable> cause = new AtomicReference<>();
        final List<String> order = Collections.synchronizedList(new ArrayList<>());

        @Override
        public void onDisconnected(Throwable why) {
            super.onDisconnected(why);
            cause.set(why);
        }

        @Override
        public void onFramesDropped(long count) {
            super.onFramesDropped(count);
            order.add("dropped " + count);
        }

        @Override
        public void onUiState(com.generalsonline.stats.proto.UiState message) {
            super.onUiState(message);
            order.add("ui");
        }
    }

    // -- The Hello.

    @Test
    void theHelloIsExposedOnThePipeTheListenerAndTheState() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        RecordingListener listener = new RecordingListener();
        MatchState state = new MatchState();
        try (StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start()) {
            assertTrue(pipe.hello().isEmpty(), "nothing before the Hello");
            assertFalse(pipe.hasCapability("requests"));
            link.give(withHello());
            Hello hello = pipe.awaitHello(Duration.ofSeconds(10)).orElseThrow();
            assertEquals(TestMessages.hello(), hello);
            assertEquals(hello, pipe.hello().orElseThrow());
            assertTrue(pipe.hasCapability("requests"));
            assertFalse(pipe.hasCapability("nope"));
            await("the callbacks", () -> state.hello().isPresent() && !listener.messages.isEmpty());
            assertEquals(List.of(TestMessages.hello()), listener.messages);
            assertEquals(hello, state.hello().orElseThrow());
            assertTrue(state.hasCapability("events"));
            assertEquals(1, state.hello().orElseThrow().getReaderSlot());
            assertEquals(4, state.hello().orElseThrow().getMaxReaders());

            link.endOfStream();
            await("the end of the connection", () -> !pipe.isConnected());
            assertTrue(pipe.hello().isEmpty(), "the pipe forgets the Hello of a connection that ended");
            assertEquals(hello, state.hello().orElseThrow(), "the state keeps the last one");
        }
    }

    @Test
    void awaitHelloGivesUpAfterItsTimeout() throws Exception {
        try (StatsPipe pipe = fake().build().start()) {
            long start = System.nanoTime();
            assertTrue(pipe.awaitHello(Duration.ofMillis(50)).isEmpty());
            assertTrue((System.nanoTime() - start) / 1_000_000 >= 40);
        }
    }

    @Test
    void aProtocolVersionOtherThanTwoIsRefusedWithAClearException() throws Exception {
        for (int version : new int[] {1, 3, 0}) {
            PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
            link.give(TestMessages.outbound(TestMessages.hello().toBuilder().setProtocolVersion(version).build(), 0));
            CauseListener listener = new CauseListener();
            MatchState state = new MatchState();
            StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
            assertTrue(pipe.awaitStopped(10_000));

            ProtocolVersionException refused = assertInstanceOf(ProtocolVersionException.class, listener.cause.get());
            assertEquals(version, refused.version());
            assertEquals(version, refused.hello().getProtocolVersion());
            assertTrue(refused.getMessage().contains("version " + version), refused.getMessage());
            assertTrue(refused.getMessage().contains("version " + Handshake.PROTOCOL_VERSION), refused.getMessage());
            assertTrue(listener.messages.isEmpty(), "a refused Hello is not delivered");
            assertTrue(state.hello().isEmpty());
            assertTrue(pipe.hello().isEmpty());
            assertTrue(link.isClosed(), "the connection was closed");
        }
    }

    @Test
    void requireSupportedAcceptsOnlyTheLibrarysVersion() throws Exception {
        Handshake.requireSupported(TestMessages.hello());
        assertThrows(ProtocolVersionException.class, () -> Handshake.requireSupported(Hello.newBuilder().setProtocolVersion(1).build()));
        assertEquals(2, Handshake.PROTOCOL_VERSION);
    }

    @Test
    void aMissingHelloEndsTheConnectionWithATimeout() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        CauseListener listener = new CauseListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).helloTimeout(Duration.ofMillis(60)).build().start();
        assertTrue(pipe.awaitStopped(10_000), "the reader gave up on the silent connection");
        HelloTimeoutException timeout = assertInstanceOf(HelloTimeoutException.class, listener.cause.get());
        assertTrue(timeout.getMessage().contains("60 ms"), timeout.getMessage());
        assertTrue(link.isClosed());
    }

    @Test
    void anArrivingHelloCancelsTheTimeout() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello());
        CauseListener listener = new CauseListener();
        try (StatsPipe pipe = fake(link).listener(listener).reconnect(false).helloTimeout(Duration.ofMillis(60)).build().start()) {
            await("the Hello", () -> pipe.hello().isPresent());
            Thread.sleep(200);
            assertTrue(pipe.isConnected(), "no timeout after the Hello");
            assertTrue(listener.cause.get() == null);
        }
    }

    @Test
    void aFirstFrameThatIsNotAHelloIsRefused() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(TestMessages.outbound(TestMessages.uiState(), 0));
        CauseListener listener = new CauseListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        FrameException refused = assertInstanceOf(FrameException.class, listener.cause.get());
        assertTrue(refused.getMessage().contains("must be the Hello"), refused.getMessage());
        assertTrue(listener.messages.isEmpty());
    }

    @Test
    void aHelloThatDoesNotParseIsRefused() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(com.generalsonline.stats.internal.FrameCodec.encodeOutbound(FrameType.HELLO.id(), 0, 0, new byte[] {(byte) 0xFF, (byte) 0xFF, (byte) 0xFF}));
        CauseListener listener = new CauseListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        FrameException refused = assertInstanceOf(FrameException.class, listener.cause.get());
        assertTrue(refused.getMessage().contains("did not parse"), refused.getMessage());
    }

    @Test
    void theTimeoutsRejectBadValues() {
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().helloTimeout(Duration.ZERO));
        assertThrows(IllegalArgumentException.class, () -> StatsPipe.builder().requestTimeout(Duration.ofNanos(5)));
        assertThrows(NullPointerException.class, () -> StatsPipe.builder().helloTimeout(null));
        assertEquals(Duration.ofSeconds(10), StatsPipe.DEFAULT_HELLO_TIMEOUT);
    }

    @Test
    void theHandshakeHelpers() {
        Hello hello = TestMessages.hello();
        assertTrue(Handshake.hasCapability(hello, "requests"));
        assertFalse(Handshake.hasCapability(hello, "request"), "names are compared exactly");
        assertEquals(1_700_000_000_000L + 1500, Handshake.wallClockMillis(hello, 1_500_000));
        assertEquals("game 1.04, protocol 2, pid 4242, reader 1 of 4, 30 logic fps", Handshake.summary(hello));
    }

    // -- Dropped frames.

    @Test
    void consecutiveSequenceNumbersAreNoGap() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello(TestMessages.uiState(), TestMessages.uiState()));
        link.endOfStream();
        CauseListener listener = new CauseListener();
        MatchState state = new MatchState();
        StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(0, pipe.droppedFrames());
        assertEquals(0, state.droppedFrames());
        assertTrue(listener.dropped.isEmpty());
    }

    @Test
    void aGapIsCountedReportedAndToldBeforeTheFrameThatShowedIt() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        // 0 hello, 1 ui, then 2, 3 and 4 were dropped (the gap), 5 ui, 6 ui
        link.give(frames(TestMessages.hello(), 0L, TestMessages.uiState(), 1L, TestMessages.uiState(), 5L, TestMessages.uiState(), 6L));
        link.endOfStream();
        CauseListener listener = new CauseListener();
        MatchState state = new MatchState();
        List<String> stateEvents = Collections.synchronizedList(new ArrayList<>());
        state.addListener(new StateListener() {
            @Override
            public void framesDropped(long count, long total) {
                stateEvents.add(count + "/" + total);
            }

            @Override
            public void helloReceived(Hello hello) {
                stateEvents.add("hello");
            }
        });
        StatsPipe pipe = fake(link).listener(listener).state(state).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));

        assertEquals(3, pipe.droppedFrames(), "sequence 2, 3 and 4 are missing");
        assertEquals(3, state.droppedFrames());
        assertEquals(List.of(3L), listener.dropped);
        assertEquals(List.of("ui", "dropped 3", "ui", "ui"), listener.order, "the report comes before the frame after the gap");
        assertEquals(List.of("hello", "3/3"), stateEvents);
    }

    @Test
    void theCountSumsSeveralGapsAndConnections() throws Exception {
        PipeTestSupport.FakeLink first = new PipeTestSupport.FakeLink();
        PipeTestSupport.FakeLink second = new PipeTestSupport.FakeLink();
        first.give(frames(TestMessages.hello(), 0L, TestMessages.uiState(), 2L, TestMessages.uiState(), 4L));
        first.endOfStream();
        // A new connection starts at 0 again: that is no gap, a later jump is.
        second.give(frames(TestMessages.hello(), 0L, TestMessages.uiState(), 1L, TestMessages.uiState(), 11L));
        CauseListener listener = new CauseListener();
        MatchState state = new MatchState();
        try (StatsPipe pipe = fake(first, second).listener(listener).state(state).reconnectBackoff(Duration.ofMillis(1), Duration.ofMillis(5)).build().start()) {
            await("both connections", () -> pipe.droppedFrames() == 1 + 1 + 9);
            assertEquals(List.of(1L, 1L, 9L), listener.dropped);
            assertEquals(11, state.droppedFrames());
        }
    }

    @Test
    void theSequenceWrapsAroundWithoutAGapAndCountsAcrossTheWrap() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        // After the Hello, the sequence restarts high: 0xFFFFFFFE is a jump back (no count) that sets the expectation.
        // 0xFFFFFFFF follows without a gap, 0 follows it across the wrap without a gap, then 3 skips 1 and 2.
        link.give(frames(TestMessages.hello(), 0L, TestMessages.uiState(), 0xFFFFFFFEL, TestMessages.uiState(), 0xFFFFFFFFL,
            TestMessages.uiState(), 0L, TestMessages.uiState(), 3L));
        link.endOfStream();
        CauseListener listener = new CauseListener();
        StatsPipe pipe = fake(link).listener(listener).reconnect(false).build().start();
        assertTrue(pipe.awaitStopped(10_000));
        assertEquals(List.of(2L), listener.dropped);
        assertEquals(2, pipe.droppedFrames());
    }

    // -- The clock.

    @Test
    void theClockEstimateIsFedByTheFramesAndStartedAgainByTheHello() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        link.give(withHello(TestMessages.uiState(), TestMessages.uiState()));
        try (StatsPipe pipe = fake(link).reconnect(false).build().start()) {
            await("three samples", () -> pipe.clockSync().sampleCount() == 3);
            assertTrue(pipe.clockSync().offsetMicros().isPresent());
            long arrival = System.nanoTime() / 1000;
            assertTrue(pipe.clockSync().excessDelayMicros(2000, arrival).isPresent());
        }
    }
}
