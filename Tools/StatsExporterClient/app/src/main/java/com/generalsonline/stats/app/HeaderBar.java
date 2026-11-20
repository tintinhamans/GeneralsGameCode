package com.generalsonline.stats.app;

import com.generalsonline.stats.app.Theme.Tone;

import javax.swing.BorderFactory;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JMenuItem;
import javax.swing.JPanel;
import javax.swing.JPopupMenu;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.BorderLayout;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;

/**
 * The strip across the top of the window: the state of the connection as a light and a word, then the screen the game is
 * on, the match, the game time, the speed and the frames lost on the way, each in a cell of its own, and at the right the
 * recording controls. A cell is as wide as its longest text so far, so nothing is ever cut off and nothing jumps.
 */
@SuppressWarnings("serial")
final class HeaderBar extends JPanel {
    /** What the buttons of the header do; the window implements it. */
    interface Actions {
        void record();

        void stopRecording();

        void openRecording();

        void openRecording(Path file);

        void showFolder();
    }

    private final Cell connection = new Cell("", Theme.semibold(15), false);
    private final Cell screen = new Cell("Screen", Theme.semibold(14), false);
    private final Cell match = new Cell("Match", Theme.semibold(14), true);
    private final Cell time = new Cell("Game time", Theme.display(22), false);
    private final Cell speed = new Cell("Speed", Theme.display(22), false);
    private final Cell dropped = new Cell("Dropped", Theme.display(22), false);
    private final RecordControl record;
    private boolean compact;

