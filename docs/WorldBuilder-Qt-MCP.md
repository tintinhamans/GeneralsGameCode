# WorldBuilder MCP Server

An AI assistant can drive WorldBuilder through a Model Context Protocol (MCP) server. The assistant
opens maps, places and edits objects, shapes and paints terrain, and edits teams, waypoints, areas
and scripts, all in the running editor.

The server has two halves.

* A bridge compiled into WorldBuilder carries out each request on the editor's UI thread, through
the same code paths and undo stack as a manual edit.
* A Python program, `tools/worldbuilder_mcp` in the contraZH repository, speaks MCP over stdio to
the assistant and passes each tool call to the bridge.

The `.map` file is never written from outside the editor. A map changes only when the editor saves
it.

Upstream's WorldBuilder MCP server is not part of contraZH. This one replaces it.

## Requirements

| Need | Detail |
|---|---|
| WorldBuilder | A contraZH build, running inside a Zero Hour install. See [WorldBuilder Qt](WorldBuilder-Qt.md#running). |
| Python | Python 3 on Windows. The server uses the standard library only, so there is nothing to install. |
| Repository | A checkout of contraZH, because the server runs from `tools/worldbuilder_mcp`. |
| MCP client | Any client that starts stdio servers. |

## Setup

### 1. Turn the editor's side on

WorldBuilder's **MCP** menu has two entries.

| Entry | Effect |
|---|---|
| Enable MCP Server | Turns the bridge on or off. The setting is remembered between sessions. |
| Server Information... | Shows the state of the bridge in this editor. |

With the bridge off, the editor is hidden from discovery and rejects new requests. Turning it off
does not stop a Python server that a client already started.

### 2. Register the server with the client

The client must start `python -m tools.worldbuilder_mcp` with the repository root as its working
directory.

```json
{
  "mcpServers": {
    "generals-worldbuilder": {
      "command": "python",
      "args": ["-m", "tools.worldbuilder_mcp"],
      "cwd": "C:\\path\\to\\contraZH"
    }
  }
}
```

Some clients have no working directory field. Give those an absolute Python path and put the
repository root in `PYTHONPATH`.

```json
{
  "mcpServers": {
    "generals-worldbuilder": {
      "command": "C:\\Path\\To\\python.exe",
      "args": ["-m", "tools.worldbuilder_mcp"],
      "env": {
        "PYTHONPATH": "C:\\path\\to\\contraZH"
      }
    }
  }
}
```

### 3. Let the server start the editor (optional)

`launch_editor` starts WorldBuilder when no MCP-enabled editor is running. It needs the path of the
executable, from the `--zero-hour-editor` option or the `WORLDBUILDER_ZERO_HOUR_PATH` environment
variable.

```json
"args": ["-m", "tools.worldbuilder_mcp", "--zero-hour-editor", "C:\\Games\\Zero Hour\\WorldBuilderZH_Qt_D3D9.exe"]
```

The editor opens as a normal visible window. `launch_editor` never starts a second editor while one
is running.

## Server options

| Option | Default | Meaning |
|---|---|---|
| `--timeout-ms <n>` | 5000 | How long an ordinary command may take in the editor. |
| `--zero-hour-editor <path>` | none | Executable that `launch_editor` starts. |
| `--temp-root <path>` | system temp | Folder that holds the bridge files. Meant for tests. |

Long commands have their own limits whatever `--timeout-ms` says. A chunk of a bulk object edit may
take 45 seconds. Opening, saving, creating or closing a map may take 120 seconds, and so may
`generate_preview` and `capture_view`.

| Environment variable | Meaning |
|---|---|
| `WORLDBUILDER_ZERO_HOUR_PATH` | Same as `--zero-hour-editor`. The option wins when both are set. |

## Tools

Every tool except `list_editors` and `launch_editor` takes an optional `editor_id`. It can be left
out while one editor is running. With several editors open, call `list_editors` first and pass the
id it returns.

Tools that change the map also take `expected_revision` and `operation_id`, described under
[Revisions and retries](#revisions-and-retries).

### Editor and maps

| Tool | Does |
|---|---|
| `list_editors` | Lists running editors that have the bridge on. |
| `launch_editor` | Starts the configured editor if none is running. `wait_ms` (default 15000) is how long to wait for it. |
| `get_bridge_info` | Returns the bridge version, edition and capabilities. |
| `list_blocking_dialogs` | Lists message boxes that are holding the editor up. |
| `list_user_maps` | Lists the maps in the user Maps folder. |
| `open_map` | Opens a map by `path`. |
| `new_map` | Creates a map from `width`, `height`, `default_height` (0 to 255) and `border_size`. |
| `close_map` | Closes the active map. |
| `get_map_state` | Returns the active map, its revision, dimensions, border, cell size and height scale. |
| `validate_map` | Checks object ids, owners, templates, bounds and start positions. |
| `save_map` | Saves the active map. |
| `save_map_as` | Saves under a new `path`. `overwrite` defaults to false. |
| `undo`, `redo` | Step through the editor's undo stack. |

`open_map`, `new_map` and `close_map` take `on_unsaved`, which decides what happens to unsaved
changes in the current map.

| `on_unsaved` | Effect |
|---|---|
| `error` | The call fails and the map stays open. This is the default. |
| `save` | The current map is saved first. |
| `discard` | The changes are dropped. |

### Objects

| Tool | Does |
|---|---|
| `list_templates` | Lists the object templates the editor loaded. Takes `filter`, `offset` and `limit`. |
| `list_objects` | Lists objects on the map. Filters are `filter`, `template`, `owner`, `selected`, `waypoint` and the box `min_x`, `max_x`, `min_y`, `max_y`. Pages with `offset` and `limit`. |
| `get_object` | Returns one object by `object_id`. |
| `add_object` | Places `template` at `x`, `y`, with optional `z`, `angle` and `owner`. |
| `add_objects` | Places up to 500 objects from `items`. |
| `update_object` | Changes `template`, `x`, `y`, `z`, `angle`, `owner`, `script_name` or `veterancy`. |
| `update_objects` | Updates up to 500 objects from `items`. |
| `delete_object` | Deletes one object. |
| `delete_objects` | Deletes up to 500 objects by `object_ids`. |
| `select_object` | Selects an object in the editor. |

Object reads report `is_unit`, `weapon_salvager` and `veterancy`. Veterancy runs from 0 to 3, which
are Regular, Veteran, Elite and Heroic. `script_name` is the unique name scripts use to refer to
the object.

### Players and teams

| Tool | Does |
|---|---|
| `list_players` | Lists the map's players. |
| `list_teams` | Lists teams and their owners. |
| `create_team` | Creates a team from `name` and `owner`. `singleton` defaults to false. |
| `update_team` | Changes a team's `owner` or `singleton` flag. |
| `delete_team` | Deletes a team that is unused and not a default team. |

### Waypoints

| Tool | Does |
|---|---|
| `list_waypoints` | Lists waypoints and the directed links between them. |
| `create_waypoint` | Creates a waypoint from `name`, `x`, `y` and optional `z`. |
| `update_waypoint` | Renames or moves the waypoint `waypoint_id`. |
| `delete_waypoint` | Deletes a waypoint and every link to or from it. |
| `connect_waypoints` | Adds a link from `from_id` to `to_id`. |
| `disconnect_waypoints` | Removes that link. |

### Areas

Areas are polygons. A `scripting` area is a trigger region, and a `water` area is a water or river
polygon.

| Tool | Does |
|---|---|
| `list_areas` | Lists areas. `kind` is `all`, `scripting` or `water`. |
| `create_area` | Creates an area from `name`, `kind` and 3 to 256 `points`. Optional keys are `export_with_scripts`, `river`, `river_start` and `layer`. |
| `update_area` | Changes those keys, or replaces all points, for `area_id`. |
| `delete_area` | Deletes an area. |

### Scripts

| Tool | Does |
|---|---|
| `list_script_types` | Lists condition and action types with their ids, names and parameter types. |
| `list_sciences` | Lists sciences and general powers, with grantability and purchase cost. |
| `list_upgrades` | Lists upgrades and whether each applies to a player or an object. |
| `list_scripts` | Lists the scripts of one player (`player_index`) or of all players. |
| `get_script` | Returns one script with its conditions, actions and false actions. |
| `upsert_script` | Creates or replaces a script. |
| `delete_script` | Deletes a script. |

`get_script`, `upsert_script` and `delete_script` find a script by `player_index` and `name`, plus
`group` when it sits inside a script group. See [Writing scripts](#writing-scripts).

### Terrain

| Tool | Does |
|---|---|
| `get_terrain_heights` | Reads heights for the rectangle `x`, `y`, `width`, `height`. |
| `set_terrain_heights` | Sets heights from `points`, each with `x`, `y` and `height`. |
| `list_terrain_textures` | Lists the terrain texture classes. |
| `get_terrain_cells` | Reads texture classes, blends, cliff and passability state and flip state for a rectangle. |
| `set_terrain_cells` | Paints `cells`, each with `x`, `y` and any of `texture_class`, `passable` and `auto_blend`. |
| `get_playable_areas` | Reads the playable-area boundaries. |
| `set_playable_areas` | Replaces them. Each boundary is the top-right cell of a rectangle that starts at (0, 0). |

`auto_blend` is on by default. It makes the editor rebuild its blend layers around each painted
cell.

### Roads and bridges

| Tool | Does |
|---|---|
| `list_linear_features` | Lists road and bridge segments. `kind` is `all`, `road` or `bridge`. |
| `create_linear_feature` | Creates a segment of `kind` and `template` from `start` to `end`. `corner` is `smooth`, `angled` or `tight`. |
| `delete_linear_feature` | Deletes the segment `feature_id`. |

A bridge template marked as a landmark is placed as a single object.

### Map settings

| Tool | Does |
|---|---|
| `get_map_settings` | Reads the name, description, player count, time of day, weather, water height and global lighting. |
| `update_map_settings` | Changes any of them. |

`update_map_settings` takes `name`, `description`, `player_count` (1 to 8), `time_of_day` (1 to 4),
`weather` and `water_height`. Lighting needs `lighting_target` (`terrain` or `objects`) and
`light_index` (0 to 2), then any of `ambient`, `diffuse` and `direction` as `{x, y, z}`.

### View and output

| Tool | Does |
|---|---|
| `focus_view` | Moves the 3D view to `x`, `y`. |
| `capture_view` | Saves the current view as a BMP and returns its path. |
| `generate_preview` | Writes the map's `.tga` minimap preview beside the map. |

## Resources

| URI | Content |
|---|---|
| `worldbuilder://editors` | The running editors, as `list_editors` returns them. |
| `worldbuilder://user-maps` | The maps in the user Maps folder. |
| `worldbuilder://scripting-guide` | A short guide to writing scripts through the server. |

An assistant should read the scripting guide before it writes scripts.

## Conventions

### Coordinates and units

| Value | Unit |
|---|---|
| Object, waypoint and area `x`, `y`, `z` | World units. |
| Object `angle` | Degrees. |
| Terrain height `x`, `y` | Height-map vertex indices. |
| Terrain `height` | The raw value, 0 to 255. |
| Terrain cell `x`, `y` | Quad indices, which stop one short of the vertex extent. |

`get_map_state` reports the world cell size and the height scale that convert between the two.

### Owners

An object's `owner` is either a player slot or a team name.

* A number from 1 to 8 means that lobby player. The server stores it as the runtime team
`teamplayer0` to `teamplayer7`, so the object goes to whoever takes that slot in the match.
* A string must be the exact name of a map team from `list_teams`.

A slot number is the right way to give a preplaced object to a real player. A team such as
`SkirmishAmerica` belongs to a template player that is inactive in a match.

### Map paths

`open_map` and `save_map_as` take one of three path forms.

* An absolute path.
* A path relative to the user Maps folder.
* A map folder, whose `.map` file has the same name.

Every path must stay inside the user Maps folder, normally
`Documents\Command and Conquer Generals Zero Hour Data\Maps`. `list_user_maps` takes no path.

### Revisions and retries

The map has a revision number that rises with every change. `get_map_state` returns it, and so does
every changing tool.

| Argument | Use |
|---|---|
| `expected_revision` | The call fails if the map has moved past this revision, so an edit never lands on a map the assistant has not seen. |
| `operation_id` | A key of up to 128 characters. A retry with the same key returns the first result and does not apply the edit twice. |

### Undo

Every change goes through the editor's undo stack, so Ctrl+Z in the editor reverses it.

A bulk object call of up to 15 items is one undo step. A larger call is split into chunks of 15, and
each chunk is its own undo step. The server chains the revision numbers across the chunks and
returns one merged result.

## Writing scripts

Condition and action ids belong to the running editor, so they differ between builds. Call
`list_script_types` on every connection and pick a type by its name. Never reuse an id from an
earlier session.

`list_script_types` also gives each type's parameters in order, with a numeric type and a name such
as `SIDE`, `SCIENCE` or `UPGRADE`. `get_script` returns each parameter with its type, value and the
text the editor shows.

`upsert_script` takes these keys.

| Key | Meaning |
|---|---|
| `player_index`, `name`, `group` | Where the script lives. |
| `comment`, `condition_comment`, `action_comment` | The three comment fields. |
| `active`, `one_shot`, `subroutine` | The script's flags. |
| `easy`, `normal`, `hard` | The difficulties the script runs in. |
| `delay_seconds` | Seconds between evaluations. |
| `conditions` | Up to 512 entries of `type`, `or_group` and `parameters`. |
| `actions`, `false_actions` | Up to 512 entries each of `type` and `parameters`. |

A parameter is a number, a string, a boolean, or `{x, y, z}` for a coordinate. A condition or
action takes at most 12.

Conditions that share an `or_group` are ANDed, and the groups are ORed together. A script with no
conditions never runs. A script that should always run needs the `[Scripting] True.` condition.

`upsert_script` replaces the player's whole script list in one undo step, so a rejected script
leaves no partial edit behind.

Global one-shot setup scripts belong on `PlyrCivilian`, with `one_shot` set and all three
difficulties on. `list_players` gives its index.

## Limits

| Limit | Value |
|---|---|
| Request size | 1 MiB |
| Response size | 32 MiB |
| List page | 500 items |
| Bulk object call | 500 items, sent in chunks of 15 |
| Terrain heights or cells per call | 4096 |
| Area points | 3 to 256 |
| Playable-area boundaries | 64 |
| Conditions, actions or false actions per script | 512 each |
| Parameters per condition or action | 12 |

## Safety

* The bridge opens no network port. It is desktop automation between two programs of the same
Windows user, so untrusted programs should not run in that desktop session.
* Requests wait in a queue for the editor's UI thread. A long open or save does not block the
transport.
* Map paths cannot leave the user Maps folder. Path traversal and links that point outside it are
rejected.
* Bridge files live only in `%TEMP%\GeneralsWorldBuilderMcp`. Subfolders and links are rejected, and
a response file is never written over an existing one.
* `capture_view` writes only `.bmp` files directly inside that folder.

## Differences from upstream

Two upstream features are missing, because both need changes to the game's logic and contraZH
keeps WorldBuilder changes out of the shared engine.

* The `apply_max_progression` tool.
* Editing an object's Salvage level. The read-only `weapon_salvager` flag is still reported.

## Tests

The tests use a fake editor window, so they need neither the game nor the editor.

```
python -m unittest discover -s tools/worldbuilder_mcp/tests -v
```
