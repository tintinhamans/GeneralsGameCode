package com.generalsonline.stats;

import com.generalsonline.stats.proto.BattlePlan;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.MatchSummaryPlayer;
import com.generalsonline.stats.proto.NetworkStats;
import com.generalsonline.stats.proto.OnlineMatch;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.ProductionItem;
import com.generalsonline.stats.proto.ProductionKind;
import com.generalsonline.stats.proto.ProductionQueue;
import com.generalsonline.stats.proto.RadarState;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SuperweaponState;
import com.generalsonline.stats.proto.SuperweaponTimer;
import com.generalsonline.stats.proto.UiState;

import java.time.Instant;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.generalsonline.stats.RowTableModel.column;

/**
 * Everything the GUI shows, without any Swing component, so it can be driven (and tested) without a display.
 *
 * Threading: every method must be called on one thread. {@link StatsGui} calls them on the Swing event thread,
 * after the reader thread has handed each frame over with SwingUtilities.invokeLater.
 *
 * The MatchInfo/Snapshot join: static player data (name, side, color, team) is only in MatchInfo, live numbers are
 * only in StatsSnapshot, and both name a player by player_index. The latest MatchInfo is kept in
 * {@link #playersByIndex}; every snapshot row is looked up there when it is built, and when a newer MatchInfo
 * arrives the existing rows are refreshed in place.
 */
final class GuiModel implements FrameListener {
    static final int DEFAULT_LOGIC_FPS = 30;
    static final int MAX_LOG_ROWS = 3000;
    private static final long CAMERA_ROW_REFRESH_MS = 250;
    private static final int MAX_SHOWN_ITEMS = 3;

    private static final DateTimeFormatter CLOCK = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final DateTimeFormatter CLOCK_MILLIS = DateTimeFormatter.ofPattern("HH:mm:ss.SSS");
    private static final DateTimeFormatter DATE_TIME = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    // -- Row types. A player row keeps the protobuf messages and derives its cells on demand.

    record PlayerRow(PlayerStats stats, MatchPlayer info, long frame) {
        String name() {
            return info != null ? info.getName() : "P" + stats.getPlayerIndex();
        }
    }

    record EventRow(long frame, String time, String player, String kind, String category, String name, String secondary,
                    long cost, String location, String variant) {
    }

    record ChatRow(String received, String channel, String sender, String audience, String text, String flags) {
    }

    record SlotRow(int slot, LobbySlot data) {
    }

    record FrameRow(String received, String type, int bytes, String summary) {
    }

    /** Logic frames per second of the build that sends the feed; 30 until a MatchInfo says otherwise (a high-fps build runs 60). */
    private int logicFps = DEFAULT_LOGIC_FPS;

    // -- Table models.

