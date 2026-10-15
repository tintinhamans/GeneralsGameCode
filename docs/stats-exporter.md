# Live stats pipe

The Generals Online client streams live match data to out-of-process readers over a Windows named pipe, and answers their queries. This document is the authoritative protocol reference for **protocol version 2** (a breaking change from version 1: new frame header, a Hello handshake, several readers, requests). The wire schema is `GeneralsMD/Code/GameEngine/Source/Common/StatsExporter.proto`; a Java library (`com.generalsonline:stats-pipe-client`, with a test client and example programs) lives in `Tools/StatsExporterClient/`.

## Transport

- Pipe name: `\.\pipe\generalsonline-stats-<pid>`, where `<pid>` is the game process id. List `\.\pipe\` and match the prefix to find running games.
- The game is the pipe server (overlapped, byte mode, duplex). The readers are the clients. Up to **4 readers** can be connected at once, each with its own outbound queue, sequence numbers, resync state and feed configuration (see Multiple readers). A fifth connect attempt fails (`ERROR_PIPE_BUSY`) until a reader leaves.
- Access: the pipe rejects remote clients (`PIPE_REJECT_REMOTE_CLIENTS`) and carries an explicit security descriptor whose only entry grants full access to the user the game runs as (the user SID of the process token). Other accounts cannot open it; neither can a reader at a lower integrity level than the game, as for any object created by a process at a higher one. If the descriptor cannot be built the pipe is not created.
- The pipe is always on, except in headless runs and replay-simulation processes, where no pipe exists. There is no setting.
- A reader that disconnects is simply dropped; the game keeps running and accepts the next reader. Readers should reconnect with backoff.

## Framing

The two directions use different envelopes.

**Outbound (game to reader), 17 header bytes, all little-endian:**

| Bytes | Meaning |
|---|---|
| 4 | body length, unsigned 32-bit |
| 1 | frame type |
| 4 | sequence number, unsigned 32-bit (see Sequence numbers and timestamps) |
| 8 | timestamp in microseconds, unsigned 64-bit (see Sequence numbers and timestamps) |
| n | protobuf body (`n` = body length) |

**Inbound (reader to game), 5 header bytes:**

| Bytes | Meaning |
|---|---|
| 4 | body length, unsigned 32-bit little-endian |
| 1 | frame type |
| n | protobuf body |

The length does not include the header bytes. A body length of 0 or over the cap is a framing error: the game drops the reader (inbound), and a reader must treat it as a broken stream (outbound). Caps: outbound 262144 bytes, inbound 4096 bytes. The inbound header has no sequence or timestamp: requests are matched by the `id` in their body, and nothing inbound needs ordering metadata.

A protobuf message whose fields all hold their default values encodes to zero bytes, which this framing cannot carry. The game therefore never sends an empty body: where a frame can be all defaults (`LobbyState` with `in_lobby = false`, `UiState` with screen UNKNOWN) it writes field 1 explicitly, giving the 2-byte body `08 00`. Every protobuf library decodes that as the default value. Readers must likewise never send an empty body; a `ChatSend` always has non-empty text and a `Request` always has a query set, so this cannot happen for a valid message.

### Frame types

| Type | Direction | Message | When |
|---|---|---|---|
| 0x09 | game to reader | `Hello` | the first frame of every connection (sequence 0) |
| 0x01 | game to reader | `MatchInfo` | match phase changes (LOADING, STARTED, ENDED) and on connect for a running match |
| 0x02 | game to reader | `StatsSnapshot` | light every 100 ms of game time, full every 250 ms by default, during a match (after STARTED) |
| 0x03 | game to reader | `MatchSummary` | end of match, scores |
| 0x04 | game to reader | `ChatMessage` | each chat line shown to the local player |
| 0x05 | game to reader | `UiState` | the screen the game is on; the first frame after the Hello |
| 0x06 | game to reader | `LobbyState` | game setup lobby changes; `in_lobby = false` on leaving |
| 0x07 | game to reader | `Events` | the discrete events of one logic frame, sent at the end of that frame (after STARTED) |
| 0x08 | game to reader | `Camera` | the local camera and minimap view box, from the render loop whenever it changed (after STARTED) |
| 0x0A | game to reader | `Response` | the answer to one `Request`, sent only to the reader that asked |
| 0x10 | reader to game | `ChatSend` | send a chat line |
| 0x11 | reader to game | `Request` | a query (see Requests and responses) |

Unknown frame types must be skipped by length, so the protocol can grow. Inbound types other than 0x10 and 0x11 are skipped by the game.

## Handshake (Hello)

The first frame on every connection is a `Hello` (type 0x09, sequence 0). The game sends it from its main thread within a render frame of the connect, so a reader that connects while the game is blocked in a map load gets it when the load ends. Nothing else is sent to a reader before its Hello, and a `Request` that arrives before the Hello was queued is dropped.

| Field | Meaning |
|---|---|
| `protocol_version` | 2 for this document. A reader must refuse a version it does not know. |
| `game_version`, `game_build` | the game's human-readable version, and the git version plus build time; for logs and bug reports |
| `process_id` | the game process, also the pipe name suffix |
| `logic_frames_per_second` | the build's logic rate (30, or 60 for the high-fps server build); game seconds are frame / this. Also on every `MatchInfo`, but known before any match here |
| `capabilities` | feature names, see below; ignore the ones you do not know |
| `max_requests_per_second`, `max_request_burst` | the request rate limit of this reader (20 per second sustained, bursts of 40) |
| `max_page_size` | the largest `page_size` a paged query honours (200) |
| `reader_slot`, `max_readers` | this connection's slot (0..3) and the number of slots (4) |
| `epoch_unix_ms` | the wall clock in milliseconds since the Unix epoch at frame timestamp 0, so a reader can turn frame timestamps into wall-clock times |
| `queue_capacity_bytes` | the outbound queue of this reader (1 MiB); a reader that falls this far behind loses frames |

Capabilities in this version: `frame.sequence`, `frame.timestamp`, `multi_reader`, `chat.send`, `request.catalog`, `request.object`, `request.objects`, `request.player`, `request.map`, `request.resync`, `request.feed_config`, `map.minimap_rgba`, `event.object_ids`, `event.kill_detail`, `feed.per_reader`. A capability is a promise that the matching frame, field or request exists; a later version can add names without changing `protocol_version`.

## Sequence numbers and timestamps

Every outbound frame carries a sequence number and a timestamp in its header.

- **Sequence.** Per reader and connection, starting at 0 with the Hello and counting every frame the game queues for that reader. A frame that is dropped because that reader's queue is full still uses its number, so **a gap in the sequence means dropped frames**: `received - expected` frames were lost. The counter is an unsigned 32-bit value and wraps around (about 4 billion frames); compare with unsigned arithmetic. Frames that were never meant for a reader (another reader's responses, frames produced before its Hello) do not use its numbers. A reader that sees a gap in `StatsSnapshot`s needs nothing special (the next snapshot recovers the cumulative numbers); a gap that swallowed `Events` means those events are lost, and `Resync` (below) refreshes the state.
- **Timestamp.** Microseconds on the exporter clock, a monotonic clock (QueryPerformanceCounter) that starts when the exporter does, near process start. It is taken when the frame was produced (not when it was written), so `arrival time - timestamp` measured on the reader side, minus the smallest such difference seen (the clock offset), is the delivery latency of the frame, and timestamp differences give exact frame spacing. `Hello.epoch_unix_ms + timestamp / 1000` is the wall clock time the frame was produced at. `Camera.timestamp_us` is on the same clock.

## Multiple readers

Up to four readers can be connected at once. Each reader has its own state:

- its own outbound queue (1 MiB, with 64 KiB reserved for one-shot frames), so a reader that does not read loses only its own frames. Writes to the pipe are overlapped and never wait for a reader, so a stalled reader cannot delay the game or the other readers;
- its own sequence counter and Hello;
- its own resync: a new reader gets the connect-time frames, a reader that lost a one-shot frame gets the resync, a `Resync` request refreshes one reader; the others receive nothing extra;
- its own feed configuration (see Feed configuration), reset to the defaults on every connection;
- its own inbound frames: `ChatSend` mailbox (8 entries), request queue (16 entries) and request rate limit, and `Response`s go only to the requester.

Frames that are the same for everyone are built once and queued for every reader. Readers whose feed configuration asks for different full snapshots cost one more object walk per distinct configuration (at most four).

## Requests and responses

A reader sends a `Request { id, query }` (inbound type 0x11) and receives a `Response { id, status, message, result }` (outbound type 0x0A) carrying the same `id`. The reader chooses the `id`; use a distinct one for every request in flight (0 is allowed but cannot be told apart from "unknown" when a request cannot be decoded).

- **Threading.** The pipe thread only decodes the framing, rate limits and queues the request. The main thread answers it once per render frame (at most 4 per frame, the rest wait), reading game state and changing nothing: no random numbers are drawn, no commands are issued, no object is touched. Because of that, a query is answered also while the game is paused or in a menu, and the answer describes the state of the moment it ran.
- **Statuses.** `OK`; `BAD_REQUEST` (undecodable, no query set, a value out of range); `NOT_FOUND` (no such object, player or template); `RATE_LIMITED` (the reader exceeded its rate or has 16 requests waiting; nothing was done, retry later); `UNAVAILABLE` (the data does not exist right now: no match running, no map loaded); `TOO_LARGE` (the result would not fit a frame: ask for a smaller page). `message` carries a short reason for any status other than OK. A request that cannot be decoded is answered with id 0.
- **Rate limit.** A token bucket per reader: 20 requests per second sustained, bursts up to 40. Requests are answered in arrival order per reader and in turn between readers. There is no answer to a request that arrives before the Hello was queued, and none when the reader's queue is full (the sequence shows the gap), so readers should time requests out.
- **Paging.** Queries that can return many rows take a `PageRequest { cursor, page_size }` and answer with a `PageInfo { has_more, next_cursor, total }`. Start with cursor 0, pass `next_cursor` back while `has_more`. The cursor is opaque: a template index for the catalog, an object id for objects. `page_size` 0 means the default, anything above `Hello.max_page_size` is lowered to it.

| Query | Result | Notes |
|---|---|---|
| `CatalogQuery` | `CatalogResult` | One `CatalogEntry` per object template in the engine's template order: internal name, localized display name, side, cost, build time in seconds, the KindOf flags that matter (`STRUCTURE`, `INFANTRY`, `VEHICLE`, `AIRCRAFT`, `HERO`, `DOZER`, `HARVESTER`, `SALVAGER`, `TRANSPORT`, `COMMANDCENTER`, `SUPPLY_SOURCE`, `TECH_BUILDING`, `CAN_ATTACK`, `IMMOBILE`, `PROJECTILE`, `MINE` and the `FS_*` faction structure kinds), whether it is buildable, the energy it produces or consumes and the build button's icon. Filters `name`, `side`, `kind_of`, `buildable_only` combine with AND. Default page 100. The cost is the template's base cost before any player's handicap. `icon` is unset when no command button builds the template |
| `ObjectQuery` | `ObjectDetail` | One object by id: template, display name, owner (-1 neutral), position and facing, health and max health, veterancy, the ObjectStatus flags that are set, construction state, contained object ids (capped at 64), the object it is inside, its producer, completed object upgrades, the current weapon, the object or position it is attacking. `NOT_FOUND` for an unknown or removed id |
| `ObjectsQuery` | `ObjectsResult` | Objects in a world rectangle (`has_area`, `min_*`/`max_*`) and/or owned by a player (`filter_player`, `player_index`, -1 = neutral), optionally of one `kind_of`, in ascending id order. Compact rows (id, template id, owner, x, y, health percent, veterancy, flags) plus one `TemplateRef` per template used on the page, which maps `template_id` to a name. The cursor is an object id, so objects that are created or destroyed between two pages never repeat or shift a page. Effectively dead objects are skipped unless `include_dead`. Default page 100 |
| `PlayerQuery` | `PlayerDetail` | One player by index: name, side, money, rank level and rank points (current and for the next rank), unspent science points, owned sciences, the sciences that can be bought now (`purchasable`, with display name and cost) and the ones that need more points or prerequisites (`locked`), completed player upgrades, the special powers the player can fire with their readiness (the same test as the snapshot's `readiness`), and the names of the templates the player can build right now (prerequisites met, own side, capped at 400). `NOT_FOUND` for an index the match does not have |
| `MapQuery` | `MapDetail` | Map name, the playable extent, every waypoint (capped at 512), the start positions (the `Player_<n>_Start` waypoints, numbered from 0 like the lobby), and with `include_minimap` the radar's terrain image |
| `ResyncQuery` | `ResyncResult` | The game sends this reader `UiState`, `LobbyState`, MatchInfo STARTED (if a match runs) and a full `StatsSnapshot` again, after the Response. Other readers are not affected. Use it after a sequence gap |
| `FeedConfigQuery` | `FeedConfig` | Changes this reader's feed configuration; the result is the effective configuration (a query with nothing set only reads it) |

**Minimap.** `Minimap.rgba` is the radar's terrain image, read back from the game's radar texture: `width * height * 4` bytes, row-major, R G B A, 128 by 128 in this build (64 KiB). Pixel (px, py) covers the world cell with its lower corner at `(origin_x + px * cell_width, origin_y + py * cell_height)`; the cell size and origin come from the radar's own world conversion, so this is exactly the mapping the game's minimap uses. It shows the terrain, water and bridges as the radar draws them, not units or fog. When the image cannot be read the `minimap` field is simply absent and the status is still OK.

**Button icons.** The catalog gives where an icon lives, not its pixels: `image_name` (the game's mapped image), `texture_file` (the texture page, a `.tga` in the game's `Art/Textures` archives), the UV rectangle on that page (0..1, origin top left) and the sizes in pixels. A tool that has the game files can crop the icon from the page. Pixel data is not sent because the page is only held in video memory by the game; reading it back would need a texture read on the render thread or decoding the `.tga` from the archives, which can be added behind a capability if tools need it.

## Feed configuration

Every reader starts with the defaults below and changes its own feed with a `FeedConfigQuery`. Fields that are not set keep their value; `reset` returns to the defaults first.

| Setting | Default | Meaning |
|---|---|---|
| `light_interval_ms` | 100 | light `StatsSnapshot` cadence in game time; 0 = none, otherwise 50 to 60000 |
| `full_interval_ms` | 250 | full `StatsSnapshot` cadence; 0 = none (the heavy fields then never arrive), otherwise 100 to 60000 |
| `events` | true | `Events` frames |
| `camera` | true | `Camera` frames |
| `chat` | true | `ChatMessage` frames |
| `grids` | true | `PlayerGrid`s in full snapshots |
| `supply` | true | the supply sources in full snapshots |
| `tech` | true | the map-level tech structures in full snapshots |
| `production` | true | the production queues in full snapshots |
| `grid_resolution` | 32 | cells per side of a `PlayerGrid`: 8, 16, 32 or 64 |

Intervals are rounded to whole logic frames (at least 1). A setting out of range answers `BAD_REQUEST` and changes nothing. `UiState`, `LobbyState`, `MatchInfo`, `MatchSummary` and the Hello are not configurable. Turning `camera` or `events` off and on again resumes with the current state (the camera is sent at once).

## Ordering and lifecycle

On connect the game sends, in order: `Hello`; `UiState`; then `MatchInfo`, which is LOADING only while the match is still loading (a reader that connects during the load gets LOADING and then STARTED), otherwise just STARTED for the running match; then `LobbyState` if the user sits in a lobby. A new reader that is not in a lobby gets no `LobbyState` at all (no `in_lobby = false` marker). After that frames flow as events happen. The cached LOADING data is dropped once STARTED has been handled, so a reader that connects mid-match never sees a stale LOADING.

Which frames wait for STARTED:

| Waits until STARTED has been queued on that connection | Not gated |
|---|---|
| `StatsSnapshot` (including the stall markers), `Events`, `Camera` | `Hello`, `UiState`, `LobbyState`, `ChatMessage`, `MatchInfo`, `MatchSummary`, `Response` |

`Camera` additionally stops after ENDED: it is sent only while a match is active, so the shell map camera never leaks before LOADING or after ENDED. `UiState` and `Camera` keep flowing while the logic is paused; `StatsSnapshot` and `Events` do not (the logic frame does not advance), nor while the logic is stalled except for the stall marker.

- **Cadence.** Snapshot cadence is defined in game time (the defaults below; each reader can change them with a `FeedConfigQuery`), so it is the same on the 30 and 60 logic-frames-per-second builds (`MatchInfo.logic_frames_per_second` says which; game seconds are frame / that value). A **light** snapshot (`full = false`) every 100 ms of game time (6 frames at 60 fps, 3 at 30) carries the cheap per-player numbers: money, income, power, score, APM, rank, skill points, counters, spend, value destroyed/lost, radar, battle plan, sabotage, network, the selection and the speed fields. A **full** snapshot (`full = true`) every 250 ms (15 frames at 60 fps, 8 at 30) also carries everything that needs a walk over the live objects: army value, template counts, production, superweapons, readiness, heroes, worker counts, class composition, grids, supply, tech and the online connections. The two clocks run independently; a sample that is due on both is one full snapshot, so a reader sees a snapshot every 100 ms and an extra full one in between at times. The first snapshot of every connection is a full one. Readers keep the last full values of the heavy fields and overwrite the light ones (the list of heavy fields is the comment on `StatsSnapshot` in the proto).
- **Who is in `players`.** One entry per non-observer player of the match, for the whole match: a defeated or departed player stays, with `state` DEFEATED or LEFT, so a reader can show the final numbers; observers are never listed. Campaign and solo games have no lobby slots to map players to and get no per-player snapshots at all (no `StatsSnapshot` is sent; `Events` still flow).
- **Frame numbers.** `StatsSnapshot.frame` is the logic frame about to be simulated: the sample is taken at the start of that frame, before its commands and object updates run, so it shows the state at the end of frame - 1. `Events` is sent at the end of each logic frame, after the object updates and the victory check, and `Events.frame` is the frame the events happened in: it equals every `StatEvent.frame` in it. So when a snapshot with frame N arrives, every event up to frame N - 1 has already been sent. (If events were left over from a frame that never completed, they are sent in their own `Events` frame, one per original frame.)
- **Events** carries the discrete events (see the event kinds below) of one logic frame, only to a connection that already has STARTED. Nothing is exported for logic frame 0: the map load places the starting buildings and units and grants radar and upgrades before the first frame, so those never appear as BUILD events or as spend. Events are dropped only when 512 are already pending or when the outbound queue is full (see below).
- **Stalls.** While the game is stalled (waiting for network players) a *stall marker* is sent about every 250 ms of wall-clock time so the reader can see the stall: a snapshot with `full = false`, no players, `network_stalled`, `stalled_frames`, the game speed fields and `local_render_fps` set to their current values, and `local_logic_fps` 0 (the logic is not advancing). Nothing else is set on it. No `StatsSnapshot` samples and no `Events` are produced while the logic is stalled or paused; `Camera` and `UiState` continue.
- `Camera` is sent from the render loop, once per rendered frame at most and 120 per second at most, only when the camera moved by more than a hair, while a match or replay is running (also while paused). `timestamp_us` is the same monotonic microsecond clock as the frame header (it equals the header timestamp up to a few microseconds), for ordering and interpolation. The view box is the four ground points where the screen corners land at the map's average terrain height, in the order the minimap draws them (top left, top right, bottom right, bottom left), computed the way the game's own minimap box is (`W3DRadar::reconstructViewBox`). Units: `x`, `y` and the view box are world coordinates; `zoom` is the view's zoom factor (1.0 is the default); `angle` and `pitch` are radians; `height_above_ground` is in world units.
- Nothing is built or queued while no reader is connected, and a reader's outbound queue is cleared when it connects, so a reader only sees frames produced after it connected (plus the connect-time sequence above). Every frame is also addressed to the connection (reader slot and connection epoch) it was produced for, and a frame for a connection that is gone is dropped, so a frame produced for a previous reader of a slot can never appear in the next reader's stream.
- **MatchInfo / ENDED.** ENDED is sent once per match from whichever of the recorder or the game's clear-game-data handling sees the match end first, so it is also sent for a quit, a disconnect, and a replay that is left before its end of file (for a replay `replay_is_playback` is true and `replay_path` its file). The events of the last frame are flushed before it. `MatchSummary` follows the ENDED `MatchInfo` (it is sent from the score screen, only if a reader is connected). `MatchSummaryPlayer.is_winner` is relative to this client's victory conditions: it is true for the players of the victorious alliance of a decisive result and false for everyone when there is none (a quit or a replay stopped early) and for observers; `MatchInfo.completed` is that same "decisive result" test.
- **Results.** `MatchPlayer.result` is UNKNOWN (OBSERVER for observers) on LOADING and STARTED. On ENDED: with a decisive result (`completed`) the victorious alliance is WON (a defeated ally of the winners counts as WON, the way the game's own score screen does) and everyone else LOST; without one a player who left (quit, disconnected or surrendered) is LEFT, a defeated player LOST and any other UNKNOWN.
- `MatchInfo.online.match_id` (the Generals Online match id) can still be 0 when STARTED is built, because the lobby assigns it a little later. The lobby also keeps the previous match's id until the next is assigned, so the id that was last exported counts as unknown (0) until it changes. `match_id` itself never changes; once the online id is known, STARTED is sent once more with `online.match_id` set, and every later resend and the ENDED frame carry it.
- `MatchInfo.start_unix_time` is in seconds since the Unix epoch and is taken when the match enters LOADING (STARTED and ENDED repeat it). `duration_frames` is in logic frames and meaningful on ENDED only.
- `map_name` (MatchInfo, MatchSummary, LobbyState) is relative to the game or user data folder, for example `maps\alpine assault\alpine assault.map`, and is UTF-8. For online lobbies `LobbyState.map_name` is the same normalised path as in `MatchInfo` (so readers can match them) and `LobbyState.map_display_name` is the lobby's display name. `MatchInfo.map_is_user_map` tells a map from the user data folder from a built-in one.
- **Local paths.** Strings are UTF-8 throughout (player names, chat, map and replay paths are converted from the game's UTF-16 or code-page text). Only `MatchInfo.replay_path` is an absolute local path and so contains the Windows user name (the replay folder lives under the user's Documents folder); local tools need it to open the file. No other field carries an absolute path: map names have the user data folder stripped, and `layout` in `UiState` is a bare file name. Treat `replay_path` as private if you forward frames to other machines.
- **Resync.** One-shot frames (`Hello`, `MatchInfo`, `MatchSummary`, `UiState`, `LobbyState`) and responses may use the whole outbound queue of a reader, periodic frames (snapshots, events, camera) may not use its last 64 KiB. If a one-shot frame is still dropped because a slow reader filled its queue, the game remembers it for that reader and, once the queue has drained to half empty, sends that reader `UiState` again, resends STARTED (a repeated STARTED for the running match; treat it as idempotent), sends `LobbyState` again if the user is in a lobby and a full snapshot. A reader can ask for the same with a `ResyncQuery`. A dropped `MatchSummary` or ENDED cannot be repeated.
- A superweapon or special power is reported when the player owns the required science, or when the power requires none; a structure still under construction is not reported until it is complete.
- **Live objects.** Everything that is computed from the object list (army value, class value and counts, live template counts, worker, harvester and idle worker counts, heroes, readiness, superweapons, production, the position grids) excludes objects that are effectively dead (destroyed and waiting for removal). A structure under construction counts as live for army value, class counts, template counts, the superweapon structure count and the grids, but not for readiness or superweapon timers.
- **Omniscient.** The stats are not filtered by what the local player can see: they come from the simulation, which every client runs identically, so a snapshot shows every player's economy, army, production and positions regardless of fog of war. Only the local client's own selection, camera and speed numbers are client-side.
- Build and spend counts (`units_built`, `buildings_built`, `spent_*`, and the BUILD events) are the engine's ScoreKeeper counts and therefore also include objects the engine creates for a player other than through production (replacements, spawned units, objects created by object creation lists), in addition to what a player pays for, but not the starting buildings and units placed at map load.
- Chat is independent of the match phase and flows in menus, lobbies and rooms.

## Backpressure and limits

- Latency: enqueueing signals the pipe worker, which writes immediately and keeps writing until the queue is empty (a short write loops instead of waiting for the next wake), so the transport adds well under a millisecond; the delay a reader sees is the sampling cadence above.
- Enqueueing never blocks the game. Every reader has its own 1 MiB ring of whole frames; a frame that does not fit is dropped for that reader (the new one, never a partial) and shows up as a sequence gap. The last 64 KiB are reserved for one-shot frames (see Resync above). A slow reader therefore misses frames but never corrupts its stream, and never delays another reader. Drain the pipe promptly.
- A single frame is capped at 256 KiB (the pipe cap is 262144 bytes).
- Events: at most 512 per frame (events pending between sends are capped at 512; once full, new events are dropped; an `Events` frame is also dropped, like any frame, if the queue is full). Per snapshot: production queues cover at most 16 structures with 6 entries each; at most 64 sciences, 64 upgrades, 32 tech structures per player, 64 map tech structures, 64 supply sources, 64 selected object ids; grids are `grid_resolution` by `grid_resolution` cells (32 by default).
- Inbound `ChatSend` text is capped at 1024 bytes; the game keeps a mailbox of 8 pending sends per reader and drops more. Requests: 16 waiting per reader, 20 per second with bursts of 40, at most 4 answered per render frame in total.

## Chat

### Inbound (ChatSend)

`ChatSend { channel, text, audience }` is run on the main thread through the same function the matching chat box uses for a typed line, so slash commands and the game's own chat limits and rate limits apply exactly as if the player had typed it. A line that the game would not accept is dropped silently; there is no acknowledgement (an accepted line shows up as a `ChatMessage` when the game displays it).

| Channel | Goes to | Dropped when |
|---|---|---|
| `CHAT_CHANNEL_MATCH` | in-match chat | not in a Generals Online multiplayer match (single player, skirmish, LAN and replays have no in-match chat path); no local player; `audience` is ALLIES and the local player is an observer; `audience` is OBSERVERS and the local player is not an observer; `audience` is OTHER |
| `CHAT_CHANNEL_GAME_LOBBY` | online game setup chat | the online game setup screen is not open |
| `CHAT_CHANNEL_ROOM` | online chat room | the online lobby (room) screen is not open, or the room chat rate limit says no |
| `CHAT_CHANNEL_LAN_LOBBY` | LAN lobby chat | the LAN lobby screen is not open |
| `CHAT_CHANNEL_LAN_GAME` | LAN game setup chat | the LAN game setup screen is not open |

`audience` only matters for the match channel: EVERYONE sends to everyone (for an observer that means the observers), ALLIES to the allies, OBSERVERS is accepted from observers only and goes to the observers. The text must be non-empty, valid UTF-8, trimmed text of at most 1024 bytes.

### Outbound (ChatMessage)

One `ChatMessage` per chat line the game shows to the local player, on every channel and in every state (it is not tied to the match phase). Fields by channel:

| Field | MATCH | GAME_LOBBY | ROOM | LAN_LOBBY / LAN_GAME |
|---|---|---|---|---|
| `logic_frame` | the logic frame the line was shown on | 0 | 0 | 0 |
| `player_index` | the sender's player index, -1 if unknown | -1 | -1 | -1 |
| `user_id` | the sender's Generals Online user id, 0 if unknown | the sender's user id | 0 | 0 |
| `lobby_id` | 0 | the current game lobby id | 0 | 0 |
| `audience` | see below | EVERYONE (not meaningful) | EVERYONE (not meaningful) | EVERYONE (not meaningful) |
| `is_local` | the sender is this client's slot | sent by the local player | always false (the room feed does not tell) | the line came from this PC's IP and the sender name equals the local LAN name (two instances on one PC share an IP) |

Room chat is only reported while the online lobby (room) menu is open, game lobby chat while the game setup menu is open, and LAN chat while the matching LAN screen is open: the game only shows those lines then, and only shown lines are reported. `is_system` marks announcements (for room chat: name change notices) and `is_action` a `/me` emote. In the room channel `sender_name` is empty and `text` is the line as the server formatted it (sender included); in the game lobby channel `sender_name` is the sender's display name.

`audience` of an in-match line is a heuristic. The game does not send an audience; the exporter compares the line's recipient mask with the masks of everyone, of the sender's allies and of the sender alone, so a line is EVERYONE, ALLIES (including a subset of the allies), OBSERVERS (the sender is an observer) or OTHER (a private line, or an unknown sender). The "everyone" mask omits the slots this client has muted, so with muted players a genuine "everyone" line can look like a smaller group and be reported as ALLIES or, for a lone remaining recipient, OTHER.

## UiState

`UiState { screen, layout, game_mode, paused }` reports which screen the game shows. `screen` is derived from the top shell layout: main menu, single player, skirmish setup, LAN lobby, LAN game setup, online welcome, online lobby, online game setup, quickmatch, replay menu, options, save/load, then the in-game screens. `layout` is the raw layout file name (empty when none). `game_mode` and `paused` are meaningful for in-game screens only. It is sent to every new reader first, and again on every change.

## Lobby state and prediction

`LobbyState` mirrors the game setup lobby (online, LAN or skirmish): kind, map, slots, online lobby data and the local client's peer connections. It is sent on every change and with `in_lobby = false` when the user leaves (only to a reader that has seen the lobby). `LobbyState.map_name` is the normalised map path (the same form as `MatchInfo.map_name`); for online lobbies `map_display_name` carries the lobby's display name. `LobbySlot.player.color` is the packed `0xAARRGGBB` colour, and 0 (with `is_color_random` set) while the slot is still on Random.

Random choices (random side, random color, random start position) are not decided until the match loads. While in the lobby the exporter predicts them on a private mirror of the game's random generator; `LobbyState.prediction_valid` and `prediction_start_positions_valid` say which predicted fields (`predicted_side`, `predicted_color`, `predicted_start_position`, `predicted_observer_camera`) can be trusted. On entering LOADING the exporter compares the last prediction with the values the engine actually resolved; the LOADING `MatchInfo` carries the resolved values plus `has_prediction_check` and `prediction_matched`. `has_prediction_check` is only true if a reader saw the lobby: the prediction is built while `LobbyState` is built (at the lobby's own polling), so a match that starts without any reader connected to the lobby has no check. Treat lobby predictions as a preview, and the LOADING `MatchInfo` as the truth.

## Events

`StatEvent { frame, player_index, kind, category, primary_name, secondary_name, has_location, x, y, variant, int_value1, int_value2, cost, object_id, killer_object_id, killer_template, weapon }` is one discrete outcome. `frame` is the logic frame it happened in. The meaning of `player_index` and the other fields depends on `kind`; fields not listed for a kind are unset (empty, 0 or `has_location = false`).

| Kind | `player_index` | `category` | Fields |
|---|---|---|---|
| `ABILITY_USE` | the player using the power | `SUPERWEAPON` for powers with a public timer, else `ABILITY` | `primary_name` = the special power; `secondary_name` = the source unit/structure template (target template for building captures, with `variant` = `source=<unit template>`); `location` if the power has one |
| `SCIENCE_PURCHASE` | the buyer | `SCIENCE` | `primary_name` = the science; `int_value1` = the player's rank level, `int_value2` = the science points left after the purchase. Only purchases are events, not rank grants or setup grants |
| `KILL` | the killer's player | the victim template's bucket | `primary_name` = the victim template; `cost` = its build cost; `object_id` = the victim; `killer_object_id`, `killer_template`, `weapon` describe the killer |
| `LOSS` | the owner of the lost object | the lost template's bucket | `primary_name` = the lost template; `cost` = its build cost; `object_id` = the lost object; `killer_object_id`, `killer_template`, `weapon` describe whoever scored the kill (empty when the engine had none) |
| `BUILD` | the owner | the new object's bucket | `primary_name` = the template; `object_id` = the new object; `int_value1` = the veterancy level it starts with (0 regular, 1 veteran, 2 elite, 3 heroic), so a unit that leaves the factory veteran or elite is a BUILD with `int_value1` > 0 and not a promotion |
| `EVACUATE` | the owner | the container's bucket | `primary_name` = the transport or garrison template; `variant` = `count=<n>` passengers |
| `VETERANCY_CHANGE` | the owner | the unit's bucket | `primary_name` = the template; `object_id` = the promoted object; `int_value1` = old level, `int_value2` = new level. Only promotions earned from experience in play are events; levels assigned at creation (production veterancy from a science or upgrade, spawned units, objects created at a level) are never events, and are visible in BUILD's `int_value1` |
| `SALVAGE_UPGRADE` | the owner | `WEAPON` or `ARMOR` | `primary_name` = the unit template; `int_value1` = the tier reached (1 or 2) |
| `BEACON` | the placer | `BEACON` | `location` |
| `PLAYER_LEFT` | the player who left, -1 if unknown | `PLAYER` | `variant` = `left` (quit or disconnect) or `self_destruct` (surrender); once per player and match, the first cause wins |
| `PLAYER_DEFEATED` | the defeated player | `PLAYER` | |
| `SELL` | the seller | the template's bucket | `primary_name` = the structure template; `cost` = the refund |
| `RADAR_CHANGED` | the player | `PLAYER` | `int_value1` = old `RadarState`, `int_value2` = new |
| `BATTLE_PLAN_CHANGED` | the player | `PLAYER` | `int_value1` = old `BattlePlan`, `int_value2` = new |
| `UPGRADE_COMPLETE` | the player | `UPGRADE` | `primary_name` = the upgrade; `cost` = its build cost; `variant` = `object` for a per-object upgrade |
| `CAPTURE` | the new owner | the structure's bucket | `primary_name` = the structure template; `object_id` = the captured structure; `location`; `int_value1` = the old owner's player index; `variant` = `tech` for a tech building; `cost` = the one-time cash bonus the capture paid (0 if none) |
| `POWER_SABOTAGED` | the victim | `PLAYER` | `int_value1` = how many logic frames the sabotage lasts |

**Object ids.** `object_id` is the engine's object id of the subject, 0 when an event has no single object. It is the key for an `ObjectQuery` and matches the ids in snapshots (`selected_object_ids`, `HeroStatus`, `SuperweaponTimer`, `ProductionQueue`) and in `ObjectsResult` rows. For a kill or loss the object is already dead when the event is read, so a query for it can answer `NOT_FOUND`; the event's own fields are the record. **Killer.** `killer_object_id` and `killer_template` are the object that scored the kill (a projectile's kill is credited to the unit that fired it). `weapon` is the weapon template name of the killer's most recently fired weapon, which can differ from the one that dealt the last blow when the killer carries several, and is empty when the killer never fired a weapon (a crush, a special power, or a script). It is read from the killer object, not from the damage record, which does not name the weapon.

The template buckets (`category` for kill, loss, build, sell, capture, evacuate, veterancy) are `SUPERWEAPON` (a superweapon structure), `STRUCTURE`, `INFANTRY`, `VEHICLE` and `OTHER`. A `CAPTURE` is only emitted when the old and new owner are not allies of each other: a surrender hands the player's structures to a living ally, and script team changes between allies are not captures, so neither produces CAPTURE events.

## Per-player state added for overlays

- **Radar** (`PlayerStats.radar`, event `STATEVENT_RADAR_CHANGED`, `int_value1` old and `int_value2` new `RadarState`): NONE means the player owns no radar structure or ability (the engine's `m_radarCount` is 0). UNPOWERED means radar producers exist but the radar is switched off: the player's power is in brown-out (`Player::onPowerBrownOutChange`, which also a power plant sabotage causes) so `Player::hasRadar()` is false. ACTIVE is `hasRadar()`. Disable-proof radars stay active in a brown-out. An EMP or disabled radar building does not change the player's radar counters in this engine, so it is not shown as UNPOWERED.
- **Battle plan** (`battle_plan`, `battle_plan_transition`, event `STATEVENT_BATTLE_PLAN_CHANGED`): `battle_plan` is the plan affecting the player's troops, from the player's battle plan counters (first of bombardment, hold the line, search and destroy with a non-zero count). `battle_plan_transition` comes from the Strategy Centers' `BattlePlanUpdate`: DEPLOYING while one is unpacking, PACKING while one is packing, ACTIVE when a plan is deployed, else IDLE. It is a full-snapshot field.
- **Power sabotage** (`power_sabotaged`, `power_sabotage_frames_left`, event `STATEVENT_POWER_SABOTAGED` with `int_value1` the duration in logic frames): from the player's Energy sabotaged-until frame, set by the saboteur's power plant attack. Both are logic frames (absolute counts, not seconds).
- **Network** (`PlayerStats.network`, human players of a network match only; every number is -1 when this client does not know it): `is_local` marks this PC's own player. `reported_fps` is that player's average display (render) frames per second as measured by that player's own client, `latency_ms` is that client's average frame round-trip latency in milliseconds (the engine's run-ahead metric, converted from seconds). The engine sends these metrics only to the packet router, so a client that is not the packet router knows them only for itself (the router knows them for everyone); for another player they are -1 until that player's first run-ahead metrics have actually arrived (not the engine's seed values). The router limitation also holds on LAN, where the same metrics are used. `ping_ms` is the Generals Online mesh round trip between this PC and that player in milliseconds, -1 for the local player and for any connection the mesh does not hold (so always -1 on LAN and in skirmish). `relayed` is true when that mesh connection goes through a relay, and is meaningful only when `ping_ms` is known; it is reported with the ping even when the address is not (a relayed peer's address is never exported). `StatsSnapshot.local_render_fps` is this PC's average display FPS and `local_logic_fps` the logic frames this PC actually advanced per wall-clock second since the previous snapshot; both describe the local client only.
- **OnlineConnection** (`StatsSnapshot.connections`, `LobbyState.connections`): the local client's own mesh peers. `latency_ms` is the mesh round trip; `relayed` says the route goes through a relay, and then `endpoint` is set with `relayed = true` and no `ip`. A direct connection carries `endpoint.ip` and `port` of the peer as the networking library reports them; `endpoint` is unset when a direct peer's address is not known. An unset `endpoint` therefore means "unknown", `endpoint.relayed` means "relayed".
- **Upgrades** (`STATEVENT_UPGRADE_COMPLETE`, `primary_name` = upgrade internal name, `cost` its build cost): emitted in `Player::addUpgrade` when a player-level upgrade completes, which covers researching it in a production queue and upgrades granted on object creation (`GrantUpgradeCreate`); an upgrade that was already complete is not repeated. Per-object upgrades from a production queue are emitted with variant `object`.
- **Capture** (`STATEVENT_CAPTURE`): a structure changed owner by capture (`Object::onCapture`, next to the JSON exporter's capture record). The event player is the new owner, `int_value1` the old owner's player index, `variant` is `tech` for a tech building. `cost` is the one-time cash bonus the capture paid (the oil derrick's `InitialCaptureBonus`, paid by `AutoDepositUpdate::awardInitialCaptureBonus`); it is 0 when no bonus was paid. Ownership changes between allies are not reported.

## Units and conventions

- **Frames** are logic frames (`MatchInfo.logic_frames_per_second` per second); game seconds are frame / that value. `ready_frame` (readiness samples and superweapon timers) is an absolute logic frame; `duration_frames` is a count of frames; `power_sabotage_frames_left` and `stalled_frames` are counts of logic frames (`stalled_frames` is derived from wall-clock time at the nominal rate).
- **Times**: `MatchInfo.start_unix_time` is whole seconds since the Unix epoch, set when the match enters LOADING; frame header timestamps and `Camera.timestamp_us` are on one monotonic microsecond clock (see Sequence numbers and timestamps).
- **Percentages**: `percent_ready` (readiness, superweapons) is 0-1, 1.0 = ready; `ProductionItem.percent_complete` is 0-100 and only the front entry of a queue has one.
- **Camera**: `zoom` is the zoom factor of the view, `angle` and `pitch` are in radians, `height_above_ground` is in world units, all like the other world coordinates.

## Scope and known limitations

- Stats are the simulation's: they are not filtered by fog of war, so they show every player's economy, army, production and positions, not just what the local client can see. They are an exporter for casting, not an anti-cheat surface.
- Campaign and solo games have no lobby slots to map players to and get no per-player snapshots (events, camera, UiState and the MatchInfo frames still flow).
- Online identity data (match id, user ids, IPs) is exported exactly as the client knows it; fields are 0 or empty when unknown.
- A frame dropped to backpressure is not retransmitted, except the one-shot resync described above; counters in snapshots are cumulative where noted, so the next snapshot recovers them, but discrete events in a dropped frame are lost. The sequence numbers tell a reader exactly how many frames it lost.
- At most four readers at a time. Inbound frames other than `ChatSend` and `Request` are ignored.
- Queries are answered on the main thread between frames; a very large page costs main-thread time, which is why pages are capped and the number of requests per frame is limited. Query results are not synchronized across clients: they describe this client's view of the simulation at the moment of the query.
- Known engine constraint: the engine's `operator new` is only 4-byte aligned, but protobuf `RepeatedField` stores tag bits in the low 3 bits of its heap pointer. Protobuf messages with repeated scalar fields (`cells`, `selected_object_ids`, `contained_ids`) must therefore be arena-allocated in the exporter, on an arena whose blocks come from `_aligned_malloc`, never from the engine's `new`. Query results are built in reused arena messages for the same reason; the inbound `Request` and `ChatSend` messages contain no repeated fields and are decoded on the stack.
- The schema may grow; add-only changes keep older readers working. Regenerate your bindings from the proto.

## Message reference

Generated from the comments in the proto file. Field meanings that are blank are self-explanatory from the name.

#### `TemplateCount`

One live template's current (not cumulative) count.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `template_name` | string |  |
| 2 | `count` | uint32 |  |

#### `ReadinessSample`

One special power's cooldown state; not deduped by power name.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `power_name` | string |  |
| 2 | `ready_frame` | uint32 | absolute logic frame at which the power is ready (not a duration) |
| 3 | `percent_ready` | float | 1.0 = ready now |

#### enum `StatEventKind`

| Value | Name | Meaning |
|---|---|---|
| 0 | `STATEVENT_ABILITY_USE` | player = the user; primary_name = the special power; secondary_name = the source unit template, or the target template for building captures (variant then "source=<unit template>") |
| 1 | `STATEVENT_SCIENCE_PURCHASE` | primary_name = the science; int_value1 = the player's rank level, int_value2 = the science purchase points left |
| 2 | `STATEVENT_KILL` | player = the killer; the victim's template and category; cost = its build cost |
| 3 | `STATEVENT_LOSS` | player = the owner of the lost object; template, category and cost of what was lost |
| 4 | `STATEVENT_BUILD` | player = the owner; the new object's template and category; int_value1 = the veterancy level it starts with (0 regular .. 3 heroic) |
| 5 | `STATEVENT_EVACUATE` | player = the owner; primary_name = the transport or garrison template; variant "count=<n>" |
| 6 | `STATEVENT_VETERANCY_CHANGE` | a promotion earned from experience (never a level given at creation); int_value1 = old level, int_value2 = new level |
| 7 | `STATEVENT_SALVAGE_UPGRADE` | category WEAPON or ARMOR; primary_name = the unit template; int_value1 = the new tier |
| 8 | `STATEVENT_BEACON` | location set; category is the player's beacon |
| 9 | `STATEVENT_PLAYER_LEFT` | once per player and match, the first cause wins; variant: "left" (quit or disconnect, from the network leave command) or "self_destruct" (surrender) |
| 10 | `STATEVENT_PLAYER_DEFEATED` | the engine marked the player defeated |
| 11 | `STATEVENT_SELL` | a structure finished selling; primary_name is its template, cost the refund |
| 12 | `STATEVENT_RADAR_CHANGED` | int_value1 = old RadarState, int_value2 = new RadarState |
| 13 | `STATEVENT_BATTLE_PLAN_CHANGED` | int_value1 = old BattlePlan, int_value2 = new BattlePlan (the plan affecting the player's troops) |
| 14 | `STATEVENT_UPGRADE_COMPLETE` | primary_name = upgrade internal name, cost = its build cost; variant "object" for a per-object upgrade |
| 15 | `STATEVENT_CAPTURE` | a structure changed owner by capture: player = new owner, int_value1 = old owner player index, variant "tech" for a tech building, cost = the one-time cash bonus the capture paid (0 if none) |
| 16 | `STATEVENT_POWER_SABOTAGED` | player = the victim, int_value1 = how many logic frames the sabotage lasts |

#### `StatEvent`

A discrete gameplay-outcome event; frame is the logic frame it happened in. player_index is the acting player (the killer, the owner, the new owner for captures; see StatEventKind). category is a coarse bucket: SUPERWEAPON/STRUCTURE/INFANTRY/VEHICLE/OTHER from the template (kill, loss, build, sell, capture, evacuate, veterancy), ABILITY or SUPERWEAPON for abilities, SCIENCE, WEAPON/ARMOR for salvage upgrades, UPGRADE, BEACON, and PLAYER for the player-state kinds (left, defeated, radar, battle plan, power sabotage).

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `frame` | uint32 |  |
| 2 | `player_index` | int32 |  |
| 3 | `kind` | StatEventKind |  |
| 4 | `category` | string |  |
| 5 | `primary_name` | string | template/power/science name |
| 6 | `secondary_name` | string | ability source/target template, when known |
| 7 | `has_location` | bool |  |
| 8 | `x` | float |  |
| 9 | `y` | float |  |
| 10 | `variant` | string | free-form extra context (e.g. evacuate count) |
| 11 | `int_value1` | int32 | e.g. veterancy oldLevel, salvage tier |
| 12 | `int_value2` | int32 | e.g. veterancy newLevel |
| 13 | `cost` | uint32 | kill and loss events: the victim template's build cost |
| 14 | `object_id` | uint32 | the subject object: the victim (kill, loss), the new object (build), the captured structure (capture), the promoted object (veterancy); 0 when not applicable |
| 15 | `killer_object_id` | uint32 |  |
| 16 | `killer_template` | string |  |
| 17 | `weapon` | string | weapon template name |

#### `Events`

The discrete events produced during one logic frame, sent at the end of that frame (never with a snapshot), so frame equals every StatEvent.frame in it. Events are dropped only when 512 are already pending, or the outbound queue is full.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `frame` | uint32 |  |
| 2 | `events` | repeated StatEvent |  |

#### `StatsSnapshot`

One periodic sample, one entry per non-observer player of the match (defeated and departed ones stay, see PlayerStats.state; campaign and solo games have no per-player entries). frame is the logic frame about to be simulated: the sample is taken at the start of that frame, so it shows the state at the end of frame - 1, and the Events up to frame - 1 have already been sent. A light snapshot (full = false) carries only the cheap per-player numbers; a full one also carries everything that needs a walk over the live objects. Readers keep the last full values of the heavy fields and overwrite the light ones: heavy = army_value, live_superweapon_structure_count, live_template_counts, readiness, sciences, upgrades, production, workers, harvesters, idle_workers, heroes, superweapons, army_value_by_class, army_count_by_class, tech_structures, battle_plan_transition (per player) and grids, supply, tech, connections (per snapshot). Stall marker: while the logic waits on a peer a snapshot with no players and full = false is sent about every 250 ms of wall-clock time. It carries frame, network_stalled, stalled_frames, game_speed_fps / game_speed_ratio and local_render_fps (all current values); local_logic_fps is 0 and everything else is absent. No snapshots or Events are produced while the logic is stalled or paused.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `frame` | uint32 |  |
| 2 | `players` | repeated PlayerStats |  |
| 4 | `connections` | repeated OnlineConnection | the local client's own peer connections |
| 5 | `grids` | repeated PlayerGrid | one per exported player |
| 6 | `supply` | repeated SupplySource | supply sources on the map, capped, non-empty first |
| 7 | `replay_total_frames` | uint32 | frames in the replay being played back, 0 otherwise |
| 8 | `game_speed_fps` | int32 | FramePacer's actual logic time scale in frames per second (equals logic_frames_per_second at normal speed); 0 = frozen or halted, -1 = uncapped |
| 9 | `game_speed_ratio` | float | the same as a ratio of normal speed |
| 10 | `tech` | repeated TechStructure | neutral and captured tech structures on the map, capped |
| 12 | `selected_object_ids` | repeated uint32 | this client's current selection, capped; client-side like the camera |
| 13 | `network_stalled` | bool | the logic is waiting on a peer (Network::isStalling) |
| 14 | `stalled_frames` | uint32 | how long, in logic frames at the nominal rate, derived from wall-clock time |
| 15 | `local_render_fps` | float | the display's average rendered frames per second (Display::getAverageFPS), 0 when unknown |
| 16 | `local_logic_fps` | float | logic frames per wall-clock second measured between this and the previous snapshot, 0 on the first |
| 17 | `full` | bool | true: carries the heavy fields too; false: light snapshot, heavy fields absent (keep the last full values) |

#### `TechStructure`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_name` | string |  |
| 3 | `x` | float |  |
| 4 | `y` | float |  |
| 5 | `owner_player_index` | int32 | -1 when neutral |

