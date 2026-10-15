package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static com.generalsonline.stats.PipeTestSupport.fake;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.FeedConfig;
import com.generalsonline.stats.proto.FeedConfigQuery;
import com.generalsonline.stats.proto.Hello;
import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.ObjectDetail;
import com.generalsonline.stats.proto.ObjectRow;
import com.generalsonline.stats.proto.ObjectsQuery;
import com.generalsonline.stats.proto.ObjectsResult;
import com.generalsonline.stats.proto.PageInfo;
import com.generalsonline.stats.proto.PageRequest;
import com.generalsonline.stats.proto.PlayerDetail;
import com.generalsonline.stats.proto.Request;
import com.generalsonline.stats.proto.ResyncResult;
import com.generalsonline.stats.proto.Response;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.TemplateRef;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.UnaryOperator;

/** The request API of StatsPipe on a fake pipe: matching by id, statuses, timeouts, disconnects, typed results and paging. */
class RequestTest {
    private static final Duration LONG = Duration.ofSeconds(10);

    /** A connected pipe whose Hello arrived, and a way to answer what it wrote. */
    private static final class Rig implements AutoCloseable {
        final PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        final RecordingListener listener = new RecordingListener();
        final StatsPipe pipe;
        private long nextSequence = 1;

        Rig() throws Exception {
            this(TestMessages.hello(), b -> b);
        }

        Rig(Hello hello, UnaryOperator<StatsPipe.Builder> configure) throws Exception {
            pipe = configure.apply(fake(link).listener(listener).reconnect(false)).build().start();
            link.give(TestMessages.outbound(hello, 0));
            await("the Hello", () -> pipe.hello().isPresent());
        }

        /** Waits until n requests were written and returns the n-th (0-based). */
        Request written(int index) throws Exception {
            await("request " + index, () -> link.written.size() > index);
            return link.requests().get(index);
        }

        void respond(Response response) {
            link.give(TestMessages.outbound(response, nextSequence++));
        }

        void respondOk(Request request, Response.Builder result) {
            respond(result.setId(request.getId()).setStatus(ResponseStatus.RESPONSE_STATUS_OK).build());
        }

        @Override
        public void close() {
            pipe.close();
        }
    }

    private static Request.Builder catalogQuery(String name) {
        return Request.newBuilder().setCatalog(CatalogQuery.newBuilder().setName(name));
    }

    private static <T> T get(CompletableFuture<T> future) throws Exception {
        return future.get(10, TimeUnit.SECONDS);
    }

    private static Throwable failureOf(CompletableFuture<?> future) throws Exception {
        ExecutionException failure = assertThrows(ExecutionException.class, () -> future.get(10, TimeUnit.SECONDS));
        return failure.getCause();
    }

    // -- Sending.

    @Test
    void aRequestIsAnInboundFrameOfType0x11WithAnIdTheLibraryChose() throws Exception {
        try (Rig rig = new Rig()) {
            rig.pipe.request(catalogQuery("A").setId(99), LONG);
            byte[] bytes = rig.link.written.get(0);
            assertEquals(0x11, bytes[4]);
            Request sent = rig.written(0);
            assertNotEquals(0, sent.getId());
            assertNotEquals(99, sent.getId(), "an id in the request is replaced");
            assertEquals("A", sent.getCatalog().getName());
        }
    }

