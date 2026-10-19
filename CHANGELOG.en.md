# Changelog — BZ_AutoDrive

> ES version: [CHANGELOG.md](CHANGELOG.md) · **MIT** license · **BrigadaZ** project

Newest on top. Up to 1.1 the **driving** (*config-as-manual* model) stayed stable, and the changes were about robustness and tooling. **1.2 changes the driving brain**: besides reading the config, it replays your driving and measures how each vehicle behaves.

---

## v1.2.4

**Fixes that came from using the framework in a real mod** (BZ_Trader, traders that travel in vehicles with stops): the panel no longer opens on its own when you close a trader's menu, and a stop requested by another mod on a mark followed by a reverse maneuver no longer throws the route off.

- **Fixed (admins): closing a trader's menu with ESC opened the AutoDrive panel.** It happened with the Expansion Market (or any other mod's menu), the first time with each trader. Now the action that arrives right after closing another mod's menu doesn't open the panel; HOME opens and closes it as always.
- **Fixed: after a stop requested by another mod, the vehicle could lose its recording and end up stopping somewhere else.** If the recording had a reverse maneuver right after the mark (marking while going forward and leaving in reverse), resuming could make the recording jump hundreds of meters ahead: the vehicle followed only the waypoints, without the recording, and the next stop ended up far from its mark. Now the recording is never placed more than 24 m from the vehicle (if something doesn't add up, it re-anchors by the real position and heading), a forward section never consumes the reverse one, and after every recovery the route cursor and the recording cursor are re-synced with the real position.
- **Improved: reverse near a mark.** The converter counts a change of direction only if the vehicle moves at least 3 m in the new direction: shifting into R for an instant while standing still no longer splits the route. **Routes already converted don't need to be converted again**: when loaded, a reverse section shorter than 3 m between two forward ones is merged automatically. **Recording tip:** always make marks (NUMPAD 4) while in forward; if you come from reverse, shift into first, brake, and then mark.
- **Improved: the log.** Every recording line (`[Cinta]`) says which vehicle it belongs to, and a `DESFASE` line warns every 5 s if the route cursor and the vehicle don't match.

---

## v1.2.3

**Important fixes for any server, and much more accurate motorbikes.** Two bugs that 1.2.1 and 1.2.2 had, plus a set of motorbike improvements measured on Badlands and Chernarus.

- **Fixed: AutoDrive vehicles stayed saved in the world when the server was shut down.** If the server closed cleanly (for example, with the window's X) while a route was running, the vehicle was saved to persistence; on the next start it was still there, driverless, on the route, and the next run crashed into it. Now nothing AutoDrive creates is saved: vehicles, drivers, guards, crew and stop signs. **Vehicles already saved by previous versions can't be told apart from any other vehicle: delete them once by hand** (for example, with COT).
- **Fixed (admins): ESC could become the panel key.** After closing another mod's menu with ESC (for example, the Expansion Market), every ESC opened or closed the AutoDrive panel and the pause menu never came back: you had to kill the game from Task Manager. Menu keys (ESC, ENTER, BACKSPACE, TAB, SPACE, Windows) can no longer become the panel key.
- **Motorbikes: stops requested by another mod** (for example, a trader that stops to sell, via `HoldRunner`): the bike stops exactly where the recording stopped (a single stop; before, it stopped 4 m early and again at the mark) and, when released, pulls away at once **in 1st gear** (before, it started in the recorded gear and took seconds).
- **Motorbikes: no sideways correction at walking pace (0-3 km/h).** At that speed the handlebar responds differently; before, starting from a stop slightly off the line could spin the bike around and end in a relocation.
- **Motorbikes: steering follows the shape of the recorded path** instead of replaying the recording's keyboard taps. With a keyboard the steering is either full or zero, and the bike, which responds more than the player, gained a little extra turn on every tap: in tight curves at 60-75 km/h it cut 1.5-2.5 m inside. Measured on a Badlands route: from 2.2-2.4 m to under 1 m in one curve and from 2.1 to ~1.3 m in another (with a terrain bump that is in the recording too); on Chernarus the three motorbike routes also improve (Enduro 0.37 → 0.30 m, Moped 0.86 → 0.46 m).
- **Motorbikes: the bike measures how it responds to the handlebar** in 10 km/h bands while driving, and saves that per route and vehicle (`BZ12_AUT_<route>_<class>.json` in the AutoDrive profile) for the next run and the next server start. The first run of a new route starts from what it measures in the recording.
- **Motorbikes: the lean-based law works from 75 to 85 km/h** (before, 65 to 85): below 75 the bike turns almost without leaning to equilibrium and that law pushed it inside.
- **Improved: the log says what was hit.** "Wheel in the air" and every sudden slowdown without own braking (more than 10 km/h lost in 0.3 s) report the last contact (object class or model) and the position.

---

## v1.2.2

**Urgent fix: compatibility with other mods.** 1.2.1 did not compile on a server with certain mods loaded: the server would not start (`Can't compile "World" script module`).

- **Fixed: the server would not start with the CarPack updated for 1.30** (and with other packs that define classes with short names, like `door` or `plate`). Those mods ship vehicles or objects whose internal name is a short word (`esc`), and in DayZ every config class is also a reserved name for scripts. Three AutoDrive variables had the same names and the compiler rejects that: `Variable name 'esc' already used as type name`. They now have names of their own. **Nothing changes in driving or behavior.**
- **Improved: how we prevent it from now on.** Before every release, the framework's variable names are checked against the class names of more than 1,400 Workshop PBOs (226 mods), and that check is part of validation.

---

## v1.2.1

**DayZ 1.30, motorbikes, and a much more precise framework.** The published 1.2 doesn't compile on 1.30: 1.2.1 runs on both versions with a single PBO. It also brings motorbikes (experimental) and a big round of precision work: a route only counts as complete if it really ends well, and the vehicle follows your line at direction changes and stops too.

### DayZ 1.30

- **Tested on launch day (15/10)** on the 1.30 stable release, without Expansion and with Expansion 1.9.79: eAI driver in cars and on motorbikes, and retirement with a player seated as a passenger (the vehicle waits until they get out).
- **New: a single PBO for 1.29 and 1.30.** The same file compiles and runs on both versions.
- **Changed: lights use the new 1.30 system.** On 1.29 they work as before.
- **Fixed: removing a vehicle with someone seated.** On 1.30, deleting an occupied vehicle crashes the server. The framework now waits until the vehicle is empty before removing it. If the one seated is a player, the vehicle stays braked and isn't deleted until they get out.
- **Fixed: the route list on 1.30.** With 1.2 it came out empty. It needs the server to start with a **relative** `-profiles` (e.g. `-profiles=profiles`): with an absolute path 1.30 doesn't allow listing files, and the RPT warns at startup (`DayZ 1.30 cannot list files under an absolute -profiles path…`).
- **New: motorbikes (experimental).** The 1.30 Moped and Enduro aren't cars (they don't use CarScript) and get their own driver: separate front and rear brakes (motorbikes have no reverse), a lean and damping law measured per bike, and arrivals within 1-3 cm. **Walking the bike** (getting off and pushing it, forward or backward) is recorded and replayed with the animation. If a bike gets stuck, it walks back 3 m and tries again. See the manual, §9B.
- **Improved: the motorbike follows your line at high speed.** A bike takes half a second to lean after the handlebar turns; correcting it like a car made it oscillate and run wide in fast curves (up to 3.7 m). Above 65 km/h it now corrects by leaning according to the upcoming curve of the path. Measured with the Enduro up to 120 km/h: maximum offset 0.4-0.56 m over the whole route.
- **Improved: the motorbike's final stop.** It brakes to the point while measuring, in that same braking, how hard the bike really brakes. It arrives within 3-4 cm even when coming in fast (before, it could overshoot by up to 0.7 m).
- **Improved: walking the bike.** If you turned the bike while pushing it in the take (backing it out of a spot and pointing it another way), the NPC reproduces that turn and corrects if it drifts off the line. When the walk ends it pulls away at full throttle, after keeping any wait you made.
- **Fixed: a speedometer that isn't a number.** On 1.30 a freshly spawned vehicle can have no valid speedometer reading for a few seconds (we saw it on the M1025, which has an automatic gearbox), and the route got stuck at the start. The framework now uses the vehicle's real speed while it lasts and says so in the RPT: `[Velocimetro] GetSpeedometerAbsolute no da un numero … el framework usa la velocidad del cuerpo`.

### When a route is complete (the verdict)

- **Changed: `ROUTE_COMPLETE` is strict.** A route only counts as complete if all three hold: the vehicle came to **rest within 0.25 m** of the take's final point, it **didn't hit any object**, and **AutoRecovery didn't have to move it** (maneuver or relocation). Soft AutoRecovery restarts don't invalidate it, but they are listed. The 1.2 1 m safety net no longer approves a route.
- **Changed: the end of a route is decided with the vehicle AT REST.** The `FIN` line and the end events come out once the vehicle has stopped (at most about 3 s after arriving), with the line `REPOSO tras T s: a X m de la pose final (longitudinal …, lateral …)`. Same for motorbikes.
- **New: the reason for a `ROUTE_INCOMPLETE`**, on the same line: how far off it stopped, what it hit and where, or which physical intervention AutoRecovery made.

### Driving: more precise

- **Improved: the final stop.** The vehicle config's brake turned out to be optimistic (8.5 m/s² declared vs 5.5 real on a Sedan). The end of the route is now planned with **the braking you measured yourself in the take** (`FRENADA de la toma` in the log), and each direction change with the braking at that point (`FRENADA del cusp`). Arrivals: 0.10-0.15 m on every test route. The take's braking is now also measured on short stops (from 8 km/h).
- **Improved: direction changes.** The vehicle follows your line at low speed and in reverse too: at each gear change it stops within centimeters of the point (before, 0.5-1 m past or off to the side) and the reverse leg starts where it should. In the last meters before each gear change it corrects lateral drift more strongly.
- **Fixed: recorded stops in the middle of a leg.** They are held for the time you recorded and the vehicle moves on; they never trap it. And a leg only ends at your **last** stop before changing gear, not an earlier one.
- **New: braking at the end of each leg.** If the take runs out (or keeps crawling without brake) while the vehicle is still rolling, the framework brakes it to the gear-change point.
- **Fixed: throttle and brake at the same time.** While the take is braking, the framework no longer adds throttle to reach a recorded speed.
- **Improved: takes with position corrections.** If the server pulled your position back while you were recording (a "rubber-band" jerk), the vehicle no longer follows that backwards jump.

### Recording and the Wizard

- **Fixed: the recorder's clock.** New takes store real time. A take with a broken clock (recorded with earlier versions) is not converted, and the Wizard says so.
- **New: take warnings** on conversion, in the Wizard's language: `(!) The take has N recorded rewind(s)` (the server corrected your position while recording: if the NPC drifts there, record that stretch again) and `(!) At N gear change(s) the vehicle rolled forward without brake before shifting` (when recording, keep the brake pressed until the other gear is in). Crashes in the take are flagged too.
- **Changed: the point of a direction change** is placed where the vehicle came to rest in the take, not where it dropped below 1 km/h. Applies to routes you convert with this version.
- **Changed: new takes record more data per frame** (lean, contacts, real clock). That's why a part recorded with 1.2 doesn't join one from 1.2.1 in **[3] Merge recording segments**: record all the parts with the same version.

### The panel

- **Fixed: on 1.30 the panel closes with HOME** (or the key you assign in Controls), the same key that opens it. **ESC** also closes it, without opening the game's pause menu.
- **Fixed: after closing it with ESC, HOME opens it again on the first press.** On 1.30 you had to press the key twice.
- **New: SPAWN EMPTY VEHICLE checks that the spot is free.** If there's already a vehicle at that point, it creates nothing and says so on screen: `Spawn point occupied by <class> (N m away)`.
- **Improved: when an empty spawn can't be done** (spot occupied, a route that doesn't load or has no waypoints, a class that doesn't exist), the warning shows on screen and the `LOADING` row goes away at once. Before, it waited 15 s.
- **Fixed: the mouse wheel over the route list.** Each notch asked the server for the route's summary card again, and the server re-read the route from disk. Now only a click asks for it, and the scrubber no longer jumps back to the start when you turn the wheel.

### Logs and diagnostics

- **New: `Diagnostics`** in the settings (off by default): with diagnostics off, the RPT of a server running routes shrinks by more than 90%. Load, end-of-route, error and AutoRecovery lines stay.
- **New: optional black boxes** (`BlackBox`, off by default), with rotation: `BlackBoxKeepPerRoute` keeps the last N per route (10 by default).
- **Improved: the crash line** (`*** CHOQUE ***`) says what it hit (the object's class, or the terrain), in which zone of the vehicle, and which wheels were touching the ground just before.
- **Improved: when the framework stops a vehicle for safety** (`SAFE_STOP`), the log says why.
- **New: a warning for old-format routes** (no `_wp.csv`). Such a route loads all at once and freezes the server while it reads it. Now the RPT says so, and so do the connected admins' notifications (per `AdminSystemNotifications`): `route <file> is in the old format (no _wp.csv): loading it froze the server for N s…`. Convert the take again with the Wizard and the route loads in small slices.
- **Fixed: `[LOADTIME]` gives real milliseconds.** Up to 1.2 the number came out 10,000 times too big.
- **New: panel diagnostics.** With `Diagnostics` at 1, an admin's client log records when the panel opens and closes and why (`[Reproductor] ABIERTO` / `CERRADO (tecla|ESC|boton|otro)`). Useful if the key doesn't respond.

### For mods that use AutoDrive (the runner API)

- **Changed:** the end of a route (`PollEvent`, `RunnerRouteEnded/Completed/EndReason`) arrives at rest, up to ~3.5 s after arriving, and `RunnerRouteCompleted()` uses the strict verdict.
- **New:** `RunnerReposoFinM()`, the distance at rest used for the verdict (cars and motorbikes).
- **Changed: new fields in the log and in the black boxes.** The `LEG n cerrado` line adds the lateral offset (`lat`, between `remFr` and `rem`) and the distance to the phase's last recorded stop; the car's black boxes add columns (contacts and `frenoFin`). If a tool of yours reads them by position, check it.
- **Changed: the motorbike's black box adds four columns at the end** (`roll_des`, `mezcla`, `w_cmd`, `vol_cam`); the existing ones don't move.

---

## v1.2

**A new driving brain: it replays your driving and learns from your takes.** It's the biggest change since 1.0. The framework still reads the vehicle's config, but now it also **replays what you did at each point of the road** and **measures** how each vehicle behaves, instead of assuming it.

### Driving

- **New: the tape.** When you convert a take, the Wizard stores your complete driving —40 samples per second: steering, throttle, brake and gear— indexed by **distance traveled**. The NPC replays what you did **at that point of the road** and lets the vehicle's physics do the rest. It flies where you flew and brakes where you braked.
- **New: the envelope, each vehicle's measured profile.** The framework learns from your own takes how hard the vehicle brakes, how much it turns for each steering angle, and whether it creeps on its own. There is **one profile per vehicle** (`envelope_<class>.csv`), shared by all its routes, and every take you convert **adds** to it. When what the config declares and what was measured disagree, **the measurement wins**.
- **Changed: no vehicle creeps on its own.** Measured on 43 vehicles: none moves forward at idle without throttle. The default went from 8.6 km/h to **0**, and each take's value **travels inside the take** (in the `_hdr.json`) instead of in a separate file that could get lost or go stale.
- **Changed: the minimum throttle to pull away is a measured value.** It used to be a fixed 0.35 for everyone; now it's **0.13**, the median of 9 measured vehicles (they range from 0.05 to 0.25). On the road **you won't notice** (one launch tick); where it matters most is a hill start.
- **Improved: direction changes.** When leaving a reverse leg forward on a long route, the vehicle could sit braked for about 7 seconds before pulling away. Now it leaves in about 2.5 seconds.
- **Fixed: on routes that run without a tape**, a vehicle stopped after a crash could stay at full throttle without moving until the route was given up. *(Found and verified on the test bench; routes with a tape were not affected.)*
- **Improved: arriving at the end of the route.** The final brake is armed earlier the faster the vehicle arrives (with the braking distance its real speed needs). And if it still stops less than 1 m from the point, the route counts as complete, with a warning in the RPT: before, it was "incomplete" and the vehicle stayed frozen with the driver inside until someone stopped it.

### When it gets stuck: a much more robust AutoRecovery

*Everything in this section came out of hours of real service with several vehicles at once.*

- **New: it detects a jam by progress, not by speed.** A vehicle that pushes something or slips against a curb reads half a km/h without moving, and before that went unnoticed. Now 10 s without progress at full throttle is a jam, and the recovery starts right there.
- **Changed: a route is only declared stuck when the recovery is exhausted**, and the log says why. Before, it could be declared stuck without having tried anything, or in the middle of a recovery step.
- **New: the recovery works on reverse legs** (backing out of a bay, backing in). The first step moves away in the right direction and **doesn't move if there's something on the side it would go to**: another vehicle, a shelter, a post, a guardrail.
- **Improved: the "bogged engine".** Full throttle with the engine at idle is also what you see when the vehicle is pushing something. If pulling away in first gear doesn't get it out, it stops insisting and moves on to the recovery right away.
- **Fixed: a recorded stop no longer counts as a jam.** Leaving a long stop could trigger a recovery.
- **Fixed: a vehicle that stopped centimeters short of a recorded stop** crawled for ~10 s (and at a stop with a shelter behind it, the recovery backed into it). Now it serves the stop where it stopped.
- **Fixed: leaving a direction change.** One in three exits waited ~11 s before pulling away; now all of them leave in 4-5 s.
- **Fixed: after a stop right before a tight curve**, the vehicle could take the curve late, run 1-3 m wide and get stuck against the edge.

### Launch, loading and spawn

- **Fixed: the vehicle appears exactly in its place.** Before, it spawned 0.2-0.4 m off (depending on the model) and dropped up to a meter. You could see it on routes recorded in segments, where a segment starts where the previous one ended.
- **New: if the starting point is occupied**, the vehicle waits for it to clear (`WAITING` in the panel) instead of appearing off to the side.
- **Improved: loading a route no longer causes stutters.** Loading is split into ~1 ms slices per frame; before, a single frame could take up to 75 ms.
- **Improved: the empty-vehicles panel.** The card shows up as soon as you click (with `LOADING`), and a second click no longer creates a vehicle on top of the first.
- **Fixed: installing from scratch.** The framework creates its profile folders. Before, on a new server, the log of a player driving tried to write into a folder that didn't exist, every frame.
- **New: startup warnings for a new install.** `AdminSteamIDs is empty: nobody is admin…` if there are no admins yet, and `No routes yet…` if there are no routes. A template route is no longer generated.

### The vehicle on the route

- **New: tamper-proof vehicle.** While the framework drives it, players can't remove its parts (wheels, doors, battery, spark plug, radiator…). Before, with the vehicle stopped at a stop or at the end of the route, they could: two wheels fewer and it was stuck forever, and "remove wheel" competed with "get in" at the driver's door. It comes on (`VehiclePartsLocked`); an event changes it (`set_vehicle_parts` with `"unlocked"`), for example when the reward is the vehicle, and from a mod, `SetVehiclePartsLocked`. The cargo stays accessible. If the driver dies and the vehicle is left as loot, it unlocks on its own.
- **New: notifications, who sees what and where.** Automatic notices (arrivals and administrative ones) are for admins only, as before, but now you choose for each type whether they reach you on screen, in chat, both or not at all (`AdminArrivalNotifications`, `AdminSystemNotifications`). And for players, the `ui_broadcast` event chooses the recipient (everyone, those nearby, the passengers) and whether it goes on screen or in chat. Details in §13.3 of the manual.

### Wizard and files

- **Changed: conversion produces four files** per route —`_cinta.csv` is added— and updates the vehicle's profile. **The vehicle's profile travels inside the route**: if you move the route to another server, the four files are enough. *(A route converted before this version still needs its vehicle's profile next to it.)* Details in §6B and §6C of the manual.
- **New: Python included.** The Wizard ships with its own Python; you no longer need to install it separately.
- **Changed: the first time you open the Wizard** it asks for your server's routes folder, in the language you chose, and won't continue without it: it no longer assumes a default folder.
- **Removed: the trajectory editor.** It was taken out of the package. The way to have the same route on another vehicle is to **record it with that vehicle**: that way the framework learns that car's physics.

### For modders

- **New: the runner API, documented** (Appendix B.7 of the manual). For a mod that drives a fleet: launch one vehicle per runner, check whether it's blocked, recovering or just crashed, whether it's waiting for its starting point, and which log lines to watch.
- **Changed: `frame_to_route.py` and `transport_v1_to_route.py` require `--profile`** (the server's routes folder); they no longer have a default. The Wizard passes it for you.
- **Changed: crashes are logged once per episode** (back-to-back hits are counted together, with a summary), and there's no longer a cap that left a vehicle silent after the eighth one.

### Compatibility

- **Everything new comes switched on: there's nothing to enable.** To go back to the previous behavior on a server there are switch-off files (§4 of the manual); an empty file does nothing.
- **1.1 routes still run.** To get the tape, reconvert the **original recording** with the new Wizard.
- **Routes imported from BrigadaZ Transport v1 run without a tape**: a v1 take doesn't have the complete driving, only the line.
- **The horn and the lights are no longer recorded** with the take. `HornMode` now defaults to `off` (`stops`, `finish` and the `horn` verb still work); `LightsMode` stays on `auto`, now also follows the time of day during the route, and looks the same on every client. For an exact spot, use the `horn`, `lights_on` and `lights_off` verbs in an event.
- **The mod's declared version is 1.2.0** (`config.cpp`).

### Security

**The admin panel is now fail-closed.** ⚠ **Behavior change (breaking) for servers with an empty `AdminSteamIDs`.**

- **Changed: with `AdminSteamIDs` EMPTY, NOBODY is admin now** (before: everyone — convenient for testing, but a freshly installed public server sat with the panel open to any player until the JSON was edited). When updating, **add your Steam ID to `AdminSteamIDs`** in `$profile:BZ_AutoDrive/BZAutoDrive_settings.json` or the panel will no longer open. The server now logs every denied access (`[Panel] acceso denegado a <SteamID>...`, rate-limited) so the diagnosis is immediate.

---

## v1.1.1

**Robust launch in reverse maneuvers.** Point patch on v1.1 — same driving model, no on-route behavior change.

- **Fixed: the engine could rev without moving when starting a reverse maneuver.** On routes with reverse maneuvers (swaps, repositioning), a vehicle could get stuck revving in place after finishing a run and respawning for the next one. The fine-maneuver control floored the throttle on the standstill launch and, on a cold respawn, the drivetrain could decouple —the wheel spun but the vehicle didn't move—. Now the reverse standstill launch is handled by the proven, stable startup, and the fine-maneuver control takes back over —with its extra precision— as soon as the vehicle moves. Clean launches, same tight stops.

---

## v1.1

**Tighter precision + robust startup + player-panel improvements.** Same driving model (*config-as-manual*), tuned: **finer stops** (checkpoints and endpoints), **clean launches**, and a fuller live dashboard.

### Stopping precision — most of all on heavy vehicles

- **Endpoints down to centimeters.** New frame-by-frame predictive braking that takes the recorded speed as its reference — the final stop nails typically **< 5 cm** (was < 0.5 m), consistent across the whole fleet. The gain shows **most on heavy vehicles** (trucks, armor, tanks), which used to overshoot the most.
- **Tighter checkpoints and reversals.** Direction changes (forward↔reverse *cusps*) nail the point before reversing — typically **< 0.2 m** — without over- or under-shooting. (On the tightest turn, very large vehicles stay a touch looser, by geometry.)

> **Tested on a broad bench:** sports cars, vanilla, military and modded vehicles.

### Startup

- **Clean launches, every time.** Fixed an intermittent case where a vehicle could get stuck on launch —engine revving but not moving—. Cause: the spawn-stabilization handbrake didn't always release when control was handed to the NPC. Vehicles now pull away smoothly in every case, on any vehicle.
- **Routes that start from the same spot (hubs).** New spawn *guard*: when several routes share a start point —a terminal with multiple trip options, or takes recorded in a chain from the end of another— vehicles no longer spawn on top of each other. They shift sideways until they find a clear spot, and self-spread when several spawn back-to-back. Enables depots/terminals with multiple destinations.

### UI / Player

- **Live speed and coordinates** in the runners panel: km/h and position (X Z) per vehicle, in real time.
- **Coordinates on the bottom-bar scrubber:** scrubbing a route shows where each waypoint falls on the map (and where it would spawn with **Spawn here**).
- **Compacted runner list:** active vehicles stay pinned to the top, with no gaps when one finishes.

> Workflow note: the recording technique **is** the behavior. The NPC faithfully replays the recorded speed profile — for gentle launches, record gentle starts; for aggressive getaways, record them that way.

---

## v1.0

Initial release. Autonomous NPC vehicle driving from a single recorded lap, reading the physics each vehicle already declares in the engine (*config-as-manual*, no machine learning, deterministic). Includes: authoring pipeline (record → convert → run → orchestrate), visual route editor, emergent graph-based routing, scenario DSL + DayZ-Expansion-Quests integration, and a multi-vehicle UI/player with hot route loading. See [README.en.md](README.en.md).
