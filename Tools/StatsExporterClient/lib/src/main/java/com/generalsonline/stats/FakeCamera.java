package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.CameraControlQuery;
import com.generalsonline.stats.proto.CameraEasing;
import com.generalsonline.stats.proto.CameraFollowEnd;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraFollowQuery;
import com.generalsonline.stats.proto.CameraState;
import com.generalsonline.stats.proto.ResponseStatus;

import java.util.function.IntFunction;

/**
 * The camera of the fake game: position, zoom, angle and pitch, transitions on wall-clock time, a lock on a unit and
 * the "who moved it" bookkeeping of the {@code Camera} frame. One per game: the game has one camera, shared by every
 * reader. The view box is a trapezoid of a fixed size scaled by the zoom, not a projection of real terrain.
 */
final class FakeCamera {
    /** Why a command was refused. */
    record Refusal(ResponseStatus status, String message) {
        static Refusal bad(String message) {
            return new Refusal(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, message);
        }
    }

    static final float DEFAULT_ZOOM = 1.0f;
    static final float DEFAULT_ANGLE = 0.0f;
    static final float DEFAULT_PITCH = 0.75f;

    private static final float FAR_HALF_WIDTH = 760f;
    private static final float NEAR_HALF_WIDTH = 480f;
    private static final float HALF_DEPTH = 330f;

    private float x;
    private float y;
    private float zoom = DEFAULT_ZOOM;
    private float angle = DEFAULT_ANGLE;
    private float pitch = DEFAULT_PITCH;
    private CameraFollowMode followMode = CameraFollowMode.CAMERA_FOLLOW_NONE;
    private int followId;
    private float tether;
    private boolean commanded;
    private long userCount;

    private boolean transition;
    private float[] from = new float[5];
    private float[] to = new float[5];
    private long startNanos;
    private long durationNanos;
    private boolean ease;

    private boolean dirty = true;
    private boolean pipeMoved;
    private boolean userMoved;
    private CameraFollowEnd ended = CameraFollowEnd.CAMERA_FOLLOW_END_NONE;
    private int endedId;
    private boolean lastTransition;

    /** True once a command moved the camera: from then on recorded camera frames are not forwarded. */
    synchronized boolean commanded() {
        return commanded;
    }

    /** Sets the starting view from a recorded camera frame (before any command). */
    synchronized void adopt(Camera recorded) {
        if (!commanded) {
            x = recorded.getX();
            y = recorded.getY();
            zoom = recorded.getZoom() > 0 ? recorded.getZoom() : DEFAULT_ZOOM;
            angle = recorded.getAngle();
            pitch = recorded.getPitch() > 0 ? recorded.getPitch() : DEFAULT_PITCH;
        }
    }

    /** Sets the starting position when the dump has no camera of its own. */
    synchronized void center(float cx, float cy) {
        if (!commanded && x == 0 && y == 0) {
            x = cx;
            y = cy;
        }
    }

    synchronized CameraState state() {
        return CameraState.newBuilder().setX(x).setY(y).setZoom(zoom).setAngle(angle).setPitch(pitch).setFollowMode(followMode)
            .setFollowObjectId(followId).setTransitionActive(transition).build();
    }

    synchronized boolean following() {
        return followId != 0;
    }

    /** A human moved the camera: ends a transition and a lock, and shows as USER in the next frame. */
    synchronized void userInput() {
        userCount++;
        commanded = true;
        userMoved = true;
        dirty = true;
        transition = false;
        if (followId != 0) {
            endFollow(CameraFollowEnd.CAMERA_FOLLOW_END_USER);
        }
    }

