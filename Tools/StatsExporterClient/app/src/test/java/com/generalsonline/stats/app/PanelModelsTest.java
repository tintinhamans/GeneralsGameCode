package com.generalsonline.stats.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.NotAllowedException;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.RequestTimeoutException;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.NotableUnit;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.PageInfo;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.PlayerViewStats;
import com.generalsonline.stats.proto.PowerStatus;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.StartPositionInfo;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.ViewStats;
import com.generalsonline.stats.proto.ViewSuperweapon;
import com.google.protobuf.ByteString;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.awt.Color;
import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;

/** The parts of the new panels that are not Swing: how failures read, how answers are turned into text, paging, the view statistics rows and the overlay helpers. */
class PanelModelsTest {
    @TempDir
    Path dir;

    // -- Failures.

    @Test
    void everyFailureHasAShortKindAndASentence() {
        PipeCommands.Failure refused = PipeCommands.describe(new CompletionException(new NotAllowedException("the local player plays")));
        assertEquals("NOT ALLOWED", refused.kind());
        assertTrue(refused.notAllowed());
        assertTrue(refused.text().contains("the local player plays") && refused.text().contains("observer"), refused.text());
        assertTrue(PipeCommands.describe(new NotAllowedException("")).text().contains("situation"));

        PipeCommands.Failure missing = PipeCommands.describe(new ExecutionException(new RequestFailedException(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no object 5")));
        assertEquals("NOT_FOUND", missing.kind());
        assertEquals("no object 5", missing.text());
        assertFalse(missing.notAllowed());
        assertEquals("RATE_LIMITED: the game answered RATE_LIMITED", PipeCommands.describe(new RequestFailedException(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, "")).line());

        assertEquals("TIMEOUT", PipeCommands.describe(new RequestTimeoutException(3, java.time.Duration.ofSeconds(5))).kind());
        assertEquals("NOT CONNECTED", PipeCommands.describe(new IOException("not connected to a game")).kind());
        assertEquals("NOT READY", PipeCommands.describe(new IllegalStateException("no Hello received yet")).kind());
        assertEquals("INVALID", PipeCommands.describe(new IllegalArgumentException("the speed ratio is 0.1 to 16")).kind());
        assertEquals("ERROR", PipeCommands.describe(new RuntimeException("boom")).kind());
    }

    @Test
    void aCommandWithoutAPipeFailsAtOnceAndAFailingStartIsReported() {
        PipeCommands none = new PipeCommands(() -> null, Runnable::run);
        assertFalse(none.available());
        assertNull(none.pipe());
        List<PipeCommands.Failure> failures = new ArrayList<>();
        none.run(pipe -> CompletableFuture.completedFuture(1), result -> failures.add(null), failures::add);
        assertEquals("NOT CONNECTED", failures.get(0).kind());

        // a pipe that is not connected: the request method fails with an IOException through the future
        com.generalsonline.stats.StatsPipe idle = com.generalsonline.stats.StatsPipe.builder().build();
        PipeCommands unconnected = new PipeCommands(() -> idle, Runnable::run);
        assertFalse(unconnected.available());
        List<String> kinds = new ArrayList<>();
        unconnected.run(pipe -> pipe.object(1), detail -> kinds.add("ok"), failure -> kinds.add(failure.kind()));
        assertEquals(List.of("NOT CONNECTED"), kinds);
        unconnected.run(pipe -> pipe.setReplaySpeed(100), detail -> kinds.add("ok"), failure -> kinds.add(failure.kind()));
        assertEquals("INVALID", kinds.get(1), "a value the client already knows is wrong is reported without a round trip");
    }

    // -- Text.

    @Test
    void anObjectReadsAsLabelledLinesAndLeavesOutWhatTheGameDidNotSay() {
        ObjectDetail detail = ObjectDetail.newBuilder().setObjectId(101).setTemplateName("AmericaTankCrusader").setDisplayName("Crusader").setOwnerPlayerIndex(2).setX(600).setY(650.5f)
            .setHealth(80).setMaxHealth(100).setVeterancy(1).addStatus("IS_ATTACKING").setTargetObjectId(201).setProducerId(102).addUpgrades("Upgrade_Composite").build();
        String text = QueryText.object(detail, index -> index == 2 ? "Alice" : "?");
        assertTrue(text.contains("AmericaTankCrusader  (Crusader)") && text.contains("Alice  (player 2)") && text.contains("80 of 100  (80 percent)"), text);
        assertTrue(text.contains("Veteran") && text.contains("IS_ATTACKING") && text.contains("object 201") && text.contains("object 102") && text.contains("Upgrade_Composite"), text);
        assertFalse(text.contains("Under construction") || text.contains("Inside") || text.contains("Contains"), text);

        String neutral = QueryText.object(ObjectDetail.newBuilder().setObjectId(7).setOwnerPlayerIndex(-1).setUnderConstruction(true).setConstructionPercent(40).setContainedById(9)
            .addContainedIds(11).addContainedIds(12).build(), index -> "x");
        assertTrue(neutral.contains("neutral or unknown") && neutral.contains("40 percent") && neutral.contains("Inside:") && neutral.contains("11, 12"), neutral);
    }

    @Test
    void aPlayerAndAMapAndAFeedReadAsText() {
        String player = QueryText.player(PlayerDetail.newBuilder().setPlayerIndex(2).setName("Alice").setSide("America").setMoney(1500).setRankLevel(3).setRankPoints(40).setRankPointsNext(100)
            .setSciencePoints(2).addSciences("SCIENCE_Paladin").addUpgrades("Upgrade_AmericaRadar").addBuildable("AmericaBarracks")
            .addPowers(PowerStatus.newBuilder().setPowerName("SuperweaponParticleUplinkCannon").setPercentReady(0.5f)).build());
        assertTrue(player.contains("Alice  (index 2, America)") && player.contains("1500") && player.contains("40 of 100") && player.contains("SCIENCE_Paladin"), player);
        assertTrue(player.contains("50 percent") && player.contains("AmericaBarracks"), player);

        String map = QueryText.map(MapDetail.newBuilder().setMapName("maps\\ridge").setHasExtent(true).setMaxX(4000).setMaxY(3000)
            .addStartPositions(StartPositionInfo.newBuilder().setStartPosition(0).setX(10).setY(20)).setMinimap(com.generalsonline.stats.proto.Minimap.newBuilder().setWidth(64).setHeight(64)).build());
        assertTrue(map.contains("0, 0 to 4000, 3000") && map.contains("1 at 10, 20") && map.contains("64 by 64 pixels") && map.contains("Waypoints:"), map);
        assertTrue(QueryText.map(MapDetail.getDefaultInstance()).contains("not requested"));

        String feed = QueryText.feed(FeedConfig.newBuilder().setLightIntervalMs(100).setFullIntervalMs(250).setGridResolution(32).setInterest(true).setInterestIntervalMs(500).setEvents(true).build());
        assertTrue(feed.contains("light 100 ms") && feed.contains("interest on, every 500 ms") && feed.contains("events on, camera off"), feed);
    }

    // -- Paging.

    private static CatalogResult page(int rows, boolean more, int next, int total) {
        CatalogResult.Builder b = CatalogResult.newBuilder();
        for (int i = 0; i < rows; i++) {
            b.addEntries(CatalogEntry.newBuilder().setName("T" + i));
        }
        return b.setPage(PageInfo.newBuilder().setHasMore(more).setNextCursor(next).setTotal(total)).build();
    }

    @Test
    void theCatalogPagerWalksForwardAndBackWithTheCursorsItSaw() {
        CatalogPager pager = new CatalogPager();
        assertEquals("no search yet", pager.text());
        assertFalse(pager.hasNext() || pager.hasPrevious());

        CatalogQuery first = pager.start(CatalogQuery.newBuilder().setSide("GLA").setPage(com.generalsonline.stats.proto.PageRequest.newBuilder().setCursor(9)).build(), 10);
        assertEquals(0, first.getPage().getCursor(), "a search starts at the beginning whatever the filter carried");
        assertEquals(10, first.getPage().getPageSize());
        assertEquals("GLA", first.getSide());
        pager.accept(page(10, true, 10, 25));
        assertTrue(pager.hasNext());
        assertFalse(pager.hasPrevious());
        assertEquals("page 1: rows 1 to 10 of 25", pager.text());

        CatalogQuery second = pager.next();
        assertEquals(10, second.getPage().getCursor());
        pager.accept(page(10, true, 20, 25));
        CatalogQuery third = pager.next();
        assertEquals(20, third.getPage().getCursor());
        pager.accept(page(5, false, 0, 25));
        assertFalse(pager.hasNext());
        assertEquals("page 3: rows 21 to 25 of 25", pager.text());

        assertEquals(10, pager.previous().getPage().getCursor());
        pager.accept(page(10, true, 20, 25));
        assertEquals(0, pager.previous().getPage().getCursor());
        assertFalse(pager.hasPrevious());
        assertEquals(10, pager.next().getPage().getCursor(), "the way forward again uses the remembered cursor");
        assertThrows(IllegalStateException.class, () -> new CatalogPager().next());
        assertThrows(IllegalStateException.class, () -> new CatalogPager().previous());

        pager.start(CatalogQuery.getDefaultInstance(), 0);
        pager.accept(page(0, false, 0, 0));
        assertEquals("no templates match", pager.text());
    }

    // -- View statistics.

    private static MatchState stateWithTwoPlayers() {
        MatchState state = new MatchState();
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m").setLogicFramesPerSecond(30)
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("Alice").setColor(0xFFFF0000).setIsHuman(true))
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(3).setName("Bob").setColor(0xFF0000FF).setIsHuman(true)).build());
        return state;
    }