#### `Camera`

The local tactical view. Per client: not part of the synchronized game state. Sent from the render loop (once per rendered frame at most, 120 per second at most, only when something changed), so it is as current as the picture.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `timestamp_us` | uint64 | the envelope clock (microseconds since the exporter clock started, see the frame header): monotonic |
| 2 | `logic_frame` | uint32 |  |
| 3 | `x` | float | world position the camera looks at |
| 4 | `y` | float |  |
| 5 | `zoom` | float | the view's zoom factor (1.0 = default) |
| 6 | `angle` | float | radians |
| 7 | `pitch` | float | radians |
| 8 | `height_above_ground` | float | world units |
| 9 | `has_view_box` | bool |  |
| 10 | `view_tl_x` | float |  |
| 11 | `view_tl_y` | float |  |
| 12 | `view_tr_x` | float |  |
| 13 | `view_tr_y` | float |  |
| 14 | `view_br_x` | float |  |
| 15 | `view_br_y` | float |  |
| 16 | `view_bl_x` | float |  |
| 17 | `view_bl_y` | float |  |

#### `PlayerGrid`

Unit and structure counts per cell over the map's playable extent (MatchInfo world_*), row-major from the min corner. Counts objects with KINDOF_STRUCTURE, INFANTRY, VEHICLE or AIRCRAFT that are not dead; projectiles, effects and inert objects are not counted.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `player_index` | int32 |  |
| 2 | `width` | uint32 | FeedConfig.grid_resolution (default 32) |
| 3 | `height` | uint32 | the same |
| 4 | `cell_width` | float | world units |
| 5 | `cell_height` | float |  |
| 6 | `cells` | repeated uint32 | width * height counts, zeros included |

