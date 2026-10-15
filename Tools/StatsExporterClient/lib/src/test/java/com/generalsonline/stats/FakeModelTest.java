package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.internal.MemoryLink;
import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.CameraControlQuery;
import com.generalsonline.stats.proto.CameraEasing;
import com.generalsonline.stats.proto.CameraFollowEnd;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraFollowQuery;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.StatsSnapshot;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Path;

/** The parts of the fake game without a pipe or a clock: the feed rules, the camera model, the stand-in frames, the dump timeline and the in-memory link. */
class FakeModelTest {
    @TempDir
    Path dir;

    // -- The feed configuration.

    @Test
    void theFeedStartsWithTheDocumentedDefaultsAndRefusesOutOfRangeSettingsWholesale() {
        FakeFeed feed = new FakeFeed();
        FeedConfig c = feed.config();
        assertEquals(100, c.getLightIntervalMs());
        assertEquals(250, c.getFullIntervalMs());
        assertEquals(32, c.getGridResolution());
        assertEquals(250, c.getViewStatsIntervalMs());
        assertEquals(500, c.getInterestIntervalMs());
        assertEquals(2000, c.getNotableCost());
        assertEquals(15000, c.getTradeWindowMs());
        assertEquals(5000, c.getIntensityWindowMs());
        assertFalse(c.getInterest());

        assertNull(feed.apply(FeedConfigQuery.newBuilder().setLightIntervalMs(0).setViewStatsIntervalMs(0).setInterest(true).build()));
        assertEquals(0, feed.config().getLightIntervalMs());
        assertTrue(feed.config().getInterest());

        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setCamera(false).setLightIntervalMs(49).build()));
        assertTrue(feed.config().getCamera(), "a refused query changes nothing, not even its valid part");
        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setFullIntervalMs(99).build()));
        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setInterestIntervalMs(249).build()));
        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setIntensityWindowMs(60001).build()));
        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setViewStatsIntervalMs(99).build()));
        assertNotNull(feed.apply(FeedConfigQuery.newBuilder().setGridResolution(24).build()));

        assertNull(feed.apply(FeedConfigQuery.newBuilder().setReset(true).setEvents(false).build()));
        assertEquals(100, feed.config().getLightIntervalMs(), "reset puts the defaults back first");
        assertFalse(feed.config().getEvents());
    }

    @Test
    void snapshotsAreThinnedByTheConfiguredCadenceAndStallMarkersAlwaysPass() {
        FakeFeed feed = new FakeFeed();
        feed.apply(FeedConfigQuery.newBuilder().setLightIntervalMs(200).build());
        int passed = 0;
        for (int frame = 0; frame <= 60; frame += 3) {
            if (feed.snapshot(StatsSnapshot.newBuilder().setFrame(frame).addPlayers(com.generalsonline.stats.proto.PlayerStats.getDefaultInstance()).build(), 30) != null) {
                passed++;
            }
        }
        assertEquals(11, passed, "200 ms at 30 fps is every 6th frame: frames 0, 6, ..., 60");
        StatsSnapshot stall = StatsSnapshot.newBuilder().setFrame(5).setNetworkStalled(true).build();
        assertNotNull(feed.snapshot(stall, 30));
        assertNotNull(feed.snapshot(stall, 30));

        feed.apply(FeedConfigQuery.newBuilder().setLightIntervalMs(0).build());
        assertNull(feed.snapshot(StatsSnapshot.newBuilder().setFrame(300).addPlayers(com.generalsonline.stats.proto.PlayerStats.getDefaultInstance()).build(), 30), "light snapshots off");
    }

    @Test
    void viewStatsAndInterestCadencesFollowWallClockTime() {
        FakeFeed feed = new FakeFeed();
        long ms = 1_000_000L;
        assertTrue(feed.viewStatsDue(0));
        assertFalse(feed.viewStatsDue(100 * ms));
        assertTrue(feed.viewStatsDue(251 * ms));
        assertFalse(feed.interestDue(0), "interest is off by default");
        feed.apply(FeedConfigQuery.newBuilder().setInterest(true).build());
        assertTrue(feed.interestDue(0));
        assertFalse(feed.interestDue(400 * ms));
        feed.interestNow();
        assertTrue(feed.interestDue(401 * ms), "a new highlight sends one at once");
        feed.apply(FeedConfigQuery.newBuilder().setViewStatsIntervalMs(0).build());
        assertFalse(feed.viewStatsDue(10_000 * ms));
    }

    // -- The camera.

    private static final long SECOND = 1_000_000_000L;

    @Test
    void aTransitionEasesBetweenTheStartAndTheTargetAndTurnsTheShortWayRound() {
        FakeCamera camera = new FakeCamera();
        camera.adopt(Camera.newBuilder().setX(0).setY(0).setZoom(1f).setAngle(3.0f).setPitch(0.75f).build());
        assertNull(camera.control(CameraControlQuery.newBuilder().setX(1000f).setY(0f).setAngle(-3.0f).setDurationMs(1000).setEasing(CameraEasing.CAMERA_EASING_EASE_IN_OUT).build(), 0));
        Camera quarter = camera.tick(250_000_000L, id -> null, 1, 1);
        Camera half = camera.tick(500_000_000L, id -> null, 2, 1);
        Camera end = camera.tick(SECOND, id -> null, 3, 1);
        assertTrue(quarter.getX() < 250f, "ease in: slow at first, " + quarter.getX());
        assertEquals(500f, half.getX(), 1f);
        assertTrue(quarter.getTransitionActive() && half.getTransitionActive());
        assertEquals(1000f, end.getX());
        assertFalse(end.getTransitionActive());
        assertEquals(CameraChangeSource.CAMERA_CHANGE_PIPE, end.getChangeSource());
        assertTrue(half.getAngle() > 3.0f, "3.0 to -3.0 goes up through pi (the short way, about 0.28 radians), not down through 0: " + half.getAngle());
        assertEquals(-3.0f + (float) (2 * Math.PI), end.getAngle(), 0.001f);
        assertNull(camera.tick(2 * SECOND, id -> null, 4, 1), "nothing changed, nothing to send");
    }

    @Test
    void cameraCommandsAreValidatedAndTheViewBoxTurnsWithTheAngle() {
        FakeCamera camera = new FakeCamera();
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, camera.control(CameraControlQuery.newBuilder().setDurationMs(60001).build(), 0).status());
        assertNotNull(camera.control(CameraControlQuery.newBuilder().setX(1f).build(), 0));
        assertNotNull(camera.control(CameraControlQuery.newBuilder().setX(2_000_000f).setY(0f).build(), 0));
        assertNotNull(camera.control(CameraControlQuery.newBuilder().setZoom(0.01f).build(), 0));
        assertNotNull(camera.control(CameraControlQuery.newBuilder().setZoom(21f).build(), 0));
        assertNotNull(camera.control(CameraControlQuery.newBuilder().setAngle(Float.NaN).build(), 0));
        assertFalse(camera.commanded(), "refused commands do not take the camera");

        assertNull(camera.control(CameraControlQuery.newBuilder().setX(100f).setY(200f).setZoom(1f).setAngle(0f).build(), 0));
        Camera flat = camera.frame(0, 0);
        assertNull(camera.control(CameraControlQuery.newBuilder().setAngle((float) Math.PI).build(), 0));
        Camera turned = camera.frame(0, 0);
        assertEquals(flat.getViewTlX() - flat.getX(), -(turned.getViewTlX() - turned.getX()), 0.01f, "half a turn mirrors the box around the centre");
        assertEquals(flat.getViewTlY() - flat.getY(), -(turned.getViewTlY() - turned.getY()), 0.01f);

        assertNull(camera.control(CameraControlQuery.newBuilder().setReset(true).build(), 0));
        assertEquals(FakeCamera.DEFAULT_ZOOM, camera.state().getZoom());
        assertEquals(FakeCamera.DEFAULT_ANGLE, camera.state().getAngle());
        assertEquals(100f, camera.state().getX(), "a reset keeps the position");
    }

    @Test
    void aLockFollowsTheUnitUntilItIsGoneOrACommandOrAHumanEndsIt() {
        FakeCamera camera = new FakeCamera();
        float[][] unit = {{500f, 600f}};
        java.util.function.IntFunction<float[]> where = id -> id == 9 ? unit[0] : null;
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, camera.follow(CameraFollowQuery.newBuilder().setObjectId(4).build(), where, 0).status());
        assertNull(camera.follow(CameraFollowQuery.newBuilder().setObjectId(9).setSnap(true).build(), where, 0));
        assertEquals(500f, camera.state().getX());
        unit[0] = new float[] {700f, 800f};
        Camera moved = camera.tick(1, where, 1, 1);
        assertEquals(700f, moved.getX());
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_FOLLOW, moved.getFollowMode());
        assertEquals(CameraChangeSource.CAMERA_CHANGE_PIPE, moved.getChangeSource(), "the command that started it");

        Camera gone = camera.tick(2, id -> null, 2, 1);
        assertEquals(CameraFollowEnd.CAMERA_FOLLOW_END_OBJECT_GONE, gone.getFollowEnded());
        assertEquals(9, gone.getFollowEndedObjectId());
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_NONE, gone.getFollowMode());

        assertNull(camera.follow(CameraFollowQuery.newBuilder().setObjectId(9).setMode(CameraFollowMode.CAMERA_FOLLOW_TETHER).setTetherDistance(100f).build(), where, 3));
        Camera tether = camera.tick(4, where, 4, 1);
        assertTrue(Math.abs(tether.getX() - 700f) < Math.abs(500f - 700f) + 0.01f);
        assertNull(camera.follow(CameraFollowQuery.newBuilder().setObjectId(0).build(), where, 5));
        assertEquals(CameraFollowEnd.CAMERA_FOLLOW_END_COMMAND, camera.tick(6, where, 5, 1).getFollowEnded());

        camera.follow(CameraFollowQuery.newBuilder().setObjectId(9).build(), where, 7);
        camera.userInput();
        Camera user = camera.tick(8, where, 6, 1);
        assertEquals(CameraChangeSource.CAMERA_CHANGE_USER, user.getChangeSource());
        assertEquals(CameraFollowEnd.CAMERA_FOLLOW_END_USER, user.getFollowEnded());
        assertEquals(1, user.getUserInputCount());
    }

    // -- The stand-in frames.

    @Test
    void theInterestScoreDecaysWithGameTimeAndHighlightsComeFromTheEvents() {
        FakeSynth synth = new FakeSynth(30);
        MatchState state = new MatchState();
        state.onMatchInfo(FakeDumpFixture.match(com.generalsonline.stats.proto.MatchPhase.MATCH_PHASE_STARTED, true));
        state.onStatsSnapshot(FakeDumpFixture.snapshot(9, true));
        for (Events events : FakeDumpFixture.eventsAt(90)) {
            synth.onEvents(events);
        }
        InterestMap fresh = synth.interest(state, 4, 90);
        InterestMap later = synth.interest(state, 4, 90 + 6 * 30);
        // the kill at 2000, 2000 lands in cell (2, 2) of a 4 by 4 grid over 4000 units
        int hot = fresh.getScores(2 * 4 + 2);
        assertTrue(hot >= 0.6 * 2000, "the kill's 1200 and the armies near it: " + hot);
        int cooled = later.getScores(2 * 4 + 2);
        assertEquals(0.3 * 2000, hot - cooled, 0.3 * 2000 * 0.05, "one half life of 6 s of game time takes half of the kill's part away");
        assertTrue(fresh.getScores(1 * 4 + 1) > 0, "armies of enemy players in the same cell score as well");

        assertFalse(synth.takeNewHighlight());
        for (Events events : FakeDumpFixture.eventsAt(120)) {
            synth.onEvents(events);
        }
        assertTrue(synth.takeNewHighlight());
        assertFalse(synth.takeNewHighlight(), "taken once");
        InterestMap map = synth.interest(state, 4, 125);
        assertEquals(1, map.getHighlightsCount());
        assertEquals(HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, map.getHighlights(0).getKind());
        assertEquals(FakeDumpFixture.BOB, map.getHighlights(0).getPlayerIndex());
        assertEquals(120 + 14 * 30, map.getHighlights(0).getImpactFrame());
        assertTrue(map.getHighlights(0).getImpactEstimated());
        assertEquals(0, synth.interest(state, 4, 1000).getHighlightsCount(), "expired highlights are dropped");
    }

    @Test
    void aLostBigStructureAndADeadHeroAreHighlights() {
        FakeSynth synth = new FakeSynth(30);
        MatchState state = new MatchState();
        state.onMatchInfo(FakeDumpFixture.match(com.generalsonline.stats.proto.MatchPhase.MATCH_PHASE_STARTED, true));
        state.onStatsSnapshot(FakeDumpFixture.snapshot(9, true));
        synth.onSnapshot(FakeDumpFixture.snapshot(9, true));
        for (Events events : FakeDumpFixture.eventsAt(150)) {
            synth.onEvents(events);
        }
        InterestMap map = synth.interest(state, 8, 151);
        assertEquals(HighlightKind.HIGHLIGHT_STRUCTURE_LOST, map.getHighlights(0).getKind());
        assertEquals(1500, map.getHighlights(0).getValue());

        Events heroDies = Events.newBuilder().setFrame(160).addEvents(com.generalsonline.stats.proto.StatEvent.newBuilder().setFrame(160).setPlayerIndex(FakeDumpFixture.ALICE)
            .setKind(com.generalsonline.stats.proto.StatEventKind.STATEVENT_LOSS).setCategory("INFANTRY").setPrimaryName("AmericaInfantryColonelBurton").setCost(1500)).build();
        synth.onEvents(heroDies);
        assertEquals(HighlightKind.HIGHLIGHT_HERO_DEATH, synth.interest(state, 8, 161).getHighlights(1).getKind());
    }

    @Test
    void thePointInQuadTestMatchesTheCorners() {
        float[] box = {0, 10, 10, 10, 8, 0, 2, 0};
        assertTrue(FakeSynth.insideQuad(box, 5, 5));
        assertTrue(FakeSynth.insideQuad(box, 9.5f, 9f));
        assertFalse(FakeSynth.insideQuad(box, 1f, 1f), "the lower left corner is cut off");
        assertFalse(FakeSynth.insideQuad(box, 5, 11));
    }

    // -- The dump timeline.

    @Test
    void theDumpTimelineNeverGoesBackAndSkipsTheHelloAndStartsOverWithANewMatch() throws IOException {
        FakeDump dump = FakeDump.load(FakeDumpFixture.write(dir));
        assertEquals(30, dump.logicFps);
        assertNotNull(dump.hello);
        assertTrue(dump.hasCamera);
        assertFalse(dump.hasViewStats || dump.hasInterest);
        assertEquals(3, dump.firstFrame);
        assertTrue(dump.entries.stream().noneMatch(e -> e.type() == FrameType.HELLO || e.type() == FrameType.RESPONSE));
        long previous = 0;
        for (FakeDump.Entry entry : dump.entries) {
            assertTrue(entry.position() >= previous);
            previous = entry.position();
        }
        assertEquals(FakeDumpFixture.LAST_FRAME - 3, dump.length());
    }

    @Test
    void gameFramesComeFromTheFramesThatCarryOne() {
        assertEquals(7, FakeDump.gameFrameOf(StatsSnapshot.newBuilder().setFrame(7).build()));
        assertEquals(8, FakeDump.gameFrameOf(Events.newBuilder().setFrame(8).build()));
        assertEquals(9, FakeDump.gameFrameOf(Camera.newBuilder().setLogicFrame(9).build()));
        assertEquals(-1, FakeDump.gameFrameOf(Camera.getDefaultInstance()), "a camera frame without a logic frame has no time");
        assertEquals(-1, FakeDump.gameFrameOf(TestMessages.hello()));
    }

    // -- The in-memory link.

    @Test
    void whatOneEndWritesTheOtherReadsInPartsAndClosingEndsBoth() throws IOException {
        PipeLink[] ends = MemoryLink.pair();
        ends[0].write(ByteBuffer.wrap(new byte[] {1, 2, 3, 4, 5}));
        ByteBuffer small = ByteBuffer.allocate(2);
        assertEquals(2, ends[1].read(small));
        assertArrayEquals(new byte[] {1, 2}, small.array());
        ByteBuffer rest = ByteBuffer.allocate(10);
        assertEquals(3, ends[1].read(rest));
        ends[1].write(ByteBuffer.wrap(new byte[] {9}));
        ByteBuffer back = ByteBuffer.allocate(4);
        assertEquals(1, ends[0].read(back));

        ends[0].close();
        assertEquals(-1, ends[1].read(ByteBuffer.allocate(4)));
        assertEquals(-1, ends[1].read(ByteBuffer.allocate(4)), "the end stays the end");
        assertThrows(IOException.class, () -> ends[1].write(ByteBuffer.wrap(new byte[] {1})));
        assertEquals(-1, ends[0].read(ByteBuffer.allocate(4)));
    }

    @Test
    void aBlockedReadEndsWhenTheOtherSideCloses() throws Exception {
        PipeLink[] ends = MemoryLink.pair();
        int[] result = {0};
        Thread reader = new Thread(() -> {
            try {
                result[0] = ends[1].read(ByteBuffer.allocate(8));
            } catch (IOException e) {
                result[0] = -2;
            }
        });
        reader.start();
        Thread.sleep(50);
        ends[0].close();
        reader.join(5000);
        assertFalse(reader.isAlive());
        assertEquals(-1, result[0]);
    }
}
