package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.DumpWriter;
import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.StatEventKind;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Command line front end: watch, dump, replay, chat, gui, serve, schema, example. */
public final class Main {
    private static final long SNAPSHOT_PRINT_INTERVAL_MS = 1000;

    private Main() {
    }

    /**
     * Runs one command; see the README of the test client for the commands.
     *
     * @param args the command line
     * @throws Exception when a command fails in a way that is not a user mistake
     */
    public static void main(String[] args) throws Exception {
        // Chat text is UTF-8; do not let the console code page mangle it.
        System.setOut(new PrintStream(new FileOutputStream(FileDescriptor.out), true, StandardCharsets.UTF_8));

        CommandLine line;
        try {
            line = CommandLine.parse(args);
            run(line);
        } catch (CommandLine.UsageException e) {
            System.err.println("error: " + e.getMessage());
            System.err.println();
            System.err.print(CommandLine.usage());
            System.exit(2);
        } catch (IllegalArgumentException e) {
            System.err.println("error: " + e.getMessage());
            System.exit(2);
        } catch (IOException | UncheckedIOException e) {
            // A message for the user; a stack trace would only hide it.
            System.err.println("error: " + Format.cause(e));
            System.exit(1);
        }
    }

    private static void run(CommandLine line) throws Exception {
        int pid = line.pid();
        String pipe = line.value("pipe");
        switch (line.command()) {
            case "watch" -> {
                Set<FramePrinter.Kind> only = line.value("only") == null ? java.util.EnumSet.allOf(FramePrinter.Kind.class) : FramePrinter.parseOnly(line.value("only"));
                follow(pid, pipe, new FramePrinter(System.out, only, SNAPSHOT_PRINT_INTERVAL_MS, System::currentTimeMillis));
            }
            case "dump" -> dump(pid, pipe, Paths.get(line.positional().get(0)), line.flag("overwrite"), line.flag("no-camera"));
            case "replay" -> {
                FramePrinter printer = new FramePrinter(System.out);
                int frames = DumpReader.replay(Paths.get(line.positional().get(0)), printer);
                System.out.println("replayed " + frames + " frames" + (printer.droppedFrames() > 0 ? ", " + printer.droppedFrames() + " dropped by the game" : ""));
            }
            case "chat" -> chat(pid, pipe, line);
            case "gui" -> {
                if (line.value("replay") != null && line.value("fake") != null) {
                    throw new CommandLine.UsageException("--replay and --fake are alternatives");
                }
                StatsGui.run(pid, pipe, line.value("replay") == null ? null : Paths.get(line.value("replay")), line.value("fake") == null ? null : Paths.get(line.value("fake")), line.speed(line.value("fake") == null ? 0 : 1));
            }
            case "serve" -> serve(line, System.out);
            case "schema" -> Schema.print(System.out);
            case "example" -> example(line, System.out);
            default -> throw new CommandLine.UsageException("unknown command '" + line.command() + "'");
        }
    }

    /** Builds the pipe for a command: an explicit pipe, a pid, or discovery of every running game. */
    private static StatsPipe.Builder pipeFor(int pid, String pipe, StatsListener listener) {
        StatsPipe.Builder builder = StatsPipe.builder().listener(listener);
        if (pipe != null) {
            builder.pipe(pipe);
        } else if (pid >= 0) {
            builder.pid(pid);
        }
        return builder;
    }

    /** Connects (waiting for the game, reconnecting after a loss) and blocks until the process is stopped. */
    private static void follow(int pid, String pipe, StatsListener listener) throws InterruptedException {
        StatsPipe client = pipeFor(pid, pipe, listener).build().start();
        System.out.println("waiting for a stats pipe (Ctrl+C to stop)");
        Runtime.getRuntime().addShutdownHook(new Thread(client::close));
        Thread.currentThread().join();
    }

