package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.CameraFollowEnd;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraState;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.ObjectsQuery;
import com.generalsonline.stats.proto.ObjectsResult;
import com.generalsonline.stats.proto.PageRequest;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.ReplayState;
import com.generalsonline.stats.proto.Request;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.StatsSnapshot;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** The fake game over the in-process transport: the stream it serves, the requests it answers and the control it accepts. */
class FakeGameTest {
    @TempDir
    Path dir;

    private final List<AutoCloseable> open = new ArrayList<>();

    @AfterEach
    void closeEverything() throws Exception {
        for (AutoCloseable c : open) {
            c.close();
        }
    }

    /** Every frame a reader received, in order. */
    static final class Tape implements StatsListener {
        final List<DecodedFrame> frames = Collections.synchronizedList(new ArrayList<>());
        final List<Long> drops = Collections.synchronizedList(new ArrayList<>());

        @Override
        public void onFrame(DecodedFrame frame) {
            frames.add(frame);
        }

        @Override
        public void onFramesDropped(long count) {
            drops.add(count);
        }

        List<DecodedFrame> of(FrameType type) {
            synchronized (frames) {
                return frames.stream().filter(f -> f.type() == type).toList();
            }
        }

        long count(FrameType type) {
            return of(type).size();
        }

        Camera lastCamera() {
            List<DecodedFrame> cameras = of(FrameType.CAMERA);
            return cameras.isEmpty() ? Camera.getDefaultInstance() : (Camera) cameras.get(cameras.size() - 1).message();
        }
    }

    private FakeGame game(double speed, boolean loop) throws IOException {
        return game(FakeGame.builder(FakeDumpFixture.write(dir)).namedPipe(false).pid(5150).speed(speed).loop(loop));
    }

    private FakeGame game(FakeGame.Builder builder) throws IOException {
        FakeGame game = builder.namedPipe(false).holdForReader(true).build().start();
        open.add(game);
        return game;
    }

    private StatsPipe connect(FakeGame game, Tape tape, MatchState state) {
        StatsPipe.Builder builder = game.connect(StatsPipe.builder()).reconnect(false).listener(tape);
        if (state != null) {
            builder.state(state);
        }
        StatsPipe pipe = builder.build().start();
        open.add(pipe);
        return pipe;
    }

    private StatsPipe connectAndWait(FakeGame game, Tape tape, MatchState state) throws InterruptedException {
        StatsPipe pipe = connect(game, tape, state);
        assertTrue(pipe.awaitHello(Duration.ofSeconds(10)).isPresent(), "a Hello arrives");
        return pipe;
    }

    private static <T> T get(java.util.concurrent.CompletableFuture<T> future) throws Exception {
        return future.get(10, TimeUnit.SECONDS);
    }

    private static RequestFailedException failure(java.util.concurrent.CompletableFuture<?> future) throws Exception {
        try {
            future.get(10, TimeUnit.SECONDS);
        } catch (ExecutionException e) {
            return assertInstanceOf(RequestFailedException.class, e.getCause());
        }
        throw new AssertionError("the request should have failed");
    }

    /** A game started and the match running: waits for the first snapshot. */
    private void awaitMatch(MatchState state) throws InterruptedException {
        await("a running match", () -> state.snapshotCount() > 3 && state.phase().orElse(null) == MatchPhase.MATCH_PHASE_STARTED);
    }

    // -- The stream.

