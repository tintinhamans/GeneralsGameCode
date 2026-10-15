package com.generalsonline.stats.app;

import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.StatEventKind;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.DefaultListCellRenderer;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JFileChooser;
import javax.swing.JLabel;
import javax.swing.JList;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.ListSelectionModel;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Toolkit;
import java.awt.datatransfer.StringSelection;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * The Code tab: pick what you want to do from the list, or choose "Show code for this" in another tab, and read a
 * complete, minimal, runnable program for it that uses the library. The selection controls fill the template with the
 * column, event kind or chat channel you care about; Copy puts the program on the clipboard and Save writes it to a
 * {@code .java} file named like its class.
 */
@SuppressWarnings("serial")
final class CodeTab extends JPanel {
    /** A request from another tab: show this scenario for this selection. */
    record Request(Examples.Scenario scenario, Examples.Selection selection) {
    }

    private final JList<Examples.Scenario> scenarios = new JList<>(Examples.Scenario.values());
    private final JLabel descriptionLabel = new JLabel(" ");
    private final JTextField playerField = new JTextField(12);
    private final JComboBox<String> columnBox = new JComboBox<>();
    private final JComboBox<StatEventKind> kindBox = new JComboBox<>();
    private final JComboBox<ChatChannel> channelBox = new JComboBox<>();
    private final JComboBox<ChatAudience> audienceBox = new JComboBox<>();
    private final JTextField textField = new JTextField(18);
    private final JTextField templateField = new JTextField(16);
    private final JTextField sideField = new JTextField(8);
    private final JTextField objectField = new JTextField(7);
    private final JTextField xField = new JTextField(6);
    private final JTextField yField = new JTextField(6);
    private final JTextField zoomField = new JTextField(4);
    private final JTextField speedField = new JTextField(4);
    private final JComboBox<DirectorMode> modeBox = new JComboBox<>(DirectorMode.values());
    private final JTextField dumpField = new JTextField(14);
    private final JTextArea codeArea = new JTextArea();
    private final JLabel statusLabel = new JLabel(" ");
    private final JButton copyButton = new JButton("Copy");
    private final JButton saveButton = new JButton("Save as .java...");
    private boolean updating;