    @Test
    void theViewStatsRowsCarryTheSharesNotableUnitsAndSuperweaponsOfTheLatestFrame() {
        MatchState state = stateWithTwoPlayers();
        ViewStatsModel rows = new ViewStatsModel();
        assertFalse(rows.update(state), "no frame yet");
        assertTrue(rows.header().startsWith("No ViewStats frame yet"));

        ViewStats view = ViewStats.newBuilder().setFrame(900).setHasView(true).setTradeWindowMs(15000).setIntensityWindowMs(5000)
            .addPlayers(PlayerViewStats.newBuilder().setPlayerIndex(2).setValueInView(3000).setEffectiveValue(2500).setValueKilled(800).setDamagePerSecond(150f)
                .addNotable(NotableUnit.newBuilder().setObjectId(77).setTemplateName("ChinaTankOverlord").setHealth(0.5f).setVeterancy(2).setCost(2000).setAddon("OverlordGattlingCannon"))
                .addSuperweapons(ViewSuperweapon.newBuilder().setObjectId(300).setTemplateName("ChinaNuclearMissileLauncher").setPowerName("SuperweaponNeutronMissile")
                    .setReadyFrame(1500).setPercentReady(0.6f).setState(SuperweaponState.SUPERWEAPON_CHARGING)))
            .addPlayers(PlayerViewStats.newBuilder().setPlayerIndex(3).setValueInView(1000).setEffectiveValue(1000).setDamagePerSecond(0f)).build();
        state.onViewStats(view);
        assertTrue(rows.update(state));
        assertFalse(rows.update(state), "the same frame again changes nothing");

        assertEquals(2, rows.players.getRowCount());
        assertEquals(0.75, rows.players.get(0).share(), 1e-9);
        assertEquals(0.25, rows.players.get(1).share(), 1e-9);
        assertEquals(4000, rows.totalValue());
        assertEquals("Alice", rows.players.get(0).name());
        assertEquals(1, rows.notable.getRowCount());
        assertEquals("Alice", rows.notable.get(0).player());
        assertEquals(50, rows.notable.get(0).healthPercent());
        assertEquals("Elite", rows.notable.get(0).veterancy());
        assertEquals(77, rows.notableRow(0).objectId());
        assertEquals(1, rows.superweapons.getRowCount());
        assertEquals("60 %", rows.superweapons.valueAt(0, "Charged"));
        assertEquals("0:20", rows.superweapons.valueAt(0, "Ready in"), "600 frames at 30 per second");
        assertTrue(rows.header().contains("Frame 900") && rows.header().contains("camera view is known") && rows.header().contains("15.0 s"), rows.header());
        assertTrue(rows.intensity().startsWith("a skirmish") && rows.intensity().contains("150"), rows.intensity());

        state.onViewStats(ViewStats.newBuilder().setFrame(930).setHasView(false).addPlayers(PlayerViewStats.newBuilder().setPlayerIndex(2)).build());
        assertTrue(rows.update(state));
        assertEquals(0, rows.players.get(0).share(), "no value in view is no share, not a division by zero");
        assertEquals(0, rows.notable.getRowCount());
        assertTrue(rows.header().contains("no camera view yet"));
        assertTrue(rows.intensity().startsWith("quiet"));
    }

