package com.generalsonline.stats.internal;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.FrameException;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.RawFrame;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.Request;
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

    private static byte[] out(int type, long sequence, long timestamp, byte[] body) {
        return FrameCodec.encodeOutbound(type, sequence, timestamp, body);
    }

    @Test
    void outboundEnvelopeIsLengthTypeSequenceTimestampLittleEndian() {
        byte[] frame = out(0x02, 0x0A0B0C0DL, 0x0102030405060708L, new byte[0x0102]);
        assertEquals(17 + 0x0102, frame.length);
        assertEquals(0x02, frame[0] & 0xFF);
        assertEquals(0x01, frame[1] & 0xFF);
        assertEquals(0, frame[2]);
        assertEquals(0, frame[3]);
        assertEquals(0x02, frame[4]);
        assertArrayEquals(new byte[] {0x0D, 0x0C, 0x0B, 0x0A}, java.util.Arrays.copyOfRange(frame, 5, 9));
        assertArrayEquals(new byte[] {8, 7, 6, 5, 4, 3, 2, 1}, java.util.Arrays.copyOfRange(frame, 9, 17));
    }

    @Test
    void inboundEnvelopeIsLengthThenType() {
        byte[] frame = FrameCodec.encodeInbound(0x11, new byte[] {1, 2, 3});
        assertArrayEquals(new byte[] {3, 0, 0, 0, 0x11, 1, 2, 3}, frame);
    }

    @Test
    void sequenceAndTimestampSurviveTheirLargestValues() throws Exception {
        long[][] cases = {{0, 0}, {1, 1}, {0xFFFFFFFFL, -1L}, {0x80000000L, Long.MIN_VALUE}, {0x7FFFFFFFL, Long.MAX_VALUE}, {123456789L, 0xFEDCBA9876543210L}};
        for (long[] c : cases) {
            RawFrame decoded = new FrameCodec.Decoder().push(out(9, c[0], c[1], body(5, 1))).get(0);
            assertEquals(9, decoded.type());
            assertEquals(c[0], decoded.sequence());
            assertEquals(c[1], decoded.timestampMicros());
            assertArrayEquals(body(5, 1), decoded.body());
        }
    }

    @Test
    void anEmptyBodyCannotBeEncoded() {
        assertThrows(IllegalArgumentException.class, () -> out(1, 0, 0, new byte[0]));
        assertThrows(IllegalArgumentException.class, () -> FrameCodec.encodeInbound(1, new byte[0]));
    }

    @Test
    void anAllDefaultMessageIsWrittenAsFieldOneZero() throws Exception {
        assertArrayEquals(new byte[] {0x08, 0x00}, FrameCodec.bodyOf(FeedConfigQuery.getDefaultInstance()));
        assertEquals(FeedConfigQuery.getDefaultInstance(), FeedConfigQuery.parseFrom(FrameCodec.bodyOf(FeedConfigQuery.getDefaultInstance())));
        Request request = Request.newBuilder().setId(5).build();
        assertArrayEquals(request.toByteArray(), FrameCodec.bodyOf(request), "a message with content is written as it is");
    }

    @Test
    void decodesSeveralFramesFromOneChunk() throws Exception {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        stream.write(out(1, 0, 10, body(10, 1)));
        stream.write(out(2, 1, 20, body(300, 2)));
        stream.write(out(6, 2, 30, body(1, 3)));

        List<RawFrame> frames = new FrameCodec.Decoder().push(stream.toByteArray());
        assertEquals(3, frames.size());
        assertEquals(2, frames.get(1).type());
        assertEquals(1, frames.get(1).sequence());
        assertEquals(20, frames.get(1).timestampMicros());
        assertArrayEquals(body(300, 2), frames.get(1).body());
    }

    @Test
    void partialReadsAreReassembledByteByByte() throws Exception {
        ByteArrayOutputStream joined = new ByteArrayOutputStream();
        joined.write(out(4, 7, 70, body(50, 9)));
        joined.write(out(5, 8, 80, body(7, 4)));
        byte[] stream = joined.toByteArray();

        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        List<RawFrame> frames = new ArrayList<>();
        for (byte value : stream) {
            frames.addAll(decoder.push(new byte[] {value}));
        }
        assertEquals(2, frames.size());
        assertEquals(4, frames.get(0).type());
        assertEquals(8, frames.get(1).sequence());
        assertEquals(0, decoder.buffered());
    }

    @Test
    void aTruncatedHeaderStaysBufferedUntilTheRestArrives() throws Exception {
        byte[] frame = out(3, 42, 4200, body(20, 0));
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        // 16 bytes are one short of the 17 byte header: not even the length can be trusted to be complete yet.
        assertTrue(decoder.push(frame, 0, 16).isEmpty());
        assertEquals(16, decoder.buffered());
        assertTrue(decoder.push(frame, 16, 10).isEmpty(), "header complete, body not");
        assertEquals(26, decoder.buffered());
        List<RawFrame> frames = decoder.push(frame, 26, frame.length - 26);
        assertEquals(1, frames.size());
        assertEquals(42, frames.get(0).sequence());
        assertEquals(0, decoder.buffered());
    }

    @Test
    void incompleteBodyStaysBuffered() throws Exception {
        byte[] frame = out(3, 0, 0, body(20, 0));
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        assertTrue(decoder.push(frame, 0, frame.length - 1).isEmpty());
        assertEquals(frame.length - 1, decoder.buffered());
        assertEquals(1, decoder.push(frame, frame.length - 1, 1).size());
    }

    @Test
    void zeroLengthIsAFramingError() {
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        byte[] header = new byte[17];
        header[4] = 1;
        assertThrows(FrameException.class, () -> decoder.push(header));
        assertThrows(FrameException.class, () -> decoder.push(new byte[] {1}), "a failed decoder stays failed");
    }

    @Test
    void oversizeIsAFramingError() {
        byte[] header = new byte[17];
        header[0] = 1;
        header[2] = 0x04;
        assertThrows(FrameException.class, () -> new FrameCodec.Decoder().push(header));

        byte[] hugeNegative = new byte[17];
        java.util.Arrays.fill(hugeNegative, 0, 4, (byte) 0xFF);
        assertThrows(FrameException.class, () -> new FrameCodec.Decoder().push(hugeNegative));
    }

    @Test
    void largestOutboundBodyIsAccepted() throws Exception {
        byte[] frame = out(2, 0, 0, new byte[FrameCodec.MAX_OUTBOUND_BODY_BYTES]);
        List<RawFrame> frames = new FrameCodec.Decoder().push(frame);
        assertEquals(1, frames.size());
        assertEquals(FrameCodec.MAX_OUTBOUND_BODY_BYTES, frames.get(0).body().length);
    }

    @Test
    void chatSendFrameHasTheInboundTypeAndRoundTrips() throws Exception {
        ChatSend send = ChatSend.newBuilder().setChannel(ChatChannel.CHAT_CHANNEL_GAME_LOBBY).setText("hello").setAudience(ChatAudience.CHAT_AUDIENCE_ALLIES).build();
        byte[] frame = FrameCodec.encodeInbound(FrameType.CHAT_SEND.id(), send.toByteArray());
        RawFrame decoded = FrameCodec.Decoder.inbound().push(frame).get(0);
        assertEquals(0x10, decoded.type());
        assertEquals(0, decoded.sequence());
        assertEquals(send, ChatSend.parseFrom(decoded.body()));
    }

    @Test
    void theLegacyDecoderReadsTheFiveByteEnvelope() throws Exception {
        byte[] old = FrameCodec.encodeInbound(0x05, body(9, 3));
        RawFrame frame = FrameCodec.Decoder.legacy().push(old).get(0);
        assertEquals(0x05, frame.type());
        assertEquals(0, frame.timestampMicros());
        assertArrayEquals(body(9, 3), frame.body());
    }
}
