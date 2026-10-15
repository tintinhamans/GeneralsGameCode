package com.generalsonline.stats.app;

import com.generalsonline.stats.MatchState;

import javax.swing.BorderFactory;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTable;
import javax.swing.table.DefaultTableCellRenderer;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.Graphics;
import java.util.Map;

/**
 * The In view tab: what the camera sees, per player. A table with the value, the health weighted value, the value by class,
 * veterancy, the value killed and lost and the damage per second, with a bar for each player's share of the value in
 * view; below it the notable units (heroes and expensive units) and the superweapon structures in view. All numbers
 * are those of the {@code ViewStats} frame; {@link ViewStatsModel} turns them into rows.
 */
@SuppressWarnings("serial")
final class ViewStatsTab extends JPanel {
    private final ViewStatsModel rows = new ViewStatsModel();
    private final GuiModel model;
    private final JLabel headerLabel = new JLabel(" ");
    private final JLabel intensityLabel = new JLabel(" ");
    private final GuiTables.HintTable playersTable;
    private final GuiTables.HintTable notableTable;
    private final GuiTables.HintTable weaponsTable;

    ViewStatsTab(GuiModel model) {
        super(new BorderLayout());
        this.model = model;
        playersTable = GuiTables.table(rows.players, "Waiting for a ViewStats frame (about four a second during a match).",
            Map.of("Player", 130, "Share", 170, "Killed (window)", 100, "Lost (window)", 100), GuiTables.Sorting.NONE);
        GuiTables.useRenderer(playersTable, "Share", new ShareBarRenderer(rows));
        GuiTables.useRenderer(playersTable, "Player", new PlayerNameRenderer(rows));
        notableTable = GuiTables.table(rows.notable, "No hero or expensive unit in view.", Map.of("Unit", 170, "Carries", 160), GuiTables.Sorting.BY_HEADER);
        weaponsTable = GuiTables.table(rows.superweapons, "No superweapon structure in view.", Map.of("Structure", 180, "Power", 150), GuiTables.Sorting.BY_HEADER);

        headerLabel.setBorder(BorderFactory.createEmptyBorder(6, 8, 0, 8));
        intensityLabel.setBorder(BorderFactory.createEmptyBorder(2, 8, 6, 8));
        intensityLabel.setFont(intensityLabel.getFont().deriveFont(Font.BOLD, 14f));
        JPanel top = new JPanel(new BorderLayout());
        top.add(headerLabel, BorderLayout.NORTH);
        top.add(intensityLabel, BorderLayout.SOUTH);

        JScrollPane notableScroll = new JScrollPane(notableTable);
        notableScroll.setBorder(BorderFactory.createTitledBorder("Notable units in view (right-click: Inspect object)"));
        JScrollPane weaponScroll = new JScrollPane(weaponsTable);
        weaponScroll.setBorder(BorderFactory.createTitledBorder("Superweapons in view"));
        JSplitPane lower = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, notableScroll, weaponScroll);
        lower.setResizeWeight(0.65);
        JScrollPane playersScroll = new JScrollPane(playersTable);
        playersScroll.setPreferredSize(new Dimension(600, 160));
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, playersScroll, lower);
        split.setResizeWeight(0.25);
        add(top, BorderLayout.NORTH);
        add(split, BorderLayout.CENTER);
    }

    /** Takes the library state's latest ViewStats frame; called from a timer, cheap when nothing is new. */
    void refresh() {
        MatchState state = model.state();
        if (rows.update(state)) {
            headerLabel.setText(rows.header());
            intensityLabel.setText("Fight intensity: " + rows.intensity());
            intensityLabel.setForeground(rows.intensity().startsWith("heavy") ? PanelKit.BAD : rows.intensity().startsWith("a skirmish") ? PanelKit.WARN : null);
        }
    }

    ViewStatsModel rows() {
        return rows;
    }

    GuiTables.HintTable playersTable() {
        return playersTable;
    }

    GuiTables.HintTable notableTable() {
        return notableTable;
    }

    GuiTables.HintTable weaponsTable() {
        return weaponsTable;
    }

    /** Draws a share as a bar in the colour of the player, with the percentage over it. */
    @SuppressWarnings("serial")
    private static final class ShareBarRenderer extends DefaultTableCellRenderer {
        private final ViewStatsModel rows;
        private double share;
        private Color color = Color.GRAY;

        ShareBarRenderer(ViewStatsModel rows) {
            this.rows = rows;
            setHorizontalAlignment(CENTER);
        }

        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            share = value instanceof Double d ? d : 0;
            super.getTableCellRendererComponent(table, Math.round(share * 100) + " %", selected, focus, row, column);
            int modelRow = table.convertRowIndexToModel(row);
            color = Minimap.playerColor(rows.players.get(modelRow).color(), rows.players.get(modelRow).playerIndex());
            return this;
        }

        @Override
        protected void paintComponent(Graphics g) {
            g.setColor(getBackground());
            g.fillRect(0, 0, getWidth(), getHeight());
            int width = (int) Math.round((getWidth() - 4) * Math.max(0, Math.min(1, share)));
            g.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), 190));
            g.fillRect(2, 3, width, Math.max(1, getHeight() - 6));
            g.setColor(getForeground());
            java.awt.FontMetrics metrics = g.getFontMetrics();
            String text = getText();
            g.drawString(text, (getWidth() - metrics.stringWidth(text)) / 2, (getHeight() + metrics.getAscent() - metrics.getDescent()) / 2);
        }
    }

    /** The player's name with a swatch of the player's colour in front of it. */
    @SuppressWarnings("serial")
    private static final class PlayerNameRenderer extends DefaultTableCellRenderer {
        private final ViewStatsModel rows;
        private Color swatch = Color.GRAY;

        PlayerNameRenderer(ViewStatsModel rows) {
            this.rows = rows;
        }

        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            super.getTableCellRendererComponent(table, "      " + value, selected, focus, row, column);
            int modelRow = table.convertRowIndexToModel(row);
            swatch = Minimap.playerColor(rows.players.get(modelRow).color(), rows.players.get(modelRow).playerIndex());
            return this;
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            g.setColor(swatch);
            g.fillRect(4, 4, 12, Math.max(1, getHeight() - 8));
        }
    }
}
