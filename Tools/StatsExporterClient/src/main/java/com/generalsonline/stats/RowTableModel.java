package com.generalsonline.stats;

import javax.swing.table.AbstractTableModel;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

/** A table model over a list of row objects; each column is a name, a class (for sorting and rendering) and a getter. */
@SuppressWarnings("serial")
final class RowTableModel<T> extends AbstractTableModel {
    record Column<T>(String name, Class<?> type, Function<T, ?> value) {
    }

    private final List<Column<T>> columns;
    private final List<T> rows = new ArrayList<>();
    private final int maxRows;

    /** @param maxRows the oldest rows are dropped beyond this many */
    RowTableModel(int maxRows, List<Column<T>> columns) {
        this.maxRows = maxRows;
        this.columns = columns;
    }

    static <T> Column<T> column(String name, Class<?> type, Function<T, ?> value) {
        return new Column<>(name, type, value);
    }

    @Override
    public int getRowCount() {
        return rows.size();
    }

    @Override
    public int getColumnCount() {
        return columns.size();
    }

    @Override
    public String getColumnName(int column) {
        return columns.get(column).name();
    }

    @Override
    public Class<?> getColumnClass(int column) {
        return columns.get(column).type();
    }

    @Override
    public Object getValueAt(int row, int column) {
        return columns.get(column).value().apply(rows.get(row));
    }

    T get(int row) {
        return rows.get(row);
    }

    /** The index of this very row object, searching from the newest end, or -1 if it was dropped. */
    int indexOfRow(T row) {
        for (int i = rows.size() - 1; i >= 0; i--) {
            if (rows.get(i) == row) {
                return i;
            }
        }
        return -1;
    }

    void add(T row) {
        rows.add(row);
        fireTableRowsInserted(rows.size() - 1, rows.size() - 1);
        int excess = rows.size() - maxRows;
        if (excess > 0) {
            rows.subList(0, excess).clear();
            fireTableRowsDeleted(0, excess - 1);
        }
    }

    /** Replaces one row in place, which keeps the selection and scroll position. */
    void set(int row, T value) {
        rows.set(row, value);
        fireTableRowsUpdated(row, row);
    }

    void replaceAll(List<T> newRows) {
        rows.clear();
        rows.addAll(newRows);
        fireTableDataChanged();
    }

    void clear() {
        if (!rows.isEmpty()) {
            rows.clear();
            fireTableDataChanged();
        }
    }
}
