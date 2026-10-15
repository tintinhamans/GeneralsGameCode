// Records the raw frames of a running game to a dump file, then replays the file at twice real time into the
// same listener. Handler is an ordinary StatsListener: it does not know whether the frames come from a game or a file.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-1.0.0.jar:protobuf-java-4.33.4.jar RecordAndReplay.java record match.dump
//   java -cp stats-pipe-client-1.0.0.jar:protobuf-java-4.33.4.jar RecordAndReplay.java replay match.dump
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.DumpReader;
import com.generalsonline.stats.DumpWriter;
import com.generalsonline.stats.Format;
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.StatsSnapshot;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class RecordAndReplay {
    /** Prints a line every ten seconds of game time, and writes every frame to the dump when it has one. */
    static final class Handler implements StatsListener {
        private final DumpWriter writer;
        private long lastBucket = -1;

        Handler(DumpWriter writer) {
            this.writer = writer;
        }

        @Override
        public void onFrame(DecodedFrame frame) {
            // Called for every frame, also unknown ones, before the typed method: the place for a recorder.
            if (writer != null) {
                try {
                    writer.write(frame);
                } catch (IOException e) {
                    throw new UncheckedIOException(e); // reported through onError, the stream continues
                }
            }
        }

        @Override
        public void onStatsSnapshot(StatsSnapshot snapshot) {
            long bucket = Format.seconds(Format.u(snapshot.getFrame()), 30) / 10; // assumes 30 logic frames per second
            if (bucket != lastBucket) {
                lastBucket = bucket;
                System.out.println("game time " + Format.gameTime(Format.u(snapshot.getFrame()), 30) + "  " + snapshot.getPlayersCount() + " players");
            }
        }

        @Override
        public void onError(int type, Throwable cause) {
            System.err.println("error while handling frame type 0x" + Integer.toHexString(type) + ": " + cause);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2 || !(args[0].equals("record") || args[0].equals("replay"))) {
            System.err.println("usage: RecordAndReplay record|replay <file>");
            return;
        }
        Path file = Path.of(args[1]);
        if (args[0].equals("record")) {
            record(file);
        } else {
            replay(file);
        }
    }

    static void record(Path file) throws Exception {
        DumpWriter writer = new DumpWriter(file, false); // false: refuse to overwrite an existing file
        // Up to 120 camera frames a second are written: flush once a second, not per frame.
        ScheduledExecutorService flusher = Executors.newSingleThreadScheduledExecutor();
        flusher.scheduleAtFixedRate(() -> {
            try {
                writer.flush();
            } catch (IOException e) {
                System.err.println("flush failed: " + e.getMessage());
            }
        }, 1, 1, TimeUnit.SECONDS);

        StatsPipe pipe = StatsPipe.builder().listener(new Handler(writer)).build().start();
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            pipe.close();
            flusher.shutdownNow();
            try {
                writer.close();
            } catch (IOException e) {
                System.err.println("closing the dump failed: " + e.getMessage());
            }
        }));
        System.out.println("recording to " + file + " (Ctrl+C to stop)");
        Thread.currentThread().join();
    }

    static void replay(Path file) throws IOException {
        // The speed is a multiplier of game time: 1.0 is real time, 2.0 twice as fast, 0 as fast as possible.
        int frames = DumpReader.replay(file, new Handler(null), 2.0);
        System.out.println("replayed " + frames + " frames");
    }
}
