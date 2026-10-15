package com.generalsonline.stats.internal;

import com.generalsonline.stats.Format;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.ExecutionException;

/**
 * A {@link PipeLink} over a Windows named pipe. The pipe is opened as an overlapped (asynchronous) channel because a
 * synchronous Windows handle would make a write wait for the blocked read. Not part of the public API.
 */
public final class NamedPipeLink implements PipeLink {
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

    @Override
    public int read(ByteBuffer buffer) throws IOException {
        try {
            return channel.read(buffer, 0).get();
        } catch (ExecutionException e) {
            // Surface the real I/O failure (for example "the pipe has been ended"), not a generic wrapper.
            throw e.getCause() instanceof IOException io ? io : new IOException("read failed: " + Format.cause(e), e.getCause());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted", e);
        }
    }

    @Override
    public void write(ByteBuffer buffer) throws IOException {
        try {
            while (buffer.hasRemaining()) {
                channel.write(buffer, 0).get();
            }
        } catch (ExecutionException e) {
            throw e.getCause() instanceof IOException io ? io : new IOException("write failed: " + Format.cause(e), e.getCause());
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
