package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.ViewStats;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

/** The view statistics and the interest map in the state: the latest value, the callbacks, and the match boundary. */
class ViewFeedStateTest {
    @Test
    void theLatestViewStatsAndInterestMapAreKeptAndAnnounced() {
        MatchState state = new MatchState();
        List<String> calls = new ArrayList<>();
        state.addListener(new StateListener() {
            @Override
            public void viewStatsChanged(ViewStats stats) {
                calls.add("view " + stats.getFrame());
            }

            @Override
            public void interestMapChanged(InterestMap map) {
                calls.add("interest " + map.getFrame());
            }
        });
        assertTrue(state.viewStats().isEmpty());
        assertTrue(state.interestMap().isEmpty());

        ViewStats view = TestMessages.viewStats();
        InterestMap interest = TestMessages.interestMap();
        state.onViewStats(view);
        state.onInterestMap(interest);
        assertSame(view, state.viewStats().orElseThrow());
        assertSame(interest, state.interestMap().orElseThrow());
        assertEquals(List.of("view 901", "interest 902"), calls);

        ViewStats newer = view.toBuilder().setFrame(950).build();
        state.onViewStats(newer);
        assertSame(newer, state.viewStats().orElseThrow());
    }

    @Test
    void aNewMatchForgetsThemAndTheSameMatchStartedAgainKeepsThem() {
        MatchState state = new MatchState();
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("a").build());
        state.onViewStats(TestMessages.viewStats());
        state.onInterestMap(TestMessages.interestMap());
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("a").build());
        assertTrue(state.viewStats().isPresent() && state.interestMap().isPresent(), "a repeated STARTED changes nothing");
        state.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_LOADING).setMatchId("b").build());
        assertTrue(state.viewStats().isEmpty());
        assertTrue(state.interestMap().isEmpty());
    }

    @Test
    void theNewFramesAreOutboundAndReachTheirTypedMethods() {
        assertEquals(0x0B, FrameType.VIEW_STATS.id());
        assertEquals(0x0C, FrameType.INTEREST_MAP.id());
        assertEquals(FrameType.VIEW_STATS, FrameType.outboundFromId(0x0B));
        assertEquals(FrameType.INTEREST_MAP, FrameType.outboundFromId(0x0C));
        RecordingListener listener = new RecordingListener();
        FrameDispatcher.dispatch(new RawFrame(0x0B, TestMessages.viewStats().toByteArray()), listener);
        FrameDispatcher.dispatch(new RawFrame(0x0C, TestMessages.interestMap().toByteArray()), listener);
        assertEquals(List.of(TestMessages.viewStats(), TestMessages.interestMap()), listener.messages);
    }
}
