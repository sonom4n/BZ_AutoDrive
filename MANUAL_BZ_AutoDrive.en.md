# BZ_AutoDrive — Administrator's Manual

> NPC vehicle-driving framework for DayZ. Didactic edition for admins/modders.
> **Open source (MIT):** the code, this manual and the AI knowledge pack are public — download it, use it on your server, fork it, extend it and share your routes.
> *Manual v1.0 — the administrator's practical guide. Replaces MANUAL_eAI_VEHICLES as the operational guide.*

---

## Contents

1. What is BZ_AutoDrive?
2. The idea, in one sentence
3. The usage model: download it, adapt it, it's your mod
4. Requirements
5. Quick start — your first route
6. The wizard in detail
6B. The trajectory and map editor — *draw instead of record*
7. How to record well
8. The permanent bus service
9. How the NPC drives (it follows your line and your speed)
10. Events and sequences
11. The graph — route network
12. Missions — integration with Quests
13. Controls (keys)
14. The interface (UI)
15. Audio
16. If something doesn't work
17. Ideas
18. Scope and limits
19. Open frontier
- Appendix A — Complete route config reference
- Appendix B — For developers (extending with code)

---

## 1. What is BZ_AutoDrive?

BZ_AutoDrive makes an **NPC drive a DayZ vehicle autonomously**, following a route that **you recorded by driving**. It's not a teleport or "fake" movement: the NPC really **accelerates, brakes and steers**, using the game's real physics — the car behaves just like it does when you drive it.

**What's it good for?** For anything that needs an AI-driven vehicle:

- A **line bus** that runs along the coast stopping in every town.
- A **taxi** that takes players from one point to another.
- A **reinforcement convoy** that arrives at a mission zone and deploys bots.
- A motorized **patrol** that circles around an area.

> *Example:* you want a bus that links three towns. You drive it **just once** (you record the route), and from then on an NPC repeats it by itself, as many times as you want, without you being there.

---

## 2. The idea, in one sentence

> **You drive the route once; the framework learns your line and reads what the vehicle is like; then an NPC drives it.**

The powerful part is the "reads what the vehicle is like": BZ_AutoDrive **reads the vehicle's config** (its steering, its gears, its engine) and drives according to that. That's why **the framework drives any vehicle with no per-car setup** — you calibrate nothing per vehicle, the declared physics is enough.

> *Mind the recording:* a recording captures the run of **its** vehicle (its *fingerprint*, its measured brake, its gear) — **it belongs to that car**. Want the **same trace** on another vehicle? That's done in the **editor**: you load/draw the trace and **assign it the vehicle** you want (§6B). The **drawn trace is what's universal**; the recording belongs to its car. Each vehicle runs it according to what its physics allow (the small one nails the line, the bus does it slower but gets there).

*(Why it matters: DayZ has no "drive to here" for AI cars — it only gives you the wheel and the pedals. BZ_AutoDrive is the missing driver.)*

---

## 3. The usage model: download it, adapt it, it's your mod

BZ_AutoDrive is a **starting point**, not a closed product. The arc for any admin is always the same:

1. **You download** the framework (open source).
2. **You adapt it to your server:** you record *your* routes and configure *your* scenarios. The **driving engine comes already integrated** — you don't program how the NPC drives, that's already solved.
3. **You're left with your mod**, with the framework inside + your content, running your own thing.

What you add depends on what you want it for:
- **Bus / taxi service** → you record the route(s) and leave them running (§8). No code needed.
- **Mission system** → you add Quests + the convoy/escort routes (§12). The framework drives your mission vehicles.
- **Something custom** → you extend it with code (Appendix B) or build your own mod on top of it as a dependency (B.6).

> The point of open source: **everyone downloads the same engine and makes it their own.** One builds a coastal bus, another an urban taxi, another a mission campaign — without anyone having to reinvent the driving.

> *If you repackage it as your own addon* (renaming it, your own Workshop): you rebuild with your own key and copy the `.bikey` to `keys/`. The MIT license allows it — just keep the credits.

---

## 4. Requirements

