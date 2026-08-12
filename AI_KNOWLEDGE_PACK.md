# BZ_AutoDrive — AI Knowledge Pack

> **Para la IA que lee esto:** sos un asistente que va a ayudar a un admin/modder con **BZ_AutoDrive**, un framework de conducción autónoma de vehículos por NPC en DayZ (motor Enfusion, lenguaje Enforce). Este documento te pone en contexto COMPLETO para **configurar, programar, escribir, modificar y seguir investigando**. Es más exhaustivo que el manual (para humanos): es tu base de conocimiento operativa. Cuando ayudes, citá paths y classnames exactos, respetá los gotchas (§12) y verificá contra el código actual antes de afirmar.
>
> *Versión ES — **2026-08-11** (**intercambio SIN tecla**: se sacó NUMPAD 3 → el cambio de sentido se **auto-detecta** del cambio de gear forward↔reverse; quedan **3 teclas** HOME/5/4. Sobre 2026-08-10: **control UNIFICADO**: se retiraron los "modos 1/2/3" → un solo control [pure-pursuit + modelo inverso]; **wizard = conversor puro** — menú [1] Convertir / [2] Importar v1 / [6] Config, sin linters/BZ-Score/DriveMode; `ai_run`/`boris_native` desde los checks del Reproductor; **reversa auto-detectada** por gear; parking/maniobra/approach = LEGACY; endpoint autoadaptivo RESUELTO forward+reversa, validado **out-of-sample** en un banco amplio [FWD/RWD/motor-central/4x4/camión]: parada **por debajo de 0.5 m en la mayoría** (Toyota 86 0.04 · Sedan 0.18 · Golf 0.20 · Hatchback 0.23 · GT2RS 0.33 · Sedan reversa 0.47 · camión Truck_01 0.54), hasta ~1 m en los grandes (Offroad 0.09–1.15); endpoint-tras-curva SEQ1 44 km/h → 0.34 m; crosstrack ~0.3 m; rumbo al parar <1° autos, ~3° camión). Base 2026-07-03 (histórico): **MANIOBRA/PARKING = direct-replay + regla del corte**: el parking reproduce open-loop los ángulos de volante+pedales grabados — que son VEHICLE-SPECIFIC — así que el CORTE debe caer donde la trayectoria es RECTA [la curva queda en closed-loop Stanley, vehicle-agnostic]; M2 preferido sobre M3 para maniobras con corte [M3 auto-approach sobre-frena en zona rápida]; `ModeEntrySnapEnabled` ahora **false** por default; generalización header-swap offroad→Sedan validada [corte-en-recta]; endpoint forward+reversa RESUELTO 2026-08-11 (<0.5 m típico, ver arriba). Sobre 2026-07-02: **REVERSE resuelto**: K-turn/maniobra de cambio de sentido validada en 4 vehículos — velocidad = min(grabada, física), autoridad plena de volante, anticipación ∝ R_min, endpoint-taper terrain-aware; fix dt-real del freeze de cruise a alta velocidad; `maniobra` mode removido del workflow; arquitectura: los fixes de control son RUNTIME, no se hornean en la toma. Sobre la base 06-27: luces+bocina Fase 2 replay, INFORMES PDF/HTML, fast-load con luces/bocina). **Versión EN pendiente de esta pasada de actualización.** MIT.*

---

## 0. Cómo usar este documento

- **Orientate** con §1 (qué es) + §2 (estado, dónde estamos).
- Para **configurar** una ruta/escenario → §6 (config) + §7 (wizard/pipeline) + §8 (eventos) + §9 (quests).
- Para **programar/extender** → §11 (patrones de código) + §4–5 (arquitectura/control) + §12 (gotchas).
- Para **seguir investigando** → §14 (metodología) + §15 (frontera) + §5 (control internals).
- **Para GUIAR a un usuario en vivo** por el core loop (grabar → convertir → play), asumiendo que **NO programa** → seguí el **Walkthrough §7.G**: UN paso a la vez, confirmá antes de avanzar, traducí todo a acciones concretas (qué tecla, qué archivo, qué mirar) y verificá los archivos en disco. Para **MAPEAR una zona** (unir grafos de rutas) → **Apéndice D.7**.
- **Regla de oro:** este pack refleja el estado al 2026-06-27; el código es la verdad. Antes de afirmar una firma de función o un default, confirmá con un grep al archivo citado.

---

## 1. Qué es BZ_AutoDrive (resumen ejecutivo)

Una **capa de piloto sobre eAI** (DayZ-Expansion-AI). eAI da el cuerpo del NPC (caminar, subir/bajar de un auto) pero conduce naïve (navmesh de infantería, dirección = ángulo/π, **siempre 1ra marcha**, cero lectura de config). BZ_AutoDrive sobrescribe sus salidas de conducción para que un NPC maneje **cualquier vehículo** siguiendo **una demostración humana grabada**, leyendo la **configuración declarada del vehículo** ("config como manual de manejo") y aplicando **control clásico** (Stanley + feedforward + modelo inverso + crucero predictivo). **No entrena por vehículo.**

Tres ideas: (1) **demostración = mapeo de ruta** (DayZ no tiene red de caminos; la grabación la suple); (2) **config = manual de manejo** (el `config.cpp` declara torque/gearbox/steering; el framework lo lee, no lo adivina); (3) **control clásico interpretable** (determinista, inspeccionable, zero-training).

---

## 2. Estado del proyecto — DÓNDE ESTAMOS (2026-06-27)