    CodeTab() {
        super(new BorderLayout());
        for (Examples.StatColumn column : Examples.statColumns()) {
            columnBox.addItem(column.name());
        }
        for (StatEventKind kind : Examples.eventKinds()) {
            kindBox.addItem(kind);
        }
        for (ChatChannel channel : Examples.chatChannels()) {
            channelBox.addItem(channel);
        }
        for (ChatAudience audience : Examples.chatAudiences()) {
            audienceBox.addItem(audience);
        }
        kindBox.setRenderer(new DefaultListCellRenderer() {
            @Override
            public Component getListCellRendererComponent(JList<?> list, Object value, int index, boolean selected, boolean focus) {
                return super.getListCellRendererComponent(list, value instanceof StatEventKind kind ? Examples.kindName(kind) : value, index, selected, focus);
            }
        });

        scenarios.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        scenarios.setSelectedIndex(0);
        scenarios.setBorder(BorderFactory.createEmptyBorder(4, 4, 4, 4));
        scenarios.addListSelectionListener(e -> {
            if (!e.getValueIsAdjusting()) {
                regenerate();
            }
        });
        Runnable change = this::regenerate;
        columnBox.addActionListener(e -> change.run());
        kindBox.addActionListener(e -> change.run());
        channelBox.addActionListener(e -> change.run());
        audienceBox.addActionListener(e -> change.run());
        DocumentListener typing = new DocumentListener() {
            @Override
            public void insertUpdate(DocumentEvent e) {
                change.run();
            }

            @Override
            public void removeUpdate(DocumentEvent e) {
                change.run();
            }

            @Override
            public void changedUpdate(DocumentEvent e) {
                change.run();
            }
        };
        playerField.getDocument().addDocumentListener(typing);
        textField.getDocument().addDocumentListener(typing);
        for (JTextField field : List.of(templateField, sideField, objectField, xField, yField, zoomField, speedField, dumpField)) {
            field.getDocument().addDocumentListener(typing);
        }
        modeBox.addActionListener(e -> change.run());
        fill(Examples.Selection.defaults());


        codeArea.setEditable(false);
        codeArea.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
        codeArea.setTabSize(4);

        copyButton.addActionListener(e -> copy());
        saveButton.addActionListener(e -> save());

        JPanel left = new JPanel(new BorderLayout());
        JLabel heading = new JLabel("What do you want to do?");
        heading.setBorder(BorderFactory.createEmptyBorder(6, 8, 4, 8));
        heading.setFont(heading.getFont().deriveFont(Font.BOLD));
        left.add(heading, BorderLayout.NORTH);
        left.add(new JScrollPane(scenarios), BorderLayout.CENTER);
        left.setPreferredSize(new Dimension(300, 400));

        JPanel right = new JPanel(new BorderLayout());
        right.add(controls(), BorderLayout.NORTH);
        right.add(new JScrollPane(codeArea), BorderLayout.CENTER);
        JPanel buttons = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 4));
        buttons.add(copyButton);
        buttons.add(saveButton);
        buttons.add(statusLabel);
        right.add(buttons, BorderLayout.SOUTH);

        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, left, right);
        split.setResizeWeight(0.0);
        split.setDividerLocation(300);
        add(split, BorderLayout.CENTER);
        regenerate();
    }

    private JComponent controls() {
        JPanel panel = new JPanel(new GridBagLayout());
        panel.setBorder(BorderFactory.createEmptyBorder(6, 8, 6, 8));
        GridBagConstraints c = new GridBagConstraints();
        c.anchor = GridBagConstraints.WEST;
        c.insets = new java.awt.Insets(2, 4, 2, 4);

        c.gridx = 0;
        c.gridy = 0;
        c.gridwidth = 6;
        c.fill = GridBagConstraints.HORIZONTAL;
        descriptionLabel.setBorder(BorderFactory.createEmptyBorder(0, 0, 4, 0));
        panel.add(descriptionLabel, c);

        c.gridwidth = 1;
        c.fill = GridBagConstraints.NONE;
        c.gridy = 1;
        c.gridx = 0;
        panel.add(new JLabel("Player:"), c);
        c.gridx = 1;
        panel.add(playerField, c);
        c.gridx = 2;
        panel.add(new JLabel("Stat (Players column):"), c);
        c.gridx = 3;
        panel.add(columnBox, c);
        c.gridx = 4;
        panel.add(new JLabel("Event kind:"), c);
        c.gridx = 5;
        panel.add(kindBox, c);

        c.gridy = 2;
        c.gridx = 0;
        panel.add(new JLabel("Chat channel:"), c);
        c.gridx = 1;
        panel.add(channelBox, c);
        c.gridx = 2;
        panel.add(new JLabel("Audience:"), c);
        c.gridx = 3;
        panel.add(audienceBox, c);
        c.gridx = 4;
        panel.add(new JLabel("Text:"), c);
        c.gridx = 5;
        panel.add(textField, c);
        c.gridx = 6;
        c.weightx = 1;
        panel.add(Box.createHorizontalGlue(), c);
        c.weightx = 0;

        c.gridy = 3;
        c.gridx = 0;
        c.gridwidth = 7;
        c.fill = GridBagConstraints.HORIZONTAL;
        panel.add(flow(new JLabel("Template:"), templateField, new JLabel("Side:"), sideField, new JLabel("Object id:"), objectField, new JLabel("Dump file:"), dumpField), c);
        c.gridy = 4;
        panel.add(flow(new JLabel("Camera x, y, zoom:"), xField, yField, zoomField, new JLabel("Replay speed:"), speedField, new JLabel("Director mode:"), modeBox), c);
        return panel;
    }

    private static JPanel flow(Component... parts) {
        JPanel row = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 0));
        for (Component part : parts) {
            row.add(part);
        }
        return row;
    }

    /** What the controls say now; a field that is empty or not a number keeps its default. */
    Examples.Selection selection() {
        Examples.Selection defaults = Examples.Selection.defaults();
        String column = (String) columnBox.getSelectedItem();
        StatEventKind kind = (StatEventKind) kindBox.getSelectedItem();
        ChatChannel channel = (ChatChannel) channelBox.getSelectedItem();
        ChatAudience audience = (ChatAudience) audienceBox.getSelectedItem();
        String player = playerField.getText().isBlank() ? defaults.playerName() : playerField.getText().trim();
        String text = textField.getText().isBlank() ? defaults.chatText() : textField.getText();
        DirectorMode mode = (DirectorMode) modeBox.getSelectedItem();
        return new Examples.Selection(player, column == null ? defaults.column() : column, kind == null ? defaults.eventKind() : kind,
            channel == null ? defaults.channel() : channel, audience == null ? defaults.audience() : audience, text,
            textOr(templateField, defaults.templateName()), textOr(sideField, defaults.side()), (int) number(objectField, defaults.objectId()),
            number(xField, defaults.cameraX()), number(yField, defaults.cameraY()), number(zoomField, defaults.zoom()), number(speedField, defaults.replaySpeed()),
            mode == null ? defaults.directorMode() : mode, textOr(dumpField, defaults.dumpFile()));
    }

    private static String textOr(JTextField field, String fallback) {
        return field.getText().isBlank() ? fallback : field.getText().trim();
    }

    private static double number(JTextField field, double fallback) {
        try {
            double value = Double.parseDouble(field.getText().trim());
            return Double.isFinite(value) ? value : fallback;
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** Puts a selection into the controls. */
    private void fill(Examples.Selection selection) {
        playerField.setText(selection.playerName());
        columnBox.setSelectedItem(selection.column());
        kindBox.setSelectedItem(selection.eventKind());
        channelBox.setSelectedItem(selection.channel());
        audienceBox.setSelectedItem(selection.audience());
        textField.setText(selection.chatText());
        templateField.setText(selection.templateName());
        sideField.setText(selection.side());
        objectField.setText(Integer.toString(selection.objectId()));
        xField.setText(Double.toString(selection.cameraX()));
        yField.setText(Double.toString(selection.cameraY()));
        zoomField.setText(Double.toString(selection.zoom()));
        speedField.setText(Double.toString(selection.replaySpeed()));
        modeBox.setSelectedItem(selection.directorMode());
        dumpField.setText(selection.dumpFile());
    }

    Examples.Scenario scenario() {
        Examples.Scenario selected = scenarios.getSelectedValue();
        return selected == null ? Examples.Scenario.PRINT_FRAMES : selected;
    }

    /** The program on screen. */
    String code() {
        return codeArea.getText();
    }

    /** Selects a scenario and fills the controls from the selection, as "Show code for this" does. */
    void show(Request request) {
        updating = true;
        try {
            scenarios.setSelectedValue(request.scenario(), true);
            fill(request.selection());
        } finally {
            updating = false;
        }
        regenerate();
    }

    private void regenerate() {
        if (updating) {
            return;
        }
        Examples.Scenario scenario = scenario();
        descriptionLabel.setText("<html>" + scenario.description() + "</html>");
        playerField.setEnabled(scenario == Examples.Scenario.TRACK_PLAYER);
        columnBox.setEnabled(scenario == Examples.Scenario.TRACK_PLAYER);
        kindBox.setEnabled(scenario == Examples.Scenario.REACT_TO_EVENT);
        boolean chat = scenario == Examples.Scenario.SEND_CHAT;
        channelBox.setEnabled(chat);
        audienceBox.setEnabled(chat && channelBox.getSelectedItem() == ChatChannel.CHAT_CHANNEL_MATCH);
        textField.setEnabled(chat);
        templateField.setEnabled(scenario == Examples.Scenario.QUERY_CATALOG);
        sideField.setEnabled(scenario == Examples.Scenario.QUERY_CATALOG);
        objectField.setEnabled(scenario == Examples.Scenario.INSPECT_OBJECT || scenario == Examples.Scenario.CAMERA_CONTROL);
        boolean camera = scenario == Examples.Scenario.CAMERA_CONTROL;
        xField.setEnabled(camera);
        yField.setEnabled(camera);
        zoomField.setEnabled(camera);
        speedField.setEnabled(scenario == Examples.Scenario.REPLAY_CONTROL);
        modeBox.setEnabled(scenario == Examples.Scenario.DIRECTOR);
        dumpField.setEnabled(scenario == Examples.Scenario.FAKE_SERVER);
        try {
            codeArea.setText(Examples.generate(scenario, selection()));
            codeArea.setCaretPosition(0);
            statusLabel.setText(scenario.fileName() + " - compiles against the library jar and protobuf-java");
        } catch (RuntimeException e) {
            codeArea.setText("// " + e.getMessage());
            statusLabel.setText(" ");
        }
    }

    private void copy() {
        try {
            Toolkit.getDefaultToolkit().getSystemClipboard().setContents(new StringSelection(code()), null);
            statusLabel.setText("copied " + code().length() + " characters");
        } catch (IllegalStateException | java.awt.HeadlessException e) {
            statusLabel.setText("the clipboard is not available: " + e.getMessage());
        }
    }

    private void save() {
        JFileChooser chooser = new JFileChooser();
        chooser.setSelectedFile(new File(scenario().fileName()));
        if (chooser.showSaveDialog(this) != JFileChooser.APPROVE_OPTION) {
            return;
        }
        Path file = chooser.getSelectedFile().toPath();
        try {
            Files.writeString(file, code(), StandardCharsets.UTF_8);
            statusLabel.setText("saved " + file);
        } catch (IOException e) {
            statusLabel.setText("could not save: " + e.getMessage());
        }
    }
}
