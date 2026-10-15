# Generals Online stats pipe client

A Java 17 library for the live stats pipe of a Generals Online game, a test client that shows everything the pipe delivers, and example programs. Protocol reference: [docs/stats-exporter.md](../../docs/stats-exporter.md). This is version 2.0.0, for **protocol version 2** (a 17 byte frame header with a sequence number and a timestamp, a Hello handshake, requests and responses); it cannot talk to a game of protocol version 1, see the [CHANGELOG](CHANGELOG.md).

| Project | What it is |
|---|---|
| [`lib/`](lib) | `com.generalsonline:stats-pipe-client:2.0.0`. The library other people depend on: the generated protobuf classes, the transport, a thread safe match state, dump files and helpers. protobuf-java is its only dependency. |
| [`app/`](app) | The test client: the commands `watch`, `dump`, `replay`, `chat`, `gui`, `schema`, `example` and a Swing GUI. It uses the library through its public API only. |
| [`examples/`](examples) | One runnable program per scenario of the GUI's Code tab, compiled against the library in every build. |

## Use the library

### Coordinates and build

Requires a JDK 17 or newer. The Gradle wrapper in this folder builds everything; nothing generated is checked in.

```
cd Tools/StatsExporterClient
gradlew build                       # generate the protobuf classes, compile, test, build the jars
gradlew :lib:publishToMavenLocal    # install the jar, the sources jar and the javadoc jar into ~/.m2
```

Then, in another project:

```groovy
repositories { mavenLocal() }
dependencies { implementation 'com.generalsonline:stats-pipe-client:2.0.0' }   // brings protobuf-java 4.33.4
```

```xml
<dependency>
  <groupId>com.generalsonline</groupId>
  <artifactId>stats-pipe-client</artifactId>
  <version>2.0.0</version>
</dependency>
```

Without a build tool: `lib/build/libs/stats-pipe-client-2.0.0.jar` plus `protobuf-java-4.33.4.jar` on the class path. The jar is an automatic module named `com.generalsonline.stats`. The packages are `com.generalsonline.stats` (the API), `com.generalsonline.stats.proto` (the generated messages) and `com.generalsonline.stats.internal` (not for users).

The message classes are generated from the game's own `StatsExporter.proto` (a filtered copy with `optimize_for = LITE_RUNTIME` removed, so the classes have descriptors and text printing). There is one copy of the schema; the build never modifies it. `protobuf-java` 4.33.4 pairs with the engine's protobuf 6.33.4 (Java 4.x corresponds to C++ 6.x); the wire format is stable across versions.

### Quick start

```java
import com.generalsonline.stats.*;
import java.util.List;

public class QuickStart {
    public static void main(String[] args) throws InterruptedException {
        MatchState state = new MatchState();                 // joins snapshots with MatchInfo, merges light/full snapshots
        state.addListener(new StateListener() {
            @Override
            public void playersChanged(List<PlayerView> players) {
                for (PlayerView p : players) {
                    System.out.println(state.gameTime(p.frame()) + " " + p.name() + " $" + p.stats().getMoney() + " " + p.stateText());
                }
            }
        });
        StatsPipe pipe = StatsPipe.builder().state(state).build().start();   // finds a game, waits for one, reconnects
        Runtime.getRuntime().addShutdownHook(new Thread(pipe::close));
        Thread.currentThread().join();
    }
}
```

Run it with the two jars on the class path, for example `java -cp stats-pipe-client-2.0.0.jar;protobuf-java-4.33.4.jar QuickStart.java` (`:` instead of `;` outside Windows).

### The public API

