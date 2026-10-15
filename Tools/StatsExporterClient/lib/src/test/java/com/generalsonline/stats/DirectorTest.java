package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatsSnapshot;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;

/** The director's I/O shell on a fake driver and a clock the test moves: feed, moves, errors, pausing and the overview spots. */
class DirectorTest {
    private static final class ManualClock implements DirectorClock {
        long now;

        @Override
        public long millis() {
            return now;
        }
    }

    private static final class FakeDriver implements Director.Driver {
        final List<DirectorMove> moves = new ArrayList<>();
        int feeds;
        Throwable moveFailure;
        Throwable feedFailure;

        @Override
        public CompletableFuture<?> enableFeed() {
            feeds++;
            return feedFailure == null ? CompletableFuture.completedFuture(null) : CompletableFuture.failedFuture(feedFailure);
        }

        @Override
        public CompletableFuture<?> move(DirectorMove move) {
            moves.add(move);
            return moveFailure == null ? CompletableFuture.completedFuture(null) : CompletableFuture.failedFuture(moveFailure);
        }
    }

    private static InterestMap hot(int frame, int cx, int cy, int score) {
        InterestMap.Builder b = InterestMap.newBuilder().setFrame(frame).setWidth(10).setHeight(10).setCellWidth(1000f).setCellHeight(1000f);
        for (int i = 0; i < 100; i++) {
            b.addScores(i == cy * 10 + cx ? score : 0);
        }
        return b.build();
    }

    private static DirectorConfig config(DirectorMode mode) {
        return DirectorConfig.defaults().toBuilder().mode(mode).build();
    }

    private final ManualClock clock = new ManualClock();
    private final FakeDriver driver = new FakeDriver();
    private final MatchState state = new MatchState();

    private Director director(DirectorMode mode) {
        Director director = new Director(driver, state, config(mode), clock, null);
        director.start();
        return director;
    }

    @Test
    void theInterestFeedIsSwitchedOnOnceTheHelloIsKnown() {
        Director director = director(DirectorMode.HOTSPOT);
        director.tick();
        assertEquals(0, driver.feeds, "no Hello yet: the game would not take a request");
        state.onHello(TestMessages.hello());
        director.tick();
        assertEquals(1, driver.feeds);
        director.tick();
        assertEquals(1, driver.feeds, "once");
        state.onHello(TestMessages.hello());
        director.tick();
        assertEquals(2, driver.feeds, "again after a reconnect: the feed configuration starts over with the connection");
    }

    @Test
    void anInterestMapMakesTheDirectorMoveTheCamera() {
        Director director = director(DirectorMode.HOTSPOT);
        assertTrue(director.lastMove().isEmpty(), "nothing sent yet");
        state.onInterestMap(hot(10, 6, 2, 900));
        assertEquals(1, driver.moves.size());
        assertEquals(DirectorMove.Reason.HOTSPOT, director.lastMove().orElseThrow().reason());
        assertEquals(driver.moves.get(0), director.lastMove().orElseThrow());
        assertEquals(6500.0, driver.moves.get(0).x(), 1.0);
        assertEquals(2500.0, driver.moves.get(0).y(), 1.0);
    }

    @Test
    void aRefusedCommandPausesTheDirectorUntilItIsResumed() {
        Director director = director(DirectorMode.HOTSPOT);
        driver.moveFailure = new NotAllowedException("the local player is an active player of the match");
        state.onInterestMap(hot(10, 6, 2, 900));
        assertEquals(1, driver.moves.size());
        assertInstanceOf(NotAllowedException.class, director.lastError().orElseThrow());
        assertEquals(DirectorLogic.State.PAUSED, director.status().state());

        clock.now = 30_000;
        state.onInterestMap(hot(20, 1, 1, 900));
        assertEquals(1, driver.moves.size(), "paused: nothing more is sent");

        driver.moveFailure = null;
        director.resume();
        assertTrue(director.lastError().isEmpty());
        assertEquals(2, driver.moves.size());
    }

    @Test
    void aHumanOnTheCameraStopsTheDirectorAndPauseIsExplicitToo() {
        Director director = director(DirectorMode.HOTSPOT);
        state.onCamera(Camera.newBuilder().setUserInputCount(1).setChangeSource(CameraChangeSource.CAMERA_CHANGE_OTHER).build());
        state.onCamera(Camera.newBuilder().setUserInputCount(2).setChangeSource(CameraChangeSource.CAMERA_CHANGE_USER).build());
        state.onInterestMap(hot(10, 6, 2, 900));
        assertTrue(driver.moves.isEmpty());
        assertEquals(DirectorLogic.State.PAUSED_BY_USER, director.status().state());

        clock.now = 11_000;
        director.tick();
        assertEquals(1, driver.moves.size(), "after the resume delay the director takes over");

        director.pause();
        clock.now = 30_000;
        state.onInterestMap(hot(900, 1, 8, 900));
        assertEquals(1, driver.moves.size());
    }

    @Test
    void theOverviewVisitsTheBasesAndArmiesOfThePlayersThatAreStillIn() {
        Director director = director(DirectorMode.OVERVIEW);
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m").setHasWorldExtent(true).setWorldMaxX(10000).setWorldMaxY(10000)
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("A").setHasStartLocation(true).setStartX(1000).setStartY(1000))
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(3).setName("B").setHasStartLocation(true).setStartX(9000).setStartY(9000))
            .build());
        // Player 2 has an army in the middle of the map: 10 x 10 cells of 1000 units, its units in the cell (5, 5).
        PlayerGrid.Builder grid = PlayerGrid.newBuilder().setPlayerIndex(2).setWidth(10).setHeight(10).setCellWidth(1000f).setCellHeight(1000f);
        for (int i = 0; i < 100; i++) {
            grid.addCells(i == 55 ? 6 : 0);
        }
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(10).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2)).addPlayers(PlayerStats.newBuilder().setPlayerIndex(3)).addGrids(grid).build());

        state.onInterestMap(hot(10, 0, 0, 0));
        clock.now = 5_000;
        director.tick();
        assertEquals(1, driver.moves.size());
        DirectorMove first = driver.moves.get(0);
        assertEquals(DirectorMove.Reason.OVERVIEW, first.reason());
        assertEquals(1000.0, first.x(), 0.5, "player 2's base first");

        clock.now = 11_000;
        director.tick();
        assertEquals(5500.0, driver.moves.get(1).x(), 0.5, "then its army");
        clock.now = 17_000;
        director.tick();
        assertEquals(9000.0, driver.moves.get(2).x(), 0.5, "then player 3's base");
    }

    @Test
    void closingStopsTheFeed() {
        Director director = director(DirectorMode.HOTSPOT);
        director.close();
        state.onInterestMap(hot(10, 6, 2, 900));
        assertTrue(driver.moves.isEmpty());
        director.close();
        assertFalse(director.status().hasTarget());
    }

    @Test
    void aNewMatchStartsTheDirectorOver() {
        director(DirectorMode.HOTSPOT);
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_LOADING).setMatchId("one").build());
        state.onInterestMap(hot(10, 6, 2, 900));
        assertEquals(1, driver.moves.size());
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_LOADING).setMatchId("two").build());
        clock.now = 1_000;
        state.onInterestMap(hot(10, 6, 2, 900));
        assertEquals(2, driver.moves.size(), "no dwell, no memory of the old target");
    }
}
