# Lights for modders

This build of Zero Hour lights the scene per pixel. Lamps on your models can light what is around them, in two ways:

1. **The game's own glow textures become lights by themselves.** Any model that uses the shipped lamp textures gets a real
   light, with no work from you.
2. **Model lamps** (the "Generals 2 style"): lamps you place yourself, each with its own colour, reach and timing per
   building state, a fixture that lights up with it, and a soft glow round it on screen.

Players switch it in `Options.ini`: `DX9Lighting = yes | no` (all of the new lighting) and `DX9LightEffects = yes | no`
(the lamps under it).

The look is tuned in the debug panel (Ctrl+Shift+Z) with the `lights` command. `lights` alone lists every value;
`lights <name> <value>` sets one; `lights save` writes them all to `Data/INI/DX9Lights.ini`, which the game reads when it
starts. A mod can ship that file, loose or inside a `.big`. Delete it to go back to the built-in look.

---

## 1. The game's own glow textures

The engine looks at the textures of every mesh it draws. Nothing in this section needs a file from you.

### Beams: `lightbeam`

Quads on a texture whose name starts with `lightbeam` (headlights, lamp cones) become a spot light aimed along the cone.
The quads stop drawing: the shader draws the beam in the air instead, and its light lands on the ground and on walls.

| knob | default | what it does |
|---|---|---|
| `beams` | 0.9 | the light's strength; 0 = the art's own beams and no light |
| `beamreach` | 1.8 | how far the light runs on surfaces, in the art cone's own length |
| `shafts` | 0.4 | the beam drawn in the air, beside the light's strength; 0 = none |
| `rays` | 0.5 | soft rays inside that beam; 0 = a smooth cone |
| `spill`, `spillwide` | 0.5, 2.2 | the dim light round the cone, and how wide it reaches (in cone widths) |
| `hotspot` | 0.5 | the share of the cone at full strength; the rest fades out |
| `lampwidth` | 1 | how wide the beam leaves its lamp, in the art's own width there |

### Lamp glows

Pieces on these textures become point lights. The colour comes from this table, not from the texture's pixels:

| texture | light colour (r g b) | shipped on |
|---|---|---|
| `coplight4` | 1.00 0.99 0.54 | street and building lamps |
| `coplight3` | 1.00 0.82 0.46 | street and building lamps |
| `coplight` | 1.00 0.23 0.15 | red lamps (always kept drawn: some ships draw it opaque) |
| `spotlight` | 1.00 0.99 0.46 | lamp glows |
| `pmredlight` | 1.00 0.29 0.28 | red warning lamps |
| `pmrunwaylightr` | 1.00 0.42 0.42 | runway lights, red |
| `pmrunwaylight` | 0.40 0.85 1.00 | runway lights |
| `pmgreenlight` | 0.21 1.00 0.21 | green lamps |
| `atyellowlite2` | 1.00 0.97 0.31 | yellow lamps |

- **Size decides what a piece is.** A small piece is a lamp. A piece larger than about 12 units across is taken as a pool of
  light painted on the ground or a wall: it is hidden, and the real lamps' light takes its place.
- **By default the glow textures go clear** and the shader draws each lamp's glow itself (`coronas`). `lights coronas 0`
  keeps your textures; `lights pools 1` keeps the painted pools too.
- **Lamps close together light as one** (a string of bulbs), each keeping its own glow: `glowjoin`.

| knob | default | what it does |
|---|---|---|
| `glows` | 0.5 | the lamps' light; 0 = no light from glow textures |
| `glowreach` | 0.8 | how far they reach, times the texture's own (15 to 20 units) |
| `glowjoin` | 2.5 | lamps closer than this (units) light as one; 0 = each its own |
| `coronas` | 1.5 | the glow the shader draws in place of the texture; 0 = your texture |
| `coronasize` | 1 | that glow's size, times the texture piece's |
| `pools` | 0 | 1 = keep the painted pools and glow textures drawn |
| `poolfill` | 0.6 | light standing in for a painted pool no lamp covers; 0 = none |

**To give a model a lamp the 2003 way:** put a small quad with one of these textures where the lamp is.

### Effects and the game's own lights

Glowing effects (fire, flames, explosions) light their surroundings in their own colour: `fxlights` (how many),
`fxstrength`, `fxreach`. The game's own lights (muzzle flashes, blasts) are drawn per pixel as well: `pulsemin`,
`pulsefade`. The police car keeps its 2003 searchlight with `searchlight 1`.

---

## 2. Model lamps

For lamps you place yourself. A model lamp needs two meshes in the model and a lamp file of its own in
`Data/INI/DX9ModelLamps/`. We tested them internally on converted Generals 2 buildings (the end of this section).

### In the model (W3D)

- **The marker:** a mesh named `G2LAMPS`. One triangle, anywhere, on the model's root (its origin pivot). It is never
  drawn and casts no shadow. It tells the engine where the model stands; lamp positions are measured in its space.
- **A fixture (optional, one per lamp):** a mesh named `G2ART<n>`, where `<n>` is the lamp's number in the lamp file,
  counted from 0. It is the lit face of the fixture: a copy of the surface the lamp is painted on, lifted a few
  millimetres off it on the side you see.
  - Material: additive (source ONE, destination ONE), no depth writes, self-lit (emissive in the lamp's colour, brighter
    than it).
  - Texture: white where the fixture glows, black everywhere else.
  - It is drawn only while its lamp is on, and the glow on screen is made from it.
  - On a moving part, rig it to that part's bone like any other mesh.

