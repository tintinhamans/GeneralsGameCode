package com.generalsonline.stats.app;

import com.generalsonline.stats.CameraMove;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraFollowQuery;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchMode;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.ReplayState;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JToggleButton;
import java.awt.BorderLayout;
import java.awt.Color;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The Control section: what a caster does during a match, in one place with the state always in view. Replay transport like a
 * media player, who to observe as one button per player in the player's colour, the camera with eight saved positions, following
 * what is selected in the game, and the director. Every control that cannot work right now is disabled and the card says why.
 * The keyboard shortcuts (see the sheet at the bottom) work while this window has the focus.
 */
@SuppressWarnings("serial")
final class ControlTab extends JPanel {
    private static final int BOOKMARKS = 8;
    private static final double[] SPEEDS = {0.5, 1, 2, 4, 8, 16};

    private final PipeCommands commands;
    private final GuiModel model;
    private final DirectorTab director;
    private final JLabel result = PanelKit.statusLabel("What you do here shows its result here. Keys: see the sheet below.");

    private final JButton playButton = new JButton("Pause");
    private final JToggleButton fastForward = new JToggleButton("Fast forward");
    private final List<JButton> speedButtons = new ArrayList<>();
    private final JLabel transportNote = Ui.caption(" ");
    private final JPanel observeRow = Ui.bar();
    private final JLabel observeNote = Ui.caption(" ");
    private final JButton allSeeing = new JButton("All seeing");
    private final JButton followSelection = new JButton("Follow selection");
    private final JButton unfollow = new JButton("Release");
    private final JLabel cameraNote = Ui.caption(" ");
    private final JButton directorButton = new JButton("Start director");
    private final JLabel directorNote = Ui.caption(" ");
    private final JButton[] go = new JButton[BOOKMARKS];
    private final JButton[] save = new JButton[BOOKMARKS];
    private final JLabel[] where = new JLabel[BOOKMARKS];
    private boolean paused;
    private double speed = 1;
    private String playersSignature = "";

    ControlTab(PipeCommands commands, GuiModel model, DirectorTab director) {
        super(new BorderLayout());
        this.commands = commands;
        this.model = model;
        this.director = director;

        Ui.primary(playButton);
        playButton.addActionListener(e -> togglePause());
        fastForward.addActionListener(e -> send("fast forward " + (fastForward.isSelected() ? "on" : "off"), p -> p.setReplayFastForward(fastForward.isSelected())));
        for (double value : SPEEDS) {
            JButton button = new JButton(format(value) + "x");
            button.addActionListener(e -> setSpeed(value));
            speedButtons.add(button);
        }
        JPanel transport = Ui.card("Replay", "Plays like a media player. The game caps a replay at its logic rate; fast forward goes beyond.",
            Ui.stack(8, Ui.bar(playButton, fastForward), Ui.bar(speedButtons.toArray(new java.awt.Component[0])), transportNote));

        allSeeing.addActionListener(e -> send("the all-seeing view", p -> p.observeNobody()));
        JPanel observe = Ui.card("Observe", "Whose units, shroud and radar the view shows. Keys 1 to 8 choose a player.", Ui.stack(8, observeRow, Ui.bar(allSeeing), observeNote));

        followSelection.addActionListener(e -> followSelected());
        unfollow.addActionListener(e -> send("camera released", p -> p.unfollowUnit()));
        JPanel follow = Ui.card("Follow", "Lock the camera on the unit you selected in the game.", Ui.stack(8, Ui.bar(followSelection, unfollow), cameraNote));

        directorButton.addActionListener(e -> director.toggle());
        JPanel directorCard = Ui.card("Director", "Lets the program steer the camera. Its settings are in the Director section.", Ui.stack(8, Ui.bar(directorButton), directorNote));

        JPanel marks = new JPanel(new java.awt.GridLayout(2, 4, Ui.GAP, Ui.GAP));
        marks.setOpaque(false);
        for (int i = 0; i < BOOKMARKS; i++) {
            marks.add(bookmark(i));
        }
        JPanel positions = Ui.card("Camera positions", "Save where the camera is, then fly back with one click. Alt+1 to Alt+8 fly, Ctrl+Alt+1 to Ctrl+Alt+8 save.", marks);

        JPanel sheet = Ui.card("Keyboard", "Works while this window has the focus and no text box is being typed in.", sheetBody());

        JPanel left = Ui.stack(Ui.GAP, transport, observe);
        JPanel right = Ui.stack(Ui.GAP, follow, directorCard);
        JPanel columns = new JPanel(new java.awt.GridLayout(1, 2, Ui.GAP, 0));
        columns.setOpaque(false);
        columns.add(pin(left));
        columns.add(pin(right));
        JPanel page = Ui.stack(Ui.GAP, result, columns, positions, sheet);
        add(Ui.pageScroll(Ui.pad(page)), BorderLayout.CENTER);
    }

