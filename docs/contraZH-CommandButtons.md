# CommandButtons & Selection

## Power requirements

`Options = REQUIRES_POWER` disables a button when its owner lacks sufficient power. It also applies to units using `PoweredBehavior`, whose movement and weapon penalties replace the normal `POWERED` disable. Buttons without this option keep their current behavior. `REQUIRES_POWER` takes precedence over `IGNORES_UNDERPOWERED`.

`Options = IGNORES_UNDERPOWERED` lets an OCL special power activate when low power is its only disable reason. EMP, hacking, subdual, and other disable reasons still block activation. A power that was ready before the shortage remains usable. An unfinished recharge stays paused until power returns, and using the ability starts a new recharge without crediting the earlier pause again.

```
CommandButton Command_ElectricityNet
  Command = SPECIAL_POWER
  SpecialPower = SuperweaponElectricityNet
  Options = IGNORES_UNDERPOWERED
End
```

The UI checks `REQUIRES_POWER` for every command type. Object command-button dispatch and special-power dispatch also check it during execution. Other command types still need execution-path validation before this option can be treated as a general simulation-side restriction.

## New CommandButton Commands

### HOLD_FIRE

Suppresses **automatic target acquisition only**; explicit attack orders still fire. Covers the
unit, its addons and sub-turrets, and contained infantry.

```
CommandButton Command_HoldFire
  Command       = HOLD_FIRE
  Options       = CHECK_LIKE     ; required, or the button never renders as toggled on
  TextLabel     = CONTROLBAR:HoldFire
  ButtonImage   = SNHoldFire
  DescriptLabel = CONTROLBAR:TooltipHoldFire
End
```

Per-object parameter:
* `HoldFireAllowsRetaliation = Yes` - (Default. `No` stops the unit returning fire even when attacked.)

Note: garrisoned buildings cannot hold fire (most have no AI module). Infantry inside a *unit* are
covered.

### TOGGLE_FIRE_WEAPON

Like `FIRE_WEAPON`, but a second click stops it.

```
CommandButton Slth_Command_JammerStationActivate_HumanPlayer
  Command          = TOGGLE_FIRE_WEAPON
  Options          = CHECK_LIKE     ; required, or the button never renders as toggled on
  WeaponSlot       = SECONDARY
  MaxShotsToFire   = 60
  TextLabel        = CONTROLBAR:GLAJamm
  ButtonImage      = SUJPulse
  ButtonBorderType = ACTION
  DescriptLabel    = CONTROLBAR:ToolTipGLAFireJamm
End
```

Notes:
* The button reflects the unit's real state, switching off once `MaxShotsToFire` is spent.
* Stopping ends only the firing; a move order survives.
* Stays clickable while reloading between shots.
* In a mixed selection, if any unit is firing, the click stops all.

### AUTO_FILL

Selects nearby infantry and orders them into the selected container.

```
CommandButton Command_AutoFill
  Command       = AUTO_FILL
  Options       = OK_FOR_MULTI_SELECT
  TextLabel     = CONTROLBAR:AutoFill
  ButtonImage   = SNAutoFill
  DescriptLabel = CONTROLBAR:TooltipAutoFill
End
```

Takes uncontained infantry not already in the group, nearest first. They break off whatever they are
doing.

### EVACUATE_TO_WORK

Evacuates like `EVACUATE`, then sends the supply gatherers among the passengers back to gathering.

```
CommandButton Command_BackToWork
  Command       = EVACUATE_TO_WORK
  Options       = OK_FOR_MULTI_SELECT
  TextLabel     = CONTROLBAR:BackToWork
  ButtonImage   = SNBackToWork
  DescriptLabel = CONTROLBAR:TooltipBackToWork
End
```

Notes:
* Applies to passengers with a `WorkerAIUpdate` or `SupplyTruckAIUpdate`; other passengers exit and idle.
* A gatherer returns to the dock the player last assigned it, else the nearest warehouse.
* A gatherer carrying boxes delivers them to a supply center first.
* Greyed out under the same conditions as `EVACUATE`.

