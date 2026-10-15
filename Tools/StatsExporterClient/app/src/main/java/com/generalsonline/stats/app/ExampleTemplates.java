package com.generalsonline.stats.app;

/**
 * The source text of the example programs, one template per scenario. {@code ${NAME}} marks a place that
 * {@link Examples#generate} fills from the user's selection; everything else is copied as is. The generated programs
 * are complete, depend only on the library's public API and compile without warnings (see ExamplesTest).
 *
 * <p>Every program is in the default package, so a single file can be run with {@code java -cp <jars> Name.java}.
 */
final class ExampleTemplates {
    private ExampleTemplates() {
    }

    static final String PRINT_FRAMES = """
        // Connects to a running Generals Online game and prints one line for every frame type.
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar PrintEveryFrame.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.StatsListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.proto.Camera;
        import com.generalsonline.stats.proto.ChatMessage;
        import com.generalsonline.stats.proto.Events;
        import com.generalsonline.stats.proto.LobbyState;
        import com.generalsonline.stats.proto.MatchInfo;
        import com.generalsonline.stats.proto.MatchSummary;
        import com.generalsonline.stats.proto.StatsSnapshot;
        import com.generalsonline.stats.proto.UiState;

        public class PrintEveryFrame {
            public static void main(String[] args) throws InterruptedException {
                // Every method of StatsListener has an empty default; override what you need.
                // The callbacks run on the reader thread, so keep them short.
                StatsListener listener = new StatsListener() {
                    @Override
                    public void onConnected(String pipePath) {
                        System.out.println("connected to " + pipePath);
                    }

                    @Override
                    public void onDisconnected(Throwable cause) {
                        System.out.println("disconnected" + (cause == null ? "" : ": " + cause.getMessage()));
                    }

                    @Override
                    public void onMatchInfo(MatchInfo match) {
                        System.out.println("MatchInfo " + match.getPhase() + " map " + match.getMapName() + ", " + match.getPlayersCount() + " players");
                    }

                    @Override
                    public void onStatsSnapshot(StatsSnapshot snapshot) {
                        // About ten a second: a light one every 100 ms of game time, a full one every 250 ms.
                        System.out.println("StatsSnapshot frame " + snapshot.getFrame() + (snapshot.getFull() ? " (full)" : " (light)")
                            + ", " + snapshot.getPlayersCount() + " players");
                    }

                    @Override
                    public void onMatchSummary(MatchSummary summary) {
                        System.out.println("MatchSummary " + summary.getPlayersCount() + " players");
                    }

                    @Override
                    public void onChatMessage(ChatMessage chat) {
                        System.out.println("ChatMessage " + chat.getSenderName() + ": " + chat.getText());
                    }

                    @Override
                    public void onUiState(UiState ui) {
                        System.out.println("UiState " + ui.getScreen());
                    }

                    @Override
                    public void onLobbyState(LobbyState lobby) {
                        System.out.println("LobbyState " + (lobby.getInLobby() ? lobby.getSlotsCount() + " slots" : "left the lobby"));
                    }

                    @Override
                    public void onEvents(Events events) {
                        System.out.println("Events frame " + events.getFrame() + ", " + events.getEventsCount() + " events");
                    }

                    @Override
                    public void onCamera(Camera camera) {
                        // Up to 120 a second: printing every one would flood the console.
                    }

                    @Override
                    public void unknownFrame(int type, byte[] body) {
                        System.out.println("unknown frame type 0x" + Integer.toHexString(type));
                    }

                    @Override
                    public void onError(int type, Throwable cause) {
                        // A callback above threw; the stream keeps going.
                        System.err.println("error while handling frame type 0x" + Integer.toHexString(type) + ": " + cause);
                    }
                };

                // No pid or pipe given: every running game is tried, highest pid first, and the first free one is used.
                // The pipe waits for the game to start and reconnects when it restarts.
                StatsPipe pipe = StatsPipe.builder().listener(listener).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
                System.out.println("waiting for a game (Ctrl+C to stop)");
                Thread.currentThread().join();
            }
        }
        """;