    private static JPanel pin(JPanel column) {
        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(column, BorderLayout.NORTH);
        return holder;
    }

    private JPanel bookmark(int index) {
        Ui.Surface card = new Ui.Surface(Theme.FIELD, Theme.HAIRLINE, 10);
        card.setLayout(new BorderLayout(0, 6));
        card.setBorder(BorderFactory.createEmptyBorder(8, 10, 8, 10));
        JLabel number = new JLabel(Integer.toString(index + 1));
        number.setFont(Theme.display(20));
        where[index] = Ui.caption("empty");
        go[index] = new JButton("Fly");
        save[index] = new JButton("Save here");
        go[index].addActionListener(e -> flyTo(index));
        save[index].addActionListener(e -> saveBookmark(index));
        JPanel head = Ui.stack(0, number, where[index]);
        card.add(head, BorderLayout.NORTH);
        card.add(Ui.bar(go[index], save[index]), BorderLayout.CENTER);
        return card;
    }

    private JPanel sheetBody() {
        String[][] keys = {{"Space", "Pause or resume a replay"}, {"Alt+Left / Alt+Right", "Slower or faster replay"}, {"F", "Fast forward on or off"},
            {"1 to 8", "Observe that player"}, {"0", "The all-seeing view"}, {"Alt+1 to 8", "Fly to a saved position"}, {"Ctrl+Alt+1 to 8", "Save the camera here"},
            {"Ctrl+R", "Start or stop recording"}, {"Ctrl+1 to 9", "Open a section of the rail"}};
        JPanel grid = new JPanel(new java.awt.GridLayout(0, 3, Ui.GAP, 4));
        grid.setOpaque(false);
        for (String[] key : keys) {
            JLabel k = new JLabel(key[0]);
            k.setFont(Theme.mono(12));
            k.setForeground(Theme.ACCENT);
            JLabel d = Ui.caption(key[1]);
            JPanel cell = new JPanel(new BorderLayout(10, 0));
            cell.setOpaque(false);
            cell.add(k, BorderLayout.WEST);
            cell.add(d, BorderLayout.CENTER);
            grid.add(cell);
        }
        return grid;
    }

    private static String format(double value) {
        return value == Math.rint(value) ? Long.toString((long) value) : Double.toString(value);
    }

    // -- Commands.

    private <T> void send(String done, java.util.function.Function<com.generalsonline.stats.StatsPipe, java.util.concurrent.CompletableFuture<T>> call) {
        PanelKit.info(result, "asking the game...");
        commands.run(call, answer -> {
            if (answer instanceof ReplayState) {
                ReplayState state = (ReplayState) answer;
                paused = state.getPaused();
                speed = state.getSpeedRatio();
                fastForward.setSelected(state.getFastForward());
            }
            PanelKit.good(result, done);
        }, failure -> PanelKit.fail(result, failure));
    }

