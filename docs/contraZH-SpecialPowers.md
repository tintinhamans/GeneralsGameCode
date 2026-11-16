# SpecialPower.ini

## Contra superweapon enums

Use these names in `Enum`. Use the full `SPECIAL_` spelling;
`TOMAHAWK_STORM` alone is not an enum name.

| Enum | Existing EVA event suffix |
| --- | --- |
| `SPECIAL_NUCLEAR_STORM` | `NUCLEAR_STORM` |
| `SPECIAL_WEATHER_CONTROL` | `WEATHER_CONTROL` |
| `SPECIAL_HATF_MISSILE` | `HATF_MISSILE` |
| `SPECIAL_COMMAND_UPLINK` | `COMMAND_UPLINK` |
| `SPECIAL_ATMOSPHERIC_LENS` | `ATMOSPHERIC_LENS` |
| `SPECIAL_ICBM` | `ICBM` |
| `SPECIAL_TOMAHAWK_STORM` | `TOMAHAWK_STORM` |

The enum names are appended after the existing values to preserve their save-file numbers.
They do not automatically select the corresponding EVA events. For example, wire the Tomahawk
Storm template as follows, retaining the template's existing reload and module settings.

```ini
SpecialPower SuperweaponTomahawkStorm
  Enum = SPECIAL_TOMAHAWK_STORM
  EvaDetectedOwn = SUPERWEAPONDETECTED_OWN_TOMAHAWK_STORM
  EvaDetectedAlly = SUPERWEAPONDETECTED_ALLY_TOMAHAWK_STORM
  EvaDetectedEnemy = SUPERWEAPONDETECTED_ENEMY_TOMAHAWK_STORM
  EvaLaunchedOwn = SUPERWEAPONLAUNCHED_OWN_TOMAHAWK_STORM
  EvaLaunchedAlly = SUPERWEAPONLAUNCHED_ALLY_TOMAHAWK_STORM
  EvaLaunchedEnemy = SUPERWEAPONLAUNCHED_ENEMY_TOMAHAWK_STORM
  EvaReadyOwn = SUPERWEAPONREADY_OWN_TOMAHAWK_STORM
  EvaReadyAlly = SUPERWEAPONREADY_ALLY_TOMAHAWK_STORM
  EvaReadyEnemy = SUPERWEAPONREADY_ENEMY_TOMAHAWK_STORM
End
```

## Enum behavior reference

`Enum` identifies the power. The engine uses that identity to find modules, group shortcut powers
and synchronize timers. Reusing an enum can therefore make otherwise separate powers interfere.
The enum does not supply an OCL, weapon, unit or update module.

For enums from `SPECIAL_ION_CANNON` onward, `BehaviorEnum` chooses the ActionManager target checks.
When omitted or `SPECIAL_INVALID`, the engine uses the fallback table below. Older enums ignore
`BehaviorEnum`. Resolution happens once, so use a concrete behavior from the first table rather
than another custom enum. `BehaviorEnum` does not copy EVA defaults, AI exceptions or
`SpecialAbilityUpdate` effects, which check the original `Enum`.

### Original target checks

Every enum before `SPECIAL_ION_CANNON` appears below. A dash means that command form is rejected.
Location checks allow fog of war unless stated otherwise. “Revealed” means the point must not be
fully shrouded. “Water allowed” means ActionManager adds no water restriction; the module and its
spawned objects can impose further limits. CommandButton target options must also match the form.
Readiness, money, target designators and module requirements still apply. Object checks reject
dead, chronodisabled and shrouded targets before applying the enum-specific rule.

