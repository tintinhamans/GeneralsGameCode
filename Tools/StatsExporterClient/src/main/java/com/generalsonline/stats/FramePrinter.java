package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
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
import java.util.Objects;

/** Prints one human-readable block per frame; lobby and UI frames are printed only when they change. */
public final class FramePrinter implements FrameListener {
    private final PrintStream out;
    private String lastUi = "";
    private String lastLobby = "";
    private long lastCameraPrint;

    public FramePrinter(PrintStream out) {
        this.out = out;
    }

    @Override
    public void onConnected(String pipePath) {
        out.println("CONNECTED " + pipePath);
    }

    @Override
    public void onDisconnected(Throwable cause) {
        out.println("DISCONNECTED " + (cause == null ? "" : cause));
        lastUi = "";
        lastLobby = "";
    }

    @Override
    public void onMatchInfo(MatchInfo m) {
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
        StringBuilder head = new StringBuilder("SNAPSHOT ").append(s.getFull() ? "full" : "light").append(" frame=").append(s.getFrame())
            .append(" players=").append(s.getPlayersCount());
        if (s.getNetworkStalled()) {
            head.append(" STALLED ").append(s.getStalledFrames()).append("f");
        }
        if (s.getReplayTotalFrames() > 0) {
            head.append(" replay=").append(s.getFrame()).append('/').append(s.getReplayTotalFrames());
        }
        head.append(" speed=").append(s.getGameSpeedFps()).append("fps");
        out.println(head);
        for (PlayerStats p : s.getPlayersList()) {
            out.println("  P" + p.getPlayerIndex() + " $" + p.getMoney() + " (" + p.getMoneyPerMinute() + "/min) army=" + p.getArmyValue()
                + " apm=" + p.getApmCurrent() + " score=" + p.getScore() + " rank=" + p.getRankLevel()
                + " prod=" + p.getProductionCount() + " heroes=" + p.getHeroesCount()
                + " destroyed=" + p.getValueDestroyed() + " lost=" + p.getValueLost());
        }
    }

    @Override
    public void onEvents(Events events) {
        for (StatEvent e : events.getEventsList()) {
            out.println("EVENT frame=" + e.getFrame() + " " + e.getKind().name().replace("STATEVENT_", "") + " P" + e.getPlayerIndex() + " "
                + e.getPrimaryName() + (e.getCost() > 0 ? " cost=" + e.getCost() : "") + (e.getVariant().isEmpty() ? "" : " " + e.getVariant())
                + (e.getIntValue1() != 0 || e.getIntValue2() != 0 ? " values=" + e.getIntValue1() + "," + e.getIntValue2() : ""));
        }
    }

    /** One line per camera frame would flood a console (up to 120 a second), so at most one is printed every half second. */
    @Override
    public void onCamera(Camera c) {
        long now = System.nanoTime();
        if (now - lastCameraPrint < 500_000_000L) {
            return;
        }
        lastCameraPrint = now;
        out.println("CAMERA at=" + Math.round(c.getX()) + "," + Math.round(c.getY()) + " zoom=" + c.getZoom() + " angle=" + c.getAngle()
            + " pitch=" + c.getPitch() + (c.getHasViewBox() ? " box=" + Math.round(c.getViewTlX()) + "," + Math.round(c.getViewTlY()) + " "
            + Math.round(c.getViewBrX()) + "," + Math.round(c.getViewBrY()) : ""));
    }

    @Override
    public void onMatchSummary(MatchSummary m) {
        out.println("SUMMARY frame=" + m.getFrame() + " map=" + m.getMapName());
        for (MatchSummaryPlayer p : m.getPlayersList()) {
            out.println("  " + p.getPlayerName() + " (" + p.getSide() + ") score=" + p.getScore() + (p.getIsWinner() ? " WINNER" : "")
                + (p.getIsObserver() ? " observer" : ""));
        }
    }

    @Override
    public void onChatMessage(ChatMessage c) {
        out.println("CHAT [" + c.getChannel().name().replace("CHAT_CHANNEL_", "") + "] "
            + (c.getSenderName().isEmpty() ? "" : c.getSenderName() + ": ") + c.getText()
            + (c.getIsLocal() ? " (local)" : "") + (c.getIsSystem() ? " (system)" : ""));
    }

    @Override
    public void onUiState(UiState u) {
        String text = "UI " + u.getScreen().name().replace("UI_SCREEN_", "") + " layout=" + u.getLayout()
            + (u.getPaused() ? " paused" : "");
        if (!Objects.equals(text, lastUi)) {
            lastUi = text;
            out.println(text);
        }
    }

    @Override
    public void onLobbyState(LobbyState l) {
        StringBuilder text = new StringBuilder("LOBBY ");
        if (!l.getInLobby()) {
            text.append("left");
        } else {
            text.append(l.getKind().name().replace("LOBBY_KIND_", "")).append(" map=").append(l.getMapName())
                .append(" predictionValid=").append(l.getPredictionValid()).append(" startPositionsValid=").append(l.getPredictionStartPositionsValid());
            for (LobbySlot slot : l.getSlotsList()) {
                if (slot.hasPlayer()) {
                    text.append("\n  ").append(slot.getPlayer().getName()).append(" side=").append(slot.getPlayer().getSide())
                        .append(" predicted=").append(slot.getPredictedSide()).append(slot.getIsReady() ? " ready" : "");
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
