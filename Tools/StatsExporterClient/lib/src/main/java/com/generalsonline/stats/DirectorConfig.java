package com.generalsonline.stats;

import java.time.Duration;
import java.util.Objects;
import java.util.OptionalDouble;

/**
 * The tunables of a {@link DirectorLogic}: immutable, with defaults that suit a normal match. Change them with
 * {@link #toBuilder()}.
 *
 * <pre>{@code
 * DirectorConfig config = DirectorConfig.defaults().toBuilder()
 *     .mode(DirectorMode.HOTSPOT)
 *     .minDwell(Duration.ofSeconds(8))
 *     .build();
 * }</pre>
 *
 * <p>Scores are those of the {@code InterestMap}: about one point per health point of damage dealt in a cell, half a
 * point per cost unit destroyed, plus a share of the value of opposing armies standing close together.
 */
public final class DirectorConfig {
    private static final DirectorConfig DEFAULTS = new Builder().build();

    private final DirectorMode mode;
    private final Duration minDwell;
    private final double switchRatio;
    private final double minScore;
    private final double fightScore;
    private final Duration quietDelay;
    private final double panLimit;
    private final double panSpeed;
    private final Duration minPan;
    private final Duration maxPan;
    private final Duration highlightLead;
    private final Duration highlightHold;
    private final boolean returnAfterHighlight;
    private final Duration overviewDwell;
    private final Duration resumeDelay;
    private final OptionalDouble zoom;

    private DirectorConfig(Builder b) {
        this.mode = b.mode;
        this.minDwell = b.minDwell;
        this.switchRatio = b.switchRatio;
        this.minScore = b.minScore;
        this.fightScore = b.fightScore;
        this.quietDelay = b.quietDelay;
        this.panLimit = b.panLimit;
        this.panSpeed = b.panSpeed;
        this.minPan = b.minPan;
        this.maxPan = b.maxPan;
        this.highlightLead = b.highlightLead;
        this.highlightHold = b.highlightHold;
        this.returnAfterHighlight = b.returnAfterHighlight;
        this.overviewDwell = b.overviewDwell;
        this.resumeDelay = b.resumeDelay;
        this.zoom = b.zoom;
    }

    /**
     * Returns the default configuration.
     *
     * @return mode DIRECTOR, 5 s minimum dwell, a 1.5 times better spot to switch, pans up to 1500 world units, 4 s lead before an impact
     */
    public static DirectorConfig defaults() {
        return DEFAULTS;
    }

    /**
     * Starts a builder with the values of this configuration.
     *
     * @return a builder
     */
    public Builder toBuilder() {
        return new Builder(this);
    }

    /**
     * Returns the mode.
     *
     * @return what the director pays attention to
     */
    public DirectorMode mode() {
        return mode;
    }

    /**
     * Returns the minimum time on a hotspot.
     *
     * @return no hotspot switch happens sooner than this after the last move
     */
    public Duration minDwell() {
        return minDwell;
    }

    /**
     * Returns the factor a new spot must beat the current one by.
     *
     * @return at least 1; 1.5 means the new cell needs a 50% higher score
     */
    public double switchRatio() {
        return switchRatio;
    }

    /**
     * Returns the score below which a place is not interesting.
     *
     * @return the lowest hotspot score (modes HOTSPOT and DIRECTOR)
     */
    public double minScore() {
        return minScore;
    }

    /**
     * Returns the score that counts as a fight.
     *
     * @return the lowest score that hands over from the overview to hotspot following (mode OVERVIEW)
     */
    public double fightScore() {
        return fightScore;
    }

    /**
     * Returns how long it must be quiet before the overview starts.
     *
     * @return the quiet time
     */
    public Duration quietDelay() {
        return quietDelay;
    }

    /**
     * Returns the longest distance that is panned.
     *
     * @return in world units; a longer way is a cut
     */
    public double panLimit() {
        return panLimit;
    }

    /**
     * Returns the pan speed.
     *
     * @return world units per second; with the limits below it sets the length of a pan
     */
    public double panSpeed() {
        return panSpeed;
    }

    /**
     * Returns the shortest pan.
     *
     * @return the least time a pan takes
     */
    public Duration minPan() {
        return minPan;
    }

    /**
     * Returns the longest pan.
     *
     * @return the most time a pan takes
     */
    public Duration maxPan() {
        return maxPan;
    }

    /**
     * Returns how long before an impact the camera is in place.
     *
     * @return the lead time of a highlight with a known impact
     */
    public Duration highlightLead() {
        return highlightLead;
    }

