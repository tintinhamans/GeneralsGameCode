# ParticleSystem.ini

## Muzzle smoke attached to barrels

Draw modules can use `WeaponFireParticleSys` to create smoke on each shot and keep its
emitter attached to the firing muzzle. See [Weapon fire particles](contraZH-Objects-and-Modules.md#weapon-fire-particles)
for the syntax, lifetime requirements and migration from smoke-only condition transitions.

## Stealth visibility

Particle systems attached to an object or its drawable stop emitting while the object is
undetected and stealthed to the viewing player. Emission resumes when the object becomes visible.
This covers `TransitionDamageFX`, model `ParticleSysBone` effects and automatic damage smoke
created by `ActiveBody` from model bones and the `AutoSmokeParticle*` settings in `GameData.ini`.
Allies, detected objects and observers use the existing object visibility rules.
Particles already emitted finish their normal lifetime.

## Attached particle trails

`PerParticleAttachedSystem` samples its controlling particle at the end of each movement step.
This keeps trails aligned with effects that offset their initial position to account for that step,
such as `ShotgunProjectileObjects` with both line endpoints at `X:-16 Y:0 Z:0`.
The controlling particle still moves smoothly between updates.

## FXList offsets

FXList `ParticleSystem` entries default to `OrientOffset = Yes`, matching vanilla offset rotation.
Effects such as the ICBM detonation spread their offsets with the missile's rotation.
Set `OrientOffset = No` to keep offsets in world axes when `OrientToObject` and `OrientXY` are also `No`.
This controls the spawn offset; `OrientToObject` controls the particle system's orientation.

## Bloom

`Bloom = Yes` is the default. Set `Bloom = No` on a particle system to exclude its particles
from the bloom pass while keeping their normal appearance.
The FX tuner's INI entries mode exposes `Bloom` under each particle system's `System` controls.

```ini
ParticleSystem MyBeamEffect
  Shader = ADDITIVE
  Bloom = No
  ; Other particle settings go here.
End
```

The global `Bloom` setting must be enabled for particles or objects to glow. Only additive
particles feed bloom; alpha-blended effects and smudges do not. The flag applies to sprites,
streaks and other additive particle types supported by the existing bloom renderer.
`ParticleSystemExtend` inherits the parent's flag and can override it with `Bloom = Yes` or `No`.
FX lists that spawn the particle system use its flag; there is no separate FXList key.

`BloomZoomCompensation = No` is the default. Set it to `Yes` on a particle system to keep its
glowing sprites and streak widths at their default-camera screen size when zoomed out.
The system must use `Shader = ADDITIVE` and `Bloom = Yes`, and global bloom must be on.
Streak paths still follow their world positions. `ParticleSystemExtend` inherits the setting
and can override it. The FX tuner exposes the flag under each particle system's `System` controls.

```ini
ParticleSystem MyGlowEffect
  Shader = ADDITIVE
  Bloom = Yes
  BloomZoomCompensation = Yes
End
```

## Fluid shading

`FluidShader = Yes` shades a particle system as flowing liquid with marbling, highlights,
growing bubble rings and broken edges. It defaults to `No` and takes precedence over cryo,
flame and electric shading. The texture supplies the footprint and colour; particle colour
keys and alpha still control tint and lifetime fading. Use a soft pool texture with
`IsGroundAligned = Yes` for acid, toxin and radiation puddles.

Requires the D3D9 build and shader model 2.0a. D3D8 and unsupported cards draw the original
particles. `FluidShaders = No` in `Options.ini`, or `CONTRA_FLUIDSHADER=0` before launch,
also restores the original appearance. The shader panel includes a fluid toggle.

The FX tuner's `Fluid` tab previews all eight settings, loads textures, compares alpha and
additive blending, and loads, saves or copies the global INI values. Its plain-pool switch
disables shading in the preview. Enable `FluidShader = Yes` in the particle definition to
use the effect in game. Cheat builds reload saved global values while a map runs.

These keys work in `GameData.ini` and in a `ParticleSystem` block. A system overrides only
the keys it supplies; omitted keys use the global defaults.

| Key | Default | Effect |
| --- | --- | --- |
| `FluidWarp` | `0.035` | Moves texture UVs with the currents, from 0 to 0.25 texture widths. |
| `FluidNoiseSize` | `48` | Sets world units across one tile of current noise. |
| `FluidSpeed` | `6` | Sets current travel in world units per second; 0 freezes animation. |
| `FluidBody` | `0.6` | Sets marbling contrast from 0 to 1. |
| `FluidBubbles` | `0.7` | Sets bubble brightness; 0 removes bubbles. |
| `FluidBubbleSize` | `8` | Sets world units between bubble cells. |
| `FluidEdge` | `0.3` | Breaks up the texture's visible fringe, from 0 to 1; 0 keeps it intact. |
| `FluidGlow` | `0.2` | Adds brightness in the particle's own colour; 0 removes it. |

Use nonnegative values. Noise and bubble sizes clamp to at least 0.01 world units.
The pattern stays fixed in world XY while the camera moves, so overlapping particles with
the same settings share currents and bubbles. Systems with local overrides batch separately.
`ParticleSystemExtend` inherits the switch and tuning keys.

```ini
ParticleSystemExtend MyAcidPool ExistingPool
  Shader = ALPHA
  IsGroundAligned = Yes
  FluidShader = Yes
  FluidSpeed = 10
  FluidBubbles = 1
  FluidBubbleSize = 4
  FluidEdge = 0.5
End
```

Starting values for the three mockup styles follow. Set the yellow-green, dark emerald or
lime tint through the texture and particle colour keys.

| Style | FluidSpeed | FluidBody | FluidBubbles | FluidBubbleSize | FluidEdge | FluidGlow |
| --- | --- | --- | --- | --- | --- | --- |
| Acid | 10 | 0.4 | 1 | 4 | 0.5 | 0.1 |
| Toxin | 2 | 0.9 | 0.6 | 14 | 0.2 | 0 |
| Radiation | 5 | 0.5 | 1.2 | 10 | 0.35 | 0.8 |

Both alpha-blended and additive sprites work. Alpha blending suits opaque sludge; additive
blending suits luminous pools and can feed bloom. `FluidGlow` brightens the fluid itself;
it does not light the surrounding terrain. Fluid particles keep their surface coverage
instead of fading against scene depth, so ground pools remain visible.
Streaks, volume particles, alpha-tested particles and multiplied particles keep their existing
rendering. Terrain-conforming particles take fluid shading on the mesh that follows the ground.

## D3D9 billboard rendering

* D3D9 builds camera-facing particle quads on the GPU when shader model 3 hardware is available.
* Texture atlases, particle colours, rotation, soft fading and shader effects keep their existing settings.
* Instancing works with `SkipTranslucencySort = No` and preserves triangle sorting with other translucent geometry.
* Each half of a sorted particle quad keeps its place in the sorted order; bloom, haze and disruption passes can instance independently.
* Streaks, volume particles, ground-aligned particles and D3D8 builds use the existing renderer.
* Set the environment variable `CONTRA_PARTICLE_INSTANCING=0` before starting the game to compare with CPU-built quads.

## ConformToTerrain

* `ConformToTerrain = No` - (Default. `Yes` opts a single effect into terrain conforming.)

Ground aligned, non-billboarded, flat particles are built as a mesh from the terrain heightmap, like
decals, instead of a flat quad that cuts through hills.

Notes:
* Opt-in per effect; retail effects are unchanged.
* Requires `IsGroundAligned = Yes` on the same effect; billboarded and multi-layer volume particles do not conform.
* Flat ground becomes one quad; only uneven ground is meshed cell by cell.
* Only the part on screen is meshed. Past 160 cells per side the mesh stops at coarser cells.
* Fluid, flame, electric and cryo shaders shade the terrain mesh. Flame heat haze and disruption
also follow the mesh, and cryo and electric particle-scale settings change its footprint.
* Ground meshes retain their surface coverage instead of soft-fading against the terrain they follow.
* Flame and electric terrain shaders require shader model 2.0a and keep warped edges inside the
original particle footprint. Other particle shaders keep their existing hardware requirements.

## Snow move effects

A vehicle showing its `SNOW` model state swaps its move particles for the system of the same name
ending in `Snow`.

```
ParticleSystem RocketBuggyDust      ; Dust = RocketBuggyDust in the draw module
ParticleSystem RocketBuggyDustSnow  ; used instead on snow
```

* `W3DTankDraw` - (`TreadDebrisLeft` and `TreadDebrisRight`. The defaults become
`TrackDebrisDirtLeftSnow` and `TrackDebrisDirtRightSnow`.)
* `W3DTruckDraw` - (`Dust`, `DirtSpray` and `PowerslideSpray`.)
* `W3DTankTruckDraw` - (All five keys above.)

Notes:
* A name without a `Snow` system keeps its normal effect, so nothing has to be defined.
* Follows the unit's own snow state, including a per-object weather override set in WorldBuilder.

## ParticleSystemExtend

See [Extend Blocks](contraZH-Extend.md#particlesystemini).
