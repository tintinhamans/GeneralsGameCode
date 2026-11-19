# Build-up animations for modders

Zero Hour shows how far a building has been built in one way: the building rises out of the ground as the build bar
fills (`ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT`). Its other construction animations, such as the fence unfolding and the
crane, play by time. They do not know how far the build has got.

This build adds a flag that ties an animation to the build bar: `ANIMATE_BY_CONSTRUCTION_PERCENT`. With it, a building
can assemble itself piece by piece, scaffold and all, and keep pace with the build whatever happens to it.

**A mod that uses this flag needs this build.** The 2003 game and other builds do not know the word. An unknown flag
stops the game loading its INI files, so the game does not start.

---

## 1. The flag

Put `Flags = ANIMATE_BY_CONSTRUCTION_PERCENT` on a `ConditionState` of a `W3DModelDraw` module, beside an `Animation`
and `AnimationMode = MANUAL`.

While the object is unfinished, the build's progress picks the animation's frame: 0% is the first frame, 100% the last.
It reads the same number as the build bar:

| what happens | what the animation does |
|---|---|
| the site is placed, no dozer there yet | shows the first frame |
| a dozer builds | plays forward at the bar's pace |
| low power (the build takes longer) | plays slower |
| the dozer stops, leaves or dies | holds its frame |
| another dozer takes over | carries on from the same frame |
| the building is sold | plays backwards (below) |
| a saved game is loaded | comes back at the same frame |
| the building is finished | the flag stops; the state plays its animation as `AnimationMode` says |

- Only the clip's number of frames matters, not its length or frame rate. One clip fits every build time. The engine
  blends between frames, so a short clip stays smooth on a long build.
- Only objects a dozer or worker builds have a build percent. On anything else the flag does nothing.

### Selling

Selling puts a building back in its construction states (`PARTIALLY_CONSTRUCTED ACTIVELY_BEING_CONSTRUCTED`). The game
sets the bar to 99.9%, waits 1.5 seconds and counts it down to 0 in 3 seconds; the `SOLD` state takes over at 0. A
flagged state therefore plays the build-up backwards while the building is sold. 2003's rising building sinks back the
same way.

---

## 2. In the INI

The example uses the USA Command Center's model names: `ABBtCmdHQ_A6` holds the building, assembled over its
animation; `ABBtCmdHQ_A4` the scaffold and cranes.

```
  ; the build-up: the building assembles as the bar fills
  Draw = W3DModelDraw ModuleTag_06
    AnimationsRequirePower = No
    DefaultConditionState
      Model           = None
    End
    ConditionState    = PARTIALLY_CONSTRUCTED
      Model           = ABBtCmdHQ_A6
      Animation       = ABBtCmdHQ_A6.ABBtCmdHQ_A6
      AnimationMode   = MANUAL
      Flags           = ANIMATE_BY_CONSTRUCTION_PERCENT
    End
    ConditionState    = NIGHT PARTIALLY_CONSTRUCTED
      Model           = ABBtCmdHQ_A6N
      Animation       = ABBtCmdHQ_A6N.ABBtCmdHQ_A6N
      AnimationMode   = MANUAL
      Flags           = ANIMATE_BY_CONSTRUCTION_PERCENT
    End
    ; ... SNOW and SNOW NIGHT the same way
  End
```

- **Every variant needs the flag.** Each `ConditionState` carries its own `Flags`: write it on the night, snow and
  damaged states too. An `AliasConditionState` copies it with the rest of the state.
- **An `Animation` line is required.** If a state lists several, the one it picks follows the bar.
- **`AnimationMode = MANUAL`**, so the clip never plays on its own.
- **Transitions play by time.** A `TransitionState` into or out of the flagged state plays its clip by time, like every
  2003 transition. Keep the build-up in the flagged state itself.
- **The finished building.** If your build-up assembles the building, hide the finished model while it is being built:
  `Model = None` in the main building's construction states, with no `ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT` there.
  Otherwise both show. The two flags can also share one state: the model then rises and animates at once.
- **Bones.** `ParticleSysBone` and every other bone line need bones the new model has. Bones of the 2003 model that the
  new one lacks show nothing.

---

## 3. In the model (W3D)

- A hierarchy model with its animation, named as usual (`Model.Animation`, here `ABBtCmdHQ_A6.ABBtCmdHQ_A6`).
- The first frame is the bare site; the last frame matches the finished building, so the swap at 100% does not jump.
- Each piece that moves on its own needs its own bone.

---

## Generals 2

Generals 2 builds the same way: one build-up clip, scrubbed by the build's progress rather than played in time. We built
the flag for it and tested it internally on Generals 2's command center build-up, converted with our own tool: it builds
with the bar, holds when the dozer dies, and comes apart backwards when sold. That model and the tool are not part of
this repository.
