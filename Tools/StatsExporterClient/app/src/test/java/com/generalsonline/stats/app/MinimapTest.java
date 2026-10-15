package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SupplySource;
import com.generalsonline.stats.proto.TechStructure;
import org.junit.jupiter.api.Test;

import java.awt.Graphics2D;
import java.awt.geom.Path2D;
import java.awt.geom.PathIterator;
import java.awt.image.BufferedImage;
import java.util.List;

/** The minimap data model and the drawing, rendered offscreen. */
class MinimapTest {
    private static MatchInfo.Builder started() {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m")
            .setHasWorldExtent(true).setWorldMinX(0).setWorldMinY(0).setWorldMaxX(1000).setWorldMaxY(500)
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(2).setName("Red").setColor(0xFFFF0000).setIsHuman(true)
                .setHasStartLocation(true).setStartX(100).setStartY(100))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(3).setName("Blue").setColor(0xFF0000FF).setIsHuman(true));
    }

    private static PlayerGrid grid(int player, int... countsAtIndex) {
        PlayerGrid.Builder g = PlayerGrid.newBuilder().setPlayerIndex(player).setWidth(32).setHeight(32).setCellWidth(1000f / 32).setCellHeight(500f / 32);
        int[] cells = new int[32 * 32];
        for (int i = 0; i + 1 < countsAtIndex.length; i += 2) {
            cells[countsAtIndex[i]] = countsAtIndex[i + 1];
        }
        for (int c : cells) {
            g.addCells(c);
        }
        return g.build();
    }

    private static StatsSnapshot full(PlayerGrid... grids) {
        StatsSnapshot.Builder s = StatsSnapshot.newBuilder().setFrame(300).setFull(true);
        for (PlayerGrid g : grids) {
            s.addGrids(g);
        }
        return s.build();
    }

    // -- What the minimap reads: the library state, and the marks derived from its events.

    @Test
    void theMinimapReadsMapDataFromTheStateAndLightSnapshotsKeepTheHeavyLists() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started().setHasWorldExtent(false).build());
        assertTrue(model.state().worldExtent().isEmpty());

        model.onMatchInfo(started().build());
        assertEquals(1000f, model.state().worldExtent().orElseThrow().width());
        assertEquals(500f, model.state().worldExtent().orElseThrow().height());
        assertTrue(model.state().matchPlayers().get(0).getHasStartLocation());
        assertEquals(100f, model.state().matchPlayers().get(0).getStartX());

        model.onStatsSnapshot(full(grid(2, 0, 3)).toBuilder()
            .addSupply(SupplySource.newBuilder().setX(10).setY(20).setRemainingValue(2000))
            .addTech(TechStructure.newBuilder().setX(5).setY(6).setOwnerPlayerIndex(-1)).build());
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(310).setFull(false).build());
        assertEquals(1, model.state().grids().size(), "a light snapshot has no grids; keep the last full ones");
        assertEquals(1, model.state().supply().size());
        assertEquals(1, model.state().tech().size());
    }

    @Test
    void killCaptureAndBeaconLocationsAreMarkedAndFade() {
        List<StatEvent> events = List.of(
            StatEvent.newBuilder().setFrame(100).setKind(StatEventKind.STATEVENT_KILL).setHasLocation(true).setX(1).setY(2).build(),
            StatEvent.newBuilder().setFrame(100).setKind(StatEventKind.STATEVENT_CAPTURE).setHasLocation(true).setX(3).setY(4).build(),
            StatEvent.newBuilder().setFrame(100).setKind(StatEventKind.STATEVENT_BEACON).setHasLocation(true).setX(5).setY(6).build(),
            StatEvent.newBuilder().setFrame(100).setKind(StatEventKind.STATEVENT_BUILD).setHasLocation(true).setX(7).setY(8).build(),
            StatEvent.newBuilder().setFrame(100).setKind(StatEventKind.STATEVENT_KILL).setX(9).setY(9).build());
        assertEquals(3, Minimap.marks(events, 100, 480).size(), "builds and events without a location leave no mark");
        assertEquals(3, Minimap.marks(events, 580, 480).size());
        assertEquals(0, Minimap.marks(events, 581, 480).size());

        assertEquals(1.0, Minimap.fade(100, 100, 50), 1e-9);
        assertEquals(0.5, Minimap.fade(125, 100, 50), 1e-9);
        assertEquals(0.0, Minimap.fade(200, 100, 50), 1e-9);
        assertEquals(1.0, Minimap.fade(90, 100, 50), 1e-9, "an event slightly ahead of the snapshot is fully visible");
    }

    @Test
    void theMarkListIsCappedToTheNewest() {
        List<StatEvent> events = new java.util.ArrayList<>();
        for (int i = 0; i < 400; i++) {
            events.add(StatEvent.newBuilder().setFrame(1).setKind(StatEventKind.STATEVENT_KILL).setHasLocation(true).setX(i).build());
        }
        List<Minimap.Mark> marks = Minimap.marks(events, 1, 10);
        assertEquals(Minimap.MAX_MARKS, marks.size());
        assertEquals(144f, marks.get(0).x());
    }

    // -- The drawing.

    @Test
    void theMapKeepsItsAspectRatioAndIsCentered() {
        WorldExtent wide = new WorldExtent(0, 0, 1000, 500);
        Minimap.View view = Minimap.View.fit(1000, 1000, 0, wide);
        assertEquals(1000.0, view.width(), 1e-9);
        assertEquals(500.0, view.height(), 1e-9);
        assertEquals(250.0, view.top(), 1e-9);
        assertEquals(0.0, view.left(), 1e-9);

        Minimap.View tall = Minimap.View.fit(600, 300, 0, new WorldExtent(0, 0, 100, 100));
        assertEquals(300.0, tall.width(), 1e-9);
        assertEquals(150.0, tall.left(), 1e-9);
    }

    @Test
    void northIsUpAndOriginOffsetsAreHonoured() {
        Minimap.View view = Minimap.View.fit(500, 500, 0, new WorldExtent(-100, 200, 100, 400));
        assertEquals(0.0, view.sx(-100), 1e-9);
        assertEquals(500.0, view.sx(100), 1e-9);
        assertEquals(0.0, view.sy(400), 1e-9, "the largest y is the top edge");
        assertEquals(500.0, view.sy(200), 1e-9);
    }

    @Test
    void theCameraPolygonHasAllFourCorners() {
        Minimap.View view = Minimap.View.fit(1000, 500, 0, new WorldExtent(0, 0, 1000, 500));
        Camera camera = Camera.newBuilder().setHasViewBox(true)
            .setViewTlX(100).setViewTlY(400).setViewTrX(300).setViewTrY(420)
            .setViewBrX(280).setViewBrY(200).setViewBlX(120).setViewBlY(180).build();
        Path2D polygon = Minimap.cameraPolygon(view, camera);

        double[] c = new double[6];
        int moves = 0;
        int lines = 0;
        int closes = 0;
        java.util.List<double[]> points = new java.util.ArrayList<>();
        for (PathIterator it = polygon.getPathIterator(null); !it.isDone(); it.next()) {
            int kind = it.currentSegment(c);
            if (kind == PathIterator.SEG_MOVETO) {
                moves++;
                points.add(new double[] {c[0], c[1]});
            } else if (kind == PathIterator.SEG_LINETO) {
                lines++;
                points.add(new double[] {c[0], c[1]});
            } else {
                closes++;
            }
        }
        assertEquals(1, moves);
        assertEquals(3, lines);
        assertEquals(1, closes);
        assertEquals(100.0, points.get(0)[0], 1e-9);
        assertEquals(100.0, points.get(0)[1], 1e-9, "top left corner: y 400 is 100 pixels from the top");
        assertEquals(300.0, points.get(1)[0], 1e-9);
        assertEquals(120.0, points.get(3)[0], 1e-9);
        assertEquals(320.0, points.get(3)[1], 1e-9);
    }

    private static BufferedImage render(Minimap minimap, int width, int height) {
        minimap.setSize(width, height);
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = image.createGraphics();
        minimap.paint(g);
        g.dispose();
        return image;
    }

    @Test
    void aPlayerGridCellIsDrawnAsATranslucentSquareInThePlayersColor() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started().build());
        model.onStatsSnapshot(full(grid(2, 0, 5), grid(3, 1023, 5)));
        Minimap minimap = new Minimap(model);
        BufferedImage image = render(minimap, 840, 440);

        // Map: 1000 x 500 fitted to 840 x 440 with a 12 pixel margin -> scale 0.816, left 12, top 16, bottom 424.
        // Cell 0 is the bottom left corner (min x, min y); cell 1023 is the top right corner.
        int bottomLeft = image.getRGB(12 + 8, 424 - 5);
        int topRight = image.getRGB(828 - 8, 16 + 5);
        int mapBackground = image.getRGB(12 + 300, 16 + 200);

        assertTrue(((bottomLeft >> 16) & 0xFF) > 120 && (bottomLeft & 0xFF) < 100, "red cell: " + Integer.toHexString(bottomLeft));
        assertTrue((topRight & 0xFF) > 120 && ((topRight >> 16) & 0xFF) < 100, "blue cell: " + Integer.toHexString(topRight));
        assertNotEquals(bottomLeft, mapBackground);
        assertEquals(0xFF2A3038, mapBackground, "empty map area is the map color");
        assertEquals(0xFF1B1F24, image.getRGB(2, 436), "outside the map is the panel color");
    }

    @Test
    void cameraBoxStartPositionsAndMarksAreDrawn() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(started().build());
        model.onStatsSnapshot(full());
        model.onCamera(Camera.newBuilder().setHasViewBox(true).setViewTlX(400).setViewTlY(300).setViewTrX(600).setViewTrY(300)
            .setViewBrX(600).setViewBrY(200).setViewBlX(400).setViewBlY(200).build(), 1);
        model.onEvents(Events.newBuilder().setFrame(300)
            .addEvents(StatEvent.newBuilder().setFrame(300).setKind(StatEventKind.STATEVENT_KILL).setHasLocation(true).setX(800).setY(100)).build());
        BufferedImage image = render(new Minimap(model), 840, 440);

        // Top edge of the camera box: world y 300 -> screen y 16 + (500 - 300) * 0.816 = 179.2; x 500 -> 12 + 408 = 420.
        boolean white = false;
        for (int dy = -2; dy <= 2; dy++) {
            int rgb = image.getRGB(420, 179 + dy);
            white |= ((rgb >> 16) & 0xFF) > 200 && ((rgb >> 8) & 0xFF) > 200 && (rgb & 0xFF) > 200;
        }
        assertTrue(white, "the camera box outline passes through (420, 179)");

        // Kill mark at world (800, 100): screen x 12 + 652.8 = 664.8, y 16 + 400 * 0.816 = 342.4.
        int mark = image.getRGB(665, 342);
        assertTrue(((mark >> 16) & 0xFF) > 200 && ((mark >> 8) & 0xFF) < 140, "kill mark is red: " + Integer.toHexString(mark));

        // Start position ring of player 2 at world (100, 100): screen x 93.6, y 342.4 -> ring radius 8 passes 8 pixels left of the center.
        boolean ring = false;
        for (int dx = -2; dx <= 2; dx++) {
            int rgb = image.getRGB(94 - 8 + dx, 342);
            ring |= ((rgb >> 16) & 0xFF) > 180 && (rgb & 0xFF) < 100;
        }
        assertTrue(ring, "start position ring in the player's color");
    }

    @Test
    void withoutAMapExtentThePanelSaysWhy() {
        GuiModel model = new GuiModel();
        Minimap minimap = new Minimap(model);
        BufferedImage image = render(minimap, 400, 200);
        boolean anyText = false;
        for (int x = 0; x < 400 && !anyText; x++) {
            for (int y = 90; y < 110; y++) {
                anyText |= image.getRGB(x, y) != 0xFF1B1F24;
            }
        }
        assertTrue(anyText);
        assertNull(model.state().worldExtent().orElse(null));
    }

    @Test
    void playersWithoutAColorGetDistinctFallbackColors() {
        assertNotEquals(Minimap.playerColor(0, 0), Minimap.playerColor(0, 1));
        assertEquals(Minimap.playerColor(0, 0), Minimap.playerColor(0, 8));
        assertEquals(0xFF112233, Minimap.playerColor(0x00112233, 0).getRGB());
    }
}
