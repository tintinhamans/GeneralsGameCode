package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.MatchPlayer;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.JSplitPane;
import javax.swing.JTabbedPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import java.awt.BorderLayout;
import java.awt.Dimension;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.function.Consumer;

import static com.generalsonline.stats.app.RowTableModel.column;

/**
 * The Query tab: the questions a reader can ask the game. A catalog browser with paging, an object inspector, player detail,
 * the map with its minimap image, and the feed configuration with resync. Every answer comes back through
 * {@link PipeCommands} and is shown on the event thread; a failure says why, and the game's refusal (NOT ALLOWED) is shown
 * apart from a mistake.
 */
@SuppressWarnings("serial")
final class QueryTab extends JPanel {
    private static final List<String> KINDS = List.of("(any)", "STRUCTURE", "INFANTRY", "VEHICLE", "AIRCRAFT", "HERO", "DOZER", "HARVESTER");

    private final PipeCommands commands;
    private final GuiModel model;
    private final JTabbedPane sections = new JTabbedPane();

    // Catalog.
    private final CatalogPager pager = new CatalogPager();
    final RowTableModel<CatalogEntry> entries = new RowTableModel<>(5000, List.of(
        column("Id", Long.class, (CatalogEntry e) -> Format.u(e.getTemplateId())),
        column("Name", String.class, CatalogEntry::getName),
        column("Display name", String.class, CatalogEntry::getDisplayName),
        column("Side", String.class, CatalogEntry::getSide),
        column("Cost", Long.class, (CatalogEntry e) -> Format.u(e.getCost())),
        column("Build time (s)", String.class, (CatalogEntry e) -> String.format("%.1f", e.getBuildTimeSeconds())),
        column("Kind", String.class, (CatalogEntry e) -> String.join(", ", e.getKindOfList())),
        column("Buildable", Boolean.class, CatalogEntry::getBuildable),
        column("Energy", Integer.class, CatalogEntry::getEnergy)));
    private final JTextField nameField = new JTextField(14);
    private final JTextField sideField = new JTextField(8);
    private final JComboBox<String> kindBox = new JComboBox<>(KINDS.toArray(new String[0]));
    private final JCheckBox buildableBox = new JCheckBox("buildable only");
    private final JSpinner pageSize = PanelKit.spinner(25, 1, 500, 5);
    private final JButton searchButton = new JButton("Search");
    private final JButton previousButton = new JButton("< Previous");
    private final JButton nextButton = new JButton("Next >");
    private final JLabel pageLabel = new JLabel("no search yet");
    private final JLabel catalogStatus = PanelKit.statusLabel();
    private final GuiTables.HintTable catalogTable;

    // Object.
    private final JTextField objectField = new JTextField(10);
    private final JButton inspectButton = new JButton("Inspect");
    private final JButton followButton = new JButton("Follow in the camera");
    private final JTextArea objectArea = PanelKit.monospaced();
    private final JLabel objectStatus = PanelKit.statusLabel();
    private int lastObjectId;
    private Consumer<Integer> followRequest = id -> {
    };

    // Player.
    private final JComboBox<PlayerChoice> playerBox = new JComboBox<>();
    private final JButton playerButton = new JButton("Ask");
    private final JTextArea playerArea = PanelKit.monospaced();
    private final JLabel playerStatus = PanelKit.statusLabel();
    private String playerSignature = "";

    // Map.
    private final JButton mapButton = new JButton("Load map");
    private final JCheckBox minimapBox = new JCheckBox("with the minimap image", true);
    private final JTextArea mapArea = PanelKit.monospaced();
    private final MapView mapView = new MapView();
    private final JLabel mapStatus = PanelKit.statusLabel();