### TOGGLE_DEPLOY
New command for deploying artillery, replacing the old trick of pointing a `FIRE_WEAPON` button at a dummy
weapon slot. That worked, but it went through the weapon lock and brought its bugs with it.

```
CommandButton Command_Deploy
  Command           = TOGGLE_DEPLOY
  Options           = OK_FOR_MULTI_SELECT
  ButtonImage       = SNNukeCannonDeploy
  ButtonBorderType  = ACTION
  TextLabel         = CONTROLBAR:ToolTipDeploy
  UnitSpecificSound = NukeCannonVoiceDeploy
End
```

`TOGGLE_DEPLOY` needs no `WeaponSlot`. It flips the object between deployed and packed, and the button shows
as toggled on while deployed. It works on any object with a `DeployStyleAIUpdate`; on anything else the
button is unavailable.

Manual deploy holds the unit stationary until another order. An in-range attack fires from the deployed
position; an attack that requires movement packs the unit first, approaches, then deploys to fire.
Pressing deploy again cancels the current attack, centers the turret, and packs the unit. It stays packed
until another order or another press of the deploy button, even with enemies nearby.
Automatic deploying when a target comes into range is unchanged outside the manual packed stance.

Selecting a mixed group and pressing the button moves the whole group to one stance rather than flipping each
unit separately, so a second press does not undo half of them.

### Queue reorder

Off by default; enable with `QueueReorder = Yes` in the `GameData` block of GameData.ini.

* Ctrl+click a queue cameo moves it one place earlier. The displaced entry restarts its build time;
finished units of a batch stay finished.
* No effect on the first entry or a finished unit waiting to exit.
* Plain click still cancels; Shift+click still cancels all of that type.

### Auto queue

