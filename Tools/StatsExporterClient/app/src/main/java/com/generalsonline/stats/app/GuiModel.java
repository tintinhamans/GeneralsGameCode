package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.PipeDiscovery;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.PredictionCheck;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.MatchSummaryPlayer;
import com.generalsonline.stats.proto.OnlineMatch;
import com.generalsonline.stats.proto.PlayerResult;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

import java.nio.file.Paths;
import java.time.Instant;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.OptionalInt;
import java.util.function.LongSupplier;

import static com.generalsonline.stats.Format.strip;
import static com.generalsonline.stats.Format.u;
import static com.generalsonline.stats.app.RowTableModel.column;

/**
 * Everything the GUI shows, without any Swing component, so it can be driven (and tested) without a display.
 *
 * <p>Threading: every method must be called on one thread. {@link StatsGui} calls them on the Swing event thread, where
 * {@link EdtFeed} delivers the frames the reader thread decoded. {@link #accept} is the single entry for a frame.
 *
 * <p>The MatchInfo/snapshot join, the light/full snapshot merge, the player state and the match lifecycle are the
 * library's {@link MatchState}; this class feeds it every frame and turns what it holds into table rows and status
 * texts. A new match is announced by the state's {@code matchReset} callback, which clears the match-scoped tables.
 */
final class GuiModel implements StatsListener {
    static final int MAX_LOG_ROWS = 3000;
    /** Events younger than this many game seconds are highlighted in the Events tab. */
    static final int RECENT_EVENT_SECONDS = 6;

    private static final DateTimeFormatter CLOCK = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final DateTimeFormatter CLOCK_MILLIS = DateTimeFormatter.ofPattern("HH:mm:ss.SSS");
    private static final DateTimeFormatter DATE_TIME = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    // -- Row types. A player row is the library's PlayerView; the other rows keep the message and derive cells on demand.

    record EventRow(StatEvent event, long frame, String time, String player, int playerIndex, String kind, String category, String name,
                    String secondary, long cost, String location, String detail) {
    }

    record ChatRow(String received, String channel, String sender, String audience, String text, String flags) {
    }

    record SlotRow(int slot, LobbySlot data) {
    }

    record FrameRow(String received, String type, int bytes, String summary) {
    }

    private final LongSupplier clockMillis;

    /** Called when a new match starts, so the owner can drop state of its own (the pending camera frame). */
    private Runnable newMatchHook = () -> {
    };

    private final MatchState state = new MatchState();

    // -- Table models.