    @Test
    void requestsBeforeTheHelloAreRefused() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink();
        try (StatsPipe pipe = fake(link).build().start()) {
            await("connection", pipe::isConnected);
            CompletableFuture<Response> early = pipe.request(catalogQuery("A"), LONG);
            assertInstanceOf(IllegalStateException.class, failureOf(early));
            assertTrue(link.written.isEmpty(), "nothing was written before the Hello");
        }
    }

    @Test
    void requestsWhileNotConnectedFailWithAnIoException() throws Exception {
        StatsPipe idle = fake().build();
        assertInstanceOf(IOException.class, failureOf(idle.request(catalogQuery("A"), LONG)));
        idle.close();
        assertInstanceOf(IOException.class, failureOf(idle.request(catalogQuery("A"), LONG)));
    }

    @Test
    void badArgumentsThrowAtOnce() throws Exception {
        try (Rig rig = new Rig()) {
            assertThrows(IllegalArgumentException.class, () -> rig.pipe.request(Request.newBuilder(), LONG), "no query");
            assertThrows(IllegalArgumentException.class, () -> rig.pipe.request(catalogQuery("A"), Duration.ZERO));
            assertThrows(NullPointerException.class, () -> rig.pipe.request((Request) null, LONG));
            String tooBig = assertThrows(IllegalArgumentException.class, () -> rig.pipe.catalog(CatalogQuery.newBuilder().setName("x".repeat(5000)).build())).getMessage();
            assertTrue(tooBig.contains("4096"), tooBig);
            assertTrue(rig.link.written.isEmpty());
            assertEquals(0, rig.pipe.pendingRequests());
        }
    }

    @Test
    void idsCountUpSkipZeroOnWrapAndNeverRepeatInFlight() throws Exception {
        try (Rig rig = new Rig()) {
            rig.pipe.setLastRequestId(-3);
            for (int i = 0; i < 5; i++) {
                rig.pipe.request(catalogQuery("A"), LONG);
            }
            // 0xFFFFFFFE, 0xFFFFFFFF, then 0 is skipped: 1, 2, 3
            assertEquals(List.of(-2, -1, 1, 2, 3), rig.link.requests().stream().map(Request::getId).toList());
        }
    }

    // -- Matching.

    @Test
    void responsesAreMatchedByIdWhateverTheirOrder() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> a = rig.pipe.request(catalogQuery("A"), LONG);
            CompletableFuture<Response> b = rig.pipe.request(catalogQuery("B"), LONG);
            CompletableFuture<Response> c = rig.pipe.request(catalogQuery("C"), LONG);
            Request ra = rig.written(0);
            Request rb = rig.written(1);
            Request rc = rig.written(2);
            assertEquals(3, rig.pipe.pendingRequests());

            rig.respond(Response.newBuilder().setId(rc.getId()).setMessage("for C").build());
            rig.respond(Response.newBuilder().setId(ra.getId()).setMessage("for A").build());
            assertEquals("for C", get(c).getMessage());
            assertEquals("for A", get(a).getMessage());
            assertFalse(b.isDone(), "B is still waiting");
            rig.respond(Response.newBuilder().setId(rb.getId()).setMessage("for B").build());
            assertEquals("for B", get(b).getMessage());
            assertEquals(0, rig.pipe.pendingRequests());
        }
    }

    @Test
    void anUnknownIdIsIgnoredButStillReachesTheListener() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> waiting = rig.pipe.request(catalogQuery("A"), LONG);
            Request sent = rig.written(0);
            rig.respond(Response.newBuilder().setId(sent.getId() + 1000).setMessage("nobody asked").build());
            await("the unsolicited response", () -> rig.listener.messages.size() == 2);
            assertFalse(waiting.isDone());
            assertEquals(1, rig.pipe.pendingRequests());
            rig.respond(Response.newBuilder().setId(sent.getId()).setMessage("mine").build());
            assertEquals("mine", get(waiting).getMessage());
        }
    }

    @Test
    void aResponseIsDeliveredOnlyOnceAndAnAnsweredIdCanBeSeenAgainWithoutEffect() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> waiting = rig.pipe.request(catalogQuery("A"), LONG);
            Request sent = rig.written(0);
            rig.respond(Response.newBuilder().setId(sent.getId()).setMessage("first").build());
            rig.respond(Response.newBuilder().setId(sent.getId()).setMessage("second").build());
            assertEquals("first", get(waiting).getMessage());
            await("both responses", () -> rig.listener.messages.size() == 3);
        }
    }

    // -- Timeouts and disconnects.

    @Test
    void aRequestWithoutAResponseTimesOutAndALateResponseIsIgnored() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> late = rig.pipe.request(catalogQuery("A"), Duration.ofMillis(50));
            Request sent = rig.written(0);
            Throwable failure = failureOf(late);
            RequestTimeoutException timeout = assertInstanceOf(RequestTimeoutException.class, failure);
            assertInstanceOf(TimeoutException.class, timeout);
            assertTrue(timeout.getMessage().contains("50 ms"), timeout.getMessage());
            assertEquals(0, rig.pipe.pendingRequests(), "the pending entry was cleaned up");

            rig.respond(Response.newBuilder().setId(sent.getId()).build());
            await("the late response", () -> rig.listener.messages.size() == 2);
            assertTrue(late.isCompletedExceptionally(), "a late response changes nothing");
            assertTrue(rig.pipe.isConnected());
        }
    }

    @Test
    void anAnsweredRequestCancelsItsTimeout() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> answered = rig.pipe.request(catalogQuery("A"), Duration.ofMillis(100));
            rig.respond(Response.newBuilder().setId(rig.written(0).getId()).build());
            get(answered);
            Thread.sleep(250);
            assertFalse(answered.isCompletedExceptionally());
        }
    }

    @Test
    void aLostConnectionFailsEveryPendingRequest() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> a = rig.pipe.request(catalogQuery("A"), LONG);
            CompletableFuture<Response> b = rig.pipe.request(catalogQuery("B"), LONG);
            rig.written(1);
            rig.link.endOfStream();
            assertInstanceOf(IOException.class, failureOf(a));
            Throwable failure = failureOf(b);
            assertInstanceOf(IOException.class, failure);
            assertTrue(failure.getMessage().contains("ended"), failure.getMessage());
            assertEquals(0, rig.pipe.pendingRequests());
            assertInstanceOf(IOException.class, failureOf(rig.pipe.request(catalogQuery("C"), LONG)), "and nothing can be sent afterwards");
        }
    }

    @Test
    void closingThePipeFailsPendingRequests() throws Exception {
        Rig rig = new Rig();
        CompletableFuture<Response> waiting = rig.pipe.request(catalogQuery("A"), LONG);
        rig.written(0);
        rig.close();
        Throwable failure = failureOf(waiting);
        assertInstanceOf(IOException.class, failure);
        assertTrue(failure.getMessage().contains("closed"), failure.getMessage());
    }

    @Test
    void aFailingWriteFailsOnlyThatRequest() throws Exception {
        PipeTestSupport.FakeLink link = new PipeTestSupport.FakeLink() {
            @Override
            public void write(java.nio.ByteBuffer buffer) throws IOException {
                throw new IOException("broken pipe");
            }
        };
        StatsPipe pipe = fake(link).reconnect(false).build().start();
        try {
            link.give(TestMessages.outbound(TestMessages.hello(), 0));
            await("the Hello", () -> pipe.hello().isPresent());
            Throwable failure = failureOf(pipe.request(catalogQuery("A"), LONG));
            assertEquals("broken pipe", failure.getMessage());
            assertEquals(0, pipe.pendingRequests());
        } finally {
            pipe.close();
        }
    }

    // -- Statuses.

    @Test
    void aStatusOtherThanOkIsAResponseForTheCoreMethodAndAnExceptionForTheTypedOnes() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<Response> raw = rig.pipe.request(Request.newBuilder().setObject(com.generalsonline.stats.proto.ObjectQuery.newBuilder().setObjectId(5)), LONG);
            rig.respond(Response.newBuilder().setId(rig.written(0).getId()).setStatus(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED).setMessage("slow down").build());
            Response limited = get(raw);
            assertEquals(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, limited.getStatus());
            assertEquals("slow down", limited.getMessage());

            CompletableFuture<ObjectDetail> typed = rig.pipe.object(5);
            rig.respond(Response.newBuilder().setId(rig.written(1).getId()).setStatus(ResponseStatus.RESPONSE_STATUS_NOT_FOUND).setMessage("no object 5").build());
            RequestFailedException failure = assertInstanceOf(RequestFailedException.class, failureOf(typed));
            assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure.status());
            assertTrue(failure.getMessage().contains("NOT_FOUND") && failure.getMessage().contains("no object 5"), failure.getMessage());
        }
    }

    @Test
    void everyNonOkStatusSurfacesAsItsOwnException() throws Exception {
        try (Rig rig = new Rig()) {
            int index = 0;
            for (ResponseStatus status : List.of(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, ResponseStatus.RESPONSE_STATUS_NOT_FOUND,
                ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, ResponseStatus.RESPONSE_STATUS_TOO_LARGE)) {
                CompletableFuture<MapDetail> map = rig.pipe.map(false);
                rig.respond(Response.newBuilder().setId(rig.written(index++).getId()).setStatus(status).build());
                assertEquals(status, assertInstanceOf(RequestFailedException.class, failureOf(map)).status());
            }
        }
    }

    @Test
    void aSuccessfulResponseWithTheWrongResultIsAProtocolError() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<ObjectDetail> object = rig.pipe.object(1);
            rig.respondOk(rig.written(0), Response.newBuilder().setCatalog(CatalogResult.getDefaultInstance()));
            Throwable failure = failureOf(object);
            assertInstanceOf(IOException.class, failure);
            assertTrue(failure.getMessage().contains("CATALOG"), failure.getMessage());
        }
    }

    // -- The typed methods.

    @Test
    void theTypedMethodsSendTheRightQueryAndReturnTheResult() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<CatalogResult> catalog = rig.pipe.catalog(CatalogQuery.newBuilder().setSide("GLA").setBuildableOnly(true).build());
            Request catalogRequest = rig.written(0);
            assertEquals("GLA", catalogRequest.getCatalog().getSide());
            assertTrue(catalogRequest.getCatalog().getBuildableOnly());
            rig.respondOk(catalogRequest, Response.newBuilder().setCatalog(TestMessages.response().getCatalog()));
            assertEquals("AmericaTankCrusader", get(catalog).getEntries(0).getName());

            CompletableFuture<ObjectDetail> object = rig.pipe.object(77);
            rig.respondOk(rig.written(1), Response.newBuilder().setObject(ObjectDetail.newBuilder().setObjectId(77).setTemplateName("ChinaTankGattling")));
            assertEquals(77, rig.link.requests().get(1).getObject().getObjectId());
            assertEquals("ChinaTankGattling", get(object).getTemplateName());

            CompletableFuture<PlayerDetail> player = rig.pipe.player(3);
            rig.respondOk(rig.written(2), Response.newBuilder().setPlayer(PlayerDetail.newBuilder().setPlayerIndex(3).setMoney(1234)));
            assertEquals(3, rig.link.requests().get(2).getPlayer().getPlayerIndex());
            assertEquals(1234, get(player).getMoney());

            CompletableFuture<MapDetail> map = rig.pipe.map(true);
            rig.respondOk(rig.written(3), Response.newBuilder().setMap(MapDetail.newBuilder().setMapName("maps\\x")));
            assertTrue(rig.link.requests().get(3).getMap().getIncludeMinimap());
            assertEquals("maps\\x", get(map).getMapName());

            CompletableFuture<ObjectsResult> objects = rig.pipe.objects(ObjectsQuery.newBuilder().setFilterPlayer(true).setPlayerIndex(2).build());
            rig.respondOk(rig.written(4), Response.newBuilder().setObjects(ObjectsResult.newBuilder().addRows(ObjectRow.newBuilder().setObjectId(9))));
            assertEquals(2, rig.link.requests().get(4).getObjects().getPlayerIndex());
            assertEquals(9, get(objects).getRows(0).getObjectId());
        }
    }

    @Test
    void resyncAndTheFeedConfiguration() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<ResyncResult> resync = rig.pipe.resync();
            Request resyncRequest = rig.written(0);
            assertEquals(Request.QueryCase.RESYNC, resyncRequest.getQueryCase());
            rig.respondOk(resyncRequest, Response.newBuilder().setResync(ResyncResult.getDefaultInstance()));
            assertEquals(ResyncResult.getDefaultInstance(), get(resync));

            CompletableFuture<FeedConfig> read = rig.pipe.feedConfig();
            Request readRequest = rig.written(1);
            assertEquals(Request.QueryCase.FEED_CONFIG, readRequest.getQueryCase());
            assertEquals(FeedConfigQuery.getDefaultInstance(), readRequest.getFeedConfig(), "reading changes nothing");
            rig.respondOk(readRequest, Response.newBuilder().setFeedConfig(FeedConfig.newBuilder().setLightIntervalMs(100).setEvents(true)));
            assertEquals(100, get(read).getLightIntervalMs());

            CompletableFuture<FeedConfig> change = rig.pipe.setFeedConfig(FeedConfigQuery.newBuilder().setCamera(false).setLightIntervalMs(200).build());
            Request changeRequest = rig.written(2);
            assertTrue(changeRequest.getFeedConfig().hasCamera());
            assertFalse(changeRequest.getFeedConfig().getCamera());
            assertFalse(changeRequest.getFeedConfig().hasEvents(), "only the fields that were set travel");
            rig.respondOk(changeRequest, Response.newBuilder().setFeedConfig(FeedConfig.newBuilder().setLightIntervalMs(200).setCamera(false)));
            assertFalse(get(change).getCamera());
        }
    }

    // -- Paging.

    private static Response.Builder catalogPage(boolean more, int next, String... names) {
        CatalogResult.Builder page = CatalogResult.newBuilder().setPage(PageInfo.newBuilder().setHasMore(more).setNextCursor(next));
        for (String name : names) {
            page.addEntries(CatalogEntry.newBuilder().setName(name));
        }
        return Response.newBuilder().setCatalog(page);
    }

    @Test
    void thePagingHelperFollowsTheCursorUntilThereIsNoMore() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<List<CatalogEntry>> all = rig.pipe.catalogAll(CatalogQuery.newBuilder().setSide("GLA").setPage(PageRequest.newBuilder().setPageSize(2)).build());
            Request first = rig.written(0);
            assertEquals(0, first.getCatalog().getPage().getCursor());
            assertEquals(2, first.getCatalog().getPage().getPageSize());
            rig.respondOk(first, catalogPage(true, 2, "a", "b"));

            Request second = rig.written(1);
            assertEquals(2, second.getCatalog().getPage().getCursor(), "the next cursor of the first page");
            assertEquals(2, second.getCatalog().getPage().getPageSize(), "the page size and the filters carry over");
            assertEquals("GLA", second.getCatalog().getSide());
            rig.respondOk(second, catalogPage(true, 4, "c", "d"));

            Request third = rig.written(2);
            assertEquals(4, third.getCatalog().getPage().getCursor());
            assertFalse(all.isDone());
            rig.respondOk(third, catalogPage(false, 0, "e"));

            assertEquals(List.of("a", "b", "c", "d", "e"), get(all).stream().map(CatalogEntry::getName).toList());
            assertEquals(3, rig.link.requests().size(), "no request after the last page");
        }
    }

    @Test
    void aSinglePageIsOneRequest() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<List<CatalogEntry>> all = rig.pipe.catalogAll(CatalogQuery.getDefaultInstance());
            rig.respondOk(rig.written(0), catalogPage(false, 0, "only"));
            assertEquals(1, get(all).size());
            assertEquals(1, rig.link.requests().size());
        }
    }

    @Test
    void thePagingHelperStopsAtItsPageLimit() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<List<CatalogEntry>> all = rig.pipe.catalogAll(CatalogQuery.getDefaultInstance(), 2);
            rig.respondOk(rig.written(0), catalogPage(true, 1, "a"));
            rig.respondOk(rig.written(1), catalogPage(true, 2, "b"));
            PageLimitException stopped = assertInstanceOf(PageLimitException.class, failureOf(all));
            assertEquals(2, stopped.pages());
            assertEquals(2, rig.link.requests().size());
            assertThrows(IllegalArgumentException.class, () -> rig.pipe.catalogAll(CatalogQuery.getDefaultInstance(), 0));
        }
    }

    @Test
    void aFailingPageFailsThePagingHelperWithTheRealCause() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<List<CatalogEntry>> all = rig.pipe.catalogAll(CatalogQuery.getDefaultInstance());
            rig.respondOk(rig.written(0), catalogPage(true, 1, "a"));
            rig.respond(Response.newBuilder().setId(rig.written(1).getId()).setStatus(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE).setMessage("no match").build());
            RequestFailedException failure = assertInstanceOf(RequestFailedException.class, failureOf(all));
            assertEquals(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, failure.status());
        }
    }

    @Test
    void theObjectsPagingHelperMergesRowsAndTemplates() throws Exception {
        try (Rig rig = new Rig()) {
            CompletableFuture<ObjectsResult> all = rig.pipe.objectsAll(ObjectsQuery.newBuilder().setPage(PageRequest.newBuilder().setCursor(10)).build());
            Request first = rig.written(0);
            assertEquals(10, first.getObjects().getPage().getCursor(), "the query's own cursor is where it starts");
            rig.respondOk(first, Response.newBuilder().setObjects(ObjectsResult.newBuilder()
                .addRows(ObjectRow.newBuilder().setObjectId(10).setTemplateId(1)).addRows(ObjectRow.newBuilder().setObjectId(11).setTemplateId(2))
                .addTemplates(TemplateRef.newBuilder().setTemplateId(1).setName("A")).addTemplates(TemplateRef.newBuilder().setTemplateId(2).setName("B"))
                .setPage(PageInfo.newBuilder().setHasMore(true).setNextCursor(12))));
            Request second = rig.written(1);
            assertEquals(12, second.getObjects().getPage().getCursor());
            rig.respondOk(second, Response.newBuilder().setObjects(ObjectsResult.newBuilder()
                .addRows(ObjectRow.newBuilder().setObjectId(12).setTemplateId(1))
                .addTemplates(TemplateRef.newBuilder().setTemplateId(1).setName("A"))
                .setPage(PageInfo.newBuilder().setTotal(3))));

            ObjectsResult merged = get(all);
            assertEquals(List.of(10, 11, 12), merged.getRowsList().stream().map(ObjectRow::getObjectId).toList());
            assertEquals(List.of("A", "B"), merged.getTemplatesList().stream().map(TemplateRef::getName).toList(), "each template once");
            assertFalse(merged.getPage().getHasMore());
            assertEquals(3, merged.getPage().getTotal());
        }
    }

    // -- The client-side limiter.

    private static Hello limitedHello() {
        return TestMessages.hello().toBuilder().setMaxRequestsPerSecond(1).setMaxRequestBurst(2).build();
    }

    @Test
    void withoutTheClientLimiterEveryRequestIsSent() throws Exception {
        try (Rig rig = new Rig(limitedHello(), b -> b)) {
            for (int i = 0; i < 5; i++) {
                rig.pipe.request(catalogQuery("A"), LONG);
            }
            assertEquals(5, rig.link.requests().size(), "limiting is the game's job unless asked for");
        }
    }

    @Test
    void theClientLimiterAnswersTheExcessWithRateLimitedAndSendsNothing() throws Exception {
        try (Rig rig = new Rig(limitedHello(), b -> b.limitRequests(true))) {
            CompletableFuture<Response> first = rig.pipe.request(catalogQuery("A"), LONG);
            CompletableFuture<Response> second = rig.pipe.request(catalogQuery("B"), LONG);
            CompletableFuture<Response> third = rig.pipe.request(catalogQuery("C"), LONG);
            assertTrue(third.isDone(), "answered at once");
            assertEquals(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, get(third).getStatus());
            assertEquals("client-side rate limit", get(third).getMessage());
            assertEquals(2, rig.link.requests().size(), "only the burst went out");
            assertFalse(first.isDone() || second.isDone());

            CompletableFuture<ObjectDetail> typed = rig.pipe.object(1);
            assertEquals(ResponseStatus.RESPONSE_STATUS_RATE_LIMITED, assertInstanceOf(RequestFailedException.class, failureOf(typed)).status());
            assertEquals(2, rig.link.requests().size());
        }
    }

    @Test
    void aHelloWithoutARateMeansNoClientLimit() throws Exception {
        Hello unlimited = TestMessages.hello().toBuilder().setMaxRequestsPerSecond(0).setMaxRequestBurst(0).build();
        try (Rig rig = new Rig(unlimited, b -> b.limitRequests(true))) {
            for (int i = 0; i < 30; i++) {
                rig.pipe.request(catalogQuery("A"), LONG);
            }
            assertEquals(30, rig.link.requests().size());
        }
    }

    @Test
    void theDefaultRequestTimeoutOfTheTypedMethodsIsConfigurable() throws Exception {
        try (Rig rig = new Rig(TestMessages.hello(), b -> b.requestTimeout(Duration.ofMillis(40)))) {
            assertInstanceOf(RequestTimeoutException.class, failureOf(rig.pipe.player(1)));
        }
        assertEquals(Duration.ofSeconds(5), StatsPipe.DEFAULT_REQUEST_TIMEOUT);
    }
}
