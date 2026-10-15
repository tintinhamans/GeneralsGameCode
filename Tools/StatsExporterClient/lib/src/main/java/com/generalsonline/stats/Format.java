package com.generalsonline.stats;

import com.generalsonline.stats.proto.BattlePlan;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.RadarState;
import com.generalsonline.stats.proto.StatEvent;

import java.util.Locale;
import java.util.function.IntFunction;

/**
 * Small helpers that every reader of the stats pipe ends up writing: game time from frames, colours from packed ARGB,
 * display names without faction prefixes, the text of an event and the shape of the camera view box. Pure functions of
 * protobuf messages and numbers.
 */
public final class Format {
    private static final String[] VETERANCY_LEVELS = {"Regular", "Veteran", "Elite", "Heroic"};

    // Template-name prefixes that only say which faction or general a unit belongs to; stripped for display, never otherwise.
    private static final String[] GENERAL_PREFIXES = {"AirF_", "Lazr_", "SupW_", "Nuke_", "Infa_", "Tank_", "Chem_", "Demo_", "Slth_", "Boss_"};
    private static final String[] FACTION_PREFIXES = {"America", "China", "GLA", "Boss"};
    private static final String[] KIND_WORDS = {"Infantry", "Vehicle", "Tank", "Jet", "Aircraft", "Structure", "Building"};

    private Format() {
    }

    // -- Numbers and time.

    /**
     * Reads a protobuf uint32 field as the unsigned value it is. The protobuf fields of type uint32 arrive as Java
     * ints, so a value above 2^31 would otherwise look negative.
     *
     * @param value the int returned by a uint32 getter
     * @return the unsigned value
     */
    public static long u(int value) {
        return Integer.toUnsignedLong(value);
    }

