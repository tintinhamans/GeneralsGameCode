package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.internal.NamedPipeLink;
import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Supplier;

/**
 * A connection to the live stats pipe of a Generals Online game: it finds a game, reads and decodes the frames,
 * reconnects when the game restarts, delivers everything to a {@link StatsListener} and can send chat lines back.
 *
 * <pre>{@code
 * try (StatsPipe pipe = StatsPipe.builder().listener(new StatsListener() {
 *     public void onStatsSnapshot(StatsSnapshot s) { System.out.println("frame " + s.getFrame()); }
 * }).build().start()) {
 *     Thread.currentThread().join();
 * }
 * }</pre>
 *
 * <p><b>Threading.</b> {@link #start()} creates one daemon reader thread that connects, reads and decodes. What
 * happens with a decoded frame is decided by the executor: by default the reader thread itself calls the listener
 * (and updates the {@link MatchState}) directly, so callbacks must be quick. With {@link Builder#executor} each frame
 * becomes one task on that executor, in order, so a UI thread can receive the callbacks; the executor must run its
 * tasks in submission order (an event loop does; a thread pool does not). {@link #sendChat} and {@link #close()} may
 * be called from any thread.
 *
 * <p><b>One reader per game.</b> The game accepts a single reader per pipe. Without {@link Builder#pid} or
 * {@link Builder#pipe} the pipe tries every running game, highest pid first, and keeps the first one that is free; a
 * pipe held by another client is skipped.
 *
 * <p><b>Errors.</b> A listener, state listener or executor task that throws costs only its frame: the exception goes to
 * {@link StatsListener#onError} and reading continues. Only a pipe or framing failure disconnects, and its real cause
 * is passed to {@link StatsListener#onDisconnected}. This class never prints.
 */
public final class StatsPipe implements AutoCloseable {
    /** The longest chat line the game takes, in UTF-8 bytes. */
    public static final int MAX_CHAT_BYTES = 1024;

    private static final StatsListener NO_LISTENER = new StatsListener() {
    };

    private final Supplier<List<String>> candidates;
    private final PipeDiscovery.PipeOpener<PipeLink> opener;
    private final StatsListener listener;
    private final MatchState state;
    private final Executor executor;
    private final boolean reconnect;
    private final long minBackoffMillis;
    private final long maxBackoffMillis;
    private final Object writeLock = new Object();
    private volatile boolean closed;
    private volatile PipeLink link;
    private volatile String connectedPath;
    private Thread thread;

    private StatsPipe(Builder b) {
        this.candidates = b.candidates != null ? b.candidates : candidatesFor(b.pid, b.pipe);
        this.opener = b.opener != null ? b.opener : NamedPipeLink::open;
        this.listener = b.listener != null ? b.listener : NO_LISTENER;
        this.state = b.state;
        this.executor = b.executor != null ? b.executor : Runnable::run;
        this.reconnect = b.reconnect;
        this.minBackoffMillis = b.minBackoff.toMillis();
        this.maxBackoffMillis = b.maxBackoff.toMillis();
    }

    private static Supplier<List<String>> candidatesFor(int pid, String pipe) {
        return () -> PipeDiscovery.candidates(pid, pipe);
    }

    /**
     * Starts building a connection.
     *
     * @return a builder with auto-discovery, reconnect on and no listener
     */
    public static Builder builder() {
        return new Builder();
    }

    /** Builds a {@link StatsPipe}. Not thread safe; use it on one thread and call {@link #build()} once. */
    public static final class Builder {
        private int pid = -1;
        private String pipe;
        private StatsListener listener;
        private MatchState state;
        private Executor executor;
        private boolean reconnect = true;
        private Duration minBackoff = Duration.ofMillis(250);
        private Duration maxBackoff = Duration.ofSeconds(5);
        private Supplier<List<String>> candidates;
        private PipeDiscovery.PipeOpener<PipeLink> opener;

        private Builder() {
        }

        /**
         * Connects to the game with this process id. Without it (and without {@link #pipe}) every running game is tried.
         *
         * @param pid the process id of the game, 0 or more
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
         * Connects to this exact pipe.
         *
         * @param pipePath the full pipe path, for example {@code \\.\pipe\generalsonline-stats-1234}
         * @return this builder
         */
        public Builder pipe(String pipePath) {
            if (pipePath == null || pipePath.isBlank()) {
                throw new IllegalArgumentException("the pipe path must not be empty");
            }
            this.pipe = pipePath;
            return this;
        }

        /**
         * Sets who receives the connection events and frames.
         *
         * @param listener the receiver; without one the pipe is only useful for {@link StatsPipe#sendChat} or a state
         * @return this builder
         */
        public Builder listener(StatsListener listener) {
            this.listener = Objects.requireNonNull(listener, "listener");
            return this;
        }

        /**
         * Keeps a {@link MatchState} up to date. The state is updated before the listener is called for the same
         * frame, so the listener always sees the state including that frame.
         *
         * @param state the state to feed
         * @return this builder
         */
        public Builder state(MatchState state) {
            this.state = Objects.requireNonNull(state, "state");
            return this;
        }

