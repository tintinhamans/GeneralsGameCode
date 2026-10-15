// Shows the game setup lobby with the predicted faction of every random slot.
// The game predicts the random sides, colours and start positions by running its own resolution on a copy of the
// lobby state, so an overlay can show them before the match starts. A prediction is only valid when the lobby says so.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.0.0.jar:protobuf-java-4.33.4.jar ShowLobby.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.Format;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchPlayer;

public class ShowLobby {
    public static void main(String[] args) throws InterruptedException {
        StatsListener listener = new StatsListener() {
            @Override
            public void onLobbyState(LobbyState lobby) {
                if (!lobby.getInLobby()) {
                    System.out.println("left the lobby");
                    return;
                }
                System.out.println("lobby on " + Format.mapDisplayName(lobby.getMapName())
                    + " (sides and colours predicted: " + lobby.getPredictionValid()
                    + ", start positions predicted: " + lobby.getPredictionStartPositionsValid() + ")");
                for (LobbySlot slot : lobby.getSlotsList()) {
                    if (!slot.hasPlayer()) {
                        continue; // an open or closed slot
                    }
                    MatchPlayer player = slot.getPlayer();
                    String side = player.getSide();
                    if (player.getIsSideRandom()) {
                        side = lobby.getPredictionValid() ? "Random -> " + slot.getPredictedSide() : "Random";
                    }
                    String color = player.getIsColorRandom() ? "random" : Format.colorHex(player.getColor());
                    if (player.getIsColorRandom() && lobby.getPredictionValid() && slot.getPredictedColor() != 0) {
                        color = "random -> " + Format.colorHex(slot.getPredictedColor());
                    }
                    String start = lobby.getPredictionStartPositionsValid() ? Integer.toString(slot.getPredictedStartPosition()) : "?";
                    System.out.println("  slot " + (player.getLobbySlot() + 1) + "  " + player.getName() + "  " + side
                        + "  colour " + color + "  start position " + start + (slot.getIsReady() ? "  ready" : ""));
                }
            }
        };

        StatsPipe pipe = StatsPipe.builder().listener(listener).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("waiting for a lobby (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
