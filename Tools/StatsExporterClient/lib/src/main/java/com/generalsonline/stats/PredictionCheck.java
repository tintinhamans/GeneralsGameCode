package com.generalsonline.stats;

import java.util.Objects;

/**
 * The result of comparing the lobby's random-side prediction with what the engine resolved, reported once on the
 * LOADING {@code MatchInfo}.
 */
public final class PredictionCheck {
    private final boolean matched;
    private final boolean randomStateClean;

    /**
     * Creates the value.
     *
     * @param matched every predicted side, colour, start position and the observer camera were right
     * @param randomStateClean the logic random generator was untouched between the lobby seed and the resolution
     */
    public PredictionCheck(boolean matched, boolean randomStateClean) {
        this.matched = matched;
        this.randomStateClean = randomStateClean;
    }

    /**
     * Returns matched.
     *
     * @return every predicted side, colour, start position and the observer camera were right
     */
    public boolean matched() {
        return matched;
    }

    /**
     * Returns randomStateClean.
     *
     * @return the logic random generator was untouched between the lobby seed and the resolution
     */
    public boolean randomStateClean() {
        return randomStateClean;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof PredictionCheck)) {
            return false;
        }
        PredictionCheck other = (PredictionCheck) o;
        return matched == other.matched
                && randomStateClean == other.randomStateClean;
    }

    @Override
    public int hashCode() {
        return Objects.hash(matched, randomStateClean);
    }

    @Override
    public String toString() {
        return "PredictionCheck[" + "matched=" + matched + ", randomStateClean=" + randomStateClean + "]";
    }
}
