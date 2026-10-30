package com.generalsonline.stats;

import com.generalsonline.stats.proto.CameraControlQuery;
import com.generalsonline.stats.proto.CameraEasing;

import java.time.Duration;
import java.util.Objects;
import java.util.OptionalDouble;

/**
 * One absolute camera command: any subset of the position, zoom, angle and pitch, optionally spread over a transition.
 * It is an immutable value; every method returns a changed copy, so a base move can be reused.
 *
 * <pre>{@code
 * pipe.moveCamera(CameraMove.to(1800, 2400).zoom(1.2).over(Duration.ofSeconds(2)).easeInOut());
 * pipe.moveCamera(CameraMove.set().angle(0.5));        // only the angle, at once
 * pipe.resetCamera();                                  // the game's default angle, pitch and zoom
 * }</pre>
 *
 * <p>The game accepts camera commands only while the local player observes a match or a replay plays back; otherwise
 * the future of {@link StatsPipe#moveCamera} fails with a {@link NotAllowedException}. Angles and pitch are in radians,
 * as in the {@code Camera} frame.
 */
public final class CameraMove {
    /** The longest transition the game accepts. */
    public static final Duration MAX_TRANSITION = Duration.ofSeconds(60);

    private static final CameraMove EMPTY = new CameraMove(OptionalDouble.empty(), OptionalDouble.empty(), OptionalDouble.empty(), OptionalDouble.empty(), OptionalDouble.empty(), false, Duration.ZERO, false);

    private final OptionalDouble x;
    private final OptionalDouble y;
    private final OptionalDouble zoom;
    private final OptionalDouble angle;
    private final OptionalDouble pitch;
    private final boolean reset;
    private final Duration transition;
    private final boolean easeInOut;

    private CameraMove(OptionalDouble x, OptionalDouble y, OptionalDouble zoom, OptionalDouble angle, OptionalDouble pitch, boolean reset, Duration transition, boolean easeInOut) {
        this.x = x;
        this.y = y;
        this.zoom = zoom;
        this.angle = angle;
        this.pitch = pitch;
        this.reset = reset;
        this.transition = transition;
        this.easeInOut = easeInOut;
    }

    /**
     * Starts a move that changes nothing yet; add the values to change.
     *
     * @return an empty move
     */
    public static CameraMove set() {
        return EMPTY;
    }

    /**
     * Starts a move to a world position.
     *
     * @param x the world x the camera looks at
     * @param y the world y the camera looks at
     * @return the move
     */
    public static CameraMove to(double x, double y) {
        return EMPTY.position(x, y);
    }

    /**
     * Starts a move that puts angle, pitch and zoom back to the game's defaults; the position stays.
     *
     * @return the move
     */
    public static CameraMove reset() {
        return EMPTY.resetFirst();
    }

    /**
     * Returns a copy that also moves to a position. A lock on a unit ends when the move runs.
     *
     * @param x the world x the camera looks at
     * @param y the world y the camera looks at
     * @return the changed copy
     */
    public CameraMove position(double x, double y) {
        requireFinite(x, "x");
        requireFinite(y, "y");
        return new CameraMove(OptionalDouble.of(x), OptionalDouble.of(y), zoom, angle, pitch, reset, transition, easeInOut);
    }

    /**
     * Returns a copy that also sets the zoom.
     *
     * @param zoom the zoom, as the {@code Camera} frame reports it (1.0 is the default); 0.05 to 20
     * @return the changed copy
     */
    public CameraMove zoom(double zoom) {
        requireFinite(zoom, "zoom");
        return new CameraMove(x, y, OptionalDouble.of(zoom), angle, pitch, reset, transition, easeInOut);
    }

    /**
     * Returns a copy that also sets the yaw angle.
     *
     * @param radians the angle in radians; a transition turns the short way round
     * @return the changed copy
     */
    public CameraMove angle(double radians) {
        requireFinite(radians, "angle");
        return new CameraMove(x, y, zoom, OptionalDouble.of(radians), pitch, reset, transition, easeInOut);
    }

    /**
     * Returns a copy that also sets the pitch.
     *
     * @param radians the pitch in radians; the game clamps it to its own limits
     * @return the changed copy
     */
    public CameraMove pitch(double radians) {
        requireFinite(radians, "pitch");
        return new CameraMove(x, y, zoom, angle, OptionalDouble.of(radians), reset, transition, easeInOut);
    }

