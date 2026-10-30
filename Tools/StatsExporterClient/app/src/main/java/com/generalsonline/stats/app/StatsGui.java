package com.generalsonline.stats.app;

import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.StatEventKind;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JDialog;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JMenuItem;
import javax.swing.JPanel;
import javax.swing.JPopupMenu;
import javax.swing.JScrollBar;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTabbedPane;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.Timer;
import javax.swing.UIManager;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import javax.swing.event.TableModelEvent;
import javax.swing.table.TableColumn;
import javax.swing.table.TableModel;
import javax.swing.table.TableRowSorter;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.HeadlessException;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.BiPredicate;
import java.util.function.IntFunction;
import java.util.function.Supplier;

/**
 * The {@code gui} command: a window that shows the stats feed as tables and a minimap.
 *
 * <p>Threading: the reader thread of {@link StatsPipe} (or the replay thread) decodes frames and hands them to
 * {@link EdtFeed}, which queues them and runs one event-thread task per burst. {@link GuiModel} updates its table
 * models there and the tables repaint themselves; the camera and the minimap are refreshed from timers. No Swing
 * object is touched from any other thread.
 *
 * <p>The window itself is plain Swing: a status bar, one tab per kind of data, and a footer with the feed health.
 * Everything that is not layout lives in {@link GuiModel}; table pieces are in {@link GuiTables}.
 */
public final class StatsGui {
    private final GuiModel model = new GuiModel();
    private final FeedHealth health = new FeedHealth();
    private final EdtFeed feed = new EdtFeed(model, health, SwingUtilities::invokeLater);
    private final EventFilter eventFilter = new EventFilter();
    private final JLabel connectionLabel = new JLabel();
    private final JLabel gameLabel = new JLabel();
    private final JLabel matchLabel = new JLabel();
    private final JLabel progressLabel = new JLabel();
    private final JLabel cameraLabel = new JLabel();
    private final JLabel staleLabel = new JLabel(" ");
    private final JLabel resultLabel = new JLabel(" ");
    private final JLabel footerLabel = new JLabel(" ");
    private final JLabel lobbyHeaderLabel = new JLabel();
    private final JLabel predictionLabel = new JLabel(" ");
    private final Minimap minimap = new Minimap(model);
    private volatile StatsPipe pipe;
    private FakeGame fakeGame;
    private final PipeCommands commands = new PipeCommands(() -> pipe, SwingUtilities::invokeLater);
    private final MinimapTab minimapTab = new MinimapTab(model, minimap, commands);
    private final QueryTab queryTab = new QueryTab(commands, model);
    private final CameraTab cameraTab = new CameraTab(commands, model);
    private final LobbyControlPanel lobbyPanel = new LobbyControlPanel(commands, model);
    private final ViewStatsTab viewTab = new ViewStatsTab(model);
    private final DirectorModel directorModel = new DirectorModel(model.state(), () -> pipe);
    private final DirectorTab directorTab = new DirectorTab(directorModel);
    private final CodeTab codeTab = new CodeTab();
    private JTabbedPane tabs;
    private JFrame frame;
    private GuiTables.HintTable playersTable;
    private GuiTables.HintTable eventsTable;
    private JComboBox<PlayerChoice> playerFilterBox;
    private String playerChoicesSignature = "";
    private Color defaultTableForeground;

    StatsGui() {
        model.setNewMatchHook(feed::clearCamera);
    }