    @Test
    void servesTheWholeDumpToOneReaderWithAHelloAndConsecutiveSequenceNumbers() throws Exception {
        FakeGame game = game(0, false);
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connect(game, tape, state);
        await("the end of the match", () -> state.phase().orElse(null) == MatchPhase.MATCH_PHASE_ENDED);

        assertEquals(5150, state.hello().orElseThrow().getProcessId());
        assertEquals(0, state.hello().orElseThrow().getReaderSlot());
        assertEquals(4, state.hello().orElseThrow().getMaxReaders());
        assertTrue(state.hasCapability("camera.control") && state.hasCapability("view_stats") && state.hasCapability("status.not_allowed"));
        assertEquals(FrameType.HELLO, tape.frames.get(0).type());
        assertEquals(PipeDiscovery.pipePath(5150), pipe.connectedPipe().orElseThrow());
        assertEquals(5150, pipe.connectedPid().getAsInt());
        long previous = -1;
        long previousTime = 0;
        synchronized (tape.frames) {
            for (DecodedFrame f : tape.frames) {
                assertEquals(previous + 1, f.sequence(), "no gap, no repeat");
                assertTrue(Long.compareUnsigned(f.timestampMicros(), previousTime) >= 0, "the clock does not go back");
                previous = f.sequence();
                previousTime = f.timestampMicros();
            }
        }
        assertTrue(tape.drops.isEmpty() && pipe.droppedFrames() == 0);
        assertTrue(tape.count(FrameType.STATS_SNAPSHOT) > 50 && tape.count(FrameType.EVENTS) == 7 && tape.count(FrameType.CHAT_MESSAGE) == 1,
            "snapshots " + tape.count(FrameType.STATS_SNAPSHOT) + ", events " + tape.count(FrameType.EVENTS) + ", chat " + tape.count(FrameType.CHAT_MESSAGE));
        assertTrue(tape.count(FrameType.CAMERA) > 10, "the recorded camera is forwarded until a command takes it");
        assertEquals(1, tape.count(FrameType.HELLO), "the dump's own Hello is not sent again");
        assertTrue(game.isFinished());
        assertEquals(FakeDumpFixture.LAST_FRAME, state.frame());
    }

    @Test
    void snapshotsCarryTheReplayTotalAndTheSpeed() throws Exception {
        FakeGame game = game(0, false);
        Tape tape = new Tape();
        connect(game, tape, null);
        await("snapshots", () -> tape.count(FrameType.STATS_SNAPSHOT) > 5);
        StatsSnapshot s = (StatsSnapshot) tape.of(FrameType.STATS_SNAPSHOT).get(2).message();
        assertEquals(FakeDumpFixture.LAST_FRAME, s.getReplayTotalFrames(), "frames in the replay: the first game frame plus the length");
        assertEquals(-1, s.getGameSpeedFps(), "unpaced is reported as uncapped");
    }

    @Test
    void pacesTheStreamByGameTimeWithTheSpeedFactor() throws Exception {
        FakeGame game = game(10, false);
        Tape tape = new Tape();
        MatchState state = new MatchState();
        long start = System.nanoTime();
        connect(game, tape, state);
        await("the end", () -> game.isFinished());
        double seconds = (System.nanoTime() - start) / 1e9;
        // 300 frames at 30 per second is 10 s of game time, a tenth of that at speed 10
        assertTrue(seconds > 0.7 && seconds < 6.0, "took " + seconds);
        assertTrue(tape.count(FrameType.STATS_SNAPSHOT) > 50);
    }

    @Test
    void loopsTheDumpAndAnnouncesTheNewMatch() throws Exception {
        FakeGame game = game(0, true);
        Tape tape = new Tape();
        MatchState state = new MatchState();
        connect(game, tape, state);
        await("two rounds", () -> game.loopsCompleted() >= 2 && tape.count(FrameType.MATCH_INFO) >= 8);
        assertFalse(game.isFinished());
        long loadings = tape.of(FrameType.MATCH_INFO).stream().filter(f -> ((com.generalsonline.stats.proto.MatchInfo) f.message()).getPhase() == MatchPhase.MATCH_PHASE_LOADING).count();
        assertTrue(loadings >= 2, "every round starts with a LOADING MatchInfo: " + loadings);
        assertTrue(tape.drops.isEmpty());
    }

    @Test
    void servesUpToFourReadersAndRefusesTheFifth() throws Exception {
        FakeGame game = game(1, false);
        List<StatsPipe> pipes = new ArrayList<>();
        for (int i = 0; i < 4; i++) {
            pipes.add(connectAndWait(game, new Tape(), null));
        }
        List<Integer> slots = new ArrayList<>();
        for (StatsPipe p : pipes) {
            slots.add(p.hello().orElseThrow().getReaderSlot());
        }
        Collections.sort(slots);
        assertEquals(List.of(0, 1, 2, 3), slots);
        assertEquals(4, game.readerCount());
        assertThrows(IOException.class, game::connectInProcess, "the fifth finds every slot taken");

        pipes.get(0).close();
        await("a slot is free again", () -> game.readerCount() == 3);
        StatsPipe again = connectAndWait(game, new Tape(), null);
        assertEquals(0, again.hello().orElseThrow().getReaderSlot(), "the freed slot is reused");
    }