    @Test
    void theFightIntensityWordsFollowTheThresholds() {
        assertEquals("quiet", ViewStatsModel.intensityWord(0));
        assertEquals("light fighting", ViewStatsModel.intensityWord(ViewStatsModel.LIGHT_DPS));
        assertEquals("a skirmish", ViewStatsModel.intensityWord(ViewStatsModel.SKIRMISH_DPS));
        assertEquals("heavy fighting", ViewStatsModel.intensityWord(ViewStatsModel.HEAVY_DPS));
    }

    // -- The interest overlay.

    @Test
    void interestColoursGetHotterAndMoreOpaqueWithTheScore() {
        assertEquals(0, Minimap.interestColor(0, 100).getAlpha());
        assertEquals(0, Minimap.interestColor(5, 0).getAlpha());
        Color cool = Minimap.interestColor(5, 1000);
        Color hot = Minimap.interestColor(1000, 1000);
        assertTrue(hot.getAlpha() > cool.getAlpha());
        assertTrue(hot.getGreen() < cool.getGreen(), "yellow towards red");
        assertEquals(255, hot.getRed());
        assertEquals(Minimap.interestColor(2000, 1000).getRGB(), hot.getRGB(), "a score above the maximum does not overflow");
    }

    private static InterestHighlight highlight(int id, HighlightKind kind, int impact, int expire, boolean estimated) {
        return InterestHighlight.newBuilder().setId(id).setKind(kind).setPlayerIndex(3).setPowerName("SuperweaponNeutronMissile").setTemplateName("AmericaBarracks").setValue(1500)
            .setStartFrame(100).setImpactFrame(impact).setImpactEstimated(estimated).setExpireFrame(expire).build();
    }