    /**
     * Returns how long a highlight stays on screen after its impact.
     *
     * @return the hold time (for a highlight without an impact: after the camera arrived)
     */
    public Duration highlightHold() {
        return highlightHold;
    }

    /**
     * Tells whether the camera goes back after a highlight.
     *
     * @return true to return to where the camera was
     */
    public boolean returnAfterHighlight() {
        return returnAfterHighlight;
    }

    /**
     * Returns how long the overview stays at each place.
     *
     * @return the overview dwell
     */
    public Duration overviewDwell() {
        return overviewDwell;
    }

    /**
     * Returns the time without user input after which the director takes the camera back.
     *
     * @return the resume delay
     */
    public Duration resumeDelay() {
        return resumeDelay;
    }

    /**
     * Returns the zoom the director sets with every move.
     *
     * @return the zoom, empty to leave the zoom to the user
     */
    public OptionalDouble zoom() {
        return zoom;
    }

    @Override
    public String toString() {
        return "DirectorConfig[mode=" + mode + ", minDwell=" + minDwell + ", switchRatio=" + switchRatio + ", minScore=" + minScore
            + ", fightScore=" + fightScore + ", quietDelay=" + quietDelay + ", panLimit=" + panLimit + ", highlightLead=" + highlightLead
            + ", highlightHold=" + highlightHold + ", overviewDwell=" + overviewDwell + ", resumeDelay=" + resumeDelay + "]";
    }

    /** Builds a {@link DirectorConfig}; start from {@link DirectorConfig#defaults()}{@code .toBuilder()}. */
    public static final class Builder {
        private DirectorMode mode = DirectorMode.DIRECTOR;
        private Duration minDwell = Duration.ofSeconds(5);
        private double switchRatio = 1.5;
        private double minScore = 150;
        private double fightScore = 400;
        private Duration quietDelay = Duration.ofSeconds(4);
        private double panLimit = 1500;
        private double panSpeed = 2000;
        private Duration minPan = Duration.ofMillis(600);
        private Duration maxPan = Duration.ofMillis(2500);
        private Duration highlightLead = Duration.ofSeconds(4);
        private Duration highlightHold = Duration.ofSeconds(3);
        private boolean returnAfterHighlight = true;
        private Duration overviewDwell = Duration.ofSeconds(6);
        private Duration resumeDelay = Duration.ofSeconds(10);
        private OptionalDouble zoom = OptionalDouble.empty();

        private Builder() {
        }

        private Builder(DirectorConfig c) {
            mode = c.mode;
            minDwell = c.minDwell;
            switchRatio = c.switchRatio;
            minScore = c.minScore;
            fightScore = c.fightScore;
            quietDelay = c.quietDelay;
            panLimit = c.panLimit;
            panSpeed = c.panSpeed;
            minPan = c.minPan;
            maxPan = c.maxPan;
            highlightLead = c.highlightLead;
            highlightHold = c.highlightHold;
            returnAfterHighlight = c.returnAfterHighlight;
            overviewDwell = c.overviewDwell;
            resumeDelay = c.resumeDelay;
            zoom = c.zoom;
        }

        /**
         * Sets the mode.
         *
         * @param mode what the director pays attention to
         * @return this builder
         */
        public Builder mode(DirectorMode mode) {
            this.mode = Objects.requireNonNull(mode, "mode");
            return this;
        }

        /**
         * Sets the minimum time on a hotspot.
         *
         * @param minDwell not negative
         * @return this builder
         */
        public Builder minDwell(Duration minDwell) {
            this.minDwell = nonNegative(minDwell, "minDwell");
            return this;
        }

        /**
         * Sets the factor a new spot must beat the current one by.
         *
         * @param switchRatio at least 1
         * @return this builder
         */
        public Builder switchRatio(double switchRatio) {
            if (!(switchRatio >= 1.0 && Double.isFinite(switchRatio))) {
                throw new IllegalArgumentException("switchRatio is at least 1");
            }
            this.switchRatio = switchRatio;
            return this;
        }

        /**
         * Sets the score below which a place is not interesting.
         *
         * @param minScore not negative
         * @return this builder
         */
        public Builder minScore(double minScore) {
            this.minScore = nonNegative(minScore, "minScore");
            return this;
        }

        /**
         * Sets the score that counts as a fight.
         *
         * @param fightScore not negative
         * @return this builder
         */
        public Builder fightScore(double fightScore) {
            this.fightScore = nonNegative(fightScore, "fightScore");
            return this;
        }

