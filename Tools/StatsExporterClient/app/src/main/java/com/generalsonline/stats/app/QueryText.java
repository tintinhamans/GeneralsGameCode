package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.PowerStatus;
import com.generalsonline.stats.proto.ScienceOption;
import com.generalsonline.stats.proto.StartPositionInfo;

import java.util.List;
import java.util.function.IntFunction;

/** The answers of the query panel as readable text: one "label: value" line per fact, nothing for a fact the game left out. */
final class QueryText {
    private QueryText() {
    }

    private static void line(StringBuilder text, String label, String value) {
        text.append(String.format("%-20s", label + ":")).append(value).append('\n');
    }

    private static String list(List<String> names) {
        return names.isEmpty() ? "-" : String.join(", ", names);
    }

    /**
     * An object, for the inspector.
     *
     * @param playerName turns a player index into a name
     */
    static String object(ObjectDetail o, IntFunction<String> playerName) {
        StringBuilder text = new StringBuilder();
        line(text, "Template", o.getTemplateName() + (o.getDisplayName().isEmpty() ? "" : "  (" + o.getDisplayName() + ")"));
        line(text, "Object id", Long.toString(Format.u(o.getObjectId())));
        line(text, "Owner", o.getOwnerPlayerIndex() < 0 ? "neutral or unknown" : playerName.apply(o.getOwnerPlayerIndex()) + "  (player " + o.getOwnerPlayerIndex() + ")");
        line(text, "Position", String.format("x %.1f  y %.1f  z %.1f   facing %.0f degrees", o.getX(), o.getY(), o.getZ(), Math.toDegrees(o.getAngle())));
        double percent = o.getMaxHealth() > 0 ? 100.0 * o.getHealth() / o.getMaxHealth() : 0;
        line(text, "Health", String.format("%.0f of %.0f  (%.0f percent)", o.getHealth(), o.getMaxHealth(), percent));
        line(text, "Veterancy", Format.veterancyName(o.getVeterancy()));
        if (o.getUnderConstruction()) {
            line(text, "Under construction", String.format("%.0f percent", o.getConstructionPercent()));
        }
        if (o.getIsEffectivelyDead()) {
            line(text, "Dead", "effectively dead");
        }
        if (!o.getStatusList().isEmpty()) {
            line(text, "Status", list(o.getStatusList()));
        }
        if (!o.getCurrentWeapon().isEmpty()) {
            line(text, "Weapon", o.getCurrentWeapon());
        }
        if (o.getTargetObjectId() != 0) {
            line(text, "Target", "object " + Format.u(o.getTargetObjectId()));
        } else if (o.getHasTargetPosition()) {
            line(text, "Target", String.format("position %.0f, %.0f", o.getTargetX(), o.getTargetY()));
        }
        if (o.getContainedById() != 0) {
            line(text, "Inside", "object " + Format.u(o.getContainedById()));
        }
        if (o.getContainedIdsCount() > 0) {
            StringBuilder ids = new StringBuilder();
            for (int id : o.getContainedIdsList()) {
                ids.append(ids.length() == 0 ? "" : ", ").append(Format.u(id));
            }
            line(text, "Contains", ids.toString());
        }
        if (o.getProducerId() != 0) {
            line(text, "Produced by", "object " + Format.u(o.getProducerId()));
        }
        if (!o.getUpgradesList().isEmpty()) {
            line(text, "Upgrades", list(o.getUpgradesList()));
        }
        return text.toString();
    }

    /** A player, for the player detail. */
    static String player(PlayerDetail p) {
        StringBuilder text = new StringBuilder();
        line(text, "Player", p.getName() + "  (index " + p.getPlayerIndex() + ", " + p.getSide() + ")");
        line(text, "Money", Long.toString(Format.u(p.getMoney())));
        line(text, "Rank", p.getRankLevel() + "   experience " + p.getRankPoints() + " of " + p.getRankPointsNext() + " for the next rank");
        line(text, "Science points", Integer.toString(p.getSciencePoints()));
        line(text, "Sciences", list(p.getSciencesList()));
        line(text, "Upgrades", list(p.getUpgradesList()));
        if (!p.getPurchasableList().isEmpty()) {
            line(text, "Can buy now", sciences(p.getPurchasableList()));
        }
        if (!p.getLockedList().isEmpty()) {
            line(text, "Later", sciences(p.getLockedList()));
        }
        if (!p.getPowersList().isEmpty()) {
            StringBuilder powers = new StringBuilder();
            for (PowerStatus power : p.getPowersList()) {
                powers.append(powers.length() == 0 ? "" : "; ").append(Format.shortPowerName(power.getPowerName())).append(power.getReady() ? " ready" : String.format(" %.0f percent", power.getPercentReady() * 100));
            }
            line(text, "Powers", powers.toString());
        }
        line(text, "Can build now", list(p.getBuildableList()));
        return text.toString();
    }

    private static String sciences(List<ScienceOption> options) {
        StringBuilder text = new StringBuilder();
        for (ScienceOption option : options) {
            text.append(text.length() == 0 ? "" : ", ").append(option.getDisplayName().isEmpty() ? option.getName() : option.getDisplayName()).append(" (").append(Format.u(option.getCost())).append(')');
        }
        return text.toString();
    }

    /** A map, without its image. */
    static String map(MapDetail m) {
        StringBuilder text = new StringBuilder();
        line(text, "Map", m.getMapName());
        if (m.getHasExtent()) {
            line(text, "Extent", String.format("%.0f, %.0f to %.0f, %.0f   (%.0f by %.0f)", m.getMinX(), m.getMinY(), m.getMaxX(), m.getMaxY(), m.getMaxX() - m.getMinX(), m.getMaxY() - m.getMinY()));
        }
        StringBuilder starts = new StringBuilder();
        for (StartPositionInfo s : m.getStartPositionsList()) {
            starts.append(starts.length() == 0 ? "" : "; ").append(s.getStartPosition() + 1).append(String.format(" at %.0f, %.0f", s.getX(), s.getY()));
        }
        line(text, "Start positions", starts.length() == 0 ? "-" : starts.toString());
        line(text, "Waypoints", m.getWaypointsCount() + (m.getWaypointsCount() == 0 ? "" : "  (" + m.getWaypoints(0).getName() + (m.getWaypointsCount() > 1 ? ", ..." : "") + ")"));
        line(text, "Minimap", m.hasMinimap() ? m.getMinimap().getWidth() + " by " + m.getMinimap().getHeight() + " pixels" : "not requested");
        return text.toString();
    }

    /** The effective feed configuration on two lines. */
    static String feed(FeedConfig c) {
        return "snapshots: light " + c.getLightIntervalMs() + " ms, full " + c.getFullIntervalMs() + " ms;  grid " + c.getGridResolution() + ";  view statistics " + c.getViewStatsIntervalMs()
            + " ms;  interest " + (c.getInterest() ? "on, every " + c.getInterestIntervalMs() + " ms" : "off") + "\n"
            + "frames: events " + onOff(c.getEvents()) + ", camera " + onOff(c.getCamera()) + ", chat " + onOff(c.getChat()) + ";  full snapshot parts: grids " + onOff(c.getGrids())
            + ", supply " + onOff(c.getSupply()) + ", tech " + onOff(c.getTech()) + ", production " + onOff(c.getProduction()) + ";  notable cost " + Format.u(c.getNotableCost())
            + ", trade window " + c.getTradeWindowMs() + " ms, intensity window " + c.getIntensityWindowMs() + " ms";
    }

    private static String onOff(boolean on) {
        return on ? "on" : "off";
    }
}
