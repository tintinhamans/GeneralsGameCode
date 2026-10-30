package com.generalsonline.stats.app;

import java.awt.Color;
import java.util.Objects;

/**
 * A player color for a table cell. It sorts by team, then name, so a sorted Color column groups the teams instead of
 * ordering by the signed ARGB int. The text of the cell is empty; the renderer paints the swatch.
 */
final class ColorCell implements Comparable<ColorCell> {
    private final int argb;
    private final int team;
    private final String name;
    private final boolean random;

    ColorCell(int argb, int team, String name, boolean random) {
        this.argb = argb;
        this.team = team;
        this.name = name;
        this.random = random;
    }

    int argb() {
        return argb;
    }

    int team() {
        return team;
    }

    String name() {
        return name;
    }

    boolean random() {
        return random;
    }

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

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof ColorCell)) {
            return false;
        }
        ColorCell other = (ColorCell) o;
        return argb == other.argb
                && team == other.team
                && Objects.equals(name, other.name)
                && random == other.random;
    }

    @Override
    public int hashCode() {
        return Objects.hash(argb, team, name, random);
    }

}
