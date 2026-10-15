package com.generalsonline.stats.app;

import com.generalsonline.stats.DirectorConfig;
import com.generalsonline.stats.DirectorLogic;
import com.generalsonline.stats.DirectorMode;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.SpinnerNumberModel;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.time.Duration;

/**
 * The Director tab: runs the library's automatic camera director against the connected game. Pick a mode, press Start;
 * the panel shows the state (a human taking the camera pauses it, and that is shown in orange with a button to take it
 * back at once), the place it went to last and why, and the problem if the game refused. The tunables apply while it runs.
 */
@SuppressWarnings("serial")
final class DirectorTab extends JPanel {
    private final DirectorModel model;
    private final JComboBox<DirectorMode> modeBox = new JComboBox<>(DirectorMode.values());
    private final JButton startButton = new JButton("Start");
    private final JButton pauseButton = new JButton("Pause");
    private final JButton resumeButton = new JButton("Resume now");
    private final JSpinner minDwell = seconds(5, 0, 300);
    private final JSpinner switchRatio = decimal(1.5, 1, 20, 0.1);
    private final JSpinner minScore = decimal(150, 0, 100000, 10);
    private final JSpinner fightScore = decimal(400, 0, 100000, 10);
    private final JSpinner quietDelay = seconds(4, 0, 300);
    private final JSpinner panLimit = decimal(1500, 0, 100000, 100);
    private final JSpinner highlightLead = seconds(4, 0, 60);
    private final JSpinner highlightHold = seconds(3, 0, 60);
    private final JSpinner overviewDwell = seconds(6, 1, 300);
    private final JSpinner resumeDelay = seconds(10, 0, 600);
    private final JCheckBox returnBox = new JCheckBox("return after a highlight", true);
    private final JLabel stateLabel = new JLabel(" ");
    private final JLabel targetLabel = new JLabel(" ");
    private final JLabel pausedBanner = new JLabel(" ");
    private final JLabel problemLabel = new JLabel(" ");
    private final JLabel hintLabel = new JLabel(" ");

    DirectorTab(DirectorModel model) {
        super(new BorderLayout());
        this.model = model;
        JPanel form = new JPanel(new GridBagLayout());
        form.setBorder(BorderFactory.createTitledBorder("Mode and tunables"));
        GridBagConstraints c = new GridBagConstraints();
        c.anchor = GridBagConstraints.WEST;
        c.insets = new Insets(2, 6, 2, 6);
        int row = 0;
        add(form, c, row++, "Mode", modeBox, "Minimum time on a hotspot (s)", minDwell);
        add(form, c, row++, "A new hotspot must score x times the current", switchRatio, "Smallest score worth a visit", minScore);
        add(form, c, row++, "Score that counts as a fight (overview hands over)", fightScore, "Quiet for this long before the overview tour (s)", quietDelay);
        add(form, c, row++, "Pan up to this far, cut beyond (units)", panLimit, "Be in place before an impact (s)", highlightLead);
        add(form, c, row++, "Stay after a highlight (s)", highlightHold, "Each stop of the overview tour (s)", overviewDwell);
        add(form, c, row++, "Take over again after a human moved the camera (s)", resumeDelay, "", returnBox);
        modeBox.setSelectedItem(DirectorMode.DIRECTOR);

        startButton.addActionListener(e -> toggleRunning());
        pauseButton.addActionListener(e -> {
            model.pause();
            refresh();
        });
        resumeButton.addActionListener(e -> {
            model.resume();
            refresh();
        });
        modeBox.addActionListener(e -> model.apply(config()));
        for (JSpinner spinner : new JSpinner[] {minDwell, switchRatio, minScore, fightScore, quietDelay, panLimit, highlightLead, highlightHold, overviewDwell, resumeDelay}) {
            spinner.addChangeListener(e -> model.apply(config()));
        }
        returnBox.addActionListener(e -> model.apply(config()));

        stateLabel.setFont(stateLabel.getFont().deriveFont(Font.BOLD, 15f));
        pausedBanner.setFont(pausedBanner.getFont().deriveFont(Font.BOLD, 13f));
        hintLabel.setText("<html>The director needs the game to be an observer or a replay. It switches the interest feed on for this connection. "
            + "Changes to the tunables apply at once.</html>");
        JPanel status = new JPanel(new GridBagLayout());
        status.setBorder(BorderFactory.createTitledBorder("What it is doing"));
        GridBagConstraints s = new GridBagConstraints();
        s.anchor = GridBagConstraints.NORTHWEST;
        s.insets = new Insets(2, 8, 2, 8);
        s.gridx = 0;
        s.weightx = 1;
        s.fill = GridBagConstraints.HORIZONTAL;
        s.gridy = 0;
        status.add(stateLabel, s);
        s.gridy = 1;
        status.add(targetLabel, s);
        s.gridy = 2;
        status.add(pausedBanner, s);
        s.gridy = 3;
        status.add(problemLabel, s);
        s.gridy = 4;
        s.weighty = 1;
        status.add(new JLabel(" "), s);

        JPanel buttons = PanelKit.row(startButton, pauseButton, resumeButton);
        JPanel top = new JPanel(new BorderLayout());
        top.add(buttons, BorderLayout.NORTH);
        top.add(hintLabel, BorderLayout.SOUTH);
        hintLabel.setBorder(BorderFactory.createEmptyBorder(0, 10, 4, 10));
        JPanel center = new JPanel(new BorderLayout());
        center.add(form, BorderLayout.NORTH);
        center.add(status, BorderLayout.CENTER);
        add(top, BorderLayout.NORTH);
        add(center, BorderLayout.CENTER);
        refresh();
    }

