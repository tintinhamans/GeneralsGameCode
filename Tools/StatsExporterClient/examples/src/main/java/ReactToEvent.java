// Reacts to KILL events: a player destroyed an object (player = the killer).
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.2.0.jar:protobuf-java-4.33.4.jar ReactToEvent.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;

import java.util.List;

public class ReactToEvent {
    public static void main(String[] args) throws InterruptedException {
        MatchState state = new MatchState();
        state.addListener(new StateListener() {
            @Override
            public void eventsAdded(List<StatEvent> added) {
                for (StatEvent e : added) {
                    if (e.getKind() != StatEventKind.STATEVENT_KILL) {
                        continue;
                    }
                    // The fields of a StatEvent mean different things per kind; these are the ones of KILL.
                    System.out.println(state.gameTime(Format.u(e.getFrame())) + "  KILL  " + state.playerName(e.getPlayerIndex()));
                    System.out.println("  victim: " + Format.shortName(e.getPrimaryName()));
                    System.out.println("  category: " + e.getCategory());
                    System.out.println("  victim cost: " + Format.u(e.getCost()));
                    if (e.getHasLocation()) {
                        System.out.println("  where: " + Format.eventLocation(e));
                    }
                }
            }
        });

        StatsPipe pipe = StatsPipe.builder().state(state).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("waiting for KILL events (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
