package com.generalsonline.stats.app;

import com.generalsonline.stats.proto.StatEventKind;

import javax.swing.RowFilter;
import javax.swing.table.TableModel;
import java.util.Locale;

/** The Events tab filters: by kind, by player and by a text. All three must match; each one defaults to "everything". */
final class EventFilter {
    private StatEventKind kind;
    private int playerIndex = -1;
    private boolean anyPlayer = true;
    private String text = "";

    /** @param kind null for all kinds */
    void setKind(StatEventKind kind) {
        this.kind = kind;
    }

    /** @param playerIndex the player's index, or null for all players */
    void setPlayer(Integer playerIndex) {
        this.anyPlayer = playerIndex == null;
        this.playerIndex = playerIndex == null ? -1 : playerIndex;
    }

    /** Case-insensitive; matches if any shown cell of the row contains it. */
    void setText(String text) {
        this.text = text == null ? "" : text.trim().toLowerCase(Locale.ROOT);
    }

    boolean matches(GuiModel.EventRow row) {
        if (kind != null && row.event().getKind() != kind) {
            return false;
        }
        if (!anyPlayer && row.playerIndex() != playerIndex) {
            return false;
        }
        if (text.isEmpty()) {
            return true;
        }
        for (String cell : new String[] {row.player(), row.kind(), row.category(), row.name(), row.secondary(), row.location(), row.detail()}) {
            if (cell.toLowerCase(Locale.ROOT).contains(text)) {
                return true;
            }
        }
        return false;
    }

    /** The filter for a TableRowSorter over the events model; calls {@link #matches} each time the sorter refilters. */
    RowFilter<TableModel, Integer> asRowFilter() {
        return new RowFilter<>() {
            @Override
            @SuppressWarnings("unchecked")
            public boolean include(Entry<? extends TableModel, ? extends Integer> entry) {
                return matches(((RowTableModel<GuiModel.EventRow>) entry.getModel()).get(entry.getIdentifier()));
            }
        };
    }
}
