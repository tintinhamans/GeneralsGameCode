package com.generalsonline.stats;

import static com.generalsonline.stats.PipeTestSupport.await;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.internal.NamedPipeLink;
import com.generalsonline.stats.internal.PowerShellPipeServer;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.CameraChangeSource;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.MatchPhase;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

/** The fake game over a real Windows named pipe, hosted by PowerShell; skipped on other systems. */
@EnabledOnOs(OS.WINDOWS)
class FakeGamePipeTest {
    @TempDir
    Path dir;

    private static int uniquePid() {
        return 60000 + (int) (System.nanoTime() % 5000);
    }

    @Test
    void servesARealNamedPipeThatDiscoveryFindsAndFourReadersCanUse() throws Exception {
        int pid = uniquePid();
        try (FakeGame game = FakeGame.builder(FakeDumpFixture.write(dir)).pid(pid).speed(1).holdForReader(true).build().start()) {
            assertTrue(PipeDiscovery.findPids().contains(pid), "the pipe is listed in the pipe folder");
            assertEquals("\\\\.\\pipe\\generalsonline-stats-" + pid, game.pipePath());

            List<StatsPipe> pipes = new ArrayList<>();
            List<FakeGameTest.Tape> tapes = new ArrayList<>();
            MatchState first = new MatchState();
            for (int i = 0; i < 4; i++) {
                FakeGameTest.Tape tape = new FakeGameTest.Tape();
                StatsPipe.Builder builder = StatsPipe.builder().pid(pid).reconnect(false).listener(tape);
                if (i == 0) {
                    builder.state(first);
                }
                StatsPipe pipe = builder.build().start();
                pipes.add(pipe);
                tapes.add(tape);
                assertTrue(pipe.awaitHello(Duration.ofSeconds(20)).isPresent(), "reader " + i + " gets a Hello");
            }
            List<Integer> slots = new ArrayList<>();
            for (StatsPipe pipe : pipes) {
                slots.add(pipe.hello().orElseThrow().getReaderSlot());
                assertEquals(pid, pipe.connectedPid().getAsInt());
            }
            slots.sort(null);
            assertEquals(List.of(0, 1, 2, 3), slots);
            assertThrows(IOException.class, () -> NamedPipeLink.open(game.pipePath()).close(), "a fifth reader finds the pipe busy");

            await("play", () -> first.phase().orElse(null) == MatchPhase.MATCH_PHASE_STARTED && first.frame() > 30);
            assertEquals("AmericaTankCrusader", pipes.get(0).object(101).get(10, TimeUnit.SECONDS).getTemplateName());
            assertEquals(3, pipes.get(1).catalogAll(CatalogQuery.getDefaultInstance()).get(10, TimeUnit.SECONDS).stream().filter(e -> e.getName().startsWith("America")).count(), "the crusader, the barracks and the hero");

            pipes.get(0).moveCamera(CameraMove.to(2500, 2600)).get(10, TimeUnit.SECONDS);
            await("the camera frame reaches every reader", () -> tapes.stream().allMatch(t -> {
                Camera c = t.lastCamera();
                return c != null && c.getX() == 2500f && c.getChangeSource() == CameraChangeSource.CAMERA_CHANGE_PIPE;
            }));

            for (StatsPipe pipe : pipes) {
                assertEquals(0, pipe.droppedFrames());
            }
            pipes.get(2).close();
            await("the slot is free", () -> game.readerCount() == 3);
            NamedPipeLink.open(game.pipePath()).close();
            for (StatsPipe pipe : pipes) {
                pipe.close();
            }
        }
        assertFalse(PipeDiscovery.findPids().contains(pid), "closing the game removes the pipe");
    }

    @Test
    void aPipeNameOfYourOwnIsNotDiscoveredButConnects() throws Exception {
        String name = "fakegame-test-" + uniquePid();
        try (FakeGame game = FakeGame.builder(FakeDumpFixture.write(dir)).pipe(name).speed(0).holdForReader(true).build().start()) {
            assertEquals("\\\\.\\pipe\\" + name, game.pipePath());
            MatchState state = new MatchState();
            try (StatsPipe pipe = StatsPipe.builder().pipe(game.pipePath()).state(state).reconnect(false).build().start()) {
                assertTrue(pipe.awaitHello(Duration.ofSeconds(20)).isPresent());
                await("the end of the match", () -> state.phase().orElse(null) == MatchPhase.MATCH_PHASE_ENDED);
                assertEquals(0, pipe.droppedFrames());
                assertEquals(FakeDumpFixture.LAST_FRAME, state.frame());
            }
        }
    }

    @Test
    void theHelperRefusesABadNameAndNeedsWindows() {
        assertThrows(IllegalArgumentException.class, () -> new PowerShellPipeServer("a b", 4));
        assertThrows(IllegalArgumentException.class, () -> new PowerShellPipeServer("ok", 0));
        assertTrue(PowerShellPipeServer.isAvailable());
    }
}
