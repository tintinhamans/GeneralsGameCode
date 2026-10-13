package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollBar;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTabbedPane;
import javax.swing.JTable;
import javax.swing.SwingUtilities;
import javax.swing.Timer;
import javax.swing.UIManager;
import javax.swing.event.TableModelEvent;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.TableColumn;
import javax.swing.table.TableModel;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.HeadlessException;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.IOException;
import java.nio.file.Path;

/**
 * The {@code gui} command: a window that shows the stats feed as tables.
 *
 * Threading: the reader thread of {@link StatsPipeClient} (or the replay thread) only decodes frames. Each decoded
 * frame is handed to the Swing event thread with SwingUtilities.invokeLater, where {@link GuiModel} updates its table
 * models and the tables repaint themselves. No Swing object is touched from any other thread.
 *
 * The window itself is plain Swing: a status bar, one tab per kind of data, and a footer with receive rates.
 * Everything that is not layout lives in {@link GuiModel}.
 */
public final class StatsGui {
    private final GuiModel model = new GuiModel();
    private final JLabel connectionLabel = new JLabel();
    private final JLabel matchLabel = new JLabel();
    private final JLabel progressLabel = new JLabel();
    private final JLabel footerLabel = new JLabel(" ");
    private final JLabel lobbyHeaderLabel = new JLabel();
    private final JLabel predictionLabel = new JLabel(" ");
    private long lastFrames;
    private long lastBytes;

    private StatsGui() {
    }

