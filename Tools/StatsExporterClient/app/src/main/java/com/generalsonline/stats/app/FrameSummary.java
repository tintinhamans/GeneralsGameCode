package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.Handshake;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

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
        if (message instanceof Hello h) {
            return Handshake.summary(h);
        }
        if (message instanceof Response r) {
            return "id " + Integer.toUnsignedString(r.getId()) + " " + strip(r.getStatus().name(), "RESPONSE_STATUS_") + (r.getMessage().isEmpty() ? "" : ": " + r.getMessage());
        }
        if (message instanceof MatchInfo m) {
            return strip(m.getPhase().name(), "MATCH_PHASE_") + " id=" + m.getMatchId() + " map=" + m.getMapName() + " players=" + m.getPlayersCount();
        }
        if (message instanceof StatsSnapshot s) {
            return (s.getFull() ? "full" : "light") + " frame " + s.getFrame() + ", " + s.getPlayersCount() + " players" + (s.getNetworkStalled() ? ", STALLED" : "");
        }
        if (message instanceof Events e) {
            StringBuilder text = new StringBuilder("frame ").append(e.getFrame()).append(", ").append(e.getEventsCount()).append(" events");
            if (e.getEventsCount() > 0) {
                text.append(": ").append(strip(e.getEvents(0).getKind().name(), "STATEVENT_"));
                if (e.getEventsCount() > 1) {
                    text.append(" +").append(e.getEventsCount() - 1);
                }
            }
            return text.toString();
        }
        if (message instanceof Camera c) {
            return String.format("at %.0f, %.0f zoom %.2f angle %.2f pitch %.2f", c.getX(), c.getY(), c.getZoom(), c.getAngle(), c.getPitch());
        }
        if (message instanceof MatchSummary m) {
            return "frame " + m.getFrame() + " map=" + m.getMapName() + " players=" + m.getPlayersCount();
        }
        if (message instanceof ChatMessage c) {
            return "[" + strip(c.getChannel().name(), "CHAT_CHANNEL_") + "] " + c.getSenderName() + ": " + c.getText();
        }
        if (message instanceof UiState u) {
            return strip(u.getScreen().name(), "UI_SCREEN_") + (u.getPaused() ? " (paused)" : "");
        }
        if (message instanceof LobbyState l) {
            return l.getInLobby() ? strip(l.getKind().name(), "LOBBY_KIND_") + " map=" + l.getMapName() + " slots=" + l.getSlotsCount() : "left the lobby";
        }
        return "";
    }
}
