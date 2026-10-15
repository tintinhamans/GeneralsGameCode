package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.LobbyKind;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.NetworkStats;
import com.generalsonline.stats.proto.OnlinePlayer;
import com.generalsonline.stats.proto.PlayerResult;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import org.junit.jupiter.api.Test;

import java.util.concurrent.atomic.AtomicLong;

/** The model rules added in the review round: event details, player state, stale tables, match switching, lobby cells. */
class GuiModelFeaturesTest {
    private static MatchInfo started(String id) {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId(id).setMapName("maps\\alpine assault\\alpine assault.map")
            .setLogicFramesPerSecond(60)
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(2).setName("Alice").setSide("America").setTeam(0).setColor(0xFFFF0000).setIsHuman(true)
                .setOnline(OnlinePlayer.newBuilder().setHasStats(true).setEloRating(1530)))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(3).setName("Bob").setSide("GLA").setTeam(1).setColor(0xFF0000FF).setIsHuman(false))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(2).setPlayerIndex(-1).setName("Olivia").setIsObserver(true).setIsHuman(true))
            .build();
    }

    private static StatsSnapshot snapshot(int frame) {
        return StatsSnapshot.newBuilder().setFrame(frame).setFull(true).setGameSpeedFps(60).setGameSpeedRatio(1.0f)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(100))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(200)).build();
    }

    private static Events event(StatEvent.Builder e) {
        return Events.newBuilder().setFrame(e.getFrame()).addEvents(e).build();
    }

    // -- Item 2: event details.

    @Test
    void veterancySalvageAndScienceEventsHaveADetail() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onEvents(Events.newBuilder().setFrame(10)
            .addEvents(StatEvent.newBuilder().setFrame(10).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_VETERANCY_CHANGE).setPrimaryName("Crusader").setIntValue1(0).setIntValue2(1))
            .addEvents(StatEvent.newBuilder().setFrame(10).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_VETERANCY_CHANGE).setIntValue1(2).setIntValue2(3))
            .addEvents(StatEvent.newBuilder().setFrame(10).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_SALVAGE_UPGRADE).setCategory("WEAPON").setIntValue1(2))
            .addEvents(StatEvent.newBuilder().setFrame(10).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_SCIENCE_PURCHASE).setPrimaryName("SCIENCE_Paladin").setIntValue1(3).setIntValue2(1))
            .build());

        assertEquals("Regular -> Veteran", model.events.valueAt(0, "Detail"));
        assertEquals("Elite -> Heroic", model.events.valueAt(1, "Detail"));
        assertEquals("tier 2", model.events.valueAt(2, "Detail"));
        assertEquals("rank 3, 1 points left", model.events.valueAt(3, "Detail"));
        assertEquals("level 7", Format.veterancyName(7));
    }

    // -- P6: player state, observers, ELO, result.

    @Test
    void stateColumnFollowsDefeatedAndLeftEventsThenTheFinalResult() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(100));
        assertEquals("alive", model.players.valueAt(0, "State"));

        model.onEvents(event(StatEvent.newBuilder().setFrame(120).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_DEFEATED)));
        model.onEvents(event(StatEvent.newBuilder().setFrame(130).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_PLAYER_LEFT).setVariant("left")));
        assertEquals("defeated", model.players.valueAt(0, "State"));
        assertEquals("left", model.players.valueAt(1, "State"));

        // A defeated player who then leaves is gone; a later "defeated" never downgrades "left".
        model.onEvents(event(StatEvent.newBuilder().setFrame(140).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_LEFT)));
        model.onEvents(event(StatEvent.newBuilder().setFrame(141).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_PLAYER_DEFEATED)));
        assertEquals("left", model.players.valueAt(0, "State"));

        MatchInfo ended = started("m1").toBuilder().setPhase(MatchPhase.MATCH_PHASE_ENDED).setCompleted(true)
            .setPlayers(0, started("m1").getPlayers(0).toBuilder().setResult(PlayerResult.PLAYER_RESULT_WON))
            .setPlayers(1, started("m1").getPlayers(1).toBuilder().setResult(PlayerResult.PLAYER_RESULT_LOST)).build();
        model.onMatchInfo(ended);
        assertEquals("won", model.players.valueAt(0, "State"));
        assertEquals("lost", model.players.valueAt(1, "State"));
        assertEquals("Match over: Alice won, Bob lost", model.resultBanner());
    }

    // -- Engine sync round.

    @Test
    void theEnginesPlayerStateIsReadFirstAndDefeatedPlayersStayInTheTable() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(100));
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(110).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(1).setState(com.generalsonline.stats.proto.PlayerState.PLAYER_STATE_DEFEATED))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(2).setState(com.generalsonline.stats.proto.PlayerState.PLAYER_STATE_LEFT)).build());
        assertEquals(2, model.players.getRowCount());
        assertEquals("defeated", model.players.valueAt(0, "State"));
        assertEquals("left", model.players.valueAt(1, "State"));
    }

    @Test
    void aRepeatedStartedForTheSameMatchKeepsTheTables() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(100));
        model.onEvents(event(StatEvent.newBuilder().setFrame(100).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL)));
        model.onMatchInfo(started("m1"));
        assertEquals(2, model.players.getRowCount());
        assertEquals(1, model.events.getRowCount());
    }

    @Test
    void buildEventsShowAStartingVeterancyAndLobbyUsesTheDisplayName() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onEvents(Events.newBuilder().setFrame(5)
            .addEvents(StatEvent.newBuilder().setFrame(5).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_BUILD).setPrimaryName("Tank").setIntValue1(1))
            .addEvents(StatEvent.newBuilder().setFrame(5).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_BUILD).setPrimaryName("Tank")).build());
        assertEquals("veteran", model.events.valueAt(0, "Detail"));
        assertEquals("", model.events.valueAt(1, "Detail"));

        model.onLobbyState(lobby(true, true).toBuilder().setMapName("maps/x/x.map").setMapDisplayName("Twilight Flame").build());
        assertTrue(model.lobbyHeader().contains("map Twilight Flame"), model.lobbyHeader());
    }

    @Test
    void aRandomLobbyColorIsASpecialSwatchNotNoColor() {
        GuiModel model = new GuiModel();
        model.onLobbyState(LobbyState.newBuilder().setInLobby(true)
            .addSlots(LobbySlot.newBuilder().setPlayer(MatchPlayer.newBuilder().setName("A").setIsColorRandom(true).setColor(0))).build());
        ColorCell cell = (ColorCell) model.lobbySlots.valueAt(0, "Color");
        assertTrue(cell.random());
        assertFalse(cell.isColor());
    }

    @Test
    void noResultBannerBeforeTheMatchEnds() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        assertNull(model.resultBanner());
    }

    @Test
    void observersListedSeparatelyEloAndMarkersInThePlayersTable() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(100));

        assertEquals(1, model.observers.getRowCount());
        assertEquals("Olivia", model.observers.valueAt(0, "Name"));
        assertEquals(1530, model.players.valueAt(0, "ELO"));
        assertEquals(-1, model.players.valueAt(1, "ELO"), "no stats: blank");
        assertEquals("Alice", model.players.valueAt(0, "Name"));
        assertEquals("Bob [AI]", model.players.valueAt(1, "Name"));
        assertEquals(1, model.players.valueAt(0, "Team"), "teams are shown from 1");
        assertEquals(2, model.players.valueAt(1, "Team"));
        assertEquals(2, model.players.valueAt(0, "Slot"));
        assertEquals(1, model.players.valueAt(1, "Slot"));
    }

    // -- Item 9: match switching, stale tables.

    @Test
    void aNewMatchResetsCameraSpeedPredictionBannerAndFrame() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(TestMessages.matchInfo()); // LOADING with a prediction check
        model.onStatsSnapshot(snapshot(900));
        model.onCamera(TestMessages.camera(), 10);
        assertEquals(Boolean.TRUE, model.predictionCheckOk());
        assertTrue(model.progressLine().contains("Speed: 1.00x"));

        MatchInfo next = TestMessages.matchInfo().toBuilder().setMatchId("another").setHasPredictionCheck(false).build();
        model.onMatchInfo(next);

        assertNull(model.predictionCheckOk());
        assertEquals("", model.predictionCheckText());
        assertEquals("Camera: -", model.cameraLine());
        assertTrue(model.progressLine().contains("Frame: -"), model.progressLine());
        assertTrue(model.progressLine().contains("Speed: -"), model.progressLine());
        assertTrue(model.state().camera().isEmpty());
    }

    @Test
    void tablesAreMarkedStaleAfterADisconnect() {
        AtomicLong clock = new AtomicLong(1_000_000);
        GuiModel model = new GuiModel(clock::get);
        assertNull(model.staleText(), "nothing to be stale before any data");

        model.onConnected("\\\\.\\pipe\\generalsonline-stats-9");
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(100));
        assertNull(model.staleText());
        assertFalse(model.isStale());

        model.onDisconnected(null);
        clock.addAndGet(12_000);
        assertEquals("Disconnected: the tables are stale, last update 12 s ago", model.staleText());
        assertTrue(model.isStale());

        model.onConnected("\\\\.\\pipe\\generalsonline-stats-9");
        assertNull(model.staleText());
    }

    // -- Item 12: stall snapshots.

    @Test
    void aStallSnapshotShowsTheStallAndKeepsTheLastSpeed() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(600));
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(601).setNetworkStalled(true).setStalledFrames(60 * 5).setGameSpeedFps(0).build());

        String line = model.progressLine();
        assertTrue(line.contains("network stall (5s), last 1.00x"), line);
        assertFalse(line.contains("paused/halted"), line);

        model.onStatsSnapshot(snapshot(700));
        assertTrue(model.progressLine().contains("Speed: 1.00x"), model.progressLine());
    }

    // -- Item 13.

    @Test
    void chatAudienceIsShownOnlyForTheMatchChannel() {
        GuiModel model = new GuiModel();
        model.onChatMessage(ChatMessage.newBuilder().setChannel(ChatChannel.CHAT_CHANNEL_MATCH).setAudience(ChatAudience.CHAT_AUDIENCE_ALLIES).setText("a").build());
        model.onChatMessage(ChatMessage.newBuilder().setChannel(ChatChannel.CHAT_CHANNEL_ROOM).setAudience(ChatAudience.CHAT_AUDIENCE_EVERYONE).setText("b").build());
        assertEquals("ALLIES", model.chat.valueAt(0, "Audience"));
        assertEquals("", model.chat.valueAt(1, "Audience"));
    }

    @Test
    void rotatedViewBoxIsSummarisedAsCentreSizeAndRotation() {
        // A 400 x 200 box turned 90 degrees counter-clockwise about its centre (1000, 500).
        Camera turned = Camera.newBuilder().setHasViewBox(true)
            .setViewTlX(900).setViewTlY(300).setViewTrX(900).setViewTrY(700)
            .setViewBrX(1100).setViewBrY(700).setViewBlX(1100).setViewBlY(300).build();
        Format.ViewBox box = Format.viewBox(turned);
        assertNotNull(box);
        assertEquals(1000.0, box.centerX(), 0.001);
        assertEquals(500.0, box.centerY(), 0.001);
        assertEquals(400.0, box.width(), 0.001);
        assertEquals(200.0, box.height(), 0.001);
        assertEquals(90.0, box.rotationDegrees(), 0.001);
        assertNull(Format.viewBox(Camera.getDefaultInstance()));
    }

    @Test
    void windowTitleShowsPidMapAndReplayName() {
        GuiModel model = new GuiModel();
        assertEquals("Generals Online stats", model.windowTitle());
        model.onConnected("\\\\.\\pipe\\generalsonline-stats-1234");
        model.onMatchInfo(started("m1").toBuilder().setReplayPath("C:\\Users\\x\\Replays\\Last Replay.rep").build());
        assertEquals("Generals Online stats - pid 1234 - alpine assault - Last Replay.rep", model.windowTitle());
        assertEquals("alpine assault", Format.mapDisplayName("maps/alpine assault/alpine assault.map"));
        assertTrue(model.connectionLine().contains("pid 1234"), model.connectionLine());
    }

    @Test
    void networkNumbersAreSortableColumns() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(5).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setNetwork(NetworkStats.newBuilder().setLatencyMs(120).setReportedFps(58).setPingMs(45)))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3)).build());
        assertEquals(120, model.players.valueAt(0, "Latency ms"));
        assertEquals(45, model.players.valueAt(0, "Ping ms"));
        assertEquals(-1, model.players.valueAt(1, "Ping ms"));
    }

    // -- Lobby (P8 and item 13).

    private static LobbyState lobby(boolean predictionValid, boolean startValid) {
        return LobbyState.newBuilder().setInLobby(true).setKind(LobbyKind.LOBBY_KIND_ONLINE)
            .setPredictionValid(predictionValid).setPredictionStartPositionsValid(startValid)
            .addSlots(LobbySlot.newBuilder().setPlayer(MatchPlayer.newBuilder().setName("Alice").setSide("Random").setIsSideRandom(true).setIsColorRandom(true).setColor(3).setTeam(0))
                .setStartPosition(-1).setPredictedSide("GLA").setPredictedColor(0xFF00FF00).setPredictedStartPosition(0))
            .addSlots(LobbySlot.newBuilder().setPlayer(MatchPlayer.newBuilder().setName("Bob").setSide("China").setColor(0xFFFF0000)).setStartPosition(2).setPredictedStartPosition(2))
            .build();
    }

    @Test
    void lobbySideCellIsOneRandomArrowSideAndFlagsAreShown() {
        GuiModel model = new GuiModel();
        model.onLobbyState(lobby(true, true));
        assertEquals("Random -> GLA", model.lobbySlots.valueAt(0, "Side"));
        assertEquals("China", model.lobbySlots.valueAt(1, "Side"));
        assertEquals("side, color", model.lobbySlots.valueAt(0, "Random"));
        assertEquals("", model.lobbySlots.valueAt(1, "Random"));
        assertEquals(1, model.lobbySlots.valueAt(0, "Team"));
    }

    @Test
    void lobbyPredictedCellsAreBlankWhenThePredictionIsNotValid() {
        GuiModel model = new GuiModel();
        model.onLobbyState(lobby(false, false));
        assertEquals("Random", model.lobbySlots.valueAt(0, "Side"));
        assertNull(model.lobbySlots.valueAt(0, "Predicted color"));
        assertEquals(-1, model.lobbySlots.valueAt(0, "Predicted start"));

        model.onLobbyState(lobby(true, false));
        assertNotNull(model.lobbySlots.valueAt(0, "Predicted color"));
        assertEquals(-1, model.lobbySlots.valueAt(0, "Predicted start"), "start positions need their own validity flag");

        model.onLobbyState(lobby(true, true));
        assertEquals(0, model.lobbySlots.valueAt(0, "Predicted start"));
    }

    @Test
    void aFailedPredictionCheckMarksTheRandomSlots() {
        GuiModel model = new GuiModel();
        model.onLobbyState(lobby(true, true));
        model.onMatchInfo(TestMessages.matchInfo().toBuilder().setPredictionMatched(false).build());
        assertEquals("Random -> GLA (check failed)", model.lobbySlots.valueAt(0, "Side"));
        assertEquals("China", model.lobbySlots.valueAt(1, "Side"), "a slot that was not random is never marked");
    }

    @Test
    void lobbyColorIndexIsNotPaintedAsNearBlack() {
        assertFalse(new ColorCell(3, 0, "a").isColor());
        assertFalse(new ColorCell(0, 0, "a").isColor());
        assertTrue(new ColorCell(0xFF0000, 0, "a").isColor());
        assertTrue(new ColorCell(0xFFFF0000, 0, "a").isColor());
    }

    // -- Batching.

    @Test
    void framesInOneBatchAreInsertedWithOneTableEvent() {
        GuiModel model = new GuiModel();
        int[] inserts = {0};
        model.frames.addTableModelListener(e -> inserts[0]++);
        model.beginBatch();
        for (int i = 0; i < 50; i++) {
            DecodedFrame chat = FrameDispatcher.decode(new RawFrame(FrameType.CHAT_MESSAGE.id(), TestMessages.chatMessage().toByteArray()));
            model.accept(chat, "line");
        }
        assertEquals(0, model.frames.getRowCount());
        model.endBatch();
        assertEquals(50, model.frames.getRowCount());
        assertEquals(1, inserts[0]);
    }

    @Test
    void oneSnapshotIsOneTableUpdate() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(10));
        int[] updates = {0};
        model.players.addTableModelListener(e -> updates[0]++);
        model.onStatsSnapshot(snapshot(20));
        assertEquals(1, updates[0]);
    }

    @Test
    void recentEventsAreThoseOfTheLastFewGameSeconds() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1")); // 60 logic frames per second
        model.onEvents(event(StatEvent.newBuilder().setFrame(100).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL)));
        model.onStatsSnapshot(snapshot(100 + 60 * GuiModel.RECENT_EVENT_SECONDS));
        assertTrue(model.isRecent(model.events.get(0)));
        model.onStatsSnapshot(snapshot(100 + 60 * GuiModel.RECENT_EVENT_SECONDS + 1));
        assertFalse(model.isRecent(model.events.get(0)));
    }
}