    /**
     * Converts logic frames to a clock text.
     *
     * @param frames logic frames since the match started; a negative value counts as 0
     * @param fps the build's logic rate ({@code MatchInfo.logic_frames_per_second}, 30 or 60)
     * @return "m:ss", or "h:mm:ss" from one hour on
     */
    public static String gameTime(long frames, int fps) {
        long seconds = seconds(frames, fps);
        if (seconds >= 3600) {
            return String.format("%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        }
        return String.format("%d:%02d", seconds / 60, seconds % 60);
    }

    /**
     * Converts logic frames to whole game seconds.
     *
     * @param frames logic frames since the match started; a negative value counts as 0
     * @param fps the build's logic rate, 1 or more
     * @return the game time in whole seconds
     */
    public static long seconds(long frames, int fps) {
        return Math.max(0, frames) / Math.max(1, fps);
    }

    // -- Colours.

    /**
     * Tells whether a colour value from the protocol is a real packed colour. A value of 0 means "no colour yet" (a
     * random lobby colour) and small values are colour indexes that older engine builds send instead of a colour.
     *
     * @param argb the packed colour of a {@code MatchPlayer} or lobby slot
     * @return true when the value can be painted
     */
    public static boolean isPackedColor(int argb) {
        return argb != 0 && (argb < 0 || argb >= 16);
    }

    /**
     * Drops the alpha byte of a packed colour.
     *
     * @param argb packed 0xAARRGGBB as the protocol sends it
     * @return 0xRRGGBB, which {@code new java.awt.Color(rgb)} accepts
     */
    public static int rgb(int argb) {
        return argb & 0xFFFFFF;
    }

    /**
     * Formats a packed colour for a web page or a log.
     *
     * @param argb packed 0xAARRGGBB as the protocol sends it
     * @return "#RRGGBB"
     */
    public static String colorHex(int argb) {
        return String.format("#%06X", rgb(argb));
    }

    // -- Names.

    /**
     * Removes the prefix when the name starts with it.
     *
     * @param name any text
     * @param prefix the prefix to remove
     * @return the name without the prefix, or the name itself
     */
    public static String strip(String name, String prefix) {
        return name.startsWith(prefix) ? name.substring(prefix.length()) : name;
    }

    /**
     * Shortens a template or upgrade name for display: "AirF_AmericaTankCrusader" becomes "Crusader". Unknown names
     * are left alone.
     *
     * @param raw an internal template name
     * @return the name without general, faction and kind prefixes
     */
    public static String shortName(String raw) {
        String name = raw;
        for (String prefix : GENERAL_PREFIXES) {
            if (name.startsWith(prefix) && name.length() > prefix.length()) {
                name = name.substring(prefix.length());
                break;
            }
        }
        for (String prefix : FACTION_PREFIXES) {
            if (name.startsWith(prefix) && name.length() > prefix.length() && Character.isUpperCase(name.charAt(prefix.length()))) {
                name = name.substring(prefix.length());
                break;
            }
        }
        // A kind word is only dropped after a faction prefix was, so "Tank..." alone stays as it is.
        if (!name.equals(raw)) {
            for (String word : KIND_WORDS) {
                if (name.startsWith(word) && name.length() > word.length() && Character.isUpperCase(name.charAt(word.length()))) {
                    name = name.substring(word.length());
                    break;
                }
            }
        }
        return name;
    }

    /**
     * Shortens an upgrade name: "Upgrade_AmericaAdvancedTraining" becomes "AdvancedTraining (upgrade)".
     *
     * @param raw an internal upgrade name
     * @return the display text
     */
    public static String shortUpgradeName(String raw) {
        String name = raw.startsWith("Upgrade_") && raw.length() > 8 ? raw.substring(8) : raw;
        return shortName(name) + " (upgrade)";
    }

    /**
     * Shortens a special power name: "SuperweaponNeutronMissile" becomes "NeutronMissile".
     *
     * @param raw an internal power name
     * @return the name without the Superweapon or SpecialPower prefix
     */
    public static String shortPowerName(String raw) {
        for (String prefix : new String[] {"Superweapon", "SpecialPower"}) {
            if (raw.startsWith(prefix) && raw.length() > prefix.length()) {
                return raw.substring(prefix.length());
            }
        }
        return raw;
    }

    /**
     * Turns a map path into a name: "maps\alpine assault\alpine assault.map" becomes "alpine assault".
     *
     * @param mapPath {@code MatchInfo.map_name}
     * @return the file name without folders and extension, or an empty text for an empty path
     */
    public static String mapDisplayName(String mapPath) {
        if (mapPath.isEmpty()) {
            return "";
        }
        int slash = Math.max(mapPath.lastIndexOf('\\'), mapPath.lastIndexOf('/'));
        String file = mapPath.substring(slash + 1);
        int dot = file.lastIndexOf('.');
        return dot > 0 ? file.substring(0, dot) : file;
    }

    /**
     * Strips an enum prefix from a protobuf enum value for display.
     *
     * @param value an enum value, or null
     * @param prefix the prefix to remove from its name
     * @return the shortened name, or "?" for null
     */
    public static String enumName(Enum<?> value, String prefix) {
        return value == null ? "?" : strip(value.name(), prefix);
    }

    // -- Errors.

    /**
     * Finds the message of the innermost cause. An ExecutionException or IOException wrapper says nothing useful.
     *
     * @param error any throwable, or null
     * @return the innermost message, its class name when it has none, or an empty text for null
     */
    public static String cause(Throwable error) {
        if (error == null) {
            return "";
        }
        Throwable root = error;
        while (root.getCause() != null && root.getCause() != root) {
            root = root.getCause();
        }
        String message = root.getMessage();
        return message == null || message.isBlank() ? root.getClass().getSimpleName() : message;
    }

    // -- Events.

    /**
     * Names a veterancy level.
     *
     * @param level 0 to 3
     * @return "Regular", "Veteran", "Elite", "Heroic", or "level N" for any other number
     */
    public static String veterancyName(int level) {
        return level >= 0 && level < VETERANCY_LEVELS.length ? VETERANCY_LEVELS[level] : "level " + level;
    }

    /**
     * Formats the location of an event.
     *
     * @param event the event
     * @return "12, 34" for an event with a location, else an empty text
     */
    public static String eventLocation(StatEvent event) {
        return event.getHasLocation() ? Math.round(event.getX()) + ", " + Math.round(event.getY()) : "";
    }

    /**
     * Describes the free-form part of an event: the old and new value of a state change, the old owner of a capture,
     * the levels of a veterancy change, the tier of a salvage upgrade, the rank of a science purchase, the killer and
     * weapon of a kill or loss. Other kinds return their variant text.
     *
     * @param event the event
     * @param playerName maps a player index to a name; used for the old owner of a capture, see {@link MatchState#playerName}
     * @param logicFps the build's logic rate, to turn a sabotage length into a time
     * @return the detail text, possibly empty
     */
    public static String eventDetail(StatEvent event, IntFunction<String> playerName, int logicFps) {
        switch (event.getKind()) {
            case STATEVENT_RADAR_CHANGED:
                return enumName(RadarState.forNumber(event.getIntValue1()), "RADAR_") + " -> " + enumName(RadarState.forNumber(event.getIntValue2()), "RADAR_");
            case STATEVENT_BATTLE_PLAN_CHANGED:
                return enumName(BattlePlan.forNumber(event.getIntValue1()), "BATTLE_PLAN_") + " -> " + enumName(BattlePlan.forNumber(event.getIntValue2()), "BATTLE_PLAN_");
            case STATEVENT_VETERANCY_CHANGE:
                return veterancyName(event.getIntValue1()) + " -> " + veterancyName(event.getIntValue2());
            case STATEVENT_BUILD:
                // int_value1 is the starting veterancy level of the built unit
                return event.getIntValue1() > 0 ? veterancyName(event.getIntValue1()).toLowerCase(Locale.ROOT) : event.getVariant();
            case STATEVENT_SALVAGE_UPGRADE:
                return "tier " + event.getIntValue1();
            case STATEVENT_SCIENCE_PURCHASE: {
                String text = "rank " + event.getIntValue1() + ", " + event.getIntValue2() + " points left";
                return event.getVariant().isEmpty() ? text : text + " (" + event.getVariant() + ")";
            }
            case STATEVENT_CAPTURE: {
                String from = "from " + (event.getIntValue1() < 0 ? "neutral" : playerName.apply(event.getIntValue1()));
                return event.getVariant().isEmpty() ? from : from + " (" + event.getVariant() + ")";
            }
            case STATEVENT_POWER_SABOTAGED:
                return "for " + gameTime(event.getIntValue1(), logicFps);
            case STATEVENT_KILL:
            case STATEVENT_LOSS:
                return killerDetail(event);
            default:
                return event.getVariant();
        }
    }

    /** The variant, then who did it and with what: "by ChinaTankGattling with GattlingTankGun". */
    private static String killerDetail(StatEvent event) {
        StringBuilder text = new StringBuilder(event.getVariant());
        if (!event.getKillerTemplate().isEmpty()) {
            text.append(text.length() > 0 ? " " : "").append("by ").append(event.getKillerTemplate());
        }
        if (!event.getWeapon().isEmpty()) {
            text.append(text.length() > 0 ? " " : "").append("with ").append(event.getWeapon());
        }
        return text.toString();
    }

    // -- Camera.

    /**
     * The minimap view box summarised: where it is, how big and how far it is turned. Sizes are in world units.
     *
     * @param centerX world x of the middle of the four corners
     * @param centerY world y of the middle of the four corners
     * @param width average length of the top and bottom edges
     * @param height average length of the left and right edges
     * @param rotationDegrees the angle of the top edge, counter-clockwise from the world x axis
     */
    public record ViewBox(double centerX, double centerY, double width, double height, double rotationDegrees) {
    }

    /**
     * Summarises the view box of a camera frame. The ground box of a tilted camera is a trapezoid, so width and
     * height are averages of the opposite edges.
     *
     * @param camera a camera frame
     * @return the summary, or null when the frame carries no view box
     */
    public static ViewBox viewBox(Camera camera) {
        if (!camera.getHasViewBox()) {
            return null;
        }
        Camera c = camera;
        double centerX = (c.getViewTlX() + c.getViewTrX() + c.getViewBrX() + c.getViewBlX()) / 4.0;
        double centerY = (c.getViewTlY() + c.getViewTrY() + c.getViewBrY() + c.getViewBlY()) / 4.0;
        double top = Math.hypot(c.getViewTrX() - c.getViewTlX(), c.getViewTrY() - c.getViewTlY());
        double bottom = Math.hypot(c.getViewBrX() - c.getViewBlX(), c.getViewBrY() - c.getViewBlY());
        double left = Math.hypot(c.getViewBlX() - c.getViewTlX(), c.getViewBlY() - c.getViewTlY());
        double right = Math.hypot(c.getViewBrX() - c.getViewTrX(), c.getViewBrY() - c.getViewTrY());
        double rotation = Math.toDegrees(Math.atan2(c.getViewTrY() - c.getViewTlY(), c.getViewTrX() - c.getViewTlX()));
        return new ViewBox(centerX, centerY, (top + bottom) / 2, (left + right) / 2, rotation);
    }
}
