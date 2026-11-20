# Generals Online stats pipe client

A Java 11 library for the live stats pipe of a Generals Online game, a client for casters and tournament admins that shows everything the pipe delivers and steers the game, and example programs. Protocol reference: [docs/stats-exporter.md](../../docs/stats-exporter.md). This is version 3.0.0, for **protocol version 2** (a 17 byte frame header with a sequence number and a timestamp, a Hello handshake, requests and responses); it cannot talk to a game of protocol version 1, see the [CHANGELOG](CHANGELOG.md).

| Project | What it is |
|---|---|
| [`lib/`](lib) | `com.generalsonline:stats-pipe-client:3.0.0`. The library other people depend on: the generated protobuf classes, the transport, a thread safe match state, dump files and helpers. protobuf-java is its only dependency. |
| [`app/`](app) | The client: the commands `watch`, `dump`, `replay`, `chat`, `gui`, `schema`, `example` and a Swing GUI in a dark theme. It uses the library through its public API only. |
| [`examples/`](examples) | One runnable program per scenario of the GUI's Code tab, compiled against the library in every build. |

## Use the library

### Coordinates and build

Requires a JDK 11 or newer; the library, the client and the examples are compiled for Java 11 (`--release 11`), so they run on Java 11 and later. The Gradle wrapper in this folder builds everything; nothing generated is checked in.

```
cd Tools/StatsExporterClient
gradlew build                       # generate the protobuf classes, compile, build the jars
gradlew :lib:publishToMavenLocal    # install the jar, the sources jar and the javadoc jar into ~/.m2
```

Then, in another project:

```groovy
repositories { mavenLocal() }
dependencies { implementation 'com.generalsonline:stats-pipe-client:3.0.0' }   // brings protobuf-java 4.33.4
```

```xml
<dependency>
  <groupId>com.generalsonline</groupId>
  <artifactId>stats-pipe-client</artifactId>
  <version>3.0.0</version>
</dependency>
```

Without a build tool: `lib/build/libs/stats-pipe-client-3.0.0.jar` plus `protobuf-java-4.33.4.jar` on the class path. The jar is an automatic module named `com.generalsonline.stats`. The packages are `com.generalsonline.stats` (the API), `com.generalsonline.stats.proto` (the generated messages) and `com.generalsonline.stats.internal` (not for users).

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

Run it with the two jars on the class path, for example `java -cp stats-pipe-client-3.0.0.jar;protobuf-java-4.33.4.jar QuickStart.java` (`:` instead of `;` outside Windows).

### The public API

