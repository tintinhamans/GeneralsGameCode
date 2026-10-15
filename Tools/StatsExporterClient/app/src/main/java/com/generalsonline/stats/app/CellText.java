package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.BattlePlan;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.CameraFollowEnd;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CategoryCounts;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.NetworkStats;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.ProductionItem;
import com.generalsonline.stats.proto.ProductionKind;
import com.generalsonline.stats.proto.ProductionQueue;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.SuperweaponTimer;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

import static com.generalsonline.stats.Format.gameTime;
import static com.generalsonline.stats.Format.shortName;
import static com.generalsonline.stats.Format.shortPowerName;
import static com.generalsonline.stats.Format.shortUpgradeName;
import static com.generalsonline.stats.Format.strip;
import static com.generalsonline.stats.Format.u;

/**
 * The one-line summaries the tables and the console printer show: production, superweapons, network, radar, battle
 * plan, camera and lobby cells. Pure functions of protobuf messages. The helpers every reader needs (game time, names,
 * event details) live in the library's {@link Format}.
 */
final class CellText {
    private static final int MAX_SHOWN_ITEMS = 3;

    private CellText() {
    }

    private static String itemName(ProductionItem item) {
        return item.getKind() == ProductionKind.PRODUCTION_KIND_UPGRADE ? shortUpgradeName(item.getName()) : shortName(item.getName());
    }

    /**
     * Per-player production summary: the number of items queued across all structures, then the items being built
     * (the front of each structure's queue) by progress, most complete first, e.g. "4 · Crusader 92%, Ranger 40%, +2".
     */
    static String productionText(PlayerStats p) {
        int total = 0;
        List<ProductionItem> fronts = new ArrayList<>();
        for (ProductionQueue queue : p.getProductionList()) {
            total += queue.getEntriesCount();
            if (queue.getEntriesCount() > 0) {
                fronts.add(queue.getEntries(0));
            }
        }
        if (total == 0) {
            return "";
        }
        fronts.sort(Comparator.comparingDouble(ProductionItem::getPercentComplete).reversed());
        StringBuilder text = new StringBuilder().append(total).append(" · ");
        int shown = Math.min(fronts.size(), MAX_SHOWN_ITEMS);
        for (int i = 0; i < shown; i++) {
            ProductionItem item = fronts.get(i);
            text.append(i > 0 ? ", " : "").append(itemName(item)).append(' ').append(Math.round(item.getPercentComplete())).append('%');
        }
        if (total > shown) {
            text.append(shown > 0 ? ", " : "").append('+').append(total - shown);
        }
        return text.toString();
    }

    /** The same production data with the raw internal names, one structure per line, for the cell tooltip. */
    static String productionDetail(PlayerStats p) {
        StringBuilder text = new StringBuilder();
        for (ProductionQueue queue : p.getProductionList()) {
            text.append(queue.getStructureTemplate()).append(':');
            for (int i = 0; i < queue.getEntriesCount(); i++) {
                ProductionItem item = queue.getEntries(i);
                text.append(' ').append(item.getName());
                if (i == 0) {
                    text.append(' ').append(Math.round(item.getPercentComplete())).append('%');
                }
            }
            text.append('\n');
        }
        return text.toString();
    }

    static String heroesText(PlayerStats p) {
        int alive = 0;
        int dead = 0;
        for (HeroStatus hero : p.getHeroesList()) {
            if (hero.getAlive()) {
                alive++;
            } else {
                dead++;
            }
        }
        return p.getHeroesCount() == 0 ? "" : alive + " alive / " + dead + " dead";
    }

    /**
     * Every superweapon as "Name 1:23" or "Name READY", ready ones first, then by time left. A weapon that reports no
     * ready frame yet shows its charge percentage instead and sorts last.
     */
    static String superweaponText(PlayerStats p, long snapshotFrame, int fps) {
        record Entry(long sortKey, String text) {
        }
        List<Entry> entries = new ArrayList<>();
        for (SuperweaponTimer t : p.getSuperweaponsList()) {
            String name = shortPowerName(t.getPowerName());
            if (t.getState() == SuperweaponState.SUPERWEAPON_READY) {
                entries.add(new Entry(-1, name + " READY"));
            } else if (t.getReadyFrame() != 0) {
                long left = Math.max(0, u(t.getReadyFrame()) - snapshotFrame);
                entries.add(new Entry(left, name + " " + gameTime(left, fps)));
            } else {
                entries.add(new Entry(Long.MAX_VALUE, name + " " + Math.round(t.getPercentReady() * 100) + "%"));
            }
        }
        entries.sort(Comparator.comparingLong(Entry::sortKey));
        List<String> texts = new ArrayList<>();
        for (Entry entry : entries) {
            texts.add(entry.text());
        }
        return String.join(", ", texts);
    }

    /** The raw superweapon power and structure names, one per line, for the cell tooltip. */
    static String superweaponDetail(PlayerStats p) {
        StringBuilder text = new StringBuilder();
        for (SuperweaponTimer t : p.getSuperweaponsList()) {
            text.append(t.getPowerName()).append(" (").append(t.getTemplateName()).append(", ").append(t.getState().name()).append(")\n");
        }
        return text.toString();
    }

