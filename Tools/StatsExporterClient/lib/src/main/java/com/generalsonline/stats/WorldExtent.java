package com.generalsonline.stats;

import java.util.Objects;

/**
 * The playable map rectangle in world units, from {@code MatchInfo.world_*}. World y points north.
 */
public final class WorldExtent {
    private final float minX;
    private final float minY;
    private final float maxX;
    private final float maxY;

    /**
     * Creates the value.
     *
     * @param minX the smallest world x
     * @param minY the smallest world y
     * @param maxX the largest world x
     * @param maxY the largest world y
     */
    public WorldExtent(float minX, float minY, float maxX, float maxY) {
        this.minX = minX;
        this.minY = minY;
        this.maxX = maxX;
        this.maxY = maxY;
    }

    /**
     * Returns minX.
     *
     * @return the smallest world x
     */
    public float minX() {
        return minX;
    }

    /**
     * Returns minY.
     *
     * @return the smallest world y
     */
    public float minY() {
        return minY;
    }

    /**
     * Returns maxX.
     *
     * @return the largest world x
     */
    public float maxX() {
        return maxX;
    }

    /**
     * Returns maxY.
     *
     * @return the largest world y
     */
    public float maxY() {
        return maxY;
    }

    /**
     * Returns the width of the map.
     *
     * @return maxX - minX
     */
    public float width() {
        return maxX - minX;
    }

    /**
     * Returns the height of the map.
     *
     * @return maxY - minY
     */
    public float height() {
        return maxY - minY;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof WorldExtent)) {
            return false;
        }
        WorldExtent other = (WorldExtent) o;
        return Float.compare(minX, other.minX) == 0
                && Float.compare(minY, other.minY) == 0
                && Float.compare(maxX, other.maxX) == 0
                && Float.compare(maxY, other.maxY) == 0;
    }

    @Override
    public int hashCode() {
        return Objects.hash(minX, minY, maxX, maxY);
    }

    @Override
    public String toString() {
        return "WorldExtent[" + "minX=" + minX + ", minY=" + minY + ", maxX=" + maxX + ", maxY=" + maxY + "]";
    }
}
