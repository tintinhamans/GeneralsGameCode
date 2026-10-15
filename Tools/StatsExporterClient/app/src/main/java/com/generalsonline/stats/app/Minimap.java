package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.WorldExtent;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.SupplySource;
import com.generalsonline.stats.proto.TechStructure;

import javax.swing.JPanel;
import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.geom.Path2D;
import java.awt.geom.Rectangle2D;
import java.util.ArrayList;
import java.util.List;
import java.util.function.IntFunction;

/**
 * The Minimap tab: one panel that draws what {@link MatchState} holds, with the map's real aspect ratio.
 *
 * <p>Reading order of the drawing code, back to front: each player's grid cells (translucent squares in the player's
 * color), supply sources, tech structures, start positions, recent event marks, the camera view box, the legend.
 * World coordinates have y pointing north; the screen has y pointing down, so {@link View#sy} flips it.
 * The window repaints this panel from a timer (about 30 times a second), never per frame.
 */
@SuppressWarnings("serial")
final class Minimap extends JPanel {
    /** Event marks fade out over this many game seconds. */
    static final int MARK_SECONDS = 8;

    private static final int MARGIN = 12;
    private static final Color[] FALLBACK_COLORS = {
        new Color(0xE53935), new Color(0x1E88E5), new Color(0x43A047), new Color(0xFDD835),
        new Color(0x8E24AA), new Color(0x00ACC1), new Color(0xFB8C00), new Color(0xEC407A)};

    private final GuiModel model;
    private boolean showInterest;

    /** Event kinds that leave a mark on the minimap. */
    private static final List<StatEventKind> MARKED = List.of(StatEventKind.STATEVENT_KILL, StatEventKind.STATEVENT_CAPTURE, StatEventKind.STATEVENT_BEACON);

    /** At most this many marks are drawn, the newest ones. */
    static final int MAX_MARKS = 256;

    /** One remembered event location. */
    record Mark(long frame, float x, float y, StatEventKind kind, int playerIndex) {
    }

    /** The marks among these events (oldest first) that are not older than maxAgeFrames at currentFrame; at most {@link #MAX_MARKS}, the newest. */
    static List<Mark> marks(List<StatEvent> events, long currentFrame, long maxAgeFrames) {
        List<Mark> marks = new java.util.ArrayList<>();
        for (StatEvent e : events) {
            if (e.getHasLocation() && MARKED.contains(e.getKind())) {
                marks.add(new Mark(Format.u(e.getFrame()), e.getX(), e.getY(), e.getKind(), e.getPlayerIndex()));
            }
        }
        if (marks.size() > MAX_MARKS) {
            marks = new java.util.ArrayList<>(marks.subList(marks.size() - MAX_MARKS, marks.size()));
        }
        marks.removeIf(mark -> currentFrame - mark.frame() > maxAgeFrames);
        return marks;
    }

    /** 1.0 for a mark made now, falling to 0.0 at maxAgeFrames; never negative. */
    static double fade(long currentFrame, long dotFrame, long maxAgeFrames) {
        if (maxAgeFrames <= 0) {
            return 0;
        }
        return Math.max(0.0, 1.0 - (double) Math.max(0, currentFrame - dotFrame) / maxAgeFrames);
    }

    /** Draws the interest map (where the action is) and the pending highlights under the units. */
    void setShowInterest(boolean show) {
        this.showInterest = show;
        repaint();
    }

    boolean showsInterest() {
        return showInterest;
    }

    /** The highlight colour of a score: transparent for 0, from yellow to red as it approaches the highest score. */
    static Color interestColor(long score, long max) {
        if (score <= 0 || max <= 0) {
            return new Color(0, 0, 0, 0);
        }
        double level = Math.min(1.0, Math.sqrt((double) score / max));
        int green = (int) Math.round(235 - 200 * level);
        return new Color(255, Math.max(0, green), 30, (int) Math.round(45 + 175 * level));
    }

    /** The highlights of an interest map that have not expired at this frame, soonest impact first. */
    static List<InterestHighlight> pendingHighlights(InterestMap map, long frame) {
        List<InterestHighlight> pending = new ArrayList<>();
        for (InterestHighlight h : map.getHighlightsList()) {
            if (Format.u(h.getExpireFrame()) >= frame) {
                pending.add(h);
            }
        }
        pending.sort(java.util.Comparator.comparingLong((InterestHighlight h) -> h.getImpactFrame() != 0 ? Format.u(h.getImpactFrame()) : Format.u(h.getStartFrame())));
        return pending;
    }

