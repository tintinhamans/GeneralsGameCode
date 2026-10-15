package com.generalsonline.stats;

import java.time.Duration;
import java.util.concurrent.TimeoutException;

/** A request got no response within its timeout. A response that arrives later is ignored. */
public final class RequestTimeoutException extends TimeoutException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param id the id the library gave the request
     * @param timeout how long it waited
     */
    public RequestTimeoutException(int id, Duration timeout) {
        super("no response to request " + Integer.toUnsignedString(id) + " within " + timeout.toMillis() + " ms");
    }
}
