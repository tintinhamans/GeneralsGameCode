package com.generalsonline.stats;

/**
 * The playable map rectangle in world units, from {@code MatchInfo.world_*}. World y points north.
 *
 * @param minX the smallest world x
 * @param minY the smallest world y
 * @param maxX the largest world x
 * @param maxY the largest world y
 */
public record WorldExtent(float minX, float minY, float maxX, float maxY) {
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
}
