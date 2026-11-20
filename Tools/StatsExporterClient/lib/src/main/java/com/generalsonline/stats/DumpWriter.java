package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;

import java.io.BufferedOutputStream;
import java.io.Closeable;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Writes frames to a dump file as they arrive. A dump file is a small header (the magic {@code GOSD} and the format
 * version as a little-endian u32) followed by the raw pipe stream: the same 17 byte envelope with its sequence number
 * and timestamp, frame after frame, so {@link DumpReader} can replay it later without a game. The Hello of a connection
 * is an ordinary frame in the file. Usually used from {@link StatsListener#onFrame}. All methods are thread safe.
 *
 * <p>Format version 1 (before protocol version 2) had no header and a 5 byte envelope; {@link DumpReader} still reads it.
 */
public final class DumpWriter implements Closeable {
    /** The dump format version this class writes. */
    public static final int FORMAT_VERSION = 2;

    private final OutputStream out;

    /**
     * Creates or truncates the file.
     *
     * @param file the dump file
     * @throws IOException when the file cannot be created
     */
    public DumpWriter(Path file) throws IOException {
        this(file, true);
    }

    /**
     * Creates the file and writes the header.
     *
     * @param file the dump file
     * @param overwrite false makes an existing file an error (FileAlreadyExistsException) instead of truncating it
     * @throws IOException when the file cannot be created
     */
    public DumpWriter(Path file, boolean overwrite) throws IOException {
        this.out = new BufferedOutputStream(overwrite
            ? Files.newOutputStream(file)
            : Files.newOutputStream(file, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE));
        try {
            out.write(FrameCodec.DUMP_MAGIC);
            out.write(new byte[] {(byte) FORMAT_VERSION, 0, 0, 0});
        } catch (IOException e) {
            out.close();
            throw e;
        }
    }

    /**
     * Appends one frame.
     *
     * @param frame the frame, exactly as received; its sequence number and timestamp are written too
     * @throws IOException when the file cannot be written
     */
    public synchronized void write(RawFrame frame) throws IOException {
        out.write(FrameCodec.encodeOutbound(frame.type(), frame.sequence(), frame.timestampMicros(), frame.body()));
    }

    /**
     * Appends one frame.
     *
     * @param frame the frame, exactly as received; only its raw bytes (and envelope data) are written
     * @throws IOException when the file cannot be written
     */
    public void write(DecodedFrame frame) throws IOException {
        write(frame.raw());
    }

    /**
     * Pushes buffered frames to the file. A recorder that sees up to 120 camera frames a second should flush on a
     * timer, once a second, not per frame.
     *
     * @throws IOException when the file cannot be written
     */
    public synchronized void flush() throws IOException {
        out.flush();
    }

    @Override
    public synchronized void close() throws IOException {
        out.close();
    }
}
