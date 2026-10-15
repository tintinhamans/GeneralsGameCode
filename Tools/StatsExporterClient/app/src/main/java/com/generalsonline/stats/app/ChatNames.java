package com.generalsonline.stats.app;

import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;

import java.util.Locale;

/** Command-line names for chat channels and audiences. The text limits are checked by {@code StatsPipe.requireSendable}. */
final class ChatNames {
    private ChatNames() {
    }

    static ChatChannel channel(String name) {
        switch (name.toLowerCase(Locale.ROOT)) {
            case "match":
                return ChatChannel.CHAT_CHANNEL_MATCH;
            case "game-lobby":
                return ChatChannel.CHAT_CHANNEL_GAME_LOBBY;
            case "room":
                return ChatChannel.CHAT_CHANNEL_ROOM;
            case "lan-lobby":
                return ChatChannel.CHAT_CHANNEL_LAN_LOBBY;
            case "lan-game":
                return ChatChannel.CHAT_CHANNEL_LAN_GAME;
            default:
                throw new IllegalArgumentException("unknown channel '" + name + "' (match, game-lobby, room, lan-lobby, lan-game)");
        }
    }

    static ChatAudience audience(String name) {
        switch (name.toLowerCase(Locale.ROOT)) {
            case "everyone":
                return ChatAudience.CHAT_AUDIENCE_EVERYONE;
            case "allies":
                return ChatAudience.CHAT_AUDIENCE_ALLIES;
            case "observers":
                return ChatAudience.CHAT_AUDIENCE_OBSERVERS;
            default:
                throw new IllegalArgumentException("unknown audience '" + name + "' (everyone, allies, observers)");
        }
    }
}