| Type | What it does |
|---|---|
| `StatsPipe` | The connection. `StatsPipe.builder()` with `.pid(int)` or `.pipe(String)` (otherwise every running game is tried, highest pid first, and a game without a free reader slot is skipped), `.listener(StatsListener)`, `.state(MatchState)`, `.executor(Executor)`, `.reconnect(boolean)`, `.reconnectBackoff(min, max)`, `.helloTimeout(Duration)` (default 10 s), `.requestTimeout(Duration)` (default 5 s), `.limitRequests(boolean)` (default off), `.build()`. Then `start()`, `close()` (it is `AutoCloseable`), `isConnected()`, `connectedPid()`, `connectedPipe()`, `hello()` / `awaitHello(Duration)` / `hasCapability(String)`, `droppedFrames()`, `clockSync()`, `sendChat(...)` and the request methods (`request`, `catalog`, `object`, `objects`, `player`, `map`, `resync`, `feedConfig`, `setFeedConfig`, `catalogAll`, `objectsAll`), see below. The builder rejects bad values (a negative pid, a blank pipe, both a pid and a pipe, a timeout under 1 ms) at once; `sendChat` rejects an empty line, a line over 1024 UTF-8 bytes and an audience the game cannot take. |
| `StatsListener` | One method per frame type (`onHello`, `onMatchInfo`, `onStatsSnapshot`, `onMatchSummary`, `onChatMessage`, `onUiState`, `onLobbyState`, `onEvents`, `onCamera`, `onResponse`), `onFramesDropped(count)`, the lifecycle (`onConnected`, `onDisconnected(cause)`), `onFrame` for every raw frame (recorders; `DecodedFrame` has `sequence()` and `timestampMicros()`), `unknownFrame`, `decodeError` and `onError`. All have empty defaults. |
| `MatchState` | The state the library maintains for you, safe to read from any thread, see below. Feed it through `StatsPipe.Builder.state(...)` or use it as a `StatsListener` yourself. |
| `StateListener` | Change callbacks of a `MatchState`: `matchChanged`, `matchReset`, `playersChanged`, `snapshotApplied`, `eventsAdded`, `cameraChanged`, `worldChanged`, `chatAdded`, `lobbyChanged`, `uiChanged`, `summaryChanged`, `helloReceived`, `framesDropped`. An overlay redraws from these instead of diffing. |
| `Handshake`, `ClockSync` | Helpers for the Hello (`PROTOCOL_VERSION`, `hasCapability`, `requireSupported`, `wallClockMillis(hello, timestampMicros)`, `summary`) and the estimate of the offset between the game's frame clock and yours. |
| `ProtocolVersionException`, `HelloTimeoutException`, `RequestFailedException`, `RequestTimeoutException`, `PageLimitException` | What the handshake and the request API fail with, see below. |
| `PlayerView` | One player: `info()` (the MatchInfo entry), `stats()` (merged `PlayerStats`), `frame()`, `life()` (`ALIVE`, `DEFEATED`, `LEFT`), and `name()`, `side()`, `color()`, `team()`, `elo()`, `stateText()`. |
| `Format` | The helpers every reader writes: `gameTime(frames, fps)`, `u(int)` (unsigned reads of uint32 fields), `colorHex(argb)` / `rgb(argb)`, `shortName`, `shortUpgradeName`, `shortPowerName`, `mapDisplayName`, `eventDetail(event, names, fps)`, `viewBox(camera)`, `cause(throwable)`. |
| `DumpWriter`, `DumpReader` | Record the raw frames of a game (with their sequence numbers and timestamps, the Hello included) and replay them without one: `DumpReader.replay(file, listener)` as fast as possible, or `replay(file, listener, speed)` paced by game time (1.0 is real time, 2.0 twice as fast). A dump goes through the same dispatcher as a live pipe, gaps in its sequence come out as `onFramesDropped`. The file starts with the magic `GOSD` and a format version (2); dumps of protocol version 1 (no header) are still read, with sequence numbers made up. |
| `FrameDispatcher`, `FrameType`, `DecodedFrame`, `RawFrame`, `PipeDiscovery` | Decoding and delivery, for building your own pipeline, and the discovery of running games. |

