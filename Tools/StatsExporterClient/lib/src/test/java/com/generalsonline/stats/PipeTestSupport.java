package com.generalsonline.stats;

import com.generalsonline.stats.internal.FrameCodec;
import com.generalsonline.stats.internal.PipeLink;
import com.generalsonline.stats.proto.Request;
import com.google.protobuf.Message;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;

/** A scripted pipe and the helpers the pipe tests share. */
final class PipeTestSupport {
    private static final byte[] EOF = new byte[0];

    private PipeTestSupport() {
    }

    /** A scripted pipe: the bytes it was given are read in order; after an EOF marker (or close) reads return -1. */
    static class FakeLink implements PipeLink {
        private final BlockingQueue<byte[]> incoming = new LinkedBlockingQueue<>();
        final List<byte[]> written = Collections.synchronizedList(new ArrayList<>());
        private volatile boolean closed;

        void give(byte[] bytes) {
            incoming.add(bytes);
        }

        void endOfStream() {
            incoming.add(EOF);
        }

        boolean isClosed() {
            return closed;
        }

        /** The requests written so far, decoded from the inbound frames on the wire. */
        List<Request> requests() throws IOException {
            List<Request> requests = new ArrayList<>();
            synchronized (written) {
                for (byte[] bytes : written) {
                    RawFrame frame = FrameCodec.Decoder.inbound().push(bytes).get(0);
                    if (frame.type() == FrameType.REQUEST.id()) {
                        requests.add(Request.parseFrom(frame.body()));
                    }
                }
            }
            return requests;
        }

        @Override
        public int read(ByteBuffer buffer) throws IOException {
            while (true) {
                if (closed) {
                    throw new IOException("closed");
                }
                try {
                    byte[] next = incoming.poll(20, TimeUnit.MILLISECONDS);
                    if (next == null) {
                        continue;
                    }
                    if (next == EOF) {
                        return -1;
                    }
                    buffer.put(next);
                    return next.length;
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("interrupted", e);
                }
            }
        }

        @Override
        public void write(ByteBuffer buffer) throws IOException {
            byte[] copy = new byte[buffer.remaining()];
            buffer.get(copy);
            written.add(copy);
        }

        @Override
        public void close() {
            closed = true;
        }
    }

    static void await(String what, BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (!condition.getAsBoolean()) {
            if (System.nanoTime() > deadline) {
                throw new AssertionError("timed out waiting for " + what);
            }
            Thread.sleep(5);
        }
    }

    static StatsPipe.Builder fake(FakeLink... links) {
        BlockingQueue<FakeLink> queue = new LinkedBlockingQueue<>(List.of(links));
        return StatsPipe.builder().candidates(() -> List.of("fake-pipe-1")).opener(path -> {
            FakeLink next = queue.poll();
            if (next == null) {
                throw new IOException("busy");
            }
            return next;
        });
    }

    /** A Hello (sequence 0) followed by the messages, numbered 1, 2, ... like the frames of a new reader. */
    static byte[] withHello(Message... messages) throws IOException {
        Message[] all = new Message[messages.length + 1];
        all[0] = TestMessages.hello();
        System.arraycopy(messages, 0, all, 1, messages.length);
        return TestMessages.outboundStream(all);
    }
}
