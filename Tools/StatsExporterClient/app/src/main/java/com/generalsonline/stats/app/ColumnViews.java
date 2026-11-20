package com.generalsonline.stats.app;

import javax.swing.JTable;
import javax.swing.table.TableColumn;
import javax.swing.table.TableColumnModel;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/**
 * The Players table has many columns, so it shows one named group at a time. The table model always holds all of
 * them (so sorting and tooltips keep working); a view only chooses which columns the table shows and in what order.
 */
final class ColumnViews {
    /** @param columns column names, as {@link RowTableModel#getColumnName} returns them */
    static final class View {
        private final String name;
        private final List<String> columns;

        View(String name, List<String> columns) {
            this.name = name;
            this.columns = columns;
        }

        String name() {
            return name;
        }

        List<String> columns() {
            return columns;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof View)) {
                return false;
            }
            View other = (View) o;
            return Objects.equals(name, other.name)
                    && Objects.equals(columns, other.columns);
        }

        @Override
        public int hashCode() {
            return Objects.hash(name, columns);
        }

        @Override
        public String toString() {
            return "View[" + "name=" + name + ", columns=" + columns + "]";
        }
    }

    static final View OVERVIEW = new View("Overview", List.of("Color", "Name", "State", "Team", "Side", "ELO", "Money", "Income/min", "Army value", "APM", "Score", "Rank"));

    static final List<View> PLAYERS = List.of(
        OVERVIEW,
        new View("Combat", List.of("Color", "Name", "State", "Units built", "Units lost", "Units destroyed", "Value destroyed", "Value lost",
            "Kills (inf/veh/air/str)", "Losses (inf/veh/air/str)", "Buildings built", "Buildings lost", "Buildings destroyed")),
        new View("Economy & tech", List.of("Color", "Name", "Money", "Income/min", "Money earned", "Spent units", "Spent structures", "Spent upgrades",
            "Workers", "Idle workers", "Harvesters", "Power (prod / use)", "Power sabotaged", "Radar", "Rank", "Skill pts")),
        new View("Production & powers", List.of("Color", "Name", "Production", "Superweapons", "Heroes", "Battle plan", "Radar")),
        new View("Network", List.of("Color", "Name", "Net (latency / fps / ping)", "Latency ms", "Display FPS", "Ping ms")));

    private ColumnViews() {
    }

    /** The first view, shown until the user picks another. */
    static View defaultView() {
        return PLAYERS.get(0);
    }

    /** The table's columns by name, captured before any view removes some of them. */
    static Map<String, TableColumn> columnsByName(JTable table) {
        Map<String, TableColumn> all = new LinkedHashMap<>();
        TableColumnModel columns = table.getColumnModel();
        for (int i = 0; i < columns.getColumnCount(); i++) {
            TableColumn column = columns.getColumn(i);
            all.put(String.valueOf(column.getHeaderValue()), column);
        }
        return all;
    }

    /** Shows exactly the view's columns, in the view's order. Renderers and widths travel with the TableColumn objects. */
    static void apply(JTable table, Map<String, TableColumn> all, View view) {
        TableColumnModel columns = table.getColumnModel();
        while (columns.getColumnCount() > 0) {
            columns.removeColumn(columns.getColumn(0));
        }
        for (String name : view.columns()) {
            TableColumn column = all.get(name);
            if (column != null) {
                columns.addColumn(column);
            }
        }
    }

    /** Names in a view that the model does not have; empty when the view is consistent with the model. */
    static List<String> unknownColumns(View view, RowTableModel<?> model) {
        List<String> unknown = new ArrayList<>();
        for (String name : view.columns()) {
            if (model.columnIndex(name) < 0) {
                unknown.add(name);
            }
        }
        return unknown;
    }
}
