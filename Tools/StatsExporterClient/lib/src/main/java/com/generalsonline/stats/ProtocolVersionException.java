package com.generalsonline.stats;

import com.generalsonline.stats.proto.Hello;

import java.io.IOException;

/** The game's Hello names a protocol version this library does not speak. The connection is closed. */
public final class ProtocolVersionException extends IOException {
    private static final long serialVersionUID = 1L;

    /** The protocol version the game announced. */
    private final int version;
    private final transient Hello hello;

    /**
     * Creates the exception.
     *
     * @param hello the refused handshake
     */
    public ProtocolVersionException(Hello hello) {
        super("the game speaks stats pipe protocol version " + hello.getProtocolVersion() + " (game " + hello.getGameVersion()
            + "), this client speaks version " + Handshake.PROTOCOL_VERSION + "; use a matching client");
        this.version = hello.getProtocolVersion();
        this.hello = hello;
    }

    /**
     * Returns the version the game announced.
     *
     * @return the {@code protocol_version} of the refused Hello
     */
    public int version() {
        return version;
    }

    /**
     * Returns the refused handshake.
     *
     * @return the Hello, or null after the exception was serialized
     */
    public Hello hello() {
        return hello;
    }
}
