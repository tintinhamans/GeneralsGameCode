# WorldBuilder Qt

A port of the Zero Hour WorldBuilder map editor from MFC to Qt. It keeps the classic workflow and
adds a dark theme, faster search and repair tools for maps made against other mods, and editors
for `map.ini` and other INI files. It builds from this repository, so it reads the same game data
and draws the same Direct3D 9 effects as the Contra game build.

It builds on Adriane's WorldBuilder, whose additions are listed in [Adriane's
changelog](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/Tools/WorldBuilder/docs/ChangeLog%20-%20Adriane.txt).

## Pages

* [Selection & Objects](WorldBuilder-Qt-Objects.md) - selection, object placement and the build list
* [Terrain Brushes](WorldBuilder-Qt-Terrain-Brushes.md) - brush files, new textures and Terrain.ini keys
* [Viewing & D3D9 Effects](WorldBuilder-Qt-Viewing.md) - model display options and the Direct3D 9 effects
* [Scripts & INI](WorldBuilder-Qt-Scripts-and-INI.md) - the script editor and the map.ini and INI editors
* [Map Generator & MCP](WorldBuilder-Qt-Map-Generator.md) - the map generator and the MCP server
* [MCP Server](WorldBuilder-Qt-MCP.md) - setup, tools, conventions and limits of the MCP server

## Running

WorldBuilder loads game data from its own folder, so it must sit inside a Zero Hour install. Copy
the exe and the Qt DLLs next to it into the game folder and run it from there. The Qt runtime ships
with the build; no separate Qt install is needed to run it.

WorldBuilder and the game use separate instance identifiers, so either can start while the
other is running. WorldBuilder does not consume a game instance number or make the game load
an `Options_InstanceXX.ini` file.

| Exe | Renderer |
|---|---|
| `WorldBuilderZH_Qt.exe` | Direct3D 8 |
| `WorldBuilderZH_Qt_D3D9.exe` | Direct3D 9, draws the D3D9 effects |

The Direct3D 9 exe loads its shaders from the `shaders\` folder in the game folder, the same one the
Contra game build uses. Shaders from a different build of the source tree load but draw terrain,
roads and water wrong.

## Interface

* Theme - (Dark, light, or following Windows. Stored in `WorldBuilder.ini`.)
* Tool windows remember their size and position, and stay on screen. Window > Reset Window Positions
puts them back. A window follows the cursor onto another monitor when dragged there. A window with
no saved position opens centered on the main window, and a saved position that is no longer on any
monitor is pulled back into view.
* Undo history depth is a setting; the default is 64 steps.
* Long combo boxes filter as you type when "Search in combo boxes" is ticked in the Entity Finder.
The NewSearch box beside it turns on live filtering in the tree pickers.
* Shift+F5 resets the Direct3D device.
* Brush size has hotkeys, and the brush panels accept values past the slider's cap.
* Open Map shows a preview for every map, including maps packed in a `.big`. A map with no `.tga`
gets a generated one.

## Building

WorldBuilder is 32-bit and needs Qt 5.15.2 for 32-bit MSVC (`msvc2019`). Run inside an x86 MSVC
environment:

```
cmake --preset win32-qt-d3d9 -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019"
cmake --build --preset win32-qt-d3d9 --target z_worldbuilder
cmake --build --preset win32-qt-d3d9 --target rts_shaders
```

Use the `win32-qt` preset for the Direct3D 8 exe. The [Qt build
guide](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/Tools/WorldBuilder/docs/QT-BUILD.md)
covers the Qt install and the debugging switches.
