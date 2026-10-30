package com.generalsonline.stats;

import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.Minimap;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.ObjectRow;
import com.generalsonline.stats.proto.ObjectsQuery;
import com.generalsonline.stats.proto.ObjectsResult;
import com.generalsonline.stats.proto.PageInfo;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.PowerStatus;
import com.generalsonline.stats.proto.ReadinessSample;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.StartPositionInfo;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.generalsonline.stats.proto.TemplateRef;
import com.generalsonline.stats.proto.UiState;
import com.generalsonline.stats.proto.ViewStats;
import com.generalsonline.stats.proto.WaypointInfo;
import com.google.protobuf.ByteString;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;

/**
 * What the fake game knows, built from the frames it has sent so far, and the answers to the queries about it. The
 * catalog is the exception: the templates of the whole dump, as the game's template list does not change during a
 * session. Objects are the ones the events and view statistics have mentioned; an object that was killed or lost no
 * longer exists. Thread safe.
 */
final class FakeWorld {
    /** One object the dump mentioned. */
    record Obj(int id, String template, int owner, float x, float y, boolean hasPosition, float health, int veterancy, boolean dead, int cost, boolean hero) {
    }

    private final Object lock = new Object();
    private final MatchState state = new MatchState();
    private final Map<Integer, Obj> objects = new TreeMap<>();
    private final List<CatalogEntry> catalog;
    private final Map<String, Integer> templateIds = new HashMap<>();
    private StatsSnapshot lastFull;
    private UiState lastUi;
    private LobbyState lastLobby;
    private MatchInfo lastMatch;
    private long frame;

    FakeWorld(FakeDump dump) {
        this.catalog = buildCatalog(dump);
        for (CatalogEntry e : catalog) {
            templateIds.put(e.getName(), e.getTemplateId());
        }
    }

    MatchState state() {
        return state;
    }

    // -- Building the catalog.

    private static List<CatalogEntry> buildCatalog(FakeDump dump) {
        Map<String, CatalogEntry.Builder> byName = new LinkedHashMap<>();
        Map<Integer, String> sideOfPlayer = new HashMap<>();
        java.util.Set<String> heroes = new java.util.HashSet<>();
        for (FakeDump.Entry entry : dump.entries) {
            if (entry.message() instanceof MatchInfo m) {
                for (MatchPlayer p : m.getPlayersList()) {
                    if (p.getPlayerIndex() >= 0) {
                        sideOfPlayer.put(p.getPlayerIndex(), p.getSide());
                    }
                }
            } else if (entry.message() instanceof Events events) {
                for (StatEvent e : events.getEventsList()) {
                    switch (e.getKind()) {
                        case STATEVENT_BUILD -> template(byName, e.getPrimaryName(), e.getCategory()).setBuildable(true).setSide(sideOfPlayer.getOrDefault(e.getPlayerIndex(), ""));
                        case STATEVENT_KILL, STATEVENT_LOSS -> {
                            CatalogEntry.Builder b = template(byName, e.getPrimaryName(), e.getCategory());
                            b.setCost(Math.max(b.getCost(), e.getCost()));
                            if (e.getKind() == com.generalsonline.stats.proto.StatEventKind.STATEVENT_LOSS && b.getSide().isEmpty()) {
                                b.setSide(sideOfPlayer.getOrDefault(e.getPlayerIndex(), ""));
                            }
                        }
                        default -> {
                            // other kinds name powers, sciences and upgrades, not templates
                        }
                    }
                }
            } else if (entry.message() instanceof StatsSnapshot s && s.getFull()) {
                for (PlayerStats p : s.getPlayersList()) {
                    for (var count : p.getLiveTemplateCountsList()) {
                        CatalogEntry.Builder b = template(byName, count.getTemplateName(), "");
                        if (b.getSide().isEmpty()) {
                            b.setSide(sideOfPlayer.getOrDefault(p.getPlayerIndex(), ""));
                        }
                    }
                    for (var hero : p.getHeroesList()) {
                        heroes.add(hero.getTemplateName());
                        template(byName, hero.getTemplateName(), "INFANTRY");
                    }
                    for (SuperweaponTimer t : p.getSuperweaponsList()) {
                        template(byName, t.getTemplateName(), "STRUCTURE");
                    }
                }
            }
        }
        List<CatalogEntry> entries = new ArrayList<>();
        int id = 1;
        for (CatalogEntry.Builder b : byName.values()) {
            b.setTemplateId(id++);
            if (heroes.contains(b.getName()) && !b.getKindOfList().contains("HERO")) {
                b.addKindOf("HERO");
            }
            entries.add(b.build());
        }
        return List.copyOf(entries);
    }

