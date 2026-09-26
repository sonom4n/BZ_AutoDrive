# Changelog — BZ_AutoDrive

> ES version: [CHANGELOG.md](CHANGELOG.md) · **MIT** license · **BrigadaZ** project

Newest on top. Up to 1.1 the **driving** (*config-as-manual* model) stayed stable, and the changes were about robustness and tooling. **1.2 changes the driving brain**: besides reading the config, it replays your driving and measures how each vehicle behaves.

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
