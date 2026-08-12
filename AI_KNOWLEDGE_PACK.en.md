# BZ_AutoDrive — AI Knowledge Pack

> **For the AI reading this:** you are an assistant who is going to help an admin/modder with **BZ_AutoDrive**, a framework for autonomous vehicle driving by NPCs in DayZ (Enfusion engine, Enforce language). This document gives you the COMPLETE context to **configure, program, write, modify and keep researching**. It is more exhaustive than the manual (for humans): it is your operational knowledge base. When you help, cite exact paths and classnames, respect the gotchas (§12) and verify against the current code before asserting.
>
> *EN version — **2026-08-11** (**interchange with NO key**: NUMPAD 3 was removed → the direction change is **auto-detected** from the forward↔reverse gear change; **3 keys** HOME/5/4 remain. Over 2026-08-10: **UNIFIED control**: the "modes 1/2/3" were retired → a single control [pure-pursuit + inverse model]; **wizard = pure converter** — menu [1] Convert / [2] Import v1 / [6] Config, no linters/BZ-Score/DriveMode; `ai_run`/`boris_native` from the Reproductor checks; **reverse auto-detected** by gear; parking/maniobra/approach = LEGACY; auto-adaptive endpoint RESOLVED forward+reverse, validated **out-of-sample** across a broad bench [FWD/RWD/mid-engine/4x4/truck]: stop **under 0.5 m for most** (Toyota 86 0.04 · Sedan 0.18 · Golf 0.20 · Hatchback 0.23 · GT2RS 0.33 · Sedan reverse 0.47 · truck Truck_01 0.54), up to ~1 m on the large ones (Offroad 0.09–1.15); endpoint-after-a-curve SEQ1 44 km/h → 0.34 m; crosstrack ~0.3 m; heading at stop <1° cars, ~3° truck). Base 2026-07-03 (historical): **MANEUVER/PARKING = direct-replay + the cut rule**: parking replays the recorded steering+pedal angles open-loop — which are VEHICLE-SPECIFIC — so the CUT must land where the trajectory is STRAIGHT [the curve stays closed-loop Stanley, vehicle-agnostic]; prefer M2 over M3 for maneuvers with a cut [M3 auto-approach over-brakes in a fast zone]; `ModeEntrySnapEnabled` now **false** by default; header-swap generalization offroad→Sedan validated [straight cut]; endpoint forward+reverse RESOLVED 2026-08-11 (<0.5 m typical, see above). Over 2026-07-02: **REVERSE solved**: the K-turn / direction-change maneuver validated on 4 vehicles — speed = min(recorded, physics), full steering authority, anticipation ∝ R_min, terrain-aware endpoint-taper; real-dt fix for the high-speed cruise freeze; `maniobra` mode removed from the workflow; architecture: the control fixes are RUNTIME, not baked into the take. Over the 06-27 base: lights+horn Phase 2 replay, PDF/HTML REPORTS, fast-load with lights/horn). MIT.*

---

## 0. How to use this document

- **Get oriented** with §1 (what it is) + §2 (status, where we are).
- To **configure** a route/scenario → §6 (config) + §7 (wizard/pipeline) + §8 (events) + §9 (quests).
- To **program/extend** → §11 (code patterns) + §4–5 (architecture/control) + §12 (gotchas).
- To **keep researching** → §14 (methodology) + §15 (frontier) + §5 (control internals).
- **To GUIDE a user live** through the core loop (record → convert → play), assuming they **do NOT program** → follow the **Walkthrough §7.G**: ONE step at a time, confirm before advancing, translate everything into concrete actions (which key, which file, what to look at) and verify the files on disk. To **MAP an area** (join route graphs) → **Appendix D.7**.
- **Golden rule:** this pack reflects the state as of 2026-06-27; the code is the truth. Before asserting a function signature or a default, confirm with a grep of the cited file.

---

## 1. What BZ_AutoDrive is (executive summary)

A **piloting layer on top of eAI** (DayZ-Expansion-AI). eAI provides the NPC body (walking, getting in/out of a car) but drives naïvely (infantry navmesh, steering = angle/π, **always 1st gear**, zero config reading). BZ_AutoDrive overrides its driving outputs so an NPC drives **any vehicle** by following **a recorded human demonstration**, reading the vehicle's **declared configuration** ("config as driving manual") and applying **classic control** (Stanley + feedforward + inverse model + predictive cruise). **No per-vehicle training.**

Three ideas: (1) **demonstration = route mapping** (DayZ has no road network; the recording supplies it); (2) **config = driving manual** (the `config.cpp` declares torque/gearbox/steering; the framework reads it, does not guess); (3) **interpretable classic control** (deterministic, inspectable, zero-training).

---

## 2. Project status — WHERE WE ARE (2026-06-27)