### The lamp file: `Data/INI/DX9ModelLamps/<model>.ini`

One file per model, under any name ending in `.ini`, loose or inside your mod's `.big`. The game reads every file in that
folder once, in name order. If two sets name the same model, the first one read wins. Since each model has its own file,
two mods that add lamp-lit buildings do not overwrite each other's lamps. The older single file
`Data/INI/DX9ModelLamps.ini` is still read, after the folder.

```
; a comment
Model ABBTCMDHQ ABBTCMDHQ_N ABBTCMDHQ_S ABBTCMDHQ_NS
Lamp -0.09 -57.33 19.66  0.883 0.934 1.000  12.7 0.00
Lamp  9.33   9.60  0.20  0.415 0.552 1.000  12.7 0.00 G2MRAD01
Track idle 0 1200 1 1 0 1 1200 1
Track idle 1 1200 1 0 0 0 225 1 276 0 570 1 628 0 928 1 981 0 1200 0
```

**`Model <names>`** starts a set: every model name it lights (night `_N`, snow `_S` and the other variants with the same
lamps). The `Lamp` and `Track` lines after it, up to the next `Model`, belong to it.

**`Lamp x y z  r g b  reach  corona  [pivot]`**
- `x y z`: where the light is, in model units, from the marker's origin. With a pivot named, from that pivot, in its own
  axes: the lamp then moves and turns with it (the radar ring above rides `G2MRAD01`, the turning base).
- `r g b`: its colour, 0 to 1.
- `reach`: how far it lights, in model units. Nothing beyond it.
- `corona`: write 0. The fixture glows instead.
- Lamps are numbered from 0 in the order of their `Lamp` lines. That number is the `<n>` in `G2ART<n>` and the lamp
  in `Track`.

**`Track state lamp length loop linear  time strength  time strength ...`**
- `state`: when it plays.
  - `idle`: nothing in the production queue.
  - `producing`: a unit in the queue.
  - `research`: an upgrade in the queue.
  - `produced`: once, each time a unit comes out, on top of the others.
- `length`: in milliseconds. `loop` 1 = repeat, 0 = once, then off.
- `linear` 1 = fade from key to key, 0 = hold each key until the next (blinks).
- The keys: a time in milliseconds and a strength. 1 = the light of one 2003 glow lamp (Generals 2's 5).
- A lamp with no track in the building's current state is off. The whole building is dark while it is being built,
  sold, disabled or destroyed.

### How a model lamp lights

- It is a point light. It lights only the surfaces that face it, and it casts no shadow, so it reaches through thin
  walls.
- A lamp right on a wall lights almost nothing of that wall: the light only grazes it. Put the light a little in front
  of its fixture. Our conversions place it in front by the fixture's own size.
- A fixture must be lifted off the side you see. Lifted inward, the wall hides it, and its glow with it.
- One fixture texture serves every variant of a model. Its white area must cover the fixtures of every variant,
  damaged ones included.

### Knobs

| knob | default | range | what it does |
|---|---|---|---|
| `modellamps` | 1 | 0 to 4 | the strength of every model lamp; 0 = all off (light, fixtures and glow) |
| `modelcore` | 0.8 | 0 to 1 | the falloff's shape: where the light halves, as a share of its reach. Low = bright on the fixture, fading fast; 0 = full strength to a quarter of the reach, then fading. Above about 0.6 the fade to the reach decides the shape and little changes. The reach itself never changes |
| `modelglow` | 0.8 | 0 to 4 | the glow round a lit fixture; 0 = none |
| `modelglowgain` | 8 | 0 to 64 | how bright the fixture counts before it is blurred |
| `modelglowsize` | 1 | 0.25 to 4 | how wide the glow spreads |

For checking: `lights modelglowview 1` shows the glow alone, `2` the fixtures it is made from (not saved).

`modelglow` is not `glows`: `glows` is the 2003 textures turned into lights (section 1); `modelglow` is the halo round a
model lamp's fixture.

### Generals 2 models

We built and tested model lamps internally on Generals 2 buildings converted with our own tool. It writes the marker, the
fixtures, their texture and the lamp file from Generals 2's own data: each lamp's place, colour, reach and timing per
state, and the bone it rides. Those models and the tool are not part of this repository.

---

## 3. Infantry

Infantry are lit by the map's lights like everything else.

Zero Hour lights infantry with their own copies of the map's lights, each multiplied by `GameData.ini`'s
`InfantryLight<time>Scale` and capped at 1. EA meant 1 by day and 2 at night, as its WorldBuilder text says, but the
shipped file writes them as percentages (`100f`, `200f`) into a value read as a plain multiplier. Every light therefore
turned full white on a soldier: lit as by day at night, and glowing against a dark scene. **This build does not use
those four keys.**

A map script's *Map / Environment / Infantry Lighting - Set* still works, as in 2003: each light × its value, capped at
1, until *Infantry Lighting - Reset*. The one shipped map that uses it, the night intro to the USA's fifth mission,
sets 1.
