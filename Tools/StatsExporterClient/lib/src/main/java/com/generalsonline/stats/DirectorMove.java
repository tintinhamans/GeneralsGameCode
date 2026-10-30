package com.generalsonline.stats;

import java.time.Duration;
import java.util.Objects;
import java.util.OptionalDouble;

/**
 * One decision of a {@link DirectorLogic}: put the camera at a position, panning there or cutting.
 */
public final class DirectorMove {
    private final double x;
    private final double y;
    private final Duration duration;
    private final boolean easeInOut;
    private final OptionalDouble zoom;
    private final Reason reason;

    /**
     * Creates the value.
     *
     * @param x the world x to look at
     * @param y the world y to look at
     * @param duration how long the pan takes; zero for a cut
     * @param easeInOut whether the pan starts and ends slowly
     * @param zoom the zoom to go to, empty to leave it
     * @param reason why the director moves
     */
    public DirectorMove(double x, double y, Duration duration, boolean easeInOut, OptionalDouble zoom, Reason reason) {
        this.x = x;
        this.y = y;
        this.duration = duration;
        this.easeInOut = easeInOut;
        this.zoom = zoom;
        this.reason = reason;
    }

    /**
     * Returns x.
     *
     * @return the world x to look at
     */
    public double x() {
        return x;
    }

    /**
     * Returns y.
     *
     * @return the world y to look at
     */
    public double y() {
        return y;
    }

    /**
     * Returns duration.
     *
     * @return how long the pan takes; zero for a cut
     */
    public Duration duration() {
        return duration;
    }

    /**
     * Returns easeInOut.
     *
     * @return whether the pan starts and ends slowly
     */
    public boolean easeInOut() {
        return easeInOut;
    }

    /**
     * Returns zoom.
     *
     * @return the zoom to go to, empty to leave it
     */
    public OptionalDouble zoom() {
        return zoom;
    }

    /**
     * Returns reason.
     *
     * @return why the director moves
     */
    public Reason reason() {
        return reason;
    }

    /** Why a director moves the camera. */
    public enum Reason {
        /** The best cell of the interest map changed. */
        HOTSPOT,
        /** A pending highlight is about to happen or just happened. */
        HIGHLIGHT,
        /** A highlight is over and the camera goes back to where it was. */
        RETURN,
        /** Nothing happens; the camera visits a base or an army. */
        OVERVIEW
    }

    /**
     * Tells whether this move is a cut.
     *
     * @return true when the camera jumps without a pan
     */
    public boolean cut() {
        return duration.isZero();
    }

    /**
     * Turns the move into the wire command.
     *
     * @return a camera move to the position over the duration
     */
    public CameraMove toCameraMove() {
        CameraMove move = CameraMove.to(x, y);
        if (zoom.isPresent()) {
            move = move.zoom(zoom.getAsDouble());
        }
        move = move.over(duration);
        return easeInOut && !duration.isZero() ? move.easeInOut() : move;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof DirectorMove)) {
            return false;
        }
        DirectorMove other = (DirectorMove) o;
        return Double.compare(x, other.x) == 0
                && Double.compare(y, other.y) == 0
                && Objects.equals(duration, other.duration)
                && easeInOut == other.easeInOut
                && Objects.equals(zoom, other.zoom)
                && Objects.equals(reason, other.reason);
    }

    @Override
    public int hashCode() {
        return Objects.hash(x, y, duration, easeInOut, zoom, reason);
    }

    @Override
    public String toString() {
        return "DirectorMove[" + "x=" + x + ", y=" + y + ", duration=" + duration + ", easeInOut=" + easeInOut + ", zoom=" + zoom + ", reason=" + reason + "]";
    }
}
