package com.generalsonline.stats.app;

import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.app.Theme.Tone;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.StatEventKind;

import javax.swing.BorderFactory;
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
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.Timer;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import javax.swing.event.TableModelEvent;
import javax.swing.table.TableColumn;
import javax.swing.table.TableModel;
import javax.swing.table.TableRowSorter;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.HeadlessException;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
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
 * <p>The window is a header strip (connection, screen, match, game time, speed, dropped frames), a navigation rail with the
 * sections in groups, the page of the open section and a footer with the feed health. The look is {@link Theme} and
 * {@link Ui}. Everything that is not layout lives in {@link GuiModel}; table pieces are in {@link GuiTables}.
 */
public final class StatsGui {
    private final GuiModel model = new GuiModel();
    private final FeedHealth health = new FeedHealth();
    private final EdtFeed feed = new EdtFeed(model, health, SwingUtilities::invokeLater);
    private final EventFilter eventFilter = new EventFilter();
    private final HeaderBar header = new HeaderBar(new HeaderBar.Actions() {
        @Override
        public void record() {
            startRecording();
        }

        @Override
        public void stopRecording() {
            StatsGui.this.stopRecording();
        }

        @Override
        public void openRecording() {
            chooseRecording();
        }

        @Override
        public void openRecording(Path file) {
            StatsGui.this.openRecording(file, 1.0);
        }

        @Override
        public void showFolder() {
            StatsGui.this.showFolder();
        }
    });
    private final Banner staleBanner = new Banner(Tone.BAD);
    private final Banner resultBanner = new Banner(Tone.OK);
    private final JLabel footerLabel = new JLabel(" ");
    private final JLabel progressLabel = new JLabel(" ");
    private final JLabel cameraLabel = new JLabel(" ");
    private final TeamSummary teamSummary = new TeamSummary();
    private final PlaybackBar playbackBar = new PlaybackBar(new PlaybackBar.Actions() {
        @Override
        public void togglePlay() {
            togglePlayback();
        }

        @Override
        public void speed(double speed) {
            if (player != null) {
                player.setSpeed(speed);
            }
        }

        @Override
        public void backToLive() {
            StatsGui.this.backToLive();
        }
    });
    private SettingsTab settingsTab;
    private boolean wasLive;
    private Recorder recorder;
    private long savedUntilNanos;
    private RecordingPlayer player;
    private LiveListener liveGate;
    private int livePid = -1;
    private String livePipe;
    private final Ui.Pill lobbyKindPill = new Ui.Pill(" ");
    private final JLabel lobbyMapLabel = new JLabel(" ");
    private final JLabel lobbyHostLabel = Ui.caption(" ");
    private final JLabel lobbyFlagsLabel = Ui.caption(" ");
    private final Ui.Pill predictionLabel = new Ui.Pill(" ");
    private final Minimap minimap = new Minimap(model);
    private volatile StatsPipe pipe;
    private final PipeCommands commands = new PipeCommands(() -> pipe, SwingUtilities::invokeLater);
    private final MinimapTab minimapTab = new MinimapTab(model, minimap, commands);
    private final QueryTab queryTab = new QueryTab(commands, model);
    private final CameraTab cameraTab = new CameraTab(commands, model);
    private final LobbyControlPanel lobbyPanel = new LobbyControlPanel(commands, model);
    private final ViewStatsTab viewTab = new ViewStatsTab(model);
    private final DirectorModel directorModel = new DirectorModel(model.state(), () -> pipe);
    private final DirectorTab directorTab = new DirectorTab(directorModel);
    private final CodeTab codeTab = new CodeTab();
    private final ControlTab controlTab = new ControlTab(commands, model, directorTab);
    private final ChatTab chatTab = new ChatTab(model, () -> pipe);
    private final NavRail nav = new NavRail();
    private JFrame frame;
    private GuiTables.HintTable playersTable;
    private GuiTables.HintTable eventsTable;
    private JComboBox<PlayerChoice> playerFilterBox;
    private String playerChoicesSignature = "";

    StatsGui() {
        model.setNewMatchHook(feed::clearCamera);
    }

