package com.generalsonline.stats;

import java.util.Objects;

/**
 * One undecoded frame: the type byte, the envelope's sequence number and timestamp, and the protobuf body without the
 * header.
 */
public final class RawFrame {
    private final int type;
    private final long sequence;
    private final long timestampMicros;
    private final byte[] body;

    /**
     * Creates the value.
     *
     * @param type the type byte of the frame, 0 to 255
     * @param sequence the per-reader sequence number, 0 to 4294967295: the number of frames queued for this reader since it connected (the Hello is 0); a frame the game dropped keeps its number, so a gap is a dropped frame
     * @param timestampMicros the game's monotonic exporter clock in microseconds when the frame was produced, an unsigned 64 bit value (see {@link Long#compareUnsigned}); 0 for frames of a version 1 dump, which has none
     * @param body the protobuf body; the array is not copied, so do not modify it
     * @throws IllegalArgumentException when the sequence is outside 0 to 4294967295
     */
    public RawFrame(int type, long sequence, long timestampMicros, byte[] body) {
        if (sequence < 0 || sequence > MAX_SEQUENCE) {
            throw new IllegalArgumentException("sequence must be 0 to 4294967295, got " + sequence);
        }
        this.type = type;
        this.sequence = sequence;
        this.timestampMicros = timestampMicros;
        this.body = body;
    }

    /**
     * Returns type.
     *
     * @return the type byte of the frame, 0 to 255
     */
    public int type() {
        return type;
    }

    /**
     * Returns sequence.
     *
     * @return the per-reader sequence number, 0 to 4294967295: the number of frames queued for this reader since it connected (the Hello is 0); a frame the game dropped keeps its number, so a gap is a dropped frame
     */
    public long sequence() {
        return sequence;
    }

    /**
     * Returns timestampMicros.
     *
     * @return the game's monotonic exporter clock in microseconds when the frame was produced, an unsigned 64 bit value (see {@link Long#compareUnsigned}); 0 for frames of a version 1 dump, which has none
     */
    public long timestampMicros() {
        return timestampMicros;
    }

    /**
     * Returns body.
     *
     * @return the protobuf body; the array is not copied, so do not modify it
     */
    public byte[] body() {
        return body;
    }

    /** The largest sequence number, 2^32 - 1. */
    public static final long MAX_SEQUENCE = 0xFFFFFFFFL;

    /**
     * Checks the sequence number.
     *
     * @throws IllegalArgumentException when the sequence is outside 0 to 4294967295
     */


    /**
     * Creates a frame without envelope data: sequence 0 and timestamp 0. For tests and for synthesized frames.
     *
     * @param type the type byte of the frame
     * @param body the protobuf body
     */
    public RawFrame(int type, byte[] body) {
        this(type, 0, 0, body);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof RawFrame)) {
            return false;
        }
        RawFrame other = (RawFrame) o;
        return type == other.type
                && sequence == other.sequence
                && timestampMicros == other.timestampMicros
                && Objects.equals(body, other.body);
    }

    @Override
    public int hashCode() {
        return Objects.hash(type, sequence, timestampMicros, body);
    }

    @Override
    public String toString() {
        return "RawFrame[" + "type=" + type + ", sequence=" + sequence + ", timestampMicros=" + timestampMicros + ", body=" + body + "]";
    }
}
