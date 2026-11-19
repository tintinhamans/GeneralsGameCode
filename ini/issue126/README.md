# C27 Spartan transport configuration

Apply the four patches to the mod's `Data/INI` directory with `git apply` after installing an
engine built from this PR.

* The Air Force C27 carries six transport slots for infantry and vehicles and shows six passenger inventory buttons and an Evacuate button.
* One drop shot queues every passenger for release at the transport's ExitDelay interval. Each passenger gets its own parachute. Vehicles use `LargeParachute`, and infantry use `AmericaParachuteSelectable`.
* The drop button is disabled while empty. Loading passengers enables it again.
* The weapon has unlimited clip ammunition. Passengers supply the drop payload.
* The firing range is 25, so the plane approaches the ordered location before releasing cargo.
* The weapon keeps its existing 0.1 damage so the engine can select it for ground attacks.

The original predefined infantry OCL remains available for other weapons.

Inventory exits and Evacuate also create parachutes when the plane is in the air. Ground exits unload normally.

The last passenger leaving an airborne C27 triggers an immediate return to its airfield. Evacuate starts unloading while parked and respects the transport's ExitDelay.

Both the weapon OCL and airborne unloads use the parachute templates defined in TransportContain.