**Hecho y validado:**
- **Conducción forward/cruise:** sólida. Generalización validada en un **banco amplio de tracciones y tamaños** (FWD / RWD / motor-central / 4x4 / **camión** out-of-sample) con una sola toma (89.9–99.5 % lateral ≤2 m; 100 % waypoints). Direccional mediana 1.4–1.6°.
- **Luces + bocina (Fase 2 replay):** Boris reproduce **bocina** y **luces** del humano por waypoint (`targetHorn`/`targetLights`); el PathLogger las captura al grabar. Config por ruta (`HornMode`/`LightsMode`). La bocina sincroniza directo; las **luces de un AI car observado** necesitaron una NetSync propia + forzado client-side (gotcha de red — §12). Ver §5/§12. **3er vehículo de ejemplo (EXAMPLE02)** con luces+bocina humano valida que generaliza (98.6 % completion, lat-dev 0.79 m).
- **Informes de diagnóstico (tooling PS, RETIRADO de la publicación):** el generador de informes PDF/HTML (`report_export.ps1`) se **movió a `..\BZ_AutoDrive_devtools\` y NO se publica**. El diagnóstico de una corrida hoy se hace leyendo el `ai_run` directamente (ver D.10). *(Histórico: generaba PDF/HTML multi-página self-contained con tres tipos de informe — toma humana, toma de Boris, comparativo.)*
- **Vehicle-recording match:** el recording es el manual de ESE vehículo (toma propia vs cruzada: dirección 0.046 vs 0.17, 0 vs 250+ saturaciones).
- **Ruteo emergente:** grafo + Dijkstra sobre segmentos grabados → rutas nunca grabadas, 96–99 % lateral.
- **Parking:** controlador de precisión validado (galpón, anti-rollback). **Se conserva.** **Naturaleza (2026-07-03): parking = DIRECT-REPLAY (open-loop).** Los wps tagged `parking` NO usan Stanley: el framework reproduce los ángulos de volante + pedales EXACTOS grabados. Premisa **"arrive ready"**: llegar al tramo en la pose+velocidad demostrada. *(LEGACY: el modo `parking` y su tecla de toggle **NUMPAD+ (`KC_ADD`) se eliminaron** — en tomas nuevas no se marca; el código conserva la branch solo para honrar tomas viejas.)* Ver §5 (regla del corte + M2 vs M3).
- **Maniobra/parking — GENERALIZACIÓN por corte-en-recta (2026-07-03, validado):** direct-replay = ángulos de volante = **VEHICLE-SPECIFIC** (mismo ángulo → distinto radio por wheelbase). **Regla del corte:** cortar donde la trayectoria es RECTA (heading plano) → la curva queda en **closed-loop** (Stanley sigue el camino, vehicle-agnostic) + la recta en replay → generaliza. Cortar en CURVA → open-loop → otro wheelbase deriva → no generaliza. Validado **como método** por **header-swap** (offroad wb 2.357 → Sedan wb 2.935): corte-en-recta **completó** en ambos; corte-en-curva el Sedan más largo derrapó. **Postura actual (cross-vehículo):** una **grabación es de su vehículo** (su fingerprint/freno/gear); NO se recomienda header-swapear grabaciones en producción. El cross-vehículo se hace en el **EDITOR** (dibujar/cargar la **traza** vehicle-independiente + **asignar el vehículo**); el header-swap queda como mecanismo técnico legacy (sigue sirviendo de método de validación). Preferir **M2** (`FollowPathCapByRecording=true`) sobre M3 para maniobras con corte (§5).
- **`ModeEntrySnapEnabled` = FALSE por default (2026-07-03, era true):** el teleport de alineación en transiciones de modo se apagó — el control cerrado (parking + reverse rear-steer, heading <1°) posiciona solo. El snap además solo disparaba a <0.5 m (`ModeEntrySnapMaxDist`). Reactivable por `_hdr`.
- **Endpoint (parada final) autoadaptativo = RESUELTO (2026-08-11):** el freno de parada se autoconfigura por **vehículo + superficie** (torque de freno del config + agarre real, tracción-aware), sin constantes por-vehículo. Precisión de parada **por debajo de 0.5 m en la mayoría** (hasta ~1 m en los grandes / wheelbase largo), validada **out-of-sample** en un banco amplio (FWD / RWD / motor-central / 4x4 / camión, forward y reversa): Toyota 86 RWD **0.04 m** (rumbo -0.6°) · Sedan **0.18** · Golf FWD (reversa en curva) **0.20** · Hatchback FWD **0.23** · Porsche GT2RS motor-central (reversa) **0.33** · Sedan en reversa **0.47** · camión Truck_01 (wheelbase largo, 43% reversa) **0.54 m** (rumbo -2.8°) · Offroad **0.09–1.15 m** (el más suelto). Crosstrack mediana **~0.3 m** (hasta ~0.5 m en FWD); rumbo al parar **<1°** en autos, **~3°** en el camión. El **endpoint tras una curva** (histórico punto débil) quedó resuelto **de raíz**: el control **sigue la velocidad grabada** en la aproximación y sólo frena en los últimos ~3 m (SEQ1 a 44 km/h → **0.34 m**) — es el mecanismo, NO un parche por-ruta.
- **Reverse (maniobra de cambio de sentido / K-turn):** control **validado por datos en 4 vehículos** (R_min 3.44–4.57: Nissan / r32 / Sedan / Camaro), sin re-grabar. Modelo bicicleta **rear-steer** (Stanley 180° + signo invertido + `ShiftTo(0)`, control point anclado al **eje trasero**). Tres piezas nuevas (2026-07-02): (1) **velocidad = min(grabada, física)** vía `ffRev` (fracción del volante máximo del vehículo para el arco local, ya normalizada por R_min) → generaliza per-vehículo (giro ancho → `ff` alto → más lento → sigue el arco); (2) **autoridad plena de volante** — el `SteeringScale` anti-sobre-rotación de forward **NO** se aplica en reverse (le cortaba el volante a la mitad) + **anticipación ∝ R_min**; (3) **endpoint-taper terrain-aware** — frena hacia el fin del bloque reverse por **distancia de path** (no por el target, que salta al forward-resume), y el floor **sube con la pendiente** → en cuesta arriba no taperea (self-guard) y preserva la trepada. Resultado: sobre-pasada de salida **16 m → 5.5–8.7 m**, arco lat-dev ~0.5 m. El **K-turn** = maniobra grabada para que el NPC **invierta el sentido de marcha** en la ruta (forward → reverse en ángulo → forward opuesto). **Único pendiente:** validar la rampa del galpón (cuesta arriba).
- **Quest integration:** 2 escenas jugables in-game (convoy `flee_on_kill`, `ambush_on_damage`). Hook MissionServer + poll + boarding animado con pacificación.
- **UI:** Control Panel + Reproductor (carga de rutas en caliente, sin restart).
- **Wizard = conversor puro:** menú **[1] Convertir (solo NOMBRE) · [2] Importar toma v1 · [6] Configurar paths · [Q]**. Sin linters, sin BZ-Score, sin elección de modo — `frame_to_route.py` lee el header, generaliza y escribe el trío desplegado. Launcher `Wizard.bat` (doble clic); paths portables (`wizard_config.json`); sin editar JSON ni correr .py/.ps1 sueltas. Ver §7.
- **Migración:** el mod se renombró de `BrigadaZ_Transport` a **`BZ_AutoDrive`** (server A hecho+verificado; B pendiente). El v1.0 `BrigadaZ_Transport` queda congelado en Workshop; BZ_AutoDrive se publicará como item nuevo.

**Abierto / en progreso:**
- **Reverse en cuesta arriba (rampa del galpón):** ÚNICO caso de reverse sin validar por datos. El endpoint-taper terrain-aware está diseñado para NO taperear en pendiente (el floor sube con el grade → queda por encima del target grabado → self-guard → no dispara) y preservar la trepada de ~3 km/h + slope-comp sin meter un brake que apague el empuje. Falta la corrida de validación (se graba aparte). El reverse en **PLANO (K-turn) está cerrado**.
- **Modos de waypoint:** el conversor actual (`frame_to_route.py`) solo **produce `normal` y `reverse`** — la reversa se **auto-detecta del `gear==0`**, y el cambio de sentido (`legBreak`) se **auto-deriva del cambio de gear forward↔reverse** (siempre a ~0 km/h; un 0 sin cambio de sentido = pausa, no intercambio; el editor también lo marca en el nodo). Los viejos `parking` · `approach` · `maniobra` **ya no se marcan ni se producen** en tomas nuevas (se sacaron del menú de teclas, igual que la vieja tecla de intercambio NUMPAD 3, 2026-08-11); el código **conserva las branches** solo para honrar **tomas viejas** que las tengan (ej. K-turn EXAMPLE18). `approach` hoy es **automático** (`ApproachAuto`).
- **Gestos** (eAI emotes): pendiente de verificar API + implementar verbo `play_gesture`.
- **Wizard 100% autónomo:** falta cerrar el lazo de auto-diagnóstico.
- **Manejo extremo (drifts):** fuera de alcance (el receptor eAI suaviza).

**Visión a futuro:** NPC con LLM (el framework ya tiene forma de agente: DSL=acciones, grafo=navegación, config-read=percepción). Ver §15.

---

## 3. Mapa del codebase

**Source:** `E:\BRIGADA Z PVE SERVER\MOD-SCIPTS\BZ_AutoDrive\` (backup en `E:\BACKUP\mod\MOD-SCIPTS\`).
**Build output:** `E:\BRIGADA Z PVE SERVER\MODS\BZ_AutoDrive\` (addons/ + keys/). **Deploy A:** `C:\DayZServer\@BZ_AutoDrive\`. **Deploy B (2da PC, offload):** `Y:\@BZ_AutoDrive\` (rutas sincronizadas a `Z:\BZ_AutoDrive\`; B corre el SERVER vía Radmin VPN mientras la PC principal corre el cliente + las tools). **Cliente:** `!Workshop\@BZ_AutoDrive\` + se carga desde `C:\DayZServer` (ver §13). **Sync A↔B siempre manual** (robocopy `@BZ_AutoDrive` a B → triple match por hash).

**Scripts (PBO), por scope Enforce:**
- `scripts\3_Game\` — helpers tempranos: `BZBusCommon.c`, `BZBusRPC.c` (enum de RPCs, incl. `RECEIVE_TOAST`), `BZBusStops.c`, `BZGearRangeTable.c`, `BZPathLogCommon.c`, `BZCleanupConfig.c`, `BZBusClientManager.c`.
- `scripts\4_World\` — **el núcleo**: `BZBusService.c` (singleton server-side; spawn, Tick, control, eventos/verbos `ExecuteAction`, quest hooks `OnQuestStart`/`CheckQuestBots`/`BoardQuestBots`; ~6500 líneas), `BZBusCarScript.c` (el `modded CarScript` override-last; también la **NetSync de luces** propia `m_BZLightsWanted` + `BZSetLights`/`OnVariablesSynchronized` para que un AI car observado muestre el haz — ver §12), `BZInverseModel.c` (PID velocidad + gear + slope + surface), `BZBusConfig.c` (clase `BZBusRouteConfig` con 50+ campos + `BZAction`/`BZTrigger`/`BZMarkerEvent`/`BZCrewMember`), `BZBusPlayerBase.c` (modded PlayerBase: OnRPC, NUMPAD handlers de grabación), `BZPathLogService.c` (PathLogger: grabación + fingerprint), `BZRouteCleanup.c`, `BZILCCorrections.c`, `BZBusStopZone.c`, `BZExpansionWreckFilter.c`.
- `scripts\5_Mission\` — UI + hooks de misión: `BZBusMissionGameplay.c` (poll de inputs del menú de Controles en OnUpdate), `BZBusMissionServer.c`, `BZQuestHook.c` (`modded MissionServer Expansion_OnQuestStart`), `BZReproductorUI.c` (Reproductor), `BZControlPanelUI.c`, `BZBusUI.c`, `BZDebugOverlay.c`.

**Config/data:** `config.cpp` (CfgPatches/CfgMods clase `BZ_AutoDrive`, prefix `$PBOPREFIX$`=`BZ_AutoDrive`), `data\` (bus_stops.json, gear_ranges.json, wrecks_cleanup.json), `gui\layouts\` (control_panel_v2.layout, etc., formato CPP-style), `gui\textures\` (.paa), `stringtable.csv`, `meta.cpp` (name=BZ_AutoDrive, publishedid=0).

**Tools (PowerShell/Python, NO en el PBO — corren en la PC del admin):** `tools\` ahora es **solo el runtime del wizard + el editor**: `tools\route_wizard.ps1` (TUI; menú [1] Convertir / [2] Importar toma v1 / [6] Configurar paths / [Q] Salir), `tools\Wizard.bat` (launcher de doble clic, `-ExecutionPolicy Bypass` solo para esa corrida), `tools\frame_to_route.py` (**el conversor real**: grabación→trío `.json`+`_hdr.json`+`_wp.csv`, sin modos), `tools\transport_v1_to_route.py` (import v1), `tools\i18n_strings.ps1` (i18n del wizard, dot-sourced), `tools\driving_config_template.json` (el config de manejo que pega el conversor), `tools\wizard_config.json` (paths portables — **no se publica**), y **`tools\editor\`** (el editor de trayectorias y mapas, §10/UI). Las herramientas **dev/análisis/legacy** (validadores `enforce_lint.py`/`check_rpt.py`, análisis `endpoint_detail.py`/`analyze_*`, calibración vieja `calibrator_lib`/`route_calibrator`/`curve_advisory`, conversor viejo `csv_to_route`/`route_split`, informes `report_export.ps1`, asset-gen) se movieron a **`..\BZ_AutoDrive_devtools\`** (fuera del mod, **no se publican**).

**build:** empaquetá el PBO con DayZ Tools (AddonBuilder) o tu propio pipeline; firmá con tu propia llave. (No se distribuye ningún script de build — cada modder empaqueta a su manera.)

---

## 4. Arquitectura

**Override-last (el breakthrough).** El control se inyecta en un `modded CarScript.OnInput(dt)` que llama `super.OnInput(dt)` PRIMERO (eAI corre, incluido `ShiftTo(FIRST)`) y DESPUÉS sobrescribe `SetSteering/SetThrottle/SetBrake/ShiftTo`. Subordinación, no reemplazo: eAI nunca "sabe" que lo dirigen. Robusto a updates de eAI.

**Tres capas:** el **camino grabado** (posiciones 3D) = objetivo sagrado (se garantiza llegar a los waypoints en orden); la **física declarada** (config + Newton) = límites que no se violan; la **ley de control** = el puente.

**Flujo runtime:** `BZBusService` (singleton server) spawnea el vehículo + el chofer eAI, corre un Tick (~cada 500 ms para lógica de eventos; el control de inputs va en OnInput por frame), avanza el waypoint index, evalúa triggers y dispara `ExecuteAction` por verbo.

**Los fixes de control viven en el CÓDIGO, no se hornean en la toma (arquitectura clave).** Toda mejora de conducción (cruise, reverse, slope, endpoint-taper, AR_OnWay…) es lógica runtime en `BZBusService`, gateada por modo/geometría — NO son campos horneados en el `_hdr`/`_wp`. Consecuencia: una **conversión header-swap** a otro vehículo (mismos waypoints, otro `VehicleClass`+`Fingerprint`) **hereda TODOS los fixes automáticamente y per-física** (leen wheelbase/maxSteer/R_min del `Fingerprint` + la geometría de los wps en vivo). *(Esto explica por qué el CONTROLADOR generaliza con una sola config; es la base técnica de que el editor pueda asignar cualquier vehículo a una traza. OJO de postura: el header-swap de una **grabación** es un mecanismo técnico, no el flujo recomendado — una grabación es de su vehículo. Para cross-vehículo en producción, el camino es el EDITOR: dibujar/cargar la traza vehicle-independiente + asignarle el vehículo.)* Por eso el K-turn generalizó a r32/Camaro sin re-tunear, y una toma VIEJA **mejora sola** al correr sobre un PBO nuevo (la data `_wp` no cambia; mejora el código que la interpreta). Único matiz: los GAINS por-ruta que el conversor NO escribe (ej. `ReverseFFWeight`/`ReverseStanleyK`) caen a los defaults del código — si un vehículo puntual necesita otro gain, es 1 línea en su `_hdr` (sin rebuild).

---

## 5. El stack de control (internals + parámetros)

**Stanley lateral.** `targetYaw = segHeading − atan2(K·lateralOffset, v)`, K=1.0 default. El `atan2(K·off, v)` atenúa por velocidad (fuerte lento, suave rápido) → mata el zigzag. **Gotcha del signo:** left-handed DayZ → `cross = AB.z·AP.x − AB.x·AP.z` (invertido = divergencia). **Lección:** modular K por curvatura local EMPEORA.
- Params: `SteeringScale` (-1=auto, derivado del wheelbase), `CurvatureSteerBoost`, `PathSmoothWindow` (0 recomendado en curvas cerradas; >0 corta curvas — bug histórico del divisor hardcodeado).

**Corredor / "paredón".** Banda muerta (semi-ancho de carril): dentro Stanley OFF (no perturba), fuera ON + damping. Params: `CruiseLateralDeadband` (~0.5), `CruiseLateralKGain` (1.0), `CruiseLateralDamp` (~0.3), `CruiseLateralCenterOffset` (bias lateral; +derecha/−izquierda; ~25 m de mediana por unidad — resolución 0.01–0.02). **Aplicar CenterOffset DESPUÉS del deadband** (bug FBC8571F: estaba sujeto al deadband).

**Feedforward.** Curvatura con lookahead (~20 m cruise, 1–3 m parking). Param `CruiseFFWeight` (-1→0.25). `CurveThrottleEnabled` corta throttle ANTES de curva cerrada.

**Modelo inverso (`BZInverseModel.c`).** PID velocidad: `targetSpeed → throttle/brake`. `UseInverseModel`, `InverseModelKp/Ki/Kd` (-1=default 0.4/0.05/x), `InverseModelLowRpmMin` (rpmMin×1.0 vs ×1.3; default true post Test C: gear amortiguado, menos lugging), `TargetSpeedSmoothWindow` (suaviza el target, menos varianza PID), `AccelShiftThreshold` (anti-catapulta). Freno predictivo: `aNeeded = u²/(2·dist) ± g·sin(pitch)`.

**Gear.** `GearStrategy = "auto_box"` (AT por RPM) o `"follow_recording"` (gear grabado; default recomendado para no-bus; sport cars patinan en 1ra con auto_box). `MaxGear` (FIRST=2…SIXTH=7). El InverseModel respeta follow_recording vía override de SelectGear.

**Pendiente.** `SlopeCompensationEnabled`, `SlopeLookaheadWps` (5), `SlopeGain`, `SlopeLateralGain`. El recording ya tiene horneado el terreno → seguirlo hereda la compensación.

**Luces + bocina (Fase 2 replay).** El PathLogger captura del humano las columnas `horn`/`lights` (vía `CarScript.Cast(parent)`); cada waypoint guarda `targetHorn`/`targetLights` (en `BZWaypoint`), y Boris las reproduce por waypoint (**replay espacial**: honkea/prende donde lo hizo el humano). Config por ruta: `HornMode` (`replay`/`stops`/`finish`/`off`) — el claxon GRABADO siempre se reproduce; `HornMode` controla solo el bocinazo AUTOMÁTICO; `LightsMode` (`replay`/`auto`/`auto_inverted`/`on`/`off`). **Bocina:** `CarScript.SetCarHornState(int)`, enum `ECarHornState{OFF=0,SHORT=1,LONG=2}`, funciona andando y **sincroniza al observador** porque `m_CarHornState` es `RegisterNetSyncVariableInt` + `SetSynchDirty`. **Luces:** requieren batería energizada (`OnBeforeLightOn()` exige `GetCompEM().GetEnergy()>0`; el fix en `EquipBus` hace `batt.GetCompEM().SetEnergy(GetEnergyMax())`). El **gotcha de red** de las luces en un AI car observado (y su fix v3 con NetSync propia) está en §12 — es de lectura obligatoria al tocar cualquier estado VISUAL de un AI car.

**AutoRecovery (clásico, stuck-based).** `AutoRecoveryEnabled` (default false), `AutoRecoveryStuckTimeS` (5–10), `AutoRecoveryAdvanceWps` (3–5), `AutoRecoveryCooldownS` (8), `AutoRecoveryMaxPerMission` (0=ilimitado). Teleporta a wp+N si Boris se traba (velocidad~0, o wp_idx sin avanzar >N s aun girando); preserva velocidad por impulso; loguea heatmap diagnóstico geográfico. **Filosofía "no salvar a Boris de Boris":** sobre terreno LIMPIO Boris es preciso → si se traba en camino limpio es bug de GRABACIÓN (taparlo lo esconde → re-grabar). Por eso queda default OFF.

**AR_OnWay (escudo contra obstáculos EXTERNOS del mundo — validado 100% en 5 vehículos, 2026-07-01).** Distinto del AR clásico: cuida a Boris del **MUNDO** (otro vehículo detenido, o uno que lo choca/empuja), no de su propio control. Dos flags **independientes**:
- **`ObstacleSlow`** = freno predictivo. Escanea el path adelante con **lookahead VARIABLE por velocidad + física del vehículo**: `scanMax = ObstacleStopDist + v²/(2a) + v·0.6 + 5`, con `a = min(ObstacleDecel, decel real del vehículo)` (`BZInverseModel.GetMaxBrakeDecel` del config, acotado por fricción·g) → una camioneta pesada de frenos flojos **mira de más lejos y frena a su máximo real, sola**. Frena hasta detenerse a `ObstacleStopDist`≈15 m. **Corredor lateral** `ObstacleCorridorHalf`≈2.3 m: mide el offset del obstáculo al eje del carril → un auto en la **banquina** o en el carril contrario NO frena a Boris (solo lo que le tapa el carril). También querea el **frente REAL de Boris** (no solo los waypoints grabados) para no perder un auto pegado/empujado.
- **`ObstacleEscape`** = teleport al primer wp **limpio** pasado el obstáculo, si persiste (>`ObstacleEscapeWaitS`≈6 s frenado) o si lo **empujan/chocan**. Gatillos robustos al flicker del scan: `owPersist` (frenado + visto hace poco), `owPushing` (quiere avanzar, target>15, pero kmh<10 sostenido tras ver obstáculo), nivel "tocando" (<5 m → 2 s). Resume suave a `ObstacleEscapeResumeKmh`≈10.

Config: `ObstacleSlow`/`ObstacleEscape` (bool, default false), `ObstacleScanDist`(50, **piso**), `ObstacleStopDist`(15), `ObstacleDecel`(4.5), `ObstacleCorridorHalf`(2.3), `ObstacleEscapeWaitS`(6), `ObstacleEscapeResumeKmh`(10). **Perfiles (toggle — la clave, runtime-settable por el quest):** **Transporte robusto** (ambos ON) / **Interceptable** (Slow ON + Escape **OFF** → Boris frena lindo ante el que lo bloquea y NO se escapa → la misión de interceptación funciona; si escapara la rompería) / **Ninguno** (ambos OFF = réplica pura). Gate `(ObstacleSlow||ObstacleEscape) && UseInverseModel` (el template ya trae `UseInverseModel=true` → engancha). El **import v1 pregunta el perfil** (**[R]**obusto / **[I]**nterceptable / **[N]**inguno → setea `ObstacleSlow`/`ObstacleEscape` en el `_hdr`); en una toma nueva se setean por config. **Borde abierto:** un choque **de frente** que empuja a Boris ~3 m off-path puede clavarlo (es territorio de **recuperación off-path**, distinto de "obstáculo adelante en el carril" — "casi cerrado"). Ver §15.

**Estrategia de velocidad — combos de flags (AVANZADO; ya NO es una elección del usuario).** El wizard produce **un solo control**: sigue la línea + la velocidad grabada (pure-pursuit + modelo inverso; `FollowPath=false` + template). Los viejos **"modos 1/2/3"** eran combos de estos flags y quedaron **unificados en ese control único**; siguen accesibles a mano en el `_hdr.json` para un modder que quiera otro comportamiento: **replay puro** (`FollowPath`/`FollowPathUseReference`/`UseInverseModel`=false; mismo vehículo, `hasInputData=1`), **geometría pura** (`FollowPath`+`UseInverseModel`=true; velocidad por curvatura+tope, pesados), **reference-assisted** (los tres true; velocidad grabada + steering por config = el generalizador). NO es un ranking.
**Maniobras con corte — comportamiento de los flags (avanzado/legacy, 2026-07-03):** `isM3approach = (UseInverseModel && !FollowPathCapByRecording)` — el **auto-approach es M3-ONLY**. Apunta a la entrada del parking como crawl (target ~5). Si el corte cae en zona rápida (recta ~23 km/h) **sobre-frena** → el vehículo se planta en la transición (caso real: Sedan M3-recta clavado en `wp2289`, on-path, spd 0). **M2** (`FollowPathCapByRecording=true`) cruza a la velocidad GRABADA → robusto y generaliza mejor. **Para maniobras con corte: preferir M2.**

**Controladores de maniobra:** **parking** (FF alto, lookahead corto, anti-rollback) — VIVO. **Es DIRECT-REPLAY (open-loop, 2026-07-03):** los wps tagged `parking` NO corren el lazo Stanley — el framework reproduce los ángulos de volante + pedales EXACTOS grabados (`targetSteering/Throttle/Brake`). Premisa **"arrive ready"**: llegar al tramo en la pose+velocidad demostrada. *(El toggle de grabación `mode="parking"` con NUMPAD+ se **sacó del menú de teclas** — parking/maniobra son **legacy**; en tomas nuevas no se marcan. El código honra las branches para tomas viejas.)* **Corolario (regla del corte):** como los ángulos son VEHICLE-SPECIFIC (mismo ángulo → radio ∝ wheelbase), el direct-replay **no generaliza en curva**. Para que otro vehículo complete (cross-vehículo = **asignar el vehículo a la traza en el editor**; el header-swap de la grabación es legacy): el CORTE al bloque de maniobra debe caer donde la trayectoria es **RECTA** (heading plano) — así la curva queda en **closed-loop Stanley** (vehicle-agnostic) y solo la recta va en replay. Detección del arranque de recta: **primera muestra post-maniobra con heading plano (<1.5° en 20 samples)**. Cortar en curva → el Sedan (wb 2.935) chocó; cortar en recta → completó (parking 0.71→1.11 m). Ver §2 (regla del corte). **`ModeEntrySnapEnabled` ahora `false` por default (era true):** el teleport de alineación en cambios de modo se apagó — el control cerrado (parking + reverse rear-steer, heading <1°) posiciona solo; el snap solo disparaba a <0.5 m. Reactivable por `_hdr`. **reverse** — VIVO y **validado** (§2, 4 vehículos). Modelo bicycle **rear-steer**: control point al **eje trasero** (`GetReverseControlOffset`≈wheelbase/2 — ancla también el FF, no solo el corredor), signo Stanley invertido, `ShiftTo(0)`, sigue volante grabado sobre `ReverseRecordedSteerThreshold`≈0.2. **Autoridad plena de volante:** el `SteeringScale` (anti-sobre-rotación de forward, ≈wheelbase/5.5) **NO** se aplica en reverse — le cortaba el volante a la mitad e impedía forzar el arco. **Anticipación ∝ R_min:** `floorFf = R_min·1.3` (R_min = wheelbase·cos/sin(maxSteer); `Math.Tan` no garantizado en Enforce) — giro ancho anticipa antes. **Velocidad = min(grabada, física):** `vPhysRev = MAX_REVERSE − (MAX_REVERSE−REV_PHYS_MIN)·|ffRev|` (MAX=`GetReverseTargetSpeedCap`≈25, MIN=5) → generaliza per-vehículo. **Endpoint-taper terrain-aware:** dentro de `REV_TAPER_M`(8 m) del fin del bloque reverse (medido por distancia de path recorriendo los wps) rampa el target a un floor que **sube con el grade** (`floor = MIN_PROG + grade·36` si grade>0.03; grade = dy/dist a la Y del endpoint) → **plano** frena a 3 km/h (mata la sobre-pasada de 16 m), **cuesta arriba** el floor supera el target grabado y el taper no dispara (self-guard, preserva la trepada). **Endpoint autoadaptativo (parada final) = RESUELTO (2026-08-11):** el endpoint-taper + el freno autoadaptativo por vehículo+superficie llevan la parada a **por debajo de 0.5 m en la mayoría** (hasta ~1 m en los grandes), forward y reversa, out-of-sample (Sedan en reversa 0.47 m, GT2RS en reversa 0.33 m; listado completo en §2). El endpoint tras curva sigue la velocidad grabada y sólo frena en los últimos ~3 m (SEQ1 44 km/h → 0.34 m). **Transición forward→reverse** honra freno-a-0 + handbrake + gear 0 grabados; la **salida** reverse→forward la resuelve el *handbrake-resume* (salta el cluster de handbrake-forward y retoma forward — antes se clavaba en gear 0 + brake 1). **approach** — VIVO: no es un controlador de steering aparte (usa el Stanley normal) sino un **tag de velocidad** que rampa el freno ANTES de entrar a una maniobra — hoy **automático** (`ApproachAuto`; ya **no** se marca con tecla) para no clavar el frenazo y derrapar. **maniobra** — **deprecado para grabaciones NUEVAS** (2026-06-17: fuera de UI/hotkeys), PERO las branches siguen en el código y honran tomas viejas que la tengan (ej. el K-turn EXAMPLE18). **Knobs de reverse (por `_hdr`, sin rebuild):** `ReverseFFWeight` (default 0.6=parking), `ReverseStanleyK` (default 0.8), `ReverseTargetSpeedCap` (25), `ReverseStanleyMinSpeed` (2 m/s, rompe la espiral 1/v). **Todos los fixes de reverse están gateados a `mode=='reverse'` / `isReversePk`** — no tocan cruise ni maniobra (regla de aislamiento).

---

## 6. Config de ruta (`BZBusRoute.json`) — referencia

Clase: `BZBusRouteConfig` en `BZBusConfig.c`. Defaults sensatos; `-1`/`0` = default del código / off. Grupos: **Básicos** (`VehicleClass`, `DriverClass`=eAI_SurvivorM_Boris, `RespawnDelay`=300, `SpawnHoldSeconds` [0=al toque, 30+=esperar trigger misión], `VehicleInvincible`, `MaxGear`, `Attachments`, `Wheelbase`); **Velocidad/modo** (§5); **Dirección** (§5); **Pendientes** (§5); **AutoRecovery** (§5); **Convoy/Quest** (`ConvoyMode` ""/"flee_on_kill"/"ambush_on_damage", `Crew[]`, `Events[]`); **Maniobras** (parking/reverse params, §5). **Waypoints[]:** `pos[x,y,z]`, `targetSpeed`, `targetGear`, `mode` (normal/parking/maniobra/reverse), `isStop`/`stopDuration`/`stopRadius`, `targetThrottle/Brake/Steering`, `hasInputData`, `name`. Embebe un `Fingerprint` (del header del recording). Para la lista exacta de campos: Apéndice A del manual.

**Attachments: NUNCA adivinar** (regla no-negociable). Vienen del fingerprint del recording (NUMPAD 5 los captura) o del trader package/config. Al cambiar `VehicleClass`, REEMPLAZAR attachments (el converter preserva los del JSON anterior = bug recurrente). El header del recording es la fuente autoritativa (el conversor ya lo prefiere).

---

## 7. El wizard + la pipeline + dónde viven los datos

**Pipeline:** grabar (NUMPAD 5 → `frame_*.csv` + `header_*.txt`) → `route_wizard.ps1` **[1] Convertir** (pide solo el NOMBRE → `frame_to_route.py` arma el trío `.json`+`_hdr.json`+`_wp.csv` y lo deja desplegado) → correr (Reproductor, hot-load sin restart).

**El wizard (`route_wizard.ps1`, TUI) — menú actual:** **[1] Convertir · [2] Importar toma v1 (BrigadaZ Transport) · [6] Configurar paths · [Q] Salir**. Es un **conversor puro**: **NO** linterea, **NO** da "puntaje/BZ-Score", **NO** elige "modo de manejo" (hay uno solo, §5). La calidad sale de **grabar bien** (§7.G): si la toma quedó sucia —te subiste a un cordón, frenaste mal—, **re-grabá** (es gratis).
- **[1] Convertir** — elegís la **grabación** (lista por **fecha**, la más nueva arriba) y le ponés un **NOMBRE** (→ `BZBusRoute_<nombre>.json`; así aparece en el Reproductor; Enter vacío = ruta activa default). El wizard llama `frame_to_route.py <frame> <nombre> --profile <RoutesDir>`: lee el `header_*.txt` (fingerprint), **generaliza** la línea con la física del vehículo, **auto-detecta la reversa por `gear==0`**, **auto-deriva los cortes de tramo** (`legBreak`) del cambio de gear forward↔reverse y escribe el **trío** (`.json` + `_hdr.json` + `_wp.csv`) ya **desplegado** (hot-load). **No hay elección de modo** — el control es único.
- **[2] Importar toma v1** — convierte una toma de **BrigadaZ Transport v1** (JSON monolítico con `Waypoints`) al formato AutoDrive. Te pide la **identidad del vehículo** (un `header_*.txt` de cualquier grabación de ese auto —sirve una de 10 s— o un `_hdr.json` ya calibrado) + un **perfil de obstáculos** (`robusto` / `interceptable` / `ninguno` → setea `ObstacleSlow`/`ObstacleEscape`). Corre `transport_v1_to_route.py`. Ver D.11 (guía de import v1).
- **[6] Configurar paths** (`Invoke-ConfigPaths`) — re-setea `RoutesDir` + (opcional) `ServerBMirror` (Enter = mantener, `-` = borrar el mirror); guarda en `wizard_config.json`.

**PRINCIPIO del wizard:** **todo se hace dentro del wizard**; orquesta `frame_to_route.py` (que ya escribe el trío `.json`+`_hdr.json`+`_wp.csv` de una) por vos. El usuario **NO edita JSON a mano ni corre `.py`/`.ps1` sueltas**. Carpetas de grabaciones **portables** (`Update-LogDirs`): ya **no** hardcodeadas — se derivan de `RoutesDir`/`ServerBMirror` (cliente vía `%LOCALAPPDATA%`). Launcher **`tools\Wizard.bat`**: doble clic, sin abrir PowerShell a mano (usa `-ExecutionPolicy Bypass` solo para esa ejecución).

**Robustez ante errores de operación:** cada opción del menú corre en su propio `try/catch`. Si una operación falla, **muestra el error y VUELVE AL MENÚ PRINCIPAL** — el wizard ya **no se cierra** (solo sale por **[Q]**). Antes, un error mataba el script y, como se lanza desde `Wizard.bat`, Windows mostraba *"¿Desea terminar el trabajo por lotes (S/N)?"* — eso ya no pasa en uso normal. → Si una operación falla, el texto del error **queda visible en pantalla** (antes se lo tragaba): que el usuario lo **copie** y siga desde el menú.

**Selector de rutas/grabaciones (`Select-Route`):** **oculta los `*_hdr.json`** (la mitad "cabecera" del par de carga rápida `_hdr.json`+`_wp.csv`, que aparecían como ítems de 0 wps y confundían). La lista muestra **únicamente las rutas `.json` reales**.

**Recordatorio de "server B" condicional (portabilidad):** antes `route_split.ps1` imprimía hardcodeado "Sincronizar el par al server B también" — irrelevante para un modder con un solo server. Ahora `route_split` **ya no lo menciona**; el recordatorio aparece SOLO si el usuario tiene un segundo server configurado (`ServerBMirror` en `wizard_config.json`), y muestra el **PATH REAL**. La mayoría (un solo server) no ve nada de "server B" — es parte de que el wizard es folder-agnostic/portable.

**Dónde viven los datos (CRÍTICO, costó 1h encontrarlo):**
- Grabación humana (`path_*.csv`, NUMPAD 5) → **CLIENTE**: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- Corrida del NPC (`ai_run_*.csv` y `boris_native_*.csv`, **opt-in con los checks del Reproductor**, ya **no** con tecla) → el **SERVER que corrió** (A: `C:\DayZServer\profiles\BZ_AutoDrive_PathLogger\`; B: `Y:\profiles\...`).
- Rutas (JSON) → `C:\DayZServer\profiles\BZ_AutoDrive\` (`BZBusRoute*.json` + `_hdr.json` + `_wp.csv`).
- `header_*.txt` (junto a cada path_*) = fingerprint del vehículo.

**Formatos de archivo:**
- `path_*.csv` (30 cols): time_s, x, y, z, heading_deg, speed_kmh, is_stop, gear, throttle, brake, steering, rpm, redline_rpm, mode, vx, vy, vz, clutch, handbrake (+ más, incl. **horn** y **lights** del humano para el replay de Fase 2).
- `header_*.txt`: `vehicleClass=`, `mass=`, `engineRPMIdle/Max/Redline=`, `gearsCount=`, `wheelCount=`, `attachmentsCount=`, `attachments=` (CSV), `maxSteeringAngle=`, `wheelbase=`. R_min = wheelbase / tan(maxSteer).
- `ai_run_*.csv`: time_s, x/y/z, heading, speed_kmh, gear, throttle, brake, steering, mode, dist_to_next_stop, next_stop_idx, **wp_idx**, **lateral_dev_m**, corridor_offset/valid, target_speed, target_throttle/brake, i_speed/throttle/brake, rpm, redline_rpm, wp_mode, is_marker.
- **El framework PREFIERE el par `_hdr.json`+`_wp.csv`** (FGets rápido, ~86 ms) sobre el JSON monolítico (parse lento ~150 s). **GOTCHA (histórico):** el viejo `csv_to_route.ps1` solo reescribía el `.json` → había que correr `route_split.ps1` o el server cargaba el par VIEJO (spawnea el vehículo anterior). El conversor actual (`frame_to_route.py`) **escribe los tres archivos de una** → ya no aplica.
- **El par fast-load lleva luces/bocina:** `route_split.ps1` escribe `targetLights` (col 16) y `targetHorn` (col 17) en cada fila del `_wp.csv`; `LoadWaypointsCSV` (en `BZBusService.c`) los parsea (`parts[16]`/`parts[17]`). Así el replay de luces/bocina funciona también por la vía de carga rápida, no solo por el JSON monolítico.

**El conversor NO linterea ni puntúa.** `frame_to_route.py` es directo: lee el header, generaliza la línea y escribe el trío — **no** te pregunta *"¿capeo esta curva?"* ni da BZ-Score. Si la toma quedó sucia (huecos por lag/teleport, gear en lugging, cordón), la respuesta es **re-grabar**, no un linter. *(El linter interactivo + BZ-Score del wizard viejo se sacaron: la fidelidad sale de grabar bien + leer el fingerprint.)*

**Guard "Recording CORRUPTO" (csv_to_route):** algunas grabaciones VIEJAS traen, en vez de números, los especificadores de formato literales (`.2f`, `.1f`) en TODAS las filas — bug de una versión vieja del PathLogger (o tomas editadas a mano). Antes crasheaba el convert con un error críptico ("no se puede convertir '.2f' a Single"). Ahora `csv_to_route` lo **detecta** (chequea que la columna `x` de la 1ra fila parsee a número) y **aborta con mensaje claro**: *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"*. NO es un bug del wizard, es la grabación → elegí una toma **más nueva** (las de ARRIBA de la lista, ordenada por fecha desc, que tengan columnas `throttle/brake/steering`).

**`frame_to_route.py` (el conversor real):** toma el `header_*.txt` → VehicleClass + Fingerprint + Attachments automáticos. **Auto-detecta reverse por `gear==0`**. Deriva `targetHeading` por geometría (`atan2(dx,dz)`). **Auto-deriva los cortes de tramo / intercambios** (`legBreak`) del cambio de gear forward↔reverse a ~0 km/h (un 0 sin cambio de sentido = pausa, no intercambio; el editor también los marca). Escribe el **trío completo** (`.json` + `_hdr.json` + `_wp.csv`) en `<profiles>\BZ_AutoDrive\` con backup `.bak` — **no** hace falta un `route_split` aparte. **No** acepta modos: el config de manejo sale del template único (`driving_config_template.json`). **Ojo:** `gear==0`=reverse asume la convención del carpack; verificar por vehículo (puede ser neutral). Guard de **corrupto**: si la columna `x` de la 1ra fila no parsea a número (grabaciones viejas con `.2f`/`.1f` literales) aborta con *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"*.

> ⚠️ **Los paths de arriba son el setup de Sonom4n (EJEMPLOS).** Cada server/instalación es distinta — `C:\DayZServer\`, `Y:\`, `%LOCALAPPDATA%\DayZ\...` valen para él, no para el usuario que te lee. Antes de guiar a alguien con ubicaciones de archivos, **PEDISELAS** (ver Walkthrough §7.G, Paso 0).

### 7.F — Problemas comunes del wizard (troubleshooting)

Mapeá el síntoma directo al fix. (Para errores de runtime/compile/deploy, ver §12.A.)

| Síntoma | Causa | Qué hacer |
|---|---|---|
| **"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"** al convertir | grabación VIEJA con `.2f`/`.1f` literales en vez de números (bug de un PathLogger viejo, o toma editada a mano) | NO es un bug del wizard, es la grabación. Elegí una toma **más nueva** (las de ARRIBA de la lista, ordenada por fecha desc, con columnas `throttle/brake/steering`). Si solo tenés tomas viejas, **regrabá**. |
| Una operación del menú **falla con un error en rojo** pero el wizard **sigue abierto** (vuelve al menú) | es el comportamiento nuevo: cada opción corre en su `try/catch` | pedile al usuario que **copie el texto del error** (ahora queda visible en pantalla) y que **siga desde el menú**. El wizard solo se cierra con **[Q]**. |
| (Versiones viejas) el wizard se **cerraba** y Windows preguntaba *"¿Desea terminar el trabajo por lotes (S/N)?"* | un error mataba el script lanzado desde `Wizard.bat` | ya **no pasa** en uso normal (try/catch por operación). Si lo ves, es un caso no contemplado → reportá el error. |
| En el selector de rutas/grabaciones aparecía un ítem **`*_hdr` de 0 waypoints** | el `_hdr.json` del par de carga rápida se listaba como ruta | ya **no se muestra**: `Select-Route` oculta los `*_hdr.json`. La lista solo trae rutas `.json` reales. |
| Aparece un recordatorio de **"sincronizar al server B"** (path real) | el usuario tiene `ServerBMirror` configurado en `wizard_config.json` | esperado: copiá el **par** (`_hdr.json`+`_wp.csv`, no solo el `.json`) al path que indica. Con un solo server, este aviso **no aparece**. |

### 7.G — Walkthrough guiado (para acompañar a un usuario en vivo)

*Para que una IA (vos) guíe a un admin que **NO programa** por el core loop. Regla: UN paso a la vez, confirmá antes de avanzar, traducí todo a acciones concretas (qué tecla, qué archivo, qué mirar) y verificá en disco.*

**0 · PEDÍ LOS PATHS DEL USUARIO PRIMERO — no asumas los de este doc.** Cada server/instalación cambia las rutas. Preguntá y anotá antes de empezar:
- **Carpeta del server** (donde viven el `@BZ_AutoDrive` y `profiles\`): Sonom4n usa `C:\DayZServer\`, pero puede ser `G:\MiServer\`, un VPS, etc. → **preguntá**.
- **Carpeta de grabaciones del cliente** (PathLogger): suele ser `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` en la PC donde juega → confirmá el `%LOCALAPPDATA%` real.
- **Dónde están las `tools\`** (el `route_wizard.ps1`).
- ¿Tiene **2 servers** (A local + B remoto)? Preguntá ambos paths; las rutas se sincronizan a mano (no hay sync automático).

Recién con esos paths, guialo con las ubicaciones REALES de SUS archivos en cada paso. *(El wizard, además, pide sus paths en la 1ra corrida y los guarda — es folder-agnostic.)*

**1 · GRABAR** (in-game, como admin):
- Teclas en `Opciones → Controles → "BZ AutoDrive"`. **Solo 3** (y solo el **admin** las ve): **Panel** (INICIO), **Grabar/Detener** (NUMPAD 5), **Marca de evento/parada** (NUMPAD 4). El **cambio de sentido/intercambio ya NO es tecla** (se sacó NUMPAD 3, 2026-08-11): se **auto-detecta** del cambio de gear forward↔reverse. El `ai_run`/`boris_native` **tampoco tienen tecla** — se arman desde los **checks del Reproductor** (Paso 3).
- Subí a un vehículo → **NUMPAD 5** (start) → manejá la ruta entera → en cada parada frená del todo y **NUMPAD 4** (marca) → si hay **cambio de sentido**, detenido **cambiás de marcha y seguís** (no tocás ninguna tecla: el intercambio se auto-detecta del gear, §9.4 manual) → **NUMPAD 5** otra vez (stop). La **reversa tampoco se marca**: se auto-detecta del gear.
- ✅ Verificá: aparecieron `path_*.csv` + `header_*.txt` en **SU** carpeta de grabaciones. **El `header_*.txt` NO debe pesar 0 KB** (si pesa 0 → el fingerprint no se capturó → regrabá). **Que anote con qué vehículo grabó.**

**2 · CONVERTIR** (en la PC, fuera del juego — **todo dentro del wizard**, sin editar JSON ni correr .py/.ps1 sueltas):
- **Abrir el wizard**: doble clic en **`tools\Wizard.bat`** (no hace falta abrir PowerShell). 1ra corrida: pide sus paths (server/cliente) y los guarda (`wizard_config.json`); después cambialos con **[6] Configurar paths**.
- Elegir **[1] Convertir** → elegís la **toma** de una lista ordenada por **fecha (la más nueva arriba)** — el nombre `frame_<stamp>.csv` es un sello del reloj **DEL JUEGO** (no de la PC). Si ves "Recording CORRUPTO", elegí una **más nueva** (§7.F). Después te pide **solo un NOMBRE** (Enter = ruta activa). Con eso corre `frame_to_route.py` (lee el header, generaliza, auto-detecta reversa, arma el trío `_hdr.json`+`_wp.csv` de una) y la deja **DESPLEGADA**: "lista en el Reproductor (hot-load)".
- **No hay modo que elegir ni linters que revisar** — es un conversor puro. Si la toma quedó sucia, la respuesta es **re-grabar** (Paso 1), no afinar. *(Un modder avanzado que quiera tocar el control —`FollowPath`/`UseInverseModel`, etc.— lo hace a mano en el `_hdr.json`; ver §5 + D.2. El 99% no lo necesita.)*
- ✅ Output: `BZBusRoute_<nombre>.json` + `_hdr.json` + `_wp.csv` en **su** `profiles\BZ_AutoDrive\`, ya desplegados.

**3 · PLAY** (in-game) — la ruta ya quedó desplegada al convertir, así que **basta abrirla en el Reproductor**:
- Si quiere diagnosticar: **tildá el check `ai_run` (y/o `boris_native`) en el Reproductor ANTES de dar play** (sino NO se escribe nada; ya no hay tecla).
- **INICIO** abre el panel → en el **Reproductor** elige la ruta (aparece con el NOMBRE que pusiste) → **LOAD&SPAWN** (o **Spawn/Restart Bus**). Boris la maneja; mirá el panel (wp, km/h, modo). El `ai_run_*.csv` queda en el **server que corrió**.

**Fallas comunes → qué preguntar/hacer:** *(para fallas del wizard al convertir/seleccionar/restaurar, ver §7.F)*
- *"Recording CORRUPTO" al convertir* → toma vieja con `.2f`/`.1f` literales; elegí una más nueva o regrabá (§7.F).
- *No aparece la ruta en el Reproductor* → ¿el `.json` está en `profiles\BZ_AutoDrive\` y se llama `BZBusRoute*`?
- *Spawnea el vehículo anterior* → par `_hdr`/`_wp` VIEJO; reconvertir con el wizard (hace el split).
- *No se escribe `ai_run`* → el check `ai_run` estaba **destildado** → tildalo en el Reproductor ANTES de dar play (ya no es una tecla).
- *Boris clavado en 1ra / no acelera* → es el `ShiftTo(FIRST)` de eAI; el override lo maneja. Si es un mod de vehículo, confirmá que extienda `CarScript`.
- *Build/deploy* → NUNCA buildear/mirror con el server O el cliente abiertos (lockean el PBO).

### 7.H — Informes de diagnóstico (tooling PS, `report_export.ps1`)

> **Nota (RETIRADO de la publicación):** el generador de informes (`report_export.ps1`) se **movió a `..\BZ_AutoDrive_devtools\` y NO se publica** con el mod (nunca estuvo en el menú del wizard, que es un conversor puro). Lo de abajo queda como **referencia histórica** de qué medía. Para diagnosticar una corrida hoy, leé el `ai_run` directo (D.10).

**Qué es:** una herramienta PowerShell (NO toca el PBO; corre en la PC del admin) que genera **informes multi-página self-contained** en **PDF y HTML** a partir de una toma humana y/o un `ai_run`. **Cero instalación** de imagen/Python: usa **Edge headless** (`msedge --headless --print-to-pdf`) + **SVG inline** — nada de ImageMagick/Pillow. Cierra el lazo de **MEDICIÓN** del framework: convierte los números crudos del `ai_run`/toma en algo que el admin lee de un vistazo.

**Entrada:** `New-RouteReport -ReportType Human|Boris|Comparative|Auto` (standalone; ya no hay submenú en el wizard).

**TRES tipos de informe** (`-ReportType Human|Boris|Comparative|Auto`):
- **① Toma humana** (`Human`) — **post-grabación, sin Boris**. Análisis de la demostración: curve advisory, pendiente, gear, **BZ-Score predicho**, markers + la **traza 🔵 sola** de la toma. Sirve para juzgar la grabación ANTES de correr a Boris.
- **② Toma de Boris** (`Boris`) — **post-ai_run, sin comparar**. La **traza 🟠 sola** de Boris + su **lectura medida** (completion, lat-dev, velocidad, saturaciones, hotspots de la corrida). Para auditar a Boris sin la toma humana a mano.
- **③ Comparativo** (`Comparative`, default) — **toma humana + su ai_run**. Traza **🔵 humana vs 🟠 Boris** + **puntos calientes** (top-N desvíos: círculos rojos sobre el mapa + **hojas de zona** con zoom y gráfico por cada hotspot) + **lectura medida de Boris** (completion, lat-dev, velocidad, saturaciones).
- `Auto` resuelve el tipo según los insumos: solo toma → `Human`; solo ai_run → `Boris`; ambos → `Comparative`.

**Detalles operativos:** idioma vía `BZ_LANG` (ES/EN). Output a la carpeta **`BZ_AutoDrive_Informes`** (hermana de `routes`). Los ejemplos quedan en `profiles\BZ_AutoDrive_Informes\` (p.ej. `EXAMPLE02.pdf` = comparativo, `EXAMPLE02_toma.pdf` = toma humana).

**Gotchas de PowerShell al armar este tooling** (van a §12 también, pero quedan acá por contexto): un `.ps1` con **acentos** necesita **UTF-8 CON BOM** (PS5.1 lee un .ps1 sin BOM como ANSI → mojibake en los textos del informe); pasar `@($obj)` a un `NoteProperty` **rompe** (usar `.ToArray()`); el JSON de datos intermedio se escribe **sin BOM** (lo lee el HTML/Edge, no PS).

---

## 8. El motor de eventos (DSL de verbos/triggers)

Marcador NUMPAD 4 (graba `isStop=true` en el wp) → nodo de evento. `Events[]` de `{wp, trigger, actions[]}`.
- **Triggers** (`BZTrigger.type`): `wp_reached` (wp), `player_in_radius` (radius), `player_enter_vehicle`, `vehicle_health_below` (threshold 0..1), `timer` (seconds).
- **Verbos** (`BZAction.verb`, dispatcher `else-if` en `BZBusService.ExecuteAction(Car car, BZAction action, int evIdx)`) — **22**: `add_cargo`, `log_event`, `freeze_vehicle`/`unfreeze_vehicle`, `set_vehicle_mortality`, `set_driver_mortality`, `start_engine`/`stop_engine`, `despawn_vehicle`, `stop_route`/`resume_route`, `set_var`, `play_sound`, `lights_on`/`lights_off`, `horn`, `repair_vehicle`, `refuel`/`drain_fuel`, `ui_broadcast`, `spawn_guard`/`dismount_guard`. (`check_once` SACADO: rompía el JSON load; el branching es de Quest.) *Reservados sin handler todavía (extensión — el campo `slot` ya existe en `BZAction`): `lock_seat`/`unlock_seat`/`eject_passenger`.*
- **Campos de BZAction:** verb, items, msg, slot, value, fvalue, var, faction, loadout, count, delay (coreografía vía CallLater).
- **Para configurar:** ver §10/§12 del manual + Apéndice (ejemplos JSON de las 2 escenas).
- **Para programar un verbo nuevo:** agregar una rama `else if (verb=="x") {...}` en ExecuteAction (tenés `car`, `action`, `m_WaypointIndex`); el delay lo maneja el dispatcher. Campos nuevos → agregalos a `BZAction` (es plana, no rompe el parser).

**Audio (`play_sound`):** SoundSet 3D pegado al vehículo (SEffectManager). Declarar `.ogg` en CfgSoundShaders+CfgSoundSets. **Gotcha:** sonido lanzado server-side puede no llegar al cliente → reenviar por RPC (la infra de toast `RECEIVE_TOAST` ya existe; ver `BroadcastGlobal` que manda RPC a todos los clientes → `NotificationSystem.AddNotificationExtended`).

---

## 9. Integración Quest (DayZ-Expansion-Quests)

**Reparto:** Quest = bots vivos + lógica de misión (reward, progresión); eAI = cuerpo del bot; BZ_AutoDrive = vehículo + manejo + coordinar subir/bajar. Un bot spawneado standalone por el framework = maniquí sin lógica → los bots VIENEN del Quest.

**Hook (`BZQuestHook.c`):** `modded MissionServer { override Expansion_OnQuestStart(quest) { super; BZBusService.GetInstance().OnQuestStart(quest); } }`. (La subclase `ExpansionQuestObjective*Event` NO compila en scope 5_Mission → por eso el hook va en MissionServer.)

**Poll (`OnQuestStart`/`CheckQuestBots`):** guardar `m_QuestCheckID = qc.GetID()`; CallLater poll de `ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(id, patrols)` (~cada 2–4 s). `patrol.m_Group.Count()` = bots (son lazy por proximidad; el conteo inicial es el PICO, materializan de a poco 3→5). Conteo baja → mataron uno → trigger.

**Boarding (`BoardQuestBots`) + el gotcha de la pacificación:** un bot EN COMBATE no camina a un waypoint (el FSM eAI exige "sin amenaza"). Hay que: `b.eAI_SetPassive(true)` + `b.eAI_SetThreatDistanceLimit(0.0)` + drenar targets (`GetTarget(0)`/`eAI_RemoveTarget` en loop). Recién entonces camina a `transport.CrewEntryWS(seat, door, ddir)` y sube animado (sino sube por teleport, feo). Seat 1+ (0=chofer). NO re-agrupar (preserva el kill-count del quest). `SetThreat` NO existe en eAIGroup.

**Escenas validadas:** `flee_on_kill` (pacificar→subir→manejar→bajar al patio→OnQuestComplete→despawn) y `ambush_on_damage` (bots instant a bordo + Boris armado → cualquier daño [CarScript EEHitBy o poll de salud] → NotifyConvoyDamaged → freeze + dismount-todos hostiles). Insight: el ambush ES un marcador NUMPAD 4 generalizado (trigger on_damage → secuencia); refactor natural = receta del DSL de Events[].

**Objective types:** validado con AICamp/AIPatrol. Los otros (AIEscort, AIVIP, Travel, Delivery, Target, Action, Collection) la arquitectura los soporta pero falta su hook — campo abierto (ver tabla en §17 del manual).

**API eAI relevante:** no hay command bus imperativo; todo es GRUPO + waypoints + FSM reactiva. Locomoción: `FormationState=IN` (NONE=halt era el bug), `AddWaypoint` + `ForceRecalculate`, `OverrideMovementSpeed/Direction`. eAIBase ES PlayerBase. Ver memorias `reference_eai_*`.

---

## 10. UI

**Control Panel / Reproductor** (se abre con la acción `UABZAutoDrivePanel`, default `INICIO`/`kHome`): tablero en vivo de cada runner (wp, velocidad, modo) + control (parar/pausar/teleport). El viejo `BZControlPanelUI.c` quedó **desconectado** — la acción ahora abre el Reproductor (`BZReproductorUI.c`); el `CTRL+HOME` se eliminó.
**Reproductor** (`BZReproductorUI.c`, MENU_BZ_REPRODUCTOR id 51213): lista rutas (`EnumerateRoutes` = `BZBusRoute*.json` excluyendo `_hdr`/tmp), LOAD&SPAWN (`RespawnFromPath`) sin restart. Pretty() strip de `BZBusRoute_`+`.json`.
**Layouts:** formato **CPP-style** (`PanelWidgetClass {...}`), NO XML/library (crashea CreateWidgets). Ref: BrigadaZRadio/gui/layouts. Texturas .paa (pipeline PNG→PAA con ImageToPAA); patrón "imagen bakeada + overlay".
**RPC (`BZBusRPC.c`):** enum con `RECEIVE_TOAST` (notificación a clientes), slots, stop/pause, telemetría. `BroadcastGlobal(msg)` itera players y manda `ScriptRPC` con `BZBusRPC.RECEIVE_TOAST` → cada cliente hace `NotificationSystem.AddNotificationExtended(6, "BZ_AutoDrive", msg, "")`.
**Pendiente UI:** rebind de teclas desde la propia UI del mod (hoy se hace desde el menú de Controles del juego, ver D.1); lista dinámica vs slots fijos.

---

## 11. Programar / extender — patrones de código

**Lenguaje:** Enforce (scope 3_Game→4_World→5_Mission; helpers compartidos al scope más temprano). Build: AddonBuilder NO valida Enforce (solo el runtime/RPT).

**(a) Verbo nuevo** → rama en `BZBusService.ExecuteAction`:
```c
} else if (verb == "honk") {
    // car, action (value/fvalue/msg/slot/...), m_WaypointIndex disponibles
    BZBusLog.Info("[EVENT " + evIdx + "] honk @ wp " + m_WaypointIndex);
}
```
**(b) Trigger nuevo** → campo en `BZTrigger` + `case` en el evaluador.
**(c) Control hook** → ya es `modded CarScript.OnInput` override-last (§4). Para tocar el control, editás `BZBusService` (el Tick/OnInput inyecta los inputs).
**(d) Quest hook** → `modded MissionServer.Expansion_OnQuestStart` (§9).
**(e) Como dependencia (otro mod)** → `requiredAddons[] = {"BZ_AutoDrive"}` + `BZBusService.GetInstance().RespawnFromPath("BZBusRoute_X.json")` / `RespawnAs(clase)`.

**Car API (escribir):** `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. **(leer):** `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (→wheelbase), `WheelGetSurface(i)`, `EngineGetRPMRedline()`, `GetGearCount()` (NO `GetGearsCount`, obsoleto). Config: `GetGame().ConfigGetFloat/Array("CfgVehicles/<cls>/...")`.

**CfgVehicles vs script classes:** `SurvivorM_*` es config, no script; en `.c` heredar de SurvivorBase/ManBase/ItemBase/House, nunca de la entry de CfgVehicles. Clases engine (CGame) NO se moddean.

---

## 12. Gotchas y errores + fixes (CRÍTICO — no repetir)

**Enforce:**
- No ternario `?:`; no `if` multilínea con `&&` al inicio de línea → "Syntax error". Una línea o bools intermedios.
- No `Math.PI` ni `Math.AbsFloat`.
- `new Clase(args)` revienta → crear vacío + setear campos.
- "Formula too complex" a ~9 operandos con `+` → dividir con `+=`.
- Scope de ramas hermanas (if/else, case) CHOCAN (no como C++) → renombrar/hoistear.
- Aritmética inline en concat de string (`"x"+(seat-1)`) rompe → hoistear a int.
- Ternario/aritmética dentro de concat también rompe.
- `PlayerBase.Cast(driver)` devuelve true para eAI → discriminar jugador real por `GetIdentity()`.
- Producto cruz left-handed: `cross = AB.z·AP.x − AB.x·AP.z` (invertido = divergencia).
- **Método de widget inexistente** (ej. `MultilineTextWidget.SetLineColor`): **compila** pero al cargar tira *"Undefined function"* y **mata el módulo Mission** → la UI no abre. `MultilineTextWidget` no tiene color por-línea: paginar con `TextWidget`s individuales (`TextWidget.SetColor` **sí** existe). Regla: verificá que el método exista en la API antes de usarlo (el `enforce_lint` ahora lo chequea).
- AddonBuilder no detecta nada de esto; confirmar en el RPT al cargar el server.

**Pipeline/datos:**
- **(histórico) el viejo `csv_to_route` dejaba el par `_hdr/_wp` VIEJO** → `frame_to_route.py` ya escribe los tres. Si spawnea el vehículo anterior: reconvertí con el wizard + sincronizá el PAR a B.
- **Attachments:** nunca adivinar; del fingerprint/header. Al cambiar VehicleClass, reemplazar (el converter preserva los viejos).
- **LoadConfig en runtime bloquea el main thread** (JSON 2.6MB = 108s = client disconnect) → cargar solo en Init(); el par fast-load (FGets) es para reload en caliente.
- **PowerShell Set-Content/Out-File -Encoding UTF8 agrega BOM** y rompe el parser de Expansion/DayZ → usar `[IO.File]::WriteAllText` con `UTF8Encoding($false)`. Para edits que preservan acentos/§: leer/escribir Latin1 (codepage 28591) o usar la herramienta de edición.
- **Tooling de informes (`report_export.ps1`) — sutilezas de encoding PS5.1 (al revés que lo de arriba):** un `.ps1` con **acentos en su propio código/strings** necesita **UTF-8 CON BOM** (PS5.1 lee un .ps1 SIN BOM como ANSI → mojibake en los textos del informe). NO confundir con los archivos que el mod consume (esos van SIN BOM). El JSON de datos intermedio que arma el script se escribe **sin BOM** (lo lee el HTML/Edge). Además: pasar `@($obj)` a un `NoteProperty` **rompe** → usar `.ToArray()`.

**Build/deploy:**
- Al mover/renombrar/borrar un `.c`, limpiá la carpeta `temp/` de AddonBuilder antes de reempaquetar (sino empaqueta versiones viejas; "zombies en temp"). Si tu pipeline copia el PBO a un server local, cerrá server y cliente antes (mientras están abiertos lockean el PBO → falla la copia).
- `-DeployClient` deja un `@<viejo>` stray en `!Workshop` al renombrar → borrarlo.
- meta.cpp en `MODS\` no lo toca el replace del source → migrar `name` a mano.

**Cliente/launcher:**
- **DZSALauncher cachea sus mods EN MEMORIA**: editar `%LOCALAPPDATA%\DayZ Launcher\Local.json` (knownLocalMods/userDirectories) NO toma efecto hasta SALIR DEL TODO (bandeja→Exit; cerrar la ventana no alcanza). Síntoma: "El cliente tiene un PBO que no está en el servidor". El `meta.cpp` define el `name` que muestra; dos mods locales con el mismo name se confunden.
- Cliente carga de `C:\DayZServer`, sin !Workshop, sin suscripción (deliberado para test local).

**Estado VISUAL en un AI car observado (gotcha de RED — ORO):**
- **`LightOn/LightOff/LightIsOn/LightToggle` son proto native de `Transport` = flag LOCAL del engine, NO una NetSyncVariable.** El engine solo los setea en el cliente que **SIMULA** el auto (owner / player en el asiento de chofer). Un **observador de un AI car** (Boris maneja, sin player en seat 0) **NUNCA** ve `LightIsOn()==true` → `UpdateLightsClient` nunca crea `m_Headlight` → **no se ve el haz** aunque el server "prenda" las luces.
- **Intentos que NO alcanzaron:** `ToggleHeadlights()` solo; `ToggleHeadlights() + ForceUpdateLightsStart/End` (re-dispara `UpdateLights()` pero `UpdateLightsClient` sigue viendo `LightIsOn()==false`).
- **FIX v3 (en `BZBusCarScript.c`, el `modded CarScript`):** NetSync propia **`m_BZLightsWanted`** (`RegisterNetSyncVariableBool` en el ctor) + método server **`BZSetLights(bool)`** (server: toggle native + `SetSynchDirty()`) + override **`OnVariablesSynchronized()`** que, **en el CLIENTE**, fuerza `LightOn()/LightOff()` + `UpdateLights()` según `m_BZLightsWanted`. Usar `LightOn/Off` y **NO `LightToggle`** (este pasa por `OnBeforeLightOn`, que chequea batería — no synced en el observador).
- **La bocina NO sufre esto:** `SetCarHornState` ya usa `RegisterNetSyncVariableInt` + `SetSynchDirty` en vanilla → sincroniza sola al observador.
- **LECCIÓN GENERAL (aplicá a cualquier feature visual futuro):** cualquier estado **VISUAL** que el server quiera mostrar en un **AI car observado** (luces, animaciones de carrocería, etc.) necesita su **NetSyncVariable propia** + forzarlo **client-side** en `OnVariablesSynchronized`. El engine solo replica el estado al cliente que SIMULA el vehículo; el resto no lo ve si no lo sincronizás vos.

**Otros:**
- Límite ~38 mods (Steam query ~255 bytes); 40 rompe con "Server can't transmit all data".
- Mod nuevo = copiar su `.bikey` a `keys/` (sino cliente "Faltan PBO").
- No sync automático A↔B (copia manual deliberada; sync tools fallaron antes).
- Layouts XML crashean → CPP-style.
- Objetos del .map binario (wrecks) no se borran con ObjectDelete → `GetSuppressedObjectManager().Suppress(obj)` (requiere @DayZ Editor Loader) o el mapping declarativo de Expansion.

### 12.A — Catálogo de errores reales que tuvimos → cómo los resolvimos

Esto es lo que efectivamente nos pasó y nos costó tiempo. Cuando un admin/modder reporte un síntoma parecido, mapeá directo al fix.

**Compile / script (Enforce):**

| Síntoma / mensaje | Causa | Fix |
|---|---|---|
| **"Syntax error"** señalando la línea de la clase + una línea interna | `if` multilínea con `&&` al inicio de línea, ternario `?:`, o aritmética inline en concat de string (`"x"+(seat-1)`) | una sola línea; o hoistear a bool/int intermedio antes |
| **"Formula too complex"** | >~9 operandos encadenados con `+` (típico armando strings de CSV/log) | dividir en varias asignaciones con `+=` |
| Variable "already defined" o valor pisado entre ramas | el scope de ramas hermanas (if/else, case) **choca** (no es como C++) | renombrar, o hoistear la variable arriba del bloque |
| **"Unknown type"** al heredar de `ExpansionQuestObjectiveAICampEvent` en scope 5_Mission | la subclase del objetivo no resuelve en ese scope | NO heredar el objetivo; usar el hook `modded MissionServer Expansion_OnQuestStart` + poll `QuestPatrolExists` |
| Crash al **cargar** el server tras `modded class CGame` (u otra clase engine) | las clases del engine no admiten `modded class` | no moddear engine; el control va por override-last en `CarScript`, los hooks por `MissionServer` |
| El constructor revienta (`new BZAction("verb")`) | Enforce no toma args en constructores de clases simples | crear vacío y setear campos (`a = new BZAction(); a.verb = "x";`) |
| Warning **"GetGearsCount is obsolete"** | API vieja | usar `Car.GetGearCount()` |
| **El PBO compila OK pero el server no arranca / tira errores** | **AddonBuilder NO valida Enforce** (empaqueta igual con errores de sintaxis) | leer el **RPT** al cargar — ahí salen los errores reales con archivo:línea |
| El PBO empaqueta versiones VIEJAS de un `.c` tras renombrar/mover/borrar | AddonBuilder reusa el temp | limpiá la carpeta `temp/` de AddonBuilder antes de reempaquetar |
| `CreateWidgets` crashea al abrir una UI | layout en formato XML/library | reescribir el layout en formato **CPP-style** (`PanelWidgetClass {...}`) |

**Runtime / lógica:**

| Síntoma | Causa | Fix |
|---|---|---|
| Un evento dispara con `type=''` (trigger fantasma) | `JsonFileLoader` instancia un `BZTrigger` default vacío al parsear | contemplar/filtrar el default vacío (ver `feedback_jsonfileloader_self_referential`) |
| El framework trata al jugador real como bot (o al revés) | `PlayerBase.Cast(driver)` devuelve **true también para eAI** | discriminar al jugador real por `GetIdentity()` |
| Stanley corrige para el lado contrario; el bus se auto-dirige al agua en ~80 s | signo del producto cruz invertido (sistema left-handed de DayZ) | `cross = AB.z·AP.x − AB.x·AP.z` |
| `ValidateSpawn` da OK falso o falla el retry | medía distancia desde el spawn, no movimiento | medir `kmh > 0.5` (el vehículo responde) |
| Boris arranca OK y a los ~20 s salta 3 km / aparece bajo el mar | `SmoothPath()` dividía por 0.2 hardcodeado → `window≠5` escalaba todos los wps (bug 7B9D0036) | corregido; además `PathSmoothWindow=0` en rutas con curvas cerradas |
| Boris corta las curvas por adentro | centroide de N wps para el lookahead cae dentro del arco / smoothing alto | interpolar **sobre** el path; `PathSmoothWindow=0` |
| `check_once` rompía el load del JSON | verbo de branching mal soportado | sacado del DSL; el branching condicional es de Quest |
| Bot se queda "maniquí" / sube por teleport en vez de caminar | el FSM eAI no navega con amenaza activa; o `FormationState=NONE` (halt) | pacificar (`eAI_SetPassive(true)` + threat 0 + drenar targets); `FormationState=IN` + `AddWaypoint` + `ForceRecalculate` |

**Build / deploy / cliente (los de la sesión de migración):**

| Síntoma | Causa | Fix |
|---|---|---|
| `robocopy` a `C:\DayZServer` exit **8/9** | el server O el cliente abiertos lockean el PBO | cerrar **ambos** antes de buildear/mirror; preguntar "¿abierto o cerrado?" |
| Tras deployar una ruta NUEVA, spawnea el **vehículo anterior** | par `_hdr.json`/`_wp.csv` VIEJO (el framework **prefiere el par**); pasa con flujos viejos — `frame_to_route.py` ya escribe los tres | reconvertir con el wizard (escribe el trío) + sincronizar el PAR a B (no solo el `.json`) |
| Cliente pateado: **"El cliente tiene un PBO que no está en el servidor"** | el DZSALauncher cachea sus mods EN MEMORIA + quedaban copias viejas (`Local.json`, `!Workshop\@viejo`) | **SALIR DEL TODO** del launcher (bandeja → Exit) tras editar `Local.json`; borrar las copias viejas del mod |
| "Faltan PBO" al conectar | falta la `.bikey` del mod en `keys/` | copiar la `.bikey` a `keys/` (A y B) |
| El launcher seguía cargando el mod viejo aunque edité el archivo | el `meta.cpp` tenía el `name` viejo (dos mods locales con mismo `name` se confunden) + launcher no relee hasta reiniciar | migrar el `name` del `meta.cpp`; reiniciar el launcher del todo |
| Boot del server ~3 min | JSON de ruta enorme parseado en `Init()` | partir la ruta / usar el par fast-load (FGets); lazy-load (futuro) |
| 40 mods → "Server can't transmit all data" | techo ~38 mods (Steam query ~255 bytes) | sacar no esenciales o consolidar PBOs chicos |

---

## 13. Build & deploy

**Para usar el mod:** suscribite en el Workshop (ya viene firmado). **Para compilar un fork:** empaquetá `BZ_AutoDrive.pbo` (prefix `BZ_AutoDrive`) con DayZ Tools (AddonBuilder) o tu propio pipeline y firmalo con tu **propia** llave; copiá tu `.bikey` a la carpeta `keys/` del server y listá `@BZ_AutoDrive` en el `-mod=` del start.bat. No se distribuye ningún script de build — cada modder empaqueta a su manera. **Gotcha universal:** al renombrar/mover/borrar un `.c`, limpiá la carpeta `temp/` de AddonBuilder antes de reempaquetar (sino empaqueta versiones "zombie" viejas). Pre-Workshop: swap-and-test con `publishedid=0` + RPT limpio + smoke test antes de subir.

---

## 14. Metodología de investigación (cómo seguir)

**El lazo (ILC manual):** grabar 3 corridas IA (check `ai_run` en el Reproductor) → medir desviación lateral vs recording humano por wp → identificar clusters sistemáticos (no ruido) → hipotetizar causa técnica → diseñar fix que NO viole los compromisos (no modificar eAI, no romper generalización) → validar con toma nueva.

**Analizar un ai_run:** columnas clave `wp_idx`, `lateral_dev_m`, `speed_kmh` vs `target_speed`, `steering`, `gear`, `mode`. Buscar: saturaciones de steering (|steer|→1), sign changes (zigzag), speed deficit (Boris −10 km/h vs target = cascada off-path→slow→off-path), stuck (full throttle + speed 0 = encajado, sin AR no se despega). **Bias lateral:** usar MEDIANA + segmentar recta/curva (el signed-avg engaña por outliers/saturaciones).

**Heurísticas diagnóstico→fix (knowledge base del wizard):** bias lateral → `CruiseLateralCenterOffset` (~25m/unidad); saturación de steering → bajar `SteeringScale`; lugging → `GearStrategy=follow_recording`; zigzag → `CruiseLateralDamp`/`TargetSpeedSmoothWindow`; corta curvas → `PathSmoothWindow=0`; off-path lento → AutoRecovery + smart throttle cap; volantazos pre-curva → brake binario (0/1 teclado) → cascada (brake rate limiter pendiente).

**Workflow estable:** grabar → stop server → deploy JSON → start server → test. NO iterar con NUMPAD 2 sobre server vivo si cambiaste código. Para solo-JSON: record→deploy→NUMPAD 2 sin restart sirve. SIEMPRE tildar el check **`ai_run`** (opt-in, en el Reproductor) en tests, sino no hay ai_run.

**Principios rectores:** (1) config como manual; (2) spatial > temporal fidelity (llegar en orden, no a tiempo); (3) framework = extractor del óptimo (diff Boris-vs-humano = info, no bug); (4) camino > inputs (la trayectoria es sagrada, los inputs son ruidosos); (5) recording = manual del vehículo (cross-vehículo invalida); (6) dual audience (wizard + override).

---

## 15. La frontera abierta (qué investigar próximo)

- **Endpoint (parada final) sub-0.5 m — RESUELTO (2026-08-11, ya no es frontera):** el freno de parada autoadaptativo por vehículo+superficie clava la parada **por debajo de 0.5 m en la mayoría** (hasta ~1 m en los grandes / wheelbase largo), forward y reversa, validado out-of-sample (§2). Incluye el endpoint-tras-curva, histórico punto débil (SEQ1 a 44 km/h → 0.34 m). Lo único que queda abierto acá es la **reversa en curva cerrada** (siguiente bullet).
- **Reverse en curva cerrada:** cerrar el ~1 m de divergencia. Opciones: leer `WheelGetContactPosition` del eje trasero real (no wheelbase/2 aprox del tándem), modelar pendiente 3D, más autoridad de corrección fina. O aplicar el control **reference-assisted a la reversa** (steering por path+config en vez de replay; el UAZ corto+2-ejes es el mejor caso de prueba). La transición forward→reverse (frenar-a-0 → palanca R → controlador) es donde Boris se traba.
- **Manejo extremo (v3):** drifts/contravolante. Override de física propio (saltear el receptor eAI en esos tramos) o residual de ML. El límite honesto actual.
- **Wizard 100% autónomo:** internalizar el §14 (auto-análisis post-run + auto-apply de heurísticas).
- **Multi-vehículo:** convoy con spacing por rastro de migas (estilo ARMA setConvoySeparation).
- **NPC con LLM:** el framework YA es LLM-shaped — el DSL de eventos = espacio de acciones, el grafo = API de navegación, config-read = percepción del vehículo. El LLM gobierna (lento, alto nivel) y las skills reactivas ejecutan (rápido). Diseño provider-agnostic. Visión más amplia: un **sobreviviente autónomo** (Voyager-for-DayZ) donde manejar es UNA skill.
- **Trenes:** ~70% cubierto sin cambios (rieles = red, sin volante, solo throttle+brake).
- **Otros motores:** el principio (demostración + config-read + control clásico) es portable.

---

## 16. Glosario y referencias clave

- **eAI** = DayZ-Expansion-AI (la IA base). **Boris** = el NPC chofer (eAI_SurvivorM_Boris). **Override-last** = correr OnInput después de eAI. **Fingerprint** = datos del vehículo capturados al grabar (header_*.txt). **R_min** = radio de giro mínimo = wheelbase/tan(maxSteer). **Corredor/paredón** = banda muerta del control lateral. **BZ-Score** = puntaje 0–100 (tooling de **informes standalone**; el wizard conversor ya no lo calcula). **Runner** = una instancia de vehículo en ejecución. **Triple match** = A=B=cliente mismo PBO.
- **Classnames demo:** vehículos del CarPack (viper_yellow, x5mcompetition_orange, Star_APC_Cobra_white, Star_Golf_MK1, etc.), UAZ_452 (mod @[CnG]UAZ_452), vanilla (Hatchback_02, M3S/V3S), **CivilianSedan_Wine** (EXAMPLE02: 3er vehículo de ejemplo, replay con luces+bocina humano en el mismo vehículo — 98.6 % completion, lat-dev 0.79 m).
- **Regla del corte (§2/§5), conceptual:** el direct-replay de una maniobra reproduce ángulos de volante VEHICLE-SPECIFIC (mismo ángulo → radio ∝ wheelbase), así que **generaliza a otro vehículo solo si el corte al bloque de maniobra cae donde la trayectoria es RECTA** (la curva queda en closed-loop Stanley, vehicle-agnostic; la recta va en replay). Cortar en curva → open-loop → otro wheelbase deriva → no generaliza. *(El mod NO incluye tomas de ejemplo: una ruta grabada son coordenadas de un mapa puntual.)*
- **Docs hermanas:** el **manual** (`MANUAL_BZ_AutoDrive.md`, didáctico para admin/modder). Las **memorias** del proyecto (`%USERPROFILE%\.claude\projects\c--DayZServer\memory\`) tienen el historial fino, milestones y feedbacks — `project_MASTER_CONTINUITY.md` es la guía de cold-start.

---

## Apéndice C — Listados de código fieles (por componente)

> Excerpts reales del source (nombres de método/campo exactos; cuerpos largos recortados con `// ...`). Para el detalle total, greppear el archivo citado.

