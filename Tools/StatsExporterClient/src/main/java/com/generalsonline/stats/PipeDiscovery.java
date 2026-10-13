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

/** Finds the stats pipes of running game processes: \\.\pipe\generalsonline-stats-&lt;pid&gt;. */
public final class PipeDiscovery {
    public static final String PIPE_DIRECTORY = "\\\\.\\pipe\\";
    public static final String PIPE_PREFIX = "generalsonline-stats-";

    private PipeDiscovery() {
    }

    public static String pipePath(int pid) {
        return PIPE_DIRECTORY + PIPE_PREFIX + pid;
    }

    /** The pid in a pipe name such as "generalsonline-stats-1234", or empty if the name is not a stats pipe. */
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

    /** Pids of the stats pipes in the given list of pipe names, ascending. */
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

    /** Lists \\.\pipe\ and returns the pids of the stats pipes found. Empty off Windows or when none exist. */
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

    /** The pipe path to use: the given pid if it is not negative, else the only (or the highest) pid found; null if none. */
    public static String choosePipe(int requestedPid) {
        if (requestedPid >= 0) {
            return pipePath(requestedPid);
        }
        List<Integer> pids = findPids();
        return pids.isEmpty() ? null : pipePath(pids.get(pids.size() - 1));
    }
}
