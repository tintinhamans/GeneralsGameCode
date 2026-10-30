package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.LobbyChangeRoute;
import com.generalsonline.stats.proto.LobbyControlQuery;
import com.generalsonline.stats.proto.LobbyControlResult;
import com.generalsonline.stats.proto.LobbyKind;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.OnlineMatch;
import com.generalsonline.stats.proto.ResponseStatus;
import com.generalsonline.stats.proto.UiScreen;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.Message;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

/** Lobby control against the fake game: the host and client rules of the setup screens, and the LobbyState that follows a change. */
class LobbyControlTest {
    @TempDir
    Path dir;

    private final List<AutoCloseable> open = new ArrayList<>();

    @AfterEach
    void closeEverything() throws Exception {
        for (AutoCloseable c : open) {
            c.close();
        }
    }

    private static LobbySlot human(int slot, String name, boolean host, String side, int color) {
        return LobbySlot.newBuilder().setState(LobbySlotState.LOBBY_SLOT_HUMAN).setIsHost(host).setStartPosition(-1)
            .setPlayer(MatchPlayer.newBuilder().setLobbySlot(slot).setPlayerIndex(-1).setName(name).setIsHuman(true).setSide(side).setColor(color).setTeam(-1).setStartPosition(-1))
            .build();
    }

    private static LobbyState lobby(LobbyKind kind) {
        LobbyState.Builder b = LobbyState.newBuilder().setInLobby(true).setKind(kind).setMapName("maps\\tournament desert\\tournament desert.map")
            .addSlots(human(0, "Alice", true, "America", 0xFF0000FF)).addSlots(human(1, "Bob", false, "China", 0xFFFF0000));
        for (int i = 2; i < 8; i++) {
            b.addSlots(LobbySlot.newBuilder().setStartPosition(-1));
        }
        if (kind == LobbyKind.LOBBY_KIND_ONLINE) {
            b.setOnline(OnlineMatch.newBuilder().setStartingCash(10000));
        }
        return b.build();
    }

    private Path writeDump(LobbyState lobby) throws IOException {
        Path file = dir.resolve("lobby-" + lobby.getKind() + ".dump");
        try (DumpWriter writer = new DumpWriter(file)) {
            long n = 0;
            for (Message m : new Message[] {TestMessages.hello(), UiState.newBuilder().setScreen(UiScreen.UI_SCREEN_LAN_GAME_SETUP).build(), lobby}) {
                writer.write(new RawFrame(TestMessages.frameTypeOf(m), n, n * 1000, m.toByteArray()));
                n++;
            }
        }
        return file;
    }

    private StatsPipe connect(LobbyState lobby, int localSlot, MatchState state) throws Exception {
        FakeGame game = FakeGame.builder(writeDump(lobby)).namedPipe(false).pid(5160).speed(0).holdForReader(true).lobbyLocalSlot(localSlot).build().start();
        open.add(game);
        StatsPipe pipe = game.connect(StatsPipe.builder()).reconnect(false).state(state).build().start();
        open.add(pipe);
        assertTrue(pipe.awaitHello(Duration.ofSeconds(10)).isPresent());
        await("the lobby", () -> state.lobby().isPresent());
        return pipe;
    }

    private static <T> T get(CompletableFuture<T> future) throws Exception {
        return future.get(10, TimeUnit.SECONDS);
    }

    private static RequestFailedException failure(CompletableFuture<?> future) throws Exception {
        ExecutionException e = assertThrows(ExecutionException.class, () -> future.get(10, TimeUnit.SECONDS));
        return assertInstanceOf(RequestFailedException.class, e.getCause());
    }

    @Test
    void theHostChangesSlotsAndRulesAndTheLobbyStateFollows() throws Exception {
        MatchState state = new MatchState();
        StatsPipe pipe = connect(lobby(LobbyKind.LOBBY_KIND_LAN), -1, state);

        LobbyControlResult result = get(pipe.addAi(2, LobbyControl.AiLevel.MEDIUM));
        assertEquals(LobbyKind.LOBBY_KIND_LAN, result.getKind());
        assertEquals(LobbyChangeRoute.LOBBY_CHANGE_APPLIED, result.getRoute(), "a LAN host changes the lobby at once");
        await("the AI", () -> state.lobby().orElseThrow().getSlots(2).getState() == LobbySlotState.LOBBY_SLOT_AI_MEDIUM);

        get(pipe.setFaction(2, "china"));
        get(pipe.setColor(2, 0xFF00FF00));
        get(pipe.setTeam(2, 1));
        get(pipe.setStartPosition(2, 3));
        get(pipe.setFaction(0, "GLA"));
        await("the changes", () -> {
            LobbyState l = state.lobby().orElseThrow();
            MatchPlayer ai = l.getSlots(2).getPlayer();
            return ai.getSide().equals("China") && ai.getColor() == 0xFF00FF00 && ai.getTeam() == 1 && l.getSlots(2).getStartPosition() == 3
                && l.getSlots(0).getPlayer().getSide().equals("GLA");
        });

        get(pipe.changeMap("maps\\fixture ridge\\fixture ridge.map"));
        await("the map", () -> state.lobby().orElseThrow().getMapName().equals("maps\\fixture ridge\\fixture ridge.map"));
        assertEquals(-1, state.lobby().orElseThrow().getSlots(2).getStartPosition(), "a new map clears the start positions");

        get(pipe.closeSlot(3));
        get(pipe.kickPlayer(1, false));
        await("the kick", () -> state.lobby().orElseThrow().getSlots(1).getState() == LobbySlotState.LOBBY_SLOT_OPEN && !state.lobby().orElseThrow().getSlots(1).hasPlayer());
        assertEquals(LobbySlotState.LOBBY_SLOT_CLOSED, state.lobby().orElseThrow().getSlots(3).getState());

        LobbyControlResult start = get(pipe.startGame());
        assertTrue(start.getNote().contains("Start"), "a LAN Start says what it does with unready players");
    }

