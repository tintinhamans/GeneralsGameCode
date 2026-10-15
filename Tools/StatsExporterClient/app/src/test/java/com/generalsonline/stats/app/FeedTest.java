package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;

/** The reader thread to event thread hand-over, the guarded dispatch and the feed health counters. */
class FeedTest {
    private static DecodedFrame decode(Message message) {
        return FrameDispatcher.decode(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
    }

    /** An executor that records the tasks instead of running them, to count event-thread tasks. */
    private static final class QueueingExecutor implements java.util.concurrent.Executor {
        final List<Runnable> tasks = new ArrayList<>();

        @Override
        public void execute(Runnable command) {
            tasks.add(command);
        }

        void runAll() {
            List<Runnable> copy = new ArrayList<>(tasks);
            tasks.clear();
            copy.forEach(Runnable::run);
        }
    }

    @Test
    void everyFrameTypePushedThroughTheRealForwardingPathReachesTheModel() {
        GuiModel model = new GuiModel();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), Runnable::run);

        // The test messages cover every outbound frame type, so a new type without a test message fails here first.
        Set<FrameType> covered = EnumSet.noneOf(FrameType.class);
        for (Message message : TestMessages.all()) {
            covered.add(decode(message).type());
            feed.onFrame(decode(message));
        }
        feed.applyCamera();
        for (FrameType type : FrameType.values()) {
            assertEquals(!type.inbound(), covered.contains(type), "test message for " + type);
        }

        assertTrue(model.matchInfo.getRowCount() > 0, "MatchInfo");
        assertEquals(1, model.players.getRowCount(), "StatsSnapshot");
        assertEquals(2, model.events.getRowCount(), "Events");
        assertEquals(1, model.summary.getRowCount(), "MatchSummary");
        assertEquals(1, model.chat.getRowCount(), "ChatMessage");
        assertEquals(1, model.lobbySlots.getRowCount(), "LobbyState");
        assertTrue(model.connectionLine().contains("Screen: ONLINE_GAME_SETUP"), "UiState: " + model.connectionLine());
        assertTrue(model.cameraLine().contains("looking at 101, 200"), "Camera: " + model.cameraLine());
        // Camera frames are not logged in the Frames tab; every other frame is.
        assertEquals(TestMessages.all().length - 1, model.frames.getRowCount());
    }

    @Test
    void aBurstOfFramesCostsOneEventThreadTask() {
        GuiModel model = new GuiModel();
        QueueingExecutor executor = new QueueingExecutor();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), executor);

        for (int i = 0; i < 200; i++) {
            feed.onFrame(decode(TestMessages.chatMessage()));
        }
        assertEquals(1, executor.tasks.size());
        executor.runAll();
        assertEquals(200, model.chat.getRowCount());
        assertEquals(200, model.frames.getRowCount());

        feed.onFrame(decode(TestMessages.chatMessage()));
        assertEquals(1, executor.tasks.size(), "a later frame schedules a new drain");
    }

    @Test
    void cameraFramesAreCoalescedAndNeverQueued() {
        GuiModel model = new GuiModel();
        QueueingExecutor executor = new QueueingExecutor();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), executor);

        for (int i = 0; i < 1000; i++) {
            feed.onFrame(decode(Camera.newBuilder().setX(i).setY(7).build()));
        }
        assertTrue(executor.tasks.isEmpty());

        feed.applyCamera();
        assertTrue(model.cameraLine().contains("looking at 999, 7"), model.cameraLine());
        assertTrue(model.cameraLine().contains("1000 camera frames"), model.cameraLine());

        feed.applyCamera();
        assertTrue(model.cameraLine().contains("1000 camera frames"), "an unchanged camera is not re-applied");
    }

    @Test
    void aNewMatchDropsTheCameraThatWasNotAppliedYet() {
        GuiModel model = new GuiModel();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), Runnable::run);
        model.setNewMatchHook(feed::clearCamera);

        feed.onFrame(decode(TestMessages.camera()));
        feed.onFrame(decode(TestMessages.matchInfo())); // LOADING: a new match
        feed.applyCamera();
        assertEquals("Camera: -", model.cameraLine());
    }

    @Test
    void anExceptionWhileApplyingAFrameIsReportedAndTheNextFrameStillApplies() {
        GuiModel model = new GuiModel();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), Runnable::run);
        feed.post(() -> {
            throw new IllegalStateException("boom");
        });
        feed.onFrame(decode(TestMessages.chatMessage()));

        assertEquals(1, model.chat.getRowCount());
        assertTrue(model.connectionLine().contains("boom"), model.connectionLine());
    }

    @Test
    void lifecycleCallbacksKeepTheirOrderWithFrames() {
        GuiModel model = new GuiModel();
        EdtFeed feed = new EdtFeed(model, new FeedHealth(), Runnable::run);
        feed.onConnected("\\\\.\\pipe\\generalsonline-stats-4242");
        feed.onDisconnected(new IOException("read failed", new IOException("The pipe has been ended")));

        assertTrue(model.connectionLine().contains("The pipe has been ended"), model.connectionLine());
        assertFalse(model.connectionLine().contains("read failed"), model.connectionLine());
        assertTrue(model.windowTitle().contains("pid 4242"), model.windowTitle());
    }

    // -- Feed health (P5).

    @Test
    void feedHealthSplitsCameraFromTheRestAndReportsTimeSinceTheLastFrame() {
        FeedHealth health = new FeedHealth();
        long t0 = 1_000_000_000L;
        health.sample(t0);
        for (int i = 0; i < 118; i++) {
            health.record(decode(TestMessages.camera()), t0 + 1_900_000_000L);
        }
        for (int i = 0; i < 4; i++) {
            health.record(decode(TestMessages.statsSnapshot()), t0 + 1_900_000_000L); // full
        }
        health.record(decode(TestMessages.statsSnapshot().toBuilder().setFull(false).build()), t0 + 1_900_000_000L);
        health.record(decode(TestMessages.events()), t0 + 1_900_000_000L);

        FeedHealth.Sample sample = health.sample(t0 + 2_000_000_000L);
        assertEquals(59.0, sample.cameraHz(), 0.01);
        assertEquals(2.0, sample.fullPerSecond(), 0.01);
        assertEquals(0.5, sample.lightPerSecond(), 0.01);
        assertEquals(1.0, sample.eventsPerSecond(), 0.01, "two events in one frame over two seconds");
        assertEquals(0.5, sample.otherPerSecond(), 0.01, "the events frame");
        assertEquals(0.1, sample.secondsSinceFrame(), 0.01);
        assertFalse(FeedHealth.isStale(sample));
        assertTrue(FeedHealth.text(sample).contains("camera 59 Hz"), FeedHealth.text(sample));

        FeedHealth.Sample later = health.sample(t0 + 9_000_000_000L);
        assertTrue(FeedHealth.isStale(later));
        assertTrue(FeedHealth.text(later).contains("NO FRAMES"), FeedHealth.text(later));
    }

    @Test
    void feedHealthBeforeAnyFrame() {
        FeedHealth health = new FeedHealth();
        FeedHealth.Sample sample = health.sample(5_000_000_000L);
        assertEquals(-1.0, sample.secondsSinceFrame());
        assertFalse(FeedHealth.isStale(sample));
        assertTrue(FeedHealth.text(sample).contains("no frame yet"));
    }
}
