package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.Handshake;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.MatchSummaryPlayer;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.TextFormat;

import java.io.PrintStream;
import java.util.EnumSet;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;
import java.util.function.LongSupplier;

/**
 * Prints one human-readable block per frame; lobby and UI frames are printed only when they change. Players are shown
 * by name once a MatchInfo has named them. Camera frames and snapshots are rate limited, because a console cannot
 * follow 120 camera frames or ten snapshots a second.
 */
public final class FramePrinter implements StatsListener {
    /** The kinds of output the {@code --only} option selects. Connection lines and errors are always printed. */
    public enum Kind {
        EVENTS, CHAT, MATCH, LOBBY, UI, SNAPSHOT, CAMERA
    }

    private static final long CAMERA_INTERVAL_MS = 500;

    private final PrintStream out;
    private final Set<Kind> only;
    private final long snapshotIntervalMillis;
    private final LongSupplier clockMillis;
    private final MatchState state = new MatchState();
    private String lastUi = "";
    private String lastLobby = "";
    private long lastCameraPrint = Long.MIN_VALUE;
    private long lastSnapshotPrint = Long.MIN_VALUE;

    /** Prints every kind, every snapshot (the replay command). */
    public FramePrinter(PrintStream out) {
        this(out, EnumSet.allOf(Kind.class), 0, System::currentTimeMillis);
    }

    /**
     * @param only the kinds to print
     * @param snapshotIntervalMillis print at most one snapshot per this many milliseconds; 0 prints all
     * @param clockMillis wall clock, replaceable by tests
     */
    public FramePrinter(PrintStream out, Set<Kind> only, long snapshotIntervalMillis, LongSupplier clockMillis) {
        this.out = out;
        this.only = only;
        this.snapshotIntervalMillis = snapshotIntervalMillis;
        this.clockMillis = clockMillis;
    }

    /** Parses "events,chat,match" for the {@code --only} option. @throws IllegalArgumentException for an unknown name */
    public static Set<Kind> parseOnly(String list) {
        Set<Kind> kinds = EnumSet.noneOf(Kind.class);
        for (String name : list.split(",")) {
            String trimmed = name.trim();
            if (trimmed.isEmpty()) {
                continue;
            }
            try {
                kinds.add(Kind.valueOf(trimmed.toUpperCase(Locale.ROOT)));
            } catch (IllegalArgumentException e) {
                throw new IllegalArgumentException("unknown kind '" + trimmed + "' (events, chat, match, lobby, ui, snapshot, camera)");
            }
        }
        if (kinds.isEmpty()) {
            throw new IllegalArgumentException("--only needs at least one kind (events, chat, match, lobby, ui, snapshot, camera)");
        }
        return kinds;
    }

    @Override
    public void onConnected(String pipePath) {
        out.println("CONNECTED " + pipePath);
    }

    @Override
    public void onDisconnected(Throwable cause) {
        out.println("DISCONNECTED " + (cause == null ? "" : Format.cause(cause)));
        lastUi = "";
        lastLobby = "";
    }

    /** Connection lines are always printed, like CONNECTED: the game's handshake. */
    @Override
    public void onHello(Hello hello) {
        state.onHello(hello);
        out.println("HELLO " + Handshake.summary(hello) + ", build " + hello.getGameBuild());
    }

    /** Dropped frames mean the printed feed has holes; always shown. */
    @Override
    public void onFramesDropped(long count) {
        state.onFramesDropped(count);
        out.println("DROPPED " + count + " frames (" + state.droppedFrames() + " in total)");
    }

    /** The frames the game dropped for this reader so far, from the sequence numbers. */
    public long droppedFrames() {
        return state.droppedFrames();
    }

    @Override
    public void onError(int type, Throwable cause) {
        out.println("ERROR while handling frame type 0x" + Integer.toHexString(type) + ": " + Format.cause(cause));
    }

    @Override
    public void onMatchInfo(MatchInfo m) {
        // Names and the logic rate are tracked even when match output is filtered out; events and snapshots use them.
        state.onMatchInfo(m);
        if (!only.contains(Kind.MATCH)) {
            return;
        }
        StringBuilder line = new StringBuilder("MATCH ").append(m.getPhase().name().replace("MATCH_PHASE_", ""));
        line.append(" id=").append(m.getMatchId()).append(" mode=").append(m.getMode().name().replace("MATCH_MODE_", ""));
        line.append(" map=").append(m.getMapName()).append(" players=").append(m.getPlayersCount());
        if (m.getReplayPath().length() > 0) {
            line.append(" replay=").append(m.getReplayPath());
        }
        if (m.hasObserverCamera()) {
            line.append(" observerCamera=").append(TextFormat.printer().emittingSingleLine(true).printToString(m.getObserverCamera()).trim());
        }
        if (m.getHasPredictionCheck()) {
            line.append(" predictionMatched=").append(m.getPredictionMatched()).append(" randomStateClean=").append(m.getRandomStateClean());
        }
        out.println(line);
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot s) {
        if (!only.contains(Kind.SNAPSHOT)) {
            return;
        }
        long now = clockMillis.getAsLong();
        if (snapshotIntervalMillis > 0 && lastSnapshotPrint != Long.MIN_VALUE && now - lastSnapshotPrint < snapshotIntervalMillis) {
            return;
        }
        lastSnapshotPrint = now;
        StringBuilder head = new StringBuilder("SNAPSHOT ").append(s.getFull() ? "full" : "light").append(" frame=").append(s.getFrame())
            .append(" (").append(Format.gameTime(Format.u(s.getFrame()), state.logicFps())).append(") players=").append(s.getPlayersCount());
        if (s.getNetworkStalled()) {
            head.append(" STALLED ").append(s.getStalledFrames()).append("f");
        }
        if (s.getReplayTotalFrames() > 0) {
            head.append(" replay=").append(s.getFrame()).append('/').append(s.getReplayTotalFrames());
        }
        head.append(" speed=").append(s.getGameSpeedFps()).append("fps");
        out.println(head);
        for (PlayerStats p : s.getPlayersList()) {
            out.println("  " + state.playerName(p.getPlayerIndex()) + " $" + p.getMoney() + " (" + p.getMoneyPerMinute() + "/min) army=" + p.getArmyValue()
                + " apm=" + p.getApmCurrent() + " score=" + p.getScore() + " rank=" + p.getRankLevel()
                + " prod=" + p.getProductionCount() + " heroes=" + p.getHeroesCount()
                + " destroyed=" + p.getValueDestroyed() + " lost=" + p.getValueLost());
        }
    }

