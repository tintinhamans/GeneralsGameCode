package com.generalsonline.stats.app;

import javax.swing.JLabel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JViewport;
import javax.swing.RowSorter;
import javax.swing.SortOrder;
import javax.swing.SwingConstants;
import javax.swing.ToolTipManager;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.TableCellRenderer;
import javax.swing.table.TableColumn;
import javax.swing.table.TableModel;
import javax.swing.table.TableRowSorter;
import java.awt.Color;
import java.awt.Component;
import java.awt.FontMetrics;
import java.awt.Font;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.Rectangle;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/** Swing table pieces shared by the tabs of {@link StatsGui}: the table itself, column widths and the cell renderers. */
final class GuiTables {
    private static final Color RECENT_ROW = Theme.mix(Theme.FIELD, Theme.ACCENT, 0.13f);
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
        alignHeader(table);
        table.setAutoResizeMode(JTable.AUTO_RESIZE_SUBSEQUENT_COLUMNS);
        FontMetrics header = table.getTableHeader().getFontMetrics(table.getTableHeader().getFont());
        for (int i = 0; i < table.getColumnModel().getColumnCount(); i++) {
            TableColumn column = table.getColumnModel().getColumn(i);
            String name = String.valueOf(column.getHeaderValue());
            int width = widths.getOrDefault(name, defaultWidth(model.getColumnClass(i)));
            column.setPreferredWidth(Math.max(width, header.stringWidth(name) + HEADER_PADDING));
        }
        return table;
    }

    /** Numbers are right aligned under their header, like their cells. */
    private static void alignHeader(JTable table) {
        TableCellRenderer base = table.getTableHeader().getDefaultRenderer();
        table.getTableHeader().setDefaultRenderer((t, value, selected, focus, row, column) -> {
            Component part = base.getTableCellRendererComponent(t, value, selected, focus, row, column);
            if (part instanceof JLabel) {
                int modelColumn = t.convertColumnIndexToModel(column);
                boolean numeric = Number.class.isAssignableFrom(t.getModel().getColumnClass(modelColumn))
                    && !String.valueOf(value).equals(t.getClientProperty("leftHeaders"));
                ((JLabel) part).setHorizontalAlignment(numeric ? SwingConstants.RIGHT : SwingConstants.LEFT);
            }
            return part;
        });
    }

    /** A table in a bordered scroll pane on the table color. */
    static JScrollPane scroll(JTable table) {
        return Ui.scroll(table);
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

        /** Fills the width of the pane while the columns fit in it; scrolls sideways when they do not. */
        @Override
        public boolean getScrollableTracksViewportWidth() {
            return getParent() instanceof JViewport && getPreferredSize().width < getParent().getWidth();
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
        protected void paintComponent(Graphics graphics) {
            super.paintComponent(graphics);
            if (getRowCount() == 0) {
                paintEmptyState((Graphics2D) graphics.create());
            }
        }

        /** The empty state: the first sentence of the hint as a headline, the rest as muted lines under it, centered. */
        private void paintEmptyState(Graphics2D g) {
            try {
                g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
                Rectangle area = getVisibleRect();
                int split = hint.indexOf(". ");
                String headline = split < 0 ? hint : hint.substring(0, split + 1);
                String rest = split < 0 ? "" : hint.substring(split + 2);
                int maxWidth = Math.max(160, Math.min(520, area.width - 48));
                g.setFont(Theme.semibold(15));
                FontMetrics big = g.getFontMetrics();
                List<String> lines = new ArrayList<>();
                g.setFont(Theme.ui(Font.PLAIN, 12.5f));
                FontMetrics small = g.getFontMetrics();
                StringBuilder line = new StringBuilder();
                for (String word : rest.split(" ")) {
                    if (line.length() > 0 && small.stringWidth(line + " " + word) > maxWidth) {
                        lines.add(line.toString());
                        line.setLength(0);
                    }
                    line.append(line.length() > 0 ? " " : "").append(word);
                }
                if (line.length() > 0) {
                    lines.add(line.toString());
                }
                int height = big.getHeight() + (lines.isEmpty() ? 0 : 6 + lines.size() * small.getHeight());
                int y = area.y + Math.max(big.getAscent() + 24, (area.height - height) / 2 + big.getAscent());
                g.setFont(Theme.semibold(15));
                g.setColor(Theme.TEXT);
                g.drawString(headline, area.x + (area.width - big.stringWidth(headline)) / 2, y);
                g.setFont(Theme.ui(Font.PLAIN, 12.5f));
                g.setColor(Theme.MUTED);
                y += 8;
                for (String text : lines) {
                    y += small.getHeight();
                    g.drawString(text, area.x + (area.width - small.stringWidth(text)) / 2, y);
                }
            } finally {
                g.dispose();
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
            boolean none = value instanceof Number && (((Number) value).doubleValue() < 0 || (zeroToo && ((Number) value).doubleValue() == 0));
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
            setHorizontalAlignment(CENTER);
            if (value instanceof ColorCell) {
                ColorCell cell = (ColorCell) value;
                if (cell.random()) {
                    setText("?");
                } else if (cell.isColor()) {
                    swatch = cell.awt();
                } else if (cell.argb() != 0) {
                    setText(Integer.toString(cell.argb()));
                }
            }
            return this;
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            super.paintComponent(graphics);
            if (swatch != null) {
                Graphics2D g = (Graphics2D) graphics.create();
                try {
                    g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                    int size = Math.min(16, getHeight() - 10);
                    int x = Math.max(6, (getWidth() - size) / 2);
                    int y = (getHeight() - size) / 2;
                    g.setColor(swatch);
                    g.fillRoundRect(x, y, size, size, 6, 6);
                    g.setColor(Theme.alpha(Color.WHITE, 40));
                    g.drawRoundRect(x, y, size, size, 6, 6);
                } finally {
                    g.dispose();
                }
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
            boolean none = blankZero && value instanceof Number && ((Number) value).doubleValue() == 0;
            setText(none || value == null ? "" : value.toString());
        }

        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            super.getTableCellRendererComponent(table, value, selected, focus, row, column);
            setHorizontalAlignment(value instanceof Number ? RIGHT : LEFT);
            if (!selected) {
                GuiModel.EventRow event = model.events.get(table.convertRowIndexToModel(row));
                Color background = row % 2 == 1 ? Theme.ZEBRA : table.getBackground();
                if (model.isRecent(event)) {
                    background = RECENT_ROW;
                }
                int argb = model.state().playerColor(event.playerIndex());
                setBackground(background);
                setForeground(playerColumn && argb != 0 ? Theme.legible(new Color(argb | 0xFF000000)) : table.getForeground());
            }
            return this;
        }
    }
}
