package com.generalsonline.stats.internal;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.RawFrame;
import com.generalsonline.stats.TestMessages;
import org.junit.jupiter.api.Test;

class ReplayPacerTest {
    @Test
    void replayIsPacedByGameTimeAndASpeedMultiplier() {
        ReplayPacer realTime = new ReplayPacer(1.0, 60);
        assertEquals(0, realTime.delayNanos(1000, 0), "the first frame anchors the clock");
        assertEquals(1_000_000_000L, realTime.delayNanos(1060, 0), "60 logic frames at 60 fps are one second");
        assertEquals(500_000_000L, realTime.delayNanos(1060, 500_000_000L), "only the remaining time");
        assertEquals(0, realTime.delayNanos(1060, 2_000_000_000L), "never sleeps when behind");

        ReplayPacer twice = new ReplayPacer(2.0, 30);
        twice.delayNanos(0, 0);
        assertEquals(1_000_000_000L, twice.delayNanos(60, 0), "60 frames at 30 fps are 2 s of game time, 1 s of wall time at twice the speed");
    }

    @Test
    void theLogicRateComesFromMatchInfoAndFastestModeNeverSleeps() {
        ReplayPacer pacer = new ReplayPacer(1.0, 30);
        pacer.delayNanos(0, 0);
        assertEquals(2_000_000_000L, pacer.delayNanos(60, 0));
        pacer.setLogicFps(60);
        assertEquals(1_000_000_000L, pacer.delayNanos(60, 0));
        pacer.setLogicFps(0);
        assertEquals(1_000_000_000L, pacer.delayNanos(60, 0), "0 means unknown: the rate is kept");

        ReplayPacer fastest = new ReplayPacer(0, 30);
        fastest.delayNanos(0, 0);
        assertEquals(0, fastest.delayNanos(100000, 0));
    }

    @Test
    void framesWithoutGameTimeAreNotDelayedAndABackwardJumpReanchors() {
        ReplayPacer pacer = new ReplayPacer(1.0, 30);
        assertEquals(0, pacer.delayNanos(-1, 0));
        pacer.delayNanos(0, 0);
        assertEquals(10_000_000_000L, pacer.delayNanos(300, 0));
        // A second match in the same dump starts again near frame 0.
        assertEquals(0, pacer.delayNanos(5, 20_000_000_000L));
        assertEquals(1_000_000_000L, pacer.delayNanos(35, 20_000_000_000L));
    }

    @Test
    void gameFrameOfSnapshotsAndEventsOnly() {
        assertEquals(900, ReplayPacer.gameFrameOf(FrameDispatcher.decode(new RawFrame(FrameType.STATS_SNAPSHOT.id(), TestMessages.statsSnapshot().toByteArray()))));
        assertEquals(880, ReplayPacer.gameFrameOf(FrameDispatcher.decode(new RawFrame(FrameType.EVENTS.id(), TestMessages.events().toByteArray()))));
        assertEquals(-1, ReplayPacer.gameFrameOf(FrameDispatcher.decode(new RawFrame(FrameType.CAMERA.id(), TestMessages.camera().toByteArray()))));
        assertEquals(-1, ReplayPacer.gameFrameOf(FrameDispatcher.decode(new RawFrame(0x7f, new byte[] {1}))));
    }
}
