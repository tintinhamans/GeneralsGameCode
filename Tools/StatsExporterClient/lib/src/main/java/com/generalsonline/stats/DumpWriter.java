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
 * Writes frames to a dump file as they arrive. A dump file is the raw pipe stream: the same envelope, frame after
 * frame, so {@link DumpReader} can replay it later without a game. Usually used from {@link StatsListener#onFrame}.
 * All methods are thread safe.
 */
public final class DumpWriter implements Closeable {
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
     * Creates the file.
     *
     * @param file the dump file
     * @param overwrite false makes an existing file an error (FileAlreadyExistsException) instead of truncating it
     * @throws IOException when the file cannot be created
     */
    public DumpWriter(Path file, boolean overwrite) throws IOException {
        this.out = new BufferedOutputStream(overwrite
            ? Files.newOutputStream(file)
            : Files.newOutputStream(file, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE));
    }

    /**
     * Appends one frame.
     *
     * @param frame the frame, exactly as received
     * @throws IOException when the file cannot be written
     */
    public synchronized void write(RawFrame frame) throws IOException {
        out.write(FrameCodec.encode(frame.type(), frame.body()));
    }

    /**
     * Appends one frame.
     *
     * @param frame the frame, exactly as received; only its raw bytes are written
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
