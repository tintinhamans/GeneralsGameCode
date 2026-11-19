# The map's wind

A map can have a wind. It blows the snow and rain at a slant, turns flags that are rigged for it (section 3) to point
downwind, and leans and sways the map's trees (section 4). Zero Hour has no wind: snow and rain fall straight down, flags
keep the direction they were built with, and trees sway in a fixed breeze unless a mission script sets one.

---

## 1. The keys

Three keys in the `Weather` block:

| key | default | what it does |
|---|---|---|
| `SnowWindDirection` | 0 | the direction the wind blows **to**, in degrees: 0 = along the map's X axis, 90 = along its Y axis |
| `SnowWindSpeed` | 0 | how fast it carries the snow across, in world units per second; 0 = no wind |
| `SnowWindGust` | 0 | 0 to 1: how much slow gusts vary the speed; 0 = steady |

For one map, put them in the map's `map.ini`. A map that already has a `Weather` block (its snow or rain) just gets the
three lines:

```
Weather
  SnowWindDirection = 30
  SnowWindSpeed = 2.0
  SnowWindGust = 0.4
End
```

For every map, put them in `Data/INI/Weather.ini`. A map's own `map.ini` values win over it.

**Older builds:** the 2003 game skips keys it does not know. There the map loads as before, with no wind.

---

## 2. Snow and rain

- Each flake moves across as it falls, so the snow falls at a slant. The slant is the wind speed against the fall speed
  (`SnowVelocity`).
- Rain falls far faster than snow, so the same wind tilts it far less, as real rain. For more slant on a rain map,
  raise its `SnowWindSpeed`.
- Gusts are two slow waves on the speed; their pattern repeats about every 31 seconds.
- The wind only shows on a map with snow or rain (`SnowEnabled = Yes`). The flags follow it either way.

## 3. Cloth flags

A flag with the bones below turns to point where the wind blows: the whole flag turns about its pole, and its own
flapping is unchanged. Only the direction counts. Any `SnowWindSpeed` above 0 turns them; the strength and the gusts do
not change the flapping. With no wind, every flag keeps the direction it was built with.

Zero Hour's own flags do not turn: they are rigid panels without these bones. We remade them internally as cloth with
these bones, using our own tool. Those models and the tool are not part of this repository.

**To make a flag that turns with the wind**, give it a chain of bones from the pole outward, and name its first two
bones with one of these pairs:

| first bone (at the pole) | second bone |
|---|---|
| `FLGU00` (a building's upper cloth) | `FLGU01` |
| `FLGL00` (its lower cloth) | `FLGL01` |
| `FC1_00` ... `FC8_00` | `FC1_01` ... `FC8_01` |

- The first bone turns, about its own Z axis, so stand it upright at the pole.
- The engine reads where the flag points in the rest pose from the first bone towards the second. The rest of the
  chain is yours: animate the flapping as usual.
- Up to ten flags per model, each name once.
- Flags are not stopped at walls. A flag hung on a wall turns into the wall when the wind blows that way.

## 4. Trees

The map's trees (the ones a map places, drawn together by the engine) lean downwind and sway with the wind.

- **Direction:** `SnowWindDirection`; the trees lean the way it blows.
- **Strength:** at `SnowWindSpeed` 2 they lean and sway as much as Zero Hour's own breeze does by default (about 3°
  each). Both grow with the speed up to 4 times that, at 8 and above.
- **Gusts:** `SnowWindGust` varies it, in step with the snow's gusts.
- With no wind, trees keep Zero Hour's own breeze.
- **A mission script wins.** Once a script runs *Map / Environment / Set wind sway amount and direction*, the trees
  follow the script, whatever the map's wind.

## 5. Trying it

In the debug panel (Ctrl+Shift+Z):
- `snow` shows the weather and the wind, with the snow's slant.
- `snow wind <degrees> <speed> [gust]` sets the wind live.
- `snow on` / `snow off` shows the weather on any map, for testing.

None of it is saved. Put the values you like in the map's `map.ini`.
