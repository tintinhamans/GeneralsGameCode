package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.Handshake;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.StatsSnapshot;

import java.io.PrintStream;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

/**
 * What the {@code dump} command prints instead of every frame: one status line per second, plus a line when the
 * connection or the match phase changes. {@link #onFrame} is called on the reader thread, {@link #printLine} by the
 * once-a-second timer.
 */
final class DumpStatus implements StatsListener {
    private static final DateTimeFormatter CLOCK = DateTimeFormatter.ofPattern("HH:mm:ss");

    private final PrintStream out;
    private final AtomicLong frames = new AtomicLong();
    private final AtomicLong cameraFrames = new AtomicLong();
    private final AtomicLong bytes = new AtomicLong();
    private final AtomicLong errors = new AtomicLong();
    private final AtomicLong dropped = new AtomicLong();
    private volatile long gameFrame = -1;
    private volatile int logicFps = MatchState.DEFAULT_LOGIC_FPS;
    private volatile String phase = "-";
    private long previousFrames;
    private long previousCamera;

    private final Consumer<DecodedFrame> recorder;

    DumpStatus(PrintStream out) {
        this(out, null);
    }

    /** @param recorder called with every frame after it was counted (the dump file writer); an exception it throws goes to onError, or null */
    DumpStatus(PrintStream out, Consumer<DecodedFrame> recorder) {
        this.recorder = recorder;
        this.out = out;
    }

    /** Counts a frame, then gives it to the recorder; the frame handler of the dump command. */
    @Override
    public void onFrame(DecodedFrame frame) {
        frames.incrementAndGet();
        bytes.addAndGet(frame.wireBytes());
        if (frame.type() == FrameType.CAMERA) {
            cameraFrames.incrementAndGet();
        }
        if (frame.message() instanceof StatsSnapshot s) {
            gameFrame = Format.u(s.getFrame());
        }
        if (recorder != null) {
            recorder.accept(frame);
        }
    }

    /** The status line for the time since the previous call; call from one thread (the timer). */
    synchronized String statusLine() {
        long total = frames.get();
        long camera = cameraFrames.get();
        String game = gameFrame < 0 ? "-" : Format.gameTime(gameFrame, logicFps) + " (frame " + gameFrame + ")";
        String line = String.format("frames %d (+%d/s, camera +%d/s)  %.1f KB  game time %s  match %s  errors %d  dropped %d",
            total, total - previousFrames, camera - previousCamera, bytes.get() / 1024.0, game, phase, errors.get(), dropped.get());
        previousFrames = total;
        previousCamera = camera;
        return line;
    }

    void printLine() {
        out.println("[" + LocalTime.now().format(CLOCK) + "] " + statusLine());
    }

    @Override
    public void onConnected(String pipePath) {
        out.println("CONNECTED " + pipePath);
    }

    @Override
    public void onDisconnected(Throwable cause) {
        out.println("DISCONNECTED " + (cause == null ? "" : Format.cause(cause)));
    }

    @Override
    public void onError(int type, Throwable cause) {
        errors.incrementAndGet();
    }

    @Override
    public void onHello(Hello hello) {
        out.println("HELLO " + Handshake.summary(hello) + ", build " + hello.getGameBuild());
    }

    @Override
    public void onFramesDropped(long count) {
        dropped.addAndGet(count);
    }

    @Override
    public void onMatchInfo(MatchInfo m) {
        if (m.getLogicFramesPerSecond() > 0) {
            logicFps = m.getLogicFramesPerSecond();
        }
        phase = Format.strip(m.getPhase().name(), "MATCH_PHASE_");
        out.println("MATCH " + phase + " id=" + m.getMatchId() + " map=" + m.getMapName());
    }
}