    /** One line about a highlight: what it is, whose, and the time to the impact when it is known. */
    static String highlightText(InterestHighlight h, long frame, int fps, IntFunction<String> playerName) {
        String who = playerName.apply(h.getPlayerIndex());
        return switch (h.getKind()) {
            case HIGHLIGHT_SUPERWEAPON_LAUNCH -> "Superweapon launch by " + who + " (" + Format.shortPowerName(h.getPowerName()) + ")" + impactText(h, frame, fps);
            case HIGHLIGHT_HERO_DEATH -> "Hero lost by " + who + " (" + Format.shortName(h.getTemplateName()) + ")";
            case HIGHLIGHT_STRUCTURE_LOST -> "Structure lost by " + who + " (" + Format.shortName(h.getTemplateName()) + ", worth " + Format.u(h.getValue()) + ")";
            default -> "Highlight " + h.getId();
        };
    }

    /** " impact in 4.0 s" (estimated ones say so), " impact now" after the impact frame, "" when the game does not know it. */
    static String impactText(InterestHighlight h, long frame, int fps) {
        if (h.getImpactFrame() == 0) {
            return "";
        }
        long left = Format.u(h.getImpactFrame()) - frame;
        String estimate = h.getImpactEstimated() ? " (estimated)" : "";
        return left <= 0 ? " impact now" + estimate : String.format(" impact in %.1f s%s", left / (double) Math.max(1, fps), estimate);
    }

    Minimap(GuiModel model) {
        this.model = model;
        setBackground(new Color(0x1B1F24));
        setPreferredSize(new java.awt.Dimension(640, 480));
    }

    /**
     * The map rectangle on screen and the world-to-screen mapping. The map keeps its aspect ratio: it is scaled to
     * fit the panel and centered, with unused space left empty.
     */
    record View(double left, double top, double width, double height, WorldExtent extent) {
        static View fit(int panelWidth, int panelHeight, int margin, WorldExtent extent) {
            double availableWidth = Math.max(1, panelWidth - 2.0 * margin);
            double availableHeight = Math.max(1, panelHeight - 2.0 * margin);
            double scale = Math.min(availableWidth / extent.width(), availableHeight / extent.height());
            double width = extent.width() * scale;
            double height = extent.height() * scale;
            return new View((panelWidth - width) / 2, (panelHeight - height) / 2, width, height, extent);
        }

        double scale() {
            return width / extent.width();
        }

        double sx(double worldX) {
            return left + (worldX - extent.minX()) * scale();
        }

        /** North (larger world y) is up. */
        double sy(double worldY) {
            return top + (extent.maxY() - worldY) * scale();
        }
    }

    /** The player's color from MatchInfo, or a distinct fallback for the index when no color is known. */
    static Color playerColor(int packedColor, int playerIndex) {
        if (packedColor != 0 && new ColorCell(packedColor, 0, "").isColor()) {
            return new Color(packedColor | 0xFF000000);
        }
        return FALLBACK_COLORS[Math.floorMod(playerIndex, FALLBACK_COLORS.length)];
    }

    private Color colorOf(int playerIndex) {
        return playerColor(model.state().playerColor(playerIndex), playerIndex);
    }