        /**
         * Chooses where callbacks run. The default is the reader thread. Pass for example
         * {@code SwingUtilities::invokeLater} (or {@code Platform::runLater} in JavaFX) to receive callbacks on a UI
         * thread. The executor must run tasks in the order they were submitted.
         *
         * @param executor runs one task per frame and per lifecycle event
         * @return this builder
         */
        public Builder executor(Executor executor) {
            this.executor = Objects.requireNonNull(executor, "executor");
            return this;
        }

        /**
         * Chooses whether to connect again after the connection ended. On by default. When off, the pipe still waits
         * for the game to appear, but stops (the reader thread ends) after the first connection ended.
         *
         * @param reconnect true to keep reconnecting with backoff
         * @return this builder
         */
        public Builder reconnect(boolean reconnect) {
            this.reconnect = reconnect;
            return this;
        }

        /**
         * Sets the wait between connection attempts. It starts at {@code min}, doubles after each failed attempt and
         * stops at {@code max}; a successful connection resets it. The default is 250 ms to 5 s.
         *
         * @param min the first wait, at least 1 ms
         * @param max the longest wait, not shorter than {@code min}
         * @return this builder
         */
        public Builder reconnectBackoff(Duration min, Duration max) {
            if (min.toMillis() < 1 || max.compareTo(min) < 0) {
                throw new IllegalArgumentException("the backoff needs 1 ms <= min <= max, got " + min + " and " + max);
            }
            this.minBackoff = min;
            this.maxBackoff = max;
            return this;
        }

        /** Replaces discovery with a fixed supplier of candidate paths. For tests. */
        Builder candidates(Supplier<List<String>> candidates) {
            this.candidates = candidates;
            return this;
        }

        /** Replaces the Windows pipe opener. For tests. */
        Builder opener(PipeDiscovery.PipeOpener<PipeLink> opener) {
            this.opener = opener;
            return this;
        }

        /**
         * Creates the connection; call {@link StatsPipe#start()} to begin.
         *
         * @return the pipe, not started
         * @throws IllegalStateException when both a pid and a pipe path were given
         */
        public StatsPipe build() {
            if (pid >= 0 && pipe != null) {
                throw new IllegalStateException("pid() and pipe() are alternatives; set only one");
            }
            return new StatsPipe(this);
        }
    }

    // -- Lifecycle.

    /**
     * Starts the reader thread. It connects to a game, waiting for one to appear if none runs.
     *
     * @return this pipe
     * @throws IllegalStateException when already started or closed
     */
    public synchronized StatsPipe start() {
        if (closed) {
            throw new IllegalStateException("closed");
        }
        if (thread != null) {
            throw new IllegalStateException("already started");
        }
        thread = new Thread(this::run, "stats-pipe-reader");
        thread.setDaemon(true);
        thread.start();
        return this;
    }

    /**
     * Tells whether a game is connected right now.
     *
     * @return true between {@link StatsListener#onConnected} and {@link StatsListener#onDisconnected}
     */
    public boolean isConnected() {
        return link != null;
    }

    /**
     * Returns the pipe of the current connection.
     *
     * @return the pipe path, empty while not connected
     */
    public Optional<String> connectedPipe() {
        return Optional.ofNullable(connectedPath);
    }

    /**
     * Returns the process id of the connected game.
     *
     * @return the pid, empty while not connected or when the pipe path is not a stats pipe name
     */
    public OptionalInt connectedPid() {
        return PipeDiscovery.pidOfPath(connectedPath);
    }

    /** Stops reading and closes the connection. Safe to call more than once and from any thread. */
    @Override
    public void close() {
        closed = true;
        PipeLink current = link;
        if (current != null) {
            try {
                current.close();
            } catch (IOException ignored) {
                // already broken
            }
        }
        Thread reader;
        synchronized (this) {
            reader = thread;
        }
        if (reader != null) {
            reader.interrupt();
        }
    }

    /** Waits for the reader thread to end; for tests. */
    boolean awaitStopped(long millis) throws InterruptedException {
        Thread reader;
        synchronized (this) {
            reader = thread;
        }
        if (reader == null) {
            return true;
        }
        reader.join(millis);
        return !reader.isAlive();
    }

    // -- Chat.

    /**
     * Checks a chat line against the limits the game applies.
     *
     * @param text the line
     * @throws IllegalArgumentException with a message fit to show a user, when the text is empty or blank, or longer
     *         than {@value #MAX_CHAT_BYTES} bytes in UTF-8
     */
    public static void requireSendable(String text) {
        if (text == null || text.isBlank()) {
            throw new IllegalArgumentException("the chat text is empty");
        }
        int bytes = text.getBytes(StandardCharsets.UTF_8).length;
        if (bytes > MAX_CHAT_BYTES) {
            throw new IllegalArgumentException("the chat text is " + bytes + " bytes in UTF-8, the limit is " + MAX_CHAT_BYTES);
        }
    }