    /**
     * Returns a copy that first resets angle, pitch and zoom to the game's defaults, then applies the other values.
     *
     * @return the changed copy
     */
    public CameraMove resetFirst() {
        return new CameraMove(x, y, zoom, angle, pitch, true, transition, easeInOut);
    }

    /**
     * Returns a copy that spreads the change over a transition of wall-clock time instead of applying it at once.
     *
     * @param duration the length, zero for at once, at most {@link #MAX_TRANSITION}; whole milliseconds are sent
     * @return the changed copy
     */
    public CameraMove over(Duration duration) {
        Objects.requireNonNull(duration, "duration");
        if (duration.isNegative() || duration.compareTo(MAX_TRANSITION) > 0) {
            throw new IllegalArgumentException("a transition lasts 0 to " + MAX_TRANSITION.toSeconds() + " seconds");
        }
        return new CameraMove(x, y, zoom, angle, pitch, reset, duration, easeInOut);
    }

    /**
     * Returns a copy whose transition starts and ends slowly instead of running at constant speed.
     *
     * @return the changed copy
     */
    public CameraMove easeInOut() {
        return new CameraMove(x, y, zoom, angle, pitch, reset, transition, true);
    }

    /**
     * Tells whether this move changes anything.
     *
     * @return false for {@link #set()} with nothing added
     */
    public boolean isEmpty() {
        return !reset && x.isEmpty() && zoom.isEmpty() && angle.isEmpty() && pitch.isEmpty();
    }

    /**
     * Returns the target x.
     *
     * @return the world x, empty when the position stays
     */
    public OptionalDouble x() {
        return x;
    }

    /**
     * Returns the target y.
     *
     * @return the world y, empty when the position stays
     */
    public OptionalDouble y() {
        return y;
    }

    /**
     * Returns the target zoom.
     *
     * @return the zoom, empty when it stays
     */
    public OptionalDouble zoom() {
        return zoom;
    }

    /**
     * Returns the target angle.
     *
     * @return the angle in radians, empty when it stays
     */
    public OptionalDouble angle() {
        return angle;
    }

    /**
     * Returns the target pitch.
     *
     * @return the pitch in radians, empty when it stays
     */
    public OptionalDouble pitch() {
        return pitch;
    }

    /**
     * Tells whether angle, pitch and zoom go back to the defaults first.
     *
     * @return true for a reset
     */
    public boolean resets() {
        return reset;
    }

    /**
     * Returns the length of the transition.
     *
     * @return the duration, zero for none
     */
    public Duration transition() {
        return transition;
    }

    /**
     * Tells whether the transition eases in and out.
     *
     * @return true for slow start and end, false for constant speed
     */
    public boolean easesInOut() {
        return easeInOut;
    }

    /**
     * Builds the wire query.
     *
     * @return the {@code CameraControlQuery} this move stands for
     */
    public CameraControlQuery toQuery() {
        CameraControlQuery.Builder q = CameraControlQuery.newBuilder().setReset(reset);
        x.ifPresent(v -> q.setX((float) v));
        y.ifPresent(v -> q.setY((float) v));
        zoom.ifPresent(v -> q.setZoom((float) v));
        angle.ifPresent(v -> q.setAngle((float) v));
        pitch.ifPresent(v -> q.setPitch((float) v));
        q.setDurationMs((int) Math.min(transition.toMillis(), MAX_TRANSITION.toMillis()));
        q.setEasing(easeInOut ? CameraEasing.CAMERA_EASING_EASE_IN_OUT : CameraEasing.CAMERA_EASING_NONE);
        return q.build();
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof CameraMove)) {
            return false;
        }
        CameraMove m = (CameraMove) o;
        return x.equals(m.x) && y.equals(m.y) && zoom.equals(m.zoom) && angle.equals(m.angle) && pitch.equals(m.pitch)
            && reset == m.reset && transition.equals(m.transition) && easeInOut == m.easeInOut;
    }

    @Override
    public int hashCode() {
        return Objects.hash(x, y, zoom, angle, pitch, reset, transition, easeInOut);
    }

    @Override
    public String toString() {
        return "CameraMove[x=" + x + ", y=" + y + ", zoom=" + zoom + ", angle=" + angle + ", pitch=" + pitch + ", reset=" + reset
            + ", transition=" + transition + ", easeInOut=" + easeInOut + "]";
    }

    private static void requireFinite(double value, String name) {
        if (!Double.isFinite(value)) {
            throw new IllegalArgumentException(name + " must be a finite number");
        }
    }
}
