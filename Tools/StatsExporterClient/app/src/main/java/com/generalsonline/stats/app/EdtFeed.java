package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.proto.Camera;

import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
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

    /** @param eventThread runs tasks on the Swing event thread (SwingUtilities::invokeLater); tests pass Runnable::run */
    EdtFeed(GuiModel model, FeedHealth health, Executor eventThread) {
        this.model = model;
        this.health = health;
        this.eventThread = eventThread;
    }

    @Override
    public void onFrame(DecodedFrame frame) {
        health.record(frame, System.nanoTime());
        if (frame.type() == FrameType.CAMERA && frame.message() instanceof Camera camera) {
            latestCamera.set(camera);
            camerasSinceApplied.incrementAndGet();
            return;
        }
        String summary = FrameSummary.of(frame);
        post(() -> model.accept(frame, summary));
    }

    /** Runs a task on the event thread, in order with the frames. */
    void post(Runnable task) {
        pending.add(task);
        if (drainScheduled.compareAndSet(false, true)) {
            eventThread.execute(this::drain);
        }
    }

    private void drain() {
        // Cleared before polling: a task queued after the last poll schedules a new drain instead of being stranded.
        drainScheduled.set(false);
        model.beginBatch();
        try {
            Runnable task;
            while ((task = pending.poll()) != null) {
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