`MatchState` holds: the game's `hello()` and `droppedFrames()`; the current screen (`ui()`), game lobby (`lobby()`) and match (`match()`, `phase()`, `matchId()`); the players (`players()`, `playersByIndex()`, `player(index)`, `playerName(index)`), each joined with its MatchInfo entry; the latest camera (`camera()`), the world extent (`worldExtent()`) and the map-level heavy data of the last full snapshot (`grids()`, `supply()`, `tech()`, `connections()`); a ring buffer of recent events (`events()`) and chat lines (`chat()`); and frame, speed and stall values (`frame()`, `gameSpeedFps()`, `networkStalled()`, ...). Its lifecycle rules: a LOADING `MatchInfo`, or a STARTED one with another match id, begins a new match and clears the old one; a repeated STARTED of the same match changes nothing; ENDED keeps the tables so the result stays readable; a reconnect keeps everything. A light snapshot overwrites the cheap per-player numbers and keeps the heavy fields (army value, production, superweapons, ...) of the last full one.

### Handshake, dropped frames and clocks

Every connection starts with the game's `Hello` (frame type `0x09`, sequence 0). The library waits for it (`helloTimeout`, 10 s by default), **refuses any protocol version but 2** with a `ProtocolVersionException` (the cause passed to `onDisconnected`, together with the refused `Hello`), refuses a connection whose first frame is something else, and ends a silent one with a `HelloTimeoutException` (a game of protocol version 1 never sends a Hello). After that:

```java
Hello hello = pipe.awaitHello(Duration.ofSeconds(10)).orElseThrow();   // or pipe.hello(), or StatsListener.onHello, or state.hello()
System.out.println(hello.getGameVersion() + " protocol " + hello.getProtocolVersion()
    + " reader " + hello.getReaderSlot() + " of " + hello.getMaxReaders() + ", " + hello.getLogicFramesPerSecond() + " logic fps");
if (pipe.hasCapability("request.catalog")) { ... }                          // capability names are listed in docs/stats-exporter.md
```

The game serves up to `Hello.max_readers` readers per pipe, each with its own queue, sequence numbers and feed configuration; there is nothing to do for that except knowing `reader_slot`.

**Dropped frames.** Every frame carries a per-reader sequence number (`DecodedFrame.sequence()`, 0 for the Hello). A reader that falls behind loses frames, and the sequence then jumps (the dropped frame keeps its number). The library sums the gaps with unsigned 32 bit arithmetic: `pipe.droppedFrames()`, `state.droppedFrames()`, `StatsListener.onFramesDropped(count)` (called before the frame that showed the gap) and `StateListener.framesDropped(count, total)`. After a drop the state you built may be stale; `pipe.resync()` asks the game to send the screen, lobby, match and a full snapshot again. A dump replay reports its gaps the same way.

**Clocks.** `DecodedFrame.timestampMicros()` is the game's monotonic exporter clock in microseconds; `Hello.epoch_unix_ms` is the wall clock at timestamp 0, so `Handshake.wallClockMillis(hello, timestamp)` gives the wall clock time of a frame. `pipe.clockSync()` keeps the minimum of `received - timestamp` over the last 256 frames, which is the offset between the two clocks plus the fastest frame's delay; `excessDelayMicros(timestamp, receivedMicros)` is how much later a frame arrived than that (queueing, a busy reader). It is "one-way-ish": good for comparing frames, not an absolute latency.

### Requests

A reader can ask the game questions. The result arrives as a `Response` frame for that reader only; the library assigns the request ids (a counter that skips 0), matches responses by id and does the timing out:

```java
CompletableFuture<ObjectDetail> unit = pipe.object(1234);                       // one live object
CompletableFuture<PlayerDetail> player = pipe.player(2);                        // money, rank, sciences, powers, what it can build
CompletableFuture<List<CatalogEntry>> templates = pipe.catalogAll(CatalogQuery.newBuilder().setSide("GLA").build());   // follows the pages
CompletableFuture<MapDetail> map = pipe.map(true);                              // extent, waypoints, start positions, minimap image
CompletableFuture<FeedConfig> feed = pipe.setFeedConfig(FeedConfigQuery.newBuilder().setCamera(false).setLightIntervalMs(200).build());
unit.thenAccept(u -> System.out.println(u.getTemplateName() + " " + u.getHealth() + "/" + u.getMaxHealth()));
```

