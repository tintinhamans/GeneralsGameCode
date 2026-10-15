package com.generalsonline.stats.internal;

import com.generalsonline.stats.FrameException;
import com.generalsonline.stats.RawFrame;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Envelope encoding: u32 little-endian body length, u8 type, body. Not part of the public API. */
public final class FrameCodec {
    /** Bytes of the length and type that precede every body. */
    public static final int HEADER_BYTES = 5;

    /** Largest body the game sends (STATS_EXPORTER_MAX_BODY_BYTES). */
    public static final int MAX_OUTBOUND_BODY_BYTES = 262144;

    /** Largest body the game accepts (STATS_EXPORTER_MAX_INBOUND_BODY_BYTES). */
    public static final int MAX_INBOUND_BODY_BYTES = 4096;

    private FrameCodec() {
    }

    /**
     * Wraps a body in the envelope.
     *
     * @param type the frame type byte
     * @param body the protobuf body, not empty
     * @return the bytes to put on the pipe
     */
    public static byte[] encode(int type, byte[] body) {
        if (body.length == 0) {
            throw new IllegalArgumentException("a frame body must not be empty");
        }
        byte[] out = new byte[HEADER_BYTES + body.length];
        out[0] = (byte) body.length;
        out[1] = (byte) (body.length >>> 8);
        out[2] = (byte) (body.length >>> 16);
        out[3] = (byte) (body.length >>> 24);
        out[4] = (byte) type;
        System.arraycopy(body, 0, out, HEADER_BYTES, body.length);
        return out;
    }

    /**
     * Incremental decoder: feed it whatever a read returned, however the bytes are split. Not thread safe.
     * A length of 0 or above the maximum is a {@link FrameException}; the decoder is unusable afterwards.
     */
    public static final class Decoder {
        private final int maxBody;
        private byte[] buffer = new byte[8192];
        private int length;
        private boolean failed;

        /** Creates a decoder with the largest body the game sends as its limit. */
        public Decoder() {
            this(MAX_OUTBOUND_BODY_BYTES);
        }

        /**
         * Creates a decoder with a custom limit.
         *
         * @param maxBody the largest body length accepted
         */
        public Decoder(int maxBody) {
            this.maxBody = maxBody;
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
            while (length - position >= HEADER_BYTES) {
                long bodyLength = (buffer[position] & 0xFFL)
                    | ((buffer[position + 1] & 0xFFL) << 8)
                    | ((buffer[position + 2] & 0xFFL) << 16)
                    | ((buffer[position + 3] & 0xFFL) << 24);
                if (bodyLength == 0 || bodyLength > maxBody) {
                    failed = true;
                    throw new FrameException("bad frame length " + bodyLength);
                }
                int frameLength = HEADER_BYTES + (int) bodyLength;
                if (length - position < frameLength) {
                    break;
                }
                int type = buffer[position + 4] & 0xFF;
                frames.add(new RawFrame(type, Arrays.copyOfRange(buffer, position + HEADER_BYTES, position + frameLength)));
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
