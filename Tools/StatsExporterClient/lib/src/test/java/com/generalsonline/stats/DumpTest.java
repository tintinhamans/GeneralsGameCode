package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/** DumpWriter, DumpReader and the framing round trip of every message. */
class DumpTest {
    @Test
    void everyMessageSurvivesEncodeFramingAndDecode() throws Exception {
        for (Message original : TestMessages.all()) {
            byte[] frame = FrameCodec.encode(TestMessages.frameTypeOf(original), original.toByteArray());

            RecordingListener listener = new RecordingListener();
            // Split in the middle so the decoder has to reassemble.
            FrameCodec.Decoder decoder = new FrameCodec.Decoder();
            int split = frame.length / 2;
            assertTrue(decoder.push(frame, 0, split).isEmpty());
            for (RawFrame raw : decoder.push(frame, split, frame.length - split)) {
                FrameDispatcher.dispatch(raw, listener);
            }

            assertEquals(1, listener.messages.size(), original.getClass().getSimpleName());
            assertEquals(original, listener.messages.get(0), original.getClass().getSimpleName());
        }
    }

    @Test
    void aSequenceOfAllFramesArrivesInOrder() throws Exception {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        for (Message message : TestMessages.all()) {
            stream.write(FrameCodec.encode(TestMessages.frameTypeOf(message), message.toByteArray()));
        }

        RecordingListener listener = new RecordingListener();
        for (RawFrame raw : new FrameCodec.Decoder().push(stream.toByteArray())) {
            FrameDispatcher.dispatch(raw, listener);
        }
        assertEquals(List.of(TestMessages.all()), listener.messages);
    }

    @Test
    void aDumpFileWrittenHereReplaysThroughTheSameListener(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("fixture.dump");
        try (DumpWriter writer = new DumpWriter(dump)) {
            for (Message message : TestMessages.all()) {
                writer.write(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
            }
            writer.write(new RawFrame(0x7E, new byte[] {1, 2, 3}));
        }

        RecordingListener listener = new RecordingListener();
        int frames = DumpReader.replay(dump, listener);

        assertEquals(TestMessages.all().length + 1, frames);
        assertEquals(List.of(TestMessages.all()), listener.messages);
        assertEquals(List.of(0x7E), listener.unknownTypes);
    }

    @Test
    void aDecodedFrameCanBeWrittenToo(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("decoded.dump");
        DecodedFrame frame = FrameDispatcher.decode(new RawFrame(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray()));
        try (DumpWriter writer = new DumpWriter(dump)) {
            writer.write(frame);
        }
        RecordingListener listener = new RecordingListener();
        assertEquals(1, DumpReader.replay(dump, listener));
        assertEquals(List.of(TestMessages.uiState()), listener.messages);
    }

    @Test
    void aTruncatedDumpIsReported(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("truncated.dump");
        byte[] frame = FrameCodec.encode(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray());
        Files.write(dump, java.util.Arrays.copyOf(frame, frame.length - 1));
        assertThrows(FrameException.class, () -> DumpReader.replay(dump, new RecordingListener()));
    }

    @Test
    void theWriterRefusesToOverwriteUnlessAskedTo(@TempDir Path directory) throws Exception {
        Path file = directory.resolve("match.dump");
        try (DumpWriter writer = new DumpWriter(file, false)) {
            writer.write(new RawFrame(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray()));
        }
        long size = Files.size(file);

        assertThrows(FileAlreadyExistsException.class, () -> new DumpWriter(file, false));
        assertEquals(size, Files.size(file), "the existing file is untouched");

        try (DumpWriter writer = new DumpWriter(file, true)) {
            writer.flush();
        }
        assertEquals(0, Files.size(file), "overwrite truncates");
    }

    // -- Paced replay.

    private static Path pacedDump(Path directory, int logicFps, int... frames) throws Exception {
        Path dump = directory.resolve("paced.dump");
        try (DumpWriter writer = new DumpWriter(dump)) {
            MatchInfo info = MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m").setLogicFramesPerSecond(logicFps).build();
            writer.write(new RawFrame(FrameType.MATCH_INFO.id(), info.toByteArray()));
            for (int frame : frames) {
                writer.write(new RawFrame(FrameType.STATS_SNAPSHOT.id(), StatsSnapshot.newBuilder().setFrame(frame).build().toByteArray()));
            }
        }
        return dump;
    }

    @Test
    void aPacedReplayWaitsForGameTimeDividedBySpeed(@TempDir Path directory) throws Exception {
        // 60 frames at the dump's 30 fps are 2 s of game time; at 4x that is 0.5 s.
        Path dump = pacedDump(directory, 30, 1, 61);
        RecordingListener listener = new RecordingListener();
        long start = System.nanoTime();
        int count = DumpReader.replay(dump, listener, 4.0);
        long millis = (System.nanoTime() - start) / 1_000_000;

        assertEquals(3, count);
        assertEquals(3, listener.messages.size());
        assertTrue(millis >= 450, "waited for the game time: " + millis + " ms");
        assertTrue(millis < 5000, "but not much longer: " + millis + " ms");
    }

    @Test
    void speedZeroReplaysAsFastAsPossible(@TempDir Path directory) throws Exception {
        Path dump = pacedDump(directory, 30, 1, 30 * 3600);
        long start = System.nanoTime();
        assertEquals(3, DumpReader.replay(dump, new RecordingListener(), 0));
        assertTrue((System.nanoTime() - start) / 1_000_000 < 3000);
    }

    @Test
    void anInterruptedPacedReplayStopsEarly(@TempDir Path directory) throws Exception {
        Path dump = pacedDump(directory, 30, 1, 30 * 3600);
        RecordingListener listener = new RecordingListener();
        Thread replaying = Thread.currentThread();
        Thread interrupter = new Thread(() -> {
            try {
                Thread.sleep(300);
            } catch (InterruptedException ignored) {
                return;
            }
            replaying.interrupt();
        });
        interrupter.start();
        try {
            int count = DumpReader.replay(dump, listener, 1.0);
            assertEquals(2, count, "stopped at the hour-long gap");
            assertTrue(Thread.currentThread().isInterrupted(), "the interrupt flag is kept");
        } finally {
            Thread.interrupted();
            interrupter.join();
        }
    }

    @Test
    void readFramesGivesRawFramesInOrder(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("raw.dump");
        try (DumpWriter writer = new DumpWriter(dump)) {
            writer.write(new RawFrame(FrameType.EVENTS.id(), Events.newBuilder().setFrame(1)
                .addEvents(StatEvent.newBuilder().setKind(StatEventKind.STATEVENT_KILL)).build().toByteArray()));
            writer.write(new RawFrame(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray()));
        }
        List<Integer> types = new java.util.ArrayList<>();
        assertEquals(2, DumpReader.readFrames(dump, f -> types.add(f.type())));
        assertEquals(List.of(FrameType.EVENTS.id(), FrameType.UI_STATE.id()), types);
    }
}