**Done and validated:**
- **Forward/cruise driving:** solid. Generalization validated across a **broad bench of drivetrains and sizes** (FWD / RWD / mid-engine / 4x4 / **truck** out-of-sample) with a single take (89.9–99.5 % lateral ≤2 m; 100 % waypoints). Steering median 1.4–1.6°.
- **Lights + horn (Phase 2 replay):** Boris reproduces the human's **horn** and **lights** per waypoint (`targetHorn`/`targetLights`); the PathLogger captures them while recording. Per-route config (`HornMode`/`LightsMode`). The horn syncs directly; the **lights of an observed AI car** required a custom NetSync + client-side forcing (network gotcha — §12). See §5/§12. **3rd example vehicle (EXAMPLE02)** with human lights+horn proves it generalizes (98.6 % completion, lat-dev 0.79 m).
- **Diagnostic reports (PS tooling, RETIRED from publication):** the PDF/HTML report generator (`report_export.ps1`) was **moved to `..\BZ_AutoDrive_devtools\` and is NOT published**. Diagnosing a run today is done by reading the `ai_run` directly (see D.10). *(Historical: it generated multi-page self-contained PDF/HTML with three report kinds — human take, Boris take, comparative.)*
- **Vehicle-recording match:** the recording is the manual of THAT vehicle (own take vs cross take: steering 0.046 vs 0.17, 0 vs 250+ saturations).
- **Emergent routing:** graph + Dijkstra over recorded segments → never-recorded routes, 96–99 % lateral.
- **Parking:** precision controller validated (shed, anti-rollback). **Kept.** **Nature (2026-07-03): parking = DIRECT-REPLAY (open-loop).** Waypoints tagged `parking` do NOT use Stanley: the framework replays the EXACT recorded steering + pedal angles. Premise **"arrive ready"**: reach the segment in the demonstrated pose+speed. *(LEGACY: the `parking` mode and its toggle key **NUMPAD+ (`KC_ADD`) were removed** — new takes no longer mark it; the code keeps the branch only to honor old takes.)* See §5 (cut rule + M2 vs M3).
- **Maneuver/parking — GENERALIZATION via straight cut (2026-07-03, validated):** direct-replay = steering angles = **VEHICLE-SPECIFIC** (same angle → different radius by wheelbase). **The cut rule:** cut where the trajectory is STRAIGHT (flat heading) → the curve stays **closed-loop** (Stanley follows the path, vehicle-agnostic) + the straight in replay → generalizes. Cut in a CURVE → open-loop → a different wheelbase drifts → does not generalize. Validated **as a method** via **header-swap** (offroad wb 2.357 → Sedan wb 2.935): straight-cut **completed** on both; curve-cut the longer Sedan drifted. **Current stance (cross-vehicle):** a **recording belongs to its vehicle** (its fingerprint/brake/gear); header-swapping recordings is NOT the recommended production flow. Cross-vehicle is done in the **EDITOR** (draw/load the vehicle-independent **trace** + **assign the vehicle**); the header-swap remains a legacy technical mechanism (still useful as a validation method). Prefer **M2** (`FollowPathCapByRecording=true`) over M3 for maneuvers with a cut (§5).
- **`ModeEntrySnapEnabled` = FALSE by default (2026-07-03, was true):** the alignment teleport on mode transitions was turned off — the closed-loop control (parking + reverse rear-steer, heading <1°) positions on its own. The snap also only fired at <0.5 m (`ModeEntrySnapMaxDist`). Re-enablable per `_hdr`.
- **Endpoint (final stop) auto-adaptive = RESOLVED (2026-08-11):** the stopping brake auto-configures per **vehicle + surface** (config brake torque + real grip, traction-aware), with no per-vehicle constants. Stopping precision **under 0.5 m for most** (up to ~1 m on the large / long-wheelbase ones), validated **out-of-sample** across a broad bench (FWD / RWD / mid-engine / 4x4 / truck, forward and reverse): Toyota 86 RWD **0.04 m** (heading -0.6°) · Sedan **0.18** · Golf FWD (reverse in a curve) **0.20** · Hatchback FWD **0.23** · Porsche GT2RS mid-engine (reverse) **0.33** · Sedan in reverse **0.47** · truck Truck_01 (long wheelbase, 43% reverse) **0.54 m** (heading -2.8°) · Offroad **0.09–1.15 m** (the loosest). Crosstrack median **~0.3 m** (up to ~0.5 m in FWD); heading at stop **<1°** on cars, **~3°** on the truck. The **endpoint after a curve** (historical weak spot) is resolved **at the root**: control **follows the recorded speed** on the approach and only brakes in the last ~3 m (SEQ1 at 44 km/h → **0.34 m**) — it is the mechanism, NOT a per-route patch.
- **Reverse (direction-change maneuver / K-turn):** control **data-validated on 4 vehicles** (R_min 3.44–4.57: Nissan / r32 / Sedan / Camaro), without re-recording. **Rear-steer** bicycle model (Stanley 180° + inverted sign + `ShiftTo(0)`, control point anchored to the **rear axle**). Three new pieces (2026-07-02): (1) **speed = min(recorded, physics)** via `ffRev` (the fraction of the vehicle's max steering for the local arc, already normalized by R_min) → generalizes per-vehicle (wide turn → high `ff` → slower → follows the arc); (2) **full steering authority** — forward's anti-over-rotation `SteeringScale` is **NOT** applied in reverse (it halved the wheel) + **anticipation ∝ R_min**; (3) **terrain-aware endpoint-taper** — it brakes toward the end of the reverse block by **path distance** (not by the target, which jumps to the forward-resume), and the floor **rises with the grade** → uphill it does not taper (self-guard) and preserves the climb. Result: exit overshoot **16 m → 5.5–8.7 m**, arc lat-dev ~0.5 m. The **K-turn** = a recorded maneuver so the NPC **reverses its direction of travel** on the route (forward → reverse at an angle → opposite forward). **Only pending:** validating the shed ramp (uphill).
- **Quest integration:** 2 in-game playable scenes (convoy `flee_on_kill`, `ambush_on_damage`). MissionServer hook + poll + animated boarding with pacification.
- **UI:** Control Panel + Reproductor (hot route loading, no restart).
- **Wizard = pure converter:** menu **[1] Convert (only NAME) · [2] Import v1 take · [6] Configure paths · [Q] Quit**. No linters, no BZ-Score, no mode choice — `frame_to_route.py` reads the header, generalizes and writes the deployed trio. `Wizard.bat` launcher (double-click); portable paths (`wizard_config.json`); everything inside the wizard, no editing JSON or running loose .py/.ps1. See §7.
- **Migration:** the mod was renamed from `BrigadaZ_Transport` to **`BZ_AutoDrive`** (server A done+verified; B pending). The v1.0 `BrigadaZ_Transport` stays frozen on Workshop; BZ_AutoDrive will be published as a new item.

**Open / in progress:**
- **Reverse uphill (the shed ramp):** the ONLY reverse case not yet data-validated. The terrain-aware endpoint-taper is designed to NOT taper on a slope (the floor rises with the grade → it sits above the recorded target → self-guard → it does not fire) and to preserve the ~3 km/h climb + slope-comp without adding a brake that kills the push. The validation run is still pending (recorded separately). Reverse on the **FLAT (K-turn) is closed**.
- **Waypoint modes:** the current converter (`frame_to_route.py`) only **produces `normal` and `reverse`** — reverse is **auto-detected from `gear==0`**, and the direction change (`legBreak`) is **auto-derived from the forward↔reverse gear change** (always at ~0 km/h; a 0 with no direction change = pause, not interchange; the editor also marks it on the node). The old `parking` · `approach` · `maniobra` are **no longer marked or produced** in new takes (removed from the key menu, as was the old interchange key NUMPAD 3, 2026-08-11); the code **keeps the branches** only to honor **OLD takes** that have them (e.g. the K-turn EXAMPLE18). `approach` is now **automatic** (`ApproachAuto`).
- **Gestures** (eAI emotes): pending API verification + implementing the `play_gesture` verb.
- **Fully autonomous wizard:** the auto-diagnosis loop still needs closing.
- **Extreme driving (drifts):** out of scope (the eAI receiver smooths it out).

**Future vision:** LLM-driven NPC (the framework already has agent shape: DSL=actions, graph=navigation, config-read=perception). See §15.

---

## 3. Codebase map

**Source:** `E:\BRIGADA Z PVE SERVER\MOD-SCIPTS\BZ_AutoDrive\` (backup in `E:\BACKUP\mod\MOD-SCIPTS\`).
**Build output:** `E:\BRIGADA Z PVE SERVER\MODS\BZ_AutoDrive\` (addons/ + keys/). **Deploy A:** `C:\DayZServer\@BZ_AutoDrive\`. **Deploy B (2nd PC, offload):** `Y:\@BZ_AutoDrive\` (routes synced to `Z:\BZ_AutoDrive\`; B runs the SERVER over Radmin VPN while the main PC runs the client + the tools). **Client:** `!Workshop\@BZ_AutoDrive\` + loaded from `C:\DayZServer` (see §13). **A↔B sync is always manual** (robocopy `@BZ_AutoDrive` to B → triple match by hash).

**Scripts (PBO), by Enforce scope:**
- `scripts\3_Game\` — early helpers: `BZBusCommon.c`, `BZBusRPC.c` (RPC enum, incl. `RECEIVE_TOAST`), `BZBusStops.c`, `BZGearRangeTable.c`, `BZPathLogCommon.c`, `BZCleanupConfig.c`, `BZBusClientManager.c`.
- `scripts\4_World\` — **the core**: `BZBusService.c` (server-side singleton; spawn, Tick, control, events/verbs `ExecuteAction`, quest hooks `OnQuestStart`/`CheckQuestBots`/`BoardQuestBots`; ~6500 lines), `BZBusCarScript.c` (the `modded CarScript` override-last; also the custom **lights NetSync** `m_BZLightsWanted` + `BZSetLights`/`OnVariablesSynchronized` so an observed AI car shows the beam — see §12), `BZInverseModel.c` (speed PID + gear + slope + surface), `BZBusConfig.c` (class `BZBusRouteConfig` with 50+ fields + `BZAction`/`BZTrigger`/`BZMarkerEvent`/`BZCrewMember`), `BZBusPlayerBase.c` (modded PlayerBase: OnRPC, NUMPAD recording handlers), `BZPathLogService.c` (PathLogger: recording + fingerprint), `BZRouteCleanup.c`, `BZILCCorrections.c`, `BZBusStopZone.c`, `BZExpansionWreckFilter.c`.
- `scripts\5_Mission\` — UI + mission hooks: `BZBusMissionGameplay.c` (polls the Controls-menu inputs in OnUpdate), `BZBusMissionServer.c`, `BZQuestHook.c` (`modded MissionServer Expansion_OnQuestStart`), `BZReproductorUI.c` (Reproductor), `BZControlPanelUI.c`, `BZBusUI.c`, `BZDebugOverlay.c`.

**Config/data:** `config.cpp` (CfgPatches/CfgMods class `BZ_AutoDrive`, prefix `$PBOPREFIX$`=`BZ_AutoDrive`), `data\` (bus_stops.json, gear_ranges.json, wrecks_cleanup.json), `gui\layouts\` (control_panel_v2.layout, etc., CPP-style format), `gui\textures\` (.paa), `stringtable.csv`, `meta.cpp` (name=BZ_AutoDrive, publishedid=0).

**Tools (PowerShell/Python, NOT in the PBO — they run on the admin's PC):** `tools\` is now **only the wizard runtime + the editor**: `tools\route_wizard.ps1` (TUI; menu [1] Convert / [2] Import v1 take / [6] Configure paths / [Q] Quit), `tools\Wizard.bat` (double-click launcher, `-ExecutionPolicy Bypass` only for that run), `tools\frame_to_route.py` (**the real converter**: recording→trio `.json`+`_hdr.json`+`_wp.csv`, no modes), `tools\transport_v1_to_route.py` (v1 import), `tools\i18n_strings.ps1` (wizard i18n, dot-sourced), `tools\driving_config_template.json` (the driving config the converter attaches), `tools\wizard_config.json` (portable paths — **not published**), and **`tools\editor\`** (the trajectory & map editor, §10/UI). The **dev/analysis/legacy** tools (validators `enforce_lint.py`/`check_rpt.py`, analysis `endpoint_detail.py`/`analyze_*`, old calibration `calibrator_lib`/`route_calibrator`/`curve_advisory`, old converter `csv_to_route`/`route_split`, reports `report_export.ps1`, asset-gen) were moved to **`..\BZ_AutoDrive_devtools\`** (outside the mod, **not published**).

**build:** package the PBO with DayZ Tools (AddonBuilder) or your own pipeline; sign with your own key. (No build script ships — each modder packages their own way.)

---

## 4. Architecture

**Override-last (the breakthrough).** Control is injected into a `modded CarScript.OnInput(dt)` that calls `super.OnInput(dt)` FIRST (eAI runs, including `ShiftTo(FIRST)`) and AFTERWARD overrides `SetSteering/SetThrottle/SetBrake/ShiftTo`. Subordination, not replacement: eAI never "knows" it is being driven. Robust to eAI updates.

**Three layers:** the **recorded path** (3D positions) = sacred objective (reaching the waypoints in order is guaranteed); the **declared physics** (config + Newton) = limits that are not violated; the **control law** = the bridge.

**Runtime flow:** `BZBusService` (server singleton) spawns the vehicle + the eAI driver, runs a Tick (~every 500 ms for event logic; input control runs in OnInput per frame), advances the waypoint index, evaluates triggers and fires `ExecuteAction` per verb.

---

## 5. The control stack (internals + parameters)

**Lateral Stanley.** `targetYaw = segHeading − atan2(K·lateralOffset, v)`, K=1.0 default. The `atan2(K·off, v)` attenuates by speed (strong slow, soft fast) → kills the zigzag. **Sign gotcha:** left-handed DayZ → `cross = AB.z·AP.x − AB.x·AP.z` (inverted = divergence). **Lesson:** modulating K by local curvature MAKES IT WORSE.
- Params: `SteeringScale` (-1=auto, derived from wheelbase), `CurvatureSteerBoost`, `PathSmoothWindow` (0 recommended on tight curves; >0 cuts curves — historical bug of the hardcoded divisor).

**Corridor / "paredón" (containment wall).** Dead band (lane half-width): inside it Stanley is OFF (does not perturb), outside it is ON + damping. Params: `CruiseLateralDeadband` (~0.5), `CruiseLateralKGain` (1.0), `CruiseLateralDamp` (~0.3), `CruiseLateralCenterOffset` (lateral bias; +right/−left; ~25 m of median per unit — resolution 0.01–0.02). **Apply CenterOffset AFTER the deadband** (bug FBC8571F: it was subject to the deadband).

**Feedforward.** Curvature with lookahead (~20 m cruise, 1–3 m parking). Param `CruiseFFWeight` (-1→0.25). `CurveThrottleEnabled` cuts throttle BEFORE a tight curve.

**Inverse model (`BZInverseModel.c`).** Speed PID: `targetSpeed → throttle/brake`. `UseInverseModel`, `InverseModelKp/Ki/Kd` (-1=default 0.4/0.05/x), `InverseModelLowRpmMin` (rpmMin×1.0 vs ×1.3; default true post Test C: damped gear, less lugging), `TargetSpeedSmoothWindow` (smooths the target, less PID variance), `AccelShiftThreshold` (anti-catapult). Predictive braking: `aNeeded = u²/(2·dist) ± g·sin(pitch)`.

**Gear.** `GearStrategy = "auto_box"` (AT by RPM) or `"follow_recording"` (recorded gear; recommended default for non-bus; sport cars spin in 1st with auto_box). `MaxGear` (FIRST=2…SIXTH=7). The InverseModel respects follow_recording via SelectGear override.

**Slope.** `SlopeCompensationEnabled`, `SlopeLookaheadWps` (5), `SlopeGain`, `SlopeLateralGain`. The recording already has the terrain baked in → following it inherits the compensation.

**Lights + horn (Phase 2 replay).** The PathLogger captures the human's `horn`/`lights` columns (via `CarScript.Cast(parent)`); each waypoint stores `targetHorn`/`targetLights` (in `BZWaypoint`), and Boris reproduces them per waypoint (**spatial replay**: he honks/turns lights on where the human did). Per-route config: `HornMode` (`replay`/`stops`/`finish`/`off`) — the RECORDED horn always replays; `HornMode` controls only the AUTOMATIC honk; `LightsMode` (`replay`/`auto`/`auto_inverted`/`on`/`off`). **Horn:** `CarScript.SetCarHornState(int)`, enum `ECarHornState{OFF=0,SHORT=1,LONG=2}`, works while moving and **syncs to the observer** because `m_CarHornState` is `RegisterNetSyncVariableInt` + `SetSynchDirty`. **Lights:** require an energized battery (`OnBeforeLightOn()` requires `GetCompEM().GetEnergy()>0`; the fix in `EquipBus` does `batt.GetCompEM().SetEnergy(GetEnergyMax())`). The **network gotcha** for lights on an observed AI car (and its v3 fix with a custom NetSync) is in §12 — mandatory reading whenever you touch any VISUAL state of an AI car.

**AutoRecovery (classic, stuck-based).** `AutoRecoveryEnabled` (default false), `AutoRecoveryStuckTimeS` (5–10), `AutoRecoveryAdvanceWps` (3–5), `AutoRecoveryCooldownS` (8), `AutoRecoveryMaxPerMission` (0=unlimited). Teleports to wp+N if Boris gets stuck (speed~0, or `wp_idx` not advancing for >N s even while steering); preserves velocity via impulse; logs a geographic diagnostic heatmap. **"Do not save Boris from Boris" philosophy:** on CLEAN terrain Boris is precise → if he gets stuck on a clean path it is a RECORDING bug (hiding it just conceals it → re-record). That is why it stays default OFF.

**AR_OnWay (shield against EXTERNAL world obstacles — 100% validated on 5 vehicles, 2026-07-01).** Different from the classic AR: it protects Boris from the **WORLD** (another stopped vehicle, or one that hits/pushes him), not from his own control. Two **independent** flags:
- **`ObstacleSlow`** = predictive brake. Scans the path ahead with a **lookahead that VARIES by speed + vehicle physics**: `scanMax = ObstacleStopDist + v²/(2a) + v·0.6 + 5`, with `a = min(ObstacleDecel, the vehicle's real decel)` (`BZInverseModel.GetMaxBrakeDecel` from config, bounded by friction·g) → a heavy pickup with weak brakes **looks farther ahead and brakes at its real maximum, on its own**. It brakes to a stop at `ObstacleStopDist`≈15 m. **Lateral corridor** `ObstacleCorridorHalf`≈2.3 m: measures the obstacle's offset from the lane axis → a car on the **shoulder** or in the opposite lane does NOT brake Boris (only what blocks his lane). It also queries **Boris's REAL front** (not just the recorded waypoints) so it does not miss a car pressed against / pushing him.
- **`ObstacleEscape`** = teleport to the first **clean** wp past the obstacle, if it persists (>`ObstacleEscapeWaitS`≈6 s braked) or if he is **pushed/hit**. Triggers robust to scan flicker: `owPersist` (braked + seen recently), `owPushing` (wants to advance, target>15, but kmh<10 sustained after seeing the obstacle), a "touching" level (<5 m → 2 s). Smooth resume at `ObstacleEscapeResumeKmh`≈10.

Config: `ObstacleSlow`/`ObstacleEscape` (bool, default false), `ObstacleScanDist`(50, **floor**), `ObstacleStopDist`(15), `ObstacleDecel`(4.5), `ObstacleCorridorHalf`(2.3), `ObstacleEscapeWaitS`(6), `ObstacleEscapeResumeKmh`(10). **Profiles (toggle — the key, runtime-settable by the quest):** **Robust transport** (both ON) / **Interceptable** (Slow ON + Escape **OFF** → Boris brakes nicely for whatever blocks him and does NOT escape → the interception mission works; if he escaped it would break it) / **None** (both OFF = pure replica). Gate `(ObstacleSlow||ObstacleEscape) && UseInverseModel` (the template already ships `UseInverseModel=true` → it engages). The **v1 import asks for the profile** (**[R]**obust / **[I]**nterceptable / **[N]**one → sets `ObstacleSlow`/`ObstacleEscape` in the `_hdr`); in a new take they are set by config. **Open edge:** a **head-on** collision that pushes Boris ~3 m off-path can wedge him (that is **off-path recovery** territory, distinct from "obstacle ahead in the lane" — "almost closed"). See §15.

**Speed strategy — flag combos (ADVANCED; no longer a user choice).** The wizard produces **a single control**: it follows the line + the recorded speed (pure-pursuit + inverse model; `FollowPath=false` + template). The old **"modes 1/2/3"** were combos of these flags and are now **unified into that single control**; they stay hand-accessible in the `_hdr.json` for a modder who wants other behavior: **pure replay** (`FollowPath`/`FollowPathUseReference`/`UseInverseModel`=false; same vehicle, `hasInputData=1`), **pure geometry** (`FollowPath`+`UseInverseModel`=true; speed by curvature + cap, heavy vehicles), **reference-assisted** (all three true; recorded speed + steering by config = the generalizer). It is NOT a ranking.
**Maneuvers with a cut — flag behavior (advanced/legacy, 2026-07-03):** `isM3approach = (UseInverseModel && !FollowPathCapByRecording)` — the **auto-approach is M3-ONLY**. It aims at the parking entry as a crawl (target ~5). If the cut lands in a fast zone (straight ~23 km/h) it **over-brakes** → the vehicle plants itself in the transition (real case: Sedan M3-straight stuck at `wp2289`, on-path, spd 0). **M2** (`FollowPathCapByRecording=true`) crosses at the RECORDED speed → robust and generalizes better. **For maneuvers with a cut: prefer M2.**

**Maneuver controllers:** **parking** (high FF, short lookahead, anti-rollback) — ALIVE. **It is DIRECT-REPLAY (open-loop, 2026-07-03):** waypoints tagged `parking` do NOT run the Stanley loop — the framework replays the EXACT recorded steering + pedal angles (`targetSteering/Throttle/Brake`). Premise **"arrive ready"**: reach the segment in the demonstrated pose+speed. *(The `mode="parking"` recording toggle with NUMPAD+ was **removed from the key menu** — parking/maniobra are **legacy**; new takes do not mark them. The code honors the branches for OLD takes.)* **Corollary (the cut rule):** since the angles are VEHICLE-SPECIFIC (same angle → radius ∝ wheelbase), direct-replay **does not generalize in a curve**. For another vehicle to complete (cross-vehicle = **assign the vehicle to the trace in the editor**; the recording header-swap is legacy): the CUT into the maneuver block must land where the trajectory is **STRAIGHT** (flat heading) — that way the curve stays **closed-loop Stanley** (vehicle-agnostic) and only the straight is replayed. Straight-start detection: **first post-maneuver sample with flat heading (<1.5° over 20 samples)**. Cut in a curve → the Sedan (wb 2.935) crashed; cut on a straight → completed (parking 0.71→1.11 m). See §2 (the cut rule). **`ModeEntrySnapEnabled` now `false` by default (was true):** the alignment teleport on mode changes was turned off — the closed-loop control (parking + reverse rear-steer, heading <1°) positions on its own; the snap only fired at <0.5 m. Re-enablable per `_hdr`. **reverse** — ALIVE and **validated** (§2, 4 vehicles). **Rear-steer** bicycle model: control point at the **rear axle** (`GetReverseControlOffset`≈wheelbase/2 — it also anchors the FF, not just the corridor), inverted Stanley sign, `ShiftTo(0)`, follows the recorded wheel above `ReverseRecordedSteerThreshold`≈0.2. **Full steering authority:** the `SteeringScale` (forward's anti-over-rotation, ≈wheelbase/5.5) is **NOT** applied in reverse — it halved the wheel and prevented forcing the arc. **Anticipation ∝ R_min:** `floorFf = R_min·1.3` (R_min = wheelbase·cos/sin(maxSteer); `Math.Tan` is not guaranteed in Enforce) — a wide turn anticipates earlier. **Speed = min(recorded, physics):** `vPhysRev = MAX_REVERSE − (MAX_REVERSE−REV_PHYS_MIN)·|ffRev|` (MAX=`GetReverseTargetSpeedCap`≈25, MIN=5) → generalizes per-vehicle. **Terrain-aware endpoint-taper:** within `REV_TAPER_M`(8 m) of the end of the reverse block (measured by path distance walking the wps) it ramps the target to a floor that **rises with the grade** (`floor = MIN_PROG + grade·36` if grade>0.03; grade = dy/dist to the endpoint's Y) → **flat** it brakes to 3 km/h (kills the 16 m overshoot), **uphill** the floor exceeds the recorded target and the taper does not fire (self-guard, preserves the climb). **Auto-adaptive endpoint (final stop) = RESOLVED (2026-08-11):** the endpoint-taper + the per-vehicle+surface auto-adaptive brake bring the stop **under 0.5 m for most** (up to ~1 m on the large ones), forward and reverse, out-of-sample (Sedan in reverse 0.47 m, GT2RS in reverse 0.33 m; full list in §2). The endpoint after a curve follows the recorded speed and only brakes in the last ~3 m (SEQ1 44 km/h → 0.34 m). **forward→reverse transition** honors the recorded brake-to-0 + handbrake + gear 0; the reverse→forward **exit** is resolved by the *handbrake-resume* (it skips the handbrake-forward cluster and resumes forward — before it got stuck in gear 0 + brake 1). **approach** — ALIVE: it is not a separate steering controller (it uses the normal Stanley) but a **speed tag** that ramps the brake BEFORE entering a maneuver — today **automatic** (`ApproachAuto`; it is **no longer** marked with a key) so it does not slam the brake and skid. **maniobra** — **deprecated for NEW recordings** (2026-06-17: out of the UI/hotkeys), BUT the branches remain in the code and honor old takes that have it (e.g. the K-turn EXAMPLE18). **Reverse knobs (per `_hdr`, no rebuild):** `ReverseFFWeight` (default 0.6=parking), `ReverseStanleyK` (default 0.8), `ReverseTargetSpeedCap` (25), `ReverseStanleyMinSpeed` (2 m/s, breaks the 1/v spiral). **All the reverse fixes are gated to `mode=='reverse'` / `isReversePk`** — they do not touch cruise or maniobra (isolation rule).

---

## 6. Route config (`BZBusRoute.json`) — reference

Class: `BZBusRouteConfig` in `BZBusConfig.c`. Sensible defaults; `-1`/`0` = code default / off. Groups: **Basics** (`VehicleClass`, `DriverClass`=eAI_SurvivorM_Boris, `RespawnDelay`=300, `SpawnHoldSeconds` [0=immediately, 30+=wait for mission trigger], `VehicleInvincible`, `MaxGear`, `Attachments`, `Wheelbase`); **Speed/mode** (§5); **Steering** (§5); **Slopes** (§5); **AutoRecovery** (§5); **Convoy/Quest** (`ConvoyMode` ""/"flee_on_kill"/"ambush_on_damage", `Crew[]`, `Events[]`); **Maneuvers** (parking/reverse params, §5). **Waypoints[]:** `pos[x,y,z]`, `targetSpeed`, `targetGear`, `mode` (normal/parking/maniobra/reverse), `isStop`/`stopDuration`/`stopRadius`, `targetThrottle/Brake/Steering`, `hasInputData`, `name`. Embeds a `Fingerprint` (from the recording header). For the exact field list: Appendix A of the manual.

**Attachments: NEVER guess** (non-negotiable rule). They come from the recording fingerprint (NUMPAD 5 captures them) or from the trader package/config. When changing `VehicleClass`, REPLACE attachments (the converter preserves the ones from the previous JSON = recurring bug). The recording header is the authoritative source (the converter already prefers it).

---

## 7. The wizard + the pipeline + where the data lives

**Pipeline:** record (NUMPAD 5 → `frame_*.csv` + `header_*.txt`) → `route_wizard.ps1` **[1] Convert** (asks only the NAME → `frame_to_route.py` builds the trio `.json`+`_hdr.json`+`_wp.csv` and leaves it deployed) → run (Reproductor, hot-load without restart).

**The wizard (`route_wizard.ps1`, TUI) — current menu:** **[1] Convert · [2] Import v1 take (BrigadaZ Transport) · [6] Configure paths · [Q] Quit**. It is a **pure converter**: it does **NOT** lint, does **NOT** give a "score/BZ-Score", does **NOT** choose a "driving mode" (there is only one, §5). Quality comes from **recording well** (§7.G): if the take came out dirty — you rode up a curb, braked badly — **re-record** (it's free).
- **[1] Convert** — you pick the **recording** (listed by **date**, newest on top) and give it a **NAME** (→ `BZBusRoute_<name>.json`; that's how it shows in the Reproductor; empty Enter = default active route). The wizard calls `frame_to_route.py <frame> <name> --profile <RoutesDir>`: it reads the `header_*.txt` (fingerprint), **generalizes** the line with the vehicle's physics, **auto-detects reverse from `gear==0`**, **auto-derives the leg breaks** (`legBreak`) from the forward↔reverse gear change and writes the **trio** (`.json` + `_hdr.json` + `_wp.csv`) already **deployed** (hot-load). **There is no mode choice** — the control is single.
- **[2] Import v1 take** — converts a **BrigadaZ Transport v1** take (monolithic JSON with `Waypoints`) to the AutoDrive format. It asks for the **vehicle identity** (a `header_*.txt` from any recording of that car — a 10 s one works — or an already-calibrated `_hdr.json`) + an **obstacle profile** (`robust` / `interceptable` / `none` → sets `ObstacleSlow`/`ObstacleEscape`). Runs `transport_v1_to_route.py`. See D.11 (v1 import guide).
- **[6] Configure paths** (`Invoke-ConfigPaths`) — re-sets `RoutesDir` + (optional) `ServerBMirror` (Enter = keep, `-` = clear the mirror); stores them in `wizard_config.json`.

**Wizard PRINCIPLE:** **everything is done inside the wizard**; it orchestrates `frame_to_route.py` (which already writes the `.json`+`_hdr.json`+`_wp.csv` trio in one shot) for you. The user **does NOT edit JSON by hand nor run loose `.py`/`.ps1`**. Recording folders are **portable** (`Update-LogDirs`): no longer hardcoded — they derive from `RoutesDir`/`ServerBMirror` (client via `%LOCALAPPDATA%`). Launcher **`tools\Wizard.bat`**: double-click, no opening PowerShell by hand (uses `-ExecutionPolicy Bypass` only for that run).

**Robustness against operator errors:** each menu option runs in its own `try/catch`. If an operation fails, **it shows the error and RETURNS TO THE MAIN MENU** — the wizard **no longer closes** (it only quits via **[Q]**). Before, an error killed the script and, since it is launched from `Wizard.bat`, Windows showed *"Terminate batch job (Y/N)?"* — that no longer happens in normal use. → If an operation fails, the error text **stays visible on screen** (it used to be swallowed): have the user **copy** it and continue from the menu.

**Route/recording selector (`Select-Route`):** **hides the `*_hdr.json`** (the "header" half of the fast-load pair `_hdr.json`+`_wp.csv`, which used to appear as 0-wp items and confused). The list shows **only the real `.json` routes**.

**Conditional "server B" reminder (portability):** `route_split.ps1` used to print a hardcoded "Also sync the pair to server B" — irrelevant for a modder with a single server. Now `route_split` **no longer mentions it**; the reminder appears ONLY if the user has a second server configured (`ServerBMirror` in `wizard_config.json`), and it shows the **REAL PATH**. Most users (a single server) never see anything about "server B" — part of the wizard being folder-agnostic/portable.

**Where the data lives (CRITICAL, it cost 1h to find):**
- Human recording (`path_*.csv`, NUMPAD 5) → **CLIENT**: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- NPC run (`ai_run_*.csv` and `boris_native_*.csv`, **opt-in via the Reproductor checks**, no longer a key) → the **SERVER that ran it** (A: `C:\DayZServer\profiles\BZ_AutoDrive_PathLogger\`; B: `Y:\profiles\...`).
- Routes (JSON) → `C:\DayZServer\profiles\BZ_AutoDrive\` (`BZBusRoute*.json` + `_hdr.json` + `_wp.csv`).
- `header_*.txt` (next to each path_*) = the vehicle fingerprint.

**File formats:**
- `path_*.csv` (30 cols): time_s, x, y, z, heading_deg, speed_kmh, is_stop, gear, throttle, brake, steering, rpm, redline_rpm, mode, vx, vy, vz, clutch, handbrake (+ more, incl. the human's **horn** and **lights** for the Phase 2 replay).
- `header_*.txt`: `vehicleClass=`, `mass=`, `engineRPMIdle/Max/Redline=`, `gearsCount=`, `wheelCount=`, `attachmentsCount=`, `attachments=` (CSV), `maxSteeringAngle=`, `wheelbase=`. R_min = wheelbase / tan(maxSteer).
- `ai_run_*.csv`: time_s, x/y/z, heading, speed_kmh, gear, throttle, brake, steering, mode, dist_to_next_stop, next_stop_idx, **wp_idx**, **lateral_dev_m**, corridor_offset/valid, target_speed, target_throttle/brake, i_speed/throttle/brake, rpm, redline_rpm, wp_mode, is_marker.
- **The framework PREFERS the `_hdr.json`+`_wp.csv` pair** (fast FGets, ~86 ms) over the monolithic JSON (slow parse ~150 s). **GOTCHA (historical):** the old `csv_to_route.ps1` only rewrote the `.json` → you had to run `route_split.ps1` afterward or the server loaded the OLD pair (spawns the previous vehicle). The current converter (`frame_to_route.py`) **writes all three files at once** → no longer applies.
- **The fast-load pair carries lights/horn:** `route_split.ps1` writes `targetLights` (col 16) and `targetHorn` (col 17) on each `_wp.csv` row; `LoadWaypointsCSV` (in `BZBusService.c`) parses them (`parts[16]`/`parts[17]`). So the lights/horn replay also works through the fast-load path, not only the monolithic JSON.

**The converter does NOT lint or score.** `frame_to_route.py` is direct: it reads the header, generalizes the line and writes the trio — it does **not** ask you *"cap this curve?"* nor give a BZ-Score. If the take came out dirty (gaps from lag/teleport, gear lugging, a curb), the answer is **re-record**, not a linter. *(The old wizard's interactive linter + BZ-Score were removed: fidelity comes from recording well + reading the fingerprint.)*

**"Recording CORRUPT" guard (csv_to_route):** some OLD recordings carry, instead of numbers, the literal format specifiers (`.2f`, `.1f`) in ALL rows — a bug of an old PathLogger version (or hand-edited takes). It used to crash convert with a cryptic error ("cannot convert '.2f' to Single"). Now `csv_to_route` **detects** it (checks that the `x` column of the 1st row parses to a number) and **aborts with a clear message**: *"Recording CORRUPT… record it again or pick a more recent one"*. It is NOT a wizard bug, it is the recording → pick a **newer** take (the ones at the TOP of the list, sorted by date desc, that have `throttle/brake/steering` columns).

**`frame_to_route.py` (the real converter):** takes the `header_*.txt` → automatic VehicleClass + Fingerprint + Attachments. **Auto-detects reverse from `gear==0`**. Derives `targetHeading` by geometry (`atan2(dx,dz)`). **Auto-derives the leg breaks / interchanges** (`legBreak`) from the forward↔reverse gear change at ~0 km/h (a 0 with no direction change = pause, not interchange; the editor also marks them). Writes the **full trio** (`.json` + `_hdr.json` + `_wp.csv`) to `<profiles>\BZ_AutoDrive\` with a `.bak` backup — **no** separate `route_split` needed. It does **NOT** accept modes: the driving config comes from the single template (`driving_config_template.json`). **Heads up:** `gear==0`=reverse assumes the carpack convention; verify per vehicle (it can be neutral). **Corrupt** guard: if the `x` column of the 1st row does not parse to a number (old recordings with literal `.2f`/`.1f`) it aborts with *"Recording CORRUPT… record it again or pick a more recent one"*.

> ⚠️ **The paths above are Sonom4n's setup (EXAMPLES).** Every server/install is different — `C:\DayZServer\`, `Y:\`, `%LOCALAPPDATA%\DayZ\...` are valid for him, not for the user reading you. Before guiding someone with file locations, **ASK FOR THEM** (see Walkthrough §7.G, Step 0).

### 7.F — Common wizard problems (troubleshooting)

Map the direct symptom to the fix. (For runtime/compile/deploy errors, see §12.A.)

| Symptom | Cause | What to do |
|---|---|---|
| **"Recording CORRUPT… record it again or pick a more recent one"** on convert | OLD recording with literal `.2f`/`.1f` instead of numbers (an old PathLogger bug, or a hand-edited take) | NOT a wizard bug, it is the recording. Pick a **newer** take (the ones at the TOP of the list, sorted by date desc, with `throttle/brake/steering` columns). If you only have old takes, **re-record**. |
| A menu operation **fails with a red error** but the wizard **stays open** (returns to the menu) | this is the new behavior: each option runs in its own `try/catch` | ask the user to **copy the error text** (now visible on screen) and to **continue from the menu**. The wizard only closes with **[Q]**. |
| (Old versions) the wizard **closed** and Windows asked *"Terminate batch job (Y/N)?"* | an error killed the script launched from `Wizard.bat` | no longer happens in normal use (try/catch per operation). If you see it, it is an uncovered case → report the error. |
| In the route/recording selector a **`*_hdr` item of 0 waypoints** appeared | the fast-load pair's `_hdr.json` was listed as a route | no longer shown: `Select-Route` hides the `*_hdr.json`. The list only carries real `.json` routes. |
| A **"sync to server B"** reminder appears (real path) | the user has `ServerBMirror` configured in `wizard_config.json` | expected: copy the **pair** (`_hdr.json`+`_wp.csv`, not only the `.json`) to the path it shows. With a single server, this notice **does not appear**. |

### 7.G — Guided walkthrough (to accompany a user live)

*For an AI (you) to guide an admin who **does NOT program** through the core loop. Rule: ONE step at a time, confirm before advancing, translate everything into concrete actions (which key, which file, what to look at) and verify on disk.*

**0 · ASK FOR THE USER'S PATHS FIRST — do not assume the ones in this doc.** Every server/install changes the paths. Ask and note before starting:
- **Server folder** (where the `@BZ_AutoDrive` and `profiles\` live): Sonom4n uses `C:\DayZServer\`, but it could be `G:\MyServer\`, a VPS, etc. → **ask**.
- **Client recording folder** (PathLogger): usually `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` on the PC where they play → confirm the real `%LOCALAPPDATA%`.
- **Where the `tools\` are** (the `route_wizard.ps1`).
- Do they have **2 servers** (A local + B remote)? Ask both paths; routes are synced by hand (no automatic sync).

Only with those paths, guide them with the REAL locations of THEIR files at each step. *(The wizard, additionally, asks for its paths on the 1st run and stores them — it is folder-agnostic.)*

**1 · RECORD** (in-game, as admin):
- Keys in `Options → Controls → "BZ AutoDrive"`. **Only 3** (and only the **admin** sees them): **Panel** (HOME), **Record/Stop** (NUMPAD 5), **Mark Event/Stop** (NUMPAD 4). The **direction change/interchange is NO longer a key** (NUMPAD 3 removed 2026-08-11): it is **auto-detected** from the forward↔reverse gear change. `ai_run`/`boris_native` **also have no key** — you arm them from the **Reproductor checks** (Step 3).
- Get into a vehicle → **NUMPAD 5** (start) → drive the whole route → at each stop brake fully and **NUMPAD 4** (mark) → if there is a **direction change**, while stopped **shift gear and carry on** (no key: the interchange is auto-detected from the gear, §9.4 manual) → **NUMPAD 5** again (stop). **Reverse is NOT marked either**: it is auto-detected from the gear.
- ✅ Verify: `path_*.csv` + `header_*.txt` appeared in **THEIR** recording folder. **The `header_*.txt` must NOT be 0 KB** (if it is 0 → the fingerprint was not captured → re-record). **Have them note which vehicle they recorded with.**

**2 · CONVERT** (on the PC, out of the game — **everything inside the wizard**, no editing JSON nor running loose .py/.ps1):
- **Open the wizard**: double-click **`tools\Wizard.bat`** (no need to open PowerShell). 1st run: it asks for their paths (server/client) and stores them (`wizard_config.json`); change them later with **[6] Configure paths**.
- Pick **[1] Convert** → first pick the **take** from a list sorted by **date (newest on top)** — the name `frame_<stamp>.csv` is a stamp of the **GAME clock** (not the PC's). If you see "Recording CORRUPT", pick a **newer** one (§7.F). Then it asks for **only a NAME** (Enter = active route). With that it runs `frame_to_route.py` (reads the header, generalizes, auto-detects reverse, builds the `_hdr.json`+`_wp.csv` trio in one shot) and leaves it **DEPLOYED**: "ready in the Reproductor (hot-load)".
- **There is no mode to choose nor linters to review** — it is a pure converter. If the take came out dirty, the answer is **re-record** (Step 1), not tuning. *(An advanced modder who wants to touch the control — `FollowPath`/`UseInverseModel`, etc. — does it by hand in the `_hdr.json`; see §5 + D.2. 99% do not need it.)*
- ✅ Output: `BZBusRoute_<name>.json` + `_hdr.json` + `_wp.csv` in **their** `profiles\BZ_AutoDrive\`, already deployed.

**3 · PLAY** (in-game) — the route was already deployed on convert, so **just open it in the Reproductor**:
- If they want to analyze: **tick the `ai_run` (and/or `boris_native`) check in the Reproductor BEFORE hitting play** (otherwise nothing is written; there is no longer a key).
- **HOME** opens the panel → in the **Reproductor** pick the route (it shows with the NAME you gave) → **LOAD&SPAWN** (or **Spawn/Restart Bus**). Boris drives it; watch the panel (wp, km/h, mode). The `ai_run_*.csv` lands on the **server that ran it**.

**Common failures → what to ask/do:** *(for wizard failures on convert/select/restore, see §7.F)*
- *"Recording CORRUPT" on convert* → old take with literal `.2f`/`.1f`; pick a newer one or re-record (§7.F).
- *The route does not appear in the Reproductor* → is the `.json` in `profiles\BZ_AutoDrive\` and named `BZBusRoute*`?
- *It spawns the previous vehicle* → OLD `_hdr`/`_wp` pair; re-convert with the wizard (it does the split).
- *No `ai_run` is written* → the `ai_run` check was **unticked** → tick it in the Reproductor BEFORE hitting play (it is no longer a key).
- *Boris stuck in 1st / does not accelerate* → it is eAI's `ShiftTo(FIRST)`; the override handles it. If it is a vehicle mod, confirm it extends `CarScript`.
- *Build/deploy* → NEVER build/mirror with the server OR the client open (they lock the PBO).

### 7.H — Diagnostic reports (PS tooling, `report_export.ps1`)

> **Note (RETIRED from publication):** the report generator (`report_export.ps1`) was **moved to `..\BZ_AutoDrive_devtools\` and is NOT published** with the mod (it was never in the wizard menu, which is a pure converter). What follows is kept as a **historical reference** of what it measured. To diagnose a run today, read the `ai_run` directly (D.10).

**What it is:** a PowerShell tool (does NOT touch the PBO; runs on the admin's PC) that generates **multi-page self-contained reports** in **PDF and HTML** from a human take and/or an `ai_run`. **Zero image/Python install**: it uses **Edge headless** (`msedge --headless --print-to-pdf`) + **inline SVG** — no ImageMagick/Pillow. Closes the framework's **MEASUREMENT** loop: turns the raw numbers of the `ai_run`/take into something the admin reads at a glance.

**Entry:** `New-RouteReport -ReportType Human|Boris|Comparative|Auto` (standalone; no wizard submenu anymore).

**THREE report kinds** (`-ReportType Human|Boris|Comparative|Auto`):
- **① Human take** (`Human`) — **post-recording, no Boris**. Analysis of the demonstration: curve advisory, slope, gear, **predicted BZ-Score**, markers + the take's **🔵 trace alone**. Used to judge the recording BEFORE running Boris.
- **② Boris take** (`Boris`) — **post-ai_run, no comparison**. Boris's **🟠 trace alone** + its **measured read** (completion, lat-dev, speed, saturations, run hotspots). For auditing Boris without the human take on hand.
- **③ Comparative** (`Comparative`, default) — **human take + its ai_run**. **🔵 human vs 🟠 Boris** trace + **hotspots** (top-N deviations: red circles on the map + **zone sheets** with zoom and a chart per hotspot) + **measured read of Boris** (completion, lat-dev, speed, saturations).
- `Auto` resolves the kind from the inputs: take only → `Human`; ai_run only → `Boris`; both → `Comparative`.

**Operational details:** language via `BZ_LANG` (ES/EN). Output to the **`BZ_AutoDrive_Informes`** folder (sibling of `routes`). The examples live in `profiles\BZ_AutoDrive_Informes\` (e.g. `EXAMPLE02.pdf` = comparative, `EXAMPLE02_toma.pdf` = human take).

**PowerShell gotchas when building this tooling** (they also go to §12, but stay here for context): a `.ps1` with **accents** needs **UTF-8 WITH BOM** (PS5.1 reads a BOM-less .ps1 as ANSI → mojibake in the report texts); passing `@($obj)` to a `NoteProperty` **breaks** (use `.ToArray()`); the intermediate data JSON is written **without BOM** (it is read by the HTML/Edge, not PS).

---

## 8. The event engine (verb/trigger DSL)

NUMPAD 4 marker (records `isStop=true` on the wp) → event node. `Events[]` of `{wp, trigger, actions[]}`.
- **Triggers** (`BZTrigger.type`): `wp_reached` (wp), `player_in_radius` (radius), `player_enter_vehicle`, `vehicle_health_below` (threshold 0..1), `timer` (seconds).
- **Verbs** (`BZAction.verb`, `else-if` dispatcher in `BZBusService.ExecuteAction(Car car, BZAction action, int evIdx)`) — **22**: `add_cargo`, `log_event`, `freeze_vehicle`/`unfreeze_vehicle`, `set_vehicle_mortality`, `set_driver_mortality`, `start_engine`/`stop_engine`, `despawn_vehicle`, `stop_route`/`resume_route`, `set_var`, `play_sound`, `lights_on`/`lights_off`, `horn`, `repair_vehicle`, `refuel`/`drain_fuel`, `ui_broadcast`, `spawn_guard`/`dismount_guard`. (`check_once` REMOVED: it broke the JSON load; branching is Quest's job.) *Reserved, no handler yet (extension — the `slot` field already exists in `BZAction`): `lock_seat`/`unlock_seat`/`eject_passenger`.*
- **BZAction fields:** verb, items, msg, slot, value, fvalue, var, faction, loadout, count, delay (choreography via CallLater).
- **To configure:** see §10/§12 of the manual + Appendix (JSON examples of the 2 scenes).
- **To program a new verb:** add an `else if (verb=="x") {...}` branch in ExecuteAction (you have `car`, `action`, `m_WaypointIndex`); the delay is handled by the dispatcher. New fields → add them to `BZAction` (it is flat, does not break the parser).

**Audio (`play_sound`):** 3D SoundSet attached to the vehicle (SEffectManager). Declare the `.ogg` in CfgSoundShaders+CfgSoundSets. **Gotcha:** a sound launched server-side may not reach the client → resend it via RPC (the `RECEIVE_TOAST` toast infra already exists; see `BroadcastGlobal`, which sends an RPC to all clients → `NotificationSystem.AddNotificationExtended`).

---

## 9. Quest integration (DayZ-Expansion-Quests)

**Division of labor:** Quest = live bots + mission logic (reward, progression); eAI = the bot body; BZ_AutoDrive = vehicle + driving + coordinating boarding/dismounting. A bot spawned standalone by the framework = a mannequin with no logic → the bots COME FROM the Quest.

**Hook (`BZQuestHook.c`):** `modded MissionServer { override Expansion_OnQuestStart(quest) { super; BZBusService.GetInstance().OnQuestStart(quest); } }`. (The `ExpansionQuestObjective*Event` subclass does NOT compile in scope 5_Mission → that is why the hook goes in MissionServer.)

**Poll (`OnQuestStart`/`CheckQuestBots`):** save `m_QuestCheckID = qc.GetID()`; CallLater poll of `ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(id, patrols)` (~every 2–4 s). `patrol.m_Group.Count()` = bots (they are lazy by proximity; the initial count is the PEAK, they materialize gradually 3→5). Count drops → one was killed → trigger.

**Boarding (`BoardQuestBots`) + the pacification gotcha:** a bot IN COMBAT does not walk to a waypoint (the eAI FSM requires "no threat"). You must: `b.eAI_SetPassive(true)` + `b.eAI_SetThreatDistanceLimit(0.0)` + drain targets (`GetTarget(0)`/`eAI_RemoveTarget` in a loop). Only then does it walk to `transport.CrewEntryWS(seat, door, ddir)` and board animated (otherwise it boards by teleport, ugly). Seat 1+ (0=driver). Do NOT re-group (it preserves the quest's kill-count). `SetThreat` does NOT exist in eAIGroup.

**Validated scenes:** `flee_on_kill` (pacify→board→drive→drop off at the yard→OnQuestComplete→despawn) and `ambush_on_damage` (bots instantly on board + armed Boris → any damage [CarScript EEHitBy or health poll] → NotifyConvoyDamaged → freeze + dismount-all-hostile). Insight: the ambush IS a generalized NUMPAD 4 marker (trigger on_damage → sequence); the natural refactor = a recipe of the Events[] DSL.

**Objective types:** validated with AICamp/AIPatrol. The others (AIEscort, AIVIP, Travel, Delivery, Target, Action, Collection) are supported by the architecture but lack their hook — open field (see the table in §17 of the manual).

**Relevant eAI API:** there is no imperative command bus; everything is GROUP + waypoints + reactive FSM. Locomotion: `FormationState=IN` (NONE=halt was the bug), `AddWaypoint` + `ForceRecalculate`, `OverrideMovementSpeed/Direction`. eAIBase IS PlayerBase. See the `reference_eai_*` memories.

---

## 10. UI

**Control Panel / Reproductor** (opened with the `UABZAutoDrivePanel` action, default `HOME`/`kHome`): live dashboard of each runner (wp, speed, mode) + control (stop/pause/teleport). The old `BZControlPanelUI.c` is now **disconnected** — the action now opens the Reproductor (`BZReproductorUI.c`); the `CTRL+HOME` was removed.
**Reproductor** (`BZReproductorUI.c`, MENU_BZ_REPRODUCTOR id 51213): lists routes (`EnumerateRoutes` = `BZBusRoute*.json` excluding `_hdr`/tmp), LOAD&SPAWN (`RespawnFromPath`) without restart. Pretty() strips `BZBusRoute_`+`.json`.
**Layouts:** **CPP-style** format (`PanelWidgetClass {...}`), NOT XML/library (crashes CreateWidgets). Ref: BrigadaZRadio/gui/layouts. .paa textures (PNG→PAA pipeline with ImageToPAA); "baked image + overlay" pattern.
**RPC (`BZBusRPC.c`):** enum with `RECEIVE_TOAST` (notification to clients), slots, stop/pause, telemetry. `BroadcastGlobal(msg)` iterates players and sends `ScriptRPC` with `BZBusRPC.RECEIVE_TOAST` → each client does `NotificationSystem.AddNotificationExtended(6, "BZ_AutoDrive", msg, "")`.
**UI pending:** rebinding keys from the mod's own UI (today it is done from the game's Controls menu, see D.1); dynamic list vs fixed slots.

---

## 11. Programming / extending — code patterns

**Language:** Enforce (scope 3_Game→4_World→5_Mission; shared helpers at the earliest scope). Build: AddonBuilder does NOT validate Enforce (only the runtime/RPT does).

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
**(e) As a dependency (another mod)** → `requiredAddons[] = {"BZ_AutoDrive"}` + `BZBusService.GetInstance().RespawnFromPath("BZBusRoute_X.json")` / `RespawnAs(class)`.

**Car API (writing):** `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. **(reading):** `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (→wheelbase), `WheelGetSurface(i)`, `EngineGetRPMRedline()`, `GetGearCount()` (NOT `GetGearsCount`, obsolete). Config: `GetGame().ConfigGetFloat/Array("CfgVehicles/<cls>/...")`.

**CfgVehicles vs script classes:** `SurvivorM_*` is config, not script; in `.c` inherit from SurvivorBase/ManBase/ItemBase/House, never from the CfgVehicles entry. Engine classes (CGame) are NOT moddable.

---

## 12. Gotchas and errors + fixes (CRITICAL — do not repeat)

**Enforce:**
- No ternary `?:`; no multiline `if` with `&&` at the start of a line → "Syntax error". Single line or intermediate bools.
- No `Math.PI` nor `Math.AbsFloat`.
- `new Class(args)` blows up → create empty + set fields.
- "Formula too complex" at ~9 operands with `+` → split with `+=`.
- Scope of sibling branches (if/else, case) COLLIDES (not like C++) → rename/hoist.
- Inline arithmetic in string concat (`"x"+(seat-1)`) breaks → hoist to an int.
- Ternary/arithmetic inside concat also breaks.
- `PlayerBase.Cast(driver)` returns true for eAI → discriminate the real player by `GetIdentity()`.
- Left-handed cross product: `cross = AB.z·AP.x − AB.x·AP.z` (inverted = divergence).
- **Nonexistent widget method** (e.g. `MultilineTextWidget.SetLineColor`): **compiles** but on load throws *"Undefined function"* and **kills the Mission module** → the UI won't open. `MultilineTextWidget` has no per-line color: paginate with individual `TextWidget`s (`TextWidget.SetColor` DOES exist). Verify the widget method exists in the API before using it.
- AddonBuilder does not detect any of this; confirm in the RPT when loading the server.

**Pipeline/data:**
- **(historical) the old `csv_to_route` left the OLD `_hdr/_wp` pair** → `frame_to_route.py` already writes all three. If it spawns the previous vehicle: re-convert with the wizard + sync the PAIR to B.
- **Attachments:** never guess; from the fingerprint/header. When changing VehicleClass, replace (the converter preserves the old ones).
- **LoadConfig at runtime blocks the main thread** (JSON 2.6MB = 108s = client disconnect) → load only in Init(); the fast-load pair (FGets) is for hot reload.
- **PowerShell Set-Content/Out-File -Encoding UTF8 adds a BOM** and breaks the Expansion/DayZ parser → use `[IO.File]::WriteAllText` with `UTF8Encoding($false)`. For edits that preserve accents/§: read/write Latin1 (codepage 28591) or use the editing tool.
- **Reports tooling (`report_export.ps1`) — PS5.1 encoding subtleties (the opposite of the above):** a `.ps1` with **accents in its own code/strings** needs **UTF-8 WITH BOM** (PS5.1 reads a BOM-less .ps1 as ANSI → mojibake in the report texts). Do NOT confuse with the files the mod consumes (those go WITHOUT BOM). The intermediate data JSON the script builds is written **without BOM** (it is read by the HTML/Edge). Also: passing `@($obj)` to a `NoteProperty` **breaks** → use `.ToArray()`.

**Build/deploy:**
- When moving/renaming/deleting a `.c`, clear AddonBuilder's `temp/` folder before repacking (otherwise it packages old versions; "zombies in temp"). If your pipeline copies the PBO to a local server, close the server and client first (while open they lock the PBO → the copy fails).
- `-DeployClient` leaves an `@<old>` stray in `!Workshop` on rename → delete it.
- meta.cpp in `MODS\` is not touched by the source replace → migrate `name` by hand.

**Client/launcher:**
- **DZSALauncher caches its mods IN MEMORY**: editing `%LOCALAPPDATA%\DayZ Launcher\Local.json` (knownLocalMods/userDirectories) does NOT take effect until you EXIT COMPLETELY (tray→Exit; closing the window is not enough). Symptom: "The client has a PBO that is not on the server." The `meta.cpp` defines the `name` it shows; two local mods with the same name get confused.
- The client loads from `C:\DayZServer`, without !Workshop, without subscription (deliberate for local testing).

**VISUAL state on an observed AI car (NETWORK gotcha — GOLD):**
- **`LightOn/LightOff/LightIsOn/LightToggle` are proto native of `Transport` = a LOCAL engine flag, NOT a NetSyncVariable.** The engine only sets them on the client that **SIMULATES** the car (owner / player in the driver seat). An **observer of an AI car** (Boris drives, no player in seat 0) **NEVER** sees `LightIsOn()==true` → `UpdateLightsClient` never creates `m_Headlight` → **the beam is not visible** even though the server "turns on" the lights.
- **Attempts that did NOT work:** `ToggleHeadlights()` alone; `ToggleHeadlights() + ForceUpdateLightsStart/End` (re-fires `UpdateLights()` but `UpdateLightsClient` still sees `LightIsOn()==false`).
- **FIX v3 (in `BZBusCarScript.c`, the `modded CarScript`):** a custom NetSync **`m_BZLightsWanted`** (`RegisterNetSyncVariableBool` in the ctor) + server method **`BZSetLights(bool)`** (server: native toggle + `SetSynchDirty()`) + override **`OnVariablesSynchronized()`** that, **on the CLIENT**, forces `LightOn()/LightOff()` + `UpdateLights()` per `m_BZLightsWanted`. Use `LightOn/Off` and **NOT `LightToggle`** (that one goes through `OnBeforeLightOn`, which checks the battery — not synced on the observer).
- **The horn does NOT suffer this:** `SetCarHornState` already uses `RegisterNetSyncVariableInt` + `SetSynchDirty` in vanilla → it syncs to the observer on its own.
- **GENERAL LESSON (apply to any future visual feature):** any **VISUAL** state the server wants to show on an **observed AI car** (lights, body animations, etc.) needs its **own NetSyncVariable** + forcing it **client-side** in `OnVariablesSynchronized`. The engine only replicates the state to the client that SIMULATES the vehicle; the rest do not see it unless you sync it yourself.

**Others:**
- ~38 mod limit (Steam query ~255 bytes); 40 breaks with "Server can't transmit all data".
- New mod = copy its `.bikey` to `keys/` (otherwise the client says "Missing PBO").
- No automatic A↔B sync (deliberate manual copy; sync tools failed before).
- XML layouts crash → CPP-style.
- Objects from the binary .map (wrecks) cannot be deleted with ObjectDelete → `GetSuppressedObjectManager().Suppress(obj)` (requires @DayZ Editor Loader) or Expansion's declarative mapping.

### 12.A — Catalog of real errors we had → how we solved them

This is what actually happened to us and cost us time. When an admin/modder reports a similar symptom, map it directly to the fix.

**Compile / script (Enforce):**

| Symptom / message | Cause | Fix |
|---|---|---|
| **"Syntax error"** pointing at the class line + an internal line | multiline `if` with `&&` at the start of a line, ternary `?:`, or inline arithmetic in string concat (`"x"+(seat-1)`) | a single line; or hoist to an intermediate bool/int beforehand |
| **"Formula too complex"** | >~9 operands chained with `+` (typical when building CSV/log strings) | split into several assignments with `+=` |
| Variable "already defined" or value overwritten between branches | the scope of sibling branches (if/else, case) **collides** (it is not like C++) | rename, or hoist the variable above the block |
| **"Unknown type"** when inheriting from `ExpansionQuestObjectiveAICampEvent` in scope 5_Mission | the objective subclass does not resolve in that scope | do NOT inherit the objective; use the hook `modded MissionServer Expansion_OnQuestStart` + poll `QuestPatrolExists` |
| Crash when **loading** the server after `modded class CGame` (or another engine class) | engine classes do not allow `modded class` | do not mod engine; control goes through override-last in `CarScript`, hooks through `MissionServer` |
| The constructor blows up (`new BZAction("verb")`) | Enforce does not take args in constructors of simple classes | create empty and set fields (`a = new BZAction(); a.verb = "x";`) |
| Warning **"GetGearsCount is obsolete"** | old API | use `Car.GetGearCount()` |
| **The PBO compiles OK but the server does not start / throws errors** | **AddonBuilder does NOT validate Enforce** (it packages anyway with syntax errors) | read the **RPT** on load — that is where the real errors show up with file:line |
| The PBO packages OLD versions of a `.c` after renaming/moving/deleting | AddonBuilder reuses the temp | clear AddonBuilder's `temp/` folder before repacking |
| `CreateWidgets` crashes when opening a UI | layout in XML/library format | rewrite the layout in **CPP-style** format (`PanelWidgetClass {...}`) |

**Runtime / logic:**

| Symptom | Cause | Fix |
|---|---|---|
| An event fires with `type=''` (phantom trigger) | `JsonFileLoader` instantiates an empty default `BZTrigger` when parsing | account for/filter the empty default (see `feedback_jsonfileloader_self_referential`) |
| The framework treats the real player as a bot (or vice versa) | `PlayerBase.Cast(driver)` returns **true also for eAI** | discriminate the real player by `GetIdentity()` |
| Stanley corrects to the wrong side; the bus self-steers into the water in ~80 s | inverted cross-product sign (DayZ left-handed system) | `cross = AB.z·AP.x − AB.x·AP.z` |
| `ValidateSpawn` gives a false OK or the retry fails | it measured distance from the spawn, not movement | measure `kmh > 0.5` (the vehicle responds) |
| Boris starts OK and after ~20 s jumps 3 km / appears under the sea | `SmoothPath()` divided by a hardcoded 0.2 → `window≠5` scaled all the wps (bug 7B9D0036) | fixed; also `PathSmoothWindow=0` on routes with tight curves |
| Boris cuts the curves on the inside | the centroid of N wps for the lookahead falls inside the arc / high smoothing | interpolate **over** the path; `PathSmoothWindow=0` |
| `check_once` broke the JSON load | poorly supported branching verb | removed from the DSL; conditional branching is Quest's job |
| Bot stays a "mannequin" / boards by teleport instead of walking | the eAI FSM does not navigate with an active threat; or `FormationState=NONE` (halt) | pacify (`eAI_SetPassive(true)` + threat 0 + drain targets); `FormationState=IN` + `AddWaypoint` + `ForceRecalculate` |

**Build / deploy / client (the ones from the migration session):**

| Symptom | Cause | Fix |
|---|---|---|
| `robocopy` to `C:\DayZServer` exit **8/9** | the server OR the client open lock the PBO | close **both** before building/mirroring; ask "open or closed?" |
| After deploying a NEW route, it spawns the **previous vehicle** | OLD `_hdr.json`/`_wp.csv` pair (the framework **prefers the pair**); happens with old flows — `frame_to_route.py` already writes all three | re-convert with the wizard (writes the trio) + sync the PAIR to B (not just the `.json`) |
| Client kicked: **"The client has a PBO that is not on the server"** | the DZSALauncher caches its mods IN MEMORY + old copies remained (`Local.json`, `!Workshop\@old`) | **EXIT COMPLETELY** from the launcher (tray → Exit) after editing `Local.json`; delete the old copies of the mod |
| "Missing PBO" on connect | the mod's `.bikey` is missing from `keys/` | copy the `.bikey` to `keys/` (A and B) |
| The launcher kept loading the old mod even though I edited the file | the `meta.cpp` had the old `name` (two local mods with the same `name` get confused) + the launcher does not re-read until restart | migrate the `name` in `meta.cpp`; restart the launcher completely |
| Server boot ~3 min | huge route JSON parsed in `Init()` | split the route / use the fast-load pair (FGets); lazy-load (future) |
| 40 mods → "Server can't transmit all data" | ~38 mod ceiling (Steam query ~255 bytes) | remove non-essentials or consolidate small PBOs |

---

## 13. Build & deploy

**To use the mod:** subscribe on the Workshop (it ships signed). **To build a fork:** package `BZ_AutoDrive.pbo` (prefix `BZ_AutoDrive`) with DayZ Tools (AddonBuilder) or your own pipeline and sign it with your **own** key; copy your `.bikey` into the server's `keys/` folder and list `@BZ_AutoDrive` in the start.bat `-mod=` line. No build script ships — each modder packages their own way. **Universal gotcha:** when renaming/moving/deleting a `.c`, clear AddonBuilder's `temp/` folder before repacking (otherwise it packages stale "zombie" versions). Pre-Workshop: swap-and-test with `publishedid=0` + clean RPT + smoke test before uploading.

---

## 14. Research methodology (how to continue)

**The loop (manual ILC):** record 3 AI runs (`ai_run` check in the Reproductor) → measure lateral deviation vs the human recording per wp → identify systematic clusters (not noise) → hypothesize a technical cause → design a fix that does NOT violate the commitments (do not modify eAI, do not break generalization) → validate with a new take.

**Analyzing an ai_run:** key columns `wp_idx`, `lateral_dev_m`, `speed_kmh` vs `target_speed`, `steering`, `gear`, `mode`. Look for: steering saturations (|steer|→1), sign changes (zigzag), speed deficit (Boris −10 km/h vs target = off-path→slow→off-path cascade), stuck (full throttle + speed 0 = wedged, without AR it does not get unstuck). **Lateral bias:** use the MEDIAN + segment straight/curve (the signed-avg deceives due to outliers/saturations).

**Diagnosis→fix heuristics (the wizard's knowledge base):** lateral bias → `CruiseLateralCenterOffset` (~25m/unit); steering saturation → lower `SteeringScale`; lugging → `GearStrategy=follow_recording`; zigzag → `CruiseLateralDamp`/`TargetSpeedSmoothWindow`; cuts curves → `PathSmoothWindow=0`; off-path slow → AutoRecovery + smart throttle cap; pre-curve jerks → binary brake (0/1 keyboard) → cascade (brake rate limiter pending).

**Stable workflow:** record → stop server → deploy JSON → start server → test. Do NOT iterate with NUMPAD 2 over a live server if you changed code. For JSON-only: record→deploy→NUMPAD 2 without restart works. ALWAYS tick the **`ai_run`** check (opt-in, in the Reproductor) in tests, otherwise there is no ai_run.

**Guiding principles:** (1) config as manual; (2) spatial > temporal fidelity (arrive in order, not on time); (3) framework = extractor of the optimum (Boris-vs-human diff = info, not a bug); (4) path > inputs (the trajectory is sacred, the inputs are noisy); (5) recording = the vehicle's manual (cross-vehicle invalidates it); (6) dual audience (wizard + override).

---

## 15. The open frontier (what to research next)

- **Endpoint (final stop) sub-0.5 m — RESOLVED (2026-08-11, no longer a frontier):** the per-vehicle+surface auto-adaptive stopping brake nails the stop **under 0.5 m for most** (up to ~1 m on the large / long-wheelbase ones), forward and reverse, validated out-of-sample (§2). Includes the endpoint-after-a-curve, a historical weak spot (SEQ1 at 44 km/h → 0.34 m). The only thing still open here is **reverse on a tight curve** (next bullet).
- **Reverse on tight curve:** close the ~1 m of divergence. Options: read `WheelGetContactPosition` of the real rear axle (not the wheelbase/2 approximation of the tandem), model the 3D slope, more fine-correction authority. Or apply **reference-assisted control to reverse** (steering by path+config instead of replay; the short 2-axle UAZ is the best test case). The forward→reverse transition (brake-to-0 → R lever → controller) is where Boris gets stuck.
- **Extreme driving (v3):** drifts/countersteer. Custom physics override (bypassing the eAI receiver on those stretches) or an ML residual. The current honest limit.
- **Fully autonomous wizard:** internalize §14 (post-run auto-analysis + auto-apply of heuristics).
- **Multi-vehicle:** convoy with spacing via a breadcrumb trail (ARMA setConvoySeparation style).
- **LLM-driven NPC:** the framework ALREADY is LLM-shaped — the event DSL = action space, the graph = navigation API, config-read = vehicle perception. The LLM governs (slow, high-level) and the reactive skills execute (fast). Provider-agnostic design. Broader vision: an **autonomous survivor** (Voyager-for-DayZ) where driving is ONE skill.
- **Trains:** ~70% covered without changes (rails = network, no steering, only throttle+brake).
- **Other engines:** the principle (demonstration + config-read + classic control) is portable.

---

## 16. Glossary and key references

- **eAI** = DayZ-Expansion-AI (the base AI). **Boris** = the NPC driver (eAI_SurvivorM_Boris). **Override-last** = running OnInput after eAI. **Fingerprint** = vehicle data captured at recording time (header_*.txt). **R_min** = minimum turning radius = wheelbase/tan(maxSteer). **Corridor/paredón** = the lateral control dead band. **BZ-Score** = 0–100 score (standalone **reports** tooling; the converter wizard no longer computes it). **Runner** = a running vehicle instance. **Triple match** = A=B=client same PBO.
- **Demo classnames:** CarPack vehicles (viper_yellow, x5mcompetition_orange, Star_APC_Cobra_white, Star_Golf_MK1, etc.), UAZ_452 (mod @[CnG]UAZ_452), vanilla (Hatchback_02, M3S/V3S), **CivilianSedan_Wine** (EXAMPLE02: 3rd example vehicle, replay (same vehicle) with human lights+horn — 98.6 % completion, lat-dev 0.79 m).
- **The cut rule (§2/§5), conceptual:** direct-replay of a maneuver reproduces VEHICLE-SPECIFIC steering angles (same angle → radius ∝ wheelbase), so it **generalizes to another vehicle only if the cut into the maneuver block lands where the trajectory is STRAIGHT** (the curve stays closed-loop Stanley, vehicle-agnostic; the straight goes in replay). Cut in a curve → open-loop → a different wheelbase drifts → does not generalize. *(The mod does NOT ship example takes: a recorded route is coordinates of a specific map.)*
- **Sibling docs:** the **manual** (`MANUAL_BZ_AutoDrive.md`, didactic for admin/modder). The project **memories** (`%USERPROFILE%\.claude\projects\c--DayZServer\memory\`) have the fine-grained history, milestones and feedbacks — `project_MASTER_CONTINUITY.md` is the cold-start guide.

---

## Appendix C — Faithful code listings (by component)

> Real excerpts from the source (exact method/field names; long bodies trimmed with `// ...`). For the full detail, grep the cited file.

### C.1 `BZBusConfig.c` — the DSL classes

```c
class BZBusRouteConfig {
    int    RespawnDelay = 300;        float AverageSpeedMS = 11.0;   float SpawnHoldSeconds = 3.0;
    string VehicleClass = "ExpansionBus";   string DriverClass = "eAI_SurvivorM_Boris";
    bool   VehicleInvincible = true;  string ConvoyMode = "";        int   MaxGear = 6;
    ref array<string> Attachments = new array<string>();
    // speed/mode
    string GearStrategy = "auto_box"; bool FollowPath = false; bool FollowPathUseReference = false;
    float  FollowPathLatAccel = 4.0;  float FollowPathMaxKmh = 50.0;
    bool   UseInverseModel = false;   float InverseModelKp/Ki/Kd = -1;  bool InverseModelLowRpmMin = false;
    int    TargetSpeedSmoothWindow = 0;  float AccelShiftThreshold = 999.0;
    // steering
    float  SteeringScale = -1;  int PathSmoothWindow = 5;  float CurvatureSteerBoost = 0;
    float  CruiseLateralDeadband = 0; float CruiseLateralKGain = 1.0; float CruiseLateralDamp = 0; float CruiseLateralCenterOffset = 0;
    float  CruiseFFWeight = -1;  bool CurveThrottleEnabled = true; /* +LookaheadM/StartDeg/FullDeg/MinScale */
    // slope / recovery
    bool   SlopeCompensationEnabled = true; int SlopeLookaheadWps = 5; float SlopeGain = 1.0; float SlopeLateralGain = 1.0;
    bool   AutoRecoveryEnabled = false; float AutoRecoveryStuckTimeS = 10.0; int AutoRecoveryAdvanceWps = 5;
    // maniobra / parking / reverse
    float  ManiobraTargetSpeedCap = 18.0; bool ModeEntrySnapEnabled = false; bool AntiRollbackEnabled = true; // ModeEntrySnap: false since 2026-07-03 (was true)
    float  ParkingStanleyK = -1; float ParkingFFWeight = -1;  float Wheelbase = 0;
    float  ReverseStanleyK = -1; float ReverseRecordedSteerThreshold = 0; /* +Reverse* gates */
    int    EndFreezeDisabled = 0;
    // lights / horn (Phase 2 replay) — the RECORDED horn/light replays per wp; these control the AUTOMATIC mode
    string LightsMode = "replay";   // replay / auto / auto_inverted / on / off
    string HornMode   = "replay";   // replay / stops / finish / off
    // content
    ref array<ref BZMarkerEvent> Events = new array<ref BZMarkerEvent>();
    ref array<ref BZCrewMember>  Crew   = new array<ref BZCrewMember>();
    ref array<ref BZWaypoint>    Waypoints = new array<ref BZWaypoint>();
}

class BZAction {                 // a verb + its parameters (union struct)
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
        super.OnInput(dt);                       // eAI runs first (forces FIRST)
        if (!GetGame().IsServer()) return;
        BZBusService srv = BZBusService.GetRunnerForCar(this);
        if (!srv) return;
        Human driver = CrewMember(0);            // if seat 0 is a real PLAYER, do not touch
        if (driver) {
            PlayerBase realPlayer = PlayerBase.Cast(driver);
            if (realPlayer && realPlayer.GetIdentity()) return;   // discriminate by GetIdentity!
        }
        srv.ApplyBusInput(this, dt);             // overrides throttle/steer/brake/handbrake
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) ShiftTo(desired);   // overrides eAI's FIRST
    }
}
```

### C.2b `BZBusCarScript.c` — lights synced on an observed AI car (network gotcha, §12)

```c
modded class CarScript {
    bool m_BZLightsWanted;   // NetSync: the light state the SERVER wants on the clients

    void BZBusCarScript_ctor() { /* in the real ctor: */
        RegisterNetSyncVariableBool("m_BZLightsWanted");   // script-sync channel (replicates to ALL clients)
    }

    void BZSetLights(bool on) {                 // SERVER: called by BZBusService per waypoint
        if (!GetGame().IsServer()) return;
        if (on) LightOn(); else LightOff();     // native local toggle on the server
        m_BZLightsWanted = on;
        SetSynchDirty();                         // fires OnVariablesSynchronized() on the clients
    }

    override void OnVariablesSynchronized() {    // runs on ALL clients (incl. the one that only OBSERVES)
        super.OnVariablesSynchronized();         // the base calls UpdateLights() at the end
        // The engine does NOT set LightIsOn() on an AI-car observer -> we force it by hand:
        if (m_BZLightsWanted && !LightIsOn())  { LightOn();  UpdateLights(); }   // creates m_Headlight
        else if (!m_BZLightsWanted && LightIsOn()) { LightOff(); UpdateLights(); }   // FadeOut + null
    }
}
// NOTE: use LightOn/LightOff (NOT LightToggle: it goes through OnBeforeLightOn which checks the battery,
//       not synced on the observer). The horn does NOT need this (SetCarHornState is already NetSyncVariableInt).
```

### C.3 `BZBusService.c` — control injection

```c
void ApplyBusInput(Car bus, float dt) {
    if (!bus) return;
    bus.SetThrottle(m_CachedThrottle);
    bus.SetSteering(m_CachedSteering);
    bus.SetBrake(m_CachedBrake);
    bus.SetHandbrake(m_CachedHandbrake);   // anti-rollback on slope
}
// DriveTowards(...) computes Stanley over an ADAPTIVE lookahead and caches the inputs:
//   targetYaw = segmentHeading - atan(K * lateralOffset / velocity)   (corridor, no step)
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
    else if (verb == "ui_broadcast")      { BroadcastGlobal(action.msg); }
    else if (verb == "spawn_guard")       { SpawnGuards(car, action.count>0?action.count:1, action.faction, action.loadout, action.value); }
    else if (verb == "dismount_guard")    { DismountCrew(car); }
    else                                  { BZBusLog.Warn("verb not implemented: '" + verb + "'"); }
}
```
*(The `delay` is scheduled by the caller with CallLater; this dispatcher executes the action once de-timed.)*

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
        else if (!m_QuestFleeing && totalBots < m_QuestInitialBots) { m_QuestFleeing = true; BoardQuestBots(); }  // count dropped = one was killed
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
            if (seat > 5) break;                          // 5 passenger seats
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m)); if (!b) continue;
            // PACIFY: eAI_SetPassive(true) + eAI_SetThreatDistanceLimit(0) + drain targets
            // then emit a WALK waypoint to the door (not teleport); the Tick boards it
            m_Crew.Insert(b); m_CrewSeats.Insert(seat); seat++;
        }
    }
}
```

### C.5b `BZBusService.c` / `BZBusCarScript.c` — convoy ambush and cleanup

Complement to C.5. The `ambush_on_damage` scene uses **instant** boarding (no walk-in), **two** damage triggers, a hostile dismount, and a completion hook that despawns the vehicle.

**Instant boarding + arming Boris (`BoardAmbushBots`):**
```c
void BoardAmbushBots() {
    if (!m_Bus) return;
    Transport transport = Transport.Cast(m_Bus);
    if (!transport) return;
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
    // ... lazy init of m_Crew / m_CrewSeats / m_CrewBoard / m_CrewLastHealth ...
    int maxSeat = transport.CrewSize() - 1;   // seat 0 = Boris; 1..maxSeat = passengers
    for (int i = 0; i < patrols.Count(); i++) {
        eAIQuestPatrol p = patrols[i];
        if (!p || !p.m_Group) continue;
        for (int m = 0; m < p.m_Group.Count(); m++) {
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
            if (!b || m_Crew.Find(b) >= 0) continue;          // idempotent
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
        ExpansionHumanLoadout.Apply(m_Driver, "BanditLoadout", false);   // Boris armed
        m_BorisArmed = true;
    }
    // lazy activation: once everyone is aboard -> m_AmbushActive = true
    int liveBots = 0;
    for (int pi = 0; pi < patrols.Count(); pi++)
        if (patrols[pi] && patrols[pi].m_Group) liveBots += patrols[pi].m_Group.Count();
    int target = maxSeat; if (liveBots < target) target = liveBots;
    if (!m_AmbushActive && target > 0 && m_Crew.Count() >= target) m_AmbushActive = true;
}
```

**Damage triggers (`NotifyConvoyDamaged` + `EEHitBy` + health poll):**
```c
// One-shot: the first damage freezes the vehicle and schedules the deployment.
void NotifyConvoyDamaged() {
    if (!m_AmbushActive || m_AmbushTriggered) return;
    m_AmbushTriggered = true;
    m_Frozen = true;                          // handbrake + brake = HARD stop
    m_AmbushStopTries = 0;
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.AmbushWaitStop, 400, false);
}