    /** Space: pause a replay that plays, resume one that is paused. */
    void togglePause() {
        send(paused ? "replay resumed" : "replay paused", p -> paused ? p.resumeReplay() : p.pauseReplay());
    }

    void setSpeed(double value) {
        send("speed " + format(value) + "x", p -> p.setReplaySpeed(value));
    }

    void slower() {
        shift(-1);
    }

    void faster() {
        shift(1);
    }

    private void shift(int direction) {
        int at = 0;
        for (int i = 0; i < SPEEDS.length; i++) {
            if (Math.abs(SPEEDS[i] - speed) < 1e-6) {
                at = i;
            }
        }
        setSpeed(SPEEDS[Math.max(0, Math.min(SPEEDS.length - 1, at + direction))]);
    }

    void toggleFastForward() {
        fastForward.setSelected(!fastForward.isSelected());
        send("fast forward " + (fastForward.isSelected() ? "on" : "off"), p -> p.setReplayFastForward(fastForward.isSelected()));
    }

    /** Key 1 to 8: observe the n-th player of the match; 0 is the all-seeing view. */
    void observeNumber(int number) {
        List<MatchPlayer> players = players();
        if (number == 0) {
            send("the all-seeing view", p -> p.observeNobody());
        } else if (number <= players.size()) {
            observe(players.get(number - 1));
        }
    }

    private void observe(MatchPlayer player) {
        send("observing " + player.getName(), p -> p.observePlayer(player.getPlayerIndex()));
    }

    private void followSelected() {
        List<Integer> selection = model.selectedObjects();
        if (selection.isEmpty()) {
            PanelKit.fail(result, new PipeCommands.Failure("NOTHING SELECTED", "select a unit in the game first", false));
            return;
        }
        CameraFollowQuery query = CameraFollowQuery.newBuilder().setObjectId(selection.get(0)).setMode(CameraFollowMode.CAMERA_FOLLOW_FOLLOW).build();
        send("following object " + Format.u(selection.get(0)), p -> p.cameraFollow(query));
    }

    void flyTo(int index) {
        String saved = Prefs.get("bookmark" + index, "");
        if (saved.isEmpty()) {
            PanelKit.fail(result, new PipeCommands.Failure("EMPTY", "position " + (index + 1) + " is empty: move the camera, then Save here", false));
            return;
        }
        String[] v = saved.split(";");
        CameraMove move = CameraMove.set().position(Double.parseDouble(v[0]), Double.parseDouble(v[1])).zoom(Double.parseDouble(v[2]))
            .angle(Double.parseDouble(v[3])).pitch(Double.parseDouble(v[4])).over(Duration.ofMillis(900)).easeInOut();
        send("flying to position " + (index + 1), p -> p.moveCamera(move));
    }

    void saveBookmark(int index) {
        Camera camera = model.state().camera().orElse(null);
        if (camera == null) {
            PanelKit.fail(result, new PipeCommands.Failure("NO CAMERA", "no camera frame has arrived yet", false));
            return;
        }
        Prefs.put("bookmark" + index, camera.getX() + ";" + camera.getY() + ";" + camera.getZoom() + ";" + camera.getAngle() + ";" + camera.getPitch());
        PanelKit.good(result, "position " + (index + 1) + " saved: " + Math.round(camera.getX()) + ", " + Math.round(camera.getY()));
        refresh();
    }

    // -- State.

    private List<MatchPlayer> players() {
        List<MatchPlayer> list = new ArrayList<>();
        for (MatchPlayer p : model.matchPlayers()) {
            if (!p.getIsObserver()) {
                list.add(p);
            }
        }
        return list;
    }

