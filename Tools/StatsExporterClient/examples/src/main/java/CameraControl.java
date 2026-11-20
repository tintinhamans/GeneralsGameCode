// Moves the game's camera: a pan with a transition, then a lock on a unit, then back to the default view.
// The game accepts camera and follow commands in any running match; when it cannot (no match is running), it
// answers UNAVAILABLE or NOT_ALLOWED, which arrives here as a failure with the game's reason.
//
// Run (Java 11+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-3.0.0.jar:protobuf-java-4.33.4.jar CameraControl.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.CameraMove;
import com.generalsonline.stats.NotAllowedException;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.CameraState;

import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

public class CameraControl {
    public static void main(String[] args) throws Exception {
        try (StatsPipe pipe = StatsPipe.builder().reconnect(false).build().start()) {
            if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
                System.err.println("no game found within 10 seconds");
                return;
            }
            if (!pipe.hasCapability("camera.control")) {
                System.err.println("this game cannot be steered (no camera.control capability)");
                return;
            }
            try {
                // A pan over two seconds that starts and ends slowly; the zoom changes with it.
                // The transition runs on wall-clock time, so even a paused game pans.
                CameraState moved = pipe.moveCamera(CameraMove.to(1800.0, 2400.0).zoom(1.2).over(Duration.ofSeconds(2)).easeInOut()).get(10, TimeUnit.SECONDS);
                System.out.println("moving, from " + moved.getX() + ", " + moved.getY() + " (a running transition shows its start)");
                Thread.sleep(2500);

                // Lock the camera on a unit by its object id. followUnit keeps it centred;
                // tetherUnit(id, 20) lets the unit move around inside a slack area first.
                CameraState locked = pipe.followUnit(1234).get(10, TimeUnit.SECONDS);
                System.out.println("following object " + locked.getFollowObjectId());
                Thread.sleep(5000);

                // Release the lock and put angle, pitch and zoom back to the game's defaults.
                pipe.unfollowUnit().get(10, TimeUnit.SECONDS);
                pipe.resetCamera().get(10, TimeUnit.SECONDS);
                System.out.println("camera released and reset");
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
