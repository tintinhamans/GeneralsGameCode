# Options.ini

Read at startup. Most can also be changed in game under Options > `Game Options` and apply on
Accept. `NewRadar` and `BlipSize` need a restart.

The menu's drop-down lists share the Resolution list's look and show every entry without a scrollbar.
`build/fix_option_combos_wnd.py` restyles them in `OptionsMenu.wnd` and orders them so each open list
draws over the rows below it; rerun it after adding a drop-down.

## Drag to Face

Thomas9393 contributed the Drag to Face feature.

* `UseFacingDrag = Yes` is the default. Set it to `No`, or untick Drag to Face under Input in Options > Game Options, to turn it off.
* Hold both mouse buttons with units selected, or rebind **Drag to Face (hold)** in Options > Hotkeys > GENERAL. The second press sets the destination;
  the drag direction sets the facing. Release either button to give the order.
* A drag under 20 pixels gives a normal move. Dragging farther adds rows.
* Ctrl snaps the facing to 45 degree steps. Translucent unit previews show the formation.
* Rows leave enough room for the largest selected unit, including when units swap rows.
* `FormationOrder = ToughFront` puts tougher units in front. `Nearest` assigns slots by distance.
* `FormationWheels = DriveIn` makes wheeled units approach along the chosen facing.
  `TurnInPlace` makes them turn at their slots.
* `FormationSpeed = MarchTogether` uses the slowest ground unit's speed. `ArriveTogether`
  adjusts speeds to arrive together; `Own` keeps each unit's speed.
* Allied units can push a formation aside. Displaced units return after a short wait,
  unless they have moved too far, returned three times, or received another player order.
* Jets keep circling because their locomotors cannot stop in place.
* Multiplayer peers and replay viewers need an engine with this order. Saves with an active
  formation need this feature too. Formation styles travel with the order.

## Camera

* `UseCustomMaxCameraHeight = No` - (`Yes` lets `MaxCameraHeight` replace `GameData.ini`'s value, 670
in Contra. Also `Max Camera Height` in Options.)
* `MaxCameraHeight = 670` - (210 to 1000. Single player, skirmish and campaign. Multiplayer uses the
host's limit, or `GameData.ini`'s.)

## Display

* `RenderFPSLimit = 240` - (`30` | `60` | `120` | `240` | `0` for Unlimited. Also the FPS Limit
combo in the main menu's Graphics section. Accept saves the selection; Back discards it.
Without this key, the limit defaults to 240 FPS. VSync can impose a lower cap.
With VSync on and a limit at or above the monitor's refresh rate, VSync alone paces the frames.
The limiter holds the average rate exactly on the limit and sleeps on a high resolution timer
on Windows 10 1803 and later, which costs far less CPU than before.
The render rate cannot drop below the current simulation rate, so faster game speeds and
60 Hz online matches can raise a selected 30 FPS limit. Replay fast-forward bypasses the cap.)

