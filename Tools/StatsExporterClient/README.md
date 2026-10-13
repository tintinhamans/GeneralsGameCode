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
| `gui` | Show the feed live in a window (see below). Options: `--pid N`, `--pipe PATH`, or `--replay FILE [--speed N]`. |
| `schema` | Print the frame table and every message and field. |

```
stats-exporter-client watch
stats-exporter-client dump match.dump
stats-exporter-client replay match.dump
stats-exporter-client chat match "gg wp" --audience allies
```

## GUI

`gui` opens a Swing window (JDK only, no extra dependency) that shows the feed as tables, for watching a match without a console:

```
stats-exporter-client gui                          # connect to the running game, reconnect if it restarts
stats-exporter-client gui --pid 1234               # a specific game process
stats-exporter-client gui --replay match.dump      # play a recorded dump, as fast as possible
stats-exporter-client gui --replay match.dump --speed 20   # about 20 frames per second
```

A status bar shows the connection, the screen, the match, the frame and game time, the game speed ("uncapped" and "paused/halted" for -1 and 0), the network stall flag and the snapshot count. The tabs are:

| Tab | Content |
|---|---|
| Players | One row per player from the latest snapshot, joined with the MatchInfo player (name, side, color, team) by `player_index`. Rows update in place; columns sort. |
| Events | Append-only list of the snapshot events (newest at the bottom, follows new rows until you scroll up, last 3000 kept). |
| Chat | Every chat line received, with channel, sender, audience and flags. |
| Lobby | The latest LobbyState: slots with the predicted side, color and start position; after LOADING, a green or red label for the prediction check. |
| Match | The latest MatchInfo as key/value rows, and the MatchSummary rows when one arrives. |
| Frames | Raw log of every frame with type, size and a one-line summary. |

The footer shows the frames and bytes received per second. Stall snapshots (no players) never clear the Players table. A reconnect keeps the tables and only changes the status; a new match (a LOADING frame, or a STARTED frame with a different match id) clears Players and Events.

The code is meant to be read: `StatsGui` is the window and the thread hand-over (the reader thread decodes, `SwingUtilities.invokeLater` delivers each frame to the event thread), `GuiModel` holds all state and table models without any Swing component, and `RowTableModel` is the small table model they share.

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
