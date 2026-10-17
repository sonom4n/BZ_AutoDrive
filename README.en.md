# BZ_AutoDrive

**Autonomous NPC vehicle driving for DayZ — from a single recorded lap.**

You record a route by driving it yourself. An NPC (Boris) drives it forever, with **each vehicle's real physics**. It trains no per-vehicle model and needs no hand-placed waypoints: it **reads the config the car already declares** in the engine (torque curve, gearbox, steering, geometry), **replays your driving point by point**, and **learns from your own takes** how that vehicle behaves. **It drives any vehicle with no per-vehicle setup.**

> Spanish version: [README.md](README.md) · **MIT** license · **BrigadaZ** project

---

## The idea in one sentence

**You record a route, the framework reads the vehicle's config and learns from your driving, and the NPC drives it — on any car, with no per-vehicle setup, no programming and no training.**

## What makes it different

- **Config as the manual, measurement as the correction:** it reads the vehicle's declared physics and corrects it with what it **measures from your takes** (each vehicle's *profile*). When the declared and the measured values disagree, the measured one wins. No machine learning: **deterministic and inspectable**.
- **It replays your driving:** the *tape* stores steering, throttle, brake and gear 40 times per second, indexed by **distance traveled**. The NPC does what you did **at that point of the road**: it flies where you flew and brakes where you braked.
- **Any vehicle, no tuning:** the control reads each car's declared physics and drives from that — front / rear / mid / 4x4 / **truck** — with **a single configuration**, no per-vehicle tuning. Validated out-of-sample on a broad bench of drivetrains and sizes. *(A **recording** belongs to its vehicle: for the same route on another car, **record it with that car** — that way the framework learns its physics.)*
- **Real physics:** the NPC applies real steering, throttle and brake to the simulated vehicle. It looks and feels like a person driving, not a scripted teleport.
- **Handles the hard stuff:** tight curves, reverse, **auto-detected** direction changes (K-turns), and a precise final stop (about **10 cm** from the recorded point). A route only counts as **complete** if the vehicle ends at rest within 0.25 m, without hitting any object and without physical help from AutoRecovery.
- **Scales:** route loading **~1700× faster** (data format change) + **multiple concurrent vehicles without moving the server FPS**.

## What's included

- **Authoring pipeline** usable by a non-programmer admin: record → convert (Wizard) → run → orchestrate.
- **Scenario DSL** (per-waypoint events/triggers/verbs) + **DayZ-Expansion-Quests integration** (convoys, ambushes, escorts).
- **UI / player panel:** live multi-vehicle dashboard, hot route loading with no restart.
- **Branded t-shirt** (classname `BZ_AutoDrive_TShirt`) — a project cosmetic, included in the mod.

---

## Documentation

| Document | For whom | What it is |
|---|---|---|
| [MANUAL_BZ_AutoDrive.en.md](MANUAL_BZ_AutoDrive.en.md) | Admins / modders | Practical manual: from your first recording to Quest missions |
| [AI_KNOWLEDGE_PACK.en.md](AI_KNOWLEDGE_PACK.en.md) | Your AI assistant | Full technical context to load into Claude/GPT/Gemini so it guides you |

*(Each one has its Spanish version — drop the `.en`.)*

---

## Getting started

**If you're a player:** subscribe. You'll see the transport driving around; hop in and it takes you.

**If you're a server admin:**
1. Add `@BZ_AutoDrive` to your active mods (+ the dependencies below).
2. Controls live in `Options → Controls → "BZ AutoDrive"` (rebindable). There are **3**: **Open panel** (HOME), **Record** (NUMPAD 5), **Mark event/stop** (NUMPAD 4). Reverse and direction changes are **auto-detected** — you don't mark anything.
3. The loop: **record** a route by driving it → the **Wizard** converts it by reading the config → **run** it from the player panel. Step-by-step guide in the manual (§5).

