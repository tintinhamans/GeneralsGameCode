package com.generalsonline.stats;

import com.generalsonline.stats.proto.CategoryCounts;
import com.generalsonline.stats.proto.CategoryValue;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.NotableUnit;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.PlayerViewStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.generalsonline.stats.proto.ViewStats;
import com.generalsonline.stats.proto.ViewSuperweapon;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Stand-ins for the frames a dump may not hold: an {@code InterestMap} and {@code ViewStats} computed from the frames the
 * fake game has sent. They follow the documented formulas where the dump has the data (decayed kill value per cell, armies
 * close together, highlights from superweapon use, hero deaths and big structure losses) and approximate the rest (the
 * view statistics come from the player grids and the objects the events named, not from real objects). Good enough to
 * develop a tool against, not a measurement.
 */
final class FakeSynth {
    private static final int HALF_LIFE_MS = 6000;

    private record Hit(long frame, float x, float y, double value, int player, boolean inView) {
    }

    private final int fps;
    private final List<Hit> hits = new ArrayList<>();
    private final List<InterestHighlight> highlights = new ArrayList<>();
    private final Set<String> heroTemplates = new HashSet<>();
    private StatsSnapshot lastFull;
    private float[] quad;
    private int nextHighlightId = 1;
    private boolean newHighlight;

    FakeSynth(int fps) {
        this.fps = fps;
    }

    synchronized void reset() {
        hits.clear();
        highlights.clear();
        lastFull = null;
        nextHighlightId = 1;
    }

    /** The camera's ground corners {x0,y0,...,x3,y3}, or null; events are counted as in view against the latest. */
    synchronized void setView(float[] corners) {
        this.quad = corners;
    }

    synchronized void onSnapshot(StatsSnapshot s) {
        if (s.getFull()) {
            lastFull = s;
            for (PlayerStats p : s.getPlayersList()) {
                for (var hero : p.getHeroesList()) {
                    heroTemplates.add(hero.getTemplateName());
                }
            }
        }
    }

    synchronized void onEvents(Events events) {
        hits.removeIf(h -> Format.u(events.getFrame()) - h.frame() > 70L * fps);
        for (StatEvent e : events.getEventsList()) {
            long frame = Format.u(e.getFrame());
            boolean located = e.getHasLocation();
            switch (e.getKind()) {
                case STATEVENT_KILL -> {
                    if (located) {
                        hits.add(new Hit(frame, e.getX(), e.getY(), Format.u(e.getCost()), e.getPlayerIndex(), inView(e.getX(), e.getY())));
                    }
                }
                case STATEVENT_LOSS -> {
                    if (located) {
                        hits.add(new Hit(frame, e.getX(), e.getY(), -(double) Format.u(e.getCost()), e.getPlayerIndex(), inView(e.getX(), e.getY())));
                    }
                    if (heroTemplates.contains(e.getPrimaryName())) {
                        highlight(HighlightKind.HIGHLIGHT_HERO_DEATH, e, frame, 0, false, frame + 8L * fps);
                    } else if (e.getCategory().equals("STRUCTURE") && Format.u(e.getCost()) >= 1000) {
                        highlight(HighlightKind.HIGHLIGHT_STRUCTURE_LOST, e, frame, 0, false, frame + 8L * fps);
                    }
                }
                case STATEVENT_ABILITY_USE -> {
                    if (e.getCategory().equals("SUPERWEAPON") && located) {
                        boolean known = e.getPrimaryName().contains("Particle");
                        long impact = frame + (known ? 9L : 14L) * fps;
                        highlight(HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH, e, frame, impact, !known, impact + 5L * fps);
                    }
                }
                default -> {
                    // no other kind adds to the map
                }
            }
        }
    }

    private void highlight(HighlightKind kind, StatEvent e, long frame, long impact, boolean estimated, long expire) {
        InterestHighlight.Builder b = InterestHighlight.newBuilder().setId(nextHighlightId++).setKind(kind).setPlayerIndex(e.getPlayerIndex())
            .setTemplateName(kind == HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH ? e.getSecondaryName() : e.getPrimaryName()).setX(e.getX()).setY(e.getY())
            .setStartFrame((int) frame).setImpactFrame((int) impact).setImpactEstimated(estimated).setExpireFrame((int) expire)
            .setValue((int) Format.u(e.getCost())).setObjectId((int) Format.u(e.getObjectId()));
        if (kind == HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH) {
            b.setPowerName(e.getPrimaryName());
        }
        highlights.add(b.build());
        newHighlight = true;
    }

    /** True once after a highlight was created: the interest map is then sent at once. */
    synchronized boolean takeNewHighlight() {
        boolean was = newHighlight;
        newHighlight = false;
        return was;
    }

