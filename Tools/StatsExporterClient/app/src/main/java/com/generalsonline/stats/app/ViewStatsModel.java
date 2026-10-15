package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.proto.NotableUnit;
import com.generalsonline.stats.proto.PlayerViewStats;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.ViewStats;
import com.generalsonline.stats.proto.ViewSuperweapon;

import java.util.ArrayList;
import java.util.List;

import static com.generalsonline.stats.app.RowTableModel.column;

/**
 * What the In view tab shows, without Swing: the latest {@code ViewStats} frame of the library state turned into table rows
 * (one per player, one per notable unit, one per superweapon structure in view) and a header with the windows and the fight
 * intensity. {@link #update} is cheap when no new frame arrived.
 */
final class ViewStatsModel {
    /** Damage per second over which the fighting counts as light, a skirmish, or heavy. */
    static final double LIGHT_DPS = 1.0;
    static final double SKIRMISH_DPS = 100.0;
    static final double HEAVY_DPS = 400.0;

    /** One player's share of the view. {@code share} is the player's value as a fraction of all players' value in view. */
    record PlayerRow(int playerIndex, String name, int color, long value, long effective, double share, long infantry, long vehicle, long aircraft, long structure,
                     int veteran, int elite, int heroic, long killed, long lost, double damagePerSecond) {
    }

    record NotableRow(int objectId, String player, int color, String unit, int healthPercent, String veterancy, long cost, boolean hero, String addon) {
    }

    record WeaponRow(String player, int color, String structure, String power, int percent, String state, String readyIn) {
    }

    final RowTableModel<PlayerRow> players = new RowTableModel<>(64, List.of(
        column("Player", String.class, PlayerRow::name),
        column("Value in view", Long.class, PlayerRow::value),
        column("Effective", Long.class, PlayerRow::effective),
        column("Share", Double.class, PlayerRow::share),
        column("Infantry", Long.class, PlayerRow::infantry),
        column("Vehicles", Long.class, PlayerRow::vehicle),
        column("Aircraft", Long.class, PlayerRow::aircraft),
        column("Structures", Long.class, PlayerRow::structure),
        column("Veteran", Integer.class, PlayerRow::veteran),
        column("Elite", Integer.class, PlayerRow::elite),
        column("Heroic", Integer.class, PlayerRow::heroic),
        column("Killed (window)", Long.class, PlayerRow::killed),
        column("Lost (window)", Long.class, PlayerRow::lost),
        column("Damage/s", String.class, (PlayerRow r) -> String.format("%.0f", r.damagePerSecond()))));

    final RowTableModel<NotableRow> notable = new RowTableModel<>(256, List.of(
        column("Player", String.class, NotableRow::player),
        column("Unit", String.class, NotableRow::unit),
        column("Health", String.class, (NotableRow r) -> r.healthPercent() + " %"),
        column("Veterancy", String.class, NotableRow::veterancy),
        column("Cost", Long.class, NotableRow::cost),
        column("Hero", Boolean.class, NotableRow::hero),
        column("Carries", String.class, NotableRow::addon),
        column("Object id", Long.class, (NotableRow r) -> Format.u(r.objectId()))));

    final RowTableModel<WeaponRow> superweapons = new RowTableModel<>(64, List.of(
        column("Player", String.class, WeaponRow::player),
        column("Structure", String.class, WeaponRow::structure),
        column("Power", String.class, WeaponRow::power),
        column("Charged", String.class, (WeaponRow r) -> r.percent() + " %"),
        column("State", String.class, WeaponRow::state),
        column("Ready in", String.class, WeaponRow::readyIn)));

    private ViewStats shown;
    private String header = "No ViewStats frame yet. They arrive about four times a second once a match has started (the Query tab's Feed section sets the interval).";
    private String intensity = "-";
    private long totalValue;

