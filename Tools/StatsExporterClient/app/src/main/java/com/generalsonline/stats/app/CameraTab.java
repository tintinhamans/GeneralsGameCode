package com.generalsonline.stats.app;

import com.generalsonline.stats.CameraMove;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraEasing;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraFollowQuery;
import com.generalsonline.stats.proto.CameraState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.ReplayState;

import javax.swing.Box;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.JTextField;
import java.awt.BorderLayout;
import java.awt.GridLayout;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * The Camera tab: moves the game's camera (a position, zoom, angle and pitch, at once or over a transition), locks it on a
 * unit, steers a replay (pause, speed, fast forward) and picks the player an observer looks through. The game accepts
 * camera and follow commands in any running match (observing a player needs an observer or a replay, replay control a replay); its refusal is shown in amber and bold, apart from mistakes in the
 * input, which are red.
 *
 * <p>Angles and pitch are typed in degrees (the protocol counts radians). An empty field leaves that value as it is.
 */
@SuppressWarnings("serial")
final class CameraTab extends JPanel {
    private final PipeCommands commands;
    private final GuiModel model;

    private final JTextField xField = new JTextField(6);
    private final JTextField yField = new JTextField(6);
    private final JTextField zoomField = new JTextField(5);
    private final JTextField angleField = new JTextField(5);
    private final JTextField pitchField = new JTextField(5);
    private final JTextField durationField = new JTextField("1500", 5);
    private final JComboBox<String> easingBox = new JComboBox<>(new String[] {"Ease in and out", "Constant speed"});
    private final JCheckBox resetFirst = new JCheckBox("reset angle, pitch and zoom first");
    private final JButton moveButton = new JButton("Move");
    private final JButton resetButton = new JButton("Reset camera");
    private final JButton cancelButton = new JButton("Cancel transition");
    private final JButton currentButton = new JButton("Use the current camera");

    private final JTextField followField = new JTextField(8);
    private final JComboBox<String> followMode = new JComboBox<>(new String[] {"Follow (centred)", "Tether (trails the unit)"});
    private final JSpinner tetherDistance = PanelKit.spinner(20, 1, 100, 5);
    private final JCheckBox snapBox = new JCheckBox("jump to the unit");
    private final JButton followButton = new JButton("Follow");
    private final JButton unfollowButton = new JButton("Unfollow");
    private final JButton inspectedButton = new JButton("Use the inspected object");

    private final JButton pauseButton = new JButton("Pause");
    private final JButton resumeButton = new JButton("Resume");
    private final JComboBox<String> speedBox = new JComboBox<>(new String[] {"0.25", "0.5", "1", "2", "4", "8", "16"});
    private final JButton speedButton = new JButton("Set speed");
    private final JCheckBox fastForwardBox = new JCheckBox("fast forward");

    private final JComboBox<QueryTab.PlayerChoice> observeBox = new JComboBox<>();
    private final JButton observeButton = new JButton("Observe");
    private final JButton nobodyButton = new JButton("Nobody (all seeing)");
    private String observeSignature = "";

    private final JLabel liveLabel = Ui.caption(" ");
    private final Ui.Stat liveX = new Ui.Stat("x");
    private final Ui.Stat liveY = new Ui.Stat("y");
    private final Ui.Stat liveZoom = new Ui.Stat("zoom");
    private final Ui.Stat liveAngle = new Ui.Stat("angle");
    private final Ui.Stat livePitch = new Ui.Stat("pitch");
    private final JLabel resultLabel = PanelKit.statusLabel("Commands you send to the game show their result here.");
    private final JPanel cameraSection;
    private final JPanel replaySection;
    private java.util.function.IntSupplier inspected = () -> 0;

