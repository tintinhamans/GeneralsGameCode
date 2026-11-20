package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.proto.Camera;

import com.generalsonline.stats.proto.StatsSnapshot;

import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/**
 * The hand-over from the reader thread to the event thread. It is the {@link StatsListener} of the pipe (or of a replay): every
 * frame arrives in {@link #onFrame}, which the pipe calls before the typed methods, so no frame type can bypass it.
 *
 * <p>The reader thread calls {@link #onFrame} and the lifecycle methods. Everything is
 * queued in order and one event-thread task drains the whole queue, so a burst of frames costs one task, not one per
 * frame. {@link GuiModel#accept} is the single entry that applies a frame, and it goes through
 * {@link FrameDispatcher#deliver}, so no frame type can be left out of the forwarding.
 *
 * <p>Camera frames (up to 120 a second) are not queued: only the newest is kept and {@link #applyCamera}, called from a
 * UI timer, hands it to the model.
 */
final class EdtFeed implements StatsListener {
    private final GuiModel model;
    private final FeedHealth health;
    private final Executor eventThread;
    private final Queue<Runnable> pending = new ConcurrentLinkedQueue<>();
    private final AtomicBoolean drainScheduled = new AtomicBoolean();
    private final AtomicReference<Camera> latestCamera = new AtomicReference<>();
    private final AtomicLong camerasSinceApplied = new AtomicLong();
    private final AtomicInteger queued = new AtomicInteger();
    private final AtomicReference<LightTask> lastLight = new AtomicReference<>();
    private volatile Consumer<DecodedFrame> recorder;

    /** @param eventThread runs tasks on the Swing event thread (SwingUtilities::invokeLater); tests pass Runnable::run */
    EdtFeed(GuiModel model, FeedHealth health, Executor eventThread) {
        this.model = model;
        this.health = health;
        this.eventThread = eventThread;
    }

    @Override
    public void onFrame(DecodedFrame frame) {
        health.record(frame, System.nanoTime());
        Consumer<DecodedFrame> recording = recorder;
        if (recording != null) {
            recording.accept(frame);
        }
        if (frame.type() == FrameType.CAMERA && frame.message() instanceof Camera) {
            latestCamera.set((Camera) frame.message());
            camerasSinceApplied.incrementAndGet();
            return;
        }
        String summary = FrameSummary.of(frame);
        if (frame.message() instanceof StatsSnapshot && !((StatsSnapshot) frame.message()).getFull()) {
            postLight(frame, summary);
            return;
        }
        post(() -> model.accept(frame, summary));
    }

    /** Where every frame goes before it is shown (the recording); null for none. Called on the reader thread. */
    void setRecorder(Consumer<DecodedFrame> recorder) {
        this.recorder = recorder;
    }

    /** The number of tasks waiting for the event thread. */
    int backlog() {
        return queued.get();
    }

    /**
     * A light snapshot (up to 60 a second with the every-frame option) replaces the one before it while that one is still
     * waiting: the window shows the newest numbers and a slow event thread never falls behind. Anything queued after it
     * ends the run, so the order against events and full snapshots is kept.
     */
    private void postLight(DecodedFrame frame, String summary) {
        Held held = new Held(frame, summary);
        LightTask task = lastLight.get();
        if (task != null) {
            Held old = task.held.get();
            if (old != null && task.held.compareAndSet(old, held)) {
                return;
            }
        }
        task = new LightTask(held);
        enqueue(task);
        lastLight.set(task);
    }

    private static final class Held {
        private final DecodedFrame frame;
        private final String summary;

        Held(DecodedFrame frame, String summary) {
            this.frame = frame;
            this.summary = summary;
        }
    }

    private final class LightTask implements Runnable {
        private final AtomicReference<Held> held;

        LightTask(Held first) {
            held = new AtomicReference<>(first);
        }

        @Override
        public void run() {
            Held now = held.getAndSet(null);
            if (now != null) {
                model.accept(now.frame, now.summary);
            }
        }
    }

    /** Runs a task on the event thread, in order with the frames. */
    void post(Runnable task) {
        lastLight.set(null);
        enqueue(task);
    }

    private void enqueue(Runnable task) {
        queued.incrementAndGet();
        pending.add(task);
        if (drainScheduled.compareAndSet(false, true)) {
            eventThread.execute(this::drain);
        }
    }

    /** Runs everything queued so far at once; the window calls it (on the event thread) before it switches to another source. */
    void flush() {
        drain();
    }

    private void drain() {
        // Cleared before polling: a task queued after the last poll schedules a new drain instead of being stranded.
        drainScheduled.set(false);
        model.beginBatch();
        try {
            Runnable task;
            while ((task = pending.poll()) != null) {
                queued.decrementAndGet();
                try {
                    task.run();
                } catch (RuntimeException e) {
                    model.onError(0, e);
                }
            }
        } finally {
            model.endBatch();
        }
    }

    /** Event thread, from a timer: gives the model the newest camera frame since the last call, if any. */
    void applyCamera() {
        Camera camera = latestCamera.getAndSet(null);
        long count = camerasSinceApplied.getAndSet(0);
        if (camera != null) {
            model.onCamera(camera, count);
        }
    }

    /** Drops a camera frame that has not been applied yet; a new match must not show the old match's view. */
    void clearCamera() {
        latestCamera.set(null);
        camerasSinceApplied.set(0);
    }

    // -- Lifecycle callbacks (reader thread), queued in order with the frames.

    @Override
    public void onConnected(String pipePath) {
        post(() -> model.onConnected(pipePath));
    }

    @Override
    public void onDisconnected(Throwable cause) {
        post(() -> model.onDisconnected(cause));
    }

    @Override
    public void onError(int type, Throwable cause) {
        post(() -> model.onError(type, cause));
    }

    @Override
    public void onFramesDropped(long count) {
        post(() -> model.onFramesDropped(count));
    }
}
