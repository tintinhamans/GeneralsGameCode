# Changelog

## 1.0.0

First release of `com.generalsonline:stats-pipe-client`.

- The library (`lib/`): `StatsPipe` (builder, auto-discovery of running games preferring a free one, reconnect with backoff, an executor for the callbacks, `sendChat` with the documented limits), `StatsListener` (one method per frame type, lifecycle, errors), `MatchState` with `StateListener` change callbacks (players joined with MatchInfo, light/full snapshot merge, player state, match lifecycle, camera, world extent and map-level data, bounded event and chat buffers), `DumpWriter`/`DumpReader` with replay paced by game time, and the `Format` helpers. Generated protobuf classes come from the engine's `StatsExporter.proto`. Javadoc on every public type and member.
- The test client (`app/`), now written against the library's public API only: the commands `watch`, `dump`, `replay`, `chat`, `gui`, `schema` and the new `example`; the GUI with the tabs Players, Minimap, Events, Chat, Lobby, Match, Frames and the new Code tab.
- The example-code generator: eight scenarios (print every frame, track a player's stat, react to an event kind, draw the camera view box, show the lobby with predicted factions, send a chat line, record and replay a dump, use the state from a UI thread), filled with the selected Players column, event kind or chat channel. Each template is compile-checked, and the programs are in `examples/`.
- Changed from the earlier single-project client: the packages are `com.generalsonline.stats` (API), `.proto`, `.internal` and, for the client, `.app`; `FrameListener` became `StatsListener` (with `onFrame` instead of a frame tap), `StatsPipeClient` became `StatsPipe`, `DumpFile` became `DumpWriter` and `DumpReader`.
