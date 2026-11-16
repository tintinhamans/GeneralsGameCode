# Soft particles

Smoke, dust, fire and explosion sprites fade out as they near the surface behind them, so they no
longer cut a hard line into the ground or the buildings they pass through. Needs the Direct3D 9
build and a shader model 2 card.

* `SoftParticles = Yes` - (No draws sprites with hard edges. Also `Soft particles` on the Graphics
page. Needs `CheckSoftParticles` in `OptionsMenu.wnd` for the menu control.)

Tuned in the mod's `GameData.ini`:

* `SoftParticleDistance = 12` - (How far in front of a surface, in world units, a sprite starts to
fade. 0 turns the fade off.)

Notes:
* With anti-aliasing off, sprites fade against everything already drawn. With it on, they fade
against the ground only, since a multisampled depth buffer cannot be read.
* Ground-aligned and alpha-tested sprites keep their edges.
* Launch with `CONTRA_SOFTPARTICLES=2` to fade against the ground only, or `0` to turn the fade off.

# Fluid shading

Particle systems can draw acid, toxin and radiation pools with flowing marbling, highlights,
bubble rings and broken edges. `FluidShader = Yes` opts a system in; `FluidShaders = No` in
`Options.ini` turns the shading off globally. Requires shader model 2.0a.
See [Fluid shading](contraZH-ParticleSystems.md#fluid-shading) for the tuning keys and examples.

# Flame shading

Flame weapon fire flickers, licks and breaks up at its edges, and glows white-hot where it is
brightest. The air behind it shimmers. Blue, green and other coloured flames keep their colour.
Needs the Direct3D 9 build and a shader model 2 card; the shimmer also needs `Heat Effects` on.

* `FlameShaders = Yes` - (No draws flames as plain sprites. Options.ini only, no menu control.)

Picked per particle system in `ParticleSystem.ini`:

* `FlameShader = Auto` - (Default. On when the system rides a projectile whose weapon has
`DamageType = FLAME`, such as the Dragon tank, Immolator and flame tower sprays. `Yes` turns it on
for any system, such as muzzle flames, burning buildings and fire fields. `No` turns it off.)

Picked per beam in a `W3DLaserDraw` module, or per model in a `W3DModelDraw` module:

* `FlameShader = No` - (Default. `Yes` shades the beam or the model's translucent meshes as fire. On
a beam it wins over the laser and electric shaders.)

Tuned in the mod's `GameData.ini`. The same keys in a `ParticleSystem` block override them for
that system alone, and a beam or model takes the six `Flame` keys the same way:

* `FlameWarp = 0.04` - (How far the noise pushes the texture lookup, in texture widths. Higher licks
more.)
* `FlameHeat = 2.2` - (How fast bright parts run to white. 0 keeps the flame's own colour.)
* `FlameFlicker = 0.3` - (How far brightness swings, as a fraction. 0 is steady.)
* `FlameBreakup = 1` - (How much the faint fringe breaks up. 0 keeps clean edges.)
* `FlameNoiseSize = 20` - (World units across one tile of flame noise. Smaller gives finer tongues.)
* `FlameRise = 0.8` - (Noise tiles the flame pattern climbs per second.)
* `HazeBend = 1.2` - (How far the haze bends the scene, in world units at the flame. The main
strength control; past about 4 edges smear.)
* `HazeSize = 1.5` - (Haze sprite size as a multiple of the flame sprite's.)
* `HazeLift = 0.3` - (How far the haze sits above the flame, as a multiple of the sprite's size.)
* `HazeNoiseSize = 14` - (World units across one tile of haze noise. Larger gives slower, broader
waves.)
* `HazeRise = 1.1` - (Noise tiles the shimmer climbs per second.)
* `HazeMask = 2` - (How quickly faint parts of the flame reach full haze strength.)

```
ParticleSystem TankDragonMuzzleFlame
  ...
  FlameShader = Yes
  FlameFlicker = 0.5
  HazeBend = 2.5
End
```

Notes:
* A key a `ParticleSystem` block leaves out keeps the `GameData.ini` value.
* Systems with settings of their own draw apart from other flames, so keep overrides to the systems
that need them.
* The `GameData.ini` keys reload while the game runs in cheat builds, as described at the top of this
page. `ParticleSystem.ini` overrides take effect on the next launch.
* Slave systems follow their master, and a system a particle carries follows that particle's system.
* A streak burns only with `FlameShader = Yes`, since `Auto` would turn every trail behind a flame
shell to fire. Streaks, beams and models take the flame shading without the shimmer behind it.
* Projectile streams and volume particles stay plain. Terrain-conforming particles take flame
shading and heat haze on their terrain mesh. Flame shading on that mesh needs shader model 2.0a.
* On a card without shader model 2.0a, flames keep their shading but lose the soft fade.
* Launch with `CONTRA_FLAMESHADER=1` to drop the shimmer, or `0` to turn flame shading off.

# Electric shading

Tesla, lightning and EMP sparks and flares crackle. Thin arcs jump across each sprite many times a
second, in its own colour taken halfway to white, while the sprite jitters and its brightness strobes.
Ported from Red Alert 3's tesla shader. Needs the Direct3D 9 build and a shader model 2 card.
Each key is pictured on [Electric & Laser Shading](Electric-Shading.md#electric-shading).

* `ElectricShaders = Yes` - (No draws electric sprites plain. Options.ini only, no menu control.)

Picked per particle system in `ParticleSystem.ini`:

* `ElectricShader = Auto` - (Default. On when the system's `ParticleName` texture is listed in
`GameData.ini`'s `ElectricParticleTextures`. `Yes` turns it on for any system, `No` turns it off.)

Picked per beam in a `W3DLaserDraw` module:

* `ElectricShader = No` - (Default. `Yes` shades the beam as electricity instead of as a laser, for
tesla and lightning bolts. The beam keeps its texture, which tiles along it as before.)

Picked per model in a `W3DModelDraw` module:

* `ElectricShader = No` - (Default. `Yes` shades the model's translucent meshes as electricity. The
module takes the six tuning keys below.)

Listed and tuned in the mod's `GameData.ini`:

* `ElectricParticleTextures = TeslaBlast.tga ...` - (Textures whose systems turn electric. Each line
adds to the list, so a long list can span several lines. Read at launch.)
* `ElectricArcs = 1.5` - (Arc brightness. 0 turns the arcs off.)
* `ElectricArcSharpness = 10` - (Lower gives broad glowing bands, higher thin threads.)
* `ElectricNoiseSize = 40` - (World units across one tile of arc noise. Smaller gives more, closer arcs.)
* `ElectricJitter = 0.03` - (How far the texture jumps each crackle, in texture widths.)
* `ElectricFlicker = 0.6` - (How far brightness swings, as a fraction. 0 is steady.)
* `ElectricRate = 15` - (Crackles per second. 0 freezes the arcs.)
* `ElectricParticleScale = 100%` - (How large electric sprites draw, as a percentage of the particle's
own size. With electric shading off they keep their own size.)

A `W3DLaserDraw` module takes the six keys from `ElectricArcs` to `ElectricRate`. Each one it sets
overrides `GameData.ini` for that beam alone, and the keys it leaves out keep `GameData.ini`'s values.
A `ParticleSystem` takes `ElectricParticleScale` alone, and uses `GameData.ini`'s other keys.

```
ParticleSystem EMPRing
  ...
  ElectricShader = Yes
  ElectricParticleScale = 80%
End

Draw = W3DLaserDraw ModuleTag_Draw
  ...
  ElectricShader = Yes
  ElectricArcs = 2.5
  ElectricRate = 30
End
```

Notes:
* Arcs lie in the view plane and are sized in world units, so ground-aligned rings and bolts, which
the pitched camera sees at a slant, crackle with the same arcs as upright sprites. Big effects carry
more arcs than small sparks.
* Past the default camera's distance, `CameraHeight` over the sine of `CameraPitch`, arcs widen with
depth, so they stay visible at the top of the screen and when zoomed out.
* A system that is both flame and electric draws as flame.
* Streaks such as `TeslaTrail.tga`, volume particles and multiplied
sprites stay plain.
* Terrain-conforming particles take electric shading and `ElectricParticleScale` on their terrain
mesh, with shader model 2.0a.
* Launch with `CONTRA_ELECTRICSHADER=0` to turn electric shading off.

# Laser shading

Laser beams burn white-hot along their axis and glow out in their own colour. The core wavers in
width, pulses of brightness run along the beam towards its target, and the flat edges of the beam
melt into glow. Beams also fade where they meet the ground, like soft particles. Needs the Direct3D 9
build and a shader model 2 card. Each key is pictured on
[Electric & Laser Shading](Laser-Shading.md#laser-shading).

* `LaserShaders = Yes` - (No draws lasers plain. Options.ini only, no menu control.)

Beams from `W3DLaserDraw` get it by default. Turned off per beam in the draw module:

* `LaserShader = Yes` - (Default. `No` draws the beam plain, for beams that are not lasers, such as
the hacker's `EXBinaryStream32.tga` data stream.)

Laser streaks from particle systems are picked in `ParticleSystem.ini`:

* `LaserShader = Auto` - (Default. On when the system is `Type = STREAK` and its `ParticleName` texture
is listed in `GameData.ini`'s `LaserParticleTextures`. `Yes` turns it on for any streak system, `No`
turns it off.)

Listed and tuned in the mod's `GameData.ini`:

* `LaserParticleTextures = EXRedLaser.dds ...` - (Streak textures whose systems draw as lasers. Each
line adds to the list, so a long list can span several lines. Read at launch.)
* `LaserCore = 1.2` - (Core brightness. 0 turns the core off.)
* `LaserCoreWidth = 0.25` - (Core width, as a fraction of the beam's half width.)
* `LaserShimmer = 0.3` - (How far the core's width wavers, as a fraction. 0 keeps it steady, 1 at most.)
* `LaserPulse = 0.4` - (How far brightness swings along the beam, as a fraction. 0 is steady.)
* `LaserPulseSize = 120` - (World units across one tile of pulse noise. Smaller gives more, closer pulses.)
* `LaserPulseSpeed = 400` - (World units a second the pulses travel. 0 freezes them.)
* `LaserDebug = No` - (Yes subtracts shaded beams from the scene instead of adding them, so they
show dark on any background. Plain beams stay bright, which also shows which beams are shaded.)

A `W3DLaserDraw` module takes the six tuning keys from `LaserCore` to `LaserPulseSpeed`. Each one it
sets overrides `GameData.ini` for that beam alone, and the keys it leaves out keep `GameData.ini`'s
values. Laser streaks from particle systems always use `GameData.ini`'s.

```
ParticleSystem Red_BurstLaserTrail
  ...
  Type = STREAK
  LaserShader = Yes
End

Draw = W3DLaserDraw ModuleTag_Draw
  ...
  LaserCore = 2
  LaserPulseSpeed = 800
End
```

Notes:
* A beam's own texture still gives it its colour and shape. The shader adds the core, the pulses and
the soft edges on top.
* Beams made of several `NumBeams` layers get a core in every layer, so the axis runs brightest.
* Pulses are fixed along the beam in the world, so they keep flowing smoothly while the shooter moves.
* Sprite particles, such as laser muzzle flares, stay plain.
* Launch with `CONTRA_LASERSHADER=0` to turn laser shading off.

# Laser ground glow

Laser beams light the ground per pixel, with one light shaped like the beam. The light fades with
the distance to the nearest point on the beam, so a beam skimming the ground lights a bright strip
and a beam climbing into the sky lights only the ground near the shooter. Slopes facing the beam
catch more light, and the laser shader's pulses brighten the ground as they pass. Beams drawn plain,
electric or cryo light the ground steadily, and cryo beams light it in their ice colour. The Direct3D 8
build keeps the strip of dynamic lights described in
[Laser ground glow](contraZH-Options.md#laser-ground-glow). Needs a shader model 2 card. Each key is
pictured on [Electric & Laser Shading](Laser-Shading.md#laser-ground-glow).

Uses the same switches and keys as the Direct3D 8 glow: `LaserRef`, `DynamicLights`,
`LaserGroundGlowColor`, `LaserGroundGlowIntensity`, `LaserGroundGlowRadius`, and `GroundGlowColor`,
`GroundGlowIntensity` and `GroundGlowRadius` on the `W3DLaserDraw` module. Shaped further in the
mod's `GameData.ini`:

* `LaserGroundGlowFalloff = 2` - (How fast the light fades with distance from the beam, as a power.
Higher gives a tight bright core, lower a broad wash.)
* `LaserGroundGlowWrap = 0.5` - (Light on ground facing away from the beam, from 0 to 1. 0 lights
only slopes facing it, 1 lights all ground evenly.)
* `LaserGroundGlowDebug = No` - (Yes darkens the ground by as much as the glow would light it, so the
light's reach and shape show as a shadow.)
* `LaserGroundGlowOverlap = Yes` - (Yes lights the ground under overlapping beams in one pass, so the
overlap brightens modestly. No lights each beam on its own, and overlaps compound.)

Notes:
* The light lights the ground's own colour, measured against the map's terrain lighting, so red
ground turns redder and a dark night map lights up as much as a bright day.
* The light reaches `GroundGlowRadius` on the module, else `LaserGroundGlowRadius`, else 1.25 times
the laser's `OuterBeamWidth`, at least 15. It is counted from the beam in three dimensions.
* Beams whose glows overlap light the ground together. Each colour channel takes the root of the sum
of the beams' squared light, so two equal beams light 1.4 times as much as one, three light 1.7
times, and crossing beams leave no crease. A lone beam lights as before.
* Up to seven overlapping beams combine. Past that, the seven brightest light the spot and the rest
go dark. Combining needs a shader model 2.0a card. Without one, or when the overlap covers too much
ground for one draw, each beam lights on its own.
* Lasers no longer take dynamic lights, so the per-pixel lights stay free for explosions and
muzzle flashes.
* Water covers the glow on ground beneath it. Units and buildings are not lit.
* Launch with `CONTRA_LASERGLOW=0` to go back to the dynamic lights.

# Cryo shading

Freeze rays turn to ice. The beam's colour is pulled toward an ice tint at its own brightness, a
blue-white core runs along its axis, frost bands drift slowly towards the target, and jagged ice teeth
cut into both edges. Frost trails from particle systems draw the same way. Frost puffs and flares take
the tint, thin cracks split their faint fringe into shards, and glints twinkle across them. Cryo wins
over the laser, electric and flame shaders wherever it is on. Needs the Direct3D 9 build and a shader
model 2 card.

* `CryoShaders = Yes` - (No draws cryo effects plain. Options.ini only, no menu control.)

Picked per beam in a `W3DLaserDraw` module:

* `CryoShader = No` - (Default. `Yes` shades the beam as ice, whatever its `LaserShader` and
`ElectricShader`.)

Picked per model in a `W3DModelDraw` module:

* `CryoShader = No` - (Default. `Yes` shades the model's translucent meshes as ice, the way a cryo
sprite draws. The module takes the nine tuning keys from `CryoTint` to `CryoShardSize`.)

Picked per particle system in `ParticleSystem.ini`:

* `CryoShader = Auto` - (Default. On when the system's `ParticleName` texture is listed in
`GameData.ini`'s `CryoParticleTextures`. `Yes` turns it on for any system, `No` turns it off. Streaks
draw as freeze rays, sprites as frost.)

Listed and tuned in the mod's `GameData.ini`:

* `CryoParticleTextures = CryoFlare.tga EXCryoRing.tga ...` - (Textures whose systems turn to ice.
Each line adds to the list, so a long list can span several lines. Read at launch.)
* `CryoTint = R:150 G:215 B:255` - (The ice colour. Only its hue counts, so the effect keeps its
brightness.)
* `CryoTintStrength = 0.8` - (How far the effect's own colour moves to the tint, from 0 to 1. 0 keeps
the texture's colour.)
* `CryoCore = 1` - (Core brightness. 0 turns the core off.)
* `CryoCoreWidth = 0.3` - (Core width, as a fraction of the beam's half width.)
* `CryoFrost = 0.5` - (How strongly the frost bands whiten the beam. 0 turns them off.)
* `CryoFrostSize = 200` - (World units across one tile of frost noise. Smaller gives more, closer
bands.)
* `CryoFrostSpeed = 60` - (World units a second the bands drift towards the target. 0 freezes them.)
* `CryoShards = 0.4` - (How far ice teeth cut into a beam, as a fraction of its half width, and how far
cracks reach into a sprite's fringe. 0 keeps edges whole.)
* `CryoShardSize = 8` - (World units from one tooth or shard to the next.)
* `CryoGlints = 2` - (Glint brightness on sprites. 0 turns glints off.)
* `CryoGlintSize = 1.5` - (World units between glints.)
* `CryoGlintRate = 2` - (About how many times a second each glint twinkles. 0 freezes them.)
* `CryoParticleScale = 100%` - (How large cryo sprites and trails draw, as a percentage of the
particle's own size. With cryo shading off they keep their own size.)

A `W3DLaserDraw` module takes the nine tuning keys from `CryoTint` to `CryoShardSize`. Each one it
sets overrides `GameData.ini` for that beam alone, and the keys it leaves out keep `GameData.ini`'s
values. A `ParticleSystem` takes `CryoParticleScale` alone, and uses `GameData.ini`'s other keys.

```
ParticleSystem FrostPuff
  ...
  CryoShader = Yes
  CryoParticleScale = 60%
End

Draw = W3DLaserDraw ModuleTag_Draw
  ...
  CryoShader = Yes
  CryoTint = R:120 G:200 B:255
  CryoShards = 0.6
End
```

Notes:
* The beam's texture still gives it its shape. With `CryoTintStrength` below 1 its colour shows through
the tint.
* Teeth and frost bands are fixed along the beam, counted from the shooter, so they travel with it.
* Cracks and glints are fixed in the world, so they hold still while the camera pans and shift as
the sprite drifts through them.
* The ground glow under a cryo beam holds steady and takes the beam's tint. With cryo shading off the
beam and its glow keep their own colour.
* Volume particles and multiplied sprites stay plain.
* Terrain-conforming particles take cryo shading and `CryoParticleScale` on their terrain mesh.
* Launch with `CONTRA_CRYOSHADER=0` to turn cryo shading off.

# Disruption shading

A shape ripples the scene behind it and pulls its colours apart, like a jammed video signal. The
shape can be a model's translucent meshes, a particle system's sprites, a strip along a laser beam, or
a plain disc from an `FXList`. Its texture is the mask, so the scene bends most where the texture is
brightest and not at all where it is black. Three movements add up, each with its own strength:

* Rings travel outwards through the shape.
* A noise field wobbles the scene, like strong heat haze.
* Bands across the screen jump sideways in fits.

Red bends further than green and blue less, which fringes every bent edge with colour. Needs the
Direct3D 9 build, a shader model 2 card and `Heat Effects` on.

Picked per entry in a `W3DModelDraw` module, a `W3DLaserDraw` module or a `ParticleSystem`:

* `DisruptionShader = No` - (Default. `Yes` bends the scene behind the shape and draws the shape's own
art over it. `Only` bends the scene and leaves the art undrawn, so the shape is a pure mask.)

A `W3DLaserDraw` module also takes:

* `DisruptionWidth = 0` - (Width of the strip that bends the scene, in world units. 0 takes the
beam's widest width, which is often too thin to see.)

Tuned in the mod's `GameData.ini`:

* `DisruptionRingStrength = 3` - (How far the rings push the scene, in world units. 0 turns them off.)
* `DisruptionRingSize = 40` - (World units from one ring to the next.)
* `DisruptionRingSpeed = 60` - (World units a second the rings travel outwards. 0 freezes them.)
* `DisruptionWobble = 1.5` - (How far the noise pushes the scene, in world units. 0 turns it off.)
* `DisruptionWobbleSize = 30` - (World units across one tile of wobble noise. Smaller is finer.)
* `DisruptionWobbleSpeed = 1.5` - (Noise tiles the wobble crosses per second.)
* `DisruptionGlitch = 4` - (How far a band jumps sideways at most, in world units. 0 turns the bands
off.)
* `DisruptionGlitchSize = 12` - (Height of a band, in pixels on a 1080p screen, scaled to other
resolutions.)
* `DisruptionGlitchRate = 12` - (Jumps per second. 0 freezes the bands.)
* `DisruptionChroma = 0.5` - (How much further red bends than green, and how much less blue does, as
a fraction. 0 keeps the colours together.)
* `DisruptionChromaSpread = 0.5` - (World units red and blue part along the rings' direction even
where nothing bends. 0 fringes only bent pixels.)
* `DisruptionMask = 2` - (How quickly the shape's brightness reaches full strength. Higher lets
fainter parts of the texture bend the scene.)

Every entry that takes `DisruptionShader` also takes these twelve keys. Each one it sets overrides
`GameData.ini` for that entry alone, and the keys it leaves out keep `GameData.ini`'s values.

```
Draw = W3DModelDraw ModuleTag_01
  DefaultConditionState
    Model = EXGLAJammer
  End
  DisruptionShader       = Yes
  DisruptionRingStrength = 5
  DisruptionGlitch       = 8
End

ParticleSystem JammerSparks
  ...
  DisruptionShader = Only
  DisruptionWobble = 3
End

Draw = W3DLaserDraw ModuleTag_Draw
  ...
  DisruptionShader = Yes
  DisruptionWidth  = 30
End
```

An `FXList` draws a disc with no art through a `Disruption` block. The disc is brightest at its
middle and fades to its rim.

* `Radius` - (The disc's radius, in world units.)
* `Duration` - (How long the disc lasts, in milliseconds.)
* `Fade = 20%` - (Share of the duration spent fading in, and again fading out.)
* The twelve tuning keys above.

```
FXList FX_JammerPulse
  Disruption
    Radius                 = 425
    Duration               = 1200
    Fade                   = 20%
    DisruptionRingStrength = 5
  End
End
```

Notes:
* Disruption bends a copy of the scene taken before anything translucent draws. Particles, beams and
translucent models stay crisp over it, and overlapping shapes do not bend each other.
* On a model only translucent meshes take part, the ones with an additive or alpha-blended material.
With `Only`, the whole model goes undrawn.
* A model's rings start at each mesh's own origin, a sprite's at the middle of its texture, and a
disc's at its middle. On a beam they run along its length and push across it.
* Rings keep time with the game clock, so an effect that is replaced every second ripples without
a jump.
* A beam needs a `Texture`, which masks its strip. Without one it draws as a plain beam, even with
`Only`.
* On a particle system, `ALPHA_TEST` and `MULTIPLY` sprites, streaks and volume particles take no
disruption, and draw their own art even with `Only`. Terrain-conforming particles disrupt through
their terrain mesh, including with `Only`.
* A particle system without a `ParticleName` texture keeps its plain art with `Only`, since it
has no texture to mask the disruption.
* The bend follows the mask at the size the sprite draws, so `CryoParticleScale` and
`ElectricParticleScale` scale it too. On a model it fades with the object's opacity.
* An `FXList` disc lies flat and ignores depth, like a shockwave. At most 32 show at once.
* With `Heat Effects` off or on the Direct3D 8 build, `Yes` and `Only` both draw the plain art, so
the effect never vanishes.
* Launch with `CONTRA_DISRUPTSHADER=0` to turn disruption off.

# Heat smudges

Heat particle systems, the shimmer behind jet engines, microwave beams and blast waves, draw as
sprites through a pixel shader. A system is a heat system when its `ParticleName` starts with
`SMUDGE.`, as in `ParticleName = SMUDGE.tga`, or when it has `Type = SMUDGE`. The texture is never
loaded, and `Shader` is ignored. The look is the original one. Each particle shifts the scene behind it
towards a spot that jumps on every particle update, strongest at the particle's middle and gone at its
edges. The area is slightly blurred, and a particle that reaches past the edge of the view loses its
shift along that axis.

Needs the Direct3D 9 build, a shader model 2.0a card and `Heat Effects` on. It has no INI keys.

What changed against the original smudges:
* The game copies only the part of the screen the heat particles cover, where the original copied
the whole screen for a single particle. With anti-aliasing on it still copies the whole screen.
* Heat particles draw in batches with the other particles. The game no longer rebuilds a smudge
list every particle update.

Notes:
* The heat still bends the particles behind it, since it draws after them.
* On the Direct3D 8 build or a card without shader model 2.0a, the original smudges draw.
* Launch with `CONTRA_SMUDGESHADER=0` to bring the original smudges back.

# Shockwave (FXList.ini)

An expanding ring on the ground that bends everything behind it, like the air a big blast pushes
out. Needs the Direct3D 9 build, a shader model 2 card and `Heat Effects` on.

* `Radius` - (How far the ring travels, in world units.)
* `Width` - (How thick the ring is, in world units.)
* `Strength` - (How far it bends the scene, in world units. Around 2 to 6 reads well. In world units
the bend shrinks on screen as the camera pulls back, and is lost when zoomed far out.)
* `StrengthInPixels = No` - (Yes counts `Strength` in pixels on a 1080p screen instead, scaled to
other resolutions, so the bend looks the same at every zoom. Around 6 to 12 reads well.)
* `Duration` - (How long the ring takes to reach `Radius`, in milliseconds. It fades as it goes.)
* `Rings = 1` - (How many waves ride in the ring. More than one makes ripples that fade toward the
ring's edges.)
* `Highlight = 0%` - (Brightens the crests and darkens the troughs by this share of the scene's
colour, so the rings show on smooth water where a bend alone is lost. Around 20% to 40% reads well.)
* `AllowedSurface = ALL` - (`ALL` / `LAND` / `WATER`. With `WATER` the ring plays only over water,
lies on the water surface, and the shore and units in front of it hide it.)

```
FXList FX_NukeExplosion
  Shockwave
    Radius           = 300
    Width            = 40
    Strength         = 8
    StrengthInPixels = Yes
    Duration         = 900
  End
End
```

This one ripples the water where a tank shell lands.

```
FXList WeaponFX_GenericTankShellDetonation
  Shockwave
    AllowedSurface   = WATER
    Rings            = 3
    Highlight        = 30%
    Radius           = 40
    Width            = 15
    Strength         = 5
    StrengthInPixels = Yes
    Duration         = 1200
  End
End
```

Notes:
* At most 32 rings show at once; a new one replaces the oldest.
* The ring lies flat at the effect's height, so on steep ground it is centred but not draped.
* A shell that does not collide with water explodes on the lake bed, and the ring still shows on the
surface above it.

