# CommandButtons & Selection

## Power requirements

`Options = REQUIRES_POWER` disables a button when its owner lacks sufficient power. It also applies to units using `PoweredBehavior`, whose movement and weapon penalties replace the normal `POWERED` disable. Buttons without this option keep their current behavior. `REQUIRES_POWER` takes precedence over `IGNORES_UNDERPOWERED`.

`Options = IGNORES_UNDERPOWERED` lets an OCL special power activate when low power is its only disable reason. EMP, hacking, subdual, and other disable reasons still block activation. A power that was ready before the shortage remains usable. An unfinished recharge stays paused until power returns, and using the ability starts a new recharge without crediting the earlier pause again. This holds for untargeted, location-targeted, and object-targeted powers alike.

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

### TOGGLE_TUNNEL_AUTO_POP

Turns a tunnel into an exit. While a player has at least one exit, every other tunnel of that player
is an entrance, and units that enter the network come straight out of an exit
([#95](https://github.com/triatomic/contraZH/pull/95)).

```
CommandButton Command_TunnelAutoExit
  Command       = TOGGLE_TUNNEL_AUTO_POP
  Options       = CHECK_LIKE OK_FOR_MULTI_SELECT   ; CHECK_LIKE shows the button as toggled on
  TextLabel     = CONTROLBAR:TunnelAutoExit
  ButtonImage   = SUTunnelAutoExit
  DescriptLabel = CONTROLBAR:ToolTipTunnelAutoExit
End
```

The label and image names above are placeholders; use the mod's own.

The button works on objects with a `TunnelContain` module and is unavailable on anything else,
including `CaveContain`.

Exits:
* An exit cannot be entered. If every tunnel is an exit, nothing can enter the network.
* Each logic frame one exit orders everyone inside the network to leave through it. Several exits
take turns, frame by frame, and a unit uses the exit that ordered it last.
* Units already inside leave as soon as the first exit is switched on.
* A disabled exit (EMP, low power) is skipped. If every exit is disabled, units wait inside.
* A sold, destroyed or captured exit stops being an exit. A captured tunnel joins its new owner's
network as an ordinary tunnel, or as an entrance if that owner has an exit.
* Save and load keep the exits.

Model conditions let art mark the two roles:
* `TUNNEL_AUTO_EXIT` is set on each exit.
* `TUNNEL_AUTO_ENTRANCE` is set on every other finished tunnel while at least one exit exists.
* Both clear when the last exit is switched off or lost.

An icon needs a draw module of its own. In the tunnel's main draw module the block would replace
the building model. `GLATunnelExitIcon` is a placeholder name.

```
Draw = W3DModelDraw ModuleTag_AutoExitIcon
  DefaultConditionState
    Model = NONE
  End
  ConditionState = TUNNEL_AUTO_EXIT
    Model = GLATunnelExitIcon
  End
End
```

With several tunnels selected, one press moves them all to the same state. If any of them is an
exit, the press switches all of them off; otherwise it makes all of them exits. Tunnels under
construction are left alone.

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

## INI-defined radius cursors

`RadiusCursorType` and `AnchorRadiusCursorType` accept additional names defined through
`RadiusCursor <name>` blocks inside `InGameUI.ini`. Existing cursor names keep their behavior.
See [INI-defined identities and announcements](contraZH-SpecialPowers.md#ini-defined-identities-and-announcements)
for complete power, EVA event and cursor examples.