    static final String TRACK_PLAYER = """
        // Follows one player's "${COLUMN}" during a match: prints a line every time the value changes.
        // The value is ${EXPRESSION}${NOTE}
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar TrackPlayerStat.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.Format;
        import com.generalsonline.stats.MatchState;
        import com.generalsonline.stats.PlayerView;
        import com.generalsonline.stats.StateListener;
        import com.generalsonline.stats.StatsPipe;

        import java.util.List;

        public class TrackPlayerStat {
            // The player to follow, by the name the lobby shows.
            private static final String PLAYER = "${PLAYER}";

            public static void main(String[] args) throws InterruptedException {
                // MatchState joins every snapshot with the MatchInfo of the player, and keeps the heavy fields of the
                // last full snapshot while light snapshots arrive, so a PlayerView is always complete.
                MatchState state = new MatchState();
                state.addListener(new StateListener() {
                    private String last = "";

                    @Override
                    public void playersChanged(List<PlayerView> players) {
                        for (PlayerView player : players) {
                            if (!player.name().equals(PLAYER)) {
                                continue;
                            }
                            String value = String.valueOf(${EXPRESSION});
                            if (!value.equals(last)) {
                                last = value;
                                System.out.println(state.gameTime(player.frame()) + "  " + player.name() + "  ${COLUMN} = " + value);
                            }
                        }
                    }
                });

                StatsPipe pipe = StatsPipe.builder().state(state).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
                System.out.println("following " + PLAYER + " (Ctrl+C to stop)");
                Thread.currentThread().join();
            }
        }
        """;

    static final String REACT_TO_EVENT = """
        // Reacts to ${KIND} events: ${KIND_DESCRIPTION}
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar ReactToEvent.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.Format;
        import com.generalsonline.stats.MatchState;
        import com.generalsonline.stats.StateListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.proto.StatEvent;
        import com.generalsonline.stats.proto.StatEventKind;

        import java.util.List;

        public class ReactToEvent {
            public static void main(String[] args) throws InterruptedException {
                MatchState state = new MatchState();
                state.addListener(new StateListener() {
                    @Override
                    public void eventsAdded(List<StatEvent> added) {
                        for (StatEvent e : added) {
                            if (e.getKind() != StatEventKind.${KIND_CONSTANT}) {
                                continue;
                            }
                            // The fields of a StatEvent mean different things per kind; these are the ones of ${KIND}.
                            System.out.println(state.gameTime(Format.u(e.getFrame())) + "  ${KIND}  " + state.playerName(e.getPlayerIndex()));
        ${FIELDS}
                        }
                    }
                });

                StatsPipe pipe = StatsPipe.builder().state(state).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
                System.out.println("waiting for ${KIND} events (Ctrl+C to stop)");
                Thread.currentThread().join();
            }
        }
        """;