    /** Called from the window's timer: enables each control when it can work, and says why when it cannot. */
    void refresh() {
        boolean connected = commands.available();
        MatchInfo match = model.state().match().orElse(null);
        boolean running = connected && match != null && match.getPhase() == com.generalsonline.stats.proto.MatchPhase.MATCH_PHASE_STARTED;
        boolean replay = running && match.getMode() == MatchMode.MATCH_MODE_REPLAY;
        boolean observer = replay || (running && isLocalObserver(match));

        String noGame = !connected ? "Connect to a game first." : !running ? "No match is running." : null;
        playButton.setText(paused ? "Play" : "Pause");
        for (java.awt.Component part : new java.awt.Component[] {playButton, fastForward}) {
            part.setEnabled(replay);
        }
        speedButtons.forEach(b -> b.setEnabled(replay));
        for (int i = 0; i < speedButtons.size(); i++) {
            speedButtons.get(i).putClientProperty("FlatLaf.style", Math.abs(SPEEDS[i] - speed) < 1e-6 && replay ? "background: " + Theme.hex(Theme.ACCENT_SOFT) + "; borderColor: " + Theme.hex(Theme.ACCENT) : null);
        }
        transportNote.setText(noGame != null ? noGame : replay ? (paused ? "Paused at " + model.gameTimeText() : "Playing at " + format(speed) + "x, game time " + model.gameTimeText()) : "Not a replay: pausing or slowing a live game would desynchronize it.");

        rebuildObserve(observer);
        allSeeing.setEnabled(observer);
        observeNote.setText(noGame != null ? noGame : observer ? " " : "You are playing: observing needs an observer or a replay.");

        followSelection.setEnabled(running);
        unfollow.setEnabled(running);
        List<Integer> selection = model.selectedObjects();
        cameraNote.setText(noGame != null ? noGame : selection.isEmpty() ? "Nothing is selected in the game." : selection.size() + " selected; the first one is followed.");

        boolean directing = director != null && director.isRunning();
        directorButton.setText(directing ? "Stop director" : "Start director");
        directorButton.setEnabled(running || directing);
        directorNote.setText(noGame != null && !directing ? noGame : director == null ? " " : director.statusLine());

        for (int i = 0; i < BOOKMARKS; i++) {
            String saved = Prefs.get("bookmark" + i, "");
            where[i].setText(saved.isEmpty() ? "empty" : Math.round(Double.parseDouble(saved.split(";")[0])) + ", " + Math.round(Double.parseDouble(saved.split(";")[1])));
            go[i].setEnabled(running && !saved.isEmpty());
            save[i].setEnabled(running);
        }
    }

    private static boolean isLocalObserver(MatchInfo match) {
        for (MatchPlayer p : match.getPlayersList()) {
            if (p.getPlayerIndex() == match.getLocalPlayerIndex()) {
                return p.getIsObserver();
            }
        }
        return match.getLocalPlayerIndex() < 0;
    }

    private void rebuildObserve(boolean enabled) {
        List<MatchPlayer> players = players();
        StringBuilder signature = new StringBuilder(enabled ? "on:" : "off:");
        for (MatchPlayer p : players) {
            signature.append(p.getPlayerIndex()).append(p.getName()).append(p.getColor()).append(';');
        }
        if (signature.toString().equals(playersSignature)) {
            return;
        }
        playersSignature = signature.toString();
        observeRow.removeAll();
        int n = 1;
        for (MatchPlayer player : players) {
            Color color = Theme.legible(Minimap.playerColor(player.getColor(), player.getPlayerIndex()));
            JButton button = new JButton(n++ + "  " + player.getName());
            button.setIcon(new Ui.Dot(color, 9));
            button.setIconTextGap(8);
            button.setEnabled(enabled);
            button.addActionListener(e -> observe(player));
            observeRow.add(button);
        }
        if (players.isEmpty()) {
            observeRow.add(Ui.caption("No players yet."));
        }
        observeRow.revalidate();
        observeRow.repaint();
    }

    @Override
    public String toString() {
        return String.format(Locale.ROOT, "ControlTab[speed=%s]", speed);
    }
}
