// Maps the camera's view box onto a minimap image: the four ground corners of the screen, in pixels.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.0.0.jar:protobuf-java-4.33.4.jar DrawCameraBox.java
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StateListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.WorldExtent;
import com.generalsonline.stats.proto.Camera;

public class DrawCameraBox {
    // The size of the minimap image you draw into, in pixels (the map is scaled to fit; this sketch keeps both axes independent).
    private static final int MAP_WIDTH = 256;
    private static final int MAP_HEIGHT = 256;

    public static void main(String[] args) throws InterruptedException {
        MatchState state = new MatchState();
        state.addListener(new StateListener() {
            private long lastPrint;

            @Override
            public void cameraChanged(Camera camera) {
                // Camera frames arrive up to 120 times a second, whenever the view changed. A real overlay stores the
                // latest one and draws from its own timer; this sketch just prints twice a second.
                long now = System.nanoTime();
                if (now - lastPrint < 500_000_000L || !camera.getHasViewBox()) {
                    return;
                }
                WorldExtent map = state.worldExtent().orElse(null);
                if (map == null) {
                    return; // the extent comes with the STARTED MatchInfo; there is none while the map loads
                }
                lastPrint = now;

                // The corners in the order the game's own minimap box uses: top left, top right, bottom right, bottom left.
                float[][] corners = {
                    {camera.getViewTlX(), camera.getViewTlY()},
                    {camera.getViewTrX(), camera.getViewTrY()},
                    {camera.getViewBrX(), camera.getViewBrY()},
                    {camera.getViewBlX(), camera.getViewBlY()}};
                StringBuilder line = new StringBuilder("view box:");
                for (float[] corner : corners) {
                    // World y points north and pixel y points down, so y is flipped.
                    long px = Math.round((corner[0] - map.minX()) / map.width() * MAP_WIDTH);
                    long py = Math.round((map.maxY() - corner[1]) / map.height() * MAP_HEIGHT);
                    line.append(" (").append(px).append(", ").append(py).append(')');
                }
                System.out.println(line);
                // camera.getX()/getY() is the point the camera looks at; Format.viewBox(camera) gives centre, size and rotation.
            }
        });

        StatsPipe pipe = StatsPipe.builder().state(state).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        System.out.println("waiting for camera frames (Ctrl+C to stop)");
        Thread.currentThread().join();
    }
}