    // Feed.
    private final JCheckBox eventsBox = new JCheckBox("events");
    private final JCheckBox cameraBox = new JCheckBox("camera");
    private final JCheckBox chatBox = new JCheckBox("chat");
    private final JCheckBox gridsBox = new JCheckBox("grids");
    private final JCheckBox supplyBox = new JCheckBox("supply");
    private final JCheckBox techBox = new JCheckBox("tech");
    private final JCheckBox productionBox = new JCheckBox("production");
    private final JCheckBox interestBox = new JCheckBox("interest map");
    private final JSpinner lightMs = PanelKit.spinner(100, 0, 60000, 50);
    private final JCheckBox everyFrameBox = new JCheckBox("every frame");
    private final JSpinner fullMs = PanelKit.spinner(250, 0, 60000, 50);
    private final JSpinner viewMs = PanelKit.spinner(250, 0, 60000, 50);
    private final JSpinner interestMs = PanelKit.spinner(500, 250, 60000, 50);
    private final JComboBox<Integer> gridBox = new JComboBox<>(new Integer[] {8, 16, 32, 64});
    private final JSpinner notableCost = PanelKit.spinner(2000, 0, 1_000_000, 100);
    private final JSpinner tradeMs = PanelKit.spinner(15000, 1000, 60000, 1000);
    private final JSpinner intensityMs = PanelKit.spinner(5000, 1000, 60000, 1000);
    private final JCheckBox followSelectionBox = new JCheckBox("Follow selection", Prefs.getBoolean("followSelection", true));
    private final JPanel selectionBar = Ui.bar();
    private JPanel feedPanel;
    private List<Integer> lastSelection = new ArrayList<>();
    private int shownObject;
    private boolean objectPending;
    private long lastAutoNanos;
    private boolean autoGone;
    private final JButton applyButton = new JButton("Apply");
    private final JButton resetButton = new JButton("Reset to defaults");
    private final JButton readButton = new JButton("Read from the game");
    private final JButton resyncButton = new JButton("Resync");
    private final JTextArea feedArea = PanelKit.monospaced();
    private final JLabel feedStatus = PanelKit.statusLabel();

    QueryTab(PipeCommands commands, GuiModel model) {
        super(new BorderLayout());
        this.commands = commands;
        this.model = model;
        catalogTable = GuiTables.table(entries, "No search yet. Press Search to list the game's object templates.",
            Map.of("Name", 200, "Display name", 160, "Kind", 200), GuiTables.Sorting.BY_HEADER);
        sections.addTab("Catalog", catalogSection());
        sections.addTab("Object", objectSection());
        sections.addTab("Player", playerSection());
        sections.addTab("Map", mapSection());
        feedPanel = feedSection();
        add(sections, BorderLayout.CENTER);
        refreshButtons();
    }

    // -- What the window wires up.

    /** Called with an object id when "Follow in the camera" is pressed; the window switches to the camera panel. */
    void onFollow(Consumer<Integer> request) {
        this.followRequest = request;
    }

    GuiTables.HintTable catalogTable() {
        return catalogTable;
    }

    JTextArea objectArea() {
        return objectArea;
    }

    /** The object the inspector looked at last, 0 before any. */
    int lastObjectId() {
        return lastObjectId;
    }

    /** The row of the catalog table for a model row. */
    CatalogEntry catalogEntry(int modelRow) {
        return entries.get(modelRow);
    }

    // -- Catalog.

