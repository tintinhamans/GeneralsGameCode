package com.generalsonline.stats.internal;

import com.generalsonline.stats.FrameException;
import com.generalsonline.stats.RawFrame;
import com.google.protobuf.MessageLite;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Envelope encoding. Outbound frames (game to reader) have a 17 byte little-endian header: u32 body length, u8 type,
 * u32 sequence, u64 timestamp in microseconds. Inbound frames (reader to game) have a 5 byte header: u32 body length,
 * u8 type. Not part of the public API.
 */
public final class FrameCodec {
    /** Header bytes of an inbound frame: length and type. */
    public static final int INBOUND_HEADER_BYTES = 5;

    /** Header bytes of an outbound frame: length, type, sequence and timestamp. */
    public static final int OUTBOUND_HEADER_BYTES = 17;

    /** Largest body the game sends (STATS_EXPORTER_MAX_BODY_BYTES). */
    public static final int MAX_OUTBOUND_BODY_BYTES = 262144;

    /** Largest body the game accepts (STATS_EXPORTER_MAX_INBOUND_BODY_BYTES). */
    public static final int MAX_INBOUND_BODY_BYTES = 4096;

    /** The wire form of a message with every field at its default: field 1 as varint 0, so a body is never empty. */
    private static final byte[] ALL_DEFAULT_BODY = {0x08, 0x00};

    /** The first four bytes of a dump file of format version 2 or later: "GOSD". A version 1 dump has no header. */
    public static final byte[] DUMP_MAGIC = {'G', 'O', 'S', 'D'};

    private FrameCodec() {
    }

    /**
     * Serializes a message for a frame body. A message with every field at its default would serialize to nothing,
     * but a body is never empty on this protocol, so it is written as {@code 08 00} like the game does.
     *
     * @param message the message
     * @return the body, at least 2 bytes when the message is empty
     */
    public static byte[] bodyOf(MessageLite message) {
        byte[] body = message.toByteArray();
        return body.length == 0 ? ALL_DEFAULT_BODY.clone() : body;
    }

    /**
     * Wraps a body in the inbound (reader to game) envelope.
     *
     * @param type the frame type byte
     * @param body the protobuf body, not empty
     * @return the bytes to put on the pipe
     */
    public static byte[] encodeInbound(int type, byte[] body) {
        requireBody(body);
        byte[] out = new byte[INBOUND_HEADER_BYTES + body.length];
        putU32(out, 0, body.length);
        out[4] = (byte) type;
        System.arraycopy(body, 0, out, INBOUND_HEADER_BYTES, body.length);
        return out;
    }

    /**
     * Wraps a body in the outbound (game to reader) envelope. The game does this; the library needs it for dump
     * files and for tests.
     *
     * @param type the frame type byte
     * @param sequence the per-reader sequence number, 0 to 4294967295
     * @param timestampMicros the producing clock in microseconds, treated as unsigned
     * @param body the protobuf body, not empty
     * @return the bytes of the frame
     */
    public static byte[] encodeOutbound(int type, long sequence, long timestampMicros, byte[] body) {
        requireBody(body);
        byte[] out = new byte[OUTBOUND_HEADER_BYTES + body.length];
        putU32(out, 0, body.length);
        out[4] = (byte) type;
        putU32(out, 5, sequence);
        for (int i = 0; i < 8; i++) {
            out[9 + i] = (byte) (timestampMicros >>> (8 * i));
        }
        System.arraycopy(body, 0, out, OUTBOUND_HEADER_BYTES, body.length);
        return out;
    }

    private static void requireBody(byte[] body) {
        if (body.length == 0) {
            throw new IllegalArgumentException("a frame body must not be empty");
        }
    }

    private static void putU32(byte[] out, int at, long value) {
        out[at] = (byte) value;
        out[at + 1] = (byte) (value >>> 8);
        out[at + 2] = (byte) (value >>> 16);
        out[at + 3] = (byte) (value >>> 24);
    }

    private static long u32(byte[] in, int at) {
        return (in[at] & 0xFFL) | ((in[at + 1] & 0xFFL) << 8) | ((in[at + 2] & 0xFFL) << 16) | ((in[at + 3] & 0xFFL) << 24);
    }

