package com.generalsonline.stats.app;

import com.generalsonline.stats.Director;
import com.generalsonline.stats.DirectorConfig;
import com.generalsonline.stats.DirectorLogic;
import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.DirectorMove;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsPipe;

import java.util.function.Supplier;

/**
 * Owns the {@link Director} of the window: starting it on the connection, stopping it, changing its tunables while it runs,
 * and describing what it is doing for the Director tab. No Swing.
 */
final class DirectorModel {
    /**
     * What the Director tab shows.
     *
     * @param running a director exists
     * @param state what it is doing, or WAITING while none runs
     * @param mode the mode in force
     * @param target "x, y" of the last move, or empty
     * @param reason why it went there (HOTSPOT, HIGHLIGHT, RETURN, OVERVIEW), or empty
     * @param pending highlights it knows of and has not shown yet
     * @param problem the last error text, or empty
     */
    record View(boolean running, DirectorLogic.State state, DirectorMode mode, String target, String reason, int pending, String problem) {
        /** True while it stepped back because a human moved the camera. */
        boolean pausedByUser() {
            return state == DirectorLogic.State.PAUSED_BY_USER;
        }

        /** True while it was paused with {@code pause()}. */
        boolean paused() {
            return state == DirectorLogic.State.PAUSED;
        }
    }

    private final MatchState state;
    private final Supplier<StatsPipe> pipe;
    private Director director;
    private DirectorConfig config = DirectorConfig.defaults();

    DirectorModel(MatchState state, Supplier<StatsPipe> pipe) {
        this.state = state;
        this.pipe = pipe;
    }

    boolean running() {
        return director != null;
    }

    DirectorConfig config() {
        return config;
    }

    /** Starts steering with the configuration; returns null, or why it cannot. */
    String start(DirectorConfig next) {
        StatsPipe p = pipe.get();
        if (p == null) {
            return "no game is connected; the director steers a game through the pipe (a replay file has none)";
        }
        stop();
        config = next;
        director = Director.start(p, state, next);
        return null;
    }

    void stop() {
        if (director != null) {
            director.close();
            director = null;
        }
    }

    /** Changes the tunables (and the mode) of the director, running or not. */
    void apply(DirectorConfig next) {
        config = next;
        if (director != null) {
            director.setConfig(next);
        }
    }

    void pause() {
        if (director != null) {
            director.pause();
        }
    }

    void resume() {
        if (director != null) {
            director.resume();
        }
    }

    View view() {
        if (director == null) {
            return new View(false, DirectorLogic.State.WAITING, config.mode(), "", "", 0, "");
        }
        DirectorLogic.Status status = director.status();
        DirectorMove move = director.lastMove().orElse(null);
        return new View(true, status.state(), status.mode(), status.hasTarget() ? Math.round(status.targetX()) + ", " + Math.round(status.targetY()) : "",
            move == null ? "" : move.reason() + (move.cut() ? " (a cut)" : " (a pan)"), status.pendingHighlights(),
            director.lastError().map(Format::cause).orElse(""));
    }
}
