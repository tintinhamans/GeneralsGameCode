package com.generalsonline.stats.internal;

/**
 * Counts dropped frames from the per-reader sequence numbers. Not thread safe: one tracker per connection, used on the
 * thread that reads the frames. Not part of the public API.
 */
public final class SequenceTracker {
    private static final long MASK = 0xFFFFFFFFL;

    /** A forward gap above this is a sequence that went backwards (a restart), not a real number of drops. */
    private static final long MAX_PLAUSIBLE_GAP = 0x7FFFFFFFL;

    private boolean primed;
    private long expected;

    /**
     * Notes the next frame and tells how many frames were dropped before it. Arithmetic is modulo 2^32, so a wrap
     * from 4294967295 to 0 is no gap. The first frame (or a Hello, which starts a connection) only sets the
     * expectation. A sequence that jumps backwards counts nothing and restarts the expectation.
     *
     * @param sequence the sequence number of the frame, 0 to 4294967295
     * @param startsConnection true for the Hello frame
     * @return the number of dropped frames before this one, 0 when there is no gap
     */
    public long observe(long sequence, boolean startsConnection) {
        long gap = 0;
        if (primed && !startsConnection) {
            long difference = (sequence - expected) & MASK;
            if (difference <= MAX_PLAUSIBLE_GAP) {
                gap = difference;
            }
        }
        primed = true;
        expected = (sequence + 1) & MASK;
        return gap;
    }
}