#### `SupplySource`

A supply source (warehouse) on the map.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_name` | string |  |
| 3 | `x` | float |  |
| 4 | `y` | float |  |
| 5 | `remaining_value` | uint32 | cash: boxes stored times ValuePerSupplyBox |
| 6 | `remaining_boxes` | uint32 |  |

#### enum `RadarState`

Whether a player's radar works. NONE: no radar structure or ability. UNPOWERED: radar producers exist but the player's radar is switched off (brown-out: Player::onPowerBrownOutChange, which a power plant sabotage also causes). ACTIVE: Player::hasRadar(). Derived from the engine's own radar counters.

| Value | Name | Meaning |
|---|---|---|
| 0 | `RADAR_NONE` |  |
| 1 | `RADAR_UNPOWERED` |  |
| 2 | `RADAR_ACTIVE` |  |

#### enum `BattlePlan`

| Value | Name | Meaning |
|---|---|---|
| 0 | `BATTLE_PLAN_NONE` |  |
| 1 | `BATTLE_PLAN_BOMBARDMENT` |  |
| 2 | `BATTLE_PLAN_HOLD_THE_LINE` |  |
| 3 | `BATTLE_PLAN_SEARCH_AND_DESTROY` |  |

#### enum `BattlePlanTransition`

What the player's Strategy Centers are doing. DEPLOYING: at least one is unpacking; PACKING: at least one is packing (DEPLOYING wins if both); ACTIVE: a plan is deployed; IDLE: none.

| Value | Name | Meaning |
|---|---|---|
| 0 | `BATTLE_PLAN_IDLE` |  |
| 1 | `BATTLE_PLAN_DEPLOYING` |  |
| 2 | `BATTLE_PLAN_ACTIVE` |  |
| 3 | `BATTLE_PLAN_PACKING` |  |

#### `NetworkStats`

Network numbers for one human player. Every field is -1 when this client does not know it.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `is_local` | bool | this player is the local client |
| 2 | `reported_fps` | int32 | the player's own average DISPLAY frames per second (ConnectionManager run-ahead metrics), -1 if unknown |
| 3 | `latency_ms` | int32 | the player's own average frame round-trip latency in milliseconds (FrameMetrics, converted from seconds), -1 if unknown |
| 4 | `ping_ms` | int32 | Generals Online mesh round-trip time between the local client and this player in milliseconds, -1 for the local player or no connection |
| 5 | `relayed` | bool | the mesh connection to this player goes through a relay (only meaningful when ping_ms >= 0) |

#### `PlayerStats`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `player_index` | int32 |  |
| 2 | `money` | uint32 |  |
| 3 | `energy_production` | int32 |  |
| 4 | `energy_consumption` | int32 |  |
| 5 | `total_money_earned` | uint32 |  |
| 6 | `units_built` | uint32 |  |
| 7 | `units_lost` | uint32 |  |
| 8 | `units_destroyed` | uint32 |  |
| 9 | `buildings_built` | uint32 |  |
| 10 | `buildings_lost` | uint32 |  |
| 11 | `buildings_destroyed` | uint32 |  |
| 12 | `score` | int32 |  |
| 13 | `army_value` | int32 | sum of build costs over the live objects: not effectively dead; a structure under construction counts |
| 14 | `live_superweapon_structure_count` | uint32 | live superweapon structures, under construction included |
| 15 | `live_template_counts` | repeated TemplateCount |  |
| 16 | `readiness` | repeated ReadinessSample | powers the player can fire, from live structures that are complete |
| 17 | `rank_level` | int32 |  |
| 18 | `skill_points` | int32 | unspent science purchase points |
| 19 | `sciences` | repeated string | purchased sciences, internal names, sent in full every sample |
| 20 | `money_per_minute` | uint32 | Money::getCashPerMinute(): income deposited over the last 60 logic seconds |
| 21 | `actions_total` | uint32 | commands executed for this player since the match started |
| 22 | `apm_current` | uint32 | actions per minute over the last 60 logic seconds (extrapolated during the first minute) |
| 23 | `value_destroyed` | uint32 | cumulative build cost of enemy objects this player destroyed |
| 24 | `value_lost` | uint32 | cumulative build cost of this player's objects lost |
| 25 | `production` | repeated ProductionQueue | structures with something in production; capped per player |
| 26 | `workers` | uint32 | live objects with KINDOF_DOZER |
| 27 | `harvesters` | uint32 | live objects with KINDOF_HARVESTER (a worker can be both) |
| 28 | `idle_workers` | uint32 | KINDOF_DOZER objects whose AI is idle: the test Player uses for the idle worker button |
| 29 | `kills` | CategoryCounts | by victim: aircraft first, else structure, infantry, vehicle |
| 30 | `losses` | CategoryCounts |  |
| 31 | `heroes` | repeated HeroStatus | KINDOF_HERO templates seen alive this match; dead ones stay listed |
| 32 | `superweapons` | repeated SuperweaponTimer | one per superweapon structure |
| 33 | `spent_units` | uint32 |  |
| 34 | `spent_structures` | uint32 |  |
| 35 | `spent_upgrades` | uint32 |  |
| 36 | `spent_sciences` | uint32 | general points spent on sciences, not cash |
| 37 | `upgrades` | repeated string |  |
| 38 | `army_value_by_class` | CategoryValue | build cost of live objects by the same classification as kills |
| 39 | `army_count_by_class` | CategoryCounts |  |
| 40 | `tech_structures` | repeated string | template names of tech structures this player owns (KINDOF_TECH_BUILDING, KINDOF_TECH_BASE_DEFENSE) |
| 41 | `radar` | RadarState |  |
| 42 | `battle_plan` | BattlePlan | the plan affecting this player's troops (Player battle plan counters); first non-zero of bombardment, hold the line, search and destroy |
| 43 | `battle_plan_transition` | BattlePlanTransition | state of this player's Strategy Centers |
| 44 | `power_sabotaged` | bool | the player's power is currently sabotaged (Energy sabotaged-until frame is in the future) |
| 45 | `power_sabotage_frames_left` | uint32 | logic frames until the sabotage ends |
| 46 | `network` | NetworkStats | set only for human players of a network match |
| 47 | `state` | PlayerState | active, defeated or departed; the numbers of a non-active player stay and stop changing |

#### enum `PlayerState`

Why a player is listed in StatsSnapshot.players. Observers are never listed.

| Value | Name | Meaning |
|---|---|---|
| 0 | `PLAYER_STATE_ACTIVE` |  |
| 1 | `PLAYER_STATE_DEFEATED` | the engine's defeat state: killed, or nothing left that counts for victory |
| 2 | `PLAYER_STATE_LEFT` | quit, disconnected or surrendered (a PLAYER_LEFT event was emitted) |

#### `CategoryValue`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `infantry` | uint32 |  |
| 2 | `vehicle` | uint32 |  |
| 3 | `aircraft` | uint32 |  |
| 4 | `structure` | uint32 |  |

#### `CategoryCounts`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `infantry` | uint32 |  |
| 2 | `vehicle` | uint32 |  |
| 3 | `aircraft` | uint32 |  |
| 4 | `structure` | uint32 |  |

#### `HeroStatus`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `template_name` | string |  |
| 2 | `alive` | bool |  |
| 3 | `object_id` | uint32 | 0 when dead |

#### enum `SuperweaponState`

| Value | Name | Meaning |
|---|---|---|
| 0 | `SUPERWEAPON_CHARGING` |  |
| 1 | `SUPERWEAPON_READY` |  |

#### `SuperweaponTimer`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_name` | string |  |
| 3 | `power_name` | string |  |
| 4 | `ready_frame` | uint32 | absolute logic frame at which it is ready |
| 5 | `percent_ready` | float | 0-1 (not 0-100), 1.0 = ready |
| 6 | `state` | SuperweaponState |  |