    @Override
    public void onEvents(Events events) {
        if (!only.contains(Kind.EVENTS)) {
            return;
        }
        for (StatEvent e : events.getEventsList()) {
            String detail = state.eventDetail(e);
            out.println("EVENT frame=" + e.getFrame() + " " + e.getKind().name().replace("STATEVENT_", "") + " " + state.playerName(e.getPlayerIndex()) + " "
                + e.getPrimaryName() + (e.getCost() > 0 ? " cost=" + e.getCost() : "") + (detail.isEmpty() ? "" : " " + detail));
        }
    }

    /** One line per camera frame would flood a console (up to 120 a second), so at most one is printed every half second. */
    @Override
    public void onCamera(Camera c) {
        if (!only.contains(Kind.CAMERA)) {
            return;
        }
        long now = clockMillis.getAsLong();
        if (lastCameraPrint != Long.MIN_VALUE && now - lastCameraPrint < CAMERA_INTERVAL_MS) {
            return;
        }
        lastCameraPrint = now;
        out.println("CAMERA at=" + Math.round(c.getX()) + "," + Math.round(c.getY()) + " zoom=" + c.getZoom() + " angle=" + c.getAngle()
            + " pitch=" + c.getPitch() + (c.getHasViewBox() ? " box=" + Math.round(c.getViewTlX()) + "," + Math.round(c.getViewTlY()) + " "
            + Math.round(c.getViewBrX()) + "," + Math.round(c.getViewBrY()) : ""));
    }

    @Override
    public void onMatchSummary(MatchSummary m) {
        if (!only.contains(Kind.MATCH)) {
            return;
        }
        out.println("SUMMARY frame=" + m.getFrame() + " map=" + m.getMapName());
        for (MatchSummaryPlayer p : m.getPlayersList()) {
            out.println("  " + p.getPlayerName() + " (" + p.getSide() + ") score=" + p.getScore() + (p.getIsWinner() ? " WINNER" : "")
                + (p.getIsObserver() ? " observer" : ""));
        }
    }

    @Override
    public void onChatMessage(ChatMessage c) {
        if (!only.contains(Kind.CHAT)) {
            return;
        }
        out.println("CHAT [" + c.getChannel().name().replace("CHAT_CHANNEL_", "") + "] "
            + (c.getSenderName().isEmpty() ? "" : c.getSenderName() + ": ") + c.getText()
            + (c.getIsLocal() ? " (local)" : "") + (c.getIsSystem() ? " (system)" : ""));
    }

    @Override
    public void onUiState(UiState u) {
        if (!only.contains(Kind.UI)) {
            return;
        }
        String text = "UI " + u.getScreen().name().replace("UI_SCREEN_", "") + " layout=" + u.getLayout()
            + (u.getPaused() ? " paused" : "");
        if (!Objects.equals(text, lastUi)) {
            lastUi = text;
            out.println(text);
        }
    }

    @Override
    public void onLobbyState(LobbyState l) {
        if (!only.contains(Kind.LOBBY)) {
            return;
        }
        StringBuilder text = new StringBuilder("LOBBY ");
        if (!l.getInLobby()) {
            text.append("left");
        } else {
            text.append(l.getKind().name().replace("LOBBY_KIND_", "")).append(" map=").append(l.getMapName())
                .append(" predictionValid=").append(l.getPredictionValid()).append(" startPositionsValid=").append(l.getPredictionStartPositionsValid());
            for (LobbySlot slot : l.getSlotsList()) {
                if (slot.hasPlayer()) {
                    text.append("\n  ").append(slot.getPlayer().getName()).append(" side=").append(CellText.lobbySide(slot, l.getPredictionValid(), null))
                        .append(slot.getIsReady() ? " ready" : "");
                }
            }
        }
        if (!text.toString().equals(lastLobby)) {
            lastLobby = text.toString();
            out.println(text);
        }
    }

    @Override
    public void unknownFrame(int type, byte[] body) {
        out.println("UNKNOWN frame type=0x" + Integer.toHexString(type) + " bytes=" + body.length);
    }

    @Override
    public void decodeError(int type, byte[] body, Exception cause) {
        out.println("DECODE ERROR type=0x" + Integer.toHexString(type) + ": " + cause.getMessage());
    }
}
