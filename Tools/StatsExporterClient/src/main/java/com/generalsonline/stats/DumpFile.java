package com.generalsonline.stats;

import java.io.BufferedOutputStream;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;

/** A dump file is the raw pipe stream: the same envelope, frame after frame, so it can be replayed. */
public final class DumpFile {
    private DumpFile() {
    }

    /** Writes frames to a file as they arrive. */
    public static final class Writer implements Closeable {
        private final OutputStream out;

        public Writer(Path file) throws IOException {
            this.out = new BufferedOutputStream(Files.newOutputStream(file));
        }

        public synchronized void write(RawFrame frame) throws IOException {
            out.write(FrameCodec.encode(frame.type(), frame.body()));
        }

        public synchronized void flush() throws IOException {
            out.flush();
        }

        @Override
        public synchronized void close() throws IOException {
            out.close();
        }
    }

    /** Feeds every frame of a dump through the same dispatcher a live client uses; returns the frame count. */
    public static int replay(Path file, FrameListener listener) throws IOException {
        int count = 0;
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        byte[] chunk = new byte[16384];
        try (InputStream in = Files.newInputStream(file)) {
            int read;
            while ((read = in.read(chunk)) >= 0) {
                for (RawFrame frame : decoder.push(chunk, 0, read)) {
                    FrameDispatcher.dispatch(frame, listener);
                    ++count;
                }
            }
        }
        if (decoder.buffered() != 0) {
            throw new FrameException("dump ends inside a frame (" + decoder.buffered() + " bytes left)");
        }
        return count;
    }
}