### C.1 `BZBusConfig.c` — clases del DSL

```c
class BZBusRouteConfig {
    int    RespawnDelay = 300;        float AverageSpeedMS = 11.0;   float SpawnHoldSeconds = 3.0;
    string VehicleClass = "ExpansionBus";   string DriverClass = "eAI_SurvivorM_Boris";
    bool   VehicleInvincible = true;  string ConvoyMode = "";        int   MaxGear = 6;
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
    string LightsMode = "replay";   // replay / auto / auto_inverted / on / off
    string HornMode   = "replay";   // replay / stops / finish / off
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

### C.2 `BZBusCarScript.c` — el override-last

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

### C.2b `BZBusCarScript.c` — luces sincronizadas en un AI car observado (gotcha de red, §12)

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

### C.3 `BZBusService.c` — inyección de control

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

### C.4 `BZBusService.c` — `ExecuteAction` (dispatcher real, 22 verbos)

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
    else                                  { BZBusLog.Warn("verbo no implementado: '" + verb + "'"); }
}
```
*(El `delay` lo agenda el llamador con CallLater; este dispatcher ejecuta la acción ya destemporizada.)*

### C.5 `BZBusService.c` — integración Quest

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

### C.5b `BZBusService.c` / `BZBusCarScript.c` — emboscada y limpieza del convoy

