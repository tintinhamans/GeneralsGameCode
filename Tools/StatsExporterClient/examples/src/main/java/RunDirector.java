// Runs the automatic camera director in the mode DIRECTOR: highlights interrupt, hotspots are followed and the overview fills the quiet.
// The director reads the interest map (it switches that feed on for this connection) and steers the camera with
// camera commands, so the game must be observed or a replay. A human moving the camera pauses it; it takes over again
// after the resume delay (10 s by default). When the game refuses (NOT_ALLOWED) it pauses itself and says why.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.2.0.jar:protobuf-java-4.33.4.jar RunDirector.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Director;
import com.generalsonline.stats.DirectorConfig;
import com.generalsonline.stats.DirectorLogic;
import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.DirectorMove;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsPipe;

import java.time.Duration;

public class RunDirector {
    public static void main(String[] args) throws Exception {
        MatchState state = new MatchState();
        StatsPipe pipe = StatsPipe.builder().state(state).build().start();

        // DirectorConfig is immutable: start from the defaults and change what you need.
        DirectorConfig config = DirectorConfig.defaults().toBuilder().mode(DirectorMode.DIRECTOR).minDwell(Duration.ofSeconds(6)).build();
        Director director = Director.start(pipe, state, config);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            director.close();
            pipe.close();
        }));
        System.out.println("directing in mode DIRECTOR (Ctrl+C to stop)");

        String last = "";
        while (true) {
            Thread.sleep(2000);
            DirectorLogic.Status status = director.status();
            String report = status.state() + (status.hasTarget() ? " at " + Math.round(status.targetX()) + ", " + Math.round(status.targetY()) : "")
                + ", " + status.pendingHighlights() + " highlights pending";
            DirectorMove move = director.lastMove().orElse(null);
            if (move != null) {
                report += ", last move " + move.reason() + (move.cut() ? " (a cut)" : " (a pan)");
            }
            if (director.lastError().isPresent()) {
                report += ", problem: " + Format.cause(director.lastError().get());
            }
            if (!report.equals(last)) {
                last = report;
                System.out.println(report);
            }
        }
    }
}