    static String radarText(PlayerStats p) {
        return strip(p.getRadar().name(), "RADAR_");
    }

    /** The plan affecting the player's troops, or the Strategy Center's transition while one is moving. */
    static String battlePlanText(PlayerStats p) {
        switch (p.getBattlePlanTransition()) {
            case BATTLE_PLAN_DEPLOYING:
                return "deploying";
            case BATTLE_PLAN_PACKING:
                return "packing";
            default:
                return p.getBattlePlan() == BattlePlan.BATTLE_PLAN_NONE ? "" : strip(p.getBattlePlan().name(), "BATTLE_PLAN_").replace('_', ' ');
        }
    }

    static String sabotageText(PlayerStats p, int fps) {
        return p.getPowerSabotaged() ? "yes, " + gameTime(u(p.getPowerSabotageFramesLeft()), fps) + " left" : "";
    }

    /** "120 ms / 58 fps / ping 45 ms (relay)": frame latency and FPS the engine measured, then the mesh ping; "?" when unknown. */
    static String networkText(PlayerStats p) {
        if (!p.hasNetwork()) {
            return "";
        }
        NetworkStats n = p.getNetwork();
        StringBuilder text = new StringBuilder();
        text.append(n.getLatencyMs() >= 0 ? n.getLatencyMs() + " ms" : "? ms").append(" / ");
        text.append(n.getReportedFps() >= 0 ? n.getReportedFps() + " fps" : "? fps");
        if (n.getPingMs() >= 0) {
            text.append(" / ping ").append(n.getPingMs()).append(" ms").append(n.getRelayed() ? " (relay)" : "");
        }
        return text.toString();
    }

    /** "3/1/0/0": infantry / vehicle / aircraft / structure. */
    static String categoryCounts(CategoryCounts c) {
        return u(c.getInfantry()) + "/" + u(c.getVehicle()) + "/" + u(c.getAircraft()) + "/" + u(c.getStructure());
    }

    /** The online rating of a lobby player, or -1 when the lobby had no stats for them. */
    static int elo(MatchPlayer info) {
        return info != null && info.hasOnline() && info.getOnline().getHasStats() ? info.getOnline().getEloRating() : -1;
    }

    /** Who moved the camera and what it is locked on, empty while the game itself moves it and nothing is locked. */
    static String cameraControlText(Camera c) {
        StringBuilder text = new StringBuilder();
        if (c.getChangeSource() == CameraChangeSource.CAMERA_CHANGE_USER) {
            text.append("    moved by the user");
        } else if (c.getChangeSource() == CameraChangeSource.CAMERA_CHANGE_PIPE) {
            text.append("    moved by a command");
        }
        if (c.getFollowObjectId() != 0) {
            text.append("    following object ").append(Integer.toUnsignedString(c.getFollowObjectId()))
                .append(c.getFollowMode() == CameraFollowMode.CAMERA_FOLLOW_TETHER ? " (tether)" : "");
        }
        if (c.getFollowEnded() != CameraFollowEnd.CAMERA_FOLLOW_END_NONE) {
            text.append("    follow ended: ").append(strip(c.getFollowEnded().name(), "CAMERA_FOLLOW_END_").toLowerCase(java.util.Locale.ROOT).replace('_', ' '));
        }
        return text.toString();
    }

    static String cameraText(Camera c) {
        Format.ViewBox box = Format.viewBox(c);
        String view = box == null ? "" : String.format("    view centre %.0f, %.0f  size %.0f x %.0f  turned %.0f deg", box.centerX(), box.centerY(), box.width(), box.height(), box.rotationDegrees());
        return String.format("looking at %.0f, %.0f    zoom %.2f    angle %.2f    pitch %.2f%s%s", c.getX(), c.getY(), c.getZoom(), c.getAngle(), c.getPitch(), view, cameraControlText(c));
    }

    /**
     * One side cell: "GLA", or "Random -> GLA" for a random slot once the lobby has a valid prediction. A random slot
     * is marked "(check failed)" when the loading screen reported that the prediction did not match the engine.
     */
    static String lobbySide(LobbySlot slot, boolean predictionValid, Boolean predictionMatched) {
        if (!slot.hasPlayer()) {
            return "";
        }
        MatchPlayer p = slot.getPlayer();
        String side = p.getSide();
        if (!p.getIsSideRandom()) {
            return side;
        }
        String text = predictionValid && !slot.getPredictedSide().isEmpty() ? "Random -> " + slot.getPredictedSide() : "Random";
        return Boolean.FALSE.equals(predictionMatched) ? text + " (check failed)" : text;
    }

    /** What the player left on random in the lobby: "side", "color" or "side, color". */
    static String lobbyRandomFlags(LobbySlot slot) {
        if (!slot.hasPlayer()) {
            return "";
        }
        List<String> flags = new ArrayList<>();
        if (slot.getPlayer().getIsSideRandom()) {
            flags.add("side");
        }
        if (slot.getPlayer().getIsColorRandom()) {
            flags.add("color");
        }
        return String.join(", ", flags);
    }
}