Complemento de C.5. La escena `ambush_on_damage` usa boarding **instantáneo** (no walk-in), **dos triggers** de daño, dismount hostil, y un hook de finalización que despawnea el vehículo.

**Boarding instantáneo + armado de Boris (`BoardAmbushBots`):**
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

**Triggers de daño (`NotifyConvoyDamaged` + `EEHitBy` + poll de salud):**
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

**Despliegue hostil (`AmbushDismount`):**
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

**Finalización / despawn (`OnQuestComplete`):**
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

**Notas de implementación:**
- **Dos triggers de daño** (chasis vía `EEHitBy` + salud de pasajeros vía poll de `CheckQuestBots`); cualquiera arma la emboscada una sola vez (`m_AmbushTriggered`).
- `EEHitBy` no conoce su runner: lo resuelve con `GetRunnerForCar(this)` (patrón **multitón** — el auto pertenece a un `BZBusService`).
- Boris: `BanditLoadout` al embarcar; en el dismount su grupo pasa a facción **Mercenaries** (hostil al jugador, sin friendly-fire con el convoy) y recién ahí `SetAllowDamage(true)`.
- Secuencia: daño → `NotifyConvoyDamaged` (freeze) → `AmbushWaitStop` (espera parada) → `AmbushDismount` (despliegue). El boarding fue instantáneo (`BoardAmbushBots`), a diferencia del walk-in pacificado de la escena `flee_on_kill` (C.5).
- `OnQuestComplete` solo actúa sobre `m_QuestCheckID` (el convoy gestionado), remueve el poll y despawnea sin auto-respawn — evita autos fantasma.

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