| Type | What it does |
|---|---|
| `StatsPipe` | The connection. `StatsPipe.builder()` with `.pid(int)` or `.pipe(String)` (otherwise every running game is tried, highest pid first, and a game without a free reader slot is skipped), `.listener(StatsListener)`, `.state(MatchState)`, `.executor(Executor)`, `.reconnect(boolean)`, `.reconnectBackoff(min, max)`, `.helloTimeout(Duration)` (default 10 s), `.requestTimeout(Duration)` (default 5 s), `.limitRequests(boolean)` (default off), `.build()`. Then `start()`, `close()` (it is `AutoCloseable`), `isConnected()`, `connectedPid()`, `connectedPipe()`, `hello()` / `awaitHello(Duration)` / `hasCapability(String)`, `droppedFrames()`, `clockSync()`, `sendChat(...)` and the request methods (`request`, `catalog`, `object`, `objects`, `player`, `map`, `resync`, `feedConfig`, `setFeedConfig`, `catalogAll`, `objectsAll`), see below. The builder rejects bad values (a negative pid, a blank pipe, both a pid and a pipe, a timeout under 1 ms) at once; `sendChat` rejects an empty line, a line over 1024 UTF-8 bytes and an audience the game cannot take. |
| `StatsListener` | One method per frame type (`onHello`, `onMatchInfo`, `onStatsSnapshot`, `onMatchSummary`, `onChatMessage`, `onUiState`, `onLobbyState`, `onEvents`, `onCamera`, `onViewStats`, `onInterestMap`, `onResponse`), `onFramesDropped(count)`, the lifecycle (`onConnected`, `onDisconnected(cause)`), `onFrame` for every raw frame (recorders; `DecodedFrame` has `sequence()` and `timestampMicros()`), `unknownFrame`, `decodeError` and `onError`. All have empty defaults. |
| `MatchState` | The state the library maintains for you, safe to read from any thread, see below. Feed it through `StatsPipe.Builder.state(...)` or use it as a `StatsListener` yourself. |
| `StateListener` | Change callbacks of a `MatchState`: `matchChanged`, `matchReset`, `playersChanged`, `snapshotApplied`, `eventsAdded`, `cameraChanged`, `viewStatsChanged`, `interestMapChanged`, `worldChanged`, `chatAdded`, `lobbyChanged`, `uiChanged`, `summaryChanged`, `helloReceived`, `framesDropped`. An overlay redraws from these instead of diffing. |
| `Handshake`, `ClockSync` | Helpers for the Hello (`PROTOCOL_VERSION`, `hasCapability`, `requireSupported`, `wallClockMillis(hello, timestampMicros)`, `summary`) and the estimate of the offset between the game's frame clock and yours. |
| `ProtocolVersionException`, `HelloTimeoutException`, `RequestFailedException`, `RequestTimeoutException`, `PageLimitException` | What the handshake and the request API fail with, see below. |
| `NotAllowedException` | A `RequestFailedException` for the status `NOT_ALLOWED`: the game refused an observe or replay command because of its situation (the local player plays, or no replay is running). `reason()` is the game's explanation. |
| `CameraMove` | An immutable absolute camera command: position, zoom, angle, pitch, an optional transition with easing, or a reset. See "Camera, replay and the director". |
| `Director`, `DirectorLogic`, `DirectorConfig`, `DirectorMode`, `DirectorMove`, `DirectorClock` | The automatic camera director: `DirectorLogic` decides (pure, with an injectable clock), `Director` connects it to a `MatchState` and a pipe (`status()` says what it does, `lastMove()` where it went last and why). See below. |
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

### Camera, replay and the director

New in 2.1.0 (still protocol version 2: the new messages are additive and announced as capabilities in the Hello). A tournament caster's tool can move the game's camera, control a replay and let an automatic director follow the action.

**Rules of the game side.** Camera and follow commands are accepted in any running match, also for an active player; with no match running the game answers `UNAVAILABLE`. Observing a player needs an **observer** of a match or a **replay playing back**, otherwise the game answers `NOT_ALLOWED` and the future fails with a `NotAllowedException`. Replay commands (pause, speed, fast forward) work during a **replay playback** only: pausing or slowing a live network game would desynchronize it, so the game refuses. Nothing here touches the simulation; the camera and the pace of a replay are client side. Check `pipe.hasCapability("camera.control")` (also `camera.follow`, `camera.source`, `replay.control`, `observe.player`, `view_stats`, `interest_map`, `status.not_allowed`).

```java
pipe.moveCamera(CameraMove.to(1800, 2400).zoom(1.2).over(Duration.ofSeconds(2)).easeInOut());   // pan with a transition
pipe.moveCamera(CameraMove.set().angle(0.5).pitch(0.9));                                          // any subset, at once
pipe.resetCamera();                                                                                // the game's default angle, pitch and zoom
pipe.followUnit(unitId);                 pipe.tetherUnit(unitId, 20);        pipe.unfollowUnit();
pipe.pauseReplay();  pipe.resumeReplay();  pipe.setReplaySpeed(0.5);  pipe.setReplayFastForward(true);
pipe.observePlayer(3);                   pipe.observeNobody();               // whose units, shroud and radar the view shows
```