    @Override
    protected void paintComponent(Graphics graphics) {
        super.paintComponent(graphics);
        Graphics2D g = (Graphics2D) graphics.create();
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            MatchState world = model.state();
            WorldExtent extent = world.worldExtent().orElse(null);
            if (extent == null) {
                g.setColor(Color.GRAY);
                String hint = "Waiting for the map extent (sent once the map is loaded).";
                FontMetrics metrics = g.getFontMetrics();
                g.drawString(hint, Math.max(8, (getWidth() - metrics.stringWidth(hint)) / 2), getHeight() / 2);
                return;
            }
            View view = View.fit(getWidth(), getHeight(), MARGIN, extent);
            g.setColor(new Color(0x2A3038));
            g.fill(new Rectangle2D.Double(view.left(), view.top(), view.width(), view.height()));

            drawGrids(g, view, world);
            if (showInterest) {
                drawInterest(g, view, world);
            }
            drawSupply(g, view, world);
            drawTech(g, view, world);
            drawStartPositions(g, view, world);
            drawEventMarks(g, view, world);
            drawCamera(g, view, world.camera().orElse(null));

            g.setColor(new Color(0x59626E));
            g.setStroke(new BasicStroke(1f));
            g.draw(new Rectangle2D.Double(view.left(), view.top(), view.width(), view.height()));
            drawLegend(g, world);
        } finally {
            g.dispose();
        }
    }

    /** Each cell with units or structures becomes a square; more objects make it more opaque. */
    private void drawGrids(Graphics2D g, View view, MatchState world) {
        for (PlayerGrid grid : world.grids()) {
            Color color = colorOf(grid.getPlayerIndex());
            int width = (int) Format.u(grid.getWidth());
            double size = view.scale();
            for (int i = 0; i < grid.getCellsCount() && width > 0; i++) {
                int count = grid.getCells(i);
                if (count <= 0) {
                    continue;
                }
                double worldX = view.extent().minX() + (i % width) * grid.getCellWidth();
                double worldY = view.extent().minY() + (i / width) * grid.getCellHeight();
                g.setColor(withAlpha(color, Math.min(210, 70 + count * 25)));
                // The cell's top edge is its far (larger y) side, which is what sy() maps highest.
                g.fill(new Rectangle2D.Double(view.sx(worldX), view.sy(worldY + grid.getCellHeight()), grid.getCellWidth() * size, grid.getCellHeight() * size));
            }
        }
    }

    /** The cells of the interest map as a heat map, then a marker for each pending highlight with its countdown. */
    private void drawInterest(Graphics2D g, View view, MatchState world) {
        InterestMap map = world.interestMap().orElse(null);
        if (map == null || map.getWidth() == 0) {
            return;
        }
        long max = 100;
        for (int score : map.getScoresList()) {
            max = Math.max(max, Integer.toUnsignedLong(score));
        }
        int width = (int) Format.u(map.getWidth());
        double size = view.scale();
        for (int i = 0; i < map.getScoresCount(); i++) {
            long score = Integer.toUnsignedLong(map.getScores(i));
            if (score == 0) {
                continue;
            }
            double worldX = map.getOriginX() + (i % width) * map.getCellWidth();
            double worldY = map.getOriginY() + (i / width) * map.getCellHeight();
            g.setColor(interestColor(score, max));
            g.fill(new Rectangle2D.Double(view.sx(worldX), view.sy(worldY + map.getCellHeight()), map.getCellWidth() * size, map.getCellHeight() * size));
        }
        long now = model.currentFrame();
        int fps = model.logicFps();
        g.setFont(g.getFont().deriveFont(java.awt.Font.BOLD, 11f));
        for (InterestHighlight h : pendingHighlights(map, now)) {
            double x = view.sx(h.getX());
            double y = view.sy(h.getY());
            g.setStroke(new BasicStroke(2f));
            if (h.getKind() == HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH) {
                g.setColor(new Color(0xFF1744));
                g.draw(new java.awt.geom.Ellipse2D.Double(x - 14, y - 14, 28, 28));
                g.draw(new java.awt.geom.Ellipse2D.Double(x - 7, y - 7, 14, 14));
                String text = impactText(h, now, fps).trim();
                g.setColor(Color.WHITE);
                g.drawString(text.isEmpty() ? "launch" : text.replace("impact ", ""), (float) x + 16, (float) y + 4);
            } else if (h.getKind() == HighlightKind.HIGHLIGHT_HERO_DEATH) {
                g.setColor(Color.WHITE);
                g.draw(new java.awt.geom.Line2D.Double(x - 8, y - 8, x + 8, y + 8));
                g.draw(new java.awt.geom.Line2D.Double(x - 8, y + 8, x + 8, y - 8));
            } else {
                g.setColor(new Color(0xFF9100));
                g.draw(new Rectangle2D.Double(x - 8, y - 8, 16, 16));
            }
        }
    }

    /** Supply sources as gold circles, bigger when more cash is left in them. */
    private void drawSupply(Graphics2D g, View view, MatchState world) {
        long most = 1;
        for (SupplySource source : world.supply()) {
            most = Math.max(most, Format.u(source.getRemainingValue()));
        }
        for (SupplySource source : world.supply()) {
            double radius = 2 + 5 * Math.sqrt(Format.u(source.getRemainingValue()) / (double) most);
            g.setColor(new Color(0xF2C94C));
            g.fill(new java.awt.geom.Ellipse2D.Double(view.sx(source.getX()) - radius, view.sy(source.getY()) - radius, radius * 2, radius * 2));
        }
    }

    /** Tech structures as diamonds in the owner's color, gray while neutral. */
    private void drawTech(Graphics2D g, View view, MatchState world) {
        for (TechStructure tech : world.tech()) {
            double x = view.sx(tech.getX());
            double y = view.sy(tech.getY());
            Path2D diamond = new Path2D.Double();
            diamond.moveTo(x, y - 5);
            diamond.lineTo(x + 5, y);
            diamond.lineTo(x, y + 5);
            diamond.lineTo(x - 5, y);
            diamond.closePath();
            g.setColor(tech.getOwnerPlayerIndex() < 0 ? new Color(0x9AA3AD) : colorOf(tech.getOwnerPlayerIndex()));
            g.fill(diamond);
            g.setColor(Color.BLACK);
            g.setStroke(new BasicStroke(1f));
            g.draw(diamond);
        }
    }

    /** A ring with the lobby slot number where each player starts. */
    private void drawStartPositions(Graphics2D g, View view, MatchState world) {
        for (MatchPlayer player : world.matchPlayers()) {
            if (!player.getHasStartLocation()) {
                continue;
            }
            double x = view.sx(player.getStartX());
            double y = view.sy(player.getStartY());
            g.setColor(playerColor(player.getColor(), player.getPlayerIndex()));
            g.setStroke(new BasicStroke(2f));
            g.draw(new java.awt.geom.Ellipse2D.Double(x - 8, y - 8, 16, 16));
            g.setFont(g.getFont().deriveFont(java.awt.Font.BOLD, 10f));
            g.drawString(Integer.toString(player.getLobbySlot() + 1), (float) x - 3, (float) y + 4);
        }
    }

    /** Kills (red), captures (yellow) and beacons (cyan) as dots that fade out with game time. */
    private void drawEventMarks(Graphics2D g, View view, MatchState world) {
        long now = model.currentFrame();
        long maxAge = (long) MARK_SECONDS * model.logicFps();
        for (Mark dot : marks(world.events(), now, maxAge)) {
            double fade = fade(now, dot.frame(), maxAge);
            Color base = dot.kind() == StatEventKind.STATEVENT_KILL ? new Color(0xFF5252)
                : dot.kind() == StatEventKind.STATEVENT_CAPTURE ? new Color(0xFFEB3B) : new Color(0x18FFFF);
            g.setColor(withAlpha(base, (int) (40 + 215 * fade)));
            double radius = 3 + 3 * fade;
            g.fill(new java.awt.geom.Ellipse2D.Double(view.sx(dot.x()) - radius, view.sy(dot.y()) - radius, radius * 2, radius * 2));
        }
    }

    /** The camera's ground box as a polygon through all four corners; a cross when the frame has no box. */
    private void drawCamera(Graphics2D g, View view, Camera camera) {
        if (camera == null) {
            return;
        }
        g.setColor(Color.WHITE);
        g.setStroke(new BasicStroke(2f));
        if (camera.getHasViewBox()) {
            g.draw(cameraPolygon(view, camera));
        } else {
            double x = view.sx(camera.getX());
            double y = view.sy(camera.getY());
            g.draw(new java.awt.geom.Line2D.Double(x - 6, y, x + 6, y));
            g.draw(new java.awt.geom.Line2D.Double(x, y - 6, x, y + 6));
        }
    }

    /** The four corners in the order the protocol sends them: top left, top right, bottom right, bottom left. */
    static Path2D cameraPolygon(View view, Camera c) {
        Path2D polygon = new Path2D.Double();
        polygon.moveTo(view.sx(c.getViewTlX()), view.sy(c.getViewTlY()));
        polygon.lineTo(view.sx(c.getViewTrX()), view.sy(c.getViewTrY()));
        polygon.lineTo(view.sx(c.getViewBrX()), view.sy(c.getViewBrY()));
        polygon.lineTo(view.sx(c.getViewBlX()), view.sy(c.getViewBlY()));
        polygon.closePath();
        return polygon;
    }

    private void drawLegend(Graphics2D g, MatchState world) {
        List<MatchPlayer> players = world.matchPlayers();
        int y = 18;
        g.setFont(g.getFont().deriveFont(java.awt.Font.PLAIN, 12f));
        for (MatchPlayer player : players) {
            if (player.getIsObserver()) {
                continue;
            }
            g.setColor(playerColor(player.getColor(), player.getPlayerIndex()));
            g.fillRect(8, y - 10, 10, 10);
            g.setColor(Color.LIGHT_GRAY);
            g.drawString(player.getName(), 24, y);
            y += 16;
        }
    }

    private static Color withAlpha(Color color, int alpha) {
        return new Color(color.getRed(), color.getGreen(), color.getBlue(), Math.max(0, Math.min(255, alpha)));
    }
}