- **DayZ server** with the **BZ_AutoDrive** mod loaded + its `.bikey` in `keys/`.
- **DayZ-Expansion-AI (eAI)** — *essential*: the NPC driver comes from here. The framework **drives**; eAI provides the NPC's "body".
- **DayZ-Expansion-Quests** — *only* if you're going to do **missions with bots** (§12). For transport / taxi / patrol it's **not** needed.
- **The wizard** (`route_wizard.ps1`) runs on **your PC** (outside the game), not on the server. You launch it by double-clicking `tools\Wizard.bat` — no need to open PowerShell or type anything.
- **Python 3 (on your PC)** — *essential for converting takes*: the wizard's converter (`frame_to_route.py`) and the v1 import are Python. Without Python, the wizard opens but **can't convert** a recording into a route. You install it **once** from [python.org/downloads](https://www.python.org/downloads/) — in the installer, tick **"Add Python to PATH"**. *(The mod, the trajectory editor and the already-converted routes do **not** need it — Python is only required for the step of **converting/importing** takes.)*

---

## 5. Quick start — your first route

We're going to build an end-to-end route. *Example we'll follow: a bus that goes from one town's terminal to another.*

### Step 0 — Check your keys *(do this BEFORE anything else)*

The framework uses **only 3 keys**, already assigned by default. Go to **Options → Controls → the "BZ AutoDrive" category** and verify them:

| Action (in the menu) | Default |
|---|---|
| **Open Control Panel** | `HOME` |
| **Record (start/stop)** | `NUMPAD 5` |
| **Mark Event / Stop** | `NUMPAD 4` |

> **⚠ First thing: make sure they don't clash with your keys.** If you already have `HOME` or `NUMPAD 4/5` assigned to **another mod** (or to a DayZ action), they'll **collide** and something won't respond — and **DayZ doesn't warn you about the conflict**. So, before recording: open that category, check that the 3 don't collide with yours, and **rebind** whichever one needs it (click the key → press the new one). It's a minute and it saves you the classic *"I record and nothing happens"* or *"I press HOME and the panel doesn't open"*.

*(These keys only take effect for the **admin** —§14.1— even though all players SEE them in their Controls menu. Full detail on the controls in §13.)*

### Step 1 — Record the route (you drive it)
Get into the vehicle and drive the route **the way you want the NPC to do it**.
- **NUMPAD 5** → start recording. Drive calmly, at the speed you want it to go.
- **NUMPAD 4** → tap it at each **stop or important point** (the terminal, a corner where something happens). It marks that point so you can later attach events to it.
- **NUMPAD 5** again → finish recording.

> You're left with a file containing your route: your **line** + your **speed** + **where you stop**. That's the raw material.

> **🔑 The most important thing that happens when recording (and that you don't see):** that same NUMPAD 5, besides your route, **takes a complete snapshot of the vehicle** — its *fingerprint*. It ends up in a `header_*.txt` next to your recording, and it brings **all the car's data, by itself**:
> - the **classname**, the **wheelbase** (distance between axles) and the **steering angle** → from which the **R_min** is computed (the smallest radius it can turn),
> - the number of **gears**, the **mass**, the engine **RPM**,
> - and the **real list of its parts** (wheels, doors, battery, radiator, spark plug…).
>
> That's why **you configure nothing about the vehicle by hand**: the wizard and the converter read that fingerprint and build the route to fit it — they compute how much it can turn, they put the correct attachments on it (*they're not guessed, they're the ones the vehicle had*) and they drive it according to *its* physics. It's the heart of "config as driving manual".
>
> *Real example:* we recorded a UAZ-452 and from **a single NUMPAD 5** everything came out: `UAZ_452`, wheelbase 2.53 m, R_min 3.9 m, 6 gears, mass 2860 kg, + its 4 wheels (including the spare), 6 doors and battery/radiator/spark plug. Zero configuration, zero guessing.

### Step 2 — Convert the recording (the wizard)
The wizard is a **converter**: it takes your raw recording —tied to the car you drove with— and **converts** it into a ready route, **"about the route" and not "about your car's pedals"** (speed now comes from the inverse model that reads the config, not from your raw pedals), and **deployed** to the Player. *It calibrates nothing by hand — the fidelity comes from reading the vehicle's config live (the fingerprint), not from a per-route tweak.*

Launch the wizard by **double-clicking `tools\Wizard.bat`** (no need to open PowerShell or type anything), pick **[1] Convert**, choose your recording and give it a **name** — and it leaves you the route ready.

> **The first time** you open the wizard it asks you for your **paths** — your server's routes folder (`RoutesDir`) and where your recordings live. You set them **once**, the wizard remembers them, and you change them anytime with **[6] Configure paths** (detail in §6).

> *Why this step?* Without it, the NPC would drive with the exact pedals of **your** car — and on another vehicle it would break. The wizard makes the route be **"about the route", not "about the car"**.
> *Where's my recording?* It lives on the **client**, the PC where you drove: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`. The wizard finds it there on its own. The converted route is **deployed** to your server's routes folder (`RoutesDir`). All the detail —the "trio" of files and the folders— in **§6**.

### Step 3 — Deploy (the wizard already does it)
On converting, the wizard runs the checks + the split and leaves the route **deployed and ready in the Player** (hot-loads, **without restarting the server**). You edit no file by hand. If you have a second server, the wizard offers to copy it to server B (you configure the paths in **[6] Configure paths**).

### Step 4 — Test it (let the NPC drive it)
Open the **Player** (the admin UI) → pick your route from the list → the NPC appears and starts driving. **Without restarting the server.**

> *Example:* you pick "Town bus" from the list and right there the bus leaves the terminal, driven by the NPC, stopping where you marked.

**That's the whole cycle:** `record → wizard → deploy → play`. The rest of the manual is for getting more out of each part.

---

## 6. The wizard in detail — your central tool

The **wizard** (`tools\route_wizard.ps1`) turns your raw recording into a ready route, deployed to the Player. It's an interactive menu (TUI) — it runs on your PC, outside the game. *(It's a **converter**: it doesn't calibrate or run you through a questionnaire — the driving comes from reading the vehicle's config.)*

> **Launch it by double-clicking `tools\Wizard.bat`** — it opens the wizard directly, without opening PowerShell or typing commands. (The `.bat` uses `-ExecutionPolicy Bypass`, which **only affects that run** — it doesn't change your system's policy.)

> **Principle: everything goes through the wizard.** The wizard runs the sub-tools (`csv_to_route`, `route_split`) **for you**, in order. You do **not** edit the JSON by hand or run the loose `.ps1` scripts — the wizard is the only entry point.

### ⚠ Where each take is stored (read this)

When you record (NUMPAD 5), the file **doesn't end up where you'd intuitively look for it**. The golden rule:

- The **human recording** (`path_*.csv` / `frame_*.csv`, the one you make with NUMPAD 5) lands on the **client** (the PC where you drove): `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- **Boris's runs** (`ai_run_*.csv` and `boris_native_*.csv`) are produced by the **Player's checks** (§14, **no longer with keys**) and land on the **server that ran them**, not on the client. *They're measurements for diagnosing/comparing, not takes to convert (§16.1–16.2).*

The wizard looks for your recordings on the **client** automatically, so it normally finds it by itself. If you run the server on another PC, you can also point it at that folder:

| Place | Folder |
|---|---|
| Client (where you drive) | `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` |
| Server (if you recorded there) | `<server>\profiles\BZ_AutoDrive_PathLogger\` |

> If you recorded and "it doesn't show up", check which machine you were on — it lands on that PC's client.

Two more files worth knowing:

- Next to each recording there's a **`header_*.txt`** = the **vehicle fingerprint** (steering, gears, wheelbase). The wizard **reads** it to build the route to fit that car (it doesn't calibrate it: it reads it).
- The **finished routes** (the "trio") live in your server's routes folder (`RoutesDir` — see `[6]`).

### The wizard menu
You launch the wizard (double-click `Wizard.bat`) and the menu appears:

> **[1] Convert · [2] Import a BrigadaZ Transport v1 route · [6] Configure paths · [Q] Quit**

#### [1] Convert — from recording to ready route
You take a raw recording (`frame_*.csv` + its paired `header_*.txt`) and the wizard turns it into a **route deployed and ready in the Player**, in one step. The only thing it asks you is a **NAME** → the route comes out as `BZBusRoute_<name>` and shows up under that name in the Player.

**What it generates and where it goes.** From one recording come **three files** (the "trio" the server reads), all in your **routes folder** (`RoutesDir` — see `[6]`):

| File | What it is |
|---|---|
| `BZBusRoute_<name>.json` | the editable **master** (header + all the waypoints) |
| `BZBusRoute_<name>_hdr.json` | the **header** alone — vehicle, fingerprint, driving config (no waypoints) |
| `BZBusRoute_<name>_wp.csv` | the **waypoints** in 21 columns (*fast-load*: the server reads them without parsing the JSON) |

> It runs the **checks + the split** by itself and tells you it came out **deployed** (hot-loads, **without a restart**). If you configured a 2nd server, it offers to **copy the trio** there. *Tip:* convert the same CSV **twice** with a different name+mode (`coast_m1` / `coast_m3`) → both stay in the Player for comparison.

#### [2] Import a BrigadaZ Transport v1 route
Were you using **BrigadaZ Transport v1** and already have a recorded route? **Don't record it again** — this option brings it into BZ_AutoDrive's format.
- **Where it looks for the file.** The v1 route **isn't in the PBO**: it lives in **your** server's profile, and that folder **can be named anything** (a repack/fork renames it), so the wizard **doesn't guess it: you point it out** with **[B]** (and it remembers it). Point it at your `profiles\` and it searches **all the subfolders**. It also looks at the **`_importar`** folder it creates for you inside your routes folder (a mailbox for a JSON you bring from **another** server), and with **[P]** you paste the path to a specific `.json`.
- **Which vehicle it's for.** It asks you for the **vehicle's identity** (`Wheelbase`, `Fingerprint`), which the v1 take doesn't know. It lists what you already have: any **recording** of that vehicle (the `header_*.txt` — a **10-second** one works) or an **already-calibrated take** (those also carry the measured brake). It puts first the one that matches the vehicle your route declares.
- **The obstacle profile.** It asks you whether Boris should get past stopped cars in the way: **Robust** (brakes + dodges, ideal for a 24/7 line bus), **Interceptable** (brakes and stays) or **None** (see §A.5b).
- **What it fixes for you.** v1 marked the stop where you pressed the key, not where the vehicle braked: stops declared **in motion** are flattened to 0 with the braking painted backwards; it collapses the repeated points from when you were stopped; it marks the end as a stop.
> What does **not** migrate: the driving profile is per vehicle. If your route is with another car, calibrate it anyway — it's *that* car's driving manual, not the route's.

#### [6] Configure paths — the server folders
The wizard **remembers** the folders in `wizard_config.json` (next to the script) so it doesn't ask you every time. On the **first run** it asks you for them; after that you change them here whenever you want *(Enter = keep each one)*:

| Folder | What it is |
|---|---|
| **RoutesDir** | where the server's **routes** go (the trio). It's your `…\profiles\BZ_AutoDrive\`. **The only indispensable one.** |
| **Client recordings** | where it reads your `frame_/path_*.csv` from (by default `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`). |
| **2nd server (mirror B)** | if you have a separate test server, it offers to copy the route there on converting. `-` = none. |
| **v1 routes** | where it looks for the BrigadaZ Transport v1 takes (for `[2]`). |

> The folders are **portable**: a modder with the server on another disk sets it **once** and never touches `-RoutesDir` again.

### What the conversion does (and what it doesn't)
It's a **direct** conversion: `frame_to_route.py` takes your recording + its `header_*.txt` (the fingerprint) and builds the trio — it **generalizes** the line by reading the vehicle's physics from the config, and attaches the driving config. **There are no interactive linters or a "score"**: it's a pure converter, it won't ask you *"do I cap this curve?"*. The quality comes from **recording well** (§7): if the take came out dirty (you climbed a curb, you braked badly), **re-record** — it's free and fast.

> **The visual comparison** —your trace 🔵 against Boris's 🟠, with the "hot spots" where he strayed most— is now done in the **editor**: you import Boris's run (`boris_native`) as a layer and overlay it on your take. See §6B (*Import run/segment*) and §16.2.

---

## 6B. The trajectory and map editor — *draw instead of record*

Besides recording by driving, you can **draw the route directly on the map** — without getting into any vehicle. It's a browser tool (a **self-contained** HTML, installs nothing) that you open by **double-clicking `tools\editor\trajectory_editor.html`**. It's good for two different things, and that's why it has **two modes** (the switch up top):

> 🚗 **Trajectory** — you draw/edit **a vehicle's route** (the one Boris follows).
> 🗺 **Map** — you edit **the map's road network** (the roads routing uses). *This is admin/authoring.*

> **Everything is live:** what you draw or erase is **active instantly** (it's saved in the browser). **Export** is only for **deploying/publishing**, not for working.

**Maps available in the editor.** It's **a single editor** (`trajectory_editor.html`) with the **background already built** (relief, vegetation, buildings, footprints), that takes the map as a parameter. To open it for each map, open the matching file:

| Map | Open | What it is |
|---|---|---|
| **Chernarus** (`chernarusplus`) | `tools\editor\trajectory_editor.html` | the editor (Chernarus is the default) |
| **Livonia** (`enoch`) | `tools\editor\trajectory_editor_livonia.html` | shortcut → opens the editor with `?map=enoch` |
| **Sakhal** | `tools\editor\trajectory_editor_sakhal.html` | shortcut → opens the editor with `?map=sakhal` |

> All three open the **same** editor: the Livonia/Sakhal ones are 2-line **shortcuts** that pass the map. Adding a new map just needs another shortcut like these.

> **More maps** will be **added** in upcoming updates (the extraction pipeline is generic — it reads the size and surface from the scan itself, nothing is hardcoded per map).
>
> **⚠️ Note — this is only the editor's BACKGROUND, not a limitation of the framework.** The vehicle **drives on any map**: in the game the NPC reads the **real roadway surface** live, so **recording and running routes works everywhere**. A map not having a drawable background here yet just means that, for now, on that map you **record** the route (NUMPAD 5) instead of drawing it by hand in the editor.

### 🚗 Trajectory mode — build a route by hand

**The tools** (the *⚒ Tools* panel):
- **✎ Pen** — adds nodes at the end, curved. **╱ Line** — same but straight.
- **⭶ Cursor** — move a point (with its curve *handles*). **▚ Selection** — a box to grab several and move/rotate the group. **＋ Insert** — a click on the line adds a node there. **🗑 Delete node**.
- **📏 Ruler** — measure in meters (point to point and total); double-click or Esc to clear.
- **🖌 Speed brush** — paint the speed (the panel's value) onto the nodes. **↹ Reverse** / **★ Event** — you mark a reverse segment or an event node (§10), just like the keys when recording.

**The 🛣 Trajectory menu** — where the trace comes from:
- **📄 Load my take (wp.csv)** — open an **already-converted** recording and edit it (touch up the speed, fix the line).
- **⏺ Import run/segment** — bring in a **`boris_native`** to **COMPARE** (it comes in as a layer: your trace 🔵 vs Boris's 🟠, you see where he strayed — §16.2), or a **`frame_`** to **COMBINE** with your trace.
- **⟿ Auto-trace (by streets)** — you mark **start and destination** and the editor **routes on its own along the map's roads** (the graph, §11). Ideal for building a long trip without drawing every segment.

**The ⚡ Route menu** — the vehicle and its speed:
- **🚗 Assign vehicle** — who runs the route.
- **⚡ Recompute optimal speed** — computes the speed **optimal for the vehicle's physics** (grip in curves, brake, minimum radius). A starting point to tune by hand.
- **✔ Validate route** — it flags **impossible curves, unreachable speeds, impossible braking/accelerations** for that vehicle (to the ⚠ panel), *before* you run it.

**The layers** (*👁 View*) are the **framework's own background** (it uses no game assets): **Relief** (measured, hillshade + height), **Vegetation** (forest/field), **Buildings** (real footprint colored by type), **Footprints** (dirt). Turn on/off whatever helps you draw.

**The project** is saved as `.bzproj.json` (📁 Project → Save, `Ctrl+S`). When the route is ready, you **export the framework's 3 files** → deploy → play, just like a recording.

### 🗺 Map mode — edit the road network *(admin / authoring)*

It's for **building or fixing the road network** the graph (§11) uses for routing — adding a street the automatic extraction didn't pick up, or erasing one that came out wrong.

- **Road type** (the one you're going to add): **🛣 Highway** (asphalt, wide, with **lanes** — two-way, driven on the right) · **🌾 Dirt road** (medium, centerline only) · **🥾 Trail** (narrow, centerline only). *Rule of thumb: lanes? highway. Centerline only? dirt if it's medium, trail if it's thin.*
- **✎ Pen** to draw → **➕ Add trace to the road network** with the chosen type → it's **routable instantly** and persists.
- **🧽 Erase road (click)** — each click removes the road you touch, **including the ones that come by default** (it's an exclusion layer, it doesn't touch the base file). **↺ Restore erased** brings them back.
- **⬇ Export roads (flatten)** — builds the **final** `.js` to publish (additions + deletions). **No post-processing**: what the browser downloads you copy and you're done.

> **Undo/redo are per mode** (`Ctrl+Z` / `Ctrl+Shift+Z`): undoing in Map doesn't move the trajectory and vice versa. Language ES/EN with the 🌐 button.

---

## 7. How to record well (the quality comes from here)

The NPC will drive **as well as you drove** — the recording is its driving manual. The wizard warns you about problems, but **it can't invent a good line**: that one you put in when recording. These habits make the difference:

- **Drive smooth and even.** No sudden acceleration or hard braking: the NPC copies your profile. Brake **progressively and early**, before the curve, not on top of it.
- **Take the curves with the line you want** the NPC to make — don't cut the apex or run wide.
- **Constant rhythm on the straights:** the speed you record is the reference the NPC will use.
- **One clean take is worth more than ten dirty ones.** If you climbed a curb or braked badly, **re-record** — it's free and fast.
- **Direction changes (K-turn, shed):** brake fully, **shift gear and continue** — the **interchange is auto-detected** from the gear change (you press no key); reverse also comes from the gear (§9.4).
- **Stops:** brake completely and mark with **NUMPAD 4** where you want the NPC to stop.
- **Horn and lights:** while you record, **whatever you do with the horn (H key) and the lights (L key) is recorded** and Boris replays it **at the same spot** where you did it (spatial replay). Want the bus to honk on reaching the stop? Press **H** there while recording. Want it to turn the lights on entering a tunnel? Press **L** there. You configure nothing — you drive the way you want it to look. *(If you'd rather control it by config instead of recording it, see the `HornMode`/`LightsMode` modes in Appendix A.2.)*
  > **Heads-up about the lights:** by default `LightsMode` is **`auto`** (automatic lights at night — see §7 "Horn and lights"), so **the L keys you record only replay if you set `"LightsMode": "replay"`** on the route. With the `auto` default the lights are decided by the game's time, not by the recording. (The horn does replay by default.)
  > **For the lights to show:** the vehicle needs a **battery + installed bulbs (headlights)**. The framework energizes the battery on spawn, but the bulbs have to be among its `Attachments` (they usually already come from the vehicle's fingerprint). Without physical headlights, the L key turns nothing on.

> *Mental rule:* drive thinking "this is how I want the bot to do it". Whatever you do, it does.

---

## 8. The simplest use: a permanent bus service

It's the case the framework was **born** for: **a line bus that runs your server by itself, all day, without you doing anything.** No missions, no admin operating it — ambient transport for your players. (It's the "server-only" use, the most common one when you download the mod.)

How to leave it running as a permanent service:
1. **Record a route in a loop** (one that returns near the start) — e.g. the coast stopping in every town.
2. **Leave it as the server's default route** (`BZBusRoute.json`).
3. When the server starts, the bus **appears by itself and starts driving**. When it finishes the lap (or if it's destroyed), it **respawns** after `RespawnDelay` seconds (`300` by default) → **continuous service, 24/7**.

> It doesn't need a Quest or for you to be connected: just the mod loaded + your default route. You turn it on and forget about it.
> *Example:* the Chernarus coastal bus linking 14 stops, going around all day — players take it to get around, like a real bus.

**The three ways to run a route** (so you have them clear):
| Way | How it starts | What for |
|---|---|---|
| **Permanent service** | by itself, at server boot (default route + loop) | ambient line bus |
| **On-demand (Player)** | you pick it in the UI, no restart | testing, one-off events |
| **By mission (Quest)** | a quest triggers it | convoys, reinforcements, ambushes (§12) |

### 8.1 — Stop on demand: wave the bus down *(validated in-game)*

The line bus doesn't only stop at the `isStop` waypoints: **any player can ask it to stop by waving it down**, like in real life.

> **It comes off by default** (opt-in — it's a rule the admin chooses). To enable it, set `HailGestureEnabled = true` in the mod's global *settings*; without that, the bus only stops at the `isStop` waypoints. While it's active, the rule applies to **all** your routes.

**How to use it (player):**
- Stand **on the road, facing the bus** (where the driver can see you), at ≤30 m.
- Do the **OK / thumbs-up** emote (`ID_EMOTE_THUMB`).
- Boris **brakes, waits for you ~10 seconds** so you can get on, and **continues** the route.

**What happens under the hood:**
- The detection is **server-side**: the framework goes through the players near the vehicle, checks whether they're in the bus's **frontal cone** (dot product of bus-heading · direction-to-player > 0.25 → "the driver sees them", not behind or to the side) and whether they're performing the OK emote.
- On detecting it, it **pauses** the bus (reusing the pause-mode brake) and marks the resume at **+10 s**.
- After 10 s it resumes, **re-locating** the waypoint to the bus's actual position (key, see below).

**Parameters:** the **on/off** is already config (`HailGestureEnabled`, default off); the rest is currently in constants (easy to expose to JSON): radius 30 m · cone `dot > 0.25` (~±75°) · wait 10 s (20 ticks) · emote `ID_EMOTE_THUMB` (9).

**Two things that were hard to solve (notes for whoever extends it):**
1. **Reading WHICH emote the player is doing, server-side.** `EmoteManager.GetGesture()` does NOT work: it returns `m_GestureID` (a different field, set by `SetGesture()`), not the emote in progress. The actual emote lives in `m_CurrentGestureID`, which is `protected` and has no public getter. Solution: a `modded class EmoteManager { int BZ_CurrentGesture() { return m_CurrentGestureID; } }` — it's a script class (not engine → moddable), and the `protected` is accessible from the subclass.
2. **Resuming straight after the stop.** When it brakes, the bus keeps going ~10-15 m while it stops, but the waypoint index stays frozen where it braked. Since the index advance is capped by speed (it doesn't advance from 0 km/h), on resuming the bus is *ahead* of its target → the control was aiming at a wp left behind → **swerve to the side**. Fix: on resume, **re-locate the index to the waypoint closest to the actual position** (once, skipping the cap).

> **The gesture as a primitive.** It's the first use of a more general pattern: *the player's emote as a control input to the framework*. Today it's wired to "OK → stop", but the same mechanism (reading the emote server-side + a proximity/vision condition) works for any command (follow, wait, turn around). It's the player↔NPC counterpart of the triggers in the event DSL (§10).

---

## 9. How the NPC drives — it follows your line and your speed

**You don't pick a "mode": there's a single control**, and it's the one that reproduces your driving. The NPC:
- **follows your recorded line** (*pure-pursuit*: it aims at a point ahead on your trace), and
- **uses your recorded speed** —it flies on the straights, brakes before the curves, just like you—, **moderated** by what the vehicle **can** do in each curve: if you recorded a curve faster than that car can hold, it **caps it by itself**.

The throttle/brake come from an **inverse model** derived from the **vehicle's config** (engine, torque, gearbox), not from your pedals. That's why **the same controller drives any vehicle with no per-car tuning** (config-as-manual): each one drives it according to ITS physics — the small one nails the line, the heavy one does it slower but gets there. *(The recording itself belongs to its vehicle; to run the same **trace** on another car, you reassign it in the editor — §6B.)*

> **You configure nothing:** the wizard produces this control on its own, reading the car's fingerprint (§5). *(There used to be "modes 1/2/3" —repeat pedals / speed by curvature / recorded speed—; they were **unified into this single control**, the one this version validates. The flags that turned them on —`FollowPath`, `UseInverseModel`…— remain in Appendix A.2 as **advanced tuning**, in case a modder wants other behavior.)*

> **Is a heavy vehicle forced along at your pace?** The best thing is to **record the route with that vehicle** — that way the run is born to fit it. *(If you later want the same trace for smaller cars, draw/reassign it in the editor — §6B — rather than reusing the heavy vehicle's recording.)*

### 9.4 — Direction changes (K-turn, backing in): auto-detected

When your route needs to **change direction** —a three-point turn (**K-turn**), turning around, backing into a shed— **you no longer mark anything with a key**. The framework does it **on its own**: it detects the **direction change** from the **gear change (forward↔reverse)** you made when recording, and cuts the segment there (`legBreak`) by itself.

**How it's recorded — the rule: always STOPPED when reversing.** You can't switch from forward to reverse (or the other way) while moving —neither you nor Boris—, and that gear change **always happens at speed ~0**. Precisely because of that, the converter can read it unambiguously. In a K-turn:
1. You're recording in **forward**. You reach the point where you'll back up → **brake fully** → engage **reverse**. *(You press no key: the forward → reverse change is the interchange, and the converter detects it here.)*
2. You do the reverse: **slowly (3–8 km/h), with small corrections** (the arc doesn't have to be perfect, you correct it as you go). The framework **auto-detects** that this segment is reverse —from the gear— and treats it as reverse.
3. When you finish → **brake fully** → engage **forward** and continue. *(Again no key: the reverse → forward change is the second interchange.)*

You repeat at each direction change, always the same: **brake fully, shift gear, continue**. Each of those gear changes cuts the segment (`legBreak`), and Boris treats it as a **new start**: he arrives aligned, plants himself, and sets off cleanly in the new direction. It's the *cusp* (reverse↔forward), the control's hardest point — now **resolved** (§18).

> **A 0 km/h is NOT always an interchange.** Braking fully **without changing direction** (waiting, yielding) is a **pause**, not a segment cut. The converter tells the two apart: only the 0 km/h **with a forward↔reverse gear change** produces the `legBreak`. That's why you don't need to mark anything — the gear already says it all.

**What the framework does BY ITSELF (you mark nothing):**
- **The interchange** — comes from the **gear change** (forward↔reverse) recorded: that's where the converter puts the `legBreak` and cuts the segment.
- **The reverse** — comes from the **recorded gear**: if you drove in reverse, that segment is reverse. Boris reproduces it with his reverse controller (steering by the rear axle) and **generalizes it to each vehicle by its physics** (a wide-turning one reverses slower and thus follows the arc better).
- **The approach** to the maneuver — **automatic** (`ApproachAuto`): Boris brakes predictively BEFORE the maneuver, without you marking anything.
- **The final stop** (endpoint) — self-adaptive per vehicle + surface (§18).

> **Golden rule: brake on the straight, not on the curve.** The point where you brake to reverse direction is where the auto-detect **cuts the segment** — so do it where the **trajectory IS ALREADY STRAIGHT** (the heading has already settled), not mid-curve. The detail of why (reverse's steering angles belong to the vehicle, not to the road) and the multi-vehicle validation are in §9.5.

> *Tip (config-as-manual):* record the reverse **on the safe side** — entry as **straight** as possible and minimal corrections. The cleaner your demo, the cleaner the NPC. On an **uphill ramp** (shed) keep the momentum: **don't brake to zero on the climb**.

> *Takes that start in reverse.* The converter defines the direction of the **initial segment** by the **first real movement** (>1 km/h), not by the gear you're parked in. If your route starts by backing up, make your **first movement BE the reverse** —without creeping forward even a hair— and set off decisively, so the start is read as reverse.

**Fine maneuvers in the editor.** Want the EXACT arc of a parking spot, without depending on your reflexes? You draw it in the **editor** (§6B) — it gives more control than recording it by hand (the editor marks the interchange at the node, also without a key).

> **`maniobra` (legacy):** there used to be a `maniobra` mode (direct replay with exit by waypoint crossing) that you marked with a key. **Deprecated** since 2026-06-17 and **removed from the menu**: cruise + automatic approach + geometry cover it. Doesn't apply to new takes.

### 9.5 — Making a trace with a maneuver work across vehicles (where to cut the direction change)

The **reverse** is **direct replay (open-loop)**: the NPC reproduces the **EXACT steering angles** you did, **with no lateral correction** (no cruise steering it back onto the road). Hence the **"arrive ready"** premise: the vehicle must reach the reverse segment in the **pose and speed** you demonstrated — because it's open-loop, an entry error is not corrected.

**The fine point: steering angles belong to the vehicle, not to the road.** The same steering angle produces a **wider or tighter radius depending on the wheelbase** (a long car runs wide, a short one closes in). So an open-loop segment **over a curve does NOT generalize** to another vehicle: the recorded car closes the curve, but a longer car, with those same angles, runs wide and takes it out.

**The golden rule — cut on the straight, not on the curve.** Make the **direction change** (brake fully before reversing, that's where the auto-detect cuts the segment) where the trajectory **IS ALREADY STRAIGHT** (the heading has already settled), not mid-curve. That way:
- The **curve** is handled by the **closed-loop control** (the pure-pursuit follows the **path**, vehicle-agnostic — each car takes it with ITS own wheel).
- Only the **straight + the maneuver** run as open-loop replay, which on a straight is safe (no curve that can run wide).

**Cross-vehicle: the editor, not the recording.** A **recording** belongs to **its** vehicle (it captures its *fingerprint*, its measured brake, its gear) — feeding it a foreign header causes problems, so **header-swapping recordings is not recommended**. To run the **same trace with a maneuver on another vehicle**, today's path is the **editor**: you load the trace and **assign it the vehicle** (or change it whenever you want — "Assign vehicle", §6B). The **drawn trace is vehicle-independent**, and there the cut rule above does apply (the maneuver on a straight). The framework, for its part, **retrains nothing per vehicle**: it reads the assigned car's config (engine, torque, gearbox — "config as the driving manual") and builds its curve advisory and inverse model to fit THAT car. *(A recording header-swap still exists as a legacy technical mechanism, but the recommended path is the editor.)*

> **Validated (the cut rule).** Testing the **same trace** on two vehicles of different wheelbase —an **OffroadHatchback** (2.357 m) and a **CivilianSedan** (2.935 m, longer)—:
> - with the **cut on the straight** → it completed the maneuver without crashing; the reverse nails the interchange **well aligned** (stop precision resolved — §18).
> - with the **cut on the curve** → the longer Sedan **ran wide and crashed** (those steering angles were the Hatchback's).
>
> Practical takeaway: **for a trace with a maneuver to work across several vehicles, the cut goes on a straight** — and you change the vehicle in the editor (§6B).

**The approach to the maneuver is automatic (`ApproachAuto`).** The control brakes predictively before the direction change, without you marking anything. If the cut falls in a **fast zone** and you notice it **brakes too much** and plants itself right at the transition, the answer is to **cut the maneuver where you're already going slow** (not mid fast-straight) — that way the entry is smooth.

**No alignment teleport (snap OFF by default).** The *ModeEntrySnap* —the little teleport that aligned pose+heading when entering the reverse— now ships **off by default**. The genuine control (rear-axle reverse steering, which nails the heading to **<1°**) positions **without teleporting**. If a specific route ever needs it, re-enable it by config (`ModeEntrySnapEnabled` — Appendix A.7).

---

### Horn and lights — the `HornMode` / `LightsMode` modes

Apart from **how** it drives, a route defines **what it does with the horn and the lights**. The horn by default **replays what you recorded** (spatial replay — §7); the lights, on the other hand, **are automatic by default** (see below). You can force another behavior without touching the recording, in Appendix A.2:

- **`HornMode`** — `replay` (default, honks where you did) · `stops` (honks at each stop) · `finish` (honks on reaching the end) · `off` (never).
- **`LightsMode`** — **`auto` (DEFAULT)** · `off` · `auto_inverted` · `replay` · `on` (detail below).

**Automatic lights by default (`auto`).** As of the current version, **every take starts in `auto`**: at night the vehicle **turns the headlights on only when the engine starts** (the moment Boris sits down and starts it, before it begins to roll — not mid-route), and during the day it leaves them off. **You don't have to record anything**: even if the take has no recorded L key, the lights come on by themselves at night. The "night" threshold is the game world's time: **lights ON from 19:00 to 06:00**, OFF the rest of the day.

The five `LightsMode` modes:

- **`auto`** — *(default)* turns on at night (19:00–06:00) when the engine starts, off during the day. Ignores the recording.
- **`off`** — lights **always off**. This is the override for **stealthy night missions**: the vehicle doesn't give itself away with the headlights even at night.
- **`auto_inverted`** — the opposite of `auto`: **turns off at night** (time-based stealth) and on during the day.
- **`replay`** — replays the **L keys you recorded** (turns on/off exactly where you did — §7).
- **`on`** — lights **always on**, day and night.

> **How to TURN OFF the lights.** If you want a route to run **in the dark** (stealthy night mission, not giving the vehicle away), put this in the route's header/JSON:
> ```json
> "LightsMode": "off"
> ```
> With `off` the headlights never turn on, even at night and even if you recorded L keys. (To go back to the automatic behavior, remove the field or set it to `"auto"`.)

> *Example:* a normal passenger bus doesn't need to touch anything — with the `auto` default it already turns on at night by itself. An infiltration convoy uses `"LightsMode": "off"` (or `auto_inverted`) to avoid giving itself away in the dark.
> Remember: for the lights to show, the vehicle needs a **battery + installed headlights** (§7).

---

## 10. Events and sequences — make things happen on the route

A route isn't just driving from A to B: you can make **things happen** at defined points — the vehicle stops, people get off, an audio plays, bots appear. That's built with the **NUMPAD 4** marker + the events.

### 10.1 — Your first sequence, step by step (from scratch)

*Goal: make the bus STOP at a point, play a message, wait 3 seconds and continue.* We do it from start to finish.

**Step 1 — Record and mark the point.**
You drive the route normally (NUMPAD 5 to start recording). When you reach the place where you want something to happen (the stop), you tap **NUMPAD 4** *once*. You keep driving to the end and tap **NUMPAD 5** to cut.
> Each NUMPAD 4 leaves a mark at that exact point. You can mark several points in the same lap.

**Step 2 — Run it through the wizard and note the number.**
You run the wizard → **Convert** → pick your recording. It generates the route and, on converting, shows you a **list of markers** with the **waypoint (wp) number** of each NUMPAD 4. For example:
```
[ 1] wp 88   'parada'   dur=0s rad=0m  mode=normal
```
> **Note that number (88).** It's the address of your mark — you'll use it to hook the action.

**Step 3 — Where the mark ended up in the file.**
Open the route JSON: `C:\DayZServer\profiles\BZ_AutoDrive\BZBusRoute.json`. Your NUMPAD 4 ended up as a waypoint with **`"isStop": true`** and, in the example, it's **wp 88** (index 88 in the `Waypoints` list).
> *How to find it if you didn't note the number:* search for `"isStop": true` in the file — that's your point. Its position in the `Waypoints` list is the wp number.

**Step 4 — Hook the sequence from there.**
In the same JSON, you add (or edit) the `Events` block, pointing to that wp:
```json
"Events": [
  {
    "wp": 88,
    "trigger": { "type": "wp_reached", "wp": 88 },
    "actions": [
      { "verb": "freeze_vehicle" },
      { "verb": "play_sound", "value": "MiVoz_SoundSet", "delay": 0.5 },
      { "verb": "resume_route", "delay": 3.0 }
    ]
  }
]
```
> The **`"wp": 88` is the hook**: it connects your mark (the NUMPAD 4) with what happens there. Change `88` for the number the wizard gave you. The `actions` run in order, with `delay` in seconds.

**Step 5 — Deploy and test.**
You save the file, deploy (the wizard does it, or you copy the `_hdr.json`/`_wp.csv`), and run the route from the Player. The bus reaches wp 88 → **brakes → the message plays → waits 3s → continues.** That's your first sequence. 🎉

> *For more actions (dropping bots, starting the engine, etc.) and more triggers (when a player approaches, when it takes damage), keep reading — the complete list is below.*

---

### 10.2 — How it works (reference)

When you recorded, you tapped NUMPAD 4 at the important points. Each one ended up as an **event node**. Then you hang **actions** on it: *"when the vehicle gets here, do this"*.

Each event has two parts:
- **When it fires** (the *trigger*): on reaching a waypoint, when a player approaches, when the vehicle takes damage, X seconds after starting…
- **What it does** (the *actions*, in order, with an optional delay to choreograph).

**The actions you have** (the "verbs"), by family — **these already work**:
- **Vehicle:** `start_engine` / `stop_engine` (start/stop the engine), `freeze_vehicle` / `unfreeze_vehicle` (lock it/release it), `set_vehicle_mortality` (**damage switch**: unbreakable ↔ destructible, hot), `set_driver_mortality` (Boris mortal or not), `repair_vehicle`, `refuel` / `drain_fuel`, `lights_on` / `lights_off`, `horn`, `despawn_vehicle`.
- **Route:** `stop_route` / `resume_route` (pause and resume the advance).
- **Cargo:** `add_cargo` — puts items in the trunk (you change the **vehicle's inventory** at a point on the route).
- **People:** `spawn_guard` (armed bots at the side), `dismount_guard` (make them get off, animated).
- **Narrative / state:** `ui_broadcast` / `log_event` (message), `play_sound` (3D audio attached to the car), `set_var` (**you declare or change a mission state**).

**The triggers** (the WHEN): `wp_reached` (Boris reaches a waypoint), `player_in_radius` (a player approaches), **`player_enter_vehicle`** (a player **gets in** — the "onPlayerSit"), `vehicle_health_below` (the vehicle got hit), `timer` (X seconds after starting).

> **⚠️ The event does NOT force a stop.** Marking a NUMPAD 4 while **stopped** creates a real **stop** (`isStop`) — that's what you want for a taxi. But an event is just *"trigger → actions"*: you can hang it on **any** waypoint (with ★ Event in the editor, §6B) and have it fire **mid-cruise, without braking** — ideal for **declaring states** (`set_var`), turning lights on, honking or dropping a message as it passes. And the triggers that are **not** `wp_reached` (`timer`, `player_enter_vehicle`, `player_in_radius`, `vehicle_health_below`) don't even depend on a point: they fire wherever it is.

**Examples:**
- *Taxi (real stop):* at each NUMPAD 4 you hang `stop_route` → `play_sound` ("a passenger got on") → wait 3s → `resume_route`. It stops, picks up a passenger and continues.
- *Sets off when someone gets in (`player_enter_vehicle`):* the bus waits **with the engine off**; when a player sits down, it fires `start_engine` → `resume_route` and **leaves on its own**. *(Already exists as a trigger — no code to touch.)*
- *Change the inventory in transit (`add_cargo`):* on reaching the depot, it puts boxes/supplies in the trunk — without braking if you hang it mid-cruise.
- *Make it fragile in the ambush (`set_vehicle_mortality`):* it comes unbreakable; at the hot spot you make it **destructible** so the player can blow it up.
- *Accident (mission hook):* you hang `stop_engine` + `ui_broadcast` ("the bus broke down"). The player finds it stopped.

*(Conditional branching —"if X happens, do Y"— doesn't live here; that's handled by the Quests system, which couples separately. See §12.)*

---

## 11. The graph — build your own route network *(advanced)*

So far, one recording = one route. But you can record **many** (all the streets of a town, a chunk of map, a train's tracks) and the framework **connects them into a network**. Then you ask it "go from here to there" and it builds you a route **you never recorded whole**, combining the segments.

**What for?** So NPCs can go to any point in your city **without recording every combination**. You record the **streets**, not every possible trip.

**How does it connect?** Easily: you record a segment, and where it crosses or grazes another, an intersection is created. You add a new street and it joins the network without touching the previous ones.

**How do you build it?** In the **editor**, 🗺 **Map** mode (§6B): you draw and erase roads, and export the network. *(It's not the wizard.)*
1. You record the streets, each one **there and back** (each direction = a lane → important so the NPC drives **on the right**).
2. A script builds the network from the recordings.
3. You ask it to go from **A to B** → it builds you the composite route.
4. You deploy it like any route and run it.

> *Example:* you recorded 9 segments of a town (≈5 km of streets). You ask it "from the east terminal to the northwest corner" → it builds you a 367m route combining 8 segments, **that no human drove together**. Tested on multiple vehicles of different drivetrain and size, at **96-99%** precision.

**How much do I have covered?** A coverage tool (`bz_coverage.py`) draws your network on the map and tells you how many segments, how many km and how many intersections you have — so you see which streets you're missing.

> *Train tracks* are the easiest case: the network **is** the rails (no wheel, no invented turns — just accelerate and brake).

---

## 12. Missions — BZ_AutoDrive + Quests *(the star integration)*

This is the most powerful piece of the framework: making an NPC-driven vehicle **part of a mission** — reinforcements arriving, a convoy escaping, an ambush on the road. For that, BZ_AutoDrive couples with **DayZ-Expansion-Quests**.

### 12.1 Why two systems? (the division of labor)

The key is that **each system does what it knows how to do**:

- **Quest** = the **bots** + the **mission logic**. It spawns living, armed bots, and it's the owner of everything "mission-related": killing them gives reward, progression, victory/defeat conditions.
- **BZ_AutoDrive** = the **vehicle**. It spawns it, **drives** it along your recorded route, and coordinates the bots getting on and off.
- **eAI** (Expansion's base AI) = the bot's basics: walking, getting in and out of a car.

> *Why doesn't the framework do everything?* A bot the framework spawns on its own **has no mission logic** — killing it gives nothing (no reward, no progression). And without a Quest, the bots don't even "live" well (they appear like mannequins). **Quest is the only owner of armed bots with meaning.** That's why they compose: Quest provides the people and the meaning; you provide the vehicle and the driving — the piece Quest is missing.

### 12.2 How a mission works, end to end

1. The player **accepts the quest** (defines where and how many bots).
2. The bots **appear** when the player approaches the zone (they're "lazy": they materialize by proximity).
3. The framework **detects** those bots and **spawns the vehicle** at the start of your route.
4. According to the script: the bots **get on** (walking or already seated) and the NPC **drives** the route.
5. On arrival (or when a trigger fires), the bots **get off** — to deploy, to camp, to whatever the mission asks.
6. When the mission ends, the vehicle is **cleaned up** (it doesn't stay wandering around).

### 12.3 Example 1 — "The convoy that flees" *(validated in-game)*

*The idea:* there's a camp with 5 bots. If the player starts killing them, the survivors **escape in a vehicle**.

*How it's built:*
- In the **quest**: a camp with 5 armed bots at a terminal.
- In the **framework**: you record the route from the terminal to an escape yard, and mark it as "convoy that flees when 1 killed".

*What happens live:*
1. The player arrives → the 5 bots appear and the vehicle (a Cobra) appears by itself at the terminal.
2. The player kills 1 → **the flight fires**: the 4 survivors **stop shooting** (they "pacify"), **walk** to the vehicle and **get in** with the door animated.
3. The NPC **drives** to the yard.
4. On arrival, it **brakes** and the 4 **get off** (animated door).

> The trigger is the quest's **kill-count** (kill 1). The framework doesn't invent the logic — it *listens* to the quest and drives the vehicle.

### 12.4 Example 2 — "Intercept the convoy" *(ambush)*

*The idea:* an enemy convoy crosses the zone; the player ambushes it.

*How it's built:*
- In the **quest**: 3 bots at the start of the circuit.
- In the **framework**: a circuit route, marked as "ambush on taking damage", with the vehicle **destructible**.

*What happens live:*
1. The 3 bots start **already on board, armed**, and the NPC drives them around the circuit.
2. The player shoots them → **any damage** (to the vehicle or to a bot) fires the ambush.
3. The vehicle **brakes**, waits to come to a full stop, and the bots (+ the driver) **get off and camp** hostile.

> *Design insight:* this ambush is, deep down, a **generalized NUMPAD 4 marker** — a trigger ("on taking damage") that fires a sequence ("brake + get off + fight"). Today it comes pre-built as a mode; the direction is for you to define it yourself in the events (§10), without touching code.

### 12.5 The vehicle lifecycle (don't leave ghost cars)

In a mission, the vehicle **must not stay wandering around** when it ends. When the quest marks the objectives as complete, the framework **cleans up** the vehicle (despawns it and stops the driving). Other possible policies: leave it as **loot**, leave it on-site for a chained objective, etc.
> *Example:* in the convoy that flees, when the player finally finishes off the 4 in the yard, the quest marks "completed" → the Cobra despawns by itself. No ghost car is left on the map.

### 12.6 What you need to build it

- **Dependency:** requires **@DayZ-Expansion-Quests**. *(To publish the framework alone, this part is separated into an optional add-on, so the core doesn't force you to have Quests.)*
- **Your part (framework):** you record the vehicle's route + configure it with the convoy mode and, if you want, the bots that travel from the start.
- **The quest's part:** you define the bots and the logic (reward, conditions) in the Expansion-Quests editor.
- **The hook:** the framework *listens* for when the quest starts and where its bots are, and from there it coordinates the vehicle.

### 12.7 — How it's written (config examples)

The framework's part is configured in the **route JSON** (`BZBusRoute*.json`). Here are the real examples of the two scenes. *(The bots and the reward you define separately, in the Expansion-Quests editor.)*

**Scene 1 — convoy that flees.** The vehicle waits at the terminal; when the quest detects that you killed 1 bot, the others get in and flee:
```json
{
  "VehicleClass": "Star_APC_Cobra_white",
  "SpawnHoldSeconds": 600,
  "ConvoyMode": "flee_on_kill",
  "Crew": [],
  "Events": [],
  "Waypoints": [ /* your route: from the terminal to the yard */ ]
}
```
- `ConvoyMode: "flee_on_kill"` → activates the mechanic (pacify → get in → drive → get off).
- `Crew: []` → empty on purpose: **the bots are placed by the quest**, not the route.
- `SpawnHoldSeconds: 600` → the vehicle waits still until the flight fires.

**Scene 2 — ambush.** The bots start on board armed; any damage fires the deployment:
```json
{
  "VehicleClass": "x5mcompetition_orange",
  "ConvoyMode": "ambush_on_damage",
  "VehicleInvincible": false,
  "Crew": [],
  "Waypoints": [ /* the circuit */ ]
}
```
- `ConvoyMode: "ambush_on_damage"` → bots instantly on board + freeze + dismount on taking damage.
- `VehicleInvincible: false` → **key**: if the vehicle is unbreakable, it never takes damage and the ambush **doesn't fire**.

**Bots that travel from the start (`Crew[]`).** If you want the vehicle to be born with bots inside (without depending on the quest), you list them:
```json
"Crew": [
  { "cls": "eAI_SurvivorM_Boris", "seat": 1, "faction": "Raiders",
    "loadout": "BanditLoadout", "offsetRight": 2.0, "offsetForward": 0 }
]
```
- `seat`: 1+ (0 is the driver). `faction`/`loadout`: how it comes armed. `offsetRight/Forward`: where it spawns **outside** before getting in (so it doesn't end up inside the car's body).

**A sequence of events at a point (`Events[]`).** To hang actions on a NUMPAD 4 node — e.g. a stop with sound:
```json
"Events": [
  {
    "wp": 88,
    "trigger": { "type": "wp_reached", "wp": 88 },
    "actions": [
      { "verb": "freeze_vehicle" },
      { "verb": "play_sound", "value": "MiVoz_SoundSet", "delay": 0.5 },
      { "verb": "resume_route", "delay": 3.0 }
    ]
  }
]
```
- `trigger` = when (here: on reaching wp 88). `actions` = what, in order, with `delay` (seconds) to choreograph.
- Available triggers: `wp_reached`, `player_in_radius` (with `radius`), `player_enter_vehicle`, `vehicle_health_below` (with `threshold` 0..1), `timer` (with `seconds`).

### 12.8 — The hook in code (how the framework "listens" to the quest)

The integration is server-side, in two pieces. **(1)** A `modded class MissionServer` that notifies the framework when a quest starts:
```c
modded class MissionServer {
    override void Expansion_OnQuestStart(ExpansionQuest quest) {
        super.Expansion_OnQuestStart(quest);
        BZBusService.GetInstance().OnQuestStart(quest);
    }
}
```
**(2)** The framework saves the quest ID and **polls**, because the bots are *lazy* (they appear only when the player approaches the camp):
```c
void OnQuestStart(ExpansionQuest quest) {
    if (!quest) return;
    ExpansionQuestConfig qc = quest.GetQuestConfig();
    if (!qc) return;
    m_QuestCheckID = qc.GetID();                       // we save the ID
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 2000, true); // poll every 2s
}

