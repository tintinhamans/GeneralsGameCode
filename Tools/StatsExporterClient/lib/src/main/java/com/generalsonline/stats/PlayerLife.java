package com.generalsonline.stats;

/** Whether a player is still in the match, as far as the feed has said so. */
public enum PlayerLife {
    /** The player is playing. */
    ALIVE,
    /** The engine marked the player defeated; the numbers stay and stop changing. */
    DEFEATED,
    /** The player quit, disconnected or surrendered. */
    LEFT
}
