package com.generalsonline.stats.app;

import com.generalsonline.stats.Director;
import com.generalsonline.stats.DirectorConfig;
import com.generalsonline.stats.DirectorLogic;
import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.DirectorMove;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsPipe;

import java.util.Objects;
import java.util.function.Supplier;

/**
 * Owns the {@link Director} of the window: starting it on the connection, stopping it, changing its tunables while it runs,
 * and describing what it is doing for the Director tab. No Swing.
 */
final class DirectorModel {
    /**
     * What the Director tab shows.
     */
    static final class View {
        private final boolean running;
        private final DirectorLogic.State state;
        private final DirectorMode mode;
        private final String target;
        private final String reason;
        private final int pending;
        private final String problem;

        View(boolean running, DirectorLogic.State state, DirectorMode mode, String target, String reason, int pending, String problem) {
            this.running = running;
            this.state = state;
            this.mode = mode;
            this.target = target;
            this.reason = reason;
            this.pending = pending;
            this.problem = problem;
        }

        boolean running() {
            return running;
        }

        DirectorLogic.State state() {
            return state;
        }

        DirectorMode mode() {
            return mode;
        }

        String target() {
            return target;
        }

        String reason() {
            return reason;
        }

        int pending() {
            return pending;
        }

        String problem() {
            return problem;
        }

        /** True while it stepped back because a human moved the camera. */
        boolean pausedByUser() {
            return state == DirectorLogic.State.PAUSED_BY_USER;
        }

        /** True while it was paused with {@code pause()}. */
        boolean paused() {
            return state == DirectorLogic.State.PAUSED;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof View)) {
                return false;
            }
            View other = (View) o;
            return running == other.running
                    && Objects.equals(state, other.state)
                    && Objects.equals(mode, other.mode)
                    && Objects.equals(target, other.target)
                    && Objects.equals(reason, other.reason)
                    && pending == other.pending
                    && Objects.equals(problem, other.problem);
        }

        @Override
        public int hashCode() {
            return Objects.hash(running, state, mode, target, reason, pending, problem);
        }

        @Override
        public String toString() {
            return "View[" + "running=" + running + ", state=" + state + ", mode=" + mode + ", target=" + target + ", reason=" + reason + ", pending=" + pending + ", problem=" + problem + "]";
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
