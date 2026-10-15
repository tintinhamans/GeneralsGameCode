package com.generalsonline.stats.app;

import com.generalsonline.stats.FrameType;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CategoryCounts;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.LobbyKind;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchMode;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.MatchSummaryPlayer;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiScreen;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.Message;

/** One filled-in message of every outbound frame type, built in code. Shared by the tests of this package and of the internal one. */
final class TestMessages {
    private TestMessages() {
    }

    static MatchInfo matchInfo() {
        return MatchInfo.newBuilder()
            .setPhase(MatchPhase.MATCH_PHASE_LOADING)
            .setMatchId("123456789")
            .setMode(MatchMode.MATCH_MODE_ONLINE)
            .setMapName("maps\\tournament desert")
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(-1).setName("Alice").setSide("America").setStartPosition(2))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(-1).setName("Bob").setSide("GLA").setStartPosition(0))
            .setHasPredictionCheck(true)
            .setPredictionMatched(true)
            .setRandomStateClean(true)
            .build();
    }

    static StatsSnapshot statsSnapshot() {
        return StatsSnapshot.newBuilder()
            .setFrame(900)
            .addPlayers(PlayerStats.newBuilder()
                .setPlayerIndex(2).setMoney(1500).setMoneyPerMinute(900).setArmyValue(4200).setApmCurrent(120)
                .addSciences("SCIENCE_Paladin").addUpgrades("Upgrade_AmericaRadar")
                .setKills(CategoryCounts.newBuilder().setInfantry(3).setVehicle(1)))
            .addGrids(PlayerGrid.newBuilder().setPlayerIndex(2).setWidth(32).setHeight(32).setCellWidth(10.0f).setCellHeight(10.0f).addCells(1).addCells(0))
            .setFull(true)
            .setGameSpeedFps(30)
            .setNetworkStalled(true)
            .setStalledFrames(45)
            .build();
    }

    static Events events() {
        return Events.newBuilder().setFrame(880)
            .addEvents(StatEvent.newBuilder().setFrame(880).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("ChinaTankGattling").setCost(800))
            .addEvents(StatEvent.newBuilder().setFrame(880).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_RADAR_CHANGED).setIntValue1(2).setIntValue2(1))
            .build();
    }

    static Camera camera() {
        return Camera.newBuilder().setTimestampUs(123456789L).setLogicFrame(900).setX(100.5f).setY(200.25f).setZoom(1.5f).setAngle(0.25f).setPitch(0.75f)
            .setHeightAboveGround(300f).setHasViewBox(true).setViewTlX(1).setViewTlY(2).setViewTrX(3).setViewTrY(4).setViewBrX(5).setViewBrY(6).setViewBlX(7).setViewBlY(8)
            .build();
    }

    static MatchSummary matchSummary() {
        return MatchSummary.newBuilder()
            .setFrame(54000)
            .setMapName("maps\\tournament desert")
            .addPlayers(MatchSummaryPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(2).setPlayerName("Alice").setScore(1000).setIsWinner(true))
            .build();
    }

    static ChatMessage chatMessage() {
        return ChatMessage.newBuilder()
            .setChannel(ChatChannel.CHAT_CHANNEL_ROOM)
            .setSenderName("Alice")
            .setText("gg éè 中")
            .setAudience(ChatAudience.CHAT_AUDIENCE_EVERYONE)
            .setIsLocal(true)
            .build();
    }

    static UiState uiState() {
        return UiState.newBuilder()
            .setScreen(UiScreen.UI_SCREEN_ONLINE_GAME_SETUP)
            .setLayout("Menus/GameSpyGameOptionsMenu.wnd")
            .setGameMode(MatchMode.MATCH_MODE_ONLINE)
            .build();
    }

    static LobbyState lobbyState() {
        return LobbyState.newBuilder()
            .setInLobby(true)
            .setKind(LobbyKind.LOBBY_KIND_ONLINE)
            .setMapName("maps\\tournament desert")
            .addSlots(LobbySlot.newBuilder().setState(LobbySlotState.LOBBY_SLOT_HUMAN).setIsReady(true)
                .setPlayer(MatchPlayer.newBuilder().setLobbySlot(0).setName("Alice").setSide("Random").setIsSideRandom(true))
                .setPredictedSide("America"))
            .setPredictionValid(true)
            .setPredictionStartPositionsValid(true)
            .build();
    }

    /** The frame type id each message type travels under. */
    static int frameTypeOf(Message message) {
        if (message instanceof MatchInfo) {
            return FrameType.MATCH_INFO.id();
        }
        if (message instanceof StatsSnapshot) {
            return FrameType.STATS_SNAPSHOT.id();
        }
        if (message instanceof MatchSummary) {
            return FrameType.MATCH_SUMMARY.id();
        }
        if (message instanceof ChatMessage) {
            return FrameType.CHAT_MESSAGE.id();
        }
        if (message instanceof UiState) {
            return FrameType.UI_STATE.id();
        }
        if (message instanceof LobbyState) {
            return FrameType.LOBBY_STATE.id();
        }
        if (message instanceof Events) {
            return FrameType.EVENTS.id();
        }
        if (message instanceof Camera) {
            return FrameType.CAMERA.id();
        }
        throw new IllegalArgumentException(message.getClass().getName());
    }

    static Message[] all() {
        return new Message[] {matchInfo(), statsSnapshot(), matchSummary(), chatMessage(), uiState(), lobbyState(), events(), camera()};
    }
}