    private static void dump(int pid, String pipe, Path file, boolean overwrite, boolean noCamera) throws Exception {
        if (!overwrite && Files.exists(file)) {
            throw new IllegalArgumentException("'" + file + "' already exists; choose another file or pass --overwrite");
        }
        DumpWriter writer = new DumpWriter(file, overwrite);
        DumpStatus status = new DumpStatus(System.out, frame -> {
            if (noCamera && frame.type() == FrameType.CAMERA) {
                return;
            }
            try {
                writer.write(frame);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        });
        // The file is flushed once a second, not after each of the up to 120 camera frames a second.
        ScheduledExecutorService timer = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "dump-timer");
            thread.setDaemon(true);
            return thread;
        });
        timer.scheduleAtFixedRate(() -> {
            try {
                writer.flush();
            } catch (IOException e) {
                System.err.println("dump flush failed: " + Format.cause(e));
            }
            status.printLine();
        }, 1, 1, TimeUnit.SECONDS);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            timer.shutdownNow();
            try {
                writer.close();
            } catch (IOException ignored) {
                // closing on exit
            }
        }));
        System.out.println("recording to " + file + (noCamera ? " (camera frames skipped)" : ""));
        follow(pid, pipe, status);
    }

    private static void chat(int pid, String pipe, CommandLine line) throws Exception {
        List<String> words = line.positional();
        ChatChannel channel = ChatNames.channel(words.get(0));
        ChatAudience audience = ChatNames.audience(line.value("audience") == null ? "everyone" : line.value("audience"));
        String text = String.join(" ", words.subList(1, words.size()));
        StatsPipe.requireSendable(text);

        if (pipe == null && pid < 0 && com.generalsonline.stats.PipeDiscovery.findPids().isEmpty()) {
            System.err.println("error: no stats pipe found; is the game running?");
            System.exit(1);
        }
        CountDownLatch connected = new CountDownLatch(1);
        // Connect once: chat only needs the pipe for one write, and must not wait for a later game.
        StatsPipe client = pipeFor(pid, pipe, new StatsListener() {
            @Override
            public void onConnected(String pipePath) {
                connected.countDown();
            }
        }).reconnect(false).build().start();
        try {
            boolean up = connected.await(10, TimeUnit.SECONDS);
            String via = client.connectedPipe().orElse("the pipe");
            if (!up || !client.sendChat(channel, text, audience)) {
                System.err.println("error: could not connect to a free game within 10 seconds.");
                System.err.println("The game serves a limited number of readers per pipe (see Hello.max_readers), and chat needs a free slot: close a gui, watch or dump connected to it first.");
                System.exit(1);
            }
            System.out.println("wrote to " + via + " (" + channel.name() + (channel == ChatChannel.CHAT_CHANNEL_MATCH ? ", " + audience.name() : "") + "): " + text);
            System.out.println("There is no delivery feedback: the game drops the line without a reply unless that channel's screen or match is active.");
            Thread.sleep(500);
        } finally {
            client.close();
        }
    }

    /**
     * {@code serve}: starts a fake game on a dump and returns it. Package-private for the tests; {@link #serve} blocks.
     */
    static FakeGame startFake(CommandLine line, PrintStream out) throws IOException {
        if (line.value("pid") != null && line.value("pipe") != null) {
            throw new CommandLine.UsageException("--pid and --pipe are alternatives");
        }
        Path dump = Paths.get(line.positional().get(0));
        if (!Files.isRegularFile(dump)) {
            throw new IllegalArgumentException("'" + dump + "' is not a file");
        }
        FakeGame.Builder builder = FakeGame.builder(dump).speed(line.speed(1.0)).loop(line.flag("loop")).holdForReader(!line.flag("no-wait"));
        if (line.value("pipe") != null) {
            builder.pipe(line.value("pipe"));
        } else if (line.pid() >= 0) {
            builder.pid(line.pid());
        }
        if (line.value("situation") != null) {
            builder.situation(situation(line.value("situation")));
        }
        FakeGame game = builder.build().start();
        out.println("serving " + dump.getFileName() + " on " + game.pipePath() + " (pid " + game.pid() + ", "
            + (line.speed(1.0) == 0 ? "as fast as possible" : "speed " + line.speed(1.0)) + (line.flag("loop") ? ", looping" : "") + ")");
        out.println(line.flag("no-wait") ? "playing now; readers join live" : "waiting for the first reader before it starts (--no-wait starts at once)");
        return game;
    }

    private static FakeGame.Situation situation(String name) {
        try {
            return FakeGame.Situation.valueOf(name.trim().toUpperCase(Locale.ROOT));
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("unknown situation '" + name + "' (replay, observer, player)");
        }
    }

    /** Serves a dump as a fake game until the process is stopped. */
    private static void serve(CommandLine line, PrintStream out) throws Exception {
        FakeGame game = startFake(line, out);
        Runtime.getRuntime().addShutdownHook(new Thread(game::close));
        int readers = 0;
        boolean announced = false;
        while (true) {
            Thread.sleep(500);
            if (game.readerCount() != readers) {
                readers = game.readerCount();
                out.println(readers + (readers == 1 ? " reader" : " readers") + " connected");
            }
            if (game.isFinished() && !announced) {
                announced = true;
                out.println("the end of the dump was reached; the game stays up (Ctrl+C to stop)");
            }
        }
    }

    /** {@code example}: with no name, lists the scenarios; with one, prints the program for it. Package-private for the tests. */
    static void example(CommandLine line, PrintStream out) {
        if (line.positional().isEmpty()) {
            out.println("Example programs (example <name>), each complete and runnable against the library:");
            for (Examples.Scenario scenario : Examples.Scenario.values()) {
                out.println(String.format("  %-14s %s", scenario.id(), scenario.title()));
            }
            out.println();
            out.println("Options fill the program: --player NAME and --column NAME (track-player), --kind KIND (react-to-event),");
            out.println("--channel CH, --audience A and --text TEXT (send-chat), --template NAME and --side SIDE (query-catalog), --object ID (inspect-object and");
            out.println("camera-control), --x, --y and --zoom (camera-control), --speed RATIO (replay-control), --mode hotspot|highlights|overview|director (director),");
            out.println("--dump FILE (fake-server).");
            return;
        }
        String name = line.positional().get(0);
        Examples.Scenario scenario = Examples.Scenario.byId(name);
        if (scenario == null) {
            StringBuilder known = new StringBuilder();
            for (Examples.Scenario s : Examples.Scenario.values()) {
                known.append(known.length() == 0 ? "" : ", ").append(s.id());
            }
            throw new IllegalArgumentException("unknown example '" + name + "' (" + known + ")");
        }
        Examples.Selection selection = Examples.Selection.defaults();
        if (line.value("player") != null) {
            selection = selection.withPlayer(line.value("player"));
        }
        if (line.value("column") != null) {
            String column = line.value("column");
            Examples.StatColumn known = null;
            for (Examples.StatColumn candidate : Examples.statColumns()) {
                if (candidate.name().equalsIgnoreCase(column)) {
                    known = candidate;
                }
            }
            if (known == null) {
                StringBuilder names = new StringBuilder();
                for (Examples.StatColumn candidate : Examples.statColumns()) {
                    names.append(names.length() == 0 ? "" : ", ").append(candidate.name());
                }
                throw new IllegalArgumentException("unknown column '" + column + "' (" + names + ")");
            }
            selection = selection.withColumn(known.name());
        }
        if (line.value("kind") != null) {
            selection = selection.withEventKind(eventKind(line.value("kind")));
        }
        if (line.value("channel") != null) {
            selection = selection.withChannel(ChatNames.channel(line.value("channel")));
        }
        if (line.value("audience") != null) {
            selection = selection.withAudience(ChatNames.audience(line.value("audience")));
        }
        if (line.value("text") != null) {
            selection = selection.withChatText(line.value("text"));
        }
        if (line.value("template") != null || line.value("side") != null) {
            selection = selection.withTemplate(line.value("template"), line.value("side"));
        }
        if (line.value("object") != null) {
            selection = selection.withObjectId((int) whole(line, "object"));
        }
        if (line.value("x") != null || line.value("y") != null || line.value("zoom") != null) {
            selection = selection.withCamera(line.value("x") == null ? selection.cameraX() : decimal(line, "x"), line.value("y") == null ? selection.cameraY() : decimal(line, "y"),
                line.value("zoom") == null ? selection.zoom() : decimal(line, "zoom"));
        }
        if (line.value("speed") != null) {
            selection = selection.withReplaySpeed(decimal(line, "speed"));
        }
        if (line.value("mode") != null) {
            selection = selection.withDirectorMode(directorMode(line.value("mode")));
        }
        if (line.value("dump") != null) {
            selection = selection.withDumpFile(line.value("dump"));
        }
        out.print(Examples.generate(scenario, selection));
    }

    private static double decimal(CommandLine line, String option) {
        try {
            double value = Double.parseDouble(line.value(option).trim());
            if (!Double.isFinite(value)) {
                throw new NumberFormatException();
            }
            return value;
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("--" + option + " needs a number, got '" + line.value(option) + "'");
        }
    }

    private static long whole(CommandLine line, String option) {
        try {
            return Integer.parseInt(line.value(option).trim());
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("--" + option + " needs a whole number, got '" + line.value(option) + "'");
        }
    }

    private static DirectorMode directorMode(String name) {
        try {
            return DirectorMode.valueOf(name.trim().toUpperCase(Locale.ROOT));
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("unknown director mode '" + name + "' (hotspot, highlights, overview, director)");
        }
    }

    private static StatEventKind eventKind(String name) {
        String wanted = name.trim().toUpperCase(Locale.ROOT);
        for (StatEventKind kind : Examples.eventKinds()) {
            if (kind.name().equals(wanted) || Examples.kindName(kind).equals(wanted)) {
                return kind;
            }
        }
        StringBuilder names = new StringBuilder();
        for (StatEventKind kind : Examples.eventKinds()) {
            names.append(names.length() == 0 ? "" : ", ").append(Examples.kindName(kind));
        }
        throw new IllegalArgumentException("unknown event kind '" + name + "' (" + names + ")");
    }
}