| Enum | Location | Object | No target |
| --- | --- | --- | --- |
| `SPECIAL_INVALID` | - | - | - |
| `SPECIAL_DAISY_CUTTER` | Revealed; water allowed | - | - |
| `SPECIAL_PARADROP_AMERICA` | Revealed; no water or cliffs | - | - |
| `SPECIAL_CARPET_BOMB` | Revealed; water allowed | - | - |
| `SPECIAL_CLUSTER_MINES` | Revealed; water allowed | - | - |
| `SPECIAL_EMP_PULSE` | Revealed; water allowed | - | - |
| `SPECIAL_NAPALM_STRIKE` | Revealed; water allowed | - | - |
| `SPECIAL_CASH_HACK` | - | Finished enemy capturable CASH_GENERATOR structure, excluding rebuild holes | - |
| `SPECIAL_NEUTRON_MISSILE` | Revealed; water allowed | - | - |
| `SPECIAL_SPY_SATELLITE` | On-map, including shroud; water allowed | - | - |
| `SPECIAL_DEFECTOR` | - | Enemy non-structure; defector checks | - |
| `SPECIAL_TERROR_CELL` | Revealed; water allowed | - | - |
| `SPECIAL_AMBUSH` | Revealed; water allowed | - | - |
| `SPECIAL_BLACK_MARKET_NUKE` | Revealed; water allowed | - | - |
| `SPECIAL_ANTHRAX_BOMB` | Revealed; water allowed | - | - |
| `SPECIAL_SCUD_STORM` | Revealed; water allowed | - | - |
| `SPECIAL_DEMORALIZE_OBSOLETE` | - | - | - |
| `SPECIAL_CRATE_DROP` | Revealed; no water or cliffs | - | - |
| `SPECIAL_A10_THUNDERBOLT_STRIKE` | Revealed; water allowed | - | - |
| `SPECIAL_DETONATE_DIRTY_NUKE` | - | - | Yes |
| `SPECIAL_ARTILLERY_BARRAGE` | Revealed; water allowed | - | - |
| `SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES` | - | Module laser-lock target filter | - |
| `SPECIAL_REMOTE_CHARGES` | - | Structure or vehicle; charge limits | Yes |
| `SPECIAL_TIMED_CHARGES` | - | Structure or vehicle; charge limits | - |
| `SPECIAL_HELIX_NAPALM_BOMB` | On-map, including shroud; water allowed | Structure or vehicle; charge limits | - |
| `SPECIAL_HACKER_DISABLE_BUILDING` | - | Enemy capturable structure, excluding rebuild holes | - |
| `SPECIAL_TANKHUNTER_TNT_ATTACK` | - | Structure or non-aircraft vehicle | - |
| `SPECIAL_BLACKLOTUS_CAPTURE_BUILDING` | - | Capture checks | - |
| `SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK` | - | Enemy ground vehicle; hack checks | - |
| `SPECIAL_BLACKLOTUS_STEAL_CASH_HACK` | - | Enemy cash generator; hack checks | - |
| `SPECIAL_INFANTRY_CAPTURE_BUILDING` | - | Capture checks | - |
| `SPECIAL_RADAR_VAN_SCAN` | On-map, including shroud; water allowed | - | - |
| `SPECIAL_SPY_DRONE` | On-map, including shroud; water allowed | - | - |
| `SPECIAL_DISGUISE_AS_VEHICLE` | - | Vehicle excluding aircraft, boats, cliff jumpers and RailroadBehavior | - |
| `SPECIAL_BOOBY_TRAP` | - | Allied or neutral structure | - |
| `SPECIAL_REPAIR_VEHICLES` | Revealed; water allowed | - | - |
| `SPECIAL_PARTICLE_UPLINK_CANNON` | Revealed; water allowed | - | - |
| `SPECIAL_CASH_BOUNTY` | - | - | - |
| `SPECIAL_CHANGE_BATTLE_PLANS` | - | - | Yes |
| `SPECIAL_CIA_INTELLIGENCE` | - | - | Yes |
| `SPECIAL_CLEANUP_AREA` | Revealed; water allowed | - | - |
| `SPECIAL_LAUNCH_BAIKONUR_ROCKET` | Unrestricted by location check | - | Yes |
| `SPECIAL_SPECTRE_GUNSHIP` | Revealed; water allowed | - | - |
| `SPECIAL_GPS_SCRAMBLER` | Revealed; water allowed | - | - |
| `SPECIAL_FRENZY` | Revealed; water allowed | - | - |
| `SPECIAL_SNEAK_ATTACK` | Building placement checks; see below | - | - |
| `SPECIAL_CHINA_CARPET_BOMB` | Revealed; water allowed | - | - |
| `EARLY_SPECIAL_CHINA_CARPET_BOMB` | Revealed; water allowed | - | - |
| `SPECIAL_LEAFLET_DROP` | Revealed; water allowed | - | - |
| `EARLY_SPECIAL_LEAFLET_DROP` | Revealed; water allowed | - | - |
| `EARLY_SPECIAL_FRENZY` | Revealed; water allowed | - | - |
| `SPECIAL_COMMUNICATIONS_DOWNLOAD` | - | - | Yes |
| `EARLY_SPECIAL_REPAIR_VEHICLES` | Revealed; water allowed | - | - |
| `SPECIAL_TANK_PARADROP` | Revealed; no water or cliffs | - | - |
| `SUPW_SPECIAL_PARTICLE_UPLINK_CANNON` | Revealed; water allowed | - | - |
| `AIRF_SPECIAL_DAISY_CUTTER` | Revealed; water allowed | - | - |
| `NUKE_SPECIAL_CLUSTER_MINES` | Revealed; water allowed | - | - |
| `NUKE_SPECIAL_NEUTRON_MISSILE` | Revealed; water allowed | - | - |
| `AIRF_SPECIAL_A10_THUNDERBOLT_STRIKE` | Revealed; water allowed | - | - |
| `AIRF_SPECIAL_SPECTRE_GUNSHIP` | Revealed; water allowed | - | - |
| `INFA_SPECIAL_PARADROP_AMERICA` | Revealed; no water or cliffs | - | - |
| `SLTH_SPECIAL_GPS_SCRAMBLER` | Revealed; water allowed | - | - |
| `AIRF_SPECIAL_CARPET_BOMB` | Revealed; water allowed | - | - |
| `SUPR_SPECIAL_CRUISE_MISSILE` | Revealed; water allowed | - | - |
| `LAZR_SPECIAL_PARTICLE_UPLINK_CANNON` | Revealed; water allowed | - | - |
| `SUPW_SPECIAL_NEUTRON_MISSILE` | Revealed; water allowed | - | - |
| `SPECIAL_BATTLESHIP_BOMBARDMENT` | Revealed; water allowed | Non-allied object | - |