// Trigger 1 — chassis damage (BZBusCarScript.c). The chassis delegates to its OWNER runner (multiton):
override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source,
                      int component, string dmgZone, string ammo, vector modelPos, float speedCoef) {
    super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
    if (!GetGame().IsServer()) return;
    BZBusService srv = BZBusService.GetRunnerForCar(this);   // multiton: owner of this car
    if (srv) srv.NotifyConvoyDamaged();
}

// Trigger 2 — a passenger is shot (health poll inside CheckQuestBots):
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
    int dc = DismountCrew(Car.Cast(m_Bus));                  // drops the passengers
    if (m_Driver) {
        eAIGroup bg = m_Driver.GetGroup();
        if (bg) bg.SetFaction(eAIFaction.Create("Mercenaries"));   // hostile to player, no FF with convoy
        HumanCommandVehicle bc = m_Driver.GetCommand_Vehicle();
        if (bc && !bc.IsGettingIn()) {
            int bseat = bc.GetVehicleSeat();
            bool bhd = false; string bds = "";
            ExpansionFSMHelper.DoorAnimationSource(m_Bus, bseat, bhd, bds);
            if (bhd && bds != "") m_Bus.SetAnimationPhase(bds, 1.0);   // opens his door
            bc.GetOutVehicle();
            m_Driver.SetAllowDamage(true);                   // Boris now mortal
        }
    }
}
```

**Completion / despawn (`OnQuestComplete`):**
```c
void OnQuestComplete(ExpansionQuest quest) {
    if (!quest) return;
    ExpansionQuestConfig qc = quest.GetQuestConfig();
    if (!qc || qc.GetID() != m_QuestCheckID) return;          // only the managed convoy
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckQuestBots);
    m_QuestCheckID = -1;
    m_QuestConvoyActive = false; m_QuestFleeing = false; m_QuestInitialBots = 0;
    CleanupEntities();    // deletes vehicle + Boris, removes Tick, NO auto-respawn
}
```

**Implementation notes:**
- **Two damage triggers** (chassis via `EEHitBy` + passenger health via the `CheckQuestBots` poll); either one arms the ambush exactly once (`m_AmbushTriggered`).
- `EEHitBy` does not know its runner: it resolves it via `GetRunnerForCar(this)` (the **multiton** pattern — the car belongs to a `BZBusService`).
- Boris: `BanditLoadout` on boarding; on dismount his group switches to the **Mercenaries** faction (hostile to the player, no friendly-fire with the convoy) and only then `SetAllowDamage(true)`.
- Sequence: damage → `NotifyConvoyDamaged` (freeze) → `AmbushWaitStop` (waits for the stop) → `AmbushDismount` (deployment). Boarding was instant (`BoardAmbushBots`), unlike the pacified walk-in of the `flee_on_kill` scene (C.5).
- `OnQuestComplete` acts only on `m_QuestCheckID` (the managed convoy), removes the poll and despawns with no auto-respawn — avoids ghost vehicles.

### C.6 `BZInverseModel.c` — PID + inverse + gear

```c
class BZInverseModel {
    float m_Mass, m_WheelRadius, m_DragCoef, m_FrontalArea, m_RPMIdle, m_RPMRedline, m_FinalDrive;
    ref array<float> m_TorqueCurve;   // [RPM,Nm,...]    ref array<float> m_GearRatios;   // forward
    ref array<float> m_PressureBySpeed;  float m_DrivenAxleWeightRatio;
    float m_PIDIntegral, m_PIDPrevError, m_PIDKp = 0.4, m_PIDKi = 0.05, m_PIDKd = 0.0;
    const float AIR_DENSITY = 1.225, G = 9.81, ROLLING_COEF = 0.015;

