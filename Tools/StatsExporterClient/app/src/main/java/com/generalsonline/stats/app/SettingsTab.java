package com.generalsonline.stats.app;

import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JFileChooser;
import javax.swing.JPanel;
import javax.swing.JTextField;
import java.awt.BorderLayout;
import java.awt.Dimension;
import java.nio.file.Paths;
import java.util.List;

/**
 * The Settings section: everything that is configuration rather than a question. What the game sends this window (it is the
 * game that applies it, per connection, so the window sends the saved settings again on every connect), how the window finds the
 * game, where recordings go, and how the window starts. Preferences are kept across runs ({@link Prefs}); the card texts say
 * which settings the game applies and which are only saved here.
 */
@SuppressWarnings("serial")
final class SettingsTab extends JPanel {
    /** What the window does for the buttons of this section. */
    interface Actions {
        void reconnect();
    }

    private final JTextField pidField = new JTextField(Prefs.get("pid", ""), 8);
    private final JTextField pipeField = new JTextField(Prefs.get("pipe", ""), 24);
    private final JCheckBox autoConnect = new JCheckBox("Connect when the window opens", Prefs.getBoolean("autoConnect", true));
    private final JTextField folderField = new JTextField(Prefs.get("recordingsDir", ""), 30);
    private final JCheckBox followSelection = new JCheckBox("Inspect whatever is selected in the game", Prefs.getBoolean("followSelection", true));
    private final JComboBox<String> startSection = new JComboBox<>();

    SettingsTab(QueryTab query, GuiModel model, List<String> sections, Actions actions, Runnable cannedChanged) {
        super(new BorderLayout());
        for (JCheckBox box : new JCheckBox[] {autoConnect, followSelection}) {
            box.setOpaque(false);
        }
        for (String title : sections) {
            startSection.addItem(title);
        }
        startSection.setSelectedItem(Prefs.get("startSection", "Players"));
        pidField.setToolTipText("The process id of the game; empty finds a running game by itself");
        pipeField.setToolTipText("A full pipe path such as \\\\.\\pipe\\generalsonline-stats-1234; empty finds it by itself");
        folderField.setToolTipText("Empty means Documents\\GeneralsOnline\\Recordings");

        JButton connect = new JButton("Save and connect now");
        Ui.primary(connect);
        connect.addActionListener(e -> {
            Prefs.put("pid", pidField.getText().trim());
            Prefs.put("pipe", pipeField.getText().trim());
            Prefs.putBoolean("autoConnect", autoConnect.isSelected());
            actions.reconnect();
        });
        autoConnect.addActionListener(e -> Prefs.putBoolean("autoConnect", autoConnect.isSelected()));

        JButton choose = new JButton("Choose...");
        choose.addActionListener(e -> {
            JFileChooser chooser = new JFileChooser(Recorder.folder().toFile().getParentFile());
            chooser.setFileSelectionMode(JFileChooser.DIRECTORIES_ONLY);
            if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
                folderField.setText(chooser.getSelectedFile().getPath());
                saveFolder();
            }
        });
        folderField.addActionListener(e -> saveFolder());
        folderField.addFocusListener(new java.awt.event.FocusAdapter() {
            @Override
            public void focusLost(java.awt.event.FocusEvent e) {
                saveFolder();
            }
        });
        followSelection.addActionListener(e -> {
            Prefs.putBoolean("followSelection", followSelection.isSelected());
            query.setFollowSelection(followSelection.isSelected());
        });
        startSection.addActionListener(e -> Prefs.put("startSection", String.valueOf(startSection.getSelectedItem())));

        JPanel connection = Ui.card("Connection", "Saved on this PC. Leave both empty and the window finds the running game by itself, highest process id first.", new Ui.Form(140)
            .row("Process id", pidField)
            .row("Pipe", pipeField)
            .row("Start", autoConnect)
            .row("", connect));
        JPanel feed = Ui.card("What the game sends this window", "Applied by the game, for this connection only. The game forgets it when the window reconnects, so the window sends your last applied settings again on every connect.", query.feedPanel());
        JPanel recordings = Ui.card("Recordings", "Saved on this PC. Where Record puts its files; a recording is never overwritten.", new Ui.Form(140)
            .row("Folder", folderField, choose));
        JPanel behaviour = Ui.card("This window", "Saved on this PC, used the next time the window opens.", new Ui.Form(140)
            .row("Opens on", startSection)
            .row("Inspector", followSelection));
        JPanel about = Ui.card("About the game", "What the game reported when this window connected: its version, build, limits and what it can do.", sized(Ui.scroll(table(model)), 190));

        javax.swing.JTextArea cannedArea = new javax.swing.JTextArea(Prefs.get("cannedLines", "gl hf\ngg\ngg wp\nbrb"), 5, 24);
        cannedArea.setBackground(Theme.FIELD);
        cannedArea.setBorder(javax.swing.BorderFactory.createEmptyBorder(6, 8, 6, 8));
        cannedArea.addFocusListener(new java.awt.event.FocusAdapter() {
            @Override
            public void focusLost(java.awt.event.FocusEvent e) {
                Prefs.put("cannedLines", cannedArea.getText().trim());
                cannedChanged.run();
            }
        });
        JPanel canned = Ui.card("Quick chat lines", "Saved on this PC. One line per button above the chat box.", Ui.scroll(cannedArea));
        JPanel left = Ui.stack(Ui.GAP, connection, recordings, canned, behaviour);
        JPanel right = Ui.stack(Ui.GAP, feed, about);
        JPanel page = new JPanel(new java.awt.GridLayout(1, 2, Ui.GAP, 0));
        page.setOpaque(false);
        page.add(pin(left));
        page.add(pin(right));
        add(Ui.pageScroll(Ui.pad(page)), BorderLayout.CENTER);
    }

    private static JComponent table(GuiModel model) {
        return GuiTables.table(model.helloInfo, "Waiting for the game to introduce itself. It does so when the window connects.", java.util.Collections.singletonMap("Key", 150), GuiTables.Sorting.NONE);
    }

    private static JComponent sized(JComponent component, int height) {
        component.setPreferredSize(new Dimension(300, height));
        return component;
    }

    private static JPanel pin(JPanel column) {
        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(column, BorderLayout.NORTH);
        return holder;
    }

    private void saveFolder() {
        String text = folderField.getText().trim();
        try {
            if (!text.isEmpty()) {
                Paths.get(text);
            }
            Prefs.put("recordingsDir", text);
        } catch (RuntimeException e) {
            folderField.setText(Prefs.get("recordingsDir", ""));
        }
    }

    /** The process id typed here, or -1. */
    static int savedPid() {
        try {
            return Integer.parseInt(Prefs.get("pid", "").trim());
        } catch (NumberFormatException e) {
            return -1;
        }
    }
}