#### enum `ProductionKind`

| Value | Name | Meaning |
|---|---|---|
| 0 | `PRODUCTION_KIND_UNIT` |  |
| 1 | `PRODUCTION_KIND_UPGRADE` |  |

#### `ProductionItem`

One entry of a structure's production queue.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `kind` | ProductionKind |  |
| 2 | `name` | string | unit template or upgrade name |
| 3 | `percent_complete` | float | 0-100 (not 0-1), the front entry only; 0 for the others |
| 4 | `quantity` | uint32 | units still to come out of this entry |

#### `ProductionQueue`

What one structure is producing, front of the queue first.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `structure_template` | string |  |
| 3 | `entries` | repeated ProductionItem |  |

#### `MatchSummary`

Sent once from the score screen, after the ENDED MatchInfo and before the game data is cleared, only to a connected reader.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `frame` | uint32 |  |
| 2 | `map_name` | string |  |
| 3 | `players` | repeated MatchSummaryPlayer |  |

#### `MatchSummaryPlayer`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `lobby_slot` | uint32 |  |
| 2 | `player_index` | int32 |  |
| 3 | `player_name` | string |  |
| 4 | `side` | string |  |
| 5 | `score` | int32 |  |
| 6 | `is_winner` | bool | the player is in the victorious alliance of a decisive result (as this client sees it); false for everyone when there is none, and for observers |
| 7 | `is_observer` | bool |  |
| 8 | `units_built` | uint32 |  |
| 9 | `units_lost` | uint32 |  |
| 10 | `units_destroyed` | uint32 |  |
| 11 | `buildings_built` | uint32 |  |
| 12 | `buildings_lost` | uint32 |  |
| 13 | `buildings_destroyed` | uint32 |  |
| 14 | `total_money_earned` | uint32 |  |