    private static CatalogEntry.Builder template(Map<String, CatalogEntry.Builder> byName, String name, String category) {
        CatalogEntry.Builder b = byName.computeIfAbsent(name, n -> CatalogEntry.newBuilder().setName(n));
        String kind = kindOf(category);
        if (!kind.isEmpty() && !b.getKindOfList().contains(kind)) {
            b.addKindOf(kind);
        }
        return b;
    }

    private static String kindOf(String category) {
        return switch (category) {
            case "STRUCTURE", "SUPERWEAPON" -> "STRUCTURE";
            case "INFANTRY", "VEHICLE", "AIRCRAFT" -> category;
            default -> "";
        };
    }

    // -- Following the frames.

    /** Takes a frame the game just sent. */
    void apply(DecodedFrame d) {
        FrameDispatcher.deliver(d, state);
        synchronized (lock) {
            if (d.message() instanceof StatsSnapshot s) {
                frame = Math.max(frame, Format.u(s.getFrame()));
                if (s.getFull()) {
                    lastFull = s;
                    noteSnapshot(s);
                }
            } else if (d.message() instanceof Events e) {
                frame = Math.max(frame, Format.u(e.getFrame()));
                for (StatEvent event : e.getEventsList()) {
                    noteEvent(event);
                }
            } else if (d.message() instanceof UiState u) {
                lastUi = u;
            } else if (d.message() instanceof LobbyState l) {
                lastLobby = l.getInLobby() ? l : null;
            } else if (d.message() instanceof MatchInfo m) {
                lastMatch = m;
                if (m.getPhase() == MatchPhase.MATCH_PHASE_LOADING) {
                    objects.clear();
                }
            } else if (d.message() instanceof ViewStats v) {
                for (var player : v.getPlayersList()) {
                    for (var unit : player.getNotableList()) {
                        put(unit.getObjectId(), unit.getTemplateName(), player.getPlayerIndex(), unit.getX(), unit.getY(), true, unit.getHealth(), unit.getVeterancy(),
                            false, (int) Format.u(unit.getCost()), unit.getIsHero());
                    }
                }
            }
        }
    }

    private void noteSnapshot(StatsSnapshot s) {
        for (PlayerStats p : s.getPlayersList()) {
            for (var hero : p.getHeroesList()) {
                if (hero.getObjectId() != 0) {
                    Obj old = objects.get(hero.getObjectId());
                    put(hero.getObjectId(), hero.getTemplateName(), p.getPlayerIndex(), old == null ? 0 : old.x(), old == null ? 0 : old.y(), old != null && old.hasPosition(),
                        old == null ? 1f : old.health(), old == null ? 0 : old.veterancy(), !hero.getAlive(), old == null ? 0 : old.cost(), true);
                }
            }
            for (SuperweaponTimer t : p.getSuperweaponsList()) {
                if (t.getObjectId() != 0 && !objects.containsKey(t.getObjectId())) {
                    put(t.getObjectId(), t.getTemplateName(), p.getPlayerIndex(), 0, 0, false, 1f, 0, false, costOf(t.getTemplateName()), false);
                }
            }
        }
    }

