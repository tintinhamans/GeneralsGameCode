package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;

import javax.swing.BorderFactory;
import javax.swing.JCheckBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import java.awt.BorderLayout;
import java.awt.Dimension;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

import static com.generalsonline.stats.app.RowTableModel.column;

/**
 * The Minimap tab: the {@link Minimap} with a switch for the interest map overlay and the list of pending highlights.
 * The switch turns the {@code interest} feed on for this connection (the game sends no InterestMap until asked) and shows
 * the heat map and the highlight markers on the map. Without a game (a replay file) it only shows or hides frames a dump
 * already holds.
 */
@SuppressWarnings("serial")
final class MinimapTab extends JPanel {
    private final GuiModel model;
    private final Minimap minimap;
    private final PipeCommands commands;
    private final JCheckBox interestBox = new JCheckBox("Interest map: where the action is, and what is about to happen");
    private final JLabel statusLabel = new JLabel(" ");
    private final GuiTables.HintTable highlightsTable;
    private Consumer<FeedConfig> feedListener = config -> {
    };
    private boolean updating;

    final RowTableModel<InterestHighlight> highlights = new RowTableModel<>(256, List.of(
        column("Id", Long.class, (InterestHighlight h) -> Format.u(h.getId())),
        column("What", String.class, this::kindText),
        column("Player", String.class, (InterestHighlight h) -> playerName(h.getPlayerIndex())),
        column("Place", String.class, (InterestHighlight h) -> String.format("%.0f, %.0f", h.getX(), h.getY())),
        column("Impact", String.class, (InterestHighlight h) -> impactColumn(h)),
        column("Value", Long.class, (InterestHighlight h) -> Format.u(h.getValue())),
        column("Ends in", String.class, (InterestHighlight h) -> endsColumn(h))));

    MinimapTab(GuiModel model, Minimap minimap, PipeCommands commands) {
        super(new BorderLayout());
        this.model = model;
        this.minimap = minimap;
        this.commands = commands;
        highlightsTable = GuiTables.table(highlights, "No highlight pending. Superweapon launches, hero deaths and big structure losses appear here.",
            Map.of("What", 260, "Player", 120, "Impact", 150), GuiTables.Sorting.NONE);
        interestBox.addActionListener(e -> toggle());
        statusLabel.setBorder(BorderFactory.createEmptyBorder(0, 8, 2, 8));

        JPanel bar = new JPanel(new BorderLayout());
        bar.add(PanelKit.row(interestBox), BorderLayout.NORTH);
        bar.add(statusLabel, BorderLayout.SOUTH);
        JScrollPane list = new JScrollPane(highlightsTable);
        list.setBorder(BorderFactory.createTitledBorder("Pending highlights"));
        list.setPreferredSize(new Dimension(400, 120));
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, minimap, list);
        split.setResizeWeight(0.8);
        add(bar, BorderLayout.NORTH);
        add(split, BorderLayout.CENTER);
    }

    /** Called with the feed configuration the game answered when the switch changed it, so the Query tab shows it too. */
    void onFeedConfig(Consumer<FeedConfig> listener) {
        this.feedListener = listener;
    }

    JCheckBox interestBox() {
        return interestBox;
    }

    Minimap minimap() {
        return minimap;
    }

    GuiTables.HintTable highlightsTable() {
        return highlightsTable;
    }

    String statusText() {
        return statusLabel.getText();
    }

    private void toggle() {
        if (updating) {
            return;
        }
        boolean on = interestBox.isSelected();
        if (!commands.available()) {
            minimap.setShowInterest(on);
            PanelKit.info(statusLabel, "display only: no game is connected, so the interest feed cannot be switched; frames in a dump are shown");
            return;
        }
        PanelKit.info(statusLabel, "asking the game to " + (on ? "send" : "stop sending") + " interest maps...");
        commands.run(pipe -> pipe.setFeedConfig(FeedConfigQuery.newBuilder().setInterest(on).build()), config -> {
            minimap.setShowInterest(config.getInterest());
            feedListener.accept(config);
            PanelKit.good(statusLabel, config.getInterest() ? "interest frames every " + config.getInterestIntervalMs() + " ms, grid " + config.getGridResolution() : "interest feed off");
        }, failure -> {
            updating = true;
            interestBox.setSelected(!on);
            updating = false;
            PanelKit.fail(statusLabel, failure);
        });
    }

    private String playerName(int index) {
        return model.state().playerName(index);
    }

    private String kindText(InterestHighlight h) {
        return Minimap.highlightText(h, model.currentFrame(), model.logicFps(), this::playerName).replaceAll(" impact (in|now).*$", "");
    }

    private String impactColumn(InterestHighlight h) {
        String text = Minimap.impactText(h, model.currentFrame(), model.logicFps()).trim();
        return text.isEmpty() ? "-" : text.replace("impact ", "");
    }

    private String endsColumn(InterestHighlight h) {
        long left = Format.u(h.getExpireFrame()) - model.currentFrame();
        return left <= 0 ? "-" : String.format("%.0f s", left / (double) Math.max(1, model.logicFps()));
    }

    /** Refreshes the list and the status line from the state; called from a timer. */
    void refresh() {
        MatchState state = model.state();
        InterestMap map = state.interestMap().orElse(null);
        if (map == null) {
            highlights.clear();
            if (interestBox.isSelected()) {
                PanelKit.info(statusLabel, "waiting for the first interest map (they arrive about twice a second)");
            }
            return;
        }
        List<InterestHighlight> pending = Minimap.pendingHighlights(map, model.currentFrame());
        highlights.replaceAll(pending);
        if (interestBox.isSelected() && !statusLabel.getText().startsWith("asking")) {
            long max = 0;
            for (int score : map.getScoresList()) {
                max = Math.max(max, Integer.toUnsignedLong(score));
            }
            PanelKit.info(statusLabel, "grid " + map.getWidth() + " by " + map.getHeight() + ", highest score " + max + ", " + pending.size() + " highlights pending, half life " + map.getHalfLifeMs() / 1000.0 + " s");
        }
    }
}