    private static JSpinner seconds(int value, int min, int max) {
        return decimal(value, min, max, 1);
    }

    private static JSpinner decimal(double value, double min, double max, double step) {
        JSpinner spinner = new JSpinner(new SpinnerNumberModel(value, min, max, step));
        ((JSpinner.DefaultEditor) spinner.getEditor()).getTextField().setColumns(6);
        return spinner;
    }

    private static void add(JPanel form, GridBagConstraints c, int row, String leftLabel, java.awt.Component left, String rightLabel, java.awt.Component right) {
        c.gridy = row;
        c.gridx = 0;
        form.add(new JLabel(leftLabel), c);
        c.gridx = 1;
        form.add(left, c);
        c.gridx = 2;
        form.add(new JLabel(rightLabel), c);
        c.gridx = 3;
        form.add(right, c);
    }

    private static double value(JSpinner spinner) {
        return ((Number) spinner.getValue()).doubleValue();
    }

    private static Duration time(JSpinner spinner) {
        return Duration.ofMillis(Math.round(value(spinner) * 1000));
    }

    /** The configuration the controls describe. */
    DirectorConfig config() {
        return DirectorConfig.defaults().toBuilder().mode((DirectorMode) modeBox.getSelectedItem()).minDwell(time(minDwell)).switchRatio(Math.max(1, value(switchRatio)))
            .minScore(value(minScore)).fightScore(value(fightScore)).quietDelay(time(quietDelay)).panLimit(value(panLimit)).highlightLead(time(highlightLead))
            .highlightHold(time(highlightHold)).returnAfterHighlight(returnBox.isSelected()).overviewDwell(time(overviewDwell)).resumeDelay(time(resumeDelay)).build();
    }

    /** The mode and nothing else, for "Show code for this". */
    Examples.Selection selection() {
        return Examples.Selection.defaults().withDirectorMode((DirectorMode) modeBox.getSelectedItem());
    }

    private void toggleRunning() {
        if (model.running()) {
            model.stop();
        } else {
            String why = model.start(config());
            if (why != null) {
                problemLabel.setForeground(PanelKit.BAD);
                problemLabel.setText(why);
            }
        }
        refresh();
    }

    /** Shows what the director is doing; called from a timer and after each button. */
    void refresh() {
        DirectorModel.View view = model.view();
        startButton.setText(view.running() ? "Stop" : "Start");
        pauseButton.setEnabled(view.running() && !view.paused());
        resumeButton.setEnabled(view.running() && (view.paused() || view.pausedByUser() || !view.problem().isEmpty()));
        if (!view.running()) {
            stateLabel.setText("Not running");
            stateLabel.setForeground(Color.GRAY);
        } else {
            stateLabel.setText(stateText(view.state()) + "   mode " + view.mode());
            stateLabel.setForeground(view.pausedByUser() ? PanelKit.WARN : view.paused() ? Color.GRAY : PanelKit.GOOD);
        }
        targetLabel.setText(view.running() ? "Last move: " + (view.target().isEmpty() ? "none yet" : view.target() + "   because " + view.reason()) + "      highlights pending: " + view.pending() : " ");
        if (view.pausedByUser()) {
            pausedBanner.setForeground(PanelKit.WARN);
            pausedBanner.setText("PAUSED BY A HUMAN: someone moved the camera. The director takes over again after " + Math.round(value(resumeDelay)) + " s without input, or press Resume now.");
        } else if (view.paused()) {
            pausedBanner.setForeground(Color.GRAY);
            pausedBanner.setText("Paused. Press Resume now to steer again.");
        } else {
            pausedBanner.setText(" ");
        }
        if (!view.problem().isEmpty()) {
            problemLabel.setForeground(PanelKit.BAD);
            problemLabel.setText("Problem: " + view.problem());
        } else if (!problemLabel.getText().startsWith("no game")) {
            problemLabel.setText(" ");
        }
    }

    private static String stateText(DirectorLogic.State state) {
        return switch (state) {
            case WAITING -> "Waiting for an interest map";
            case HOTSPOT -> "Following a hotspot";
            case HIGHLIGHT -> "Showing a highlight";
            case OVERVIEW -> "Touring the players";
            case PAUSED_BY_USER -> "Paused: a human has the camera";
            case PAUSED -> "Paused";
        };
    }

    // -- For the tests.

    JButton startButton() {
        return startButton;
    }

    JButton resumeButton() {
        return resumeButton;
    }

    JComboBox<DirectorMode> modeBox() {
        return modeBox;
    }

    String stateText() {
        return stateLabel.getText();
    }

    String bannerText() {
        return pausedBanner.getText();
    }

    String targetText() {
        return targetLabel.getText();
    }

    String problemText() {
        return problemLabel.getText();
    }
}
