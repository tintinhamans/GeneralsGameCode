// Follows one player's "Money" during a match: prints a line every time the value changes.
// The value is Format.u(player.stats().getMoney()) (uint32 fields come as Java ints; Format.u reads them as unsigned).
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.0.0.jar:protobuf-java-4.33.4.jar TrackPlayerStat.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsPipe;

import java.util.List;

public class TrackPlayerStat {
    // The player to follow, by the name the lobby shows.
    private static final String PLAYER = "Alice";

    public static void main(String[] args) throws InterruptedException {
        // MatchState joins every snapshot with the MatchInfo of the player, and keeps the heavy fields of the
        // last full snapshot while light snapshots arrive, so a PlayerView is always complete.
        MatchState state = new MatchState();
        state.addListener(new StateListener() {
            private String last = "";

            @Override
            public void playersChanged(List<PlayerView> players) {
                for (PlayerView player : players) {
                    if (!player.name().equals(PLAYER)) {
                        continue;
                    }
                    String value = String.valueOf(Format.u(player.stats().getMoney()));
                    if (!value.equals(last)) {
                        last = value;
                        System.out.println(state.gameTime(player.frame()) + "  " + player.name() + "  Money = " + value);
                    }
                }
            }
        });

        StatsPipe pipe = StatsPipe.builder().state(state).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("following " + PLAYER + " (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