    /**
     * Opens the window and returns; the JVM then lives until the window is closed.
     *
     * @param pid the game process to connect to, or a negative number for any game
     * @param pipe an explicit pipe path, or null
     * @param replayFile if not null, feed this recorded dump instead of connecting to a game
     * @param replaySpeed pace for a replay as a multiplier of game time (1.0 is real time); 0 plays as fast as possible
     */
    public static void run(int pid, String pipe, Path replayFile, double replaySpeed) {
        Theme.install();
        JFrame.setDefaultLookAndFeelDecorated(true);
        SwingUtilities.invokeLater(() -> {
            try {
                new StatsGui().open(pid, pipe, replayFile, replaySpeed);
            } catch (HeadlessException e) {
                System.err.println("the gui command needs a display");
                System.exit(1);
            }
        });
    }

    private void open(int pid, String pipe, Path replayFile, double replaySpeed) {
        frame = new JFrame(model.windowTitle());
        frame.getRootPane().putClientProperty("JRootPane.titleBarShowIcon", false);
        frame.setDefaultCloseOperation(JFrame.DO_NOTHING_ON_CLOSE);
        frame.setSize(1380, 840);
        frame.setMinimumSize(new Dimension(960, 620));
        frame.setLocationByPlatform(true);
        frame.setContentPane(buildContent());
        startTimers();

        livePid = pid >= 0 ? pid : SettingsTab.savedPid();
        livePipe = pipe != null ? pipe : Prefs.get("pipe", "").isEmpty() ? null : Prefs.get("pipe", "");
        if (replayFile != null) {
            openRecording(replayFile, replaySpeed);
        } else if (Prefs.getBoolean("autoConnect", true) || pid >= 0 || pipe != null) {
            startLive();
        }

        frame.addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                directorModel.stop();
                stopRecording();
                stopPlayer();
                stopLive();
                frame.dispose();
                System.exit(0);
            }
        });
        frame.setVisible(true);
    }

    // -- The sources of the window: the live game, or a recording.

    /** Passes what a live pipe says on to the feed until the window switches away from it, so a late frame of the old source never lands in the new one. */
    private static final class LiveListener implements com.generalsonline.stats.StatsListener {
        private final EdtFeed feed;
        private volatile boolean closed;

        LiveListener(EdtFeed feed) {
            this.feed = feed;
        }

        void close() {
            closed = true;
        }

        @Override
        public void onFrame(com.generalsonline.stats.DecodedFrame frame) {
            if (!closed) {
                feed.onFrame(frame);
            }
        }

        @Override
        public void onConnected(String pipePath) {
            if (!closed) {
                feed.onConnected(pipePath);
            }
        }

        @Override
        public void onDisconnected(Throwable cause) {
            if (!closed) {
                feed.onDisconnected(cause);
            }
        }

        @Override
        public void onError(int type, Throwable cause) {
            if (!closed) {
                feed.onError(type, cause);
            }
        }

        @Override
        public void onFramesDropped(long count) {
            if (!closed) {
                feed.onFramesDropped(count);
            }
        }
    }

    /** Connects to the running game; several may run, so the pipe tries the highest pid first and moves on when a pipe is busy. */
    private void startLive() {
        liveGate = new LiveListener(feed);
        StatsPipe.Builder builder = StatsPipe.builder().listener(liveGate);
        if (livePipe != null) {
            builder.pipe(livePipe);
        } else if (livePid >= 0) {
            builder.pid(livePid);
        }
        pipe = builder.build().start();
    }

    /** Connects again with the connection settings as they are saved now. */
    private void reconnect() {
        stopRecording();
        stopPlayer();
        stopLive();
        feed.flush();
        model.resetForSource();
        livePid = SettingsTab.savedPid();
        livePipe = Prefs.get("pipe", "").isEmpty() ? null : Prefs.get("pipe", "");
        startLive();
    }

    private void stopLive() {
        if (liveGate != null) {
            liveGate.close();
            liveGate = null;
        }
        StatsPipe closing = pipe;
        pipe = null;
        if (closing != null) {
            closing.close();
        }
    }

    private void stopPlayer() {
        if (player != null) {
            player.stop();
            player = null;
        }
        playbackBar.show(" ", false);
    }

    /** Plays a recording in this window; the live game is let go until "Back to live". */
    void openRecording(Path file, double speed) {
        stopRecording();
        stopPlayer();
        stopLive();
        feed.flush();
        model.resetForSource();
        model.setConnection("replaying " + file);
        player = new RecordingPlayer(file, feed, speed);
        playbackBar.setSpeed(speed);
        playbackBar.show(file.getFileName().toString().replaceAll("\\.dump$", ""), true);
        player.start();
    }

    private void backToLive() {
        stopPlayer();
        feed.flush();
        model.resetForSource();
        startLive();
    }

    private void togglePlayback() {
        if (player == null) {
            return;
        }
        if (player.isFinished()) {
            openRecording(player.file(), player.speed());
        } else if (player.isPaused()) {
            player.resume();
        } else {
            player.pause();
        }
    }

    private void chooseRecording() {
        javax.swing.JFileChooser chooser = new javax.swing.JFileChooser(Files.isDirectory(Recorder.folder()) ? Recorder.folder().toFile() : null);
        chooser.setDialogTitle("Open recording");
        chooser.setFileFilter(new javax.swing.filechooser.FileNameExtensionFilter("Recordings (*.dump)", "dump"));
        if (chooser.showOpenDialog(frame) == javax.swing.JFileChooser.APPROVE_OPTION) {
            openRecording(chooser.getSelectedFile().toPath(), 1.0);
        }
    }

    private void startRecording() {
        if (recorder != null && recorder.isOpen()) {
            return;
        }
        try {
            recorder = Recorder.start(model.state(), model.mapTitle());
            feed.setRecorder(recorder::accept);
        } catch (IOException e) {
            javax.swing.JOptionPane.showMessageDialog(frame, "The recording could not be started: " + Format.cause(e), "Record", javax.swing.JOptionPane.ERROR_MESSAGE);
        }
    }

    private void stopRecording() {
        if (recorder != null && recorder.isOpen()) {
            feed.setRecorder(null);
            recorder.close();
            savedUntilNanos = System.nanoTime() + 10_000_000_000L;
        }
    }

    private void showFolder() {
        try {
            Files.createDirectories(Recorder.folder());
            java.awt.Desktop.getDesktop().open(Recorder.folder().toFile());
        } catch (IOException | RuntimeException e) {
            javax.swing.JOptionPane.showMessageDialog(frame, "The folder is " + Recorder.folder(), "Recordings", javax.swing.JOptionPane.INFORMATION_MESSAGE);
        }
    }

    GuiModel model() {
        return model;
    }

    /** The whole window content. Package-private so the window can be built and painted without opening it. */
    JComponent buildContent() {
        Theme.install();
        JPanel banners = new JPanel();
        banners.setLayout(new BoxLayout(banners, BoxLayout.Y_AXIS));
        banners.setOpaque(false);
        banners.add(playbackBar);
        banners.add(staleBanner);
        banners.add(resultBanner);
        JPanel top = new JPanel(new BorderLayout());
        top.add(header, BorderLayout.NORTH);
        top.add(banners, BorderLayout.CENTER);

        JPanel footer = new JPanel(new BorderLayout(24, 0));
        footer.setBackground(Theme.BG);
        footer.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.HAIRLINE), BorderFactory.createEmptyBorder(6, 14, 6, 14)));
        for (JLabel label : new JLabel[] {footerLabel, progressLabel, cameraLabel}) {
            label.setFont(Ui.small());
            label.setForeground(Theme.DIM);
        }
        JPanel footerRight = Ui.line(progressLabel, javax.swing.Box.createHorizontalStrut(16), cameraLabel);
        footer.add(footerLabel, BorderLayout.WEST);
        footer.add(footerRight, BorderLayout.EAST);

        attachShowCode(minimap, () -> new CodeTab.Request(Examples.Scenario.CAMERA_BOX, Examples.Selection.defaults()));
        wireControlPanels();
        nav.group("Live");
        nav.add("Players", playersTab());
        nav.add("Minimap", minimapTab);
        nav.add("In view", viewTab);
        nav.group("Feed");
        nav.add("Events", eventsTab());
        nav.add("Chat", chatTab);
        nav.add("Frames", framesTab());
        nav.group("Match");
        nav.add("Lobby", lobbyTab());
        nav.add("Match", matchTab());
        nav.group("Control");
        nav.add("Control", controlTab);
        nav.add("Camera", cameraTab);
        nav.add("Director", directorTab);
        nav.add("Query", queryTab);
        nav.group("Develop");
        nav.add("Code", codeTab);
        nav.group("App");
        settingsTab = new SettingsTab(queryTab, model, java.util.Arrays.asList("Players", "Minimap", "In view", "Events", "Chat", "Frames", "Lobby", "Match", "Control", "Camera", "Director", "Query", "Code"), this::reconnect, chatTab::reloadCanned);
        nav.add("Settings", settingsTab);
        nav.select("Players");
        nav.select(Prefs.get("startSection", "Players"));

        JPanel root = new JPanel(new BorderLayout());
        root.setBackground(Theme.SURFACE);
        root.add(top, BorderLayout.NORTH);
        root.add(nav, BorderLayout.CENTER);
        root.add(footer, BorderLayout.SOUTH);
        installShortcuts(root);
        refreshLabels();
        return root;
    }

    /** True while a text box, a combo box or a spinner has the focus: the shortcuts keep out of its way. */
    private static boolean typing() {
        java.awt.Component owner = java.awt.KeyboardFocusManager.getCurrentKeyboardFocusManager().getFocusOwner();
        return owner instanceof javax.swing.text.JTextComponent || owner instanceof javax.swing.JComboBox || owner instanceof javax.swing.JSpinner;
    }

    private void shortcut(JComponent root, String name, int key, int modifiers, Runnable action) {
        root.getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(javax.swing.KeyStroke.getKeyStroke(key, modifiers), name);
        root.getActionMap().put(name, new javax.swing.AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                if (!typing()) {
                    action.run();
                }
            }
        });
    }

    /** The keys of the Control section, and a few of the window; they work while this window has the focus. */
    private void installShortcuts(JComponent root) {
        int alt = java.awt.event.InputEvent.ALT_DOWN_MASK;
        int ctrl = java.awt.event.InputEvent.CTRL_DOWN_MASK;
        shortcut(root, "pause", java.awt.event.KeyEvent.VK_SPACE, 0, controlTab::togglePause);
        shortcut(root, "slower", java.awt.event.KeyEvent.VK_LEFT, alt, controlTab::slower);
        shortcut(root, "faster", java.awt.event.KeyEvent.VK_RIGHT, alt, controlTab::faster);
        shortcut(root, "fast-forward", java.awt.event.KeyEvent.VK_F, 0, controlTab::toggleFastForward);
        shortcut(root, "record", java.awt.event.KeyEvent.VK_R, ctrl, () -> {
            if (recorder != null && recorder.isOpen()) {
                stopRecording();
            } else if (player == null && pipe != null) {
                startRecording();
            }
        });
        for (int n = 0; n <= 9; n++) {
            int number = n;
            if (n <= 8) {
                shortcut(root, "observe" + n, java.awt.event.KeyEvent.VK_0 + n, 0, () -> controlTab.observeNumber(number));
            }
            if (n >= 1 && n <= 8) {
                shortcut(root, "fly" + n, java.awt.event.KeyEvent.VK_0 + n, alt, () -> controlTab.flyTo(number - 1));
                shortcut(root, "save" + n, java.awt.event.KeyEvent.VK_0 + n, alt | ctrl, () -> controlTab.saveBookmark(number - 1));
            }
            if (n >= 1) {
                shortcut(root, "section" + n, java.awt.event.KeyEvent.VK_0 + n, ctrl, () -> {
                    java.util.List<String> titles = nav.titles();
                    if (number - 1 < titles.size()) {
                        nav.select(titles.get(number - 1));
                    }
                });
            }
        }
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
            Map.of("Name", 210, "Production", 230, "Superweapons", 200, "Net (latency / fps / ping)", 230, "Power (prod / use)", 110, "Heroes", 100,
                "Kills (inf/veh/air/str)", 150, "Losses (inf/veh/air/str)", 150, "Color", 46),
            GuiTables.Sorting.BY_HEADER);
        GuiTables.useRenderer(playersTable, "Color", new GuiTables.SwatchRenderer());
        for (String blankable : List.of("Team", "ELO", "Latency ms", "Display FPS", "Ping ms")) {
            GuiTables.useRenderer(playersTable, blankable, new GuiTables.BlankRenderer(false));
        }
        // Stable order by team, then lobby slot, however the live updates reorder the rows.
        GuiTables.sortBy(playersTable, "Team", "Slot");
        // The cells show shortened names; the tooltip has the raw ones.
        playersTable.setTooltips((row, column) -> {
            var stats = model.players.get(row).stats();
            switch (model.players.getColumnName(column)) {
                case "Production":
                    return CellText.productionDetail(stats);
                case "Superweapons":
                    return CellText.superweaponDetail(stats);
                case "Net (latency / fps / ping)":
                    return "latency: this player's average frame round trip, FPS: this player's display FPS (both as the engine measures them; known for all players only on the packet router),\nping: Generals Online mesh round trip from this PC";
                case "Team":
                    return "Shown from 1; blank when the lobby did not set one";
                default:
                    return null;
            }
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
        List<String> viewNames = new ArrayList<>();
        for (ColumnViews.View view : ColumnViews.PLAYERS) {
            viewNames.add(view.name());
        }
        Ui.TabStrip viewTabs = new Ui.TabStrip(viewNames, i -> ColumnViews.apply(playersTable, allColumns, ColumnViews.PLAYERS.get(i)));
        viewTabs.select(Math.max(0, ColumnViews.PLAYERS.indexOf(ColumnViews.defaultView())));

        JPanel bar = new Ui.Toolbar(viewTabs).hint("Double-click a player for every field. Right-click a cell for its code.").underlined();
        JPanel table = new JPanel(new BorderLayout());
        table.setOpaque(false);
        table.add(Ui.scroll(playersTable), BorderLayout.CENTER);
        JPanel panel = new JPanel(new BorderLayout());
        JPanel head = new JPanel(new BorderLayout());
        head.setOpaque(false);
        head.add(bar, BorderLayout.NORTH);
        head.add(teamSummary, BorderLayout.CENTER);
        panel.add(head, BorderLayout.NORTH);
        panel.add(table, BorderLayout.CENTER);
        return Ui.pad(panel);
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

        JPanel bar = new Ui.Toolbar(Ui.labeled("Kind", kindBox), Ui.labeled("Player", playerFilterBox), Ui.labeled("Text", textBox))
            .hint("Rows from the last " + GuiModel.RECENT_EVENT_SECONDS + " game seconds are highlighted. Double-click for details.");

        JPanel panel = new JPanel(new BorderLayout());
        panel.add(bar, BorderLayout.NORTH);
        panel.add(followNewest(eventsTable), BorderLayout.CENTER);
        return Ui.pad(panel);
    }

    /** An append-only table that follows new rows until the user scrolls up. */
    private JScrollPane logTab(TableModel tableModel, String hint, Map<String, Integer> widths) {
        GuiTables.HintTable table = GuiTables.table(tableModel, hint, widths, GuiTables.Sorting.NONE);
        return followNewest(table);
    }

    /** Scrolls to the last row after an append, once per burst of rows, unless the user has scrolled up. */
    private static JScrollPane followNewest(GuiTables.HintTable table) {
        JScrollPane scroll = Ui.scroll(table);
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

    private JComponent lobbyTab() {
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

        predictionLabel.setFont(Theme.semibold(12));
        lobbyMapLabel.setFont(Theme.semibold(15));
        lobbyMapLabel.setForeground(Theme.TEXT);

        JPanel header = new JPanel(new BorderLayout(12, 0));
        header.setOpaque(false);
        header.setBorder(BorderFactory.createEmptyBorder(0, 0, 10, 0));
        header.add(Ui.line(lobbyKindPill, lobbyMapLabel, lobbyHostLabel), BorderLayout.CENTER);
        header.add(Ui.line(lobbyFlagsLabel, predictionLabel), BorderLayout.EAST);

        JScrollPane tableScroll = Ui.scroll(table);
        tableScroll.setPreferredSize(new Dimension(400, 285));
        JPanel upper = new JPanel(new BorderLayout());
        upper.setOpaque(false);
        upper.add(header, BorderLayout.NORTH);
        upper.add(tableScroll, BorderLayout.CENTER);
        JPanel panel = new JPanel(new BorderLayout(0, Ui.GAP));
        panel.setOpaque(false);
        panel.add(upper, BorderLayout.NORTH);
        panel.add(lobbyPanel, BorderLayout.CENTER);
        return Ui.pageScroll(Ui.pad(panel));
    }

    private JTable lobbyTable;

    JTable lobbyTable() {
        return lobbyTable;
    }

    private boolean isHuman(int modelRow) {
        return model.lobbySlots.get(modelRow).data().getState() == LobbySlotState.LOBBY_SLOT_HUMAN;
    }

    private JComponent matchTab() {
        GuiTables.HintTable info = GuiTables.table(model.matchInfo, "Waiting for a MatchInfo frame.", Map.of("Key", 200, "Value", 700), GuiTables.Sorting.NONE);
        GuiTables.HintTable observers = GuiTables.table(model.observers, "No observers in this match.", Map.of("Name", 160), GuiTables.Sorting.NONE);
        GuiTables.HintTable summary = GuiTables.table(model.summary, "Filled at the score screen when the match ends.",
            Map.of("Player", 130, "Side", 90, "Buildings destroyed", 120), GuiTables.Sorting.BY_HEADER);
        GuiTables.useRenderer(observers, "ELO", new GuiTables.BlankRenderer(false));
        GuiTables.HintTable hello = GuiTables.table(model.helloInfo, "Waiting for the Hello of the game.", Map.of("Key", 130, "Value", 420), GuiTables.Sorting.NONE);
        JPanel infoPanel = Ui.section("Match info", Ui.scroll(info));
        JPanel top = infoPanel;
        top.setPreferredSize(new Dimension(400, 280));
        JPanel middle = Ui.section("Observers", Ui.scroll(observers));
        middle.setPreferredSize(new Dimension(400, 130));
        JPanel bottom = Ui.section("Match summary (sent when the match ends)", Ui.scroll(summary));
        JSplitPane lower = Ui.split(JSplitPane.VERTICAL_SPLIT, middle, bottom, 0.35);
        JSplitPane split = Ui.split(JSplitPane.VERTICAL_SPLIT, top, lower, 0.5);
        return Ui.pad(wrap(split));
    }

    private static JPanel logPage(JComponent toolbar, JComponent content) {
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(content, BorderLayout.CENTER);
        return panel;
    }

    private static JPanel wrap(JComponent content) {
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(content, BorderLayout.CENTER);
        return panel;
    }

    // -- "Show code for this".

    private JComponent framesTab() {
        GuiTables.HintTable table = GuiTables.table(model.frames, "No frames received yet.", Map.of("Summary", 800, "Type", 150, "Received", 100), GuiTables.Sorting.NONE);
        attachShowCode(table, (row, column) -> new CodeTab.Request(Examples.Scenario.PRINT_FRAMES, Examples.Selection.defaults()));
        return Ui.pad(logPage(new Ui.Toolbar().hint("Every frame the game sent, newest at the bottom. Camera frames are counted, not listed."), followNewest(table)));
    }

    /** Adds a context menu item "Show code for this" to a table; the function maps the clicked model row and column to a request. */
    private void attachShowCode(JTable table, BiFunction<Integer, Integer, CodeTab.Request> request) {
        attachMenu(table, List.of(new MenuEntry("Show code for this", (row, column) -> showCode(request.apply(row, column)), (row, column) -> true)));
    }

    private static final class TableMenu {
        private final int[] target;
        private final List<JMenuItem> items;
        private final List<MenuEntry> entries;

        private TableMenu(int[] target, List<JMenuItem> items, List<MenuEntry> entries) {
            this.target = target;
            this.items = items;
            this.entries = entries;
        }

        int[] target() {
            return target;
        }

        List<JMenuItem> items() {
            return items;
        }

        List<MenuEntry> entries() {
            return entries;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof TableMenu)) {
                return false;
            }
            TableMenu other = (TableMenu) o;
            return Objects.equals(target, other.target)
                    && Objects.equals(items, other.items)
                    && Objects.equals(entries, other.entries);
        }

        @Override
        public int hashCode() {
            return Objects.hash(target, items, entries);
        }

        @Override
        public String toString() {
            return "TableMenu[" + "target=" + target + ", items=" + items + ", entries=" + entries + "]";
        }
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
    private static final class MenuEntry {
        private final String label;
        private final BiConsumer<Integer, Integer> action;
        private final BiPredicate<Integer, Integer> enabled;

        private MenuEntry(String label, BiConsumer<Integer, Integer> action, BiPredicate<Integer, Integer> enabled) {
            this.label = label;
            this.action = action;
            this.enabled = enabled;
        }

        String label() {
            return label;
        }

        BiConsumer<Integer, Integer> action() {
            return action;
        }

        BiPredicate<Integer, Integer> enabled() {
            return enabled;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof MenuEntry)) {
                return false;
            }
            MenuEntry other = (MenuEntry) o;
            return Objects.equals(label, other.label)
                    && Objects.equals(action, other.action)
                    && Objects.equals(enabled, other.enabled);
        }

        @Override
        public int hashCode() {
            return Objects.hash(label, action, enabled);
        }

        @Override
        public String toString() {
            return "MenuEntry[" + "label=" + label + ", action=" + action + ", enabled=" + enabled + "]";
        }
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
        nav.select(codeTab);
    }

    // -- The panels that ask the game.

    /** Connects the menus and buttons of the Query, Camera, In view and Director tabs to each other and to the code generator. */
    private void wireControlPanels() {
        queryTab.onFollow(id -> {
            nav.select(cameraTab);
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
            nav.select(queryTab);
            queryTab.inspect(objectId);
        }
    }

    /** The panels that read the library state (view statistics, camera, interest, director) refresh from the same timer as the labels. */
    private void refreshPanels() {
        queryTab.refreshPlayers();
        queryTab.refreshSelection();
        controlTab.refresh();
        chatTab.refresh();
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

    NavRail nav() {
        return nav;
    }

    /** The titles of the sections, in the order of the rail. */
    java.util.List<String> sectionTitles() {
        return nav.titles();
    }

    /** Opens a section by its title. */
    void select(String title) {
        nav.select(title);
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
        JTextArea area = Ui.code();
        area.setText(text);
        area.setCaretPosition(0);
        JDialog dialog = new JDialog(frame, title, false);
        dialog.getContentPane().setBackground(Theme.SURFACE);
        dialog.add(Ui.scroll(area));
        dialog.setSize(700, 740);
        dialog.setLocationRelativeTo(frame);
        dialog.setVisible(true);
    }

    // -- Status.

    private void refreshLabels() {
        header.update(model);
        progressLabel.setText("<html><span style='color:" + Theme.hex(Theme.DIM) + "'>Frame</span> " + model.frameText() + "&nbsp;&nbsp; <span style='color:" + Theme.hex(Theme.DIM)
            + "'>Network</span> <span style='color:" + (model.networkStalled() ? Theme.hex(Theme.WARN) : Theme.hex(Theme.MUTED)) + "'>" + model.networkText() + "</span></html>");
        cameraLabel.setText("<html><span style='color:" + Theme.hex(Theme.DIM) + "'>This PC</span> " + model.thisPcText() + "</html>");
        boolean live = player == null && pipe != null && model.connectionTone() == Tone.OK;
        if (live && !wasLive) {
            queryTab.applySaved();
        }
        wasLive = live;
        header.updateRecording(player == null && pipe != null && model.connectionTone() == Tone.OK, recorder != null && (recorder.isOpen() || System.nanoTime() < savedUntilNanos) ? recorder : null, player != null);
        if (recorder != null && recorder.isOpen()) {
            recorder.flush();
        }
        if (player != null) {
            playbackBar.update(player.isPaused(), player.isFinished(), player.progress(), player.failure());
        }
        teamSummary.refresh(model.players.rows());
        boolean inLobby = !model.lobbyKind().isEmpty();
        lobbyKindPill.setVisible(inLobby);
        lobbyKindPill.setText(model.lobbyKind());
        lobbyKindPill.setTone(Tone.OK);
        lobbyMapLabel.setText(inLobby ? model.lobbyMap() : "Not in a game lobby");
        lobbyMapLabel.setForeground(inLobby ? Theme.TEXT : Theme.MUTED);
        lobbyHostLabel.setText(inLobby ? "hosted by " + model.lobbyHost() : " ");
        lobbyFlagsLabel.setText(inLobby ? model.lobbyFlags() : " ");
        if (frame != null) {
            String title = model.windowTitle();
            if (!title.equals(frame.getTitle())) {
                frame.setTitle(title);
            }
        }

        staleBanner.show(model.staleText());
        resultBanner.show(model.resultBanner());
        if (eventsTable != null) {
            Color foreground = model.isStale() ? Theme.DIM : Theme.TEXT;
            playersTable.setForeground(foreground);
            eventsTable.setForeground(foreground);
            eventsTable.repaint(); // the "recent" highlight moves with game time
        }
        refreshPlayerChoices();
        header.setToolTipText(model.state().hello().map(h -> "<html>" + h.getGameBuild() + "<br>" + h.getCapabilitiesCount() + " capabilities: " + String.join(", ", h.getCapabilitiesList()) + "</html>").orElse(null));
        nav.badge("Players", model.players.getRowCount() == 0 ? "" : Integer.toString(model.players.getRowCount()));
        nav.badge("Events", model.events.getRowCount() == 0 ? "" : Integer.toString(model.events.getRowCount()));
        nav.badge("Chat", model.chat.getRowCount() == 0 ? "" : Integer.toString(model.chat.getRowCount()));
        nav.badge("Frames", model.frames.getRowCount() == 0 ? "" : Integer.toString(model.frames.getRowCount()));

        Boolean ok = model.predictionCheckOk();
        predictionLabel.setText(ok == null ? " " : model.predictionCheckText());
        predictionLabel.setVisible(ok != null);
        predictionLabel.setTone(ok == null ? Tone.IDLE : ok ? Tone.OK : Tone.BAD);
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
        footerLabel.setText(FeedHealth.quiet(sample));
    }

    /** A strip under the header for a state that needs attention: the feed went stale, or the match is over. It takes no room while it has nothing to say. */
    @SuppressWarnings("serial")
    private static final class Banner extends JLabel {
        private final Tone tone;

        Banner(Tone tone) {
            this.tone = tone;
            setOpaque(true);
            setBackground(tone.soft());
            setForeground(tone.color());
            setFont(Theme.semibold(13));
            setBorder(BorderFactory.createEmptyBorder(8, 18, 8, 18));
            setAlignmentX(java.awt.Component.LEFT_ALIGNMENT);
            setVisible(false);
        }

        void show(String text) {
            setText(text == null ? " " : text);
            setVisible(text != null);
        }

        @Override
        public Dimension getMaximumSize() {
            return new Dimension(Integer.MAX_VALUE, getPreferredSize().height);
        }

        Tone tone() {
            return tone;
        }
    }

    /** One entry of the Events player filter; a null index means all players. */
    static final class PlayerChoice {
        private final Integer index;
        private final String label;

        PlayerChoice(Integer index, String label) {
            this.index = index;
            this.label = label;
        }

        Integer index() {
            return index;
        }

        String label() {
            return label;
        }

        static final PlayerChoice ALL = new PlayerChoice(null, "All players");

        @Override
        public String toString() {
            return label;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof PlayerChoice)) {
                return false;
            }
            PlayerChoice other = (PlayerChoice) o;
            return Objects.equals(index, other.index)
                    && Objects.equals(label, other.label);
        }

        @Override
        public int hashCode() {
            return Objects.hash(index, label);
        }

    }
}
