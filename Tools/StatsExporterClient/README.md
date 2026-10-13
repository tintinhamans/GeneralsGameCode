# Stats exporter reference client

Java 17 reference client for the Generals Online live stats pipe. Protocol reference: [docs/stats-exporter.md](../../docs/stats-exporter.md).

## Build

Requires a JDK 17 or newer. Protobuf classes are generated at build time from the engine's `StatsExporter.proto` (a filtered copy with `optimize_for = LITE_RUNTIME` removed, so the client gets descriptors and text printing). Nothing generated is checked in.

```
cd Tools/StatsExporterClient
gradlew build        # compile, generate protobuf, run the tests
gradlew installDist  # runnable script in build/install/stats-exporter-client/bin
```

`protobuf-java` 4.33.4 pairs with the engine's protobuf 6.33.4 (Java 4.x corresponds to C++ 6.x). The wire format is stable across versions, so a mismatch only matters for the generated code.

## Commands

Run with `gradlew run --args="<command> ..."` or the installed script. `--pid N` or `--pipe PATH` picks a pipe; otherwise the highest pid found is used.

| Command | What it does |
|---|---|
| `watch` | Connect (waiting for the game if needed), reconnect on loss, print every frame. |
| `dump <file>` | Same as `watch`, and record the raw frames to a file. |
| `replay <file>` | Print a recorded dump without a game. |
| `chat <channel> <text>` | Send one chat line; channels: `match game-lobby room lan-lobby lan-game`; `--audience everyone\|allies\|observers`. |
| `schema` | Print the frame table and every message and field. |

```
stats-exporter-client watch
stats-exporter-client dump match.dump
stats-exporter-client replay match.dump
stats-exporter-client chat match "gg wp" --audience allies
```

## Library use

```java
FrameListener listener = new FrameListener() {
    @Override public void onStatsSnapshot(StatsSnapshot s) {
        System.out.println("frame " + s.getFrame() + " players " + s.getPlayersCount());
    }
};
StatsPipeClient client = new StatsPipeClient(() -> PipeDiscovery.choosePipe(-1), listener);
client.start();                       // daemon reader thread, reconnects with backoff
client.sendChat(ChatChannel.CHAT_CHANNEL_ROOM, "hello", ChatAudience.CHAT_AUDIENCE_EVERYONE);
client.close();
```

Listener callbacks run on the reader thread, so keep them short; an exception from a callback ends the connection and triggers a reconnect. `sendChat` may be called from any thread.

## Changing the proto

The build regenerates the Java classes from `GeneralsMD/Code/GameEngine/Source/Common/StatsExporter.proto` on every build, so a proto change needs no edits here beyond handling new fields. Update `docs/stats-exporter.md` and, if a message or frame type was added, `FrameType`, `FrameDispatcher`, `FrameListener` and `Schema`.