    @Test
    void aReaderThatJoinsLateGetsTheCurrentScreenMatchAndAFullSnapshot() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(5));
        Tape first = new Tape();
        MatchState firstState = new MatchState();
        connect(game, first, firstState);
        await("some play", () -> firstState.frame() > 60);

        Tape late = new Tape();
        MatchState lateState = new MatchState();
        connect(game, late, lateState);
        await("the late reader's first snapshot", () -> late.count(FrameType.STATS_SNAPSHOT) > 0);
        List<FrameType> start = new ArrayList<>();
        synchronized (late.frames) {
            for (int i = 0; i < 5 && i < late.frames.size(); i++) {
                start.add(late.frames.get(i).type());
            }
        }
        assertEquals(FrameType.HELLO, start.get(0));
        assertEquals(FrameType.UI_STATE, start.get(1));
        assertEquals(FrameType.MATCH_INFO, start.get(2));
        assertEquals(FrameType.STATS_SNAPSHOT, start.get(3));
        assertTrue(((StatsSnapshot) late.of(FrameType.STATS_SNAPSHOT).get(0).message()).getFull(), "a joining reader starts from a full snapshot");
        assertEquals(MatchPhase.MATCH_PHASE_STARTED, lateState.phase().orElseThrow());
        assertEquals(1, late.frames.get(1).sequence());
    }

    @Test
    void aReaderThatFallsBehindLosesFramesAndSeesTheGap() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(0).queueCapacityBytes(2048));
        BlockingSink sink = new BlockingSink();
        assertTrue(game.accept(-1, sink));
        await("the game runs to the end", () -> game.isFinished());
        assertTrue(game.framesDropped() > 0, "a stuck reader loses frames once its queue is full");
        sink.release();
    }

    /** A link whose writes block until released: a reader that does not read. */
    private static final class BlockingSink implements PipeLink {
        private final Object gate = new Object();
        private boolean open;

        void release() {
            synchronized (gate) {
                open = true;
                gate.notifyAll();
            }
        }

        @Override
        public int read(ByteBuffer buffer) throws IOException {
            synchronized (gate) {
                while (!open) {
                    try {
                        gate.wait();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new IOException(e);
                    }
                }
            }
            return -1;
        }

        @Override
        public void write(ByteBuffer buffer) throws IOException {
            synchronized (gate) {
                while (!open) {
                    try {
                        gate.wait();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new IOException(e);
                    }
                }
            }
        }

        @Override
        public void close() {
            release();
        }
    }

    // -- Requests.

    @Test
    void theCatalogListsTheTemplatesTheDumpMentionsWithPaging() throws Exception {
        FakeGame game = game(0, false);
        StatsPipe pipe = connectAndWait(game, new Tape(), null);

        CatalogResult first = get(pipe.catalog(CatalogQuery.newBuilder().setPage(PageRequest.newBuilder().setPageSize(2)).build()));
        assertEquals(2, first.getEntriesCount());
        assertTrue(first.getPage().getHasMore());
        assertTrue(first.getPage().getTotal() >= 5, "at least the five templates the fixture names: " + first.getPage().getTotal());
        CatalogResult second = get(pipe.catalog(CatalogQuery.newBuilder().setPage(PageRequest.newBuilder().setPageSize(2).setCursor(first.getPage().getNextCursor())).build()));
        assertFalse(second.getEntries(0).getName().equals(first.getEntries(0).getName()));

        List<CatalogEntry> all = get(pipe.catalogAll(CatalogQuery.newBuilder().setPage(PageRequest.newBuilder().setPageSize(3)).build()));
        assertEquals(first.getPage().getTotal(), all.size());

        CatalogEntry crusader = get(pipe.catalog(CatalogQuery.newBuilder().setName("AmericaTankCrusader").build())).getEntries(0);
        assertEquals("America", crusader.getSide());
        assertTrue(crusader.getKindOfList().contains("VEHICLE") && crusader.getBuildable());
        assertEquals(1, get(pipe.catalog(CatalogQuery.newBuilder().setName("Nonexistent").build())).getPage().getTotal() + 1);
        CatalogResult structures = get(pipe.catalog(CatalogQuery.newBuilder().setKindOf("structure").setSide("america").build()));
        assertEquals("AmericaBarracks", structures.getEntries(0).getName());
        assertEquals(1500, structures.getEntries(0).getCost(), "the cost the loss event revealed");
    }

    @Test
    void objectsExistWhileTheyLiveAndPlayersAndTheMapComeFromTheMatch() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(0));
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, new Tape(), state);
        await("the end", game::isFinished);
        await("the end seen", () -> state.phase().orElse(null) == MatchPhase.MATCH_PHASE_ENDED);

        // after the match ended nothing is running
        assertEquals(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, failure(pipe.player(2)).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, failure(pipe.moveCamera(CameraMove.to(1, 1))).status());
    }

    @Test
    void objectsPlayersAndTheMapAreAnsweredDuringTheMatch() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(2));
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, new Tape(), state);
        await("play to frame 70", () -> state.frame() >= 70 && state.frame() < 85);

        ObjectDetail crusader = get(pipe.object(101));
        assertEquals("AmericaTankCrusader", crusader.getTemplateName());
        assertEquals(FakeDumpFixture.ALICE, crusader.getOwnerPlayerIndex());
        assertEquals(600f, crusader.getX());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.object(9999)).status());
        assertEquals("ChinaTankOverlord", get(pipe.object(201)).getTemplateName(), "alive until the kill at frame 90");

        ObjectsResult mine = get(pipe.objects(ObjectsQuery.newBuilder().setFilterPlayer(true).setPlayerIndex(FakeDumpFixture.ALICE).build()));
        assertTrue(mine.getRowsList().stream().anyMatch(r -> r.getObjectId() == 101));
        assertTrue(mine.getRowsList().stream().noneMatch(r -> r.getOwnerPlayerIndex() != FakeDumpFixture.ALICE));
        assertTrue(mine.getTemplatesList().stream().anyMatch(t -> t.getName().equals("AmericaTankCrusader")));
        ObjectsResult area = get(pipe.objects(ObjectsQuery.newBuilder().setHasArea(true).setMinX(3000).setMinY(3000).setMaxX(4000).setMaxY(4000).build()));
        assertEquals(List.of(201), area.getRowsList().stream().map(r -> r.getObjectId()).toList());

        PlayerDetail alice = get(pipe.player(FakeDumpFixture.ALICE));
        assertEquals("Alice", alice.getName());
        assertEquals("America", alice.getSide());
        assertTrue(alice.getSciencesList().contains("SCIENCE_Paladin"));
        assertTrue(alice.getBuildableList().contains("AmericaTankCrusader"));
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.player(7)).status());

        MapDetail map = get(pipe.map(true));
        assertEquals(4000f, map.getMaxX());
        assertEquals(2, map.getStartPositionsCount());
        assertEquals("Player_1_Start", map.getWaypoints(0).getName());
        assertEquals(128 * 128 * 4, map.getMinimap().getRgba().size());
        assertEquals(0, get(pipe.map(false)).getMinimap().getWidth());

        await("the kill at frame 90", () -> state.frame() >= 100);
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.object(201)).status(), "a destroyed object is gone");
    }

    @Test
    void feedConfigIsPerReaderAndOutOfRangeValuesAreRefused() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(3));
        Tape quiet = new Tape();
        Tape normal = new Tape();
        StatsPipe quietPipe = connectAndWait(game, quiet, null);
        connectAndWait(game, normal, null);

        FeedConfig defaults = get(quietPipe.feedConfig());
        assertEquals(100, defaults.getLightIntervalMs());
        assertEquals(250, defaults.getFullIntervalMs());
        assertTrue(defaults.getCamera() && defaults.getEvents() && !defaults.getInterest());
        assertEquals(32, defaults.getGridResolution());

        FeedConfig changed = get(quietPipe.setFeedConfig(FeedConfigQuery.newBuilder().setCamera(false).setEvents(false).setChat(false).build()));
        assertFalse(changed.getCamera() || changed.getEvents() || changed.getChat());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(quietPipe.setFeedConfig(FeedConfigQuery.newBuilder().setLightIntervalMs(20).build())).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(quietPipe.setFeedConfig(FeedConfigQuery.newBuilder().setGridResolution(10).build())).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(quietPipe.setFeedConfig(FeedConfigQuery.newBuilder().setTradeWindowMs(10).build())).status());
        assertFalse(get(quietPipe.feedConfig()).getCamera(), "a refused query changes nothing");
        assertFalse(game.feedConfig(0).getEvents());
        assertTrue(game.feedConfig(1).getEvents(), "the other reader is not affected");

        await("the end", game::isFinished);
        await("the other reader saw the events", () -> normal.count(FrameType.EVENTS) == 7);
        assertTrue(quiet.count(FrameType.EVENTS) < 7, "the quiet reader lost them: " + quiet.count(FrameType.EVENTS));
        assertTrue(normal.count(FrameType.CAMERA) > quiet.count(FrameType.CAMERA));
    }

    @Test
    void snapshotCadenceIsThinnedAndTheHeavyPartsCanBeLeftOut() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(3));
        Tape tape = new Tape();
        StatsPipe pipe = connectAndWait(game, tape, null);
        get(pipe.setFeedConfig(FeedConfigQuery.newBuilder().setLightIntervalMs(0).setFullIntervalMs(500).setGrids(false).setSupply(false).build()));
        await("the end", game::isFinished);
        List<DecodedFrame> snapshots = tape.of(FrameType.STATS_SNAPSHOT);
        assertTrue(snapshots.size() < 30, "one full snapshot per half second instead of every 9 frames: " + snapshots.size());
        boolean anyLight = false;
        for (DecodedFrame f : snapshots.subList(2, snapshots.size())) {
            StatsSnapshot s = (StatsSnapshot) f.message();
            anyLight |= !s.getFull();
            assertEquals(0, s.getGridsCount(), "grids left out");
        }
        assertFalse(anyLight, "light snapshots switched off");
    }

    @Test
    void resyncSendsTheScreenMatchAndAFullSnapshotAgainToThatReaderOnly() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(2));
        Tape tape = new Tape();
        Tape other = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        connectAndWait(game, other, null);
        awaitMatch(state);
        long matchInfos = tape.count(FrameType.MATCH_INFO);
        long otherMatchInfos = other.count(FrameType.MATCH_INFO);
        get(pipe.resync());
        await("MatchInfo again", () -> tape.count(FrameType.MATCH_INFO) > matchInfos);
        assertEquals(otherMatchInfos, other.count(FrameType.MATCH_INFO), "the other reader is not resynced");
    }

    @Test
    void anExcessRequestGetsRateLimited() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1).requestsPerSecond(2));
        StatsPipe pipe = connectAndWait(game, new Tape(), null);
        assertEquals(2, pipe.hello().orElseThrow().getMaxRequestsPerSecond());
        assertEquals(4, pipe.hello().orElseThrow().getMaxRequestBurst());
        int limited = 0;
        for (int i = 0; i < 8; i++) {
            if (pipe.request(Request.newBuilder().setMap(com.generalsonline.stats.proto.MapQuery.getDefaultInstance()), Duration.ofSeconds(5)).get(10, TimeUnit.SECONDS)
                .getStatus() == ResponseStatus.RESPONSE_STATUS_RATE_LIMITED) {
                limited++;
            }
        }
        assertTrue(limited >= 3, "limited: " + limited);
    }

    @Test
    void chatIsEchoedBackAsAChatMessage() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1));
        Tape tape = new Tape();
        StatsPipe pipe = connectAndWait(game, tape, null);
        assertTrue(pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, "hello fake"));
        await("the echo", () -> tape.of(FrameType.CHAT_MESSAGE).stream().anyMatch(f -> ((com.generalsonline.stats.proto.ChatMessage) f.message()).getText().equals("hello fake")));
    }

    // -- Control.

    @Test
    void aCameraCommandMovesTheCameraAndShowsAsPipeInTheCameraFrames() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        awaitMatch(state);

        CameraState answer = get(pipe.moveCamera(CameraMove.to(1800, 2400).zoom(1.5).angle(0.5).pitch(0.9)));
        assertEquals(1800f, answer.getX());
        assertEquals(1.5f, answer.getZoom());
        await("a camera frame at the target", () -> {
            Camera c = tape.lastCamera();
            return c != null && c.getX() == 1800f && c.getY() == 2400f;
        });
        Camera frame = tape.lastCamera();
        assertEquals(CameraChangeSource.CAMERA_CHANGE_PIPE, frame.getChangeSource());
        assertEquals(1.5f, frame.getZoom());
        assertEquals(0.5f, frame.getAngle());
        assertTrue(frame.getHasViewBox());
        // recorded camera frames are not forwarded any more: the position stays
        Thread.sleep(150);
        assertEquals(1800f, tape.lastCamera().getX());

        CameraState reset = get(pipe.resetCamera());
        assertEquals(1.0f, reset.getZoom());
        assertEquals(0f, reset.getAngle());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.cameraControl(com.generalsonline.stats.proto.CameraControlQuery.newBuilder().setX(5f).build())).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.moveCamera(CameraMove.set().zoom(99))).status());
    }

    @Test
    void aTransitionRunsOnWallClockTimeAndEndsWithTransitionActiveOff() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        awaitMatch(state);
        CameraState first = get(pipe.moveCamera(CameraMove.to(100, 100)));
        assertFalse(first.getTransitionActive());
        await("at 100", () -> tape.lastCamera().getX() == 100f);

        get(pipe.moveCamera(CameraMove.to(2100, 100).over(Duration.ofMillis(600)).easeInOut()));
        await("underway", () -> tape.lastCamera().getTransitionActive() && tape.lastCamera().getX() > 100f && tape.lastCamera().getX() < 2100f);
        await("arrived", () -> tape.lastCamera().getX() == 2100f && !tape.lastCamera().getTransitionActive());
        assertEquals(CameraChangeSource.CAMERA_CHANGE_PIPE, tape.lastCamera().getChangeSource());

        get(pipe.moveCamera(CameraMove.to(0, 0).over(Duration.ofSeconds(30))));
        await("underway again", () -> tape.lastCamera().getTransitionActive());
        get(pipe.cancelCameraTransition());
        await("stopped", () -> !tape.lastCamera().getTransitionActive());
        float stopped = tape.lastCamera().getX();
        Thread.sleep(100);
        assertEquals(stopped, tape.lastCamera().getX(), "a cancelled transition stays where it is");
    }

    @Test
    void followingAUnitKeepsTheCameraOnItAndTheEndOfTheLockIsReported() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(2));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        await("the overlord exists", () -> state.frame() >= 65 && state.frame() < 85);

        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.followUnit(4242)).status());
        CameraState answer = get(pipe.followUnit(201, true));
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_FOLLOW, answer.getFollowMode());
        assertEquals(201, answer.getFollowObjectId());
        await("on the unit", () -> {
            Camera c = tape.lastCamera();
            return c != null && c.getX() == 3400f && c.getY() == 3400f && c.getFollowObjectId() == 201;
        });
        await("the unit dies and the lock ends", () -> tape.of(FrameType.CAMERA).stream().anyMatch(f -> ((Camera) f.message()).getFollowEnded() == CameraFollowEnd.CAMERA_FOLLOW_END_OBJECT_GONE));
        Camera after = tape.lastCamera();
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_NONE, after.getFollowMode());

        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.cameraFollow(com.generalsonline.stats.proto.CameraFollowQuery.newBuilder().setObjectId(101).setMode(CameraFollowMode.CAMERA_FOLLOW_TETHER).setTetherDistance(500).build())).status());
        CameraState tether = get(pipe.tetherUnit(101, 50));
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_TETHER, tether.getFollowMode());
        get(pipe.unfollowUnit());
        await("the lock ended by command", () -> tape.of(FrameType.CAMERA).stream().anyMatch(f -> ((Camera) f.message()).getFollowEnded() == CameraFollowEnd.CAMERA_FOLLOW_END_COMMAND));
    }

    @Test
    void aHumanTakingTheCameraShowsAsUserAndStopsALock() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        await("the crusader exists", () -> state.frame() >= 35);
        get(pipe.followUnit(101));
        await("locked", () -> tape.lastCamera().getFollowObjectId() == 101);
        game.simulateUserCameraInput();
        await("a user frame", () -> tape.of(FrameType.CAMERA).stream().anyMatch(f -> ((Camera) f.message()).getChangeSource() == CameraChangeSource.CAMERA_CHANGE_USER));
        Camera user = tape.of(FrameType.CAMERA).stream().map(f -> (Camera) f.message()).filter(c -> c.getChangeSource() == CameraChangeSource.CAMERA_CHANGE_USER).findFirst().orElseThrow();
        assertEquals(1, user.getUserInputCount());
        assertEquals(CameraFollowEnd.CAMERA_FOLLOW_END_USER, user.getFollowEnded());
    }

    @Test
    void replayPauseSpeedAndFastForwardChangeThePace() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(3));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        awaitMatch(state);

        ReplayState paused = get(pipe.pauseReplay());
        assertTrue(paused.getPaused());
        Thread.sleep(100);
        long frozen = state.frame();
        Thread.sleep(200);
        assertEquals(frozen, state.frame(), "no game frames while paused");
        assertTrue(game.replayState().getPaused());

        ReplayState resumed = get(pipe.resumeReplay());
        assertFalse(resumed.getPaused());
        await("playing again", () -> state.frame() > frozen);

        ReplayState fast = get(pipe.setReplaySpeed(4));
        assertTrue(fast.getSpeedCapped(), "the game's replay runs at its logic rate at most");
        assertEquals(1.0f, fast.getSpeedRatio());
        ReplayState slow = get(pipe.setReplaySpeed(0.5));
        assertFalse(slow.getSpeedCapped());
        assertEquals(0.5f, slow.getSpeedRatio());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.replayControl(com.generalsonline.stats.proto.ReplayControlQuery.newBuilder().setSpeedRatio(40f).build())).status());

        ReplayState forward = get(pipe.setReplayFastForward(true));
        assertTrue(forward.getFastForward());
        await("the end in fast forward", game::isFinished);
    }

    @Test
    void observeAcceptsAPlayerOfTheMatchAndNobody() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1));
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, new Tape(), state);
        awaitMatch(state);
        assertEquals(FakeDumpFixture.BOB, get(pipe.observePlayer(FakeDumpFixture.BOB)).getPlayerIndex());
        assertEquals(FakeDumpFixture.BOB, game.observedPlayer());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.observePlayer(9)).status());
        assertEquals(-1, get(pipe.observeNobody()).getPlayerIndex());
    }

    @Test
    void theSituationDecidesWhatIsAllowed() throws Exception {
        MatchState playing = new MatchState();
        FakeGame player = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1).situation(FakeGame.Situation.PLAYER));
        StatsPipe playerPipe = connectAndWait(player, new Tape(), playing);
        awaitMatch(playing);
        for (java.util.concurrent.CompletableFuture<?> command : List.of(playerPipe.moveCamera(CameraMove.to(1, 1)), playerPipe.followUnit(101), playerPipe.pauseReplay(), playerPipe.observeNobody())) {
            ExecutionException e = assertThrows(ExecutionException.class, () -> command.get(10, TimeUnit.SECONDS));
            assertInstanceOf(NotAllowedException.class, e.getCause());
            assertFalse(((NotAllowedException) e.getCause()).reason().isEmpty());
        }
        assertEquals(0f, player.cameraState().getX() == 1f ? 1f : 0f, "nothing moved");

        MatchState watching = new MatchState();
        FakeGame observer = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5151).speed(1).situation(FakeGame.Situation.OBSERVER));
        StatsPipe observerPipe = connectAndWait(observer, new Tape(), watching);
        awaitMatch(watching);
        get(observerPipe.moveCamera(CameraMove.to(10, 10)));
        get(observerPipe.observeNobody());
        ExecutionException e = assertThrows(ExecutionException.class, () -> observerPipe.pauseReplay().get(10, TimeUnit.SECONDS));
        assertInstanceOf(NotAllowedException.class, e.getCause(), "a live match cannot be paused");
    }

    // -- Stand-ins for what the dump does not hold.

    @Test
    void aDumpWithoutViewStatsOrInterestGetsApproximationsOnRequest() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(2));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        awaitMatch(state);
        await("view statistics", () -> tape.count(FrameType.VIEW_STATS) > 2);
        assertEquals(0, tape.count(FrameType.INTEREST_MAP), "interest is off until asked for");
        assertEquals(2, state.viewStats().orElseThrow().getPlayersCount());

        get(pipe.setFeedConfig(FeedConfigQuery.newBuilder().setInterest(true).setInterestIntervalMs(250).setViewStatsIntervalMs(100).build()));
        await("interest maps", () -> tape.count(FrameType.INTEREST_MAP) > 1);
        var map = state.interestMap().orElseThrow();
        assertEquals(32, map.getWidth());
        assertEquals(32 * 32, map.getScoresCount());
        await("the superweapon highlight (frame 120)", () -> state.interestMap().orElseThrow().getHighlightsCount() > 0 || state.frame() > 200);
        get(pipe.setFeedConfig(FeedConfigQuery.newBuilder().setViewStatsIntervalMs(0).build()));
        Thread.sleep(200);
        long views = tape.count(FrameType.VIEW_STATS);
        Thread.sleep(300);
        assertEquals(views, tape.count(FrameType.VIEW_STATS), "view statistics switched off");
    }

    @Test
    void theDirectorCanSteerAFakeGameOffline() throws Exception {
        FakeGame game = game(FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(2).loop(true));
        Tape tape = new Tape();
        MatchState state = new MatchState();
        StatsPipe pipe = connectAndWait(game, tape, state);
        awaitMatch(state);
        DirectorConfig config = DirectorConfig.defaults().toBuilder().mode(DirectorMode.HOTSPOT).minDwell(Duration.ofMillis(200)).minScore(1).build();
        try (Director director = Director.start(pipe, state, config)) {
            await("the director moved the camera", () -> tape.of(FrameType.CAMERA).stream().anyMatch(f -> ((Camera) f.message()).getChangeSource() == CameraChangeSource.CAMERA_CHANGE_PIPE));
            assertTrue(director.lastMove().isPresent());
            game.simulateUserCameraInput();
            await("the director steps back", () -> director.status().state() == DirectorLogic.State.PAUSED_BY_USER);
        }
    }

    @Test
    void closingTheGameEndsTheReaders() throws Exception {
        FakeGame game = game(1, false);
        Tape tape = new Tape();
        StatsPipe pipe = connectAndWait(game, tape, null);
        AtomicBoolean disconnected = new AtomicBoolean();
        game.close();
        await("the reader notices", () -> !pipe.isConnected() || disconnected.get());
        assertThrows(IOException.class, game::connectInProcess);
        game.close();
    }

    @Test
    void theBuilderRejectsBadValuesAndAGameStartsOnce() throws Exception {
        Path file = FakeDumpFixture.write(dir);
        assertThrows(IllegalArgumentException.class, () -> FakeGame.builder(file).speed(-1));
        assertThrows(IllegalArgumentException.class, () -> FakeGame.builder(file).pid(-1));
        assertThrows(IllegalArgumentException.class, () -> FakeGame.builder(file).pipe("bad name"));
        assertThrows(IllegalArgumentException.class, () -> FakeGame.builder(file).maxReaders(5));
        assertThrows(IllegalArgumentException.class, () -> FakeGame.builder(file).queueCapacityBytes(10));
        assertThrows(IOException.class, () -> FakeGame.builder(dir.resolve("missing.dump")).build());
        FakeGame game = game(1, false);
        assertThrows(IllegalStateException.class, game::start);
        assertEquals(FakeGame.DEFAULT_PID, FakeGame.builder(file).namedPipe(false).build().pid());
        assertEquals("\\\\.\\pipe\\generalsonline-stats-77", FakeGame.builder(file).pid(77).build().pipePath());
        assertEquals("\\\\.\\pipe\\mine", FakeGame.builder(file).pipe("\\\\.\\pipe\\mine").build().pipePath());
        assertNotNull(game.pipePath());
    }
}