- A transition runs on wall-clock time (a paused game still pans). A position command ends a lock on a unit. The `Camera` frame says what moved the camera since the previous frame (`change_source`: `OTHER` the game, `USER` a human, `PIPE` a command), carries a running count of user camera actions (`user_input_count`), the lock in force (`follow_mode`, `follow_object_id`) and, for a lock a command made, why it ended (`follow_ended`: `OBJECT_GONE`, `USER`, `COMMAND`). That is how a tool notices a caster taking the camera by hand, and a unit dying under a follow.
- Replay speed: the game runs one logic frame per displayed frame at most, so `setReplaySpeed` can slow a replay down or keep it at normal speed, and the answer's `speed_capped` says when the display frame rate limit kept it lower; `setReplayFastForward(true)` is the game's own fast forward (as fast as the machine runs) for anything faster.
- With the replay camera option on, a camera recorded in the replay file overrides camera commands.

**What is in view.** `ViewStats` frames (`StatsListener.onViewStats`, `MatchState.viewStats()`; about four a second, `FeedConfig.view_stats_interval_ms`, 0 turns them off) describe per player what is inside the four ground corners of the local camera: the value (sum of build costs) and the effective value (cost times health fraction), the value and count by class (infantry, vehicles, aircraft, structures), veterancy counts, **notable units** (heroes and anything costing at least `notable_cost`, 2000 by default; with health, veterancy and the portable structure an Overlord or Helix carries), superweapon structures with their timers, and, over rolling windows of game time, the value killed and lost (`trade_window_ms`, 15 s) and the damage dealt per second (`intensity_window_ms`, 5 s), counting only what happened inside the view.