### C.7 `BZPathLogService.c` — grabación (50 Hz) + fingerprint

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

### C.8 `BZBusRPC.c` — enum de RPCs (rango 32410+)

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
*(Referencia interna: el enum incluye RPC de **tooling avanzado/retirado** — `REQUEST_AI_LOG_TOGGLE`, `REQUEST_SYSID_STEP`/`REQUEST_SYSID_CURVE`, `REQUEST_RESPAWN_SLOT` corresponden a features de debug/SysID que ya no están en el flujo publicado. `RECEIVE_PANEL_SETTINGS` NUNCA envía la lista de admins — solo la tecla + `esAdmin` del que pregunta. `BORIS_CHAT_*` = el experimento de NPC con LLM.)*

---

*Pendientes de este pack: figuras. **Corrección a propagar:** el PathLogger graba a **50 Hz** (no 10) — actualizar el manual. Mantener sincronizado con el código al evolucionar.*


---

## Apéndice D — Referencia rápida (keybindings, defaults, ai_run, JSON, guías)

Material de referencia para trabajar sin tener que greppear el código. Fuente: lectura directa del source 2026-06-22 — **el código es la verdad; si divergen, gana el código.**

> **Índice de guías AI-actionable:** D.5 (verbo nuevo) · D.6 (vehículo nuevo) · D.7 (mapear una zona) · D.8 (vehículo a un Quest) · **D.10 (diagnosticar una corrida / `ai_run`)**. Para el formato crudo del `ai_run` ver **D.3**; para la metodología de calibración (lazo ILC, heurísticas síntoma→fix) ver **§14**.

