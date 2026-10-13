# Live stats pipe

The Generals Online client streams live match data to an out-of-process reader over a Windows named pipe. This document is the authoritative protocol reference. The wire schema is `GeneralsMD/Code/GameEngine/Source/Common/StatsExporter.proto`; a Java reference client lives in `Tools/StatsExporterClient/`.

## Transport

- Pipe name: `\.\pipe\generalsonline-stats-<pid>`, where `<pid>` is the game process id. List `\.\pipe\` and match the prefix to find running games.
- The game is the pipe server (overlapped, byte mode, duplex, one instance). The reader is the client. Only one reader can be connected at a time; a second connect attempt fails until the first disconnects.
- The pipe is always on, except in headless runs and replay-simulation processes, where no pipe exists. There is no setting.
- A reader that disconnects is simply dropped; the game keeps running and accepts the next reader. Readers should reconnect with backoff.

## Framing

Both directions use the same envelope:

| Bytes | Meaning |
|---|---|
| 4 | body length, unsigned 32-bit little-endian |
| 1 | frame type |
| n | protobuf body (`n` = body length) |

The length does not include the 5 header bytes. A body length of 0 or over the cap is a framing error: the game drops the reader (inbound), and a reader must treat it as a broken stream (outbound). Caps: outbound 262144 bytes, inbound 4096 bytes.

### Frame types

| Type | Direction | Message | When |
|---|---|---|---|
| 0x01 | game to reader | `MatchInfo` | match phase changes (LOADING, STARTED, ENDED) and on connect for a running match |
| 0x02 | game to reader | `StatsSnapshot` | periodic during a match |
| 0x03 | game to reader | `MatchSummary` | end of match, scores |
| 0x04 | game to reader | `ChatMessage` | each chat line shown to the local player |
| 0x05 | game to reader | `UiState` | the screen the game is on; first frame to a new reader |
| 0x06 | game to reader | `LobbyState` | game setup lobby changes; `in_lobby = false` on leaving |
| 0x10 | reader to game | `ChatSend` | send a chat line |

Unknown frame types must be skipped by length, so the protocol can grow. Inbound types other than 0x10 are skipped by the game.

## Ordering and lifecycle

On connect the game sends, in order: `UiState`, then `MatchInfo` (phase LOADING from the cached loading data, or STARTED for a running match), then `LobbyState` if the user sits in a lobby. After that frames flow as events happen.

- `StatsSnapshot` is gated: none is sent until a STARTED `MatchInfo` has been sent on that connection. A reader connecting mid-match gets STARTED first, then snapshots.
- Snapshots are sampled every 15 logic frames (half a second at 30 fps). While the game is stalled (waiting for network players) a snapshot with no players is sent about every 250 ms so the reader can see the stall; like normal snapshots, these wait until STARTED `MatchInfo` has been queued for that connection.
- Nothing is built or queued while no reader is connected, and the outbound queue is cleared when a new reader connects, so a reader only sees frames produced after it connected (plus the connect-time sequence above).
- `MatchSummary` follows the ENDED `MatchInfo`.
- Chat is independent of the match phase and flows in menus, lobbies and rooms.

## Backpressure and limits

- Enqueueing never blocks the game. Frames go into a 1 MiB ring of whole frames; a frame that does not fit is dropped (the new one, never a partial). A slow reader therefore misses frames but never corrupts the stream. Drain the pipe promptly.
- A single frame is capped at 256 KiB (the pipe cap is 262144 bytes).
- Per snapshot: at most 128 events (events pending between snapshots are capped at 512; once full, new events are dropped); production queues cover at most 16 structures with 6 entries each; at most 64 sciences, 64 upgrades, 32 tech structures per player, 64 map tech structures, 64 supply sources, 64 selected object ids; grids are 32 by 32 cells.
- Inbound `ChatSend` text is capped at 1024 bytes; the game keeps a mailbox of 8 pending sends and drops more.

## Inbound chat (ChatSend)

`ChatSend { channel, text, audience }` is run on the main thread through the same function the matching chat box uses for a typed line, so slash commands and the game's own chat limits apply exactly as if the player had typed it.

| Channel | Goes to |
|---|---|
| `CHAT_CHANNEL_MATCH` | in-match chat; `audience` chooses everyone, allies or observers |
| `CHAT_CHANNEL_GAME_LOBBY` | online game setup chat |
| `CHAT_CHANNEL_ROOM` | online chat room |
| `CHAT_CHANNEL_LAN_LOBBY` | LAN lobby chat |
| `CHAT_CHANNEL_LAN_GAME` | LAN game setup chat |

`audience` only matters for the match channel. For an observer, "everyone" means observers. The text must be non-empty UTF-8. There is no acknowledgement; the line shows up as a `ChatMessage` when the game displays it.

## UiState

`UiState { screen, layout, game_mode, paused }` reports which screen the game shows. `screen` is derived from the top shell layout: main menu, single player, skirmish setup, LAN lobby, LAN game setup, online welcome, online lobby, online game setup, quickmatch, replay menu, options, save/load, then the in-game screens. `layout` is the raw layout file name (empty when none). `game_mode` and `paused` are meaningful for in-game screens only.

## Lobby state and prediction

`LobbyState` mirrors the game setup lobby (online, LAN or skirmish): kind, map, slots, online lobby data and the local client's peer connections. It is sent on every change and with `in_lobby = false` when the user leaves.

Random choices (random side, random color, random start position) are not decided until the match loads. While in the lobby the exporter predicts them on a private mirror of the game's random generator; `LobbyState.prediction_valid` and `prediction_start_positions_valid` say which predicted fields (`predicted_side`, `predicted_color`, `predicted_start_position`, `predicted_observer_camera`) can be trusted. On entering LOADING the exporter compares the last prediction with the values the engine actually resolved; the LOADING `MatchInfo` carries the resolved values plus `has_prediction_check` and `prediction_matched`. Treat lobby predictions as a preview, and the LOADING `MatchInfo` as the truth.

## Scope and known limitations

- Stats cover what the local client simulates and can see; they are an exporter for casting, not an anti-cheat surface.
- Online identity data (match id, user ids, IPs) is exported exactly as the client knows it; fields are 0 or empty when unknown.
- A frame dropped to backpressure is not retransmitted; counters in snapshots are cumulative where noted, so the next snapshot recovers them, but discrete events in a dropped snapshot are lost.
- Only one reader at a time. Inbound frames other than ChatSend are ignored.
- Known engine constraint: the engine's `operator new` is only 4-byte aligned, but protobuf `RepeatedField` stores tag bits in the low 3 bits of its heap pointer. Protobuf messages with repeated scalar fields (`cells`, `selected_object_ids`) must therefore be arena-allocated in the exporter, on an arena whose blocks come from `_aligned_malloc`, never from the engine's `new`.
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
| 2 | `ready_frame` | uint32 |  |
| 3 | `percent_ready` | float | 1.0 = ready now |

#### enum `StatEventKind`

| Value | Name | Meaning |
|---|---|---|
| 0 | `STATEVENT_ABILITY_USE` |  |
| 1 | `STATEVENT_SCIENCE_PURCHASE` |  |
| 2 | `STATEVENT_KILL` |  |
| 3 | `STATEVENT_LOSS` |  |
| 4 | `STATEVENT_BUILD` |  |
| 5 | `STATEVENT_EVACUATE` |  |
| 6 | `STATEVENT_VETERANCY_CHANGE` |  |
| 7 | `STATEVENT_SALVAGE_UPGRADE` |  |
| 8 | `STATEVENT_BEACON` | location set; category is the player's beacon |
| 9 | `STATEVENT_PLAYER_LEFT` | variant: "left" (quit or disconnect, from the network leave command) or "self_destruct" |
| 10 | `STATEVENT_PLAYER_DEFEATED` | the engine marked the player defeated |
| 11 | `STATEVENT_SELL` | a structure finished selling; primary_name is its template, cost the refund |

#### `StatEvent`

A discrete gameplay-outcome event, drained into the next StatsSnapshot. category is a coarse bucket (SUPERWEAPON/STRUCTURE/INFANTRY/VEHICLE/ABILITY/SCIENCE/WEAPON/ARMOR/OTHER).

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

#### `StatsSnapshot`

One periodic sample, one entry per exported player.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `frame` | uint32 |  |
| 2 | `players` | repeated PlayerStats |  |
| 3 | `events` | repeated StatEvent |  |
| 4 | `connections` | repeated OnlineConnection | the local client's own peer connections |
| 5 | `grids` | repeated PlayerGrid | one per exported player |
| 6 | `supply` | repeated SupplySource | supply sources on the map, capped, non-empty first |
| 7 | `replay_total_frames` | uint32 | frames in the replay being played back, 0 otherwise |
| 8 | `game_speed_fps` | int32 | FramePacer's actual logic time scale in frames per second (30 is normal speed); 0 = frozen or halted, -1 = uncapped |
| 9 | `game_speed_ratio` | float | the same as a ratio of normal speed |
| 10 | `tech` | repeated TechStructure | neutral and captured tech structures on the map, capped |
| 11 | `camera` | LocalCamera | this client's own camera: client-side state, differs per client |
| 12 | `selected_object_ids` | repeated uint32 | this client's current selection, capped; client-side like the camera |
| 13 | `network_stalled` | bool | the logic is waiting on a peer (Network::isStalling) |
| 14 | `stalled_frames` | uint32 | how long, in logic frames at the nominal rate, derived from wall-clock time |

#### `TechStructure`

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `template_name` | string |  |
| 3 | `x` | float |  |
| 4 | `y` | float |  |
| 5 | `owner_player_index` | int32 | -1 when neutral |

#### `LocalCamera`

The local tactical view. Per client: not part of the synchronized game state.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `x` | float | world position the camera looks at |
| 2 | `y` | float |  |
| 3 | `zoom` | float |  |
| 4 | `angle` | float |  |
| 5 | `pitch` | float |  |
| 6 | `height_above_ground` | float |  |

#### `PlayerGrid`

Unit and structure counts per cell over the map's playable extent (MatchInfo world_*), row-major from the min corner. Counts objects with KINDOF_STRUCTURE, INFANTRY, VEHICLE or AIRCRAFT that are not dead; projectiles, effects and inert objects are not counted.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `player_index` | int32 |  |
| 2 | `width` | uint32 | always 32 |
| 3 | `height` | uint32 | always 32 |
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
| 13 | `army_value` | int32 | sum of build costs over the live objects |
| 14 | `live_superweapon_structure_count` | uint32 |  |
| 15 | `live_template_counts` | repeated TemplateCount |  |
| 16 | `readiness` | repeated ReadinessSample |  |
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
| 4 | `ready_frame` | uint32 |  |
| 5 | `percent_ready` | float | 1.0 = ready |
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
| 3 | `percent_complete` | float | 0-100, the front entry only |
| 4 | `quantity` | uint32 | units still to come out of this entry |

#### `ProductionQueue`

What one structure is producing, front of the queue first.

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `object_id` | uint32 |  |
| 2 | `structure_template` | string |  |
| 3 | `entries` | repeated ProductionItem |  |

#### `MatchSummary`

Sent once from the score screen, before the game data is cleared.

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
| 6 | `is_winner` | bool |  |
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

A slot's outcome as of the ENDED frame.

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
| 5 | `color` | uint32 | packed 0xAARRGGBB |
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
| 4 | `map_name` | string |  |
| 5 | `start_unix_time` | uint64 |  |
| 6 | `duration_frames` | uint32 | only meaningful on ENDED |
| 7 | `players` | repeated MatchPlayer |  |
| 8 | `replay_path` | string | absolute, closed and flushed on ENDED; empty when no replay exists |
| 9 | `replay_is_playback` | bool | true when replay_path is an existing file being watched |
| 10 | `completed` | bool | a decisive result was reached; always false on STARTED |
| 11 | `local_player_index` | int32 | the player at this PC, -1 when none |
| 12 | `online` | OnlineMatch | online matches only |
| 13 | `observer_camera` | ObserverCamera | set only when the local player is an observer or this is a replay |
| 14 | `has_prediction_check` | bool |  |
| 15 | `prediction_matched` | bool | every predicted side, color, start position and the observer camera were right |
| 16 | `random_state_clean` | bool | the logic generator was untouched between the lobby seed and the resolution |
| 17 | `has_world_extent` | bool |  |
| 18 | `world_min_x` | float |  |
| 19 | `world_min_y` | float |  |
| 20 | `world_max_x` | float |  |
| 21 | `world_max_y` | float |  |

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
| 1 | `match_id` | uint64 | the Generals Online match id |
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
| 4 | `endpoint` | Endpoint | unset when relayed or not known |

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
