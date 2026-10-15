package com.generalsonline.stats.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.FrameDispatcher;
import com.generalsonline.stats.RawFrame;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;

/** The Hello and the dropped-frame count in the GUI status line, the watch printer and the dump status line. */
class HelloStatusTest {
    private static DecodedFrame decode(com.google.protobuf.Message message) {
        return FrameDispatcher.decode(new RawFrame(TestMessages.frameTypeOf(message), message.toByteArray()));
    }

    @Test
    void theGuiStatusShowsTheHelloAndTheDroppedFrames() {
        GuiModel model = new GuiModel();
        assertEquals("Game: no Hello received    Dropped frames: 0", model.gameLine());

        EdtFeed feed = new EdtFeed(model, new FeedHealth(), Runnable::run);
        feed.onFrame(decode(TestMessages.hello()));
        assertEquals("Game: version 1.04    Protocol: 2    Reader: 2 of 4    Logic: 30 fps    Dropped frames: 0", model.gameLine());

        feed.onFramesDropped(3);
        feed.onFramesDropped(2);
        assertEquals(5, model.droppedFrames());
        assertTrue(model.gameLine().endsWith("Dropped frames: 5"), model.gameLine());
    }

    @Test
    void theFramesTabDescribesHelloAndResponse() {
        assertEquals("game 1.04, protocol 2, pid 4242, reader 1 of 4, 30 logic fps", FrameSummary.of(decode(TestMessages.hello())));
        assertEquals("id 7 NOT_FOUND: no such object", FrameSummary.of(decode(TestMessages.response())));
    }

    @Test
    void theWatchPrinterShowsTheHelloAndEveryDrop() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        FramePrinter printer = new FramePrinter(new PrintStream(captured, true, "UTF-8"));
        FrameDispatcher.deliver(decode(TestMessages.hello()), printer);
        printer.onFramesDropped(4);
        printer.onFramesDropped(1);
        String text = captured.toString("UTF-8");
        assertTrue(text.contains("HELLO game 1.04, protocol 2, pid 4242, reader 1 of 4, 30 logic fps, build abc123"), text);
        assertTrue(text.contains("DROPPED 4 frames (4 in total)"), text);
        assertTrue(text.contains("DROPPED 1 frames (5 in total)"), text);
        assertEquals(5, printer.droppedFrames());
    }

    @Test
    void theDumpStatusLineCountsDroppedFrames() throws Exception {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        DumpStatus status = new DumpStatus(new PrintStream(captured, true, "UTF-8"));
        FrameDispatcher.deliver(decode(TestMessages.hello()), status);
        status.onFramesDropped(7);
        assertTrue(captured.toString("UTF-8").startsWith("HELLO game 1.04"), captured.toString("UTF-8"));
        assertTrue(status.statusLine().endsWith("dropped 7"), "the line ends with the drop count");
    }
}
