package com.generalsonline.stats;

import java.io.IOException;

/** An unrecoverable framing error: a byte stream cannot resynchronize after a bad length. */
public final class FrameException extends IOException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param message what is wrong with the stream
     */
    public FrameException(String message) {
        super(message);
    }
}
