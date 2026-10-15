package com.generalsonline.stats.app;

import javax.swing.JTable;
import javax.swing.RowSorter;
import javax.swing.SortOrder;
import javax.swing.ToolTipManager;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.TableColumn;
import javax.swing.table.TableModel;
import javax.swing.table.TableRowSorter;
import java.awt.Color;
import java.awt.Component;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Rectangle;
import java.awt.event.MouseEvent;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/** Swing table pieces shared by the tabs of {@link StatsGui}: the table itself, column widths and the cell renderers. */
final class GuiTables {
    private static final Color RECENT_ROW = new Color(0xFFF3C4);
    private static final int HEADER_PADDING = 30;

    private GuiTables() {
    }

    /** How a table's header click behaves. */
    enum Sorting {
        /** Clicking a header sorts; the rows keep their order across live updates. */
        BY_HEADER,
        /** An append-only log: header clicks do nothing, so "follow newest" always means the last row. A sorter exists so filters can attach. */
        NONE
    }

    /**
     * @param widths preferred widths by column name; other columns get a default for their type. No column is made
     *               narrower than its header text needs.
     */
    static HintTable table(TableModel model, String hint, Map<String, Integer> widths, Sorting sorting) {
        HintTable table = new HintTable(model, hint);
        TableRowSorter<TableModel> sorter = new TableRowSorter<>(model);
        if (sorting == Sorting.NONE) {
            for (int i = 0; i < model.getColumnCount(); i++) {
                sorter.setSortable(i, false);
            }
        }
        table.setRowSorter(sorter);
        table.setFillsViewportHeight(true);
        table.setRowHeight(table.getRowHeight() + 2);
        table.setAutoResizeMode(JTable.AUTO_RESIZE_OFF);
        FontMetrics header = table.getTableHeader().getFontMetrics(table.getTableHeader().getFont());
        for (int i = 0; i < table.getColumnModel().getColumnCount(); i++) {
            TableColumn column = table.getColumnModel().getColumn(i);
            String name = String.valueOf(column.getHeaderValue());
            int width = widths.getOrDefault(name, defaultWidth(model.getColumnClass(i)));
            column.setPreferredWidth(Math.max(width, header.stringWidth(name) + HEADER_PADDING));
        }
        return table;
    }

    private static int defaultWidth(Class<?> type) {
        if (type == Boolean.class) {
            return 55;
        }
        if (type == ColorCell.class) {
            return 50;
        }
        return Number.class.isAssignableFrom(type) ? 70 : 110;
    }

    /** The default order of a live table: sorted by these model columns, ascending. Clicking a header replaces it. */
    static void sortBy(JTable table, String... columnNames) {
        TableModel model = table.getModel();
        List<RowSorter.SortKey> keys = new java.util.ArrayList<>();
        for (String name : columnNames) {
            for (int i = 0; i < model.getColumnCount(); i++) {
                if (model.getColumnName(i).equals(name)) {
                    keys.add(new RowSorter.SortKey(i, SortOrder.ASCENDING));
                }
            }
        }
        table.getRowSorter().setSortKeys(keys);
    }

    static void useRenderer(JTable table, String columnName, DefaultTableCellRenderer renderer) {
        for (int i = 0; i < table.getColumnModel().getColumnCount(); i++) {
            TableColumn column = table.getColumnModel().getColumn(i);
            if (columnName.equals(column.getHeaderValue())) {
                column.setCellRenderer(renderer);
            }
        }
    }

    /** A color blended towards white, for tinting a text cell with a player's color. */
    static Color tint(int argb) {
        Color base = new Color(argb | 0xFF000000);
        return new Color((base.getRed() + 255) / 2, (base.getGreen() + 255) / 2, (base.getBlue() + 255) / 2);
    }

    /** A table that explains itself while it is empty, and can show a tooltip per cell. */
    @SuppressWarnings("serial")
    static final class HintTable extends JTable {
        private final String hint;
        private BiFunction<Integer, Integer, String> tooltips;

        HintTable(TableModel model, String hint) {
            super(model);
            this.hint = hint;
            ToolTipManager.sharedInstance().registerComponent(this);
        }

