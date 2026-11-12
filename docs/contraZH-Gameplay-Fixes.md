# Gameplay Fixes

Always on, no `Options.ini` key. All are retail bugs upstream still carries. Each entry says whether
it changes the simulation (older replays no longer play back identically) or only what is drawn.

## Failed saves clean up safely

A save that fails to load can leave update modules with unfinished queue indices. Cleanup now
restores those indices before removing objects, so a rejected save no longer crashes the game
while clearing the partially loaded match.

This only changes cleanup; simulation and replay playback are unaffected.

## Guard mode holds its ground

* A guarding unit that gets hit finishes its return or its current attack instead of chasing the
attacker and stuttering. It picks up the attacker through its normal enemy scan.
* Deploy-to-fire units (Nuke Cannon) stay deployed when:
  * their target dies and another enemy is inside the guard circle
  * they are ordered to guard the spot they already stand on
  * a target leaves range while they are still unpacking (the unpack reverses)
* Manually deployed units keep their stance.

Changes the simulation. Ported from CookieLandProjects/CLP_AI.

## Weapon bonus no longer restarts the reload

A bonus changing mid-reload (Propaganda, horde, continuous fire, promotion) used to reset the reload
to zero, which could stall a unit or let it skip the reload (Nuke Cannon plus Propaganda toggling).
The reload now keeps the fraction already served; only the remaining time scales.

## Snipe survives a weapon set change

Jarmen Kell's snipe wind-up was lost when his weapon set changed (salvage, promotion, bike). The
charge now carries over and finishes on schedule.

Note: dropping out of snipe mode entirely is data - a set change drops the weapon lock unless the
set is marked `WeaponLockSharedAcrossSets`.

## Stealth kills no longer reveal the killer

The floating bounty text is shown only to players who can see the killer: its owner, allies, and
replay observers. Stealthed snipers and stealthed transports no longer give themselves away. A
detected stealth unit still shows it.

Presentation only; replays unaffected.

## Solo LAN games respond instantly

A LAN game hosted with no other human player had a noticeable lag between a click and the unit
obeying: a full second at the start, then about a quarter of a second for the rest of the match. The
engine schedules every command a number of frames ahead to cover the network round trip, and it
seeds that round trip with a 200 ms placeholder until real measurements arrive. A solo host has
nobody to measure against, so the placeholder stood for the whole game.

A host who is alone now counts as having zero latency, and starts the match at the minimum run
ahead instead of the one second start-up value. The Generals Online build also now uses the same
latency formula for LAN as the plain build, so both executables give a LAN game the same delay.

This only changes how far ahead commands are scheduled, which the replay records as it happened,
so older replays still play back identically.

## LAN over a VPN stalls less

Players who meet over Radmin or a similar VPN play a "LAN" game at 100 to 500 ms ping with jitter
and the odd lost packet. The engine sizes its command delay (the run ahead) from the measured ping,
but it did so in ways tuned for a real LAN: the first estimate came from a fixed 200 ms placeholder
mixed into the history, the estimate was refreshed only every five seconds, one very slow player was
averaged against the next slowest and so under-provisioned, the delay was lowered as eagerly as it
was raised, and a lost packet was resent only after a fixed two seconds, freezing everyone for that
long.

Now the history holds measured round trips only and the first run ahead waits for one, the mod's
`GameData.ini` refreshes the estimate every half second (`NetworkRunAheadMetricsTime = 500`), the
slowest player alone sets the budget when they are clearly behind the rest, the run ahead rises at
once but drops only after three consecutive lower readings, and a lost packet is resent after one and
a half round trips (50 ms to 2 s). A real LAN keeps its minimum delay; the changes only matter once
pings are long or uneven.

Two modem-era habits are gone as well. Each player used to hold outgoing packets back for half the
command delay to save bandwidth, which spent half the delay budget on waiting; packets now go out at
once. And each player now reports its average ping plus twice its jitter (the standard deviation of
its round trips), so the delay covers the swings of an uneven link instead of only its average. A
steady LAN adds only a few milliseconds. Online Generals Online games keep the official behaviour.

This changes when commands execute, which the replay records, so older replays still play back
identically.

## Larger game packets

Network game packets now carry up to about 1100 bytes instead of the 476 bytes sized for a 512-byte
modem packet, so a burst of orders goes out in one packet instead of several. This applies to every
multiplayer game. The lobby already refuses a player whose executable differs, so an older build
cannot join a game hosted on this one.

## Drones stay on their leash

* The leash (twice the guard range from the master) was never checked while the master had a
victim, so drones wandered off indefinitely.
* The leash is now checked first. A drone past it drops its attack and returns.
* Unchanged: normal fights within the master's range, drones without guard range, Stinger Site
stingers.

Changes the simulation.

Note: a drone that auto-acquires targets can still start fights after returning; that is data.

## USE_OWNER_OBJECT fires from the owner

`OCLSpecialPower` with `CreateLocation = USE_OWNER_OBJECT` now has the owner carry out the delivery,
as in Generals, instead of spawning a new transport. Only powers using that value are affected.

Changes the simulation for those powers.

