package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.BattlePlanTransition;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.OnlinePlayer;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerResult;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.ProductionItem;
import com.generalsonline.stats.proto.ProductionQueue;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.generalsonline.stats.proto.SupplySource;
import com.generalsonline.stats.proto.TechStructure;
import com.generalsonline.stats.proto.UiScreen;
import com.generalsonline.stats.proto.UiState;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/** The state layer: the join, the light/full merge, player state, the match lifecycle, world data, ring buffers and change callbacks. */
class MatchStateTest {
    private static MatchInfo started(String id) {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId(id).setMapName("maps\\alpine assault\\alpine assault.map")
            .setLogicFramesPerSecond(60)
            .setHasWorldExtent(true).setWorldMinX(0).setWorldMinY(0).setWorldMaxX(1000).setWorldMaxY(500)
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(2).setName("Alice").setSide("America").setTeam(0).setColor(0xFFFF0000).setIsHuman(true)
                .setOnline(OnlinePlayer.newBuilder().setHasStats(true).setEloRating(1530)))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(3).setName("Bob").setSide("GLA").setTeam(1).setColor(0xFF0000FF).setIsHuman(false))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(2).setPlayerIndex(-1).setName("Olivia").setIsObserver(true).setIsHuman(true))
            .build();
    }

    private static MatchInfo loading(String id) {
        return started(id).toBuilder().setPhase(MatchPhase.MATCH_PHASE_LOADING).setHasWorldExtent(false).build();
    }

    private static StatsSnapshot snapshot(int frame, boolean full) {
        return StatsSnapshot.newBuilder().setFrame(frame).setFull(full).setGameSpeedFps(60).setGameSpeedRatio(1.0f)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(100))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(200)).build();
    }

    private static Events event(StatEvent.Builder e) {
        return Events.newBuilder().setFrame(e.getFrame()).addEvents(e).build();
    }

    private static StatEvent.Builder kill(int frame, int player) {
        return StatEvent.newBuilder().setFrame(frame).setPlayerIndex(player).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("Tank");
    }

    // -- The join.

    @Test
    void playersJoinTheSnapshotWithMatchInfoByPlayerIndex() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(100, true));

        assertEquals(2, state.players().size());
        PlayerView alice = state.players().get(0);
        assertEquals(2, alice.playerIndex());
        assertEquals("Alice", alice.name());
        assertEquals("America", alice.side());
        assertEquals(0xFFFF0000, alice.color());
        assertEquals(0, alice.team());
        assertEquals(1, alice.lobbySlot());
        assertEquals(1530, alice.elo());
        assertTrue(alice.isHuman());
        assertEquals(100, alice.stats().getMoney());
        assertEquals(100, alice.frame());
        assertEquals(-1, state.players().get(1).elo(), "no rating: -1");
        assertFalse(state.players().get(1).isHuman());
        assertSame(state.playersByIndex().get(3), state.player(3).orElseThrow());
        assertTrue(state.player(9).isEmpty());
        assertEquals("Bob", state.playerName(3));
        assertEquals(0xFF0000FF, state.playerColor(3));
        assertEquals("Olivia", state.matchPlayers().get(2).getName(), "observers are in the match players but never in the snapshot players");
    }

    @Test
    void aSnapshotBeforeMatchInfoFallsBackToTheIndexAndALaterMatchInfoFillsItIn() {
        MatchState state = new MatchState();
        state.onStatsSnapshot(snapshot(10, true));
        assertEquals("P2", state.players().get(0).name());
        assertEquals("P2", state.playerName(2));
        assertEquals("", state.playerName(-1));
        assertEquals(-1, state.players().get(0).team());
        assertEquals("", state.players().get(0).side());

        state.onMatchInfo(started("m1"));
        assertEquals("Alice", state.players().get(0).name());
        assertEquals(2, state.players().size(), "the rows that arrived before the first MatchInfo belong to its match");
    }

    @Test
    void playersKeepTheirOrderWhenSnapshotsUpdateThemInPlace() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(10, true));
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(20).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(5))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(6))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(4).setMoney(7)).build());
        List<Integer> order = new ArrayList<>();
        state.players().forEach(p -> order.add(p.playerIndex()));
        assertEquals(List.of(2, 3, 4), order, "first seen first, new players appended");
        assertEquals(6, state.player(2).orElseThrow().stats().getMoney());
        assertEquals("P4", state.player(4).orElseThrow().name());
    }

    // -- The light/full merge.

    @Test
    void aLightSnapshotKeepsTheHeavyFieldsOfTheLastFullOne() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        PlayerStats heavy = PlayerStats.newBuilder().setPlayerIndex(2).setMoney(100).setArmyValue(5000).setWorkers(6)
            .addProduction(ProductionQueue.newBuilder().addEntries(ProductionItem.newBuilder().setName("AmericaTankCrusader").setPercentComplete(50)))
            .addHeroes(HeroStatus.newBuilder().setTemplateName("Hero").setAlive(true))
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponScudStorm").setReadyFrame(30 * 100))
            .addSciences("SCIENCE_Paladin").addUpgrades("Upgrade_AmericaRadar").addTechStructures("OilDerrick")
            .setBattlePlanTransition(BattlePlanTransition.BATTLE_PLAN_DEPLOYING)
            .build();
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(15).setFull(true).addPlayers(heavy).build());

        PlayerStats light = PlayerStats.newBuilder().setPlayerIndex(2).setMoney(250).setScore(77).build();
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(21).setFull(false).addPlayers(light).build());

        assertEquals(1, state.players().size());
        PlayerStats shown = state.players().get(0).stats();
        assertEquals(250, shown.getMoney(), "light fields are overwritten");
        assertEquals(77, shown.getScore());
        assertEquals(5000, shown.getArmyValue(), "heavy fields are kept");
        assertEquals(6, shown.getWorkers());
        assertEquals(1, shown.getProductionCount());
        assertEquals(1, shown.getHeroesCount());
        assertEquals(1, shown.getSuperweaponsCount());
        assertEquals(List.of("SCIENCE_Paladin"), shown.getSciencesList());
        assertEquals(List.of("Upgrade_AmericaRadar"), shown.getUpgradesList());
        assertEquals(List.of("OilDerrick"), shown.getTechStructuresList());
        assertEquals(BattlePlanTransition.BATTLE_PLAN_DEPLOYING, shown.getBattlePlanTransition());
        assertEquals(21, state.players().get(0).frame());

        // a full snapshot replaces everything again, even with empty heavy fields
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(30).setFull(true).addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(1).build()).build());
        assertEquals(0, state.players().get(0).stats().getArmyValue());
        assertEquals(0, state.players().get(0).stats().getProductionCount());
    }

    @Test
    void aFirstLightSnapshotOfAPlayerIsShownAsItIs() {
        MatchState state = new MatchState();
        state.onStatsSnapshot(snapshot(5, false));
        assertEquals(2, state.players().size());
        assertEquals(0, state.players().get(0).stats().getArmyValue());
    }

    @Test
    void aStallSnapshotNeverClearsThePlayersAndKeepsTheLastSpeed() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(600, true));
        assertEquals(60, state.gameSpeedFps().getAsInt());
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(601).setNetworkStalled(true).setStalledFrames(300).setGameSpeedFps(0).build());

        assertEquals(2, state.players().size());
        assertTrue(state.networkStalled());
        assertEquals(300, state.stalledFrames());
        assertEquals(60, state.gameSpeedFps().getAsInt(), "a stall reports no speed; the last one is kept");
        assertEquals(601, state.frame());

        state.onStatsSnapshot(snapshot(700, true));
        assertFalse(state.networkStalled());
        assertEquals(3, state.snapshotCount());
    }

    @Test
    void speedAndLocalFpsFollowSnapshots() {
        MatchState state = new MatchState();
        assertTrue(state.gameSpeedFps().isEmpty());
        assertEquals(1.0f, state.gameSpeedRatio());
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(1).setGameSpeedFps(-1).setLocalRenderFps(143.4f).setLocalLogicFps(59.6f).build());
        assertEquals(-1, state.gameSpeedFps().getAsInt());
        assertEquals(143.4f, state.localRenderFps());
        assertEquals(59.6f, state.localLogicFps());
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(2).setGameSpeedFps(30).setGameSpeedRatio(0.5f).build());
        assertEquals(0.5f, state.gameSpeedRatio());
        assertEquals(143.4f, state.localRenderFps(), "a snapshot without local numbers keeps them");
    }

    // -- Player state.

    @Test
    void playerStateFollowsTheEngineThenTheEventsThenTheFinalResult() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(100, true));
        assertEquals("alive", state.players().get(0).stateText());
        assertEquals(PlayerLife.ALIVE, state.players().get(0).life());

        state.onEvents(event(StatEvent.newBuilder().setFrame(120).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_DEFEATED)));
        state.onEvents(event(StatEvent.newBuilder().setFrame(130).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_PLAYER_LEFT).setVariant("left")));
        assertEquals(PlayerLife.DEFEATED, state.player(2).orElseThrow().life());
        assertEquals("left", state.player(3).orElseThrow().stateText());

        // A defeated player who then leaves is gone; a later "defeated" never downgrades "left".
        state.onEvents(event(StatEvent.newBuilder().setFrame(140).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_LEFT)));
        state.onEvents(event(StatEvent.newBuilder().setFrame(141).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_DEFEATED)));
        assertEquals(PlayerLife.LEFT, state.player(2).orElseThrow().life());

        MatchInfo ended = started("m1").toBuilder().setPhase(MatchPhase.MATCH_PHASE_ENDED).setCompleted(true)
            .setPlayers(0, started("m1").getPlayers(0).toBuilder().setResult(PlayerResult.PLAYER_RESULT_WON))
            .setPlayers(1, started("m1").getPlayers(1).toBuilder().setResult(PlayerResult.PLAYER_RESULT_LOST)).build();
        state.onMatchInfo(ended);
        assertEquals("won", state.player(2).orElseThrow().stateText());
        assertEquals("lost", state.player(3).orElseThrow().stateText());
        assertEquals(2, state.players().size(), "ENDED keeps the tables");
    }

    @Test
    void theEnginesPlayerStateIsReadFirst() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(100, true));
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(110).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(1).setState(com.generalsonline.stats.proto.PlayerState.PLAYER_STATE_DEFEATED))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(2).setState(com.generalsonline.stats.proto.PlayerState.PLAYER_STATE_LEFT)).build());
        assertEquals(2, state.players().size(), "defeated and departed players stay");
        assertEquals("defeated", state.players().get(0).stateText());
        assertEquals("left", state.players().get(1).stateText());
    }

    // -- The match lifecycle.

    @Test
    void aRepeatedStartedForTheSameMatchKeepsEverythingAndDoesNotReset() {
        MatchState state = new MatchState();
        AtomicInteger resets = new AtomicInteger();
        state.addListener(new StateListener() {
            @Override
            public void matchReset() {
                resets.incrementAndGet();
            }
        });
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(100, true));
        state.onEvents(event(kill(100, 2)));
        state.onCamera(TestMessages.camera());
        state.onMatchInfo(started("m1"));

        assertEquals(0, resets.get(), "a STARTED without any earlier match adopts what arrived before it, and a repeat changes nothing");
        assertEquals(2, state.players().size());
        assertEquals(1, state.events().size());
        assertTrue(state.camera().isPresent());
        assertEquals(100, state.frame());
    }

    @Test
    void aLoadingOrAnotherMatchIdBeginsANewMatchThatClearsTheOldOne() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(100, true).toBuilder().addGrids(PlayerGrid.newBuilder().setPlayerIndex(2).addCells(1)).build());
        state.onEvents(event(kill(100, 2)));
        state.onCamera(TestMessages.camera());
        state.onMatchSummary(TestMessages.matchSummary());
        state.onChatMessage(TestMessages.chatMessage());
        state.onMatchInfo(started("m1").toBuilder().setPhase(MatchPhase.MATCH_PHASE_ENDED).build());
        assertEquals(2, state.players().size(), "ENDED does not clear");
        assertEquals(MatchPhase.MATCH_PHASE_ENDED, state.phase().orElseThrow());

        state.onMatchInfo(started("m2"));
        assertEquals("m2", state.matchId());
        assertTrue(state.players().isEmpty());
        assertTrue(state.events().isEmpty());
        assertTrue(state.camera().isEmpty());
        assertTrue(state.summary().isEmpty());
        assertTrue(state.grids().isEmpty());
        assertEquals(-1, state.frame());
        assertEquals(-1, state.currentFrame());
        assertEquals(0, state.snapshotCount());
        assertEquals(0, state.cameraFrames());
        assertTrue(state.gameSpeedFps().isEmpty());
        assertEquals(1, state.chat().size(), "chat belongs to the session, not the match");

        state.onStatsSnapshot(snapshot(5, true));
        state.onMatchInfo(loading("m2"));
        assertTrue(state.players().isEmpty(), "a LOADING always begins a fresh match, even with the same id");
        assertTrue(state.worldExtent().isEmpty(), "a LOADING message has no extent and the old one was cleared");
    }

    @Test
    void thePredictionCheckComesFromLoadingAndSurvivesStartedUntilTheNextMatch() {
        MatchState state = new MatchState();
        assertTrue(state.predictionCheck().isEmpty());
        state.onMatchInfo(TestMessages.matchInfo());
        assertEquals(new PredictionCheck(true, true), state.predictionCheck().orElseThrow());
        state.onMatchInfo(TestMessages.matchInfo().toBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setHasPredictionCheck(false).build());
        assertTrue(state.predictionCheck().isPresent(), "a STARTED without a check keeps the one from LOADING");
        state.onMatchInfo(TestMessages.matchInfo().toBuilder().setMatchId("another").setHasPredictionCheck(false).build());
        assertTrue(state.predictionCheck().isEmpty(), "a new match forgets it");
    }

    @Test
    void theLogicRateComesFromMatchInfoAndSurvivesAReset() {
        MatchState state = new MatchState();
        assertEquals(30, state.logicFps());
        assertEquals("1:30", state.gameTime(60 * 90 / 2));
        state.onMatchInfo(started("m1"));
        assertEquals(60, state.logicFps());
        assertEquals("1:30", state.gameTime(60 * 90));
        state.onMatchInfo(started("m2").toBuilder().setLogicFramesPerSecond(0).build());
        assertEquals(60, state.logicFps(), "0 means unknown: the rate is kept");
    }

    // -- World data.

    @Test
    void worldExtentAndStartPositionsComeFromMatchInfoAndLoadingHasNone() {
        MatchState state = new MatchState();
        state.onMatchInfo(loading("m1"));
        assertTrue(state.worldExtent().isEmpty());

        state.onMatchInfo(started("m1"));
        WorldExtent extent = state.worldExtent().orElseThrow();
        assertEquals(1000f, extent.width());
        assertEquals(500f, extent.height());

        state.onMatchInfo(started("m1").toBuilder().setHasWorldExtent(false).build());
        assertTrue(state.worldExtent().isPresent(), "a later frame that omits the extent keeps the earlier one");

        state.onMatchInfo(started("m1").toBuilder().setWorldMaxX(0).build());
        assertEquals(1000f, state.worldExtent().orElseThrow().width(), "an empty rectangle is not an extent");
    }

    @Test
    void heavyMapDataOfTheLastFullSnapshotSurvivesLightSnapshots() {
        MatchState state = new MatchState();
        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(300).setFull(true)
            .addGrids(PlayerGrid.newBuilder().setPlayerIndex(2).setWidth(32).setHeight(32).addCells(3))
            .addSupply(SupplySource.newBuilder().setX(10).setY(20).setRemainingValue(2000))
            .addTech(TechStructure.newBuilder().setX(5).setY(6).setOwnerPlayerIndex(-1))
            .addConnections(com.generalsonline.stats.proto.OnlineConnection.newBuilder().setUserId(7)).build());
        assertEquals(1, state.grids().size());
        assertEquals(1, state.supply().size());
        assertEquals(1, state.tech().size());
        assertEquals(1, state.connections().size());

        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(310).setFull(false).build());
        assertEquals(1, state.grids().size(), "a light snapshot has no grids; keep the last full ones");
        assertEquals(1, state.supply().size());
        assertEquals(1, state.tech().size());
        assertEquals(1, state.connections().size());

        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(320).setFull(true).build());
        assertTrue(state.grids().isEmpty(), "a full snapshot is authoritative, even when it is empty");
        assertTrue(state.supply().isEmpty());
    }

    @Test
    void theLatestCameraIsKeptAndCounted() {
        MatchState state = new MatchState();
        assertTrue(state.camera().isEmpty());
        state.onCamera(Camera.newBuilder().setX(1).build());
        state.onCamera(Camera.newBuilder().setX(2).build());
        assertEquals(2f, state.camera().orElseThrow().getX());
        assertEquals(2, state.cameraFrames());
    }

    // -- Screens, lobby, chat, events.

    @Test
    void uiLobbyAndSummaryAreKept() {
        MatchState state = new MatchState();
        assertTrue(state.ui().isEmpty());
        assertTrue(state.lobby().isEmpty());
        assertTrue(state.match().isEmpty());
        assertTrue(state.phase().isEmpty());
        assertEquals("", state.matchId());
        state.onUiState(UiState.newBuilder().setScreen(UiScreen.UI_SCREEN_IN_GAME).build());
        state.onLobbyState(TestMessages.lobbyState());
        state.onMatchSummary(TestMessages.matchSummary());
        assertEquals(UiScreen.UI_SCREEN_IN_GAME, state.ui().orElseThrow().getScreen());
        assertTrue(state.lobby().isPresent());
        assertEquals(1, state.summary().orElseThrow().getPlayersCount());

        state.onLobbyState(LobbyState.newBuilder().setInLobby(false).build());
        assertTrue(state.lobby().isEmpty(), "leaving the lobby empties it");
    }

    @Test
    void eventsAndChatAreBoundedRingBuffers() {
        MatchState state = new MatchState(5, 3);
        Events.Builder many = Events.newBuilder().setFrame(1);
        for (int i = 0; i < 8; i++) {
            many.addEvents(StatEvent.newBuilder().setFrame(i).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("n" + i));
        }
        state.onEvents(many.build());
        List<StatEvent> kept = state.events();
        assertEquals(5, kept.size());
        assertEquals("n3", kept.get(0).getPrimaryName(), "the oldest were dropped");
        assertEquals("n7", kept.get(4).getPrimaryName());
        assertEquals(7, state.currentFrame());

        for (int i = 0; i < 5; i++) {
            state.onChatMessage(ChatMessage.newBuilder().setText("line " + i).build());
        }
        assertEquals(3, state.chat().size());
        assertEquals("line 2", state.chat().get(0).getText());
        assertThrows(IllegalArgumentException.class, () -> new MatchState(0, 1));
        assertThrows(IllegalArgumentException.class, () -> new MatchState(1, 0));
    }

    @Test
    void eventDetailUsesTheNamesOfTheState() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        StatEvent capture = StatEvent.newBuilder().setKind(StatEventKind.STATEVENT_CAPTURE).setPlayerIndex(2).setIntValue1(3).setVariant("tech").build();
        assertEquals("from Bob (tech)", state.eventDetail(capture));
    }

    // -- Immutability and threads.

    @Test
    void whatTheStateReturnsCannotBeModified() {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        state.onStatsSnapshot(snapshot(10, true));
        state.onEvents(event(kill(10, 2)));
        List<PlayerView> players = state.players();
        assertThrows(UnsupportedOperationException.class, () -> players.add(players.get(0)));
        assertThrows(UnsupportedOperationException.class, () -> state.playersByIndex().clear());
        assertThrows(UnsupportedOperationException.class, () -> state.matchPlayers().clear());
        assertThrows(UnsupportedOperationException.class, () -> state.events().clear());
        assertThrows(UnsupportedOperationException.class, () -> state.grids().clear());

        // A returned list is a snapshot: later updates do not change it.
        state.onStatsSnapshot(snapshot(20, true).toBuilder().clearPlayers().addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(999)).build());
        assertEquals(100, players.get(0).stats().getMoney());
        assertEquals(999, state.players().get(0).stats().getMoney());
    }

    @Test
    void readersOnOtherThreadsSeeConsistentSnapshotsWhileFramesArrive() throws Exception {
        MatchState state = new MatchState();
        state.onMatchInfo(started("m1"));
        AtomicBoolean stop = new AtomicBoolean();
        AtomicBoolean inconsistent = new AtomicBoolean();
        Thread reader = new Thread(() -> {
            while (!stop.get()) {
                List<PlayerView> view = state.players();
                // Every snapshot below has money == frame for both players; a torn read would show two different values.
                if (view.size() == 2 && view.get(0).stats().getMoney() != view.get(1).stats().getMoney()) {
                    inconsistent.set(true);
                }
                state.events();
                state.camera();
            }
        });
        reader.start();
        for (int i = 1; i <= 2000; i++) {
            state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(i).setFull(true)
                .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(i))
                .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(i)).build());
            state.onEvents(event(kill(i, 2)));
            state.onCamera(Camera.newBuilder().setX(i).build());
        }
        stop.set(true);
        reader.join(10_000);
        assertFalse(inconsistent.get());
        assertEquals(2000, state.frame());
    }

    // -- Change callbacks.

    @Test
    void everyKindOfChangeCallsItsListenerMethod() {
        MatchState state = new MatchState();
        List<String> calls = new ArrayList<>();
        state.addListener(new StateListener() {
            @Override
            public void matchChanged(MatchInfo match) {
                calls.add("matchChanged " + match.getMatchId());
            }

            @Override
            public void matchReset() {
                calls.add("matchReset");
            }

            @Override
            public void playersChanged(List<PlayerView> players) {
                calls.add("playersChanged " + players.size());
            }

            @Override
            public void snapshotApplied(StatsSnapshot snapshot) {
                calls.add("snapshotApplied " + snapshot.getFrame());
            }

            @Override
            public void eventsAdded(List<StatEvent> added) {
                calls.add("eventsAdded " + added.size());
            }

            @Override
            public void cameraChanged(Camera camera) {
                calls.add("cameraChanged");
            }

            @Override
            public void worldChanged() {
                calls.add("worldChanged");
            }

            @Override
            public void chatAdded(ChatMessage message) {
                calls.add("chatAdded");
            }

            @Override
            public void lobbyChanged(LobbyState lobby) {
                calls.add("lobbyChanged");
            }

            @Override
            public void uiChanged(UiState ui) {
                calls.add("uiChanged");
            }

            @Override
            public void summaryChanged(MatchSummary summary) {
                calls.add("summaryChanged");
            }
        });

        state.onMatchInfo(started("m1"));
        assertEquals(List.of("matchChanged m1", "worldChanged"), calls, "the first STARTED is no reset, and with no players there is no playersChanged");
        calls.clear();

        state.onStatsSnapshot(snapshot(10, true));
        assertEquals(List.of("snapshotApplied 10", "worldChanged", "playersChanged 2"), calls);
        calls.clear();

        state.onStatsSnapshot(snapshot(11, false));
        assertEquals(List.of("snapshotApplied 11", "playersChanged 2"), calls, "a light snapshot changes no map data");
        calls.clear();

        state.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(12).setNetworkStalled(true).build());
        assertEquals(List.of("snapshotApplied 12"), calls, "a stall snapshot changes no players");
        calls.clear();

        state.onEvents(event(kill(12, 2)));
        assertEquals(List.of("eventsAdded 1"), calls);
        calls.clear();

        state.onEvents(event(StatEvent.newBuilder().setFrame(13).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_DEFEATED)));
        assertEquals(List.of("eventsAdded 1", "playersChanged 2"), calls, "a state change of a player changes the players");
        calls.clear();

        state.onEvents(Events.newBuilder().setFrame(14).build());
        assertTrue(calls.isEmpty(), "an empty events frame is no change");

        state.onCamera(TestMessages.camera());
        state.onChatMessage(TestMessages.chatMessage());
        state.onLobbyState(TestMessages.lobbyState());
        state.onUiState(TestMessages.uiState());
        state.onMatchSummary(TestMessages.matchSummary());
        assertEquals(List.of("cameraChanged", "chatAdded", "lobbyChanged", "uiChanged", "summaryChanged"), calls);
        calls.clear();

        state.onMatchInfo(started("m2"));
        assertEquals(List.of("matchReset", "matchChanged m2", "playersChanged 0", "worldChanged"), calls, "a new match empties the players");
    }

    @Test
    void listenersAreCalledAfterTheUpdateAndMayReadTheState() {
        MatchState state = new MatchState();
        List<Long> seenFrames = new ArrayList<>();
        state.addListener(new StateListener() {
            @Override
            public void snapshotApplied(StatsSnapshot snapshot) {
                seenFrames.add(state.frame());
            }
        });
        state.onStatsSnapshot(snapshot(42, true));
        assertEquals(List.of(42L), seenFrames);
    }

    @Test
    void aRemovedListenerIsNotCalledAndAThrowingOneDoesNotStopTheOthers() {
        MatchState state = new MatchState();
        AtomicInteger good = new AtomicInteger();
        StateListener bad = new StateListener() {
            @Override
            public void uiChanged(UiState ui) {
                throw new IllegalStateException("bug");
            }
        };
        StateListener counting = new StateListener() {
            @Override
            public void uiChanged(UiState ui) {
                good.incrementAndGet();
            }
        };
        state.addListener(bad);
        state.addListener(counting);
        IllegalStateException thrown = assertThrows(IllegalStateException.class, () -> state.onUiState(TestMessages.uiState()));
        assertEquals("bug", thrown.getMessage());
        assertEquals(1, good.get(), "the second listener was still called");
        assertNotNull(state.ui().orElse(null), "and the state itself was updated");

        state.removeListener(bad);
        state.onUiState(TestMessages.uiState());
        assertEquals(2, good.get());
    }

    @Test
    void theLifecycleOfAFullFeedThroughTheDispatcher() {
        MatchState state = new MatchState();
        for (var message : TestMessages.all()) {
            FrameDispatcher.dispatch(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()), state);
        }
        assertEquals("123456789", state.matchId());
        assertEquals(1, state.players().size());
        assertEquals(2, state.events().size());
        assertEquals(1, state.chat().size());
        assertTrue(state.camera().isPresent());
        assertTrue(state.summary().isPresent());
        assertTrue(state.lobby().isPresent());
        assertTrue(state.ui().isPresent());
        assertTrue(state.networkStalled());
    }
}