> The **first time** you open the Wizard (`tools\Wizard.bat`) it asks for your **paths** (your server's routes folder + where you record). You set them **once** and it remembers them.

**If you're a modder:** load the [AI knowledge pack](AI_KNOWLEDGE_PACK.en.md) into your AI assistant. It carries walkthroughs to record/convert/run and build quests with vehicles — and a step-by-step guide to **port the framework to another game** (Appendix E, with 7 Days to Die as the worked example).

> **⚠️ Third-party permissions:** referencing a modded vehicle by **classname** (the mod loads separately, untouched) does **not** require permission. **Repacking or extracting** a third party's assets **does** require the author's explicit permission. It's the DayZ community norm. When in doubt: reference, don't extract.

---

## Architecture (the foundation)

The framework hooks into `CarScript` with **override-last**: it lets the eAI receiver run and **overwrites the inputs afterward** with the ones its control stack computes. Since `CarScript` is inherited across the whole vehicle family, **any car that extends it becomes bot-drivable without writing a single line per vehicle**.

The driver **replays your driving by position** (the *tape*: what you did at each point of the road, not at each instant), follows the path with a **pure-pursuit** controller (it aims at a point ahead on the line), and derives throttle/brake/gear from an **inverse model built from the vehicle's config and corrected by what was measured** in its profile. It is not a time-based replay: the real physics does the rest, frame by frame.

```
PathLogger (record)  →  Wizard (route + tape + vehicle profile)  →  1.2 brain (tape + pure-pursuit + inverse model + measurements)  →  Player panel (run)
```

## Dependencies

**Required: DayZ-Expansion, through the Bundle** (`@DayZ-Expansion-Bundle`, with its own dependencies: CF, Dabs Framework and Expansion-Licensed). The framework needs three parts of Expansion, and **all three come inside the Bundle**:

- **AI (eAI)** — the NPC driver's body.
- **Vehicles**.
- **Quests** — the mod needs it to load even if you don't make missions.

> ⚠ **Do not add `@DayZ-Expansion-AI` on its own** if you already have the Bundle: they are the same classes in another version and **the server won't start**.

**Workshop vehicle mods** — any that extend `CarScript` (most of them) work automatically, no changes needed. **On DayZ 1.30**, a vehicle mod made for 1.29 needs an update from its author (some don't compile; others load but their doors, wheels or damage break). 1.2.1 is validated with the vanilla vehicles of 1.30; modded ones are added as they get updated.

## Build from source

To use it: subscribe on the Workshop (it ships signed). To build your fork: package the PBO however you prefer — DayZ Tools (AddonBuilder) or your own pipeline — and sign it with your **own** key. Each modder packages their own way.

## Status

**v1.2.3** — AutoDrive vehicles are no longer saved in the world when the server shuts down; ESC can no longer become the panel key; much more accurate motorbikes (they follow the path's shape, measure how they respond, stop exactly where other mods ask and pull away in 1st).

**v1.2.2** — urgent fix: 1.2.1 did not compile together with the CarPack updated for 1.30 (and other packs with short-named classes). Only variable names change; driving is the same.

**v1.2.1** — **ready for DayZ 1.30**: a single package runs on 1.29 and 1.30; **motorbikes** (experimental, 1.30 only); a **strict verdict** for the end of a route, decided with the vehicle at rest; final braking measured from your take; direction changes within centimeters; light logs by default (`Diagnostics`, `BlackBox`). On 1.30, start the server with a relative `-profiles`.

**v1.2** — **new driving brain**: it replays your driving (the *tape*) and **measures** each vehicle (its *profile*); no vehicle creeps on its own at idle (measured on 43 vehicles); launch throttle measured across the fleet; an **AutoRecovery** that detects a jam by lack of progress and also recovers in reverse; faster direction changes; conversion produces **four** files and ships with **Python included**. Kept from before: **auto-detected reverse and direction changes**, **precise final stop**, **event DSL**, **Quests integration** (convoy/ambush/escort), **multi-runner**, UI/player panel. Details in the [CHANGELOG](CHANGELOG.en.md).

**Roadmap:** 100% autonomous Wizard, boat/heli adapter, rail archetype, LLM driver.

---

## License

**MIT.** Free to use, modify, repack and redistribute. No attribution required. Third-party components referenced as dependencies (DayZ-Expansion, vehicle mods) keep their own licenses.

## Credits

Developed by **Sonom4n** and **Hiperhipo10** — **BrigadaZ PVE Server**. AI-assisted development (pair-programming).

## Support the project

The framework is **free, with or without a contribution**. If it helps you and you want to back the development of upcoming versions: **[paypal.me/Sonom4n](https://paypal.me/Sonom4n)**.

---

*It's under the MIT license, and not by oversight: it's a stance. Take it, study it, modify it, **repack it**, publish your version — you won't need my permission. What's shared freely isn't stolen: it multiplies. A community doesn't grow by hoarding; it grows by passing things on. If this becomes the base for something better, it already did its job.*
