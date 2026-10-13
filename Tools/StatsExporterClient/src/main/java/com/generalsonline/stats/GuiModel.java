package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.HeroStatus;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.MatchSummaryPlayer;
import com.generalsonline.stats.proto.OnlineMatch;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.ProductionItem;
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
    static final int LOGIC_FRAMES_PER_SECOND = 30;
    static final int MAX_LOG_ROWS = 3000;

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
        column("Power", String.class, (PlayerRow r) -> r.stats().getEnergyProduction() + " / " + r.stats().getEnergyConsumption()),
        column("Built", Long.class, (PlayerRow r) -> u(r.stats().getUnitsBuilt())),
        column("Lost", Long.class, (PlayerRow r) -> u(r.stats().getUnitsLost())),
        column("Destroyed", Long.class, (PlayerRow r) -> u(r.stats().getUnitsDestroyed())),
        column("Value destroyed", Long.class, (PlayerRow r) -> u(r.stats().getValueDestroyed())),
        column("Value lost", Long.class, (PlayerRow r) -> u(r.stats().getValueLost())),
        column("Workers", Long.class, (PlayerRow r) -> u(r.stats().getWorkers())),
        column("Idle", Long.class, (PlayerRow r) -> u(r.stats().getIdleWorkers())),
        column("Production", String.class, (PlayerRow r) -> productionText(r.stats())),
        column("Heroes", String.class, (PlayerRow r) -> heroesText(r.stats())),
        column("Superweapons", String.class, (PlayerRow r) -> superweaponText(r.stats(), r.frame()))));

    final RowTableModel<EventRow> events = new RowTableModel<>(MAX_LOG_ROWS, List.of(
        column("Frame", Long.class, EventRow::frame),
        column("Time", String.class, EventRow::time),
        column("Player", String.class, EventRow::player),
        column("Kind", String.class, EventRow::kind),
        column("Category", String.class, EventRow::category),
        column("Name", String.class, EventRow::name),
        column("Secondary", String.class, EventRow::secondary),
        column("Cost", Long.class, EventRow::cost),
        column("Location", String.class, EventRow::location),
        column("Variant", String.class, EventRow::variant)));

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
        column("Bytes", Integer.class, FrameRow::bytes),
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
        line.append("    Game time: ").append(frame < 0 ? "-" : gameTime(frame));
        line.append("    Speed: ").append(speedText());
        line.append("    Network: ").append(stalled ? "STALLED " + stalledFrames + " frames" : "ok");
        line.append("    Snapshots: ").append(snapshotCount);
        return line.toString();
    }

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
        return speedFps + " fps (" + String.format("%.2fx", speedRatio) + ")";
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
        frames.add(new FrameRow(LocalTime.now().format(CLOCK_MILLIS), name, raw.body().length, summarize(raw)));
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
                text.append("frame ").append(s.getFrame()).append(", ").append(s.getPlayersCount()).append(" players, ")
                    .append(s.getEventsCount()).append(" events").append(s.getNetworkStalled() ? ", STALLED" : "");
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
            match += "  duration " + gameTime(u(message.getDurationFrames())) + (message.getCompleted() ? "  completed" : "");
        }
        if (message.getHasPredictionCheck()) {
            predictionCheckOk = message.getPredictionMatched();
            predictionCheck = "Lobby prediction " + (message.getPredictionMatched() ? "MATCHED" : "DID NOT MATCH")
                + ", random state " + (message.getRandomStateClean() ? "clean" : "NOT clean");
        }
        matchInfo.replaceAll(matchRows(message));
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        snapshotCount++;
        frame = u(message.getFrame());
        stalled = message.getNetworkStalled();
        stalledFrames = u(message.getStalledFrames());
        speedFps = message.getGameSpeedFps();
        speedRatio = message.getGameSpeedRatio();

        // A stall snapshot has no players; it must not clear the table.
        for (PlayerStats stats : message.getPlayersList()) {
            PlayerRow row = new PlayerRow(stats, playersByIndex.get(stats.getPlayerIndex()), frame);
            int at = indexOfPlayer(stats.getPlayerIndex());
            if (at >= 0) {
                players.set(at, row);
            } else {
                players.add(row);
            }
        }
        for (StatEvent e : message.getEventsList()) {
            events.add(eventRow(e));
        }
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
        return new EventRow(u(e.getFrame()), gameTime(u(e.getFrame())), player, strip(e.getKind().name(), "STATEVENT_"), e.getCategory(),
            e.getPrimaryName(), e.getSecondaryName(), u(e.getCost()), location, e.getVariant());
    }

    private static List<String[]> matchRows(MatchInfo m) {
        List<String[]> rows = new ArrayList<>();
        rows.add(new String[] {"Phase", strip(m.getPhase().name(), "MATCH_PHASE_")});
        rows.add(new String[] {"Match id", m.getMatchId()});
        rows.add(new String[] {"Mode", strip(m.getMode().name(), "MATCH_MODE_")});
        rows.add(new String[] {"Map", m.getMapName()});
        rows.add(new String[] {"Started", m.getStartUnixTime() == 0 ? "" : DATE_TIME.format(Instant.ofEpochSecond(m.getStartUnixTime()).atZone(ZoneId.systemDefault()))});
        if (m.getPhase() == MatchPhase.MATCH_PHASE_ENDED) {
            rows.add(new String[] {"Duration", gameTime(u(m.getDurationFrames())) + " (" + u(m.getDurationFrames()) + " frames)"});
        }
        rows.add(new String[] {"Replay file", m.getReplayPath() + (m.getReplayIsPlayback() ? " (being played back)" : "")});
        rows.add(new String[] {"Completed", Boolean.toString(m.getCompleted())});
        rows.add(new String[] {"Local player index", Integer.toString(m.getLocalPlayerIndex())});
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
            rows.add(new String[] {"Online match id", Long.toUnsignedString(o.getMatchId())});
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

    static String productionText(PlayerStats p) {
        for (var queue : p.getProductionList()) {
            if (queue.getEntriesCount() > 0) {
                ProductionItem first = queue.getEntries(0);
                return first.getName() + " " + Math.round(first.getPercentComplete()) + "%  (" + p.getProductionCount() + " queues)";
            }
        }
        return p.getProductionCount() == 0 ? "" : p.getProductionCount() + " queues";
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

    /** "READY" if any superweapon is ready, else the shortest time to the next one. */
    static String superweaponText(PlayerStats p, long snapshotFrame) {
        long soonest = Long.MAX_VALUE;
        String soonestName = "";
        for (SuperweaponTimer t : p.getSuperweaponsList()) {
            if (t.getState() == SuperweaponState.SUPERWEAPON_READY) {
                return "READY " + t.getPowerName();
            }
            long left = Math.max(0, u(t.getReadyFrame()) - snapshotFrame);
            if (left < soonest) {
                soonest = left;
                soonestName = t.getPowerName();
            }
        }
        return soonest == Long.MAX_VALUE ? "" : gameTime(soonest) + " " + soonestName;
    }

    /** Frames to m:ss (or h:mm:ss) at the normal logic rate. */
    static String gameTime(long frames) {
        long seconds = Math.max(0, frames) / LOGIC_FRAMES_PER_SECOND;
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