    final RowTableModel<PlayerView> players = new RowTableModel<>(64, List.of(
        column("Color", ColorCell.class, (PlayerView r) -> colorCell(r.info(), r.name())),
        column("Name", String.class, this::displayName),
        column("Side", String.class, (PlayerView r) -> r.side()),
        column("Team", Integer.class, (PlayerView r) -> r.info() != null ? displayTeam(r.info().getTeam()) : -1),
        column("Money", Long.class, (PlayerView r) -> u(r.stats().getMoney())),
        column("Income/min", Long.class, (PlayerView r) -> u(r.stats().getMoneyPerMinute())),
        column("Army value", Integer.class, (PlayerView r) -> r.stats().getArmyValue()),
        column("APM", Long.class, (PlayerView r) -> u(r.stats().getApmCurrent())),
        column("Score", Integer.class, (PlayerView r) -> r.stats().getScore()),
        column("Rank", Integer.class, (PlayerView r) -> r.stats().getRankLevel()),
        column("Skill pts", Integer.class, (PlayerView r) -> r.stats().getSkillPoints()),
        column("Power (prod / use)", String.class, (PlayerView r) -> r.stats().getEnergyProduction() + " / " + r.stats().getEnergyConsumption()),
        column("Power sabotaged", String.class, (PlayerView r) -> CellText.sabotageText(r.stats(), state.logicFps())),
        column("Radar", String.class, (PlayerView r) -> CellText.radarText(r.stats())),
        column("Units built", Long.class, (PlayerView r) -> u(r.stats().getUnitsBuilt())),
        column("Units lost", Long.class, (PlayerView r) -> u(r.stats().getUnitsLost())),
        column("Units destroyed", Long.class, (PlayerView r) -> u(r.stats().getUnitsDestroyed())),
        column("Value destroyed", Long.class, (PlayerView r) -> u(r.stats().getValueDestroyed())),
        column("Value lost", Long.class, (PlayerView r) -> u(r.stats().getValueLost())),
        column("Workers", Long.class, (PlayerView r) -> u(r.stats().getWorkers())),
        column("Idle workers", Long.class, (PlayerView r) -> u(r.stats().getIdleWorkers())),
        column("Battle plan", String.class, (PlayerView r) -> CellText.battlePlanText(r.stats())),
        column("Production", String.class, (PlayerView r) -> CellText.productionText(r.stats())),
        column("Heroes", String.class, (PlayerView r) -> CellText.heroesText(r.stats())),
        column("Superweapons", String.class, (PlayerView r) -> CellText.superweaponText(r.stats(), r.frame(), state.logicFps())),
        column("Net (latency / fps / ping)", String.class, (PlayerView r) -> CellText.networkText(r.stats())),
        // Columns added for the column views; they come after the original ones.
        column("State", String.class, PlayerView::stateText),
        column("Slot", Integer.class, (PlayerView r) -> r.info() != null ? r.info().getLobbySlot() + 1 : Integer.MAX_VALUE),
        column("ELO", Integer.class, PlayerView::elo),
        column("Harvesters", Long.class, (PlayerView r) -> u(r.stats().getHarvesters())),
        column("Buildings built", Long.class, (PlayerView r) -> u(r.stats().getBuildingsBuilt())),
        column("Buildings lost", Long.class, (PlayerView r) -> u(r.stats().getBuildingsLost())),
        column("Buildings destroyed", Long.class, (PlayerView r) -> u(r.stats().getBuildingsDestroyed())),
        column("Money earned", Long.class, (PlayerView r) -> u(r.stats().getTotalMoneyEarned())),
        column("Spent units", Long.class, (PlayerView r) -> u(r.stats().getSpentUnits())),
        column("Spent structures", Long.class, (PlayerView r) -> u(r.stats().getSpentStructures())),
        column("Spent upgrades", Long.class, (PlayerView r) -> u(r.stats().getSpentUpgrades())),
        column("Kills (inf/veh/air/str)", String.class, (PlayerView r) -> CellText.categoryCounts(r.stats().getKills())),
        column("Losses (inf/veh/air/str)", String.class, (PlayerView r) -> CellText.categoryCounts(r.stats().getLosses())),
        column("Latency ms", Integer.class, (PlayerView r) -> r.stats().hasNetwork() ? r.stats().getNetwork().getLatencyMs() : -1),
        column("Display FPS", Integer.class, (PlayerView r) -> r.stats().hasNetwork() ? r.stats().getNetwork().getReportedFps() : -1),
        column("Ping ms", Integer.class, (PlayerView r) -> r.stats().hasNetwork() ? r.stats().getNetwork().getPingMs() : -1)));

    final RowTableModel<EventRow> events = new RowTableModel<>(MAX_LOG_ROWS, List.of(
        column("Frame", Long.class, EventRow::frame),
        column("Game time", String.class, EventRow::time),
        column("Player", String.class, EventRow::player),
        column("Kind", String.class, EventRow::kind),
        column("Category", String.class, EventRow::category),
        column("Name", String.class, EventRow::name),
        column("Secondary", String.class, EventRow::secondary),
        column("Cost", Long.class, EventRow::cost),
        column("Location", String.class, EventRow::location),
        column("Detail", String.class, EventRow::detail)));

