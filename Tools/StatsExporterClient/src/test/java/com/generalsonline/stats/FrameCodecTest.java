package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;

class FrameCodecTest {
    private static byte[] body(int length, int seed) {
        byte[] b = new byte[length];
        for (int i = 0; i < length; i++) {
            b[i] = (byte) (seed + i);
        }
        return b;
    }

    @Test
    void envelopeIsLengthLittleEndianThenType() {
        byte[] frame = FrameCodec.encode(0x02, new byte[0x0102]);
        assertEquals(5 + 0x0102, frame.length);
        assertEquals(0x02, frame[0] & 0xFF);
        assertEquals(0x01, frame[1] & 0xFF);
        assertEquals(0, frame[2]);
        assertEquals(0, frame[3]);
        assertEquals(0x02, frame[4]);
    }

    @Test
    void decodesSeveralFramesFromOneChunk() throws Exception {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        stream.write(FrameCodec.encode(1, body(10, 1)));
        stream.write(FrameCodec.encode(2, body(300, 2)));
        stream.write(FrameCodec.encode(6, body(1, 3)));

        List<RawFrame> frames = new FrameCodec.Decoder().push(stream.toByteArray());
        assertEquals(3, frames.size());
        assertEquals(2, frames.get(1).type());
        assertArrayEquals(body(300, 2), frames.get(1).body());
    }

    @Test
    void partialReadsAreReassembledByteByByte() throws Exception {
        byte[] stream = new byte[0];
        ByteArrayOutputStream joined = new ByteArrayOutputStream();
        joined.write(FrameCodec.encode(4, body(50, 9)));
        joined.write(FrameCodec.encode(5, body(7, 4)));
        stream = joined.toByteArray();

        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        List<RawFrame> frames = new ArrayList<>();
        for (byte value : stream) {
            frames.addAll(decoder.push(new byte[] {value}));
        }
        assertEquals(2, frames.size());
        assertEquals(4, frames.get(0).type());
        assertEquals(5, frames.get(1).type());
        assertEquals(0, decoder.buffered());
    }

    @Test
    void incompleteFrameStaysBuffered() throws Exception {
        byte[] frame = FrameCodec.encode(3, body(20, 0));
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        assertTrue(decoder.push(frame, 0, frame.length - 1).isEmpty());
        assertEquals(frame.length - 1, decoder.buffered());
        assertEquals(1, decoder.push(frame, frame.length - 1, 1).size());
    }

    @Test
    void zeroLengthIsAFramingError() {
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        assertThrows(FrameException.class, () -> decoder.push(new byte[] {0, 0, 0, 0, 1}));
        assertThrows(FrameException.class, () -> decoder.push(new byte[] {1}));
    }

    @Test
    void oversizeIsAFramingError() {
        byte[] header = {1, 0, 0x04, 0, 2};
        assertThrows(FrameException.class, () -> new FrameCodec.Decoder().push(header));

        byte[] hugeNegative = {(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, 2};
        assertThrows(FrameException.class, () -> new FrameCodec.Decoder().push(hugeNegative));
    }

    @Test
    void largestOutboundBodyIsAccepted() throws Exception {
        byte[] frame = FrameCodec.encode(2, new byte[FrameCodec.MAX_OUTBOUND_BODY_BYTES]);
        List<RawFrame> frames = new FrameCodec.Decoder().push(frame);
        assertEquals(1, frames.size());
        assertEquals(FrameCodec.MAX_OUTBOUND_BODY_BYTES, frames.get(0).body().length);
    }

    @Test
    void unknownTypeIsDeliveredAndTheStreamContinues() throws Exception {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        stream.write(FrameCodec.encode(0x7F, body(4, 0)));
        stream.write(FrameCodec.encode(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray()));

        RecordingListener listener = new RecordingListener();
        for (RawFrame frame : new FrameCodec.Decoder().push(stream.toByteArray())) {
            FrameDispatcher.dispatch(frame, listener);
        }
        assertEquals(List.of(0x7F), listener.unknownTypes);
        assertEquals(1, listener.messages.size());
    }

    @Test
    void anInboundTypeSentByTheGameIsUnknownToTheClient() {
        RecordingListener listener = new RecordingListener();
        FrameDispatcher.dispatch(new RawFrame(FrameType.CHAT_SEND.id(), new byte[] {1}), listener);
        assertEquals(List.of(0x10), listener.unknownTypes);
    }

    @Test
    void aBodyThatDoesNotParseIsADecodeErrorNotAnException() {
        RecordingListener listener = new RecordingListener();
        FrameDispatcher.dispatch(new RawFrame(FrameType.STATS_SNAPSHOT.id(), new byte[] {(byte) 0xFF, (byte) 0xFF, (byte) 0xFF}), listener);
        assertEquals(List.of(FrameType.STATS_SNAPSHOT.id()), listener.decodeErrors);
        assertTrue(listener.messages.isEmpty());
    }

    @Test
    void chatSendFrameHasTheInboundTypeAndRoundTrips() throws Exception {
        ChatSend send = ChatSend.newBuilder().setChannel(ChatChannel.CHAT_CHANNEL_GAME_LOBBY).setText("hello").setAudience(ChatAudience.CHAT_AUDIENCE_ALLIES).build();
        byte[] frame = FrameCodec.encode(FrameType.CHAT_SEND.id(), send.toByteArray());
        RawFrame decoded = new FrameCodec.Decoder(FrameCodec.MAX_INBOUND_BODY_BYTES).push(frame).get(0);
        assertEquals(0x10, decoded.type());
        assertEquals(send, ChatSend.parseFrom(decoded.body()));
    }
}
