package com.generalsonline.stats.internal;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.StatsSnapshot;

/**
 * Paces a replayed dump by game time. The first game frame seen anchors the wall clock; a frame N logic frames later
 * is due N / logicFps / speed seconds after it. The replay thread sleeps for {@link #delayNanos} only when it is
 * ahead of that schedule, so a slow consumer is never slowed further. Not part of the public API.
 */
public final class ReplayPacer {
    private final double speed;
    private int logicFps;
    private long anchorFrame = -1;
    private long anchorNanos;
    private long lastFrame = -1;

    /**
     * Creates a pacer.
     *
     * @param speed 1.0 is real time, 2.0 twice as fast; 0 or less plays as fast as possible
     * @param logicFps the logic rate to assume until a MatchInfo says otherwise
     */
    public ReplayPacer(double speed, int logicFps) {
        this.speed = speed;
        this.logicFps = logicFps;
    }

    /**
     * Sets the build's logic rate, from MatchInfo.
     *
     * @param fps frames per second; 0 or less means unknown and keeps the current rate
     */
    public void setLogicFps(int fps) {
        if (fps > 0) {
            logicFps = fps;
        }
    }

    /**
     * How long to wait before delivering a frame that belongs to this game frame. A game frame that jumps back by
     * more than a second starts a new match in the dump and re-anchors the clock.
     *
     * @param gameFrame the logic frame the frame belongs to, or -1 for a frame without game time
     * @param nowNanos the current time, for example {@link System#nanoTime()}
     * @return nanoseconds to sleep; 0 when due or late, or for a frame with no game time
     */
    public long delayNanos(long gameFrame, long nowNanos) {
        if (speed <= 0 || gameFrame < 0) {
            return 0;
        }
        if (anchorFrame < 0 || gameFrame + logicFps < lastFrame) {
            anchorFrame = gameFrame;
            anchorNanos = nowNanos;
            lastFrame = gameFrame;
            return 0;
        }
        lastFrame = Math.max(lastFrame, gameFrame);
        double dueSeconds = (gameFrame - anchorFrame) / (double) logicFps / speed;
        return Math.max(0, anchorNanos + (long) (dueSeconds * 1e9) - nowNanos);
    }

    /**
     * Finds the game frame a frame belongs to: snapshots and events carry one, the rest do not.
     *
     * @param frame the decoded frame
     * @return the logic frame, or -1
     */
    public static long gameFrameOf(DecodedFrame frame) {
        if (frame.message() instanceof StatsSnapshot) {
            return Format.u(((StatsSnapshot) frame.message()).getFrame());
        }
        if (frame.message() instanceof Events) {
            return Format.u(((Events) frame.message()).getFrame());
        }
        return -1;
    }
}