    void LoadFromConfig(string vehicleClass, Car bus) { /* reads CfgVehicles + runtime (EngineGetRPM, dBodyGetMass) */ }

    float ComputeDesiredAccel(float targetKmh, float curKmh, float dt) {     // Layer 3: speed PID
        float err = targetKmh - curKmh;
        m_PIDIntegral = Math.Clamp(m_PIDIntegral + err*dt, -30, 30);          // anti-windup ±30
        float dErr = (err - m_PIDPrevError)/dt; m_PIDPrevError = err;
        return (m_PIDKp*err + m_PIDKi*m_PIDIntegral + m_PIDKd*dErr) / 3.6;    // km/h/s -> m/s²
    }
    void ComputeInputs(float desiredAccel, int gear, float kmh, float slope, float surfFric, float surfRoll,
                       out float outThrottle, out float outBrake, out string note) {        // Layer 4
        float v = kmh/3.6;
        float natural = 0.5*m_DragCoef*m_FrontalArea*AIR_DENSITY*v*v + ROLLING_COEF*m_Mass*G*surfRoll + m_Mass*G*slope;
        float needF = desiredAccel*m_Mass + natural;
        if (needF > 0) { /* RPM->torque, cap by traction (m*G*fric*axleRatio), throttle = torque/maxTorque */ outBrake = 0; }
        else           { /* pressureBySpeed -> maxBrakeF, cap by traction, brake = decel/maxBrakeF */ outThrottle = 0; }
    }
    int SelectGear(int gear, float kmh, float targetKmh, float desiredAccel) {                // Layer 5
        if (GetGame().GetTickTime() - m_LastShiftTime < SHIFT_LOCK_S) return gear;  // hysteresis 2s
        if (kmh < 5) return 2;                                                       // FIRST (DayZ gear=idx+2)
        // highest gear with RPM in band [m_RPMIdle*1.3, m_RPMRedline*0.85]; aggressive downshift if desiredAccel>2.5
    }
}
```

### C.7 `BZPathLogService.c` — recording (50 Hz) + fingerprint

```c
static const float SAMPLE_INTERVAL_MS = 20;   // 0.02s = 50 samples/s   (50 Hz, not 10!)

