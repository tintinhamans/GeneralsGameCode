# Weapon.ini

## RequiresTransportOccupants

`RequiresTransportOccupants = Yes` makes a weapon usable only while its source contains at least
one passenger. The default is `No`. Works in `WeaponExtend`.

* The weapon button is disabled while the transport is empty.
* Target checks, weapon selection, and firing reject an empty transport.
* Loading a passenger enables the weapon subject to its usual shot and reload delays.
* Pair this with `CreateObject` and `UseTransportOccupants = Yes` in the weapon's `FireOCL`.
* Cargo does not replace clip ammunition. Use `ClipSize = 0` and `AutoReloadsClip = Yes` for cargo-only firing.

Changes simulation when enabled. [Issue #126](https://github.com/triatomic/contraZH/issues/126).

## LineOfSightGraceHeight

* `LineOfSightGraceHeight = 0` - (Default. Terrain that rises this many feet above the firing line
does not block the shot.)

Units with `ATTACK_NEEDS_LINE_OF_SIGHT` only fire when the terrain between them and the target stays
below the firing line. The line runs from the top of the shooter to the target's centre, or to the
ground for a force-attacked spot, so long-range shots graze the terrain near the target and small
bumps block them. The unit then drives closer to fire. A grace height lets terrain rise that far
above the line before it counts as blocking.

```
Weapon EmperorSniperGun
  AttackRange            = 325.0
  LineOfSightGraceHeight = 8.0
End
```

Notes:
* Only units with `ATTACK_NEEDS_LINE_OF_SIGHT` test terrain before an attack, so the key does nothing
for others there. Immobile units skip that test and ignore the key.
* The current weapon's grace also applies when a unit scans for enemies it can see.
* A negative value is an INI error.
* A hill taller than the grace height still blocks the shot.
* Works in `WeaponExtend`.

Changes when units fire; affects replays when set.
([#120](https://github.com/triatomic/contraZH/issues/120))

## Gradual Clip Reload

Refill a clip one round at a time instead of all at once, so a weapon that fired only part of its clip is back
in action sooner. `AutoReloadsClip` accepts a new value for this; the retail values are unchanged.

* `AutoReloadsClip = YES` - (Retail values: `YES` refills the whole clip in `ClipReloadTime` once it runs empty;
`NO` never refills it; `RETURN_TO_BASE` refills only after landing at an airfield. New: `GRADUAL` loads a single
round every `ClipReloadTime / ClipSize`, up to a full clip.)
* `ClipReloadDelay = 0` - (`GRADUAL` only. Quiet time owed after the last shot before any round starts loading.
Defaults to 0, which still waits out one shot cycle. Ignored by the other reload types.)

With `GRADUAL`, rounds only load while the weapon is not attacking: a round cannot even begin until the weapon has
been quiet for `ClipReloadDelay`, so a weapon working through a target gains nothing between its shots however slow
its rate of fire, and the clip only refills once the firing stops. `ClipReloadDelay` never falls below one shot
cycle, so leaving it out still keeps a burst from feeding itself. When the clip runs empty the weapon waits for one
round and then fires again, rather than waiting out the whole clip, and it keeps waiting one round at a time for as
long as it keeps shooting. There is no return-to-base prerequisite, so aircraft with a `GRADUAL` weapon do not fly
home to rearm. Rate-of-fire bonuses scale the round the same way they scale `ClipReloadTime`, and a bonus that
changes mid-round carries the progress across instead of restarting it.

Notes:
- `AutoReloadWhenIdle` is ignored on a `GRADUAL` weapon. The clip already refills on its own, so a forced idle
reload would only cut the round timer short.
- `ClipSize = 0` means an unlimited clip and has no rounds to count, so `GRADUAL` behaves exactly like `YES`.
- `ClipReloadTime / ClipSize` is the time per round once loading starts, so a clip empties in `ClipSize` shots
whatever the rate of fire, and tops back up in about `ClipReloadDelay` plus `ClipReloadTime` of quiet.
- While the clip is empty and waiting for its first round the weapon reports no ammo, so the reload animation and
the command button clock run for that one round. A partly filled clip shows its real count on the ammo pips.
- `WeaponClipShared` is not supported with `GRADUAL`: a shared clip is decremented from another weapon slot, which
has no firing object to scale the round with.
- `WeaponReloadSharedAcrossSets` carries the round timer only when both weapon sets use `GRADUAL`; mixing it with a
non-gradual weapon drops the timer, and it restarts on the next shot.
- Modules that derive a shot index from the clip (`FireWeaponAdvancedUpdate`, `KodiakUpdate` scatter patterns)
assume a clip that only drains, so their sequence restarts as rounds come back.
