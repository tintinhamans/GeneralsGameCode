// Uses MatchState from a UI thread. The pattern is the same for Swing, JavaFX or any other toolkit:
//  * give the pipe an executor that runs tasks on the UI thread, in order (Swing: SwingUtilities::invokeLater,
//    JavaFX: Platform::runLater); the reader thread then only decodes, and every callback, including the
//    state change callbacks, runs on the UI thread;
//  * update the widgets from the StateListener methods, which are called after the state was updated;
//  * MatchState itself is thread safe and returns immutable values, so any other thread may read it too.
// Camera frames arrive up to 120 times a second; if the UI only needs the latest one, do not repaint per frame.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.2.0.jar:protobuf-java-4.33.4.jar UiThreadState.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.PlayerView;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsPipe;

import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.SwingUtilities;
import java.util.List;
import java.util.concurrent.Executor;

public class UiThreadState {
    public static void main(String[] args) throws Exception {
        MatchState state = new MatchState();
        JLabel label = new JLabel("waiting for a game...");
        SwingUtilities.invokeAndWait(() -> {
            JFrame window = new JFrame("Generals Online match state");
            window.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            window.add(label);
            window.setSize(520, 90);
            window.setVisible(true);
        });

        state.addListener(new StateListener() {
            @Override
            public void playersChanged(List<PlayerView> players) {
                // Runs on the Swing event thread because of the executor below.
                StringBuilder text = new StringBuilder(state.gameTime(state.frame()));
                for (PlayerView player : players) {
                    text.append("   ").append(player.name()).append(' ').append(player.stats().getMoney()).append(" (").append(player.stateText()).append(')');
                }
                label.setText(text.toString());
            }

            @Override
            public void matchReset() {
                label.setText("new match");
            }
        });

        Executor uiThread = SwingUtilities::invokeLater; // JavaFX: Platform::runLater
        StatsPipe pipe = StatsPipe.builder().state(state).executor(uiThread).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
    }
}
