# ObjectCreationList.ini

## Attack nugget: FireRegardlessOfOrders

* `FireRegardlessOfOrders = No` - (Default. `Yes` fires every `NumberOfShots` no matter what the
unit is ordered to do meanwhile.)

Normally new orders cancel the `Attack` nugget's barrage. With `Yes` the shots are queued on the
unit and fired from the launch bone whenever `WeaponSlot` is ready, while the unit stays fully
controllable. The delivery decal stays until the last shot.

```
ObjectCreationList SUPERWEAPON_TomahawkStrike
  Attack
    WeaponSlot             = TERTIARY
    NumberOfShots          = 6
    FireRegardlessOfOrders = Yes
  End
End
```

Notes:
* No turning or aiming, so do not use a turret weapon. `PreAttackDelay` is ignored.
* Range is not checked.
* Entering a container that blocks passenger fire drops the rest.
* Timing follows the weapon (`DelayBetweenShots`, clip reloads). Running dry with
`AutoReloadsClip = No` drops the rest.
* Firing again replaces the queue. Death drops it. Save/load keeps it.

## Attack nugget: placed shots

When a `SPECIAL_POWER_CONSTRUCT` button fires the `Attack` nugget, the weapon's `FireOCL` spawns
at the spot and rotation the player placed, instead of at the shooter with the shooter's heading.
The placement cursor's legality check therefore applies to where the object really lands. Powers
fired by the AI, by scripts or by a plain `SPECIAL_POWER` button are unchanged.

```
ObjectCreationList OCL_SpecialPowerDig
  Attack
    WeaponSlot    = SECONDARY
    NumberOfShots = 1
  End
End

Weapon PlaceTunnelWeapon
  FireOCL        = OCL_CreateSneakAttackTunnelStart
  PreAttackDelay = 3795
  ...
End
```

Notes:
* Only `FireOCL` moves. `ProjectileDetonationOCL` is unchanged.
* Each of `NumberOfShots` spawns at the placement. A new order drops the placement with the
attack, unless `FireRegardlessOfOrders = Yes`. Save/load keeps it.

## DeliverPayload formations

`FormationSize` and `FormationSpacing` now work when an `OCLSpecialPower` uses
`CreateLocation = CREATE_AT_LOCATION` or `CREATE_ABOVE_LOCATION`. When the spawn and
target share the same X/Y coordinates, the formation uses the default delivery heading
along positive X. `TargetOffset` also uses that heading.

## ObjectCreationListExtend

See [Extend Blocks](contraZH-Extend.md#objectcreationlistini).
