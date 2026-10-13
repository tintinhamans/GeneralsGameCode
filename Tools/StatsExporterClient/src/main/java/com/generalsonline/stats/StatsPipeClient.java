package com.generalsonline.stats;

import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.ChatSend;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.ExecutionException;
import java.util.function.Consumer;
import java.util.function.Supplier;

/**
 * Reads the stats pipe and dispatches decoded frames to a {@link FrameListener}.
 *
 * <p>Threading: {@link #start()} creates one daemon reader thread that connects, reads, decodes and calls the
 * listener, and reconnects with backoff after a disconnect. The listener is only ever called on that thread.
 * {@link #sendChat} may be called from any thread; writes are serialized. The pipe is opened as an overlapped
 * (asynchronous) channel because a synchronous Windows handle would make a write wait for the blocked read.
 * The game accepts a single reader at a time.
 */
public final class StatsPipeClient implements AutoCloseable {
    private static final long MIN_BACKOFF_MS = 250;
    private static final long MAX_BACKOFF_MS = 5000;

    private final Supplier<String> pipePath;
    private final FrameListener listener;
    private final Object writeLock = new Object();
    private volatile boolean closed;
    private volatile AsynchronousFileChannel channel;
    private volatile Consumer<RawFrame> rawTap;
    private Thread thread;

    /** @param pipePath called before each connection attempt; return null while no pipe is known */
    public StatsPipeClient(Supplier<String> pipePath, FrameListener listener) {
        this.pipePath = pipePath;
        this.listener = listener;
    }

    public synchronized void start() {
        if (thread != null) {
            throw new IllegalStateException("already started");
        }
        thread = new Thread(this::run, "stats-pipe-reader");
        thread.setDaemon(true);
        thread.start();
    }

    /** Called on the reader thread with every frame, before it is decoded; used by the dump command. */
    public void setRawTap(Consumer<RawFrame> tap) {
        this.rawTap = tap;
    }

    public boolean isConnected() {
        return channel != null;
    }

    private void run() {
        long backoff = MIN_BACKOFF_MS;
        while (!closed) {
            String path = pipePath.get();
            AsynchronousFileChannel opened = null;
            if (path != null) {
                try {
                    opened = AsynchronousFileChannel.open(Paths.get(path), StandardOpenOption.READ, StandardOpenOption.WRITE);
                } catch (IOException | RuntimeException e) {
                    opened = null;
                }
            }

            if (opened == null) {
                sleep(backoff);
                backoff = Math.min(backoff * 2, MAX_BACKOFF_MS);
                continue;
            }

            backoff = MIN_BACKOFF_MS;
            channel = opened;
            Throwable cause = null;
            try {
                listener.onConnected(path);
                readLoop(opened);
            } catch (Throwable t) {
                cause = t;
            } finally {
                channel = null;
                try {
                    opened.close();
                } catch (IOException ignored) {
                    // closing a broken pipe
                }
            }
            if (!closed) {
                listener.onDisconnected(cause);
                sleep(backoff);
            }
        }
    }

    private void readLoop(AsynchronousFileChannel in) throws IOException {
        FrameCodec.Decoder decoder = new FrameCodec.Decoder();
        ByteBuffer buffer = ByteBuffer.allocate(65536);
        while (!closed) {
            buffer.clear();
            int read;
            try {
                read = in.read(buffer, 0).get();
            } catch (ExecutionException e) {
                throw new IOException("read failed", e.getCause());
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("interrupted", e);
            }
            if (read < 0) {
                return;
            }
            for (RawFrame frame : decoder.push(buffer.array(), 0, read)) {
                Consumer<RawFrame> tap = rawTap;
                if (tap != null) {
                    tap.accept(frame);
                }
                FrameDispatcher.dispatch(frame, listener);
            }
        }
    }

    /**
     * Sends one ChatSend frame. The game handles the text as if it was typed into that channel's chat box,
     * slash commands included, and ignores it unless the channel's menu or match is active.
     *
     * @return false if not connected
     */
    public boolean sendChat(ChatChannel chatChannel, String text, ChatAudience audience) throws IOException {
        byte[] body = ChatSend.newBuilder().setChannel(chatChannel).setText(text).setAudience(audience).build().toByteArray();
        if (body.length == 0 || body.length > FrameCodec.MAX_INBOUND_BODY_BYTES) {
            throw new IllegalArgumentException("ChatSend body must be 1.." + FrameCodec.MAX_INBOUND_BODY_BYTES + " bytes");
        }
        return writeFrame(FrameType.CHAT_SEND.id(), body);
    }

    /** Writes any frame; for tests and tools. */
    public boolean writeFrame(int type, byte[] body) throws IOException {
        AsynchronousFileChannel out = channel;
        if (out == null) {
            return false;
        }
        ByteBuffer frame = ByteBuffer.wrap(FrameCodec.encode(type, body));
        synchronized (writeLock) {
            try {
                while (frame.hasRemaining()) {
                    out.write(frame, 0).get();
                }
            } catch (ExecutionException e) {
                throw new IOException("write failed", e.getCause());
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("interrupted", e);
            }
        }
        return true;
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    @Override
    public void close() {
        closed = true;
        AsynchronousFileChannel current = channel;
        if (current != null) {
            try {
                current.close();
            } catch (IOException ignored) {
                // already broken
            }
        }
        if (thread != null) {
            thread.interrupt();
        }
    }
}
