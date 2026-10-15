package com.generalsonline.stats;

import java.util.OptionalLong;

/**
 * Estimates the offset between the game's frame clock and a local clock, and how late a frame is compared with the best
 * frame seen lately. A frame carries the game's monotonic timestamp; the reader notes its own monotonic time when the
 * frame arrives. The difference {@code received - timestamp} is the clock offset plus the transport delay, and its
 * minimum over a window of recent frames is the best estimate of the offset (the frame that travelled fastest). The
 * excess of any single frame over that minimum is its extra delay: queueing behind a slow reader, or a busy pipe.
 *
 * <p>The delay in the offset itself cannot be known from one side, so the numbers are "one-way-ish": reliable for
 * comparing frames with each other, not an absolute latency. {@link StatsPipe} feeds one instance per connection, see
 * {@link StatsPipe#clockSync()}. All methods are thread safe.
 */
public final class ClockSync {
    /** The number of recent frames the minimum is taken over unless another size is given. */
    public static final int DEFAULT_WINDOW = 256;

    private final long[] samples;
    private int count;
    private int next;

    /** Creates an estimator with the default window of {@value #DEFAULT_WINDOW} frames. */
    public ClockSync() {
        this(DEFAULT_WINDOW);
    }

    /**
     * Creates an estimator.
     *
     * @param window how many recent frames the minimum is taken over, at least 1
     */
    public ClockSync(int window) {
        if (window < 1) {
            throw new IllegalArgumentException("the window must hold at least 1 sample, got " + window);
        }
        this.samples = new long[window];
    }

    /**
     * Notes one frame.
     *
     * @param frameTimestampMicros {@link RawFrame#timestampMicros()} of the frame
     * @param receivedMicros the local monotonic time the frame arrived, in microseconds (for example
     *        {@code System.nanoTime() / 1000})
     */
    public synchronized void observe(long frameTimestampMicros, long receivedMicros) {
        samples[next] = receivedMicros - frameTimestampMicros;
        next = (next + 1) % samples.length;
        if (count < samples.length) {
            count++;
        }
    }

    /** Forgets every sample; a new connection has a new game clock origin relative to the reader. */
    public synchronized void reset() {
        count = 0;
        next = 0;
    }

    /**
     * Counts the samples in the window.
     *
     * @return 0 to the window size
     */
    public synchronized int sampleCount() {
        return count;
    }

    /**
     * Returns the estimated offset: the local monotonic time at game timestamp 0, including the fastest frame's delay.
     *
     * @return the minimum of {@code received - timestamp} over the window, empty before the first frame
     */
    public synchronized OptionalLong offsetMicros() {
        if (count == 0) {
            return OptionalLong.empty();
        }
        long min = Long.MAX_VALUE;
        for (int i = 0; i < count; i++) {
            min = Math.min(min, samples[i]);
        }
        return OptionalLong.of(min);
    }

    /**
     * Returns how much later a frame arrived than the fastest recent one.
     *
     * @param frameTimestampMicros the frame's timestamp
     * @param receivedMicros the local monotonic time it arrived
     * @return the extra delay in microseconds, never negative; empty before the first frame was observed
     */
    public OptionalLong excessDelayMicros(long frameTimestampMicros, long receivedMicros) {
        OptionalLong offset = offsetMicros();
        if (offset.isEmpty()) {
            return OptionalLong.empty();
        }
        return OptionalLong.of(Math.max(0, receivedMicros - frameTimestampMicros - offset.getAsLong()));
    }

    /**
     * Converts a game timestamp to the local monotonic clock.
     *
     * @param frameTimestampMicros a timestamp from this connection
     * @return {@code timestamp + offset}, the local time the frame was (about) produced; empty before the first frame
     */
    public OptionalLong toLocalMicros(long frameTimestampMicros) {
        OptionalLong offset = offsetMicros();
        return offset.isEmpty() ? OptionalLong.empty() : OptionalLong.of(frameTimestampMicros + offset.getAsLong());
    }
}
