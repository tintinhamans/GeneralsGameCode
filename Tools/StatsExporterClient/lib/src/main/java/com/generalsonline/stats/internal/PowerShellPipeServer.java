package com.generalsonline.stats.internal;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Base64;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Serves a real Windows named pipe. Java cannot create one, so a hidden Windows PowerShell process hosts the pipe
 * (a few lines of C# compiled by Add-Type) and relays the bytes of each connection over its standard streams, as
 * frames of {@code u8 op, u8 slot, u8 generation, u32 length, payload}. The helper exits when this process closes its
 * input, so it cannot outlive the JVM. Not part of the public API.
 */
public final class PowerShellPipeServer implements PipeServer {
    private static final byte[] MAGIC = {'G', 'O', 'P', 'S'};
    private static final int HEADER_BYTES = 7;
    private static final int MAX_ERROR_CHARS = 2000;

    private static final String HOST_SOURCE = """
        using System;
        using System.Collections.Generic;
        using System.IO;
        using System.IO.Pipes;
        using System.Threading;

        public static class GoPipeHost
        {
            sealed class Item { public int Gen; public byte[] Data; }
            sealed class Slot { public NamedPipeServerStream Stream; public int Gen; public readonly Queue<Item> Queue = new Queue<Item>(); }

            static Stream output;
            static readonly object outputLock = new object();
            static Slot[] slots;

            static void Send(byte op, int slot, int gen, byte[] data, int length)
            {
                lock (outputLock)
                {
                    byte[] h = new byte[7];
                    h[0] = op; h[1] = (byte)slot; h[2] = (byte)gen;
                    h[3] = (byte)length; h[4] = (byte)(length >> 8); h[5] = (byte)(length >> 16); h[6] = (byte)(length >> 24);
                    output.Write(h, 0, 7);
                    if (length > 0) output.Write(data, 0, length);
                    output.Flush();
                }
            }

            static NamedPipeServerStream Make(string name, int instances)
            {
                return new NamedPipeServerStream(name, PipeDirection.InOut, instances, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
            }

            static void ReadExactly(Stream s, byte[] buf, int count)
            {
                int got = 0;
                while (got < count)
                {
                    int n = s.Read(buf, got, count - got);
                    if (n <= 0) Environment.Exit(0);
                    got += n;
                }
            }

            static void Serve(string name, int count, int index)
            {
                Slot slot = slots[index];
                byte[] buf = new byte[65536];
                for (;;)
                {
                    NamedPipeServerStream s;
                    lock (slot) { s = slot.Stream; }
                    int gen = -1;
                    try
                    {
                        s.WaitForConnection();
                        lock (slot) { slot.Gen = (slot.Gen + 1) & 255; gen = slot.Gen; slot.Queue.Clear(); }
                        Send((byte)'C', index, gen, null, 0);
                        for (;;)
                        {
                            int n = s.Read(buf, 0, buf.Length);
                            if (n <= 0) break;
                            Send((byte)'D', index, gen, buf, n);
                        }
                    }
                    catch (IOException) { }
                    catch (ObjectDisposedException) { }
                    catch (InvalidOperationException) { }
                    lock (slot) { slot.Gen = (slot.Gen + 1) & 255; }
                    if (gen >= 0) Send((byte)'X', index, gen, null, 0);
                    try { s.Dispose(); } catch (Exception) { }
                    NamedPipeServerStream next = null;
                    while (next == null)
                    {
                        try { next = Make(name, count + 1); } catch (Exception) { Thread.Sleep(50); }
                    }
                    lock (slot) { slot.Stream = next; }
                }
            }

            static void Writer(int index)
            {
                Slot slot = slots[index];
                for (;;)
                {
                    Item item; NamedPipeServerStream s;
                    lock (slot)
                    {
                        while (slot.Queue.Count == 0) Monitor.Wait(slot);
                        item = slot.Queue.Dequeue();
                        s = slot.Stream;
                        if (item.Gen != slot.Gen) continue;
                    }
                    try { s.Write(item.Data, 0, item.Data.Length); s.Flush(); } catch (Exception) { }
                }
            }

            public static void Run(string name, int count)
            {
                output = Console.OpenStandardOutput();
                Stream input = Console.OpenStandardInput();
                slots = new Slot[count];
                try
                {
                    for (int i = 0; i < count; i++) { slots[i] = new Slot(); slots[i].Stream = Make(name, count + 1); }
                }
                catch (Exception e)
                {
                    Console.Error.WriteLine("cannot create the pipe: " + e.Message);
                    Environment.Exit(3);
                }
                lock (outputLock) { output.Write(new byte[] { 71, 79, 80, 83 }, 0, 4); }
                Send((byte)'R', 0, 0, null, 0);
                for (int i = 0; i < count; i++)
                {
                    int index = i;
                    Thread serve = new Thread(() => Serve(name, count, index)); serve.IsBackground = true; serve.Start();
                    Thread writer = new Thread(() => Writer(index)); writer.IsBackground = true; writer.Start();
                }
                byte[] header = new byte[7];
                for (;;)
                {
                    ReadExactly(input, header, 7);
                    int length = header[3] | (header[4] << 8) | (header[5] << 16) | (header[6] << 24);
                    byte[] payload = new byte[length];
                    if (length > 0) ReadExactly(input, payload, length);
                    if (header[0] == (byte)'Q') Environment.Exit(0);
                    int index = header[1];
                    if (index >= count) continue;
                    Slot slot = slots[index];
                    if (header[0] == (byte)'W')
                    {
                        lock (slot)
                        {
                            if (header[2] == slot.Gen) { Item item = new Item(); item.Gen = header[2]; item.Data = payload; slot.Queue.Enqueue(item); Monitor.Pulse(slot); }
                        }
                    }
                    else if (header[0] == (byte)'K')
                    {
                        NamedPipeServerStream s = null;
                        lock (slot) { if (header[2] == slot.Gen) s = slot.Stream; }
                        if (s != null) { try { s.Dispose(); } catch (Exception) { } }
                    }
                }
            }
        }
        """;

    private final String pipeName;
    private final int slotCount;
    private final Map<Integer, Link> links = new ConcurrentHashMap<>();
    private final Object inputLock = new Object();
    private final StringBuilder errors = new StringBuilder();
    private final CountDownLatch ready = new CountDownLatch(1);
    private volatile boolean closed;
    private Process process;
    private OutputStream toHelper;
    private Acceptor acceptor;

    /**
     * Creates a server; nothing runs until {@link #start}.
     *
     * @param pipeName the name after {@code \\.\pipe\}, letters, digits, dot, dash and underscore only
     * @param slotCount how many readers the pipe serves at once, 1 to 16
     */
    public PowerShellPipeServer(String pipeName, int slotCount) {
        if (!pipeName.matches("[A-Za-z0-9._-]+")) {
            throw new IllegalArgumentException("a pipe name has letters, digits, '.', '-' and '_' only, got '" + pipeName + "'");
        }
        if (slotCount < 1 || slotCount > 16) {
            throw new IllegalArgumentException("slots must be 1 to 16, got " + slotCount);
        }
        this.pipeName = pipeName;
        this.slotCount = slotCount;
    }

    /**
     * Finds the Windows PowerShell executable.
     *
     * @return its path, or null when this is not Windows
     */
    public static String executable() {
        if (!System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT).contains("windows")) {
            return null;
        }
        String root = System.getenv("SystemRoot");
        if (root != null) {
            Path path = Path.of(root, "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
            if (Files.isRegularFile(path)) {
                return path.toString();
            }
        }
        return "powershell.exe";
    }

    /**
     * Tells whether a named pipe can be served here.
     *
     * @return true on Windows
     */
    public static boolean isAvailable() {
        return executable() != null;
    }

    @Override
    public synchronized void start(Acceptor acceptor) throws IOException {
        String shell = executable();
        if (shell == null) {
            throw new IOException("serving a real named pipe needs Windows (Windows PowerShell hosts the pipe); use the in-process connection elsewhere");
        }
        this.acceptor = acceptor;
        String script = "Add-Type -TypeDefinition @'\n" + HOST_SOURCE + "'@\n[GoPipeHost]::Run('" + pipeName + "', " + slotCount + ")\n";
        String encoded = Base64.getEncoder().encodeToString(script.getBytes(StandardCharsets.UTF_16LE));
        ProcessBuilder builder = new ProcessBuilder(shell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-EncodedCommand", encoded);
        process = builder.start();
        toHelper = process.getOutputStream();
        daemon("stats-pipe-host-errors", () -> drainErrors(process.getErrorStream())).start();
        daemon("stats-pipe-host", () -> readHelper(process.getInputStream())).start();
        try {
            boolean up = ready.await(30, TimeUnit.SECONDS);
            if (!up || !process.isAlive()) {
                String why = errorText();
                close();
                throw new IOException("the named pipe host did not start" + (why.isEmpty() ? "" : ": " + why));
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            close();
            throw new IOException("interrupted while starting the named pipe host", e);
        }
    }

    private static Thread daemon(String name, Runnable task) {
        Thread thread = new Thread(task, name);
        thread.setDaemon(true);
        return thread;
    }

    private synchronized String errorText() {
        return errors.toString().trim();
    }

    private void drainErrors(InputStream in) {
        byte[] buffer = new byte[1024];
        try {
            int count;
            while ((count = in.read(buffer)) >= 0) {
                synchronized (this) {
                    if (errors.length() < MAX_ERROR_CHARS) {
                        errors.append(new String(buffer, 0, count, StandardCharsets.UTF_8));
                    }
                }
            }
        } catch (IOException ignored) {
            // the process ended
        }
    }

    private void readHelper(InputStream raw) {
        DataInputStream in = new DataInputStream(raw);
        try {
            int matched = 0;
            while (matched < MAGIC.length) {
                int b = in.read();
                if (b < 0) {
                    return;
                }
                matched = b == MAGIC[matched] ? matched + 1 : (b == MAGIC[0] ? 1 : 0);
            }
            byte[] header = new byte[HEADER_BYTES];
            while (true) {
                in.readFully(header);
                int op = header[0];
                int slot = header[1] & 0xFF;
                int generation = header[2] & 0xFF;
                int length = (header[3] & 0xFF) | ((header[4] & 0xFF) << 8) | ((header[5] & 0xFF) << 16) | ((header[6] & 0xFF) << 24);
                byte[] payload = new byte[length];
                in.readFully(payload);
                switch (op) {
                    case 'R' -> ready.countDown();
                    case 'C' -> {
                        Link link = new Link(slot, generation);
                        links.put(slot, link);
                        acceptor.accept(slot, link);
                    }
                    case 'D' -> {
                        Link link = links.get(slot);
                        if (link != null && link.generation == generation) {
                            link.incoming.add(payload);
                        }
                    }
                    case 'X' -> {
                        Link link = links.get(slot);
                        if (link != null && link.generation == generation) {
                            links.remove(slot, link);
                            link.endOfStream();
                        }
                    }
                    default -> {
                        // an op of a newer helper: ignore
                    }
                }
            }
        } catch (EOFException e) {
            // the helper ended
        } catch (IOException e) {
            // the helper's output broke: the same
        } finally {
            for (Link link : links.values()) {
                link.endOfStream();
            }
            links.clear();
            ready.countDown();
        }
    }

    private void send(int op, int slot, int generation, byte[] payload) throws IOException {
        byte[] data = payload == null ? new byte[0] : payload;
        byte[] frame = new byte[HEADER_BYTES + data.length];
        frame[0] = (byte) op;
        frame[1] = (byte) slot;
        frame[2] = (byte) generation;
        frame[3] = (byte) data.length;
        frame[4] = (byte) (data.length >>> 8);
        frame[5] = (byte) (data.length >>> 16);
        frame[6] = (byte) (data.length >>> 24);
        System.arraycopy(data, 0, frame, HEADER_BYTES, data.length);
        synchronized (inputLock) {
            if (closed && op != 'Q') {
                throw new IOException("the pipe server is closed");
            }
            toHelper.write(frame);
            toHelper.flush();
        }
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        try {
            send('Q', 0, 0, null);
        } catch (IOException ignored) {
            // already gone
        }
        closed = true;
        for (Link link : links.values()) {
            link.endOfStream();
        }
        Process helper = process;
        if (helper != null) {
            try {
                if (!helper.waitFor(2, TimeUnit.SECONDS)) {
                    helper.destroyForcibly();
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                helper.destroyForcibly();
            }
        }
    }

    /** One reader's connection: the bytes the helper relays. */
    private final class Link implements PipeLink {
        private final byte[] end = new byte[0];
        private final int slot;
        private final int generation;
        private final BlockingQueue<byte[]> incoming = new LinkedBlockingQueue<>();
        private byte[] rest = new byte[0];
        private volatile boolean ended;

        Link(int slot, int generation) {
            this.slot = slot;
            this.generation = generation;
        }

        void endOfStream() {
            ended = true;
            incoming.add(end);
        }

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
                if (next == end) {
                    incoming.add(end);
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
            if (ended) {
                throw new IOException("the reader disconnected");
            }
            byte[] copy = new byte[buffer.remaining()];
            buffer.get(copy);
            send('W', slot, generation, copy);
        }

        @Override
        public void close() {
            if (!ended) {
                try {
                    send('K', slot, generation, null);
                } catch (IOException ignored) {
                    // the helper is gone
                }
                links.remove(slot, this);
                endOfStream();
            }
        }
    }
}
