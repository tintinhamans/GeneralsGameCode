package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.RawFrame;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.StatsSnapshot;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Plays a recording into the window's feed on its own thread, paced by game time. Unlike {@link DumpReader#replay} it can be
 * paused, resumed, sped up and slowed down while it runs, and it reports how far it has read.
 */
final class RecordingPlayer {
    private static final int BACKLOG_LIMIT = 3000;

    private final Path file;
    private final EdtFeed feed;
    private final long fileBytes;
    private final Object lock = new Object();
    private volatile double speed;
    private volatile boolean paused;
    private volatile boolean stopped;
    private volatile boolean finished;
    private volatile boolean reanchor = true;
    private volatile long readBytes;
    private volatile String failure;
    private long anchorFrame = -1;
    private long anchorNanos;
    private long lastFrame = -1;
    private int fps = 30;

    /** @param speed multiplier of game time, 1 is real time; 0 plays as fast as the window can take it */
    RecordingPlayer(Path file, EdtFeed feed, double speed) {
        this.file = file;
        this.feed = feed;
        this.speed = speed;
        long size = 1;
        try {
            size = Math.max(1, Files.size(file));
        } catch (IOException e) {
            // the read below reports it
        }
        this.fileBytes = size;
    }

    void start() {
        Thread thread = new Thread(this::run, "recording-player");
        thread.setDaemon(true);
        thread.start();
    }

    private void run() {
        try {
            DumpReader.readFrames(file, this::deliver);
        } catch (Stop e) {
            return;
        } catch (IOException | RuntimeException e) {
            failure = Format.cause(e);
        }
        finished = true;
    }

    private void deliver(RawFrame raw) {
        DecodedFrame decoded = FrameDispatcher.decode(raw);
        if (decoded.message() instanceof MatchInfo) {
            int rate = ((MatchInfo) decoded.message()).getLogicFramesPerSecond();
            if (rate > 0) {
                fps = rate;
            }
        }
        try {
            gate(gameFrameOf(decoded));
            while (feed.backlog() > BACKLOG_LIMIT && !stopped) {
                Thread.sleep(2);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new Stop();
        }
        if (stopped) {
            throw new Stop();
        }
        FrameDispatcher.deliver(decoded, feed);
        readBytes += decoded.wireBytes();
    }

    /** Waits while paused, then until the frame is due at the current speed. */
    private void gate(long gameFrame) throws InterruptedException {
        while (true) {
            synchronized (lock) {
                while (paused && !stopped) {
                    lock.wait();
                }
            }
            if (stopped) {
                throw new Stop();
            }
            double rate = speed;
            if (rate <= 0 || gameFrame < 0) {
                return;
            }
            long now = System.nanoTime();
            if (reanchor || anchorFrame < 0 || gameFrame + fps < lastFrame) {
                reanchor = false;
                anchorFrame = gameFrame;
                anchorNanos = now;
                lastFrame = gameFrame;
                return;
            }
            lastFrame = Math.max(lastFrame, gameFrame);
            long due = anchorNanos + (long) ((gameFrame - anchorFrame) / (double) fps / rate * 1e9) - now;
            if (due <= 0) {
                return;
            }
            synchronized (lock) {
                lock.wait(Math.min(50, due / 1_000_000 + 1));
            }
        }
    }

    private static long gameFrameOf(DecodedFrame frame) {
        if (frame.message() instanceof StatsSnapshot) {
            return Format.u(((StatsSnapshot) frame.message()).getFrame());
        }
        if (frame.message() instanceof Events) {
            return Format.u(((Events) frame.message()).getFrame());
        }
        return -1;
    }

    void pause() {
        paused = true;
    }

    void resume() {
        synchronized (lock) {
            paused = false;
            reanchor = true;
            lock.notifyAll();
        }
    }

    void setSpeed(double speed) {
        synchronized (lock) {
            this.speed = speed;
            reanchor = true;
            lock.notifyAll();
        }
    }

    void stop() {
        synchronized (lock) {
            stopped = true;
            lock.notifyAll();
        }
    }

    Path file() {
        return file;
    }

    boolean isPaused() {
        return paused;
    }

    boolean isFinished() {
        return finished;
    }

    double speed() {
        return speed;
    }

    /** How much of the file has been played, 0 to 1. */
    double progress() {
        return finished ? 1.0 : Math.min(1.0, readBytes / (double) fileBytes);
    }

    /** Non-null when the file could not be read to its end. */
    String failure() {
        return failure;
    }

    private static final class Stop extends RuntimeException {
        private static final long serialVersionUID = 1L;

        Stop() {
            super(null, null, false, false);
        }
    }
}
