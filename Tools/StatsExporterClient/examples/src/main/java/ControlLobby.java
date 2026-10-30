// Changes the game setup lobby the user sits in (skirmish, LAN or Generals Online) the way the setup screen's own
// controls would: it puts a computer opponent into an open slot, picks its faction and sets the starting cash.
// The game drives the screen's controls, so the screen's rules apply: only the host changes slots, the map and the
// rules, a client changes only its own slot and accepts. What the screen would refuse arrives here as a
// NotAllowedException with the reason. The LobbyState frame that follows a change is the confirmation.
//
// Run (Java 11+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-3.0.0.jar:protobuf-java-4.33.4.jar ControlLobby.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.LobbyControl;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.NotAllowedException;
import com.generalsonline.stats.RequestFailedException;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.LobbyControlResult;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.LobbyState;

import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

public class ControlLobby {
    public static void main(String[] args) throws Exception {
        MatchState state = new MatchState();
        try (StatsPipe pipe = StatsPipe.builder().reconnect(false).state(state).build().start()) {
            if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
                System.err.println("no game found within 10 seconds");
                return;
            }
            if (!pipe.hasCapability("lobby.control")) {
                System.err.println("this game cannot change a lobby (no lobby.control capability)");
                return;
            }
            LobbyState lobby = null;
            for (int i = 0; i < 50 && lobby == null; i++) {
                lobby = state.lobby().orElse(null);
                if (lobby == null) {
                    Thread.sleep(100);
                }
            }
            if (lobby == null) {
                System.out.println("the user is not in a game setup lobby");
                return;
            }

            int open = -1;
            for (int slot = 0; slot < lobby.getSlotsCount() && open < 0; slot++) {
                if (lobby.getSlots(slot).getState() == LobbySlotState.LOBBY_SLOT_OPEN) {
                    open = slot;
                }
            }
            try {
                if (open >= 0) {
                    // The result says whether the lobby changed at once (skirmish, a LAN host) or the change was
                    // requested from the host or the lobby service and shows in the next LobbyState.
                    LobbyControlResult added = pipe.addAi(open, LobbyControl.AiLevel.MEDIUM).get(10, TimeUnit.SECONDS);
                    System.out.println("computer opponent in slot " + (open + 1) + ": " + added.getRoute());
                    pipe.setFaction(open, "China").get(10, TimeUnit.SECONDS);
                } else {
                    System.out.println("no open slot to put a computer opponent in");
                }
                pipe.setStartingCash(20000).get(10, TimeUnit.SECONDS);
                System.out.println("starting cash set");

                // The Start button, if the screen has it enabled; with players that have not accepted the game only
                // announces the intent to start. Leave it commented out unless you mean it.
                // pipe.startGame().get(10, TimeUnit.SECONDS);
            } catch (ExecutionException e) {
                if (e.getCause() instanceof NotAllowedException) {
                    System.out.println("the setup screen would not allow it: " + ((NotAllowedException) e.getCause()).reason());
                } else if (e.getCause() instanceof RequestFailedException) {
                    System.out.println("the request failed: " + e.getCause().getMessage());
                } else {
                    throw e;
                }
            }
        }
    }
}
