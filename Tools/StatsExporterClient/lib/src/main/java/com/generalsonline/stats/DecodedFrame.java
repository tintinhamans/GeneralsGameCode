package com.generalsonline.stats;

import com.google.protobuf.Message;

/**
 * One frame after parsing: the raw bytes, the frame type and the decoded message. Parsing happens once, on the reader
 * thread, and everything after that (dump files, statistics, user interfaces) works from this object.
 *
 * @param raw the frame as received
 * @param type the frame type, or null for a type byte this library does not know
 * @param message the decoded message, or null when the type is unknown or the body did not parse
 * @param error the parse failure, or null when the body parsed (or the type is unknown)
 */
public record DecodedFrame(RawFrame raw, FrameType type, Message message, Exception error) {
    private static final int WIRE_HEADER_BYTES = 17;

    /**
     * Returns the size of the body.
     *
     * @return the body length in bytes, without the 17 byte header
     */
    public int bodyBytes() {
        return raw.body().length;
    }

    /**
     * Returns the size of the frame on the wire.
     *
     * @return the body length plus the 17 byte header (4 byte length, 1 byte type, 4 byte sequence, 8 byte timestamp)
     */
    public int wireBytes() {
        return raw.body().length + WIRE_HEADER_BYTES;
    }

    /**
     * Returns the sequence number of the frame.
     *
     * @return the per-reader sequence number, 0 to 4294967295; see {@link RawFrame#sequence()}
     */
    public long sequence() {
        return raw.sequence();
    }

    /**
     * Returns the production time of the frame.
     *
     * @return microseconds on the game's monotonic exporter clock, unsigned; see {@link RawFrame#timestampMicros()}
     */
    public long timestampMicros() {
        return raw.timestampMicros();
    }

    /**
     * Returns a printable name of the frame type.
     *
     * @return "MATCH_INFO", "CAMERA", ... or "UNKNOWN 0x7f" for an unknown type byte
     */
    public String typeName() {
        return type != null ? type.name() : String.format("UNKNOWN 0x%02x", raw.type());
    }

    /**
     * Tells whether the frame decoded to a message.
     *
     * @return true for a frame of a known type whose body parsed
     */
    public boolean isDecoded() {
        return message != null;
    }
}