- The typed methods complete with the result and fail with a `RequestFailedException` carrying the `ResponseStatus` (`BAD_REQUEST`, `NOT_FOUND`, `RATE_LIMITED`, `UNAVAILABLE`, `TOO_LARGE`) and the game's message. `request(Request, Duration)` is the core: it completes normally with the `Response` whatever the status. It fails with a `RequestTimeoutException` (a `TimeoutException`) after the timeout, with an `IOException` when the pipe is not connected, a write fails, or the connection ends or the pipe is closed while it waits (every pending request fails then), and with an `IllegalStateException` when it is called before the Hello arrived. A late or unsolicited response is ignored (but still reaches `StatsListener.onResponse`).
- The futures complete on the reader thread, not on the executor of the builder. Do not block on one inside a callback that runs on the reader thread; chain with `thenAccept` or use another thread.
- Paged queries (`catalog`, `objects`) return one page and a `PageInfo`; `catalogAll` / `objectsAll` follow `next_cursor` until `has_more` is false, at most 100 pages (a `PageLimitException` after that, or pass your own limit).
- **The game rate-limits requests**: `Hello.max_requests_per_second` sustained and `Hello.max_request_burst` at once; more are answered with `RATE_LIMITED` and nothing is done. `StatsPipe.builder().limitRequests(true)` makes the library apply the same limit itself: an excess request is not sent and answers `RATE_LIMITED` ("client-side rate limit") immediately. Off by default.
- A request body is at most 4096 bytes (`IllegalArgumentException` otherwise).
- **Events** carry more since protocol version 2: `StatEvent.getObjectId()` (the victim, new, captured or promoted object), and for kills and losses `getKillerObjectId()`, `getKillerTemplate()` and `getWeapon()` (also in `Format.eventDetail`: "by ChinaTankGattling with GattlingTankGun"). They are plain fields of the generated classes; use `pipe.object(id)` to look an object up while it lives.

### Threading

- A pipe has one daemon reader thread that connects, reads and decodes. By default the listener and the state are called on that thread, so keep callbacks short.
- `.executor(Executor)` moves delivery: each frame becomes one task on that executor, in order (it must be a single threaded event loop, for example `SwingUtilities::invokeLater` or JavaFX `Platform::runLater`). The reader thread then only decodes, and the state is updated and its change callbacks run on that executor.
- `MatchState` is thread safe: every method may be called from any thread and returns immutable values (records, protobuf messages, unmodifiable lists), so a UI thread can keep what it read.
- A callback that throws costs only its frame: the exception goes to `onError` and reading continues. Only a pipe or framing failure disconnects, and `onDisconnected` receives the real cause. The library never prints and never exits the process.
- Camera frames arrive up to 120 times a second. If you only need the latest one, store it and draw from your own timer instead of repainting per frame.
- The game serves up to `Hello.max_readers` (4) readers per pipe. A client that finds every slot taken gets no connection; the pipe then tries the next game, or waits and retries.

### Example programs

[`examples/src/main/java`](examples/src/main/java) has one complete program per scenario, compiled against the library on every build. Each can also be run on its own: `java -cp stats-pipe-client-2.0.0.jar;protobuf-java-4.33.4.jar PrintEveryFrame.java`.