    static final String CAMERA_BOX = """
        // Maps the camera's view box onto a minimap image: the four ground corners of the screen, in pixels.
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar DrawCameraBox.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.MatchState;
        import com.generalsonline.stats.StateListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.WorldExtent;
        import com.generalsonline.stats.proto.Camera;

        public class DrawCameraBox {
            // The size of the minimap image you draw into, in pixels (the map is scaled to fit; this sketch keeps both axes independent).
            private static final int MAP_WIDTH = 256;
            private static final int MAP_HEIGHT = 256;

            public static void main(String[] args) throws InterruptedException {
                MatchState state = new MatchState();
                state.addListener(new StateListener() {
                    private long lastPrint;

                    @Override
                    public void cameraChanged(Camera camera) {
                        // Camera frames arrive up to 120 times a second, whenever the view changed. A real overlay stores the
                        // latest one and draws from its own timer; this sketch just prints twice a second.
                        long now = System.nanoTime();
                        if (now - lastPrint < 500_000_000L || !camera.getHasViewBox()) {
                            return;
                        }
                        WorldExtent map = state.worldExtent().orElse(null);
                        if (map == null) {
                            return; // the extent comes with the STARTED MatchInfo; there is none while the map loads
                        }
                        lastPrint = now;

                        // The corners in the order the game's own minimap box uses: top left, top right, bottom right, bottom left.
                        float[][] corners = {
                            {camera.getViewTlX(), camera.getViewTlY()},
                            {camera.getViewTrX(), camera.getViewTrY()},
                            {camera.getViewBrX(), camera.getViewBrY()},
                            {camera.getViewBlX(), camera.getViewBlY()}};
                        StringBuilder line = new StringBuilder("view box:");
                        for (float[] corner : corners) {
                            // World y points north and pixel y points down, so y is flipped.
                            long px = Math.round((corner[0] - map.minX()) / map.width() * MAP_WIDTH);
                            long py = Math.round((map.maxY() - corner[1]) / map.height() * MAP_HEIGHT);
                            line.append(" (").append(px).append(", ").append(py).append(')');
                        }
                        System.out.println(line);
                        // camera.getX()/getY() is the point the camera looks at; Format.viewBox(camera) gives centre, size and rotation.
                    }
                });

                StatsPipe pipe = StatsPipe.builder().state(state).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
                System.out.println("waiting for camera frames (Ctrl+C to stop)");
                Thread.currentThread().join();
            }
        }
        """;

    static final String LOBBY = """
        // Shows the game setup lobby with the predicted faction of every random slot.
        // The game predicts the random sides, colours and start positions by running its own resolution on a copy of the
        // lobby state, so an overlay can show them before the match starts. A prediction is only valid when the lobby says so.
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar ShowLobby.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.Format;
        import com.generalsonline.stats.StatsListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.proto.LobbySlot;
        import com.generalsonline.stats.proto.LobbyState;
        import com.generalsonline.stats.proto.MatchPlayer;

        public class ShowLobby {
            public static void main(String[] args) throws InterruptedException {
                StatsListener listener = new StatsListener() {
                    @Override
                    public void onLobbyState(LobbyState lobby) {
                        if (!lobby.getInLobby()) {
                            System.out.println("left the lobby");
                            return;
                        }
                        System.out.println("lobby on " + Format.mapDisplayName(lobby.getMapName())
                            + " (sides and colours predicted: " + lobby.getPredictionValid()
                            + ", start positions predicted: " + lobby.getPredictionStartPositionsValid() + ")");
                        for (LobbySlot slot : lobby.getSlotsList()) {
                            if (!slot.hasPlayer()) {
                                continue; // an open or closed slot
                            }
                            MatchPlayer player = slot.getPlayer();
                            String side = player.getSide();
                            if (player.getIsSideRandom()) {
                                side = lobby.getPredictionValid() ? "Random -> " + slot.getPredictedSide() : "Random";
                            }
                            String color = player.getIsColorRandom() ? "random" : Format.colorHex(player.getColor());
                            if (player.getIsColorRandom() && lobby.getPredictionValid() && slot.getPredictedColor() != 0) {
                                color = "random -> " + Format.colorHex(slot.getPredictedColor());
                            }
                            String start = lobby.getPredictionStartPositionsValid() ? Integer.toString(slot.getPredictedStartPosition()) : "?";
                            System.out.println("  slot " + (player.getLobbySlot() + 1) + "  " + player.getName() + "  " + side
                                + "  colour " + color + "  start position " + start + (slot.getIsReady() ? "  ready" : ""));
                        }
                    }
                };

                StatsPipe pipe = StatsPipe.builder().listener(listener).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
                System.out.println("waiting for a lobby (Ctrl+C to stop)");
                Thread.currentThread().join();
            }
        }
        """;