    /** Applies a camera command; returns null, or the refusal (the camera is unchanged then). */
    synchronized Refusal control(CameraControlQuery q, long nowNanos) {
        if (q.getDurationMs() > 60000) {
            return Refusal.bad("duration_ms must be 0 to 60000");
        }
        if (q.hasX() != q.hasY()) {
            return Refusal.bad("x and y are set together or not at all");
        }
        if (q.hasX() && (!finite(q.getX(), 1_000_000f) || !finite(q.getY(), 1_000_000f))) {
            return Refusal.bad("x and y must be finite and within a million");
        }
        if (q.hasZoom() && (!finite(q.getZoom(), 20f) || q.getZoom() < 0.05f)) {
            return Refusal.bad("zoom must be 0.05 to 20");
        }
        if (q.hasAngle() && !finite(q.getAngle(), 1e6f)) {
            return Refusal.bad("angle must be finite");
        }
        if (q.hasPitch() && !finite(q.getPitch(), 1e6f)) {
            return Refusal.bad("pitch must be finite");
        }
        if (q.getCancelTransition()) {
            transition = false;
        }
        float[] now = current(nowNanos);
        float[] target = now.clone();
        if (q.getReset()) {
            target[2] = DEFAULT_ZOOM;
            target[3] = DEFAULT_ANGLE;
            target[4] = DEFAULT_PITCH;
        }
        if (q.hasX()) {
            target[0] = q.getX();
            target[1] = q.getY();
            if (followId != 0) {
                endFollow(CameraFollowEnd.CAMERA_FOLLOW_END_COMMAND);
            }
        }
        if (q.hasZoom()) {
            target[2] = q.getZoom();
        }
        if (q.hasAngle()) {
            target[3] = q.getAngle();
        }
        if (q.hasPitch()) {
            target[4] = Math.max(0.1f, Math.min(1.45f, q.getPitch()));
        }
        commanded = true;
        pipeMoved = true;
        dirty = true;
        if (q.getDurationMs() > 0) {
            apply(now);
            from = now;
            to = target;
            to[3] = from[3] + wrap(to[3] - from[3]);
            startNanos = nowNanos;
            durationNanos = q.getDurationMs() * 1_000_000L;
            ease = q.getEasing() == CameraEasing.CAMERA_EASING_EASE_IN_OUT;
            transition = true;
        } else {
            transition = false;
            apply(target);
        }
        return null;
    }