private void WriteSample(bool isStop) {
    Car car = Car.Cast(GetGame().GetPlayer().GetParent());
    // reads: GetPosition, GetDirection, GetSpeedometerAbsolute, GetGear, GetBrake/Throttle/Steering,
    //        EngineGetRPM/Redline, GetVelocity, GetClutch, GetHandbrake,
    //        + horn (GetCarHornState) and lights (LightIsOn) via CarScript.Cast(parent)   // Phase 2 replay
    // CSV (19+ cols): time_s,x,y,z,heading_deg,speed_kmh,is_stop,gear,throttle,brake,steering,
    //                 rpm,redline_rpm,mode,vx,vy,vz,clutch,handbrake,...,horn,lights
}
private void WriteVehicleHeader() {   // sidecar: path_<ts>.csv -> header_<ts>.txt
    FPrint(h, "vehicleClass=" + car.GetType());      FPrint(h, "mass=" + dBodyGetMass(car));
    FPrint(h, "engineRPMIdle/Max/Redline=" + ...);   FPrint(h, "gearsCount=" + car.GetGearsCount());
    // attachments: iterate GetInventory().GetAttachmentFromIndex(i).GetType() -> "attachments=a,b,c"
    FPrint(h, "maxSteeringAngle=" + GetGame().ConfigGetFloat("CfgVehicles "+car.GetType()+" SimulationModule Steering maxSteeringAngle"));
    // wheelbase: project WheelGetContactPosition(i) onto GetDirection() -> max-min
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
*(Internal reference: the enum includes **advanced/retired tooling** RPCs — `REQUEST_AI_LOG_TOGGLE`, `REQUEST_SYSID_STEP`/`REQUEST_SYSID_CURVE`, `REQUEST_RESPAWN_SLOT` correspond to debug/SysID features no longer in the published flow. `RECEIVE_PANEL_SETTINGS` NEVER sends the admin list — only the key + `esAdmin` of the one asking. `BORIS_CHAT_*` = the LLM-driven NPC experiment.)*

---

*Pending for this pack: figures. **Correction to propagate:** the PathLogger records at **50 Hz** (not 10) — update the manual. Keep it synced with the code as it evolves.*


---

## Appendix D — Quick reference (keybindings, defaults, ai_run, JSON, guides)

Reference material so you can work without grepping the code. Source: direct read of the source 2026-06-22 — **the code is the truth; if they diverge, the code wins.**

> **Index of AI-actionable guides:** D.5 (new verb) · D.6 (new vehicle) · D.7 (map an area) · D.8 (a vehicle into a Quest) · **D.10 (diagnose a run / `ai_run`)**. For the raw `ai_run` format see **D.3**; for the calibration methodology (ILC loop, symptom→fix heuristics) see **§14**.

### D.1 — Keybindings (the Controls-menu input system)

Migrated to **DayZ's native input system** (no longer `OnKeyPress`). The actions are declared in `data/inputs.xml` (`<modded_inputs>` → `<actions>` with name+loc EN · `<sorting name="BZ_AutoDrive" loc="BZ AutoDrive">` = the tab in `Options → Controls` · `<preset>` with the defaults), referenced in `config.cpp` (`inputs = "BZ_AutoDrive/data/inputs.xml";`). `build_include.lst` includes `*.xml` (otherwise AddonBuilder silently excludes it). Rebindable from the menu; admin-only.

**Detection:** polled in the client's `OnUpdate` (`BZBusMissionGameplay.c`) with `GetUApi().GetInputByName("UABZ...").LocalPress()`; client-side → server via RPC (validated server-side by `IsControlPanelAdmin`). **Critical guard** (otherwise the actions fire while rebinding a key): `if (GetGame().GetUIManager().GetMenu() || GetGame().GetUIManager().IsDialogVisible()) return;` before the poll — NOT `HasGameFocus()` (= OS window focus, still `true` with a menu open).

**Defaults design (2026 redesign):** the "BZ AutoDrive" Controls section was reduced to **3 inputs, all 3 bound** — the whole core loop. The ~16 old debug/tuning/mode controls were removed (Parking/Reverse/Maneuver Mode, AI Logger, SysID, Spawn Slots, Pause, Mark Gear/Max…), and so was the old **interchange key** (`UABZMarkLeg`/NUMPAD 3, removed 2026-08-11): maneuvers come from the **auto-detected interchange** (forward↔reverse gear change), reverse is **auto-detected from the gear**, and `ai_run`/`boris_native` are armed from the **Reproductor checks**. The controls are **admin-only** (`IsControlPanelAdmin` gate). Labels LITERAL in English (the inputs menu does NOT resolve stringtable).

| Action (label EN) | `UAName` | Default | Notes |
|---|---|---|---|
| Open Control Panel | `UABZAutoDrivePanel` | `kHome` | opens the Reproductor / panel |
| Record (start/stop) | `UABZRecord` | `kNumpad5` | PathLogger **+ FrameRecorder** toggle |
| Mark Event / Stop | `UABZMarkStop` | `kNumpad4` | marks stop / event node (marks **both** recorders) |

> **Interchange (`legBreak`), no key:** the leg break for a direction change is **auto-derived** from the forward↔reverse gear change (always at ~0 km/h) in `frame_to_route.py`; the editor also marks it on the node on export. A 0 km/h **without** a direction change = pause, not interchange.

> **Key names** (preset): `kHome`, `kNumpad0`–`kNumpad9` **confirmed**. Trick to discover names: read the user's preset after binding by hand (`Documents\DayZ\<profile>.dayz_preset_User.xml`).
> `F` (public) and `ESC` are unchanged (bus-stop UI / close). The old NUMPAD . double-bind conflict is now **resolved** by reducing to 3 keys.

### D.2 — `BZBusRouteConfig` defaults (exact values)

Source: `scripts\4_World\BZBusConfig.c`. Convention: `-1` (and sometimes `0`) = "use the internal code constant / derive from the vehicle".

**Driving control (ADVANCED — the wizard produces a single control; these flags are for a modder who wants other behavior, §5)**
| Field | Default | What it does |
|---|---|---|
| `FollowPath` | `false` | `true` = speed by **curvature** (pure geometry, ignores the recorded one); `false` (default) = uses your recorded speed |
| `FollowPathUseReference` | `false` | uses the recorded speed **capped by the curve** (reference-assisted: recording as a bound + vehicle physics) |
| `UseInverseModel` | `false` | PID + inverse model for throttle/brake instead of replay |
| `InverseModelKp / Ki / Kd` | `-1 / -1 / -1` | Speed PID; defaults 0.4 / 0.05 / 0 |

**Lateral / Stanley**
| Field | Default | What it does |
|---|---|---|
| `SteeringScale` | `-1` | AUTO: derives from wheelbase (`clamp(wb/5.5, 0.4, 1.0)`) |
| `CruiseLateralDeadband` | `0.0` | Lane half-width; inside it, no correction |
| `CruiseLateralKGain` | `1.0` | Multiplies the offset before Stanley's atan |
| `CruiseLateralDamp` | `0.0` | D-gain on the offset rate (0.3 kills zigzag) |
| `CruiseLateralCenterOffset` | `0.0` | Center bias (+ right, - left) |
| `CurvatureSteerBoost` | `0` | Amplifies steer in a curve: `steer × (1 + boost × bendFrac)` |
| `CruiseFFWeight` | `-1` | Feedforward weight in cruise; default 0.25 |

**Speed / cruise**
| Field | Default | What it does |
|---|---|---|
| `AverageSpeedMS` | `11.0` | ~40 km/h, for ETA |
| `FollowPathMaxKmh` | `50.0` | Cap on straights (when FollowPath=true) |
| `FollowPathLatAccel` | `4.0` | Max lateral accel (m/s²) for curvature-based speed |
| `TargetSpeedSmoothWindow` | `0` | Smooths targetSpeed (5=moderate, 10=aggressive) |
| `CurveThrottleEnabled` | `true` | Anticipatory throttle cut by curvature |
| `CurveThrottleStartDeg / FullDeg` | `35.0 / 80.0` | Accumulated bend where the cut starts / maxes out |
| `CurveThrottleMinScale` | `0.35` | Throttle factor in a tight curve |
| `CurveThrottleLookaheadM` | `14.0` | Scan distance for the cut |
| `FollowPathSpeedSmooth` | `8` | Smooths the speed profile (0=off) |
| `FollowPathCurveSpan` | `5` | Spacing (wps) to measure curvature |

**Gear / brake**
| Field | Default | What it does |
|---|---|---|
| `MaxGear` | `6` | Max gear the AT shifts (FIRST=2, SIXTH=7) |
| `GearStrategy` | `"auto_box"` | "auto_box" or "follow_recording" |
| `AccelShiftThreshold` | `999.0` | Anti-catapult (km/h/s); 999=off (bus), ~15 for a 4x4 |
| `InverseModelLowRpmMin` | `false` | false = rpmMin×1.3 (conservative); true = ×1.0 |
| `EndFreezeDisabled` | `0` | 1 = does not brake at the end |

**Slope**
| Field | Default | What it does |
|---|---|---|
| `SlopeCompensationEnabled` | `true` | Compensates throttle by the path's pitch |
| `SlopeLookaheadWps` | `5` | Wps ahead for lookahead |
| `SlopeGain / SlopeLateralGain` | `1.0 / 1.0` | Compensation gain / lateral bias by pitch |

**AutoRecovery**
| Field | Default | What it does |
|---|---|---|
| `AutoRecoveryEnabled` | `false` | Teleport when Boris gets stuck |
| `AutoRecoveryStuckTimeS` | `10.0` | Seconds stuck before it triggers |
| `AutoRecoveryAdvanceWps` | `5` | How many wps ahead it teleports |
| `AutoRecoveryCooldownS` | `8.0` | Minimum between teleports |
| `AutoRecoveryMaxPerMission` | `0` | 0=unlimited; X=fails the mission if exceeded |

**Corridor / hybrid**
| Field | Default | What it does |
|---|---|---|
| `CruiseHybridSteerThreshold` | `-1` | -1=off; 0.7=override recorded steer jolts >70% |
| `CruiseHybridThrottleThreshold` | `-1` | -1=off; 0.5=override when recorded throttle >=50% |

**Parking**
| Field | Default | What it does |
|---|---|---|
| `ParkingStanleyK` | `-1` | Default = internal `STANLEY_K_PARKING` |
| `ParkingFFWeight` | `-1` | Default 0.6 |
| `ModeEntrySnapEnabled` | `false` | (2026-07-03: was `true`) Alignment snap/teleport on entering a mode; off — the closed-loop control (parking direct-replay + reverse rear-steer, heading <1°) positions on its own. Re-enablable per `_hdr` |
| `ModeEntrySnapMaxDist` | `0.5` | Max distance (m) for the snap |
| `AntiRollbackEnabled` | `true` | Handbrake on a slope to avoid rolling back |
| `AntiRollbackPitchThreshold` | `0.05` | Pitch (rad, ~2.86°) to activate |

**Reverse** (all `-1`/`0` = use internal default)
| Field | Internal default | What it does |
|---|---|---|
| `ReverseStanleyK` | `STANLEY_K_REVERSE` | Stanley gain in reverse |
| `Wheelbase` | from fingerprint | Wheelbase (rear-steer feedforward) |
| `ReverseFFSign` | `-1` (flip) | Feedforward sign (inverted rear-steer) |
| `ReverseFFMaxSteerRad` | `0.6` | Feedforward normalization |
| `ReverseFFWeight` | `=ParkingFFWeight` | Feedforward weight |
| `ReverseSteerGateOffset` | `0.5` | Threshold (m) of the "discrete input" gate |
| `ReverseSteerThrottleFloor` | `0.35` | Floor of steer-then-throttle |
| `ReverseSteerMax` | `1.0` | Clamp on \|steering\| |
| `ReverseRecordedSteerThreshold` | `0.2` | Threshold to follow the recorded wheel |
| `ReverseTargetSpeedCap` | `25` | Speed cap (km/h) |
| `ReverseStanleyFineMax` | `0.15` | Fine-correction cap |
| `ReverseHeadingDeadbandDeg` | `4` | Heading deadband (degrees) |

**Smoothing / maneuver (legacy)**
| Field | Default | What it does |
|---|---|---|
| `PathSmoothWindow` | `5` | Moving-average of positions (0=off; **use 0 on tight curves** — flattens 90°) |
| `DirectReplayFromWaypoint` | `-1` | Wp from which control is bypassed and recorded inputs are replayed (legacy) |

### D.3 — `ai_run_*.csv` format

Server-side log of Boris's run (opt-in with the **`ai_run` check in the Reproductor**, no longer a key). One row every ~0.5 s (~2 Hz). Written to `$profile:BZ_AutoDrive_PathLogger\ai_run_<ts>.csv`. **27 columns** (with header), in this order:

```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```

Key for analysis: `lateral_dev_m` (signed lateral deviation vs the route — the % within ±2 m and the median come from here), `steering` (saturations / sign changes = zigzag), `mode` (cruise/parking/reverse/...), `is_marker` (events marked with NUMPAD 4). Don't confuse it with the PathLogger's `path_*.csv` (the human recording, 50 Hz, different columns).

### D.4 — `BZBusRoute.json` structure

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
    // ... (N waypoints total)
  ]
}
```

Notes: `targetLights` (0/1) and `targetHorn` (0=OFF/1=SHORT/2=LONG, `ECarHornState`) = recorded lights/horn, replayed per wp (spatial replay; see §5). In the `_wp.csv` they sit at col 16 (`targetLights`) and col 17 (`targetHorn`). `Attachments` must be REPLACED when you change vehicle (parts from an authoritative source, NEVER guessed). Empty `Events` = no events. The final waypoints may have `"mode": "reverse"` for a parking maneuver. `Crew` is not a JSON field (the convoy bots come from the Quest, not the JSON).

### D.5 — Guide: add a new verb to the events DSL

1. In `scripts\4_World\BZBusService.c`, find the `ExecuteAction(BZAction a)` dispatcher (the `if/else if` over `a.verb`).
2. Add a branch: `else if (a.verb == "myverb") { /* your logic */ }`. Use the existing `BZAction` fields (`strParam`, `floatParam`, etc.).
3. If you need a new field, add it to the `BZAction` class in `BZBusConfig.c`. **Enforce does not allow constructor args**: create the object empty and set fields.
4. If the verb sends something to the client (UI/sound/toast), forward it via RPC (see `BZBusRPC.c`, e.g. `RECEIVE_TOAST`).
5. Repack the PBO, then test by adding the verb to a route's `Events[]` → deploy → `NUMPAD 2` reloads the route without restarting the server.

### D.6 — Guide: add a new vehicle

1. **Exact classname** from the carpack (from an authoritative source: trader package / config.cpp / spawn-inspect). Copy the mod's `.bikey` to `keys\` (A and B).
2. **Record the FULL route** as a human: `NUMPAD 5` starts/stops. The header (`header_*.txt`) captures the fingerprint (wheelbase, steering angle → R_min, gears, mass, **real parts**).
3. **Wizard** (double-click `tools\Wizard.bat`): **[1] Convert** asks for **only a NAME** (→ `BZBusRoute_<name>.json`). `frame_to_route.py` reads the header, generalizes, auto-detects reverse and writes the `.json`+`_hdr.json`+`_wp.csv` trio, already **deployed** (hot-load). No modes, no linters.
4. **Deploy:** Convert itself does it (split + write to `profiles\BZ_AutoDrive\`). The mirror to B comes from `wizard_config.json` (`ServerBMirror`); see **[6] Configure paths**.
5. **Validate**: tick the **`ai_run`** check in the Reproductor + spawn/test. Analyze the `ai_run` (see D.3) and tune params by symptom (see D.2 + §14).

> **Non-negotiable:** when you change `VehicleClass`, REPLACE `Attachments` with the new vehicle's parts (`csv_to_route` preserves the previous ones → breaks the spawn). Recurring bug.

### D.7 — Guide: map an area (build and join route graphs)

**What it is:** beyond replaying ONE take, MANY are recorded and composed into a **directed graph** → a pathfinder (**Dijkstra**) builds A→B routes **that were never recorded whole**. Today it is **Tier 2**: LLM-assisted offline tools (the recording is in-game; the graph build and routing are solved out of the game; the roadmap is to bring it to runtime).

**How the graph is joined (automatic and incremental):**
- Each point (downsampled ~4 m) of each trace = a **node**.
- Where two traces **cross or graze ≤6 m** = an **edge/intersection** (auto-merge; clusters ≤10 m).
- It is **directed**: each street is traversed only in the **recorded direction** (= it respects the lane). To go and come back, **record both directions** (each direction = a trace).
- A new trace **integrates without touching the previous ones** (monotone: it only adds coverage).
- **Turns:** recorded (followed from real points) vs not-recorded (arc at the vehicle's **R_min**). The destination extends to the perpendicular intersection.

**The tool — `bz_coverage.py`** (on the dev's PC; **analysis script, NOT in the PBO**): reads the `path_*.csv` from the client's recording folder, **filters by a bbox** (`X0,X1,Z0,Z1`) and reports **segments, km, nodes, intersections, bbox** + saves a **coverage map PNG**.
- ⚠️ The `HD` (recording folder) and the script's `bbox` are **Sonom4n's setup** → **adjust them to the user's** (ask for their PathLogger folder + the area to map).
- To find a town's bbox: take the median (x,z) of each `path_*.csv` and group by ~500 m cells; the cell with the most traces = the dense area.

**How to guide a modder to map (you, the AI):** (1) ask for the **paths** + the **area** · (2) set the town's **bbox** · (3) run → report + map → identify **gaps** (missing streets/directions) → tell them which to record · (4) **iterate** (record → re-run, coverage grows monotonically) · (5) build **graph + Dijkstra** A→B → export as an **executable take**.

**Gotchas:** directed (without the reverse direction it does not come back) · thresholds 6 m/10 m depending on the map scale · not-recorded turns = R_min arc (record the turn if you want fidelity).
**Applications:** routable city · map sector · **train tracks** (the graph *is* the tracks: no steering or turns, only accel/brake). Witness case: Novaya Petrovka, Chernarus — 9 traces → 5.1 km, ~1200 nodes, ~102 intersections.

### D.8 — Guide: add a vehicle to a Quest (convoy)

**Division of labor:** the modder configures **(1) the ROUTE** (framework side) + **(2) the QUEST** (Expansion side). The **hook is already pre-built** in the PBO — no code is written.

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
- `ConvoyMode`: `"flee_on_kill"` (kill 1 → they board and flee) or `"ambush_on_damage"` (aboard, armed → damage → freeze + dismount + camp).
- `VehicleInvincible: false` ← **MANDATORY for `ambush_on_damage`** (if it is unbreakable it never takes damage → it does not fire).
- `Crew: []` ← empty: **the bots are placed by the Quest**, not the route. (`Crew[]` is for bots that travel WITHOUT a quest.)
- `SpawnHoldSeconds: 600` (optional, flee): the vehicle waits still until the trigger.
The route comes from the normal flow (record → wizard → deploy to `profiles\BZ_AutoDrive\`).

**FILE 2 — the Quest** (config of **DayZ-Expansion-Quests**, in the user's quest mod data, **NOT** in the framework): there you define the **live bots** (patrol/camp), the objective (kill them), the reward — with the Expansion-Quests editor/JSON. *The framework does NOT spawn armed bots with logic; that is exclusively the Quest's job.*

**The hook (ALREADY in the PBO — just to understand):** `modded class MissionServer.Expansion_OnQuestStart` → `BZBusService.OnQuestStart(quest)` saves the quest ID (`qc.GetID()`) and **polls** `ExpansionQuestModule...QuestPatrolExists(questID, patrols)` every ~4 s (the bots are *lazy*, by proximity). When the bot count **drops** (one was killed) → it fires the scene.

**Dependency:** the framework's `config.cpp` declares `DayZExpansion_Quests_Scripts` (for publishing it is better to split it into an optional sub-addon).

**Checklist for the modder:** (1) record+deploy the route with `ConvoyMode` set · (2) in Expansion-Quests create the quest with its bots near the **route start** · (3) test: you accept → the bots appear → you kill 1 (flee) or hit the vehicle (ambush) → it fires.
**Gotchas:** ambush without `VehicleInvincible:false` does not fire · bots in the Quest (not in `Crew[]`) · full validated examples in the **manual §12.3/§12.4** (convoy) and §12.10/§12.11 (Travel/Escort, which auto-select a route by quest).

### D.9 — External references + searches (wiki + cited material)

*To send the modder to the authoritative source, or to search more. The **local code is the truth**; this is external context.*

**DayZ / Enforce (game modding):** BIS Community Wiki — base `https://community.bistudio.com/wiki/` · DayZ modding: `https://community.bistudio.com/wiki/DayZ:Modding_Basics`. Search: `DayZ Enforce Script`, `DayZ CarScript SetThrottle`, `DayZ Transport WheelGetSurface`, `DayZ modded class MissionServer`, `DayZ EmoteManager`, `DayZ ConfigGetFloat CfgVehicles SimulationModule`.