    final RowTableModel<ChatRow> chat = new RowTableModel<>(MAX_LOG_ROWS, List.of(
        column("Received", String.class, ChatRow::received),
        column("Channel", String.class, ChatRow::channel),
        column("Sender", String.class, ChatRow::sender),
        column("Audience", String.class, ChatRow::audience),
        column("Text", String.class, ChatRow::text),
        column("Flags", String.class, ChatRow::flags)));

    final RowTableModel<SlotRow> lobbySlots = new RowTableModel<>(64, List.of(
        column("Slot", Integer.class, (SlotRow r) -> r.slot() + 1),
        column("State", String.class, (SlotRow r) -> strip(r.data().getState().name(), "LOBBY_SLOT_")),
        column("Name", String.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getName() : ""),
        column("Side", String.class, (SlotRow r) -> CellText.lobbySide(r.data(), this.lobbyPredictionValid, this.predictionCheckOk())),
        column("Color", ColorCell.class, (SlotRow r) -> slotColor(r.data(), false)),
        column("Team", Integer.class, (SlotRow r) -> r.data().hasPlayer() ? displayTeam(r.data().getPlayer().getTeam()) : -1),
        column("Start pos", Integer.class, (SlotRow r) -> r.data().getStartPosition()),
        column("Ready", Boolean.class, (SlotRow r) -> r.data().getIsReady()),
        column("Has map", Boolean.class, (SlotRow r) -> r.data().getHasMap()),
        column("Random", String.class, (SlotRow r) -> CellText.lobbyRandomFlags(r.data())),
        column("Predicted color", ColorCell.class, (SlotRow r) -> slotColor(r.data(), true)),
        column("Predicted start", Integer.class, (SlotRow r) -> this.lobbyStartPositionsValid ? r.data().getPredictedStartPosition() : -1)));

    final RowTableModel<String[]> matchInfo = new RowTableModel<>(200, List.of(
        column("Key", String.class, (String[] r) -> r[0]),
        column("Value", String.class, (String[] r) -> r[1])));

    final RowTableModel<MatchPlayer> observers = new RowTableModel<>(64, List.of(
        column("Name", String.class, MatchPlayer::getName),
        column("Slot", Integer.class, (MatchPlayer r) -> r.getLobbySlot() + 1),
        column("ELO", Integer.class, CellText::elo),
        column("Result", String.class, (MatchPlayer r) -> r.getResult() == PlayerResult.PLAYER_RESULT_UNKNOWN ? "" : PlayerView.resultText(r.getResult()))));

    final RowTableModel<MatchSummaryPlayer> summary = new RowTableModel<>(64, List.of(
        column("Player", String.class, MatchSummaryPlayer::getPlayerName),
        column("Side", String.class, MatchSummaryPlayer::getSide),
        column("Score", Integer.class, MatchSummaryPlayer::getScore),
        column("Winner", Boolean.class, MatchSummaryPlayer::getIsWinner),
        column("Observer", Boolean.class, MatchSummaryPlayer::getIsObserver),
        column("Units built", Long.class, (MatchSummaryPlayer r) -> u(r.getUnitsBuilt())),
        column("Units lost", Long.class, (MatchSummaryPlayer r) -> u(r.getUnitsLost())),
        column("Units destroyed", Long.class, (MatchSummaryPlayer r) -> u(r.getUnitsDestroyed())),
        column("Buildings built", Long.class, (MatchSummaryPlayer r) -> u(r.getBuildingsBuilt())),
        column("Buildings lost", Long.class, (MatchSummaryPlayer r) -> u(r.getBuildingsLost())),
        column("Buildings destroyed", Long.class, (MatchSummaryPlayer r) -> u(r.getBuildingsDestroyed())),
        column("Money earned", Long.class, (MatchSummaryPlayer r) -> u(r.getTotalMoneyEarned()))));

    final RowTableModel<FrameRow> frames = new RowTableModel<>(MAX_LOG_ROWS, List.of(
        column("Received", String.class, FrameRow::received),
        column("Type", String.class, FrameRow::type),
        column("Body bytes", Integer.class, FrameRow::bytes),
        column("Summary", String.class, FrameRow::summary)));

