# Game MCP Bridge

Written by contraZH. The bridge is an upstream feature that upstream's wiki does not cover.

A localhost command bridge that lets a tool drive the running game: pause and step it, inspect
objects and players, give orders, change values, run scripts, move the camera and take screenshots.
The game is built with it by default (`RTS_BUILD_OPTION_GAME_MCP`), but it only exists when the game
is started with `-mcp`. Without that flag nothing is created.

The bridge speaks line-delimited JSON on `127.0.0.1`. Upstream drives it with a Python MCP server
(`scripts/worldbuilder-mcp/game_server.py` in GeneralsGameCode_Modding); contraZH does not ship that
server.

## Command line

| Flag | Effect |
|---|---|
| `-mcp` | Listen on `127.0.0.1:47801` |
| `-mcpport:<n>` | Listen on another port |
| `-mcpGame <map>` | Start a skirmish on that map (map name or part of its path) |
| `-mcpPlayers <list>` | `Faction[:team]` of the local player (or `Observer`), then the AI players as `Faction:difficulty[:team]`, comma separated |
| `-mcpCash <n>` | Starting cash (default: the game's) |
| `-mcpStartPaused` | Pause as soon as the game has started |
| `-seed <n>` | Random seed |
| `-headless` | No window, graphics or sound; only the game logic runs |

With only the local player in `-mcpPlayers` the game is a sandbox without opponents, in which nobody
wins or loses.

Example:

```
generalszh.exe -mcp -mcpGame "Tournament Desert" -mcpPlayers FactionAmerica,FactionChina:hard -mcpStartPaused
```

## Commands

| Group | Commands |
|---|---|
| Game | `game.status`, `game.pause`, `game.resume`, `game.step`, `game.set_speed`, `game.messages`, `game.save`, `game.load`, `game.quit` |
| Objects | `objects.list`, `objects.get`, `objects.templates`, `objects.spawn`, `objects.destroy`, `objects.set` |
| Players | `players.list`, `players.get`, `players.set` |
| Orders | `orders.move`, `orders.attack`, `orders.stop`, `orders.guard`, `orders.sell`, `orders.build`, `orders.produce`, `orders.upgrade`, `orders.special_power`, `orders.purchase_science` |
| Scripts | `scripts.run`, `scripts.set_active`, `scripts.flags`, `scripts.counters` |
| View | `view.camera`, `view.screenshot` |

Object rows include `producer_id`, which identifies the factory or supply truck that created a unit.
This allows supply crew counts and worker losses to be checked for each truck.
| Other | `list_commands`, `app.info` |

Orders go through the normal command list, like a player's clicks. `objects.spawn`, `objects.destroy`,
`objects.set`, `players.set`, `scripts.run`, `scripts.set_active` and setting flags or counters change
the game directly and are not recorded, so a replay of a game changed this way does not play back the
same.

In contraZH these direct changes are refused in LAN and online games, where they would desync the
match. Reading state and giving orders still work there.

`orders.move` accepts `reverse = true` to send the same reverse order as the terrain-click command.

# AI Matches

A skirmish between AI players, started from the command line, watched by an observer and logged:
what every player built, bought and lost, which of its scripts ran and what the script engine
complained about. Use it to judge AI scripts without watching the match.

| Flag | Effect |
|---|---|
| `-aiMatch <map>` | Map name or part of its path; starts the match |
| `-aiPlayers <list>` | `Faction:difficulty[:team]`, comma separated, e.g. `FactionChina:hard,FactionGLA:hard` (difficulty `easy`, `normal` or `hard`) |
| `-aiMatchLog <file>` | Where the log goes (JSON, one record per line) |
| `-aiMatchMinutes <n>` | Game time after which the match stops (default 30) |
| `-aiMatchInterval <seconds>` | Game time between snapshots (default 10) |
| `-aiMatchCash <n>` | Starting cash (default: the game's) |
| `-aiMatchScripts <file>` | AI scripts to use instead of `Data\Scripts\SkirmishScripts.scb` |
| `-aiMatchAIData <file>` | An `AIData.ini` read on top of the game's, to try out changes to it |
| `-seed <n>` | Random seed, for a repeatable match |
| `-headless` | Run the logic only, as fast as it can |

An AI match runs as its own instance, so it can start while WorldBuilder or another game is open.
