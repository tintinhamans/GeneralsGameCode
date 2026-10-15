package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.internal.ReplayPacer;
import com.generalsonline.stats.internal.SequenceTracker;
import com.generalsonline.stats.proto.MatchInfo;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.function.Consumer;

/**
 * Reads dump files written by {@link DumpWriter} and replays them into a {@link StatsListener} through the same
 * dispatcher a live {@link StatsPipe} uses, so a program written against a live game also runs on a recording. Dumps of
 * format version 2 carry the sequence number and timestamp of every frame, and gaps in the sequence are reported to the
 * listener as {@link StatsListener#onFramesDropped} like on a live pipe. Dumps of format version 1 (no header, 5 byte
 * envelope, written before protocol version 2) are still read; their frames get consecutive sequence numbers from 0 and
 * a timestamp of 0. A file of any other version is refused.
 */
public final class DumpReader {
    private static final int DEFAULT_LOGIC_FPS = 30;
    private static final int HEADER_BYTES = 8;

    private DumpReader() {
    }

    /**
     * Hands every raw frame of a dump to the consumer, in order.
     *
     * @param file the dump file
     * @param consumer receives each frame
     * @return the number of frames
     * @throws IOException when the file cannot be read, has a format version this library does not know, or ends inside
     *         a frame ({@link FrameException})
     */
    public static int readFrames(Path file, Consumer<RawFrame> consumer) throws IOException {
        int count = 0;
        try (InputStream in = Files.newInputStream(file)) {
            byte[] head = in.readNBytes(HEADER_BYTES);
            boolean hasHeader = startsWithMagic(head);
            if (hasHeader && head.length < HEADER_BYTES) {
                throw new FrameException("dump header is truncated (" + head.length + " bytes)");
            }
            if (hasHeader) {
                long version = (head[4] & 0xFFL) | ((head[5] & 0xFFL) << 8) | ((head[6] & 0xFFL) << 16) | ((head[7] & 0xFFL) << 24);
                if (version != DumpWriter.FORMAT_VERSION) {
                    throw new IOException("unsupported dump format version " + version + "; this library reads versions 1 and " + DumpWriter.FORMAT_VERSION);
                }
            }
            FrameCodec.Decoder decoder = hasHeader ? new FrameCodec.Decoder() : FrameCodec.Decoder.legacy();
            byte[] chunk = new byte[16384];
            int read = 0;
            if (!hasHeader) {
                // A version 1 dump has no header: the bytes read ahead are the start of its first frame.
                System.arraycopy(head, 0, chunk, 0, head.length);
                read = head.length;
            }
            do {
                for (RawFrame frame : decoder.push(chunk, 0, read)) {
                    consumer.accept(hasHeader ? frame : new RawFrame(frame.type(), count, 0, frame.body()));
                    ++count;
                }
            } while ((read = in.read(chunk)) >= 0);
            if (decoder.buffered() != 0) {
                throw new FrameException("dump ends inside a frame (" + decoder.buffered() + " bytes left)");
            }
        }
        return count;
    }

    private static boolean startsWithMagic(byte[] head) {
        int size = FrameCodec.DUMP_MAGIC.length;
        return head.length >= size && Arrays.equals(Arrays.copyOf(head, size), FrameCodec.DUMP_MAGIC);
    }

    /** Reports the frames missing before this one, if any, to the listener. */
    private static void reportGap(SequenceTracker tracker, RawFrame frame, StatsListener listener) {
        long dropped = tracker.observe(frame.sequence(), frame.type() == FrameType.HELLO.id());
        if (dropped > 0) {
            listener.onFramesDropped(dropped);
        }
    }

    /**
     * Feeds every frame of a dump to the listener as fast as possible. An exception thrown by the listener ends the
     * replay and propagates; wrap the listener's work yourself if that is not wanted.
     *
     * @param file the dump file
     * @param listener receives the frames
     * @return the number of frames
     * @throws IOException when the file cannot be read or ends inside a frame
     */
    public static int replay(Path file, StatsListener listener) throws IOException {
        SequenceTracker tracker = new SequenceTracker();
        return readFrames(file, frame -> {
            reportGap(tracker, frame, listener);
            FrameDispatcher.dispatch(frame, listener);
        });
    }

    /**
     * Feeds the frames to the listener paced by game time. The first game frame anchors the wall clock; a frame N
     * logic frames later is delivered N / logic rate / speed seconds after it, using the logic rate of the dump's
     * MatchInfo (30 until one is seen). The thread sleeps only when it is ahead of that schedule, so a slow listener
     * is never slowed further. The calling thread does the work and sleeps; if it is interrupted the replay stops
     * early and the interrupt flag stays set.
     *
     * @param file the dump file
     * @param listener receives the frames, on the calling thread
     * @param speed 1.0 is real time, 2.0 twice as fast, 0 or less as fast as possible
     * @return the number of frames delivered
     * @throws IOException when the file cannot be read or ends inside a frame
     */
    public static int replay(Path file, StatsListener listener, double speed) throws IOException {
        ReplayPacer pacer = new ReplayPacer(speed, DEFAULT_LOGIC_FPS);
        SequenceTracker tracker = new SequenceTracker();
        int[] delivered = {0};
        try {
            readFrames(file, raw -> {
                DecodedFrame decoded = FrameDispatcher.decode(raw);
                if (decoded.message() instanceof MatchInfo match) {
                    pacer.setLogicFps(match.getLogicFramesPerSecond());
                }
                if (!sleepNanos(pacer.delayNanos(ReplayPacer.gameFrameOf(decoded), System.nanoTime()))) {
                    throw new ReplayInterrupted();
                }
                reportGap(tracker, raw, listener);
                FrameDispatcher.deliver(decoded, listener);
                delivered[0]++;
            });
        } catch (ReplayInterrupted stopped) {
            // the thread was interrupted: stop early, the flag is already set again
        }
        return delivered[0];
    }

    /** Thrown inside {@link #readFrames} to leave the loop when the thread is interrupted. */
    private static final class ReplayInterrupted extends RuntimeException {
        private static final long serialVersionUID = 1L;

        ReplayInterrupted() {
            super(null, null, false, false);
        }
    }

    private static boolean sleepNanos(long nanos) {
        if (nanos <= 0) {
            return true;
        }
        try {
            Thread.sleep(nanos / 1_000_000, (int) (nanos % 1_000_000));
            return true;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }
}
