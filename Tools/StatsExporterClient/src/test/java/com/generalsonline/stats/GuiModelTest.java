package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The GUI's model runs without a display, so its update rules are tested here; the window itself is not. */
class GuiModelTest {
    private static MatchInfo started(String id) {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId(id).setMapName("Alpine")
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("Alice").setSide("America").setTeam(1).setColor(0x00FF0000))
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(3).setName("Bob").setSide("GLA").setTeam(2))
            .build();
    }

    private static StatsSnapshot snapshot(int frame, int money, boolean withEvent) {
        StatsSnapshot.Builder b = StatsSnapshot.newBuilder().setFrame(frame).setGameSpeedFps(30).setGameSpeedRatio(1.0f)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(money))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(100));
        if (withEvent) {
            b.addEvents(StatEvent.newBuilder().setFrame(frame).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("Tank").setCost(800));
        }
        return b.build();
    }

    @Test
    void snapshotIsJoinedWithMatchInfoByPlayerIndex() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(900, 1500, true));

        assertEquals(2, model.players.getRowCount());
        assertEquals("Alice", model.players.getValueAt(0, 1));
        assertEquals("America", model.players.getValueAt(0, 2));
        assertEquals(1500L, model.players.getValueAt(0, 4));
        assertEquals("Alice", model.events.getValueAt(0, 2));
        assertEquals("KILL", model.events.getValueAt(0, 3));
        assertEquals("0:30", model.events.getValueAt(0, 1));
    }

    @Test
    void snapshotBeforeMatchInfoFallsBackToIndexAndLaterMatchInfoFillsItIn() {
        GuiModel model = new GuiModel();
        model.onStatsSnapshot(snapshot(10, 1, false));
        assertEquals("P2", model.players.getValueAt(0, 1));

        model.onMatchInfo(started("m1"));
        assertEquals("Alice", model.players.getValueAt(0, 1));
        assertEquals(2, model.players.getRowCount());
    }

    @Test
    void rowsUpdateInPlace() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(15, 1000, false));
        model.onStatsSnapshot(snapshot(30, 2000, false));

        assertEquals(2, model.players.getRowCount());
        assertEquals(2000L, model.players.getValueAt(0, 4));
        assertEquals(2, model.snapshotCount());
    }

    @Test
    void stallSnapshotWithNoPlayersKeepsTheTable() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(15, 1000, true));
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(16).setNetworkStalled(true).setStalledFrames(30).build());

        assertEquals(2, model.players.getRowCount());
        assertEquals(1, model.events.getRowCount());
        assertTrue(model.progressLine().contains("STALLED 30"));
    }

    @Test
    void newMatchIdClearsPlayersAndEventsButEndedAndReconnectDoNot() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(15, 1000, true));

        model.onDisconnected(null);
        assertTrue(model.connectionLine().contains("disconnected"));
        model.onConnected("pipe");
        model.onMatchInfo(started("m1"));
        assertEquals(2, model.players.getRowCount());

        model.onMatchInfo(started("m1").toBuilder().setPhase(MatchPhase.MATCH_PHASE_ENDED).build());
        assertEquals(2, model.players.getRowCount());
        assertEquals(1, model.events.getRowCount());

        model.onMatchInfo(started("m2"));
        assertEquals(0, model.players.getRowCount());
        assertEquals(0, model.events.getRowCount());
    }

    @Test
    void predictionCheckAppearsOnLoading() {
        GuiModel model = new GuiModel();
        assertNull(model.predictionCheckOk());
        model.onMatchInfo(TestMessages.matchInfo());
        assertEquals(Boolean.TRUE, model.predictionCheckOk());
        assertTrue(model.predictionCheckText().contains("MATCHED"));

        model.onMatchInfo(TestMessages.matchInfo().toBuilder().setPredictionMatched(false).build());
        assertEquals(Boolean.FALSE, model.predictionCheckOk());
    }

    @Test
    void lobbyIsShownAndClearedOnLeaving() {
        GuiModel model = new GuiModel();
        model.onLobbyState(TestMessages.lobbyState());
        assertEquals(1, model.lobbySlots.getRowCount());
        assertEquals("Alice", model.lobbySlots.getValueAt(0, 2));
        assertEquals("America", model.lobbySlots.getValueAt(0, 9));
        assertTrue(model.lobbyHeader().contains("prediction valid: true"));

        model.onLobbyState(LobbyState.newBuilder().setInLobby(false).build());
        assertEquals(0, model.lobbySlots.getRowCount());
        assertEquals("not in a lobby", model.lobbyHeader());
    }

    @Test
    void chatSummaryAndMatchTablesFill() {
        GuiModel model = new GuiModel();
        model.onChatMessage(TestMessages.chatMessage());
        assertEquals("ROOM", model.chat.getValueAt(0, 1));
        assertEquals("local", model.chat.getValueAt(0, 5));

        model.onMatchSummary(TestMessages.matchSummary());
        assertEquals(1, model.summary.getRowCount());

        model.onMatchInfo(TestMessages.matchInfo());
        assertTrue(model.matchInfo.getRowCount() > 5);
    }

    @Test
    void speedTextShowsUncappedAndHalted() {
        GuiModel model = new GuiModel();
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(300).setGameSpeedFps(-1).build());
        assertTrue(model.progressLine().contains("uncapped"));
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(300).setGameSpeedFps(0).build());
        assertTrue(model.progressLine().contains("paused/halted"));
    }

    @Test
    void frameLogSummarizesEveryTypeAndToleratesUnknownAndBadFrames() {
        GuiModel model = new GuiModel();
        for (Message message : TestMessages.all()) {
            model.onRawFrame(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
        }
        model.onRawFrame(new RawFrame(0x7f, new byte[] {1, 2, 3}));
        model.onRawFrame(new RawFrame(FrameType.STATS_SNAPSHOT.id(), new byte[] {(byte) 0xff, (byte) 0xff}));

        assertEquals(8, model.frames.getRowCount());
        for (int i = 0; i < 6; i++) {
            assertFalse(((String) model.frames.getValueAt(i, 3)).isEmpty(), "summary of row " + i);
        }
        assertTrue(((String) model.frames.getValueAt(6, 1)).startsWith("UNKNOWN"));
        assertTrue(((String) model.frames.getValueAt(7, 3)).startsWith("DECODE ERROR"));
        assertEquals(8, model.framesTotal());
    }

    @Test
    void logsAreCapped() {
        GuiModel model = new GuiModel();
        ChatMessage line = TestMessages.chatMessage();
        for (int i = 0; i < GuiModel.MAX_LOG_ROWS + 50; i++) {
            model.onChatMessage(line);
        }
        assertEquals(GuiModel.MAX_LOG_ROWS, model.chat.getRowCount());
    }

    @Test
    void gameTimeFormats() {
        assertEquals("0:00", GuiModel.gameTime(0));
        assertEquals("1:05", GuiModel.gameTime(65 * 30));
        assertEquals("1:01:01", GuiModel.gameTime(3661 * 30));
    }
}