    // -- State shown in the status bar and the Lobby tab header.

    private final List<FrameRow> pendingFrameRows = new ArrayList<>();
    private int batchDepth;
    private String connection = "waiting for a game";
    private boolean connected;
    private int pid = -1;
    private long lastUpdateMillis;
    private String lastError;
    private String ui = "-";
    private String match = "-";
    private String mapName = "";
    private String replayName = "";
    private long cameraFrames;
    private String lobbyHeader = "not in a lobby";
    private boolean lobbyPredictionValid;
    private boolean lobbyStartPositionsValid;

    GuiModel() {
        this(System::currentTimeMillis);
    }

    /** @param clockMillis wall clock in milliseconds; tests pass a controllable one */
    GuiModel(LongSupplier clockMillis) {
        this.clockMillis = clockMillis;
        state.addListener(new StateListener() {
            @Override
            public void matchReset() {
                startNewMatch();
            }
        });
    }

    void setNewMatchHook(Runnable hook) {
        this.newMatchHook = hook;
    }

    /** The library state this model feeds; the minimap and the code generator read it. */
    MatchState state() {
        return state;
    }

    // -- Status bar texts.

    String connectionLine() {
        return "Pipe: " + connection + "    Screen: " + ui + (lastError == null ? "" : "    Last error: " + lastError);
    }

    String matchLine() {
        return "Match: " + match;
    }

    String progressLine() {
        long frame = state.frame();
        StringBuilder line = new StringBuilder("Frame: ");
        line.append(frame < 0 ? "-" : Long.toString(frame));
        line.append("    Game time: ").append(frame < 0 ? "-" : Format.gameTime(frame, state.logicFps()));
        line.append("    Speed: ").append(speedText());
        line.append("    Network: ").append(state.networkStalled() ? "STALLED " + state.stalledFrames() + " frames" : "ok");
        line.append("    Snapshots: ").append(state.snapshotCount());
        line.append("    This PC: ").append(String.format("render %.0f fps, logic %.0f fps", state.localRenderFps(), state.localLogicFps()));
        return line.toString();
    }

    /** The latest camera frame: where this client looks, and the minimap view box. */
    String cameraLine() {
        Camera camera = state.camera().orElse(null);
        if (camera == null) {
            return "Camera: -";
        }
        return "Camera: " + CellText.cameraText(camera) + "    (" + cameraFrames + " camera frames)";
    }

    /**
     * The ratio is the primary value; the fps is added only when it is not simply the normal logic rate. While the
     * network is stalled the game reports no speed, so the stall is shown instead and the last known speed kept.
     */
    private String speedText() {
        String known = knownSpeedText();
        if (state.networkStalled()) {
            return "network stall (" + (state.stalledFrames() / Math.max(1, state.logicFps())) + "s)" + (state.gameSpeedFps().isEmpty() ? "" : ", last " + known);
        }
        return known;
    }

    private String knownSpeedText() {
        OptionalInt speed = state.gameSpeedFps();
        if (speed.isEmpty()) {
            return "-";
        }
        int fps = speed.getAsInt();
        if (fps < 0) {
            return "uncapped";
        }
        if (fps == 0) {
            return "paused/halted";
        }
        String text = String.format("%.2fx", state.gameSpeedRatio());
        return fps == state.logicFps() ? text : text + " (" + fps + " fps)";
    }

    int logicFps() {
        return state.logicFps();
    }

    String lobbyHeader() {
        return lobbyHeader;
    }

    String predictionCheckText() {
        PredictionCheck check = state.predictionCheck().orElse(null);
        return check == null ? "" : "Lobby prediction " + (check.matched() ? "MATCHED" : "DID NOT MATCH")
            + ", random state " + (check.randomStateClean() ? "clean" : "NOT clean");
    }

    /** True when the engine's resolved values matched the lobby prediction, false when not, null before any check. */
    Boolean predictionCheckOk() {
        return state.predictionCheck().map(PredictionCheck::matched).orElse(null);
    }