    @Test
    void pendingHighlightsAreTheUnexpiredOnesSoonestFirstAndReadAsText() {
        InterestMap map = InterestMap.newBuilder()
            .addHighlights(highlight(1, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 900, 1000, true))
            .addHighlights(highlight(2, HighlightKind.HIGHLIGHT_STRUCTURE_LOST, 0, 500, false))
            .addHighlights(highlight(3, HighlightKind.HIGHLIGHT_HERO_DEATH, 0, 150, false))
            .addHighlights(highlight(4, HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, 600, 700, false)).build();
        List<InterestHighlight> pending = Minimap.pendingHighlights(map, 200);
        assertEquals(List.of(2, 4, 1), pending.stream().map(h -> h.getId()).toList(), "the one that expired at 150 is gone; no impact sorts by its start frame");

        assertEquals("Superweapon launch by Bob (NeutronMissile) impact in 13.3 s (estimated)", Minimap.highlightText(pending.get(2), 500, 30, i -> "Bob"));
        assertEquals("Structure lost by Bob (Barracks, worth 1500)", Minimap.highlightText(pending.get(0), 500, 30, i -> "Bob").replace("AmericaBarracks", "Barracks"));
        assertEquals(" impact now", Minimap.impactText(pending.get(1), 700, 30));
        assertEquals("", Minimap.impactText(pending.get(0), 500, 30), "unknown impact time");
    }