        /**
         * Sets how long it must be quiet before the overview starts.
         *
         * @param quietDelay not negative
         * @return this builder
         */
        public Builder quietDelay(Duration quietDelay) {
            this.quietDelay = nonNegative(quietDelay, "quietDelay");
            return this;
        }

        /**
         * Sets the longest distance that is panned.
         *
         * @param panLimit in world units, not negative; 0 cuts always
         * @return this builder
         */
        public Builder panLimit(double panLimit) {
            this.panLimit = nonNegative(panLimit, "panLimit");
            return this;
        }

        /**
         * Sets the pan speed.
         *
         * @param panSpeed world units per second, above 0
         * @return this builder
         */
        public Builder panSpeed(double panSpeed) {
            if (!(panSpeed > 0.0 && Double.isFinite(panSpeed))) {
                throw new IllegalArgumentException("panSpeed is above 0");
            }
            this.panSpeed = panSpeed;
            return this;
        }

        /**
         * Sets the limits of a pan's length.
         *
         * @param minPan the shortest pan, not negative
         * @param maxPan the longest pan, at least {@code minPan} and at most 60 s
         * @return this builder
         */
        public Builder panTime(Duration minPan, Duration maxPan) {
            nonNegative(minPan, "minPan");
            nonNegative(maxPan, "maxPan");
            if (maxPan.compareTo(minPan) < 0 || maxPan.compareTo(CameraMove.MAX_TRANSITION) > 0) {
                throw new IllegalArgumentException("maxPan is at least minPan and at most " + CameraMove.MAX_TRANSITION.toSeconds() + " s");
            }
            this.minPan = minPan;
            this.maxPan = maxPan;
            return this;
        }

        /**
         * Sets how long before an impact the camera is in place.
         *
         * @param highlightLead not negative
         * @return this builder
         */
        public Builder highlightLead(Duration highlightLead) {
            this.highlightLead = nonNegative(highlightLead, "highlightLead");
            return this;
        }

        /**
         * Sets how long a highlight stays on screen after its impact.
         *
         * @param highlightHold not negative
         * @return this builder
         */
        public Builder highlightHold(Duration highlightHold) {
            this.highlightHold = nonNegative(highlightHold, "highlightHold");
            return this;
        }

        /**
         * Sets whether the camera goes back after a highlight.
         *
         * @param returnAfterHighlight true to return to where the camera was
         * @return this builder
         */
        public Builder returnAfterHighlight(boolean returnAfterHighlight) {
            this.returnAfterHighlight = returnAfterHighlight;
            return this;
        }

        /**
         * Sets how long the overview stays at each place.
         *
         * @param overviewDwell above zero
         * @return this builder
         */
        public Builder overviewDwell(Duration overviewDwell) {
            if (nonNegative(overviewDwell, "overviewDwell").isZero()) {
                throw new IllegalArgumentException("overviewDwell is above zero");
            }
            this.overviewDwell = overviewDwell;
            return this;
        }

        /**
         * Sets the time without user input after which the director takes the camera back.
         *
         * @param resumeDelay not negative
         * @return this builder
         */
        public Builder resumeDelay(Duration resumeDelay) {
            this.resumeDelay = nonNegative(resumeDelay, "resumeDelay");
            return this;
        }

        /**
         * Sets the zoom the director applies with every move.
         *
         * @param zoom a zoom as the {@code Camera} frame reports it, 0.05 to 20, or empty to leave it to the user
         * @return this builder
         */
        public Builder zoom(OptionalDouble zoom) {
            Objects.requireNonNull(zoom, "zoom");
            if (zoom.isPresent() && !(zoom.getAsDouble() >= 0.05 && zoom.getAsDouble() <= 20.0)) {
                throw new IllegalArgumentException("zoom is 0.05 to 20");
            }
            this.zoom = zoom;
            return this;
        }

        /**
         * Builds the configuration.
         *
         * @return the immutable configuration
         */
        public DirectorConfig build() {
            return new DirectorConfig(this);
        }

        private static Duration nonNegative(Duration value, String name) {
            Objects.requireNonNull(value, name);
            if (value.isNegative()) {
                throw new IllegalArgumentException(name + " is not negative");
            }
            return value;
        }

        private static double nonNegative(double value, String name) {
            if (!(value >= 0.0 && Double.isFinite(value))) {
                throw new IllegalArgumentException(name + " is not negative");
            }
            return value;
        }
    }
}