#### enum `MatchPhase`

| Value | Name | Meaning |
|---|---|---|
| 0 | `MATCH_PHASE_LOADING` | the loading screen: sides, colors and start positions are resolved, the map is not loaded yet |
| 1 | `MATCH_PHASE_STARTED` |  |
| 2 | `MATCH_PHASE_ENDED` |  |

#### enum `MatchMode`

| Value | Name | Meaning |
|---|---|---|
| 0 | `MATCH_MODE_UNKNOWN` |  |
| 1 | `MATCH_MODE_SINGLE_PLAYER` |  |
| 2 | `MATCH_MODE_SKIRMISH` |  |
| 3 | `MATCH_MODE_LAN` |  |
| 4 | `MATCH_MODE_ONLINE` |  |
| 5 | `MATCH_MODE_REPLAY` | offline .rep file played back locally |

#### enum `PlayerResult`

A slot's outcome as of the ENDED frame; UNKNOWN (OBSERVER for observers) on LOADING and STARTED. On ENDED a decisive result (MatchInfo.completed) gives the victorious alliance WON (defeated allies included, as the game counts them) and everyone else LOST; without one a player who left is LEFT, a defeated player LOST and the rest UNKNOWN.

| Value | Name | Meaning |
|---|---|---|
| 0 | `PLAYER_RESULT_UNKNOWN` |  |
| 1 | `PLAYER_RESULT_WON` |  |
| 2 | `PLAYER_RESULT_LOST` |  |
| 3 | `PLAYER_RESULT_LEFT` |  |
| 4 | `PLAYER_RESULT_OBSERVER` |  |