    static final String SEND_CHAT = """
        // Sends one chat line to ${CHANNEL_DESCRIPTION}.
        //
        // Things to know before you rely on this:
        //  * The game serves up to four readers per pipe (Hello.max_readers). This program needs a free slot, so it fails
        //    while that many other clients (the test client GUI, a watcher, an overlay) are connected to the same game.
        //  * The text must not be empty and at most 1024 bytes in UTF-8; StatsPipe.requireSendable checks that.
        //  * There is no reply. A true result only means the bytes were written; the game drops the line unless that
        //    channel's screen or match is active. Slash commands are handled exactly as if you had typed them.
        //  * The audience matters for the match channel only: EVERYONE, ALLIES, or OBSERVERS (observers only).
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar SendChat.java "your text"
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.StatsListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.proto.ChatAudience;
        import com.generalsonline.stats.proto.ChatChannel;

        import java.util.concurrent.CountDownLatch;
        import java.util.concurrent.TimeUnit;

        public class SendChat {
            public static void main(String[] args) throws Exception {
                String text = args.length > 0 ? String.join(" ", args) : "${TEXT}";
                StatsPipe.requireSendable(text); // throws IllegalArgumentException with a readable message

                CountDownLatch connected = new CountDownLatch(1);
                // reconnect(false): connect once and stop; this program only needs the pipe for one write.
                StatsPipe pipe = StatsPipe.builder().reconnect(false).listener(new StatsListener() {
                    @Override
                    public void onConnected(String pipePath) {
                        connected.countDown();
                    }
                }).build().start();
                try {
                    if (!connected.await(10, TimeUnit.SECONDS)) {
                        System.err.println("no game found within 10 seconds: is it running, and is another client connected to it?");
                        return;
                    }
                    boolean written = pipe.sendChat(ChatChannel.${CHANNEL_CONSTANT}, text${AUDIENCE_ARGUMENT});
                    System.out.println(written ? "written to the pipe (the game gives no reply)" : "not connected");
                    Thread.sleep(500); // let the write finish before the pipe is closed
                } finally {
                    pipe.close();
                }
            }
        }
        """;

    static final String RECORD_REPLAY = """
        // Records the raw frames of a running game to a dump file, then replays the file at twice real time into the
        // same listener. Handler is an ordinary StatsListener: it does not know whether the frames come from a game or a file.
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar RecordAndReplay.java record match.dump
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar RecordAndReplay.java replay match.dump
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.DecodedFrame;
        import com.generalsonline.stats.DumpReader;
        import com.generalsonline.stats.DumpWriter;
        import com.generalsonline.stats.Format;
        import com.generalsonline.stats.StatsListener;
        import com.generalsonline.stats.StatsPipe;
        import com.generalsonline.stats.proto.StatsSnapshot;

        import java.io.IOException;
        import java.io.UncheckedIOException;
        import java.nio.file.Path;
        import java.util.concurrent.Executors;
        import java.util.concurrent.ScheduledExecutorService;
        import java.util.concurrent.TimeUnit;

        public class RecordAndReplay {
            /** Prints a line every ten seconds of game time, and writes every frame to the dump when it has one. */
            static final class Handler implements StatsListener {
                private final DumpWriter writer;
                private long lastBucket = -1;

                Handler(DumpWriter writer) {
                    this.writer = writer;
                }

                @Override
                public void onFrame(DecodedFrame frame) {
                    // Called for every frame, also unknown ones, before the typed method: the place for a recorder.
                    if (writer != null) {
                        try {
                            writer.write(frame);
                        } catch (IOException e) {
                            throw new UncheckedIOException(e); // reported through onError, the stream continues
                        }
                    }
                }

                @Override
                public void onStatsSnapshot(StatsSnapshot snapshot) {
                    long bucket = Format.seconds(Format.u(snapshot.getFrame()), 30) / 10; // assumes 30 logic frames per second
                    if (bucket != lastBucket) {
                        lastBucket = bucket;
                        System.out.println("game time " + Format.gameTime(Format.u(snapshot.getFrame()), 30) + "  " + snapshot.getPlayersCount() + " players");
                    }
                }

                @Override
                public void onError(int type, Throwable cause) {
                    System.err.println("error while handling frame type 0x" + Integer.toHexString(type) + ": " + cause);
                }
            }

            public static void main(String[] args) throws Exception {
                if (args.length != 2 || !(args[0].equals("record") || args[0].equals("replay"))) {
                    System.err.println("usage: RecordAndReplay record|replay <file>");
                    return;
                }
                Path file = Path.of(args[1]);
                if (args[0].equals("record")) {
                    record(file);
                } else {
                    replay(file);
                }
            }

            static void record(Path file) throws Exception {
                DumpWriter writer = new DumpWriter(file, false); // false: refuse to overwrite an existing file
                // Up to 120 camera frames a second are written: flush once a second, not per frame.
                ScheduledExecutorService flusher = Executors.newSingleThreadScheduledExecutor();
                flusher.scheduleAtFixedRate(() -> {
                    try {
                        writer.flush();
                    } catch (IOException e) {
                        System.err.println("flush failed: " + e.getMessage());
                    }
                }, 1, 1, TimeUnit.SECONDS);

                StatsPipe pipe = StatsPipe.builder().listener(new Handler(writer)).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                    pipe.close();
                    flusher.shutdownNow();
                    try {
                        writer.close();
                    } catch (IOException e) {
                        System.err.println("closing the dump failed: " + e.getMessage());
                    }
                }));
                System.out.println("recording to " + file + " (Ctrl+C to stop)");
                Thread.currentThread().join();
            }

            static void replay(Path file) throws IOException {
                // The speed is a multiplier of game time: 1.0 is real time, 2.0 twice as fast, 0 as fast as possible.
                int frames = DumpReader.replay(file, new Handler(null), 2.0);
                System.out.println("replayed " + frames + " frames");
            }
        }
        """;

