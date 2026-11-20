package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerGrid;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * An automatic camera director for a caster: it watches a {@link MatchState} and steers the game's camera with
 * {@link CameraMove}s, following the action ({@link DirectorMode}). The decisions are made by a {@link DirectorLogic};
 * this class feeds it from the state, switches the interest feed on, and sends its moves.
 *
 * <pre>{@code
 * MatchState state = new MatchState();
 * try (StatsPipe pipe = StatsPipe.builder().state(state).build().start();
 *      Director director = Director.start(pipe, state, DirectorConfig.defaults())) {
 *     // casting...
 * }
 * }</pre>
 *
 * <p>The game only accepts camera commands while the local player observes a match or plays back a replay. When it
 * refuses, the director pauses itself and {@link #lastError()} holds the {@link NotAllowedException}; call
 * {@link #resume()} to try again. When a human moves the camera, the director steps back and takes over again after the
 * {@link DirectorConfig#resumeDelay() resume delay}.
 *
 * <p>Thread safe. It uses one daemon thread for its timer, and {@link #close()} stops it.
 */
public final class Director implements AutoCloseable {
    /** How a director reaches the game. {@link #forPipe} is the real one; a test supplies its own. */
    public interface Driver {
        /**
         * Switches the interest feed on for this reader. Called when the director starts and after every reconnect.
         *
         * @return completes when the game accepted it
         */
        CompletableFuture<?> enableFeed();

        /**
         * Moves the camera.
         *
         * @param move the decision
         * @return completes when the game accepted it; a failure with a {@link NotAllowedException} pauses the director
         */
        CompletableFuture<?> move(DirectorMove move);
    }

    /** How often the timer lets the director look at the clock. */
    private static final long TICK_MILLIS = 250;

    private final Driver driver;
    private final MatchState state;
    private final DirectorClock clock;
    private final ScheduledExecutorService timer;
    private final boolean ownsTimer;
    private final DirectorLogic logic;
    private final Object lock = new Object();
    private final StateListener listener = new Feed();

    // Guarded by lock.
    private boolean started;
    private boolean closed;
    private boolean feedPending;
    private Throwable lastError;
    private DirectorMove lastMove;

    /**
     * Creates a director on the real clock with its own timer. Nothing happens until {@link #start()}.
     *
     * @param driver how to reach the game, usually {@link #forPipe}
     * @param state the state to watch
     * @param config the tunables and the starting mode
     */
    public Director(Driver driver, MatchState state, DirectorConfig config) {
        this(driver, state, config, DirectorClock.system(), Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "stats-director");
            t.setDaemon(true);
            return t;
        }), true);
    }

    /**
     * Creates a director with a clock and a timer of the caller's, for tests and hosts with a scheduler of their own.
     *
     * @param driver how to reach the game
     * @param state the state to watch
     * @param config the tunables and the starting mode
     * @param clock the time source
     * @param timer the scheduler for the periodic look at the clock, or null for none: then call {@link #tick()}
     */
    public Director(Driver driver, MatchState state, DirectorConfig config, DirectorClock clock, ScheduledExecutorService timer) {
        this(driver, state, config, clock, timer, false);
    }

    private Director(Driver driver, MatchState state, DirectorConfig config, DirectorClock clock, ScheduledExecutorService timer, boolean ownsTimer) {
        this.driver = Objects.requireNonNull(driver, "driver");
        this.state = Objects.requireNonNull(state, "state");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.timer = timer;
        this.ownsTimer = ownsTimer;
        this.logic = new DirectorLogic(Objects.requireNonNull(config, "config"));
    }

    /**
     * Creates a director on a pipe and starts it.
     *
     * @param pipe the connection that steers the camera
     * @param state the state that connection feeds
     * @param config the tunables and the starting mode
     * @return the running director
     */
    public static Director start(StatsPipe pipe, MatchState state, DirectorConfig config) {
        Director director = new Director(forPipe(pipe), state, config);
        director.start();
        return director;
    }

    /**
     * Returns a driver that talks to a game through a pipe.
     *
     * @param pipe the connection
     * @return a driver that enables the interest feed with a {@code FeedConfigQuery} and moves the camera with {@link StatsPipe#moveCamera}
     */
    public static Driver forPipe(StatsPipe pipe) {
        Objects.requireNonNull(pipe, "pipe");
        return new Driver() {
            @Override
            public CompletableFuture<?> enableFeed() {
                return pipe.setFeedConfig(FeedConfigQuery.newBuilder().setInterest(true).build());
            }

            @Override
            public CompletableFuture<?> move(DirectorMove move) {
                return pipe.moveCamera(move.toCameraMove());
            }
        };
    }

    // -- Life cycle.

    /**
     * Starts watching the state and steering the camera. Calling it again does nothing.
     *
     * @return this director
     */
    public Director start() {
        synchronized (lock) {
            if (started || closed) {
                return this;
            }
            started = true;
            feedPending = true;
        }
        state.addListener(listener);
        if (timer != null) {
            timer.scheduleWithFixedDelay(this::tickGuarded, TICK_MILLIS, TICK_MILLIS, TimeUnit.MILLISECONDS);
        }
        return this;
    }

    @Override
    public void close() {
        synchronized (lock) {
            if (closed) {
                return;
            }
            closed = true;
        }
        state.removeListener(listener);
        if (timer != null && ownsTimer) {
            timer.shutdownNow();
        }
    }

    // -- Control.

    /**
     * Changes the mode.
     *
     * @param mode the new mode
     */
    public void setMode(DirectorMode mode) {
        synchronized (lock) {
            logic.setMode(mode);
        }
    }

    /**
     * Returns the mode in force.
     *
     * @return the mode
     */
    public DirectorMode mode() {
        synchronized (lock) {
            return logic.mode();
        }
    }

    /**
     * Replaces the tunables.
     *
     * @param config the new configuration, including the mode
     */
    public void setConfig(DirectorConfig config) {
        synchronized (lock) {
            logic.setConfig(config);
        }
    }

    /**
     * Returns the tunables in force.
     *
     * @return the configuration
     */
    public DirectorConfig config() {
        synchronized (lock) {
            return logic.config();
        }
    }

    /** Stops steering the camera until {@link #resume()}. */
    public void pause() {
        synchronized (lock) {
            logic.pause();
        }
    }

    /** Steers again at once, also after a user took the camera or the game refused a command. */
    public void resume() {
        synchronized (lock) {
            lastError = null;
            logic.resume(clock.millis());
        }
        tick();
    }

    /**
     * Describes what the director is doing.
     *
     * @return the status as of the last look at the clock
     */
    public DirectorLogic.Status status() {
        synchronized (lock) {
            return logic.status();
        }
    }

    /**
     * Returns the last problem.
     *
     * @return the {@link NotAllowedException} of a refused command, or the failure of a request, empty when all is well
     */
    public Optional<Throwable> lastError() {
        synchronized (lock) {
            return Optional.ofNullable(lastError);
        }
    }

    /**
     * Returns the last move the director sent, with the reason for it.
     *
     * @return the move ({@link DirectorMove#reason()} says why it went there), empty before the first one
     */
    public Optional<DirectorMove> lastMove() {
        synchronized (lock) {
            return Optional.ofNullable(lastMove);
        }
    }

    /**
     * Looks at the clock once and sends what is due. The timer does this four times a second; a host that created the
     * director without a timer calls it itself.
     */
    public void tick() {
        List<DirectorMove> moves;
        boolean enable;
        synchronized (lock) {
            if (!started || closed) {
                return;
            }
            long now = clock.millis();
            if (logic.mode() == DirectorMode.OVERVIEW || logic.mode() == DirectorMode.DIRECTOR) {
                logic.setOverviewSpots(overviewSpots());
            }
            moves = logic.poll(now);
            enable = feedPending && state.hello().isPresent();
            if (enable) {
                feedPending = false;
            }
        }
        if (enable) {
            enableFeed();
        }
        for (DirectorMove move : moves) {
            send(move);
        }
    }

    private void tickGuarded() {
        try {
            tick();
        } catch (RuntimeException e) {
            synchronized (lock) {
                lastError = e;
            }
        }
    }

    private void enableFeed() {
        try {
            driver.enableFeed().whenComplete((ok, error) -> {
                if (error != null) {
                    synchronized (lock) {
                        feedPending = true;
                        lastError = unwrap(error);
                    }
                }
            });
        } catch (RuntimeException e) {
            synchronized (lock) {
                feedPending = true;
                lastError = e;
            }
        }
    }

    private void send(DirectorMove move) {
        synchronized (lock) {
            lastMove = move;
        }
        try {
            driver.move(move).whenComplete((ok, error) -> {
                if (error != null) {
                    Throwable cause = unwrap(error);
                    synchronized (lock) {
                        lastError = cause;
                        if (cause instanceof NotAllowedException) {
                            logic.pause();
                        }
                    }
                }
            });
        } catch (RuntimeException e) {
            synchronized (lock) {
                lastError = e;
            }
        }
    }

    private static Throwable unwrap(Throwable error) {
        return error instanceof CompletionException && error.getCause() != null ? error.getCause() : error;
    }

    // -- Reading the state.

    /** The bases and armies of the players still in the match, in player order. */
    private List<DirectorLogic.Spot> overviewSpots() {
        List<DirectorLogic.Spot> spots = new ArrayList<>();
        List<PlayerGrid> grids = state.grids();
        WorldExtent extent = state.worldExtent().orElse(null);
        for (PlayerView player : state.players()) {
            MatchPlayer info = player.info();
            if (player.life() != PlayerLife.ALIVE || info.getIsObserver()) {
                continue;
            }
            if (info.getHasStartLocation()) {
                spots.add(new DirectorLogic.Spot(player.playerIndex(), info.getStartX(), info.getStartY(), DirectorLogic.Spot.Kind.BASE));
            }
            for (PlayerGrid grid : grids) {
                if (grid.getPlayerIndex() != player.playerIndex() || extent == null) {
                    continue;
                }
                double sumX = 0.0;
                double sumY = 0.0;
                double weight = 0.0;
                int width = grid.getWidth();
                for (int i = 0; i < grid.getCellsCount() && width > 0; i++) {
                    double count = Integer.toUnsignedLong(grid.getCells(i));
                    sumX += count * (extent.minX() + (i % width + 0.5) * grid.getCellWidth());
                    sumY += count * (extent.minY() + (i / width + 0.5) * grid.getCellHeight());
                    weight += count;
                }
                boolean awayFromBase = !info.getHasStartLocation() || Math.hypot(sumX / weight - info.getStartX(), sumY / weight - info.getStartY()) > 600.0;
                if (weight > 0.0 && awayFromBase) {
                    spots.add(new DirectorLogic.Spot(player.playerIndex(), sumX / weight, sumY / weight, DirectorLogic.Spot.Kind.ARMY));
                }
            }
        }
        return spots;
    }

    /** Feeds the logic from the state's change callbacks. */
    private final class Feed implements StateListener {
        @Override
        public void interestMapChanged(InterestMap map) {
            synchronized (lock) {
                logic.onInterest(map, clock.millis(), state.logicFps(), state.gameSpeedRatio());
            }
            tickGuarded();
        }

        @Override
        public void cameraChanged(Camera camera) {
            synchronized (lock) {
                logic.onCamera(camera, clock.millis());
            }
        }

        @Override
        public void matchReset() {
            synchronized (lock) {
                logic.reset();
            }
        }

        @Override
        public void helloReceived(Hello hello) {
            synchronized (lock) {
                feedPending = true;
            }
        }
    }
}