* `SPECIAL_INVALID` is a sentinel, not a usable power. `SPECIAL_DEMORALIZE_OBSOLETE` is a reserved
  slot. Builds defining `ALLOW_DEMORALIZE` replace it with `SPECIAL_DEMORALIZE`, which accepts
  revealed locations without a water restriction.
* Paradrops and crate drops fall through to the jumpjet terrain check, so cliffs are rejected too.
* `SPECIAL_SNEAK_ATTACK` checks its reference building's placement angle, terrain, clear path,
  overlap and revealed shroud. Retail-compatible builds only check shroud. Custom enums using
  `BehaviorEnum = SPECIAL_SNEAK_ATTACK` do not enter this identity-based placement branch and
  currently fail the location check. Use the original enum for this behavior.
* Capture rejects allied, immune, unfinished, sold, unrevealed and undetected stealthed buildings.
  Enemy buildings need not be `CAPTURABLE`; neutral buildings must be. Visible garrison occupants
  and containers that appear friendly also prevent capture.
* Vehicle hacking rejects aircraft, airborne targets, undetected stealth unless disguised, and
  containers that appear friendly. Cash stealing requires a finished, capturable enemy cash
  generator and rejects rebuild holes and undetected stealth.
* Charge limits require `SpecialAbilityUpdate`, room below `MaxSpecialObjects`, and no charge from
  this caster already on the target. Remote and timed charges also check each other's module.
  Helix bombs share these object checks even though their usual command uses a location.
* `SPECIAL_TOGGLE_DRAWBRIDGE` is a custom enum despite appearing in the no-target rules. Its
  fallback is itself, so it accepts the no-target form and rejects location/object forms.
* Some original variants have incomplete command support. In particular,
  `SUPW_SPECIAL_PARTICLE_UPLINK_CANNON` and `LAZR_SPECIAL_PARTICLE_UPLINK_CANNON` accept locations
  but have no object branch; `SUPW_SPECIAL_NEUTRON_MISSILE` has no explicit no-target case and is
  still rejected. Names alone do not establish equivalent behavior.

### Custom enum fallbacks

Every enum from `SPECIAL_ION_CANNON` onward appears here. These are target-check fallbacks only.
Unless overridden with `BehaviorEnum`, the new Contra superweapon enums use the default
`SPECIAL_NEUTRON_MISSILE` fallback. They accept revealed locations, including water, reject object
and no-target forms, and have no hardcoded superweapon EVA events.