| Program | Shows |
|---|---|
| [`PrintEveryFrame`](examples/src/main/java/PrintEveryFrame.java) | Connect and print every frame type (listener basics). |
| [`TrackPlayerStat`](examples/src/main/java/TrackPlayerStat.java) | Follow one player's money, income or army value over time with `MatchState`. |
| [`ReactToEvent`](examples/src/main/java/ReactToEvent.java) | React to one kind of event (KILL, CAPTURE, UPGRADE_COMPLETE, ...) with its detail fields. |
| [`DrawCameraBox`](examples/src/main/java/DrawCameraBox.java) | Turn a camera frame into the four corners of the view on a minimap. |
| [`ShowLobby`](examples/src/main/java/ShowLobby.java) | The lobby with the predicted faction, colour and start position of random slots. |
| [`SendChat`](examples/src/main/java/SendChat.java) | Send a chat line, with the limits and the one-reader caveat. |
| [`RecordAndReplay`](examples/src/main/java/RecordAndReplay.java) | Record a dump, replay it at 2x into the same listener. |
| [`UiThreadState`](examples/src/main/java/UiThreadState.java) | Use `MatchState` from a UI thread with an executor. |

These files are the output of the test client's code generator for its default selection (`example <name>`), and a test fails when one is out of date. The GUI's **Code** tab and the `example` command fill the same templates with what you selected, for example the Players column or the event kind.

## The test client

Run with `gradlew :app:run --args="<command> ..."` or the installed script (`gradlew :app:installDist`, then `app/build/install/stats-exporter-client/bin/stats-exporter-client`). Options start with `--`, take their value as the next argument (or `--name=value`) and go before the positional arguments; `--` ends the options. A mistake prints one line saying what is wrong plus the usage, never a stack trace.

`--pid N` or `--pipe PATH` picks a pipe. Without them the client lists the stats pipes and tries them **highest pid first**, which is only a guess at the newest game (Windows reuses pids), not a rule. The game serves a few readers per pipe, so when every slot of that pipe is taken the next one is tried, and the first pipe with a free slot wins. The pid of the connected game is shown in the GUI status bar and window title.

| Command | What it does |
|---|---|
| `watch [--only KINDS]` | Connect (waiting for the game if needed), reconnect on loss, print the feed. |
| `dump [--overwrite] [--no-camera] <file>` | Record the raw frames to a file and print one status line per second. |
| `replay <file>` | Print a recorded dump without a game, every frame. |
| `chat [--audience A] <channel> <text...>` | Send one chat line. |
| `gui [--replay FILE [--speed X]]` | Show the feed in a window (see below). |
| `schema` | Print the frame table and every message and field. |
| `example [<name>] [--player P] [--column C] [--kind K] [--channel CH] [--audience A] [--text T]` | Print a complete example program for a scenario (no name lists them). |

```
stats-exporter-client watch --only events,chat
stats-exporter-client dump match.dump
stats-exporter-client replay match.dump
stats-exporter-client chat --audience allies match "gg wp"
stats-exporter-client example track-player --column "Income/min" --player Alice > TrackPlayerStat.java
stats-exporter-client example react-to-event --kind capture
```

### watch

Prints players by name (from MatchInfo; `P3` only until a MatchInfo arrived) and game time. Snapshots are printed at most once per second and camera frames at most twice a second, because a console cannot follow ten snapshots and up to 120 camera frames a second. `--only` takes a comma separated list of `events,chat,match,lobby,ui,snapshot,camera` (`match` includes the match summary); connection lines and errors are always shown. `replay` prints everything, unthrottled.

### dump

The file is a `GOSD` header (format version 2) followed by the raw frames with their sequence numbers and timestamps, the Hello first. Refuses to overwrite an existing file unless you pass `--overwrite`. `--no-camera` leaves the camera frames out of the file (they are most of the bytes and rarely needed for stats). The file is flushed once a second, and the console shows one line per second (frames, camera frames, bytes, game time, match phase, errors) instead of every frame.

### chat

`chat [options] <channel> <text...>`. Channels: `match game-lobby room lan-lobby lan-game`; `--audience everyone|allies|observers` (match chat only).

