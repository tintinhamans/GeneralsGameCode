package com.generalsonline.stats.internal;

import com.generalsonline.stats.Format;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.ExecutionException;

/**
 * One open connection to a Windows named pipe: a blocking read and a blocking write, which may run at the same time on
 * two threads. The pipe is opened as an overlapped (asynchronous) channel because a
 * synchronous Windows handle would make a write wait for the blocked read. Not part of the public API.
 */
public final class NamedPipeLink implements Closeable {
    private final AsynchronousFileChannel channel;

    private NamedPipeLink(AsynchronousFileChannel channel) {
        this.channel = channel;
    }

    /**
     * Opens a pipe for reading and writing.
     *
     * @param path the pipe path, for example {@code \\.\pipe\generalsonline-stats-1234}
     * @return the open link
     * @throws IOException when the pipe cannot be opened, for example because another reader holds it
     */
    public static NamedPipeLink open(String path) throws IOException {
        return new NamedPipeLink(AsynchronousFileChannel.open(Paths.get(path), StandardOpenOption.READ, StandardOpenOption.WRITE));
    }

    /**
     * Reads at least one byte, blocking until some arrive.
     *
     * @param buffer where to put the bytes
     * @return the number of bytes read, or -1 at the end of the stream
     * @throws IOException when the pipe fails
     */
    public int read(ByteBuffer buffer) throws IOException {
        try {
            return channel.read(buffer, 0).get();
        } catch (ExecutionException e) {
            // Surface the real I/O failure (for example "the pipe has been ended"), not a generic wrapper.
            Throwable cause = e.getCause();
            throw cause instanceof IOException ? (IOException) cause : new IOException("read failed: " + Format.cause(e), cause);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted", e);
        }
    }

    /**
     * Writes every remaining byte of the buffer.
     *
     * @param buffer the bytes to write
     * @throws IOException when the pipe fails
     */
    public void write(ByteBuffer buffer) throws IOException {
        try {
            while (buffer.hasRemaining()) {
                channel.write(buffer, 0).get();
            }
        } catch (ExecutionException e) {
            Throwable cause = e.getCause();
            throw cause instanceof IOException ? (IOException) cause : new IOException("write failed: " + Format.cause(e), cause);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted", e);
        }
    }

    @Override
    public void close() throws IOException {
        channel.close();
    }
}