| Effective behavior | Enums |
| --- | --- |
| `SPECIAL_NEUTRON_MISSILE` | `SPECIAL_ION_CANNON`, `SPECIAL_CLUSTER_MISSILE`, `SPECIAL_SUNSTORM_MISSILE`, `SPECIAL_METEOR_STRIKE`, `SPECIAL_PUNISHER_CANNON`, `SPECIAL_CHEMICAL_MISSILE`, `SPECIAL_CHRONOSPHERE`, `SPECIAL_RIFT_GENERATOR`, `SOCOM_SPECIAL_AIR_DEPLOY_MARKER`, `SPECIAL_JUMPJET`, `SPECIAL_ALL_BATTLE_PLANS_ACTIVE`, `SPECIAL_NUCLEAR_STORM`, `SPECIAL_WEATHER_CONTROL`, `SPECIAL_HATF_MISSILE`, `SPECIAL_COMMAND_UPLINK`, `SPECIAL_ATMOSPHERIC_LENS`, `SPECIAL_ICBM`, `SPECIAL_TOMAHAWK_STORM` |
| `SPECIAL_FRENZY` | `AIRF_SPECIAL_HOLO_PLANES`, `TANK_SPECIAL_FRENZY`, `NUKE_SPECIAL_FRENZY`, `DEMO_SPECIAL_FRENZY`, `CHEM_SPECIAL_FRENZY`, `FORT_SPECIAL_FRENZY` |
| `SPECIAL_PARADROP_AMERICA` | `AIRF_SPECIAL_PARADROP_AMERICA`, `SOCOM_SPECIAL_SUPPLY_DROP`, `SOCOM_SPECIAL_TANK_PARADROP`, `TANK_SPECIAL_TANK_PARADROP`, `TANK_SPECIAL_PARADROP`, `SUPW_SPECIAL_PARADROP_AMERICA`, `SUPW_SPECIAL_TANK_PARADROP` |
| `SPECIAL_AMBUSH` | `AIRF_SPECIAL_HELICOPTER_AMBUSH`, `DEMO_SPECIAL_AMBUSH`, `CHEM_SPECIAL_AMBUSH`, `LAZR_SPECIAL_AMBUSH` |
| `SPECIAL_CARPET_BOMB` | `AIRF_SPECIAL_SUPERSONIC_AIRSTRIKE`, `AIRF_SPECIAL_HEAVY_AIRSTRIKE`, `SOCOM_SPECIAL_COASTAL_BOMBARDEMENT`, `TANK_SPECIAL_NAPALM_BOMB`, `TANK_SPECIAL_CHINA_CARPET_BOMB`, `NUKE_SPECIAL_NUCLEAR_AIRSTRIKE`, `NUKE_SPECIAL_CHINA_CARPET_BOMB`, `NUKE_SPECIAL_BALLISTIC_MISSILE`, `SECW_SPECIAL_SYSTEM_HACK`, `DEMO_SPECIAL_SUICIDE_PLANE`, `DEMO_SPECIAL_CARPET_BOMB`, `CHEM_SPECIAL_CARPET_BOMB`, `CHEM_SPECIAL_AIRSTRIKE`, `FORT_SPECIAL_AIRSTRIKE`, `FORT_SPECIAL_CARPET_BOMB`, `LAZR_SPECIAL_DAISY_CUTTER`, `LAZR_SPECIAL_AIRSTRIKE`, `SUPW_SPECIAL_AIRSTRIKE` |
| `SPECIAL_CLUSTER_MINES` | `TANK_SPECIAL_CLUSTER_MINES` |
| `SPECIAL_REPAIR_VEHICLES` | `TANK_SPECIAL_REPAIR_VEHICLES`, `NUKE_SPECIAL_REPAIR_VEHICLES`, `DEMO_SPECIAL_REPAIR_VEHICLES`, `CHEM_SPECIAL_REPAIR_VEHICLES`, `FORT_SPECIAL_REPAIR_VEHICLES`, `LAZR_SPECIAL_NANO_SWARM`, `SUPW_SPECIAL_FORCEFIELD` |
| `SPECIAL_EMP_PULSE` | `TANK_SPECIAL_EMP_PULSE`, `NUKE_SPECIAL_NEUTRON_BOMB`, `SECW_SPECIAL_EMP_HACK` |
| `SPECIAL_ARTILLERY_BARRAGE` | `TANK_SPECIAL_ARTILLERY_BARRAGE`, `NUKE_SPECIAL_ARTILLERY_BARRAGE`, `DEMO_SPECIAL_ARTILLERY_BARRAGE`, `FORT_SPECIAL_ARTILLERY_BARRAGE`, `LAZR_SPECIAL_ORBITAL_STRIKE`, `SUPW_SPECIAL_ORBITAL_STRIKE` |
| `SPECIAL_CASH_HACK` | `NUKE_SPECIAL_CASH_HACK` |
| `SPECIAL_SPY_SATELLITE` | `CHINA_SPECIAL_SPY_SATELLITE`, `SECW_SPECIAL_SPY_SATELLITE`, `LAZR_SPECIAL_SPY_SATELLITE` |
| `SPECIAL_CIA_INTELLIGENCE` | `SECW_SPECIAL_HUNTER_SEEKER`, `SPECIAL_ALL_BATTLE_PLANS` |
| `SPECIAL_SPECTRE_GUNSHIP` | `SECW_SPECIAL_DRONE_GUNSHIP`, `LAZR_SPECIAL_SPECTRE_GUNSHIP`, `SUPW_SPECIAL_SPECTRE_GUNSHIP` |
| `SPECIAL_SNEAK_ATTACK` | `DEMO_SPECIAL_SNEAK_ATTACK`, `CHEM_SPECIAL_SNEAK_ATTACK` |
| `SPECIAL_GPS_SCRAMBLER` | `DEMO_SPECIAL_GPS_SCRAMBLER`, `CHEM_SPECIAL_GPS_SCRAMBLER`, `FORT_SPECIAL_GPS_SCRAMBLER` |
| `SPECIAL_ANTHRAX_BOMB` | `DEMO_SPECIAL_ANTHRAX_BOMB`, `CHEM_SPECIAL_ANTHRAX_BOMB` |
| `SPECIAL_LEAFLET_DROP` | `CHEM_SPECIAL_VIRUS`, `SUPW_SPECIAL_CRYOBOMB` |
| `SPECIAL_JUMPJET` | `SPECIAL_TELEPORT_SELF` |
| `SPECIAL_TOGGLE_DRAWBRIDGE` | `SPECIAL_TOGGLE_DRAWBRIDGE` |

`BehaviorEnum = SPECIAL_JUMPJET` accepts revealed locations without water or cliffs and rejects
object/no-target forms. The `SPECIAL_JUMPJET` identity itself defaults to neutron-missile target
checks unless given that explicit behavior. `SPECIAL_TELEPORT_SELF` borrows the jumpjet checks; its `TeleportSelfSpecialPower` module
performs the teleport. `SPECIAL_ALL_BATTLE_PLANS` borrows the no-target CIA checks and requires
`AllBattlePlansUpdate`. `SPECIAL_ALL_BATTLE_PLANS_ACTIVE` is intended only to display a timer;
its default neutron-missile target checks do not implement battle plans.

