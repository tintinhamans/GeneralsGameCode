package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.internal.MemoryLink;
import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.internal.PipeServer;
import com.generalsonline.stats.internal.PowerShellPipeServer;
import com.generalsonline.stats.internal.RequestLimiter;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraControlQuery;
import com.generalsonline.stats.proto.CameraFollowQuery;
import com.generalsonline.stats.proto.CameraState;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.ChatSend;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.LobbyControlQuery;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.ObserveQuery;
import com.generalsonline.stats.proto.ObserveState;
import com.generalsonline.stats.proto.ReplayControlQuery;
import com.generalsonline.stats.proto.ReplayState;
import com.generalsonline.stats.proto.Request;
import com.generalsonline.stats.proto.ResyncResult;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.ViewStats;
import com.google.protobuf.Message;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * A stand-in for a running game, so tools can be written and tested without one: it serves a dump file as the stats pipe
 * of a game. It speaks protocol 2 like the real thing: a {@code Hello} with a reader slot, frames with fresh sequence
 * numbers and timestamps, up to four readers, and answers requests from a model built from the dump.
 *
 * <pre>{@code
 * try (FakeGame game = FakeGame.builder(Path.of("match.dump")).speed(2).loop(true).build().start()) {
 *     // a real Windows named pipe now exists: game.pipePath(); StatsPipe.builder().pid(game.pid()) finds it
 *     StatsPipe pipe = game.connect(StatsPipe.builder()).build().start();   // or connect in-process, no pipe needed
 * }
 * }</pre>
 *
 * <p><b>Transports.</b> {@link #start()} opens a real named pipe {@code \\.\pipe\generalsonline-stats-<pid>} (Windows
 * only; Java cannot create one, so a hidden Windows PowerShell process hosts it and relays the bytes) unless
 * {@link Builder#namedPipe(boolean) namedPipe(false)} asks for none. {@link #connect(StatsPipe.Builder)} connects a
 * {@link StatsPipe} through memory instead, on any system, with the same protocol and the same limits.
 *
 * <p><b>The stream.</b> One clock drives the game, paced by game time at {@link Builder#speed(double) speed} times the
 * replay speed. A reader <b>joins live</b>: it gets the Hello, then (like a reader that connects to a running game) the
 * latest screen, lobby, match and full snapshot, then the frames as the game reaches them. Every reader has its own
 * sequence numbers and feed configuration; a reader that falls {@link Builder#queueCapacityBytes more than its queue
 * holds} behind loses frames (a gap in the sequence). The dump's own Hello and responses are not forwarded. At the end of
 * the dump the stream stops, or starts again with {@link Builder#loop loop}.
 *
 * <p><b>The feed configuration</b> is honored per reader: snapshot cadences (thinning only: a cadence faster than the
 * dump's is not invented), events, chat, camera, view statistics and interest on or off and their intervals, and the
 * grid, supply, tech and production parts of full snapshots. {@code grid_resolution} is accepted and echoed but the
 * grids are the dump's. A dump without {@code ViewStats} or {@code InterestMap} frames gets approximations computed from
 * its snapshots and events (the grids say where armies are, kills and losses where fights were; the numbers are
 * plausible, not exact).
 *
 * <p><b>Requests</b> are answered from what the dump contains: the catalog lists the templates the dump mentions (with the
 * cost, side and kind the events reveal), objects are the ones events and view statistics named (a killed object is gone),
 * players come from the latest snapshot, the map from the match info (with a generated terrain image as minimap, since a
 * dump has none). Questions the dump cannot answer get {@code NOT_FOUND} or {@code UNAVAILABLE}. Camera, follow, replay and
 * observe commands follow the game's rules for the chosen {@link Situation}: a camera command moves a camera model (a
 * transition on wall-clock time, a lock on a unit that follows the positions the dump gives) and shows up in
 * {@code Camera} frames with the change source PIPE; replay pause and speed change the pace of the stream. There is one
 * camera, shared by all readers, as there is one in a game. Requests over the rate limit get {@code RATE_LIMITED}.
 *
 * <p><b>Lobby control</b> works while the dump is in a game setup lobby (a {@code LobbyState} with {@code in_lobby} and no
 * match running; otherwise {@code UNAVAILABLE}). The lobby is modelled by the rules of the setup screens: the local user
 * is the host unless {@link Builder#lobbyLocalSlot} makes it a client, only the host changes slots, map and rules, a
 * client changes its own slot and accepts, and what the screens refuse gets {@code NOT_ALLOWED}. A change is sent to every
 * reader as a new {@code LobbyState} and replaces the dump's lobby for readers that join later, until the dump sends the
 * next one. The model has no map list, no random resolution and no network: any {@code .map} name is a map, and Start
 * only answers.
 *
 * <p>Thread safe; it uses daemon threads and {@link #close()} stops them. This class never prints.
 */
public final class FakeGame implements AutoCloseable {
    /** The process id the fake game uses unless the builder says otherwise. */
    public static final int DEFAULT_PID = 41937;

    /** The most readers a pipe serves at once. */
    public static final int MAX_READERS = 4;

    /** The fastest a replay can be played without fast forward: the game's replay runs at its logic rate at most. */
    public static final double DEFAULT_REPLAY_SPEED_CAP = 1.0;

    private static final int DEFAULT_QUEUE_BYTES = 1 << 20;
    private static final int MAX_PAGE_SIZE = 200;
    private static final long TICK_NANOS = 4_000_000L;
    private static final String[] CAPABILITIES = {
        "frame.sequence", "frame.timestamp", "multi_reader", "chat.send", "request.catalog", "request.object", "request.objects", "request.player",
        "request.map", "request.resync", "request.feed_config", "map.minimap_rgba", "event.object_ids", "event.kill_detail", "feed.per_reader",
        "camera.control", "camera.follow", "camera.source", "replay.control", "observe.player", "view_stats", "interest_map", "status.not_allowed", "lobby.control"};

    /** What the local player of the fake game is doing, which decides what the control commands may do. */
    public enum Situation {
        /** The dump plays back as a replay: camera, follow, observe and replay commands are all accepted. */
        REPLAY,
        /** The local player observes a live match: camera, follow and observe are accepted, replay commands are refused with {@code NOT_ALLOWED}. */
        OBSERVER,
        /** The local player plays: every control command is refused with {@code NOT_ALLOWED}. */
        PLAYER
    }

    private final FakeDump dump;
    private final double speed;
    private final boolean loop;
    private final int pid;
    private final String pipeName;
    private final boolean namedPipe;
    private final int maxReaders;
    private final int requestsPerSecond;
    private final double replaySpeedCap;
    private final Situation situation;
    private final int queueBytes;
    private final boolean holdForReader;
    private final int lobbyLocalSlot;

    private final FakeWorld world;
    private final FakeCamera camera = new FakeCamera();
    private final FakeSynth synth;
    private final long clockStart = System.nanoTime();
    private final long epochMillis = System.currentTimeMillis();
    private final Object lock = new Object();
    private final Session[] slots;
    private final AtomicLong released = new AtomicLong();
    private final AtomicLong droppedTotal = new AtomicLong();
    private final AtomicInteger loops = new AtomicInteger();

    // Guarded by lock.
    private boolean paused;
    private double ratio = 1.0;
    private boolean fastForward;
    private boolean capped;
    private int observed = -1;
    private Camera lastRecordedCamera;

    private volatile boolean readerSeen;
    private volatile boolean started;
    private volatile boolean closed;
    private volatile boolean finished;
    private volatile Throwable failure;
    private Thread clock;
    private PipeServer server;

    private FakeGame(Builder b) throws IOException {
        this.dump = FakeDump.load(b.dump);
        this.speed = b.speed;
        this.loop = b.loop;
        this.pid = b.pid;
        this.pipeName = b.pipeName != null ? b.pipeName : PipeDiscovery.PIPE_PREFIX + b.pid;
        this.namedPipe = b.namedPipe;
        this.maxReaders = b.maxReaders;
        this.requestsPerSecond = b.requestsPerSecond;
        this.replaySpeedCap = b.replaySpeedCap;
        this.situation = b.situation;
        this.queueBytes = b.queueBytes;
        this.holdForReader = b.holdForReader;
        this.lobbyLocalSlot = b.lobbyLocalSlot;
        this.world = new FakeWorld(dump);
        this.synth = new FakeSynth(dump.logicFps);
        this.slots = new Session[maxReaders];
    }

    /**
     * Starts building a fake game that serves a dump.
     *
     * @param dump a dump file written by {@link DumpWriter} (the {@code dump} command of the test client)
     * @return a builder with the defaults: pid {@value #DEFAULT_PID}, real time, no loop, a real named pipe, four readers
     */
    public static Builder builder(Path dump) {
        return new Builder(Objects.requireNonNull(dump, "dump"));
    }

    /** Builds a {@link FakeGame}. Not thread safe. */
    public static final class Builder {
        private final Path dump;
        private double speed = 1.0;
        private boolean loop;
        private int pid = DEFAULT_PID;
        private String pipeName;
        private boolean namedPipe = true;
        private int maxReaders = MAX_READERS;
        private int requestsPerSecond = 20;
        private double replaySpeedCap = DEFAULT_REPLAY_SPEED_CAP;
        private Situation situation = Situation.REPLAY;
        private int queueBytes = DEFAULT_QUEUE_BYTES;
        private boolean holdForReader;
        private int lobbyLocalSlot = -1;

        private Builder(Path dump) {
            this.dump = dump;
        }

        /**
         * Sets the pace of game time.
         *
         * @param speed 1 is real time, 2 twice as fast, 0 as fast as the readers keep up; not negative
         * @return this builder
         */
        public Builder speed(double speed) {
            if (!(speed >= 0) || Double.isInfinite(speed)) {
                throw new IllegalArgumentException("speed must be 0 (as fast as possible) or a positive multiplier, got " + speed);
            }
            this.speed = speed;
            return this;
        }

        /**
         * Chooses whether the stream starts again after the last frame of the dump. Off by default.
         *
         * @param loop true to play the dump over and over
         * @return this builder
         */
        public Builder loop(boolean loop) {
            this.loop = loop;
            return this;
        }

        /**
         * Sets the process id the game claims; it names the pipe and is in the Hello. {@code StatsPipe} finds a game
         * by it.
         *
         * @param pid 0 or more
         * @return this builder
         */
        public Builder pid(int pid) {
            if (pid < 0) {
                throw new IllegalArgumentException("pid must not be negative, got " + pid);
            }
            this.pid = pid;
            return this;
        }

        /**
         * Sets the pipe name instead of one derived from the pid. Such a pipe is found by {@code StatsPipe.builder().pipe(path)}
         * only, not by discovery.
         *
         * @param pipe a full pipe path ({@code \\.\pipe\name}) or just the name; letters, digits, '.', '-' and '_'
         * @return this builder
         */
        public Builder pipe(String pipe) {
            String name = Objects.requireNonNull(pipe, "pipe");
            if (name.startsWith(PipeDiscovery.PIPE_DIRECTORY)) {
                name = name.substring(PipeDiscovery.PIPE_DIRECTORY.length());
            }
            if (!name.matches("[A-Za-z0-9._-]+")) {
                throw new IllegalArgumentException("a pipe name has letters, digits, '.', '-' and '_' only, got '" + pipe + "'");
            }
            this.pipeName = name;
            return this;
        }

        /**
         * Chooses whether {@link FakeGame#start()} opens a real Windows named pipe. On by default; turn it off to serve
         * in-process connections only (any system, no helper process).
         *
         * @param namedPipe false for no named pipe
         * @return this builder
         */
        public Builder namedPipe(boolean namedPipe) {
            this.namedPipe = namedPipe;
            return this;
        }

        /**
         * Sets how many readers are served at once.
         *
         * @param readers 1 to {@value FakeGame#MAX_READERS}; the default is {@value FakeGame#MAX_READERS}
         * @return this builder
         */
        public Builder maxReaders(int readers) {
            if (readers < 1 || readers > MAX_READERS) {
                throw new IllegalArgumentException("readers must be 1 to " + MAX_READERS + ", got " + readers);
            }
            this.maxReaders = readers;
            return this;
        }

        /**
         * Sets the request rate limit announced in the Hello (the burst is twice that) and enforced.
         *
         * @param perSecond requests per second per reader, 0 for no limit; the game's value is 20
         * @return this builder
         */
        public Builder requestsPerSecond(int perSecond) {
            if (perSecond < 0) {
                throw new IllegalArgumentException("the rate must not be negative, got " + perSecond);
            }
            this.requestsPerSecond = perSecond;
            return this;
        }

        /**
         * Sets the fastest replay ratio without fast forward; a faster request is answered with {@code speed_capped}.
         *
         * @param cap more than 0; the game's own limit during a replay is {@value FakeGame#DEFAULT_REPLAY_SPEED_CAP}
         * @return this builder
         */
        public Builder replaySpeedCap(double cap) {
            if (!(cap > 0) || Double.isInfinite(cap)) {
                throw new IllegalArgumentException("the cap must be above 0, got " + cap);
            }
            this.replaySpeedCap = cap;
            return this;
        }

        /**
         * Chooses what the local player is doing, which decides which control commands are refused.
         *
         * @param situation the situation; {@link Situation#REPLAY} by default
         * @return this builder
         */
        public Builder situation(Situation situation) {
            this.situation = Objects.requireNonNull(situation, "situation");
            return this;
        }

        /**
         * Chooses who the local user is in a game setup lobby of the dump, which decides what lobby control may do: the
         * host (the default) changes slots, map and rules; a client changes only its own slot and accepts.
         *
         * @param slot the lobby slot the local user holds as a client, or -1 for the host
         * @return this builder
         */
        public Builder lobbyLocalSlot(int slot) {
            if (slot < -1 || slot >= LobbyControl.MAX_SLOTS) {
                throw new IllegalArgumentException("a lobby slot is 0 to " + (LobbyControl.MAX_SLOTS - 1) + ", or -1 for the host, got " + slot);
            }
            this.lobbyLocalSlot = slot;
            return this;
        }

        /**
         * Sets the outbound queue of each reader (announced in the Hello): a reader further behind than this loses frames.
         *
         * @param bytes at least 1024
         * @return this builder
         */
        public Builder queueCapacityBytes(int bytes) {
            if (bytes < 1024) {
                throw new IllegalArgumentException("the queue must hold at least 1024 bytes, got " + bytes);
            }
            this.queueBytes = bytes;
            return this;
        }

        /**
         * Holds the stream back until the first reader has connected, so that reader sees the dump from its first frame
         * (a reader joins live, and a fast game would otherwise be well under way when a tool connects). Off by default.
         *
         * @param hold true to wait for a reader before the dump starts
         * @return this builder
         */
        public Builder holdForReader(boolean hold) {
            this.holdForReader = hold;
            return this;
        }

        /**
         * Reads the dump and creates the game; call {@link FakeGame#start()} to serve it.
         *
         * @return the game, not started
         * @throws IOException when the dump cannot be read or is damaged
         */
        public FakeGame build() throws IOException {
            return new FakeGame(this);
        }
    }

    // -- Life cycle.

    /**
     * Starts the clock and, unless turned off, opens the named pipe. The pipe exists when this returns.
     *
     * @return this game
     * @throws IOException when the named pipe cannot be created (not Windows, a pipe of that name exists, the helper
     *         did not start)
     * @throws IllegalStateException when already started or closed
     */
    public synchronized FakeGame start() throws IOException {
        if (closed || started) {
            throw new IllegalStateException(closed ? "closed" : "already started");
        }
        if (namedPipe) {
            PipeServer pipe = new PowerShellPipeServer(pipeName, maxReaders);
            pipe.start(this::accept);
            server = pipe;
        }
        started = true;
        clock = new Thread(this::run, "fake-game-clock");
        clock.setDaemon(true);
        clock.start();
        return this;
    }

    /** Stops the game: closes the pipe and every reader. Safe to call more than once. */
    @Override
    public void close() {
        closed = true;
        PipeServer pipe;
        synchronized (this) {
            pipe = server;
        }
        if (pipe != null) {
            try {
                pipe.close();
            } catch (IOException ignored) {
                // already gone
            }
        }
        if (clock != null) {
            clock.interrupt();
        }
        synchronized (lock) {
            for (Session s : slots) {
                if (s != null) {
                    s.close();
                }
            }
        }
    }

    /**
     * Returns the pipe path of this game.
     *
     * @return {@code \\.\pipe\generalsonline-stats-<pid>}, or the path of the pipe name given to the builder
     */
    public String pipePath() {
        return PipeDiscovery.PIPE_DIRECTORY + pipeName;
    }

    /**
     * Returns the process id this game claims.
     *
     * @return the pid of the Hello
     */
    public int pid() {
        return pid;
    }

    /**
     * Makes a {@link StatsPipe} builder connect to this game in memory instead of through the pipe. The game must be
     * started. The pipe path reported by the connection is {@link #pipePath()}, so {@code connectedPid()} works.
     *
     * @param builder the builder to configure
     * @return the same builder
     */
    public StatsPipe.Builder connect(StatsPipe.Builder builder) {
        String path = pipePath();
        return builder.candidates(() -> List.of(path)).opener(p -> connectInProcess());
    }

    PipeLink connectInProcess() throws IOException {
        if (!started || closed) {
            throw new IOException("the fake game is not running");
        }
        PipeLink[] ends = MemoryLink.pair();
        if (!accept(-1, ends[0])) {
            ends[1].close();
            throw new IOException("all " + maxReaders + " reader slots are taken");
        }
        return ends[1];
    }

    /**
     * Counts the readers connected now.
     *
     * @return 0 to the maximum number of readers
     */
    public int readerCount() {
        synchronized (lock) {
            int count = 0;
            for (Session s : slots) {
                if (s != null) {
                    count++;
                }
            }
            return count;
        }
    }

    /**
     * Tells whether the end of the dump was reached (never true with {@link Builder#loop loop}).
     *
     * @return true once the last frame was sent
     */
    public boolean isFinished() {
        return finished;
    }

    /**
     * Waits for the end of the dump.
     *
     * @param timeout the longest wait
     * @return true when the end was reached in time
     * @throws InterruptedException when the waiting thread is interrupted
     */
    public boolean awaitFinished(Duration timeout) throws InterruptedException {
        long deadline = System.nanoTime() + timeout.toNanos();
        while (!finished && System.nanoTime() < deadline) {
            Thread.sleep(2);
        }
        return finished;
    }

    /**
     * Counts the frames of the dump that were released to the readers so far, over all loops.
     *
     * @return the number of dump frames sent
     */
    public long framesReleased() {
        return released.get();
    }

    /**
     * Counts how many times the stream started over.
     *
     * @return 0 until the dump ended once with {@link Builder#loop loop} on
     */
    public int loopsCompleted() {
        return loops.get();
    }

    /**
     * Counts the frames dropped because a reader's queue was full.
     *
     * @return the number of dropped frames over all readers
     */
    public long framesDropped() {
        return droppedTotal.get();
    }

    /**
     * Simulates a human moving the camera: the next {@code Camera} frame has the change source USER and a higher user
     * input count, a transition and a lock end, and recorded camera frames stop. For developing a tool that steps back
     * when a caster takes over (the {@link Director} does).
     */
    public void simulateUserCameraInput() {
        camera.userInput();
    }

    /**
     * Returns the camera as commands left it.
     *
     * @return position, zoom, angle, pitch and lock
     */
    public CameraState cameraState() {
        return camera.state();
    }

    /**
     * Returns the replay state.
     *
     * @return pause, effective speed ratio, fast forward and whether the last requested speed was capped
     */
    public ReplayState replayState() {
        synchronized (lock) {
            return ReplayState.newBuilder().setPaused(paused).setSpeedRatio((float) Math.min(ratio, replaySpeedCap)).setFastForward(fastForward).setSpeedCapped(capped).build();
        }
    }

    /**
     * Returns the feed configuration of a connected reader.
     *
     * @param slot the reader slot, 0 based
     * @return the effective configuration, or null when no reader holds the slot
     */
    public FeedConfig feedConfig(int slot) {
        synchronized (lock) {
            Session s = slot >= 0 && slot < slots.length ? slots[slot] : null;
            return s == null ? null : s.feed.config();
        }
    }

    /**
     * Returns what ended the clock thread unexpectedly.
     *
     * @return the failure, or null when all is well
     */
    public Throwable failure() {
        return failure;
    }

    private long micros() {
        return (System.nanoTime() - clockStart) / 1000;
    }

    // -- Readers.

    boolean accept(int preferred, PipeLink link) {
        Session session;
        synchronized (lock) {
            int slot = -1;
            if (preferred >= 0 && preferred < maxReaders && slots[preferred] == null) {
                slot = preferred;
            }
            for (int i = 0; slot < 0 && i < maxReaders; i++) {
                if (slots[i] == null) {
                    slot = i;
                }
            }
            if (slot < 0 || closed) {
                try {
                    link.close();
                } catch (IOException ignored) {
                    // refused anyway
                }
                return false;
            }
            session = new Session(slot, link);
            slots[slot] = session;
            session.greet();
            readerSeen = true;
        }
        session.startThreads();
        return true;
    }

    /** One connected reader. */
    private final class Session {
        final int slot;
        final PipeLink link;
        final FakeFeed feed = new FakeFeed();
        final RequestLimiter limiter = requestsPerSecond > 0 ? new RequestLimiter(requestsPerSecond, requestsPerSecond * 2.0) : null;
        final BlockingQueue<byte[]> out = new LinkedBlockingQueue<>();
        final AtomicLong queued = new AtomicLong();
        long sequence;
        volatile boolean ended;

        Session(int slot, PipeLink link) {
            this.slot = slot;
            this.link = link;
        }

        synchronized void send(FrameType type, Message message) {
            send(type.id(), FrameCodec.bodyOf(message));
        }

        synchronized void send(int type, byte[] body) {
            long number = sequence++ & RawFrame.MAX_SEQUENCE;
            byte[] frame = FrameCodec.encodeOutbound(type, number, micros(), body);
            if (queued.get() + frame.length > queueBytes && number != 0) {
                droppedTotal.incrementAndGet();
                return;
            }
            queued.addAndGet(frame.length);
            out.add(frame);
        }

        /** The Hello and what a reader joining a running game gets; called with the game lock held. */
        void greet() {
            send(FrameType.HELLO, hello(slot));
            sendJoin(this);
        }

        void startThreads() {
            Thread writer = new Thread(this::writeLoop, "fake-game-writer-" + slot);
            writer.setDaemon(true);
            writer.start();
            Thread reader = new Thread(this::readLoop, "fake-game-reader-" + slot);
            reader.setDaemon(true);
            reader.start();
        }

        private void writeLoop() {
            try {
                while (!ended) {
                    byte[] frame = out.poll(50, TimeUnit.MILLISECONDS);
                    if (frame == null) {
                        continue;
                    }
                    link.write(ByteBuffer.wrap(frame));
                    queued.addAndGet(-frame.length);
                }
            } catch (IOException e) {
                // the reader went away
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } finally {
                close();
            }
        }

        private void readLoop() {
            FrameCodec.Decoder decoder = FrameCodec.Decoder.inbound();
            ByteBuffer buffer = ByteBuffer.allocate(8192);
            try {
                while (!ended) {
                    buffer.clear();
                    int read = link.read(buffer);
                    if (read < 0) {
                        break;
                    }
                    for (RawFrame frame : decoder.push(buffer.array(), 0, read)) {
                        handle(this, frame);
                    }
                }
            } catch (IOException | RuntimeException e) {
                // a broken pipe or a framing error ends this reader only
            } finally {
                close();
            }
        }

        void close() {
            if (ended) {
                return;
            }
            ended = true;
            try {
                link.close();
            } catch (IOException ignored) {
                // closing
            }
            synchronized (lock) {
                if (slots[slot] == this) {
                    slots[slot] = null;
                }
            }
        }
    }

    private Hello hello(int slot) {
        Hello recorded = dump.hello;
        Hello.Builder b = Hello.newBuilder().setProtocolVersion(Handshake.PROTOCOL_VERSION)
            .setGameVersion(recorded == null || recorded.getGameVersion().isEmpty() ? "fake" : recorded.getGameVersion())
            .setGameBuild("FakeGame of stats-pipe-client, dump " + dump.name).setProcessId(pid).setLogicFramesPerSecond(dump.logicFps)
            .setMaxRequestsPerSecond(requestsPerSecond).setMaxRequestBurst(requestsPerSecond * 2).setReaderSlot(slot).setMaxReaders(maxReaders)
            .setEpochUnixMs(epochMillis).setMaxPageSize(MAX_PAGE_SIZE).setQueueCapacityBytes(queueBytes);
        for (String capability : CAPABILITIES) {
            b.addCapabilities(capability);
        }
        return b.build();
    }

    /** What a reader that joins a running game (or asks for a resync) is sent after the Hello. Called with the lock held. */
    private void sendJoin(Session s) {
        for (Message m : world.joinMessages()) {
            s.send(typeOf(m), m);
        }
        if (world.matchRunning()) {
            FeedConfig config = s.feed.config();
            Camera current = currentCamera();
            if (config.getCamera() && current != null) {
                s.send(FrameType.CAMERA, current);
            }
        }
    }

    private Camera currentCamera() {
        if (camera.commanded()) {
            return camera.frame(micros(), world.frame());
        }
        return lastRecordedCamera == null ? null : lastRecordedCamera.toBuilder().setTimestampUs(micros()).build();
    }

    private static FrameType typeOf(Message m) {
        if (m instanceof MatchInfo) {
            return FrameType.MATCH_INFO;
        }
        if (m instanceof StatsSnapshot) {
            return FrameType.STATS_SNAPSHOT;
        }
        if (m instanceof com.generalsonline.stats.proto.UiState) {
            return FrameType.UI_STATE;
        }
        if (m instanceof com.generalsonline.stats.proto.LobbyState) {
            return FrameType.LOBBY_STATE;
        }
        throw new IllegalArgumentException(m.getClass().getName());
    }

    // -- The clock: releases the dump at the pace of game time and ticks the camera and the stand-in frames.

    private void run() {
        try {
            clockLoop();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException e) {
            failure = e;
        }
    }

    private void clockLoop() throws InterruptedException {
        List<FakeDump.Entry> entries = dump.entries;
        int index = 0;
        double playhead = 0;
        long last = System.nanoTime();
        while (!closed) {
            if (holdForReader && !readerSeen) {
                TimeUnit.MILLISECONDS.sleep(2);
                last = System.nanoTime();
                continue;
            }
            long now = System.nanoTime();
            double dt = Math.min(0.1, (now - last) / 1e9);
            last = now;
            boolean unpaced;
            synchronized (lock) {
                double rate = fastForward ? 0 : speed == 0 ? 0 : speed * Math.min(ratio, replaySpeedCap);
                unpaced = rate == 0;
                if (!paused && !unpaced) {
                    playhead += dt * dump.logicFps * rate;
                }
                if (paused) {
                    unpaced = false;
                }
            }
            boolean isPaused;
            synchronized (lock) {
                isPaused = paused;
            }
            int budget = unpaced ? 256 : Integer.MAX_VALUE;
            long patience = System.nanoTime() + 20_000_000L;
            while (!isPaused && index < entries.size() && (unpaced || entries.get(index).position() <= playhead) && budget-- > 0) {
                if (unpaced) {
                    waitForRoom(patience);
                }
                release(entries.get(index));
                index++;
            }
            if (index >= entries.size() && !entries.isEmpty()) {
                if (loop) {
                    index = 0;
                    playhead = 0;
                    loops.incrementAndGet();
                    synth.reset();
                } else {
                    finished = true;
                }
            } else if (entries.isEmpty()) {
                finished = true;
            }
            tickCamera(now);
            tickSynthesized(now);
            if (!unpaced) {
                TimeUnit.NANOSECONDS.sleep(TICK_NANOS);
            } else {
                Thread.yield();
            }
        }
    }

    /** As fast as possible still waits a little for readers that fall behind, so a quick test does not lose frames to its own speed. */
    private void waitForRoom(long deadlineNanos) throws InterruptedException {
        while (System.nanoTime() < deadlineNanos) {
            boolean room = true;
            synchronized (lock) {
                for (Session s : slots) {
                    if (s != null && s.queued.get() > queueBytes / 2) {
                        room = false;
                    }
                }
            }
            if (room) {
                return;
            }
            TimeUnit.MILLISECONDS.sleep(1);
        }
    }

    private void release(FakeDump.Entry entry) {
        DecodedFrame d = entry.frame();
        synchronized (lock) {
            released.incrementAndGet();
            Message message = d.message();
            if (message instanceof Camera recorded) {
                lastRecordedCamera = recorded;
                if (camera.commanded()) {
                    return;
                }
                camera.adopt(recorded);
                if (recorded.getHasViewBox()) {
                    synth.setView(corners(recorded));
                }
                Camera stamped = recorded.toBuilder().setTimestampUs(micros()).build();
                for (Session s : slots) {
                    if (s != null && s.feed.config().getCamera()) {
                        s.send(FrameType.CAMERA, stamped);
                    }
                }
                return;
            }
            world.apply(d);
            if (message instanceof StatsSnapshot snapshot) {
                synth.onSnapshot(snapshot);
                StatsSnapshot adjusted = adjustSpeed(snapshot);
                for (Session s : slots) {
                    StatsSnapshot filtered = s == null ? null : s.feed.snapshot(adjusted, dump.logicFps);
                    if (filtered != null) {
                        s.send(FrameType.STATS_SNAPSHOT, filtered);
                    }
                }
            } else if (message instanceof Events events) {
                synth.onEvents(events);
                for (Session s : slots) {
                    if (s != null && s.feed.config().getEvents()) {
                        s.send(FrameType.EVENTS, events);
                    }
                }
            } else if (message instanceof ViewStats || message instanceof InterestMap) {
                long now = System.nanoTime();
                for (Session s : slots) {
                    boolean due = s != null && (message instanceof ViewStats ? s.feed.viewStatsDue(now) : s.feed.interestDue(now));
                    if (due) {
                        s.send(d.type(), message);
                    }
                }
            } else if (message instanceof ChatMessage) {
                for (Session s : slots) {
                    if (s != null && s.feed.config().getChat()) {
                        s.send(FrameType.CHAT_MESSAGE, message);
                    }
                }
            } else {
                if (message instanceof MatchInfo m && m.getPhase() == com.generalsonline.stats.proto.MatchPhase.MATCH_PHASE_LOADING) {
                    synth.reset();
                }
                for (Session s : slots) {
                    if (s != null) {
                        s.send(d.type(), message);
                    }
                }
            }
        }
    }

    /** The speed fields of a snapshot say how fast this fake game runs: paused, fast forward or the effective ratio. */
    private StatsSnapshot adjustSpeed(StatsSnapshot s) {
        if (s.getPlayersCount() == 0 && !s.getFull()) {
            return s;
        }
        double effective = fastForward ? 0 : Math.min(ratio, replaySpeedCap) * (speed == 0 ? 1 : speed);
        StatsSnapshot.Builder b = s.toBuilder().setReplayTotalFrames((int) (dump.firstFrame + dump.length()));
        if (paused) {
            b.setGameSpeedFps(0).setGameSpeedRatio(0f);
        } else if (fastForward || speed == 0) {
            b.setGameSpeedFps(-1).setGameSpeedRatio(0f);
        } else {
            b.setGameSpeedFps((int) Math.round(dump.logicFps * effective)).setGameSpeedRatio((float) effective);
        }
        return b.build();
    }

    private static float[] corners(Camera c) {
        return new float[] {c.getViewTlX(), c.getViewTlY(), c.getViewTrX(), c.getViewTrY(), c.getViewBrX(), c.getViewBrY(), c.getViewBlX(), c.getViewBlY()};
    }

    private void tickCamera(long now) {
        synchronized (lock) {
            if (!camera.commanded()) {
                if (!dump.hasCamera) {
                    world.state().worldExtent().ifPresent(e -> camera.center((e.minX() + e.maxX()) / 2, (e.minY() + e.maxY()) / 2));
                    if (world.matchRunning()) {
                        synth.setView(corners(camera.frame(micros(), world.frame())));
                    }
                }
                return;
            }
            Camera frame = camera.tick(now, world::positionOf, micros(), world.frame());
            if (frame != null) {
                synth.setView(corners(frame));
                for (Session s : slots) {
                    if (s != null && s.feed.config().getCamera()) {
                        s.send(FrameType.CAMERA, frame);
                    }
                }
            }
        }
    }

    /** The view statistics and interest maps a dump without them does not have, on each reader's cadence. */
    private void tickSynthesized(long now) {
        synchronized (lock) {
            if (!world.matchRunning() || world.frame() == 0) {
                return;
            }
            boolean highlight = synth.takeNewHighlight();
            for (Session s : slots) {
                if (s == null) {
                    continue;
                }
                if (!dump.hasViewStats && s.feed.viewStatsDue(now)) {
                    FeedConfig c = s.feed.config();
                    ViewStats view = synth.view(world.state(), world.liveObjects(), world.frame(), (int) Format.u(c.getNotableCost()), c.getTradeWindowMs(), c.getIntensityWindowMs());
                    s.send(FrameType.VIEW_STATS, view);
                }
                if (!dump.hasInterest) {
                    if (highlight && s.feed.config().getInterest()) {
                        s.feed.interestNow();
                    }
                    if (s.feed.interestDue(now)) {
                        InterestMap map = synth.interest(world.state(), s.feed.config().getGridResolution(), world.frame());
                        if (map != null) {
                            s.send(FrameType.INTEREST_MAP, map);
                        }
                    }
                }
            }
        }
    }

    // -- Requests.

    private void handle(Session s, RawFrame frame) {
        if (frame.type() == FrameType.CHAT_SEND.id()) {
            try {
                echoChat(ChatSend.parseFrom(frame.body()));
            } catch (com.google.protobuf.InvalidProtocolBufferException e) {
                // the game drops what it cannot read
            }
            return;
        }
        if (frame.type() != FrameType.REQUEST.id()) {
            return;
        }
        Request request;
        try {
            request = Request.parseFrom(frame.body());
        } catch (com.google.protobuf.InvalidProtocolBufferException e) {
            respond(s, Response.newBuilder().setId(0).setStatus(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST).setMessage("the request could not be decoded"));
            return;
        }
        Response.Builder response;
        Runnable after = null;
        if (s.limiter != null && !s.limiter.tryAcquire(System.nanoTime())) {
            response = refuse(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, "too many requests");
        } else {
            switch (request.getQueryCase()) {
                case CATALOG -> response = world.catalog(request.getCatalog(), MAX_PAGE_SIZE);
                case OBJECT -> response = world.object(request.getObject().getObjectId());
                case OBJECTS -> response = world.objects(request.getObjects(), MAX_PAGE_SIZE);
                case PLAYER -> response = world.player(request.getPlayer().getPlayerIndex());
                case MAP -> response = world.map(request.getMap().getIncludeMinimap());
                case RESYNC -> {
                    response = Response.newBuilder().setResync(ResyncResult.getDefaultInstance());
                    after = () -> {
                        synchronized (lock) {
                            sendJoin(s);
                        }
                    };
                }
                case FEED_CONFIG -> {
                    boolean cameraWas = s.feed.config().getCamera();
                    String problem = s.feed.apply(request.getFeedConfig());
                    response = problem == null ? Response.newBuilder().setFeedConfig(s.feed.config())
                        : refuse(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, problem);
                    if (problem == null) {
                        boolean cameraNow = s.feed.config().getCamera();
                        if (s.feed.config().getInterest()) {
                            s.feed.interestNow();
                        }
                        if (cameraNow && !cameraWas) {
                            after = () -> {
                                synchronized (lock) {
                                    Camera current = currentCamera();
                                    if (current != null) {
                                        s.send(FrameType.CAMERA, current);
                                    }
                                }
                            };
                        }
                    }
                }
                case CAMERA_CONTROL -> response = cameraControl(request.getCameraControl());
                case CAMERA_FOLLOW -> response = cameraFollow(request.getCameraFollow());
                case REPLAY_CONTROL -> response = replayControl(request.getReplayControl());
                case OBSERVE -> response = observe(request.getObserve());
                case LOBBY_CONTROL -> response = lobbyControl(request.getLobbyControl());
                default -> response = refuse(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, "the request has no query");
            }
        }
        response.setId(request.getId());
        respond(s, response);
        if (after != null) {
            after.run();
        }
    }

    private static Response.Builder refuse(ResponseStatus status, String message) {
        return Response.newBuilder().setStatus(status).setMessage(message);
    }

    private void respond(Session s, Response.Builder response) {
        s.send(FrameType.RESPONSE, response.build());
    }

    private void echoChat(ChatSend chat) {
        if (chat.getText().isEmpty()) {
            return;
        }
        ChatMessage message = ChatMessage.newBuilder().setChannel(chat.getChannel()).setSenderName("FakeGame").setText(chat.getText()).setAudience(chat.getAudience())
            .setIsLocal(true).build();
        synchronized (lock) {
            for (Session s : slots) {
                if (s != null && s.feed.config().getChat()) {
                    s.send(FrameType.CHAT_MESSAGE, message);
                }
            }
        }
    }

    /** The answer for a control command the situation or the missing match forbids, or null when it may go ahead. */
    private Response.Builder controlRefusal(boolean replayCommand) {
        if (!world.matchRunning()) {
            return refuse(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, "no match is running");
        }
        if (situation == Situation.PLAYER) {
            return refuse(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED, "the local player is an active player: the camera and the observer view are not touched");
        }
        if (replayCommand && situation != Situation.REPLAY) {
            return refuse(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED, "replay commands need a replay playback; pausing or slowing a live game would break synchronization");
        }
        return null;
    }

    private Response.Builder cameraControl(CameraControlQuery query) {
        Response.Builder refused = controlRefusal(false);
        if (refused != null) {
            return refused;
        }
        synchronized (lock) {
            FakeCamera.Refusal why = camera.control(query, System.nanoTime());
            return why != null ? refuse(why.status(), why.message()) : Response.newBuilder().setCameraControl(camera.state());
        }
    }

    private Response.Builder cameraFollow(CameraFollowQuery query) {
        Response.Builder refused = controlRefusal(false);
        if (refused != null) {
            return refused;
        }
        synchronized (lock) {
            FakeCamera.Refusal why = camera.follow(query, world::positionOf, System.nanoTime());
            return why != null ? refuse(why.status(), why.message()) : Response.newBuilder().setCameraFollow(camera.state());
        }
    }

    private Response.Builder replayControl(ReplayControlQuery query) {
        Response.Builder refused = controlRefusal(true);
        if (refused != null) {
            return refused;
        }
        if (query.hasSpeedRatio() && !(query.getSpeedRatio() >= 0.1f && query.getSpeedRatio() <= 16f)) {
            return refuse(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, "speed_ratio must be 0.1 to 16");
        }
        synchronized (lock) {
            if (query.hasPaused()) {
                paused = query.getPaused();
            }
            if (query.hasSpeedRatio()) {
                ratio = query.getSpeedRatio();
                capped = ratio > replaySpeedCap;
                fastForward = false;
            }
            if (query.hasFastForward()) {
                fastForward = query.getFastForward();
            }
            return Response.newBuilder().setReplayControl(replayState());
        }
    }

    private Response.Builder observe(ObserveQuery query) {
        Response.Builder refused = controlRefusal(false);
        if (refused != null) {
            return refused;
        }
        int index = query.getPlayerIndex();
        if (index >= 0) {
            boolean known = false;
            for (MatchPlayer p : world.state().matchPlayers()) {
                known |= p.getPlayerIndex() == index && !p.getIsObserver();
            }
            if (!known) {
                return refuse(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no player " + index + " in this match");
            }
        }
        synchronized (lock) {
            observed = index < 0 ? -1 : index;
            return Response.newBuilder().setObserve(ObserveState.newBuilder().setPlayerIndex(observed).setFullView(true));
        }
    }

    /** A lobby control action, while the dump is in a game setup lobby: applied to the lobby model, then sent to every reader as a LobbyState. */
    private Response.Builder lobbyControl(LobbyControlQuery query) {
        synchronized (lock) {
            LobbyState current = world.lobby();
            if (current == null) {
                return refuse(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, "the user is not in a game setup lobby");
            }
            FakeLobby.Outcome outcome = FakeLobby.apply(current, query, lobbyLocalSlot);
            if (outcome.refusal() != null) {
                return refuse(outcome.refusal().status(), outcome.refusal().message());
            }
            if (!outcome.state().equals(current)) {
                world.replaceLobby(outcome.state());
                for (Session s : slots) {
                    if (s != null) {
                        s.send(FrameType.LOBBY_STATE, outcome.state());
                    }
                }
            }
            return Response.newBuilder().setLobbyControl(outcome.result());
        }
    }

    /**
     * Returns the player whose point of view the observer command selected.
     *
     * @return the player index, or -1 for the all-seeing observer
     */
    public int observedPlayer() {
        synchronized (lock) {
            return observed;
        }
    }
}