        /** @param tooltips given a model row and model column, returns the tooltip text or null */
        void setTooltips(BiFunction<Integer, Integer, String> tooltips) {
            this.tooltips = tooltips;
        }

        @Override
        public String getToolTipText(MouseEvent event) {
            int row = rowAtPoint(event.getPoint());
            int column = columnAtPoint(event.getPoint());
            if (tooltips == null || row < 0 || column < 0) {
                return super.getToolTipText(event);
            }
            String text = tooltips.apply(convertRowIndexToModel(row), convertColumnIndexToModel(column));
            return text == null || text.isEmpty() ? null : "<html>" + text.trim().replace("\n", "<br>") + "</html>";
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            if (getRowCount() == 0) {
                Rectangle area = getVisibleRect();
                g.setColor(Color.GRAY);
                FontMetrics metrics = g.getFontMetrics();
                g.drawString(hint, area.x + Math.max(8, (area.width - metrics.stringWidth(hint)) / 2), area.y + area.height / 2);
            }
        }
    }

    /** Shows a number as an empty cell when it means "none" (negative, and optionally zero). */
    @SuppressWarnings("serial")
    static final class BlankRenderer extends DefaultTableCellRenderer {
        private final boolean zeroToo;

        BlankRenderer(boolean zeroToo) {
            this.zeroToo = zeroToo;
            setHorizontalAlignment(RIGHT);
        }

        @Override
        protected void setValue(Object value) {
            boolean none = value instanceof Number number && (number.doubleValue() < 0 || (zeroToo && number.doubleValue() == 0));
            setText(none || value == null ? "" : value.toString());
        }
    }

    /**
     * Paints a {@link ColorCell} as a filled square inset in the cell, so the selection color still shows around it.
     * A value that is a color index rather than a packed color is shown as its number; null and 0 are empty.
     */
    @SuppressWarnings("serial")
    static final class SwatchRenderer extends DefaultTableCellRenderer {
        private Color swatch;

        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            super.getTableCellRendererComponent(table, "", selected, focus, row, column);
            swatch = null;
            setHorizontalAlignment(LEFT);
            if (value instanceof ColorCell cell) {
                if (cell.random()) {
                    setText("?");
                    setHorizontalAlignment(CENTER);
                } else if (cell.isColor()) {
                    swatch = cell.awt();
                } else if (cell.argb() != 0) {
                    setText(Integer.toString(cell.argb()));
                }
            }
            return this;
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            if (swatch != null) {
                g.setColor(swatch);
                g.fillRect(4, 3, Math.max(1, getWidth() - 8), Math.max(1, getHeight() - 6));
            }
        }
    }

    /**
     * Event table cells: rows from the last few game seconds get a highlight, and the Player cell is tinted with the
     * player's color. Numbers are right aligned; the Cost column shows nothing for zero.
     */
    @SuppressWarnings("serial")
    static final class EventCellRenderer extends DefaultTableCellRenderer {
        private final GuiModel model;
        private final boolean playerColumn;
        private final boolean blankZero;

        EventCellRenderer(GuiModel model, boolean playerColumn, boolean blankZero) {
            this.model = model;
            this.playerColumn = playerColumn;
            this.blankZero = blankZero;
        }

        @Override
        protected void setValue(Object value) {
            boolean none = blankZero && value instanceof Number number && number.doubleValue() == 0;
            setText(none || value == null ? "" : value.toString());
        }

        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            super.getTableCellRendererComponent(table, value, selected, focus, row, column);
            setHorizontalAlignment(value instanceof Number ? RIGHT : LEFT);
            if (!selected) {
                GuiModel.EventRow event = model.events.get(table.convertRowIndexToModel(row));
                Color background = table.getBackground();
                if (model.isRecent(event)) {
                    background = RECENT_ROW;
                }
                int argb = model.state().playerColor(event.playerIndex());
                if (playerColumn && argb != 0) {
                    background = tint(argb);
                }
                setBackground(background);
                setForeground(table.getForeground());
            }
            return this;
        }
    }
}