    long snapshotCount() {
        return state.snapshotCount();
    }

    /** The window title: the pid of the game, the map and the replay being watched, when known. */
    String windowTitle() {
        StringBuilder title = new StringBuilder("Generals Online stats");
        if (pid >= 0) {
            title.append(" - pid ").append(pid);
        }
        if (!mapName.isEmpty()) {
            title.append(" - ").append(mapName);
        }
        if (!replayName.isEmpty()) {
            title.append(" - ").append(replayName);
        }
        return title.toString();
    }

    /** Non-null while the pipe is down: the tables show the last data, and this says how old it is. */
    String staleText() {
        if (!isStale()) {
            return null;
        }
        long seconds = Math.max(0, (clockMillis.getAsLong() - lastUpdateMillis) / 1000);
        return "Disconnected: the tables are stale, last update " + seconds + " s ago";
    }

    /** True while the data shown is older than the connection. */
    boolean isStale() {
        return !connected && lastUpdateMillis != 0;
    }

    /** "Match over: Alice won, Bob lost" once the final MatchInfo arrived; null before. */
    String resultBanner() {
        if (state.phase().orElse(null) != MatchPhase.MATCH_PHASE_ENDED) {
            return null;
        }
        List<String> parts = new ArrayList<>();
        for (MatchPlayer p : state.matchPlayers()) {
            if (!p.getIsObserver() && p.getResult() != PlayerResult.PLAYER_RESULT_UNKNOWN) {
                parts.add(p.getName() + " " + PlayerView.resultText(p.getResult()));
            }
        }
        return parts.isEmpty() ? "Match over (no decisive result)" : "Match over: " + String.join(", ", parts);
    }

    /** The players of the latest MatchInfo that have a player index (for the Events player filter). */
    List<MatchPlayer> matchPlayers() {
        List<MatchPlayer> withIndex = new ArrayList<>();
        for (MatchPlayer p : state.matchPlayers()) {
            if (p.getPlayerIndex() >= 0) {
                withIndex.add(p);
            }
        }
        return withIndex;
    }

    /** True for an event from the last few game seconds. */
    boolean isRecent(EventRow row) {
        long now = state.currentFrame();
        return row.frame() <= now && now - row.frame() <= (long) RECENT_EVENT_SECONDS * state.logicFps();
    }

    /** The newest game frame seen in a snapshot or an events frame; the minimap fades event marks against it. */
    long currentFrame() {
        return state.currentFrame();
    }

    // -- Batching: a drain of several frames fires one update for the Frames tab.

    void beginBatch() {
        batchDepth++;
    }

    void endBatch() {
        if (--batchDepth <= 0) {
            batchDepth = 0;
            flushFrameRows();
        }
    }

    private void flushFrameRows() {
        if (!pendingFrameRows.isEmpty()) {
            frames.addAll(new ArrayList<>(pendingFrameRows));
            pendingFrameRows.clear();
        }
    }

    // -- The single entry for frames.

    /**
     * Applies one decoded frame: logs it in the Frames tab (the one-line summary was built on the reader thread) and
     * hands it to the matching handler through {@link FrameDispatcher#deliver}, the same path every other consumer uses.
     */
    void accept(DecodedFrame frame, String summaryLine) {
        pendingFrameRows.add(new FrameRow(LocalTime.now().format(CLOCK_MILLIS), frame.typeName(), frame.bodyBytes(), summaryLine));
        try {
            FrameDispatcher.deliver(frame, this);
        } finally {
            if (batchDepth == 0) {
                flushFrameRows();
            }
        }
    }

    // -- Connection.

    @Override
    public void onConnected(String pipePath) {
        OptionalInt id = PipeDiscovery.pidOfPath(pipePath);
        pid = id.isPresent() ? id.getAsInt() : -1;
        connected = true;
        connection = "connected " + (pid >= 0 ? "to pid " + pid + " " : "") + "(" + pipePath + ")";
        lastError = null;
    }