    private void noteEvent(StatEvent e) {
        int id = (int) Format.u(e.getObjectId());
        switch (e.getKind()) {
            case STATEVENT_BUILD -> {
                if (id != 0) {
                    put(id, e.getPrimaryName(), e.getPlayerIndex(), e.getX(), e.getY(), e.getHasLocation(), 1f, e.getIntValue1(), false, costOf(e.getPrimaryName()), false);
                }
            }
            case STATEVENT_KILL, STATEVENT_LOSS -> {
                if (id != 0) {
                    Obj old = objects.get(id);
                    int owner = e.getKind() == com.generalsonline.stats.proto.StatEventKind.STATEVENT_LOSS ? e.getPlayerIndex() : old == null ? -1 : old.owner();
                    put(id, e.getPrimaryName(), owner, e.getHasLocation() ? e.getX() : old == null ? 0 : old.x(), e.getHasLocation() ? e.getY() : old == null ? 0 : old.y(),
                        e.getHasLocation() || (old != null && old.hasPosition()), 0f, old == null ? 0 : old.veterancy(), true, (int) Format.u(e.getCost()), old != null && old.hero());
                }
                int killer = (int) Format.u(e.getKillerObjectId());
                if (e.getKind() == com.generalsonline.stats.proto.StatEventKind.STATEVENT_KILL && killer != 0 && !objects.containsKey(killer)) {
                    put(killer, e.getKillerTemplate(), e.getPlayerIndex(), 0, 0, false, 1f, 0, false, costOf(e.getKillerTemplate()), false);
                }
            }
            case STATEVENT_CAPTURE -> {
                Obj old = objects.get(id);
                if (id != 0) {
                    put(id, e.getPrimaryName(), e.getPlayerIndex(), e.getHasLocation() ? e.getX() : old == null ? 0 : old.x(), e.getHasLocation() ? e.getY() : old == null ? 0 : old.y(),
                        e.getHasLocation() || (old != null && old.hasPosition()), old == null ? 1f : old.health(), old == null ? 0 : old.veterancy(), false, costOf(e.getPrimaryName()), false);
                }
            }
            case STATEVENT_VETERANCY_CHANGE -> {
                Obj old = objects.get(id);
                if (old != null) {
                    objects.put(id, new Obj(id, old.template(), old.owner(), old.x(), old.y(), old.hasPosition(), old.health(), e.getIntValue2(), old.dead(), old.cost(), old.hero()));
                } else if (id != 0) {
                    put(id, e.getPrimaryName(), e.getPlayerIndex(), 0, 0, false, 1f, e.getIntValue2(), false, costOf(e.getPrimaryName()), false);
                }
            }
            default -> {
                // the other kinds do not name an object
            }
        }
    }

    private void put(int id, String template, int owner, float x, float y, boolean hasPosition, float health, int veterancy, boolean dead, int cost, boolean hero) {
        if (id != 0) {
            objects.put(id, new Obj(id, template, owner, x, y, hasPosition, health, veterancy, dead, cost, hero));
        }
    }

    private int costOf(String template) {
        for (CatalogEntry e : catalog) {
            if (e.getName().equals(template)) {
                return (int) Format.u(e.getCost());
            }
        }
        return 0;
    }

    // -- What a restart of the stream or a resync sends.

    /** The frames a reader that joins now gets, as the messages to send in order (empty parts are left out). */
    List<com.google.protobuf.Message> joinMessages() {
        List<com.google.protobuf.Message> out = new ArrayList<>();
        synchronized (lock) {
            if (lastUi != null) {
                out.add(lastUi);
            }
            if (lastLobby != null) {
                out.add(lastLobby);
            }
            if (lastMatch != null && lastMatch.getPhase() != MatchPhase.MATCH_PHASE_ENDED) {
                out.add(lastMatch);
                if (lastFull != null) {
                    out.add(lastFull);
                }
            }
        }
        return out;
    }

    /** True while a match is running (a MatchInfo that is not ENDED). */
    /** The lobby the user sits in now, or null when there is none or a match runs. */
    LobbyState lobby() {
        synchronized (lock) {
            return lastLobby != null && !(lastMatch != null && lastMatch.getPhase() != MatchPhase.MATCH_PHASE_ENDED) ? lastLobby : null;
        }
    }

    /** Replaces the lobby, after a lobby control action changed it. */
    void replaceLobby(LobbyState lobby) {
        synchronized (lock) {
            lastLobby = lobby;
        }
    }

    boolean matchRunning() {
        synchronized (lock) {
            return lastMatch != null && lastMatch.getPhase() != MatchPhase.MATCH_PHASE_ENDED;
        }
    }

    long frame() {
        synchronized (lock) {
            return frame;
        }
    }

    Optional<MatchInfo> match() {
        synchronized (lock) {
            return Optional.ofNullable(lastMatch);
        }
    }

    /** The position {x, y} of a live object; NaN positions when it exists but its place is not known; null when it does not exist or is dead. */
    float[] positionOf(int id) {
        synchronized (lock) {
            Obj o = objects.get(id);
            if (o == null || o.dead()) {
                return null;
            }
            return o.hasPosition() ? new float[] {o.x(), o.y()} : new float[] {Float.NaN, Float.NaN};
        }
    }

    /** The live objects with a known position, for the synthesized view statistics. */
    List<Obj> liveObjects() {
        synchronized (lock) {
            List<Obj> live = new ArrayList<>();
            for (Obj o : objects.values()) {
                if (!o.dead() && o.hasPosition()) {
                    live.add(o);
                }
            }
            return live;
        }
    }

    // -- Queries.

    private static Response.Builder status(ResponseStatus status, String message) {
        return Response.newBuilder().setStatus(status).setMessage(message);
    }