    private boolean inView(float x, float y) {
        return quad != null && insideQuad(quad, x, y);
    }

    static boolean insideQuad(float[] q, float x, float y) {
        boolean inside = false;
        for (int i = 0, j = 3; i < 4; j = i++) {
            float xi = q[i * 2];
            float yi = q[i * 2 + 1];
            float xj = q[j * 2];
            float yj = q[j * 2 + 1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
                inside = !inside;
            }
        }
        return inside;
    }

    // -- The interest map.

    synchronized InterestMap interest(MatchState state, int resolution, long frame) {
        WorldExtent extent = state.worldExtent().orElse(null);
        if (extent == null || resolution <= 0) {
            return null;
        }
        double[] score = new double[resolution * resolution];
        double cellW = extent.width() / resolution;
        double cellH = extent.height() / resolution;
        hits.removeIf(h -> frame - h.frame() > 10L * HALF_LIFE_MS * fps / 1000);
        for (Hit h : hits) {
            int cx = (int) Math.floor((h.x() - extent.minX()) / cellW);
            int cy = (int) Math.floor((h.y() - extent.minY()) / cellH);
            if (cx < 0 || cy < 0 || cx >= resolution || cy >= resolution) {
                continue;
            }
            double age = Math.max(0, frame - h.frame()) * 1000.0 / fps;
            score[cy * resolution + cx] += (h.value() > 0 ? 0.6 * h.value() : 0.0) * Math.pow(0.5, age / HALF_LIFE_MS);
        }
        addArmies(state, score, resolution, extent);
        highlights.removeIf(h -> Format.u(h.getExpireFrame()) < frame);
        InterestMap.Builder b = InterestMap.newBuilder().setFrame((int) frame).setWidth(resolution).setHeight(resolution).setCellWidth((float) cellW).setCellHeight((float) cellH)
            .setOriginX(extent.minX()).setOriginY(extent.minY()).setHalfLifeMs(HALF_LIFE_MS).addAllHighlights(highlights);
        for (double s : score) {
            b.addScores((int) Math.round(s));
        }
        return b.build();
    }