**The interest map.** Off by default; `setFeedConfig(FeedConfigQuery.newBuilder().setInterest(true).build())` turns on `InterestMap` frames (`onInterestMap`, `MatchState.interestMap()`, about twice a second): a grid over the playable extent (`grid_resolution`) with a score per cell from the damage dealt there and the value destroyed there (both decaying with a 6 s half life) and from opposing armies standing close together, plus the **pending highlights**: superweapon launches with the target and, where the game knows it, the impact frame (a particle cannon keeps a schedule; a neutron missile's flight time is an estimate, flagged `impact_estimated`), hero deaths and the loss of structures that cost 1000 or more. Every highlight has an id and an expiry frame.

**The director.** `Director` watches a `MatchState` and moves the camera:

```java
MatchState state = new MatchState();
StatsPipe pipe = StatsPipe.builder().state(state).build().start();
DirectorConfig config = DirectorConfig.defaults().toBuilder().mode(DirectorMode.DIRECTOR).minDwell(Duration.ofSeconds(6)).build();
Director director = Director.start(pipe, state, config);     // switches the interest feed on and steers; close() it when done
```

| Mode | Behaviour |
|---|---|
| `HOTSPOT` | Goes to the best cell. Stays at least `minDwell` (5 s); switches only to a cell scoring `switchRatio` (1.5) times what the current place scores now. A short way is panned, a way over `panLimit` (1500 units) is a cut. |
| `HIGHLIGHTS` | Idle until a highlight is pending; the camera is in place `highlightLead` (4 s) before a known impact, stays `highlightHold` (3 s) after it, then returns to where it was. |
| `OVERVIEW` | When it has been quiet for `quietDelay` (4 s) it tours every active player's base and army (`overviewDwell`, 6 s each); a place scoring `fightScore` (400) hands over to hotspot following until it is quiet again. |
| `DIRECTOR` | All three: highlights interrupt (a superweapon before a hero before a structure), hotspots are followed, the overview fills the quiet. |

The director pauses when a `Camera` frame says a human moved the camera (`change_source` `USER`, or the user action count grew) and takes over again after `resumeDelay` (10 s) without input, or at once with `resume()`. When the game refuses (`NotAllowedException`) it pauses itself and `lastError()` holds the exception. `DirectorLogic` is the pure decision part: it takes interest maps and camera frames and the time of each call, and `poll(now)` returns the moves; with a manual `DirectorClock` it is deterministic, which is how it is tested. `DirectorConfig` is immutable (`defaults().toBuilder()...build()`).

### Lobby control

New in 2.3.0 (protocol version 2, announced by the capability `lobby.control`). A tool can change the game setup lobby the user sits in: skirmish, LAN or Generals Online. The game does not poke the lobby's data, it **operates the setup screen's own controls** (the slot dropdowns, the start position buttons, the check box, the Start and Accept buttons), so the screen's handlers validate the change and send it on as usual, and the screen refreshes itself.

```java
pipe.addAi(3, LobbyControl.AiLevel.MEDIUM);          pipe.openSlot(4);        pipe.closeSlot(5);       pipe.kickPlayer(1, false);
pipe.setFaction(3, "China");                         pipe.setColor(3, 0xFF3060FF);   pipe.setTeam(3, 1);  pipe.setStartPosition(3, 2);
pipe.changeMap("maps\\tournament desert\\tournament desert.map");     pipe.setStartingCash(20000);    pipe.limitSuperweapons(true);
pipe.acceptLobby();                                  pipe.startGame();        // a client accepts, the host starts
pipe.lobbyControl(LobbyControl.faction(0, "Random"));   // the same, built by LobbyControl; slots are 0-based as in LobbyState
```

- **The screen's rules apply.** Only the host changes slots, the map, the starting cash and the superweapon limit; a client changes its own slot and accepts; the host also sets the side, color, team and start position of an AI slot. What the screen would not allow (a locked or hidden control, a colour another slot holds, a button that is disabled) fails with a `NotAllowedException` carrying the reason, and nothing changes. A bad argument fails with `RequestFailedException` (`BAD_REQUEST`, or `NOT_FOUND` for an unknown side or map), and with no setup screen up the status is `UNAVAILABLE`. Nothing is done during a match.
- **The result** (`LobbyControlResult`) tells the `kind` of lobby and the `route`: `APPLIED` (the lobby changed at once: skirmish, a LAN host), `REQUESTED_FROM_HOST` (a LAN client asked the host) or `REQUESTED_FROM_SERVICE` (Generals Online: the lobby service decides and echoes the change). The `LobbyState` frame that follows is the confirmation.
- `startGame()` presses the Start button: in skirmish it starts the game, in LAN and online it starts when every human has accepted and otherwise only announces the intent (the result's note says so). `setLobbyGameSpeed` is the skirmish speed slider.
- The client's **Lobby** section has the controls (a slot spinner and buttons under the table, and a right-click menu on a slot) and a "Show code for this" for the `lobby-control` example.

### Threading

- A pipe has one daemon reader thread that connects, reads and decodes. By default the listener and the state are called on that thread, so keep callbacks short.
- `.executor(Executor)` moves delivery: each frame becomes one task on that executor, in order (it must be a single threaded event loop, for example `SwingUtilities::invokeLater` or JavaFX `Platform::runLater`). The reader thread then only decodes, and the state is updated and its change callbacks run on that executor.
- `MatchState` is thread safe: every method may be called from any thread and returns immutable values (value classes, protobuf messages, unmodifiable lists), so a UI thread can keep what it read.
- A callback that throws costs only its frame: the exception goes to `onError` and reading continues. Only a pipe or framing failure disconnects, and `onDisconnected` receives the real cause. The library never prints and never exits the process.
- Camera frames arrive up to 120 times a second. If you only need the latest one, store it and draw from your own timer instead of repainting per frame.
- The game serves up to `Hello.max_readers` (4) readers per pipe. A client that finds every slot taken gets no connection; the pipe then tries the next game, or waits and retries.

### Example programs

[`examples/src/main/java`](examples/src/main/java) has one complete program per scenario, compiled against the library on every build. Each can also be run on its own: `java -cp stats-pipe-client-3.0.0.jar;protobuf-java-4.33.4.jar PrintEveryFrame.java`.

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
| [`QueryCatalog`](examples/src/main/java/QueryCatalog.java) | Page through the object catalog and look a template up by name. |
| [`InspectObject`](examples/src/main/java/InspectObject.java) | Look a live object up by id and handle `NOT_FOUND`. |
| [`CameraControl`](examples/src/main/java/CameraControl.java) | Pan the camera with a transition, follow a unit, release and reset; handles `NOT_ALLOWED`. |
| [`ReplayControl`](examples/src/main/java/ReplayControl.java) | Pause, resume, set a speed (and see it capped) and fast forward a replay. |
| [`ViewStatsShare`](examples/src/main/java/ViewStatsShare.java) | Print each player's share of the value in view from the view statistics. |
| [`RunDirector`](examples/src/main/java/RunDirector.java) | Run the `Director` in a mode and print what it does. |

These files are the output of the client's code generator for its default selection (`example <name>`), and the build compiles each of them with `-Xlint:all`. The GUI's **Code** tab and the `example` command fill the same templates with what you selected, for example the Players column or the event kind.

## The client

Run with `gradlew :app:run --args="<command> ..."` or the installed script (`gradlew :app:installDist`, then `app/build/install/stats-exporter-client/bin/stats-exporter-client`). Options start with `--`, take their value as the next argument (or `--name=value`) and go before the positional arguments; `--` ends the options. A mistake prints one line saying what is wrong plus the usage, never a stack trace.

`--pid N` or `--pipe PATH` picks a pipe. Without them the client lists the stats pipes and tries them **highest pid first**, which is only a guess at the newest game (Windows reuses pids), not a rule. The game serves a few readers per pipe, so when every slot of that pipe is taken the next one is tried, and the first pipe with a free slot wins. The pid of the connected game is shown in the GUI status bar and window title.

| Command | What it does |
|---|---|
| `watch [--only KINDS]` | Connect (waiting for the game if needed), reconnect on loss, print the feed. |
| `dump [--overwrite] [--no-camera] <file>` | Record the raw frames to a file and print one status line per second. |
| `replay <file>` | Print a recorded dump without a game, every frame. |
| `chat [--audience A] <channel> <text...>` | Send one chat line. |
| `gui [--replay FILE [--speed X]]` | Show the feed in a window (see below). With `--replay` the window plays a recorded dump instead of connecting to a game. |
| `schema` | Print the frame table and every message and field. |
| `example [<name>] [--player P] [--column C] [--kind K] [--channel CH] [--audience A] [--text T] [--template N] [--side S] [--object ID] [--x X] [--y Y] [--zoom Z] [--speed R] [--mode M]` | Print a complete example program for a scenario (no name lists them). |

```
stats-exporter-client watch --only events,chat
stats-exporter-client dump match.dump
stats-exporter-client replay match.dump
stats-exporter-client chat --audience allies match "gg wp"
stats-exporter-client example track-player --column "Income/min" --player Alice > TrackPlayerStat.java
stats-exporter-client example react-to-event --kind capture
stats-exporter-client example camera-control --x 900 --y 1200 --object 77 > CameraControl.java
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

`example` lists the scenarios; `example <name>` prints the program, which compiles against the library as it is. `--player` and `--column` fill `track-player` (the column is any column of the Players table, mapped to its accessor, for example `Money` to `player.stats().getMoney()`), `--kind` fills `react-to-event` (`KILL`, `CAPTURE`, ... or the full `STATEVENT_` name), `--channel`, `--audience` and `--text` fill `send-chat`, `--template` and `--side` fill `query-catalog`, `--object` fills `inspect-object` and `camera-control`, `--x`, `--y` and `--zoom` fill `camera-control`, `--speed` fills `replay-control` and `--mode` (`hotspot`, `highlights`, `overview`, `director`) fills `director`.

## GUI

**Recording.** Record in the header writes the live feed to `Documents\GeneralsOnline\Recordings\<date>-<map>.dump` (a new file every time, never an overwrite) and shows the time and size while it runs. Recordings opens one, or one of the newest, and the window plays it with play and pause, a speed and Back to live; `gui --replay FILE` opens one directly. A recording is not a game replay (.rep): it holds what this client received, so it can be watched here and read by the library (`DumpReader`), not played in the game.

`gui` opens a Swing window (a dark theme from FlatLaf, the only dependency of the client) that shows the feed as tables and a minimap, for watching a match without a console:

```
stats-exporter-client gui                          # connect to the running game, reconnect if it restarts
stats-exporter-client gui --pid 1234               # a specific game process
stats-exporter-client gui --replay match.dump      # play a recorded dump, as fast as possible
stats-exporter-client gui --replay match.dump --speed 1   # real time (game time / MatchInfo logic rate)
stats-exporter-client gui --replay match.dump --speed 4   # four times real time
```

`--speed` is a multiplier of game time: 1 is real time, 2 twice as fast, 0 or no option as fast as possible. A replay sleeps only when it is ahead of the schedule given by the snapshot and event frames and the logic rate from MatchInfo (60 on the high-fps build), so a slow window is never slowed further.

**What the window looks like.** A header strip across the top has the state of the connection as a coloured light and a word (green live, amber while it waits for a game, red once the pipe was lost) with the game's pid, then the screen the game is on, the match (map and phase), the game time with the frame, the speed (or the network stall) and the dropped frames, in red once above 0. A banner under it appears while the pipe is down ("Disconnected: the tables are stale, last update 12 s ago", and the tables turn gray) and, in green, once a match ended ("Match over: Alice won, Bob lost"). The window title carries the pid, map and replay file name. On the left a rail groups the sections (Live: Players, Minimap, In view; Feed: Events, Chat, Frames; Match: Lobby, Match; Control: Camera, Director, Query; Develop: Code) with a count beside the logs; the page of the open section fills the rest. A footer shows the feed health: light and full snapshots per second, events per second, camera Hz, other frames per second, KB/s and the time since the last frame, in red as "NO FRAMES for N s" when the feed went quiet, then the frame, the network state, the snapshot count, this PC's FPS and the camera as centre, size and rotation of the view box. A new match (a LOADING frame, or a STARTED with another match id) resets the tables, camera, speed, prediction banner and frame; a repeated STARTED of the same match does not. A reconnect keeps the tables. All of this is the library's `MatchState`: the window only turns it into rows. The window resizes down to 960 by 620 without clipping: toolbars wrap, the forms of the control sections stay in cards, and a table scrolls sideways when its columns do not fit.

| Section | Content |
|---|---|
| Players | One row per player, joined with the MatchInfo player by `player_index`, a colour swatch next to the name, `[AI]` marking computer players, teams shown from 1, rows sorted by team then lobby slot. The **Columns** selector chooses a view: Overview (state, team, side, ELO, money, income, army, APM, score, rank; the default), Combat, Economy & tech, Production & powers, Network. The sort survives live updates, rows update in place and the selection is kept. The State column is the engine's player state (alive, defeated, left), won/lost once the match ended. **Double-click a row** for the stat card: every field of that player's MatchPlayer (including the online rating data) and latest PlayerStats as protobuf text. Light snapshots update the cheap columns and keep the heavy ones from the last full snapshot. |
| Minimap | The map drawn at its real aspect ratio from the MatchInfo world extent: each player's grid cells as translucent squares in the player's colour (more objects, more opaque), supply sources as gold circles (bigger when richer), tech structures as diamonds (owner colour, gray when neutral), start positions as numbered rings, the camera view box as a white polygon through all four corners, and recent kills (red), captures (yellow) and beacons (cyan) as dots that fade over 8 game seconds. Redrawn about 30 times a second from a timer; the grids, supply and tech are kept from the last full snapshot. The **Interest map** switch turns the interest feed on for this connection and draws the heat map (yellow to red by score) and a marker for each pending highlight (a superweapon launch as two red rings with the time to the impact, a hero death as a white cross, a lost structure as an orange square); below the map a list of the pending highlights with their impact time and how long they stay. Without a game (a replay file) the switch only shows or hides frames the dump holds. |
| In view | What the camera sees, per player, from the `ViewStats` frames: value, health weighted value, value by class, veterancy counts, value killed and lost and damage per second over their windows, with a **bar for each player's share of the value in view**; the **fight intensity** (quiet, light fighting, a skirmish, heavy fighting by the total damage per second); a table of the **notable units** (heroes and expensive units, with health, veterancy and the portable structure they carry; right-click: **Inspect object**, **Show code for this**) and of the **superweapon structures in view** with charge and time to ready. |
| Events | Append-only list of the events frames, newest at the bottom, follows new rows until you scroll up, last 3000 kept; header clicks do not sort it. Filter by kind, by player and by text. Rows from the last 6 game seconds are highlighted and the player's name is drawn in the player's colour. The Detail column decodes the state changes: radar and battle plan changes, veterancy (`Regular -> Veteran`), salvage tier, science rank and points, the old owner of a capture, a starting veterancy of a built unit. **Double-click a row** for the raw StatEvent. |
| Chat | The conversation, one tab per channel (match, game lobby, room, LAN lobby, LAN game) with the number of lines you have not seen. Sender names wear the player's colour, the audience of a match line is shown, the lines of the game itself are set apart, a search narrows the view, and the view follows new lines until you scroll up. Below it a composer offers only the channel the game delivers to on its current screen (and the audience for a match line), counts the 1024 bytes of a line, sends on Enter, recalls earlier lines with Up and Down, passes game commands such as `/help` through, and has quick lines you edit in Settings. The game gives no reply to a chat line: your own line is confirmed when the game echoes it, and shown as not confirmed otherwise. |
| Lobby | The latest LobbyState: one Side cell such as `Random -> GLA` (the prediction, only while the lobby says it is valid; `Random` otherwise), the random flags, colour swatches (a `?` while a colour is random), predicted colour and start position (blank when not predicted). After LOADING the check result is shown as a green or red pill, and random slots are marked `(check failed)` when the prediction did not match. |
| Match | The latest MatchInfo as key/value rows next to **the game's Hello** (version, build, pid, reader slot, limits, capabilities), the observers (from MatchInfo), and the MatchSummary rows when one arrives. |
| Query | The questions a reader can ask, in five sections. **Catalog**: the game's object templates with name, side, kind and buildable filters and **paging** (rows per page, Previous and Next; right-click a row: **Look up this name**, **Show code for this**). **Object**: an inspector by object id (also from **Inspect object** in the right-click menu of an Events row, for the object or the killer, and of a notable unit), with **Follow in the camera**. **Player**: money, rank, sciences, upgrades, powers and what the player can build. **Map**: extent, start positions and waypoints, and the **minimap image** drawn with the start positions. **Feed**: this reader's feed configuration as controls (frames, cadences, grid resolution, view statistics windows), **Apply**, **Reset to defaults**, **Read from the game**, and **Resync**. Every answer shows its status; a failure says why. |
| Camera | **Camera**: position, zoom, angle and pitch (degrees) with a transition and its easing, Move, Reset camera, Cancel transition, Use the current camera. **Follow a unit**: object id, follow or tether (with its distance), jump to the unit, Follow, Unfollow, Use the inspected object. **Replay**: Pause, Resume, a speed, Set speed, fast forward. **Observer view**: pick a player or Nobody. The game accepts camera and follow commands in any running match (observing a player needs an observer or a replay, replay control a replay): its refusal (**NOT ALLOWED**) is shown in bold amber with the reason, apart from your own mistakes in red. A **Camera now** card shows the camera frames: where it looks, who moved it (the game, a command or a human), the user action count, the lock and a running transition. |
| Director | Runs the library's `Director` against the connection: the mode, Start and Stop, Pause and Resume now, and every tunable (dwell, switch ratio, minimum and fight score, quiet delay, pan limit, lead and hold, overview dwell, resume delay) applied while it runs. It shows the state, the place it went to last and why, the highlights pending, an amber **PAUSED BY A HUMAN** banner when someone moved the camera, and the problem when the game refused. |
| Control | What a caster does during a match, with its state in view: the replay as a media player (play, pause, speeds, fast forward), one button per player in the player's colour to observe, follow what is selected in the game, eight saved camera positions, and the director. A control that cannot work now is disabled and says why (no game, no match, not a replay, not an observer). Keys, while the window has the focus and no text box is in use: Space pauses a replay, Alt+Left and Alt+Right change its speed, F toggles fast forward, 1 to 8 observe a player, 0 the all-seeing view, Alt+1 to 8 fly to a saved position, Ctrl+Alt+1 to 8 save the camera, Ctrl+R records, Ctrl+1 to 9 open a section. |
| Settings | The connection (process id, pipe, connect on open), what the game sends this window (applied by the game per connection, sent again from the saved settings on every connect), the recordings folder, quick chat lines, the opening section, and what the game reported about itself. Saved across runs. |
| Frames | Raw log of every frame except camera frames (those are counted in the footer) with type, size and a one-line summary. |
| Code | The example-code generator, see below. |

A table with nothing to show says why in plain text in its middle. Stall snapshots never clear the Players table and show the network stall in the header with the last known speed.

### The Code section

Pick what you want to do from the list on the left (the fifteen scenarios of [`examples/`](examples/src/main/java)), or right-click a Players cell or row, an Events row, a Chat row, a Lobby row, a Frames row, the Minimap, a catalog row, the object text, a notable unit or an In view row, the camera or replay card of the Camera section or the Director section and choose **Show code for this**. The tab then shows a complete, minimal, runnable Java program that does exactly that with the library, filled in with what you selected:

- a Players cell fills `track-player` with that player and that column's accessor (the Money column becomes `Format.u(player.stats().getMoney())`, Income/min `getMoneyPerMinute()`, Network columns `player.stats().getNetwork()...`);
- an Events row fills `react-to-event` with its kind (`StatEventKind.STATEVENT_CAPTURE`) and prints the detail fields of that kind (the old owner and cash bonus of a capture, the cost of a kill, the levels of a promotion, ...);
- a Chat row fills `send-chat` with its channel;
- a catalog row fills `query-catalog` with its name and side; the object text and a notable unit fill `inspect-object` with the id; the camera section fills `camera-control` with the position, zoom and unit of its fields, the replay section `replay-control` with its speed, the Director section `director` with its mode;
- the controls above the code (player, stat, event kind, chat channel, audience, text, template, side, object id, camera position and zoom, replay speed, director mode) change the selection at any time.

**Copy** puts the program on the clipboard and **Save as .java...** writes it to a file named like its class, ready for `java -cp <jars> Name.java`. The files in `examples/` are the generator's output for the default selection and are compiled with `-Xlint:all` by the build; the programs use nothing newer than Java 11.

## How the code is organised

- `lib`: `StatsPipe` is the connection (candidates, reconnect with backoff, executor, the Hello handshake, sequence gap detection, `sendChat`, the request API); `FrameDispatcher` decodes a frame once (`decode`) and calls the matching `StatsListener` method (`deliver`), both switches over `FrameType`; `MatchState` is the state layer; `DumpWriter`/`DumpReader` are the dump files. `internal` holds the envelope codec, the sequence tracker, the request limiter, the replay pacer and the Windows pipe link.
- `app`: `GuiModel` turns the library's `MatchState` into table rows and status texts without any Swing; `EdtFeed` is the reader thread to event thread hand-over (one task per burst, camera frames coalesced); `Theme` (the palette, the type and the FlatLaf defaults) and `Ui` (cards, forms, wrapping toolbars, buttons and pills built from it) are the one place of the look; `HeaderBar`, `NavRail`, `StatsGui`, `GuiTables`, `Minimap` and `CodeTab` are the Swing parts, with `MinimapTab`, `QueryTab`, `CameraTab`, `ViewStatsTab` and `DirectorTab` for the panels that read or steer the game (`PipeCommands` sends their requests and turns a failure into a sentence; `QueryText`, `CatalogPager`, `ViewStatsModel` and `DirectorModel` are their parts without Swing); `Examples` and `ExampleTemplates` are the generator; `FramePrinter` and `DumpStatus` are the console output.

## Changing the proto

The build regenerates the Java classes from `GeneralsMD/Code/GameEngine/Source/Common/StatsExporter.proto` on every build, so a proto change needs no edits here beyond handling new fields. Update `docs/stats-exporter.md` and, if a frame type was added, `FrameType`, `FrameDispatcher`, `StatsListener`, `MatchState`, the app's `FrameSummary` and `Schema`; if a query was added, a typed method of `StatsPipe`. If a Players column was added to the GUI, `Examples` needs an accessor for it (the Code section lists the columns from `Examples`).

See [CHANGELOG.md](CHANGELOG.md) for the versions of the library.
