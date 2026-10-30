package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.OnlineConnection;
import com.generalsonline.stats.proto.PlayerGrid;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.SupplySource;
import com.generalsonline.stats.proto.TechStructure;
import com.generalsonline.stats.proto.UiState;
import com.generalsonline.stats.proto.ViewStats;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;

/**
 * The state of the game as the feed describes it, maintained for you. Feed it frames (it is a {@link StatsListener}:
 * pass it to {@link StatsPipe.Builder#state}, or to {@link DumpReader#replay(java.nio.file.Path, StatsListener)}) and
 * read it from any thread:
 *
 * <ul>
 * <li>the current screen ({@link #ui()}), game lobby ({@link #lobby()}) and match ({@link #match()});</li>
 * <li>the players ({@link #players()}), each joining the live numbers of the latest snapshot with the static
 *     {@code MatchInfo} data by player index, the light/full snapshot merge already done (heavy fields keep the
 *     values of the last full snapshot), and whether the player is alive, defeated or gone;</li>
 * <li>the latest camera, the world extent and the map-level heavy data (grids, supply sources, tech structures);</li>
 * <li>the most recent events and chat lines, in bounded ring buffers;</li>
 * <li>frame, speed and stall values;</li>
 * <li>the connection: the game's {@link #hello() Hello} and how many frames were lost on the way ({@link #droppedFrames()}).</li>
 * </ul>
 *
 * <p>The match lifecycle is handled here: a LOADING {@code MatchInfo}, or a STARTED one with another match id, begins a
 * new match and clears everything of the old one; a repeated STARTED for the same match id changes nothing but the
 * match data; ENDED keeps the tables so the result stays readable. A reconnect keeps the state.
 *
 * <p>Threading: all methods are thread safe. Readers get immutable values (records, protobuf messages and unmodifiable
 * lists), so a UI thread may keep them. Writes happen on whichever thread delivers the frames. Add a
 * {@link StateListener} to be told about changes; its methods are called after the update, outside the lock.
 */
public final class MatchState implements StatsListener {
    /** The logic rate assumed until a {@code MatchInfo} says otherwise. */
    public static final int DEFAULT_LOGIC_FPS = 30;

    /** How many events the default ring buffer keeps. */
    public static final int DEFAULT_EVENT_CAPACITY = 2000;

    /** How many chat lines the default ring buffer keeps. */
    public static final int DEFAULT_CHAT_CAPACITY = 500;

    private static final class Entry {
        private final PlayerStats stats;
        private final long frame;

        private Entry(PlayerStats stats, long frame) {
            this.stats = stats;
            this.frame = frame;
        }

        PlayerStats stats() {
            return stats;
        }

