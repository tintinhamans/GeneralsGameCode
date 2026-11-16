# Map Generator

Map Generator > Generate Map builds a playable skirmish map into the open document. It lays Perlin
terrain with cliffs and textures, symmetric start positions on flat pads, equal supply sources per
player, roads between the starts, and scattered trees and rocks. Trees, rocks and textures come from
the loaded game data, so it works with any mod. The map grows when the player count needs room.
Randomize rerolls the settings. One Undo restores the previous map.

# MCP server

WorldBuilder can be driven by an AI assistant through a Model Context Protocol (MCP) server. The
server runs as `python -m tools.worldbuilder_mcp` from the repository root and talks to a running
editor. It can open maps, place and edit objects, paint terrain, and edit teams, waypoints and
scripts. The MCP menu turns the editor's side on or off. Setup, the tool list and the limits are on
the [MCP Server](WorldBuilder-Qt-MCP.md) page.

