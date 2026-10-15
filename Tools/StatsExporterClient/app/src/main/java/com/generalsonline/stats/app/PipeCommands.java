package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.NotAllowedException;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.RequestTimeoutException;
import com.generalsonline.stats.StatsPipe;

import java.io.IOException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * How the panels that ask the game things reach it: a request goes out on the pipe, and the answer (or the reason there is
 * none) comes back on the event thread as text fit for a label. No Swing in here, so the failure texts are tested without a display.
 *
 * <p>A game of this protocol answers a request with a status; the typed methods of {@link StatsPipe} turn every status
 * but OK into an exception. {@link #describe} maps those to a short kind ("NOT ALLOWED", "NOT_FOUND", ...) and a sentence.
 */
final class PipeCommands {
    /**
     * Why a command did not work.
     *
     * @param kind a short word for a label, for example NOT ALLOWED or NOT_FOUND
     * @param text a sentence that says what happened and what to do
     * @param notAllowed true for the game's refusal because of its situation (the player plays, no replay runs)
     */
    record Failure(String kind, String text, boolean notAllowed) {
        /** One line for a status label. */
        String line() {
            return kind + ": " + text;
        }
    }

    private final Supplier<StatsPipe> pipe;
    private final Executor eventThread;

    /**
     * @param pipe the connection, or null while there is none (a replay file, no game yet)
     * @param eventThread runs the callbacks (SwingUtilities::invokeLater); tests pass Runnable::run
     */
    PipeCommands(Supplier<StatsPipe> pipe, Executor eventThread) {
        this.pipe = pipe;
        this.eventThread = eventThread;
    }

    /** True when a pipe exists and is connected with its Hello received. */
    boolean available() {
        StatsPipe p = pipe.get();
        return p != null && p.isConnected() && p.hello().isPresent();
    }

    /** The connection, or null. */
    StatsPipe pipe() {
        return pipe.get();
    }

    /**
     * Sends one command and reports the outcome on the event thread.
     *
     * @param call starts the request on the pipe
     * @param ok receives the result
     * @param failed receives the reason when there is no result
     * @param <T> the result type
     */
    <T> void run(Function<StatsPipe, CompletableFuture<T>> call, Consumer<T> ok, Consumer<Failure> failed) {
        StatsPipe p = pipe.get();
        if (p == null) {
            failed.accept(new Failure("NOT CONNECTED", "no game is connected; requests need a live pipe (or a fake game: serve a dump and connect to it)", false));
            return;
        }
        CompletableFuture<T> future;
        try {
            future = call.apply(p);
        } catch (RuntimeException e) {
            failed.accept(describe(e));
            return;
        }
        future.whenComplete((result, error) -> eventThread.execute(() -> {
            if (error != null) {
                failed.accept(describe(error));
            } else {
                ok.accept(result);
            }
        }));
    }

    /** Turns whatever a request failed with into a short kind and a sentence. */
    static Failure describe(Throwable error) {
        Throwable cause = error;
        while ((cause instanceof CompletionException || cause instanceof java.util.concurrent.ExecutionException) && cause.getCause() != null) {
            cause = cause.getCause();
        }
        if (cause instanceof NotAllowedException refused) {
            String why = refused.reason().isEmpty() ? "the game's situation does not allow it" : refused.reason();
            return new Failure("NOT ALLOWED", "the game refused: " + why + ". Camera and observer commands need an observer or a replay, replay commands a replay.", true);
        }
        if (cause instanceof RequestFailedException failed) {
            String status = Format.strip(failed.status().name(), "RESPONSE_STATUS_");
            return new Failure(status, failed.reason().isEmpty() ? "the game answered " + status : failed.reason(), false);
        }
        if (cause instanceof RequestTimeoutException) {
            return new Failure("TIMEOUT", "the game did not answer in time", false);
        }
        if (cause instanceof IOException) {
            return new Failure("NOT CONNECTED", Format.cause(cause), false);
        }
        if (cause instanceof IllegalStateException) {
            return new Failure("NOT READY", Format.cause(cause), false);
        }
        if (cause instanceof IllegalArgumentException) {
            return new Failure("INVALID", Format.cause(cause), false);
        }
        return new Failure("ERROR", Format.cause(cause), false);
    }
}
