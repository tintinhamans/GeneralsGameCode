package com.generalsonline.stats;

import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerResult;
import com.generalsonline.stats.proto.PlayerStats;

import java.util.Locale;

/**
 * One player of a match as {@link MatchState} knows it: the live numbers of the latest snapshot (with the heavy fields
 * of the last full snapshot kept) joined with the static data of {@code MatchInfo} by {@code player_index}. Immutable;
 * a new view replaces it when anything changes.
 *
 * @param playerIndex the engine's player index, the key that snapshots and events use
 * @param info the player's {@code MatchInfo} entry (name, side, colour, team, online rating), or null while no
 *             {@code MatchInfo} named this player
 * @param stats the merged statistics: light fields of the latest snapshot, heavy fields of the last full one
 * @param frame the logic frame of the snapshot these numbers are from
 * @param life whether the player is still in the match
 */
public record PlayerView(int playerIndex, MatchPlayer info, PlayerStats stats, long frame, PlayerLife life) {
    /**
     * Returns the player's name.
     *
     * @return the name from MatchInfo, or "P2" for index 2 while no MatchInfo named the player
     */
    public String name() {
        return info != null ? info.getName() : "P" + playerIndex;
    }

    /**
     * Returns the raw faction name.
     *
     * @return the side such as "America" or "GLA", or an empty text while unknown
     */
    public String side() {
        return info != null ? info.getSide() : "";
    }

    /**
     * Returns the player's colour.
     *
     * @return packed 0xAARRGGBB, or 0 while unknown; see {@link Format#isPackedColor} and {@link Format#rgb}
     */
    public int color() {
        return info != null ? info.getColor() : 0;
    }

    /**
     * Returns the lobby alliance team.
     *
     * @return the team, counted from 0 as on the wire, or -1 when unknown
     */
    public int team() {
        return info != null ? info.getTeam() : -1;
    }

    /**
     * Returns the lobby slot.
     *
     * @return the 0-based lobby slot, or -1 while unknown
     */
    public int lobbySlot() {
        return info != null ? info.getLobbySlot() : -1;
    }

    /**
     * Tells whether a person plays this slot.
     *
     * @return true for a human, false for a computer player or while unknown
     */
    public boolean isHuman() {
        return info != null && info.getIsHuman();
    }

    /**
     * Returns the online rating from the lobby.
     *
     * @return the ELO rating, or -1 when the lobby had no statistics for the player
     */
    public int elo() {
        return info != null && info.hasOnline() && info.getOnline().getHasStats() ? info.getOnline().getEloRating() : -1;
    }

    /**
     * Describes the player's state in one word. The result of a finished match wins, then the engine's own state,
     * then the defeated/left events that arrived before the next snapshot.
     *
     * @return "alive", "defeated", "left", or after the match "won", "lost", "observer"
     */
    public String stateText() {
        if (info != null && info.getResult() != PlayerResult.PLAYER_RESULT_UNKNOWN) {
            String result = resultText(info.getResult());
            if (!result.isEmpty()) {
                return result;
            }
        }
        return life.name().toLowerCase(Locale.ROOT);
    }

    /**
     * Names a match result.
     *
     * @param result a player's result
     * @return "won", "lost", "left", "observer", or an empty text for unknown
     */
    public static String resultText(PlayerResult result) {
        return switch (result) {
            case PLAYER_RESULT_WON -> "won";
            case PLAYER_RESULT_LOST -> "lost";
            case PLAYER_RESULT_LEFT -> "left";
            case PLAYER_RESULT_OBSERVER -> "observer";
            default -> "";
        };
    }
}
