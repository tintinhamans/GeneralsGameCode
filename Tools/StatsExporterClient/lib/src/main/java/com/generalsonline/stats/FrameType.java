package com.generalsonline.stats;

/**
 * The frame types of the stats pipe. Each frame is a length, a one byte type and a protobuf body; this enum maps the
 * type byte to the message it carries. The protocol is described in {@code docs/stats-exporter.md} of the game
 * repository.
 */
public enum FrameType {
    /** 0x01, {@code MatchInfo}: a match is loading, started or ended, with its players. */
    MATCH_INFO(0x01, false),
    /** 0x02, {@code StatsSnapshot}: periodic live statistics, light about every 100 ms of game time and full about every 250 ms. */
    STATS_SNAPSHOT(0x02, false),
    /** 0x03, {@code MatchSummary}: the end-of-match scores. */
    MATCH_SUMMARY(0x03, false),
    /** 0x04, {@code ChatMessage}: a chat line as the local player sees it. */
    CHAT_MESSAGE(0x04, false),
    /** 0x05, {@code UiState}: the screen the game is on. */
    UI_STATE(0x05, false),
    /** 0x06, {@code LobbyState}: the game setup lobby the local user sits in. */
    LOBBY_STATE(0x06, false),
    /** 0x07, {@code Events}: the discrete events of one logic frame. */
    EVENTS(0x07, false),
    /** 0x08, {@code Camera}: the local camera and minimap view box, up to about 120 times a second. */
    CAMERA(0x08, false),
    /** 0x10, {@code ChatSend}: the only frame a reader sends to the game. */
    CHAT_SEND(0x10, true);

    private final int id;
    private final boolean inbound;

    FrameType(int id, boolean inbound) {
        this.id = id;
        this.inbound = inbound;
    }

    /**
     * Returns the type byte of this frame type.
     *
     * @return the id as it appears on the wire
     */
    public int id() {
        return id;
    }

    /**
     * Tells which way this frame travels.
     *
     * @return true for frames the reader sends to the game, false for frames the game sends
     */
    public boolean inbound() {
        return inbound;
    }

    /**
     * Looks up a frame type that the game sends.
     *
     * @param id the type byte of a received frame
     * @return the outbound (game to reader) type with this id, or null when the id is not one the game sends
     */
    public static FrameType outboundFromId(int id) {
        for (FrameType t : values()) {
            if (!t.inbound && t.id == id) {
                return t;
            }
        }
        return null;
    }
}
