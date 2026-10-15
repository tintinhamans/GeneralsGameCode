package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static com.generalsonline.stats.PipeTestSupport.fake;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.CameraEasing;
import com.generalsonline.stats.proto.CameraFollowMode;
import com.generalsonline.stats.proto.CameraState;
import com.generalsonline.stats.proto.ObserveState;
import com.generalsonline.stats.proto.ReplayState;
import com.generalsonline.stats.proto.Request;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.ResponseStatus;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.time.Duration;
import java.util.OptionalDouble;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

/** The typed camera, replay and observer commands of StatsPipe on a fake pipe: what goes out, what comes back, and the typed refusal. */
class ControlRequestTest {
    private final PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
    private final StatsPipe pipe;
    private long sequence = 1;

    ControlRequestTest() throws Exception {
        pipe = fake(link).reconnect(false).limitRequests(false).build().start();
        link.give(TestMessages.outbound(TestMessages.hello(), 0));
        await("the Hello", () -> pipe.hello().isPresent());
    }

    @AfterEach
    void close() {
        pipe.close();
    }

    private Request written(int index) throws Exception {
        await("request " + index, () -> link.written.size() > index);
        return link.requests().get(index);
    }

    private void answer(Request request, Response.Builder result) {
        link.give(TestMessages.outbound(result.setId(request.getId()).build(), sequence++));
    }

    private static <T> T get(CompletableFuture<T> future) throws Exception {
        return future.get(10, TimeUnit.SECONDS);
    }

    @Test
    void aCameraMoveGoesOutAsOneControlQueryAndTheStateComesBack() throws Exception {
        CompletableFuture<CameraState> future = pipe.moveCamera(CameraMove.to(1800, 2400).zoom(1.2).angle(0.5).over(Duration.ofSeconds(2)).easeInOut());
        Request sent = written(0);
        assertEquals(Request.QueryCase.CAMERA_CONTROL, sent.getQueryCase());
        assertEquals(1800f, sent.getCameraControl().getX());
        assertEquals(2400f, sent.getCameraControl().getY());
        assertEquals(1.2f, sent.getCameraControl().getZoom(), 1e-6);
        assertEquals(0.5f, sent.getCameraControl().getAngle(), 1e-6);
        assertFalse(sent.getCameraControl().hasPitch(), "what the move does not mention is not sent");
        assertEquals(2000, sent.getCameraControl().getDurationMs());
        assertEquals(CameraEasing.CAMERA_EASING_EASE_IN_OUT, sent.getCameraControl().getEasing());
        answer(sent, Response.newBuilder().setStatus(ResponseStatus.RESPONSE_STATUS_OK).setCameraControl(CameraState.newBuilder().setX(10).setTransitionActive(true)));
        CameraState state = get(future);
        assertEquals(10f, state.getX());
        assertTrue(state.getTransitionActive());
    }

    @Test
    void resetFollowTetherAndUnfollowAreTheirOwnQueries() throws Exception {
        pipe.resetCamera();
        assertTrue(written(0).getCameraControl().getReset());
        assertFalse(written(0).getCameraControl().hasX());

        pipe.followUnit(42, true);
        assertEquals(Request.QueryCase.CAMERA_FOLLOW, written(1).getQueryCase());
        assertEquals(42, written(1).getCameraFollow().getObjectId());
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_FOLLOW, written(1).getCameraFollow().getMode());
        assertTrue(written(1).getCameraFollow().getSnap());

        pipe.tetherUnit(43, 20);
        assertEquals(CameraFollowMode.CAMERA_FOLLOW_TETHER, written(2).getCameraFollow().getMode());
        assertEquals(20f, written(2).getCameraFollow().getTetherDistance());

        pipe.unfollowUnit();
        assertEquals(0, written(3).getCameraFollow().getObjectId());

