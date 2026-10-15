package com.generalsonline.stats;

import java.io.IOException;
import java.time.Duration;

/** No Hello arrived in time after the pipe was opened. The connection is closed. */
public final class HelloTimeoutException extends IOException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param timeout how long the library waited
     */
    public HelloTimeoutException(Duration timeout) {
        super("no Hello from the game within " + timeout.toMillis() + " ms; the game may speak an older stats pipe protocol");
    }
}