* `HealthBarDisplayMode = Classic` - (`Classic` | `Damaged` | `Always`. `Damaged` shows bars only on
hurt objects, `Always` on everything.)
* `AlliedDecalMode = House` - (`Hidden` | `House` | `Army`. Shows allies' general power decals once
fired, tinted in player colour (`House`) or faction colour (`Army`). `Hidden` is retail. Enemy
decals stay hidden. Needs `ComboBoxAlliedDecals` and `AlliedDecalsLabel` in `OptionsMenu.wnd` for
the menu control.)
* `HouseColorMode = Default` - (`Default` | `Basic`. `Basic` draws your units blue, allies yellow
and enemies red, on the models and the radar. Neutrals and civilians keep their own color, a
disguised unit takes the color of the side it pretends to be, and an observer watching no one
sees the default colors. Only this client's view changes. `Ctrl + Shift + G` cycles it in game for the
session. Needs `ComboBoxHouseColors` and `HouseColorsLabel` in `OptionsMenu.wnd` for the menu
control.)
* `NumericalHealth = No` - (Yes prints hit points beside the health bar, wherever a bar shows.)
* `VeterancyLevel = No` - (Yes draws a beveled, antialiased gold veterancy icon that fills from bottom to top on the
selected unit's portrait, with outlined current XP / next-rank XP text below it. A single chevron,
double chevron and star keep their original proportions and show progress toward veteran, elite and heroic. The fill resets after a
promotion and stays full at the unit's rank cap. Mod ranks above heroic keep their existing art.
Non-trainable units show no XP text. Icons above units in the world stay unchanged. Also the
Veterancy Level checkbox below Attack Range Ring in Game Options. Applies on Accept.)
* `SelectionCircle = No` - (Yes draws a green ring under selected objects. The D3D9 build draws
a hexagon fitted to the object's collision shape with a shader instead, on cards with `ps_2_a`.)
* `DefensesRangeCircle = No` - (Yes shows an armed structure's attack range, upgrades included,
while placing it. A solid ring marks its widest anti-ground weapon and a paler dotted ring marks its
widest anti-air weapon. Wall pieces get their rings each. The D3D9 build draws the rings with the
same shader as `SelectionCircle`, on cards with `ps_2_a`. Other builds and cards draw one ring at
the wider of the two ranges.)
* `QueueBar = No` - (Yes shows up to four buttons at the top right in game: infantry, vehicles,
aircraft and support units. A button shows while the player has an idle unit of that kind. Clicking it
selects one idle unit and centres the camera on it, and clicking again steps to the next. `Shift` +
click selects every idle unit of that kind without moving the camera. Support units are those with
`IGNORES_SELECT_ALL`, such as radar vans, listening outposts and sentry drones; they show under
Support only. Dozers, workers, supply gatherers and units inside a transport or building are left out.
A unit is idle when its AI has no order and no target, so a parked jet with full ammo counts and one
that is reloading does not. Hidden for observers and while the control bar is hidden. Also the Idle
unit tracker checkbox on the Interface tab. Applies on Accept.)
* `ObjectDecals = Yes` - (No hides ground decals requested with `DisplayDecal`. Independent of shadow
settings. Applies on Accept, including to objects already on the map.)
* `SmartPips = No` - (Yes keeps ammo and passenger pips visible on own units whenever there is
something to show.)
* `BuildTimerDisplayMode = None` - (`None` | `Seconds` | `Auto`. Countdowns on build queue and special
power cameos. `Auto` switches to MM:SS past a minute. Hover tooltips always show build time,
including power penalties, regardless of this setting.)
* `NewRadar = No` - (Yes gives an RA3 style radar: outlined blips, larger structures, dark
waterline, flat ground in three tones (olive, tan for desert sand, grey for rock and pavement),
256x256 grid.)
* `BlipSize = Large` - (`Small` | `Large`. `Small` is 3/5 px unit/structure, `Large` 5/7. Only with
`NewRadar`.)
* `Resolution = <width> <height>` - (Missing: the primary desktop's current resolution, or 800 x 600 when
that cannot be read or is smaller. `-xres`/`-yres` and a `GameData.ini` `XResolution`/`YResolution` set it
when the key is missing. Also the Resolution dropdown, written on Accept.)
* `BorderlessWindow = No` - (Yes runs a frameless centred window at the selected resolution. Also
the Borderless checkbox beside Resolution, which disables the resolution dropdown while checked.
`-win` still gives a captioned window.)
Mouse input follows the rendered picture if Windows stretches the window after a resolution change.
* `Windowed = No` - (Yes runs a movable window with a title bar at the selected resolution, like
`-win`. Also the Windowed checkbox beside Borderless; checking one clears the other. `-win` still
forces it on. A window larger than the monitor's work area shrinks at start to fit, keeping its aspect
ratio. Needs `CheckWindowed` in `OptionsMenu.wnd` for the menu control.)

Notes:
* A blue bar under a building's health bar shows production progress; on supply gatherers it shows
cargo. Own objects, when selected or moused over, or always with `HealthBarDisplayMode = Always`.
* `Damaged` and `Always` bars and `SmartPips` hide while a script letterboxes the screen, as campaign
cinematics do.
* `NewRadar` cannot hide roads, since they are painted into the terrain textures. Bridges still
draw.
* `SelectionCircle` and `DefensesRangeCircle` need a mod-side `PlainRingSelection.tga` (white or
greyscale ring with alpha).
* `DisplayDecal` needs the `.tga` named by `DecalTexture`; there is no default.
* The `DefensesRangeCircle` checkbox needs `CheckDefensesRangeCircle` in `OptionsMenu.wnd`.
* The `VeterancyLevel` checkbox needs `CheckVeterancyLevel` in `OptionsMenu.wnd`.
* `QueueBar` needs mod-side windows in `ControlBar.wnd`: a second top-level window `QueueBarParent`
(hidden, `SYSTEMCALLBACK = "QueueBarSystem"`) holding the push buttons `ButtonQueueBarInfantry`,
`ButtonQueueBarVehicle`, `ButtonQueueBarAircraft` and `ButtonQueueBarSupport`. The layout sets their
place, images and tooltips. Its checkbox needs `CheckQueueBar` in `OptionsMenu.wnd`.

### Recommended detail level

The game picks the first-launch detail level and the Options menu's default from the graphics card
and RAM alone. It no longer detects or times the CPU.

* `IdealStaticGameLOD` stores the result. A value saved by an older build stays until the
`-forceBenchmark` launch option recomputes it.
* In `GameLODPresets.ini`, a `LODPreset` line still needs its CPU type and MHz fields, but they are
not checked. The highest level whose graphics card and memory requirements pass wins.
* `BenchProfile` lines and `ReallyLowMHz` are still read and have no effect. Low RAM still turns the
shell map off.
* Startup is about 0.4 s faster: the CPU speed test at launch takes about 20 ms instead of at least
400 ms.
* Before, the 2003-era CPU tables misread current CPUs. Intel CPUs landed on a preset by the low digit
of their model number, and every AMD CPU since the Athlon 64 was treated as a 1 GHz Pentium III,
which usually meant `Low`.

## Hotkey overlay

Draws each cameo's hotkey letter on the cameo.

* `KeyboardOverlay = Yes` - (No hides the letters.)
* `KeyboardOverlayRed = 255` - (Letter colour, 0-255 per channel.)
* `KeyboardOverlayGreen = 255`
* `KeyboardOverlayBlue = 255`
* `KeyboardOverlayBackdrop = Yes` - (Draw a translucent plate behind the letter.)
* `KeyboardOverlayBackdropRed = 0` - (Backdrop colour, 0-255 per channel.)
* `KeyboardOverlayBackdropGreen = 0`
* `KeyboardOverlayBackdropBlue = 0`
* `KeyboardOverlayBackdropOpacity = 128` - (0-255; 128 is 50%.)

Shows the hotkey actually registered, so a dropped duplicate is not advertised.

## Grid hotkeys

Keys each cameo by **its slot in the command bar** instead of its ampersand letter, so keys stay put.

* `GridHotkeys = No` - (Yes keys cameos by slot position.)
* `GridHotkeyLayout = QWERTYUIOASDFGHJKL` - (The options screen offers `QWERTYUIOASDFGHJKL`
(QWER / ASDF), `ASDFGHJKLZXCVBNM,.` (ASDF / ZXCV) and `QWERTYUIOZXCVBNM,.` (QWER / ZXCV).)
* `GridHotkeyUnitOrders = Yes` - (Unit orders keep their own letters.)

The layout splits evenly over the command bar's rows, top row first. Rows and columns come from where
the bar's buttons sit, so any bar shape works; columns past a row's share of the layout keep their
string file letter.

With `GridHotkeyUnitOrders` on, Stop, Guard, Attack Move and Evacuate keep their string file letter. A
grid slot on the same key swaps with them and takes the unit order's grid key. Production buildings
have no such buttons, so the whole grid works unchanged. Off, unit orders take their slot's grid key
like any other button.

## Options menu tabs

A layout that pairs radio buttons named `Tab<Name>` with windows named `Page<Name>` gets a tabbed
Options menu. Any other `OptionsMenu.wnd` keeps the single page menu with its Game Options and
Shaders panels. The tab logic is adapted from the tabbed menu by simonx645.

* The tabs run left to right in screen order, and the last tab used reopens for the rest of the session.
* **F1** and up pick a tab. **PageUp**, **PageDown**, **Ctrl+Tab**, **Q** and **E** step through them.
* **Tab** and **Shift+Tab** move the focus through the shown page's controls, then Defaults, Accept and Cancel.
* Optional `TabIcon<Name>` and `TabLabel<Name>` windows are drawn by their tab. The icon keeps its
  shape at any aspect ratio and shows the mapped image `<its image>On` on the current tab. The label
  reads `GUI:OptionsTab<Name>`, or the tab's name when the string is missing, and takes its hilite
  colour on the current tab.
* A tab without the `IMAGE` status is drawn as a flat box, so it keeps its shape at any resolution.
  Its fill and border come from its draw data: slot 0 of `ENABLEDDRAWDATA` at rest, slot 0 of
  `HILITEDRAWDATA` under the mouse and slot 1 of `HILITEDRAWDATA` on the current tab. A tab with
  `IMAGE` keeps its radio button art.
* The custom detail toggles sit on a page of their own, so changing one sets **Detail** to Custom.
* **VSync** is saved at any detail level. It used to be saved only with Detail on Custom.
* Controls are still found by name, so they can sit on any page.
* A `TabHotkeys` button needs no page. It opens the [Hotkeys screen](#hotkeys-screen) over the menu,
  and the page that was showing stays. The stepping keys skip it.

## Hotkeys screen

**Options > Hotkeys** rebinds keys and writes them to `Hotkeys.ini` in the user data folder, beside
`Options.ini`. The tabbed Options menu opens it from its Hotkeys tab. Ported from Zero Hour Reforged.

* Left: **USA**, **CHINA**, **GLA**, then **GENERAL** and **KEY CONFLICTS** (red while any key is shared).
* Middle: a general picker, then the general's heroes, buildings and units as icons.
* A general that picks a path at the start is listed once per path, e.g. Superweapon General:
  Classic and Futuristic tech, Cyborg General: Spiders and Angels. Each entry shows the cards that
  path's upgrade swaps in and leaves out the units only the other path builds. A path is a player
  upgrade on a card whose rival upgrades swap the command sets of the same buildings.
* Right: the picked object's portrait, name and command card, showing slots 1â€“18 in two rows of nine,
  each button showing its letter. **Quick filter** narrows the icons or commands by name.
* Click a card button, then press its new letter. Backspace takes the key off, Esc cancels.
* Mouse inputs bind like keys, to commands and card buttons alike: the middle button, the two side
  buttons and the wheel up or down. Card corners show `M3`, `M4`, `M5`, `WU` or `WD`.
  * The middle button fires on a quick click that did not drag, so middle-drag camera rotation still
    works. A click that fires a binding does not reset the camera.
  * A wheel direction that fires a binding does not zoom; every unbound combination still zooms, so
    Ctrl + Wheel up can take a command while the plain wheel keeps zooming.
  * A binding fires once per whole wheel notch, so a touchpad swipe does not repeat it.
  * A wheel notch over a menu or list still scrolls it and fires nothing.
* **GENERAL** lists every command that needs no unit (selection, control groups, camera, chat,
  beacons, holds such as Force Attack). **Change**, a double click or Enter waits for a key; **No key** unbinds.
  * Text tabs over the list show one group at a time: **All**, Selection, Groups, Orders, Info,
    Interface, Taunts, Other, and GO in the Generals Online build.
  * Typing in **Quick filter** returns to **All**, so the search covers every group.
  * A tab's text is red while a command under it shares a key, and yellow while one differs from
    the game's key.
* **Drag to Face (hold)** in **GENERAL** changes LMB + RMB to a key or side mouse button,
  with optional modifiers, or a modifier alone. Release the hold to give the order, even while Ctrl is held for snapping.
  **RESET TO DEFAULT** restores the mod's default (LMB + RMB unless overridden); **No key** disables the binding.
  The Drag to Face option must still be enabled.
  Mods can supply defaults through `CommandMap BEGIN_FACING_DRAG` and `CommandMap END_FACING_DRAG`.
* A hold command (Force Attack, Waypoints, Prefer Selection) also takes Ctrl, Alt or Shift alone:
  press it and let go with no other key. It does not take the middle button or the wheel.
* **KEY CONFLICTS** lists every card with a shared letter and every shared command chord; a double
  click opens it.
* Card borders: gold for the selected button, red for a shared letter, amber for a changed one.
* **RESET TO DEFAULT** restores the selected object's card or the selected command. **RESET ALL**
  restores everything.
* **SAVE** writes the file and applies it on the first save, also from Esc > Options in a match. **BACK**
  leaves without saving.

A button's letter applies on every bar that has the button. With grid hotkeys on, most slots still
take their grid key; unit orders use the letter set here while `GridHotkeyUnitOrders` is on. Tooltips mark
the new letter.

`Hotkeys.ini` holds only what differs from the game:

```ini
CommandMap SELECT_ALL
  Key = KEY_Q
  Transition = DOWN
  Modifiers = CTRL
  UseableIn = GAME
End

CommandButtonHotKey Command_ConstructAmericaBarracks
  Key = K
End
```

* `CommandMap` blocks use `CommandMap.ini` syntax, plus `KEY_MOUSE3`, `KEY_MOUSE4`, `KEY_MOUSE5`,
  `KEY_WHEELUP` and `KEY_WHEELDOWN`. `Key = KEY_NONE` with `UseableIn = NONE` unbinds.
* `CommandButtonHotKey <button>` takes one letter, a mouse key (`MOUSE3`, `MOUSE4`, `MOUSE5`, `WHEELUP`,
  `WHEELDOWN`) or `NONE` for no key. Digits are left out because they are control group keys.
* Not part of the multiplayer checksum. A block that fails to parse drops the keys from there on.
* Needs no files: the exe builds the screen in the look of the options menu beneath it, adds the
  Hotkeys button right of Shaders when `OptionsMenu.wnd` has no `ButtonHotkeys`, and shows built-in
  English for any `GUI:Hotkeys*` label the string file lacks.

### Limitations

Notes:

* **Grid keys win over bare-letter commands.** With grid hotkeys on, a command bound to a bare grid
  letter does nothing while a visible cameo sits on that letter. Example: Next Idle Worker on plain
  `I` (in `QWERTYUIOASDFGHJKL`) fails whenever a worker or building is selected. Use a modifier
  (Ctrl + I) or a key outside the grid layout.
* **A bare-letter command and a cameo on the same letter both fire.** Without grid hotkeys, the
  command fires on key down and the cameo on key up. KEY CONFLICTS does not report this clash; it
  only checks cards against themselves and commands against each other.
* **Cameo keys ignore Ctrl and Alt.** A card button fires on its letter alone or with Shift
  (Shift batches production), so a card cannot take Ctrl + letter.
* **Only path upgrades show their swapped cards.** A general's starting path choice (Superweapon's
  tech, Cyborg's Spiders or Angels) gets its own entry with the cards it swaps in. Every other
  command set swap, from an Overlord add-on, a mines or promotion upgrade, or a state change, is not
  listed: cards show each object's own `CommandSet`. Letters still carry over to a swapped set for
  every button it shares, but its new buttons cannot be set here; add them to `Hotkeys.ini` by hand.
* **Path detection follows the INI.** A path is a player upgrade on a card whose rival upgrades swap
  the command sets of the same buildings. A mod that builds a choice another way, such as through a
  science or an object upgrade, is listed as one general.
* **Mouse binds have no hold on the middle button or the wheel.** Both fire once per click or notch;
  only the side buttons can hold a command such as Force Attack.
* **Hand edits during a session are overwritten.** SAVE writes the screen's keys, which were read
  when the game started; edit `Hotkeys.ini` by hand only while the game is closed.

## Reverse move hotkey

Caps Lock arms a reverse move for the whole selection; the next terrain click drives there in
reverse, like the `REVERSE_MOVE` button. Pressing Caps Lock again or arming attack move cancels it.

* `CastMode` applies to it like a command hotkey. `QuickCast` reverses to the cursor on the press.
`QuickCastWithIndicator` shows the reverse move cursor while held and reverses to the cursor on
release; releasing over the UI cancels it. A `QuickCast` press over the UI arms it for the next
click instead.
* `TOGGLE_REVERSEMOVE` in CommandMap.ini rebinds it. The Caps Lock default only fills an empty slot.
* `KEY_CAPS` is a new CommandMap key name. The Caps Lock state still toggles.
* The cursor uses a `MouseCursor ReverseMove` block from Mouse.ini if present (e.g. `Image = SCCMove`,
`Texture = SCCMove`), else the move cursor.

Needs a locomotor with `CanMoveBackwards = Yes`. Each selected unit backs up while turning its
rear toward the path, even when units start facing different directions. Tanks keep at least a
quarter of their requested reverse speed during sharp turns, subject to destination braking.
Manual reverse orders ignore `BackwardsMoveAngleThreshold` and `ReverseMoveIgnoreAngleThreshold`.
During manual reverse orders, units check collisions in their direction of travel and yield when blocked.
Packed selections, formations, and routes around obstacles keep their reverse order.
Automatic reversing keeps its existing movement and collision behavior.

## Smart selection

A row of small cameos above the command bar: one per unit type with a count, or one per object for
a single-type selection. Single-object cameos show health, ammo and veterancy. Ammo clips with up
to six shots show segments; larger clips show a continuous fill bar. Up to 16 cameos; counts past
999 show no badge. Reskins share a cameo.

* `SmartSelection = Yes` - (No hides the row and unbinds its keys.)
* `SmartSelectionUseMouse = Yes` - (Yes keeps only a cameo's units on double click, No on
Ctrl+Shift+click.)

* Left click: show that type's (or object's) command set. The whole group stays selected; map
orders go to everyone, bar commands go to the focused object or type. Plain orders (move, attack
move, guard, stop, scatter, cheer, formation, waypoints, enter, dock, repair, heal) always go to
everyone. A building placed from a focused dozer is built by it, with the other dozers helping.
* Right click the pushed-in cameo: back to the group's common commands. Right click another cameo:
drop it from the selection.
* Double click (or Ctrl+Shift+click with `SmartSelectionUseMouse = No`): keep only that cameo's
units.
* Tab and Shift+Tab (`SMART_SELECTION_NEXT_TYPE` / `SMART_SELECTION_PREV_TYPE` in CommandMap.ini)
step the focus through the row.

### Command group row

A row on the command bar frame with one cameo per non-empty command group (1-9, then 0). Each shows
the group's most common unit, its number and live member count. Shown whenever any group has
members.

* `SmartCommandGroup = Yes` - (No hides the row. `SmartSelection = No` hides it too.)

* Left click selects the group; double click also centres the camera on it.
* Numpad `*` (`AUTO_CREATE_TEAMS` in CommandMap.ini) gives each of the first ten selected objects a
group of its own, in selection order: 1 to 9, then 0. A group that takes a new object loses its old
members, and objects past the tenth are left alone. In cheat builds the fast forward toggle moves from
numpad `*` to Ctrl + numpad `*`.

## Action lines

A faint line from each selected unit to where its order sends it, fading in from the unit and
ending in a small dot. Units moving together share one line. Lines last as long as the order and follow a
chased target.

* `ActionLines = Off` - (`Off` | `All` | `AttackOnly`. Game Options > Action lines. `AttackOnly` draws
attack and attack move orders only.)

* Green: move, enter, dock, repair. Pink: attack move. Red: attack. Blue: guard.
* No line draws to a target in fog or shroud.
* A waypoint path draws one line per leg. With `All` it replaces the white path shown while Alt is
held.
* Credit to olcayseygan, author of Generals Reforged.

## Input

* `CastMode = Normal` - (`Normal` | `QuickCast` | `QuickCastWithIndicator`. `QuickCast` fires a
targeted ability at the cursor on the hotkey press. `QuickCastWithIndicator` aims with a decal while
held and fires on release.)

Notes:
* Superweapons, structure placement, rally points and beacons are excluded from quick cast.
* A cast pressed during cooldown is queued and fires when the ability is ready, never earlier.
* Shift+click queues five units. Shift+click a queue entry cancels all of that type; Shift+click a
passenger cameo unloads all of that type.

## Clipboard paste

`Ctrl + V` pastes into any text field: in game chat (Enter, Shift+Enter for allies), lobby chat,
names, and so on.

Notes:
* Pastes obey the field's filters; hidden fields still show asterisks.
* Text is truncated to fit the field.
* A multi-line paste stops at the first line break.

## Command line

* `-loadreplay <file>` starts straight into a replay with menus and Options loaded. `<file>` is a
name in the Replays folder or an absolute path. Unreadable replays or missing maps show a message
box.
* `-loadsave <file>` also accepts an absolute path, and shows a message box on failure.
* `-live` checks the loose file before the `.big` on every read. Without it, a file
first found only in a `.big` keeps loading from there, so a loose override dropped into the game
folder while the game runs needs a restart. Loading opens fewer files without the flag.

Window layouts accept `DISABLED` in `STATUS` fields to clear the enabled flag.

## Rendering

TheSuperHackers' texture filter option replaced this fork's version in the August 2026 merge:

* `TextureFilter = Anisotropic` - (`None` | `Point` | `Bilinear` | `Trilinear` | `Anisotropic`. Also in
the in-game options menu.)
* `AnisotropyLevel = 8` - (`2` | `4` | `8` | `16`. Only with `TextureFilter = Anisotropic`.
**Renamed from the pre-merge `AnisotropicLevel`** â€” update Options.ini by hand; the old key is
silently ignored.)

Notes:
* An unknown `TextureFilter` value falls back to `None`, so typos are visible. `AnisotropyLevel`
rounds down to a valid step.
* The level is clamped to what the device supports.
* Fork fixes on top: settings survive a device reset (alt-tab, window toggle), and a driver that
supports anisotropic for only min or mag filtering falls back to linear for the other.
* The terrain follows `Anisotropic` too, blend edges and the lower detail settings included, so distant
ground stays sharp. It used to keep the mod's filtering. In the other modes the terrain still filters
as the mod's `BilinearTerrainTex` and `TrilinearTerrainTex` say.
* Roads follow `Anisotropic` as well. Their mips blend smoothly even with `TrilinearTerrainTex = No`,
and their bump maps stay sharp in the distance.

Shadow mapping, specular, terrain glint, HQ sky cloud shadows, stochastic terrain, normal and glow maps, per-pixel lights, soft particles, flame
and laser shading, per-map colour grading, planar mirrors and the other features that need the Direct3D 9 build are on [Direct3D 9 Features](dx9feat.md).

The Direct3D 9 build retries its embedded shader when a loose or archived override is rejected.
This lets the original DX8 tree shader coexist with the DX9 renderer.

HQ sky cloud shadows preserve the contrast of object shadows as clouds pass over them.

With `DrawEntireTerrain = No` in `GameData.ini`, the terrain draw window grows to cover the
visible ground at high camera zoom, including scripted cameras. This prevents the map's water
plane from showing through gaps beyond the regular window. The window grows in whole buffer
blocks and stays stable during camera rotation. Views across the horizon use the full map.
`DrawEntireTerrain = Yes` keeps drawing the full map. This rendering fix comes from
[TheSuperHackers PR #2785](https://github.com/TheSuperHackers/GeneralsGameCode/pull/2785).

### Bloom

Soft glow around `Add` blended object meshes, such as building lights, and object textures with
`_emi` glow masks. Additive particles also feed bloom unless their particle system sets `Bloom = No`.
Terrain textures also support `_emi` masks and a per-terrain `UnitEmissiveIntensity` key; see
[Glow masks](dx9-Lighting.md#glow-masks). Terrain masks glow without feeding bloom.
Ordinary opaque surfaces, smoke and alpha blended effects do not feed bloom. Needs no shader support.
The glow is screen blended, so pixels that are already bright take little of it and keep their detail.

* `Bloom = Yes` - (No turns the glow off. Also `Bloom` on the Graphics page,
applied on Accept.)
* `BloomStrength = 0.5` - (0 to 1. 0 is off. The box beside `Bloom` on the Graphics page.)
* `BloomThreshold = 0.25` - (0 to 1. Only source brightness above this value feeds the glow, fading
in over a soft knee below it. Uses the brightest RGB channel and preserves its color. `Threshold %` on the Graphics page,
from 0 to 100. Requires the Direct3D 9 pixel shader.)
* `BloomSize = 1.0` - (0.25 to 4. Multiplies the glow radius. 1 keeps the original size.
`Size %` on the Graphics page, from 25 to 400.)
* `BloomDebug = No` - (Yes shows only the blurred glow buffer on black, to see what feeds it.
`Bloom debug view` on the Graphics page. Cheat builds only: a release build hides the checkbox and
ignores the key.)

Menu controls need `CheckBloom`, `TextEntryBloomStrength` and `CheckBloomDebug` in `OptionsMenu.wnd`;
`build/move_bloom_to_shaders_wnd.py` moves them onto the Graphics page and `build/bloom_row_wnd.py`
puts the strength box beside the checkbox.
`python scripts/add_bloom_controls.py <game-folder>` adds the threshold and size rows and their
labels to the loose menu and string files, keeping copies under the game folder's sibling
`_bak_to_delete` directory.
Cheat builds also let you adjust threshold and size live in the
[`Ctrl + Shift + Tab` panel](contraZH-Debug-and-Cheats.md#debug-panel), without saving them.
The FX tuner's `Bloom` checkbox updates its previews and writes the global `Bloom` setting to
`Options.ini` immediately. Choose that file in the tuner's Configuration page. The game reads the
setting when options reload or at launch; the preview uses its strength, threshold and size.

Notes:
* Replays the glowing object meshes, then blurs the glow at half resolution and adds it to the scene.
* Off while `AntiAliasing` is above 1 on the Direct3D 8 build. The Direct3D 9 build glows with MSAA
on too, and blurs through a pixel shader where the card has one.
* Particle systems can opt out with [`Bloom = No`](contraZH-ParticleSystems.md#bloom).
Their normal drawing and any glow painted into their textures stay unchanged.
* Additive meshes on skinned models (infantry and other bone-deformed meshes) do not glow.
* Retail night models keep their lights in the opaque pass and add the lit skin on top. The game
swaps the two passes at load, so the lights glow and the walls do not.

### Vertical sync

* `VSync = Yes` - (Waits for the monitor's refresh before showing each frame, which stops tearing.
No shows frames as soon as they are drawn. Without the key, vsync is on in fullscreen and off in a
window, as before. Also `Vertical sync` in the advanced display options, applied on Accept. Needs
`CheckVSync` in `OptionsMenu.wnd` for the menu control.)

### Anti-aliasing

* `AntiAliasing = 0` - (Multisample anti-aliasing: 0 off, 2, 4 or 8 samples. Also `Anti-aliasing` under
Detail in the options menu, applied on Accept without a restart; a sample count the card lacks falls
back to off. Needs `AntiAliasingLabel` in `OptionsMenu.wnd` and `ComboBoxAntiAliasing` not hidden
for the menu control. Ambient occlusion is off while it is on.)

### Low latency mode

* `LowLatency = No` - (Yes lets the game prepare at most one frame ahead of the graphics card, instead
of the driver's usual three, so the screen answers the mouse sooner. Can cost a little frame rate. Also
`Low latency mode` in the advanced display options, applied on Accept. Needs `CheckLowLatency` in
`OptionsMenu.wnd` for the menu control. Direct3D 9 build only.)

### Path tracing

`PathTracing = Off` is the default. `Quality` and `Fast` let additive particles, streaks,
laser beams, additive lamp meshes and model `_emi.dds` glow masks light nearby surfaces.
The `yes` and `no` of older files read as `Quality` and `Off`.

| Value | Look | Cost |
|---|---|---|
| `Quality` | Each surface is lit with its own textures, normals and normal maps. | Extra passes per surface, so a lit base at night can add thousands of draw calls. |
| `Fast` | Softer and blockier on models, because surface facing is rebuilt from the depth buffer. | One screen-space draw for all of these lights. |

The `Path tracing` drop-down on the Graphics page applies on Accept. Back discards changes
and Defaults turns it off. A layout supplies the drop-down as `ComboBoxPathTracing` with a
`PathTracingLabel`. In a layout without it the menu creates a checkbox when there is a
Graphics panel and an ambient occlusion checkbox, and that checkbox switches between `Off`
and the mode last saved, `Quality` at first. Layouts can supply `CheckPathTracing` to place it.

Requires D3D9, shader model 2.0a, `DynamicLights = Yes` and `PixelLights = Yes`.
The control stays grey while either lighting setting is off. Bloom is independent.
Glow masks follow the existing specular pass and the day/night emissive
intensity settings; `Specular = No` disables their light.

A mesh lights its surroundings through its material only when it is additive. A mesh that is
emissive without being additive, the usual way to keep thin parts such as barbed wire bright in
shadow, emits no light. Give it an `_emi.dds` mask to make it a lamp, or tick `Specular To Diffuse`
on its vertex material in the W3D exporter. That checkbox has no other effect in the game, and a
ticked material lights its surroundings with its emissive colour times its texture, by the
[lamp formula](#lamp-formula) below. W3DView's
Material Viewer has the same checkbox on the Vertex Material tab and draws the lamp's light on
the model while it is ticked.

#### Lamp formula

A lamp mesh is sampled four times per triangle. Each sample's colour is

```
colour = emission * texture * Opacity + mask * UnitEmissiveIntensity
```

| Term | Value |
|---|---|
| `emission` | `Emissive` when `Specular To Diffuse` is ticked. `Emissive + Diffuse` on an additive mesh (destination blend `One`, source blend `One` or `Src Alpha`), ticked or not. Zero on any other mesh. |
| `texture` | The base texture's pixel at the sample, read from a 32x32 copy. It is multiplied by the pixel's alpha when the shader's source blend is `Src Alpha`. A mesh without a texture counts as white. |
| `Opacity` | The vertex material's opacity. |
| `mask` | The `_emi.dds` pixel at the sample, or zero without a mask. At night `UnitEmissiveNightIntensity` takes the place of `UnitEmissiveIntensity`, and `Specular = No` makes both zero. |

Samples within one 24-unit cell merge into one point light, so a larger or brighter lamp
gives a stronger light, up to a limit.

```
weight = sum(area * brightness)
light  = sum(colour * area) / weight * min(sqrt(weight) / 8, 1.5) * PathTracingLightBrightness
radius = clamp(3 * sqrt(area), 24, 120), then capped at PathTracingLightMaxRadius
```

* `area` is a quarter of the triangle's area.
* `brightness` is the largest of the sample colour's red, green and blue.
* `radius` uses the largest sample in the cell.

A surface point at `distance` from the light receives

```
ramp = clamp((radius - distance) / (radius - 0.5), 0, 1)
lit  = light * ramp * ramp * (3 - 2 * ramp) * (max(facing, 0) + 0.1)
```

where `facing` is the cosine between the surface normal and the direction to the light. The
surface shows `lit` times its own texture colour.

`PathTracingLightMaxRadius` in `GameData.ini` caps large lights without shrinking
smaller ones. It defaults to `25`; `0` means no cap. `PathTracingLightBrightness`
scales brightness independently and defaults to `0.75`; zero disables the new light
spill. See [Light radius and brightness](contraZH-GameData.md#path-tracing-light-radius-and-brightness).

In `Fast`, where the scene's depth can be read and the card instances draws, these lights
are added in one screen-space draw after the opaque scene, so their count no longer adds
draw calls. That draw takes a surface's normal from the depth buffer, without normal maps,
and multiplies the colour already on screen, so it leaves sprites and transparent meshes
drawn later unlit. The batches described below draw everything in `Quality`. In `Fast` they
remain for game lights, laser spill on models, and cards or settings the screen-space draw
cannot run on, such as multisampling.
Setting the environment variable `CONTRA_DEFERREDLIGHTS=0` sends every light back to them.

This option approximates emissive lighting with world-space point lights. It does not
cast local shadows or calculate reflected light.
Particle texture colours are averaged once per cached texture.

A model's own opaque meshes block the light its lamps throw on the ground. Each group of
lamps that sit together and face one way casts 24 rays to the ground around it. A lamp the
hull hides, such as one on a turret roof, lights no ground. A half-hidden one, such as a lamp
on a wall, lights a smaller pool on its open side. The lamp still lights models as before,
including its own, and one model's meshes never block another model's lamps. Meshes that blend, skinned meshes and particles block
nothing. `Fast` tells ground from models by height, so on a slope the split is rough.
Nearby emitters share lights within 24 world units. All visible lights are kept, and
surfaces draw additional batches of eight lights, or two under standing water.
Extra passes add only light and do not repeat sunlight, glow masks or shadow darkening.
On detailed terrain, roads and bridges, local lights retain surface textures, normals and
ground noise but ignore sun shadows and clouds. This also applies to three-texture terrain
blends and extra light batches. First and extra batches use the same brightness range to
avoid seams between terrain tiles. Opaque surfaces receive the full light in every batch;
roads and other transparent surfaces keep their alpha coverage.
Dense scenes cost more draw calls; there is no fixed light count shared by the whole view.
Extra batches leave out lights that cannot reach the terrain tile or rigid mesh being drawn.
Single-instance draws use that mesh's world bounds too. Skinned meshes and groups
with several instances keep their conservative light lists.
Extra batches also skip pixels outside conservative screen bounds of their lights,
and batches wholly outside the view can be skipped. This keeps full resolution,
all light contributions and the existing falloff.
Laser spill on models uses up to
16 samples per beam. `Lasers light the ground` keeps the existing shaped terrain glow;
with that setting off, the new point lights also light the ground. Roads exclude the
object-only samples while the shaped ground glow is enabled, avoiding duplicate laser lighting.

Model masks are sampled at 32 by 32 resolution on rigid meshes. Hidden and shrouded
models emit no light. Skinned emitters, animated texture coordinates, terrain
glow masks, alpha-blended smoke and multiplied effects do not supply lights. Mask and
material samples are cached until the map closes. Source-alpha materials scale emitted
light by texture alpha, so transparent texels emit no light. These changes affect drawing only.

### Fluid particle shading

`FluidShaders = Yes` enables fluid shading on particle systems that opt in with
`FluidShader = Yes`. `No` restores their original sprites. The setting requires D3D9
and shader model 2.0a. See [Fluid shading](contraZH-ParticleSystems.md#fluid-shading)
for the tuning keys and acid, toxin and radiation examples.

### Smooth unit motion

The game logic runs 30 times a second. Above that frame rate, units, projectiles and turrets draw
between their last two logic positions instead of jumping once per logic frame, so they move smoothly
at 60, 144 Hz and up. Health bars, status icons, attached effects and the camera lock follow the drawn
model. Only drawing changes; the simulation does not, so it cannot desync.

* `SmoothUnitMotion = Yes` - (No draws units at their logic positions, as before. Also `Smooth unit
motion` in the advanced display options, applied on Accept. Needs `CheckSmoothUnitMotion` in
`OptionsMenu.wnd` for the menu control.)

Skirmish, campaign and replays run the logic at the game speed and render up to `FramesPerSecondLimit`
from `GameData.ini`. The skirmish game speed slider sets the logic rate, 30 being normal speed; the
campaign starts at 30. Mission scripts that set an FPS limit change the logic rate the same way, and
the main menu map always runs at 30. Setting the slider to `--` leaves the game uncapped, with the
logic stepping once per drawn frame, so there is nothing to smooth.

Notes:
* Units are drawn up to one logic frame (33 ms) behind the simulation.
* Single player, skirmish and replays only. Online and LAN games draw as before.
* Teleports, transport and tunnel exits, garrison exits and newly spawned objects snap into place.
* The environment variable `CONTRA_INTERPOLATION=0` turns it off regardless of the option.

### Model animations between keyframes

Model animations advance every drawn frame and blend between keyframes, independent of `Smooth unit
motion`. A bone that holds still and then jumps, such as a runway light stepping from bulb to bulb,
jumps instead of sliding along the runway.

Notes:
* A step counts as a jump when it is at least 1 unit long and more than 4 times the steps before and
after it, so loop restarts and bones hidden by moving them away jump too. Two such steps in a row
count as one jump split over two frames.
* Applies to uncompressed animations; compressed ones mark their jumps in the file.
* Rotation still blends.

### Laser ground glow

Each laser beam lights the terrain along its length with up to twelve dynamic lights in the beam's
colour (house coloured when the laser asks). Terrain only; units, buildings and roads are not lit.
The Direct3D 9 build lights it with a shader instead, see
[Laser ground glow](dx9-Effect-Shaders.md#laser-ground-glow).

* `LaserRef = Yes` - (No turns the glow off. Also `Lasers light the ground` on the Graphics page, applied
on Accept. Needs `CheckLaserRef` in `OptionsMenu.wnd` for the menu control.)

Colour and intensity come from `GameData.ini`, overridable per `W3DLaserDraw` module with
`GroundGlowColor` and `GroundGlowIntensity`. A non-zero module value wins, then `GameData.ini`, then
the default:

* `LaserGroundGlowColor = R:0 G:0 B:0` - (Light colour. Black derives it from the beam layers'
colours weighted by width, times the laser texture's average colour, normalised to full brightness.)
* `LaserGroundGlowIntensity = 70%` - (How strongly the colour is added to the ground.)
* `LaserGroundGlowRadius = 0` - (How far the light reaches from the beam, in world units. 0 takes it
from the beam, as below. Also `GroundGlowRadius` on the module.)

Notes:
* Each light's reach is `GroundGlowRadius` on the module, else `LaserGroundGlowRadius`, else the
laser's `OuterBeamWidth`, at least 15, plus a 6 unit soft edge. Long beams spread and dim their twelve lights.
* The terrain takes up to 64 dynamic lights a frame, of all kinds.
* In the Direct3D 9 build, beams whose glows overlap light the ground together, so a spot several
lasers hit brightens modestly instead of compounding. `LaserGroundGlowOverlap = No` in
`GameData.ini` lights each beam on its own. See [Laser ground glow](dx9-Effect-Shaders.md#laser-ground-glow).

## Drag Selection

### EasyMilitaryDrag

* `EasyMilitaryDrag = No` - (Yes leaves builders out of a drag selection, so boxing over a base picks
up the army without dragging workers along.)

Builders are `KINDOF_DOZER` and `KINDOF_IGNORES_SELECT_ALL`, as for Select All.

`KindOf = NOT_MILITARY` marks any other unit as a builder for this option only. Select All still
picks it up.

Notes:
* Ctrl while dragging selects **only** builders.
* A drag that would select only structures ignores the filter.