#### `MatchPlayer`

One lobby slot's static data: everything that does not change during the match.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `lobby_slot` | uint32 |  |
| 2 | `player_index` | int32 |  |
| 3 | `name` | string |  |
| 4 | `side` | string | raw faction name |
| 5 | `color` | uint32 | packed 0xAARRGGBB; 0 when the colour is still Random (in a lobby) |
| 6 | `team` | int32 | lobby alliance team; -1 when unknown |
| 7 | `is_human` | bool |  |
| 8 | `is_observer` | bool |  |
| 9 | `is_color_random` | bool | left on Random in the lobby |
| 10 | `is_side_random` | bool |  |
| 11 | `result` | PlayerResult |  |
| 12 | `online` | OnlinePlayer | online matches only |
| 13 | `lan_address` | Endpoint | LAN matches only: the lobby slot address |
| 14 | `start_position` | int32 | start position index as the map defines it, -1 when none; resolved from LOADING on |
| 15 | `has_start_location` | bool | not set at LOADING, the map is not loaded yet |
| 16 | `start_x` | float | world position of the start waypoint |
| 17 | `start_y` | float |  |

#### `ObserverCamera`

Where the observer camera starts: the start position of the local observer slot, which is a spot a player holds.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `start_position` | int32 |  |
| 2 | `lobby_slot` | int32 | the player holding that start position, -1 when none |
| 3 | `player_index` | int32 | the same player's Player::getPlayerIndex(), -1 until the match has started |

#### `MatchInfo`

