# BZ_AutoDrive — AI Knowledge Pack

> **For the AI reading this:** you are an assistant who is going to help an admin/modder with **BZ_AutoDrive**, a framework for autonomous vehicle driving by NPCs in DayZ (Enfusion engine, Enforce language). This document gives you the COMPLETE context to **configure, program, write, modify and keep researching**. It is more exhaustive than the manual (for humans): it is your operational knowledge base. When you help, cite exact paths and classnames, respect the gotchas (§12) and verify against the current code before asserting.
>
> *EN version — **2026-09-25 · BZ AutoDrive 1.2**. This pass rewrites the pack around the **1.2 brain** (`scripts\4_World\bz12drive\`, §5), the one that runs by default. What describes the **1.1** brain (Stanley + the inverse model of `BZBusService`) stays as **LEGACY in §5-L**. What is new: the **tape** (`_cinta.csv`, the 40 Hz driving indexed by arc) · the **envelope** per class (what is MEASURED overrides the config) · the **creep derived from the `coast2`**, traveling in the `_hdr.json` · the **launch floor** (fleet prior 0.13; calibration exists on the bench, §5.7) · the conversion writes **four** files (§7.0) · the **1.2 gotchas** (§12.0) · the **AutoRecovery** that detects a jam by lack of progress and also recovers in reverse (§5.9) · the **runner API** (§11) · the **guide to port the framework to another game** (Appendix E). The trajectory editor was **dropped**. History of earlier versions in the CHANGELOG. MIT.*

---

## 0. How to use this document

- **Get oriented** with §1 (what it is) + §2 (status, where we are).
- To **configure** a route/scenario → §6 (config) + §7 (wizard/pipeline) + §8 (events) + §9 (quests).
- To **program/extend** → §11 (code patterns) + §4–5 (architecture/control) + §12 (gotchas).
- To **keep researching** → §14 (methodology) + §15 (frontier) + §5 (control internals).
- **To GUIDE a user live** through the core loop (record → convert → play), assuming they **do NOT program** → follow the **Walkthrough §7.G**: ONE step at a time, confirm before advancing, translate everything into concrete actions (which key, which file, what to look at) and verify the files on disk. To **MAP an area** (join route graphs) → **Appendix D.7** (⚠ not available in 1.2: its tools are not in the package).
- **To understand HOW 1.2 drives** → §5 (the 1.2 brain). §5-L is 1.1, legacy. **The 1.2 gotchas** → §12.0; read them before you diagnose anything.
- **To PORT the framework to another game** (7 Days to Die / Unity + C#, or any engine with vehicle physics) → **Appendix E**: a step-by-step guide written so that a coding AI (Codex, ChatGPT, Claude) can guide the port, with the code of a proof of concept that already drove on its own in 7DTD, measurable milestones, the file formats and the brain specification.
- **Golden rule:** this pack reflects the state as of **2026-09-25 (1.2)**; the code is the truth. Before asserting a function signature or a default, confirm with a grep of the cited file.

---

## 1. What BZ_AutoDrive is (executive summary)

A **piloting layer on top of eAI** (DayZ-Expansion-AI). eAI provides the NPC body (walking, getting in/out of a car) but drives naïvely (infantry navmesh, steering = angle/π, **always 1st gear**, zero config reading). BZ_AutoDrive overrides its driving outputs so an NPC drives **any vehicle** by following **a recorded human demonstration**, reading the vehicle's **declared configuration** ("config as driving manual") and **replaying the take in space** (in 1.2: the *tape* + lateral pure-pursuit + an inverse model derived from the config and corrected by what is **measured** in the *envelope* — §5). **It does not train per vehicle**: it reads the config and **measures** from your own takes.

Three ideas: (1) **demonstration = route mapping** (DayZ has no road network; the recording supplies it); (2) **config = driving manual** (the `config.cpp` declares torque/gearbox/steering; the framework reads it, does not guess); (3) **interpretable classic control** (deterministic, inspectable, zero-training).

---

## 2. Project status — WHERE WE ARE (2026-09-25, BZ AutoDrive 1.2)

**The north star (September 2026): the product is the FRAMEWORK.** BZ_BusDrive, BZ_Convoy, BZ_HeliDrive and BZ_SeaDrive are **separate** mods that use it as a dependency. The split of responsibilities, which is the rule you will use to say no:
```
the FRAMEWORK  replays the take and GUARANTEES THAT THE VEHICLE DOES NOT STAY STOPPED
the MOD        provides the INTENT: when to stop, who to yield to, which mission, with whom
```
If a modder asks you to put logic from their game inside the framework (a right-of-way yield, a timetable, a mission rule), that goes in **their** mod, which consumes the framework — not in the framework.

**1.2 is a new brain.** `BZBusService` remained as a **shell** (spawn, events, quests, UI, lights); the **driving decision** is made by the stack in `scripts\4_World\bz12drive\` (§5). What is new, one line each:

- **The tape** (`_cinta.csv`): your driving at 40 Hz, indexed by **arc traveled** (not by clock). The NPC replays the input you had **at that point of the path** and lets the engine physics drive. It is the difference between "following waypoints" and "replaying your driving".
- **The envelope** (`envelope_<clase>.csv`, one per vehicle **class**): what the framework **measured** from that vehicle. Each data item carries provenance (CONFIG / DERIVED / MEASURED / FALLBACK) and **what is measured overrides what is derived from the config**.
- **The creep is 0.** No vehicle in this engine creeps on its own at idle (**43 of 43** measured). It is derived from the `coast2` of the take itself (the decel in neutral when almost stopped: > 0 = does not creep) and **travels in the `_hdr.json`** (`CreepKmh`, `CreepSource`, `CreepSamples`, `CoastAtRest`).
- **"Is it stopped?" reads the measurement** (`idleCreepKmh`), not the creep target (`creepKmh`). Times the launch ramp armed with the vehicle rolling: from 48 to 0.
- **The launch floor is MEASURED** with a bench calibration (§5.7), which in the published 1.2 is **not available to the user**. Uncalibrated: **fleet prior 0.13** (median of the measured values; before, it was a hand-picked 0.35). Effect on the route: **one ramp tick**. Where it matters: the **stop on a slope** (not measured yet).
- **The conversion writes FOUR files** + the class envelope (§7.0).

**Validated in 1.2 (DEV, September 2026):**
- **Svergino ×5 — 5/5 `ROUTE_COMPLETE`**, final stop ~0.10 m, 0 crashes: **no-regression** with all the changes from 914-930 on top. *(Project criterion for that route: ≥ 5 runs, because the historical baseline passed 1 of 3.)*
- **Floor counterfactual** (5 × 0.35 forced against 5 × 0.15 measured, same route): 5/5 both; the floor only moves the first tick (0.4 s at 5 km/h, complete separation, p 0.008) and the run absorbs it.
- **Cusp (direction change reverse → forward), fix 936:** Pavlovo, 5 km/h at 2.3-2.6 s after the leg change (before: ~11 s with throttle 0 / brake 1).
- **Fix 938 (NULL pointer):** Pavlovo 4/4 `ROUTE_COMPLETE`, 0 exceptions (before: 372-719 per session in the harness).
- **Floor measured in 9 classes**, from motorcycle to truck (table in §5.7).
- **Arrival (endpoint) measured on more than 40 classes** (table in the manual §17.1): since 2026-09-17, 52 arrivals, median 0.11 m, 87 % ≤ 0.25 m; from 2026-09-02 to 09-16 (before the 40 Hz entry and the lateral term), 430 arrivals on 41 classes, median 0.19 m, 77 %. The final brake parks ~0.10 m short on purpose.

**Open:**
- ✅ **1.2 brain, tape, FF-primary steering and player→runner BY DEFAULT** (decision of 2026-09-23). Until then all four depended on **presence** files that nobody created (`CEREBRO_12.txt`, `BZ12_CINTA.txt`, `BZ12_FF_PRIMARIO.txt`, `REPRODUCTOR_LANZA_11.txt`): without them the subscriber ran 1.1, or 1.2 without the tape, or the old steering, and the player (the HOME panel) sent the route to the test bench (which is not published) and "load" did nothing. Now they are **turned off** on request with a file **with text** (`CEREBRO_11.txt`, `BZ12_SIN_CINTA.txt`, `BZ12_SIN_FF_PRIMARIO.txt`, `REPRODUCTOR_LANZA_HARNESS.txt`), and an empty file does nothing. **Check in `BZBusService.c` (`Cerebro12AlCargar`, `PrepararCarga12`, `BanderaConContenido`) what the code in front of you says** before asserting what runs.
- ✅ **The whole envelope travels in the `_hdr.json`** (`Envelope` + `EnvelopeClass`): a route taken to another server doesn't start blind. The class profile wins if it exists; the hdr one is the fallback (§5.10).
- **Stop on a slope** not measured yet, and the instrument `[C12] PRIMER GAS tras el hold` (= "FIRST THROTTLE after the hold") not exercised yet (they need real BusDrive stops).
- **Floor prior by mass**: evaluated, NOT for now. Pre-registered falsifier: ≥ 15 calibrated classes, ≥ 4 of them over 3 t, and the straight line must win the leave-one-out whichever point is removed.
- **Tapeless reverse**: in the harness (which does not load the tape) reverse is reactive and can crash on a straight; with the tape, the tape also possesses the reverse.

**Dropped:** the **trajectory editor** (2026-09-23). It contradicted the thesis of *not editing takes*. It is replaced by an **event viewer** (see stops and events over the trace, write the event chain, export) that goes **only to GitHub**. Do not recommend it: it is not in the package.

**What was already validated in 1.1** (out-of-sample endpoint on 8 vehicles, reverse on 4, AR_OnWay on 5, the Quest integrations) is in §5-L and in the CHANGELOG. The 1.2 endpoint **keeps the logic** of 1.1 (`BZEndpointSlam`: "in v1.1 it nailed ~8 cm and that worked"); there is **a single** arrival tolerance, `BZDriveTol.LLEGADA_M = 0.10 m`.

**Future vision:** LLM-driven NPC (the framework already has agent shape: DSL = actions, graph = navigation, config + envelope = perception). See §15.

---

## 3. Codebase map

**Source:** the GitHub repository (`scripts\`, `tools\`, `config.cpp`, docs). Everyone packages it with their own pipeline and signs it with their own key (README, "Build from source").
**Installed package:** `<server>\@BZ_AutoDrive\` (`addons\` + `keys\` + `tools\` + docs); the data goes in `<server>\profiles\BZ_AutoDrive\` (§7, "Where the data lives"). **Client:** the same `@BZ_AutoDrive` (Workshop). **With two servers** (e.g. a test one on another PC): both must run the **same PBO** before the client connects; the Wizard can copy each converted route to the second one (`ServerBMirror`, option [6]).

**Scripts (PBO), by Enforce scope:**
- `scripts\3_Game\` — early helpers: `BZBusCommon.c`, `BZBusRPC.c` (RPC enum, incl. `RECEIVE_TOAST`), `BZBusStops.c`, `BZGearRangeTable.c`, `BZPathLogCommon.c`, `BZCleanupConfig.c`, `BZBusClientManager.c`.
- `scripts\4_World\` — **the core**: `BZBusService.c` (server-side singleton; spawn, Tick, control, events/verbs `ExecuteAction`, quest hooks `OnQuestStart`/`CheckQuestBots`/`BoardQuestBots`; ~6500 lines), `BZBusCarScript.c` (the `modded CarScript` override-last; also the custom **lights NetSync** `m_BZLightsWanted` + `BZSetLights`/`OnVariablesSynchronized` so an observed AI car shows the beam — see §12), `BZInverseModel.c` (speed PID + gear + slope + surface), `BZBusConfig.c` (class `BZBusRouteConfig` with 50+ fields + `BZAction`/`BZTrigger`/`BZMarkerEvent`/`BZCrewMember`), `BZBusPlayerBase.c` (modded PlayerBase: OnRPC, NUMPAD recording handlers), `BZPathLogService.c` (PathLogger: recording + fingerprint), `BZRouteCleanup.c`, `BZILCCorrections.c`, `BZBusStopZone.c`, `BZExpansionWreckFilter.c`.
- `scripts\4_World\bz12drive\` — **the 1.2 brain** (21 files, ~17,000 lines): `BZDriveController` (orchestrator), `BZCintaRunner` (the tape), `BZPurePursuit`, `BZLegMachine`, `BZModeArbiter`, `BZDerivationEngine`, `BZManeuverController`, `BZEndpointSlam`, `BZAutoRecovery12`, `BZVehicleDataReader`/`BZVehicleManual`, `BZEnvelopeStore`/`Recorder`/`Source`, `BZRouteData`/`BZRouteLoader`, `BZBusInputApplier`. See §5.
- `scripts\5_Mission\` — UI + mission hooks: `BZBusMissionGameplay.c` (polls the Controls-menu inputs in OnUpdate), `BZBusMissionServer.c`, `BZQuestHook.c` (`modded MissionServer Expansion_OnQuestStart`), `BZReproductorUI.c` (the player), `BZControlPanelUI.c`, `BZBusUI.c`, `BZDebugOverlay.c`.

**Config/data:** `config.cpp` (CfgPatches/CfgMods class `BZ_AutoDrive`, prefix `$PBOPREFIX$`=`BZ_AutoDrive`), `data\` (bus_stops.json, gear_ranges.json, wrecks_cleanup.json), `gui\layouts\` (control_panel_v2.layout, etc., CPP-style format), `gui\textures\` (.paa), `stringtable.csv`, `meta.cpp` (name=BZ_AutoDrive, publishedid=0).

**Tools (PowerShell/Python, NOT in the PBO — they run on the admin's PC):** `tools\` is **only the wizard runtime**: `tools\route_wizard.ps1` (TUI; menu [1] Convert / [2] Import v1 take / [3] Merge recording segments (continue a recording, `frame_merge.py`) / [6] Configure paths / [Q] Quit), `tools\Wizard.bat` (double-click launcher, `-ExecutionPolicy Bypass` only for that run), `tools\frame_to_route.py` (**the real converter**: recording → **the four files** `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv`, no modes), `tools\envelope_emit.py` (**the envelope**: the wizard invokes it on every conversion, and it writes/merges `envelope_<clase>.csv` in `RoutesDir`), `tools\python\` (**embedded Python**: the wizard does not depend on the system Python), `tools\transport_v1_to_route.py` (v1 import), `tools\i18n_strings.ps1` (wizard i18n, dot-sourced), `tools\driving_config_template.json` (the driving config the converter attaches), `tools\wizard_config.json` (portable paths — **not published**). The **dev/analysis/legacy** tools (validators `enforce_lint.py`/`check_rpt.py`, analysis `endpoint_detail.py`/`analyze_*`, old calibration `calibrator_lib`/`route_calibrator`/`curve_advisory`, old converter `csv_to_route`/`route_split`, reports `report_export.ps1`, asset-gen) were moved to **`..\BZ_AutoDrive_devtools\`** (outside the mod, **not published**).

**build:** package the PBO with DayZ Tools (AddonBuilder) or your own pipeline; sign with your own key. (No build script ships — each modder packages their own way.)

---

## 4. Architecture

**Override-last (the breakthrough).** Control is injected into a `modded CarScript.OnInput(dt)` that calls `super.OnInput(dt)` FIRST (eAI runs, including `ShiftTo(FIRST)`) and AFTERWARD overrides `SetSteering/SetThrottle/SetBrake/ShiftTo`. Subordination, not replacement: eAI never "knows" it is being driven. Robust to eAI updates.

**Three layers:** the **recorded path** (3D positions) = sacred objective (reaching the waypoints in order is guaranteed); the **declared physics** (config + Newton) = limits that are not violated; the **control law** = the bridge.

**Runtime flow:** `BZBusService` (server singleton) spawns the vehicle + the eAI driver, runs a Tick (~every 500 ms for event logic; input control runs in OnInput per frame), advances the waypoint index, evaluates triggers and fires `ExecuteAction` per verb.

**The brain is chosen per route (1.2).** `BZBusService` is the **shell**: spawn, pause/hold, lights, events, convoy, quests and UI always run. The **driving decision** is made by the 1.2 brain (`Tick12` at 2 Hz + `SampleDrive12` at 40 Hz from `CarScript`); what sits below it in `BZBusService` (`m_AtStop`, the magnet, `DriveTowards`) is 1.1 and **does not run**. If the route has no `_wp.csv`, it falls back to 1.1 **with a log line** (`[C12] ... va por el cerebro 1.1` (= "goes through the 1.1 brain")). ⚠ Until 2026-09-23, 1.2 was only activated with `profiles\BZ_AutoDrive\CEREBRO_12.txt` (and the tape with `BZ12_CINTA.txt`); since then both run **by default** and are turned off with a file with text (`CEREBRO_11.txt`, `BZ12_SIN_CINTA.txt`). Check `Cerebro12AlCargar` in the code in front of you.

**Control fixes live in the CODE; they are not baked into the take (key architecture).** Every driving improvement (cruise, reverse, slope, endpoint-taper, AR_OnWay…) is runtime logic in the code (in 1.2, in `scripts\4_World\bz12drive\`; in 1.1, in `BZBusService`), gated by mode/geometry — they are NOT fields baked into the `_hdr`/`_wp`. Consequence: a **header-swap conversion** to another vehicle (same waypoints, another `VehicleClass`+`Fingerprint`) **inherits ALL the fixes automatically and per-physics** (they read wheelbase/maxSteer/R_min from the `Fingerprint` + the geometry of the wps live). *(This explains why the controller generalizes with a single config. **1.2 stance:** a recording belongs to ITS vehicle —it brings its fingerprint and feeds the envelope of ITS class—; the header-swap remains as a technical/legacy mechanism, **not** as a workflow. For the same route on another vehicle: **record it with that vehicle**. The trajectory editor was dropped on 2026-09-23.)* That is why the K-turn generalized to r32/Camaro without re-tuning, and an OLD take **improves on its own** when run on a new PBO (the `_wp` data does not change; the code that interprets it improves). Only nuance: the per-route GAINS that the converter does NOT write (e.g. `ReverseFFWeight`/`ReverseStanleyK`) fall back to the code defaults — if a particular vehicle needs another gain, it is 1 line in its `_hdr` (no rebuild).

---

## 5. The 1.2 brain — how it drives (internals + parameters)

**Where:** `scripts\4_World\bz12drive\` — 21 files, ~17,000 lines. **What §5-L describes (Stanley, `DriveTowards`, `m_AtStop`, the magnet) is the 1.1 brain**: it does not run when 1.2 is in charge. Before answering anything about the driving, confirm which one runs (§2, *Open*).

### 5.1 The architecture in one figure
```
BZVehicleDataReader  --(once, at spawn)-->     BZVehicleManual  (read-only, ONE truth per vehicle)
       SENSOR: one of the only TWO that touch the Car          |
                                                                v
                BZDriveController  = the ORCHESTRATOR (the only part of the brain the runner sees)
                  |   Plan()   at  2 Hz  -> decides the MODE and the targets   (Tick12 in BZBusService)
                  |   Sample() at 40 Hz  -> tape, AR, pedals, application      (SampleDrive12, from CarScript)
                  v
   BZModeArbiter -> who drives this frame          BZLegMachine -> the cursor (its ONLY owner), advances by ARC
   BZCintaRunner -> replays the take               BZPurePursuit -> trajectory (pure function, stateless)
   BZDerivationEngine -> targets -> pedals         BZManeuverController / BZEndpointSlam -> POSE modules
   BZAutoRecovery12 -> restart in place            BZEnvelopeStore/Recorder/Source -> what is MEASURED
                  |
                  v
BZBusInputApplier  --(the only other one that touches the Car)-->  SetSteering / SetThrottle / SetBrake / ShiftTo
```
**Design principle:** no module calls another one or reads its state; the Controller composes them. Each one has **one** responsibility and is testable without the game (there are Python benches that reproduce the modules line by line — "mirrors").

### 5.2 The modes (`BZDriveMode`, in `BZDriveTypes.c`)
```
0 INVALID · 1 IDLE · 2 CRUISE · 3 APPROACH · 4 MANEUVER · 5 ENDPOINT · 6 HALTED
```
The logs print the number (`modo 2` = CRUISE). **The arbiter's table is TOTAL:** every (state, condition) pair that does not match falls into an explicit fallback (STAY or SAFE_STOP). In 1.1 the failure mode was a hook that **did not decide**; here there are no gaps.

### 5.3 The tape (`BZCintaRunner`) — the core of 1.2
- It replays **the input the human had AT THIS POINT of the path**: achieved wheel angle + throttle + brake + **gear**, and lets the physics do the rest (Arma native-driver style).
- **It is spatial, not temporal:** the index is the **arc**. It is queried by projecting the vehicle's position, with a **monotonic cursor with a step bound** (a cursor without a bound hung on diverging trajectories). The 40 Hz capture rate is only the resolution.
- **"The tape possesses":** latch `m_CintaRodo`. ⚠ **Since 938 it is only armed with a valid tape.** Before, it was armed without a tape, and the plateau (a recorded stop) engagement called `m_Cinta.EnMeseta()` on null → `NULL pointer` at 40 Hz → `Sample` died → frozen input (§12.0).
- **Recorded plateau:** where the human **stopped**, the tape holds the stop. An imposed stop (a stop event) is also held. **The human's stopped time is NOT replayed on its own**: waits and stops are set with events.
- **Tapeless** (old takes, or the bench harness, which does not load the tape), the framework still drives, as a trajectory follower. Tapeless reverse is reactive and it is the most fragile part.

### 5.4 Trajectory: `BZPurePursuit` + `BZLegMachine` + `BZRouteData`
- **Pure-pursuit** aims at a point at distance Ld ahead on the trace; **Ld comes from the speed** (it is a control parameter, not a per-vehicle constant), with a **geometric floor of ~1.5 × wheelbase**. It produces a target (speed, decel, curvature) and **does not touch the pedals**. **It has no state**: it is the piece that killed "jumps across legs / runs off into open ground".
- **`BZLegMachine`** is the **only owner of the cursor**, it advances **by arc** and does not go back except on an explicit `SeekTo()`. (In 1.1 each module re-scanned with a global nearest search, which lies on a figure-8 loop or at a perpendicular spawn.)
- **`BZRouteData`** precomputes arc, heading, curvature and legs only once. **Legs:** a new one starts at a `legBreak` or at a **change of travel direction** (shifting the gear is already a declaration).

### 5.5 From targets to pedals: `BZDerivationEngine`
(speed, decel, curvature) → (throttle, brake, gear, steering) **for THIS vehicle**. With `hasInputData = 0` the recorded pedals are discarded and re-derived: the fidelity depends on this file. The central idea: **start from what the vehicle does on its own** (creep, coast, measured brake) and ask only for the difference.

### 5.6 The vehicle manual and provenance (`BZVehicleDataReader` → `BZVehicleManual`)
- *"Derive what you can, MEASURE what you can't."* Each field carries provenance: **CONFIG** (read as is) · **DERIVED** (formula from the config: a **prior**, not a truth) · **MEASURED** (from the envelope) · **FALLBACK** (emergency prior, **logged loudly**: a silent fallback is a magic number in disguise).
- **What is measured overrides what is derived. Never the other way around.**
- ⚠ **The mass is the CONFIG's, not the real vehicle's** (the Tiger weighs 2,000 kg for the engine, not 57 t). The log prints it: `[Manual] masa=...` (= "mass").
- **Creep vs stopped:** `creepKmh` is a creep **target** (the POSE modules use it); `idleCreepKmh` is the **measurement** (0 across the whole fleet). Everything that asks "is it stopped?" reads the measurement; `UmbralCasiParado()` has a floor of 0.5 km/h.

### 5.7 The launch floor (`launchThrottleFloor`)
**What it is:** the minimum throttle with which the vehicle breaks from rest on flat ground.
**How the control uses it:** `RampaDeArranque` starts **from the floor** and rises `+0.10` per Plan tick (0.5 s). **It disarms as soon as the speed crosses `UmbralCasiParado()`**, and the module gets its throttle back. That is why the floor only sets **the first tick**: a high floor launches sooner, a low floor takes one more tick. The log says where it came from: `ARRANQUE: ... rampa desde el piso 0.15 (medido)` / `(prior de flota)` / `(FORZADO por banco)` (= "LAUNCH: ... ramp from the floor 0.15 (measured)" / "(fleet prior)" / "(FORCED by bench)").
**Where it comes from:**
```
calibrated    the MEASURED value of that class (lcal lines of the envelope)
uncalibrated  LAUNCH_PRIOR_FLOTA = 0.13  (median of the calibrated classes; in BZVehicleManual)
derived       printed "(informativo, no decide)" (= "informational, does not decide"): it uses PEAK torque, and the launch is at idle
              -> measured/derived ratio 0.6 to 7.5 across 9 classes: useless, even scaled
```
**Measured (September 2026):**
```
CivilianSedan 0.05 · Harley 0.07 · PAZ_Yellow 0.11 · Tiger 0.13 · ChernoBus 0.15 · ChernoBus_02 0.15
ApocalypseTruck 0.17 · Truck_01_Covered 0.25        (AEC_Matador: discarded, distribution 14x too wide)
```
**Why it does NOT come from the takes (two measured reasons):** with a keyboard, **1,778 of 1,873** launches are recorded with the throttle at full (the key is binary); and the stretch from standstill is an **engine transient** (§12.0). **Measuring with a ramp is not the best way: it is the only way.**
**How to calibrate (bench, step by step).** Calibration lives in the **bench harness** (`BZ12TestDrive`, mod `BZ_AutoDrive12`), which **is not published**: in 1.2 only someone who has that mod can run it.
1. With the bench mod loaded, write the **name of a route** of the vehicle to calibrate in `profiles\BZ_AutoDrive\BZ12_CALIBRATE.txt` (first line; an **empty** file is ignored with a warning). Pick a route that **starts on flat ground** (|pitch| ≤ 2.2°) and has **long straights** (≥ 45 m with no leg end) where it can reach 45 km/h.
2. Start the server: the harness spawns the route's vehicle at its wp 0, waits for it to **settle** (4/4 wheels and still) and announces `[BZ12Drive] MODO CALIBRACION: ...` (= "CALIBRATION MODE").
3. **Phase 1 — the launch floor** (with the AR suspended): **9 launches** from rest on flat ground, in 1st gear; before each one, 1 s braked; throttle from 0 rising `+0.02` every 0.5 s; the sample is the throttle of the first tick of a crossing of 0.05 m/s **sustained for 4 ticks**. The **median** goes into the envelope as `lcal` lines, with the run signature `lcalrun,<pitch#1>|<throttles>`. Log: `LAUNCH cal: empieza` (= starts) · `LAUNCH cal #n: despega con gas X` (= pulls away with throttle X) · `LAUNCH cal TERMINADA: piso X (mediana de N, M corridas)` (= FINISHED: floor X, median of N, M runs) · `[Envelope] guardado tras la calibracion de launch.` (= saved after the launch calibration). Abnormal exits: `LAUNCH cal OMITIDA: pendiente ...` (= SKIPPED: slope; it wasn't on flat ground, use another route) · `LAUNCH cal ABORTADA: no despega ni con gas 1,0` (= ABORTED: won't pull away even at full throttle) · `LAUNCH cal DUPLICADA` (= DUPLICATE: same spawn ⇒ **the same physics bit for bit**, so that run doesn't count; for new samples, calibrate from another point; equality is by a pitch tolerance of 0.005°, not by rounding).
4. **Phase 2 — the 8-cycle bench** (`BANCO de creep/despegue: 8 ciclos antes de la ruta`): brake to a stop → **release everything** (measures the **creep**; the raw speed is logged with no gates, to tell "doesn't creep" from "creeps and something rejects it") → brake → fine throttle ramp from rest → **full-lock sweep** with the brake on (full steering to each side: the **capacity** `steerMax`, which can't depend on where the vehicle was driven).
5. **Phase 3 — the route, with forced cycles.** On the straights (CRUISE mode with ≥ 45 m ahead) it forces: **full throttle up to 45 km/h** (`aMax`) → **coast down to 8** (`coast2`) → **part throttle 0.35 up to 25** (gear ratio) → coast → **settle** for 260 ticks (creep is an equilibrium, not a point you pass through). And the **steering ladder**: 10 steps of 1 s, symmetric to each side, above 12 km/h. That gives the command → angle transfer (`stx2`), which **can't be measured** with a keyboard (the key is binary).
6. When it finishes, or when the run is cut, **the class envelope is saved**. Only calibration writes the envelope from the game; a driving run prints `[Envelope] NO guardado (corrida de manejo)` (= NOT saved, driving run).

**Bench flags** (harness only): `BZ12_FORCE_LAUNCH.txt` (0.01-0.99: forces the floor for that run, for counterfactuals) · `BZ12_FORCE_GEAR.txt` · `BZ12_FORCE_LDFLOOR.txt` · `BZ12_FF_WINDOW.txt` · `BZ12_FORCE_PURSUIT_REV.txt` · `BZ12_SPEED_CAP.txt`.
⚠ **If a USER asks you how to calibrate:** in the published 1.2 **they can't**, because the trigger lives in the harness. Every subscriber vehicle runs with `LAUNCH_PRIOR_FLOTA` (0.13); the manual doesn't offer calibration, and it's planned for a 1.2.1. Don't tell them to calibrate.
- **What they would gain by calibrating:** the number would be THEIR vehicle's. **It doesn't change how it drives on the road** (one ramp tick).
- **Where it should matter:** the **hill start**, **not measured yet**.
- **What it does learn without calibrating:** the rest of the envelope (curvature, braking, coasting, creep) comes **from their takes** when they convert (§5.10).

### 5.8 Maneuver, cusp and endpoint (POSE modules)
- **`BZManeuverController`** and **`BZEndpointSlam`** work at ~0-2 km/h against a point, where the curvature is dominated by the **pose error**, not by the shape of the path. They are **exempt from the DerivationEngine** but **not from the Manual** (decel, creep and gain come from there).
- **Cusp (reverse ↔ forward):** every direction change opens a leg. **Fix 936:** outside the `CreepZone` (1.5 × wheelbase) the remaining distance the maneuver uses is the **arc**, not the signed projection `signedToEndM` (which lied far from the end: −34 m with 4,737 m ahead ⇒ throttle 0 / brake 1 for 7 s).
- **The cut rule still applies:** the reverse steering angles belong **to the vehicle, not to the path**. Record the direction change **on a straight**.
- **Endpoint:** single tolerance `BZDriveTol.LLEGADA_M = 0.10 m`. **"It is not moved to make a run pass. What moves is the vehicle."**

### 5.9 AutoRecovery v2 (`BZAutoRecovery12`)
**Restart in place** for non-physical jams: the vehicle **stays where it is and the take resumes** — no teleport (design decision). The v1.1 AR teleported; in 1.2 that path does not run.
**Detection (the speed you read is NOT movement: a vehicle pushing something reads 0.3-0.7 km/h):** CLAVADO (pinned: full throttle with no displacement; it resets inside a recorded plateau (a recorded stop): a recorded stop is not a CLAVADO) · **no-progress jam** (`DetectarTrabaPorProgreso`: ≥ 10 s without cursor advance with requested throttle ≥ 0.95) · **arbiter SAFE_STOP** (20 s without advance) → **opens** the ladder; with a rung in progress (F1 active / relocation pending) the arbiter restarts and re-decides the mode, never STUCK · the **bog** (AHOGO; C1: first gear + hillstart) yields to the ladder with the verdict "advanced and got pinned again", or if in a 10 s window it did not advance the distance it would cover at `CINTA_RODANDO_KMH`; with the ladder open, a bog does not start C1.
**Physical ladder:** 1 = F1 **with direction** (it moves 3 m away opposite to the leg: reverse in a normal leg, forward in a reverse leg; before moving it probes from the bumper for **any object with collision** —vehicles, shelters, posts, guardrails, trees, rocks, walls; people, items, infected, animals and bushes don't count— and if something is there it skips to rung 2 without spending it: `escalon 1 rechazado: obstaculo <clase>` (= rung 1 rejected: obstacle <class>)) · 2 = relocate one wheelbase back (tape pose) · 3 = safe zones +N m; 2 and 3 also apply in reverse legs. **STUCK only with the ladder exhausted or disabled**, with `MotivoStuck()`. The `BLOQUEADO` notice comes from the closed→open TRANSITION (a single source, whatever path opens it). The heartbeat shows `AR=N (xL+yF)`: **logical** and **physical** recoveries of the brain that is driving. *(In earlier versions that field showed the 1.1 teleport counter and read 0 while AR v2 did 29 maneuvers; if you read an old RPT, count the `[AR]` lines.)*

### 5.10 The envelope (`BZEnvelopeStore` / `BZEnvelopeRecorder` / `BZEnvelopeSource`)
- **File:** `<RoutesDir>\envelope_<CLASE>.csv` — **one per vehicle class**, shared by all the routes of that class. The **wizard** writes it (`envelope_emit.py`) from every take you convert, **adding up** (it merges) — it does not overwrite, except with `--pisar`. The bench calibration adds `lcal`/`lcalrun`. **In game the engine READS it; it does not learn it while running.**
- **Fields:** `scalars` (wheel radius, wheelbase) · `fp` (fingerprint: it does not mix classes) · `medido` (provenance: how many takes and frames) · `k` (curvature per steering angle) · `ratio` (transfer per gear) · `stx2` (command → wheel angle) · `shist` · `ahist` (acceleration) · `coast2` (decel in neutral per surface: **the creep comes from here**) · `chist2` / `nocreep` · `lhist` (throttle at launch **while driving**: ramp position, **not** the floor) · `lcal` / `lcalrun` (the **measured** floor).
- **An envelope belongs to ONE class.** Two different liveries are two classes. *"The only way to fill an envelope is with the same class."*
- **All the difficulty is in the gates**: each quantity is valid only under certain conditions (`r = v/ω` requires clean rolling, with no throttle or brake). A dirty measurement is **worse** than none, because what is measured overrides.
- **`Save()` keeps what it does not understand** (since 920), and so does `envelope_emit.py`: a file that someone else writes is not trimmed.
- **Precedence on load (`Configurar12` step 0):** the converter copies `envelope_<clase>.csv` into `hdr["Envelope"]` (+ `EnvelopeClass`). At startup **the CLASS profile wins** (it accumulates); the hdr copy is used only if the class file does not exist or if the class measured LESS (`MuestrasTotales()`: sum of the counts of all the histograms; tie → class). Log line: `[Envelope] cargado de la clase ... · el hdr trae otra copia ...: no se usa` (= "loaded from the class ... · the hdr carries another copy ...: not used") / `sin archivo de la clase: cargado del hdr` (= "no class file: loaded from the hdr") / `la ficha de la clase ... es mas pobre ...: se usa el hdr` (= "the class profile ... is poorer ...: the hdr is used"). The **creep** is different: the hdr one wins (it is the measurement of THAT take).
- **How it's generated, merged and inspected:** on every conversion the Wizard runs `envelope_emit.py <frame.csv> --out <RoutesDir>`.
  - **Merging:** if that class already has a profile with the same **stable fingerprint** (`Mass`, `GearsCount`, `MaxSteeringAngle`), it **adds** the bins and the console says how much it grew. A take already listed in `medido` is not added twice.
  - **If the fingerprint changed** (the vehicle's mod was updated), it exits with **code 3** and the Wizard asks **`[R] regenerate / [S] keep as is`**. `R` = `--pisar` (overwrite): the profile starts over from THIS take.
  - **By hand:** `--dir <folder> --clase X` (every take of a class) · `--fusionar a.csv b.csv --out ...` (merge two profiles; `--forzar` if the fingerprints differ).
  - **A take with no usable rows** exits with code 2 **without writing anything**: an empty profile is worse than none.
  - **A "stub" profile:** one with almost no bins; in September 2026, 18 of 38 were stubs. You can spot it by the `medido` line (takes and frames) and by empty histograms. A route with a stub profile runs, but on the config priors. **The cure is to convert more takes of that class, and varied ones** (curves, braking, speeds): the richness is the variety, not the length.

### 5.11 Bench harness vs. runner
The **runner** is `BZBusService` (what runs on a normal server, with the tape). The **harness** is `BZ12TestDrive`, from the `BZ_AutoDrive12` mod — **it is not published**; it is the test bench. Differences that matter when you read a bench RPT: **the harness does not load the tape**; it is driven by flags in `profiles\BZ_AutoDrive\`: `BZ12_DRIVE.txt` (route to drive), `BZ12_CALIBRATE.txt` (route to calibrate; **the content is checked** since 930: empty = ignored with a warning), `BZ12_FORCE_LAUNCH.txt` (forces the floor, bench only).
**In the runner** (what a normal server runs) there are four **off** flags, all by content: `CEREBRO_11.txt` (use the 1.1 brain), `BZ12_SIN_CINTA.txt` (run without the tape), `BZ12_SIN_FF_PRIMARIO.txt` (steering without FF-primary) and `REPRODUCTOR_LANZA_HARNESS.txt` (the player sends the route to the harness; only useful with the bench mod installed). The only presence flag left is `BZ12_HEAD_TANGENTE.txt` (old heading convention; it warns with ⚠ in the log). If you see `[C12] el cerebro 1.2, la cinta y el FF-primario son el DEFAULT ...: sobran ...` (= "the 1.2 brain, the tape and FF-primary are the DEFAULT ...: ... are not needed") in the log, those are old enable flags that are no longer needed.

---

## 5-L. The 1.1 brain — LEGACY (Stanley + inverse model of `BZBusService`)

> ⚠ **This describes the 1.1 brain**, the one that runs only if 1.2 does not take over the route (the `_wp.csv` is missing, or 1.1 was requested explicitly — see §2). It is kept because there are 1.1 takes and servers, and because many of the validations here (endpoint, reverse, AR_OnWay) were inherited by 1.2. **Do not use it to explain how 1.2 drives.**


**Lateral Stanley.** `targetYaw = segHeading − atan2(K·lateralOffset, v)`, K=1.0 default. The `atan2(K·off, v)` attenuates by speed (strong when slow, soft when fast) → kills the zigzag. **Sign gotcha:** left-handed DayZ → `cross = AB.z·AP.x − AB.x·AP.z` (inverted = divergence). **Lesson:** modulating K by local curvature MAKES IT WORSE.
- Params: `SteeringScale` (-1=auto, derived from the wheelbase), `CurvatureSteerBoost`, `PathSmoothWindow` (0 recommended on tight curves; >0 cuts curves — historical bug of the hardcoded divisor).

**Corridor / "paredón" (containment wall).** Dead band (lane half-width): inside it Stanley is OFF (it does not perturb), outside it is ON + damping. Params: `CruiseLateralDeadband` (~0.5), `CruiseLateralKGain` (1.0), `CruiseLateralDamp` (~0.3), `CruiseLateralCenterOffset` (lateral bias; +right/−left; ~25 m of median per unit — resolution 0.01–0.02). **Apply CenterOffset AFTER the deadband** (bug FBC8571F: it was subject to the deadband).

**Feedforward.** Curvature with lookahead (~20 m cruise, 1–3 m parking). Param `CruiseFFWeight` (-1→0.25). `CurveThrottleEnabled` cuts throttle BEFORE a tight curve.

**Inverse model (`BZInverseModel.c`).** Speed PID: `targetSpeed → throttle/brake`. `UseInverseModel`, `InverseModelKp/Ki/Kd` (-1=default 0.4/0.05/x), `InverseModelLowRpmMin` (rpmMin×1.0 vs ×1.3; default true after Test C: damped gear, less lugging), `TargetSpeedSmoothWindow` (smooths the target, less PID variance), `AccelShiftThreshold` (anti-catapult). Predictive braking: `aNeeded = u²/(2·dist) ± g·sin(pitch)`.

**Gear.** `GearStrategy = "auto_box"` (AT by RPM) or `"follow_recording"` (recorded gear; recommended default for non-bus vehicles; sports cars spin their wheels in 1st with auto_box). `MaxGear` (FIRST=2…SIXTH=7). The InverseModel respects follow_recording via a SelectGear override.

**Slope.** `SlopeCompensationEnabled`, `SlopeLookaheadWps` (5), `SlopeGain`, `SlopeLateralGain`. The recording already has the terrain baked in → following it inherits the compensation.

**Lights + horn (Phase 2 replay, 1.1 brain — ⚠ NOT in 1.2: the `frame_` take doesn't record horn or lights, and `frame_to_route.py` writes 0 in columns 16-17; the `HornMode` default is now `off` and `"replay"` only warns at load. In 1.2: `HornMode` `off` (default), `stops` (when each stop ends) or `finish` (at `ROUTE_COMPLETE`); `LightsMode` `auto` (default), `on`, `off`, `auto_inverted`; for an exact spot, the `horn` / `lights_on` / `lights_off` verbs in an event).** The PathLogger captures the human's `horn`/`lights` columns (via `CarScript.Cast(parent)`); each waypoint stores `targetHorn`/`targetLights` (in `BZWaypoint`), and Boris replays them per waypoint (**spatial replay**: he honks/turns the lights on where the human did). Per-route config: `HornMode` (`replay`/`stops`/`finish`/`off`) — the RECORDED horn is always replayed; `HornMode` controls only the AUTOMATIC honk; `LightsMode` (`replay`/`auto`/`auto_inverted`/`on`/`off`). **Horn:** `CarScript.SetCarHornState(int)`, enum `ECarHornState{OFF=0,SHORT=1,LONG=2}`, works while moving and **syncs to the observer** because `m_CarHornState` is `RegisterNetSyncVariableInt` + `SetSynchDirty`. **Lights:** they need an energized battery (`OnBeforeLightOn()` requires `GetCompEM().GetEnergy()>0`; the fix in `EquipBus` does `batt.GetCompEM().SetEnergy(GetEnergyMax())`). The **network gotcha** of the lights on an observed AI car (and its v3 fix with its own NetSync) is in §12 — it is mandatory reading whenever you touch any VISUAL state of an AI car.

**AutoRecovery (classic, stuck-based).** `AutoRecoveryEnabled` (default false), `AutoRecoveryStuckTimeS` (5–10), `AutoRecoveryAdvanceWps` (3–5), `AutoRecoveryCooldownS` (8), `AutoRecoveryMaxPerMission` (0=unlimited). It teleports to wp+N if Boris gets stuck (speed~0, or wp_idx not advancing for >N s even while steering); it preserves velocity with an impulse; it logs a geographic diagnostic heatmap. **"Do not save Boris from Boris" philosophy:** on CLEAN terrain Boris is precise → if he gets stuck on a clean road it is a RECORDING bug (covering it up hides it → re-record). That is why it stays OFF by default.

**AR_OnWay (shield against EXTERNAL obstacles of the world — 100% validated on 5 vehicles, 2026-07-01).** Different from the classic AR: it protects Boris from the **WORLD** (another stopped vehicle, or one that hits/pushes him), not from his own control. Two **independent** flags:
- **`ObstacleSlow`** = predictive brake. It scans the path ahead with a **lookahead that VARIES with speed + vehicle physics**: `scanMax = ObstacleStopDist + v²/(2a) + v·0.6 + 5`, with `a = min(ObstacleDecel, real decel of the vehicle)` (`BZInverseModel.GetMaxBrakeDecel` from the config, bounded by friction·g) → a heavy pickup with weak brakes **looks farther ahead and brakes at its real maximum, on its own**. It brakes to a stop at `ObstacleStopDist`≈15 m. **Lateral corridor** `ObstacleCorridorHalf`≈2.3 m: it measures the obstacle's offset from the lane axis → a car on the **shoulder** or in the opposite lane does NOT brake Boris (only what blocks his lane). It also queries **Boris's REAL front** (not only the recorded waypoints) so it does not miss a car pressed against him or pushing him.
- **`ObstacleEscape`** = teleport to the first **clean** wp past the obstacle, if it persists (>`ObstacleEscapeWaitS`≈6 s braked) or if he is **pushed/hit**. Triggers robust to scan flicker: `owPersist` (braked + seen recently), `owPushing` (wants to advance, target>15, but kmh<10 sustained after seeing an obstacle), a "touching" level (<5 m → 2 s). Smooth resume at `ObstacleEscapeResumeKmh`≈10.

Config: `ObstacleSlow`/`ObstacleEscape` (bool, default false), `ObstacleScanDist`(50, **floor**), `ObstacleStopDist`(15), `ObstacleDecel`(4.5), `ObstacleCorridorHalf`(2.3), `ObstacleEscapeWaitS`(6), `ObstacleEscapeResumeKmh`(10). **Profiles (toggle — the key point, settable at runtime by the quest):** **Robust transport** (both ON) / **Interceptable** (Slow ON + Escape **OFF** → Boris brakes nicely for whoever blocks him and does NOT escape → the interception mission works; if he escaped, it would break it) / **None** (both OFF = pure replica). Gate `(ObstacleSlow||ObstacleEscape) && UseInverseModel` (the template already ships `UseInverseModel=true` → it engages). The **v1 import asks for the profile** (**[R]**obust / **[I]**nterceptable / **[N]**one → sets `ObstacleSlow`/`ObstacleEscape` in the `_hdr`); in a new take they are set through the config. **Open edge:** a **head-on** crash that pushes Boris ~3 m off-path can pin him (this is **off-path recovery** territory, different from "obstacle ahead in the lane" — "almost closed"). See §15.

**Speed strategy — flag combos (ADVANCED; NO longer a user choice).** The Wizard produces **a single control**: it follows the line + the recorded speed (pure-pursuit + inverse model; `FollowPath=false` + template). The old **"modes 1/2/3"** were combos of these flags and were **unified into that single control**; they are still available by hand in the `_hdr.json` for a modder who wants another behavior: **pure replay** (`FollowPath`/`FollowPathUseReference`/`UseInverseModel`=false; same vehicle, `hasInputData=1`), **pure geometry** (`FollowPath`+`UseInverseModel`=true; speed from curvature + cap, heavy vehicles), **reference-assisted** (all three true; recorded speed + steering from the config = the generalizer). It is NOT a ranking.
**Maneuvers with a cut — flag behavior (advanced/legacy, 2026-07-03):** `isM3approach = (UseInverseModel && !FollowPathCapByRecording)` — the **auto-approach is M3-ONLY**. It aims at the parking entry as a crawl (target ~5). If the cut falls in a fast zone (straight ~23 km/h) it **over-brakes** → the vehicle stalls in the transition (real case: Sedan M3-straight pinned at `wp2289`, on-path, spd 0). **M2** (`FollowPathCapByRecording=true`) crosses at the RECORDED speed → robust, and it generalizes better. **For maneuvers with a cut: prefer M2.**

**Maneuver controllers:** **parking** (high FF, short lookahead, anti-rollback) — ALIVE. **It is DIRECT-REPLAY (open-loop, 2026-07-03):** the wps tagged `parking` do NOT run the Stanley loop — the framework replays the EXACT recorded steering angles + pedals (`targetSteering/Throttle/Brake`). Premise **"arrive ready"**: reach the phase segment in the demonstrated pose+speed. *(The `mode="parking"` recording toggle with NUMPAD+ was **removed from the key menu** — parking/maniobra are **legacy**; new takes do not mark them. The code honors the branches for old takes.)* **Corollary (the cut rule):** since the angles are VEHICLE-SPECIFIC (same angle → radius ∝ wheelbase), direct-replay **does not generalize in a curve**. For another vehicle to complete it (cross-vehicle: **record with that vehicle**; the header-swap is legacy): the CUT into the maneuver block must fall where the trajectory is **STRAIGHT** (flat heading) — that way the curve stays in **closed-loop Stanley** (vehicle-agnostic) and only the straight is replayed. Detecting where the straight starts: **first post-maneuver sample with a flat heading (<1.5° over 20 samples)**. Cutting in a curve → the Sedan (wb 2.935) crashed; cutting on a straight → it completed (parking 0.71→1.11 m). See §2 (the cut rule). **`ModeEntrySnapEnabled` is now `false` by default (it was true):** the alignment teleport on mode changes was turned off — the closed-loop control (parking + reverse rear-steer, heading <1°) positions the vehicle on its own; the snap only fired at <0.5 m. It can be re-enabled per `_hdr`. **reverse** — ALIVE and **validated** (§2, 4 vehicles). **Rear-steer** bicycle model: control point at the **rear axle** (`GetReverseControlOffset`≈wheelbase/2 — it also anchors the FF, not only the corridor), inverted Stanley sign, `ShiftTo(0)`, it follows the recorded steering above `ReverseRecordedSteerThreshold`≈0.2. **Full steering authority:** the `SteeringScale` (forward's anti-over-rotation, ≈wheelbase/5.5) is **NOT** applied in reverse — it cut the steering in half and prevented forcing the arc. **Anticipation ∝ R_min:** `floorFf = R_min·1.3` (R_min = wheelbase·cos/sin(maxSteer); `Math.Tan` is not guaranteed in Enforce) — a wide turn anticipates earlier. **Speed = min(recorded, physics):** `vPhysRev = MAX_REVERSE − (MAX_REVERSE−REV_PHYS_MIN)·|ffRev|` (MAX=`GetReverseTargetSpeedCap`≈25, MIN=5) → it generalizes per vehicle. **Terrain-aware endpoint-taper:** within `REV_TAPER_M`(8 m) of the end of the reverse block (measured as path distance along the wps) it ramps the target down to a floor that **rises with the grade** (`floor = MIN_PROG + grade·36` if grade>0.03; grade = dy/dist to the Y of the endpoint) → on **flat ground** it brakes to 3 km/h (kills the 16 m overshoot), **uphill** the floor exceeds the recorded target and the taper does not fire (self-guard, it preserves the climb). **Self-adapting endpoint (final stop) = SOLVED (2026-08-11):** the endpoint-taper + the self-adapting brake per vehicle+surface bring the stop to **below 0.5 m in most cases** (up to ~1 m on the big ones), forward and reverse, out-of-sample (Sedan in reverse 0.47 m, GT2RS in reverse 0.33 m; full list in §2). The endpoint after a curve follows the recorded speed and only brakes in the last ~3 m (SEQ1 44 km/h → 0.34 m). The **forward→reverse transition** honors the recorded brake-to-0 + handbrake + gear 0; the reverse→forward **exit** is solved by the *handbrake-resume* (it skips the handbrake-forward cluster and resumes forward — before, it got pinned in gear 0 + brake 1). **approach** — ALIVE: it is not a separate steering controller (it uses the normal Stanley) but a **speed tag** that ramps the brake BEFORE entering a maneuver — today it is **automatic** (`ApproachAuto`; it is **no** longer marked with a key) so that it does not slam the brakes and skid. **maniobra** — **deprecated for NEW recordings** (2026-06-17: out of the UI/hotkeys), BUT the branches are still in the code and honor old takes that have it (e.g. the K-turn EXAMPLE18). **Reverse knobs (per `_hdr`, no rebuild):** `ReverseFFWeight` (default 0.6=parking), `ReverseStanleyK` (default 0.8), `ReverseTargetSpeedCap` (25), `ReverseStanleyMinSpeed` (2 m/s, breaks the 1/v spiral). **All the reverse fixes are gated to `mode=='reverse'` / `isReversePk`** — they do not touch cruise or maniobra (isolation rule).

---

## 6. Route config (`BZBusRoute.json`) — reference

Class: `BZBusRouteConfig` in `BZBusConfig.c`. Sensible defaults; `-1`/`0` = code default / off. Groups: **Basics** (`VehicleClass`, `DriverClass`=eAI_SurvivorM_Boris, `RespawnDelay`=300, `SpawnHoldSeconds` [0=right away, 30+=wait for a mission trigger], `VehicleInvincible`, `VehiclePartsLocked` (tamper-proof vehicle, default `true`), `MaxGear`, `Attachments`, `Wheelbase`); **Speed/mode** (§5); **Steering** (§5); **Slopes** (§5); **AutoRecovery** (§5); **Convoy/Quest** (`ConvoyMode` ""/"flee_on_kill"/"ambush_on_damage", `Crew[]`, `Events[]`); **Maneuvers** (parking/reverse params, §5). **Waypoints[]:** `pos[x,y,z]`, `targetSpeed`, `targetGear`, `mode` (normal/parking/maniobra/reverse), `isStop`/`stopDuration`/`stopRadius`, `targetThrottle/Brake/Steering`, `hasInputData`, `name`. It embeds a `Fingerprint` (from the recording header). For the exact list of fields: Appendix A of the manual.

**Attachments: NEVER guess** (non-negotiable rule). They come from the recording fingerprint (NUMPAD 5 captures them) or from the trader package/config. When you change `VehicleClass`, REPLACE the attachments (the converter keeps the ones from the previous JSON = recurring bug). The recording header is the authoritative source (the converter already prefers it).

---

## 7. The Wizard + the pipeline + where the data lives

### 7.0 — What the conversion produces in 1.2 (READ FIRST; it overrides whatever follows in §7)

The real chain: **`Wizard.bat` → `route_wizard.ps1` → `frame_to_route.py`** (the route) **+ `envelope_emit.py`** (the envelope). The Wizard ships with **embedded Python** (`tools\python\`): it does not depend on the system's Python.

**Four route files**, in `RoutesDir` (the server's `…\profiles\BZ_AutoDrive\`):
```
BZBusRoute_<NAME>.json            the MASTER: waypoints + config + events, human-readable. It is the CATALOG:
                                  the name that shows up in the player and the one you ask for at launch.
BZBusRoute_<NAME>_hdr.json        the ID CARD (~2 KB): VehicleClass, Fingerprint, Wheelbase, MaxGear, Events,
                                  Attachments, the creep (CreepKmh/CreepSource/CreepSamples/CoastAtRest)
                                  and WpRows/CintaRows (how many rows the other two have).
BZBusRoute_<NAME>_wp.csv          one row per waypoint (pos, heading, gear, wheel angle, stop, name).
BZBusRoute_<NAME>_cinta.csv       the driving at 40 Hz indexed by ARC LENGTH:
                                  s,x,y,z,heading,chassis_kmh,front_wheel_deg,throttle,brake,gear,handbrake,dt
```
**Plus one per vehicle:** `envelope_<CLASS>.csv`, in the same folder. The name carries the **class**, not the route: three routes of the same bus feed and use the same vehicle profile. Each conversion **adds to it** (merges); `--pisar` rebuilds it from scratch.

⚠ **When a route starts, the server does NOT read the master:** it loads the **compact pair `_hdr.json` + `_wp.csv`** and the **tape**, in slices. If someone edits events by hand, they must be right in the `_hdr.json`, not only in the master.

**Why four and not one:** loading half a megabyte at once inside one frame hangs the server. The **global load queue** splits a per-frame budget among all the queued routes; to split it, it needs to know how much there is, and `WpRows`/`CintaRows` in the ID card declare that. Measured: the worst frame is the same with 1, 2, 3 or 5 routes launched at the same time.

**When you move a route to another server:** the **four** files **and** the envelope of its class. Without the envelope it starts with the factory values.

**Cross-vehicle:** a recording belongs to ITS vehicle (its fingerprint, and it feeds the envelope of ITS class). **The same route on another vehicle = record it with that vehicle.** The header-swap exists as a technical/legacy mechanism; do not recommend it as a workflow. The trajectory editor **no longer exists** in the package.

---

**Pipeline:** record (NUMPAD 5 → `frame_*.csv` + `header_*.txt`) → `route_wizard.ps1` **[1] Convert** (it asks only for the NAME → `frame_to_route.py` builds the four files `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv` (and the envelope of the class) and leaves them deployed) → run (player, hot-load without restart).

**The Wizard (`route_wizard.ps1`, TUI) — current menu:** **[1] Convert · [2] Import v1 take (BrigadaZ Transport) · [6] Configure paths · [Q] Quit**. It is a **pure converter**: it does **NOT** lint, does **NOT** give a "score/BZ-Score", does **NOT** choose a "driving mode" (there is only one, §5). Quality comes from **recording well** (§7.G): if the take came out dirty —you rode up a curb, you braked badly—, **re-record** (it is free).
- **[1] Convert** — you pick the **recording** (listed by **date**, newest on top; the tag says which folder it comes from: `[Cl]` client, `[A]` the server's recordings, next to `RoutesDir`, `[B]` second server) and give it a **NAME** (→ `BZBusRoute_<name>.json`; that is how it shows up in the player; empty Enter = default active route). The Wizard calls `frame_to_route.py <frame> <name> --profile <RoutesDir>`: it reads the `header_*.txt` (fingerprint), **generalizes** the line with the vehicle's physics, **auto-detects reverse from `gear==0`**, **auto-derives the leg breaks** (`legBreak`) from the forward↔reverse gear change and writes the **four files** (`.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`) already **deployed** (hot-load). **There is no mode choice** — the control is single.
- **[2] Import v1 take** — converts a take from **BrigadaZ Transport v1** (monolithic JSON with `Waypoints`) to the AutoDrive format. It asks you for the **vehicle identity** (a `header_*.txt` from any recording of that car —a 10 s one is enough— or an already calibrated `_hdr.json`) + an **obstacle profile** (`robusto` / `interceptable` / `ninguno` = robust / interceptable / none → sets `ObstacleSlow`/`ObstacleEscape`). It runs `transport_v1_to_route.py`. See D.11 (v1 import guide).
- **[6] Configure paths** (`Invoke-ConfigPaths`) — re-sets `RoutesDir` + (optional) `ServerBMirror` (Enter = keep, `-` = delete the mirror); it saves them in `wizard_config.json`.

**Wizard PRINCIPLE:** **everything is done inside the Wizard**; it orchestrates `frame_to_route.py` (which already writes the four files in one go; and `envelope_emit.py` writes the envelope) for you. The user **does NOT edit JSON by hand or run loose `.py`/`.ps1` scripts**. **Portable** recording folders (`Update-LogDirs`): **no** longer hardcoded — they are derived from `RoutesDir`/`ServerBMirror` (client via `%LOCALAPPDATA%`). Launcher **`tools\Wizard.bat`**: double-click, without opening PowerShell by hand (it uses `-ExecutionPolicy Bypass` only for that run).

**Robustness against operation errors:** each menu option runs in its own `try/catch`. If an operation fails, **it shows the error and GOES BACK TO THE MAIN MENU** — the Wizard **no longer closes** (it only exits through **[Q]**). Before, an error killed the script and, since it is launched from `Wizard.bat`, Windows showed *"¿Desea terminar el trabajo por lotes (S/N)?"* (= "Terminate batch job (Y/N)?" on an English Windows) — that no longer happens in normal use. → If an operation fails, the error text **stays visible on screen** (before, it was swallowed): the user should **copy** it and continue from the menu.

**Route/recording selector (`Select-Route`):** it **hides the `*_hdr.json` files** (the "header" half of the fast-load pair `_hdr.json`+`_wp.csv`, which showed up as 0-wp items and confused people). The list shows **only the real `.json` routes**.

**Conditional "server B" reminder (portability):** before, `route_split.ps1` printed a hardcoded "Sincronizar el par al server B también" (= "Also sync the pair to server B") — irrelevant for a modder with a single server. Now `route_split` **no longer mentions it**; the reminder appears ONLY if the user has a second server configured (`ServerBMirror` in `wizard_config.json`), and it shows the **REAL PATH**. Most people (a single server) see nothing about "server B" — this is part of the Wizard being folder-agnostic/portable.

**Where the data lives (CRITICAL, it took 1 h to find):**
- Human recording (`path_*.csv`, NUMPAD 5) → **CLIENT**: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- NPC run (`ai_run_*.csv` and `boris_native_*.csv`, **opt-in with the player's checkboxes**, **no** longer with a key) → the **SERVER that ran it** (`<server>\profiles\BZ_AutoDrive_PathLogger\`).
- Routes → `<server>\profiles\BZ_AutoDrive\` (`BZBusRoute*.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`), and in the same folder one `envelope_<class>.csv` per vehicle (§7.0).
- `header_*.txt` (next to each path_*) = vehicle fingerprint.

**File formats:**
- `path_*.csv` (30 cols): time_s, x, y, z, heading_deg, speed_kmh, is_stop, gear, throttle, brake, steering, rpm, redline_rpm, mode, vx, vy, vz, clutch, handbrake (+ more, incl. the human's **horn** and **lights** for the Phase 2 replay).
- `header_*.txt`: `vehicleClass=`, `mass=`, `engineRPMIdle/Max/Redline=`, `gearsCount=`, `wheelCount=`, `attachmentsCount=`, `attachments=` (CSV), `maxSteeringAngle=`, `wheelbase=`. R_min = wheelbase / tan(maxSteer).
- `ai_run_*.csv`: time_s, x/y/z, heading, speed_kmh, gear, throttle, brake, steering, mode, dist_to_next_stop, next_stop_idx, **wp_idx**, **lateral_dev_m**, corridor_offset/valid, target_speed, target_throttle/brake, i_speed/throttle/brake, rpm, redline_rpm, wp_mode, is_marker.
- **The framework PREFERS the `_hdr.json`+`_wp.csv` pair** (fast FGets, ~86 ms) over the monolithic JSON (slow parse, ~150 s). **GOTCHA (historical):** the old `csv_to_route.ps1` only rewrote the `.json` → you had to run `route_split.ps1` or the server loaded the OLD pair (it spawns the previous vehicle). The current converter (`frame_to_route.py`) **writes the four files in one go** → it no longer applies.
- **The fast-load pair carries lights/horn:** `route_split.ps1` writes `targetLights` (col 16) and `targetHorn` (col 17) on each row of the `_wp.csv`; `LoadWaypointsCSV` (in `BZBusService.c`) parses them (`parts[16]`/`parts[17]`). So the lights/horn replay also works through the fast-load path, not only through the monolithic JSON.

**The converter does NOT lint or score.** `frame_to_route.py` is direct: it reads the header, generalizes the line and writes the four files — it does **not** ask you *"should I cap this curve?"* and it does not give a BZ-Score. If the take came out dirty (gaps from lag/teleport, gear lugging, a curb), the answer is to **re-record**, not a linter. *(The interactive linter + BZ-Score of the old Wizard were removed: fidelity comes from recording well + reading the fingerprint.)*

**"Recording CORRUPTO" guard (csv_to_route):** some OLD recordings have, instead of numbers, the literal format specifiers (`.2f`, `.1f`) in ALL rows — a bug of an old PathLogger version (or hand-edited takes). Before, it crashed the conversion with a cryptic error ("no se puede convertir '.2f' a Single" = "cannot convert '.2f' to Single"). Now `csv_to_route` **detects** it (it checks that the `x` column of the 1st row parses as a number) and **aborts with a clear message**: *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"* (= "Recording CORRUPT… record it again or pick a more recent one"). It is NOT a Wizard bug, it is the recording → pick a **newer** take (the ones at the TOP of the list, sorted by date descending, that have `throttle/brake/steering` columns).

**`frame_to_route.py` (the real converter):** it takes the `header_*.txt` → automatic VehicleClass + Fingerprint + Attachments. **It auto-detects reverse from `gear==0`**. It derives `targetHeading` from geometry (`atan2(dx,dz)`). **It auto-derives the leg breaks / interchanges** (`legBreak`) from the forward↔reverse gear change at ~0 km/h (a 0 without a direction change = a pause, not an interchange). It writes the **four files** (`.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`) to `<profiles>\BZ_AutoDrive\` with a `.bak` backup — **no** separate `route_split` is needed. It does **not** accept modes: the driving config comes from the single template (`driving_config_template.json`). **Watch out:** `gear==0`=reverse assumes the carpack convention; check it per vehicle (it can be neutral). **Corrupt** guard: if the `x` column of the 1st row does not parse as a number (old recordings with literal `.2f`/`.1f`), it aborts with *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"*.

> ⚠️ **`<server>` is the server folder of whoever is reading you** (where `@BZ_AutoDrive` and `profiles\` live), and it changes with every installation; only `%LOCALAPPDATA%\DayZ\...` is the same on every Windows PC. Before you guide someone with file locations, **ASK FOR THEM** (see Walkthrough §7.G, Step 0).

### 7.F — Common Wizard problems (troubleshooting)

Map the symptom straight to the fix. (For runtime/compile/deploy errors, see §12.A.)

| Symptom | Cause | What to do |
|---|---|---|
| The panel doesn't open, NUMPAD 5 doesn't record, on a **fresh install** | empty `AdminSteamIDs` = nobody is admin (the boot RPT says `AdminSteamIDs is empty` / `Admins: 0`) | add the SteamID64 **in quotes** to `profiles\BZ_AutoDrive\BZAutoDrive_settings.json` and restart (step 0b in §7.G) |
| The boot RPT says **`No routes yet`** | there are no routes yet | record (NUMPAD 5) and convert with the wizard. `No default route … N route(s) available in the panel` is normal: without `BZBusRoute.json` nothing starts by itself at boot (since 1002 the old template is no longer generated) |
| **"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"** (= "record it again or pick a more recent one") when converting | OLD recording with literal `.2f`/`.1f` instead of numbers (bug of an old PathLogger, or a hand-edited take) | It is NOT a Wizard bug, it is the recording. Pick a **newer** take (the ones at the TOP of the list, sorted by date descending, with `throttle/brake/steering` columns). If you only have old takes, **re-record**. |
| A menu operation **fails with a red error** but the Wizard **stays open** (goes back to the menu) | this is the new behavior: each option runs in its own `try/catch` | ask the user to **copy the error text** (it now stays visible on screen) and to **continue from the menu**. The Wizard only closes with **[Q]**. |
| (Old versions) the Wizard **closed** and Windows asked *"¿Desea terminar el trabajo por lotes (S/N)?"* (= "Terminate batch job (Y/N)?") | an error killed the script launched from `Wizard.bat` | it **no longer happens** in normal use (try/catch per operation). If you see it, it is an unhandled case → report the error. |
| In the route/recording selector an item **`*_hdr` with 0 waypoints** showed up | the `_hdr.json` of the fast-load pair was listed as a route | it is **no longer shown**: `Select-Route` hides the `*_hdr.json` files. The list only has real `.json` routes. |
| A **"sync to server B"** reminder appears (real path) | the user has `ServerBMirror` configured in `wizard_config.json` | expected: copy the **pair** (`_hdr.json`+`_wp.csv`, not only the `.json`) to the path it shows. With a single server, this notice **does not appear**. |

### 7.G — Guided walkthrough (to accompany a user live)

*So that an AI (you) can guide an admin who does **NOT program** through the core loop. Rule: ONE step at a time, confirm before you move on, turn everything into concrete actions (which key, which file, what to look at) and verify on disk.*

**0 · ASK FOR THE USER'S PATHS FIRST — do not assume the ones in this doc.** Every server/installation changes the paths. Ask and write them down before you start:
- **Server folder** (where `@BZ_AutoDrive` and `profiles\` live): it can be on any drive or folder (`G:\MyServer\`, a VPS, etc.) → **ask**.
- **Client recording folder** (PathLogger): it is usually `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` on the PC where they play → confirm the real `%LOCALAPPDATA%`.
- **Where the `tools\` folder is** (the `route_wizard.ps1`).
- Do they have **2 servers** (A local + B remote)? Ask for both paths; routes are synced by hand (there is no automatic sync).

Only with those paths, guide them with the REAL locations of THEIR files at each step. *(Also, the Wizard asks for its paths on the 1st run and saves them — it is folder-agnostic.)*

**0b · MAKE THEM ADMIN — nothing works without this.** The server's first start with the mod creates `<server>\profiles\BZ_AutoDrive\BZAutoDrive_settings.json` with `"AdminSteamIDs": []`: **empty list = NOBODY is admin** (the panel doesn't open, NUMPAD 5 and 4 don't record; the boot RPT warns with `AdminSteamIDs is empty`). Ask for their Steam64 (on steamid.io, or in the server log when they connect) and have them add it **in quotes**: `"AdminSteamIDs": ["76561198000000000"]`. **Without quotes it isn't taken** (tested in game). Then they restart the server. ✅ Check the startup RPT: `[Settings] Cargado. Admins: 1`. If it says `Admins: 0` or `AdminSteamIDs is empty` shows up, the ID is written wrong. Human guide: manual §13.1.

**1 · RECORD** (in-game, as admin):
- Keys in `Options → Controls → "BZ AutoDrive"`. **Only 3** (and only the **admin** sees them): **Panel** (HOME), **Record/Stop** (NUMPAD 5), **Mark Event/Stop** (NUMPAD 4). The **direction change/interchange is NO longer a key** (NUMPAD 3 was removed, 2026-08-11): it is **auto-detected** from the forward↔reverse gear change. `ai_run`/`boris_native` **have no key either** — they are armed from the **player's checkboxes** (Step 3).
- Get into a vehicle → **NUMPAD 5** (start) → drive the whole route → at each stop brake fully and press **NUMPAD 4** (mark) → if there is a **direction change**, while stopped **you shift gear and carry on** (you do not press any key: the interchange is auto-detected from the gear, §9.4 of the manual) → **NUMPAD 5** again (stop). **Reverse is not marked either**: it is auto-detected from the gear.
- ✅ Verify: `path_*.csv` + `header_*.txt` showed up in **THEIR** recording folder. **The `header_*.txt` must NOT be 0 KB** (if it is 0 → the fingerprint was not captured → re-record). **Have them write down which vehicle they recorded with.**

**2 · CONVERT** (on the PC, outside the game — **everything inside the Wizard**, without editing JSON or running loose .py/.ps1 scripts):
- **Open the Wizard**: double-click **`tools\Wizard.bat`** (no need to open PowerShell). 1st run: it asks for their paths (server/client) and saves them (`wizard_config.json`); change them later with **[6] Configure paths**.
- Pick **[1] Convert** → you pick the **take** from a list sorted by **date (newest on top)** — the name `frame_<stamp>.csv` is a timestamp from the **GAME's** clock (not the PC's). If you see "Recording CORRUPTO", pick a **newer** one (§7.F). Then it asks you for **only a NAME** (Enter = active route). With that it runs `frame_to_route.py` (it reads the header, generalizes, auto-detects reverse, builds the four files in one go) and leaves the route **DEPLOYED**: "ready in the player (hot-load)".
- **There is no mode to choose and no linters to review** — it is a pure converter. If the take came out dirty, the answer is to **re-record** (Step 1), not to fine-tune. *(An advanced modder who wants to touch the control —`FollowPath`/`UseInverseModel`, etc.— does it by hand in the `_hdr.json`; see §5 + D.2. 99% do not need it.)*
- ✅ Output: `BZBusRoute_<name>.json` + `_hdr.json` + `_wp.csv` in **their** `profiles\BZ_AutoDrive\`, already deployed.

**3 · PLAY** (in-game) — the route was already deployed when it was converted, so **just open it in the player**:
- If they want to diagnose: **tick the `ai_run` checkbox (and/or `boris_native`) in the player BEFORE you press play** (otherwise NOTHING is written; there is no key anymore).
- **HOME** opens the panel → in the **player** pick the route (it shows up with the NAME you gave it) → **LOAD&SPAWN** (or **Spawn/Restart Bus**). Boris drives it; watch the panel (wp, km/h, mode). The `ai_run_*.csv` ends up on the **server that ran it**.

**Common failures → what to ask/do:** *(for Wizard failures when converting/selecting/restoring, see §7.F)*
- *"Recording CORRUPTO" when converting* → old take with literal `.2f`/`.1f`; pick a newer one or re-record (§7.F).
- *The route does not show up in the player* → is the `.json` in `profiles\BZ_AutoDrive\` and is it named `BZBusRoute*`?
- *It spawns the previous vehicle* → OLD `_hdr`/`_wp` pair; re-convert with the Wizard (it does the split).
- *No `ai_run` is written* → the `ai_run` checkbox was **unticked** → tick it in the player BEFORE you press play (it is no longer a key).
- *Boris stuck in 1st / does not accelerate* → it is eAI's `ShiftTo(FIRST)`; the override handles it. If it is a vehicle mod, confirm that it extends `CarScript`.
- *Build/deploy* → NEVER build/mirror with the server OR the client open (they lock the PBO).

### 7.H — Diagnostic reports (PS tooling, `report_export.ps1`)

> **Note (RETIRED from the publication):** the report generator (`report_export.ps1`) was **moved to `..\BZ_AutoDrive_devtools\` and is NOT published** with the mod (it was never in the Wizard menu, which is a pure converter). What follows stays as a **historical reference** of what it measured. To diagnose a run today, read the `ai_run` directly (D.10).

**What it is:** a PowerShell tool (it does NOT touch the PBO; it runs on the admin's PC) that generates **self-contained multi-page reports** in **PDF and HTML** from a human take and/or an `ai_run`. **Zero installation** of image/Python tools: it uses **Edge headless** (`msedge --headless --print-to-pdf`) + **inline SVG** — no ImageMagick/Pillow. It closes the framework's **MEASUREMENT** loop: it turns the raw numbers of the `ai_run`/take into something the admin reads at a glance.

**Entry point:** `New-RouteReport -ReportType Human|Boris|Comparative|Auto` (standalone; there is no Wizard submenu anymore).

**THREE report types** (`-ReportType Human|Boris|Comparative|Auto`):
- **① Human take** (`Human`) — **post-recording, without Boris**. Analysis of the demonstration: curve advisory, slope, gear, **predicted BZ-Score**, markers + the take's **🔵 trace alone**. It is used to judge the recording BEFORE running Boris.
- **② Boris take** (`Boris`) — **post-ai_run, without comparing**. Boris's **🟠 trace alone** + his **measured reading** (completion, lat-dev, speed, saturations, hotspots of the run). To audit Boris without having the human take at hand.
- **③ Comparative** (`Comparative`, default) — **human take + its ai_run**. Trace **🔵 human vs 🟠 Boris** + **hotspots** (top-N deviations: red circles on the map + **zone sheets** with zoom and a chart for each hotspot) + **measured reading of Boris** (completion, lat-dev, speed, saturations).
- `Auto` resolves the type from the inputs: take only → `Human`; ai_run only → `Boris`; both → `Comparative`.

**Operational details:** language via `BZ_LANG` (ES/EN). Output to the **`BZ_AutoDrive_Informes`** folder (sibling of `routes`). The examples are in `profiles\BZ_AutoDrive_Informes\` (e.g. `EXAMPLE02.pdf` = comparative, `EXAMPLE02_toma.pdf` = human take).

**PowerShell gotchas when building this tooling** (they also go in §12, but they stay here for context): a `.ps1` with **accents** needs **UTF-8 WITH BOM** (PS5.1 reads a .ps1 without BOM as ANSI → mojibake in the report texts); passing `@($obj)` to a `NoteProperty` **breaks** (use `.ToArray()`); the intermediate data JSON is written **without BOM** (it is read by the HTML/Edge, not by PS).

---

## 8. The event engine (verb/trigger DSL)

NUMPAD 4 marker (records `isStop=true` on the wp) → event node. `Events[]` of `{wp, trigger, actions[]}`.
- **Triggers** (`BZTrigger.type`): `wp_reached` (wp), `player_in_radius` (radius), `player_enter_vehicle`, `vehicle_health_below` (threshold 0..1), `timer` (seconds).
- **Verbs** (`BZAction.verb`, `else-if` dispatcher in `BZBusService.ExecuteAction(Car car, BZAction action, int evIdx)`) — **22**: `add_cargo`, `log_event`, `freeze_vehicle`/`unfreeze_vehicle`, `set_vehicle_mortality`, `set_driver_mortality`, `start_engine`/`stop_engine`, `despawn_vehicle`, `stop_route`/`resume_route`, `set_var`, `play_sound`, `lights_on`/`lights_off`, `horn`, `repair_vehicle`, `refuel`/`drain_fuel`, `ui_broadcast`, `spawn_guard`/`dismount_guard`. (`check_once` REMOVED: it broke the JSON load; branching belongs to Quest.) *Reserved, with no handler yet (extension — the `slot` field already exists in `BZAction`): `lock_seat`/`unlock_seat`/`eject_passenger`.*
- **BZAction fields:** verb, items, msg, slot, value, fvalue, var, faction, loadout, count, delay (choreography via CallLater).
- **Verified against the code (2026-09-25) — the complete reference, verb by verb and trigger by trigger, is in the manual §10.3.** What gets misread the most:
  - **24 verbs** (add `set_vehicle_parts`, see below, and `guard_stop`: the whole escorted stop; fields `radius` 8, `holdS` 20, `clearS` 5, `maxS` 120, `boardS` 30, `unreachableS` 15). `lock_seat`/`unlock_seat`/`eject_passenger` have no handler. No verb reads `slot` or `fvalue`.
  - **6 triggers**: also `player_near_waypoint` (`wp` + `radius`: a fixed zone around a waypoint, wherever the vehicle is). `waypoint` = alias of `wp_reached`; an event with no trigger is a `wp_reached` on its `wp`. A misspelled trigger warns at load (`TriggerTipoConocido`); a misspelled verb, only when it fires.
  - **`delay` counts from when the event fires**, not from the previous action. Each event fires **once per run** and everything re-arms on every spawn (frozen, `stop_route`, mortality, variables, the `timer` clock). The automatic respawn **does not re-read** the `_hdr.json`: to see a change, relaunch from the player panel.
  - **`timer`** counts from when **the vehicle is born** (spawn hold included). **`vehicle_health_below`** almost never fires with `VehicleInvincible: true` (the default).
  - **`stop_engine` alone lasts half a second**: the tick restarts the engine in every state except **frozen**. Waiting with the engine off = `freeze_vehicle` + `stop_engine`; `stop_route` can't keep it off.
  - **`ui_broadcast` (1000): `value` = to whom** (`""`/`"admins"` default · `"all"` · `"near"` + `radius`, 0 = 150 m · `"passengers"`; anything else = admins, a typo never reaches players), **`format` = where** (`"toast"` default · `"chat"` = `GetGame().ChatMP(p, "BZ AutoDrive: " + msg, "colorStatusChannel")` · `"both"`; unknown = toast); one delivery per player; log `[EVENT n @ wp X] ui_broadcast (all → N jugadores, toast): msg`. **Automatic notices are admin-only**, in two types set separately in `BZAutoDrive_settings.json`: `AdminArrivalNotifications` (arrivals: stop + wait, distance, end of line, route complete, left terminal) and `AdminSystemNotifications` (vehicle stopped, pause/resume, spawn failed, the Boris gesture): `"toast"` (default, also missing/unknown) · `"chat"` · `"both"` · `"off"` (still `[GLOBAL]` in the RPT). Panel replies always go on screen to the admin who clicked. Recording notices are client-local (admins only can record). Human guide: manual §13.3. **`set_var` has no effect**: `BZCondition`/`EvalCondition` exist but nothing calls the evaluator and no JSON field accepts a condition → **an admin can't use conditions**. Branching belongs to Quests.
  - **`set_vehicle_mortality` / `set_driver_mortality`**: only the exact `"mortal"` makes it mortal; no `value` or `"Mortal"` = invincible. **`set_vehicle_parts`**: only the exact `"unlocked"` makes the parts removable; any other value = **tamper-proof** (route default: `VehiclePartsLocked: true`). Mechanism: the runner sets `m_BZPartsLocked` (NetSync) on the `modded CarScript`, which overrides `CanReleaseAttachment` → `false`; the vanilla actions (`ActionDetach`, `ActionDetachFromTarget`, `ActionTakeItem`) ask this on the client, so the "remove wheel" prompt doesn't even show up and the server rejects it anyway. The default on the car is `false` (players' cars stay vanilla); when the vehicle is released as loot (driver dead) it goes back to `false`. It doesn't touch the cargo, getting in/out, or attaching parts. Vanilla only blocks removing parts with the engine running **and** moving.
  - **With the 1.2 brain**, `wp_reached` compares against the brain's cursor: on routes with `[Route] saneo de retrocesos: N>0` it can fire a few points late. Lights: since 992 `Tick12` calls `AplicarLucesYLatido` every tick (it recomputes `LightsMode` by time of day and pulses `BZPulseLights` for the other clients); **a `lights_on`/`lights_off` verb overrides `LightsMode` until the next spawn** (`m_LightsVerbo`), with both brains.
- **To configure:** see §10/§11 of the manual + Appendix (JSON examples of the 2 scenes).
- **To program a new verb:** add an `else if (verb=="x") {...}` branch in ExecuteAction (you have `car`, `action`, `m_WaypointIndex`); the dispatcher handles the delay. New fields → add them to `BZAction` (it is flat, it does not break the parser).

**Audio (`play_sound`):** 3D SoundSet attached to the vehicle (SEffectManager). Declare the `.ogg` in CfgSoundShaders+CfgSoundSets. **Gotcha:** a sound launched server-side may not reach the client → resend it via RPC (the `RECEIVE_TOAST` toast infrastructure already exists; see `BroadcastGlobal`, which sends an RPC to all clients → `NotificationSystem.AddNotificationExtended`).

---

## 9. Quest integration (DayZ-Expansion-Quests)

**Division of work:** Quest = live bots + mission logic (reward, progression); eAI = the bot's body; BZ_AutoDrive = vehicle + driving + coordinating boarding/getting off. A bot spawned standalone by the framework = a mannequin with no logic → the bots COME FROM the Quest.

**Hook (`BZQuestHook.c`):** `modded MissionServer { override Expansion_OnQuestStart(quest) { super; BZBusService.GetInstance().OnQuestStart(quest); } }`. (The `ExpansionQuestObjective*Event` subclass does NOT compile in scope 5_Mission → that is why the hook goes in MissionServer.)

**Poll (`OnQuestStart`/`CheckQuestBots`):** save `m_QuestCheckID = qc.GetID()`; CallLater poll of `ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(id, patrols)` (~every 2–4 s). `patrol.m_Group.Count()` = bots (they are lazy by proximity; the initial count is the PEAK, they materialize little by little 3→5). Count goes down → someone killed one → trigger.

**Boarding (`BoardQuestBots`) + the pacification gotcha:** a bot IN COMBAT does not walk to a waypoint (the eAI FSM requires "no threat"). You must: `b.eAI_SetPassive(true)` + `b.eAI_SetThreatDistanceLimit(0.0)` + drain the targets (`GetTarget(0)`/`eAI_RemoveTarget` in a loop). Only then does it walk to `transport.CrewEntryWS(seat, door, ddir)` and board with the animation (otherwise it boards by teleport, ugly). Seat 1+ (0=driver). Do NOT re-group (it preserves the quest's kill count). `SetThreat` does NOT exist in eAIGroup.

**Validated scenes:** `flee_on_kill` (pacify→board→drive→get off at the yard→OnQuestComplete→despawn) and `ambush_on_damage` (bots instantly on board + armed Boris → any damage [CarScript EEHitBy or health poll] → NotifyConvoyDamaged → freeze + dismount-all hostile). Insight: the ambush IS a generalized NUMPAD 4 marker (trigger on_damage → sequence); natural refactor = a recipe of the Events[] DSL.

**Objective types:** validated with AICamp/AIPatrol. The others (AIEscort, AIVIP, Travel, Delivery, Target, Action, Collection) are supported by the architecture but their hook is missing — open field (see the table in §16 of the manual).

**Relevant eAI API:** there is no imperative command bus; everything is GROUP + waypoints + reactive FSM. Locomotion: `FormationState=IN` (NONE=halt was the bug), `AddWaypoint` + `ForceRecalculate`, `OverrideMovementSpeed/Direction`. eAIBase IS PlayerBase. See the `reference_eai_*` memories.

---

## 10. UI

**Control Panel / Player** (opened with the `UABZAutoDrivePanel` action, default `HOME`/`kHome`): live dashboard of each runner (wp, speed, mode) + control (stop/pause/teleport). The old `BZControlPanelUI.c` is now **disconnected** — the action now opens the player (`BZReproductorUI.c`); `CTRL+HOME` was removed.
**Player** (`BZReproductorUI.c`, MENU_BZ_REPRODUCTOR id 51213): lists routes (`EnumerateRoutes` = `BZBusRoute*.json` excluding `_hdr`/tmp), LOAD&SPAWN (`RespawnFromPath`) without restart. Pretty() strips `BZBusRoute_`+`.json`.
**Layouts:** **CPP-style** format (`PanelWidgetClass {...}`), NOT XML/library (it crashes CreateWidgets). Ref: BrigadaZRadio/gui/layouts. .paa textures (PNG→PAA pipeline with ImageToPAA); "baked image + overlay" pattern.
**RPC (`BZBusRPC.c`):** enum with `RECEIVE_TOAST` (notification to clients), slots, stop/pause, telemetry. Since 1000: `EnviarAviso(p, msg, formato)` is the only per-player sender (toast = `ScriptRPC` `ScriptRPC` with `BZBusRPC.RECEIVE_TOAST` → each client does `NotificationSystem.AddNotificationExtended(6, "BZ_AutoDrive", msg, "")`; chat = `GetGame().ChatMP`). `AvisoLlegada`/`AvisoSistema` → `AvisoAdmins` (admins only, per the settings mode); `ui_broadcast` → `Difundir` (recipient + format). `BroadcastGlobal` no longer exists.
**UI pending:** key rebinding from the mod's own UI (today it is done from the game's Controls menu, see D.1); dynamic list vs fixed slots.

---

## 11. Programming / extending — code patterns

- **No mod of your own: launching and chaining routes from the mission's `init.c`** (manual §8.2). In `class CustomMission: MissionServer`: `override void OnInit() { super.OnInit(); CallLater(...60 s...) }`; each route with **its own runner** (`new BZBusService()` → `GetRunners().Insert(r)` → `SetHoldAtStops(false)` → `SetEtiqueta` → `RespawnFromPath("BZBusRoute_X.json")`), not with `PlayRouteByName` (its default `holdAtStops=true` leaves the vehicle held at the first stop, and it can reuse the primary runner, which `UnregisterRunner` doesn't remove). Chaining = a `CallLater(..., 5000, true)` that checks `RunnerRouteEnded()`/`RunnerRouteCompleted()` and launches the next one; a `RespawnFromPath` runner that ends COMPLETE removes itself (`EndHoldSeconds`); one that ends badly stays still → `UnregisterRunner`.

**Language:** Enforce (scope 3_Game→4_World→5_Mission; shared helpers go in the earliest scope). Build: AddonBuilder does NOT validate Enforce (only the runtime/RPT does).

**(a) New verb** → branch in `BZBusService.ExecuteAction`:
```c
} else if (verb == "honk") {
    // car, action (value/fvalue/msg/slot/...), m_WaypointIndex available
    BZBusLog.Info("[EVENT " + evIdx + "] honk @ wp " + m_WaypointIndex);
}
```
**(b) New trigger** → field in `BZTrigger` + `case` in the evaluator.
**(c) Control hook** → it is already a `modded CarScript.OnInput` override-last (§4). To touch the control, you edit `BZBusService` (the Tick/OnInput injects the inputs).
**(d) Quest hook** → `modded MissionServer.Expansion_OnQuestStart` (§9).
**(e) As a dependency (another mod)** → `requiredAddons[] = {"BZ_AutoDrive"}` + `BZBusService.GetInstance().RespawnFromPath("BZBusRoute_X.json")` / `RespawnAs(class)`. That uses the MAIN runner (one route at a time); for a fleet, (f).
**(f) The runner API (fleet, one runner per vehicle)** — full reference, written for humans, in the **manual, Appendix B.7**. The essentials, no RPC, all server-side:
- **Launch:** `PlayRouteByName(fname, holdAtStops)` (taxi: no duplicates, one runner per file) **or** `new BZBusService()` + `GetRunners().Insert(r)` (the ONLY strong reference) + `SetHoldAtStops(true)` + `SetEtiqueta(e)` (BEFORE loading) + `RespawnFromPath(fname)`; if `!RunnerIsActive() && !RunnerEsperandoCabecera()` → `UnregisterRunner(r)`. Finish: `UnregisterRunner(r)` (= `StopBus` + remove it from the registry; it deletes the vehicle and the driver). **Nothing retires itself.**
- **⚠ wp 0 occupied** (another `CarScript` < 3.5 m away): the runner WAITS (it retries every 5 s; it does NOT move the point), `RunnerIsActive()` false + `RunnerEsperandoCabecera()` true: **it is not a failure**. Panel `WAITING wp0 ocupado Ns` (= "wp0 occupied").
- **State (polling at 1 s):** `RunnerIsActive` · `RunnerWpIdx/WpTotal` · `RunnerPos/SpeedKmh` · `RunnerAtStop` · `IsHeld` (hold) ≠ `RunnerIsPaused` (soft pause) · `RunnerRouteEnded/RouteCompleted/EndReason` · `RunnerFellBackTo11` · `RunnerBloqueado/BloqueadoMotivo/BloqueadoDesdeS` (recovery ladder open) · `RunnerRecuperando/RecuperandoQue` (superset: + C1, F1, relocation) · `RunnerChoqueHaceS` (same definition as the `*** CHOQUE ***` (= "crash") line; −1 if there was none) · `RunnerEsperandoCabecera/EsperandoCabeceraS`.
- **Control:** `HoldRunner` (with a recorded plateau (a recorded stop) < 8 m away, it engages ON it; ⚠ a hold up to 8 m before a recorded stop counts that stop as served) · `ResumeFromHold` · `SetPaused` · `HornBeep` · `StopRunner` · `SetVehiclePartsLocked(bool)` / `VehiclePartsLocked()` (tamper-proof vehicle). **Identity:** `GetInstance` · `GetRunners` · `GetRunnerForCar` · `GetBus/GetDriver` · `GetRouteName` · `Etiqueta/NombreLog` · `GetOrigin` · `IsControlPanelAdmin(sid)` (public, fail-closed).
- **Events** (`PollEvent`, `BZDriveEv`): 1 ARRIVED · 2 ROUTE_COMPLETE · 3 STUCK · 4 ROUTE_INCOMPLETE · 5 BLOQUEADO (does not close). **The queue is internal**: the controller is private to the runner and the runner drains it itself; a consumer mod uses the `Runner*` methods (polling).
- **Counting in the RPT:** crashes per EPISODE (`*** CHOQUE #N ***` + `episodio cerrado` (= "episode closed"); `BACHE` (= "bump") counted apart), BLOQUEADO == `BLOQUEO terminado` (= "block ended") per car, black spots by POSITION (not by wp), `quedo:` (= "ended up at:") per spawn. ⚠ ms without leading zeros (`41.27` = 41.027 s); ⚠ the 40 Hz CSVs are named with the UTC time and WITHOUT a date: sort by file date.

**Car API (write):** `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. **(read):** `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (→wheelbase), `WheelGetSurface(i)`, `EngineGetRPMRedline()`, `GetGearCount()` (NOT `GetGearsCount`, obsolete). Config: `GetGame().ConfigGetFloat/Array("CfgVehicles/<cls>/...")`.

**CfgVehicles vs script classes:** `SurvivorM_*` is config, not script; in `.c` inherit from SurvivorBase/ManBase/ItemBase/House, never from the CfgVehicles entry. Engine classes (CGame) CANNOT be modded.

---

## 12. Gotchas and errors + fixes (CRITICAL — do not repeat)

### 12.0 — 1.2 gotchas (September 2026) — READ FIRST

Each of these cost us hours. They come first because they are the ones a modder (and you) will step on.

1. **Existing is not the same as having content.** Several framework flags were read with `FileExist`: an **empty** file counts as present. That is how the 1.2 brain depended on `CEREBRO_12.txt` (without it, 1.1 ran **silently**) and an emptied `BZ12_CALIBRATE.txt` left the harness mute. Rule: **to disable, delete; to read a flag, look at its content.**
2. **Check the RPT for `NULL pointer`, not only for `Can't compile`.** `grep -c "NULL pointer to instance"`. The pattern is `SCRIPT    (E)` with **four spaces**: a search for `ERROR` does not find it. An exception in `Sample` aborts the rest of the frame and **the vehicle is left with its last input frozen** (throttle 1 → "full throttle without moving"). That is how 372-719 exceptions per session stayed hidden for a whole day (fix 938).
3. **The physics is deterministic.** Same vehicle, same spawn point ⇒ **the same run, bit for bit** (two runs 20 minutes apart gave the same 9 samples in the same order, with the pitch within 4e-5°). Repeating a test from the same spawn **adds no information**.
4. **Speed read ≠ movement.** When launching from standstill, `|GetVelocity|` climbs to ~1.8 km/h and resets 1-3 times **without the vehicle moving 10 cm** (63 % of 203 human launches, all classes, and the same with the NPC). Consequences: the take is **not a reference** for the launch, and "still" (0.5 km/h) is crossed by the reading before any movement. To know whether it moved, **look at the position**, not the speed.
5. **The mass is the config's**, not the real vehicle's (Tiger: 2,000 kg for the game engine). Any physical reasoning uses what `[Manual] masa=` (= "mass=") prints.
6. **The keyboard is binary.** 95 % of the recorded launches have full throttle: you cannot derive any intermediate pedal value from a keyboard take.
7. **The clock and the "finished" signal come from the RPT, never from the script that orchestrates the test.** A `sleep` in a test loop invented a 37 % spread that was really 0.4 %; a `DESPAWN FINAL` caused by the flag change itself was read as the end of the route. For durations: `FIN: ROUTE_` and the RPT timestamps.
8. **A median over a histogram with a small n is checked by hand.** `MedianOf` asked for rank `n/2` (the 2nd of 5) and two different calibrations gave **the same** number. **Same output with different inputs = the instrument is not reading.**
9. **A `Save()` must not delete what it does not understand.** It saved only its own fields, and the provenance written by the converter (`fp`, `medido`) was lost on the first save. Today it is kept (`m_Extra`); any new writer of a shared file must do the same.
10. **Parsers with generic `return`s:** a new branch for a 2-field line type ended up **after** an `if (p.Count() < 3) return;` and never ran. Test a new branch **with the real line** from the file, not with made-up data.
11. **For "they are equal", use a tolerance, not rounding.** Rounding to bins has boundaries: two values 0.0002° apart fell into different bins. With more decimals it fails more often; with fewer, it fails less, but it still fails.
12. **An old take does not guarantee a clean run**, and for stochastic routes the thread already set how many passes are needed (Svergino: ≥ 5). **Before you run something to prove a point, check whether the evidence criterion is already written down.**

**Enforce:**
- No ternary `?:`; no multiline `if` with `&&` at the start of a line → "Syntax error". One line, or intermediate bools.
- `new Class(args)` blows up → create it empty + set the fields.
- "Formula too complex" at ~9 operands with `+` → split with `+=`.
- The scopes of sibling branches (if/else, case) COLLIDE (unlike C++) → rename/hoist.
- Inline arithmetic in a string concat (`"x"+(seat-1)`) breaks → hoist it to an int.
- A ternary/arithmetic inside a concat also breaks.
- `PlayerBase.Cast(driver)` returns true for eAI → tell the real player apart by `GetIdentity()`.
- Left-handed cross product: `cross = AB.z·AP.x − AB.x·AP.z` (inverted = divergence).
- **Widget method that does not exist** (e.g. `MultilineTextWidget.SetLineColor`): it **compiles**, but on load it throws *"Undefined function"* and **kills the Mission module** → the UI does not open. `MultilineTextWidget` has no per-line color: paginate with individual `TextWidget`s (`TextWidget.SetColor` **does** exist). Rule: check that the method exists in the API before you use it (`enforce_lint` now checks this).
- AddonBuilder detects none of this; confirm in the RPT when the server loads.

**Pipeline/data:**
- **(historical) the old `csv_to_route` left the OLD `_hdr/_wp` pair** → `frame_to_route.py` now writes all three. If it spawns the previous vehicle: re-convert with the Wizard + sync the PAIR to B.
- **Attachments:** never guess; take them from the fingerprint/header. When you change VehicleClass, replace them (the converter keeps the old ones).
- **LoadConfig at runtime blocks the main thread** (JSON 2.6MB = 108s = client disconnect) → load only in Init(); the fast-load pair (FGets) is for hot reload.
- **PowerShell Set-Content/Out-File -Encoding UTF8 adds a BOM** and breaks the Expansion/DayZ parser → use `[IO.File]::WriteAllText` with `UTF8Encoding($false)`. For edits that keep accents/§: read/write Latin1 (codepage 28591) or use the editing tool.
- **Report tooling (`report_export.ps1`) — PS5.1 encoding subtleties (the opposite of the above):** a `.ps1` with **accents in its own code/strings** needs **UTF-8 WITH BOM** (PS5.1 reads a .ps1 WITHOUT BOM as ANSI → mojibake in the report texts). Do NOT confuse this with the files the mod reads (those go WITHOUT BOM). The intermediate data JSON that the script builds is written **without BOM** (the HTML/Edge reads it). Also: passing `@($obj)` to a `NoteProperty` **breaks** → use `.ToArray()`.

**Build/deploy:**
- When you move/rename/delete a `.c`, clear AddonBuilder's `temp/` folder before repacking (otherwise it packs old versions; "zombies in temp"). If your pipeline copies the PBO to a local server, close the server and the client first (while they are open they lock the PBO → the copy fails).
- `-DeployClient` leaves a stray `@<old>` in `!Workshop` when you rename → delete it.
- meta.cpp in `MODS\` is not touched by the source replace → migrate `name` by hand.

**Client/launcher:**
- **DZSALauncher caches its mods IN MEMORY**: editing `%LOCALAPPDATA%\DayZ Launcher\Local.json` (knownLocalMods/userDirectories) does NOT take effect until you EXIT IT COMPLETELY (tray→Exit; closing the window is not enough). Symptom: "The client has a PBO that is not on the server". The `meta.cpp` defines the `name` it shows; two local mods with the same name get mixed up.
- For local testing, the client can load `@BZ_AutoDrive` from the server folder, without !Workshop or a subscription.

**VISUAL state on an observed AI car (NETWORK gotcha — GOLD):**
- **`LightOn/LightOff/LightIsOn/LightToggle` are proto native methods of `Transport` = a LOCAL engine flag, NOT a NetSyncVariable.** The engine only sets them on the client that **SIMULATES** the car (the owner / the player in the driver's seat). An **observer of an AI car** (Boris drives, no player in seat 0) **NEVER** sees `LightIsOn()==true` → `UpdateLightsClient` never creates `m_Headlight` → **the beam is not visible**, even though the server "turns on" the lights.
- **Attempts that were NOT enough:** `ToggleHeadlights()` alone; `ToggleHeadlights() + ForceUpdateLightsStart/End` (it re-fires `UpdateLights()`, but `UpdateLightsClient` still sees `LightIsOn()==false`).
- **FIX v3 (in `BZBusCarScript.c`, the `modded CarScript`):** our own NetSync **`m_BZLightsWanted`** (`RegisterNetSyncVariableBool` in the ctor) + a server method **`BZSetLights(bool)`** (server: native toggle + `SetSynchDirty()`) + an override of **`OnVariablesSynchronized()`** that, **on the CLIENT**, forces `LightOn()/LightOff()` + `UpdateLights()` according to `m_BZLightsWanted`. Use `LightOn/Off` and **NOT `LightToggle`** (that one goes through `OnBeforeLightOn`, which checks the battery — not synced on the observer).
- **The horn does NOT have this problem:** `SetCarHornState` already uses `RegisterNetSyncVariableInt` + `SetSynchDirty` in vanilla → it syncs to the observer on its own.
- **GENERAL LESSON (apply it to any future visual feature):** any **VISUAL** state that the server wants to show on an **observed AI car** (lights, body animations, etc.) needs its **own NetSyncVariable** + forcing it **client-side** in `OnVariablesSynchronized`. The engine only replicates the state to the client that SIMULATES the vehicle; the others do not see it unless you sync it yourself.

**Others:**
- Limit of ~38 mods (Steam query ~255 bytes); 40 breaks with "Server can't transmit all data".
- New mod = copy its `.bikey` to `keys/` (otherwise the client says "Missing PBO").
- No automatic A↔B sync (deliberate manual copy; sync tools failed before).
- XML layouts crash → CPP-style.
- Objects from the binary .map (wrecks) are not deleted with ObjectDelete → `GetSuppressedObjectManager().Suppress(obj)` (requires @DayZ Editor Loader) or Expansion's declarative mapping.

### 12.A — Catalog of real errors we had → how we solved them

This is what actually happened to us and cost us time. When an admin/modder reports a similar symptom, map it straight to the fix.

**Compile / script (Enforce):**

| Symptom / message | Cause | Fix |
|---|---|---|
| **"Syntax error"** pointing at the class line + an inner line | multiline `if` with `&&` at the start of a line, ternary `?:`, or inline arithmetic in a string concat (`"x"+(seat-1)`) | a single line; or hoist to an intermediate bool/int first |
| **"Formula too complex"** | >~9 operands chained with `+` (typical when building CSV/log strings) | split into several assignments with `+=` |
| Variable "already defined", or a value overwritten between branches | the scope of sibling branches (if/else, case) **collides** (it is not like C++) | rename, or hoist the variable above the block |
| **"Unknown type"** when inheriting from `ExpansionQuestObjectiveAICampEvent` in scope 5_Mission | the objective subclass does not resolve in that scope | do NOT inherit from the objective; use the hook `modded MissionServer Expansion_OnQuestStart` + poll `QuestPatrolExists` |
| Crash when **loading** the server after `modded class CGame` (or another engine class) | engine classes do not accept `modded class` | do not mod engine classes; control goes through override-last in `CarScript`, hooks through `MissionServer` |
| The constructor blows up (`new BZAction("verb")`) | Enforce does not take args in the constructors of simple classes | create it empty and set the fields (`a = new BZAction(); a.verb = "x";`) |
| Warning **"GetGearsCount is obsolete"** | old API | use `Car.GetGearCount()` |
| **The PBO compiles OK but the server does not start / throws errors** | **AddonBuilder does NOT validate Enforce** (it packs anyway, syntax errors included) | read the **RPT** on load — the real errors show up there, with file:line |
| The PBO packs OLD versions of a `.c` after renaming/moving/deleting | AddonBuilder reuses the temp | clear AddonBuilder's `temp/` folder before repacking |
| `CreateWidgets` crashes when a UI opens | layout in XML/library format | rewrite the layout in **CPP-style** format (`PanelWidgetClass {...}`) |

**Runtime / logic:**

| Symptom | Cause | Fix |
|---|---|---|
| An event fires with `type=''` (phantom trigger) | `JsonFileLoader` instantiates an empty default `BZTrigger` while parsing | handle/filter the empty default (see `feedback_jsonfileloader_self_referential`) |
| The framework treats the real player as a bot (or the other way round) | `PlayerBase.Cast(driver)` returns **true for eAI too** | tell the real player apart by `GetIdentity()` |
| Stanley corrects to the wrong side; the bus steers itself into the water in ~80 s | inverted cross-product sign (DayZ's left-handed system) | `cross = AB.z·AP.x − AB.x·AP.z` |
| `ValidateSpawn` gives a false OK or the retry fails | it measured the distance from the spawn, not movement | measure `kmh > 0.5` (the vehicle responds) |
| Boris starts OK and after ~20 s jumps 3 km / appears under the sea | `SmoothPath()` divided by a hardcoded 0.2 → `window≠5` scaled all the wps (bug 7B9D0036) | fixed; also `PathSmoothWindow=0` on routes with tight curves |
| Boris cuts the curves on the inside | the centroid of N wps used for the lookahead falls inside the arc / high smoothing | interpolate **on** the path; `PathSmoothWindow=0` |
| `check_once` broke the JSON load | branching verb that was poorly supported | removed from the DSL; conditional branching belongs to Quest |
| A bot stays a "mannequin" / boards by teleport instead of walking | the eAI FSM does not navigate with an active threat; or `FormationState=NONE` (halt) | pacify it (`eAI_SetPassive(true)` + threat 0 + drain targets); `FormationState=IN` + `AddWaypoint` + `ForceRecalculate` |

**Build / deploy / client (the ones from the migration session):**

| Symptom | Cause | Fix |
|---|---|---|
| copying the PBO to the server (`robocopy`) exits **8/9** | the server OR the client, while open, lock the PBO | close **both** before building/mirroring; ask "open or closed?" |
| After deploying a NEW route, the **previous vehicle** spawns | OLD `_hdr.json`/`_wp.csv` pair (the framework **prefers the pair**); it happens with old flows — `frame_to_route.py` now writes all three | re-convert with the Wizard (it writes the four files) + sync the PAIR to B (not only the `.json`) |
| Client kicked: **"The client has a PBO that is not on the server"** | the DZSALauncher caches its mods IN MEMORY + old copies were left behind (`Local.json`, `!Workshop\@old`) | **EXIT the launcher COMPLETELY** (tray → Exit) after editing `Local.json`; delete the old copies of the mod |
| "Missing PBO" on connect | the mod's `.bikey` is missing from `keys/` | copy the `.bikey` to `keys/` (A and B) |
| The launcher kept loading the old mod even though I edited the file | the `meta.cpp` had the old `name` (two local mods with the same `name` get mixed up) + the launcher does not re-read until it restarts | migrate the `name` in `meta.cpp`; restart the launcher completely |
| Server boot takes ~3 min | huge route JSON parsed in `Init()` | split the route / use the fast-load pair (FGets); lazy-load (future) |
| 40 mods → "Server can't transmit all data" | ceiling of ~38 mods (Steam query ~255 bytes) | remove non-essential mods or merge small PBOs |

---

## 13. Build & deploy

**To use the mod:** subscribe on the Workshop (it comes signed). **To build a fork:** pack `BZ_AutoDrive.pbo` (prefix `BZ_AutoDrive`) with DayZ Tools (AddonBuilder) or your own pipeline and sign it with your **own** key; copy your `.bikey` to the server's `keys/` folder and list `@BZ_AutoDrive` in the `-mod=` of the start.bat. No build script is distributed — each modder packs in their own way. **Universal gotcha:** when you rename/move/delete a `.c`, clear AddonBuilder's `temp/` folder before repacking (otherwise it packs old "zombie" versions). Before the Workshop: swap-and-test with `publishedid=0` + a clean RPT + a smoke test before uploading.

---

## 14. Research methodology (how to continue)

**The loop (manual ILC):** record 3 AI runs (the `ai_run` check in the Player) → measure the lateral deviation vs the human recording per wp → identify systematic clusters (not noise) → form a hypothesis about the technical cause → design a fix that does NOT break the commitments (do not modify eAI, do not break generalization) → validate with a new take.

**Analyzing an ai_run:** key columns `wp_idx`, `lateral_dev_m`, `speed_kmh` vs `target_speed`, `steering`, `gear`, `mode`. Look for: steering saturations (|steer|→1), sign changes (zigzag), speed deficit (Boris −10 km/h vs target = off-path→slow→off-path cascade), stuck (full throttle + speed 0 = wedged; without AR it does not get free). **Lateral bias:** use the MEDIAN + split straights and curves (the signed average misleads because of outliers/saturations).

**Diagnosis→fix heuristics (the Wizard's knowledge base):** lateral bias → `CruiseLateralCenterOffset` (~25m/unit); steering saturation → lower `SteeringScale`; lugging → `GearStrategy=follow_recording`; zigzag → `CruiseLateralDamp`/`TargetSpeedSmoothWindow`; cuts curves → `PathSmoothWindow=0`; slow off-path → AutoRecovery + smart throttle cap; steering jerks before a curve → binary brake (0/1 keyboard) → cascade (brake rate limiter pending).

**Stable workflow:** record → stop server → deploy JSON → start server → test. Do NOT iterate with NUMPAD 2 on a live server if you changed code. For JSON-only changes: record→deploy→NUMPAD 2 without a restart works. ALWAYS tick the **`ai_run`** check (opt-in, in the Player) in tests, otherwise there is no ai_run.

**Guiding principles:** (1) config as a manual; (2) spatial > temporal fidelity (arrive in order, not on time); (3) framework = extractor of the optimum (the Boris-vs-human diff = information, not a bug); (4) path > inputs (the trajectory is sacred, the inputs are noisy); (5) recording = the vehicle's manual (cross-vehicle use invalidates it); (6) dual audience (Wizard + override).

---

## 15. The open frontier (what to research next)

- **Endpoint (final stop) sub-0.5 m — SOLVED (2026-08-11, no longer a frontier):** the auto-adaptive stopping brake, per vehicle+surface, nails the stop **under 0.5 m in most cases** (up to ~1 m on the large / long-wheelbase ones), forward and reverse, validated out-of-sample (§2). This includes the endpoint after a curve, a historical weak spot (SEQ1 at 44 km/h → 0.34 m). The only thing still open here is **reverse on a tight curve** (next bullet).
- **Reverse on a tight curve:** close the ~1 m of divergence. Options: read `WheelGetContactPosition` of the real rear axle (not the approximate wheelbase/2 of the tandem), model the 3D slope, more fine-correction authority. Or apply **reference-assisted control to reverse** (steering from path+config instead of replay; the short 2-axle UAZ is the best test case). The forward→reverse transition (brake to 0 → R lever → controller) is where Boris gets stuck.
- **Extreme driving (v3):** drifts/countersteering. Our own physics override (skip the eAI receiver on those stretches) or an ML residual. The honest limit today.
- **100% autonomous Wizard:** internalize §14 (self-analysis after the run + automatic application of the heuristics).
- **Multi-vehicle:** convoy with spacing by breadcrumb trail (ARMA setConvoySeparation style).
- **LLM-driven NPC:** the framework is ALREADY LLM-shaped — the event DSL = the action space, the graph = the navigation API, config-read = the vehicle's perception. The LLM governs (slow, high level) and the reactive skills execute (fast). Provider-agnostic design. Wider vision: an **autonomous survivor** (Voyager-for-DayZ) where driving is ONE skill.
- **Trains:** ~70% covered with no changes (rails = network, no steering, only throttle+brake).
- **Other engines:** the principle (demonstration + config-read + classic control) is portable.

---

## 16. Glossary and key references

- **1.2 terms** (details in §5): **tape** = the drive recorded at 40 Hz, indexed by arc length (`_cinta.csv`) · **envelope** = the MEASURED profile of a vehicle class (`envelope_<clase>.csv`) · **creep** = crawling at idle (0 across the whole fleet) · **launch floor** = the minimum throttle that breaks rest (fleet prior 0.13) · **plateau** = a recorded stop that the tape holds · **leg** = the stretch between direction changes · **cusp** = the direction-change point · **POSE** = modules that work against a point at ~0-2 km/h (maneuver, endpoint) · **provenance** = CONFIG / DERIVED / MEASURED / FALLBACK for each value in the vehicle's manual.
- **Expansion dependencies:** the mod requires (`config.cpp`, `requiredAddons`) AI, Vehicles and **Quests** —Quests even with no missions—, and **all three come in `@DayZ-Expansion-Bundle`**. ⚠ **Never tell a user to add `@DayZ-Expansion-AI` on its own** if they have the Bundle: same classes, a different version, and the server does not start.
- **eAI** = DayZ-Expansion-AI (the base AI). **Boris** = the NPC driver (eAI_SurvivorM_Boris). **Override-last** = running OnInput after eAI. **Fingerprint** = vehicle data captured while recording (header_*.txt). **R_min** = minimum turning radius = wheelbase/tan(maxSteer). **Corridor/paredón (wall)** = the dead band of the lateral control. **BZ-Score** = 0–100 score (**standalone reports** tooling; the converter Wizard no longer computes it). **Runner** = a running vehicle instance. **Triple match** = A=B=client, same PBO.
- **Demo classnames:** CarPack vehicles (viper_yellow, x5mcompetition_orange, Star_APC_Cobra_white, Star_Golf_MK1, etc.), UAZ_452 (mod @[CnG]UAZ_452), vanilla (Hatchback_02, M3S/V3S), **CivilianSedan_Wine** (EXAMPLE02: 3rd example vehicle, replay with the human's lights+horn on the same vehicle — 98.6 % completion, lat-dev 0.79 m).
- **The cut rule (§2/§5), conceptual:** the direct replay of a maneuver reproduces VEHICLE-SPECIFIC steering angles (same angle → radius ∝ wheelbase), so it **generalizes to another vehicle only if the cut into the maneuver block lands where the trajectory is STRAIGHT** (the curve stays in closed-loop Stanley, vehicle-agnostic; the straight goes in replay). Cut in a curve → open loop → a different wheelbase drifts → it does not generalize. *(The mod does NOT include example takes: a recorded route is coordinates on one specific map.)*
- **Sibling docs:** the **manual** (`MANUAL_BZ_AutoDrive.en.md`, a teaching guide for admins/modders). The **CHANGELOG** (`CHANGELOG.en.md`) has the version history.

---

## Appendix C — Faithful code listings (by component)

> Real excerpts from the source (exact method/field names; long bodies trimmed with `// ...`). For the full detail, grep the cited file.

### C.1 `BZBusConfig.c` — the DSL classes

```c
class BZBusRouteConfig {
    int    RespawnDelay = 300;        float AverageSpeedMS = 11.0;   float SpawnHoldSeconds = 3.0;
    string VehicleClass = "ExpansionBus";   string DriverClass = "eAI_SurvivorM_Boris";
    bool   VehicleInvincible = true;  string ConvoyMode = "";        int   MaxGear = 6;
    bool   VehiclePartsLocked = true;   // tamper-proof vehicle: players can't remove its parts
    ref array<string> Attachments = new array<string>();
    // velocidad/modo
    string GearStrategy = "auto_box"; bool FollowPath = false; bool FollowPathUseReference = false;
    float  FollowPathLatAccel = 4.0;  float FollowPathMaxKmh = 50.0;
    bool   UseInverseModel = false;   float InverseModelKp/Ki/Kd = -1;  bool InverseModelLowRpmMin = false;
    int    TargetSpeedSmoothWindow = 0;  float AccelShiftThreshold = 999.0;
    // dirección
    float  SteeringScale = -1;  int PathSmoothWindow = 5;  float CurvatureSteerBoost = 0;
    float  CruiseLateralDeadband = 0; float CruiseLateralKGain = 1.0; float CruiseLateralDamp = 0; float CruiseLateralCenterOffset = 0;
    float  CruiseFFWeight = -1;  bool CurveThrottleEnabled = true; /* +LookaheadM/StartDeg/FullDeg/MinScale */
    // pendiente / recovery
    bool   SlopeCompensationEnabled = true; int SlopeLookaheadWps = 5; float SlopeGain = 1.0; float SlopeLateralGain = 1.0;
    bool   AutoRecoveryEnabled = false; float AutoRecoveryStuckTimeS = 10.0; int AutoRecoveryAdvanceWps = 5;
    // maniobra / parking / reverse
    float  ManiobraTargetSpeedCap = 18.0; bool ModeEntrySnapEnabled = false; bool AntiRollbackEnabled = true; // ModeEntrySnap: false desde 2026-07-03 (era true)
    float  ParkingStanleyK = -1; float ParkingFFWeight = -1;  float Wheelbase = 0;
    float  ReverseStanleyK = -1; float ReverseRecordedSteerThreshold = 0; /* +Reverse* gates */
    int    EndFreezeDisabled = 0;
    // luces / bocina (Fase 2 replay) — el claxon/luz GRABADO se reproduce por wp; estos controlan el modo AUTOMÁTICO
    string LightsMode = "auto";     // auto / auto_inverted / on / off  (replay: la toma 1.2 no graba luces)
    string HornMode   = "off";      // off / stops / finish  (replay: la toma 1.2 no graba bocina)
    // contenido
    ref array<ref BZMarkerEvent> Events = new array<ref BZMarkerEvent>();
    ref array<ref BZCrewMember>  Crew   = new array<ref BZCrewMember>();
    ref array<ref BZWaypoint>    Waypoints = new array<ref BZWaypoint>();
}

class BZAction {                 // un verbo + sus parámetros (struct unión)
    string verb;  ref array<ref BZCargoItem> items; string msg; int slot = -1;
    string value; float fvalue; string var; string faction; string loadout; int count; float delay;
}
class BZTrigger { string type = "wp_reached"; int wp; float radius; float threshold; float seconds; }
class BZMarkerEvent { int wp; ref BZTrigger trigger; ref array<ref BZAction> actions; }
class BZCrewMember { string cls = "eAI_SurvivorM_Boris"; int seat = 1; string faction = "Raiders"; string loadout = "BanditLoadout"; float offsetRight; float offsetForward; }
```

### C.2 `BZBusCarScript.c` — the override-last

```c
modded class CarScript {
    override void OnInput(float dt) {
        super.OnInput(dt);                       // eAI corre primero (mete FIRST)
        if (!GetGame().IsServer()) return;
        BZBusService srv = BZBusService.GetRunnerForCar(this);
        if (!srv) return;
        Human driver = CrewMember(0);            // si el seat 0 es un PLAYER real, no tocar
        if (driver) {
            PlayerBase realPlayer = PlayerBase.Cast(driver);
            if (realPlayer && realPlayer.GetIdentity()) return;   // ¡discriminar por GetIdentity!
        }
        srv.ApplyBusInput(this, dt);             // sobreescribe throttle/steer/brake/handbrake
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) ShiftTo(desired);   // pisa el FIRST de eAI
    }
}
```

### C.2b `BZBusCarScript.c` — lights synced on an observed AI car (network gotcha, §12)

```c
modded class CarScript {
    bool m_BZLightsWanted;   // NetSync: estado de luz que el SERVER quiere en los clientes

    void BZBusCarScript_ctor() { /* en el ctor real: */
        RegisterNetSyncVariableBool("m_BZLightsWanted");   // canal de script-sync (replica a TODOS los clientes)
    }

    void BZSetLights(bool on) {                 // SERVER: lo llama BZBusService por waypoint
        if (!GetGame().IsServer()) return;
        if (on) LightOn(); else LightOff();     // toggle native local del server
        m_BZLightsWanted = on;
        SetSynchDirty();                         // dispara OnVariablesSynchronized() en los clientes
    }

    override void OnVariablesSynchronized() {    // corre en TODOS los clientes (incl. el que solo OBSERVA)
        super.OnVariablesSynchronized();         // la base llama UpdateLights() al final
        // El engine NO setea LightIsOn() en un observador de un AI car -> lo forzamos a mano:
        if (m_BZLightsWanted && !LightIsOn())  { LightOn();  UpdateLights(); }   // crea m_Headlight
        else if (!m_BZLightsWanted && LightIsOn()) { LightOff(); UpdateLights(); }   // FadeOut + null
    }
}
// NOTA: usar LightOn/LightOff (NO LightToggle: pasa por OnBeforeLightOn que chequea batería,
//       no synced en el observador). La bocina NO necesita esto (SetCarHornState ya es NetSyncVariableInt).
```

### C.3 `BZBusService.c` — control injection

```c
void ApplyBusInput(Car bus, float dt) {
    if (!bus) return;
    bus.SetThrottle(m_CachedThrottle);
    bus.SetSteering(m_CachedSteering);
    bus.SetBrake(m_CachedBrake);
    bus.SetHandbrake(m_CachedHandbrake);   // anti-rollback en pendiente
}
// DriveTowards(...) computa Stanley sobre un lookahead ADAPTATIVO y cachea los inputs:
//   targetYaw = segmentHeading - atan(K * lateralOffset / velocity)   (corredor, sin escalón)
```

### C.4 `BZBusService.c` — `ExecuteAction` (real dispatcher, 22 verbs)

```c
private void ExecuteAction(Car car, BZAction action, int evIdx) {
    if (!action) return;
    string verb = action.verb;
    if (verb == "add_cargo")              { SpawnCargoItems(car, action.items); }
    else if (verb == "log_event")         { BZBusLog.Info(action.msg); }
    else if (verb == "freeze_vehicle")    { m_Frozen = true; }
    else if (verb == "unfreeze_vehicle")  { m_Frozen = false; SetCachedHandbrake(0.0); }
    else if (verb == "set_vehicle_mortality") { car.SetAllowDamage(action.value == "mortal"); }
    else if (verb == "set_vehicle_parts")     { m_VehiclePartsLocked = (action.value != "unlocked"); AplicarBloqueoDePiezas(); }
    else if (verb == "set_driver_mortality")  { if (m_Driver) m_Driver.SetAllowDamage(action.value == "mortal"); }
    else if (verb == "start_engine")      { if (!car.EngineIsOn()) car.EngineStart(); }
    else if (verb == "stop_engine")       { if (car.EngineIsOn()) car.EngineStop(); }
    else if (verb == "despawn_vehicle")   { GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.StopBus, 50, false); }
    else if (verb == "stop_route")        { m_RouteStopped = true; }
    else if (verb == "resume_route")      { m_RouteStopped = false; }
    else if (verb == "set_var")           { SetScenarioVar(action.var, action.value); }
    else if (verb == "play_sound")        { EffectSound s = SEffectManager.PlaySoundOnObject(action.value, m_Bus); if (s) { s.SetSoundLoop(false); s.SetSoundAutodestroy(true); } }
    else if (verb == "repair_vehicle")    { car.SetHealth("", "", car.GetMaxHealth("", "")); }
    else if (verb == "refuel")            { car.Fill(CarFluid.FUEL, 99999); }
    else if (verb == "drain_fuel")        { car.Leak(CarFluid.FUEL, 99999); }
    else if (verb == "ui_broadcast")      { Difundir(destUb, action.msg, action.radius, fmtUb); }   // destUb/fmtUb = value/format normalizados
    else if (verb == "spawn_guard")       { SpawnGuards(car, action.count>0?action.count:1, action.faction, action.loadout, action.value); }
    else if (verb == "dismount_guard")    { DismountCrew(car); }
    else                                  { BZBusLog.Warn("verbo no implementado: '" + verb + "'"); }
}
```
*(The caller schedules the `delay` with CallLater; this dispatcher runs the action once its delay is over.)*

### C.5 `BZBusService.c` — Quest integration

```c
void OnQuestStart(ExpansionQuest quest) {
    ExpansionQuestConfig qc = quest.GetQuestConfig(); if (!qc) return;
    m_QuestCheckID = qc.GetID(); m_QuestPollTries = 0; m_QuestConvoyActive = false; m_QuestFleeing = false;
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 4000, false);
}
void CheckQuestBots() {
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    bool exists = ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols);
    int totalBots = 0;
    if (exists) for (int i=0;i<patrols.Count();i++) if (patrols[i] && patrols[i].m_Group) totalBots += patrols[i].m_Group.Count();
    bool ambush = (m_Config && m_Config.ConvoyMode == "ambush_on_damage");
    if (totalBots > 0) {
        if (!m_QuestConvoyActive) { m_QuestConvoyActive = true; m_QuestInitialBots = totalBots; if (!m_Bus) RespawnBus(); }
        if (ambush) { if (m_Bus) BoardAmbushBots(); }
        else if (!m_QuestFleeing && totalBots < m_QuestInitialBots) { m_QuestFleeing = true; BoardQuestBots(); }  // bajó el conteo = mataron uno
    }
    m_QuestPollTries++;
    if (!m_QuestFleeing && m_QuestPollTries < 600)                                  // poll 600×3s ≈ 30 min
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 3000, false);
}
void BoardQuestBots() {
    Transport transport = Transport.Cast(m_Bus); if (!transport) return;
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
    int seat = 1;
    for (int i=0;i<patrols.Count();i++) { eAIQuestPatrol p = patrols[i]; if (!p || !p.m_Group) continue;
        for (int m=0;m<p.m_Group.Count();m++) {
            if (seat > 5) break;                          // 5 plazas de pasajero
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m)); if (!b) continue;
            // PACIFICAR: eAI_SetPassive(true) + eAI_SetThreatDistanceLimit(0) + drenar targets
            // luego emitir WALK waypoint a la puerta (no teleport); el Tick lo sube
            m_Crew.Insert(b); m_CrewSeats.Insert(seat); seat++;
        }
    }
}
```

### C.5b `BZBusService.c` / `BZBusCarScript.c` — convoy ambush and cleanup

Complement to C.5. The `ambush_on_damage` scene uses **instant** boarding (no walk-in), **two damage triggers**, a hostile dismount, and a completion hook that despawns the vehicle.

**Instant boarding + arming Boris (`BoardAmbushBots`):**
```c
void BoardAmbushBots() {
    if (!m_Bus) return;
    Transport transport = Transport.Cast(m_Bus);
    if (!transport) return;
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
    // ... init lazy de m_Crew / m_CrewSeats / m_CrewBoard / m_CrewLastHealth ...
    int maxSeat = transport.CrewSize() - 1;   // seat 0 = Boris; 1..maxSeat = pasajeros
    for (int i = 0; i < patrols.Count(); i++) {
        eAIQuestPatrol p = patrols[i];
        if (!p || !p.m_Group) continue;
        for (int m = 0; m < p.m_Group.Count(); m++) {
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
            if (!b || m_Crew.Find(b) >= 0) continue;          // idempotente
            int seat = m_Crew.Count() + 1;
            if (seat > maxSeat) break;
            vector door; vector ddir;
            transport.CrewEntryWS(seat, door, ddir);
            bool hd = false; string ds = "";
            ExpansionFSMHelper.DoorAnimationSource(m_Bus, seat, hd, ds);
            BZBoardState e = new BZBoardState();
            e.bot = b; e.seat = seat; e.timer = 0; e.hasDoor = hd; e.doorSrc = ds; e.entry = door; e.phase = 0;
            m_CrewBoard.Insert(e); m_Crew.Insert(b); m_CrewSeats.Insert(seat);
            m_CrewLastHealth.Insert(b.GetHealth("", ""));
        }
    }
    if (!m_BorisArmed && m_Driver) {
        ExpansionHumanLoadout.Apply(m_Driver, "BanditLoadout", false);   // Boris armado
        m_BorisArmed = true;
    }
    // activación lazy: cuando estén todos a bordo -> m_AmbushActive = true
    int liveBots = 0;
    for (int pi = 0; pi < patrols.Count(); pi++)
        if (patrols[pi] && patrols[pi].m_Group) liveBots += patrols[pi].m_Group.Count();
    int target = maxSeat; if (liveBots < target) target = liveBots;
    if (!m_AmbushActive && target > 0 && m_Crew.Count() >= target) m_AmbushActive = true;
}
```

**Damage triggers (`NotifyConvoyDamaged` + `EEHitBy` + health poll):**
```c
// One-shot: el primer daño congela el vehículo y agenda el despliegue.
void NotifyConvoyDamaged() {
    if (!m_AmbushActive || m_AmbushTriggered) return;
    m_AmbushTriggered = true;
    m_Frozen = true;                          // handbrake + brake = parada DURA
    m_AmbushStopTries = 0;
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.AmbushWaitStop, 400, false);
}

// Trigger 1 — daño al chasis (BZBusCarScript.c). El chasis delega al runner DUEÑO (multitón):
override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source,
                      int component, string dmgZone, string ammo, vector modelPos, float speedCoef) {
    super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
    if (!GetGame().IsServer()) return;
    BZBusService srv = BZBusService.GetRunnerForCar(this);   // multitón: dueño de este auto
    if (srv) srv.NotifyConvoyDamaged();
}

// Trigger 2 — disparo a un pasajero (poll de salud dentro de CheckQuestBots):
if (m_AmbushActive && !m_AmbushTriggered && m_Crew && m_CrewLastHealth) {
    for (int hi = 0; hi < m_Crew.Count() && hi < m_CrewLastHealth.Count(); hi++) {
        if (!m_Crew[hi]) continue;
        float hNow = m_Crew[hi].GetHealth("", "");
        if (hNow < m_CrewLastHealth[hi] - 1.0) { NotifyConvoyDamaged(); break; }
        m_CrewLastHealth[hi] = hNow;
    }
}
```

**Hostile deployment (`AmbushDismount`):**
```c
void AmbushDismount() {
    if (!m_Bus) return;
    int dc = DismountCrew(Car.Cast(m_Bus));                  // baja a los pasajeros
    if (m_Driver) {
        eAIGroup bg = m_Driver.GetGroup();
        if (bg) bg.SetFaction(eAIFaction.Create("Mercenaries"));   // hostil al jugador, sin FF con el convoy
        HumanCommandVehicle bc = m_Driver.GetCommand_Vehicle();
        if (bc && !bc.IsGettingIn()) {
            int bseat = bc.GetVehicleSeat();
            bool bhd = false; string bds = "";
            ExpansionFSMHelper.DoorAnimationSource(m_Bus, bseat, bhd, bds);
            if (bhd && bds != "") m_Bus.SetAnimationPhase(bds, 1.0);   // abre su puerta
            bc.GetOutVehicle();
            m_Driver.SetAllowDamage(true);                   // Boris ahora mortal
        }
    }
}
```

**Completion / despawn (`OnQuestComplete`):**
```c
void OnQuestComplete(ExpansionQuest quest) {
    if (!quest) return;
    ExpansionQuestConfig qc = quest.GetQuestConfig();
    if (!qc || qc.GetID() != m_QuestCheckID) return;          // solo el convoy gestionado
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckQuestBots);
    m_QuestCheckID = -1;
    m_QuestConvoyActive = false; m_QuestFleeing = false; m_QuestInitialBots = 0;
    CleanupEntities();    // borra vehículo + Boris, remueve Tick, SIN auto-respawn
}
```

**Implementation notes:**
- **Two damage triggers** (the chassis via `EEHitBy` + passenger health via the `CheckQuestBots` poll); either one arms the ambush only once (`m_AmbushTriggered`).
- `EEHitBy` does not know its runner: it resolves it with `GetRunnerForCar(this)` (the **multiton** pattern — the car belongs to a `BZBusService`).
- Boris: `BanditLoadout` on boarding; on the dismount his group switches to the **Mercenaries** faction (hostile to the player, no friendly fire with the convoy) and only then `SetAllowDamage(true)`.
- Sequence: damage → `NotifyConvoyDamaged` (freeze) → `AmbushWaitStop` (waits for the stop) → `AmbushDismount` (deployment). Boarding was instant (`BoardAmbushBots`), unlike the pacified walk-in of the `flee_on_kill` scene (C.5).
- `OnQuestComplete` only acts on `m_QuestCheckID` (the managed convoy), removes the poll and despawns with no auto-respawn — this avoids ghost cars.

### C.6 `BZInverseModel.c` — PID + inverse + gear

```c
class BZInverseModel {
    float m_Mass, m_WheelRadius, m_DragCoef, m_FrontalArea, m_RPMIdle, m_RPMRedline, m_FinalDrive;
    ref array<float> m_TorqueCurve;   // [RPM,Nm,...]    ref array<float> m_GearRatios;   // forward
    ref array<float> m_PressureBySpeed;  float m_DrivenAxleWeightRatio;
    float m_PIDIntegral, m_PIDPrevError, m_PIDKp = 0.4, m_PIDKi = 0.05, m_PIDKd = 0.0;
    const float AIR_DENSITY = 1.225, G = 9.81, ROLLING_COEF = 0.015;

    void LoadFromConfig(string vehicleClass, Car bus) { /* lee CfgVehicles + runtime (EngineGetRPM, dBodyGetMass) */ }

    float ComputeDesiredAccel(float targetKmh, float curKmh, float dt) {     // Capa 3: PID velocidad
        float err = targetKmh - curKmh;
        m_PIDIntegral = Math.Clamp(m_PIDIntegral + err*dt, -30, 30);          // anti-windup ±30
        float dErr = (err - m_PIDPrevError)/dt; m_PIDPrevError = err;
        return (m_PIDKp*err + m_PIDKi*m_PIDIntegral + m_PIDKd*dErr) / 3.6;    // km/h/s -> m/s²
    }
    void ComputeInputs(float desiredAccel, int gear, float kmh, float slope, float surfFric, float surfRoll,
                       out float outThrottle, out float outBrake, out string note) {        // Capa 4
        float v = kmh/3.6;
        float natural = 0.5*m_DragCoef*m_FrontalArea*AIR_DENSITY*v*v + ROLLING_COEF*m_Mass*G*surfRoll + m_Mass*G*slope;
        float needF = desiredAccel*m_Mass + natural;
        if (needF > 0) { /* RPM->torque, cap por tracción (m*G*fric*axleRatio), throttle = torque/maxTorque */ outBrake = 0; }
        else           { /* pressureBySpeed -> maxBrakeF, cap por tracción, brake = decel/maxBrakeF */ outThrottle = 0; }
    }
    int SelectGear(int gear, float kmh, float targetKmh, float desiredAccel) {                // Capa 5
        if (GetGame().GetTickTime() - m_LastShiftTime < SHIFT_LOCK_S) return gear;  // hysteresis 2s
        if (kmh < 5) return 2;                                                       // FIRST (DayZ gear=idx+2)
        // gear más ALTO con RPM en banda [m_RPMIdle*1.3, m_RPMRedline*0.85]; downshift agresivo si desiredAccel>2.5
    }
}
```

### C.7 `BZPathLogService.c` — recording (50 Hz) + fingerprint

```c
static const float SAMPLE_INTERVAL_MS = 20;   // 0.02s = 50 samples/s   (¡50 Hz, no 10!)

private void WriteSample(bool isStop) {
    Car car = Car.Cast(GetGame().GetPlayer().GetParent());
    // lee: GetPosition, GetDirection, GetSpeedometerAbsolute, GetGear, GetBrake/Throttle/Steering,
    //      EngineGetRPM/Redline, GetVelocity, GetClutch, GetHandbrake,
    //      + horn (GetCarHornState) y lights (LightIsOn) vía CarScript.Cast(parent)   // Fase 2 replay
    // CSV (19+ cols): time_s,x,y,z,heading_deg,speed_kmh,is_stop,gear,throttle,brake,steering,
    //                 rpm,redline_rpm,mode,vx,vy,vz,clutch,handbrake,...,horn,lights
}
private void WriteVehicleHeader() {   // sidecar: path_<ts>.csv -> header_<ts>.txt
    FPrint(h, "vehicleClass=" + car.GetType());      FPrint(h, "mass=" + dBodyGetMass(car));
    FPrint(h, "engineRPMIdle/Max/Redline=" + ...);   FPrint(h, "gearsCount=" + car.GetGearsCount());
    // attachments: iterar GetInventory().GetAttachmentFromIndex(i).GetType() -> "attachments=a,b,c"
    FPrint(h, "maxSteeringAngle=" + GetGame().ConfigGetFloat("CfgVehicles "+car.GetType()+" SimulationModule Steering maxSteeringAngle"));
    // wheelbase: proyectar WheelGetContactPosition(i) sobre GetDirection() -> max-min
    FPrint(h, "wheelbase=" + wheelbase);
}
```

### C.8 `BZBusRPC.c` — RPC enum (range 32410+)

```c
enum BZBusRPC {
    INVALID = 32410, REQUEST_STATUS, RECEIVE_STATUS, REQUEST_RESPAWN, REQUEST_RESPAWN_TEST,
    REQUEST_AI_LOG_TOGGLE, REQUEST_SYSID_STEP, REQUEST_SYSID_CURVE, REQUEST_PAUSE_TOGGLE,
    REQUEST_RAMP_TOGGLE, REQUEST_AI_MARK, REQUEST_STOP_BUS, BORIS_CHAT_SEND, BORIS_CHAT_RECEIVE,
    REQUEST_RESPAWN_SLOT, REQUEST_PANEL_SETTINGS, RECEIVE_PANEL_SETTINGS, REQUEST_PANEL_STATUS,
    RECEIVE_PANEL_STATUS, REQUEST_ROUTE_LIST, RECEIVE_ROUTE_LIST, REQUEST_LOAD_ROUTE,
    REQUEST_TELEPORT_RUNNER, REQUEST_RUNNERS, RECEIVE_RUNNERS, REQUEST_RUNNER_CTL,
    REQUEST_STOP_ALL, RECEIVE_TOAST
}
```
*(Internal reference: the enum includes RPCs for **advanced/retired tooling** — `REQUEST_AI_LOG_TOGGLE`, `REQUEST_SYSID_STEP`/`REQUEST_SYSID_CURVE`, `REQUEST_RESPAWN_SLOT` belong to debug/SysID features that are no longer in the published flow. `RECEIVE_PANEL_SETTINGS` NEVER sends the admin list — only the key + the `esAdmin` of whoever asks. `BORIS_CHAT_*` = the LLM-driven NPC experiment.)*

---

*Pending for this pack: figures. **Correction to propagate:** the PathLogger records at **50 Hz** (not 10) — update the manual. Keep it in sync with the code as it evolves.*


---

## Appendix D — Quick reference (keybindings, defaults, ai_run, JSON, guides)

Reference material so you can work without grepping the code. Source: direct reading of the source code on 2026-06-22 — **the code is the truth; if they diverge, the code wins.**

> **Index of AI-actionable guides:** D.5 (new verb) · D.6 (new vehicle) · D.7 (map an area — not available in 1.2) · D.8 (vehicle into a Quest) · **D.10 (diagnose a run / `ai_run`)**. For the raw `ai_run` format see **D.3**; for the calibration methodology (ILC loop, symptom→fix heuristics) see **§14**.

### D.1 — Keybindings (Controls-menu input system)

Migrated to the **native DayZ input system** (NO longer `OnKeyPress`). The actions are declared in `data/inputs.xml` (`<modded_inputs>` → `<actions>` with name+loc EN · `<sorting name="BZ_AutoDrive" loc="BZ AutoDrive">` = the tab in `Options → Controls` · `<preset>` with the defaults), referenced in `config.cpp` (`inputs = "BZ_AutoDrive/data/inputs.xml";`). `build_include.lst` includes `*.xml` (otherwise AddonBuilder silently excludes it). Rebindable from the menu; admin-only.

**Detection:** polled in the client's `OnUpdate` (`BZBusMissionGameplay.c`) with `GetUApi().GetInputByName("UABZ...").LocalPress()`; client-side → server via RPC (validated server-side by `IsControlPanelAdmin`). **Critical guard** (otherwise the actions fire while you rebind a key): `if (GetGame().GetUIManager().GetMenu() || GetGame().GetUIManager().IsDialogVisible()) return;` before the poll — NOT `HasGameFocus()` (= OS window focus, still `true` with a menu open).

**Defaults design (2026 redesign):** the "BZ AutoDrive" Controls menu was reduced to **3 inputs, all 3 bound** — the whole core loop. The ~16 old debug/tuning/mode controls were removed (Parking/Reverse/Maneuver Mode, AI Logger, SysID, Spawn Slots, Pause, Mark Gear/Max…), and so was the old **interchange key** (`UABZMarkLeg`/NUMPAD 3, removed 2026-08-11): maneuvers come from the **auto-detected interchange** (forward↔reverse gear change), reverse is **auto-detected from the gear**, and `ai_run`/`boris_native` are armed from the **player's checkboxes**. **Only the admin sees** the controls (`IsControlPanelAdmin` gate). Labels are LITERAL English (the inputs menu does NOT resolve the stringtable).

| Action (EN label) | `UAName` | Default | Notes |
|---|---|---|---|
| Open Control Panel | `UABZAutoDrivePanel` | `kHome` | opens the player / panel |
| Record (start/stop) | `UABZRecord` | `kNumpad5` | toggles the PathLogger **+ FrameRecorder** |
| Mark Event / Stop | `UABZMarkStop` | `kNumpad4` | marks a stop / event node (marks **both** recorders) |

> **Interchange (`legBreak`), no key:** the leg break at a direction change is **auto-derived** from the forward↔reverse gear change (always at ~0 km/h) in `frame_to_route.py`. A 0 km/h **without** a direction change = a pause, not an interchange.

> **Key names** (preset): `kHome`, `kNumpad0`–`kNumpad9` **confirmed**. Trick to discover names: read the user's preset after binding by hand (`Documents\DayZ\<profile>.dayz_preset_User.xml`).
> `F` (public) and `ESC` stay the same (bus-stop UI / close). The old NUMPAD . double-bind conflict was **resolved** by reducing to 3 keys.

### D.2 — `BZBusRouteConfig` defaults (exact values)

Source: `scripts\4_World\BZBusConfig.c`. Convention: `-1` (and sometimes `0`) = "use the internal code constant / derive from the vehicle".

**Driving control (ADVANCED — the Wizard produces a single control; these flags are for a modder who wants a different behavior, §5)**
| Field | Default | What it does |
|---|---|---|
| `FollowPath` | `false` | `true` = speed from **curvature** (pure geometry, ignores the recorded speed); `false` (default) = uses your recorded speed |
| `FollowPathUseReference` | `false` | uses the recorded speed **capped by the curve** (reference-assisted: recording as a limit + vehicle physics) |
| `UseInverseModel` | `false` | PID + inverse model for throttle/brake instead of replay |
| `InverseModelKp / Ki / Kd` | `-1 / -1 / -1` | Speed PID; defaults 0.4 / 0.05 / 0 |

**Lateral / Stanley**
| Field | Default | What it does |
|---|---|---|
| `SteeringScale` | `-1` | AUTO: derived from the wheelbase (`clamp(wb/5.5, 0.4, 1.0)`) |
| `CruiseLateralDeadband` | `0.0` | Half-width of the lane; inside it, no correction |
| `CruiseLateralKGain` | `1.0` | Multiplies the offset before the Stanley atan |
| `CruiseLateralDamp` | `0.0` | D-gain on the offset rate (0.3 kills zigzag) |
| `CruiseLateralCenterOffset` | `0.0` | Center bias (+ right, - left) |
| `CurvatureSteerBoost` | `0` | Amplifies steer in curves: `steer × (1 + boost × bendFrac)` |
| `CruiseFFWeight` | `-1` | Feedforward weight in cruise; default 0.25 |

**Speed / cruise**
| Field | Default | What it does |
|---|---|---|
| `AverageSpeedMS` | `11.0` | ~40 km/h, for the ETA |
| `FollowPathMaxKmh` | `50.0` | Cap on straights (when `FollowPath=true`) |
| `FollowPathLatAccel` | `4.0` | Max lateral accel (m/s²) for curvature-based speed |
| `TargetSpeedSmoothWindow` | `0` | Smooths targetSpeed (5=moderate, 10=aggressive) |
| `CurveThrottleEnabled` | `true` | Anticipatory throttle cut from curvature |
| `CurveThrottleStartDeg / FullDeg` | `35.0 / 80.0` | Accumulated bend where the cut starts / reaches its maximum |
| `CurveThrottleMinScale` | `0.35` | Throttle factor in a tight curve |
| `CurveThrottleLookaheadM` | `14.0` | Scan distance for the cut |
| `FollowPathSpeedSmooth` | `8` | Smooths the speed profile (0=off) |
| `FollowPathCurveSpan` | `5` | Spacing (wps) used to measure curvature |

**Gear / brake**
| Field | Default | What it does |
|---|---|---|
| `MaxGear` | `6` | Highest gear the AT shifts to (FIRST=2, SIXTH=7) |
| `GearStrategy` | `"auto_box"` | "auto_box" or "follow_recording" |
| `AccelShiftThreshold` | `999.0` | Anti-catapult (km/h/s); 999=off (bus), ~15 for a 4x4 |
| `InverseModelLowRpmMin` | `false` | false = rpmMin×1.3 (conservative); true = ×1.0 |
| `EndFreezeDisabled` | `0` | 1 = does not brake at the end |

**Slope**
| Field | Default | What it does |
|---|---|---|
| `SlopeCompensationEnabled` | `true` | Compensates throttle for the pitch of the path |
| `SlopeLookaheadWps` | `5` | Wps ahead for the lookahead |
| `SlopeGain / SlopeLateralGain` | `1.0 / 1.0` | Compensation gain / lateral bias from pitch |

**AutoRecovery**
| Field | Default | What it does |
|---|---|---|
| `AutoRecoveryEnabled` | `false` | Teleport when Boris gets stuck |
| `AutoRecoveryStuckTimeS` | `10.0` | Seconds stuck before it triggers |
| `AutoRecoveryAdvanceWps` | `5` | How many wps ahead it teleports |
| `AutoRecoveryCooldownS` | `8.0` | Minimum time between teleports |
| `AutoRecoveryMaxPerMission` | `0` | 0=unlimited; X=the mission fails if exceeded |

**Corridor / hybrid**
| Field | Default | What it does |
|---|---|---|
| `CruiseHybridSteerThreshold` | `-1` | -1=off; 0.7=override recorded steering jerks >70% |
| `CruiseHybridThrottleThreshold` | `-1` | -1=off; 0.5=override recorded throttle >=50% |

**Parking**
| Field | Default | What it does |
|---|---|---|
| `ParkingStanleyK` | `-1` | Default = internal `STANLEY_K_PARKING` |
| `ParkingFFWeight` | `-1` | Default 0.6 |
| `ModeEntrySnapEnabled` | `false` | (2026-07-03: it was `true`) Alignment snap/teleport when entering a mode; off — the closed-loop control (parking direct-replay + reverse rear-steer, heading <1°) positions the vehicle by itself. Can be re-enabled per `_hdr` |
| `ModeEntrySnapMaxDist` | `0.5` | Max distance (m) for the snap |
| `AntiRollbackEnabled` | `true` | Handbrake on a slope so the vehicle does not roll back |
| `AntiRollbackPitchThreshold` | `0.05` | Pitch (rad, ~2.86°) that activates it |

**Reverse** (all `-1`/`0` = use the internal default)
| Field | Internal default | What it does |
|---|---|---|
| `ReverseStanleyK` | `STANLEY_K_REVERSE` | Stanley gain in reverse |
| `Wheelbase` | from the fingerprint | Wheelbase (rear-steer feedforward) |
| `ReverseFFSign` | `-1` (flip) | Sign of the feedforward (inverted rear-steer) |
| `ReverseFFMaxSteerRad` | `0.6` | Feedforward normalization |
| `ReverseFFWeight` | `=ParkingFFWeight` | Feedforward weight |
| `ReverseSteerGateOffset` | `0.5` | Threshold (m) of the "discrete input" gate |
| `ReverseSteerThrottleFloor` | `0.35` | Floor of the steer-then-throttle |
| `ReverseSteerMax` | `1.0` | Clamp on \|steering\| |
| `ReverseRecordedSteerThreshold` | `0.2` | Threshold for following the recorded steering |
| `ReverseTargetSpeedCap` | `25` | Speed cap (km/h) |
| `ReverseStanleyFineMax` | `0.15` | Cap on the fine correction |
| `ReverseHeadingDeadbandDeg` | `4` | Heading deadband (degrees) |

**Smoothing / maneuver (legacy)**
| Field | Default | What it does |
|---|---|---|
| `PathSmoothWindow` | `5` | Moving average of positions (0=off; **use 0 on tight curves** — it flattens 90° turns) |
| `DirectReplayFromWaypoint` | `-1` | Wp from which the control is bypassed and a direct replay is done (legacy) |

### D.3 — Format of `ai_run_*.csv`

Server-side log of Boris's run (opt-in with the **`ai_run` checkbox in the player**, no longer a key). One row every ~0.5 s (~2 Hz). Written to `$profile:BZ_AutoDrive_PathLogger\ai_run_<ts>.csv`. **27 columns** (with a header), in this order:

```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```

Key for analysis: `lateral_dev_m` (signed lateral deviation vs the route — the % within ±2 m and the median come from here), `steering` (saturations / sign changes = zigzag), `mode` (cruise/parking/reverse/...), `is_marker` (events marked with NUMPAD 4). Do not confuse it with the PathLogger's `path_*.csv` (the human recording, 50 Hz, different columns).

### D.4 — Structure of `BZBusRoute.json`

The framework prefers the pair `_hdr.json` (config, no waypoints) + `_wp.csv` (the waypoints, fast-load via FGets) over the monolithic JSON. Condensed example (real, from `BZBusRoute_hdr.json`):

```json
{
  "RespawnDelay": 300,
  "AverageSpeedMS": 11,
  "VehicleClass": "UAZ_452",
  "DriverClass": "eAI_SurvivorM_Boris",
  "Wheelbase": 2.53,
  "FollowPath": false,
  "FollowPathUseReference": false,
  "Fingerprint": {
    "VehicleClass": "UAZ_452", "MaxSteeringAngle": 33,
    "Wheelbase": 2.53, "RminM": 3.9, "Mass": 2859.98, "GearsCount": 6
  },
  "Attachments": [
    "CarBattery", "CarRadiator", "SparkPlug",
    "UAZ_452_Wheel", "UAZ_452_Wheel", "UAZ_452_Wheel", "UAZ_452_Wheel",
    "UAZ_452_driverdoor", "UAZ_452_codriverdoor"
  ],
  "Events": [],
  "Waypoints": [
    { "pos": [13058.4, 5.58, 7612.17], "isStop": false, "targetSpeed": 12.47,
      "targetGear": 2, "targetThrottle": 1, "targetBrake": 0, "targetSteering": 0,
      "mode": "normal", "hasInputData": true, "targetLights": 0, "targetHorn": 0 },
    { "pos": [13058.4, 5.58, 7612.25], "...": "...", "mode": "normal" }
    // ... (N waypoints in total)
  ]
}
```

Notes: `targetLights` (0/1) and `targetHorn` (0=OFF/1=SHORT/2=LONG, `ECarHornState`) = recorded lights/horn, replayed per wp (spatial replay; see §5). In the `_wp.csv` they are col 16 (`targetLights`) and col 17 (`targetHorn`). `Attachments` must be REPLACED when you change the vehicle (parts from an authoritative source, NEVER guess them). Empty `Events` = no events. The final waypoints can have `"mode": "reverse"` for a parking maneuver. `Crew` is not a JSON field (the convoy bots come from the Quest, not from the JSON).

### D.5 — Guide: add a new verb to the event DSL

1. In `scripts\4_World\BZBusService.c`, find the `ExecuteAction(BZAction a)` dispatcher (the `if/else if` on `a.verb`).
2. Add a branch: `else if (a.verb == "myverb") { /* your logic */ }`. Use the existing `BZAction` fields (`strParam`, `floatParam`, etc.).
3. If you need a new field, add it to the `BZAction` class in `BZBusConfig.c`. **Enforce does not support constructor args**: create the empty object and set the fields.
4. If the verb sends something to the client (UI/sound/toast), forward it via RPC (see `BZBusRPC.c`, e.g. `RECEIVE_TOAST`).
5. Repack the PBO, then test by adding the verb to an `Events[]` in the JSON → deploy → `NUMPAD 2` reloads the route without restarting the server.

### D.6 — Guide: add a new vehicle

1. **Exact classname** from the carpack (from an authoritative source: trader package / config.cpp / spawn-inspect). Copy the mod's `.bikey` to `keys\` (A and B).
2. **Record the FULL route** as a human: `NUMPAD 5` starts/stops. The header (`header_*.txt`) captures the fingerprint (wheelbase, steering angle → R_min, gears, mass, **real parts**).
3. **Wizard** (double-click `tools\Wizard.bat`): **[1] Convert** asks you **only for the NAME** (→ `BZBusRoute_<name>.json`). `frame_to_route.py` reads the header, generalizes, auto-detects reverse and writes the four files `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv`, already **deployed** (hot-load). No modes, no linters.
4. **Deploy:** Convert itself does it (split + write to `profiles\BZ_AutoDrive\`). The mirror to B comes from `wizard_config.json` (`ServerBMirror`); see **[6] Configure paths**.
5. **Validate**: tick the **`ai_run`** checkbox in the player + spawn/test. Analyze the `ai_run` (see D.3) and tune params by symptom (see D.2 + §14).

> **Non-negotiable:** when you change `VehicleClass`, REPLACE `Attachments` with the parts of the new vehicle (`csv_to_route` keeps the previous ones → it breaks the spawn). Recurring bug.

### D.7 — Guide: map an area (build and join route graphs) — ⚠ NOT available in 1.2 (no tools in the package; the manual no longer covers it)

**What it is:** beyond replaying ONE take, you record MANY and compose them into a **directed graph** → a pathfinder (**Dijkstra**) builds A→B routes **that were never recorded whole**. Today it is **Tier 2**: offline tools assisted by an LLM (recording is in-game; building the graph and routing are done outside the game; the roadmap is to bring it into the runtime).

**How the graph is joined (automatic and incremental):**
- Each point (downsampled to ~4 m) of each trace = a **node**.
- Where two traces **cross or come within ≤6 m** = an **edge/intersection** (auto-merge; clusters ≤10 m).
- It is **directed**: each street is driven only in the **recorded direction** (= it respects the lane). To go and come back, **record both directions** (each direction = one trace).
- A new trace **is integrated without touching the previous ones** (monotonic: it only adds coverage).
- **Turns:** recorded (followed from real points) vs not recorded (an arc at the vehicle's **R_min**). The destination is extended to the perpendicular intersection.

**The tool — `bz_coverage.py`** (on the dev's PC; **analysis script, it does NOT go in the PBO nor in this version's package**): reads the `path_*.csv` files from the client's recordings folder, **filters by a bbox** (`X0,X1,Z0,Z1`) and reports **segments, km, nodes, intersections, bbox** + saves a **coverage map PNG**.
- ⚠️ The script's `HD` (recordings folder) and `bbox` carry **example values** → **adjust them to the user's** (ask for their PathLogger folder + the area to map).
- To find the bbox of a town: take the median (x,z) of each `path_*.csv` and group them in cells of ~500 m; the cell with the most traces = the dense area.

**How to guide a modder through mapping (you, the AI):** (1) ask for the **paths** + the **area** · (2) set the **bbox** of the town · (3) run it → report + map → identify **gaps** (missing streets/directions) → tell them which ones to record · (4) **iterate** (record → re-run; coverage grows monotonically) · (5) build **graph + Dijkstra** A→B → export as an **executable take**.

**Gotchas:** directed (without the opposite direction you cannot come back) · thresholds 6 m/10 m depending on the map scale · turns not recorded = R_min arc (record the turn if you want fidelity).
**Applications:** routable city · map sector · **railway tracks** (the graph *is* the tracks: no steering and no turns, only accel/brake). Reference case: Novaya Petrovka, Chernarus — 9 traces → 5.1 km, ~1200 nodes, ~102 intersections.

### D.8 — Guide: add a vehicle to a Quest (convoy)

**Division of work:** the modder configures **(1) the ROUTE** (framework side) + **(2) the QUEST** (Expansion side). The **hook is already pre-built** in the PBO — you write no code.

**FILE 1 — the route** (`<profiles>\BZ_AutoDrive\BZBusRoute*.json`). Convoy lines (e.g. ambush):
```json
{
  "VehicleClass": "x5mcompetition_orange",
  "ConvoyMode": "ambush_on_damage",
  "VehicleInvincible": false,
  "Crew": [],
  "Waypoints": [ /* the recorded route */ ]
}
```
- `ConvoyMode`: `"flee_on_kill"` (you kill 1 → they get in and flee) or `"ambush_on_damage"` (armed on board → damage → freeze + dismount + they camp).
- `VehicleInvincible: false` ← **MANDATORY for `ambush_on_damage`** (if it is indestructible it never takes damage → it never triggers).
- `Crew: []` ← empty: **the Quest places the bots**, not the route. (`Crew[]` is for bots that travel WITHOUT a quest.)
- `SpawnHoldSeconds: 600` (optional, flee): the vehicle waits, stopped, until the trigger.
The route comes from the normal flow (record → Wizard → deploy to `profiles\BZ_AutoDrive\`).

**FILE 2 — the Quest** (config of **DayZ-Expansion-Quests**, in the data of the user's quest mod, **NOT** in the framework): there you define the **live bots** (patrol/camp), the objective (kill them), the reward — with the Expansion-Quests editor/JSON. *The framework does NOT spawn armed bots with meaningful behavior; that belongs only to the Quest.*

**The hook (ALREADY in the PBO — only so you understand it):** `modded class MissionServer.Expansion_OnQuestStart` → `BZBusService.OnQuestStart(quest)` stores the quest ID (`qc.GetID()`) and **polls** `ExpansionQuestModule...QuestPatrolExists(questID, patrols)` every ~4 s (the bots are *lazy*, spawned by proximity). When the bot count **drops** (one was killed) → it triggers the scene.

**Dependency:** the framework's `config.cpp` declares `DayZExpansion_Quests_Scripts` (for publishing, it is better to split it into an optional sub-addon).

**Checklist for the modder:** (1) record+deploy the route with `ConvoyMode` set · (2) in Expansion-Quests, create the quest with its bots near the **route start** · (3) test: you accept → the bots appear → you kill 1 (flee) or hit the vehicle (ambush) → it triggers.
**Gotchas:** ambush without `VehicleInvincible:false` does not trigger · bots go in the Quest (not in `Crew[]`) · complete validated examples in the **manual §11.3/§11.4** (convoy) and §11.10/§11.11 (Travel/Escort, which auto-select a route per quest).

### D.9 — External references + searches (wiki + cited material)

*To send the modder to the authoritative source, or to search further. The **local code is the truth**; this is external context.*

**DayZ / Enforce (game modding):** BIS Community Wiki — base `https://community.bistudio.com/wiki/` · DayZ modding: `https://community.bistudio.com/wiki/DayZ:Modding_Basics`. Search for: `DayZ Enforce Script`, `DayZ CarScript SetThrottle`, `DayZ Transport WheelGetSurface`, `DayZ modded class MissionServer`, `DayZ EmoteManager`, `DayZ ConfigGetFloat CfgVehicles SimulationModule`.

**DayZ-Expansion (eAI + Quests + vehicles):** public source `https://github.com/salutesh/DayZ-Expansion-Scripts` (look at `ai_scripts`, `quests_scripts`, `vehicles_scripts`). Search for: `DayZ Expansion AI eAIBase`, `Expansion Quests ExpansionQuestObjective`, `Expansion StartCommand_Vehicle`, `Expansion_OnHandleController`.

**Earlier engine / Related Work (verified deep research):**
- ARMA 3 AI driving (official OPREP): `https://dev.arma3.com/post/ai-path-following-improvements`
- ARMA 3 `AICarSteeringComponent` (Biki): `https://community.bistudio.com/wiki/Arma_3:_AICarSteeringComponent` · `setDriveOnPath`: `https://community.bistudio.com/wiki/setDriveOnPath`
- Enfusion (Reforger) navmesh: `https://community.bistudio.com/wiki/Arma_Reforger:Navmesh_Tutorial`
- EA SEED, *Efficient Ground Vehicle Path Following in Game AI* (CoG 2023): `https://arxiv.org/abs/2307.03379`
- Codevilla et al., *Limitations of Behavior Cloning* (ICCV 2019) · *One-Shot Imitation Learning* (NeurIPS 2017).

**Tooling (dev's PC):** ImageToPAA (PNG→PAA), AddonBuilder (build PBO). Search for: `DayZ Tools ImageToPAA`, `DayZ AddonBuilder pbo`.

### D.10 — Diagnose a run (ai_run) — AI-actionable

*So that an AI (you: Claude/GPT/Gemini) can **diagnose a Boris run** by reading an `ai_run` file the admin pastes to you. This is actionable: read the precomputed columns, map the signatures to causes, and deliver the diagnosis **in Spanish for the admin**, in the format below. It complements §14 ("Analyzing an ai_run") and D.3 (file format).*

**What the ai_run is.** It is the **telemetry of a Boris run** — the "black box" of what the NPC actually did. It is **opt-in** (the admin arms it by ticking the **`ai_run` checkbox in the player**, BEFORE pressing play — no longer a key; without it, NOTHING is written). It lives in `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv` (on the server that ran it). **Do NOT confuse it with the human take** (`path_*.csv`, the recording/demonstration, 50 Hz, different columns) — they are different files with opposite purposes (measurement vs reference).

**REAL schema (27 columns, ~2 Hz)** — this is the CURRENT format; LEAD with it. Header as is (with a `header`, in this order):
```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```
**Key: the file ALREADY carries the diagnosis precomputed by the framework — READ those columns, do NOT recompute them.** The ai_run **does NOT need the route separately**: the lateral deviation and the targets are already inside it.

**Columns you READ directly (what each one does):**
- `lateral_dev_m` — **signed lateral deviation per sample** vs the route (already computed). Where it goes **wide**. Use the MEDIAN and split straight/curve; the signed average misleads because of outliers/saturations (§14). **High in a curve** = enters fast / understeer.
- `target_speed`, `target_throttle`, `target_brake` — the controller's **targets**. **Deficit = `speed_kmh − target_speed`** directly (sustained negative = Boris does not reach the target → lugging/slope).
- `corridor_offset`, `corridor_valid` — tracking of the **corridor/wall**. `corridor_valid == 0` → **off-path** (Boris left the lane band on that stretch).
- `rpm`, `redline_rpm` — engine. **`rpm` close to `redline_rpm` at low `speed_kmh`** = **lugging** (low gear at full throttle without moving forward) or a badly chosen gear.
- `mode` = **active** controller (cruise/maniobra/parking/reverse); `wp_mode` = the waypoint's **declared** mode. If they differ, or if `mode=cruise` on a slow, tight stretch, there is a mode↔geometry mismatch.
- `is_marker` — samples marked with **NUMPAD 4** = **events/stops** the admin wanted to flag; anchor the analysis there.
- `wp_idx` — the waypoint Boris is chasing; `next_stop_idx`/`dist_to_next_stop` = next stop.
- `i_speed`, `i_throttle`, `i_brake` — internal terms of the controller (PID/inverse); useful to understand why it asked for that throttle/brake, not for the first-level diagnosis.

**Computations you apply by READING those columns:**
- **Lateral deviation:** median of `lateral_dev_m` (split straight/curve). Peaks in curves = it goes wide.
- **Speed deficit:** `speed_kmh − target_speed`; find where it becomes negative and stays there.
- **Off-path:** stretches with `corridor_valid == 0`.
- **Lugging:** `rpm`≈`redline_rpm` with low `speed_kmh` (+ `throttle`>0).
- **Stuck/jammed:** `wp_idx` **constant** for many samples + `speed_kmh ~0`. Look at `mode`/`gear`/`rpm` there.
- **Mode vs geometry:** `mode` vs `wp_mode` and vs the speed of the stretch.
- **Completion:** did `wp_idx` reach the **last** wp? If not, how far did it get.

**Fallback (OLD 14-column files, or to verify).** Old takes only have `time_s,x,y,z,heading_deg,speed_kmh,gear,throttle,brake,steering,mode,dist_to_next_stop,next_stop_idx,wp_idx` — **without** `lateral_dev_m`/`target_*`/`corridor_*`/`rpm`. In that case you DO recompute by hand (and it is a good idea to ask for the route `BZBusRoute_<name>_wp.csv`/JSON for the path and the target speeds):
- `dt` = diff of `time_s`; v in m/s = `speed_kmh/3.6`.
- **AR teleport / jump:** real distance between 2 samples = `sqrt(dx²+dz²)`. If it is **>> `(speed_kmh/3.6)·dt`** (e.g. >3× and several meters at once) → **AutoRecovery teleported** Boris (he was stuck or off-path); the jump marks WHERE he got stuck (look at the previous `wp_idx`).
- **Lateral deviation** = distance from `(x,z)` to the route path near that `wp_idx` (what `lateral_dev_m` already gives in the new format).
- **Deficit** = `speed_kmh` vs the wp's `targetSpeed` in the route.

**Table of failure signatures → cause → fix:**
| Signature in the ai_run (column) | Probable cause | Suggested fix |
|---|---|---|
| `wp_idx` stays fixed + sustained `speed_kmh`~0 | impossible curve / high gear / off-path | re-record wider and slower (with THAT vehicle); check the gear |
| position jump (sqrt(dx²+dz²) >> v·dt) | AutoRecovery rescued Boris (he got stuck there) | fix the **cause** of the jam at that wp |
| sustained `speed_kmh` << `target_speed` | lugging / speed deficit | `GearStrategy=follow_recording`; check the slope |
| high `lateral_dev_m` in a curve | overspeed / understeer, goes wide | enter slower; wider curve; cap from curvature |
| `corridor_valid==0` on a stretch | Boris off-path (out of the lane) | check the curve entry; AutoRecovery; re-record the stretch |
| `rpm`≈`redline_rpm` + low `speed_kmh` + `throttle`>0 | lugging (low gear at full throttle) | `GearStrategy=auto_box` or `follow_recording` |
| `mode=cruise` on a slow, tight stretch | the cut falls in a fast zone | re-record the stretch slower; if it is a direction change, stop completely on the straight and change gear direction there (the interchange is auto-detected from the gear) |
| `wp_idx` does not reach the end | it did not complete | diagnose the last problem above |

**How to deliver the diagnosis (you → the admin, in their language):**
1. **Summary:** did it complete, yes/no? + WHERE the problems are (wp + type).
2. **For each problem:** the observed **signature** (cite the column: `lateral_dev_m`, `corridor_valid`, `speed_kmh` vs `target_speed`, `rpm`/`redline_rpm`…) + the **cause** + the **concrete fix** (cite the D.2 / §5 param when it applies).
3. **Remember** that the fix is usually to **re-record that stretch** or **adjust a route parameter** (not to feed the ai_run back in).

> **PRINCIPLE (do not violate):** the ai_run is a **MEASUREMENT** that is compared AGAINST the human take to calibrate (functional feedback); **NEVER a new reference**. **Never** suggest turning an `ai_run` into a route: it clones Boris's errors → each iteration gets worse ("model collapse"). The Wizard filters it out on purpose. The human take is the sacred reference; the ai_run is the thermometer.

### D.11 — Guide: import a take from BrigadaZ Transport v1

*Convert a route from the old **BrigadaZ Transport v1.0** mod (a monolithic JSON with `Waypoints`) to the AutoDrive format, without re-recording. Wizard → **[2] Import v1 take**.*

1. **Pick the `.json`** of the v1 take.
2. **Vehicle identity (`--fingerprint`):** the v1 take does NOT carry `Wheelbase`/`Fingerprint`/`Attachments` (they depend on the VEHICLE, not on the route). You give it **any** `header_*.txt` of that vehicle (a **10 s** recording is enough) or a `_hdr.json` from an already calibrated take. The Wizard lists first the ones that match the declared vehicle.
3. **Obstacle profile (AR_OnWay, §5):** `[R]obust` (Slow+Escape ON — a line bus that gets around whatever blocks its way) · `[I]nterceptable` (Slow ON, Escape **OFF** — it brakes but does NOT escape → useful for interception missions) · `[N]one` (pure replica). It sets `ObstacleSlow`/`ObstacleEscape` in the `_hdr` (requires `UseInverseModel=true`, which the template includes).
4. **Output:** the trio `BZBusRoute_<name>.json`+`_hdr.json`+`_wp.csv`, already deployed — **without `_cinta.csv`**: a v1 take does not have the 40 Hz driving, so it runs **tapeless** (as a trajectory follower, §5.3). It runs `transport_v1_to_route.py` (same signature as `frame_to_route.py`).

**What migrates and what does not:** the **trace + speed + stops** migrate (the AutoDrive header/wp is a strict superset of the v1 ones). The v1 **pedals** are **discarded** (the control is rebuilt from trace+speed, not replayed). The **driving profile is per vehicle** → it comes from the `--fingerprint`, not from the v1 route. `targetHeading` is derived from geometry (`atan2(dx,dz)`, median error ~0.35°). See manual §6.2.

---

## Appendix E — Guide to porting the framework to another game: 7 Days to Die (Unity / C#)

> **What this appendix is for.** Some people want to take the idea of BZ AutoDrive to their own game. This appendix is written so that **a coding AI (Codex, ChatGPT, Claude or another one)** can guide that port step by step, using the concrete case of **7 Days to Die** (Unity, C# mods with Harmony). Everything it says about 7DTD is **verified**: in August 2026 the author built a proof of concept that drove a vehicle on its own on a 7DTD dedicated server. The full code of that test is in E.3.
>
> **If you are the AI that will do the port:** read this whole appendix before you write a single line. Then read §5 (the 1.2 brain) and §7.0 (what the conversion produces). Do not try to port everything at once: follow the milestones in E.4 **in order**, and do not move on to the next one until the previous one meets its "done" criterion, **measured in the game with numbers in the log**. The DayZ framework was built this way: one change at a time, each one with its measurement. It is the only way that worked.

### E.0 — How to work (instructions for the AI)

1. **The core idea, which you must not lose.** The framework is NOT a pilot that "decides how to drive". It is a **player of measured human driving**: a human drives the route once, the framework records what they did at each point along the way (40 times per second, **indexed by distance traveled, not by time**) and the NPC plays it back, correcting only what is needed to stay on the line. *"Fly where you flew, brake where you braked."* The vehicle physics does the rest. If at some point you find yourself writing a trajectory planner or a controller that "invents" speeds, you have gone off track.
2. **Three separate pieces, with file contracts between them:**
   - **the recorder** (inside the game): records the human's take into a CSV;
   - **the converter** (outside the game, Python): converts the take into the route files. **It can be reused as is** (`tools/frame_to_route.py`, `tools/envelope_emit.py`, `tools/frame_merge.py`): it does not depend on DayZ if the input CSV has the same columns;
   - **the runner** (inside the game): loads the route files and drives.
   Porting means: writing the recorder and the runner in C# for 7DTD, and keeping to the formats in E.5.
3. **Measure before you change.** Each milestone has a numeric criterion. Ask the user to run the test and send you the log. If the number is not right, **do not patch blindly**: explain what you think is happening, propose a measurement that confirms it, and only then change code. The rule that saved the most time in DayZ: *"a hypothesis is falsified with a number, not with an impression"*.
4. **One change at a time.** If you change two things and it gets better, you do not know which one it was. If it gets worse, you do not know either.
5. **Log everything with your own prefix** (`[BZAD]` in the example) and with numbers: position, speed, applied inputs, state. One telemetry line per second, plus one line for each event (start, stop, jam, recovery). Without that, no diagnosis is possible.
6. **The take is a measurement: it is not edited.** If a take has something odd (a sudden jerk of the steering wheel, a false braking), it is re-recorded. Using an edit to cover up what is really a controller problem is the most expensive trap.
7. **The user tests; you cannot.** Every time you finish something, tell them exactly what to run, what they should see in the log and which number means success.

### E.1 — What gets ported and what does not

| Piece | In DayZ (this framework) | In 7 Days to Die | Is it ported? |
|---|---|---|---|
| **Idea: play back recorded driving by distance** | the tape (`BZCintaRunner`) | the same | ✅ it is the heart: port the logic (E.6) |
| **File format** | `frame_*.csv`, `_wp.csv`, `_cinta.csv`, `_hdr.json`, `.json` | the same ones | ✅ as is (E.5) |
| **Converter** | Python (`tools/frame_to_route.py` + `envelope_emit.py`) | the same one | ✅ as is; ignore the DayZ fields (E.5.5) |
| **Control surface** | `modded class CarScript` → `OnInput()`: throttle, brake, steering, gear | `EntityVehicle.movementInput` (`moveForward` −1..1, `moveStrafe` −1..1, `jump` = brake, `running` = full torque), written in a Harmony `Prefix` on `EntityVehicle.PhysicsFixedUpdate` | ✅ different mechanism, same idea (E.3) |
| **Driver** | a seated eAI (Expansion) bot | an `EntityAlive` seated in seat 0 (`npcTraderJoel` works). **It is required**: without it the engine does not start | ✅ (E.2, obstacle 5) |
| **Physics authority** | the server | the server, **as long as there is no human in seat 0** | ✅ (E.2) |
| **Steering: command → wheel angle** | measured (`stx2`, "plant gain" ~85) | **it is read**: `wheelDir = moveStrafe × SteerAngleMax` (32 on the Truck4x4) | ✅ easier |
| **Wheelbase** | configured or measured | **it is measured** from the `WheelCollider`s (2.85 m on the Truck4x4) | ✅ easier |
| **Gears** | gearbox with gears, reverse = gear 0 | there is no gearbox: a negative `moveForward` is reverse | ⚠ simplify: ignore `gear` except to know the direction |
| **Envelope (what is measured for each vehicle)** | learned from the takes (curvature, braking, creep, throttle floor) | a good part of it **is read** from `Vehicle`: `MotorTorqueForward/Backward`, `VelocityMaxForward/Backward`, `SteerAngleMax`, `SteerRate`, `BrakeTorque` | ⚠ optional at first; useful later to brake well |
| **World loaded under the vehicle** | always | **no**: you must keep a chunk observer **with a mesh** around the vehicle | ⚠ mandatory (E.2, obstacle 3) |
| **Obstacle probing** | `GetObjectsAtPosition3D` | Unity's `Physics.OverlapBox` / `Physics.Raycast` | ✅ |
| **Recoveries (AutoRecovery)** | ladder: move away, relocate, safe zones | the same | ✅ (E.6.4) |
| **Road network** | not needed (the take IS the route) | not needed. If some day you want generated routes: `splat3_processed.png` (E.7) | optional |
| **Events, missions, panel, RPC, lights, eAI** | Expansion / Quests / own UI | — | ❌ do not port: they belong to the consumer or to DayZ |
| **Enforce, PBO and signature gotchas** | §12 | — | ❌ they do not apply |

### E.2 — The 7DTD anatomy that matters (verified by decompiling `Assembly-CSharp.dll`)

**Modding.** 7DTD loads DLL mods with **official Harmony** (`Mods/0_TFP_Harmony`, HarmonyX). The game assembly comes **publicized**: private members are public (marked `[PublicizedFrom(EAccessModifier.Private)]`), so you do not need reflection. **DLL mods require EAC (anticheat) to be off** (`ModInfo`: `SkipWithAntiCheat`). The entry point is a class that implements `IModApi` (`InitMod(Mod)`).

**The control surface.** `EntityVehicle.movementInput` (of type `MovementInput`) has `moveForward` (−1..1), `moveStrafe` (−1..1), `jump` (brake) and `running` (turbo/full torque). The player's input comes in through a single place (`MoveByAttachedEntity`) that only copies it into those fields. On every `FixedUpdate`, `EntityVehicle.PhysicsFixedUpdate()` reads them:
```
EntityVehicle.PhysicsFixedUpdate()
  ├─ [mod Prefix]  <- write movementInput.moveForward / moveStrafe / jump / running
  ├─ wheelMotor = movementInput.moveForward
  ├─ wheelDir  <- MoveTowards(wheelDir, moveStrafe * SteerAngleMax, rate)   (with lastInputController = true)
  └─ SetWheelsForces(...)
```
Alternative without Harmony: `PhysicsInputMove()` is an empty virtual method that is called on every `FixedUpdate` (only the blimp and the helicopter override it). You can inherit from `EntityVehicle`, override it and register the subclass with `<property name="Class" value="..."/>` in `entityclasses.xml` (`EntityClass` does `Type.GetType(Properties["Class"])`).

**Authority.** The vehicle **inherits** `isEntityRemote` from whoever occupies seat 0 (`Entity.AttachToEntity`): with a player driving, the client simulates; **with no human at the wheel, the server simulates**. A server-side NPC driver is the natural setup for the game engine. ⚠ If a player gets into seat 0, the server stops simulating: the runner must check `isEntityRemote` and send nothing if it is `true`.

**NPC.** Hierarchy `EntityAlive → EntityNPC → EntitySurvivor / EntityTrader`. The seat accepts **any `EntityAlive`** (`StartAttachToEntity(vehiculo, 0)` does not check that it is a player, and it replicates through `NetPackageEntityAttach`). ⚠ `npcSurvivorTemplate` / `npcSurvivorRanged` are in `entityclasses.xml` **but they do not end up registered** in `EntityClass.list`; `npcTraderJoel` and any `zombie*` do work. For your own AI there are two systems that you can extend from XML with classes from a DLL: **EAI** (`AITask-N` → `Type.GetType("EAI" + nombre)`) and **Utility AI** (`utilityai.xml`). They are not needed to drive: the NPC **enables** (it starts the engine), it does not drive.

**Chunks: the world must be loaded WITH collision.** A chunk observer has a flag `_bBuildVisualMeshAround`: the mesh is built only with `true` (`doCopyChunksToUnity` is the only place that sets `IsCollisionMeshGenerated = true`). `Entity.bIsChunkObserver = true` creates an observer with that flag set to `false`: **that is enough for zombies (which walk on voxels) but NOT for a vehicle**, which needs `WheelCollider`s against real geometry. Measured: with `false`, `IsChunkAreaLoaded = True` but `IsChunkAreaCollidersLoaded = False`, and the vehicle falls into the void and the game engine kills it (`VehicleManager.RemoveTrackedVehicle ... Killed`). The dedicated server does build meshes, with a bigger budget than the client (`IsDedicatedServer ? 25000 : 2500` µs per frame).

**The obstacles that the proof of concept solved, in the order in which they show up** (none of them is obvious; each one costs hours if you do not know it):

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | `NullReferenceException` when writing the input | `movementInput` is `null` on the dedicated server: it is only instantiated for the local player (`EntityVehicle.AttachEntityToSelf`) | `if (veh.movementInput == null) veh.movementInput = new MovementInput();` |
| 2 | the vehicle does not move even though the input is correct | with `RBActive = false`, `PhysicsFixedUpdate` does `PhysicsResetAndSleep()` + `isKinematic = true` and returns | `veh.RBActive = true; veh.vehicleRB.isKinematic = false; veh.vehicleRB.WakeUp();` |
| 3 | the vehicle falls into the void and dies | there is no collision mesh: see "Chunks" above | your own observer with `AddChunkObserver(pos, true, 3, -1)` that is re-created when the vehicle changes chunk, and **do not activate the rigidbody until `world.IsChunkAreaCollidersLoaded(pos)` is `true`** |
| 4 | with no driver, the game engine brakes the vehicle and puts it to sleep | on purpose: `velocity.x *= 0.98f` per tick and `RBActive = false` after 3 s almost still | force `RBActive` on every tick while the runner is driving (obstacle 2) and **seat a driver** (obstacle 5) |
| 5 | the engine does not start | `IsEngineRunning` is only turned on by `VPEngine.startEngine()` via `Vehicle.Event.Start`, which is emitted by `AttachEntityToSelf` when **someone occupies seat 0**. Forcing `hasDriver = true` **does not work**: `UpdateAttachment()` does `if (hasDriver && attachedMainEntity == null) DriverRemoved();` | seat a real `EntityAlive`: `EntityFactory.CreateEntity(...)` → `world.SpawnEntityInWorld(npc)` → `npc.StartAttachToEntity(veh, 0)` |
| 6 | `wheelMotor` arrives fine but `wheelDir = 0` and `torque% = 0` | **game bug, server-side only**: `EntityVehicle.isTurnTowardsLook` is `public static` and starts as `true`; in that mode `PhysicsFixedUpdate` uses `GetAttachedPlayerLocal().vp_FPCamera`, which is `null` on a dedicated server → NRE on every tick, right after assigning `wheelMotor` | when the mod starts: `EntityVehicle.isTurnTowardsLook = false;` |
| 7 | NRE on vehicles with fewer than 4 wheels (bicycle, minibike, motorcycle) | **second server-side bug**: `if (hasDriver && wheels.Length < 4 && GetAttachedPlayerLocal().isPlayerInStorm)` | a Harmony transpiler that replaces the access to `isPlayerInStorm` with a null-safe helper (E.3, `Patches.cs`) |

Also: `movementInput.lastInputController = true` makes the steering map **directly** (`wheelDir = MoveTowards(wheelDir, moveStrafe × SteerAngleMax, rate)`), which is what a controller needs; and **`running = true`**, because without it the game engine does `wheelMotor *= 0.5`.

**Practical details of the test bench:**
- **Compiling without Visual Studio or an SDK:** Windows' native Roslyn (`csc.exe`, the **net472** variant of the NuGet package `Microsoft.Net.Compilers.Toolset`; the `bincore` variant of Roslyn 4.14 requires .NET 9). Required references that are not obvious: `LogLibrary.dll` (the `Log` class), `Mods/0_TFP_Harmony/0Harmony.dll`, `UnityEngine.VehiclesModule.dll` (for `WheelCollider`), in addition to `Assembly-CSharp.dll`, `UnityEngine.CoreModule.dll` and `UnityEngine.PhysicsModule.dll`.
- ⚠ **The dedicated server's `Assembly-CSharp.dll` is NOT the same as the client's** (the server build strips out rendering and UI; the hashes differ). For a server-side mod, **compile against the dedicated server's one**; compiling against the client's one can give a `TypeLoadException` at runtime.
- **The DLL goes in the ROOT** of `Mods/<TuMod>/` (loading is not recursive), next to the `ModInfo.xml`.
- **Dedicated server:** it is downloaded separately (Steam → Tools → *7 Days to Die Dedicated Server*, app 294420). To test without opening the game: telnet to `127.0.0.1:8081` (with an empty `TelnetPassword` it listens only on loopback).
- `VehicleManager.Update()` returns right away if `world.Players.Count == 0`: it helps to have a player connected for some tests. **But if the player gets into the vehicle, authority passes to their client.**
- `EntityClass.FromString(s)` is just `s.GetHashCode()`: **it can be negative**. Validate with `EntityClass.list.ContainsKey(hash)`, never with `hash <= 0`.
- A `vehicleTruck4x4` spawned from the console comes complete: engine, steering, 8000 hp, fuel 400/400, `MotorTorqueForward = 3500`, `VelocityMaxForward = 10`, `SteerAngleMax = 32`. It is the best vehicle to start with.
- **Map coordinates (Navezgane, verified):** `column = x_world + WorldSize/2`, `row = z_world + WorldSize/2`; height = `dtm_processed.raw` (uint16 little-endian, 6144×6144) `/ 256`. It is useful for picking a flat test stretch without opening the game.

### E.3 — The proof-of-concept code (C#, 7DTD, dedicated server)

This is the code that drove a `vehicleTruck4x4` on its own in Navezgane, on a dedicated server, on 2026-08-12. **It is the recommended base to start from:** it solves all the obstacles in E.2 and includes the pure pursuit, ported and measured (H1). What it does **not** have: the tape, the recorded stops, reverse, good speed control (its own is a proportional controller that oscillates) and real recoveries (its own is "reverse for 2 s"). That is what is left to port, with E.5 and E.6.

Structure:
```
src/ModApi.cs             IModApi entry point: applies Harmony, turns off isTurnTowardsLook
src/Patches.cs            Prefix on PhysicsFixedUpdate (injection) + null-safe transpiler + removal and damage diagnostics
src/AutoDriveManager.cs   pilot registry (server-side), chunk observers
src/AutoDrivePilot.cs     the pilot: physics guards, chunk bubble, control, telemetry
src/PurePursuit.cs        the framework's pure pursuit, ported and measured
src/Route.cs              simple polyline (circle, straight line, JSON)
src/ConsoleCmdBzDrive.cs  console command 'bzdrive' (list / spawn / driver / circle / line / route / speed / status / stop)
mod/BZ_AutoDrive/ModInfo.xml
```

#### `ModInfo.xml`
```xml
<?xml version="1.0" encoding="UTF-8" ?>
<xml>
	<Name value="BZ_AutoDrive" />
	<DisplayName value="BZ AutoDrive" />
	<Version value="0.1.0" />
	<Description value="Conduccion autonoma de vehiculos server-side. Banco de pruebas del framework de path-following." />
	<Author value="TU_NOMBRE" />
	<Website value="" />
</xml>
```

#### `ModApi.cs`
```csharp
using System;
using System.Reflection;
using HarmonyLib;

namespace BZAutoDrive
{
    /// <summary>
    /// Mod entry point. 7DTD instantiates every class that implements IModApi
    /// and calls InitMod (see Mod.InitModCode in Assembly-CSharp).
    /// </summary>
    public class ModApi : IModApi
    {
        public const string ModId = "BZ_AutoDrive";
        public const string Version = "0.1.0";

        public void InitMod(Mod _modInstance)
        {
            Log.Out("[BZAD] BZ_AutoDrive " + Version + " iniciando...");
            Log.Out("[BZAD]   IsDedicatedServer = " + GameManager.IsDedicatedServer);

            try
            {
                var harmony = new Harmony(ModId);
                harmony.PatchAll(Assembly.GetExecutingAssembly());
                Log.Out("[BZAD]   parches Harmony aplicados");
            }
            catch (Exception e)
            {
                Log.Error("[BZAD] fallo aplicando parches Harmony");
                Log.Exception(e);
                return;
            }

            // See the note in AutoDrivePilot: with this set to true, PhysicsFixedUpdate throws
            // a NullReferenceException on every tick on a dedicated server.
            if (GameManager.IsDedicatedServer && EntityVehicle.isTurnTowardsLook)
            {
                EntityVehicle.isTurnTowardsLook = false;
                Log.Out("[BZAD]   isTurnTowardsLook -> false (evita NRE server-side)");
            }

            Log.Out("[BZAD] listo. Comando de consola: bzdrive");
        }
    }
}
```

#### `Patches.cs`
```csharp
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace BZAutoDrive
{
    /// <summary>Null-safe helpers that the transpiler injects.</summary>
    public static class BZSafe
    {
        public static bool InStorm(EntityPlayerLocal _p)
        {
            return _p != null && _p.isPlayerInStorm;
        }
    }

    /// <summary>
    /// SECOND game-engine bug that is only visible server-side. In PhysicsFixedUpdate:
    ///
    ///     if (hasDriver &amp;&amp; wheels.Length &lt; 4 &amp;&amp; GetAttachedPlayerLocal().isPlayerInStorm)
    ///
    /// GetAttachedPlayerLocal() looks for an EntityPlayerLocal among the occupants, and on a
    /// dedicated server there is NEVER one -> it returns null -> NRE with any vehicle that has
    /// fewer than 4 wheels (bicycle, minibike, motorcycle: that is, almost all of them).
    ///
    /// The transpiler replaces the access to the isPlayerInStorm field with BZSafe.InStorm(), which
    /// tolerates the null. It is the smallest possible change: a single IL instruction.
    /// </summary>
    [HarmonyPatch(typeof(EntityVehicle), "PhysicsFixedUpdate")]
    public static class Patch_EntityVehicle_StormNullSafe
    {
        public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> _in)
        {
            FieldInfo campo = AccessTools.Field(typeof(EntityPlayerLocal), "isPlayerInStorm");
            MethodInfo seguro = AccessTools.Method(typeof(BZSafe), "InStorm");
            int cambios = 0;
            foreach (var ins in _in)
            {
                if (ins.opcode == OpCodes.Ldfld && (ins.operand as FieldInfo) == campo)
                {
                    cambios++;
                    yield return new CodeInstruction(OpCodes.Call, seguro);
                }
                else
                {
                    yield return ins;
                }
            }
            Log.Out("[BZAD] transpiler: " + cambios + " acceso(s) a isPlayerInStorm -> null-safe");
        }
    }
    /// <summary>
    /// Injection point of the actuator.
    ///
    /// EntityVehicle.PhysicsFixedUpdate() is where the game engine READS movementInput
    /// (wheelMotor = movementInput.moveForward; wheelDir <- movementInput.moveStrafe ...).
    /// A Prefix lets us write it just before that read, a full tick
    /// before forces are applied to the wheels.
    ///
    /// Also, reading wheelMotor/wheelDir FROM the prefix returns the values of the previous
    /// tick (the game engine sets them to zero at the start of PhysicsFixedUpdate), so
    /// the telemetry confirms that the input really reached the engine.
    ///
    /// Alternative without Harmony: inherit from EntityVehicle and override the empty
    /// virtual method PhysicsInputMove(), registering the subclass with
    /// <property name="Class" value="..."/> in entityclasses.xml.
    /// </summary>
    [HarmonyPatch(typeof(EntityVehicle), "PhysicsFixedUpdate")]
    public static class Patch_EntityVehicle_PhysicsFixedUpdate
    {
        public static void Prefix(EntityVehicle __instance)
        {
            if (__instance == null) return;
            if (!AutoDriveManager.IsServer) return;
            AutoDriveManager.OnPhysicsTick(__instance);
        }
    }

    /// <summary>Diagnostics: why the vehicle disappeared.</summary>
    [HarmonyPatch(typeof(VehicleManager), "RemoveTrackedVehicle")]
    public static class Patch_VehicleManager_RemoveTrackedVehicle
    {
        public static void Prefix(EntityVehicle _vehicle, EnumRemoveEntityReason _reason)
        {
            if (_vehicle == null) return;
            int hp = (_vehicle.vehicle != null) ? _vehicle.vehicle.GetHealth() : -1;
            Log.Warning(string.Format(
                "[BZAD] REMOVIDO id={0} motivo={1} pos=({2:0.0},{3:0.0},{4:0.0}) hp={5} " +
                "RBActive={6} ruedas={7} muerto={8} marcadoUnload={9}",
                _vehicle.entityId, _reason,
                _vehicle.position.x, _vehicle.position.y, _vehicle.position.z,
                hp, _vehicle.RBActive, _vehicle.GetWheelsOnGround(),
                _vehicle.IsDead(), _vehicle.IsMarkedForUnload()));

            // who killed it: this is the only thing that tells it without ambiguity
            string st = System.Environment.StackTrace;
            foreach (string linea in st.Split('\n'))
            {
                string t = linea.Trim();
                if (t.Length == 0) continue;
                if (t.Contains("Patch_VehicleManager") || t.Contains("Environment.get_StackTrace")) continue;
                Log.Warning("[BZAD]   <- " + t);
            }
        }
    }

    /// <summary>Diagnostics: if the vehicle is taking damage, where it comes from.</summary>
    [HarmonyPatch(typeof(EntityVehicle), "ApplyDamage")]
    public static class Patch_EntityVehicle_ApplyDamage
    {
        public static void Prefix(EntityVehicle __instance, int damage)
        {
            if (__instance == null || damage <= 0) return;
            if (!AutoDriveManager.IsTracked(__instance.entityId)) return;
            int hp = (__instance.vehicle != null) ? __instance.vehicle.GetHealth() : -1;
            Log.Warning("[BZAD] DANO id=" + __instance.entityId + " dmg=" + damage + " hp_antes=" + hp);
        }
    }
}
```

#### `AutoDriveManager.cs`
```csharp
using System;
using System.Collections.Generic;
using UnityEngine;

namespace BZAutoDrive
{
    /// <summary>Registry of the vehicles under autonomous control. All of it server-side.</summary>
    public static class AutoDriveManager
    {
        private static readonly Dictionary<int, AutoDrivePilot> pilots = new Dictionary<int, AutoDrivePilot>();
        private static readonly List<ChunkManager.ChunkObserver> observers = new List<ChunkManager.ChunkObserver>();

        /// <summary>
        /// Forces chunks to load around a position. Without this, on a server
        /// with no players connected there is no terrain loaded: there is no collision mesh,
        /// and a vehicle spawned there falls into the void.
        /// The same mechanism that the airdrop uses (AIDirectorAirDropComponent).
        /// </summary>
        public static void AddObserver(Vector3 _pos, int _viewDim = 3)
        {
            // _bBuildVisualMeshAround: TRUE is mandatory.
            // With false the game engine loads the chunk DATA but never builds the mesh
            // (ChunkManager: only the chunks in m_ViewingChunkPositions reach
            // doCopyChunksToUnity, which is where IsCollisionMeshGenerated is set).
            // Without a mesh there is no collider, and without a collider a vehicle has no ground.
            // That is why the wandering hordes' flag (bIsChunkObserver -> false) is enough
            // for zombies, which path on voxels, but NOT for vehicles.
            var obs = GameManager.Instance.AddChunkObserver(_pos, true, _viewDim, -1);
            if (obs != null) observers.Add(obs);
            Log.Out("[BZAD] observador de chunks (con malla) en " + _pos + " viewDim=" + _viewDim);
        }

        public static int ReleaseObservers()
        {
            int n = observers.Count;
            foreach (var o in observers) GameManager.Instance.RemoveChunkObserver(o);
            observers.Clear();
            return n;
        }

        public static int ObserverCount { get { return observers.Count; } }

        public static bool IsTracked(int _entityId) { return pilots.ContainsKey(_entityId); }

        public static bool IsServer
        {
            get
            {
                var cm = SingletonMonoBehaviour<ConnectionManager>.Instance;
                return cm != null && cm.IsServer;
            }
        }

        public static int Count { get { return pilots.Count; } }

        public static AutoDrivePilot Start(EntityVehicle _veh, Route _route)
        {
            if (_veh == null) return null;
            var pilot = new AutoDrivePilot(_veh, _route);
            pilots[_veh.entityId] = pilot;

            // _veh.bIsChunkObserver = true is NOT enough: that observer is created with
            // _bBuildVisualMeshAround=false and does not generate collision. The pilot keeps
            // its own bubble with a mesh (AutoDrivePilot.UpdateObserver).

            Log.Out("[BZAD] START id=" + _veh.entityId + " ruta='" + _route.name +
                    "' puntos=" + _route.Count + " vel=" + _route.targetSpeed);
            return pilot;
        }

        public static bool Stop(int _entityId)
        {
            AutoDrivePilot p;
            if (!pilots.TryGetValue(_entityId, out p)) return false;
            p.ReleaseObserver();
            if (p.Veh != null && p.Veh.movementInput != null)
            {
                p.Veh.movementInput.moveForward = 0f;
                p.Veh.movementInput.moveStrafe = 0f;
                p.Veh.movementInput.jump = true;
            }
            pilots.Remove(_entityId);
            Log.Out("[BZAD] STOP id=" + _entityId);
            return true;
        }

        public static void StopAll()
        {
            var ids = new List<int>(pilots.Keys);
            foreach (int id in ids) Stop(id);
        }

        /// <summary>Called from the Prefix of EntityVehicle.PhysicsFixedUpdate.</summary>
        public static void OnPhysicsTick(EntityVehicle _veh)
        {
            if (pilots.Count == 0) return;
            AutoDrivePilot p;
            if (!pilots.TryGetValue(_veh.entityId, out p)) return;

            // we only drive where the vehicle is really simulated
            if (_veh.isEntityRemote) return;

            try { p.OnPhysicsTick(Time.deltaTime); }
            catch (Exception e)
            {
                Log.Error("[BZAD] excepcion en piloto id=" + _veh.entityId + ", desenganchando");
                Log.Exception(e);
                pilots.Remove(_veh.entityId);
            }
        }

        public static IEnumerable<AutoDrivePilot> All() { return pilots.Values; }

        // --- search utilities ---

        public static List<EntityVehicle> ActiveVehicles()
        {
            var list = new List<EntityVehicle>();
            var vm = VehicleManager.Instance;
            if (vm != null) list.AddRange(vm.vehiclesActive);
            return list;
        }

        public static EntityVehicle FindById(int _id)
        {
            foreach (var v in ActiveVehicles()) if (v.entityId == _id) return v;
            return null;
        }

        public static EntityVehicle FindNearest(Vector3 _pos)
        {
            EntityVehicle best = null;
            float bestD = float.MaxValue;
            foreach (var v in ActiveVehicles())
            {
                float d = Vector3.SqrMagnitude(v.position - _pos);
                if (d < bestD) { bestD = d; best = v; }
            }
            return best;
        }
    }
}
```

#### `AutoDrivePilot.cs`
```csharp
using System;
using UnityEngine;

namespace BZAutoDrive
{
    /// <summary>
    /// Route-following controller for an EntityVehicle.
    ///
    /// WATCH OUT: this follower is DELIBERATELY simple. The goal of v0.1 is not to
    /// drive well, it is to answer a single question: does the dedicated server, with no
    /// human driver, simulate the vehicle physics, and does the input reach the engine?
    /// The framework's real pure pursuit replaces StepControl() without touching anything else.
    /// </summary>
    public class AutoDrivePilot
    {
        public readonly EntityVehicle Veh;
        public Route Route;

        public int   waypointIdx;
        public bool  finished;
        public float lookaheadBase = 8f;    // m
        public float lookaheadPerSpeed = 0.6f;  // m per m/s

        // last command sent (for telemetry)
        public float lastThrottle, lastSteer;
        public bool  lastBrake;

        // telemetry
        public int    ticks;
        public int    esperandoSuelo;
        public float  telemetryEverySec = 1f;
        private float telemetryAccum;
        private Vector3 lastLoggedPos;
        private bool  havePos;

        public AutoDrivePilot(EntityVehicle _veh, Route _route)
        {
            Veh = _veh;
            Route = _route;
            lastLoggedPos = _veh.position;
        }

        public Vector3 Pos { get { return Veh.position; } }

        private Transform Tf
        {
            get { return Veh.PhysicsTransform != null ? Veh.PhysicsTransform : Veh.ModelTransform; }
        }

        /// <summary>Called from the Prefix of EntityVehicle.PhysicsFixedUpdate, on the server.</summary>
        public void OnPhysicsTick(float _dt)
        {
            ticks++;
            UpdateObserver();

            // ---- guard: do not activate the physics without ground ----
            // If we activate the rigidbody before the chunk has a collision mesh,
            // the vehicle falls into the void and the game engine kills it. We wait.
            var world = GameManager.Instance.World;
            bool suelo = world != null && world.IsChunkAreaCollidersLoaded(Veh.position);
            if (!suelo)
            {
                esperandoSuelo++;
                if (esperandoSuelo == 1 || esperandoSuelo % 100 == 0)
                    Log.Out(string.Format(
                        "[BZAD] id={0} esperando colliders del chunk en ({1:0.0},{2:0.0},{3:0.0}) " +
                        "[areaLoaded={4}] tick={5}",
                        Veh.entityId, Veh.position.x, Veh.position.y, Veh.position.z,
                        world != null && world.IsChunkAreaLoaded(Veh.position), ticks));
                return;   // we touch nothing until there is ground
            }
            if (esperandoSuelo > 0)
            {
                Log.Out("[BZAD] id=" + Veh.entityId + " colliders listos tras " + esperandoSuelo + " ticks");
                esperandoSuelo = 0;
                // while we were waiting it may have sunk: we set it back on the terrain
                float h = GameManager.Instance.World.GetHeightAt(Veh.position.x, Veh.position.z);
                if (h > 0f && Veh.position.y < h + 0.5f)
                {
                    Veh.SetPosition(new Vector3(Veh.position.x, h + 1.5f, Veh.position.z));
                    Log.Out("[BZAD] id=" + Veh.entityId + " reapoyado en y=" + (h + 1.5f).ToString("0.0"));
                }
            }

            // The game engine short-circuits PhysicsFixedUpdate if RBActive is false
            // (PhysicsResetAndSleep + isKinematic = true). We force it.
            if (!Veh.RBActive)
            {
                Veh.RBActive = true;
                if (Veh.vehicleRB != null)
                {
                    Veh.vehicleRB.isKinematic = false;
                    Veh.vehicleRB.WakeUp();
                }
                Log.Out("[BZAD] id=" + Veh.entityId + " rigidbody activado");
            }

            // On a dedicated server movementInput is NEVER instantiated (it is only created for
            // the local player in EntityVehicle.AttachEntityToSelf). We create it ourselves.
            if (Veh.movementInput == null)
            {
                Veh.movementInput = new MovementInput();
                Log.Out("[BZAD] movementInput creado para vehiculo " + Veh.entityId);
            }

            // ---- game-engine BUG, only visible server-side ----
            // EntityVehicle.isTurnTowardsLook is static and starts as TRUE. In that mode
            // PhysicsFixedUpdate does GetAttachedPlayerLocal().vp_FPCamera, which on a
            // dedicated server is NULL -> NullReferenceException on EVERY tick, and the method
            // aborts before computing wheelDir and before calling SetWheelsForces.
            // Symptom: wheelMotor arrives fine but wheelDir=0 and torque=0, the vehicle does not move.
            if (EntityVehicle.isTurnTowardsLook)
            {
                EntityVehicle.isTurnTowardsLook = false;
                Log.Warning("[BZAD] isTurnTowardsLook estaba en true (NRE server-side) -> apagado");
            }

            // With lastInputController the steering maps directly:
            //   wheelDir = MoveTowards(wheelDir, moveStrafe * SteerAngleMax, rate)
            // which is exactly what a controller needs, instead of the incremental mode.
            Veh.movementInput.lastInputController = true;

            StepControl(_dt);

            Veh.movementInput.moveForward = lastThrottle;
            Veh.movementInput.moveStrafe  = lastSteer;
            Veh.movementInput.jump        = lastBrake;   // 'jump' is the brake on vehicles
            Veh.movementInput.running     = fullTorque;  // without this the game engine does wheelMotor *= 0.5

            // the first ticks are all logged: that is where it dies
            telemetryAccum += _dt;
            if (ticks <= 5 || telemetryAccum >= telemetryEverySec)
            {
                telemetryAccum = 0f;
                LogTelemetry();
            }
        }

        // ------------------------------------------------------------------
        // Follower: pure pursuit ported from the DayZ framework + speed
        // profile by curvature + stuck detector.
        // ------------------------------------------------------------------
        public readonly PurePursuit PP = new PurePursuit();
        public bool  fullTorque = true;
        public float latAccel = 3.0f;      // allowed lateral m/s2 -> v_max = sqrt(a/|k|)
        public float speedKp  = 0.6f;

        // stuck
        private float stuckTime, unstuckTime;
        public int    stuckCount;

        private void StepControl(float _dt)
        {
            if (Route == null || Route.Count < 2 || finished)
            {
                lastThrottle = 0f; lastSteer = 0f; lastBrake = true;
                return;
            }

            Vector3 pos = Pos;
            float speed = (Veh.vehicle != null) ? Veh.vehicle.CurrentForwardVelocity : 0f;
            float absSpeed = Mathf.Abs(speed);

            // ---- unstick maneuver ----
            // Without this the vehicle pushes against the obstacle with the throttle floored, and the
            // game engine's UnstickForce ends up making it CLIMB (it happened: it climbed a tree).
            if (unstuckTime > 0f)
            {
                unstuckTime -= _dt;
                lastThrottle = -0.6f;
                lastSteer = -Mathf.Sign(lastSteer == 0f ? 1f : lastSteer) * 0.7f;
                lastBrake = false;
                return;
            }
            if (Mathf.Abs(lastThrottle) > 0.3f && absSpeed < 0.5f)
            {
                stuckTime += _dt;
                if (stuckTime > 2.5f)
                {
                    stuckTime = 0f;
                    unstuckTime = 2.0f;
                    stuckCount++;
                    Log.Warning("[BZAD] id=" + Veh.entityId + " ATASCADO (" + stuckCount +
                                ") -> reversa 2 s");
                    if (stuckCount >= 5)
                    {
                        finished = true;
                        Log.Warning("[BZAD] id=" + Veh.entityId + " atascado 5 veces, abandono");
                    }
                    return;
                }
            }
            else stuckTime = 0f;

            // ---- steering: pure pursuit ----
            Transform t = Tf;
            float yawRad = Mathf.Atan2(t.forward.x, t.forward.z);
            float steerMax = (Veh.vehicle != null && Veh.vehicle.SteerAngleMax > 1f)
                             ? Veh.vehicle.SteerAngleMax : 30f;
            lastSteer = PP.Compute(Route, pos, yawRad, absSpeed, steerMax, Wheelbase());
            waypointIdx = PP.NearIdx;

            if (!Route.loop && PP.NearIdx >= Route.Count - 2)
            {
                finished = true;
                lastThrottle = 0f; lastSteer = 0f; lastBrake = true;
                Log.Out("[BZAD] ruta terminada, vehiculo " + Veh.entityId);
                return;
            }

            // ---- target speed: the route's own, capped by the curvature ahead ----
            float want = Route.targetSpeed;
            float kAhead = Mathf.Abs(PP.LastCurvature);
            if (kAhead > 0.001f)
            {
                float vCurve = PurePursuit.SpeedForCurvature(kAhead, latAccel);
                if (vCurve < want) want = vCurve;
            }
            if (want < 1.5f) want = 1.5f;

            // WATCH OUT: the error uses the SIGNED speed. With |v| the controller sees
            // "I am going fast" when it is actually going fast BACKWARDS, it cuts the throttle
            // and gets stuck in a limit cycle going back and forth (measured 2026-08-12).
            float err = want - speed;
            lastThrottle = Mathf.Clamp(err * speedKp, -1f, 1f);
            lastBrake = false;
            if (speed > want * 1.4f) { lastThrottle = 0f; lastBrake = true; }
        }

        /// <summary>
        /// Wheelbase MEASURED from the WheelColliders. In DayZ it had to be configured
        /// per vehicle; here it comes from the real geometry.
        /// </summary>
        private float cachedWheelbase = -1f;
        public float Wheelbase()
        {
            if (cachedWheelbase > 0f) return cachedWheelbase;
            float min = float.MaxValue, max = float.MinValue;
            for (int i = 0; i < Veh.wheels.Length; i++)
            {
                var wc = Veh.wheels[i].wheelC;
                if (wc == null) continue;
                float z = wc.transform.localPosition.z;
                if (z < min) min = z;
                if (z > max) max = z;
            }
            cachedWheelbase = (max > min) ? (max - min) : 2.5f;
            Log.Out("[BZAD] id=" + Veh.entityId + " wheelbase medido = " +
                    cachedWheelbase.ToString("0.00") + " m");
            return cachedWheelbase;
        }

        private static Vector3 Flat(Vector3 v) { v.y = 0f; return v; }

        // ------------------------------------------------------------------
        // Chunk bubble WITH a mesh that follows the vehicle.
        // bIsChunkObserver does not work: it creates the observer with
        // _bBuildVisualMeshAround=false and the collision is never generated.
        // ------------------------------------------------------------------
        private int obsChunkX = int.MinValue, obsChunkZ = int.MinValue;
        private ChunkManager.ChunkObserver obs;

        private void UpdateObserver()
        {
            int cx = World.toChunkXZ(Utils.Fastfloor(Veh.position.x));
            int cz = World.toChunkXZ(Utils.Fastfloor(Veh.position.z));
            if (cx == obsChunkX && cz == obsChunkZ) return;
            obsChunkX = cx; obsChunkZ = cz;

            var gm = GameManager.Instance;
            if (obs != null) gm.RemoveChunkObserver(obs);
            obs = gm.AddChunkObserver(Veh.position, true, 3, -1);
            Log.Out("[BZAD] id=" + Veh.entityId + " burbuja de chunks -> (" + cx + "," + cz + ")");
        }

        public void ReleaseObserver()
        {
            if (obs != null)
            {
                GameManager.Instance.RemoveChunkObserver(obs);
                obs = null;
            }
        }

        // ------------------------------------------------------------------
        // Telemetry: this is what answers the open question.
        // ------------------------------------------------------------------
        private void LogTelemetry()
        {
            Vector3 p = Pos;
            float moved = havePos ? Vector3.Distance(p, lastLoggedPos) : 0f;
            lastLoggedPos = p; havePos = true;

            Rigidbody rb = Veh.vehicleRB;
            var v = Veh.vehicle;

            Log.Out(string.Format(
                "[BZAD] id={0} pos=({1:0.0},{2:0.0},{3:0.0}) mov/s={4:0.00} " +
                "| remote={5} RBActive={6} kinematic={7} ruedas={8} driver={9} " +
                "| IN thr={10:0.00} steer={11:0.00} brake={12} " +
                "| OUT wheelMotor={13:0.00} wheelDir={14:0.0} fwdVel={15:0.00} torque%={16:0.00}",
                Veh.entityId, p.x, p.y, p.z, moved,
                Veh.isEntityRemote, Veh.RBActive, rb != null ? rb.isKinematic : true,
                Veh.GetWheelsOnGround(), Veh.hasDriver,
                lastThrottle, lastSteer, lastBrake,
                Veh.wheelMotor, Veh.wheelDir,
                v != null ? v.CurrentForwardVelocity : 0f,
                v != null ? v.CurrentMotorTorquePercent : 0f));
        }

        public string StatusLine()
        {
            return string.Format("id={0} ruta='{1}' wp={2}/{3} {4} thr={5:0.00} steer={6:0.00}",
                Veh.entityId, Route != null ? Route.name : "-",
                waypointIdx, Route != null ? Route.Count : 0,
                finished ? "TERMINADA" : "activa", lastThrottle, lastSteer);
        }
    }
}
```

#### `PurePursuit.cs`
```csharp
using System;
using UnityEngine;

namespace BZAutoDrive
{
    /// <summary>
    /// Pure pursuit ported from the DayZ framework (BZBusService.ComputePurePursuit).
    /// What is ported is the MATH, with its corrections earned the hard way by measuring; no Enforce at all.
    ///
    /// Key difference in units compared with DayZ:
    ///   DayZ  -> cmd = deg_wheel / PlantSteerGain, with PlantSteerGain=85.1 IDENTIFIED by measuring.
    ///   7DTD  -> the game engine does wheelDir = moveStrafe * SteerAngleMax, so the plant gain
    ///            IS SteerAngleMax and it is READ from the vehicle config (32 on the Truck4x4).
    /// The wheelbase is also measured from the WheelColliders instead of being configured by hand.
    /// </summary>
    public class PurePursuit
    {
        // --- parameters (same concepts as the DayZ version) ---
        public float LdStraightM = 8.5f;      // Ld on a straight. The LONG Ld is the stable side.
        public float LdCurveM = 4.5f;         // Ld in a tight curve: it nails the apex, it does not anticipate
        public float LdMinM = 2.0f;
        public float LdSpeedTimeS = 0.4f;     // floor Ld >= 0.4*v (Arma-style steerAhead)
        public bool  UseCurvatureAdaptiveLd = true;
        public float CurvatureSenseM = 12.0f; // how far ahead to sense the turn of the trace
        public float CurvatureTurnRad = 0.6f; // accumulated turn that saturates the shortening
        public float LdSpeedFactor = 0.55f;   // Ld ~ proportional to v on a straight (tau = Ld/v ~ const)
        public float CurveComp = 1.0f;        // compensation for cutting inside the arc
        public float CurveCompMaxSpeedMs = 12.0f;
        public float Gain = 1.0f;
        public float LpureFloorM = 3.0f;      // floor of the real distance to the aim point

        // --- state ---
        public int NearIdx;

        // --- diagnostics ---
        public float LastLd, LastAlphaDeg, LastDistT, LastKappa, LastDeltaDeg, LastCurvature;
        public int   LastAimIdx;
        public bool  LastSaturated;

        /// <summary>Normalized steering command [-1,1] for movementInput.moveStrafe.</summary>
        public float Compute(Route route, Vector3 pos, float yawRad, float speedMs,
                             float steerAngleMax, float wheelbase)
        {
            if (route == null || route.Count < 2) return 0f;
            int count = route.Count;

            // 1) nearest waypoint, searching in a window around the last one
            NearIdx = FindNearest(route, pos, NearIdx);

            // 2) lookahead distance
            float v = Mathf.Max(speedMs, 1.0f);
            float Ld = Mathf.Max(LdStraightM, LdSpeedTimeS * v);
            if (Ld < LdMinM) Ld = LdMinM;

            float fracCurve = 0f;
            if (UseCurvatureAdaptiveLd)
            {
                float totalTurn = SenseTurn(route, NearIdx, CurvatureSenseM);
                fracCurve = Mathf.Clamp01(totalTurn / Mathf.Max(CurvatureTurnRad, 0.01f));
                Ld = LdStraightM - fracCurve * (LdStraightM - LdCurveM);

                // Ld grows with v ONLY on a straight: it keeps tau=Ld/v constant and averages out the jitter
                // of the trace. In a curve, the short ldCurve rules.
                if (LdSpeedFactor > 0f)
                {
                    float floorV = LdSpeedFactor * v * (1f - fracCurve);
                    if (floorV > Ld) Ld = floorV;
                }
                if (Ld < LdMinM) Ld = LdMinM;
            }
            LastLd = Ld;

            // 3) walk Ld meters along the trace to get the aim point
            int aim = NearIdx;
            float acc = 0f;
            Vector3 prev = route[NearIdx];
            int steps = 0;
            while (acc < Ld && steps < count)
            {
                aim++;
                steps++;
                if (!route.loop && aim >= count) { aim = count - 1; break; }
                Vector3 cur = route[aim];
                acc += Vector3.Distance(Flat(prev), Flat(cur));
                prev = cur;
            }
            LastAimIdx = aim;
            Vector3 target = route[aim];

            // 3b) cut compensation: the pursuit cuts INSIDE the arc. I move the target
            // outward by the sagitta ~|k|*Ld^2/8. The "outside" is the side where the arc bulges,
            // without depending on the sign of the curvature.
            LastCurvature = 0f;
            if (CurveComp > 0f && speedMs < CurveCompMaxSpeedMs)
            {
                float k = LocalCurvature(route, aim);
                LastCurvature = k;
                if (Mathf.Abs(k) > 0.01f)
                {
                    Vector3 pPrev = route[aim - 1];
                    Vector3 pNext = route[aim + 1];
                    Vector3 outward = Flat(target) - Flat((pPrev + pNext) * 0.5f);
                    float outLen = outward.magnitude;
                    if (outLen > 0.001f)
                    {
                        outward /= outLen;
                        float sag = Mathf.Abs(k) * Ld * Ld * 0.125f * CurveComp;
                        target += outward * sag;
                    }
                }
            }

            // 4) alpha = angle from the current heading to the target
            float dx = target.x - pos.x;
            float dz = target.z - pos.z;
            float distT = Mathf.Sqrt(dx * dx + dz * dz);
            LastDistT = distT;
            if (distT < 0.3f) return 0f;

            float targetHeading = Mathf.Atan2(dx, dz);
            float alpha = Mathf.DeltaAngle(yawRad * Mathf.Rad2Deg, targetHeading * Mathf.Rad2Deg) * Mathf.Deg2Rad;
            LastAlphaDeg = alpha * Mathf.Rad2Deg;

            // 5) curvature -> wheel angle -> command
            // WATCH OUT (this cost a lot in DayZ): the canonical form is kappa = 2*sin(alpha)/L with L the
            // REAL distance to the aim point, NOT the desired Ld. If the aim point ends up too close (end of route,
            // low speed) and you use Ld, the formula lies and 1/L inflates the gain ~3x ->
            // full-lock steering jerk with the target almost straight ahead. Hence the floor.
            float Lpure = distT;
            if (Lpure < Ld) Lpure = Ld;
            if (Lpure < LpureFloorM) Lpure = LpureFloorM;

            float kappa = 2f * Mathf.Sin(alpha) / Lpure;
            LastKappa = kappa;

            float deltaRad = Mathf.Atan(wheelbase * kappa);
            LastDeltaDeg = deltaRad * Mathf.Rad2Deg;

            // plant gain = SteerAngleMax, READ from the vehicle (in DayZ it had to be measured)
            float gainDeg = (steerAngleMax > 1f) ? steerAngleMax : 30f;
            float cmd = (LastDeltaDeg / gainDeg) * Gain;

            LastSaturated = (cmd > 0.95f || cmd < -0.95f);
            return Mathf.Clamp(cmd, -1f, 1f);
        }

        // ------------------------------------------------------------------

        private static Vector3 Flat(Vector3 v) { v.y = 0f; return v; }

        /// <summary>Nearest waypoint, in a window around the last one (avoids jumping to another stretch).</summary>
        private int FindNearest(Route route, Vector3 pos, int from)
        {
            int count = route.Count;
            int lo = from - 20, hi = from + 40;
            if (!route.loop)
            {
                if (lo < 0) lo = 0;
                if (hi > count - 1) hi = count - 1;
            }
            int best = from;
            float bestD = float.MaxValue;
            for (int i = lo; i <= hi; i++)
            {
                Vector3 w = route[i];
                float dx = pos.x - w.x, dz = pos.z - w.z;
                float d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; best = i; }
            }
            if (route.loop) best = ((best % count) + count) % count;
            return best;
        }

        /// <summary>Total turn (rad) that the trace accumulates over the next _senseM meters.</summary>
        private float SenseTurn(Route route, int start, float senseM)
        {
            int count = route.Count;
            Vector3 d0 = Flat(route[start + 1] - route[start]);
            if (d0.sqrMagnitude < 0.0001f) return 0f;
            float prevHead = Mathf.Atan2(d0.x, d0.z);

            float acc = 0f, totalTurn = 0f;
            Vector3 prev = route[start];
            int i = start, guard = 0;
            while (acc < senseM && guard++ < count)
            {
                i++;
                if (!route.loop && i >= count) break;
                Vector3 cur = route[i];
                Vector3 seg = Flat(cur - prev);
                float segLen = seg.magnitude;
                if (segLen > 0.1f)
                {
                    float h = Mathf.Atan2(seg.x, seg.z);
                    float dh = Mathf.DeltaAngle(prevHead * Mathf.Rad2Deg, h * Mathf.Rad2Deg) * Mathf.Deg2Rad;
                    totalTurn += Mathf.Abs(dh);
                    prevHead = h;
                }
                acc += segLen;
                prev = cur;
            }
            return totalTurn;
        }

        /// <summary>Menger curvature at waypoint i, using its neighbors.</summary>
        public static float LocalCurvature(Route route, int i)
        {
            Vector3 a = Flat(route[i - 1]);
            Vector3 b = Flat(route[i]);
            Vector3 c = Flat(route[i + 1]);
            float ab = Vector3.Distance(a, b);
            float bc = Vector3.Distance(b, c);
            float ca = Vector3.Distance(c, a);
            if (ab < 0.01f || bc < 0.01f || ca < 0.01f) return 0f;
            // signed area (XZ plane)
            float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
            float area = cross * 0.5f;
            return 4f * area / (ab * bc * ca);
        }

        /// <summary>Maximum speed for a given curvature, from the allowed lateral acceleration.</summary>
        public static float SpeedForCurvature(float kappa, float latAccel)
        {
            float k = Mathf.Abs(kappa);
            if (k < 0.001f) return 999f;
            return Mathf.Sqrt(latAccel / k);
        }
    }
}
```

#### `Route.cs`
```csharp
using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using UnityEngine;

namespace BZAutoDrive
{
    /// <summary>
    /// Route polyline in world coordinates. The format is deliberately flat
    /// so that it is interchangeable with the takes of the DayZ framework.
    /// </summary>
    public class Route
    {
        public string name = "unnamed";
        public bool loop = true;
        public float targetSpeed = 8f;          // m/s
        public List<Vector3> points = new List<Vector3>();

        public int Count { get { return points.Count; } }

        public Vector3 this[int i]
        {
            get
            {
                int n = points.Count;
                if (n == 0) return Vector3.zero;
                if (loop) return points[((i % n) + n) % n];
                return points[Mathf.Clamp(i, 0, n - 1)];
            }
        }

        /// <summary>Circle centered on _center. A self-contained test route, with no files.</summary>
        public static Route Circle(Vector3 _center, float _radius, int _segments = 32, float _speed = 6f)
        {
            var r = new Route();
            r.name = "circle_r" + _radius.ToString("0");
            r.loop = true;
            r.targetSpeed = _speed;
            for (int i = 0; i < _segments; i++)
            {
                float a = (float)i / _segments * Mathf.PI * 2f;
                r.points.Add(new Vector3(
                    _center.x + Mathf.Cos(a) * _radius,
                    _center.y,
                    _center.z + Mathf.Sin(a) * _radius));
            }
            return r;
        }

        /// <summary>Straight line of _length meters from _from in direction _dir.</summary>
        public static Route Straight(Vector3 _from, Vector3 _dir, float _length, float _speed = 6f)
        {
            var r = new Route();
            r.name = "straight_" + _length.ToString("0");
            r.loop = false;
            r.targetSpeed = _speed;
            Vector3 d = _dir.normalized;
            for (float t = 0f; t <= _length; t += 5f)
                r.points.Add(_from + d * t);
            return r;
        }

        // --- serialization ---

        private class Dto
        {
            public string name;
            public bool loop;
            public float targetSpeed;
            public float[][] points;
        }

        public static Route Load(string _path)
        {
            var dto = JsonConvert.DeserializeObject<Dto>(File.ReadAllText(_path));
            var r = new Route();
            r.name = string.IsNullOrEmpty(dto.name) ? Path.GetFileNameWithoutExtension(_path) : dto.name;
            r.loop = dto.loop;
            r.targetSpeed = dto.targetSpeed > 0f ? dto.targetSpeed : 6f;
            if (dto.points != null)
                foreach (var p in dto.points)
                    if (p != null && p.Length >= 3)
                        r.points.Add(new Vector3(p[0], p[1], p[2]));

            // The file's Y is only a reference (it comes from the heightmap on disk, which may
            // include POI stamps). The authority is the loaded world. The follower works
            // in XZ anyway, so this is for diagnostics and for rendering.
            var world = GameManager.Instance != null ? GameManager.Instance.World : null;
            if (world != null)
            {
                float maxDif = 0f;
                for (int i = 0; i < r.points.Count; i++)
                {
                    Vector3 p = r.points[i];
                    float h = world.GetHeightAt(p.x, p.z);
                    if (h > 0f)
                    {
                        maxDif = Mathf.Max(maxDif, Mathf.Abs(h - p.y));
                        p.y = h + 0.5f;
                        r.points[i] = p;
                    }
                }
                Log.Out("[BZAD] ruta '" + r.name + "': Y reajustada contra el mundo (dif max " +
                        maxDif.ToString("0.0") + " m)");
            }
            return r;
        }

        public void Save(string _path)
        {
            var dto = new Dto { name = name, loop = loop, targetSpeed = targetSpeed };
            dto.points = new float[points.Count][];
            for (int i = 0; i < points.Count; i++)
                dto.points[i] = new float[] { points[i].x, points[i].y, points[i].z };
            File.WriteAllText(_path, JsonConvert.SerializeObject(dto, Formatting.Indented));
        }
    }
}
```

#### `ConsoleCmdBzDrive.cs` — spawning the vehicle and seating the driver (excerpt)
```csharp
    private void CmdSpawn(List<string> p)
    {
        if (p.Count < 2) { SdtdConsole.Instance.Output("falta la clase, ej: bzdrive spawn vehicleMinibike"); return; }
        string cls = p[1];
        // WATCH OUT: EntityClass.FromString is just _s.GetHashCode() -> it can be negative.
        // The real validation is that the class exists in the dictionary.
        int et = EntityClass.FromString(cls);
        if (!EntityClass.list.ContainsKey(et))
        {
            SdtdConsole.Instance.Output("[BZAD] clase desconocida: " + cls);
            SdtdConsole.Instance.Output("       proba: vehicleMinibike, vehicleMotorcycle, vehicleTruck4x4, vehicleBicycle");
            return;
        }

        Vector3 pos;
        if (p.Count >= 5)
            pos = new Vector3(ParseF(p[2]), ParseF(p[3]), ParseF(p[4]));
        else
        {
            var world = GameManager.Instance.World;
            var players = world.GetPlayers();
            if (players.Count > 0) pos = players[0].position + Vector3.forward * 4f;
            else { SdtdConsole.Instance.Output("[BZAD] sin jugadores: dame coordenadas x y z"); return; }
        }

        // with no players there are no loaded chunks: we force them to load before spawning
        AutoDriveManager.AddObserver(pos);

        // set it on the terrain instead of letting it drop from wherever it is
        var w = GameManager.Instance.World;
        float suelo = w.GetHeightAt(pos.x, pos.z);
        if (suelo > 0f)
        {
            pos.y = suelo + 1.5f;
            SdtdConsole.Instance.Output("[BZAD] terreno en y=" + suelo.ToString("0.0") +
                                        ", spawneo en y=" + pos.y.ToString("0.0"));
        }

        var e = EntityFactory.CreateEntity(et, pos) as EntityVehicle;
        if (e == null) { SdtdConsole.Instance.Output("[BZAD] esa clase no es un EntityVehicle"); return; }
        GameManager.Instance.World.SpawnEntityInWorld(e);
        e.bIsChunkObserver = true;   // so that it drags its own bubble along
        SdtdConsole.Instance.Output("[BZAD] spawn id=" + e.entityId + " en " + pos);
        SdtdConsole.Instance.Output("[BZAD] espera ~5s a que carguen los chunks y corre 'bzdrive list'");
    }

    private void CmdDriver(List<string> p)
    {
        var v = Resolve(p, 1); if (v == null) return;
        string cls = p.Count >= 3 ? p[2] : "npcSurvivorTemplate";
        int et = EntityClass.FromString(cls);
        if (!EntityClass.list.ContainsKey(et))
        {
            SdtdConsole.Instance.Output("[BZAD] clase de NPC desconocida: " + cls);
            return;
        }
        var npc = EntityFactory.CreateEntity(et, v.position + Vector3.up * 1.2f) as EntityAlive;
        if (npc == null) { SdtdConsole.Instance.Output("[BZAD] esa clase no es un EntityAlive"); return; }

        GameManager.Instance.World.SpawnEntityInWorld(npc);
        npc.StartAttachToEntity(v, 0);

        SdtdConsole.Instance.Output("[BZAD] NPC id=" + npc.entityId + " (" + cls + ") al volante de id=" + v.entityId);
        SdtdConsole.Instance.Output("       hasDriver=" + v.hasDriver + " IsEngineRunning=" + v.IsEngineRunning +
                                    " vehiculo.remote=" + v.isEntityRemote + " npc.remote=" + npc.isEntityRemote);
    }
```

**The H1 test protocol** (server console): `bzdrive load <x> <y> <z>` (forces the chunks to load; wait ~10 s) → `bzdrive spawn vehicleTruck4x4 <x> <y> <z>` → `bzdrive list` (confirm `ruedas` (= wheels on the ground) > 0) → `bzdrive driver <id> npcTraderJoel` → `bzdrive circle <id> 12` → read the `[BZAD]` telemetry once per second → `bzdrive stop`.

**How to read the telemetry:** `remote=False` (the server simulates; with `True` it is not simulated here) · `RBActive=True kinematic=False` (the rigidbody is alive) · `ruedas>0` (there is ground with collision) · `mov/s > 0` (it really moves) · `IN thr` ≈ `OUT wheelMotor` (the input reaches the engine) · `fwdVel` (real speed).

### E.4 — The order: milestones with a "done" criterion

Each milestone has a **criterion you can measure in the game**. Do not move on until you meet it. The numbers come from what was measured in DayZ and in the 7DTD proof of concept; adjust them if the game gives something else, but **with a measurement that justifies it**.

**H0 — The environment.** A dedicated server installed, a mod that loads (in the log: `[BZAD] ... iniciando` = "starting", `parches Harmony aplicados` = "Harmony patches applied"), a telnet console, and a command of your own (`bzdrive` in the example) to spawn, seat the driver, start and stop.
*Done:* the log shows the mod loading and the command responds.

**H1 — Drive a circle (the proof of concept of E.3).** Spawn a `vehicleTruck4x4` on flat ground, seat an `npcTraderJoel`, and follow a circle of 12 m radius with the pure pursuit of E.3.
*Done (what the proof of concept gave):* fitted radius 11.91 m against 12.00; steady-state tracking error **mean 0.07 m · RMS 0.10 m · max 0.37 m**; in the telemetry, `remote=False RBActive=True ruedas=4 driver=True` (`ruedas` = wheels) and `IN thr` ≈ `OUT wheelMotor`.

**H2 — The recorder.** While a player drives, record at **40 Hz** a `frame_<fecha>_<clase>.csv` with the exact columns of E.5.1, plus the `header_<fecha>_<clase>.txt` (E.5.2). A key marks events (`is_marker`).
*Done:* a 2-minute lap gives a CSV with an increasing `t_cum` and no jumps over 0.1 s; the distance summed from the `x, z` matches the integrated speed (`Σ speed_kmh/3.6 × dt`) within 3 %; the trace drawn on the map follows the street; `heading` matches the direction of motion (median difference < 5° above 10 km/h, going forward).
⚠ **Where to record:** while the player drives, the physics is simulated by **their client** (authority through seat 0). Record on the driver's client and copy the file, or record on the server what the server sees of the vehicle (less precise). In DayZ, recording happens on the client.

**H3 — The conversion.** Run the Python converter as it is on the take: `python tools/frame_to_route.py <frame_csv> <NOMBRE> --profile <carpeta>` (E.5.4).
*Done:* the four files come out (`BZBusRoute_<NOMBRE>.json`, `_hdr.json`, `_wp.csv`, `_cinta.csv`); in the `_hdr.json`, `WpRows` and `CintaRows` match the rows of each file; in `_cinta.csv`, `s` always grows (it never goes back) and the last `s` is the length of the route.

**H4 — Replay the tape (the central milestone).** Load `_hdr.json` + `_wp.csv` + `_cinta.csv`, spawn at wp 0 with the recorded heading, and drive with the logic of E.6: cursor by distance, feed-forward of the recorded steering + lateral corrector, target speed from the tape.
*Done, on a 1-3 km take with no stops:* lateral error against the recorded trace **p90 < 0.5 m, max < 1.5 m**; travel time within **±5 %** of the recorded one; it reaches the end **less than 1 m** from the last point. All of this measured with one telemetry line per second (`wp`, the cursor's `s`, `lat`, real vs recorded speed).

**H5 — Recorded stops and direction changes.** A take with two stops (the human stopped for 5 s) and a maneuver with reverse (a K-turn or backing in).
*Done:* the vehicle stops **where** the human stopped (±0.5 m) and **for the recorded time** (±1 s); the reverse happens in the right leg and closes the direction change without waiting around (in DayZ, 4-5 s stopped in total at a direction change with a recorded stop).

**H6 — Never stay stopped (AutoRecovery).** Block the route with a parked vehicle and, in another test, with a fixed object.
*Done:* the jam is detected **by lack of progress** in ≤ 10-20 s (not by speed), the recovery starts and **does not crash into what is behind it**; the route is given up **only** if the recovery is exhausted; a recorded stop **never** triggers a recovery; zero new crashes on a clear route.

**H7 — Service robustness (optional, for a service with several vehicles).** Several runners at once, the start point occupied (the second one **waits**; it does not show up displaced), the route load spread over several server frames (no server frame gets longer), and a status API for your mod (blocked, recovering, recent crash): see Appendix B.7 of the manual for the DayZ one.

### E.5 — The file formats (the contract between the three pieces)

If the 7DTD recorder writes **exactly** these columns, the DayZ Python converter can be reused without changing a line. What is a porting recommendation, and not something the DayZ code does, is marked **[port]**.

#### E.5.0 The complete flow

```
The driver's CLIENT (human)
  "start" key    → frame_<ts>_<Clase>.csv  +  header_<ts>_<Clase>.txt
  "mark" key     → is_marker=1 on the NEXT frame (a stop or an event)
  "finish" key
      ↓ copy to the place where the conversion runs
[frame_merge.py]    several frame_ files of a route recorded in parts → a single one
[envelope_emit.py]  frame_ → envelope_<Clase>.csv   (optional; what was measured of the vehicle)
frame_to_route.py   frame_ + header_ (+ envelope_<Clase>.csv) → BZBusRoute_<R>.json · _hdr.json · _wp.csv · _cinta.csv
      ↓ copy to the server's routes folder
SERVER: _hdr.json (small JSON) + _wp.csv and _cinta.csv read line by line, spread over several server frames
```

- `<ts>` = `YYYYMMDD-HHMMSS` in **UTC** (not local time: keep this in mind when you look for a take by time).
- `frame_X.csv` goes with `header_X.txt` in **the same directory**. The converter pairs them by replacing the `frame_` prefix with `header_` and the extension with `.txt`.
- **Where to record:** while a human drives, the physics is simulated by **their client**. In DayZ you record on the client and copy the file to the server by hand. **[port]** In 7DTD the same happens while the human is in seat 0 (E.2). Record on the driver's client.

#### E.5.1 `frame_*.csv`: the take (18 columns, in this order, with a header row)

```
t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg,is_marker,pitch_deg,surface,chassis_kmh
```

| col | name | unit | what it is · **[port]** how to get it in 7DTD |
|---|---|---|---|
| 0 | `t_cum` | s | sum of `dt` since the start; row 0 has `t_cum = 0`, `dt = 0` (initial state) |
| 1 | `dt` | s | physics step of THAT row. DayZ: fixed 0.025 (40 Hz). **[port]** Unity uses `Time.fixedDeltaTime` = 0.02 by default; see E.5.5 |
| 2 | `throttle` | 0..1 | applied throttle. **[port]** `max(0, movementInput.moveForward)` when going forward |
| 3 | `brake` | 0..1 | applied brake. **[port]** `jump ? 1 : 0` (the 7DTD brake is binary) |
| 4 | `steering` | −1..1 | the steering KEY. **It is not used for replay** (with a keyboard it is almost binary): it is recorded for diagnostics |
| 5 | `gear` | integer | **0 = reverse, 1 = neutral, 2 = first**, 3 = second… **[port]** without a gearbox: 0 if it is going backwards, 2 if it is going forward, 1 if it is still and not accelerating |
| 6 | `handbrake` | 0..1 | handbrake. **[port]** 0 if the vehicle does not have one |
| 7-9 | `x,y,z` | m | world position, **y = up**. **[port]** `vehicle.position` (Unity is also Y-up) |
| 10 | `heading` | degrees | `atan2(fwd.x, fwd.z)` in degrees, range (−180, 180], **0 = +Z, 90 = +X, grows clockwise seen from above**. **[port]** `Mathf.Atan2(fwd.x, fwd.z) * Mathf.Rad2Deg` |
| 11 | `speed_kmh` | km/h | WHEEL speedometer, as an absolute value. **[port]** if there is no wheel speedometer, the same as `chassis_kmh` |
| 12 | `rpm` | rpm | engine rpm. **[port]** 0 if it does not exist |
| 13 | `front_wheel_deg` | degrees | **REAL angle of the front wheel relative to the chassis**, + = right, with the game engine's ramp already applied. **It is the most important column of the take**: it is the one that gets replayed. **[port]** `WheelCollider.steerAngle` of a front wheel, or `wheelDir` if you read it from the vehicle; check the sign (+ = right) |
| 14 | `is_marker` | 0/1/2 | 1 = stop/event mark; 2 = manual leg break. **Only one row** carries it (the flag is consumed when the row is written) |
| 15 | `pitch_deg` | degrees | `asin(clamp(fwd.y, −1, 1))` in degrees; + = nose up |
| 16 | `surface` | text | name of the material under the vehicle, or `none`. **It is the only text column** |
| 17 | `chassis_kmh` | km/h | `|linear velocity of the body| × 3.6`. **[port]** `rigidbody.velocity.magnitude * 3.6` |

Recorder rules:
- **One row per physics step** of the vehicle being recorded. **[port]** A `Postfix` on `EntityVehicle.PhysicsFixedUpdate` of the local player's vehicle.
- If `dt ≤ 0`, the row is not written.
- Buffer in memory and append to disk every ~30 rows, and always when you finish. Writing row by row slows the game down.
- Numbers with a decimal point (`3.14`), **never a comma**: use `CultureInfo.InvariantCulture` in C#. A system in Spanish writes `3,14` and breaks the CSV.

#### E.5.2 `header_*.txt`: the vehicle profile (one `key=value` line per item, written once at the start)

| key | required | what it is · **[port]** |
|---|---|---|
| `vehicleClass` | **yes** | vehicle class; it also names the files. **[port]** `EntityClass.list[vehicle.entityClass].entityClassName` (e.g. `vehicleTruck4x4`) |
| `wheelbase` | **yes** | wheelbase in m: project the contact point of each wheel onto the forward axis (XZ) and take max − min. **[port]** from the `WheelCollider`s (2.85 m on the Truck4x4) |
| `maxSteeringAngle` | **yes, > 0** | maximum wheel angle, in degrees. **[port]** `Vehicle.SteerAngleMax`. With 0 the converter divides by zero |
| `mass` | no (1000) | kg. **[port]** `vehicleRB.mass` |
| `gearsCount` | no (6) | **[port]** 1 |
| `engineRPMIdle`, `engineRPMMax`, `engineRPMRedline` | no | **[port]** 0 |
| `wheelCount`, `wheelCountPresent` | no | number of wheels |
| `attachmentsCount`, `attachments` | no | installed parts; **[port]** 0 and empty |

With no vehicle at the start, the file has a single line: `NO_VEHICLE_AT_START`.

#### E.5.3 What the converter really needs

- **Without this it aborts:** from the header, `vehicleClass`, `wheelbase` and `maxSteeringAngle ≠ 0`. From the take, `x, y, z, heading, speed_kmh, gear`. A row that does not parse is dropped silently; if all of them are dropped, you get "grabación vacía" (= "empty recording").
- **Without this it works, but badly:**
  - `t_cum` and `dt`: without them there are no declared stops and the tape comes out with a stepped `s`.
  - `is_marker`: without it there are no stops and no events.
  - `chassis_kmh`: if it is missing, the converter uses `speed_kmh`.
  - `front_wheel_deg`: **without it there is no useful tape**.
  - `throttle`, `brake`: the pedals of the tape.
- **Only for the envelope (E.5.8):** `steering`, `rpm`, `pitch_deg`, `handbrake`, `surface`.

#### E.5.4 Running the conversion

```
python tools/frame_merge.py <NOMBRE> parte1.csv parte2.csv … [--out DIR]          # only if the route was recorded in parts
python tools/envelope_emit.py <frame.csv> --out <CARPETA_RUTAS>                    # optional, before converting
python tools/frame_to_route.py <frame.csv> <NOMBRE> --profile <CARPETA_RUTAS> --cinta
```

- `--profile` is the output folder and it is **required**: without it the converter exits with an error (the Wizard passes it for you). The converter also looks for `envelope_<Clase>.csv` there.
- `--cinta` writes the `_cinta.csv`. **Without it there is no faithful replay.**
- `--keep-inputs` writes throttle and brake to the `_wp.csv`. The tape does not need it.
- `--sin-marca N …` deletes the N-th mark (1..N, in order). `--marca-wp W …` adds an event at output waypoint W. This is how you fix the marks **without editing the take**.
- The docstring mentions `--min-move`, but that option does not exist: passing it gives an error.
- **`frame_merge.py`** orders the parts **by where their positions join**, never by name or by time: the end of one part must be less than 8 m from the start of the next one, with exactly one candidate. It requires the same header and the same class in all the parts, and it re-accumulates `t_cum`.

#### E.5.5 What to do with the DayZ-specific parts

| DayZ field | What to do in 7DTD |
|---|---|
| `gear` 0/1/2+ | without a gearbox: 0 = reverse, 2 = forward, 1 = still. `MaxGear` stays at 2. The framework only needs to know **the direction** (`gear == 0` ⇒ reverse leg) |
| `rpm`, `engineRPM*` | 0. The gates that use the idle RPM turn themselves off; `GearStrategy` is not written |
| `speed_kmh` vs `chassis_kmh` | if you only have one, write the same value in both |
| `surface` | name of the block or material under the wheels, or `none` |
| `DriverClass` in the hdr (`eAI_SurvivorM_Boris`) | the converter sets it: your runner ignores it and seats its own NPC (`npcTraderJoel` in the proof of concept) |
| `Attachments`, `QuestTravelID`, `QuestEscortID`, `RespawnDelay`, `AverageSpeedMS`, `FollowPath` and the ~27 keys of `driving_config_template.json` | ignore them (they belong to v1.1 or to Expansion) |
| **40 Hz** | the Python code counts windows **in rows**, assuming 40 Hz (e.g. 8 rows = 0.2 s, 80 rows = 2 s, default `dt` 0.025). **[port]** The simplest option: set `Time.fixedDeltaTime = 0.025` while recording, or record every step and accept windows that are 20 % shorter. Do **not** resample the take: the tape is indexed by distance, so the density only has to be even |

#### E.5.6 What comes out (in `--profile`, with the prefix `BZBusRoute_<NOMBRE>`)

**`_hdr.json`**, the header. It is a small JSON, with no waypoints.

| key | what it is |
|---|---|
| `VehicleClass`, `Wheelbase` | from the header |
| `Fingerprint` | `{VehicleClass, MaxSteeringAngle, Wheelbase, RminM = wb/tan(maxSteer), Mass, GearsCount}` |
| `Events` | `[{"wp": i, "trigger": {"type": "wp_reached", "wp": i}, "actions": []}, …]`, one per mark. **The type must be `wp_reached`** |
| `MaxGear` | highest gear used (≥ 2) |
| `EndpointBrakeDecel` | m/s², the human's braking at the end (if it could be measured), clamped to [0.5, 8] |
| `CreepKmh`, `CreepSource`, `CreepSamples`, `CoastAtRest` | derived from the envelope: creep speed with no throttle. **[port]** almost surely 0 without a clutch |
| `EnvelopeClass`, `Envelope` | copy of the class envelope (a fallback if the route is taken to another server) |
| `WpRows`, `CintaRows` | data rows of `_wp.csv` and `_cinta.csv` (for the load %) |

**`_wp.csv`**, the waypoint polyline: one every 0.5-2 m, denser the slower the vehicle goes. **No header**, **21 columns**, LF line endings. Column 14 is **empty** (so the reader must keep empty fields; a `Split` that drops them shifts all the indices).

| col | field | what it writes | read by the runner |
|---|---|---|---|
| 0-2 | x, y, z | frame position (3 decimals) | **yes** (dropped if not finite) |
| 3 | isStop | 1 only at the final point, if it ended stopped | **yes** |
| 4 | stopDuration | 6 at the end, otherwise 0 | no |
| 5 | stopRadius | 0 | no |
| 6 | targetSpeed | recorded km/h (already with the stops and the arrival ramp) | **yes** (`recKmh`) |
| 7 | targetGear | recorded gear | no |
| 8 / 9 | throttle / brake | 0, or the recorded value with `--keep-inputs` | only if col 12 = 1 |
| 10 | handbrake | 0 | yes |
| 11 | steering | 0 | no |
| 12 | hasInputData | 0, or 1 with `--keep-inputs` | gate for 8 and 18 |
| 13 | mode | `normal` or `reverse` (`parking` also exists, but the converter does not write it) | **yes** |
| 14 | name | **empty** | no |
| 15 | heading | degrees; **exactly 0.000 = "no data"** | **yes** |
| 16 / 17 | lights / horn | 0 | no |
| 18 | frontWheel | `front_wheel_deg` | only if col 12 = 1 |
| 19 | corridorHalfWidth | 0 | no |
| 20 | legBreak | 1 = last wp of a leg (direction change or manual break) | **yes** |

**`_cinta.csv`**, the tape: **one row per frame** of the take, with a header row. **It is what gets replayed.**

```
s,x,y,z,heading,chassis_kmh,front_wheel_deg,throttle,brake,gear,handbrake,dt
```

- `s` is the distance traveled, in m with 3 decimals. **It never decreases.**
- The runner does not read `handbrake` and `dt`.

**`BZBusRoute_<NOMBRE>.json`**, the hdr plus the waypoints in JSON. In DayZ it is no longer used to run, because the JSON reader took ~180 s with 5,522 waypoints. **[port]** Ignore it and read `_hdr.json` + `_wp.csv` + `_cinta.csv`.

#### E.5.7 What the converter does inside (to understand what comes out, not to rewrite it)

1. **Rewinds.** If the take goes back more than 2 m along its own line (at over 15 km/h, with less than 1 m of lateral offset) and passes the point again in less than 3 s, that is the server correcting the client. The repeated rows are dropped, without leaving a gap.
2. **Polyline resample.** A wp is written every `clamp(v·0.15 s, 0.5 m, 2 m)`. Distance only accumulates at ≥ 0.6 km/h. The marked rows, the first one and the last one are always written.
3. **Stops.** A halt (< 0.6 km/h for ≥ 0.4 s) **with a mark** leaves a wp with `targetSpeed = 0`. Without a mark it is a pause: it is crossed without stopping.
4. **Direction.** A wp is reverse if `gear == 0`, or if the motion goes against the heading (`dot < −0.30` at ≤ 15 km/h). Where the direction changes, the last wp of the outgoing leg has `legBreak = 1`.
5. **End.** If the take ends at ≤ 1 km/h, the last wp is `isStop = 1`. Before each stop or direction change there is an **arrival ramp**: `v ≤ √(2·decel·d)` with `decel = max(0.35, 1.40 + 9.8·slope)`, over the last 25 m.
6. **Backtrack cleanup.** A wp that goes back ≥ 2 m against the recorded heading of the previous one is dropped.
7. **Tape.** Two filters are applied:
   - **glitch:** a jump larger than `max(2 m, v·12·dt)` that comes back in less than 3 s is dropped;
   - **rewinds:** the same ones as in step 1.

   Then `s` = distance between "anchors" (rows more than 6 cm apart), spread within each stretch according to `v·dt`. The recorded position comes quantized: it stays still for several frames and then jumps.
8. **Warnings, without fixing anything.** Spins (more than 90° in 2 s, while rolling) and halts without a mark. **The take is not edited**: it is recorded again.

#### E.5.8 The envelope (optional): what the take measured of the vehicle

`envelope_emit.py` writes `envelope_<Clase>.csv`. It has one line per measurement: `scalars`, `stx2` (command → angle), `shist`, `chist2`/`nocreep` (creep), `lhist` (launch), `ahist` (full-throttle acceleration), `coast2` (deceleration with no pedals), `k` (understeer per speed band) and `medido`.

- The bins that the take did not touch stay **empty on purpose**: it is the honest map of what was measured.
- A new take of the same vehicle **adds** bins if the fingerprint (mass, gears, maximum angle) did not change. If it changed, it exits with code 3 and touches nothing.
- **[port]** In 7DTD almost all of this **can be read** from the `Vehicle` (E.1). Start without an envelope and use the direct readings. The envelope is worth it later, to measure what the game does not declare: real braking, deceleration with no pedals, and understeer.

#### E.5.9 The runner side: how it loads

- **`_hdr.json`:** with the JSON reader.
- **`_wp.csv`:** **line by line**, splitting each line with a split that **keeps the empty fields**. Fewer than 14 fields = line dropped.
  - `pos` = columns 0-2.
  - `isStop` = (col 3 == 1).
  - `recKmh` = col 6.
  - `isRev` = (col 13 == `reverse`).
  - `headRad` = col 15 in radians (exactly 0 = no data).
  - `legBreak` = (col 20 == 1).
- **`_cinta.csv`:** line by line. The first line (header) is skipped and ≥ 12 fields are required. Columns 0-9 are read. The tape is valid with more than 10 rows.
- **Without `_cinta.csv`, the route still runs**, in reactive mode (pure pursuit on the polyline): it is much less faithful.
- **Load over many server frames.** A tape of 17,000 rows processed in a single server frame froze the DayZ server for 1.84 s. A global FIFO queue is used with a budget of 5 ms per frame, and each step yields at ~1 ms.
  - **[port]** A coroutine per `Update`, with a `Stopwatch`.
  - Measure the **worst** frame, not the average.

### E.6 — The brain, piece by piece (portable specification)

This is what the DayZ 1.2 brain does, written so you can reimplement it in C#. It comes from reading the source code, which is in the repository under `scripts/4_World/bz12drive/`. **If something here does not add up for you, the code is the truth**: each subsection names its files.

Each rule comes with its reason (a "scar"): the measured case that made it necessary. **Do not remove a rule because it looks unnecessary** before you understand its scar. Do not copy it blindly either: many scars come from DayZ (clutch, gearbox, a speedometer that lies at launch) and may not appear in 7DTD. The rule of E.0 applies: if a rule does not apply, you prove it with a measurement.

#### E.6.0 The map: two clocks, one order

- **`Plan()` at 2 Hz** (dt = 0.5 s). Route cursor, mode, target speed, driving modules, slow detectors and the recovery ladder.
- **`Sample()` at 40 Hz** (dt ≈ 0.025 s). The tape, the steering refresh, the per-frame final brake, the entry into the endpoint, and the fast detectors (crash, rolling back, rest).
- Both write **the same** output object (`throttle 0..1`, `brake 0..1`, `steering −1..1`, `handbrake`, `gear`), and the runner applies it after each call. When the tape "possesses", Sample overwrites at 40 Hz what Plan decided.
- **[port]** Sample in a `Postfix` or `Prefix` of the vehicle's `PhysicsFixedUpdate`; Plan every 20 Sample calls (or with a time accumulator). Every threshold in "ticks" is in Plan ticks: 20 ticks = 10 s.

**The best order to port it** (it matches the milestones of E.4):
1. **Tape + cursor** (E.6.1). With the tape's steering and the tape's pedals it already drives: that is H4.
2. **Legs and arbiter** (E.6.3) when the take has stops and direction changes: H5.
3. **Reactive control** (E.6.2): for when the tape does not possess (launch, it got lost, final arrival).
4. **AutoRecovery** (E.6.4): H6.

#### E.6.1 The tape: cursor by distance, possession and recorded stops

Files: `BZCintaRunner.c` and the "tape" part of `BZDriveController.c`.

**What it is.** The `_cinta.csv` in memory (parallel arrays `S, X, Y, Z, H, Kmh, Fw, Thr, Brk, Gear`) and a **cursor** that says "at which point of the take the vehicle is". On each Sample it reads what the human had **at that point of the road**: achieved wheel angle, throttle, brake and gear. **Linear interpolation by arc length, zero smoothing: the texture of the steering is signal.**

**Precomputation at load time:**
- **Phase segments.** They are contiguous blocks with `gear == 0` (reverse) or `gear != 0` (forward).
  - A block is a phase segment if it spans **≥ 3 m** of `s` **and** its phase differs from that of the previous phase segment.
  - Micro-blocks (one frame in R at spawn, a one-frame change) are absorbed. Seeding against them sent the cursor 130 m away.
  - Per phase segment it stores `TramoS[k]`, `TramoIdx[k]` and `TramoRev[k]`, and per frame `FrameTramo[i]`.
- **[port]** Load spread over server frames (E.5.9). A row counter that lives in the slice, and not in the object, loses one row per slice.

**The cursor (`Muestra(pos, out res, esperaRev)`)**, on each Sample in which the tape possesses. `esperaRev` is **the direction of the route leg**, not the recorded gear, which lags one frame at each direction change.

```
// 1. ODOMETRY (unsigned: going backwards also adds)
av = |pos.xz - prevQ.xz|;  if av < 8 m: sEst += av  else: prevQOk = false (teleport ⇒ recovery)
// 2. PHASE CLAMP: the cursor does not pass the end of the phase segment of ITS phase; only the leg change crosses the boundary
tr = phase segment of sEst, stepped back to one with TramoRev == esperaRev
if there is a next phase segment: finFase = TramoS[tr+1] - 0.05; if sEst > finFase: sEst = finFase; clampeoBorde = true
// 3. the cursor walks the arc (monotonic): while S[cursor+1] <= sEst: cursor++
// 4. PLATEAU (a recorded stop; see below)
// 5. ANCHOR WINDOW: normal [cursor-3, cursor+6], minimum step 0.25 m
//    recovery (after a teleport or 12 ticks far away): ±(12 m × (1 + fallos)), cap 60 m, minimum step 0.10 m
// 6. TOURNAMENT: for each segment j of the window
//      skip if FrameTramo[j] < ultTramoOk (phase segment already closed: FORBIDDEN, not penalized)
//      skip if FrameTramo[j] > tr (the future is crossed by the leg)
//      skip if (Gear[j]==0) != esperaRev
//      skip if |seg| < pasoMínimo (dense knot: quantized position)
//      project: t ∈ [0,1], d² = distance²;  skip if t is at the edge (≤0.02 or ≥0.98) and d² ≥ 0.09
//      costo = d² + ((S[j] - sEst)/5)²       // λ = 5 m: position rules, but a foreign pass 60 m away does not win
// 7. ACCEPT the best one if it is interior (0.02<t<0.98) or is < 0.3 m away,
//    AND if it is not farther than the cursor's point + 0.5 m ("the anchor only brings closer")
//    ⇒ cursor = j; sEst = S[j] + t·(S[j+1]-S[j]);  else: pure odometry
// 8. ticksLejos (distM > 1.5 m); phase segment consumption: if distM ≤ 1.5: ultTramoOk = max(ultTramoOk, tramo(sEst))
// 9. INTERPOLATE row cursor..cursor+1 with t = (sEst - S[c])/(S[c+1]-S[c]): fwDeg, thr, brk, kmhRec, heading (along the short arc)
//    latM = (pos.x-q.x)·cos h - (pos.z-q.z)·sin h      // + = vehicle to the RIGHT of the take
// 10. LOST: lon = √(distM² - latM²) > 6 m  or  |latM| > 12 m  ⇒ return false (the reactive control drives that tick)
// 11. EDGE EXHAUSTED: clamped for 20 ticks and distM > max(1 m, distM_al_clampear + 0.3) ⇒ false
// 12. gear = that of the nearest frame; if the phase read is not the expected one: ticksLejos++
```

Why it is hybrid (odometry + anchors):
- The recorded position is quantized at ~10-16 Hz: 8 frames at the same point while the wheel sweeps 2°/frame. Indexing only by position gave 14° of wheel error.
- A raw projection in dense zones doubled the steering reversals.
- An anchor at the end of a segment (t = 1) pulled `sEst` back on every query and froze the cursor.

**External re-anchors** (all three clear the plateau in progress):
- **`SembrarFase(esperaRev, pos)`**: the controller fires it when `leg.isReverse` changes, on every tick, even if the tape does not possess.
  - It jumps to the first phase segment `kt > ultTramoOk` of that phase: `sEst = TramoS[kt]`, `cursor = TramoIdx[kt]`, `ultTramoOk = kt`.
  - It sets `prevQOk = false`, so the first query does not add old odometry.
  - If there is no next phase segment of that phase, it looks for the frame of that phase nearest to `pos`.
- **`AnclarEnS(s)`**: for the recovery's relocation (E.6.4).
- A tape with no phase segments of that phase is only logged.

**Possession: the edge-triggered, per-leg latch.** The tape **is in control** on a Sample if all of these conditions hold:
- the tape is valid;
- the leg is valid;
- `rodo == true`;
- the mode is CRUISE, APPROACH or MANEUVER, or `cintaVolanteSolo` holds;
- `Muestra` returned true.

`cintaVolanteSolo` = ENDPOINT on a reverse leg with v ≥ 2 km/h: the tape sets only the steering and the endpoint does the closing. In a forward ENDPOINT the tape does not possess.

```
on leg change:               rodo = false; quieto = false; rearmaDist = -1
if v < 2 km/h:               quieto = true
ARM if !rodo && quieto && v ≥ 2 km/h && no recovery in progress
      && (rearmaDist < 0 || straight-line distance from where it was released ≥ rearmaDist)
```

- **Edge-triggered:** the vehicle must have been seen still in THIS leg and then roll. The leg change arrives at 2 Hz with the vehicle still rolling out what was left of the previous leg (1.1-1.5 km/h). A level-triggered arm re-armed on the spot, seeded wrong, and the cursor stayed off the tape forever.
- **The start from standstill is done by LAUNCH** (the throttle ramp of E.6.2), never by the tape. The take starts with ~70 frames of the human sitting with throttle 0: the tape alone would never start moving.
- **No tape, no possession.** Every reference to the tape needs a guard. In DayZ, without that guard, the plateau engagement read a null tape 40 times per second.

**Plateaus: the recorded stops.** A plateau is a run of rows with `Kmh < 1 km/h` inside the same phase segment. It is a **timed event with an exit**: "I stopped here for this long" is replayed as it is, and then the vehicle goes on.
- **Enter** (when the cursor lands on a slow row that has not been served): extend the run backwards (without passing `ultFin`) and forwards, set `cursor = ini` and `llamadas = 0`.
- **Serve:** each `Muestra` adds 1 and resets the cursor to the entry. It exits after `fin − ini + 1` calls, which is the recorded duration at 40 Hz. On exit, `ultFin = fin` and the cursor moves to `fin + 1` if that row is in the same phase segment.
- **A served plateau is not entered again** (`k > ultFin`). Otherwise the projection recovery would fall into it, because all its rows are at the same point, and the vehicle would stay pinned.
- **Why 1 km/h and not 0.3:** the criterion is "slower than the vehicle can start off at". A bus got pinned on a row recorded at 0.02 km/h.
- **Holding the stop:** during the plateau, throttle 0 and service brake 1.0, never the handbrake. The human barely braked when stopping, and on a +2.3° slope the row had throttle 0 and brake 0: the vehicle rolled backwards.
  - ⚠ **[port]** Apply the holding brake **in the fast loop, after writing the tape's pedals**. In DayZ the holding brake is in Plan (2 Hz), and the tape's pedal block in Sample (40 Hz) overwrites it with the recorded brake on the next frame. It is a known defect; do not copy it.
- **While there is a plateau**, the jam detectors are turned off (CLAVADO, no-progress jam, the logical recovery). **A recorded stop is not a jam**: the 22 ladders opened by CLAVADO in four days were all at recorded stops.

**Engaging the plateau where the vehicle stopped.** The cursor is spatial and the vehicle does not stop on the exact row. If it stopped 2.5 cm short, its row is the last one of the braking (brake 1) and the plateau is never reached: the cursor only advances if the vehicle advances. In DayZ, two buses stayed pinned for 22 s this way.

The first unserved plateau within 8 m ahead is engaged if **any** of these three things happens, in CRUISE mode and with no recovery or plateau in progress:
- **still:** `|v| < 0.3 km/h` for ≥ `WaitS` (0.5 s);
- **rolling back:** `fwdKmh < −2` for ≥ `WaitS`, outside a reverse leg or a maneuver;
- **pinned:** the tape's arc does not advance 1 cm for ≥ `WaitS`, with `|v| < 2 km/h`, and the take does not ask for throttle before the rest.

The **pinned** trigger exists because in DayZ the speedometer reads 0.06-0.5 km/h with the vehicle still, so "still" never added up to 0.5 s. With the tape released, it is also required that the take does not ask for throttle before the plateau.

**Releasing the tape until the end of the plateau** (when leaving a consumer hold, or as a logical recovery, E.6.4):

```
rodo = false; quieto = true; sueltaPos = pos
rearmaDist = (plateau ahead) ? FinMesetaAdelante() - sEst : -1   // measure BEFORE moving the cursor
if no plateau in progress: ConsumirMesetasServidas(pos)          // the ones behind the vehicle and the first one ahead
                                                                 // cursor ⇒ the vehicle's row; prevQ = pos
close the plateau in progress
```

- After that, LAUNCH starts the vehicle again and the tape re-arms when the vehicle has traveled `rearmaDist`.
- **Why consume:** during a hold the cursor stays **frozen**. On re-arming, the odometry left it on a rest row with the vehicle already moving, and the recorded curvature arrived ~1 m late. In DayZ the bus went 2.9 m wide and bogged down (5 jams in 12 versus 2 in 65).
- **It engages by nearness and releases by distance:** with a plateau in progress and the vehicle more than 8 m away from it, the tape is released. A car stayed 15.8 m behind its plateau for 90 s.

**Consumer hold (API).** The mod that uses the framework (a bus service, for example) can ask "stay here". The runner engages it when any of these conditions holds:
- `sEst ≥ plateau entry` (by arc: at 3 km/h a 2.8 cm plateau is skipped in one step);
- `|v| < 0.3 km/h`;
- there is no tape;
- the vehicle moved more than 8 m away from the requested point.

With the hold active, throttle 0, brake 1 and handbrake 1 are applied, and **Sample is not called**. `Resume()` sets the arbiter to IDLE and releases the tape until the end of the plateau.

**Steering and pedals of the tape:** see E.6.2 §1.6 and §2.5.

**Scars of the tape (one line each):**
- Spatial, not temporal.
- `front_wheel_deg`, not the key.
- Anchors only on segments ≥ 0.25 m.
- Filter out the unacceptable candidates **before** the tournament: otherwise an invalid one wins and the anchor stays null even when there was a valid one.
- Position + arc cost: two reverse passes over the same ground, 7 cm apart, made the cursor jump 60 m.
- Phase-segment monotonicity as a **prohibition**, not as a penalty.
- A phase segment is consumed by healthy tracking, not by an anchor: in slow reverses there is never an anchor.
- The seeding is triggered by the leg, not by an internal detector: the detector only saw 2 of 6 direction changes.
- Loss per axis: with 12 m of lateral offset the tape is still useful. Releasing it at 6 m total left the reactive control driving at 186 km/h with 5 m of error.
- The cursor recovery is stepped and by arc, not by frames (±60 frames are ±1.8 m in a slow zone).
- The odometry has no sign: rolling back needs its own detector.

#### E.6.2 — Lateral and longitudinal control (the steering and the pedals)

##### 0. Common frame

**Clocks.** There are two:
- `Plan()` at 2 Hz (dt 0.5 s). It handles the leg cursor, the mode, the target speed, the gear, the pedals and the steering target.
- `Sample()` at 40 Hz (dt 0.025 s). It handles the re-aimed steering, the slew, the per-frame endpoint brake, the endpoint entry and the tape.

The "tick" counters of the maneuver, the endpoint, the ramps and the arbiter count Plan ticks.

**Conventions** (verified in the code):
- Heading = `atan2(x, z)`; it grows clockwise when seen from above.
- Curvature + = turn to the RIGHT. Normalized steering + = RIGHT, in [-1, 1].
- `crossTrackM` + = vehicle to the LEFT of the line. On the tape, `latM` + = vehicle to the RIGHT (the sign is the opposite).
- In reverse, the steering sign is inverted: `s = -s`.
- Unity uses X right, Z forward and a clockwise heading from +Z, so it looks like the same convention. **Verify it with a measurement before you trust it.**

**Vehicle state:**
- `chassisKmh` = |velocity| of the chassis, unsigned.
- `fwdKmh` = forward component, signed.
- `kmh` = wheel speedometer. It is only used for `ku(v)` and `ApproachZone`.
- `courseRad` = heading of the velocity if v > 7 km/h. Otherwise, equal to the yaw.
- `pitch` > 0 = nose up.

**Leg view.** A leg is a stretch between cuts: a cusp or a stop.
- `remainM` = arc left to the end of the leg.
- `alongM` = arc traveled since the start of the leg.
- `signedToEndM` = straight projection on the tangent of the final pose. + = not there yet, − = already past it.
- `fwdM` = arc visible ahead in the window. The window covers 25 m behind and 80 m ahead.
- Flags: `endIsStop`, `endIsCusp`, `isReverse`, `nearWp`.

**Tolerances:**
- `LLEGADA_M = 0.10 m`: the strict pose bar, for maneuvers and reverse.
- `ACEPTA_ENDPOINT_M = 0.25 m`: the acceptance for a forward endpoint. It exists because the per-frame brake parks about 0.10 m before the point.
- `ACEPTA_ENDPOINT_RED_M = 1.0 m` (1004): safety net. An engaged slam that gives up between 0.25 and 1.0 m is **not** a give-up: `ROUTE_COMPLETE` (normal cycle, `RunnerRouteCompleted` true) with `[C12] LLEGADA fuera de la tolerancia estricta: <d> m`. `ROUTE_INCOMPLETE` is kept for > 1 m, a maneuver that gave up, or an abandoned leg. After the FIN nothing moves the index (`UpdateLegBounds` only before the FIN).
- Slam arming (1004, `BZModeArbiter.LlegoLaHoraDelSlamCon`): the fixed zone (the wheelbase) **or** `remM <= v²/(2·MaxBrakeDecel(surf))·(1 + MARGEN_FRENO)`, with `MARGEN_FRENO = 0.25`. On profile nothing changes (same frame). ⚠ The hdr's `EndpointBrakeDecel` is an **average** of the final phase, not a capability: it's no good for arming. ⚠ If a vehicle's `MaxBrakeDecel` is overestimated, the physics clause doesn't arm any earlier (seen with Hatchback_02_BanditKitty: armed at 2.56 m, arrived at 0.27 m, complete thanks to the net).

**Order per Plan tick:**
1. Update the leg.
2. Arbiter.
3. Module by mode:
   - MANEUVER → MC.
   - ENDPOINT → Slam.
   - CRUISE/APPROACH → PurePursuit + Derivation.
4. `AcotarVolante`: grip cap and countersteer.
5. Steering authority clamp.
6. Recorded pedal.
7. Traction cap.
8. NoSoltarHaciaElBog.
9. RampaDeArranque.
10. Detectors (they only report) and recoveries.
11. NoPlanearHastaElBog.
12. Handbrake = the recorded value of the wp. Nobody else writes it.
13. `targetSteer = out.steering`.

**Order per Sample tick:**
1. `remAhora`.
2. PromoverEndpoint.
3. Per-frame endpoint brake.
4. Gear-change window.
5. Drift.
6. RefrescarVolante.
7. SteerSlew.
8. Tape: if it possesses, it overrides steering, throttle, brake and gear.

**The actuator** writes throttle, steering, brake and handbrake, and shifts gear if it differs. It does nothing else.

**Production defaults:**
- The tape (spatial replay) is ACTIVE.
- The N2 FF-primary steering law is ACTIVE.
- When the tape does not possess, the reactive law of §1.3 and §1.4 is in command.

##### 1. Lateral control

###### 1.1 From curvature to steering (inverse bicycle model with measured understeer)

```
MaxCurvature(v)      = tan(steerMax) * max(ku(v),0.05) / wb
CurvaturaUtilMax(v)  = MaxCurvature(v) * (1 - 0.05)      // MARGEN_AUTORIDAD
SteerCommandSplit(kFF,kCorr,v): delta = atan(wb*(kFF/ku + kCorr/ku)); cmd = clamp(delta/steerMax,±1)
SteerCommandForCurvature(k,v):  delta = atan(wb*k/ku); cmd = CommandForAngle(delta)   // POSE modules
```

- `ku(v)` is the understeer factor measured in 5 km/h bins, with 20 bins from 0 to 100 km/h. The prior is 1.0 (neutral). The 0.90 of v1.1 is not used; the envelope measured it at ~0.50.
- `SteerCommandSplit` divides both terms by `ku`. So the split is only useful to freeze `kCorr` (see §1.5). It uses the linear relation `/steerMax`, not the measured transfer.
- Reason for the 0.05 authority margin: while the steering is against the stop, the loop is open. Keeping 5 % in reserve leaves the loop something to modulate with. It was measured that the peak error follows the longest saturation streak.

**`CommandForAngle(δ)`, with the measured transfer "stx2":**
- 10 buckets by |cmd|: `balde = floor(|cmd|·10)`.
- Each bucket averages the wheel angle executed in steady state. 20 samples per bucket are needed.
- It is only used if it is sane:
  - monotonic, with a tolerance of 0.004 rad;
  - the buckets with data are contiguous from 0;
  - at least 4 buckets.
- It is inverted by interpolating between bucket centers `(b+0.5)/10`. If more angle is requested than was measured, it returns 1.
- If it is not sane: `δ/steerMax`.
- Reason: a noisy table, once inverted, asked for half the command, and the lateral error went from 0.49 to 2.21 m.
- **In 7DTD this is simpler:** the engine does `wheelDir = moveStrafe · SteerAngleMax` (with `lastInputController = true`), so the transfer is linear and known.

**Replay of the recorded wheel** (`CommandForAngleReplay`):
- If the measured curve is sane, it uses it.
- If not, it uses the "plant": `cmd = deg / 85.1` (a constant measured in DayZ).
- Reason: in DayZ, input 1.0 is NOT equal to `steerMax`; with `/steerMax` (≈30°), the wheel executed about 2.8 times what was recorded.
- The closed-loop law still uses `/steerMax`, because it is a calibrated knob of the loop.
- **In 7DTD:** `cmd = deg / SteerAngleMax` (the plant IS `SteerAngleMax`). Verify it by measuring the real wheel angle against the command.

###### 1.2 Commandable curvature ceiling (`KMaxCmd`, computed on every Plan)

```
KMaxCmd = MaxCurvature(v)*0.95
if v>1 m/s and measured aDir>0.1:
    aDir = ALatDirEn(velocidadGrabadaEnLaMira)   // p95 of the take's a_lat in 20 km/h bands; never lower than the scalar
    aDir = max(aDir, 5.0)                        // ALAT_DIR_PISO: minimum physical grip to recover
    KMaxCmd = min(KMaxCmd, aDir / v²)
```

**How the take's `a_lat` is measured:**
- Per waypoint: `v²·|k|`, only with v ≥ 5 m/s.
- `k` is the curvature from a centered difference of ±1.5 m on the smoothed heading, with a ±1 clamp.
- `ALatToma` = p90, discarding values ≥ 40.
- `ALatDir` = p95, discarding values ≥ 100.
- 20 km/h bands: 13 bands, from 0 to 260 km/h.

**Rules:**
- What is measured can only TIGHTEN the ceiling. Without a measurement, nothing is invented.
- Reason: commanding more curvature than the tires can give makes the vehicle slide, not turn. There were cases of 5 to 11 times the grip.

###### 1.3 Pure pursuit (fallback path without N2, and always in reverse)

**Horizon Ld** (computed in Plan with `chassisKmh`):

```
ld       = v*0.6                                   // PREVIEW_S (it was 1.2 when the aim point ran at 2 Hz)
floor    = max(1.5*wb  (3.0 m if there is no wb),  v*0.30)  // LD_WB_MULT=1.5 · PREVIEW_MIN_S=0.30
kPeor    = max_j |WrapPi(h(d0+base)-h(d0))|/base, d0 = 0, base, 2base… ≤ ld, max 16 steps, base = VentanaFF
techo    = (kPeor<0.0005) ? ld : sqrt(8*0.5/kPeor)  // corner cut eps≈Ld²k/8, CORTE_MAX_M=0.5
if v ≥ 60 km/h: pisoEstab = (kPeor<0.0005)? ld : min(ld, sqrt(8*1.65/kPeor)); floor = max(floor,pisoEstab)
ld = min(ld, techo); ld = max(ld, floor); if fwdM>0.05: ld = min(ld, fwdM)
```

- `VentanaFF` = min(wb, 3.0 m), or 3.0 if there is no wb.
- Corner-cut constants: `CORTE_ALTA_M = 1.65` and `CORTE_ALTA_KMH = 60`.
- First the ceiling, then the floor. The other way round, the ceiling could leave Ld below the floor.
- `kPeor` is the WORST curvature in the window, not the average.
- Reason for the time-based floor, with the linearized model:
  - `s² + (2v/Ld)s + 2v²/Ld² = 0` always gives ζ = 0.707.
  - What destabilizes it is the delay: `margen = 65° − deg(v√2/Ld·T)`.
  - 0.30 s leaves a margin proportional to the speed.
- 1.5 wb is the measured midpoint between a floor of 2.0 wb, which crossed lanes, and 1.2 wb, which hunted.

**Aim point and law:**

```
aim  = SampleAt(ld)            // BARE sample, no bias
pref = pos;  if reverse: pref = pos - wb*(sin yaw, cos yaw)     // leading axle
refYaw = yaw (+π in reverse)
dist = |aim - pref|; if dist<0.10 → k=0
alpha = WrapPi(atan2(aim-pref) - refYaw)
den = max(ld, dist)            // in Plan
k = clamp(2*sin(alpha)/den + ffDeficit, ±KMaxCmd)
```

**Aim-point refresh at 40 Hz** (`RefrescarMiraA40`):
- `avance = |pos − posDelPlan|`.
- If 0.05 < avance ≤ 40 m and `avance + ld ≤ fwdM`, then `aim = SampleAt(avance + ld)`.
- The 40 Hz curvature uses `2·sin(α)/dist + ffDeficit`, with the same clamp. If the refresh did not apply, the Plan's aim point stays.
- Ld is not recomputed at 40 Hz.

**FF probe (deficit), only without N2:**
- `ffDeficit = kLocal − kArco`.
  - `kArco = Δheading[0, ld] / ld`.
  - `kLocal = Δheading[0, VentanaFF] / VentanaFF`.
- It is set to 0 if any of these happens:
  - the cursor did not move since the previous Plan (same `nearWp`);
  - both |k| are below 0.006 (a straight; this is the angular noise floor);
  - `endIsStop` and `remainM < 4·wb`, because the endpoint is in command.
- Only the deficit is added, not the whole `kLocal`. On an arc of constant radius it is exactly 0.

⚠ **Difference from the pure pursuit of the 7DTD proof of concept (E.3):** that one shortens Ld in curves and shifts the aim point outward (sagitta compensation). In DayZ **both things were removed because of measured regressions** (see point 6 of this section, "Scars": shortening Ld in a curve raises the gain and diverges; shifting the aim point steals α from the loop). It measured well on a circle of constant radius, but for a real take, port the version above.

###### 1.4 N2 "FF-primary" (on by default, forward only)

It replaces the pursuit. Plan runs it with `avance = 0`, and every Sample runs it with the fresh advance. If it is not valid, it falls back to pure pursuit.

Important: the feed-forward is the **recorded curvature of the trajectory**, computed from the recorded heading. It is not the recorded wheel angle; that one belongs to the tape (§1.6).

```
V   = VentanaFF (min(wb,3.0))
# position/heading reference: tangent of POSITIONS at ±1.5 m (FFP_TAN_M)
p1 = SampleAt(max(0,a-1.5)).pos ; p2 = SampleAt(min(fwdM,a+1.5)).pos ; (a2-a1 ≥ 0.5)
hseg = atan2(p2-p1);  u = (sin hseg, cos hseg)
dr = wb/2                                      // REAR AXLE: vehicle and reference
rv = pos - dr*(sin yaw, cos yaw);  rr = S(a).pos - dr*(sin hRec, cos hRec)
y   = ux*(rv.z-rr.z) - uz*(rv.x-rr.x)         // + = left
psi = WrapPi(hseg - yaw)                       // yaw := courseRad (heading of the velocity)
vf  = max(v, 3.0)                              // FFP_VMIN_MS
lead = vf*0.06                                 // FFP_LEAD_S ≈ actuator delay
a0 = max(0, a+lead-V/2); a1 = a0+V (if a1>fwdM: a1=fwdM, a0=max(0,a1-V)); a1-a0 ≥ 0.5
kFF = WrapPi(hRec(a1)-hRec(a0)) / (a1-a0)      // per WINDOW, never the per-wp k
KY = ω²/vf²,  KPSI = 2ζω/vf,  ω=1.4, ζ=0.9
db(x,e) = x*|x|/(|x|+e)                        // C1 dead zone, no phase lag; e_y=0.05 m, e_psi=0.00524 rad (0.3°)
corr = KY*db(y,0.05) + KPSI*db(psi,0.00524) + trim
# caps PER COMPONENT
corr = clamp(corr, ±MaxCurvature(v)*0.95*(1-|volanteAplicadoAnterior|))   // wheel headroom
kFF  = clamp(kFF, ±KMaxCmd)                                                 // FF ≤ what the take proved
if v>1: corr = clamp(corr, ±10/v²)                                          // CORR_ALAT_RECUP = 10 m/s²
k = clamp(kFF+corr, ±MaxCurvature(v)*0.95)                                   // hard safety net: the wheels
```

**Integral trim** (only in the 40 Hz refresh):
- If the law was valid and no cap engaged:
  `trim += (0.77/vf²)·db(y)·dt·(1 − |volAplicado|)`, with a ±0.010 clamp.
- In any other case: `trim *= 0.98` per 40 Hz tick (reverse, invalid law, or N2 off).
- Reason: KI is in units of a_lat and is divided by v². That way, the loop gain does not change with speed. With a constant KI, at 230 km/h it caused a spin.
- The trim absorbs the irreducible FF error of ±0.005 1/m.

**Reasons for the law:**
- `y` and `ψ` are measured against the tangent of POSITIONS, not against the recorded heading. The human's heading differs from the tangent by up to 9° (p90) in curves: that is its drift.
- `ψ` is measured against the heading of the velocity (`courseRad`), so the vehicle's own drift does not enter the corrector either.
- The rear axle absorbs the off-tracking of a long vehicle.
- The headroom cap silences the corrector when the wheel is at its stop, and lets it act strongly when leaving the curve.

###### 1.5 Steering output layer (shared by pursuit and N2)

- **Freeze the correction during a gear change** (DayZ; 7DTD has no gearbox, so you can skip it).
  - The window opens when the commanded gear changes and lasts `FRZ_MAX_S = 0.80 s`: that is the p90 of 23 human shifts.
  - It freezes (kCorr = 0) only while the clutch is in transition: `|ratioInstantáneo − nominal| > 0.15·step to the neighboring gear`.
  - It freezes to zero. Holding the old value made it worse.
- **Grip angle cap** (pursuit only): `strGrip = atan(wb·KMaxCmd / clamp(ku, 0.05, 1)) / steerMax`, and `cmd = clamp(cmd, ±strGrip)`.
  - Reason: within the grip, the understeer compensation is correct. At the limit, the extra angle is drift.
- **Countersteer** (pursuit only, forward, v ≥ 60 km/h):
  - `deriva = heading of the 40 Hz displacement − yaw`. It is only computed if the step is larger than 0.05 m and v ≥ 20 km/h.
  - If 10° < |deriva| < 90°: `cmd = clamp(deriva/steerMax, ±1)`.
  - It is a geometric identity: no ramp, no grip cap, no timer. Past 90° it returns the base command, because there is no "forward" any more.
- **Low-speed authority** (all modes, in Plan and in the refresh):
  `aut = 0.35 + 0.65·clamp(v / escala, 0, 1)`, with `escala = idleCreepKmh` if it is greater than 0.5, or 4.0 km/h otherwise. Then `cmd = clamp(cmd, ±aut)`.
  - Reason: with the vehicle almost still, turned wheels only plow and misalign it.
- **The 40 Hz refresh does not act** below `max(1.0, idleCreepKmh)` km/h, because it would inject instead of correct. It does not act in the pose modules either (maneuver, endpoint).
- **Slew:** `tasa = min(3.0, 1.396 rad/s / steerMax)` in normalized units per second. `applied += clamp(target − applied, ±tasa·dt)`.
  - 1.396 rad/s = 80°/s, the measured p90 of the human steering rack across the whole fleet.
  - In 7DTD the engine already applies `SteerRate` to `MoveTowards`: measure how much, and do not double the slew.

###### 1.6 The tape: replay of the recorded steering + "magnet" (in command when it possesses)

**What it is.** A spatial player: the index is the ARC, not the time. On every 40 Hz tick it takes, interpolating linearly by arc, what the human had at THAT point: achieved wheel angle (`fwDeg`), throttle, brake, gear and recorded speed. It smooths nothing: the texture is signal.

**Loss.** The tape is considered lost if the cursor's longitudinal residual exceeds 6 m or the lateral one exceeds 12 m. In that case the reactive path is in command.

**Possession.** It is a per-leg latch:
- It arms on an edge: the vehicle must have been seen below 2 km/h in THIS leg and then go above 2 km/h, with no recovery in progress.
- It disarms when the leg changes. Pulling away from standstill always belongs to LAUNCH.
- It possesses in CRUISE, APPROACH and MANEUVER.
- In a reverse ENDPOINT it only takes the steering, and only while v ≥ 2 km/h. The endpoint closes the last stretch.

**Steering:**

```
psi  = WrapPi(yaw - hRec) [deg];  sPsi = (reverse? -1 : +1)
vF   = clamp((v-120)/(220-120),0,1)
C1 = 3.0 deg/m; C2 = 0.40 + (2.0-0.40)*vF (reverse with |fwRec|<3°: 1.0); cap = 4.5 + (12-4.5)*vF
ref  = ±0.7 m (inward) if there is a long lock ≤20 m ahead (≥28°, sustained ≥24°, length ≥8 m); forward only
iman = -(C1*(latM-ref) + sPsi*C2*psi)
asymmetric cap: if fwRec ≥ 20°, the side that OPENS can reach -12°; symmetric for fwRec ≤ -20°
steer = CommandForAngleReplay((fwRec + clamp(iman)) in rad)
```

The magnet is a trim: it was measured at 2.3 % of the command. Two proportional laws and one integral law meant to close the small, persistent error failed (see point 6).

##### 2. Longitudinal control

###### 2.1 Target speed ("the eye", in Plan)

```
decel = max(MaxBrakeDecel(piso), 0.5)
for i in nearWp..winHi:
    d = arc(cursor→i); vj = TargetAt(i)
    best = min(best, sqrt(vj² + 2*decel*d))      // anticipates: the MINIMUM rules
objetivo = best
halt if remainM ≤ 0.05 and fwdM ≤ 0.05
```

**`TargetAt(wp)`:**

```
vj = recKmh/3.6
if vj < 0.5 m/s:                       // REC_USABLE_MS
   isStop and not served → 0 ("STOP")
   isStop served         → curve limit      // a STOP already served after a hold does not ask for 0
   tight                 → creepKmh/3.6
   else                  → curve limit (aLat = decel)
else:
   v = PisoDeMordida(vj)                        // only on the final ramp
   aLat = max(decel, ALatToma)                  // the take can only RAISE the budget
   v = min(v, sqrt(aLat/|k_wp|))                // the curve is ALWAYS a ceiling; |k|<0.001 → no ceiling
```

**Final ramp and bite floor** (DayZ: it makes sense with a clutch; in 7DTD you can probably skip it):
- The vehicle is on the final ramp if `endIsStop` or `endIsCusp`, and all the wps from `winHi` to the cursor (except the isStop ones) have `rec < mordida` for the gear of that segment, and the bite speed is greater than 0.5 km/h.
- On the final ramp, a wp with `rec < mordida` is raised to the bite speed if `rec ≥ mordida / 1.10` and `remainM > d* = vMord² / (2·decel)`. Past that point, the braking curve is in command.
- Reason: below the bite speed the engine does not pull, and the steering loses effect.

###### 2.2 From target to pedals (DerivationEngine)

```
aFree = -CoastDecel(v,piso) - 9.81*sin(pitch)     // what the vehicle does ON ITS OWN (measured)
err = objetivo - v;  aReq = (|err|>0.25) ? err/1.0 : 0       // TAU 1 s, dead zone 0.25 m/s
aReq = max(aReq, -decelObjetivo, -MaxBrakeDecel)
if aReq > aFree: throttle = ThrottleForAccel(aReq - aFree), brake 0
else:            brake = clamp((aFree - aReq)/MaxBrakeDecel, 0,1)   (if MaxBrake<0.5: clamp(aNeed))
```

- The dead zone means "hold", not "release". It only cancels the error term.

**`ThrottleForAccel(aNeed)`** (torque model):

```
if aNeed ≤ 0 → 0
ratio = RatioEngagedNow (rpm·2π/60 ÷ v/r, only if v≥2.5 m/s, rpm≥0.5·idle, 0.5<ratio<60) or RatioForGear(gear)
rpm = rpmReal (if ≤0.5·idle: geometric rpm; minimum idle)
thr = (m*aNeed*r) / (ratio*0.85) / TorqueAt(rpm)          // DRIVETRAIN_EFF 0.85
if it does not resolve (no torque/gearbox; or FALLBACK gearbox with measured aMax): thr = aNeed/aMax, or launchFloor if there is no aMax
if 0.01<thr<launchFloor and v<creepKmh: thr = launchFloor
clamp 0..1
```

**In 7DTD:** there is no torque curve or gearbox exposed as in DayZ, but `Vehicle` declares `MotorTorqueForward/Backward` and `VelocityMaxForward/Backward`. Start with the degraded form `thr = aNeed / aMax` (with a measured `aMax`: accelerate at full throttle from standstill on flat ground and log the acceleration) and a measured launch floor. `moveForward` is continuous (−1..1), so the throttle is applied directly.

###### 2.3 Gears (DayZ-specific)

**Indices:** 0 = reverse, 1 = neutral, 2 = first. **Selection** with the CURRENT speed (never the target), a debounce of 3 Plan ticks, and an immediate downshift if the vehicle loses speed at full throttle. **7DTD has no gearbox:** the direction of travel is the sign of `moveForward`. Only use the take's `gear` column to know whether a segment is in reverse (`gear == 0`).

###### 2.4 Cross-cutting throttle rules (in Plan, after the module)

- **Recorded pedal** (pursuit only): if the target is greater than v, `thr = max(thr, gasGrabadoAquí)`. It helps to reach the target, never to overshoot it.
- **Traction cap:** `thr ≤ ruedasEnPiso / ruedas`, with a minimum of 0.15. Without this cap, the wheel spins at full throttle in the air. (In 7DTD: `GetWheelsOnGround()`.)
- **NoSoltarHaciaElBog** (DayZ, anti-bog): it looks at the recorded minimum within `max(3 m, wb)` ahead and does not release the throttle too early if the vehicle is going to drop below the bite speed.
- **NoPlanearHastaElBog:** if throttle and brake ≤ 0.02 and `v ≤ 0.5·recAquí`, `thr = launchFloor`, brake 0.
- **LAUNCH ramp** (any mode; it only LIMITS):
  - It arms if `v < max(0.5, idleCreepKmh)` (the MEASURED creep, not the target); the vehicle is not falling (`aObs < 0` and |v| > 0.3 km/h are not both true); throttle > 0.01; brake ≤ 0.01; and it is not on a recorded plateau (a recorded stop in the tape).
  - `rampa = piso + n·0.10`, with `n` in Plan ticks. `thr = min(thr, rampa)`. `pasos = floor((1 − piso)/0.10)` (8 with a floor of 0.13). When it runs out, it hands back control without giving a verdict.
  - Reason: asking for 1.0 all at once pins the engine below the bite speed. A ramp lets the engine gain revs before loading it.

###### 2.5 Tape longitudinal (40 Hz, when it possesses)

```
kmhRecEff = kmhRec
if endIsStop: aClav = measured braking of the take (or MaxBrakeDecel); vClav = 3.6*sqrt(2*aClav*remain)
    if vClav < kmhRec ≤ vClav + max(5, 2.5*aClav): kmhRecEff = vClav; topeFreno = 1.0
dv = kmhRecEff - v
thr = clamp(thrRec + clamp(0.08*dv, -1, 0.5), 0,1)
if thr≈0 and -dv > 1.0: frn = min(0.08*(-dv-1.0), topeFreno=0.35)
brk = clamp(brkRec + frn, 0,1);  gear = gearRec
pedal deadlock: if v<2 and kmhRecEff≥2 → (remGate ≤ 0.10 ? thr0/brk1 : brk=0)
```

There is also an anticipated brake at a break into a descent (v ≥ 60 km/h, slope ≤ −5°, it looks 1 s ahead, 2 km/h margin), and recorded plateaus (v < 1 km/h) that are replayed as temporary stops (see E.6.1).

##### 3. Maneuvers and direction changes (cusps)

**Arbiter** (in Plan):
- Minimum dwell of 2 ticks, except when entering ENDPOINT. ENDPOINT is absorbing.
- **CRUISE → MANEUVER** if a reverse leg "needs a maneuver" (`remainM < 1.5·wb`, ×1.5 on exit; or the worst curvature within 1.5·wb ahead exceeds `CurvaturaUtilMax`, sampled at 6 points with base VentanaFF without crossing the end of the leg), or if the leg does not end at a stop and `remainM ≤ 3·wb` (cusp or leg cut).
- **ENDPOINT entry:** `endIsStop` and `remain ≤ max(wb, 2.0)`; or `endIsStop`, `remain ≤ 2·wb` and `v < max(creep, 1)`.
- **APPROACH entry:** `endIsStop` and `remain ≤ max(v²/(2a) + 0.5·v, zonaEndpoint)`.
- **MANEUVER exit on a forward leg:** it stays if it is the same leg of the chain, if `alongM < 3·wb·1.5`, or if v < creep. Last-resort exit: the end is not near and it has been in maneuver for more than 20 ticks.
- **Stall:** 40 ticks (20 s) with less than 0.3 m of arc → SAFE_STOP (see E.6.4: it opens the recovery ladder).

**ManeuverController** (direct output):

```
CreepZone = 1.5*wb (default wb 2.5); StoppedKmh = max(0.3, creepKmh*0.5)
signed = signedToEndM;  if remainM > CreepZone: signed = remainM   // far away, the projection means nothing
progress: dist=|signed|; improvement >0.02 m → counter=0, ramp restarts; else: stopped → +1, moving → −1 (drains)
Closed = v<Stopped and ((signed ≤ 0.10 and remainM ≤ CreepZone) or sinProgreso > 20)
   → HOLD (brake 1), LegDone; GaveUp if signed > 0.10
stopped: pitch > 0.03 rad → HILLSTART, else LAUNCH;  rolling: signed < CreepZone → CREEP, else TRACK
LAUNCH:    if signed ≤ 0 → brake 1 (never push to correct an overshoot); thr = launchFloor + n*0.10
HILLSTART: same ramp; brake 1 while thr < clamp(g·sinθ/aMax, 0.1, 1.0) and n ≤ 12 (anti-deadlock exit)
TRACK/CREEP:
   d = max(0, signed-0.10); vT = sqrt(2*max(aBrake,0.5)*d)
   lead = min(v*0.25, wb); if there is recKmh>0.1 within lead: vT = min(vT, rec)   // the human's profile at the same distance
   if there is no recording and the phase is CREEP: vT = min(vT, creep)
   tau = 1.0; if err<0 and signed < 2*CreepZone: tau = 0.25     // braking late costs precision
   aNeed = err/tau - aFree → throttle via ThrottleForAccel, or brake = -aNeed/max(aBrake,0.5)
steering: L = clamp(|signedToEnd|, 2*wb, 15 (only if 15 > 2wb))
   hErr = WrapPi(rumboGrabado 1 wb ahead − yaw)
   k = hErr/L + 2*crossTrack/L²;  s = SteerCommandForCurvature(k); reverse: -s
```

**"The tape nailed the cusp" closure** (in addition to `IsLegDone`): the leg is considered done if the tape possesses, the vehicle has been stopped (|v| < 0.3 km/h) for at least `WaitS` (0.5 s), the brake of the recorded row is greater than 0.5, and `remainM ≤ CreepZone`. Reason: of 63 measured exits, 19 ended short by 0.11-0.35 m; the maneuver asked for throttle against the recorded brake until it gave up after 11 s. The human's stop had already been replayed.

If the leg ends at a semantic stop, ARRIVED is emitted and the controller waits for `Resume`. Otherwise, it moves on to the next leg and resets the maneuver and the endpoint.

##### 4. Arrival at the end (EndpointSlam)

**Entry at 40 Hz:** `remAhora = remainM − max(0, remFrAlPlan − remFrAhora)`, where `remFr` is the projection on the final tangent. It is monotonic: it can only bring the entry forward. Do not use `min(arc, projection)`: in a parking lot the projection is ~0 far from the end. Once in ENDPOINT, every frame applies the forward brake: `d = remFr − 0.10`; if d ≤ 0.01, brake 1; otherwise `brake = max(brake, (v²/2d)/aMax)`.

**Slam** (in Plan):

```
dist = hypot(signed, crossTrack)                       // 2D, not only along
progress: improvement >0.02 → acercando=true, counter=0; else: stopped → +1, moving → −1
LATCH (brake 1, steering 0, irreversible) if:
   (dist ≤ 0.25 and v < StoppedKmh and !acercando)   or   signed < -0.40 (4×LLEGADA)
   or dist > minDist + 0.10                          or   sinProgreso > 6 (3 s)
GaveUp if dist > 0.25 (the real number is reported)
LONGITUDINAL (d = signed, the target is the point, not the edge of the tolerance):
   d ≤ 0.01 → brake 1
   aWant = -v²/(2d); aNeed = aWant - aFree
   frenoRuido = v<mordida and -aNeed < 0.5*|aFree| ;  detenidoCorto = v < 0.3 m/s
   aNeed < -0.05 and !frenoRuido and !detenidoCorto → brake = -aNeed/max(aBrake,0.5)
   v < creep*0.5 → CARRY: thr = min(1.0, launchFloor + nCarry*0.05)
   aNeed > 0.02 → SOSTIENE: thr = ThrottleForAccel(aNeed)
   else → coast (0,0)
STEERING: L = clamp(|signed|, 2*wb, 8 (only if 8>2wb)); hErr = rumboGrabadoFinal − yaw
   kLat = clamp(2*crossTrack/max(|signed|,wb)², ±0.12) * clamp((|signed|-0.5)/(1.5-0.5),0,1)
   k = hErr/L + kLat;  s = SteerCommandForCurvature(k); reverse: -s   (the lateral term also in reverse)
```

**Declared limit:** it closes to about 10.4 cm on the sedan; below the bite speed, the drag of the engaged clutch beats the CARRY (DayZ).

##### 5. Vehicle parameters the control needs

Precedence: MEASURED > CONFIG > DERIVED > FALLBACK. A derivation inherits the worst provenance of its inputs.

| Parameter | Use | Source in DayZ | In 7DTD |
|---|---|---|---|
| wheelbase `wb` | Ld, zones, bicycle, N2 | wheel contacts projected on the axis; if not, envelope | **measure** from the `WheelCollider`s (E.3) |
| `steerMax` | all the steering | envelope > config > 0.61 rad | **read** `Vehicle.SteerAngleMax` |
| `ku(v)` understeer | bicycle, MaxCurvature, grip cap | measured per 5 km/h bin; prior 1.0; floor 0.05 | measure; start with 1.0 |
| command → angle transfer | cmd → angle | measured in steady state | linear (`moveStrafe · SteerAngleMax`): verify |
| a_lat of the take (p90 and p95 per band) | speed and curvature ceilings | from the recording | the same (it comes from the take) |
| `CoastDecel(v, piso)` | aFree | measured; if not, rolling + aerodynamic | measure (release everything on flat ground and log) |
| `MaxBrakeDecel(piso)` | braking, zones | derived from brake torque and mass, ceiling μ·g | measure full braking; `BrakeTorque` as a reference |
| mass, wheel radius | torque model | runtime / envelope | `vehicleRB.mass`, radius of the `WheelCollider` |
| torque curve, ratios, gears, idle/redline | ThrottleForAccel, gears, bite | config | **there is none**: use `thr = aNeed/aMax` |
| `maxAccelMs2` | degraded form, uphill start | measured | measure (full throttle from standstill) |
| `launchThrottleFloor` | launch, CARRY, ramp | calibration on flat ground with a ramp; if not, 0.13 | measure with the same ramp (+0.02 every 0.5 s until it moves at a sustained 0.05 m/s) |
| `idleCreepKmh` / `creepKmh` | "still", authority, creep | measured or derived from the brake | almost certainly 0 (no clutch): verify |
| slew 80°/s | slew | measured on humans | 7DTD already applies `SteerRate`: measure |

##### 6. Control scars (why each rule is the way it is)

**Lateral: pure pursuit**
- **Curvature always per arc window, never per waypoint.** The single-wp curvature changes sign at almost every point, and with wps 2.7 cm apart it saturates the 1.0 clamp.
- **Denominator = Ld, not the remaining distance** (between Plan refreshes). Otherwise the gain rises on its own: ×2.05 was measured, and the lateral error reached −2.15 m.
- **The aim point is refreshed at 40 Hz.** The 0.5 s delay, not the gain, was eating the phase margin.
- **Do not shorten Ld in curves.** That raises the gain and diverges: 8 m and 18 m were measured. What was missing was feed-forward.
- **Do not shift the aim point outward ("sagitta").** On an arc, `2·sin α / d = 1/R` exactly. The bias steals α from the loop and the equilibrium ends up outside.
- **Ceiling first, floor after.** And above 60 km/h, the corner cut cannot bring Ld below the design value (1.65 m of corner cut is allowed).
- **Reverse: the reference goes on the leading axle.** The error was one of sign: the same geometry pushes outward, and no gain fixes that.

**Lateral: N2 and output**
- **N2: ψ against the tangent of positions and the heading of the velocity.** Otherwise, the human's drift comes in as a spurious correction.
- **Trim KI divided by v².** With a constant KI, the loop gain grows with v², and it ended in a spin.
- **Steering cap in a single funnel.** There are two writers (Plan and 40 Hz); twice a fix was applied in one and the other overwrote it.
- **The grip cap is in angle units with `ku`.** Capping the kinematic angle limited it to 20-26 % of the budget.
- **Removed: easing the steering to zero (1.5 s), the spin brake, and the countersteer anticipation.** All three were net regressions. A fixed timer costs 45 m at 107 km/h.
- **Blending the recorded wheel in as FF at high speed was negative.** The human's steering assumes the human's position.
- **The recorded wheel column is the angle, not the key** (the key is binary).
- **A measured curve must be sane:** monotonic and without gaps. Otherwise, the linear relation is used.

**Longitudinal**
- **The curve is a ceiling even with a recorded speed.** The recording is a permission: the vehicle can arrive faster than the human did.
- **The take can only raise the a_lat budget.** If it could lower it, half the fleet would slow down.
- **A STOP already served does not ask for 0.** On a downhill, that 0 asked for brake, and the vehicle stayed stuck forever.
- **Gear based on the current speed** (DayZ). With the target speed, the vehicle stayed in 5th gear at 0 km/h.

**Launch and recovery**
- **The floor is a FLOOR, and the ramp has to ramp.** A constant throttle never reaches a threshold.
- **The launch ramp arms with the MEASURED creep and disarms if the vehicle falls.**
- **Every "wait for X" needs an exit** (no progress: 20 ticks in maneuver, 6 in the endpoint, 12 in hillstart).
- **Moving is not progressing.** The no-progress counter drains one at a time; it is not reset.

**Maneuvers and closing**
- **`signed` only means something near the end.** Once it closed a leg with 189 m of arc still ahead.
- **The sign of `signed` is data.** Using |signed| made the vehicle drive backwards forever.
- **Asymmetric τ, only in the precision zone.** Applying it over the whole leg killed the speed 15 m from the end.
- **Slam: aim at the point, not at the tolerance.** Do not accept the endpoint while the vehicle is still getting closer. The endpoint distance is 2D.

**Tape**
- **Recorded brake against a still vehicle = pedal deadlock.** The brake is released, but the vehicle still brakes at 0.10 m from the end of the leg.

##### 7. Ambiguities found in the DayZ code (so that you do not copy them blindly)

- `CurvatureLimitMs` uses the ±1.5 m centered-difference curvature instead of the window curvature.
- At 40 Hz, `kCorr = k40 − kFF` uses the `kFF` of the last Plan, not the freshly computed one (it only affects the gear-change freeze).
- `CurvaturaDesdePose` uses `dist` in the denominator: if the aim-point refresh did not apply, the 1/d gain that the Plan rule avoids comes back.
- The tape output is written after the slew and does not update the applied steering: when the tape lets go, the slew starts from an old value.
- The per-frame endpoint brake keeps the 0.10 m offset that the slam removed; `ACEPTA_ENDPOINT_M = 0.25` compensates for it.
- The plant of 85.1°/u is a value measured in DayZ: do not use it in 7DTD.

#### E.6.3 Legs and mode arbiter (stops, direction changes and arrival)

Files: `BZRouteData.c` (precomputation and legs), `BZLegMachine.c` and `BZLegView.c` (route cursor), `BZModeArbiter.c` (modes). The maneuver and the endpoint are in E.6.2 §3-4.

**Two parallel indexes. Do not mix them up:**
- the **route cursor** (over `_wp.csv`, 2 Hz): `remainM`, `isReverse` and `endIsStop` come from it;
- the **tape cursor** (over `_cinta.csv`, 40 Hz), from E.6.1.

Their arcs **do not match**: the route arc is 3D and the tape arc is XZ (on one route an offset of 1.35 m was measured). That is why a route stop is matched to the tape **by position**, never by `s`.

**Route precomputation at load time:**
1. **Sanitizing** on insert: a wp is dropped if it advances ≤ −2 m along the recorded heading of the previous accepted wp (+π if that one was reverse). `legBreak`, `isStop` and direction changes are exempt. Using the last two points instead of the recorded heading, the sanitizing cascaded: 451 wps dropped.
2. **Arc** `S[i]`: the sum of 3D distances.
3. **Heading** `Head[i]`: the recorded one (col 15, +π in reverse). If there is no data, the tangent.
4. **Smoothing** of the heading: average of sin and cos over ±2 wps, without crossing leg breaks. The recorded yaw jitters 0.2-2.9°/wp.
5. **Curvature** `k[i]` = `WrapPi(Head[b] − Head[a]) / (S[b] − S[a])` over a window of **±1.5 m**, clamped to ±1 (+ = right). The wp-to-wp curvature kept flipping sign and capped the speed on straights.
6. **Lateral grip of the take**: `aLat = v²·|k|`, with v ≥ 5 m/s.
   - `ALatToma` = p90 (cap 40), for speed. `ALatDir` = p95 (cap 100), for steering. The percentiles are computed by bisection over a fixed range.
   - They are also computed per 20 km/h band. The lookup interpolates between bands and **never returns less than the global value**, because of downforce.
7. **Legs.** A new leg starts at `i` if `legBreak[i]` or if `isRev` changes.
   - A leg of **a single wp** is absorbed into the previous one. With `legBreak` at 211 and reverse at 212, a ghost leg was born, the arbiter did not see the direction change and the vehicle froze.
   - The **direction** of each leg is decided by **majority vote** of its wps with `recKmh ≥ 1`, not by the first one: when stopped, the vehicle keeps the gear it had.
   - `endIsStop` = the last wp of the leg is `isStop` or is the last one of the route. `endIsCusp` = there is another leg after it.

**Leg-machine: sole owner of the route cursor** (once per Plan).
- It projects `pos` onto the segments of the leg, up to 60 m ahead of the cursor. The cursor only accepts a **greater** arc (it is monotonic).
- **Reacquisition:** if the best segment in the last 25 m is closer than 1/1.5 of the distance of the best segment ahead, the cursor moves back there. The trigger is not a lateral threshold, because being **behind** is different from being **displaced**.
- `SeekTo(wp)` is the only other way to move the cursor back.

**LegView: what the modules read:**
- `alongM` = arc traveled since the start of the leg; `remainM` = arc still to go.
- `crossTrackM` = lateral offset against the cursor's segment. **+ = left**: this is the opposite convention to the tape's `latM`.
- `headingErrRad` = `WrapPi(Head(cursor) [+π in reverse] − yaw)`.
- `signedToEndM` = `(end − pos)·(sin θ, cos θ)`, with θ the heading of the **last segment of the same leg**. `Head(end)` is already the heading of the next leg: at a direction change it points the opposite way, and it closed the leg on the first tick.
- `SampleAt(ds)`: position, heading and curvature interpolated `ds` meters ahead.

**Closing a leg:**
1. **Maneuver closed** (E.6.2 §3): `v < max(0.3, creep/2)` and also one of two: `signed ≤ 0.10 m` with `remainM ≤ 1.5·wb`, or more than 20 ticks without progress. Closing with `signed > 0.10` counts as "gave up".
2. **The tape nailed the direction change:** the tape possesses, the vehicle has been still for ≥ 0.5 s, the brake of the row is > 0.5 and `remainM ≤ 1.5·wb`.
3. **Endpoint:** the slam latches (E.6.2 §4).
4. **Abandonment:** more than 20 ticks of CLAVADO (pinned) with no recovery in progress (E.6.4).

- `OnLegDone`: if `endIsStop` ⇒ `OnArrived`. Otherwise ⇒ next leg (and `OnArrived` if there are no more).
- `OnArrived`: the arbiter goes to HALTED. On an intermediate leg, `ARRIVED` is emitted and it waits for `Resume()`. On the last one, `ROUTE_COMPLETE`, or `ROUTE_INCOMPLETE` if any leg gave up or was abandoned, or if the final error (longitudinal-lateral hypotenuse) is above 0.25 m.
- **A single arrival threshold:** `LLEGADA_M = 0.10 m`. Endpoint acceptance is 0.25 m, because the per-frame brake parks ~0.10 m short. In DayZ there were two thresholds (0.10 and 0.12) and they gave opposite verdicts. **The tolerance is not moved to make a run pass.**

**The mode arbiter** (in Plan). Modes: `INVALID 0 · IDLE 1 · CRUISE 2 · APPROACH 3 · MANEUVER 4 · ENDPOINT 5 · HALTED 6`.

Zones, with `wb` the wheelbase (2.5 m if unknown):
- `ManeuverZone = 3·wb`.
- `EndpointZone = max(wb, 2 m)`.
- `ApproachZone = max(v²/(2a) + 0.5·v, EndpointZone)`, with `a` the maximum braking.
- `LlegoLaHoraDelSlam` = `endIsStop` and also `remain ≤ EndpointZone`, or `remain ≤ 2·wb` with `v < max(creep, 1 km/h)`.
- `ReversaNecesitaManiobra` = `remainM < window` (×1.5 on exit), or the worst curvature over the next 2·wb (7 points, **not measured across the end of the leg**) is above the usable curvature of the vehicle.

```
Decide():
  if the state is incoherent (NaN, no leg): SAFE_STOP      // safety FIRST, no dwell
  if IDLE:    enter the initial mode (reverse: MANEUVER if it needs it; otherwise CRUISE)
  if HALTED:  stay (it only leaves through Resume)
  if 40 ticks (20 s) without the route cursor advancing 0.3 m: SAFE_STOP  // by ARC, not by speed
  next = NextMode(); if next != mode and (next == ENDPOINT or dwell ≥ 2 ticks): enter
NextMode():
  CRUISE:   reverse that needs a maneuver → MANEUVER · end of leg (not a stop) at ≤ 3·wb → MANEUVER
            · slam → ENDPOINT · endIsStop and remain ≤ ApproachZone → APPROACH · otherwise CRUISE
  APPROACH: (does not go back to CRUISE) same as CRUISE without the last branch
  MANEUVER: slam → ENDPOINT · in reverse: stays while it needs it, otherwise CRUISE
            · last-resort exit: > 20 ticks and the end is not near → CRUISE
            · same leg of the chain, or alongM < 4.5·wb, or v < creep → MANEUVER · otherwise CRUISE
  ENDPOINT: absorbing; leaves through OnArrived → HALTED
```

- **Entering ENDPOINT at 40 Hz:** the same slam predicate with `remAhora = remainM − max(0, remFrAlPlan − remFrAhora)`, where `remFr` is the projection onto the final tangent. At 16 km/h one Plan tick is 2.2 m against a 3.2 m zone, and the arrival speed was left to the luck of the sampling. **Do not use `min(arc, projection)`**: in a parking maneuver the projection is ~0 far from the end.
- **Hysteresis on the distance, not on the curvature.** On the curvature it opened a 50 % band and the reverse never left MANEUVER.
- **SAFE_STOP** is not an ending: if the recovery ladder is available, it opens it (E.6.4). It is only `STUCK` if the ladder is exhausted or disabled.

#### E.6.4 AutoRecovery: so the vehicle does not stay stopped

Files: `BZAutoRecovery12.c` (episodes and ladder), `BZWorldProbe.c` (obstacle probing), `BZDriveController.c` (detectors and actions) and `BZBusService.c` (relocation and spawn, on the runner side).

**The scope, which is a design rule.** The framework guarantees that the vehicle **does not stay stopped forever**, and it **notifies**. What to do with a blocked vehicle (remove it, wait, send another one) belongs to the mod that uses it.

##### 1. The basis: the speed reading is not movement

- In DayZ, a vehicle pushing an obstacle reads 0.3-0.7 km/h: 70 s at full throttle against a parked bus.
- Stopped 1 cm from a rest row, it reads 0.06-0.5 km/h.
- When launching from a standstill it reads up to ~1.8 km/h without moving 10 cm (in 63 % of 203 human launches).

**That is why progress is measured by position along the route.** There are two measures:
- **the route cursor** (`alongM`), which is monotonic: backing up does not lower it, it stays flat;
- **the tape arc** (`sEst`): `SinProgresoT` accumulates while `sEst` advances less than 1 cm per tick. Since the odometry has no sign, **backing up makes it advance**, and backing up needs its own clock.

**[port]** First measure what 7DTD reads in these three cases (pushing, still, launching). If `rigidbody.velocity` does not lie, some guards are unnecessary. If it lies, the same jams show up.

##### 2. Detectors

| Detector | Clock | Fires when | Reason |
|---|---|---|---|
| **Crash (the AR one)** | 40 Hz | within 0.5 s the maximum was ≥ 20 km/h and it dropped by ≥ 20 km/h | send it to physical recovery and rule out bog. It stays valid for 60 s within an 8 m radius |
| **Crash/bump (the one the consumer sees)** | 40 Hz | deceleration above 1.5 × max(g, maximum braking). With no contact and < 5 km/h lost, it is `BACHE` (= bump) | one line **per episode** (5 s); at 2 Hz a 25 ms impact does not exist. When leaving a hold, clear the previous speed (otherwise every departure from a stop was a false crash) |
| **Non-physical signature** | 40 Hz | the tape possesses, still ≥ 0.5 s, mode CRUISE, no plateau (a recorded stop) and the recorded row **is not a departure row**: `kmhRec > 2` or `brk > 0.5` | at that point the human was still rolling or braking, and applying that to a still vehicle does not move it. The OR covered 100 % of the simulated cases |
| **CLAVADO** (pinned) | 2 Hz | **requested** throttle ≥ 0.95, all wheels on the ground, no plateau, and `alongM` did not advance 0.30 m during 11 ticks (5.5 s) | it asks for full throttle and does not advance. If wheels are off the ground and it is still: "RUEDAS EN EL AIRE" (= "wheels in the air"; straight to physical) |
| **No-progress jam** | 2 Hz | `SinProgresoT ≥ 10 s` with requested throttle ≥ 0.95, already started (wp > 0), no plateau and no recovery in progress | the one that pushes, oscillates or creeps: neither CLAVADO nor the stillness check sees it |
| **AHOGO** (bog) | 2 Hz | forward segment, gear ≥ first, applied throttle ≥ 0.5, brake ≤ 0.1, wheels on the ground, no recent crash, and in 3 s it advanced < 0.5 m **and** it was seen going backwards or the engine dropped to idle | movement without progress. **[port]** Without a gearbox or rpm, bog does not exist as such: skip C1 and let the no-progress jam cover this |
| **SAFE_STOP** | 2 Hz | 20 s without 0.3 m of progress of the route cursor (E.6.3) | the safety net of the arbiter |

##### 3. Response levels, from lowest to highest

**Level 1: logical restart** (without moving the vehicle). When the non-physical signature fires:
- If the tape throttle is ≥ 0.9, or there was a recent crash, it goes to **physical**: the brain is not stuck, the vehicle is the one that cannot move.
- Otherwise: the **tape is released until the end of the plateau** (a recorded stop; E.6.1) and LAUNCH starts again with its ramp.
- At most 3 attempts **per episode** (one episode = one place, radius 8 m), with 1 s between attempts. Counted per route, a bus ran out of attempts at the fourth stop.
- **Success of any action:** ≥ 1 m from where it was applied **and** ≥ 2 km/h **in the direction of the segment**. Rolling backwards does not count: a bogged bus reported "avanza a 2,26 km/h" (= "advancing at 2.26 km/h") while going backwards. It expires after 15 s.

**Level 2: C1, first gear + hill start** (DayZ only, see the table). Throttle ramp from the measured floor, with brake until the throttle is enough for the slope. If it advanced but stayed 3 s under 2 km/h, or covered less than 5.56 m in 10 s, the verdict is **"traba, no ahogo"** (= "jam, not bog") and the ladder opens **on that same tick**: waiting for another bog cost 32 s.

**Level 3: the physical ladder.** A single entry point, `Fisico(evidencia)`. Any new physical evidence while the ladder is open **climbs one rung**.
1. **Rung 1, "drive out":** move 3 m away **against the direction of the segment** (on a reverse segment, forward), at up to 5 km/h; then 1 s of brake and 4 s of grace.
   - **First, probe** from the bumper (length/2, not the center): two 2.5 m circles at `pos + dir·(half + 1)` and `pos + dir·(half + 1 + 3)`.
   - **Any object with collision** counts: vehicles, bus shelters, poles, guardrails, trees, rocks. People, items and bushes do not count. If there is something, it jumps to rung 2 without spending rung 1. In DayZ the maneuver reversed 3 m into a bus shelter that was 1.84 m away.
   - It cuts with a **braking margin**: `d + v²/(2a) ≥ 3 m`. Without it, 3 m requested gave 4.89 m.
   - Without all wheels on the ground (sunk or overturned), it is skipped.
2. **Rung 2, relocate** to the last good ground **minus one wheelbase**.
   - "Good ground" = a continuous interval of arc where the vehicle was going forward within ≤ 1 m of lateral offset.
   - One wheelbase is subtracted because the good ground before a crash ends right at the impact (0.57 m from it).
3. **Rungs 3 and up, safe zones:** relocate **ahead** of the stuck point, at `ZonaBase + k·1 m` (k = 0..6).
   - `ZonaBase = length/2 + 1.5 m`: it is the first pose in which the rear bumper is past the stuck point. With a fixed +2 m, a 7.66 m vehicle landed on top of the obstacle.
   - Past the maximum, the ladder is **AGOTADA** (= exhausted).

**Relocation** (the runner does it):
- Pose = the one **recorded** in the tape at that `s` (interpolated position and heading of the frame), at `y + 0.10 m`.
- It is rejected if the destination belongs to a segment of the opposite direction, or if there is a vehicle in the **footprint**: circles every 1.5 m along the axis, not a single central circle. A long vehicle ended up on top of a sedan with "destino libre" (= "destination clear").
- Inertia is cleared (`impulse = −mass·velocity`), the tape is anchored at `s`, the route cursor is reset and LAUNCH starts.
- Probe at 1.5 s: if ≥ 2 wheels are off the ground and it ended up > 0.3 m above the ground, it is **sitting on top of something** and it is rejected on the spot.
- **A rejection does not spend a rung:** on rungs ≥ 3 the same zone is swept 1 m further ahead, up to 10 m of sweep. Two ladders had been exhausted without trying a single usable pose.
- A rung that did not succeed within 15 s **climbs on its own**.
- The ladder closes when the vehicle gets ≥ 8 m away from where it was opened.

**STUCK** comes only from SAFE_STOP and means only one thing: **20 s without progress and the ladder exhausted or disabled**. It always carries its reason.
- A SAFE_STOP with a rung in progress is **not** STUCK: the arbiter resets and decides again on the same tick.
- A SAFE_STOP with no open ladder **opens it**. Before this, a bus spent 22 s against another one and was declared STUCK with 0 attempts.

##### 4. What the consumer is notified of

| Signal | What it says |
|---|---|
| `BLOQUEADO` event | the ladder **has opened**. It is emitted on the transition, no matter who opened it, once per ladder. It carries the evidence and whether there was a crash |
| `Bloqueado()` · `BloqueadoMotivo()` · `BloqueadoDesdeS()` | the ladder is in progress. The runner logs "BLOQUEO terminado tras N s" (= "block ended after N s"); check: number of BLOQUEADO = number of ended blocks |
| `Recuperando()` | a superset of the above: C1, maneuver, pending relocation or ladder in progress. It answers "will this car be able to move now?" |
| `ChoqueHaceS()` | seconds since the last crash (not bump); −1 if there never was one |
| `STUCK` event + `MotivoStuck()` | the route ended without being able to continue |
| `ROUTE_INCOMPLETE` + reason | the last leg was abandoned, or the end was out of tolerance |
| `EsperandoCabecera()` | the departure point is occupied (see spawn). It is a wait, not a failure |

**[port]** One log line per action, with the format `date, route, vehicle, wp, s, type (NO-FISICO|FISICO|AHOGO), evidence, action, result`. Every `return false` of the recovery records **why** it did not act: a recovery that says "no" cannot be told apart from one that was never called.

##### 5. Per-route configuration (in the `_hdr.json`; a key only overrides the default if it is written)

`AutoRecovery.Logical.{Enabled true, WaitS 0.5, MaxAttempts 3, CooldownS 1.0}` · `AutoRecovery.Physical.{Enabled true, ReverseM 3.0, ReverseMaxKmh 5.0, LadderMaxM 6.0, SweepMaxM 10.0}` · `BogRecovery.{Enabled true, WindowS 3.0, MinProgressM 0.5, MaxAttempts 2}` · `ManualAR.{AdvanceM 10.0, GoodLatM 1.0}`.

An absent block is **not** read as zeros. In DayZ, the JSON reader initialized an absent nested block to zero and recovery ended up off by default.

##### 6. Robust spawn

1. **Height:** `y = y_recorded + 0.10` if it is within 1.5 m of the terrain. Otherwise, `terrain + 0.5`. With a fixed `terrain + 0.5`, a low pickup fell 0.7 m onto its body and ended up with 0 of 4 wheels on the ground.
2. **Departure point occupied = WAIT, do not move the point.** If there is a vehicle less than 3.5 m from wp 0, nothing is created and it retries every 5 s, with no limit, logging who is occupying it. With the point moved 4 m, a bus spawned outside its take, next to a reverse segment, and ended in STUCK.
3. **Re-assert the pose after creating.** In DayZ the engine puts the **center of the model** (not its origin) at the requested point, facing north: the bus ended up 0.22 m off and 0.73 m too high. You have to call `SetPosition(requested)` and set the heading right after creation.
   - **Heading:** from wp 0 to the first wp more than 2 m away, +180° if that segment is reverse.
   - Recommended: freeze the physics, orient, zero the velocities and only then enable the physics.
4. **Verify at 3 s:** position, heading, pitch, roll and the deviation from what was requested.
   - **[port]** This is the first measurement you have to make in 7DTD. We do not know whether Unity places the origin or the center of the prefab.
5. **Wheel probe** at 1.5 s and at 3.5 s: how many touch the ground.

### E.7 — (Optional) The 7DTD road network, if some day you want routes that were not recorded

The framework **does not need it**: the take IS the route. But if you want to generate routes (a taxi that goes to any POI), 7DTD ships the network in a PNG:
- **`splat3_processed.png`** in the world folder **is the road network** (`WorldBuilder` itself labels it `xuiRoads`). **1 pixel = 1 block = 1 m**, an image of `WorldSize × WorldSize` (6144² in Navezgane and in Pregen 6k). World coordinates are centered: `x_world = px − WorldSize/2`.
- Colors (`WorldGenerationEngineFinal.PathShared.IdToColor`): black = empty; **G (0,255,0)** = country road and dirt highway; **R (255,0,0)** = highway; B = water.
- ⚠ Use `splat3_processed.png`, **not** `splat3.png`: the raw one only has what the generator planned (highways and country roads) and is disconnected inside the towns; the `_processed` one has the streets of towns and POIs merged in (4× more pixels).
- Offline extraction: `skeletonize` + distance transform (road width = distance × 2). Measured: Navezgane 127 km of centerline, 1,218 nodes, median width 8 m; Pregen06k01 478.5 km, 26,580 nodes, 5.7 m.
- ⚠ In towns, paved lots and driveways are painted as street: the skeleton builds spiderwebs (median segment 4 m). Prune spurs and filter by width, or use `splat3.png` as the arterial layer and the `_processed` one for local streets.
- **There is no "this is a street" API at game time** (this was searched for): the extraction is offline and the mod carries the graph as data.
- `prefabs.xml` gives the POIs with their world position (3,469 in Pregen06k01): free taxi destinations.
- A higher-fidelity alternative, only in generated worlds (RWG): `WorldGenerationEngineFinal.Path` has `FinalPathPoints` / `pathPoints3d` (vector polylines with lanes and radii), but it only lives in memory during generation; you would have to hook into `WorldBuilder` to dump it.
**Mind the thesis:** a route generated from a graph **has no tape** (nobody drove it). It is driven with pure pursuit + curvature-based speed (what the proof of concept in E.3 does), which is quite a bit less faithful than replaying a take. In DayZ the conclusion was: **record what matters; generate only what cannot be recorded.**

### E.8 — Framework lessons that hold in any engine

Each one cost days in DayZ. They are the most valuable part of this guide.

1. **Spatial fidelity before temporal fidelity.** Index the playback by **distance traveled along the route**, never by time. If the NPC goes slower on a climb, playing back by time puts it out of phase and applies the steering of the next curve on the straight; by distance, the steering of each point is applied at that point.
2. **The speed reading is not movement.** A vehicle that is pushing something, or spinning against a curb, shows half a km/h without advancing a meter; and when starting from a standstill, the physics engine can report up to ~2 km/h before the vehicle has moved 10 cm. **To decide whether it is stuck, measure progress along the route (the cursor), not the speedometer.**
3. **A recorded stop is not a jam.** Where the human stopped, the vehicle has to stop, and no "it is stuck" detector may fire there.
4. **A "bogged engine" and a push look the same** (throttle floored, engine at idle). If the bog remedy does not work within a few seconds, it is a push: go to recovery.
5. **Recovery must not back up into whatever is behind it**, and on a reverse segment "moving away" means going forward. Probe **any object with collision** in the footprint of the maneuver, not only vehicles.
6. **Do not declare "stuck" without having tried anything**, nor in the middle of an attempt. The route is given up only when recovery is exhausted, and the log says why.
7. **An occupied departure point is waited for, not moved:** a vehicle that appears beside its tape cannot follow it.
8. **When you create a vehicle, check where it ended up** a few seconds later (position, heading, tilt) and log the deviation from what was requested. In DayZ the engine placed the center of the model at the requested point (not the origin), and every vehicle spawned 0.2-0.4 m off without anyone noticing for weeks.
9. **Nothing that lasts more than a couple of milliseconds in a single frame.** Loading a 17,000-point route in one frame freezes the server; spread it over many frames with a per-frame budget, and measure the **worst** frame, not the average.
10. **Count per episode and per place.** Ten log lines about the same crash are one crash; and problems repeat at **places** (a tight curve, a stop with a bus shelter), not at route point numbers.
11. **What the consumer decides, the framework does not guess.** When to launch, where to wait, whom to yield to, when to remove a vehicle: that belongs to your mod. The framework guarantees that the vehicle does not stay stopped forever, and it **notifies**; it does not decide for you.
12. **The take is a measurement.** It is not edited. If something always goes wrong at the same place, first look at the take at that place: often the human passed too close to something, and the fix is to re-record that segment.
