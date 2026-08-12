# BZ_AutoDrive

**Autonomous NPC vehicle driving for DayZ — from a single recorded lap.**

You record a route by driving it —or draw it in the editor—. An NPC (Boris) drives it forever, with **each vehicle's real physics**. It trains no per-vehicle model and needs no hand-placed waypoints: it **reads the config the car already declares** in the engine (torque curve, gearbox, steering, geometry) and drives from that. **It drives any vehicle with no per-vehicle setup.**

> Spanish version: [README.md](README.md) · **MIT** license · **BrigadaZ** project

---

## The idea in one sentence

**You record a route —or draw it in the editor—, the framework reads the vehicle's config, and the NPC drives it — on any car, no per-vehicle setup, no programming or training.**

## What makes it different

- **Config-as-manual:** instead of learning the dynamics from data, it reads the vehicle's declared physics. No machine learning, **deterministic and inspectable**.
- **Any vehicle, no tuning:** the control reads each car's declared physics and drives from that — front / rear / mid / 4x4 / **truck** — with a **single configuration**, no per-vehicle tuning. Validated out-of-sample across a broad bench of drivetrains and sizes. *(A **recording** belongs to its vehicle; for cross-vehicle, you draw the **trace** in the editor and assign whatever vehicle you want.)*
- **Real physics:** the NPC applies real steering, throttle, and brake on the simulated vehicle — it looks and feels like a person driving, not a scripted teleport.
- **Handles the hard stuff:** tight curves, reverse, **auto-detected** direction changes (K-turns), and precise final stops (typically **< 0.5 m**, up to ~1 m on the largest ones).
- **Scales:** route loading **~1700× faster** (data format change) + **multiple concurrent vehicles without moving the server FPS**.

## What's included

- **Authoring pipeline** usable by a non-programmer admin: record → convert (wizard) → run → orchestrate.
- **Visual route editor** (`tools/editor/`) to draw, join, and fine-tune routes on the map.
- **Emergent graph routing:** compose many takes and route A→B pairs never recorded end-to-end (mapping a town).
- **Scenario DSL** (per-waypoint events/triggers/verbs) + **DayZ-Expansion-Quests integration** (convoys, ambushes, escorts).
- **UI / Reproductor:** live multi-vehicle dashboard, hot route loading with no restart.
- **Branded t-shirt** (classname `BZ_AutoDrive_TShirt`) — a project cosmetic, included in the mod.

---

## Documentation

| Document | For whom | What it is |
|---|---|---|
| [MANUAL_BZ_AutoDrive.en.md](MANUAL_BZ_AutoDrive.en.md) | Admins / modders | Practical manual: from your first recording to Quest missions |
| [AI_KNOWLEDGE_PACK.en.md](AI_KNOWLEDGE_PACK.en.md) | Your AI assistant | Full technical context to load into Claude/GPT/Gemini so it guides you |

*(Each with its Spanish version — drop the `.en`.)*

---

## Getting started

**If you're a player:** subscribe. You'll see the transport driving around; hop in and it takes you.

**If you're a server admin:**
1. Add `@BZ_AutoDrive` to your active mods (+ the dependencies below).
2. Controls live in `Options → Controls → "BZ AutoDrive"` (rebindable). There are **3**: **Open panel** (HOME), **Record** (NUMPAD 5), **Mark event/stop** (NUMPAD 4). Reverse and direction changes are **auto-detected** — you mark nothing.
3. The loop: **record** a route by driving it → the **wizard** converts it by reading the config → **run** it from the Reproductor. Step-by-step guide in the manual (§5).

> The **first time** you open the wizard (`tools\Wizard.bat`) it asks for your **paths** (your server's routes folder + where you record) — you set them **once** and it remembers them.

**If you're a modder:** load the [AI knowledge pack](AI_KNOWLEDGE_PACK.en.md) into your AI assistant — it carries walkthroughs to record/convert/run, map areas (join graphs), and build quests with vehicles.

> **⚠️ Third-party permissions:** referencing a modded vehicle by **classname** (the mod loads separately, untouched) does **not** require permission. **Repacking or extracting** a third party's assets **does** require the author's explicit permission. It's the DayZ community norm. When in doubt: reference, don't extract.

---

## Architecture (the foundation)

The framework hooks into `CarScript` with **override-last**: it lets the eAI receiver run and **overwrites the inputs afterward** with the ones its control stack computes. Since `CarScript` is inherited across the whole vehicle family, **any car that extends it becomes bot-drivable without writing a single line per vehicle**.

The driver follows the path with a **pure-pursuit** controller (aims at a point ahead on the line), sets speed from the **curvature of what's coming**, and derives throttle/brake/gear from an **inverse model built from the vehicle's config**. It's not a frame replay — it's real control, frame by frame.

```
PathLogger (record)  →  Wizard (convert by reading the config)  →  Control stack (pure-pursuit + curvature + inverse model)  →  Reproductor (run)
```

## Dependencies

**Required:**

- **DayZ-Expansion-Core**
- **DayZ-Expansion-AI (eAI)** — the NPC driver's body.
- **DayZ-Expansion-Vehicles**
- **DayZ-Expansion-Quests**

**Workshop vehicle mods** — any that extend `CarScript` (most of them) work automatically, no changes needed.

## Build from source

To use it: subscribe on the Workshop (it ships signed). To build a fork: package the PBO however you prefer — DayZ Tools (AddonBuilder) or your own pipeline — and sign it with your **own** key. Each modder packages their own way.

## Status

**v1.0** — **unified** control (one stack, no modes), forward/cruise driving + **reverse solved** (geometric pure-pursuit), **auto-detected direction changes**, **final stop solved** (endpoint < 0.5 m typical, validated out-of-sample across front/rear/mid/4x4/truck), **emergent graph routing**, **event DSL**, **Quests integration** (convoy/ambush/escort), **multi-runner**, UI/Reproductor, conversion wizard.

**Roadmap:** in-game graph + pathfinding (offline today), 100% autonomous wizard, boat/heli adapter, rail archetype, LLM driver.

---

## License

**MIT.** Free to use, modify, repack, and redistribute. No attribution required. Third-party components referenced as dependencies (DayZ-Expansion, vehicle mods) keep their own licenses.

## Credits

Developed by **Sonom4n** and **Hiperhipo10** — **BrigadaZ PVE Server**. AI-assisted development (pair-programming).

## Support the project

The framework is **free, with or without a contribution**. If it helps you and you want to back the development of upcoming versions: **[paypal.me/Sonom4n](https://paypal.me/Sonom4n)**.

---

*It's under the MIT license, and not by oversight: it's a stance. Take it, study it, modify it, **repack it**, publish your version — you won't need my permission. What's shared freely isn't stolen: it multiplies. A community doesn't grow by hoarding; it grows by passing things on. If this becomes the base for something better, it already did its job.*