    private static int pageSize(int requested, int fallback, int max) {
        return Math.min(max, requested == 0 ? fallback : requested);
    }

    Response.Builder catalog(CatalogQuery q, int maxPage) {
        List<CatalogEntry> matching = new ArrayList<>();
        for (CatalogEntry e : catalog) {
            if (!q.getName().isEmpty() && !e.getName().equals(q.getName())) {
                continue;
            }
            if (!q.getSide().isEmpty() && !e.getSide().equalsIgnoreCase(q.getSide())) {
                continue;
            }
            if (!q.getKindOf().isEmpty() && !e.getKindOfList().contains(q.getKindOf().toUpperCase(Locale.ROOT))) {
                continue;
            }
            if (q.getBuildableOnly() && !e.getBuildable()) {
                continue;
            }
            matching.add(e);
        }
        int size = pageSize(q.getPage().getPageSize(), 100, maxPage);
        long cursor = Format.u(q.getPage().getCursor());
        int from = (int) Math.min(cursor, matching.size());
        int to = Math.min(matching.size(), from + size);
        CatalogResult.Builder result = CatalogResult.newBuilder().addAllEntries(matching.subList(from, to));
        result.setPage(PageInfo.newBuilder().setHasMore(to < matching.size()).setNextCursor(to < matching.size() ? to : 0).setTotal(matching.size()));
        return Response.newBuilder().setCatalog(result);
    }

