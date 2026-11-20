// Steers a replay playback: pause, resume, a speed ratio and fast forward.
// Only a replay can be steered this way; pausing or slowing a live network game would desynchronize it, so the game
// answers NOT_ALLOWED there (a NotAllowedException). Nothing here touches the simulation, only the pace of the playback.
//
// Run (Java 11+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-3.0.0.jar:protobuf-java-4.33.4.jar ReplayControl.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.NotAllowedException;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ReplayState;

import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

public class ReplayControl {
    public static void main(String[] args) throws Exception {
        try (StatsPipe pipe = StatsPipe.builder().reconnect(false).build().start()) {
            if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
                System.err.println("no game found within 10 seconds");
                return;
            }
            try {
                ReplayState paused = pipe.pauseReplay().get(10, TimeUnit.SECONDS);
                System.out.println("paused: " + paused.getPaused());
                Thread.sleep(2000);
                pipe.resumeReplay().get(10, TimeUnit.SECONDS);

                // The speed ratio is 0.1 to 16 (1 is normal). The game's replay runs one logic update per displayed frame at
                // most, so a ratio above what the frame rate limit allows is capped: the answer says what you got.
                ReplayState speed = pipe.setReplaySpeed(0.5).get(10, TimeUnit.SECONDS);
                System.out.println("speed " + speed.getSpeedRatio() + (speed.getSpeedCapped() ? " (capped: use fast forward for more)" : ""));
                Thread.sleep(5000);

                // Fast forward is the game's own: as fast as the machine runs, whatever the ratio.
                ReplayState fast = pipe.setReplayFastForward(true).get(10, TimeUnit.SECONDS);
                System.out.println("fast forward: " + fast.getFastForward());
                Thread.sleep(3000);
                ReplayState normal = pipe.setReplaySpeed(1.0).get(10, TimeUnit.SECONDS);
                System.out.println("back to normal speed: " + normal.getSpeedRatio() + ", fast forward " + normal.getFastForward());
            } catch (ExecutionException e) {
                if (e.getCause() instanceof NotAllowedException) {
                    System.out.println("the game refused: " + ((NotAllowedException) e.getCause()).reason());
                } else if (e.getCause() instanceof RequestFailedException) {
                    System.out.println("the request failed: " + e.getCause().getMessage());
                } else {
                    throw e;
                }
            }
        }
    }
}