**DayZ-Expansion (eAI + Quests + vehicles):** public source `https://github.com/salutesh/DayZ-Expansion-Scripts` (look at `ai_scripts`, `quests_scripts`, `vehicles_scripts`). Search: `DayZ Expansion AI eAIBase`, `Expansion Quests ExpansionQuestObjective`, `Expansion StartCommand_Vehicle`, `Expansion_OnHandleController`.

**Prior engine / Related Work (deep-research verified):**
- ARMA 3 AI driving (official OPREP): `https://dev.arma3.com/post/ai-path-following-improvements`
- ARMA 3 `AICarSteeringComponent` (Biki): `https://community.bistudio.com/wiki/Arma_3:_AICarSteeringComponent` · `setDriveOnPath`: `https://community.bistudio.com/wiki/setDriveOnPath`
- Enfusion (Reforger) navmesh: `https://community.bistudio.com/wiki/Arma_Reforger:Navmesh_Tutorial`
- EA SEED, *Efficient Ground Vehicle Path Following in Game AI* (CoG 2023): `https://arxiv.org/abs/2307.03379`
- Codevilla et al., *Limitations of Behavior Cloning* (ICCV 2019) · *One-Shot Imitation Learning* (NeurIPS 2017).

**Tooling (dev's PC):** ImageToPAA (PNG→PAA), AddonBuilder (build PBO). Search: `DayZ Tools ImageToPAA`, `DayZ AddonBuilder pbo`.

### D.10 — Diagnose a run (ai_run) — AI-actionable

*For an AI (you: Claude/GPT/Gemini) to **diagnose a Boris run** by reading an `ai_run` file the admin pastes you. This is executable: read the precomputed columns, map the signatures to causes, and deliver the diagnosis **in plain language for the admin**, in the format below. Complements §14 ("Analyzing an ai_run") and D.3 (the file format).*

**What the ai_run is.** It is the **telemetry of a Boris run** — the "black box" of what the NPC actually did. It is **opt-in** (the admin arms it by ticking the **`ai_run` check in the Reproductor**, BEFORE hitting play — no longer a key; without it nothing is written). It lives in `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv` (the server that ran it: A `C:\DayZServer\profiles\...`, B `Y:\profiles\...`). **Do NOT confuse it with the human take** (`path_*.csv`, the recording/demonstration, 50 Hz, different columns) — they are different files with opposite purposes (measurement vs reference).

**REAL schema (27 columns, ~2 Hz)** — this is the CURRENT format; LEAD with it. Header as-is (with `header`, in this order):
```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```
**Key: the file ALREADY carries the diagnosis precomputed by the framework — READ those columns, do NOT recompute them.** The ai_run **does NOT need the route separately**: the lateral deviation and the targets already come inside it.

**Columns you READ directly (what each one does):**
- `lateral_dev_m` — **signed lateral deviation per sample** vs the route (already computed). Where it goes **wide**. Use the MEDIAN and segment straight/curve; the signed average deceives due to outliers/saturations (§14). **High in a curve** = enters fast / understeer.
- `target_speed`, `target_throttle`, `target_brake` — the controller's **targets**. **Deficit = `speed_kmh − target_speed`** directly (sustained negative = Boris does not reach the target → lugging/slope).
- `corridor_offset`, `corridor_valid` — **corridor/containment-wall** tracking. `corridor_valid == 0` → **off-path** (Boris left the lane band on that stretch).
- `rpm`, `redline_rpm` — engine. **`rpm` near `redline_rpm` at low `speed_kmh`** = **lugging** (short gear maxed out without moving) or a badly chosen gear.
- `mode` = **active** controller (cruise/maniobra/parking/reverse); `wp_mode` = the waypoint's **declared** mode. If they differ, or if `mode=cruise` on a slow-tight stretch, there is a mode↔geometry mismatch.
- `is_marker` — samples marked with **NUMPAD 4** = **events/stops** the admin wanted to flag; anchor the analysis there.
- `wp_idx` — the waypoint Boris chases; `next_stop_idx`/`dist_to_next_stop` = the next stop.
- `i_speed`, `i_throttle`, `i_brake` — the controller's internal terms (PID/inverse); useful to understand why it asked for that throttle/brake, not for first-level diagnosis.

**Computations you apply by READING those columns:**
- **Lateral deviation:** median of `lateral_dev_m` (segment straight/curve). Peaks in a curve = it goes wide.
- **Speed deficit:** `speed_kmh − target_speed`; locate where it turns sustainedly negative.
- **Off-path:** stretches with `corridor_valid == 0`.
- **Lugging:** `rpm`≈`redline_rpm` with low `speed_kmh` (+ `throttle`>0).
- **Stuck/wedged:** `wp_idx` **constant** for many samples + `speed_kmh ~0`. Look at `mode`/`gear`/`rpm` there.
- **Mode vs geometry:** `mode` vs `wp_mode` and vs the stretch's speed.
- **Completion:** did `wp_idx` reach the **last** wp? If not, how far.

**Fallback (OLD 14-column files, or to verify).** Old takes carry only `time_s,x,y,z,heading_deg,speed_kmh,gear,throttle,brake,steering,mode,dist_to_next_stop,next_stop_idx,wp_idx` — **without** `lateral_dev_m`/`target_*`/`corridor_*`/`rpm`. There you DO recompute by hand (and it is better to ask for the route `BZBusRoute_<name>_wp.csv`/JSON for the path and the target speeds):
- `dt` = diff of `time_s`; v in m/s = `speed_kmh/3.6`.
- **AR teleport / jump:** real distance between 2 samples = `sqrt(dx²+dz²)`. If **>> `(speed_kmh/3.6)·dt`** (e.g. >3× and several meters at once) → **AutoRecovery teleported** Boris (he was wedged or off-path); the jump marks WHERE he got stuck (look at the previous `wp_idx`).
- **Lateral deviation** = distance from `(x,z)` to the route's path near that `wp_idx` (what `lateral_dev_m` already gives in the new format).
- **Deficit** = `speed_kmh` vs the wp's `targetSpeed` in the route.

**Failure-signature → cause → fix table:**
| Signature in the ai_run (column) | Probable cause | Suggested fix |
|---|---|---|
| `wp_idx` sticks + `speed_kmh`~0 sustained | impossible curve / high gear / off-path | re-record wider and slower (with THAT vehicle); check the gear |
| position jump (sqrt(dx²+dz²) >> v·dt) | AutoRecovery rescued Boris (he got stuck there) | attack the **cause** of the stick at that wp |
| `speed_kmh` << `target_speed` sustained | lugging / speed deficit | `GearStrategy=follow_recording`; check the slope |
| `lateral_dev_m` high in a curve | overspeed / understeer, goes wide | enter slower; wider curve; curvature cap |
| `corridor_valid==0` on a stretch | Boris off-path (out of the lane) | check the curve entry; AutoRecovery; re-record the stretch |
| `rpm`≈`redline_rpm` + low `speed_kmh` + `throttle`>0 | lugging (short gear maxed out) | `GearStrategy=auto_box` or `follow_recording` |
| `mode=cruise` (or `mode`≠`wp_mode`) on a slow-tight stretch | the cut lands in a fast zone | re-record that stretch slower (with THAT vehicle); if it's a direction change, brake fully on the straight and shift gear there (the interchange is auto-detected from the gear) |
| `wp_idx` does not reach the end | did not complete | diagnose the last problem above |

**How to deliver the diagnosis (you → the admin, in plain language):**
1. **Summary:** did it complete yes/no? + WHERE the problems are (wp + type).
2. **For each problem:** the observed **signature** (cite the column: `lateral_dev_m`, `corridor_valid`, `speed_kmh` vs `target_speed`, `rpm`/`redline_rpm`…) + the **cause** + the **concrete fix** (cite the D.2 / §5 param when it applies).
3. **Remember** that the fix is usually to **re-record that stretch** or **adjust a route parameter** (not to feed back the ai_run).

> **PRINCIPLE (do not violate):** the ai_run is a **MEASUREMENT** cross-referenced AGAINST the human take to calibrate (functional feedback); **NEVER a new reference**. **Never** suggest turning an `ai_run` into a route: it clones Boris's errors → degrades each iteration ("model collapse"). The wizard filters it on purpose. The human take is the sacred reference; the ai_run is the thermometer.

### D.11 — Guide: importing a BrigadaZ Transport v1 take

*Convert a route from the old **BrigadaZ Transport v1.0** mod (a monolithic JSON with `Waypoints`) to the AutoDrive format, without re-recording. Wizard → **[2] Import v1 take**.*

1. **Pick the `.json`** of the v1 take.
2. **Vehicle identity (`--fingerprint`):** the v1 take does NOT carry `Wheelbase`/`Fingerprint`/`Attachments` (they depend on the VEHICLE, not the route). You give it **any** `header_*.txt` of that vehicle (a **10 s** recording works) or an `_hdr.json` from an already-calibrated take. The wizard lists first the ones that match the declared vehicle.
3. **Obstacle profile (AR_OnWay, §5):** `[R]obust` (Slow+Escape ON — a line bus that skirts whatever blocks its path) · `[I]nterceptable` (Slow ON, Escape **OFF** — it brakes but does NOT escape → good for interception missions) · `[N]one` (pure replica). Sets `ObstacleSlow`/`ObstacleEscape` in the `_hdr` (requires `UseInverseModel=true`, which the template ships).
4. **Output:** the `BZBusRoute_<name>.json`+`_hdr.json`+`_wp.csv` trio, already deployed. Runs `transport_v1_to_route.py` (same signature as `frame_to_route.py`).

**What migrates and what doesn't:** the **trace + speed + stops** migrate (the AutoDrive header/wp is a strict superset of v1's). The v1 **pedals** are **discarded** (the control is rebuilt from trace+speed, not replayed). The **driving profile is per-vehicle** → it comes from the `--fingerprint`, not the v1 route. `targetHeading` is derived by geometry (`atan2(dx,dz)`, median error ~0.35°). See manual §6.2.
