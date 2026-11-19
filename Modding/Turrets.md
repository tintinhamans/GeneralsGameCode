# A second gun on a turret

Zero Hour aims a turret by turning two bones that the model's INI state names: `Turret` turns it and `TurretPitch`
raises it (`AltTurret` and `AltTurretPitch` for a second turret). There is one bone each, so a turret that carries two
guns can aim only one of them. The other stays facing forward.

In this build, **a bone named as a turret bone plus `_2` turns with it.** Nothing goes in the INI.

---

## 1. In the model (W3D)

Name the second gun's bones after the first gun's, plus `_2`:

| INI | first gun's bone | second gun's bone |
|---|---|---|
| `Turret = TURRET01` | `TURRET01` (turns) | `TURRET01_2` |
| `TurretPitch = TURRETEL` | `TURRETEL` (pitches) | `TURRETEL_2` |

- **Each twin turns by its turret bone's angle, about its own pivot.** Put the twin's pivot where the second gun is
  mounted: the point it turns about.
- **Give the twin the same axes as its turret bone in the rest pose.** The turn is about each bone's own Z axis and
  the pitch about its own Y axis. A twin with other axes turns the wrong way.
- **Do not hang a twin under its turret bone** in the hierarchy, or it turns twice: once with its parent, once by
  itself. Hang it where the first gun's bone hangs. A pitch twin goes under the turn twin, as `TURRETEL` goes under
  `TURRET01`.
- The second gun's barrels, muzzles and flashes go under its twin, so they turn with it.
- It works for both turrets (`Turret` / `TurretPitch` and `AltTurret` / `AltTurretPitch`), for the turn and the pitch
  each on its own. There is one twin per bone: `_2` only, no `_3`.

## 2. Firing

The game fires a weapon's barrels in number order: `Muzzle` in the INI means `MUZZLE01`, `MUZZLE02` and so on. Number
them across both guns, for example `MUZZLE01` on the left gun, `MUZZLE02` on the right, `MUZZLE03` on the left, so the
guns fire in turn.

- **Muzzle flashes and fire effects** (`WeaponMuzzleFlash`, `FireFX`, tracers) show at the barrel itself, wherever the
  twin points.
- **Shots that fly** (a weapon with a `ProjectileObject`: missiles, shells) start where the barrel would be if it sat
  on the first gun. The game turns every barrel about the turret bones the INI names; it does not know about the twin.
  When the turret turns, a projectile from the twin's barrel leaves away from its muzzle. The gap grows with the
  distance between the two guns' pivots and with the angle. Put a projectile weapon's barrels on the first gun.
  Weapons that hit at once (`ProjectileObject = NONE`: machine guns, gatlings) are not affected.

## 3. Compatibility

- **Older builds:** a model with a `_2` bone loads in the 2003 game and other builds. There the twin stays still.
- **Saves, replays, multiplayer:** unchanged. Only the drawing turns the twin; the game's logic is the same.
- No shipped model has such a bone, so the 2003 units look as they always have.

## Generals 2

Generals 2 turns several guns from one aim. We built this for it and tested it internally on the China Helix's gatling
upgrade (two side guns: `TURRET01_2` and `TURRETEL_2`), converted from Generals 2's air transport with our own tool.
Those models and the tool are not part of this repository.
