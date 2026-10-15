package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
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
            byte[] frame = TestMessages.outbound(original, 3);

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
        RecordingListener listener = new RecordingListener();
        for (RawFrame raw : new FrameCodec.Decoder().push(TestMessages.outboundStream(TestMessages.all()))) {
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
        byte[] frame = TestMessages.outbound(TestMessages.uiState(), 0);
        Files.write(dump, concat(header(2), java.util.Arrays.copyOf(frame, frame.length - 1)));
        assertThrows(FrameException.class, () -> DumpReader.replay(dump, new RecordingListener()));

        // Cut inside the 17 byte envelope of the second frame.
        Files.write(dump, concat(header(2), frame, java.util.Arrays.copyOf(frame, 10)));
        String message = assertThrows(FrameException.class, () -> DumpReader.replay(dump, new RecordingListener())).getMessage();
        assertTrue(message.contains("ends inside a frame"), message);
    }

    private static byte[] header(int version) {
        return new byte[] {0x47, 0x4F, 0x53, 0x44, (byte) version, 0, 0, 0};
    }

    private static byte[] concat(byte[]... parts) throws java.io.IOException {
        ByteArrayOutputStream joined = new ByteArrayOutputStream();
        for (byte[] part : parts) {
            joined.write(part);
        }
        return joined.toByteArray();
    }

    @Test
    void theNewEnvelopeRoundTripsThroughAFile(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("envelope.dump");
        long[] sequences = {0, 1, 2, 0xFFFFFFFEL, 0xFFFFFFFFL};
        long[] timestamps = {0, 1500, 33_000, Long.MAX_VALUE, -1L};
        try (DumpWriter writer = new DumpWriter(dump)) {
            writer.write(new RawFrame(FrameType.HELLO.id(), 0, 0, TestMessages.hello().toByteArray()));
            for (int i = 1; i < sequences.length; i++) {
                writer.write(new RawFrame(FrameType.UI_STATE.id(), sequences[i], timestamps[i], TestMessages.uiState().toByteArray()));
            }
        }
        byte[] bytes = Files.readAllBytes(dump);
        assertArrayEquals(header(DumpWriter.FORMAT_VERSION), java.util.Arrays.copyOf(bytes, 8), "the magic and the format version come first");

        List<RawFrame> frames = new java.util.ArrayList<>();
        assertEquals(5, DumpReader.readFrames(dump, frames::add));
        assertEquals(FrameType.HELLO.id(), frames.get(0).type());
        for (int i = 1; i < sequences.length; i++) {
            assertEquals(sequences[i], frames.get(i).sequence());
            assertEquals(timestamps[i], frames.get(i).timestampMicros());
        }
    }

    @Test
    void theHelloIsAnOrdinaryFrameOfTheDump(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("hello.dump");
        try (DumpWriter writer = new DumpWriter(dump)) {
            for (Message message : TestMessages.all()) {
                writer.write(new RawFrame(TestMessages.frameTypeOf(message), 0, 0, message.toByteArray()));
            }
        }
        MatchState state = new MatchState();
        assertEquals(TestMessages.all().length, DumpReader.replay(dump, state));
        assertEquals(TestMessages.hello(), state.hello().orElseThrow());
        assertTrue(state.hasCapability("requests"));
        assertEquals(0, state.droppedFrames(), "frames that all say sequence 0 are no gap");
    }

    @Test
    void aGapInTheSequenceOfADumpIsReportedAsDroppedFrames(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("gap.dump");
        try (DumpWriter writer = new DumpWriter(dump)) {
            writer.write(new RawFrame(FrameType.HELLO.id(), 0, 0, TestMessages.hello().toByteArray()));
            writer.write(new RawFrame(FrameType.UI_STATE.id(), 1, 10, TestMessages.uiState().toByteArray()));
            writer.write(new RawFrame(FrameType.UI_STATE.id(), 4, 20, TestMessages.uiState().toByteArray()));
            writer.write(new RawFrame(FrameType.UI_STATE.id(), 5, 30, TestMessages.uiState().toByteArray()));
        }
        RecordingListener listener = new RecordingListener();
        assertEquals(4, DumpReader.replay(dump, listener));
        assertEquals(List.of(2L), listener.dropped);
        assertEquals(List.of(2L), replayPaced(dump).dropped, "the paced replay reports the same gaps");
    }

    private static RecordingListener replayPaced(Path dump) throws Exception {
        RecordingListener listener = new RecordingListener();
        DumpReader.replay(dump, listener, 0);
        return listener;
    }

    @Test
    void aVersionOneDumpIsStillReadWithSynthesizedSequenceNumbers(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("old.dump");
        Files.write(dump, concat(
            FrameCodec.encodeInbound(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray()),
            FrameCodec.encodeInbound(FrameType.MATCH_INFO.id(), TestMessages.matchInfo().toByteArray())));
        List<RawFrame> frames = new java.util.ArrayList<>();
        assertEquals(2, DumpReader.readFrames(dump, frames::add));
        assertEquals(List.of(0L, 1L), List.of(frames.get(0).sequence(), frames.get(1).sequence()));
        assertEquals(0, frames.get(1).timestampMicros());

        RecordingListener listener = new RecordingListener();
        assertEquals(2, DumpReader.replay(dump, listener));
        assertEquals(List.of(TestMessages.uiState(), TestMessages.matchInfo()), listener.messages);
        assertTrue(listener.dropped.isEmpty());
    }

    @Test
    void anEmptyFileHasNoFrames(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("empty.dump");
        Files.write(dump, new byte[0]);
        assertEquals(0, DumpReader.replay(dump, new RecordingListener()));
    }

    @Test
    void aDumpOfAnUnknownVersionOrWithATruncatedHeaderIsRefusedClearly(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("future.dump");
        Files.write(dump, concat(header(3), TestMessages.outbound(TestMessages.uiState(), 0)));
        String message = assertThrows(java.io.IOException.class, () -> DumpReader.replay(dump, new RecordingListener())).getMessage();
        assertTrue(message.contains("unsupported dump format version 3"), message);

        Files.write(dump, new byte[] {0x47, 0x4F, 0x53, 0x44, 2});
        String truncated = assertThrows(FrameException.class, () -> DumpReader.replay(dump, new RecordingListener())).getMessage();
        assertTrue(truncated.contains("header is truncated"), truncated);
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
        assertEquals(8, Files.size(file), "overwrite truncates: only the 8 byte header is left");
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