    CameraTab(PipeCommands commands, GuiModel model) {
        super(new BorderLayout());
        this.commands = commands;
        this.model = model;

        moveButton.addActionListener(e -> move());
        resetButton.addActionListener(e -> send("camera reset", p -> p.resetCamera(), CameraTab::describeCamera));
        cancelButton.addActionListener(e -> send("transition cancelled", p -> p.cancelCameraTransition(), CameraTab::describeCamera));
        currentButton.addActionListener(e -> useCurrent());
        followButton.addActionListener(e -> follow());
        unfollowButton.addActionListener(e -> send("camera released", p -> p.unfollowUnit(), CameraTab::describeCamera));
        inspectedButton.addActionListener(e -> {
            int id = inspected.getAsInt();
            if (id != 0) {
                followField.setText(Long.toString(Format.u(id)));
            } else {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "no object was inspected yet: open the Query tab and inspect one", false));
            }
        });
        pauseButton.addActionListener(e -> send("replay paused", p -> p.pauseReplay(), CameraTab::describeReplay));
        resumeButton.addActionListener(e -> send("replay resumed", p -> p.resumeReplay(), CameraTab::describeReplay));
        speedButton.addActionListener(e -> {
            double ratio;
            try {
                ratio = Double.parseDouble(String.valueOf(speedBox.getSelectedItem()).trim());
            } catch (NumberFormatException ex) {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "the speed is a number such as 0.5 or 2", false));
                return;
            }
            send("speed set", p -> p.setReplaySpeed(ratio), CameraTab::describeReplay);
        });
        speedBox.setEditable(true);
        fastForwardBox.addActionListener(e -> send("fast forward " + (fastForwardBox.isSelected() ? "on" : "off"), p -> p.setReplayFastForward(fastForwardBox.isSelected()), CameraTab::describeReplay));
        observeButton.addActionListener(e -> {
            QueryTab.PlayerChoice choice = (QueryTab.PlayerChoice) observeBox.getSelectedItem();
            if (choice == null) {
                PanelKit.fail(resultLabel, new PipeCommands.Failure("NO PLAYER", "no match with players yet", false));
                return;
            }
            send("observing " + choice.name(), p -> p.observePlayer(choice.index()), state -> (state.getFullView() ? "the view switched to " : "the game remembered the choice of ") + choice.name());
        });
        nobodyButton.addActionListener(e -> send("observer view reset", p -> p.observeNobody(), state -> "the all-seeing observer view"));

        Ui.primary(moveButton);
        Ui.primary(followButton);
        Ui.primary(speedButton);
        Ui.primary(observeButton);
        resetFirst.setOpaque(false);
        snapBox.setOpaque(false);
        fastForwardBox.setOpaque(false);
        JPanel coordinates = Ui.bar(Ui.field("x", xField), Ui.field("y", yField), Ui.field("zoom", zoomField), Ui.field("angle (degrees)", angleField), Ui.field("pitch (degrees)", pitchField));
        JPanel transition = Ui.bar(Ui.field("transition (ms, 0 = at once)", durationField), Ui.field("easing", easingBox), Ui.field(" ", resetFirst));
        cameraSection = Ui.card("Move the camera", "Leave a field empty to keep that value.", Ui.stack(10, coordinates, transition,
            Ui.bar(moveButton, resetButton, cancelButton, currentButton)));

        JPanel followSection = Ui.card("Follow a unit", "Lock the camera on an object by its id; the Query tab finds ids.", new Ui.Form(110)
            .row("Object id", followField, inspectedButton)
            .row("Mode", followMode)
            .row("Tether distance", tetherDistance)
            .row("Options", snapBox)
            .row("", followButton, unfollowButton));

        JPanel readout = Ui.bar(liveX, Box.createHorizontalStrut(14), liveY, Box.createHorizontalStrut(14), liveZoom, Box.createHorizontalStrut(14), liveAngle, Box.createHorizontalStrut(14), livePitch);
        JPanel liveSection = Ui.card("Camera now", "Live, from the game's camera frames.", Ui.stack(8, readout, liveLabel));
        replaySection = Ui.card("Replay", "The game caps a replay at its logic rate; fast forward goes beyond.", new Ui.Form(110)
            .row("Playback", pauseButton, resumeButton)
            .row("Speed", speedBox, speedButton)
            .row("Options", fastForwardBox));
        JPanel observeSection = Ui.card("Observer view", "Whose units, shroud and radar the view shows.", new Ui.Form(110)
            .row("Player", observeBox, observeButton)
            .row("", nobodyButton));

        JPanel left = Ui.stack(Ui.GAP, cameraSection, followSection);
        JPanel right = Ui.stack(Ui.GAP, liveSection, replaySection, observeSection);
        JPanel columns = new JPanel(new GridLayout(1, 2, Ui.GAP, 0));
        columns.setOpaque(false);
        columns.add(top(left));
        columns.add(top(right));
        JPanel page = new JPanel(new BorderLayout(0, Ui.GAP));
        page.setOpaque(false);
        page.add(resultLabel, BorderLayout.NORTH);
        page.add(columns, BorderLayout.CENTER);
        add(Ui.pageScroll(Ui.pad(page)), BorderLayout.CENTER);
    }

    /** Pins a column of cards to the top of its cell, so a short column does not stretch its cards. */
    private static JPanel top(JPanel column) {
        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(column, BorderLayout.NORTH);
        return holder;
    }

    /** Where "Use the inspected object" gets its id. */
    void inspectedObject(java.util.function.IntSupplier source) {
        this.inspected = source;
    }

    /** Fills the object id and starts the lock, for "Follow in the camera" in the Query tab. */
    void followObject(int objectId) {
        followField.setText(Long.toString(Format.u(objectId)));
        follow();
    }

    JPanel cameraSection() {
        return cameraSection;
    }

    JPanel replaySection() {
        return replaySection;
    }

    // -- Commands.

    private <T> void send(String done, java.util.function.Function<com.generalsonline.stats.StatsPipe, java.util.concurrent.CompletableFuture<T>> call,
                          java.util.function.Function<T, String> describe) {
        PanelKit.info(resultLabel, "asking the game...");
        commands.run(call, result -> {
            PanelKit.good(resultLabel, done + (describe == null ? "" : ": " + describe.apply(result)));
            if (result instanceof ReplayState) {
                fastForwardBox.setSelected(((ReplayState) result).getFastForward());
            }
        }, failure -> PanelKit.fail(resultLabel, failure));
    }

    /** The move the fields describe, or a reason there is none. */
    Optional<CameraMove> buildMove(List<String> problems) {
        double x = PanelKit.optionalDecimal(xField);
        double y = PanelKit.optionalDecimal(yField);
        double zoom = PanelKit.optionalDecimal(zoomField);
        double angle = PanelKit.optionalDecimal(angleField);
        double pitch = PanelKit.optionalDecimal(pitchField);
        for (JTextField field : List.of(xField, yField, zoomField, angleField, pitchField)) {
            if (!field.getText().isBlank() && Double.isNaN(PanelKit.optionalDecimal(field))) {
                problems.add("'" + field.getText().trim() + "' is not a number");
            }
        }
        if (Double.isNaN(x) != Double.isNaN(y) && problems.isEmpty()) {
            problems.add("x and y are set together or not at all");
        }
        long duration = PanelKit.whole(durationField, -1);
        if (duration < 0 || duration > 60000) {
            problems.add("the transition is 0 to 60000 ms");
        }
        if (!problems.isEmpty()) {
            return Optional.empty();
        }
        CameraMove move = CameraMove.set();
        if (!Double.isNaN(x)) {
            move = move.position(x, y);
        }
        if (!Double.isNaN(zoom)) {
            move = move.zoom(zoom);
        }
        if (!Double.isNaN(angle)) {
            move = move.angle(Math.toRadians(angle));
        }
        if (!Double.isNaN(pitch)) {
            move = move.pitch(Math.toRadians(pitch));
        }
        if (resetFirst.isSelected()) {
            move = move.resetFirst();
        }
        if (duration > 0) {
            move = move.over(Duration.ofMillis(duration));
            if (easingBox.getSelectedIndex() == 0) {
                move = move.easeInOut();
            }
        }
        if (move.isEmpty()) {
            problems.add("nothing to move: fill in a position, zoom, angle or pitch, or press Reset camera");
            return Optional.empty();
        }
        return Optional.of(move);
    }

    private void move() {
        List<String> problems = new ArrayList<>();
        Optional<CameraMove> move = buildMove(problems);
        if (move.isEmpty()) {
            PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", String.join("; ", problems), false));
            return;
        }
        send("moving", p -> p.moveCamera(move.get()), CameraTab::describeCamera);
    }

    private void follow() {
        long id = PanelKit.whole(followField, 0);
        if (id <= 0 || id > 0xFFFFFFFFL) {
            PanelKit.fail(resultLabel, new PipeCommands.Failure("INVALID", "enter the object id to follow (a whole number above 0)", false));
            return;
        }
        boolean tether = followMode.getSelectedIndex() == 1;
        CameraFollowQuery.Builder query = CameraFollowQuery.newBuilder().setObjectId((int) id).setMode(tether ? CameraFollowMode.CAMERA_FOLLOW_TETHER : CameraFollowMode.CAMERA_FOLLOW_FOLLOW);
        if (tether) {
            query.setTetherDistance(PanelKit.intOf(tetherDistance));
        } else {
            query.setSnap(snapBox.isSelected());
        }
        send("following object " + id, p -> p.cameraFollow(query.build()), CameraTab::describeCamera);
    }

    /** Fills the position, zoom, angle and pitch fields from the camera the game last reported. */
    void useCurrent() {
        Camera camera = model.state().camera().orElse(null);
        if (camera == null) {
            PanelKit.fail(resultLabel, new PipeCommands.Failure("NO CAMERA", "no camera frame has arrived yet", false));
            return;
        }
        xField.setText(PanelKit.shortNumber(camera.getX()));
        yField.setText(PanelKit.shortNumber(camera.getY()));
        zoomField.setText(PanelKit.shortNumber(camera.getZoom()));
        angleField.setText(PanelKit.shortNumber(Math.toDegrees(camera.getAngle())));
        pitchField.setText(PanelKit.shortNumber(Math.toDegrees(camera.getPitch())));
        PanelKit.info(resultLabel, "filled from the camera frame of the game");
    }

    private static String describeCamera(CameraState s) {
        return String.format("camera at %.0f, %.0f, zoom %.2f, angle %.0f, pitch %.0f degrees", s.getX(), s.getY(), s.getZoom(), Math.toDegrees(s.getAngle()), Math.toDegrees(s.getPitch()))
            + (s.getFollowObjectId() != 0 ? ", locked on object " + Format.u(s.getFollowObjectId()) + " (" + Format.strip(s.getFollowMode().name(), "CAMERA_FOLLOW_").toLowerCase(java.util.Locale.ROOT) + ")" : "")
            + (s.getTransitionActive() ? ", a transition is running" : "");
    }

    private static String describeReplay(ReplayState s) {
        return (s.getPaused() ? "paused" : "playing") + ", speed " + s.getSpeedRatio() + (s.getSpeedCapped() ? " (capped by the game)" : "") + (s.getFastForward() ? ", fast forward" : "");
    }

    // -- Live view of the camera frames.

    /** Refreshes the line that says who moved the camera and what it is locked on; called from a timer. */
    void refresh() {
        Camera c = model.state().camera().orElse(null);
        if (c == null) {
            liveLabel.setText("No camera frames yet");
        } else {
            liveX.setValue(String.format("%.0f", c.getX()));
            liveY.setValue(String.format("%.0f", c.getY()));
            liveZoom.setValue(String.format("%.2f", c.getZoom()));
            liveAngle.setValue(String.format("%.0f", Math.toDegrees(c.getAngle())));
            livePitch.setValue(String.format("%.0f", Math.toDegrees(c.getPitch())));
            liveLabel.setText(String.format("Moved by %s   %d user actions   lock: %s%s%s", CellText.cameraSourceText(c), Integer.toUnsignedLong(c.getUserInputCount()),
                c.getFollowObjectId() == 0 ? "none" : Format.strip(c.getFollowMode().name(), "CAMERA_FOLLOW_").toLowerCase(java.util.Locale.ROOT) + " object " + Format.u(c.getFollowObjectId()),
                c.getTransitionActive() ? "   transition running" : "",
                c.getFollowEnded().getNumber() != 0 ? "   lock ended: " + Format.strip(c.getFollowEnded().name(), "CAMERA_FOLLOW_END_").toLowerCase(java.util.Locale.ROOT) : ""));
        }
        List<QueryTab.PlayerChoice> choices = new ArrayList<>();
        StringBuilder signature = new StringBuilder();
        for (MatchPlayer p : model.matchPlayers()) {
            if (!p.getIsObserver()) {
                choices.add(new QueryTab.PlayerChoice(p.getPlayerIndex(), p.getName()));
                signature.append(p.getPlayerIndex()).append(':').append(p.getName()).append(';');
            }
        }
        if (!signature.toString().equals(observeSignature)) {
            observeSignature = signature.toString();
            observeBox.setModel(new javax.swing.DefaultComboBoxModel<>(choices.toArray(new QueryTab.PlayerChoice[0])));
        }
    }

    // -- For the code generator and the tests.

    /** The camera selection the fields describe, for "Show code for this". */
    Examples.Selection cameraSelection() {
        Examples.Selection base = Examples.Selection.defaults();
        double x = PanelKit.decimal(xField, base.cameraX());
        double y = PanelKit.decimal(yField, base.cameraY());
        double zoom = PanelKit.decimal(zoomField, base.zoom());
        return base.withCamera(x, y, zoom).withObjectId((int) PanelKit.whole(followField, base.objectId()));
    }

    /** The replay selection the speed box describes. */
    Examples.Selection replaySelection() {
        double ratio;
        try {
            ratio = Double.parseDouble(String.valueOf(speedBox.getSelectedItem()).trim());
        } catch (NumberFormatException e) {
            ratio = Examples.Selection.defaults().replaySpeed();
        }
        return Examples.Selection.defaults().withReplaySpeed(Double.isFinite(ratio) ? ratio : Examples.Selection.defaults().replaySpeed());
    }

    JTextField field(String name) {
        switch (name) {
            case "x":
                return xField;
            case "y":
                return yField;
            case "zoom":
                return zoomField;
            case "angle":
                return angleField;
            case "pitch":
                return pitchField;
            case "duration":
                return durationField;
            default:
                return followField;
        }
    }

    JButton button(String name) {
        switch (name) {
            case "move":
                return moveButton;
            case "reset":
                return resetButton;
            case "cancel":
                return cancelButton;
            case "current":
                return currentButton;
            case "follow":
                return followButton;
            case "unfollow":
                return unfollowButton;
            case "pause":
                return pauseButton;
            case "resume":
                return resumeButton;
            case "speed":
                return speedButton;
            case "observe":
                return observeButton;
            default:
                return nobodyButton;
        }
    }

    JComboBox<String> speedBox() {
        return speedBox;
    }

    JComboBox<String> followMode() {
        return followMode;
    }

    JComboBox<QueryTab.PlayerChoice> observeBox() {
        return observeBox;
    }

    JCheckBox fastForwardBox() {
        return fastForwardBox;
    }

    String resultText() {
        return resultLabel.getText();
    }

    String liveText() {
        return liveLabel.getText();
    }

    CameraEasing easing() {
        return easingBox.getSelectedIndex() == 0 ? CameraEasing.CAMERA_EASING_EASE_IN_OUT : CameraEasing.CAMERA_EASING_NONE;
    }
}
