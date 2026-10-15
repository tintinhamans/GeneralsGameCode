package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;
import org.junit.jupiter.api.Test;

import java.time.Duration;
import java.util.List;
import java.util.OptionalDouble;

/** The director's decisions, with a clock the test moves by hand: no threads, no pipe, no sleeping. */
class DirectorLogicTest {
    private static final int FPS = 30;

    /** A 10 x 10 map of 1000 unit cells whose cell (cx, cy) scores as given; unlisted cells score 0. */
    private static InterestMap.Builder map(int frame, int[]... hot) {
        int size = 10;
        InterestMap.Builder b = InterestMap.newBuilder().setFrame(frame).setWidth(size).setHeight(size).setCellWidth(1000f).setCellHeight(1000f).setOriginX(0f).setOriginY(0f);
        int[] scores = new int[size * size];
        for (int[] h : hot) {
            scores[h[1] * size + h[0]] = h[2];
        }
        for (int s : scores) {
            b.addScores(s);
        }
        return b;
    }

    private static InterestHighlight highlight(int id, HighlightKind kind, float x, float y, int impactFrame, int expireFrame) {
        return InterestHighlight.newBuilder().setId(id).setKind(kind).setX(x).setY(y).setImpactFrame(impactFrame).setExpireFrame(expireFrame).build();
    }

    private static Camera camera(float x, float y, CameraChangeSource source, int userCount) {
        return Camera.newBuilder().setX(x).setY(y).setChangeSource(source).setUserInputCount(userCount).build();
    }

    private static DirectorLogic logic(DirectorMode mode) {
        return new DirectorLogic(DirectorConfig.defaults().toBuilder().mode(mode).build());
    }

    /** The Camera frame that reports the camera at the end of a move, as the game would a moment later. */
    private static void arrive(DirectorLogic logic, DirectorMove move, long nowMs) {
        logic.onCamera(camera((float) move.x(), (float) move.y(), CameraChangeSource.CAMERA_CHANGE_PIPE, 0), nowMs);
    }

    private static DirectorMove only(List<DirectorMove> moves) {
        assertEquals(1, moves.size(), moves.toString());
        return moves.get(0);
    }

    // -- Hotspot.

