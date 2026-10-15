// Sends one chat line to the in-match chat (everyone).
//
// Things to know before you rely on this:
//  * The game serves up to four readers per pipe (Hello.max_readers). This program needs a free slot, so it fails
//    while that many other clients (the test client GUI, a watcher, an overlay) are connected to the same game.
//  * The text must not be empty and at most 1024 bytes in UTF-8; StatsPipe.requireSendable checks that.
//  * There is no reply. A true result only means the bytes were written; the game drops the line unless that
//    channel's screen or match is active. Slash commands are handled exactly as if you had typed them.
//  * The audience matters for the match channel only: EVERYONE, ALLIES, or OBSERVERS (observers only).
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.0.0.jar:protobuf-java-4.33.4.jar SendChat.java "your text"
// (use ; instead of : between the jars on Windows)
import com.generalsonline.stats.StatsListener;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class SendChat {
    public static void main(String[] args) throws Exception {
        String text = args.length > 0 ? String.join(" ", args) : "gg wp";
        StatsPipe.requireSendable(text); // throws IllegalArgumentException with a readable message

        CountDownLatch connected = new CountDownLatch(1);
        // reconnect(false): connect once and stop; this program only needs the pipe for one write.
        StatsPipe pipe = StatsPipe.builder().reconnect(false).listener(new StatsListener() {
            @Override
            public void onConnected(String pipePath) {
                connected.countDown();
            }
        }).build().start();
        try {
            if (!connected.await(10, TimeUnit.SECONDS)) {
                System.err.println("no game found within 10 seconds: is it running, and is another client connected to it?");
                return;
            }
            boolean written = pipe.sendChat(ChatChannel.CHAT_CHANNEL_MATCH, text);
            System.out.println(written ? "written to the pipe (the game gives no reply)" : "not connected");
            Thread.sleep(500); // let the write finish before the pipe is closed
        } finally {
            pipe.close();
        }
    }
}