    @Override
    public void onDisconnected(Throwable cause) {
        // Tables are kept so the last match stays readable, but marked stale; a new match clears the live ones.
        connected = false;
        connection = "disconnected, retrying" + (cause == null ? "" : " (" + Format.cause(cause) + ")");
    }

    @Override
    public void onError(int type, Throwable cause) {
        lastError = String.format("frame type 0x%02x: %s", type, Format.cause(cause));
        pendingFrameRows.add(new FrameRow(LocalTime.now().format(CLOCK_MILLIS), "ERROR", 0, lastError));
        if (batchDepth == 0) {
            flushFrameRows();
        }
    }

    /** Free-form connection text, for the replay mode, where the source never goes stale. */
    void setConnection(String text) {
        connection = text;
        connected = true;
    }

    // -- Frames.

    @Override
    public void onUiState(UiState message) {
        state.onUiState(message);
        ui = strip(message.getScreen().name(), "UI_SCREEN_") + (message.getPaused() ? " (paused)" : "");
    }

    @Override
    public void onMatchInfo(MatchInfo message) {
        state.onMatchInfo(message);
        mapName = Format.mapDisplayName(message.getMapName());
        replayName = message.getReplayPath().isEmpty() ? "" : Paths.get(message.getReplayPath()).getFileName().toString();

        // Refresh the join for rows already shown, in place.
        players.replaceAll(state.players());
        List<MatchPlayer> observerRows = new ArrayList<>();
        for (MatchPlayer p : message.getPlayersList()) {
            if (p.getIsObserver()) {
                observerRows.add(p);
            }
        }
        observers.replaceAll(observerRows);

        int logicFps = state.logicFps();
        match = strip(message.getPhase().name(), "MATCH_PHASE_") + "  " + strip(message.getMode().name(), "MATCH_MODE_")
            + "  map " + message.getMapName() + "  id " + message.getMatchId();
        if (message.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            match += "  duration " + Format.gameTime(u(message.getDurationFrames()), logicFps) + (message.getCompleted() ? "  completed" : "");
        }
        if (message.getHasPredictionCheck()) {
            lobbySlots.refreshAll();
        }
        matchInfo.replaceAll(matchRows(message, logicFps));
        if (message.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            players.refreshAll();
        }
    }

    /** The state began a new match: forget the tables of the previous one and tell the owner (the pending camera frame). */
    private void startNewMatch() {
        players.clear();
        events.clear();
        summary.clear();
        observers.clear();
        cameraFrames = 0;
        newMatchHook.run();
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        lastUpdateMillis = clockMillis.getAsLong();
        state.onStatsSnapshot(message);
        // A stall snapshot has no players; it must not clear the table. One update event for the whole snapshot (in place when the set of players is unchanged).
        if (message.getPlayersCount() > 0) {
            players.replaceAll(state.players());
        }
    }

    @Override
    public void onEvents(Events message) {
        lastUpdateMillis = clockMillis.getAsLong();
        List<PlayerView> before = state.players();
        state.onEvents(message);
        List<EventRow> rows = new ArrayList<>();
        for (StatEvent e : message.getEventsList()) {
            rows.add(eventRow(e));
        }
        events.addAll(rows);
        if (state.players() != before) {
            // A defeated or left event changed a player's state before the next snapshot.
            players.replaceAll(state.players());
        }
    }

    @Override
    public void onCamera(Camera message) {
        onCamera(message, 1);
    }

    /** @param frames how many camera frames this one stands for (the feed keeps only the newest of a burst) */
    void onCamera(Camera message, long frames) {
        state.onCamera(message);
        cameraFrames += frames;
    }

    @Override
    public void onMatchSummary(MatchSummary message) {
        state.onMatchSummary(message);
        summary.replaceAll(message.getPlayersList());
    }

