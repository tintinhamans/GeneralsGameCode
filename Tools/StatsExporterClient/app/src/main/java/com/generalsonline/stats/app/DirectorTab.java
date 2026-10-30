package com.generalsonline.stats.app;

import com.generalsonline.stats.DirectorConfig;
import com.generalsonline.stats.DirectorLogic;
import com.generalsonline.stats.DirectorMode;

import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.SpinnerNumberModel;
import java.awt.BorderLayout;
import java.awt.GridLayout;
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
    private String startProblem;
    private final JLabel targetLabel = Ui.caption(" ");
    private final JLabel pausedBanner = PanelKit.statusLabel();
    private final JLabel problemLabel = PanelKit.statusLabel();

    DirectorTab(DirectorModel model) {
        super(new BorderLayout());
        this.model = model;
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

        stateLabel.setFont(Theme.semibold(15));
        stateLabel.setIconTextGap(10);
        Ui.primary(startButton);
        returnBox.setOpaque(false);
        JPanel runCard = Ui.card("Run", "Needs a running match. It switches the interest feed on for this connection; changes apply at once.", new Ui.Form(150)
            .row("Mode", modeBox)
            .row("", startButton, pauseButton, resumeButton));
        JPanel whereCard = Ui.card("Where it looks", "A hotspot is the place with the most going on.", new Ui.Form(250)
            .row("Minimum time on a hotspot (s)", minDwell)
            .row("A new hotspot must score x times the current", switchRatio)
            .row("Smallest score worth a visit", minScore)
            .row("Pan up to this far, cut beyond (units)", panLimit));
        JPanel timingCard = Ui.card("Highlights and the tour", "A highlight is a superweapon launch, a hero death or a big structure loss.", new Ui.Form(250)
            .row("Score of a fight (the overview hands over)", fightScore)
            .row("Quiet for this long before the overview tour (s)", quietDelay)
            .row("Each stop of the overview tour (s)", overviewDwell)
            .row("Be in place before an impact (s)", highlightLead)
            .row("Stay after a highlight (s)", highlightHold)
            .row("Return", returnBox)
            .row("Take over after a human moved the camera (s)", resumeDelay));
        JPanel statusCard = Ui.card("What it is doing", null, Ui.stack(8, stateLabel, targetLabel, pausedBanner, problemLabel));
        JPanel left = Ui.stack(Ui.GAP, runCard, whereCard);
        JPanel right = Ui.stack(Ui.GAP, statusCard, timingCard);
        JPanel columns = new JPanel(new GridLayout(1, 2, Ui.GAP, 0));
        columns.setOpaque(false);
        columns.add(pin(left));
        columns.add(pin(right));
        add(Ui.pageScroll(Ui.pad(columns)), BorderLayout.CENTER);
        refresh();
    }

    /** Pins a column of cards to the top of its cell. */
    private static JPanel pin(JPanel column) {
        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(column, BorderLayout.NORTH);
        return holder;
    }

    private static JSpinner seconds(int value, int min, int max) {
        return decimal(value, min, max, 1);
    }

    private static JSpinner decimal(double value, double min, double max, double step) {
        JSpinner spinner = new JSpinner(new SpinnerNumberModel(value, min, max, step));
        ((JSpinner.DefaultEditor) spinner.getEditor()).getTextField().setColumns(6);
        return spinner;
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

    /** Starts or stops the director, for the Control section and its key. */
    void toggle() {
        toggleRunning();
    }

    boolean isRunning() {
        return model.running();
    }

    /** One line about what the director is doing, for the Control section. */
    String statusLine() {
        DirectorModel.View view = model.view();
        if (!view.running()) {
            return "Not running.";
        }
        return view.pausedByUser() ? "Paused: a human has the camera." : view.paused() ? "Paused." : stateText(view.state()) + ", mode " + view.mode();
    }

    private void toggleRunning() {
        startProblem = null;
        if (model.running()) {
            model.stop();
        } else {
            String why = model.start(config());
            if (why != null) {
                PanelKit.bad(problemLabel, why);
                startProblem = why;
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
            stateLabel.setIcon(new Ui.Dot(Theme.DIM, 9));
        } else {
            stateLabel.setText(stateText(view.state()) + "   mode " + view.mode());
            stateLabel.setIcon(new Ui.Dot(view.pausedByUser() ? Theme.WARN : view.paused() ? Theme.DIM : Theme.GOOD, 9));
        }
        stateLabel.setForeground(view.running() ? Theme.TEXT : Theme.MUTED);
        targetLabel.setText(view.running() ? "Last move: " + (view.target().isEmpty() ? "none yet" : view.target() + "   because " + view.reason()) + "      highlights pending: " + view.pending() : " ");
        if (view.pausedByUser()) {
            PanelKit.warn(pausedBanner, "PAUSED BY A HUMAN: someone moved the camera. The director takes over again after " + Math.round(value(resumeDelay)) + " s without input, or press Resume now.");
        } else if (view.paused()) {
            PanelKit.info(pausedBanner, "Paused. Press Resume now to steer again.");
        } else {
            PanelKit.info(pausedBanner, "");
        }
        if (!view.problem().isEmpty()) {
            PanelKit.bad(problemLabel, "Problem: " + view.problem());
        } else if (startProblem == null) {
            PanelKit.info(problemLabel, "");
        }
    }

    private static String stateText(DirectorLogic.State state) {
        switch (state) {
            case WAITING:
                return "Waiting for an interest map";
            case HOTSPOT:
                return "Following a hotspot";
            case HIGHLIGHT:
                return "Showing a highlight";
            case OVERVIEW:
                return "Touring the players";
            case PAUSED_BY_USER:
                return "Paused: a human has the camera";
            case PAUSED:
                return "Paused";
            default:
                throw new IllegalArgumentException(String.valueOf(state));
        }
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