    /** Rebuilds the rows from the state's latest frame; returns false (and does nothing) when it is the one already shown. */
    boolean update(MatchState state) {
        ViewStats view = state.viewStats().orElse(null);
        if (view == null || view == shown) {
            return false;
        }
        shown = view;
        int fps = state.logicFps();
        long total = 0;
        double damage = 0;
        for (PlayerViewStats p : view.getPlayersList()) {
            total += Format.u(p.getValueInView());
            damage += p.getDamagePerSecond();
        }
        totalValue = total;

        List<PlayerRow> playerRows = new ArrayList<>();
        List<NotableRow> notableRows = new ArrayList<>();
        List<WeaponRow> weaponRows = new ArrayList<>();
        for (PlayerViewStats p : view.getPlayersList()) {
            String name = state.playerName(p.getPlayerIndex());
            int color = state.playerColor(p.getPlayerIndex());
            long value = Format.u(p.getValueInView());
            playerRows.add(new PlayerRow(p.getPlayerIndex(), name, color, value, Format.u(p.getEffectiveValue()), total == 0 ? 0 : (double) value / total,
                Format.u(p.getValueByClass().getInfantry()), Format.u(p.getValueByClass().getVehicle()), Format.u(p.getValueByClass().getAircraft()),
                Format.u(p.getValueByClass().getStructure()), (int) Format.u(p.getVeteran()), (int) Format.u(p.getElite()), (int) Format.u(p.getHeroic()),
                Format.u(p.getValueKilled()), Format.u(p.getValueLost()), p.getDamagePerSecond()));
            for (NotableUnit u : p.getNotableList()) {
                notableRows.add(new NotableRow(u.getObjectId(), name, color, Format.shortName(u.getTemplateName()), Math.round(u.getHealth() * 100f), Format.veterancyName(u.getVeterancy()),
                    Format.u(u.getCost()), u.getIsHero(), u.getAddon().isEmpty() ? "" : Format.shortName(u.getAddon())));
            }
            for (ViewSuperweapon w : p.getSuperweaponsList()) {
                boolean ready = w.getState() == SuperweaponState.SUPERWEAPON_READY;
                long framesLeft = Math.max(0, Format.u(w.getReadyFrame()) - Format.u(view.getFrame()));
                weaponRows.add(new WeaponRow(name, color, Format.shortName(w.getTemplateName()), Format.shortPowerName(w.getPowerName()), Math.round(w.getPercentReady() * 100f),
                    ready ? "READY" : "charging", ready ? "-" : Format.gameTime(framesLeft, fps)));
            }
        }
        players.replaceAll(playerRows);
        notable.replaceAll(notableRows);
        superweapons.replaceAll(weaponRows);
        intensity = intensityWord(damage) + String.format(" (%.0f damage per second in view)", damage);
        header = "Frame " + Format.u(view.getFrame()) + " (" + state.gameTime(Format.u(view.getFrame())) + ")    "
            + (view.getHasView() ? "the camera view is known" : "no camera view yet: only the rolling windows carry data")
            + "    kills and losses over " + view.getTradeWindowMs() / 1000.0 + " s, damage over " + view.getIntensityWindowMs() / 1000.0 + " s of game time"
            + "    value in view " + total;
        return true;
    }

    /** A word for how hard the fighting in view is. */
    static String intensityWord(double damagePerSecond) {
        if (damagePerSecond < LIGHT_DPS) {
            return "quiet";
        }
        if (damagePerSecond < SKIRMISH_DPS) {
            return "light fighting";
        }
        if (damagePerSecond < HEAVY_DPS) {
            return "a skirmish";
        }
        return "heavy fighting";
    }

    String header() {
        return header;
    }

    /** "quiet (0 damage per second in view)" and the like. */
    String intensity() {
        return intensity;
    }

    long totalValue() {
        return totalValue;
    }

    /** The row of the notable table, for the context menu. */
    NotableRow notableRow(int modelRow) {
        return notable.get(modelRow);
    }
}
