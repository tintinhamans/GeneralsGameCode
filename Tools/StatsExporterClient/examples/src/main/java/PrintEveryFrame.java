// Connects to a running Generals Online game and prints one line for every frame type.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.0.0.jar:protobuf-java-4.33.4.jar PrintEveryFrame.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.ChatMessage;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchSummary;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.UiState;

public class PrintEveryFrame {
    public static void main(String[] args) throws InterruptedException {
        // Every method of StatsListener has an empty default; override what you need.
        // The callbacks run on the reader thread, so keep them short.
        StatsListener listener = new StatsListener() {
            @Override
            public void onConnected(String pipePath) {
                System.out.println("connected to " + pipePath);
            }

            @Override
            public void onDisconnected(Throwable cause) {
                System.out.println("disconnected" + (cause == null ? "" : ": " + cause.getMessage()));
            }

            @Override
            public void onMatchInfo(MatchInfo match) {
                System.out.println("MatchInfo " + match.getPhase() + " map " + match.getMapName() + ", " + match.getPlayersCount() + " players");
            }

            @Override
            public void onStatsSnapshot(StatsSnapshot snapshot) {
                // About ten a second: a light one every 100 ms of game time, a full one every 250 ms.
                System.out.println("StatsSnapshot frame " + snapshot.getFrame() + (snapshot.getFull() ? " (full)" : " (light)")
                    + ", " + snapshot.getPlayersCount() + " players");
            }

            @Override
            public void onMatchSummary(MatchSummary summary) {
                System.out.println("MatchSummary " + summary.getPlayersCount() + " players");
            }

            @Override
            public void onChatMessage(ChatMessage chat) {
                System.out.println("ChatMessage " + chat.getSenderName() + ": " + chat.getText());
            }

            @Override
            public void onUiState(UiState ui) {
                System.out.println("UiState " + ui.getScreen());
            }

            @Override
            public void onLobbyState(LobbyState lobby) {
                System.out.println("LobbyState " + (lobby.getInLobby() ? lobby.getSlotsCount() + " slots" : "left the lobby"));
            }

            @Override
            public void onEvents(Events events) {
                System.out.println("Events frame " + events.getFrame() + ", " + events.getEventsCount() + " events");
            }

            @Override
            public void onCamera(Camera camera) {
                // Up to 120 a second: printing every one would flood the console.
            }

            @Override
            public void unknownFrame(int type, byte[] body) {
                System.out.println("unknown frame type 0x" + Integer.toHexString(type));
            }

            @Override
            public void onError(int type, Throwable cause) {
                // A callback above threw; the stream keeps going.
                System.err.println("error while handling frame type 0x" + Integer.toHexString(type) + ": " + cause);
            }
        };

        // No pid or pipe given: every running game is tried, highest pid first, and the first free one is used.
        // The pipe waits for the game to start and reconnects when it restarts.
        StatsPipe pipe = StatsPipe.builder().listener(listener).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("waiting for a game (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
