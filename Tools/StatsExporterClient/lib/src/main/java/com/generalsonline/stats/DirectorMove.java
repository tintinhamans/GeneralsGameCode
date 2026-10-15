package com.generalsonline.stats;

import java.time.Duration;
import java.util.OptionalDouble;

/**
 * One decision of a {@link DirectorLogic}: put the camera at a position, panning there or cutting.
 *
 * @param x the world x to look at
 * @param y the world y to look at
 * @param duration how long the pan takes; zero for a cut
 * @param easeInOut whether the pan starts and ends slowly
 * @param zoom the zoom to go to, empty to leave it
 * @param reason why the director moves
 */
public record DirectorMove(double x, double y, Duration duration, boolean easeInOut, OptionalDouble zoom, Reason reason) {
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
}
