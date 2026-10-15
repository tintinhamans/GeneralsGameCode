package com.generalsonline.stats;

/**
 * The result of comparing the lobby's random-side prediction with what the engine resolved, reported once on the
 * LOADING {@code MatchInfo}.
 *
 * @param matched every predicted side, colour, start position and the observer camera were right
 * @param randomStateClean the logic random generator was untouched between the lobby seed and the resolution
 */
public record PredictionCheck(boolean matched, boolean randomStateClean) {
}
