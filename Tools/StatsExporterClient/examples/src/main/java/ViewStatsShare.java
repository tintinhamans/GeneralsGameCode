// Prints what share of the value inside the camera's view each player holds, about once a second.
// ViewStats frames describe, per player, what lies inside the four ground corners of the local camera: the value (sum of
// build costs), the same weighted by health, the value by class, veterancy, notable units, superweapons in view, and the
// value killed and lost and the damage dealt over rolling windows.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.3.0.jar:protobuf-java-4.33.4.jar ViewStatsShare.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.PlayerViewStats;
import com.generalsonline.stats.proto.ViewStats;

public class ViewStatsShare {
    public static void main(String[] args) throws InterruptedException {
        MatchState state = new MatchState();
        state.addListener(new StateListener() {
            private long lastPrint;

            @Override
            public void viewStatsChanged(ViewStats view) {
                // About four a second (FeedConfig.view_stats_interval_ms); print once a second.
                long now = System.nanoTime();
                if (!view.getHasView() || now - lastPrint < 1_000_000_000L) {
                    return;
                }
                long total = 0;
                for (PlayerViewStats player : view.getPlayersList()) {
                    total += Format.u(player.getValueInView());
                }
                if (total == 0) {
                    return; // nothing in view
                }
                lastPrint = now;
                StringBuilder line = new StringBuilder(state.gameTime(Format.u(view.getFrame()))).append(" in view:");
                for (PlayerViewStats player : view.getPlayersList()) {
                    long value = Format.u(player.getValueInView());
                    line.append("  ").append(state.playerName(player.getPlayerIndex())).append(' ').append(Math.round(100.0 * value / total)).append("% (").append(value).append(')');
                }
                System.out.println(line);
                // Also there: getEffectiveValue, getValueByClass, getNotableList (heroes and expensive units), getSuperweaponsList,
                // getValueKilled / getValueLost over the trade window and getDamagePerSecond over the intensity window.
            }
        });

        StatsPipe pipe = StatsPipe.builder().state(state).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("waiting for view statistics (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