- Options go **before** the channel. Everything after the channel is the text. A word after the channel that is a known option name (for example `--audience`) is an error that says so, instead of being sent as chat or silently taken as an option. Put `--` before the channel to send such words literally: `chat -- match "use --pid to pick a game"`.
- The text must not be empty and must be at most 1024 bytes of UTF-8; this is checked before connecting.
- The game sends no reply. "wrote to ..." only means the bytes went into the pipe; the game ignores the line unless that channel's screen or match is active.
- Chat needs a free reader slot of the pipe (the game serves up to four): it fails with an explanation while every slot is held by other clients, for example several `gui`, `watch` or `dump` processes. Close one first.

### example

`example` lists the scenarios; `example <name>` prints the program, which compiles against the library as it is. `--player` and `--column` fill `track-player` (the column is any column of the Players table, mapped to its accessor, for example `Money` to `player.stats().getMoney()`), `--kind` fills `react-to-event` (`KILL`, `CAPTURE`, ... or the full `STATEVENT_` name), `--channel`, `--audience` and `--text` fill `send-chat`.

## GUI

`gui` opens a Swing window (JDK only, no extra dependency) that shows the feed as tables and a minimap, for watching a match without a console:

```
stats-exporter-client gui                          # connect to the running game, reconnect if it restarts
stats-exporter-client gui --pid 1234               # a specific game process
stats-exporter-client gui --replay match.dump      # play a recorded dump, as fast as possible
stats-exporter-client gui --replay match.dump --speed 1   # real time (game time / MatchInfo logic rate)
stats-exporter-client gui --replay match.dump --speed 4   # four times real time
```

`--speed` is a multiplier of game time: 1 is real time, 2 twice as fast, 0 or no option as fast as possible. A replay sleeps only when it is ahead of the schedule given by the snapshot and event frames and the logic rate from MatchInfo (60 on the high-fps build), so a slow window is never slowed further.

