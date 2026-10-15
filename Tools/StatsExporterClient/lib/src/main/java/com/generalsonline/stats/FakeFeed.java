package com.generalsonline.stats;

import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.StatsSnapshot;

/**
 * One reader's feed configuration and the filtering it implies. The fake game keeps one per connection; the rules are
 * those of the game's {@code FeedConfig} (defaults, ranges, what a setting out of range does).
 */
final class FakeFeed {
    private FeedConfig config = defaults();
    private long lastLightFrame = Long.MIN_VALUE;
    private long lastFullFrame = Long.MIN_VALUE;
    private long lastViewNanos = Long.MIN_VALUE;
    private long lastInterestNanos = Long.MIN_VALUE;

    static FeedConfig defaults() {
        return FeedConfig.newBuilder().setLightIntervalMs(100).setFullIntervalMs(250).setEvents(true).setCamera(true).setChat(true).setGrids(true)
            .setSupply(true).setTech(true).setProduction(true).setGridResolution(32).setViewStatsIntervalMs(250).setInterest(false)
            .setInterestIntervalMs(500).setNotableCost(2000).setTradeWindowMs(15000).setIntensityWindowMs(5000).build();
    }

    synchronized FeedConfig config() {
        return config;
    }

    /**
     * Applies a query.
     *
     * @return null when applied, else the reason the whole query was refused (nothing changes then)
     */
    synchronized String apply(FeedConfigQuery q) {
        FeedConfig.Builder next = (q.getReset() ? defaults() : config).toBuilder();
        if (q.hasLightIntervalMs()) {
            next.setLightIntervalMs(q.getLightIntervalMs());
        }
        if (q.hasFullIntervalMs()) {
            next.setFullIntervalMs(q.getFullIntervalMs());
        }
        if (q.hasEvents()) {
            next.setEvents(q.getEvents());
        }
        if (q.hasCamera()) {
            next.setCamera(q.getCamera());
        }
        if (q.hasChat()) {
            next.setChat(q.getChat());
        }
        if (q.hasGrids()) {
            next.setGrids(q.getGrids());
        }
        if (q.hasSupply()) {
            next.setSupply(q.getSupply());
        }
        if (q.hasTech()) {
            next.setTech(q.getTech());
        }
        if (q.hasProduction()) {
            next.setProduction(q.getProduction());
        }
        if (q.hasGridResolution()) {
            next.setGridResolution(q.getGridResolution());
        }
        if (q.hasViewStatsIntervalMs()) {
            next.setViewStatsIntervalMs(q.getViewStatsIntervalMs());
        }
        if (q.hasInterest()) {
            next.setInterest(q.getInterest());
        }
        if (q.hasInterestIntervalMs()) {
            next.setInterestIntervalMs(q.getInterestIntervalMs());
        }
        if (q.hasNotableCost()) {
            next.setNotableCost(q.getNotableCost());
        }
        if (q.hasTradeWindowMs()) {
            next.setTradeWindowMs(q.getTradeWindowMs());
        }
        if (q.hasIntensityWindowMs()) {
            next.setIntensityWindowMs(q.getIntensityWindowMs());
        }
        String problem = check(next);
        if (problem == null) {
            config = next.build();
        }
        return problem;
    }

    private static String check(FeedConfig.Builder c) {
        if (c.getLightIntervalMs() != 0 && (c.getLightIntervalMs() < 50 || c.getLightIntervalMs() > 60000)) {
            return "light_interval_ms must be 0 or 50 to 60000";
        }
        if (c.getFullIntervalMs() != 0 && (c.getFullIntervalMs() < 100 || c.getFullIntervalMs() > 60000)) {
            return "full_interval_ms must be 0 or 100 to 60000";
        }
        int grid = c.getGridResolution();
        if (grid != 8 && grid != 16 && grid != 32 && grid != 64) {
            return "grid_resolution must be 8, 16, 32 or 64";
        }
        if (c.getViewStatsIntervalMs() != 0 && (c.getViewStatsIntervalMs() < 100 || c.getViewStatsIntervalMs() > 60000)) {
            return "view_stats_interval_ms must be 0 or 100 to 60000";
        }
        if (c.getInterestIntervalMs() < 250 || c.getInterestIntervalMs() > 60000) {
            return "interest_interval_ms must be 250 to 60000";
        }
        if (c.getTradeWindowMs() < 1000 || c.getTradeWindowMs() > 60000) {
            return "trade_window_ms must be 1000 to 60000";
        }
        if (c.getIntensityWindowMs() < 1000 || c.getIntensityWindowMs() > 60000) {
            return "intensity_window_ms must be 1000 to 60000";
        }
        return null;
    }

    /**
     * Thins and trims a snapshot for this reader.
     *
     * @return the snapshot to send, or null when this reader's cadence skips it
     */
    synchronized StatsSnapshot snapshot(StatsSnapshot s, int logicFps) {
        boolean stall = s.getPlayersCount() == 0 && !s.getFull();
        if (!stall) {
            long frame = Format.u(s.getFrame());
            int interval = s.getFull() ? config.getFullIntervalMs() : config.getLightIntervalMs();
            if (interval == 0) {
                return null;
            }
            long gap = Math.max(1, Math.round(interval * logicFps / 1000.0));
            long last = s.getFull() ? lastFullFrame : lastLightFrame;
            if (last != Long.MIN_VALUE && frame >= last && frame - last < gap) {
                return null;
            }
            if (s.getFull()) {
                lastFullFrame = frame;
            } else {
                lastLightFrame = frame;
            }
        }
        if (!s.getFull() || (config.getGrids() && config.getSupply() && config.getTech() && config.getProduction())) {
            return s;
        }
        StatsSnapshot.Builder b = s.toBuilder();
        if (!config.getGrids()) {
            b.clearGrids();
        }
        if (!config.getSupply()) {
            b.clearSupply();
        }
        if (!config.getTech()) {
            b.clearTech();
        }
        if (!config.getProduction()) {
            for (int i = 0; i < b.getPlayersCount(); i++) {
                b.getPlayersBuilder(i).clearProduction();
            }
        }
        return b.build();
    }

    /** True once per configured view stats interval of wall-clock time; 0 turns them off. */
    synchronized boolean viewStatsDue(long nowNanos) {
        return due(config.getViewStatsIntervalMs(), nowNanos, true);
    }

    /** True once per configured interest interval of wall-clock time while the interest feed is on. */
    synchronized boolean interestDue(long nowNanos) {
        return config.getInterest() && due(config.getInterestIntervalMs(), nowNanos, false);
    }

    /** Makes the next interest map due at once (a new highlight sends one). */
    synchronized void interestNow() {
        lastInterestNanos = Long.MIN_VALUE;
    }

    private boolean due(int intervalMs, long nowNanos, boolean view) {
        if (intervalMs == 0) {
            return false;
        }
        long last = view ? lastViewNanos : lastInterestNanos;
        if (last != Long.MIN_VALUE && nowNanos - last < intervalMs * 1_000_000L) {
            return false;
        }
        if (view) {
            lastViewNanos = nowNanos;
        } else {
            lastInterestNanos = nowNanos;
        }
        return true;
    }
}
