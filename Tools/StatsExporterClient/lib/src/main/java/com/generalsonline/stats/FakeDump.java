package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.ViewStats;
import com.google.protobuf.Message;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/** A dump file loaded for the fake game: the frames the game would send, each with its place on the game time line. */
final class FakeDump {
    /**
     * One frame to send.
     *
     * @param frame the decoded frame
     * @param position game frames since the start of the dump, never going back, so a pacing clock can compare it
     */
    record Entry(DecodedFrame frame, long position) {
        FrameType type() {
            return frame.type();
        }

        Message message() {
            return frame.message();
        }
    }

    final String name;
    final List<Entry> entries;
    final Hello hello;
    final int logicFps;
    final boolean hasViewStats;
    final boolean hasInterest;
    final boolean hasCamera;
    final long firstFrame;

    private FakeDump(String name, List<Entry> entries, Hello hello, int logicFps, boolean hasViewStats, boolean hasInterest, boolean hasCamera, long firstFrame) {
        this.name = name;
        this.entries = entries;
        this.hello = hello;
        this.logicFps = logicFps;
        this.hasViewStats = hasViewStats;
        this.hasInterest = hasInterest;
        this.hasCamera = hasCamera;
        this.firstFrame = firstFrame;
    }

    /** The logic frame a message belongs to, or -1 when it carries none. */
    static long gameFrameOf(Message message) {
        if (message instanceof StatsSnapshot s) {
            return Format.u(s.getFrame());
        }
        if (message instanceof Events e) {
            return Format.u(e.getFrame());
        }
        if (message instanceof ViewStats v) {
            return Format.u(v.getFrame());
        }
        if (message instanceof InterestMap m) {
            return Format.u(m.getFrame());
        }
        if (message instanceof Camera c && c.getLogicFrame() != 0) {
            return Format.u(c.getLogicFrame());
        }
        return -1;
    }

    /**
     * Reads a dump. Hello and Response frames are not kept (the fake makes its own); frames that do not decode are
     * skipped.
     */
    static FakeDump load(Path file) throws IOException {
        List<RawFrame> raw = new ArrayList<>();
        DumpReader.readFrames(file, raw::add);
        Hello hello = null;
        int fps = 0;
        boolean view = false;
        boolean interest = false;
        boolean camera = false;
        List<DecodedFrame> frames = new ArrayList<>();
        for (RawFrame r : raw) {
            DecodedFrame d = FrameDispatcher.decode(r);
            if (d.type() == null || d.message() == null || d.type().inbound()) {
                continue;
            }
            if (d.type() == FrameType.HELLO) {
                hello = hello == null ? (Hello) d.message() : hello;
                continue;
            }
            if (d.type() == FrameType.RESPONSE) {
                continue;
            }
            if (d.message() instanceof MatchInfo m && fps == 0 && m.getLogicFramesPerSecond() > 0) {
                fps = m.getLogicFramesPerSecond();
            }
            view |= d.type() == FrameType.VIEW_STATS;
            interest |= d.type() == FrameType.INTEREST_MAP;
            camera |= d.type() == FrameType.CAMERA;
            frames.add(d);
        }
        if (fps == 0) {
            fps = hello != null && hello.getLogicFramesPerSecond() > 0 ? hello.getLogicFramesPerSecond() : 30;
        }

        List<Entry> entries = new ArrayList<>(frames.size());
        long last = -1;
        long first = 0;
        long position = 0;
        for (DecodedFrame d : frames) {
            long own = gameFrameOf(d.message());
            if (own >= 0) {
                if (last < 0) {
                    last = own;
                    first = own;
                } else if (own >= last) {
                    position += own - last;
                    last = own;
                } else if (last - own > fps) {
                    last = own; // a new match in the same dump
                }
            }
            entries.add(new Entry(d, position));
        }
        return new FakeDump(file.getFileName().toString(), List.copyOf(entries), hello, fps, view, interest, camera, first);
    }

    /** The position of the last frame: the length of the dump in game frames. */
    long length() {
        return entries.isEmpty() ? 0 : entries.get(entries.size() - 1).position();
    }
}
