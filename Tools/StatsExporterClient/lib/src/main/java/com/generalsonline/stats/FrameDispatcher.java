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
 * switches below are switch expressions over {@link FrameType}, which the compiler requires to cover every constant.
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
            // A switch expression over an enum must cover every constant, so a new FrameType does not compile until it is handled here.
            Message message = switch (type) {
                case MATCH_INFO -> MatchInfo.parseFrom(body);
                case STATS_SNAPSHOT -> StatsSnapshot.parseFrom(body);
                case MATCH_SUMMARY -> MatchSummary.parseFrom(body);
                case CHAT_MESSAGE -> ChatMessage.parseFrom(body);
                case UI_STATE -> UiState.parseFrom(body);
                case LOBBY_STATE -> LobbyState.parseFrom(body);
                case EVENTS -> Events.parseFrom(body);
                case CAMERA -> Camera.parseFrom(body);
                case HELLO -> Hello.parseFrom(body);
                case RESPONSE -> Response.parseFrom(body);
                case VIEW_STATS -> ViewStats.parseFrom(body);
                case INTEREST_MAP -> InterestMap.parseFrom(body);
                case CHAT_SEND, REQUEST -> null; // inbound only; outboundFromId never returns them
            };
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
        // Again an exhaustive switch expression: the yield value says whether the frame was a known outbound type.
        boolean delivered = switch (frame.type()) {
            case MATCH_INFO -> {
                listener.onMatchInfo((MatchInfo) frame.message());
                yield true;
            }
            case STATS_SNAPSHOT -> {
                listener.onStatsSnapshot((StatsSnapshot) frame.message());
                yield true;
            }
            case MATCH_SUMMARY -> {
                listener.onMatchSummary((MatchSummary) frame.message());
                yield true;
            }
            case CHAT_MESSAGE -> {
                listener.onChatMessage((ChatMessage) frame.message());
                yield true;
            }
            case UI_STATE -> {
                listener.onUiState((UiState) frame.message());
                yield true;
            }
            case LOBBY_STATE -> {
                listener.onLobbyState((LobbyState) frame.message());
                yield true;
            }
            case EVENTS -> {
                listener.onEvents((Events) frame.message());
                yield true;
            }
            case CAMERA -> {
                listener.onCamera((Camera) frame.message());
                yield true;
            }
            case HELLO -> {
                listener.onHello((Hello) frame.message());
                yield true;
            }
            case RESPONSE -> {
                listener.onResponse((Response) frame.message());
                yield true;
            }
            case VIEW_STATS -> {
                listener.onViewStats((ViewStats) frame.message());
                yield true;
            }
            case INTEREST_MAP -> {
                listener.onInterestMap((InterestMap) frame.message());
                yield true;
            }
            case CHAT_SEND, REQUEST -> false;
        };
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
