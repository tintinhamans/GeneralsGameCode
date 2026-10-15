package com.generalsonline.stats.internal;

import java.io.Closeable;
import java.io.IOException;

/**
 * Accepts reader connections for a fake game and hands each over as a {@link PipeLink}. Not part of the public API.
 */
public interface PipeServer extends Closeable {
    /** Receives a connected reader. */
    @FunctionalInterface
    interface Acceptor {
        /**
         * Called on the server's own thread for each new connection.
         *
         * @param slot the reader slot the transport gave the connection, 0 based
         * @param link the server's end of the connection
         */
        void accept(int slot, PipeLink link);
    }

    /**
     * Starts accepting. The pipe exists when this returns.
     *
     * @param acceptor receives every connection
     * @throws IOException when the pipe cannot be created
     */
    void start(Acceptor acceptor) throws IOException;
}