    /**
     * Incremental decoder: feed it whatever a read returned, however the bytes are split. Not thread safe.
     * A length of 0 or above the maximum is a {@link FrameException}; the decoder is unusable afterwards.
     */
    public static final class Decoder {
        private final int maxBody;
        private final int headerBytes;
        private byte[] buffer = new byte[8192];
        private int length;
        private boolean failed;

        /** Creates a decoder for outbound (17 byte header) frames with the largest body the game sends as its limit. */
        public Decoder() {
            this(MAX_OUTBOUND_BODY_BYTES);
        }

        /**
         * Creates a decoder for outbound (17 byte header) frames with a custom limit.
         *
         * @param maxBody the largest body length accepted
         */
        public Decoder(int maxBody) {
            this(maxBody, OUTBOUND_HEADER_BYTES);
        }

        private Decoder(int maxBody, int headerBytes) {
            this.maxBody = maxBody;
            this.headerBytes = headerBytes;
        }

        /**
         * Creates a decoder for inbound (5 byte header) frames, the form a reader sends; used by tests that look at
         * what was written to the pipe.
         *
         * @return a decoder limited to the largest body the game accepts
         */
        public static Decoder inbound() {
            return new Decoder(MAX_INBOUND_BODY_BYTES, INBOUND_HEADER_BYTES);
        }

        /**
         * Creates a decoder for the frames of a version 1 dump file: the 5 byte header, no sequence or timestamp.
         * The frames it returns carry sequence 0 and timestamp 0.
         *
         * @return a decoder limited to the largest body the game sends
         */
        public static Decoder legacy() {
            return new Decoder(MAX_OUTBOUND_BODY_BYTES, INBOUND_HEADER_BYTES);
        }

        /**
         * Adds bytes and returns every frame they completed.
         *
         * @param data the bytes read
         * @param offset where the bytes start in the array
         * @param count how many bytes there are
         * @return the completed frames, possibly none
         * @throws FrameException when a length field is 0 or above the limit
         */
        public List<RawFrame> push(byte[] data, int offset, int count) throws FrameException {
            if (failed) {
                throw new FrameException("decoder already failed");
            }
            if (length + count > buffer.length) {
                buffer = Arrays.copyOf(buffer, Math.max(buffer.length * 2, length + count));
            }
            System.arraycopy(data, offset, buffer, length, count);
            length += count;

            List<RawFrame> frames = new ArrayList<>();
            int position = 0;
            while (length - position >= headerBytes) {
                long bodyLength = u32(buffer, position);
                if (bodyLength == 0 || bodyLength > maxBody) {
                    failed = true;
                    throw new FrameException("bad frame length " + bodyLength);
                }
                int frameLength = headerBytes + (int) bodyLength;
                if (length - position < frameLength) {
                    break;
                }
                int type = buffer[position + 4] & 0xFF;
                long sequence = 0;
                long timestamp = 0;
                if (headerBytes == OUTBOUND_HEADER_BYTES) {
                    sequence = u32(buffer, position + 5);
                    for (int i = 7; i >= 0; i--) {
                        timestamp = (timestamp << 8) | (buffer[position + 9 + i] & 0xFFL);
                    }
                }
                frames.add(new RawFrame(type, sequence, timestamp, Arrays.copyOfRange(buffer, position + headerBytes, position + frameLength)));
                position += frameLength;
            }
            if (position > 0) {
                System.arraycopy(buffer, position, buffer, 0, length - position);
                length -= position;
            }
            return frames;
        }

        /**
         * Adds a whole array.
         *
         * @param data the bytes read
         * @return the completed frames, possibly none
         * @throws FrameException when a length field is 0 or above the limit
         */
        public List<RawFrame> push(byte[] data) throws FrameException {
            return push(data, 0, data.length);
        }

        /**
         * Tells how much of an incomplete frame is waiting.
         *
         * @return the bytes of an incomplete frame still buffered
         */
        public int buffered() {
            return length;
        }
    }
}
