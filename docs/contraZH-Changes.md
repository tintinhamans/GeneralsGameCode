# contraZH Changes

Changes in [contraZH](https://github.com/triatomic/contraZH), a fork of GeneralsGameCode_Modding.
Everything here is additional to upstream; the rest of these docs still applies.

* Most changes are client-side and read from `Options.ini`; each setting states its default.
* [Gameplay Fixes](contraZH-Gameplay-Fixes.md) are always on and fix retail simulation bugs.
* A few simulation rules are read from the mod's `GameData.ini`; each states its default.
* NVIDIA cards now load DXT1 textures natively, as AMD and Intel cards do. This halves their memory use and makes one-bit alpha visible.
* The map editor has its own page, [WorldBuilder Qt](WorldBuilder-Qt.md).
* A W3D vertex material with `Specular To Diffuse` ticked now acts as a lamp. With [Path tracing](contraZH-Options.md#path-tracing) on, it lights its surroundings with its emissive colour times its texture, by the [lamp formula](contraZH-Options.md#lamp-formula). The checkbox had no effect before.
* The FXList [Shockwave](dx9-Effect-Shaders.md#shockwave-fxlistini) block can ripple water. `Rings` sets how many waves ride in the ring, and `AllowedSurface = WATER` plays it only over water, on the water surface, so a tank shell that lands in a lake bends the water around it.
* W3DView carries the [W3DView+](https://github.com/triatomic/w3dviewerPlus) changes: dark mode, a Material Viewer with a Qt material panel, bone and sub-object overlays, a frame scrubber, a ground plane, MSAA and texture filter menus, folder loading and asset reload. Both viewports also have a view cube at the top right, and the `7` key shows the poly and vertex totals. The asset tree lists one node per object, with its meshes, skeleton, animations and textures inside, and a filter box on the toolbar that also searches mesh and bone names. The Material Viewer draws the light of a material with `Specular To Diffuse` ticked. W3DView also answers a local MCP server that reads a model's meshes, counts, W3D properties, passes, vertex materials, shaders, textures and texture paths, opens a model, lists its animations and jumps to a frame, searches and hides its meshes, and screenshots it from view cube sides; see [W3DView](contraZH-W3DView.md).
* The Options menu can be [tabbed](contraZH-Options.md#options-menu-tabs): a layout with `Tab<Name>` and `Page<Name>` windows shows one page at a time, with F-key and PageUp/PageDown switching.
* The [idle unit tracker](contraZH-Options.md) option (`QueueBar`) shows buttons at the top right for idle infantry, vehicles, aircraft and support units, each with a count of the units idle for 6 seconds or more; clicking one selects an idle unit, and Shift + click selects all of them.
* [Smart selection](contraZH-Options.md#smart-selection) shows the selection as a row of cameos above the command bar, and the [command groups](contraZH-Options.md#command-group-row) as a box of cameos at the top left of the screen.
* The `=` key gives each of the first ten selected objects its own [command group](contraZH-Options.md#command-group-row), 1 to 9 then 0.
* A network mismatch writes `MismatchInfo.txt` to the user data folder (the previous one is kept as `MismatchInfoPrev.txt`): the frame, every player's CRC, separate CRCs for objects, the partition manager, the player list and the AI, and the first 25 objects. Comparing two players' files shows which part of the simulation went apart.
* When more than 128 audio requests are waiting, the oldest unplayed sound effects are dropped down to 64. Stops, pauses, music and speech are never dropped.
* On laptops with two GPUs, the game, WorldBuilder and GUIEdit ask the NVIDIA or AMD driver to run them on the dedicated GPU.
* Synced with TheSuperHackers/GeneralsGameCode as of August 2026 and with GeneralsGameCode_Modding
as of October 2026. The only casualty was the texture filter option, replaced by TheSuperHackers'
version - see [Rendering](contraZH-Options.md#rendering) for the renamed `AnisotropyLevel` key.
* Upstream's WorldBuilder MCP server is not included; contraZH has its own, see
[MCP Server](WorldBuilder-Qt-MCP.md).

## Pages

* [Gameplay Fixes](contraZH-Gameplay-Fixes.md) - always-on fixes for retail simulation bugs
* [Pathfinding Improvements](contraZH-Pathfinding-Improvements.md) - search limits, queue retries, shoreline clearance, group movement and bridges
* [Maps & Game Setup](contraZH-Maps-and-Setup.md) - map features, 30-player maps, CLP_AI scripts, factions, game setup and the Diplomacy, Free Radar and Reveal Map rules
* [Campaign](contraZH-Campaign.md) - the campaign screen, campaign tabs, mission unlocks, mission save names and the challenge screen
* [Options.ini](contraZH-Options.md) - every Options.ini setting: camera, display, hotkeys, selection, input, drag-to-face formations and rendering, including the Path tracing emissive lighting option, plus the Hotkeys screen and Hotkeys.ini
* [Commandline Options](contraZH-Commandline-Options.md) - all game launch options, arguments and build requirements
* [Objects & Modules](contraZH-Objects-and-Modules.md) - new object keys, module parameters, bone-attached weapon fire particles and behavior modules
* [GameData.ini](contraZH-GameData.md) - new GameData.ini keys and simulation rules
* [SpecialPower.ini](contraZH-SpecialPowers.md) - enum behavior reference, Contra superweapon enums, INI-defined power identities, EVA events and radius cursors, plus new SpecialPower.ini keys
* [Weapon.ini](contraZH-Weapons.md) - new Weapon.ini keys
* [Locomotors](contraZH-Locomotors.md) - terrain-following projectiles and slope collision limits
* [CommandButtons & Selection](contraZH-CommandButtons.md) - new command buttons and options, queue reorder, auto queue and structure multi-select
* [ParticleSystem.ini](contraZH-ParticleSystems.md) - new ParticleSystem.ini keys
* [ObjectCreationList.ini](contraZH-Object-Creation-List.md) - new ObjectCreationList.ini nuggets
* [ChatCommands.ini](contraZH-ChatCommands.md) - new chat command fields: health, ownership, EMP, crew kill, player control and typed spawns
* [Extend Blocks](contraZH-Extend.md) - inheriting Armor, Locomotor, Weapon, Object, OCL, FXList and ParticleSystem entries
* [Animation2D.ini](contraZH-Animation2D.md) - direct textures in Animation2D.ini
* [Debug and Cheat Features](contraZH-Debug-and-Cheats.md) - debug overlays, cheat hotkeys and the debug panel with its Cheats tab in cheat builds
* [Generals Online (experimental)](contraZH-Generals-Online.md) - the experimental Generals Online build
