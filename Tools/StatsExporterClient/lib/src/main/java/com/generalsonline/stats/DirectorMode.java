package com.generalsonline.stats;

/** What an automatic camera {@link Director} pays attention to. */
public enum DirectorMode {
    /** Goes to the cell of the interest map with the highest score and stays there until a clearly better one appears. */
    HOTSPOT,
    /** Stays where it is until a highlight is pending (a superweapon launch, a hero death, a big structure lost), shows it, then returns. */
    HIGHLIGHTS,
    /** Cycles through every player's base and army while nothing happens, and hands over to hotspot following when a fight starts. */
    OVERVIEW,
    /** Follows hotspots, is interrupted by highlights, and cycles through the players when it is quiet. */
    DIRECTOR
}