### Effects and identity checks

The following checks use the original `Enum`, even when `BehaviorEnum` is set.

| Enum | Hardcoded behavior |
| --- | --- |
| `SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES` | SpecialAbilityUpdate locks the secondary weapon and orders a force attack. It creates a laser during preparation, rechecks the module's target filter during the lock, and restores the primary weapon when the laser is removed. |
| `SPECIAL_INFANTRY_CAPTURE_BUILDING`, `SPECIAL_BLACKLOTUS_CAPTURE_BUILDING` | SpecialAbilityUpdate captures or clears a garrison, checks booby traps, plays BUILDINGSTOLEN for the victim and records capture statistics. Preparation plays BUILDINGBEINGSTOLEN for the victim, marks the script capture state and scales PreparationTime by the target's CaptureTimePercent. Movement cancels capture. Infantry and Black Lotus restart recharge at different preparation/completion stages. Command translation also recognizes these identities for capture orders. |
| `SPECIAL_HACKER_DISABLE_BUILDING`, `SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK` | SpecialAbilityUpdate applies DISABLED_HACKED for EffectDuration and creates the disable particles. |
| `SPECIAL_BLACKLOTUS_STEAL_CASH_HACK` | SpecialAbilityUpdate transfers EffectValue credits, capped by the victim's money, and plays CASHSTOLEN for the victim. Retail-compatible or PRESERVE_HARDCODED_BLACK_LOTUS_CASH_HACK builds use 1000 credits. |
| `SPECIAL_REMOTE_CHARGES`, `SPECIAL_TIMED_CHARGES`, `SPECIAL_TANKHUNTER_TNT_ATTACK`, `SPECIAL_BOOBY_TRAP` | SpecialAbilityUpdate creates and attaches SpecialObject through StickyBombUpdate and checks existing booby traps. Remote charges also detonate the caster's existing charges when invoked without a target. These identities keep placed special objects when preparation ends. |
| `SPECIAL_HELIX_NAPALM_BOMB` | SpecialAbilityUpdate creates SpecialObject and keeps it when preparation ends. |
| `SPECIAL_DISGUISE_AS_VEHICLE` | SpecialAbilityUpdate tells StealthUpdate to disguise as the target and keeps special objects when preparation ends. |
| `SPECIAL_JUMPJET` | SpecialAbilityUpdate creates the jumpjet container, launches through JumpjetMissileAIUpdate and keeps the container when preparation ends. AIGroup preserves formation destinations for this identity. |
| `SPECIAL_BLACKLOTUS_CAPTURE_BUILDING`, `SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK`, `SPECIAL_BLACKLOTUS_STEAL_CASH_HACK` | SpecialAbilityUpdate chooses VoiceCaptureBuildingComplete, VoiceDisableVehicleComplete or VoiceStealCashComplete instead of VoiceTaskComplete. SpecialAbilityUpdate disables competing hacker/capture/charge abilities when an ability starts. |
| `SPECIAL_TANKHUNTER_TNT_ATTACK` | SquishCollide prevents the target from crushing the caster while this ability is active. |
| `SPECIAL_INFANTRY_CAPTURE_BUILDING`, `SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK` | CommandButtonHuntUpdate allows capture targets such as oil derricks and avoids rehacking disabled vehicles. Its timed-charge and TNT flags are inside the infantry-capture branch and cannot currently be set. |
| `SPECIAL_CHANGE_BATTLE_PLANS`, `SPECIAL_TOGGLE_DRAWBRIDGE` | ControlBarCommand chooses dedicated battle-plan or drawbridge button handling. |
| `SPECIAL_CLUSTER_MINES`, `NUKE_SPECIAL_CLUSTER_MINES` | Skirmish AI favors approaches to its own base when finding a superweapon target. Custom mine enums do not inherit this exception. |
| `SPECIAL_SNEAK_ATTACK` | AI target scoring favors poorly defended valuable areas and script firing paths validate the destination. Custom sneak-attack enums do not inherit these identity checks. |

SpecialAbilityUpdate creates preparation lasers for the hacker and three Black Lotus abilities,
triggers radar infiltration events and destroys their special objects when preparation ends.

Other enums obtain their effects from the attached modules. For example, selecting a carpet-bomb,
repair, frenzy or neutron-missile behavior does not itself create aircraft, repair units, apply
frenzy or launch a missile.

### Hardcoded EVA defaults

| Enum identities | Detected | Launched | Ready |
| --- | --- | --- | --- |
| `SPECIAL_PARTICLE_UPLINK_CANNON`, `SUPW_SPECIAL_PARTICLE_UPLINK_CANNON`, `LAZR_SPECIAL_PARTICLE_UPLINK_CANNON` | PARTICLECANNON | PARTICLECANNON | PARTICLECANNON |
| `SPECIAL_NEUTRON_MISSILE`, `NUKE_SPECIAL_NEUTRON_MISSILE`, `SUPW_SPECIAL_NEUTRON_MISSILE` | NUKE | NUKE | NUKE |
| `SPECIAL_SCUD_STORM` | SCUDSTORM | SCUDSTORM | SCUDSTORM |
| `SPECIAL_GPS_SCRAMBLER`, `SLTH_SPECIAL_GPS_SCRAMBLER` | None | GPS_SCRAMBLER | None |
| `SPECIAL_SNEAK_ATTACK` | None | SNEAK_ATTACK | None |

