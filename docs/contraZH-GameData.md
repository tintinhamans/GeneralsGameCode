# GameData.ini

## Guard target visibility

`GuardRequiresVisibility` defaults to `No`, preserving guard mode's ability to acquire
unseen targets. Set it to `Yes` inside the `GameData` block in `GameData.ini` to require
new guard targets to be visible to the guarding player, including through shared vision.
Fogged and unexplored targets are excluded, including shared team targets.

```ini
  GuardRequiresVisibility = Yes
```

The setting applies to guard-position, guard-object, guard-area and tunnel network guard
target searches.
Existing attacks and retaliation keep their normal behavior. Enabling this setting changes
the simulation, so multiplayer clients and replay playback need matching values.

## Headlight aim

`HeadlightPerConeAim` defaults to `Yes`, so each cone of a `HEADLIGHT` mesh aims in its own direction.
Set it to `No` to use the mesh's shared direction. The game reads this setting at launch.
See [Headlights](dx9-Environment.md#headlights) for the other headlight settings.

## Path tracing light radius and brightness

These settings control the light cast by particles, lasers and emissive meshes while
the Graphics menu's `Path tracing` option is enabled. They affect drawing only.

| Key | Default | Effect |
| --- | --- | --- |
| `PathTracingLightMaxRadius` | `25.0` | Caps light radius in world units. Smaller lights keep their original radius; `0` means no cap. |
| `PathTracingLightBrightness` | `0.75` | Multiplies emitted light. `0.25` gives one quarter of the brightness. |

Zero or negative brightness disables this light spill. A negative radius cap means no cap.
The cap and brightness are independent. These keys do not change the emitting sprites,
lit windows or the legacy laser ground glow itself.

Start with these values to limit large light pools and reduce brightness. Add the keys inside the
existing `GameData` block in `GameData.ini`.

```ini
  PathTracingLightMaxRadius = 30.0
  PathTracingLightBrightness = 0.25
```

Cheat builds reload changes to these keys while a map runs. Other builds read them
at launch. A radius cap of `0` and brightness of `1.0` preserve the original appearance.

## Fluid defaults

`FluidWarp`, `FluidNoiseSize`, `FluidSpeed`, `FluidBody`, `FluidBubbles`, `FluidBubbleSize`,
`FluidEdge` and `FluidGlow` set the defaults for particle systems with `FluidShader = Yes`.
Each system can override them. See [Fluid shading](contraZH-ParticleSystems.md#fluid-shading)
for defaults, units and acid, toxin and radiation examples.

## Storm defaults

`SandStorm...` keys set defaults for `Type = SAND`; `SnowStorm...` keys set defaults for
`Type = SNOW`. For example, `SnowStormGrainCount` supplies an omitted `GrainCount` in an object's
`W3DStormDraw` module or an FXList's `Storm` block. Explicit block values override these defaults.
See [Storms](dx9-Environment.md#key-names-and-overrides) for all 18 keys and examples.

## Unknown keys and blocks are skipped

An INI key or block the game does not know is logged to `DebugLogFile.txt` (player builds need `-log`) and skipped instead of
crashing the game or WorldBuilder. Retail asserted on an unknown key in debug builds and threw on
an unknown block in every build, so a mod INI written for a newer exe could not load on an older
one. An unknown block is skipped up to its `End`, which also covers a new key that opens a nested
block. A known key with a bad value still fails the load.

## Subdual Damage Defaults

Global defaults for the subdual, jamming, frozen and chrono values that otherwise repeat on every
ActiveBody. Any number of blocks may appear; each one can be restricted to a KindOf and can
exclude a KindOf.

```
SubdualDamageDefaults
  SubdualDamageCap        = MaxHealth * 2
  SubdualDamageHealRate   = 500
  SubdualDamageHealAmount = MaxHealth / 16.25
  JammingDamageCap        = MaxHealth * 2
  JammingDamageHealRate   = 500
  JammingDamageHealAmount = MaxHealth / 16.25
  FrozenDamageCap         = MaxHealth * 2
  FrozenDamageHealRate    = 500
  FrozenDamageHealAmount  = MaxHealth / 16.25
  ChronoDamageHealRate    = 500
  ChronoDamageHealAmount  = MaxHealth * 0.1
End

SubdualDamageDefaults
  KindOf                  = PROJECTILE
  SubdualDamageCap        = MaxHealth
  SubdualDamageHealRate   = 1000
End

SubdualDamageDefaults
  KindOf                  = VEHICLE
  ForbiddenKindOf         = AIRCRAFT DRONE
  FrozenDamageCap         = MaxHealth * 3
End
```

Every key is optional. `KindOf` takes a list of KindOf bits and matches an object that has any of
them; a block without `KindOf` matches every object. `ForbiddenKindOf` takes the same list and
skips the block for any object that has any of those bits, whatever `KindOf` says. Both may be
omitted. Blocks are searched from last to first, so
put general blocks first and specific ones after them. A map's own `GameData` block appends its
`SubdualDamageDefaults` after the global ones and therefore wins.

Cap and HealAmount values take one of these forms:

* `1000` - (A fixed value.)
* `MaxHealth` - (The unit's max health.)
* `MaxHealth * 2` - (A multiple of max health.)
* `MaxHealth / 16.25` - (A fraction of max health.)

`MaxHealth` is the unit's max health at the moment it is used, so a cap written as `MaxHealth * 2`
grows with veterancy health bonuses just as the subdue threshold does. HealRate values are
durations in milliseconds as before.

Precedence for each value on each unit:

1. The value written on the unit's ActiveBody, if present. An explicit `SubdualDamageCap = 0`
   still means immune.
2. The last matching `SubdualDamageDefaults` block that sets that value.
3. Zero for subdual, jamming and frozen. For chrono, `ChronoDamageHealRate` and
   `ChronoDamageHealAmountPercent` from GameData.

The same value forms are accepted on ActiveBody itself, and ActiveBody also accepts
`ChronoDamageHealRate` and `ChronoDamageHealAmount`; see the [jamming](contraZH-Objects-and-Modules.md#jamming-activebody-fields) and [frozen](contraZH-Objects-and-Modules.md#frozen-activebody-fields) ActiveBody fields.

## Subdual Overlay Textures

A texture can be drawn over units taking jamming or frozen damage. Each overlay has its own key
set; the effect is off until a texture is named.

Visible contained objects, including Overlord add-ons and carrier passengers, inherit their
container's overlay intensity. This only changes rendering; replays are unaffected.

```
JammingOverlayTexture  = JammingFX.tga
JammingOverlayScrollU  = 0.5
JammingOverlayScrollV  = 0.0
JammingOverlayScale    = 1.0
JammingOverlayColor    = R:255 G:255 B:255
JammingOverlayAdditive = Yes

FrozenOverlayTexture   = FrostFX.tga
FrozenOverlayScrollU   = 0.0
FrozenOverlayScrollV   = 0.0
FrozenOverlayScale     = 1.0
FrozenOverlayColor     = R:255 G:255 B:255
FrozenOverlayAdditive  = No
```

The values shown are the defaults apart from the texture names. See
[Subdual Jamming](contraZH-Objects-and-Modules.md#subdual-jamming)
and [Subdual Frozen](contraZH-Objects-and-Modules.md#subdual-frozen)
for what each key does and how the two overlays stack.

## BatchParticles

* `BatchParticles = No` - (Default. `Yes` draws consecutive particle systems that share a texture,
blend mode and billboard mode in a single call.)

Batches plain particle systems into one 512-point buffer, 15 to 30 percent cheaper per
TheSuperHackers. Streak, volume and terrain-conforming systems are never batched.

Notes:
* Particle systems with nothing on screen are now skipped regardless of the option.
* Ported from TheSuperHackers commit `de20ae0cb` (Ronin and Mauller).

## ParticleAtlas

* `ParticleAtlas = No` - (Default. `Yes` packs particle textures into shared atlas pages, so alpha
and additive systems with different textures batch into the same draws.)

Plain alpha and additive particle systems draw from 2048x2048 pages with premultiplied blending.
One blend state covers both modes, and the translucency sorter merges neighbouring triangles that
share a page into one draw. Batches no longer break on texture or blend changes.
Alpha-test and multiply systems also share pages, keeping their original texels and blend modes.

Notes:
* Implies batching for the systems it covers, whatever `BatchParticles` says.
* Additive particles now depth-sort with smoke instead of drawing after it, so smoke in front of
a fire covers it.
* Textures are packed the first time they draw, at most four per frame, and the pages are cleared between maps. Up to four
pages per map; textures that do not fit draw as before.
* Not covered: streak, volume and terrain-conforming systems, systems with
the flame, cryo, electric or disruption shaders, non power-of-two textures and ones smaller than
16 pixels. The glow pass also keeps the old path.
* Each texture keeps five mip levels, so very distant sprites alias a little more.
* Adapted from stephanmeesters' [texture atlas work for TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode/pull/3557).

## SkipTranslucencySort

* `SkipTranslucencySort = No` - (Default. `Yes` draws translucent triangles in submission order
instead of depth-sorting them every frame.)

Skips the per-frame CPU sort of every translucent triangle, which is costly with many particles.
The cost is layering errors where translucent effects overlap. Opaque and alpha-tested geometry is
unaffected.

Notes:
* Also settable per detail level in GameLOD.ini (`StaticGameLOD` blocks). Sorting is skipped when
either the GameData key or the active level says `Yes`.
* No Options menu checkbox.

## BackToFront

* `BackToFront = No` - (Default. `Yes` draws whole particle systems farthest first when
`SkipTranslucencySort = Yes`.)

Orders whole particle systems by distance, which removes most layering errors between effects.
Particles within one system, streaks, tank tracks and water are not reordered.

Notes:
* Does nothing while `SkipTranslucencySort = No`.
* `BatchParticles` batches get slightly smaller.

## ForceFireAllWeapons

* `ForceFireAllWeapons = No` - (Default. `Yes` makes an attack ground order fire every weapon that
can hit the ground, on every turret, instead of only the primary weapon.) Goes in the `AIUpdate`
block next to `TurretsLinked`.

Unlike `TurretsLinked`, this only affects attack ground and skips weapons that cannot hit ground.
Each turret aims and fires its own ground weapon when ready and in range.

Notes:
* Attacking a unit or building still picks the single best weapon.
* Synced slots still follow their lead; a locked weapon (attack ground special powers) fires alone.
* Weapons beyond the lead skip `PreAttackDelay`, as with linked turrets.
* Slots whose `AutoChooseSources` excludes `FROM_PLAYER` are skipped for player orders.

## NoOccupantFriendlyFire

* `NoOccupantFriendlyFire = No` - (Default. `Yes` spares the container a passenger is riding in from
that passenger's own splash damage.)

Stops e.g. a Tank Hunter in a bunker knocking down its own bunker.

Notes:
* Covers the whole containment chain (infantry in a bunker on an Overlord spare both).
* `RadiusDamageAffects = SELF` weapons (suicide attacks) still destroy the container.
* A direct order to attack the container still damages it.
* Changes the simulation; replays must use the same setting.

## Transport load slowdown

* `TransportLoadSpeedPenalty = 0%` - (Default. The fraction of its `Speed` a container loses when it
is completely full.)
* `TransportLoadTurnRatePenalty = 0%` - (Default. The same for `TurnRate`.)
* `TransportLoadAccelerationPenalty = 0%` - (Default. The same for `Acceleration`.)
* `TransportLoadLiftPenalty = 0%` - (Default. The same for `Lift`.)
* `TransportLoadPenaltyKindOf` - (Default: every kind. Only occupants with at least one of these
`KindOf` bits count toward the load.)
* `TransportLoadPenaltyForbidKindOf` - (Default: none. Occupants with any of these `KindOf` bits
never count toward the load.)

A container loses the penalty in proportion to how full it is, counted in slots, and regains it as
passengers leave.

```
GameData
  TransportLoadSpeedPenalty        = 40%
  TransportLoadAccelerationPenalty = 25%
End
```

A container's own `LoadPenaltyKindOf` and `LoadPenaltyForbidKindOf` replace the global `KindOf`
keys. Per-container overrides and opt-outs: see
[Load slowdown from occupants](contraZH-Objects-and-Modules.md#load-slowdown-from-occupants).

Notes:
* Damaged values (`SpeedDamaged`) scale by the same amount; no double penalty.
* Survives locomotor changes (`SET_NORMAL_UPGRADED`, `SET_PANIC`).
* Immobile containers (garrisoned buildings) are unaffected.
* A loaded transport slows its group like any slow unit.
* Changes the simulation; replays must use the same setting. At `0%` nothing changes.

## Distance fog

The D3D9 game can blend distant scenery into a haze tinted by the map's current sun and ambient
light. Nearby surfaces stay clear, and a smooth fade reaches full strength at the end distance.
Fog draws before colour grading and the interface. Black shroud stays black.
WorldBuilder Qt exposes the same map settings under File > Map.ini > Water tuning > Fog, with a
live viewport preview.

Put these keys in `GameData.ini`, or override them in a `GameData` block in the map's `map.ini`.
`DistanceFog = Yes` activates the effect. It defaults to `No`. Map overrides use the normal
GameData override lifetime, so the next map returns to the mod's settings.

| Key | Default | Effect |
| --- | --- | --- |
| `DistanceFog` | `No` | Activates distance fog. |
| `DistanceFogPreset` | `Sun` | Chooses `Sun`, `Desert`, `Futuristic`, `DarkHorse`, `Apocalyptic`, or `Custom`. |
| `DistanceFogColor` | `R:166 G:184 B:191` | Sets the base colour when the preset is `Custom`. |
| `DistanceFogStart` | `500` | Starts the fade at this camera depth, in world units. Negative values become zero. |
| `DistanceFogEnd` | `1800` | Reaches full strength at this camera depth. A value at or below the start disables fog. |
| `DistanceFogStrength` | `0.65` | Sets the maximum blend, clamped from 0 to 1. |
| `DistanceFogSunInfluence` | `0.5` | Blends the preset or custom colour toward the map's current sun plus ambient light, from 0 to 1. `Sun` always takes the full light colour. |

`Desert` gives a sandy tan, `Futuristic` a blue-green haze, `DarkHorse` a dark blue-grey haze,
and `Apocalyptic` a dusty olive haze. Setting sun influence to zero keeps the chosen colour fixed.

For example, a desert map's `map.ini` can contain

```ini
GameData
  DistanceFog = Yes
  DistanceFogPreset = Desert
  DistanceFogStart = 500
  DistanceFogEnd = 1800
  DistanceFogStrength = 0.65
  DistanceFogSunInfluence = 0.5
End
```

For a colour of your own, use `DistanceFogPreset = Custom` and, for example,
`DistanceFogColor = R:100 G:150 B:145`. A map can explicitly turn global fog off with
`DistanceFog = No`. Cheat builds also reload these keys from the mod's `GameData.ini` while running.

The pass needs shader model 2 and a readable INTZ depth buffer. It is inactive on D3D8, with MSAA,
or when readable depth is unavailable. Supersampling works. Translucent effects take the depth of
the opaque surface behind them. Very dark pixels receive less haze to preserve the black shroud.

## Map override cleanup

Maps with a `GameData` block no longer hang the game during exit or a map change. Reset removes
each override from the current list head and restores the original settings. Each override owns
its weapon bonus table, so removing an override leaves the original table intact.
