package com.generalsonline.stats.app;

import com.generalsonline.stats.LobbyControl;
import com.generalsonline.stats.proto.LobbyControlQuery;
import com.generalsonline.stats.proto.LobbyControlResult;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;

import javax.swing.BorderFactory;
import javax.swing.DefaultComboBoxModel;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.JTextField;
import javax.swing.BoxLayout;
import java.awt.BorderLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The controls under the Lobby tab: change the game setup lobby the user sits in through the setup screen's own controls.
 * The slot actions work on the slot in the spinner (the table selection sets it). The game applies the screen's rules, so a
 * client may only change its own slot and accept, and only the host changes the map, the cash and the other slots; what
 * the screen refuses is shown in orange and bold, mistakes in the input in red.
 */
@SuppressWarnings("serial")
final class LobbyControlPanel extends JPanel {
    private final PipeCommands commands;
    private final GuiModel model;

    private final JSpinner slotSpinner = PanelKit.spinner(1, 1, LobbyControl.MAX_SLOTS, 1);
    private final JButton openButton = new JButton("Open");
    private final JButton closeButton = new JButton("Close");
    private final JButton kickButton = new JButton("Kick");
    private final JCheckBox keepClosed = new JCheckBox("keep it closed");
    private final JComboBox<String> aiBox = new JComboBox<>(new String[] {"Easy", "Medium", "Hard"});
    private final JButton aiButton = new JButton("Add AI");

    private final JComboBox<String> factionBox = new JComboBox<>(new String[] {"Random", "Observer", "America", "China", "GLA"});
    private final JButton factionButton = new JButton("Set faction");
    private final JComboBox<String> colorBox = new JComboBox<>(new String[] {"Random"});
    private final JButton colorButton = new JButton("Set color");
    private final JComboBox<String> teamBox = new JComboBox<>(new String[] {"None", "1", "2", "3", "4"});
    private final JButton teamButton = new JButton("Set team");
    private final JComboBox<String> positionBox = new JComboBox<>(new String[] {"None", "1", "2", "3", "4", "5", "6", "7", "8"});
    private final JButton positionButton = new JButton("Set start position");

    private final JTextField mapField = new JTextField(34);
    private final JButton mapButton = new JButton("Change map");
    private final JComboBox<String> cashBox = new JComboBox<>(new String[] {"5000", "10000", "20000", "50000", "100000"});
    private final JButton cashButton = new JButton("Set cash");
    private final JCheckBox limitBox = new JCheckBox("limit superweapons");
    private final JButton limitButton = new JButton("Apply");
    private final JSpinner speedSpinner = PanelKit.spinner(30, 15, 61, 1);
    private final JButton speedButton = new JButton("Set speed");

    private final JButton acceptButton = new JButton("Accept");
    private final JButton startButton = new JButton("Start");

    private final JLabel resultLabel = PanelKit.statusLabel();
    private String colorSignature = "";