    /**
     * Opens the window and returns; the JVM then lives until the window is closed.
     *
     * @param pid the game process to connect to, or a negative number for any game
     * @param pipe an explicit pipe path, or null
     * @param replayFile if not null, feed this recorded dump instead of connecting to a game
     * @param fakeFile if not null, serve this dump as a fake game in this process and connect to it, so the panels that ask the game work offline
     * @param replaySpeed pace for a replay or a fake game as a multiplier of game time (1.0 is real time); 0 plays as fast as possible
     */
    public static void run(int pid, String pipe, Path replayFile, Path fakeFile, double replaySpeed) {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception e) {
            // The default look and feel is fine.
        }
        SwingUtilities.invokeLater(() -> {
            try {
                new StatsGui().open(pid, pipe, replayFile, fakeFile, replaySpeed);
            } catch (HeadlessException e) {
                System.err.println("the gui command needs a display");
                System.exit(1);
            }
        });
    }

    private void open(int pid, String pipe, Path replayFile, Path fakeFile, double replaySpeed) {
        frame = new JFrame(model.windowTitle());
        frame.setDefaultCloseOperation(JFrame.DO_NOTHING_ON_CLOSE);
        frame.setSize(1280, 780);
        frame.setLocationByPlatform(true);
        frame.setContentPane(buildContent());
        startTimers();

        StatsPipe client = null;
        if (replayFile != null) {
            startReplay(replayFile, replaySpeed);
        } else if (fakeFile != null) {
            try {
                fakeGame = FakeGame.builder(fakeFile).speed(replaySpeed).holdForReader(true).namedPipe(false).build().start();
            } catch (java.io.IOException e) {
                model.setConnection("fake game failed: " + Format.cause(e));
                fakeGame = null;
            }
            if (fakeGame != null) {
                client = fakeGame.connect(StatsPipe.builder().listener(feed)).build().start();
            }
        } else {
            // Several games may run; the pipe tries the highest pid first and moves on when a pipe is busy.
            StatsPipe.Builder builder = StatsPipe.builder().listener(feed);
            if (pipe != null) {
                builder.pipe(pipe);
            } else if (pid >= 0) {
                builder.pid(pid);
            }
            client = builder.build().start();
        }
        this.pipe = client;

        final StatsPipe closing = client;
        frame.addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                directorModel.stop();
                if (closing != null) {
                    closing.close();
                }
                if (fakeGame != null) {
                    fakeGame.close();
                }
                frame.dispose();
                System.exit(0);
            }
        });
        frame.setVisible(true);
    }

    GuiModel model() {
        return model;
    }

    /** The whole window content. Package-private so a test can build and paint it without opening a window. */
    JComponent buildContent() {
        JPanel status = new JPanel();
        status.setLayout(new BoxLayout(status, BoxLayout.Y_AXIS));
        status.setBorder(BorderFactory.createEmptyBorder(6, 8, 6, 8));
        status.add(connectionLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(gameLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(matchLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(progressLabel);
        status.add(Box.createVerticalStrut(2));
        status.add(cameraLabel);
        status.add(Box.createVerticalStrut(2));
        staleLabel.setForeground(new Color(0xB71C1C));
        staleLabel.setFont(staleLabel.getFont().deriveFont(Font.BOLD));
        resultLabel.setForeground(new Color(0x1B5E20));
        resultLabel.setFont(resultLabel.getFont().deriveFont(Font.BOLD, 15f));
        status.add(staleLabel);
        status.add(resultLabel);

        footerLabel.setBorder(BorderFactory.createEmptyBorder(3, 8, 3, 8));

        JTabbedPane tabs = new JTabbedPane();
        this.tabs = tabs;
        attachShowCode(minimap, () -> new CodeTab.Request(Examples.Scenario.CAMERA_BOX, Examples.Selection.defaults()));
        attachShowCode(status, () -> new CodeTab.Request(Examples.Scenario.FAKE_SERVER, Examples.Selection.defaults()));
        wireControlPanels();
        tabs.addTab("Players", playersTab());
        tabs.addTab("Minimap", minimapTab);
        tabs.addTab("In view", viewTab);
        tabs.addTab("Events", eventsTab());
        tabs.addTab("Chat", chatTab());
        tabs.addTab("Lobby", lobbyTab());
        tabs.addTab("Match", matchTab());
        tabs.addTab("Query", queryTab);
        tabs.addTab("Camera", cameraTab);
        tabs.addTab("Director", directorTab);
        tabs.addTab("Frames", framesTab());
        tabs.addTab("Code", codeTab);

        JPanel root = new JPanel(new BorderLayout());
        root.add(status, BorderLayout.NORTH);
        root.add(tabs, BorderLayout.CENTER);
        root.add(footerLabel, BorderLayout.SOUTH);
        refreshLabels();
        return root;
    }

    /** Timers on the event thread: the camera and minimap at about 30 Hz, the labels five times a second, the feed health once a second. */
    private void startTimers() {
        new Timer(33, e -> {
            feed.applyCamera();
            if (minimap.isShowing()) {
                minimap.repaint();
            }
        }).start();
        new Timer(200, e -> {
            refreshLabels();
            refreshPanels();
        }).start();
        new Timer(1000, e -> refreshFeedHealth()).start();
    }

    // -- Feeds.

    /** Plays a dump on its own thread through the same path a live frame takes, paced by game time. */
    private void startReplay(Path file, double speed) {
        model.setConnection("replaying " + file);
        Thread thread = new Thread(() -> {
            try {
                // The library paces the replay by game time and the logic rate of the dump's MatchInfo.
                int count = DumpReader.replay(file, feed, speed);
                feed.post(() -> model.setConnection("replay finished, " + count + " frames from " + file));
            } catch (IOException e) {
                feed.post(() -> model.setConnection("replay failed: " + Format.cause(e)));
            }
        }, "stats-replay");
        thread.setDaemon(true);
        thread.start();
    }

    // -- Tabs.

    private JComponent playersTab() {
        playersTable = GuiTables.table(model.players, "Waiting for the first snapshot. Snapshots start once a match has started.",
            Map.of("Name", 150, "Production", 230, "Superweapons", 200, "Net (latency / fps / ping)", 230, "Power (prod / use)", 110, "Heroes", 100,
                "Kills (inf/veh/air/str)", 150, "Losses (inf/veh/air/str)", 150, "Color", 46),
            GuiTables.Sorting.BY_HEADER);
        defaultTableForeground = playersTable.getForeground();
        GuiTables.useRenderer(playersTable, "Color", new GuiTables.SwatchRenderer());
        for (String blankable : List.of("Team", "ELO", "Latency ms", "Display FPS", "Ping ms")) {
            GuiTables.useRenderer(playersTable, blankable, new GuiTables.BlankRenderer(false));
        }
        // Stable order by team, then lobby slot, however the live updates reorder the rows.
        GuiTables.sortBy(playersTable, "Team", "Slot");
        // The cells show shortened names; the tooltip has the raw ones.
        playersTable.setTooltips((row, column) -> {
            var stats = model.players.get(row).stats();
            return switch (model.players.getColumnName(column)) {
                case "Production" -> CellText.productionDetail(stats);
                case "Superweapons" -> CellText.superweaponDetail(stats);
                case "Net (latency / fps / ping)" -> "latency: this player's average frame round trip, FPS: this player's display FPS (both as the engine measures them; known for all players only on the packet router),\nping: Generals Online mesh round trip from this PC";
                case "Team" -> "Shown from 1; blank when the lobby did not set one";
                default -> null;
            };
        });
        openCardOnDoubleClick(playersTable, row -> {
            PlayerView player = model.players.get(row);
            return new String[] {player.name(), StatCard.forPlayer(player, player.stateText(), model.logicFps())};
        });
        attachShowCode(playersTable, (row, column) -> {
            PlayerView player = model.players.get(row);
            String columnName = model.players.getColumnName(column);
            String stat = Examples.statColumn(columnName) != null ? columnName : Examples.Selection.defaults().column();
            return new CodeTab.Request(Examples.Scenario.TRACK_PLAYER, Examples.Selection.defaults().withPlayer(player.name()).withColumn(stat));
        });

        Map<String, TableColumn> allColumns = ColumnViews.columnsByName(playersTable);
        ColumnViews.apply(playersTable, allColumns, ColumnViews.defaultView());
        JComboBox<ColumnViews.View> viewBox = new JComboBox<>(ColumnViews.PLAYERS.toArray(new ColumnViews.View[0]));
        viewBox.setRenderer(new javax.swing.DefaultListCellRenderer() {
            @Override
            public java.awt.Component getListCellRendererComponent(javax.swing.JList<?> list, Object value, int index, boolean selected, boolean focus) {
                return super.getListCellRendererComponent(list, value instanceof ColumnViews.View view ? view.name() : value, index, selected, focus);
            }
        });
        viewBox.addActionListener(e -> ColumnViews.apply(playersTable, allColumns, (ColumnViews.View) viewBox.getSelectedItem()));

        JPanel bar = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 4));
        bar.add(new JLabel("Columns:"));
        bar.add(viewBox);
        bar.add(new JLabel("Double-click a player for every field."));
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(bar, BorderLayout.NORTH);
        panel.add(new JScrollPane(playersTable), BorderLayout.CENTER);
        return panel;
    }

    private JComponent eventsTab() {
        eventsTable = GuiTables.table(model.events, "No events yet. Kills, losses, builds and ability uses appear here as they happen.",
            Map.of("Frame", 60, "Game time", 70, "Player", 110, "Kind", 150, "Category", 90, "Name", 160, "Secondary", 130, "Location", 90, "Detail", 200),
            GuiTables.Sorting.NONE);
        for (int i = 0; i < model.events.getColumnCount(); i++) {
            String name = model.events.getColumnName(i);
            GuiTables.useRenderer(eventsTable, name, new GuiTables.EventCellRenderer(model, name.equals("Player"), name.equals("Cost")));
        }
        openCardOnDoubleClick(eventsTable, row -> {
            GuiModel.EventRow event = model.events.get(row);
            return new String[] {event.kind() + " " + event.player(), StatCard.forEvent(event)};
        });
        attachMenu(eventsTable, List.of(
            new MenuEntry("Show code for this", (row, column) -> {
                GuiModel.EventRow event = model.events.get(row);
                StatEventKind kind = event.event().getKind() == StatEventKind.UNRECOGNIZED ? Examples.Selection.defaults().eventKind() : event.event().getKind();
                showCode(new CodeTab.Request(Examples.Scenario.REACT_TO_EVENT, Examples.Selection.defaults().withEventKind(kind).withPlayer(event.player())));
            }, (row, column) -> true),
            new MenuEntry("Inspect object", (row, column) -> inspect(model.events.get(row).event().getObjectId()), (row, column) -> model.events.get(row).event().getObjectId() != 0),
            new MenuEntry("Inspect killer", (row, column) -> inspect(model.events.get(row).event().getKillerObjectId()), (row, column) -> model.events.get(row).event().getKillerObjectId() != 0)));
        // The filter object holds the three criteria; the controls below update it and ask the sorter to refilter.
        @SuppressWarnings("unchecked")
        TableRowSorter<TableModel> sorter = (TableRowSorter<TableModel>) eventsTable.getRowSorter();

        JComboBox<String> kindBox = new JComboBox<>();
        kindBox.addItem("All kinds");
        for (StatEventKind kind : StatEventKind.values()) {
            if (kind != StatEventKind.UNRECOGNIZED) {
                kindBox.addItem(Format.strip(kind.name(), "STATEVENT_"));
            }
        }
        playerFilterBox = new JComboBox<>(new PlayerChoice[] {PlayerChoice.ALL});
        JTextField textBox = new JTextField(18);
        Runnable apply = () -> {
            eventFilter.setKind(kindBox.getSelectedIndex() <= 0 ? null : StatEventKind.valueOf("STATEVENT_" + kindBox.getSelectedItem()));
            PlayerChoice player = (PlayerChoice) playerFilterBox.getSelectedItem();
            eventFilter.setPlayer(player == null ? null : player.index());
            eventFilter.setText(textBox.getText());
            sorter.setRowFilter(eventFilter.asRowFilter());
        };
        kindBox.addActionListener(e -> apply.run());
        playerFilterBox.addActionListener(e -> apply.run());
        textBox.getDocument().addDocumentListener(new DocumentListener() {
            @Override
            public void insertUpdate(DocumentEvent e) {
                apply.run();
            }

            @Override
            public void removeUpdate(DocumentEvent e) {
                apply.run();
            }

            @Override
            public void changedUpdate(DocumentEvent e) {
                apply.run();
            }
        });

        JPanel bar = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 4));
        bar.add(new JLabel("Kind:"));
        bar.add(kindBox);
        bar.add(new JLabel("Player:"));
        bar.add(playerFilterBox);
        bar.add(new JLabel("Text:"));
        bar.add(textBox);
        bar.add(new JLabel("Rows from the last " + GuiModel.RECENT_EVENT_SECONDS + " game seconds are highlighted. Double-click for details."));

        JPanel panel = new JPanel(new BorderLayout());
        panel.add(bar, BorderLayout.NORTH);
        panel.add(followNewest(eventsTable), BorderLayout.CENTER);
        return panel;
    }

    /** An append-only table that follows new rows until the user scrolls up. */
    private JScrollPane logTab(TableModel tableModel, String hint, Map<String, Integer> widths) {
        GuiTables.HintTable table = GuiTables.table(tableModel, hint, widths, GuiTables.Sorting.NONE);
        return followNewest(table);
    }

    /** Scrolls to the last row after an append, once per burst of rows, unless the user has scrolled up. */
    private static JScrollPane followNewest(GuiTables.HintTable table) {
        JScrollPane scroll = new JScrollPane(table);
        boolean[] scrollPending = {false};
        table.getModel().addTableModelListener(e -> {
            if (e.getType() != TableModelEvent.INSERT || scrollPending[0]) {
                return;
            }
            // This listener runs before the table lays the new row out, so the bar still shows the old end.
            JScrollBar bar = scroll.getVerticalScrollBar();
            boolean atEnd = bar.getValue() + bar.getModel().getExtent() >= bar.getMaximum() - 4;
            if (atEnd) {
                scrollPending[0] = true;
                SwingUtilities.invokeLater(() -> {
                    scrollPending[0] = false;
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
        GuiTables.HintTable table = GuiTables.table(model.lobbySlots, "Not in a game lobby. The slots appear when you join or host one.",
            Map.of("Name", 130, "Side", 180, "Random", 100, "Color", 46, "Predicted color", 90, "Predicted start", 100), GuiTables.Sorting.BY_HEADER);
        GuiTables.useRenderer(table, "Color", new GuiTables.SwatchRenderer());
        GuiTables.useRenderer(table, "Predicted color", new GuiTables.SwatchRenderer());
        lobbyTable = table;
        attachMenu(table, List.of(
            new MenuEntry("Open slot", (row, column) -> lobbyPanel.open(model.lobbySlots.get(row).slot()), (row, column) -> !isHuman(row)),
            new MenuEntry("Close slot", (row, column) -> lobbyPanel.close(model.lobbySlots.get(row).slot()), (row, column) -> !isHuman(row)),
            new MenuEntry("Kick player", (row, column) -> lobbyPanel.kick(model.lobbySlots.get(row).slot()), (row, column) -> isHuman(row)),
            new MenuEntry("Add easy AI", (row, column) -> lobbyPanel.addAi(model.lobbySlots.get(row).slot(), 0), (row, column) -> !isHuman(row)),
            new MenuEntry("Add medium AI", (row, column) -> lobbyPanel.addAi(model.lobbySlots.get(row).slot(), 1), (row, column) -> !isHuman(row)),
            new MenuEntry("Add hard AI", (row, column) -> lobbyPanel.addAi(model.lobbySlots.get(row).slot(), 2), (row, column) -> !isHuman(row)),
            new MenuEntry("Show code for this", (row, column) -> showCode(new CodeTab.Request(Examples.Scenario.LOBBY, Examples.Selection.defaults())), (row, column) -> true)));
        table.getSelectionModel().addListSelectionListener(e -> {
            int viewRow = table.getSelectedRow();
            if (!e.getValueIsAdjusting() && viewRow >= 0) {
                lobbyPanel.selectSlot(model.lobbySlots.get(table.convertRowIndexToModel(viewRow)).slot());
            }
        });
        for (String blankable : List.of("Team", "Start pos", "Predicted start")) {
            GuiTables.useRenderer(table, blankable, new GuiTables.BlankRenderer(false));
        }

        predictionLabel.setOpaque(true);
        predictionLabel.setFont(predictionLabel.getFont().deriveFont(Font.BOLD, 14f));
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
        panel.add(lobbyPanel, BorderLayout.SOUTH);
        return panel;
    }

    private JTable lobbyTable;

    JTable lobbyTable() {
        return lobbyTable;
    }

    private boolean isHuman(int modelRow) {
        return model.lobbySlots.get(modelRow).data().getState() == LobbySlotState.LOBBY_SLOT_HUMAN;
    }

    private JSplitPane matchTab() {
        GuiTables.HintTable info = GuiTables.table(model.matchInfo, "Waiting for a MatchInfo frame.", Map.of("Key", 200, "Value", 700), GuiTables.Sorting.NONE);
        GuiTables.HintTable observers = GuiTables.table(model.observers, "No observers in this match.", Map.of("Name", 160), GuiTables.Sorting.NONE);
        GuiTables.HintTable summary = GuiTables.table(model.summary, "Filled at the score screen when the match ends.",
            Map.of("Player", 130, "Side", 90, "Buildings destroyed", 120), GuiTables.Sorting.BY_HEADER);
        GuiTables.useRenderer(observers, "ELO", new GuiTables.BlankRenderer(false));
        GuiTables.HintTable hello = GuiTables.table(model.helloInfo, "Waiting for the Hello of the game.", Map.of("Key", 130, "Value", 420), GuiTables.Sorting.NONE);
        JScrollPane infoScroll = new JScrollPane(info);
        infoScroll.setBorder(BorderFactory.createTitledBorder("Match info"));
        JScrollPane helloScroll = new JScrollPane(hello);
        helloScroll.setBorder(BorderFactory.createTitledBorder("The game (its Hello)"));
        JSplitPane top = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, infoScroll, helloScroll);
        top.setResizeWeight(0.55);
        top.setPreferredSize(new Dimension(400, 280));
        JScrollPane middle = new JScrollPane(observers);
        middle.setBorder(BorderFactory.createTitledBorder("Observers"));
        middle.setPreferredSize(new Dimension(400, 110));
        JScrollPane bottom = new JScrollPane(summary);
        bottom.setBorder(BorderFactory.createTitledBorder("Match summary (sent when the match ends)"));
        JSplitPane lower = new JSplitPane(JSplitPane.VERTICAL_SPLIT, middle, bottom);
        lower.setResizeWeight(0.35);
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, top, lower);
        split.setResizeWeight(0.5);
        return split;
    }

    // -- "Show code for this".

    private JScrollPane chatTab() {
        GuiTables.HintTable table = GuiTables.table(model.chat, "No chat lines received yet.", Map.of("Text", 500), GuiTables.Sorting.NONE);
        attachShowCode(table, (row, column) -> {
            GuiModel.ChatRow line = model.chat.get(row);
            Examples.Selection selection = Examples.Selection.defaults();
            try {
                selection = selection.withChannel(ChatChannel.valueOf("CHAT_CHANNEL_" + line.channel()));
                if (!line.audience().isEmpty()) {
                    selection = selection.withAudience(com.generalsonline.stats.proto.ChatAudience.valueOf("CHAT_AUDIENCE_" + line.audience()));
                }
            } catch (IllegalArgumentException e) {
                // a channel or audience name this client does not know: keep the defaults
            }
            return new CodeTab.Request(Examples.Scenario.SEND_CHAT, selection);
        });
        return followNewest(table);
    }

    private JScrollPane framesTab() {
        GuiTables.HintTable table = GuiTables.table(model.frames, "No frames received yet.", Map.of("Summary", 800, "Type", 150, "Received", 100), GuiTables.Sorting.NONE);
        attachShowCode(table, (row, column) -> new CodeTab.Request(Examples.Scenario.PRINT_FRAMES, Examples.Selection.defaults()));
        return followNewest(table);
    }

    /** Adds a context menu item "Show code for this" to a table; the function maps the clicked model row and column to a request. */
    private void attachShowCode(JTable table, BiFunction<Integer, Integer, CodeTab.Request> request) {
        attachMenu(table, List.of(new MenuEntry("Show code for this", (row, column) -> showCode(request.apply(row, column)), (row, column) -> true)));
    }

    private record TableMenu(int[] target, List<JMenuItem> items, List<MenuEntry> entries) {
    }

    private final Map<JTable, TableMenu> menus = new java.util.HashMap<>();

    /** For the tests: points the menu of a table at a model row, as a right click would, and returns the item with this label (enabled or not as the row decides). */
    JMenuItem popupItem(JTable table, int modelRow, String label) {
        TableMenu menu = menus.get(table);
        menu.target()[0] = modelRow;
        menu.target()[1] = 0;
        JMenuItem found = null;
        for (int i = 0; i < menu.entries().size(); i++) {
            menu.items().get(i).setEnabled(menu.entries().get(i).enabled().test(modelRow, 0));
            if (menu.entries().get(i).label().equals(label)) {
                found = menu.items().get(i);
            }
        }
        return found;
    }

    /** One entry of a table's context menu: it acts on the clicked model row and column, and is greyed out where {@code enabled} says no. */
    private record MenuEntry(String label, BiConsumer<Integer, Integer> action, BiPredicate<Integer, Integer> enabled) {
    }

    /** Adds a context menu to a table; the entries get the clicked model row and column. */
    private void attachMenu(JTable table, List<MenuEntry> entries) {
        JPopupMenu menu = new JPopupMenu();
        int[] target = {-1, -1};
        List<JMenuItem> items = new ArrayList<>();
        menus.put(table, new TableMenu(target, items, entries));
        for (MenuEntry entry : entries) {
            JMenuItem item = new JMenuItem(entry.label());
            item.addActionListener(e -> {
                if (target[0] >= 0 && target[0] < table.getModel().getRowCount()) {
                    entry.action().accept(target[0], target[1]);
                }
            });
            menu.add(item);
            items.add(item);
        }
        table.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                popup(e);
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                popup(e);
            }

            private void popup(MouseEvent e) {
                if (!e.isPopupTrigger()) {
                    return;
                }
                int viewRow = table.rowAtPoint(e.getPoint());
                if (viewRow < 0) {
                    return;
                }
                int viewColumn = table.columnAtPoint(e.getPoint());
                table.setRowSelectionInterval(viewRow, viewRow);
                target[0] = table.convertRowIndexToModel(viewRow);
                target[1] = viewColumn < 0 ? 0 : table.convertColumnIndexToModel(viewColumn);
                for (int i = 0; i < entries.size(); i++) {
                    items.get(i).setEnabled(entries.get(i).enabled().test(target[0], target[1]));
                }
                menu.show(table, e.getX(), e.getY());
            }
        });
    }

    /** The same menu for a component that is not a table: one fixed request. */
    private void attachShowCode(JComponent component, Supplier<CodeTab.Request> request) {
        JPopupMenu menu = new JPopupMenu();
        JMenuItem item = new JMenuItem("Show code for this");
        item.addActionListener(e -> showCode(request.get()));
        menu.add(item);
        component.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                popup(e);
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                popup(e);
            }

            private void popup(MouseEvent e) {
                if (e.isPopupTrigger()) {
                    menu.show(component, e.getX(), e.getY());
                }
            }
        });
    }

    private void showCode(CodeTab.Request request) {
        codeTab.show(request);
        tabs.setSelectedComponent(codeTab);
    }

    // -- The panels that ask the game.

    /** Connects the menus and buttons of the Query, Camera, In view and Director tabs to each other and to the code generator. */
    private void wireControlPanels() {
        queryTab.onFollow(id -> {
            tabs.setSelectedComponent(cameraTab);
            cameraTab.followObject(id);
        });
        cameraTab.inspectedObject(queryTab::lastObjectId);
        minimapTab.onFeedConfig(queryTab::showFeed);
        attachMenu(queryTab.catalogTable(), List.of(
            new MenuEntry("Show code for this", (row, column) -> {
                CatalogEntry entry = queryTab.catalogEntry(row);
                showCode(new CodeTab.Request(Examples.Scenario.QUERY_CATALOG, Examples.Selection.defaults().withTemplate(entry.getName(), entry.getSide())));
            }, (row, column) -> true),
            new MenuEntry("Look up this name", (row, column) -> queryTab.lookUp(queryTab.catalogEntry(row).getName(), ""), (row, column) -> true)));
        attachShowCode(queryTab.objectArea(), () -> new CodeTab.Request(Examples.Scenario.INSPECT_OBJECT,
            Examples.Selection.defaults().withObjectId(queryTab.lastObjectId() != 0 ? queryTab.lastObjectId() : Examples.Selection.defaults().objectId())));
        attachMenu(viewTab.notableTable(), List.of(
            new MenuEntry("Inspect object", (row, column) -> inspect(viewTab.rows().notableRow(row).objectId()), (row, column) -> true),
            new MenuEntry("Show code for this", (row, column) -> showCode(new CodeTab.Request(Examples.Scenario.INSPECT_OBJECT,
                Examples.Selection.defaults().withObjectId(viewTab.rows().notableRow(row).objectId()))), (row, column) -> true)));
        attachShowCode(viewTab.playersTable(), (row, column) -> new CodeTab.Request(Examples.Scenario.VIEW_STATS, Examples.Selection.defaults()));
        attachShowCode(viewTab.weaponsTable(), (row, column) -> new CodeTab.Request(Examples.Scenario.VIEW_STATS, Examples.Selection.defaults()));
        attachShowCode(cameraTab.cameraSection(), () -> new CodeTab.Request(Examples.Scenario.CAMERA_CONTROL, cameraTab.cameraSelection()));
        attachShowCode(cameraTab.replaySection(), () -> new CodeTab.Request(Examples.Scenario.REPLAY_CONTROL, cameraTab.replaySelection()));
        attachShowCode(directorTab, () -> new CodeTab.Request(Examples.Scenario.DIRECTOR, directorTab.selection()));
        attachShowCode(lobbyPanel, () -> new CodeTab.Request(Examples.Scenario.CONTROL_LOBBY, Examples.Selection.defaults()));
    }

    /** Shows the Query tab and asks the game about an object; 0 is no object and does nothing. */
    private void inspect(int objectId) {
        if (objectId != 0) {
            tabs.setSelectedComponent(queryTab);
            queryTab.inspect(objectId);
        }
    }

    /** The panels that read the library state (view statistics, camera, interest, director) refresh from the same timer as the labels. */
    private void refreshPanels() {
        queryTab.refreshPlayers();
        cameraTab.refresh();
        viewTab.refresh();
        minimapTab.refresh();
        directorTab.refresh();
        lobbyPanel.refresh();
    }

    // -- For the tests: the pieces, and a way to attach a connection without opening a window.

    EdtFeed feed() {
        return feed;
    }

    /** Uses this connection for the panels' requests; the caller builds it with {@link #feed()} as its listener. */
    void connectTo(StatsPipe connection) {
        this.pipe = connection;
    }

    QueryTab queryTab() {
        return queryTab;
    }

    CameraTab cameraTab() {
        return cameraTab;
    }

    LobbyControlPanel lobbyPanel() {
        return lobbyPanel;
    }

    ViewStatsTab viewTab() {
        return viewTab;
    }

    MinimapTab minimapTab() {
        return minimapTab;
    }

    DirectorTab directorTab() {
        return directorTab;
    }

    DirectorModel directorModel() {
        return directorModel;
    }

    CodeTab codeTab() {
        return codeTab;
    }

    JTabbedPane tabs() {
        return tabs;
    }

    GuiTables.HintTable eventsTable() {
        return eventsTable;
    }

    /** Runs the timers' work once; tests call it instead of waiting for the timers. */
    void refreshAll() {
        feed.applyCamera();
        refreshLabels();
        refreshPanels();
    }

    // -- Stat cards.

    /** Opens a card on double click; the function maps a model row to {title, text}. */
    private void openCardOnDoubleClick(JTable table, IntFunction<String[]> card) {
        table.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() != 2) {
                    return;
                }
                int viewRow = table.rowAtPoint(e.getPoint());
                if (viewRow >= 0) {
                    String[] content = card.apply(table.convertRowIndexToModel(viewRow));
                    showCard(content[0], content[1]);
                }
            }
        });
    }

    private void showCard(String title, String text) {
        JTextArea area = new JTextArea(text);
        area.setEditable(false);
        area.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
        area.setCaretPosition(0);
        JDialog dialog = new JDialog(frame, title, false);
        dialog.add(new JScrollPane(area));
        dialog.setSize(680, 720);
        dialog.setLocationRelativeTo(frame);
        dialog.setVisible(true);
    }

    // -- Status.

    private void refreshLabels() {
        connectionLabel.setText(model.connectionLine());
        gameLabel.setText(model.gameLine());
        gameLabel.setForeground(model.droppedFrames() > 0 ? new Color(0xB71C1C) : null);
        matchLabel.setText(model.matchLine());
        progressLabel.setText(model.progressLine());
        cameraLabel.setText(model.cameraLine());
        lobbyHeaderLabel.setText(model.lobbyHeader());
        if (frame != null) {
            String title = model.windowTitle();
            if (!title.equals(frame.getTitle())) {
                frame.setTitle(title);
            }
        }

        String stale = model.staleText();
        staleLabel.setText(stale == null ? " " : stale);
        String result = model.resultBanner();
        resultLabel.setText(result == null ? " " : result);
        Color foreground = model.isStale() ? Color.GRAY : defaultTableForeground;
        if (playersTable != null && foreground != null) {
            playersTable.setForeground(foreground);
            eventsTable.setForeground(foreground);
        }
        if (eventsTable != null) {
            eventsTable.repaint(); // the "recent" highlight moves with game time
        }
        refreshPlayerChoices();
        gameLabel.setToolTipText(model.state().hello().map(h -> "<html>" + h.getGameBuild() + "<br>" + h.getCapabilitiesCount() + " capabilities: " + String.join(", ", h.getCapabilitiesList()) + "</html>").orElse(null));

        Boolean ok = model.predictionCheckOk();
        predictionLabel.setText(ok == null ? " " : model.predictionCheckText());
        predictionLabel.setBackground(ok == null ? null : ok ? new Color(0xC8E6C9) : new Color(0xFFCDD2));
        predictionLabel.setForeground(ok == null ? null : ok ? new Color(0x1B5E20) : new Color(0xB71C1C));
    }

    /** The entries of the Events player filter follow the players of the current match. */
    private void refreshPlayerChoices() {
        if (playerFilterBox == null) {
            return;
        }
        StringBuilder signature = new StringBuilder();
        for (MatchPlayer p : model.matchPlayers()) {
            signature.append(p.getPlayerIndex()).append(':').append(p.getName()).append(';');
        }
        if (signature.toString().equals(playerChoicesSignature)) {
            return;
        }
        playerChoicesSignature = signature.toString();
        PlayerChoice selected = (PlayerChoice) playerFilterBox.getSelectedItem();
        List<PlayerChoice> choices = new ArrayList<>(List.of(PlayerChoice.ALL));
        for (MatchPlayer p : model.matchPlayers()) {
            choices.add(new PlayerChoice(p.getPlayerIndex(), p.getName()));
        }
        playerFilterBox.setModel(new javax.swing.DefaultComboBoxModel<>(choices.toArray(new PlayerChoice[0])));
        if (selected != null) {
            for (PlayerChoice choice : choices) {
                if (java.util.Objects.equals(choice.index(), selected.index())) {
                    playerFilterBox.setSelectedItem(choice);
                }
            }
        }
    }

    private void refreshFeedHealth() {
        FeedHealth.Sample sample = health.sample(System.nanoTime());
        footerLabel.setText(FeedHealth.text(sample));
        footerLabel.setForeground(FeedHealth.isStale(sample) ? new Color(0xB71C1C) : null);
    }

    /** One entry of the Events player filter; a null index means all players. */
    record PlayerChoice(Integer index, String label) {
        static final PlayerChoice ALL = new PlayerChoice(null, "All players");

        @Override
        public String toString() {
            return label;
        }
    }
}
