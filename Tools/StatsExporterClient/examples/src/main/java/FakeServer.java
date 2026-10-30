// Plays a recorded dump back as a game, in this process, and reads it with a StatsPipe: the code that reads a live
// game also reads this, so a tool can be written and tested without a game running. The fake game sends a Hello,
// re-sequenced frames paced by game time, and answers requests (catalog, objects, players, the map, feed configuration,
// camera, replay and observer control) from what the dump contains.
//
// To serve other programs instead, leave out namedPipe(false): the fake game then opens a real Windows named pipe named
// like a game's (generalsonline-stats-<pid>), which any client finds. The test client does that with its serve command.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.3.0.jar:protobuf-java-4.33.4.jar FakeServer.java match.dump
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.Handshake;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.Hello;

import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.TimeUnit;

public class FakeServer {
    public static void main(String[] args) throws Exception {
        Path dump = Path.of(args.length > 0 ? args[0] : "match.dump");

        // Four times real time; holdForReader starts the dump when the first reader connects.
        try (FakeGame game = FakeGame.builder(dump).speed(4).holdForReader(true).namedPipe(false).build().start()) {
            MatchState state = new MatchState();
            // connect(...) points the builder at the fake game, through memory; everything else is the usual StatsPipe.
            try (StatsPipe pipe = game.connect(StatsPipe.builder().state(state).reconnect(false)).build().start()) {
                Hello hello = pipe.awaitHello(Duration.ofSeconds(10)).orElseThrow();
                System.out.println("connected: " + Handshake.summary(hello));
                System.out.println("catalog: " + pipe.catalogAll(CatalogQuery.getDefaultInstance()).get(10, TimeUnit.SECONDS).size() + " templates in the dump");

                while (!game.awaitFinished(Duration.ofSeconds(2))) {
                    System.out.println("game time " + state.gameTime(Math.max(0, state.frame())));
                }
                Thread.sleep(300); // let the last frames arrive
                for (PlayerView player : state.players()) {
                    System.out.println(player.name() + " (" + player.side() + ") " + player.stateText() + ", money " + Format.u(player.stats().getMoney()));
                }
            }
        }
    }
}
