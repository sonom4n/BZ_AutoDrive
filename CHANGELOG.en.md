# Changelog — BZ_AutoDrive

> ES version: [CHANGELOG.md](CHANGELOG.md) · **MIT** license · **BrigadaZ** project

Newest on top. The **driving** (*config-as-manual* model) stays stable across versions — changes are about **robustness** and **tooling**, not the driving model.

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