### D.1 — Keybindings (sistema de input del menú de Controles)

Migrado al **sistema de input nativo de DayZ** (ya NO `OnKeyPress`). Las acciones se declaran en `data/inputs.xml` (`<modded_inputs>` → `<actions>` con name+loc EN · `<sorting name="BZ_AutoDrive" loc="BZ AutoDrive">` = la pestaña en `Opciones → Controles` · `<preset>` con los defaults), referenciado en `config.cpp` (`inputs = "BZ_AutoDrive/data/inputs.xml";`). `build_include.lst` incluye `*.xml` (sino AddonBuilder lo excluye en silencio). Rebindeables desde el menú; admin-only.

**Detección:** se pollea en el `OnUpdate` del cliente (`BZBusMissionGameplay.c`) con `GetUApi().GetInputByName("UABZ...").LocalPress()`; client-side → server vía RPC (validado server-side por `IsControlPanelAdmin`). **Guard crítico** (sino las acciones se disparan al reasignar una tecla): `if (GetGame().GetUIManager().GetMenu() || GetGame().GetUIManager().IsDialogVisible()) return;` antes del poll — NO `HasGameFocus()` (= foco de ventana del SO, sigue `true` con un menú abierto).

**Diseño de defaults (rediseño 2026):** el menú de Controles "BZ AutoDrive" quedó reducido a **3 inputs, los 3 asignados** — el core loop entero. Se sacaron los ~16 controles viejos de debug/tuning/modo (Parking/Reverse/Maneuver Mode, AI Logger, SysID, Spawn Slots, Pause, Mark Gear/Max…) y también la vieja **tecla de intercambio** (`UABZMarkLeg`/NUMPAD 3, sacada 2026-08-11): las maniobras salen del **intercambio auto-detectado** (cambio de gear forward↔reverse), la reversa se **auto-detecta del gear**, y `ai_run`/`boris_native` se arman desde los **checks del Reproductor**. Los controles **solo los ve el admin** (gate `IsControlPanelAdmin`). Labels LITERAL en inglés (el menú de inputs NO resuelve stringtable).

