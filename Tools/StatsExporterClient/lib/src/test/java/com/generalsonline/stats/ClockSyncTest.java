package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.util.OptionalLong;

class ClockSyncTest {
    @Test
    void nothingIsKnownBeforeTheFirstFrame() {
        ClockSync clock = new ClockSync();
        assertTrue(clock.offsetMicros().isEmpty());
        assertTrue(clock.excessDelayMicros(5, 10).isEmpty());
        assertTrue(clock.toLocalMicros(5).isEmpty());
        assertEquals(0, clock.sampleCount());
    }

    @Test
    void theOffsetIsTheMinimumOfReceivedMinusTimestamp() {
        ClockSync clock = new ClockSync();
        clock.observe(1000, 501_000);   // 500_000 + 0 delay
        clock.observe(2000, 502_900);   // 500_900: 900 us late
        clock.observe(3000, 503_300);   // 500_300
        assertEquals(OptionalLong.of(500_000), clock.offsetMicros());
        assertEquals(OptionalLong.of(900), clock.excessDelayMicros(2000, 502_900));
        assertEquals(OptionalLong.of(0), clock.excessDelayMicros(4000, 504_000));
        assertEquals(OptionalLong.of(0), clock.excessDelayMicros(4000, 503_000), "never negative");
        assertEquals(OptionalLong.of(510_000), clock.toLocalMicros(10_000));
    }

    @Test
    void onlyTheLatestWindowCounts() {
        ClockSync clock = new ClockSync(3);
        clock.observe(0, 100);
        clock.observe(0, 500);
        clock.observe(0, 600);
        assertEquals(OptionalLong.of(100), clock.offsetMicros());
        clock.observe(0, 700);
        assertEquals(OptionalLong.of(500), clock.offsetMicros(), "the 100 left the window");
        assertEquals(3, clock.sampleCount());
    }

    @Test
    void resetForgetsEverything() {
        ClockSync clock = new ClockSync();
        clock.observe(0, 5);
        clock.reset();
        assertEquals(0, clock.sampleCount());
        assertTrue(clock.offsetMicros().isEmpty());
        clock.observe(0, 9);
        assertEquals(OptionalLong.of(9), clock.offsetMicros());
    }

    @Test
    void theWindowMustHoldAtLeastOneSample() {
        assertThrows(IllegalArgumentException.class, () -> new ClockSync(0));
    }

    @Test
    void aRawFrameChecksItsSequenceRange() {
        new RawFrame(1, 0xFFFFFFFFL, -1L, new byte[] {1});
        assertThrows(IllegalArgumentException.class, () -> new RawFrame(1, -1, 0, new byte[] {1}));
        assertThrows(IllegalArgumentException.class, () -> new RawFrame(1, 0x100000000L, 0, new byte[] {1}));
        RawFrame plain = new RawFrame(5, new byte[] {1});
        assertEquals(0, plain.sequence());
        assertEquals(0, plain.timestampMicros());
        DecodedFrame decoded = FrameDispatcher.decode(new RawFrame(0x7F, 77, 8800, new byte[] {1}));
        assertEquals(77, decoded.sequence());
        assertEquals(8800, decoded.timestampMicros());
        assertEquals(18, decoded.wireBytes());
    }
}
