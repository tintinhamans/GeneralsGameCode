package com.generalsonline.stats.internal;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * One open connection to a stats pipe: a blocking read and a blocking write, which may run at the same time on two
 * threads. The indirection exists so the connection loop can be tested without a Windows named pipe. Not part of the
 * public API.
 */
public interface PipeLink extends Closeable {
    /**
     * Reads at least one byte, blocking until some arrive.
     *
     * @param buffer where to put the bytes
     * @return the number of bytes read, or -1 at the end of the stream
     * @throws IOException when the pipe fails
     */
    int read(ByteBuffer buffer) throws IOException;

    /**
     * Writes every remaining byte of the buffer.
     *
     * @param buffer the bytes to write
     * @throws IOException when the pipe fails
     */
    void write(ByteBuffer buffer) throws IOException;
}
