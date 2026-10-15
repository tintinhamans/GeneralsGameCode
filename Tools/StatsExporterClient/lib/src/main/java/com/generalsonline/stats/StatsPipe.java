package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.internal.NamedPipeLink;
import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.internal.RequestLimiter;
import com.generalsonline.stats.internal.SequenceTracker;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.MapQuery;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.ObjectQuery;
import com.generalsonline.stats.proto.ObjectsQuery;
import com.generalsonline.stats.proto.ObjectsResult;
import com.generalsonline.stats.proto.PageInfo;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.PlayerQuery;
import com.generalsonline.stats.proto.Request;
import com.generalsonline.stats.proto.ResyncQuery;
import com.generalsonline.stats.proto.ResyncResult;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.TemplateRef;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.IntFunction;
import java.util.function.Supplier;

/**
 * A connection to the live stats pipe of a Generals Online game: it finds a game, reads and decodes the frames,
 * reconnects when the game restarts, delivers everything to a {@link StatsListener}, can send chat lines back and
 * asks the game questions (the request API).
 *
 * <pre>{@code
 * try (StatsPipe pipe = StatsPipe.builder().listener(new StatsListener() {
 *     public void onStatsSnapshot(StatsSnapshot s) { System.out.println("frame " + s.getFrame()); }
 * }).build().start()) {
 *     Thread.currentThread().join();
 * }
 * }</pre>
 *
 * <p><b>Handshake.</b> The first frame of every connection is the game's {@code Hello}. The reader thread waits for it
 * (at most {@link Builder#helloTimeout}, 10 s by default), refuses a protocol version other than
 * {@value Handshake#PROTOCOL_VERSION} with a {@link ProtocolVersionException} and a connection that sends something else
 * first with a {@link FrameException}; the cause reaches {@link StatsListener#onDisconnected}. The accepted Hello is
 * available as {@link #hello()} and is passed to {@link StatsListener#onHello}.
 *
 * <p><b>Threading.</b> {@link #start()} creates one daemon reader thread that connects, reads and decodes. What
 * happens with a decoded frame is decided by the executor: by default the reader thread itself calls the listener
 * (and updates the {@link MatchState}) directly, so callbacks must be quick. With {@link Builder#executor} each frame
 * becomes one task on that executor, in order, so a UI thread can receive the callbacks; the executor must run its
 * tasks in submission order (an event loop does; a thread pool does not). {@link #sendChat}, the request methods and
 * {@link #close()} may be called from any thread. The futures of the request API are completed on the reader thread,
 * not on the executor: never block on one from a callback that runs on the reader thread, use
 * {@code thenAccept} instead.
 *
 * <p><b>Readers.</b> The game serves several readers per pipe ({@code Hello.max_readers}); each has its own queue,
 * sequence numbers and feed configuration. Without {@link Builder#pid} or {@link Builder#pipe} the pipe tries every
 * running game, highest pid first, and keeps the first one that has a free slot; a game whose slots are all taken is
 * skipped.
 *
 * <p><b>Dropped frames.</b> Every frame carries a per-reader sequence number. A reader that falls behind loses frames
 * and the sequence then jumps. The gaps are summed in {@link #droppedFrames()} and reported through
 * {@link StatsListener#onFramesDropped}; after a drop the state a listener built may be stale, and
 * {@link #resync()} asks the game to send the current screen, lobby, match and a full snapshot again.
 *
 * <p><b>Requests.</b> {@link #request(Request, Duration)} sends a query and returns a future for the matching
 * {@link Response}, and the typed methods ({@link #catalog}, {@link #object}, {@link #objects}, {@link #player},
 * {@link #map}, {@link #resync}, {@link #feedConfig}, {@link #setFeedConfig}, {@link #catalogAll},
 * {@link #objectsAll}) unwrap the result. The library assigns the request ids. The game rate-limits requests
 * ({@code Hello.max_requests_per_second} sustained, {@code Hello.max_request_burst} at once) and answers an excess
 * request with {@code RATE_LIMITED}; {@link Builder#limitRequests} makes the library apply the same limit itself.
 * Requests can only be sent once the Hello arrived.
 *
 * <p><b>Errors.</b> A listener, state listener or executor task that throws costs only its frame: the exception goes to
 * {@link StatsListener#onError} and reading continues. Only a pipe or framing failure disconnects, and its real cause
 * is passed to {@link StatsListener#onDisconnected}. This class never prints.
 */