| Acción (label EN) | `UAName` | Default | Notas |
|---|---|---|---|
| Open Control Panel | `UABZAutoDrivePanel` | `kHome` | abre el Reproductor / panel |
| Record (start/stop) | `UABZRecord` | `kNumpad5` | toggle del PathLogger **+ FrameRecorder** |
| Mark Event / Stop | `UABZMarkStop` | `kNumpad4` | marca parada / nodo de evento (marca **ambos** recorders) |

> **Intercambio (`legBreak`), sin tecla:** el corte de tramo por cambio de sentido se **auto-deriva** del cambio de gear forward↔reverse (siempre a ~0 km/h) en `frame_to_route.py`; el editor también lo marca en el nodo al exportar. Un 0 km/h **sin** cambio de sentido = pausa, no intercambio.

> **Nombres de tecla** (preset): `kHome`, `kNumpad0`–`kNumpad9` **confirmados**. Truco para descubrir nombres: leer el preset del usuario tras bindear a mano (`Documents\DayZ\<perfil>.dayz_preset_User.xml`).
> `F` (público) y `ESC` siguen igual (UI de parada del bus / cerrar). El viejo conflicto del NUMPAD . doble-bind quedó **resuelto** al reducir a 3 teclas.

### D.2 — Defaults de `BZBusRouteConfig` (valores exactos)

Fuente: `scripts\4_World\BZBusConfig.c`. Convención: `-1` (y a veces `0`) = "usar la constante interna del código / derivar del vehículo".

**Control de manejo (AVANZADO — el wizard produce un solo control; estos flags son para un modder que quiera otro comportamiento, §5)**
| Campo | Default | Qué hace |
|---|---|---|
| `FollowPath` | `false` | `true` = velocidad por **curvatura** (geometría pura, ignora la grabada); `false` (default) = usa tu velocidad grabada |
| `FollowPathUseReference` | `false` | usa la velocidad grabada **capeada por la curva** (reference-assisted: grabación como límite + física del vehículo) |
| `UseInverseModel` | `false` | PID + modelo inverso para throttle/brake en vez de replay |
| `InverseModelKp / Ki / Kd` | `-1 / -1 / -1` | PID de velocidad; defaults 0.4 / 0.05 / 0 |

**Lateral / Stanley**
| Campo | Default | Qué hace |
|---|---|---|
| `SteeringScale` | `-1` | AUTO: deriva del wheelbase (`clamp(wb/5.5, 0.4, 1.0)`) |
| `CruiseLateralDeadband` | `0.0` | Semi-ancho del carril; dentro no corrige |
| `CruiseLateralKGain` | `1.0` | Multiplica el offset antes del atan de Stanley |
| `CruiseLateralDamp` | `0.0` | D-gain sobre el rate del offset (0.3 mata zigzag) |
| `CruiseLateralCenterOffset` | `0.0` | Bias del centro (+ derecha, - izquierda) |
| `CurvatureSteerBoost` | `0` | Amplifica steer en curva: `steer × (1 + boost × bendFrac)` |
| `CruiseFFWeight` | `-1` | Peso del feedforward en cruise; default 0.25 |

**Velocidad / cruise**
| Campo | Default | Qué hace |
|---|---|---|
| `AverageSpeedMS` | `11.0` | ~40 km/h, para ETA |
| `FollowPathMaxKmh` | `50.0` | Tope en recta (cuando `FollowPath=true`) |
| `FollowPathLatAccel` | `4.0` | Accel lateral máx (m/s²) para velocidad por curvatura |
| `TargetSpeedSmoothWindow` | `0` | Suaviza targetSpeed (5=moderado, 10=agresivo) |
| `CurveThrottleEnabled` | `true` | Corte anticipatorio de throttle por curvatura |
| `CurveThrottleStartDeg / FullDeg` | `35.0 / 80.0` | Bend acumulado donde empieza/llega al corte máx |
| `CurveThrottleMinScale` | `0.35` | Factor de throttle en curva cerrada |
| `CurveThrottleLookaheadM` | `14.0` | Distancia de escaneo del corte |
| `FollowPathSpeedSmooth` | `8` | Suaviza el perfil de velocidad (0=off) |
| `FollowPathCurveSpan` | `5` | Espaciado (wps) para medir curvatura |

**Gear / freno**
| Campo | Default | Qué hace |
|---|---|---|
| `MaxGear` | `6` | Marcha máx que la AT shiftea (FIRST=2, SIXTH=7) |
| `GearStrategy` | `"auto_box"` | "auto_box" o "follow_recording" |
| `AccelShiftThreshold` | `999.0` | Anti-catapulta (km/h/s); 999=off (bus), ~15 para 4x4 |
| `InverseModelLowRpmMin` | `false` | false = rpmMin×1.3 (conservador); true = ×1.0 |
| `EndFreezeDisabled` | `0` | 1 = no frena al final |

**Pendiente**
| Campo | Default | Qué hace |
|---|---|---|
| `SlopeCompensationEnabled` | `true` | Compensa throttle por pitch del path |
| `SlopeLookaheadWps` | `5` | Wps adelante para el lookahead |
| `SlopeGain / SlopeLateralGain` | `1.0 / 1.0` | Ganancia de compensación / bias lateral por pitch |

**AutoRecovery**
| Campo | Default | Qué hace |
|---|---|---|
| `AutoRecoveryEnabled` | `false` | Teleport cuando Boris se atasca |
| `AutoRecoveryStuckTimeS` | `10.0` | Segundos de stuck para activar |
| `AutoRecoveryAdvanceWps` | `5` | Cuántos wps adelante teleporta |
| `AutoRecoveryCooldownS` | `8.0` | Mínimo entre teleports |
| `AutoRecoveryMaxPerMission` | `0` | 0=ilimitado; X=falla misión si supera |

**Corredor / híbrido**
| Campo | Default | Qué hace |
|---|---|---|
| `CruiseHybridSteerThreshold` | `-1` | -1=off; 0.7=override volantazos grabados >70% |
| `CruiseHybridThrottleThreshold` | `-1` | -1=off; 0.5=override throttle grabado >=50% |

**Parking**
| Campo | Default | Qué hace |
|---|---|---|
| `ParkingStanleyK` | `-1` | Default = `STANLEY_K_PARKING` interno |
| `ParkingFFWeight` | `-1` | Default 0.6 |
| `ModeEntrySnapEnabled` | `false` | (2026-07-03: era `true`) Snap/teleport de alineación al entrar a un modo; apagado — el control cerrado (parking direct-replay + reverse rear-steer, heading <1°) posiciona solo. Reactivable por `_hdr` |
| `ModeEntrySnapMaxDist` | `0.5` | Distancia máx (m) para el snap |
| `AntiRollbackEnabled` | `true` | Handbrake en pendiente para no retroceder |
| `AntiRollbackPitchThreshold` | `0.05` | Pitch (rad, ~2.86°) para activar |

**Reverse** (todos `-1`/`0` = usar default interno)
| Campo | Default interno | Qué hace |
|---|---|---|
| `ReverseStanleyK` | `STANLEY_K_REVERSE` | Ganancia Stanley en reverse |
| `Wheelbase` | del fingerprint | Distancia entre ejes (feedforward rear-steer) |
| `ReverseFFSign` | `-1` (flip) | Signo del feedforward (rear-steer invertido) |
| `ReverseFFMaxSteerRad` | `0.6` | Normalización del feedforward |
| `ReverseFFWeight` | `=ParkingFFWeight` | Peso del feedforward |
| `ReverseSteerGateOffset` | `0.5` | Umbral (m) del gate "discrete input" |
| `ReverseSteerThrottleFloor` | `0.35` | Piso del steer-then-throttle |
| `ReverseSteerMax` | `1.0` | Clamp del \|steering\| |
| `ReverseRecordedSteerThreshold` | `0.2` | Umbral para seguir el volante grabado |
| `ReverseTargetSpeedCap` | `25` | Cap de velocidad (km/h) |
| `ReverseStanleyFineMax` | `0.15` | Cap de corrección fina |
| `ReverseHeadingDeadbandDeg` | `4` | Deadband de heading (grados) |

**Smoothing / maniobra (legacy)**
| Campo | Default | Qué hace |
|---|---|---|
| `PathSmoothWindow` | `5` | Moving-average de posiciones (0=off; **usar 0 en curvas cerradas** — achata 90°) |
| `DirectReplayFromWaypoint` | `-1` | Wp desde el que se bypasea el control y se hace replay directo (legacy) |

### D.3 — Formato del `ai_run_*.csv`

Log server-side de la corrida de Boris (opt-in con el **check `ai_run` del Reproductor**, ya no con tecla). Una fila cada ~0.5 s (~2 Hz). Se escribe en `$profile:BZ_AutoDrive_PathLogger\ai_run_<ts>.csv`. **27 columnas** (con header), en este orden:

```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```

Clave para análisis: `lateral_dev_m` (desvío lateral firmado vs la ruta — el % dentro de ±2 m y la mediana salen de acá), `steering` (saturaciones / cambios de signo = zigzag), `mode` (cruise/parking/reverse/...), `is_marker` (eventos marcados con NUMPAD 4). No confundir con el `path_*.csv` del PathLogger (la grabación humana, 50 Hz, otras columnas).

### D.4 — Estructura de `BZBusRoute.json`

El framework prefiere el par `_hdr.json` (config, sin waypoints) + `_wp.csv` (los waypoints, fast-load por FGets) sobre el JSON monolítico. Ejemplo condensado (real, de `BZBusRoute_hdr.json`):

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
    // ... (N waypoints en total)
  ]
}
```

Notas: `targetLights` (0/1) y `targetHorn` (0=OFF/1=SHORT/2=LONG, `ECarHornState`) = luces/bocina grabadas, reproducidas por wp (replay espacial; ver §5). En el `_wp.csv` van en col 16 (`targetLights`) y col 17 (`targetHorn`). `Attachments` debe REEMPLAZARSE al cambiar de vehículo (partes de fuente autoritativa, NUNCA adivinar). `Events` vacío = sin eventos. Los waypoints finales pueden tener `"mode": "reverse"` para una maniobra de estacionamiento. `Crew` no es campo del JSON (los bots del convoy vienen del Quest, no del JSON).

### D.5 — Guía: agregar un verbo nuevo al DSL de eventos

1. En `scripts\4_World\BZBusService.c`, ubicá el dispatcher `ExecuteAction(BZAction a)` (el `if/else if` por `a.verb`).
2. Agregá una rama: `else if (a.verb == "miverbo") { /* tu lógica */ }`. Usá los campos existentes de `BZAction` (`strParam`, `floatParam`, etc.).
3. Si necesitás un campo nuevo, agregalo a la clase `BZAction` en `BZBusConfig.c`. **Enforce no admite args de constructor**: creá el objeto vacío y seteá campos.
4. Si el verbo manda algo al cliente (UI/sonido/toast), reenvialo por RPC (ver `BZBusRPC.c`, p.ej. `RECEIVE_TOAST`).
5. Reempaquetá el PBO, después probá agregando el verbo a un `Events[]` del JSON → deploy → `NUMPAD 2` recarga la ruta sin reiniciar el server.

### D.6 — Guía: agregar un vehículo nuevo

1. **Classname exacto** del carpack (de fuente autoritativa: trader package / config.cpp / spawn-inspect). Copiá la `.bikey` del mod a `keys\` (A y B).
2. **Grabá la ruta COMPLETA** como humano: `NUMPAD 5` inicia/para. El header (`header_*.txt`) captura el fingerprint (wheelbase, ángulo de dirección → R_min, marchas, masa, **partes reales**).
3. **Wizard** (doble clic en `tools\Wizard.bat`): **[1] Convertir** te pide **solo el NOMBRE** (→ `BZBusRoute_<nombre>.json`). `frame_to_route.py` lee el header, generaliza, auto-detecta reversa y escribe el trío `.json`+`_hdr.json`+`_wp.csv`, ya **desplegado** (hot-load). Sin modos ni linters.
4. **Deploy:** lo hace el propio Convertir (split + escritura a `profiles\BZ_AutoDrive\`). El mirror a B sale de `wizard_config.json` (`ServerBMirror`); ver **[6] Configurar paths**.
5. **Validá**: tildá el check **`ai_run`** en el Reproductor + spawneá/test. Analizá el `ai_run` (ver D.3) y ajustá params por síntoma (ver D.2 + §14).

> **No-negociable:** al cambiar `VehicleClass`, REEMPLAZAR `Attachments` por las partes del vehículo nuevo (`csv_to_route` preserva las anteriores → rompe el spawn). Bug recurrente.

### D.7 — Guía: mapear una zona (armar y unir grafos de rutas)

**Qué es:** más allá de reproducir UNA toma, se graban MUCHAS y se componen en un **grafo dirigido** → un pathfinder (**Dijkstra**) arma rutas A→B **que nunca se grabaron enteras**. Hoy es **Tier 2**: herramientas offline asistidas por LLM (la grabación es in-game; el armado del grafo y el ruteo se resuelven fuera del juego; el roadmap es llevarlo al runtime).

**Cómo se une el grafo (automático e incremental):**
- Cada punto (downsampleado ~4 m) de cada traza = un **nodo**.
- Donde dos trazas **se cruzan o rozan ≤6 m** = una **arista/intersección** (auto-merge; clusters ≤10 m).
- Es **dirigido**: cada calle se recorre solo en el **sentido grabado** (= respeta el carril). Para ir y volver, **grabá los dos sentidos** (cada sentido = una traza).
- Una traza nueva **se integra sin tocar las anteriores** (monótono: solo suma cobertura).
- **Giros:** grabados (se siguen de puntos reales) vs no-grabados (arco al **R_min** del vehículo). El destino se extiende a la intersección perpendicular.

**La herramienta — `bz_coverage.py`** (en la PC del dev; **script de análisis, NO va en el PBO**): lee los `path_*.csv` de la carpeta de grabaciones del cliente, **filtra por un bbox** (`X0,X1,Z0,Z1`) y reporta **tramos, km, nodos, intersecciones, bbox** + guarda un **coverage map PNG**.
- ⚠️ El `HD` (carpeta de grabaciones) y el `bbox` del script son **del setup de Sonom4n** → **ajustalos a los del usuario** (preguntá su carpeta PathLogger + la zona a mapear).
- Para hallar el bbox de un pueblo: sacá la mediana (x,z) de cada `path_*.csv` y agrupá por celdas de ~500 m; la celda con más trazas = la zona densa.

**Cómo guiar a un modder a mapear (vos, la IA):** (1) pedí los **paths** + la **zona** · (2) fijá el **bbox** del pueblo · (3) corré → reporte + mapa → identificá **huecos** (calles/sentidos que faltan) → decile cuáles grabar · (4) **iterá** (grabar → re-correr, la cobertura crece monótona) · (5) armá **grafo + Dijkstra** A→B → exportá como **toma ejecutable**.

**Gotchas:** dirigido (sin el sentido inverso no se vuelve) · umbrales 6 m/10 m según escala del mapa · giros no-grabados = arco R_min (grabá el giro si querés fidelidad).
**Aplicaciones:** ciudad ruteable · sector de mapa · **vías de tren** (el grafo *son* las vías: sin steering ni giros, solo accel/freno). Caso testigo: Novaya Petrovka, Chernarus — 9 trazas → 5.1 km, ~1200 nodos, ~102 intersecciones.

### D.8 — Guía: agregar un vehículo a un Quest (convoy)

**Reparto:** el modder configura **(1) la RUTA** (lado framework) + **(2) el QUEST** (lado Expansion). El **hook ya está pre-construido** en el PBO — no se escribe código.

**ARCHIVO 1 — la ruta** (`<profiles>\BZ_AutoDrive\BZBusRoute*.json`). Líneas de convoy (ej. emboscada):
```json
{
  "VehicleClass": "x5mcompetition_orange",
  "ConvoyMode": "ambush_on_damage",
  "VehicleInvincible": false,
  "Crew": [],
  "Waypoints": [ /* la ruta grabada */ ]
}
```
- `ConvoyMode`: `"flee_on_kill"` (matás 1 → suben y huyen) o `"ambush_on_damage"` (a bordo armados → daño → freeze + dismount + campean).
- `VehicleInvincible: false` ← **OBLIGATORIO para `ambush_on_damage`** (si es irrompible nunca recibe daño → no dispara).
- `Crew: []` ← vacío: **los bots los pone el Quest**, no la ruta. (`Crew[]` es para bots que viajan SIN quest.)
- `SpawnHoldSeconds: 600` (opcional, flee): el vehículo espera quieto hasta el disparo.
La ruta sale del flujo normal (grabar → wizard → deploy a `profiles\BZ_AutoDrive\`).

**ARCHIVO 2 — el Quest** (config de **DayZ-Expansion-Quests**, en la data del mod de quests del usuario, **NO** en el framework): ahí definís los **bots vivos** (patrulla/campamento), el objetivo (matarlos), el reward — con el editor/JSON de Expansion-Quests. *El framework NO spawnea bots armados con sentido; eso es exclusivo del Quest.*

**El hook (YA en el PBO — solo para entender):** `modded class MissionServer.Expansion_OnQuestStart` → `BZBusService.OnQuestStart(quest)` guarda el quest ID (`qc.GetID()`) y **pollea** `ExpansionQuestModule...QuestPatrolExists(questID, patrols)` cada ~4 s (los bots son *lazy*, por proximidad). Cuando el conteo de bots **baja** (mataron uno) → dispara la escena.

**Dependencia:** el `config.cpp` del framework declara `DayZExpansion_Quests_Scripts` (para publicar conviene splitearlo a sub-addon opcional).

**Checklist para el modder:** (1) grabá+deployá la ruta con `ConvoyMode` seteado · (2) en Expansion-Quests creá el quest con sus bots cerca del **arranque de la ruta** · (3) probá: aceptás → aparecen los bots → matás 1 (flee) o pegás al vehículo (ambush) → dispara.
**Gotchas:** ambush sin `VehicleInvincible:false` no dispara · bots en el Quest (no en `Crew[]`) · ejemplos completos validados en el **manual §12.3/§12.4** (convoy) y §12.10/§12.11 (Travel/Escort, que auto-seleccionan ruta por quest).

### D.9 — Referencias externas + búsquedas (wiki + material citado)

*Para mandar al modder a la fuente autoritativa, o buscar más. El **código local es la verdad**; esto es contexto externo.*

**DayZ / Enforce (modding del juego):** BIS Community Wiki — base `https://community.bistudio.com/wiki/` · DayZ modding: `https://community.bistudio.com/wiki/DayZ:Modding_Basics`. Buscar: `DayZ Enforce Script`, `DayZ CarScript SetThrottle`, `DayZ Transport WheelGetSurface`, `DayZ modded class MissionServer`, `DayZ EmoteManager`, `DayZ ConfigGetFloat CfgVehicles SimulationModule`.

