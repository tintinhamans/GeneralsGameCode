package com.generalsonline.stats;

import com.generalsonline.stats.proto.Hello;

/** Helpers for the {@code Hello} the game sends first on every connection. */
public final class Handshake {
    /** The protocol version this library speaks. A Hello with another version is refused. */
    public static final int PROTOCOL_VERSION = 2;

    private Handshake() {
    }

    /**
     * Tells whether the game announced a capability.
     *
     * @param hello the handshake
     * @param name a capability name, see {@code docs/stats-exporter.md}; compared exactly
     * @return true when {@code Hello.capabilities} contains it
     */
    public static boolean hasCapability(Hello hello, String name) {
        return hello.getCapabilitiesList().contains(name);
    }

    /**
     * Checks the protocol version.
     *
     * @param hello the handshake
     * @throws ProtocolVersionException when the game speaks another protocol version than {@link #PROTOCOL_VERSION}
     */
    public static void requireSupported(Hello hello) throws ProtocolVersionException {
        if (hello.getProtocolVersion() != PROTOCOL_VERSION) {
            throw new ProtocolVersionException(hello);
        }
    }

    /**
     * Converts a frame timestamp to wall clock time with the epoch of the Hello.
     *
     * @param hello the handshake of the connection the frame came from
     * @param timestampMicros {@link RawFrame#timestampMicros()} of a frame of that connection
     * @return milliseconds since the Unix epoch, as {@code System.currentTimeMillis()} counts
     */
    public static long wallClockMillis(Hello hello, long timestampMicros) {
        return hello.getEpochUnixMs() + Long.divideUnsigned(timestampMicros, 1000);
    }

    /**
     * Describes the game in one line, for a status area or a log.
     *
     * @param hello the handshake
     * @return for example {@code "game 1.04, protocol 2, pid 4242, reader 0 of 4, 30 logic fps"}
     */
    public static String summary(Hello hello) {
        return "game " + hello.getGameVersion() + ", protocol " + hello.getProtocolVersion() + ", pid " + Format.u(hello.getProcessId())
            + ", reader " + hello.getReaderSlot() + " of " + hello.getMaxReaders() + ", " + hello.getLogicFramesPerSecond() + " logic fps";
    }
}
