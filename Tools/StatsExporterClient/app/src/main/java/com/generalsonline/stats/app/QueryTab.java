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
import javax.swing.JScrollPane;
import javax.swing.JSpinner;
import javax.swing.JSplitPane;
import javax.swing.JTabbedPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import java.awt.BorderLayout;
import java.awt.Dimension;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
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
    private final JSpinner fullMs = PanelKit.spinner(250, 0, 60000, 50);
    private final JSpinner viewMs = PanelKit.spinner(250, 0, 60000, 50);
    private final JSpinner interestMs = PanelKit.spinner(500, 250, 60000, 50);
    private final JComboBox<Integer> gridBox = new JComboBox<>(new Integer[] {8, 16, 32, 64});
    private final JSpinner notableCost = PanelKit.spinner(2000, 0, 1_000_000, 100);
    private final JSpinner tradeMs = PanelKit.spinner(15000, 1000, 60000, 1000);
    private final JSpinner intensityMs = PanelKit.spinner(5000, 1000, 60000, 1000);
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
        sections.addTab("Feed", feedSection());
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
        JPanel bar = PanelKit.row(new JLabel("Name:"), nameField, new JLabel("Side:"), sideField, new JLabel("Kind:"), kindBox, buildableBox,
            new JLabel("Rows per page:"), pageSize, searchButton, previousButton, nextButton, pageLabel);
        searchButton.addActionListener(e -> search());
        nameField.addActionListener(e -> search());
        sideField.addActionListener(e -> search());
        previousButton.addActionListener(e -> load(pager.previous()));
        nextButton.addActionListener(e -> load(pager.next()));
        JPanel top = new JPanel(new BorderLayout());
        top.add(bar, BorderLayout.CENTER);
        top.add(catalogStatus, BorderLayout.SOUTH);
        catalogStatus.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(top, BorderLayout.NORTH);
        panel.add(new JScrollPane(catalogTable), BorderLayout.CENTER);
        return panel;
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
        JPanel panel = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout());
        top.add(PanelKit.row(new JLabel("Object id:"), objectField, inspectButton, followButton,
            new JLabel("Ids are in the Events tab (right-click a row: Inspect object) and the In view tab (notable units).")), BorderLayout.CENTER);
        top.add(objectStatus, BorderLayout.SOUTH);
        objectStatus.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));
        panel.add(top, BorderLayout.NORTH);
        panel.add(new JScrollPane(objectArea), BorderLayout.CENTER);
        return panel;
    }

    /** Shows the Object section and asks the game about this object. */
    void inspect(int objectId) {
        sections.setSelectedIndex(1);
        if (objectId == 0) {
            PanelKit.fail(objectStatus, new PipeCommands.Failure("INVALID", "enter an object id (a whole number above 0)", false));
            return;
        }
        objectField.setText(Long.toString(Format.u(objectId)));
        PanelKit.info(objectStatus, "asking the game...");
        commands.run(pipe -> pipe.object(objectId), detail -> {
            lastObjectId = objectId;
            objectArea.setText(QueryText.object(detail, model.state()::playerName));
            objectArea.setCaretPosition(0);
            PanelKit.good(objectStatus, "object " + Format.u(objectId) + " found");
        }, failure -> {
            objectArea.setText("");
            PanelKit.fail(objectStatus, failure);
        });
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
        JPanel panel = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout());
        top.add(PanelKit.row(new JLabel("Player:"), playerBox, playerButton), BorderLayout.CENTER);
        top.add(playerStatus, BorderLayout.SOUTH);
        playerStatus.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));
        panel.add(top, BorderLayout.NORTH);
        panel.add(new JScrollPane(playerArea), BorderLayout.CENTER);
        return panel;
    }

    /** One entry of the player list: the engine's player index and the name. */
    record PlayerChoice(int index, String name) {
        @Override
        public String toString() {
            return name + " (" + index + ")";
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
        JPanel left = new JPanel(new BorderLayout());
        JPanel top = new JPanel(new BorderLayout());
        top.add(PanelKit.row(mapButton, minimapBox), BorderLayout.CENTER);
        top.add(mapStatus, BorderLayout.SOUTH);
        mapStatus.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));
        left.add(top, BorderLayout.NORTH);
        left.add(new JScrollPane(mapArea), BorderLayout.CENTER);
        left.setPreferredSize(new Dimension(380, 300));
        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, left, mapView);
        split.setResizeWeight(0.35);
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(split, BorderLayout.CENTER);
        return panel;
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
        JPanel form = new JPanel(new GridBagLayout());
        form.setBorder(BorderFactory.createEmptyBorder(6, 8, 6, 8));
        GridBagConstraints c = new GridBagConstraints();
        c.anchor = GridBagConstraints.WEST;
        c.insets = new Insets(2, 4, 2, 4);
        c.gridy = 0;
        int x = 0;
        for (JCheckBox box : List.of(eventsBox, cameraBox, chatBox, gridsBox, supplyBox, techBox, productionBox, interestBox)) {
            c.gridx = x++;
            form.add(box, c);
        }
        c.gridy = 1;
        c.gridx = 0;
        form.add(new JLabel("Light snapshot (ms):"), c);
        c.gridx = 1;
        form.add(lightMs, c);
        c.gridx = 2;
        form.add(new JLabel("Full (ms):"), c);
        c.gridx = 3;
        form.add(fullMs, c);
        c.gridx = 4;
        form.add(new JLabel("View stats (ms):"), c);
        c.gridx = 5;
        form.add(viewMs, c);
        c.gridx = 6;
        form.add(new JLabel("Interest (ms):"), c);
        c.gridx = 7;
        form.add(interestMs, c);
        c.gridy = 2;
        c.gridx = 0;
        form.add(new JLabel("Grid resolution:"), c);
        c.gridx = 1;
        form.add(gridBox, c);
        c.gridx = 2;
        form.add(new JLabel("Notable cost:"), c);
        c.gridx = 3;
        form.add(notableCost, c);
        c.gridx = 4;
        form.add(new JLabel("Trade window (ms):"), c);
        c.gridx = 5;
        form.add(tradeMs, c);
        c.gridx = 6;
        form.add(new JLabel("Intensity window (ms):"), c);
        c.gridx = 7;
        form.add(intensityMs, c);
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

        JPanel buttons = PanelKit.row(applyButton, resetButton, readButton, resyncButton,
            new JLabel("Each reader has its own configuration; this changes only this window's."));
        JPanel top = new JPanel(new BorderLayout());
        top.add(form, BorderLayout.NORTH);
        top.add(buttons, BorderLayout.CENTER);
        top.add(feedStatus, BorderLayout.SOUTH);
        feedStatus.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));
        feedArea.setRows(4);
        JPanel panel = new JPanel(new BorderLayout());
        panel.add(top, BorderLayout.NORTH);
        panel.add(new JScrollPane(feedArea), BorderLayout.CENTER);
        return panel;
    }

    /** The query that applies every control; with {@code only} false all settings are sent, so the game ends up exactly as the form shows. */
    FeedConfigQuery feedQuery(boolean reset) {
        return FeedConfigQuery.newBuilder().setReset(reset).setLightIntervalMs(PanelKit.intOf(lightMs)).setFullIntervalMs(PanelKit.intOf(fullMs))
            .setEvents(eventsBox.isSelected()).setCamera(cameraBox.isSelected()).setChat(chatBox.isSelected()).setGrids(gridsBox.isSelected()).setSupply(supplyBox.isSelected())
            .setTech(techBox.isSelected()).setProduction(productionBox.isSelected()).setGridResolution((Integer) gridBox.getSelectedItem())
            .setViewStatsIntervalMs(PanelKit.intOf(viewMs)).setInterest(interestBox.isSelected()).setInterestIntervalMs(PanelKit.intOf(interestMs))
            .setNotableCost(PanelKit.intOf(notableCost)).setTradeWindowMs(PanelKit.intOf(tradeMs)).setIntensityWindowMs(PanelKit.intOf(intensityMs)).build();
    }

    private void sendFeed(FeedConfigQuery query, String done) {
        PanelKit.info(feedStatus, "asking the game...");
        commands.run(pipe -> pipe.setFeedConfig(query), config -> {
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
        lightMs.setValue(c.getLightIntervalMs());
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
        JLabel label = switch (section) {
            case "catalog" -> catalogStatus;
            case "object" -> objectStatus;
            case "player" -> playerStatus;
            case "map" -> mapStatus;
            default -> feedStatus;
        };
        return label.getText();
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
