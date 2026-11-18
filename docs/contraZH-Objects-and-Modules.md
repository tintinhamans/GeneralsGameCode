# Objects & Modules

## Weapon fire particles

`WeaponFireParticleSys = <weapon slot> <particle system>` creates a bone-attached emitter
for each shot. It works in `W3DModelDraw` and its derived draw modules, including
`W3DTankDraw` and `W3DOverlordTankDraw`. It defaults to `None` for each slot.

```ini
DefaultConditionState
  Model                 = NVBtMstr
  WeaponFireFXBone      = PRIMARY Muzzle
  WeaponFireParticleSys = PRIMARY TankCannonSmokeSmall
End
```

The emitter follows the firing barrel's `WeaponFireFXBone`, including turret rotation,
pitch and recoil. Numbered muzzle bones use the barrel selected by the weapon.
`ParticlesAttachedToAnimatedBones = Yes` is not required for this parameter.
The weapon may use `FireFX = None`; ordinary `FireFX` effects remain independent.

The parameter inherits from `DefaultConditionState`. Override it per condition state or
set `WeaponFireParticleSys = PRIMARY None` to disable it. Ordinary condition changes do
not stop existing emitters. Model replacements rebind them by bone name; a missing bone
or a destroyed drawable stops emission while existing particles fade.

Use a particle template with a finite, nonzero `SystemLifetime`. Each shot creates a new
system, so long emission times can overlap during rapid fire. The template's `InitialDelay`,
`BurstCount`, `BurstDelay` and particle lifetime keep their usual meanings.
`TankCannonSmokeSmall`, for example, retains its 12-frame initial delay.
Only the emitter follows the bone; emitted particles retain their template's behavior.

Hidden or shrouded drawables do not create emitters. Existing emitters pause emission
while hidden, following `KEEP_PARTICLES_WHEN_HIDDEN`. Weapon FX suspension also suppresses
new emitters. These temporary systems are client-only and are not saved or recreated on load.

Remove muzzle-smoke `ParticleSysBone` entries and their smoke-only `PREATTACK` and
`TransitionState` blocks when migrating a unit. Keep states that serve other purposes.
Do not also put the same smoke in the weapon's `FireFX`, which would create a second emitter.

## Lifetime progress bars

`LifetimeUpdate` can show a draining lifetime bar on objects with `SHOW_PROGRESS_BAR`
in their `KindOf` list. The game's health-bar display setting must be enabled.

```
  Behavior = LifetimeUpdate ModuleTag_Lifetime
    MinLifetime = 60000
    MaxLifetime = 60000
    ShowProgressBar = Yes
    ProgressBarUpgrade = Upgrade_GLARecycling
  End
```

`ShowProgressBar` defaults to `No`. `ProgressBarUpgrade` defaults to `None`.
When an upgrade is specified, the bar appears only after the object or its current owner
has completed that upgrade. The timer keeps running while the bar is hidden. Researching
the upgrade reveals the remaining lifetime without restarting it.

## Building lights

Building lights turn off while the building is disabled, including low power, EMP, hacking
and scripted power cuts. Lights return when the building recovers, following the normal
day/night rules. The internal `DISABLED_HELD` flag does not turn lights off.

Buildings using `PoweredBehavior` also switch their lights off when their owner runs out
of power. Animated textures keep their scroll and rotation state when lights switch off.

This covers `HEADLIGHT` meshes, building meshes named `LIGHT` or `LIGHT` followed by digits
(such as `Light00`, ignoring case), additive glow and `_emi.dds` textures,
including bloom and path tracing light spill. The change only affects rendering.
The building's other materials keep their authored surface lighting, so a power outage
does not darken the walls and roof or change their normal-map shading.

## Storm draw module