    LobbyControlPanel(PipeCommands commands, GuiModel model) {
        super(new BorderLayout());
        this.commands = commands;
        this.model = model;

        factionBox.setEditable(true);
        colorBox.setEditable(true);
        cashBox.setEditable(true);
        openButton.addActionListener(e -> send("slot opened", LobbyControl.openSlot(slot())));
        closeButton.addActionListener(e -> send("slot closed", LobbyControl.closeSlot(slot())));
        kickButton.addActionListener(e -> send("player removed", LobbyControl.kick(slot(), keepClosed.isSelected())));
        aiButton.addActionListener(e -> send("AI added", LobbyControl.addAi(slot(), aiLevel(aiBox.getSelectedIndex()))));
        factionButton.addActionListener(e -> send("faction set", LobbyControl.faction(slot(), String.valueOf(factionBox.getSelectedItem()).trim())));
        colorButton.addActionListener(e -> {
            Integer argb = parseColor(String.valueOf(colorBox.getSelectedItem()));
            if (argb == null) {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "a color is Random, or a hex value such as #3060FF or 0xFF3060FF", false));
                return;
            }
            send("color set", LobbyControl.color(slot(), argb));
        });
        teamButton.addActionListener(e -> send("team set", LobbyControl.team(slot(), teamBox.getSelectedIndex() - 1)));
        positionButton.addActionListener(e -> send("start position set", LobbyControl.startPosition(slot(), positionBox.getSelectedIndex() - 1)));
        mapButton.addActionListener(e -> {
            String map = mapField.getText().trim();
            if (map.isEmpty()) {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "type the map as the lobby header shows it, for example maps\\tournament desert\\tournament desert.map", false));
                return;
            }
            send("map changed", LobbyControl.map(map));
        });
        cashButton.addActionListener(e -> {
            long cash;
            try {
                cash = Long.parseLong(String.valueOf(cashBox.getSelectedItem()).trim());
            } catch (NumberFormatException ex) {
                cash = -1;
            }
            if (cash < 0 || cash > Integer.MAX_VALUE) {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "the starting cash is a whole number such as 10000", false));
                return;
            }
            send("starting cash set", LobbyControl.startingCash((int) cash));
        });
        limitButton.addActionListener(e -> send("superweapon limit set", LobbyControl.limitSuperweapons(limitBox.isSelected())));
        speedButton.addActionListener(e -> send("game speed set", LobbyControl.gameSpeed(PanelKit.intOf(speedSpinner))));
        acceptButton.addActionListener(e -> send("accepted", LobbyControl.accept()));
        startButton.addActionListener(e -> send("start pressed", LobbyControl.start()));

        JPanel stack = new JPanel();
        stack.setLayout(new BoxLayout(stack, BoxLayout.Y_AXIS));
        stack.setBorder(BorderFactory.createTitledBorder("Change the lobby (works like the setup screen: the host changes the lobby, a client its own slot)"));
        stack.add(PanelKit.row(new JLabel("slot"), slotSpinner, openButton, closeButton, kickButton, keepClosed, new JLabel("   computer"), aiBox, aiButton));
        stack.add(PanelKit.row(new JLabel("faction"), factionBox, factionButton, new JLabel("color"), colorBox, colorButton, new JLabel("team"), teamBox, teamButton,
            new JLabel("start position"), positionBox, positionButton));
        stack.add(PanelKit.row(new JLabel("map"), mapField, mapButton, new JLabel("starting cash"), cashBox, cashButton, limitBox, limitButton));
        stack.add(PanelKit.row(new JLabel("skirmish speed"), speedSpinner, speedButton, new JLabel("   "), acceptButton, startButton));

        JPanel bottom = new JPanel(new BorderLayout());
        bottom.setBorder(BorderFactory.createEmptyBorder(2, 10, 4, 10));
        bottom.add(resultLabel, BorderLayout.CENTER);
        add(stack, BorderLayout.CENTER);
        add(bottom, BorderLayout.SOUTH);
    }

    private int slot() {
        return PanelKit.intOf(slotSpinner) - 1;
    }

    /** Points the slot actions at a slot (0-based), for the table selection and its menu. */
    void selectSlot(int slot) {
        if (slot >= 0 && slot < LobbyControl.MAX_SLOTS) {
            slotSpinner.setValue(slot + 1);
        }
    }

    static LobbyControl.AiLevel aiLevel(int index) {
        return switch (index) {
            case 0 -> LobbyControl.AiLevel.EASY;
            case 1 -> LobbyControl.AiLevel.MEDIUM;
            default -> LobbyControl.AiLevel.BRUTAL;
        };
    }

    /** A color as typed: Random (or nothing) is 0, else hex with or without # and 0x; null when it is not one. */
    static Integer parseColor(String text) {
        String t = text.trim();
        if (t.isEmpty() || t.equalsIgnoreCase("random")) {
            return 0;
        }
        if (t.startsWith("#")) {
            t = t.substring(1);
        } else if (t.toLowerCase(Locale.ROOT).startsWith("0x")) {
            t = t.substring(2);
        }
        try {
            long value = Long.parseLong(t, 16);
            if (t.length() <= 6) {
                value |= 0xFF000000L;
            }
            return t.length() > 8 ? null : (int) value;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** What the answer says: the kind of lobby, how the change travels and the game's note. */
    static String describe(LobbyControlResult r) {
        String kind = r.getKind().name().replace("LOBBY_KIND_", "").toLowerCase(Locale.ROOT);
        String route = switch (r.getRoute()) {
            case LOBBY_CHANGE_APPLIED -> "applied at once";
            case LOBBY_CHANGE_REQUESTED_FROM_HOST -> "requested from the host, the lobby follows when it confirms";
            case LOBBY_CHANGE_REQUESTED_FROM_SERVICE -> "requested from the lobby service, the lobby follows when it confirms";
            default -> "sent";
        };
        return kind + " lobby, " + route + (r.getNote().isEmpty() ? "" : " (" + r.getNote() + ")");
    }

    private void send(String done, LobbyControlQuery action) {
        PanelKit.info(resultLabel, "asking the game...");
        commands.run(p -> p.lobbyControl(action), result -> PanelKit.good(resultLabel, done + ": " + describe(result)), failure -> PanelKit.fail(resultLabel, failure));
    }

    // -- Actions of the table menu, on a slot (0-based).

    void open(int slot) {
        selectSlot(slot);
        openButton.doClick();
    }

    void close(int slot) {
        selectSlot(slot);
        closeButton.doClick();
    }

    void kick(int slot) {
        selectSlot(slot);
        kickButton.doClick();
    }

    void addAi(int slot, int levelIndex) {
        selectSlot(slot);
        aiBox.setSelectedIndex(levelIndex);
        aiButton.doClick();
    }

    // -- Live data: the colors the lobby uses are offered, and the map field follows the lobby.

    /** Refreshes the offered colors and the map; called from a timer. */
    void refresh() {
        LobbyState lobby = model.state().lobby().orElse(null);
        if (lobby == null) {
            return;
        }
        List<String> colors = new ArrayList<>();
        colors.add("Random");
        StringBuilder signature = new StringBuilder();
        for (LobbySlot s : lobby.getSlotsList()) {
            if (s.hasPlayer() && s.getPlayer().getColor() != 0) {
                String text = String.format("#%06X", s.getPlayer().getColor() & 0xFFFFFF);
                if (!colors.contains(text)) {
                    colors.add(text);
                    signature.append(text);
                }
            }
        }
        if (!signature.toString().equals(colorSignature)) {
            colorSignature = signature.toString();
            Object selected = colorBox.getSelectedItem();
            colorBox.setModel(new DefaultComboBoxModel<>(colors.toArray(new String[0])));
            colorBox.setSelectedItem(selected == null ? "Random" : selected);
        }
        if (!mapField.hasFocus() && !mapField.getText().equals(lobby.getMapName()) && mapField.getText().isEmpty()) {
            mapField.setText(lobby.getMapName());
        }
    }

    // -- For the tests.

    JButton button(String name) {
        return switch (name) {
            case "open" -> openButton;
            case "close" -> closeButton;
            case "kick" -> kickButton;
            case "ai" -> aiButton;
            case "faction" -> factionButton;
            case "color" -> colorButton;
            case "team" -> teamButton;
            case "position" -> positionButton;
            case "map" -> mapButton;
            case "cash" -> cashButton;
            case "limit" -> limitButton;
            case "speed" -> speedButton;
            case "accept" -> acceptButton;
            default -> startButton;
        };
    }

    JSpinner slotSpinner() {
        return slotSpinner;
    }

    JComboBox<String> box(String name) {
        return switch (name) {
            case "ai" -> aiBox;
            case "faction" -> factionBox;
            case "color" -> colorBox;
            case "team" -> teamBox;
            case "position" -> positionBox;
            default -> cashBox;
        };
    }

    JTextField mapField() {
        return mapField;
    }

    JCheckBox limitBox() {
        return limitBox;
    }

    String resultText() {
        return resultLabel.getText();
    }
}
