package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CategoryCounts;
import com.generalsonline.stats.proto.CategoryValue;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchMode;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.generalsonline.stats.proto.TemplateCount;
import com.generalsonline.stats.proto.UiScreen;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.Message;

import java.io.IOException;
import java.nio.file.Path;

/**
 * A small made-up game for the fake game's tests: ten seconds at 30 logic frames a second, two players (Alice 2, America;
 * Bob 3, China) on a 4000 by 4000 map, snapshots every 3 frames (a full one every 9), build, kill, loss and superweapon
 * events, camera frames and a chat line, with the Hello first.
 */
final class FakeDumpFixture {
    static final int LAST_FRAME = 300;
    static final int ALICE = 2;
    static final int BOB = 3;

    private FakeDumpFixture() {
    }

    static Path write(Path dir) throws IOException {
        Path file = dir.resolve("fixture.dump");
        try (DumpWriter writer = new DumpWriter(file)) {
            long[] sequence = {0};
            add(writer, sequence, TestMessages.hello());
            add(writer, sequence, UiState.newBuilder().setScreen(UiScreen.UI_SCREEN_ONLINE_GAME_SETUP).build());
            add(writer, sequence, match(MatchPhase.MATCH_PHASE_LOADING, false));
            add(writer, sequence, match(MatchPhase.MATCH_PHASE_STARTED, true));
            for (int frame = 3; frame <= LAST_FRAME; frame += 3) {
                if (frame % 15 == 0) {
                    add(writer, sequence, camera(frame));
                }
                for (Events events : eventsAt(frame)) {
                    add(writer, sequence, events);
                }
                add(writer, sequence, snapshot(frame, frame % 9 == 0));
                if (frame == 99) {
                    add(writer, sequence, ChatMessage.newBuilder().setChannel(ChatChannel.CHAT_CHANNEL_MATCH).setSenderName("Alice").setText("gg").build());
                }
            }
            add(writer, sequence, match(MatchPhase.MATCH_PHASE_ENDED, true).toBuilder().setDurationFrames(LAST_FRAME).build());
            add(writer, sequence, MatchSummary.newBuilder().setFrame(LAST_FRAME).build());
        }
        return file;
    }

    private static void add(DumpWriter writer, long[] sequence, Message message) throws IOException {
        long n = sequence[0]++;
        writer.write(new RawFrame(TestMessages.frameTypeOf(message), n, n * 1000, message.toByteArray().length == 0 ? new byte[] {0x08, 0x00} : message.toByteArray()));
    }

