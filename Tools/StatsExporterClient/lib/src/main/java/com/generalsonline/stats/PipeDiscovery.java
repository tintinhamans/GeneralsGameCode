package com.generalsonline.stats;

import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.OptionalInt;

/**
 * Finds the stats pipes of running game processes: the named pipes {@code generalsonline-stats-<pid>}. {@link StatsPipe}
 * uses this to pick a game; call it yourself to list the running games.
 */
public final class PipeDiscovery {
    /** The Windows folder of named pipes. */
    public static final String PIPE_DIRECTORY = "\\\\.\\pipe\\";

    /** The pipe name up to the process id. */
    public static final String PIPE_PREFIX = "generalsonline-stats-";

    private PipeDiscovery() {
    }

    /**
     * Builds the pipe path of a game process.
     *
     * @param pid the process id of the game
     * @return {@code \\.\pipe\generalsonline-stats-<pid>}
     */
    public static String pipePath(int pid) {
        return PIPE_DIRECTORY + PIPE_PREFIX + pid;
    }

    /**
     * Reads the pid out of a pipe name.
     *
     * @param pipeName a name such as "generalsonline-stats-1234", without the pipe folder
     * @return the pid, or empty when the name is not a stats pipe
     */
    public static OptionalInt parsePid(String pipeName) {
        if (pipeName == null || !pipeName.startsWith(PIPE_PREFIX)) {
            return OptionalInt.empty();
        }
        String digits = pipeName.substring(PIPE_PREFIX.length());
        if (digits.isEmpty() || digits.length() > 10) {
            return OptionalInt.empty();
        }
        for (int i = 0; i < digits.length(); i++) {
            if (digits.charAt(i) < '0' || digits.charAt(i) > '9') {
                return OptionalInt.empty();
            }
        }
        long pid = Long.parseLong(digits);
        if (pid > Integer.MAX_VALUE) {
            return OptionalInt.empty();
        }
        return OptionalInt.of((int) pid);
    }

    /**
     * Picks the stats pipes out of a list of pipe names.
     *
     * @param names the names in the pipe folder; may be null
     * @return the pids of the stats pipes among them, ascending
     */
    public static List<Integer> pidsFromNames(String[] names) {
        List<Integer> pids = new ArrayList<>();
        if (names != null) {
            for (String name : names) {
                OptionalInt pid = parsePid(name);
                if (pid.isPresent()) {
                    pids.add(pid.getAsInt());
                }
            }
        }
        Collections.sort(pids);
        return pids;
    }

    /**
     * Lists the pipe folder and returns the games found.
     *
     * @return the pids of the stats pipes, ascending; empty off Windows or when no game runs
     */
    public static List<Integer> findPids() {
        // java.io.File.list() returns null for the pipe directory, so a DirectoryStream is used.
        List<String> names = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(Paths.get(PIPE_DIRECTORY))) {
            for (Path entry : stream) {
                names.add(entry.getFileName().toString());
            }
        } catch (IOException | RuntimeException e) {
            return Collections.emptyList();
        }
        return pidsFromNames(names.toArray(new String[0]));
    }

    /**
     * Reads the pid out of a pipe path.
     *
     * @param path a pipe path ending in a stats pipe name such as generalsonline-stats-1234
     * @return the pid, or empty for any other path
     */
    public static OptionalInt pidOfPath(String path) {
        if (path == null) {
            return OptionalInt.empty();
        }
        int slash = Math.max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
        return parsePid(path.substring(slash + 1));
    }

    /**
     * Lists the pipes to try, in order. An explicit pipe or pid is the only candidate. Otherwise every stats pipe
     * found, highest pid first. The highest pid is only a guess at the newest game (Windows reuses pids), so it is a
     * first choice, not a rule: {@link #openFirst} falls through to the next pipe when one is busy.
     *
     * @param requestedPid a game's pid, or a negative number for any game
     * @param explicitPipe a full pipe path, or null
     * @return the pipe paths, best first; empty when no game runs
     */
    public static List<String> candidates(int requestedPid, String explicitPipe) {
        if (explicitPipe != null) {
            return List.of(explicitPipe);
        }
        if (requestedPid >= 0) {
            return List.of(pipePath(requestedPid));
        }
        return candidatesFromPids(findPids());
    }

    /**
     * Orders pipes for the given games.
     *
     * @param ascendingPids pids as {@link #pidsFromNames} returns them
     * @return their pipe paths, highest pid first
     */
    public static List<String> candidatesFromPids(List<Integer> ascendingPids) {
        List<String> paths = new ArrayList<>();
        for (int i = ascendingPids.size() - 1; i >= 0; i--) {
            paths.add(pipePath(ascendingPids.get(i)));
        }
        return paths;
    }

    /**
     * Picks the first candidate, for callers that connect once.
     *
     * @param requestedPid a game's pid, or a negative number for any game
     * @return the best pipe path, or null when no game runs
     */
    public static String choosePipe(int requestedPid) {
        List<String> paths = candidates(requestedPid, null);
        return paths.isEmpty() ? null : paths.get(0);
    }

    /**
     * Opens one pipe; throws when it cannot be opened, for example because another reader holds it (ERROR_PIPE_BUSY).
     *
     * @param <T> what an open pipe is represented by
     */
    @FunctionalInterface
    public interface PipeOpener<T> {
        /**
         * Opens a pipe.
         *
         * @param path the pipe path
         * @return the open pipe, or null when there is none
         * @throws IOException when the pipe cannot be opened
         */
        T open(String path) throws IOException;
    }

    /**
     * A pipe that was opened, with the path it was opened under.
     *
     * @param <T> what an open pipe is represented by
     * @param path the pipe path that opened
     * @param channel the open pipe
     */
    public record Opened<T>(String path, T channel) {
    }

    /**
     * Tries each path in turn and returns the first that opens. The game accepts one reader per pipe, so a pipe held
     * by another client fails to open; trying the others finds a game that is free.
     *
     * @param <T> what an open pipe is represented by
     * @param paths the candidates, best first
     * @param opener opens one pipe
     * @return the first pipe that opened, or null when none did
     */
    public static <T> Opened<T> openFirst(List<String> paths, PipeOpener<T> opener) {
        for (String path : paths) {
            try {
                T channel = opener.open(path);
                if (channel != null) {
                    return new Opened<>(path, channel);
                }
            } catch (IOException | RuntimeException e) {
                // busy, or the game exited since the listing: try the next one
            }
        }
        return null;
    }
}