    HeaderBar(Actions actions) {
        super(new BorderLayout());
        record = new RecordControl(actions);
        setBackground(Theme.BG);
        setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.HAIRLINE));
        add(group(connection, screen), BorderLayout.WEST);
        add(match, BorderLayout.CENTER);
        add(group(time, speed, dropped, record), BorderLayout.EAST);
    }

    private static JPanel group(Component... cells) {
        JPanel panel = new JPanel();
        panel.setOpaque(false);
        panel.setLayout(new BoxLayout(panel, BoxLayout.X_AXIS));
        for (Component cell : cells) {
            panel.add(cell);
        }
        return panel;
    }

    /** A narrow window gives the screen's cell up and puts the screen in the match's detail line, so the match keeps room for its name. */
    @Override
    public void doLayout() {
        boolean narrow = getWidth() > 0 && getWidth() < 1200;
        if (narrow != compact) {
            compact = narrow;
            screen.setVisible(!narrow);
        }
        super.doLayout();
    }

    /** Takes the facts of the model; called from the window's timer. */
    void update(GuiModel model) {
        Tone tone = model.connectionTone();
        connection.setValue(model.connectionTitle(), Theme.TEXT);
        connection.setDetail(model.connectionDetail());
        connection.setDot(tone.color());
        screen.setValue(prettyScreen(model.screenText()), Theme.TEXT);
        match.setValue(model.matchTitle(), Theme.TEXT);
        match.setDetail(compact ? prettyScreen(model.screenText()) + ", " + model.matchDetailText().trim() : model.matchDetailText());
        time.setValue(model.gameTimeText(), Theme.TEXT);
        time.setDetail("frame " + model.frameText());
        speed.setValue(model.speedValue(), model.networkStalled() ? Theme.WARN : Theme.TEXT);
        speed.setDetail(model.networkStalled() ? "network stalled" : "game speed");
        long lost = model.droppedFrames();
        dropped.setValue(Long.toString(lost), lost > 0 ? Theme.BAD : Theme.TEXT);
        dropped.setDetail(lost > 0 ? "frames lost" : "none lost");
    }

    /**
     * @param canRecord whether a live game is connected, so there is something to record
     * @param recorder the recording in progress, or null
     * @param playing whether a recording is being watched (recording is off then)
     */
    void updateRecording(boolean canRecord, Recorder recorder, boolean playing) {
        record.update(canRecord, recorder, playing);
    }

    /** "ONLINE_GAME_SETUP" becomes "Online game setup". */
    static String prettyScreen(String screen) {
        String text = screen.replace('_', ' ').toLowerCase(Locale.ROOT).trim();
        return text.isEmpty() || text.equals("-") ? "-" : Character.toUpperCase(text.charAt(0)) + text.substring(1);
    }

    /** One element of the strip: a small caption (or a state light), the value in the type that suits it, and a muted detail line. */
    private static final class Cell extends JPanel {
        private final JLabel caption = new JLabel(" ");
        private final JLabel value;
        private final JLabel detail;
        private int widest;
        private final boolean wraps;

        /** @param wraps whether the value (a map name) wraps onto a second line instead of widening the cell */
        Cell(String captionText, Font valueFont, boolean wraps) {
            this.wraps = wraps;
            value = wraps ? new Ui.WrapLabel() : new JLabel(" ");
            detail = wraps ? new Ui.WrapLabel() : new JLabel(" ");
            setOpaque(false);
            setLayout(new BoxLayout(this, BoxLayout.Y_AXIS));
            setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, Theme.HAIRLINE), BorderFactory.createEmptyBorder(9, 14, 9, 14)));
            caption.setText(captionText.isEmpty() ? " " : captionText);
            caption.setFont(Ui.small());
            caption.setForeground(Theme.MUTED);
            value.setFont(valueFont);
            value.setForeground(Theme.TEXT);
            detail.setFont(Ui.small());
            detail.setForeground(Theme.DIM);
            for (JLabel label : new JLabel[] {caption, value, detail}) {
                label.setAlignmentX(Component.LEFT_ALIGNMENT);
                add(label);
            }
        }

        @Override
        public Dimension getPreferredSize() {
            Dimension size = super.getPreferredSize();
            if (wraps) {
                return new Dimension(Math.min(size.width, 340), Math.max(size.height, 68));
            }
            widest = Math.max(widest, size.width);
            return new Dimension(widest, Math.max(size.height, 68));
        }

        @Override
        public Dimension getMinimumSize() {
            return wraps ? new Dimension(140, 68) : getPreferredSize();
        }

        void setValue(String text, Color color) {
            value.setText(text.isEmpty() ? " " : wraps ? "<html>" + PanelKit.escape(text) + "</html>" : text);
            value.setForeground(color);
        }

        void setDetail(String text) {
            detail.setText(text.isEmpty() ? " " : wraps ? "<html>" + PanelKit.escape(text) + "</html>" : text);
        }

        void setDot(Color color) {
            value.setIcon(new Ui.Dot(color, 9));
            value.setIconTextGap(8);
        }
    }

    /** Record, Stop, the recording light with its time and size, and the Recordings menu. */
    private static final class RecordControl extends JPanel {
        private final Actions actions;
        private final JButton recordButton = new JButton("Record");
        private final JLabel indicator = new JLabel(" ");
        private final JButton menuButton = new JButton("Recordings");
        private boolean recording;

        RecordControl(Actions actions) {
            this.actions = actions;
            setOpaque(false);
            setLayout(new BoxLayout(this, BoxLayout.X_AXIS));
            setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, Theme.HAIRLINE), BorderFactory.createEmptyBorder(0, 14, 0, 14)));
            recordButton.setIcon(new Ui.Dot(Theme.BAD, 9));
            recordButton.setIconTextGap(8);
            recordButton.addActionListener(e -> {
                if (recording) {
                    actions.stopRecording();
                } else {
                    actions.record();
                }
            });
            menuButton.addActionListener(e -> showMenu());
            indicator.setFont(Theme.semibold(12));
            indicator.setForeground(Theme.BAD);
            indicator.setVisible(false);
            add(javax.swing.Box.createVerticalGlue());
            add(recordButton);
            add(javax.swing.Box.createHorizontalStrut(8));
            add(indicator);
            add(javax.swing.Box.createHorizontalStrut(8));
            add(menuButton);
            add(javax.swing.Box.createVerticalGlue());
            recordButton.setAlignmentY(CENTER_ALIGNMENT);
            indicator.setAlignmentY(CENTER_ALIGNMENT);
            menuButton.setAlignmentY(CENTER_ALIGNMENT);
        }

        void update(boolean canRecord, Recorder recorder, boolean playing) {
            recording = recorder != null && recorder.isOpen();
            recordButton.setText(recording ? "Stop" : "Record");
            recordButton.setIcon(recording ? new Ui.Dot(Theme.TEXT, 9) : new Ui.Dot(Theme.BAD, 9));
            recordButton.setEnabled(recording || (canRecord && !playing));
            recordButton.setToolTipText(playing ? "Go back to live to record" : canRecord || recording ? "Record the live game to a file" : "Connect to a game to record it");
            if (recording) {
                Ui.danger(recordButton);
                indicator.setText(Recorder.clock(recorder.elapsedMillis()) + "  " + Recorder.size(recorder.bytes()));
                indicator.setIcon(new Ui.Dot(Theme.BAD, 9));
                indicator.setIconTextGap(6);
                indicator.setToolTipText(recorder.file().toString());
            } else {
                recordButton.putClientProperty("FlatLaf.style", null);
                if (recorder != null && recorder.failure() != null) {
                    indicator.setText("Recording stopped: " + recorder.failure());
                    indicator.setIcon(null);
                }
            }
            indicator.setVisible(recording || (recorder != null && recorder.failure() != null));
            recordButton.revalidate();
        }

        private void showMenu() {
            JPopupMenu menu = new JPopupMenu();
            JMenuItem open = new JMenuItem("Open recording...");
            open.addActionListener(e -> actions.openRecording());
            menu.add(open);
            List<Path> recent = Recorder.recent(6);
            if (!recent.isEmpty()) {
                menu.addSeparator();
                for (Path path : recent) {
                    JMenuItem item = new JMenuItem(path.getFileName().toString().replaceAll("\\.dump$", ""));
                    item.setToolTipText(path.toString());
                    item.addActionListener(e -> actions.openRecording(path));
                    menu.add(item);
                }
            }
            menu.addSeparator();
            JMenuItem folder = new JMenuItem("Show the recordings folder");
            folder.addActionListener(e -> actions.showFolder());
            menu.add(folder);
            menu.show(menuButton, 0, menuButton.getHeight() + 2);
        }
    }
}
