package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.HighlightKind;
import com.generalsonline.stats.proto.InterestHighlight;
import com.generalsonline.stats.proto.InterestMap;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * The decisions of an automatic camera director, with no I/O and no clock of its own: feed it the interest maps and
 * camera frames, ask it with {@link #poll} what the camera should do now, and pass the time of every call in. Given the
 * same inputs it always decides the same, which is what makes it testable; {@link Director} wires it to a game.
 *
 * <p>What it does depends on the {@link DirectorMode}:
 * <ul>
 * <li>{@code HOTSPOT}: goes to the best cell of the interest map. It stays at least the minimum dwell time, and
 *     switches only to a cell that scores {@code switchRatio} times as much as the current place does now. A short way is
 *     panned, a long one is a cut.</li>
 * <li>{@code HIGHLIGHTS}: idle until the map lists a highlight. A superweapon with a known impact time is shown so that the
 *     camera is in place the lead time before the impact; a hero death or a big structure lost is shown at once. The
 *     camera stays until the hold time after the impact has passed and goes back to where it was.</li>
 * <li>{@code OVERVIEW}: when it has been quiet for a while, visits every active player's base and army one after the
 *     other; when a place scores as a fight, follows it like {@code HOTSPOT} until it is quiet again.</li>
 * <li>{@code DIRECTOR}: all of it. Highlights interrupt, hotspots are followed, and the overview fills the quiet.</li>
 * </ul>
 *
 * <p>User input pauses everything: when a {@code Camera} frame says a human moved the camera, the director sends nothing
 * until the {@link DirectorConfig#resumeDelay() resume delay} has passed without input, or {@link #resume} is called.
 *
 * <p>Not thread safe; {@link Director} serializes the calls.
 */
public final class DirectorLogic {
    /**
     * A place the overview can visit.
     *
     * @param playerIndex the player it belongs to
     * @param x the world x
     * @param y the world y
     * @param kind whether it is a base or an army
     */
    public record Spot(int playerIndex, double x, double y, Kind kind) {
        /** What the spot is. */
        public enum Kind {
            /** A player's start position. */
            BASE,
            /** The centre of a player's units and structures. */
            ARMY
        }
    }

    /** What the logic is doing. */
    public enum State {
        /** No interest map yet, or nothing worth showing. */
        WAITING,
        /** Following the best cell. */
        HOTSPOT,
        /** Showing a highlight. */
        HIGHLIGHT,
        /** Touring bases and armies in a quiet time. */
        OVERVIEW,
        /** Stopped because a human moved the camera; resumes by itself after the resume delay. */
        PAUSED_BY_USER,
        /** Stopped by {@link DirectorLogic#pause}. */
        PAUSED
    }

    /**
     * A snapshot of what the logic is doing.
     *
     * @param mode the mode in force
     * @param state what it is doing
     * @param hasTarget whether it has put the camera somewhere
     * @param targetX the world x of the last move, meaningful when {@code hasTarget}
     * @param targetY the world y of the last move, meaningful when {@code hasTarget}
     * @param pendingHighlights the highlights it knows of and has not shown yet
     */
    public record Status(DirectorMode mode, State state, boolean hasTarget, double targetX, double targetY, int pendingHighlights) {
    }

    /** How long a superweapon without a known impact time is waited for after the camera arrived. */
    private static final long UNKNOWN_IMPACT_WAIT_MS = 6000;
    private static final int HANDLED_MEMORY = 512;

    private static final class Pending {
        final int id;
        final HighlightKind kind;
        final double x;
        final double y;
        long impactAtMs = -1;
        long goAtMs;
        long expireAtMs;

        Pending(int id, HighlightKind kind, double x, double y) {
            this.id = id;
            this.kind = kind;
            this.x = x;
            this.y = y;
        }

        int priority() {
            return switch (kind) {
                case HIGHLIGHT_SUPERWEAPON_LAUNCH -> 0;
                case HIGHLIGHT_HERO_DEATH -> 1;
                default -> 2;
            };
        }
    }

    private static final class Showing {
        final Pending highlight;
        long releaseAtMs;
        final boolean hasReturn;
        final double returnX;
        final double returnY;

        Showing(Pending highlight, long releaseAtMs, boolean hasReturn, double returnX, double returnY) {
            this.highlight = highlight;
            this.releaseAtMs = releaseAtMs;
            this.hasReturn = hasReturn;
            this.returnX = returnX;
            this.returnY = returnY;
        }
    }

    private record Best(double x, double y, double score, double cellSize) {
    }

    private DirectorConfig config;

    private InterestMap interest;
    private int logicFps = 30;
    private double speedRatio = 1.0;
    private boolean cameraKnown;
    private double cameraX;
    private double cameraY;
    private boolean countKnown;
    private int lastUserCount;
    private long lastUserInputMs;
    private boolean userPaused;
    private boolean manualPaused;
    private List<Spot> spots = List.of();

    private boolean hasTarget;
    private double targetX;
    private double targetY;
    private long lastMoveMs = Long.MIN_VALUE / 2;
    private long quietSinceMs = -1;
    private boolean overviewActive;
    private long overviewNextMs;
    private int overviewIndex;
    private State state = State.WAITING;
    private Showing showing;
    private final Map<Integer, Pending> pending = new LinkedHashMap<>();
    private final Set<Integer> handled = new LinkedHashSet<>();

    /**
     * Creates the logic.
     *
     * @param config the tunables and the starting mode
     */
    public DirectorLogic(DirectorConfig config) {
        this.config = Objects.requireNonNull(config, "config");
    }

    // -- Setup.

    /**
     * Returns the configuration in force.
     *
     * @return the current configuration, including the mode
     */
    public DirectorConfig config() {
        return config;
    }

    /**
     * Replaces the configuration. Whatever the logic was in the middle of is dropped, so the next {@link #poll} decides afresh.
     *
     * @param config the new configuration
     */
    public void setConfig(DirectorConfig config) {
        this.config = Objects.requireNonNull(config, "config");
        dropTransientState();
    }

    /**
     * Changes the mode.
     *
     * @param mode the new mode
     */
    public void setMode(DirectorMode mode) {
        setConfig(config.toBuilder().mode(mode).build());
    }

    /**
     * Returns the mode in force.
     *
     * @return the mode
     */
    public DirectorMode mode() {
        return config.mode();
    }

    /** Forgets everything of the match: the map, the highlights, the target. Pauses stay. Call when a new match begins. */
    public void reset() {
        interest = null;
        cameraKnown = false;
        countKnown = false;
        userPaused = false;
        spots = List.of();
        pending.clear();
        handled.clear();
        overviewIndex = 0;
        dropTransientState();
        state = State.WAITING;
    }

    private void dropTransientState() {
        hasTarget = false;
        lastMoveMs = Long.MIN_VALUE / 2;
        quietSinceMs = -1;
        overviewActive = false;
        showing = null;
    }

    /**
     * Sets the places the overview visits, in the order it visits them.
     *
     * @param spots the bases and armies of the active players
     */
    public void setOverviewSpots(List<Spot> spots) {
        this.spots = List.copyOf(spots);
    }

    // -- Pausing.

    /** Stops the director until {@link #resume}. */
    public void pause() {
        manualPaused = true;
        showing = null;
    }

    /**
     * Lets the director act again at once, also after a user pause.
     *
     * @param nowMs the current time
     */
    public void resume(long nowMs) {
        manualPaused = false;
        if (userPaused) {
            userPaused = false;
            lastUserInputMs = nowMs - config.resumeDelay().toMillis();
            dropTransientState();
        }
    }

    /**
     * Tells whether the director is stopped.
     *
     * @return true after {@link #pause}, or while a user's camera input holds it
     */
    public boolean isPaused() {
        return manualPaused || userPaused;
    }

    // -- Inputs.

    /**
     * Takes a new interest map.
     *
     * @param map the map; its highlights are noted
     * @param nowMs the time it arrived
     * @param logicFps the game's logic rate, to turn frames into time
     * @param speedRatio the current pace of the game as a ratio of normal speed
     */
    public void onInterest(InterestMap map, long nowMs, int logicFps, double speedRatio) {
        interest = Objects.requireNonNull(map, "map");
        this.logicFps = Math.max(1, logicFps);
        this.speedRatio = speedRatio > 0.0 ? speedRatio : 1.0;
        long frame = Integer.toUnsignedLong(map.getFrame());
        for (InterestHighlight h : map.getHighlightsList()) {
            int id = h.getId();
            Pending known = pending.get(id);
            if (known == null && handled.contains(id)) {
                continue;
            }
            long impactAt = h.getImpactFrame() != 0 ? nowMs + framesToMs(Integer.toUnsignedLong(h.getImpactFrame()) - frame) : -1;
            if (known == null) {
                known = new Pending(id, h.getKind(), h.getX(), h.getY());
                pending.put(id, known);
            }
            known.expireAtMs = nowMs + Math.max(1000, framesToMs(Integer.toUnsignedLong(h.getExpireFrame()) - frame));
            if (impactAt >= 0) {
                known.impactAtMs = impactAt;
                if (showing != null && showing.highlight == known) {
                    showing.releaseAtMs = Math.max(showing.releaseAtMs, impactAt + config.highlightHold().toMillis());
                }
            }
            known.goAtMs = known.impactAtMs >= 0
                ? Math.max(nowMs, known.impactAtMs - config.highlightLead().toMillis() - config.maxPan().toMillis())
                : nowMs;
        }
    }

    /**
     * Takes a camera frame: where the camera is, and whether a human moved it.
     *
     * @param camera the frame
     * @param nowMs the time it arrived
     */
    public void onCamera(Camera camera, long nowMs) {
        Objects.requireNonNull(camera, "camera");
        cameraKnown = true;
        cameraX = camera.getX();
        cameraY = camera.getY();
        boolean user = camera.getChangeSource() == CameraChangeSource.CAMERA_CHANGE_USER || (countKnown && camera.getUserInputCount() != lastUserCount);
        lastUserCount = camera.getUserInputCount();
        countKnown = true;
        if (user) {
            lastUserInputMs = nowMs;
            if (!userPaused) {
                userPaused = true;
                showing = null;
            }
        }
    }

    // -- Deciding.

    /**
     * Decides what the camera should do now.
     *
     * @param nowMs the current time
     * @return the moves to make, usually none and at most one
     */
    public List<DirectorMove> poll(long nowMs) {
        List<DirectorMove> out = new ArrayList<>();
        if (userPaused && nowMs - lastUserInputMs >= config.resumeDelay().toMillis()) {
            userPaused = false;
            dropTransientState();
        }
        if (manualPaused) {
            state = State.PAUSED;
            return out;
        }
        if (userPaused) {
            state = State.PAUSED_BY_USER;
            return out;
        }
        if (interest == null) {
            state = State.WAITING;
            return out;
        }

        for (Iterator<Pending> it = pending.values().iterator(); it.hasNext();) {
            Pending p = it.next();
            if (p.expireAtMs <= nowMs && (showing == null || showing.highlight != p)) {
                // Expired unseen: the map may list it for a few more frames, and it must not come back.
                remember(p.id);
                it.remove();
            }
        }
        DirectorMode mode = config.mode();
        boolean highlights = mode == DirectorMode.HIGHLIGHTS || mode == DirectorMode.DIRECTOR;

        if (showing != null) {
            Pending interrupt = highlights ? due(nowMs) : null;
            if (interrupt != null && interrupt.priority() == 0 && showing.highlight.priority() > 0) {
                show(interrupt, nowMs, showing.hasReturn, showing.returnX, showing.returnY, out);
            } else if (nowMs >= showing.releaseAtMs) {
                finishHighlight(nowMs, out);
            }
            state = showing != null ? State.HIGHLIGHT : State.WAITING;
            return out;
        }
        if (highlights) {
            Pending next = due(nowMs);
            if (next != null) {
                show(next, nowMs, hasTarget || cameraKnown, hasTarget ? targetX : cameraX, hasTarget ? targetY : cameraY, out);
                state = State.HIGHLIGHT;
                return out;
            }
        }

        state = State.WAITING;
        if (mode == DirectorMode.HOTSPOT || mode == DirectorMode.DIRECTOR) {
            if (hotspot(nowMs, config.minScore(), out)) {
                state = State.HOTSPOT;
            } else if (mode == DirectorMode.DIRECTOR && overview(nowMs, out)) {
                state = State.OVERVIEW;
            }
        } else if (mode == DirectorMode.OVERVIEW) {
            if (hotspot(nowMs, config.fightScore(), out)) {
                state = State.HOTSPOT;
            } else if (overview(nowMs, out)) {
                state = State.OVERVIEW;
            }
        }
        return out;
    }

    /**
     * Describes what the logic is doing.
     *
     * @return the status as of the last {@link #poll}; a pause shows at once
     */
    public Status status() {
        State shown = manualPaused ? State.PAUSED : (userPaused ? State.PAUSED_BY_USER : state);
        return new Status(config.mode(), shown, hasTarget, targetX, targetY, pending.size());
    }

    // -- Highlights.

    private long framesToMs(long frames) {
        return Math.round(frames * 1000.0 / logicFps / Math.max(0.1, speedRatio));
    }

    /** The pending highlight that is due and matters most. */
    private Pending due(long nowMs) {
        return pending.values().stream()
            .filter(p -> p.goAtMs <= nowMs && p.expireAtMs > nowMs && (showing == null || showing.highlight != p))
            .min(Comparator.<Pending>comparingInt(Pending::priority)
                .thenComparingLong(p -> p.impactAtMs >= 0 ? p.impactAtMs : Long.MAX_VALUE)
                .thenComparingInt(p -> p.id))
            .orElse(null);
    }

    private void show(Pending p, long nowMs, boolean hasReturn, double returnX, double returnY, List<DirectorMove> out) {
        if (showing != null) {
            remember(showing.highlight.id);
            pending.remove(showing.highlight.id);
        }
        DirectorMove move = moveTo(p.x, p.y, DirectorMove.Reason.HIGHLIGHT, nowMs, out);
        long arrival = nowMs + (move != null ? move.duration().toMillis() : 0);
        long release = p.impactAtMs >= 0
            ? Math.max(p.impactAtMs, arrival) + config.highlightHold().toMillis()
            : arrival + config.highlightHold().toMillis() + (p.kind == HighlightKind.HIGHLIGHT_SUPERWEAPON_LAUNCH ? UNKNOWN_IMPACT_WAIT_MS : 0);
        showing = new Showing(p, release, hasReturn, returnX, returnY);
        overviewActive = false;
    }

    private void finishHighlight(long nowMs, List<DirectorMove> out) {
        Showing done = showing;
        showing = null;
        remember(done.highlight.id);
        pending.remove(done.highlight.id);
        if (config.returnAfterHighlight() && done.hasReturn) {
            moveTo(done.returnX, done.returnY, DirectorMove.Reason.RETURN, nowMs, out);
        } else {
            hasTarget = false;
            lastMoveMs = nowMs;
        }
        quietSinceMs = -1;
    }

    private void remember(int id) {
        handled.add(id);
        Iterator<Integer> oldest = handled.iterator();
        while (handled.size() > HANDLED_MEMORY && oldest.hasNext()) {
            oldest.next();
            oldest.remove();
        }
    }

    // -- Hotspots and the overview.

    /** Follows the best cell; returns whether there is one that scores at least {@code engage}. */
    private boolean hotspot(long nowMs, double engage, List<DirectorMove> out) {
        Best best = best(interest);
        if (best == null || best.score() < engage || best.score() <= 0.0) {
            if (quietSinceMs < 0) {
                quietSinceMs = nowMs;
            }
            return false;
        }
        quietSinceMs = -1;
        if (overviewActive) {
            overviewActive = false;
            hasTarget = false;
        }
        if (!hasTarget) {
            moveTo(best.x(), best.y(), DirectorMove.Reason.HOTSPOT, nowMs, out);
            return true;
        }
        double current = scoreAt(interest, targetX, targetY);
        boolean elsewhere = Math.hypot(best.x() - targetX, best.y() - targetY) > 1.5 * best.cellSize();
        if (elsewhere && nowMs - lastMoveMs >= config.minDwell().toMillis() && best.score() >= current * config.switchRatio()) {
            moveTo(best.x(), best.y(), DirectorMove.Reason.HOTSPOT, nowMs, out);
        }
        return true;
    }

    /** Tours the spots when it has been quiet; returns whether the tour is on. */
    private boolean overview(long nowMs, List<DirectorMove> out) {
        if (spots.isEmpty() || quietSinceMs < 0 || nowMs - quietSinceMs < config.quietDelay().toMillis()) {
            return overviewActive;
        }
        if (!overviewActive) {
            overviewActive = true;
            overviewNextMs = nowMs;
        }
        if (nowMs >= overviewNextMs) {
            Spot spot = spots.get(overviewIndex % spots.size());
            overviewIndex++;
            moveTo(spot.x(), spot.y(), DirectorMove.Reason.OVERVIEW, nowMs, out);
            overviewNextMs = nowMs + config.overviewDwell().toMillis();
        }
        return true;
    }

    /** Adds a move to the output unless the camera is already there, and remembers it as the target. */
    private DirectorMove moveTo(double x, double y, DirectorMove.Reason reason, long nowMs, List<DirectorMove> out) {
        boolean known = cameraKnown || hasTarget;
        double fromX = cameraKnown ? cameraX : targetX;
        double fromY = cameraKnown ? cameraY : targetY;
        double distance = known ? Math.hypot(x - fromX, y - fromY) : Double.POSITIVE_INFINITY;
        hasTarget = true;
        targetX = x;
        targetY = y;
        lastMoveMs = nowMs;
        if (distance < 1.0) {
            return null;
        }
        boolean cut = distance > config.panLimit();
        long millis = Math.round(distance / config.panSpeed() * 1000.0);
        millis = Math.max(config.minPan().toMillis(), Math.min(config.maxPan().toMillis(), millis));
        DirectorMove move = new DirectorMove(x, y, cut ? Duration.ZERO : Duration.ofMillis(millis), !cut, config.zoom(), reason);
        out.add(move);
        return move;
    }

    // -- Reading the map.

    /** The cell with the highest smoothed score, and the centre of the action around it. */
    private static Best best(InterestMap m) {
        int w = m.getWidth();
        int h = m.getHeight();
        if (w <= 0 || h <= 0 || m.getScoresCount() < w * h) {
            return null;
        }
        int bestCell = -1;
        double bestScore = -1.0;
        for (int cy = 0; cy < h; cy++) {
            for (int cx = 0; cx < w; cx++) {
                double s = smoothed(m, cx, cy);
                if (s > bestScore) {
                    bestScore = s;
                    bestCell = cy * w + cx;
                }
            }
        }
        int bx = bestCell % w;
        int by = bestCell / w;
        double sumX = 0.0;
        double sumY = 0.0;
        double weight = 0.0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = bx + dx;
                int ny = by + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    double s = Integer.toUnsignedLong(m.getScores(ny * w + nx));
                    sumX += s * cellCentreX(m, nx);
                    sumY += s * cellCentreY(m, ny);
                    weight += s;
                }
            }
        }
        double x = weight > 0.0 ? sumX / weight : cellCentreX(m, bx);
        double y = weight > 0.0 ? sumY / weight : cellCentreY(m, by);
        return new Best(x, y, bestScore, Math.max(m.getCellWidth(), m.getCellHeight()));
    }

    /** A cell's score plus a quarter of its neighbours', so that a fight spread over two cells beats a lone spike. */
    private static double smoothed(InterestMap m, int cx, int cy) {
        int w = m.getWidth();
        int h = m.getHeight();
        double sum = Integer.toUnsignedLong(m.getScores(cy * w + cx));
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = cx + dx;
                int ny = cy + dy;
                if ((dx != 0 || dy != 0) && nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    sum += 0.25 * Integer.toUnsignedLong(m.getScores(ny * w + nx));
                }
            }
        }
        return sum;
    }

    private static double scoreAt(InterestMap m, double x, double y) {
        int w = m.getWidth();
        int h = m.getHeight();
        if (m.getCellWidth() <= 0.0f || m.getCellHeight() <= 0.0f) {
            return 0.0;
        }
        int cx = (int) Math.floor((x - m.getOriginX()) / m.getCellWidth());
        int cy = (int) Math.floor((y - m.getOriginY()) / m.getCellHeight());
        cx = Math.max(0, Math.min(w - 1, cx));
        cy = Math.max(0, Math.min(h - 1, cy));
        return smoothed(m, cx, cy);
    }

    private static double cellCentreX(InterestMap m, int cx) {
        return m.getOriginX() + (cx + 0.5) * m.getCellWidth();
    }

    private static double cellCentreY(InterestMap m, int cy) {
        return m.getOriginY() + (cy + 0.5) * m.getCellHeight();
    }
}
