package com.generalsonline.stats;

import com.generalsonline.stats.proto.ResponseStatus;

/**
 * A request got an answer other than OK. The typed request methods of {@link StatsPipe} complete exceptionally with
 * this; the status tells why (for example {@code RESPONSE_STATUS_RATE_LIMITED}: retry later).
 */
public class RequestFailedException extends Exception {
    private static final long serialVersionUID = 1L;

    /** The status of the response. */
    private final ResponseStatus status;

    /** The game's reason, possibly empty. */
    private final String reason;

    /**
     * Creates the exception.
     *
     * @param status the status the game answered with, or RATE_LIMITED for the library's own limiter
     * @param message the game's short reason, possibly empty
     */
    public RequestFailedException(ResponseStatus status, String message) {
        super(status.name().replace("RESPONSE_STATUS_", "") + (message == null || message.isEmpty() ? "" : ": " + message));
        this.status = status;
        this.reason = message == null ? "" : message;
    }

    /**
     * Returns the game's explanation.
     *
     * @return the short reason the game gave, empty when it gave none
     */
    public String reason() {
        return reason;
    }

    /**
     * Returns the status of the response.
     *
     * @return a status other than OK
     */
    public ResponseStatus status() {
        return status;
    }
}
