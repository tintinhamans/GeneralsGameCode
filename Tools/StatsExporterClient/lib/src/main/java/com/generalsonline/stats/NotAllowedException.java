package com.generalsonline.stats;

import com.generalsonline.stats.proto.ResponseStatus;

/**
 * The game refused a command because of the situation it is in, not because the command was wrong: an observe command
 * while the local player plays (only observers and replay playback may choose a point of view), or a replay command while no
 * replay plays back. The {@link #reason()} is the game's own explanation. Nothing was changed.
 */
public final class NotAllowedException extends RequestFailedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param reason the game's short explanation, possibly empty
     */
    public NotAllowedException(String reason) {
        super(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED, reason);
    }
}
