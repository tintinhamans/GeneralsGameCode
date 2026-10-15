package com.generalsonline.stats.app;

import java.awt.Color;

/**
 * A player color for a table cell. It sorts by team, then name, so a sorted Color column groups the teams instead of
 * ordering by the signed ARGB int. The text of the cell is empty; the renderer paints the swatch.
 *
 * @param random the color is still on Random in the lobby (argb is 0); drawn as "?"
 * @param argb packed 0xAARRGGBB as the protocol sends it; alpha is ignored when painting
 */
record ColorCell(int argb, int team, String name, boolean random) implements Comparable<ColorCell> {
    ColorCell(int argb, int team, String name) {
        this(argb, team, name, false);
    }

    /**
     * Values below 16 are a color index, not a packed color (older engine builds send an index for lobby colors);
     * they must not be painted as near-black.
     */
    boolean isColor() {
        return com.generalsonline.stats.Format.isPackedColor(argb);
    }

    Color awt() {
        return new Color(argb | 0xFF000000);
    }

    @Override
    public int compareTo(ColorCell other) {
        int byTeam = Integer.compare(team, other.team);
        if (byTeam != 0) {
            return byTeam;
        }
        int byName = name.compareToIgnoreCase(other.name);
        return byName != 0 ? byName : Integer.compare(argb, other.argb);
    }

    @Override
    public String toString() {
        return "";
    }
}