        pipe.cancelCameraTransition();
        assertTrue(written(4).getCameraControl().getCancelTransition());
    }

    @Test
    void replayAndObserverCommands() throws Exception {
        CompletableFuture<ReplayState> paused = pipe.pauseReplay();
        assertTrue(written(0).getReplayControl().hasPaused() && written(0).getReplayControl().getPaused());
        answer(written(0), Response.newBuilder().setStatus(ResponseStatus.RESPONSE_STATUS_OK).setReplayControl(ReplayState.newBuilder().setPaused(true).setSpeedRatio(1f)));
        assertTrue(get(paused).getPaused());

        pipe.resumeReplay();
        assertFalse(written(1).getReplayControl().getPaused());
        assertTrue(written(1).getReplayControl().hasPaused());

        pipe.setReplaySpeed(2.0);
        assertEquals(2f, written(2).getReplayControl().getSpeedRatio());
        assertFalse(written(2).getReplayControl().hasPaused());

        pipe.setReplayFastForward(true);
        assertTrue(written(3).getReplayControl().getFastForward());

        CompletableFuture<ObserveState> looked = pipe.observePlayer(3);
        assertEquals(3, written(4).getObserve().getPlayerIndex());
        answer(written(4), Response.newBuilder().setStatus(ResponseStatus.RESPONSE_STATUS_OK).setObserve(ObserveState.newBuilder().setPlayerIndex(3).setFullView(true)));
        assertTrue(get(looked).getFullView());

        pipe.observeNobody();
        assertEquals(-1, written(5).getObserve().getPlayerIndex());
    }

    @Test
    void notAllowedIsATypedExceptionThatIsStillARequestFailure() throws Exception {
        CompletableFuture<CameraState> future = pipe.moveCamera(CameraMove.to(1, 2));
        answer(written(0), Response.newBuilder().setStatus(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED).setMessage("the local player is an active player of the match"));
        ExecutionException failure = assertThrows(ExecutionException.class, () -> future.get(10, TimeUnit.SECONDS));
        NotAllowedException refused = assertInstanceOf(NotAllowedException.class, failure.getCause());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED, refused.status());
        assertEquals("the local player is an active player of the match", refused.reason());
        assertInstanceOf(RequestFailedException.class, refused);
    }

    @Test
    void otherStatusesStayPlainRequestFailures() throws Exception {
        CompletableFuture<CameraState> future = pipe.followUnit(99);
        answer(written(0), Response.newBuilder().setStatus(ResponseStatus.RESPONSE_STATUS_NOT_FOUND).setMessage("no such object"));
        ExecutionException failure = assertThrows(ExecutionException.class, () -> future.get(10, TimeUnit.SECONDS));
        assertEquals(RequestFailedException.class, failure.getCause().getClass());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, ((RequestFailedException) failure.getCause()).status());
    }

    @Test
    void badArgumentsAreRefusedBeforeAnythingIsSent() {
        assertThrows(IllegalArgumentException.class, () -> pipe.moveCamera(CameraMove.set()));
        assertThrows(IllegalArgumentException.class, () -> pipe.followUnit(0));
        assertThrows(IllegalArgumentException.class, () -> pipe.tetherUnit(5, 0));
        assertThrows(IllegalArgumentException.class, () -> pipe.tetherUnit(5, 101));
        assertThrows(IllegalArgumentException.class, () -> pipe.setReplaySpeed(0.01));
        assertThrows(IllegalArgumentException.class, () -> pipe.setReplaySpeed(Double.NaN));
        assertThrows(IllegalArgumentException.class, () -> pipe.observePlayer(-1));
        assertThrows(IllegalArgumentException.class, () -> CameraMove.to(Double.NaN, 1));
        assertThrows(IllegalArgumentException.class, () -> CameraMove.set().over(Duration.ofSeconds(61)));
        assertThrows(IllegalArgumentException.class, () -> CameraMove.set().over(Duration.ofMillis(-1)));
        assertTrue(link.written.isEmpty());
    }

    @Test
    void aCameraMoveIsAnImmutableValue() {
        CameraMove base = CameraMove.to(1, 2);
        CameraMove zoomed = base.zoom(2.0);
        assertTrue(base.zoom().isEmpty(), "the base did not change");
        assertEquals(OptionalDouble.of(2.0), zoomed.zoom());
        assertEquals(zoomed, base.zoom(2.0));
        assertEquals(zoomed.hashCode(), base.zoom(2.0).hashCode());
        assertTrue(CameraMove.set().isEmpty());
        assertFalse(CameraMove.reset().isEmpty());
        assertTrue(CameraMove.reset().resets());
        assertEquals(Duration.ZERO, base.transition());
        assertFalse(base.easesInOut());
        assertEquals(CameraEasing.CAMERA_EASING_NONE, base.toQuery().getEasing());
    }
}
