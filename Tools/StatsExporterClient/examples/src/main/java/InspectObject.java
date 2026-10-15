// Looks one live object up by its id: template, owner, place, health and veterancy.
// Ids come from events (StatEvent.getObjectId, getKillerObjectId), from the notable units of the view statistics and
// from the units a camera lock follows. An object that was destroyed answers NOT_FOUND.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.2.0.jar:protobuf-java-4.33.4.jar InspectObject.java [object id]
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Format;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ObjectDetail;

import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

public class InspectObject {
    private static final int DEFAULT_ID = 1234;

    public static void main(String[] args) throws Exception {
        int id = args.length > 0 ? Integer.parseInt(args[0]) : DEFAULT_ID;
        try (StatsPipe pipe = StatsPipe.builder().reconnect(false).build().start()) {
            if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
                System.err.println("no game found within 10 seconds");
                return;
            }
            try {
                ObjectDetail object = pipe.object(id).get(10, TimeUnit.SECONDS);
                System.out.println(object.getTemplateName() + " (object " + Format.u(object.getObjectId()) + ") of player " + object.getOwnerPlayerIndex()
                    + (object.getOwnerPlayerIndex() < 0 ? " (neutral or unknown)" : ""));
                System.out.println("at " + object.getX() + ", " + object.getY() + ", health " + object.getHealth() + " of " + object.getMaxHealth()
                    + ", " + Format.veterancyName(object.getVeterancy()));
                if (object.getUnderConstruction()) {
                    System.out.println("under construction: " + object.getConstructionPercent() + " percent");
                }
                if (!object.getStatusList().isEmpty()) {
                    System.out.println("status: " + String.join(", ", object.getStatusList()));
                }
                if (object.getTargetObjectId() != 0) {
                    System.out.println("busy with object " + Format.u(object.getTargetObjectId()));
                }
            } catch (ExecutionException e) {
                // The typed request methods fail with a RequestFailedException carrying the status of the answer.
                if (e.getCause() instanceof RequestFailedException failed) {
                    System.out.println("object " + id + ": " + failed.getMessage());
                } else {
                    throw e;
                }
            }
        }
    }
}