    static final String UI_THREAD = """
        // Uses MatchState from a UI thread. The pattern is the same for Swing, JavaFX or any other toolkit:
        //  * give the pipe an executor that runs tasks on the UI thread, in order (Swing: SwingUtilities::invokeLater,
        //    JavaFX: Platform::runLater); the reader thread then only decodes, and every callback, including the
        //    state change callbacks, runs on the UI thread;
        //  * update the widgets from the StateListener methods, which are called after the state was updated;
        //  * MatchState itself is thread safe and returns immutable values, so any other thread may read it too.
        // Camera frames arrive up to 120 times a second; if the UI only needs the latest one, do not repaint per frame.
        //
        // Run (Java 17+), with the library jar and protobuf-java on the class path:
        //   java -cp stats-pipe-client-2.1.0.jar:protobuf-java-4.33.4.jar UiThreadState.java
        // (use ; instead of : between the jars on Windows)
        import com.generalsonline.stats.MatchState;
        import com.generalsonline.stats.PlayerView;
        import com.generalsonline.stats.StateListener;
        import com.generalsonline.stats.StatsPipe;

        import javax.swing.JFrame;
        import javax.swing.JLabel;
        import javax.swing.SwingUtilities;
        import java.util.List;
        import java.util.concurrent.Executor;

        public class UiThreadState {
            public static void main(String[] args) throws Exception {
                MatchState state = new MatchState();
                JLabel label = new JLabel("waiting for a game...");
                SwingUtilities.invokeAndWait(() -> {
                    JFrame window = new JFrame("Generals Online match state");
                    window.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
                    window.add(label);
                    window.setSize(520, 90);
                    window.setVisible(true);
                });

                state.addListener(new StateListener() {
                    @Override
                    public void playersChanged(List<PlayerView> players) {
                        // Runs on the Swing event thread because of the executor below.
                        StringBuilder text = new StringBuilder(state.gameTime(state.frame()));
                        for (PlayerView player : players) {
                            text.append("   ").append(player.name()).append(' ').append(player.stats().getMoney()).append(" (").append(player.stateText()).append(')');
                        }
                        label.setText(text.toString());
                    }

                    @Override
                    public void matchReset() {
                        label.setText("new match");
                    }
                });

                Executor uiThread = SwingUtilities::invokeLater; // JavaFX: Platform::runLater
                StatsPipe pipe = StatsPipe.builder().state(state).executor(uiThread).build().start();
                Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
            }
        }
        """;
}