    Response.Builder object(int id) {
        synchronized (lock) {
            Obj o = objects.get(id);
            if (o == null || o.dead()) {
                return status(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no object " + Integer.toUnsignedString(id));
            }
            ObjectDetail.Builder d = ObjectDetail.newBuilder().setObjectId(id).setTemplateName(o.template()).setOwnerPlayerIndex(o.owner())
                .setHealth(o.health() * 100f).setMaxHealth(100f).setVeterancy(o.veterancy());
            if (o.hasPosition()) {
                d.setX(o.x()).setY(o.y());
            }
            return Response.newBuilder().setObject(d);
        }
    }

    Response.Builder objects(ObjectsQuery q, int maxPage) {
        if (q.getHasArea() && (q.getMinX() > q.getMaxX() || q.getMinY() > q.getMaxY())) {
            return status(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, "the area is empty (min above max)");
        }
        int size = pageSize(q.getPage().getPageSize(), 100, maxPage);
        long cursor = Format.u(q.getPage().getCursor());
        String kind = q.getKindOf().toUpperCase(Locale.ROOT);
        ObjectsResult.Builder result = ObjectsResult.newBuilder();
        Map<Integer, String> used = new LinkedHashMap<>();
        boolean more = false;
        int next = 0;
        synchronized (lock) {
            for (Obj o : objects.values()) {
                if (Integer.toUnsignedLong(o.id()) < cursor || (o.dead() && !q.getIncludeDead())) {
                    continue;
                }
                if (q.getFilterPlayer() && o.owner() != q.getPlayerIndex()) {
                    continue;
                }
                if (q.getHasArea() && (!o.hasPosition() || o.x() < q.getMinX() || o.x() > q.getMaxX() || o.y() < q.getMinY() || o.y() > q.getMaxY())) {
                    continue;
                }
                if (!kind.isEmpty() && !hasKind(o, kind)) {
                    continue;
                }
                if (result.getRowsCount() == size) {
                    more = true;
                    next = o.id();
                    break;
                }
                int templateId = templateIds.getOrDefault(o.template(), 0);
                used.put(templateId, o.template());
                result.addRows(ObjectRow.newBuilder().setObjectId(o.id()).setTemplateId(templateId).setOwnerPlayerIndex(o.owner()).setX(o.x()).setY(o.y())
                    .setHealthPercent(Math.round(o.health() * 100f)).setVeterancy(o.veterancy()).setIsDead(o.dead()));
            }
        }
        used.forEach((id, name) -> result.addTemplates(TemplateRef.newBuilder().setTemplateId(id).setName(name)));
        result.setPage(PageInfo.newBuilder().setHasMore(more).setNextCursor(more ? next : 0));
        return Response.newBuilder().setObjects(result);
    }

    private boolean hasKind(Obj o, String kind) {
        if (kind.equals("HERO")) {
            return o.hero();
        }
        for (CatalogEntry e : catalog) {
            if (e.getName().equals(o.template())) {
                return e.getKindOfList().contains(kind);
            }
        }
        return false;
    }

    Response.Builder player(int index) {
        if (state.match().isEmpty() || !matchRunning()) {
            return status(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, "no match is running");
        }
        PlayerView view = state.player(index).orElse(null);
        if (view == null) {
            return status(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no player " + index);
        }
        PlayerStats stats = view.stats();
        PlayerDetail.Builder d = PlayerDetail.newBuilder().setPlayerIndex(index).setName(view.name()).setSide(view.side()).setMoney(stats.getMoney())
            .setRankLevel(stats.getRankLevel()).setSciencePoints(stats.getSkillPoints()).addAllSciences(stats.getSciencesList()).addAllUpgrades(stats.getUpgradesList());
        for (ReadinessSample r : stats.getReadinessList()) {
            PowerStatus.Builder power = PowerStatus.newBuilder().setPowerName(r.getPowerName()).setReadyFrame(r.getReadyFrame()).setPercentReady(r.getPercentReady())
                .setReady(r.getPercentReady() >= 1f);
            for (SuperweaponTimer t : stats.getSuperweaponsList()) {
                if (t.getPowerName().equals(r.getPowerName())) {
                    power.setObjectId(t.getObjectId()).setTemplateName(t.getTemplateName());
                }
            }
            d.addPowers(power);
        }
        for (CatalogEntry e : catalog) {
            if (e.getBuildable() && e.getSide().equalsIgnoreCase(view.side())) {
                d.addBuildable(e.getName());
            }
        }
        return Response.newBuilder().setPlayer(d);
    }

    Response.Builder map(boolean includeMinimap) {
        MatchInfo m = match().orElse(null);
        if (m == null || !m.getHasWorldExtent()) {
            return status(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, "no map is loaded");
        }
        MapDetail.Builder d = MapDetail.newBuilder().setMapName(m.getMapName()).setHasExtent(true).setMinX(m.getWorldMinX()).setMinY(m.getWorldMinY())
            .setMaxX(m.getWorldMaxX()).setMaxY(m.getWorldMaxY());
        int waypoint = 1;
        for (MatchPlayer p : m.getPlayersList()) {
            if (p.getHasStartLocation() && p.getStartPosition() >= 0) {
                d.addStartPositions(StartPositionInfo.newBuilder().setStartPosition(p.getStartPosition()).setX(p.getStartX()).setY(p.getStartY()));
                d.addWaypoints(WaypointInfo.newBuilder().setId(waypoint++).setName("Player_" + (p.getStartPosition() + 1) + "_Start").setX(p.getStartX()).setY(p.getStartY()));
            }
        }
        if (includeMinimap) {
            d.setMinimap(terrain(m));
        }
        return Response.newBuilder().setMap(d);
    }

    /** A generated terrain-like image for the extent: the dump has no map, so this is a stand-in with the right geometry. */
    static Minimap terrain(MatchInfo m) {
        int size = 128;
        long seed = m.getMapName().hashCode();
        double ax = 3 + Math.floorMod(seed, 5);
        double ay = 2 + Math.floorMod(seed / 5, 5);
        byte[] rgba = new byte[size * size * 4];
        for (int py = 0; py < size; py++) {
            for (int px = 0; px < size; px++) {
                double u = px / (double) size;
                double v = py / (double) size;
                double h = 0.5 + 0.25 * Math.sin(u * ax * 2.1 + seed % 7) + 0.2 * Math.sin(v * ay * 2.3 + seed % 5) + 0.1 * Math.sin((u + v) * 11.0);
                int r;
                int g;
                int b;
                if (h < 0.3) {
                    r = 40; g = 90; b = 150;
                } else if (h < 0.4) {
                    r = 190; g = 175; b = 120;
                } else if (h < 0.75) {
                    int shade = (int) ((h - 0.4) * 80);
                    r = 70 + shade / 2; g = 120 + shade; b = 60;
                } else {
                    r = 120; g = 115; b = 110;
                }
                int at = (py * size + px) * 4;
                rgba[at] = (byte) r;
                rgba[at + 1] = (byte) g;
                rgba[at + 2] = (byte) b;
                rgba[at + 3] = (byte) 255;
            }
        }
        float w = m.getWorldMaxX() - m.getWorldMinX();
        float h = m.getWorldMaxY() - m.getWorldMinY();
        return Minimap.newBuilder().setWidth(size).setHeight(size).setRgba(ByteString.copyFrom(rgba)).setOriginX(m.getWorldMinX()).setOriginY(m.getWorldMinY())
            .setCellWidth(w / size).setCellHeight(h / size).build();
    }
}
