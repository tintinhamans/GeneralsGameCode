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
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
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
    private final JLabel headerLabel = Ui.caption(" ");
    private final Ui.Pill intensityLabel = new Ui.Pill("Fight intensity: -");
    private final GuiTables.HintTable playersTable;
    private final GuiTables.HintTable notableTable;
    private final GuiTables.HintTable weaponsTable;

    ViewStatsTab(GuiModel model) {
        super(new BorderLayout());
        this.model = model;
        playersTable = GuiTables.table(rows.players, "Waiting for the view statistics. About four a second arrive during a match.",
            Map.ofEntries(Map.entry("Player", 140), Map.entry("In view", 80), Map.entry("Effective", 80), Map.entry("Share", 150), Map.entry("Infantry", 74), Map.entry("Vehicles", 74), Map.entry("Aircraft", 74), Map.entry("Structures", 84), Map.entry("Veteran", 70), Map.entry("Elite", 56), Map.entry("Heroic", 62), Map.entry("Killed", 74), Map.entry("Lost", 66), Map.entry("Damage/s", 80)), GuiTables.Sorting.NONE);
        playersTable.putClientProperty("leftHeaders", "Share");
        GuiTables.useRenderer(playersTable, "Share", new ShareBarRenderer(rows));
        GuiTables.useRenderer(playersTable, "Player", new PlayerNameRenderer(rows));
        notableTable = GuiTables.table(rows.notable, "No hero or expensive unit in view.", Map.of("Unit", 170, "Carries", 160), GuiTables.Sorting.BY_HEADER);
        weaponsTable = GuiTables.table(rows.superweapons, "No superweapon structure in view.", Map.of("Structure", 180, "Power", 150), GuiTables.Sorting.BY_HEADER);

        JPanel top = new JPanel(new BorderLayout());
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, 10, 0));
        top.add(Ui.line(intensityLabel, headerLabel), BorderLayout.CENTER);

        JPanel notablePanel = Ui.section("Notable units in view", Ui.scroll(notableTable));
        JPanel weaponPanel = Ui.section("Superweapons in view", Ui.scroll(weaponsTable));
        JSplitPane lower = Ui.split(JSplitPane.HORIZONTAL_SPLIT, notablePanel, weaponPanel, 0.65);
        JScrollPane playersScroll = Ui.scroll(playersTable);
        playersScroll.setPreferredSize(new Dimension(600, 128));
        playersTable.setToolTipText("Killed and lost count the value over the trade window");
        JSplitPane split = Ui.split(JSplitPane.VERTICAL_SPLIT, playersScroll, lower, 0.25);
        JPanel page = new JPanel(new BorderLayout());
        page.add(top, BorderLayout.NORTH);
        page.add(split, BorderLayout.CENTER);
        add(Ui.pad(page), BorderLayout.CENTER);
    }

    /** Takes the library state's latest ViewStats frame; called from a timer, cheap when nothing is new. */
    void refresh() {
        MatchState state = model.state();
        if (rows.update(state)) {
            headerLabel.setText(rows.header());
            intensityLabel.setText("Fight intensity: " + rows.intensity());
            intensityLabel.setTone(rows.intensity().startsWith("heavy") ? Theme.Tone.BAD : rows.intensity().startsWith("a skirmish") ? Theme.Tone.WARN : Theme.Tone.IDLE);
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
            share = value instanceof Double ? (Double) value : 0;
            super.getTableCellRendererComponent(table, Math.round(share * 100) + " %", selected, focus, row, column);
            if (!selected) {
                setBackground(row % 2 == 1 ? Theme.ZEBRA : table.getBackground());
            }
            int modelRow = table.convertRowIndexToModel(row);
            color = Minimap.playerColor(rows.players.get(modelRow).color(), rows.players.get(modelRow).playerIndex());
            return this;
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(getBackground());
                g.fillRect(0, 0, getWidth(), getHeight());
                int barHeight = Math.max(6, getHeight() - 14);
                int y = (getHeight() - barHeight) / 2;
                int track = getWidth() - 20;
                g.setColor(Theme.mix(Theme.FIELD, Theme.TEXT, 0.07f));
                g.fillRoundRect(10, y, track, barHeight, 6, 6);
                int width = (int) Math.round(track * Math.max(0, Math.min(1, share)));
                g.setColor(Theme.alpha(color, 210));
                g.fillRoundRect(10, y, Math.max(width, share > 0 ? 6 : 0), barHeight, 6, 6);
                g.setFont(getFont());
                g.setColor(Theme.TEXT);
                java.awt.FontMetrics metrics = g.getFontMetrics();
                String text = getText();
                g.drawString(text, (getWidth() - metrics.stringWidth(text)) / 2, (getHeight() + metrics.getAscent() - metrics.getDescent()) / 2);
            } finally {
                g.dispose();
            }
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
            super.getTableCellRendererComponent(table, "        " + value, selected, focus, row, column);
            int modelRow = table.convertRowIndexToModel(row);
            swatch = Minimap.playerColor(rows.players.get(modelRow).color(), rows.players.get(modelRow).playerIndex());
            return this;
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            super.paintComponent(graphics);
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(swatch);
                g.fillRoundRect(10, (getHeight() - 12) / 2, 12, 12, 5, 5);
            } finally {
                g.dispose();
            }
        }
    }
}