void CheckQuestBots() {
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    bool exists = ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols);
    int totalBots = 0;
    if (exists)
        for (int i = 0; i < patrols.Count(); i++)
            if (patrols[i] && patrols[i].m_Group)
                totalBots += patrols[i].m_Group.Count();
    // totalBots > 0  -> the convoy materialized (we save the initial count)
    // the count DROPS -> one was killed -> we fire the trigger (flight / ambush)
}
```
> **The entry door:** `QuestPatrolExists(questID, patrols)` gives you the quest's living patrols, and `patrol.m_Group` are the bots. From there the framework reaches them — they're the references you need to board them onto the vehicle.

### 12.9 — How it boards the bots (the mechanic + the gotcha)

When the trigger fires, the framework grabs the quest's living bots and boards them. The non-obvious part —and the one that cost hours— is that you have to **pacify them first**: a bot *in combat* won't walk to a waypoint (eAI's FSM requires "no threat"):
```c
void BoardQuestBots() {
    Transport transport = Transport.Cast(m_Bus);
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
    int seat = 1;
    for (int i = 0; i < patrols.Count(); i++) {
        eAIQuestPatrol p = patrols[i];
        if (!p || !p.m_Group) continue;
        for (int m = 0; m < p.m_Group.Count(); m++) {
            if (seat > 5) break;                          // Cobra: 5 passenger seats
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
            if (!b) continue;

            // 1) PACIFY (otherwise they won't walk):
            b.eAI_SetPassive(true);
            b.eAI_SetThreatDistanceLimit(0.0);            // so it doesn't re-lock on the player
            for (int tt = 0; tt < 16; tt++) {             // drain the already-acquired targets
                eAITarget tg = b.GetTarget(0);
                if (!tg) break;
                b.eAI_RemoveTarget(tg);
            }

            // 2) Walk to the door of ITS seat and get in (animated):
            vector door, ddir;
            transport.CrewEntryWS(seat, door, ddir);
            // ... SetMovementSpeedLimits + AddWaypoint(door); the Tick boards it through the door ...
            seat++;
        }
    }
}
```
> **The gotcha that cost:** without pacifying, the bot in combat won't navigate → it ended up boarding by *teleport* (ugly, "mannequin"). With the combo `eAI_SetPassive(true)` + `eAI_SetThreatDistanceLimit(0)` + **draining the targets**, the bot "forgets" the player, **walks and opens the door** (animated, nice). *Trade-off:* they stay passive during the flight (consistent with the fact that they're escaping).

> *Even more detail (all the lifecycle functions, the complete list of verbs, the eAI API gotchas) in the **AI knowledge pack**.*

---

### 12.10 — Example 3 — "Take the bus" (Travel) *(validated in-game)*

The simplest objective type to integrate, and the **first one validated beyond convoys**: a **Travel** objective (`ObjectiveType` 3) is fulfilled when the player **reaches a position**. If that position is the **end of a route** of the framework, then **riding the vehicle fulfills the objective**. The bus *is* the trip.

**The new piece: the `QuestTravelID` field.** A route declares which quest triggers it. When the player **claims** that quest, the framework auto-spawns the route, waits `SpawnHoldSeconds` (so you can arrive and get on), and drives to the destination. Zero admin intervention — "you claim it and the bus appears and runs by itself".

**What you build (all config; on the quest side no code is touched):**

**1.** The Travel objective (`Objectives\Travel\Objective_T_8.json`) — the destination = the end of the route:
```json
{
  "ObjectiveType": 3,
  "ObjectiveText": "Viaja en el bus hasta la terminal.",
  "Position": [3322.8, 200.0, 13029.7],   // = last waypoint of the route
  "MaxDistance": 30.0,
  "TriggerOnEnter": 1
}
```

**2.** The quest (`Quests\Quest_42.json`) that wraps it, with its giver (a chalkboard) and its reward.

**3.** In the route (`BZBusRoute_..._hdr.json`), two fields:
```json
{
  "QuestTravelID": 42,        // the quest that auto-spawns THIS route
  "SpawnHoldSeconds": 20.0    // the vehicle waits 20s before starting (so you can get on)
}
```

**The in-game flow:**
1. You claim the quest at the chalkboard.
2. The vehicle appears at the start of the route and **waits 20s**.
3. You get on as a passenger.
4. Boris drives to the destination → you enter the radius → **objective fulfilled**, you collect the reward.

**The hook** (in `OnQuestStart`): when a quest starts, the framework scans the routes; if any declares `QuestTravelID` == that quest, it loads it (`LoadConfigFromPath`) and spawns it (`RespawnBus`); the route's `SpawnHoldSeconds` does the waiting. It's the **same mold** that later extends to Escort/VIP (the vehicle carries the VIP) and Delivery (the vehicle carries the cargo).

### 12.11 — Example 4 — Escort the VIP (AIVIP) *(validated in-game)*

The objective that **closes** the integration: an **AIVIP** (`ObjectiveType` 9, "escort the VIP to a place"). Expansion spawns a **VIP** (an eAI NPC, e.g. `eAI_SurvivorF_Gabi`); the framework **puts him into a vehicle** and Boris drives him to the destination, while you **escort** him (protect him along the way). The VIP arrives → objective fulfilled.

**Two distinct NPCs (don't mix them up):**
- **Boris** = the driver (always the framework's).
- **The VIP** = the quest's NPC, the passenger. You don't drive: you protect him so he arrives alive.

**The new piece: the `QuestEscortID` field.** Same as `QuestTravelID` (auto-spawns the route when claiming the quest), but it also **boards the VIP**:
```json
{
  "QuestEscortID": 10,         // the AIVIP quest that triggers this route
  "SpawnHoldSeconds": 35.0,    // generous: the VIP must spawn, walk and board BEFORE starting
  "VehicleClass": "CivilianSedan"
}
```

**The key insight — how the VIP is grabbed.** Here we had to read Expansion's code: the VIP is **NOT** in `QuestPatrolExists()` (that list is only AICamp/AIPatrol). It's **flagged with `Expansion_IsQuestVIP()`** and comes out of the global enum **`eAIBase.eAI_GetAll()`**. The framework scans that list, filters by the flag + proximity to the vehicle, and boards him into its own group (without touching the player's group). *(Useful technique: Expansion's `.c` files are read as text inside the PBO — no need to extract.)*

**The in-game flow:**
1. You claim `Quest_10` at the NPC (Denis, next to the chalkboard).
2. The **CivilianSedan + Boris** appears at the start of the route and waits 35s.
3. **The VIP** spawns nearby → the framework finds him (`eAI_GetAll` + `Expansion_IsQuestVIP`) → he walks up and boards.
4. Boris takes him to the destination (end of route) → he gets off → **objective fulfilled** (with `Autocomplete: 1`, it finalizes by itself).

**Gotchas we learned (skipping them costs dearly):**
- **The grab is NOT `QuestPatrolExists`** — for AIVIP it's `eAI_GetAll()` + `Expansion_IsQuestVIP()`.
- **The vehicle matters:** it has to have valid crew seats + **vanilla doors**. A carpack (we tried the X5M) **sank** (weird bbox) and its door **crashed the eAI FSM** (`OpenVehicleDoor`) → spam of errors → server overloaded. The **CivilianSedan** (vanilla, 4 doors) worked perfectly.
- **The VIP's seat:** send him to a **rear seat**, not the front passenger seat — otherwise the player and the VIP fight over the same seat and merge (it locks you inside the car).
- **Generous hold:** the VIP needs time to spawn, walk, and board before the car starts (`SpawnHoldSeconds` ~30-40s).
- **If the quest gets stuck** (changing an objective's config while the quest is active breaks it): set `Autocomplete: 1` + restart — the fulfilled quest auto-finalizes.

**What this closes:** with AIVIP validated, the objective types where the vehicle has a role — **AICamp/AIPatrol, Travel, AIEscort/VIP** — are all running. The rest (Collection, Crafting, etc.) **coexist** without conflict. That's the complete Quest integration.

## 13. Controls (the keys)

The framework uses **only 3 keys** — the recording cycle + opening the UI. Everything else (launch / stop / pause a vehicle, spawn empty ones, arm the loggers) is done **from the Player** (§14), not with keys. The 3 live in the **game menu**: `Options → Controls → the "BZ AutoDrive" section`, **rebindable** like any DayZ control, and they come **assigned by default**:

| Action (name in the menu) | Default | What for |
|---|---|---|
| **Open Control Panel** | `HOME` | **open / close** the Player (the door to the whole UI) |
| **Record (start/stop)** | `NUMPAD 5` | start / finish recording a route |
| **Mark Event / Stop** | `NUMPAD 4` | mark a **stop / event node** (§10) while recording |

> *The direction change is no longer a key:* the **interchange / K-turn** (§9.4) is **auto-detected** from the forward↔reverse gear change — you brake fully, shift gear and continue; the converter cuts the segment there and Boris treats it as a new start.

> *Typical flow:* you open the panel with `HOME`; to record, `NUMPAD 5` (with `NUMPAD 4` at the stops/events; at the direction changes you just brake, shift gear and continue — they're auto-detected), `NUMPAD 5` again to cut; then you pick the route in the Player and watch how it goes. Your bindings are saved in your profile (`Documents\DayZ\<profile>.dayz_preset_User.xml`).

> **⚠ Everyone SEES the controls, but they only work for the admin.** DayZ shows the "BZ AutoDrive" category in Controls to **all** players (it registers the inputs globally — it can't be hidden per admin). But the keys **do nothing** if you're not admin: the gate is **server-side**, validated on every action against your Steam ID (§14.1). That's why the menu was kept **minimal** (3 keys): a regular player sees a short, inert category, not a debug board.

> *Note:* the `ai_run` (Boris's black box) **no longer has a key** — it's armed from the Player's check (§14, §16.1). The old tuning/debug tools (live Parking/Reverse/Approach, SysID, gear markers, spawn slots) were removed from the menu; the maneuvers come from the **auto-detected interchange** (gear change) + the editor.

---

## 14. The interface (UI)

A single panel for the admin — the **Player** — which you open with a key (**HOME** by default, configurable). It gathers everything on one screen: the route list, what's being driven live, and the spawning of empty vehicles. It's split into three zones:

**① ROUTES (left) — your route list.** All the saved routes. You click one and **the NPC drives it instantly, without restarting the server**. On selecting it, below it shows the route's **sheet** (vehicle, waypoint count, distance, max speed) and a **scrubber** to run through the route wp by wp. Below the list, the **LOAD & SPAWN** button (launch the chosen route) and the **logger checks** (see below).
> *Example:* you have "Town bus", "Downtown taxi", "AB - Jeep" in the list; click one and it starts.

**② ACTIVE RUNNERS (right, top) — your live board.** Each vehicle being driven **right now** — your routes, the mission ones, the ones from server boot — with its **name + waypoint** and its **state** (`DRIVING` / `PAUSED` / `STOPPED`). For each one, four buttons: **RST** (restart from wp 0 of its route, without reloading), **TP** (teleport next to it to intercept it), **II** (pause / resume), **[]** (stop it and remove it). If there are many, the list **scrolls** with the wheel. Top right, **STOP ALL** to take them all down at once.
> *Example:* you see "Town bus — wp 88/166 — DRIVING". You pause it with **II** or TP next to it to get on.

**③ ACTIVE SPAWN VEHICLE (right, bottom) — the empty vehicles you seeded.** Each vehicle you spawned empty (with START/HERE/END, see below) appears here as a row —**route · wp · position**— with two buttons: **TP** (go next to it) and **DELETE** (remove it). You can leave **several** empties seeded around the map and manage them from here. It scrolls if there are more than fit.

**The bottom bar — spawning empty vehicles + scrubber.** It shows the selected route's sheet + the scrubber, and on the right **SPAWN EMPTY VEHICLE AT: START · HERE · END**: it spawns the route's vehicle **empty and drivable** at the start, at the scrubber's wp (**HERE**) or at the end. It's useful for **continuing a recording** (spawn the car where you left off and keep driving) or for seeding cars. It **no longer teleports you** to the vehicle: it appears in the **ACTIVE SPAWN VEHICLE** list (③) and from there you TP or delete it.

**The logger checks (opt-in, before you hit play).** Below the route list there are two boxes — **`[ ] boris_native`** and **`[ ] ai_run`** — that you **tick BEFORE launching the route** (LOAD & SPAWN). If they're ticked, that run records its trajectory **in sync with play**:
- **`ai_run`** — Boris's **black box** (run telemetry; see §16.1). It's armed **from this check** (no longer with a key) for that specific run.
- **`boris_native`** — Boris's **server-side trajectory** (same shape as a human recording), to **overlay it against your take in the editor** and see where he strayed (detail in §16.2).
> They come **unticked** (they record nothing). It's opt-in per run: you tick, hit play, and only that run writes the log. Without ticking, normal use, zero files.

> *Note:* the keys are rebound from the game's Controls menu (§13). The checks arm the loggers from the UI itself, without depending on the keys.

### 14.1 — Who is admin? — configuring `AdminSteamIDs`

We just said the controls and the UIs **"only take effect for you (admin)"**. But how does the framework decide who is admin? By your **Steam ID**.

**How it identifies you.** The server compares your Steam ID (the Steam64, also called *PlainId*) against a list named `AdminSteamIDs` in its settings file. If your ID is in that list, you're admin.

**The file.** `<server>\profiles\BZ_AutoDrive\BZAutoDrive_settings.json`. If it doesn't exist, you create it. The two fields that matter to you:
- `AdminSteamIDs` — list of strings with the authorized Steam IDs.
- `ControlPanelKey` — the key that opens the panel (`-1` = HOME by default).

**⚠️ THE MOST IMPORTANT PART (this is the security point):** if `AdminSteamIDs` is **EMPTY, EVERYONE is admin**. That's convenient for local testing, but on a **PUBLIC server you MUST put your Steam ID(s)** in there. If you don't, **any player** can open the panel, spawn and control the vehicles. It's the **first** thing to set before opening the server to the public.

**JSON example:**
```json
{
  "AdminSteamIDs": ["76561198000000000"],
  "ControlPanelKey": -1
}
```
You can put several IDs separated by commas. `ControlPanelKey: -1` = HOME key.

**How to find your Steam ID** (the Steam64 / *PlainId*): on your Steam profile, on **steamid.io**, or in the **server log** when you connect.

**Why it's safe.** The check is done **server-side on every action** (open panel, spawn, load route, stop…), comparing your Steam ID authenticated by Steam. It can't be spoofed from a modified client: the client-side gate is only a visual convenience.

> *Honest note:* a regular player can still **SEE** the "BZ AutoDrive" category in their Controls menu (DayZ shows the registered inputs to everyone), but the keys **do nothing** for them if they're not admin.

---

## 15. Audio (.ogg) — give your events a voice

With the `play_sound` verb (in a NUMPAD 4 event) you play an audio **3D attached to the vehicle**. The audio you put in your addon:
1. Put your `.ogg` in your addon.
2. Declare it in your `config.cpp` (a `CfgSoundShaders` + a `CfgSoundSets` — copy the example pattern).
3. In the event: `play_sound` with the SoundSet name.

> *Example:* your taxi, when picking up a passenger, says "good morning" — you record `buen_dia.ogg`, declare it, and fire it in the stop's event.
> *Tip:* to test without creating an OGG, use a sound that already exists (e.g. from the radio mod). *Careful:* sometimes a sound fired from the server doesn't reach the client — if it doesn't play, it has to be re-sent over the network (the infra is already there).

---

## 16. If something doesn't work

Some classics (the fine detail + more cases are in the **AI knowledge pack**, see below):
- **The vehicle appears but doesn't move** → almost always the route coordinates don't match the map (you recorded on another map). Record your own route on the map where you run it.
- **My NPC run wasn't recorded** → the **`ai_run`** check was unticked. Tick it in the Player *before* hitting play (§14).
- **The NPC doesn't enter a tight curve** → it's the most demanding segment (§18); the control **caps the speed** by itself, but if the vehicle is **heavy**, the best thing is to **record the route with IT** (it's born to fit it). If it's a genuinely tight maneuver (shed, direction change), it goes as **reverse** — see §9.4–9.5.
- **The audio doesn't play** → see §15 (server→client replication).

> **This framework comes with an AI knowledge pack:** a document meant to be handed to an AI (Claude/GPT/Gemini) that assists you in configuring your case, solving errors and building your routes. If you get stuck, that's the fast lane — the AI has all the framework's context loaded.

> **Performance tip — run the server on another PC.** If you iterate a lot (record, convert, test), it's worth **running the server on a 2nd PC** while your main PC runs the **game client + the wizard and the tools**. That way neither one chokes and the runs come out smoother (better for calibrating). The **deploy** to that 2nd server is: *build* the mod → *copy* the `@BZ_AutoDrive` to the 2nd server → *sync the routes* (the JSONs). The wizard already supports pointing at that 2nd server: you set its folder in **[6] Configure paths** and it offers to copy the routes there when converting.

---

### 16.1 — AI logger — Boris's black box (ai_run)

When a route **doesn't turn out as you expected** and you want to know *why*, the framework lets you record a **black box** of the NPC's run. It works like an airplane's *flight recorder*: it logs, second by second, what Boris did while driving, so you can diagnose the problem afterwards.

**What it is.** A **telemetry log** of Boris's runs. It doesn't change how he drives: it only **observes and notes** (position, speed, gear, which pedals he pressed, which mode he used). It's the raw material for understanding odd behavior.

**It's OPT-IN, off by default.** It's armed by **ticking the `ai_run` check in the Player BEFORE hitting play** (§14, **no longer with a key**) → it records in sync with that specific run. **If you don't tick it, nothing gets written.** It's not part of daily use: an admin who just wants their buses or convoys running **doesn't need it**.

**When to use it.** ONLY when a route misbehaves —Boris **runs wide on a curve**, **gets stuck**, **goes slow**— and you want to understand **why**, or when you're **tuning the config by hand**. The rest of the time, leave it off.

**Where it lives.** On the **server that ran Boris** (not on your client): `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv`. *Note:* if you tested the route on another server, the `ai_run` stays on **THAT** server — look for it on the machine where it ran.

**What it records.** Around **~27 telemetry columns**: the **position** and heading, the **actual and target speed**, the **lateral deviation** (already computed), the **inputs** it applied (`throttle`, `brake`, `steering`), the **rpm**, the gear, the **active mode**, the **event markers**, and the **waypoint it's chasing** (`wp_idx`) at that instant.

> **⚠ Important principle — make this crystal clear:** the `ai_run` is a **MEASUREMENT to calibrate AGAINST your take** (functional *feedback* on how Boris did), **NOT a new recording**. **Do not feed it to the wizard as if it were a take** — in fact the wizard **filters them out on purpose**. Why? Because feeding Boris's output back in **clones his own errors**: the route degrades generation after generation (it's Machine Learning's *model collapse* — the photocopy of the photocopy). The source of truth is always **your** human recording; the `ai_run` only serves to **measure the deviation** against that truth.

**How to get value from it.** Two paths:
- **With AI (recommended):** you hand the `ai_run` **together with the route** to an AI with the **AI Knowledge Pack** loaded, and it diagnoses it for you — where Boris struggled and what to adjust (which config field to touch).
- **By hand (if you're technical):** you open it (it's a CSV) and look yourself at where it **drifted**, **got stuck** or **lost speed** — cross-referencing it against your original take.

> *Example:* the bus always runs wide on the same curve. You tick the **`ai_run`** check, run the route, grab the `ai_run_*.csv` from the server's `profiles`, hand it to the AI with the route → it tells you *"at wp 340 the `steering` saturates (it hits the limit without reaching the turn): raise `CurvatureSteerBoost`"*. You adjust that, you **don't** feed the `ai_run` back as a take.

---

### 16.2 — boris_native — Boris's trajectory (to compare in the editor)

Apart from the `ai_run` (the diagnostic telemetry), the framework can record the **trajectory** of Boris's run: where he passed, at what speed, with what heading — in the **same format as a human recording**. It's called **`boris_native`**.

**What it is.** A CSV with the line Boris **actually drove** (position, heading, speed, `throttle`/`brake`/`steering`, gear, wheel angle), server-side, at ~40 Hz. Since it's in the same format as your `path_`/`frame_`, it can be **loaded into the trajectory editor** and **overlaid against your take** to see, at a glance, where Boris followed your line and where he ran wide.

**How it differs from the `ai_run`** — the key distinction:
- **`ai_run`** = **diagnostic telemetry** (lateral deviation, saturations, corridor, pedals) → answers **"why"** Boris did something odd. Read with the AI / by hand (§16.1).
- **`boris_native`** = the **raw trajectory** (the line + speed, in recording format) → answers **"where"** he went → it's **drawn/overlaid** in the editor against your take.

> One **measures**, the other is **drawn**. To **diagnose** you look at the `ai_run`; to compare the line by eye, you load the `boris_native` into the editor over your recording.

**It's OPT-IN.** It's armed by **ticking the `boris_native` check in the Player BEFORE hitting play** (§14). It lives on the **server that ran Boris**: `<server>\profiles\BZ_AutoDrive_PathLogger\boris_native_*.csv` (one per run, timestamped — never overwritten).

> **⚠ Just like the `ai_run`: it's a MEASUREMENT, not a new take.** Don't feed it to the wizard as a recording — feeding Boris's output back in **clones his errors** (§16.1, *model collapse*). It's for **comparing** against your human recording (the source of truth), not for regenerating the route.

---

## 17. Ideas — what you can build (and what could be added)

Framework + Quest open up a big space. These ideas are to inspire you: some you build **today** with what's there; others are **extensions** to add (new verbs/triggers). The rule: *if you can describe it as "a vehicle that goes from here to there and along the way X happens", you can probably build it — or add the missing verb.*

### Archetypes you build TODAY
- **Public transport** — bus/taxi with stops and events (§10).
- **Convoy** — reinforcements, escort, flight, ambush (§12).
- **Motorized patrol** — a vehicle in a loop around an area.
- **Logistics / supply run** — a truck that takes loot (`add_cargo`) to a point and drops it.
- **Rescue / extraction** — the vehicle arrives, picks up bots or players, gets them out.
- **Ambient event** — a car that crosses the town and leaves; life in the world, no mission.

> *Example combining:* mission **"protect the convoy"** — a framework truck does the route, the quest places enemies that ambush it, and the player has to defend it to the destination.

### Actions that could be added *(extension of the events DSL)*
- **Lock / eject seats** (`lock_seat` / `unlock_seat` / `eject_passenger`) — so **players can't get in** (or drop them out). The `slot` field is already **reserved** in the DSL (`BZAction`); the handler in `ExecuteAction` still needs to be written.
- **Change the faction** of the occupants at runtime (`set_faction`) — so Boris or his crew switch sides mid-mission (from neutral to hostile, for instance).
- **Driver swap** (`swap_driver`) — replace the driver at a point (Boris gets off, another gets on; or a quest bot takes over the driving).
- **Gestures** — wave, salute, point (on the roadmap; not implemented yet).
- **Beacons / hazard lights** — beacons on a breakdown. *(The regular **horn and lights** already work: they're recorded and replayed — see §7 and the `HornMode`/`LightsMode` modes in Appendix A.2.)*
- **Drop loot / open trunk** — leave an item at a point; open the trunk on arrival.
- **Call reinforcements** — spawn *another* framework vehicle (convoy chains).
- **Map marker** — make the vehicle appear on the player's map (follow the bus live).
- **Give / advance quest objective** — make reaching a point **complete an objective** (the framework as an objectives engine).

### Triggers that could be added
- **By occupancy** (`occupancy >= N`) — so the vehicle waits to **fill up** (N passengers/bots seated) before starting. Today `player_enter_vehicle` already fires when **the first one** gets in; the "when the N are on board" threshold is what's missing. Combined with `start_engine` + `resume_route` = the bus leaves **when the quota is met**.
- **On shooting** (`on_player_shoot`), **at night**, **by player count** in the zone, **on entering an area**.

### Framework + Quest combos *(the big space)*
- **Taxi that is a quest-giver** — you get in, it takes you, and on the trip it offers you a mission.
- **Boss who arrives by vehicle** — the boss appears driving, gets off and fights.
- **Mobile trader** — a vendor runs a route; players stop it to buy. *(Needs Market integration — parked idea.)*
- **Dynamic events** — random spawns of vehicles + routes, to make the world feel alive.
- **The framework as an objectives engine** — make the vehicle's events (arrived / was destroyed / delivered) be the **completion conditions** of a quest. *This is the project's north star.*

### Framework + each type of Quest objective

The integration was validated in-game with **AICamp/AIPatrol** (the convoys, §12), **Travel** (§12.10), and **AIEscort/AIVIP** (§12.11) — the types where the vehicle plays a role. Vehicle-less objectives (Collection, Crafting, etc.) coexist without conflict. The complete table:

| Quest objective | Vehicle's role (BZ_AutoDrive) | Example |
|---|---|---|
| **AICamp / AIPatrol** ✅ | brings/takes the bots, boards them and drives | the convoy that flees, the ambush (§12) |
| **AIEscort** ✅ | the vehicle **is what's being escorted** | "accompany Boris's convoy to the base"; if it arrives intact, completed |
| **AIVIP** ✅ | the VIP **travels in** the vehicle | "protect the VIP" while the NPC takes them to a safe zone |
| **Travel** ✅ | the vehicle **takes you** | "travel to Cherno" is completed by getting on the bus going there |
| **Delivery** | the vehicle **makes the delivery** | supply-run: the NPC truck takes the cargo to the point; the objective is that it arrives |
| **Target** | the target **rides in** the vehicle | "eliminate the boss" who escapes in the convoy → you chase it |
| **Action / Collection** | the vehicle **drops something** or **is the place of the action** | inspect the accident, repair the truck, collect the loot it dropped |

> *What's validated:* the framework "listens" to the quest and contributes the vehicle, whatever the objective is. The **three axes where the vehicle plays a role** are already running in-game — **AICamp/AIPatrol** (convoys, §12.3/§12.4), **Travel** (§12.10) and **AIEscort/AIVIP** (§12.11). The rest (Delivery, Target, Action/Collection) **reuse the same hook** with their own declarative field: the hook already exists, the use case is what's missing.

> *For modders:* if you come up with an action or trigger that isn't there, it almost always **fits into the existing DSL** (a new verb in `ExecuteAction`, a new trigger in the list). The AI knowledge pack shows you how to add it.

---

## 18. Scope and limits (what to expect)

So you know what to ask the framework and what not to:

- **Nominal regime:** it drives at the vehicle's *normal* limit. Extreme maneuvers (drifts, hard oversteer, countersteer) it does **not** reproduce faithfully — the engine's receiver doesn't deliver them. That's out of scope, and that's fine: the framework aims at realistic driving, not at stunts.
- **High speed:** it works, with **graceful degradation**. Tested up to ~190 km/h: the line loosens a bit with speed (from ~0.2m at standstill to ~0.8m at +120 km/h) but **doesn't break** (no oscillation, 99% within 2m).
- **Tight 90° curves:** it's the most demanding point. They come out, but there the vehicle shows — a heavy one runs wider than a small one.
- **The CONTROLLER generalizes** over a **wide R_min range** (from a small hatchback to a truck) with **a single config**: the same trace works for all, and the nuance is provided by each one's physics.
- **Maneuvers (parking/reverse/K-turn) across vehicles:** the cross-vehicle case is done in the **editor** —you assign the vehicle to the trace (§6B, §9.5)— and it works **as long as the maneuver's cut falls on a straight, not on a curve** (validated with the same trace on an OffroadHatchback and a longer CivilianSedan, parking 0.71 m). The **reverse→forward interchange** —the control's hardest point, a K-turn inside a shed— is **resolved and validated across a broad bank** (Golf FWD, BMW E60, OffroadHatchback, Hatchback FWD, Porsche GT2RS mid-engine, Sedan, Toyota 86 RWD, and the **Truck_01 truck**): **0 AutoRecovery rescues**, with physical rules and **no per-vehicle constants**. The interchange is **auto-detected from the direction change** (no need to mark it — see §9).
- **Endpoint (stop) precision — self-adaptive:** the stop brake **no longer uses per-route constants**; it self-configures per **vehicle + surface** (it reads the car's brake torque from config and the **real grip** under Boris) and is **traction-aware**. Without touching any parameter it nails the final point **under 0.5 m** on most vehicles, and up to **~1 m** on the largest / long-wheelbase ones. Validated out-of-sample across a wide range of tractions and sizes (Toyota 86 RWD **0.04** · Sedan **0.18** · Golf FWD **0.20** · Hatchback FWD **0.23** · Porsche GT2RS **0.33** · Sedan in reverse **0.47** · **Truck_01 truck 0.54** · Offroad 0.09–1.15 m), with **crosstrack ~0.3 m** and heading at stop **<1°** on cars (~3° on the truck). Includes the **endpoint after a curve** —a historical weak point— now resolved at the root: the control **follows your recorded speed** on the approach and only brakes in the last ~3 m (SEQ1 at 44 km/h → **0.34 m**). It generalizes by reading the game, not a formula.
- **What it does NOT do:** it doesn't invent routes (it needs your recording or the graph) nor discover behavior beyond what you demonstrated. It's deterministic and inspectable — that's its appeal.

> In one sentence: **faithful and predictable in the normal regime; the honest limit is in the extreme.**

---

## 19. Open frontier — take it further

This is open source for a reason: **I got this far; the next level is yours.** Here it is, no fluff, where the edge is and how to push it. If you're going to contribute, start with one of these.

### The big leap: extreme driving (a "v3")
*Where it is today:* it drives at the vehicle's **nominal** regime (§18). Drifts, hard oversteer, countersteer — the engine's receiver doesn't reproduce them faithfully.
*The leap:* a custom physics override (skip eAI's model on those segments) or an ML layer that captures what classic control can't. It's the biggest and the hardest — and the one that opens up "movie-style" driving.

### 100% autonomous wizard
*Where it is today:* it detects problems and proposes, but **your eye** confirms (the A/V/R/I cycle).
*The leap:* have it internalize the diagnosis (bias→`CenterOffset`, saturation→`SteeringScale`, lugging→gear) and **calibrate by itself**, from the recording to the ready route without intervention. The skeleton is already there (the heuristics base); closing the loop is what's missing.

### Multi-vehicle coordination
*Where it is today:* one vehicle per route.
*The leap:* real convoys — dynamic spacing, the rear one follows the front one, reformation if one falls. There's technique borrowed from ARMA/RV (breadcrumb + convoy separation) to draw inspiration from.

### The living NPC
*Where it is today:* the driver drives, but is mute.
*The leap:* a driver with an LLM that talks, reacts and even gives a mission on the trip. The design is already provider-agnostic (changing the model = one config line).

### More worlds
- **Trains** — the easiest case: the network *is* the rails, no wheel or invented turns. ~70% covered without touching anything.
- **Other engines** — the principle (demonstration + config reading + classic control) isn't exclusive to Enfusion; it's portable.

> **If you take one:** the AI knowledge pack leaves you the complete context —why each decision, what was tested, what failed— so you start **at the edge and don't repeat the path**. That's the idea of opening it: so the next person starts where I finished. Take it and keep going. 🚀

---

## Appendix A — Complete route config reference

All the fields of the JSON (`BZBusRoute*.json`). Almost all have a sensible default: **you only touch what you need**. Convention: a value of `-1` or `0` usually means *"use the code's default / disabled"*.

### A.1 Basics
| Field | Default | What it does |
|---|---|---|
| `VehicleClass` | `"ExpansionBus"` | which vehicle spawns |
| `DriverClass` | `eAI_SurvivorM_Boris` | the NPC driver |
| `RespawnDelay` | `300` | sec before respawning (continuous service) |
| `AverageSpeedMS` | `11` | average speed, for the ETA calculation |
| `SpawnHoldSeconds` | `3` | wait before starting (`0`=right away, `30+`=wait for mission trigger) |
| `VehicleInvincible` | `true` | unbreakable; `false` = destructible (enables damage/ambush) |
| `DriverInvincible` | `true` | Boris unbreakable; `false` = mortal (can die in combat / when taking damage) |
| `DriverClothing` | `[]` | the **driver's** clothing (classnames to equip). Empty = framework default outfit. See §A.1b |
| `MaxGear` | `6` | maximum gear of the automatic gearbox (FIRST=2 … SIXTH=7) |
| `Attachments` | `[]` | parts to equip on spawn (wheels, battery, spark plugs…) |
| `Wheelbase` | `0` | distance between axles (`0`=from the code); used by reverse |

### A.1b How to dress the driver

The framework dresses **Boris (the driver, seat 0)** with a configurable outfit **per route**. You define it with `DriverClothing` in the route header/JSON: a list of clothing **classnames**, each equipped into its slot.

```json
"DriverClothing": ["BZ_AutoDrive_TShirt", "PolicePants", "PoliceCap", "CombatBoots_Black"]
```

- **If you do NOT set `DriverClothing`** (or leave it `[]`): Boris uses the **framework default outfit** — `BZ_AutoDrive_TShirt` (the branded shirt, bundled inside `@BZ_AutoDrive`, always loaded) + `PolicePants` + `PoliceCap` + `CombatBoots_Black`. Old routes look exactly the same as before.
- **If you set `DriverClothing` with items:** that list **replaces** the default entirely (you provide the whole outfit you want; nothing is merged).
- Each classname is equipped via `CreateAttachment` by string: if a classname **does not exist or its mod isn't loaded**, that item **fails silently** (nothing breaks, it just doesn't appear). Use valid classnames (from a loaded clothing mod, or vanilla).

> **Scope:** this dresses **only the driver**. The clothing/loadout of the **convoy/crew bots** is handled by **Quest (Expansion Quests)**, not the framework — see §12.

### A.2 Speed / driving control *(advanced)*

> **The wizard produces the control by default (§9) — you normally do NOT touch these fields**; they're here as **advanced tuning**. By default the converted route uses **your recorded speed** (capped by the curve) with an **inverse model**: `FollowPath=false`, `UseInverseModel=true`. If a modder wants Boris to **compute the speed by curvature** instead of using the recorded one, they turn on `FollowPath=true`. *(These flags were the old "modes 1/2/3", now unified into the default control — §9.)*

| Field | Default | What it does |
|---|---|---|
| `GearStrategy` | `"auto_box"` | `auto_box` (the AT decides) or `follow_recording` (uses the recorded gear — for sport cars that spin in 1st) |
| `FollowPath` | `false` | `true` = computes the speed by **curvature** (ignores the recorded one); `false` (default) = uses your recorded speed |
| `FollowPathLatAccel` | `4.0` | lateral grip (m/s²) → defines the vmax per curve |
| `FollowPathMaxKmh` | `50` | speed cap on the straight (when `FollowPath=true`) |
| `FollowPathCurveSpan` | `5` | wp spacing to measure curvature (avoids noise) |
| `FollowPathSpeedSmooth` | `8` | smooths the speed profile (anticipates curves) |
| `FollowPathUseReference` | `false` | uses the recorded speed capped by the curve (part of the default control) |
| `UseInverseModel` | `false` | throttle/brake by PID + inverse model (vehicle-agnostic) |
| `InverseModelKp/Ki/Kd` | `-1` | gains of the speed PID (`-1`=default) |
| `InverseModelLowRpmMin` | `false` | damped gear (high gears in cruise, more stable) |
| `TargetSpeedSmoothWindow` | `0` | smooths the target speed (less PID oscillation) |
| `AccelShiftThreshold` | `999` | anti-catapult: upshifts if it accelerates too much (`999`=off) |

### A.2b Horn and lights *(human replay — §7, §9)*
| Field | Default | What it does |
|---|---|---|
| `HornMode` | `"replay"` | `replay` (honks where you recorded it) · `stops` (at each stop) · `finish` (on reaching the end) · `off` |
| `LightsMode` | `"auto"` | **`auto`** (default — turns on at night **when the engine starts**, 19:00–06:00; ignores the recording) · **`off`** (always off → **turns the lights off**, for stealthy night missions) · `auto_inverted` (off at night → stealth) · `replay` (turns on/off where you recorded it) · `on` (always) |

> **Default `auto`:** every take turns the headlights on by itself at night, recording nothing. The lights come on **the moment the engine starts** (during the spawn-hold), not mid-route. Night threshold = 19:00–06:00 (game world time).
> **Turn the lights off:** put `"LightsMode": "off"` in the route header (stealthy night mission → the vehicle doesn't give itself away).
> For the lights to show, the vehicle needs a **battery + installed headlights (bulbs)** (§7); the framework energizes the battery on spawn, but the headlights have to be in the `Attachments`.

### A.3 Steering
| Field | Default | What it does |
|---|---|---|
| `SteeringScale` | `-1` (auto) | steering scale; short wheelbase → lower it (auto derives it from the wheelbase) |
| `CurvatureSteerBoost` | `0` | amplifies the steering in a curve (against under-turn in 90°) |
| `PathSmoothWindow` | `5` | smooths the path positions |
| `CruiseLateralDeadband` | `0` | "wall": band where it doesn't correct (anti-microsteer); `0.5` recommended |
| `CruiseLateralKGain` | `1.0` | gain of the lateral correction |
| `CruiseLateralDamp` | `0` | lateral damping (kills the zigzag); `0.3` moderate |
| `CruiseLateralCenterOffset` | `0` | corrects the lateral bias (`+`=right, `−`=left) |
| `CruiseHybridSteerThreshold` | `-1` | uses the recorded steering if it exceeds the threshold (keyboard pulses) |
| `CruiseHybridThrottleThreshold` | `-1` | uses the recorded throttle when the human was accelerating |
| `CruiseFFWeight` | `-1` | weight of the curvature feedforward in cruise (`-1`=0.25) |
| `CurveThrottleEnabled` (+ `...LookaheadM/StartDeg/FullDeg/MinScale`) | `true` | cuts the throttle BEFORE a tight curve |

### A.4 Slopes
| Field | Default | What it does |
|---|---|---|
| `SlopeCompensationEnabled` | `true` | adds throttle uphill, subtracts downhill |
| `SlopeLookaheadWps` | `5` | how many wps ahead it looks at the slope |
| `SlopeGain` | `1.0` | how much it compensates (`1`=full) |
| `SlopeLateralGain` | `1.0` | corrects the lateral bias the slope introduces |

### A.5 AutoRecovery (so it always arrives)
| Field | Default | What it does |
|---|---|---|
| `AutoRecoveryEnabled` | `false` | if Boris gets stuck, it teleports him forward |
| `AutoRecoveryStuckTimeS` | `10` | sec stuck before teleporting |
| `AutoRecoveryAdvanceWps` | `5` | how many wps ahead it teleports |
| `AutoRecoveryCooldownS` | `8` | minimum between teleports (anti-spam) |
| `AutoRecoveryMaxPerMission` | `0` | `0`=unlimited, `X`=fails the mission if it exceeds |
| `StuckAdvanceTimeoutS` | `0` | a safety **separate** from AutoRecovery: if it doesn't advance any wp for N s, it **pushes** the index (no teleport). `0`=default 60 s. Runs even if AutoRecovery is off; raise it a lot to disable |

> **"Don't save Boris from Boris" philosophy:** AutoRecovery ships **OFF** on purpose. On clean terrain Boris is precise; if he gets stuck there, it's a **recording** problem (re-record the segment), not something the teleport should paper over. Turn it on only if you want the absolute "always arrives" guarantee (unattended service), accepting the visual jump.

### A.5b External obstacles — AR_OnWay *(validated on 5 vehicles)*
Different from AutoRecovery (which keeps Boris from getting stuck **on his own**), this keeps him safe from the **world**: another vehicle stopped in the way, or one that hits/pushes him. Two independent flags; **the wizard sets them** on converting (it asks you the profile).
| Field | Default | What it does |
|---|---|---|
| `ObstacleSlow` | `false` | **predictive** braking for a vehicle ahead (lookahead that grows with speed and the vehicle's physics; brakes at its real maximum until stopping before the obstacle) |
| `ObstacleEscape` | `false` | if the obstacle **persists** (or he's hit/pushed off the line), it **teleports** to the first clean wp past the obstacle |
| `ObstacleScanDist` | `50` | minimum (**floor**) path scan distance; the real lookahead grows with speed |
| `ObstacleStopDist` | `15` | how many meters from the obstacle it stops |
| `ObstacleCorridorHalf` | `2.3` | half-width of the lane: a car on the **shoulder** (lateral offset > this) does NOT brake Boris |
| `ObstacleEscapeWaitS` | `6` | sec braked at the obstacle before escaping |
| `ObstacleEscapeResumeKmh` | `10` | gentle speed on resuming after the teleport |

> **Profiles (the v1 import asks for them; in a new take they're set by config):** **Robust transport** = both ON (the bus gets past whatever blocks its path). **Interceptable** = `ObstacleSlow` ON + `ObstacleEscape` **OFF** (Boris brakes nicely for whoever blocks him but does **not** escape → the interception mission works). **None** = both OFF (pure replay). Requires `UseInverseModel=true` (the default control).

### A.6 Convoy / Quest *(see §12)*
| Field | Default | What it does |
|---|---|---|
| `ConvoyMode` | `""` | `""` / `"flee_on_kill"` / `"ambush_on_damage"` |
| `Crew` | `[]` | bots that travel from the start (§12.7) |
| `Events` | `[]` | event nodes (§10, §12.7) |

### A.7 Reverse and interchanges — *advanced*
*They're barely touched: the current flow is automatic. They're here in case you fine-tune a fine maneuver.*

> **The flow today is simple.** The **reverse** is auto-detected from the **recorded gear**, the **approach** is automatic (`ApproachAuto`), and the **direction change** is **auto-detected** from the forward↔reverse gear change —no key— (§9.4). The old **hand-marked waypoint modes** (`parking`, `maniobra`, `approach`) **are no longer produced** — their fields remain only for compatibility with old takes (marked *legacy* below).
| Field | Default | What it does |
|---|---|---|
| `DirectReplayFromWaypoint` | `-1` | from this wp, literal replay of the recorded inputs |
| `ManiobraTargetSpeedCap` | `18` | *(legacy)* cap of the old `maniobra` mode (deprecated, not produced) |
| `ParkingTargetSpeedCap` | `0` | *(legacy)* cap of the old `parking` mode (deprecated, not produced) |
| `ModeEntrySnapEnabled` / `...MaxDist` | `false` / `0.5` | snap (teleport) to the recorded pos+heading when entering the **reverse**. **OFF by default**: the genuine control positions without a teleport (§9.5); re-enable only if a specific route needs it |
| `AntiRollbackEnabled` / `...PitchThreshold` | `true` / `0.05` | locks brakes on a slope at speed ~0 (doesn't roll backwards) |
| `ParkingStanleyK` / `ParkingFFWeight` | `-1` | *(legacy)* Stanley of the old `parking` mode (deprecated) |
| `ReverseStanleyK` / `ReverseFFWeight` / `ReverseFFSign` / `ReverseFFMaxSteerRad` | `-1`/`-1`/`0`/`0` | bicycle-model control in reverse |
| `ReverseSteerGateOffset` / `ReverseSteerThrottleFloor` / `ReverseSteerMax` | `0` | anti-stall and anti-overshoot gates in reverse |
| `ReverseRecordedSteerThreshold` | `0` (→0.2) | follow the recorded steering in reverse (recording-as-manual) |
| `ReverseTargetSpeedCap` / `ReverseStanleyFineMax` / `ReverseHeadingDeadbandDeg` | `0` | speed ceiling / fine correction / angle deadband in reverse |
| `EndFreezeDisabled` | `0` | `0`=at the end, it brakes and freezes where the human stopped; `1`=continues without braking |
| `ReverseStanleyMinSpeed` | `0` (→2 m/s) | speed floor for the lateral correction in reverse (breaks the 1/v spiral at low speed) |
| `ApproachAuto` | `false` | **automatic** predictive braking into the maneuver — it's the default behavior (`approach` is no longer marked by hand) |
| `ApproachExitKmh` | `0` (→~20) | target speed at the end of the approach ramp |

> **Reverse speed — automatic, no config (validated on 4 vehicles, R_min 3.44–4.57).** Boris reverses at `min(recorded speed, speed his physics allow for the arc)` → it generalizes to any vehicle (a wide-turning one reverses slower and thus follows the arc better). As it approaches the **end** of the reverse segment it brakes by itself (*endpoint-taper*): on the **flat** it arrives at walking pace (killing the overshoot on the exit), **uphill** it keeps the momentum to climb (doesn't brake too much). There are no fields to touch — it comes from the vehicle's physics and the slope.

> **Cut the maneuver on a straight (to generalize — §9.5).** Direct replay (`DirectReplayFromWaypoint`) reproduces **vehicle-specific steering angles**: if the reverse segment starts **on a curve**, a vehicle with a different wheelbase runs wide. Make the **direction change** (brake fully before reversing, that's where the auto-detect cuts the segment) **where the trajectory is already straight** (the curve is handled by the closed-loop, vehicle-agnostic control). Watch out for the auto-approach (`ApproachAuto`): it may **brake too much** if the cut lands on a fast straight — that's why it's best to cut where you're already going slow.

> **Reverse→forward interchange + self-adaptive endpoint (RESOLVED).** The *cusp* (reverse→forward, the K-turn inside a shed) is **validated across a broad bank of tractions and sizes (FWD / RWD / mid-engine / 4x4 / truck), with 0 AutoRecovery rescues** — with physical rules (CuspExitHeadingBand + stop suppression with an anti-"baby-steps" latch), no per-vehicle constants. And the stop brake of the **final endpoint self-configures per vehicle + surface**: `GetMaxBrakeDecel` (brake torque from config / radius / mass) bounded by the **real grip of the surface** under Boris, **traction-aware** → it nails the point **under 0.5 m** on most (up to ~1 m on the largest / long-wheelbase ones) without touching anything. Validated out-of-sample (Toyota 86 RWD **0.04** · Sedan **0.18** · Golf FWD **0.20** · Hatchback FWD **0.23** · Porsche GT2RS mid-engine **0.33** · Sedan in reverse **0.47** · Truck_01 truck **0.54** · Offroad 0.09–1.15 m), with crosstrack **~0.3 m** and heading at stop **<1°** on cars (~3° on the truck). Includes the **endpoint after a curve** (a historical weak point): the control follows the recorded speed on the approach and only brakes in the last ~3 m (SEQ1 at 44 km/h → **0.34 m**).
| Field | Default | What it does |
|---|---|---|
| `EndpointThrottleCapEnabled` | `true` | enables the throttle cut + self-adaptive brake in the final endpoint zone |
| `EndpointStopDecelMS` | `0` (→auto) | target decel of the stop; `0` = derives it from the vehicle's config × the real surface grip |
| `EndpointStopDecelFactor` | `0.85` | fraction of the real max brake it applies (margin so it doesn't lock) |
| `FwdClimbFactor` | `1.6` | sustained throttle help to a **front-wheel-drive** to climb on the endpoint's uphill (avoids the "baby-steps") |

### A.8 Waypoints
Each `Waypoints[]` entry:
| Field | What it is |
|---|---|
| `pos` | `[x, y, z]` the position |
| `targetSpeed` | target speed (km/h) at that point |
| `targetGear` | gear (if `follow_recording`) |
| `mode` | `normal` or `reverse` — the reverse is **auto-detected from the gear** recorded; the direction change goes separately in `legBreak`. *(The old `parking`/`maniobra`/`approach` are no longer produced.)* |
| `isStop` / `stopDuration` / `stopRadius` | stop: whether it's a stop, how long it brakes, radius |
| `targetThrottle/Brake/Steering` | recorded inputs (only used if `hasInputData=1`, literal replay — advanced) |
| `hasInputData` | `1` = literal replay of your inputs (advanced, same vehicle); `0` (default) = control by config |
| `name` | the point's label (the stops) |

---

## Appendix B — For developers: extending with code

If you want to go beyond the JSON —add a verb, a trigger or a behavior— here are the extension points. It's **Enforce** (DayZ's scripting).

### B.1 The heart: how the framework drives the vehicle
The framework injects the inputs in the `OnInput` of a `modded class CarScript`, **after** eAI's (override-last):
```c
modded class CarScript {
    override void OnInput(float dt) {
        super.OnInput(dt);                    // eAI runs its logic (including ShiftTo(FIRST))
        if (!GetGame().IsServer()) return;
        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;
        // ... here the framework overrides gear/throttle/steer with ITS control ...
    }
}
```
> **The breakthrough was that order:** eAI forces 1st every frame; our override runs AFTER and sets the correct gear/inputs.

Surface the framework **writes** on `Car`: `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. And it **reads**: `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (this is where the wheelbase comes from), `WheelGetSurface(i)`, etc.

### B.2 Adding a new verb
All the verbs live in `BZBusService.ExecuteAction(Car car, BZAction action, int evIdx)`, one `else if` per verb. Real code pattern:
```c
private void ExecuteAction(Car car, BZAction action, int evIdx) {
    if (!action) return;
    string verb = action.verb;
    if (verb == "start_engine") {
        if (!car.EngineIsOn()) car.EngineStart();
    } else if (verb == "freeze_vehicle") {
        m_Frozen = true;
    }
    // ... you add your branch:
    } else if (verb == "mi_verbo") {
        // you have at hand: car (the vehicle), action (its fields: value, fvalue, msg, slot...),
        // m_WaypointIndex (where it is). Do your thing:
        car.SetHandbrake(1.0);
        BZBusLog.Info("[EVENT " + evIdx + "] mi_verbo executed.");
    }
}
```
The `delay` (choreography) is already handled by the dispatcher for you (`CallLater`). And that's it: the modder can now put `{ "verb": "mi_verbo" }` in their `Events[]`. *(For new fields, add them to the `BZAction` class — it's flat, doesn't break the parser.)*

### B.3 Adding a new trigger
Triggers are evaluated by `BZTrigger.type`. You add the field it needs to the `BZTrigger` class and a `case` in the evaluation (e.g. `on_player_shoot` with its condition).

### B.4 The Quest hook
The integration with Expansion-Quests comes in through a `modded class MissionServer`:
```c
modded class MissionServer {
    override void Expansion_OnQuestStart(ExpansionQuest quest) {
        super.Expansion_OnQuestStart(quest);
        BZBusService.GetInstance().OnQuestStart(quest);   // the framework "listens" to the quest
    }
}
```
From `OnQuestStart`, the framework **polls** `ExpansionQuestModule…QuestPatrolExists(questID, patrols)` to reach the living bots (they're lazy by proximity). *(The objective subclass `ExpansionQuestObjective*Event` does NOT compile in our scope → that's why the MissionServer hook + poll is used.)*

### B.5 Enforce gotchas (the ones that hit us)
Enforce isn't C# nor C++; these traps cost hours:
- **There's no ternary** `?:` nor multiline `if` with `&&` at the start of a line → "Syntax error". Use `if/else` or intermediate bools.
- **`Math.PI` and `Math.AbsFloat` don't exist.**
- **`new Class(args)` blows up** → create empty and set the fields.
- **Don't mod engine classes** (`CGame`, etc.) → it breaks on load.
- **"Formula too complex"** at ~9 operands with `+` → split into several lines with `+=`.
- **Sibling-branch scope:** variables in different branches of an `if/else` (or `case`) **collide** (they don't have their own scope like in C++) → rename them or hoist.
- **Inline arithmetic in string concat** (`"x" + (seat-1)`) breaks → hoist it to an `int` first.
- **AddonBuilder does NOT validate Enforce** — it compiles fine with syntax errors; they only fail when **loading the server**. Always confirm in the RPT.

> These gotchas (and more) also go to the **AI knowledge pack**, so an AI can warn you before you step on them.

### B.6 — Integrating BZ_AutoDrive as a dependency of your mod
Since it's open source, you can build **your own mod on top of** BZ_AutoDrive: have your mission, event or transport mod use the framework for the **driving**, without reimplementing anything. There are two ways to couple to it:

**(a) As a dependency + calling its API.** In your mod's `config.cpp`, depend on the addon:
```cpp
class CfgPatches {
    class TuMod {
        requiredAddons[] = { "BZ_AutoDrive" };   // the framework addon
    };
};
```
And from your code, the service is a singleton — you ask it to spawn/drive a route of yours:
```c
BZBusService srv = BZBusService.GetInstance();
srv.RespawnFromPath("BZBusRoute_MiRuta.json");   // spawns the vehicle and drives that route
// (RespawnAs(class) to change the vehicle at runtime)
```
Your mod decides **when** (a trigger of yours, a schedule, an event of your mod); the framework provides the **driving**.

**(b) Extending it with `modded class`** (B.1–B.4): you add your own verbs, triggers or hooks without touching the framework's code.

> *Example:* your mission mod, when starting one, calls `RespawnFromPath` so a convoy sets off driving — you provide the mission logic, the framework the vehicle. (Same division as with Quest, but from **your** mod.)

> *Naming:* the addon is called **`BZ_AutoDrive`**; the bus demo mod (the coastal service) is separate content that uses it. It's the name that goes in `requiredAddons`.


### B.7 — Build & deploy your fork

**To just use the mod:** subscribe on the Workshop — it ships signed, you don't have to build anything.

**To build a fork:** package the PBO however you prefer — **DayZ Tools** (AddonBuilder) or your own pipeline — and sign it with your **own** key (`.biprivatekey`). No build script ships: each modder packages their own way. Copy your `.bikey` into the server's `keys\` folder; otherwise the client kicks with "missing PBO".

**The one gotcha worth remembering:** when you rename, move, or delete a `.c` file, **clear AddonBuilder's `temp/` folder before repacking** — otherwise it packs stale ("zombie") versions of the code. After that, validate a clean RPT + an in-game smoke test before uploading to the Workshop.