    /**
     * Applies a follow command.
     *
     * @param positionOf the world position {x, y} of an object, or null when it does not exist
     * @return null, or the refusal
     */
    synchronized Refusal follow(CameraFollowQuery q, IntFunction<float[]> positionOf, long nowNanos) {
        int id = q.getObjectId();
        if (id != 0 && positionOf.apply(id) == null) {
            return new Refusal(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no such object: " + id);
        }
        if (id != 0 && q.getMode() == CameraFollowMode.CAMERA_FOLLOW_TETHER && !(q.getTetherDistance() > 0 && q.getTetherDistance() <= 100)) {
            return Refusal.bad("tether_distance must be above 0 and at most 100");
        }
        if (q.getMode() == CameraFollowMode.UNRECOGNIZED) {
            return Refusal.bad("unknown follow mode");
        }
        commanded = true;
        pipeMoved = true;
        dirty = true;
        transition = false;
        if (id == 0) {
            if (followId != 0) {
                endFollow(CameraFollowEnd.CAMERA_FOLLOW_END_COMMAND);
            }
            return null;
        }
        if (followId != 0 && followId != id) {
            endFollow(CameraFollowEnd.CAMERA_FOLLOW_END_COMMAND);
        }
        followId = id;
        followMode = q.getMode() == CameraFollowMode.CAMERA_FOLLOW_TETHER ? CameraFollowMode.CAMERA_FOLLOW_TETHER : CameraFollowMode.CAMERA_FOLLOW_FOLLOW;
        tether = q.getTetherDistance();
        if (followMode == CameraFollowMode.CAMERA_FOLLOW_FOLLOW && q.getSnap()) {
            float[] at = positionOf.apply(id);
            if (!Float.isNaN(at[0])) {
                x = at[0];
                y = at[1];
            }
        }
        return null;
    }

    private void endFollow(CameraFollowEnd why) {
        ended = why;
        endedId = followId;
        followId = 0;
        followMode = CameraFollowMode.CAMERA_FOLLOW_NONE;
        dirty = true;
    }

    private float[] current(long nowNanos) {
        if (transition) {
            tickTransition(nowNanos);
        }
        return new float[] {x, y, zoom, angle, pitch};
    }

    private void apply(float[] v) {
        x = v[0];
        y = v[1];
        zoom = v[2];
        angle = v[3];
        pitch = v[4];
    }

    private void tickTransition(long nowNanos) {
        double t = durationNanos <= 0 ? 1.0 : Math.min(1.0, Math.max(0.0, (nowNanos - startNanos) / (double) durationNanos));
        double k = ease ? t * t * (3.0 - 2.0 * t) : t;
        float[] v = new float[5];
        for (int i = 0; i < 5; i++) {
            v[i] = (float) (from[i] + (to[i] - from[i]) * k);
        }
        apply(v);
        if (t >= 1.0) {
            transition = false;
        }
        dirty = true;
        pipeMoved = true;
    }

    /**
     * Advances the camera and builds the frame to send when something changed since the last one.
     *
     * @param positionOf positions of objects, null for one that is gone
     * @param timestampMicros the envelope clock to put in the frame
     * @param logicFrame the logic frame the game is at
     * @return the frame, or null when nothing changed
     */
    synchronized Camera tick(long nowNanos, IntFunction<float[]> positionOf, long timestampMicros, long logicFrame) {
        if (transition) {
            tickTransition(nowNanos);
        }
        if (followId != 0) {
            float[] at = positionOf.apply(followId);
            if (at == null) {
                endFollow(CameraFollowEnd.CAMERA_FOLLOW_END_OBJECT_GONE);
            } else if (Float.isNaN(at[0])) {
                // the unit exists but the dump never said where: the camera stays
            } else if (followMode == CameraFollowMode.CAMERA_FOLLOW_FOLLOW) {
                dirty |= at[0] != x || at[1] != y;
                x = at[0];
                y = at[1];
            } else {
                float pull = Math.max(0.02f, Math.min(1f, tether / 100f));
                float nx = x + (at[0] - x) * pull;
                float ny = y + (at[1] - y) * pull;
                dirty |= nx != x || ny != y;
                x = nx;
                y = ny;
            }
        }
        if (!dirty && lastTransition == transition) {
            return null;
        }
        return frame(timestampMicros, logicFrame);
    }

    /** The camera frame as it is now, sent whether it changed or not (a new reader, the camera feed switched on). */
    synchronized Camera frame(long timestampMicros, long logicFrame) {
        CameraChangeSource source = userMoved ? CameraChangeSource.CAMERA_CHANGE_USER
            : pipeMoved ? CameraChangeSource.CAMERA_CHANGE_PIPE : CameraChangeSource.CAMERA_CHANGE_OTHER;
        float cos = (float) Math.cos(angle);
        float sin = (float) Math.sin(angle);
        float depth = HALF_DEPTH * zoom * (pitch / DEFAULT_PITCH);
        float[][] corners = {
            {-FAR_HALF_WIDTH * zoom, depth}, {FAR_HALF_WIDTH * zoom, depth}, {NEAR_HALF_WIDTH * zoom, -depth}, {-NEAR_HALF_WIDTH * zoom, -depth}};
        Camera.Builder b = Camera.newBuilder().setTimestampUs(timestampMicros).setLogicFrame((int) logicFrame).setX(x).setY(y).setZoom(zoom).setAngle(angle)
            .setPitch(pitch).setHeightAboveGround(300f * zoom).setHasViewBox(true).setChangeSource(source).setUserInputCount((int) userCount)
            .setFollowMode(followMode).setFollowObjectId(followId).setFollowEnded(ended).setFollowEndedObjectId(endedId).setTransitionActive(transition);
        float[] wx = new float[4];
        float[] wy = new float[4];
        for (int i = 0; i < 4; i++) {
            wx[i] = x + corners[i][0] * cos - corners[i][1] * sin;
            wy[i] = y + corners[i][0] * sin + corners[i][1] * cos;
        }
        b.setViewTlX(wx[0]).setViewTlY(wy[0]).setViewTrX(wx[1]).setViewTrY(wy[1]).setViewBrX(wx[2]).setViewBrY(wy[2]).setViewBlX(wx[3]).setViewBlY(wy[3]);
        dirty = false;
        pipeMoved = false;
        userMoved = false;
        ended = CameraFollowEnd.CAMERA_FOLLOW_END_NONE;
        endedId = 0;
        lastTransition = transition;
        return b.build();
    }

    private static boolean finite(float value, float limit) {
        return Float.isFinite(value) && Math.abs(value) <= limit;
    }

    private static float wrap(float radians) {
        double twoPi = Math.PI * 2;
        double r = radians % twoPi;
        if (r > Math.PI) {
            r -= twoPi;
        } else if (r < -Math.PI) {
            r += twoPi;
        }
        return (float) r;
    }
}
