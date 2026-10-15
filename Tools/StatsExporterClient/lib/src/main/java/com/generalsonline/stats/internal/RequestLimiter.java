package com.generalsonline.stats.internal;

/**
 * A token bucket for the request rate the game allows: {@code burst} requests at once, refilled at {@code perSecond}.
 * Thread safe. Not part of the public API.
 */
public final class RequestLimiter {
    private final double perSecond;
    private final double burst;
    private double tokens;
    private long lastNanos;
    private boolean started;

    /**
     * Creates a limiter that starts full.
     *
     * @param perSecond the sustained rate, more than 0
     * @param burst the bucket size, at least 1
     */
    public RequestLimiter(double perSecond, double burst) {
        if (perSecond <= 0 || burst < 1) {
            throw new IllegalArgumentException("the limiter needs a rate above 0 and a burst of at least 1");
        }
        this.perSecond = perSecond;
        this.burst = burst;
        this.tokens = burst;
    }

    /**
     * Takes one token if there is one.
     *
     * @param nowNanos a monotonic clock in nanoseconds, for example {@code System.nanoTime()}
     * @return true when the request may be sent, false when it would exceed the rate
     */
    public synchronized boolean tryAcquire(long nowNanos) {
        if (started) {
            double elapsedSeconds = Math.max(0, nowNanos - lastNanos) / 1e9;
            tokens = Math.min(burst, tokens + elapsedSeconds * perSecond);
        }
        started = true;
        lastNanos = nowNanos;
        if (tokens >= 1.0) {
            tokens -= 1.0;
            return true;
        }
        return false;
    }
}