    private JPanel catalogSection() {
        buildableBox.setOpaque(false);
        Ui.primary(searchButton);
        JPanel bar = new Ui.Toolbar(Ui.labeled("Name", nameField), Ui.labeled("Side", sideField), Ui.labeled("Kind", kindBox), buildableBox,
            Ui.labeled("Rows per page", pageSize), searchButton, previousButton, nextButton, pageLabel);
        searchButton.addActionListener(e -> search());
        nameField.addActionListener(e -> search());
        sideField.addActionListener(e -> search());
        previousButton.addActionListener(e -> load(pager.previous()));
        nextButton.addActionListener(e -> load(pager.next()));
        JPanel top = new JPanel(new BorderLayout(0, 6));
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, 8, 0));
        top.add(bar, BorderLayout.CENTER);
        top.add(catalogStatus, BorderLayout.SOUTH);
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(top, BorderLayout.NORTH);
        panel.add(Ui.scroll(catalogTable), BorderLayout.CENTER);
        return Ui.pad(panel);
    }

    /** Fills the filters and searches; used by "Look up by name" in the catalog menu and by tests. */
    void lookUp(String name, String side) {
        sections.setSelectedIndex(0);
        nameField.setText(name);
        sideField.setText(side);
        search();
    }

    private void search() {
        CatalogQuery.Builder filter = CatalogQuery.newBuilder().setName(nameField.getText().trim()).setSide(sideField.getText().trim()).setBuildableOnly(buildableBox.isSelected());
        if (kindBox.getSelectedIndex() > 0) {
            filter.setKindOf((String) kindBox.getSelectedItem());
        }
        load(pager.start(filter.build(), PanelKit.intOf(pageSize)));
    }

    private void load(CatalogQuery query) {
        PanelKit.info(catalogStatus, "asking the game...");
        commands.run(pipe -> pipe.catalog(query), result -> {
            pager.accept(result);
            entries.replaceAll(result.getEntriesList());
            pageLabel.setText(pager.text());
            PanelKit.good(catalogStatus, result.getEntriesCount() + " templates");
            refreshButtons();
        }, failure -> {
            PanelKit.fail(catalogStatus, failure);
            refreshButtons();
        });
    }

    private void refreshButtons() {
        previousButton.setEnabled(pager.hasPrevious());
        nextButton.setEnabled(pager.hasNext());
    }

    // -- Object.

    private JPanel objectSection() {
        inspectButton.addActionListener(e -> inspect((int) PanelKit.whole(objectField, 0)));
        objectField.addActionListener(e -> inspect((int) PanelKit.whole(objectField, 0)));
        followButton.addActionListener(e -> {
            if (lastObjectId != 0) {
                followRequest.accept(lastObjectId);
            }
        });
        Ui.primary(inspectButton);
        followSelectionBox.setOpaque(false);
        followSelectionBox.setToolTipText("Inspect whatever you select in the game, and keep it up to date. Looking up an id yourself turns this off.");
        selectionBar.setVisible(false);
        JPanel panel = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout(0, 6));
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, 8, 0));
        top.add(Ui.stack(4, new Ui.Toolbar(Ui.labeled("Object id", objectField), inspectButton, followButton, followSelectionBox)
            .hint("Ids are in the Events tab (right-click a row: Inspect object) and the In view tab (notable units)."), selectionBar), BorderLayout.CENTER);
        top.add(objectStatus, BorderLayout.SOUTH);
        panel.add(top, BorderLayout.NORTH);
        panel.add(Ui.scroll(objectArea), BorderLayout.CENTER);
        return Ui.pad(panel);
    }

    /** Shows the Object section and asks the game about this object. */
    void inspect(int objectId) {
        sections.setSelectedIndex(1);
        if (objectId == 0) {
            PanelKit.fail(objectStatus, new PipeCommands.Failure("INVALID", "enter an object id (a whole number above 0)", false));
            return;
        }
        if (followSelectionBox.isSelected()) {
            followSelectionBox.setSelected(false);
        }
        showObject(objectId, false);
    }

    /**
     * Asks the game about an object and shows the answer. An automatic request (following the selection) keeps the reading
     * position of the text, says so in the status and, when the object is gone, keeps the last text and says that.
     */
    private void showObject(int objectId, boolean automatic) {
        objectField.setText(Long.toString(Format.u(objectId)));
        if (!automatic) {
            PanelKit.info(objectStatus, "asking the game...");
        }
        objectPending = true;
        shownObject = objectId;
        autoGone = false;
        commands.run(pipe -> pipe.object(objectId), detail -> {
            objectPending = false;
            boolean sameObject = lastObjectId == objectId;
            lastObjectId = objectId;
            String text = QueryText.object(detail, model.state()::playerName);
            if (!text.equals(objectArea.getText())) {
                int caret = objectArea.getCaretPosition();
                objectArea.setText(text);
                objectArea.setCaretPosition(automatic && sameObject ? Math.min(caret, text.length()) : 0);
            }
            if (automatic) {
                PanelKit.info(objectStatus, "Following the selection: object " + Format.u(objectId) + ", kept up to date");
            } else {
                PanelKit.good(objectStatus, "object " + Format.u(objectId) + " found");
            }
        }, failure -> {
            objectPending = false;
            if (automatic && "NOT_FOUND".equals(failure.kind())) {
                autoGone = true;
                PanelKit.warn(objectStatus, "Object " + Format.u(objectId) + " is gone (destroyed or removed). The text above is its last known state.");
            } else if (automatic) {
                PanelKit.fail(objectStatus, failure);
            } else {
                objectArea.setText("");
                PanelKit.fail(objectStatus, failure);
            }
        });
    }

    /** Turns "follow the selection" on or off (the setting of the Settings section). */
    void setFollowSelection(boolean on) {
        followSelectionBox.setSelected(on);
    }

    /**
     * Called from the window's timer: inspects the object selected in the game when the selection changes, and refreshes it about
     * once a second while it stays selected. Only while this section is on screen, so a hidden window asks the game for nothing.
     */
    void refreshSelection() {
        List<Integer> selection = model.selectedObjects();
        boolean changed = !selection.equals(lastSelection);
        if (changed) {
            lastSelection = new ArrayList<>(selection);
            rebuildSelectionBar();
        }
        if (!followSelectionBox.isSelected() || !isShowing() || sections.getSelectedIndex() != 1) {
            return;
        }
        long now = System.nanoTime();
        if (selection.isEmpty()) {
            if (changed && shownObject != 0) {
                PanelKit.warn(objectStatus, "Object " + Format.u(shownObject) + " is no longer selected in the game. Showing its last known state.");
            }
            return;
        }
        if (objectPending) {
            return;
        }
        boolean stillShown = selection.contains(shownObject);
        if (changed && !stillShown) {
            lastAutoNanos = now;
            showObject(selection.get(0), true);
        } else if (stillShown && !autoGone && now - lastAutoNanos >= 1_000_000_000L) {
            lastAutoNanos = now;
            showObject(shownObject, true);
        }
    }

    /** "Selected in the game: 601 602 603": one click switches the inspector to another selected object. */
    private void rebuildSelectionBar() {
        selectionBar.removeAll();
        if (lastSelection.isEmpty()) {
            selectionBar.setVisible(false);
            return;
        }
        selectionBar.add(Ui.caption("Selected in the game"));
        int shown = 0;
        for (int id : lastSelection) {
            if (shown++ == 12) {
                selectionBar.add(Ui.caption("+" + (lastSelection.size() - 12) + " more"));
                break;
            }
            JButton chip = new JButton(Long.toString(Format.u(id)));
            chip.putClientProperty("FlatLaf.style", "arc: 999; margin: 2,10,2,10");
            chip.addActionListener(e -> showObject(id, true));
            selectionBar.add(chip);
        }
        selectionBar.setVisible(true);
        selectionBar.revalidate();
    }

    /** The feed settings, as a panel for the Settings section. */
    JPanel feedPanel() {
        return feedPanel;
    }

    /** Sends the settings saved by the last Apply, if any; called on every connect, because the game keeps them per connection. */
    void applySaved() {
        String saved = Prefs.get("feed", "");
        if (saved.isEmpty()) {
            return;
        }
        try {
            sendFeed(FeedConfigQuery.parseFrom(java.util.Base64.getDecoder().decode(saved)), "your saved settings were sent to the game again");
        } catch (com.google.protobuf.InvalidProtocolBufferException | IllegalArgumentException e) {
            Prefs.put("feed", "");
        }
    }

    // -- Player.

    private JPanel playerSection() {
        playerButton.addActionListener(e -> {
            PlayerChoice choice = (PlayerChoice) playerBox.getSelectedItem();
            if (choice == null) {
                PanelKit.fail(playerStatus, new PipeCommands.Failure("NO PLAYER", "no match with players yet", false));
                return;
            }
            PanelKit.info(playerStatus, "asking the game...");
            commands.run(pipe -> pipe.player(choice.index()), detail -> {
                playerArea.setText(QueryText.player(detail));
                playerArea.setCaretPosition(0);
                PanelKit.good(playerStatus, "player " + choice.index() + " answered");
            }, failure -> {
                playerArea.setText("");
                PanelKit.fail(playerStatus, failure);
            });
        });
        Ui.primary(playerButton);
        JPanel panel = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout(0, 6));
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, 8, 0));
        top.add(new Ui.Toolbar(Ui.labeled("Player", playerBox), playerButton), BorderLayout.CENTER);
        top.add(playerStatus, BorderLayout.SOUTH);
        panel.add(top, BorderLayout.NORTH);
        panel.add(Ui.scroll(playerArea), BorderLayout.CENTER);
        return Ui.pad(panel);
    }

    /** One entry of the player list: the engine's player index and the name. */
    static final class PlayerChoice {
        private final int index;
        private final String name;

        PlayerChoice(int index, String name) {
            this.index = index;
            this.name = name;
        }

        int index() {
            return index;
        }

        String name() {
            return name;
        }

        @Override
        public String toString() {
            return name + " (" + index + ")";
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
            return index == other.index
                    && Objects.equals(name, other.name);
        }

        @Override
        public int hashCode() {
            return Objects.hash(index, name);
        }

    }

    /** Keeps the player list in step with the players of the match; cheap when nothing changed. */
    void refreshPlayers() {
        List<PlayerChoice> choices = new ArrayList<>();
        StringBuilder signature = new StringBuilder();
        for (MatchPlayer p : model.matchPlayers()) {
            if (!p.getIsObserver()) {
                choices.add(new PlayerChoice(p.getPlayerIndex(), p.getName()));
                signature.append(p.getPlayerIndex()).append(':').append(p.getName()).append(';');
            }
        }
        if (signature.toString().equals(playerSignature)) {
            return;
        }
        playerSignature = signature.toString();
        PlayerChoice selected = (PlayerChoice) playerBox.getSelectedItem();
        playerBox.setModel(new javax.swing.DefaultComboBoxModel<>(choices.toArray(new PlayerChoice[0])));
        for (PlayerChoice choice : choices) {
            if (selected != null && choice.index() == selected.index()) {
                playerBox.setSelectedItem(choice);
            }
        }
    }

    // -- Map.

    private JPanel mapSection() {
        mapButton.addActionListener(e -> {
            PanelKit.info(mapStatus, "asking the game...");
            commands.run(pipe -> pipe.map(minimapBox.isSelected()), this::showMap, failure -> {
                mapArea.setText("");
                mapView.show(null);
                PanelKit.fail(mapStatus, failure);
            });
        });
        mapArea.setRows(8);
        Ui.primary(mapButton);
        minimapBox.setOpaque(false);
        JPanel left = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout(0, 6));
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, 8, 0));
        top.add(new Ui.Toolbar(mapButton, minimapBox), BorderLayout.CENTER);
        top.add(mapStatus, BorderLayout.SOUTH);
        left.add(top, BorderLayout.NORTH);
        left.add(Ui.scroll(mapArea), BorderLayout.CENTER);
        left.setPreferredSize(new Dimension(380, 300));
        JSplitPane split = Ui.split(JSplitPane.HORIZONTAL_SPLIT, left, mapView, 0.35);
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(split, BorderLayout.CENTER);
        return Ui.pad(panel);
    }

    void showMap(MapDetail map) {
        mapArea.setText(QueryText.map(map));
        mapArea.setCaretPosition(0);
        mapView.show(map);
        PanelKit.good(mapStatus, "map " + Format.mapDisplayName(map.getMapName()) + (map.hasMinimap() ? " with its minimap image" : ""));
    }

    MapView mapView() {
        return mapView;
    }

    // -- Feed.

    private JPanel feedSection() {
        showFeedControls(FeedConfig.newBuilder().setLightIntervalMs(100).setFullIntervalMs(250).setEvents(true).setCamera(true).setChat(true).setGrids(true).setSupply(true)
            .setTech(true).setProduction(true).setGridResolution(32).setViewStatsIntervalMs(250).setInterestIntervalMs(500).setNotableCost(2000).setTradeWindowMs(15000)
            .setIntensityWindowMs(5000).build());

        applyButton.addActionListener(e -> sendFeed(feedQuery(false), "feed configuration applied"));
        resetButton.addActionListener(e -> sendFeed(FeedConfigQuery.newBuilder().setReset(true).build(), "feed configuration reset to the defaults"));
        readButton.addActionListener(e -> {
            PanelKit.info(feedStatus, "asking the game...");
            commands.run(pipe -> pipe.feedConfig(), config -> {
                showFeed(config);
                PanelKit.good(feedStatus, "this reader's feed configuration");
            }, failure -> PanelKit.fail(feedStatus, failure));
        });
        resyncButton.addActionListener(e -> {
            PanelKit.info(feedStatus, "asking the game...");
            commands.run(pipe -> pipe.resync(), done -> PanelKit.good(feedStatus, "resync asked: the screen, lobby, match and a full snapshot follow"),
                failure -> PanelKit.fail(feedStatus, failure));
        });

        Ui.primary(applyButton);
        for (JCheckBox box : List.of(eventsBox, cameraBox, chatBox, gridsBox, supplyBox, techBox, productionBox, interestBox)) {
            box.setOpaque(false);
        }
        JPanel streams = Ui.card("What this reader receives", "Each reader has its own configuration; this changes only this window's.",
            Ui.bar(eventsBox, cameraBox, chatBox, gridsBox, supplyBox, techBox, productionBox, interestBox));
        JPanel timing = Ui.card("How often", "Intervals in milliseconds; 0 turns a snapshot off. Every frame sends a light snapshot with each logic frame.", Ui.bar(
            Ui.field("Light snapshot", Ui.line(lightMs, everyFrameBox)), Ui.field("Full snapshot", fullMs), Ui.field("View stats", viewMs), Ui.field("Interest", interestMs),
            Ui.field("Grid resolution", gridBox), Ui.field("Notable cost", notableCost), Ui.field("Trade window", tradeMs), Ui.field("Intensity window", intensityMs)));
        lightMs.setToolTipText("Milliseconds between light snapshots; 0 = none");
        everyFrameBox.setOpaque(false);
        everyFrameBox.setToolTipText("A light snapshot with every logic frame (30 or 60 a second). Older game builds refuse it.");
        everyFrameBox.addActionListener(e -> lightMs.setEnabled(!everyFrameBox.isSelected()));
        JPanel buttons = PanelKit.row(applyButton, resetButton, readButton, resyncButton);
        JPanel top = new JPanel(new BorderLayout(0, Ui.GAP));
        top.setOpaque(false);
        top.setBorder(BorderFactory.createEmptyBorder(0, 0, Ui.GAP, 0));
        top.add(Ui.stack(Ui.GAP, streams, timing, buttons), BorderLayout.CENTER);
        top.add(feedStatus, BorderLayout.SOUTH);
        feedArea.setRows(4);
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(top, BorderLayout.NORTH);
        panel.add(Ui.scroll(feedArea), BorderLayout.CENTER);
        return panel;
    }

    /** The query that applies every control; with {@code only} false all settings are sent, so the game ends up exactly as the form shows. */
    FeedConfigQuery feedQuery(boolean reset) {
        return FeedConfigQuery.newBuilder().setReset(reset).setLightIntervalMs(everyFrameBox.isSelected() ? com.generalsonline.stats.StatsPipe.LIGHT_EVERY_FRAME : PanelKit.intOf(lightMs)).setFullIntervalMs(PanelKit.intOf(fullMs))
            .setEvents(eventsBox.isSelected()).setCamera(cameraBox.isSelected()).setChat(chatBox.isSelected()).setGrids(gridsBox.isSelected()).setSupply(supplyBox.isSelected())
            .setTech(techBox.isSelected()).setProduction(productionBox.isSelected()).setGridResolution((Integer) gridBox.getSelectedItem())
            .setViewStatsIntervalMs(PanelKit.intOf(viewMs)).setInterest(interestBox.isSelected()).setInterestIntervalMs(PanelKit.intOf(interestMs))
            .setNotableCost(PanelKit.intOf(notableCost)).setTradeWindowMs(PanelKit.intOf(tradeMs)).setIntensityWindowMs(PanelKit.intOf(intensityMs)).build();
    }

    private void sendFeed(FeedConfigQuery query, String done) {
        PanelKit.info(feedStatus, "asking the game...");
        commands.run(pipe -> pipe.setFeedConfig(query), config -> {
            Prefs.put("feed", query.getReset() ? "" : java.util.Base64.getEncoder().encodeToString(query.toByteArray()));
            showFeed(config);
            PanelKit.good(feedStatus, done);
        }, failure -> PanelKit.fail(feedStatus, failure));
    }

    /** Shows a feed configuration the game reported (also called when the minimap's interest toggle changed it). */
    void showFeed(FeedConfig config) {
        showFeedControls(config);
        feedArea.setText(QueryText.feed(config));
        feedArea.setCaretPosition(0);
    }

    private void showFeedControls(FeedConfig c) {
        eventsBox.setSelected(c.getEvents());
        cameraBox.setSelected(c.getCamera());
        chatBox.setSelected(c.getChat());
        gridsBox.setSelected(c.getGrids());
        supplyBox.setSelected(c.getSupply());
        techBox.setSelected(c.getTech());
        productionBox.setSelected(c.getProduction());
        interestBox.setSelected(c.getInterest());
        everyFrameBox.setSelected(c.getLightIntervalMs() == com.generalsonline.stats.StatsPipe.LIGHT_EVERY_FRAME);
        lightMs.setEnabled(!everyFrameBox.isSelected());
        lightMs.setValue(Math.max(50, c.getLightIntervalMs()));
        fullMs.setValue(c.getFullIntervalMs());
        viewMs.setValue(c.getViewStatsIntervalMs());
        interestMs.setValue(Math.max(250, c.getInterestIntervalMs()));
        gridBox.setSelectedItem(c.getGridResolution());
        notableCost.setValue((int) Math.min(1_000_000, Format.u(c.getNotableCost())));
        tradeMs.setValue(Math.max(1000, c.getTradeWindowMs()));
        intensityMs.setValue(Math.max(1000, c.getIntensityWindowMs()));
    }

    // -- For the tests.

    JTabbedPane sections() {
        return sections;
    }

    String objectText() {
        return objectArea.getText();
    }

    String statusOf(String section) {
        switch (section) {
            case "catalog":
                return catalogStatus.getText();
            case "object":
                return objectStatus.getText();
            case "player":
                return playerStatus.getText();
            case "map":
                return mapStatus.getText();
            default:
                return feedStatus.getText();
        }
    }

    JButton searchButton() {
        return searchButton;
    }

    JButton nextButton() {
        return nextButton;
    }

    JButton previousButton() {
        return previousButton;
    }

    JButton mapButton() {
        return mapButton;
    }

    JButton playerButton() {
        return playerButton;
    }

    JButton applyButton() {
        return applyButton;
    }

    JButton resyncButton() {
        return resyncButton;
    }

    JTextField nameField() {
        return nameField;
    }

    JTextField sideField() {
        return sideField;
    }

    JCheckBox interestBox() {
        return interestBox;
    }

    JSpinner pageSize() {
        return pageSize;
    }

    String pageText() {
        return pageLabel.getText();
    }

    JTextArea playerArea() {
        return playerArea;
    }

    JComboBox<PlayerChoice> playerBox() {
        return playerBox;
    }
}
