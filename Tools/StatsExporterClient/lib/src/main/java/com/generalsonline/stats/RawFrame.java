package com.generalsonline.stats;

/**
 * One undecoded frame: the type byte, the envelope's sequence number and timestamp, and the protobuf body without the
 * header.
 *
 * @param type the type byte of the frame, 0 to 255
 * @param sequence the per-reader sequence number, 0 to 4294967295: the number of frames queued for this reader since
 *        it connected (the Hello is 0); a frame the game dropped keeps its number, so a gap is a dropped frame
 * @param timestampMicros the game's monotonic exporter clock in microseconds when the frame was produced, an unsigned
 *        64 bit value (see {@link Long#compareUnsigned}); 0 for frames of a version 1 dump, which has none
 * @param body the protobuf body; the array is not copied, so do not modify it
 */
public record RawFrame(int type, long sequence, long timestampMicros, byte[] body) {
    /** The largest sequence number, 2^32 - 1. */
    public static final long MAX_SEQUENCE = 0xFFFFFFFFL;

    /**
     * Checks the sequence number.
     *
     * @throws IllegalArgumentException when the sequence is outside 0 to 4294967295
     */
    public RawFrame {
        if (sequence < 0 || sequence > MAX_SEQUENCE) {
            throw new IllegalArgumentException("sequence must be 0 to 4294967295, got " + sequence);
        }
    }

    /**
     * Creates a frame without envelope data: sequence 0 and timestamp 0. For tests and for synthesized frames.
     *
     * @param type the type byte of the frame
     * @param body the protobuf body
     */
    public RawFrame(int type, byte[] body) {
        this(type, 0, 0, body);
    }
}