    /** 0.05 per cost unit of the weaker side wherever armies of enemy players stand within a cell of each other. */
    private void addArmies(MatchState state, double[] score, int n, WorldExtent extent) {
        List<PlayerView> players = state.players();
        double[][] value = new double[players.size()][n * n];
        for (int i = 0; i < players.size(); i++) {
            PlayerView p = players.get(i);
            long total = 0;
            for (PlayerGrid g : state.grids()) {
                if (g.getPlayerIndex() == p.playerIndex()) {
                    for (int c : g.getCellsList()) {
                        total += Integer.toUnsignedLong(c);
                    }
                }
            }
            double each = total == 0 ? 0 : Math.max(0, p.stats().getArmyValue()) / (double) total;
            for (PlayerGrid g : state.grids()) {
                if (g.getPlayerIndex() != p.playerIndex() || g.getWidth() == 0) {
                    continue;
                }
                for (int c = 0; c < g.getCellsCount(); c++) {
                    double wx = extent.minX() + (c % g.getWidth() + 0.5) * g.getCellWidth();
                    double wy = extent.minY() + (c / g.getWidth() + 0.5) * g.getCellHeight();
                    int cx = (int) Math.min(n - 1, Math.max(0, (wx - extent.minX()) / (extent.width() / n)));
                    int cy = (int) Math.min(n - 1, Math.max(0, (wy - extent.minY()) / (extent.height() / n)));
                    value[i][cy * n + cx] += Integer.toUnsignedLong(g.getCells(c)) * each;
                }
            }
        }
        for (int a = 0; a < players.size(); a++) {
            for (int b = a + 1; b < players.size(); b++) {
                if (players.get(a).team() >= 0 && players.get(a).team() == players.get(b).team()) {
                    continue;
                }
                for (int cy = 0; cy < n; cy++) {
                    for (int cx = 0; cx < n; cx++) {
                        double sa = 0;
                        double sb = 0;
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                int x = cx + dx;
                                int y = cy + dy;
                                if (x >= 0 && y >= 0 && x < n && y < n) {
                                    sa += value[a][y * n + x];
                                    sb += value[b][y * n + x];
                                }
                            }
                        }
                        score[cy * n + cx] += 0.05 * Math.min(sa, sb);
                    }
                }
            }
        }
    }

    // -- View statistics.

    synchronized ViewStats view(MatchState state, List<FakeWorld.Obj> live, long frame, int notableCost, int tradeMs, int intensityMs) {
        ViewStats.Builder out = ViewStats.newBuilder().setFrame((int) frame).setHasView(quad != null).setTradeWindowMs(tradeMs).setIntensityWindowMs(intensityMs);
        long tradeFrames = (long) tradeMs * fps / 1000;
        long intensityFrames = (long) intensityMs * fps / 1000;
        for (PlayerView p : state.players()) {
            PlayerStats stats = p.stats();
            PlayerViewStats.Builder v = PlayerViewStats.newBuilder().setPlayerIndex(p.playerIndex());
            if (quad != null) {
                double objects = 0;
                double all = 0;
                for (PlayerGrid g : state.grids()) {
                    if (g.getPlayerIndex() != p.playerIndex() || g.getWidth() == 0) {
                        continue;
                    }
                    WorldExtent extent = state.worldExtent().orElse(null);
                    for (int c = 0; c < g.getCellsCount() && extent != null; c++) {
                        long count = Integer.toUnsignedLong(g.getCells(c));
                        all += count;
                        if (count > 0 && insideQuad(quad, (float) (extent.minX() + (c % g.getWidth() + 0.5) * g.getCellWidth()), (float) (extent.minY() + (c / g.getWidth() + 0.5) * g.getCellHeight()))) {
                            objects += count;
                        }
                    }
                }
                double share = all == 0 ? 0 : objects / all;
                int value = (int) Math.round(Math.max(0, stats.getArmyValue()) * share);
                v.setValueInView(value).setEffectiveValue((int) Math.round(value * 0.9));
                CategoryValue by = stats.getArmyValueByClass();
                v.setValueByClass(CategoryValue.newBuilder().setInfantry((int) Math.round(by.getInfantry() * share)).setVehicle((int) Math.round(by.getVehicle() * share))
                    .setAircraft((int) Math.round(by.getAircraft() * share)).setStructure((int) Math.round(by.getStructure() * share)));
                CategoryCounts counts = stats.getArmyCountByClass();
                v.setCountByClass(CategoryCounts.newBuilder().setInfantry((int) Math.round(counts.getInfantry() * share)).setVehicle((int) Math.round(counts.getVehicle() * share))
                    .setAircraft((int) Math.round(counts.getAircraft() * share)).setStructure((int) Math.round(counts.getStructure() * share)));
                for (FakeWorld.Obj o : live) {
                    boolean hero = o.hero();
                    if (o.owner() == p.playerIndex() && (hero || (notableCost > 0 && o.cost() >= notableCost)) && insideQuad(quad, o.x(), o.y())) {
                        v.addNotable(NotableUnit.newBuilder().setObjectId(o.id()).setTemplateName(o.template()).setHealth(o.health()).setVeterancy(o.veterancy())
                            .setCost(o.cost()).setIsHero(hero).setX(o.x()).setY(o.y()));
                        if (o.veterancy() == 1) {
                            v.setVeteran(v.getVeteran() + 1);
                        } else if (o.veterancy() == 2) {
                            v.setElite(v.getElite() + 1);
                        } else if (o.veterancy() >= 3) {
                            v.setHeroic(v.getHeroic() + 1);
                        }
                    }
                }
                MatchPlayer info = p.info();
                if (info != null && info.getHasStartLocation() && insideQuad(quad, info.getStartX(), info.getStartY())) {
                    for (SuperweaponTimer t : stats.getSuperweaponsList()) {
                        v.addSuperweapons(ViewSuperweapon.newBuilder().setObjectId(t.getObjectId()).setTemplateName(t.getTemplateName()).setPowerName(t.getPowerName())
                            .setReadyFrame(t.getReadyFrame()).setPercentReady(t.getPercentReady()).setState(t.getState() == SuperweaponState.SUPERWEAPON_READY
                                ? SuperweaponState.SUPERWEAPON_READY : SuperweaponState.SUPERWEAPON_CHARGING).setX(info.getStartX()).setY(info.getStartY()));
                    }
                }
            }
            double damage = 0;
            for (Hit h : hits) {
                if (!h.inView()) {
                    continue;
                }
                long age = frame - h.frame();
                if (h.value() > 0 && h.player() == p.playerIndex()) {
                    if (age <= tradeFrames) {
                        v.setValueKilled((int) (v.getValueKilled() + h.value()));
                    }
                    if (age <= intensityFrames) {
                        damage += 0.4 * h.value();
                    }
                }
                if (h.value() < 0 && h.player() == p.playerIndex() && age <= tradeFrames) {
                    v.setValueLost((int) (v.getValueLost() - h.value()));
                }
            }
            v.setDamagePerSecond((float) (damage / Math.max(0.001, intensityMs / 1000.0)));
            out.addPlayers(v);
        }
        return out.build();
    }
}
