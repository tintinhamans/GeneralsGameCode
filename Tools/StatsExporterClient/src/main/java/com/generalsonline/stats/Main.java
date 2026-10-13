package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

/** Command line front end: watch, dump, replay, chat, schema. */
public final class Main {
    private Main() {
    }

    public static void main(String[] args) throws Exception {
        // Chat text is UTF-8; do not let the console code page mangle it.
        System.setOut(new PrintStream(new FileOutputStream(FileDescriptor.out), true, StandardCharsets.UTF_8));

        if (args.length == 0) {
            usage();
            System.exit(2);
        }

        int pid = -1;
        String pipe = null;
        String audienceName = "everyone";
        String replayFile = null;
        int speed = 0;
        List<String> rest = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            switch (args[i]) {
                case "--pid" -> pid = Integer.parseInt(args[++i]);
                case "--pipe" -> pipe = args[++i];
                case "--audience" -> audienceName = args[++i];
                case "--replay" -> replayFile = args[++i];
                case "--speed" -> speed = Integer.parseInt(args[++i]);
                default -> rest.add(args[i]);
            }
        }

        switch (args[0]) {
            case "watch" -> watch(pid, pipe, null);
            case "dump" -> {
                requireArgs(rest, 1, "dump <file>");
                watch(pid, pipe, Paths.get(rest.get(0)));
            }
            case "replay" -> {
                requireArgs(rest, 1, "replay <file>");
                int frames = DumpFile.replay(Paths.get(rest.get(0)), new FramePrinter(System.out));
                System.out.println("replayed " + frames + " frames");
            }
            case "chat" -> {
                requireArgs(rest, 2, "chat <channel> <text>");
                chat(pid, pipe, ChatNames.channel(rest.get(0)), String.join(" ", rest.subList(1, rest.size())), ChatNames.audience(audienceName));
            }
            case "gui" -> StatsGui.run(pid, pipe, replayFile == null ? null : Paths.get(replayFile), speed);
            case "schema" -> Schema.print(System.out);
            default -> {
                usage();
                System.exit(2);
            }
        }
    }

    private static void usage() {
        System.err.println("usage: stats-exporter-client <command> [options]");
        System.err.println("  watch  [--pid N | --pipe PATH]            print every frame");
        System.err.println("  dump   <file> [--pid N | --pipe PATH]     print and record raw frames to <file>");
        System.err.println("  replay <file>                             print a recorded dump");
        System.err.println("  chat   <channel> <text> [--audience A] [--pid N | --pipe PATH]");
        System.err.println("         channels: match game-lobby room lan-lobby lan-game; audiences: everyone allies observers");
        System.err.println("  gui    [--pid N | --pipe PATH | --replay FILE [--speed FRAMES_PER_SECOND]]   show the feed in a window");
        System.err.println("  schema                                    print the frame table and messages");
    }

    private static void requireArgs(List<String> rest, int count, String form) {
        if (rest.size() < count) {
            throw new IllegalArgumentException("usage: " + form);
        }
    }

    private static String fixedOrDiscovered(int pid, String pipe) {
        if (pipe != null) {
            return pipe;
        }
        return PipeDiscovery.choosePipe(pid);
    }

    private static void watch(int pid, String pipe, Path dumpFile) throws Exception {
        DumpFile.Writer dump = dumpFile == null ? null : new DumpFile.Writer(dumpFile);
        StatsPipeClient client = new StatsPipeClient(() -> fixedOrDiscovered(pid, pipe), new FramePrinter(System.out));
        if (dump != null) {
            client.setRawTap(frame -> {
                try {
                    dump.write(frame);
                    dump.flush();
                } catch (IOException e) {
                    System.err.println("dump write failed: " + e);
                }
            });
        }
        client.start();
        System.out.println("waiting for a stats pipe (Ctrl+C to stop)");
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            client.close();
            if (dump != null) {
                try {
                    dump.close();
                } catch (IOException ignored) {
                    // closing on exit
                }
            }
        }));
        Thread.currentThread().join();
    }

    private static void chat(int pid, String pipe, ChatChannel channel, String text, ChatAudience audience) throws Exception {
        String path = fixedOrDiscovered(pid, pipe);
        if (path == null) {
            System.err.println("no stats pipe found");
            System.exit(1);
        }
        Object lock = new Object();
        boolean[] connected = {false};
        StatsPipeClient client = new StatsPipeClient(() -> path, new FrameListener() {
            @Override
            public void onConnected(String pipePath) {
                synchronized (lock) {
                    connected[0] = true;
                    lock.notifyAll();
                }
            }
        });
        client.start();
        try {
            synchronized (lock) {
                long deadline = System.currentTimeMillis() + 10000;
                while (!connected[0] && System.currentTimeMillis() < deadline) {
                    lock.wait(250);
                }
            }
            if (!client.sendChat(channel, text, audience)) {
                System.err.println("could not connect to " + path);
                System.exit(1);
            }
            System.out.println("sent to " + channel.name() + ": " + text);
            Thread.sleep(500);
        } finally {
            client.close();
        }
    }
}