Announces that a real (non-shell) match started or ended. Sent per phase while a reader is connected; a reader connecting mid-match gets a STARTED for the running match before its first StatsSnapshot.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `phase` | MatchPhase |  |
| 2 | `match_id` | string | opaque; stable across STARTED and ENDED |
| 3 | `mode` | MatchMode |  |
| 4 | `map_name` | string | relative to the game or user data folder, e.g. "maps\alpine assault\alpine assault.map"; UTF-8, never an absolute path |
| 5 | `start_unix_time` | uint64 | seconds since the Unix epoch, taken when the match entered LOADING (kept for STARTED and ENDED) |
| 6 | `duration_frames` | uint32 | logic frames from the start to ENDED; 0 on LOADING and STARTED |
| 7 | `players` | repeated MatchPlayer |  |
| 8 | `replay_path` | string | absolute (it contains the Windows user name), UTF-8, closed and flushed on ENDED; empty when no replay exists |
| 9 | `replay_is_playback` | bool | true when replay_path is an existing file being watched |
| 10 | `completed` | bool | a decisive result was reached (this client's victory conditions); always false on STARTED |
| 11 | `local_player_index` | int32 | the player at this PC, -1 when none |
| 12 | `online` | OnlineMatch | online matches only |
| 13 | `observer_camera` | ObserverCamera | set only when the local player is an observer or this is a replay |
| 14 | `has_prediction_check` | bool | only true if this reader saw the lobby (the prediction is made from LobbyState) |
| 15 | `prediction_matched` | bool | every predicted side, color, start position and the observer camera were right |
| 16 | `random_state_clean` | bool | the logic generator was untouched between the lobby seed and the resolution |
| 17 | `has_world_extent` | bool |  |
| 18 | `world_min_x` | float |  |
| 19 | `world_min_y` | float |  |
| 20 | `world_max_x` | float |  |
| 21 | `world_max_y` | float |  |
| 22 | `map_is_user_map` | bool | the map comes from the user data folder, not the game install |
| 23 | `logic_frames_per_second` | uint32 | the build's logic rate (30, or 60 for the high-fps server build); frame / this = game seconds |

#### `Endpoint`

A network endpoint the client already holds. Never a relay address presented as the peer's.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `ip` | string | textual IPv4/IPv6 |
| 2 | `port` | uint32 | 0 when unknown |
| 3 | `relayed` | bool | the connection goes through a relay, ip is unset |

#### enum `OnlineLobbyType`

| Value | Name | Meaning |
|---|---|---|
| 0 | `ONLINE_LOBBY_UNKNOWN` |  |
| 1 | `ONLINE_LOBBY_CUSTOM` |  |
| 2 | `ONLINE_LOBBY_QUICKMATCH` |  |

#### `OnlineMatch`

Generals Online lobby data the client holds for the current match.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `match_id` | uint64 | the Generals Online match id; 0 until the lobby holds it, then sent in a later STARTED resend |
| 2 | `lobby_id` | int64 |  |
| 3 | `lobby_name` | string |  |
| 4 | `lobby_type` | OnlineLobbyType |  |
| 5 | `map_official` | bool |  |
| 6 | `starting_cash` | uint32 |  |
| 7 | `limit_superweapons` | bool |  |
| 8 | `track_stats` | bool |  |
| 9 | `allow_observers` | bool |  |
| 10 | `vanilla_teams` | bool |  |
| 11 | `max_players` | uint32 |  |
| 12 | `max_camera_height` | uint32 |  |
| 13 | `region` | string |  |

#### `OnlinePlayer`

A lobby member, joined to MatchPlayer. The ratings are the cached values the lobby tooltip shows; unset if not cached.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `user_id` | int64 |  |
| 2 | `display_name` | string |  |
| 3 | `has_stats` | bool |  |
| 4 | `elo_rating` | int32 |  |
| 5 | `monthly_elo_rating` | int32 |  |
| 6 | `elo_num_matches` | int32 |  |
| 7 | `total_wins` | uint32 |  |
| 8 | `total_losses` | uint32 |  |
| 9 | `total_disconnects` | uint32 |  |

#### `OnlineConnection`

One of the local client's peer connections.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `user_id` | int64 |  |
| 2 | `latency_ms` | int32 |  |
| 3 | `relayed` | bool |  |
| 4 | `endpoint` | Endpoint | relayed: set with relayed = true and no ip; direct: the peer address; unset when the address is not known |

#### enum `ChatChannel`

| Value | Name | Meaning |
|---|---|---|
| 0 | `CHAT_CHANNEL_MATCH` | in-match chat |
| 1 | `CHAT_CHANNEL_GAME_LOBBY` | the game setup lobby before a match |
| 2 | `CHAT_CHANNEL_ROOM` | the chat room / lobby browser |
| 3 | `CHAT_CHANNEL_LAN_LOBBY` | the LAN lobby |
| 4 | `CHAT_CHANNEL_LAN_GAME` | the LAN game setup screen |

#### enum `ChatAudience`

| Value | Name | Meaning |
|---|---|---|
| 0 | `CHAT_AUDIENCE_EVERYONE` |  |
| 1 | `CHAT_AUDIENCE_ALLIES` |  |
| 2 | `CHAT_AUDIENCE_OBSERVERS` |  |
| 3 | `CHAT_AUDIENCE_OTHER` |  |

#### `ChatMessage`

A chat line as shown to the local player. For lobby and room chat, text is the line as the server formatted it.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `channel` | ChatChannel |  |
| 2 | `logic_frame` | uint32 | in-match only, 0 otherwise |
| 3 | `player_index` | int32 | in-match sender, -1 when unknown |
| 4 | `user_id` | int64 | lobby sender, 0 when unknown |
| 5 | `sender_name` | string |  |
| 6 | `text` | string | UTF-8 |
| 7 | `audience` | ChatAudience | in-match only |
| 8 | `is_local` | bool | sent by the local player |
| 9 | `is_system` | bool | announcement |
| 10 | `is_action` | bool | /me emote |
| 11 | `lobby_id` | int64 | game lobby chat only |

#### `ChatSend`

Asks the game to handle a chat line exactly as if it was typed into that channel's chat box, slash commands included.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `channel` | ChatChannel |  |
| 2 | `text` | string | UTF-8 |
| 3 | `audience` | ChatAudience | in-match only |

#### enum `UiScreen`

| Value | Name | Meaning |
|---|---|---|
| 0 | `UI_SCREEN_UNKNOWN` |  |
| 1 | `UI_SCREEN_MAIN_MENU` |  |
| 2 | `UI_SCREEN_SINGLE_PLAYER` | campaign, difficulty and map selection |
| 3 | `UI_SCREEN_SKIRMISH_SETUP` |  |
| 4 | `UI_SCREEN_LAN_LOBBY` |  |
| 5 | `UI_SCREEN_LAN_GAME_SETUP` |  |
| 6 | `UI_SCREEN_ONLINE_WELCOME` |  |
| 7 | `UI_SCREEN_ONLINE_LOBBY` | chat room and game list |
| 8 | `UI_SCREEN_ONLINE_GAME_SETUP` |  |
| 9 | `UI_SCREEN_ONLINE_QUICKMATCH` |  |
| 10 | `UI_SCREEN_REPLAY_MENU` |  |
| 11 | `UI_SCREEN_OPTIONS` |  |
| 12 | `UI_SCREEN_SAVE_LOAD` |  |
| 13 | `UI_SCREEN_CREDITS` |  |
| 14 | `UI_SCREEN_SCORE_SCREEN` |  |
| 15 | `UI_SCREEN_CONNECTING` | establishing connections before a match |
| 16 | `UI_SCREEN_LOADING` | map or save loading |
| 17 | `UI_SCREEN_IN_GAME` |  |
| 18 | `UI_SCREEN_IN_GAME_MENU` | quit menu open |

#### `UiState`

The screen the game is on, sent on every change and to each new reader on connect.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `screen` | UiScreen |  |
| 2 | `layout` | string | raw top shell layout file name, empty when none |
| 3 | `game_mode` | MatchMode | meaningful for the in-game screens |
| 4 | `paused` | bool | in-game screens only |

#### enum `LobbySlotState`

| Value | Name | Meaning |
|---|---|---|
| 0 | `LOBBY_SLOT_OPEN` |  |
| 1 | `LOBBY_SLOT_CLOSED` |  |
| 2 | `LOBBY_SLOT_AI_EASY` |  |
| 3 | `LOBBY_SLOT_AI_MEDIUM` |  |
| 4 | `LOBBY_SLOT_AI_BRUTAL` |  |
| 5 | `LOBBY_SLOT_HUMAN` |  |

#### `LobbySlot`

One slot of the game setup lobby. player reuses MatchPlayer: lobby_slot, name, side, color, team, is_human, is_observer, the random flags and online; player_index is -1 and result unknown before the match.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `state` | LobbySlotState |  |
| 2 | `player` | MatchPlayer | unset for open and closed slots |
| 3 | `is_host` | bool |  |
| 4 | `is_ready` | bool | the slot accepted the current options |
| 5 | `has_map` | bool |  |
| 6 | `start_position` | int32 | -1 when none |
| 7 | `predicted_side` | string |  |
| 8 | `predicted_color` | uint32 | packed 0xAARRGGBB, 0 when none |
| 9 | `predicted_start_position` | int32 |  |

#### enum `LobbyKind`

| Value | Name | Meaning |
|---|---|---|
| 0 | `LOBBY_KIND_UNKNOWN` |  |
| 1 | `LOBBY_KIND_ONLINE` |  |
| 2 | `LOBBY_KIND_LAN` |  |
| 3 | `LOBBY_KIND_SKIRMISH` |  |

#### `LobbyState`

The game setup lobby the user sits in, before the match starts: online, LAN or skirmish setup.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `in_lobby` | bool | false: the user left the lobby, nothing else is set |
| 2 | `kind` | LobbyKind |  |
| 3 | `map_name` | string |  |
| 4 | `online` | OnlineMatch | online lobbies only: lobby id, name and rules; match_id is whatever the lobby holds before the match |
| 5 | `host_user_id` | int64 | online lobbies only |
| 6 | `slots` | repeated LobbySlot |  |
| 7 | `connections` | repeated OnlineConnection | online lobbies only: the local client's own peer connections |
| 8 | `prediction_valid` | bool | sides and colors are predicted |
| 9 | `prediction_start_positions_valid` | bool | start positions (and the observer camera) are predicted; false when the map is not in the map cache |
| 10 | `predicted_observer_camera` | ObserverCamera | set only when the local slot is an observer and the start positions are predicted |
| 11 | `map_display_name` | string | online lobbies only: the lobby's map display name; map_name is the normalised path, as in MatchInfo |

#### `Hello`

The first frame of every connection (sequence 0). Everything a reader needs to know about the game and the pipe before it sends anything.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `protocol_version` | uint32 | 2 for this schema; readers refuse a version they do not know |
| 2 | `game_version` | string | the game's human-readable version, e.g. "1.04" |
| 3 | `game_build` | string | git version and build time, free-form, for logs and bug reports |
| 4 | `process_id` | uint32 | the game process, also the pipe name suffix |
| 5 | `logic_frames_per_second` | uint32 | the build's logic rate (30, or 60 for the high-fps server build); frame / this = game seconds |
| 6 | `capabilities` | repeated string | feature names, see docs/stats-exporter.md; unknown ones can be ignored |
| 7 | `max_requests_per_second` | uint32 | sustained Request rate per reader; faster requests get RATE_LIMITED |
| 8 | `reader_slot` | uint32 | this connection's slot, 0 .. max_readers - 1 |
| 9 | `max_readers` | uint32 | simultaneous readers the pipe serves |
| 10 | `epoch_unix_ms` | uint64 | wall clock (ms since the Unix epoch) at frame timestamp 0 |
| 11 | `max_request_burst` | uint32 | requests that may arrive at once before the rate limit bites |
| 12 | `max_page_size` | uint32 | the largest page_size a paged query honours |
| 13 | `queue_capacity_bytes` | uint32 | outbound queue per reader; a reader that falls this far behind loses frames (visible as sequence gaps) |

#### enum `ResponseStatus`

| Value | Name | Meaning |
|---|---|---|
| 0 | `RESPONSE_STATUS_OK` |  |
| 1 | `RESPONSE_STATUS_BAD_REQUEST` | undecodable, no query set, or an argument out of range |
| 2 | `RESPONSE_STATUS_NOT_FOUND` | the object, player or template asked for does not exist |
| 3 | `RESPONSE_STATUS_RATE_LIMITED` | too many requests; nothing was done, retry later |
| 4 | `RESPONSE_STATUS_UNAVAILABLE` | the data does not exist right now (no match running, no map loaded) |
| 5 | `RESPONSE_STATUS_TOO_LARGE` | the result does not fit a frame; ask for a smaller page |

#### `PageRequest`

Paging for the queries that can return many rows. The cursor is opaque to the reader: pass the previous page's next_cursor back, 0 starts at the beginning.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `cursor` | uint32 |  |
| 2 | `page_size` | uint32 | 0 = the server default; larger than Hello.max_page_size is lowered to it |

#### `PageInfo`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `has_more` | bool |  |
| 2 | `next_cursor` | uint32 | only meaningful when has_more |
| 3 | `total` | uint32 | matching rows at the time of the query, when known (0 = not known) |

#### `Request`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `id` | uint32 | chosen by the reader, echoed in the Response; ids of requests in flight should be distinct |
| 10 | `catalog` | CatalogQuery | one of `query`.  |
| 11 | `object` | ObjectQuery | one of `query`.  |
| 12 | `objects` | ObjectsQuery | one of `query`.  |
| 13 | `player` | PlayerQuery | one of `query`.  |
| 14 | `map` | MapQuery | one of `query`.  |
| 15 | `resync` | ResyncQuery | one of `query`.  |
| 16 | `feed_config` | FeedConfigQuery | one of `query`.  |

#### `Response`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `id` | uint32 |  |
| 2 | `status` | ResponseStatus |  |
| 3 | `message` | string | short human-readable reason when status is not OK |
| 10 | `catalog` | CatalogResult | one of `result`.  |
| 11 | `object` | ObjectDetail | one of `result`.  |
| 12 | `objects` | ObjectsResult | one of `result`.  |
| 13 | `player` | PlayerDetail | one of `result`.  |
| 14 | `map` | MapDetail | one of `result`.  |
| 15 | `resync` | ResyncResult | one of `result`.  |
| 16 | `feed_config` | FeedConfig | one of `result`.  |

#### `CatalogQuery`

Lists templates in the engine's template order (stable during a session). All filters are optional and combine with AND.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `page` | PageRequest |  |
| 2 | `name` | string | exact internal template name; the result has at most one entry |
| 3 | `side` | string | default owning side, case-insensitive (e.g. "America", "China", "GLA") |
| 4 | `kind_of` | string | a KindOf name the template must have (e.g. "STRUCTURE", "INFANTRY", "HERO") |
| 5 | `buildable_only` | bool | only templates a player can build (not scripted-only or debris/effect templates) |

#### `ButtonIcon`

Where a button icon lives. The pixels are not sent: they are in the game's mapped image textures (texture_file in Art/Textures, the UV rectangle selects the icon).

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `image_name` | string | the mapped image's name |
| 2 | `texture_file` | string | the texture page's file name |
| 3 | `u0` | float | UV rectangle on the texture page, 0..1, origin top left |
| 4 | `v0` | float |  |
| 5 | `u1` | float |  |
| 6 | `v1` | float |  |
| 7 | `width` | uint32 | the icon's size in pixels |
| 8 | `height` | uint32 |  |
| 9 | `texture_width` | uint32 | the texture page's size in pixels |
| 10 | `texture_height` | uint32 |  |

#### `CatalogEntry`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `template_id` | uint32 | the engine's template id; the key objects rows refer to |
| 2 | `name` | string | internal template name |
| 3 | `display_name` | string | localized, UTF-8; empty when the template has none |
| 4 | `side` | string | default owning side, empty when none |
| 5 | `cost` | uint32 | build cost in money |
| 6 | `build_time_seconds` | float |  |
| 7 | `kind_of` | repeated string | the KindOf flags that matter for tooling: STRUCTURE, INFANTRY, VEHICLE, AIRCRAFT, HERO, DOZER, HARVESTER and the faction structure kinds |
| 8 | `buildable` | bool |  |
| 9 | `energy` | int32 | energy produced (positive) or consumed (negative) by a structure |
| 10 | `icon` | ButtonIcon | the build button's icon; unset when no command button builds it |

#### `CatalogResult`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `entries` | repeated CatalogEntry |  |
| 2 | `page` | PageInfo |  |

#### `ObjectQuery`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |

#### `ObjectDetail`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_name` | string |  |
| 3 | `display_name` | string |  |
| 4 | `owner_player_index` | int32 | -1 when neutral or unknown |
| 5 | `x` | float |  |
| 6 | `y` | float |  |
| 7 | `z` | float |  |
| 8 | `angle` | float | radians |
| 9 | `health` | float |  |
| 10 | `max_health` | float |  |
| 11 | `veterancy` | int32 | 0 regular .. 3 heroic |
| 12 | `status` | repeated string | ObjectStatus flag names that are set, e.g. "UNDER_CONSTRUCTION", "IS_ATTACKING" |
| 13 | `under_construction` | bool |  |
| 14 | `construction_percent` | float | 0-100, meaningful while under construction |
| 15 | `contained_ids` | repeated uint32 | objects inside this one (passengers, garrison) |
| 16 | `contained_by_id` | uint32 | the object this one is inside, 0 when none |
| 17 | `producer_id` | uint32 | the structure that produced it, 0 when none |
| 18 | `upgrades` | repeated string | completed object-level upgrades, internal names |
| 19 | `current_weapon` | string | weapon template name, empty when it has none |
| 20 | `target_object_id` | uint32 | what the AI is attacking or approaching, 0 when none |
| 21 | `has_target_position` | bool |  |
| 22 | `target_x` | float |  |
| 23 | `target_y` | float |  |
| 24 | `is_effectively_dead` | bool |  |

#### `ObjectsQuery`

Objects in a world rectangle and/or owned by one player, in ascending object id order. The cursor is an object id: the page holds the matching objects with an id >= cursor, so objects created or destroyed between pages never repeat or shift a page.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `page` | PageRequest |  |
| 2 | `has_area` | bool |  |
| 3 | `min_x` | float |  |
| 4 | `min_y` | float |  |
| 5 | `max_x` | float |  |
| 6 | `max_y` | float |  |
| 7 | `filter_player` | bool |  |
| 8 | `player_index` | int32 | used when filter_player; -1 selects neutral objects |
| 9 | `include_dead` | bool | effectively dead objects are skipped by default |
| 10 | `kind_of` | string | a KindOf name the object must have |

#### `ObjectRow`

A compact object row; the template is a template_id, named once in ObjectsResult.templates.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_id` | uint32 |  |
| 3 | `owner_player_index` | int32 |  |
| 4 | `x` | float |  |
| 5 | `y` | float |  |
| 6 | `health_percent` | uint32 | 0-100 |
| 7 | `veterancy` | int32 |  |
| 8 | `under_construction` | bool |  |
| 9 | `is_dead` | bool |  |

#### `TemplateRef`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `template_id` | uint32 |  |
| 2 | `name` | string |  |

#### `ObjectsResult`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `rows` | repeated ObjectRow |  |
| 2 | `templates` | repeated TemplateRef | the templates the rows of this page use |
| 3 | `page` | PageInfo |  |

#### `PlayerQuery`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `player_index` | int32 |  |

#### `ScienceOption`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `name` | string | internal science name |
| 2 | `display_name` | string | localized, UTF-8 |
| 3 | `cost` | uint32 | science purchase points |

#### `PowerStatus`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `power_name` | string |  |
| 2 | `object_id` | uint32 | the structure or unit that carries it |
| 3 | `template_name` | string |  |
| 4 | `ready_frame` | uint32 | absolute logic frame at which it is ready |
| 5 | `percent_ready` | float | 0-1, 1.0 = ready |
| 6 | `ready` | bool |  |

#### `PlayerDetail`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `player_index` | int32 |  |
| 2 | `name` | string |  |
| 3 | `side` | string |  |
| 4 | `money` | uint32 |  |
| 5 | `rank_level` | int32 |  |
| 6 | `rank_points` | int32 | experience points toward the next rank (Player::getSkillPoints) |
| 7 | `rank_points_next` | int32 | points at which the next rank is reached |
| 8 | `science_points` | int32 | unspent science purchase points |
| 9 | `sciences` | repeated string | owned sciences, internal names |
| 10 | `purchasable` | repeated ScienceOption | can be bought right now |
| 11 | `locked` | repeated ScienceOption | could be bought later: prerequisites or points are missing |
| 12 | `upgrades` | repeated string | completed player-level upgrades |
| 13 | `powers` | repeated PowerStatus | special powers the player can fire, from structures that are complete |
| 14 | `buildable` | repeated string | template names the player can build right now (prerequisites met), of the player's own side |

#### `MapQuery`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `include_minimap` | bool | the image is about 64 KiB; waypoints and positions are always returned |

#### `WaypointInfo`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `id` | uint32 |  |
| 2 | `name` | string |  |
| 3 | `x` | float |  |
| 4 | `y` | float |  |
| 5 | `z` | float |  |

#### `StartPositionInfo`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `start_position` | int32 | 0-based, as the lobby numbers them |
| 2 | `x` | float |  |
| 3 | `y` | float |  |

#### `Minimap`

The radar's terrain image. Pixel (px, py) of the image covers the world cell whose lower corner is (origin_x + px * cell_width, origin_y + py * cell_height); y grows downward in the image the same way it does in the world.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `width` | uint32 |  |
| 2 | `height` | uint32 |  |
| 3 | `rgba` | bytes | width * height * 4 bytes, row-major, R G B A |
| 4 | `origin_x` | float |  |
| 5 | `origin_y` | float |  |
| 6 | `cell_width` | float |  |
| 7 | `cell_height` | float |  |

#### `MapDetail`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `map_name` | string | as in MatchInfo.map_name |
| 2 | `has_extent` | bool |  |
| 3 | `min_x` | float |  |
| 4 | `min_y` | float |  |
| 5 | `max_x` | float |  |
| 6 | `max_y` | float |  |
| 7 | `waypoints` | repeated WaypointInfo |  |
| 8 | `start_positions` | repeated StartPositionInfo |  |
| 9 | `minimap` | Minimap | only when requested and available |

#### `ResyncQuery`

Asks the game to send this reader the current UiState, LobbyState, MatchInfo (STARTED) and a full StatsSnapshot again. Other readers are not affected.

| # | Field | Type | Meaning |
|---|---|---|---|

#### `ResyncResult`

| # | Field | Type | Meaning |
|---|---|---|---|

#### `FeedConfig`

Which frames this reader gets and how often. Defaults apply to every new connection.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `light_interval_ms` | uint32 | light StatsSnapshot cadence in game time, 0 = none; default 100, lowest 50 |
| 2 | `full_interval_ms` | uint32 | full StatsSnapshot cadence, 0 = none (the heavy fields then never arrive); default 250, lowest 100 |
| 3 | `events` | bool | default true |
| 4 | `camera` | bool | default true |
| 5 | `chat` | bool | ChatMessage frames, default true |
| 6 | `grids` | bool | PlayerGrid in full snapshots, default true |
| 7 | `supply` | bool | supply sources in full snapshots, default true |
| 8 | `tech` | bool | map-level tech structures in full snapshots, default true |
| 9 | `production` | bool | production queues in full snapshots, default true |
| 10 | `grid_resolution` | uint32 | cells per side of a PlayerGrid: 8, 16, 32 or 64; default 32 |

#### `FeedConfigQuery`

Every field that is set changes that setting; the rest stay as they are. The Response carries the effective configuration.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `reset` | bool | back to the defaults first, then apply the fields below |