    @Override
    public void onChatMessage(ChatMessage message) {
        state.onChatMessage(message);
        List<String> flags = new ArrayList<>();
        if (message.getIsLocal()) {
            flags.add("local");
        }
        if (message.getIsSystem()) {
            flags.add("system");
        }
        if (message.getIsAction()) {
            flags.add("action");
        }
        // The audience is only defined for in-match chat; for the other channels it would always read EVERYONE.
        String audience = message.getChannel() == ChatChannel.CHAT_CHANNEL_MATCH ? strip(message.getAudience().name(), "CHAT_AUDIENCE_") : "";
        chat.add(new ChatRow(LocalTime.now().format(CLOCK), strip(message.getChannel().name(), "CHAT_CHANNEL_"), message.getSenderName(),
            audience, message.getText(), String.join(", ", flags)));
    }

    @Override
    public void onLobbyState(LobbyState message) {
        state.onLobbyState(message);
        if (!message.getInLobby()) {
            lobbySlots.clear();
            lobbyHeader = "not in a lobby";
            return;
        }
        lobbyPredictionValid = message.getPredictionValid();
        lobbyStartPositionsValid = message.getPredictionStartPositionsValid();
        List<SlotRow> rows = new ArrayList<>();
        String host = message.getHostUserId() != 0 ? "user " + message.getHostUserId() : "-";
        for (int i = 0; i < message.getSlotsCount(); i++) {
            LobbySlot slot = message.getSlots(i);
            rows.add(new SlotRow(i, slot));
            if (slot.getIsHost() && slot.hasPlayer()) {
                host = slot.getPlayer().getName();
            }
        }
        lobbySlots.replaceAll(rows);
        lobbyHeader = strip(message.getKind().name(), "LOBBY_KIND_") + "    map " + (message.getMapDisplayName().isEmpty() ? message.getMapName() : message.getMapDisplayName()) + "    host " + host
            + "    prediction valid: " + message.getPredictionValid() + "    start positions valid: " + message.getPredictionStartPositionsValid()
            + (message.hasOnline() && !message.getOnline().getLobbyName().isEmpty() ? "    lobby \"" + message.getOnline().getLobbyName() + "\"" : "");
    }

    @Override
    public void unknownFrame(int type, byte[] body) {
        // Visible in the Frames tab only.
    }

    @Override
    public void decodeError(int type, byte[] body, Exception cause) {
        // Visible in the Frames tab only.
    }

    // -- Helpers.

    /** Teams are 0-based on the wire and 1-based on screen; unknown (-1) stays -1 and shows blank. */
    private static int displayTeam(int team) {
        return team >= 0 ? team + 1 : -1;
    }

    /** The name with a marker for computer players and observers. */
    private String displayName(PlayerView row) {
        MatchPlayer info = row.info();
        if (info == null) {
            return row.name();
        }
        if (info.getIsObserver()) {
            return info.getName() + " [Obs]";
        }
        return info.getIsHuman() ? info.getName() : info.getName() + " [AI]";
    }

    private static ColorCell colorCell(MatchPlayer info, String name) {
        return info == null ? new ColorCell(0, -1, name) : new ColorCell(info.getColor(), displayTeam(info.getTeam()), name);
    }

    /** The slot's chosen color, or its predicted one (null when the lobby has no valid prediction or none for this slot). */
    private ColorCell slotColor(LobbySlot slot, boolean predicted) {
        if (!slot.hasPlayer()) {
            return null;
        }
        MatchPlayer p = slot.getPlayer();
        if (!predicted) {
            // 0 while the color is random: shown as "?", not as no color
            return new ColorCell(p.getColor(), displayTeam(p.getTeam()), p.getName(), p.getColor() == 0 && p.getIsColorRandom());
        }
        if (!lobbyPredictionValid || slot.getPredictedColor() == 0) {
            return null;
        }
        return new ColorCell(slot.getPredictedColor(), displayTeam(p.getTeam()), p.getName());
    }

