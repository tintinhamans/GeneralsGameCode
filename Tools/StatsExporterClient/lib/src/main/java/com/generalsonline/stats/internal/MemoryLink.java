package com.generalsonline.stats.internal;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Two connected {@link PipeLink}s that pass bytes through memory: what one end writes the other reads. Closing either
 * end ends both. The in-process transport of the fake game. Not part of the public API.
 */
public final class MemoryLink {
    private static final byte[] EOF = new byte[0];

    private MemoryLink() {
    }

    /**
     * Creates a connected pair.
     *
     * @return two ends; what is written to one is read from the other
     */
    public static PipeLink[] pair() {
        End first = new End();
        End second = new End();
        first.peer = second;
        second.peer = first;
        return new PipeLink[] {first, second};
    }

    private static final class End implements PipeLink {
        private final BlockingQueue<byte[]> incoming = new LinkedBlockingQueue<>();
        private End peer;
        private byte[] rest = new byte[0];
        private volatile boolean closed;

        @Override
        public int read(ByteBuffer buffer) throws IOException {
            if (rest.length == 0) {
                byte[] next;
                try {
                    next = incoming.take();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("interrupted", e);
                }
                if (next == EOF) {
                    incoming.add(EOF);
                    return -1;
                }
                rest = next;
            }
            int count = Math.min(buffer.remaining(), rest.length);
            buffer.put(rest, 0, count);
            rest = count == rest.length ? new byte[0] : java.util.Arrays.copyOfRange(rest, count, rest.length);
            return count;
        }

        @Override
        public void write(ByteBuffer buffer) throws IOException {
            if (closed || peer.closed) {
                throw new IOException("the pipe is closed");
            }
            byte[] copy = new byte[buffer.remaining()];
            buffer.get(copy);
            if (copy.length > 0) {
                peer.incoming.add(copy);
            }
        }

        @Override
        public void close() {
            closed = true;
            incoming.add(EOF);
            peer.closed = true;
            peer.incoming.add(EOF);
        }
    }
}