**What the window looks like.** The top is a status area: the connection (with the game's pid), the game (version, protocol version, reader slot of the maximum, logic rate from the Hello, and the count of dropped frames, in red once it is above 0), the match, the frame, game time, speed, network state and this PC's FPS, and the camera as centre, size and rotation of the view box. A red line appears while the pipe is down ("Disconnected: the tables are stale, last update 12 s ago", and the tables turn gray); a green bold line shows the result once a match ended ("Match over: Alice won, Bob lost"). The window title carries the pid, map and replay file name. Below are tabs, and a footer shows the feed health: light and full snapshots per second, events per second, camera Hz, other frames per second, KB/s and the time since the last frame, in red as "NO FRAMES for N s" when the feed went quiet. A new match (a LOADING frame, or a STARTED with another match id) resets the tables, camera, speed, prediction banner and frame; a repeated STARTED of the same match does not. A reconnect keeps the tables. All of this is the library's `MatchState`: the window only turns it into rows.

| Tab | Content |
|---|---|
| Players | One row per player, joined with the MatchInfo player by `player_index`, a colour swatch next to the name, `[AI]` marking computer players, teams shown from 1, rows sorted by team then lobby slot. The **Columns** selector chooses a view: Overview (state, team, side, ELO, money, income, army, APM, score, rank; the default), Combat, Economy & tech, Production & powers, Network. The sort survives live updates, rows update in place and the selection is kept. The State column is the engine's player state (alive, defeated, left), won/lost once the match ended. **Double-click a row** for the stat card: every field of that player's MatchPlayer (including the online rating data) and latest PlayerStats as protobuf text. Light snapshots update the cheap columns and keep the heavy ones from the last full snapshot. |
| Minimap | The map drawn at its real aspect ratio from the MatchInfo world extent: each player's grid cells as translucent squares in the player's colour (more objects, more opaque), supply sources as gold circles (bigger when richer), tech structures as diamonds (owner colour, gray when neutral), start positions as numbered rings, the camera view box as a white polygon through all four corners, and recent kills (red), captures (yellow) and beacons (cyan) as dots that fade over 8 game seconds. Redrawn about 30 times a second from a timer; the grids, supply and tech are kept from the last full snapshot. |
| Events | Append-only list of the events frames, newest at the bottom, follows new rows until you scroll up, last 3000 kept; header clicks do not sort it. Filter by kind, by player and by text. Rows from the last 6 game seconds are highlighted and the Player cell is tinted with the player's colour. The Detail column decodes the state changes: radar and battle plan changes, veterancy (`Regular -> Veteran`), salvage tier, science rank and points, the old owner of a capture, a starting veterancy of a built unit. **Double-click a row** for the raw StatEvent. |
| Chat | Every chat line received, with channel, sender, audience (match channel only) and flags. |
| Lobby | The latest LobbyState: one Side cell such as `Random -> GLA` (the prediction, only while the lobby says it is valid; `Random` otherwise), the random flags, colour swatches (a `?` while a colour is random), predicted colour and start position (blank when not predicted). After LOADING the check result is shown as a green or red label, and random slots are marked `(check failed)` when the prediction did not match. |
| Match | The latest MatchInfo as key/value rows, the observers (from MatchInfo), and the MatchSummary rows when one arrives. |
| Frames | Raw log of every frame except camera frames (those are counted in the footer) with type, size and a one-line summary. |
| Code | The example-code generator, see below. |

A table with nothing to show says why in gray text. Stall snapshots never clear the Players table and show `network stall (Ns)` in the status bar with the last known speed.

### The Code tab

Pick what you want to do from the list on the left (the eight scenarios of [`examples/`](examples/src/main/java)), or right-click a Players cell or row, an Events row, a Chat row, a Lobby row, a Frames row or the Minimap and choose **Show code for this**. The tab then shows a complete, minimal, runnable Java program that does exactly that with the library, filled in with what you selected:

- a Players cell fills `track-player` with that player and that column's accessor (the Money column becomes `Format.u(player.stats().getMoney())`, Income/min `getMoneyPerMinute()`, Network columns `player.stats().getNetwork()...`);
- an Events row fills `react-to-event` with its kind (`StatEventKind.STATEVENT_CAPTURE`) and prints the detail fields of that kind (the old owner and cash bonus of a capture, the cost of a kill, the levels of a promotion, ...);
- a Chat row fills `send-chat` with its channel;
- the controls above the code (player, stat, event kind, chat channel, audience, text) change the selection at any time.

**Copy** puts the program on the clipboard and **Save as .java...** writes it to a file named like its class, ready for `java -cp <jars> Name.java`. Every template is compile-checked by a test (all Players columns, all event kinds, all channels and audiences) with `-Xlint:all`, and the files in `examples/` are the generator's output.

## How the code is organised

- `lib`: `StatsPipe` is the connection (candidates, reconnect with backoff, executor, the Hello handshake, sequence gap detection, `sendChat`, the request API); `FrameDispatcher` decodes a frame once (`decode`) and calls the matching `StatsListener` method (`deliver`), both exhaustive switches over `FrameType`, so a new frame type does not compile until handled; `MatchState` is the state layer; `DumpWriter`/`DumpReader` are the dump files. `internal` holds the envelope codec, the sequence tracker, the request limiter, the replay pacer and the Windows pipe link.
- `app`: `GuiModel` turns the library's `MatchState` into table rows and status texts without any Swing; `EdtFeed` is the reader thread to event thread hand-over (one task per burst, camera frames coalesced); `StatsGui`, `GuiTables`, `Minimap` and `CodeTab` are the Swing parts; `Examples` and `ExampleTemplates` are the generator; `FramePrinter` and `DumpStatus` are the console output.

## Changing the proto

The build regenerates the Java classes from `GeneralsMD/Code/GameEngine/Source/Common/StatsExporter.proto` on every build, so a proto change needs no edits here beyond handling new fields. Update `docs/stats-exporter.md` and, if a frame type was added, `FrameType`, `FrameDispatcher`, `StatsListener`, `MatchState`, the app's `FrameSummary` and `Schema`; if a query was added, a typed method of `StatsPipe`. If a Players column was added to the GUI, `Examples` needs an accessor for it (a test fails until it has one).

See [CHANGELOG.md](CHANGELOG.md) for the versions of the library.
