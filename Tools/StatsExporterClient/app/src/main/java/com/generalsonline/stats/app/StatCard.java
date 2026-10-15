package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.PlayerView;
import com.google.protobuf.Message;
import com.google.protobuf.TextFormat;

/** The "stat card" shown on double click: every field of the underlying protobuf messages, as protobuf text. */
final class StatCard {
    private StatCard() {
    }

    private static String text(Message message) {
        return TextFormat.printer().escapingNonAscii(false).printToString(message);
    }

    /** The player's MatchInfo entry (including the online rating data) and their latest PlayerStats, as of that snapshot. */
    static String forPlayer(PlayerView row, String state, int logicFps) {
        StringBuilder card = new StringBuilder();
        card.append(row.name()).append("  -  ").append(state).append("  -  as of frame ").append(row.frame())
            .append(" (").append(Format.gameTime(row.frame(), logicFps)).append(")\n\n");
        card.append("== MatchPlayer (static, from MatchInfo) ==\n");
        card.append(row.info() == null ? "(no MatchInfo for this player yet)\n" : text(row.info()));
        card.append("\n== PlayerStats (latest snapshot; heavy fields from the last full one) ==\n");
        card.append(text(row.stats()));
        return card.toString();
    }

    /** The event as the game sent it, with the decoded detail line first. */
    static String forEvent(GuiModel.EventRow row) {
        StringBuilder card = new StringBuilder();
        card.append(row.kind()).append("  -  ").append(row.player()).append("  -  frame ").append(row.frame()).append(" (").append(row.time()).append(")\n");
        if (!row.detail().isEmpty()) {
            card.append("detail: ").append(row.detail()).append('\n');
        }
        card.append("\n== StatEvent ==\n").append(text(row.event()));
        return card.toString();
    }
}
