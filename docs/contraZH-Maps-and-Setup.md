# Maps & Game Setup

## Maps

### More terrain texture tiles

* Terrain `Texture` entries accept TGA or legacy DXT1/DXT5 DDS files in `Art/Terrain`, in both the
game and WorldBuilder. DDS images use the same tile layout and atlas space as TGA images.
* A missing terrain TGA falls back to the matching DDS, so existing `Texture = name.tga` entries
also work when the base texture is supplied only as `name.dds`. WorldBuilder uses the same
fallback for its texture palette, previews and brush validation.
* A terrain texture missing from `Art/Terrain` is looked up in `Art/Textures`, as DDS and then TGA, in
the game and WorldBuilder. A mod can pack it there beside its `_nrm`, `_hgt` and `_emi` maps. A file
there that terrain cannot read counts as missing.
* A terrain texture missing from both folders draws `misstex.dds` from either folder instead, in the
game and on maps opened in WorldBuilder. Any multiple of 64 pixels works, because its tiles repeat to
cover a larger texture. The WorldBuilder palette and brush validation still skip missing textures.
* A terrain image with fewer tiles than a map saved repeats them to fill the texture on that map.
Its `_nrm`, `_hgt` and `_emi` maps do not repeat, so on that map it has no normals or glow and its
height comes from its color.
* Individual terrain images still fit within 640 by 640 pixels. DDS cube maps, volume textures,
DXT2-DXT4, uncompressed DDS and newer DX10/BC7 formats are unsupported.
* D3D9 builds use a 4096-pixel terrain atlas instead of 2048, increasing capacity from 784 to
3,136 tile slots before gaps from packing texture classes. Source-tile storage rises from 1,024 to 4,096.
* WorldBuilder reports texture usage against the atlas slot count. High-resolution textures use
more slots, and a full atlas uses about four times the texture memory of the previous limit.
* Requires support for 4096-pixel textures. Use matching D3D9 game and WorldBuilder builds for maps
that exceed the old capacity. D3D8 builds keep the old limits.
* Saved cliff texture coordinates keep their existing scale, so maps with adjusted cliffs render
and edit consistently with either atlas size.

### Per-object scale

* Each placed object has a `Scale` field in WorldBuilder's Object Properties (`objectScale` in the
map). It multiplies the template `Scale`.
* WorldBuilder rolls a random scale when it places an object whose template has
`InstanceScaleFuzziness`, so a forest of one tree type gets mixed sizes. The roll is stored in the
map; the game never re-rolls it.
* Works for ordinary objects, optimized trees and props, and under fog of war.