    @Test
    void theMinimapImageIsFlippedSoThatNorthIsUp() {
        // two rows: row 0 is the southern edge (red), row 1 the northern (green)
        byte[] rgba = {(byte) 255, 0, 0, (byte) 255, (byte) 255, 0, 0, (byte) 255, 0, (byte) 255, 0, (byte) 255, 0, (byte) 255, 0, (byte) 255};
        BufferedImage image = MapView.image(com.generalsonline.stats.proto.Minimap.newBuilder().setWidth(2).setHeight(2).setRgba(ByteString.copyFrom(rgba)).build());
        assertEquals(0xFF00FF00, image.getRGB(0, 0), "the northern row is drawn on top");
        assertEquals(0xFFFF0000, image.getRGB(0, 1));
        assertNotNull(MapView.image(com.generalsonline.stats.proto.Minimap.newBuilder().setWidth(2).setHeight(2).setRgba(ByteString.copyFrom(new byte[3])).build()), "a short image does not throw");
    }

    // -- The serve command.

    private static CommandLine parse(String... args) {
        return CommandLine.parse(args);
    }

    @Test
    void theServeCommandTakesADumpAndItsOptions() {
        CommandLine line = parse("serve", "--pid", "777", "--speed", "2.5", "--loop", "--no-wait", "--situation", "observer", "match.dump");
        assertEquals(List.of("match.dump"), line.positional());
        assertEquals(777, line.pid());
        assertEquals(2.5, line.speed(1.0));
        assertTrue(line.flag("loop") && line.flag("no-wait"));
        assertEquals("observer", line.value("situation"));
        assertEquals(1.0, parse("serve", "a.dump").speed(1.0), "real time unless told otherwise");
        assertEquals(0.0, parse("serve", "--speed", "0", "a.dump").speed(1.0), "0 is as fast as possible");
        assertEquals("\\\\.\\pipe\\mine", parse("serve", "--pipe", "\\\\.\\pipe\\mine", "a.dump").value("pipe"));
        assertThrows(CommandLine.UsageException.class, () -> parse("serve"));
        assertThrows(CommandLine.UsageException.class, () -> parse("serve", "a", "b"));
        assertThrows(CommandLine.UsageException.class, () -> parse("serve", "--bogus", "a"));
        assertTrue(CommandLine.usage().contains("serve [--pid N | --pipe NAME]"));
        assertEquals("a.dump", parse("gui", "--fake", "a.dump", "--speed", "2").value("fake"));
    }

    @Test
    void serveRefusesWhatCannotWorkBeforeStartingAnything() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        PrintStream print = new PrintStream(out, true, StandardCharsets.UTF_8);
        Path dump = FakeDumpFixture.write(dir);
        assertTrue(assertThrows(CommandLine.UsageException.class, () -> Main.startFake(parse("serve", "--pid", "5", "--pipe", "x", dump.toString()), print)).getMessage().contains("alternatives"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> Main.startFake(parse("serve", dir.resolve("missing.dump").toString()), print)).getMessage().contains("not a file"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> Main.startFake(parse("serve", "--situation", "spectator", dump.toString()), print)).getMessage().contains("replay, observer, player"));
        assertEquals("", out.toString(StandardCharsets.UTF_8), "nothing was announced");
    }

    @Test
    void serveStartsAFakeGameOnARealPipeWhereWindowsHasOne() throws Exception {
        org.junit.jupiter.api.Assumptions.assumeTrue(System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT).contains("windows"));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int pid = 61000 + (int) (System.nanoTime() % 3000);
        try (FakeGame game = Main.startFake(parse("serve", "--pid", Integer.toString(pid), "--speed", "0", "--loop", FakeDumpFixture.write(dir).toString()), new PrintStream(out, true, StandardCharsets.UTF_8))) {
            String text = out.toString(StandardCharsets.UTF_8);
            assertTrue(text.contains("serving fixture.dump on \\\\.\\pipe\\generalsonline-stats-" + pid) && text.contains("as fast as possible") && text.contains("looping"), text);
            assertTrue(text.contains("waiting for the first reader"), text);
            assertEquals(pid, game.pid());
            assertTrue(com.generalsonline.stats.PipeDiscovery.findPids().contains(pid));
        }
    }
}
