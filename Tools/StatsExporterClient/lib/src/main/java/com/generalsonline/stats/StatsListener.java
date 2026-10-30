package com.generalsonline.stats;

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

/**
 * Receives what the stats pipe delivers: the connection lifecycle and one callback per frame type. Every method has an
 * empty default, so a reader overrides only what it needs.
 *
 * <p>Threading: the methods are called on the thread of the {@link StatsPipe}'s executor, which is the reader thread
 * unless the builder was given another one, or on the caller's thread when a dump is replayed. Callbacks of one pipe
 * never overlap. A callback that throws costs only its frame: the exception goes to {@link #onError} and reading
 * continues.
 *
 * <p>Frames reach the typed methods through {@link FrameDispatcher#deliver}, whose switch handles every outbound
 * {@link FrameType}; any other frame reaches {@link #unknownFrame}, so a frame is never silently skipped.
 */
public interface StatsListener {
    /**
     * Called when a connection to a game was established.
     *
     * @param pipePath the pipe path that was opened, for example {@code \\.\pipe\generalsonline-stats-1234}
     */
    default void onConnected(String pipePath) {
    }

    /**
     * Called when the connection ended, whether the game closed the pipe or a read failed.
     *
     * @param cause the real cause of a lost connection (wrappers unwrapped by the library), or null for a clean close
     */
    default void onDisconnected(Throwable cause) {
    }

    /**
     * Called with the Hello that opens every connection, after the library checked that its protocol version is
     * supported. Frames that follow it can rely on {@link StatsPipe#hello()}.
     *
     * @param message the handshake: game build, capabilities and limits
     */
    default void onHello(Hello message) {
    }

    /**
     * Called with every response the game sent on this connection, after a pending {@link StatsPipe#request} with the
     * same id was completed. A response nobody waits for (late, or to a request made outside this library) arrives
     * here too, and is otherwise ignored.
     *
     * @param message the response
     */
    default void onResponse(Response message) {
    }

    /**
     * Called when frames were dropped: the game's queue for this reader overflowed and it skipped frames, which shows
     * as a gap in the sequence numbers. Called before the frame that revealed the gap.
     *
     * @param count the number of frames missing in this gap
     */
    default void onFramesDropped(long count) {
    }

    /**
     * Called for every frame, decoded once, before the typed method of that frame. This also reaches frames of an
     * unknown type and frames whose body did not parse, which makes it the place for recorders and statistics.
     *
     * @param frame the frame with its raw bytes and, when it parsed, the decoded message
     */
    default void onFrame(DecodedFrame frame) {
    }

    /**
     * Called when a match is loading, started or ended.
     *
     * @param message the match and its players
     */
    default void onMatchInfo(MatchInfo message) {
    }

    /**
     * Called for a periodic snapshot. A light snapshot (full is false) leaves the heavy fields empty; use
     * {@link MatchState} to get the last full values merged in.
     *
     * @param message the snapshot
     */
    default void onStatsSnapshot(StatsSnapshot message) {
    }

    /**
     * Called once at the end of a match with the final scores.
     *
     * @param message the summary
     */
    default void onMatchSummary(MatchSummary message) {
    }

    /**
     * Called for a chat line the local player sees.
     *
     * @param message the line
     */
    default void onChatMessage(ChatMessage message) {
    }

    /**
     * Called when the game moves to another screen, and once on connect.
     *
     * @param message the screen
     */
    default void onUiState(UiState message) {
    }

    /**
     * Called when the game setup lobby the user sits in changes, and when the user leaves it.
     *
     * @param message the lobby
     */
    default void onLobbyState(LobbyState message) {
    }

    /**
     * Called with the discrete events of one logic frame.
     *
     * @param message the events
     */
    default void onEvents(Events message) {
    }

    /**
     * Called with the local camera and the minimap view box. Sent up to about 120 times a second while the camera
     * moves, so keep the work short or coalesce.
     *
     * @param message the camera
     */
    default void onCamera(Camera message) {
    }

    /**
     * Called with what the local camera sees, per player. Sent about four times a second unless the feed configuration
     * changed that.
     *
     * @param message the in-view statistics
     */
    default void onViewStats(ViewStats message) {
    }

    /**
     * Called with the interest grid and the pending highlights. Sent only after the reader switched
     * {@code FeedConfig.interest} on.
     *
     * @param message the interest map
     */
    default void onInterestMap(InterestMap message) {
    }

    /**
     * Called for a frame type this library does not know. The frame is skipped and the stream continues.
     *
     * @param type the type byte
     * @param body the undecoded body
     */
    default void unknownFrame(int type, byte[] body) {
    }

    /**
     * Called for a frame of a known type whose body did not parse. The frame is skipped and the stream continues.
     *
     * @param type the type byte
     * @param body the undecoded body
     * @param cause the parse failure
     */
    default void decodeError(int type, byte[] body, Exception cause) {
    }

    /**
     * Called when a callback threw while handling a frame (or a lifecycle event). The frame is dropped and the stream
     * continues; the connection is not treated as lost.
     *
     * @param type the type byte of the frame being handled, or 0 for a lifecycle callback
     * @param cause what the callback threw
     */
    default void onError(int type, Throwable cause) {
    }
}
