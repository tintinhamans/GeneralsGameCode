package com.generalsonline.stats;

/**
 * The time source of a {@link DirectorLogic} and a {@link Director}: milliseconds on a monotonic scale. A test supplies
 * its own to make the decisions deterministic.
 */
@FunctionalInterface
public interface DirectorClock {
    /**
     * Returns the current time.
     *
     * @return milliseconds since some fixed origin; never goes back
     */
    long millis();

    /**
     * Returns the real clock.
     *
     * @return a clock on {@link System#nanoTime()}
     */
    static DirectorClock system() {
        return () -> System.nanoTime() / 1_000_000L;
    }
}