    @Test
    void theScreensRulesRefuseWhatTheUserCouldNotDo() throws Exception {
        MatchState state = new MatchState();
        StatsPipe pipe = connect(lobby(LobbyKind.LOBBY_KIND_SKIRMISH), -1, state);

        ExecutionException human = assertThrows(ExecutionException.class, () -> pipe.addAi(1, LobbyControl.AiLevel.EASY).get(10, TimeUnit.SECONDS));
        assertInstanceOf(NotAllowedException.class, human.getCause());
        assertTrue(((NotAllowedException) human.getCause()).reason().contains("kick"));
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.openSlot(0).get(10, TimeUnit.SECONDS)).getCause(), "the host's own slot");
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.setColor(1, 0xFF0000FF).get(10, TimeUnit.SECONDS)).getCause(),
            "a colour another slot holds, and a human's slot that is not yours");
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.acceptLobby().get(10, TimeUnit.SECONDS)).getCause(), "the host has no accept button");

        get(pipe.addAi(2, LobbyControl.AiLevel.BRUTAL));
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.setColor(2, 0xFF0000FF).get(10, TimeUnit.SECONDS)).getCause());
        get(pipe.setStartPosition(2, 1));
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.setStartPosition(0, 1).get(10, TimeUnit.SECONDS)).getCause(), "a start position is held");
        get(pipe.setStartPosition(0, 0));
        get(pipe.setLobbyGameSpeed(30));

        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.setStartingCash(1234)).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.setFaction(2, "Atlantis")).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, failure(pipe.changeMap("not a map")).status());
        LobbyControlQuery human5 = LobbyControlQuery.newBuilder().setLobbySlot(2).setSetSlotState(LobbySlotState.LOBBY_SLOT_HUMAN).build();
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.lobbyControl(human5)).status());
        assertEquals(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, failure(pipe.lobbyControl(LobbyControlQuery.getDefaultInstance())).status(), "no action");
        assertThrows(IllegalArgumentException.class, () -> LobbyControl.openSlot(8));
        assertThrows(IllegalArgumentException.class, () -> LobbyControl.gameSpeed(10));
    }

    @Test
    void aClientChangesOnlyItsOwnSlotAndAccepts() throws Exception {
        MatchState state = new MatchState();
        StatsPipe pipe = connect(lobby(LobbyKind.LOBBY_KIND_LAN), 1, state);

        LobbyControlResult result = get(pipe.setFaction(1, "GLA"));
        assertEquals(LobbyChangeRoute.LOBBY_CHANGE_REQUESTED_FROM_HOST, result.getRoute());
        await("own faction", () -> state.lobby().orElseThrow().getSlots(1).getPlayer().getSide().equals("GLA"));
        get(pipe.setTeam(1, 0));
        for (CompletableFuture<?> refused : List.of(pipe.setFaction(0, "China"), pipe.addAi(2, LobbyControl.AiLevel.EASY), pipe.changeMap("maps\\x\\x.map"),
            pipe.setStartingCash(5000), pipe.limitSuperweapons(true), pipe.startGame(), pipe.kickPlayer(0, false))) {
            assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> refused.get(10, TimeUnit.SECONDS)).getCause());
        }

        get(pipe.acceptLobby());
        await("accepted", () -> state.lobby().orElseThrow().getSlots(1).getIsReady());
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.acceptLobby().get(10, TimeUnit.SECONDS)).getCause(), "already accepted");
    }

    @Test
    void anOnlineLobbyRequestsFromTheServiceAndKeepsItsRules() throws Exception {
        MatchState state = new MatchState();
        StatsPipe pipe = connect(lobby(LobbyKind.LOBBY_KIND_ONLINE), -1, state);

        assertEquals(LobbyChangeRoute.LOBBY_CHANGE_REQUESTED_FROM_SERVICE, get(pipe.setStartingCash(20000)).getRoute());
        get(pipe.limitSuperweapons(true));
        await("the rules", () -> state.lobby().orElseThrow().getOnline().getStartingCash() == 20000 && state.lobby().orElseThrow().getOnline().getLimitSuperweapons());
        assertInstanceOf(NotAllowedException.class, assertThrows(ExecutionException.class, () -> pipe.setLobbyGameSpeed(30).get(10, TimeUnit.SECONDS)).getCause(), "only skirmish has the slider");
    }

    @Test
    void outsideALobbyTheGameAnswersUnavailable() throws Exception {
        FakeGame game = FakeGame.builder(FakeDumpFixture.write(dir)).namedPipe(false).pid(5161).speed(0).holdForReader(true).build().start();
        open.add(game);
        MatchState state = new MatchState();
        StatsPipe pipe = game.connect(StatsPipe.builder()).reconnect(false).state(state).build().start();
        open.add(pipe);
        assertTrue(pipe.awaitHello(Duration.ofSeconds(10)).isPresent());
        await("the end of the match", () -> game.isFinished());
        assertEquals(ResponseStatus.RESPONSE_STATUS_UNAVAILABLE, failure(pipe.acceptLobby()).status());
        assertFalse(state.lobby().isPresent());
        assertTrue(state.hasCapability("lobby.control"));
    }
}