`Draw = W3DStormDraw ModuleTag_Storm` draws a sandstorm (`Type = SAND`) or snowstorm
(`Type = SNOW`) around an object. Module keys use short names such as `Radius` and `GrainCount`;
omitted keys inherit the matching `SandStorm...` or `SnowStorm...` GameData defaults.
See [Storms](dx9-Environment.md#key-names-and-overrides) for the full key mapping and examples.

## Object.ini

### Conditional weapon sync

`AutoChooseSources = TERTIARY SYNC_TO_PRIMARY` fires the tertiary weapon alongside the
primary weapon. Synced slots now require a valid target by default.

```
  WeaponSet
    Conditions = PLAYER_UPGRADE
    Weapon = PRIMARY M113MGGun
    Weapon = SECONDARY M113MGGunAir
    Weapon = TERTIARY M113FlashBangGrenadeWeapon
    AutoChooseSources = TERTIARY SYNC_TO_PRIMARY
    SyncRequiresValidTarget = TERTIARY Yes
  End
```

`SyncRequiresValidTarget` takes one slot and `Yes` or `No`. It defaults to `Yes` for every
slot and only affects slots synced through `AutoChooseSources`. Repeat the key for
multiple slots. All eight weapon slots are supported.

* `Yes` checks the synced weapon's target category, target pitch, range and estimated damage.
  Garrison-clearing weapons use their normal occupied-building damage check. Torpedoes
  require a target over water. Attack-ground orders require a ground-capable weapon in range.
* `No` preserves unconditional target selection. The synced weapon follows its lead even
  when it cannot normally target or damage the victim.
* Both modes require the synced weapon to be ready. Reloading slots produce no firing sound
  or shot notification and spend no ammo.
* Sync still follows the lead weapon's firing events and skips a separate pre-attack wind-up.
  `PreferredAgainst` remains a selection preference and does not restrict valid targets.

Omit `SyncRequiresValidTarget` for conditional sync. Use `SyncRequiresValidTarget = TERTIARY No`
to retain unconditional sync for that slot. These changes affect the simulation, so all players
and replays need matching engine and INI versions.

### WeaponSet line of sight

`IgnoreLineOfSight` lists weapon slots that bypass `ATTACK_NEEDS_LINE_OF_SIGHT`.
The default is `NONE`. Separate multiple slots with spaces.

```
  WeaponSet
    Conditions = WEAPON_RIDER1
    Weapon = PRIMARY CyborgCommandoWeapon
    Weapon = SECONDARY CyborgCommandoLongRangeWeapon
    AutoChooseSources = PRIMARY FROM_PLAYER FROM_SCRIPT FROM_AI DEFAULT_SWITCH_WEAPON
    AutoChooseSources = SECONDARY NONE
    IgnoreLineOfSight = SECONDARY
  End
```

The exception applies while a listed slot is selected in that weapon set. It lets the unit
attack through buildings and over terrain without moving for a clear shot. Range, targeting
rules, projectile collisions and bridge deck obstruction still apply.

Use `IgnoreLineOfSight = SECONDARY TERTIARY` to exempt both slots, or `NONE` to clear the list.
This changes the simulation when enabled; all players and replays need the same INI.

### Reverse movement sounds

* `SoundReverseMoveLoop` plays while the unit moves in reverse.
* `SoundReverseMoveLoopDamaged` plays while the unit moves in reverse and its body state is
worse than `BODY_DAMAGED`, matching `SoundMoveLoopDamaged`.

```
  SoundReverseMoveLoop = TankReverseLoop
  SoundReverseMoveLoopDamaged = TankReverseDamagedLoop
```

The reverse loop replaces the matching `SoundMoveLoop` or `SoundMoveLoopDamaged`. If the
matching reverse sound is omitted, the unit keeps its normal movement sound behavior.
The loop switches when direction or damage state changes during movement. Reverse loops also
play when `SoundMoveStart` or `SoundMoveStartDamaged` is set.

### CaptureTimePercent

* `CaptureTimePercent = 100%` - (Default. Scales how long it takes to capture this object.)

Multiplies the capturer's `PreparationTime` when the target is this object. Applies to infantry
capture (`SPECIAL_INFANTRY_CAPTURE_BUILDING`) and Black Lotus capture
(`SPECIAL_BLACKLOTUS_CAPTURE_BUILDING`).

```
Object AmericaCommandCenter
  CaptureTimePercent = 200%
End
```

With `PreparationTime = 17000`, capturing this Command Center takes 34 seconds.

Notes:
* The capture flash and the raising-flag animation follow the scaled time.
* Changes the simulation; replays must use the same INI.

### QuickGamePrerequisites

* `QuickGamePrerequisites` - (Default: none. Replaces `Prerequisites` when
[Quick Game](contraZH-Maps-and-Setup.md#quick-game) is on.)

Takes the same `Object` and `Science` lines as `Prerequisites`. An object that leaves it out uses
its `Prerequisites` in a Quick Game too.

```
Object AmericaParticleCannonUplink
  Prerequisites
    Object  = AmericaStrategyCenter
    Science = SCIENCE_Rank5
  End
  QuickGamePrerequisites
    Object  = AmericaStrategyCenter
  End
End
```

In a Quick Game the uplink needs only the Strategy Center; otherwise it also needs rank 5.

Notes:
* An empty `QuickGamePrerequisites` block lets the object be built with no prerequisites in a
Quick Game.
* The build tooltip lists the requirements of the current game.
* A map.ini override replaces the list, as it does for `Prerequisites`.
* An `ObjectExtend` child inherits the list until it defines `QuickGamePrerequisites`. Its first
block replaces the inherited entries; later blocks add entries. See [Extend Blocks](contraZH-Extend.md#object-inis).

### Display Decal

A ground decal drawn under the object, for auras, faction markers and similar. It is separate from
the object's shadow: an object can carry a `Shadow` and a display decal at the same time, and the
decal keeps drawing when the player turns 2D or 3D shadows off in the video options. It does hide
under shroud, while the object is hidden, and once the object dies, slow death included.

Decals stay visible when their model leaves the camera view but the decal still reaches visible
terrain. This also applies to legacy `ShadowTexture` decals. Contained addons follow their
carrier's hidden and shroud state.

Added new parameters for object definitions:

```
  DisplayDecal = Yes                ; default No
  DecalTexture = ShadowIH           ; required; the engine appends .tga
  DecalSizeX = 14                   ; world-space size in X
  DecalSizeY = 14                   ; world-space size in Y
  DecalOffsetX = 0                  ; world-space offset in X
  DecalOffsetY = 0                  ; world-space offset in Y
  DecalStyle = SHADOW_ALPHA_DECAL   ; SHADOW_DECAL, SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL
  DecalColor = R:255 G:255 B:255    ; tint, default white
  DecalOpacity = 100%               ; default 100%
  DecalHideWhenDisabled = No        ; default No
```

* `DecalTexture` has no default, unlike `ShadowTexture`. An object that sets `DisplayDecal = Yes`
without naming a texture draws nothing.
* `DecalStyle` takes only the three decal blend styles; the projection and volume types need a
shadow-casting setup a display decal does not have, and fall back to `SHADOW_ALPHA_DECAL`.
  - `SHADOW_DECAL` multiplies, exactly like a 2D shadow. White is invisible and darker pixels
  darken the ground, so the art needs no alpha channel at all - a plain DXT1 works.
  - `SHADOW_ALPHA_DECAL` alpha blends, for a painted marker. Needs a real alpha channel.
  - `SHADOW_ADDITIVE_DECAL` adds, for a glow. Ignores alpha; black is what reads as nothing.
* Whichever style, the decal always rasterizes the full `DecalSizeX` by `DecalSizeY` rectangle,
snapped outward to whole terrain cells, and relies on clamped texture addressing. The art has to
fade out at its own edges - an opaque edge texel gets stretched across the whole footprint.
* `DecalHideWhenDisabled = Yes` stops drawing the decal while the object is disabled - EMP,
hacked, subdued, unmanned, out of power, disabled by script and so on - and brings it back when
the object recovers. Useful when the decal reads as something the unit is actively doing.
Being held by a container alone does not hide the decal, so visible addons such as Overlord
propaganda towers keep their auras. Other disabling states still hide it.
* `DecalColor` tints the texture. Any alpha written here is ignored - `DecalOpacity` is what fades
the decal. Both are ignored under `SHADOW_DECAL`, whose blend has no way to apply them.
* `DecalOpacity` means different things per style. Under `SHADOW_ALPHA_DECAL` it is an ordinary
alpha fade. Under `SHADOW_ADDITIVE_DECAL` the blend ignores alpha entirely, so opacity instead
scales the colour toward black, dimming the glow.
* A unit with several draw modules gets one decal, not one per module.
* The whole feature can be switched off by the player with `ObjectDecals = No` in Options.ini.

This is for a decal that belongs to a unit for its whole life. For a temporary decal spawned by an
effect — a scorch mark or a footprint — use the [Decal FX nugget](FXList-&-ParticleSystems.md#decal-entries) with `W3DDecalDraw` instead.

## Turret modules

### Limited turret angles

Units with `MinTurretAngle` or `MaxTurretAngle` turn their hull as soon as a target lies outside
the turret's arc. `AimDelta` no longer widens the hull's allowed arc, which left a band near each
limit where the turret could not fire and the hull would not turn. Targets exactly at either limit
remain valid.

Changes the simulation for units using these limits; older replays may play differently.

### MaxPhysicalPitch

* `MaxPhysicalPitch = 90` - (Default. The highest pitch a turret with `AllowsPitch = Yes` will aim at,
in degrees.)

Counterpart of `MinPhysicalPitch`. Stops a vehicle's cannon pointing skyward at out-of-range
targets.

## RiderChangeContain

Two opt-in fields on the combat bike contain module, both default `No`.

### SurviveScuttle

* `SurviveScuttle = No` - (Default. `Yes` keeps the transport alive after its rider dismounts.)

The abandoned bike topples into its `ScuttleStatus` pose and stays. A new valid rider stands it back
up.

Notes:
* Not box-selectable, but can be right-clicked, so a rider can be ordered onto it.
* Enemies can still destroy it; only its owner can mount it.
* `ScuttleDelay` is unused.

### SilentScuttle

* `SilentScuttle = No` - (Default. `Yes` scuttles the bike without announcing a loss.)

A dismount scuttle no longer triggers "Unit Lost", a radar ping or an "under attack" warning. A bike
killed by an enemy still announces. Kill credit and score are unchanged. Not needed with
`SurviveScuttle = Yes`.

## MissileAIUpdate

### DetonateOnTargetLost

* `DetonateOnTargetLost = Yes` - (Default. `No` keeps the retail lost-target behaviour.)

A missile whose target dies in flight flies straight to where the target was last seen and detonates
there. A missile already out of fuel detonates where it is. Retail missiles vanished without
exploding.

`No` makes the missile kill itself on the spot with a `NORMAL` death, as retail did. Set it on any
missile whose die modules react to that death. The frog missile upgrade retargets this way, so its
missile opts out:

```
Object F18MissileFrog
  Behavior = MissileAIUpdate ModuleTag_08
    DetonateOnTargetLost = No
    TryToFollowTarget    = Yes
    DetonateCallsKill    = Yes
  End
  Behavior = CreateObjectDie ModuleTag_03A
    DeathTypes   = NONE +NORMAL
    CreationList = OCL_F18RetargetObject
  End
End
```

When the target dies, the missile dies a `NORMAL` death where it is, and `OCL_F18RetargetObject`
spawns a new attacker there. With `Yes` the missile would detonate where the target was last
seen instead, die a `DETONATED` death and never spawn the retarget object.

Notes:
* `DetonateCallsKill = Yes` is needed, or the missile is destroyed without running its die modules.
* Torpedoes (`IsTorpedo = Yes`) ignore the key. They chase the target's last position and detonate
there.
* A missile diverted to a flare still deals no damage. One whose target died before the flare
diversion keeps its damage.
* Changes the simulation; replays must use the same INI.

## FireWeaponWhenDamagedBehavior

### NoHealthLoss

* `NoHealthLoss = No` - (Default. `Yes` refunds the health taken by any hit that passes the
`DamageTypes` and `DamageAmount` gate.)

For using the module as a trigger (e.g. a 1 point `MELEE` hit) without the trigger hit costing
health.

Notes:
* Every qualifying hit is refunded, even when no weapon fires.
* Qualifying hits can no longer kill the object or change its damage state.
* The hit still plays damage effects and raises "under attack".
* Other damage types and smaller hits drain health normally.

## FireWeaponUpdate

### Upgrade parameters

`FireWeaponUpdate` and `FireWeaponAdvancedUpdate` now take the standard upgrade keys, so a
self-firing weapon can be switched on by an upgrade instead of needing a separate object variant.

* `TriggeredBy` - upgrades that switch the module on. Any one of them is enough unless
`RequiresAllTriggers = Yes`.
* `ConflictsWith` - upgrades that switch it off. A conflict wins over a trigger.
* `RequiresAllTriggers = No` - (Default. `Yes` demands every upgrade in `TriggeredBy`.)
* `RemovesUpgrades` - upgrades stripped from the object when the trigger first completes.
* `FXListUpgrade` - played once when the trigger first completes.

With no `TriggeredBy` listed the module fires as it always has, so existing objects need no change.

Notes:
* The gate is re-read every frame rather than latched, so an upgrade named in `ConflictsWith` stops
the firing again once it completes. Most upgrade modules only ever turn on; this one can turn back
off.
* `RemovesUpgrades` and `FXListUpgrade` still run only once, on the frame the trigger completes.
* Both the object's own upgrades and its owning player's upgrades count toward the gate.

## MinefieldBehavior

### KindOf filters

Which objects can set the mine off, checked on top of `DetonatedBy` and `WorkersDetonate`:

```ini
  Behavior = MinefieldBehavior ModuleTag_Mine
    DetonationWeapon = AntiTankMineWeapon
    DetonatedBy      = ENEMIES NEUTRAL
    RequiredKindOf   = VEHICLE
    ForbiddenKindOf  = HUGE_VEHICLE
  End
```

* `RequiredKindOf` - (A detonator needs at least one of these. Default = everything.)
* `ForbiddenKindOf` - (A detonator may have none of these. Default = none.)

An object that fails the filter walks over the mine without triggering it. Mine clearing is
unaffected.

## GenerateMinefieldBehavior

### Placement blockers

`SkipIfThisMuchUnderStructure` used to skip a mine only under a `STRUCTURE`. Two keys now pick the
blocking objects:

* `BlockedByKindOf = STRUCTURE` - (Default. A mine is skipped when an object with any of these
overlaps it. `NONE` places mines under anything; `+VEHICLE` adds to the default.)
* `NotBlockedByKindOf` - (An object with any of these never blocks a mine. Default = none.)

## StealthUpdate

### New StealthForbiddenConditions

New values for `StealthForbiddenConditions` (and `OverrideStealthForbiddenConditions` on
`StealthUpgrade`):

* `RIDERS_FIRING_PRIMARY`, `RIDERS_FIRING_SECONDARY`, `RIDERS_FIRING_TERTIARY` - no stealth while a
passenger fired that slot this frame or last. Only for `PassengersAllowedToFire = Yes`. Unlike
`RIDERS_ATTACKING`, they reveal only on shots, so the container can re-cloak between them.
* `UNIT_CREATED` - no stealth in the frame it finishes producing a unit (production queue, spawner
or drone release, dozer finishing a structure).

Each breaks stealth for one frame; `StealthDelay` decides when it can cloak again.

## AIUpdateInterface

New `AutoAcquireEnemiesWhenIdle` value:
* `NOTWHILEMOVING` - the object will not shoot on the move. Intended for artillery that has to be stationary
to fire, which previously had to be faked in other ways.
Combine it with the other values as usual, e.g. `AutoAcquireEnemiesWhenIdle = Yes NOTWHILEMOVING`.

What it does, by order type:

| Order | Behaviour |
|---|---|
| Move | Drives past enemies without stopping or engaging, including while turning in place or waiting for a path. |
| Attack-move | Drives, spots a target, halts once in range, fires, then carries on. |
| Attack (direct order) | Closes on the target, halts, fires. |
| Stationary | Fires normally. |

Warning: a unit with a second turret also holds that turret's idle scan while the chassis turns in
place to aim. Check multi-turret units that use `NOTWHILEMOVING`.

This takes two separate checks, because the two halves pull against each other. Targets of opportunity - the
idle scans, including a turret scanning while the chassis under it drives - are suppressed while the object is
in motion, which is what keeps it from shooting on an ordinary move. Attack-move deliberately still acquires
on the move, because spotting a target while driving is exactly what makes the unit stop for it; the object
then keeps aiming, without firing, until it has actually come to rest. It holds its target throughout and
fires the moment it settles, so any PreAttackDelay wind-up plays where the shot is taken rather than being
spent on the approach.

## Contain modules

### Targeting through addon turrets

A unit whose weapons live on a contained addon turret (an OverlordContain or MultiAddOnContain
rider) could not be ordered to attack anything only the turret can hit -- the cursor showed a red
cross, and the workaround was a dummy weapon on the carrier with the turret's range. This key
replaces the dummy weapon. It works on every contain module except TunnelContain and CaveContain,
where it parses but stays inert: their contained list is the player's whole shared network, so
the answers would come from units sitting at other entrances.

* `AcceptTargetsForPassengers = No` - (Yes lets the container accept attack orders on behalf of its
passengers: when the container's own weapons cannot attack a target, the passengers' weapons are
asked instead, and their best answer drives the cursor and the order. Requires
`PassengersAllowedToFire = Yes`.)

```
Behavior = OverlordContain ModuleTag_Turret
  Slots                     = 1
  AllowInsideKindOf         = PORTABLE_STRUCTURE
  PassengersAllowedToFire   = Yes
  PassengersInTurret        = Yes
  PayloadTemplateName       = AmericaThorTurretBolt
  AcceptTargetsForPassengers = Yes   ; New
End
```

Behaviour notes:
* When the order is given, the turret is told to attack (the engine already forwards attack orders
to firing passengers); the carrier's own guns do not try to aim at a target they cannot attack.
* A turret that is EMP'd, hacked, subdued or paralyzed does not answer for the carrier.
* The turret cannot move, so a target beyond its range shows the out-of-range cursor rather than a
green attack cursor -- the carrier does not automatically drive into range. Keep a dummy weapon if
you want the carrier to approach on its own.

### Add-on turret range from the carrier's center

An add-on turret is a separate object placed at a bone on its carrier -- `AddOnBoneName` for
MultiAddOnContain, the FIREPOINT bones for OverlordContain -- and with `PassengersInTurret = Yes`
that bone rides the carrier's turret. Weapon range is measured from the firing object's own
position and bounding circle, so the turret's reach swings by the bone's offset as the carrier's
turret sweeps: longer the way the barrel points, shorter the other way. The Overlord's gattling
shows it, and a turret mounted at the front of a chassis shows it plainly -- it opens fire early
forward and falls short backward, and no single `AttackRange` works in both directions.

* `AddOnWeaponRangeFromCenter = No` - (Yes measures the add-on's weapon range from the carrier's
center, using the carrier's bounding circle, instead of from its own attachment point.)

```
Behavior = OverlordContain ModuleTag_Turret
  Slots                      = 1
  AllowInsideKindOf          = PORTABLE_STRUCTURE
  PassengersAllowedToFire    = Yes
  PassengersInTurret         = Yes
  PayloadTemplateName        = AmericaThorTurretBolt
  AddOnWeaponRangeFromCenter = Yes   ; New
End
```

Behaviour notes:
* An add-on whose `AttackRange` equals the carrier's now reaches exactly as far as the carrier
does, in every direction, at every turret angle -- the range no longer has to be tuned to
compensate for where the bone sits.
* `MinimumAttackRange` moves with it, so the too-close band is measured from the carrier's hull
too, as it would be for a weapon mounted on the carrier.
* The approach distance moves with it, so a carrier ordered to attack stops where its add-on can
actually reach.
* Only the range test moves. The add-on still aims and fires from its own barrel, and its line of
sight, muzzle effects and projectiles are unchanged.
* Parses on every contain module but stays inert on TunnelContain and CaveContain, whose passengers
sit at whichever entrance they used rather than on a bone of the container.


### Load slowdown from occupants

A container can be slowed by what it carries. The penalty scales with how full it is, so each
passenger costs a share of it and leaving gives that share back; an emptied container returns to
exactly its original speed. Fullness is measured in slots, not bodies, so a unit that takes three
slots slows the transport three times as much as one that takes a single slot.

The defaults come from GameData's `TransportLoadSpeedPenalty` and friends, and these four override
them per container. Each is the fraction of that value lost at a full load.

* `LoadSpeedPenalty = 0%` - (Fraction of `Speed` lost at a full load.)
* `LoadTurnRatePenalty = 0%` - (Fraction of `TurnRate` lost at a full load.)
* `LoadAccelerationPenalty = 0%` - (Fraction of `Acceleration` lost at a full load.)
* `LoadLiftPenalty = 0%` - (Fraction of `Lift` lost at a full load.)
* `LoadPenaltyEnabled = Yes` - (`No` exempts this container entirely, whatever GameData sets. Use it
to keep one transport at full speed without restating every percentage.)
* `LoadPenaltyKindOf = <KindOf list>` - (If set, only occupants with one of these KindOfs count
toward the load. Everything counts by default.)
* `LoadPenaltyForbidKindOf = <KindOf list>` - (Occupants with any of these KindOfs never count
toward the load. Nothing is excluded by default.)

Example - a transport that is dragged down by vehicles but not by the infantry it carries:
```
Behavior = TransportContain ModuleTag_07
  Slots                   = 8
  LoadSpeedPenalty        = 40%
  LoadAccelerationPenalty = 25%
  LoadPenaltyForbidKindOf = INFANTRY
End
```

Behaviour notes:
* Each penalty covers the damaged variant of its value, so `SpeedDamaged` scales by the same
percentage as `Speed`.
* The penalty follows the container across locomotor sets, so an upgrade that grants
`SET_NORMAL_UPGRADED`, or a switch to `SET_PANIC`, keeps it in effect.
* A container that cannot move ignores all of this. The keys parse on every contain module, but a
garrisoned building has no locomotor to slow down.
* Slot counts come from the occupant's `TransportSlotCount`, so a bunker riding an Overlord is
weighed by what is inside it.
* A loaded transport moving with a group slows the whole group, the same way any slow unit does.
* Leaving every penalty at `0%` is the previous behavior exactly, so existing INI is unaffected.

### Filtering passengers by object name

`AllowInsideKindOf` and `ForbidInsideKindOf` can only speak in whole KindOfs. These two name
individual objects instead, and work on every contain module -- TunnelContain, TransportContain,
GarrisonContain, CaveContain, OverlordContain, HelixContain and the rest.

* `ForbidInsideObjects = <object list>` - (These objects can never enter. Checked first, so it
beats everything else, including `AllowInsideObjects`.)
* `AllowInsideObjects = <object list>` - (If set, ONLY these objects may enter, whatever their
KindOfs say. Leave it out to allow everything the other filters permit.)

Example -- a tunnel network that refuses one specific unit:
```
Behavior = TunnelContain ModuleTag_05
  TimeForFullHeal = 5000
  ForbidInsideObjects = Aslt_GLAInfantryHijacker
End
```

Example -- a transport that carries nothing but two named units:
```
Behavior = TransportContain ModuleTag_07
  ContainMax = 4
  AllowInsideObjects = Aslt_GLAInfantryJarmenKell Aslt_GLAInfantryHijacker
End
```

Both keys take several names per line and append across repeated lines, so a long list can be
split up. Matching is on the object name and ignores case.

Note: these filters decide whether a unit may *enter*. They do not evict anyone already inside,
so changing them does not affect units that are already loaded.

## FlightDeckBehavior

Aircraft carriers accept the same landing filters as `ParkingPlaceBehavior`.

* `RequiredKindOf = <KindOf list>` requires every listed KindOf. Defaults to none.
* `ForbiddenKindOf = <KindOf list>` rejects aircraft with any listed KindOf. Defaults to none.
* `AllowedObjects = <object list>` limits landing to the named aircraft when the list is nonempty.
* `ForbiddenObjects = <object list>` rejects the named aircraft, even if also allowed.

Aircraft must pass both the KindOf masks and the object-name lists. Names ignore case, and each
object-list line replaces the previous value. Leaving all four keys out keeps existing behavior.
The filters apply when checking space for production or landing and when reserving a parking space.

Add these fields to an existing flight deck's behavior block, alongside its runway settings.

```ini
RequiredKindOf = AIRCRAFT
ForbiddenKindOf = PRODUCED_AT_HELIPAD
AllowedObjects = AmericaJetRaptor AmericaJetStealthFighter
ForbiddenObjects = AmericaJetAurora
```

## ParkingPlaceBehavior

### Filtering aircraft by object name

`RequiredKindOf` and `ForbiddenKindOf` can only speak in whole KindOfs. These two name individual
objects instead, so a single aircraft can be kept off an airfield without inventing a KindOf for it.

* `ForbiddenObjects = <object list>` - (These aircraft can never land here. Checked first, so it beats
everything else, including `AllowedObjects`.)
* `AllowedObjects = <object list>` - (If set, ONLY these aircraft may land here. Leave it out to allow
everything the other filters permit.)

Example - an airfield that takes only two of the player's jets:
```
Behavior = ParkingPlaceBehavior ModuleTag_park
  NumRows = 2
  NumCols = 2
  HasRunways = Yes
  ApproachHeight = 40
  AllowedObjects = AirF_AmericaJetRaptor SupW_AmericaJetStealthFighter
End
```

Both keys take several names on one line, and a second line of the same key replaces the first rather
than adding to it. Matching is on the object name and ignores case.

Notes:
* These filters are checked in addition to `RequiredKindOf` and `ForbiddenKindOf`, not instead of them.
An aircraft has to pass both to land, so naming it in `AllowedObjects` does not get it past a
`ForbiddenKindOf` that rejects it.
* Leaving both keys out is the previous behavior exactly, so existing INI is unaffected.
* The filters decide whether an aircraft may land or be produced here. They do not evict anything
already parked, so changing them does not affect aircraft that are on the airfield.
* Aircraft with `KINDOF_PRODUCED_AT_HELIPAD` skip these filters, as they already skip the KindOf ones.

## DozerAIUpdate / WorkerAIUpdate

Both modules build and repair structures, and both take the same restriction keys. A module without
them behaves exactly as before.

### Restricting what may be built

* `ForbiddenBuildObjects = <object list>` - (These may never be built. Checked first, so it beats
everything else, including `AllowedBuildObjects`.)
* `AllowedBuildObjects = <object list>` - (If set, ONLY these may be built. Leave it out to allow
everything the unit's CommandSet offers.)

Example - a worker that may only put up the two cheapest structures:
```
Behavior = WorkerAIUpdate ModuleTag_worker
  RepairHealthPercentPerSecond = 2%
  BoredTime = 5000
  BoredRange = 150
  AllowedBuildObjects = GLAPowerPlant GLABarracks
End
```

Both keys take several names on one line, and a second line of the same key adds to the first rather
than replacing it. Matching is on the object name and ignores case.

Notes:
* The list gates starting a building, resuming one somebody else began, and a GLA hole rebuild. An AI
player is held to it too, unlike the normal build validation which AI players skip.
* A forbidden structure's command button greys out rather than disappearing, so button positions stay
the same across units that share a CommandSet.
* This is checked in addition to the unit's CommandSet, not instead of it. Naming something in
`AllowedBuildObjects` does not let a unit build what its CommandSet never offered.

### Removing the repair ability

* `CanRepair = Yes` - (Default. `No` takes the repair ability away entirely.)

A unit with `CanRepair = No` shows no repair cursor over a damaged building, ignores a repair order,
and does not go looking for something to repair when it gets bored.

### Giving a dozer to an armed unit

A stock dozer carries no weapon except a mine clearing one. When it has been idle for `BoredTime` it
looks for something to repair, and failing that it sets its mine clearing weapon set and attacks the
nearest enemy or neutral object within `BoredRange`. That search does not check that the target is a
mine - it checks that the target is attackable, which on a stock dozer only mines are.

Put `DozerAIUpdate` or `WorkerAIUpdate` on a unit that keeps a real weapon and the same search starts
returning ordinary neutral objects, so the unit opens fire on scenery whenever it goes idle. Two ways
around it:

* `BoredRange = 0` - the search runs with a zero radius and finds nothing, so the unit never picks a
bored target. This also switches off the bored auto-repair scan, which shares the same range. Direct
repair orders still work.
* Give the unit a weapon set under `WEAPONSET_MINE_CLEARING_DETAIL` that cannot hit ordinary ground
targets. The bored search then fails the attackability test for everything but mines, the same way it
does on a stock dozer, and bored auto-repair keeps working.

`BoredTime = 0` does not help - the check is `idle time > BoredTime`, so zero makes the unit run the
search on every idle frame rather than never.

## PoisonedBehavior

Added Beta and Gamma poison tiers, so the poison-over-time effect can be strengthened once the attacker owns an upgrade. The retail parameters keep working unchanged.

* `PoisonBetaDamageInterval = 0` - tick interval while Beta poison is active. 0 uses `PoisonDamageInterval`
* `PoisonBetaDuration = 0` - how long Beta poison lasts after the last dose. 0 uses `PoisonDuration`
* `PoisonBetaDamageBonus = 1.0` - multiplier on the per-tick damage (1.2 = 20% more)
* `PoisonBetaTriggeredBy = <upgrade name>` - upgrade the attacker needs for Beta poison. Empty disables the tier
* `PoisonGammaDamageInterval = 0` - as above, for Gamma
* `PoisonGammaDuration = 0` - as above, for Gamma
* `PoisonGammaDamageBonus = 1.0` - as above, for Gamma
* `PoisonGammaTriggeredBy = <upgrade name>` - as above, for Gamma

Notes:
* The tier is picked per hit from the attacker, not the victim. The upgrade counts if the attacking player has researched it or the attacking object itself carries it, so both player and object upgrades work.
* Gamma is checked before Beta. If neither upgrade is present the hit applies normal poison.
* Only one tier is active at a time and poison never stacks. A stronger hit takes over damage, interval and duration; an equally strong hit refreshes them as retail does; a weaker hit is ignored while the stronger poison is still running.
* A hit with `DeathType = POISONED` is promoted to `POISONED_BETA` or `POISONED_GAMMA` for the active tier, so die modules and death FX can tell the tiers apart. A weapon that already names a tier death type is left alone.
* If the attacker no longer exists when the damage lands, the hit counts as normal poison.
* Healing of any kind still cures poison completely and clears the tier.

## NeutronBlastBehavior

The neutron blast that kills infantry and leaves vehicles unmanned. Two parameters were added so
the effect can be kept off things it should not touch.

```
Behavior = NeutronBlastBehavior ModuleTag_neutron
  BlastRadius = 100.0
  AffectAirborne = No
  AffectAllies = No
  AffectGarrison = Yes         ; New
  RejectEffectOnUnit = CyborgCommando AnotherUnit YetAnotherUnit   ; New
End
```

Added parameters:
* `AffectGarrison = Yes` - (No spares infantry garrisoned in a structure. Only garrisons are
affected by this: passengers of transports, tunnels and bunkers are killed either way, as before.)
* `RejectEffectOnUnit = <object list>` - (Object names that the blast skips entirely, whatever their
KindOfs. This is the only way to spare a unit the hardcoded infantry and vehicle rules, which is what
it exists for: riders such as the Cyborg Commando. A rejected object keeps its passengers too. All
names go on one line; a second line replaces the first. Matching ignores case.)

## SpecialAbilityUpdate

The valid targets for the laser lock ability (`SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES`) used to be hardcoded to
`VEHICLE`, which in Zero Hour covers aircraft as well as ground vehicles, and to enemies only. These three keys make
the target filter data-driven.

* `ForbiddenTargetKindOf = <KindOf list>` - (The target may have none of these. Checked first, so it beats
`RequiredTargetKindOf`. Default = `STRUCTURE`.)
* `RequiredTargetKindOf = <KindOf list>` - (The target must have **all** of these. Default = `VEHICLE`.)
* `TargetRelationship = [ALLIES/ENEMIES/NEUTRALS]` - (Which relationships to the owner may be targeted. Several may be
listed together. Default = `ENEMIES`.)

Example - an Avenger that can only lock onto enemy ground vehicles:
```
Behavior = SpecialAbilityUpdate ModuleTag_09
  SpecialPowerTemplate  = SpecialAbilityLaserGuidedMissiles
  StartAbilityRange     = 200.0
  RequiredTargetKindOf  = VEHICLE
  ForbiddenTargetKindOf = STRUCTURE AIRCRAFT   ; New
  TargetRelationship    = ENEMIES              ; New
End
```

The defaults reproduce the original hardcoded behaviour exactly, so leaving all three keys out changes nothing.

Behaviour notes:
* `RequiredTargetKindOf` is an all of test, not an any of test. Listing two KindOfs means the target must have both.
* All three keys are honoured in two places, and always agree: the check that decides whether the cursor and the order
are valid, and the check that runs while a lock is already in progress and cancels it if the target stops qualifying.
Because the relationship is part of that test, a lock breaks off if the target changes sides mid lock - by being
hijacked or captured - unless the new relationship is also allowed.
* To let the ability target allies, **both** `TargetRelationship` here and `NEED_TARGET_ALLY_OBJECT` on the
CommandButton are needed. The button filters the player's click; this key covers the paths that never see a button and
keeps checking while the lock is held.
* Allowing a relationship other than `ENEMIES` means the ability really will shoot that target - the missile is fired as
a forced attack, so the usual "you may not attack allies" rule does not stop it. Do not allow allies unless that is what
you want.
* The AI never reads CommandButton target options, and its own scan only considers enemies, so it will not use ally or
neutral locking even when both are configured.
* These keys currently affect the laser lock ability only. Other abilities that use `SpecialAbilityUpdate` keep their
own built in target rules and ignore all three.

### Black Lotus vehicle control

`SPECIAL_BLACKLOTUS_CONTROL_VEHICLE_HACK` gives temporary control of an enemy ground vehicle.
It uses `SpecialAbility` and `SpecialAbilityUpdate`, like the disable vehicle hack, and can coexist
with that ability on the same unit. Aircraft, structures, contained units and vehicles already
under temporary control cannot be targeted.

| SpecialAbilityUpdate key | Default | Effect |
| --- | --- | --- |
| `ControlWhileHacking` | `No` | `Yes` keeps control while Lotus maintains the hack. |
| `SustainControlAfterHack` | `0` | Milliseconds of control after continuous hacking stops. |
| `EffectDuration` | `0` | Milliseconds of control after a completed hack when `ControlWhileHacking = No`. |

Continuous control starts after `UnpackTime` and `PreparationTime`. Moving, stopping, using another
ability, leaving `AbilityAbortRange`, entering a transport, dying, becoming disabled or changing owners
ends the hack. The vehicle returns after `SustainControlAfterHack`. The bar stays full while the
hack continues and drains during that delay.

Timed control lasts for `EffectDuration` after preparation completes. Lotus can then move, use another
ability or die without shortening the timer. Use a positive duration. Leave `PersistentPrepTime`
and `PersistenceRequiresRecharge` at their defaults for this ability.

The vehicle returns to its previous team. If that team no longer exists, it returns to its previous
player's default team. If the previous player has been defeated, the vehicle becomes neutral.
A later capture by another team cancels the return timer. Saving and loading preserves control and
the remaining time. The controlling player sees a draining bar above the vehicle without needing
`SHOW_PROGRESS_BAR` on its template. The bar follows the global health-bar display setting.

```ini
SpecialPower SpecialAbilityBlackLotusControlVehicleHack
  Enum = SPECIAL_BLACKLOTUS_CONTROL_VEHICLE_HACK
  ReloadTime = 0
  PublicTimer = No
End

Behavior = SpecialAbility ModuleTag_ControlVehicle
  SpecialPowerTemplate = SpecialAbilityBlackLotusControlVehicleHack
  UpdateModuleStartsAttack = Yes
  InitiateSound = BlackLotusVoiceStartVehicleHack
End

Behavior = SpecialAbilityUpdate ModuleTag_ControlVehicleUpdate
  SpecialPowerTemplate = SpecialAbilityBlackLotusControlVehicleHack
  StartAbilityRange = 200.0
  AbilityAbortRange = 240.0
  UnpackTime = 2000
  PackTime = 1000
  PreparationTime = 2000
  ControlWhileHacking = Yes
  SustainControlAfterHack = 3000
  SpecialObject = BinaryDataStream
  PackSound = BlackLotusPack
  UnpackSound = BlackLotusUnpack
  TriggerSound = BlackLotusTrigger
  PrepSoundLoop = BlackLotusPrepLoop
End
```

For a completed hack that grants 15 seconds of control, use `ControlWhileHacking = No` and
`EffectDuration = 15000` instead. Link a `SPECIAL_POWER` command button to the new template and
use `NEED_TARGET_ENEMY_OBJECT` in its options.

## ParticleUplinkCannonUpdate

### ScorchType

The particle beam used to burn a random scorch decal (`SCORCH_1` to `SCORCH_4`) every time it marked the ground, which
a mod cannot change. Mods that replace the scorch art with crater-like decals end up with craters punched along the
beam path. This key picks which decals the beam is allowed to use, or turns them off entirely.

* `ScorchType = SCORCH_1 SCORCH_2 SCORCH_3 SCORCH_4` - (The scorch types the beam may burn. One name always burns that
type, several pick randomly among just those, and `NONE` burns no scorches at all. Accepts `SCORCH_1`, `SCORCH_2`,
`SCORCH_3`, `SCORCH_4`, `SHADOW_SCORCH` and `NONE`.)

Example - a beam that only leaves the fourth scorch decal:

```
Behavior = ParticleUplinkCannonUpdate ModuleTag_12
  TotalScorchMarks = 20
  ScorchMarkScalar = 1.0
  ScorchType       = SCORCH_4   ; New
End
```

Omitting `ScorchType` reproduces the original random `SCORCH_1` to `SCORCH_4` behaviour exactly, so data that does not
mention it is unaffected.

Behaviour notes:
* `NONE` only suppresses the decal. The beam still counts its scorch marks, still fires its `GroundHitFX` on the same
rhythm and still reveals the shroud, so the timing of everything else is untouched.
* `SHADOW_SCORCH` was never reachable before, because the old random pick stopped at `SCORCH_4`. It is only used if you
name it.

### TornadoObjectName

* `TornadoObjectName = <object>` - (Creates this object at the beam ground point when the orbital
  beam appears, drags it along with the beam, ramps it down when the beam starts decaying, and
  destroys it when the beam dies or the cannon is sold or destroyed.)

The named object must carry a [TornadoUpdate](#tornadoupdate). The cannon supplies both ramp
durations from `WidthGrowTime` and holds the tornado until the orbital beam starts decaying.
`TotalFiringTime` therefore controls the hold through the beam's own schedule. The tornado's
`RampUpTime`, `FullStrengthTime` and `RampDownTime` apply only when it runs without a cannon.
An interrupted beam starts the tornado's fade at its current strength. Selling or destroying the
cannon releases every held unit immediately. An active tornado uses the extended cannon save
format even when retail-compatible saving is enabled, so the controller link survives loading.

## Model conditions

### UNMANNED

Set while an object is `DISABLED_UNMANNED` (pilot sniped, drone whose carrier died, or the
`NAMED_SET_UNMANNED_STATUS` script action) and cleared when it is manned again. A
`ConditionState = UNMANNED` block on a draw module can hide crew art, such as the gunner on a
technical, while the vehicle sits empty.

```ini
  Draw = W3DModelDraw ModuleTag_Gunner
    DefaultConditionState
      Model = GunnerMesh
    End
    ConditionState = UNMANNED
      Model = NONE
    End
  End
```

## New Behavior Modules

### TimeOfDayOverrideUpdate

Switches the map's time of day, piggybacking on an existing special power. Two modes:

* On the structure firing the power: switches at launch, for `Duration`.
* On an object the power creates, with `ActivateOnCreate`: switches when it is created and reverts
when it dies.

```
Behavior = TimeOfDayOverrideUpdate ModuleTag_TODOverride01
  SpecialPowerTemplate = SuperweaponScudStorm  ; optional, see below
  TimeOfDay            = NIGHT
  FallbackTimeOfDay    = AFTERNOON
  Duration             = 60000
End
```

Or on an object the power creates, where the effect itself decides how long the night lasts:

```
Behavior = TimeOfDayOverrideUpdate ModuleTag_TODOverride01
  TimeOfDay        = NIGHT
  ActivateOnCreate = Yes
End
```

* `SpecialPowerTemplate` - (Only react to this power. Leave it out and the module reacts to every
special power the object fires. Not used when `ActivateOnCreate` is set.)
* `TimeOfDay = NIGHT` - (`MORNING` | `AFTERNOON` | `EVENING` | `NIGHT`. What the world switches to.)
* `FallbackTimeOfDay = AFTERNOON` - (Revert target when the map already sits at `TimeOfDay`. Never
switched to.)
* `Duration = 0` - (In milliseconds. `0` makes the switch permanent, and firing again toggles it
back. Any other value reverts after that long. Ignored with `ActivateOnCreate`.)
* `ActivateOnCreate = No` - (Yes switches on creation and reverts on death, instead of on a special
power.)

A real time of day change: the map's night lighting, night model variants, ambient sounds, sky,
water and player indicator colours all come with it.

Notes:
* Without `ActivateOnCreate` the switch lands at **launch**, not impact.
* A map already at `TimeOfDay` is left alone.
* Global: every player and observer sees it.
* Overlapping holders share the change; it reverts when the last one lets go.
* Multiplayer and replay safe.
* A timed switch reverts if the firing structure dies.
* Runtime switches now relight trees, bibs, bridges and roads immediately; the debug time of day
hotkey benefits too, and refreshes player indicator colours.

### GrantTemporaryStealthBehavior

Grants stealth for a set time. It runs beside permanent stealth (`GrantStealthBehavior`, upgrades,
innate) and never removes it when it runs out. Two modes:

* On the object firing a special power: grants when the power fires.
* On an object an OCL or weapon creates, with `ActivateOnCreate`: grants once, on creation.

```
Behavior = GrantTemporaryStealthBehavior ModuleTag_TempStealth01
  SpecialPowerTemplate = SpecialAbilityCloak  ; optional, see below
  Duration             = 10000
  Radius               = 0
  KindOf               = INFANTRY VEHICLE
  ForbiddenKindOf      = AIRCRAFT
End
```

* `SpecialPowerTemplate` - (Only react to this power. Leave it out and the module reacts to every
special power the object fires. Not used when `ActivateOnCreate` is set.)
* `Duration = 0` - (In milliseconds. How long the stealth lasts. Required.)
* `Radius = 0` - (`0` grants to a single object. Any other value grants to every allied object
within that distance.)
* `KindOf` - (A receiver needs at least one of these. Default = everything.)
* `ForbiddenKindOf` - (A receiver may have none of these. Default = none.)
* `ActivateOnCreate = No` - (Yes grants on creation instead of on a special power.)

Who receives the stealth:

| Trigger | `Radius = 0` | `Radius > 0` |
| --- | --- | --- |
| Power without a target | the firing object | allies around the firing object |
| Power on an object | the target | allies around the target |
| Power on a location | nobody | allies around the location |
| `ActivateOnCreate` | the created object | allies around the created object |

A unit ability needs a special power, a button and a `SpecialAbility` module beside this one:

```
SpecialPower SpecialAbilityCloak
  Enum        = SPECIAL_CIA_INTELLIGENCE
  ReloadTime  = 30000
  PublicTimer = No
End

CommandButton Command_Cloak
  Command      = SPECIAL_POWER
  SpecialPower = SpecialAbilityCloak
  TextLabel    = CONTROLBAR:Cloak
  ButtonImage  = SUGPS02
End

; on the unit
Behavior = SpecialAbility ModuleTag_CloakPower
  SpecialPowerTemplate = SpecialAbilityCloak
End
Behavior = GrantTemporaryStealthBehavior ModuleTag_Cloak
  SpecialPowerTemplate = SpecialAbilityCloak
  Duration             = 10000
End
```

The engine decides by the power's `Enum` which kind of target it accepts. These work without an
update module of their own:

| Target | `Enum` | Button `Options` |
| --- | --- | --- |
| None | `SPECIAL_CIA_INTELLIGENCE`, `SPECIAL_COMMUNICATIONS_DOWNLOAD` | - |
| Allied object | `SPECIAL_TANKHUNTER_TNT_ATTACK` | `CONTEXTMODE_COMMAND NEED_TARGET_ALLY_OBJECT` |
| Location | `SPECIAL_RADAR_VAN_SCAN` | `CONTEXTMODE_COMMAND NEED_TARGET_POS` |

`SPECIAL_TANKHUNTER_TNT_ATTACK` accepts structures and ground vehicles only. A power on an object
has no range limit and lands the moment the player clicks.

A field dropped by an OCL, for an area effect that needs no special power on the caster:

```
Object TempStealthField
  KindOf = IMMOBILE UNATTACKABLE INERT
  Behavior = GrantTemporaryStealthBehavior ModuleTag_01
    ActivateOnCreate = Yes
    Duration         = 15000
    Radius           = 150
  End
  Behavior = LifetimeUpdate ModuleTag_02
    MinLifetime = 100
    MaxLifetime = 100
  End
End
```

Notes:
* A receiver needs a `StealthUpdate`, as with the GPS Scrambler. Its `StealthForbiddenConditions`
and `StealthDelay` still apply while the grant runs.
* Only allies of the granting object receive it, its own units included.
* A second grant never shortens a running one. The later end time wins.
* When the time is up, a unit that can stealth by other means stays stealthed.
* A portable structure or drone that joins a carrier running on temporary stealth alone gets the
time the carrier has left, not permanent stealth.
* A player order does not end it, unlike the supply center's `GrantTemporaryStealth`.
* Disguisers such as the Bomb Truck are skipped.
* List the module before the power's own update module (e.g. `SpecialAbilityUpdate`), which stops
later modules from seeing the power fire.

### ShieldFieldBehavior

A projected shield. A sphere around the object stops enemy fire at its surface. Projectiles that
fly into the sphere vanish where they cross it. Lasers, tracer guns and every other weapon without
a `ProjectileObject` stop at the surface, beam and all. This replaces the `FireWeaponUpdate` plus
anti-projectile radius weapon setup, which pulsed every few frames and let instant-hit weapons
through.

```
Behavior = ShieldFieldBehavior ModuleTag_Shield01
  Radius           = 200
  CoverageAngle    = 180          ; degrees each side of the facing; 180 covers all round
  BlockProjectiles = Yes
  BlockBeams       = Yes
  DamageSelf       = No
  HitFX            = FX_ShieldHit
  TriggeredBy      = Upgrade_Shield   ; optional, see below
End
```

* `Radius` - (Sphere radius around the object's position. Required.)
* `CoverageAngle = 180` - (Degrees each side of the object's facing. A shot is stopped only when
it comes from inside that arc, measured from the shield centre to the shooter, or to a projectile's
launch point. 180 covers the whole sphere, 45 gives a 90 degree front shield.)
* `BlockProjectiles = Yes` - (Stop missiles, shells and bombs.)
* `BlockBeams = Yes` - (Stop weapons without a `ProjectileObject`: lasers, beams, tracer guns.)
* `DamageSelf = No` - (`Yes` lands each stopped shot's `PrimaryDamage` on the object itself,
through its armor. With a `ShieldBody` that drains the shield health pool.)
* `HitFX` - (Played where a shot stops.)
* The upgrade keys `TriggeredBy`, `ConflictsWith`, `RequiresAllTriggers`, `RemovesUpgrades` and
`FXListUpgrade` work as in [FireWeaponUpdate](#upgrade-parameters). Without `TriggeredBy` the
field is always up.

Notes:
* Only fire crossing the surface from outside is stopped. A shooter inside the sphere hits as
usual, and its shots leave the sphere freely.
* Allies pass. Enemies and neutrals are stopped.
* A stopped projectile is removed without detonating and without running its die modules. A
stopped beam deals no damage to its target. With `DamageSelf = No` the shot is simply gone.
* The field is down while the object is dead, under construction or has its systems cut (EMP,
hack, no power, subdual, frozen, paralysed). Being held, carried or unmanned keeps it up.
* Projectiles are caught once per frame, one frame of flight ahead of the surface, so a fast one
stops at the surface and one that would hit its target in the same frame is still caught. A
projectile with no launch point on record is stopped as if it came from outside.
* With `TriggeredBy`, the scan sleeps until the upgrade lands.
* Weapons with `DamageDealtAtSelfPosition` are never stopped, nor is a projectile's own
detonation.
* The AI keeps firing at shielded targets, as it did with the weapon-based shields.
* The logic is deterministic and runs in game logic only, so it is multiplayer and replay safe.

### Teleport targeting

`ResetTargetingOnTeleport = Yes` makes other units drop their attack orders and turret targets
on each teleported unit. It applies to `TeleportSelfSpecialPower`, `TeleporterAIUpdate` and
`ChronoSphereUpdateModule`. The default is `No` for all three modules.

Set the field inside the teleport behavior block. For the chronosphere, it applies to every
object moved by the power. Units can acquire the teleported object again through normal targeting
at its destination. The movement teleporter keeps its own attack target.

This changes the simulation when enabled; all players and replays need the same INI.

### TeleportSelfSpecialPower

Special power update module that teleports its own object to a clicked ground position. This is the activated-ability counterpart to [TeleporterAIUpdate](Objects-&-Modules.md#teleporteraiupdate-new---experimental), which instead replaces all normal movement with teleporting; a unit with this module walks normally the rest of the time.

Pair it with a `SpecialPowerModule` (or `SpecialAbility`) that sets `UpdateModuleStartsAttack = Yes`, otherwise the power triggers itself and a rejected click still consumes the recharge. The `SpecialPower` entry needs `BehaviorEnum = SPECIAL_JUMPJET`, which supplies the position-only validation (no water, no cliffs).

```
Behavior = TeleportSelfSpecialPower ModuleTag_Teleport
  SpecialPowerTemplate = <SpecialPower entry>
  MaxTeleportRange = 0.0       ; furthest the object may teleport, 0 = unlimited
  TeleportDelay = 0            ; ms between activation and the teleport happening
  ResetTargetingOnTeleport = No ; Yes makes attackers lose this unit
  TeleportStartFX = <FXList>   ; FX at the position we left
  TeleportTargetFX = <FXList>  ; FX at the position we arrived at

  ; Recovery is off entirely unless RecoverDuration is set
  RecoverDuration = 0          ; ms the unit is immobilized after landing
  TeleportRecoverEndFX = <FXList>             ; FX when the recovery finishes
  TeleportRecoverSoundAmbient = <AudioEvent>  ; looped while recovering
  TeleportRecoverTint = <TintStatus>          ; color tint applied while recovering
  TeleportRecoverCondition = <ModelCondition> ; e.g. TELEPORT_RECOVER
  TeleportRecoverOpacityStart = 100%          ; opacity when the recovery starts
  TeleportRecoverOpacityEnd = 100%            ; opacity when the recovery ends
End
```

Notes:
* The destination is validated when the order is given and again when it fires, so a spot that becomes blocked during `TeleportDelay` cancels the teleport rather than stranding the unit.
* An unaffordable `Cost`, a dead/garrisoned/disabled caster, or an invalid destination all refuse the order without consuming the recharge.
* `RecoverDuration` uses `DISABLED_TELEPORT_RECOVER`, which genuinely immobilizes the unit. Because any disable pauses special power countdowns, the effective cooldown becomes `ReloadTime` + `RecoverDuration`.
* Do **not** set `KINDOF_TELEPORTER` on a unit using this module - that KindOf excludes units from group speed, leader selection and column formations, which would break normal squad movement.

### TornadoUpdate

Applies a tornado effect around the object: nearby units are dragged toward it, lifted, spun, and
damaged, and they fall when the effect fades. The strength follows a weak - strong - weak envelope,
so the tornado builds up, holds, and dies away like the particle uplink cannon beam does.

The module never moves its own object, so a tornado that wanders needs a `Locomotor` and an AI
module on the object like any other unit. A stationary one can be spawned by a weapon through
`ProjectileDetonationOCL` or `FireOCL` with a `CreateObject` nugget.

```
Behavior = TornadoUpdate ModuleTag_Tornado
  Radius = 120                    ; (required; how far out units are grabbed)
  RingRadius = 12                 ; (distance from the axis victims orbit at; 0 = a tenth of Radius)
  PullForce = 0.6                 ; (inward speed toward the centre, in distance per frame)
  LiftForce = 1.5                 ; (climb speed toward MaxLiftHeight, in height per frame)
  SpinForce = 6                   ; (orbit speed around the centre; a negative value orbits the other way)
  YawRate = 360                   ; (full-strength yaw in degrees per second; also drives pitch and roll)
  MaxLiftHeight = 40              ; (above this height over the tornado ground, lift stops, so victims hover)
  MaxVictimSpeed = 12             ; (speed cap relative to the tornado; 0 = uncapped)
  MassReference = 100             ; (heavier victims spin up more slowly during ramp-up; 0 = no scaling)
  ReleaseSpeed = 0                ; (horizontal release speed in a random direction; 0 drops straight down)
  RequiredKindOf = VEHICLE INFANTRY   ; (optional; a victim must be at least one of these)
  ForbiddenKindOf = AIRCRAFT      ; (optional; a victim must be none of these)
  AffectsTargets = ENEMIES NEUTRALS   ; (default = ALLIES ENEMIES NEUTRALS)
  AffectAirborne = No             ; (default = No)
  IgnoreVictimGeometry = No       ; (default = No; Yes stops held victims shoving each other apart)
  RampUpTime = 2000               ; (ms to reach full strength)
  FullStrengthTime = 6000         ; (ms at full strength; 0 = until the object dies or a controller ends it)
  RampDownTime = 2000             ; (ms to fade; victims are released at random times during this interval)
  DamagePerSecond = 20            ; (damage rate at full strength)
  DamageRadius = 0                ; (default = 0 damages held victims; positive values search that radius)
  DamagePulseDelay = 500          ; (ms between damage pulses; 0 disables damage)
  DamageType = EXPLOSION          ; (default = EXPLOSION)
  DeathType = EXPLODED            ; (default = EXPLODED)
  KillObjectWhenDone = No         ; (default = No; Yes destroys the object once the ramp down finishes)
End
```
**Notes:**
- A victim needs a `PhysicsBehavior`. `ShockResistance` slows pull, lift and rotation. Full
  resistance prevents capture. `MassReference` slows heavy units' rotation while strength builds.
- Structures, immobile objects, projectiles and units inside transports are skipped.
- `Radius` controls initial capture. Captured units remain held outside that radius and at
  `MaxLiftHeight`. The height stays fixed during decay until each unit is released.
- Orbit speed, yaw, pitch and roll build and fade with strength. Pitch and roll vary by unit and
  time at up to a quarter of the yaw rate.
- Each victim gets a release time within `RampDownTime`. Release order and directions survive
  save/load. The tornado stops capturing new units when decay begins.
- Enemy victims receive `NO_ATTACK` and `UNSELECTABLE` while held. Release clears the flags the
  tornado added and preserves flags that were already set. Overlapping tornadoes give each
  victim to one controller.
- Airborne targets are skipped unless `AffectAirborne = Yes`. Aircraft with a fixed flight height
  overwrite their own height every frame, so lifting them does not work well.
- Falling damage on release is the victim's own, from `MinFallHeightForDamage` and
  `FallHeightDamageFactor` on its `PhysicsBehavior`. It only counts a steep descent, which is why
  `ReleaseSpeed = 0` is the reliable way to splat units.
- `IgnoreVictimGeometry` suppresses collision push-apart between held victims. Because they are
  all steered onto the same ring they overlap constantly, and wide units can be shoved around
  faster than the orbit settles, which reads as juddering. It is restored on release.
- `KillObjectWhenDone = Yes` destroys the object after decay. A `LifetimeUpdate` or OCL lifetime
  can also destroy the object and release its victims. A standalone tornado with
  `FullStrengthTime = 0` and no `LifetimeUpdate` starts fading after 30 seconds at full strength.

### ThermiteBehavior

Goes on a projectile object. When the projectile detonates, the projectile itself stays alive as a
hidden burn instead of being destroyed. If the projectile hit an object, the burn sticks to that
object and follows it around. If the projectile detonated on the ground, the burn stays where it
landed. Every time `Weapon` is ready it fires at the stuck object, or at the ground spot, until the
lifetime runs out. Damage, radius, pulse rate and FX all come from that weapon.

The module is meant for an upgrade such as thermite shells, so the upgrade fields gate it. Without
the trigger the projectile detonates and dies as usual.

```
Behavior = ThermiteBehavior ModuleTag_Thermite
  Weapon = FlameTankThermiteBurn  ; (required; fired on every pulse, its FireFX is the burn visual)
  MinLifetime = 3000              ; (ms; shortest burn)
  MaxLifetime = 5000              ; (ms; longest burn, picked once per projectile)
  TriggeredBy = Upgrade_ChinaThermiteShells   ; (optional; none = always on)
  ConflictsWith = Upgrade_Something           ; (optional)
  RequiresAllTriggers = No        ; (default = No)
End
```
**Notes:**
- Works with `DumbProjectileBehavior`, `MissileAIUpdate` and `FreeFallProjectileBehavior`. The
  normal `ProjectileDetonationFX` and damage still happen first, and only then does the burn start.
- The projectile is the burn, so `DetonateCallsKill` and the projectile's own death FX are skipped
  when the thermite ignites.
- `Weapon` is fired from the projectile with the stuck object as the target, so it must have no
  `MinimumAttackRange`. `DelayBetweenShots` sets the pulse rate. On the ground it is fired at the
  burn position, so give it a `PrimaryDamageRadius`.
- The stuck object is the one the projectile collided with, not the one it was aimed at. Mobile
  victims are followed at their center. Structures and the ground keep the impact point.
- A victim that dies or enters a transport or tunnel drops the burn to the ground under it, where
  it keeps burning for the rest of its lifetime.
- The upgrade check looks at the firing player, the launcher object and the projectile, so both
  player upgrades and object upgrades on the launcher work as `TriggeredBy`.

### PoweredBehavior

Controls what an object does while its owner is out of power. Without the module, a `KINDOF_POWERED`
object gets the underpowered disable and shuts down completely. With the module the object stays
active, and only the listed effects apply. The module replaces the `KINDOF_POWERED` disable on that
object, so abilities and command buttons keep working.

```
Behavior = PoweredBehavior ModuleTag_Powered
  IsMobile = No         ; (default = Yes; No = cannot move while out of power)
  DisableWeapon = Yes   ; (default = No; Yes = cannot attack while out of power)
  MovePenalty = 50%     ; (default = 0%; move speed lost while out of power)
  LiftPenalty = 20%     ; (default = 0%; lift lost while out of power, hovering units only)
  DamagePenalty = 30%   ; (default = 0%; weapon damage lost while out of power)
  RateOfFirePenalty = 50%  ; (default = 0%; rate of fire lost while out of power)
  RangePenalty = 25%    ; (default = 0%; weapon range lost while out of power)
  Icon = LowPower       ; (optional; name of an Animation block, shown while out of power)
End
```
**Notes:**
- `MovePenalty` of 100% or more acts like `IsMobile = No`.
- `DamagePenalty`, `RateOfFirePenalty` or `RangePenalty` of 100% or more acts like `DisableWeapon = Yes`.
- The weapon penalties multiply the result of any `WeaponBonus` conditions. A 50% `RateOfFirePenalty`
  doubles the delay between shots and the clip reload time.
- A projectile keeps the penalties of its launcher at the moment it was fired.
- `RangePenalty` does not lower `MinimumAttackRange`. Keep the reduced range above it, or the
  weapon cannot fire while out of power.
- The weapon effects cover the object's own weapon set. `DisableWeapon` does not stop weapons
  fired by other modules, such as `FireWeaponUpdate`, and a penalty of 100% or more cuts those
  weapons to 1%. `PointDefenseLaserUpdate` ignores `RateOfFirePenalty` and `RangePenalty`. Slaves
  of a `KINDOF_SPAWNS_ARE_THE_WEAPONS` object fire normally.
- `MovePenalty` never touches lift, so helicopters keep hovering. `LiftPenalty` lowers lift on
  its own. Once lift no longer beats gravity the unit sinks, so keep it small. 100% is ignored.
- `Icon` names an `Animation` block from `animation2d.ini`, like the stock `Disabled` one. It sits
  above the health bar, beside the disabled icon when both show. See
  [Animation2D.ini](contraZH-Animation2D.md) for the single-image `Texture` form.
- Losing power with `IsMobile = No` stops the current move order. Attack orders still work, so a
  turreted unit keeps firing from where it stands.
- The effects follow the owner. A captured object takes the new owner's power state.
- No disabled tint is shown; use `Icon` for feedback.

### GasBehavior

Reacts to the new `GAS` damage type. A gassed object takes damage over time like poison and can
also be slowed, disoriented and made to hallucinate. Every effect is optional.

```
Behavior = GasBehavior ModuleTag_Gas01
  AffectAirborne                 = No
  GasDamageInterval              = 500
  GasDuration                    = 4000
  MoveSpeedReduction             = 30%
  MoveSpeedRequiredAffectKindOf  = INFANTRY
  DisorientKindOf                = INFANTRY
  DisorientRadius                = 60
  DisorientInterval              = 1500
  HallucinationEffectInterval    = 1000
  HallucinationDamageCap         = MaxHealth * 2
  HallucinationHealRate          = 500
  HallucinationHealAmount        = MaxHealth / 10
  TriggeredBy                    = Upgrade_ChinaHallucinationGas
  FXListUpgrade                  = FX_HallucinationStart
End
```

A weapon sets it off with `DamageType = GAS`. Armor scales it with `Armor = GAS 100%`.

Who is affected:

* `RequiredAffectKindOf` - (Default = everything.)
* `RequiresAllKindOfs = No` - (Yes needs every `RequiredAffectKindOf`, No needs one of them.)
* `ForbiddenAffectKindOf` - (Default = none.)
* `AffectsTargets = ENEMIES NEUTRALS` - (The gassed object's relationship to the gas owner. Add
`ALLIES` to let gas work on the owner's own side.)
* `AffectAirborne = Yes` - (No ignores objects that count as airborne targets.)

Damage over time:

* `GasDamageInterval = 0` - (In milliseconds. The hit's damage repeats this often. `0` disables
the repeats.)
* `GasDuration = 0` - (In milliseconds. How long the gas lasts after the last hit. The repeats,
the slow and disorientation all end with it.)

Slow:

* `MoveSpeedReduction = 0%` - (Speed loss while gassed. `30%` moves at 70% speed.)
* `MoveSpeedRequiredAffectKindOf` - (Default = everything gassed.)
* `MoveSpeedRequiresAllKindOfs = No`
* `MoveSpeedForbiddenAffectKindOf` - (Default = none.)

Disorientation:

* `DisorientKindOf = INFANTRY` - (Disoriented objects need one of these.)
* `DisorientRadius = 50` - (Each move goes half to all of this distance in a random direction.)
* `DisorientInterval = 0` - (In milliseconds. A new random move this often. `0` disables
disorientation.)

Hallucination works like subdual damage. Each qualifying hit adds its damage to a hallucination
pool; the unit hallucinates while the pool is at or above its max health, and stops once the pool
drains below it.

* `HallucinationEffectInterval = 1000` - (In milliseconds. How often a new target is picked. `0`
picks one target when the hallucination starts.)
* `HallucinationDamageCap = 0` - (Most the pool can hold. `0` disables hallucination. Takes the
[MaxHealth forms](contraZH-GameData.md#subdual-damage-defaults): `MaxHealth * 2`, `MaxHealth / 4`,
`MaxHealth` or a plain number. Below `MaxHealth` the unit can never hallucinate.)
* `HallucinationHealRate = 0` - (In milliseconds. The pool drains this often, starting this long
after the last hit. `0` never drains, so a hallucination never ends.)
* `HallucinationHealAmount = 0` - (Drained per step. Takes the same MaxHealth forms.)
* `TriggeredBy` - (Upgrades the attacker needs. Leave it out and every gas hit can cause
hallucination.)
* `ConflictsWith` - (Upgrades that stop it when the attacker has any of them.)
* `RequiresAllTriggers = No` - (Yes needs every `TriggeredBy`, No needs one of them.)
* `FXListUpgrade` - (Plays on the gassed object each time the pool reaches max health.)

Notes:
* The module reacts only when a hit lowers health. A `GAS` hit that armor cuts to zero does
nothing.
* Each repeat deals the first hit's damage after armor, and armor applies again to the repeat, as
with poison.
* Gas never stacks. A new hit refreshes the damage amount and `GasDuration`, and never delays a
repeat that is already due.
* An owner that no longer exists still counts through its player. Gas with no owner at all counts
as an enemy's.
* The slow is applied once per gassing and lifted when the gas ends. It multiplies with other speed
changes such as battle plans.
* Hallucination needs an armed unit. It force-attacks the nearest unit of any side within its
longest weapon range, structures excepted. With nothing in range it waits for the next pick.
Hold Fire does not stop it.
* Only hits that pass `TriggeredBy` and `ConflictsWith` fill the pool, and only the damage left
after armor counts. The `GasDamageInterval` repeats do not add to it.
* `MaxHealth` is read when it is used, so veterancy health bonuses raise the threshold, as with
subdual.
* A hallucinating gas unit that shoots its own side passes the gas on only when `AffectsTargets`
lists `ALLIES`, so by default hallucination cannot spread through a squad.
* A hallucinating unit that is busy attacking does not wander off when disorientation is also on.
* The upgrade check looks at the attacker, like the poison tiers. An upgrade counts when the
attacking player has researched it or the attacking object carries it.
* Player orders and abilities are ignored while an object is disoriented or hallucinating.
Computer players and map scripts can still order it, and the next move or pick takes over again.
* A unit goes idle when the last of these effects ends.
* Units inside a transport or building are not moved and can still be evacuated.
* A disoriented unit only moves to spots its locomotor can reach. With none nearby it stays put
until the next move.
* Healing does not cure gas, unlike poison. Every effect runs until its duration ends.
* Multiplayer and replay safe.

## Subdual Jamming

Jamming damage that accumulates on the body like subdual damage, interacts with armor, and disables
the unit when the threshold is reached. Healing ticks remove the damage over time.

### Jamming New Damage Types

Two new entries in `DamageType`:

* `SUBDUAL_JAMMING` - jamming damage adjusted by armor coefficients
* `SUBDUAL_JAMMING_UNRESISTABLE` - bypasses armor entirely (used internally for healing ticks)

Neither type reduces health. Set these on a weapon's `DamageType` field.

### Jam Effect

When jamming damage reaches the unit's max health, the unit gains `UNSELECTABLE` object status, drops
out of the player's selection, and its passengers are ordered to idle. The status clears once the
jamming damage heals below the threshold.

`UNSELECTABLE` on its own only blocks a new selection click. Dropping the unit from the current
selection is something jamming asks for specifically, so other sources of the status - a slaved drone,
a docked unit, a sold building - leave the selection alone.

Jamming is independent of the `DISABLED_*` states: it neither waits for them nor ends them, and a
disabled unit stays disabled through a jam. On unjam it only clears `UNSELECTABLE` if the jam was
what set it, so a docked, slaved, or status-held unit keeps that state.

### Jamming ActiveBody Fields

```
Body = ActiveBody ModuleTag_Body
  ; ...existing ActiveBody fields...
  JammingDamageCap  = 100.0   ; max jamming damage this unit can accumulate (0 = immune)
  JammingDamageHealRate   = 500 ; milliseconds between each healing tick
  JammingDamageHealAmount = 5.0 ; jamming damage removed per tick
End
```

A unit with `JammingDamageCap = 0` ignores `SUBDUAL_JAMMING` damage entirely.
The unit becomes jammed when accumulated jamming damage reaches `MaxHealth`. Healing begins
automatically after the first hit.

#### Recovery timing

Every hit resets the heal countdown, so the first healing tick lands one full `HealRate` after
the last hit. Each tick then removes `HealAmount` until the stored damage reaches zero. The unit
unjams as soon as the stored damage drops below `MaxHealth`; the overlay only disappears once it
reaches zero. `JammingDamageCap` bounds the stored damage and therefore the longest recovery.

```
time to unjam        = HealRate * ceil((stored - MaxHealth) / HealAmount)
time to clear overlay = HealRate * ceil(stored / HealAmount)
```

With the defaults in [Subdual Damage Defaults](contraZH-GameData.md#subdual-damage-defaults)
(cap `MaxHealth * 2`, heal `MaxHealth / 16.25` every 500 ms) a fully capped unit unjams after
8.5 s and its overlay clears after 16.5 s. Subdual damage heals the same way, but its blue tint
is a flag rather than a level and clears on the first healing tick.

Any of these keys, and the `SubdualDamage*` trio, may be omitted. An omitted key takes its value
from the matching `SubdualDamageDefaults` block in GameData.ini, and is 0 if no block matches.
Cap and HealAmount keys accept `MaxHealth * 2` or `MaxHealth / 16.25` as well as a plain number,
evaluated against the unit's current max health. ActiveBody also accepts `ChronoDamageHealRate`
and `ChronoDamageHealAmount` in the same forms, which override the global chrono keys for that
unit. See [Subdual Damage Defaults](contraZH-GameData.md#subdual-damage-defaults).

### Jamming Armor

`SUBDUAL_JAMMING` interacts with armor normally. Define coefficients in `Armor.ini`:

```
Armor ChinaTankArmor
  Armor = SUBDUAL_JAMMING 200%   ; takes twice as long to jam
End
```

`SUBDUAL_JAMMING_UNRESISTABLE` bypasses armor (like `UNRESISTABLE` and `SUBDUAL_UNRESISTABLE`).

### Jamming Overlay Texture

A scrolling texture can be drawn over units taking jamming damage. Its opacity is the stored
jamming damage divided by `MaxHealth`, clamped to 1, so it fades in as the unit is jammed, holds
full strength while damage sits above `MaxHealth`, and fades out as it heals (see
[Recovery timing](#recovery-timing)). Configured globally in `GameData.ini`:

```
JammingOverlayTexture  = JammingFX   ; texture name; omit or leave empty to disable
JammingOverlayScrollU  = 0.5         ; horizontal scroll per second
JammingOverlayScrollV  = 0.0         ; vertical scroll per second
JammingOverlayScale    = 1.0         ; UV tiling; >1 repeats the texture more densely
JammingOverlayColor    = R:255 G:255 B:255  ; tint multiplied into the texture
JammingOverlayAdditive = Yes         ; Yes = additive glow, No = alpha blend
```

The effect is off by default. `Yes` suits an electrical shimmer; `No` suits an opaque layer
such as frost, and needs the texture to carry an alpha channel.

Note: a unit that is both jammed and stealth-detected shows the jamming overlay only. The
engine carries one opacity value per render call, so the two effects cannot be layered with
independent strengths. The [frozen overlay](#subdual-frozen) stacks with this one and shares
that single value.

### Jamming Icon

A jammed unit shows its own icon beside the health bar, next to the disabled icon when both
apply. Define an `Animation Jammed` block in `Animation2D.ini`; the icon is not drawn until this
exists. The single-image `Texture` form is the natural fit — see
[Animation2D.ini](contraZH-Animation2D.md):

```
Animation Jammed
  NumberImages   = 1
  Texture        = jammer.tga
  AnimationMode  = ONCE
  AnimationDelay = 0
End
```

### Jamming Per-Unit Sounds

Units can define custom jam/unjam sounds via their `UnitSpecificSounds` block:

```
Object SomeUnit
  UnitSpecificSounds
    SoundJammed   = JammedSoundEvent
    SoundUnjammed = UnjammedSoundEvent
  End
End
```

If no per-unit sound is defined, the unit falls back to the global jam sounds in
`MiscAudio.ini`, and is silent if those are unset too:

```
UnitJammed   = JammedSoundEvent
UnitUnjammed = UnjammedSoundEvent
```

Jamming never borrows the building-disabled or vehicle-disabled sounds, so an EMP'd unit that
is also jammed plays two distinct cues rather than a repeated one.

### Jamming Example Weapon

```
Weapon JammerGun
  PrimaryDamage     = 50.0
  PrimaryDamageRadius = 0.0
  DamageType        = SUBDUAL_JAMMING
  DeathType         = NORMAL
  ; ...other weapon fields...
End
```

## Subdual Frozen

Frozen damage is a third subdual pool beside retail subdual and [jamming](#subdual-jamming). It
accumulates on the body, interacts with armor, and disables the unit the way retail subdual does
once it reaches the threshold. Healing ticks remove the damage over time. The three pools are
independent: each has its own counter, cap, heal rate and effect, and a unit can be subdued,
jammed and frozen at the same time.

### Frozen New Damage Types

Two new entries in `DamageType`:

* `SUBDUAL_FROZEN` - frozen damage adjusted by armor coefficients
* `SUBDUAL_FROZEN_UNRESISTABLE` - bypasses armor entirely (used internally for healing ticks)

Neither type reduces health. Set these on a weapon's `DamageType` field.

### Freeze Effect

When frozen damage reaches the unit's max health, the unit gains the `DISABLED_FROZEN` disabled
type and the `FROZEN` condition state, and its passengers are ordered to idle. Both clear once the
frozen damage heals below the threshold.

`DISABLED_FROZEN` is checked everywhere the game checks `DISABLED_SUBDUED`, so a frozen unit
behaves exactly like a subdued one: it cannot move, fire, animate, sell, evacuate or auto-target,
transports refuse to load or unload it, a frozen stinger site silences its soldiers, and its
command buttons grey out. A unit that is both frozen and EMP'd, subdued or hacked stays disabled
until the last of those clears.

The `FROZEN` condition state lets art react. A `ConditionState = FROZEN` block on a draw module can
swap the model or play an animation for as long as the unit is frozen.

A frozen unit does not take the dark-gray disabled tint that EMP and subdual apply, so the
frozen overlay and any `FROZEN` art carry the look on their own.

Projectiles take frozen damage but are never disabled by it; only the sound plays.

### Frozen ActiveBody Fields

```
Body = ActiveBody ModuleTag_Body
  ; ...existing ActiveBody fields...
  FrozenDamageCap        = 100.0 ; max frozen damage this unit can accumulate (0 = immune)
  FrozenDamageHealRate   = 500   ; milliseconds between each healing tick
  FrozenDamageHealAmount = 5.0   ; frozen damage removed per tick
End
```

A unit with `FrozenDamageCap = 0` ignores `SUBDUAL_FROZEN` damage entirely. The unit freezes when
accumulated frozen damage reaches `MaxHealth`. Healing begins automatically after the first hit,
with the same [recovery timing](#recovery-timing) as jamming.

Any of these keys may be omitted. An omitted key takes its value from the matching
`SubdualDamageDefaults` block in GameData.ini, and is 0 if no block matches. Cap and HealAmount
keys accept `MaxHealth * 2` or `MaxHealth / 16.25` as well as a plain number. See
[Subdual Damage Defaults](contraZH-GameData.md#subdual-damage-defaults).

### Frozen Armor

`SUBDUAL_FROZEN` interacts with armor normally. Define coefficients in `Armor.ini`:

```
Armor ChinaTankArmor
  Armor = SUBDUAL_FROZEN 200%   ; takes twice as long to freeze
End
```

`SUBDUAL_FROZEN_UNRESISTABLE` bypasses armor (like `UNRESISTABLE` and `SUBDUAL_UNRESISTABLE`).

### Frozen Overlay Texture

A texture can be drawn over units taking frozen damage, with the same fade rule as the jamming
overlay: opacity is the stored frozen damage divided by `MaxHealth`, clamped to 1. Configured
globally in `GameData.ini` with the same keys as the jamming overlay:

```
FrozenOverlayTexture  = FrostFX.tga  ; texture name; omit or leave empty to disable
FrozenOverlayScrollU  = 0.0          ; horizontal scroll per second
FrozenOverlayScrollV  = 0.0          ; vertical scroll per second
FrozenOverlayScale    = 1.0          ; UV tiling; >1 repeats the texture more densely
FrozenOverlayColor    = R:255 G:255 B:255  ; tint multiplied into the texture
FrozenOverlayAdditive = No           ; Yes = additive glow, No = alpha blend
```

The effect is off by default. The defaults differ from jamming: no scroll and alpha blend, which
suits a static frost layer with an alpha channel.

The jamming and frozen overlays stack, so a unit that is both jammed and frozen draws both
textures. The engine carries one opacity value per render call, so both passes use the stronger
of the two intensities and fade together. A unit that is also stealth-detected shows the subdual
overlays only.

### Frozen Icon

A frozen unit shows its own icon beside the health bar, after the disabled and jammed icons when
those apply. Define an `Animation Frozen` block in `Animation2D.ini`; the icon is not drawn until
this exists. See [Animation2D.ini](contraZH-Animation2D.md):

```
Animation Frozen
  NumberImages   = 1
  Texture        = frozen.tga
  AnimationMode  = ONCE
  AnimationDelay = 0
End
```

### Frozen Per-Unit Sounds

Units can define custom freeze/thaw sounds via their `UnitSpecificSounds` block:

```
Object SomeUnit
  UnitSpecificSounds
    SoundFrozen   = FrozenSoundEvent
    SoundUnfrozen = UnfrozenSoundEvent
  End
End
```

If no per-unit sound is defined, the unit falls back to the global sounds in `MiscAudio.ini`,
and is silent if those are unset too:

```
UnitFrozen   = FrozenSoundEvent
UnitUnfrozen = UnfrozenSoundEvent
```

Freezing never borrows the building-disabled or vehicle-disabled sounds.

### Frozen Example Weapon

```
Weapon FreezeGun
  PrimaryDamage     = 50.0
  PrimaryDamageRadius = 0.0
  DamageType        = SUBDUAL_FROZEN
  DeathType         = NORMAL
  ; ...other weapon fields...
End
```