    /**
     * Opens the window and returns; the JVM then lives until the window is closed.
     *
     * @param replayFile if not null, feed this recorded dump instead of connecting to a game
     * @param replayFramesPerSecond pace for a replay; 0 plays as fast as possible
     */
    public static void run(int pid, String pipe, Path replayFile, int replayFramesPerSecond) {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception e) {
            // The default look and feel is fine.
        }
        SwingUtilities.invokeLater(() -> {
            try {
                new StatsGui().open(pid, pipe, replayFile, replayFramesPerSecond);
            } catch (HeadlessException e) {
                System.err.println("the gui command needs a display");
                System.exit(1);
            }
        });
    }

    private void open(int pid, String pipe, Path replayFile, int replayFramesPerSecond) {
        JFrame frame = new JFrame("Generals Online stats");
        frame.setDefaultCloseOperation(JFrame.DO_NOTHING_ON_CLOSE);
        frame.setSize(1200, 750);
        frame.setLocationByPlatform(true);

        JPanel status = new JPanel();
        status.setLayout(new BoxLayout(status, BoxLayout.Y_AXIS));
        status.setBorder(BorderFactory.createEmptyBorder(6, 8, 6, 8));
        status.add(connectionLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(matchLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(progressLabel);

        footerLabel.setBorder(BorderFactory.createEmptyBorder(3, 8, 3, 8));

        JTabbedPane tabs = new JTabbedPane();
        tabs.addTab("Players", playersTab());
        tabs.addTab("Events", logTab(model.events, 60, 70, 110, 150, 90, 160, 130, 60, 90, 120));
        tabs.addTab("Chat", logTab(model.chat, 80, 100, 130, 90, 500, 120));
        tabs.addTab("Lobby", lobbyTab());
        tabs.addTab("Match", matchTab());
        tabs.addTab("Frames", logTab(model.frames, 100, 150, 60, 800));

        frame.add(status, BorderLayout.NORTH);
        frame.add(tabs, BorderLayout.CENTER);
        frame.add(footerLabel, BorderLayout.SOUTH);

        // The labels are refreshed on a timer, so a fast feed does not cost a repaint per frame.
        new Timer(200, e -> refreshLabels()).start();
        new Timer(1000, e -> refreshRates()).start();
        refreshLabels();

        FrameListener onEdt = new EdtListener(model);
        StatsPipeClient client = null;
        if (replayFile != null) {
            startReplay(replayFile, replayFramesPerSecond, onEdt);
        } else {
            client = new StatsPipeClient(() -> pipe != null ? pipe : PipeDiscovery.choosePipe(pid), onEdt);
            client.setRawTap(raw -> SwingUtilities.invokeLater(() -> model.onRawFrame(raw)));
            client.start();
        }

        final StatsPipeClient closing = client;
        frame.addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                if (closing != null) {
                    closing.close();
                }
                frame.dispose();
                System.exit(0);
            }
        });
        frame.setVisible(true);
    }

    // -- Feeds.

    /** Plays a dump on its own thread through the same path a live frame takes. */
    private void startReplay(Path file, int framesPerSecond, FrameListener onEdt) {
        model.setConnection("replaying " + file);
        Thread thread = new Thread(() -> {
            try {
                int count = DumpFile.readFrames(file, raw -> {
                    pause(framesPerSecond);
                    SwingUtilities.invokeLater(() -> model.onRawFrame(raw));
                    FrameDispatcher.dispatch(raw, onEdt);
                });
                SwingUtilities.invokeLater(() -> model.setConnection("replay finished, " + count + " frames from " + file));
            } catch (IOException e) {
                SwingUtilities.invokeLater(() -> model.setConnection("replay failed: " + e.getMessage()));
            }
        }, "stats-replay");
        thread.setDaemon(true);
        thread.start();
    }

    private static void pause(int framesPerSecond) {
        if (framesPerSecond <= 0) {
            return;
        }
        try {
            Thread.sleep(Math.max(1, 1000 / framesPerSecond));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    /** Forwards every callback to the model on the event thread. */
    private static final class EdtListener implements FrameListener {
        private final GuiModel model;

        EdtListener(GuiModel model) {
            this.model = model;
        }

        @Override
        public void onConnected(String pipePath) {
            SwingUtilities.invokeLater(() -> model.onConnected(pipePath));
        }

        @Override
        public void onDisconnected(Throwable cause) {
            SwingUtilities.invokeLater(() -> model.onDisconnected(cause));
        }

        @Override
        public void onMatchInfo(MatchInfo message) {
            SwingUtilities.invokeLater(() -> model.onMatchInfo(message));
        }

        @Override
        public void onStatsSnapshot(StatsSnapshot message) {
            SwingUtilities.invokeLater(() -> model.onStatsSnapshot(message));
        }

        @Override
        public void onMatchSummary(MatchSummary message) {
            SwingUtilities.invokeLater(() -> model.onMatchSummary(message));
        }

        @Override
        public void onChatMessage(ChatMessage message) {
            SwingUtilities.invokeLater(() -> model.onChatMessage(message));
        }

        @Override
        public void onUiState(UiState message) {
            SwingUtilities.invokeLater(() -> model.onUiState(message));
        }

        @Override
        public void onLobbyState(LobbyState message) {
            SwingUtilities.invokeLater(() -> model.onLobbyState(message));
        }
    }

    // -- Tabs.

    private JScrollPane playersTab() {
        JTable table = table(model.players, 55, 130, 90, 45, 70, 80, 85, 50, 60, 45, 70, 85, 55, 55, 70, 100, 85, 65, 45, 230, 110, 200);
        swatchColumn(table, "Color");
        return new JScrollPane(table);
    }

    /** An append-only table that follows new rows until the user scrolls up. */
    private static JScrollPane logTab(TableModel model, int... widths) {
        JTable table = table(model, widths);
        JScrollPane scroll = new JScrollPane(table);
        model.addTableModelListener(e -> {
            if (e.getType() != TableModelEvent.INSERT) {
                return;
            }
            // This listener runs before the table lays the new row out, so the bar still shows the old end.
            JScrollBar bar = scroll.getVerticalScrollBar();
            boolean atEnd = bar.getValue() + bar.getModel().getExtent() >= bar.getMaximum() - 4;
            if (atEnd) {
                SwingUtilities.invokeLater(() -> {
                    int last = table.getRowCount() - 1;
                    if (last >= 0) {
                        table.scrollRectToVisible(table.getCellRect(last, 0, true));
                    }
                });
            }
        });
        return scroll;
    }

    private JPanel lobbyTab() {
        JTable table = table(model.lobbySlots, 45, 80, 130, 90, 55, 45, 70, 50, 65, 110, 100, 100);
        swatchColumn(table, "Color");
        swatchColumn(table, "Predicted color");

        predictionLabel.setOpaque(true);
        predictionLabel.setFont(predictionLabel.getFont().deriveFont(java.awt.Font.BOLD, 14f));
        predictionLabel.setBorder(BorderFactory.createEmptyBorder(4, 8, 4, 8));

        JPanel header = new JPanel();
        header.setLayout(new BoxLayout(header, BoxLayout.Y_AXIS));
        header.setBorder(BorderFactory.createEmptyBorder(6, 8, 6, 8));
        header.add(lobbyHeaderLabel);
        header.add(Box.createVerticalStrut(4));
        header.add(predictionLabel);

        JPanel panel = new JPanel(new BorderLayout());
        panel.add(header, BorderLayout.NORTH);
        panel.add(new JScrollPane(table), BorderLayout.CENTER);
        return panel;
    }

    private JSplitPane matchTab() {
        JTable info = table(model.matchInfo, 200, 700);
        JTable summary = table(model.summary, 130, 90, 60, 60, 65, 80, 80, 100, 100, 100, 120, 100);
        JScrollPane top = new JScrollPane(info);
        top.setPreferredSize(new Dimension(400, 300));
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, top, new JScrollPane(summary));
        split.setResizeWeight(0.6);
        return split;
    }

    // -- Table helpers.

    private static JTable table(TableModel model, int... widths) {
        JTable table = new JTable(model);
        table.setAutoCreateRowSorter(true);
        table.setFillsViewportHeight(true);
        table.setRowHeight(table.getRowHeight() + 2);
        if (widths.length == model.getColumnCount()) {
            table.setAutoResizeMode(JTable.AUTO_RESIZE_OFF);
            for (int i = 0; i < widths.length; i++) {
                table.getColumnModel().getColumn(i).setPreferredWidth(widths[i]);
            }
        }
        return table;
    }

    private static void swatchColumn(JTable table, String columnName) {
        for (int i = 0; i < table.getColumnCount(); i++) {
            TableColumn column = table.getColumnModel().getColumn(i);
            if (columnName.equals(column.getHeaderValue())) {
                column.setCellRenderer(new SwatchRenderer());
            }
        }
    }

    /** Paints a packed 0xAARRGGBB color value as a filled cell; 0 means no color. */
    @SuppressWarnings("serial")
    private static final class SwatchRenderer extends DefaultTableCellRenderer {
        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean selected, boolean focus, int row, int column) {
            super.getTableCellRendererComponent(table, "", selected, focus, row, column);
            int argb = value instanceof Integer ? (Integer) value : 0;
            if (argb != 0) {
                setBackground(new Color(argb | 0xFF000000));
            }
            return this;
        }
    }

    // -- Status.

    private void refreshLabels() {
        connectionLabel.setText(model.connectionLine());
        matchLabel.setText(model.matchLine());
        progressLabel.setText(model.progressLine());
        lobbyHeaderLabel.setText(model.lobbyHeader());

        Boolean ok = model.predictionCheckOk();
        predictionLabel.setText(ok == null ? " " : model.predictionCheckText());
        predictionLabel.setBackground(ok == null ? null : ok ? new Color(0xC8E6C9) : new Color(0xFFCDD2));
        predictionLabel.setForeground(ok == null ? null : ok ? new Color(0x1B5E20) : new Color(0xB71C1C));
    }

    private void refreshRates() {
        long frames = model.framesTotal();
        long bytes = model.bytesTotal();
        footerLabel.setText(String.format("%d frames/s, %.1f KB/s    total %d frames, %.1f KB",
            frames - lastFrames, (bytes - lastBytes) / 1024.0, frames, bytes / 1024.0));
        lastFrames = frames;
        lastBytes = bytes;
    }
}
