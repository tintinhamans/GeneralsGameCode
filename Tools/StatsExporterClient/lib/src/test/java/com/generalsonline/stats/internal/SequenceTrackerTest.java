package com.generalsonline.stats.internal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class SequenceTrackerTest {
    @Test
    void consecutiveNumbersAreNoGap() {
        SequenceTracker tracker = new SequenceTracker();
        assertEquals(0, tracker.observe(0, true));
        assertEquals(0, tracker.observe(1, false));
        assertEquals(0, tracker.observe(2, false));
    }

    @Test
    void aJumpCountsTheMissingFrames() {
        SequenceTracker tracker = new SequenceTracker();
        tracker.observe(0, true);
        assertEquals(0, tracker.observe(1, false));
        assertEquals(2, tracker.observe(4, false));
        assertEquals(0, tracker.observe(5, false), "the expectation moved on");
        assertEquals(1, tracker.observe(7, false));
    }

    @Test
    void theWrapAroundOfAnUnsigned32BitCounterIsNoGap() {
        SequenceTracker tracker = new SequenceTracker();
        tracker.observe(0, true);
        tracker.observe(0xFFFFFFFEL, false);
        assertEquals(0, tracker.observe(0xFFFFFFFFL, false));
        assertEquals(0, tracker.observe(0, false));
        assertEquals(1, tracker.observe(2, false), "and a drop right after the wrap counts");
    }

    @Test
    void aDropAcrossTheWrapIsCountedWithUnsignedArithmetic() {
        SequenceTracker tracker = new SequenceTracker();
        tracker.observe(0xFFFFFFFDL, false);
        assertEquals(0, tracker.observe(0xFFFFFFFEL, false));
        assertEquals(3, tracker.observe(2, false), "0xFFFFFFFF, 0 and 1 are missing");
    }

    @Test
    void aHelloStartsAgainWithoutCountingAnything() {
        SequenceTracker tracker = new SequenceTracker();
        tracker.observe(0, true);
        tracker.observe(50, false);
        assertEquals(0, tracker.observe(0, true), "a new connection");
        assertEquals(0, tracker.observe(1, false));
    }

    @Test
    void theFirstFrameOnlySetsTheExpectation() {
        SequenceTracker tracker = new SequenceTracker();
        assertEquals(0, tracker.observe(1000, false), "a dump cut from the middle");
        assertEquals(1, tracker.observe(1002, false));
    }

    @Test
    void aSequenceThatGoesBackwardsCountsNothing() {
        SequenceTracker tracker = new SequenceTracker();
        tracker.observe(0, true);
        tracker.observe(10, false);
        assertEquals(0, tracker.observe(3, false));
        assertEquals(0, tracker.observe(4, false));
    }

    @Test
    void theLimiterLetsABurstThroughAndRefillsAtItsRate() {
        RequestLimiter limiter = new RequestLimiter(2.0, 3);
        long t = 1_000_000_000L;
        assertTrue(limiter.tryAcquire(t));
        assertTrue(limiter.tryAcquire(t));
        assertTrue(limiter.tryAcquire(t));
        assertTrue(!limiter.tryAcquire(t), "the burst is used up");
        assertTrue(!limiter.tryAcquire(t + 400_000_000L), "0.8 tokens");
        assertTrue(limiter.tryAcquire(t + 500_000_000L), "one token after 0.5 s at 2 per second");
        assertTrue(!limiter.tryAcquire(t + 500_000_000L));
        // A long pause refills to the burst size, not beyond.
        long later = t + 60_000_000_000L;
        assertTrue(limiter.tryAcquire(later));
        assertTrue(limiter.tryAcquire(later));
        assertTrue(limiter.tryAcquire(later));
        assertTrue(!limiter.tryAcquire(later));
    }

    @Test
    void theLimiterRejectsNonsense() {
        assertThrows(IllegalArgumentException.class, () -> new RequestLimiter(0, 1));
        assertThrows(IllegalArgumentException.class, () -> new RequestLimiter(1, 0));
    }
}
