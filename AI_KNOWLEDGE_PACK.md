# BZ_AutoDrive — AI Knowledge Pack

> **Para la IA que lee esto:** sos un asistente que va a ayudar a un admin/modder con **BZ_AutoDrive**, un framework de conducción autónoma de vehículos por NPC en DayZ (motor Enfusion, lenguaje Enforce). Este documento te pone en contexto COMPLETO para **configurar, programar, escribir, modificar y seguir investigando**. Es más exhaustivo que el manual (para humanos): es tu base de conocimiento operativa. Cuando ayudes, citá paths y classnames exactos, respetá los gotchas (§12) y verificá contra el código actual antes de afirmar.
>
> *Versión ES — **2026-09-25 · BZ AutoDrive 1.2**. Esta pasada reescribe el pack alrededor del **cerebro 1.2** (`scripts\4_World\bz12drive\`, §5), el que corre por defecto. Lo que describe el cerebro **1.1** (Stanley + modelo inverso de `BZBusService`) queda como **LEGACY en §5-L**. Lo nuevo: la **cinta** (`_cinta.csv`, el manejo a 40 Hz indexado por arco) · el **envelope** por clase (lo MEDIDO manda sobre el config) · el **creep derivado del `coast2`**, viajando en el `_hdr.json` · el **piso de despegue** (prior de flota 0,13; la calibración existe en el banco, §5.7) · la conversión escribe **cuatro** archivos (§7.0) · los **gotchas de 1.2** (§12.0) · el **AutoRecovery** que detecta la traba por avance y rescata también en reversa (§5.9) · la **API del runner** (§11) · la **guía para portar a otro juego** (Apéndice E). El editor de trayectorias se **descartó**. Historial de versiones anteriores en el CHANGELOG. MIT.*

---

## 0. Cómo usar este documento

- **Orientate** con §1 (qué es) + §2 (estado, dónde estamos).
- Para **configurar** una ruta/escenario → §6 (config) + §7 (wizard/pipeline) + §8 (eventos) + §9 (quests).
- Para **programar/extender** → §11 (patrones de código) + §4–5 (arquitectura/control) + §12 (gotchas).
- Para **seguir investigando** → §14 (metodología) + §15 (frontera) + §5 (control internals).
- **Para GUIAR a un usuario en vivo** por el core loop (grabar → convertir → play), asumiendo que **NO programa** → seguí el **Walkthrough §7.G**: UN paso a la vez, confirmá antes de avanzar, traducí todo a acciones concretas (qué tecla, qué archivo, qué mirar) y verificá los archivos en disco. Para **MAPEAR una zona** (unir grafos de rutas) → **Apéndice D.7** (⚠ no disponible en 1.2: sus herramientas no vienen en el paquete).
- **Para entender CÓMO maneja 1.2** → §5 (el cerebro 1.2). §5-L es el 1.1, legacy. **Los gotchas de 1.2** → §12.0, leelos antes de diagnosticar nada.
- **Para PORTAR el framework a otro juego** (7 Days to Die / Unity + C#, o cualquier motor con física de vehículos) → **Apéndice E**: guía paso a paso escrita para que una IA de programación (Codex, ChatGPT, Claude) guíe el port, con el código de una prueba de concepto que ya manejó sola en 7DTD, hitos medibles, los formatos de archivo y la especificación del cerebro.
- **Regla de oro:** este pack refleja el estado al **2026-09-25 (1.2)**; el código es la verdad. Antes de afirmar una firma de función o un default, confirmá con un grep al archivo citado.

---

## 1. Qué es BZ_AutoDrive (resumen ejecutivo)

Una **capa de piloto sobre eAI** (DayZ-Expansion-AI). eAI da el cuerpo del NPC (caminar, subir/bajar de un auto) pero conduce naïve (navmesh de infantería, dirección = ángulo/π, **siempre 1ra marcha**, cero lectura de config). BZ_AutoDrive sobrescribe sus salidas de conducción para que un NPC maneje **cualquier vehículo** siguiendo **una demostración humana grabada**, leyendo la **configuración declarada del vehículo** ("config como manual de manejo") y **reproduciendo la toma en el espacio** (en 1.2: la *cinta* + pure-pursuit lateral + modelo inverso derivado del config y corregido por lo **medido** en el *envelope* — §5). **No entrena por vehículo**: lee el config y **mide** de tus propias tomas.

Tres ideas: (1) **demostración = mapeo de ruta** (DayZ no tiene red de caminos; la grabación la suple); (2) **config = manual de manejo** (el `config.cpp` declara torque/gearbox/steering; el framework lo lee, no lo adivina); (3) **control clásico interpretable** (determinista, inspeccionable, zero-training).

---

## 2. Estado del proyecto — DÓNDE ESTAMOS (2026-09-25, BZ AutoDrive 1.2)

### 2.0 — La 1.2.1 (2026-09-29): lo que cambió desde la 1.2 *(leé esto primero)*
- **DayZ 1.30:** un solo PBO compila en la 1.29 y la 1.30 (`#ifdef DAYZ_1_29`). La 1.2 publicada **no compila en la 1.30** (`ToggleHeadlights()` ya no existe): con la 1.30 hay que usar la 1.2.1. Luces con el sistema nuevo. Borrar un vehículo con alguien sentado tira el server en la 1.30: el framework espera a que quede vacío.
- **Motos (experimental):** `MotorbikeScript` **no es CarScript** y tiene conductor propio (`BZConductorMoto`): `SetBrake` es el freno DELANTERO y `SetHandbrake` el TRASERO (los comentarios de `Motorbike.c` están al revés); sin reversa; ley de inclinación con amortiguación; caminar con la moto se graba y se reproduce (con animación, hacia adelante y atrás); destrabe caminando 3 m atrás. **La planta dirección→inclinación tiene 0,4-0,7 s de retardo:** la ley directa (ganancia 2) oscilaba a 85-100 km/h (hasta 3,7 m), así que arriba de 65 km/h (entera desde 85) corre una **cascada por inclinación** (roll objetivo desde la curvatura grabada 0,3 s adelante; la dirección sigue al roll; constantes `CASC_*`). El roll grabado NO sirve de base (el humano inclina de más con los toques de teclado): queda para la guarda. Final por **eficacia de freno medida** en el slam (m/s² por unidad de pedal). Caminata: el rumbo se impone por orientación cuadro a cuadro (con las ruedas apoyadas la velocidad angular impuesta no gira la moto), el lateral se corrige por el rumbo, y al terminar arranca a fondo. Llegadas de 1-4 cm; Enduro a 120 km/h con desvío ≤ 0,56 m.
- **Velocímetro NaN (1.30):** `GetSpeedometerAbsolute()` puede dar NaN varios segundos tras el spawn (medido en un Offroad_02 automático). Toda lectura pasa por `KmhDe()` (cae a la velocidad del cuerpo); `BZMath.Finite` decide qué es un número (el motor no compara NaN como IEEE: `NaN > 0.5` dio verdadero). Para probar: `$profile:BZ_AutoDrive\BZ12_PRUEBA_NAN_S.txt` con N segundos fuerza la lectura cruda a NaN.
- **El veredicto estricto:** `ROUTE_COMPLETE` exige, en **reposo**, ≤ 0,25 m de la pose final **y** ningún choque contra un objeto **y** ninguna intervención física del AR. El FIN y los eventos de fin salen **en el reposo** (≤ ~3,5 s después de llegar), con `[C12] REPOSO tras T s: a X m …`. `RunnerReposoFinM()` nuevo. La red de 1 m ya no aprueba.
- **Precisión:** la frenada final y la de cada cusp se miden en la toma (`FRENADA de la toma` / `FRENADA del cusp`): el `MaxBrakeDecel` del config sobreestima (8,5 contra 5,5 real). La cuerda del anclaje de la cinta corre en avance y reversa (solo con el casco rodando) y descarta cuerdas contra el rumbo grabado (retrocesos de la toma). El leg cierra solo en la ÚLTIMA parada grabada de la fase; las paradas intermedias se cumplen por tiempo (también en maniobra, con 1,5 s de gracia tras una meseta). Freno al fin del leg si la toma se agota con el casco rodando. Imán lateral ×3 en los últimos ~6 batallas antes de un cusp. Sin imán de gas contra un freno grabado.
- **Wizard:** reloj real del grabador (una toma con reloj roto no se convierte); avisos `(!)` de retrocesos y de arrastre sin freno antes de un cambio de marcha; el wp de un cusp va en el reposo de la toma.
- **Panel:** en la 1.30 cierra con la tecla que lo abrió (INICIO por defecto) y con ESC (sin abrir la pausa).
- **Logs:** `Diagnostics` (off por defecto: RPT > 90 % más chico) y `BlackBox` (off por defecto) / `BlackBoxKeepPerRoute` (10 por defecto) en el settings.
- **Limitación conocida:** lo que la toma avanzó a < 1 km/h dentro de la última parada antes de un cambio de marcha (típico 30-40 cm) no se repite. Regla de grabación: freno pisado hasta poner la otra marcha.

**El norte (septiembre 2026): el producto es el FRAMEWORK.** BZ_BusDrive, BZ_Convoy, BZ_HeliDrive y BZ_SeaDrive son mods **aparte** que lo usan como dependencia. Reparto de responsabilidades, que es la regla que vas a usar para decir que no:
```
el FRAMEWORK   reproduce la toma y GARANTIZA QUE EL VEHICULO NO SE QUEDE DETENIDO
el MOD         pone la INTENCION: cuando parar, a quien ceder el paso, que mision, con quien
```
Si un modder te pide meter lógica de su juego adentro del framework (una cesión de paso, un horario, una regla de misión), eso va en **su** mod, que consume al framework — no en el framework.

**1.2 es un cerebro nuevo.** `BZBusService` quedó como **shell** (spawn, eventos, quests, UI, luces); la **decisión de manejo** la toma el stack de `scripts\4_World\bz12drive\` (§5). Lo nuevo, en una línea cada uno:

- **La cinta** (`_cinta.csv`): tu manejo a 40 Hz, indexado por **arco recorrido** (no por reloj). El NPC reproduce el input que tenías **en ese punto del camino** y deja que la física del motor maneje. Es la diferencia entre "seguir waypoints" y "reproducir tu manejo".
- **El envelope** (`envelope_<clase>.csv`, uno por **clase** de vehículo): lo que el framework **midió** de ese vehículo. Cada dato lleva procedencia (CONFIG / DERIVED / MEASURED / FALLBACK) y **lo medido manda sobre lo derivado del config**.
- **El creep es 0.** Ningún vehículo de este motor se arrastra solo en ralentí (**43 de 43** medidos). Se deriva del `coast2` de la propia toma (la decel en punto muerto casi parado: > 0 = no repta) y **viaja en el `_hdr.json`** (`CreepKmh`, `CreepSource`, `CreepSamples`, `CoastAtRest`).
- **"¿Está quieto?" lee la medición** (`idleCreepKmh`), no el objetivo de arrastre (`creepKmh`). Armados de rampa con el vehículo rodando: de 48 a 0.
- **El piso de despegue se MIDE** con una calibración de banco (§5.7), que en la 1.2 publicada **no está al alcance del usuario**. Sin calibrar: **prior de flota 0,13** (mediana de lo medido; antes era un 0,35 elegido a mano). Efecto en ruta: **un tick de rampa**. Donde importa: la **parada en pendiente** (todavía sin medir).
- **La conversión escribe CUATRO archivos** + el envelope de la clase (§7.0).

**Validado en 1.2 (DEV, septiembre 2026):**
- **Svergino ×5 — 5/5 `ROUTE_COMPLETE`**, cierre ~0,10 m, 0 choques: **no-regresión** con todos los cambios de 914-930 encima. *(Criterio del proyecto para esa ruta: ≥ 5 pasadas, porque la línea base histórica pasaba 1 de 3.)*
- **Contrafactual del piso** (5 × 0,35 forzado contra 5 × 0,15 medido, misma ruta): 5/5 los dos; el piso sólo mueve el primer tick (0,4 s a 5 km/h, separación completa, p 0,008) y la pasada lo absorbe.
- **Cusp (cambio de sentido reversa → avance), fix 936:** Pavlovo, 5 km/h a los 2,3-2,6 s del cambio de leg (antes ~11 s con gas 0 / freno 1).
- **Fix 938 (NULL pointer):** Pavlovo 4/4 `ROUTE_COMPLETE`, 0 excepciones (antes 372-719 por sesión en el harness).
- **Piso medido en 9 clases**, de moto a camión (tabla en §5.7).
- **Llegada (endpoint) medida en más de 40 clases** (tabla en el manual §17.1): desde el 17/09, 52 llegadas, mediana 0,11 m, 87 % ≤ 0,25 m; del 02 al 16/09 (antes de la entrada a 40 Hz y el término lateral), 430 llegadas en 41 clases, mediana 0,19 m, 77 %. El freno final estaciona ~0,10 m antes a propósito.

**Abierto:**
- ✅ **Cerebro 1.2, cinta, volante FF-primario y Reproductor→runner POR DEFAULT** (decisión del 23/09). Hasta entonces los cuatro dependían de archivos de **presencia** que nadie creaba (`CEREBRO_12.txt`, `BZ12_CINTA.txt`, `BZ12_FF_PRIMARIO.txt`, `REPRODUCTOR_LANZA_11.txt`): sin ellos el suscriptor corría el 1.1, o el 1.2 sin cinta, o el volante viejo, y el Reproductor mandaba la ruta al banco de pruebas (que no se publica) y "cargar" no hacía nada. Ahora se **apagan** a pedido con un archivo **con texto** (`CEREBRO_11.txt`, `BZ12_SIN_CINTA.txt`, `BZ12_SIN_FF_PRIMARIO.txt`, `REPRODUCTOR_LANZA_HARNESS.txt`) y un archivo vacío no hace nada. **Verificá en `BZBusService.c` (`Cerebro12AlCargar`, `PrepararCarga12`, `BanderaConContenido`) qué dice el código que tenés adelante** antes de afirmar qué corre.
- ✅ **El envelope entero viaja en el `_hdr.json`** (`Envelope` + `EnvelopeClass`): una ruta llevada a otro server no arranca ciega. Manda la ficha de la clase si existe; la del hdr es el respaldo (§5.10).
- **Parada en pendiente** sin medir, e instrumento `[C12] PRIMER GAS tras el hold` sin ejercitar (necesitan paradas reales de BusDrive).
- **Prior de piso por masa**: evaluado, NO por ahora. Falsador pre-registrado: ≥ 15 clases calibradas, ≥ 4 de más de 3 t, y que la recta gane el leave-one-out sacando cualquier punto.
- **Reversa sin cinta**: en el harness (que no carga cinta) la reversa es reactiva y puede chocar en recta; con cinta, la cinta posee también la reversa.

**Descartado:** el **editor de trayectorias** (23/09). Contradecía la tesis de *no editar tomas*. Lo reemplaza un **visualizador de eventos** (ver paradas y eventos sobre la traza, escribir la cadena de eventos, exportar) que va **sólo a GitHub**. No lo recomiendes: no viene en el paquete.

**Lo que ya estaba validado en 1.1** (endpoint out-of-sample en 8 vehículos, reverse en 4, AR_OnWay en 5, las integraciones Quest) está en §5-L y en el CHANGELOG. El endpoint del 1.2 **conserva la lógica** del 1.1 (`BZEndpointSlam`: "en v1.1 clavaba ~8 cm y eso andaba"); la tolerancia de llegada es **una sola**, `BZDriveTol.LLEGADA_M = 0,10 m`.

**Visión a futuro:** NPC con LLM (el framework ya tiene forma de agente: DSL = acciones, grafo = navegación, config + envelope = percepción). Ver §15.

---

## 3. Mapa del codebase

**Source:** el repositorio de GitHub (`scripts\`, `tools\`, `config.cpp`, docs). Se empaqueta con el pipeline de cada uno y se firma con la llave propia (README, "Compilar desde el source").
**Paquete instalado:** `<server>\@BZ_AutoDrive\` (`addons\` + `keys\` + `tools\` + docs); los datos van en `<server>\profiles\BZ_AutoDrive\` (§7, "Dónde viven los datos"). **Cliente:** el mismo `@BZ_AutoDrive` (Workshop). **Con dos servers** (p. ej. uno de prueba en otra PC): los dos tienen que correr el **mismo PBO** antes de conectar el cliente; el Wizard puede copiar cada ruta convertida al segundo (`ServerBMirror`, opción [6]).

**Scripts (PBO), por scope Enforce:**
- `scripts\3_Game\` — helpers tempranos: `BZBusCommon.c`, `BZBusRPC.c` (enum de RPCs, incl. `RECEIVE_TOAST`), `BZBusStops.c`, `BZGearRangeTable.c`, `BZPathLogCommon.c`, `BZCleanupConfig.c`, `BZBusClientManager.c`.
- `scripts\4_World\` — **el núcleo**: `BZBusService.c` (singleton server-side; spawn, Tick, control, eventos/verbos `ExecuteAction`, quest hooks `OnQuestStart`/`CheckQuestBots`/`BoardQuestBots`; ~6500 líneas), `BZBusCarScript.c` (el `modded CarScript` override-last; también la **NetSync de luces** propia `m_BZLightsWanted` + `BZSetLights`/`OnVariablesSynchronized` para que un AI car observado muestre el haz — ver §12), `BZInverseModel.c` (PID velocidad + gear + slope + surface), `BZBusConfig.c` (clase `BZBusRouteConfig` con 50+ campos + `BZAction`/`BZTrigger`/`BZMarkerEvent`/`BZCrewMember`), `BZBusPlayerBase.c` (modded PlayerBase: OnRPC, NUMPAD handlers de grabación), `BZPathLogService.c` (PathLogger: grabación + fingerprint), `BZRouteCleanup.c`, `BZILCCorrections.c`, `BZBusStopZone.c`, `BZExpansionWreckFilter.c`.
- `scripts\4_World\bz12drive\` — **el cerebro 1.2** (21 archivos, ~17.000 líneas): `BZDriveController` (orquestador), `BZCintaRunner` (la cinta), `BZPurePursuit`, `BZLegMachine`, `BZModeArbiter`, `BZDerivationEngine`, `BZManeuverController`, `BZEndpointSlam`, `BZAutoRecovery12`, `BZVehicleDataReader`/`BZVehicleManual`, `BZEnvelopeStore`/`Recorder`/`Source`, `BZRouteData`/`BZRouteLoader`, `BZBusInputApplier`. Ver §5.
- `scripts\5_Mission\` — UI + hooks de misión: `BZBusMissionGameplay.c` (poll de inputs del menú de Controles en OnUpdate), `BZBusMissionServer.c`, `BZQuestHook.c` (`modded MissionServer Expansion_OnQuestStart`), `BZReproductorUI.c` (Reproductor), `BZControlPanelUI.c`, `BZBusUI.c`, `BZDebugOverlay.c`.

**Config/data:** `config.cpp` (CfgPatches/CfgMods clase `BZ_AutoDrive`, prefix `$PBOPREFIX$`=`BZ_AutoDrive`), `data\` (bus_stops.json, gear_ranges.json, wrecks_cleanup.json), `gui\layouts\` (control_panel_v2.layout, etc., formato CPP-style), `gui\textures\` (.paa), `stringtable.csv`, `meta.cpp` (name=BZ_AutoDrive, publishedid=0).

**Tools (PowerShell/Python, NO en el PBO — corren en la PC del admin):** `tools\` es **sólo el runtime del wizard**: `tools\route_wizard.ps1` (TUI; menú [1] Convertir / [2] Importar toma v1 / [3] Merge de tramos (continuar grabación, `frame_merge.py`) / [6] Configurar paths / [Q] Salir), `tools\Wizard.bat` (launcher de doble clic, `-ExecutionPolicy Bypass` solo para esa corrida), `tools\frame_to_route.py` (**el conversor real**: grabación → **los cuatro archivos** `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv`, sin modos), `tools\envelope_emit.py` (**el envelope**: lo invoca el wizard en cada conversión y escribe/fusiona `envelope_<clase>.csv` en `RoutesDir`), `tools\python\` (**Python embebido**: el wizard no depende del Python del sistema), `tools\transport_v1_to_route.py` (import v1), `tools\i18n_strings.ps1` (i18n del wizard, dot-sourced), `tools\driving_config_template.json` (el config de manejo que pega el conversor), `tools\wizard_config.json` (paths portables — **no se publica**). Las herramientas **dev/análisis/legacy** (validadores `enforce_lint.py`/`check_rpt.py`, análisis `endpoint_detail.py`/`analyze_*`, calibración vieja `calibrator_lib`/`route_calibrator`/`curve_advisory`, conversor viejo `csv_to_route`/`route_split`, informes `report_export.ps1`, asset-gen) se movieron a **`..\BZ_AutoDrive_devtools\`** (fuera del mod, **no se publican**).

**build:** empaquetá el PBO con DayZ Tools (AddonBuilder) o tu propio pipeline; firmá con tu propia llave. (No se distribuye ningún script de build — cada modder empaqueta a su manera.)

---

## 4. Arquitectura

**Override-last (el breakthrough).** El control se inyecta en un `modded CarScript.OnInput(dt)` que llama `super.OnInput(dt)` PRIMERO (eAI corre, incluido `ShiftTo(FIRST)`) y DESPUÉS sobrescribe `SetSteering/SetThrottle/SetBrake/ShiftTo`. Subordinación, no reemplazo: eAI nunca "sabe" que lo dirigen. Robusto a updates de eAI.

**Tres capas:** el **camino grabado** (posiciones 3D) = objetivo sagrado (se garantiza llegar a los waypoints en orden); la **física declarada** (config + Newton) = límites que no se violan; la **ley de control** = el puente.

**Flujo runtime:** `BZBusService` (singleton server) spawnea el vehículo + el chofer eAI, corre un Tick (~cada 500 ms para lógica de eventos; el control de inputs va en OnInput por frame), avanza el waypoint index, evalúa triggers y dispara `ExecuteAction` por verbo.

**El cerebro se elige por ruta (1.2).** `BZBusService` es el **shell**: spawn, pausa/hold, luces, eventos, convoy, quests y UI corren siempre. La **decisión de manejo** la toma el cerebro 1.2 (`Tick12` a 2 Hz + `SampleDrive12` a 40 Hz desde `CarScript`); lo que está debajo en `BZBusService` (`m_AtStop`, el imán, `DriveTowards`) es el 1.1 y **no corre**. Si a la ruta le falta el `_wp.csv`, cae al 1.1 **con log** (`[C12] ... va por el cerebro 1.1`). ⚠ Hasta el 23/09 el 1.2 sólo se activaba con `profiles\BZ_AutoDrive\CEREBRO_12.txt` (y la cinta con `BZ12_CINTA.txt`); desde ahí los dos corren **por default** y se apagan con un archivo con texto (`CEREBRO_11.txt`, `BZ12_SIN_CINTA.txt`). Verificá `Cerebro12AlCargar` en el código que tengas adelante.

**Los fixes de control viven en el CÓDIGO, no se hornean en la toma (arquitectura clave).** Toda mejora de conducción (cruise, reverse, slope, endpoint-taper, AR_OnWay…) es lógica runtime en el código (en 1.2, en `scripts\4_World\bz12drive\`; en 1.1, en `BZBusService`), gateada por modo/geometría — NO son campos horneados en el `_hdr`/`_wp`. Consecuencia: una **conversión header-swap** a otro vehículo (mismos waypoints, otro `VehicleClass`+`Fingerprint`) **hereda TODOS los fixes automáticamente y per-física** (leen wheelbase/maxSteer/R_min del `Fingerprint` + la geometría de los wps en vivo). *(Esto explica por qué el controlador generaliza con una sola config. **Postura 1.2:** una grabación es de SU vehículo —trae su fingerprint y alimenta el envelope de SU clase—; el header-swap queda como mecanismo técnico/legacy, **no** como flujo. Para la misma ruta en otro vehículo: **grabala con ese vehículo**. El editor de trayectorias se descartó el 23/09.)* Por eso el K-turn generalizó a r32/Camaro sin re-tunear, y una toma VIEJA **mejora sola** al correr sobre un PBO nuevo (la data `_wp` no cambia; mejora el código que la interpreta). Único matiz: los GAINS por-ruta que el conversor NO escribe (ej. `ReverseFFWeight`/`ReverseStanleyK`) caen a los defaults del código — si un vehículo puntual necesita otro gain, es 1 línea en su `_hdr` (sin rebuild).

---

## 5. El cerebro 1.2 — cómo maneja (internals + parámetros)

**Dónde:** `scripts\4_World\bz12drive\` — 21 archivos, ~17.000 líneas. **Lo que describe §5-L (Stanley, `DriveTowards`, `m_AtStop`, el imán) es el cerebro 1.1**: no corre cuando manda el 1.2. Antes de responder algo sobre el manejo, confirmá cuál corre (§2, *Abierto*).

### 5.1 La arquitectura en una figura
```
BZVehicleDataReader  --(una vez, al spawn)-->  BZVehicleManual  (read-only, UNA verdad por vehiculo)
       SENSOR: uno de los DOS unicos que tocan el Car          |
                                                                v
                BZDriveController  = el ORQUESTADOR (lo unico que el runner ve del cerebro)
                  |   Plan()   a  2 Hz  -> decide el MODO y los objetivos   (Tick12 en BZBusService)
                  |   Sample() a 40 Hz  -> cinta, AR, pedales, aplicacion   (SampleDrive12, desde CarScript)
                  v
   BZModeArbiter -> quien maneja este frame        BZLegMachine -> el cursor (UNICO duenio), avance por ARCO
   BZCintaRunner -> reproduce la toma              BZPurePursuit -> trayectoria (funcion pura, sin estado)
   BZDerivationEngine -> objetivos -> pedales      BZManeuverController / BZEndpointSlam -> modulos POSE
   BZAutoRecovery12 -> re-arranque en el lugar     BZEnvelopeStore/Recorder/Source -> lo MEDIDO
                  |
                  v
BZBusInputApplier  --(el otro unico que toca el Car)-->  SetSteering / SetThrottle / SetBrake / ShiftTo
```
**Principio de diseño:** ningún módulo llama a otro ni le lee el estado; el Controller los compone. Cada uno tiene **una** responsabilidad y es testeable sin juego (hay bancos en Python que reproducen línea por línea los módulos — "espejos").

### 5.2 Los modos (`BZDriveMode`, en `BZDriveTypes.c`)
```
0 INVALID · 1 IDLE · 2 CRUISE · 3 APPROACH · 4 MANEUVER · 5 ENDPOINT · 6 HALTED
```
Los logs imprimen el número (`modo 2` = CRUISE). **La tabla del árbitro es TOTAL:** todo par (estado, condición) que no matchea cae en un fallback explícito (STAY o SAFE_STOP). En 1.1 el modo de falla era un hook que **no decidía**; acá no hay huecos.

### 5.3 La cinta (`BZCintaRunner`) — el núcleo de 1.2
- Reproduce **el input que el humano tenía EN ESTE PUNTO del camino**: rueda lograda + gas + freno + **marcha**, y deja que la física haga el resto (estilo chofer nativo de Arma).
- **Es espacial, no temporal:** el índice es el **arco**. Se consulta por proyección de la posición del vehículo con un **cursor monótono con cota de pasos** (un cursor sin cota se colgaba con trayectorias divergentes). Los 40 Hz de captura son sólo la resolución.
- **"La cinta posee":** latch `m_CintaRodo`. ⚠ **Desde el 938 sólo se arma con una cinta válida.** Antes se armaba sin cinta y el enganche de meseta hacía `m_Cinta.EnMeseta()` sobre null → `NULL pointer` a 40 Hz → `Sample` moría → input congelado (§12.0).
- **Meseta grabada:** donde el humano **se detuvo**, la cinta sostiene la detención. Una detención impuesta (un evento de parada) también se sostiene. **El tiempo detenido del humano NO se reproduce solo**: esperas y paradas se ponen con eventos.
- **Sin cinta** (tomas viejas, o el harness de banco, que no la carga) el framework maneja igual, como seguidor de trayectoria. La reversa sin cinta es reactiva y es lo más frágil.

### 5.4 Trayectoria: `BZPurePursuit` + `BZLegMachine` + `BZRouteData`
- **Pure-pursuit** apunta a un punto a distancia Ld adelante sobre la traza; **Ld sale de la velocidad** (es un parámetro de control, no una constante por vehículo), con un **piso geométrico ~1,5 × batalla**. Produce un objetivo (velocidad, decel, curvatura) y **no toca pedales**. **No tiene estado**: es la pieza que mató "cruza tramos / se va al descampado".
- **`BZLegMachine`** es el **único dueño del cursor**, avanza **por arco** y no retrocede salvo `SeekTo()` explícito. (En 1.1 cada módulo re-escaneaba con un nearest global, que miente en un bucle en 8 o en un spawn perpendicular.)
- **`BZRouteData`** precomputa arco, rumbo, curvatura y legs una sola vez. **Legs:** arranca uno nuevo por `legBreak` o por **cambio de sentido de marcha** (meter la marcha ya es una declaración).

### 5.5 De objetivos a pedales: `BZDerivationEngine`
(velocidad, decel, curvatura) → (gas, freno, marcha, volante) **para ESTE vehículo**. Con `hasInputData = 0` los pedales grabados se descartan y se re-derivan: la fidelidad depende de este archivo. La idea central: **partir de lo que el vehículo hace solo** (creep, coast, freno medido) y pedir sólo la diferencia.

### 5.6 El manual del vehículo y la procedencia (`BZVehicleDataReader` → `BZVehicleManual`)
- *"Derivá lo que puedas, MEDÍ lo que no."* Cada campo lleva procedencia: **CONFIG** (leído tal cual) · **DERIVED** (fórmula desde el config: un **prior**, no una verdad) · **MEASURED** (del envelope) · **FALLBACK** (prior de emergencia, **logueado fuerte**: un fallback callado es un número mágico disfrazado).
- **Lo medido manda sobre lo derivado. Nunca al revés.**
- ⚠ **La masa es la del CONFIG, no la del vehículo real** (el Tiger pesa 2.000 kg para el motor, no 57 t). El log la imprime: `[Manual] masa=...`.
- **Creep vs quieto:** `creepKmh` es un **objetivo** de arrastre (lo usan los módulos POSE); `idleCreepKmh` es la **medición** (0 en toda la flota). Todo lo que pregunta "¿está quieto?" lee la medición; `UmbralCasiParado()` tiene piso 0,5 km/h.

### 5.7 El piso de despegue (`launchThrottleFloor`)
**Qué es:** el gas mínimo con el que el vehículo rompe el reposo en llano.
**Cómo lo usa el control:** `RampaDeArranque` arranca **desde el piso** y sube `+0,10` por tick de Plan (0,5 s). **Se desarma apenas la velocidad cruza `UmbralCasiParado()`** y el módulo recupera su gas. Por eso el piso sólo pone **el primer tick**: piso alto arranca antes, piso bajo tarda un tick más. El log dice de dónde salió: `ARRANQUE: ... rampa desde el piso 0.15 (medido)` / `(prior de flota)` / `(FORZADO por banco)`.
**De dónde sale:**
```
calibrado     el MEDIDO de esa clase (lineas lcal del envelope)
sin calibrar  LAUNCH_PRIOR_FLOTA = 0,13  (mediana de las clases calibradas; en BZVehicleManual)
el derivado   se imprime "(informativo, no decide)": usa el par PICO y el arranque es a ralenti
              -> razon medido/derivado 0,6 a 7,5 en 9 clases: NO sirve ni escalado
```
**Medido (septiembre 2026):**
```
CivilianSedan 0,05 · Harley 0,07 · PAZ_Yellow 0,11 · Tiger 0,13 · ChernoBus 0,15 · ChernoBus_02 0,15
ApocalypseTruck 0,17 · Truck_01_Covered 0,25        (AEC_Matador: descartado, distribucion 14x de ancha)
```
**Por qué NO sale de las tomas (dos razones medidas):** con teclado, **1.778 de 1.873** despegues quedan grabados con el gas a fondo (la tecla es binaria); y el tramo desde parado es un **transitorio del motor** (§12.0). **Medir con rampa no es la mejor vía: es la única.**
**Cómo se calibra (banco, paso a paso).** La calibración vive en el **harness de banco** (`BZ12TestDrive`, mod `BZ_AutoDrive12`), que **no se publica**: en la 1.2 sólo la corre quien tenga ese mod.
1. Con el mod de banco cargado, escribí en `profiles\BZ_AutoDrive\BZ12_CALIBRATE.txt` el **nombre de una ruta** del vehículo a calibrar (primera línea; un archivo **vacío** se ignora con aviso). Conviene una ruta que **arranque en llano** (|pitch| ≤ 2,2°) y tenga **rectas largas** (≥ 45 m sin fin de tramo) donde llegar a 45 km/h.
2. Arrancá el server: el harness spawnea el vehículo de la ruta en su wp 0, espera que **asiente** (4/4 ruedas y quieto) y anuncia `[BZ12Drive] MODO CALIBRACION: ...`.
3. **Fase 1 — el piso de despegue** (con el AR suspendido): **9 despegues** desde quieto en llano, en 1ra; antes de cada uno, 1 s frenado; gas desde 0 subiendo `+0,02` cada 0,5 s; la muestra es el gas del primer tick de un cruce **sostenido 4 ticks** de 0,05 m/s. La **mediana** va al envelope como líneas `lcal`, con la firma de la corrida `lcalrun,<pitch#1>|<gases>`. Log: `LAUNCH cal: empieza` · `LAUNCH cal #n: despega con gas X` · `LAUNCH cal TERMINADA: piso X (mediana de N, M corridas)` · `[Envelope] guardado tras la calibracion de launch.` Salidas anormales: `LAUNCH cal OMITIDA: pendiente ...` (no estaba en llano: usá otra ruta) · `LAUNCH cal ABORTADA: no despega ni con gas 1,0` · `LAUNCH cal DUPLICADA` (mismo spawn ⇒ **misma física bit a bit**: esa corrida no suma; para muestras nuevas, calibrá desde otro punto; la igualdad es por tolerancia de pitch 0,005°, no por redondeo).
4. **Fase 2 — el banco de 8 ciclos** (`BANCO de creep/despegue: 8 ciclos antes de la ruta`): frenar hasta parar → **soltar todo** (mide el **creep**; la velocidad cruda se registra sin compuertas, para distinguir "no repta" de "repta y algo lo rechaza") → frenar → rampa de gas fina desde parado → **barrido de full-lock** con freno (tope de volante a cada lado: la **capacidad** `steerMax`, que no puede depender de por dónde se manejó).
5. **Fase 3 — la ruta, con ciclos forzados.** En las rectas (modo CRUISE con ≥ 45 m por delante) fuerza: **a fondo hasta 45 km/h** (`aMax`) → **planeo hasta 8** (`coast2`) → **gas parcial 0,35 hasta 25** (relación de marcha) → planeo → **asentamiento** de 260 ticks (el creep es un equilibrio, no un punto por el que se pasa). Y la **escalera de volante**: 10 escalones de 1 s, simétricos a cada lado, por encima de 12 km/h. De ahí sale la transferencia comando → ángulo (`stx2`), que con teclado **no se puede medir** (la tecla es binaria).
6. Al terminar, o al cortar la corrida, **se guarda el envelope de la clase**. Sólo la calibración escribe el envelope desde el juego; una corrida de manejo imprime `[Envelope] NO guardado (corrida de manejo)`.

**Banderas de banco** (sólo harness): `BZ12_FORCE_LAUNCH.txt` (0,01-0,99: fuerza el piso en esa corrida, para contrafactuales) · `BZ12_FORCE_GEAR.txt` · `BZ12_FORCE_LDFLOOR.txt` · `BZ12_FF_WINDOW.txt` · `BZ12_FORCE_PURSUIT_REV.txt` · `BZ12_SPEED_CAP.txt`.
⚠ **Si un USUARIO te pide calibrar:** en la 1.2 publicada **no puede**, porque el disparador vive en el harness. Todo vehículo de un suscriptor corre con `LAUNCH_PRIOR_FLOTA` (0,13); el manual no ofrece calibración y queda para una 1.2.1. No le digas que calibre.
- **Qué ganaría calibrando:** que el número sea el de SU vehículo. **No cambia cómo maneja en ruta** (un tick de rampa).
- **Dónde debería importar:** en el **arranque en pendiente**, **todavía no medido**.
- **Lo que sí aprende sin calibrar:** el resto del envelope (curvatura, frenada, planeo, creep) sale **de sus tomas** al convertir (§5.10).

### 5.8 Maniobra, cusp y endpoint (módulos POSE)
- **`BZManeuverController`** y **`BZEndpointSlam`** trabajan a ~0-2 km/h contra un punto, donde la curvatura la domina el **error de pose**, no la forma del camino. Están **exentos de la DerivationEngine** pero **no del Manual** (decel, creep y ganancia salen de ahí).
- **Cusp (reversa ↔ avance):** cada cambio de sentido abre un leg. **Fix 936:** fuera de la `CreepZone` (1,5 × batalla) el remanente que usa la maniobra es el **arco**, no la proyección firmada `signedToEndM` (que lejos del final mentía: −34 m con 4.737 m por delante ⇒ gas 0 / freno 1 durante 7 s).
- **Regla del corte, sigue vigente:** los ángulos de volante de la reversa son **del vehículo, no del camino**. Grabá el cambio de sentido **en recta**.
- **Endpoint:** tolerancia única `BZDriveTol.LLEGADA_M = 0,10 m`. **"No se mueve para aprobar una corrida. Lo que se mueve es el vehículo."**

### 5.9 AutoRecovery v2 (`BZAutoRecovery12`)
**Re-arranque en el lugar** para trabas no físicas: el vehículo **se queda donde está y la toma retoma** — no teleport (decisión de diseño). El AR v1.1 teleportaba; en 1.2 ese camino no corre.
**Detección (la velocidad leída NO es movimiento: un vehículo que empuja algo lee 0,3-0,7 km/h):** CLAVADO (a fondo sin desplazamiento; se resetea dentro de una meseta grabada: una parada grabada no es una clavada) · **traba por progreso** (`DetectarTrabaPorProgreso`: ≥ 10 s sin avance del cursor con gas pedido ≥ 0,95) · **SAFE_STOP del árbitro** (20 s sin avance) → **abre** la escalera; con un escalón en curso (F1 activa / re-ubicación pendiente) el árbitro se reinicia y re-decide el modo, nunca STUCK · el **AHOGO** (C1: primera + hillstart) cede a la escalera con el veredicto "avanzó y se volvió a clavar" o si en una ventana de 10 s no avanzó lo que se rueda a `CINTA_RODANDO_KMH`; con la escalera abierta un AHOGO no arranca C1.
**Escalera física:** 1 = F1 **con sentido** (se aleja 3 m al revés del leg: reversa en un leg normal, adelante en un leg de reversa; antes de moverse sondea desde el paragolpes **cualquier objeto con colisión** —vehículos, garitas, postes, guardrails, árboles, rocas, paredes; no cuentan personas, ítems, infectados, animales ni arbustos— y si hay algo salta al escalón 2 sin gastarlo: `escalon 1 rechazado: obstaculo <clase>`) · 2 = re-ubicar una batalla atrás (pose de la cinta) · 3 = zonas seguras +N m; 2 y 3 valen también en legs de reversa. **STUCK sólo con la escalera agotada o desactivada**, con `MotivoStuck()`. El aviso `BLOQUEADO` sale de la TRANSICIÓN cerrada→abierta (una sola fuente, cualquier camino que la abra). El heartbeat muestra `AR=N (xL+yF)`: rescates **lógicos** y **físicos** del cerebro que está manejando. *(En versiones anteriores ese campo mostraba el contador de teleports del 1.1 y marcaba 0 mientras el AR v2 hacía 29 maniobras; si leés un RPT viejo, contá líneas `[AR]`.)*

### 5.10 El envelope (`BZEnvelopeStore` / `BZEnvelopeRecorder` / `BZEnvelopeSource`)
- **Archivo:** `<RoutesDir>\envelope_<CLASE>.csv` — **uno por clase de vehículo**, lo comparten todas las rutas de esa clase. Lo escribe el **wizard** (`envelope_emit.py`) desde cada toma que convertís, **sumando** (fusiona) — no pisa, salvo `--pisar`. La calibración de banco agrega `lcal`/`lcalrun`. **En juego el motor lo LEE; no lo aprende corriendo.**
- **Campos:** `scalars` (radio de rueda, batalla) · `fp` (fingerprint: no mezcla clases) · `medido` (procedencia: cuántas tomas y cuadros) · `k` (curvatura por ángulo de volante) · `ratio` (transferencia por marcha) · `stx2` (comando → ángulo de rueda) · `shist` · `ahist` (aceleración) · `coast2` (decel en punto muerto por superficie: **de acá sale el creep**) · `chist2` / `nocreep` · `lhist` (gas al despegar **durante el manejo**: posición de rampa, **no** es el piso) · `lcal` / `lcalrun` (el piso **medido**).
- **Un envelope es de UNA clase.** Dos libreas distintas son dos clases. *"La única manera de llenar un envelope es con la misma clase."*
- **Toda la dificultad está en las compuertas**: cada magnitud vale sólo bajo condiciones (`r = v/ω` exige rodadura limpia, sin gas ni freno). Una medición sucia es **peor** que ninguna, porque lo medido manda.
- **El `Save()` conserva lo que no entiende** (desde el 920) y `envelope_emit.py` también: un archivo que otro escribe no se recorta.
- **Precedencia al cargar (`Configurar12` paso 0):** el conversor copia `envelope_<clase>.csv` en `hdr["Envelope"]` (+ `EnvelopeClass`). Al arrancar **manda la ficha de la CLASE** (acumula); la copia del hdr se usa sólo si no existe el archivo de la clase o si la clase midió MENOS (`MuestrasTotales()`: suma de las cuentas de todos los histogramas; empate → clase). Línea `[Envelope] cargado de la clase ... · el hdr trae otra copia ...: no se usa` / `sin archivo de la clase: cargado del hdr` / `la ficha de la clase ... es mas pobre ...: se usa el hdr`. El **creep** es distinto: el del hdr manda (es la medición de ESA toma).
- **Cómo se genera, se fusiona y se inspecciona:** en cada conversión el wizard corre `envelope_emit.py <frame.csv> --out <RoutesDir>`.
  - **Fusión:** si ya existe la ficha de esa clase con la misma **huella estable** (`Mass`, `GearsCount`, `MaxSteeringAngle`), **suma** los bins y la consola dice cuánto creció. Una toma que ya figura en `medido` no se suma dos veces.
  - **Si la huella cambió** (actualización del mod del vehículo), sale con **código 3** y el wizard pregunta **`[R] regenerar / [S] dejar como esta`**. `R` = `--pisar`: la ficha vuelve a empezar desde ESTA toma.
  - **A mano:** `--dir <carpeta> --clase X` (todas las tomas de una clase) · `--fusionar a.csv b.csv --out ...` (unir dos fichas; `--forzar` si las huellas difieren).
  - **Una toma sin filas útiles** sale con código 2 **sin escribir nada**: una ficha vacía es peor que ninguna.
  - **Ficha "stub":** la que casi no tiene bins; en septiembre de 2026 lo eran 18 de 38. Se reconoce por la línea `medido` (tomas y cuadros) y por los histogramas vacíos. Una ruta con ficha stub corre, pero con los priors del config. **La cura es convertir más tomas de esa clase, y variadas** (curvas, frenadas, velocidades): la riqueza es la variedad, no el largo.

### 5.11 Harness de banco vs. runner
El **runner** es `BZBusService` (lo que corre en un servidor normal, con cinta). El **harness** es `BZ12TestDrive`, del mod `BZ_AutoDrive12` — **no se publica**; es el banco de pruebas. Diferencias que importan al leer un RPT de banco: **el harness no carga cinta**; se maneja con banderas en `profiles\BZ_AutoDrive\`: `BZ12_DRIVE.txt` (ruta a manejar), `BZ12_CALIBRATE.txt` (ruta a calibrar; **se mira el contenido** desde el 930: vacío = ignorado con aviso), `BZ12_FORCE_LAUNCH.txt` (fuerza el piso, sólo banco).
**En el runner** (lo que corre un servidor normal) hay cuatro banderas de **apagado**, todas por contenido: `CEREBRO_11.txt` (usar el cerebro 1.1), `BZ12_SIN_CINTA.txt` (correr sin cinta), `BZ12_SIN_FF_PRIMARIO.txt` (volante sin FF-primario) y `REPRODUCTOR_LANZA_HARNESS.txt` (el Reproductor manda la ruta al harness; sólo sirve con el mod de banco instalado). La única de presencia que queda es `BZ12_HEAD_TANGENTE.txt` (convención vieja de rumbo; avisa con ⚠ en el log). Si ves en el log `[C12] el cerebro 1.2, la cinta y el FF-primario son el DEFAULT ...: sobran ...`, son banderas viejas de encendido que ya no hacen falta.

---

## 5-L. El cerebro 1.1 — LEGACY (Stanley + modelo inverso de `BZBusService`)

> ⚠ **Esto describe el cerebro 1.1**, el que corre sólo si el 1.2 no toma la ruta (falta el `_wp.csv`, o el 1.1 se pidió explícitamente — ver §2). Se conserva porque hay tomas y servidores de 1.1, y porque muchas validaciones de acá (endpoint, reverse, AR_OnWay) se heredaron al 1.2. **No lo uses para explicar cómo maneja 1.2.**


**Stanley lateral.** `targetYaw = segHeading − atan2(K·lateralOffset, v)`, K=1.0 default. El `atan2(K·off, v)` atenúa por velocidad (fuerte lento, suave rápido) → mata el zigzag. **Gotcha del signo:** left-handed DayZ → `cross = AB.z·AP.x − AB.x·AP.z` (invertido = divergencia). **Lección:** modular K por curvatura local EMPEORA.
- Params: `SteeringScale` (-1=auto, derivado del wheelbase), `CurvatureSteerBoost`, `PathSmoothWindow` (0 recomendado en curvas cerradas; >0 corta curvas — bug histórico del divisor hardcodeado).

**Corredor / "paredón".** Banda muerta (semi-ancho de carril): dentro Stanley OFF (no perturba), fuera ON + damping. Params: `CruiseLateralDeadband` (~0.5), `CruiseLateralKGain` (1.0), `CruiseLateralDamp` (~0.3), `CruiseLateralCenterOffset` (bias lateral; +derecha/−izquierda; ~25 m de mediana por unidad — resolución 0.01–0.02). **Aplicar CenterOffset DESPUÉS del deadband** (bug FBC8571F: estaba sujeto al deadband).

**Feedforward.** Curvatura con lookahead (~20 m cruise, 1–3 m parking). Param `CruiseFFWeight` (-1→0.25). `CurveThrottleEnabled` corta throttle ANTES de curva cerrada.

**Modelo inverso (`BZInverseModel.c`).** PID velocidad: `targetSpeed → throttle/brake`. `UseInverseModel`, `InverseModelKp/Ki/Kd` (-1=default 0.4/0.05/x), `InverseModelLowRpmMin` (rpmMin×1.0 vs ×1.3; default true post Test C: gear amortiguado, menos lugging), `TargetSpeedSmoothWindow` (suaviza el target, menos varianza PID), `AccelShiftThreshold` (anti-catapulta). Freno predictivo: `aNeeded = u²/(2·dist) ± g·sin(pitch)`.

**Gear.** `GearStrategy = "auto_box"` (AT por RPM) o `"follow_recording"` (gear grabado; default recomendado para no-bus; sport cars patinan en 1ra con auto_box). `MaxGear` (FIRST=2…SIXTH=7). El InverseModel respeta follow_recording vía override de SelectGear.

**Pendiente.** `SlopeCompensationEnabled`, `SlopeLookaheadWps` (5), `SlopeGain`, `SlopeLateralGain`. El recording ya tiene horneado el terreno → seguirlo hereda la compensación.

**Luces + bocina (Fase 2 replay, cerebro 1.1 — ⚠ en 1.2 NO: la toma `frame_` no graba bocina ni luces y `frame_to_route.py` escribe 0 en las columnas 16-17; el default de `HornMode` pasó a `off` y `"replay"` sólo avisa al cargar. En 1.2: `HornMode` `off` (default), `stops` (al terminar cada parada) o `finish` (al `ROUTE_COMPLETE`); `LightsMode` `auto` (default), `on`, `off`, `auto_inverted`; en un punto exacto, los verbos `horn` / `lights_on` / `lights_off` en un evento).** El PathLogger captura del humano las columnas `horn`/`lights` (vía `CarScript.Cast(parent)`); cada waypoint guarda `targetHorn`/`targetLights` (en `BZWaypoint`), y Boris las reproduce por waypoint (**replay espacial**: honkea/prende donde lo hizo el humano). Config por ruta: `HornMode` (`replay`/`stops`/`finish`/`off`) — el claxon GRABADO siempre se reproduce; `HornMode` controla solo el bocinazo AUTOMÁTICO; `LightsMode` (`replay`/`auto`/`auto_inverted`/`on`/`off`). **Bocina:** `CarScript.SetCarHornState(int)`, enum `ECarHornState{OFF=0,SHORT=1,LONG=2}`, funciona andando y **sincroniza al observador** porque `m_CarHornState` es `RegisterNetSyncVariableInt` + `SetSynchDirty`. **Luces:** requieren batería energizada (`OnBeforeLightOn()` exige `GetCompEM().GetEnergy()>0`; el fix en `EquipBus` hace `batt.GetCompEM().SetEnergy(GetEnergyMax())`). El **gotcha de red** de las luces en un AI car observado (y su fix v3 con NetSync propia) está en §12 — es de lectura obligatoria al tocar cualquier estado VISUAL de un AI car.

**AutoRecovery (clásico, stuck-based).** `AutoRecoveryEnabled` (default false), `AutoRecoveryStuckTimeS` (5–10), `AutoRecoveryAdvanceWps` (3–5), `AutoRecoveryCooldownS` (8), `AutoRecoveryMaxPerMission` (0=ilimitado). Teleporta a wp+N si Boris se traba (velocidad~0, o wp_idx sin avanzar >N s aun girando); preserva velocidad por impulso; loguea heatmap diagnóstico geográfico. **Filosofía "no salvar a Boris de Boris":** sobre terreno LIMPIO Boris es preciso → si se traba en camino limpio es bug de GRABACIÓN (taparlo lo esconde → re-grabar). Por eso queda default OFF.

**AR_OnWay (escudo contra obstáculos EXTERNOS del mundo — validado 100% en 5 vehículos, 2026-07-01).** Distinto del AR clásico: cuida a Boris del **MUNDO** (otro vehículo detenido, o uno que lo choca/empuja), no de su propio control. Dos flags **independientes**:
- **`ObstacleSlow`** = freno predictivo. Escanea el path adelante con **lookahead VARIABLE por velocidad + física del vehículo**: `scanMax = ObstacleStopDist + v²/(2a) + v·0.6 + 5`, con `a = min(ObstacleDecel, decel real del vehículo)` (`BZInverseModel.GetMaxBrakeDecel` del config, acotado por fricción·g) → una camioneta pesada de frenos flojos **mira de más lejos y frena a su máximo real, sola**. Frena hasta detenerse a `ObstacleStopDist`≈15 m. **Corredor lateral** `ObstacleCorridorHalf`≈2.3 m: mide el offset del obstáculo al eje del carril → un auto en la **banquina** o en el carril contrario NO frena a Boris (solo lo que le tapa el carril). También querea el **frente REAL de Boris** (no solo los waypoints grabados) para no perder un auto pegado/empujado.
- **`ObstacleEscape`** = teleport al primer wp **limpio** pasado el obstáculo, si persiste (>`ObstacleEscapeWaitS`≈6 s frenado) o si lo **empujan/chocan**. Gatillos robustos al flicker del scan: `owPersist` (frenado + visto hace poco), `owPushing` (quiere avanzar, target>15, pero kmh<10 sostenido tras ver obstáculo), nivel "tocando" (<5 m → 2 s). Resume suave a `ObstacleEscapeResumeKmh`≈10.

Config: `ObstacleSlow`/`ObstacleEscape` (bool, default false), `ObstacleScanDist`(50, **piso**), `ObstacleStopDist`(15), `ObstacleDecel`(4.5), `ObstacleCorridorHalf`(2.3), `ObstacleEscapeWaitS`(6), `ObstacleEscapeResumeKmh`(10). **Perfiles (toggle — la clave, runtime-settable por el quest):** **Transporte robusto** (ambos ON) / **Interceptable** (Slow ON + Escape **OFF** → Boris frena lindo ante el que lo bloquea y NO se escapa → la misión de interceptación funciona; si escapara la rompería) / **Ninguno** (ambos OFF = réplica pura). Gate `(ObstacleSlow||ObstacleEscape) && UseInverseModel` (el template ya trae `UseInverseModel=true` → engancha). El **import v1 pregunta el perfil** (**[R]**obusto / **[I]**nterceptable / **[N]**inguno → setea `ObstacleSlow`/`ObstacleEscape` en el `_hdr`); en una toma nueva se setean por config. **Borde abierto:** un choque **de frente** que empuja a Boris ~3 m off-path puede clavarlo (es territorio de **recuperación off-path**, distinto de "obstáculo adelante en el carril" — "casi cerrado"). Ver §15.

**Estrategia de velocidad — combos de flags (AVANZADO; ya NO es una elección del usuario).** El wizard produce **un solo control**: sigue la línea + la velocidad grabada (pure-pursuit + modelo inverso; `FollowPath=false` + template). Los viejos **"modos 1/2/3"** eran combos de estos flags y quedaron **unificados en ese control único**; siguen accesibles a mano en el `_hdr.json` para un modder que quiera otro comportamiento: **replay puro** (`FollowPath`/`FollowPathUseReference`/`UseInverseModel`=false; mismo vehículo, `hasInputData=1`), **geometría pura** (`FollowPath`+`UseInverseModel`=true; velocidad por curvatura+tope, pesados), **reference-assisted** (los tres true; velocidad grabada + steering por config = el generalizador). NO es un ranking.
**Maniobras con corte — comportamiento de los flags (avanzado/legacy, 2026-07-03):** `isM3approach = (UseInverseModel && !FollowPathCapByRecording)` — el **auto-approach es M3-ONLY**. Apunta a la entrada del parking como crawl (target ~5). Si el corte cae en zona rápida (recta ~23 km/h) **sobre-frena** → el vehículo se planta en la transición (caso real: Sedan M3-recta clavado en `wp2289`, on-path, spd 0). **M2** (`FollowPathCapByRecording=true`) cruza a la velocidad GRABADA → robusto y generaliza mejor. **Para maniobras con corte: preferir M2.**

**Controladores de maniobra:** **parking** (FF alto, lookahead corto, anti-rollback) — VIVO. **Es DIRECT-REPLAY (open-loop, 2026-07-03):** los wps tagged `parking` NO corren el lazo Stanley — el framework reproduce los ángulos de volante + pedales EXACTOS grabados (`targetSteering/Throttle/Brake`). Premisa **"arrive ready"**: llegar al tramo en la pose+velocidad demostrada. *(El toggle de grabación `mode="parking"` con NUMPAD+ se **sacó del menú de teclas** — parking/maniobra son **legacy**; en tomas nuevas no se marcan. El código honra las branches para tomas viejas.)* **Corolario (regla del corte):** como los ángulos son VEHICLE-SPECIFIC (mismo ángulo → radio ∝ wheelbase), el direct-replay **no generaliza en curva**. Para que otro vehículo complete (cross-vehículo: **grabar con ese vehículo**; el header-swap es legacy): el CORTE al bloque de maniobra debe caer donde la trayectoria es **RECTA** (heading plano) — así la curva queda en **closed-loop Stanley** (vehicle-agnostic) y solo la recta va en replay. Detección del arranque de recta: **primera muestra post-maniobra con heading plano (<1.5° en 20 samples)**. Cortar en curva → el Sedan (wb 2.935) chocó; cortar en recta → completó (parking 0.71→1.11 m). Ver §2 (regla del corte). **`ModeEntrySnapEnabled` ahora `false` por default (era true):** el teleport de alineación en cambios de modo se apagó — el control cerrado (parking + reverse rear-steer, heading <1°) posiciona solo; el snap solo disparaba a <0.5 m. Reactivable por `_hdr`. **reverse** — VIVO y **validado** (§2, 4 vehículos). Modelo bicycle **rear-steer**: control point al **eje trasero** (`GetReverseControlOffset`≈wheelbase/2 — ancla también el FF, no solo el corredor), signo Stanley invertido, `ShiftTo(0)`, sigue volante grabado sobre `ReverseRecordedSteerThreshold`≈0.2. **Autoridad plena de volante:** el `SteeringScale` (anti-sobre-rotación de forward, ≈wheelbase/5.5) **NO** se aplica en reverse — le cortaba el volante a la mitad e impedía forzar el arco. **Anticipación ∝ R_min:** `floorFf = R_min·1.3` (R_min = wheelbase·cos/sin(maxSteer); `Math.Tan` no garantizado en Enforce) — giro ancho anticipa antes. **Velocidad = min(grabada, física):** `vPhysRev = MAX_REVERSE − (MAX_REVERSE−REV_PHYS_MIN)·|ffRev|` (MAX=`GetReverseTargetSpeedCap`≈25, MIN=5) → generaliza per-vehículo. **Endpoint-taper terrain-aware:** dentro de `REV_TAPER_M`(8 m) del fin del bloque reverse (medido por distancia de path recorriendo los wps) rampa el target a un floor que **sube con el grade** (`floor = MIN_PROG + grade·36` si grade>0.03; grade = dy/dist a la Y del endpoint) → **plano** frena a 3 km/h (mata la sobre-pasada de 16 m), **cuesta arriba** el floor supera el target grabado y el taper no dispara (self-guard, preserva la trepada). **Endpoint autoadaptativo (parada final) = RESUELTO (2026-08-11):** el endpoint-taper + el freno autoadaptativo por vehículo+superficie llevan la parada a **por debajo de 0.5 m en la mayoría** (hasta ~1 m en los grandes), forward y reversa, out-of-sample (Sedan en reversa 0.47 m, GT2RS en reversa 0.33 m; listado completo en §2). El endpoint tras curva sigue la velocidad grabada y sólo frena en los últimos ~3 m (SEQ1 44 km/h → 0.34 m). **Transición forward→reverse** honra freno-a-0 + handbrake + gear 0 grabados; la **salida** reverse→forward la resuelve el *handbrake-resume* (salta el cluster de handbrake-forward y retoma forward — antes se clavaba en gear 0 + brake 1). **approach** — VIVO: no es un controlador de steering aparte (usa el Stanley normal) sino un **tag de velocidad** que rampa el freno ANTES de entrar a una maniobra — hoy **automático** (`ApproachAuto`; ya **no** se marca con tecla) para no clavar el frenazo y derrapar. **maniobra** — **deprecado para grabaciones NUEVAS** (2026-06-17: fuera de UI/hotkeys), PERO las branches siguen en el código y honran tomas viejas que la tengan (ej. el K-turn EXAMPLE18). **Knobs de reverse (por `_hdr`, sin rebuild):** `ReverseFFWeight` (default 0.6=parking), `ReverseStanleyK` (default 0.8), `ReverseTargetSpeedCap` (25), `ReverseStanleyMinSpeed` (2 m/s, rompe la espiral 1/v). **Todos los fixes de reverse están gateados a `mode=='reverse'` / `isReversePk`** — no tocan cruise ni maniobra (regla de aislamiento).

---

## 6. Config de ruta (`BZBusRoute.json`) — referencia

Clase: `BZBusRouteConfig` en `BZBusConfig.c`. Defaults sensatos; `-1`/`0` = default del código / off. Grupos: **Básicos** (`VehicleClass`, `DriverClass`=eAI_SurvivorM_Boris, `RespawnDelay`=300, `SpawnHoldSeconds` [0=al toque, 30+=esperar trigger misión], `VehicleInvincible`, `VehiclePartsLocked` (vehículo inviolable, default `true`), `MaxGear`, `Attachments`, `Wheelbase`); **Velocidad/modo** (§5); **Dirección** (§5); **Pendientes** (§5); **AutoRecovery** (§5); **Convoy/Quest** (`ConvoyMode` ""/"flee_on_kill"/"ambush_on_damage", `Crew[]`, `Events[]`); **Maniobras** (parking/reverse params, §5). **Waypoints[]:** `pos[x,y,z]`, `targetSpeed`, `targetGear`, `mode` (normal/parking/maniobra/reverse), `isStop`/`stopDuration`/`stopRadius`, `targetThrottle/Brake/Steering`, `hasInputData`, `name`. Embebe un `Fingerprint` (del header del recording). Para la lista exacta de campos: Apéndice A del manual.

**Attachments: NUNCA adivinar** (regla no-negociable). Vienen del fingerprint del recording (NUMPAD 5 los captura) o del trader package/config. Al cambiar `VehicleClass`, REEMPLAZAR attachments (el converter preserva los del JSON anterior = bug recurrente). El header del recording es la fuente autoritativa (el conversor ya lo prefiere).

---

## 7. El wizard + la pipeline + dónde viven los datos

### 7.0 — Lo que produce la conversión en 1.2 (LEER PRIMERO; manda sobre lo que siga en §7)

La cadena real: **`Wizard.bat` → `route_wizard.ps1` → `frame_to_route.py`** (la ruta) **+ `envelope_emit.py`** (el envelope). El wizard trae **Python embebido** (`tools\python\`): no depende del Python del sistema.

**Cuatro archivos de la ruta**, en `RoutesDir` (el `…\profiles\BZ_AutoDrive\` del server):
```
BZBusRoute_<NOMBRE>.json          el MASTER: waypoints + config + eventos, legible. Es el CATALOGO:
                                  el nombre que aparece en el Reproductor y el que se pide al lanzar.
BZBusRoute_<NOMBRE>_hdr.json      el CARNET (~2 KB): VehicleClass, Fingerprint, Wheelbase, MaxGear, Events,
                                  Attachments, el creep (CreepKmh/CreepSource/CreepSamples/CoastAtRest)
                                  y WpRows/CintaRows (cuantas filas tienen los otros dos).
BZBusRoute_<NOMBRE>_wp.csv        una fila por waypoint (pos, rumbo, marcha, angulo de rueda, parada, nombre).
BZBusRoute_<NOMBRE>_cinta.csv     el manejo a 40 Hz indexado por ARCO:
                                  s,x,y,z,heading,chassis_kmh,front_wheel_deg,throttle,brake,gear,handbrake,dt
```
**Más uno por vehículo:** `envelope_<CLASE>.csv`, en la misma carpeta. El nombre lleva la **clase**, no la ruta: tres rutas del mismo bus alimentan y usan la misma ficha. Cada conversión **suma** (fusiona); `--pisar` lo rehace desde cero.

⚠ **Al arrancar una ruta el servidor NO lee el master:** carga el **par compacto `_hdr.json` + `_wp.csv`** y la **cinta**, por tajadas. Si alguien edita eventos a mano, tienen que quedar bien en el `_hdr.json`, no sólo en el master.

**Por qué cuatro y no uno:** cargar medio mega de una vez dentro de un cuadro cuelga el servidor. La **cola de carga global** reparte un presupuesto por cuadro entre todas las rutas en cola; para repartir necesita saber cuánto hay, y eso lo declaran `WpRows`/`CintaRows` en el carnet. Medido: el peor cuadro es el mismo con 1, 2, 3 o 5 rutas lanzadas a la vez.

**Al mover una ruta a otro servidor:** los **cuatro** archivos **y** el envelope de su clase. Sin el envelope arranca con los valores de fábrica.

**Cross-vehículo:** una grabación es de SU vehículo (su fingerprint, y alimenta el envelope de SU clase). **La misma ruta en otro vehículo = grabala con ese vehículo.** El header-swap existe como mecanismo técnico/legacy; no lo recomiendes como flujo. El editor de trayectorias **no existe más** en el paquete.

---

**Pipeline:** grabar (NUMPAD 5 → `frame_*.csv` + `header_*.txt`) → `route_wizard.ps1` **[1] Convertir** (pide solo el NOMBRE → `frame_to_route.py` arma los cuatro archivos `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv` (y el envelope de la clase) y los deja desplegados) → correr (Reproductor, hot-load sin restart).

**El wizard (`route_wizard.ps1`, TUI) — menú actual:** **[1] Convertir · [2] Importar toma v1 (BrigadaZ Transport) · [6] Configurar paths · [Q] Salir**. Es un **conversor puro**: **NO** linterea, **NO** da "puntaje/BZ-Score", **NO** elige "modo de manejo" (hay uno solo, §5). La calidad sale de **grabar bien** (§7.G): si la toma quedó sucia —te subiste a un cordón, frenaste mal—, **re-grabá** (es gratis).
- **[1] Convertir** — elegís la **grabación** (lista por **fecha**, la más nueva arriba; la etiqueta dice de qué carpeta viene: `[Cl]` cliente, `[A]` grabaciones del server, al lado de `RoutesDir`, `[B]` segundo server) y le ponés un **NOMBRE** (→ `BZBusRoute_<nombre>.json`; así aparece en el Reproductor; Enter vacío = ruta activa default). El wizard llama `frame_to_route.py <frame> <nombre> --profile <RoutesDir>`: lee el `header_*.txt` (fingerprint), **generaliza** la línea con la física del vehículo, **auto-detecta la reversa por `gear==0`**, **auto-deriva los cortes de tramo** (`legBreak`) del cambio de gear forward↔reverse y escribe los **cuatro archivos** (`.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`) ya **desplegados** (hot-load). **No hay elección de modo** — el control es único.
- **[2] Importar toma v1** — convierte una toma de **BrigadaZ Transport v1** (JSON monolítico con `Waypoints`) al formato AutoDrive. Te pide la **identidad del vehículo** (un `header_*.txt` de cualquier grabación de ese auto —sirve una de 10 s— o un `_hdr.json` ya calibrado) + un **perfil de obstáculos** (`robusto` / `interceptable` / `ninguno` → setea `ObstacleSlow`/`ObstacleEscape`). Corre `transport_v1_to_route.py`. Ver D.11 (guía de import v1).
- **[6] Configurar paths** (`Invoke-ConfigPaths`) — re-setea `RoutesDir` + (opcional) `ServerBMirror` (Enter = mantener, `-` = borrar el mirror); guarda en `wizard_config.json`.

**PRINCIPIO del wizard:** **todo se hace dentro del wizard**; orquesta `frame_to_route.py` (que ya escribe los cuatro archivos de una; y `envelope_emit.py` el envelope) por vos. El usuario **NO edita JSON a mano ni corre `.py`/`.ps1` sueltas**. Carpetas de grabaciones **portables** (`Update-LogDirs`): ya **no** hardcodeadas — se derivan de `RoutesDir`/`ServerBMirror` (cliente vía `%LOCALAPPDATA%`). Launcher **`tools\Wizard.bat`**: doble clic, sin abrir PowerShell a mano (usa `-ExecutionPolicy Bypass` solo para esa ejecución).

**Robustez ante errores de operación:** cada opción del menú corre en su propio `try/catch`. Si una operación falla, **muestra el error y VUELVE AL MENÚ PRINCIPAL** — el wizard ya **no se cierra** (solo sale por **[Q]**). Antes, un error mataba el script y, como se lanza desde `Wizard.bat`, Windows mostraba *"¿Desea terminar el trabajo por lotes (S/N)?"* — eso ya no pasa en uso normal. → Si una operación falla, el texto del error **queda visible en pantalla** (antes se lo tragaba): que el usuario lo **copie** y siga desde el menú.

**Selector de rutas/grabaciones (`Select-Route`):** **oculta los `*_hdr.json`** (la mitad "cabecera" del par de carga rápida `_hdr.json`+`_wp.csv`, que aparecían como ítems de 0 wps y confundían). La lista muestra **únicamente las rutas `.json` reales**.

**Recordatorio de "server B" condicional (portabilidad):** antes `route_split.ps1` imprimía hardcodeado "Sincronizar el par al server B también" — irrelevante para un modder con un solo server. Ahora `route_split` **ya no lo menciona**; el recordatorio aparece SOLO si el usuario tiene un segundo server configurado (`ServerBMirror` en `wizard_config.json`), y muestra el **PATH REAL**. La mayoría (un solo server) no ve nada de "server B" — es parte de que el wizard es folder-agnostic/portable.

**Dónde viven los datos (CRÍTICO, costó 1h encontrarlo):**
- Grabación humana (`path_*.csv`, NUMPAD 5) → **CLIENTE**: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- Corrida del NPC (`ai_run_*.csv` y `boris_native_*.csv`, **opt-in con los checks del Reproductor**, ya **no** con tecla) → el **SERVER que corrió** (`<server>\profiles\BZ_AutoDrive_PathLogger\`).
- Rutas → `<server>\profiles\BZ_AutoDrive\` (`BZBusRoute*.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`), y en la misma carpeta un `envelope_<clase>.csv` por vehículo (§7.0).
- `header_*.txt` (junto a cada path_*) = fingerprint del vehículo.

**Formatos de archivo:**
- `path_*.csv` (30 cols): time_s, x, y, z, heading_deg, speed_kmh, is_stop, gear, throttle, brake, steering, rpm, redline_rpm, mode, vx, vy, vz, clutch, handbrake (+ más, incl. **horn** y **lights** del humano para el replay de Fase 2).
- `header_*.txt`: `vehicleClass=`, `mass=`, `engineRPMIdle/Max/Redline=`, `gearsCount=`, `wheelCount=`, `attachmentsCount=`, `attachments=` (CSV), `maxSteeringAngle=`, `wheelbase=`. R_min = wheelbase / tan(maxSteer).
- `ai_run_*.csv`: time_s, x/y/z, heading, speed_kmh, gear, throttle, brake, steering, mode, dist_to_next_stop, next_stop_idx, **wp_idx**, **lateral_dev_m**, corridor_offset/valid, target_speed, target_throttle/brake, i_speed/throttle/brake, rpm, redline_rpm, wp_mode, is_marker.
- **El framework PREFIERE el par `_hdr.json`+`_wp.csv`** (FGets rápido, ~86 ms) sobre el JSON monolítico (parse lento ~150 s). **GOTCHA (histórico):** el viejo `csv_to_route.ps1` solo reescribía el `.json` → había que correr `route_split.ps1` o el server cargaba el par VIEJO (spawnea el vehículo anterior). El conversor actual (`frame_to_route.py`) **escribe los cuatro archivos de una** → ya no aplica.
- **El par fast-load lleva luces/bocina:** `route_split.ps1` escribe `targetLights` (col 16) y `targetHorn` (col 17) en cada fila del `_wp.csv`; `LoadWaypointsCSV` (en `BZBusService.c`) los parsea (`parts[16]`/`parts[17]`). Así el replay de luces/bocina funciona también por la vía de carga rápida, no solo por el JSON monolítico.

**El conversor NO linterea ni puntúa.** `frame_to_route.py` es directo: lee el header, generaliza la línea y escribe los cuatro archivos — **no** te pregunta *"¿capeo esta curva?"* ni da BZ-Score. Si la toma quedó sucia (huecos por lag/teleport, gear en lugging, cordón), la respuesta es **re-grabar**, no un linter. *(El linter interactivo + BZ-Score del wizard viejo se sacaron: la fidelidad sale de grabar bien + leer el fingerprint.)*

**Guard "Recording CORRUPTO" (csv_to_route):** algunas grabaciones VIEJAS traen, en vez de números, los especificadores de formato literales (`.2f`, `.1f`) en TODAS las filas — bug de una versión vieja del PathLogger (o tomas editadas a mano). Antes crasheaba el convert con un error críptico ("no se puede convertir '.2f' a Single"). Ahora `csv_to_route` lo **detecta** (chequea que la columna `x` de la 1ra fila parsee a número) y **aborta con mensaje claro**: *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"*. NO es un bug del wizard, es la grabación → elegí una toma **más nueva** (las de ARRIBA de la lista, ordenada por fecha desc, que tengan columnas `throttle/brake/steering`).

**`frame_to_route.py` (el conversor real):** toma el `header_*.txt` → VehicleClass + Fingerprint + Attachments automáticos. **Auto-detecta reverse por `gear==0`**. Deriva `targetHeading` por geometría (`atan2(dx,dz)`). **Auto-deriva los cortes de tramo / intercambios** (`legBreak`) del cambio de gear forward↔reverse a ~0 km/h (un 0 sin cambio de sentido = pausa, no intercambio). Escribe los **cuatro archivos** (`.json` + `_hdr.json` + `_wp.csv` + `_cinta.csv`) en `<profiles>\BZ_AutoDrive\` con backup `.bak` — **no** hace falta un `route_split` aparte. **No** acepta modos: el config de manejo sale del template único (`driving_config_template.json`). **Ojo:** `gear==0`=reverse asume la convención del carpack; verificar por vehículo (puede ser neutral). Guard de **corrupto**: si la columna `x` de la 1ra fila no parsea a número (grabaciones viejas con `.2f`/`.1f` literales) aborta con *"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"*.

> ⚠️ **`<server>` es la carpeta del server de quien te lee** (donde están `@BZ_AutoDrive` y `profiles\`), y cambia en cada instalación; sólo `%LOCALAPPDATA%\DayZ\...` es igual en todas las PC con Windows. Antes de guiar a alguien con ubicaciones de archivos, **PEDISELAS** (ver Walkthrough §7.G, Paso 0).

### 7.F — Problemas comunes del wizard (troubleshooting)

Mapeá el síntoma directo al fix. (Para errores de runtime/compile/deploy, ver §12.A.)

| Síntoma | Causa | Qué hacer |
|---|---|---|
| El panel no abre, NUMPAD 5 no graba, en una **instalación nueva** | `AdminSteamIDs` vacío = nadie es admin (el RPT del boot dice `AdminSteamIDs is empty` / `Admins: 0`) | agregar el SteamID64 **entre comillas** en `profiles\BZ_AutoDrive\BZAutoDrive_settings.json` y reiniciar (paso 0b del §7.G) |
| El RPT del boot dice **`No routes yet`** | no hay ninguna ruta todavía | grabar (NUMPAD 5) y convertir con el wizard. `No default route … N route(s) available in the panel` es normal: sin `BZBusRoute.json` nada arranca solo al boot (desde el 1002 no se genera la plantilla vieja) |
| **"Recording CORRUPTO… grabala de nuevo o elegí una más reciente"** al convertir | grabación VIEJA con `.2f`/`.1f` literales en vez de números (bug de un PathLogger viejo, o toma editada a mano) | NO es un bug del wizard, es la grabación. Elegí una toma **más nueva** (las de ARRIBA de la lista, ordenada por fecha desc, con columnas `throttle/brake/steering`). Si solo tenés tomas viejas, **regrabá**. |
| Una operación del menú **falla con un error en rojo** pero el wizard **sigue abierto** (vuelve al menú) | es el comportamiento nuevo: cada opción corre en su `try/catch` | pedile al usuario que **copie el texto del error** (ahora queda visible en pantalla) y que **siga desde el menú**. El wizard solo se cierra con **[Q]**. |
| (Versiones viejas) el wizard se **cerraba** y Windows preguntaba *"¿Desea terminar el trabajo por lotes (S/N)?"* | un error mataba el script lanzado desde `Wizard.bat` | ya **no pasa** en uso normal (try/catch por operación). Si lo ves, es un caso no contemplado → reportá el error. |
| En el selector de rutas/grabaciones aparecía un ítem **`*_hdr` de 0 waypoints** | el `_hdr.json` del par de carga rápida se listaba como ruta | ya **no se muestra**: `Select-Route` oculta los `*_hdr.json`. La lista solo trae rutas `.json` reales. |
| Aparece un recordatorio de **"sincronizar al server B"** (path real) | el usuario tiene `ServerBMirror` configurado en `wizard_config.json` | esperado: copiá el **par** (`_hdr.json`+`_wp.csv`, no solo el `.json`) al path que indica. Con un solo server, este aviso **no aparece**. |

### 7.G — Walkthrough guiado (para acompañar a un usuario en vivo)

*Para que una IA (vos) guíe a un admin que **NO programa** por el core loop. Regla: UN paso a la vez, confirmá antes de avanzar, traducí todo a acciones concretas (qué tecla, qué archivo, qué mirar) y verificá en disco.*

**0 · PEDÍ LOS PATHS DEL USUARIO PRIMERO — no asumas los de este doc.** Cada server/instalación cambia las rutas. Preguntá y anotá antes de empezar:
- **Carpeta del server** (donde viven el `@BZ_AutoDrive` y `profiles\`): puede estar en cualquier disco o carpeta (`G:\MiServer\`, un VPS, etc.) → **preguntá**.
- **Carpeta de grabaciones del cliente** (PathLogger): suele ser `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` en la PC donde juega → confirmá el `%LOCALAPPDATA%` real.
- **Dónde están las `tools\`** (el `route_wizard.ps1`).
- ¿Tiene **2 servers** (A local + B remoto)? Preguntá ambos paths; las rutas se sincronizan a mano (no hay sync automático).

Recién con esos paths, guialo con las ubicaciones REALES de SUS archivos en cada paso. *(El wizard, además, pide sus paths en la 1ra corrida y los guarda — es folder-agnostic.)*

**0b · QUE SEA ADMIN — sin esto nada anda.** El primer arranque del server con el mod crea `<server>\profiles\BZ_AutoDrive\BZAutoDrive_settings.json` con `"AdminSteamIDs": []`: **lista vacía = NADIE es admin** (el panel no abre, NUMPAD 5 y 4 no graban; el RPT del boot lo avisa con `AdminSteamIDs is empty`). Pedile su Steam64 (en steamid.io, o en el log del server cuando se conecta) y que lo agregue **entre comillas**: `"AdminSteamIDs": ["76561198000000000"]`. **Sin comillas no lo toma** (probado en juego). Después, que reinicie el server. ✅ Verificá en el RPT del arranque: `[Settings] Cargado. Admins: 1`. Si dice `Admins: 0` o aparece `AdminSteamIDs is empty`, el ID está mal escrito. Guía para humanos: manual §13.1.

**1 · GRABAR** (in-game, como admin):
- Teclas en `Opciones → Controles → "BZ AutoDrive"`. **Solo 3** (y solo el **admin** las ve): **Panel** (INICIO), **Grabar/Detener** (NUMPAD 5), **Marca de evento/parada** (NUMPAD 4). El **cambio de sentido/intercambio ya NO es tecla** (se sacó NUMPAD 3, 2026-08-11): se **auto-detecta** del cambio de gear forward↔reverse. El `ai_run`/`boris_native` **tampoco tienen tecla** — se arman desde los **checks del Reproductor** (Paso 3).
- Subí a un vehículo → **NUMPAD 5** (start) → manejá la ruta entera → en cada parada frená del todo y **NUMPAD 4** (marca) → si hay **cambio de sentido**, detenido **cambiás de marcha y seguís** (no tocás ninguna tecla: el intercambio se auto-detecta del gear, §9.4 manual) → **NUMPAD 5** otra vez (stop). La **reversa tampoco se marca**: se auto-detecta del gear.
- ✅ Verificá: aparecieron `path_*.csv` + `header_*.txt` en **SU** carpeta de grabaciones. **El `header_*.txt` NO debe pesar 0 KB** (si pesa 0 → el fingerprint no se capturó → regrabá). **Que anote con qué vehículo grabó.**

**2 · CONVERTIR** (en la PC, fuera del juego — **todo dentro del wizard**, sin editar JSON ni correr .py/.ps1 sueltas):
- **Abrir el wizard**: doble clic en **`tools\Wizard.bat`** (no hace falta abrir PowerShell). 1ra corrida: pide sus paths (server/cliente) y los guarda (`wizard_config.json`); después cambialos con **[6] Configurar paths**.
- Elegir **[1] Convertir** → elegís la **toma** de una lista ordenada por **fecha (la más nueva arriba)** — el nombre `frame_<stamp>.csv` es un sello del reloj **DEL JUEGO** (no de la PC). Si ves "Recording CORRUPTO", elegí una **más nueva** (§7.F). Después te pide **solo un NOMBRE** (Enter = ruta activa). Con eso corre `frame_to_route.py` (lee el header, generaliza, auto-detecta reversa, arma los cuatro archivos de una) y la deja **DESPLEGADA**: "lista en el Reproductor (hot-load)".
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
- **Verificado contra el código (25/09/2026) — la referencia completa, verbo por verbo y trigger por trigger, está en el manual §10.3.** Lo que más se malinterpreta:
  - **24 verbos** (se suman `set_vehicle_parts`, ver abajo, y `guard_stop`: la parada con escolta entera; campos `radius` 8, `holdS` 20, `clearS` 5, `maxS` 120, `boardS` 30, `unreachableS` 15). `lock_seat`/`unlock_seat`/`eject_passenger` no tienen handler. `slot` y `fvalue` no los lee ningún verbo.
  - **6 triggers**: además, `player_near_waypoint` (`wp` + `radius`: una zona fija alrededor de un waypoint, esté donde esté el vehículo). `waypoint` = alias de `wp_reached`; un evento sin trigger es un `wp_reached` sobre su `wp`. Un trigger mal escrito avisa al cargar (`TriggerTipoConocido`); un verbo mal escrito, recién al disparar.
  - **`delay` se cuenta desde el disparo del evento**, no desde la acción anterior. Cada evento dispara **una vez por corrida** y todo se re-arma en cada spawn (congelado, `stop_route`, mortalidades, variables, reloj de `timer`). El respawn automático **no relee** el `_hdr.json`: para ver un cambio, relanzar desde el Reproductor.
  - **`timer`** cuenta desde que **nace el vehículo** (incluye la espera de arranque). **`vehicle_health_below`** casi nunca dispara con `VehicleInvincible: true` (el default).
  - **`stop_engine` solo dura medio segundo**: el tick re-enciende el motor en todo estado salvo **congelado**. Esperar apagado = `freeze_vehicle` + `stop_engine`; `stop_route` no permite apagar.
  - **`ui_broadcast` (1000): `value` = a quién** (`""`/`"admins"` default · `"all"` · `"near"` + `radius`, 0 = 150 m · `"passengers"`; cualquier otro = admins, un tipeo nunca llega a los jugadores), **`format` = por dónde** (`"toast"` default · `"chat"` = `GetGame().ChatMP(p, "BZ AutoDrive: " + msg, "colorStatusChannel")` · `"both"`; desconocido = toast); una entrega por jugador; log `[EVENT n @ wp X] ui_broadcast (all → N jugadores, toast): msg`. **Los avisos automáticos son sólo de los admins**, en dos tipos que se eligen por separado en `BZAutoDrive_settings.json`: `AdminArrivalNotifications` (llegadas: parada + espera, distancia, fin de línea, ruta completa, salió de la terminal) y `AdminSystemNotifications` (vehículo detenido, pausa/reanuda, spawn fallido, la seña a Boris): `"toast"` (default, también ausente/desconocido) · `"chat"` · `"both"` · `"off"` (igual queda `[GLOBAL]` en el RPT). Las respuestas del panel van siempre en pantalla al admin que hizo el clic. Los avisos de grabación son locales del cliente (sólo los admins pueden grabar). Guía para humanos: manual §13.3. **`set_var` no tiene efecto**: `BZCondition`/`EvalCondition` existen pero nadie llama al evaluador y ningún campo del JSON acepta una condición → **un admin no puede usar condiciones**. El branching es de Quests.
  - **`set_vehicle_mortality` / `set_driver_mortality`**: sólo `"mortal"` exacto vuelve mortal; sin `value` o `"Mortal"` = invencible. **`set_vehicle_parts`**: sólo `"unlocked"` exacto deja sacar piezas; cualquier otro valor = **inviolable** (default de la ruta: `VehiclePartsLocked: true`). Mecanismo: el runner fija `m_BZPartsLocked` (NetSync) en el `modded CarScript`, que sobrescribe `CanReleaseAttachment` → `false`; las acciones vanilla (`ActionDetach`, `ActionDetachFromTarget`, `ActionTakeItem`) lo preguntan en el cliente, así que el cartel "quitar rueda" ni aparece y el server rechaza igual. El default en el auto es `false` (los autos de jugadores quedan vanilla); al soltar el vehículo de botín (chofer muerto) vuelve a `false`. No toca la carga, ni subir/bajar, ni poner piezas. Vanilla sólo bloquea sacar piezas con el motor en marcha **y** en movimiento.
  - **Con el cerebro 1.2**, `wp_reached` compara contra el cursor del cerebro: en rutas con `[Route] saneo de retrocesos: N>0` puede disparar unos puntos tarde. Las luces: desde el 992 `Tick12` llama a `AplicarLucesYLatido` en cada tick (recalcula `LightsMode` por hora y late `BZPulseLights` para los otros clientes); **un verbo `lights_on`/`lights_off` manda sobre `LightsMode` hasta el próximo spawn** (`m_LightsVerbo`), en los dos cerebros.
- **Para configurar:** ver §10/§11 del manual + Apéndice (ejemplos JSON de las 2 escenas).
- **Para programar un verbo nuevo:** agregar una rama `else if (verb=="x") {...}` en ExecuteAction (tenés `car`, `action`, `m_WaypointIndex`); el delay lo maneja el dispatcher. Campos nuevos → agregalos a `BZAction` (es plana, no rompe el parser).

**Audio (`play_sound`):** SoundSet 3D pegado al vehículo (SEffectManager). Declarar `.ogg` en CfgSoundShaders+CfgSoundSets. **Gotcha:** sonido lanzado server-side puede no llegar al cliente → reenviar por RPC (la infra de toast `RECEIVE_TOAST` ya existe; ver `BroadcastGlobal` que manda RPC a todos los clientes → `NotificationSystem.AddNotificationExtended`).

---

## 9. Integración Quest (DayZ-Expansion-Quests)

**Reparto:** Quest = bots vivos + lógica de misión (reward, progresión); eAI = cuerpo del bot; BZ_AutoDrive = vehículo + manejo + coordinar subir/bajar. Un bot spawneado standalone por el framework = maniquí sin lógica → los bots VIENEN del Quest.

**Hook (`BZQuestHook.c`):** `modded MissionServer { override Expansion_OnQuestStart(quest) { super; BZBusService.GetInstance().OnQuestStart(quest); } }`. (La subclase `ExpansionQuestObjective*Event` NO compila en scope 5_Mission → por eso el hook va en MissionServer.)

**Poll (`OnQuestStart`/`CheckQuestBots`):** guardar `m_QuestCheckID = qc.GetID()`; CallLater poll de `ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(id, patrols)` (~cada 2–4 s). `patrol.m_Group.Count()` = bots (son lazy por proximidad; el conteo inicial es el PICO, materializan de a poco 3→5). Conteo baja → mataron uno → trigger.

**Boarding (`BoardQuestBots`) + el gotcha de la pacificación:** un bot EN COMBATE no camina a un waypoint (el FSM eAI exige "sin amenaza"). Hay que: `b.eAI_SetPassive(true)` + `b.eAI_SetThreatDistanceLimit(0.0)` + drenar targets (`GetTarget(0)`/`eAI_RemoveTarget` en loop). Recién entonces camina a `transport.CrewEntryWS(seat, door, ddir)` y sube animado (sino sube por teleport, feo). Seat 1+ (0=chofer). NO re-agrupar (preserva el kill-count del quest). `SetThreat` NO existe en eAIGroup.

**Escenas validadas:** `flee_on_kill` (pacificar→subir→manejar→bajar al patio→OnQuestComplete→despawn) y `ambush_on_damage` (bots instant a bordo + Boris armado → cualquier daño [CarScript EEHitBy o poll de salud] → NotifyConvoyDamaged → freeze + dismount-todos hostiles). Insight: el ambush ES un marcador NUMPAD 4 generalizado (trigger on_damage → secuencia); refactor natural = receta del DSL de Events[].

**Objective types:** validado con AICamp/AIPatrol. Los otros (AIEscort, AIVIP, Travel, Delivery, Target, Action, Collection) la arquitectura los soporta pero falta su hook — campo abierto (ver tabla en §16 del manual).

**API eAI relevante:** no hay command bus imperativo; todo es GRUPO + waypoints + FSM reactiva. Locomoción: `FormationState=IN` (NONE=halt era el bug), `AddWaypoint` + `ForceRecalculate`, `OverrideMovementSpeed/Direction`. eAIBase ES PlayerBase. Ver memorias `reference_eai_*`.

---

## 10. UI

**Control Panel / Reproductor** (se abre con la acción `UABZAutoDrivePanel`, default `INICIO`/`kHome`): tablero en vivo de cada runner (wp, velocidad, modo) + control (parar/pausar/teleport). El viejo `BZControlPanelUI.c` quedó **desconectado** — la acción ahora abre el Reproductor (`BZReproductorUI.c`); el `CTRL+HOME` se eliminó.
**Reproductor** (`BZReproductorUI.c`, MENU_BZ_REPRODUCTOR id 51213): lista rutas (`EnumerateRoutes` = `BZBusRoute*.json` excluyendo `_hdr`/tmp), LOAD&SPAWN (`RespawnFromPath`) sin restart. Pretty() strip de `BZBusRoute_`+`.json`.
**Layouts:** formato **CPP-style** (`PanelWidgetClass {...}`), NO XML/library (crashea CreateWidgets). Ref: BrigadaZRadio/gui/layouts. Texturas .paa (pipeline PNG→PAA con ImageToPAA); patrón "imagen bakeada + overlay".
**RPC (`BZBusRPC.c`):** enum con `RECEIVE_TOAST` (notificación a clientes), slots, stop/pause, telemetría. Desde el 1000: `EnviarAviso(p, msg, formato)` es el único que le manda algo a un jugador (toast = `ScriptRPC` `ScriptRPC` con `BZBusRPC.RECEIVE_TOAST` → cada cliente hace `NotificationSystem.AddNotificationExtended(6, "BZ_AutoDrive", msg, "")`; chat = `GetGame().ChatMP`). `AvisoLlegada`/`AvisoSistema` → `AvisoAdmins` (sólo admins, según el modo del settings); `ui_broadcast` → `Difundir` (destinatario + formato). `BroadcastGlobal` ya no existe.
**Pendiente UI:** rebind de teclas desde la propia UI del mod (hoy se hace desde el menú de Controles del juego, ver D.1); lista dinámica vs slots fijos.

---

## 11. Programar / extender — patrones de código

- **Sin mod propio: lanzar y encadenar rutas desde el `init.c` de la misión** (manual §8.2). En `class CustomMission: MissionServer`: `override void OnInit() { super.OnInit(); CallLater(...60 s...) }`; cada ruta con **su propio runner** (`new BZBusService()` → `GetRunners().Insert(r)` → `SetHoldAtStops(false)` → `SetEtiqueta` → `RespawnFromPath("BZBusRoute_X.json")`), no con `PlayRouteByName` (su `holdAtStops=true` por defecto deja el vehículo retenido en la primera parada, y puede reusar el runner principal, que `UnregisterRunner` no da de baja). Encadenar = un `CallLater(..., 5000, true)` que mira `RunnerRouteEnded()`/`RunnerRouteCompleted()` y lanza la siguiente; un runner `RespawnFromPath` que termina COMPLETE se retira solo (`EndHoldSeconds`); uno que termina mal queda quieto → `UnregisterRunner`.

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
**(e) Como dependencia (otro mod)** → `requiredAddons[] = {"BZ_AutoDrive"}` + `BZBusService.GetInstance().RespawnFromPath("BZBusRoute_X.json")` / `RespawnAs(clase)`. Eso usa el runner PRINCIPAL (una ruta por vez); para una flota, (f).
**(f) La API del runner (flota, un runner por vehículo)** — referencia completa y para humanos en el **manual, Apéndice B.7**. Lo esencial, sin RPC, todo server-side:
- **Lanzar:** `PlayRouteByName(fname, holdAtStops)` (taxi: no duplica, un runner por archivo) **o** `new BZBusService()` + `GetRunners().Insert(r)` (la ÚNICA referencia fuerte) + `SetHoldAtStops(true)` + `SetEtiqueta(e)` (ANTES de cargar) + `RespawnFromPath(fname)`; si `!RunnerIsActive() && !RunnerEsperandoCabecera()` → `UnregisterRunner(r)`. Terminar: `UnregisterRunner(r)` (= `StopBus` + sacar del registro; borra vehículo y chofer). **Nada se retira solo.**
- **⚠ wp 0 ocupado** (otro `CarScript` a < 3,5 m): el runner ESPERA (re-intenta cada 5 s; NO corre el punto), `RunnerIsActive()` false + `RunnerEsperandoCabecera()` true: **no es una falla**. Panel `WAITING wp0 ocupado Ns`.
- **Estado (polling a 1 s):** `RunnerIsActive` · `RunnerWpIdx/WpTotal` · `RunnerPos/SpeedKmh` · `RunnerAtStop` · `IsHeld` (hold) ≠ `RunnerIsPaused` (pausa blanda) · `RunnerRouteEnded/RouteCompleted/EndReason` · `RunnerFellBackTo11` · `RunnerBloqueado/BloqueadoMotivo/BloqueadoDesdeS` (escalera abierta) · `RunnerRecuperando/RecuperandoQue` (superconjunto: + C1, F1, re-ubicación) · `RunnerChoqueHaceS` (definición de la línea `*** CHOQUE ***`; −1 si no hubo) · `RunnerEsperandoCabecera/EsperandoCabeceraS`.
- **Control:** `HoldRunner` (con meseta grabada a < 8 m engancha EN ella; ⚠ un hold hasta 8 m antes de una parada grabada la da por servida) · `ResumeFromHold` · `SetPaused` · `HornBeep` · `StopRunner` · `SetVehiclePartsLocked(bool)` / `VehiclePartsLocked()` (vehículo inviolable). **Identidad:** `GetInstance` · `GetRunners` · `GetRunnerForCar` · `GetBus/GetDriver` · `GetRouteName` · `Etiqueta/NombreLog` · `GetOrigin` · `IsControlPanelAdmin(sid)` (pública, fail-closed).
- **Eventos** (`PollEvent`, `BZDriveEv`): 1 ARRIVED · 2 ROUTE_COMPLETE · 3 STUCK · 4 ROUTE_INCOMPLETE · 5 BLOQUEADO (no cierra). **1.2.1:** 2 y 4 salen en el REPOSO (≤ ~3,5 s después de llegar) con el veredicto estricto; `RunnerReposoFinM()` da la distancia en reposo. **La cola es interna**: el controlador es privado del runner y el runner la drena él mismo; un mod consumidor usa los métodos `Runner*` (polling).
- **Contar en el RPT:** choques por EPISODIO (`*** CHOQUE #N ***` + `episodio cerrado`; `BACHE` aparte), BLOQUEADO == `BLOQUEO terminado` por coche, puntos negros por POSICIÓN (no por wp), `quedo:` por spawn. ⚠ ms sin ceros a la izquierda (`41.27` = 41,027 s); ⚠ los CSV de 40 Hz se nombran con la hora UTC y SIN fecha: ordenar por fecha de archivo.

**Car API (escribir):** `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. **(leer):** `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (→wheelbase), `WheelGetSurface(i)`, `EngineGetRPMRedline()`, `GetGearCount()` (NO `GetGearsCount`, obsoleto). Config: `GetGame().ConfigGetFloat/Array("CfgVehicles/<cls>/...")`.

**CfgVehicles vs script classes:** `SurvivorM_*` es config, no script; en `.c` heredar de SurvivorBase/ManBase/ItemBase/House, nunca de la entry de CfgVehicles. Clases engine (CGame) NO se moddean.

---

## 12. Gotchas y errores + fixes (CRÍTICO — no repetir)

### 12.0 — Gotchas de 1.2 (septiembre 2026) — LEER PRIMERO

Cada uno de estos nos costó horas. Van primero porque son los que un modder (y vos) va a pisar.

1. **Existir no es tener contenido.** Varias banderas del framework se leían con `FileExist`: un archivo **vacío** cuenta como presente. Así el cerebro 1.2 dependía de `CEREBRO_12.txt` (sin él corría el 1.1 **en silencio**) y un `BZ12_CALIBRATE.txt` vaciado dejaba mudo al harness. Regla: **para desactivar, borrar; para leer una bandera, mirar el contenido.**
2. **Revisá `NULL pointer` en el RPT, no sólo `Can't compile`.** `grep -c "NULL pointer to instance"`. El patrón es `SCRIPT    (E)` con **cuatro espacios**: no aparece buscando `ERROR`. Una excepción en `Sample` aborta el resto del cuadro y **el vehículo queda con el último input congelado** (gas 1 → "a fondo sin avanzar"). Así se escondieron 372-719 excepciones por sesión durante un día entero (fix 938).
3. **La física es determinista.** Mismo vehículo, mismo punto de spawn ⇒ **la misma corrida bit a bit** (dos corridas separadas por 20 minutos dieron las mismas 9 muestras en el mismo orden, con el pitch a 4e-5°). Repetir una prueba desde el mismo spawn **no agrega información**.
4. **Velocidad leída ≠ movimiento.** En el despegue desde parado, `|GetVelocity|` sube hasta ~1,8 km/h y se resetea 1-3 veces **sin que el vehículo se mueva 10 cm** (63 % de 203 despegues humanos, todas las clases, y con el NPC igual). Consecuencias: la toma **no es referencia** del arranque, y "quieto" (0,5 km/h) se cruza por la lectura antes del movimiento. Para saber si se movió, **mirá la posición**, no la velocidad.
5. **La masa es la del config**, no la del vehículo real (Tiger: 2.000 kg para el motor). Cualquier razonamiento físico usa lo que imprime `[Manual] masa=`.
6. **El teclado es binario.** 95 % de los despegues grabados tienen el gas a fondo: de una toma de teclado no se deduce ningún valor intermedio de pedal.
7. **El reloj y la señal de "terminó" salen del RPT, nunca del script que orquesta.** Un `sleep` en un bucle de prueba inventó una dispersión del 37 % que era 0,4 %; un `DESPAWN FINAL` provocado por el propio cambio de bandera se leyó como fin de ruta. Para duraciones: `FIN: ROUTE_` y las marcas de tiempo del RPT.
8. **Una mediana sobre histograma con n chico se verifica a mano.** `MedianOf` pedía el rango `n/2` (el 2.º de 5) y dos calibraciones distintas dieron **el mismo** número. **Misma salida con entradas distintas = el instrumento no lee.**
9. **Un `Save()` no puede borrar lo que no entiende.** Guardaba sólo sus propios campos y la procedencia que escribía el conversor (`fp`, `medido`) se perdía en el primer guardado. Hoy se conserva (`m_Extra`); cualquier escritor nuevo de un archivo compartido tiene que hacer lo mismo.
10. **Parsers con `return` genéricos:** una rama nueva para un tipo de línea de 2 campos quedó **después** de un `if (p.Count() < 3) return;` y nunca se ejecutaba. Probá una rama nueva **con la línea real** del archivo, no con datos armados.
11. **Para "son iguales", tolerancia, no redondeo.** Redondear a bins tiene fronteras: dos valores a 0,0002° cayeron en bins distintos. Con más decimales falla más seguido; con menos, falla menos pero igual.
12. **Una toma vieja no garantiza una corrida limpia**, y para rutas estocásticas el hilo ya fijó cuántas pasadas hacen falta (Svergino: ≥ 5). **Antes de correr para probar algo, buscá si el criterio de evidencia ya está escrito.**

**Enforce:**
- No ternario `?:`; no `if` multilínea con `&&` al inicio de línea → "Syntax error". Una línea o bools intermedios.
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
- Para probar en local, el cliente puede cargar el `@BZ_AutoDrive` desde la carpeta del server, sin !Workshop ni suscripción.

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
| copiar el PBO al server (`robocopy`) sale con exit **8/9** | el server O el cliente abiertos lockean el PBO | cerrar **ambos** antes de buildear/mirror; preguntar "¿abierto o cerrado?" |
| Tras deployar una ruta NUEVA, spawnea el **vehículo anterior** | par `_hdr.json`/`_wp.csv` VIEJO (el framework **prefiere el par**); pasa con flujos viejos — `frame_to_route.py` ya escribe los tres | reconvertir con el wizard (escribe los cuatro archivos) + sincronizar el PAR a B (no solo el `.json`) |
| Cliente pateado: **"El cliente tiene un PBO que no está en el servidor"** | el DZSALauncher cachea sus mods EN MEMORIA + quedaban copias viejas (`Local.json`, `!Workshop\@viejo`) | **SALIR DEL TODO** del launcher (bandeja → Exit) tras editar `Local.json`; borrar las copias viejas del mod |
| "Faltan PBO" al conectar | falta la `.bikey` del mod en `keys/` | copiar la `.bikey` a `keys/` (A y B) |
| El launcher seguía cargando el mod viejo aunque edité el archivo | el `meta.cpp` tenía el `name` viejo (dos mods locales con mismo `name` se confunden) + launcher no relee hasta reiniciar | migrar el `name` del `meta.cpp`; reiniciar el launcher del todo |
| Boot del server ~3 min | JSON de ruta enorme parseado en `Init()` | partir la ruta / usar el par fast-load (FGets); lazy-load (futuro) |
| 40 mods → "Server can't transmit all data" | techo ~38 mods (Steam query ~255 bytes) | sacar no esenciales o consolidar PBOs chicos |

### 12.x — DayZ 1.30 (medido, septiembre 2026)
| Síntoma | Causa | Qué hacer |
|---|---|---|
| La 1.2 no compila en la 1.30 | `ToggleHeadlights()` ya no existe; las luces son otro sistema | usar la 1.2.1 (un PBO con `#ifdef DAYZ_1_29`) |
| El server se cae al retirar un vehículo | borrar un vehículo con un Man sentado tira el server en la 1.30 | esperar a que quede vacío (el framework ya lo hace; ojo con otros mods que borran vehículos) |
| `FindFile("$profile:…")` no encuentra archivos | en la 1.30 lista la raíz del perfil | construir la ruta completa; insistir si el primer `ShiftTo` se pierde |
| La lista de rutas sale vacía en la 1.30 | con `-profiles` absoluto la 1.30 no deja listar archivos del perfil (RPT: `DayZ 1.30 cannot list files under an absolute -profiles path…`) | arrancar el server con `-profiles` relativo (`-profiles=profiles`) |
| La moto frena "al revés" | `SetBrake` = delantero, `SetHandbrake` = trasero (los comentarios del vanilla están al revés) | usar los dos para sostenerla quieta |
| `MotorbikeFluid.OIL` no compila | enum derivada sin `;` en el vanilla | usar `ETransportFluid` / `GetFluidCapacityScript` |
| El reloj de la moto va al doble | `EOnPostSimulate` corre 2 veces por paso en la moto | medir el tiempo con `GetGame().GetTime()` |
| Una toma de la 1.29 choca en la 1.30 | el mapa cambió (p. ej. un wreck nuevo en la ruta) | las tomas se prueban en el server donde se grabaron; regrabar donde el mapa cambió |

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

- **Términos de 1.2** (detalle en §5): **cinta** = el manejo grabado a 40 Hz indexado por arco (`_cinta.csv`) · **envelope** = la ficha MEDIDA de una clase de vehículo (`envelope_<clase>.csv`) · **creep** = arrastre en ralentí (0 en toda la flota) · **piso de despegue** = gas mínimo para romper el reposo (prior de flota 0,13) · **meseta** = una detención grabada que la cinta sostiene · **leg** = tramo entre cambios de sentido · **cusp** = el punto de cambio de sentido · **POSE** = módulos que trabajan contra un punto a ~0-2 km/h (maniobra, endpoint) · **procedencia** = CONFIG / DERIVED / MEASURED / FALLBACK de cada dato del manual del vehículo.
- **Dependencias de Expansion:** el mod exige (`config.cpp`, `requiredAddons`) AI, Vehicles y **Quests** —Quests también sin misiones—, y **las tres vienen en `@DayZ-Expansion-Bundle`**. ⚠ **Nunca le digas a un usuario que agregue `@DayZ-Expansion-AI` suelto** si tiene el Bundle: mismas clases, otra versión, el server no arranca.
- **eAI** = DayZ-Expansion-AI (la IA base). **Boris** = el NPC chofer (eAI_SurvivorM_Boris). **Override-last** = correr OnInput después de eAI. **Fingerprint** = datos del vehículo capturados al grabar (header_*.txt). **R_min** = radio de giro mínimo = wheelbase/tan(maxSteer). **Corredor/paredón** = banda muerta del control lateral. **BZ-Score** = puntaje 0–100 (tooling de **informes standalone**; el wizard conversor ya no lo calcula). **Runner** = una instancia de vehículo en ejecución. **Triple match** = A=B=cliente mismo PBO.
- **Classnames demo:** vehículos del CarPack (viper_yellow, x5mcompetition_orange, Star_APC_Cobra_white, Star_Golf_MK1, etc.), UAZ_452 (mod @[CnG]UAZ_452), vanilla (Hatchback_02, M3S/V3S), **CivilianSedan_Wine** (EXAMPLE02: 3er vehículo de ejemplo, replay con luces+bocina humano en el mismo vehículo — 98.6 % completion, lat-dev 0.79 m).
- **Regla del corte (§2/§5), conceptual:** el direct-replay de una maniobra reproduce ángulos de volante VEHICLE-SPECIFIC (mismo ángulo → radio ∝ wheelbase), así que **generaliza a otro vehículo solo si el corte al bloque de maniobra cae donde la trayectoria es RECTA** (la curva queda en closed-loop Stanley, vehicle-agnostic; la recta va en replay). Cortar en curva → open-loop → otro wheelbase deriva → no generaliza. *(El mod NO incluye tomas de ejemplo: una ruta grabada son coordenadas de un mapa puntual.)*
- **Docs hermanas:** el **manual** (`MANUAL_BZ_AutoDrive.md`, didáctico para admin/modder). El **CHANGELOG** (`CHANGELOG.md`) tiene el historial de versiones.

---

## Apéndice C — Listados de código fieles (por componente)

> Excerpts reales del source (nombres de método/campo exactos; cuerpos largos recortados con `// ...`). Para el detalle total, greppear el archivo citado.

### C.1 `BZBusConfig.c` — clases del DSL

```c
class BZBusRouteConfig {
    int    RespawnDelay = 300;        float AverageSpeedMS = 11.0;   float SpawnHoldSeconds = 3.0;
    string VehicleClass = "ExpansionBus";   string DriverClass = "eAI_SurvivorM_Boris";
    bool   VehicleInvincible = true;  string ConvoyMode = "";        int   MaxGear = 6;
    bool   VehiclePartsLocked = true;   // vehículo inviolable: los jugadores no le sacan piezas
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

> **Índice de guías AI-actionable:** D.5 (verbo nuevo) · D.6 (vehículo nuevo) · D.7 (mapear una zona — no disponible en 1.2) · D.8 (vehículo a un Quest) · **D.10 (diagnosticar una corrida / `ai_run`)**. Para el formato crudo del `ai_run` ver **D.3**; para la metodología de calibración (lazo ILC, heurísticas síntoma→fix) ver **§14**.

### D.1 — Keybindings (sistema de input del menú de Controles)

Migrado al **sistema de input nativo de DayZ** (ya NO `OnKeyPress`). Las acciones se declaran en `data/inputs.xml` (`<modded_inputs>` → `<actions>` con name+loc EN · `<sorting name="BZ_AutoDrive" loc="BZ AutoDrive">` = la pestaña en `Opciones → Controles` · `<preset>` con los defaults), referenciado en `config.cpp` (`inputs = "BZ_AutoDrive/data/inputs.xml";`). `build_include.lst` incluye `*.xml` (sino AddonBuilder lo excluye en silencio). Rebindeables desde el menú; admin-only.

**Detección:** se pollea en el `OnUpdate` del cliente (`BZBusMissionGameplay.c`) con `GetUApi().GetInputByName("UABZ...").LocalPress()`; client-side → server vía RPC (validado server-side por `IsControlPanelAdmin`). **Guard crítico** (sino las acciones se disparan al reasignar una tecla): `if (GetGame().GetUIManager().GetMenu() || GetGame().GetUIManager().IsDialogVisible()) return;` antes del poll — NO `HasGameFocus()` (= foco de ventana del SO, sigue `true` con un menú abierto).

**Diseño de defaults (rediseño 2026):** el menú de Controles "BZ AutoDrive" quedó reducido a **3 inputs, los 3 asignados** — el core loop entero. Se sacaron los ~16 controles viejos de debug/tuning/modo (Parking/Reverse/Maneuver Mode, AI Logger, SysID, Spawn Slots, Pause, Mark Gear/Max…) y también la vieja **tecla de intercambio** (`UABZMarkLeg`/NUMPAD 3, sacada 2026-08-11): las maniobras salen del **intercambio auto-detectado** (cambio de gear forward↔reverse), la reversa se **auto-detecta del gear**, y `ai_run`/`boris_native` se arman desde los **checks del Reproductor**. Los controles **solo los ve el admin** (gate `IsControlPanelAdmin`). Labels LITERAL en inglés (el menú de inputs NO resuelve stringtable).

| Acción (label EN) | `UAName` | Default | Notas |
|---|---|---|---|
| Open Control Panel | `UABZAutoDrivePanel` | `kHome` | abre el Reproductor / panel |
| Record (start/stop) | `UABZRecord` | `kNumpad5` | toggle del PathLogger **+ FrameRecorder** |
| Mark Event / Stop | `UABZMarkStop` | `kNumpad4` | marca parada / nodo de evento (marca **ambos** recorders) |

> **Intercambio (`legBreak`), sin tecla:** el corte de tramo por cambio de sentido se **auto-deriva** del cambio de gear forward↔reverse (siempre a ~0 km/h) en `frame_to_route.py`. Un 0 km/h **sin** cambio de sentido = pausa, no intercambio.

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
3. **Wizard** (doble clic en `tools\Wizard.bat`): **[1] Convertir** te pide **solo el NOMBRE** (→ `BZBusRoute_<nombre>.json`). `frame_to_route.py` lee el header, generaliza, auto-detecta reversa y escribe los cuatro archivos `.json`+`_hdr.json`+`_wp.csv`+`_cinta.csv`, ya **desplegado** (hot-load). Sin modos ni linters.
4. **Deploy:** lo hace el propio Convertir (split + escritura a `profiles\BZ_AutoDrive\`). El mirror a B sale de `wizard_config.json` (`ServerBMirror`); ver **[6] Configurar paths**.
5. **Validá**: tildá el check **`ai_run`** en el Reproductor + spawneá/test. Analizá el `ai_run` (ver D.3) y ajustá params por síntoma (ver D.2 + §14).

> **No-negociable:** al cambiar `VehicleClass`, REEMPLAZAR `Attachments` por las partes del vehículo nuevo (`csv_to_route` preserva las anteriores → rompe el spawn). Bug recurrente.

### D.7 — Guía: mapear una zona (armar y unir grafos de rutas) — ⚠ NO disponible en 1.2 (sin herramientas en el paquete; el manual ya no lo trata)

**Qué es:** más allá de reproducir UNA toma, se graban MUCHAS y se componen en un **grafo dirigido** → un pathfinder (**Dijkstra**) arma rutas A→B **que nunca se grabaron enteras**. Hoy es **Tier 2**: herramientas offline asistidas por LLM (la grabación es in-game; el armado del grafo y el ruteo se resuelven fuera del juego; el roadmap es llevarlo al runtime).

**Cómo se une el grafo (automático e incremental):**
- Cada punto (downsampleado ~4 m) de cada traza = un **nodo**.
- Donde dos trazas **se cruzan o rozan ≤6 m** = una **arista/intersección** (auto-merge; clusters ≤10 m).
- Es **dirigido**: cada calle se recorre solo en el **sentido grabado** (= respeta el carril). Para ir y volver, **grabá los dos sentidos** (cada sentido = una traza).
- Una traza nueva **se integra sin tocar las anteriores** (monótono: solo suma cobertura).
- **Giros:** grabados (se siguen de puntos reales) vs no-grabados (arco al **R_min** del vehículo). El destino se extiende a la intersección perpendicular.

**La herramienta — `bz_coverage.py`** (en la PC del dev; **script de análisis, NO va en el PBO ni en el paquete de esta versión**): lee los `path_*.csv` de la carpeta de grabaciones del cliente, **filtra por un bbox** (`X0,X1,Z0,Z1`) y reporta **tramos, km, nodos, intersecciones, bbox** + guarda un **coverage map PNG**.
- ⚠️ El `HD` (carpeta de grabaciones) y el `bbox` del script traen **valores de ejemplo** → **ajustalos a los del usuario** (preguntá su carpeta PathLogger + la zona a mapear).
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
**Gotchas:** ambush sin `VehicleInvincible:false` no dispara · bots en el Quest (no en `Crew[]`) · ejemplos completos validados en el **manual §11.3/§11.4** (convoy) y §11.10/§11.11 (Travel/Escort, que auto-seleccionan ruta por quest).

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

**Qué es el ai_run.** Es la **telemetría de una corrida de Boris** — la "caja negra" de lo que el NPC efectivamente hizo. Es **opt-in** (el admin lo arma tildando el **check `ai_run` en el Reproductor**, ANTES de dar play — ya no con tecla; sin eso NO se escribe). Vive en `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv` (en el server que corrió). **NO lo confundas con la toma humana** (`path_*.csv`, la grabación/demostración, 50 Hz, otras columnas) — son archivos distintos con propósito opuesto (medición vs referencia).

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

**Cómo entregar el diagnóstico (vos → el admin, en su idioma):**
1. **Resumen:** ¿completó sí/no? + DÓNDE están los problemas (wp + tipo).
2. **Por cada problema:** la **firma** observada (citá la columna: `lateral_dev_m`, `corridor_valid`, `speed_kmh` vs `target_speed`, `rpm`/`redline_rpm`…) + la **causa** + el **fix concreto** (citá el param de D.2 / §5 cuando aplique).
3. **Recordá** que el fix suele ser **re-grabar ese tramo** o **ajustar un parámetro de la ruta** (no realimentar el ai_run).

> **PRINCIPIO (no violar):** el ai_run es una **MEDICIÓN** que se cruza CONTRA la toma humana para calibrar (feedback funcional); **NUNCA una referencia nueva**. **Jamás** sugieras convertir un `ai_run` en ruta: clona los errores de Boris → degrada cada iteración ("model collapse"). El wizard lo filtra a propósito. La toma humana es la referencia sagrada; el ai_run es el termómetro.

### D.11 — Guía: importar una toma de BrigadaZ Transport v1

*Convertir una ruta del mod viejo **BrigadaZ Transport v1.0** (un JSON monolítico con `Waypoints`) al formato AutoDrive, sin regrabar. Wizard → **[2] Importar toma v1**.*

1. **Elegís el `.json`** de la toma v1.
2. **Identidad del vehículo (`--fingerprint`):** la toma v1 NO trae `Wheelbase`/`Fingerprint`/`Attachments` (dependen del VEHÍCULO, no de la ruta). Le das **cualquier** `header_*.txt` de ese vehículo (sirve una grabación de **10 s**) o un `_hdr.json` de una toma ya calibrada. El wizard pone primero los que matchean el vehículo declarado.
3. **Perfil de obstáculos (AR_OnWay, §5):** `[R]obusto` (Slow+Escape ON — un bus de línea que sortea lo que le tapa el camino) · `[I]nterceptable` (Slow ON, Escape **OFF** — frena pero NO se escapa → sirve para misiones de interceptación) · `[N]inguno` (réplica pura). Setea `ObstacleSlow`/`ObstacleEscape` en el `_hdr` (requiere `UseInverseModel=true`, que el template trae).
4. **Salida:** el trío `BZBusRoute_<nombre>.json`+`_hdr.json`+`_wp.csv`, ya desplegado — **sin `_cinta.csv`**: una toma v1 no tiene el manejo a 40 Hz, así que corre **sin cinta** (como seguidor de trayectoria, §5.3). Corre `transport_v1_to_route.py` (misma firma que `frame_to_route.py`).

**Qué migra y qué no:** la **traza + velocidad + paradas** migran (el header/wp de AutoDrive es superconjunto estricto de los de v1). Los **pedales** de v1 se **descartan** (el control se reconstruye desde traza+velocidad, no se replica). El **perfil de manejo es por vehículo** → sale del `--fingerprint`, no de la ruta v1. `targetHeading` se deriva por geometría (`atan2(dx,dz)`, error mediano ~0.35°). Ver manual §6.2.

---

## Apéndice E — Guía para portar el framework a otro juego: 7 Days to Die (Unity / C#)

> **Para qué es este apéndice.** Hay gente que quiere llevar la idea de BZ AutoDrive a su propio juego. Este apéndice está escrito para que **una IA de programación (Codex, ChatGPT, Claude u otra)** pueda guiar ese port paso a paso, con el caso concreto de **7 Days to Die** (Unity, mods en C# con Harmony). Todo lo que dice sobre 7DTD está **verificado**: el autor hizo en agosto de 2026 una prueba de concepto que manejó sola un vehículo en un server dedicado de 7DTD. El código de esa prueba está completo en E.3.
>
> **Si sos la IA que va a hacer el port:** leé este apéndice entero antes de escribir una línea. Después leé la §5 (el cerebro 1.2) y la §7.0 (lo que produce la conversión). No intentes portar todo de una: seguí los hitos de E.4 **en orden**, y no pases al siguiente hasta que el anterior cumpla su criterio de "hecho", **medido en el juego con números en el log**. El framework de DayZ se construyó así: un cambio por vez, cada uno con su medición. Es la única forma que funcionó.

### E.0 — Cómo trabajar (instrucciones para la IA)

1. **La idea central, que no hay que perder.** El framework NO es un piloto que "decide cómo manejar". Es un **reproductor de manejo humano medido**: un humano maneja el recorrido una vez, el framework graba lo que hizo en cada punto del camino (40 veces por segundo, **indexado por distancia recorrida, no por tiempo**) y el NPC lo reproduce, corrigiendo sólo lo necesario para no salirse de la línea. *"Vuela donde volaste, frena donde frenaste."* La física del vehículo pone el resto. Si en algún momento te encontrás escribiendo un planificador de trayectorias o un controlador que "inventa" velocidades, te desviaste.
2. **Tres piezas separadas, con contratos de archivo entre ellas:**
   - **el grabador** (dentro del juego): graba la toma del humano en un CSV;
   - **el conversor** (fuera del juego, Python): convierte la toma en los archivos de ruta. **Se puede reusar tal cual** (`tools/frame_to_route.py`, `tools/envelope_emit.py`, `tools/frame_merge.py`): no depende de DayZ si el CSV de entrada tiene las mismas columnas;
   - **el runner** (dentro del juego): carga los archivos de ruta y maneja.
   Portar es: escribir el grabador y el runner en C# para 7DTD, y respetar los formatos de E.5.
3. **Medí antes de cambiar.** Cada hito tiene un criterio numérico. Pedile al usuario que corra la prueba y te pase el log. Si el número no da, **no parchees al voleo**: explicá qué creés que pasa, proponé una medición que lo confirme y recién después cambiá código. La regla que más tiempo ahorró en DayZ: *"la hipótesis se falsa con un número, no con una impresión"*.
4. **Un cambio por vez.** Si cambiás dos cosas y mejora, no sabés cuál fue. Si empeora, tampoco.
5. **Logueá todo con un prefijo propio** (`[BZAD]` en el ejemplo) y con números: posición, velocidad, entradas aplicadas, estado. Una línea por segundo de telemetría, más una línea por cada evento (arranque, parada, traba, rescate). Sin eso no hay diagnóstico posible.
6. **La toma es una medición: no se edita.** Si una toma tiene algo raro (un volantazo, una frenada en falso), se regraba. Tapar con edición lo que es un problema del controlador es la trampa más cara.
7. **El usuario prueba; vos no podés.** Cada vez que termines algo, decile exactamente qué correr, qué tiene que ver en el log y qué número indica éxito.

### E.1 — Qué se porta y qué no

| Pieza | En DayZ (este framework) | En 7 Days to Die | ¿Se porta? |
|---|---|---|---|
| **Idea: reproducir manejo grabado por distancia** | la cinta (`BZCintaRunner`) | igual | ✅ es el corazón: portar la lógica (E.6) |
| **Formato de archivos** | `frame_*.csv`, `_wp.csv`, `_cinta.csv`, `_hdr.json`, `.json` | los mismos | ✅ tal cual (E.5) |
| **Conversor** | Python (`tools/frame_to_route.py` + `envelope_emit.py`) | el mismo | ✅ tal cual; ignorar campos de DayZ (E.5.5) |
| **Superficie de control** | `modded class CarScript` → `OnInput()`: acelerador, freno, volante, marcha | `EntityVehicle.movementInput` (`moveForward` −1..1, `moveStrafe` −1..1, `jump` = freno, `running` = torque completo), escrito en un `Prefix` de Harmony sobre `EntityVehicle.PhysicsFixedUpdate` | ✅ distinto mecanismo, misma idea (E.3) |
| **Chofer** | un bot de eAI (Expansion) sentado | un `EntityAlive` sentado en el asiento 0 (`npcTraderJoel` funciona). **Hace falta**: sin él el motor no arranca | ✅ (E.2, obstáculo 5) |
| **Autoridad de la física** | el server | el server, **mientras no haya un humano en el asiento 0** | ✅ (E.2) |
| **Volante: comando → ángulo de rueda** | medido (`stx2`, "ganancia de planta" ~85) | **se lee**: `wheelDir = moveStrafe × SteerAngleMax` (32 en el Truck4x4) | ✅ más fácil |
| **Distancia entre ejes** | configurada o medida | **se mide** de los `WheelCollider` (2,85 m en el Truck4x4) | ✅ más fácil |
| **Marchas** | caja con marchas, reversa = marcha 0 | no hay caja: `moveForward` negativo es reversa | ⚠ simplificar: ignorar `gear` salvo para saber el sentido |
| **Envelope (lo que se mide de cada vehículo)** | se aprende de las tomas (curvatura, frenada, arrastre, piso de gas) | buena parte **se lee** de `Vehicle`: `MotorTorqueForward/Backward`, `VelocityMaxForward/Backward`, `SteerAngleMax`, `SteerRate`, `BrakeTorque` | ⚠ opcional al principio; útil después para frenar bien |
| **Mundo cargado bajo el vehículo** | siempre | **no**: hay que mantener un observador de chunks **con malla** alrededor del vehículo | ⚠ obligatorio (E.2, obstáculo 3) |
| **Sondeo de obstáculos** | `GetObjectsAtPosition3D` | `Physics.OverlapBox` / `Physics.Raycast` de Unity | ✅ |
| **Rescates (AutoRecovery)** | escalera: alejarse, re-ubicar, zonas seguras | igual | ✅ (E.6.4) |
| **Red vial** | no hace falta (la toma ES la ruta) | no hace falta. Si algún día querés rutas generadas: `splat3_processed.png` (E.7) | opcional |
| **Eventos, misiones, panel, RPC, luces, eAI** | Expansion / Quests / UI propia | — | ❌ no portar: son del consumidor o de DayZ |
| **Gotchas de Enforce, PBO, firmas** | §12 | — | ❌ no aplican |

### E.2 — Anatomía de 7DTD que importa (verificado decompilando `Assembly-CSharp.dll`)

**Modding.** 7DTD carga mods DLL con **Harmony oficial** (`Mods/0_TFP_Harmony`, HarmonyX). La assembly del juego viene **publicizada**: los miembros privados son públicos (marcados `[PublicizedFrom(EAccessModifier.Private)]`), así que no hace falta reflexión. **Los mods DLL exigen EAC (anticheat) apagado** (`ModInfo`: `SkipWithAntiCheat`). El punto de entrada es una clase que implementa `IModApi` (`InitMod(Mod)`).

**La superficie de control.** `EntityVehicle.movementInput` (tipo `MovementInput`) tiene `moveForward` (−1..1), `moveStrafe` (−1..1), `jump` (freno) y `running` (turbo/torque completo). El input del jugador entra por un solo lugar (`MoveByAttachedEntity`) que sólo copia a esos campos. En cada `FixedUpdate`, `EntityVehicle.PhysicsFixedUpdate()` los lee:
```
EntityVehicle.PhysicsFixedUpdate()
  ├─ [Prefix del mod]  <- escribir movementInput.moveForward / moveStrafe / jump / running
  ├─ wheelMotor = movementInput.moveForward
  ├─ wheelDir  <- MoveTowards(wheelDir, moveStrafe * SteerAngleMax, rate)   (con lastInputController = true)
  └─ SetWheelsForces(...)
```
Alternativa sin Harmony: `PhysicsInputMove()` es un virtual vacío que se llama en cada `FixedUpdate` (sólo lo sobreescriben el dirigible y el helicóptero). Se puede heredar de `EntityVehicle`, sobreescribirlo y registrar la subclase con `<property name="Class" value="..."/>` en `entityclasses.xml` (`EntityClass` hace `Type.GetType(Properties["Class"])`).

**Autoridad.** El vehículo **hereda** `isEntityRemote` de quien ocupa el asiento 0 (`Entity.AttachToEntity`): con un jugador manejando, simula el cliente; **sin un humano al volante, simula el server**. Un chofer NPC del lado del server es la configuración natural del motor. ⚠ Si un jugador se sube al asiento 0, el server deja de simular: el runner tiene que chequear `isEntityRemote` y no mandar nada si es `true`.

**NPC.** Jerarquía `EntityAlive → EntityNPC → EntitySurvivor / EntityTrader`. El asiento acepta **cualquier `EntityAlive`** (`StartAttachToEntity(vehiculo, 0)` no chequea que sea jugador y replica por `NetPackageEntityAttach`). ⚠ `npcSurvivorTemplate` / `npcSurvivorRanged` están en `entityclasses.xml` **pero no quedan registrados** en `EntityClass.list`; sí funcionan `npcTraderJoel` y cualquier `zombie*`. Para IA propia hay dos sistemas extensibles desde XML con clases de un DLL: **EAI** (`AITask-N` → `Type.GetType("EAI" + nombre)`) y **Utility AI** (`utilityai.xml`). No hacen falta para manejar: el NPC **habilita** (enciende el motor), no maneja.

**Chunks: el mundo tiene que estar cargado CON colisión.** Un observador de chunks tiene un flag `_bBuildVisualMeshAround`: sólo con `true` se construye la malla (`doCopyChunksToUnity` es el único lugar que pone `IsCollisionMeshGenerated = true`). `Entity.bIsChunkObserver = true` crea un observador con ese flag en `false`: **alcanza para zombies (que caminan sobre voxels) pero NO para un vehículo**, que necesita `WheelCollider` contra geometría real. Medido: con `false`, `IsChunkAreaLoaded = True` pero `IsChunkAreaCollidersLoaded = False`, y el vehículo cae al vacío y el motor lo mata (`VehicleManager.RemoveTrackedVehicle ... Killed`). El server dedicado sí construye mallas, con más presupuesto que el cliente (`IsDedicatedServer ? 25000 : 2500` µs por cuadro).

**Los obstáculos que resolvió la prueba de concepto, en el orden en que aparecen** (ninguno es obvio; cada uno cuesta horas si no se sabe):

| # | Síntoma | Causa | Arreglo |
|---|---|---|---|
| 1 | `NullReferenceException` al escribir el input | `movementInput` es `null` en el server dedicado: sólo se instancia para el jugador local (`EntityVehicle.AttachEntityToSelf`) | `if (veh.movementInput == null) veh.movementInput = new MovementInput();` |
| 2 | el vehículo no se mueve aunque el input esté bien | con `RBActive = false`, `PhysicsFixedUpdate` hace `PhysicsResetAndSleep()` + `isKinematic = true` y sale | `veh.RBActive = true; veh.vehicleRB.isKinematic = false; veh.vehicleRB.WakeUp();` |
| 3 | el vehículo se cae al vacío y muere | no hay malla de colisión: ver "Chunks" arriba | observador propio con `AddChunkObserver(pos, true, 3, -1)` que se re-crea al cambiar de chunk, y **no activar el rigidbody hasta que `world.IsChunkAreaCollidersLoaded(pos)` sea `true`** |
| 4 | sin conductor, el motor frena y duerme el vehículo | a propósito: `velocity.x *= 0.98f` por tick y `RBActive = false` tras 3 s casi quieto | forzar `RBActive` en cada tick mientras maneja el runner (obstáculo 2) y **sentar un chofer** (obstáculo 5) |
| 5 | el motor no arranca | `IsEngineRunning` sólo lo prende `VPEngine.startEngine()` vía `Vehicle.Event.Start`, que se emite con `AttachEntityToSelf` cuando **alguien ocupa el asiento 0**. Forzar `hasDriver = true` **no sirve**: `UpdateAttachment()` hace `if (hasDriver && attachedMainEntity == null) DriverRemoved();` | sentar un `EntityAlive` real: `EntityFactory.CreateEntity(...)` → `world.SpawnEntityInWorld(npc)` → `npc.StartAttachToEntity(veh, 0)` |
| 6 | `wheelMotor` llega bien pero `wheelDir = 0` y `torque% = 0` | **bug del juego, sólo server-side**: `EntityVehicle.isTurnTowardsLook` es `public static` y arranca en `true`; en ese modo `PhysicsFixedUpdate` usa `GetAttachedPlayerLocal().vp_FPCamera`, que en un dedicado es `null` → NRE en cada tick, justo después de asignar `wheelMotor` | al iniciar el mod: `EntityVehicle.isTurnTowardsLook = false;` |
| 7 | NRE en vehículos de menos de 4 ruedas (bici, minibike, moto) | **segundo bug server-side**: `if (hasDriver && wheels.Length < 4 && GetAttachedPlayerLocal().isPlayerInStorm)` | transpiler de Harmony que cambia el acceso a `isPlayerInStorm` por un helper null-safe (E.3, `Patches.cs`) |

Además: `movementInput.lastInputController = true` hace que el volante se mapee **directo** (`wheelDir = MoveTowards(wheelDir, moveStrafe × SteerAngleMax, rate)`), que es lo que necesita un controlador; y **`running = true`**, porque sin eso el motor hace `wheelMotor *= 0.5`.

**Detalles prácticos del banco de pruebas:**
- **Compilar sin Visual Studio ni SDK:** Roslyn nativo de Windows (`csc.exe`, variante **net472** del paquete NuGet `Microsoft.Net.Compilers.Toolset`; la variante `bincore` de Roslyn 4.14 pide .NET 9). Referencias obligatorias no obvias: `LogLibrary.dll` (la clase `Log`), `Mods/0_TFP_Harmony/0Harmony.dll`, `UnityEngine.VehiclesModule.dll` (para `WheelCollider`), además de `Assembly-CSharp.dll`, `UnityEngine.CoreModule.dll` y `UnityEngine.PhysicsModule.dll`.
- ⚠ **`Assembly-CSharp.dll` del server dedicado NO es la misma que la del cliente** (el build de server saca render y UI; los hashes difieren). Para un mod server-side, **compilá contra la del server dedicado**; compilar contra la del cliente puede dar `TypeLoadException` en runtime.
- **El DLL va en la RAÍZ** de `Mods/<TuMod>/` (la carga no es recursiva), junto al `ModInfo.xml`.
- **Server dedicado:** se descarga aparte (Steam → Herramientas → *7 Days to Die Dedicated Server*, app 294420). Para probar sin abrir el juego: telnet a `127.0.0.1:8081` (con `TelnetPassword` vacío escucha sólo en loopback).
- `VehicleManager.Update()` sale enseguida si `world.Players.Count == 0`: conviene tener un jugador conectado para algunas pruebas. **Pero si el jugador se sube al vehículo, la autoridad pasa a su cliente.**
- `EntityClass.FromString(s)` es sólo `s.GetHashCode()`: **puede ser negativo**. Validá con `EntityClass.list.ContainsKey(hash)`, nunca con `hash <= 0`.
- Un `vehicleTruck4x4` spawneado por consola viene completo: motor, dirección, 8000 hp, combustible 400/400, `MotorTorqueForward = 3500`, `VelocityMaxForward = 10`, `SteerAngleMax = 32`. Es el mejor vehículo para empezar.
- **Coordenadas del mapa (Navezgane, verificado):** `columna = x_mundo + WorldSize/2`, `fila = z_mundo + WorldSize/2`; altura = `dtm_processed.raw` (uint16 little-endian, 6144×6144) `/ 256`. Sirve para elegir un tramo plano de prueba sin abrir el juego.

### E.3 — El código de la prueba de concepto (C#, 7DTD, server dedicado)

Es el código que manejó solo un `vehicleTruck4x4` en Navezgane, en un server dedicado, el 12/08/2026. **Es la base recomendada para empezar:** resuelve todos los obstáculos de E.2 y trae el pure pursuit portado y medido (H1). Lo que **no** tiene: la cinta, las paradas grabadas, la reversa, el control de velocidad bueno (el suyo es un proporcional que oscila) y los rescates de verdad (el suyo es "reversa 2 s"). Eso es lo que falta portar, con E.5 y E.6.

Estructura:
```
src/ModApi.cs             entrada IModApi: aplica Harmony, apaga isTurnTowardsLook
src/Patches.cs            Prefix sobre PhysicsFixedUpdate (inyección) + transpiler null-safe + diagnóstico de bajas y daño
src/AutoDriveManager.cs   registro de pilotos (server-side), observadores de chunks
src/AutoDrivePilot.cs     el piloto: guardas de física, burbuja de chunks, control, telemetría
src/PurePursuit.cs        el pure pursuit del framework, portado y medido
src/Route.cs              polilínea simple (círculo, recta, JSON)
src/ConsoleCmdBzDrive.cs  comando de consola 'bzdrive' (list / spawn / driver / circle / line / route / speed / status / stop)
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
    /// Punto de entrada del mod. 7DTD instancia toda clase que implemente IModApi
    /// y llama InitMod (ver Mod.InitModCode en Assembly-CSharp).
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

            // Ver la nota en AutoDrivePilot: con esto en true, PhysicsFixedUpdate tira
            // NullReferenceException en cada tick en un server dedicado.
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
    /// <summary>Helpers null-safe que inyecta el transpiler.</summary>
    public static class BZSafe
    {
        public static bool InStorm(EntityPlayerLocal _p)
        {
            return _p != null && _p.isPlayerInStorm;
        }
    }

    /// <summary>
    /// SEGUNDO bug del motor solo visible server-side. En PhysicsFixedUpdate:
    ///
    ///     if (hasDriver &amp;&amp; wheels.Length &lt; 4 &amp;&amp; GetAttachedPlayerLocal().isPlayerInStorm)
    ///
    /// GetAttachedPlayerLocal() busca un EntityPlayerLocal entre los ocupantes, y en un
    /// server dedicado NUNCA existe uno -> devuelve null -> NRE con cualquier vehiculo de
    /// menos de 4 ruedas (bici, minibike, moto: o sea, casi todos).
    ///
    /// El transpiler cambia el acceso al campo isPlayerInStorm por BZSafe.InStorm(), que
    /// tolera el null. Es el cambio minimo posible: una sola instruccion IL.
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
    /// Punto de inyeccion del actuador.
    ///
    /// EntityVehicle.PhysicsFixedUpdate() es donde el motor LEE movementInput
    /// (wheelMotor = movementInput.moveForward; wheelDir <- movementInput.moveStrafe ...).
    /// Un Prefix nos deja escribirlo justo antes de esa lectura, un tick completo
    /// antes de que se apliquen fuerzas a las ruedas.
    ///
    /// Ademas, leer wheelMotor/wheelDir DESDE el prefix devuelve los valores del tick
    /// anterior (el motor los pone a cero al principio de PhysicsFixedUpdate), asi que
    /// la telemetria confirma que el input efectivamente llego al motor.
    ///
    /// Alternativa sin Harmony: heredar de EntityVehicle y sobreescribir el virtual
    /// vacio PhysicsInputMove(), registrando la subclase con
    /// <property name="Class" value="..."/> en entityclasses.xml.
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

    /// <summary>Diagnostico: por que desaparecio el vehiculo.</summary>
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

            // quien lo mato: esto es lo unico que lo dice sin ambiguedad
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

    /// <summary>Diagnostico: si el vehiculo esta recibiendo dano, de donde sale.</summary>
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
    /// <summary>Registro de vehiculos bajo control autonomo. Todo server-side.</summary>
    public static class AutoDriveManager
    {
        private static readonly Dictionary<int, AutoDrivePilot> pilots = new Dictionary<int, AutoDrivePilot>();
        private static readonly List<ChunkManager.ChunkObserver> observers = new List<ChunkManager.ChunkObserver>();

        /// <summary>
        /// Fuerza la carga de chunks alrededor de una posicion. Sin esto, en un server
        /// sin jugadores conectados no hay terreno cargado: no hay malla de colision,
        /// y un vehiculo spawneado ahi se cae al vacio.
        /// Mismo mecanismo que usa el airdrop (AIDirectorAirDropComponent).
        /// </summary>
        public static void AddObserver(Vector3 _pos, int _viewDim = 3)
        {
            // _bBuildVisualMeshAround: TRUE es obligatorio.
            // Con false el motor carga los DATOS del chunk pero nunca construye la malla
            // (ChunkManager: solo los chunks en m_ViewingChunkPositions llegan a
            // doCopyChunksToUnity, que es donde se setea IsCollisionMeshGenerated).
            // Sin malla no hay collider, y sin collider un vehiculo no tiene piso.
            // Por eso el flag de las hordas errantes (bIsChunkObserver -> false) alcanza
            // para zombies, que pathean sobre voxels, pero NO para vehiculos.
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

            // NO alcanza con _veh.bIsChunkObserver = true: ese observador se crea con
            // _bBuildVisualMeshAround=false y no genera colision. El piloto mantiene
            // su propia burbuja con malla (AutoDrivePilot.UpdateObserver).

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

        /// <summary>Llamado desde el Prefix de EntityVehicle.PhysicsFixedUpdate.</summary>
        public static void OnPhysicsTick(EntityVehicle _veh)
        {
            if (pilots.Count == 0) return;
            AutoDrivePilot p;
            if (!pilots.TryGetValue(_veh.entityId, out p)) return;

            // solo conducimos donde se simula de verdad
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

        // --- utilidades de busqueda ---

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
    /// Controlador de seguimiento de ruta para un EntityVehicle.
    ///
    /// OJO: este seguidor es DELIBERADAMENTE simple. El objetivo de la v0.1 no es
    /// conducir bien, es responder una sola pregunta: el server dedicado, sin
    /// conductor humano, simula la fisica del vehiculo y llega el input al motor?
    /// El pure-pursuit real del framework reemplaza a StepControl() sin tocar nada mas.
    /// </summary>
    public class AutoDrivePilot
    {
        public readonly EntityVehicle Veh;
        public Route Route;

        public int   waypointIdx;
        public bool  finished;
        public float lookaheadBase = 8f;    // m
        public float lookaheadPerSpeed = 0.6f;  // m por m/s

        // ultimo comando emitido (para telemetria)
        public float lastThrottle, lastSteer;
        public bool  lastBrake;

        // telemetria
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

        /// <summary>Se llama desde el Prefix de EntityVehicle.PhysicsFixedUpdate, en el server.</summary>
        public void OnPhysicsTick(float _dt)
        {
            ticks++;
            UpdateObserver();

            // ---- guarda: no activar la fisica sin piso ----
            // Si activamos el rigidbody antes de que el chunk tenga malla de colision,
            // el vehiculo cae al vacio y el motor lo mata. Esperamos.
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
                return;   // no tocamos nada hasta que haya piso
            }
            if (esperandoSuelo > 0)
            {
                Log.Out("[BZAD] id=" + Veh.entityId + " colliders listos tras " + esperandoSuelo + " ticks");
                esperandoSuelo = 0;
                // mientras esperabamos pudo haberse hundido: lo reapoyamos sobre el terreno
                float h = GameManager.Instance.World.GetHeightAt(Veh.position.x, Veh.position.z);
                if (h > 0f && Veh.position.y < h + 0.5f)
                {
                    Veh.SetPosition(new Vector3(Veh.position.x, h + 1.5f, Veh.position.z));
                    Log.Out("[BZAD] id=" + Veh.entityId + " reapoyado en y=" + (h + 1.5f).ToString("0.0"));
                }
            }

            // El motor cortocircuita PhysicsFixedUpdate si RBActive es false
            // (PhysicsResetAndSleep + isKinematic = true). Lo forzamos.
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

            // En server dedicado movementInput NUNCA se instancia (solo se crea para
            // el jugador local en EntityVehicle.AttachEntityToSelf). Lo creamos nosotros.
            if (Veh.movementInput == null)
            {
                Veh.movementInput = new MovementInput();
                Log.Out("[BZAD] movementInput creado para vehiculo " + Veh.entityId);
            }

            // ---- BUG del motor, solo visible server-side ----
            // EntityVehicle.isTurnTowardsLook es static y arranca en TRUE. En ese modo
            // PhysicsFixedUpdate hace GetAttachedPlayerLocal().vp_FPCamera, que en un
            // server dedicado es NULL -> NullReferenceException en CADA tick, y el metodo
            // aborta antes de calcular wheelDir y de llamar SetWheelsForces.
            // Sintoma: wheelMotor llega bien pero wheelDir=0 y torque=0, el vehiculo no se mueve.
            if (EntityVehicle.isTurnTowardsLook)
            {
                EntityVehicle.isTurnTowardsLook = false;
                Log.Warning("[BZAD] isTurnTowardsLook estaba en true (NRE server-side) -> apagado");
            }

            // Con lastInputController el volante mapea directo:
            //   wheelDir = MoveTowards(wheelDir, moveStrafe * SteerAngleMax, rate)
            // que es justo lo que necesita un controlador, en vez del modo incremental.
            Veh.movementInput.lastInputController = true;

            StepControl(_dt);

            Veh.movementInput.moveForward = lastThrottle;
            Veh.movementInput.moveStrafe  = lastSteer;
            Veh.movementInput.jump        = lastBrake;   // 'jump' es el freno en vehiculos
            Veh.movementInput.running     = fullTorque;  // sin esto el motor hace wheelMotor *= 0.5

            // los primeros ticks se loguean todos: ahi es donde se muere
            telemetryAccum += _dt;
            if (ticks <= 5 || telemetryAccum >= telemetryEverySec)
            {
                telemetryAccum = 0f;
                LogTelemetry();
            }
        }

        // ------------------------------------------------------------------
        // Seguidor: pure-pursuit portado del framework de DayZ + perfil de
        // velocidad por curvatura + detector de atasco.
        // ------------------------------------------------------------------
        public readonly PurePursuit PP = new PurePursuit();
        public bool  fullTorque = true;
        public float latAccel = 3.0f;      // m/s2 lateral admisible -> v_max = sqrt(a/|k|)
        public float speedKp  = 0.6f;

        // atasco
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

            // ---- maniobra de desatasco ----
            // Sin esto el vehiculo empuja contra el obstaculo con el acelerador a fondo, y el
            // UnstickForce del motor lo termina TREPANDO (paso: se subio a un arbol).
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

            // ---- volante: pure-pursuit ----
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

            // ---- velocidad objetivo: la de la ruta, acotada por la curvatura de adelante ----
            float want = Route.targetSpeed;
            float kAhead = Mathf.Abs(PP.LastCurvature);
            if (kAhead > 0.001f)
            {
                float vCurve = PurePursuit.SpeedForCurvature(kAhead, latAccel);
                if (vCurve < want) want = vCurve;
            }
            if (want < 1.5f) want = 1.5f;

            // OJO: el error va con la velocidad CON SIGNO. Con |v| el controlador ve
            // "voy rapido" cuando en realidad va rapido HACIA ATRAS, corta el acelerador
            // y se queda en un ciclo limite yendo y viniendo (medido 12/08/2026).
            float err = want - speed;
            lastThrottle = Mathf.Clamp(err * speedKp, -1f, 1f);
            lastBrake = false;
            if (speed > want * 1.4f) { lastThrottle = 0f; lastBrake = true; }
        }

        /// <summary>
        /// Distancia entre ejes MEDIDA de los WheelCollider. En DayZ habia que configurarla
        /// por vehiculo; aca sale de la geometria real.
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
        // Burbuja de chunks CON malla que sigue al vehiculo.
        // No sirve bIsChunkObserver: ese crea el observador con
        // _bBuildVisualMeshAround=false y nunca se genera la colision.
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
        // Telemetria: esto es lo que responde la pregunta abierta.
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
    /// Pure-pursuit portado del framework de DayZ (BZBusService.ComputePurePursuit).
    /// Se porta la MATEMATICA con sus correcciones ganadas a fuerza de medir; nada de Enforce.
    ///
    /// Diferencia clave de unidades respecto de DayZ:
    ///   DayZ  -> cmd = deg_rueda / PlantSteerGain, con PlantSteerGain=85.1 IDENTIFICADO midiendo.
    ///   7DTD  -> el motor hace wheelDir = moveStrafe * SteerAngleMax, asi que la ganancia de planta
    ///            ES SteerAngleMax y se LEE del config del vehiculo (32 en el Truck4x4).
    /// El wheelbase tambien sale medido de los WheelCollider en vez de configurarse a mano.
    /// </summary>
    public class PurePursuit
    {
        // --- parametros (mismos conceptos que la version DayZ) ---
        public float LdStraightM = 8.5f;      // Ld en recta. El Ld LARGO es el lado estable.
        public float LdCurveM = 4.5f;         // Ld en curva cerrada: clava el apex, no anticipa
        public float LdMinM = 2.0f;
        public float LdSpeedTimeS = 0.4f;     // piso Ld >= 0.4*v (steerAhead estilo Arma)
        public bool  UseCurvatureAdaptiveLd = true;
        public float CurvatureSenseM = 12.0f; // cuanto adelante sensar el giro de la traza
        public float CurvatureTurnRad = 0.6f; // giro acumulado que satura el acortamiento
        public float LdSpeedFactor = 0.55f;   // Ld ~ proporcional a v en recta (tau = Ld/v ~ cte)
        public float CurveComp = 1.0f;        // compensacion de corte por dentro del arco
        public float CurveCompMaxSpeedMs = 12.0f;
        public float Gain = 1.0f;
        public float LpureFloorM = 3.0f;      // piso de la distancia real al aim

        // --- estado ---
        public int NearIdx;

        // --- diagnostico ---
        public float LastLd, LastAlphaDeg, LastDistT, LastKappa, LastDeltaDeg, LastCurvature;
        public int   LastAimIdx;
        public bool  LastSaturated;

        /// <summary>Comando de volante normalizado [-1,1] para movementInput.moveStrafe.</summary>
        public float Compute(Route route, Vector3 pos, float yawRad, float speedMs,
                             float steerAngleMax, float wheelbase)
        {
            if (route == null || route.Count < 2) return 0f;
            int count = route.Count;

            // 1) waypoint mas cercano, buscando en una ventana alrededor del ultimo
            NearIdx = FindNearest(route, pos, NearIdx);

            // 2) distancia de lookahead
            float v = Mathf.Max(speedMs, 1.0f);
            float Ld = Mathf.Max(LdStraightM, LdSpeedTimeS * v);
            if (Ld < LdMinM) Ld = LdMinM;

            float fracCurve = 0f;
            if (UseCurvatureAdaptiveLd)
            {
                float totalTurn = SenseTurn(route, NearIdx, CurvatureSenseM);
                fracCurve = Mathf.Clamp01(totalTurn / Mathf.Max(CurvatureTurnRad, 0.01f));
                Ld = LdStraightM - fracCurve * (LdStraightM - LdCurveM);

                // Ld crece con v SOLO en recta: mantiene tau=Ld/v constante y promedia el jitter
                // de la traza. En curva manda el ldCurve corto.
                if (LdSpeedFactor > 0f)
                {
                    float floorV = LdSpeedFactor * v * (1f - fracCurve);
                    if (floorV > Ld) Ld = floorV;
                }
                if (Ld < LdMinM) Ld = LdMinM;
            }
            LastLd = Ld;

            // 3) caminar Ld metros sobre la traza para sacar el punto de mira
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

            // 3b) compensacion de corte: el pursuit corta POR DENTRO del arco. Corro el objetivo
            // hacia afuera la sagita ~|k|*Ld^2/8. El lado "afuera" es donde bombea el arco,
            // sin depender del signo de la curvatura.
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

            // 4) alpha = angulo del rumbo actual al objetivo
            float dx = target.x - pos.x;
            float dz = target.z - pos.z;
            float distT = Mathf.Sqrt(dx * dx + dz * dz);
            LastDistT = distT;
            if (distT < 0.3f) return 0f;

            float targetHeading = Mathf.Atan2(dx, dz);
            float alpha = Mathf.DeltaAngle(yawRad * Mathf.Rad2Deg, targetHeading * Mathf.Rad2Deg) * Mathf.Deg2Rad;
            LastAlphaDeg = alpha * Mathf.Rad2Deg;

            // 5) curvatura -> angulo de rueda -> comando
            // OJO (esto costo caro en DayZ): la forma canonica es kappa = 2*sin(alpha)/L con L la
            // distancia REAL al punto de mira, NO el Ld deseado. Si el aim se pega (fin de ruta,
            // baja velocidad) y uno usa Ld, la formula miente y 1/L infla la ganancia ~3x ->
            // volantazo al tope con el objetivo casi de frente. De ahi el piso.
            float Lpure = distT;
            if (Lpure < Ld) Lpure = Ld;
            if (Lpure < LpureFloorM) Lpure = LpureFloorM;

            float kappa = 2f * Mathf.Sin(alpha) / Lpure;
            LastKappa = kappa;

            float deltaRad = Mathf.Atan(wheelbase * kappa);
            LastDeltaDeg = deltaRad * Mathf.Rad2Deg;

            // ganancia de planta = SteerAngleMax, LEIDA del vehiculo (en DayZ habia que medirla)
            float gainDeg = (steerAngleMax > 1f) ? steerAngleMax : 30f;
            float cmd = (LastDeltaDeg / gainDeg) * Gain;

            LastSaturated = (cmd > 0.95f || cmd < -0.95f);
            return Mathf.Clamp(cmd, -1f, 1f);
        }

        // ------------------------------------------------------------------

        private static Vector3 Flat(Vector3 v) { v.y = 0f; return v; }

        /// <summary>Waypoint mas cercano, en ventana alrededor del ultimo (evita saltar de tramo).</summary>
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

        /// <summary>Giro total (rad) que acumula la traza en los proximos _senseM metros.</summary>
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

        /// <summary>Curvatura de Menger en el waypoint i, usando sus vecinos.</summary>
        public static float LocalCurvature(Route route, int i)
        {
            Vector3 a = Flat(route[i - 1]);
            Vector3 b = Flat(route[i]);
            Vector3 c = Flat(route[i + 1]);
            float ab = Vector3.Distance(a, b);
            float bc = Vector3.Distance(b, c);
            float ca = Vector3.Distance(c, a);
            if (ab < 0.01f || bc < 0.01f || ca < 0.01f) return 0f;
            // area con signo (plano XZ)
            float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
            float area = cross * 0.5f;
            return 4f * area / (ab * bc * ca);
        }

        /// <summary>Velocidad maxima para una curvatura dada, por aceleracion lateral admisible.</summary>
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
    /// Polilinea de ruta en coordenadas de mundo. Formato deliberadamente plano
    /// para que sea intercambiable con las tomas del framework de DayZ.
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

        /// <summary>Circulo centrado en _center. Ruta de prueba autonoma, sin archivos.</summary>
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

        /// <summary>Recta de _length metros desde _from en direccion _dir.</summary>
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

        // --- serializacion ---

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

            // La Y del archivo es solo referencia (sale del heightmap en disco, que puede
            // traer stamps de POI). La autoridad es el mundo cargado. El seguidor trabaja
            // en XZ igual, asi que esto es para diagnostico y para render.
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

#### `ConsoleCmdBzDrive.cs` — spawnear el vehículo y sentar al chofer (extracto)
```csharp
    private void CmdSpawn(List<string> p)
    {
        if (p.Count < 2) { SdtdConsole.Instance.Output("falta la clase, ej: bzdrive spawn vehicleMinibike"); return; }
        string cls = p[1];
        // OJO: EntityClass.FromString es solo _s.GetHashCode() -> puede ser negativo.
        // La validacion real es que la clase exista en el diccionario.
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

        // sin jugadores no hay chunks cargados: forzamos su carga antes de spawnear
        AutoDriveManager.AddObserver(pos);

        // apoyarlo sobre el terreno en vez de dejarlo caer desde donde sea
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
        e.bIsChunkObserver = true;   // que arrastre su propia burbuja
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

**El protocolo de prueba de H1** (consola del server): `bzdrive load <x> <y> <z>` (fuerza la carga de chunks; esperar ~10 s) → `bzdrive spawn vehicleTruck4x4 <x> <y> <z>` → `bzdrive list` (confirmar `ruedas` > 0) → `bzdrive driver <id> npcTraderJoel` → `bzdrive circle <id> 12` → leer la telemetría `[BZAD]` una vez por segundo → `bzdrive stop`.

**Cómo leer la telemetría:** `remote=False` (el server simula; con `True` no se simula acá) · `RBActive=True kinematic=False` (rigidbody vivo) · `ruedas>0` (hay piso con colisión) · `mov/s > 0` (se mueve de verdad) · `IN thr` ≈ `OUT wheelMotor` (el input llega al motor) · `fwdVel` (velocidad real).

### E.4 — El orden: hitos con criterio de "hecho"

Cada hito tiene un **criterio medible en el juego**. No avances sin cumplirlo. Los números salen de los que se midieron en DayZ y en la prueba de concepto de 7DTD; ajustalos si el juego da otra cosa, pero **con una medición que lo justifique**.

**H0 — El entorno.** Server dedicado instalado, mod que carga (en el log: `[BZAD] ... iniciando`, `parches Harmony aplicados`), consola por telnet, un comando propio (`bzdrive` en el ejemplo) para spawnear, sentar al chofer, arrancar y detener.
*Hecho:* el log muestra la carga del mod y el comando responde.

**H1 — Manejar un círculo (la prueba de concepto de E.3).** Spawnear un `vehicleTruck4x4` en llano, sentar un `npcTraderJoel`, seguir un círculo de 12 m de radio con el pure pursuit de E.3.
*Hecho (lo que dio la prueba de concepto):* radio ajustado 11,91 m sobre 12,00; error de seguimiento en régimen **medio 0,07 m · RMS 0,10 m · máximo 0,37 m**; en la telemetría `remote=False RBActive=True ruedas=4 driver=True` e `IN thr` ≈ `OUT wheelMotor`.

**H2 — El grabador.** Mientras un jugador maneja, grabar a **40 Hz** un `frame_<fecha>_<clase>.csv` con las columnas exactas de E.5.1, más el `header_<fecha>_<clase>.txt` (E.5.2). Una tecla marca eventos (`is_marker`).
*Hecho:* una vuelta de 2 minutos da un CSV con `t_cum` creciente sin saltos de más de 0,1 s; la distancia que suman los `x, z` coincide con la velocidad integrada (`Σ speed_kmh/3,6 × dt`) con menos de 3 % de diferencia; la traza dibujada sobre el mapa sigue la calle; `heading` coincide con la dirección del movimiento (diferencia mediana < 5° a más de 10 km/h, yendo hacia adelante).
⚠ **Dónde grabar:** mientras el jugador maneja, la física la simula **su cliente** (autoridad por el asiento 0). Grabá en el cliente del que maneja y copiá el archivo, o grabá en el server lo que el server ve del vehículo (menos preciso). En DayZ se graba en el cliente.

**H3 — La conversión.** Correr el conversor Python tal cual sobre la toma: `python tools/frame_to_route.py <frame_csv> <NOMBRE> --profile <carpeta>` (E.5.4).
*Hecho:* salen los cuatro archivos (`BZBusRoute_<NOMBRE>.json`, `_hdr.json`, `_wp.csv`, `_cinta.csv`); en el `_hdr.json`, `WpRows` y `CintaRows` coinciden con las filas de cada archivo; en `_cinta.csv`, `s` crece siempre (sin retrocesos) y el último `s` es el largo del recorrido.

**H4 — Reproducir la cinta (el hito central).** Cargar `_hdr.json` + `_wp.csv` + `_cinta.csv`, spawnear en el wp 0 con el rumbo grabado, y manejar con la lógica de E.6: cursor por distancia, feed-forward del volante grabado + corrector lateral, velocidad objetivo de la cinta.
*Hecho, sobre una toma de 1-3 km sin paradas:* error lateral respecto de la traza grabada **p90 < 0,5 m, máximo < 1,5 m**; tiempo de recorrido dentro de **±5 %** del grabado; llega al final a **menos de 1 m** del último punto. Todo eso medido con una línea de telemetría por segundo (`wp`, `s` del cursor, `lat`, velocidad real vs grabada).

**H5 — Paradas grabadas y cambios de sentido.** Una toma con dos paradas (el humano se detuvo 5 s) y una maniobra con reversa (K-turn o entrar de cola).
*Hecho:* el vehículo se detiene **donde** se detuvo el humano (±0,5 m) y **el tiempo** grabado (±1 s); la reversa se hace en el tramo que corresponde y cierra el cambio de sentido sin quedarse esperando (en DayZ, 4-5 s detenido en total en un cambio de sentido con parada grabada).

**H6 — No quedarse detenido (AutoRecovery).** Cortar la ruta con un vehículo estacionado y, en otra prueba, con un objeto fijo.
*Hecho:* la traba se detecta **por falta de avance** en ≤ 10-20 s (no por velocidad), el rescate empieza y **no choca contra lo que tiene detrás**; la ruta se da por perdida **sólo** si el rescate se agotó; una parada grabada **nunca** dispara un rescate; cero choques nuevos en una ruta libre.

**H7 — Robustez de servicio (opcional, para un servicio con varios vehículos).** Varios runners a la vez, el punto de salida ocupado (el segundo **espera**, no aparece corrido), la carga de la ruta repartida en varios cuadros (ningún cuadro del server se estira), y una API de estado para tu mod (bloqueado, recuperándose, choque reciente): ver el Apéndice B.7 del manual para la de DayZ.

### E.5 — Los formatos de archivo (el contrato entre las tres piezas)

Si el grabador de 7DTD escribe **exactamente** estas columnas, el conversor Python de DayZ se reusa sin tocar una línea. Lo que es recomendación de portado, y no algo que hace el código de DayZ, va marcado **[port]**.

#### E.5.0 El flujo completo

```
CLIENTE del que maneja (humano)
  tecla "empezar"  → frame_<ts>_<Clase>.csv  +  header_<ts>_<Clase>.txt
  tecla "marca"    → is_marker=1 en el PRÓXIMO frame (una parada o un evento)
  tecla "terminar"
      ↓ copiar al lugar donde se convierte
[frame_merge.py]    varios frame_ de una ruta grabada en partes → uno solo
[envelope_emit.py]  frame_ → envelope_<Clase>.csv   (opcional; lo que se midió del vehículo)
frame_to_route.py   frame_ + header_ (+ envelope_<Clase>.csv) → BZBusRoute_<R>.json · _hdr.json · _wp.csv · _cinta.csv
      ↓ copiar a la carpeta de rutas del server
SERVER: _hdr.json (JSON chico) + _wp.csv y _cinta.csv leídos por líneas, repartidos en varios cuadros
```

- `<ts>` = `YYYYMMDD-HHMMSS` en **UTC** (no en hora local: acordate al buscar una toma por hora).
- `frame_X.csv` va con `header_X.txt` en **el mismo directorio**. El conversor los empareja cambiando el prefijo `frame_` por `header_` y la extensión por `.txt`.
- **Dónde grabar:** mientras un humano maneja, la física la simula **su cliente**. En DayZ se graba en el cliente y el archivo se copia a mano al server. **[port]** En 7DTD pasa lo mismo mientras el humano está en el asiento 0 (E.2). Grabá en el cliente del que maneja.

#### E.5.1 `frame_*.csv`: la toma (18 columnas, en este orden, con fila de encabezado)

```
t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg,is_marker,pitch_deg,surface,chassis_kmh
```

| col | nombre | unidad | qué es · **[port]** cómo sacarlo en 7DTD |
|---|---|---|---|
| 0 | `t_cum` | s | suma de `dt` desde el inicio; la fila 0 lleva `t_cum = 0`, `dt = 0` (estado inicial) |
| 1 | `dt` | s | paso de física de ESA fila. DayZ: fijo 0,025 (40 Hz). **[port]** Unity usa `Time.fixedDeltaTime` = 0,02 por defecto; ver E.5.5 |
| 2 | `throttle` | 0..1 | acelerador aplicado. **[port]** `max(0, movementInput.moveForward)` yendo adelante |
| 3 | `brake` | 0..1 | freno aplicado. **[port]** `jump ? 1 : 0` (el freno de 7DTD es binario) |
| 4 | `steering` | −1..1 | la TECLA de volante. **No se usa para reproducir** (con teclado es casi binaria): se graba para diagnóstico |
| 5 | `gear` | entero | **0 = reversa, 1 = neutro, 2 = primera**, 3 = segunda… **[port]** sin caja: 0 si va marcha atrás, 2 si va adelante, 1 si está quieto sin acelerar |
| 6 | `handbrake` | 0..1 | freno de mano. **[port]** 0 si el vehículo no lo tiene |
| 7-9 | `x,y,z` | m | posición de mundo, **y = arriba**. **[port]** `vehicle.position` (Unity también es Y arriba) |
| 10 | `heading` | grados | `atan2(fwd.x, fwd.z)` en grados, rango (−180, 180], **0 = +Z, 90 = +X, crece en sentido horario visto desde arriba**. **[port]** `Mathf.Atan2(fwd.x, fwd.z) * Mathf.Rad2Deg` |
| 11 | `speed_kmh` | km/h | velocímetro de RUEDA, en valor absoluto. **[port]** si no hay velocímetro de rueda, la misma que `chassis_kmh` |
| 12 | `rpm` | rpm | del motor. **[port]** 0 si no existe |
| 13 | `front_wheel_deg` | grados | **ángulo REAL de la rueda delantera respecto del chasis**, + = derecha, ya con la rampa del motor aplicada. **Es la columna más importante de la toma**: es la que se reproduce. **[port]** `WheelCollider.steerAngle` de una rueda delantera, o `wheelDir` si lo leés del vehículo; verificá el signo (+ = derecha) |
| 14 | `is_marker` | 0/1/2 | 1 = marca de parada/evento; 2 = corte de tramo manual. La lleva **una sola fila** (la bandera se consume al escribir) |
| 15 | `pitch_deg` | grados | `asin(clamp(fwd.y, −1, 1))` en grados; + = trompa arriba |
| 16 | `surface` | texto | nombre del material bajo el vehículo, o `none`. **Es la única columna de texto** |
| 17 | `chassis_kmh` | km/h | `|velocidad lineal del cuerpo| × 3,6`. **[port]** `rigidbody.velocity.magnitude * 3.6` |

Reglas del grabador:
- **Una fila por paso de física** del vehículo que se está grabando. **[port]** Un `Postfix` sobre `EntityVehicle.PhysicsFixedUpdate` del vehículo del jugador local.
- Si `dt ≤ 0`, no se escribe la fila.
- Buffer en memoria y append al disco cada ~30 filas, y siempre al terminar. Escribir fila por fila frena el juego.
- Números con punto decimal (`3.14`), **nunca coma**: usá `CultureInfo.InvariantCulture` en C#. Un sistema en español escribe `3,14` y rompe el CSV.

#### E.5.2 `header_*.txt`: la ficha del vehículo (una línea `clave=valor` por dato, escrita una vez al empezar)

| clave | obligatoria | qué es · **[port]** |
|---|---|---|
| `vehicleClass` | **sí** | clase del vehículo; también nombra los archivos. **[port]** `EntityClass.list[vehicle.entityClass].entityClassName` (ej. `vehicleTruck4x4`) |
| `wheelbase` | **sí** | distancia entre ejes en m: proyectar el punto de contacto de cada rueda sobre el eje adelante (XZ) y tomar máx − mín. **[port]** de los `WheelCollider` (2,85 m en el Truck4x4) |
| `maxSteeringAngle` | **sí, > 0** | ángulo máximo de la rueda, en grados. **[port]** `Vehicle.SteerAngleMax`. Con 0 el conversor divide por cero |
| `mass` | no (1000) | kg. **[port]** `vehicleRB.mass` |
| `gearsCount` | no (6) | **[port]** 1 |
| `engineRPMIdle`, `engineRPMMax`, `engineRPMRedline` | no | **[port]** 0 |
| `wheelCount`, `wheelCountPresent` | no | cantidad de ruedas |
| `attachmentsCount`, `attachments` | no | piezas instaladas; **[port]** 0 y vacío |

Sin vehículo al empezar, el archivo tiene una sola línea: `NO_VEHICLE_AT_START`.

#### E.5.3 Qué necesita el conversor de verdad

- **Sin esto aborta:** del header, `vehicleClass`, `wheelbase` y `maxSteeringAngle ≠ 0`. De la toma, `x, y, z, heading, speed_kmh, gear`. Una fila que no parsea se descarta en silencio; si se descartan todas, sale "grabación vacía".
- **Sin esto funciona, pero mal:**
  - `t_cum` y `dt`: sin ellos no hay paradas declaradas y la cinta sale con `s` escalonado.
  - `is_marker`: sin él no hay paradas ni eventos.
  - `chassis_kmh`: si falta, el conversor usa `speed_kmh`.
  - `front_wheel_deg`: **sin él no hay cinta útil**.
  - `throttle`, `brake`: los pedales de la cinta.
- **Sólo para el envelope (E.5.8):** `steering`, `rpm`, `pitch_deg`, `handbrake`, `surface`.

#### E.5.4 Correr la conversión

```
python tools/frame_merge.py <NOMBRE> parte1.csv parte2.csv … [--out DIR]          # sólo si la ruta se grabó en partes
python tools/envelope_emit.py <frame.csv> --out <CARPETA_RUTAS>                    # opcional, antes de convertir
python tools/frame_to_route.py <frame.csv> <NOMBRE> --profile <CARPETA_RUTAS> --cinta
```

- `--profile` es la carpeta de salida y es **obligatorio**: sin él el conversor termina con un error (el wizard lo pasa solo). Ahí también se busca `envelope_<Clase>.csv`.
- `--cinta` emite el `_cinta.csv`. **Sin él no hay reproducción fiel.**
- `--keep-inputs` escribe gas y freno en el `_wp.csv`. No hace falta para la cinta.
- `--sin-marca N …` borra la marca N-ésima (1..N, en orden). `--marca-wp W …` agrega un evento en el waypoint W de salida. Así se corrigen las marcas **sin editar la toma**.
- La docstring menciona `--min-move`, pero esa opción no existe: pasarla da error.
- **`frame_merge.py`** ordena las partes **por empalme de posición**, nunca por nombre ni por hora: el fin de una parte tiene que estar a menos de 8 m del inicio de la siguiente, con exactamente un candidato. Exige el mismo encabezado y la misma clase en todas las partes, y re-acumula `t_cum`.

#### E.5.5 Qué hacer con lo que es de DayZ

| Campo de DayZ | Qué hacer en 7DTD |
|---|---|
| `gear` 0/1/2+ | sin caja: 0 = reversa, 2 = adelante, 1 = quieto. `MaxGear` queda en 2. El framework sólo necesita saber **el sentido** (`gear == 0` ⇒ tramo en reversa) |
| `rpm`, `engineRPM*` | 0. Las compuertas que usan el ralentí se apagan solas; `GearStrategy` no se emite |
| `speed_kmh` vs `chassis_kmh` | si sólo tenés una, escribí la misma en las dos |
| `surface` | nombre del bloque o material bajo las ruedas, o `none` |
| `DriverClass` del hdr (`eAI_SurvivorM_Boris`) | lo fija el conversor: tu runner lo ignora y sienta su propio NPC (`npcTraderJoel` en la prueba de concepto) |
| `Attachments`, `QuestTravelID`, `QuestEscortID`, `RespawnDelay`, `AverageSpeedMS`, `FollowPath` y las ~27 claves del `driving_config_template.json` | ignorar (son de la v1.1 o de Expansion) |
| **40 Hz** | el Python cuenta ventanas **en filas** pensando en 40 Hz (ej. 8 filas = 0,2 s, 80 filas = 2 s, `dt` por defecto 0,025). **[port]** Lo más simple: poner `Time.fixedDeltaTime = 0.025` mientras se graba, o grabar cada paso y aceptar ventanas un 20 % más cortas. **No** remuestrees la toma: la cinta se indexa por distancia, así que la densidad sólo tiene que ser pareja |

#### E.5.6 Lo que sale (en `--profile`, con prefijo `BZBusRoute_<NOMBRE>`)

**`_hdr.json`**, el encabezado. Es un JSON chico, sin waypoints.

| clave | qué es |
|---|---|
| `VehicleClass`, `Wheelbase` | del header |
| `Fingerprint` | `{VehicleClass, MaxSteeringAngle, Wheelbase, RminM = wb/tan(maxSteer), Mass, GearsCount}` |
| `Events` | `[{"wp": i, "trigger": {"type": "wp_reached", "wp": i}, "actions": []}, …]`, uno por marca. **El tipo tiene que ser `wp_reached`** |
| `MaxGear` | marcha más alta usada (≥ 2) |
| `EndpointBrakeDecel` | m/s², la frenada del humano al final (si se pudo medir), acotada a [0,5; 8] |
| `CreepKmh`, `CreepSource`, `CreepSamples`, `CoastAtRest` | derivados del envelope: velocidad de arrastre sin gas. **[port]** casi seguro 0 sin embrague |
| `EnvelopeClass`, `Envelope` | copia del envelope de la clase (respaldo si la ruta se lleva a otro server) |
| `WpRows`, `CintaRows` | filas de datos de `_wp.csv` y `_cinta.csv` (para el % de carga) |

**`_wp.csv`**, la polilínea de waypoints: uno cada 0,5-2 m, más denso cuanto más lento. **Sin encabezado**, **21 columnas**, fin de línea LF. La columna 14 va **vacía** (así que el lector tiene que conservar los campos vacíos; un `Split` que los descarta corre todos los índices).

| col | campo | lo que escribe | lo lee el runner |
|---|---|---|---|
| 0-2 | x, y, z | posición del frame (3 decimales) | **sí** (se descarta si no es finita) |
| 3 | isStop | 1 sólo en el punto final, si terminó parado | **sí** |
| 4 | stopDuration | 6 en el final, si no 0 | no |
| 5 | stopRadius | 0 | no |
| 6 | targetSpeed | km/h grabada (ya con paradas y rampa de llegada) | **sí** (`recKmh`) |
| 7 | targetGear | marcha grabada | no |
| 8 / 9 | throttle / brake | 0, o lo grabado con `--keep-inputs` | sólo si col 12 = 1 |
| 10 | handbrake | 0 | sí |
| 11 | steering | 0 | no |
| 12 | hasInputData | 0, o 1 con `--keep-inputs` | compuerta de 8 y 18 |
| 13 | mode | `normal` o `reverse` (también existe `parking`, que el conversor no escribe) | **sí** |
| 14 | name | **vacío** | no |
| 15 | heading | grados; **0,000 exacto = "sin dato"** | **sí** |
| 16 / 17 | lights / horn | 0 | no |
| 18 | frontWheel | `front_wheel_deg` | sólo si col 12 = 1 |
| 19 | corridorHalfWidth | 0 | no |
| 20 | legBreak | 1 = último wp de un tramo (cambio de sentido o corte manual) | **sí** |

**`_cinta.csv`**, la cinta: **una fila por frame** de la toma, con fila de encabezado. **Es lo que se reproduce.**

```
s,x,y,z,heading,chassis_kmh,front_wheel_deg,throttle,brake,gear,handbrake,dt
```

- `s` es la distancia recorrida, en m y con 3 decimales. **Nunca decrece.**
- `handbrake` y `dt` no los lee el runner.

**`BZBusRoute_<NOMBRE>.json`**, el hdr más los waypoints en JSON. En DayZ ya no se usa para correr, porque el lector JSON tardaba ~180 s con 5.522 waypoints. **[port]** Ignoralo y leé `_hdr.json` + `_wp.csv` + `_cinta.csv`.

#### E.5.7 Qué hace el conversor por dentro (para entender lo que sale, no para reescribirlo)

1. **Rebobinados.** Si la toma retrocede más de 2 m sobre su propia línea (a más de 15 km/h, con menos de 1 m de lateral) y vuelve a pasar el punto en menos de 3 s, eso es el server corrigiendo al cliente. Las filas repetidas se descartan, sin dejar hueco.
2. **Resample de la polilínea.** Se emite un wp cada `clamp(v·0,15 s, 0,5 m, 2 m)`. Sólo se acumula distancia a ≥ 0,6 km/h. Las filas marcadas, la primera y la última se emiten siempre.
3. **Paradas.** Una detención (< 0,6 km/h durante ≥ 0,4 s) **con marca** deja un wp con `targetSpeed = 0`. Sin marca es una pausa: se cruza sin parar.
4. **Sentido.** Un wp es reversa si `gear == 0`, o si el movimiento va contra el rumbo (`dot < −0,30` a ≤ 15 km/h). Donde cambia el sentido, el último wp del tramo que sale lleva `legBreak = 1`.
5. **Final.** Si la toma termina a ≤ 1 km/h, el último wp es `isStop = 1`. Antes de cada parada o cambio de sentido hay una **rampa de llegada**: `v ≤ √(2·decel·d)` con `decel = max(0,35; 1,40 + 9,8·pendiente)`, en los últimos 25 m.
6. **Saneo de retrocesos.** Se descarta un wp que retrocede ≥ 2 m contra el rumbo grabado del anterior.
7. **Cinta.** Se aplican dos filtros:
   - **glitch:** un salto mayor que `max(2 m, v·12·dt)` que vuelve en menos de 3 s se descarta;
   - **rebobinados:** los mismos del paso 1.

   Después, `s` = distancia entre "anclas" (filas separadas más de 6 cm), repartida dentro de cada tramo según `v·dt`. La posición grabada viene cuantizada: se queda quieta varios frames y después salta.
8. **Avisos, sin corregir nada.** Trompos (más de 90° en 2 s, rodando) y detenciones sin marca. **La toma no se edita**: se regraba.

#### E.5.8 El envelope (opcional): lo que la toma midió del vehículo

`envelope_emit.py` escribe `envelope_<Clase>.csv`. Tiene una línea por medición: `scalars`, `stx2` (comando → ángulo), `shist`, `chist2`/`nocreep` (arrastre), `lhist` (arranque), `ahist` (aceleración a fondo), `coast2` (desaceleración sin pedales), `k` (subviraje por banda de velocidad) y `medido`.

- Los bins que la toma no tocó quedan **vacíos a propósito**: es el mapa sincero de lo que se midió.
- Una toma nueva del mismo vehículo **suma** bins si la huella (masa, marchas, ángulo máximo) no cambió. Si cambió, sale con código 3 y no toca nada.
- **[port]** En 7DTD casi todo esto **se lee** del `Vehicle` (E.1). Empezá sin envelope y usá las lecturas directas. El envelope vale después, para medir lo que el juego no declara: frenada real, desaceleración sin pedales y subviraje.

#### E.5.9 El lado del runner: cómo se carga

- **`_hdr.json`:** con el lector JSON.
- **`_wp.csv`:** **por líneas**, partiendo cada una con un split que **conserva los vacíos**. Menos de 14 campos = línea descartada.
  - `pos` = columnas 0-2.
  - `isStop` = (col 3 == 1).
  - `recKmh` = col 6.
  - `isRev` = (col 13 == `reverse`).
  - `headRad` = col 15 en radianes (0 exacto = sin dato).
  - `legBreak` = (col 20 == 1).
- **`_cinta.csv`:** por líneas. La primera (encabezado) se saltea y se exigen ≥ 12 campos. Se leen las columnas 0-9. La cinta es válida con más de 10 filas.
- **Sin `_cinta.csv`, la ruta corre igual**, en modo reactivo (pure pursuit sobre la polilínea): es mucho menos fiel.
- **Cargar en muchos cuadros.** Una cinta de 17.000 filas procesada en un solo cuadro congeló el server de DayZ 1,84 s. Se usa una cola global FIFO con un presupuesto de 5 ms por cuadro, y cada paso corta a ~1 ms.
  - **[port]** Una corrutina por `Update` con un `Stopwatch`.
  - Medí el **peor** cuadro, no el promedio.

### E.6 — El cerebro, pieza por pieza (especificación portable)

Esto es lo que hace el cerebro 1.2 de DayZ, escrito para reimplementarlo en C#. Sale de leer el código fuente, que está en el repositorio en `scripts/4_World/bz12drive/`. **Si algo de acá no te cierra, el código es la verdad**: cada apartado nombra sus archivos.

Cada regla trae su motivo ("cicatriz"): el caso medido que la hizo necesaria. **No saques una regla porque parece de más** sin antes entender su cicatriz. Tampoco la copies a ciegas: muchas cicatrices son de DayZ (embrague, caja, velocímetro que miente al arrancar) y en 7DTD pueden no aparecer. Vale la regla de E.0: si una regla no aplica, se demuestra con una medición.

#### E.6.0 El mapa: dos relojes, un orden

- **`Plan()` a 2 Hz** (dt = 0,5 s). Cursor de la ruta, modo, velocidad objetivo, módulos de manejo, detectores lentos y escalera de rescate.
- **`Sample()` a 40 Hz** (dt ≈ 0,025 s). La cinta, el refresco del volante, el freno final por cuadro, la entrada al endpoint y los detectores rápidos (choque, retroceso, reposo).
- Los dos escriben **el mismo** objeto de salida (`throttle 0..1`, `brake 0..1`, `steering −1..1`, `handbrake`, `gear`), y el runner lo aplica después de cada llamada. Cuando la cinta "posee", Sample pisa a 40 Hz lo que decidió Plan.
- **[port]** Sample en un `Postfix` o `Prefix` de `PhysicsFixedUpdate` del vehículo; Plan cada 20 llamadas de Sample (o con un acumulador de tiempo). Todo umbral en "ticks" es de Plan: 20 ticks = 10 s.

**Orden en que conviene portarlo** (coincide con los hitos de E.4):
1. **Cinta + cursor** (E.6.1). Con el volante de la cinta y los pedales de la cinta ya maneja: es el H4.
2. **Legs y árbitro** (E.6.3) cuando la toma tenga paradas y cambios de sentido: H5.
3. **Control reactivo** (E.6.2): para cuando la cinta no posee (arranque, se perdió, llegada final).
4. **AutoRecovery** (E.6.4): H6.

#### E.6.1 La cinta: cursor por distancia, posesión y paradas grabadas

Archivos: `BZCintaRunner.c` y la parte "cinta" de `BZDriveController.c`.

**Qué es.** El `_cinta.csv` en memoria (arrays paralelos `S, X, Y, Z, H, Kmh, Fw, Thr, Brk, Gear`) y un **cursor** que dice "en qué punto de la toma está el vehículo". En cada Sample se lee lo que el humano tenía **en ese punto del camino**: rueda lograda, gas, freno y marcha. **Interpolación lineal por arco, cero suavizado: la textura del volante es señal.**

**Precálculo al cargar:**
- **Tramos de fase.** Son bloques contiguos con `gear == 0` (reversa) o `gear != 0` (adelante).
  - Un bloque es tramo si mide **≥ 3 m** de `s` **y** su fase difiere de la del tramo anterior.
  - Los micro-bloques (un frame en R al spawnear, un cambio de un frame) se absorben. Sembrar contra ellos mandó el cursor a 130 m.
  - Se guarda, por tramo, `TramoS[k]`, `TramoIdx[k]` y `TramoRev[k]`, y por frame `FrameTramo[i]`.
- **[port]** Cargar repartido en cuadros (E.5.9). Un contador de filas que viva en la tajada, y no en el objeto, pierde una fila por tajada.

**El cursor (`Muestra(pos, out res, esperaRev)`)**, en cada Sample en que la cinta posee. `esperaRev` es **el sentido del tramo de la ruta** (leg), no la marcha grabada, que se atrasa un frame en cada cambio de sentido.

```
// 1. ODOMETRÍA (sin signo: retroceder también suma)
av = |pos.xz - prevQ.xz|;  si av < 8 m: sEst += av  si no: prevQOk = false (teleport ⇒ rescate)
// 2. CLAMP DE FASE: el cursor no pasa el fin del tramo de SU fase; sólo el cambio de leg cruza la frontera
tr = tramo de sEst retrocedido hasta uno con TramoRev == esperaRev
si hay tramo siguiente: finFase = TramoS[tr+1] - 0,05; si sEst > finFase: sEst = finFase; clampeoBorde = true
// 3. el cursor camina el arco (monótono): while S[cursor+1] <= sEst: cursor++
// 4. MESETA (ver abajo)
// 5. VENTANA DE ANCLAS: normal [cursor-3, cursor+6], paso mínimo 0,25 m
//    rescate (tras teleport o 12 ticks lejos): ±(12 m × (1 + fallos)), tope 60 m, paso mínimo 0,10 m
// 6. TORNEO: para cada segmento j de la ventana
//      saltar si FrameTramo[j] < ultTramoOk (tramo ya cerrado: PROHIBIDO, no castigado)
//      saltar si FrameTramo[j] > tr (el futuro lo cruza el leg)
//      saltar si (Gear[j]==0) != esperaRev
//      saltar si |seg| < pasoMínimo (nudo denso: posición cuantizada)
//      proyectar: t ∈ [0,1], d² = distancia²;  saltar si t en el borde (≤0,02 o ≥0,98) y d² ≥ 0,09
//      costo = d² + ((S[j] - sEst)/5)²       // λ = 5 m: la posición manda, pero una pasada ajena a 60 m no gana
// 7. ACEPTAR el mejor si es interior (0,02<t<0,98) o está a < 0,3 m,
//    Y si no está más lejos que el punto del cursor + 0,5 m ("el ancla sólo acerca")
//    ⇒ cursor = j; sEst = S[j] + t·(S[j+1]-S[j]);  si no: odometría pura
// 8. ticksLejos (distM > 1,5 m); consumo del tramo: si distM ≤ 1,5: ultTramoOk = max(ultTramoOk, tramo(sEst))
// 9. INTERPOLAR fila cursor..cursor+1 con t = (sEst - S[c])/(S[c+1]-S[c]): fwDeg, thr, brk, kmhRec, heading (por el arco corto)
//    latM = (pos.x-q.x)·cos h - (pos.z-q.z)·sin h      // + = vehículo a la DERECHA de la toma
// 10. PERDIDA: lon = √(distM² - latM²) > 6 m  ó  |latM| > 12 m  ⇒ return false (ese tick maneja el reactivo)
// 11. BORDE AGOTADO: clampeado 20 ticks y distM > max(1 m, distM_al_clampear + 0,3) ⇒ false
// 12. gear = el del frame más cercano; si la fase leída no es la esperada: ticksLejos++
```

Por qué es híbrido (odometría + anclas):
- La posición grabada está cuantizada a ~10-16 Hz: 8 frames en el mismo punto con la rueda barriendo 2°/frame. Indexar sólo por posición daba 14° de error de rueda.
- Una proyección cruda en zonas densas duplicaba las reversiones de volante.
- Un ancla en el extremo de un segmento (t = 1) tiraba `sEst` para atrás en cada consulta y congelaba el cursor.

**Re-anclajes externos** (los tres borran la meseta en curso):
- **`SembrarFase(esperaRev, pos)`**: la dispara el controlador cuando cambia `leg.isReverse`, en todos los ticks aunque la cinta no posea.
  - Salta al primer tramo `kt > ultTramoOk` de esa fase: `sEst = TramoS[kt]`, `cursor = TramoIdx[kt]`, `ultTramoOk = kt`.
  - Pone `prevQOk = false`, así la primera consulta no suma odometría vieja.
  - Si no hay tramo siguiente de esa fase, busca el frame de esa fase más cercano a `pos`.
- **`AnclarEnS(s)`**: para la re-ubicación del rescate (E.6.4).
- Una cinta sin tramos de esa fase sólo se loguea.

**Posesión: el latch por flanco y por leg.** La cinta **manda** en un Sample si se cumplen todas estas condiciones:
- la cinta es válida;
- el leg es válido;
- `rodo == true`;
- el modo es CRUISE, APPROACH o MANEUVER, o se da `cintaVolanteSolo`;
- `Muestra` dio true.

`cintaVolanteSolo` = ENDPOINT en un leg de reversa con v ≥ 2 km/h: la cinta pone sólo el volante y el cierre es del endpoint. En el ENDPOINT de avance la cinta no posee.

```
al cambiar de leg:           rodo = false; quieto = false; rearmaDist = -1
si v < 2 km/h:               quieto = true
ARMAR si !rodo && quieto && v ≥ 2 km/h && sin recuperación en curso
      && (rearmaDist < 0 || distancia recta desde donde se soltó ≥ rearmaDist)
```

- **Por flanco:** el vehículo tiene que haberse visto quieto en ESTE leg y después rodar. El cambio de leg llega a 2 Hz con el vehículo todavía rodando lo que le quedaba del leg anterior (1,1-1,5 km/h). Un arme por nivel se re-armaba en el acto, sembraba mal y el cursor quedaba afuera para siempre.
- **El arranque desde parado lo hace el LAUNCH** (la rampa de gas de E.6.2), nunca la cinta. La toma empieza con ~70 frames del humano sentado con gas 0: la cinta sola no arrancaría nunca.
- **Sin cinta no hay posesión.** Toda referencia a la cinta necesita guarda. En DayZ, sin esa guarda, el enganche de meseta leía una cinta nula 40 veces por segundo.

**Mesetas: las paradas grabadas.** Una meseta es una racha de filas con `Kmh < 1 km/h` dentro del mismo tramo de fase. Es un evento **temporal con salida**: "acá me detuve tanto tiempo" se reproduce tal cual y después se sigue.
- **Entrar** (cuando el cursor cae en una fila lenta que no fue cumplida): expandir la racha hacia atrás (sin pasar `ultFin`) y hacia adelante, fijar `cursor = ini` y `llamadas = 0`.
- **Cumplir:** cada `Muestra` suma 1 y re-fija el cursor en la entrada. Sale a las `fin − ini + 1` llamadas, que es la duración grabada a 40 Hz. Al salir, `ultFin = fin` y el cursor pasa a `fin + 1` si es del mismo tramo.
- **Una meseta cumplida no se re-entra** (`k > ultFin`). Si no, el rescate por proyección caería en ella, porque todas sus filas están en el mismo punto, y el vehículo quedaría clavado.
- **Por qué 1 km/h y no 0,3:** el criterio es "más lento de lo que el vehículo puede arrancar". Un bus quedó clavado sobre una fila grabada a 0,02 km/h.
- **Sostener la parada:** durante la meseta, gas 0 y freno de servicio 1,0, nunca el de mano. El humano casi no frenó al detenerse, y en una cuesta de +2,3° la fila traía gas 0 y freno 0: el vehículo rodaba hacia atrás.
  - ⚠ **[port]** Aplicá el sostén **en el lazo rápido, después de escribir los pedales de la cinta**. En DayZ el sostén está en Plan (2 Hz) y el bloque de pedales de la cinta en Sample (40 Hz) lo re-escribe con el freno grabado en el cuadro siguiente. Es un defecto conocido, no lo copies.
- **Mientras hay meseta** se apagan los detectores de traba (CLAVADO, traba por progreso, el rescate lógico). **Una parada grabada no es una traba**: las 22 escaleras abiertas por CLAVADO en cuatro días estaban todas en paradas grabadas.

**Enganchar la meseta donde el vehículo se detuvo.** El cursor es espacial y el vehículo no frena en la fila exacta. Si quedó 2,5 cm corto, la fila que le toca es la última de la frenada (freno 1) y la meseta nunca se alcanza: el cursor sólo avanza si el vehículo avanza. En DayZ, dos buses quedaron 22 s clavados así.

Se engancha la primera meseta no cumplida dentro de 8 m adelante si pasa **cualquiera** de estas tres cosas, en modo CRUISE y sin recuperación ni meseta en curso:
- **quieto:** `|v| < 0,3 km/h` durante ≥ `WaitS` (0,5 s);
- **retroceso:** `fwdKmh < −2` durante ≥ `WaitS`, fuera de un leg de reversa o de una maniobra;
- **clavado:** el arco de la cinta no avanza 1 cm durante ≥ `WaitS`, con `|v| < 2 km/h`, y la toma no pide gas antes del reposo.

El disparo **clavado** existe porque en DayZ el velocímetro lee 0,06-0,5 km/h con el vehículo quieto, así que "quieto" nunca juntaba 0,5 s. Con la cinta suelta se exige además que la toma no pida gas antes de la meseta.

**Soltar la cinta hasta el fin de la meseta** (al salir de un hold del consumidor, o como rescate lógico, E.6.4):

```
rodo = false; quieto = true; sueltaPos = pos
rearmaDist = (hay meseta adelante) ? FinMesetaAdelante() - sEst : -1   // medir ANTES de mover el cursor
si no hay meseta en curso: ConsumirMesetasServidas(pos)                 // las detrás del vehículo y la primera adelante
                                                                        // cursor ⇒ la fila del vehículo; prevQ = pos
cerrar la meseta en curso
```

- Después, el LAUNCH relanza y la cinta se re-arma cuando el vehículo recorrió `rearmaDist`.
- **Por qué consumir:** durante un hold el cursor queda **congelado**. Al re-armar, la odometría lo dejaba en una fila de reposo con el vehículo ya en marcha, y la curvatura grabada llegaba ~1 m tarde. En DayZ el bus abría 2,9 m hacia afuera y se ahogaba (5 trabas en 12 contra 2 en 65).
- **Engancha por cercanía, se suelta por lejanía:** con una meseta en curso y el vehículo a más de 8 m de ella, se suelta. Un coche quedó 15,8 m detrás de su meseta durante 90 s.

**Hold del consumidor (API).** El mod que usa el framework (un servicio de buses, por ejemplo) puede pedir "quedate acá". El runner lo engancha cuando se cumple cualquiera de estas condiciones:
- `sEst ≥ entrada de la meseta` (por arco: a 3 km/h una meseta de 2,8 cm se salta en un paso);
- `|v| < 0,3 km/h`;
- no hay cinta;
- el vehículo se alejó más de 8 m del pedido.

Con el hold activo se aplica gas 0, freno 1 y freno de mano 1, y **no se llama a Sample**. `Resume()` pasa el árbitro a IDLE y suelta la cinta hasta el fin de la meseta.

**Volante y pedales de la cinta:** ver E.6.2 §1.6 y §2.5.

**Cicatrices de la cinta (una línea cada una):**
- Espacial, no temporal.
- `front_wheel_deg` y no la tecla.
- Anclas sólo en segmentos ≥ 0,25 m.
- Filtrar los candidatos inaceptables **antes** del torneo: si no, gana uno inválido y el ancla queda nula aunque hubiera uno válido.
- Costo posición + arco: dos pasadas de reversa sobre el mismo suelo, a 7 cm, hacían saltar el cursor 60 m.
- Monotonía de tramos como **prohibición**, no como castigo.
- El tramo se consume por seguimiento sano, no por ancla: en las reversas lentas nunca hay ancla.
- La siembra la dispara el leg, no un detector interno: el detector sólo veía 2 de 6 cambios de sentido.
- Pérdida por ejes: con un lateral de 12 m la cinta todavía sirve. Soltarla a 6 m totales dejó al reactivo manejando a 186 km/h con 5 m de error.
- El rescate del cursor es escalonado y por arco, no por frames (±60 frames son ±1,8 m en una zona lenta).
- La odometría no tiene signo: el retroceso necesita un detector propio.

#### E.6.2 — Control lateral y longitudinal (el volante y los pedales)

##### 0. Marco común

**Relojes.** Hay dos:
- `Plan()` a 2 Hz (dt 0,5 s). Hace el cursor del leg, el modo, la velocidad objetivo, la marcha, los pedales y el objetivo de volante.
- `Sample()` a 40 Hz (dt 0,025 s). Hace el volante re-apuntado, el slew, el freno de endpoint por cuadro, la entrada al endpoint y la cinta.

Los contadores "ticks" de maniobra, endpoint, rampas y árbitro son ticks de Plan.

**Convenciones** (verificadas en el código):
- Rumbo = `atan2(x, z)` y crece en sentido horario visto desde arriba.
- Curvatura + = giro a la DERECHA. Volante normalizado + = DERECHA, en [-1, 1].
- `crossTrackM` + = vehículo a la IZQUIERDA de la línea. En la cinta, `latM` + = vehículo a la DERECHA (el signo es el opuesto).
- En reversa se invierte el signo del volante: `s = -s`.
- Unity usa X derecha, Z adelante y rumbo horario desde +Z, así que parece la misma convención. **Verificalo con una medición antes de confiar.**

**Estado del vehículo:**
- `chassisKmh` = |velocidad| del chasis, sin signo.
- `fwdKmh` = componente hacia adelante, con signo.
- `kmh` = velocímetro de rueda. Sólo se usa para `ku(v)` y `ApproachZone`.
- `courseRad` = rumbo de la velocidad si v > 7 km/h. Si no, igual al yaw.
- `pitch` > 0 = trompa arriba.

**Vista del leg.** Un leg es un tramo entre cortes: un cusp o una parada.
- `remainM` = arco que falta hasta el fin del leg.
- `alongM` = arco recorrido desde el inicio del leg.
- `signedToEndM` = proyección recta sobre la tangente de la pose final. + = falta llegar, − = ya se pasó.
- `fwdM` = arco visible hacia adelante en la ventana. La ventana cubre 25 m atrás y 80 m adelante.
- Flags: `endIsStop`, `endIsCusp`, `isReverse`, `nearWp`.

**Tolerancias:**
- `LLEGADA_M = 0,10 m`: la barra estricta de pose, para maniobras y reversa.
- `ACEPTA_ENDPOINT_M = 0,25 m`: la aceptación del endpoint de frente. Existe porque el freno por cuadro estaciona unos 0,10 m antes del punto.
- `ACEPTA_ENDPOINT_RED_M = 1,0 m` (1004): red de seguridad. Un slam enganchado que se rinde entre 0,25 y 1,0 m **no** es rendición: `ROUTE_COMPLETE` (ciclo normal, `RunnerRouteCompleted` true) con `[C12] LLEGADA fuera de la tolerancia estricta: <d> m`. `ROUTE_INCOMPLETE` queda para > 1 m, maniobra rendida o leg abandonado. Después del FIN nada mueve el índice (`UpdateLegBounds` sólo antes del FIN).
- Armado del slam (1004, `BZModeArbiter.LlegoLaHoraDelSlamCon`): la zona fija (la batalla) **o** `remM <= v²/(2·MaxBrakeDecel(surf))·(1 + MARGEN_FRENO)`, con `MARGEN_FRENO = 0,25`. En perfil no cambia nada (mismo cuadro). ⚠ `EndpointBrakeDecel` del hdr es un **promedio** de la fase final, no una capacidad: no sirve para armar. ⚠ Si el `MaxBrakeDecel` de un vehículo está sobreestimado, la cláusula por física no adelanta el armado (visto con Hatchback_02_BanditKitty: armó a 2,56 m, llegó a 0,27 m, completa por la red).

**Orden por tick de Plan:**
1. Actualizar leg.
2. Árbitro.
3. Módulo según el modo:
   - MANIOBRA → MC.
   - ENDPOINT → Slam.
   - CRUISE/APPROACH → PurePursuit + Derivation.
4. `AcotarVolante`: cap de agarre y contravolante.
5. Clamp de autoridad de volante.
6. Pedal grabado.
7. Tope de tracción.
8. NoSoltarHaciaElBog.
9. RampaDeArranque.
10. Detectores (sólo informan) y recuperaciones.
11. NoPlanearHastaElBog.
12. Handbrake = el grabado del wp. Nadie más lo escribe.
13. `targetSteer = out.steering`.

**Orden por tick de Sample:**
1. `remAhora`.
2. PromoverEndpoint.
3. Freno de endpoint por cuadro.
4. Ventana de cambio de marcha.
5. Deriva.
6. RefrescarVolante.
7. SteerSlew.
8. Cinta: si posee, pisa volante, gas, freno y marcha.

**El actuador** escribe throttle, steering, brake y handbrake, y cambia de marcha si difiere. No hace nada más.

**Defaults en producción:**
- La cinta (replay espacial) está ACTIVA.
- La ley de volante N2 FF-primario está ACTIVA.
- Cuando la cinta no posee, manda la ley reactiva de §1.3 y §1.4.

##### 1. Control lateral

###### 1.1 De curvatura a volante (modelo bicicleta inverso con subviraje medido)

```
MaxCurvature(v)      = tan(steerMax) * max(ku(v),0.05) / wb
CurvaturaUtilMax(v)  = MaxCurvature(v) * (1 - 0.05)      // MARGEN_AUTORIDAD
SteerCommandSplit(kFF,kCorr,v): delta = atan(wb*(kFF/ku + kCorr/ku)); cmd = clamp(delta/steerMax,±1)
SteerCommandForCurvature(k,v):  delta = atan(wb*k/ku); cmd = CommandForAngle(delta)   // módulos POSE
```

- `ku(v)` es el factor de subviraje medido en bins de 5 km/h, con 20 bins de 0 a 100 km/h. El prior es 1,0 (neutro). No se usa el 0,90 de v1.1, que el envelope midió en ~0,50.
- `SteerCommandSplit` divide los dos términos por `ku`. Así que el split sólo sirve para congelar `kCorr` (ver §1.5). Usa la relación lineal `/steerMax`, no la transferencia medida.
- Motivo del margen de autoridad 0,05: mientras el volante está contra el tope, el lazo está abierto. Reservar un 5 % deja al lazo algo con qué modular. Se midió que el error pico sigue a la racha más larga de saturación.

**`CommandForAngle(δ)`, con la transferencia medida "stx2":**
- 10 baldes por |cmd|: `balde = floor(|cmd|·10)`.
- Cada balde promedia el ángulo de rueda ejecutado en régimen estacionario. Se necesitan 20 muestras por balde.
- Sólo se usa si es sana:
  - monótona, con tolerancia de 0,004 rad;
  - baldes con dato contiguos desde el 0;
  - al menos 4 baldes.
- Se invierte interpolando entre centros de balde `(b+0,5)/10`. Si se pide más ángulo del que se midió, devuelve 1.
- Si no es sana: `δ/steerMax`.
- Motivo: una tabla ruidosa invertida pidió la mitad de comando y el lateral pasó de 0,49 a 2,21 m.
- **En 7DTD esto es más simple:** el motor hace `wheelDir = moveStrafe · SteerAngleMax` (con `lastInputController = true`), así que la transferencia es lineal y conocida.

**Replay de la rueda grabada** (`CommandForAngleReplay`):
- Si la curva medida es sana, la usa.
- Si no, usa la "planta": `cmd = deg / 85,1` (una constante medida en DayZ).
- Motivo: en DayZ el input 1,0 NO equivale a `steerMax`; con `/steerMax` (≈30°), la rueda ejecutaba unas 2,8 veces lo grabado.
- La ley cerrada sigue usando `/steerMax`, porque es una perilla calibrada del lazo.
- **En 7DTD:** `cmd = deg / SteerAngleMax` (la planta ES `SteerAngleMax`). Verificalo midiendo el ángulo real de rueda contra el comando.

###### 1.2 Techo de curvatura comandable (`KMaxCmd`, se calcula en cada Plan)

```
KMaxCmd = MaxCurvature(v)*0.95
if v>1 m/s y aDir medido>0.1:
    aDir = ALatDirEn(velocidadGrabadaEnLaMira)   // p95 de a_lat de la toma en bandas de 20 km/h; nunca menor al escalar
    aDir = max(aDir, 5.0)                        // ALAT_DIR_PISO: agarre físico mínimo para recuperar
    KMaxCmd = min(KMaxCmd, aDir / v²)
```

**Cómo se mide `a_lat` de la toma:**
- Por waypoint: `v²·|k|`, sólo con v ≥ 5 m/s.
- `k` es la curvatura con diferencia centrada ±1,5 m sobre rumbo suavizado, con clamp ±1.
- `ALatToma` = p90, descartando valores ≥ 40.
- `ALatDir` = p95, descartando valores ≥ 100.
- Bandas de 20 km/h: 13 bandas, de 0 a 260 km/h.

**Reglas:**
- Lo medido sólo puede APRETAR el techo. Sin medición no se inventa nada.
- Motivo: comandar más curvatura de la que dan las gomas hace deslizar, no doblar. Hubo casos de 5 a 11 veces el agarre.

###### 1.3 Pure pursuit (camino de reserva sin N2, y siempre en reversa)

**Horizonte Ld** (se calcula en Plan con `chassisKmh`):

```
ld       = v*0.6                                   // PREVIEW_S (era 1.2 cuando la mira iba a 2 Hz)
floor    = max(1.5*wb  (3.0 m si no hay wb),  v*0.30)  // LD_WB_MULT=1.5 · PREVIEW_MIN_S=0.30
kPeor    = max_j |WrapPi(h(d0+base)-h(d0))|/base, d0 = 0, base, 2base… ≤ ld, máx 16 pasos, base = VentanaFF
techo    = (kPeor<0.0005) ? ld : sqrt(8*0.5/kPeor)  // corte eps≈Ld²k/8, CORTE_MAX_M=0.5
if v ≥ 60 km/h: pisoEstab = (kPeor<0.0005)? ld : min(ld, sqrt(8*1.65/kPeor)); floor = max(floor,pisoEstab)
ld = min(ld, techo); ld = max(ld, floor); if fwdM>0.05: ld = min(ld, fwdM)
```

- `VentanaFF` = min(wb, 3,0 m), o 3,0 si no hay wb.
- Constantes de corte: `CORTE_ALTA_M = 1,65` y `CORTE_ALTA_KMH = 60`.
- Primero el techo y después el piso. Si fuera al revés, el techo podría dejar Ld debajo del piso.
- `kPeor` es la PEOR curvatura de la ventana, no el promedio.
- Motivo del piso por tiempo, con el modelo linealizado:
  - `s² + (2v/Ld)s + 2v²/Ld² = 0` da ζ = 0,707 siempre.
  - Lo que desestabiliza es el retardo: `margen = 65° − deg(v√2/Ld·T)`.
  - 0,30 s deja margen proporcional a la velocidad.
- 1,5 wb es el punto medio medido entre el piso de 2,0 wb, que cruzaba de carril, y 1,2 wb, que cazaba.

**Punto de mira y ley:**

```
aim  = SampleAt(ld)            // sample PELADO, sin sesgo
pref = pos;  if reversa: pref = pos - wb*(sin yaw, cos yaw)     // eje que lidera
refYaw = yaw (+π en reversa)
dist = |aim - pref|; if dist<0.10 → k=0
alpha = WrapPi(atan2(aim-pref) - refYaw)
den = max(ld, dist)            // en Plan
k = clamp(2*sin(alpha)/den + ffDeficit, ±KMaxCmd)
```

**Refresco de la mira a 40 Hz** (`RefrescarMiraA40`):
- `avance = |pos − posDelPlan|`.
- Si 0,05 < avance ≤ 40 m y `avance + ld ≤ fwdM`, entonces `aim = SampleAt(avance + ld)`.
- La curvatura a 40 Hz usa `2·sin(α)/dist + ffDeficit`, con el mismo clamp. Si el refresco no aplicó, sigue la mira del Plan.
- Ld no se recalcula a 40 Hz.

**Sonda FF (déficit), sólo sin N2:**
- `ffDeficit = kLocal − kArco`.
  - `kArco = Δrumbo[0, ld] / ld`.
  - `kLocal = Δrumbo[0, VentanaFF] / VentanaFF`.
- Se pone en 0 si pasa cualquiera de estas cosas:
  - el cursor no se movió desde el Plan anterior (`nearWp` igual);
  - las dos |k| son menores que 0,006 (recta; es el piso de ruido angular);
  - `endIsStop` y `remainM < 4·wb`, porque manda el endpoint.
- Se suma sólo el déficit, no `kLocal` entero. En arco de radio constante vale exactamente 0.

⚠ **Diferencia con el pure pursuit de la prueba de concepto de 7DTD (E.3):** aquel acorta Ld en curva y corre la mira hacia afuera (compensación de sagita). En DayZ **las dos cosas se retiraron por regresiones medidas** (ver el punto 6 de este apartado, "Cicatrices": acortar Ld en curva sube la ganancia y diverge; correr la mira le roba α al lazo). Midió bien en un círculo de radio constante, pero para una toma real portá la versión de arriba.

###### 1.4 N2 "FF-primario" (activo por defecto, sólo de frente)

Reemplaza a la persecución. La corre Plan con `avance = 0` y cada Sample con el avance fresco. Si no es válida, cae a pure pursuit.

Importante: el feed-forward es la **curvatura grabada de la trayectoria**, calculada con el rumbo grabado. No es el ángulo de rueda grabado; ese es de la cinta (§1.6).

```
V   = VentanaFF (min(wb,3.0))
# referencia de posición/rumbo: tangente de POSICIONES a ±1.5 m (FFP_TAN_M)
p1 = SampleAt(max(0,a-1.5)).pos ; p2 = SampleAt(min(fwdM,a+1.5)).pos ; (a2-a1 ≥ 0.5)
hseg = atan2(p2-p1);  u = (sin hseg, cos hseg)
dr = wb/2                                      // EJE TRASERO: vehículo y referencia
rv = pos - dr*(sin yaw, cos yaw);  rr = S(a).pos - dr*(sin hRec, cos hRec)
y   = ux*(rv.z-rr.z) - uz*(rv.x-rr.x)         // + = izquierda
psi = WrapPi(hseg - yaw)                       // yaw := courseRad (rumbo de la velocidad)
vf  = max(v, 3.0)                              // FFP_VMIN_MS
lead = vf*0.06                                 // FFP_LEAD_S ≈ retardo del actuador
a0 = max(0, a+lead-V/2); a1 = a0+V (si a1>fwdM: a1=fwdM, a0=max(0,a1-V)); a1-a0 ≥ 0.5
kFF = WrapPi(hRec(a1)-hRec(a0)) / (a1-a0)      // por VENTANA, nunca el k por wp
KY = ω²/vf²,  KPSI = 2ζω/vf,  ω=1.4, ζ=0.9
db(x,e) = x*|x|/(|x|+e)                        // zona muerta C1, sin fase; e_y=0.05 m, e_psi=0.00524 rad (0.3°)
corr = KY*db(y,0.05) + KPSI*db(psi,0.00524) + trim
# caps POR COMPONENTE
corr = clamp(corr, ±MaxCurvature(v)*0.95*(1-|volanteAplicadoAnterior|))   // headroom de rueda
kFF  = clamp(kFF, ±KMaxCmd)                                                 // FF ≤ lo que la toma demostró
if v>1: corr = clamp(corr, ±10/v²)                                          // CORR_ALAT_RECUP = 10 m/s²
k = clamp(kFF+corr, ±MaxCurvature(v)*0.95)                                   // red dura: ruedas
```

**Trim integral** (sólo en el refresco de 40 Hz):
- Si la ley fue válida y ningún cap mordió:
  `trim += (0,77/vf²)·db(y)·dt·(1 − |volAplicado|)`, con clamp ±0,010.
- En cualquier otro caso: `trim *= 0,98` por tick de 40 Hz (reversa, ley inválida o N2 apagado).
- Motivo: KI va en unidades de a_lat y se divide por v². Así la ganancia de lazo no cambia con la velocidad. Con KI constante, a 230 km/h daba un trompo.
- El trim come el error irreducible del FF, de ±0,005 1/m.

**Motivos de la ley:**
- `y` y `ψ` se miden contra la tangente de POSICIONES, no contra el rumbo grabado. El rumbo del humano difiere de la tangente hasta 9° (p90) en curva: es su deriva.
- `ψ` se mide contra el rumbo de la velocidad (`courseRad`), así la deriva propia tampoco entra al corrector.
- El eje trasero absorbe el off-tracking del vehículo largo.
- El cap por headroom calla al corrector cuando la rueda está en el tope, y lo deja actuar fuerte al salir de la curva.

###### 1.5 Capa de salida del volante (común a pursuit y N2)

- **Congelar la corrección durante un cambio de marcha** (DayZ; en 7DTD no hay caja, se puede omitir).
  - La ventana se abre cuando cambia la marcha comandada y dura `FRZ_MAX_S = 0,80 s`: es el p90 de 23 cambios humanos.
  - Congela (kCorr = 0) sólo mientras el embrague está en transición: `|ratioInstantáneo − nominal| > 0,15·paso a la marcha vecina`.
  - Se congela a cero. Mantener el valor viejo empeoró.
- **Cap de ángulo por agarre** (sólo pursuit): `strGrip = atan(wb·KMaxCmd / clamp(ku, 0,05, 1)) / steerMax`, y `cmd = clamp(cmd, ±strGrip)`.
  - Motivo: dentro del agarre la compensación de subviraje es correcta. En el límite, el ángulo extra es deriva.
- **Contravolante** (sólo pursuit, de frente, v ≥ 60 km/h):
  - `deriva = rumbo del desplazamiento de 40 Hz − yaw`. Sólo se calcula si el paso es mayor que 0,05 m y v ≥ 20 km/h.
  - Si 10° < |deriva| < 90°: `cmd = clamp(deriva/steerMax, ±1)`.
  - Es una identidad geométrica: no lleva rampa, ni cap de agarre, ni reloj. Pasados los 90° devuelve el comando base, porque ya no hay un "adelante".
- **Autoridad a baja velocidad** (todos los modos, en Plan y en el refresco):
  `aut = 0,35 + 0,65·clamp(v / escala, 0, 1)`, con `escala = idleCreepKmh` si es mayor que 0,5, o 4,0 km/h si no. Luego `cmd = clamp(cmd, ±aut)`.
  - Motivo: con el vehículo casi quieto, las ruedas cruzadas sólo aran y desalinean.
- **El refresco de 40 Hz no actúa** por debajo de `max(1,0, idleCreepKmh)` km/h, porque inyectaría en vez de corregir. Tampoco actúa en los módulos de pose (maniobra, endpoint).
- **Slew:** `tasa = min(3,0, 1,396 rad/s / steerMax)` en unidades normalizadas por segundo. `applied += clamp(target − applied, ±tasa·dt)`.
  - 1,396 rad/s = 80°/s, el p90 medido de la cremallera humana en toda la flota.
  - En 7DTD el motor ya aplica `SteerRate` al `MoveTowards`: medí cuánto y no dupliques el slew.

###### 1.6 La cinta: replay del volante grabado + "imán" (manda cuando posee)

**Qué es.** Un reproductor espacial: el índice es el ARCO, no el tiempo. En cada tick de 40 Hz toma, interpolando linealmente por arco, lo que el humano tenía en ESE punto: ángulo de rueda logrado (`fwDeg`), gas, freno, marcha y velocidad grabada. No suaviza nada: la textura es señal.

**Pérdida.** La cinta se considera perdida si el residuo longitudinal del cursor supera 6 m o el lateral supera 12 m. En ese caso manda el camino reactivo.

**Posesión.** Es un latch por leg:
- Se arma por flanco: el vehículo tiene que haberse visto a menos de 2 km/h en ESTE leg y después superar 2 km/h, sin ninguna recuperación en curso.
- Se desarma al cambiar de leg. El despegue desde parado siempre es del LAUNCH.
- Posee en CRUISE, APPROACH y MANEUVER.
- En ENDPOINT de reversa sólo toma el volante, y sólo mientras v ≥ 2 km/h. El último tramo lo cierra el endpoint.

**Volante:**

```
psi  = WrapPi(yaw - hRec) [deg];  sPsi = (reversa? -1 : +1)
vF   = clamp((v-120)/(220-120),0,1)
C1 = 3.0 deg/m; C2 = 0.40 + (2.0-0.40)*vF (reversa con |fwRec|<3°: 1.0); cap = 4.5 + (12-4.5)*vF
ref  = ±0.7 m (hacia adentro) si hay un lock largo ≤20 m adelante (≥28°, sostenido ≥24°, largo ≥8 m); sólo de frente
iman = -(C1*(latM-ref) + sPsi*C2*psi)
cap asimétrico: si fwRec ≥ 20°, el lado que ABRE puede llegar a -12°; simétrico para fwRec ≤ -20°
steer = CommandForAngleReplay((fwRec + clamp(iman)) en rad)
```

El imán es un trim: se midió que es el 2,3 % del comando. Dos leyes proporcionales y una integral para cerrar el error pequeño y persistente fracasaron (ver el punto 6).

##### 2. Control longitudinal

###### 2.1 Velocidad objetivo ("el ojo", en Plan)

```
decel = max(MaxBrakeDecel(piso), 0.5)
for i in nearWp..winHi:
    d = arco(cursor→i); vj = TargetAt(i)
    best = min(best, sqrt(vj² + 2*decel*d))      // anticipa: el MÍNIMO manda
objetivo = best
halt si remainM ≤ 0.05 y fwdM ≤ 0.05
```

**`TargetAt(wp)`:**

```
vj = recKmh/3.6
if vj < 0.5 m/s:                       // REC_USABLE_MS
   isStop y no servido → 0 ("STOP")
   isStop servido      → límite de curva      // un STOP ya servido tras un hold no pide 0
   tight               → creepKmh/3.6
   else                → límite de curva (aLat = decel)
else:
   v = PisoDeMordida(vj)                        // sólo en la rampa final
   aLat = max(decel, ALatToma)                  // la toma sólo SUBE el presupuesto
   v = min(v, sqrt(aLat/|k_wp|))                // la curva es SIEMPRE techo; |k|<0.001 → sin techo
```

**Rampa final y piso de mordida** (DayZ: tiene sentido con embrague; en 7DTD probablemente se puede omitir):
- Estamos en la rampa final si `endIsStop` o `endIsCusp`, y todos los wp desde `winHi` hasta el cursor (salvo los isStop) tienen `rec < mordida` de la marcha del tramo, y la mordida es mayor que 0,5 km/h.
- En la rampa final, un wp con `rec < mordida` sube a la mordida si `rec ≥ mordida / 1,10` y `remainM > d* = vMord² / (2·decel)`. Pasado ese punto manda la curva de frenado.
- Motivo: por debajo de la mordida el motor no tracciona y el volante pierde efecto.

###### 2.2 De objetivo a pedales (DerivationEngine)

```
aFree = -CoastDecel(v,piso) - 9.81*sin(pitch)     // lo que el vehículo hace SOLO (medido)
err = objetivo - v;  aReq = (|err|>0.25) ? err/1.0 : 0       // TAU 1 s, zona muerta 0.25 m/s
aReq = max(aReq, -decelObjetivo, -MaxBrakeDecel)
if aReq > aFree: throttle = ThrottleForAccel(aReq - aFree), brake 0
else:            brake = clamp((aFree - aReq)/MaxBrakeDecel, 0,1)   (si MaxBrake<0.5: clamp(aNeed))
```

- La zona muerta significa "mantener", no "soltar". Sólo anula el término de error.

**`ThrottleForAccel(aNeed)`** (modelo de par):

```
si aNeed ≤ 0 → 0
ratio = RatioEngagedNow (rpm·2π/60 ÷ v/r, sólo si v≥2.5 m/s, rpm≥0.5·idle, 0.5<ratio<60) o RatioForGear(gear)
rpm = rpmReal (si ≤0.5·idle: rpm geométrico; mínimo idle)
thr = (m*aNeed*r) / (ratio*0.85) / TorqueAt(rpm)          // DRIVETRAIN_EFF 0.85
si no resuelve (sin par/caja; o caja FALLBACK con aMax medido): thr = aNeed/aMax, o launchFloor si no hay aMax
si 0.01<thr<launchFloor y v<creepKmh: thr = launchFloor
clamp 0..1
```

**En 7DTD:** no hay curva de par ni caja expuestas como en DayZ, pero `Vehicle` declara `MotorTorqueForward/Backward` y `VelocityMaxForward/Backward`. Empezá con la forma degradada `thr = aNeed / aMax` (con `aMax` medido: acelerá a fondo desde parado en llano y registrá la aceleración) y un piso de arranque medido. `moveForward` es continuo (−1..1), así que el gas se aplica directo.

###### 2.3 Marchas (específico de DayZ)

**Índices:** 0 = reversa, 1 = neutro, 2 = primera. **Selección** con la velocidad ACTUAL (nunca la objetivo), debounce de 3 ticks de Plan, reducción inmediata si a fondo pierde velocidad. **En 7DTD no hay caja:** el sentido de marcha es el signo de `moveForward`. Sólo usá la columna `gear` de la toma para saber si el tramo es de reversa (`gear == 0`).

###### 2.4 Reglas de gas transversales (en Plan, después del módulo)

- **Pedal grabado** (sólo pursuit): si el objetivo es mayor que v, `thr = max(thr, gasGrabadoAquí)`. Ayuda a alcanzar el objetivo, nunca a pasarlo.
- **Tope de tracción:** `thr ≤ ruedasEnPiso / ruedas`, con mínimo 0,15. Sin este tope, la rueda gira a fondo en el aire. (En 7DTD: `GetWheelsOnGround()`.)
- **NoSoltarHaciaElBog** (DayZ, anti-ahogo): mira el mínimo grabado en `max(3 m, wb)` adelante y no suelta el gas antes de tiempo si el vehículo va a caer debajo de la velocidad de mordida.
- **NoPlanearHastaElBog:** si gas y freno ≤ 0,02 y `v ≤ 0,5·recAquí`, `thr = launchFloor`, freno 0.
- **Rampa de ARRANQUE** (cualquier modo; sólo ACOTA):
  - Se arma si `v < max(0,5, idleCreepKmh)` (el creep MEDIDO, no el objetivo); el vehículo no está cayendo (no se cumple a la vez `aObs < 0` y |v| > 0,3 km/h); hay gas > 0,01; freno ≤ 0,01; y no está en una meseta grabada.
  - `rampa = piso + n·0,10`, con `n` en ticks de Plan. `thr = min(thr, rampa)`. `pasos = floor((1 − piso)/0,10)` (8 con piso 0,13). Al agotarse, devuelve el mando sin dar veredicto.
  - Motivo: pedir 1,0 de golpe clava el motor debajo de la mordida. Una rampa deja que el motor gane vueltas antes de cargarlo.

###### 2.5 Longitudinal de la cinta (40 Hz, cuando posee)

```
kmhRecEff = kmhRec
si endIsStop: aClav = freno medido de la toma (o MaxBrakeDecel); vClav = 3.6*sqrt(2*aClav*remain)
    si vClav < kmhRec ≤ vClav + max(5, 2.5*aClav): kmhRecEff = vClav; topeFreno = 1.0
dv = kmhRecEff - v
thr = clamp(thrRec + clamp(0.08*dv, -1, 0.5), 0,1)
si thr≈0 y -dv > 1.0: frn = min(0.08*(-dv-1.0), topeFreno=0.35)
brk = clamp(brkRec + frn, 0,1);  gear = gearRec
empate de pedales: si v<2 y kmhRecEff≥2 → (remGate ≤ 0.10 ? thr0/brk1 : brk=0)
```

Además hay un freno anticipado sobre un quiebre de bajada (v ≥ 60 km/h, pendiente ≤ −5°, mira 1 s adelante, margen 2 km/h), y mesetas grabadas (v < 1 km/h) que se reproducen como detenciones temporales (ver E.6.1).

##### 3. Maniobras y cambios de sentido (cusps)

**Árbitro** (en Plan):
- Dwell mínimo de 2 ticks, salvo al entrar a ENDPOINT. ENDPOINT es absorbente.
- **CRUISE → MANIOBRA** si un leg de reversa "necesita maniobra" (`remainM < 1,5·wb`, ×1,5 al salir; o la peor curvatura en 1,5·wb adelante supera `CurvaturaUtilMax`, muestreada en 6 puntos con base VentanaFF sin cruzar el final del leg), o si el leg no termina en parada y `remainM ≤ 3·wb` (cusp o corte de leg).
- **Entrada a ENDPOINT:** `endIsStop` y `remain ≤ max(wb, 2,0)`; o `endIsStop`, `remain ≤ 2·wb` y `v < max(creep, 1)`.
- **Entrada a APPROACH:** `endIsStop` y `remain ≤ max(v²/(2a) + 0,5·v, zonaEndpoint)`.
- **Salida de MANIOBRA en un leg de frente:** se queda si es el mismo leg de la cadena, si `alongM < 3·wb·1,5`, o si v < creep. Salida de última instancia: el final no está cerca y lleva más de 20 ticks en maniobra.
- **Stall:** 40 ticks (20 s) con menos de 0,3 m de arco → SAFE_STOP (ver E.6.4: abre la escalera de rescate).

**ManeuverController** (salida directa):

```
CreepZone = 1.5*wb (wb por defecto 2.5); StoppedKmh = max(0.3, creepKmh*0.5)
signed = signedToEndM;  si remainM > CreepZone: signed = remainM   // lejos, la proyección no significa nada
progreso: dist=|signed|; mejora >0.02 m → contador=0, rampa reinicia; si no: detenido → +1, moviéndose → −1 (drena)
Closed = v<Stopped y ((signed ≤ 0.10 y remainM ≤ CreepZone) o sinProgreso > 20)
   → HOLD (freno 1), LegDone; GaveUp si signed > 0.10
detenido: pitch > 0.03 rad → HILLSTART, si no LAUNCH;  rodando: signed < CreepZone → CREEP, si no TRACK
LAUNCH:    si signed ≤ 0 → freno 1 (nunca empujar para corregir un sobrepaso); thr = launchFloor + n*0.10
HILLSTART: igual rampa; freno 1 mientras thr < clamp(g·sinθ/aMax, 0.1, 1.0) y n ≤ 12 (salida anti-deadlock)
TRACK/CREEP:
   d = max(0, signed-0.10); vT = sqrt(2*max(aBrake,0.5)*d)
   lead = min(v*0.25, wb); si hay recKmh>0.1 en lead: vT = min(vT, rec)   // perfil del humano a la misma distancia
   si no hay grabado y fase CREEP: vT = min(vT, creep)
   tau = 1.0; si err<0 y signed < 2*CreepZone: tau = 0.25     // frenar tarde cuesta precisión
   aNeed = err/tau - aFree → gas por ThrottleForAccel, o freno = -aNeed/max(aBrake,0.5)
volante: L = clamp(|signedToEnd|, 2*wb, 15 (sólo si 15 > 2wb))
   hErr = WrapPi(rumboGrabado a 1 wb adelante − yaw)
   k = hErr/L + 2*crossTrack/L²;  s = SteerCommandForCurvature(k); reversa: -s
```

**Cierre "la cinta clavó el cusp"** (además de `IsLegDone`): el leg se da por hecho si la cinta posee, el vehículo lleva detenido (|v| < 0,3 km/h) al menos `WaitS` (0,5 s), el freno de la fila grabada es mayor que 0,5, y `remainM ≤ CreepZone`. Motivo: de 63 salidas medidas, 19 quedaban cortas por 0,11-0,35 m; la maniobra pedía gas contra el freno grabado hasta rendirse a los 11 s. La parada del humano ya estaba reproducida.

Si el leg termina en parada semántica, se emite ARRIVED y se espera `Resume`. Si no, se avanza de leg y se resetean maniobra y endpoint.

##### 4. Llegada al final (EndpointSlam)

**Entrada a 40 Hz:** `remAhora = remainM − max(0, remFrAlPlan − remFrAhora)`, donde `remFr` es la proyección sobre la tangente final. Es monótona: sólo adelanta la entrada. No usar `min(arco, proyección)`: en un estacionamiento la proyección vale ~0 lejos del final. Una vez en ENDPOINT, cada cuadro aplica freno de frente: `d = remFr − 0,10`; si d ≤ 0,01, freno 1; si no, `freno = max(freno, (v²/2d)/aMax)`.

**Slam** (en Plan):

```
dist = hypot(signed, crossTrack)                       // 2D, no sólo along
progreso: mejora >0.02 → acercando=true, contador=0; si no: detenido → +1, moviéndose → −1
LATCH (freno 1, volante 0, irreversible) si:
   (dist ≤ 0.25 y v < StoppedKmh y !acercando)   o   signed < -0.40 (4×LLEGADA)
   o dist > minDist + 0.10                        o   sinProgreso > 6 (3 s)
GaveUp si dist > 0.25 (se reporta el número real)
LONGITUDINAL (d = signed, el objetivo es el punto, no el borde de la tolerancia):
   d ≤ 0.01 → freno 1
   aWant = -v²/(2d); aNeed = aWant - aFree
   frenoRuido = v<mordida y -aNeed < 0.5*|aFree| ;  detenidoCorto = v < 0.3 m/s
   aNeed < -0.05 y !frenoRuido y !detenidoCorto → freno = -aNeed/max(aBrake,0.5)
   v < creep*0.5 → CARRY: thr = min(1.0, launchFloor + nCarry*0.05)
   aNeed > 0.02 → SOSTIENE: thr = ThrottleForAccel(aNeed)
   si no → planeo (0,0)
VOLANTE: L = clamp(|signed|, 2*wb, 8 (sólo si 8>2wb)); hErr = rumboGrabadoFinal − yaw
   kLat = clamp(2*crossTrack/max(|signed|,wb)², ±0.12) * clamp((|signed|-0.5)/(1.5-0.5),0,1)
   k = hErr/L + kLat;  s = SteerCommandForCurvature(k); reversa: -s   (el término lateral también en reversa)
```

**Límite declarado:** se cierra a unos 10,4 cm en el sedán; por debajo de la mordida, el arrastre del embrague enganchado vence el CARRY (DayZ).

##### 5. Parámetros del vehículo que necesita el control

Precedencia: MEDIDO > CONFIG > DERIVADO > FALLBACK. Una derivación hereda la peor procedencia de sus insumos.

| Parámetro | Uso | Origen en DayZ | En 7DTD |
|---|---|---|---|
| batalla `wb` | Ld, zonas, bicicleta, N2 | contactos de rueda proyectados sobre el eje; si no, envelope | **medir** de los `WheelCollider` (E.3) |
| `steerMax` | todo el volante | envelope > config > 0,61 rad | **leer** `Vehicle.SteerAngleMax` |
| `ku(v)` subviraje | bicicleta, MaxCurvature, cap de agarre | medido por bin de 5 km/h; prior 1,0; piso 0,05 | medir; empezar con 1,0 |
| transferencia comando → ángulo | cmd → ángulo | medida en régimen estacionario | lineal (`moveStrafe · SteerAngleMax`): verificar |
| a_lat de la toma (p90 y p95 por banda) | techos de velocidad y curvatura | de la grabación | igual (sale de la toma) |
| `CoastDecel(v, piso)` | aFree | medido; si no, rodadura + aerodinámico | medir (soltar todo en llano y registrar) |
| `MaxBrakeDecel(piso)` | frenado, zonas | derivado de par de freno y masa, techo μ·g | medir la frenada a fondo; `BrakeTorque` como referencia |
| masa, radio de rueda | modelo de par | runtime / envelope | `vehicleRB.mass`, radio del `WheelCollider` |
| curva de par, relaciones, marchas, idle/redline | ThrottleForAccel, marchas, mordida | config | **no hay**: usar `thr = aNeed/aMax` |
| `maxAccelMs2` | forma degradada, arranque en subida | medido | medir (a fondo desde parado) |
| `launchThrottleFloor` | arranque, CARRY, rampa | calibración en llano con rampa; si no, 0,13 | medir con la misma rampa (+0,02 cada 0,5 s hasta moverse 0,05 m/s sostenido) |
| `idleCreepKmh` / `creepKmh` | "quieto", autoridad, arrastre | medido o derivado del freno | casi seguro 0 (sin embrague): verificar |
| slew 80°/s | slew | medido en humanos | 7DTD ya aplica `SteerRate`: medir |

##### 6. Cicatrices del control (por qué cada regla es así)

**Lateral: pure pursuit**
- **Curvatura siempre por ventana de arco, nunca por waypoint.** La de un solo wp cambia de signo casi en cada punto, y con wp a 2,7 cm satura el clamp de 1,0.
- **Denominador = Ld, no la distancia restante** (entre refrescos de Plan). Si no, la ganancia sube sola: se midió ×2,05 y el lateral llegó a −2,15 m.
- **La mira se refresca a 40 Hz.** El retardo de 0,5 s, no la ganancia, se comía el margen de fase.
- **No acortar Ld en curva.** Eso sube la ganancia y diverge: se midieron 8 m y 18 m. Lo que faltaba era feed-forward.
- **No correr la mira hacia afuera ("flecha").** Sobre un arco, `2·sin α / d = 1/R` exacto. El sesgo le roba α al lazo y el equilibrio queda afuera.
- **Techo primero, piso después.** Y a más de 60 km/h, el corte no puede bajar Ld del diseño (se permiten 1,65 m de corte).
- **Reversa: la referencia va sobre el eje que lidera.** El error era de signo: la misma geometría empuja hacia afuera, y ninguna ganancia lo arregla.

**Lateral: N2 y salida**
- **N2: ψ contra la tangente de posiciones y el rumbo de la velocidad.** Si no, la deriva del humano entra como corrección espuria.
- **KI del trim dividido por v².** Con KI constante, la ganancia de lazo crece con v² y terminó en trompo.
- **Cap del volante en un solo embudo.** Hay dos escritores (Plan y 40 Hz); dos veces un arreglo se aplicó en uno y el otro lo pisó.
- **El cap de agarre va en unidades de ángulo con `ku`.** Capar el ángulo cinemático limitaba al 20-26 % del presupuesto.
- **Se retiraron: aflojar el volante a cero (1,5 s), el freno por trompo y la anticipación del contravolante.** Las tres fueron regresiones netas. Un reloj fijo cuesta 45 m a 107 km/h.
- **Mezclar la rueda grabada como FF a alta velocidad fue negativo.** El volante del humano presupone la posición del humano.
- **La columna de rueda grabada es el ángulo, no la tecla** (la tecla es binaria).
- **Una curva medida tiene que ser sana:** monótona y sin huecos. Si no, se usa la relación lineal.

**Longitudinal**
- **La curva es techo aun con velocidad grabada.** Lo grabado es un permiso: el vehículo puede llegar más rápido que el humano.
- **La toma sólo sube el presupuesto de a_lat.** Si pudiera bajarlo, se frenaría media flota.
- **Un STOP ya servido no pide 0.** En bajada, ese 0 pedía freno y el vehículo quedaba clavado para siempre.
- **Marcha según la velocidad actual** (DayZ). Con la objetivo, el vehículo quedaba en 5.ª a 0 km/h.

**Arranque y recuperación**
- **El piso es un PISO y la rampa tiene que rampear.** Un gas constante no alcanza nunca un umbral.
- **La rampa de arranque se arma con el creep MEDIDO y se desarma si el vehículo cae.**
- **Todo "esperar X" necesita salida** (sin progreso: 20 ticks en maniobra, 6 en el endpoint, 12 en hillstart).
- **Moverse no es avanzar.** El contador de sin-progreso se drena de a uno; no se resetea.

**Maniobras y cierre**
- **`signed` sólo significa algo cerca del final.** Una vez cerró un leg con 189 m de arco por delante.
- **El signo de `signed` es dato.** Usar |signed| hacía que el vehículo manejara al revés para siempre.
- **τ asimétrico, sólo en la zona de precisión.** Aplicarlo en todo el leg mataba la velocidad a 15 m del final.
- **Slam: apuntar al punto, no a la tolerancia.** No aceptar el endpoint mientras el vehículo se sigue acercando. La distancia del endpoint es 2D.

**Cinta**
- **Freno grabado contra un vehículo quieto = empate de pedales.** Se suelta el freno, pero se frena igual a 0,10 m del fin del leg.

##### 7. Ambigüedades detectadas en el código de DayZ (para que no las copies a ciegas)

- `CurvatureLimitMs` usa la curvatura de diferencia centrada ±1,5 m en lugar de la curvatura por ventana.
- A 40 Hz, `kCorr = k40 − kFF` usa el `kFF` del último Plan, no el recién calculado (sólo afecta el congelamiento por cambio de marcha).
- `CurvaturaDesdePose` usa `dist` en el denominador: si el refresco de la mira no aplicó, reaparece la ganancia 1/d que la regla del Plan evita.
- La salida de la cinta se escribe después del slew y no actualiza el volante aplicado: al soltar la cinta, el slew parte de un valor viejo.
- El freno por cuadro del endpoint conserva el offset de 0,10 m que el slam eliminó; `ACEPTA_ENDPOINT_M = 0,25` lo compensa.
- La planta de 85,1°/u es un valor medido en DayZ: no la uses en 7DTD.

#### E.6.3 Legs y árbitro de modos (paradas, cambios de sentido y llegada)

Archivos: `BZRouteData.c` (precálculo y legs), `BZLegMachine.c` y `BZLegView.c` (cursor de la ruta), `BZModeArbiter.c` (modos). La maniobra y el endpoint están en E.6.2 §3-4.

**Dos índices paralelos, no los confundas:**
- el **cursor de la ruta** (sobre `_wp.csv`, 2 Hz): de él salen `remainM`, `isReverse` y `endIsStop`;
- el **cursor de la cinta** (sobre `_cinta.csv`, 40 Hz), de E.6.1.

Sus arcos **no coinciden**: el de la ruta es 3D y el de la cinta es XZ (en una ruta se midió un desfase de 1,35 m). Por eso una parada de la ruta se cruza con la cinta **por posición**, nunca por `s`.

**Precálculo de la ruta al cargar:**
1. **Saneo** al insertar: se descarta un wp que avanza ≤ −2 m sobre el rumbo grabado del anterior aceptado (+π si era reversa). Quedan exentos `legBreak`, `isStop` y los cambios de sentido. Con los dos últimos puntos en vez del rumbo grabado, el saneo cascadeaba: 451 wps descartados.
2. **Arco** `S[i]`: la suma de distancias 3D.
3. **Rumbo** `Head[i]`: el grabado (col 15, +π en reversa). Si no hay dato, la tangente.
4. **Suavizado** del rumbo: promedio de sin y cos sobre ±2 wps, sin cruzar cortes de tramo. El yaw grabado tiembla 0,2-2,9°/wp.
5. **Curvatura** `k[i]` = `WrapPi(Head[b] − Head[a]) / (S[b] − S[a])` sobre una ventana de **±1,5 m**, con clamp ±1 (+ = derecha). La curvatura de wp a wp alternaba de signo y capaba la velocidad en las rectas.
6. **Agarre lateral de la toma**: `aLat = v²·|k|`, con v ≥ 5 m/s.
   - `ALatToma` = p90 (tope 40), para la velocidad. `ALatDir` = p95 (tope 100), para el volante. Los percentiles se calculan por bisección sobre un rango fijo.
   - También se calculan por bandas de 20 km/h. La consulta interpola entre bandas y **nunca da menos que el global**, por el downforce.
7. **Legs.** Empieza un leg nuevo en `i` si `legBreak[i]` o si cambia `isRev`.
   - Un leg de **un solo wp** se absorbe en el anterior. Con `legBreak` en el 211 y la reversa en el 212 nacía un leg fantasma, el árbitro no veía el cambio de sentido y el vehículo se congelaba.
   - El **sentido** de cada leg se decide por **mayoría de votos** de sus wps con `recKmh ≥ 1`, no por el primero: parado, el vehículo conserva la marcha que traía.
   - `endIsStop` = el último wp del leg es `isStop` o es el último de la ruta. `endIsCusp` = hay otro leg después.

**Leg-machine: dueña única del cursor de la ruta** (una vez por Plan).
- Proyecta `pos` sobre los segmentos del leg, hasta 60 m adelante del cursor. El cursor sólo acepta arco **mayor** (es monótono).
- **Readquisición:** si el mejor segmento de los últimos 25 m está a menos de 1/1,5 de la distancia del mejor adelante, el cursor retrocede ahí. El disparador no es un umbral lateral, porque estar **atrasado** es distinto de estar **desplazado**.
- `SeekTo(wp)` es la única otra forma de retroceder el cursor.

**LegView: lo que leen los módulos:**
- `alongM` = arco recorrido desde el inicio del leg; `remainM` = arco que falta.
- `crossTrackM` = lateral contra el segmento del cursor. **+ = izquierda**: es la convención contraria a `latM` de la cinta.
- `headingErrRad` = `WrapPi(Head(cursor) [+π en reversa] − yaw)`.
- `signedToEndM` = `(fin − pos)·(sin θ, cos θ)`, con θ el rumbo del **último segmento del mismo leg**. `Head(fin)` ya es el rumbo del leg siguiente: en un cambio de sentido apunta al revés, y cerraba el leg en el primer tick.
- `SampleAt(ds)`: posición, rumbo y curvatura interpolados a `ds` metros adelante.

**Cierre de un leg:**
1. **Maniobra cerrada** (E.6.2 §3): `v < max(0,3; creep/2)` y además una de dos: `signed ≤ 0,10 m` con `remainM ≤ 1,5·wb`, o más de 20 ticks sin progreso. Cerrar con `signed > 0,10` cuenta como "se rindió".
2. **La cinta clavó el cambio de sentido:** la cinta posee, el vehículo está quieto ≥ 0,5 s, el freno de la fila > 0,5 y `remainM ≤ 1,5·wb`.
3. **Endpoint:** el slam latchea (E.6.2 §4).
4. **Abandono:** más de 20 ticks de CLAVADO sin recuperación en curso (E.6.4).

- `OnLegDone`: si `endIsStop` ⇒ `OnArrived`. Si no ⇒ siguiente leg (y `OnArrived` si no hay más).
- `OnArrived`: el árbitro pasa a HALTED. En un leg intermedio se emite `ARRIVED` y se espera `Resume()`. En el último, `ROUTE_COMPLETE`, o `ROUTE_INCOMPLETE` si algún leg se rindió o fue abandonado, o si el error final (hipotenusa longitudinal-lateral) supera 0,25 m.
- **Un solo umbral de llegada:** `LLEGADA_M = 0,10 m`. La aceptación del endpoint es 0,25 m, porque el freno por cuadro estaciona ~0,10 m antes. En DayZ existían dos umbrales (0,10 y 0,12) y daban veredictos opuestos. **La tolerancia no se mueve para aprobar una corrida.**

**El árbitro de modos** (en Plan). Modos: `INVALID 0 · IDLE 1 · CRUISE 2 · APPROACH 3 · MANEUVER 4 · ENDPOINT 5 · HALTED 6`.

Zonas, con `wb` la batalla (2,5 m si no se conoce):
- `ManeuverZone = 3·wb`.
- `EndpointZone = max(wb, 2 m)`.
- `ApproachZone = max(v²/(2a) + 0,5·v, EndpointZone)`, con `a` la frenada máxima.
- `LlegoLaHoraDelSlam` = `endIsStop` y además `remain ≤ EndpointZone`, o `remain ≤ 2·wb` con `v < max(creep, 1 km/h)`.
- `ReversaNecesitaManiobra` = `remainM < ventana` (×1,5 al salir), o la peor curvatura de los próximos 2·wb (7 puntos, **sin medir a través del fin del leg**) supera la curvatura útil del vehículo.

```
Decide():
  si el estado es incoherente (NaN, sin leg): SAFE_STOP      // la seguridad PRIMERO, sin dwell
  si IDLE:    entrar al modo inicial (reversa: MANEUVER si la necesita; si no CRUISE)
  si HALTED:  quedarse (sólo sale por Resume)
  si 40 ticks (20 s) sin que el cursor de la ruta avance 0,3 m: SAFE_STOP  // por ARCO, no por velocidad
  next = NextMode(); si next != modo y (next == ENDPOINT o dwell ≥ 2 ticks): entrar
NextMode():
  CRUISE:   reversa que necesita maniobra → MANEUVER · fin de leg (no parada) a ≤ 3·wb → MANEUVER
            · slam → ENDPOINT · endIsStop y remain ≤ ApproachZone → APPROACH · si no CRUISE
  APPROACH: (no vuelve a CRUISE) igual que CRUISE sin la última rama
  MANEUVER: slam → ENDPOINT · en reversa: sigue mientras la necesite, si no CRUISE
            · salida de último recurso: > 20 ticks y el fin no está cerca → CRUISE
            · mismo leg de la cadena, o alongM < 4,5·wb, o v < creep → MANEUVER · si no CRUISE
  ENDPOINT: absorbente; sale por OnArrived → HALTED
```

- **Entrada al ENDPOINT a 40 Hz:** mismo predicado del slam con `remAhora = remainM − max(0, remFrAlPlan − remFrAhora)`, donde `remFr` es la proyección sobre la tangente final. A 16 km/h un tick de Plan son 2,2 m contra una zona de 3,2 m, y la velocidad de llegada quedaba a la suerte del muestreo. **No uses `min(arco, proyección)`**: en un estacionamiento la proyección vale ~0 lejos del final.
- **Histéresis en la distancia, no en la curvatura.** En curvatura abría una banda del 50 % y la reversa nunca salía de MANEUVER.
- **SAFE_STOP** no es un final: con la escalera de rescate disponible, la abre (E.6.4). Sólo es `STUCK` si la escalera está agotada o desactivada.

#### E.6.4 AutoRecovery: que no se quede detenido

Archivos: `BZAutoRecovery12.c` (episodios y escalera), `BZWorldProbe.c` (sondeo de obstáculos), `BZDriveController.c` (detectores y acciones) y `BZBusService.c` (re-ubicación y spawn, del lado del runner).

**El alcance, que es una regla de diseño.** El framework garantiza que el vehículo **no se quede detenido para siempre** y **avisa**. Qué hacer con un vehículo bloqueado (retirarlo, esperar, mandar otro) es del mod que lo usa.

##### 1. La base: la velocidad leída no es movimiento

- En DayZ, un vehículo que empuja un obstáculo lee 0,3-0,7 km/h: 70 s a fondo contra un bus estacionado.
- Detenido a 1 cm de una fila de reposo lee 0,06-0,5 km/h.
- Al despegar desde parado lee hasta ~1,8 km/h sin moverse 10 cm (en el 63 % de 203 despegues humanos).

**Por eso el progreso se mide por posición sobre la ruta.** Hay dos medidas:
- **el cursor de la ruta** (`alongM`), que es monótono: retroceder no lo baja, queda plano;
- **el arco de la cinta** (`sEst`): `SinProgresoT` suma mientras `sEst` avanza menos de 1 cm por tick. Como la odometría no tiene signo, **retroceder lo hace avanzar**, y el retroceso necesita su propio reloj.

**[port]** Medí primero qué lee 7DTD en estos tres casos (empujar, quieto, despegar). Si `rigidbody.velocity` no miente, algunas guardas sobran. Si miente, aparecen las mismas trabas.

##### 2. Detectores

| Detector | Reloj | Dispara cuando | Motivo |
|---|---|---|---|
| **Choque (el del AR)** | 40 Hz | en 0,5 s el máximo fue ≥ 20 km/h y bajó ≥ 20 km/h | derivar a rescate físico y descartar el ahogo. Vale 60 s en un radio de 8 m |
| **Choque/bache (el que ve el consumidor)** | 40 Hz | desaceleración de más de 1,5 × max(g, frenada máxima). Sin contacto y con < 5 km/h perdidos es BACHE | una línea **por episodio** (5 s); a 2 Hz un impacto de 25 ms no existe. Al salir de un hold, anular la velocidad previa (si no, cada salida de parada era un choque falso) |
| **Firma no-física** | 40 Hz | la cinta posee, quieto ≥ 0,5 s, modo CRUISE, sin meseta y la fila grabada **no es de salida**: `kmhRec > 2` o `brk > 0,5` | el humano ahí seguía rodando o frenando, y eso aplicado a un vehículo quieto no lo mueve. El OR cubrió el 100 % de los casos simulados |
| **CLAVADO** | 2 Hz | gas **pedido** ≥ 0,95, todas las ruedas en el piso, sin meseta, y `alongM` no avanzó 0,30 m durante 11 ticks (5,5 s) | pide a fondo y no avanza. Si faltan ruedas en el piso y está quieto: "RUEDAS EN EL AIRE" (físico directo) |
| **Traba por progreso** | 2 Hz | `SinProgresoT ≥ 10 s` con gas pedido ≥ 0,95, ya largó (wp > 0), sin meseta y sin rescate en curso | el que empuja, oscila o repta: ni CLAVADO ni el reposo lo ven |
| **AHOGO** | 2 Hz | tramo de avance, marcha ≥ primera, gas aplicado ≥ 0,5, freno ≤ 0,1, ruedas en el piso, sin choque reciente, y en 3 s avanzó < 0,5 m **y** se lo vio ir hacia atrás o el motor bajó al ralentí | movimiento sin avance. **[port]** Sin caja ni rpm, el ahogo no existe como tal: saltá C1 y dejá que esto lo cubra la traba por progreso |
| **SAFE_STOP** | 2 Hz | 20 s sin 0,3 m de avance del cursor de la ruta (E.6.3) | la red de seguridad del árbitro |

##### 3. Niveles de respuesta, de menor a mayor

**Nivel 1: re-arranque lógico** (sin mover el vehículo). Cuando da verde la firma no-física:
- Si el gas de la cinta es ≥ 0,9, o hubo un choque reciente, se deriva a **físico**: el cerebro no está trabado, es el vehículo el que no puede.
- Si no: se **suelta la cinta hasta el fin de la meseta** (E.6.1) y el LAUNCH relanza con su rampa.
- Máximo 3 intentos **por episodio** (un episodio = un lugar, radio 8 m), con 1 s entre intentos. Por ruta, un bus se quedaba sin intentos en la cuarta parada.
- **Éxito de cualquier acción:** ≥ 1 m desde donde se aplicó **y** ≥ 2 km/h **en el sentido del tramo**. Rodar para atrás no cuenta: un bus ahogado dio "avanza a 2,26 km/h" yendo hacia atrás. Vence a los 15 s.

**Nivel 2: C1, primera + arranque en subida** (sólo DayZ, ver la tabla). Rampa de gas desde el piso medido, con freno hasta que el gas alcance para la pendiente. Si avanzó pero se quedó 3 s a menos de 2 km/h, o en 10 s hizo menos de 5,56 m, el veredicto es **"traba, no ahogo"** y la escalera se abre **en ese mismo tick**: esperar otro ahogo costaba 32 s.

**Nivel 3: la escalera física.** Un solo punto de entrada, `Fisico(evidencia)`. Toda evidencia física nueva con la escalera abierta **sube un escalón**.
1. **Escalón 1, "salir manejando":** alejarse 3 m **en contra del sentido del tramo** (en un tramo de reversa, hacia adelante), hasta 5 km/h; después 1 s de freno y 4 s de gracia.
   - **Antes, sondear** desde el paragolpes (largo/2, no el centro): dos círculos de 2,5 m en `pos + dir·(mitad + 1)` y `pos + dir·(mitad + 1 + 3)`.
   - Cuenta **cualquier objeto con colisión**: vehículos, garitas, postes, guardarraíles, árboles, rocas. No cuentan las personas, los ítems ni los arbustos. Si hay algo, se salta al escalón 2 sin gastarlo. En DayZ la maniobra reversó 3 m contra una garita que estaba a 1,84 m.
   - Corta con **margen de frenado**: `d + v²/(2a) ≥ 3 m`. Sin él, 3 m pedidos dieron 4,89 m.
   - Sin todas las ruedas en el piso (hundido o volcado), se salta.
2. **Escalón 2, re-ubicar** en el último tramo "bien pisado" **menos una batalla**.
   - "Bien pisado" = un intervalo continuo de arco donde el vehículo iba hacia adelante a ≤ 1 m de lateral.
   - Se resta una batalla porque el bien pisado de un choque queda pegado al impacto (a 0,57 m).
3. **Escalones 3 en adelante, zonas seguras:** re-ubicar **adelante** del punto trabado, en `ZonaBase + k·1 m` (k = 0..6).
   - `ZonaBase = largo/2 + 1,5 m`: es la primera pose en la que el paragolpes trasero pasa el punto trabado. Con +2 m fijos, un vehículo de 7,66 m caía montado sobre el obstáculo.
   - Pasado el máximo, la escalera queda **AGOTADA**.

**Re-ubicación** (la hace el runner):
- Pose = la **grabada** en la cinta en ese `s` (posición interpolada y rumbo del frame), a `y + 0,10 m`.
- Se rechaza si el destino es de un tramo del otro sentido, o si hay un vehículo en la **huella**: círculos cada 1,5 m a lo largo del eje, no un solo círculo central. Un vehículo largo quedó montado sobre un sedán con "destino libre".
- Se anula la inercia (`impulso = −masa·velocidad`), se ancla la cinta en `s`, se reinicia el cursor de la ruta y el LAUNCH arranca.
- Sonda a 1,5 s: si le faltan ≥ 2 ruedas en el piso y quedó > 0,3 m arriba del suelo, está **montado** y se rechaza en el acto.
- **Un rechazo no gasta escalón:** en los escalones ≥ 3 se barre la misma zona 1 m más adelante, hasta 10 m de barrido. Dos escaleras se habían agotado sin probar ninguna pose usable.
- Un escalón que no dio éxito en 15 s **sube solo**.
- La escalera se cierra cuando el vehículo se aleja ≥ 8 m de donde se abrió.

**STUCK** sale sólo del SAFE_STOP y significa una sola cosa: **20 s sin avance y la escalera agotada o desactivada**. Siempre lleva su motivo.
- Un SAFE_STOP con un escalón en curso **no** es STUCK: el árbitro se reinicia y re-decide en el mismo tick.
- Un SAFE_STOP sin escalera abierta **la abre**. Antes, un bus pasó 22 s contra otro y se declaró STUCK con 0 intentos.

##### 4. Lo que se le avisa al consumidor

| Señal | Qué dice |
|---|---|
| evento `BLOQUEADO` | la escalera **se abrió**. Sale en la transición, sea quien sea el que la abrió, una vez por escalera. Lleva la evidencia y si hubo choque |
| `Bloqueado()` · `BloqueadoMotivo()` · `BloqueadoDesdeS()` | la escalera está en curso. El runner loguea "BLOQUEO terminado tras N s"; control: cantidad de BLOQUEADO = cantidad de terminados |
| `Recuperando()` | superconjunto de lo anterior: C1, maniobra, re-ubicación pendiente o escalera en curso. Responde "¿este coche va a poder moverse ahora?" |
| `ChoqueHaceS()` | segundos desde el último choque (no bache); −1 si nunca hubo |
| evento `STUCK` + `MotivoStuck()` | la ruta terminó sin poder seguir |
| `ROUTE_INCOMPLETE` + motivo | se abandonó el último leg, o el final quedó fuera de tolerancia |
| `EsperandoCabecera()` | el punto de salida está ocupado (ver spawn). Es una espera, no una falla |

**[port]** Una línea de log por acción, con el formato `fecha, ruta, vehículo, wp, s, tipo (NO-FISICO|FISICO|AHOGO), evidencia, acción, resultado`. Cada `return false` del rescate guarda **por qué** no actuó: un rescate que dice "no" es indistinguible de uno que no se llamó.

##### 5. Configuración por ruta (en el `_hdr.json`; una clave sólo pisa el default si está escrita)

`AutoRecovery.Logical.{Enabled true, WaitS 0,5, MaxAttempts 3, CooldownS 1,0}` · `AutoRecovery.Physical.{Enabled true, ReverseM 3,0, ReverseMaxKmh 5,0, LadderMaxM 6,0, SweepMaxM 10,0}` · `BogRecovery.{Enabled true, WindowS 3,0, MinProgressM 0,5, MaxAttempts 2}` · `ManualAR.{AdvanceM 10,0, GoodLatM 1,0}`.

Un bloque ausente **no** se lee como ceros. En DayZ, el lector JSON inicializaba en cero un bloque anidado ausente y el rescate quedaba apagado por defecto.

##### 6. Spawn robusto

1. **Altura:** `y = y_grabada + 0,10` si está a ≤ 1,5 m del terreno. Si no, `terreno + 0,5`. Con `terreno + 0,5` fijo, un pickup bajo cayó 0,7 m sobre la carrocería y quedó con 0 de 4 ruedas en el piso.
2. **Punto de salida ocupado = ESPERAR, no correr el punto.** Si hay un vehículo a menos de 3,5 m del wp 0, no se crea nada y se reintenta cada 5 s, sin tope, logueando quién ocupa. Corrido 4 m, un bus nació fuera de su toma, junto a un tramo de reversa, y terminó en STUCK.
3. **Re-afirmar la pose después de crear.** En DayZ el motor pone el **centro del modelo** (no su origen) en el punto pedido y mirando al norte: el bus quedaba corrido 0,22 m y 0,73 m arriba. Hay que hacer `SetPosition(pedida)` y fijar el rumbo apenas se crea.
   - **Rumbo:** del wp 0 al primer wp a más de 2 m, +180° si ese tramo es de reversa.
   - Recomendado: congelar la física, orientar, anular las velocidades y recién ahí activar la física.
4. **Verificar a los 3 s:** posición, rumbo, pitch, roll y el desvío respecto de lo pedido.
   - **[port]** Esta es la primera medición que tenés que hacer en 7DTD. No sabemos si Unity pone el origen o el centro del prefab.
5. **Sonda de ruedas** a 1,5 s y a 3,5 s: cuántas tocan el piso.

### E.7 — (Opcional) La red vial de 7DTD, si algún día querés rutas sin grabar

El framework **no la necesita**: la toma ES la ruta. Pero si querés generar recorridos (un taxi que va a cualquier POI), 7DTD la trae en un PNG:
- **`splat3_processed.png`** en la carpeta del mundo **es la red vial** (el propio `WorldBuilder` lo rotula `xuiRoads`). **1 píxel = 1 bloque = 1 m**, imagen de `WorldSize × WorldSize` (6144² en Navezgane y en Pregen 6k). Coordenadas de mundo centradas: `x_mundo = px − WorldSize/2`.
- Colores (`WorldGenerationEngineFinal.PathShared.IdToColor`): negro = libre; **G (0,255,0)** = camino rural y autopista de tierra; **R (255,0,0)** = autopista; B = agua.
- ⚠ Usá `splat3_processed.png`, **no** `splat3.png`: el crudo tiene sólo lo que planificó el generador (autopistas y rurales) y queda desconectado en los pueblos; el `_processed` trae las calles de pueblos y POIs fusionadas (4× más píxeles).
- Extracción offline: `skeletonize` + transformada de distancia (ancho de calzada = distancia × 2). Medido: Navezgane 127 km de eje, 1.218 nodos, ancho mediano 8 m; Pregen06k01 478,5 km, 26.580 nodos, 5,7 m.
- ⚠ En los pueblos, playones y entradas de garaje están pintados como calle: el esqueleto arma telarañas (tramo mediano 4 m). Podá espolones y filtrá por ancho, o usá `splat3.png` como capa arterial y el `_processed` para lo local.
- **No hay API de "esto es calle" en tiempo de juego** (se buscó): la extracción es offline y el mod lleva el grafo como dato.
- `prefabs.xml` da los POIs con posición de mundo (3.469 en Pregen06k01): destinos de taxi gratis.
- Alternativa de más fidelidad, sólo en mundos generados (RWG): `WorldGenerationEngineFinal.Path` tiene `FinalPathPoints` / `pathPoints3d` (polilíneas vectoriales con carriles y radios), pero vive sólo en memoria durante la generación; habría que engancharse a `WorldBuilder` para volcarlo.
**Ojo con la tesis:** una ruta generada desde un grafo **no tiene cinta** (nadie la manejó). Se maneja con pure pursuit + velocidad por curvatura (lo que hace la prueba de concepto de E.3), que es bastante menos fiel que reproducir una toma. En DayZ la conclusión fue: **grabá lo que importa; generá sólo lo que no se puede grabar.**

### E.8 — Lecciones del framework que valen en cualquier motor

Cada una costó días en DayZ. Son la parte más valiosa de esta guía.

1. **Fidelidad espacial antes que temporal.** Indexá la reproducción por **distancia recorrida sobre la ruta**, nunca por tiempo. Si el NPC va más lento en una subida, reproducir por tiempo lo desfasa y aplica el volante de la curva siguiente en la recta; por distancia, el volante de cada punto se aplica en ese punto.
2. **La velocidad leída no es movimiento.** Un vehículo que empuja algo, o que patina contra un cordón, marca medio km/h sin avanzar un metro; y al arrancar desde parado, el motor de física puede reportar hasta ~2 km/h sin que el vehículo se haya movido 10 cm. **Para decidir si está trabado, medí el avance sobre la ruta (el cursor), no el velocímetro.**
3. **Una parada grabada no es una traba.** Donde el humano se detuvo, el vehículo tiene que detenerse, y ningún detector de "está trabado" puede dispararse ahí.
4. **Un "motor ahogado" y un empuje se ven igual** (acelerador a fondo, motor en ralentí). Si el remedio del ahogo no funciona en unos segundos, es un empuje: pasá al rescate.
5. **El rescate no puede retroceder contra lo que tiene detrás**, y en un tramo en reversa "alejarse" es ir hacia adelante. Sondeá **cualquier objeto con colisión** en la huella de la maniobra, no sólo vehículos.
6. **No declares "trabado" sin haber intentado nada**, ni en medio de un intento. La ruta se da por perdida sólo con el rescate agotado, y el log dice por qué.
7. **El punto de salida ocupado se espera, no se corre:** un vehículo que aparece al costado de su cinta no la puede seguir.
8. **Al crear un vehículo, verificá dónde quedó** unos segundos después (posición, rumbo, inclinación) y logueá el desvío respecto de lo pedido. En DayZ el motor ponía el centro del modelo en el punto pedido (no el origen) y cada vehículo nacía corrido 0,2-0,4 m sin que nadie lo notara durante semanas.
9. **Nada que dure más de un par de milisegundos en un solo cuadro.** Cargar una ruta de 17.000 puntos en un cuadro congela el server; repartila en muchos cuadros con un presupuesto por cuadro, y medí el **peor** cuadro, no el promedio.
10. **Contá por episodio y por lugar.** Diez líneas de log del mismo choque son un choque; y los problemas se repiten en **lugares** (una curva cerrada, una parada con una garita), no en números de punto de ruta.
11. **Lo que decide el consumidor, el framework no lo adivina.** Cuándo lanzar, dónde esperar, a quién ceder el paso, cuándo retirar un vehículo: eso es de tu mod. El framework garantiza que el vehículo no se quede detenido para siempre y **avisa**; no decide por vos.
12. **La toma es una medición.** No se edita. Si algo sale mal en un lugar siempre igual, primero mirá la toma en ese lugar: muchas veces el humano pasó demasiado cerca de algo, y la solución es regrabar ese tramo.
