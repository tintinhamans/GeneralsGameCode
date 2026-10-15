package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import java.util.EnumSet;
import java.util.List;
import java.util.Set;

/** FrameDispatcher: every frame type reaches its listener method, and nothing can bypass the dispatch. */
class DispatchTest {
    private static DecodedFrame decode(Message message) {
        return FrameDispatcher.decode(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
    }

    @Test
    void everyFrameTypeReachesItsOwnTypedMethodAndOnFrame() {
        // The test messages cover every outbound frame type, so a new type without a test message fails here first.
        Set<FrameType> covered = EnumSet.noneOf(FrameType.class);
        RecordingListener listener = new RecordingListener();
        for (Message message : TestMessages.all()) {
            DecodedFrame frame = decode(message);
            assertTrue(frame.isDecoded(), message.getClass().getSimpleName());
            covered.add(frame.type());
            FrameDispatcher.deliver(frame, listener);
        }
        for (FrameType type : FrameType.values()) {
            assertEquals(!type.inbound(), covered.contains(type), "test message for " + type);
        }
        assertEquals(List.of(TestMessages.all()), listener.messages, "each message arrived at its own method, in order");
        assertEquals(TestMessages.all().length, listener.frameTypes.size(), "onFrame saw every frame");
    }

    @Test
    void eachFrameTypeCallsExactlyItsOwnMethod() {
        List<String> called = new java.util.ArrayList<>();
        StatsListener byName = new StatsListener() {
            @Override
            public void onHello(com.generalsonline.stats.proto.Hello m) {
                called.add("onHello");
            }

            @Override
            public void onResponse(com.generalsonline.stats.proto.Response m) {
                called.add("onResponse");
            }

            @Override
            public void onMatchInfo(com.generalsonline.stats.proto.MatchInfo m) {
                called.add("onMatchInfo");
            }

            @Override
            public void onStatsSnapshot(com.generalsonline.stats.proto.StatsSnapshot m) {
                called.add("onStatsSnapshot");
            }

            @Override
            public void onMatchSummary(com.generalsonline.stats.proto.MatchSummary m) {
                called.add("onMatchSummary");
            }

            @Override
            public void onChatMessage(com.generalsonline.stats.proto.ChatMessage m) {
                called.add("onChatMessage");
            }

            @Override
            public void onUiState(com.generalsonline.stats.proto.UiState m) {
                called.add("onUiState");
            }

            @Override
            public void onLobbyState(com.generalsonline.stats.proto.LobbyState m) {
                called.add("onLobbyState");
            }

            @Override
            public void onEvents(com.generalsonline.stats.proto.Events m) {
                called.add("onEvents");
            }

            @Override
            public void onCamera(com.generalsonline.stats.proto.Camera m) {
                called.add("onCamera");
            }
        };
        for (Message message : TestMessages.all()) {
            FrameDispatcher.deliver(decode(message), byName);
        }
        assertEquals(List.of("onHello", "onMatchInfo", "onStatsSnapshot", "onMatchSummary", "onChatMessage", "onUiState", "onLobbyState", "onEvents", "onCamera", "onResponse"), called);
    }

    @Test
    void onFrameAlsoSeesUnknownAndUnparsableFrames() {
        RecordingListener listener = new RecordingListener();
        FrameDispatcher.dispatch(new RawFrame(0x7F, new byte[] {1, 2}), listener);
        FrameDispatcher.dispatch(new RawFrame(FrameType.STATS_SNAPSHOT.id(), new byte[] {(byte) 0xFF, (byte) 0xFF, (byte) 0xFF}), listener);
        assertEquals(List.of(0x7F, FrameType.STATS_SNAPSHOT.id()), listener.frameTypes);
        assertEquals(List.of(0x7F), listener.unknownTypes);
        assertEquals(List.of(FrameType.STATS_SNAPSHOT.id()), listener.decodeErrors);
        assertTrue(listener.messages.isEmpty());
    }

    @Test
    void anInboundTypeSentByTheGameIsUnknownToTheClient() {
        RecordingListener listener = new RecordingListener();
        FrameDispatcher.dispatch(new RawFrame(FrameType.CHAT_SEND.id(), new byte[] {1}), listener);
        FrameDispatcher.dispatch(new RawFrame(FrameType.REQUEST.id(), new byte[] {1}), listener);
        assertEquals(List.of(0x10, 0x11), listener.unknownTypes);
    }

    @Test
    void frameTypeIdsAreUniqueAndOutboundLookupSkipsTheInboundType() {
        Set<Integer> ids = new java.util.HashSet<>();
        for (FrameType type : FrameType.values()) {
            assertTrue(ids.add(type.id()), "duplicate id " + type);
            if (!type.inbound()) {
                assertEquals(type, FrameType.outboundFromId(type.id()));
            }
        }
        org.junit.jupiter.api.Assertions.assertNull(FrameType.outboundFromId(FrameType.CHAT_SEND.id()));
        org.junit.jupiter.api.Assertions.assertNull(FrameType.outboundFromId(FrameType.REQUEST.id()));
        assertEquals(FrameType.HELLO, FrameType.outboundFromId(0x09));
        assertEquals(FrameType.RESPONSE, FrameType.outboundFromId(0x0A));
        assertEquals("UNKNOWN 0x7f", FrameDispatcher.decode(new RawFrame(0x7F, new byte[] {1})).typeName());
        assertEquals("CAMERA", decode(TestMessages.camera()).typeName());
    }

    @Test
    void aThrowingListenerIsReportedThroughOnErrorAndDoesNotPropagate() {
        RecordingListener listener = new RecordingListener() {
            @Override
            public void onChatMessage(com.generalsonline.stats.proto.ChatMessage message) {
                throw new IllegalStateException("listener bug");
            }
        };
        FrameDispatcher.deliverGuarded(decode(TestMessages.chatMessage()), listener);
        assertEquals(1, listener.errors.size());
        assertEquals("listener bug", listener.errors.get(0).getMessage());

        FrameDispatcher.deliverGuarded(decode(TestMessages.uiState()), listener);
        assertEquals(1, listener.messages.size(), "the next frame is still delivered");
    }

    @Test
    void aFailingOnFrameDoesNotStopTheTypedMethod() {
        RecordingListener listener = new RecordingListener() {
            @Override
            public void onFrame(DecodedFrame frame) {
                throw new IllegalArgumentException("tap bug");
            }
        };
        FrameDispatcher.deliverGuarded(decode(TestMessages.uiState()), listener);
        assertEquals(1, listener.errors.size());
        assertEquals(1, listener.messages.size(), "the typed method still ran");
    }

    @Test
    void errorsCanBeReportedToAnotherListener() {
        RecordingListener errors = new RecordingListener();
        StatsListener broken = new StatsListener() {
            @Override
            public void onUiState(com.generalsonline.stats.proto.UiState message) {
                throw new IllegalStateException("x");
            }
        };
        FrameDispatcher.deliverGuarded(decode(TestMessages.uiState()), broken, errors);
        assertEquals(1, errors.errors.size());
    }

    @Test
    void aListenerThatThrowsFromOnErrorIsIgnored() {
        StatsListener hopeless = new StatsListener() {
            @Override
            public void onUiState(com.generalsonline.stats.proto.UiState message) {
                throw new IllegalStateException("x");
            }

            @Override
            public void onError(int type, Throwable cause) {
                throw new IllegalStateException("y");
            }
        };
        FrameDispatcher.deliverGuarded(decode(TestMessages.uiState()), hopeless);
    }

    @Test
    void unguardedDeliveryPropagatesTheListenersException() {
        StatsListener broken = new StatsListener() {
            @Override
            public void onUiState(com.generalsonline.stats.proto.UiState message) {
                throw new IllegalStateException("boom");
            }
        };
        org.junit.jupiter.api.Assertions.assertThrows(IllegalStateException.class, () -> FrameDispatcher.deliver(decode(TestMessages.uiState()), broken));
    }
}