    private EventRow eventRow(StatEvent e) {
        int logicFps = state.logicFps();
        return new EventRow(e, u(e.getFrame()), Format.gameTime(u(e.getFrame()), logicFps), state.playerName(e.getPlayerIndex()), e.getPlayerIndex(),
            strip(e.getKind().name(), "STATEVENT_"), e.getCategory(), e.getPrimaryName(), e.getSecondaryName(), u(e.getCost()),
            Format.eventLocation(e), state.eventDetail(e));
    }

    private static List<String[]> matchRows(MatchInfo m, int logicFps) {
        List<String[]> rows = new ArrayList<>();
        rows.add(new String[] {"Phase", strip(m.getPhase().name(), "MATCH_PHASE_")});
        rows.add(new String[] {"Match id", m.getMatchId()});
        rows.add(new String[] {"Mode", strip(m.getMode().name(), "MATCH_MODE_")});
        rows.add(new String[] {"Map", m.getMapName() + (m.getMapIsUserMap() ? " (user map)" : "")});
        rows.add(new String[] {"Started", m.getStartUnixTime() == 0 ? "" : DATE_TIME.format(Instant.ofEpochSecond(m.getStartUnixTime()).atZone(ZoneId.systemDefault()))});
        if (m.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            rows.add(new String[] {"Duration", Format.gameTime(u(m.getDurationFrames()), logicFps) + " (" + u(m.getDurationFrames()) + " frames)"});
        }
        rows.add(new String[] {"Replay file", m.getReplayPath() + (m.getReplayIsPlayback() ? " (being played back)" : "")});
        rows.add(new String[] {"Completed", Boolean.toString(m.getCompleted())});
        rows.add(new String[] {"Local player index", Integer.toString(m.getLocalPlayerIndex())});
        rows.add(new String[] {"Logic rate", (m.getLogicFramesPerSecond() > 0 ? m.getLogicFramesPerSecond() : MatchState.DEFAULT_LOGIC_FPS) + " frames per second"});
        if (m.hasObserverCamera()) {
            rows.add(new String[] {"Observer camera", "start position " + m.getObserverCamera().getStartPosition()
                + ", lobby slot " + m.getObserverCamera().getLobbySlot() + ", player " + m.getObserverCamera().getPlayerIndex()});
        }
        if (m.getHasWorldExtent()) {
            rows.add(new String[] {"World extent", m.getWorldMinX() + ", " + m.getWorldMinY() + " to " + m.getWorldMaxX() + ", " + m.getWorldMaxY()});
        }
        if (m.getHasPredictionCheck()) {
            rows.add(new String[] {"Prediction matched", Boolean.toString(m.getPredictionMatched())});
            rows.add(new String[] {"Random state clean", Boolean.toString(m.getRandomStateClean())});
        }
        if (m.hasOnline()) {
            OnlineMatch o = m.getOnline();
            rows.add(new String[] {"Online match id", o.getMatchId() == 0 ? "not assigned yet" : Long.toUnsignedString(o.getMatchId())});
            rows.add(new String[] {"Lobby", o.getLobbyName() + " (id " + o.getLobbyId() + ", " + strip(o.getLobbyType().name(), "ONLINE_LOBBY_") + ")"});
            rows.add(new String[] {"Region", o.getRegion()});
            rows.add(new String[] {"Starting cash", Long.toString(u(o.getStartingCash()))});
            rows.add(new String[] {"Limit superweapons", Boolean.toString(o.getLimitSuperweapons())});
            rows.add(new String[] {"Track stats", Boolean.toString(o.getTrackStats())});
            rows.add(new String[] {"Allow observers", Boolean.toString(o.getAllowObservers())});
            rows.add(new String[] {"Vanilla teams", Boolean.toString(o.getVanillaTeams())});
            rows.add(new String[] {"Max players", Long.toString(u(o.getMaxPlayers()))});
            rows.add(new String[] {"Max camera height", Long.toString(u(o.getMaxCameraHeight()))});
            rows.add(new String[] {"Official map", Boolean.toString(o.getMapOfficial())});
        }
        return rows;
    }
}