    final RowTableModel<PlayerRow> players = new RowTableModel<>(64, List.of(
        column("Color", Integer.class, (PlayerRow r) -> r.info() != null ? r.info().getColor() : 0),
        column("Name", String.class, PlayerRow::name),
        column("Side", String.class, (PlayerRow r) -> r.info() != null ? r.info().getSide() : ""),
        column("Team", Integer.class, (PlayerRow r) -> r.info() != null ? r.info().getTeam() : -1),
        column("Money", Long.class, (PlayerRow r) -> u(r.stats().getMoney())),
        column("Income/min", Long.class, (PlayerRow r) -> u(r.stats().getMoneyPerMinute())),
        column("Army value", Integer.class, (PlayerRow r) -> r.stats().getArmyValue()),
        column("APM", Long.class, (PlayerRow r) -> u(r.stats().getApmCurrent())),
        column("Score", Integer.class, (PlayerRow r) -> r.stats().getScore()),
        column("Rank", Integer.class, (PlayerRow r) -> r.stats().getRankLevel()),
        column("Skill pts", Integer.class, (PlayerRow r) -> r.stats().getSkillPoints()),
        column("Power (prod / use)", String.class, (PlayerRow r) -> r.stats().getEnergyProduction() + " / " + r.stats().getEnergyConsumption()),
        column("Power sabotaged", String.class, (PlayerRow r) -> sabotageText(r.stats(), logicFps)),
        column("Radar", String.class, (PlayerRow r) -> radarText(r.stats())),
        column("Units built", Long.class, (PlayerRow r) -> u(r.stats().getUnitsBuilt())),
        column("Units lost", Long.class, (PlayerRow r) -> u(r.stats().getUnitsLost())),
        column("Units destroyed", Long.class, (PlayerRow r) -> u(r.stats().getUnitsDestroyed())),
        column("Value destroyed", Long.class, (PlayerRow r) -> u(r.stats().getValueDestroyed())),
        column("Value lost", Long.class, (PlayerRow r) -> u(r.stats().getValueLost())),
        column("Workers", Long.class, (PlayerRow r) -> u(r.stats().getWorkers())),
        column("Idle workers", Long.class, (PlayerRow r) -> u(r.stats().getIdleWorkers())),
        column("Battle plan", String.class, (PlayerRow r) -> battlePlanText(r.stats())),
        column("Production", String.class, (PlayerRow r) -> productionText(r.stats())),
        column("Heroes", String.class, (PlayerRow r) -> heroesText(r.stats())),
        column("Superweapons", String.class, (PlayerRow r) -> superweaponText(r.stats(), r.frame(), logicFps)),
        column("Net (latency / fps / ping)", String.class, (PlayerRow r) -> networkText(r.stats()))));

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
        column("Detail", String.class, EventRow::variant)));

    final RowTableModel<ChatRow> chat = new RowTableModel<>(MAX_LOG_ROWS, List.of(
        column("Received", String.class, ChatRow::received),
        column("Channel", String.class, ChatRow::channel),
        column("Sender", String.class, ChatRow::sender),
        column("Audience", String.class, ChatRow::audience),
        column("Text", String.class, ChatRow::text),
        column("Flags", String.class, ChatRow::flags)));

    final RowTableModel<SlotRow> lobbySlots = new RowTableModel<>(64, List.of(
        column("Slot", Integer.class, SlotRow::slot),
        column("State", String.class, (SlotRow r) -> strip(r.data().getState().name(), "LOBBY_SLOT_")),
        column("Name", String.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getName() : ""),
        column("Side", String.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getSide() : ""),
        column("Color", Integer.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getColor() : 0),
        column("Team", Integer.class, (SlotRow r) -> r.data().hasPlayer() ? r.data().getPlayer().getTeam() : -1),
        column("Start pos", Integer.class, (SlotRow r) -> r.data().getStartPosition()),
        column("Ready", Boolean.class, (SlotRow r) -> r.data().getIsReady()),
        column("Has map", Boolean.class, (SlotRow r) -> r.data().getHasMap()),
        column("Predicted side", String.class, (SlotRow r) -> r.data().getPredictedSide()),
        column("Predicted color", Integer.class, (SlotRow r) -> r.data().getPredictedColor()),
        column("Predicted start", Integer.class, (SlotRow r) -> r.data().getPredictedStartPosition())));

    final RowTableModel<String[]> matchInfo = new RowTableModel<>(200, List.of(
        column("Key", String.class, (String[] r) -> r[0]),
        column("Value", String.class, (String[] r) -> r[1])));

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

    private final Map<Integer, MatchPlayer> playersByIndex = new HashMap<>();
    private String connection = "waiting for a game";
    private String ui = "-";
    private String match = "-";
    private String matchId = "";
    private long frame = -1;
    private int speedFps = Integer.MIN_VALUE;
    private float speedRatio = 1.0f;
    private boolean stalled;
    private long stalledFrames;
    private long snapshotCount;
    private float localRenderFps;
    private float localLogicFps;
    private Camera camera;
    private long cameraFrames;
    private long cameraRawFrames;
    private FrameRow cameraRow;
    private long cameraRowUpdated;
    private String lobbyHeader = "not in a lobby";
    private String predictionCheck = "";
    private Boolean predictionCheckOk;
    private long framesTotal;
    private long bytesTotal;

    // -- Status bar texts.

    String connectionLine() {
        return "Pipe: " + connection + "    Screen: " + ui;
    }

    String matchLine() {
        return "Match: " + match;
    }

    String progressLine() {
        StringBuilder line = new StringBuilder("Frame: ");
        line.append(frame < 0 ? "-" : Long.toString(frame));
        line.append("    Game time: ").append(frame < 0 ? "-" : gameTime(frame, logicFps));
        line.append("    Speed: ").append(speedText());
        line.append("    Network: ").append(stalled ? "STALLED " + stalledFrames + " frames" : "ok");
        line.append("    Snapshots: ").append(snapshotCount);
        line.append("    This PC: ").append(String.format("render %.0f fps, logic %.0f fps", localRenderFps, localLogicFps));
        return line.toString();
    }

    /** The latest camera frame: where this client looks, and the minimap view box. */
    String cameraLine() {
        if (camera == null) {
            return "Camera: -";
        }
        String box = camera.getHasViewBox() ? String.format("    view box %.0f,%.0f to %.0f,%.0f", camera.getViewTlX(), camera.getViewTlY(), camera.getViewBrX(), camera.getViewBrY()) : "";
        return String.format("Camera: looking at %.0f, %.0f    zoom %.2f    angle %.2f    pitch %.2f%s    (%d camera frames)", camera.getX(), camera.getY(),
            camera.getZoom(), camera.getAngle(), camera.getPitch(), box, cameraFrames);
    }

    /** The ratio is the primary value; the fps is added only when it is not simply the normal logic rate. */
    private String speedText() {
        if (speedFps == Integer.MIN_VALUE) {
            return "-";
        }
        if (speedFps < 0) {
            return "uncapped";
        }
        if (speedFps == 0) {
            return "paused/halted";
        }
        String text = String.format("%.2fx", speedRatio);
        return speedFps == logicFps ? text : text + " (" + speedFps + " fps)";
    }

    int logicFps() {
        return logicFps;
    }

    String lobbyHeader() {
        return lobbyHeader;
    }

    String predictionCheckText() {
        return predictionCheck;
    }

    /** True when the engine's resolved values matched the lobby prediction, false when not, null before any check. */
    Boolean predictionCheckOk() {
        return predictionCheckOk;
    }

    /** Frames and bytes received so far; the window turns the difference between two readings into rates. */
    long framesTotal() {
        return framesTotal;
    }

    long bytesTotal() {
        return bytesTotal;
    }

    long snapshotCount() {
        return snapshotCount;
    }

    // -- Raw frame log (the Frames tab and the footer counters).

    void onRawFrame(RawFrame raw) {
        FrameType type = FrameType.outboundFromId(raw.type());
        String name = type != null ? type.name() : String.format("UNKNOWN 0x%02x", raw.type());
        framesTotal++;
        bytesTotal += raw.body().length + FrameCodec.HEADER_BYTES;
        if (type == FrameType.CAMERA) {
            // Up to 120 camera frames a second would bury everything else: one row is kept and refreshed a few times a second.
            cameraRawFrames++;
            long nowMs = System.currentTimeMillis();
            if (cameraRow == null || frames.indexOfRow(cameraRow) < 0) {
                cameraRow = new FrameRow(LocalTime.now().format(CLOCK_MILLIS), name, raw.body().length, cameraSummary(raw));
                frames.add(cameraRow);
                cameraRowUpdated = nowMs;
            } else if (nowMs - cameraRowUpdated >= CAMERA_ROW_REFRESH_MS) {
                FrameRow updated = new FrameRow(LocalTime.now().format(CLOCK_MILLIS), name, raw.body().length, cameraSummary(raw));
                frames.set(frames.indexOfRow(cameraRow), updated);
                cameraRow = updated;
                cameraRowUpdated = nowMs;
            }
            return;
        }
        frames.add(new FrameRow(LocalTime.now().format(CLOCK_MILLIS), name, raw.body().length, summarize(raw)));
    }

    private String cameraSummary(RawFrame raw) {
        return "(collapsed, " + cameraRawFrames + " camera frames so far) " + summarize(raw);
    }

    /** One line describing a frame, for the Frames tab. */
    static String summarize(RawFrame raw) {
        StringBuilder text = new StringBuilder();
        FrameDispatcher.dispatch(raw, new FrameListener() {
            @Override
            public void onMatchInfo(MatchInfo m) {
                text.append(strip(m.getPhase().name(), "MATCH_PHASE_")).append(" id=").append(m.getMatchId())
                    .append(" map=").append(m.getMapName()).append(" players=").append(m.getPlayersCount());
            }

            @Override
            public void onStatsSnapshot(StatsSnapshot s) {
                text.append(s.getFull() ? "full" : "light").append(" frame ").append(s.getFrame()).append(", ").append(s.getPlayersCount())
                    .append(" players").append(s.getNetworkStalled() ? ", STALLED" : "");
            }

            @Override
            public void onEvents(Events e) {
                text.append("frame ").append(e.getFrame()).append(", ").append(e.getEventsCount()).append(" events");
                if (e.getEventsCount() > 0) {
                    text.append(": ").append(strip(e.getEvents(0).getKind().name(), "STATEVENT_"));
                    if (e.getEventsCount() > 1) {
                        text.append(" +").append(e.getEventsCount() - 1);
                    }
                }
            }

            @Override
            public void onCamera(Camera c) {
                text.append(String.format("at %.0f, %.0f zoom %.2f angle %.2f pitch %.2f", c.getX(), c.getY(), c.getZoom(), c.getAngle(), c.getPitch()));
            }

            @Override
            public void onMatchSummary(MatchSummary m) {
                text.append("frame ").append(m.getFrame()).append(" map=").append(m.getMapName()).append(" players=").append(m.getPlayersCount());
            }

            @Override
            public void onChatMessage(ChatMessage c) {
                text.append('[').append(strip(c.getChannel().name(), "CHAT_CHANNEL_")).append("] ").append(c.getSenderName()).append(": ").append(c.getText());
            }

            @Override
            public void onUiState(UiState u) {
                text.append(strip(u.getScreen().name(), "UI_SCREEN_")).append(u.getPaused() ? " (paused)" : "");
            }

            @Override
            public void onLobbyState(LobbyState l) {
                text.append(l.getInLobby() ? strip(l.getKind().name(), "LOBBY_KIND_") + " map=" + l.getMapName() + " slots=" + l.getSlotsCount() : "left the lobby");
            }

            @Override
            public void unknownFrame(int type, byte[] body) {
                text.append("unknown frame type");
            }

            @Override
            public void decodeError(int type, byte[] body, Exception cause) {
                text.append("DECODE ERROR: ").append(cause.getMessage());
            }
        });
        return text.toString();
    }

    // -- Connection.

    @Override
    public void onConnected(String pipePath) {
        connection = "connected " + pipePath;
    }

    @Override
    public void onDisconnected(Throwable cause) {
        // Tables are kept so the last match stays readable; a new match clears the live ones.
        connection = "disconnected, retrying" + (cause == null ? "" : " (" + cause.getMessage() + ")");
    }

    /** Free-form connection text, for the replay mode. */
    void setConnection(String text) {
        connection = text;
    }

    // -- Frames.

    @Override
    public void onUiState(UiState message) {
        ui = strip(message.getScreen().name(), "UI_SCREEN_") + (message.getPaused() ? " (paused)" : "");
    }

    @Override
    public void onMatchInfo(MatchInfo message) {
        // A match ends with the same id it started with; LOADING always begins a fresh one. Rows that arrived
        // before any MatchInfo (no id known yet) belong to the match this first MatchInfo describes.
        boolean differentMatch = !matchId.isEmpty() && !message.getMatchId().equals(matchId);
        boolean newMatch = message.getPhase() != MatchPhase.MATCH_PHASE_ENDED
            && (message.getPhase() == MatchPhase.MATCH_PHASE_LOADING || differentMatch);
        if (newMatch) {
            players.clear();
            events.clear();
            summary.clear();
            frame = -1;
            stalled = false;
            snapshotCount = 0;
        }
        matchId = message.getMatchId();
        if (message.getLogicFramesPerSecond() > 0) {
            logicFps = message.getLogicFramesPerSecond();
        }

        playersByIndex.clear();
        for (MatchPlayer p : message.getPlayersList()) {
            if (p.getPlayerIndex() >= 0) {
                playersByIndex.put(p.getPlayerIndex(), p);
            }
        }
        // Refresh the join for rows already shown, in place.
        for (int i = 0; i < players.getRowCount(); i++) {
            PlayerRow row = players.get(i);
            players.set(i, new PlayerRow(row.stats(), playersByIndex.get(row.stats().getPlayerIndex()), row.frame()));
        }

        match = strip(message.getPhase().name(), "MATCH_PHASE_") + "  " + strip(message.getMode().name(), "MATCH_MODE_")
            + "  map " + message.getMapName() + "  id " + message.getMatchId();
        if (message.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            match += "  duration " + gameTime(u(message.getDurationFrames()), logicFps) + (message.getCompleted() ? "  completed" : "");
        }
        if (message.getHasPredictionCheck()) {
            predictionCheckOk = message.getPredictionMatched();
            predictionCheck = "Lobby prediction " + (message.getPredictionMatched() ? "MATCHED" : "DID NOT MATCH")
                + ", random state " + (message.getRandomStateClean() ? "clean" : "NOT clean");
        }
        matchInfo.replaceAll(matchRows(message, logicFps));
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        snapshotCount++;
        frame = u(message.getFrame());
        stalled = message.getNetworkStalled();
        stalledFrames = u(message.getStalledFrames());
        speedFps = message.getGameSpeedFps();
        speedRatio = message.getGameSpeedRatio();
        if (message.getLocalRenderFps() > 0 || message.getLocalLogicFps() > 0) {
            localRenderFps = message.getLocalRenderFps();
            localLogicFps = message.getLocalLogicFps();
        }

        // A stall snapshot has no players; it must not clear the table. A light snapshot keeps the heavy values of the last full one.
        for (PlayerStats stats : message.getPlayersList()) {
            int at = indexOfPlayer(stats.getPlayerIndex());
            PlayerStats shown = !message.getFull() && at >= 0 ? withHeavyFieldsOf(stats, players.get(at).stats()) : stats;
            PlayerRow row = new PlayerRow(shown, playersByIndex.get(stats.getPlayerIndex()), frame);
            if (at >= 0) {
                players.set(at, row);
            } else {
                players.add(row);
            }
        }
    }

    @Override
    public void onEvents(Events message) {
        for (StatEvent e : message.getEventsList()) {
            events.add(eventRow(e));
        }
    }

    @Override
    public void onCamera(Camera message) {
        camera = message;
        cameraFrames++;
    }

    /**
     * A light snapshot carries only the cheap per-player numbers. The fields that need a walk over the live objects
     * (listed at StatsSnapshot in the proto) are taken from the previous row instead.
     */
    static PlayerStats withHeavyFieldsOf(PlayerStats light, PlayerStats previous) {
        return light.toBuilder()
            .setArmyValue(previous.getArmyValue())
            .setLiveSuperweaponStructureCount(previous.getLiveSuperweaponStructureCount())
            .clearLiveTemplateCounts().addAllLiveTemplateCounts(previous.getLiveTemplateCountsList())
            .clearReadiness().addAllReadiness(previous.getReadinessList())
            .clearSciences().addAllSciences(previous.getSciencesList())
            .clearUpgrades().addAllUpgrades(previous.getUpgradesList())
            .clearProduction().addAllProduction(previous.getProductionList())
            .setWorkers(previous.getWorkers())
            .setHarvesters(previous.getHarvesters())
            .setIdleWorkers(previous.getIdleWorkers())
            .clearHeroes().addAllHeroes(previous.getHeroesList())
            .clearSuperweapons().addAllSuperweapons(previous.getSuperweaponsList())
            .setArmyValueByClass(previous.getArmyValueByClass())
            .setArmyCountByClass(previous.getArmyCountByClass())
            .clearTechStructures().addAllTechStructures(previous.getTechStructuresList())
            .setBattlePlanTransition(previous.getBattlePlanTransition())
            .build();
    }

    @Override
    public void onMatchSummary(MatchSummary message) {
        summary.replaceAll(message.getPlayersList());
    }

    @Override
    public void onChatMessage(ChatMessage message) {
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
        chat.add(new ChatRow(LocalTime.now().format(CLOCK), strip(message.getChannel().name(), "CHAT_CHANNEL_"), message.getSenderName(),
            strip(message.getAudience().name(), "CHAT_AUDIENCE_"), message.getText(), String.join(", ", flags)));
    }

    @Override
    public void onLobbyState(LobbyState message) {
        if (!message.getInLobby()) {
            lobbySlots.clear();
            lobbyHeader = "not in a lobby";
            return;
        }
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
        lobbyHeader = strip(message.getKind().name(), "LOBBY_KIND_") + "    map " + message.getMapName() + "    host " + host
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

    private int indexOfPlayer(int playerIndex) {
        for (int i = 0; i < players.getRowCount(); i++) {
            if (players.get(i).stats().getPlayerIndex() == playerIndex) {
                return i;
            }
        }
        return -1;
    }

    private EventRow eventRow(StatEvent e) {
        MatchPlayer info = playersByIndex.get(e.getPlayerIndex());
        String player = info != null ? info.getName() : e.getPlayerIndex() >= 0 ? "P" + e.getPlayerIndex() : "";
        String location = e.getHasLocation() ? Math.round(e.getX()) + ", " + Math.round(e.getY()) : "";
        return new EventRow(u(e.getFrame()), gameTime(u(e.getFrame()), logicFps), player, strip(e.getKind().name(), "STATEVENT_"), e.getCategory(),
            e.getPrimaryName(), e.getSecondaryName(), u(e.getCost()), location, eventDetail(e));
    }

    /** The free-form part of an event: the old and new value for state changes, the old owner for a capture. */
    private String eventDetail(StatEvent e) {
        switch (e.getKind()) {
            case STATEVENT_RADAR_CHANGED:
                return enumName(RadarState.forNumber(e.getIntValue1()), "RADAR_") + " -> " + enumName(RadarState.forNumber(e.getIntValue2()), "RADAR_");
            case STATEVENT_BATTLE_PLAN_CHANGED:
                return enumName(BattlePlan.forNumber(e.getIntValue1()), "BATTLE_PLAN_") + " -> " + enumName(BattlePlan.forNumber(e.getIntValue2()), "BATTLE_PLAN_");
            case STATEVENT_CAPTURE: {
                MatchPlayer previous = playersByIndex.get(e.getIntValue1());
                String from = "from " + (previous != null ? previous.getName() : "P" + e.getIntValue1());
                return e.getVariant().isEmpty() ? from : from + " (" + e.getVariant() + ")";
            }
            case STATEVENT_POWER_SABOTAGED:
                return "for " + gameTime(e.getIntValue1(), logicFps);
            default:
                return e.getVariant();
        }
    }

    private static String enumName(Enum<?> value, String prefix) {
        return value == null ? "?" : strip(value.name(), prefix);
    }

    private static List<String[]> matchRows(MatchInfo m, int logicFps) {
        List<String[]> rows = new ArrayList<>();
        rows.add(new String[] {"Phase", strip(m.getPhase().name(), "MATCH_PHASE_")});
        rows.add(new String[] {"Match id", m.getMatchId()});
        rows.add(new String[] {"Mode", strip(m.getMode().name(), "MATCH_MODE_")});
        rows.add(new String[] {"Map", m.getMapName() + (m.getMapIsUserMap() ? " (user map)" : "")});
        rows.add(new String[] {"Started", m.getStartUnixTime() == 0 ? "" : DATE_TIME.format(Instant.ofEpochSecond(m.getStartUnixTime()).atZone(ZoneId.systemDefault()))});
        if (m.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            rows.add(new String[] {"Duration", gameTime(u(m.getDurationFrames()), logicFps) + " (" + u(m.getDurationFrames()) + " frames)"});
        }
        rows.add(new String[] {"Replay file", m.getReplayPath() + (m.getReplayIsPlayback() ? " (being played back)" : "")});
        rows.add(new String[] {"Completed", Boolean.toString(m.getCompleted())});
        rows.add(new String[] {"Local player index", Integer.toString(m.getLocalPlayerIndex())});
        rows.add(new String[] {"Logic rate", (m.getLogicFramesPerSecond() > 0 ? m.getLogicFramesPerSecond() : DEFAULT_LOGIC_FPS) + " frames per second"});
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

    // Template-name prefixes that only say which faction or general a unit belongs to; stripped for display, never otherwise.
    private static final String[] GENERAL_PREFIXES = {"AirF_", "Lazr_", "SupW_", "Nuke_", "Infa_", "Tank_", "Chem_", "Demo_", "Slth_", "Boss_"};
    private static final String[] FACTION_PREFIXES = {"America", "China", "GLA", "Boss"};
    private static final String[] KIND_WORDS = {"Infantry", "Vehicle", "Tank", "Jet", "Aircraft", "Structure", "Building"};

    /** A template or upgrade name for display: "AirF_AmericaTankCrusader" becomes "Crusader". Unknown names are left alone. */
    static String shortName(String raw) {
        String name = raw;
        for (String prefix : GENERAL_PREFIXES) {
            if (name.startsWith(prefix) && name.length() > prefix.length()) {
                name = name.substring(prefix.length());
                break;
            }
        }
        for (String prefix : FACTION_PREFIXES) {
            if (name.startsWith(prefix) && name.length() > prefix.length() && Character.isUpperCase(name.charAt(prefix.length()))) {
                name = name.substring(prefix.length());
                break;
            }
        }
        // A kind word is only dropped after a faction prefix was, so "Tank..." alone stays as it is.
        if (!name.equals(raw)) {
            for (String word : KIND_WORDS) {
                if (name.startsWith(word) && name.length() > word.length() && Character.isUpperCase(name.charAt(word.length()))) {
                    name = name.substring(word.length());
                    break;
                }
            }
        }
        return name;
    }

    /** "Upgrade_AmericaAdvancedTraining" becomes "AdvancedTraining (upgrade)". */
    static String shortUpgradeName(String raw) {
        String name = raw.startsWith("Upgrade_") && raw.length() > 8 ? raw.substring(8) : raw;
        return shortName(name) + " (upgrade)";
    }

    private static String itemName(ProductionItem item) {
        return item.getKind() == ProductionKind.PRODUCTION_KIND_UPGRADE ? shortUpgradeName(item.getName()) : shortName(item.getName());
    }

    /**
     * Per-player production summary: the number of items queued across all structures, then the items being built
     * (the front of each structure's queue) by progress, most complete first, e.g. "4 \u00b7 Crusader 92%, Ranger 40%, +2".
     */
    static String productionText(PlayerStats p) {
        int total = 0;
        List<ProductionItem> fronts = new ArrayList<>();
        for (ProductionQueue queue : p.getProductionList()) {
            total += queue.getEntriesCount();
            if (queue.getEntriesCount() > 0) {
                fronts.add(queue.getEntries(0));
            }
        }
        if (total == 0) {
            return "";
        }
        fronts.sort(Comparator.comparingDouble(ProductionItem::getPercentComplete).reversed());
        StringBuilder text = new StringBuilder().append(total).append(" \u00b7 ");
        int shown = Math.min(fronts.size(), MAX_SHOWN_ITEMS);
        for (int i = 0; i < shown; i++) {
            ProductionItem item = fronts.get(i);
            text.append(i > 0 ? ", " : "").append(itemName(item)).append(' ').append(Math.round(item.getPercentComplete())).append('%');
        }
        if (total > shown) {
            text.append(shown > 0 ? ", " : "").append('+').append(total - shown);
        }
        return text.toString();
    }

    /** The same production data with the raw internal names, one structure per line, for the cell tooltip. */
    static String productionDetail(PlayerStats p) {
        StringBuilder text = new StringBuilder();
        for (ProductionQueue queue : p.getProductionList()) {
            text.append(queue.getStructureTemplate()).append(':');
            for (int i = 0; i < queue.getEntriesCount(); i++) {
                ProductionItem item = queue.getEntries(i);
                text.append(' ').append(item.getName());
                if (i == 0) {
                    text.append(' ').append(Math.round(item.getPercentComplete())).append('%');
                }
            }
            text.append('\n');
        }
        return text.toString();
    }

    static String heroesText(PlayerStats p) {
        int alive = 0;
        int dead = 0;
        for (HeroStatus hero : p.getHeroesList()) {
            if (hero.getAlive()) {
                alive++;
            } else {
                dead++;
            }
        }
        return p.getHeroesCount() == 0 ? "" : alive + " alive / " + dead + " dead";
    }

    /** "SuperweaponNeutronMissile" becomes "NeutronMissile". */
    static String shortPowerName(String raw) {
        for (String prefix : new String[] {"Superweapon", "SpecialPower"}) {
            if (raw.startsWith(prefix) && raw.length() > prefix.length()) {
                return raw.substring(prefix.length());
            }
        }
        return raw;
    }

    /**
     * Every superweapon as "Name 1:23" or "Name READY", ready ones first, then by time left. A weapon that reports no
     * ready frame yet shows its charge percentage instead and sorts last.
     */
    static String superweaponText(PlayerStats p, long snapshotFrame, int fps) {
        record Entry(long sortKey, String text) {
        }
        List<Entry> entries = new ArrayList<>();
        for (SuperweaponTimer t : p.getSuperweaponsList()) {
            String name = shortPowerName(t.getPowerName());
            if (t.getState() == SuperweaponState.SUPERWEAPON_READY) {
                entries.add(new Entry(-1, name + " READY"));
            } else if (t.getReadyFrame() != 0) {
                long left = Math.max(0, u(t.getReadyFrame()) - snapshotFrame);
                entries.add(new Entry(left, name + " " + gameTime(left, fps)));
            } else {
                entries.add(new Entry(Long.MAX_VALUE, name + " " + Math.round(t.getPercentReady() * 100) + "%"));
            }
        }
        entries.sort(Comparator.comparingLong(Entry::sortKey));
        List<String> texts = new ArrayList<>();
        for (Entry entry : entries) {
            texts.add(entry.text());
        }
        return String.join(", ", texts);
    }

    static String radarText(PlayerStats p) {
        return strip(p.getRadar().name(), "RADAR_");
    }

    /** The plan affecting the player's troops, or the Strategy Center's transition while one is moving. */
    static String battlePlanText(PlayerStats p) {
        switch (p.getBattlePlanTransition()) {
            case BATTLE_PLAN_DEPLOYING:
                return "deploying";
            case BATTLE_PLAN_PACKING:
                return "packing";
            default:
                return p.getBattlePlan() == BattlePlan.BATTLE_PLAN_NONE ? "" : strip(p.getBattlePlan().name(), "BATTLE_PLAN_").replace('_', ' ');
        }
    }

    static String sabotageText(PlayerStats p, int fps) {
        return p.getPowerSabotaged() ? "yes, " + gameTime(u(p.getPowerSabotageFramesLeft()), fps) + " left" : "";
    }

    /** "120 ms / 58 fps / ping 45 ms (relay)": frame latency and FPS the engine measured, then the mesh ping; "?" when unknown. */
    static String networkText(PlayerStats p) {
        if (!p.hasNetwork()) {
            return "";
        }
        NetworkStats n = p.getNetwork();
        StringBuilder text = new StringBuilder();
        text.append(n.getLatencyMs() >= 0 ? n.getLatencyMs() + " ms" : "? ms").append(" / ");
        text.append(n.getReportedFps() >= 0 ? n.getReportedFps() + " fps" : "? fps");
        if (n.getPingMs() >= 0) {
            text.append(" / ping ").append(n.getPingMs()).append(" ms").append(n.getRelayed() ? " (relay)" : "");
        }
        return text.toString();
    }

    /** The raw superweapon power and structure names, one per line, for the cell tooltip. */
    static String superweaponDetail(PlayerStats p) {
        StringBuilder text = new StringBuilder();
        for (SuperweaponTimer t : p.getSuperweaponsList()) {
            text.append(t.getPowerName()).append(" (").append(t.getTemplateName()).append(", ").append(t.getState().name()).append(")\n");
        }
        return text.toString();
    }

    /** Frames to m:ss (or h:mm:ss) at the build's logic rate. */
    static String gameTime(long frames, int fps) {
        long seconds = Math.max(0, frames) / Math.max(1, fps);
        if (seconds >= 3600) {
            return String.format("%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        }
        return String.format("%d:%02d", seconds / 60, seconds % 60);
    }

    private static long u(int value) {
        return Integer.toUnsignedLong(value);
    }

    private static String strip(String name, String prefix) {
        return name.startsWith(prefix) ? name.substring(prefix.length()) : name;
    }
}
