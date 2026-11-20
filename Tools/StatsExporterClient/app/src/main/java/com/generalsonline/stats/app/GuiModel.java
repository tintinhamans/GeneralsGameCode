package com.generalsonline.stats.app;

import com.generalsonline.stats.app.Theme.Tone;
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
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.ViewStats;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbySlotState;
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
import java.util.Objects;
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

    static final class EventRow {
        private final StatEvent event;
        private final long frame;
        private final String time;
        private final String player;
        private final int playerIndex;
        private final String kind;
        private final String category;
        private final String name;
        private final String secondary;
        private final long cost;
        private final String location;
        private final String detail;

        EventRow(StatEvent event, long frame, String time, String player, int playerIndex, String kind, String category, String name, String secondary, long cost, String location, String detail) {
            this.event = event;
            this.frame = frame;
            this.time = time;
            this.player = player;
            this.playerIndex = playerIndex;
            this.kind = kind;
            this.category = category;
            this.name = name;
            this.secondary = secondary;
            this.cost = cost;
            this.location = location;
            this.detail = detail;
        }

        StatEvent event() {
            return event;
        }

        long frame() {
            return frame;
        }

        String time() {
            return time;
        }

        String player() {
            return player;
        }

        int playerIndex() {
            return playerIndex;
        }

        String kind() {
            return kind;
        }

        String category() {
            return category;
        }

        String name() {
            return name;
        }

        String secondary() {
            return secondary;
        }

        long cost() {
            return cost;
        }

        String location() {
            return location;
        }

        String detail() {
            return detail;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof EventRow)) {
                return false;
            }
            EventRow other = (EventRow) o;
            return Objects.equals(event, other.event)
                    && frame == other.frame
                    && Objects.equals(time, other.time)
                    && Objects.equals(player, other.player)
                    && playerIndex == other.playerIndex
                    && Objects.equals(kind, other.kind)
                    && Objects.equals(category, other.category)
                    && Objects.equals(name, other.name)
                    && Objects.equals(secondary, other.secondary)
                    && cost == other.cost
                    && Objects.equals(location, other.location)
                    && Objects.equals(detail, other.detail);
        }

        @Override
        public int hashCode() {
            return Objects.hash(event, frame, time, player, playerIndex, kind, category, name, secondary, cost, location, detail);
        }

        @Override
        public String toString() {
            return "EventRow[" + "event=" + event + ", frame=" + frame + ", time=" + time + ", player=" + player + ", playerIndex=" + playerIndex + ", kind=" + kind + ", category=" + category + ", name=" + name + ", secondary=" + secondary + ", cost=" + cost + ", location=" + location + ", detail=" + detail + "]";
        }
    }

    static final class ChatRow {
        private final String received;
        private final String channel;
        private final String sender;
        private final String audience;
        private final String text;
        private final String flags;
        private final int playerIndex;

        ChatRow(String received, String channel, String sender, String audience, String text, String flags, int playerIndex) {
            this.playerIndex = playerIndex;
            this.received = received;
            this.channel = channel;
            this.sender = sender;
            this.audience = audience;
            this.text = text;
            this.flags = flags;
        }

        int playerIndex() {
            return playerIndex;
        }

        String received() {
            return received;
        }

        String channel() {
            return channel;
        }

        String sender() {
            return sender;
        }

        String audience() {
            return audience;
        }

        String text() {
            return text;
        }

        String flags() {
            return flags;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof ChatRow)) {
                return false;
            }
            ChatRow other = (ChatRow) o;
            return Objects.equals(received, other.received)
                    && Objects.equals(channel, other.channel)
                    && Objects.equals(sender, other.sender)
                    && Objects.equals(audience, other.audience)
                    && Objects.equals(text, other.text)
                    && Objects.equals(flags, other.flags);
        }

        @Override
        public int hashCode() {
            return Objects.hash(received, channel, sender, audience, text, flags);
        }

        @Override
        public String toString() {
            return "ChatRow[" + "received=" + received + ", channel=" + channel + ", sender=" + sender + ", audience=" + audience + ", text=" + text + ", flags=" + flags + "]";
        }
    }

    static final class SlotRow {
        private final int slot;
        private final LobbySlot data;

        SlotRow(int slot, LobbySlot data) {
            this.slot = slot;
            this.data = data;
        }

        int slot() {
            return slot;
        }

        LobbySlot data() {
            return data;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof SlotRow)) {
                return false;
            }
            SlotRow other = (SlotRow) o;
            return slot == other.slot
                    && Objects.equals(data, other.data);
        }

        @Override
        public int hashCode() {
            return Objects.hash(slot, data);
        }

        @Override
        public String toString() {
            return "SlotRow[" + "slot=" + slot + ", data=" + data + "]";
        }
    }

    static final class FrameRow {
        private final String received;
        private final String type;
        private final int bytes;
        private final String summary;

        FrameRow(String received, String type, int bytes, String summary) {
            this.received = received;
            this.type = type;
            this.bytes = bytes;
            this.summary = summary;
        }

        String received() {
            return received;
        }

        String type() {
            return type;
        }

        int bytes() {
            return bytes;
        }

        String summary() {
            return summary;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof FrameRow)) {
                return false;
            }
            FrameRow other = (FrameRow) o;
            return Objects.equals(received, other.received)
                    && Objects.equals(type, other.type)
                    && bytes == other.bytes
                    && Objects.equals(summary, other.summary);
        }

        @Override
        public int hashCode() {
            return Objects.hash(received, type, bytes, summary);
        }

        @Override
        public String toString() {
            return "FrameRow[" + "received=" + received + ", type=" + type + ", bytes=" + bytes + ", summary=" + summary + "]";
        }
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
        column("State", String.class, (SlotRow r) -> slotStateText(r.data().getState())),
        column("Name", String.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getName() : ""),
        column("Side", String.class, (SlotRow r) -> CellText.lobbySide(r.data(), this.lobbyPredictionValid, this.predictionCheckOk())),
        column("Color", ColorCell.class, (SlotRow r) -> slotColor(r.data(), false)),
        column("Team", Integer.class, (SlotRow r) -> r.data().hasPlayer() ? displayTeam(r.data().getPlayer().getTeam()) : -1),
        column("Start pos", Integer.class, (SlotRow r) -> emptySlot(r.data()) ? -1 : r.data().getStartPosition()),
        column("Ready", Boolean.class, (SlotRow r) -> r.data().getIsReady()),
        column("Has map", Boolean.class, (SlotRow r) -> r.data().getHasMap()),
        column("Random", String.class, (SlotRow r) -> CellText.lobbyRandomFlags(r.data())),
        column("Predicted color", ColorCell.class, (SlotRow r) -> slotColor(r.data(), true)),
        column("Predicted start", Integer.class, (SlotRow r) -> this.lobbyStartPositionsValid && !emptySlot(r.data()) ? r.data().getPredictedStartPosition() : -1)));

    final RowTableModel<String[]> matchInfo = new RowTableModel<>(200, List.of(
        column("Key", String.class, (String[] r) -> r[0]),
        column("Value", String.class, (String[] r) -> r[1])));

    /** The game's Hello as key/value rows: version, build, pid, reader slot, limits and the capabilities. */
    final RowTableModel<String[]> helloInfo = new RowTableModel<>(40, List.of(
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
    private long droppedBase;
    private volatile List<Integer> selectedObjects = List.of();
    private String matchDetail = "";
    private String mapName = "";
    private String replayName = "";
    private long cameraFrames;
    private String lobbyHeader = "not in a lobby";
    private String lobbyKind = "";
    private String lobbyMap = "";
    private String lobbyHost = "";
    private String lobbyFlags = "";
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

    private static boolean emptySlot(LobbySlot slot) {
        return slot.getState() == LobbySlotState.LOBBY_SLOT_OPEN || slot.getState() == LobbySlotState.LOBBY_SLOT_CLOSED;
    }

    /** "Human", "Open", "Closed", "Easy AI": the slot state as the setup screen would say it. */
    static String slotStateText(LobbySlotState state) {
        switch (state) {
            case LOBBY_SLOT_HUMAN:
                return "Human";
            case LOBBY_SLOT_OPEN:
                return "Open";
            case LOBBY_SLOT_CLOSED:
                return "Closed";
            case LOBBY_SLOT_AI_EASY:
                return "Easy AI";
            case LOBBY_SLOT_AI_MEDIUM:
                return "Medium AI";
            case LOBBY_SLOT_AI_BRUTAL:
                return "Hard AI";
            default:
                return titleCase(strip(state.name(), "LOBBY_SLOT_").replace('_', ' '));
        }
    }

    /**
     * Forgets what the previous source showed, for a window that switches between the live game and a recording: the match
     * data (as a new match does), the lobby, the logs, the status texts and the dropped frames counted so far.
     */
    /** The objects the user has selected in the game, as the latest snapshot says (empty when none). */
    List<Integer> selectedObjects() {
        return selectedObjects;
    }

    void resetForSource() {
        selectedObjects = List.of();
        onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_LOADING).build());
        onLobbyState(LobbyState.getDefaultInstance());
        chat.clear();
        frames.clear();
        match = "-";
        matchDetail = "";
        mapName = "";
        replayName = "";
        ui = "-";
        pid = -1;
        lastError = null;
        lastUpdateMillis = 0;
        cameraFrames = 0;
        droppedBase = state.droppedFrames();
        newMatchHook.run();
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

    /** The game's Hello (version, protocol, reader slot, logic rate) and the frames lost on the way. */
    String gameLine() {
        Hello hello = state.hello().orElse(null);
        String game = hello == null ? "no Hello received"
            : "version " + hello.getGameVersion() + "    Protocol: " + hello.getProtocolVersion()
                + "    Reader: " + (hello.getReaderSlot() + 1) + " of " + hello.getMaxReaders() + "    Logic: " + hello.getLogicFramesPerSecond() + " fps";
        return "Game: " + game + "    Dropped frames: " + droppedFrames();
    }

    /** The number of frames the game dropped for this reader, from the sequence numbers. */
    long droppedFrames() {
        return state.droppedFrames() - droppedBase;
    }

    // The same facts as the lines above, split into the cells of the header.

    /** The state light of the connection: green while frames can arrive, amber while no game is there yet, red once it was lost. */
    Tone connectionTone() {
        if (!connected) {
            return lastUpdateMillis != 0 || connection.startsWith("disconnected") ? Tone.BAD : Tone.WARN;
        }
        return connection.startsWith("replay failed") ? Tone.BAD : Tone.OK;
    }

    String connectionTitle() {
        if (!connected) {
            return lastUpdateMillis != 0 || connection.startsWith("disconnected") ? "Disconnected" : "Waiting for game";
        }
        return connection.startsWith("replay") ? "Recording" : "Live";
    }

    String connectionDetail() {
        if (!connected) {
            return lastUpdateMillis != 0 || connection.startsWith("disconnected") ? "retrying" : "no game running";
        }
        if (connection.startsWith("replay")) {
            String text = connection.startsWith("replaying ") ? connection.substring("replaying ".length()) : connection;
            return replayName.isEmpty() ? text.substring(Math.max(text.lastIndexOf('\\'), text.lastIndexOf('/')) + 1) : replayName;
        }
        return pid >= 0 ? "pid " + pid : "";
    }

    private static String titleCase(String text) {
        return text.isEmpty() ? text : text.charAt(0) + text.substring(1).toLowerCase(java.util.Locale.ROOT);
    }

    String screenText() {
        return ui;
    }

    /** The map as the header shows it, or empty before a match. */
    String mapTitle() {
        return mapName;
    }

    String matchTitle() {
        return mapName.isEmpty() ? "No match yet" : mapName;
    }

    String matchDetailText() {
        return matchDetail.isEmpty() ? " " : matchDetail;
    }

    String gameTimeText() {
        long frame = state.frame();
        return frame < 0 ? "-" : Format.gameTime(frame, state.logicFps());
    }

    String frameText() {
        long frame = state.frame();
        return frame < 0 ? "-" : Long.toString(frame);
    }

    /** The game speed as the header shows it ("1.00x", "paused", "network stall"). */
    String speedValue() {
        return speedText();
    }

    String networkText() {
        return state.networkStalled() ? "stalled " + state.stalledFrames() + " frames" : "ok";
    }

    boolean networkStalled() {
        return state.networkStalled();
    }

    String thisPcText() {
        return String.format("render %.0f fps, logic %.0f fps", state.localRenderFps(), state.localLogicFps());
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

    String lobbyKind() {
        return lobbyKind;
    }

    String lobbyMap() {
        return lobbyMap;
    }

    String lobbyHost() {
        return lobbyHost;
    }

    String lobbyFlags() {
        return lobbyFlags;
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
    public void onHello(Hello message) {
        state.onHello(message);
        helloInfo.replaceAll(helloRows(message));
    }

    @Override
    public void onFramesDropped(long count) {
        state.onFramesDropped(count);
    }

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
        matchDetail = titleCase(strip(message.getPhase().name(), "MATCH_PHASE_")) + ", " + titleCase(strip(message.getMode().name(), "MATCH_MODE_"))
            + (message.getMatchId().isEmpty() ? "" : ", id " + message.getMatchId());
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
        selectedObjects = message.getSelectedObjectIdsList();
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
    public void onViewStats(ViewStats message) {
        state.onViewStats(message);
    }

    @Override
    public void onInterestMap(InterestMap message) {
        state.onInterestMap(message);
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
            audience, message.getText(), String.join(", ", flags), message.getPlayerIndex()));
    }

    @Override
    public void onLobbyState(LobbyState message) {
        state.onLobbyState(message);
        if (!message.getInLobby()) {
            lobbySlots.clear();
            lobbyHeader = "not in a lobby";
            lobbyKind = "";
            lobbyMap = "";
            lobbyHost = "";
            lobbyFlags = "";
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
        lobbyKind = strip(message.getKind().name(), "LOBBY_KIND_");
        lobbyMap = message.getMapDisplayName().isEmpty() ? message.getMapName() : message.getMapDisplayName();
        lobbyHost = host;
        lobbyFlags = (message.getPredictionValid() ? "Prediction valid" : "Prediction not valid") + ", " + (message.getPredictionStartPositionsValid() ? "start positions valid" : "start positions not valid")
            + (message.hasOnline() && !message.getOnline().getLobbyName().isEmpty() ? ", lobby \"" + message.getOnline().getLobbyName() + "\"" : "");
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

    private static List<String[]> helloRows(Hello h) {
        List<String[]> rows = new ArrayList<>();
        rows.add(new String[] {"Game version", h.getGameVersion()});
        rows.add(new String[] {"Game build", h.getGameBuild()});
        rows.add(new String[] {"Process id", Long.toString(u(h.getProcessId()))});
        rows.add(new String[] {"Protocol version", Long.toString(u(h.getProtocolVersion()))});
        rows.add(new String[] {"Logic rate", u(h.getLogicFramesPerSecond()) + " frames per second"});
        rows.add(new String[] {"Reader slot", (h.getReaderSlot() + 1) + " of " + h.getMaxReaders() + " readers"});
        rows.add(new String[] {"Game clock zero", h.getEpochUnixMs() == 0 ? "" : DATE_TIME.format(Instant.ofEpochMilli(h.getEpochUnixMs()).atZone(ZoneId.systemDefault()))});
        rows.add(new String[] {"Request rate", h.getMaxRequestsPerSecond() + " per second, bursts of " + h.getMaxRequestBurst()});
        rows.add(new String[] {"Page size limit", Long.toString(u(h.getMaxPageSize()))});
        rows.add(new String[] {"Outbound queue", u(h.getQueueCapacityBytes()) / 1024 + " KB per reader; further behind loses frames"});
        rows.add(new String[] {"Capabilities", String.join(", ", h.getCapabilitiesList())});
        return rows;
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