**DayZ-Expansion (eAI + Quests + vehículos):** source público `https://github.com/salutesh/DayZ-Expansion-Scripts` (mirar `ai_scripts`, `quests_scripts`, `vehicles_scripts`). Buscar: `DayZ Expansion AI eAIBase`, `Expansion Quests ExpansionQuestObjective`, `Expansion StartCommand_Vehicle`, `Expansion_OnHandleController`.

**Motor anterior / Related Work (deep-research verificada):**
- ARMA 3 AI driving (OPREP oficial): `https://dev.arma3.com/post/ai-path-following-improvements`
- ARMA 3 `AICarSteeringComponent` (Biki): `https://community.bistudio.com/wiki/Arma_3:_AICarSteeringComponent` · `setDriveOnPath`: `https://community.bistudio.com/wiki/setDriveOnPath`
- Enfusion (Reforger) navmesh: `https://community.bistudio.com/wiki/Arma_Reforger:Navmesh_Tutorial`
- EA SEED, *Efficient Ground Vehicle Path Following in Game AI* (CoG 2023): `https://arxiv.org/abs/2307.03379`
- Codevilla et al., *Limitations of Behavior Cloning* (ICCV 2019) · *One-Shot Imitation Learning* (NeurIPS 2017).

**Tooling (PC del dev):** ImageToPAA (PNG→PAA), AddonBuilder (build PBO). Buscar: `DayZ Tools ImageToPAA`, `DayZ AddonBuilder pbo`.

### D.10 — Diagnosticar una corrida (ai_run) — AI-actionable

*Para que una IA (vos: Claude/GPT/Gemini) **diagnostique una corrida de Boris** leyendo un archivo `ai_run` que el admin te pega. Esto es ejecutable: leé las columnas precomputadas, mapeá las firmas a causas, y entregá el diagnóstico **en castellano para el admin**, en el formato de abajo. Complementa §14 ("Analizar un ai_run") y D.3 (formato del archivo).*

**Qué es el ai_run.** Es la **telemetría de una corrida de Boris** — la "caja negra" de lo que el NPC efectivamente hizo. Es **opt-in** (el admin lo arma tildando el **check `ai_run` en el Reproductor**, ANTES de dar play — ya no con tecla; sin eso NO se escribe). Vive en `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv` (el server que corrió: A `C:\DayZServer\profiles\...`, B `Y:\profiles\...`). **NO lo confundas con la toma humana** (`path_*.csv`, la grabación/demostración, 50 Hz, otras columnas) — son archivos distintos con propósito opuesto (medición vs referencia).

**Esquema REAL (27 columnas, ~2 Hz)** — este es el formato ACTUAL; LIDERÁ con él. Header tal cual (con `header`, en este orden):
```
time_s, x, y, z, heading_deg, speed_kmh, gear, throttle, brake, steering, mode,
dist_to_next_stop, next_stop_idx, wp_idx, lateral_dev_m, corridor_offset, corridor_valid,
target_speed, target_throttle, target_brake, i_speed, i_throttle, i_brake,
rpm, redline_rpm, wp_mode, is_marker
```
**Clave: el archivo YA trae el diagnóstico precomputado por el framework — LEÉ esas columnas, NO las recalcules.** El ai_run **NO necesita la ruta aparte**: el desvío lateral y los objetivos ya vienen adentro.

**Columnas que LEÉS directo (lo que hace cada una):**
- `lateral_dev_m` — **desvío lateral firmado por muestra** vs la ruta (ya calculado). Dónde se va **ancho**. Usá MEDIANA y segmentá recta/curva; el promedio firmado engaña por outliers/saturaciones (§14). **Alto en curva** = entra rápido / understeer.
- `target_speed`, `target_throttle`, `target_brake` — los **objetivos** del controlador. **Deficit = `speed_kmh − target_speed`** directo (negativo sostenido = Boris no alcanza el target → lugging/pendiente).
- `corridor_offset`, `corridor_valid` — tracking del **corredor/paredón**. `corridor_valid == 0` → **off-path** (Boris se salió de la banda del carril en ese tramo).
- `rpm`, `redline_rpm` — motor. **`rpm` cerca de `redline_rpm` a baja `speed_kmh`** = **lugging** (marcha corta a tope sin avanzar) o gear mal elegido.
- `mode` = controlador **activo** (cruise/maniobra/parking/reverse); `wp_mode` = modo **declarado** del waypoint. Si difieren, o si `mode=cruise` en un tramo lento-cerrado, hay un mismatch modo↔geometría.
- `is_marker` — muestras marcadas con **NUMPAD 4** = **eventos/paradas** que el admin quiso señalar; anclá el análisis ahí.
- `wp_idx` — waypoint que Boris persigue; `next_stop_idx`/`dist_to_next_stop` = próxima parada.
- `i_speed`, `i_throttle`, `i_brake` — términos internos del controlador (PID/inverse); útiles para entender por qué pidió ese throttle/brake, no para el diagnóstico de primer nivel.

**Cómputos que aplicás LEYENDO esas columnas:**
- **Desvío lateral:** mediana de `lateral_dev_m` (segmentá recta/curva). Picos en curva = se va ancho.
- **Deficit de velocidad:** `speed_kmh − target_speed`; ubicá dónde se vuelve negativo sostenido.
- **Off-path:** tramos con `corridor_valid == 0`.
- **Lugging:** `rpm`≈`redline_rpm` con `speed_kmh` baja (+ `throttle`>0).
- **Stuck/trabado:** `wp_idx` **constante** por muchas muestras + `speed_kmh ~0`. Mirá `mode`/`gear`/`rpm` ahí.
- **Modo vs geometría:** `mode` vs `wp_mode` y vs la velocidad del tramo.
- **Completion:** ¿`wp_idx` llegó al **último** wp? Si no, hasta dónde.

**Fallback (archivos VIEJOS de 14 columnas, o para verificar).** Tomas viejas traen solo `time_s,x,y,z,heading_deg,speed_kmh,gear,throttle,brake,steering,mode,dist_to_next_stop,next_stop_idx,wp_idx` — **sin** `lateral_dev_m`/`target_*`/`corridor_*`/`rpm`. Ahí SÍ recomputás a mano (y conviene pedir la ruta `BZBusRoute_<nombre>_wp.csv`/JSON para el path y las velocidades objetivo):
- `dt` = diff de `time_s`; v en m/s = `speed_kmh/3.6`.
- **AR teleport / salto:** distancia real entre 2 muestras = `sqrt(dx²+dz²)`. Si **>> `(speed_kmh/3.6)·dt`** (ej. >3× y varios metros de golpe) → **AutoRecovery teleportó** a Boris (estaba trabado u off-path); el salto marca DÓNDE se trabó (mirá el `wp_idx` previo).
- **Desvío lateral** = distancia de `(x,z)` al path de la ruta cerca de ese `wp_idx` (lo que `lateral_dev_m` ya da en el formato nuevo).
- **Deficit** = `speed_kmh` vs la `targetSpeed` del wp en la ruta.

**Tabla de firmas de falla → causa → fix:**
| Firma en el ai_run (columna) | Causa probable | Fix sugerido |
|---|---|---|
| `wp_idx` se clava + `speed_kmh`~0 sostenido | curva imposible / gear alto / off-path | re-grabar más amplio y lento (con ESE vehículo); revisar gear |
| salto de posición (sqrt(dx²+dz²) >> v·dt) | AutoRecovery rescató a Boris (se trabó ahí) | atacar la **causa** del trabe en ese wp |
| `speed_kmh` << `target_speed` sostenido | lugging / deficit de velocidad | `GearStrategy=follow_recording`; revisar pendiente |
| `lateral_dev_m` alto en curva | overspeed / understeer, se va ancho | entrar más lento; curva más amplia; cap por curvatura |
| `corridor_valid==0` en un tramo | Boris off-path (fuera del carril) | revisar entrada de curva; AutoRecovery; re-grabar el tramo |
| `rpm`≈`redline_rpm` + `speed_kmh` baja + `throttle`>0 | lugging (marcha corta a tope) | `GearStrategy=auto_box` o `follow_recording` |
| `mode=cruise` en tramo lento-cerrado | el corte cae en zona rápida | re-grabar el tramo más lento; si es cambio de sentido, frená del todo en la recta e invertí la marcha ahí (el intercambio se auto-detecta del gear) |
| `wp_idx` no llega al final | no completó | diagnosticar el último problema de arriba |

**Cómo entregar el diagnóstico (vos → el admin, en castellano):**
1. **Resumen:** ¿completó sí/no? + DÓNDE están los problemas (wp + tipo).
2. **Por cada problema:** la **firma** observada (citá la columna: `lateral_dev_m`, `corridor_valid`, `speed_kmh` vs `target_speed`, `rpm`/`redline_rpm`…) + la **causa** + el **fix concreto** (citá el param de D.2 / §5 cuando aplique).
3. **Recordá** que el fix suele ser **re-grabar ese tramo** o **ajustar un parámetro de la ruta** (no realimentar el ai_run).

> **PRINCIPIO (no violar):** el ai_run es una **MEDICIÓN** que se cruza CONTRA la toma humana para calibrar (feedback funcional); **NUNCA una referencia nueva**. **Jamás** sugieras convertir un `ai_run` en ruta: clona los errores de Boris → degrada cada iteración ("model collapse"). El wizard lo filtra a propósito. La toma humana es la referencia sagrada; el ai_run es el termómetro.

### D.11 — Guía: importar una toma de BrigadaZ Transport v1

*Convertir una ruta del mod viejo **BrigadaZ Transport v1.0** (un JSON monolítico con `Waypoints`) al formato AutoDrive, sin regrabar. Wizard → **[2] Importar toma v1**.*

1. **Elegís el `.json`** de la toma v1.
2. **Identidad del vehículo (`--fingerprint`):** la toma v1 NO trae `Wheelbase`/`Fingerprint`/`Attachments` (dependen del VEHÍCULO, no de la ruta). Le das **cualquier** `header_*.txt` de ese vehículo (sirve una grabación de **10 s**) o un `_hdr.json` de una toma ya calibrada. El wizard pone primero los que matchean el vehículo declarado.
3. **Perfil de obstáculos (AR_OnWay, §5):** `[R]obusto` (Slow+Escape ON — un bus de línea que sortea lo que le tapa el camino) · `[I]nterceptable` (Slow ON, Escape **OFF** — frena pero NO se escapa → sirve para misiones de interceptación) · `[N]inguno` (réplica pura). Setea `ObstacleSlow`/`ObstacleEscape` en el `_hdr` (requiere `UseInverseModel=true`, que el template trae).
4. **Salida:** el trío `BZBusRoute_<nombre>.json`+`_hdr.json`+`_wp.csv`, ya desplegado. Corre `transport_v1_to_route.py` (misma firma que `frame_to_route.py`).

**Qué migra y qué no:** la **traza + velocidad + paradas** migran (el header/wp de AutoDrive es superconjunto estricto de los de v1). Los **pedales** de v1 se **descartan** (el control se reconstruye desde traza+velocidad, no se replica). El **perfil de manejo es por vehículo** → sale del `--fingerprint`, no de la ruta v1. `targetHeading` se deriva por geometría (`atan2(dx,dz)`, error mediano ~0.35°). Ver manual §6.2.