        long frame() {
            return frame;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Entry)) {
                return false;
            }
            Entry other = (Entry) o;
            return Objects.equals(stats, other.stats)
                    && frame == other.frame;
        }

        @Override
        public int hashCode() {
            return Objects.hash(stats, frame);
        }

        @Override
        public String toString() {
            return "Entry[" + "stats=" + stats + ", frame=" + frame + "]";
        }
    }

    private final Object lock = new Object();
    private final List<StateListener> listeners = new CopyOnWriteArrayList<>();
    private final int eventCapacity;
    private final int chatCapacity;

    // Everything below is guarded by lock.
    private UiState ui;
    private LobbyState lobby;
    private MatchInfo match;
    private String matchId = "";
    private MatchSummary summary;
    private List<MatchPlayer> matchPlayers = List.of();
    private final Map<Integer, MatchPlayer> infoByIndex = new HashMap<>();
    private final Map<Integer, Entry> entries = new LinkedHashMap<>();
    private final Map<Integer, PlayerLife> lifeStates = new HashMap<>();
    private List<PlayerView> playerViews = List.of();
    private Map<Integer, PlayerView> playersByIndex = Map.of();
    private int logicFps = DEFAULT_LOGIC_FPS;
    private long frame = -1;
    private long latestEventFrame = -1;
    private boolean speedKnown;
    private int speedFps;
    private float speedRatio = 1.0f;
    private boolean stalled;
    private long stalledFrames;
    private long snapshotCount;
    private float localRenderFps;
    private float localLogicFps;
    private Camera camera;
    private long cameraFrames;
    private ViewStats viewStats;
    private InterestMap interestMap;
    private WorldExtent extent;
    private List<PlayerGrid> grids = List.of();
    private List<SupplySource> supply = List.of();
    private List<TechStructure> tech = List.of();
    private List<OnlineConnection> connections = List.of();
    private PredictionCheck predictionCheck;
    private Hello hello;
    private long droppedFrames;
    private final Deque<StatEvent> events = new ArrayDeque<>();
    private final Deque<ChatMessage> chat = new ArrayDeque<>();

    /** Creates a state with the default ring buffer sizes. */
    public MatchState() {
        this(DEFAULT_EVENT_CAPACITY, DEFAULT_CHAT_CAPACITY);
    }

    /**
     * Creates a state with custom ring buffer sizes.
     *
     * @param eventCapacity how many of the latest events to keep, at least 1
     * @param chatCapacity how many of the latest chat lines to keep, at least 1
     */
    public MatchState(int eventCapacity, int chatCapacity) {
        if (eventCapacity < 1 || chatCapacity < 1) {
            throw new IllegalArgumentException("the ring buffers need a capacity of at least 1");
        }
        this.eventCapacity = eventCapacity;
        this.chatCapacity = chatCapacity;
    }

    // -- Change listeners.

    /**
     * Registers a change listener.
     *
     * @param listener called after every change, see {@link StateListener}
     */
    public void addListener(StateListener listener) {
        listeners.add(java.util.Objects.requireNonNull(listener, "listener"));
    }

    /**
     * Unregisters a change listener.
     *
     * @param listener a listener that was added
     */
    public void removeListener(StateListener listener) {
        listeners.remove(listener);
    }

    // -- Reading.

    /**
     * Returns the game's handshake. It is kept across a disconnect, and replaced when the next connection's Hello arrives.
     *
     * @return the latest {@code Hello}, empty before the first one (and always empty when replaying a dump written
     *         before a Hello was recorded)
     */
    public Optional<Hello> hello() {
        synchronized (lock) {
            return Optional.ofNullable(hello);
        }
    }

    /**
     * Tells whether the game announced a capability in its Hello.
     *
     * @param name a capability name, see {@code docs/stats-exporter.md}
     * @return true when the latest Hello lists it; false when it does not or no Hello arrived
     */
    public boolean hasCapability(String name) {
        synchronized (lock) {
            return hello != null && Handshake.hasCapability(hello, name);
        }
    }

    /**
     * Counts the frames the game dropped for this reader (its queue overflowed), as seen in the sequence numbers. It is
     * cumulative over all connections and not reset by a new match.
     *
     * @return the number of frames known to be lost
     */
    public long droppedFrames() {
        synchronized (lock) {
            return droppedFrames;
        }
    }

    /**
     * Returns the screen the game is on.
     *
     * @return the latest {@code UiState}, empty before the first one
     */
    public Optional<UiState> ui() {
        synchronized (lock) {
            return Optional.ofNullable(ui);
        }
    }

    /**
     * Returns the game setup lobby the user sits in.
     *
     * @return the latest {@code LobbyState}, empty before the first one and after the user left the lobby
     */
    public Optional<LobbyState> lobby() {
        synchronized (lock) {
            return lobby != null && lobby.getInLobby() ? Optional.of(lobby) : Optional.empty();
        }
    }

    /**
     * Returns the latest match message.
     *
     * @return the latest {@code MatchInfo} (any phase), empty before the first one
     */
    public Optional<MatchInfo> match() {
        synchronized (lock) {
            return Optional.ofNullable(match);
        }
    }

    /**
     * Returns the match phase.
     *
     * @return LOADING, STARTED or ENDED, empty before the first {@code MatchInfo}
     */
    public Optional<MatchPhase> phase() {
        synchronized (lock) {
            return match == null ? Optional.empty() : Optional.of(match.getPhase());
        }
    }

    /**
     * Returns the id of the current match.
     *
     * @return the opaque match id, an empty text before the first {@code MatchInfo}
     */
    public String matchId() {
        synchronized (lock) {
            return matchId;
        }
    }

    /**
     * Returns every player and observer of the latest {@code MatchInfo}.
     *
     * @return the lobby slots with their static data, unmodifiable; players that have no player index yet are included
     */
    public List<MatchPlayer> matchPlayers() {
        synchronized (lock) {
            return matchPlayers;
        }
    }

    /**
     * Returns the end-of-match summary.
     *
     * @return the {@code MatchSummary} of this match, empty until the score screen sent it
     */
    public Optional<MatchSummary> summary() {
        synchronized (lock) {
            return Optional.ofNullable(summary);
        }
    }

    /**
     * Returns the players that statistics were received for: defeated and departed players stay, observers are never
     * in snapshots. In the order the players first appeared in a snapshot.
     *
     * @return an immutable list that does not change after it was returned
     */
    public List<PlayerView> players() {
        synchronized (lock) {
            return playerViews;
        }
    }

    /**
     * Returns the players keyed by player index.
     *
     * @return an immutable map that does not change after it was returned
     */
    public Map<Integer, PlayerView> playersByIndex() {
        synchronized (lock) {
            return playersByIndex;
        }
    }

    /**
     * Looks up one player.
     *
     * @param playerIndex the engine's player index, as found in snapshots and events
     * @return the player, empty when no snapshot has listed that index
     */
    public Optional<PlayerView> player(int playerIndex) {
        synchronized (lock) {
            return Optional.ofNullable(playersByIndex.get(playerIndex));
        }
    }

    /**
     * Looks up the static data of a player.
     *
     * @param playerIndex the engine's player index
     * @return the {@code MatchInfo} entry, empty while no {@code MatchInfo} named this index
     */
    public Optional<MatchPlayer> playerInfo(int playerIndex) {
        synchronized (lock) {
            return Optional.ofNullable(infoByIndex.get(playerIndex));
        }
    }

    /**
     * Returns the display name of a player. This is what {@link Format#eventDetail} uses for the old owner of a capture.
     *
     * @param playerIndex the engine's player index, or a negative number for "no player"
     * @return the name from MatchInfo, "P2" for index 2 while unnamed, or an empty text for a negative index
     */
    public String playerName(int playerIndex) {
        synchronized (lock) {
            MatchPlayer info = infoByIndex.get(playerIndex);
            if (info != null) {
                return info.getName();
            }
            return playerIndex >= 0 ? "P" + playerIndex : "";
        }
    }

    /**
     * Returns the colour of a player.
     *
     * @param playerIndex the engine's player index
     * @return packed 0xAARRGGBB, or 0 when unknown; see {@link Format#isPackedColor} and {@link Format#rgb}
     */
    public int playerColor(int playerIndex) {
        synchronized (lock) {
            MatchPlayer info = infoByIndex.get(playerIndex);
            return info != null ? info.getColor() : 0;
        }
    }

    /**
     * Returns the logic rate of the build that sends the feed.
     *
     * @return frames per second, 30 until a {@code MatchInfo} says otherwise (the high-fps build runs 60)
     */
    public int logicFps() {
        synchronized (lock) {
            return logicFps;
        }
    }

    /**
     * Returns the logic frame of the latest snapshot.
     *
     * @return the frame, or -1 when no snapshot has arrived in this match
     */
    public long frame() {
        synchronized (lock) {
            return frame;
        }
    }

    /**
     * Returns the newest game frame seen in a snapshot or an events frame.
     *
     * @return the frame, or -1 when neither has arrived in this match
     */
    public long currentFrame() {
        synchronized (lock) {
            return Math.max(frame, latestEventFrame);
        }
    }

    /**
     * Formats a logic frame as game time at the current logic rate.
     *
     * @param frames logic frames
     * @return "m:ss" or "h:mm:ss"
     */
    public String gameTime(long frames) {
        return Format.gameTime(frames, logicFps());
    }

    /**
     * Returns the game speed of the latest running snapshot. A stall snapshot does not change it.
     *
     * @return the logic frames per second the game runs at: 0 is frozen or halted, -1 uncapped; empty before any
     */
    public OptionalInt gameSpeedFps() {
        synchronized (lock) {
            return speedKnown ? OptionalInt.of(speedFps) : OptionalInt.empty();
        }
    }

    /**
     * Returns the game speed as a ratio of normal speed.
     *
     * @return 1.0 at normal speed
     */
    public float gameSpeedRatio() {
        synchronized (lock) {
            return speedRatio;
        }
    }

    /**
     * Tells whether the logic waits on a peer.
     *
     * @return true while the latest snapshot is a network stall
     */
    public boolean networkStalled() {
        synchronized (lock) {
            return stalled;
        }
    }

    /**
     * Returns how long the stall lasts.
     *
     * @return logic frames at the nominal rate, from the latest snapshot
     */
    public long stalledFrames() {
        synchronized (lock) {
            return stalledFrames;
        }
    }

    /**
     * Counts snapshots in this match.
     *
     * @return light, full and stall snapshots since the match began
     */
    public long snapshotCount() {
        synchronized (lock) {
            return snapshotCount;
        }
    }

    /**
     * Returns this machine's rendered frames per second.
     *
     * @return the last reported value, 0 when unknown
     */
    public float localRenderFps() {
        synchronized (lock) {
            return localRenderFps;
        }
    }

    /**
     * Returns this machine's logic frames per wall-clock second.
     *
     * @return the last reported value, 0 when unknown
     */
    public float localLogicFps() {
        synchronized (lock) {
            return localLogicFps;
        }
    }

    /**
     * Returns the latest camera frame.
     *
     * @return where this client looks and the minimap view box, empty before the first camera frame of the match
     */
    public Optional<Camera> camera() {
        synchronized (lock) {
            return Optional.ofNullable(camera);
        }
    }

    /**
     * Counts camera frames.
     *
     * @return the camera frames received in this match
     */
    public long cameraFrames() {
        synchronized (lock) {
            return cameraFrames;
        }
    }

    /**
     * Returns the latest in-view statistics: what the local camera sees, per player.
     *
     * @return the latest {@code ViewStats} of the match, empty before the first one (and when the reader turned the feed off)
     */
    public Optional<ViewStats> viewStats() {
        synchronized (lock) {
            return Optional.ofNullable(viewStats);
        }
    }

    /**
     * Returns the latest interest map: where the action is, and the pending highlights.
     *
     * @return the latest {@code InterestMap} of the match, empty until the reader switched {@code FeedConfig.interest} on
     */
    public Optional<InterestMap> interestMap() {
        synchronized (lock) {
            return Optional.ofNullable(interestMap);
        }
    }

    /**
     * Returns the playable map rectangle.
     *
     * @return the extent from {@code MatchInfo}, empty while the map is loading (a LOADING message has none)
     */
    public Optional<WorldExtent> worldExtent() {
        synchronized (lock) {
            return Optional.ofNullable(extent);
        }
    }

    /**
     * Returns the per-player unit grids of the last full snapshot.
     *
     * @return unmodifiable; a light snapshot does not change it
     */
    public List<PlayerGrid> grids() {
        synchronized (lock) {
            return grids;
        }
    }

    /**
     * Returns the supply sources of the last full snapshot.
     *
     * @return unmodifiable; a light snapshot does not change it
     */
    public List<SupplySource> supply() {
        synchronized (lock) {
            return supply;
        }
    }

    /**
     * Returns the tech structures of the last full snapshot.
     *
     * @return unmodifiable; a light snapshot does not change it
     */
    public List<TechStructure> tech() {
        synchronized (lock) {
            return tech;
        }
    }

    /**
     * Returns the local client's peer connections of the last full snapshot.
     *
     * @return unmodifiable; a light snapshot does not change it
     */
    public List<OnlineConnection> connections() {
        synchronized (lock) {
            return connections;
        }
    }

    /**
     * Returns the recent events of this match.
     *
     * @return a copy of the ring buffer, oldest first; the newest {@value #DEFAULT_EVENT_CAPACITY} unless a capacity was given
     */
    public List<StatEvent> events() {
        synchronized (lock) {
            return List.copyOf(events);
        }
    }

    /**
     * Returns the recent chat lines. A new match does not clear them.
     *
     * @return a copy of the ring buffer, oldest first
     */
    public List<ChatMessage> chat() {
        synchronized (lock) {
            return List.copyOf(chat);
        }
    }

    /**
     * Returns the lobby prediction check of this match.
     *
     * @return the result reported by the LOADING {@code MatchInfo}, empty when this reader did not see the lobby
     */
    public Optional<PredictionCheck> predictionCheck() {
        synchronized (lock) {
            return Optional.ofNullable(predictionCheck);
        }
    }

    /**
     * Describes an event's detail with the names and logic rate of this state, see {@link Format#eventDetail}.
     *
     * @param event an event
     * @return the detail text, possibly empty
     */
    public String eventDetail(StatEvent event) {
        return Format.eventDetail(event, this::playerName, logicFps());
    }

    // -- Feeding.

    @Override
    public void onHello(Hello message) {
        synchronized (lock) {
            hello = message;
        }
        publish(l -> l.helloReceived(message));
    }

    @Override
    public void onFramesDropped(long count) {
        long total;
        synchronized (lock) {
            droppedFrames += count;
            total = droppedFrames;
        }
        publish(l -> l.framesDropped(count, total));
    }

    @Override
    public void onUiState(UiState message) {
        synchronized (lock) {
            ui = message;
        }
        publish(l -> l.uiChanged(message));
    }

    @Override
    public void onLobbyState(LobbyState message) {
        synchronized (lock) {
            lobby = message;
        }
        publish(l -> l.lobbyChanged(message));
    }

    @Override
    public void onChatMessage(ChatMessage message) {
        synchronized (lock) {
            chat.addLast(message);
            while (chat.size() > chatCapacity) {
                chat.removeFirst();
            }
        }
        publish(l -> l.chatAdded(message));
    }

    @Override
    public void onMatchSummary(MatchSummary message) {
        synchronized (lock) {
            summary = message;
        }
        publish(l -> l.summaryChanged(message));
    }

    @Override
    public void onCamera(Camera message) {
        synchronized (lock) {
            camera = message;
            cameraFrames++;
        }
        publish(l -> l.cameraChanged(message));
    }

    @Override
    public void onViewStats(ViewStats message) {
        synchronized (lock) {
            viewStats = message;
        }
        publish(l -> l.viewStatsChanged(message));
    }

    @Override
    public void onInterestMap(InterestMap message) {
        synchronized (lock) {
            interestMap = message;
        }
        publish(l -> l.interestMapChanged(message));
    }

    @Override
    public void onMatchInfo(MatchInfo message) {
        List<PlayerView> views;
        boolean newMatch;
        synchronized (lock) {
            // A match ends with the same id it started with; LOADING always begins a fresh one. Rows that arrived
            // before any MatchInfo (no id known yet) belong to the match this first MatchInfo describes.
            boolean differentMatch = !matchId.isEmpty() && !message.getMatchId().equals(matchId);
            newMatch = message.getPhase() != MatchPhase.MATCH_PHASE_ENDED
                && (message.getPhase() == MatchPhase.MATCH_PHASE_LOADING || differentMatch);
            if (newMatch) {
                clearMatchData();
            }
            matchId = message.getMatchId();
            match = message;
            matchPlayers = List.copyOf(message.getPlayersList());
            infoByIndex.clear();
            for (MatchPlayer p : message.getPlayersList()) {
                if (p.getPlayerIndex() >= 0) {
                    infoByIndex.put(p.getPlayerIndex(), p);
                }
            }
            if (message.getLogicFramesPerSecond() > 0) {
                logicFps = message.getLogicFramesPerSecond();
            }
            // The extent is unset while LOADING (the map is not loaded yet); keep an earlier one if a later frame omits it.
            if (message.getHasWorldExtent() && message.getWorldMaxX() > message.getWorldMinX() && message.getWorldMaxY() > message.getWorldMinY()) {
                extent = new WorldExtent(message.getWorldMinX(), message.getWorldMinY(), message.getWorldMaxX(), message.getWorldMaxY());
            }
            if (message.getHasPredictionCheck()) {
                predictionCheck = new PredictionCheck(message.getPredictionMatched(), message.getRandomStateClean());
            }
            rebuildPlayers();
            views = playerViews;
        }
        List<Consumer<StateListener>> calls = new ArrayList<>();
        if (newMatch) {
            calls.add(StateListener::matchReset);
        }
        calls.add(l -> l.matchChanged(message));
        if (newMatch || !views.isEmpty()) {
            calls.add(l -> l.playersChanged(views));
        }
        calls.add(StateListener::worldChanged);
        publishAll(calls);
    }

    @Override
    public void onStatsSnapshot(StatsSnapshot message) {
        List<PlayerView> views = null;
        boolean full = message.getFull();
        synchronized (lock) {
            snapshotCount++;
            frame = Format.u(message.getFrame());
            stalled = message.getNetworkStalled();
            stalledFrames = Format.u(message.getStalledFrames());
            // A stall snapshot reports no game speed (0 would read as "paused"); keep the last known one.
            if (!stalled) {
                speedKnown = true;
                speedFps = message.getGameSpeedFps();
                speedRatio = message.getGameSpeedRatio();
            }
            if (message.getLocalRenderFps() > 0 || message.getLocalLogicFps() > 0) {
                localRenderFps = message.getLocalRenderFps();
                localLogicFps = message.getLocalLogicFps();
            }
            // Light snapshots carry no grids, supply, tech or connections: keep the lists of the last full snapshot.
            if (full) {
                grids = List.copyOf(message.getGridsList());
                supply = List.copyOf(message.getSupplyList());
                tech = List.copyOf(message.getTechList());
                connections = List.copyOf(message.getConnectionsList());
            }
            // A stall snapshot has no players; it must not clear the table. A light snapshot keeps the heavy values of the last full one.
            if (message.getPlayersCount() > 0) {
                for (PlayerStats stats : message.getPlayersList()) {
                    Entry previous = entries.get(stats.getPlayerIndex());
                    PlayerStats shown = !full && previous != null ? mergeHeavyFields(stats, previous.stats()) : stats;
                    entries.put(stats.getPlayerIndex(), new Entry(shown, frame));
                }
                rebuildPlayers();
                views = playerViews;
            }
        }
        List<Consumer<StateListener>> calls = new ArrayList<>();
        calls.add(l -> l.snapshotApplied(message));
        if (full) {
            calls.add(StateListener::worldChanged);
        }
        if (views != null) {
            List<PlayerView> changed = views;
            calls.add(l -> l.playersChanged(changed));
        }
        publishAll(calls);
    }

    @Override
    public void onEvents(Events message) {
        if (message.getEventsCount() == 0) {
            return;
        }
        List<StatEvent> added = List.copyOf(message.getEventsList());
        List<PlayerView> views = null;
        synchronized (lock) {
            boolean lifeChanged = false;
            for (StatEvent e : added) {
                events.addLast(e);
                while (events.size() > eventCapacity) {
                    events.removeFirst();
                }
                latestEventFrame = Math.max(latestEventFrame, Format.u(e.getFrame()));
                lifeChanged |= noteLife(e);
            }
            if (lifeChanged) {
                rebuildPlayers();
                views = playerViews;
            }
        }
        List<Consumer<StateListener>> calls = new ArrayList<>();
        calls.add(l -> l.eventsAdded(added));
        if (views != null) {
            List<PlayerView> changed = views;
            calls.add(l -> l.playersChanged(changed));
        }
        publishAll(calls);
    }

    /**
     * Merges a light snapshot's player with the previous values. A light snapshot carries only the cheap per-player
     * numbers; the fields that need a walk over the live objects (listed at {@code StatsSnapshot} in the proto) are
     * taken from the previous values instead.
     *
     * @param light the player from a light snapshot
     * @param previous the same player as shown so far
     * @return {@code light} with the heavy fields of {@code previous}
     */
    public static PlayerStats mergeHeavyFields(PlayerStats light, PlayerStats previous) {
        return light.toBuilder()
            .setArmyValue(previous.getArmyValue())
            .setLiveSuperweaponStructureCount(previous.getLiveSuperweaponStructureCount())
            .clearLiveTemplateCounts().addAllLiveTemplateCounts(previous.getLiveTemplateCountsList())
            .clearReadiness().addAllReadiness(previous.getReadinessList())
            .clearSciences().addAllSciences(previous.getSciencesList())
            .clearUpgrades().addAllUpgrades(previous.getUpgradesList())
            .clearProduction().addAllProduction(previous.getProductionList())
            .setWorkers(previous.getWorkers())
            .setHarvesters(previous.getHarvesters())
            .setIdleWorkers(previous.getIdleWorkers())
            .clearHeroes().addAllHeroes(previous.getHeroesList())
            .clearSuperweapons().addAllSuperweapons(previous.getSuperweaponsList())
            .setArmyValueByClass(previous.getArmyValueByClass())
            .setArmyCountByClass(previous.getArmyCountByClass())
            .clearTechStructures().addAllTechStructures(previous.getTechStructuresList())
            .setBattlePlanTransition(previous.getBattlePlanTransition())
            .build();
    }

    // -- Internals.

    /** Forgets everything that belongs to the previous match. Called with the lock held. */
    private void clearMatchData() {
        entries.clear();
        events.clear();
        lifeStates.clear();
        infoByIndex.clear();
        summary = null;
        extent = null;
        grids = List.of();
        supply = List.of();
        tech = List.of();
        connections = List.of();
        camera = null;
        cameraFrames = 0;
        viewStats = null;
        interestMap = null;
        frame = -1;
        latestEventFrame = -1;
        stalled = false;
        stalledFrames = 0;
        snapshotCount = 0;
        speedKnown = false;
        speedFps = 0;
        speedRatio = 1.0f;
        localRenderFps = 0;
        localLogicFps = 0;
        predictionCheck = null;
    }

    /** Records a PLAYER_DEFEATED or PLAYER_LEFT event; returns whether the player's state changed. Lock held. */
    private boolean noteLife(StatEvent e) {
        PlayerLife before = lifeStates.getOrDefault(e.getPlayerIndex(), PlayerLife.ALIVE);
        PlayerLife after = before;
        if (e.getKind() == StatEventKind.STATEVENT_PLAYER_LEFT) {
            after = PlayerLife.LEFT;
        } else if (e.getKind() == StatEventKind.STATEVENT_PLAYER_DEFEATED && before == PlayerLife.ALIVE) {
            after = PlayerLife.DEFEATED;
        }
        if (after != before) {
            lifeStates.put(e.getPlayerIndex(), after);
            return true;
        }
        return false;
    }

    /** The engine's own state wins; the events cover the moment between the event and the next snapshot. */
    private PlayerLife lifeOf(PlayerStats stats) {
        switch (stats.getState()) {
            case PLAYER_STATE_DEFEATED:
                return PlayerLife.DEFEATED;
            case PLAYER_STATE_LEFT:
                return PlayerLife.LEFT;
            default:
                return lifeStates.getOrDefault(stats.getPlayerIndex(), PlayerLife.ALIVE);
        }
    }

    /** Rebuilds the immutable player views from the entries. Called with the lock held. */
    private void rebuildPlayers() {
        List<PlayerView> views = new ArrayList<>(entries.size());
        Map<Integer, PlayerView> byIndex = new LinkedHashMap<>();
        for (Map.Entry<Integer, Entry> e : entries.entrySet()) {
            Entry entry = e.getValue();
            PlayerView view = new PlayerView(e.getKey(), infoByIndex.get(e.getKey()), entry.stats(), entry.frame(), lifeOf(entry.stats()));
            views.add(view);
            byIndex.put(e.getKey(), view);
        }
        playerViews = Collections.unmodifiableList(views);
        playersByIndex = Collections.unmodifiableMap(byIndex);
    }

    /**
     * Calls a method of every change listener, after the lock was released. A listener that throws does not stop the
     * others; the first exception is rethrown afterwards so the pipe reports it through {@link StatsListener#onError}.
     */
    private void publish(Consumer<StateListener> call) {
        publishAll(List.of(call));
    }

    private void publishAll(List<Consumer<StateListener>> calls) {
        RuntimeException first = null;
        for (Consumer<StateListener> call : calls) {
            for (StateListener listener : listeners) {
                try {
                    call.accept(listener);
                } catch (RuntimeException e) {
                    if (first == null) {
                        first = e;
                    } else {
                        first.addSuppressed(e);
                    }
                }
            }
        }
        if (first != null) {
            throw first;
        }
    }
}
