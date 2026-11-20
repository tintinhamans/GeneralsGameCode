package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.Handshake;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;
import com.generalsonline.stats.proto.ViewStats;

import static com.generalsonline.stats.Format.strip;

/** One line describing a decoded frame, for the Frames tab. Built from the message the reader already parsed. */
final class FrameSummary {
    private FrameSummary() {
    }

    static String of(DecodedFrame frame) {
        if (frame.error() != null) {
            return "DECODE ERROR: " + frame.error().getMessage();
        }
        Object message = frame.message();
        if (message == null) {
            return "unknown frame type";
        }
        if (message instanceof Hello) {
            Hello h = (Hello) message;
            return Handshake.summary(h);
        }
        if (message instanceof Response) {
            Response r = (Response) message;
            return "id " + Integer.toUnsignedString(r.getId()) + " " + strip(r.getStatus().name(), "RESPONSE_STATUS_") + (r.getMessage().isEmpty() ? "" : ": " + r.getMessage());
        }
        if (message instanceof MatchInfo) {
            MatchInfo m = (MatchInfo) message;
            return strip(m.getPhase().name(), "MATCH_PHASE_") + " id=" + m.getMatchId() + " map=" + m.getMapName() + " players=" + m.getPlayersCount();
        }
        if (message instanceof StatsSnapshot) {
            StatsSnapshot s = (StatsSnapshot) message;
            return (s.getFull() ? "full" : "light") + " frame " + s.getFrame() + ", " + s.getPlayersCount() + " players" + (s.getNetworkStalled() ? ", STALLED" : "");
        }
        if (message instanceof Events) {
            Events e = (Events) message;
            StringBuilder text = new StringBuilder("frame ").append(e.getFrame()).append(", ").append(e.getEventsCount()).append(" events");
            if (e.getEventsCount() > 0) {
                text.append(": ").append(strip(e.getEvents(0).getKind().name(), "STATEVENT_"));
                if (e.getEventsCount() > 1) {
                    text.append(" +").append(e.getEventsCount() - 1);
                }
            }
            return text.toString();
        }
        if (message instanceof Camera) {
            Camera c = (Camera) message;
            return String.format("at %.0f, %.0f zoom %.2f angle %.2f pitch %.2f", c.getX(), c.getY(), c.getZoom(), c.getAngle(), c.getPitch());
        }
        if (message instanceof ViewStats) {
            ViewStats v = (ViewStats) message;
            return "frame " + v.getFrame() + (v.getHasView() ? "" : " (no view)") + ", " + v.getPlayersCount() + " players";
        }
        if (message instanceof InterestMap) {
            InterestMap i = (InterestMap) message;
            return "frame " + i.getFrame() + " " + i.getWidth() + "x" + i.getHeight() + " cells, " + i.getHighlightsCount() + " highlights";
        }
        if (message instanceof MatchSummary) {
            MatchSummary m = (MatchSummary) message;
            return "frame " + m.getFrame() + " map=" + m.getMapName() + " players=" + m.getPlayersCount();
        }
        if (message instanceof ChatMessage) {
            ChatMessage c = (ChatMessage) message;
            return "[" + strip(c.getChannel().name(), "CHAT_CHANNEL_") + "] " + c.getSenderName() + ": " + c.getText();
        }
        if (message instanceof UiState) {
            UiState u = (UiState) message;
            return strip(u.getScreen().name(), "UI_SCREEN_") + (u.getPaused() ? " (paused)" : "");
        }
        if (message instanceof LobbyState) {
            LobbyState l = (LobbyState) message;
            return l.getInLobby() ? strip(l.getKind().name(), "LOBBY_KIND_") + " map=" + l.getMapName() + " slots=" + l.getSlotsCount() : "left the lobby";
        }
        return "";
    }
}
