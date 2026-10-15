package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import com.generalsonline.stats.proto.BattlePlan;
import com.generalsonline.stats.proto.BattlePlanTransition;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.NetworkStats;
import com.generalsonline.stats.proto.RadarState;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.OnlineMatch;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.ProductionItem;
import com.generalsonline.stats.proto.ProductionKind;
import com.generalsonline.stats.proto.ProductionQueue;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The GUI's model runs without a display, so its update rules are tested here; the window itself is not. */
class GuiModelTest {
    private static final String DOT = " " + (char) 0xB7 + " ";
    private static final String MAP = "maps" + (char) 92 + "twilight" + (char) 92 + "twilight.map";

    private static MatchInfo started(String id) {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId(id).setMapName("Alpine")
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("Alice").setSide("America").setTeam(1).setColor(0x00FF0000).setIsHuman(true))
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(3).setName("Bob").setSide("GLA").setTeam(2).setIsHuman(true))
            .build();
    }

    private static StatsSnapshot snapshot(int frame, int money, boolean withEvent) {
        StatsSnapshot.Builder b = StatsSnapshot.newBuilder().setFrame(frame).setFull(true).setGameSpeedFps(30).setGameSpeedRatio(1.0f)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(money))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(100));
        return b.build();
    }

    private static Events killEvent(int frame) {
        return Events.newBuilder().setFrame(frame)
            .addEvents(StatEvent.newBuilder().setFrame(frame).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("Tank").setCost(800)).build();
    }

    @Test
    void snapshotIsJoinedWithMatchInfoByPlayerIndex() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(900, 1500, true));
        model.onEvents(killEvent(900));

        assertEquals(2, model.players.getRowCount());
        assertEquals("Alice", model.players.valueAt(0, "Name"));
        assertEquals("America", model.players.valueAt(0, "Side"));
        assertEquals(1500L, model.players.valueAt(0, "Money"));
        assertEquals("Alice", model.events.valueAt(0, "Player"));
        assertEquals("KILL", model.events.valueAt(0, "Kind"));
        assertEquals("0:30", model.events.valueAt(0, "Game time"));
    }

    @Test
    void snapshotBeforeMatchInfoFallsBackToIndexAndLaterMatchInfoFillsItIn() {
        GuiModel model = new GuiModel();
        model.onStatsSnapshot(snapshot(10, 1, false));
        assertEquals("P2", model.players.valueAt(0, "Name"));

        model.onMatchInfo(started("m1"));
        assertEquals("Alice", model.players.valueAt(0, "Name"));
        assertEquals(2, model.players.getRowCount());
    }

    @Test
    void rowsUpdateInPlace() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(15, 1000, false));
        model.onStatsSnapshot(snapshot(30, 2000, false));

        assertEquals(2, model.players.getRowCount());
        assertEquals(2000L, model.players.valueAt(0, "Money"));
        assertEquals(2, model.snapshotCount());
    }

    @Test
    void stallSnapshotWithNoPlayersKeepsTheTable() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onStatsSnapshot(snapshot(15, 1000, true));
        model.onEvents(killEvent(15));
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
        model.onEvents(killEvent(15));

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
        assertEquals("Alice", model.lobbySlots.valueAt(0, "Name"));
        assertEquals("Random -> America", model.lobbySlots.valueAt(0, "Side"));
        assertTrue(model.lobbyHeader().contains("prediction valid: true"));

        model.onLobbyState(LobbyState.newBuilder().setInLobby(false).build());
        assertEquals(0, model.lobbySlots.getRowCount());
        assertEquals("not in a lobby", model.lobbyHeader());
    }

    @Test
    void chatSummaryAndMatchTablesFill() {
        GuiModel model = new GuiModel();
        model.onChatMessage(TestMessages.chatMessage());
        assertEquals("ROOM", model.chat.valueAt(0, "Channel"));
        assertEquals("local", model.chat.valueAt(0, "Flags"));

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
            accept(model, new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
        }
        accept(model, new RawFrame(0x7f, new byte[] {1, 2, 3}));
        accept(model, new RawFrame(FrameType.STATS_SNAPSHOT.id(), new byte[] {(byte) 0xff, (byte) 0xff}));

        assertEquals(10, model.frames.getRowCount());
        for (int i = 0; i < 8; i++) {
            assertFalse(((String) model.frames.getValueAt(i, 3)).isEmpty(), "summary of row " + i);
        }
        assertTrue(((String) model.frames.getValueAt(8, 1)).startsWith("UNKNOWN"));
        assertTrue(((String) model.frames.getValueAt(9, 3)).startsWith("DECODE ERROR"));
    }

    /** What the feed does on the event thread: summary built from the decoded frame, then the model's single entry. */
    private static void accept(GuiModel model, RawFrame raw) {
        DecodedFrame decoded = FrameDispatcher.decode(raw);
        model.accept(decoded, FrameSummary.of(decoded));
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
    void gameTimeFormatsAtTheGivenLogicRate() {
        assertEquals("0:00", Format.gameTime(0, 30));
        assertEquals("1:05", Format.gameTime(65 * 30, 30));
        assertEquals("1:05", Format.gameTime(65 * 60, 60));
        assertEquals("1:01:01", Format.gameTime(3661 * 30, 30));
    }

    @Test
    void gameTimeAndSpeedUseTheLogicRateFromMatchInfo() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1").toBuilder().setLogicFramesPerSecond(60).build());
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(60 * 90).setGameSpeedFps(60).setGameSpeedRatio(1.0f).build());
        assertTrue(model.progressLine().contains("Game time: 1:30"), model.progressLine());
        assertTrue(model.progressLine().contains("Speed: 1.00x"), model.progressLine());
        assertFalse(model.progressLine().contains("(60 fps)"));

        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(60 * 90).setGameSpeedFps(30).setGameSpeedRatio(0.5f).build());
        assertTrue(model.progressLine().contains("0.50x (30 fps)"), model.progressLine());
    }

    @Test
    void productionSummaryCountsItemsAndListsTheMostCompleteFirst() {
        PlayerStats stats = PlayerStats.newBuilder()
            .addProduction(ProductionQueue.newBuilder().setStructureTemplate("AmericaWarFactory")
                .addEntries(ProductionItem.newBuilder().setKind(ProductionKind.PRODUCTION_KIND_UNIT).setName("AmericaTankCrusader").setPercentComplete(92))
                .addEntries(ProductionItem.newBuilder().setKind(ProductionKind.PRODUCTION_KIND_UNIT).setName("AmericaTankCrusader")))
            .addProduction(ProductionQueue.newBuilder().setStructureTemplate("AmericaBarracks")
                .addEntries(ProductionItem.newBuilder().setKind(ProductionKind.PRODUCTION_KIND_UNIT).setName("AmericaInfantryRanger").setPercentComplete(40))
                .addEntries(ProductionItem.newBuilder().setKind(ProductionKind.PRODUCTION_KIND_UNIT).setName("AmericaInfantryRanger")))
            .build();
        assertEquals("4" + DOT + "Crusader 92%, Ranger 40%, +2", CellText.productionText(stats));
        assertTrue(CellText.productionDetail(stats).contains("AmericaTankCrusader 92%"));
        assertEquals("", CellText.productionText(PlayerStats.getDefaultInstance()));

        PlayerStats upgrade = PlayerStats.newBuilder().addProduction(ProductionQueue.newBuilder()
            .addEntries(ProductionItem.newBuilder().setKind(ProductionKind.PRODUCTION_KIND_UPGRADE).setName("Upgrade_AmericaAdvancedTraining").setPercentComplete(10))).build();
        assertEquals("1" + DOT + "AdvancedTraining (upgrade) 10%", CellText.productionText(upgrade));
    }

    @Test
    void namesAreShortenedOnlyForKnownPrefixes() {
        assertEquals("Crusader", Format.shortName("AirF_AmericaTankCrusader"));
        assertEquals("Redguard", Format.shortName("ChinaInfantryRedguard"));
        assertEquals("Terrorist", Format.shortName("GLAInfantryTerrorist"));
        assertEquals("Redguard", Format.shortName("Boss_InfantryRedguard"));
        assertEquals("SomethingElse", Format.shortName("SomethingElse"));
        assertEquals("Americana", Format.shortName("Americana"));
        assertEquals("NeutronMissile", Format.shortPowerName("SuperweaponNeutronMissile"));
    }

    @Test
    void superweaponsShowReadyThenTimeLeftThenChargePercent() {
        PlayerStats stats = PlayerStats.newBuilder()
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponScudStorm").setReadyFrame(30 * 200).setState(SuperweaponState.SUPERWEAPON_CHARGING))
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponParticleUplinkCannon").setReadyFrame(30 * 83).setState(SuperweaponState.SUPERWEAPON_CHARGING))
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponNeutronMissile").setReadyFrame(5).setPercentReady(1.0f).setState(SuperweaponState.SUPERWEAPON_READY))
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponDaisyCutter").setPercentReady(0.25f).setState(SuperweaponState.SUPERWEAPON_CHARGING))
            .build();
        assertEquals("NeutronMissile READY, ParticleUplinkCannon 1:23, ScudStorm 3:20, DaisyCutter 25%", CellText.superweaponText(stats, 0, 30));
        assertEquals("", CellText.superweaponText(PlayerStats.getDefaultInstance(), 0, 30));
        assertEquals("ScudStorm 0:00", CellText.superweaponText(PlayerStats.newBuilder()
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponScudStorm").setReadyFrame(10)).build(), 500, 30));
    }

    @Test
    void onlineMatchIdShowsAsNotAssignedUntilItArrives() {
        GuiModel model = new GuiModel();
        MatchInfo noId = started("m1").toBuilder().setOnline(OnlineMatch.newBuilder().setLobbyId(276327).setLobbyName("Lobby")).build();
        model.onMatchInfo(noId);
        assertEquals("not assigned yet", valueOf(model, "Online match id"));

        model.onMatchInfo(noId.toBuilder().setOnline(noId.getOnline().toBuilder().setMatchId(98765)).build());
        assertEquals("98765", valueOf(model, "Online match id"));
        assertEquals("m1", valueOf(model, "Match id"));
    }

    @Test
    void userMapIsMarkedAndLogicRateShown() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1").toBuilder().setMapName(MAP).setMapIsUserMap(true).setLogicFramesPerSecond(60).build());
        assertEquals(MAP + " (user map)", valueOf(model, "Map"));
        assertEquals("60 frames per second", valueOf(model, "Logic rate"));
    }

    @Test
    void lightSnapshotKeepsTheHeavyColumnsOfTheLastFullOne() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        PlayerStats heavy = PlayerStats.newBuilder().setPlayerIndex(2).setMoney(100).setArmyValue(5000).setWorkers(6)
            .addProduction(ProductionQueue.newBuilder().addEntries(ProductionItem.newBuilder().setName("AmericaTankCrusader").setPercentComplete(50)))
            .addHeroes(HeroStatus.newBuilder().setTemplateName("Hero").setAlive(true))
            .addSuperweapons(SuperweaponTimer.newBuilder().setPowerName("SuperweaponScudStorm").setReadyFrame(30 * 100))
            .setBattlePlanTransition(BattlePlanTransition.BATTLE_PLAN_DEPLOYING)
            .build();
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(15).setFull(true).addPlayers(heavy).build());

        PlayerStats light = PlayerStats.newBuilder().setPlayerIndex(2).setMoney(250).setScore(77).build();
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(21).setFull(false).addPlayers(light).build());

        assertEquals(1, model.players.getRowCount());
        assertEquals(250L, model.players.valueAt(0, "Money"));
        PlayerStats shown = model.players.get(0).stats();
        assertEquals(5000, shown.getArmyValue());
        assertEquals(6, shown.getWorkers());
        assertEquals("1" + DOT + "Crusader 50%", CellText.productionText(shown));
        assertEquals("1 alive / 0 dead", CellText.heroesText(shown));
        assertEquals("ScudStorm 1:39", CellText.superweaponText(shown, 21, 30));
        assertEquals("deploying", CellText.battlePlanText(shown));

        // a full snapshot replaces everything again
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(30).setFull(true).addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(1).build()).build());
        assertEquals(0, model.players.get(0).stats().getArmyValue());
    }

    @Test
    void eventsFrameFillsTheEventsTableWithDetailForStateChanges() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started("m1"));
        model.onEvents(Events.newBuilder().setFrame(100)
            .addEvents(StatEvent.newBuilder().setFrame(100).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_RADAR_CHANGED).setIntValue1(2).setIntValue2(1))
            .addEvents(StatEvent.newBuilder().setFrame(100).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_BATTLE_PLAN_CHANGED).setIntValue1(0).setIntValue2(1))
            .addEvents(StatEvent.newBuilder().setFrame(100).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_CAPTURE).setIntValue1(3).setVariant("tech").setPrimaryName("OilDerrick").setCost(1000))
            .addEvents(StatEvent.newBuilder().setFrame(100).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_POWER_SABOTAGED).setIntValue1(30 * 45))
            .build());

        assertEquals(4, model.events.getRowCount());
        assertEquals("ACTIVE -> UNPOWERED", model.events.valueAt(0, "Detail"));
        assertEquals("NONE -> BOMBARDMENT", model.events.valueAt(1, "Detail"));
        assertEquals("from Bob (tech)", model.events.valueAt(2, "Detail"));
        assertEquals(1000L, model.events.valueAt(2, "Cost"));
        assertEquals("for 0:45", model.events.valueAt(3, "Detail"));
    }

    @Test
    void playerCellsForRadarBattlePlanSabotageAndNetwork() {
        PlayerStats stats = PlayerStats.newBuilder().setRadar(RadarState.RADAR_UNPOWERED)
            .setBattlePlan(BattlePlan.BATTLE_PLAN_HOLD_THE_LINE).setBattlePlanTransition(BattlePlanTransition.BATTLE_PLAN_ACTIVE)
            .setPowerSabotaged(true).setPowerSabotageFramesLeft(30 * 75)
            .setNetwork(NetworkStats.newBuilder().setReportedFps(58).setLatencyMs(120).setPingMs(45).setRelayed(true))
            .build();
        assertEquals("UNPOWERED", CellText.radarText(stats));
        assertEquals("HOLD THE LINE", CellText.battlePlanText(stats));
        assertEquals("yes, 1:15 left", CellText.sabotageText(stats, 30));
        assertEquals("120 ms / 58 fps / ping 45 ms (relay)", CellText.networkText(stats));

        PlayerStats unknown = PlayerStats.newBuilder().setNetwork(NetworkStats.newBuilder().setReportedFps(-1).setLatencyMs(-1).setPingMs(-1)).build();
        assertEquals("? ms / ? fps", CellText.networkText(unknown));
        assertEquals("", CellText.networkText(PlayerStats.getDefaultInstance()));
        assertEquals("", CellText.sabotageText(PlayerStats.getDefaultInstance(), 30));
        assertEquals("packing", CellText.battlePlanText(PlayerStats.newBuilder().setBattlePlanTransition(BattlePlanTransition.BATTLE_PLAN_PACKING).build()));
    }

    @Test
    void cameraFramesFeedTheStatusLineWithCentreSizeAndRotation() {
        GuiModel model = new GuiModel();
        assertEquals("Camera: -", model.cameraLine());
        model.onCamera(TestMessages.camera(), 500);

        assertTrue(model.cameraLine().contains("looking at 101, 200"), model.cameraLine());
        assertTrue(model.cameraLine().contains("500 camera frames"), model.cameraLine());
        assertTrue(model.cameraLine().contains("view centre"), model.cameraLine());
        assertTrue(model.cameraLine().contains("turned"), model.cameraLine());
    }

    @Test
    void localFpsAppearsInTheProgressLine() {
        GuiModel model = new GuiModel();
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(10).setLocalRenderFps(143.4f).setLocalLogicFps(59.6f).build());
        assertTrue(model.progressLine().contains("render 143 fps, logic 60 fps"), model.progressLine());
    }

    private static String valueOf(GuiModel model, String key) {
        for (int i = 0; i < model.matchInfo.getRowCount(); i++) {
            if (key.equals(model.matchInfo.getValueAt(i, 0))) {
                return (String) model.matchInfo.getValueAt(i, 1);
            }
        }
        return null;
    }
}
