package com.generalsonline.stats.app;

import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.StatEventKind;

import javax.swing.BorderFactory;
import javax.swing.DefaultListCellRenderer;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JFileChooser;
import javax.swing.JLabel;
import javax.swing.JList;
import javax.swing.JPanel;
import javax.swing.JSplitPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.ListSelectionModel;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.Toolkit;
import java.awt.datatransfer.StringSelection;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Objects;

/**
 * The Code tab: pick what you want to do from the list, or choose "Show code for this" in another tab, and read a
 * complete, minimal, runnable program for it that uses the library. The selection controls fill the template with the
 * column, event kind or chat channel you care about; Copy puts the program on the clipboard and Save writes it to a
 * {@code .java} file named like its class.
 */
@SuppressWarnings("serial")
final class CodeTab extends JPanel {
    /** A request from another tab: show this scenario for this selection. */
    static final class Request {
        private final Examples.Scenario scenario;
        private final Examples.Selection selection;

        Request(Examples.Scenario scenario, Examples.Selection selection) {
            this.scenario = scenario;
            this.selection = selection;
        }

        Examples.Scenario scenario() {
            return scenario;
        }

        Examples.Selection selection() {
            return selection;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Request)) {
                return false;
            }
            Request other = (Request) o;
            return Objects.equals(scenario, other.scenario)
                    && Objects.equals(selection, other.selection);
        }

        @Override
        public int hashCode() {
            return Objects.hash(scenario, selection);
        }

        @Override
        public String toString() {
            return "Request[" + "scenario=" + scenario + ", selection=" + selection + "]";
        }
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
                return super.getListCellRendererComponent(list, value instanceof StatEventKind ? Examples.kindName((StatEventKind) value) : value, index, selected, focus);
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
        for (JTextField field : List.of(templateField, sideField, objectField, xField, yField, zoomField, speedField)) {
            field.getDocument().addDocumentListener(typing);
        }
        modeBox.addActionListener(e -> change.run());
        fill(Examples.Selection.defaults());


        codeArea.setEditable(false);
        codeArea.setFont(Theme.mono(12.5f));
        codeArea.setBackground(Theme.FIELD);
        codeArea.setForeground(Theme.TEXT);
        codeArea.setBorder(BorderFactory.createEmptyBorder(10, 12, 10, 12));
        codeArea.setTabSize(4);

        copyButton.addActionListener(e -> copy());
        saveButton.addActionListener(e -> save());
        Ui.primary(copyButton);
        scenarios.setFixedCellHeight(32);
        scenarios.setBackground(Theme.FIELD);
        descriptionLabel.setForeground(Theme.MUTED);
        statusLabel.setForeground(Theme.MUTED);

        JPanel left = new JPanel(new BorderLayout(0, 8));
        left.setOpaque(false);
        left.add(Ui.heading("What do you want to do?"), BorderLayout.NORTH);
        left.add(Ui.scroll(scenarios), BorderLayout.CENTER);
        left.setPreferredSize(new Dimension(300, 400));

        JPanel right = new JPanel(new BorderLayout(0, 10));
        right.setOpaque(false);
        right.add(controls(), BorderLayout.NORTH);
        right.add(Ui.scroll(codeArea), BorderLayout.CENTER);
        right.add(Ui.bar(copyButton, saveButton, statusLabel), BorderLayout.SOUTH);

        JSplitPane split = Ui.split(JSplitPane.HORIZONTAL_SPLIT, left, right, 0.0);
        split.setDividerLocation(300);
        add(Ui.pad(wrap(split)), BorderLayout.CENTER);
        regenerate();
    }

    private static JPanel wrap(JComponent content) {
        JPanel panel = new JPanel(new BorderLayout());
        panel.setOpaque(false);
        panel.add(content, BorderLayout.CENTER);
        return panel;
    }

    /** The choices that fill the program, as captioned fields that wrap; the ones a scenario does not use are dimmed. */
    private JComponent controls() {
        JPanel panel = new JPanel(new BorderLayout(0, 8));
        panel.setOpaque(false);
        descriptionLabel.setFont(Theme.ui(Font.PLAIN, 12.5f));
        panel.add(descriptionLabel, BorderLayout.NORTH);
        panel.add(Ui.bar(Ui.field("Player", playerField), Ui.field("Stat (Players column)", columnBox), Ui.field("Event kind", kindBox),
            Ui.field("Chat channel", channelBox), Ui.field("Audience", audienceBox), Ui.field("Text", textField),
            Ui.field("Template", templateField), Ui.field("Side", sideField), Ui.field("Object id", objectField),
            Ui.field("Camera x", xField), Ui.field("y", yField), Ui.field("zoom", zoomField), Ui.field("Replay speed", speedField), Ui.field("Director mode", modeBox)),
            BorderLayout.CENTER);
        return panel;
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
            mode == null ? defaults.directorMode() : mode);
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