Needs the [Military Auto Queue](contraZH-Maps-and-Setup.md#military-auto-queue) lobby rule. Right click
a unit cameo on a production building to have that building build the unit by itself.

* The building queues one unit at a time, only when its queue is empty.
* The cameo shows a green frame and an AUTO tag.
* Right click the cameo again to stop. Right clicking another unit switches to that unit.
* Short of money, at the unit limit or out of parking, the building waits and builds when it can.
* Cancelling the last queued unit of that type also stops it. So does losing the building to capture.
* With several producers selected, a right click starts it on all of them. If any already auto queues
that unit, those producers stop instead.
* On a greyed out cameo, a right click can only stop it.
* Save and load keep it.

## New CommandButton Options

### SKIP_CLEAR_PATH_CHECK

For `SPECIAL_POWER_CONSTRUCT` and `SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT` on a unit. The placement
cursor normally turns red where the unit cannot path to, so a unit on an island cannot use the
command at all. Structures never had that check. With the option the unit skips it too; every other
placement rule still applies.

```
CommandButton Command_Dig
  Command       = SPECIAL_POWER_CONSTRUCT
  Options       = NEED_SPECIAL_POWER_SCIENCE SKIP_CLEAR_PATH_CHECK
  SpecialPower  = SpecialPowerDig
  Object        = GLASneakAttackTunnelNetwork
  TextLabel     = CONTROLBAR:Dig
  ButtonImage   = SUDig
  ButtonBorderType = ACTION
  DescriptLabel = CONTROLBAR:ToolTipGLADig
End
```

A unit power that places its object through an `Attack` nugget and the weapon's `FireOCL` now
spawns that object at the placed spot and rotation. See
[Attack nugget: placed shots](contraZH-Object-Creation-List.md#attack-nugget-placed-shots).

## REVERSE_MOVE hotkey

Caps Lock arms upstream's `REVERSE_MOVE` order for any selection, without the button. See
[Reverse move hotkey](contraZH-Options.md#reverse-move-hotkey).

## Structure Multi-Select

Structures of one type can be selected together.

* The select-matching key (E by default) picks same-type structures on screen, or map-wide with Alt.
* Shift+click and Shift+double click add a same-type structure.
* Shift plus a team number (0 to 9) adds that team if it holds only that structure type.
* Mixing with units or other structure types still replaces the selection.

The bar shows shared commands. Build and upgrade commands every producer has are enabled even without
`OK_FOR_MULTI_SELECT`. The queue shows the nine entries closest to finishing.

* Click a unit or upgrade: queue it on the producer that finishes first.
* Shift+click a unit: queue five, spread across producers, stopping at money or unit limits.
* Shift+click an upgrade: queue it on the five fastest producers.
* Shift+click a queue entry: cancel that type everywhere.

## Advanced Waypoints

Waypoint mode queues almost every order a unit can take. Hold the Waypoints key (Alt by default)
and give orders, and each unit carries them out in sequence.

| Type | Orders that can be queued |
| --- | --- |
| Movement | Move, force move, reverse move, attack move |
| Combat | Attack, force attack on a unit or the ground, weapon buttons, guard a position or unit |
| Construction | Build a structure, resume construction, repair |
| Transport | Enter, exit, evacuate, combat drop, dock |
| Services | Get repaired, get healed, hack the internet |
| Special | Special powers and special abilities, with or without a target |
| Toggles | Switch weapon, overcharge, Hold Fire, Deploy |

### Queue rules

* An order given without the key replaces the queue, and Stop and Scatter empty it.
* A toggle given without the key applies at once and leaves the queue alone.
* A queued Deploy only deploys, so it never packs a unit up.
* Units given one order share it, and each unit moves on when its own part ends.
* Consecutive moves join one path, so a unit does not stop at each point.
* An order whose target is gone, or that a unit cannot carry out, is skipped.
* Guard position, hack the internet and an unlimited attack on a location never end, so nothing can
  be queued after them.
* A transport starts its next order after the passengers it is unloading have left.
* A unit that enters a transport keeps the rest of its queue and resumes after it leaves.
* A jet, Chinook or hacker that must take off, land, reload or pack up starts the order afterward.
* Alt plus a command bar hotkey presses that button, so bar orders can be queued from the keyboard.
* A radar click in waypoint mode moves the camera and queues nothing.
* Wall lines, Auto Fill, drag-to-face formations and shortcut special power buttons cannot be
  queued.
* Alt over a transport now queues Enter, so it no longer starts
  [Smart Garrison](GameData.md#smart-garrison).

### Waypoint display

* Holding the key shows the queues of the selected units.
* One queued order draws one marker at the point you clicked, however many units share it.
* Each unit draws a line to its first marker, and shared legs between markers draw once.
* A marker on a target follows that target and disappears when the target is gone.
* Orders with no map position, such as Exit, Evacuate or a toggle, draw no marker.
* A unit shown here draws no [action line](contraZH-Options.md#action-lines) while the key is held.

### Queued buildings

* A queued building appears as a ghost at its site, visible to its owner.
* It is paid for when placed, and the order is refused without the full cost.
* Clearing the queue before the builder starts it refunds the cost.
* Its site blocks later placements, and it counts toward the build limit.
* The build button stays available in waypoint mode while the builder is already constructing.

## Dozer Smart Cast

Build buttons shared by every selected builder are available without `OK_FOR_MULTI_SELECT`. Placing
a building or resuming a scaffold sends one builder, chosen from the selection, and the others keep
working. Dozers and workers both count as builders.

| Order | Builder chosen |
| --- | --- |
| Normal | The nearest idle builder, or the nearest one that is only repairing if none is idle |
| Waypoint mode | The builder with the fewest queued construction orders, then the nearest |

* A normal order skips builders that are already constructing.
* The order does nothing if no builder qualifies.
* A new building considers only builders that can make it and reach the site. Shipyards skip the
  reach check.
* The placement preview is legal when any selected builder can build there.
* A wall line goes to the first selected builder and starts at once.
