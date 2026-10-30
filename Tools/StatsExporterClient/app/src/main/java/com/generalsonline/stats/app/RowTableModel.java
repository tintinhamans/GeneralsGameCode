package com.generalsonline.stats.app;

import javax.swing.table.AbstractTableModel;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.function.Function;

/** A table model over a list of row objects; each column is a name, a class (for sorting and rendering) and a getter. */
@SuppressWarnings("serial")
final class RowTableModel<T> extends AbstractTableModel {
    static final class Column<T> {
        private final String name;
        private final Class<?> type;
        private final Function<T, ?> value;

        Column(String name, Class<?> type, Function<T, ?> value) {
            this.name = name;
            this.type = type;
            this.value = value;
        }

        String name() {
            return name;
        }

        Class<?> type() {
            return type;
        }

        Function<T, ?> value() {
            return value;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Column)) {
                return false;
            }
            Column<?> other = (Column<?>) o;
            return Objects.equals(name, other.name)
                    && Objects.equals(type, other.type)
                    && Objects.equals(value, other.value);
        }

        @Override
        public int hashCode() {
            return Objects.hash(name, type, value);
        }

        @Override
        public String toString() {
            return "Column[" + "name=" + name + ", type=" + type + ", value=" + value + "]";
        }
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

    /** The index of the column with this name, or -1. */
    int columnIndex(String name) {
        for (int i = 0; i < columns.size(); i++) {
            if (columns.get(i).name().equals(name)) {
                return i;
            }
        }
        return -1;
    }

    /** The cell of a named column; throws if there is no such column, so a renamed column fails loudly in tests. */
    Object valueAt(int row, String columnName) {
        int column = columnIndex(columnName);
        if (column < 0) {
            throw new IllegalArgumentException("no column " + columnName);
        }
        return getValueAt(row, column);
    }

    T get(int row) {
        return rows.get(row);
    }

    /** A read-only view of the rows, oldest first. */
    List<T> rows() {
        return Collections.unmodifiableList(rows);
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
        addAll(List.of(row));
    }

    /** Appends rows with one insert event and one trim, however many there are: a burst costs the table one update. */
    void addAll(List<T> newRows) {
        if (newRows.isEmpty()) {
            return;
        }
        int first = rows.size();
        rows.addAll(newRows);
        fireTableRowsInserted(first, rows.size() - 1);
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

    /**
     * Replaces all rows. With the same row count the rows are updated in place (one update event, selection and
     * sort order kept); otherwise the table is told its data changed, which clears the selection.
     */
    void replaceAll(List<T> newRows) {
        boolean sameSize = newRows.size() == rows.size();
        List<T> copy = new ArrayList<>(newRows);
        rows.clear();
        rows.addAll(copy);
        if (sameSize && !rows.isEmpty()) {
            fireTableRowsUpdated(0, rows.size() - 1);
        } else {
            fireTableDataChanged();
        }
    }

    /** Tells the table every row changed although the row objects did not (a cell reads state from elsewhere). */
    void refreshAll() {
        if (!rows.isEmpty()) {
            fireTableRowsUpdated(0, rows.size() - 1);
        }
    }

    void clear() {
        if (!rows.isEmpty()) {
            rows.clear();
            fireTableDataChanged();
        }
    }
}