## More Generals Challenge personas

* The persona limit is now 24 (`GeneralPersona0` to `GeneralPersona23`). A 13th used to be dropped
along with everything after it.
* Each persona needs a `GeneralPosition<N>` button in ChallengeMenu.wnd to show. Slots without a
button are skipped instead of crashing.
* The retail twelve are untouched.

## Dying infantry do not block or catch clicks

* A dying soldier leaves the pathfind grid immediately instead of blocking non-crushing vehicles
until it sinks. Dying vehicles still block until they become hulks.
* Dead objects no longer catch mouse clicks, unless `ALWAYS_SELECTABLE`.

## Jammed units deselect properly

* Jamming deselects the unit, so the control bar no longer appears stuck.
* `UNSELECTABLE` alone blocks new selection clicks but no longer empties the current selection.
* The jam state is tracked directly, so a promotion or health upgrade can no longer leave a unit
jammed forever.

Changes selection state; affects replays.

## Portable addons no longer block building

Placing a building over your own `PORTABLE_STRUCTURE` carrier (Overlord with Gattling Cannon, etc.)
now shoves it aside like any unit instead of refusing placement. Stuck or enemy carriers still
block.

Changes build legality; affects replays.

## Buildings can be placed on rubble

A building centred on the rubble of a destroyed structure was refused as unreachable, yet the same
building shifted so its centre missed the rubble was allowed and cleared it. Placement on rubble is
now allowed wherever the builder can reach the rubble, and construction clears it.

The pathfinder never linked rubble to the ground around it, so infantry and other units that can
cross rubble were judged unable to reach it. They now path into rubble normally. Amphibious units
no longer treat rubble and broken bridge pieces as reachable.

Changes build legality and pathfinding; affects replays.

## Allied scaffolds no longer lure your builders

An ally's unfinished building showed the resume-construction cursor, and a dozer or worker sent
there drove up and stood idle because only the owner can build. Only the owner's builders are
offered it now.

The host can tick **Coop Buildings** in the Skirmish, LAN or Generals Online lobby to let allies
finish each other's buildings. The building stays the owner's. The option is off by default and is
not kept in save games. On Generals Online the host's game turns it on in the first frames of the
match, as with the other Rules tab rules.

Changes the simulation; affects replays.

## Units chasing a moving target now shoot it

* Fast units ordered onto slow moving targets used to twitch forever without firing.
* The approach point and weapon range now measure edge to edge the same way.
* An engaged unit tolerates about one pathfinding cell of drift before chasing again. Starting an
attack still uses exact range, so nothing gains reach.
* A unit that falls behind stops as soon as a shot opens up.
* Turretless units benefit most.

Changes when units fire; affects replays.

## Missiles whose target dies still explode

A guided missile whose target died in flight used to vanish mid-air with no explosion or damage.
Nuke Tank Hunter and Nuke Overlord anti-tank missiles lost the most. The missile now flies straight
to where the target was last seen and detonates there. A missile already out of fuel detonates
where it is. Torpedoes already detonated at the target's last position and are unchanged.

