package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;
import com.generalsonline.stats.proto.ViewStats;

import java.util.List;

/**
 * Change callbacks of a {@link MatchState}: an overlay redraws from these instead of diffing the state. Every method
 * has an empty default. Each is called after the state was updated and after its lock was released, so the callback
 * may read the state freely. They run on the thread that fed the state: the {@link StatsPipe} executor, or the caller of
 * a replay.
 */
public interface StateListener {
    /**
     * A {@code MatchInfo} arrived (loading, started or ended) and the state already reflects it.
     *
     * @param match the message
     */
    default void matchChanged(MatchInfo match) {
    }

    /**
     * A new match began (a LOADING {@code MatchInfo}, or a STARTED with another match id): players, events, summary,
     * camera, world data and frame counters were cleared. Called before {@link #matchChanged} for the same message.
     * A repeated STARTED of the same match does not clear anything and does not call this.
     */
    default void matchReset() {
    }

    /**
     * The player list changed: a snapshot with players arrived, a {@code MatchInfo} renamed or recoloured them, or a
     * defeated/left event changed a player's state.
     *
     * @param players the new immutable list, see {@link MatchState#players()}
     */
    default void playersChanged(List<PlayerView> players) {
    }

    /**
     * A snapshot was applied, including a stall marker that carries no players. Frame, speed and stall values changed.
     *
     * @param snapshot the message as received (light snapshots are not merged)
     */
    default void snapshotApplied(StatsSnapshot snapshot) {
    }

    /**
     * An events frame arrived.
     *
     * @param added the events of the frame, oldest first
     */
    default void eventsAdded(List<StatEvent> added) {
    }

    /**
     * A camera frame arrived. This is called up to about 120 times a second.
     *
     * @param camera the message
     */
    default void cameraChanged(Camera camera) {
    }

    /**
     * A {@code ViewStats} frame arrived and the state holds it, see {@link MatchState#viewStats()}.
     *
     * @param stats the message
     */
    default void viewStatsChanged(ViewStats stats) {
    }

    /**
     * An {@code InterestMap} frame arrived and the state holds it, see {@link MatchState#interestMap()}.
     *
     * @param map the message
     */
    default void interestMapChanged(InterestMap map) {
    }

    /**
     * Map-level data changed: the world extent or start positions (from {@code MatchInfo}), or the grids, supply
     * sources, tech structures or connections (from a full snapshot).
     */
    default void worldChanged() {
    }

    /**
     * A chat line arrived.
     *
     * @param message the line
     */
    default void chatAdded(ChatMessage message) {
    }

    /**
     * The game setup lobby changed, or the user left it ({@code in_lobby} is false then).
     *
     * @param lobby the message
     */
    default void lobbyChanged(LobbyState lobby) {
    }

    /**
     * The game moved to another screen.
     *
     * @param ui the message
     */
    default void uiChanged(UiState ui) {
    }

    /**
     * The end-of-match summary arrived.
     *
     * @param summary the message
     */
    default void summaryChanged(MatchSummary summary) {
    }

    /**
     * The game's Hello arrived (a new connection) and the state already holds it, see {@link MatchState#hello()}.
     *
     * @param hello the handshake
     */
    default void helloReceived(Hello hello) {
    }

    /**
     * Frames were dropped on the way from the game, see {@link StatsListener#onFramesDropped}.
     *
     * @param count the frames missing in this gap
     * @param total all dropped frames counted by the state so far, see {@link MatchState#droppedFrames()}
     */
    default void framesDropped(long count, long total) {
    }
}