Notes:
* Visual only. Footprint, collision, selection box and bone positions keep the template size.
* Saves from before this build load with every object at scale 1.
([#121](https://github.com/triatomic/contraZH/issues/121))

### Lightning lights the scene

* Maps fake lightning with the `CAMERA_FADE_ADD` script action, which washed the screen white. An
additive fade that lasts 1 second or less now lights terrain, roads, bridges and units with a
bluish-white light above the view instead.
* Longer additive fades, and all other fade types, still draw on the screen as before.
* Health bars and tooltips stay visible during the flash.

Notes:
* Needs `DynamicLights = Yes` in `Options.ini`. Without it the white screen flash is kept.
* Water surfaces do not take the light.
([#121](https://github.com/triatomic/contraZH/issues/121))

### Up to 30 players per map

* A map can hold 32 sides instead of 16. WorldBuilder keeps one free for the replay observer, so
it adds up to 30 players beside the neutral player (retail stopped at 14).
* The Add Player button now greys out at the same count where it stops working.

Notes:
* Games with 16 or fewer sides stay CRC compatible with retail and with older replays.
* Saves from games with 16 or fewer sides keep the retail format. Saves with more sides use a new
format that older builds refuse to load.
* Retail and older contraZH builds cannot load a map with more than 16 sides.
([#82](https://github.com/triatomic/contraZH/issues/82))

### Every road type draws

* Every road in `Roads.ini` now gets a slot. Roads past `MaxRoadTypes` in `GameData.ini` used to
appear in WorldBuilder's list but never drew in the editor or the game.
* `MaxRoadTypes` still sets the minimum number of slots.

Presentation only; replays unaffected.
([#82](https://github.com/triatomic/contraZH/issues/82))

### Neutral teams show in WorldBuilder

* Selecting the neutral player in the Teams dialog now lists its teams. They were hidden before,
so maps could carry neutral teams that nobody could see, edit, move or delete.
* Applies to both the classic and the Qt Teams dialog.
([#82](https://github.com/triatomic/contraZH/issues/82))

### Move to a free supply source

Two script actions send units to the nearest free supply source, for example a GLA Mobile Supply
Stash that deploys there.

| Action | Parameters |
|---|---|
| `Contra/Team_/Move/Set to move to the nearest free supply source` | team, minimum resources |
| `Contra/Unit_/Move/Move a specific unit to the nearest free supply source` | unit, minimum resources |

* Free means not enemy-owned, with no supply center or reserved destination within 20 cells.
* Each unit reserves its chosen source immediately, so trucks travelling together choose separate
sources. The team action uses its lowest-ID living member with AI to hold the reservation.
* Repeated calls retain a usable source, even below the original resource threshold, and leave a
unit that has arrived in place. Reservations survive save/load.
* A dead or captured unit releases its reservation. A unit that stops moving before arrival for
30 seconds releases the source and avoids retrying it for 60 seconds.
* Sources near allied structures are excluded within `SupplyCenterSafeRadius`, measured from the
structures' edges. This also applies to AI supply-center construction, which respects other
players' supply centers and truck reservations.
* Nearest is measured from the unit, or from the team's position.
* If no source holds the minimum resources, the threshold halves until a source qualifies or it
drops to 100. Empty sources never qualify.
* Units stop beside the source on the side facing them, outside the `SupplyBuildBorder`.
* Does nothing when no source qualifies.
([#134](https://github.com/triatomic/contraZH/issues/134))
([#220](https://github.com/triatomic/contraZH/issues/220))

### Manage mobile supply crews

`Contra/Team_/Supply/Manage supply trucks and their workers` takes a team, worker object type,
workers per truck, and minimum source resources. Call it from each truck team's generic hook
with `<This Team>`. The action manages each living mobile cash generator separately.

* Each truck keeps its assigned source while it has supplies. Nearby construction and losses
at other sources do not displace it. The minimum resource threshold applies to new assignments.
* Each truck trains its own workers through its production queue and pays normal unit costs.
Living and queued workers both count toward its crew limit.
* Workers use their producer truck's source and unload at that truck. A busy dock makes them
wait rather than switch to another truck. Their normal collection trips provide the leash.
* Workers follow a new source when their truck relocates after depletion. If the truck dies or
changes owner, its workers can use normal supply selection. A replacement truck can adopt nearby
orphan workers of the configured type before training replacements.
* Truck assignments and worker bindings survive save/load.

`Contra/Player/Supply/A free supply source is available` takes a player and minimum resources.
Use it as the production condition for a repeatable truck template. It rejects occupied sources,
allied bases, reserved destinations, and further production while a living truck lacks a source.
The skirmish AI already permits only one build of a given team template in its queue at a time.

The supplied migration tool replaces four numbered truck slots with one repeatable template in
each GLA faction. Export the latest scripts as JSON, run
`python tools/migrate_mobile_supply_scripts.py before.json after.json`, import `after.json` in
WorldBuilder's Edit mode, and export the resulting SCB. The migration keeps six workers per truck,
removes 40 obsolete scripts and 12 numbered truck teams, and updates supply-guard prerequisites.
Stealth uses 7,500 resources in both production checks and truck actions. The other GLA factions
use 10,000. Free sources near enemy base structures are excluded as well as allied bases.
Choose No when Export Scripts offers to save back to the JSON, then select SCB output.
Both the editor and game must include the new action and condition before using these scripts.
([#222](https://github.com/triatomic/contraZH/issues/222))

### CLP_AI scripts

142 script actions and 67 conditions ported from
[CookieLandProjects/CLP_AI](https://github.com/CookieLandProjects/CLP_AI), under the `CLP_AI` folder in
WorldBuilder's action and condition trees. The scripts were written by toirehantaa and CookieLand
Projects.

| Group | Examples |
|---|---|
| Team and unit orders | move relative, move towards or away from a relation, keep distance, meet, attack or attack-move by type, area or sight, teleport, repair, load transports evenly |
| Player and team management | merge, disband, garrison, hunt, sell, cancel construction, surrender, transfer an area, custom colour |
| Counters and KD ratios | random counter, copy counter, kills to deaths ratios fed by real kills and losses |
| Attack priority sets | apply per player or type, add sets, raise or lower a type |
| Skirmish AI building | rotated placement near a type, kind, team or supply source; base defense lists; closest-dozer priority; build list editing; particle cannon targeting |
| Waypoints | create, remove, relocate, link, label at runtime |
| Conditions | relations, start spots, map control, unit ratios, sighting, losses and kills, captures, team idle, health, reload state |

* Two new parameter types: KD ratio and selection mode (closest, farthest, random, youngest, oldest).
* Not ported, because they need data from CLP_AI's own INI files: ID build lists, AI playstyles,
Brutal/Absurd/Inhumane difficulties.
* "Seen" means the player can see the unit right now. CLP remembered sightings for 10 seconds, but
counted any player's units.
* Kill and loss conditions see every event since their last check, kept for 5 minutes.
* Runtime waypoint changes survive save and load. Removed waypoints stay valid for units already
following them.
* Old saves load unchanged; the new state lives in its own save blocks.

### New factions in PlayerTemplate.ini

`PlayableSide` defaults to `No`. Killing enemy units of these factions now grants the configured
general experience and unit veterancy. Objects ignored in the GUI and objects under construction
still grant neither. Kill/loss scores and cash bounties still require `PlayableSide = Yes`.
([#200](https://github.com/triatomic/contraZH/issues/200))

A faction whose `Side` has no matching scheme in `ControlBarScheme.ini` uses the scheme of its
`BaseSide`, with `USA` read as `America`. Retail fell back to the art-less `Default` scheme, which
left the control bar broken for custom factions such as a mod's `AmericaCampaignGeneral`.

### Command sets swapped by upgrade

A player upgrade can replace the general's powers purchase sets and the powers shortcut bar, so a
tech-path choice can change which powers a general offers. Each key names an upgrade and the set
that stands in for the template's once the player has completed it. A key may repeat; when several
listed upgrades are complete, the last line wins.

```ini
PlayerTemplate FactionAmericaSuperWeapon
  PurchaseScienceCommandSetRank1 = SupW_SCIENCE_AMERICA_CommandSetRank1
  PurchaseScienceCommandSetRank3 = SupW_SCIENCE_AMERICA_CommandSetRank3
  PurchaseScienceCommandSetRank8 = SupW_SCIENCE_AMERICA_CommandSetRank8
  SpecialPowerShortcutCommandSet = SupW_SpecialPowerShortcutUSA
  PurchaseScienceCommandSetRank1Upgrade = Upgrade_AmericaSelectAngels SupW_SCIENCE_AMERICA_Angels_CommandSetRank1
  PurchaseScienceCommandSetRank3Upgrade = Upgrade_AmericaSelectAngels SupW_SCIENCE_AMERICA_Angels_CommandSetRank3
  PurchaseScienceCommandSetRank8Upgrade = Upgrade_AmericaSelectAngels SupW_SCIENCE_AMERICA_Angels_CommandSetRank8
  SpecialPowerShortcutCommandSetUpgrade = Upgrade_AmericaSelectAngels SupW_SpecialPowerShortcutUSA_Angels
End
```

* The four `...Upgrade` keys each take `<UpgradeName> <CommandSetName>`.
* Only player upgrades count (`Type = PLAYER` in `Upgrade.ini`), the kind a `PLAYER_UPGRADE`
button researches. Object upgrades never swap the sets.
* The shortcut bar and an open purchase panel refresh on the frame the upgrade completes. Powers
already bought stay bought; the swap only changes which buttons are shown.
* Presentation only; the simulation and replays are unaffected.

### Folders with a dot in the name

* Maps in a folder with a dot in its name, such as `Maps\Desert v1.2\Desert v1.2.map`, now show
in the map list. Folder scans used to skip every folder with a dot anywhere in its name.
* The same applies to every folder scan of loose files: INI folders, `.big` archives in subfolders
of the game folder, and the user data folder.
* Folders whose name starts with a dot, such as `.git` or a launcher's `.ContraDevSync`, are always
skipped. Before, whether they were skipped depended on the drive's 8.3 short-name setting.

## Game Setup

### Random army per faction

* The army list gets a `Random <faction>` entry per base faction (`Random USA`, `Random China`,
`Random GLA`) after `Random`, for human and AI slots.
* Factions come from `BaseSide` in `PlayerTemplate.ini`, so new mod factions get an entry
automatically.

Notes:
* Labels are `GUI:Random<BaseSide>` (`GUI:RandomUSA`, `GUI:RandomChina`, `GUI:RandomGLA`), falling
back to `Random <BaseSide>`.
* Limit Armies still applies; an entry disappears when none of its generals are allowed.
* All LAN/online players need this build, and so do replays using these entries.

### Rules tab

* The Skirmish, LAN and Generals Online lobbies get **Game** and **Rules** tab buttons in the title
row.
* The Rules page holds Quick Game, Coop Buildings, Diplomacy, Free Radar, Reveal Map, Military Auto
Queue and, in LAN, Max Camera Height. It takes the place of the battle honors and info box in Skirmish, and of the chat
in LAN and Generals Online. The Generals Online page holds every rule but Max Camera Height.

Notes:
* Labels are `GUI:TabGame` and `GUI:TabRules`, falling back to `Game` and `Rules`.
* Rule labels, tooltips and `Enabled` messages have no English fallback. A game folder without them
in its string file shows `MISSING` text.
* The layouts carry `ButtonTabGame`, `ButtonTabRules`, `PanelGame` and `PanelRules`. The buttons and
the Rules page start hidden, so an older exe shows the Game page without the new options.
* `build/add_go_rules_tab_wnd.py` writes a loose `GameSpyGameOptionsMenu.wnd` with the tab pair and
the Generals Online checkboxes; the other two layouts get theirs from `build/add_rules_tab_wnd.py`.

### LAN unready

* A LAN client that has accepted sees **Unready** on the Accept button and can take the accept back.
* The host clears that player's accept, so Start waits for them again.

Notes:
* The label is `GUI:Unready`, falling back to `Unready`.
* Unready greys out once the host's start countdown runs, and a host on this build ignores a late one.
* A host on an older exe also clears the accept, so only the client needs this build.

### Quick Game

* The Skirmish, LAN and Generals Online lobbies get a **Quick Game** checkbox, off by default. Only
the host can change it.
* In a Quick Game, objects use their `QuickGamePrerequisites` in place of `Prerequisites`, so the mod
can drop the rank requirements on advanced structures, superweapons and super units. An object
without `QuickGamePrerequisites` keeps its normal ones. See
[QuickGamePrerequisites](contraZH-Objects-and-Modules.md#quickgameprerequisites).
* Skirmish AI scripts that test `SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD` follow the Quick Game
list, so the AI builds those structures early too.
* The `QUICK_GAME_ENABLED` condition (`Skirmish/ Quick Game is enabled` in WorldBuilder) is true in
a Quick Game. To stop the AI waiting for a rank before it builds, copy the script's condition clause
as a new OR clause and swap the rank test for `QUICK_GAME_ENABLED`.
* Leave rank tests that time attack waves alone. A script that raises a rank counter should not get
the clause either, or the counter jumps at game start; move its other actions into a new script
gated by `QUICK_GAME_ENABLED` instead.

Notes:
* Labels are `GUI:QuickGame`, `TOOLTIP:QuickGame` and `GUI:QuickGameEnabled`.
* The checkbox comes from `CheckQuickGame` in `SkirmishGameOptionsMenu.wnd` and
`LanGameOptionsMenu.wnd`; a layout without it shows no checkbox.
* Save games and replays keep the setting. A save without Quick Game stays loadable by retail.
* All LAN players need this build.
* Generals Online has no lobby field for the rule. Its host ticks it on the Rules tab or types
`/quickgame` in lobby chat, as with Diplomacy, and the host's game turns the rule on in the first
frames of the match.
* An older exe reads `QUICK_GAME_ENABLED` as false, so a script with it still runs on the rank test.
* Changes the simulation; affects replays.

### Diplomacy

* The Skirmish and LAN lobbies get a **Diplomacy** checkbox on the Rules page, off by default. Only
the LAN host can change it.
* With Diplomacy on, players can change alliances during the match. In the Diplomacy window
(the in-game player list), each other player's Team column becomes a stance button.
* The button shows your stance toward that player. **Enemy** means you treat them as an enemy,
**Ally** means you treat them as an ally but they don't return it, and **Allied** means you both do.
Click it to switch between ally and enemy.
* A stance is one-sided and needs no consent. Treating someone as an ally only affects your side.
Your units hold fire at them, stop any attack on them, and share their vision with them.
* An alliance counts for victory and ally chat only when both players are allies of each other. The
match ends once every surviving player is allied with every other one, and they all win.
* Every player sees a message when a stance changes.
* The lobby teams set the starting stances.
* AI players keep their lobby stances. You can treat an AI as an ally, but it never returns the
stance.
* Observers and defeated players can't change stances, and nobody can change stance toward them.

Notes:
* Labels are `GUI:DiplomacyRule`, `TOOLTIP:DiplomacyRule` and `GUI:DiplomacyRuleEnabled`. These also
exist, falling back to English text: `GUI:DiplomacyStanceAlly`,
`GUI:DiplomacyStanceAllied`, `GUI:DiplomacyStanceEnemy`, `TOOLTIP:DiplomacyStanceToAlly`,
`TOOLTIP:DiplomacyStanceToEnemy`, `GUI:DiplomacyAllied`, `GUI:DiplomacyOfferAlly`,
`GUI:DiplomacyBrokeAlliance` and `GUI:DiplomacyEnemy`. The four message labels take two `%ls` player names; `GUI:DiplomacyRuleEnabled`
takes one.
* The checkbox comes from `CheckDiplomacyRule` in `SkirmishGameOptionsMenu.wnd` and
`LanGameOptionsMenu.wnd`. A layout without it shows no checkbox. `build/add_diplomacy_wnd.py` adds it
under Coop Buildings and needs the Rules tab from `build/add_rules_tab_wnd.py`.
* The stance buttons are created in code, so `Diplomacy.wnd` needs no change.
* Generals Online has no lobby field for the rule. Its host ticks it on the Rules tab or types
`/diplomacy` in lobby chat, and the lobby sees an announcement. The host's client mirrors the state
to everyone's Rules tab through a hidden lobby message. The host's game turns the rule on in the
first frames of the match, and every player sees a message. The game ignores that switch from anyone
but the lobby host. A host change turns the rule off, and the lobby sees a message saying so.
* The options string carries `DP=1` only when the rule is on. Older exes can't join such a lobby or
play its replays, but games with the rule off are unaffected.
* All players need this build. Replays play stance changes back exactly.
* Save games keep the stances but not the rule, so a loaded save plays with Diplomacy off.
* Changes the simulation; affects replays.

### Free Radar

* The Skirmish and LAN lobbies get a **Free Radar** checkbox on the Rules page, off by default. Only
the LAN host can change it.
* With Free Radar on, every player has radar from the first frame without building a radar source,
and a power brownout does not take it away.

Notes:
* Labels are `GUI:FreeRadar`, `TOOLTIP:FreeRadar` and `GUI:FreeRadarEnabled`.
`GUI:FreeRadarEnabled` takes one `%ls` player name.
* The checkbox comes from `CheckFreeRadar` in `SkirmishGameOptionsMenu.wnd` and
`LanGameOptionsMenu.wnd`. A layout without it shows no checkbox. `build/add_game_rules_wnd.py` adds
it and Reveal Map in a second column of the Rules page and needs the Diplomacy checkbox from
`build/add_diplomacy_wnd.py`.
* Generals Online has no lobby field for the rule. Its host ticks it on the Rules tab or types
`/freeradar` in lobby chat, as with Diplomacy, and the host's game turns the rule on in the first
frames of the match.
* The options string carries `FR=1` only when the rule is on. Older exes can't join such a lobby or
play its replays, but games with the rule off are unaffected.
* Save games keep the rule.
* The simulation is unchanged; a replay carries the rule and shows the radar.

### Reveal Map

* The Skirmish and LAN lobbies get a **Reveal Map** checkbox on the Rules page, off by default. Only
the LAN host can change it.
* With Reveal Map on there is no shroud: every player sees the whole map and everything on it from
the first frame, the way an observer does.

Notes:
* Labels are `GUI:RevealMap`, `TOOLTIP:RevealMap` and `GUI:RevealMapEnabled`.
`GUI:RevealMapEnabled` takes one `%ls` player name.
* The checkbox comes from `CheckRevealMap` in both lobby layouts; `build/add_game_rules_wnd.py` adds
it beside Free Radar.
* Generals Online has no lobby field for the rule. Its host ticks it on the Rules tab or types
`/revealmap` in lobby chat, and the map opens up in the first frames of the match.
* The options string carries `RM=1` only when the rule is on. Older exes can't join such a lobby or
play its replays, but games with the rule off are unaffected.
* A save game brings the revealed map back on its own.
* Changes the simulation; affects replays.

### Military Auto Queue

* The Skirmish, LAN and Generals Online lobbies get a **Military Auto Queue** checkbox on the Rules
page, off by default. Only the host can change it.
* With the rule on, right clicking a unit cameo controls
[auto queue](contraZH-CommandButtons.md#auto-queue). With it off, right clicks on cameos do nothing new.
* The campaign has no Rules page, so auto queue stays off there.

Notes:
* Labels are `GUI:AutoQueue`, `TOOLTIP:AutoQueue` and `GUI:AutoQueueEnabled`.
`GUI:AutoQueueEnabled` takes one `%ls` player name.
* The checkbox comes from `CheckAutoQueue` in the lobby layouts. `build/add_game_rules_wnd.py` adds it
to Skirmish and LAN, and `build/add_go_rules_tab_wnd.py` to Generals Online.
* Generals Online has no lobby field for the rule. Its host ticks it on the Rules tab or types
`/autoqueue` in lobby chat, and the host's game turns the rule on in the first frames of the match.
* The options string carries `AQ=1` only when the rule is on. Older exes can't join such a lobby or
play its replays, but games with the rule off are unaffected.
* Save games keep the rule.
* Changes the simulation; affects replays.

### Host camera height

* The LAN host gets a `Max Camera Height` checkbox and field (210 to 1000) that overrides everyone's
`MaxCameraHeight` for that game.
* Clients need this build. Replays keep the limit they were played with.
* Generals Online keeps its `/maxcameraheight` lobby command; GO's default (310) means the mod's own
limit.
* The `Shift + Ctrl + Z` zoom-limit cheat is unaffected.

### Map search

* The skirmish, LAN and Generals Online map select windows have a search box above the map list, focused on open.
* Typing filters the list to maps whose name contains the text, ignoring case. It applies to both
official and custom maps.
* The box takes its height from the top of the list and copies the look of the setup screen's text
box (player name in skirmish, chat in LAN), so mod layouts need no `.wnd` change.
