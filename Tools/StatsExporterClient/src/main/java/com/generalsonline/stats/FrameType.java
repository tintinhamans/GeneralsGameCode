package com.generalsonline.stats;

/** Frame types of the stats pipe (see docs/stats-exporter.md). */
public enum FrameType {
    MATCH_INFO(0x01, false),
    STATS_SNAPSHOT(0x02, false),
    MATCH_SUMMARY(0x03, false),
    CHAT_MESSAGE(0x04, false),
    UI_STATE(0x05, false),
    LOBBY_STATE(0x06, false),
    CHAT_SEND(0x10, true);

    private final int id;
    private final boolean inbound;

    FrameType(int id, boolean inbound) {
        this.id = id;
        this.inbound = inbound;
    }

    public int id() {
        return id;
    }

    /** True for frames the reader sends to the game. */
    public boolean inbound() {
        return inbound;
    }

    /** The outbound (game to reader) type with this id, or null. */
    public static FrameType outboundFromId(int id) {
        for (FrameType t : values()) {
            if (!t.inbound && t.id == id) {
                return t;
            }
        }
        return null;
    }
}
