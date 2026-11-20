package com.generalsonline.stats;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.InterestMap;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;
import com.generalsonline.stats.proto.ViewStats;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.Message;

/**
 * Decodes a frame body to its message ({@link #decode}) and calls the matching listener method ({@link #deliver}).
 * Every consumer of frames goes through these two methods, so a new frame type is added in exactly one place: both
 * switches below are over {@link FrameType}.
 */
public final class FrameDispatcher {
    private FrameDispatcher() {
    }

    /**
     * Parses one frame. Never throws: an unknown type or a bad body is recorded in the result.
     *
     * @param frame the raw frame
     * @return the decoded frame
     */
    public static DecodedFrame decode(RawFrame frame) {
        FrameType type = FrameType.outboundFromId(frame.type());
        if (type == null) {
            return new DecodedFrame(frame, null, null, null);
        }
        byte[] body = frame.body();
        try {
            Message message;
            switch (type) {
                case MATCH_INFO:
                    message = MatchInfo.parseFrom(body);
                    break;
                case STATS_SNAPSHOT:
                    message = StatsSnapshot.parseFrom(body);
                    break;
                case MATCH_SUMMARY:
                    message = MatchSummary.parseFrom(body);
                    break;
                case CHAT_MESSAGE:
                    message = ChatMessage.parseFrom(body);
                    break;
                case UI_STATE:
                    message = UiState.parseFrom(body);
                    break;
                case LOBBY_STATE:
                    message = LobbyState.parseFrom(body);
                    break;
                case EVENTS:
                    message = Events.parseFrom(body);
                    break;
                case CAMERA:
                    message = Camera.parseFrom(body);
                    break;
                case HELLO:
                    message = Hello.parseFrom(body);
                    break;
                case RESPONSE:
                    message = Response.parseFrom(body);
                    break;
                case VIEW_STATS:
                    message = ViewStats.parseFrom(body);
                    break;
                case INTEREST_MAP:
                    message = InterestMap.parseFrom(body);
                    break;
                default:
                    message = null; // inbound only; outboundFromId never returns them
                    break;
            }
            return new DecodedFrame(frame, message == null ? null : type, message, null);
        } catch (InvalidProtocolBufferException e) {
            return new DecodedFrame(frame, type, null, e);
        }
    }

    /**
     * Calls {@link StatsListener#onFrame} and then the typed method for a decoded frame. Exceptions thrown by the
     * listener propagate.
     *
     * @param frame the decoded frame
     * @param listener the receiver
     */
    public static void deliver(DecodedFrame frame, StatsListener listener) {
        listener.onFrame(frame);
        deliverTyped(frame, listener);
    }

    private static void deliverTyped(DecodedFrame frame, StatsListener listener) {
        if (frame.error() != null) {
            listener.decodeError(frame.raw().type(), frame.raw().body(), frame.error());
            return;
        }
        if (frame.type() == null) {
            listener.unknownFrame(frame.raw().type(), frame.raw().body());
            return;
        }
        // The inbound only types (CHAT_SEND, REQUEST) are not delivered; they count as unknown.
        boolean delivered = true;
        switch (frame.type()) {
            case MATCH_INFO:
                listener.onMatchInfo((MatchInfo) frame.message());
                break;
            case STATS_SNAPSHOT:
                listener.onStatsSnapshot((StatsSnapshot) frame.message());
                break;
            case MATCH_SUMMARY:
                listener.onMatchSummary((MatchSummary) frame.message());
                break;
            case CHAT_MESSAGE:
                listener.onChatMessage((ChatMessage) frame.message());
                break;
            case UI_STATE:
                listener.onUiState((UiState) frame.message());
                break;
            case LOBBY_STATE:
                listener.onLobbyState((LobbyState) frame.message());
                break;
            case EVENTS:
                listener.onEvents((Events) frame.message());
                break;
            case CAMERA:
                listener.onCamera((Camera) frame.message());
                break;
            case HELLO:
                listener.onHello((Hello) frame.message());
                break;
            case RESPONSE:
                listener.onResponse((Response) frame.message());
                break;
            case VIEW_STATS:
                listener.onViewStats((ViewStats) frame.message());
                break;
            case INTEREST_MAP:
                listener.onInterestMap((InterestMap) frame.message());
                break;
            default:
                delivered = false;
                break;
        }
        if (!delivered) {
            listener.unknownFrame(frame.raw().type(), frame.raw().body());
        }
    }

    /**
     * Decodes and delivers; an exception from the listener propagates to the caller.
     *
     * @param frame the raw frame
     * @param listener the receiver
     */
    public static void dispatch(RawFrame frame, StatsListener listener) {
        deliver(decode(frame), listener);
    }

    /**
     * Like {@link #deliver}, but an exception thrown by the listener is reported through
     * {@link StatsListener#onError} of the same listener instead of propagating, so one bad frame never ends a
     * connection. A failure in {@link StatsListener#onFrame} does not stop the typed method.
     *
     * @param frame the decoded frame
     * @param listener the receiver, which also receives the error report
     */
    public static void deliverGuarded(DecodedFrame frame, StatsListener listener) {
        deliverGuarded(frame, listener, listener);
    }

    /**
     * Like {@link #deliverGuarded(DecodedFrame, StatsListener)}, with the errors reported to another listener.
     *
     * @param frame the decoded frame
     * @param target the receiver of the frame
     * @param errors the listener whose {@link StatsListener#onError} gets the exceptions {@code target} throws
     */
    public static void deliverGuarded(DecodedFrame frame, StatsListener target, StatsListener errors) {
        try {
            target.onFrame(frame);
        } catch (RuntimeException e) {
            reportError(frame.raw().type(), e, errors);
        }
        try {
            deliverTyped(frame, target);
        } catch (RuntimeException e) {
            reportError(frame.raw().type(), e, errors);
        }
    }

    /**
     * Calls {@link StatsListener#onError}; a listener that throws from it is ignored, there is nowhere left to report it.
     *
     * @param frameType the type byte of the frame being handled, or 0
     * @param cause what was thrown
     * @param listener the listener to tell
     */
    public static void reportError(int frameType, Throwable cause, StatsListener listener) {
        try {
            listener.onError(frameType, cause);
        } catch (RuntimeException ignored) {
            // nothing more can be done
        }
    }
}