    /**
     * Sends a chat line to everyone in a channel. See {@link #sendChat(ChatChannel, String, ChatAudience)}.
     *
     * @param channel the chat box to send to
     * @param text the line, not empty, at most {@value #MAX_CHAT_BYTES} bytes in UTF-8
     * @return false when not connected, true when the bytes were written
     * @throws IOException when writing to the pipe failed
     * @throws IllegalArgumentException when the text is empty or too long
     */
    public boolean sendChat(ChatChannel channel, String text) throws IOException {
        return sendChat(channel, text, ChatAudience.CHAT_AUDIENCE_EVERYONE);
    }

    /**
     * Sends a chat line. The game handles the text as if it was typed into that channel's chat box, slash commands
     * included, and drops it without a reply unless that channel's screen or match is active. A true result therefore
     * means the bytes were written, not that the game accepted the line. The game serves one reader per pipe, so this
     * works while this pipe is the reader of the game, not alongside another client.
     *
     * @param channel the chat box to send to
     * @param text the line, not empty, at most {@value #MAX_CHAT_BYTES} bytes in UTF-8
     * @param audience who gets an in-match line: EVERYONE, ALLIES or OBSERVERS (observers only); ignored by the
     *                 other channels
     * @return false when not connected, true when the bytes were written
     * @throws IOException when writing to the pipe failed
     * @throws IllegalArgumentException when the text is empty or too long, or the channel or audience cannot be sent
     */
    public boolean sendChat(ChatChannel channel, String text, ChatAudience audience) throws IOException {
        Objects.requireNonNull(channel, "channel");
        Objects.requireNonNull(audience, "audience");
        if (channel == ChatChannel.UNRECOGNIZED) {
            throw new IllegalArgumentException("unknown chat channel");
        }
        if (audience == ChatAudience.UNRECOGNIZED || audience == ChatAudience.CHAT_AUDIENCE_OTHER) {
            throw new IllegalArgumentException("the audience " + audience + " cannot be sent to; use EVERYONE, ALLIES or OBSERVERS");
        }
        requireSendable(text);
        byte[] body = ChatSend.newBuilder().setChannel(channel).setText(text).setAudience(audience).build().toByteArray();
        if (body.length == 0 || body.length > FrameCodec.MAX_INBOUND_BODY_BYTES) {
            throw new IllegalArgumentException("ChatSend body must be 1.." + FrameCodec.MAX_INBOUND_BODY_BYTES + " bytes");
        }
        return writeFrame(FrameType.CHAT_SEND.id(), body);
    }

    private boolean writeFrame(int type, byte[] body) throws IOException {
        PipeLink out = link;
        if (out == null) {
            return false;
        }
        ByteBuffer frame = ByteBuffer.wrap(FrameCodec.encode(type, body));
        synchronized (writeLock) {
            out.write(frame);
        }
        return true;
    }

    // -- The reader thread.

    private void run() {
        long backoff = minBackoffMillis;
        while (!closed) {
            List<String> paths = candidates.get();
            PipeDiscovery.Opened<PipeLink> connection = paths == null || paths.isEmpty() ? null : PipeDiscovery.openFirst(paths, opener);

            if (connection == null) {
                sleep(backoff);
                backoff = Math.min(backoff * 2, maxBackoffMillis);
                continue;
            }

            PipeLink opened = connection.channel();
            String path = connection.path();
            backoff = minBackoffMillis;
            link = opened;
            connectedPath = path;
            Throwable cause = null;
            try {
                post(0, () -> listener.onConnected(path));
                readLoop(opened);
            } catch (Throwable t) {
                cause = t;
            } finally {
                link = null;
                connectedPath = null;
                try {
                    opened.close();
                } catch (IOException ignored) {
                    // closing a broken pipe
                }
            }
            if (!closed) {
                Throwable why = cause;
                post(0, () -> listener.onDisconnected(why));
                if (!reconnect) {
                    return;
                }
                sleep(backoff);
            }
        }
    }

    private void readLoop(PipeLink in) throws IOException {
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        ByteBuffer buffer = ByteBuffer.allocate(65536);
        while (!closed) {
            buffer.clear();
            int read = in.read(buffer);
            if (read < 0) {
                return;
            }
            for (RawFrame frame : decoder.push(buffer.array(), 0, read)) {
                handle(frame);
            }
        }
    }

    /** Decodes once on the reader thread, then queues delivery on the executor. */
    void handle(RawFrame frame) {
        DecodedFrame decoded = FrameDispatcher.decode(frame);
        post(frame.type(), () -> deliver(decoded));
    }

    private void deliver(DecodedFrame decoded) {
        // The state first, so the listener of the same frame sees it up to date. A state that throws is reported to the listener.
        if (state != null) {
            FrameDispatcher.deliverGuarded(decoded, state, listener);
        }
        FrameDispatcher.deliverGuarded(decoded, listener);
    }

    /** Runs a task through the executor; whatever it throws goes to onError, a rejecting executor drops the task. */
    private void post(int frameType, Runnable task) {
        try {
            executor.execute(() -> {
                try {
                    task.run();
                } catch (RuntimeException e) {
                    FrameDispatcher.reportError(frameType, e, listener);
                }
            });
        } catch (RejectedExecutionException e) {
            // the executor is shutting down (for example the UI was closed): nobody is left to tell
        }
    }

    private void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