    static MatchInfo match(MatchPhase phase, boolean extent) {
        MatchInfo.Builder b = MatchInfo.newBuilder().setPhase(phase).setMatchId("fixture-1").setMode(MatchMode.MATCH_MODE_REPLAY).setMapName("maps\\fixture ridge\\fixture ridge.map")
            .setLogicFramesPerSecond(30).setLocalPlayerIndex(-1)
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(ALICE).setName("Alice").setSide("America").setColor(0xFFFF0000).setTeam(0).setIsHuman(true)
                .setStartPosition(0).setHasStartLocation(extent).setStartX(500).setStartY(500))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(BOB).setName("Bob").setSide("China").setColor(0xFF0000FF).setTeam(1).setIsHuman(true)
                .setStartPosition(1).setHasStartLocation(extent).setStartX(3500).setStartY(3500));
        if (extent) {
            b.setHasWorldExtent(true).setWorldMinX(0).setWorldMinY(0).setWorldMaxX(4000).setWorldMaxY(4000);
        }
        return b.build();
    }

    static Camera camera(int frame) {
        float x = 1000 + frame;
        float y = 1500;
        return Camera.newBuilder().setTimestampUs(frame * 33_000L).setLogicFrame(frame).setX(x).setY(y).setZoom(1f).setAngle(0f).setPitch(0.75f).setHasViewBox(true)
            .setViewTlX(x - 700).setViewTlY(y + 300).setViewTrX(x + 700).setViewTrY(y + 300).setViewBrX(x + 500).setViewBrY(y - 300).setViewBlX(x - 500).setViewBlY(y - 300).build();
    }

    private static StatEvent.Builder event(int frame, int player, StatEventKind kind) {
        return StatEvent.newBuilder().setFrame(frame).setPlayerIndex(player).setKind(kind);
    }

    static Events[] eventsAt(int frame) {
        return switch (frame) {
            case 30 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, ALICE, StatEventKind.STATEVENT_BUILD).setCategory("VEHICLE")
                .setPrimaryName("AmericaTankCrusader").setObjectId(101).setHasLocation(true).setX(600).setY(600)).build()};
            case 33 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, ALICE, StatEventKind.STATEVENT_BUILD).setCategory("INFANTRY")
                .setPrimaryName("AmericaInfantryColonelBurton").setObjectId(77).setHasLocation(true).setX(1300).setY(1500)).build()};
            case 42 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, ALICE, StatEventKind.STATEVENT_BUILD).setCategory("STRUCTURE")
                .setPrimaryName("AmericaBarracks").setObjectId(102).setHasLocation(true).setX(500).setY(700)).build()};
            case 60 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, BOB, StatEventKind.STATEVENT_BUILD).setCategory("VEHICLE")
                .setPrimaryName("ChinaTankOverlord").setObjectId(201).setHasLocation(true).setX(3400).setY(3400)).build()};
            case 90 -> new Events[] {Events.newBuilder().setFrame(frame)
                .addEvents(event(frame, ALICE, StatEventKind.STATEVENT_KILL).setCategory("VEHICLE").setPrimaryName("ChinaTankOverlord").setCost(2000).setObjectId(201)
                    .setKillerObjectId(101).setKillerTemplate("AmericaTankCrusader").setWeapon("CrusaderTankGun").setHasLocation(true).setX(2000).setY(2000))
                .addEvents(event(frame, BOB, StatEventKind.STATEVENT_LOSS).setCategory("VEHICLE").setPrimaryName("ChinaTankOverlord").setCost(2000).setObjectId(201)
                    .setHasLocation(true).setX(2000).setY(2000)).build()};
            case 120 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, BOB, StatEventKind.STATEVENT_ABILITY_USE).setCategory("SUPERWEAPON")
                .setPrimaryName("SuperweaponNeutronMissile").setSecondaryName("ChinaNuclearMissileLauncher").setHasLocation(true).setX(600).setY(600)).build()};
            case 150 -> new Events[] {Events.newBuilder().setFrame(frame).addEvents(event(frame, ALICE, StatEventKind.STATEVENT_LOSS).setCategory("STRUCTURE")
                .setPrimaryName("AmericaBarracks").setCost(1500).setObjectId(102).setHasLocation(true).setX(500).setY(700)).build()};
            default -> new Events[0];
        };
    }

    static StatsSnapshot snapshot(int frame, boolean full) {
        StatsSnapshot.Builder b = StatsSnapshot.newBuilder().setFrame(frame).setFull(full).setGameSpeedFps(30).setGameSpeedRatio(1f);
        b.addPlayers(player(ALICE, frame, full)).addPlayers(player(BOB, frame, full));
        if (full) {
            b.addGrids(grid(ALICE, new int[] {3, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
            b.addGrids(grid(BOB, new int[] {0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4}));
        }
        return b.build();
    }

    private static PlayerStats.Builder player(int index, int frame, boolean full) {
        PlayerStats.Builder p = PlayerStats.newBuilder().setPlayerIndex(index).setMoney(1000 + frame * 10).setMoneyPerMinute(600);
        if (full) {
            p.setArmyValue(index == ALICE ? 9000 : 12000)
                .setArmyValueByClass(CategoryValue.newBuilder().setInfantry(1000).setVehicle(index == ALICE ? 6000 : 9000).setStructure(2000))
                .setArmyCountByClass(CategoryCounts.newBuilder().setInfantry(10).setVehicle(index == ALICE ? 6 : 9).setStructure(3))
                .addLiveTemplateCounts(TemplateCount.newBuilder().setTemplateName(index == ALICE ? "AmericaTankCrusader" : "ChinaTankOverlord").setCount(2))
                .addSciences(index == ALICE ? "SCIENCE_Paladin" : "SCIENCE_Redguard");
            if (index == ALICE) {
                p.addHeroes(HeroStatus.newBuilder().setTemplateName("AmericaInfantryColonelBurton").setAlive(true).setObjectId(77));
            } else {
                p.addSuperweapons(SuperweaponTimer.newBuilder().setObjectId(300).setTemplateName("ChinaNuclearMissileLauncher").setPowerName("SuperweaponNeutronMissile").setReadyFrame(900).setPercentReady(0.3f));
            }
        }
        return p;
    }

    private static PlayerGrid grid(int player, int[] cells) {
        PlayerGrid.Builder g = PlayerGrid.newBuilder().setPlayerIndex(player).setWidth(4).setHeight(4).setCellWidth(1000f).setCellHeight(1000f);
        for (int c : cells) {
            g.addCells(c);
        }
        return g.build();
    }
}
