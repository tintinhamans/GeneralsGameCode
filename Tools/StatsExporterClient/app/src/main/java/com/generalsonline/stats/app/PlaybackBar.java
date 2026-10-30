package com.generalsonline.stats.app;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JProgressBar;
import java.awt.BorderLayout;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;

/**
 * The banner of the window while it watches a recording: what it is, play or pause, the speed, how far it has played and
 * the way back to the live game. Hidden while the window shows a live game.
 */
@SuppressWarnings("serial")
final class PlaybackBar extends JPanel {
    /** What the controls do; the window implements it. */
    interface Actions {
        void togglePlay();

        void speed(double speed);

        void backToLive();
    }

    private static final String[] SPEED_NAMES = {"0.5x", "1x", "2x", "4x", "8x", "As fast as possible"};
    private static final double[] SPEEDS = {0.5, 1, 2, 4, 8, 0};

    private final JLabel title = new JLabel("Watching a recording");
    private final JLabel file = new JLabel(" ");
    private final JButton playButton = new JButton("Pause");
    private final JComboBox<String> speedBox = new JComboBox<>(SPEED_NAMES);
    private final JProgressBar progress = new JProgressBar(0, 1000);
    private final JButton liveButton = new JButton("Back to live");
    private boolean updating;

    PlaybackBar(Actions actions) {
        super(new BorderLayout(16, 0));
        setBackground(Theme.mix(Theme.SURFACE, Theme.ACCENT, 0.13f));
        setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.HAIRLINE), BorderFactory.createEmptyBorder(8, 18, 8, 18)));
        title.setFont(Theme.semibold(13));
        title.setForeground(Theme.ACCENT);
        file.setFont(Ui.small());
        file.setForeground(Theme.MUTED);
        progress.setPreferredSize(new Dimension(120, 6));
        progress.setMaximumSize(new Dimension(120, 6));
        progress.setBorderPainted(false);
        progress.putClientProperty("FlatLaf.style", "arc: 6; foreground: " + Theme.hex(Theme.ACCENT));
        Ui.primary(playButton);
        playButton.addActionListener(e -> actions.togglePlay());
        speedBox.setToolTipText("How fast the recording plays");
        speedBox.addActionListener(e -> {
            if (!updating) {
                actions.speed(SPEEDS[speedBox.getSelectedIndex()]);
            }
        });
        liveButton.addActionListener(e -> actions.backToLive());

        JPanel text = Ui.stack(1, title, file);
        JPanel controls = Ui.line(playButton, speedBox, progress, liveButton);
        add(text, BorderLayout.CENTER);
        add(controls, BorderLayout.EAST);
        setAlignmentX(LEFT_ALIGNMENT);
        setVisible(false);
    }

    /** Shows or hides the banner for a recording. */
    void show(String fileName, boolean visible) {
        file.setText(fileName);
        setVisible(visible);
    }

    void setSpeed(double speed) {
        updating = true;
        int best = SPEEDS.length - 1;
        for (int i = 0; i < SPEEDS.length; i++) {
            if (SPEEDS[i] == speed) {
                best = i;
            }
        }
        speedBox.setSelectedIndex(best);
        updating = false;
    }

    /** @param status "Playing", "Paused", "Finished" or an error text */
    void update(boolean paused, boolean finished, double fraction, String failure) {
        title.setText(failure != null ? "This recording cannot be played to its end" : finished ? "Watching a recording (finished)" : paused ? "Watching a recording (paused)" : "Watching a recording");
        title.setForeground(failure != null ? Theme.BAD : Theme.ACCENT);
        file.setToolTipText(failure);
        playButton.setText(finished ? "Play again" : paused ? "Play" : "Pause");
        progress.setValue((int) Math.round(fraction * 1000));
    }

    @Override
    protected void paintComponent(Graphics graphics) {
        Graphics2D g = (Graphics2D) graphics.create();
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            super.paintComponent(g);
            g.setColor(Theme.ACCENT);
            g.fillRect(0, 0, 3, getHeight());
        } finally {
            g.dispose();
        }
    }
}