Missiles that retarget when their target dies, like the frog missiles, opt out with
[DetonateOnTargetLost](contraZH-Objects-and-Modules.md#detonateontargetlost) `= No`.

Changes the simulation. Contributed by MayeAmiya.

## Force fire respects line of sight

* A turreted unit with `ATTACK_NEEDS_LINE_OF_SIGHT` used to force fire at the ground through
buildings while its hull drove around them.
* The turret now holds fire until the shot is clear, as it already did for object targets.

Changes when units fire; affects replays.

## Wheeled units over water draw level

Wheeled amphibious units (Centurion) stayed frozen at the shore slope's tilt while floating on deep
water. They draw level again, as in retail. This one is an upstream regression from TheSuperHackers'
render decoupling, not a retail bug
([TheSuperHackers#3395](https://github.com/TheSuperHackers/GeneralsGameCode/issues/3395)).

Only changes what is drawn.

## Slow aircraft no longer jitter over slopes

Aircraft with `PitchInDirectionOfZVelFactor` (A-10 and other slow jets) shook in straight flight
over sloped ground. Their height control switches lift fully on and off on alternate frames, and the
nose pitch followed every switch; the slower the plane, the bigger the swing. The nose now follows
the averaged climb rate.

Only changes what is drawn.

## Hover units stop at the units they touch

* `HOVER` and `SHIP` locomotors brake slowly, so a unit kept sliding for about two cells after its
move ended. The AI counts it as idle by then and does no blocking, so it slid into nearby units and
stayed inside them.
* An idle hover or ship unit that is still sliding toward a unit it touches now stops there.
* Water units can use `Appearance = HOVER` again instead of `FOUR_WHEELS`
([#125](https://github.com/triatomic/contraZH/issues/125)).

Changes the simulation; affects replays.

## Slow units leave tunnels and transports cleanly

* A tunnel releases everyone at once on one exit point. A transport does too, unless it has several
exit paths or an `ExitDelay`. Each unit ignores collisions for one second while they spread out.
* A unit with low acceleration had barely moved by then, so the stacked units blocked each other
and crawled or stuck at the exit.
* The window now also covers the time the unit's locomotor needs to reach its exit spot, up to five
seconds. Fast units keep about one second
([#96](https://github.com/triatomic/contraZH/issues/96)).

Changes the simulation; affects replays.

## Alt-tab no longer leaves waypoint mode on

Alt-tabbing out hides the alt release from the game, so you came back in waypoint mode and every
click queued another point. Losing or regaining focus now clears every held-key mode (waypoint,
force attack, prefer selection, keypad camera).

Keys pressed in the last frame before the switch also waited in the DirectInput buffer and replayed
when you clicked back in, so a quick alt-tab at low fps still came back with alt held. The game now
drops that buffer when it gets the keyboard back.

Only changes input.

## Rubble and cliff surfaces are honoured

* The coarse route planner only ever looked at ground cells, so units with `RUBBLE` or `CLIFF`
locomotor surfaces were steered around rubble and cliffs whenever a ground detour existed.
* Locomotor sets mixing `RUBBLE` with `CLIFF` or `WATER` could not take a destination on rubble at
all; both combinations now count rubble as connected terrain.
* Climbers paid a heavy toll on nearly every cliff cell. Only cliff cells that are flat (painted
impassable in the map editor) still carry it.

Changes the simulation; affects replays.
([#120](https://github.com/triatomic/contraZH/issues/120))

## Bridges block fire from both sides

Units under a bridge could shoot units on the deck while the deck stopped fire the other way. A
sight line that crosses a bridge deck is now blocked in both directions, for every unit.

Changes when units fire; affects replays.
([#120](https://github.com/triatomic/contraZH/issues/120))

## Scaled objects keep their size under fog

A scaled object that sat in the fog before it was ever on screen was remembered at its unscaled
size, so its fogged ghost looked smaller than the real thing. The ghost now matches the object.

Presentation only; replays unaffected.
([#121](https://github.com/triatomic/contraZH/issues/121))

## AI special powers no longer see through fog

Skirmish and multiplayer AI fired special powers at targets it had never seen. These script
actions now pick only targets a human in the AI's seat could pick:

* `SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST` and its CLP `..._TYPE` variant
* `SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT`
* `TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_*`

A valid target is in view, or a structure the AI has scouted (its fogged ghost). Undetected
stealth units and buildings never count. With nothing valid, the superweapon holds its fire. The
campaign and challenge missions keep the old behavior.

Changes the simulation.
([#115](https://github.com/triatomic/contraZH/issues/115))

## Hijackers cannot take stealthed vehicles

A Hijacker no longer picks or seizes a stealthed vehicle that has not been detected, like capture
and hacking already worked. A disguised vehicle can still be hijacked. This covers hijack-guard,
hunt, scripts and an order given before the target went stealth.

Changes the simulation.
([#115](https://github.com/triatomic/contraZH/issues/115))

## Jammed AI units wait out the jam

A human cannot order a jammed unit, because jamming makes it unselectable. Skirmish and
multiplayer AI now plays by the same rule. A jammed unit ignores script orders, command-button
abilities included, and keeps doing what it was doing. The newest order it missed runs when the
jam ends. The campaign and challenge missions keep the old behavior.

Changes the simulation.
([#115](https://github.com/triatomic/contraZH/issues/115))

## Wandering units wander in every direction

Objects with `WanderAIUpdate` picked each new spot to the north east of where they stood, so they
drifted across the map in one direction. Each new spot is now random around the current position
([TheSuperHackers#3544](https://github.com/TheSuperHackers/GeneralsGameCode/pull/3544)).

Changes the simulation; affects replays.

## Longer weapons no longer wait for the shorter one

A unit that cannot move to its target (base defences, Stinger Sites, garrisoned infantry) only
checked the range of the weapon it fired last. A Gatling Cannon that had just shot infantry would
ignore an aircraft inside its anti-air range until the aircraft came within its shorter ground
range, and ordering it onto the aircraft was refused. Every weapon that can hit the target now
counts for range, and the attack cursor agrees.

Changes the simulation; affects replays.
([#66](https://github.com/triatomic/contraZH/issues/66))

## Empty jets fly their move orders

A parked jet with no ammo that was told to move, or to land at another airfield, took off, turned
round and landed again to reload, and the order was lost. Orders that need no ammo are now flown
first; the jet returns to reload once it is idle again. An empty jet flies with its return-for-ammo
locomotor, so an Aurora stays sluggish until it has reloaded.

Orders that fire or guard still need the reload first. A parked empty jet now stays parked until it
has reloaded and then takes off for the order
([TheSuperHackers#2174](https://github.com/TheSuperHackers/GeneralsGameCode/pull/2174), always on
here). An airborne empty jet lands, reloads and takes off for the order instead of flying there
with nothing to fire.

Changes the simulation; affects replays.
([#80](https://github.com/triatomic/contraZH/issues/80))

## Pathfinding

See [Pathfinding Improvements](contraZH-Pathfinding-Improvements.md) for search limits, queue retries,
shoreline clearance, group movement and bridge handling.