    @Test
    void hotspotGoesToTheBestCellAndCutsOnALongWayButPansOnAShortOne() {
        DirectorLogic far = logic(DirectorMode.HOTSPOT);
        far.onCamera(camera(500, 500, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        far.onInterest(map(100, new int[] {7, 3, 900}).build(), 0, FPS, 1.0);
        DirectorMove cut = only(far.poll(0));
        assertEquals(DirectorMove.Reason.HOTSPOT, cut.reason());
        assertTrue(cut.cut(), "a way of several thousand units is a cut");
        assertEquals(7500.0, cut.x(), 1.0);
        assertEquals(3500.0, cut.y(), 1.0);

        DirectorLogic near = logic(DirectorMode.HOTSPOT);
        near.onCamera(camera(6900, 3400, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        near.onInterest(map(100, new int[] {7, 3, 900}).build(), 0, FPS, 1.0);
        DirectorMove pan = only(near.poll(0));
        assertFalse(pan.cut());
        assertTrue(pan.easeInOut());
        assertTrue(pan.duration().compareTo(DirectorConfig.defaults().minPan()) >= 0 && pan.duration().compareTo(DirectorConfig.defaults().maxPan()) <= 0);
    }

    @Test
    void hotspotCentresOnTheActionAroundTheBestCell() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onInterest(map(100, new int[] {4, 4, 600}, new int[] {5, 4, 400}).build(), 0, FPS, 1.0);
        DirectorMove move = only(logic.poll(0));
        assertTrue(move.x() > 4500.0 && move.x() < 5500.0, "between the two cells, nearer the stronger: " + move.x());
        assertEquals(4500.0, move.y(), 1.0);
    }

    @Test
    void hotspotStaysAtLeastTheDwellTimeAndSwitchesOnlyToAClearlyBetterSpot() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onInterest(map(100, new int[] {2, 2, 500}).build(), 0, FPS, 1.0);
        assertEquals(1, logic.poll(0).size());

        // Twice as good, but too soon.
        logic.onInterest(map(130, new int[] {2, 2, 500}, new int[] {8, 8, 1000}).build(), 1000, FPS, 1.0);
        assertTrue(logic.poll(1000).isEmpty());
        assertTrue(logic.poll(4900).isEmpty(), "still inside the 5 s dwell");

        // Dwell over, but only 20% better: not enough.
        logic.onInterest(map(200, new int[] {2, 2, 500}, new int[] {8, 8, 600}).build(), 5100, FPS, 1.0);
        assertTrue(logic.poll(5100).isEmpty());

        // Dwell over and clearly better.
        logic.onInterest(map(210, new int[] {2, 2, 500}, new int[] {8, 8, 900}).build(), 5200, FPS, 1.0);
        DirectorMove move = only(logic.poll(5200));
        assertEquals(8500.0, move.x(), 1.0);
    }

    @Test
    void hotspotLeavesAColdSpotForAHotOneAfterTheDwellAndDoesNothingWhenNothingHappens() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onInterest(map(100).build(), 0, FPS, 1.0);
        assertTrue(logic.poll(0).isEmpty(), "an empty map is not worth a move");
        assertEquals(DirectorLogic.State.WAITING, logic.status().state());

        logic.onInterest(map(100, new int[] {1, 1, 400}).build(), 0, FPS, 1.0);
        assertEquals(1, logic.poll(0).size());
        logic.onInterest(map(400).build(), 10_000, FPS, 1.0);
        assertTrue(logic.poll(10_000).isEmpty(), "it stays where it is when everything cools down");
        logic.onInterest(map(500, new int[] {6, 6, 300}).build(), 11_000, FPS, 1.0);
        assertEquals(1, logic.poll(11_000).size(), "a cold spot (score 0 now) loses to anything above the minimum");
    }

    // -- Highlights.

    @Test
    void aSuperweaponIsShownSoThatTheCameraIsInPlaceTheLeadTimeBeforeTheImpactAndThenGoesBack() {
        DirectorLogic logic = logic(DirectorMode.HIGHLIGHTS);
        logic.onCamera(camera(500, 500, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        // frame 0 now, impact in 20 s of game time at normal speed = frame 600
        InterestMap listed = map(0).addHighlights(highlight(7, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 8000, 8000, 600, 1500)).build();
        logic.onInterest(listed, 0, FPS, 1.0);
        assertTrue(logic.poll(0).isEmpty(), "highlights mode idles until the highlight is due");
        assertTrue(logic.poll(13_000).isEmpty(), "impact - lead 4 s - longest pan 2.5 s = 13.5 s");

        DirectorMove go = only(logic.poll(13_500));
        assertEquals(DirectorMove.Reason.HIGHLIGHT, go.reason());
        assertEquals(8000.0, go.x(), 0.5);
        assertEquals(DirectorLogic.State.HIGHLIGHT, logic.status().state());
        arrive(logic, go, 13_600);

        // The map keeps listing it; nothing changes while the camera stays through the impact and the hold.
        logic.onInterest(map(405).addHighlights(highlight(7, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 8000, 8000, 600, 1500)).build(), 13_500, FPS, 1.0);
        assertTrue(logic.poll(20_000).isEmpty());
        assertTrue(logic.poll(22_900).isEmpty(), "impact at 20 s plus the 3 s hold");
        DirectorMove back = only(logic.poll(23_000));
        assertEquals(DirectorMove.Reason.RETURN, back.reason());
        assertEquals(500.0, back.x(), 0.5);

        // Listed again later: it was shown, it is not shown twice.
        logic.onInterest(map(700).addHighlights(highlight(7, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 8000, 8000, 600, 1500)).build(), 24_000, FPS, 1.0);
        assertTrue(logic.poll(24_000).isEmpty());
    }

    @Test
    void theImpactTimeFollowsTheSpeedOfTheGame() {
        DirectorLogic logic = logic(DirectorMode.HIGHLIGHTS);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        // 600 frames ahead at double speed is 10 s of real time
        logic.onInterest(map(0).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 5000, 5000, 600, 1500)).build(), 0, FPS, 2.0);
        assertTrue(logic.poll(3_000).isEmpty());
        assertEquals(1, logic.poll(3_500).size(), "10 s - 4 s - 2.5 s");
    }

    @Test
    void aHeroDeathIsShownAtOnceAndAMoreImportantHighlightGoesFirst() {
        DirectorLogic logic = logic(DirectorMode.HIGHLIGHTS);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        logic.onInterest(map(10)
            .addHighlights(highlight(1, HighlightKind.HIGHLIGHT_STRUCTURE_LOST, 1000, 1000, 0, 400))
            .addHighlights(highlight(2, HighlightKind.HIGHLIGHT_HERO_DEATH, 2000, 2000, 0, 400)).build(), 0, FPS, 1.0);
        DirectorMove first = only(logic.poll(0));
        assertEquals(2000.0, first.x(), 0.5, "the hero before the structure");
        arrive(logic, first, 100);
        // The structure is next, after the hero's hold.
        assertTrue(logic.poll(500).isEmpty());
        List<DirectorMove> after = logic.poll(10_000);
        assertEquals(DirectorMove.Reason.RETURN, only(after).reason());
        logic.onInterest(map(400).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_STRUCTURE_LOST, 1000, 1000, 0, 1000)).build(), 10_100, FPS, 1.0);
        assertEquals(DirectorMove.Reason.HIGHLIGHT, only(logic.poll(10_100)).reason());
    }

    @Test
    void aLateImpactEstimateExtendsTheStayAtASuperweaponWithoutImpactTime() {
        DirectorLogic logic = logic(DirectorMode.HIGHLIGHTS);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        logic.onInterest(map(0).addHighlights(highlight(3, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 3000, 3000, 0, 1500)).build(), 0, FPS, 1.0);
        arrive(logic, only(logic.poll(0)), 100);
        // 5 s later the game gives the missile's flight time: impact 22 s ahead
        logic.onInterest(map(150).addHighlights(highlight(3, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 3000, 3000, 810, 1500)).build(), 5_000, FPS, 1.0);
        assertTrue(logic.poll(5_000).isEmpty());
        assertTrue(logic.poll(27_000).isEmpty(), "impact at 27 s, plus the hold");
        assertEquals(DirectorMove.Reason.RETURN, only(logic.poll(30_000)).reason());
    }

    // -- Overview.

    private static void twoPlayers(DirectorLogic logic) {
        logic.setOverviewSpots(List.of(
            new DirectorLogic.Spot(2, 1000, 1000, DirectorLogic.Spot.Kind.BASE),
            new DirectorLogic.Spot(2, 3000, 3000, DirectorLogic.Spot.Kind.ARMY),
            new DirectorLogic.Spot(3, 9000, 9000, DirectorLogic.Spot.Kind.BASE)));
    }

    @Test
    void theOverviewTouresTheSpotsInAQuietTimeAndWrapsAround() {
        DirectorLogic logic = logic(DirectorMode.OVERVIEW);
        twoPlayers(logic);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        logic.onInterest(map(0).build(), 0, FPS, 1.0);
        assertTrue(logic.poll(0).isEmpty());
        assertTrue(logic.poll(3_900).isEmpty(), "quiet for 4 s first");
        DirectorMove first = only(logic.poll(4_000));
        assertEquals(DirectorMove.Reason.OVERVIEW, first.reason());
        assertEquals(1000.0, first.x(), 0.5);
        assertTrue(logic.poll(9_900).isEmpty(), "6 s at each place");
        assertEquals(3000.0, only(logic.poll(10_000)).x(), 0.5);
        assertEquals(9000.0, only(logic.poll(16_000)).x(), 0.5);
        assertEquals(1000.0, only(logic.poll(22_000)).x(), 0.5, "back to the first");
        assertEquals(DirectorLogic.State.OVERVIEW, logic.status().state());
    }

    @Test
    void aFightHandsTheOverviewOverToTheHotspotAtOnceAndQuietBringsItBack() {
        DirectorLogic logic = logic(DirectorMode.OVERVIEW);
        twoPlayers(logic);
        logic.onInterest(map(0).build(), 0, FPS, 1.0);
        assertTrue(logic.poll(0).isEmpty());
        assertEquals(DirectorMove.Reason.OVERVIEW, only(logic.poll(4_000)).reason());
        // Below the fight score: the tour goes on.
        logic.onInterest(map(100, new int[] {5, 5, 300}).build(), 5_000, FPS, 1.0);
        assertTrue(logic.poll(5_000).isEmpty());
        // A fight: immediate, whatever the dwell.
        logic.onInterest(map(130, new int[] {5, 5, 700}).build(), 6_000, FPS, 1.0);
        DirectorMove move = only(logic.poll(6_000));
        assertEquals(DirectorMove.Reason.HOTSPOT, move.reason());
        assertEquals(DirectorLogic.State.HOTSPOT, logic.status().state());
        // It cools down; after the quiet delay the tour resumes.
        logic.onInterest(map(500).build(), 20_000, FPS, 1.0);
        assertTrue(logic.poll(20_000).isEmpty());
        assertEquals(DirectorMove.Reason.OVERVIEW, only(logic.poll(24_000)).reason());
    }

    // -- Director mode.

    @Test
    void theDirectorFollowsHotspotsIsInterruptedByHighlightsAndTouresWhenQuiet() {
        DirectorLogic logic = logic(DirectorMode.DIRECTOR);
        twoPlayers(logic);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        logic.onInterest(map(0, new int[] {4, 4, 800}).build(), 0, FPS, 1.0);
        DirectorMove spot = only(logic.poll(0));
        assertEquals(DirectorMove.Reason.HOTSPOT, spot.reason());
        arrive(logic, spot, 100);

        // A hero dies elsewhere: the highlight interrupts the hotspot.
        logic.onInterest(map(30, new int[] {4, 4, 800}).addHighlights(highlight(9, HighlightKind.HIGHLIGHT_HERO_DEATH, 9500, 500, 0, 300)).build(), 1_000, FPS, 1.0);
        DirectorMove hero = only(logic.poll(1_000));
        assertEquals(DirectorMove.Reason.HIGHLIGHT, hero.reason());
        assertEquals(9500.0, hero.x(), 0.5);
        arrive(logic, hero, 1_100);

        // After the hold the camera goes back to the hotspot it left.
        DirectorMove back = only(logic.poll(1_000 + 6_000));
        assertEquals(DirectorMove.Reason.RETURN, back.reason());
        assertEquals(spot.x(), back.x(), 0.5);

        // Everything cools: after the quiet delay the tour starts.
        logic.onInterest(map(900).build(), 30_000, FPS, 1.0);
        assertTrue(logic.poll(30_000).isEmpty());
        assertEquals(DirectorMove.Reason.OVERVIEW, only(logic.poll(34_100)).reason());
    }

    @Test
    void aSuperweaponPreemptsAHeroHighlightButNotTheOtherWayRound() {
        DirectorLogic logic = logic(DirectorMode.DIRECTOR);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 0), 0);
        logic.onInterest(map(0).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_HERO_DEATH, 2000, 2000, 0, 600)).build(), 0, FPS, 1.0);
        assertEquals(2000.0, only(logic.poll(0)).x(), 0.5);
        logic.onInterest(map(30).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_HERO_DEATH, 2000, 2000, 0, 600))
            .addHighlights(highlight(2, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 7000, 7000, 150, 900)).build(), 1_000, FPS, 1.0);
        DirectorMove nuke = only(logic.poll(1_000));
        assertEquals(7000.0, nuke.x(), 0.5);
        // A hero dying while the nuke is shown waits its turn.
        logic.onInterest(map(60).addHighlights(highlight(2, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 7000, 7000, 150, 900))
            .addHighlights(highlight(3, HighlightKind.HIGHLIGHT_HERO_DEATH, 100, 100, 0, 600)).build(), 2_000, FPS, 1.0);
        assertTrue(logic.poll(2_000).isEmpty());
    }

    // -- Users.

    @Test
    void userInputPausesTheDirectorAndItTakesTheCameraBackAfterTheResumeDelay() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 5), 0);
        logic.onInterest(map(0, new int[] {3, 3, 800}).build(), 0, FPS, 1.0);
        assertEquals(1, logic.poll(0).size());

        // The caster moves the mouse: a Camera frame with a user action.
        logic.onCamera(camera(50, 50, CameraChangeSource.CAMERA_CHANGE_USER, 6), 2_000);
        assertTrue(logic.isPaused());
        logic.onInterest(map(60, new int[] {8, 8, 5000}).build(), 3_000, FPS, 1.0);
        assertTrue(logic.poll(3_000).isEmpty());
        assertEquals(DirectorLogic.State.PAUSED_BY_USER, logic.status().state());

        // More input keeps the pause going; the delay counts from the last input.
        logic.onCamera(camera(60, 60, CameraChangeSource.CAMERA_CHANGE_OTHER, 7), 8_000);
        assertTrue(logic.poll(17_000).isEmpty());
        List<DirectorMove> resumed = logic.poll(18_000);
        assertFalse(logic.isPaused());
        assertEquals(8500.0, only(resumed).x(), 0.5, "decides afresh from the latest map");
    }

    @Test
    void aCountThatGrowsIsUserInputEvenWhenTheFrameSaysOther() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 5), 0);
        assertFalse(logic.isPaused(), "the first frame only sets the count");
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_PIPE, 5), 100);
        assertFalse(logic.isPaused(), "the director's own moves are not user input");
        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_OTHER, 6), 200);
        assertTrue(logic.isPaused());
    }

    @Test
    void resumeAndPauseAreExplicit() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onInterest(map(0, new int[] {3, 3, 800}).build(), 0, FPS, 1.0);
        logic.pause();
        assertTrue(logic.poll(0).isEmpty());
        assertEquals(DirectorLogic.State.PAUSED, logic.status().state());
        logic.resume(10);
        assertEquals(1, logic.poll(10).size());

        logic.onCamera(camera(0, 0, CameraChangeSource.CAMERA_CHANGE_USER, 1), 20);
        assertTrue(logic.isPaused());
        logic.resume(30);
        assertFalse(logic.isPaused(), "an explicit resume does not wait for the delay");
    }

    @Test
    void aNewMatchForgetsTheMapAndTheHighlights() {
        DirectorLogic logic = logic(DirectorMode.DIRECTOR);
        logic.onInterest(map(0, new int[] {3, 3, 800}).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_HERO_DEATH, 1, 1, 0, 300)).build(), 0, FPS, 1.0);
        logic.reset();
        assertTrue(logic.poll(0).isEmpty());
        assertEquals(0, logic.status().pendingHighlights());
        // The same highlight id in the new match is new again.
        logic.onInterest(map(0).addHighlights(highlight(1, HighlightKind.HIGHLIGHT_HERO_DEATH, 1, 1, 0, 300)).build(), 100, FPS, 1.0);
        assertEquals(1, logic.poll(100).size());
    }

    @Test
    void theZoomOfTheConfigurationRidesOnEveryMove() {
        DirectorLogic logic = new DirectorLogic(DirectorConfig.defaults().toBuilder().mode(DirectorMode.HOTSPOT).zoom(OptionalDouble.of(1.4)).build());
        logic.onInterest(map(0, new int[] {3, 3, 800}).build(), 0, FPS, 1.0);
        DirectorMove move = only(logic.poll(0));
        assertEquals(1.4, move.zoom().getAsDouble(), 1e-9);
        CameraMove wire = move.toCameraMove();
        assertEquals(1.4, wire.zoom().getAsDouble(), 1e-9);
        assertEquals(move.duration(), wire.transition());
    }

    // -- Configuration.

    @Test
    void theConfigurationIsImmutableAndValidated() {
        DirectorConfig defaults = DirectorConfig.defaults();
        assertEquals(DirectorMode.DIRECTOR, defaults.mode());
        assertEquals(Duration.ofSeconds(5), defaults.minDwell());
        DirectorConfig changed = defaults.toBuilder().minDwell(Duration.ofSeconds(9)).build();
        assertEquals(Duration.ofSeconds(5), defaults.minDwell(), "the original did not change");
        assertEquals(Duration.ofSeconds(9), changed.minDwell());
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().switchRatio(0.5));
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().minDwell(Duration.ofSeconds(-1)));
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().panTime(Duration.ofSeconds(3), Duration.ofSeconds(1)));
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().overviewDwell(Duration.ZERO));
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().zoom(OptionalDouble.of(100)));
        assertThrows(IllegalArgumentException.class, () -> defaults.toBuilder().panSpeed(0));
    }

    @Test
    void aModeChangeDropsWhatWasInProgress() {
        DirectorLogic logic = logic(DirectorMode.HOTSPOT);
        logic.onInterest(map(0, new int[] {3, 3, 800}).build(), 0, FPS, 1.0);
        assertEquals(1, logic.poll(0).size());
        logic.setMode(DirectorMode.HIGHLIGHTS);
        assertEquals(DirectorMode.HIGHLIGHTS, logic.mode());
        assertTrue(logic.poll(10).isEmpty());
        logic.setMode(DirectorMode.HOTSPOT);
        assertEquals(1, logic.poll(20).size(), "a fresh decision after the switch");
    }
}
