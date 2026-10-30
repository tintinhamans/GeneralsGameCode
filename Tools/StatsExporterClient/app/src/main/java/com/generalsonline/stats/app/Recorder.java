package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.DumpWriter;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.RawFrame;
import com.google.protobuf.Message;

import javax.swing.filechooser.FileSystemView;
import java.io.IOException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Stream;

/**
 * A recording of the live feed: every frame the window receives is written to a file in the recordings folder. The
 * user sees it as "a recording"; the file is a dump of the library ({@link DumpWriter}) and can be played back in the
 * window, with {@code replay} on the command line, or with {@code DumpReader}. It is not a game replay (.rep).
 */
final class Recorder {
    private static final DateTimeFormatter STAMP = DateTimeFormatter.ofPattern("yyyy-MM-dd_HH-mm");

    private final DumpWriter writer;
    private final Path file;
    private final long startedNanos = System.nanoTime();
    private final AtomicLong bytes = new AtomicLong();
    private volatile String failure;
    private volatile boolean closed;

    private Recorder(DumpWriter writer, Path file) {
        this.writer = writer;
        this.file = file;
    }

    /** Documents\GeneralsOnline\Recordings (the Documents folder of the user, wherever Windows keeps it). */
    static Path folder() {
        String override = System.getProperty("stats.recordings.dir");
        if (override != null) {
            return Paths.get(override);
        }
        String saved = Prefs.get("recordingsDir", "");
        if (!saved.isEmpty()) {
            return Paths.get(saved);
        }
        Path documents;
        try {
            documents = FileSystemView.getFileSystemView().getDefaultDirectory().toPath();
        } catch (RuntimeException e) {
            documents = Paths.get(System.getProperty("user.home"));
        }
        return documents.resolve("GeneralsOnline").resolve("Recordings");
    }

    /**
     * Starts a recording in a new file, named after the date and the map; an existing file is never overwritten (a number is added).
     * The file begins with what the window already knows (the game's Hello, the screen, the lobby and the match), so a recording
     * that starts in the middle of a match plays back on its own.
     */
    static Recorder start(MatchState state, String mapName) throws IOException {
        Path folder = folder();
        Files.createDirectories(folder);
        String map = mapName.replaceAll("[^A-Za-z0-9]+", "-").replaceAll("^-|-$", "").toLowerCase(Locale.ROOT);
        String base = LocalDateTime.now().format(STAMP) + "-" + (map.isEmpty() ? "match" : map);
        for (int n = 1; ; n++) {
            Path path = folder.resolve(base + (n == 1 ? "" : "-" + n) + ".dump");
            try {
                Recorder recorder = new Recorder(new DumpWriter(path, false), path);
                recorder.preamble(state);
                return recorder;
            } catch (FileAlreadyExistsException e) {
                // try the next name
            }
        }
    }

    private void preamble(MatchState state) throws IOException {
        long sequence = 0;
        List<Message> known = new ArrayList<>();
        state.hello().ifPresent(known::add);
        state.ui().ifPresent(known::add);
        state.lobby().ifPresent(known::add);
        state.match().ifPresent(known::add);
        for (Message message : known) {
            FrameType type = message instanceof com.generalsonline.stats.proto.Hello ? FrameType.HELLO
                : message instanceof com.generalsonline.stats.proto.UiState ? FrameType.UI_STATE
                : message instanceof com.generalsonline.stats.proto.LobbyState ? FrameType.LOBBY_STATE : FrameType.MATCH_INFO;
            byte[] body = message.toByteArray();
            RawFrame raw = new RawFrame(type.id(), sequence++, 0, body.length == 0 ? new byte[] {0x08, 0x00} : body);
            writer.write(raw);
            bytes.addAndGet(raw.body().length + 17L);
        }
    }

    Path file() {
        return file;
    }

    /** Called on the reader thread for every frame. */
    void accept(DecodedFrame frame) {
        if (closed) {
            return;
        }
        try {
            writer.write(frame);
            bytes.addAndGet(frame.wireBytes());
        } catch (IOException e) {
            failure = e.getMessage() == null ? e.toString() : e.getMessage();
            close();
        }
    }

    long bytes() {
        return bytes.get();
    }

    long elapsedMillis() {
        return (System.nanoTime() - startedNanos) / 1_000_000;
    }

    /** Non-null when writing failed (a full disk, a removed folder); the recording stopped there. */
    String failure() {
        return failure;
    }

    boolean isOpen() {
        return !closed;
    }

    /** Pushes what is buffered to the file, so a crash loses about a second at most. */
    void flush() {
        if (closed) {
            return;
        }
        try {
            writer.flush();
        } catch (IOException e) {
            failure = e.getMessage() == null ? e.toString() : e.getMessage();
            close();
        }
    }

    void close() {
        if (closed) {
            return;
        }
        closed = true;
        try {
            writer.close();
        } catch (IOException e) {
            if (failure == null) {
                failure = e.getMessage();
            }
        }
    }

    /** The newest recordings in the folder, newest first. */
    static List<Path> recent(int limit) {
        Path folder = folder();
        if (!Files.isDirectory(folder)) {
            return new ArrayList<>();
        }
        try (Stream<Path> files = Files.list(folder)) {
            List<Path> found = new ArrayList<>();
            files.filter(p -> p.getFileName().toString().endsWith(".dump")).forEach(found::add);
            found.sort((a, b) -> Long.compare(b.toFile().lastModified(), a.toFile().lastModified()));
            return found.size() > limit ? new ArrayList<>(found.subList(0, limit)) : found;
        } catch (IOException e) {
            return new ArrayList<>();
        }
    }

    /** "1:05" or "1:02:03". */
    static String clock(long millis) {
        long seconds = millis / 1000;
        return seconds >= 3600 ? String.format("%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60) : String.format("%d:%02d", seconds / 60, seconds % 60);
    }

    /** "850 KB" or "12.4 MB". */
    static String size(long bytes) {
        return bytes < 1_000_000 ? Math.max(1, bytes / 1000) + " KB" : String.format(Locale.ROOT, "%.1f MB", bytes / 1_000_000.0);
    }
}