Each populated cell means `SUPERWEAPONDETECTED_*_<suffix>`, `SUPERWEAPONLAUNCHED_*_<suffix>` or
`SUPERWEAPONREADY_*_<suffix>` for OWN, ALLY and ENEMY. All other enums have no hardcoded defaults
in these three paths. Ally selection also includes neutral relationships.

Detected events run when structure construction completes. A template with custom detected
events takes precedence over the structure's vanilla defaults. Launched events run through
SpecialPowerModule; ready events run through the public countdown UI after frame zero.
A custom `EvaLaunchedOwn/Ally/Enemy` or `EvaReadyOwn/Ally/Enemy` replaces the corresponding
relationship's default when supplied. New enums never inherit defaults through `BehaviorEnum`.
Define the event in Eva.ini and wire it to the SpecialPower template explicitly.

### Source map

* [SpecialPowerType.h](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Include/Common/SpecialPowerType.h) defines enum identities and save-file order.
* [SpecialPower.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/Common/RTS/SpecialPower.cpp) registers accepted INI names.
* [ActionManager.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/Common/RTS/ActionManager.cpp) implements target checks and custom fallbacks.
* [SpecialAbilityUpdate.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/SpecialAbilityUpdate.cpp) implements identity-specific ability effects.
* [SpecialPowerModule.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/GameLogic/Object/SpecialPower/SpecialPowerModule.cpp), [Player.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp) and [InGameUI.cpp](https://github.com/triatomic/contraZH/blob/main/GeneralsMD/Code/GameEngine/Source/GameClient/InGameUI.cpp) choose launched, detected and ready EVA events.

## INI-defined identities and announcements

Existing compiled enum, EVA and cursor names keep their IDs and behavior. Additional names can
be defined in INI without rebuilding the executable. The three name lists remain separate.

### Special-power identities

An unknown `Enum` name declares a custom identity. Templates using the same name share that
identity, regardless of capitalization. Use a mod-specific prefix to avoid accidental reuse.
Unknown names containing `SPECIAL_` are rejected as misspelled built-in enums.

```ini
SpecialPower SpecialAbilityCustomTeleport
  Enum = CONTRA_CUSTOM_TELEPORT
  BehaviorEnum = SPECIAL_JUMPJET
  ReloadTime = 20000
  RadiusCursorRadius = 20
  EvaLaunchedEnemy = SuperweaponLaunched_Enemy_CustomTeleport
End
```

`BehaviorEnum` still accepts compiled behavior names. It selects target checks, and does not
copy hardcoded effects or EVA defaults. This example borrows the jumpjet terrain checks and
requires a suitable module, such as `TeleportSelfSpecialPower`, referencing this template on
the caster. An omitted behavior uses the existing neutron-missile targeting fallback.

Built-in identities still use the object's fixed bitmask. Custom identities are found through
the object's special-power modules. They do not enlarge the bitmask or change its save format.
Saving and loading reconstructs custom availability from those modules and templates.

### Custom EVA events

Define the event in `Eva.ini` and reference its name in any of the nine `EvaDetected*`,
`EvaLaunched*` or `EvaReady*` fields. References can precede their definitions.

```ini
EvaEvent SuperweaponLaunched_Enemy_CustomTeleport
  Priority = 5
  TimeBetweenChecksMS = 30000
  ExpirationTimeMS = 5000
  SideSounds
    Side = America
    Sounds = EvaUSA_TeleportLaunched
  End
End
```

`Sounds` must name an audio event defined by the mod. This example uses Contra's teleport
announcement. Add SideSounds for each listener side that should hear it. The side belongs to
the listener, not the caster. `EvaLaunchedEnemy` is only triggered for enemy listeners.

Custom events use the existing speech queue, priorities, expiration and repeat delays. They
play when explicitly triggered; a new name does not create an automatic engine condition.
Built-in events keep their existing condition handlers. `None` still suppresses the matching
built-in announcement. Public countdown settings still control ready announcements.

### Custom radius cursors

Add a named block inside `InGameUI` in `InGameUI.ini`.

```ini
InGameUI
  RadiusCursor CONTRA_CUSTOM_TELEPORT_CURSOR
    Texture = SCCWidowJump
    Style = SHADOW_ALPHA_DECAL
    OpacityMin = 50%
    OpacityMax = 100%
    OpacityThrobTime = 1000
    Color = R:255 G:0 B:0 A:255
    OnlyVisibleToOwningPlayer = Yes
  End
End
```

Reference it from a command button in `CommandButton.ini`.

```ini
CommandButton Command_CustomTeleport
  Command = SPECIAL_POWER
  SpecialPower = SpecialAbilityCustomTeleport
  Options = NEED_TARGET_POS
  RadiusCursorType = CONTRA_CUSTOM_TELEPORT_CURSOR
  CursorName = Defector
  InvalidCursorName = GenericInvalid
End
```

Attach the button through the caster's CommandSet. `Texture` must exist in the mod; this example
uses Contra's Widow jump decal. The circle takes its size from `RadiusCursorRadius`, or the
existing command-button radius overrides. `AnchorRadiusCursorType` also accepts custom names.
A custom cursor changes the decal, not target legality or the power's effects.

### Registration and compatibility

* Names are case-insensitive. Built-in names always retain their compiled identities.
* Custom IDs occupy a separate positive integer range and are derived from the lowercase name.
  Definition order and unrelated additions do not change those IDs. Each category has its own
  registry; a power, event and cursor can use the same name without sharing an identity.
* A hash collision between different names is rejected during loading. Rename one definition
  if the engine reports a collision; the engine never silently aliases the two names.
* Startup validation rejects referenced custom EVA events and cursors that have no definition,
  and the error names the missing event or cursor. Defining a custom power identity needs no
  separate registration block.
* Normal matching mod-data requirements still apply to multiplayer and replays. Stable custom
  IDs do not make different definitions compatible, or make these definitions work in older builds.
* Existing saves keep the same built-in mask format. Saves using custom powers require those
  power templates and modules to remain installed; renaming a custom identity changes its ID.

## StartCooldownOnFirstShot

* `StartCooldownOnFirstShot = No` - (Default. `Yes` delays `ReloadTime` until the unit has fired the
shots the power ordered.)

For powers whose OCL has an `Attack` nugget, the cooldown starts after the unit finishes shooting.
Until then the power is locked (cameo greyed out), through the last of `NumberOfShots`.

```
SpecialPower SpecialPowerDig
  Enum                     = SPECIAL_HELIX_NAPALM_BOMB
  ReloadTime               = 60000
  StartCooldownOnFirstShot = Yes
End
```

Notes:
* At least one shot fired: the cooldown starts even if the rest never fire (move order, empty clip,
death).
* **No** shot fired: the power comes back ready. Upfront costs are not refunded;
deferred costs are not charged.
* Failsafe: after `ReloadTime` of waiting the cooldown starts anyway, and the log names the power
(also catches OCLs with no `Attack` nugget).
* Ignored for `SharedSyncedTimer` powers.

## DeductCostOnFirstShot

`DeductCostOnFirstShot = No` by default. Set it to `Yes` to deduct `Cost` once, when the
caster fires its first shot, instead of when the power triggers.

```
SpecialPower SpecialPowerDig
  Enum                     = SPECIAL_HELIX_NAPALM_BOMB
  ReloadTime               = 60000
  Cost                     = 500
  DeductCostOnFirstShot     = Yes
  StartCooldownOnFirstShot  = Yes
End
```

* The player must have enough money when the power triggers. Money is not reserved.
* If the player spends that money before firing, the first shot takes up to the remaining balance.
* Cancelling before the first shot costs nothing. Later shots in the burst cost nothing extra.
* Cooldown timing still follows `StartCooldownOnFirstShot`. With that setting off, the cooldown
starts when the power triggers, even if the attack is later cancelled.
* The caster must fire the shot itself, as with an OCL `Attack` nugget. Shots from spawned units
do not count. A caster that cannot fire pays when the power triggers.
* Any shot from the caster during the wait counts, including one at a target ordered afterwards.
* The shot wait ends after `ReloadTime`. A caster still attacking at that point pays then.
* A script or crate that resets the timer during the wait also collects the cost.
* Saving and loading preserves any unpaid cost.

## Eva events for Contra superweapons

Events for the remaining Contra superweapons. These are not tied to a special power enum, so any
template can use them, including several templates sharing one set.

* SUPERWEAPONDETECTED_OWN_NUCLEAR_STORM
* SUPERWEAPONDETECTED_ALLY_NUCLEAR_STORM
* SUPERWEAPONDETECTED_ENEMY_NUCLEAR_STORM
* SUPERWEAPONLAUNCHED_OWN_NUCLEAR_STORM
* SUPERWEAPONLAUNCHED_ALLY_NUCLEAR_STORM
* SUPERWEAPONLAUNCHED_ENEMY_NUCLEAR_STORM
* SUPERWEAPONREADY_OWN_NUCLEAR_STORM
* SUPERWEAPONREADY_ALLY_NUCLEAR_STORM
* SUPERWEAPONREADY_ENEMY_NUCLEAR_STORM

* SUPERWEAPONDETECTED_OWN_WEATHER_CONTROL
* SUPERWEAPONDETECTED_ALLY_WEATHER_CONTROL
* SUPERWEAPONDETECTED_ENEMY_WEATHER_CONTROL
* SUPERWEAPONLAUNCHED_OWN_WEATHER_CONTROL
* SUPERWEAPONLAUNCHED_ALLY_WEATHER_CONTROL
* SUPERWEAPONLAUNCHED_ENEMY_WEATHER_CONTROL
* SUPERWEAPONREADY_OWN_WEATHER_CONTROL
* SUPERWEAPONREADY_ALLY_WEATHER_CONTROL
* SUPERWEAPONREADY_ENEMY_WEATHER_CONTROL

* SUPERWEAPONDETECTED_OWN_HATF_MISSILE
* SUPERWEAPONDETECTED_ALLY_HATF_MISSILE
* SUPERWEAPONDETECTED_ENEMY_HATF_MISSILE
* SUPERWEAPONLAUNCHED_OWN_HATF_MISSILE
* SUPERWEAPONLAUNCHED_ALLY_HATF_MISSILE
* SUPERWEAPONLAUNCHED_ENEMY_HATF_MISSILE
* SUPERWEAPONREADY_OWN_HATF_MISSILE
* SUPERWEAPONREADY_ALLY_HATF_MISSILE
* SUPERWEAPONREADY_ENEMY_HATF_MISSILE

* SUPERWEAPONDETECTED_OWN_COMMAND_UPLINK
* SUPERWEAPONDETECTED_ALLY_COMMAND_UPLINK
* SUPERWEAPONDETECTED_ENEMY_COMMAND_UPLINK
* SUPERWEAPONLAUNCHED_OWN_COMMAND_UPLINK
* SUPERWEAPONLAUNCHED_ALLY_COMMAND_UPLINK
* SUPERWEAPONLAUNCHED_ENEMY_COMMAND_UPLINK
* SUPERWEAPONREADY_OWN_COMMAND_UPLINK
* SUPERWEAPONREADY_ALLY_COMMAND_UPLINK
* SUPERWEAPONREADY_ENEMY_COMMAND_UPLINK

* SUPERWEAPONDETECTED_OWN_ATMOSPHERIC_LENS
* SUPERWEAPONDETECTED_ALLY_ATMOSPHERIC_LENS
* SUPERWEAPONDETECTED_ENEMY_ATMOSPHERIC_LENS
* SUPERWEAPONLAUNCHED_OWN_ATMOSPHERIC_LENS
* SUPERWEAPONLAUNCHED_ALLY_ATMOSPHERIC_LENS
* SUPERWEAPONLAUNCHED_ENEMY_ATMOSPHERIC_LENS
* SUPERWEAPONREADY_OWN_ATMOSPHERIC_LENS
* SUPERWEAPONREADY_ALLY_ATMOSPHERIC_LENS
* SUPERWEAPONREADY_ENEMY_ATMOSPHERIC_LENS

* SUPERWEAPONDETECTED_OWN_ICBM
* SUPERWEAPONDETECTED_ALLY_ICBM
* SUPERWEAPONDETECTED_ENEMY_ICBM
* SUPERWEAPONLAUNCHED_OWN_ICBM
* SUPERWEAPONLAUNCHED_ALLY_ICBM
* SUPERWEAPONLAUNCHED_ENEMY_ICBM
* SUPERWEAPONREADY_OWN_ICBM
* SUPERWEAPONREADY_ALLY_ICBM
* SUPERWEAPONREADY_ENEMY_ICBM

* SUPERWEAPONDETECTED_OWN_TOMAHAWK_STORM
* SUPERWEAPONDETECTED_ALLY_TOMAHAWK_STORM
* SUPERWEAPONDETECTED_ENEMY_TOMAHAWK_STORM
* SUPERWEAPONLAUNCHED_OWN_TOMAHAWK_STORM
* SUPERWEAPONLAUNCHED_ALLY_TOMAHAWK_STORM
* SUPERWEAPONLAUNCHED_ENEMY_TOMAHAWK_STORM
* SUPERWEAPONREADY_OWN_TOMAHAWK_STORM
* SUPERWEAPONREADY_ALLY_TOMAHAWK_STORM
* SUPERWEAPONREADY_ENEMY_TOMAHAWK_STORM

### Which template uses which event set

The Contra superweapon templates these were added for. Several templates share one `Enum`, so the
event set is chosen per template, not per enum.

| Template | Event set |
| --- | --- |
| SuperweaponNuclearStorm | NUCLEAR_STORM |
| SuperweaponEMPStorm (weather control device) | WEATHER_CONTROL |
| SuperweaponHatfMissile, SuperweaponHatfVMissile | HATF_MISSILE |
| SuperweaponStrategicBombing (strategic command uplink) | COMMAND_UPLINK |
| SuperweaponAtmoLenzWeapon | ATMOSPHERIC_LENS |
| SuperweaponICBMMissile | ICBM |
| SuperweaponTomahawkStorm | TOMAHAWK_STORM |

`SuperweaponParticleUplinkCannon`, `SuperweaponParticleUplinkCannonSupW`, `SuperweaponNeutronMissile`,
`Nuke_SuperweaponNeutronMissile` and `SuperweaponScudStorm` keep the vanilla hardcoded events, so they
got no new set.
