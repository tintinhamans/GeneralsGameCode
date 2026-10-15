package com.generalsonline.stats.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.DumpWriter;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.RawFrame;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

/** The console side: the watch printer, the dump status line and the dump recorder built on the library. */
class ConsoleTest {
    private static MatchInfo namedMatch() {
        return MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m").setLogicFramesPerSecond(60)
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("Alice").setIsHuman(true))
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(3).setName("Bob")).build();
    }

    private static StatsSnapshot snapshot(int frame, boolean full) {
        return StatsSnapshot.newBuilder().setFrame(frame).setFull(full).addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(5)).build();
    }

    private static void feed(FramePrinter printer, Message message) {
        FrameDispatcher.dispatch(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()), printer);
    }

    @Test
    void watchShowsNamesFromMatchInfoInsteadOfPlayerIndexes() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"));
        feed(printer, namedMatch());
        feed(printer, snapshot(120, true));
        feed(printer, Events.newBuilder().setFrame(120)
            .addEvents(StatEvent.newBuilder().setFrame(120).setPlayerIndex(3).setKind(StatEventKind.STATEVENT_VETERANCY_CHANGE).setPrimaryName("Crusader").setIntValue1(0).setIntValue2(1)).build());

        String text = captured.toString("UTF-8");
        assertTrue(text.contains("  Alice $5"), text);
        assertTrue(text.contains("(0:02)"), "game time at the build's 60 fps: " + text);
        assertTrue(text.contains("VETERANCY_CHANGE Bob Crusader Regular -> Veteran"), text);
        assertFalse(text.contains("P2"), text);
    }

    @Test
    void snapshotsArePrintedAtMostOncePerSecond() throws Exception {
        AtomicLong clock = new AtomicLong(10_000);
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"), EnumSet.allOf(FramePrinter.Kind.class), 1000, clock::get);

        for (int i = 0; i < 25; i++) { // ten snapshots a second for 2.5 seconds
            feed(printer, snapshot(i * 6, i % 3 == 0));
            clock.addAndGet(100);
        }
        long printed = captured.toString("UTF-8").lines().filter(l -> l.startsWith("SNAPSHOT")).count();
        assertEquals(3, printed, "at 0 s, 1 s and 2 s");

        ByteArrayOutputStream all = new ByteArrayOutputStream();
        FramePrinter unthrottled = new FramePrinter(new PrintStream(all, true, "UTF-8"));
        for (int i = 0; i < 25; i++) {
            feed(unthrottled, snapshot(i, true));
        }
        assertEquals(25, all.toString("UTF-8").lines().filter(l -> l.startsWith("SNAPSHOT")).count(), "replay prints everything");
    }

    @Test
    void onlyFiltersWhatIsPrintedButStillLearnsTheNames() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"), FramePrinter.parseOnly("events, chat"), 0, System::currentTimeMillis);
        feed(printer, namedMatch());
        feed(printer, snapshot(1, true));
        feed(printer, TestMessages.uiState());
        feed(printer, TestMessages.camera());
        feed(printer, TestMessages.chatMessage());
        feed(printer, Events.newBuilder().setFrame(1).addEvents(StatEvent.newBuilder().setFrame(1).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("Tank")).build());

        String text = captured.toString("UTF-8");
        assertTrue(text.contains("CHAT [ROOM]"), text);
        assertTrue(text.contains("EVENT frame=1 KILL Alice Tank"), "names still come from the filtered-out MatchInfo: " + text);
        for (String hidden : new String[] {"MATCH ", "SNAPSHOT", "UI ", "CAMERA"}) {
            assertFalse(text.contains(hidden), hidden + " in " + text);
        }
    }

    @Test
    void onlyRejectsUnknownKindsWithTheListOfValidOnes() {
        assertEquals(EnumSet.of(FramePrinter.Kind.CAMERA, FramePrinter.Kind.LOBBY), FramePrinter.parseOnly("Camera,lobby"));
        String message = assertThrows(IllegalArgumentException.class, () -> FramePrinter.parseOnly("events,nope")).getMessage();
        assertTrue(message.contains("'nope'") && message.contains("snapshot"), message);
        assertThrows(IllegalArgumentException.class, () -> FramePrinter.parseOnly(" , "));
    }

    @Test
    void errorsAreAlwaysPrintedWithTheRealCause() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"), EnumSet.of(FramePrinter.Kind.CHAT), 0, System::currentTimeMillis);
        printer.onError(0x02, new IllegalStateException("bug"));
        printer.onDisconnected(new IOException("read failed", new IOException("The pipe has been ended")));
        String text = captured.toString("UTF-8");
        assertTrue(text.contains("ERROR while handling frame type 0x2: bug"), text);
        assertTrue(text.contains("DISCONNECTED The pipe has been ended"), text);
    }

    @Test
    void printerHandlesEveryFrameWithoutThrowing() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"));
        for (Message message : TestMessages.all()) {
            feed(printer, message);
        }
        String text = captured.toString("UTF-8");
        assertTrue(text.contains("MATCH LOADING"));
        assertTrue(text.contains("SNAPSHOT full frame=900"));
        assertTrue(text.contains("EVENT frame=880 KILL P2 ChinaTankGattling cost=800"));
        assertTrue(text.contains("EVENT frame=880 RADAR_CHANGED P2"));
        assertTrue(text.contains("CAMERA at=101,200 zoom=1.5"));
        assertTrue(text.contains("STALLED 45f"));
        assertTrue(text.contains("CHAT [ROOM]"));
        assertTrue(text.contains("UI ONLINE_GAME_SETUP"));
        assertTrue(text.contains("LOBBY ONLINE"));
    }

    @Test
    void dumpStatusIsOneLinePerCallWithRatesAndNoPerFramePrinting() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        DumpStatus status = new DumpStatus(new PrintStream(captured, true, "UTF-8"));
        for (int i = 0; i < 120; i++) {
            status.onFrame(FrameDispatcher.decode(new RawFrame(FrameType.CAMERA.id(), TestMessages.camera().toByteArray())));
        }
        status.onFrame(FrameDispatcher.decode(new RawFrame(FrameType.STATS_SNAPSHOT.id(), TestMessages.statsSnapshot().toByteArray())));

        assertEquals("", captured.toString("UTF-8"), "frames print nothing by themselves");
        String line = status.statusLine();
        assertTrue(line.contains("frames 121 (+121/s, camera +120/s)"), line);
        assertTrue(line.contains("game time 0:30 (frame 900)"), line);
        assertTrue(status.statusLine().contains("(+0/s, camera +0/s)"), "rates are per call");
    }

    @Test
    void theDumpRecorderWritesTheFramesAndASkippedCameraStaysOut(@TempDir Path directory) throws Exception {
        Path file = directory.resolve("recorded.dump");
        List<DecodedFrame> seen = new ArrayList<>();
        try (DumpWriter writer = new DumpWriter(file)) {
            DumpStatus status = new DumpStatus(new PrintStream(new ByteArrayOutputStream()), frame -> {
                seen.add(frame);
                if (frame.type() == FrameType.CAMERA) {
                    return; // --no-camera
                }
                try {
                    writer.write(frame);
                } catch (IOException e) {
                    throw new java.io.UncheckedIOException(e);
                }
            });
            for (Message message : TestMessages.all()) {
                FrameDispatcher.deliver(FrameDispatcher.decode(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray())), status);
            }
        }
        assertEquals(TestMessages.all().length, seen.size());
        List<Integer> types = new ArrayList<>();
        assertEquals(TestMessages.all().length - 1, DumpReader.readFrames(file, frame -> types.add(frame.type())));
        assertFalse(types.contains(FrameType.CAMERA.id()));
    }
}