public final class StatsPipe implements AutoCloseable {
    /** The longest chat line the game takes, in UTF-8 bytes. */
    public static final int MAX_CHAT_BYTES = 1024;

    /** How long the library waits for the game's Hello after the pipe was opened unless {@link Builder#helloTimeout} says otherwise. */
    public static final Duration DEFAULT_HELLO_TIMEOUT = Duration.ofSeconds(10);

    /** How long a typed request method waits for the response unless {@link Builder#requestTimeout} says otherwise. */
    public static final Duration DEFAULT_REQUEST_TIMEOUT = Duration.ofSeconds(5);

    /** How many pages {@link #catalogAll} and {@link #objectsAll} fetch at most unless told otherwise. */
    public static final int DEFAULT_MAX_PAGES = 100;

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
    private final Duration helloTimeout;
    private final Duration requestTimeout;
    private final boolean limitRequests;
    private final Object writeLock = new Object();
    private final Object helloLock = new Object();
    private final ScheduledThreadPoolExecutor scheduler;
    private final Map<Integer, CompletableFuture<Response>> pending = new ConcurrentHashMap<>();
    private final AtomicInteger lastRequestId = new AtomicInteger();
    private final AtomicLong droppedFrames = new AtomicLong();
    private final ClockSync clockSync = new ClockSync();
    private volatile RequestLimiter limiter;
    private volatile Hello hello;
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
        this.helloTimeout = b.helloTimeout;
        this.requestTimeout = b.requestTimeout;
        this.limitRequests = b.limitRequests;
        this.scheduler = new ScheduledThreadPoolExecutor(1, runnable -> {
            Thread timer = new Thread(runnable, "stats-pipe-timer");
            timer.setDaemon(true);
            return timer;
        });
        this.scheduler.setRemoveOnCancelPolicy(true);
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
        private Duration helloTimeout = DEFAULT_HELLO_TIMEOUT;
        private Duration requestTimeout = DEFAULT_REQUEST_TIMEOUT;
        private boolean limitRequests;
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
         * @param listener the receiver; without one the pipe is only useful for {@link StatsPipe#sendChat}, requests or a state
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

        /**
         * Sets how long to wait for the game's Hello after the pipe was opened. When it does not arrive the connection
         * is closed and {@link StatsListener#onDisconnected} receives a {@link HelloTimeoutException} (a game that
         * speaks an older protocol never sends one). The default is {@link StatsPipe#DEFAULT_HELLO_TIMEOUT}, 10 seconds.
         *
         * @param timeout the wait, at least 1 ms
         * @return this builder
         */
        public Builder helloTimeout(Duration timeout) {
            requirePositive(timeout, "the Hello timeout");
            this.helloTimeout = timeout;
            return this;
        }

        /**
         * Sets how long the typed request methods wait for a response. {@link StatsPipe#request(Request, Duration)}
         * takes its timeout as an argument. The default is {@link StatsPipe#DEFAULT_REQUEST_TIMEOUT}, 5 seconds.
         *
         * @param timeout the wait, at least 1 ms
         * @return this builder
         */
        public Builder requestTimeout(Duration timeout) {
            requirePositive(timeout, "the request timeout");
            this.requestTimeout = timeout;
            return this;
        }

        /**
         * Makes the library enforce the game's request rate itself, from {@code Hello.max_requests_per_second} and
         * {@code Hello.max_request_burst}: a request over the limit is not sent and its response has the status
         * {@code RATE_LIMITED} with the message "client-side rate limit" (so a typed method fails with a
         * {@link RequestFailedException}). Off by default, which leaves the limit to the game. A Hello without a rate
         * means no limit.
         *
         * @param limit true to limit requests on the client
         * @return this builder
         */
        public Builder limitRequests(boolean limit) {
            this.limitRequests = limit;
            return this;
        }

        private static void requirePositive(Duration timeout, String what) {
            Objects.requireNonNull(timeout, what);
            if (timeout.toMillis() < 1) {
                throw new IllegalArgumentException(what + " must be at least 1 ms, got " + timeout);
            }
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
     * @return true between {@link StatsListener#onConnected} and {@link StatsListener#onDisconnected}; the game's Hello
     *         may not have arrived yet, see {@link #hello()}
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

    /**
     * Returns the Hello of the current connection.
     *
     * @return the accepted handshake, empty from the moment the pipe is opened until the Hello arrives and again after
     *         the connection ended; {@link MatchState#hello()} keeps the last one
     */
    public Optional<Hello> hello() {
        return Optional.ofNullable(hello);
    }

    /**
     * Waits for the Hello of the current or next connection.
     *
     * @param timeout the longest wait
     * @return the handshake, empty when none arrived in time
     * @throws InterruptedException when the waiting thread is interrupted
     */
    public Optional<Hello> awaitHello(Duration timeout) throws InterruptedException {
        long deadline = System.nanoTime() + timeout.toNanos();
        synchronized (helloLock) {
            while (hello == null && !closed) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0) {
                    break;
                }
                TimeUnit.NANOSECONDS.timedWait(helloLock, remaining);
            }
        }
        return Optional.ofNullable(hello);
    }

    /**
     * Tells whether the game announced a capability in its Hello.
     *
     * @param name a capability name, see {@code docs/stats-exporter.md}
     * @return true when the current connection's Hello lists it; false when it does not or no Hello is available
     */
    public boolean hasCapability(String name) {
        Hello current = hello;
        return current != null && Handshake.hasCapability(current, name);
    }

    /**
     * Counts the frames the game dropped for this reader because its queue overflowed, found from gaps in the sequence
     * numbers. Cumulative over all connections of this pipe.
     *
     * @return the number of frames known to be lost
     */
    public long droppedFrames() {
        return droppedFrames.get();
    }

    /**
     * Returns the clock estimator, fed with the timestamp and arrival time of every frame and reset by every Hello.
     *
     * @return the estimator of the current connection
     */
    public ClockSync clockSync() {
        return clockSync;
    }

    /** Stops reading and closes the connection. Safe to call more than once and from any thread. */
    @Override
    public void close() {
        closed = true;
        failAllPending(new IOException("the stats pipe was closed"));
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
        synchronized (helloLock) {
            helloLock.notifyAll();
        }
        scheduler.shutdownNow();
    }

    /** The number of requests waiting for a response; for tests. */
    int pendingRequests() {
        return pending.size();
    }

    /** Sets the id counter so the next request gets {@code lastId + 1}; for tests of the wrap-around. */
    void setLastRequestId(int lastId) {
        lastRequestId.set(lastId);
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
     * means the bytes were written, not that the game accepted the line.
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
        byte[] body = FrameCodec.bodyOf(ChatSend.newBuilder().setChannel(channel).setText(text).setAudience(audience).build());
        if (body.length > FrameCodec.MAX_INBOUND_BODY_BYTES) {
            throw new IllegalArgumentException("ChatSend body must be 1.." + FrameCodec.MAX_INBOUND_BODY_BYTES + " bytes");
        }
        return writeFrame(FrameType.CHAT_SEND.id(), body);
    }

    /** Writes one inbound frame; the only write path, shared by chat and requests. */
    private boolean writeFrame(int type, byte[] body) throws IOException {
        PipeLink out = link;
        if (out == null) {
            return false;
        }
        ByteBuffer frame = ByteBuffer.wrap(FrameCodec.encodeInbound(type, body));
        synchronized (writeLock) {
            out.write(frame);
        }
        return true;
    }

    // -- Requests.

    /**
     * Sends a request and returns a future for the matching response. The id of the request is assigned here (any id in
     * {@code request} is replaced): a counter that skips 0 and the ids in flight. The future completes normally with the
     * {@link Response} whatever its status is ({@code NOT_FOUND}, {@code RATE_LIMITED}, ...; the typed methods turn a
     * status other than OK into a {@link RequestFailedException}), and exceptionally with
     * <ul>
     * <li>a {@link RequestTimeoutException} when no response came within {@code timeout} (a later response is ignored);</li>
     * <li>an {@link IOException} when the pipe is not connected, writing failed, or the connection ended or the pipe was
     *     closed while the request was waiting;</li>
     * <li>an {@link IllegalStateException} when the Hello has not arrived yet: wait for it with {@link #awaitHello} or
     *     {@link StatsListener#onHello}.</li>
     * </ul>
     *
     * <p>The future is completed on the reader thread. The game rate-limits requests, see the class description.
     *
     * @param request the query; one of the cases of its {@code query} must be set
     * @param timeout how long to wait for the response, at least 1 ms
     * @return the future of the response
     * @throws IllegalArgumentException when no query is set, the timeout is below 1 ms, or the request is over 4096 bytes
     */
    public CompletableFuture<Response> request(Request request, Duration timeout) {
        Objects.requireNonNull(request, "request");
        Builder.requirePositive(timeout, "the request timeout");
        if (request.getQueryCase() == Request.QueryCase.QUERY_NOT_SET) {
            throw new IllegalArgumentException("the request has no query set");
        }
        if (closed) {
            return CompletableFuture.failedFuture(new IOException("the stats pipe was closed"));
        }
        if (link == null) {
            return CompletableFuture.failedFuture(new IOException("not connected to a game"));
        }
        if (hello == null) {
            return CompletableFuture.failedFuture(new IllegalStateException("no Hello received yet; requests wait for the handshake"));
        }
        int id = nextRequestId();
        byte[] body = FrameCodec.bodyOf(request.toBuilder().setId(id).build());
        if (body.length > FrameCodec.MAX_INBOUND_BODY_BYTES) {
            throw new IllegalArgumentException("the request is " + body.length + " bytes, the limit is " + FrameCodec.MAX_INBOUND_BODY_BYTES);
        }
        RequestLimiter limit = limiter;
        if (limit != null && !limit.tryAcquire(System.nanoTime())) {
            Response refused = Response.newBuilder().setId(id).setStatus(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED).setMessage("client-side rate limit").build();
            return CompletableFuture.completedFuture(refused);
        }

        CompletableFuture<Response> future = new CompletableFuture<>();
        ScheduledFuture<?> timer;
        try {
            timer = scheduler.schedule(() -> {
                CompletableFuture<Response> expired = pending.remove(id);
                if (expired != null) {
                    expired.completeExceptionally(new RequestTimeoutException(id, timeout));
                }
            }, timeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (RejectedExecutionException e) {
            return CompletableFuture.failedFuture(new IOException("the stats pipe was closed", e));
        }
        future.whenComplete((response, error) -> {
            timer.cancel(false);
            pending.remove(id, future);
        });
        pending.put(id, future);
        try {
            if (!writeFrame(FrameType.REQUEST.id(), body)) {
                future.completeExceptionally(new IOException("not connected to a game"));
            }
        } catch (IOException e) {
            future.completeExceptionally(e);
        }
        return future;
    }

    /**
     * Sends a request built by the caller. See {@link #request(Request, Duration)}.
     *
     * @param request a builder with one query set; its id is ignored
     * @param timeout how long to wait for the response, at least 1 ms
     * @return the future of the response
     */
    public CompletableFuture<Response> request(Request.Builder request, Duration timeout) {
        return request(request.build(), timeout);
    }

    /** The next id: a uint32 counter that wraps and never yields 0 or an id that is still waiting. */
    private int nextRequestId() {
        while (true) {
            int id = lastRequestId.incrementAndGet();
            if (id != 0 && !pending.containsKey(id)) {
                return id;
            }
        }
    }

    private void completePending(Response response) {
        CompletableFuture<Response> waiting = pending.remove(response.getId());
        if (waiting != null) {
            waiting.complete(response);
        }
    }

    private void failAllPending(Throwable error) {
        for (Integer id : new ArrayList<>(pending.keySet())) {
            CompletableFuture<Response> waiting = pending.remove(id);
            if (waiting != null) {
                waiting.completeExceptionally(error);
            }
        }
    }

    /**
     * Sends a typed request and unwraps the result. Fails with a {@link RequestFailedException} for a status other
     * than OK, and with an {@link IOException} when the response carries another result than the query implies.
     */
    private <T> CompletableFuture<T> typed(Request.Builder query, Response.ResultCase expected, Function<Response, T> result) {
        return request(query, requestTimeout).thenCompose(response -> {
            if (response.getStatus() != ResponseStatus.RESPONSE_STATUS_OK) {
                return CompletableFuture.failedFuture(new RequestFailedException(response.getStatus(), response.getMessage()));
            }
            if (response.getResultCase() != expected) {
                return CompletableFuture.failedFuture(new IOException("the game answered with " + response.getResultCase() + ", expected " + expected));
            }
            return CompletableFuture.completedFuture(result.apply(response));
        });
    }

    /**
     * Lists object templates (one page). Use {@link #catalogAll} to follow the pages.
     *
     * @param query the filters and the page; an empty query asks for the first page of everything
     * @return the entries of the page and its {@link PageInfo}
     */
    public CompletableFuture<CatalogResult> catalog(CatalogQuery query) {
        return typed(Request.newBuilder().setCatalog(query), Response.ResultCase.CATALOG, Response::getCatalog);
    }

    /**
     * Describes one live object.
     *
     * @param objectId the object id, for example from an event or a selection
     * @return the object; fails with NOT_FOUND when it does not exist (any more)
     */
    public CompletableFuture<ObjectDetail> object(int objectId) {
        return typed(Request.newBuilder().setObject(ObjectQuery.newBuilder().setObjectId(objectId)), Response.ResultCase.OBJECT, Response::getObject);
    }

    /**
     * Lists live objects in an area and/or of one player (one page). Use {@link #objectsAll} to follow the pages.
     *
     * @param query the filters and the page
     * @return the rows of the page, the templates they use and its {@link PageInfo}
     */
    public CompletableFuture<ObjectsResult> objects(ObjectsQuery query) {
        return typed(Request.newBuilder().setObjects(query), Response.ResultCase.OBJECTS, Response::getObjects);
    }

    /**
     * Describes one player: money, rank, sciences, powers and what the player can build.
     *
     * @param playerIndex the engine's player index
     * @return the player; fails with NOT_FOUND for an unknown index, UNAVAILABLE when no match runs
     */
    public CompletableFuture<PlayerDetail> player(int playerIndex) {
        return typed(Request.newBuilder().setPlayer(PlayerQuery.newBuilder().setPlayerIndex(playerIndex)), Response.ResultCase.PLAYER, Response::getPlayer);
    }

    /**
     * Describes the map: its extent, waypoints, start positions and optionally the minimap image.
     *
     * @param includeMinimap true to include the radar terrain image (about 64 KiB)
     * @return the map; fails with UNAVAILABLE when no map is loaded
     */
    public CompletableFuture<MapDetail> map(boolean includeMinimap) {
        return typed(Request.newBuilder().setMap(MapQuery.newBuilder().setIncludeMinimap(includeMinimap)), Response.ResultCase.MAP, Response::getMap);
    }

    /**
     * Asks the game to send this reader the current UiState, LobbyState, MatchInfo and a full StatsSnapshot again, for
     * example after {@link StatsListener#onFramesDropped}. Other readers are not affected. The frames arrive through
     * the listener like any others, after the response.
     *
     * @return the (empty) result; completes when the game accepted the resync
     */
    public CompletableFuture<ResyncResult> resync() {
        return typed(Request.newBuilder().setResync(ResyncQuery.getDefaultInstance()), Response.ResultCase.RESYNC, Response::getResync);
    }

    /**
     * Reads which frames this reader gets and how often.
     *
     * @return the effective feed configuration of this reader
     */
    public CompletableFuture<FeedConfig> feedConfig() {
        return typed(Request.newBuilder().setFeedConfig(FeedConfigQuery.getDefaultInstance()), Response.ResultCase.FEED_CONFIG, Response::getFeedConfig);
    }

    /**
     * Changes the feed of this reader: every field that is set in the query changes that setting, the rest stay as they
     * are, and {@code reset} returns to the defaults first. Other readers are not affected.
     *
     * @param query the changes
     * @return the effective feed configuration after the change
     */
    public CompletableFuture<FeedConfig> setFeedConfig(FeedConfigQuery query) {
        return typed(Request.newBuilder().setFeedConfig(query), Response.ResultCase.FEED_CONFIG, Response::getFeedConfig);
    }

    /**
     * Fetches every page of a catalog query, following {@code PageInfo.next_cursor} until {@code has_more} is false,
     * with at most {@value #DEFAULT_MAX_PAGES} pages. See {@link #catalogAll(CatalogQuery, int)}.
     *
     * @param query the filters; its page size is used for every page, its cursor is where to start
     * @return all entries, in the game's order
     */
    public CompletableFuture<List<CatalogEntry>> catalogAll(CatalogQuery query) {
        return catalogAll(query, DEFAULT_MAX_PAGES);
    }

    /**
     * Fetches every page of a catalog query. Pages are fetched one after the other. The future fails with the failure
     * of the first page that fails (nothing is returned from the pages before it) and with a {@link PageLimitException}
     * when {@code maxPages} pages were fetched and more remain.
     *
     * @param query the filters; its page size is used for every page, its cursor is where to start
     * @param maxPages the most pages to fetch, at least 1
     * @return all entries, in the game's order
     */
    public CompletableFuture<List<CatalogEntry>> catalogAll(CatalogQuery query, int maxPages) {
        requireMaxPages(maxPages);
        List<CatalogEntry> all = new ArrayList<>();
        CompletableFuture<Void> done = new CompletableFuture<>();
        followPages(query.getPage().getCursor(), 1, maxPages,
            cursor -> catalog(query.toBuilder().setPage(query.getPage().toBuilder().setCursor(cursor)).build()),
            CatalogResult::getPage, page -> all.addAll(page.getEntriesList()), done);
        return done.thenApply(ignored -> all);
    }

    /**
     * Fetches every page of an objects query, with at most {@value #DEFAULT_MAX_PAGES} pages. See
     * {@link #objectsAll(ObjectsQuery, int)}.
     *
     * @param query the filters; its page size is used for every page, its cursor is where to start
     * @return one result holding all rows and the templates they use
     */
    public CompletableFuture<ObjectsResult> objectsAll(ObjectsQuery query) {
        return objectsAll(query, DEFAULT_MAX_PAGES);
    }

    /**
     * Fetches every page of an objects query. The pages are fetched one after the other, so objects created or destroyed
     * in between never repeat or shift a page (the cursor is an object id). The result holds all rows, each template
     * once, and the {@link PageInfo} of the last page. Failures as for {@link #catalogAll(CatalogQuery, int)}.
     *
     * @param query the filters; its page size is used for every page, its cursor is where to start
     * @param maxPages the most pages to fetch, at least 1
     * @return one result holding all rows and the templates they use
     */
    public CompletableFuture<ObjectsResult> objectsAll(ObjectsQuery query, int maxPages) {
        requireMaxPages(maxPages);
        ObjectsResult.Builder merged = ObjectsResult.newBuilder();
        Map<Integer, TemplateRef> templates = new LinkedHashMap<>();
        CompletableFuture<Void> done = new CompletableFuture<>();
        followPages(query.getPage().getCursor(), 1, maxPages,
            cursor -> objects(query.toBuilder().setPage(query.getPage().toBuilder().setCursor(cursor)).build()),
            ObjectsResult::getPage, page -> {
                merged.addAllRows(page.getRowsList());
                for (TemplateRef template : page.getTemplatesList()) {
                    templates.putIfAbsent(template.getTemplateId(), template);
                }
                merged.setPage(page.getPage());
            }, done);
        return done.thenApply(ignored -> merged.addAllTemplates(templates.values()).build());
    }

    private static void requireMaxPages(int maxPages) {
        if (maxPages < 1) {
            throw new IllegalArgumentException("maxPages must be at least 1, got " + maxPages);
        }
    }

    /** Fetches a page, hands it to the sink and continues with the next cursor until has_more is false or the limit is hit. */
    private static <R> void followPages(int cursor, int fetched, int maxPages, IntFunction<CompletableFuture<R>> fetch,
                                        Function<R, PageInfo> info, Consumer<R> sink, CompletableFuture<Void> done) {
        fetch.apply(cursor).whenComplete((page, error) -> {
            if (error != null) {
                done.completeExceptionally(error instanceof CompletionException && error.getCause() != null ? error.getCause() : error);
                return;
            }
            sink.accept(page);
            PageInfo pageInfo = info.apply(page);
            if (!pageInfo.getHasMore()) {
                done.complete(null);
            } else if (fetched >= maxPages) {
                done.completeExceptionally(new PageLimitException(fetched));
            } else {
                followPages(pageInfo.getNextCursor(), fetched + 1, maxPages, fetch, info, sink, done);
            }
        });
    }

    // -- The reader thread.

    /** What the reader thread knows about the connection it is reading. */
    private static final class Connection {
        final SequenceTracker tracker = new SequenceTracker();
        final AtomicBoolean helloTimedOut = new AtomicBoolean();
        volatile boolean helloSeen;
    }

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
                hello = null;
                limiter = null;
                failAllPending(new IOException("the connection to the game ended" + (cause == null ? "" : ": " + Format.cause(cause)), cause));
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
        Connection connection = new Connection();
        ScheduledFuture<?> watchdog = null;
        try {
            watchdog = scheduler.schedule(() -> {
                if (!connection.helloSeen) {
                    connection.helloTimedOut.set(true);
                    try {
                        in.close();
                    } catch (IOException ignored) {
                        // the read below fails and reports the timeout
                    }
                }
            }, helloTimeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (RejectedExecutionException e) {
            return; // closed
        }
        try {
            FrameCodec.Decoder decoder = new FrameCodec.Decoder();
            ByteBuffer buffer = ByteBuffer.allocate(65536);
            while (!closed) {
                buffer.clear();
                int read = in.read(buffer);
                if (read < 0) {
                    return;
                }
                for (RawFrame frame : decoder.push(buffer.array(), 0, read)) {
                    handle(frame, connection);
                }
            }
        } catch (IOException e) {
            if (connection.helloTimedOut.get()) {
                throw new HelloTimeoutException(helloTimeout);
            }
            throw e;
        } finally {
            watchdog.cancel(false);
        }
    }

    /** Decodes once on the reader thread, checks the handshake and the sequence, then queues delivery on the executor. */
    private void handle(RawFrame frame, Connection connection) throws IOException {
        DecodedFrame decoded = FrameDispatcher.decode(frame);
        if (!connection.helloSeen) {
            acceptHello(decoded);
            connection.helloSeen = true;
        }
        long dropped = connection.tracker.observe(frame.sequence(), frame.type() == FrameType.HELLO.id());
        if (dropped > 0) {
            droppedFrames.addAndGet(dropped);
            if (state != null) {
                post(0, () -> state.onFramesDropped(dropped));
            }
            post(0, () -> listener.onFramesDropped(dropped));
        }
        clockSync.observe(frame.timestampMicros(), System.nanoTime() / 1000);
        if (decoded.message() instanceof Response response) {
            completePending(response);
        }
        post(frame.type(), () -> deliver(decoded));
    }

    /** The first frame must be a Hello with a supported protocol version. */
    private void acceptHello(DecodedFrame decoded) throws IOException {
        if (decoded.type() != FrameType.HELLO) {
            throw new FrameException(String.format("the first frame of a connection must be the Hello (type 0x09), got type 0x%02x; the game may speak an older protocol", decoded.raw().type()));
        }
        if (decoded.error() != null) {
            throw new FrameException("the Hello did not parse: " + decoded.error().getMessage());
        }
        Hello received = (Hello) decoded.message();
        Handshake.requireSupported(received);
        clockSync.reset();
        limiter = limitRequests && received.getMaxRequestsPerSecond() > 0
            ? new RequestLimiter(received.getMaxRequestsPerSecond(), Math.max(1, received.getMaxRequestBurst()))
            : null;
        synchronized (helloLock) {
            hello = received;
            helloLock.notifyAll();
        }
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
