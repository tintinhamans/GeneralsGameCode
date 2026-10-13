package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

class MessageRoundTripTest {
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
    void aDumpFileGeneratedHereReplaysThroughTheSameListener(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("fixture.dump");
        try (DumpFile.Writer writer = new DumpFile.Writer(dump)) {
            for (Message message : TestMessages.all()) {
                writer.write(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
            }
            writer.write(new RawFrame(0x7E, new byte[] {1, 2, 3}));
        }

        RecordingListener listener = new RecordingListener();
        int frames = DumpFile.replay(dump, listener);

        assertEquals(TestMessages.all().length + 1, frames);
        assertEquals(List.of(TestMessages.all()), listener.messages);
        assertEquals(List.of(0x7E), listener.unknownTypes);
    }

    @Test
    void aTruncatedDumpIsReported(@TempDir Path directory) throws Exception {
        Path dump = directory.resolve("truncated.dump");
        byte[] frame = FrameCodec.encode(FrameType.UI_STATE.id(), TestMessages.uiState().toByteArray());
        Files.write(dump, java.util.Arrays.copyOf(frame, frame.length - 1));
        org.junit.jupiter.api.Assertions.assertThrows(FrameException.class, () -> DumpFile.replay(dump, new RecordingListener()));
    }

    @Test
    void printerHandlesEveryFrameWithoutThrowing() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new java.io.PrintStream(captured, true, "UTF-8"));
        for (Message message : TestMessages.all()) {
            FrameDispatcher.dispatch(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()), printer);
        }
        String text = captured.toString("UTF-8");
        assertTrue(text.contains("MATCH LOADING"));
        assertTrue(text.contains("SNAPSHOT frame=900"));
        assertTrue(text.contains("STALLED 45f"));
        assertTrue(text.contains("CHAT [ROOM]"));
        assertTrue(text.contains("UI ONLINE_GAME_SETUP"));
        assertTrue(text.contains("LOBBY ONLINE"));
    }
}
