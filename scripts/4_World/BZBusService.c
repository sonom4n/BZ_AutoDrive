// Estado de boarding animado por bot (caminar->abrir->subir->cerrar). Usado para el crew (Stage 1).
class BZBoardState {
    eAIBase bot;
    int     seat;
    int     phase;     // 0=esperando turno (gate de proximidad), 1=caminando, 2=subiendo, 3=sentado/done
    float   timer;
    bool    hasDoor;
    string  doorSrc;
    vector  entry;     // entrada (CrewEntryWS) cacheada al spawn -> clasificacion + gate de proximidad
    bool    teleport;  // clasificado teleport-board (dup exacto / techo / garbage) -> NO camina, se sienta directo
    float   lastDist;  // ultima dist XZ a la puerta (timeout por PROGRESO, no por tiempo absoluto)
    float   stuckTimer;// seg sin acercarse -> si supera BOARD_TIMEOUT_S, recien ahi teleport fallback
}

// Registro de un vehiculo spawneado VACIO (START/HERE/END del reproductor). NO es un runner: es un auto
// parado que aparece en el panel "ACTIVE SPAWN VEHICLE" con TP (llevar al player) + ELIMINAR (Fase 2).
class BZEmptyVehicle {
    EntityAI veh;        // el vehiculo spawneado (handle persistente de CreateObject)
    string   routeName;  // ruta de la que salio (para etiquetar la fila)
    int      wpIndex;    // wp donde se spawneo
    vector   pos;        // posicion de spawn
}

class BZBusService {
    private static ref BZBusService s_Instance;          // "primary" runner (legacy single-bus; GetInstance lo devuelve)
    private static ref array<ref BZBusService> s_Runners; // MULTITON: registro de todos los runners activos (Fase 1 = 1)
    private static ref array<ref BZEmptyVehicle> s_Empties; // Fase 2: vehiculos spawneados vacios (panel ACTIVE SPAWN VEHICLE)

    static const string CONFIG_PATH       = "$profile:BZ_AutoDrive/BZBusRoute.json";
    static const string SETTINGS_PATH     = "$profile:BZ_AutoDrive/BZAutoDrive_settings.json";
    static const float  WAYPOINT_RADIUS   = 15.0;   // trigger del scan de pasajeros en paradas
    // WAYPOINT_RADIUS_PARKING: radio chico para waypoints con mode="parking".
    // En parking los wps estan a ~14cm uno del otro (5 km/h x 100ms sampling).
    // Con el radius default de 15m, el bot "alcanza" decenas de wps en un solo
    // tick y Stanley se salta al medio de la maniobra Ã¢â‚¬â€ explica el giro 90Ã‚Â°
    // ejecutado 2m antes del lugar correcto.
    // 0.8m era demasiado estricto: tras el giro 90Ã‚Â° el bot quedaba trabado en
    // un wp del medio de la curva porque no llegaba al siguiente, manteniendo
    // el volante girado y derivando off-course. 1.2m da margen suficiente para
    // avance limpio post-curva sin reabrir el bug de 15m.
    static const float  WAYPOINT_RADIUS_PARKING = 1.2;
    static const float  STOP_FINAL_RADIUS = 3.0;    // distancia real a la que el bus frena full en parada
    static const float  LOOKAHEAD_DIST    = 10.0;   // pure pursuit: apuntar a un punto interpolado sobre la ruta a esta distancia. Aim al PATH (no al centroide, que cortaba curvas por adentro).
    static const float  LOOKAHEAD_DIST_MIN = 5.0;   // lookahead en curvas pronunciadas (zigzag, rotonda)
    static const float  LOOKAHEAD_CURVATURE_WINDOW = 10; // wps adelante para medir curvatura local
    static const float  LOOKAHEAD_CURVATURE_LOW    = 0.3; // rad: por debajo de esto, lookahead full
    static const float  LOOKAHEAD_CURVATURE_HIGH   = 1.5; // rad: por encima, lookahead minimo
    // Corredor (rieles) - Stanley controller simplificado. Antes habia un
    // deadband absoluto: dentro del corredor target yaw = segmento, fuera =
    // lookahead. Eso producia zigzags amplios a alta velocidad por la
    // discontinuidad del input al cruzar el umbral (volantazo + inercia del
    // chasis -> sobreoscila al otro lado -> volantazo opuesto -> repeat).
    //
    // Reemplazado por Stanley simplificado (2026-05-24 sesion segunda corrida):
    //   targetYaw = segmentHeading - atan(K * lateralOffset / velocity)
    // Corregimos PROPORCIONALMENTE al offset, sin escalon. Replica el patron
    // del operador humano con teclado (a-a-a-d-d-d): correcciones leves y
    // frecuentes, no volantazos discretos. Bonus: el divisor por velocidad
    // atenua la correccion a alta velocidad (rectas) y la fortalece a baja
    // velocidad (rotonda, maniobras precisas), exactamente la asimetria que
    // observamos empiricamente en la primer corrida con corredor naive.
    //
    // CORRIDOR_HALF_WIDTH ya no es un umbral de activacion, ahora se usa solo
    // como referencia para el AI logger (clasificar samples en _inC / _outC
    // para analisis posterior).
    static const float  CORRIDOR_HALF_WIDTH   = 1.05;
    // 2026-06-10: BACK reducido de 20 a 4. Con BACK=20 el corridor podia
    // enganchar segmentos VIEJOS del arco en curvas tomadas a velocidad: Boris
    // corta la cuerda, queda cerca (perpendicular ~0) de un segmento de hace
    // muchos wps, ComputeCorridorInfo lo elegia (criterio min-perpendicular) y
    // reportaba corridor_offset ~0 mientras el offset REAL al path era ~9m.
    // Stanley corregia sobre el offset falso (~0) y NO giraba -> Boris seguia
    // derecho y el offset crecia hasta 9m (validado ai_run Zenit curva 2,
    // wp 1116->1347: latdev 0.1->9.1m con steering ~0). Con BACK=4 la ventana
    // es chica: al INICIO de la curva (offset ~1m) engancha el segmento correcto
    // y Stanley corrige temprano, antes de que el offset crezca. No afecta
    // rectas (segmento correcto siempre en ventana) ni movimiento lento
    // (fallback second-pass camina forward). Solo arregla curvas a velocidad.
    static const int    CORRIDOR_SEARCH_BACK  = 4;  // wps atras
    static const int    CORRIDOR_SEARCH_FWD   = 5;  // wps adelante
    // REVERSE_CONTROL_OFFSET: en reverse el eje TRASERO lidera el movimiento (el eje de
    // direccion delantero trailea). El corridor se referencia al eje trasero (busPos - fwd*off)
    // en vez del centro Ã¢â‚¬â€ sino ve el centro on-track mientras el punto que lidera esta off
    // (Sonom4n 2026-06-12). ~mitad del wheelbase. PENDIENTE: per-vehiculo desde el config/wheelbase.
    static const float  REVERSE_CONTROL_OFFSET = 1.4;  // metros atras del origen, solo en reverse
    // Modelo bicicleta en reverse (rear-steer archetype). Defaults validados contra
    // literatura de vehicle dynamics (Rajamani / Snider / MathWorks Stanley reverse):
    static const float  REVERSE_WHEELBASE_DEFAULT  = 2.7;  // m, ~Golf/compacto si no hay config
    static const float  REVERSE_FF_MAXSTEER_DEFAULT = 0.6; // rad, ~35deg lock tipico de auto
    static const float  REVERSE_FF_SIGN_DEFAULT    = -1.0; // flip: v<0 invierte el yaw rate
    static const float  REVERSE_STEER_GATE_DEFAULT = 0.5;  // m, gate steering=0 solo si on-path
    static const float  REVERSE_STEER_THR_FLOOR_DEFAULT = 0.35; // piso steer-then-throttle (mantiene crawl para rotar)
    // 1.0 = clamp OFF. El anti-overshoot real es FOLLOW-RECORDING (el volante grabado
    // manda, Stanley solo corrige fino), no cortar el full lock que la maniobra necesita.
    // (2026-06-13: clamp 0.5 cortaba el replay grabado de full lock = era el parche equivocado.)
    static const float  REVERSE_STEER_MAX_DEFAULT = 1.0;
    static const float  REVERSE_REC_STEER_THRESH_DEFAULT = 0.2; // umbral para seguir el volante grabado en reverse
    static const float  REVERSE_TARGET_SPEED_CAP_DEFAULT = 25.0; // km/h, techo reverse (sigue la targetSpeed grabada; era 6 = estrangulaba la rampa)
    static const float  REVERSE_STANLEY_FINE_MAX_DEFAULT = 0.15; // cap de Stanley como correccion fina sobre el volante grabado en reverse
    static const float  REVERSE_HEAD_DEADBAND_DEG_DEFAULT = 4.0;  // grados: corregir por ANGULO si el heading esta torcido mas que esto
    static const float  REVERSE_ARRIVAL_TOL = 1.5;               // m: dentro de esto del wp FINAL, dar la reversa por completada. Los wps densos del endpoint (creep-to-stop del humano) no se consumen porque el bus para antes -> wp_idx se clava -> la ruta nunca marca el fin.
    static const int    HANDBRAKE_STRAIGHTEN_LOOKAHEAD = 20;      // wps: enderezar el volante al aproximar un handbrake-stop grabado
    // STANLEY_K: ganancia de correccion lateral. K=1.0 (clasico de literatura)
    // dio el mejor resultado empirico (dev avg lento 0.31m, medio 0.74m,
    // rapido 1.14m). Probamos K modulado por curvatura local (1.0 recta,
    // 2.5 curva) pero EMPEORO: la metrica de curvatura cumulativa detecta
    // rotonda (donde sobre-corrige) pero NO curvas largas a alta velocidad
    // (curvas problematicas reales, donde no actua). Resultado: peor en
    // ambos extremos. Lecciones a paper:
    //   - El residual a alta velocidad es feedback honesto al operador sobre
    //     su estilo de conduccion, no bug del control. Forzarlo con K alto
    //     ocultaria el mismatch grabacion <-> capacidades del vehiculo.
    //   - Curvatura local no es la metrica correcta para detectar zonas
    //     donde se necesita mas correccion. La metrica deberia ser proxy
    //     de fuerza centrifuga (curvatura x velocidad^2), pero el atan(K/v)
    //     ya integrado en Stanley provee la atenuacion necesaria.
    static const float  STANLEY_K             = 1.0;
    // STANLEY_K_PARKING: ganancia agresiva para waypoints con mode=="parking".
    // En parking el throttle/brake/gear vienen del recording (direct replay)
    // pero el steering lo sigue computando Stanley. A baja velocidad (~5 km/h)
    // y con divisor v_ms = 1.0 saturado, K=1.0 aplica correccion muy timida
    // y el bot deriva metros del trazado. K=3.0 jala fuerte hacia la linea,
    // necesario para precision quirurgica en maniobras de galpon/parking.
    // Razon de no usar este K siempre: a velocidad de ruta (>30 km/h) genera
    // zigzag y volantazos. Por eso es solo para parking, baja velocidad.
    static const float  STANLEY_K_PARKING     = 3.0;
    // STANLEY_K_REVERSE: ganancia mas suave para reverse. Stanley flip + steer
    // invert + K=3.0 sobrecorrige el lateral en cada tick Ã¢â‚¬â€ bot deriva del
    // trazado, choca contra paredes (validado AI log 2026-05-31, drift 7m oeste
    // en wp 295 reverse). K=1.5 da correccion gentil, permite curvas modestas
    // en reverse sin overshoot.
    static const float  STANLEY_K_REVERSE     = 0.8;
    static const float  STUCK_TIMEOUT     = 60.0;
    // 2026-06-27: heartbeat de luces + top-up de bateria. El Tick corre cada 500ms
    // (CallLater 500, true), asi que sumamos TICK_DT_S por tick al acumulador y
    // disparamos cuando cruza el intervalo. El heartbeat llama BZPulseLights() (cambia
    // una NetSyncVar -> re-dispara OnVariablesSynchronized en los observadores y re-prende
    // el faro); SetSynchDirty() pelado sin cambiar valor es no-op. El top-up de bateria
    // es secundario (inofensivo).
    static const float  TICK_DT_S            = 0.5;   // periodo nominal del Tick (CallLater 500ms)
    static const float  LIGHTS_HEARTBEAT_S   = 1.0;   // re-sync luces cada ~1.0s (vuelve rapido tras apagon del engine, menos flicker)
    static const float  BATT_TOPUP_S         = 4.0;   // top-up bateria cada ~4s
    // Auto-retry del spawn: si a los SPAWN_VALIDATION_DELAY_MS ms el bus no
    // se movio al menos MIN_DIST metros del spawn point, asumimos que el
    // driver no se sento bien y reintentamos hasta MAX_SPAWN_RETRIES veces.
    // SPAWN_VALIDATION_DELAY_MS = pre-roll (3s) + tiempo real para moverse (5s).
    static const int    MAX_SPAWN_RETRIES         = 5;
    static const int    SPAWN_VALIDATION_DELAY_MS = 8000;
    static const float  SPAWN_VALIDATION_MIN_DIST = 2.0;

    // Pre-roll: tiempo desde SpawnBus hasta que la IA empieza a manejar.
    // Durante este intervalo el bus queda con brake aplicado mientras se
    // asegura el driver, motor y estabilizacion. Articulado por Sonom4n
    // 2026-05-24: "la grabacion es el momento en que Boris arranca motor",
    // separar pre-roll de playback hace el spawn robusto contra timing
    // (driver tarda en sentarse, motor en encender, etc).
    static const float  SPAWN_PREROLL_SECONDS     = 3.0;

    private EntityAI             m_Bus;
    private eAIBase              m_Driver;
    private eAIGroup             m_Group;
    private ref array<eAIBase>   m_Guards;    // bots armados spawneados por spawn_guard (v1 teleport)
    private ref array<eAIBase>   m_Crew;      // bots que viajan sentados desde el arranque (v2)
    private ref array<int>       m_CrewSeats; // asiento de cada bot de m_Crew (array paralelo)
    private ref array<ref BZBoardState> m_CrewBoard; // estado de boarding animado por cada crew (Stage 1)
    private eAIDynamicPatrol     m_CrewPatrol;   // patrulla viva del crew (spawner propio de eAI, como Quest/AICamp)
    private bool                 m_CrewWillBoard = false; // MILESTONE 1: crew patrulla y NO aborda (confirmar movimiento)
    private int                  m_QuestCheckID = -1;     // INTEGRACION QUEST Step 1: id de la quest a inspeccionar
    private int                  m_QuestPollTries = 0;    // INTEGRACION QUEST: intentos del poll de bots (lazy/proximidad)
    private bool                 m_QuestConvoyActive = false; // STEP 2 CONVOY: convoy del quest detectado (bots vivos)
    private bool                 m_QuestFleeing = false;  // STEP 2 CONVOY: ya se disparo la huida (mataste 1)
    private int                  m_QuestInitialBots = 0;  // STEP 2 CONVOY: conteo inicial (para detectar el primer kill)
    private bool                 m_ConvoyDriving = false; // STEP 2 CONVOY: board completo -> vehiculo soltado a manejar (huye al patio)
    private bool                 m_AmbushActive = false;    // ESCENA 2: bots a bordo, vehiculo en ruta, esperando daÃƒÂ±o
    private bool                 m_AmbushTriggered = false; // ESCENA 2: daÃƒÂ±o recibido -> ya se desplego (one-shot)
    private bool                 m_BorisArmed = false;      // ESCENA 2: Boris ya recibio loadout de combate
    private int                  m_AmbushStopTries = 0;     // ESCENA 2: polls esperando que el vehiculo frene antes del dismount
    private ref array<float>     m_CrewLastHealth;          // ESCENA 2: salud previa de cada crew (detecta disparo al bot)
    private bool                 m_EscortBoarded = false;   // ESCORT (AIVIP): el VIP ya fue embarcado (one-shot)
    private int                  m_EscortPollTries = 0;     // ESCORT (AIVIP): intentos del poll buscando al VIP
    private ref BZBusRouteConfig m_Config;
    private static ref BZAutoDriveSettings s_Settings;  // config GLOBAL compartida (admin + tecla panel). Static: 1 sola para todos los runners.
    private int                  m_WaypointIndex;
    private ref array<int>       m_AppliedEvents;   // indices de Events (markers) ya disparados (reset por corrida)
    private int                  m_NextStopIndex; // idx del proximo wp con isStop=true, para frenado por proximidad en DriveTowards
    private bool                 m_AtStop;
    private bool                 m_StopDecided;  // decision tomada en OnWaypointReached, aprox suave hasta STOP_FINAL_RADIUS
    private bool                 m_HoldActive = false;  // API TAXI: detenido (en un stop o por HoldRunner) esperando ResumeFromHold() del eAI
    private bool                 m_HoldAtStops = false; // API TAXI: si true, en cada isStop hace HOLD en vez de auto-continuar
    private bool                 m_Reverse;
    private float                m_StuckTimer;
    private float                m_StuckDiagTimer;   // segundos en condicion "stuck con throttle alto"
    private float                m_StuckDiagLastLog; // tickTime del ultimo dump diagnostico, para no spammear
    private int                  m_DR_PrevWpIdx;     // ultimo m_WaypointIndex visto en DirectReplay, para scanear pulses sub-tick
    private float                m_DR_NoAdvanceTimer; // segundos sin avanzar wps en DirectReplay, para wp_idx snap
    private bool                 m_DR_InRecovery;     // true cuando Boris perturbado (offset > threshold), Stanley toma control hasta volver al corredor
    private bool                 m_OffPath_InRecovery; // true en cruise mode (no DR) cuando Boris esta fuera del corredor: throttle forzado + Stanley puro hasta volver. Independiente del DR recovery.
    private ref BZInverseModel   m_InverseModel;       // Capa 3+4 framework v2. Instanciado en SpawnBus si UseInverseModel=true.
    private float                m_LastInverseModelLog; // throttling de log [InvModel]
    private float                m_SurfPitchSm;        // SurfaceSense: grade REAL suavizado (del forward vector), para forzar/aflojar throttle
    private float                m_LastSurfLog;        // throttle del log [SurfaceSense] (cada 2s)
    private bool                 m_EndpointLatched;    // endpoint: una vez que toco/paro cerca, CLAVA (freno+handbrake) y no creepea mas
    private float                m_EndpointMinDist;    // distancia MINIMA alcanzada al endpoint (para detectar overshoot)
    private bool                 m_StopArrivedDeclared; // 2026-07-29: el "llegue" (notif + countdown) arranca en el LATCH, no a 3m -> el iman clava sin que el despawn lo corte
    private int                  m_AtStopTicks;         // ticks en estado AtStop (safety: forzar latch si el iman no clava en ~60s)
    private float                m_PrevSteering;       // Capa 1 rate limiter: ultimo steering aplicado, para clamp del delta por tick
    // AutoRecovery state (prefijo m_AR_ para evitar colision con m_StuckTimer pre-existente)
    private int                  m_AR_Count;             // cantidad de teleports en esta sesion del bus
    private float                m_AR_LastTime;          // tickTime del ultimo teleport (cooldown)
    private int                  m_AR_LastWpIdx;         // ultimo wp_idx visto, para detectar no-progress
    private float                m_AR_LastWpProgressTime; // tickTime cuando wp_idx avanzo por ultima vez
    private string               m_PrevTickMode = "normal"; // mode del wp anterior, para detectar transicion a maniobra/parking/reverse y aplicar ModeEntrySnap
    private int                  m_ReverseEntrySteerReset = 0; // 2026-06-25: ticks restantes p/ forzar steering=0 al entrar reversa (el snap no resetea el volante)
    // "ParedÃƒÂ³n" cruise Ã¢â‚¬â€ D-term para el Stanley lateral no-lineal
    private float                m_LastCorridorOffset;   // offset previo, para D-term anti-zigzag
    private float                m_RevLatIntegral;       // I-term anti-drift reverse: integral acumulada del offset lateral
    // MODO APROXIMACION (puente Modo3 -> maniobra): estado de la rampa de velocidad.
    private bool                 m_ApproachActive;       // true mientras Boris esta dentro de un bloque mode=approach
    private float                m_ApproachEntrySpeed;   // km/h capturada al ENTRAR al bloque (velocidad real de Boris)
    private float                m_ApproachTotalDist;    // m, distancia approach->maniobra medida al entrar (denominador de la rampa)
    private float                m_ApproachExitSpeed;    // km/h objetivo al final de la rampa = velocidad GRABADA del humano en la entrada de la maniobra (fallback ApproachExitKmh)
    // AR_OnWay fase 1 (ObstacleSlow): scan throttled de vehÃƒÂ­culo adelante.
    private float                m_ObstacleScanTime;     // tickTime del ÃƒÂºltimo scan
    private float                m_ObstacleDist;         // distancia cacheada al vehÃƒÂ­culo adelante (-1 = no hay)
    private float                m_BZHitTime;            // tickTime del ÃƒÂºltimo golpe de vehÃƒÂ­culo (EEHitBy) Ã¢â‚¬â€ gatilla escape
    private float                m_ObstacleStuckSince;   // tickTime desde que Boris quedÃƒÂ³ frenado ante un obstÃƒÂ¡culo (timeout escape)
    private float                m_ObstacleSeenTime;     // tickTime de la ÃƒÂºltima vez que el scan vio un vehÃƒÂ­culo (tolera flicker del scan)
    private float                m_ObstaclePushSince;    // tickTime desde que Boris quedÃƒÂ³ empujando/trabado (target alto pero sin moverse ni progresar)
    private int                  m_ObstaclePushWpIdx;    // wp_idx al empezar el empuje (para ver si progresa de verdad)
    private int                  m_SpawnAttempt;     // contador de intentos para auto-retry del spawn
    private vector               m_SpawnInitialPos;  // pos donde spawneÃƒÂ³ el bus, para chequear si se moviÃƒÂ³
    private ref array<BZBusStopSign> m_Signs = new array<BZBusStopSign>();

    // AI logging Ã¢â‚¬â€ graba la trayectoria del bus mientras la maneja la IA, a CSV.
    // Se usa para comparar contra la grabacion humana y encontrar zonas de
    // divergencia. Hotkey NUMPAD 7 (cliente) -> RPC -> ToggleAILogging.
    private bool   m_AILoggerActive;
    // CHECKS DE LOGGER DEL REPRODUCTOR (2026-08-09): opt-in por corrida, armados con el play (no auto).
    // boris_native lo escribe BZServerProbe SOLO si m_LogBorisNative; ai_run lo arranca el tick si m_LogAiRun.
    private bool   m_LogBorisNative;   // check "Log boris_native" del reproductor
    private bool   m_LogAiRun;         // check "Log ai_run" del reproductor
    private string m_AILogPath;
    private float  m_AILogStartTime;
    private int    m_AILogSampleCount;
    private int    m_AILogNextMarker;  // se setea a 1 con NUMPAD - desde el cliente; LogAITick lo escribe y resetea

    // Snapshot del ultimo ciclo de DriveTowards: valores interpolados (i*) que
    // el controller efectivamente uso como referencia interna. Los cacheamos
    // aca para que LogAITick pueda escribirlos al CSV sin recalcularlos.
    // Sirven para diagnosticar precision: comparar i_throttle vs target_throttle
    // (del recording) muestra cuanto suaviza la interpolacion temporal; comparar
    // i_throttle vs el throttle final cached (m_CachedThrottle) muestra el
    // override de MODO PARKING / SAFETY OVERRIDE / KICK encima.
    private float  m_LastIThrottle;
    private float  m_LastIBrake;
    private float  m_LastISpeed;

    // System Identification Ã¢â‚¬â€ experimentos para caracterizar la funcion de
    // transferencia interna de eAI (que hace internamente con nuestros inputs).
    // Cuando estos modos estan activos, el Tick() IGNORA la ruta y aplica
    // inputs programados para revelar el patron de respuesta del sistema.
    //   m_SysIDMode: 0=off, 1=step response, 2=curva de respuesta
    private int    m_SysIDMode;
    private float  m_SysIDStartTime;
    private string m_SysIDLogPath;
    private int    m_SysIDSampleCount;

    // PAUSE mode Ã¢â‚¬â€ congela el bus en su posicion actual con brake aplicado.
    // Util para setup de experimentos: NUMPAD 2 (spawn) -> NUMPAD . (pausa)
    // -> COT teleport + alinear -> NUMPAD 8/9 (experimento) -> queda pausado al
    // terminar. NUMPAD 2 vuelve a modo ruta normal.
    private bool   m_Paused;
    private int    m_HailResumeTick = 0; // parada a demanda (gesto OK): tick en que Boris reanuda; 0 = sin hail activo
    private bool   m_Frozen;   // verbo freeze_vehicle: clava el vehiculo (handbrake), sin avance de ruta
    private bool   m_VehicleInvincible = true;   // estado: irrompible (default) vs destructible. Verbo set_vehicle_mortality lo flipea.
    private bool   m_DriverInvincible = true;    // Boris mortal o no. Verbo set_driver_mortality lo flipea.
    private float  m_MissionStartTime;           // GetTickTime al spawn Ã¢â‚¬â€ para triggers tipo timer
    private float  m_LastAdvTickTime;            // GetTickTime del ultimo tick de avance de wp Ã¢â‚¬â€ para medir dt REAL (fix freeze cruise alta velocidad)
    private bool   m_RouteStopped;               // verbo stop_route: frena el avance (reanudable con resume_route)
    private ref map<string, string> m_ScenarioVars;   // variables de escenario (set_var / condicion var_equals)

    // Pre-roll: hasta este tickTime, la IA no maneja (brake aplicado, motor
    // encendiendo, driver acomodandose). Se setea en SpawnBus a now+3s.
    // (Legacy: ahora gestionado por m_CurrentInput + SpawnHoldSeconds, queda
    // como respaldo para compatibilidad con el codigo existente.)
    private float  m_PreRollEndTime;

    // ---- INPUT (vocabulario de comandos al bus, ver BZBusCommon.c) ----
    // m_CurrentInput refleja que comando esta ejecutando el bus en este momento.
    // Cada cambio se logea y broadcastea via SetInput().
    private int    m_CurrentInput = BZBusInput.NONE;
    private float  m_InputEnterTime;     // tickTime cuando entramos al input actual

    // Corredor (rieles): resultado cacheado del calculo en DriveTowards. Se
    // expone via getters para que LogAITick pueda escribir si el bus iba
    // "in_corridor" o "out_corridor" al CSV.
    private float  m_CorridorLateralOffset; // firmado, + = bus a la derecha del segmento
    private float  m_CorridorSegmentHeading; // rad, heading del segmento del recording
    private bool   m_CorridorValid;          // false si no se encontro segmento valido
    private ref array<float> m_PathPitch;    // pitch[wpIdx] en rad, pre-computed al cargar JSON

    // -------------------------------------------------------------------------

    static BZBusService GetInstance() {
        if (!s_Instance) {
            s_Instance = new BZBusService();
            if (!s_Runners) s_Runners = new array<ref BZBusService>();
            s_Runners.Insert(s_Instance);   // el primary tambien es un runner del registro
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_Instance.PerfTick, 0, true);   // perf logger per-frame
        }
        return s_Instance;
    }

    // MULTITON: registro de runners activos. El panel itera esto; Fase 1 = solo el primary.
    static array<ref BZBusService> GetRunners() {
        if (!s_Runners) s_Runners = new array<ref BZBusService>();
        return s_Runners;
    }

    BZBusRouteConfig GetConfig() {
        return m_Config;
    }

    // ---- INPUT helpers ----
    int   GetCurrentInput() { return m_CurrentInput; }
    float GetTimeInCurrentInput() {
        return GetGame().GetTickTime() - m_InputEnterTime;
    }

    // Cambia el input activo del bus. Logea la transicion en RPT y broadcast
    // global para visibilidad del admin durante experimentos. Incluye:
    //   - input anterior + duracion en ese estado
    //   - input nuevo
    //   - razon textual (debug)
    void SetInput(int newInput, string reason) {
        int prevInput = m_CurrentInput;
        if (prevInput == newInput) return;  // no-op si ya estamos en ese input
        float duration = GetTimeInCurrentInput();
        m_CurrentInput = newInput;
        m_InputEnterTime = GetGame().GetTickTime();
        string msg = "[INPUT] " + BZBusInputName.Of(prevInput);
        msg += " (" + duration.ToString() + "s)";
        msg += " -> " + BZBusInputName.Of(newInput);
        if (reason != "") msg += " | " + reason;
        BZBusLog.Info(msg);
    }

    float GetDistanceToNextStop() {
        if (!m_Bus || !m_Config) return -1.0;
        if (m_NextStopIndex < 0 || m_NextStopIndex >= m_Config.Waypoints.Count()) return -1.0;
        BZWaypoint targetWp = m_Config.Waypoints[m_NextStopIndex];
        vector busPos = m_Bus.GetPosition();
        vector wpPos = targetWp.GetVector();
        wpPos[1] = busPos[1];  // alineamos Y para que distancia sea solo 2D horizontal
        return vector.Distance(busPos, wpPos);
    }

    int GetPassengerCount() {
        if (!m_Bus) return 0;
        Car carBus = Car.Cast(m_Bus);
        if (!carBus) return 0;
        int count = 0;
        for (int i = 0; i < carBus.CrewSize(); i++) {
            Human crew = carBus.CrewMember(i);
            if (crew && crew != m_Driver) {
                count++;
            }
        }
        return count;
    }

    // Llamado desde modded CarScript.OnInput cada frame para saber si este
    // vehiculo es nuestro bus
    bool IsBusActive(EntityAI candidate) {
        return m_Bus && candidate == m_Bus;
    }

    // MULTITON: encuentra el runner DUEÃƒâ€˜O de este vehiculo (el que lo spawneo), iterando el registro.
    // El CarScript lo usa para aplicar el control del runner CORRECTO (no del primary singleton).
    // Asi cada vehiculo lo maneja su propio runner -> varios buses andan en paralelo.
    static BZBusService GetRunnerForCar(EntityAI car) {
        if (!car || !s_Runners) return null;
        for (int i = 0; i < s_Runners.Count(); i++) {
            BZBusService r = s_Runners.Get(i);
            if (r && r.IsBusActive(car)) return r;
        }
        return null;
    }

    // ---- PERF LOGGER: mide el FPS del server (per-frame) y lo loguea al RPT cada 5s + conteo de runners.
    // Para medir el TECHO en vivo: sumas buses, miras como cae el FPS. s_ServerFps queda disponible para el overlay.
    static float s_ServerFps = 30;
    // PERF (ms/frame del framework): cada Tick() de cada runner suma su delta aca durante el frame.
    // PerfTick (per-frame, primary) lo vacia cada frame, promedia sobre la ventana (~1s) y deja el
    // resultado suavizado en s_FrameworkMs. La telemetria empuja s_ServerFps + s_FrameworkMs a la UI.
    static float s_PerfFrameAccumMs = 0;   // suma de los Tick de TODOS los runners en el frame en curso
    static float s_FrameworkMs = 0;        // ms/frame del framework, suavizado (lo lee la telemetria)
    private float m_PerfLast = 0;
    private float m_PerfLogTimer = 0;
    private float m_PerfWinTimer = 0;      // acumulador de la ventana de promedio (~1s)
    private float m_PerfWinSumMs = 0;      // suma de ms-de-framework por-frame dentro de la ventana
    private int   m_PerfWinFrames = 0;     // cantidad de frames en la ventana
    void PerfTick() {
        if (!GetGame().IsServer()) return;
        float now = GetGame().GetTickTime();
        // Tomar y RESETEAR el acumulador del frame que acaba de pasar (lo llenaron los Tick de los runners).
        float frameMs = s_PerfFrameAccumMs;
        s_PerfFrameAccumMs = 0;
        if (m_PerfLast == 0) { m_PerfLast = now; return; }   // 1era muestra: solo baseline
        float dt = now - m_PerfLast;
        m_PerfLast = now;
        if (dt <= 0.0001) return;
        s_ServerFps = s_ServerFps * 0.92 + (1.0 / dt) * 0.08;   // FPS suavizado
        // Promedio del ms/frame del framework sobre una ventana de ~1s (no titila).
        m_PerfWinSumMs += frameMs;
        m_PerfWinFrames++;
        m_PerfWinTimer += dt;
        if (m_PerfWinTimer >= 1.0) {
            if (m_PerfWinFrames > 0) s_FrameworkMs = m_PerfWinSumMs / m_PerfWinFrames;
            m_PerfWinTimer  = 0;
            m_PerfWinSumMs  = 0;
            m_PerfWinFrames = 0;
        }
        m_PerfLogTimer += dt;
        if (m_PerfLogTimer >= 5.0) {
            m_PerfLogTimer = 0;
            int active = 0;
            if (s_Runners) {
                for (int i = 0; i < s_Runners.Count(); i++) {
                    BZBusService r = s_Runners.Get(i);
                    if (r && r.RunnerIsActive()) active++;
                }
            }
            BZBusLog.Info("[PERF] server " + (int)s_ServerFps + " fps | BZ " + s_FrameworkMs + " ms/frame | runners activos: " + active);
        }
    }

    // Cached input que se reinyecta cada frame desde OnInput modded
    private int   m_ApplyCount;   // diagnostico: cuantas veces ApplyBusInput escribio en el auto
    private float m_CachedThrottle;
    private float m_CachedSteering;
    private float m_CachedBrake;
    private float m_CachedHandbrake;   // 2026-06-08: anti-rollback en pendientes (parking/maniobra)
    private float m_CachedRpmClutch;   // cached al SpawnBus desde config del vehiculo
    // TRACER DEL CONTROLADOR (2026-07-21, idea de Sonom4n: "que Boris diga en log que lineas esta tocando").
    // Cada seccion del pipeline llama CtlSnap(tag, thr, brk, steer). Comparando snapshots consecutivos
    // se ve QUE SECCION movio el control (y cuales NUNCA se ejecutan -> borrables). Se loguea solo cerca
    // del intercambio a baja velocidad (donde esta el problema), para no llenar el RPT. Gate por flag.
    private string m_CtlTrace;
    private int    m_PpSatCount;   // throttle del sensor [PP-AIM]: loguea 1 de cada 8 saturaciones
    private float  m_CtlPrevThr;
    private float  m_CtlPrevBrk;
    private float  m_CtlPrevSteer;
    // ARRANQUE DE TRAMO (2026-07-21): true desde que se abre un tramo hasta que el auto realmente rueda.
    // Mientras esta prendido, la rueda va DERECHA (ver LaunchStraightEnabled en BZBusConfig).
    private bool  m_LegLaunch;
    private int   m_LaunchDbg = 99;   // diagnostico one-shot: cuenta ticks logueados por tramo (reset en SetLegFrom)
    // true si la ruta trae al menos un wp con legBreak (INTERCAMBIO declarado por el autor). Si lo trae,
    // los tramos salen de las marcas y no de los cambios forward<->reverse. Se calcula al cargar la ruta.
    private bool  m_HasLegBreaks;
    // LOOP DE DIRECCION RAPIDO (2026-07-10): el pure-pursuit es GEOMETRICO (no dt-sensible, no gain que
    // sobre-corrija como Stanley a 150ms) -> se recomputa a 50ms (20Hz) en un loop aparte -> volante ~10x
    // mas fino (correccion cada ~0.4m vs 4m). El Tick de 500ms sigue con velocidad/gear/learners (dt-sensible).
    private bool  m_FastSteerActive;   // el Tick lo prende solo en modo pure-pursuit forward normal
    private float m_MicroBaseSteer;    // 2026-07-12: steering base del tick lento (500ms); el microtick de centrado pulsa SOBRE esto sin acumular
    private int   m_MicroPhase;        // 2026-07-12: contador del fast tick para el duty-cycle on/off de los microticks de centrado
    private float m_YawEMA;             // 2026-07-07: yaw-rate real suavizado (EMA) para el feedback de yaw
    private bool  m_SpawnHoldActive;   // 2026-06-09: handbrake on al spawn, suelta cuando Boris arranca
    private float m_SpawnHoldTime;     // tickTime cuando arranco el spawn hold
    private bool  m_EndHoldActive;     // 2026-06-27: handbrake on al fin de ruta, sostiene N seg antes de despawn/respawn (espejo del spawn-hold)
    private float m_EndHoldTime;       // tickTime cuando arranco el end hold
    private bool  m_LightsOn;          // 2026-06-27: estado de luces del vehiculo (verbos lights_on/lights_off)
    private int   m_LastHornState;     // 2026-06-27: ultimo estado de bocina aplicado (replay espacial), solo re-aplica al cambiar
    private float m_BattTopUpAccum;    // 2026-06-27: acumulador de dt para top-up periodico de bateria (faros drenan CompEM), dispara cada BATT_TOPUP_S
    private float m_LightsHeartbeatAccum; // 2026-06-27: acumulador de dt para heartbeat de SetSynchDirty (re-prende luces en observadores), dispara cada LIGHTS_HEARTBEAT_S

    // Gear deseado por la AT - eAI mete ShiftTo(FIRST) cada frame en su OnInput,
    // nosotros lo sobreescribimos despues de super.OnInput con este valor.
    private int m_DesiredGear = 2; // 2 = CarGear.FIRST (arranque)

    void SetCachedInput(float throttle, float steering, float brake) {
        m_CachedThrottle = throttle;
        m_CachedSteering = steering;
        m_CachedBrake    = brake;
        m_PrevSteering   = steering;   // rate-limiter (2026-07-11): trackea el ultimo volante aplicado
        m_MicroBaseSteer = steering;   // 2026-07-12: base sobre la que pulsa el microtick de centrado (no acumula)
    }

    void SetCachedHandbrake(float h) { m_CachedHandbrake = h; }
    float GetCachedHandbrake() { return m_CachedHandbrake; }
    float GetCachedRpmClutch() { return m_CachedRpmClutch; }

    float GetCachedSteering() { return m_CachedSteering; }

    int  GetDesiredGear() { return m_DesiredGear; }
    void SetDesiredGear(int g) { m_DesiredGear = g; }

    // Max gear configurable por vehiculo desde el JSON. Default 6 (5ta).
    int GetMaxGear() {
        if (m_Config && m_Config.MaxGear > 0) return m_Config.MaxGear;
        return 6;
    }

    // Umbral de aceleracion (km/h por segundo) para shift up anti-catapulta.
    // Por encima de este valor, la AT shiftea UP para suavizar la aceleracion.
    // Default 999 (deshabilitado).
    float GetAccelShiftThreshold() {
        if (m_Config && m_Config.AccelShiftThreshold > 0) return m_Config.AccelShiftThreshold;
        return 999.0;
    }

    // Escala del steering final del Stanley. Compensa wheelbase corto que
    // produce sobre-rotacion para el mismo input nominal. Default 1.0.
    float GetSteeringScale() {
        if (m_Config && m_Config.SteeringScale > 0) return m_Config.SteeringScale;
        return 1.0;
    }

    float GetParkingStanleyK() {
        if (m_Config && m_Config.ParkingStanleyK > 0) return m_Config.ParkingStanleyK;
        return STANLEY_K_PARKING;
    }

    // EXIT-TIGHTEN: sube K lateral proporcional al offset lateral que quede fuera de la deadzone, faded por
    // velocidad. Ataca la apertura de SALIDA de curva (StanleyCurvatureAware relaja K cuando cae la curvatura).
    // En recta el offset ~0 -> boost ~0 -> cruise intacto. Solo forward/cruise normal. Ver camino (A).
    float ApplyExitTightenK(float kStanley, float adjustedOffset, float kmh, bool isReversePk, bool isNormalCruise) {
        if (!m_Config || !m_Config.ExitTightenEnabled) return kStanley;
        if (isReversePk || !isNormalCruise) return kStanley;
        float absOff = Math.AbsFloat(adjustedOffset);
        if (absOff <= m_Config.ExitTightenDeadM) return kStanley;
        if (kmh >= m_Config.ExitTightenHighKmh) return kStanley;
        float vFade = 1.0;
        if (kmh > m_Config.ExitTightenLowKmh) vFade = 1.0 - (kmh - m_Config.ExitTightenLowKmh) / (m_Config.ExitTightenHighKmh - m_Config.ExitTightenLowKmh);
        float kBoost = (absOff - m_Config.ExitTightenDeadM) * m_Config.ExitTightenGain * vFade;
        if (kBoost > m_Config.ExitTightenMaxK) kBoost = m_Config.ExitTightenMaxK;
        return kStanley + kBoost;
    }

    float GetParkingFFWeight() {
        if (m_Config && m_Config.ParkingFFWeight > 0) return m_Config.ParkingFFWeight;
        return 0.6;
    }

    float GetReverseStanleyK() {
        if (m_Config && m_Config.ReverseStanleyK > 0) return m_Config.ReverseStanleyK;
        return STANLEY_K_REVERSE;
    }

    float GetReverseStanleyMinSpeed() {
        if (m_Config && m_Config.ReverseStanleyMinSpeed > 0) return m_Config.ReverseStanleyMinSpeed;
        return 2.0; // m/s Ã¢â‚¬â€ piso de velocidad para Stanley en reverse (rompe la espiral 1/v)
    }

    // === Modelo bicicleta en reverse (rear-steer archetype) ===
    float GetWheelbase() {
        if (m_Config && m_Config.Wheelbase > 0) return m_Config.Wheelbase;
        return REVERSE_WHEELBASE_DEFAULT;
    }
    // Offset del punto de control al EJE TRASERO en reverse = wheelbase/2 (per-vehiculo).
    // El 1.4 hardcoded era el medio-wheelbase del Golf -> en vehiculos largos (V3S 4.61m)
    // la referencia quedaba 0.9m adelante del eje trasero real (del lado equivocado del
    // pivote) -> offset con signo invertido -> Stanley corregia AL REVES (Sonom4n 2026-06-13).
    float GetReverseControlOffset() {
        float wbHalf = GetWheelbase() * 0.5;
        if (wbHalf > 0.3) return wbHalf;
        return REVERSE_CONTROL_OFFSET;
    }
    float GetReverseFFSign() {
        if (m_Config && m_Config.ReverseFFSign != 0) return m_Config.ReverseFFSign;
        return REVERSE_FF_SIGN_DEFAULT;
    }
    float GetReverseFFMaxSteer() {
        if (m_Config && m_Config.ReverseFFMaxSteerRad > 0) return m_Config.ReverseFFMaxSteerRad;
        return REVERSE_FF_MAXSTEER_DEFAULT;
    }
    float GetReverseFFWeight() {
        if (m_Config && m_Config.ReverseFFWeight > 0) return m_Config.ReverseFFWeight;
        return GetParkingFFWeight();
    }
    float GetReverseSteerGateOffset() {
        if (m_Config && m_Config.ReverseSteerGateOffset > 0) return m_Config.ReverseSteerGateOffset;
        return REVERSE_STEER_GATE_DEFAULT;
    }
    float GetReverseSteerThrottleFloor() {
        if (m_Config && m_Config.ReverseSteerThrottleFloor > 0) return m_Config.ReverseSteerThrottleFloor;
        return REVERSE_STEER_THR_FLOOR_DEFAULT;
    }
    float GetReverseSteerMax() {
        if (m_Config && m_Config.ReverseSteerMax > 0) return m_Config.ReverseSteerMax;
        return REVERSE_STEER_MAX_DEFAULT;
    }

    // I-term anti-drift de reverse: ganancia del integral. 0 = apagado (opt-in, tuneable por _hdr).
    float GetReverseLateralKi() {
        if (m_Config && m_Config.ReverseLateralKi > 0) return m_Config.ReverseLateralKi;
        return 0;
    }
    float GetReverseTargetSpeedCap() {
        if (m_Config && m_Config.ReverseTargetSpeedCap > 0) return m_Config.ReverseTargetSpeedCap;
        return REVERSE_TARGET_SPEED_CAP_DEFAULT;
    }
    float GetReverseStanleyFineMax() {
        if (m_Config && m_Config.ReverseStanleyFineMax > 0) return m_Config.ReverseStanleyFineMax;
        return REVERSE_STANLEY_FINE_MAX_DEFAULT;
    }
    float GetReverseHeadingDeadbandDeg() {
        if (m_Config && m_Config.ReverseHeadingDeadbandDeg > 0) return m_Config.ReverseHeadingDeadbandDeg;
        return REVERSE_HEAD_DEADBAND_DEG_DEFAULT;
    }
    float GetReverseRecordedSteerThreshold() {
        if (m_Config && m_Config.ReverseRecordedSteerThreshold > 0) return m_Config.ReverseRecordedSteerThreshold;
        return REVERSE_REC_STEER_THRESH_DEFAULT;
    }
    // Lookahead del pure-pursuit de reverse (m). Default 5.0 = largo, para SUAVIZAR el rear-steer sensible.
    float GetReversePPLookahead() {
        if (m_Config && m_Config.ReversePPLookaheadM > 0) return m_Config.ReversePPLookaheadM;
        return 5.0;
    }
    // Ld corto para el codo (curva cerrada) del pure-pursuit de reverse. Default 2.0 m.
    float GetReversePPLdCurve() {
        if (m_Config && m_Config.ReversePPLdCurveM > 0) return m_Config.ReversePPLdCurveM;
        return 2.0;
    }

    // === MODO APROXIMACION (puente SOLO Modo 3 -> maniobra) ===
    // Velocidad objetivo (km/h) al final del bloque approach = entrada limpia a maniobra.
    float GetApproachExitKmh() {
        if (m_Config && m_Config.ApproachExitKmh > 0) return m_Config.ApproachExitKmh;
        return 20.0;
    }

    float GetApproachAutoDecel() {
        if (m_Config && m_Config.ApproachAutoDecel > 0) return m_Config.ApproachAutoDecel;
        return 2.5;
    }

    // === AR_OnWay fase 1 (ObstacleSlow) ===
    float GetObstacleScanDist() {
        if (m_Config && m_Config.ObstacleScanDist > 0) return m_Config.ObstacleScanDist;
        return 50.0;
    }
    float GetObstacleStopDist() {
        if (m_Config && m_Config.ObstacleStopDist > 0) return m_Config.ObstacleStopDist;
        return 15.0;
    }

    // Escanea el path adelante (cada ~4m, hasta ObstacleScanDist) buscando un VEHICULO (CarScript)
    // distinto de Boris en el corredor. Devuelve la distancia de path al primero, o -1 si no hay.
    // Usa GetObjectsAtPosition3D (misma API que BZRouteCleanup). Empieza a 5m (no detectarse a si mismo).
    float ScanObstacleAhead(vector busPos, Object selfBus, float currentKmh) {
        if (!m_Config || !m_Config.Waypoints) return -1;
        int n = m_Config.Waypoints.Count();
        // Lookahead VARIABLE con la velocidad: hay que ver el obstaculo a MAS de la distancia de
        // frenado a esta velocidad, o Boris no llega a parar. scanMax = stopDist + v^2/(2a) +
        // margen de reaccion (scan throttled + ticks) + colchon. ObstacleScanDist = PISO (minimo a
        // baja velocidad). Cap a 130m por costo de queries (engancha recien sobre ~105 km/h).
        float vMs = currentKmh / 3.6;
        float a = GetEffObstacleDecel();   // decel REAL de este vehiculo (frenos+masa+agarre del config)
        float adaptiveMax = GetObstacleStopDist() + (vMs * vMs) / (2.0 * a) + vMs * 0.6 + 5.0;
        float scanMax = Math.Max(GetObstacleScanDist(), adaptiveMax);
        if (scanMax > 130.0) scanMax = 130.0;

        // FRENTE REAL de Boris: query directo adelante de su morro (segun su HEADING actual, NO la
        // linea grabada). Capta un vehiculo pegado/empujado que se corrio de los waypoints -> sin
        // esto, al acercarse el scan lo pierde ("despejado") y Boris lo FUERZA (bug con vehiculos
        // fuertes que empujan). Radio chico = solo lo que tiene realmente en frente (banquina afuera).
        CarScript busCar = CarScript.Cast(selfBus);
        if (busCar) {
            vector busFwd = busCar.GetDirection();
            busFwd[1] = 0;
            float bfLen = busFwd.Length();
            if (bfLen > 0.001) {
                busFwd = busFwd * (1.0 / bfLen);
                for (float fd = 3.0; fd <= 9.0; fd = fd + 3.0) {
                    vector fpt = busPos + busFwd * fd;
                    array<Object>    fobjs = new array<Object>;
                    array<CargoBase> fprox = new array<CargoBase>;
                    GetGame().GetObjectsAtPosition3D(fpt, 3.0, fobjs, fprox);
                    foreach (Object fo : fobjs) {
                        if (!fo || fo == selfBus) continue;
                        if (CarScript.Cast(fo)) return fd;
                    }
                }
            }
        }

        float cumDist = 0;
        float nextQuery = 3.0;   // arrancar a 3m (selfBus excluido) -> detecta un vehiculo pegado adelante
        vector prev = busPos;
        for (int i = m_WaypointIndex; i < n; i++) {
            vector wp = m_Config.Waypoints[i].GetVector();
            cumDist += vector.Distance(prev, wp);
            prev = wp;
            if (cumDist > scanMax) break;
            if (cumDist >= nextQuery) {
                nextQuery = nextQuery + 4.0;
                // Direccion del path aca -> mide el OFFSET LATERAL del obstaculo respecto al eje del
                // carril. Un vehiculo corrido a la banquina (offset > corredor) NO bloquea: Boris pasa.
                // Solo cuenta si INVADE el carril. (No mira solo "hay un auto cerca" sino "me tapa el paso".)
                vector segFwd;
                if (i + 1 < n) segFwd = m_Config.Waypoints[i + 1].GetVector() - wp;
                else if (i > 0) segFwd = wp - m_Config.Waypoints[i - 1].GetVector();
                else segFwd = "0 0 1";
                segFwd[1] = 0;
                float segLen = segFwd.Length();
                if (segLen > 0.001) segFwd = segFwd * (1.0 / segLen); else segFwd = "0 0 1";
                vector rightv = Vector(segFwd[2], 0, -segFwd[0]);   // perpendicular XZ = costado del carril
                float corridorHalf = GetObstacleCorridorHalf();
                array<Object>    objs = new array<Object>;
                array<CargoBase> prox = new array<CargoBase>;
                GetGame().GetObjectsAtPosition3D(wp, 4.5, objs, prox);   // radio para CAPTAR candidatos; el corredor filtra
                foreach (Object o : objs) {
                    if (!o) continue;
                    if (o == selfBus) continue;
                    if (!CarScript.Cast(o)) continue;
                    vector rel = o.GetPosition() - wp;
                    rel[1] = 0;
                    float lateral = rel[0] * rightv[0] + rel[2] * rightv[2];
                    if (lateral < 0) lateral = -lateral;
                    if (lateral < corridorHalf) return cumDist;   // invade el carril -> bloquea de verdad
                }
            }
        }
        return -1;
    }

    // Scan THROTTLED (~3/s): cachea la distancia para no consultar entidades cada tick.
    float GetObstacleDistThrottled(vector busPos, Object selfBus, float currentKmh) {
        float now = GetGame().GetTickTime();
        float interval = 0.3;                   // scan adaptativo: mas rapido a alta velocidad
        if (currentKmh > 30.0) interval = 0.1;  //   (recupera margen ante un obstaculo que entra rapido y tarde)
        if (now - m_ObstacleScanTime > interval) {
            m_ObstacleScanTime = now;
            float prevD = m_ObstacleDist;
            m_ObstacleDist = ScanObstacleAhead(busPos, selfBus, currentKmh);
            if (prevD < 0 && m_ObstacleDist > 0) BZBusLog.Info("[AR_OnWay] vehiculo adelante a " + m_ObstacleDist + "m -> frenando");
            if (prevD > 0 && m_ObstacleDist < 0) BZBusLog.Info("[AR_OnWay] camino despejado -> retomando");
        }
        return m_ObstacleDist;
    }

    float GetObstacleDecel() {
        if (m_Config && m_Config.ObstacleDecel > 0) return m_Config.ObstacleDecel;
        return 4.5;
    }

    // Decel EFECTIVO para freno/lookahead anti-obstaculo: el target de config (ObstacleDecel)
    // PERO acotado por lo que el vehiculo FISICAMENTE puede frenar (el InverseModel ya leyo su
    // brakeTorque + masa + agarre del config). Un vehiculo que no llega al ObstacleDecel frena
    // a SU maximo real y, como el decel es menor, el lookahead lo hace mirar de mas lejos. Config-as-manual.
    float GetEffObstacleDecel() {
        float a = GetObstacleDecel();
        if (m_InverseModel) {
            float vehMax = m_InverseModel.GetMaxBrakeDecel("");
            if (vehMax > 0 && vehMax < a) a = vehMax;
        }
        if (a < 1.0) a = 1.0;   // piso de sanidad
        return a;
    }

    // Medio ancho del carril que cuenta como BLOQUEO. Un obstÃƒÂ¡culo con offset lateral mayor
    // (corrido a la banquina) NO bloquea Ã¢â€ â€™ Boris pasa. Config-tunable.
    float GetObstacleCorridorHalf() {
        if (m_Config && m_Config.ObstacleCorridorHalf > 0) return m_Config.ObstacleCorridorHalf;
        return 2.3;
    }

    float GetObstacleEscapeResumeKmh() {
        if (m_Config && m_Config.ObstacleEscapeResumeKmh > 0) return m_Config.ObstacleEscapeResumeKmh;
        return 10.0;
    }

    // Velocidad objetivo del freno predictivo para DETENERSE a ObstacleStopDist del obstaculo.
    // v_req = sqrt(2Ã‚Â·aÃ‚Â·distancia_de_frenado), a = ObstacleDecel (firme). Pasado el punto -> 0 (esperar).
    // Si Boris viene muy rapido para esta distancia, el target queda << su velocidad y el InverseModel
    // frena al MAXIMO (escala a emergencia solo). La fisica manda: si no le da, lo toca -> escape por golpe.
    float ComputeObstacleSlowSpeed(float distToObstacle) {
        float brakeDist = distToObstacle - GetObstacleStopDist();
        if (brakeDist <= 0.5) return 0.0;   // dentro del margen -> detenerse y esperar
        float a = GetEffObstacleDecel();    // mismo decel real que usa el lookahead -> coherente
        float vReqMs = Math.Sqrt(2.0 * a * brakeDist);
        return vReqMs * 3.6;
    }

    // === AR_OnWay fase 2 (ObstacleEscape) ===
    float GetObstacleEscapeWaitS() {
        if (m_Config && m_Config.ObstacleEscapeWaitS > 0) return m_Config.ObstacleEscapeWaitS;
        return 6.0;
    }

    // Llamado por BZBusCarScript.EEHitBy cuando OTRO VEHICULO choca a Boris -> sella el momento.
    void NotifyHitByVehicle() {
        m_BZHitTime = GetGame().GetTickTime();
    }

    // Primer wp adelante (pasado el obstaculo si hay) cuyo entorno este LIMPIO de vehiculos.
    // Empieza ~8m pasado el obstaculo y consulta cada ~4m. -1 si no encuentra.
    int FindClearWpAhead(vector busPos, Object selfBus) {
        if (!m_Config || !m_Config.Waypoints) return -1;
        int n = m_Config.Waypoints.Count();
        float startBeyond = 8.0;
        if (m_ObstacleDist > 0) startBeyond = m_ObstacleDist + 8.0;
        float cumDist = 0;
        float nextCheck = startBeyond;
        vector prev = busPos;
        for (int i = m_WaypointIndex; i < n; i++) {
            vector wp = m_Config.Waypoints[i].GetVector();
            cumDist += vector.Distance(prev, wp);
            prev = wp;
            if (cumDist < nextCheck) continue;
            nextCheck = nextCheck + 4.0;
            array<Object>    objs = new array<Object>;
            array<CargoBase> prox = new array<CargoBase>;
            GetGame().GetObjectsAtPosition3D(wp, 3.5, objs, prox);
            bool clear = true;
            foreach (Object o : objs) {
                if (!o) continue;
                if (o == selfBus) continue;
                if (CarScript.Cast(o)) { clear = false; break; }
            }
            if (clear) return i;
        }
        return -1;
    }

    // APPROACH AUTOMATICA (Modo 3, sin zona marcada): escanea adelante el proximo bloque maniobra O parking
    // dentro de un horizonte. Si Boris viene mas rapido que la velocidad GRABADA de esa maniobra/parking,
    // calcula el perfil de FRENO PREDICTIVO (v_req = sqrt(v_manÃ‚Â² + 2Ã‚Â·aÃ‚Â·dist)) y devuelve la velocidad
    // a la que deberia estar AHORA para llegar suave a v_man. La distancia escala con vÃ‚Â² -> se adapta
    // al vehiculo solo. Devuelve -1 si no hay maniobra en rango o si todavia no hace falta frenar.
    float ComputeAutoApproachSpeed(float currentKmh, vector busPos) {
        if (!m_Config || !m_Config.Waypoints) return -1;
        int n = m_Config.Waypoints.Count();
        float cumDist = 0;
        vector prev = busPos;
        float vManKmh = -1;
        float distToMan = -1;
        bool isParkingApproach = false;
        for (int i = m_WaypointIndex; i < n; i++) {
            BZWaypoint w = m_Config.Waypoints[i];
            vector p = w.GetVector();
            cumDist += vector.Distance(prev, p);
            prev = p;
            if (cumDist > 150.0) break; // horizonte de busqueda
            if (w.mode == "maniobra" || w.mode == "parking") {
                vManKmh = w.recordedSpeed;            // velocidad grabada de entrada de la maniobra/parking
                isParkingApproach = (w.mode == "parking");
                if (vManKmh < 3.0) {
                    // fallback si el dato grabado no sirve: maniobra -> ApproachExitKmh (~20, entra a la curva);
                    // parking -> piso 3 (NO 20: el parking es un crawl; tirarlo a 20 seria un frenazo al pedo,
                    // el "me va a tirar a 10/20 desde el auto-approach" de Sonom4n 2026-07-02).
                    if (w.mode == "parking") vManKmh = 3.0;
                    else                     vManKmh = GetApproachExitKmh();
                }
                distToMan = cumDist;
                break;
            }
        }
        if (vManKmh < 0) return -1;                   // no hay maniobra/parking en rango
        // PARKING (Sonom4n 2026-07-03, "parking mas predictivo y agresivo"): aspirar a un CRAWL, no a la
        // velocidad grabada de entrada (~14 km/h -> Boris se pasaba la coordenada, terminaba 3.43m off
        // del handbrake del humano, y el reverse heredaba ese error). Cap a PARKING_APPROACH_CRAWL ->
        // Boris llega despacio y clava el endpoint del parking = coord justa para arrancar reversa.
        // Tunable; primer pase 5.0, iterar. Ver [[project_direct_replay_arrive_ready_premise]].
        if (isParkingApproach) {
            float PARKING_APPROACH_CRAWL = 5.0;
            if (vManKmh > PARKING_APPROACH_CRAWL) vManKmh = PARKING_APPROACH_CRAWL;
        }
        if (currentKmh <= vManKmh + 1.0) return -1;   // ya viene lento, no frenar
        // Perfil de freno predictivo: velocidad requerida AHORA para llegar a v_man con decel comoda.
        float vm = vManKmh / 3.6;
        float aBrake = GetApproachAutoDecel();
        // PARKING: decel asumido BAJO (Sonom4n 2026-07-03, "no se activo el parking autoaproach"): con 2.5
        // la vReq quedaba alta lejos -> el approach enganchaba a ~12m con Boris ya flooreado a 35 km/h
        // (target geometrico de la recta 60-75) -> llegaba a 23 igual. Con 1.0 la vReq cae antes ->
        // engancha ~40-50m antes -> Boris no floorea la recta y baja al crawl a tiempo. Tunable; iterar.
        if (isParkingApproach) aBrake = 1.0;
        float vReqMs = Math.Sqrt(vm*vm + 2.0 * aBrake * distToMan);
        float vReqKmh = vReqMs * 3.6;
        if (currentKmh <= vReqKmh + 1.0) return -1;   // todavia lejos -> aun no hace falta frenar
        return vReqKmh;                               // objetivo del freno predictivo
    }

    // Distancia de PATH (m) desde el wp fromIdx hasta el PRIMER wp cuyo mode != "approach"
    // (= la entrada de la maniobra / fin del bloque approach). Suma los segmentos, incluido
    // el que entra al primer wp no-approach. Llamada al ENTRAR (denominador) y cada tick
    // (numerador restante) -> a medida que avanza el wp_idx, el restante baja -> rampa.
    float MeasureApproachDistance(int fromIdx) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        int n = m_Config.Waypoints.Count();
        if (fromIdx < 0 || fromIdx >= n) return 0;
        float d = 0;
        vector prev = m_Config.Waypoints[fromIdx].GetVector();
        for (int i = fromIdx + 1; i < n; i++) {
            BZWaypoint w = m_Config.Waypoints[i];
            vector p = w.GetVector();
            d += vector.Distance(prev, p);
            prev = p;
            if (w.mode != "approach") break; // primer wp no-approach = entrada maniobra
        }
        return d;
    }

    // Velocidad de SALIDA de la rampa = velocidad GRABADA del humano en la ENTRADA de la maniobra
    // (primer wp no-approach adelante). El humano sabe a que velocidad meter cada curva (ej 14.5 en
    // una subida dificil, NO 20 fijo). Fallback a ApproachExitKmh si no hay dato grabado valido.
    float FindManiobraEntrySpeed(int fromIdx) {
        float fallback = GetApproachExitKmh();
        if (!m_Config || !m_Config.Waypoints) return fallback;
        int n = m_Config.Waypoints.Count();
        for (int i = fromIdx + 1; i < n; i++) {
            BZWaypoint w = m_Config.Waypoints[i];
            if (w.mode != "approach") {
                if (w.recordedSpeed >= 3.0) return w.recordedSpeed; // floor 3 km/h anti-stall
                return fallback;
            }
        }
        return fallback; // bloque approach al final sin maniobra adelante
    }

    // Velocidad objetivo EFECTIVA (km/h) mientras Boris esta en un bloque mode=approach.
    // Dos regimenes segun la velocidad REAL de Boris al ENTRAR al bloque:
    //   - entry > exitK (venia RAPIDO): NOS APODERAMOS de la velocidad -> rampa LINEAL
    //     descendente desde la velocidad de entrada hasta exitK (= velocidad GRABADA en la maniobra) sobre la distancia
    //     approach->maniobra. Ignora la targetSpeed (que en M3 viene alta en el run-up
    //     recto y lo dejaba entrar caliente). Desaceleracion PREDECIBLE, sin frenazo.
    //   - entry <= exitK (venia LENTO: ya alineando, o pendiente cuesta arriba): NO rampa.
    //     Lo dejamos correr a su velocidad NATURAL (M3) hasta que llegue/supere exitK, y
    //     ahi lo capeamos (techo en exitK = mantiene/corrige). min(natural, exitK).
    // El caller debe gatear (mode=="approach" && UseInverseModel). Mantiene estado de entrada.
    float ComputeApproachTargetSpeed(float currentKmh, float naturalTargetSpeed) {
        if (!m_ApproachActive) {
            m_ApproachActive     = true;
            m_ApproachEntrySpeed = currentKmh; // velocidad REAL al entrar (sin clamp)
            m_ApproachTotalDist  = MeasureApproachDistance(m_WaypointIndex);
            m_ApproachExitSpeed  = FindManiobraEntrySpeed(m_WaypointIndex); // = velocidad grabada en la entrada de la maniobra
            BZBusLog.Info("[Approach] ON entry=" + m_ApproachEntrySpeed + " dist=" + m_ApproachTotalDist + " exit=" + m_ApproachExitSpeed);
        }
        float exitK = m_ApproachExitSpeed;
        // Entro por DEBAJO de la velocidad de salida: techo, corre a su velocidad.
        if (m_ApproachEntrySpeed <= exitK) {
            return Math.Min(naturalTargetSpeed, exitK);
        }
        // Entro RAPIDO: rampa lineal descendente entry -> exitK.
        float remD = MeasureApproachDistance(m_WaypointIndex);
        float frac = 1.0;
        if (m_ApproachTotalDist > 0.5) frac = remD / m_ApproachTotalDist;
        frac = Math.Clamp(frac, 0.0, 1.0);
        return exitK + (m_ApproachEntrySpeed - exitK) * frac;
    }

    float GetCruiseHybridSteerThreshold() {
        if (m_Config && m_Config.CruiseHybridSteerThreshold >= 0) return m_Config.CruiseHybridSteerThreshold;
        return -1;
    }

    // Softening de Stanley (k_soft) sumado a v en el denominador. 0 = off (default, preserva).
    float GetStanleySoftening() {
        if (m_Config && m_Config.StanleySoftening > 0) return m_Config.StanleySoftening;
        return 0.0;
    }

    // === YAW-RATE FEEDBACK (2026-07-07): getters de los knobs del lazo de yaw ===
    float GetYawFeedbackGain() {
        if (m_Config) return m_Config.YawFeedbackGain;
        return 0.0;
    }
    float GetYawFeedbackCap() {
        if (m_Config && m_Config.YawFeedbackCap > 0) return m_Config.YawFeedbackCap;
        return 0.25;
    }
    float GetYawFeedbackMinKmh() {
        if (m_Config && m_Config.YawFeedbackMinKmh > 0) return m_Config.YawFeedbackMinKmh;
        return 8.0;
    }

    // === PLANT FEEDFORWARD (2026-07-04): params del plant de direccion medido por el receiver ===
    float GetPlantSteerGain() {
        if (m_Config && m_Config.PlantSteerGain > 1.0) return m_Config.PlantSteerGain;
        return 85.1;   // deg de rueda por unidad de cmd_steer (front_wheel = gain*cmd)
    }
    float GetPlantUndersteerK(float kmh) {
        // ENVELOPE: si esta ON y hay mapa aprendido, el understeer sale MEDIDO de la demo por velocidad
        // (BZVehicleEnvelope) en vez del k constante inventado. GetK devuelve -1 si no hay data -> fallback.
        if (m_Config && m_Config.PlantUseEnvelope && m_Config.VehicleClass != "") {
            float ke = BZVehicleEnvelope.Get(m_Config.VehicleClass).GetK(kmh);
            if (ke > 0.01) return ke;
        }
        if (m_Config && m_Config.PlantUndersteerK > 0.01) return m_Config.PlantUndersteerK;
        return 0.90;   // yaw_real / yaw_bicicleta
    }
    float GetPlantFeedbackScale() {
        if (m_Config && m_Config.PlantFeedbackScale > 0) return m_Config.PlantFeedbackScale;
        return 1.0;
    }
    // Lag-lead del actuador: estado para el termino derivativo (cmd += lead*d(cmd)/dt).
    private float m_LastFfRaw;
    private float m_LastFfTime;
    float GetPlantLagLead() {
        if (m_Config && m_Config.PlantLagLead > 0) return m_Config.PlantLagLead;
        return 0.0;
    }
    float GetPlantFFLookaheadTime() {
        if (m_Config && m_Config.PlantFFLookaheadTime > 0) return m_Config.PlantFFLookaheadTime;
        return 1.5;
    }
    float GetPlantFFLookaheadFloor() {
        if (m_Config && m_Config.PlantFFLookaheadFloor > 0) return m_Config.PlantFFLookaheadFloor;
        return 5.0;
    }

    float GetCruiseHybridThrottleThreshold() {
        if (m_Config && m_Config.CruiseHybridThrottleThreshold >= 0) return m_Config.CruiseHybridThrottleThreshold;
        return -1;
    }

    float GetCruiseFFWeight() {
        if (m_Config && m_Config.CruiseFFWeight >= 0) return m_Config.CruiseFFWeight;
        return 0.25;
    }

    // === Corte de throttle anticipatorio por curvatura (2026-06-10) ===
    bool GetCurveThrottleEnabled() {
        if (m_Config) return m_Config.CurveThrottleEnabled;
        return false;
    }
    float GetCurveThrottleLookaheadM() {
        if (m_Config && m_Config.CurveThrottleLookaheadM > 0) return m_Config.CurveThrottleLookaheadM;
        return 14.0;
    }
    float GetCurveThrottleStartDeg() {
        if (m_Config && m_Config.CurveThrottleStartDeg > 0) return m_Config.CurveThrottleStartDeg;
        return 35.0;
    }
    float GetCurveThrottleFullDeg() {
        if (m_Config && m_Config.CurveThrottleFullDeg > 0) return m_Config.CurveThrottleFullDeg;
        return 80.0;
    }
    float GetCurveThrottleMinScale() {
        if (m_Config && m_Config.CurveThrottleMinScale >= 0) return m_Config.CurveThrottleMinScale;
        return 0.35;
    }

    private bool m_CurveCutActive;  // para loguear la entrada a cada curva una sola vez
    private bool m_SlopeGearCapActive;  // histeresis del slope gear cap (anti gear-thrashing en pendiente)

    // Suma el cambio de heading (grados) de los waypoints que VIENEN, dentro de
    // lookaheadM metros. Remuestrea cada ~3m para que el heading no sea ruidoso.
    // = "que tan cerrada es la curva que se viene". 0 = recta. ~90 = curva 90 grados.
    // Radio de la circunferencia que pasa por 3 puntos (plano XZ). Colineal -> recta -> R enorme.
    // Lo usa follow_path (Modo 2) para la velocidad optima por curva: v = sqrt(aLat * R).
    float CircumRadius2D(vector a, vector b, vector c) {
        float abx = b[0] - a[0]; float abz = b[2] - a[2];
        float bcx = c[0] - b[0]; float bcz = c[2] - b[2];
        float acx = c[0] - a[0]; float acz = c[2] - a[2];
        float lab = Math.Sqrt(abx * abx + abz * abz);
        float lbc = Math.Sqrt(bcx * bcx + bcz * bcz);
        float lac = Math.Sqrt(acx * acx + acz * acz);
        float cross = abx * bcz - abz * bcx;     // = 2 * area (con signo)
        float area2 = Math.AbsFloat(cross);
        if (area2 < 0.001) return 100000.0;      // colineal -> recta -> R enorme
        return (lab * lbc * lac) / (2.0 * area2); // R = (a*b*c)/(4*Area) = (a*b*c)/(2*|cross|)
    }

    // Radio de curvatura sobre un span FISICO (arco en metros), no en indices. En grabaciones densas
    // (10Hz, ~0.33m/wp) el span-en-indices colapsa a <4m fisicos y el circulo de 3 puntos sobre la
    // escalera de posicion explota (kappa>1 = radio<1m). Camina +-spanM metros por arco y toma esos
    // 3 puntos -> R estable (validado offline: TV 69->7 en EXAMPLE03, 2026-07-07). El punto medio es
    // centerIdx. Devuelve R grande si degenera (recta/bordes).
    float CircumRadiusMeters(int centerIdx, float spanM) {
        if (!m_Config || !m_Config.Waypoints) return 100000.0;
        int cnt = m_Config.Waypoints.Count();
        if (centerIdx < 0 || centerIdx >= cnt) return 100000.0;
        int ia = centerIdx;
        float da = 0;
        while (ia > 0 && da < spanM) {
            da += vector.Distance(m_Config.Waypoints[ia].GetVector(), m_Config.Waypoints[ia - 1].GetVector());
            ia--;
        }
        int ib = centerIdx;
        float db = 0;
        while (ib < cnt - 1 && db < spanM) {
            db += vector.Distance(m_Config.Waypoints[ib].GetVector(), m_Config.Waypoints[ib + 1].GetVector());
            ib++;
        }
        if (ib - ia < 2) return 100000.0;
        return CircumRadius2D(m_Config.Waypoints[ia].GetVector(), m_Config.Waypoints[centerIdx].GetVector(), m_Config.Waypoints[ib].GetVector());
    }

    float ComputeUpcomingBend(int fromIdx, float lookaheadM) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        int cnt = m_Config.Waypoints.Count();
        if (fromIdx < 0 || fromIdx >= cnt) return 0;
        float nodeStep = 3.0;
        vector cur = m_Config.Waypoints[fromIdx].GetVector();
        float prevHeading = -999.0;
        float bend = 0;
        float total = 0;
        int i = fromIdx + 1;
        while (i < cnt && total < lookaheadM) {
            vector p = m_Config.Waypoints[i].GetVector();
            float dx = p[0] - cur[0];
            float dz = p[2] - cur[2];
            float seg = Math.Sqrt(dx * dx + dz * dz);
            if (seg >= nodeStep) {
                float heading = Math.Atan2(dz, dx) * 57.29578;  // rad->deg (literal: Math.PI no existe en Enforce)
                if (prevHeading > -900.0) {
                    float diff = heading - prevHeading;
                    while (diff > 180.0) diff = diff - 360.0;
                    while (diff < -180.0) diff = diff + 360.0;
                    if (diff < 0) diff = -diff;
                    bend = bend + diff;
                }
                prevHeading = heading;
                total = total + seg;
                cur = p;
            }
            i++;
        }
        return bend;
    }

    // Volcar al RPT TODAS las propiedades fisicas del vehiculo que ConfigGet expone.
    // Experimento 2026-06-02 para validar la viabilidad del wizard config-read.
    // El propio engine resuelve la herencia (si VW_T6 hereda de Car y mass esta en
    // Car, ConfigGetFloat("CfgVehicles VW_T6 mass") devuelve el valor del padre).
    void DumpVehicleConfig(string vc) {
        BZBusLog.Info("==============================================");
        BZBusLog.Info("[WIZARD-DUMP] Vehicle config para: " + vc);
        BZBusLog.Info("==============================================");

        string base = "CfgVehicles " + vc + " ";

        // --- Top level ---
        BZBusLog.Info("[WIZARD] displayName: " + GetGame().ConfigGetTextOut(base + "displayName"));
        BZBusLog.Info("[WIZARD] fuelCapacity: " + GetGame().ConfigGetFloat(base + "fuelCapacity"));
        BZBusLog.Info("[WIZARD] fuelConsumption: " + GetGame().ConfigGetFloat(base + "fuelConsumption"));

        // --- Steering ---
        string sim = base + "SimulationModule ";
        BZBusLog.Info("[WIZARD] Steering.maxSteeringAngle: " + GetGame().ConfigGetFloat(sim + "Steering maxSteeringAngle"));
        DumpFloatArray(sim + "Steering increaseSpeed", "Steering.increaseSpeed");
        DumpFloatArray(sim + "Steering decreaseSpeed", "Steering.decreaseSpeed");
        DumpFloatArray(sim + "Steering centeringSpeed", "Steering.centeringSpeed");

        // --- Throttle ---
        BZBusLog.Info("[WIZARD] Throttle.reactionTime: " + GetGame().ConfigGetFloat(sim + "Throttle reactionTime"));
        BZBusLog.Info("[WIZARD] Throttle.defaultThrust: " + GetGame().ConfigGetFloat(sim + "Throttle defaultThrust"));
        BZBusLog.Info("[WIZARD] Throttle.gentleThrust: " + GetGame().ConfigGetFloat(sim + "Throttle gentleThrust"));
        BZBusLog.Info("[WIZARD] Throttle.turboCoef: " + GetGame().ConfigGetFloat(sim + "Throttle turboCoef"));

        // --- Brake ---
        BZBusLog.Info("[WIZARD] Brake.reactionTime: " + GetGame().ConfigGetFloat(sim + "Brake reactionTime"));
        DumpFloatArray(sim + "Brake pressureBySpeed", "Brake.pressureBySpeed");

        // --- Aerodynamics ---
        BZBusLog.Info("[WIZARD] Aero.frontalArea: " + GetGame().ConfigGetFloat(sim + "Aerodynamics frontalArea"));
        BZBusLog.Info("[WIZARD] Aero.dragCoefficient: " + GetGame().ConfigGetFloat(sim + "Aerodynamics dragCoefficient"));
        BZBusLog.Info("[WIZARD] Aero.downforceCoefficient: " + GetGame().ConfigGetFloat(sim + "Aerodynamics downforceCoefficient"));

        // --- Engine ---
        BZBusLog.Info("[WIZARD] Engine.rpmIdle: " + GetGame().ConfigGetFloat(sim + "Engine rpmIdle"));
        BZBusLog.Info("[WIZARD] Engine.rpmMin: " + GetGame().ConfigGetFloat(sim + "Engine rpmMin"));
        // 2026-06-08: cachear rpmClutch para anti-rollback (umbral de liberacion de handbrake)
        m_CachedRpmClutch = GetGame().ConfigGetFloat(sim + "Engine rpmClutch");
        if (m_CachedRpmClutch <= 0) m_CachedRpmClutch = 2000; // fallback razonable
        BZBusLog.Info("[WIZARD] Engine.rpmClutch: " + m_CachedRpmClutch + " (cached para anti-rollback)");
        BZBusLog.Info("[WIZARD] Engine.rpmRedline: " + GetGame().ConfigGetFloat(sim + "Engine rpmRedline"));
        BZBusLog.Info("[WIZARD] Engine.inertia: " + GetGame().ConfigGetFloat(sim + "Engine inertia"));
        BZBusLog.Info("[WIZARD] Engine.frictionTorque: " + GetGame().ConfigGetFloat(sim + "Engine frictionTorque"));
        BZBusLog.Info("[WIZARD] Engine.rollingFriction: " + GetGame().ConfigGetFloat(sim + "Engine rollingFriction"));
        BZBusLog.Info("[WIZARD] Engine.viscousFriction: " + GetGame().ConfigGetFloat(sim + "Engine viscousFriction"));
        DumpFloatArray(sim + "Engine torqueCurve", "Engine.torqueCurve");

        // --- Gearbox ---
        BZBusLog.Info("[WIZARD] Gearbox.type: " + GetGame().ConfigGetTextOut(sim + "Gearbox type"));
        BZBusLog.Info("[WIZARD] Gearbox.reverse: " + GetGame().ConfigGetFloat(sim + "Gearbox reverse"));
        DumpFloatArray(sim + "Gearbox ratios", "Gearbox.ratios");

        // --- Clutch ---
        BZBusLog.Info("[WIZARD] Clutch.maxTorqueTransfer: " + GetGame().ConfigGetFloat(sim + "Clutch maxTorqueTransfer"));
        BZBusLog.Info("[WIZARD] Clutch.uncoupleTime: " + GetGame().ConfigGetFloat(sim + "Clutch uncoupleTime"));
        BZBusLog.Info("[WIZARD] Clutch.coupleTime: " + GetGame().ConfigGetFloat(sim + "Clutch coupleTime"));

        // --- Drive type ---
        BZBusLog.Info("[WIZARD] SimulationModule.drive: " + GetGame().ConfigGetTextOut(sim + "drive"));

        // --- Axles (front / rear): brake torque, suspension, diff ratio ---
        BZBusLog.Info("[WIZARD] Axles.Front.maxBrakeTorque: " + GetGame().ConfigGetFloat(sim + "Axles Front maxBrakeTorque"));
        BZBusLog.Info("[WIZARD] Axles.Front.maxHandbrakeTorque: " + GetGame().ConfigGetFloat(sim + "Axles Front maxHandbrakeTorque"));
        BZBusLog.Info("[WIZARD] Axles.Front.wheelHubMass: " + GetGame().ConfigGetFloat(sim + "Axles Front wheelHubMass"));
        BZBusLog.Info("[WIZARD] Axles.Front.Differential.ratio: " + GetGame().ConfigGetFloat(sim + "Axles Front Differential ratio"));
        BZBusLog.Info("[WIZARD] Axles.Front.Suspension.stiffness: " + GetGame().ConfigGetFloat(sim + "Axles Front Suspension stiffness"));
        BZBusLog.Info("[WIZARD] Axles.Front.Suspension.damping: " + GetGame().ConfigGetFloat(sim + "Axles Front Suspension damping"));
        BZBusLog.Info("[WIZARD] Axles.Rear.maxBrakeTorque: " + GetGame().ConfigGetFloat(sim + "Axles Rear maxBrakeTorque"));
        BZBusLog.Info("[WIZARD] Axles.Rear.maxHandbrakeTorque: " + GetGame().ConfigGetFloat(sim + "Axles Rear maxHandbrakeTorque"));
        BZBusLog.Info("[WIZARD] Axles.Rear.wheelHubMass: " + GetGame().ConfigGetFloat(sim + "Axles Rear wheelHubMass"));
        BZBusLog.Info("[WIZARD] Axles.Rear.Differential.ratio: " + GetGame().ConfigGetFloat(sim + "Axles Rear Differential ratio"));

        // --- CentralDifferential ---
        BZBusLog.Info("[WIZARD] CentralDifferential.ratio: " + GetGame().ConfigGetFloat(sim + "CentralDifferential ratio"));
        BZBusLog.Info("[WIZARD] CentralDifferential.type: " + GetGame().ConfigGetTextOut(sim + "CentralDifferential type"));

        // --- Vanilla DayZ legacy props (algunos vehiculos los tienen igual) ---
        BZBusLog.Info("[WIZARD] (legacy) enginePower: " + GetGame().ConfigGetFloat(base + "enginePower"));
        BZBusLog.Info("[WIZARD] (legacy) peakTorque: " + GetGame().ConfigGetFloat(base + "peakTorque"));
        BZBusLog.Info("[WIZARD] (legacy) engineMaxRPM: " + GetGame().ConfigGetFloat(base + "engineMaxRPM"));
        BZBusLog.Info("[WIZARD] (legacy) brakeForceCoef: " + GetGame().ConfigGetFloat(base + "brakeForceCoef"));
        BZBusLog.Info("[WIZARD] (legacy) airDragFrontCoef: " + GetGame().ConfigGetFloat(base + "airDragFrontCoef"));
        BZBusLog.Info("[WIZARD] (legacy) wheelMassCoef: " + GetGame().ConfigGetFloat(base + "wheelMassCoef"));

        BZBusLog.Info("==============================================");
        BZBusLog.Info("[WIZARD-DUMP] FIN");
        BZBusLog.Info("==============================================");
    }

    private void DumpFloatArray(string path, string label) {
        array<float> vals = new array<float>;
        GetGame().ConfigGetFloatArray(path, vals);
        string s = "[";
        for (int i = 0; i < vals.Count(); i++) {
            if (i > 0) s = s + ", ";
            s = s + vals[i].ToString();
        }
        s = s + "]";
        BZBusLog.Info("[WIZARD] " + label + ": " + s);
    }

    // Dump de propiedades RUNTIME que necesitan al entity spawneado (mass,
    // bounding box, gears count efectivos). Llamado desde SpawnBus despues
    // de m_Bus.CreateObject. Completa el config-read con lo que solo se sabe
    // con el vehiculo instanciado.
    void DumpRuntimeProperties(string vc) {
        if (!m_Bus) return;
        BZBusLog.Info("--- [WIZARD-RUNTIME] " + vc + " ---");

        // Masa real via physics API (puede variar con fuel/cargo)
        BZBusLog.Info("[WIZARD-RT] mass(dBodyGetMass): " + dBodyGetMass(m_Bus));

        // Bounding box Ã¢â€ â€™ wheelbase aproximado
        vector minBB, maxBB;
        m_Bus.GetWorldBounds(minBB, maxBB);
        float boxX = maxBB[0] - minBB[0];
        float boxY = maxBB[1] - minBB[1];
        float boxZ = maxBB[2] - minBB[2];
        BZBusLog.Info("[WIZARD-RT] bbox X=" + boxX.ToString() + " Y=" + boxY.ToString() + " Z=" + boxZ.ToString());

        // Car-specific runtime props
        Car bus = Car.Cast(m_Bus);
        if (bus) {
            BZBusLog.Info("[WIZARD-RT] EngineGetRPMIdle: " + bus.EngineGetRPMIdle());
            BZBusLog.Info("[WIZARD-RT] EngineGetRPMMin: " + bus.EngineGetRPMMin());
            BZBusLog.Info("[WIZARD-RT] EngineGetRPMRedline: " + bus.EngineGetRPMRedline());
            BZBusLog.Info("[WIZARD-RT] EngineGetRPMMax: " + bus.EngineGetRPMMax());
            BZBusLog.Info("[WIZARD-RT] GetSpeedometerAbsolute(idle): " + bus.GetSpeedometerAbsolute());
        }
    }

    // Dump de propiedades de neumaticos / rueda. Las ruedas en DayZ son items
    // CfgVehicles con tireGrip, tireRollResistance, tireOffroadResistance,
    // tireTread, radius, etc. Esto completa la pieza fisica que faltaba
    // (interaccion con superficie) para el wizard config-read.
    void DumpWheelConfig(string wheelClass) {
        if (wheelClass == "") return;
        BZBusLog.Info("--- [WIZARD-WHEEL] " + wheelClass + " ---");
        string base = "CfgVehicles " + wheelClass + " ";
        // Validar que existe
        if (!GetGame().ConfigIsExisting(base + "scope")) {
            BZBusLog.Warn("[WIZARD-WHEEL] " + wheelClass + " NO existe en CfgVehicles");
            return;
        }
        BZBusLog.Info("[WIZARD-WHEEL] radius: " + GetGame().ConfigGetFloat(base + "radius"));
        BZBusLog.Info("[WIZARD-WHEEL] width: " + GetGame().ConfigGetFloat(base + "width"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreGrip: " + GetGame().ConfigGetFloat(base + "tyreGrip"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreOffroadResistance: " + GetGame().ConfigGetFloat(base + "tyreOffroadResistance"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreRollResistance: " + GetGame().ConfigGetFloat(base + "tyreRollResistance"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreTread: " + GetGame().ConfigGetFloat(base + "tyreTread"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreRoughness: " + GetGame().ConfigGetFloat(base + "tyreRoughness"));
        BZBusLog.Info("[WIZARD-WHEEL] tyreRollDrag: " + GetGame().ConfigGetFloat(base + "tyreRollDrag"));
        // Damage progressive: radiusByDamage
        DumpFloatArray(base + "radiusByDamage", "radiusByDamage");
    }

    // Aplica los inputs cacheados (llamado cada frame por modded CarScript)
    void ApplyBusInput(Car bus, float dt) {
        if (!bus) return;
        // === FRAME REPLAY (2026-07-05) ===
        // Comando PER-FRAME (40Hz) de los inputs grabados: reproduce los taps del humano.
        // TickBody corre a 2Hz -> comandar alla los aliasaria; aca (OnInput, cada frame de
        // fisica) matchea el ritmo del frame_ (dt=0.025). Solo en PLAY, fuera de pausa/spawn/
        // frozen. El cursor avanza por tiempo transcurrido real. Boris SetSteering(comando) ->
        // su motor lo rampea igual que el del humano -> mismo volante -> misma linea con taps.
        if (FrameReplayActive()) {
            if (m_FrameReplayStartTime <= 0) {
                m_FrameReplayStartTime = 1;   // marca "arrancado"; el reloj REAL es m_FrameReplayElapsed (tiempo-motor)
                m_FrameReplayElapsed = 0;
                m_FrameReplay.ResetCursor();
                // INERCIA INICIAL (2026-07-05): la toma arranca a v>0 (fila 0) y suele coastear
                // (throttle 0 la mayoria). Sin la inercia inicial Boris arranca de 0 y se para.
                // Le damos la velocidad de la fila 0 en la direccion actual del auto (mismo criterio
                // que GetVelocity del receiver). Sin esto el replay open-loop no puede reproducir el coast.
                // ORIENTACION INICIAL EXACTA (2026-07-05): setear el heading de la FILA 0 (no el del
                // spawn/waypoint, que difiere ~2deg). Cierra la semilla de rotacion inicial. Va ANTES
                // de la velocidad para que fwd0 use la orientacion corregida.
                // POSICION INICIAL EXACTA (2026-07-06): setear la posicion de la FILA 0 (no la del spawn de la
                // ruta, que puede diferir metros). Cierra el offset de LARGADA (el origen de la bola de nieve) e
                // independiza el frame_ de la ruta -> Boris arranca EXACTO donde arrancaste vos. Va antes de todo.
                bus.SetPosition(m_FrameReplay.GetStartPos());
                bus.SetOrientation(Vector(m_FrameReplay.GetStartHeading(), 0, 0));
                float v0ms = m_FrameReplay.GetStartSpeed() / 3.6;
                vector fwd0 = bus.GetDirection();
                SetVelocity(bus, fwd0 * v0ms);
                // AUTO-LOGGER (solo modo ILC): arranca el receiver en el momento 0 exacto de la toma,
                // sin depender de cuando el operador dispare NUMPAD 7. Cada pasada loguea desde t=0.
                if ((m_Config.FrameReplayILC || m_LogAiRun) && !m_AILoggerActive) StartAILogging();   // m_LogAiRun: check del reproductor
                BZBusLog.Info("[FrameReplay] START t=0 v0=" + m_FrameReplay.GetStartSpeed() + "km/h hdg0=" + m_FrameReplay.GetStartHeading() + " (" + m_FrameReplay.GetDuration() + "s, " + m_FrameReplay.GetRowCount() + " filas)");
            }
            // TICK-LOCK (2026-07-05): el reloj del replay es la ACUMULACION del dt de fisica
            // (tiempo-motor), NO GetTickTime (wall-clock/humano). Suelda cada fila grabada al tick
            // del motor -> el estado existe EN el tick correcto, no cerca de un segundo humano.
            m_FrameReplayElapsed += dt;
            if (m_FrameReplay.Sample(m_FrameReplayElapsed)) {
                float frThr = m_FrameReplay.CurThrottle();
                float frBrk = m_FrameReplay.CurBrake();
                // COAST COMPENSATION (2026-07-05): FEEDFORWARD determinista del exceso de engine-braking
                // (canal embrague, ciego). En coast puro (throttle~0 y brake~0) aplica el throttle chico
                // calibrado. NO reacciona al error de Boris (eso seria feedback); emula una caracteristica
                // MEDIDA+SISTEMATICA del motor. Nuestro aporte: reconstruir la fisica que el motor esconde.
                if (m_Config && m_Config.FrameReplayCoastComp > 0 && frThr < 0.1 && frBrk < 0.1) {
                    frThr = m_Config.FrameReplayCoastComp;
                }
                // ANCLA LONGITUDINAL DE POSICION = FEEDBACK (2026-07-05): rechaza el CAOS de la aproximacion.
                // TEOREMA (verificado): endpoint = 1.43*(v_aprox - 10.56), r=0.99; y v_aprox NO es predecible
                // desde ningun estado inicial medido (fuel/masa/RPM/marcha/FPS todos iguales) -> es caos
                // determinista amplificado sobre 17s. El feedforward (comp) aprende lo REPETIBLE (el sesgo medio);
                // el caos NO es repetible -> solo el FEEDBACK lo rechaza (control 101). Trackea DONDE deberias
                // estar (pos grabada), corrige v hacia el target. SIN tocar el steering (los taps intactos).
                // TUNING (2026-07-05): apretado para ver el caos -> el deadband viejo (1.5) era > que la
                // perturbacion (+-0.5km/h) y no reaccionaba (dejaba ~1.1m). posBoost 1.5->2.5, deadband 1.5->0.6.
                if (!m_Config || m_Config.FrameReplaySpeedLock) {
                    vector bpos = bus.GetPosition();
                    vector bfwd = bus.GetDirection();
                    // Error a lo largo del camino: proyeccion de (pos_grabada - pos_boris) sobre el forward.
                    // + = la pos grabada esta ADELANTE = Boris atrasado -> acelerar.
                    float dxAlong = m_FrameReplay.CurX() - bpos[0];
                    float dzAlong = m_FrameReplay.CurZ() - bpos[2];
                    float alongErr = dxAlong * bfwd[0] + dzAlong * bfwd[2];
                    float posBoost = alongErr * 2.5;
                    if (posBoost > 8.0) posBoost = 8.0;
                    if (posBoost < -8.0) posBoost = -8.0;
                    float target = m_FrameReplay.CurSpeed() + posBoost;
                    if (target < 0) target = 0;
                    float spdErr = target - bus.GetSpeedometerAbsolute();
                    if (spdErr > 0.6) {
                        frThr = frThr + (spdErr - 0.6) * 0.08;
                        if (frThr > 1.0) frThr = 1.0;
                        frBrk = 0;
                    } else if (spdErr < -0.6) {
                        frThr = 0;
                        frBrk = frBrk + (-spdErr - 0.6) * 0.08;
                        if (frBrk > 1.0) frBrk = 1.0;
                    }
                }
                // AUTOCOMPENSACION UNIVERSAL (ILC, 2026-07-05): sumar la comp per-frame IMPRESA (inverso del
                // residuo medido en pasadas anteriores). Feedforward determinista, CIEGO A LA CAUSA (el residuo
                // es la firma combinada de todos los canales ciegos). 0 si el frame_ aun no fue compensado.
                frThr = frThr + m_FrameReplay.CurCompThr();
                if (frThr > 1.0) frThr = 1.0;
                if (frThr < 0) frThr = 0;
                float frStr = m_FrameReplay.CurSteering() + m_FrameReplay.CurCompStr();
                // === WHEEL-TRACKING (Path 1, 2026-07-06): FEEDBACK EN EL ANGULO DE RUEDA, no en la posicion ===
                // El cmd grabado es un pulso +-1 FRAGIL al timing (un tick corrido -> la rampa integra distinto ->
                // drift). La rueda EFECTIVA (front_wheel_deg) es la salida robusta del interprete = lo que genera
                // la fuerza. Aca: cmd = cmd_grabado (feedforward, corre la rampa) + K*(rueda_grabada - rueda_boris)
                // (feedback). Cierra el lazo donde la fisica LEE la fuerza -> reproduce la salida, no pelea la pos.
                // Lazo RAPIDO (la rueda responde al toque) y estable. Rueda de Boris = mismo metodo que el receiver.
                if (m_FrameReplay.HasWheel()) {
                    vector wdirB = bus.WheelGetDirection(0);
                    float wheelHB = Math.Atan2(wdirB[0], wdirB[2]) * Math.RAD2DEG;
                    vector fwdB = bus.GetDirection();
                    float headingB = Math.Atan2(fwdB[0], fwdB[2]) * Math.RAD2DEG;
                    float borisWheel = wheelHB - headingB;
                    while (borisWheel > 180)  borisWheel = borisWheel - 360;
                    while (borisWheel < -180) borisWheel = borisWheel + 360;
                    float wheelErr = m_FrameReplay.CurWheelDeg() - borisWheel;
                    // OPCION B (2026-07-06): el cmd_steer es un TARGET de POSICION (rueda -> cmd*maxAngle tras la
                    // rampa), NO un rate. frStr=K*error lo trataba como rate -> error 3deg pedia cmd 0.36 = target
                    // 10.8deg -> OVERSHOOT/wander. Ahora comando la rueda grabada DIRECTO: cmd = rueda_grabada /
                    // maxAngle (~30deg Sarka; TODO leer del config) + feedback CHICO del lag. Sin overshoot: en
                    // recta rec=0 -> cmd~0 -> centra suave; en curva rec=30 -> cmd~1 -> rampea al tope.
                    frStr = m_FrameReplay.CurWheelDeg() / 30.0 + 0.02 * wheelErr;
                }
                // ANCLA CROSS-TRACK = FEEDBACK LATERAL (2026-07-05): la MITAD PERPENDICULAR del teorema.
                // El steering es open-loop (los taps) -> el error de rumbo se integra en un drift LATERAL que
                // hace BOLA DE NIEVE (medido: 0.2m -> 0.75m; el along-ancla no lo toca, no toca steering).
                // Correccion GENTIL (Stanley chico) hacia TU posicion+rumbo grabados: mantiene a Boris sobre tu
                // camino sin reemplazar la forma de los taps. Gateado con el along-ancla (los dos = el feedback).
                // Signos: steering + = derecha (aumenta yaw). crossErr + = pos grabada a la IZQ -> steer izq (neg).
                // hdgErr + = rumbo grabado mayor -> steer der (pos).
                // OFF POR DEFECTO (2026-07-05): el feedback lateral DISTORSIONA (abria la curva; "la soga"). Lo
                // reemplaza el ILC LATERAL (comp_str, feedforward que APRENDE sin tocar la soga en vivo). Queda
                // como fallback gateado (FrameReplayCrossAnchor).
                if (m_Config && m_Config.FrameReplayCrossAnchor) {
                    vector cpos = bus.GetPosition();
                    vector cfwd = bus.GetDirection();
                    vector cori = bus.GetOrientation();
                    float dxc = m_FrameReplay.CurX() - cpos[0];
                    float dzc = m_FrameReplay.CurZ() - cpos[2];
                    float crossErr = dxc * (-cfwd[2]) + dzc * cfwd[0];   // (rec - boris) . perpLeft
                    float hdgErr = m_FrameReplay.CurHeading() - cori[0];
                    while (hdgErr > 180)  hdgErr = hdgErr - 360;
                    while (hdgErr < -180) hdgErr = hdgErr + 360;
                    // Gentil (2026-07-05): gains bajos para rechazar el drift LENTO de las rectas SIN reventar
                    // la curva (el cross-anchor agresivo abria la curva 1.35m por overshoot). heading chico = poco
                    // damping, no duplica los taps en la curva. Cap Ã‚Â±0.15 (no puede pisar la forma del tap).
                    float crossCorr = -0.06 * crossErr + 0.008 * hdgErr;
                    if (crossCorr > 0.15)  crossCorr = 0.15;
                    if (crossCorr < -0.15) crossCorr = -0.15;
                    // ATENUACION EN CURVA (2026-07-05, "la soga"): defiere a TUS taps donde el camino gira rapido
                    // (heading-rate alto) -> ahi manda la forma. Solo corrige el drift en las RECTAS (donde se
                    // acumula). atten ~0.1 en curva, ~1 en recta. El endpoint (approach recto) se corrige full.
                    float atten = 1.0 - m_FrameReplay.CurHdgRate() * 0.4;
                    if (atten < 0.1) atten = 0.1;
                    if (atten > 1.0) atten = 1.0;
                    frStr = frStr + crossCorr * atten;
                }
                if (frStr > 1.0) frStr = 1.0;
                if (frStr < -1.0) frStr = -1.0;
                bus.SetThrottle(frThr);
                bus.SetSteering(frStr);
                bus.SetBrake(frBrk);
                bus.SetHandbrake(0);
                SetDesiredGear(m_FrameReplay.CurGear());
                // ILC IN-GAME (autocompensacion universal): el cursor ES la alineacion temporal exacta.
                // Mido el residuo (velocidad grabada vs actual de Boris) y acumulo el inverso en la comp del
                // frame_ para la PROXIMA pasada. Ciego a la causa. Persiste al fin del stream (Save).
                if (m_Config.FrameReplayILC) {
                    float ilcSpdErr = m_FrameReplay.CurSpeed() - bus.GetSpeedometerAbsolute();
                    // La ZONA DE COMPENSACION (slope-aware) la decide AccumComp con la pendiente GRABADA
                    // (m_Slope, de la elevacion de la toma) -> fisica-fiel y no depende del pitch vivo de Boris.
                    // El gate viejo de pitch<2 tiraba justo los frames de descenso suave donde vive el deficit.
                    m_FrameReplay.AccumComp(ilcSpdErr);
                    // ILC-LATERAL (feedforward comp_str) DESACTIVADO (2026-07-05): hornear un perfil de steering
                    // en lazo abierto DIVERGE (posicion Y rumbo, 3 pasadas: un sesgo per-frame se integra sobre
                    // 17s -> spin). Lo lateral es un problema de control -> lo maneja el cross-anchor (feedback
                    // estable) con atenuacion en curva. Sin AccumCrossErr -> comp_str queda en 0.
                }
            } else {
                // FISICA PRIMERO (2026-07-05): por defecto SIN endgame. Boris para donde la fisica lo deja
                // y medimos el endpoint -> el feedforward slope-aware debe cerrar el sesgo POR FISICA, no un
                // controlador. El endgame (creep al endpoint) queda como ULTIMO RECURSO, gateado por config.
                if (!m_Config.FrameReplayEndgame) {
                    if (!m_FrameReplayDone) {
                        m_FrameReplayDone = true;
                        if (m_Config.FrameReplayILC && !m_ILCSaved) {
                            m_FrameReplay.Save();
                            if (m_AILoggerActive) StopAILogging();
                            m_ILCSaved = true;
                        }
                        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EndOfRouteDespawn, 2500, false);
                    }
                    bus.SetThrottle(0);
                    bus.SetSteering(0);
                    bus.SetBrake(1.0);
                    bus.SetHandbrake(1.0);
                    return;
                }
                // === ENDGAME DE PRECISION DE POSICION (2026-07-05, DIRECCIONAL-GENERAL) ===
                // El humano ya paro (fin del stream), pero en TERRENO ALTO Boris pudo quedar CORTO: el freno
                // grabado=1 + la gravedad lo clavan en seco, y el deadband del ancla (~1m) no lo corrige.
                // Reptar al endpoint EXACTO en el gear GRABADO antes de clavar. Es SIMETRICO forward/reverse
                // x uphill/downhill: el error lo mide approachDir grabado, la direccion del creep la da el gear.
                // Si Boris ya esta en/pasado el endpoint (downhill overshoot) -> reached=true -> clava directo.
                vector egpos = bus.GetPosition();
                float dxe = m_FrameReplay.EndX() - egpos[0];
                float dze = m_FrameReplay.EndZ() - egpos[2];
                float alongEnd = dxe * m_FrameReplay.ApproachX() + dze * m_FrameReplay.ApproachZ(); // + = Boris corto
                m_EndgameTime = m_EndgameTime + dt;
                bool reached = alongEnd <= 0.3;
                if (m_EndgameTime > 5.0) reached = true;  // timeout de seguridad: no reptar para siempre
                if (!reached && !m_EndgameLocked) {
                    // Boris corto -> reptar. Creep base + asistencia por pendiente (crawl que trepa el grado).
                    // El gear GRABADO (forward o reverse) da la direccion -> mismo codigo en reverse-cuesta.
                    vector eori = bus.GetOrientation();
                    float slopeMag = Math.AbsFloat(eori[1]);
                    float creep = 0.14 + slopeMag * 0.015;
                    if (creep > 0.40) creep = 0.40;
                    bus.SetThrottle(creep);
                    bus.SetSteering(0);
                    bus.SetBrake(0);
                    bus.SetHandbrake(0);
                    SetDesiredGear(m_FrameReplay.EndGear());
                } else {
                    // En el endpoint (o pasado, o timeout): clavar con anti-rollback + persistir ILC + despawn (una vez).
                    m_EndgameLocked = true;
                    if (!m_FrameReplayDone) {
                        m_FrameReplayDone = true;
                        if (m_Config.FrameReplayILC && !m_ILCSaved) {
                            m_FrameReplay.Save();
                            if (m_AILoggerActive) StopAILogging();
                            m_ILCSaved = true;
                        }
                        // Despawn DIFERIDO (CallLater, no sincrono -> no borrar el auto que simulamos este tick).
                        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EndOfRouteDespawn, 2500, false);
                    }
                    bus.SetThrottle(0);
                    bus.SetSteering(0);
                    bus.SetBrake(1.0);
                    bus.SetHandbrake(1.0);
                }
            }
            return;
        }
        // DIAGNOSTICO (2026-07-21): confirmar que el comando LLEGA al auto. Medido: gas 0.99 con RPM en
        // ralenti (1125) durante 2061 frames. El log de Boris registra lo que el SERVICIO quiere, no lo que
        // el AUTO recibe -> hay que contar las aplicaciones reales y ver el RPM resultante.
        m_ApplyCount++;
        // ARRANQUE DERECHO (2026-07-21). Al abrir un tramo la rueda va derecha hasta que el auto RUEDA.
        // El humano hace exactamente esto: medido, 2,35 s parado antes de la reversa con el input en 0.000
        // clavado y la rueda en 2 grados. Boris barria de +25 a -35 estando a 0 km/h, y con la rueda cruzada
        // un auto parado no despega -> 12 s clavado saliendo de la reversa. Va ACA (apply por frame) y no en
        // DriveTowards porque esa funcion esta al limite de instrucciones de Enforce.
        float steerOut = m_CachedSteering;
        if (m_LegLaunch && m_Config && m_Config.LaunchStraightEnabled) {
            if (bus.GetSpeedometerAbsolute() >= m_Config.LaunchStraightKmh) {
                m_LegLaunch = false;
            } else {
                float capLs = m_Config.LaunchStraightCap;
                if (steerOut >  capLs) steerOut =  capLs;
                if (steerOut < -capLs) steerOut = -capLs;
            }
        }
        // DIAGNOSTICO one-shot (2026-07-25): el cap de LaunchStraight no aplicaba en el arranque de la reversa
        // (volante -11deg con cap 0.03). Logueo los primeros N ticks de cada tramo para ver m_LegLaunch real.
        if (m_LaunchDbg < 22) {
            BZBusLog.Info("[LAUNCHDBG] leg=" + m_LegStart + ".." + m_LegEnd + " launch=" + m_LegLaunch + " vel=" + bus.GetSpeedometerAbsolute() + " cmd=" + m_CachedSteering + " out=" + steerOut);
            m_LaunchDbg++;
        }
        bus.SetThrottle(m_CachedThrottle);
        bus.SetSteering(steerOut);
        bus.SetBrake(m_CachedBrake);
        // 2026-06-08: handbrake aplicado para anti-rollback en pendientes.
        // En DayZ altera la friccion del eje trasero en SimulationModule,
        // bloqueando el micro-deslizamiento que SetBrake solo no captura.
        bus.SetHandbrake(m_CachedHandbrake);
    }

    void Init() {
        if (!GetGame().IsServer()) return;
        LoadSettings(); // config global del framework (admin + tecla Control Panel)
        LoadConfig();
        if (!ValidateConfig()) return;
        PreloadSlots(); // 2026-06-09: precarga slots para CTRL+NUMPAD switch instantaneo
        SpawnStopSigns();   // una sola vez, persisten entre respawns del bus
        // SCAN MAPA-COMPLETO AL BOOT (2026-07-08): headless, sin cliente ni Boris. Diferido: el mundo
        // estatico (caminos) ya esta cargado en un dedicated, pero damos margen. Gate por settings.
        if (s_Settings && s_Settings.RoadScanOnBoot) {
            float bootScanDelay = s_Settings.RoadScanBootDelaySec; if (bootScanDelay < 5.0) bootScanDelay = 45.0;
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.TriggerBootMapScan, Math.Round(bootScanDelay * 1000), false);
            BZBusLog.Info("[MAPSCAN] programado al boot en " + bootScanDelay + "s (headless, sin cliente)");
        }
        // NOTA: auto-spawn al startup no esta implementado en esta version.
        // Probamos varias variantes (directo, CallLater 10s) sin exito Ã¢â‚¬â€
        // el mundo no esta listo en Init() para crear el bus consistentemente.
        // El admin del server debe spawnear el bus manualmente con NUMPAD 2
        // despues del startup. Para detenerlo: NUMPAD 1.

        // === WIZARD COMPARATIVE DUMP (2026-06-02 experimento) ===
        // Volcar el config de varios vehiculos al startup para comparar lado a
        // lado en el RPT. No requiere spawnear Ã¢â‚¬â€ ConfigGet lee del CfgVehicles
        // directo. Util para validar config-read approach del wizard.
        DumpVehicleConfig("VW_T6");
        DumpVehicleConfig("Nissan");
        DumpVehicleConfig("ExpansionBus");
        DumpVehicleConfig("OffroadHatchback");
        DumpVehicleConfig("Hatchback_02");

        // Wheels Ã¢â‚¬â€ la pieza fisica que falta (grip por superficie). Probamos
        // varios classnames conocidos; los que no existan loguean WARN.
        DumpWheelConfig("t6Wheel_offroad");
        DumpWheelConfig("NissanWheel_offroad");
        DumpWheelConfig("ExpansionBusWheel");
        DumpWheelConfig("ExpansionBusWheel_Double");
        DumpWheelConfig("OffroadHatchback_Wheel");
        DumpWheelConfig("Hatchback_02_Wheel");
        DumpWheelConfig("HatchbackWheel");
    }

    // ===================== INTEGRACION QUEST (fase 2026-06-15) =====================
    // Hook desde Expansion_OnQuestStart (modded MissionServer). Al arrancar una quest con bots AI
    // (AICamp/Patrol), los agarramos VIVOS via el modulo global. STEP 1: validar ACCESO (loguear).
    // Los bots se spawnean en el OnEventStart del objetivo (un toque DESPUES de OnQuestStart) -> grab
    // diferido 3s. Ver [[reference_expansion_quest_api]]. Reparto: Quest=bots+logica, Framework=vehiculo.
    void OnQuestStart(ExpansionQuest quest) {
        if (!quest) return;
        ExpansionQuestConfig qc = quest.GetQuestConfig();
        if (!qc) return;
        // --- Hook Travel: si alguna ruta declara QuestTravelID == este quest, la auto-spawneamos.
        // El jugador reclama -> el bus aparece, espera SpawnHoldSeconds y maneja al destino. No es convoy.
        int _travelQid = qc.GetID();
        array<string> _troutes = GetRouteList();
        string _trf;
        string _thdr;
        BZBusRouteConfig _ttmp;
        for (int _ti = 0; _ti < _troutes.Count(); _ti++) {
            _trf = _troutes[_ti];
            _thdr = "$profile:BZ_AutoDrive\\" + _trf;
            _thdr.Replace(".json", "_hdr.json");
            if (!FileExist(_thdr)) continue;
            _ttmp = new BZBusRouteConfig();
            JsonFileLoader<BZBusRouteConfig>.JsonLoadFile(_thdr, _ttmp);
            if (_ttmp.QuestTravelID == _travelQid) {
                BZBusLog.Info("[QUEST-TRAVEL] quest " + _travelQid + " -> auto-spawn ruta " + _trf);
                m_QuestCheckID = _travelQid;
                if (LoadConfigFromPath("$profile:BZ_AutoDrive\\" + _trf)) RespawnBus();
                return;
            }
            if (_ttmp.QuestEscortID == _travelQid) {
                BZBusLog.Info("[QUEST-ESCORT] quest " + _travelQid + " -> spawn ruta " + _trf + " + buscar VIP");
                m_QuestCheckID = _travelQid;
                m_EscortBoarded = false;
                m_EscortPollTries = 0;
                if (LoadConfigFromPath("$profile:BZ_AutoDrive\\" + _trf)) {
                    RespawnBus();
                    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckEscortVIP, 4000, false);
                }
                return;
            }
        }
        m_QuestCheckID = qc.GetID();
        m_QuestPollTries = 0;
        m_QuestConvoyActive = false;
        m_QuestFleeing = false;
        m_QuestInitialBots = 0;
        m_ConvoyDriving = false;
        m_AmbushActive = false;
        m_AmbushTriggered = false;
        m_BorisArmed = false;
        BZBusLog.Info("[QUEST] Expansion_OnQuestStart -> quest id=" + m_QuestCheckID + " (poll de convoy arranca)");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 4000, false);
    }

    // ESCORT (AIVIP, ObjectiveType 9): poll buscando al VIP del quest. El VIP NO esta en QuestPatrolExists
    // (esa lista es solo AICamp/AIPatrol); esta flageado con Expansion_IsQuestVIP() y sale del enum global
    // eAIBase.eAI_GetAll(). Cuando aparece cerca del vehiculo, lo embarcamos (BoardEscortVIP, walk-in en su
    // propio grupo). Despues el vehiculo maneja al destino y el VIP baja ahi (route-end = DismountQuestCrew).
    void CheckEscortVIP() {
        if (m_QuestCheckID < 0 || m_EscortBoarded) return;
        if (!m_Bus) {
            m_EscortPollTries++;
            if (m_EscortPollTries < 200) GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckEscortVIP, 3000, false);
            return;
        }
        eAIBase vip = null;
        vector busPos = m_Bus.GetPosition();
        array<eAIBase> allAI = eAIBase.eAI_GetAll();
        for (int i = 0; i < allAI.Count(); i++) {
            eAIBase ai = allAI[i];
            if (ai && ai.Expansion_IsQuestVIP() && vector.Distance(ai.GetPosition(), busPos) <= 150.0) {
                vip = ai;
                break;
            }
        }
        if (vip) {
            BZBusLog.Info("[QUEST-ESCORT] VIP detectado (Expansion_IsQuestVIP) -> embarcando");
            BoardEscortVIP(vip);
            m_EscortBoarded = true;
            return;
        }
        m_EscortPollTries++;
        if (m_EscortPollTries < 200) {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckEscortVIP, 3000, false);
        } else {
            BZBusLog.Warn("[QUEST-ESCORT] VIP no aparecio (eAI_GetAll) tras los polls -> escort abortado");
        }
    }

    // BoardEscortVIP: sube al VIP (un eAIBase suelto, que vive en el grupo del player). Lo saca a su PROPIO
    // grupo (no tocar el grupo del jugador), lo pacifica y lo manda a la puerta del asiento 1 reusando el
    // walk-in del convoy (m_CrewBoard + StepCrewBoarding). El dismount en el destino lo hace el route-end de quest.
    void BoardEscortVIP(eAIBase vip) {
        if (!m_Bus || !vip) return;
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) return;
        if (!m_CrewBoard) m_CrewBoard = new array<ref BZBoardState>();
        if (!m_Crew) m_Crew = new array<eAIBase>();
        if (!m_CrewSeats) m_CrewSeats = new array<int>();
        // seat 2 = trasero: deja el asiento 1 (acompanante delantero) LIBRE para el jugador.
        // (bug 2026-06-22: VIP en seat 1 + player en seat 1 -> se fusionaban y trababan al player)
        int seat = 2;
        vector door; vector ddir;
        transport.CrewEntryWS(seat, door, ddir);
        bool hd = false; string ds = "";
        ExpansionFSMHelper.DoorAnimationSource(m_Bus, seat, hd, ds);
        eAIGroup vg = eAIGroup.CreateGroup(eAIFaction.Create("Passive"));
        vip.SetGroup(vg);
        vip.eAI_SetPassive(true);
        vip.eAI_SetThreatDistanceLimit(0.0);
        for (int tt = 0; tt < 16; tt++) {
            eAITarget tg = vip.GetTarget(0);
            if (!tg) break;
            vip.eAI_RemoveTarget(tg);
        }
        vip.SetMovementSpeedLimits(2, 3);
        vg.ClearWaypoints();
        vg.SetFormationState(eAIGroupFormationState.IN);
        vg.AddWaypoint(BoardGroundWP(door));
        vg.SetWaypointBehaviour(eAIWaypointBehavior.ONCE);
        if (vip.GetPathFinding()) vip.GetPathFinding().ForceRecalculate(true);
        BZBoardState e = new BZBoardState();
        e.bot = vip; e.seat = seat; e.timer = 0; e.hasDoor = hd; e.doorSrc = ds; e.entry = door; e.phase = 1;
        m_CrewBoard.Insert(e);
        m_Crew.Insert(vip);
        m_CrewSeats.Insert(seat);
        BZBusLog.Info("[QUEST-ESCORT] VIP -> WALK board seat " + seat + " (grupo propio, asiento trasero)");
    }

    // STEP 2 CONVOY (2026-06-16): el quest spawnea bots VIVOS (lazy por proximidad). Poll-eamos:
    // (1) al detectarlos -> convoy ACTIVO + guardamos el conteo inicial. (2) cuando el conteo BAJA
    // (mataste 1) -> TRIGGER: los sobrevivientes SUBEN al Cobra del framework (BoardQuestBots) y (futuro)
    // huyen al patio. Requiere el Cobra ya spawneado (NUMPAD 2). Ver [[project_brigadaz_transport_quest_integration]].
    void CheckQuestBots() {
        if (m_QuestCheckID < 0) return;
        array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
        bool exists = ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols);
        int npat = 0;
        int totalBots = 0;
        if (exists) {
            npat = patrols.Count();
            for (int i = 0; i < patrols.Count(); i++) {
                if (patrols[i] && patrols[i].m_Group) totalBots += patrols[i].m_Group.Count();
            }
        }

        // Heartbeat de diagnostico cada ~30s (10 polls x 3s): vemos QUE devuelve QuestPatrolExists sin spamear.
        if (m_QuestPollTries % 10 == 0) {
            string diag = "[QUEST-CONVOY] poll #" + m_QuestPollTries + ": patrols=" + npat + " bots=" + totalBots;
            if (!exists) diag = diag + " (QuestPatrolExists=false)";
            BZBusLog.Info(diag);
        }

        bool ambushMode = (m_Config && m_Config.ConvoyMode == "ambush_on_damage");
        string modeStr = "flee";
        if (ambushMode) modeStr = "ambush";
        if (totalBots > 0) {
            if (!m_QuestConvoyActive) {
                m_QuestConvoyActive = true;
                m_QuestInitialBots = totalBots;
                BZBusLog.Info("[QUEST-CONVOY] convoy detectado: " + totalBots + " bots VIVOS (materializando...). Modo=" + modeStr);
                // El vehiculo es PARTE del convoy: lo spawnea el framework solo, parado en la terminal
                // (route start = donde nace el convoy). NO hace falta NUMPAD 2.
                if (!m_Bus) {
                    BZBusLog.Info("[QUEST-CONVOY] auto-spawn del vehiculo del convoy en la terminal");
                    RespawnBus();
                }
            }

            if (ambushMode) {
                // ESCENA 2: los bots arrancan A BORDO. Sentarlos instant (idempotente, cubre la
                // materializacion gradual) + armar a Boris. Sin trigger por kill: el trigger es el DAÃƒâ€˜O.
                if (m_Bus) BoardAmbushBots();
            } else if (!m_QuestFleeing && totalBots > m_QuestInitialBots) {
                // Los bots materializan DE A POCO (lazy por proximidad: 3 primero, luego 5). Trackeamos el
                // PICO: el conteo inicial es el MAXIMO observado, no la primera materializacion parcial.
                m_QuestInitialBots = totalBots;
                BZBusLog.Info("[QUEST-CONVOY] convoy materializado: " + totalBots + " bots (pico actualizado)");
            } else if (!m_QuestFleeing && totalBots < m_QuestInitialBots) {
                m_QuestFleeing = true;
                int killed = m_QuestInitialBots - totalBots;
                BZBusLog.Info("[QUEST-CONVOY] >>> TRIGGER! mataste " + killed + " -> los " + totalBots + " sobrevivientes SUBEN al vehiculo");
                BoardQuestBots();
            }
        }

        // Poll por la VIDA UTIL de la quest (~30min: 600 x 3s). El jugador-sniper puede tardar mucho en
        // llegar/enganchar -> el poll viejo (3min) moria antes de que aparecieran los bots. Para al disparar.
        m_QuestPollTries++;
        if (m_QuestFleeing) return;
        if (m_QuestPollTries < 600) {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 3000, false);
        } else {
            BZBusLog.Info("[QUEST-CONVOY] poll terminado (~30min sin trigger)");
        }
    }

    // BoardQuestBots: los bots VIVOS del quest suben al Cobra del framework. Cada bot del quest esta en su
    // propia patrulla/grupo (eAIQuestPatrol, 1 c/u) -> aplicamos el recipe de caminata por bot (waypoint a
    // la puerta del asiento, al piso) reusando StepCrewBoarding (el Tick lo orquesta). Como son VIVOS (no
    // maniquies), el recipe funciona. Los metemos en m_Crew para el dismount/deploy posterior. NO los
    // re-agrupamos (preserva el kill-count del quest).
    void BoardQuestBots() {
        if (!m_Bus) { BZBusLog.Warn("[QUEST-CONVOY] no hay Cobra spawneado (NUMPAD 2) -> no se puede embarcar"); return; }
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) return;
        array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
        if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
        if (!m_CrewBoard) m_CrewBoard = new array<ref BZBoardState>();
        if (!m_Crew) m_Crew = new array<eAIBase>();
        if (!m_CrewSeats) m_CrewSeats = new array<int>();
        int seat = 1;
        for (int i = 0; i < patrols.Count(); i++) {
            eAIQuestPatrol p = patrols[i];
            if (!p || !p.m_Group) continue;
            for (int m = 0; m < p.m_Group.Count(); m++) {
                if (seat > 5) break;   // Cobra: 5 plazas de pasajero (seat 1-5)
                eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
                if (!b) continue;
                vector door; vector ddir;
                transport.CrewEntryWS(seat, door, ddir);
                bool hd = false; string ds = "";
                ExpansionFSMHelper.DoorAnimationSource(m_Bus, seat, hd, ds);
                // PACIFICAR: los bots estan en COMBATE (te disparan) -> con threat activo NO recorren
                // waypoints (el FSM exige "sin threat" para TraversingWaypoints), por eso terminaban
                // subiendo por teleport (fallback 6s). Los volvemos Passive + threat 0 + sin re-deteccion
                // + limpiamos los targets ya adquiridos -> se "olvidan" del jugador y caminan al vehiculo.
                eAIGroup bg = b.GetGroup();
                b.eAI_SetPassive(true);
                b.eAI_SetThreatDistanceLimit(0.0);   // no vuelve a fijar al jugador
                for (int tt = 0; tt < 16; tt++) {     // drenar la cola de targets ya adquiridos
                    eAITarget tg = b.GetTarget(0);
                    if (!tg) break;
                    b.eAI_RemoveTarget(tg);
                }
                if (bg) {
                    bg.SetFaction(eAIFaction.Create("Passive"));   // grupo pasivo: deja de combatir
                    b.SetMovementSpeedLimits(2, 3);
                    bg.ClearWaypoints();
                    bg.SetFormationState(eAIGroupFormationState.IN);
                    bg.AddWaypoint(BoardGroundWP(door));
                    bg.SetWaypointBehaviour(eAIWaypointBehavior.ONCE);
                    if (b.GetPathFinding()) b.GetPathFinding().ForceRecalculate(true);
                }
                BZBoardState e = new BZBoardState();
                e.bot = b; e.seat = seat; e.timer = 0; e.hasDoor = hd; e.doorSrc = ds; e.entry = door; e.phase = 1;
                m_CrewBoard.Insert(e);
                m_Crew.Insert(b);
                m_CrewSeats.Insert(seat);
                BZBusLog.Info("[QUEST-CONVOY] bot del quest -> WALK board seat " + seat + " (vivo)");
                seat++;
            }
        }
        int boarded = seat - 1;
        BZBusLog.Info("[QUEST-CONVOY] " + boarded + " sobrevivientes embarcando (StepCrewBoarding orquesta el walk).");
    }

    // OnQuestComplete: objetivos del quest cumplidos (ej. convoy entero muerto). Decide el DESTINO del
    // vehiculo del framework. Caso actual: DESPAWN (config "despawn"). Otras politicas posibles (ver manual
    // 7.7): dejarlo como botin (soltar gestion sin borrar), cleanup con timer, o dejarlo en sitio para un
    // objetivo encadenado. CleanupEntities saca el Tick -> NO se dispara el auto-respawn de OnBusDestroyed.
    // ESCENA 2 (ambush): los bots CAMINAN a bordo (NO instant-seat). El instant-seat hace SetPosition de los bots
    // ENCIMA del vehiculo (cuerpos fisicos superpuestos) -> inestabilidad que se acumula -> el vehiculo se hunde/
    // vuela + OVERLOAD del server (confirmado 2026-06-17, build 25F61F43 crasheo B). El walk-board los deja bien
    // seteados por la FSM, sin overlap. NO se pacifica (hostil). Idempotente (materializacion gradual). OJO: en
    // vehiculos con entradas CrewEntryWS superpuestas (x5: traseros = delanteros) el board se contiende -> ver
    // [[feedback_vehicle_shared_doors_boarding]]; fix real = serializar por entrada.
    void BoardAmbushBots() {
        if (!m_Bus) return;
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) return;
        array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
        if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
        if (!m_CrewBoard) m_CrewBoard = new array<ref BZBoardState>();
        if (!m_Crew) m_Crew = new array<eAIBase>();
        if (!m_CrewSeats) m_CrewSeats = new array<int>();
        if (!m_CrewLastHealth) m_CrewLastHealth = new array<float>();
        int maxSeat = transport.CrewSize() - 1;   // pasajeros = asientos 1..maxSeat (seat 0 = Boris)
        for (int i = 0; i < patrols.Count(); i++) {
            eAIQuestPatrol p = patrols[i];
            if (!p || !p.m_Group) continue;
            for (int m = 0; m < p.m_Group.Count(); m++) {
                eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
                if (!b) continue;
                if (m_Crew.Find(b) >= 0) continue;   // ya embarcando/embarcado (idempotente)
                int seat = m_Crew.Count() + 1;
                if (seat > maxSeat) break;            // no hay mas asientos de pasajero
                vector door; vector ddir;
                transport.CrewEntryWS(seat, door, ddir);
                bool hd = false; string ds = "";
                ExpansionFSMHelper.DoorAnimationSource(m_Bus, seat, hd, ds);
                // phase=0 (esperando turno): el waypoint lo emite StepCrewBoarding al promover (de a uno).
                BZBoardState e = new BZBoardState();
                e.bot = b; e.seat = seat; e.timer = 0; e.hasDoor = hd; e.doorSrc = ds; e.entry = door; e.phase = 0;
                m_CrewBoard.Insert(e);
                m_Crew.Insert(b);
                m_CrewSeats.Insert(seat);
                m_CrewLastHealth.Insert(b.GetHealth("", ""));
                BZBusLog.Info("[QUEST-AMBUSH] bot del quest -> en cola para subir (seat " + seat + ", hostil)");
            }
        }
        if (!m_BorisArmed && m_Driver) {
            ExpansionHumanLoadout.Apply(m_Driver, "BanditLoadout", false);
            m_BorisArmed = true;
            BZBusLog.Info("[QUEST-AMBUSH] Boris armado (BanditLoadout)");
        }
        // Activar cuando subieron TODOS los bots vivos del quest (no cuando se llenan los asientos) -> asi
        // funciona con menos bots que asientos (ej. 1 bot en el x5). target = min(bots vivos, maxSeat).
        int liveBots = 0;
        for (int pi = 0; pi < patrols.Count(); pi++) {
            if (patrols[pi] && patrols[pi].m_Group) liveBots += patrols[pi].m_Group.Count();
        }
        int target = maxSeat;
        if (liveBots < target) target = liveBots;
        if (!m_AmbushActive && target > 0 && m_Crew.Count() >= target) {
            m_AmbushActive = true;
            BZBusLog.Info("[QUEST-AMBUSH] convoy embarcando (caminando) (" + m_Crew.Count() + " bots + Boris) -> a manejar");
        }
    }

    // Lo llama el CarScript (EEHitBy del vehiculo) y el poll de salud de los bots. Al primer daÃƒÂ±o de arma
    // -> despliega (one-shot). Solo en ambush activo y antes de disparar.
    void NotifyConvoyDamaged() {
        if (!m_AmbushActive || m_AmbushTriggered) return;
        m_AmbushTriggered = true;
        BZBusLog.Info("[QUEST-AMBUSH] >>> DAÃƒâ€˜O recibido! FREEZE (handbrake) -> esperar parada para desplegar");
        m_Frozen = true;   // handbrake + brake -> parada DURA (mas rapida que route_stopped)
        m_AmbushStopTries = 0;
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.AmbushWaitStop, 400, false);
    }

    // El dismount animado SOLO funciona con el vehiculo DETENIDO (DayZ bloquea GetOutVehicle en movimiento).
    // El ambush ocurre a mitad de ruta a velocidad de crucero -> hay que esperar a que frene de verdad, no
    // un delay fijo (un SUV no para en 1.5s desde 40km/h). Polleamos la velocidad cada 0.4s.
    void AmbushWaitStop() {
        if (!m_Bus) return;
        Car c = Car.Cast(m_Bus);
        float kmh = 999.0;
        if (c) kmh = c.GetSpeedometerAbsolute();
        m_AmbushStopTries++;
        if (kmh < 3.0 || m_AmbushStopTries > 20) {   // parado (<3km/h) o timeout ~8s
            BZBusLog.Info("[QUEST-AMBUSH] vehiculo detenido (" + kmh + " km/h) -> DESPLIEGUE");
            AmbushDismount();
        } else {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.AmbushWaitStop, 400, false);
        }
    }

    // Despliegue (escena 2): el vehiculo ya freno -> bajan los bots (animado, puerta abierta) + Boris
    // (armado, hostil) -> campean. Reusa DismountCrew (que abre puerta y deja patrulla LOOP).
    void AmbushDismount() {
        if (!m_Bus) return;
        int dc = DismountCrew(Car.Cast(m_Bus));
        // Boris: hostil + baja animado (abre su puerta) + mortal.
        if (m_Driver) {
            eAIGroup bg = m_Driver.GetGroup();
            if (bg) bg.SetFaction(eAIFaction.Create("Mercenaries"));   // hostil al jugador, sin FF con el convoy
            HumanCommandVehicle bc = m_Driver.GetCommand_Vehicle();
            if (bc && !bc.IsGettingIn()) {
                int bseat = bc.GetVehicleSeat();
                bool bhd = false; string bds = "";
                ExpansionFSMHelper.DoorAnimationSource(m_Bus, bseat, bhd, bds);
                if (bhd && bds != "") m_Bus.SetAnimationPhase(bds, 1.0);
                bc.GetOutVehicle();
                m_Driver.SetAllowDamage(true);
            }
        }
        BZBusLog.Info("[QUEST-AMBUSH] despliegue: " + dc + " bots + Boris bajando -> campean");
    }

    // Wrapper sin-args para CallLater: baja al crew del convoy (animado) una vez que el vehiculo paro.
    void DismountQuestCrew() {
        if (!m_Bus) return;
        int dc = DismountCrew(Car.Cast(m_Bus));
        BZBusLog.Info("[QUEST-CONVOY] dismount en el patio: " + dc + " bot(s) bajando (animado). Fin de la secuencia del vehiculo.");
    }

    void OnQuestComplete(ExpansionQuest quest) {
        if (!quest) return;
        ExpansionQuestConfig qc = quest.GetQuestConfig();
        if (!qc || qc.GetID() != m_QuestCheckID) return;   // solo el convoy que estamos gestionando
        BZBusLog.Info("[QUEST-CONVOY] quest " + m_QuestCheckID + " objetivos completos -> DESPAWN del vehiculo + stop gestion");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckQuestBots);
        m_QuestCheckID = -1;
        m_QuestConvoyActive = false;
        m_QuestFleeing = false;
        m_QuestInitialBots = 0;
        CleanupEntities();   // borra Cobra + Boris y remueve el Tick (sin auto-respawn)
    }
    // ==============================================================================

    // Cierre manual del bus (NUMPAD 1 -> RPC -> aca). Limpia el vehiculo
    // y al driver sin respawnearlos. Util para liberar recursos durante
    // grabaciones humanas o test.
    void StopBus() {
        BZBusLog.Info("StopBus solicitado (manual)");
        CleanupEntities();
        BroadcastGlobal("BZ AutoDrive vehicle stopped.");
    }

    // -------------------------------------------------------------------------
    // Config

    private void LoadConfig() {
        LoadConfigFromPath(CONFIG_PATH);
    }

    // Dispara el scan del mapa completo al boot (diferido desde Init). Headless: no requiere cliente
    // ni Boris spawneado Ã¢â‚¬â€ lee los objetos estaticos del mapa (caminos) directo. 2026-07-08.
    void TriggerBootMapScan() {
        // EXTRACTOR COMPLETO y AGNOSTICO DEL MUNDO (2026-07-15). Se dispara por el flag
        // RoadScanOnBoot porque el server de scan es HEADLESS: no hay cliente => no hay consola.
        // Dimensiones automaticas (GetWorldSize) => sirve para chernarusplus / enoch / el que sea.
        // Salidas: <world>_{meta,roads,objects,railbridge,water}.csv
        // REEMPLAZA a los scanners viejos, que tenian 15360 CLAVADO (solo Chernarus: en Livonia
        // barrerian fuera del mapa) y hacian 3 barridos separados del mismo GetObjectsAtPosition3D.
        //    BZRoadScan.StartMapScan(0,0,0) Ã‚Â· BZObjectScanAll.Begin(0,0,0)
        //    BZRailBridgeScan.Begin(0,0,0)  Ã‚Â· BZWaterFineScan.Begin(0,0)
        // --- pipeline COMPLETO de un mapa nuevo (descomentar lo que haga falta) ---
        // BZMapExtract.Begin();       // calles/objetos/rail/agua/VEGETACION/ALTURA -> <world>_*.csv
        //   Para un mapa NUEVO: descomentar + RoadScanOnBoot=true + bootear el server VACIO (~6 min).
        //   HECHO los 3 (2026-07-20): chernarusplus + enoch + sakhal, altura a 5 m.
        //   La vegetacion por aca NO sirve (da ~100 = arbolitos de vereda): el motor no devuelve
        //   los arboles del terreno. Se saca del satelital -> tools\build_map_tiles.py
        //   La vegetacion NO sirve por aca (dio 781 = arbolitos de vereda): el motor no devuelve
        //   los arboles del terreno. Se saca del satelital -> tools\build_map_tiles.py
        // BZRoadwayWidth.Begin();     // ancho de calzada del motor (Roadway LOD)
        // BZSurfaceScan.Discover();   // FASE 1: vocabulario de materiales -> <world>_surftypes.csv
        // BZSurfaceScan.Capture();    // FASE 2: celdas de material-camino -> <world>_surfroad.csv
        //
        // OJO con Capture() / las "huellas" (medido 2026-07-16): SOLO sirve en SAKHAL. Ahi todo lo
        // que no es camino es NIEVE => grava = camino (479k celdas, 431 km utiles). En Chernarus
        // (y por extension Livonia, mapa templado) tierra/grava son TEXTURA DE TERRENO: 5,2M celdas,
        // 90% cp_gravel+cp_dirt = 14% del mapa entero => dio 1.070 km de caminos INVENTADOS, mas que
        // toda la red de objetos (793 km). Se descarto. Chernarus/Livonia no lo necesitan: sus caminos
        // SON objetos (polylines2 cubre 99,87% / 99,96%). Regla: nada se hereda entre mapas.
    }

    // Config GLOBAL del framework (admin + tecla del Control Panel). Se carga una
    // vez al Init, independiente de la ruta. Si no existe, escribe un default vacio.
    private void LoadSettings() {
        if (s_Settings) return;   // global compartido: cargar UNA vez (multiton: solo el 1er runner)
        s_Settings = new BZAutoDriveSettings();
        EnsureProfileDir();
        if (!FileExist(SETTINGS_PATH)) {
            JsonFileLoader<BZAutoDriveSettings>.JsonSaveFile(SETTINGS_PATH, s_Settings);
            BZBusLog.Info("[Settings] Generado default en " + SETTINGS_PATH);
            return;
        }
        JsonFileLoader<BZAutoDriveSettings>.JsonLoadFile(SETTINGS_PATH, s_Settings);
        BZBusLog.Info("[Settings] Cargado. Admins: " + s_Settings.AdminSteamIDs.Count() + " | ControlPanelKey: " + s_Settings.ControlPanelKey);
    }

    // Ã‚Â¿El SteamID es admin del Control Panel? Lista vacia = todos (modo testing).
    bool IsControlPanelAdmin(string steamId) {
        if (!s_Settings) return true;  // sin settings = permitir (no deberia pasar)
        if (s_Settings.AdminSteamIDs.Count() == 0) return true;  // vacio = todos
        return (s_Settings.AdminSteamIDs.Find(steamId) >= 0);
    }

    // Tecla configurada para abrir el Control Panel (-1 = KC_HOME default).
    int GetControlPanelKey() {
        if (!s_Settings || s_Settings.ControlPanelKey < 0) return KeyCode.KC_HOME;
        return s_Settings.ControlPanelKey;
    }

    // 2026-06-09: refactored para soportar multi-slot. Carga JSON arbitrario y
    // hace post-processing (smoothing + pitch). Llamado al boot con CONFIG_PATH
    // y en runtime con BZBusRoute_slotN.json (RespawnFromSlot).
    // Lee los waypoints de un CSV (1 linea por wp) via FGets -> sin el JsonFileLoader (reflexion) que tardaba.
    // Columnas: pos0,pos1,pos2,isStop,stopDuration,stopRadius,targetSpeed,targetGear,targetThrottle,
    //           targetBrake,targetHandbrake,targetSteering,hasInputData,mode,name
    // Split que PRESERVA campos vacios. OJO: el Split NATIVO de Enforce DROPEA los vacios,
    // y los _wp.csv con tgtHandbrake en blanco (",,") perdian columnas -> count<14 -> linea
    // descartada -> ruta con 0 waypoints (bug slot/toma 2026-06-19). Parseo por coma manual.
    private void SplitKeepEmpty(string line, TStringArray outArr) {
        outArr.Clear();
        int n = line.Length();
        int start = 0;
        for (int i = 0; i <= n; i++) {
            if (i == n || line.Substring(i, 1) == ",") {
                outArr.Insert(line.Substring(start, i - start));
                start = i + 1;
            }
        }
    }

    private int LoadWaypointsCSV(string wpPath) {
        FileHandle f = OpenFile(wpPath, FileMode.READ);
        if (!f) { BZBusLog.Err("[FASTLOAD] no se pudo abrir " + wpPath); return 0; }
        int count = 0;
        string line;
        while (FGets(f, line) >= 0) {
            if (line == "") continue;
            TStringArray parts = new TStringArray();
            SplitKeepEmpty(line, parts);        // preserva vacios (el Split nativo los dropea -> rompia slot/toma)
            if (parts.Count() < 14) continue;   // linea invalida / header de columnas
            BZWaypoint wp = new BZWaypoint();
            wp.pos[0]         = parts[0].ToFloat();
            wp.pos[1]         = parts[1].ToFloat();
            wp.pos[2]         = parts[2].ToFloat();
            wp.isStop         = (parts[3].ToInt() == 1);
            wp.stopDuration   = parts[4].ToInt();
            wp.stopRadius     = parts[5].ToFloat();
            wp.targetSpeed    = parts[6].ToFloat();
            wp.recordedSpeed  = wp.targetSpeed; // preservar la velocidad GRABADA (M2/M3 pisan targetSpeed con geometria; el approach la necesita)
            wp.targetGear     = parts[7].ToInt();
            wp.targetThrottle = parts[8].ToFloat();
            wp.targetBrake    = parts[9].ToFloat();
            wp.targetHandbrake= parts[10].ToFloat();
            wp.targetSteering = parts[11].ToFloat();
            wp.hasInputData   = (parts[12].ToInt() == 1);
            wp.mode           = parts[13];
            if (parts.Count() >= 15) wp.name = parts[14];
            if (parts.Count() >= 16) wp.targetHeading = parts[15].ToFloat();
            if (parts.Count() >= 17) wp.targetLights  = parts[16].ToInt();   // 2026-06-27: fast-load lleva luces grabadas
            if (parts.Count() >= 18) wp.targetHorn    = parts[17].ToInt();   // y bocina grabada (replay espacial)
            if (parts.Count() >= 19) wp.targetFrontWheel = parts[18].ToFloat(); // FASE 2: angulo de rueda EJECUTADO por el humano
            if (parts.Count() >= 20) wp.corridorHalfWidth = parts[19].ToFloat(); // CORREDOR-BANDA: ancho del corredor por-wp (angosto en recta, ancho en nodo)
            if (parts.Count() >= 21) wp.legBreak = (parts[20].ToInt() == 1);     // INTERCAMBIO: corte de tramo declarado por el autor (tecla al grabar / nodo en el editor)
            m_Config.Waypoints.Insert(wp);
            count++;
        }
        CloseFile(f);
        return count;
    }

    private bool LoadConfigFromPath(string path) {
        m_Config = new BZBusRouteConfig();
        EnsureProfileDir();

        // nombre de la ruta para el panel (bare filename, sacando el prefijo del profile, "\" o "/")
        string _rn = path;
        _rn.Replace("$profile:BZ_AutoDrive\\", "");
        _rn.Replace("$profile:BZ_AutoDrive/", "");
        m_RouteName = _rn;

        // CONVERSION PARA EL SERVER (2026-06-17): el JSON full queda como fuente editable, pero el server
        // lee un par compacto: _hdr.json (header sin waypoints -> JsonFileLoader en ms) + _wp.csv (waypoints
        // por FGets, loop lineal SIN reflexion). Le gana al parseo de ~180s con 5522 wps. Fallback al JSON full
        // si el par no existe. Ver [[project_json_load_metrics]] [[project_multiroute_lazy_load_design]].
        string hdrPath = path;
        hdrPath.Replace(".json", "_hdr.json");
        string wpPath = path;
        wpPath.Replace(".json", "_wp.csv");

        int loadT0 = TickCount(0);          // reloj de sistema (avanza durante el parseo bloqueante)
        string loadMethod = "none";

        if (FileExist(hdrPath) && FileExist(wpPath)) {
            JsonFileLoader<BZBusRouteConfig>.JsonLoadFile(hdrPath, m_Config);   // header chico -> instantaneo
            if (!m_Config.Waypoints) m_Config.Waypoints = new array<ref BZWaypoint>();
            int fastN = LoadWaypointsCSV(wpPath);
            loadMethod = "FGets/CSV";
            BZBusLog.Info("[FASTLOAD] header + " + fastN + " waypoints por FGets (sin JsonFileLoader). Events: " + m_Config.Events.Count());
        } else if (FileExist(path)) {
            JsonFileLoader<BZBusRouteConfig>.JsonLoadFile(path, m_Config);
            loadMethod = "JsonFileLoader";
            BZBusLog.Info("Config cargado (JSON full, fallback lento) desde " + path + ". Waypoints: " + m_Config.Waypoints.Count() + ". Events: " + m_Config.Events.Count());
        } else {
            if (path == CONFIG_PATH) {
                WriteDefaultConfig();
                BZBusLog.Warn("Config generado en: " + path + " - completar waypoints y reiniciar.");
            } else {
                BZBusLog.Err("LoadConfigFromPath: archivo no existe: " + path);
            }
            return false;
        }

        int loadMs = TickCount(0) - loadT0;   // ms transcurridos (TickCount(arg) da absoluto, no delta -> resto a mano)
        BZBusLog.Info("[LOADTIME] metodo=" + loadMethod + " waypoints=" + m_Config.Waypoints.Count() + " -> " + loadMs + " ms");

        // INTERCAMBIOS declarados: si los hay, los tramos salen de ELLOS (ver SetLegFrom).
        m_HasLegBreaks = false;
        int nLegBrk = 0;
        for (int iLb = 0; iLb < m_Config.Waypoints.Count(); iLb++) {
            if (m_Config.Waypoints[iLb].legBreak) nLegBrk++;
        }
        if (nLegBrk > 0) {
            m_HasLegBreaks = true;
            BZBusLog.Info("[TRAMO] la ruta declara " + nLegBrk + " intercambio(s) -> los tramos salen de las marcas, no del cambio de sentido");
        }

        // FRAME REPLAY (2026-07-05): si la ruta pide replay temporal fiel, cargar el stream frame_.
        // El archivo debe estar deployado en el profile del server ($profile:BZ_AutoDrive_PathLogger\).
        m_FrameReplay = null;
        if (m_Config && m_Config.FrameReplay && m_Config.FrameReplayFile != "") {
            m_FrameReplay = new BZFrameReplay();
            if (m_FrameReplay.Load(m_Config.FrameReplayFile)) {
                BZBusLog.Info("[FrameReplay] ACTIVO: " + m_Config.FrameReplayFile + " (" + m_FrameReplay.GetRowCount() + " filas, " + m_FrameReplay.GetDuration() + "s)");
            } else {
                BZBusLog.Err("[FrameReplay] fallo la carga de " + m_Config.FrameReplayFile + " -> control normal");
                m_FrameReplay = null;
            }
        }

        for (int ei = 0; ei < m_Config.Events.Count(); ei++) {
            BZMarkerEvent dev = m_Config.Events[ei];
            string trigState = "NULL";
            if (dev.trigger) trigState = "type='" + dev.trigger.type + "'";
            string v0 = "";
            if (dev.actions.Count() > 0 && dev.actions[0]) v0 = dev.actions[0].verb;
            BZBusLog.Info("[BZBus][EVT-LOAD] Event " + ei + " wp=" + dev.wp + " actions=" + dev.actions.Count() + " trigger=" + trigState + " verb0='" + v0 + "'");
        }

        // PATH SMOOTHING (ver comentario original abajo).
        if (m_Config && m_Config.UseInverseModel && m_Config.Waypoints.Count() > 10 && m_Config.PathSmoothWindow >= 3) {
            SmoothPath();
        } else if (m_Config && m_Config.PathSmoothWindow < 3) {
            BZBusLog.Info("[PathSmooth] DESACTIVADO (PathSmoothWindow=" + m_Config.PathSmoothWindow + ")");
        }

        if (m_Config && m_Config.UseInverseModel && m_Config.Waypoints.Count() > 10 && m_Config.TargetSpeedSmoothWindow >= 3) {
            SmoothTargetSpeed();
        }

        if (m_Config && m_Config.SlopeCompensationEnabled && m_Config.Waypoints.Count() > 1) {
            PrecomputePathPitch();
        }

        // === MODO 2 (follow_path): despojar a GEOMETRIA PURA + velocidad OPTIMA por curvatura ===
        // Ignora el control grabado (vehicle-specific) y deriva la velocidad de ESTE vehiculo:
        // v = sqrt(FollowPathLatAccel * R), R = radio de curvatura local (3 wps). Recta -> cap;
        // curva 90 -> R chico -> v baja (lo que el vehiculo puede sostener). hasInputData=false
        // -> el lazo cae en "sin grabacion = cruise puro + Stanley" (config+geometria). auto_box
        // para la marcha. Permite que un vehiculo DISTINTO maneje la toma. [[demonstrated_road_graph]]
        if (m_Config && m_Config.FollowPath && m_Config.Waypoints.Count() > 2) {
            int fpN = m_Config.Waypoints.Count();
            int fpK = m_Config.FollowPathCurveSpan;   // espaciado para medir R (anti-jitter)
            if (fpK < 1) fpK = 5;
            for (int fi = 0; fi < fpN; fi++) {
                BZWaypoint fwp = m_Config.Waypoints[fi];
                float recSpeedFp = fwp.targetSpeed; // velocidad GRABADA (antes de sobreescribir) Ã¢â‚¬â€ para el cap de Modo 2
                float vKmh = m_Config.FollowPathMaxKmh;
                // span-en-METROS (fisico) si FollowPathCurveSpanM>0 (raiz del ruido de curvatura en
                // grabaciones densas); si no, span-en-indices viejo. 2026-07-07.
                if (m_Config.FollowPathCurveSpanM > 0) {
                    float RcurM = CircumRadiusMeters(fi, m_Config.FollowPathCurveSpanM);
                    if (RcurM > 0.5) {
                        float vcM = Math.Sqrt(m_Config.FollowPathLatAccel * RcurM) * 3.6;
                        if (vcM < vKmh) vKmh = vcM;
                    }
                }
                else if (fi >= fpK && fi < fpN - fpK) {
                    float Rcur = CircumRadius2D(m_Config.Waypoints[fi - fpK].GetVector(), fwp.GetVector(), m_Config.Waypoints[fi + fpK].GetVector());
                    if (Rcur > 0.5) {
                        float vc = Math.Sqrt(m_Config.FollowPathLatAccel * Rcur) * 3.6;
                        if (vc < vKmh) vKmh = vc;
                    }
                }
                // MODO 2: cap por velocidad GRABADA (respeta el ritmo del humano que la geometria pura
                // no ve: pueblo, lomada, ripio). Floor 3 km/h: no capear con ruido casi-cero (stall).
                // FollowPaintedToStop (2026-07-13, RAIZ del punch del endpoint): con floor 3 la decel fina dibujada
            // (2.6,2,1) se DESCARTA y queda la vel por curvatura = MaxKmh (184.7 recta) -> Boris punchea al top en
            // los ultimos wps. Con el flag, umbral ~0 -> se honra tu acercamiento lento hasta el stop.
            float capThreshFp = 3.0;
            if (m_Config && m_Config.FollowPaintedToStop) capThreshFp = 0.05;
            if (m_Config.FollowPathCapByRecording && recSpeedFp > capThreshFp && recSpeedFp < vKmh) vKmh = recSpeedFp;
                // HONRAR la vel GRABADA en curvas: el cap geometrico bajo el target por debajo de lo que
                // grabaste (90Ã‚Â° grabado 26, cap 14). La grabacion probo que esa vel es segura para ESTE
                // vehiculo -> la velocidad especificada del wp manda. Bounded por MaxKmh. Opt-in por flag.
                if (m_Config.FollowPathHonorCurveSpeed && recSpeedFp > 0.5) {
                    vKmh = recSpeedFp;
                    if (vKmh > m_Config.FollowPathMaxKmh) vKmh = m_Config.FollowPathMaxKmh;
                }
                // Un STOP es 0 km/h (no velocidad por curvatura): sin esto el wp isStop se recomputa alto y el
                // smoothing (window +-2) lo desparrama a los ultimos wps -> spike/punch en el tramo final (medido:
                // wp 2719 -> 40, wp 2721 -> 66). Solo con el flag (el bus lo maneja con su parking).
                if (m_Config.FollowPaintedToStop && fwp.isStop) vKmh = 0;
                fwp.targetSpeed    = vKmh;
                fwp.targetThrottle = 0;
                fwp.targetBrake    = 0;
                fwp.targetSteering = 0;
                fwp.hasInputData   = false;
            }
            // Suavizado del perfil (anti "acelera por tiempos" / "no mantiene acelerador"): MA centrado
            // sobre la velocidad por curvatura -> rampea las transiciones (anticipa la curva, acelera gradual
            // a la salida). Ayuda a TODO vehiculo a mantener momentum (clave en los de poca potencia, ej Truck).
            int fpSw = m_Config.FollowPathSpeedSmooth;
            if (fpSw >= 2) {
                array<float> fpOrig = new array<float>();
                for (int oi = 0; oi < fpN; oi++) fpOrig.Insert(m_Config.Waypoints[oi].targetSpeed);
                int fpHw = fpSw / 2;
                for (int si = 0; si < fpN; si++) {
                    float fpSum = 0; int fpCnt = 0;
                    for (int sj = si - fpHw; sj <= si + fpHw; sj++) {
                        if (sj >= 0 && sj < fpN) { fpSum += fpOrig[sj]; fpCnt++; }
                    }
                    if (fpCnt > 0) m_Config.Waypoints[si].targetSpeed = fpSum / fpCnt;
                }
            }
            // BRAKE-AHEAD (2026-07-08, "predictTurn/brakeDistance" del desacople Arma): pasada BACKWARD que
            // garantiza que Boris pueda DESACELERAR a la velocidad de cada curva ANTES de llegar (v[i] tal
            // que frenando a FollowPathBrakeDecel llega a v[i+1]). Sin esto entra a la V aguda a 24 km/h
            // (necesita ~17 a R=6.7m) -> understeer -> corta -> frena en seco -> AR. Va DESPUES del smooth.
            if (m_Config.FollowPathBrakeDecel > 0 && fpN > 1) {
                float bdec = m_Config.FollowPathBrakeDecel;
                int   nHonorBk = 0;   // cuantos wps salvo la grabacion del brake-ahead conservador
                for (int bi = fpN - 2; bi >= 0; bi--) {
                    float segLbk = vector.Distance(m_Config.Waypoints[bi].GetVector(), m_Config.Waypoints[bi + 1].GetVector());
                    float vNextMs = m_Config.Waypoints[bi + 1].targetSpeed / 3.6;
                    float vCapKmh = Math.Sqrt(vNextMs * vNextMs + 2.0 * bdec * segLbk) * 3.6;
                    // HONRAR LA DECEL GRABADA: el brake-ahead asume bdec (conservador) y exige llegar mas lento
                    // que lo que el humano DEMOSTRO. La grabacion es autoconsistente -> el cap no baja de ella.
                    float recBk = m_Config.Waypoints[bi].recordedSpeed;
                    if (m_Config.FollowPathHonorDecel && recBk > 0.5 && vCapKmh < recBk) {
                        vCapKmh = recBk;
                        nHonorBk++;
                    }
                    if (m_Config.Waypoints[bi].targetSpeed > vCapKmh) m_Config.Waypoints[bi].targetSpeed = vCapKmh;
                }
                if (m_Config.FollowPathHonorDecel)
                    BZBusLog.Info("[HonorDecel] brake-ahead limitado por la grabacion en " + nHonorBk + "/" + fpN + " wps (bdec=" + bdec + " m/s2 era mas conservador que el humano)");
            }
            // PISO DE VELOCIDAD ANTI-CLAVADO (2026-07-09): la velocidad nunca baja del piso (salvo en stops
            // reales isStop), asi Boris mantiene momentum en la curva al limite y no se clava a 0 -> AR.
            // FollowPaintedToStop (2026-07-13): OFF el floor -> se respeta la decel fina dibujada (4,3,2,1) hasta 0.
            if (m_Config.FollowPathMinKmh > 0 && !m_Config.FollowPaintedToStop) {
                for (int fli = 0; fli < fpN; fli++) {
                    if (!m_Config.Waypoints[fli].isStop && m_Config.Waypoints[fli].targetSpeed < m_Config.FollowPathMinKmh)
                        m_Config.Waypoints[fli].targetSpeed = m_Config.FollowPathMinKmh;
                }
            }
            m_Config.GearStrategy = "auto_box";
            string fpSpanDesc = "" + fpK + " idx";
            if (m_Config.FollowPathCurveSpanM > 0) fpSpanDesc = "" + m_Config.FollowPathCurveSpanM + " m";
            BZBusLog.Info("[FollowPath] Modo 2 geometria+config: " + fpN + " wps, R-span=" + fpSpanDesc + " smooth=" + fpSw + " (aLat=" + m_Config.FollowPathLatAccel + " m/s2, maxKmh=" + m_Config.FollowPathMaxKmh + ")");
        }
        return true;
    }

    // 2026-06-09: respawn switching a slot pre-cargado. CTRL+NUMPAD2/3/8/9 mapean
    // a slots 1/2/3/4. CERO JSON parse en runtime (slots ya cargados al Init).
    // Switch = swap de referencias m_Config + m_PathPitch. Instant.
    // CRITICO: nunca volver a parsear JSON aca Ã¢â‚¬â€ 3MB JSON bloquea server 100+s.
    void RespawnFromSlot(int slot) {
        if (slot < 1 || slot > 4) {
            BZBusLog.Err("RespawnFromSlot slot fuera de rango: " + slot);
            return;
        }
        if (!m_SlotConfigs || slot >= m_SlotConfigs.Count() || !m_SlotConfigs[slot]) {
            BZBusLog.Err("RespawnFromSlot slot " + slot + " no esta cargado. Pre-load fallo o archivo no existe.");
            return;
        }
        BZBusLog.Info("RespawnFromSlot: switch a slot " + slot + " (pre-cargado, sin parse)");
        m_Config = m_SlotConfigs[slot];
        m_PathPitch = m_SlotPitches[slot];
        m_VehicleClassOverride = "";
        m_SpawnAttempt = 0;
        m_Paused = false;
        m_SysIDMode = 0;
        CleanupEntities();
        if (!ValidateConfig()) return;
        SpawnBus();
    }

    // CONTINUAR GRABACION (2026-07-31): carga la ruta por NOMBRE (como el reproductor) y en vez de spawnear a Boris
    // con autoplay, spawnea el vehiculo VACIO en el endpoint + teleporta al player. Para grabar la continuacion.
    void ContinuarFromPath(string fname, int wpIndex, PlayerBase player) {
        string path = "$profile:BZ_AutoDrive\\" + fname;
        m_RouteName = fname;
        m_Origin    = "continuar";
        m_VehicleClassOverride = "";
        CleanupEntities();
        if (!LoadConfigFromPath(path)) {
            BZBusLog.Err("[Continuar] no se pudo cargar " + fname);
            return;
        }
        if (!ValidateConfig()) return;
        SpawnParaContinuar(wpIndex, player);
    }

    // Pre-carga slots al Init() para hot-swap sin runtime JSON parse.
    // Size limit 1.2 MB por slot (files mas grandes bloquearian el thread).
    private void PreloadSlots() {
        if (!m_SlotConfigs) m_SlotConfigs = new array<ref BZBusRouteConfig>;
        if (!m_SlotPitches) m_SlotPitches = new array<ref array<float>>;
        m_SlotConfigs.Clear();
        m_SlotPitches.Clear();
        // index 0 = default (unused, placeholder)
        for (int n = 0; n < 5; n++) {
            m_SlotConfigs.Insert(null);
            m_SlotPitches.Insert(null);
        }
        const int MAX_SLOT_SIZE_KB = 1200;
        // Backup state pre-load (LoadConfigFromPath modifica m_Config y m_PathPitch)
        ref BZBusRouteConfig defaultConfig = m_Config;
        ref array<float> defaultPitch = m_PathPitch;
        for (int s = 1; s <= 4; s++) {
            string slotPath = "$profile:BZ_AutoDrive\\BZBusRoute_slot" + s + ".json";
            if (!FileExist(slotPath)) {
                BZBusLog.Info("[PreloadSlots] slot " + s + " no existe (skip)");
                continue;
            }
            FileAttr attr;
            FindFileHandle fh = FindFile(slotPath, slotPath, attr, FindFileFlags.ALL);
            CloseFindFile(fh);
            // TamaÃƒÂ±o check via lectura del archivo
            FileHandle sizeFh = OpenFile(slotPath, FileMode.READ);
            if (sizeFh == 0) {
                BZBusLog.Err("[PreloadSlots] no se pudo abrir slot " + s);
                continue;
            }
            string sizeBuf;
            int totalBytes = 0;
            string lineBuf;
            while (FGets(sizeFh, lineBuf) > 0) totalBytes += lineBuf.Length() + 1;
            CloseFile(sizeFh);
            int sizeKB = totalBytes / 1024;
            if (sizeKB > MAX_SLOT_SIZE_KB) {
                BZBusLog.Err("[PreloadSlots] slot " + s + " demasiado grande: " + sizeKB + " KB (limite " + MAX_SLOT_SIZE_KB + " KB). Rechazado para evitar bloqueo del thread.");
                continue;
            }
            BZBusLog.Info("[PreloadSlots] cargando slot " + s + " (" + sizeKB + " KB)...");
            if (LoadConfigFromPath(slotPath)) {
                m_SlotConfigs[s] = m_Config;
                m_SlotPitches[s] = m_PathPitch;
                BZBusLog.Info("[PreloadSlots] slot " + s + " OK: " + m_Config.Waypoints.Count() + " wps");
            } else {
                BZBusLog.Err("[PreloadSlots] slot " + s + " fallo al cargar");
            }
        }
        // Restaurar default
        m_Config = defaultConfig;
        m_PathPitch = defaultPitch;
        BZBusLog.Info("[PreloadSlots] done. Default config restaurado.");
    }

    private ref array<ref BZBusRouteConfig> m_SlotConfigs;
    private ref array<ref array<float>>      m_SlotPitches;

    // Reproductor (admin UI): lista de rutas del profile. Enumeradas on-demand (solo
    // filenames -> instantaneo, sin costo de boot). El cuerpo se carga al spawnear via _wp.csv.
    private static ref array<string> s_RouteList;   // catalogo GLOBAL de rutas. Static: compartido entre runners.

    // Enumera BZBusRoute*.json del profile (excluye _hdr y temporales). Lazy: no carga waypoints.
    private void EnumerateRoutes() {
        if (!s_RouteList) s_RouteList = new array<string>();
        s_RouteList.Clear();
        string fileName;
        FileAttr attr;
        string pattern = "$profile:BZ_AutoDrive\\BZBusRoute*.json";
        FindFileHandle fh = FindFile(pattern, fileName, attr, FindFileFlags.ALL);
        if (fh) {
            bool more = true;
            while (more) {
                if (IsRouteListable(fileName)) s_RouteList.Insert(fileName);
                more = FindNextFile(fh, fileName, attr);
            }
            CloseFindFile(fh);
        }
        BZBusLog.Info("[RouteList] " + s_RouteList.Count() + " rutas enumeradas");
    }

    private bool IsRouteListable(string fn) {
        if (fn == "") return false;
        if (fn.Contains("_hdr")) return false;   // header del fast-load
        if (fn.Contains("tmp")) return false;    // _tmp, _wiztmp, _revtmp, _newtake_tmp
        return true;
    }

    array<string> GetRouteList() {
        if (!s_RouteList) EnumerateRoutes();
        return s_RouteList;
    }

    // Server: responde la lista al admin que la pide (re-valida admin).
    void HandleRouteListRequest(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        EnumerateRoutes();
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(s_RouteList.Count());
        for (int i = 0; i < s_RouteList.Count(); i++) rpc.Write(s_RouteList[i]);
        rpc.Send(recipient, BZBusRPC.RECEIVE_ROUTE_LIST, true, sender);
        BZBusLog.Info("[RouteList] enviadas " + s_RouteList.Count() + " rutas a " + sender.GetPlainId());
    }

    // Server: info de una toma para el PREVIEW del reproductor. Lee el _hdr (vehiculo) + el _wp.csv (N wps,
    // distancia, vel max). No carga la ruta (no toca estado): solo lee para mostrar antes de spawnear/cargar.
    void HandleRouteInfoRequest(ParamsReadContext ctx, PlayerIdentity sender) {
        string fname;
        if (!ctx.Read(fname)) return;
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        // vehiculo: del _hdr.json (config chico, load rapido a un temp; NO toca m_Config)
        string hdrPath = "$profile:BZ_AutoDrive\\" + fname;
        hdrPath.Replace(".json", "_hdr.json");
        string vehicle = "?";
        if (FileExist(hdrPath)) {
            BZBusRouteConfig tmp = new BZBusRouteConfig();
            JsonFileLoader<BZBusRouteConfig>.JsonLoadFile(hdrPath, tmp);
            if (tmp && tmp.VehicleClass != "") vehicle = tmp.VehicleClass;
        }
        // N wps + distancia + vel max: del _wp.csv
        string wpPath = "$profile:BZ_AutoDrive\\" + fname;
        wpPath.Replace(".json", "_wp.csv");
        int nwp = 0;
        float dist = 0;
        float maxkmh = 0;
        float px = 0;
        float pz = 0;
        bool first = true;
        FileHandle fh = OpenFile(wpPath, FileMode.READ);
        if (fh) {
            string ln;
            while (FGets(fh, ln) >= 0) {
                if (ln == "") continue;
                TStringArray parts = new TStringArray();
                SplitKeepEmpty(ln, parts);
                if (parts.Count() < 14) continue;
                float x = parts[0].ToFloat();
                float z = parts[2].ToFloat();
                float sp = parts[6].ToFloat();
                if (sp > maxkmh) maxkmh = sp;
                if (!first) dist += Math.Sqrt((x - px) * (x - px) + (z - pz) * (z - pz));
                px = x; pz = z; first = false;
                nwp++;
            }
            CloseFile(fh);
        }
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(fname);
        rpc.Write(vehicle);
        rpc.Write(nwp);
        rpc.Write(dist);
        rpc.Write(maxkmh);
        rpc.Send(recipient, BZBusRPC.RECEIVE_ROUTE_INFO, true, sender);
        BZBusLog.Info("[RouteInfo] " + fname + ": " + vehicle + " " + nwp + "wp dist=" + dist + " max=" + maxkmh);
    }

    // Server: carga + spawnea una ruta por nombre (re-valida admin + valida que este en la lista).
    void HandleLoadRouteRequest(ParamsReadContext ctx, PlayerIdentity sender) {
        string fname;
        if (!ctx.Read(fname)) return;
        bool logNative = false; ctx.Read(logNative);   // checks del reproductor: opt-in por corrida (no auto)
        bool logAi = false; ctx.Read(logAi);
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        EnumerateRoutes();
        if (s_RouteList.Find(fname) < 0) {
            BZBusLog.Err("[RouteList] load rechazado (no esta en la lista): " + fname);
            return;
        }
        // UN RUNNER POR RUTA (decision 2026-06-19): si esa toma ya esta corriendo, no duplicar.
        array<ref BZBusService> rs0 = GetRunners();
        for (int di = 0; di < rs0.Count(); di++) {
            BZBusService ex = rs0.Get(di);
            if (ex && ex.RunnerIsActive() && ex.GetRouteName() == fname) {
                BZBusLog.Info("[Multiton] '" + fname + "' ya esta corriendo (runner " + di + "), no se duplica.");
                return;
            }
        }
        // MULTITON: si el primary esta LIBRE (sin bus), cargar aca (reusa runner[0]).
        // Si esta ocupado, crear un runner NUEVO -> corren en paralelo (varios buses).
        if (!RunnerIsActive()) {
            m_LogBorisNative = logNative; m_LogAiRun = logAi;   // armar loggers ANTES del respawn (BZServerProbe los ve de frame 1)
            RespawnFromPath(fname);
            BZBusLog.Info("[Multiton] cargado en el primary: " + fname + " | logNative=" + logNative + " logAi=" + logAi);
        } else {
            if (!s_Runners) s_Runners = new array<ref BZBusService>();
            BZBusService r = new BZBusService();
            s_Runners.Insert(r);
            r.m_LogBorisNative = logNative; r.m_LogAiRun = logAi;
            r.RespawnFromPath(fname);
            BZBusLog.Info("[Multiton] runner nuevo: " + fname + " (total runners: " + s_Runners.Count() + ")");
        }
    }

    // Lo lee BZServerProbe: solo graba boris_native de este runner si el check estaba tildado al dar play.
    bool WantsBorisNative() { return m_LogBorisNative; }

    // Carga una ruta del profile y respawnea. Freeze-guard: si no hay _wp.csv y el JSON es
    // grande, lo rechaza (cargar JSON crudo grande en runtime congelaria el thread).
    void RespawnFromPath(string fname) {
        string path = "$profile:BZ_AutoDrive\\" + fname;
        string wpPath = path;
        wpPath.Replace(".json", "_wp.csv");
        if (!FileExist(wpPath)) {
            FileHandle szf = OpenFile(path, FileMode.READ);
            if (szf == 0) { BZBusLog.Err("[RouteList] no se pudo abrir " + path); return; }
            int bytes = 0;
            string ln;
            while (FGets(szf, ln) > 0) bytes += ln.Length() + 1;
            CloseFile(szf);
            int kb = bytes / 1024;
            if (kb > 400) {
                BZBusLog.Err("[RouteList] " + fname + " sin _wp.csv y " + kb + "KB (>400): rechazado, congelaria el server. Pre-splitear con route_split.ps1.");
                return;
            }
        }
        m_RouteName = fname;
        m_Origin    = "lista";   // cargado desde el reproductor (vs "quest"/"config")
        BZBusLog.Info("[RouteList] cargando " + fname + " ...");
        if (LoadConfigFromPath(path)) {
            RespawnBus();
            BZBusLog.Info("[RouteList] " + fname + " cargada + spawneada");
        }
    }

    // ---- MULTITON: identidad + estado que cada runner expone al panel ----
    private string m_RouteName = "";       // nombre de archivo de la ruta de ESTE runner
    private string m_Origin    = "config"; // de donde salio: lista / quest / config

    void SetOrigin(string o) { m_Origin = o; }
    string GetRouteName() { return m_RouteName; }
    string GetOrigin()    { return m_Origin; }
    bool RunnerIsActive() { return (m_Bus && !m_Bus.IsRuined()); }
    bool RunnerIsPaused() { return m_Paused; }
    int  RunnerWpIdx()    { return m_WaypointIndex; }
    int RunnerWpTotal() {
        if (m_Config && m_Config.Waypoints) return m_Config.Waypoints.Count();
        return 0;
    }
    vector RunnerPos() {
        if (m_Bus) return m_Bus.GetPosition();
        return "0 0 0";
    }
    float RunnerSpeedKmh() {
        Car c = Car.Cast(m_Bus);
        if (c) return c.GetSpeedometerAbsolute();
        return 0;
    }

    // ========================================================================
    //  API DE CONTROL PARA MODS EXTERNOS (ej. NPC chofer eAI de BZ_Citizens).
    //  BZ_AutoDrive = ejecutor de conduccion; el mod eAI = cerebro de eventos del viaje.
    //  Todo server-side, SIN RPC: obtene el runner con GetRunnerForCar(car) o PlayRouteByName(),
    //  y llama estos metodos. Contrato estable -> no hurgar internals privados.
    // ========================================================================

    // Reproduce una toma por nombre de archivo: carga (_hdr.json/_wp.csv) + spawnea en el wp0 de la
    // ruta + arranca. Devuelve el runner que la maneja (o null si fallo/ruta inexistente). Reusa el
    // primary si esta libre; si no, crea un runner nuevo (corren en paralelo). No duplica si ya corre.
    // holdAtStops=true (default) -> en CADA parada (isStop, las que grabas con NUMPAD4) queda en HOLD
    // esperando ResumeFromHold(): asi el eAI controla la decision de cada parada sin carrera de tiempos.
    static BZBusService PlayRouteByName(string fname, bool holdAtStops = true) {
        BZBusService prim = GetInstance();
        prim.EnumerateRoutes();
        if (s_RouteList.Find(fname) < 0) {
            BZBusLog.Err("[API] PlayRouteByName: ruta no esta en la lista: " + fname);
            return null;
        }
        array<ref BZBusService> rs = GetRunners();
        for (int i = 0; i < rs.Count(); i++) {
            BZBusService ex = rs.Get(i);
            if (ex && ex.RunnerIsActive() && ex.GetRouteName() == fname) return ex;  // ya corre -> devolver ese
        }
        BZBusService target;
        if (!prim.RunnerIsActive()) {
            target = prim;
        } else {
            if (!s_Runners) s_Runners = new array<ref BZBusService>();
            target = new BZBusService();
            s_Runners.Insert(target);
        }
        target.m_HoldAtStops = holdAtStops;
        target.RespawnFromPath(fname);
        target.SetOrigin("taxi");
        return target;
    }

    // ---- estado (para que el eAI sepa que hacer) ----
    bool IsHeld()       { return m_HoldActive; }   // true = detenido en un stop (o por HoldRunner) esperando decision
    bool RunnerAtStop() { return m_AtStop; }       // true = parado en una parada de servicio (isStop)

    // ---- control del viaje (lo maneja el eAI) ----
    void SetHoldAtStops(bool on) { m_HoldAtStops = on; }        // activar/desactivar el modo taxi (HOLD en cada stop)

    // Reanuda desde un HOLD. Si estaba EN una parada -> avanza al proximo wp. Si fue un HoldRunner()
    // a mitad de tramo -> solo suelta el freno y sigue hacia el wp actual (no saltea).
    void ResumeFromHold() {
        if (!m_HoldActive) return;
        m_HoldActive = false;
        if (m_AtStop) {
            m_AtStop = false;
            AdvanceWaypoint();
            m_NextStopIndex = FindNextStopIndex(m_WaypointIndex);
        }
        BZBusLog.Info("[API] ResumeFromHold (wp=" + m_WaypointIndex + ")");
    }

    // Detiene YA al vehiculo y lo mantiene quieto (jugador pidio bajar a mitad de tramo). Reversible
    // con ResumeFromHold(). NO despawnea.
    void HoldRunner() { m_HoldActive = true; }

    // Pausa/reanuda blando (freeze de manejo). Espejo explicito de TogglePause (idempotente).
    void SetPaused(bool p) { if (m_Paused != p) TogglePause(); }

    // Termina el viaje: detiene y limpia el runner (borra vehiculo + chofer).
    void StopRunner() { StopBus(); }

    // ---- getters para que el eAI ubique/siente sus entidades (m_Bus/m_Driver son privados) ----
    EntityAI GetBus()    { return m_Bus; }
    eAIBase  GetDriver() { return m_Driver; }

    // Wheelbase del vehiculo spawneado, medido de los contactos de rueda (mismo metodo que PathLogService).
    private float ComputeWheelbaseLive() {
        Car car = Car.Cast(m_Bus);
        if (!car) return 0;
        vector fwd = car.GetDirection();
        vector vpos = car.GetPosition();
        int wcount = car.WheelCount();
        float minProj = 99999.0;
        float maxProj = -99999.0;
        int wgood = 0;
        for (int wi = 0; wi < wcount; wi++) {
            vector wpos = car.WheelGetContactPosition(wi);
            if (wpos[0] == 0 && wpos[1] == 0 && wpos[2] == 0) continue;
            wgood++;
            float proj = (wpos[0] - vpos[0]) * fwd[0] + (wpos[2] - vpos[2]) * fwd[2];
            if (proj < minProj) minProj = proj;
            if (proj > maxProj) maxProj = proj;
        }
        float wb = 0;
        if (wgood >= 2) wb = maxProj - minProj;
        if (wb < 0) wb = -wb;
        return wb;
    }

    // CONFIG-AS-DRIVING-MANUAL: auto-deriva SteeringScale del wheelbase si esta en modo auto (<=0).
    // Hace al framework vehicle-agnostic para steering (cualquier CarScript sin tuneo manual).
    // Fuente del wheelbase: fingerprint embebido (m_Config.Wheelbase) -> medicion viva -> fallback 0.7.
    // Valor > 0 en el JSON = override explicito del modder (se respeta).
    private void AutoSteeringScale() {
        if (!m_Config || m_Config.SteeringScale > 0) return;
        float wb = m_Config.Wheelbase;
        if (wb <= 0.5) wb = ComputeWheelbaseLive();
        float ss;
        if (wb > 0.5) ss = Math.Clamp(wb / 5.5, 0.4, 1.0);   // bus 5.5m->1.0 (baseline); corto -> menos
        else          ss = 0.7;                              // sin wheelbase: fallback seguro
        m_Config.SteeringScale = ss;
        BZBusLog.Info("[AutoSteer] SteeringScale auto=" + ss + " (wheelbase=" + wb + ", veh=" + m_Config.VehicleClass + ")");
    }

    // ---- DEBUGGER: logging por-runner ----
    // Cada runner se identifica en el RPT por su ruta -> con N buses sabes cual es cual.
    private string m_LogTag = "?";    // = nombre corto de la ruta. Seteado en SpawnBus.
    private int    m_TickCount = 0;   // para el heartbeat periodico (cada ~5s)

    private void SetLogTag() {
        string t = m_RouteName;
        t.Replace("BZBusRoute_", "");
        t.Replace(".json", "");
        if (t == "BZBusRoute" || t == "") t = "default";
        m_LogTag = t;
    }
    private void LogI(string m) { BZBusLog.Info("[" + m_LogTag + "] " + m); }   // siempre
    private void LogD(string m) { BZBusLog.Debug("[" + m_LogTag + "] " + m); }  // solo si verbose

    // Teleporta al admin CERCA del runner (no encima) para interceptarlo. Re-valida admin.
    void HandleTeleportToRunner(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        if (!RunnerIsActive()) { BZBusLog.Info("[Teleport] runner sin vehiculo activo"); return; }
        vector bpos = RunnerPos();
        vector dest = bpos + "8 0 8";   // ~8m al costado: cerca para interceptar, sin caer encima
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        PlayerBase pb = PlayerBase.Cast(recipient);
        if (pb) {
            pb.SetPosition(dest);
            BZBusLog.Info("[Teleport] admin -> " + dest + " (cerca del runner " + m_RouteName + ")");
        }
    }

    // Snapshot de TODOS los runners para el panel (multi-card). Re-valida admin.
    // Saca un runner secundario del registro multiton (al despawnear su toma terminada). El primary
    // (s_Instance) nunca se saca: queda inactivo en el registro y el panel lo oculta hasta la proxima carga.
    static void UnregisterRunner(BZBusService r) {
        if (!s_Runners || !r) return;
        if (r == s_Instance) return;
        int idx = s_Runners.Find(r);
        if (idx >= 0) s_Runners.RemoveOrdered(idx);
        BZBusLog.Info("[Multiton] runner desregistrado (toma terminada). Quedan: " + s_Runners.Count());
    }

    void HandleRunnersRequest(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        array<ref BZBusService> runners = GetRunners();
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(runners.Count());
        // PERF: medidor de consumo del framework (footer de la UI). Van JUSTO despues del count.
        rpc.Write(s_ServerFps);      // FPS del server (suavizado)
        rpc.Write(s_FrameworkMs);    // ms/frame que consume BZ_AutoDrive (sum runners, promediado ~1s)
        for (int i = 0; i < runners.Count(); i++) {
            BZBusService r = runners.Get(i);
            rpc.Write(r.GetRouteName());
            rpc.Write(r.GetOrigin());
            rpc.Write(r.RunnerIsActive());
            rpc.Write(r.RunnerIsPaused());
            rpc.Write(r.RunnerWpIdx());
            rpc.Write(r.RunnerWpTotal());
            rpc.Write(r.RunnerSpeedKmh());
        }
        rpc.Send(recipient, BZBusRPC.RECEIVE_RUNNERS, true, sender);
    }

    // Accion sobre un runner por indice (desde las cards del panel). Re-valida admin.
    // action: 0=stop(despawn) 1=pause/play(toggle) 2=teleport 3=reset(re-arranca desde wp0).
    // idx = posicion en GetRunners().
    void HandleRunnerCtl(ParamsReadContext ctx, PlayerIdentity sender) {
        int idx;
        int action;
        if (!ctx.Read(idx)) return;
        if (!ctx.Read(action)) return;
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        array<ref BZBusService> rs = GetRunners();
        if (idx < 0 || idx >= rs.Count()) return;
        BZBusService r = rs.Get(idx);
        if (!r) return;
        if (action == 0) {
            r.StopBus();
            if (idx >= 1) rs.RemoveOrdered(idx);   // panel runners se sacan del registro; el primary (0) queda
            BZBusLog.Info("[Multiton] runner " + idx + " STOP (total: " + rs.Count() + ")");
        } else if (action == 1) {
            r.TogglePause();
        } else if (action == 2) {
            r.HandleTeleportToRunner(sender);
        } else if (action == 3) {
            // RESET: re-arranca ESTE runner desde wp 0 de su ruta YA CARGADA. RespawnBus()
            // reusa el config en memoria (NO recarga JSON -> no congela el server) y resetea
            // m_WaypointIndex=0. Sirve para re-ver un tramo sin el ciclo completo de recarga.
            r.RespawnBus();
            BZBusLog.Info("[Multiton] runner " + idx + " RESET (re-arranca '" + r.GetRouteName() + "' desde wp 0)");
        }
    }

    // Para TODOS los runners (limpieza rapida del test de techo). El primary queda en el registro (bus parado).
    void HandleStopAll(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        array<ref BZBusService> rs = GetRunners();
        int n = rs.Count();
        for (int i = n - 1; i >= 0; i--) {   // backward: remover no afecta los indices menores
            BZBusService r = rs.Get(i);
            if (!r) continue;
            r.StopBus();
            if (i >= 1) rs.RemoveOrdered(i);
        }
        BZBusLog.Info("[Multiton] STOP ALL: " + n + " runners parados (quedan " + rs.Count() + ")");
    }

    // ===== Fase 2: registro de vehiculos VACIOS (spawn START/HERE/END sin teleport del player) =====
    // Registra un vacio recien spawneado. Lo llama SpawnParaContinuar.
    static void RegisterEmpty(EntityAI veh, string routeName, int wpIndex, vector pos) {
        if (!veh) return;
        if (!s_Empties) s_Empties = new array<ref BZEmptyVehicle>();
        BZEmptyVehicle e = new BZEmptyVehicle();
        e.veh = veh;
        e.routeName = routeName;
        e.wpIndex = wpIndex;
        e.pos = pos;
        s_Empties.Insert(e);
        BZBusLog.Info("[Empty] registrado: " + routeName + " wp" + wpIndex + " (total vacios: " + s_Empties.Count() + ")");
    }

    // Devuelve el registro depurando los que ya no existen (eliminados por otra via).
    static array<ref BZEmptyVehicle> GetEmpties() {
        if (!s_Empties) s_Empties = new array<ref BZEmptyVehicle>();
        for (int i = s_Empties.Count() - 1; i >= 0; i--) {
            BZEmptyVehicle e = s_Empties.Get(i);
            if (!e || !e.veh) s_Empties.RemoveOrdered(i);
        }
        return s_Empties;
    }

    // Snapshot de vacios para el panel. Re-valida admin. Formato: count, luego {routeName, wpIndex, posX, posZ} x N.
    void HandleEmptyListRequest(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        array<ref BZEmptyVehicle> es = GetEmpties();
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(es.Count());
        for (int i = 0; i < es.Count(); i++) {
            BZEmptyVehicle e = es.Get(i);
            rpc.Write(e.routeName);
            rpc.Write(e.wpIndex);
            float px = e.pos[0];
            float pz = e.pos[2];
            rpc.Write(px);
            rpc.Write(pz);
        }
        rpc.Send(recipient, BZBusRPC.RECEIVE_EMPTY_LIST, true, sender);
    }

    // Accion sobre un vacio por indice. Re-valida admin. action: 0=TP player al lado, 1=eliminar (ObjectDelete).
    void HandleEmptyCtl(ParamsReadContext ctx, PlayerIdentity sender) {
        int idx;
        int action;
        if (!ctx.Read(idx)) return;
        if (!ctx.Read(action)) return;
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;
        array<ref BZEmptyVehicle> es = GetEmpties();
        if (idx < 0 || idx >= es.Count()) return;
        BZEmptyVehicle e = es.Get(idx);
        if (!e || !e.veh) return;
        if (action == 0) {
            Man recipient = GetGame().GetPlayerByIdentity(sender);
            PlayerBase pb = PlayerBase.Cast(recipient);
            if (pb) {
                vector vpos = e.veh.GetPosition();
                vector dest = vpos + "2.5 0 0";   // ~2.5m al costado, sin caer encima
                dest[1] = GetGame().SurfaceY(dest[0], dest[2]);
                pb.SetPosition(dest);
                BZBusLog.Info("[Empty] admin TP -> vacio " + idx + " (" + e.routeName + " wp" + e.wpIndex + ")");
            }
        } else if (action == 1) {
            GetGame().ObjectDelete(e.veh);
            es.RemoveOrdered(idx);
            BZBusLog.Info("[Empty] eliminado " + idx + " (" + e.routeName + "). Quedan: " + es.Count());
        }
    }

    // Pre-compute pitch del path (cached). dy/dist entre wps consecutivos en
    // radianes. Positivo = subida, negativo = bajada. Llamado UNA VEZ al cargar
    // (despues de SmoothPath). En runtime se lee directo del array, sin recomputo.
    private void PrecomputePathPitch() {
        int n = m_Config.Waypoints.Count();
        if (!m_PathPitch) m_PathPitch = new array<float>;
        m_PathPitch.Clear();
        for (int i = 0; i < n; i++) {
            if (i + 1 >= n) { m_PathPitch.Insert(0); continue; }
            vector p1 = m_Config.Waypoints[i].GetVector();
            vector p2 = m_Config.Waypoints[i + 1].GetVector();
            float dx_sl = p2[0] - p1[0];
            float dz_sl = p2[2] - p1[2];
            float dxz_sl = Math.Sqrt(dx_sl*dx_sl + dz_sl*dz_sl);
            if (dxz_sl < 0.1) { m_PathPitch.Insert(0); continue; }
            float dy_sl = p2[1] - p1[1];
            m_PathPitch.Insert(Math.Atan2(dy_sl, dxz_sl));
        }
        BZBusLog.Info("[SlopeCompensation] Pre-computed pitch para " + n + " wps");
    }

    // Pitch promediado lookahead. Suaviza spikes de microruido del path.
    private float GetEffectivePitch(int wpIdx, int lookaheadWps) {
        if (!m_PathPitch || m_PathPitch.Count() == 0) return 0;
        if (wpIdx < 0) wpIdx = 0;
        if (wpIdx >= m_PathPitch.Count()) wpIdx = m_PathPitch.Count() - 1;
        float sum_sl = 0;
        int count_sl = 0;
        int maxIdx_sl = wpIdx + lookaheadWps;
        if (maxIdx_sl >= m_PathPitch.Count()) maxIdx_sl = m_PathPitch.Count() - 1;
        for (int j = wpIdx; j <= maxIdx_sl; j++) {
            sum_sl += m_PathPitch[j];
            count_sl++;
        }
        if (count_sl == 0) return 0;
        return sum_sl / count_sl;
    }

    // Target speed smoothing: moving average window sobre el targetSpeed escalar de cada wp.
    // NO toca posiciones (mas seguro que SmoothPath). El PID converge mejor a una curva
    // suave de speed que persiguiendo micro-fluctuaciones del recording humano.
    private void SmoothTargetSpeed() {
        int n = m_Config.Waypoints.Count();
        int windowSize = m_Config.TargetSpeedSmoothWindow;
        if (windowSize < 3) return;
        if (windowSize > 15) windowSize = 15;
        int half = (windowSize - 1) / 2;
        int smoothed = 0;

        // Snapshot velocidades originales
        array<float> origSpeeds = new array<float>;
        for (int ci = 0; ci < n; ci++) {
            origSpeeds.Insert(m_Config.Waypoints[ci].targetSpeed);
        }

        for (int i = half; i < n - half; i++) {
            BZWaypoint wp = m_Config.Waypoints[i];
            if (wp.isStop) continue;  // preservar stops (speed exacta importa)

            // Solo smoothear dentro del mismo mode
            bool sameMode = true;
            string myMode = wp.mode;
            for (int k = i - half; k <= i + half; k++) {
                if (m_Config.Waypoints[k].mode != myMode) { sameMode = false; break; }
            }
            if (!sameMode) continue;

            float sum = 0;
            for (int kk = i - half; kk <= i + half; kk++) {
                sum += origSpeeds[kk];
            }
            wp.targetSpeed = sum / windowSize;
            smoothed++;
        }

        BZBusLog.Info("[SpeedSmooth] Smoothed " + smoothed + " / " + n + " targetSpeed values (window=" + windowSize + ")");
    }

    // Path smoothing: moving average window configurable sobre x/y/z.
    // Solo aplica dentro de blocks del mismo mode. Boundary preservation.
    private void SmoothPath() {
        int n = m_Config.Waypoints.Count();
        int windowSize = m_Config.PathSmoothWindow;
        if (windowSize < 3) return;
        if (windowSize > 11) windowSize = 11;  // cap razonable
        int half = (windowSize - 1) / 2;
        int smoothed = 0;

        // Copia inicial de posiciones originales (para no mezclar progresivamente)
        array<vector> origPos = new array<vector>;
        for (int ci = 0; ci < n; ci++) {
            origPos.Insert(m_Config.Waypoints[ci].GetVector());
        }

        for (int i = half; i < n - half; i++) {
            BZWaypoint wp = m_Config.Waypoints[i];
            if (wp.isStop) continue;  // preservar paradas exactas

            // Verificar que toda la ventana sea del mismo mode
            bool sameMode = true;
            string myMode = wp.mode;
            for (int k = i - half; k <= i + half; k++) {
                if (m_Config.Waypoints[k].mode != myMode) { sameMode = false; break; }
            }
            if (!sameMode) continue;

            // Moving average
            vector sum = "0 0 0";
            for (int kk = i - half; kk <= i + half; kk++) {
                sum = sum + origPos[kk];
            }
            vector smoothedPos = sum * (1.0 / windowSize);  // promedio sobre N puntos efectivos

            // Aplicar al waypoint
            wp.pos[0] = smoothedPos[0];
            wp.pos[1] = smoothedPos[1];
            wp.pos[2] = smoothedPos[2];
            smoothed++;
        }

        BZBusLog.Info("[PathSmooth] Smoothed " + smoothed + " / " + n + " wps (window=" + windowSize + ", preserved stops + mode boundaries)");
    }

    private void EnsureProfileDir() {
        string dir = "$profile:BZ_AutoDrive\\";
        if (!FileExist(dir))
            MakeDirectory(dir);
    }

    private void WriteDefaultConfig() {
        BZBusRouteConfig def = new BZBusRouteConfig();
        def.RespawnDelay   = 300;
        def.AverageSpeedMS = 11.0;
        def.EndHoldSeconds = 3.0;
        def.VehicleClass   = "ExpansionBus";
        def.DriverClass    = "eAI_SurvivorM_Boris";

        // Template con 9 waypoints alternando stop/intermedio. Coords en cero,
        // el admin completa a mano antes del primer arranque real.
        AddTemplateStop(def, "Kamenka");
        AddTemplateMid(def);
        AddTemplateStop(def, "Komarovo");
        AddTemplateMid(def);
        AddTemplateStop(def, "Balota");
        AddTemplateMid(def);
        AddTemplateStop(def, "Chernogorsk");
        AddTemplateMid(def);
        AddTemplateStop(def, "Elektrozavodsk");

        JsonFileLoader<BZBusRouteConfig>.JsonSaveFile(CONFIG_PATH, def);
    }

    private void AddTemplateStop(BZBusRouteConfig cfg, string name) {
        BZWaypoint wp = new BZWaypoint();
        wp.pos[0] = 0; wp.pos[1] = 0; wp.pos[2] = 0;
        wp.isStop = true;
        wp.name = name;
        wp.stopDuration = 7;
        wp.stopRadius = 60;
        cfg.Waypoints.Insert(wp);
    }

    private void AddTemplateMid(BZBusRouteConfig cfg) {
        BZWaypoint wp = new BZWaypoint();
        wp.pos[0] = 0; wp.pos[1] = 0; wp.pos[2] = 0;
        wp.isStop = false;
        wp.name = "";
        wp.stopDuration = 0;
        wp.stopRadius = 0;
        cfg.Waypoints.Insert(wp);
    }

    private bool ValidateConfig() {
        if (!m_Config || m_Config.Waypoints.Count() < 2) {
            BZBusLog.Warn("Ruta incompleta (< 2 waypoints), servicio no iniciado.");
            return false;
        }

        // Si todos los waypoints estan en [0,0,0] es el template sin completar
        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (!wp.IsZero())
                return true;
        }

        BZBusLog.Warn("Todos los waypoints son [0,0,0] - completar " + CONFIG_PATH + " y reiniciar.");
        return false;
    }

    // -------------------------------------------------------------------------
    // Spawn

    // Respawn manual (NUMPAD 2 desde cliente -> RPC -> aca)
    // NO recarga el JSON Ã¢â‚¬â€ el archivo se lee solo en Init() porque cargar
    // un JSON grande (5MB / 7004 wps) en runtime bloquea el thread del
    // servidor 2+ minutos. Para cambiar entre estados (BUS / LANDROVER /
    // etc) hay que cerrar el server, hacer swap_route.ps1, y reabrir.
    void RespawnBus() {
        BZBusLog.Info("RespawnBus solicitado (manual) Ã¢â‚¬â€ respawn sin recargar JSON (usa config en memoria)");
        m_VehicleClassOverride = "";
        m_SpawnAttempt         = 0;
        m_Paused               = false;
        m_SysIDMode            = 0;
        CleanupEntities();
        // NO recargar JSON aca Ã¢â‚¬â€ bloquea el thread del servidor varios minutos
        // con rutas de 2+ MB (3000+ waypoints). Trade-off documentado:
        // perdemos iter rapido (record -> deploy -> NUMPAD 2 sin reiniciar)
        // para preservar estabilidad con rutas largas. Para cambiar de ruta:
        // cerrar server, swap JSON, reiniciar. Validado 2026-06-01: con JSON
        // 2.6MB, LoadConfig en runtime tomaba 108s y disparaba "No alive 10000"
        // -> disconnect del cliente -> vehiculo invisible.
        if (!ValidateConfig()) return;
        SpawnBus();
    }

    // Respawn con vehiculo distinto (debug del problema de shift con eAI driver)
    void RespawnAs(string vehicleClass) {
        BZBusLog.Info("RespawnAs: " + vehicleClass);
        m_VehicleClassOverride = vehicleClass;
        CleanupEntities();
        if (!ValidateConfig()) return;
        SpawnBus();
    }

    private string m_VehicleClassOverride;

    // CONTINUAR GRABACION (2026-07-31, Sonom4n): para grabar rutas largas (la costera ida y vuelta) en varias sesiones.
    // Spawnea el vehiculo de la toma VACIO (sin eAI, sin autoplay) en el ULTIMO wp (el endpoint), orientado al heading
    // grabado, y teleporta al player al lado. El jugador se sube, graba la continuacion (frame recorder de siempre), y
    // mergea despues en el editor. El merge es LIMPIO: la continuacion arranca fisicamente donde termino la original,
    // sin gap ni desalineacion. NO toca m_Bus (no lo maneja el service) -> es un vehiculo libre para el player.
    // Spawnea el vehiculo de la toma VACIO en el wp <wpIndex> (0=inicio, Count-1=final, cualquiera del medio via
    // scrubber) orientado al heading del segmento, + teleporta al player al lado. El vehiculo + vos, en ese punto.
    void SpawnParaContinuar(int wpIndex, PlayerBase player) {
        if (!m_Config || !m_Config.Waypoints || m_Config.Waypoints.Count() < 1) {
            BZBusLog.Err("[Continuar] la ruta no tiene waypoints");
            return;
        }
        int cnt = m_Config.Waypoints.Count();
        if (wpIndex < 0) wpIndex = cnt - 1;   // negativo = endpoint (sentinel del boton CONTINUAR viejo)
        if (wpIndex >= cnt) wpIndex = cnt - 1;
        vector spawnPos = m_Config.Waypoints[wpIndex].GetVector();
        float surfY = GetGame().SurfaceY(spawnPos[0], spawnPos[2]);
        spawnPos[1] = surfY + 0.5;
        // heading: el grabado (targetHeading); si 0, se deriva de la direccion de la ruta en ese punto (segmento
        // wp->wp+1; en el ultimo wp usa el anterior->este).
        float heading = m_Config.Waypoints[wpIndex].targetHeading;
        if (heading == 0) {
            int a = wpIndex;
            int b = wpIndex + 1;
            if (b >= cnt) { a = wpIndex - 1; b = wpIndex; }
            if (a >= 0 && b < cnt && a != b) {
                vector d = m_Config.Waypoints[b].GetVector() - m_Config.Waypoints[a].GetVector();
                if (d[0] * d[0] + d[2] * d[2] > 0.01)
                    heading = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            }
        }
        string vc = m_Config.VehicleClass;
        if (m_VehicleClassOverride != "") vc = m_VehicleClassOverride;
        bool initAI = (vc.Contains("Bus") || vc.Contains("bus"));
        EntityAI veh = EntityAI.Cast(GetGame().CreateObject(vc, spawnPos, false, initAI));
        if (!veh) {
            BZBusLog.Err("[Continuar] no se pudo crear el vehiculo: " + vc);
            return;
        }
        veh.SetOrientation(Vector(heading, 0, 0));
        EquipBus(veh);   // attachments del hdr (ruedas/bateria/etc) + fluidos + energiza bateria -> manejable
        BZBusLog.Info("[Continuar] " + vc + " spawneado en wp" + wpIndex + "/" + (cnt - 1) + " pos=" + spawnPos.ToString() + " heading=" + heading + " (con attachments)");
        // Fase 2: NO teleportamos al player. Registramos el vacio -> aparece en el panel ACTIVE SPAWN VEHICLE
        // con TP + ELIMINAR. Asi se pueden dejar VARIOS vehiculos vacios sembrados en la ruta sin saltar de lugar.
        BZBusService.RegisterEmpty(veh, m_RouteName, wpIndex, spawnPos);
        if (player && player.GetIdentity()) {
            ScriptRPC trpc = new ScriptRPC();
            trpc.Write("Vehiculo vacio spawneado -> panel ACTIVE SPAWN VEHICLE (TP para ir)");
            trpc.Send(player, BZBusRPC.RECEIVE_TOAST, true, player.GetIdentity());
        }
    }

    private void SpawnBus() {
        m_WaypointIndex = 0;
        m_BoardPhase    = 0;   // reset boarding animado (BoardDriver lo arranca en fase 1)
        m_CrewBoard     = null; // reset boarding del crew (BoardDriver lo reconstruye)
        m_AppliedEvents = new array<int>();   // re-dispara los marker events en cada corrida
        m_Frozen        = false;
        m_VehicleInvincible = true;
        if (m_Config) m_VehicleInvincible = m_Config.VehicleInvincible;
        m_MissionStartTime = GetGame().GetTickTime();
        m_RouteStopped = false;
        m_DriverInvincible = true;
        if (m_Config) m_DriverInvincible = m_Config.DriverInvincible;
        m_ScenarioVars = new map<string, string>();
        m_NextStopIndex = FindNextStopIndex(0);
        m_AtStop        = false;
        m_StopDecided   = false;
        m_Reverse       = false;
        m_StuckTimer    = 0;
        m_DR_PrevWpIdx  = -1;
        m_DR_NoAdvanceTimer = 0;
        m_DR_InRecovery = false;
        m_SpawnHoldActive = true; // 2026-06-09: handbrake on hasta que Boris arranque
        m_SpawnHoldTime   = GetGame().GetTickTime();
        m_EndHoldActive   = false; // 2026-06-27: limpiar end-hold de la corrida anterior
        m_EndHoldTime     = 0;
        m_LightsOn        = false; // 2026-06-27: luces apagadas al spawn
        m_LastHornState   = 0;     // 2026-06-27: bocina off al spawn (replay espacial)
        m_BattTopUpAccum  = 0;     // 2026-06-27: reset acumulador top-up bateria
        m_LightsHeartbeatAccum = 0;// 2026-06-27: reset acumulador heartbeat luces
        m_OffPath_InRecovery = false;
        m_AR_Count = 0;
        m_AR_LastTime = 0;
        m_AR_LastWpIdx = -1;
        m_AR_LastWpProgressTime = GetGame().GetTickTime();
        m_DesiredGear   = 2; // CarGear.FIRST
        m_FrameReplayStartTime = 0; // 2026-07-05: el reloj del replay temporal arranca al 1er tick de PLAY
        m_FrameReplayElapsed   = 0; // tick-lock: acumulador de tiempo-motor
        m_ILCSaved             = false; // ILC: nueva pasada, aun no guardamos
        m_FrameReplayDone      = false; // fin de stream: nueva pasada, aun no procesado
        m_EndgameTime          = 0;     // endgame de precision: reset del timeout de creep
        m_EndgameLocked        = false; // endgame: nueva pasada, aun no clavamos el endpoint
        m_RevLatIntegral = 0; // reset I-term anti-drift de reverse
        m_ApproachActive = false; // reset rampa modo aproximacion
        m_ObstacleDist = -1; m_ObstacleScanTime = 0; // reset scan AR_OnWay
        m_BZHitTime = 0; m_ObstacleStuckSince = 0; m_ObstacleSeenTime = 0; m_ObstaclePushSince = 0;   // reset escape AR_OnWay
        m_SpawnAttempt++; // contador para auto-retry, RespawnBus lo resetea a 0 antes
        // Reset cached input para evitar residuos de la sesion anterior
        m_SurfPitchSm     = 0;
        m_EndpointLatched = false;
        m_EndpointMinDist = 9999;
        // RESET DEL TRAMO LATCHEADO Y DE LA CALIBRACION (2026-07-21, MEDIDO). Sin esto, al cargar OTRA
        // ruta se conservaban los limites del tramo de la corrida anterior: en ESQ2 Boris spawneo en el
        // wp0 pero su indice decia wp126 -> el AR vio "sin progreso" y lo teleporto 129 m adelante,
        // saltandose el tramo entero y las maniobras. La calibracion tambien arrastraba el vehiculo viejo
        // ("midiendo zona muerta de M3_G80_Police" durante la corrida del Sedan).
        m_LegInit    = false;
        m_LegStart   = 0;
        m_LegEnd     = 0;
        m_CalState   = 0;
        m_CalThr     = 0;
        m_CalGot     = 0;
        m_RevAlignTicks = 0;
        m_PoseGateActive = false;
        m_PoseGateTicks  = 0;
        m_StopAheadDist  = -1.0;
        m_CachedThrottle = 0;
        m_CachedSteering = 0;
        m_LegLaunch = false;   // el spawn ya alinea por OrientBusToNext; lo prende SetLegFrom
        m_CachedBrake    = 1.0; // pre-roll: brake aplicado desde el primer frame

        // Entrar al input SPAWN. La transicion a PLAY ocurre en Tick cuando
        // pase SpawnHoldSeconds. Durante SPAWN: brake aplicado, bus quieto.
        SetInput(BZBusInput.SPAWN, "SpawnBus inicio");

        // ILC: cargar correcciones pre-calculadas del routeId actual. Si no
        // existe el JSON (primera corrida o reset), las correcciones quedan
        // en 0 y el comportamiento es identico al baseline sin ILC.
        BZILCCorrections.GetInstance().LoadFromDisk("BUS_CANONICO");
        // LEARNER in-engine del corredor (gancho 1/4): mide el desvio SISTEMATICO de Boris vuelta a
        // vuelta y pre-distorsiona la referencia lateral para que trace la linea exacta. Init idempotente.
        if (m_Config.CorridorLearnerEnabled) BZCorridorLearner.GetInstance().Init(m_Config.Waypoints.Count(), "BUS_CANONICO", m_Config.Waypoints);
        // STOP-LEARNER in-engine (gancho 1/3): aprende la distancia de frenado REAL de Boris por parada
        // (feedforward calibrado) para que pare EXACTO en el checkpoint -> re-ancla el estado, corta la acumulacion.
        if (m_Config.StopLearnerEnabled) BZStopLearner.GetInstance().Init(m_Config.Waypoints.Count(), "BUS_CANONICO");
        // ENVELOPE del vehiculo (gancho): extrae el understeer(v) REAL de la toma (volante grabado vs
        // curvatura de la linea) y lo persiste per-vehiculo. Boris computa el volante justo para SU
        // velocidad -> generaliza, no replaya el volante. Replace idempotente (misma toma = mismo mapa).
        BZVehicleEnvelope.Get(m_Config.VehicleClass).UpdateFromWaypoints(m_Config.Waypoints, GetWheelbase());
        // SURFACE SCAN (prototipo extractor de grafo vial desde el mapa, 2026-07-06): barre un segmento
        // chico alrededor del spawn (la interseccion) dumpeando el tipo de superficie -> offline: mancha de
        // ruta -> grafo. One-shot. 120x120m paso 2m = ~3700 pts. [[project_demonstrated_road_graph]]
        vector scanCenter = m_Config.Waypoints[0].GetVector();
        if (m_Config.ScanCenterX != 0 && m_Config.ScanCenterZ != 0) scanCenter = Vector(m_Config.ScanCenterX, 0, m_Config.ScanCenterZ);
        float scanHalf = m_Config.ScanHalf; if (scanHalf <= 0) scanHalf = 60.0;
        float scanStep = m_Config.ScanStep; if (scanStep <= 0) scanStep = 2.0;
        BZSurfaceScan.ScanOnce(scanCenter, scanHalf, scanStep);
        // ROAD SCAN (sonda de objetos de carretera, 2026-07-07): reusa scanCenter/scanHalf. Vuelca
        // clase+pos+yaw+bbox de objetos road-like + censo de TODAS las clases. Gate por RoadScanEnabled.
        if (m_Config.RoadScanEnabled) {
            float rStep = m_Config.RoadScanStep;   if (rStep <= 0) rStep = 8.0;
            float rRad  = m_Config.RoadScanRadius; if (rRad  <= 0) rRad  = 6.0;
            if (m_Config.RoadScanFullMap) {
                // MAPA-COMPLETO frame-spread: barre todo el mapa filtrando road parts (roadscan_map.csv)
                float mStep = m_Config.RoadScanStep;   if (mStep <= 0) mStep = 40.0;
                float mRad  = m_Config.RoadScanRadius; if (mRad  <= 0) mRad  = 30.0;
                BZRoadScan.StartMapScan(m_Config.RoadScanMapSize, mStep, mRad);
                BZObjectScanAll.Begin(0, 0, 0);   // + objetos/estructuras de TODO el mapa (roadscan_objects_full.csv)
            } else {
                BZRoadScan.ScanOnce(scanCenter, scanHalf, rStep, rRad);
            }
        }
        // ROADWAY EXTRACT (2026-07-10): lee la capa ROADWAY REAL del motor (FindPath + GetSurface) a lo
        // largo de la ruta cargada -> roadway_path.csv + roadway_edges.csv. Gate por RoadwayExtractEnabled.
        if (m_Config.RoadwayExtractEnabled && m_Config.Waypoints && m_Config.Waypoints.Count() > 1) {
            vector rwFrom = m_Config.Waypoints[0].GetVector();
            vector rwTo   = m_Config.Waypoints[m_Config.Waypoints.Count() - 1].GetVector();
            BZRoadwayExtract.Extract(rwFrom, rwTo, m_Config.Waypoints);
        }

        // SAMPLE TERRAIN Y (2026-07-12, ultimo ladrillo modo-libre): rutas dibujadas de cero en el editor vienen
        // con Y=0 (el fondo vial es 2D). Con SampleTerrainY ON, sampleamos la altura REAL del terreno en CADA
        // wp (SurfaceY) -> frenado por pendiente y slope-aware correctos. El editor no puede (no tiene terreno);
        // aca si (el mundo esta cargado). El spawn ya se corrige aparte con surfY.
        if (m_Config.SampleTerrainY && m_Config.Waypoints) {
            for (int syi = 0; syi < m_Config.Waypoints.Count(); syi++) {
                vector wpvSy = m_Config.Waypoints[syi].GetVector();
                m_Config.Waypoints[syi].pos[1] = GetGame().SurfaceY(wpvSy[0], wpvSy[2]);
            }
            BZBusLog.Info("[TERRAIN-Y] altura del terreno sampleada en " + m_Config.Waypoints.Count() + " waypoints");
        }

        // m_PreRollEndTime sigue usandose como respaldo. El hold viene del JSON.
        float holdSec = m_Config.SpawnHoldSeconds;
        if (holdSec < 0) holdSec = SPAWN_PREROLL_SECONDS;
        m_PreRollEndTime = GetGame().GetTickTime() + holdSec;

        vector startPos = m_Config.Waypoints[0].GetVector();
        // Asegurar Y sobre el suelo (algunos vehiculos spawnean hundidos o flotando)
        float surfY = GetGame().SurfaceY(startPos[0], startPos[2]);
        startPos[1] = surfY + 0.5;
        m_SpawnInitialPos = startPos; // guardado para ValidateSpawn

        string vc = m_Config.VehicleClass;
        if (m_VehicleClassOverride != "") vc = m_VehicleClassOverride;
        // === WIZARD DUMP: leer config del vehiculo y loguear. Experimento
        // 2026-06-02 para validar que ConfigGet expone las propiedades fisicas
        // declaradas en el PBO (torqueCurve, gearbox, brake pressureBySpeed,
        // steering tables, etc). Si todo se lee, el wizard puede derivar
        // hyperparams del controller sin tests dinamicos.
        DumpVehicleConfig(vc);
        // initAI=true solo para vehiculos AI (ExpansionBus, etc). Vanilla cars => false.
        bool initAI = (vc.Contains("Bus") || vc.Contains("bus"));
        m_Bus = EntityAI.Cast(GetGame().CreateObject(vc, startPos, false, initAI));
        if (!m_Bus) {
            BZBusLog.Err("No se pudo crear vehiculo: " + vc);
            return;
        }
        BZBusLog.Info("SpawnBus: surfY=" + surfY + " startPos=" + startPos.ToString());
        BZBusLog.Info("CarGear constants: NEUTRAL=" + CarGear.NEUTRAL + " REVERSE=" + CarGear.REVERSE + " FIRST=" + CarGear.FIRST + " SECOND=" + CarGear.SECOND);
        DumpRuntimeProperties(vc);

        // LANE PROBE (2026-07-10): en rutas ESTATICAS (FollowPath=false, ej. test de carriles del grafo)
        // orientar el vehiculo a lo largo del camino (wp0->wp1) al spawnear, para que durante el SpawnHold
        // quede mirando la calle y se pueda juzgar el centrado. Gateado a !FollowPath -> rutas normales
        // (bus) intactas: ahi el snap-to-path orienta al arrancar como siempre.
        if (!m_Config.FollowPath && m_Config.Waypoints && m_Config.Waypoints.Count() > 1) {
            vector d0Spawn = m_Config.Waypoints[1].GetVector() - startPos;
            float d0LenSq = d0Spawn[0]*d0Spawn[0] + d0Spawn[2]*d0Spawn[2];
            if (d0LenSq > 0.01) {
                float hd0Spawn = Math.Atan2(d0Spawn[0], d0Spawn[2]) * Math.RAD2DEG;
                m_Bus.SetOrientation(Vector(hd0Spawn, 0, 0));
                BZBusLog.Info("[LaneProbe] orient estatico a heading " + hd0Spawn);
            }
        }

        // AUTO steering gain (2026-07-13): escala PurePursuitGain por la relacion de angulos de direccion
        // del vehiculo. Formula ANCLADA en el Sedan (30Ã‚Â°): gain = base * (RefAngle / steerAngle). Para el
        // Sedan queda igual (30/30=1); un auto que dobla mas baja el gain -> no sobre-dobla. Config->param,
        // generaliza a cualquier vehiculo desde SU config (steerAngle se lee del config, sin grabar). Opt-in.
        if (m_Config && m_Config.AutoSteerGainFromConfig && m_Config.PurePursuitGain > 0 && m_Config.SteerGainRefAngle > 1.0) {
            float steerAngVc = GetGame().ConfigGetFloat("CfgVehicles " + vc + " SimulationModule Steering maxSteeringAngle");
            if (steerAngVc > 1.0) {
                float gOldSg = m_Config.PurePursuitGain;
                m_Config.PurePursuitGain = gOldSg * (m_Config.SteerGainRefAngle / steerAngVc);
                BZBusLog.Info("[AutoSteerGain] steerAngle=" + steerAngVc + " deg -> PurePursuitGain " + gOldSg + " -> " + m_Config.PurePursuitGain);
            }
        }

        if (m_Config && m_Config.SpeedSourceNearest)
            BZBusLog.Info("[SpeedSourceNearest] ON: el target de velocidad se lee del wp mas cercano a Boris (no del indice, que corre ~15m adelante)");

        // CAPA 3+4 INVERSE MODEL: inicializar si esta habilitado en config
        if (m_Config && m_Config.UseInverseModel) {
            Car carForIM = Car.Cast(m_Bus);
            if (carForIM) {
                m_InverseModel = new BZInverseModel();
                m_InverseModel.LoadFromConfig(vc, carForIM);
                m_InverseModel.SetPIDGains(m_Config.InverseModelKp, m_Config.InverseModelKi, m_Config.InverseModelKd);
                if (m_Config.InverseModelLowRpmMin) {
                    m_InverseModel.SetRPMMinMultiplier(1.0);
                    BZBusLog.Info("[InverseModel] RPM min multiplier = 1.0 (gear amortiguado, gears altos en cruise)");
                } else {
                    m_InverseModel.SetRPMMinMultiplier(1.3);
                    BZBusLog.Info("[InverseModel] RPM min multiplier = 1.3 (default conservador)");
                }
                m_InverseModel.ResetPID();
                m_LastInverseModelLog = 0;
                BZBusLog.Info("[InverseModel] ACTIVATED for " + vc + " Ã¢â‚¬â€ Capa 3+4 framework v2 controlling throttle/brake");
                // AUTO-MaxGear (2026-07-13): deriva MaxGear del CONFIG del vehiculo (tope por gear:
                // ratios+redline+radio) + la vel maxima de la ruta -> el gear mas bajo que la alcanza.
                // Generaliza a cualquier vehiculo (modded: Nissan hace 82 en 1ra -> MaxGear=2) y a
                // rutas DIBUJADAS (sin datos de gear; el vehiculo aporta el dato). Opt-in por config.
                if (m_Config.AutoMaxGearFromConfig && m_Config.Waypoints) {
                    float routeMaxKmh = 0;
                    int nwpMg = m_Config.Waypoints.Count();
                    for (int wmg = 0; wmg < nwpMg; wmg++) {
                        if (m_Config.Waypoints[wmg].targetSpeed > routeMaxKmh) routeMaxKmh = m_Config.Waypoints[wmg].targetSpeed;
                    }
                    int autoMg = m_InverseModel.MaxGearForRouteSpeed(routeMaxKmh);
                    if (autoMg > 0) {
                        m_Config.MaxGear = autoMg;
                        BZBusLog.Info("[AutoMaxGear] routeMax=" + routeMaxKmh + " km/h -> MaxGear=" + autoMg + " (gear DayZ, 2=1ra)");
                    }
                }
                // TOPE POR VEHÃƒÂCULO: re-capa la velocidad objetivo de cada wp a la top-speed FÃƒÂSICA del auto
                // (redline en marcha alta). El max lo pone el vehÃƒÂ­culo, no un nÃƒÂºmero fijo. Gateado.
                if (m_Config.FollowPathMaxFromVehicle && m_Config.Waypoints) {
                    float vehTopKmh = m_InverseModel.GetTopSpeedKmh();
                    if (vehTopKmh > 1.0) {
                        int recapN = 0;
                        for (int vti = 0; vti < m_Config.Waypoints.Count(); vti++) {
                            if (m_Config.Waypoints[vti].targetSpeed > vehTopKmh) {
                                m_Config.Waypoints[vti].targetSpeed = vehTopKmh;
                                recapN++;
                            }
                        }
                        BZBusLog.Info("[VehTopSpeed] tope fÃƒÂ­sico del vehÃƒÂ­culo = " + vehTopKmh + " km/h (recapeados " + recapN + " wps)");
                    }
                }
            }
        } else {
            m_InverseModel = null;
        }

        // Activar POSTSIMULATE para que EOnPostSimulate (auto transmission) se dispare
        m_Bus.SetEventMask(EntityEvent.POSTSIMULATE);

        // Bus invulnerable durante el servicio (no se debe romper en la ruta)
        m_Bus.SetAllowDamage(false);

        // Equipar el bus con ruedas/bateria/etc (algunos vehiculos no traen attachments default)
        EquipBus(m_Bus);

        // Orientar el bus hacia el siguiente waypoint para que arranque mirando la ruta
        OrientBusToNext();

        // Spawnear el driver AFUERA, en el punto de entrada de la puerta del conductor
        // (CrewEntryWS) + 2m hacia afuera para que quede DESPEJADO del cuerpo. ANTES spawneaba
        // en el CENTRO del vehiculo (default, autos/camiones) o 2m al frente (solo buses) -> quedaba
        // DENTRO del cuerpo -> la fisica lo eyectaba 10m o lo dejaba bajo el camion cuerpo a tierra
        // (validado 2026-06-14, V3S no contiene "Bus" -> caia en el default roto). Desde aca el
        // boarding animado camina lo corto hasta la puerta y sube.
        vector busPosSpawn = m_Bus.GetPosition();
        vector driverPos = busPosSpawn;
        Transport transportSpawn = Transport.Cast(m_Bus);
        if (transportSpawn) {
            vector entryPos; vector entryDir;
            transportSpawn.CrewEntryWS(0, entryPos, entryDir);
            float odx = entryPos[0] - busPosSpawn[0];
            float odz = entryPos[2] - busPosSpawn[2];
            float ol = Math.Sqrt(odx * odx + odz * odz);
            if (ol < 0.1) { ol = 1.0; odx = 1.0; odz = 0.0; }
            // PLAN B (2026-06-28): el eAI NO produce locomocion para este bot (lider pacificado): aiSpeed=0
            // CLAVADO incluso forzando OverrideMovementSpeed(true,2) (builds 1BCF3758/D4C578F0). Se descarta
            // el tramo CAMINADO: Boris spawnea AL LADO de la puerta (~1.0m hacia afuera del door entry, al
            // piso), DENTRO del radio de llegada de StepBoarding (d2<=4.0 = 2m). Asi el PRIMER tick pega la
            // rama de llegada -> abre puerta + Notify_Transport (entrada ANIMADA) -> entra -> EnsureDriverAnim
            // (brazos al volante) -> maneja. Aparece "al lado del auto" sin clipear el cuerpo y sin idle de 6.5s.
            float dpx = entryPos[0] + (odx / ol) * 1.0;
            float dpz = entryPos[2] + (odz / ol) * 1.0;
            driverPos = Vector(dpx, GetGame().SurfaceY(dpx, dpz), dpz);   // al PISO, no a la altura de la puerta
            BZBusLog.Info("[Boarding] spawn driver al lado de la puerta (~1m, sin tramo caminado): entry=" + entryPos + " driverPos=" + driverPos + " (bus=" + busPosSpawn + ")");
        }
        m_Driver = eAIBase.Cast(GetGame().CreateObject(m_Config.DriverClass, driverPos, false, true));
        if (!m_Driver) {
            BZBusLog.Err("No se pudo crear conductor: " + m_Config.DriverClass);
            m_Bus.Delete(); m_Bus = null;
            return;
        }

        // Driver invulnerable durante el servicio: si lo matan, el bus queda inerte
        // y la ruta se rompe hasta el proximo respawn. Mismo criterio que el bus.
        m_Driver.SetAllowDamage(false);

        m_Group = eAIGroup.CreateGroup(eAIFaction.Create("Passive"));
        m_Driver.SetGroup(m_Group);

        // CAUSA RAIZ del "Boris idle, no camina los 3m, timeout->teleport" (2026-06-28):
        // eAIState_TraversingWaypoints.Guard() ABORTA (FAIL -> Idle) si GetThreatToSelf() >= 0.4.
        // El chofer NO se seteaba passive (a diferencia del VIP/quest bots, que SI caminan), asi que
        // en un mundo vivo (zombies/players cerca, o self-threat al spawn) su amenaza sube >=0.4 ->
        // sale del seguimiento de waypoints -> se queda tieso hasta el timeout. El crew funcionaba
        // porque esos bots SI estan passive (lineas ~933/1053). Pacificamos a Boris IGUAL: passive +
        // sin threat-distance + limpiar targets -> el guard de waypoints pasa y CAMINA a la puerta.
        m_Driver.eAI_SetPassive(true);
        m_Driver.eAI_SetThreatDistanceLimit(0.0);
        for (int dt = 0; dt < 16; dt++) {
            eAITarget dtg = m_Driver.GetTarget(0);
            if (!dtg) break;
            m_Driver.eAI_RemoveTarget(dtg);
        }

        // ACTIVAR el FSM de Boris al spawn. Sin una tarea el bot eAI queda "maniqui" (Idle = velocidad 0,
        // sin animacion) y el waypoint de boarding (1s despues) no lo despierta. Insight de Sonom4n 2026-06-15:
        // "spawnean tiesos porque no tienen que hacer; ganan movimiento al subirse". Le damos un roam ->
        // FSM vivo -> responde al waypoint de la puerta en BoardDriver.
        m_Group.SetFormationState(eAIGroupFormationState.IN);
        m_Group.AddWaypoint(driverPos);
        m_Group.SetWaypointBehaviour(eAIWaypointBehavior.ROAMING_LOCAL);
        m_Driver.SetMovementSpeedLimits(1, 2);
        if (m_Driver.GetPathFinding()) m_Driver.GetPathFinding().ForceRecalculate(true);

        // Vestir a Boris de policia
        DressDriver(m_Driver);

        // Crew: bots que viajan con Boris desde el arranque. Se crean aca (afuera del
        // cuerpo, por offset configurable, invencibles), se SIENTAN en BoardDriver.
        SpawnCrewBots();

        SetNextWaypoint();

        // Diferir el embarque: el motor necesita 1s para inicializar bus+driver
        // antes de aceptar CrewGetIn. Hacerlo inmediato suele fallar.
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.BoardDriver, 1000, false);
        // Tick a 500ms (2Hz, VALIDADO): 150ms (6.7Hz) sobre-corrigio (gains de 2Hz aplicados 3.3x/seg ->
        // zigzag + curvas anchas, ai_run 2026-06-20). Volvimos a 500 para aislar: control validado, sin
        // oscilacion. El tema curva se ataca aparte (de-noise del jitter del recording + cap por GRIP del
        // vehiculo via fingerprint, no subiendo el rate). Si se quiere rate alto -> re-tunear Stanley K primero.
        SetLogTag();           // tag de este runner en los logs (= nombre de la ruta)
        m_TickCount = 0;
        AutoSteeringScale();   // auto-deriva SteeringScale del wheelbase si esta en modo auto (-1)
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(Tick, 500, true);
        // LOOP DE DIRECCION RAPIDO a 50ms (20Hz): DESACTIVADO (2026-07-10). Al recomputar SOLO el pure-pursuit
        // crudo salteaba la pila de correcciones que el Tick aplica DESPUES (ILC, curvature-boost, yaw-corr) y
        // NO tenia rate-limit -> volante clavado a full-lock oscilando ~2Hz (zigzag violento, |steer|med 0.74,
        // 56 reversiones/100m, offset a traza 3.8m med / 15.9m max en la corrida 190710-221419). LECCION: el
        // Tick de 500ms no era el cuello, era el que enmascaraba una ganancia caliente (actuaba de rate-limiter
        // implicito). La anticipacion de 5-6m era el Ld LARGO, no el rate -> se ataca con el Ld adaptativo, no
        // con un loop rapido. Para re-activarlo hay que: (1) aplicar la MISMA pila de correcciones, (2) rate-
        // limiter explicito del delta por tick (~0.2/tick = ~4/s, = el slew que el 500ms permitia). Metodo
        // FastSteerTick + flag m_FastSteerActive quedan por si se retoma con esas dos piezas.
        // GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(FastSteerTick, 50, true);
        // MICROTICKS DE CENTRADO (2026-07-12): re-usa el fast tick 50ms pero SOLO para el corrector de centrado
        // pulsado (NO el pursuit crudo que fallo). El propio FastSteerTick rutea a FastCenterMicroTick cuando el
        // flag esta ON. Se remueve en el cleanup (Remove(FastSteerTick) ya existe).
        if (m_Config.CenterMicroTickEnabled) {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(FastSteerTick, 50, true);
        }

        // Auto-retry: chequear a los 5s si el bus se movio. Si no, asumir
        // que Boris no se sento y reintentar (hasta MAX_SPAWN_RETRIES).
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.ValidateSpawn, SPAWN_VALIDATION_DELAY_MS, false);

    }

    // Chequea 5s post-spawn si el bus se moviÃƒÂ³. Si no se moviÃƒÂ³ (driver no
    // boardeo, o trabado en colisiÃƒÂ³n, o similar), reintenta el spawn.
    void ValidateSpawn() {
        if (!m_Bus) {
            BZBusLog.Warn("ValidateSpawn: m_Bus es null, validacion cancelada");
            return;
        }

        // Boarding animado en progreso: Boris esta caminando/subiendo, NO es spawn fallido.
        // No reintentar (eso causaba el "falso 2"); re-chequear en otra ventana.
        if (m_BoardPhase >= 1 && m_BoardPhase < 3) {
            BZBusLog.Info("ValidateSpawn: boarding animado en progreso (fase " + m_BoardPhase + "), no retry, re-chequeo en " + SPAWN_VALIDATION_DELAY_MS + "ms");
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.ValidateSpawn, SPAWN_VALIDATION_DELAY_MS, false);
            return;
        }

        // Si el bus esta en parada (m_AtStop), NO es spawn fallido Ã¢â‚¬â€ es el bus
        // dentro de la zona de un wp con isStop=true. Reset retry y exit.
        if (m_AtStop) {
            BZBusLog.Info("ValidateSpawn OK: bus en parada (m_AtStop=true), spawn exitoso, no retry");
            m_SpawnAttempt = 0;
            return;
        }

        // Si un EVENTO congelo/paro el bus (freeze_vehicle / stop_route), NO es spawn
        // fallido Ã¢â‚¬â€ esta detenido a proposito. Reintentar aca causaba un loop infinito
        // (respawn -> freeze -> respawn...) cuando la proximidad disparaba en el spawn.
        if (m_Frozen || m_RouteStopped) {
            BZBusLog.Info("ValidateSpawn OK: bus detenido por evento (frozen/route_stopped), no retry");
            m_SpawnAttempt = 0;
            return;
        }

        // Si el bus esta en movimiento (kmh > 0.5), el driver se sento, el motor
        // respondio y la cadena del control funciona. Es spawn exitoso aunque
        // no haya alcanzado 2m fisicos. Pasa cuando el operador grabo los
        // primeros samples quieto en la terminal: los primeros wps tienen
        // targetSpeed=0, la cruise control aplica throttle bajo, el bus arranca
        // despacio. La validacion por distancia falla pero el spawn es valido.
        Car bus_v = Car.Cast(m_Bus);
        if (bus_v && bus_v.GetSpeedometerAbsolute() > 0.5) {
            BZBusLog.Info("ValidateSpawn OK: bus en movimiento (kmh=" + bus_v.GetSpeedometerAbsolute() + "), spawn exitoso, no retry");
            m_SpawnAttempt = 0;
            return;
        }

        // FIX "falso 2" (2026-06-14): si Boris YA ESTA SENTADO en el asiento del conductor,
        // el spawn estructuralmente funciono -> va a arrancar. A veces a los 5s todavia no se
        // movio (boarding + encendido tardan) y la validacion por distancia daba falso negativo
        // -> respawneaba al inicio (el "falso 2" que reseteaba la corrida a mitad de ruta). El
        // failure real que guarda ValidateSpawn es "el driver NUNCA se sento" -> si esta sentado,
        // no hay nada que reintentar. (Validado RPT 2026-06-14: "no se movio 0.51m" -> intento 2,
        // y en el intento 2 arranca normal = boarding lento, no spawn fallido.)
        Car bus_seat = Car.Cast(m_Bus);
        if (bus_seat && m_Driver && bus_seat.CrewMember(0) == m_Driver) {
            BZBusLog.Info("ValidateSpawn OK: Boris sentado en seat 0 (boarding completo), spawn exitoso, no retry");
            m_SpawnAttempt = 0;
            return;
        }

        float distFromSpawn = vector.Distance(m_Bus.GetPosition(), m_SpawnInitialPos);
        if (distFromSpawn >= SPAWN_VALIDATION_MIN_DIST) {
            BZBusLog.Info("ValidateSpawn OK: bus se movio " + distFromSpawn + "m desde spawn en intento #" + m_SpawnAttempt);
            m_SpawnAttempt = 0; // reset, todo bien
            return;
        }

        // No se moviÃƒÂ³. Si todavÃƒÂ­a hay retries disponibles, reintentar.
        if (m_SpawnAttempt >= MAX_SPAWN_RETRIES) {
            BZBusLog.Err("ValidateSpawn: agote " + MAX_SPAWN_RETRIES + " intentos, bus no arranca. Abortando auto-retry.");
            BroadcastGlobal("BZ AutoDrive: spawn failed after " + MAX_SPAWN_RETRIES + " attempts.");
            m_SpawnAttempt = 0; // reset por si hace falta intentarlo manualmente despues
            return;
        }

        BZBusLog.Warn("ValidateSpawn: bus no se movio (" + distFromSpawn + "m), intento #" + m_SpawnAttempt + " de " + MAX_SPAWN_RETRIES + ". Reintentando...");
        CleanupEntities();
        SpawnBus(); // SpawnBus incrementa m_SpawnAttempt
    }

    // === BOARDING ANIMADO DESDE AFUERA (2026-06-14) ===
    // Boris camina hasta la puerta del conductor, la abre, sube ANIMADO y la cierra,
    // en vez de teleportarse a la butaca. Mas robusto (mata el "falso 2": el spawn ya no
    // depende de meterlo en el asiento junto con el vehiculo) y se ve mejor (puerta animada).
    // Replica la secuencia de la FSM de eAI (GoToVehicle->OpenDoor->GetIn->CloseDoor) porque el
    // trigger nativo esta atado al patron lider-pasajero (no sirve para chofer/seat 0).
    // Primitivas: CrewEntryWS (pos puerta), OverrideTargetPosition (caminar), Notify_Transport
    // (subida animada), GetCommand_Vehicle/IsGettingIn (detectar adentro), SetAnimationPhase (puerta).
    // Fallback: si tarda > BOARD_TIMEOUT_S, teleport-seat (comportamiento viejo) -> nunca rompe.
    static const float BOARD_TIMEOUT_S = 6.0;
    // Clasificacion walk-vs-teleport (heuristica derivada de data real 2026-06-15: Offroad/Cobra/Vodnik).
    // Ver [[project_ai_vehicle_crew]] 3 arquetipos. CrewEntryWS por asiento -> decide la estrategia.
    static const float BOARD_DUP_EPS      = 0.5;   // 2 entradas a < esto = MISMA plaza (dup exacto, Vodnik) -> teleport
    static const float BOARD_CLUSTER_DIST = 1.6;   // entradas a < esto = comparten acceso -> walk SECUENCIAL (gate)
    static const float BOARD_ROOF_H       = 1.8;   // entrada por encima del piso del vehiculo + esto = techo -> teleport
    private int    m_BoardPhase;     // 0=sin boarding, 1=caminando, 2=subiendo, 3=sentado/done
    private float  m_BoardTimer;
    private bool   m_BoardHasDoor;
    private string m_BoardDoorSrc;

    void BoardDriver() {
        BZBusLog.Info("BoardDriver: iniciando boarding animado desde afuera");
        if (!m_Bus || !m_Driver) { BZBusLog.Warn("BoardDriver: bus o driver null"); return; }
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) { BZBusLog.Warn("BoardDriver: bus no es Transport"); return; }

        DumpSeatSystem(transport);   // diagnostico: volcar el seating real del vehiculo al RPT

        // Caminar hacia la PUERTA DELANTERA (conductor, seat 0) via WAYPOINT DE GRUPO.
        // eAIGroup.AddWaypoint es la API que mueve al bot DE VERDAD (OverrideTargetPosition
        // solo NO: la FSM lo pisa -> Boris se quedaba parado). Asi encuentra la puerta y camina
        // desde cualquier radio (clave para convoy/escuadra que monta desde lejos).
        vector doorPos; vector doorDir;
        transport.CrewEntryWS(0, doorPos, doorDir);

        // Anim source de la puerta del conductor (para abrir/cerrar).
        bool hd = false; string ds = "";
        ExpansionFSMHelper.DoorAnimationSource(m_Bus, 0, hd, ds);
        m_BoardHasDoor = hd;
        m_BoardDoorSrc = ds;

        // Clasificar la puerta del chofer. REGRESION 2026-06-28: la condicion vieja tenia
        // "!hd" (sin door anim source) como motivo de TELEPORT -> Boris se sentaba INSTANT
        // (gorro clipeando el techo) en cualquier vehiculo cuya puerta NO sea un attachment
        // separado (DoorAnimationSource devuelve false). Pero la subida ANIMADA (Notify_Transport)
        // NO requiere el door anim source: ese solo abre/cierra la hoja de la puerta (cosmetico,
        // ya gateado por if(m_BoardHasDoor) en StepBoarding). El chofer DEBE caminar igual.
        //
        // FIX: el chofer teleporta SOLO si la entrada es GEOMETRICAMENTE no-caminable
        // (techo: entrada > BOARD_ROOF_H sobre el piso del vehiculo; o garbage: dist<0.5 / dist>15).
        // "Sin puerta" deja de forzar teleport -> walk = default real del chofer. Si no hay door
        // anim, simplemente sube sin animar la hoja (Notify_Transport igual hace la anim de get-in).
        vector busP0 = m_Bus.GetPosition();
        float dDrv = vector.Distance(doorPos, busP0);
        bool drvTeleport = ((doorPos[1] - busP0[1] > BOARD_ROOF_H) || dDrv < 0.5 || dDrv > 15.0);
        if (drvTeleport) {
            m_Driver.SetPosition(busP0);
            m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
            m_Driver.Notify_Transport(transport, 0);
            EnsureDriverAnim();   // anim de conductor (volante/pedales/palanca) visible al observador
            m_BoardPhase = 3;
            BZBusLog.Info("[Boarding] chofer -> TELEPORT (hasDoor=" + hd + " dist=" + dDrv + ")");
        } else {
            // PLAN B: Boris spawnea AL LADO de la puerta (~1m, ver SpawnBus), DENTRO del radio de llegada.
            // No hay tramo caminado (el eAI no produce locomocion para este lider pacificado). El waypoint de
            // grupo de abajo queda INERTE (Boris ya esta "llegado" en el 1er tick de StepBoarding -> abre +
            // sube animado). Se deja por seguridad/compat (no molesta); el FormationState IN + waypoint no
            // estorban la rama de llegada.
            if (m_Group) {
                m_Group.ClearWaypoints();
                m_Group.SetFormationState(eAIGroupFormationState.IN);
                m_Group.AddWaypoint(BoardGroundWP(doorPos));
                m_Group.SetWaypointBehaviour(eAIWaypointBehavior.ONCE);
            }
            m_BoardPhase = 1;
            m_BoardTimer = 0;
            BZBusLog.Info("[Boarding] chofer -> al lado de la puerta (Plan B, sin caminata): doorPos=" + doorPos + " hasDoor=" + hd + " dist=" + dDrv);
        }

        // Crew (Stage 1) Ã¢â‚¬â€ boarding VEHICULO-AGNOSTICO con clasificacion walk-vs-teleport.
        // El vehiculo declara sus asientos (CrewEntryWS); NO adivinamos. Segun la disposicion REAL:
        //   - entrada duplicada exacta / en el techo / garbage -> TELEPORT (se sienta directo). Fix del crash 2.
        //   - entrada propia/separada o agrupada -> WALK (camina), serializada por el gate de proximidad
        //     (StepCrewBoarding): lejos = arrancan en paralelo, agrupadas = de a una (sin pile-up).
        // Ver [[project_ai_vehicle_crew]] 3 arquetipos (Offroad=paralelo, Cobra=secuencial, Vodnik=teleport).
        m_CrewBoard = new array<ref BZBoardState>();
        if (m_Crew && m_CrewWillBoard) {   // MILESTONE 1: false -> el crew patrulla y NO aborda (confirmar movimiento)
            vector busP = m_Bus.GetPosition();
            // 1) Recolectar bot/seat/entrada/puerta de cada crew (vehiculo estatico durante el boarding).
            for (int ci = 0; ci < m_Crew.Count(); ci++) {
                eAIBase cbot = m_Crew[ci];
                if (!cbot) continue;
                int cseat = 1;
                if (m_CrewSeats && ci < m_CrewSeats.Count()) cseat = m_CrewSeats[ci];
                vector cdoor; vector cdoorDir;
                transport.CrewEntryWS(cseat, cdoor, cdoorDir);
                bool chd = false; string cds = "";
                ExpansionFSMHelper.DoorAnimationSource(m_Bus, cseat, chd, cds);
                BZBoardState e = new BZBoardState();
                e.bot = cbot; e.seat = cseat; e.timer = 0; e.hasDoor = chd; e.doorSrc = cds; e.entry = cdoor;
                m_CrewBoard.Insert(e);
            }
            // 2) Clasificar cada plaza por su entrada (vs el resto del crew) y arrancar.
            for (int k = 0; k < m_CrewBoard.Count(); k++) {
                BZBoardState ek = m_CrewBoard[k];
                float dVeh = vector.Distance(ek.entry, busP);
                bool roof    = (ek.entry[1] - busP[1] > BOARD_ROOF_H);
                bool garbage = (dVeh < 0.5 || dVeh > 15.0);
                float nearest = 99999.0;
                for (int mm = 0; mm < m_CrewBoard.Count(); mm++) {
                    if (mm == k) continue;
                    float dd = vector.Distance(ek.entry, m_CrewBoard[mm].entry);
                    if (dd < nearest) nearest = dd;
                }
                bool dup = (nearest < BOARD_DUP_EPS);
                // !hasDoor = no hay puerta a la que caminar/abrir (hatch/techo/acceso compartido tipo
                // Cobra, Vodnik) -> caminar falla (no llega, 12s de timeout) -> teleport directo. La
                // caminata animada solo aplica con puerta PROPIA a nivel piso (auto 4 puertas, bus).
                ek.teleport = (!ek.hasDoor || roof || garbage || dup);
                if (ek.teleport) {
                    // No-walkable (sin puerta, Vodnik techo, dup): teleport-seat. IGUAL llena el vehiculo.
                    ek.bot.SetPosition(busP);
                    ek.bot.StartCommand_Vehicle(transport, ek.seat, 0, false);
                    ek.bot.Notify_Transport(transport, ek.seat);
                    ek.phase = 3;
                    BZBusLog.Info("[Boarding] crew seat " + ek.seat + " -> TELEPORT (noDoor=" + (!ek.hasDoor) + " roof=" + roof + " dup=" + dup + " garbage=" + garbage + ")");
                } else {
                    // Walkable: recipe de caminata DIRECTO, en PARALELO (sin gate -> entran en <6s). El bot
                    // ya esta "vivo" (activado al spawn) -> responde. Camina al piso de su puerta, abre y sube.
                    eAIGroup cgw = ek.bot.GetGroup();
                    if (cgw) {
                        ek.bot.SetMovementSpeedLimits(2, 3);
                        cgw.ClearWaypoints();
                        cgw.SetFormationState(eAIGroupFormationState.IN);
                        cgw.AddWaypoint(BoardGroundWP(ek.entry));
                        cgw.SetWaypointBehaviour(eAIWaypointBehavior.ONCE);
                        if (ek.bot.GetPathFinding()) ek.bot.GetPathFinding().ForceRecalculate(true);
                    }
                    ek.phase = 1;
                    BZBusLog.Info("[Boarding] crew seat " + ek.seat + " -> WALK directo (nearest=" + nearest + ")");
                }
            }
        }
    }

    // Progresa el boarding animado un paso. Lo llama Tick mientras m_BoardPhase esta en 1..2.
    private void StepBoarding() {
        if (!m_Bus || !m_Driver) { m_BoardPhase = 3; return; }
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) { m_BoardPhase = 3; return; }
        m_BoardTimer += 0.5;   // Tick corre a 500ms

        // Fallback: si el boarding animado se traba (pathfinding, etc.), volver al teleport-seat
        // viejo. Garantiza que Boris se sienta y el bus arranca pase lo que pase.
        if (m_BoardTimer > BOARD_TIMEOUT_S) {
            BZBusLog.Warn("StepBoarding: timeout (" + m_BoardTimer + "s), fallback a teleport-seat");
            m_Driver.SetPosition(m_Bus.GetPosition());
            // ABRIR la puerta durante el get-in del fallback (2026-06-28): antes se cerraba (0.0) de una,
            // asi la anim de subida se veia con la puerta CERRADA. La abrimos (1.0) para que el get-in se
            // vea bien, y la cerramos ~1.5s despues (cuando ya termino la subida) via CallLater.
            if (m_BoardHasDoor) m_Bus.SetAnimationPhase(m_BoardDoorSrc, 1.0);
            m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
            m_Driver.Notify_Transport(transport, 0);
            EnsureDriverAnim();   // anim de conductor (volante/pedales/palanca) visible al observador
            if (m_BoardHasDoor) GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CloseDriverDoor, 1500, false);
            m_BoardPhase = 3;
            return;
        }

        if (m_BoardPhase == 1) {
            // PLAN B: Boris spawnea AL LADO de la puerta (~1m, dentro del radio d2<=4.0). El eAI no produce
            // locomocion para este lider pacificado (aiSpeed=0 confirmado), asi que NO hay tramo caminado:
            // el PRIMER tick ya esta "llegado" -> abre puerta + sube ANIMADO. Sin idle, sin teleport.
            vector doorPos; vector doorDir;
            transport.CrewEntryWS(0, doorPos, doorDir);
            float d2 = BoardDistXZ(m_Driver.GetPosition(), doorPos);   // XZ: ignora la altura de la puerta
            if (d2 <= 4.0) {
                if (m_BoardHasDoor) m_Bus.SetAnimationPhase(m_BoardDoorSrc, 1.0);   // abrir
                m_Driver.LookAtDirection("0 0 1");
                m_Driver.Notify_Transport(transport, 0);   // subida ANIMADA
                m_BoardPhase = 2;
                BZBusLog.Info("[Boarding] al lado de la puerta (d2=" + d2 + "), abre + sube animado");
            }
            // (si por algun motivo no esta dentro del radio en este tick, el timeout-fallback lo sienta)
        } else if (m_BoardPhase == 2) {
            // Subiendo: cuando ya esta adentro del vehiculo, cerrar puerta y terminar.
            HumanCommandVehicle vc = m_Driver.GetCommand_Vehicle();
            if (vc && !vc.IsGettingIn()) {
                if (m_BoardHasDoor) m_Bus.SetAnimationPhase(m_BoardDoorSrc, 0.0);   // cerrar
                EnsureDriverAnim();   // bindear la anim de conductor (volante/pedales/palanca) al observador
                m_BoardPhase = 3;
                BZBusLog.Info("StepBoarding: Boris sentado (boarding animado completo)");
            }
        }
    }

    // Liga la ANIMACION DE CONDUCTOR (volante, pedales, palanca) al comando de vehiculo de Boris.
    //
    // CAUSA del "Boris tieso manejando tras el boarding ANIMADO" (2026-06-28): la anim de operario la
    // bindea HumanCommandVehicle.SetVehicleType(transport.GetAnimInstance()). El FSM de eAI lo hace en
    // su command-handler (eaibase.c ~7406-7408): StartCommand_Vehicle(seat, seat_anim_type) + SetVehicleType.
    // El teleport-seat viejo terminaba enganchando esa anim porque tras su StartCommand_Vehicle directo el
    // FSM corria 7404 y re-emitia con SetVehicleType. Por el path animado, segun como quede el comando, el
    // SetVehicleType podia NO aplicarse -> Boris sentado SIN anim de manejo (tieso) en el observador.
    //
    // FIX: replicamos EXACTAMENTE lo que hace el FSM, explicito, una vez sentado. Idempotente: si ya esta
    // en COMMANDID_VEHICLE, SetVehicleType re-liga la anim; si por algun motivo no tiene comando de vehiculo,
    // lo arrancamos con el seat_anim_type correcto (no 0). Asi los DOS paths (animado y teleport) terminan
    // en el MISMO estado final: sentado + anim de conductor activa y visible para el que mira.
    void EnsureDriverAnim() {
        if (!m_Bus || !m_Driver) return;
        Transport tr = Transport.Cast(m_Bus);
        if (!tr) return;
        int seatAnim = tr.GetSeatAnimationType(0);
        HumanCommandVehicle vcmd = m_Driver.GetCommand_Vehicle();
        if (!vcmd) vcmd = m_Driver.StartCommand_Vehicle(tr, 0, seatAnim, false);
        if (vcmd) vcmd.SetVehicleType(tr.GetAnimInstance());
    }

    // Cierra la puerta del chofer (wrapper sin-args para CallLater). Usado por el fallback de teleport-seat:
    // se abre la puerta para que el get-in se vea bien y se cierra ~1.5s despues (subida ya terminada).
    void CloseDriverDoor() {
        if (m_Bus && m_BoardHasDoor && m_BoardDoorSrc != "") m_Bus.SetAnimationPhase(m_BoardDoorSrc, 0.0);
    }

    // Progresa el boarding de CADA crew (paralelo al del chofer). Mismo patron: caminar -> abrir -> subir ->
    // cerrar. Cada crew usa SU propio grupo (ya lider) para navegar a su puerta. Timeout-fallback por bot.
    // DIAGNOSTICO: vuelca el sistema de asientos del vehiculo (cuantos, entrada, puerta) al RPT.
    // Para entender empiricamente como es el seating de cada vehiculo (ej V3S carga por atras).
    private void DumpSeatSystem(Transport transport) {
        if (!transport || !m_Bus) return;
        int cs = transport.CrewSize();
        vector busP = m_Bus.GetPosition();
        BZBusLog.Info("[SEATS] === " + m_Bus.GetType() + " CrewSize=" + cs + " ===");
        for (int s = 0; s < cs; s++) {
            vector ep; vector ed;
            transport.CrewEntryWS(s, ep, ed);
            float d = vector.Distance(ep, busP);
            bool hd = false; string ds = "";
            ExpansionFSMHelper.DoorAnimationSource(m_Bus, s, hd, ds);
            Human occ = transport.CrewMember(s);
            BZBusLog.Info("[SEATS] seat " + s + " entryWS=" + ep + " dist=" + d + " hasDoor=" + hd + " doorSrc=" + ds + " ocupado=" + (occ != null));
        }
    }

    // Proyecta un punto al PISO (Y = superficie). CLAVE: el pathfinder de eAI no rutea a puntos
    // ELEVADOS (la entrada de una puerta de blindado esta ~0.8m sobre el suelo) -> no encuentra
    // ruta -> el bot queda Idle = TIESO (no camina). El waypoint de caminata DEBE estar a ras del
    // suelo. En el V3S la entrada ya estaba a nivel piso (por eso Boris caminaba); en el Cobra no.
    private vector BoardGroundWP(vector p) {
        vector g = p;
        g[1] = GetGame().SurfaceY(p[0], p[2]);
        return g;
    }
    // Distancia HORIZONTAL (XZ) al cuadrado Ã¢â‚¬â€ para el trigger "llegue a la puerta". Ignora la
    // altura de la entrada (la puerta esta a la altura de la manija, no del piso): si midieramos
    // 3D, el offset vertical solo ya casi llena el umbral y el bot nunca "llega" -> timeout.
    private float BoardDistXZ(vector a, vector b) {
        float dx = a[0] - b[0]; float dz = a[2] - b[2];
        return dx * dx + dz * dz;
    }

    private void StepCrewBoarding() {
        if (!m_CrewBoard || !m_Bus) return;
        Transport transport = Transport.Cast(m_Bus);
        if (!transport) return;

        // SERIALIZACION ("juego de la silla", 2026-06-17): los bots en phase 0 (esperando turno) suben de a
        // UNO. Si nadie esta activo (ningun crew en phase 1/2, ni Boris boarding), promovemos el primer phase-0
        // a phase-1 -> recien ahi emite su waypoint y camina. Cuando ese se sienta (phase 3), larga al proximo.
        // Evita que 2 bots se choquen en una entrada compartida (x5: traseros = delanteros) -> sin contencion,
        // sin teleport-fallback (que hacia SetPosition encima del vehiculo -> overload). Los bots seteados a
        // phase 1 directo (escena 1 / flee) NO usan esto (no hay phase-0) -> siguen en paralelo, sin cambios.
        bool anyActive = (m_BoardPhase >= 1 && m_BoardPhase < 3);   // Boris boarding cuenta como activo
        for (int ai = 0; ai < m_CrewBoard.Count() && !anyActive; ai++) {
            if (m_CrewBoard[ai] && (m_CrewBoard[ai].phase == 1 || m_CrewBoard[ai].phase == 2)) anyActive = true;
        }
        if (!anyActive) {
            for (int pi = 0; pi < m_CrewBoard.Count(); pi++) {
                BZBoardState pe = m_CrewBoard[pi];
                if (pe && pe.phase == 0 && pe.bot) {
                    eAIGroup peg = pe.bot.GetGroup();
                    if (peg) {
                        pe.bot.SetMovementSpeedLimits(2, 3);
                        peg.ClearWaypoints();
                        peg.SetFormationState(eAIGroupFormationState.IN);
                        peg.AddWaypoint(BoardGroundWP(pe.entry));
                        peg.SetWaypointBehaviour(eAIWaypointBehavior.ONCE);
                        if (pe.bot.GetPathFinding()) pe.bot.GetPathFinding().ForceRecalculate(true);
                    }
                    pe.phase = 1;
                    pe.timer = 0; pe.lastDist = 0; pe.stuckTimer = 0;
                    BZBusLog.Info("[Boarding] turno seat " + pe.seat + " (juego de la silla, sube de a uno)");
                    break;   // SOLO uno por vez
                }
            }
        }

        for (int i = 0; i < m_CrewBoard.Count(); i++) {
            BZBoardState e = m_CrewBoard[i];
            if (!e || e.phase >= 3) continue;
            if (!e.bot) { e.phase = 3; continue; }
            // (Sin gate de fase-0: BoardDriver ya arranca la caminata DIRECTO en paralelo -> entran en <6s.)

            if (e.phase == 1) {
                vector cdoor; vector cdoorDir;
                transport.CrewEntryWS(e.seat, cdoor, cdoorDir);
                float cd2 = BoardDistXZ(e.bot.GetPosition(), cdoor);   // XZ: ignora la altura de la puerta
                if (cd2 <= 4.0) {
                    if (e.hasDoor) m_Bus.SetAnimationPhase(e.doorSrc, 1.0);   // abrir
                    e.bot.LookAtDirection("0 0 1");
                    e.bot.Notify_Transport(transport, e.seat);   // subir animado
                    e.phase = 2;
                    e.timer = 0;
                    eAIGroup ceg = e.bot.GetGroup();
                    if (ceg) ceg.ClearWaypoints();
                } else {
                    // Timeout por PROGRESO (no por tiempo absoluto): mientras se ACERQUE a la puerta no hay
                    // limite -> un bot lejos camina lo que tarde. Solo teleporta si esta ATASCADO (sin
                    // acercarse) BOARD_TIMEOUT_S seg seguidos. Resuelve el teleport prematuro de bots lejanos.
                    if (e.lastDist <= 0.0 || cd2 < e.lastDist - 0.5) {
                        e.lastDist = cd2;
                        e.stuckTimer = 0;
                    } else {
                        e.stuckTimer += 0.5;
                        if (e.stuckTimer > BOARD_TIMEOUT_S) {
                            e.bot.SetPosition(m_Bus.GetPosition());
                            e.bot.StartCommand_Vehicle(transport, e.seat, 0, false);
                            e.bot.Notify_Transport(transport, e.seat);
                            if (e.hasDoor) m_Bus.SetAnimationPhase(e.doorSrc, 0.0);
                            e.phase = 3;
                            BZBusLog.Info("[Boarding] seat " + e.seat + " ATASCADO -> teleport fallback");
                            continue;
                        }
                    }
                }
            } else if (e.phase == 2) {
                e.timer += 0.5;   // subida: comando rapido -> timeout absoluto corto
                if (e.timer > BOARD_TIMEOUT_S) {
                    e.bot.SetPosition(m_Bus.GetPosition());
                    e.bot.StartCommand_Vehicle(transport, e.seat, 0, false);
                    e.bot.Notify_Transport(transport, e.seat);
                    if (e.hasDoor) m_Bus.SetAnimationPhase(e.doorSrc, 0.0);
                    e.phase = 3;
                    continue;
                }
                HumanCommandVehicle cvc = e.bot.GetCommand_Vehicle();
                if (cvc && !cvc.IsGettingIn()) {
                    if (e.hasDoor) m_Bus.SetAnimationPhase(e.doorSrc, 0.0);   // cerrar
                    e.phase = 3;
                    BZBusLog.Info("[Boarding] crew seat " + e.seat + " sentado");
                }
            }
        }
    }

    private bool AnyCrewBoarding() {
        if (!m_CrewBoard) return false;
        for (int i = 0; i < m_CrewBoard.Count(); i++) {
            if (m_CrewBoard[i] && m_CrewBoard[i].phase < 3) return true;
        }
        return false;
    }

    // Gate de proximidad: la entrada de la plaza idx esta "libre" si ningun OTRO boarder ACTIVO
    // (caminando=1 o subiendo=2), ni el chofer si esta caminando/subiendo, tiene su entrada a menos
    // de BOARD_CLUSTER_DIST. Asi se serializa SOLO lo que comparte acceso (Cobra trasera) y se deja
    // en paralelo lo que tiene puertas separadas (Offroad). Resuelve el pile-up del crash 2.
    private bool BoardEntryClear(int idx) {
        if (!m_CrewBoard) return true;
        BZBoardState me = m_CrewBoard[idx];
        if (!me) return true;
        for (int j = 0; j < m_CrewBoard.Count(); j++) {
            if (j == idx) continue;
            BZBoardState o = m_CrewBoard[j];
            if (!o) continue;
            if (o.phase == 1 || o.phase == 2) {
                if (vector.Distance(me.entry, o.entry) < BOARD_CLUSTER_DIST) return false;
            }
        }
        if ((m_BoardPhase == 1 || m_BoardPhase == 2) && m_Bus) {
            Transport t = Transport.Cast(m_Bus);
            if (t) {
                vector dp; vector dd;
                t.CrewEntryWS(0, dp, dd);
                if (vector.Distance(me.entry, dp) < BOARD_CLUSTER_DIST) return false;
            }
        }
        return true;
    }

    // Crea attachments del vehiculo desde la lista configurable del JSON
    // (m_Config.Attachments). Rellena fluidos siempre. Si la lista del JSON
    // esta vacia, el vehiculo spawnea desnudo y hay que equiparlo con COT.
    // Asi soporta cualquier vehiculo sin hardcodear sus partes en codigo.
    private void EquipBus(EntityAI bus) {
        if (!bus) return;
        if (m_Config && m_Config.Attachments) {
            foreach (string p : m_Config.Attachments) {
                if (p != "") bus.GetInventory().CreateAttachment(p);
            }
        }

        // Rellenar fluidos al maximo (aplica a cualquier Car)
        Car car = Car.Cast(bus);
        if (car) {
            car.Fill(CarFluid.FUEL,    car.GetFluidCapacity(CarFluid.FUEL));
            car.Fill(CarFluid.OIL,     car.GetFluidCapacity(CarFluid.OIL));
            car.Fill(CarFluid.COOLANT, car.GetFluidCapacity(CarFluid.COOLANT));
            car.Fill(CarFluid.BRAKE,   car.GetFluidCapacity(CarFluid.BRAKE));

            // 2026-06-27: energizar la bateria (CompEM) para que los FAROS prendan.
            // OnBeforeLightOn() del CarScript exige battery.GetCompEM().GetEnergy() > 0; la bateria
            // attacheada spawnea con energia ELECTRICA 0 (el "qty" del JSON es condicion, no carga).
            ItemBase batt = ItemBase.Cast(bus.GetInventory().FindAttachment(CarBattery.SLOT_ID));
            if (!batt) batt = ItemBase.Cast(bus.GetInventory().FindAttachment(TruckBattery.SLOT_ID));
            if (batt && batt.GetCompEM()) {
                batt.GetCompEM().SetEnergy(batt.GetCompEM().GetEnergyMax());
                BZBusLog.Info("[Battery] CompEM energizada -> faros habilitados");
            }
        }
    }

    // Aplica steering + throttle + auto gearbox para que el bus vaya hacia el waypoint.
    // === THROTTLE CATCH-UP por DEFICIT (2026-07-14; extraido de DriveTowards el 2026-07-21) ===
    // Cuando Boris va por DEBAJO de la velocidad grabada y no llega (peldano/obstaculo que el slope NO ve
    // -> el SlopeAssist no engancha; o el launch conservador del InverseModel), fuerza throttle proporcional
    // al deficit. Al trabarse contra el escalon el deficit crece -> mete mas gas -> lo trepa. El gate
    // effTarget>0.5 excluye stops (el creep del endpoint lo maneja aparte). Vehicle-agnostic.
    //
    // TECHO, NO META (2026-07-21, MEDIDO en las 3 tomas ESQ, IDENTICO en los 3 vehiculos): antes pisaba para
    // ALCANZAR la grabada aunque el waypoint de ADELANTE pidiera MENOS. Medido a 10 m de la curva de 90:
    // Boris ACELERANDO a 25 km/h con el wp de adelante pidiendo 11.8 (SpeedSourceNearest lee el wp de ATRAS,
    // que venia de la recta). Dos segundos despues frenaba 0.34, se abria 5-8 m de la traza, entraba el
    // OFF-PATH recovery ("throttle forzado, recording ignorado") y llegaba a la curva a 6 km/h. El humano
    // hace esos mismos 40 m con acelerador Y freno en CERO -- coasteo puro -- y entra a 18-19 km/h.
    // Si adelante piden menos de lo que ya vamos, NO se agrega gas: se deja coastear.
    private float ComputeThrottleCatchup(float throttle, float effTarget, float kmh, float tgtAhead) {
        if (!m_Config) return throttle;
        if (!m_Config.ThrottleCatchupEnabled) return throttle;
        if (effTarget <= 0.5) return throttle;
        if (tgtAhead > 0.5 && tgtAhead < kmh) return throttle;   // techo, no meta
        float deficit = effTarget - kmh;
        if (deficit <= m_Config.ThrottleCatchupDeadband) return throttle;
        float minThr = deficit * m_Config.ThrottleCatchupGain;
        if (minThr > m_Config.ThrottleCatchupCap) minThr = m_Config.ThrottleCatchupCap;
        if (throttle >= minThr) return throttle;
        BZBusLog.Info("[Catchup] kmh=" + kmh + " target=" + effTarget + " deficit=" + deficit + " throttle " + throttle + " -> " + minThr);
        return minThr;
    }

    // TRACER DEL CONTROLADOR (2026-07-21, Sonom4n). CtlSnap se llama en cada limite de seccion del pipeline.
    // Si el control CAMBIO desde el snapshot anterior, anota "tag(t/b/s)" -> esa seccion escribio. Al final,
    // CtlFlush loguea la secuencia (solo cerca del intercambio, a baja vel). Un parser offline suma la
    // cobertura de todas las tomas: seccion que NUNCA aparece = codigo muerto -> se borra con prueba.
    // Costo en DriveTowards = 1 instruccion por llamada (el trabajo pesado vive aca). Gate por flag.
    private void CtlSnap(string tag, float thr, float brk, float steer) {
        if (!m_Config || !m_Config.ControlTraceEnabled) return;
        // Registra SOLO la(s) variable(s) que ESTA seccion cambio -> atribucion correcta por sentido:
        //   tag[t35]  = puso gas 35    tag[b50] = puso freno 50    tag[s-35] = puso volante -35
        string d = "";
        if (Math.AbsFloat(thr - m_CtlPrevThr) > 0.02)   d = d + "t" + (int)(thr*100);
        if (Math.AbsFloat(brk - m_CtlPrevBrk) > 0.02)   d = d + "b" + (int)(brk*100);
        if (Math.AbsFloat(steer - m_CtlPrevSteer) > 0.02) d = d + "s" + (int)(steer*100);
        if (d != "") {
            m_CtlTrace = m_CtlTrace + tag + "[" + d + "] ";
            m_CtlPrevThr = thr; m_CtlPrevBrk = brk; m_CtlPrevSteer = steer;
        }
    }
    private void CtlReset(float thr, float brk, float steer) {
        if (!m_Config || !m_Config.ControlTraceEnabled) return;
        m_CtlTrace = "";
        m_CtlPrevThr = thr; m_CtlPrevBrk = brk; m_CtlPrevSteer = steer;
    }
    private void CtlFlush(vector pos, float kmh) {
        if (!m_Config || !m_Config.ControlTraceEnabled || !m_Config.Waypoints || m_CtlTrace == "") return;
        // (a) SIEMPRE cerca de un intercambio (legBreak) o endpoint a baja vel -> el PROBLEMA (fino).
        // (b) cada 10 ticks (~5 s) en CUALQUIER situacion -> COBERTURA (cruise, recta, frenada, reversa),
        //     para que corriendo varias tomas se vea que secciones NUNCA se usan (codigo muerto).
        float best = 99999.0;
        for (int i = 0; i < m_Config.Waypoints.Count(); i++) {
            if (!m_Config.Waypoints[i].legBreak && !m_Config.Waypoints[i].isStop) continue;
            float d = vector.Distance(pos, m_Config.Waypoints[i].GetVector());
            if (d < best) best = d;
        }
        bool cerca = (best < 15.0 && kmh < 20.0);
        bool periodico = (m_TickCount % 10 == 0);
        if (cerca || periodico) {
            // idx + modo del target: si el volante satura con modo=reverse en un tramo forward, es el
            // indice filtrandose al tramo de reversa -> dispara el reverse-steering (steering=-steering).
            string modoTr = "?";
            BZWaypoint wpTr = CurrentWaypoint();
            if (wpTr) modoTr = wpTr.mode;
            BZBusLog.Info("[CTL] v=" + (int)kmh + " d=" + (int)best + "m idx=" + m_WaypointIndex + "(" + modoTr + ") leg=" + m_LegStart + ".." + m_LegEnd + " | " + m_CtlTrace);
        }
    }

    // FRENO DE TRANSICION A REVERSE #1 (extraido de DriveTowards 2026-07-21 para hacer lugar al tracer).
    // ACOTADO AL TRAMO: en un forward no ve la reversa del tramo siguiente (que Boris no deberia conocer).
    // Redundante con el perfil de velocidad + brake-ahead; candidato a BORRAR en la depuracion.
    private bool ShouldReverseTransitionBrake(Car bus, BZWaypoint target) {
        if (!target || target.mode == "reverse" || !m_Config || !m_Config.Waypoints) return false;
        int rvScanTr = Math.Min(m_WaypointIndex + 80, m_Config.Waypoints.Count());
        if (m_LegEnd >= m_LegStart && m_LegEnd + 1 < rvScanTr) rvScanTr = m_LegEnd + 1;
        int rvIdxTr = -1;
        for (int riTr = m_WaypointIndex; riTr < rvScanTr; riTr++) {
            if (m_Config.Waypoints[riTr].mode == "reverse") { rvIdxTr = riTr; break; }
        }
        if (rvIdxTr < 0) return false;
        float distRvTr = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[rvIdxTr].GetVector());
        return (distRvTr < 12.0 && bus.GetSpeedometerAbsolute() > 3.5);
    }

    // EL TRAMO decide si estamos en reversa, NO el waypoint individual (2026-07-21, EL VOLANTAZO — cazado
    // con el tracer: idx=122(reverse) leg=0..121 -> steering=-steering -> volante a -98 estando en forward).
    // El indice se filtra al wp de reversa (a centimetros del intercambio por el pliegue) 9 wps ANTES del
    // checkpoint; target.mode pasa a "reverse" y dispara la direccion de reversa en pleno forward. Mirando
    // el modo del PRIMER wp del tramo activo (m_LegStart), Boris no "sabe" de la reversa hasta que el tramo
    // se abre en el checkpoint -> el principio de raiz de Sonom4n, ahora tambien en el camino del volante.
    private bool ActiveLegIsReverse() {
        if (!m_Config || !m_Config.Waypoints) return false;
        if (m_LegStart < 0 || m_LegStart >= m_Config.Waypoints.Count()) return false;
        return m_Config.Waypoints[m_LegStart].mode == "reverse";
    }

    // BANDA DE SALIDA DEL CUSP (2026-08-05, Sonom4n). Saliendo de un tramo reversa/parking hacia forward
    // 'normal' a MUY baja velocidad, pure-pursuit satura el volante: apunta a un punto de mira y con el
    // auto casi parado el indice avanza pero el vehiculo no se mueve -> el vector al punto GIRA y el steer
    // (~1/Ld) se dispara y oscila tope-a-tope -> las ruedas trabadas izq-der matan la traccion -> clavado
    // -> AR (medido con CTL tracer: IM sat s99/-99/100 en idx=78, gas t45-60 presente pero inutil).
    // FIX: mientras dura la banda steereamos por el HEADING GRABADO del wp (estable, del humano), igual que
    // la reversa (linea ~5280), y gateamos pure-pursuit off (~5398). Fiel y sin constante por-vehiculo.
    // Stateless: detecta un wp reversa/parking dentro de los ultimos CuspExitMaxWps.
    private bool CuspExitActive(float kmh, bool isReversePk, string mode) {
        if (!m_Config || !m_Config.CuspExitHeadingBand) return false;
        if (isReversePk || mode != "normal") return false;
        if (kmh >= m_Config.CuspExitKmh) return false;
        if (!m_Config.Waypoints) return false;
        int cnt = m_Config.Waypoints.Count();
        if (m_WaypointIndex < 0 || m_WaypointIndex >= cnt) return false;
        if (m_Config.Waypoints[m_WaypointIndex].targetHeading == 0) return false;
        int back = m_WaypointIndex - m_Config.CuspExitMaxWps;
        if (back < 0) back = 0;
        for (int i = m_WaypointIndex - 1; i >= back; i--) {
            string m = m_Config.Waypoints[i].mode;
            if (m == "reverse" || m == "parking" || m == "maniobra") return true;
        }
        return false;
    }

    // SLOPE ASSIST (extraido de DriveTowards 2026-07-21): en subida fuerza un piso de throttle proporcional
    // a la pendiente, para que Boris no bogue. Solo a baja velocidad y cerca de la linea (latdev<3).
    private float ComputeSlopeAssist(float throttle, float slopeIM, float kmh) {
        if (!(slopeIM > 0.04 && kmh < 20)) return throttle;
        float latDevAbsSa = 0;
        if (m_CorridorValid) {
            latDevAbsSa = m_CorridorLateralOffset;
            if (latDevAbsSa < 0) latDevAbsSa = -latDevAbsSa;
        }
        if (latDevAbsSa >= 3.0) return throttle;
        float minThrottleSlope = slopeIM * 4.0;
        if (minThrottleSlope > 0.90) minThrottleSlope = 0.90;
        if (throttle >= minThrottleSlope) return throttle;
        BZBusLog.Info("[SlopeAssist] kmh=" + kmh + " slope=" + (slopeIM*100) + "% latdev=" + latDevAbsSa + " throttle " + throttle + " -> " + minThrottleSlope);
        return minThrottleSlope;
    }

    private float m_StopBrakePrev;   // freno del tick anterior, para el limite de jerk del controlador de parada

    // CONTROLADOR DE FRENO DE PARADA PREDICTIVO (2026-07-22, blindado por [[project_brake_controller_research]]).
    // Devuelve el freno [0..1] para llegar al punto (distToStop) a velocidad 0, TEMPRANO y SUAVE. -1 = no
    // interviene. slopeSin = pendiente en el sentido del MOVIMIENTO (+ = sube; la gravedad ya frena). NO toca
    // el volante: el freno modula la velocidad, el pure-pursuit mantiene la linea (desacople estilo Arma).
    private float ComputeStopBrake(float kmh, float distToStop, float slopeSin, float dt) {
        if (!m_Config || !m_Config.StopBrakeControllerEnabled) return -1.0;
        if (distToStop < 0) { m_StopBrakePrev = 0; return -1.0; }
        float v = kmh / 3.6;
        // SOLO EN LA FASE DE PARADA (2026-07-22, fix "slow motion"). Antes intervenia hasta 120 m -> en un
        // tramo corto (reversa 45 m) cortaba el gas desde el arranque y Boris nunca aceleraba. El controlador
        // solo actua cuando el punto entra en la DISTANCIA DE FRENADO real: d_brake = v²/(2·a_comoda) + creep,
        // con margen para empezar temprano (estilo Arma). Mas lejos -> crucero normal (lo maneja el InverseModel).
        float aComoda = m_Config.StopBrakeDecelMS;
        if (aComoda < 0.3) aComoda = 0.3;
        // DISTANCIA DE FRENADO SEGUN PENDIENTE (2026-07-22, Sonom4n: "mientras mas inclinado, mas cerca tiene que
        // frenar; en subida menos freno y mas pegado al objetivo, seco"). La desaceleracion TOTAL disponible
        // = a_comoda + g*sin(theta). En SUBIDA (slopeSin>0) la gravedad FRENA -> total mayor -> distancia MAS
        // CORTA -> frena mas cerca/tarde -> Boris CARGA el envion que necesita para llegar y clava seco. En
        // BAJADA (slopeSin<0) al reves: total menor -> distancia mas larga -> frena ANTES (la gravedad empuja).
        // En llano aComodaEff = aComoda (casi solo friccion, como dijo Sonom4n). Es la constante slope-aware.
        float aComodaEff = aComoda + 9.81 * slopeSin;
        if (aComodaEff < 0.3) aComodaEff = 0.3;
        float dBrakePhase = (v * v) / (2.0 * aComodaEff) + m_Config.StopBrakeCreepRangeM;
        if (distToStop > dBrakePhase * 1.3 + 3.0) { m_StopBrakePrev = 0; return -1.0; }
        // ZONA DE CREEP: cerca del punto mantiene ~StopBrakeCreepKmh para asentar, y clava al llegar (anti-jerk).
        if (distToStop <= m_Config.StopBrakeCreepRangeM) {
            if (distToStop <= 0.4 || v < 0.15) { m_StopBrakePrev = 1.0; return 1.0; }
            if (kmh > m_Config.StopBrakeCreepKmh + 0.6) return 0.35;
            m_StopBrakePrev = 0.0;
            return 0.0;
        }
        // LEY PREDICTIVA: desaceleracion necesaria para llegar a la vel de creep en la distancia restante.
        float vTgt = m_Config.StopBrakeCreepKmh / 3.6;
        float dRem = distToStop - m_Config.StopBrakeCreepRangeM;
        if (dRem < 0.15) dRem = 0.15;
        float aNeeded = (v*v - vTgt*vTgt) / (2.0 * dRem);
        if (aNeeded <= 0.0) { m_StopBrakePrev = 0.0; return 0.0; }   // va lento o lejos: todavia no frena
        // COMPENSACION DE PENDIENTE: la gravedad aporta g·sin(θ). En subida (slopeSin>0) el freno necesita MENOS.
        float aBrake = aNeeded - 9.81 * slopeSin;
        if (aBrake < 0.0) aBrake = 0.0;
        // fraccion de freno = decel pedida / decel a freno FONDO del vehiculo (el envelope refina; fallback config).
        float aFull = m_Config.BrakeDecelMS;
        if (aFull < 1.0) aFull = 1.0;
        float b = aBrake / aFull;
        if (b > 1.0) b = 1.0;
        // JERK LIMIT: el freno no salta -> rampa suave (Δbrake por tick acotado por StopBrakeJerkMax).
        float dMax = (m_Config.StopBrakeJerkMax / aFull) * dt;
        if (dMax < 0.02) dMax = 0.02;
        if (b > m_StopBrakePrev + dMax) b = m_StopBrakePrev + dMax;
        if (b < m_StopBrakePrev - dMax) b = m_StopBrakePrev - dMax;
        if (b < 0.0) b = 0.0;
        m_StopBrakePrev = b;
        return b;
    }


    // [AUDITORIA 26/07] EXTRAIDO de DriveTowards para liberar presupuesto de instrucciones.
    // b42 INVERSE MODEL OVERRIDE. Comportamiento identico; el gate queda en DriveTowards.
    private void ApplyInverseModelControl(BZWaypoint target, float effApproachSpeed, float kmh, inout float throttle, inout float brake, float steering) {
        Car bus = Car.Cast(m_Bus);
            // Compute slope desde los wps adyacentes
            float slopeIM = 0;
            int nextWpIdxIM = m_WaypointIndex + 1;
            if (nextWpIdxIM < m_Config.Waypoints.Count() && m_WaypointIndex >= 0) {
                if (m_Config.SlopeBaselineM > 0.5) {
                    // BASE LARGA: rechaza el ruido del SurfaceY por-wp (leÃƒÂ­a +subida en bajadas -> gas fantasma).
                    int slAIdx = m_WaypointIndex;
                    int slBIdx = m_WaypointIndex;
                    float slHalf = m_Config.SlopeBaselineM * 0.5;
                    float slAccB = 0;
                    while (slBIdx < m_Config.Waypoints.Count() - 1 && slAccB < slHalf) {
                        slAccB += vector.Distance(m_Config.Waypoints[slBIdx].GetVector(), m_Config.Waypoints[slBIdx + 1].GetVector());
                        slBIdx++;
                    }
                    float slAccA = 0;
                    while (slAIdx > 0 && slAccA < slHalf) {
                        slAccA += vector.Distance(m_Config.Waypoints[slAIdx].GetVector(), m_Config.Waypoints[slAIdx - 1].GetVector());
                        slAIdx--;
                    }
                    if (slBIdx > slAIdx) slopeIM = m_InverseModel.ComputeSlope(m_Config.Waypoints[slAIdx].GetVector(), m_Config.Waypoints[slBIdx].GetVector());
                } else {
                    BZWaypoint wpA = m_Config.Waypoints[m_WaypointIndex];
                    BZWaypoint wpB = m_Config.Waypoints[nextWpIdxIM];
                    slopeIM = m_InverseModel.ComputeSlope(wpA.GetVector(), wpB.GetVector());
                }
            }

            // Surface real bajo el vehiculo Ã¢â€ â€™ friccion/rodadura por superficie (06-25)
            // Reemplaza el 1.0 hardcodeado: el inverse model ahora capa throttle/freno
            // a la traccion REAL del piso (dirt 0.4, concrete 0.95, asphalt 1.0...).
            vector posIM = bus.GetPosition();
            string surfTypeIM = "";
            GetGame().SurfaceGetType3D(posIM[0], posIM[1], posIM[2], surfTypeIM);
            float surfFriction = m_InverseModel.GetSurfaceFriction(surfTypeIM);
            float surfRolling = m_InverseModel.GetSurfaceRolling(surfTypeIM);

            // dt fijo a 0.1s (10Hz tick aprox del service)
            float dtIM = 0.1;

            // Capa 3: PID
            // effApproachSpeed = target.targetSpeed salvo en bloque approach (rampa lineal hacia maniobra).
            float desiredAccelIM = m_InverseModel.ComputeDesiredAccel(effApproachSpeed, kmh, dtIM);

            // Capa 5: Gear Selector Ã¢â‚¬â€ pre-shift basado en speed + accel deseado
            // OVERRIDE: si GearStrategy=follow_recording, usar el gear del recording
            // en vez del selector del Inverse Model. El recording captura la decision
            // del humano que es informacion subjetiva que el modelo no puede inferir
            // (ej preferir gear alto en cruise por estilo, aunque RPM caiga).
            int currentGearIM = bus.GetGear();
            int newGearIM;
            // follow_recording (2026-07-30): SIN el gate hasInputData (el targetGear esta GRABADO igual aunque
            // frame_to_route ponga hasInputData=false) y SIN capear a AutoMaxGear/MaxGear: la GRABACION es la
            // AUTORIDAD. Con el diesel de bajas vueltas (Truck redline 2400) el SelectGear redlineaba en 1ra y el
            // AutoMaxGear derivaba 4 en vez de 5 -> Boris lugueaba y se clavaba. Lo que el humano usó, Boris lo usa.
            // Per-ruta (GearStrategy sale del hdr), asi el Sedan (auto_box) NO se toca. Ver [[project_lowrpm_diesel_gear_gap]].
            if (m_Config.GearStrategy == "follow_recording" && target.targetGear >= 2 && target.mode != "reverse") {
                newGearIM = target.targetGear;
                if (newGearIM < 2) newGearIM = 2;   // sanity: nunca 0 (reverse) / 1 (neutral) en cruise
            } else {
                newGearIM = m_InverseModel.SelectGear(currentGearIM, kmh, effApproachSpeed, desiredAccelIM);
                // Respetar MaxGear TAMBIEN en el selector del InverseModel (antes solo lo hacia la
                // branch follow_recording -> el cap de AutoMaxGear no tomaba efecto y el Nissan seguia
                // subiendo a 6-7 y ahogandose). Sanity igual que arriba. (2026-07-13)
                if (m_Config.MaxGear > 0 && newGearIM > m_Config.MaxGear) newGearIM = m_Config.MaxGear;
                if (newGearIM < 2) newGearIM = 2;
            }

            // === SLOPE GEAR CAP (2026-06-08) ===
            // En pendiente fuerte (>~5Ã‚Â°) a baja velocidad, FORZAR FIRST gear.
            // El recording puede tener SECOND porque el humano paso por ahi rapido,
            // pero Boris a 1 km/h en SECOND no tiene torque para vencer la gravedad.
            // FIRST gear da el torque multiplicado de la primera ratio = capacidad
            // de trepar incluso 15Ã‚Â° de pendiente. Caso real Z71 en Klen Mountain:
            // Boris stuck a 1 km/h con throttle 0.71 sostenido en SECOND, nunca
            // alcanzaba el RPM optimo de torque. Fix: cap a FIRST cuando lo necesita.
            // Threshold subido a kmh<20: en pendientes fuertes, Boris puede tener
            // momentum hasta 15-20 km/h pero igual no llegar. FIRST gear le da el
            // torque que SECOND no. Caso real Z71 Klen 17Ã‚Â°: kmh=12 en SECOND con
            // traction_limited, no llegaba, posible engine crash.
            //
            // HISTERESIS (2026-06-10): sin ella, en terreno ondulado la pendiente
            // cruza el 8% y la velocidad el 20km/h una y otra vez -> el gear oscilaba
            // 2<->grabado cada tick = GEAR THRASHING -> torque irregular -> zigzag
            // (23 disparos del cap en UN run, confirmado en ai_run + RPT). Fix: una
            // vez que el cap ENGANCHA FIRST, lo MANTIENE hasta salir CLARO de la zona
            // (slope<5% O kmh>25), no re-evalua en el borde. Engancha 1 vez, sostiene,
            // suelta 1 vez. follow_recording seguia bien; el cap sin histeresis lo pisaba.
            // 2026-07-30: el slope-cap NO debe pisar follow_recording. Con el camion pesado (5.5x masa) en una
            // pendiente leve a baja velocidad, el cap forzaba 1ra -> redlineaba a 12 km/h -> nunca llegaba a kmh>25
            // para soltar la histeresis -> DEADLOCK en 1ra medio recorrido (test 30/07). Sonom4n YA demostro que esta
            // pendiente se maneja en gear 4-5 (la grabacion = prueba de factibilidad); el cap es heuristica del MODELO
            // para cuando NO hay demostracion. El DATO gana. Solo aplica en auto_box (Sedan intacto). Ver [[project_lowrpm_diesel_gear_gap]].
            float slopeAbsSc = slopeIM;
            if (slopeAbsSc < 0) slopeAbsSc = -slopeAbsSc;
            bool slopeCapNow;
            if (m_SlopeGearCapActive) slopeCapNow = (slopeAbsSc > 0.05 && kmh < 25.0);
            else                      slopeCapNow = (slopeAbsSc > 0.08 && kmh < 20.0);
            if (m_Config.GearStrategy == "follow_recording") slopeCapNow = false;
            if (slopeCapNow && newGearIM > 2) {
                if (!m_SlopeGearCapActive)
                    BZBusLog.Info("[SlopeGearCap] kmh=" + kmh + " slope=" + (slopeIM*100) + "% gear " + newGearIM + " -> 2 (FIRST, hold)");
                newGearIM = 2;
            }
            m_SlopeGearCapActive = slopeCapNow;

            if (newGearIM != currentGearIM && newGearIM >= 2) {
                SetDesiredGear(newGearIM);
                bus.ShiftTo(newGearIM);
            }

            // Capa 4: Inverse Model (usa el gear actual o post-shift)
            float imThrottle = 0;
            float imBrake = 0;
            string imNote = "";
            m_InverseModel.ComputeInputs(desiredAccelIM, bus.GetGear(), kmh, slopeIM, surfFriction, surfRolling, imThrottle, imBrake, imNote);

            // OVERRIDE
            throttle = imThrottle;
            brake = imBrake;
            CtlSnap("IM", throttle, brake, steering);   // TRACER: el InverseModel puso este gas/freno (lo bueno)

            // === SLOPE ASSIST (2026-06-09 v2, mas agresivo) ===
            // Tuning iterativo: Boris seguia aceleranddo poco en pendientes mid (5-10Ã‚Â°).
            // Threshold 0.04 (~2.3Ã‚Â°), factor 4.0, cap 0.90.
            // Distribuciones:
            //   2.3Ã‚Â° (0.04) Ã¢â€ â€™ 0.16    3Ã‚Â° (0.052) Ã¢â€ â€™ 0.21
            //   5Ã‚Â° (0.087)  Ã¢â€ â€™ 0.35    7Ã‚Â° (0.122) Ã¢â€ â€™ 0.49
            //  10Ã‚Â° (0.176)  Ã¢â€ â€™ 0.70   12Ã‚Â° (0.21)  Ã¢â€ â€™ 0.84
            //  13Ã‚Â°+ Ã¢â€ â€™ 0.90 (cap)
            // Cap 0.90 sigue preservando algo de steering authority.
            throttle = ComputeSlopeAssist(throttle, slopeIM, kmh);   // extraido 2026-07-21 para hacer lugar al sensor del aim

            // === THROTTLE CATCH-UP por DEFICIT (2026-07-14) ===
            // EXTRAIDO a ComputeThrottleCatchup (2026-07-21): DriveTowards toca el techo de
            // "Too many instructions per function" de Enforce. Ver la funcion para el porque del guard.
            // GATE cerca del endpoint (2026-07-31): dentro del rango del iman el catchup NO corre. Sino floreaba
            // throttle 0.95 para "alcanzar" el target local MIENTRAS el lookahead frenaba para el stop -> oscilacion
            // surge<->freno = los PASITOS. Cerca del stop Boris se asienta hacia la parada (lookahead/iman), no cachea.
            bool nearStopCu = false;
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                float dStopCu = vector.Distance(bus.GetPosition(), m_Config.Waypoints[m_NextStopIndex].GetVector());
                if (dStopCu < m_Config.EndpointGlideRangeM) nearStopCu = true;
            }
            if (!nearStopCu) throttle = ComputeThrottleCatchup(throttle, effApproachSpeed, kmh, target.targetSpeed);
            CtlSnap("slope+catchup", throttle, brake, steering);

            // === SENSADO DE SUPERFICIE EN RUNTIME (2026-07-14, v2 ROBUSTO) ===
            // Boris SIENTE la inclinacion REAL debajo suyo y ajusta throttle: fuerza en subida, conservador
            // en bajada. Usa el VECTOR FORWARD (GetDirection): grade = comp. vertical / horizontal = rise/run.
            // ROBUSTO: acotado (dir es unitario), SIN ambiguedad de Euler, SIN wrapping, SIN Math.Tan. La v1
            // usaba GetOrientation[1]+Tan -> cerca de 90Ã‚Â° daba Inf -> formatear el log crasheaba ucrtbase
            // (0xc0000409). Clamp defensivo + guard horizontal. Log throttled (cada 2s) con valor acotado.
            if (m_Config.SurfaceSenseEnabled) {
                vector fwdSs = bus.GetDirection();
                float horizSs = Math.Sqrt(fwdSs[0]*fwdSs[0] + fwdSs[2]*fwdSs[2]);
                float slopeInst = 0;
                if (horizSs > 0.05) slopeInst = (fwdSs[1] / horizSs) * m_Config.SurfaceSensePitchSign;
                // EN REVERSA EL FRENTE APUNTA AL REVES DEL MOVIMIENTO (2026-07-22, Sonom4n: "en ese tramo el
                // terreno se eleva bastante"). GetDirection() es el FRENTE; retrocediendo cuesta arriba el
                // frente mira cuesta ABAJO -> el sensor creia "bajada" (medido: DOWN grade=-0.124 mientras
                // subia +12.7%) y RECORTABA el gas de 1.0 a 0.62 justo donde Boris necesita envion para
                // subir en reversa -> bogaba, se cruzaba y se desalineaba. Invertir el signo lo corrige.
                if (ActiveLegIsReverse()) slopeInst = -slopeInst;
                if (slopeInst > 1.5)  slopeInst = 1.5;
                if (slopeInst < -1.5) slopeInst = -1.5;
                m_SurfPitchSm = m_SurfPitchSm * m_Config.SurfaceSenseSmooth + slopeInst * (1.0 - m_Config.SurfaceSenseSmooth);
                float slopeReal = m_SurfPitchSm;
                bool logSurf = (GetGame().GetTickTime() - m_LastSurfLog > 2.0);
                if (slopeReal > 0.04 && kmh < m_Config.SurfaceSenseUpMinKmh) {
                    float floorSs = slopeReal * m_Config.SurfaceSenseUpFactor;
                    if (floorSs > m_Config.SurfaceSenseUpCap) floorSs = m_Config.SurfaceSenseUpCap;
                    if (throttle < floorSs) {
                        if (logSurf) { BZBusLog.Info("[SurfaceSense] UP grade=" + slopeReal + " kmh=" + kmh + " throttle " + throttle + " -> " + floorSs); m_LastSurfLog = GetGame().GetTickTime(); }
                        throttle = floorSs;
                    }
                } else if (slopeReal < -m_Config.SurfaceSenseDownThresh) {
                    float capDown = 1.0 - (-slopeReal) * m_Config.SurfaceSenseDownFactor;
                    if (capDown < 0) capDown = 0;
                    if (throttle > capDown) {
                        if (logSurf) { BZBusLog.Info("[SurfaceSense] DOWN grade=" + slopeReal + " throttle " + throttle + " -> " + capDown); m_LastSurfLog = GetGame().GetTickTime(); }
                        throttle = capDown;
                    }
                }
            }

            // === OVERSPEED THROTTLE CUT (2026-07-12) ===
            // Si Boris ya SUPERA la velocidad objetivo (effApproachSpeed = pintada/curvatura/capada),
            // cortar el gas (que coastee hasta el target) y, si se pasa mucho, meter freno proporcional.
            // Va DESPUES del InverseModel Y del SlopeAssist: los dos fuerzan throttle en subida sin mirar
            // si esta overspeed -> por eso en la subida +7.9% pintada 7 Boris hacia 13.5 con throttle 0.63.
            // effApproachSpeed>0.5 excluye stops (target 0, los maneja la logica de parada aparte). Gateado.
            // trepada EMPINADA y LENTA: NO cortar el gas (necesita el envion; la gravedad ya frena).
            // slopeIM es el pitch REAL del path (SampleTerrainY lo dejo correcto). El +7.9% de una
            // curva-en-subida NO llega al umbral -> ahi el cut SI actua y respeta el 7 pintado.
            bool steepClimbOs = (slopeIM > m_Config.ClimbAssistSlope && kmh < m_Config.ClimbAssistMinKmh);
            if (m_Config.InverseModelOverspeedCut && effApproachSpeed > 0.5 && !steepClimbOs) {
                float overKmh = kmh - effApproachSpeed - m_Config.OverspeedCutDeadbandKmh;
                if (overKmh > 0) {
                    throttle = 0;
                    // FRENO solo en llano/bajada. En subida no frenar (la gravedad ya desacelera; frenar
                    // cuesta arriba mata el enviÃƒÂ³n -> bog -> AR @170m). En bajada frenar fuerte (la gravedad
                    // empuja a la curva; el freno topado en 0.5 no alcanzaba -> entraba a 26 a la 90).
                    if (slopeIM < m_Config.OverspeedCutBrakeMaxUphill) {
                        float ovBrake = overKmh * m_Config.OverspeedCutBrakeGain;
                        if (ovBrake > m_Config.OverspeedCutBrakeCap) ovBrake = m_Config.OverspeedCutBrakeCap;
                        if (ovBrake > brake) brake = ovBrake;
                    }
                }
            }
            // CLIMB ASSIST: en trepada empinada+lenta, gas a fondo para no bogar/clavarse (mejor pasarse
            // del pintado que quedar a 0 -> AR). Caso @94m NUEVO01: +15% a 7 km/h con throttle 0.60 se
            // clavaba; con 1.0 (post-AR) trepaba a 8.5 sin drama. 2026-07-12.
            if (m_Config.ClimbAssistEnabled && steepClimbOs && throttle < m_Config.ClimbAssistThrottle) {
                // GUARD DE STOP (2026-08-05): no asistir la trepada pegado al endpoint del tramo -> soltar antes
                // para que el freno de endpoint clave limpio (sino ClimbAssist lo pasaba de largo, Hatchback +1.26m).
                bool nearLegStopCa = false;
                if (m_Config.ClimbAssistStopGuardM > 0 && m_Bus && m_LegEnd >= 0 && m_LegEnd < m_Config.Waypoints.Count()) {
                    float dLegCa = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[m_LegEnd].GetVector());
                    if (dLegCa < m_Config.ClimbAssistStopGuardM) nearLegStopCa = true;
                }
                if (!nearLegStopCa) {
                    if (brake < 0.05) brake = 0;   // no frenar mientras trepa
                    throttle = m_Config.ClimbAssistThrottle;
                }
            }
            CtlSnap("surf+overspd+climb", throttle, brake, steering);

            // LOG DE CURVA (diagnÃƒÂ³stico): cuando va PASADO de la velocidad objetivo, capturar de dÃƒÂ³nde sale
            // el throttle Ã¢â‚¬â€ imThr (InverseModel puro) vs thr final (post SlopeAssist/cut) Ã¢â‚¬â€ + slope y wpIdx.
            // Confirma si en las 90 el gas es del InverseModel (target alto) o del SlopeAssist (slope mal leÃƒÂ­do).
            if (m_Config.LogCornerApproach && effApproachSpeed > 0.5 && kmh > effApproachSpeed + 3.0) {
                int scInt = 0;
                if (steepClimbOs) scInt = 1;
                string cdbg = "[CornerDbg] kmh=" + kmh + " effTgt=" + effApproachSpeed + " wpTgt=" + target.targetSpeed;
                cdbg = cdbg + " slope=" + (slopeIM*100) + "% wpIdx=" + m_WaypointIndex;
                cdbg = cdbg + " imThr=" + imThrottle + " thr=" + throttle + " brk=" + brake + " steepClimb=" + scInt;
                BZBusLog.Info(cdbg);
            }

            // Log ocasional para debug (cada ~5s)
            float logElapsed = GetGame().GetTickTime() - m_LastInverseModelLog;
            if (logElapsed < 0) logElapsed = -logElapsed;
            if (logElapsed > 5.0) {
                m_LastInverseModelLog = GetGame().GetTickTime();
                BZBusLog.Info("[InvModel] target=" + target.targetSpeed + " current=" + kmh + " desiredA=" + desiredAccelIM + " slope=" + (slopeIM*100) + "% Ã¢â€ â€™ throttle=" + throttle + " brake=" + brake + " " + imNote);
            }
    }

    // [AUDITORIA 26/07] EXTRAIDO de DriveTowards para liberar presupuesto de instrucciones.
    // b34 DIRECT REPLAY (P-controller reversa/parking/maniobra, taper endpoint). Identico; gate queda en DriveTowards.
    private void ApplyDirectReplayControl(BZWaypoint target, float kmh, inout float throttle, inout float brake, inout float steering) {
        Car bus = Car.Cast(m_Bus);
            // === STEERING ===
            // En parking mode (mode=="parking"): MANTENEMOS Stanley computado
            // arriba. El input steering del recording es binario (0/-1/+1) y
            // no reproduce los angulos reales que la rueda alcanzo Ã¢â‚¬â€
            // Boris con steering=0 la mayoria del tiempo va en linea recta
            // ignorando las curvas que el humano hizo tocando A/D brevemente.
            // Stanley sigue el corredor (la linea que el humano dibujo) bien.
            //
            // En cruise (mode="normal"): respetar el steering del recording.
            // SUB-TICK PULSE CAPTURE 2026-06-03: el framework avanza N wps por
            // tick (cap por movimiento fisico). Si tomamos solo target.targetSteering
            // (el wp final del avance), perdemos pulses que cayeron en wps
            // intermedios. Boris a 19 km/h come ~50 wps/s a 50Hz = 25 wps por
            // tick. Pulses humanos de keyboard duran 80-160ms = 4-8 wps. Sin
            // este scan, los pulses pasan invisibles. Fix: scanear todos los
            // wps avanzados en este tick, aplicar el de mayor |steer|.
            // (movido afuera del if(directReplayActive) abajo, ver 1985+)
            if (target.mode != "parking" && target.mode != "reverse") {
                // YIELD a Stanley cuando Boris esta stuck (no avanza wps).
                // Insight 2026-06-03 (T6 spiral): si Boris over-rotates y se sale del
                // corredor, wp_idx deja de avanzar. Aplicar target.targetSteering directo
                // = el ultimo wp visitado, que puede tener steer=1 (pulse humano). Spiral
                // de muerte: sigue rotando, mas lejos del path. Solucion: solo override
                // steering cuando hay avance de wps. Si no avanza, dejar Stanley que ya
                // se calculo arriba Ã¢â‚¬â€ Stanley corrige hacia el corredor (recovery natural).
                // Sub-tick: AVERAGE del steering en el rango avanzado.
                // Cambio 2026-06-03: antes usabamos MAX |steer| pero aplicar full
                // lock durante 500ms (tick) cuando el humano lo aplico solo 160ms
                // produce over-rotation. El AVERAGE preserva el integral del input
                // (area bajo la curva), que es lo que determina la respuesta del
                // vehiculo. Wps a 50Hz tienen duracion uniforme (20ms) Ã¢â€ â€™ average
                // aritmetico equivale a weighted-by-time-average.
                //
                // NO sobreescribir steering si no hay avance (Stanley toma control).
                // El steering inicial = lo que Stanley calculo arriba, intacto.
                if (m_DR_PrevWpIdx >= 0 && m_DR_PrevWpIdx < m_WaypointIndex) {
                    int scanEnd = m_WaypointIndex;
                    if (scanEnd >= m_Config.Waypoints.Count()) scanEnd = m_Config.Waypoints.Count() - 1;
                    float sumSteer = 0;
                    int   countSteer = 0;
                    for (int wsi = m_DR_PrevWpIdx; wsi <= scanEnd; wsi++) {
                        BZWaypoint scanWp = m_Config.Waypoints[wsi];
                        if (!scanWp.hasInputData) continue;
                        if (scanWp.mode != "normal") continue;
                        sumSteer += scanWp.targetSteering;
                        countSteer++;
                    }
                    if (countSteer > 0) {
                        float avgSteer = sumSteer / countSteer;
                        // === TIME-DOMAIN SCALING ===
                        // El recording esta en "wp-time" (cada wp = 20ms a 50Hz).
                        // Si Boris consume MAS wps de lo que el humano hizo en 500ms
                        // (tick), esta procesando time-compressed inputs. Si consume
                        // POCOS, el average pesa demasiado. Escalar por duracion real
                        // del rango del recording vs duracion del tick:
                        //   scale = (wps_avanzados * 20ms) / 500ms
                        //   Si scale < 1: humano hizo menos input en el tiempo del tick Ã¢â€ â€™ reducir steer
                        //   Si scale > 1: humano hizo mas input, capear a 1 (Boris ya no puede pulsear mas)
                        // Sin esto: variabilidad enorme entre tomas (drift 3deg vs 176deg
                        // con mismo build), observado 2026-06-03 con T6 AVERAGE fix.
                        float realTimeOfRange = countSteer * 0.02; // 50Hz = 20ms/wp
                        float scale = realTimeOfRange / 0.5;        // tick = 500ms
                        if (scale > 1.0) scale = 1.0;
                        steering = avgSteer * scale;
                    }
                }
                m_DR_PrevWpIdx = m_WaypointIndex;
                if (steering > 1.0)  steering = 1.0;
                if (steering < -1.0) steering = -1.0;
            }
            // (else: para parking O reverse, steering ya quedo computado por
            //  Stanley arriba Ã¢â‚¬â€ Stanley flippeado + steer invertido en reverse,
            //  Stanley normal en parking. No sobrescribir con targetSteering
            //  binario del recording.)

            // === THROTTLE / BRAKE ===
            // En parking: NEGOCIACION ESTRUCTURAL Ã¢â‚¬â€ forzar velocidad minima
            // para que wp_index avance. Sin esto, cuando el recording decel
            // bajo el threshold y nuestro speed-aware tambien lo replicaba,
            // el bot se quedaba a kmh < 1 y wp_index se CONGELABA mientras el
            // bot se movia (validado por AI log de 2026-05-31: wpidx 826
            // clavado 21s mientras bot se movio 9m, corredor invalido,
            // Stanley fallback pidiendo full steer perpetuo). Sacrificamos
            // matching de velocidad del recording por sincronizacion del
            // framework. Stanley sigue el corredor (linea grabada), velocidad
            // se "negocia" para mantener wp_index en sync con la posicion fisica.
            //
            // Excepcion: si el recording pidio brake fuerte (>0.7), respetarlo
            // Ã¢â‚¬â€ es la senal de "fin de parking, frenar al endpoint".
            if (target.mode == "parking" || target.mode == "reverse" || target.mode == "maniobra") {
                // ROLLBACK 2026-06-07: el direct replay throttle en parking copiaba
                // target_throttle=1.0 literal del humano (que pisaba full para escalar
                // a baja velocidad). Boris reproducia el throttle pero sin la friccion/
                // contexto fisico del humano Ã¢â€ â€™ explote a 40+ km/h Ã¢â€ â€™ choque Ã¢â€ â€™ stuck Ã¢â€ â€™ AR.
                // Comparativa empirica del 06-07:
                //   - 9:35 (sin direct replay): 96% ruta, 0 stuck en parking, max 14.2
                //   - 10:02+ (con direct replay): 28-64 km/h max parking, 28-121 stuck
                // Vuelta al P controller original con cap MAX_PARKING_KMH. Conservamos
                // direct replay STEERING para full lock (otro bloque del codigo).
                if (target.mode == "maniobra" && target.hasInputData && target.targetThrottle > 0.7 && target.targetBrake < 0.2) {
                    // SAFETY: honor recording solo si Boris esta DEBAJO del cap maniobra+3.
                    // 2026-06-07: humano pisaba full saliendo del bloque maniobra (acelerando
                    // hacia recta siguiente). Boris copiaba y llegaba a 66 km/h Ã¢â€ â€™ no podia
                    // frenar para la curva 2 que requeria parking. Cap limita el momentum
                    // de salida para que cruise predictivo pueda anticipar siguiente curva.
                    float manCapHonor = 13.0;
                    if (m_Config && m_Config.ManiobraTargetSpeedCap > 0) manCapHonor = m_Config.ManiobraTargetSpeedCap + 3.0;
                    if (kmh < manCapHonor) {
                        throttle = target.targetThrottle;
                        brake    = target.targetBrake;
                    }
                } else if (target.mode == "maniobra" && target.hasInputData && target.targetThrottle < 0.2 && target.targetBrake < 0.2) {
                    // HONOR COASTING: humano coasteaba por la curva (sin throttle, sin
                    // brake). Boris debe coastear tambien Ã¢â‚¬â€ no pisar el cap del P controller
                    // que generaria full lock + stall.
                    // 2026-06-07 Impreza 2da curva: recording targetThrottle=0 en wp 964-999
                    // (humano coastea con momentum). P controller cap 0.4 + slope ~0.13
                    // pushea contra cornering forces Ã¢â€ â€™ Boris se traba a 0.1 km/h sustained.
                    // Coast preserva momentum si Boris entro al bloque con velocidad.
                    // SAFETY: solo si Boris ya tiene velocidad razonable. Si esta lento,
                    // dejar que el P controller intente empujar (sino se queda parado).
                    if (kmh > 5.0) {
                        throttle = 0;
                        brake = 0;
                    }
                    // si kmh <= 5, fall through al P controller (intenta arrancar)
                } else {
                // === DOS FASES (mental model del usuario, AI log 2026-05-31) ===
                // 1) APPROACH: cruise predictivo arriba ya desacelerÃƒÂ³ a 8 km/h
                //    en los wps preparking. Bot entra a parking ~8 km/h.
                // 2) EXECUTE: aca mantenemos MAX 8 km/h con P controller suave.
                //    Brake solo cuando el RECORDING senala stop final
                //    (targetSpeed < 1.0 = humano estaba parado aca).
                //
                // Bug anterior (brake > 0.7 del recording): el humano taps el
                // freno breve durante la maniobra, captura brake=1 en samples
                // aislados. Mi excepcion frenaba a Boris perpetuo desde el
                // primer tap. Fix: usar targetSpeed<1 como senal de fin real.
                // PARKING velocidad: respetar la cadencia del recording.
                // El humano grababa 3 km/h en curvas cerradas porque el radio
                // de giro del vehiculo lo requeria. Forzar 8 km/h en esa misma
                // curva = Boris no logra el radio = orbita el wp objetivo
                // (validado AI log 2026-05-31, wp 326, 10s dando vueltas).
                //
                // Formula: clamp(recording.targetSpeed, MIN, MAX)
                //   MIN: garantiza wp_index avanzando (sino se congela)
                //   MAX: cap de seguridad para parking en zonas rectas
                //   targetSpeed del recording: senal de control real del humano
                float MIN_PROGRESSION_KMH = 3.0;
                float MAX_PARKING_KMH     = 15.0; // forward parking Ã¢â‚¬â€ permite rectas (subido de 12 a 15 Ã¢â‚¬â€ Impreza validation 2026-06-07)
                // reverse: SEGUIR la targetSpeed grabada (techo configurable). El viejo 6.0 fijo
                // estrangulaba la rampa del galpon (recording pedia 20, ver [[recording manda]]).
                float MAX_REVERSE_KMH     = GetReverseTargetSpeedCap();
                float capKmhPk;
                if (target.mode == "reverse")  capKmhPk = MAX_REVERSE_KMH;
                else if (target.mode == "maniobra") {
                    capKmhPk = MAX_PARKING_KMH;
                    if (m_Config && m_Config.ManiobraTargetSpeedCap > 0) capKmhPk = m_Config.ManiobraTargetSpeedCap;
                }
                else {
                    capKmhPk = MAX_PARKING_KMH;
                    if (m_Config && m_Config.ParkingTargetSpeedCap > 0) capKmhPk = m_Config.ParkingTargetSpeedCap;
                }
                float targetKmhPk = target.targetSpeed;
                if (targetKmhPk < MIN_PROGRESSION_KMH) targetKmhPk = MIN_PROGRESSION_KMH;
                if (targetKmhPk > capKmhPk)            targetKmhPk = capKmhPk;
                // RECORDED-SPEED CAP en REVERSE (2026-07-02, Sonom4n): en M3 target.targetSpeed es la
                // velocidad GEOMETRICA (M3 la sobre-escribe) -> Boris reversa a ~14-22 km/h cuando el
                // humano fue a ~6 -> arco ANCHO (drift 12m, validado ai_run 093300). Capear a la
                // recordedSpeed (lo que hizo el humano) -> reversa a SU velocidad -> arco tight -> sigue
                // la traza. La recordedSpeed ya codifica vehiculo + curvatura + PENDIENTE (lo manejo asi);
                // el P-controller de abajo la SOSTIENE en la inclinacion (frena en bajada, throttle en
                // subida). Espejo del cap forward de precision. Solo baja; re-floor a MIN para no clavar el wp.
                if (target.mode == "reverse" && target.recordedSpeed > 0.5 && target.recordedSpeed < targetKmhPk) targetKmhPk = target.recordedSpeed;
                // PHYSICS-SPEED CAP en REVERSE (2026-07-02, Sonom4n): velocidad optima del arco segun la
                // FISICA de ESTE vehiculo -> generaliza M3 a CUALQUIER vehiculo (no solo el grabado).
                // ffRev = fraccion del volante MAXIMO que el vehiculo necesita para el arco local (ya
                // normaliza la curvatura por wheelbase + maxSteer = R_min del propio vehiculo). ff~0
                // (recta) -> rapido; ff~1 (arco al limite del vehiculo, volante a fondo) -> lento. Un
                // vehiculo de giro ANCHO -> ff mas alto p/ el mismo arco -> mas lento -> logra seguirlo.
                // v_reverse final = min(recorded, physics). REV_PHYS_MIN_KMH = vel en el arco mas cerrado.
                // Tunear in-game: MAX (recta) = MAX_REVERSE_KMH; MIN (full lock) = REV_PHYS_MIN_KMH.
                if (target.mode == "reverse") {
                    float ffRevSpd = ComputeFeedforwardSteering(bus.GetPosition(), kmh, false, true);
                    if (ffRevSpd < 0) ffRevSpd = -ffRevSpd;
                    float REV_PHYS_MIN_KMH = 5.0;
                    float vPhysRev = MAX_REVERSE_KMH - (MAX_REVERSE_KMH - REV_PHYS_MIN_KMH) * ffRevSpd;
                    if (vPhysRev < targetKmhPk) targetKmhPk = vPhysRev;
                }
                if (targetKmhPk < MIN_PROGRESSION_KMH) targetKmhPk = MIN_PROGRESSION_KMH;

                // REVERSE ENDPOINT TAPER Ã¢â‚¬â€ TERRAIN-AWARE (Sonom4n 2026-07-02, ai_run 135411/135158): en el
                // tramo RECTO/PLANO de salida el arco se endereza -> ffRev~0 -> vPhysRev sube al
                // MAX_REVERSE (25) y Boris reversa a 21-27 km/h hasta el ULTIMO wp; el brake de endpoint
                // (isEndOfParkingPk) recien frena en la transicion de mode -> TARDE -> se pasa 12-16m del
                // endpoint y recupera en cruise (max latdev 16m, ambos runs). Fix: rampar la velocidad
                // objetivo al acercarse al FIN del bloque reverse, medido por DISTANCIA de path (no por el
                // target que salta al forward-resume) -> llega lento y para preciso.
                //
                // LA VELOCIDAD DEL ENDPOINT LA FIJA EL TERRENO (Sonom4n): en PLANO 3 km/h esta bien (previene
                // la sobre-pasada). CUESTA ARRIBA (rampa galpon ~8deg) el floor SUBE con la pendiente ->
                // queda por ENCIMA del target (el galpon ya reversa a ~3 grabado) -> el taper NO dispara
                // (self-guard: floor>=target => vTaper>=target siempre) -> el galpon conserva su trepada
                // validada de 3 km/h + slope-comp SIN que le metan un brake que apague el empuje -> no
                // rollback. Grade = dy/dist medido en el MISMO loop que suma la distancia (usa la Y de los
                // wps, gratis). REVERSE-ONLY (no toca cruise/maniobra). vTaper solo BAJA la velocidad.
                if (target.mode == "reverse" && m_Config && m_Config.Waypoints) {
                    vector busPosTaper = bus.GetPosition();
                    float distToRevEnd = 0;
                    float endptYTaper = busPosTaper[1];
                    int riTaper = m_WaypointIndex;
                    vector prevRpTaper = busPosTaper;
                    while (riTaper < m_Config.Waypoints.Count() && m_Config.Waypoints[riTaper].mode == "reverse") {
                        vector rpTaper = m_Config.Waypoints[riTaper].GetVector();
                        distToRevEnd = distToRevEnd + vector.Distance(prevRpTaper, rpTaper);
                        endptYTaper = rpTaper[1];
                        prevRpTaper = rpTaper;
                        riTaper++;
                    }
                    float REV_TAPER_M = 8.0; // ventana (m) de frenado antes del endpoint (tunable; sube=frena antes)
                    if (distToRevEnd < REV_TAPER_M && distToRevEnd > 0.5) {
                        float gradeTaper = (endptYTaper - busPosTaper[1]) / distToRevEnd; // + = endpoint cuesta arriba
                        float taperFloor = MIN_PROGRESSION_KMH; // PLANO: 3 km/h
                        if (gradeTaper > 0.03) taperFloor = MIN_PROGRESSION_KMH + gradeTaper * 36.0; // CUESTA ARRIBA: mas momentum (8deg~0.14 -> floor ~8)
                        float taperFrac = distToRevEnd / REV_TAPER_M; // 1 lejos -> 0 en el endpoint
                        float vTaper = taperFloor + (targetKmhPk - taperFloor) * taperFrac;
                        if (vTaper < targetKmhPk) targetKmhPk = vTaper;
                    }
                }

                // MICRO-APPROACH al reverse (entry-taper, espejo del endpoint-taper de salida; Sonom4n
                // 2026-07-02, "como un automatic micro approach"): cuando un bloque FORWARD
                // (parking/maniobra) se acerca al INICIO de un bloque reverse (que es un STOP para el
                // shift), rampa la velocidad a un stop preciso -> Boris NO llega rapido y se PASA.
                // Data (OffroadHatch EXAMPLE19-M3-A-1): llegaba a ~12 km/h y frenaba seco -> se pasaba
                // (latdev bump 2.31->2.75) + el reverse lo corregia a volantazos. Con el micro-approach
                // llega despacio y para clavado -> reverse RECTO (como la grabacion humana, "casi sin
                // volante"). Automatico + reverse-gated: solo dispara si hay un wp reverse INMINENTE
                // adelante (dentro de REV_ENTRY_WINDOW). Espejo simetrico: micro-approach a la ENTRADA,
                // endpoint-taper a la SALIDA. NO toca cruise/parking normal (solo con reverse por venir).
                if (target.mode != "reverse" && m_Config && m_Config.Waypoints) {
                    float distToRevStart = 0;
                    int riEntry = m_WaypointIndex;
                    vector prevRpEntry = bus.GetPosition();
                    bool foundRev = false;
                    float REV_ENTRY_WINDOW = 8.0; // ventana (m) de micro-approach antes del reverse-start (tunable)
                    while (riEntry < m_Config.Waypoints.Count()) {
                        vector rpEntry = m_Config.Waypoints[riEntry].GetVector();
                        distToRevStart = distToRevStart + vector.Distance(prevRpEntry, rpEntry);
                        if (distToRevStart > REV_ENTRY_WINDOW) break; // reverse-start mas lejos que la ventana
                        if (m_Config.Waypoints[riEntry].mode == "reverse") { foundRev = true; break; }
                        prevRpEntry = rpEntry;
                        riEntry++;
                    }
                    if (foundRev) {
                        float entryFrac = distToRevStart / REV_ENTRY_WINDOW; // 0 en el reverse-start, ~1 lejos
                        float vEntry = MIN_PROGRESSION_KMH + (targetKmhPk - MIN_PROGRESSION_KMH) * entryFrac;
                        if (vEntry < targetKmhPk) targetKmhPk = vEntry;
                    }
                }

                // Fin de bloque: detectado por TRANSICION de mode.
                // - parking/reverse: si proximo wp no es parking/reverse Ã¢â€ â€™ fin (frenar al endpoint).
                // - maniobra: si proximo wp no es maniobra Ã¢â€ â€™ fin (NO frenar, dejar flow continuo).
                bool isEndOfParkingPk = true;
                bool isEndOfManiobraPk = (target.mode == "maniobra");
                int nextIdxPk = m_WaypointIndex + 1;
                if (m_Config && nextIdxPk < m_Config.Waypoints.Count()) {
                    string nextModePk = m_Config.Waypoints[nextIdxPk].mode;
                    if (target.mode == "maniobra") {
                        if (nextModePk == "maniobra") isEndOfManiobraPk = false;
                        isEndOfParkingPk = false; // maniobra nunca dispara el path de frenado parking
                    } else {
                        if (nextModePk == "parking" || nextModePk == "reverse") isEndOfParkingPk = false;
                    }
                }

                if (isEndOfParkingPk && target.mode != "maniobra") {
                    // Fin de parking/reverse: brake fuerte para detenerse en el endpoint
                    throttle = 0;
                    brake    = 1.0;
                } else {
                    float speedErrPk = targetKmhPk - kmh; // + = bot lento, - = bot rapido
                    if (speedErrPk > 1.0) {
                        // Lento: throttle proporcional. En reverse ganancia mas
                        // baja (0.05) + cap 0.25 Ã¢â‚¬â€ evita el overshoot 0Ã¢â€ â€™8 km/h
                        // que causaba tap-tap en reverse. Forward parking
                        // mantiene 0.08 + cap 0.4 (necesario para vencer
                        // rampas etc).
                        float gainPk, capPk;
                        // reverse: cap subido 0.25->0.4 (igual que forward) para VENCER LA RAMPA del
                        // galpon (8deg). El 0.25 anti-tap-tap solo importaba a target bajo (curva);
                        // el cap alto recien actua con error grande = la rampa (2026-06-13).
                        // reverse subido 0.05/0.4 -> 0.12/0.7: el 0.05 daba throttle ~0.22, muy debil
                        // para vehiculos pesados (UAZ 2860kg se clavaba a 0.5km/h en plano, 2026-06-23).
                        // El P-controller se auto-regula (throttle Ã¢Ë†Â speedErr), asi que livianos no lurchan.
                        if (target.mode == "reverse") { gainPk = 0.12; capPk = 0.7; }
                        else                          { gainPk = 0.08; capPk = 0.4; }
                        throttle = speedErrPk * gainPk;
                        if (throttle > capPk) throttle = capPk;
                        if (throttle < 0.10) throttle = 0.10;
                        brake = 0;
                    } else if (speedErrPk < -1.0) {
                        // Frenado escalado por magnitud de overspeed:
                        //   - overspeed >5 km/h: cap 0.6 (replica frenazo humano fuerte
                        //     al entrar a parking desde cruise Ã¢â‚¬â€ validado AI log 2026-05-31:
                        //     humano hacia brake=1 al entry, Boris con cap 0.2 quedaba lento
                        //     y entraba a la curva 90Ã‚Â° demasiado rapido Ã¢â€ â€™ overshoot 8m).
                        //   - overspeed 1-5 km/h: cap 0.2 (suave, no slam) Ã¢â‚¬â€ micro ajustes
                        //     normales dentro de la maniobra.
                        float overspeedPk = -speedErrPk;
                        if (overspeedPk > 5.0) {
                            brake = overspeedPk * 0.08;
                            if (brake > 0.6) brake = 0.6;
                        } else {
                            brake = overspeedPk * 0.05;
                            if (brake > 0.2)  brake = 0.2;
                            if (brake < 0.05) brake = 0.05;
                        }
                        throttle = 0;
                    } else {
                        // Dentro de banda +-1 km/h: coast.
                        // En reverse: 0.12 (vs 0.18 prev y 0.10 forward). Para
                        // reverse el 0.18 aceleraba sobre el cap 6 generando
                        // tap-tap a 2-8 km/h. Con 0.12 + cap=6, bot mantiene
                        // velocidad cerca del target sin overshoot.
                        if (target.mode == "reverse") throttle = 0.12;
                        else                          throttle = 0.10;
                        brake = 0;
                    }
                }
                } // fin del else del HONOR RECORDING en maniobra
            } else {
                throttle = target.targetThrottle;
                brake    = target.targetBrake;
            }

            // === SLOPE COMPENSATION en maniobra/parking ===
            // 2026-06-07: el P controller del bloque parking/maniobra no compensa
            // pendiente (a diferencia del InverseModel que aplica en mode=normal).
            // Caso real Impreza 2da curva post-parking: cap 8 km/h + subida ~1Ã‚Â° Ã¢â€ â€™
            // Boris pierde momentum mid-curva y se clava en wp 1096 hasta AR
            // rescatarlo. Fix: agregar throttle proporcional al pitch en subida.
            // No tocar bajada (gravedad ayuda; bajar throttle ahi puede romper
            // cap de velocidad en parking).
            if ((target.mode == "maniobra" || target.mode == "parking") && m_Config && m_Config.SlopeCompensationEnabled && m_PathPitch && throttle > 0.05 && brake < 0.05) {
                float pitchRadMan = GetEffectivePitch(m_WaypointIndex, m_Config.SlopeLookaheadWps);
                if (pitchRadMan > 0.005) { // umbral ~0.3Ã‚Â° solo subida
                    float throttleDeltaMan = (Math.Sin(pitchRadMan) * 9.81 / 4.0) * m_Config.SlopeGain;
                    throttle = throttle + throttleDeltaMan;
                    if (throttle > 1.0) throttle = 1.0;
                }
            }

            // === SLOPE COMP REVERSE (Sonom4n 2026-06-24): trepar la cuesta del galpon ===
            // El slope comp de arriba es solo parking/maniobra -> en reverse NUNCA se
            // aplicaba. En la subida del galpon (~8deg) el P-controller suelta el throttle
            // al tocar el target y la gravedad frena a Boris -> patina/retrocede (data
            // 2026-06-24, todas las tomas trepaban lento + retrocediendo). Idea de Sonom4n
            // ("mas throttle cuando se inclina el terreno"): throttle += pendiente en
            // reverse, para trepar parejo SIN subir la velocidad (mantiene la alineacion
            // lograda a 3 km/h). Gain 2.0 (la reversa trepa con menos traccion efectiva).
            if (target.mode == "reverse" && m_Config && m_Config.Waypoints && brake < 0.05) {
                // Pitch sobre SPAN LARGO (wp+20), NO GetEffectivePitch: a 3 km/h los wps
                // de la cuesta estan a ~7cm -> el guard dxz<0.1 de PrecomputePathPitch los
                // zera -> pitch ~0 -> boost nulo (bug 2026-06-24, throttle oscilaba 0.1/0.8).
                // Calculamos dy/dxz sobre 20 wps (~1.4m) para captar los 8deg reales del galpon.
                int aheadIdxR = Math.Min(m_WaypointIndex + 20, m_Config.Waypoints.Count() - 1);
                vector pNowR = m_Config.Waypoints[m_WaypointIndex].GetVector();
                vector pAheadR = m_Config.Waypoints[aheadIdxR].GetVector();
                float dyR = pAheadR[1] - pNowR[1];
                float dxR = pAheadR[0] - pNowR[0];
                float dzR = pAheadR[2] - pNowR[2];
                float dxzR = Math.Sqrt(dxR*dxR + dzR*dzR);
                if (dxzR > 0.5 && dyR > 0.05) {
                    float pitchRevR = Math.Atan2(dyR, dxzR);
                    float slopeAddRev = (Math.Sin(pitchRevR) * 9.81 / 4.0) * 2.0;
                    throttle = throttle + slopeAddRev;
                    if (throttle > 1.0) throttle = 1.0;
                }
            }

            // === ANTI-ROLLBACK EN PENDIENTE (parking/maniobra) ===
            // 2026-06-08: en velocidad ~0 km/h en pendiente, SetBrake solo no alcanza Ã¢â‚¬â€
            // Enfusion permite micro-slip del eje trasero. SetHandbrake bloquea la
            // friccion del eje en SimulationModule. Combinacion handbrake+brake clava
            // el vehiculo al piso. Para arrancar en subida, se mantiene el lock hasta
            // que EngineGetRPM cruce rpmClutch (punto donde el embrague tiene torque
            // suficiente para vencer la gravedad Ã¢â‚¬â€ soltar antes = rollback).
            // Documentado por Sonom4n 2026-06-08 (doc parking).
            float handbrakeOut = 0; // default: handbrake off
            if ((target.mode == "parking" || target.mode == "maniobra") && m_Config && m_Config.AntiRollbackEnabled && m_PathPitch) {
                float pitchAR = GetEffectivePitch(m_WaypointIndex, m_Config.SlopeLookaheadWps);
                float arThreshold = m_Config.AntiRollbackPitchThreshold;
                if (arThreshold <= 0) arThreshold = 0.05;
                if (pitchAR > arThreshold && kmh < 0.5) {
                    // Auto casi quieto en pendiente positiva
                    if (throttle < 0.1) {
                        // No quiere avanzar Ã¢â€ â€™ HOLD posicion absoluto
                        handbrakeOut = 1.0;
                        brake = 1.0;
                        throttle = 0;
                        // Reset integral del PID para evitar windup (cuando arranque,
                        // sino sale disparado)
                        if (m_InverseModel) m_InverseModel.ResetPID();
                    } else {
                        // Quiere arrancar Ã¢â€ â€™ mantener brakes hasta que RPM cruce rpmClutch
                        float rpmAR = bus.EngineGetRPM();
                        float rpmClutchAR = m_CachedRpmClutch;
                        if (rpmClutchAR <= 0) rpmClutchAR = 2000;
                        if (rpmAR < rpmClutchAR) {
                            // Embrague aun no tiene torque - hold brakes, throttle ramp
                            handbrakeOut = 1.0;
                            brake = 1.0;
                            // throttle queda como lo computo el P controller (>0.1)
                        } else {
                            // RPM cruzo - embrague muerde, liberar para salir limpio
                            handbrakeOut = 0;
                            brake = 0;
                        }
                    }
                }
            }
            SetCachedHandbrake(handbrakeOut);

            // Clamps de seguridad
            if (throttle > 1.0)  throttle = 1.0;
            if (throttle < 0.0)  throttle = 0.0;
            if (brake > 1.0)     brake = 1.0;
            if (brake < 0.0)     brake = 0.0;
    }
    private void DriveTowards(Car bus, BZWaypoint target) {
        m_FastSteerActive = false;   // default OFF cada tick; solo el bloque pure-pursuit forward lo prende
        vector targetPos = target.GetVector();
        vector busPos = bus.GetPosition();
        vector toTarget = targetPos - busPos;
        float dist = toTarget.Length();
        if (dist < 0.01) return;

        // === SPAWN HANDBRAKE HOLD (2026-06-09) ===
        // Al spawn, handbrake on por 1.5s para estabilizar el vehiculo en pendiente
        // (gravedad pre-Boris-input). Despues del timer, el resto de la logica
        // (anti-rollback, end-of-route, etc) toma el control. Timer en vez de "kmh>1"
        // para evitar deadlock: si dejamos brake on hasta que se mueva, nunca se mueve.
        if (m_SpawnHoldActive) {
            float spawnElapsed = GetGame().GetTickTime() - m_SpawnHoldTime;
            if (spawnElapsed < 1.5) {
                SetCachedHandbrake(1.0);
                SetCachedInput(0, 0, 1.0);
                return;
            }
            m_SpawnHoldActive = false;
            BZBusLog.Info("[SpawnHold] Released after " + spawnElapsed + "s");
        }

        // === HONOR HANDBRAKE GRABADO (2026-06-12, Sonom4n) ===
        // Si el humano apreto el HANDBRAKE en este wp = seÃƒÂ±al explicita de STOP/transicion
        // (parar antes de meter reversa, o cualquier parada arbitraria). Boris se DETIENE:
        // handbrake on + throttle 0 + brake. Override de input LIMPIO Ã¢â‚¬â€ NO toca la logica del
        // cruise. El wp avanza por proximidad por el bloque handbrake -> sigue (o entra a reverse).
        // Reemplaza los stops rigidos de 2s: el modder graba el stop natural y Boris lo replica.
        // Honor SOLO en forward (mode != reverse). En reverse el reverse block maneja, y
        // el humano a veces deja el handbrake puesto durante la maniobra -> no frenar ahi.
        if (target.targetHandbrake > 0.5 && target.mode != "reverse") {
            SetCachedHandbrake(1.0);
            SetCachedInput(0, 0, 1.0);
            // RESUME determinista: una vez DETENIDO, saltar a la primera wp reverse adelante
            // (la transicion handbrake esta justo antes de la reversa). Sin esto Boris se
            // clavaba en el handbrake forever (el avance por proximidad no progresa parado,
            // tomas 3/4 del Golf 2026-06-12). Si no hay reverse adelante, queda como stop.
            if (bus.GetSpeedometerAbsolute() < 1.5) {
                // Saltar el cluster de handbrake-stop FORWARD hasta el proximo wp que sea REVERSE
                // (ENTRADA a reversa) o forward SIN handbrake (SALIDA de reversa / stop cualquiera).
                // FIX 2026-07-02: antes solo buscaba reverse -> en la SALIDA de reversa (sin reverse
                // adelante) el scan llegaba al final, no saltaba, y Boris quedaba clavado en el
                // handbrake-stop forever (gear 0 + brake 1, deadlock validado ai_run M3 Sedan_02).
                int jr = m_WaypointIndex + 1;
                while (jr < m_Config.Waypoints.Count() && m_Config.Waypoints[jr].mode != "reverse" && m_Config.Waypoints[jr].targetHandbrake > 0.5) jr++;
                if (jr < m_Config.Waypoints.Count() && jr > m_WaypointIndex) {
                    m_WaypointIndex = jr;
                    BZBusLog.Info("[Handbrake] Detenido -> resume a wp " + jr + " (mode=" + m_Config.Waypoints[jr].mode + ")");
                }
            }
            return;
        }

        // === MODE ENTRY/EXIT SNAP ===
        // 2026-06-07: cuando Boris cruza un boundary mode (entra o sale de maniobra/parking/reverse)
        // y esta muy cerca del wp (< MaxDist), snap a la posicion + heading exacta del recording.
        // Imperceptible visualmente a baja velocidad (parking/maniobra cap < 20 km/h, snap < 0.5m).
        // Garantiza entry/exit consistente, critico para parking en espacios chicos donde
        // 0.5m de drift puede romper la maniobra (puerta de casa, galpon).
        // Si dist > MaxDist, no snap Ã¢â‚¬â€ AR/cruise debe acercarlo primero.
        if (m_Config && m_Config.ModeEntrySnapEnabled && target.mode != m_PrevTickMode) {
            bool currentIsSpecial = (target.mode == "maniobra" || target.mode == "parking" || target.mode == "reverse");
            bool prevWasSpecial = (m_PrevTickMode == "maniobra" || m_PrevTickMode == "parking" || m_PrevTickMode == "reverse");
            // Snap si: entra a special (currentIsSpecial) o sale de special (prevWasSpecial)
            if (currentIsSpecial || prevWasSpecial) {
                float snapMaxDist = m_Config.ModeEntrySnapMaxDist;
                if (snapMaxDist <= 0) snapMaxDist = 0.5;
                if (dist < snapMaxDist) {
                    vector snapPos = targetPos;
                    snapPos[1] = busPos[1];
                    bus.SetPosition(snapPos);
                    if (m_Config.Waypoints) {
                        // Heading: buscar un wp adelante a >0.5m (saltar los densos del creep/reverse,
                        // que dan atan2~0 y dejaban el heading sobre-rotado de la aproximacion).
                        // En reverse encarar OPUESTO al sentido de marcha (la cola lidera).
                        vector snapDirP = "0 0 0"; bool snapFoundP = false;
                        int snapEndP = Math.Min(m_WaypointIndex + 60, m_Config.Waypoints.Count());
                        for (int siP = m_WaypointIndex + 1; siP < snapEndP; siP++) {
                            vector spP = m_Config.Waypoints[siP].GetVector();
                            float sdxP = spP[0] - snapPos[0]; float sdzP = spP[2] - snapPos[2];
                            if (sdxP*sdxP + sdzP*sdzP > 0.25) { snapDirP = Vector(sdxP, 0, sdzP); snapFoundP = true; break; }
                        }
                        // HEADING DEL SNAP (Sonom4n 2026-06-24):
                        // - REVERSE con heading grabado: orientar al facing EXACTO del humano. La
                        //   geometria (hacia el proximo wp +180) se torcia ~15-24deg porque la reversa
                        //   curva apenas arranca, y el approach de Boris quedaba ~7-12 off -> latd
                        //   0.6-0.9 -> puerta derecha. El recorded es la verdad -> arranca derecho.
                        // - REVERSE sin heading grabado (rutas viejas): NO re-orientar (mantiene su
                        //   heading actual, que el creep/approach dejo ~ok).
                        // - parking/maniobra: geometria (el teleport puede dejarlos facing cualquier lado).
                        if (target.mode == "reverse" && target.targetHeading != 0) {
                            bus.SetOrientation(Vector(target.targetHeading, 0, 0));
                        } else if (snapFoundP && target.mode != "reverse") {
                            float snapHeadingDeg = Math.Atan2(snapDirP[0], snapDirP[2]) * Math.RAD2DEG;
                            bus.SetOrientation(Vector(snapHeadingDeg, 0, 0));
                        }
                    }
                    // STOP LIMPIO (Sonom4n 2026-06-24): Boris llega coasteando (no frena) y se la
                    // da contra la pared -> queda con velocidad residual DISTINTA cada vuelta,
                    // que el snap (pos+heading) no limpiaba -> la reversa arrancaba sucia y el
                    // heading divergia. Matamos el coast (J = -m*v) al entrar a la maniobra:
                    // arranque desde CERO, consistente, como cuando el humano frena y para muerto.
                    if (currentIsSpecial) {
                        vector snapVel = GetVelocity(bus);
                        float snapMass = dBodyGetMass(bus);
                        if (snapMass > 0) dBodyApplyImpulse(bus, snapVel * (-snapMass));
                        // 2026-06-25: reset de volante al entrar reversa. El snap limpia pos+heading+vel
                        // pero NO el volante -> arrancaba girado ~-0.08 (residual del approach). Forzamos
                        // steering=0 los primeros ticks (override abajo en el cap de reversa) -> arranque centrado.
                        if (target.mode == "reverse") m_ReverseEntrySteerReset = 3;
                    }
                    string snapKind = "ENTRY";
                    if (prevWasSpecial && !currentIsSpecial) snapKind = "EXIT";
                    BZBusLog.Info("[ModeSnap-" + snapKind + "] wp=" + m_WaypointIndex + " prev=" + m_PrevTickMode + " curr=" + target.mode + " dist=" + dist + "m Ã¢â€ â€™ snapped");
                }
            }
        }
        m_PrevTickMode = target.mode;

        // === AUTO-RECOVERY: detectar stuck y teleportar adelante ===
        // Garantiza spatial-fidelity absoluta (Boris siempre llega) y limpia las tomas
        // (sin necesidad de asistencia humana con H). Cada evento logged para pattern analysis.
        if (m_Config && m_Config.AutoRecoveryEnabled) {
            float arNow = GetGame().GetTickTime();
            float arKmh = bus.GetSpeedometerAbsolute();

            // Track wp progress
            if (m_WaypointIndex != m_AR_LastWpIdx) {
                m_AR_LastWpIdx = m_WaypointIndex;
                m_AR_LastWpProgressTime = arNow;
            }

            // Acumular stuck timer: velocidad baja Y wp_idx no avanza
            bool isLowSpeed = (arKmh < 1.0);
            float noProgressTime = arNow - m_AR_LastWpProgressTime;
            bool noProgress = (noProgressTime > m_Config.AutoRecoveryStuckTimeS);

            // Triggers para teleport
            bool triggerTeleport = false;
            string triggerReason = "";
            if (isLowSpeed && noProgress) {
                triggerTeleport = true;
                triggerReason = "stuck_lowspeed_noprogress";
            } else if (arKmh < 3.0 && noProgress && noProgressTime > m_Config.AutoRecoveryStuckTimeS * 1.5) {
                // Inclusive si velocidad > 0 pero no avanza wps (bus girando en circulo, off-path lejano)
                // 2026-06-07 fix: agregada condicion arKmh < 3 para NO disparar cuando Boris
                // esta avanzando fisicamente pero el wp_idx no progresa por cluster de wps
                // del recording (zona donde el humano grabo wps muy densos). Caso real
                // Impreza wp 564-585: humano grabo 20+ wps en 2m, Boris avanzaba 100m/15s
                // pero wp_idx solo subia 6 Ã¢â€ â€™ AR teleportaba en loop sin sentido.
                triggerTeleport = true;
                triggerReason = "stuck_circling_noprogress";
            }

            // Cooldown
            if (arNow - m_AR_LastTime < m_Config.AutoRecoveryCooldownS) {
                triggerTeleport = false;
            }

            // Limite por mision
            if (m_Config.AutoRecoveryMaxPerMission > 0 && m_AR_Count >= m_Config.AutoRecoveryMaxPerMission) {
                triggerTeleport = false;
            }

            // En mode=parking/maniobra, AR distance-based en vez de time-based.
            // Si Boris esta cerca del wp logico (< 5m), detencion/maniobra legitima Ã¢â€ â€™ NO AR.
            // Si Boris esta lejos del wp logico (> 5m), se fue del path Ã¢â€ â€™ AR dispara.
            // 2026-06-07: Impreza validation Ã¢â‚¬â€
            //   parking: activado en movimiento puede dejar a Boris "planchado contra
            //     contencion" a 16m del wp. Sin AR queda stuck forever.
            //   maniobra: si Boris no logra el radio de curva 90Ã‚Â°, sigue derecho y wp_idx
            //     no avanza porque siguientes wps quedan 26m+ lateral. AR time-based no
            //     dispara (Boris avanza fisicamente). Distance-based si.
            // Distance-based captura ambos casos sin romper detenciones/maniobras legitimas
            // (que mantienen dist < 5m del wp).
            if (m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count()) {
                string curModeAr = m_Config.Waypoints[m_WaypointIndex].mode;
                if (curModeAr == "parking" || curModeAr == "maniobra") {
                    vector wpPosAr = m_Config.Waypoints[m_WaypointIndex].GetVector();
                    vector busPosAr = bus.GetPosition();
                    float dxAr = busPosAr[0] - wpPosAr[0];
                    float dzAr = busPosAr[2] - wpPosAr[2];
                    float distToWpAr = Math.Sqrt(dxAr*dxAr + dzAr*dzAr);
                    if (distToWpAr < 5.0) {
                        triggerTeleport = false;
                    } else if (distToWpAr > 25.0 && noProgressTime > m_Config.AutoRecoveryStuckTimeS) {
                        // 2026-06-07 fix: off-path en parking/maniobra. Cuando Boris no logra
                        // el radio de curva 90Ã‚Â° (full lock + AWD + bajada), sigue derecho a
                        // 15-28 km/h. wp_idx clavado, lateral_dev explota 1Ã¢â€ â€™70m. El guard
                        // arKmh<3 del stuck_circling no dispara. Distance-based ACTIVA aca.
                        // Distingue del cluster (donde Boris esta <5m del wp logico).
                        triggerTeleport = true;
                        triggerReason = "offpath_parking_maniobra";
                    }
                }
            }

            if (triggerTeleport) {
                int currentWp = m_WaypointIndex;
                int targetWp = currentWp + m_Config.AutoRecoveryAdvanceWps;
                int totalWps = m_Config.Waypoints.Count();
                if (targetWp >= totalWps) targetWp = totalWps - 1;
                if (targetWp <= currentWp) targetWp = currentWp + 1;

                vector teleportPos = m_Config.Waypoints[targetWp].GetVector();
                teleportPos[1] = teleportPos[1] + 0.5;  // levantar un poco para evitar clipping

                // Heading basado en segmento siguiente. En REVERSE el vehiculo mira OPUESTO al avance:
                // sin el +180 el AR lo suelta mirando al reves dentro del rulo de reversa -> se re-traba ->
                // loop infinito (medido M3 T2, 2026-07-17). Reverse-aware: heading+180, gear=0, sin impulse fwd.
                bool arTargetReverse = (m_Config.Waypoints[targetWp].mode == "reverse");
                float teleHeadingDeg = 0;
                if (targetWp + 1 < totalWps) {
                    vector arNextP = m_Config.Waypoints[targetWp + 1].GetVector();
                    teleHeadingDeg = Math.Atan2(arNextP[0] - teleportPos[0], arNextP[2] - teleportPos[2]) * Math.RAD2DEG;
                }
                if (arTargetReverse) teleHeadingDeg = teleHeadingDeg + 180.0;

                // Teleport via APIs ya validadas en framework
                bus.SetPosition(teleportPos);
                bus.SetOrientation(Vector(teleHeadingDeg, 0, 0));

                // Preservar velocidad: dar a Boris la targetSpeed del wp destino
                // via impulse (Newton: J = m * dv). Sin esto Boris arrancaba de 0
                // post-teleport y nunca catcheaba al recording Ã¢â€ â€™ cascada de offpath.
                // En reverse NO impulse: la vel es baja y la direccion se complica -> que arranque por control.
                // ANULADO (2026-07-20, Sonom4n): "el AR dejalo en 0 km/h, que no se traslade ya con velocidad".
                // El timing NO es un objetivo: un AR es un evento impredecible y lo unico que importa es
                // que Boris quede bien PUESTO. Soltarlo ya lanzado lo metia con velocidad en un tramo que
                // nunca negocio. Ahora aparece DETENIDO y acelera con su propio control.
                vector arKillVel = GetVelocity(bus);
                float arMass = dBodyGetMass(bus);
                if (arMass > 0) dBodyApplyImpulse(bus, arKillVel * (-1.0 * arMass));

                // Resetear gear y estado interno: reverse -> gear 0, forward -> gear 2 (FIRST)
                if (arTargetReverse) { SetDesiredGear(0); bus.ShiftTo(0); }
                else { SetDesiredGear(2); bus.ShiftTo(2); }
                if (m_InverseModel) m_InverseModel.ResetPID();

                // Actualizar tracking
                m_WaypointIndex = targetWp;
                m_AR_LastWpIdx = targetWp;
                m_AR_LastWpProgressTime = arNow;
                m_AR_LastTime = arNow;
                m_AR_Count++;
                m_OffPath_InRecovery = false;
                m_DR_InRecovery = false;

                BZBusLog.Info("[AUTO-RECOVERY #" + m_AR_Count + "] reason=" + triggerReason + " wp " + currentWp + " Ã¢â€ â€™ " + targetWp + " (pos=" + teleportPos.ToString() + " heading=" + teleHeadingDeg + "Ã‚Â°)");
                return;  // skip this tick
            }
        }

        // === INTERPOLACION TEMPORAL DE INPUTS (Paso 2) ===
        // El playback con waypoint discreto (cada Tick lee el wp mas cercano)
        // pierde la resolucion del recording (10 samples/s con SAMPLE 100ms).
        // Aca proyectamos la posicion del bus sobre el segmento current->next y
        // calculamos un "progreso" 0..1 para lerp lineal de throttle/brake/speed.
        //
        // Resultado: aunque el Tick siga a 500ms, los inputs que aplicamos son
        // continuos como si el bus leyera el recording a cualquier momento.
        // Suaviza transiciones (modulacion de throttle al acercarse a parada,
        // micro-correcciones de steering en curvas).
        float iThrottle = target.targetThrottle;
        float iBrake    = target.targetBrake;
        float iSpeed    = target.targetSpeed;

        // [AUDITORIA 26/07] eliminada la INTERPOLACION temporal de inputs (replay Modo1): gateada por
        // hasInputData=0 en todo el contexto nuevo (Boris DERIVA los inputs). iThrottle/iBrake/iSpeed quedan
        // = target.* (base, arriba), que es lo que el flujo vivo usa.

        // Cache de los valores interpolados para que LogAITick los pueda escribir.
        m_LastIThrottle = iThrottle;
        m_LastIBrake    = iBrake;
        m_LastISpeed    = iSpeed;

        // Pure pursuit con lookahead ADAPTATIVO: en rectas usamos LOOKAHEAD_DIST
        // (10m) para suavidad. En curvas pronunciadas (zigzag, rotonda) reducimos
        // a LOOKAHEAD_DIST_MIN (5m) para que el bus no "corte" la curva apuntando
        // a un punto que ya esta del otro lado. La curvatura local se mide como
        // suma de cambios absolutos de heading en los proximos WINDOW waypoints.
        // El analisis de 3 tomas IA vs grabacion humana identifico 8 wp criticos
        // (zigzag wp 543/605, rotonda Cherno wp 1557-1577) donde el lookahead
        // fijo causaba desviacion de 2-5m del trazo humano.
        float adaptiveLookahead = ComputeAdaptiveLookahead();
        vector lookahead = ComputeLookahead(busPos, adaptiveLookahead);
        vector toLookahead = lookahead - busPos;

        // v1.0: el bus frena en CADA parada, sin chequear pasajeros
        bool willStop = target.isStop;

        // Heading actual del bus: usar GetOrientation() (valor controlado por SetOrientation())
        // en vez de GetDirection() Ã¢â‚¬â€ el segundo puede lagear al render/physics y devolver
        // la direccion default del modelo en los primeros ticks post-spawn, causando que
        // pure pursuit calcule dYaw=180 y haga full turn al arrancar.
        vector busOrient = bus.GetOrientation();
        float currentYaw = busOrient[0] * Math.DEG2RAD;

        // === CORREDOR (Stanley controller simplificado) ===
        // targetYaw = segmentHeading - atan(K * lateralOffset / velocity)
        // Correccion proporcional al offset (sin deadband), atenuada por velocidad.
        // Replica el patron del operador humano (a-a-a-d-d-d, tap-tap continuo).
        // En cada tick aplicamos una correccion leve proporcional al offset
        // actual; nunca un volantazo discreto. Bonus: a alta velocidad el
        // divisor por v_ms reduce la correccion automaticamente (atan tiende
        // a 0), evitando zigzag en rectas. A baja velocidad la correccion
        // crece (atan tiende al limite), util en maniobras precisas (rotonda,
        // ajuste al stop).
        //
        // Si no hay segmento valido (bus muy lateral fuera del trazado, p.ej.
        // trabado contra una estructura), fallback a pure pursuit clasico.
        // Punto de control del corridor: en reverse el eje TRASERO lidera el movimiento (el
        // eje de direccion delantero trailea), asi que referenciamos el corridor al eje
        // trasero (busPos - fwd*offset), no al centro. Sino el centro se ve on-track mientras
        // el punto que lidera esta off -> Boris reversea torcido y Stanley "no ve" el error
        // (Sonom4n 2026-06-12, ai_run: corridor 0.31m vs latdev_real 2.09m). currentYaw ya arriba.
        vector corridorRefPos = busPos;
        if (target.mode == "reverse") {
            vector fwdRef = Vector(Math.Sin(currentYaw), 0, Math.Cos(currentYaw));
            corridorRefPos = busPos - fwdRef * GetReverseControlOffset();
        }
        ComputeCorridorInfo(corridorRefPos);
        // LEARNER (gancho 2/4): observa el cross-track HONESTO -offset firmado contra el wp que Boris
        // ESTA siguiendo, exacto, sin closest-point- para promediar el sistematico entre vueltas.
        if (m_CorridorValid && m_Config.CorridorLearnerEnabled) BZCorridorLearner.GetInstance().Observe(m_WaypointIndex, busPos);
        float kmhForStanley = bus.GetSpeedometerAbsolute();
        float vForStanley = kmhForStanley / 3.6;
        // V-FLOOR REVERSE (Sonom4n 2026-06-24): en reversa pisar la velocidad usada por Stanley a un
        // minimo mas alto. La correccion = K*offset/v EXPLOTA a baja v (1/v) -> sobre-corrige ->
        // zigzag -> steer-then-throttle corta acelerador -> mas lento -> mas zigzag = ESPIRAL DE
        // LA MUERTE (las que entran reversan ~5km/h, las que fallan ~2-3). Pisar v mantiene la
        // correccion suave aunque Boris se frene -> rompe la espiral. Tuneable por _hdr.
        float vFloorStan = 1.0;
        if (target.mode == "reverse") vFloorStan = GetReverseStanleyMinSpeed();
        if (vForStanley < vFloorStan) vForStanley = vFloorStan; // safety + anti-espiral reverse
        float targetYaw;
        // REVERSE MODE: estado separado del recording (toggle NUMPAD - durante
        // grabacion). El bot debe FACE opuesto al motion direction Ã¢â€ â€™ flip
        // target_yaw por PI. Detection por mode field, no por gear field Ã¢â‚¬â€
        // mantiene parking intacto y separa cleanly las semanticas.
        bool isReversePk = ActiveLegIsReverse();   // era target.mode: el indice se filtra a la reversa antes del checkpoint
        bool cuspExit = CuspExitActive(kmhForStanley, isReversePk, target.mode);   // banda de salida reversa->forward

        if (m_CorridorValid) {
            // Convencion de signo: cross product > 0 = bus a la derecha del
            // segmento. Para volver al centro hay que girar a la izquierda, que
            // en yaw DayZ (0=norte, 90=este, creciente clockwise) significa
            // RESTAR del segmentHeading. De ahi el signo negativo en atan.
            // Parking mode: K agresivo (3.0 vs 1.0) para precision quirurgica
            // en maniobras finas a baja velocidad.
            float kStanley = STANLEY_K;
            if (target.mode == "parking")  kStanley = GetParkingStanleyK();
            if (target.mode == "maniobra") kStanley = GetParkingStanleyK(); // maniobra usa misma K agresiva que parking
            // reverse usa K gentil per-vehiculo (default 0.8). K alto SOBRE-CORRIGE en reverse
            // (el eje de direccion trailea). El fix real de precision es el CONTROL-POINT trasero
            // (ver ComputeCorridorInfo con punto trasero en reverse, Sonom4n 2026-06-12).
            if (target.mode == "reverse")  kStanley = GetReverseStanleyK();

            // === "ParedÃƒÂ³n" Ã¢â‚¬â€ deadband + K-gain + D-damp anti-zigzag (cruise normal only) ===
            // Reduce zigzag de baja amplitud: cuando Boris esta cerca del path (lat_dev pequeÃƒÂ±o)
            // no perturba, dejando que la inercia lo mantenga. Cuando se aleja, aplica Stanley
            // normal. El D-term penaliza correcciones bruscas (anti-overshoot).
            // Paredon (deadband + KGain + Damp) opera sobre el offset RAW Ã¢â‚¬â€
            // absorbe zigzag reactivo de baja amplitud cerca del path.
            float adjustedOffset = m_CorridorLateralOffset;
            bool isNormalCruise = (target.mode == "normal" || target.mode == "" || target.mode == "approach"); // approach maneja igual que cruise normal (solo cambia la velocidad)
            if (isNormalCruise && m_Config) {
                // 1. Deadband Ã¢â‚¬â€ CORREDOR-BANDA (2026-07-08): si el wp trae corridorHalfWidth>0, ese es el
                // ancho del corredor AHI (angosto en recta = mantiene carril; ANCHO en el nodo = room para
                // que el vehiculo arquee SU curva segun su fisica, no una linea impuesta). Si no, la constante.
                float deadband = m_Config.CruiseLateralDeadband;
                if (m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count()) {
                    float wpCorrDb = m_Config.Waypoints[m_WaypointIndex].corridorHalfWidth;
                    if (wpCorrDb > 0) deadband = wpCorrDb;
                }
                if (deadband > 0) {
                    float pdAbsOffset = adjustedOffset;
                    if (pdAbsOffset < 0) pdAbsOffset = -pdAbsOffset;
                    if (pdAbsOffset < deadband) {
                        adjustedOffset = 0;
                    } else {
                        float ofSign = 1.0;
                        if (adjustedOffset < 0) ofSign = -1.0;
                        adjustedOffset = ofSign * (pdAbsOffset - deadband);
                    }
                }
                // 2. K-gain (multiplicador adicional al STANLEY_K)
                float kGain = m_Config.CruiseLateralKGain;
                if (kGain > 0 && kGain != 1.0) adjustedOffset = adjustedOffset * kGain;
                // 3. D-term (damping anti-zigzag, opera sobre offset RAW no el adjusted)
                float kDamp = m_Config.CruiseLateralDamp;
                if (kDamp > 0) {
                    float dOffset = m_CorridorLateralOffset - m_LastCorridorOffset;
                    adjustedOffset = adjustedOffset + kDamp * dOffset;
                }
            }
            m_LastCorridorOffset = m_CorridorLateralOffset;
            // I-TERM ANTI-DRIFT REVERSE (Sonom4n 2026-06-25): el drift de reverse es error de regimen
            // permanente (full-locks grabados capeados empujan parejo); el Stanley P con cap bajo no
            // lo mata (queda offset ~0.63). El integral acumula el offset persistente y rampa una
            // correccion gentil hasta 0 Ã¢â‚¬â€ sin el overshoot de subir la P. Solo reverse; resetea
            // fuera de reverse. ReverseLateralKi=0 => off (opt-in, tuneable por _hdr).
            if (isReversePk && GetReverseLateralKi() > 0) {
                m_RevLatIntegral = m_RevLatIntegral + m_CorridorLateralOffset;
                if (m_RevLatIntegral > 200.0)  m_RevLatIntegral = 200.0;
                if (m_RevLatIntegral < -200.0) m_RevLatIntegral = -200.0;
                adjustedOffset = adjustedOffset + GetReverseLateralKi() * m_RevLatIntegral;
            } else {
                m_RevLatIntegral = 0;
            }
            // CenterOffset se aplica DESPUES del paredon: es un bias fijo del vehiculo,
            // no un desvio reactivo. Debe escapar del deadband para no quedar anulado
            // dentro del corredor. Stanley ve "Boris esta shift m fuera de su centro logico".
            // SLOPE-AWARE LATERAL 2026-06-07: bias asimetrico segun pitch.
            // Impreza validado: bajada centrado, plano -0.7m izq, subida -1.4m izq.
            // En subida agregamos offset positivo (Boris a derecha), en bajada negativo.
            float effectiveCenterOffset = 0;
            if (m_Config) effectiveCenterOffset = m_Config.CruiseLateralCenterOffset;
            if (m_Config && m_Config.SlopeLateralGain != 0.0 && m_PathPitch && m_PathPitch.Count() > 0) {
                float pitchRadSl2 = GetEffectivePitch(m_WaypointIndex, m_Config.SlopeLookaheadWps);
                float pitchDegSl2 = pitchRadSl2 * Math.RAD2DEG;
                // 2026-06-07 fix: SOLO aplicar en SUBIDA (pitch > 1Ã‚Â°). En bajada
                // y plano Boris no necesita compensacion (datos Impreza: bajada
                // bias natural +0.04 centrado, plano -0.65, subida -1.32). Aplicar
                // slope en bajada empujaba mas a la izquierda y EMPEORABA el bias.
                if (pitchDegSl2 > 1.0) {
                    float slopeBiasSl = (pitchDegSl2 / 70.0) * m_Config.SlopeLateralGain;
                    effectiveCenterOffset = effectiveCenterOffset + slopeBiasSl;
                }
            }
            if (effectiveCenterOffset != 0.0) {
                adjustedOffset = adjustedOffset - effectiveCenterOffset;
            }

            // STANLEY CURVATURA-AWARE (idea Sonom4n 2026-07-06): en curva subir K hacia el de maniobra/parking
            // para que Stanley SIGA la curva en vez de cortarla (el corte es del lookahead + K debil a baja v).
            // Escala con la curvatura LOCAL del corredor: recta -> K normal (sin zigzag); curva -> K agresivo.
            // ORIENTA, no parcha: ley de control general (mas ganancia donde hay que doblar mas), cualquier ruta.
            if (isNormalCruise && m_Config && m_Config.StanleyCurvatureAware && m_Config.Waypoints && m_Config.Waypoints.Count() > 12) {
                int cwN = m_Config.Waypoints.Count();
                int cca = m_WaypointIndex - 6; if (cca < 0) cca = 0;
                int ccb = m_WaypointIndex + 6; if (ccb >= cwN) ccb = cwN - 1;
                vector ccpa = m_Config.Waypoints[cca].GetVector();
                vector ccpm = m_Config.Waypoints[m_WaypointIndex].GetVector();
                vector ccpb = m_Config.Waypoints[ccb].GetVector();
                float cch1 = Math.Atan2(ccpm[0] - ccpa[0], ccpm[2] - ccpa[2]);
                float cch2 = Math.Atan2(ccpb[0] - ccpm[0], ccpb[2] - ccpm[2]);
                float ccdh = cch2 - cch1;
                while (ccdh > Math.PI)  ccdh = ccdh - 2 * Math.PI;
                while (ccdh < -Math.PI) ccdh = ccdh + 2 * Math.PI;
                float curvFactor = Math.AbsFloat(ccdh) / 0.08;   // 0.08 rad/+-6wp = curva plena (EX05 pico 0.107)
                if (curvFactor > 1.0) curvFactor = 1.0;
                kStanley = STANLEY_K + (GetParkingStanleyK() - STANLEY_K) * curvFactor;
            }

            // EXIT-TIGHTEN (camino A): boost de K por offset lateral remanente -> re-aprieta en la salida de
            // curva donde la curvatura (y por ende K arriba) ya cayo. Cruise recto intacto (offset ~0).
            kStanley = ApplyExitTightenK(kStanley, adjustedOffset, kmhForStanley, isReversePk, isNormalCruise);

            // SOFTENING de Stanley: sumar k_soft a v en el denominador (forward) -> baja la ganancia
            // 1/v a baja velocidad y mata el zigzag al acelerar. Reverse usa su propio piso anti-espiral.
            float vStanleyDen = vForStanley;
            if (!isReversePk) vStanleyDen = vForStanley + GetStanleySoftening();
            float crossCorrection = Math.Atan2(kStanley * adjustedOffset, vStanleyDen);
            // FIX heading-grabado (Sonom4n 2026-06-24): en reversa encarar al facing GRABADO del wp
            // actual (~128 en la puerta) en vez de la tangente geometrica (segmentHeading+PI ~137).
            // El humano reversaba con el auto en diagonal al movimiento; la geometria sobre-rota
            // ~10 grados y Boris se va ancho. crossCorrection (offset lateral) se mantiene de base.
            bool hasRecHead = ((isReversePk || cuspExit) && m_Config && m_Config.Waypoints && m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count() && m_Config.Waypoints[m_WaypointIndex].targetHeading != 0);
            if (hasRecHead) {
                targetYaw = (m_Config.Waypoints[m_WaypointIndex].targetHeading * Math.DEG2RAD) - crossCorrection;
            } else {
                targetYaw = m_CorridorSegmentHeading - crossCorrection;
                // Reverse: el bot debe face opuesto al segment direction
                if (isReversePk) targetYaw = targetYaw + Math.PI;
            }
        } else if ((target.mode == "parking" || target.mode == "reverse" || target.mode == "maniobra") && m_Config) {
            // Fallback parking/reverse: el pure pursuit falla cuando los wps
            // cercanos estan clusterizados (humano casi parado, segmentos < 10cm
            // y proyeccion t fuera [0,1]) Ã¢â‚¬â€ Boris orbita el cluster. Mejor
            // caminar BACKWARDS desde wp_idx hasta encontrar un segmento con
            // length > 0.5m, usar SU heading. Le da a Stanley una direccion
            // estable basada en la trayectoria real del recording.
            float fallbackHeadingPk = 0;
            bool foundFbPk = false;
            // Solo 10 wps back Ã¢â‚¬â€ sino agarra un segmento muy lejano (ej. cruise
            // approach con heading totalmente distinto) y manda al bot a rotar
            // hacia esa direccion obsoleta. 10 wps mantiene heading local.
            int fbMinIdx = m_WaypointIndex - 10;
            if (fbMinIdx < 0) fbMinIdx = 0;
            for (int fbi = m_WaypointIndex; fbi > fbMinIdx; fbi--) {
                if (fbi + 1 >= m_Config.Waypoints.Count()) continue;
                vector fbAP = m_Config.Waypoints[fbi].GetVector();
                vector fbBP = m_Config.Waypoints[fbi + 1].GetVector();
                float fbDx = fbBP[0] - fbAP[0];
                float fbDz = fbBP[2] - fbAP[2];
                float fbLen2 = fbDx*fbDx + fbDz*fbDz;
                if (fbLen2 > 0.0025) { // length > 5cm. Aun con segmentos chicos
                    // (humano a 2 km/h hace 6cm por sample) el heading es
                    // consistente Ã¢â‚¬â€ eso le sirve a Stanley como direccion target.
                    // Validado wp 360-410: todos los segmentos 5-9cm con heading
                    // 123Ã‚Â° estable. Threshold de 50cm los excluia Ã¢â€ â€™ pure pursuit
                    // Ã¢â€ â€™ orbital. 5cm los incluye Ã¢â€ â€™ heading correcto Ã¢â€ â€™ Stanley OK.
                    fallbackHeadingPk = Math.Atan2(fbDx, fbDz);
                    foundFbPk = true;
                    break;
                }
            }
            if (foundFbPk) {
                targetYaw = fallbackHeadingPk;
                // Reverse: el bot debe face opuesto al segment direction
                if (target.mode == "reverse") targetYaw = targetYaw + Math.PI;
            } else {
                targetYaw = Math.Atan2(toLookahead[0], toLookahead[2]);
            }
        } else {
            // Fallback cruise: pure pursuit clasico (apuntar al lookahead point)
            targetYaw = Math.Atan2(toLookahead[0], toLookahead[2]);
        }

        // Diferencia normalizada a -PI..PI
        float dYaw = targetYaw - currentYaw;
        while (dYaw > Math.PI)  dYaw -= 2.0 * Math.PI;
        while (dYaw < -Math.PI) dYaw += 2.0 * Math.PI;

        // Steering en -1..1, sensibilidad mas suave para no oscilar
        float steering = dYaw / (Math.PI * 0.5);
        if (steering > 1.0)  steering = 1.0;
        if (steering < -1.0) steering = -1.0;
        // (Capa 1 deadband + rate limiter REVERTIDOS 2026-06-04: rompian curvas.
        //  PrÃƒÂ³xima iteraciÃƒÂ³n: path smoothing del recording antes de pasar a Stanley.)

        // === FEEDFORWARD PREDICTIVO DE STEERING ===
        // Stanley arriba reacciona al error LATERAL actual. Pero en curvas
        // pronunciadas el bus tracking lo lateral correctamente cuando entra
        // al segmento nuevo igual corta la curva porque a la velocidad
        // alcanzada no llega a girar el radio. Solucion analoga a la del
        // cruise predictivo: mirar adelante, calcular cuanto va a curvar la
        // ruta en el horizonte cercano, y pre-steerear con un peso modesto.
        //
        // Solo agrega anticipacion. Stanley sigue corrigiendo todo el resto
        // (error lateral, error de heading). El feedforward es 0 en rectas
        // y crece proporcional al cambio de heading futuro.
        bool isParking = (target.mode == "parking" || ActiveLegIsReverse() || target.mode == "maniobra");
        // El FF debe anclarse en el EJE TRASERO en reverse (el punto que LIDERA al reversar), igual
        // que el corredor (~linea 3433). Antes usaba busPos (frente) -> la prediccion se hacia desde
        // el punto equivocado -> el arco se abria aunque anticipara (Sonom4n 2026-07-02: "por mas que lo
        // prediga lo hace con su referencia en el frente"). Ahora consistente con el corredor/Stanley.
        vector ffRefPos = busPos;
        if (isReversePk) ffRefPos = busPos - Vector(Math.Sin(currentYaw), 0, Math.Cos(currentYaw)) * GetReverseControlOffset();
        float ffSteer = ComputeFeedforwardSteering(ffRefPos, kmhForStanley, isParking, isReversePk);
        // FF_WEIGHT: 0.25 cruise (anticipacion modesta de curvas a 1.5s ~ 5-35m).
        // En parking subimos a 0.6 + lookahead corto (1-3m) Ã¢â‚¬â€ el bot mira segmento
        // por segmento los proximos waypoints, anticipando con detalle fino las
        // maniobras lentas (entrar al galpon, contracurva).
        // En reverse la anticipacion importa MAS (non-minimum-phase = "delay" en la
        // direccion): peso propio ReverseFFWeight (default = parking).
        float ffWeight = GetCruiseFFWeight();
        if (isParking) ffWeight = GetParkingFFWeight();
        if (isReversePk) ffWeight = GetReverseFFWeight();

        // Compensacion por wheelbase (SteeringScale): escala el steering final para evitar sobre-rotacion
        // DINAMICA en vehiculos cortos A VELOCIDAD (forward). En REVERSE NO aplica: lento + rear-steer, el
        // R_min ya limita el arco geometricamente -> escalarla ademas le cortaba el volante a la MITAD
        // (Sedan_02 wb2.51 -> scale 0.456 -> Boris topaba 0.46 aunque el humano usaba full lock -> arco
        // abierto, "no lo esta forzando", Sonom4n 2026-07-02). Reverse = autoridad plena.
        //
        // PLANT FEEDFORWARD (2026-07-04): con el FF fisicamente EXACTO (inversa del plant medido), el FF
        // ya es cmd real -> NO se pondera por ffWeight ni se escala por SteeringScale. Solo el feedback
        // (Stanley) se escala por wheelbase y se afloja por PlantFeedbackScale -> el FF carga la curva y
        // el feedback deja de pegar fuerte (menos zigzag). Forward-only; flag OFF = rama else = intacto.
        if (m_Config && m_Config.UsePlantFeedforward && !isParking && !isReversePk) {
            steering = steering * GetSteeringScale() * GetPlantFeedbackScale() + ffSteer;
            steering = ApplyAdditiveCrossTrack(steering, busPos, kmhForStanley, currentYaw);   // cross-track amortiguado -> pega la linea
            if (steering > 1.0)  steering = 1.0;
            if (steering < -1.0) steering = -1.0;
        } else {
            steering += ffSteer * ffWeight;
            if (steering > 1.0)  steering = 1.0;
            if (steering < -1.0) steering = -1.0;
            if (!isReversePk) steering = steering * GetSteeringScale();
        }

        // PURE PURSUIT (2026-07-10, de Arma 2): closed-loop que apunta el volante a un punto CORTO adelante
        // SOBRE la linea -> se auto-corrige cada frame, NO se desvia. REEMPLAZA el steering FF+Stanley de
        // arriba cuando esta ON (forward normal). La velocidad la sigue manejando FollowPath+InverseModel.
        if (m_Config && m_Config.UsePurePursuit && !isParking && !isReversePk && !cuspExit) {
            steering = ComputePurePursuitSteering(busPos, currentYaw, kmhForStanley);
            steering = ApplyPurePursuitBlend(steering, busPos);   // BLEND FF complementario (extraido por limite instr)
            if (steering > 1.0)  steering = 1.0;
            if (steering < -1.0) steering = -1.0;
            m_FastSteerActive = true;   // habilita el loop rapido de 50ms (forward normal pure-pursuit)
        }

        // Captura del componente STANLEY+FF puro (antes de que los bloques de replay
        // pisen 'steering'). En reverse se usa como correccion FINA acotada sumada al
        // volante GRABADO (blend follow-recording, 2026-06-13).
        float reverseStanleyComp = steering;

        // PRE-HANDBRAKE STRAIGHTEN (2026-06-13): si un handbrake-stop grabado viene en los
        // proximos wps, enderezar (steering 0) en la aproximacion. Sino Stanley corrige offset
        // a velocidad casi-cero y ROTA al vehiculo aunque avance poco -> queda torcido al parar
        // -> arranca el reverse DESALINEADO (Sonom4n 2026-06-13, V3S). El humano va derecho a parar
        // (steering grabado 0 en todo el approach). Forward-only; ayuda a todo stop-antes-de-reverse.
        if (target.mode != "reverse" && m_Config && m_Config.Waypoints) {
            int hbMaxIdx = m_WaypointIndex + HANDBRAKE_STRAIGHTEN_LOOKAHEAD;
            if (hbMaxIdx >= m_Config.Waypoints.Count()) hbMaxIdx = m_Config.Waypoints.Count() - 1;
            bool hbImminent = false;
            for (int hbi = m_WaypointIndex + 1; hbi <= hbMaxIdx; hbi++) {
                if (m_Config.Waypoints[hbi].targetHandbrake > 0.5) { hbImminent = true; break; }
            }
            if (hbImminent) { steering = 0; m_FastSteerActive = false; }   // straighten manda: apagar loop rapido
        }

        float throttle = 1.0;
        float brake    = 0.0;
        float kmh = bus.GetSpeedometerAbsolute();
        // TRACER: baseline = steering del pure-pursuit (ya computado) + throttle/brake default. Lo que
        // CUALQUIER capa posterior cambie se atribuye a esa capa (por variable: t/b/s). Ver CtlSnap.
        CtlReset(throttle, brake, steering);

        // === MODO APROXIMACION (SOLO Modo 3): velocidad objetivo efectiva ===
        // Approach es la herramienta especifica de Modo 3: el 3 ignora la velocidad grabada
        // y va al limite geometrico en el run-up recto -> entra caliente a la maniobra. La
        // rampa lo baja desde su velocidad de ENTRADA hasta ApproachExitKmh. En Modo 1 (replay)
        // y Modo 2 (geometria CAPEADA por grabacion) es INERTE: ahi la grabacion ya desacelera,
        // no hace falta. Gate = UseInverseModel (M2/M3) && !FollowPathCapByRecording (excluye M2) = M3 puro.
        // POSICION-SYNC DE LA VELOCIDAD (2026-07-16): gemelo de PlantSteerSourceNearest. m_WaypointIndex
        // corre ~15m adelante -> leer target.targetSpeed = leer el perfil 15-20m antes de estar ahi ->
        // frena temprano (medido: ruta pedia 26.7 en el 90Ã‚Â°, Boris entro a 15.3). El brake-ahead YA hornea
        // la anticipacion EN el perfil -> leerlo adelantado anticipa DOS VECES. El volante quiere lookahead,
        // la velocidad quiere el wp donde Boris ESTA. NO toca el indice ni el aim del pure-pursuit.
        int wpiSpd = m_WaypointIndex;
        if (m_Config.SpeedSourceNearest && m_Config.Waypoints && m_Config.Waypoints.Count() > 0) {
            float bestDsqSpd = 1000000000.0;
            int loSpd = m_WaypointIndex - 40;
            if (loSpd < 0) loSpd = 0;
            int hiSpd = m_WaypointIndex + 2;
            if (hiSpd > m_Config.Waypoints.Count() - 1) hiSpd = m_Config.Waypoints.Count() - 1;
            // ACOTAR AL TRAMO ACTIVO: 40 wps hacia atras cruzan de pierna (la reversa de ESQ son 33) y las
            // trazas estan superpuestas a centimetros -> agarraba la velocidad de OTRO tramo.
            if (loSpd < m_LegStart) loSpd = m_LegStart;
            if (hiSpd > m_LegEnd && m_LegEnd >= m_LegStart) hiSpd = m_LegEnd;
            for (int siSpd = loSpd; siSpd <= hiSpd; siSpd++) {
                vector wpvSpd = m_Config.Waypoints[siSpd].GetVector();
                float dxSpd = busPos[0] - wpvSpd[0];
                float dzSpd = busPos[2] - wpvSpd[2];
                float dsqSpd = dxSpd * dxSpd + dzSpd * dzSpd;
                if (dsqSpd < bestDsqSpd) {
                    bestDsqSpd = dsqSpd;
                    wpiSpd = siSpd;
                }
            }
        }
        float effApproachSpeed = target.targetSpeed;
        if (m_Config.SpeedSourceNearest) effApproachSpeed = m_Config.Waypoints[wpiSpd].targetSpeed;
        // TARGET POR LOOKAHEAD: la vel que Boris puede tener AHORA y aun frenar comodo a lo que viene.
        // vAllow(wp) = sqrt(vWp^2 + 2*a*d) en m/s. El MINIMO de la ventana manda. Incluye el wp mas cercano
        // (d~0 -> vAllow~vWp) asi que nunca sube el target, solo lo BAJA anticipando la curva de adelante.
        // EL OJO TAMBIEN EN MANIOBRA (2026-07-20, MEDIDO). Estaba limitado a mode=="normal": en reverse /
        // parking / maniobra ni se llamaba, y el log lo mostro crudo -> "ojo=-1" en TODAS las lineas de
        // reversa mientras Boris iba a 22 km/h sin nada que le anticipara el final del tramo. Justo donde
        // mas falta hace anticipar, iba ciego: no sabia que el tramo terminaba, no planificaba la frenada,
        // se pasaba de largo y quedaba desorientado. La ley del ojo es la misma en cualquier modo: mira la
        // traza que viene y toma el minimo de vAllow = sqrt(vWp^2 + 2*a*d).
        float vLookaheadKmh = -1.0;
        if (m_Config.UseSpeedLookahead) {
            vLookaheadKmh = ComputeLookaheadSpeed(wpiSpd, kmh, busPos);
            if (vLookaheadKmh > 0 && vLookaheadKmh < effApproachSpeed) effApproachSpeed = vLookaheadKmh;
        }
        bool isM3approach = (m_Config && m_Config.UseInverseModel && !m_Config.FollowPathCapByRecording); // Modo 3 puro
        bool approachRamp = (target.mode == "approach" && isM3approach);
        if (approachRamp) {
            effApproachSpeed = ComputeApproachTargetSpeed(kmh, target.targetSpeed);
            m_LastISpeed     = effApproachSpeed; // que el AI logger refleje la rampa
        } else {
            m_ApproachActive = false; // fuera de approach (o Modo 1) -> rearmar para el proximo bloque
            // APPROACH AUTOMATICA: sin zona marcada a mano, freno predictivo a la maniobra que viene.
            // Solo Modo 3 + ApproachAuto + wp normal. La grabada (tag) tiene prioridad (rama de arriba).
            if (isM3approach && m_Config.ApproachAuto && (target.mode == "normal" || target.mode == "")) {
                float autoSpd = ComputeAutoApproachSpeed(kmh, busPos);
                if (autoSpd > 0) {
                    effApproachSpeed = autoSpd;
                    m_LastISpeed     = effApproachSpeed;
                }
            }
        }

        // === GARANTIA UNIVERSAL DE VELOCIDAD GRABADA en zonas de PRECISION (Sonom4n 2026-07-01) ===
        // El humano PROBO que ESTE vehiculo hace la maniobra/parking/reverse a la velocidad que grabo
        // -> es una PRUEBA DE FACTIBILIDAD para ESE auto (si el la ejecuto, Boris debe poder reproducirla).
        // M3 pisa targetSpeed con GEOMETRIA (rapido en la recta) y entra CALIENTE: un auto de giro ancho
        // (CivilianSedan R_min 4.19m) derrapa el 90Ã‚Â°; uno agil (Nissan 3.44m) perdona. Capeando
        // effApproachSpeed (= target real del InverseModel) a la recordedSpeed POR-WP en las zonas de
        // precision, Boris sigue el PERFIL EXACTO del humano y entra a SU velocidad -> misma capacidad,
        // para CUALQUIER vehiculo (no solo el grabado). Es el PISO; corre bajo approach MANUAL y AUTO por
        // igual (el approach define la FORMA de la desaceleracion, el cap el techo). Solo M3 (M1 replica,
        // M2 ya capea por grabacion). El crucero/normal sigue generalizando libre por geometria.
        bool precZone = (target.mode == "approach" || target.mode == "maniobra" || target.mode == "parking" || target.mode == "reverse");
        if (isM3approach && precZone && target.recordedSpeed > 0.5 && target.recordedSpeed < effApproachSpeed) {
            effApproachSpeed = target.recordedSpeed;
            m_LastISpeed     = effApproachSpeed;
        }

        // DIAGNOSTICO (2026-07-20, Sonom4n: "no se si el lookahead esta funcionando"). Medido: el ojo permitia
        // 22.7 km/h a 10 m del endpoint y Boris iba a 7.8 -> ALGUIEN le pisa la salida al ojo. En vez de
        // seguir adivinando cual, mostramos las fuentes y cual gana, en cada tick lento.
        if (m_Config && m_Config.SpeedDecisionDebug && kmh < 30.0 && m_TickCount % 4 == 0) {
            string sdDbg = "[SPD] v=" + (int)kmh + " wp=" + target.targetSpeed;
            sdDbg = sdDbg + " near=" + m_Config.Waypoints[wpiSpd].targetSpeed;
            sdDbg = sdDbg + " ojo=" + vLookaheadKmh + " -> eff=" + effApproachSpeed + " " + target.mode;
            BZBusLog.Info(sdDbg);
        }

        // === AR_OnWay Ã¢â‚¬â€ ObstacleSlow (fase 1) + ObstacleEscape (fase 2) ===
        // Escudo contra obstruccion EXTERNA (otro vehiculo). Solo M2/M3. NO salva a Boris de Boris.
        // Slow: freno predictivo, se detiene a ObstacleStopDist (no ramea). Escape: si persiste o hay
        // golpe (EEHitBy), teleporta al primer wp LIMPIO pasado el obstaculo. Toggles separados (un
        // mision interceptable usa Slow ON / Escape OFF -> Boris frena y se queda bloqueado).
        if ((m_Config.ObstacleSlow || m_Config.ObstacleEscape) && m_Config.UseInverseModel) {
            float obsD = GetObstacleDistThrottled(busPos, bus, kmh);
            if (m_Config.ObstacleSlow && obsD > 0) {
                float obsSpeed = ComputeObstacleSlowSpeed(obsD);
                if (obsSpeed < effApproachSpeed) {
                    effApproachSpeed = obsSpeed;
                    m_LastISpeed     = effApproachSpeed;
                }
            }
            if (m_Config.ObstacleEscape) {
                float owNow = GetGame().GetTickTime();
                if (obsD > 0) m_ObstacleSeenTime = owNow;   // sello del ultimo avistaje
                bool owCollided = (m_BZHitTime > 0 && owNow - m_BZHitTime < 2.0);
                bool owPersist = false;
                // Bloqueado = frenado/gateando Y un vehiculo visto hace POCO (tolera el FLICKER del
                // scan: un tick sin deteccion no resetea el timer -> era el bug por el que no escapaba).
                // Dos niveles: TOCANDO (obsD<5m) escapa rapido (lo estan chocando/bloqueando pegado,
                // sin depender de EEHitBy que es evento de DAÃƒâ€˜O); bloqueo lejano espera el timeout.
                bool owSeenRecently = (m_ObstacleSeenTime > 0 && owNow - m_ObstacleSeenTime < 3.0);
                bool owTouching = (obsD > 0 && obsD < 5.0);
                if (kmh < 3.0 && owSeenRecently) {
                    if (m_ObstacleStuckSince <= 0) m_ObstacleStuckSince = owNow;
                    float owWait = GetObstacleEscapeWaitS();
                    if (owTouching && owWait > 2.0) owWait = 2.0;
                    if (owNow - m_ObstacleStuckSince > owWait) owPersist = true;
                } else {
                    m_ObstacleStuckSince = 0;   // solo resetea si Boris avanza (kmh>=3) o ya no ve nada hace >3s
                }
                // EMPUJE / STALL: Boris QUIERE avanzar (target alto, modo normal) pero no se mueve NI
                // progresa por wps -> esta empujando/trabado contra algo. NO depende de que el scan lo
                // siga viendo (caso "me ve pero me empuja": al empujarlo se corre de la linea grabada,
                // el scan lo pierde, Boris destapa el throttle y lo empuja). Gatilla el escape igual.
                bool owPushing = false;
                bool owSeenLately = (m_ObstacleSeenTime > 0 && owNow - m_ObstacleSeenTime < 20.0);
                // FORZANDO/EMPUJANDO: quiere avanzar (target alto, modo normal) pero va MUY lento
                // (<10 km/h) sostenido -> esta empujando algo. Incluye el vehiculo FUERTE que empuja
                // el obstaculo despacio (no se traba del todo, pero nunca alcanza su velocidad -> el
                // Nissan debil se trababa <3 y por eso solo el escapaba). No exige "sin progreso".
                if (owSeenLately && (target.mode == "normal" || target.mode == "") && target.targetSpeed > 15.0 && kmh < 10.0) {
                    if (m_ObstaclePushSince <= 0) m_ObstaclePushSince = owNow;
                    if (owNow - m_ObstaclePushSince > 4.0) owPushing = true;
                } else {
                    m_ObstaclePushSince = 0;
                }
                if (owCollided || owPersist || owPushing) {
                    int owClear = FindClearWpAhead(busPos, bus);
                    if (owClear > m_WaypointIndex) {
                        int owTotal = m_Config.Waypoints.Count();
                        vector owPos = m_Config.Waypoints[owClear].GetVector();
                        owPos[1] = owPos[1] + 0.5;
                        float owHeadingDeg = 0;
                        if (owClear + 1 < owTotal) {
                            vector owNext = m_Config.Waypoints[owClear + 1].GetVector();
                            owHeadingDeg = Math.Atan2(owNext[0] - owPos[0], owNext[2] - owPos[2]) * Math.RAD2DEG;
                        }
                        bus.SetPosition(owPos);
                        bus.SetOrientation(Vector(owHeadingDeg, 0, 0));
                        float owSpdKmh = m_Config.Waypoints[owClear].targetSpeed;
                        float owResumeCap = GetObstacleEscapeResumeKmh();
                        if (owSpdKmh > owResumeCap) owSpdKmh = owResumeCap;   // arrancar SUAVE tras el escape (default 10 km/h), no a la velocidad grabada del wp
                        if (owSpdKmh > 5.0) {
                            float owSpdMs = owSpdKmh / 3.6;
                            float owHeadRad = owHeadingDeg * Math.DEG2RAD;
                            vector owFwd = Vector(Math.Sin(owHeadRad), 0, Math.Cos(owHeadRad));
                            vector owTgtVel = owFwd * owSpdMs;
                            vector owCurVel = GetVelocity(bus);
                            vector owDelta = owTgtVel - owCurVel;
                            float owMass = dBodyGetMass(bus);
                            if (owMass > 0) dBodyApplyImpulse(bus, owDelta * owMass);
                        }
                        SetDesiredGear(2);
                        bus.ShiftTo(2);
                        if (m_InverseModel) m_InverseModel.ResetPID();
                        m_WaypointIndex = owClear;
                        m_AR_LastWpIdx = owClear;
                        m_AR_LastWpProgressTime = owNow;
                        m_OffPath_InRecovery = false;
                        m_DR_InRecovery = false;
                        m_BZHitTime = 0;
                        m_ObstacleStuckSince = 0;
                        m_ObstacleSeenTime = 0;
                        m_ObstaclePushSince = 0;
                        m_ObstacleDist = -1;
                        string owReason = "obstaculo persistente";
                        if (owCollided) owReason = "golpe";
                        else if (owPushing) owReason = "empuje/stall";
                        BZBusLog.Info("[AR_OnWay] ESCAPE (" + owReason + ") -> teleport a wp " + owClear);
                        return;  // skip tick
                    }
                }
            }
        }

        // Distancia al proximo wp con isStop=true. Si no hay mas (fin de linea),
        // tratamos al ultimo waypoint como "stop" para que tambien frene al final.
        float distToNextStop = 99999.0;
        if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            distToNextStop = vector.Distance(busPos, m_Config.Waypoints[m_NextStopIndex].GetVector());
        }
        // CERCA DE UN CHECKPOINT (extraido a IsNearCheckpoint por el limite de instrucciones de DriveTowards):
        // dentro de 18m del fin de tramo, el StopBrake slope-aware es el unico controlador longitudinal.
        bool nearCheckpoint = IsNearCheckpoint(busPos);

        // === MODO PARKING: control fisico predictivo ===
        // En vez de rampa lineal, calculamos cuanto freno necesitamos AHORA
        // para llegar a 0 km/h exactamente a STOP_FINAL_RADIUS metros del cartel,
        // dada la velocidad actual y distancia restante. Cinematica clasica:
        //   vÃ‚Â² = uÃ‚Â² - 2Ã‚Â·aÃ‚Â·s  Ã¢â€ â€™  a = uÃ‚Â² / (2Ã‚Â·s)
        // Donde u = velocidad actual (m/s), s = distancia hasta el punto de parada
        // ideal, a = deceleracion necesaria. Convertimos a fraccion de freno
        // dividiendo por MAX_BRAKE_DECEL (deceleracion del bus a freno fondo).
        //
        // Ventaja sobre la rampa: el bus llega EXACTO al cartel sin "stop fake".
        // Si va rapido, frena fuerte; si va lento, deja correr; si esta parado
        // lejos, throttle suave para acercarse.
        const float PARKING_THRESHOLD = 40.0;
        // VALOR DE REFERENCIA (good known state, validado 2026-05-22):
        // MAX_BRAKE_DECEL = 20.0 hace que el bus pare ~1m antes del cartel.
        // Aceptable para el caso de uso (jugador agazapado esperando, precision
        // sobre realismo). Si experimentos posteriores rompen el comportamiento,
        // volver a este valor. Iteracion empirica: 6 -> 9 -> 12 -> 16 -> 20.
        //
        // PRUEBA v2.7 (2026-05-22 tarde): 50.0. Con 30 el bus no se paso
        // del cartel (hipotesis: drag del juego frena mas de lo asumido,
        // o el bus real frena mas fuerte que ~7 m/sÃ‚Â²). Subimos a 50 para
        // confirmar limite empirico.
        const float MAX_BRAKE_DECEL   = 50.0;  // m/sÃ‚Â² a freno fondo, EXPERIMENTO LIMITE (rollback a 20.0 si rompe)
        // Rollback 2026-05-30: el bloque PARKING NAVIGATION (cruise 10 km/h entre
        // 10-40m del stop) rompia las curvas 90Ã‚Â° normales porque se activaba para
        // CUALQUIER stop, no solo secuencias de parking. Cuando lo retomemos, hay
        // que detectar la condicion "secuencia de stops cercanos" (ej. siguiente
        // stop esta a <80m del proximo) y solo ahi activar nav cruise.
        if (distToNextStop <= PARKING_THRESHOLD && !(m_Config && m_Config.FollowPaintedToStop)) {
            // STOP-LEARNER (gancho 2/3): resta el brake-point advance APRENDIDO -> achica distRemaining ->
            // aNeeded = u2/(2*distRemaining) sube antes -> Boris frena mas temprano -> el que se pasaba, para en el punto.
            float stopBias = 0;
            if (m_Config.StopLearnerEnabled) stopBias = BZStopLearner.GetInstance().GetBias(m_NextStopIndex);
            float distRemaining = distToNextStop - STOP_FINAL_RADIUS - stopBias;
            if (distRemaining <= 0.0) {
                // Ya en el cartel o nos pasamos: freno a fondo
                brake    = 1.0;
                throttle = 0;
            } else {
                float u_ms = kmh / 3.6;
                float aNeeded = (u_ms * u_ms) / (2.0 * distRemaining);

                // Factor altura (2026-05-23): validamos empiricamente en 3 terrenos
                // que el modelo simple a_eff = a_motor +/- g*sin(theta) predice
                // con <10% error. Aplicamos correccion por pendiente al aNeeded:
                //   - Bajada (pendiente negativa): gravedad asiste al movimiento
                //     -> bus viene con velocidad extra -> necesitamos MAS freno
                //   - Subida (pendiente positiva): gravedad opone al movimiento
                //     -> bus se frena solo -> necesitamos MENOS freno
                // pendiente = (Y_stop - Y_bus) / dist_horizontal (signed)
                // Para angulos pequeÃƒÂ±os, sin(theta) ~ tan(theta) ~ pendiente.
                float pendiente = 0;
                if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                    vector stopWpPos = m_Config.Waypoints[m_NextStopIndex].GetVector();
                    vector busHoriz = Vector(busPos[0], 0, busPos[2]);
                    vector stopHoriz = Vector(stopWpPos[0], 0, stopWpPos[2]);
                    float horizDist = vector.Distance(busHoriz, stopHoriz);
                    if (horizDist > 1.0) {
                        pendiente = (stopWpPos[1] - busPos[1]) / horizDist;
                    }
                }
                float gAssist = -9.8 * pendiente; // positivo en bajada
                aNeeded += gAssist;
                if (aNeeded < 0) aNeeded = 0; // gravedad sola alcanza para frenar

                // Knob per-ruta (2026-07-06): el 50 legacy es del BUS; un auto liviano frena ~7 m/s2.
                // Con decel realista el freno progresivo arranca desde lejos y el lazo cerrado absorbe
                // la velocidad de entrada -> parada deterministica (el bias escalar no puede con el ruido).
                float brakeDecel = MAX_BRAKE_DECEL;
                if (m_Config && m_Config.BrakeDecelMS > 0) brakeDecel = m_Config.BrakeDecelMS;
                float brakeFrac = aNeeded / brakeDecel;

                if (brakeFrac > 1.0) {
                    // No alcanza con freno fondo, igual aplicamos max
                    brake    = 1.0;
                    throttle = 0;
                } else if (brakeFrac > 0.05) {
                    // Freno modulado segun fisica
                    brake    = brakeFrac;
                    throttle = 0;
                } else if (kmh < 3.0) {
                    // Vamos muy lento dentro de zona parking: empujar para llegar
                    // al cartel. 0.2 no alcanzaba para vencer la inercia estatica
                    // del bus parado en gear FIRST, dejaba al bus a 3-4m del cartel.
                    brake    = 0;
                    throttle = 0.35;
                } else {
                    // Coast: la inercia nos lleva, no hace falta tocar nada
                    brake    = 0;
                    throttle = 0;
                }
            }
        }
        // === MODO CRUCERO PREDICTIVO: planeamos throttle/brake mirando adelante ===
        // En vez de obedecer al pie de la letra iThrottle/iBrake del recording
        // (que dejaba al bus undershoot post-frenazo y cortando curvas a
        // velocidad), miramos los proximos waypoints y planeamos la
        // deceleracion necesaria, igual que MODO PARKING calcula la decel
        // para llegar a 0 en el cartel. La diferencia: aca calculamos decel
        // para llegar a target_speed[i] del waypoint i, no a velocidad 0.
        //
        // Resuelve dos problemas observados en telemetria (corrida 2026-05-28):
        //   - Undershoot: bus quedaba 10 km/h debajo del target porque recording
        //     tenia throttle=0 post-frenazo. Ahora las bandas aplican throttle
        //     pleno cuando no hay decel pendiente y kmh < iSpeed * 0.9.
        //   - Cortar curvas: bus entraba a curvas demasiado rapido porque no
        //     anticipaba la caida de target_speed que el humano si anticipo
        //     al grabar. Ahora el lookahead la ve y frena con tiempo.
        // 2026-06-21: gate SIN hasInputData -> Modo 3 (hasInputData=false) TAMBIEN usa este
        // freno PREDICTIVO. Antes Modo 3 caia al fallback reactivo (solo frenaba tras pasarse
        // 10 km/h del target) -> entraba caliente, no anticipaba como el humano. El escaneo solo
        // necesita targetSpeed (presente en Modo 3); HONOR/HYBRID (copian inputs) tienen su
        // propio guard hasInputData adentro -> se saltean en Modo 3 (freno predictivo puro).
        else if (iSpeed > 5) {
            // Sufijo "Cr" en variables locales para evitar colision con las
            // del bloque PARKING (Enforce trata ramas hermanas del if/else
            // como mismo scope Ã¢â‚¬â€ declarar u_ms aca chocaria con el u_ms de
            // PARKING aunque sean ramas distintas).
            float u_msCr = kmh / 3.6;

            // === ESCANEO PREDICTIVO ===
            // Recorremos waypoints futuros sumando distancia hasta 100m, o
            // hasta el proximo cartel de parada (lo que llegue antes).
            // Cortamos antes del cartel porque a partir de ahi MODO PARKING
            // se hace cargo con su propia cinematica de aproximacion.
            float CRUISE_LOOKAHEAD_DIST = 100.0;
            float MAX_BRAKE_DECEL_CR    = 50.0; // decel freno fondo m/sÃ‚Â², igual valor que PARKING
            // Radio de la zona de PARKING: el predictivo NO debe escanear
            // dentro de los ultimos 40m al cartel Ã¢â‚¬â€ esa zona la maneja MODO
            // PARKING con su propia cinematica. Si pre-frena en esa zona,
            // el bus llega undershoot y "entra lento" (confirmado en
            // telemetria 2026-05-29: Kamenka, Balota, Vysotovo).
            float PARKING_ZONE_CR = 40.0;

            // Effective lookahead = el minimo entre CRUISE_LOOKAHEAD_DIST y
            // la distancia hasta el borde de la zona de PARKING. Asi
            // dejamos a PARKING su espacio.
            float effectiveLookaheadCr = CRUISE_LOOKAHEAD_DIST;
            float distToParkingEdgeCr  = distToNextStop - PARKING_ZONE_CR;
            if (distToParkingEdgeCr < effectiveLookaheadCr) effectiveLookaheadCr = distToParkingEdgeCr;
            if (effectiveLookaheadCr < 0) effectiveLookaheadCr = 0;

            float maxNeededDecelCr = 0;
            float cumDistCr        = 0;
            int   maxIdxCr         = m_Config.Waypoints.Count() - 1;
            int   scanLimitCr      = maxIdxCr;
            if (m_NextStopIndex >= 0 && m_NextStopIndex < maxIdxCr) {
                scanLimitCr = m_NextStopIndex - 1;
                if (scanLimitCr < m_WaypointIndex) scanLimitCr = m_WaypointIndex;
            }

            vector lastPosCr = busPos;
            for (int fi = m_WaypointIndex; fi <= scanLimitCr && cumDistCr < effectiveLookaheadCr; fi++) {
                BZWaypoint fwp = m_Config.Waypoints[fi];
                vector fwpPos = fwp.GetVector();
                float segDistCr = vector.Distance(lastPosCr, fwpPos);
                cumDistCr += segDistCr;
                lastPosCr = fwpPos;

                if (cumDistCr < 1.0) continue; // proteccion division por cero

                // Si el wp futuro es parking, override su targetSpeed al cap
                // de parking (8 km/h). Asi cruise predictivo desacelera ANTES
                // de entrar a parking Ã¢â‚¬â€ igual que decel para stops. Sin esto
                // Boris llegaba a parking a 17 km/h (la velocidad del recording
                // en ese punto) y mi controller parking slameaba el freno
                // creando oscilacion (validado en AI log 2026-05-31).
                float effectiveTargetSpeedCr = fwp.targetSpeed;
                if (fwp.mode == "parking") effectiveTargetSpeedCr = 12.0;
                if (fwp.mode == "maniobra" && m_Config) {
                    float manCap = m_Config.ManiobraTargetSpeedCap;
                    if (manCap > 0 && effectiveTargetSpeedCr > manCap) effectiveTargetSpeedCr = manCap;
                }

                if (effectiveTargetSpeedCr >= kmh - 2.0) continue; // futuro mas rapido o casi igual, no decel

                float v_future_ms = effectiveTargetSpeedCr / 3.6;
                // vÃ‚Â² = uÃ‚Â² - 2Ã‚Â·aÃ‚Â·s  =>  a = (uÃ‚Â² - vÃ‚Â²) / (2Ã‚Â·s)
                float dv2 = u_msCr*u_msCr - v_future_ms*v_future_ms;
                float aNeededCr = dv2 / (2.0 * cumDistCr);
                if (aNeededCr > maxNeededDecelCr) maxNeededDecelCr = aNeededCr;
            }

            // === DECISION DE OUTPUT ===
            if (maxNeededDecelCr > 0.5) {
                // Slowdown pendiente: brake proporcional a la necesidad.
                // SLOPE-AWARE (2026-06-21): en bajada la gravedad empuja CONTRA el freno
                // (decel_real = brake - g*sin|pitch|) -> el freno debe proveer la decel necesaria
                // MAS la componente de gravedad, sino under-frena en descenso y entra caliente a
                // las curvas de bajada (residual del fix del gate). m_PathPitch ya esta pre-computed.
                if (m_PathPitch) {
                    float pitchDecelCr = GetEffectivePitch(m_WaypointIndex, 8);
                    if (pitchDecelCr < 0) maxNeededDecelCr = maxNeededDecelCr - 9.81 * Math.Sin(pitchDecelCr);
                }
                float brakeFracCr = maxNeededDecelCr / MAX_BRAKE_DECEL_CR;
                brake    = Math.Clamp(brakeFracCr, 0, 1.0);
                throttle = 0;
            }
            else {
                // No hay frenada pendiente en el horizonte. Aplicamos bandas
                // de throttle tipo v1.0 segun gap actual de velocidad:
                //   - muy abajo del target: throttle pleno (recovery post-frenazo)
                //   - cerca del target: throttle medio para mantener
                //   - arriba del target: soltar / freno suave
                float speedDelta = kmh - iSpeed;
                if (speedDelta > 10) {
                    brake    = Math.Clamp(speedDelta / 30.0, 0, 0.7);
                    throttle = 0;
                }
                else if (kmh > iSpeed * 1.05) {
                    // 2026-06-21: ANTES throttle=0.3 MANTENIA velocidad estando over el target ->
                    // en targets BAJOS (serpenteo a Zenit, target ~14) dejaba a Boris crucereando
                    // +50% (21 km/h) sin bajar -> demasiado rapido para curvas cerradas -> se abria.
                    // Ahora COAST + freno suave proporcional -> DESCIENDE hacia el target (clave para
                    // <15 en el serpenteo). En cruise normal el over es chico -> freno minimo, queda
                    // mas preciso (cruza AL target, no +10% arriba). El 0.9 band recupera si baja de mas.
                    throttle = 0;
                    brake    = Math.Clamp((kmh - iSpeed) / 40.0, 0, 0.35);
                }
                else if (kmh > iSpeed * 0.9) {
                    throttle = 0.6;
                    brake    = 0;
                }
                else {
                    // UNDERSHOOT RECOVERY con throttle progresivo por velocidad absoluta.
                    // Lo viejo era throttle=1.0 Ã¢â‚¬â€ generaba spin al salir de curvas en
                    // sport cars (Nissan a 10 km/h con throttle full = wheel spin).
                    // Lo nuevo replica la firma humana: a baja velocidad arrancar
                    // suave, abrir throttle progresivamente conforme sube la velocidad.
                    // Calibracion 2026-05-30 con datos del Nissan (RPM avg humano 2027
                    // vs RPM avg Boris 4084 Ã¢â‚¬â€ la diferencia se hace en estas bandas).
                    // Rollback 2026-05-30: el tuning de 0.55 en 15-30 km/h roto las
                    // curvas 90Ã‚Â° (Boris no levantaba suficiente al salir). Vuelta
                    // al original 0.7 que funcionaba.
                    if      (kmh < 15) throttle = 0.5;
                    else if (kmh < 30) throttle = 0.7;
                    else               throttle = 1.0;
                    brake = 0;
                }
            }

            // === KICK al arranque: vencer friccion estatica ===
            // Si el bus esta casi parado (acaba de arrancar o de salir de
            // parada), throttle 1.0 puede revolucionar demasiado en gear
            // FIRST y disparar anti-catapulta. 0.6 es suficiente para
            // moverse sin revolucionarse.
            if (kmh < 5.0 && brake == 0) {
                throttle = 0.6;
            }

            // === HONOR RECORDING: rampas y obstaculos ===
            // Cuando el humano estaba pushing hard (throttle > 0.8) y el bot
            // esta debajo del pace, replicar el throttle del recording.
            // Maneja casos donde "bands por velocidad" subestiman el esfuerzo
            // (rampas, terreno difÃƒÂ­cil, atascado contra obstaculo).
            // Validado en grabacion 2026-05-31: rampa del galpon humano = 11 km/h
            // con throttle=1.0; Boris en bands UNDERSHOOT da throttle=0.5
            // (porque kmh<15) y no escalaba la rampa. Honor recording lo soluciona.
            if (target.hasInputData && target.targetThrottle > 0.8 && kmh < iSpeed * 0.9 && brake == 0) {
                throttle = target.targetThrottle;
            }

            // === HYBRID THROTTLE: respect the recording ===
            // Cuando el humano estaba acelerando (targetThrottle >= threshold) y
            // NO frenaba (targetBrake < 0.2), Boris debe replicar esa intencion
            // verbatim Ã¢â‚¬â€ override del cruise predictivo. Sin esto, el predictivo
            // puede activar brake por anticipacion de un targetSpeed mas bajo
            // downstream y dejar al bot parado en una recta donde el humano iba
            // a 50 km/h. Validado 2026-06-01: stuck zones en wp 203, 323, 548, 623
            // del recording T6 long, todos en rectas con human kmh=38-67. El bot
            // se quedaba a 0 km/h mientras el humano aceleraba ahi. Hybrid throttle
            // honra el contrato "Boris reproduce el recording".
            float hybridThrottleThresh = GetCruiseHybridThrottleThreshold();
            bool useHybridThrottle = false;
            if (hybridThrottleThresh >= 0 && target.hasInputData) {
                if (target.mode == "normal") {
                    if (target.targetThrottle >= hybridThrottleThresh) {
                        if (target.targetBrake < 0.2) {
                            // Guardrail: hybrid copia el throttle humano TAL CUAL, pero el humano
                            // grababa con SU velocidad del momento. Si Boris esta muy por debajo
                            // del target, copiar el throttle conservador del humano (0.3 mantiene
                            // 38 km/h) no le alcanza para acelerar desde 10 km/h. En undershoot
                            // las bandas dan throttle alto (0.5-1.0) Ã¢â‚¬â€ dejarlas ganar.
                            if (kmh >= iSpeed * 0.7) {
                                useHybridThrottle = true;
                            }
                        }
                    }
                }
            }
            if (useHybridThrottle) {
                throttle = target.targetThrottle;
                brake    = target.targetBrake;
            }

            // === SAFETY OVERRIDE: ultimo recurso ===
            // Si por algun bug el escaneo no anticipo y excedimos target_speed
            // por mas de 15 km/h, freno extra. No deberia disparar en
            // operacion normal con el escaneo predictivo arriba.
            if (kmh > iSpeed + 15) {
                brake = Math.Max(brake, 0.5);
                throttle = 0;
            }

            // === SLOPE COMPENSATION ===
            // En subida la gravedad penaliza la aceleracion (~g*sin(pitch) m/sÃ‚Â²),
            // en bajada la regala. El PID asume terreno plano. Aca compensamos:
            // - Subida: agregar throttle proporcional al pitch
            // - Bajada: bajar throttle (no agregamos brake Ã¢â‚¬â€ el cruise predictivo lo hace)
            // Solo cuando NO hay brake activo y NO estamos en honor/hybrid (que ya
            // copian el throttle humano que ya tiene la compensacion implicita).
            if (m_Config && m_Config.SlopeCompensationEnabled && m_PathPitch && !useHybridThrottle && brake < 0.05) {
                float pitchRadSl = GetEffectivePitch(m_WaypointIndex, m_Config.SlopeLookaheadWps);
                // sin(pitch) * 9.81 = componente longitudinal de gravedad (m/sÃ‚Â²)
                // Dividido por 4 m/sÃ‚Â² (aceleracion tipica de un vehiculo mid) = fraccion de throttle equivalente
                float throttleDeltaSl = (Math.Sin(pitchRadSl) * 9.81 / 4.0) * m_Config.SlopeGain;
                // pitch > 0 (subida) Ã¢â€ â€™ throttleDelta > 0 Ã¢â€ â€™ mas throttle
                // pitch < 0 (bajada) Ã¢â€ â€™ throttleDelta < 0 Ã¢â€ â€™ menos throttle (no brake)
                throttle = throttle + throttleDeltaSl;
                if (throttle > 1.0) throttle = 1.0;
                if (throttle < 0)   throttle = 0;
            }
        } else if (iSpeed > 5) {
            // Fallback sin recording: cruise control puro (con velocidad interpolada)
            float speedDelta2 = kmh - iSpeed;
            if (speedDelta2 > 10) {
                brake    = Math.Clamp(speedDelta2 / 30.0, 0, 0.7);
                throttle = 0;
            } else if (kmh > iSpeed * 1.05) {
                throttle = 0.3;
            } else if (kmh > iSpeed * 0.9) {
                throttle = 0.6;
            }
        }

        // [AUDITORIA 26/07] eliminado THROTTLE GAP FIX ("capa vieja"): gateado por !UseSpeedLookahead (ON en el banco).
        // Se conserva isNormalGap: lo usa el CREEP-TO-POINT de abajo.
        bool isNormalGap = (target.mode == "normal" || target.mode == "");

        // CREEP-TO-POINT (Sonom4n 2026-06-24, idea del usuario): si Boris quedo en un stop grabado
        // (targetSpeed~0) pero CORTO del punto fisico (quedo "en el limbo", off del path grabado),
        // en vez de dejarlo quieto y que el snap teletransporte, maneja despacio el gap REAL hasta
        // el punto. Micro-correccion RUNTIME distinta cada toma (segun como encaro Boris hasta ahi)
        // -> imposible precalcular. El snap queda de red invisible; el dwell-skip transiciona recien
        // cuando para ENCIMA del punto. Ver [[project_snap_minimize_visual_teleport]].
        if (isNormalGap && target.targetSpeed < 0.5 && distToNextStop > PARKING_THRESHOLD) {
            vector busPcr = bus.GetPosition();
            vector wpPcr  = target.GetVector();
            float gapCr = Math.Sqrt((busPcr[0]-wpPcr[0])*(busPcr[0]-wpPcr[0]) + (busPcr[2]-wpPcr[2])*(busPcr[2]-wpPcr[2]));
            if (gapCr > 0.4) {
                // todavia corto -> creep ~3 km/h manejando hacia el punto (Stanley ya apunta al wp)
                if (kmh < 3.0) throttle = 0.3; else throttle = 0;
                brake = 0;
            } else {
                // ya encima del punto -> frenar para parar exacto; el dwell-skip transiciona a reversa
                throttle = 0;
                if (kmh > 0.5) brake = 0.4;
            }
        }

        // === RECOVERY THROTTLE LIMITER ===
        // Cuando Stanley o FF estan pidiendo correccion fuerte de steering,
        // el bot esta en recovery (off corridor o entrando a curva cerrada).
        // En ese estado, throttle pleno es lo peor que podemos hacer porque
        // dispara wheel spin -> mas deviacion lateral -> cascada de fallo
        // (validado 2026-05-30 con Nissan: vino del cruise predictivo
        // pidiendo throttle 1.0 para recuperar velocidad despues de una
        // desaceleracion fuerte, lo que en sport car ligero produjo trompo).
        //
        // Solucion: si steering es alto y velocidad esta en el rango donde
        // el torque vuelca a las ruedas, capamos throttle. Stanley/FF
        // siguen comandando steering, pero con menos torque el bot puede
        // girar sin patinar. Filosofia: PRIMERO volver al trazo controlado,
        // DESPUES recuperar velocidad.
        //
        // No aplicamos:
        //   - Si ya estamos frenando (es deceleracion intencional, no recovery)
        //   - Si kmh < 2 (dejamos que KICK arranque desde quieto)
        //   - Si kmh > 40 (a velocidad alta el problema no es torque sino inercia)
        float steerAbsRl = steering;
        if (steerAbsRl < 0) steerAbsRl = -steerAbsRl;
        if (steerAbsRl > 0.4 && kmh > 2.0 && kmh < 40.0 && brake < 0.1) {
            float recoveryMaxThrottle = 0.4;
            if (throttle > recoveryMaxThrottle) throttle = recoveryMaxThrottle;
        }

        // === OFFSET RECOVERY PROGRESIVO ===
        // Cuanto mas lejos del trazado esta Boris, mas restriccion aplicamos.
        // No es binario "estoy bien / estoy mal" sino continuo: a 0m normal,
        // a 1m empieza a bajar throttle, a 5m empieza a aplicar brake, a 10m+
        // brake full. Eso elimina la transicion abrupta de recovery a cruise
        // que reactivaba la cascada de spin. Equivalente a como maneja un
        // humano: cuanto mas off-line, mas suave con los inputs.
        //
        // Insight del usuario 2026-05-30: "mientras mas lejos del objetivo
        // mas aplica" Ã¢â‚¬â€ recovery debe ser proporcional a la desviacion,
        // tanto para navegacion normal como para recuperacion fuerte.
        if (m_CorridorValid) {
            float absOffset = m_CorridorLateralOffset;
            if (absOffset < 0) absOffset = -absOffset;

            // Smart throttle cap: aplicar SOLO cuando offset CRECIENDO (Boris alejandose).
            // Si offset DECRECE (volviendo al path), no cappear Ã¢â‚¬â€ Boris debe poder
            // acelerar para recuperar la velocidad target. Sin este matiz, despues
            // de cada volantazo Boris queda 10-20 km/h debajo del recording y nunca
            // catchea, cascada continua de off-path.
            // Diferencial: comparar absOffset con su prev. Si delta > 0 Ã¢â€ â€™ alejandose.
            float absOffsetPrev = m_LastCorridorOffset;
            if (absOffsetPrev < 0) absOffsetPrev = -absOffsetPrev;
            bool offsetGrowing = (absOffset > absOffsetPrev + 0.05);  // hysteresis 5cm
            if (absOffset > 1.0 && offsetGrowing) {
                float throttleCap = 1.0 - (absOffset - 1.0) * 0.15;
                if (throttleCap < 0.0) throttleCap = 0.0;
                if (throttle > throttleCap) throttle = throttleCap;
            }

            // Brake progresivo: a partir de 5m de offset, brake crece linealmente.
            // A offset 10m, brake full.
            if (absOffset > 5.0) {
                float brakeRequired = (absOffset - 5.0) * 0.20;
                if (brakeRequired > 1.0) brakeRequired = 1.0;
                if (brakeRequired > brake) brake = brakeRequired;
            }
        }

        // Gestion de gear segun GearStrategy del config:
        //   "auto_box"         - dejar a la AT (RPM-based en modded CarScript) decidir.
        //                         Apropiado para vehiculos pesados (bus).
        //   "follow_recording" - usar el gear capturado en el recording PERO con
        //                         contexto: si el recording esta en fase de
        //                         aceleracion, capamos el gear max segun velocidad
        //                         para evitar que el bot arranque en FOURTH cuando
        //                         el humano paso por ahi en FOURTH frenando.
        // Default es auto_box, mismo comportamiento que antes.
        //
        // LECCION 2026-05-30: el recording naive no alcanza. El gear humano a
        // baja velocidad suele ser ALTO porque el humano estaba decelerando
        // (4ta a 10 km/h venia frenando de 60). El bot acelera por la misma
        // zona y arranca en 4ta -> bog, breakaway, derrape. Solucion: usar
        // target_throttle/target_brake del recording como CONTEXTO. Si el
        // recording manda throttle pleno y NO brake, estamos en fase de
        // aceleracion -> capamos el gear con tabla por velocidad.
        // [AUDITORIA 26/07] eliminado GEAR follow_recording (b26): triple-muerto -> !UseInverseModel(ON), GearStrategy=auto_box, hasInputData=0. Gear forward lo maneja el InverseModel (b42).

        // === MARCHA BAJA EN PRECISION (M3) Ã¢â‚¬â€ slope-aware (Sonom4n 2026-07-01) ===
        // En M3 la gestion de marcha del InverseModel SALTA maniobra/parking (corre solo normal/approach,
        // ~linea 5011) y el follow_recording de arriba es solo M1 -> en una maniobra Boris HEREDA la marcha
        // del crucero (ej FOURTH=5) y en CUESTA ARRIBA a baja velocidad NO tiene torque -> se CLAVA (motor
        // a ~idle en 5ta = 0 torque a las ruedas; el slope-throttle mete 0.9 pero en 5ta no sirve). Fix
        // UNIVERSAL (cualquier vehiculo en pendiente): capear la marcha (solo DOWNSHIFT) para torque; en
        // subida fuerte, FIRST asegurada. Reverse tiene su propia marcha (ShiftTo 0), queda afuera.
        // DayZ gear: FIRST=2 SECOND=3 THIRD=4 FOURTH=5.
        if (m_Config && m_Config.UseInverseModel && (target.mode == "maniobra" || target.mode == "parking")) {
            int precGearCap = 4; // THIRD: techo para maniobra normal
            if (kmh < 12.0) precGearCap = 3; // SECOND a baja velocidad
            float pitchPrecGear = GetEffectivePitch(m_WaypointIndex, m_Config.SlopeLookaheadWps);
            if (pitchPrecGear > 0.08 && kmh < 20.0) precGearCap = 2; // subida (~5deg+) a baja vel -> FIRST (torque de arranque)
            if (bus.GetGear() > precGearCap) {
                SetDesiredGear(precGearCap);
                bus.ShiftTo(precGearCap);
            }
        }

        // ILC: aplicar correccion pre-calculada por waypoint si esta cargada.
        // El offset compensa desviaciones sistematicas observadas en corridas
        // anteriores. Si no hay JSON cargado, GetSteeringFor devuelve 0.
        float ilcOffset = BZILCCorrections.GetInstance().GetSteeringFor(m_WaypointIndex);
        steering = steering + ilcOffset;
        if (steering > 1.0)  steering = 1.0;
        if (steering < -1.0) steering = -1.0;

        // === HYBRID CRUISE STEERING ===
        // En cruise mode: si el wp grabado tiene targetSteering fuerte (|s|>threshold),
        // override Stanley con ese valor. Captura los pulsos binarios del input
        // keyboard del humano (A/D = steer +-1 mientras tecla apretada, 0 al
        // soltar). Stanley produce salida continua y no puede reproducir esos
        // pulsos Ã¢â‚¬â€ en vehiculos con yaw rate alto (T6, sport cars) la
        // sub-reaccion en cruise se transforma en zigzag por correccion tardia.
        // Threshold y mode chequeados; -1 = deshabilitado (Stanley puro).
        //
        // SKIP si Direct Replay activo (Direct Replay con scan sub-tick + average
        // ya hace mejor el trabajo, son redundantes y se pisan). Insight 2026-06-03:
        // ambos overrides al mismo tiempo causaban steering pinned cuando Boris
        // se atascaba en wp con steer=1 Ã¢â‚¬â€ Direct Replay yielded pero Hybrid seguia
        // forzando steer=1 cada tick.
        // [AUDITORIA 26/07] eliminado HYBRID CRUISE STEERING (replay Modo1): gateado por hasInputData=0.

        // === DIRECT REPLAY OVERRIDE (modo MANIOBRA) ===
        // Si DirectReplayFromWaypoint esta seteado y ya alcanzamos ese wp,
        // OVERRIDE todos los inputs calculados (Stanley, FF, cruise predictivo,
        // PARKING, ilc) con los valores grabados directamente del recording.
        // Reproduce la secuencia exacta de inputs del humano Ã¢â‚¬â€ bypassa toda
        // logica del controller. Util para maniobras coreograficas (parking
        // fino, drifts, sequences de stops cercanos) donde el controlador no
        // llega a la precision necesaria. Default -1 = deshabilitado.
        bool directReplayActive = false;
        if (m_Config && target.hasInputData) {
            int drFrom = m_Config.DirectReplayFromWaypoint;
            if (drFrom >= 0 && m_WaypointIndex >= drFrom) directReplayActive = true;
            // Mode-based activation: si el waypoint actual fue grabado con
            // parking mode ON (toggle NUMPAD + del modder), usar direct replay
            // sin tocar la config global. Permite mezclar tramos cruise y
            // parking en una misma ruta sin global override.
            if (target.mode == "parking" || target.mode == "reverse" || target.mode == "maniobra") directReplayActive = true;
        }

        // === PERTURBATION DETECTION (Direct Replay closed-loop guard) ===
        // Insight 2026-06-03 (usuario): "no quiero que reproduzca mi input ciego,
        // los waypoints son para que permanezca en el camino intentando reproducir
        // lo que hice". Direct Replay puro es open-loop: si algo perturba al
        // vehiculo (choque, lag, debris, divergencia acumulada), sigue aplicando
        // inputs grabados ignorando la realidad fisica Ã¢â€ â€™ catastrofico.
        //
        // Fix arquitectural: detectar divergencia via offset al corredor y
        // switchear a Stanley puro cuando se excede threshold. Hysteresis para
        // evitar bouncing.
        //   offset > 5m: ENTRA en recovery (Direct Replay disabled, Stanley toma control)
        //   offset < 2m: SALE de recovery (Direct Replay resume)
        if (directReplayActive && m_CorridorValid) {
            float absOffsetDr = m_CorridorLateralOffset;
            if (absOffsetDr < 0) absOffsetDr = -absOffsetDr;
            if (!m_DR_InRecovery && absOffsetDr > 5.0) {
                m_DR_InRecovery = true;
                BZBusLog.Info("[DR-RECOVERY] ENTER (offset=" + absOffsetDr.ToString() + "m wp=" + m_WaypointIndex + ") Ã¢â‚¬â€ Stanley toma control");
            } else if (m_DR_InRecovery && absOffsetDr < 2.0) {
                m_DR_InRecovery = false;
                BZBusLog.Info("[DR-RECOVERY] EXIT (offset=" + absOffsetDr.ToString() + "m wp=" + m_WaypointIndex + ") Ã¢â‚¬â€ DirectReplay resume");
            }
        }
        if (m_DR_InRecovery) {
            directReplayActive = false; // Stanley + cruise predictivo se hacen cargo
        }

        // DIRECT REPLAY STEERING en PARKING/REVERSE/MANIOBRA Ã¢â‚¬â€ siempre activo, no
        // depende de directReplayActive ni DR-RECOVERY. Justamente cuando Boris
        // se desvio >5m por NO poder doblar (DR-RECOVERY=true), MAS necesita
        // copiar el steering del humano para retomar la curva. Stanley solo no
        // alcanza para curva 90Ã‚Â° de radio chico (es el problema original).
        // Si humano hizo |steering|>0.5 (full lock), copiar literal del recording.
        // 2026-06-07 17:30: descubierto al ver Boris max steering 0.29 cuando
        // humano grababa -1 full lock en wp 455-458 Ã¢â‚¬â€ el override estaba
        // bloqueado por DR-RECOVERY que se activo al desviarse 26m.
        if ((target.mode == "parking" || target.mode == "reverse" || target.mode == "maniobra") && target.hasInputData) {
            float absRecParkSteerGlobal = target.targetSteering;
            if (absRecParkSteerGlobal < 0) absRecParkSteerGlobal = -absRecParkSteerGlobal;
            // REVERSE 2026-06-13: umbral mas bajo (follow-recording). En reverse el
            // volante grabado MANDA Ã¢â‚¬â€ Boris copia el manual del humano en vez de Stanley
            // reactivo que satura y se pasa de eje. Parking/maniobra siguen en 0.5
            // (calibrados, no tocar). Stanley queda solo para tramos recorded~0.
            float recSteerThresh = 0.5;
            if (target.mode == "reverse") recSteerThresh = GetReverseRecordedSteerThreshold();
            if (absRecParkSteerGlobal > recSteerThresh) {
                // 2026-06-07 fix: solo aplicar direct replay si Boris esta CERCA del wp logico.
                // Si Boris se desvio >5m, esta perdido y el target_steering del wp logico
                // ya no corresponde a su posicion fisica Ã¢â€ â€™ aplicar full lock equivocadamente
                // hace que Boris patine en circulos en la salida de la maniobra. Stanley
                // toma para "llevar suave a la ruta" como dijo Sonom4n.
                vector wpPosDR = m_Config.Waypoints[m_WaypointIndex].GetVector();
                vector busPosDR = bus.GetPosition();
                float dxDR = busPosDR[0] - wpPosDR[0];
                float dzDR = busPosDR[2] - wpPosDR[2];
                float distDR = Math.Sqrt(dxDR*dxDR + dzDR*dzDR);
                if (distDR < 5.0) {
                    steering = target.targetSteering;
                    if (steering > 1.0)  steering = 1.0;
                    if (steering < -1.0) steering = -1.0;
                }
            }
        }

        // MODO 3 REVERSE (sin grabacion): forzar directReplayActive para que el control de
        // throttle de reversa (P controller que respeta targetSpeed + cap) corra aunque
        // hasInputData=0 (y aunque la DR-recovery lo haya apagado). El replay de steering
        // (~3697) sigue gateado a hasInputData -> NO se activa, asi que la direccion la dan
        // Stanley rear-steer + FF por curvatura = config-derived puro. Sin esto cae al
        // throttle de cruise -> overspeed -> el rear-steer no cierra el giro.
        if (m_Config && target.mode == "reverse") directReplayActive = true;

        if (directReplayActive) {
        ApplyDirectReplayControl(target, kmh, throttle, brake, steering);
        }

        bool reverseSteerFinalized = false;
        // === FOLLOW-RECORDING BLEND en reverse (2026-06-13) ===
        // El volante GRABADO es la BASE; Stanley solo suma una correccion FINA acotada.
        // El 20:33 grabo el reverse casi todo recto (steering 0) con un full-lock puntual
        // -> con el switch por umbral, Stanley manejaba TODO lo recto y a velocidad alta
        // desviaba a Boris (no subia la rampa derecho, full-lockeaba al arrancar). Asi
        // Boris va DERECHO donde el humano fue derecho, hace el full-lock donde el humano
        // lo hizo, y Stanley solo corrige offset suave. (recorded + stanley = pre-invert,
        // el invert de abajo los flippea juntos = consistente con el path probado.)
        if (isReversePk && target.hasInputData) {
            float fineStanRev = reverseStanleyComp;
            // DEADBAND (2026-06-13): si Boris esta SOBRE el eje (offset chico) o el corridor
            // es invalido (arranque a velocidad 0), NO corregir -> seguir el grabado EXACTO
            // (recto donde el humano fue recto). Sin esto, el Stanley saturado del arranque
            // metia +0.15 constante AUN con offset 0 -> Boris derivaba fuera de eje en el
            // backing inicial y despues le erraba a la puerta del galpon (Sonom4n: "fui derecho
            // al inicio"). Solo corrige cuando se va de verdad del corredor.
            float offAbsRev = m_CorridorLateralOffset;
            if (offAbsRev < 0) offAbsRev = -offAbsRev;
            // CONTROL DE HEADING (causa raiz, 2026-06-13): en reverse el bot debe FACE
            // segmentHeading + PI. El patron mostro que las que raspan la puerta llegan
            // SOBRE-ROTADAS (heading 138-141 vs 128-135 las que entran) con MISMO offset.
            // Por eso la correccion dispara tambien por ANGULO, no solo por offset: asi
            // corrige la sobre-rotacion TEMPRANO (en el codo) antes de que se vuelva
            // diagonal en la puerta. on-path = offset chico Y heading alineado -> recto.
            // FIX heading-grabado (Sonom4n 2026-06-24): medir alineacion contra el facing GRABADO del
            // wp actual (128), NO la tangente geometrica (segmentHeading+PI=137). Antes, con Boris
            // encarado como el humano (128), headErr daba ~9 grados "no alineado" y el Stanley lo
            // empujaba a 137 = ESTA era la sobre-rotacion (el control que debia evitarla la causaba,
            // por apuntar al angulo equivocado). Ahora alineado = encarado como vos.
            bool hasRecHeadB = (m_Config && m_Config.Waypoints && m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count() && m_Config.Waypoints[m_WaypointIndex].targetHeading != 0);
            float revRefHeadRad = m_CorridorSegmentHeading + Math.PI;
            if (hasRecHeadB) revRefHeadRad = m_Config.Waypoints[m_WaypointIndex].targetHeading * Math.DEG2RAD;
            float headErrRev = revRefHeadRad - currentYaw;
            while (headErrRev > Math.PI)  headErrRev -= 2.0 * Math.PI;
            while (headErrRev < -Math.PI) headErrRev += 2.0 * Math.PI;
            float headErrDegRev = headErrRev * Math.RAD2DEG;
            if (headErrDegRev < 0) headErrDegRev = -headErrDegRev;
            bool revOnPath = (offAbsRev < GetReverseSteerGateOffset());
            bool revAligned = (headErrDegRev < GetReverseHeadingDeadbandDeg());
            bool revHold = (revOnPath && revAligned);
            // Ir RECTO (seguir el grabado, sin correccion fina) si: el corridor es INVALIDO
            // (arranque del reverse, sin referencia confiable) O si esta sobre-el-eje Y alineado.
            // FIX 2026-06-13: al agregar el control de heading se perdio el "!m_CorridorValid ->
            // recto", asi que en el arranque (corridor invalido) Boris aplicaba 0.15 de garbage =
            // "arranca volanteando". Corregir solo cuando el corridor es VALIDO y esta desviado.
            if (!m_CorridorValid || revHold) {
                fineStanRev = 0;
            }
            float fineMaxRev = GetReverseStanleyFineMax();
            if (fineStanRev > fineMaxRev)  fineStanRev = fineMaxRev;
            if (fineStanRev < -fineMaxRev) fineStanRev = -fineMaxRev;
            // RECORDED RAW (2026-06-13): el volante GRABADO se aplica TAL CUAL (no invertido) Ã¢â‚¬â€
            // es el input crudo del humano (misma convencion que SetSteering). Stanley va
            // INVERTIDO (el -fineStanRev; se computo en frame flippeado +PI). Antes el invert
            // global flippeaba TAMBIEN el grabado -> Boris hacia el arco al REVES (Sonom4n 2026-06-13:
            // "marque arco grande a la izq, Boris corrige al lado contrario"). El Golf no lo mostraba
            // (su grabado reverse es 0, anulado -> solo Stanley). Marcamos finalized -> saltear invert.
            // TIMING (2026-06-13): leer el volante grabado del wp ACTUAL (posicion logica de
            // Boris), NO del lookahead 'target'. El lookahead se adelanta y se pasa del arco
            // grabado -> Boris leia 0 durante tu full-lock y lo aplicaba ~180 wps TARDE (Sonom4n:
            // "mucho delay, cuando corrige ya es tarde"). En reverse la maniobra es densa y
            // localizada -> el volante debe aplicarse en el punto exacto, no anticipado.
            float recSteerRev = target.targetSteering;
            if (m_Config.Waypoints && m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count()) {
                recSteerRev = m_Config.Waypoints[m_WaypointIndex].targetSteering;
            }
            steering = recSteerRev - fineStanRev;
            if (steering > 1.0)  steering = 1.0;
            if (steering < -1.0) steering = -1.0;
            reverseSteerFinalized = true;
        }

        // REVERSE STEERING INVERT: en reversa, el wheel-left causa que el back
        // del coche (que lidera el motion) curve a la DERECHA. Stanley computo
        // steering pensando en motion-direction (con target_yaw flippeado), pero
        // el input que va al vehiculo necesita la inversion adicional para que
        // la geometria salga al reves correcta. SKIP si el blend ya finalizo el
        // steering (recorded raw + Stanley ya invertido adentro).
        if (isReversePk && !reverseSteerFinalized) {
            steering = -steering;
        }

        // PURE-PURSUIT DE REVERSE (opt-in ReverseUsePurePursuit, aislado, 2026-08-10): OVERRIDE UNICO para
        // AMBAS ramas (hasInputData 0 y 1). El replay+Stanley/FF de arriba acumula/oscila en la curva -> el
        // LAZO (ai_run FT_03). Esto lo reemplaza por geometria MEMORYLESS desde el eje trasero (apunta a un
        // lookahead sobre la traza). Corre en TODA la reversa (no depende de hasInputData, por eso va aca y
        // no adentro del blend). El clamp de ReverseSteerMax de abajo sigue como red de seguridad. OFF = intacto.
        if (isReversePk && m_Config.ReverseUsePurePursuit) {
            steering = ComputeReversePurePursuit(bus.GetPosition(), currentYaw);
            if (steering > 1.0)  steering = 1.0;
            if (steering < -1.0) steering = -1.0;
        }

        // CLAMP REVERSE STEERING (2026-06-12): la primera correccion saturaba a 1.0
        // (full lock) y se pasaba del eje -> overshoot -> en non-minimum-phase diverge
        // y Boris se clava en el mismo lugar. El unico lap que completaba la maniobra
        // uso pico 0.40, prueba de que un volante modesto ALCANZA. Cap fuerza el
        // regimen suave (literatura reverse: gain alto oscila, no corrige mejor).
        if (isReversePk) {
            float revSteerMax = GetReverseSteerMax();
            if (steering > revSteerMax)  steering = revSteerMax;
            if (steering < -revSteerMax) steering = -revSteerMax;
            // 2026-06-25: reset de volante al entrar reversa Ã¢â‚¬â€ fuerza steering=0 los primeros
            // ticks post-snap (m_ReverseEntrySteerReset seteado en el ModeEntrySnap). Arranque
            // centrado y consistente (sino arrancaba con el volante girado ~-0.08 del approach).
            if (m_ReverseEntrySteerReset > 0) { steering = 0; m_ReverseEntrySteerReset = m_ReverseEntrySteerReset - 1; }
        }

        // STEER-THEN-THROTTLE en reverse: cuando el steer es fuerte (>0.3 abs),
        // reducir throttle. Replica tecnica humana: en reverse el operador
        // orienta el wheel y RECIEN AHI acelera Ã¢â‚¬â€ no ambas cosas a la vez (sino
        // el bot va en diagonal, no en linea + correcciÃƒÂ³n discreta). Idea del
        // usuario 2026-05-31.
        if (isReversePk) {
            float absSteerPk = steering;
            if (absSteerPk < 0) absSteerPk = -absSteerPk;
            // GRADUADO + AGRESIVO (Sonom4n 2026-06-12 "acomodo volante sigo / arranca girando"):
            // a mas steering, menos throttle, hasta 0 en correcciones grandes. Acomoda el
            // volante PRIMERO (casi sin moverse), despues mueve. Sin umbral duro -> incluso
            // correcciones chicas separan steer de move (el 0.4x flat sobre umbral 0.3 no
            // alcanzaba: arrancaba en diagonal). steering 0.4+ -> throttle 0 (solo volante).
            float steerThrFactorPk = 1.0 - absSteerPk * 2.5;
            // PISO (2026-06-12): NO bajar a 0. En reverse, velocidad cero = autoridad
            // de direccion cero (sin movimiento no rota el heading). Sin piso, doblar
            // fuerte frena en seco en la curva y Boris se clava (stall wp ~1050). El
            // piso mantiene un crawl que permite rotar mientras dobla Ã¢â‚¬â€ steer+throttle
            // coordinados, como el humano y como el unico lap que entraba (factor ~0.45).
            float thrFloorPk = GetReverseSteerThrottleFloor();
            if (steerThrFactorPk < thrFloorPk) steerThrFactorPk = thrFloorPk;
            throttle = throttle * steerThrFactorPk;
        }

        // MATCH GEAR para transicion REVERSE: detection por mode == "reverse"
        // (no por gear field). Cuando el wp esta en mode="reverse" y el bot no
        // esta en gear=0 (REVERSE), o cuando el wp NO esta en reverse pero el
        // bot todavia esta en gear=0, frenar y shiftear apropiadamente.
        // Los shifts entre gears positivos (2 <-> 4 por recovery, AT, etc.) no
        // necesitan intervencion Ã¢â‚¬â€ son shifts smoothly manejados.
        int botGearMg = bus.GetGear();
        bool wpWantsReverseMg = (target.mode == "reverse");
        bool botInReverseMg   = (botGearMg == 0);
        if (wpWantsReverseMg != botInReverseMg) {
            if (kmh > 1.5) {
                throttle = 0;
                brake    = 1.0;
            } else {
                if (wpWantsReverseMg) bus.ShiftTo(0); // REVERSE
                else                  bus.ShiftTo(2); // FIRST forward
            }
        }

        // === HYBRID THROTTLE FINAL OVERRIDE ===
        // Cuando el recording claramente estaba "full throttle sin freno" en
        // cruise normal, override ABSOLUTO de cualquier cap aplicado upstream
        // (RECOVERY THROTTLE LIMITER, OFFSET RECOVERY, etc). Respect the recording:
        // si el humano pisaba, Boris pisa. Sin esto, el offset cap entra en espiral
        // con vehiculos pesados (T6): zigzag -> sale del corredor -> throttle cap a
        // 0 -> stuck -> mas offset -> mas cap -> stuck eternamente. Bypass del cap
        // restaura movimiento; el OFFSET BRAKE separado sigue activo para casos
        // realmente extremos (>5m).
        // [AUDITORIA 26/07] eliminado HYBRID THROTTLE FINAL OVERRIDE (replay Modo1): gateado por hasInputData=0.
        if (false) {
            // No tocamos brake Ã¢â‚¬â€ el OFFSET BRAKE (>5m offset) puede haber aplicado
            // freno legitimo. Solo override del throttle.
        }

        // === DIRECT REPLAY SPEED ASSIST ===
        // Cuando DirectReplay esta activo y Boris esta MUY por debajo de la
        // velocidad target del recording (kmh < target.targetSpeed * 0.6), Y
        // el recording esta coasteando (throttle<0.2, brake<0.2), aplicar
        // throttle para alcanzar el target. Sin esto, Direct Replay falla si
        // Boris diverge: vos coasteabas a 37 km/h con momentum, Boris llego
        // a 15 km/h con menos momentum, throttle=0 del recording = friction
        // lo paro hasta 0 = stuck eterno.
        //
        // Dos niveles 2026-06-02 (T6 stationary loop test):
        //   - kmh < 2 Ã¢â€ â€™ throttle = 1.0 (vencer friccion estatica, mover desde quieto)
        //   - 2 <= kmh < target*0.6 Ã¢â€ â€™ throttle = 0.5 (acelerar suave hacia target)
        // Sin el nivel kmh<2, Boris stuck a 0 km/h post-coast no podia arrancar
        // porque throttle=0.5 en gear 3 no vence la friccion. Determinismo confirmado:
        // mismo stuck point cada respawn (8137, 9301) hasta speed assist agresivo.
        // [AUDITORIA 26/07] eliminado DIRECT REPLAY SPEED ASSIST (replay Modo1): gateado por hasInputData=0.

        // === CAPA 3+4: INVERSE MODEL OVERRIDE ===
        // Si UseInverseModel=true, computar throttle/brake desde target_speed
        // via PID + modelo inverso, en vez de replicar el target_throttle/brake
        // del recording. Vehicle-agnostic. Validado offline (simulator.js + bridge.js).
        // Default false Ã¢â€ â€™ bloque skip, comportamiento v1 (replay-based) sigue activo.
        if (m_Config.UseInverseModel && m_InverseModel && (target.mode == "normal" || target.mode == "approach") && !directReplayActive) {
        ApplyInverseModelControl(target, effApproachSpeed, kmh, throttle, brake, steering);
        }

        // === OFF-PATH RECOVERY (cruise mode) ===
        // Insight 2026-06-04 (Hatchback test): cuando Boris se sale del corredor
        // en cruise (no DR), el framework sigue leyendo target.throttle del
        // recording Ã¢â‚¬â€ pero el wp mÃƒÂ¡s cercano puede tener throttle=0 si vos
        // venias coasteando. Resultado: Boris sin throttle en cesped, Stanley
        // apunta al corredor pero sin momentum Ã¢â€ â€™ STUCK.
        //
        // Fix: cuando |lat_offset| supera threshold sostenido, ignorar el
        // recording (throttle/brake) y aplicar throttle forzado. Stanley sigue
        // dirigiendo hacia el corredor. Hysteresis para evitar bouncing:
        //   |offset| > 4m: ENTRA en recovery
        //   |offset| < 1.5m: SALE de recovery
        //
        // Spatial fidelity > timing fidelity ([[project_spatial_over_temporal_fidelity]]):
        // Boris se demora N segundos extra recuperando, pero LLEGA. Eso es lo
        // que importa. Eventos disparan por wp alcanzado, no por timer.
        // APAGABLE (2026-07-21, Sonom4n: "estamos intentando controlar a Boris por demas"). MEDIDO en las 3
        // tomas ESQ con el MISMO patron: ENTER recovery con offset 4-7 m en wp84-86, siempre en la curva
        // de 90, y Boris salia de ahi rotado ~45 deg respecto del intercambio (-55.9 / -43.3 / -44.8; el
        // humano llega a -0,2). El offset es FALSO: el corredor CORTA la esquina, no es que Boris se fue.
        // Ver el comentario de arriba, que ya describia el circulo vicioso. Con el flag en false manda el
        // pure-pursuit, que es lo que resuelve el cruise sin ayuda.
        if (!directReplayActive && m_CorridorValid && (!m_Config || m_Config.OffPathRecoveryEnabled)) {
            float absOffOp = m_CorridorLateralOffset;
            if (absOffOp < 0) absOffOp = -absOffOp;
            if (!m_OffPath_InRecovery && absOffOp > 4.0) {
                m_OffPath_InRecovery = true;
                BZBusLog.Info("[OFF-PATH] ENTER recovery (offset=" + absOffOp.ToString() + "m wp=" + m_WaypointIndex + ") Ã¢â‚¬â€ throttle forzado, recording ignorado");
            } else if (m_OffPath_InRecovery && absOffOp < 1.5) {
                m_OffPath_InRecovery = false;
                BZBusLog.Info("[OFF-PATH] EXIT recovery (offset=" + absOffOp.ToString() + "m wp=" + m_WaypointIndex + ") Ã¢â‚¬â€ recording resume");
            }
        }
        if (m_OffPath_InRecovery) {
            // Override para volver al corredor (Stanley sigue apuntando al path).
            // El throttle forzado era para NO perder momentum en cruise LENTO (Boris se salia y el
            // recording le soltaba el gas). PERO a ALTA velocidad forzar throttle ACELERA a Boris
            // derecho fuera del path si el Stanley no alcanza a corregir (validado 2026-07-01: Nissan
            // 76->92 km/h en recovery, offset 4->219m, choque). FIX: a alta velocidad NO acelerar Ã¢â‚¬â€
            // FRENAR suave para bajar la velocidad off-path y darle al Stanley autoridad de volver
            // (a menor velocidad la correccion 1/v es mas fuerte). A baja velocidad, mantener el
            // force-throttle (recuperar momentum, uso original). UNIVERSAL: un desvio ya no es un choque.
            float offKmhRec = bus.GetSpeedometerAbsolute();
            float offTgtRec = target.targetSpeed;
            // NO forzar gas si va PASADO del target (tÃƒÂ­pico en CURVA: el corredor la corta -> offset alto
            // falso -> la recovery le metÃƒÂ­a throttle=0.6 y PISABA el overspeed-cut -> entraba embalado a la
            // 90 y se abrÃƒÂ­a mÃƒÂ¡s -> mÃƒÂ¡s off-path. Vicioso). Forzar gas SOLO cuando estÃƒÂ¡ lento y por debajo
            // del target (uso original: recuperar momentum en cruise lento off-path). 2026-07-12.
            if (offKmhRec > 25.0 || (offTgtRec > 0.5 && offKmhRec > offTgtRec + 2.0)) {
                throttle = 0;
                if (brake < 0.35) brake = 0.35;    // preserva el freno mÃƒÂ¡s fuerte del cut (0.85) si ya frenaba
            } else {
                throttle = 0.6;
                brake = 0;
            }
        }

        // === DISCRETE INPUT PATTERN en PARKING/REVERSE ===
        // 2026-06-07 Impreza: si humano estaba idle (sin throttle ni brake) en
        // parking o reverse, Boris idle tambien (steering=0). Reproduce el
        // patron humano de "girar Ã¢â€ â€™ quieto Ã¢â€ â€™ acelerar Ã¢â€ â€™ quieto" en vez del
        // mecanismo simultaneo Stanley+P-controller. Solo aplica si NO esta
        // en off-path recovery (ahi Stanley debe seguir corrigiendo).
        // [AUDITORIA 26/07] eliminado DISCRETE INPUT PATTERN parking/reverse (replay Modo1): gateado por hasInputData=0.

        // === END FREEZE Ã¢â‚¬â€ OnEnd: freeze por default (2026-06-13) ===
        // El recording suele terminar con un TAIL de targetSpeed~0: el humano llego a su
        // posicion final y el logger siguio grabandolo quieto (ej V3S: para en wp ~4730,
        // 180 wps mas de idle hasta el 4912). Ese tail le decia a Boris "anda a 3km/h"
        // (MIN_PROGRESSION floor) + disparaba el KICK de baja velocidad => Boris no frenaba
        // y empujaba contra la pared. El viejo hold (kmh<2) nunca enganchaba por eso.
        // FIX: si el wp actual esta casi-detenido Y no hay NINGUN wp mas rapido por delante
        // hasta el final (= es la parada FINAL, no una intermedia tipo parada de bus),
        // frenar a fondo + handbrake y CONGELAR, sin importar la velocidad actual ni el mode
        // (el tail puede quedar tagueado normal si el humano saco el cambio al estacionar).
        // El return pisa al kick y a todo lo de arriba. Route-overridable: EndFreezeDisabled.
        bool atEndOfRoute = false;
        if (m_Config && m_Config.Waypoints && m_Config.EndFreezeDisabled == 0 && target.targetSpeed < 0.5) {
            int cntEr = m_Config.Waypoints.Count();
            bool fasterAhead = false;
            for (int ie = m_WaypointIndex + 1; ie < cntEr; ie++) {
                if (m_Config.Waypoints[ie].targetSpeed > 1.5) { fasterAhead = true; break; }
            }
            if (!fasterAhead) atEndOfRoute = true;
        }
        if (atEndOfRoute) {
            throttle = 0;
            brake = 1.0;
            CtlSnap("atEndOfRoute", throttle, brake, steering);   // TRACER: este congela y hace return -> sin esto era invisible
            CtlFlush(busPos, kmh);
            SetCachedHandbrake(1.0);
            SetCachedInput(throttle, steering, brake);
            m_DespawnRecoveryCount = 0;   // corrida completada OK: resetea el guard anti-loop del despawn-proof
            m_LastDespawnWp = -1;
            return;
        }

        CtlSnap("stopBlock", throttle, brake, steering);   // TRACER: aisla el freno de parada / suppressSteer del hueco
        // === CORTE DE THROTTLE ANTICIPATORIO POR CURVATURA (2026-06-10) ===
        // En cruise normal, si viene una curva cerrada, baja el throttle ANTES de que
        // arranque el volante. Ataja "acelera-antes-de-girar" (Boris metia gas con el
        // volante derecho, ganaba momentum y entraba mal Ã¢â‚¬â€ 2da/3ra curva del recording).
        // Distinto del corte reactivo (|steering|>0.3 ya alto): este mira la curvatura
        // que VIENE. Solo cruise normal, fuera de off-path recovery, y a velocidad
        // donde el momentum importa (>12 km/h; a baja velocidad no hace falta).
        if (GetCurveThrottleEnabled() && target.mode == "normal" && !m_OffPath_InRecovery && throttle > 0 && kmh > 12.0) {
            float cuBend = ComputeUpcomingBend(m_WaypointIndex, GetCurveThrottleLookaheadM());
            float cuStart = GetCurveThrottleStartDeg();
            float cuFull = GetCurveThrottleFullDeg();
            if (cuBend > cuStart) {
                float cuT = (cuBend - cuStart) / (cuFull - cuStart);
                if (cuT > 1.0) cuT = 1.0;
                float cuScale = 1.0 - cuT * (1.0 - GetCurveThrottleMinScale());
                throttle = throttle * cuScale;
                if (cuScale < 0.9 && !m_CurveCutActive) {
                    BZBusLog.Info("[CurveThrottle] curva adelante wp=" + m_WaypointIndex + " bend=" + cuBend + "deg throttle x" + cuScale);
                    m_CurveCutActive = true;
                } else if (cuScale >= 0.9 && m_CurveCutActive) {
                    m_CurveCutActive = false;
                }
            } else if (m_CurveCutActive) {
                m_CurveCutActive = false;
            }
        }
        CtlSnap("curveThr", throttle, brake, steering);

        // Curvature steering BOOST: el Stanley sub-comanda en 90s (steering no satura -> el receptor
        // eAI sub-gira -> se abre). En curva amplificamos el steering (MISMO signo del Stanley, seguro)
        // para forzar el compromiso. Config-derived, gated cruise normal sin recording, capeado a +-1.
        if (m_Config && m_Config.CurvatureSteerBoost > 0 && target.mode == "normal" && !m_OffPath_InRecovery && !target.hasInputData) {
            float sbBend = ComputeUpcomingBend(m_WaypointIndex, 22.0);
            if (sbBend > 25.0) {
                float sbFrac = sbBend / 90.0;
                if (sbFrac > 1.0) sbFrac = 1.0;
                steering = steering * (1.0 + m_Config.CurvatureSteerBoost * sbFrac);
                if (steering > 1.0) steering = 1.0;
                if (steering < -1.0) steering = -1.0;
            }
        }

        // === FRENO DE TRANSICION A REVERSE ===
        // Al acercarse al inicio del reverse (en forward), desacelerar a paso de hombre para
        // LLEGAR LENTO Y CONTROLADO (sino acelera + sobre-rota + se va contra la banquina). Una
        // vez lento, el handbrake-honor + el MODE SNAP clavan el punto + orientacion exactos.
        //
        // ACOTADO AL TRAMO ACTIVO (2026-07-21, EL INTRUSO — Sonom4n: "sigue capeado por algo... está en
        // nuestras lineas"). MEDIDO: este bloque escaneaba 80 wps adelante, VEIA la reversa del tramo
        // SIGUIENTE (que Boris todavia no deberia conocer) y como wp76 esta a 11 m del wp93, clavaba
        // freno 0,5 a paso de hombre EN PLENA CURVA -> Boris frenaba a 0 antes de ejecutarla, donde el
        // humano iba a 18-19 km/h. Viola el principio del tramo latcheado ("Boris no sabe de la reversa
        // hasta hacer checkpoint en el intercambio"). Ademas es REDUNDANTE: el perfil de velocidad ya
        // baja a 0 en el intercambio y el brake-ahead ya hornea la desaceleracion. Con el escaneo
        // acotado a m_LegEnd, en un tramo forward la reversa del tramo siguiente no aparece -> no frena;
        // solo actuaria si el reverse estuviera DENTRO del tramo actual (no es el caso con marcas).
        if (ShouldReverseTransitionBrake(bus, target)) { throttle = 0; brake = 0.5; }
        CtlSnap("revTrans1", throttle, brake, steering);

        // === ANTI-STALL REVERSE ===
        // El UAZ pesado a veces se clava a mitad de un reverse (punto cerrado, volante a
        // fondo): queda casi parado y el throttle computado no alcanza para re-arrancar.
        // Si esta en reverse, casi parado, LEJOS del final, y no frenando a proposito ->
        // piso de throttle fuerte para despegarlo. Se libera apenas se mueve (>1 km/h).
        if (target && target.mode == "reverse" && bus.GetSpeedometerAbsolute() < 1.0 && brake < 0.1 && m_Config && m_Config.Waypoints) {
            int lastIdxAS = m_Config.Waypoints.Count() - 1;
            float distFinalAS = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[lastIdxAS].GetVector());
            if (distFinalAS > REVERSE_ARRIVAL_TOL && throttle < 0.8) {
                throttle = 0.8;
            }
        }

        // === YAW-RATE FEEDBACK (imÃƒÂ¡n acotado por grip) Ã¢â‚¬â€ 2026-07-07 ===
        // Sensamos la rotacion REAL del cuerpo (dBodyGetAngularVelocity[1]) pero no la usabamos: el
        // understeer del FF es estatico. Aca cerramos el lazo VIVO. yaw deseado (magnitud) = v/R del path.
        // Si el cuerpo sub-rota (understeer real, ej grav) -> agrega volante EN LA DIRECCION del steering
        // que ya viene bien firmado del FF/Stanley. Robusto a signo (usa |yaw|). Solo corrige understeer
        // (el overshoot = irse ancho); no toca oversteer. M2/M3 (mode normal, sin recording).
        if (GetYawFeedbackGain() != 0 && target.mode == "normal" && !target.hasInputData && kmh > GetYawFeedbackMinKmh() && Math.AbsFloat(steering) > 0.05 && m_Config && m_Config.Waypoints) {
            int yfN = m_Config.Waypoints.Count();
            int yfK = 5;
            if (m_WaypointIndex >= yfK && m_WaypointIndex < yfN - yfK) {
                float yfR = CircumRadius2D(m_Config.Waypoints[m_WaypointIndex - yfK].GetVector(), m_Config.Waypoints[m_WaypointIndex].GetVector(), m_Config.Waypoints[m_WaypointIndex + yfK].GetVector());
                if (yfR > 0.5 && yfR < 500) {
                    float yfVms = kmh / 3.6;
                    float yawDesMag = yfVms / yfR;                          // rad/s magnitud deseada (v/R)
                    vector yfAngVel = dBodyGetAngularVelocity(bus);        // rad/s real (usamos magnitud)
                    m_YawEMA = m_YawEMA * 0.6 + yfAngVel[1] * 0.4;          // suavizar (ruidoso)
                    float yawActMag = Math.AbsFloat(m_YawEMA);
                    if (yawActMag < yawDesMag) {                            // sub-rota = understeer
                        float yfSteerSign = 1.0;
                        if (steering < 0) yfSteerSign = -1.0;
                        float yfCorr = GetYawFeedbackGain() * (yawDesMag - yawActMag) * yfSteerSign;
                        float yfCap = GetYawFeedbackCap();
                        if (yfCorr > yfCap) yfCorr = yfCap;
                        if (yfCorr < -yfCap) yfCorr = -yfCap;
                        steering = steering + yfCorr;
                        if (steering > 1.0) steering = 1.0;
                        if (steering < -1.0) steering = -1.0;
                    }
                }
            }
        }

        // RATE-LIMITER del volante (2026-07-11, red de seguridad anti-zigzag): limita el |cambio| de steering
        // por tick. Con Ld adaptativo por curvatura no deberia hacer falta, pero atrapa cualquier oscilacion
        // residual (el fast-loop fallo por NO tener esto). 0 = OFF (baseline intacto).
        // GATE POR VELOCIDAD (2026-07-13): el rate-limiter clava el volante contra el vaivÃƒÂ©n del tick de 500ms a
        // ALTA velocidad. Pero aplicado abajo tocarÃƒÂ­a el modo edit (perfecto <50). SteerRateLimitMinKmh>0 -> solo
        // actÃƒÂºa arriba de ese umbral -> red anti-vaivÃƒÂ©n en la recta rÃƒÂ¡pida, lÃƒÂ­nea de baja velocidad INTACTA.
        if (m_Config.SteerRateLimitPerTick > 0 && kmh >= m_Config.SteerRateLimitMinKmh) {
            float maxDSteer = m_Config.SteerRateLimitPerTick;
            float dSteer = steering - m_PrevSteering;
            if (dSteer > maxDSteer)  steering = m_PrevSteering + maxDSteer;
            if (dSteer < -maxDSteer) steering = m_PrevSteering - maxDSteer;
        }

        // AMORTIGUADO AL FRENAR (tick principal): clava la direcciÃƒÂ³n mientras frena fuerte a velocidad (mismo
        // criterio que el fast loop). El giro sostenido llega igual; la sacudida del yaw por el frenado se amortigua.
        if (m_Config.SteerBrakeDampEnabled && brake > m_Config.SteerBrakeDampMinBrake && kmh > m_Config.SteerBrakeDampMinKmh) {
            float denomSd2 = m_Config.SteerBrakeDampFullKmh - m_Config.SteerBrakeDampMinKmh;
            if (denomSd2 < 1.0) denomSd2 = 1.0;
            float spdFsd2 = (kmh - m_Config.SteerBrakeDampMinKmh) / denomSd2;
            if (spdFsd2 > 1.0) spdFsd2 = 1.0;
            float effAsd2 = 1.0 - spdFsd2 * (1.0 - m_Config.SteerBrakeDampAlpha);
            steering = m_PrevSteering + (steering - m_PrevSteering) * effAsd2;
        }

        // ENDPOINT CREEP slope-aware (FollowPaintedToStop, 2026-07-13): ULTIMA palabra sobre throttle/brake en la
        // parada. Honramos la decel PINTADA toda la aproximacion (el InverseModel apunta a la pintada); aca, en los
        // ultimos metros, si Boris quedo CORTO del punto DIBUJADO (subida, se quedo sin velocidad) forzamos un creep
        // proporcional a la pendiente para clavar el endpoint EXACTO; si esta encima/pasado, frena. Apunta al punto
        // dibujado real (no a STOP_FINAL_RADIUS de un cartel) -> precision de estacionamiento. Solo con el flag ON.
        // GATE: el creep viejo entra con iSpeed<1.5 (ya casi encima del punto). El FRENO UNIVERSAL necesita
        // entrar ANTES para poder frenar a tiempo -> con EndpointGlide toma el control dentro de RangeM.
        m_EpGlideShort = false;
        bool endGateOn = (iSpeed < 1.5);
        if (m_Config && m_Config.EndpointGlide && distToNextStop < m_Config.EndpointGlideRangeM) endGateOn = true;
        // (2026-07-22 re-revertido: EndpointGlide vuelve a manejar forward/endpoint. La consolidacion que lo
        // apagaba cerca del checkpoint deadlockeaba interc1 con el creep forward. El corrector de controles sera la via.)
        if (m_Config && m_Config.FollowPaintedToStop && endGateOn && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            vector bposEnd = bus.GetPosition();
            vector swpEnd = m_Config.Waypoints[m_NextStopIndex].GetVector();
            float dxEnd = swpEnd[0] - bposEnd[0];
            float dzEnd = swpEnd[2] - bposEnd[2];
            float gapEnd = Math.Sqrt(dxEnd * dxEnd + dzEnd * dzEnd);   // distancia FISICA al punto dibujado exacto
            float slEnd = 0;
            if (gapEnd > 1.0) slEnd = (swpEnd[1] - bposEnd[1]) / gapEnd;   // + = subida hacia el stop
            // PENDIENTE SANA (2026-07-17, MEDIDO): la de arriba compara la Y del WP contra la del BUS, y con
            // SampleTerrainY=true la del wp es la del TERRENO mientras que el origen del vehiculo va ~0.52m
            // MAS ARRIBA -> en terreno LLANO da -34% (bajada fantasma) y envenena el creep/freno. Con el flag,
            // se mide terreno-contra-terreno (SurfaceY vs SurfaceY): pendiente REAL de la calzada.
            if (m_Config.EndpointGlide && gapEnd > 1.0) {
                float ySrfBus = GetGame().SurfaceY(bposEnd[0], bposEnd[2]);
                float ySrfWp  = GetGame().SurfaceY(swpEnd[0], swpEnd[2]);
                slEnd = (ySrfWp - ySrfBus) / gapEnd;
                if (slEnd > 0.5) slEnd = 0.5;
                if (slEnd < -0.5) slEnd = -0.5;
            }
            if (m_Config.EndpointGlide) {
                // GAP CON SIGNO (2026-07-17, MEDIDO): gapEnd es una DISTANCIA (sin signo) -> apenas Boris
                // PASA el punto vuelve a crecer y el creep lo empuja MAS LEJOS todavia (medido: toco 0.020
                // y siguio acelerando con throttle 0.22 hasta 0.956 PASADO). Proyecto sobre la direccion de
                // llegada: >0 = falta llegar; <=0 = llego/paso -> CLAVAR. El endpoint es un primitivo
                // general (Sonom4n: "lo estamos haciendo detenerse en cualquier contexto"): un frenazo es
                // aceptable; pasarse de largo NO.
                float signedEnd = gapEnd;
                int piEnd = m_NextStopIndex - 1;
                while (piEnd > 0) {
                    vector pvEnd = m_Config.Waypoints[piEnd].GetVector();
                    float ddxEnd = swpEnd[0] - pvEnd[0];
                    float ddzEnd = swpEnd[2] - pvEnd[2];
                    if (ddxEnd * ddxEnd + ddzEnd * ddzEnd > 1.0) break;   // wp a >=1m: direccion estable
                    piEnd = piEnd - 1;
                }
                if (piEnd >= 0 && piEnd < m_NextStopIndex) {
                    vector prevEnd = m_Config.Waypoints[piEnd].GetVector();
                    float fxEnd = swpEnd[0] - prevEnd[0];
                    float fzEnd = swpEnd[2] - prevEnd[2];
                    float fnEnd = Math.Sqrt(fxEnd * fxEnd + fzEnd * fzEnd);
                    if (fnEnd > 0.01) {
                        fxEnd = fxEnd / fnEnd;
                        fzEnd = fzEnd / fnEnd;
                        signedEnd = dxEnd * fxEnd + dzEnd * fzEnd;   // dx/dz van DEL bus AL punto
                    }
                }
                // [EndGap] DIAGNOSTICO (2026-07-29): a que distancia del punto quedo Boris cuando para. Mide la
                // precision del endpoint sin depender de acordarse donde estaba el marcador. Util en todo el frente.
                if (kmh < 0.5) BZBusLog.Info("[EndGap] Boris parado a signed=" + signedEnd + "m (gap=" + gapEnd + "m) del endpoint wp" + m_NextStopIndex);
                // FRENO UNIVERSAL (2026-07-17): sirve en CUALQUIER contexto (rapido, lento, en pendiente,
                // desde grabacion) porque es FISICA, no perillas: la desaceleracion que hace falta para
                // parar EN el punto es a = v^2/(2*d), y la gravedad suma/resta segun la pendiente REAL.
                //   brake = (a_necesaria - g*sin(pendiente)) / decel_max_del_vehiculo
                // Viene rapido -> a alta -> freno fuerte (un frenazo es ACEPTABLE, pasarse NO).
                // Viene lento  -> a ~0  -> no frena; si ni llega (subida), creep slope-aware.
                // En bajada    -> la gravedad EMPUJA -> pide MAS freno. En subida -> ya frena sola -> menos.
                // Es la ULTIMA palabra sobre throttle/brake -> tambien pisa el brake=1.0 prematuro que
                // frenaba en seco a 1.5m (distRemaining negativa dentro de STOP_FINAL_RADIUS).
                if (signedEnd <= m_Config.EndpointGlideStopM) {
                    throttle = 0;
                    brake = 1.0;
                    if (!m_EndpointLatched) BZBusLog.Info("[GlideLatch] DriveTowards: LLEGO signed=" + signedEnd + " gap=" + gapEnd + " kmh=" + kmh);
                    m_EndpointLatched = true;   // TOCO el punto -> clavar y no creepear mas
                }
                else {
                    // CHECKPOINT forward (intercambio, no el endpoint final): el creep todavia esta clavando el
                    // ORIGEN -> avisa a UpdateLegBounds que NO cierre el tramo aun (llega a <CheckpointCloseTolM).
                    if (m_LegInit && m_LegEnd < m_Config.Waypoints.Count() - 1 && signedEnd > m_Config.CheckpointCloseTolM) m_EpGlideShort = true;
                    float vMsEnd = kmh / 3.6;
                    float aNeedEnd = (vMsEnd * vMsEnd) / (2.0 * signedEnd);
                    aNeedEnd = aNeedEnd - 9.8 * slEnd;   // subida: la gravedad ya frena -> menos freno
                    float bDecEnd = m_Config.BrakeDecelMS;
                    if (bDecEnd <= 0) bDecEnd = 7.0;
                    float bFracEnd = aNeedEnd / bDecEnd;
                    if (bFracEnd > 1.0) bFracEnd = 1.0;
                    if (bFracEnd > 0.03) {
                        throttle = 0;
                        brake = bFracEnd;
                    }
                    else if (kmh < m_Config.EndpointGlideMaxKmh) {
                        float creepG = 0.14 + slEnd * 1.5;
                        if (creepG < 0.10) creepG = 0.10;
                        if (creepG > 0.55) creepG = 0.55;
                        // KICK-START: parado y todavia corto -> empujon firme para arrancar (el creep no vence
                        // la inercia estatica en gear alto). Apenas se mueve, el creep normal lo mantiene.
                        if (kmh < m_Config.EndpointKickExitKmh && signedEnd > m_Config.CheckpointCloseTolM && m_Config.EndpointKickThrottle > creepG) creepG = m_Config.EndpointKickThrottle;
                        throttle = creepG;
                        brake = 0;
                    }
                    else {
                        throttle = 0;
                        brake = 0;
                    }
                }
            }
            else if (gapEnd > 0.4) {
                // corto del punto -> creep para alcanzarlo (mas gas en subida); si ya viene con velocidad, no tocar
                if (kmh < 4.0) {
                    float creepEnd = 0.14 + slEnd * 1.5;
                    if (creepEnd < 0.10) creepEnd = 0.10;
                    if (creepEnd > 0.55) creepEnd = 0.55;
                    // KICK-START: parado y corto -> empujon firme para vencer la inercia (ver EndpointKickThrottle)
                    if (kmh < m_Config.EndpointKickExitKmh && gapEnd > m_Config.CheckpointCloseTolM && m_Config.EndpointKickThrottle > creepEnd) creepEnd = m_Config.EndpointKickThrottle;
                    throttle = creepEnd;
                    brake = 0;
                }
            } else {
                // encima/pasado el punto exacto -> clavar
                throttle = 0;
                if (kmh > 0.4) brake = 1.0;
            }
        }

        // === LEY LONGITUDINAL UNICA (2026-07-17, UseSpeedLookahead) ===
        // Va ULTIMO: pisa al patchwork reactivo (overspeed-cut/catchup/throttle-gap) que peleaba = saltitos.
        // effApproachSpeed YA es el target por lookahead (min de la ventana). UN tracker fisico:
        //   sobra vel -> freno proporcional al exceso ; falta -> el throttle del inverse model ; en banda -> coast.
        if (m_Config && m_Config.UseSpeedLookahead && (target.mode == "normal" || target.mode == "")) {
            float bandLa = m_Config.ApproachCoastBand;
            if (bandLa <= 0) bandLa = 2.0;
            float errLa = kmh - effApproachSpeed;   // >0 sobra, <0 falta
            // GRADE-HOLD EN BAJADA (2026-07-26, SEQ1 endpoint tras curva): DENTRO de la banda esta ley ponia
            // brake=0 -> en bajada la GRAVEDAD flotaba a Boris fuera de la banda (~1.5 km/h/tick en -9%) -> freno
            // -> cae -> vuelve = serrucho (medido con el tracer [CTL]). Sostenemos con el freno que CANCELA la
            // gravedad de la bajada al punto: Boris se SIENTA en el target en vez de flotar. 0 en llano/subida ->
            // comportamiento intacto. En "falta vel" NO frenamos (dejamos que gravedad/throttle lo suban).
            float gradeHoldLa = 0;
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                float slLa = SlopeToPoint(busPos, m_Config.Waypoints[m_NextStopIndex].GetVector());
                if (slLa < 0) {
                    float bDecLa = m_Config.BrakeDecelMS;
                    if (bDecLa < 1.0) bDecLa = 7.0;
                    gradeHoldLa = (9.8 * (-slLa)) / bDecLa;
                }
            }
            if (errLa > bandLa) {
                brake = errLa * m_Config.SpeedLookaheadBrakeGain + gradeHoldLa;
                if (brake > 0.85) brake = 0.85;
                throttle = 0;
            } else if (errLa < -bandLa) {
                brake = 0;   // falta vel -> dejar acelerar (gravedad en bajada + inverse model)
            } else {
                // EN LA BANDA: el grade-hold SOLO cuando esta AL/POR ENCIMA del target (errLa>0), para que no
                // FLOTE mas arriba en bajada. Por DEBAJO (errLa<=0) NO frenar ni capear el gas -> dejar que la
                // gravedad+throttle lo SUBAN al target (si no, el grade-hold lo dejaba clavado lento -> "muy lenta").
                if (errLa > 0) {
                    brake = gradeHoldLa;
                    if (throttle > m_Config.ApproachCoastHold) throttle = m_Config.ApproachCoastHold;
                } else {
                    brake = 0;
                }
            }
        }

        // === BANDA DE COAST (2026-07-17): RESPETAR la velocidad del target (la tuya, grabada o dibujada) ===
        // Hoy el control rebota ALREDEDOR del target (brake<->catchup) = saltitos. Cuando kmh esta dentro de
        // +-ApproachCoastBand del target -> ni freno ni catchup -> coast -> Boris SE SIENTA en tu velocidad.
        // Solo forward normal (reverse y endpoint se manejan aparte). Opt-in. NO baja el target: solo deja de
        // sobre-corregir cerca de el. (Se apaga si UseSpeedLookahead: la ley de arriba ya lo incluye.)
        // CERCA DE UN CHECKPOINT NO CORRE (2026-07-22, Sonom4n): coastBand es para CRUCERO (sentarse en la
        // velocidad, no sobre-corregir). En la aproximacion a un checkpoint capeaba el gas a 0 -> arrastre.
        // Ahi manda solo el StopBrake slope-aware. Fuera de esa zona, coastBand normal.
        if (m_Config && !nearCheckpoint && !m_Config.UseSpeedLookahead && m_Config.ApproachCoastBand > 0 && (target.mode == "normal" || target.mode == "")) {
            float diffCb = kmh - effApproachSpeed;
            if (diffCb < m_Config.ApproachCoastBand && diffCb > -m_Config.ApproachCoastBand) {
                if (brake > 0) brake = 0;
                if (throttle > m_Config.ApproachCoastHold) throttle = m_Config.ApproachCoastHold;
            }
        }
        CtlSnap("coastBand", throttle, brake, steering);

        // === APROXIMACION A LA TRANSICION REVERSE POR FISICA (2026-07-17) === (extraido a
        // ComputeReverseApproach para no pasar el limite de instrucciones de DriveTowards). Va ULTIMO.
        // CERCA DE UN CHECKPOINT NO CORRE (2026-07-22, Sonom4n): revApproach capeaba el gas ("floor" 0.22) y
        // metia su propio freno, peleandole al StopBrake slope-aware -> arrastre. En la zona del checkpoint
        // manda solo el StopBrake (que frena seco/tarde en subida, temprano en bajada, segun la pendiente).
        if (m_Config && m_Config.ReverseApproachStop && !nearCheckpoint && target.mode != "reverse" && bus.GetGear() != 0) {
            ComputeReverseApproach(busPos, kmh);
            // El sostenimiento es PISO, NO TECHO (2026-07-20, medido): reemplazar el throttle mataba lo que
            // el lazo cerrado (InverseModel) queria meter para alcanzar la velocidad que pide el ojo -> con
            // 0.22 fijo el M3 seguia desacelerando y entraba al intercambio a 2 km/h donde el humano iba a 10.
            // El freno fisico SI manda (es el que ordena llegar a la puerta a GateKmh).
            if (m_RaActive) {
                if (m_RaBrk > 0.0) { throttle = 0; brake = m_RaBrk; }
                else { if (throttle < m_RaThr) throttle = m_RaThr; brake = 0; }
            }
        }

        // ANTI-PLANTADA POR COASTEO hacia el ENDPOINT (2026-07-20). Medido: el M3 solto todo a 11.9 km/h
        // a 11.6 m del punto, se planto a 8.5 m y ahi quedo 4.5 s con throttle 0 (el creep del endpoint
        // solo engancha dentro de 3 m, asi que ni siquiera disparo) hasta que lo saco el AR. Con el
        // planeo MEDIDO del vehiculo: si lo que falta no entra en el planeo, sostener en vez de soltar.
        bool coastingNow = (throttle < 0.05 && brake < 0.05);
        if (coastingNow && m_Config && m_Config.CoastGuardEnabled && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            vector ggStop = m_Config.Waypoints[m_NextStopIndex].GetVector();
            float ggdx = busPos[0] - ggStop[0];
            float ggdz = busPos[2] - ggStop[2];
            ComputeGlideGuard(kmh, Math.Sqrt(ggdx * ggdx + ggdz * ggdz), busPos);
            if (m_GgActive) throttle = m_GgThr;
        }
        CtlSnap("revApproach+coastGuard", throttle, brake, steering);
        // (el aprendizaje del coasteo vive en TickBody: aca solo se veria 1 de los 21 caminos del control)

        // GATE DE POSE: si la transicion de salida todavia no esta alineada, rotar en vez de seguir viaje.
        // Va ULTIMO, despues de todo lo demas: mientras alinea, manda el.
        // Doble condicion a proposito: la bandera Y la cercania real al wp. Que alinee SOLO estando encima
        // del punto; si esta lejos, es que la bandera quedo pegada y no debe tocar nada (bug del 20/07).
        if (m_PoseGateActive && m_Config && m_Config.PoseGateEnabled) {
            float dPoseChk = vector.Distance(busPos, target.GetVector());
            if (dPoseChk < 6.0) ApplyPoseAlign(throttle, brake, steering);
            else m_PoseGateActive = false;
        }
        CtlSnap("poseGate", throttle, brake, steering);

        // RAMPA DE DESPEGUE: Boris parado, sin freno pedido y con el punto todavia por delante -> subir el
        // acelerador hasta que se mueva. Medido: quedaba clavado a 5.4 m del intercambio y a 8.5 m del
        // endpoint porque ningun valor fijo lo movia. Va ULTIMO y solo en ese caso puntual.
        // FASE DE VERIFICACION DEL STOP (2026-07-20, Sonom4n: "un intercambio es simplemente: aca el
        // velocimetro marco 0"). El objetivo era "estar a 0" y Boris lo cumplia EN EL LUGAR EQUIVOCADO:
        // medido, quedo 9,6 s parado A 1,96 m DEL ENDPOINT con el freno puesto y cero gas, dando el stop
        // por cumplido. Un stop no es una velocidad: es una velocidad EN UN PUNTO. Mientras falte
        // distancia, se suelta el freno y se empuja -- aunque el control crea que ya llego.
        // OJO: la rampa SOLO empuja si el punto esta ADELANTE (o atras, si va en reversa). Sin este chequeo
        // le metia gas hacia donde estuviera mirando y se iba para cualquier lado (medido 20/07).
        if (m_Config && m_Config.BreakawayRampEnabled && kmh < 1.0) {
            vector tgtRamp = target.GetVector();
            float dRamp = vector.Distance(busPos, tgtRamp);
            float hdRamp = m_Bus.GetOrientation()[0] * Math.DEG2RAD;
            float fxRamp = Math.Sin(hdRamp);
            float fzRamp = Math.Cos(hdRamp);
            float dotRamp = (tgtRamp[0] - busPos[0]) * fxRamp + (tgtRamp[2] - busPos[2]) * fzRamp;
            if (bus.GetGear() == 0) dotRamp = -dotRamp;   // en reversa el objetivo va detras
            if (dRamp > m_Config.StopResidualTolM && dRamp < 12.0 && dotRamp > 0) {
                float rampThr = BreakawayRamp(busPos, kmh);
                if (rampThr > 0) { throttle = rampThr; brake = 0; }
            }
        }

        // CORRECTOR DE VELOCIDAD contra la toma (2026-07-23, Sonom4n): en TODO momento (cruise incluido) monitorea
        // vel vs cota grabada y corrige SUAVE (rampa, sin traqueteo): se pasa -> un poquito de freno; va lento ->
        // un poquito de gas. Va ANTES del StopBrake/checkpoint (que lo pisan en la parada); en cruise queda esto.
        ComputeSpeedCorrector(kmh, busPos);
        if (m_ScActive) {
            if (m_ScBrk > 0.01) { if (m_ScBrk > brake) brake = m_ScBrk; throttle = 0; }
            else if (m_ScThr > 0.01) { if (m_ScThr > throttle) throttle = m_ScThr; }
        }

        // CONTROLADOR DE FRENO DE PARADA PREDICTIVO (2026-07-22): si esta ON, maneja el FRENO para llegar
        // al fin del tramo (intercambio/endpoint) temprano y suave, DESACOPLADO del volante. Ultima palabra
        // sobre el freno de parada; deja el gas al InverseModel/breakaway para el creep. m_SurfPitchSm ya
        // viene corregido para reversa. Gate opt-in (StopBrakeControllerEnabled).
        if (m_Config && m_Config.StopBrakeControllerEnabled && m_Config.Waypoints && m_LegEnd >= 0 && m_LegEnd < m_Config.Waypoints.Count()) {
            vector wpEndSb = m_Config.Waypoints[m_LegEnd].GetVector();
            float dStopSb = vector.Distance(busPos, wpEndSb);
            // PENDIENTE REAL AL PUNTO (2026-07-22, Sonom4n: la inclinacion es input OBLIGATORIO del freno, siempre).
            // Grade GEOMETRICO terreno-vs-terreno de Boris al punto (SlopeToPoint): >0 = TREPA hacia el punto
            // -> la gravedad ya desacelera -> ComputeStopBrake resta g*sin y frena MENOS -> Boris conserva el
            // envion y planea al punto en vez de pararse metros antes. EXACTO y sin depender de la direccion.
            // Antes pasaba m_SurfPitchSm (pitch del vehiculo, suavizado + flip en reversa) que en la subida
            // quedaba corto -> no acreditaba la gravedad -> frenaba de mas y mataba el envion para trepar.
            float slopeSb = SlopeToPoint(busPos, wpEndSb);
            float sbBrake = ComputeStopBrake(kmh, dStopSb, slopeSb, 0.5);
            if (sbBrake >= 0.0) {
                brake = sbBrake;
                if (sbBrake > 0.05) throttle = 0;
            }
        }

        // SUPRESION DE EMPUJE EN EL CUSP DE REVERSA (2026-08-05, Sonom4n). MEDIDO (CTL+TRAMO): en el endpoint de la
        // reversa (v~0), BreakawayRamp/coastBand empujan throttle hacia el wp forward que ya sangro al aim ->
        // lanzan a Boris 9-22m PASADO el cusp -> la pierna cierra tarde/torcida -> clavado (Golf/E60). Lo que
        // deberia apagarlos (nearCheckpoint) esta muerto: IsNearCheckpoint exige StopBrakeControllerEnabled=false.
        // Regla leg-based, sin constante por-vehiculo: mientras la pierna ACTIVA sea reversa y Boris este cerca de
        // su endpoint con gear FORWARD ya metido, cortar el empuje y frenar suave para ASENTARLO en el cusp -> la
        // pierna cierra limpia (llego lento+alineado) -> recien ahi arranca forward. Reversa-creep (gear rev) intacto.
        // TECHO DE THROTTLE EN EL ENDPOINT FINAL (2026-08-05, Sonom4n). MEDIDO (CTL): en la zona de glide del ultimo
        // wp los pisos de throttle (revApproach+coastGuard[t22], DeadZoneInverse, BreakawayRamp) se apilaban a
        // ~0.29 sostenido y aceleraban a Boris a 13 km/h en la bajada -> sobrepaso 1.66m (Golf). Va DESPUES de
        // todos esos pisos: si Boris se MUEVE (>EndpointGlideMaxKmh) corta el gas (coast -> el FRENO UNIVERSAL de
        // ~6730 clava); si esta des-clavando (<MaxKmh) lo capea al EndpointKick (creep gentil). Solo endpoint FINAL.
        m_InFinalEpZone = false;
        // ANCLAJE AL ENDPOINT REAL (2026-08-07, Sonom4n): antes gateaba por m_NextStopIndex==ultimo, pero al acelerar en
        // la cresta del lomo m_NextStopIndex dejaba de apuntar al endpoint -> el bloque se APAGABA justo ahi -> el
        // throttle 1.00 pasaba sin cortar -> sobrepaso (Hatchback +1.3m). Ahora se ancla a la PIERNA final
        // (m_LegEnd == ultimo wp) y a la distancia REAL al ultimo wp (dToEndEp), que nunca se corrompe.
        int lastWpEp0 = -1;
        float dToEndEp = 99999.0;
        if (m_Config && m_Config.Waypoints && m_Config.Waypoints.Count() > 0) {
            lastWpEp0 = m_Config.Waypoints.Count() - 1;
            dToEndEp = vector.Distance(busPos, m_Config.Waypoints[lastWpEp0].GetVector());
        }
        if (m_Config && m_Config.EndpointThrottleCapEnabled && m_Config.EndpointGlide && lastWpEp0 >= 0 && m_LegEnd == lastWpEp0 && dToEndEp < m_Config.EndpointGlideRangeM) {
            m_InFinalEpZone = true;   // FinalizeControl NO aplica DeadZoneInverse aca -> no re-infla el creep a un lanzon
            int nearEp = NearestRecordedWp(busPos);
            float pitchEp = GetEffectivePitch(nearEp, 1);   // pitch LOCAL del path (rad, >0=subida) -> captura el LOMO del galpon; SlopeToPoint al endpoint lo promediaba a ~0 y no lo veia
            float slEp = Math.Sin(pitchEp);                 // componente de gravedad ~ sin(pitch); >0 subida (menos freno), <0 bajada/cresta (mas freno)
            // FRENO DE PARADA ADITIVO, SLOPE-AWARE (2026-08-07). Solo AGREGA freno cuando Boris va SOBRE-VELOCIDAD
            // para parar en lo que queda: bEp = (v^2/2d - 9.8*sin) / decel_por_vehiculo (auto del fingerprint). En
            // subida acredita la gravedad (menos freno). Trepando/creepeando LENTO -> a_need chico -> bEp~0 -> NO
            // frena ni corta gas -> el DeadZoneInverse DES-CLAVA normal (la zona muerta medida por-vehiculo; roto
            // antes por gatearlo -> quedaba corto en la cuesta del galpon -> AR). Corta el gas solo cuando frena de
            // verdad (no empujar y frenar a la vez). NO gatea DeadZoneInverse (el des-clave es del framework).
            if (dToEndEp > m_Config.EndpointGlideStopM) {
                float vMsEp = kmh / 3.6;
                float decEp = m_Config.EndpointStopDecelMS;
                if (decEp <= 0.5) {
                    // AUTOCONFIGURACION POR-VEHICULO (2026-08-07, Sonom4n: "el framework se autoconfigura segun el vehiculo").
                    // decel = freno FISICO del vehiculo (Tf+Tr)/R/masa del CONFIG, acotado por el agarre de la SUPERFICIE
                    // REAL bajo Boris (pasto del galpon ~5.6, no asfalto 9.81). Lee todo del juego -> generaliza a
                    // cualquier vehiculo+piso sin tocar hdrs ni constantes. Fallback al EndpointBrakeDecel del recording.
                    if (m_InverseModel) {
                        string surfEp = "";
                        GetGame().SurfaceGetType3D(busPos[0], busPos[1], busPos[2], surfEp);
                        decEp = m_InverseModel.GetMaxBrakeDecel(surfEp) * m_Config.EndpointStopDecelFactor;
                        if (kmh < 0.6) BZBusLog.Info("[EndpDecel] surf=" + surfEp + " maxBrake=" + m_InverseModel.GetMaxBrakeDecel(surfEp) + " decEp=" + decEp);
                    } else {
                        decEp = m_Config.EndpointBrakeDecel * m_Config.EndpointStopDecelFactor;
                    }
                    if (decEp <= 0.5) decEp = 4.0;
                }
                float aRawEp = (vMsEp * vMsEp) / (2.0 * dToEndEp);   // demanda de parada CRUDA (sin credito de pendiente)
                float bRawEp = aRawEp / decEp;
                bool stoppingZoneEp = (bRawEp > 0.05);
                float aNeedEp = aRawEp - 9.8 * slEp;   // el FRENO si acredita la gravedad en subida (menos freno)
                float bEp = aNeedEp / decEp;
                if (bEp > 0.0) {
                    if (bEp > 1.0) bEp = 1.0;
                    if (bEp > brake) brake = bEp;
                }
                // CORTE DE GAS por demanda CRUDA (2026-08-07): aunque la subida reduzca el freno (credito de gravedad),
                // si ya hay que empezar a parar cortamos el gas -> sino el throttle (InverseModel/floor FWD) empujaba
                // PASADO el endpoint EN SUBIDA (Hatchback/E60 +0.7-0.9m). Indep de la pendiente.
                if (stoppingZoneEp) throttle = 0;
                // TREPADA TRACCION-AWARE: el FWD des-clava el lomo con mas piso de throttle -- SOLO fuera de la zona de
                // parada (sino empuja pasado el endpoint) y solo clavado (kmh<Exit) trepando (pitch>0.02). AWD/RWD igual.
                // SOSTENER la trepada (no solo des-clavar): mientras hay SUBIDA (pitchEp>0.02) y va lento
                // (<EndpointGlideMaxKmh), el FWD mantiene el piso de throttle -> trepa PAREJO en vez de pasitos
                // (Sonom4n: "le cuesta subir, le toma tiempo"). Se apaga en la cresta (pitch<=0) y en la zona de parada
                // (!stoppingZoneEp) -> el freno lo clava sin sobrepaso. AWD/RWD no entran (GetDriveKind==0).
                if (!stoppingZoneEp && kmh < m_Config.EndpointGlideMaxKmh && throttle > 0.01 && pitchEp > 0.02 && m_InverseModel && m_InverseModel.GetDriveKind() == 0) {
                    float fwdFloor = m_Config.EndpointKickThrottle * m_Config.FwdClimbFactor;
                    if (fwdFloor > 0.95) fwdFloor = 0.95;
                    if (throttle < fwdFloor) throttle = fwdFloor;
                }
                if (kmh > 2.5) {
                    int szEp = 0; if (stoppingZoneEp) szEp = 1;
                    BZBusLog.Info("[EpCut] dEnd=" + dToEndEp + " legEnd=" + m_LegEnd + " last=" + lastWpEp0 + " stopZone=" + szEp + " thrPostCut=" + throttle + " kmh=" + kmh);
                }
            }
        }
        // DIAG INCONDICIONAL (2026-08-08): por que m_InFinalEpZone muere abajo de 6m? Loguea el gate crudo.
        if (dToEndEp < 8.0 && kmh > 1.0) {
            int inz = 0; if (m_InFinalEpZone) inz = 1;
            int ce = 0; if (m_Config && m_Config.EndpointThrottleCapEnabled) ce = 1;
            int ge = 0; if (m_Config && m_Config.EndpointGlide) ge = 1;
            BZBusLog.Info("[EpZone] inZone=" + inz + " capEn=" + ce + " glide=" + ge + " legEnd=" + m_LegEnd + " last=" + lastWpEp0 + " dEnd=" + dToEndEp + " kmh=" + kmh);
        }

        // El brake se aplica UNA vez para asentar; apenas Boris queda parado dentro del radio de captura, se
        // LATCHEA (m_CuspSettled) y se pide cerrar la pierna YA (m_ForceLegAdvance, lo consume UpdateLegBounds).
        // Sin el latch, mi propio brake se re-disparaba en cada pasito (kmh>1.5) -> reseteaba la rampa de
        // despegue en 0.45 -> nunca llegaba a 0.95 -> "forward de a pasitos" (MEDIDO: Golf 0.45+rebrake vs E60
        // 0.95 limpio). Latcheado, la rampa lanza forward a 0.95 sin interrupcion, igual que E60.
        bool legRevSup = ActiveLegIsReverse();
        if (!legRevSup) m_CuspSettled = false;   // fuera de reversa: listo para el proximo cusp
        if (m_Config && m_Config.CuspStopSuppressEnabled && legRevSup && m_LegEnd >= 0 && m_LegEnd < m_Config.Waypoints.Count() && bus.GetGear() != 0) {
            float dCuspSup = vector.Distance(busPos, m_Config.Waypoints[m_LegEnd].GetVector());
            if (dCuspSup < m_Config.CuspStopSuppressM) {
                float captCusp = m_Config.LegDoneTolM + m_Config.LegDoneCaptureExtraM;
                if (kmh < 0.6 && dCuspSup < captCusp) { m_CuspSettled = true; m_ForceLegAdvance = true; }
                if (!m_CuspSettled) {
                    throttle = 0;
                    if (kmh > m_Config.CuspStopSuppressKmh) brake = 0.4;
                }
            }
        }
        CtlSnap("cuspStopSup", throttle, brake, steering);

        // FINAL DE LA CADENA DE CONTROL (extraido 2026-07-25 por el limite de instrucciones de DriveTowards):
        // creep de reversa + inversa de zona muerta + tracer + corrector de volante + SetCachedInput + handbrake.
        if (m_Config && m_Config.DriveStateDump) DumpDriveState(iSpeed, distToNextStop, directReplayActive, isParking, isReversePk, target);
        FinalizeControl(busPos, kmh, throttle, brake, steering, target);
    }

    // Tramo FINAL de DriveTowards, extraido por el limite de instrucciones de Enforce. Aplica el creep de
    // reversa, la inversa de zona muerta, el tracer, el corrector de volante, guarda el input y setea el
    // handbrake del hill-hold. Recibe throttle/brake/steering por valor (los ajusta localmente y los guarda).
    private void FinalizeControl(vector busPos, float kmh, float throttle, float brake, float steering, BZWaypoint target) {
        ComputeReverseCreep(busPos, kmh);
        if (m_RcActive) { throttle = m_RcThr; brake = m_RcBrk; }
        if (m_Config && m_Config.DeadZoneInverseEnabled && !m_RcActive && kmh < m_Config.DeadZoneMaxKmh && throttle > 0.001 && brake < 0.05) {
            throttle = DeadZoneInverse(throttle, busPos);   // !m_RcActive: el creep de reversa ya incluye la zona muerta. Des-clava tambien en el endpoint final (el freno aditivo slope-aware corta el gas cuando frena, asi no hay lanzon)
        }
        if (m_InFinalEpZone && kmh > 2.5) {
            int rcEp = 0; if (m_RcActive) rcEp = 1;
            BZBusLog.Info("[EpFinal] thrFinal=" + throttle + " brk=" + brake + " rcActive=" + rcEp + " kmh=" + kmh);
        }
        CtlSnap("ramp+deadzone", throttle, brake, steering);
        CtlFlush(busPos, kmh);
        // CORRECTOR DE VOLANTE contra la toma: nudge intermitente hacia el volante GRABADO cuando Boris se desvia.
        steering = ComputeSteeringCorrector(steering, busPos, kmh);
        SetCachedInput(throttle, steering, brake);
        // HILL-HOLD CON HANDBRAKE: en el checkpoint el freno 1.0 no sostiene en pendiente -> handbrake lockea.
        if (m_CheckpointHold) {
            SetCachedHandbrake(1);
        }
        else if (m_EndpointLatched) {
            // HILL-HOLD FORWARD (2026-07-25, Sonom4n: "se deja caer hacia atras y ahi clava freno"). El endpoint
            // en forward aplica brake=1.0, pero el freno de SERVICIO no sostiene en pendiente -> Boris rueda
            // hacia atras (~0.16m medido en T3, cuesta arriba) ANTES de asentarse. Al TOCAR el endpoint
            // (m_EndpointLatched) en cuesta, clavar el HANDBRAKE de una -- igual que el hill-hold de la reversa
            // (m_CheckpointHold). En plano no hace falta (SetCachedHandbrake(0) intacto) -> solo si hay pendiente.
            float slopeEp = SlopeToPoint(busPos, target.GetVector());
            if (Math.AbsFloat(slopeEp) > 0.04) SetCachedHandbrake(1);
            else SetCachedHandbrake(0);
        }
        else if (target.mode != "parking" && target.mode != "maniobra") {
            SetCachedHandbrake(0);
        }
    }

    // SENSOR DE AUDITORIA (2026-07-26, temporal, gate DriveStateDump). Vuelca el ESTADO INTERNO de control por
    // frame a drive_state.csv -> se reconstruye EXTERNAMENTE (Python) que bloques de DriveTowards se ejecutan
    // (cobertura empirica exacta), sin instrumentar cada bloque (DriveTowards esta al filo). Segmentable por
    // m_LogTag (nombre de ruta). Se saca cuando termina la auditoria.
    private int Bi(bool x) {
        if (x) return 1;
        return 0;
    }
    private void DumpDriveState(float iSpeed, float distToNextStop, bool directReplayActive, bool isParking, bool isReversePk, BZWaypoint target) {
        FileHandle f = OpenFile("$profile:BZ_AutoDrive_PathLogger\\drive_state.csv", FileMode.APPEND);
        if (f == 0) return;
        string mode = "";
        int hid = 0;
        if (target) { mode = target.mode; if (target.hasInputData) hid = 1; }
        string ln = m_LogTag + "," + m_WaypointIndex + "," + m_LegStart + "," + m_LegEnd;
        ln = ln + "," + Bi(m_Reverse) + "," + Bi(isParking) + "," + Bi(isReversePk) + "," + Bi(m_AtStop);
        ln = ln + "," + Bi(m_EpGlideShort) + "," + Bi(m_RcActive) + "," + Bi(m_CheckpointHold);
        ln = ln + "," + Bi(m_CorridorValid) + "," + Bi(m_OffPath_InRecovery) + "," + Bi(directReplayActive);
        ln = ln + "," + distToNextStop + "," + iSpeed + "," + mode + "," + hid + "\n";
        FPrint(f, ln);
        CloseFile(f);
    }

    // Calcula un punto a 'distance' metros adelante sobre la ruta a partir del
    // waypoint actual. Pure pursuit clasico: el punto interpolado esta SOBRE el
    // path, no inside como seria con un centroide. En curvas el bus sigue el
    // trazado real, no lo corta. Si la ruta no alcanza la distancia pedida
    // (fin de ruta), devuelve el ultimo waypoint disponible.
    private vector ComputeLookahead(vector startPos, float distance) {
        if (!m_Config) return startPos;
        int count = m_Config.Waypoints.Count();
        int step = 1;
        if (m_Reverse) step = -1;

        int idx = m_WaypointIndex;
        vector prev = startPos;
        float acc = 0;

        // EL PUNTO DE MIRA NO SALE DEL TRAMO ACTIVO (2026-07-21). Aca se decide el VOLANTE: si el aim se
        // corre a la pierna siguiente -- que en una maniobra esta encima, medido a 1-8 cm -- Boris apunta
        // a otro tramo y se desvia justo antes de llegar al intercambio ("como si tuviera la vista puesta
        // en otro objetivo"). Al llegar al final del tramo devuelve ese ultimo punto: su objetivo ES el
        // intercambio, igual que un endpoint.
        while (idx >= 0 && idx < count) {
            if (m_LegEnd >= m_LegStart) {
                // NO clavar el aim en el ultimo wp: el pure-pursuit manda volante ~ 1/distancia_al_aim, y
                // si el punto deja de avanzar mientras Boris se le acerca, esa distancia tiende a cero y el
                // volante SE DISPARA (medido 21/07: oscilo +0.72/-0.65 y se abrio 1.32 m justo antes del
                // intercambio -- el "rechazo" al punto). Se ESTIRA en la direccion de salida del tramo, asi
                // conserva el rumbo correcto y la distancia nunca colapsa.
                if (idx > m_LegEnd || idx < m_LegStart) {
                    int eA = m_LegEnd - 1;
                    if (eA < m_LegStart) eA = m_LegStart;
                    vector pEnd = m_Config.Waypoints[m_LegEnd].GetVector();
                    vector pPre = m_Config.Waypoints[eA].GetVector();
                    float exDx = pEnd[0] - pPre[0];
                    float exDz = pEnd[2] - pPre[2];
                    float exL = Math.Sqrt(exDx * exDx + exDz * exDz);
                    if (exL < 0.05) return pEnd;
                    float falta = distance - acc;
                    if (falta < 0) falta = 0;
                    vector ext;
                    ext[0] = pEnd[0] + (exDx / exL) * falta;
                    ext[1] = pEnd[1];
                    ext[2] = pEnd[2] + (exDz / exL) * falta;
                    return ext;
                }
            }
            BZWaypoint wp = m_Config.Waypoints[idx];
            vector wpPos = wp.GetVector();
            float segLen = vector.Distance(prev, wpPos);

            if (acc + segLen >= distance) {
                // El punto buscado esta en este segmento. Interpolar linealmente.
                float remaining = distance - acc;
                float t = remaining / segLen;
                vector interp;
                interp[0] = prev[0] + (wpPos[0] - prev[0]) * t;
                interp[1] = prev[1] + (wpPos[1] - prev[1]) * t;
                interp[2] = prev[2] + (wpPos[2] - prev[2]) * t;
                return interp;
            }

            acc += segLen;
            prev = wpPos;
            idx += step;
        }

        return prev;
    }

    // Mide la curvatura local de los proximos LOOKAHEAD_CURVATURE_WINDOW
    // waypoints sumando los cambios absolutos de heading entre segmentos
    // consecutivos. Devuelve total en radianes.
    //   - Recta:                 ~0 rad
    //   - Curva moderada (45Ã‚Â°):  ~0.78 rad
    //   - Curva fuerte (90Ã‚Â°):    ~1.57 rad
    //   - Zigzag o rotonda:      acumula varios cambios -> 1.0-2.0+ rad
    private float ComputeLocalCurvature() {
        if (!m_Config) return 0;
        int count = m_Config.Waypoints.Count();
        int windowEnd = m_WaypointIndex + LOOKAHEAD_CURVATURE_WINDOW;
        if (windowEnd >= count) windowEnd = count - 1;
        if (windowEnd - m_WaypointIndex < 2) return 0;

        float totalDelta = 0;
        float prevHeading = 0;
        bool hasPrev = false;

        for (int i = m_WaypointIndex; i < windowEnd; i++) {
            vector pA = m_Config.Waypoints[i].GetVector();
            vector pB = m_Config.Waypoints[i + 1].GetVector();
            float dx = pB[0] - pA[0];
            float dz = pB[2] - pA[2];
            if (dx * dx + dz * dz < 0.01) continue; // segmento degenerado (bus parado)
            float h = Math.Atan2(dx, dz);

            if (hasPrev) {
                float dh = h - prevHeading;
                while (dh > Math.PI)  dh -= 2.0 * Math.PI;
                while (dh < -Math.PI) dh += 2.0 * Math.PI;
                if (dh < 0) dh = -dh;
                totalDelta += dh;
            }
            prevHeading = h;
            hasPrev = true;
        }
        return totalDelta;
    }

    // Calcula el lookahead apropiado para la curvatura local actual.
    // Interpola linealmente entre LOOKAHEAD_DIST (recta) y LOOKAHEAD_DIST_MIN
    // (curva fuerte) usando la curvatura medida.
    private float ComputeAdaptiveLookahead() {
        float curvature = ComputeLocalCurvature();
        if (curvature <= LOOKAHEAD_CURVATURE_LOW) return LOOKAHEAD_DIST;
        if (curvature >= LOOKAHEAD_CURVATURE_HIGH) return LOOKAHEAD_DIST_MIN;
        // Interpolacion lineal
        float range = LOOKAHEAD_CURVATURE_HIGH - LOOKAHEAD_CURVATURE_LOW;
        float factor = (curvature - LOOKAHEAD_CURVATURE_LOW) / range;
        return LOOKAHEAD_DIST - factor * (LOOKAHEAD_DIST - LOOKAHEAD_DIST_MIN);
    }

    // Feedforward predictivo de steering: mira el segmento ~1.5s adelante y
    // computa cuanto va a cambiar el heading de la RUTA respecto al segmento
    // actual. Devuelve un valor normalizado [-1, 1] que el caller suma con
    // peso modesto al output de Stanley.
    //
    // DiseÃƒÂ±o analogo al cruise predictivo: aportar ANTICIPACION sin reemplazar
    // el feedback de Stanley. En recta devuelve 0; en curva devuelve no-cero
    // antes de que el segmento debajo del bus cambie, lo que hace que el bus
    // empiece a girar pre-curva en vez de "cortarla" cuando entra al segmento
    // nuevo a velocidad alta (volantazos #1 y #5 del baseline 2026-05-28).
    // === TRAYECTORIA: perfil de HEADING suavizado (2026-07-04, formato fiel) ===
    // Pre-computa UNA vez por ruta el heading por-waypoint via CHORDA sobre +-ventana metros
    // (denoise robusto, sin problemas de wrap: es un solo atan2 del vector chorda). El FF
    // plant-inverso lo lee en vez de re-derivar curvatura de segmentos discretos ruidosos cada
    // frame. Recompute si cambia la ruta (count de wps distinto). Ver [[receiver_faithful_format]].
    private ref array<float> m_PathHeadingSmooth;
    private void EnsureSmoothHeading() {
        if (!m_Config || !m_Config.Waypoints) return;
        int cntSh = m_Config.Waypoints.Count();
        if (m_PathHeadingSmooth && m_PathHeadingSmooth.Count() == cntSh && cntSh > 0) return;
        if (!m_PathHeadingSmooth) m_PathHeadingSmooth = new array<float>();
        m_PathHeadingSmooth.Clear();
        if (cntSh < 2) return;
        float winSh = 3.0;   // ventana de suavizado por distancia (m)
        for (int i = 0; i < cntSh; i++) {
            int jb = i; float db = 0;
            while (jb > 0 && db < winSh) {
                db += vector.Distance(m_Config.Waypoints[jb].GetVector(), m_Config.Waypoints[jb - 1].GetVector());
                jb--;
            }
            int jf = i; float df = 0;
            while (jf < cntSh - 1 && df < winSh) {
                df += vector.Distance(m_Config.Waypoints[jf].GetVector(), m_Config.Waypoints[jf + 1].GetVector());
                jf++;
            }
            vector chordSh = m_Config.Waypoints[jf].GetVector() - m_Config.Waypoints[jb].GetVector();
            float lenSqSh = chordSh[0] * chordSh[0] + chordSh[2] * chordSh[2];
            float hSh = 0;
            if (lenSqSh > 0.0001) {
                hSh = Math.Atan2(chordSh[0], chordSh[2]);
            } else if (i < cntSh - 1) {
                vector segSh = m_Config.Waypoints[i + 1].GetVector() - m_Config.Waypoints[i].GetVector();
                hSh = Math.Atan2(segSh[0], segSh[2]);
            }
            m_PathHeadingSmooth.Insert(hSh);
        }
        BZBusLog.Info("PathHeadingSmooth precomputado: " + cntSh + " wps (ventana " + winSh + "m)");
    }

    // CURVATURA LOCAL en centerIdx (2026-07-06). El promedio dHead/arco sobre el lookahead DILUYE el pico
    // del apice (sub-comanda -> arco abierto); el MAXIMO de la ventana sobre-comanda (agresivo/temprano ->
    // se pasa). El punto medio correcto = la curvatura LOCAL en la posicion actual (ventana centrada +-W,
    // perfil de heading SUAVIZADO -> denoise de la escalera de z / jitter 10Hz) -> comanda el pico del apice
    // CUANDO llega al apice, ni diluido ni temprano. La anticipacion la da el lag-lead (tau). "Predictivo
    // pero corto" (Sonom4n): magnitud = curvatura local AHORA, timing = lag-lead. cuerda a->b (~= arco).
    private float ComputeFFLocalCurvature(int centerIdx) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        EnsureSmoothHeading();
        if (!m_PathHeadingSmooth) return 0;
        int cnt = m_PathHeadingSmooth.Count();
        int W = 4;
        int a = centerIdx - W;
        int b = centerIdx + W;
        if (a < 0) a = 0;
        if (b >= cnt) b = cnt - 1;
        if (b - a < 3) return 0;
        float dH = m_PathHeadingSmooth[b] - m_PathHeadingSmooth[a];
        while (dH > Math.PI)  dH = dH - 2.0 * Math.PI;
        while (dH < -Math.PI) dH = dH + 2.0 * Math.PI;
        float arc = vector.Distance(m_Config.Waypoints[a].GetVector(), m_Config.Waypoints[b].GetVector());
        if (arc < 0.3) return 0;
        return dH / arc;
    }

    float GetPurePursuitLookaheadM() {
        float ldBase = 5.0;
        if (m_Config && m_Config.PurePursuitLookaheadM > 0) ldBase = m_Config.PurePursuitLookaheadM;
        // ACORTAR LA MIRA CERCA DE UN PUNTO DE PARADA (2026-07-20, MEDIDO). El punto de intercambio 1 esta
        // al final de un desvio corto que sale de la linea principal. Con la mira en 8.5 m Boris "ve" mas
        // alla del desvio, CORTA LA CURVA y pasa de largo: llego a 8.78 m sin doblar nunca hacia el punto.
        // Es corner-cutting clasico de pure-pursuit: si la mira es mas larga que el rasgo, el rasgo se
        // saltea. Cerca de una parada la mira se acorta para que siga la geometria fina y entre al punto.
        if (m_Config && m_Config.StopLookaheadShrink && m_StopAheadDist > 0 && m_StopAheadDist < m_Config.StopLookaheadRangeM) {
            float ldNear = m_Config.StopLookaheadMinM;
            float fr = m_StopAheadDist / m_Config.StopLookaheadRangeM;
            float ldMix = ldNear + (ldBase - ldNear) * fr;
            if (ldMix < ldBase) return ldMix;
        }
        return ldBase;
    }
    private float m_StopAheadDist;   // distancia al proximo wp con velocidad objetivo 0 (-1 = no hay cerca)

    // Busca adelante el proximo punto de parada (targetSpeed 0) dentro del rango; -1 si no hay.
    private void UpdateStopAhead(vector pos) {
        m_StopAheadDist = -1.0;
        if (!m_Config || !m_Config.Waypoints || !m_Config.StopLookaheadShrink) return;
        int cntSa = m_Config.Waypoints.Count();
        int hiSa = m_WaypointIndex + 60;
        if (hiSa > cntSa - 1) hiSa = cntSa - 1;
        int qSa;
        for (qSa = m_WaypointIndex; qSa <= hiSa; qSa++) {
            if (m_Config.Waypoints[qSa].targetSpeed > 0.01) continue;
            float dSa = vector.Distance(pos, m_Config.Waypoints[qSa].GetVector());
            if (dSa < m_Config.StopLookaheadRangeM) m_StopAheadDist = dSa;
            return;
        }
    }
    float GetPurePursuitGain() {
        if (m_Config && m_Config.PurePursuitGain > 0) return m_Config.PurePursuitGain;
        return 1.0;
    }

    // Volante GRABADO (cmd) en la posicion de Boris + un lead posicional (compensa fase). Devuelve -999 si no
    // hay grabado. Usado por el BLEND complementario sobre el pure-pursuit (2026-07-25, Sonom4n). Busca el wp mas
    // cercano a Boris (no m_WaypointIndex que corre 15m adelante), camina leadM metros adelante (clamp al fin
    // del tramo por el cusp), y devuelve targetFrontWheel/gain = el cmd que reproduce tu rueda ejecutada.
    private float GetRecordedWheelCmd(vector busPos, float leadM) {
        if (!m_Config || !m_Config.Waypoints) return -999.0;
        int cntRw = m_Config.Waypoints.Count();
        if (cntRw < 2) return -999.0;
        int loRw = m_WaypointIndex - 40; if (loRw < 0) loRw = 0;
        int hiSrchRw = m_WaypointIndex + 2; if (hiSrchRw > cntRw - 1) hiSrchRw = cntRw - 1;
        int nearRw = m_WaypointIndex; float bestRw = 1000000000.0;
        for (int i = loRw; i <= hiSrchRw; i++) {
            vector wvRw = m_Config.Waypoints[i].GetVector();
            float dxRw = busPos[0] - wvRw[0]; float dzRw = busPos[2] - wvRw[2];
            float dsqRw = dxRw * dxRw + dzRw * dzRw;
            if (dsqRw < bestRw) { bestRw = dsqRw; nearRw = i; }
        }
        int wpRw = nearRw;
        if (leadM > 0) {
            float cumRw = 0; int jRw = nearRw;
            int hiRw = cntRw - 1;
            if (m_LegEnd > nearRw && m_LegEnd < hiRw) hiRw = m_LegEnd;
            while (jRw < hiRw && cumRw < leadM) {
                cumRw = cumRw + vector.Distance(m_Config.Waypoints[jRw].GetVector(), m_Config.Waypoints[jRw + 1].GetVector());
                jRw++;
            }
            wpRw = jRw;
        }
        return m_Config.Waypoints[wpRw].targetFrontWheel / GetPlantSteerGain();
    }

    // BLEND FF COMPLEMENTARIO sobre el pure-pursuit (2026-07-25, Sonom4n). Nudge del steering hacia el volante
    // GRABADO (leido adelante p/ fase) -> sostiene la curva donde el pp suelta. Peso bajo: el pp manda (no
    // diverge). Extraido a funcion por el limite de instrucciones de Enforce en DriveTowards.
    // CROSS-TRACK EXPLICITO para el modo aditivo (2026-07-25, Sonom4n + research). Offset FRESCO al punto mas
    // cercano de la traza (busqueda ancha, NO depende del corredor de-synced) + correccion Stanley atan2(K*e,v)
    // -> tira el paralelo a la linea. Extraido por el limite de instrucciones de DriveTowards.
    // PURE-PURSUIT COMO FEEDBACK DE ERROR PURO (2026-07-25, Sonom4n + Kapania&Gerdes). Devuelve SOLO la correccion
    // de desviacion, sin la curvatura del camino (esa la lleva el FF grabado). Proyecta a Boris en la linea,
    // corre el pursuit DOS veces: real (desde Boris) y nominal (desde la proyeccion, rumbo tangente). La resta
    // cancela la curvatura comun -> queda el puro error, con la estabilidad del pursuit (lookahead adaptativo).
    private float ComputePPErrorFeedback(vector busPos, float currentYaw, float kmh) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        int cntFb = m_Config.Waypoints.Count();
        if (cntFb < 2) return 0;
        int loFb = m_WaypointIndex - 40; if (loFb < 0) loFb = 0;
        int hiFb = m_WaypointIndex + 10; if (hiFb > cntFb - 2) hiFb = cntFb - 2;
        // acotar al tramo activo (mismo criterio que el pursuit): no cruzar de pierna en el intercambio
        if (m_LegEnd >= m_LegStart) {
            if (loFb < m_LegStart) loFb = m_LegStart;
            if (hiFb > m_LegEnd - 1 && m_LegEnd - 1 >= 0) hiFb = m_LegEnd - 1;
        }
        float bestDFb = 1000000000.0; vector projFb = busPos; float tanFb = currentYaw; bool foundFb = false;
        for (int i = loFb; i <= hiFb; i++) {
            vector Afb = m_Config.Waypoints[i].GetVector();
            vector Bfb = m_Config.Waypoints[i + 1].GetVector();
            float ABxFb = Bfb[0] - Afb[0]; float ABzFb = Bfb[2] - Afb[2];
            float segFb = ABxFb * ABxFb + ABzFb * ABzFb;
            if (segFb < 0.01) continue;
            float APxFb = busPos[0] - Afb[0]; float APzFb = busPos[2] - Afb[2];
            float tFb = (APxFb * ABxFb + APzFb * ABzFb) / segFb;
            if (tFb < 0.0) tFb = 0.0;
            if (tFb > 1.0) tFb = 1.0;
            float pxFb = Afb[0] + tFb * ABxFb; float pzFb = Afb[2] + tFb * ABzFb;
            float dxFb = busPos[0] - pxFb; float dzFb = busPos[2] - pzFb;
            float dsFb = dxFb * dxFb + dzFb * dzFb;
            if (dsFb < bestDFb) {
                bestDFb = dsFb;
                projFb = Vector(pxFb, busPos[1], pzFb);
                tanFb = Math.Atan2(ABxFb, ABzFb);   // rumbo tangente del tramo (radianes, convenio atan2(x,z))
                foundFb = true;
            }
        }
        if (!foundFb) return 0;
        float ppReal = ComputePurePursuitSteering(busPos, currentYaw, kmh);   // curvatura del camino + error
        float ppNom  = ComputePurePursuitSteering(projFb, tanFb, kmh);        // solo curvatura del camino
        return ppReal - ppNom;                                                // puro error de desviacion
    }

    private float ApplyAdditiveCrossTrack(float steering, vector busPos, float kmh, float currentYaw) {
        if (!m_Config || !m_Config.Waypoints || m_Config.AdditiveCrossTrackGain <= 0) return steering;
        int cntXt = m_Config.Waypoints.Count();
        if (cntXt < 2) return steering;
        int loXt = m_WaypointIndex - 40; if (loXt < 0) loXt = 0;
        int hiXt = m_WaypointIndex + 10; if (hiXt > cntXt - 2) hiXt = cntXt - 2;
        float bestDXt = 1000000000.0; float bestSignXt = 0; float bestHeadXt = 0; bool foundXt = false;
        for (int i = loXt; i <= hiXt; i++) {
            vector Axt = m_Config.Waypoints[i].GetVector();
            vector Bxt = m_Config.Waypoints[i + 1].GetVector();
            float ABxXt = Bxt[0] - Axt[0]; float ABzXt = Bxt[2] - Axt[2];
            float segL2Xt = ABxXt * ABxXt + ABzXt * ABzXt;
            if (segL2Xt < 0.01) continue;
            float APxXt = busPos[0] - Axt[0]; float APzXt = busPos[2] - Axt[2];
            float tXt = (APxXt * ABxXt + APzXt * ABzXt) / segL2Xt;
            if (tXt < 0.0) tXt = 0.0;
            if (tXt > 1.0) tXt = 1.0;
            float pxXt = Axt[0] + tXt * ABxXt; float pzXt = Axt[2] + tXt * ABzXt;
            float dxpXt = busPos[0] - pxXt; float dzpXt = busPos[2] - pzXt;
            float dsqXt = dxpXt * dxpXt + dzpXt * dzpXt;
            if (dsqXt < bestDXt) {
                bestDXt = dsqXt;
                float crossXt = ABzXt * APxXt - ABxXt * APzXt;   // + = bus a la derecha (convencion linea 7821)
                bestSignXt = 1.0;
                if (crossXt < 0) bestSignXt = -1.0;
                bestHeadXt = Math.Atan2(ABxXt, ABzXt);           // rumbo del segmento mas cercano (radianes)
                foundXt = true;
            }
        }
        if (!foundXt) return steering;
        float bestPerp = bestSignXt * Math.Sqrt(bestDXt);   // offset REAL firmado (dist al PUNTO, acotado)
        if (bestPerp > 5.0)  bestPerp = 5.0;                // safety: no dejar que un offset raro dispare el volante
        if (bestPerp < -5.0) bestPerp = -5.0;
        // AMORTIGUAMIENTO (deep-research): error efectivo = e + xLA*Drumbo. El Drumbo (rumbo de Boris vs el del
        // path) ANTICIPA: si Boris ya encara hacia la linea, resta del error -> corrige menos -> NO sobre-pasa.
        float dPsiXt = currentYaw - bestHeadXt;
        while (dPsiXt > Math.PI)  dPsiXt = dPsiXt - 2.0 * Math.PI;
        while (dPsiXt < -Math.PI) dPsiXt = dPsiXt + 2.0 * Math.PI;
        float xLAeffXt = m_Config.AdditiveCrossTrackLookahead + (kmh / 3.6) * m_Config.AdditiveCrossTrackLookaheadTau;
        float effErrXt = bestPerp + xLAeffXt * dPsiXt;
        float corrXt = Math.Atan2(m_Config.AdditiveCrossTrackGain * effErrXt, kmh / 3.6 + 2.0) / (Math.PI * 0.5);
        float outXt = steering - corrXt;   // error a la derecha (+) -> restar -> gira izquierda -> a la linea
        if (outXt > 1.0)  outXt = 1.0;
        if (outXt < -1.0) outXt = -1.0;
        return outXt;
    }

    private float ApplyPurePursuitBlend(float steering, vector busPos) {
        if (!m_Config || m_Config.PurePursuitFFBlend <= 0) return steering;
        float rwCmd = GetRecordedWheelCmd(busPos, m_Config.PurePursuitFFLeadM);
        if (rwCmd <= -900.0) return steering;
        float wBl = m_Config.PurePursuitFFBlend;
        // GATE POR CURVATURA (Sonom4n): solo blend en CURVA. En recta el grabado es ~0 y el blend mete un limit-cycle
        // (el zigzag/bulto que vaga). Rampa de 0 (umbral) a full (2x umbral) para no saltar en el borde.
        if (m_Config.PurePursuitFFMinWheelDeg > 0) {
            float recDeg = Math.AbsFloat(rwCmd * GetPlantSteerGain());
            float rampBl = (recDeg - m_Config.PurePursuitFFMinWheelDeg) / m_Config.PurePursuitFFMinWheelDeg;
            if (rampBl <= 0) return steering;
            if (rampBl < 1.0) wBl = wBl * rampBl;
        }
        return steering + wBl * (rwCmd - steering);
    }

    // LOOP DE DIRECCION RAPIDO (2026-07-10, 20Hz): recomputa SOLO el pure-pursuit (geometrico) y refresca
    // m_CachedSteering entre ticks de 500ms. El apply per-frame ya usa m_CachedSteering -> el volante corrige
    // cada ~50ms (~0.4m a 30km/h) en vez de cada 500ms (~4m). No toca velocidad/gear/learners (dt-sensibles).
    // El flag m_FastSteerActive lo prende el Tick SOLO en modo forward normal con pure-pursuit (no parking/
    // reverse/handbrake/stop) para no pisar el volante recto del straighten ni el 0 de las paradas.
    void FastSteerTick() {
        if (!m_Config) return;
        Car c = Car.Cast(m_Bus);
        if (!c) return;
        // MICROTICK DE CENTRADO PULSADO (2026-07-12): NO recomputa el pursuit (eso fue el fast-loop que fallo).
        // Solo agrega un toque chico hacia la linea sobre el steering base del tick lento. Rutea aca y sale.
        if (m_Config.CenterMicroTickEnabled) {
            FastCenterMicroTick(c);
            return;
        }
        // --- loop rapido legacy (pure-pursuit crudo, DESACTIVADO: zigzag full-lock) ---
        if (!m_FastSteerActive) return;
        if (!m_Config.UsePurePursuit) return;
        vector busPos = c.GetPosition();
        vector dir = c.GetDirection();
        // RADIANES (2026-07-21, BUG DE UNIDADES — LA CAUSA RAIZ). Aca se pasaba en GRADOS
        // (* Math.RAD2DEG) a una funcion que hace alphaPp = targetHeading[RAD] - currentYaw.
        // DriveTowards (linea ~4067) siempre la llamo bien, en radianes; este loop corre a 20 Hz y pisa
        // m_CachedSteering 10 veces por tick -> el valor que llegaba al auto era SIEMPRE el equivocado.
        // El alpha resultante es una funcion MODULAR del rumbo: casi correcto en algunos angulos,
        // catastrofico en otros. MEDIDO: rumbo 51,1 deg (la recta larga) -> error +3,3 deg, se ve perfecto;
        // rumbo 116,3 deg (la curva) -> error -67,2 deg -> pide rueda -48 deg, satura en el tope fisico y
        // queda clavado en -35 (el volante al tope del lado CONTRARIO al que necesita). Coincide exacto
        // con el log. Por eso "el cruise es impecable y las maniobras no": la recta cae en un rumbo donde
        // el wrap acierta de casualidad. No era un problema de baja velocidad.
        float currentYaw = Math.Atan2(dir[0], dir[2]);
        float kmh = c.GetSpeedometerAbsolute();
        float s = ComputePurePursuitSteering(busPos, currentYaw, kmh);
        s = ApplyPurePursuitBlend(s, busPos);   // BLEND FF complementario: el loop rapido no pisa el blend
        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        // AL FRENAR a velocidad: low-pass fuerte -> clava la direcciÃƒÂ³n, no persigue la sacudida del yaw por
        // el frenado (fuente del zigzag). El giro sostenido igual llega en pocos ticks; la sacudida se amortigua.
        if (m_Config.SteerBrakeDampEnabled && m_CachedBrake > m_Config.SteerBrakeDampMinBrake && kmh > m_Config.SteerBrakeDampMinKmh) {
            float denomSd = m_Config.SteerBrakeDampFullKmh - m_Config.SteerBrakeDampMinKmh;
            if (denomSd < 1.0) denomSd = 1.0;
            float spdFsd = (kmh - m_Config.SteerBrakeDampMinKmh) / denomSd;
            if (spdFsd > 1.0) spdFsd = 1.0;
            float effAsd = 1.0 - spdFsd * (1.0 - m_Config.SteerBrakeDampAlpha);   // 1 en MinKmh -> Alpha (lock) en FullKmh+
            s = m_CachedSteering + (s - m_CachedSteering) * effAsd;
        }
        m_CachedSteering = s;
    }

    // Corrector de centrado PULSADO (microticks) para RECTA (2026-07-12, pedido de Sonom4n). Un humano no sostiene
    // un angulo continuo: da TOQUES chicos hacia la linea y suelta (asi el tren no lava y no se abre). Cada 50ms:
    // en fase ON del duty-cycle agrega un toque ACOTADO hacia la linea (mini-pursuit de Ld corto = signo correcto
    // + ganancia fuerte) sobre el steering base del tick lento; en fase OFF suelta a la base (deja asentar). NO
    // recomputa el pursuit completo (eso fue el fast-loop que fallo). Gateado: solo forward-normal, sin recovery,
    // sin freno, >MinKmh, |offset|>deadband y RECTA (curvatura del horizonte baja; la curva la maneja el pursuit).
    void FastCenterMicroTick(Car c) {
        m_MicroPhase++;   // reloj del duty-cycle (avanza siempre, aunque este gateado)
        if (!m_FastSteerActive || m_OffPath_InRecovery) { m_CachedSteering = m_MicroBaseSteer; return; }
        if (m_CachedBrake > 0.05) { m_CachedSteering = m_MicroBaseSteer; return; }   // frenando: manda el damp
        if (!m_Config.Waypoints) { m_CachedSteering = m_MicroBaseSteer; return; }
        int count = m_Config.Waypoints.Count();
        if (count < 3) { m_CachedSteering = m_MicroBaseSteer; return; }
        float kmh = c.GetSpeedometerAbsolute();
        if (kmh < m_Config.CenterMicroTickMinKmh) { m_CachedSteering = m_MicroBaseSteer; return; }
        // duty-cycle: fase OFF -> soltar a la base
        int period = m_Config.CenterMicroTickOnTicks + m_Config.CenterMicroTickOffTicks;
        if (period < 1) period = 1;
        int phase = m_MicroPhase % period;
        if (phase >= m_Config.CenterMicroTickOnTicks) { m_CachedSteering = m_MicroBaseSteer; return; }
        // wp mas cercano a Boris (ventana estrecha anclada al indice de avance)
        vector busPos = c.GetPosition();
        int loM = m_WaypointIndex - 10;
        if (loM < 0) loM = 0;
        int hiM = m_WaypointIndex + 5;
        if (hiM > count - 1) hiM = count - 1;
        int nearM = loM;
        float bestM = 1000000000.0;
        for (int im = loM; im <= hiM; im++) {
            vector wpM = m_Config.Waypoints[im].GetVector();
            float dxM = busPos[0] - wpM[0];
            float dzM = busPos[2] - wpM[2];
            float dsM = dxM * dxM + dzM * dzM;
            if (dsM < bestM) { bestM = dsM; nearM = im; }
        }
        // OFFSET lateral = distancia perpendicular al segmento [nearM, nearM+1]
        int segB = nearM + 1;
        if (segB > count - 1) segB = count - 1;
        vector pA = m_Config.Waypoints[nearM].GetVector();
        vector pB = m_Config.Waypoints[segB].GetVector();
        float ex = pB[0] - pA[0];
        float ez = pB[2] - pA[2];
        float segL2 = ex * ex + ez * ez;
        float offM = Math.Sqrt(bestM);
        if (segL2 > 0.01) {
            float tM = ((busPos[0] - pA[0]) * ex + (busPos[2] - pA[2]) * ez) / segL2;
            if (tM < 0.0) tM = 0.0;
            if (tM > 1.0) tM = 1.0;
            float odx = busPos[0] - (pA[0] + ex * tM);
            float odz = busPos[2] - (pA[2] + ez * tM);
            offM = Math.Sqrt(odx * odx + odz * odz);
        }
        if (offM < m_Config.CenterMicroTickDeadbandM) { m_CachedSteering = m_MicroBaseSteer; return; }
        // CURVATURA del horizonte (~12m): si es curva, NO actua (la maneja el pursuit)
        float accM = 0;
        int liM = nearM;
        vector prevM = m_Config.Waypoints[nearM].GetVector();
        float prevHeadM = Math.Atan2(ex, ez);
        float totTurnM = 0;
        while (liM < count - 1 && accM < 12.0) {
            liM++;
            vector curM = m_Config.Waypoints[liM].GetVector();
            vector sdM = curM - prevM;
            float slM = sdM.Length();
            if (slM > 0.1) {
                float hhM = Math.Atan2(sdM[0], sdM[2]);
                float dhM = hhM - prevHeadM;
                while (dhM > Math.PI)  dhM = dhM - 2.0 * Math.PI;
                while (dhM < -Math.PI) dhM = dhM + 2.0 * Math.PI;
                totTurnM = totTurnM + Math.AbsFloat(dhM);
                prevHeadM = hhM;
            }
            accM = accM + slM;
            prevM = curM;
        }
        if (totTurnM > m_Config.CenterMicroTickStraightRad) { m_CachedSteering = m_MicroBaseSteer; return; }
        // MINI-PURSUIT de Ld CORTO -> toque de centrado con SIGNO correcto (misma convencion que el pursuit)
        float Ld = m_Config.CenterMicroTickNearLd;
        if (Ld < 1.0) Ld = 1.0;
        float accT = 0;
        int liT = nearM;
        vector prevT = m_Config.Waypoints[nearM].GetVector();
        while (liT < count - 1 && accT < Ld) {
            liT++;
            vector curT = m_Config.Waypoints[liT].GetVector();
            accT = accT + vector.Distance(prevT, curT);
            prevT = curT;
        }
        vector tgtT = m_Config.Waypoints[liT].GetVector();
        float dxT = tgtT[0] - busPos[0];
        float dzT = tgtT[2] - busPos[2];
        float distT = Math.Sqrt(dxT * dxT + dzT * dzT);
        if (distT < 0.3) { m_CachedSteering = m_MicroBaseSteer; return; }
        vector dirM = c.GetDirection();
        float yawM = Math.Atan2(dirM[0], dirM[2]);
        float tgtHeadM = Math.Atan2(dxT, dzT);
        float alphaM = tgtHeadM - yawM;
        while (alphaM > Math.PI)  alphaM = alphaM - 2.0 * Math.PI;
        while (alphaM < -Math.PI) alphaM = alphaM + 2.0 * Math.PI;
        float kappaM = 2.0 * Math.Sin(alphaM) / Ld;
        float deltaM = Math.Atan2(GetWheelbase() * kappaM, 1.0);
        float microCmd = (deltaM * Math.RAD2DEG) / GetPlantSteerGain();
        microCmd = microCmd * m_Config.CenterMicroTickGain;
        float capM = m_Config.CenterMicroTickMaxCmd;
        if (microCmd > capM)  microCmd = capM;
        if (microCmd < -capM) microCmd = -capM;
        float outM = m_MicroBaseSteer + microCmd;
        if (outM > 1.0)  outM = 1.0;
        if (outM < -1.0) outM = -1.0;
        m_CachedSteering = outM;
    }

    // === PURE PURSUIT (2026-07-10, de Arma 2: steerAhead corto + turnCoef) ===
    // Controlador CLOSED-LOOP: cada frame apunta el volante a un punto CORTO adelante SOBRE la linea del
    // path. Como siempre apunta a la linea, se AUTO-CORRIGE (si drifta, el proximo frame apunta de vuelta)
    // -> NO se desvia. Es el path-follower clasico de robotica y como Arma no se escapa. A diferencia del
    // arco FF/horneado (open-loop: entiende de mas o de menos y drifta sin corregir). Devuelve cmd [-1,1].
    // === PURE-PURSUIT DE REVERSE (2026-08-10, Sonom4n) — AISLADO, opt-in ReverseUsePurePursuit ===
    // Controlador GEOMETRICO para la reversa: apunta el volante a un lookahead sobre la traza, medido
    // desde el EJE TRASERO (el que lidera al reversar). Es MEMORYLESS (no acumula como el replay+Stanley
    // -> mata el LAZO que se veia en el arco de 90) e INDEPENDIENTE de velocidad/RPM (puro geometrico).
    // Ld largo = mas suave (gain ~1/Ld) -> compensa el rear-steer hipersensible. Devuelve [-1,1].
    // NO toca el controlador de forward: solo se llama desde el bloque reverse cuando el flag esta ON.
    private float ComputeReversePurePursuit(vector busPos, float currentYaw) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        int countR = m_Config.Waypoints.Count();
        if (countR < 2) return 0;
        // 1) punto de control = EJE TRASERO (mismo offset que el corredor/FF de reverse)
        float syR = Math.Sin(currentYaw);
        float cyR = Math.Cos(currentYaw);
        vector ctrlR = busPos - Vector(syR, 0, cyR) * GetReverseControlOffset();
        // 2) ventana ACOTADA AL TRAMO alrededor del indice de avance (no saltar de pierna en un loop)
        int loR = m_WaypointIndex - 5;
        if (loR < 0) loR = 0;
        int hiR = m_WaypointIndex + 25;
        if (hiR > countR - 1) hiR = countR - 1;
        if (m_LegEnd >= m_LegStart) {
            if (loR < m_LegStart) loR = m_LegStart;
            if (hiR > m_LegEnd)   hiR = m_LegEnd;
        }
        // 3) wp mas cercano al eje trasero dentro de la ventana
        int nearR = m_WaypointIndex;
        if (nearR < loR) nearR = loR;
        if (nearR > hiR) nearR = hiR;
        float bestDR = 1000000000.0;
        for (int iR = loR; iR <= hiR; iR++) {
            vector wpvR = m_Config.Waypoints[iR].GetVector();
            float dxR = ctrlR[0] - wpvR[0];
            float dzR = ctrlR[2] - wpvR[2];
            float dR = dxR * dxR + dzR * dzR;
            if (dR < bestDR) {
                bestDR = dR;
                nearR = iR;
            }
        }
        // 4) lookahead: caminar hacia adelante acumulando distancia de PATH hasta Ld (interpolado)
        float LdTarget = GetReversePPLookahead();
        // Ld ADAPTATIVO POR CURVATURA (opt-in ReversePPCurvAdaptive): sensa el cambio de rumbo del path
        // sobre ~senseM m adelante del near y ACORTA el Ld en curva cerrada (clava el codo/apex) dejandolo
        // LARGO en lo liso (arranque suave, sin exagerar el primer volantazo). Resuelve el tradeoff del Ld fijo.
        if (m_Config.ReversePPCurvAdaptive) {
            float senseM = 6.0;
            float hdStartS = 0;
            float hdEndS = 0;
            bool haveHdS = false;
            float accSenseS = 0;
            vector pSenseS = m_Config.Waypoints[nearR].GetVector();
            for (int kS = nearR + 1; kS <= hiR; kS++) {
                vector cSenseS = m_Config.Waypoints[kS].GetVector();
                float sxS = cSenseS[0] - pSenseS[0];
                float szS = cSenseS[2] - pSenseS[2];
                float segLenS = Math.Sqrt(sxS * sxS + szS * szS);
                if (segLenS > 0.05) {
                    float hSegS = Math.Atan2(sxS, szS);
                    if (!haveHdS) { hdStartS = hSegS; haveHdS = true; }
                    hdEndS = hSegS;
                }
                accSenseS = accSenseS + segLenS;
                pSenseS = cSenseS;
                if (accSenseS >= senseM) break;
            }
            if (haveHdS) {
                float dHdS = hdEndS - hdStartS;
                while (dHdS > Math.PI)  dHdS = dHdS - 2.0 * Math.PI;
                while (dHdS < -Math.PI) dHdS = dHdS + 2.0 * Math.PI;
                if (dHdS < 0) dHdS = -dHdS;   // rad total de giro sobre senseM = proxy de curvatura local
                float curvLoS = 0.20;
                float curvHiS = 0.80;
                float ldCurveS = GetReversePPLdCurve();
                if (dHdS >= curvHiS) {
                    LdTarget = ldCurveS;               // curva cerrada -> Ld corto (clava el codo)
                } else if (dHdS > curvLoS) {
                    float tCurvS = (dHdS - curvLoS) / (curvHiS - curvLoS);
                    LdTarget = LdTarget + (ldCurveS - LdTarget) * tCurvS;   // interpola liso->cerrado
                }
            }
        }
        vector aimR = m_Config.Waypoints[hiR].GetVector();   // fallback = fin del tramo
        float accR = 0;
        vector prevR = m_Config.Waypoints[nearR].GetVector();
        for (int jR = nearR + 1; jR <= hiR; jR++) {
            vector curR = m_Config.Waypoints[jR].GetVector();
            float sgxR = curR[0] - prevR[0];
            float sgzR = curR[2] - prevR[2];
            float segR = Math.Sqrt(sgxR * sgxR + sgzR * sgzR);
            if (accR + segR >= LdTarget) {
                float tR = 0.5;
                if (segR > 0.001) tR = (LdTarget - accR) / segR;
                aimR = prevR + (curR - prevR) * tR;
                break;
            }
            accR = accR + segR;
            prevR = curR;
        }
        // 5) geometria pure-pursuit: rumbo de MARCHA en reverse = currentYaw + PI (el trasero lidera)
        float LxR = aimR[0] - ctrlR[0];
        float LzR = aimR[2] - ctrlR[2];
        float LdistR = Math.Sqrt(LxR * LxR + LzR * LzR);
        if (LdistR < 0.5) return 0;   // demasiado cerca -> no confiable
        float travelHeadR = currentYaw + Math.PI;
        float headLR = Math.Atan2(LxR, LzR);
        float alphaR = headLR - travelHeadR;
        while (alphaR > Math.PI)  alphaR = alphaR - 2.0 * Math.PI;
        while (alphaR < -Math.PI) alphaR = alphaR + 2.0 * Math.PI;
        // kappa = 2 sin(alpha)/Ld ; delta = atan(L*kappa) ; normalizar a [-1,1] + flip rear-steer (ReverseFFSign)
        float kappaR = 2.0 * Math.Sin(alphaR) / LdistR;
        float deltaR = Math.Atan2(GetWheelbase() * kappaR, 1.0);   // rad
        float steerR = (deltaR / GetReverseFFMaxSteer()) * GetReverseFFSign();
        if (steerR > 1.0)  steerR = 1.0;
        if (steerR < -1.0) steerR = -1.0;
        return steerR;
    }

    private float ComputePurePursuitSteering(vector busPos, float currentYaw, float kmh) {
        if (!m_Config || !m_Config.Waypoints) return 0;
        int count = m_Config.Waypoints.Count();
        if (count < 2) return 0;
        // 1) waypoint MAS CERCANO a la posicion REAL de Boris (ventana alrededor del indice de avance)
        int loPp = m_WaypointIndex - 30;
        if (loPp < 0) loPp = 0;
        int hiPp = m_WaypointIndex + 5;
        if (hiPp > count - 1) hiPp = count - 1;
        // SEGUIR LA TRAZA EN SECUENCIA (2026-07-11): ventana ESTRECHA forward-biased anclada al indice de
        // avance (monotono) -> en un cruce/loop NO salta a un wp cercano-pero-de-otra-parte. Trata la traza
        // como linea GRABADA en orden, ignora los nodos vecinos de otra pasada.
        if (m_Config.PurePursuitSequential) {
            loPp = m_WaypointIndex - 5;
            if (loPp < 0) loPp = 0;
            hiPp = m_WaypointIndex + 15;
            if (hiPp > count - 1) hiPp = count - 1;
        }
        // ACOTAR AL TRAMO (2026-07-21): la ventana cruzaba de pierna. Con la reversa retrocediendo sobre
        // la misma huella, los wps del tramo siguiente estan a CENTIMETROS -> "el mas cercano" podia ser
        // uno de la reversa estando Boris todavia en el forward. Mismo criterio que ya usaba la busqueda
        // de la velocidad (ver wpiSpd), que sufria exactamente esto.
        if (m_LegEnd >= m_LegStart) {
            if (loPp < m_LegStart) loPp = m_LegStart;
            if (hiPp > m_LegEnd)   hiPp = m_LegEnd;
        }
        int nearPp = m_WaypointIndex;
        if (nearPp < 0) nearPp = 0;
        if (nearPp > count - 1) nearPp = count - 1;
        // SENTIDO DE CIRCULACION (2026-07-21, Sonom4n: "como la corriente electrica"). La traza tiene
        // POLARIDAD: se descarta el candidato que circula al reves del wp donde Boris esta. El rumbo
        // por-waypoint ya estaba precalculado (m_PathHeadingSmooth, cuerda de 3 m) y no se usaba para
        // esto. Cubre el caso que ni el tramo ni la ventana pueden: volver por la misma huella dentro
        // de la ventana, en el mismo tramo y con la misma marcha (horquilla / circuito corto).
        float flujoRef = 0;
        bool usarFlujo = false;
        if (m_Config.PathFlowFilterDeg > 0) {
            EnsureSmoothHeading();
            if (m_PathHeadingSmooth && m_PathHeadingSmooth.Count() == count) {
                flujoRef = m_PathHeadingSmooth[nearPp];
                usarFlujo = true;
            }
        }
        float tolFlujo = m_Config.PathFlowFilterDeg * Math.DEG2RAD;
        float bestDpp = 1000000000.0;
        for (int ip = loPp; ip <= hiPp; ip++) {
            if (usarFlujo) {
                float dFl = m_PathHeadingSmooth[ip] - flujoRef;
                while (dFl > Math.PI)  dFl = dFl - 2.0 * Math.PI;
                while (dFl < -Math.PI) dFl = dFl + 2.0 * Math.PI;
                if (Math.AbsFloat(dFl) > tolFlujo) continue;   // circula al reves: no es candidato
            }
            vector wpP = m_Config.Waypoints[ip].GetVector();
            float dxp = busPos[0] - wpP[0];
            float dzp = busPos[2] - wpP[2];
            float dsp = dxp * dxp + dzp * dzp;
            if (dsp < bestDpp) {
                bestDpp = dsp;
                nearPp = ip;
            }
        }
        // 2) distancia de lookahead: piso + escalado por velocidad (Arma steerAhead ~0.4s)
        float vMsPp = kmh / 3.6;
        if (vMsPp < 1.0) vMsPp = 1.0;
        // Ld = max(config M, 0.4*v, 2.0). REVERTIDO al formulon estable (2026-07-10): acortar Ld (ppT=0.6 ->
        // ~5.3m) subio la ganancia (gain Ã¢Ë†Â 1/Ld) y con PurePursuitGain=1.4 cruzo el limite de estabilidad ->
        // bang-bang Ã‚Â±1 en TODA la ruta (volante 0.5 reversiones/seg, festones sobre la traza, corrida 130710-
        // 223413). El Ld largo (8.5) es el lado ESTABLE. La anticipacion NO se ataca acortando Ld a ciegas
        // (desestabiliza) -> hay que co-bajar PurePursuitGain o atacarla por otro lado. Config PurePursuitTimeS/
        // FloorM quedan en el header pero NO se usan aca hasta re-tunear gain junto con Ld.
        float Ld = GetPurePursuitLookaheadM();
        float LdV = 0.4 * vMsPp;
        if (LdV > Ld) Ld = LdV;
        if (Ld < 2.0) Ld = 2.0;
        // Ld ADAPTATIVO POR CURVATURA (2026-07-11): sensar el GIRO del path adelante y acortar Ld en curva
        // cerrada (clava el apex, no anticipa), largo en recta (liso). Reemplaza el Ld de arriba cuando ON.
        if (m_Config.UseCurvatureAdaptiveLd) {
            float senseM = m_Config.CurvatureLdSenseM;
            if (senseM <= 0) senseM = 12.0;
            float ldStr = m_Config.CurvatureLdStraightM;
            if (ldStr <= 0) ldStr = 8.5;
            float ldCrv = m_Config.CurvatureLdCurveM;
            if (ldCrv <= 0) ldCrv = 4.5;
            float turnSat = m_Config.CurvatureLdTurnRad;
            if (turnSat <= 0) turnSat = 0.6;
            // TAMBIEN ACOTADO AL TRAMO (2026-07-21). Este sensor mide cuanto GIRA la traza 12 m adelante
            // para decidir Ld. Cruzando de tramo, en el intercambio la traza se DA VUELTA 180 deg -> mide
            // un giro enorme -> satura -> Ld colapsa al minimo (4,5 m). Y como la ganancia del pursuit va
            // como 1/Ld, le daba la GANANCIA MAXIMA justo donde va mas lento y mas fragil esta. No solo
            // miraba al lugar equivocado: lo miraba con el volante al maximo de sensibilidad.
            int topeS = count - 1;
            if (m_LegEnd >= m_LegStart && m_LegEnd < topeS) topeS = m_LegEnd;
            int hA1 = nearPp + 1;
            if (hA1 > topeS) hA1 = topeS;
            vector d0v = m_Config.Waypoints[hA1].GetVector() - m_Config.Waypoints[nearPp].GetVector();
            float prevHead = Math.Atan2(d0v[0], d0v[2]);
            float accS = 0;
            int liS = nearPp;
            vector prevS = m_Config.Waypoints[nearPp].GetVector();
            float totalTurn = 0;
            while (liS < topeS && accS < senseM) {
                liS++;
                vector curS = m_Config.Waypoints[liS].GetVector();
                vector segd = curS - prevS;
                float segLen = segd.Length();
                if (segLen > 0.1) {
                    float hh = Math.Atan2(segd[0], segd[2]);
                    float dh = hh - prevHead;
                    while (dh > Math.PI)  dh = dh - 2.0 * Math.PI;
                    while (dh < -Math.PI) dh = dh + 2.0 * Math.PI;
                    totalTurn = totalTurn + Math.AbsFloat(dh);
                    prevHead = hh;
                }
                accS = accS + segLen;
                prevS = curS;
            }
            float fracLd = totalTurn / turnSat;
            if (fracLd > 1.0) fracLd = 1.0;
            if (fracLd < 0.0) fracLd = 0.0;
            Ld = ldStr - fracLd * (ldStr - ldCrv);
            // Ld CRECE CON LA VELOCIDAD en recta (2026-07-13): a alta velocidad el Ld clavado en ldStr (8.5m) deja
            // el lazo con margen corto (tau=Ld/v: 0.34s a 90km/h vs 0.77s a 40) -> el pursuit persigue el jitter de
            // la linea dibujada y la oscilacion CRECE (diverge ~65km/h). Un Ld ~proporcional a v mantiene tau ~cte
            // -> estable + PROMEDIA el jitter (mira mas lejos, no reacciona a cada temblor de 0.1m). Atenuado por
            // (1-frac): SOLO en recta (en curva manda el ldCrv corto que clava el apex). NO toca la linea. 0 = OFF.
            if (m_Config.CurvatureLdSpeedFactor > 0) {
                float ldSpeedFloor = m_Config.CurvatureLdSpeedFactor * vMsPp * (1.0 - fracLd);
                if (ldSpeedFloor > Ld) Ld = ldSpeedFloor;
            }
        }
        // 3) caminar adelante desde el nearest hasta cubrir Ld -> punto objetivo SOBRE la linea
        //
        // ACOTADO AL TRAMO ACTIVO (2026-07-21, LA CAUSA RAIZ). Caminar "adelante" por el INDICE de la
        // polilinea NO es caminar adelante en el ESPACIO: cuando el tramo siguiente es una reversa, los
        // waypoints que siguen RETROCEDEN sobre la misma huella (medido en ESQ: el wp97 esta a 2,27 m del
        // wp92, o sea a centimetros de donde viene Boris).
        // Simulado sobre la ruta real, con Boris SOBRE la linea y con el rumbo grabado:
        //     a 2,51 m del wp92 -> mira el wp97 a 0,28 m -> alpha  -15,4 deg
        //     a 1,70 m del wp92 -> mira el wp98          -> alpha -166,6 deg   <-- se da vuelta
        //     a 0,46 m del wp92 -> mira el wp100         -> alpha -176,4 deg
        // El volante recibe la orden OPUESTA, y a 0,28 m se dispara el guard distT<0.3 que devuelve 0 ->
        // cero, signo contrario, cero: los "saltitos" y el barrido de tope a tope a 0 km/h.
        // Acotando al tramo, la misma geometria da +32 -> +28 -> +23 -> +16 -> +11 -> +7: converge suave.
        // Es PURA GEOMETRIA -> les pasaba identico a los 3 vehiculos, y por eso apagar 14 capas de control
        // no cambio nada (Sonom4n: "ninguna de las cosas que desactivaste modifico la trayectoria").
        int topePp = count - 1;
        if (m_LegEnd >= m_LegStart && m_LegEnd < topePp) topePp = m_LegEnd;
        float accPp = 0;
        int liPp = nearPp;
        vector prevPp = m_Config.Waypoints[nearPp].GetVector();
        while (liPp < topePp && accPp < Ld) {
            liPp++;
            vector curPp = m_Config.Waypoints[liPp].GetVector();
            accPp += vector.Distance(prevPp, curPp);
            prevPp = curPp;
        }
        vector targetPp = m_Config.Waypoints[liPp].GetVector();
        // EXTENDER EL AIM MAS ALLA DEL FINAL DEL TRAMO — LA FLECHA (2026-07-21, idea de Sonom4n, MEDIDO).
        // Si el caminado se corto porque llego al FIN DEL TRAMO (liPp == topePp) pero todavia le falta Ld
        // por cubrir (accPp < Ld), el aim quedaba CLAVADO en el ultimo waypoint. Boris se le sigue
        // acercando a un punto FIJO -> pure-pursuit se degenera -> volantazo al tope. MEDIDO: el volante
        // saturaba a ~Ld (4,5 m) del final del tramo, simetrico a los dos lados del cruce (Sonom4n: "estan a
        // la misma distancia adentro del cruce"). Solucion: el wp final tiene ORIENTACION (targetHeading,
        // la pose del intercambio); extendemos el punto de mira los metros que faltan MAS ALLA del final,
        // a lo largo de esa flecha. Asi el pursuit siempre tiene un punto a distancia ~Ld adelante y no se
        // degenera; ademas apunta a SALIR alineado con la pose, que es lo que se quiere en el intercambio.
        if (liPp == topePp && topePp < count - 1 && accPp < Ld) {
            float faltaEx = Ld - accPp;
            float hdEx = m_Config.Waypoints[topePp].targetHeading;
            if (hdEx != 0) {
                float hdExRad = hdEx * Math.DEG2RAD;
                targetPp[0] = targetPp[0] + Math.Sin(hdExRad) * faltaEx;
                targetPp[2] = targetPp[2] + Math.Cos(hdExRad) * faltaEx;
            }
        }
        // 3b) COMPENSACIÃƒâ€œN DE CORTE (2026-07-12): el pursuit corta por DENTRO del arco. Desplazo el punto
        // objetivo hacia AFUERA (perpendicular al path, lado convexo) la sagita ~|ÃŽÂº|Ã‚Â·LdÃ‚Â²/8 Ãƒâ€” factor. El lado
        // "afuera" = donde bombea el arco = (target - punto medio de los vecinos), sin depender del signo.
        if (m_Config.PurePursuitCurveComp > 0 && kmh < m_Config.PurePursuitCurveCompMaxKmh && liPp >= 1 && liPp < count - 1) {
            float kComp = ComputeFFLocalCurvature(liPp);
            if (Math.AbsFloat(kComp) > 0.01) {
                vector pPrevC = m_Config.Waypoints[liPp - 1].GetVector();
                vector pNextC = m_Config.Waypoints[liPp + 1].GetVector();
                vector outwardC = targetPp - (pPrevC + pNextC) * 0.5;
                outwardC[1] = 0;
                float outLenC = outwardC.Length();
                if (outLenC > 0.001) {
                    outwardC = outwardC * (1.0 / outLenC);   // Enforce NO permite vector/float -> reciproco
                    float sagC = Math.AbsFloat(kComp) * Ld * Ld * 0.125 * m_Config.PurePursuitCurveComp;
                    targetPp[0] = targetPp[0] + outwardC[0] * sagC;
                    targetPp[2] = targetPp[2] + outwardC[2] * sagC;
                }
            }
        }
        // 4) angulo alpha = heading -> objetivo (mismo convenio atan2(x,z) que el FF)
        float dxT = targetPp[0] - busPos[0];
        float dzT = targetPp[2] - busPos[2];
        float distT = Math.Sqrt(dxT * dxT + dzT * dzT);
        if (distT < 0.3) return 0;
        float targetHeading = Math.Atan2(dxT, dzT);
        float alphaPp = targetHeading - currentYaw;
        while (alphaPp > Math.PI)  alphaPp = alphaPp - 2.0 * Math.PI;
        while (alphaPp < -Math.PI) alphaPp = alphaPp + 2.0 * Math.PI;
        // 5) curvatura pure-pursuit -> volante -> cmd (via plant gain, como el FF) x turnCoef
        // LA DISTANCIA REAL AL PUNTO, CON PISO (2026-07-21, MEDIDO — el volantazo del ultimo metro).
        // La forma canonica es kappa = 2*sin(alpha)/L con L = distancia REAL al punto de mira, NO el Ld
        // deseado. Cuando el aim se PEGA porque el tramo termina (el wp92 queda a 1,59 m y no hay mas
        // waypoints adelante), Ld seguia siendo 4,5 pero el punto estaba a 1,59: la formula mentia. Y sin
        // piso, ese 1/L infla la ganancia ~3x -> un alpha de 6,5 deg saturaba el volante al tope (medido:
        // pedia +35 con el objetivo a 6,5 deg) -> el "volantazo apenas termina la curva" que vio Sonom4n.
        // Piso = max(Ld, 3 m): a baja velocidad el punto pegado ya no dispara la curvatura al infinito;
        // el auto llega recto al ultimo metro en vez de tirar un volantazo a 0 km/h.
        float LpurePp = distT;
        if (LpurePp < Ld) LpurePp = Ld;
        if (LpurePp < 3.0) LpurePp = 3.0;
        float kappaPp = 2.0 * Math.Sin(alphaPp) / LpurePp;
        float deltaPp = Math.Atan2(GetWheelbase() * kappaPp, 1.0);   // rad
        float cmdPp = (deltaPp * Math.RAD2DEG) / GetPlantSteerGain();
        cmdPp = cmdPp * GetPurePursuitGain();
        if (cmdPp > 1.0)  cmdPp = 1.0;
        if (cmdPp < -1.0) cmdPp = -1.0;
        // SENSOR DEL AIM (2026-07-21, Sonom4n): cuando el volante SATURA a baja velocidad (donde Boris se traba
        // cruzado), loguea POR QUE. near=wp mas cercano, aim=wp objetivo, alpha=angulo al objetivo, dist=cuan
        // pegado esta el aim, Ld usado. Si el aim apunta ATRAS (alpha ~180) o el aim esta pegado (dist<Ld),
        // es el aim-pegging. Throttled 1/8 para no spamear (el pursuit corre a 20Hz).
        if (m_Config && m_Config.ControlTraceEnabled && (cmdPp > 0.85 || cmdPp < -0.85) && kmh < 12.0) {
            m_PpSatCount++;
            if (m_PpSatCount % 8 == 0) {
                string ppMsg = "[PP-AIM] SAT cmd=" + cmdPp + " near=" + nearPp + " aim=" + liPp;
                ppMsg = ppMsg + " leg=" + m_LegStart + ".." + m_LegEnd;
                ppMsg = ppMsg + " alpha=" + (int)(alphaPp*Math.RAD2DEG) + "deg distAim=" + distT + " Ld=" + LpurePp;
                BZBusLog.Info(ppMsg);
            }
        }
        return cmdPp;
    }

    private float ComputeFeedforwardSteering(vector busPos, float kmh, bool parkingMode = false, bool reverseMode = false) {
        if (!m_CorridorValid) return 0;
        if (!m_Config) return 0;

        // Distancia de escaneo: ~1.5s lookahead segun velocidad actual.
        // A 50 km/h (~14 m/s) escanea ~21m. A 20 km/h (~6 m/s) escanea ~9m.
        // Clamp [5m, 35m] para no degenerar cerca de paradas ni mirar demasiado lejos.
        // En parking mode: clamp [1.0m, 3.0m] Ã¢â‚¬â€ miramos MUY de cerca (1-3
        // waypoints adelante), porque las maniobras son discretas y necesitamos
        // anticipar segmento por segmento, no 5+ metros lejos.
        float vMs = kmh / 3.6;
        if (vMs < 2.0) vMs = 2.0;
        float scanDistFf = vMs * GetPlantFFLookaheadTime();
        float floorFf, ceilFf;
        // REVERSE: ventana de anticipacion PROPIA y PER-VEHICULO (2026-07-02, Sonom4n: "ejecutar
        // anticipado cuando sabe que no le da" + "no el mismo anticipo el Sedan que el Nissan").
        // Antes reverse heredaba el clamp de parking (1-3m) -> veia el arco tarde -> cortaba ancho
        // (latdev 0.4->2.3m, ai_run 100133). FISICA: rotar dÃË† necesita distancia Ã¢Ë†Â R_min (dÃË†/ds=1/R)
        // -> giro ANCHO (R_min grande) rota mas lento/metro -> debe empezar ANTES (mas anticipacion);
        // agil (R_min chico) -> despues. R_min = wheelbase/tan(maxSteer) = wb*cos/sin (Math.Tan no
        // garantizado en Enforce). lookahead = R_min * REV_ANTICIP_FACTOR (tuneable). Config-as-manual.
        if (reverseMode) {
            float msRev = GetReverseFFMaxSteer();
            float rMinRev = 4.0;
            if (msRev > 0.05) rMinRev = GetWheelbase() * Math.Cos(msRev) / Math.Sin(msRev);
            if (rMinRev < 1.5)  rMinRev = 1.5;
            if (rMinRev > 10.0) rMinRev = 10.0;
            float REV_ANTICIP_FACTOR = 1.3;
            floorFf = rMinRev * REV_ANTICIP_FACTOR;
            ceilFf  = floorFf * 2.0;
        }
        else if (parkingMode) { floorFf = 1.0; ceilFf = 3.0; }
        else                  { floorFf = GetPlantFFLookaheadFloor(); ceilFf = 35.0; }
        if (scanDistFf < floorFf) scanDistFf = floorFf;
        if (scanDistFf > ceilFf)  scanDistFf = ceilFf;
        // DESACOPLE VOLANTE/PREDICCION (2026-07-08, de Arma 2): si PlantFFSteerLookaheadM>0, el VOLANTE
        // usa un lookahead CORTO y FIJO (metros) en vez del escalado por velocidad -> mira cerca, sigue el
        // camino pegado, cae al fondo del nodo en vez de pre-girar a la salida. La predicciÃƒÂ³n larga (para la
        // VELOCIDAD) la hace el cruise aparte. Solo forward normal.
        if (!reverseMode && !parkingMode && m_Config.PlantFFSteerLookaheadM > 0) {
            scanDistFf = m_Config.PlantFFSteerLookaheadM;
        }

        // POSICION-SYNC (2026-07-09, MEDIDO): la FUENTE de la direccion parte del wp mas cercano a Boris,
        // no de m_WaypointIndex (que corre ~15m adelante por WAYPOINT_RADIUS=15) -> sin esto el volante
        // grabado/FF se ejecuta ~15m antes = anticipa (14m lead medido). NO altera el avance del indice.
        int wpiFf = m_WaypointIndex;
        if (m_Config.PlantSteerSourceNearest) {
            float bestDsqFf = 1000000000.0;
            int loSrcFf = m_WaypointIndex - 40;
            if (loSrcFf < 0) loSrcFf = 0;
            int hiSrcFf = m_WaypointIndex + 2;
            if (hiSrcFf > m_Config.Waypoints.Count() - 1) hiSrcFf = m_Config.Waypoints.Count() - 1;
            for (int siSrcFf = loSrcFf; siSrcFf <= hiSrcFf; siSrcFf++) {
                vector wpvSrcFf = m_Config.Waypoints[siSrcFf].GetVector();
                float dxSrcFf = busPos[0] - wpvSrcFf[0];
                float dzSrcFf = busPos[2] - wpvSrcFf[2];
                float dsqSrcFf = dxSrcFf * dxSrcFf + dzSrcFf * dzSrcFf;
                if (dsqSrcFf < bestDsqFf) {
                    bestDsqFf = dsqSrcFf;
                    wpiFf = siSrcFf;
                }
            }
        }

        // Recorrer waypoints adelante hasta cubrir scanDistFf (desde wpiFf = posicion real de Boris)
        float cumDistFf  = 0;
        vector lastPosFf = busPos;
        int futureIdxFf  = wpiFf;
        int lastIdxFf    = m_Config.Waypoints.Count() - 1;
        // EL FF DE DIRECCION NO MIRA MAS ALLA DEL FINAL DE SU TRAMO (2026-07-22, MEDIDO). En el
        // intercambio la traza se PLIEGA (la reversa avanza en un rumbo, el forward de vuelta en el
        // OPUESTO) -> el vector del segmento da un giro FANTASMA de ~180 deg al cruzar el leg-end.
        // Sin este clamp, al llegar a ~Ld del punto (medido SEDAN 22/07: d_end 9,8 m a 21 km/h) el
        // lookahead cruzaba a los wps del forward y el FF metia un volantazo (-0.59) sobre la RECTA;
        // como en reversa el volante corre solo a 2Hz se volvia ciclo limite y Boris se iba de la
        // traza. Igual que el ojo longitudinal (ComputeLookaheadSpeed ~9433). Cerca del leg-end el FF
        // devuelve 0 (guarda de abajo) y el volante queda solo con el control de rumbo, que es estable.
        if (m_LegEnd > wpiFf && m_LegEnd < lastIdxFf) lastIdxFf = m_LegEnd;
        while (futureIdxFf < lastIdxFf && cumDistFf < scanDistFf) {
            futureIdxFf++;
            vector wpPosFf = m_Config.Waypoints[futureIdxFf].GetVector();
            cumDistFf += vector.Distance(lastPosFf, wpPosFf);
            lastPosFf = wpPosFf;
        }
        if (futureIdxFf <= wpiFf || futureIdxFf >= lastIdxFf) return 0;

        // === LOOKAHEAD ADAPTATIVO POR CURVATURA (2026-07-07) ===
        // Si el horizonte cae en/cerca de un nodo AGUDO, el FF pre-gira a la SALIDA y corta el nodo por
        // dentro (wp207 V R=5.8m: anticipaba 22m). Re-escanear MAS CERCA cuando la curvatura del horizonte
        // es alta -> llega al apice antes de girar. Solo forward normal; opt-in (PlantFFCurvatureShrink>0).
        if (!reverseMode && !parkingMode && m_Config.PlantFFCurvatureShrink > 0) {
            float kAheadFf = Math.AbsFloat(ComputeFFLocalCurvature(futureIdxFf));
            float shrinkFf = 1.0 - m_Config.PlantFFCurvatureShrink * (kAheadFf / (kAheadFf + 0.08));
            if (shrinkFf < 0.35) shrinkFf = 0.35;
            if (shrinkFf < 0.999) {
                float scanShrunkFf = scanDistFf * shrinkFf;
                if (scanShrunkFf < floorFf) scanShrunkFf = floorFf;
                cumDistFf  = 0;
                lastPosFf  = busPos;
                futureIdxFf = wpiFf;
                while (futureIdxFf < lastIdxFf && cumDistFf < scanShrunkFf) {
                    futureIdxFf++;
                    vector wpShrFf = m_Config.Waypoints[futureIdxFf].GetVector();
                    cumDistFf += vector.Distance(lastPosFf, wpShrFf);
                    lastPosFf = wpShrFf;
                }
                if (futureIdxFf <= wpiFf || futureIdxFf >= lastIdxFf) return 0;
            }
        }

        // Heading del segmento futuro (de futureIdxFf a futureIdxFf+1)
        BZWaypoint fwFutFf = m_Config.Waypoints[futureIdxFf];
        BZWaypoint fwNxtFf = m_Config.Waypoints[futureIdxFf + 1];
        vector segVecFf = fwNxtFf.GetVector() - fwFutFf.GetVector();
        float segLenSqFf = segVecFf[0]*segVecFf[0] + segVecFf[2]*segVecFf[2];
        if (segLenSqFf < 0.01) return 0; // segmento degenerado
        float futHeadingFf = Math.Atan2(segVecFf[0], segVecFf[2]);
        // FORMATO FIEL (2026-07-04): con PlantFFSmoothCurvature, el heading del horizonte sale del
        // perfil suavizado pre-computado (chorda +-ventana) en vez del segmento discreto de 2 wps ->
        // la curvatura que consume el FF plant-inverso deja de heredar el jitter del muestreo 10Hz.
        if (m_Config && m_Config.UsePlantFeedforward && m_Config.PlantFFSmoothCurvature) {
            EnsureSmoothHeading();
            if (m_PathHeadingSmooth && futureIdxFf < m_PathHeadingSmooth.Count()) {
                futHeadingFf = m_PathHeadingSmooth[futureIdxFf];
            }
        }

        // Delta vs heading del segmento ACTUAL donde esta el bus (no vs bus heading,
        // eso lo maneja Stanley). El delta puro de la curvatura de la ruta.
        float dHeadFf = futHeadingFf - m_CorridorSegmentHeading;
        while (dHeadFf > Math.PI)  dHeadFf -= 2.0 * Math.PI;
        while (dHeadFf < -Math.PI) dHeadFf += 2.0 * Math.PI;

        // === MODELO BICICLETA EN REVERSE (rear-steer archetype, 2026-06-12) ===
        // Forward usa el heuristico dHead/(PI/2) de abajo (NO tocar Ã¢â‚¬â€ calibrado para
        // bus/Nissan). Reverse usa la geometria EXACTA del modelo bicicleta:
        //   kappa = dHead / arco   (curvatura de la ruta, rad/m)
        //   delta = atan(L * kappa)   (steering steady-state para sostener la curva)
        // y FLIPEA el signo porque en reverse v<0 => omega=(v/L)tan(delta) invierte
        // (Rajamani/Snider/MathWorks Stanley reverse). Sin este flip, el FF empujaba
        // al lado equivocado EXACTAMENTE en la curva = la divergencia que veiamos
        // (Sonom4n 2026-06-12: "se desvian en el punto de la curva, falta un calculo").
        if (reverseMode) {
            if (cumDistFf < 0.5) return 0; // arco muy corto, curvatura no confiable
            float kappaRev = dHeadFf / cumDistFf;                 // rad/m
            // atan(x) = atan2(x,1); Atan2 esta garantizado en Enforce (Atan de 1 arg no se usa)
            float deltaRev = Math.Atan2(GetWheelbase() * kappaRev, 1.0); // rad ideal
            float ffRev = deltaRev / GetReverseFFMaxSteer();       // normaliza a ~[-1,1]
            ffRev = ffRev * GetReverseFFSign();                    // flip rear-steer
            if (ffRev > 1.0)  ffRev = 1.0;
            if (ffRev < -1.0) ffRev = -1.0;
            return ffRev;
        }

        // === PLANT FEEDFORWARD (forward, 2026-07-04) Ã¢â‚¬â€ inversa del plant MEDIDO por el receiver ===
        // Igual estructura que el reverse de arriba, pero con la GANANCIA medida (no maxSteer) y el
        // understeer k:  kappa = dHead/arco ;  delta = atan(L*kappa/k) ;  cmd = delta_deg / gain.
        // El heuristico dHead/(PI/2)*0.25 aproximaba esto para curvas tipicas; el plant lo hace EXACTO
        // en todo el rango de curvatura/velocidad (no re-tunear por vehiculo). Ver [[steering_plant]].
        if (m_Config && m_Config.UsePlantFeedforward && cumDistFf >= 0.5) {
            float cmdFf = 0;
            if (m_Config.PlantUseRecordedSteering && wpiFf >= 0 && wpiFf < m_Config.Waypoints.Count()) {
                // FASE 2b: comandar el INPUT grabado del humano (sin filtrar). El actuador de Boris lo filtra
                // igual que el del humano -> mismo volante ejecutado -> misma linea con los taps. Position-synced
                // (por wp mas cercano a Boris via wpiFf); el Stanley con feedback bajo trima el drift del replay.
                cmdFf = m_Config.Waypoints[wpiFf].targetSteering;
            } else if (m_Config.PlantUseRecordedWheel && wpiFf >= 0 && wpiFf < m_Config.Waypoints.Count()) {
                // FASE 2 (2026-07-04): comandar el angulo de rueda EJECUTADO por el humano (grabado via
                // WheelGetDirection) en vez de re-derivarlo de la geometria. Boris reproduce lo que las
                // ruedas del humano HICIERON (taps/scrub incluidos), no una version geometrica limpia.
                // ADELANTO DE FASE DEL FF (2026-07-25, Sonom4n + deep-research). Pre-compensa el retardo del actuador
                // (~50ms) + slew leyendo el volante grabado ADELANTE de la pose real. FORWARD-PREDICTOR: la
                // distancia de lead = LeadM (fija) + v*LeadTau (predicha = lo que Boris avanza en el retardo tau).
                // Asi el FF llega al angulo justo cuando Boris esta en el punto. Clamp al fin del tramo (cusp).
                int wpRw = wpiFf;
                float leadMrw = m_Config.PlantRecordedWheelLeadM + (kmh / 3.6) * m_Config.PlantRecordedWheelLeadTau;
                if (leadMrw > 0.01) {
                    float cumRw = 0;
                    int jRw = wpiFf;
                    int hiRw = m_Config.Waypoints.Count() - 1;
                    if (m_LegEnd > wpiFf && m_LegEnd < hiRw) hiRw = m_LegEnd;
                    while (jRw < hiRw && cumRw < leadMrw) {
                        cumRw = cumRw + vector.Distance(m_Config.Waypoints[jRw].GetVector(), m_Config.Waypoints[jRw + 1].GetVector());
                        jRw++;
                    }
                    wpRw = jRw;
                }
                cmdFf = m_Config.Waypoints[wpRw].targetFrontWheel / GetPlantSteerGain();
            } else {
                float kappaFwd = dHeadFf / cumDistFf;                                            // rad/m (promedio sobre el lookahead)
                // PICO LOCAL (2026-07-06): el promedio dHead/arco DILUYE el pico del apice (la ventana
                // incluye la bajada de curvatura DESPUES del pico) -> sub-comanda en el punto de maxima
                // carga -> el arco se abre ~1m y cruza carril. Con PlantFFPeakCurvature tomamos el MAXIMO
                // de curvatura local de la ventana (perfil suavizado) -> comanda el pico completo en el
                // apice y sigue anticipando la salida. max(pico,promedio): nunca comanda menos que el promedio.
                if (m_Config.PlantFFPeakCurvature) {
                    float kloc = ComputeFFLocalCurvature(wpiFf);
                    if (Math.AbsFloat(kloc) > 0.001) kappaFwd = kloc;   // curvatura LOCAL en la posicion (no el promedio forward)
                }
                float deltaFwd = Math.Atan2(GetWheelbase() * kappaFwd, GetPlantUndersteerK(kmh));   // atan(L*kappa/k) rad; k=envelope(v) si ON
                cmdFf = (deltaFwd * Math.RAD2DEG) / GetPlantSteerGain();                          // invertir gain -> cmd
            }
            if (cmdFf > 1.0)  cmdFf = 1.0;
            if (cmdFf < -1.0) cmdFf = -1.0;
            // LAG LEAD (2026-07-04): inversa de 1er orden del actuador (tau~0.12s) -> cmd += tau*d(cmd)/dt.
            // Adelanta el volante en TODAS las transiciones (flancos); en regimen estable no agrega nada.
            float lagLead = GetPlantLagLead();
            if (lagLead > 0) {
                float nowFf = GetGame().GetTickTime();
                float dtFf  = nowFf - m_LastFfTime;
                float ledFf = cmdFf;
                if (m_LastFfTime > 0 && dtFf > 0.005 && dtFf < 0.5) {
                    float rateFf = (cmdFf - m_LastFfRaw) / dtFf;
                    ledFf = cmdFf + lagLead * rateFf;
                    if (ledFf > 1.0)  ledFf = 1.0;
                    if (ledFf < -1.0) ledFf = -1.0;
                }
                m_LastFfRaw  = cmdFf;
                m_LastFfTime = nowFf;
                return ledFf;
            }
            return cmdFf;
        }

        // Normalizar a [-1, 1]: 90 grados de cambio de ruta = full steer (heuristico legacy)
        float ffOut = dHeadFf / (Math.PI * 0.5);
        if (ffOut > 1.0)  ffOut = 1.0;
        if (ffOut < -1.0) ffOut = -1.0;
        return ffOut;
    }

    // Corredor (rieles): busca el segmento del recording mas cercano al bus en
    // una ventana local [-BACK, +FWD] alrededor de m_WaypointIndex. Calcula la
    // distancia perpendicular firmada (positiva si bus a la derecha del
    // segmento) y el heading del segmento. Solo considera segmentos donde la
    // proyeccion del bus cae DENTRO del segmento (0 <= t <= 1), descartando
    // wps fuera del rango lateral. Actualiza m_Corridor* y devuelve true si
    // encontro segmento valido.
    private bool ComputeCorridorInfo(vector busPos) {
        m_CorridorValid = false;
        if (!m_Config) return false;

        int count = m_Config.Waypoints.Count();
        int from = m_WaypointIndex - CORRIDOR_SEARCH_BACK;
        if (from < 0) from = 0;
        int toIdx = m_WaypointIndex + CORRIDOR_SEARCH_FWD;
        if (toIdx >= count - 1) toIdx = count - 2;
        if (toIdx < from) return false;

        int bestIdxDist = 99999;   // FIX chord-cut: segmento por cercania al wp OBJETIVO, no min-perp
        float bestSignedPerp = 0;
        float bestHeading = 0;
        bool found = false;

        for (int i = from; i <= toIdx; i++) {
            vector A = m_Config.Waypoints[i].GetVector();
            vector B = m_Config.Waypoints[i + 1].GetVector();
            float ABx = B[0] - A[0];
            float ABz = B[2] - A[2];
            float segLen2 = ABx * ABx + ABz * ABz;
            if (segLen2 < 0.01) continue; // segmento degenerado

            float APx = busPos[0] - A[0];
            float APz = busPos[2] - A[2];

            // t parametriza posicion del bus a lo largo del segmento (0=A, 1=B)
            float t = (APx * ABx + APz * ABz) / segLen2;
            if (t < 0.0 || t > 1.0) continue; // proyeccion fuera del segmento

            // Cross product Y component en coords DayZ (left-handed con Y arriba,
            // X este, Z norte): cross.Y = AB.z * AP.x - AB.x * AP.z
            // Positivo si bus a la derecha de AB. La version original (ABx*APz - ABz*APx)
            // estaba invertida y mando al bus al agua con Stanley K=1.0 Ã¢â‚¬â€ el bus
            // se autocorregia al lado contrario.
            float cross = ABz * APx - ABx * APz;
            float segLen = Math.Sqrt(segLen2);
            float signedPerp = cross / segLen;

            // FIX chord-cutting (2026-06-21): elegir el segmento mas cercano al wp OBJETIVO actual,
            // NO el de min-perpendicular. En curva el bus corta la cuerda y queda perp~0 de un segmento
            // equivocado del arco -> offset ~0 -> Stanley no giraba -> se abria 9-11m. El segmento del
            // target da el cross-track VERDADERO. En recta no cambia (el target ES el segmento cercano).
            int idxDist = i - m_WaypointIndex;
            if (idxDist < 0) idxDist = -idxDist;
            if (idxDist < bestIdxDist) {
                bestIdxDist    = idxDist;
                bestSignedPerp = signedPerp;
                bestHeading    = Math.Atan2(ABx, ABz);
                found          = true;
            }
        }

        if (found) {
            m_CorridorLateralOffset  = bestSignedPerp;
            m_CorridorSegmentHeading = bestHeading;
            // ANTI-CHORD-CUT (2026-06-21): el offset perpendicular-al-segmento SUBESTIMA en curva
            // (el bus corta la cuerda -> perp~0 de las cuerdas; el offset REAL al path es la distancia
            // al wp mas cercano, ~9-11m, que el Stanley NO veia -> no giraba). Si el wp mas cercano
            // esta mucho mas lejos que el perp del segmento = chord-cutting -> usar el cross-track
            // FIRMADO al wp cercano (offset verdadero). En recta no dispara (nearDist ~ perp).
            float nearD2 = 99999.0;
            int nearWp = -1;
            for (int ni = from; ni <= toIdx; ni++) {
                vector nwp = m_Config.Waypoints[ni].GetVector();
                float ndx = busPos[0] - nwp[0];
                float ndz = busPos[2] - nwp[2];
                float nd2 = ndx * ndx + ndz * ndz;
                if (nd2 < nearD2) { nearD2 = nd2; nearWp = ni; }
            }
            float absPerp = bestSignedPerp;
            if (absPerp < 0) absPerp = -absPerp;
            float nearDist = Math.Sqrt(nearD2);
            if (nearWp >= 0 && nearWp < count - 1 && nearDist > absPerp + 2.0) {
                vector nA = m_Config.Waypoints[nearWp].GetVector();
                vector nB = m_Config.Waypoints[nearWp + 1].GetVector();
                float nABx = nB[0] - nA[0];
                float nABz = nB[2] - nA[2];
                float nSeg = Math.Sqrt(nABx * nABx + nABz * nABz);
                if (nSeg > 0.01) {
                    float nAPx = busPos[0] - nA[0];
                    float nAPz = busPos[2] - nA[2];
                    float nCross = nABz * nAPx - nABx * nAPz;
                    m_CorridorLateralOffset  = nCross / nSeg;
                    m_CorridorSegmentHeading = Math.Atan2(nABx, nABz);
                }
            }
            m_CorridorValid          = true;
            return true;
        }

        // SECOND-PASS FALLBACK: cuando la proyeccion estandar falla (todos los
        // segmentos del search window tienen t fuera de [0,1] Ã¢â‚¬â€ caso tipico en
        // zonas de movimiento muy lento del recording, segmentos < 10cm), usar
        // el wp mas cercano por distancia euclidiana como ancla. Computar
        // heading caminando ADELANTE hasta encontrar un segmento >10cm.
        // Esto da a Stanley una direccion + offset lateral validos siempre,
        // evitando que el bot vaya "recto en la direccion correcta pero off-line"
        // (validado AI log 2026-05-31 reverse: bot recto 15m NW erra puerta 1.5m).
        float bestDistFb = 99999.0;
        int bestIdxFb = -1;
        for (int ki = from; ki <= toIdx; ki++) {
            vector wpPosFb = m_Config.Waypoints[ki].GetVector();
            float dxF = busPos[0] - wpPosFb[0];
            float dzF = busPos[2] - wpPosFb[2];
            float distSqF = dxF*dxF + dzF*dzF;
            if (distSqF < bestDistFb) {
                bestDistFb = distSqF;
                bestIdxFb = ki;
            }
        }
        if (bestIdxFb >= 0) {
            vector currWpFb = m_Config.Waypoints[bestIdxFb].GetVector();
            // Walk forward hasta encontrar un wp lo suficientemente lejos (>10cm)
            float fwdDxFb = 0;
            float fwdDzFb = 0;
            float fwdLen2Fb = 0;
            for (int kj = bestIdxFb + 1; kj < m_Config.Waypoints.Count() && kj < bestIdxFb + 30; kj++) {
                vector candFb = m_Config.Waypoints[kj].GetVector();
                fwdDxFb = candFb[0] - currWpFb[0];
                fwdDzFb = candFb[2] - currWpFb[2];
                fwdLen2Fb = fwdDxFb*fwdDxFb + fwdDzFb*fwdDzFb;
                if (fwdLen2Fb > 0.01) break;
            }
            if (fwdLen2Fb > 0.01) {
                float segLenFb = Math.Sqrt(fwdLen2Fb);
                float APxFb = busPos[0] - currWpFb[0];
                float APzFb = busPos[2] - currWpFb[2];
                float crossFb = fwdDzFb * APxFb - fwdDxFb * APzFb;
                m_CorridorLateralOffset  = crossFb / segLenFb;
                m_CorridorSegmentHeading = Math.Atan2(fwdDxFb, fwdDzFb);
                m_CorridorValid          = true;
                return true;
            }
        }
        return false;
    }

    // Getters para LogAITick: permite escribir al CSV si el bus iba dentro o
    // fuera del corredor en ese sample.
    bool   IsCorridorValid()       { return m_CorridorValid; }
    float  GetCorridorOffset()     { return m_CorridorLateralOffset; }

    // Viste al CHOFER (Boris, seat 0). Config-driven por ruta: si m_Config.DriverClothing tiene
    // items, EQUIPA ESOS (el admin define el outfit completo); si esta vacia/null, usa el outfit
    // DEFAULT del framework. El default es la remera branded BZ_AutoDrive_TShirt (PLEGADA en el
    // propio @BZ_AutoDrive, CfgVehicles del config.cpp raiz -> SIEMPRE cargada, sin dependencia
    // externa; va al slot Body) + PolicePants + PoliceCap + CombatBoots_Black.
    //
    // SCOPE: el framework SOLO viste al chofer. La ropa/loadout de los bots de convoy/crew la
    // maneja Quest (Expansion Quests), NO aca.
    //
    // CreateAttachment es por string -> si un classname no existe o su mod no esta cargado,
    // falla en SILENCIO (no es dependencia dura). El admin debe poner classnames validos.
    private void DressDriver(eAIBase driver) {
        if (!driver) return;
        // Default del framework (se usa si la ruta NO define DriverClothing).
        array<string> outfit = new array<string>();
        outfit.Insert("BZ_AutoDrive_TShirt");
        outfit.Insert("PolicePants");
        outfit.Insert("PoliceCap");
        outfit.Insert("CombatBoots_Black");
        // Override por ruta: si DriverClothing tiene items, REEMPLAZA el default.
        if (m_Config && m_Config.DriverClothing && m_Config.DriverClothing.Count() > 0) {
            outfit = m_Config.DriverClothing;
        }
        foreach (string item : outfit) {
            driver.GetInventory().CreateAttachment(item);
        }
    }

    private void OrientBusToNext() {
        if (!m_Bus || m_Config.Waypoints.Count() < 2) return;
        vector a = m_Config.Waypoints[0].GetVector();

        // Caminar por los waypoints hasta encontrar uno suficientemente lejos
        // del inicial. Si el bus estaba quieto al empezar a grabar, los primeros
        // samples estan en la misma posicion y dan direccion=0 (norte) random.
        vector b = a;
        int i = 1;
        int count = m_Config.Waypoints.Count();
        while (i < count) {
            vector candidate = m_Config.Waypoints[i].GetVector();
            if (vector.Distance(a, candidate) > 2.0) {
                b = candidate;
                break;
            }
            i++;
        }

        if (vector.Distance(a, b) < 0.01) return; // safety: ruta degenerada

        vector dir = b - a;
        float yaw = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        // Si la ruta ARRANCA en reverse, el bus debe mirar al reves del sentido de marcha
        // (la cola lidera la maniobra). Sino spawnea con la trompa hacia donde va la cola
        // y el controlador de reverse lo manda para el lado opuesto.
        // BUG FIX (2026-07-25, Sonom4n: "Boris spawnea al reves" en T2): NO chequear Waypoints[0].mode,
        // sino el modo del wp de DESTINO de la orientacion (i), que es el primer wp con MOVIMIENTO real.
        // Si la toma arranca con el auto PARADO en gear forward (interc1, justo antes de meter reversa),
        // el conversor marca wp0-1 como "normal" (parados) y recien wp2+ como "reverse" -> la condicion
        // vieja daba falso y spawneaba con la trompa hacia donde va la cola (180 al reves). El wp `i`
        // (primer wp a >2m, ya en movimiento) SI refleja la maniobra: reverse.
        if (m_Config.Waypoints[i].mode == "reverse") yaw = yaw + 180.0;
        m_Bus.SetOrientation(Vector(yaw, 0, 0));
        BZBusLog.Info("OrientBusToNext: waypoint inicial -> waypoint " + i + " (dist=" + vector.Distance(a, b) + "m), mode[i]=" + m_Config.Waypoints[i].mode + " yaw=" + yaw);
    }

    // -------------------------------------------------------------------------
    // Carteles de parada - se crean una sola vez en Init() y persisten

    private void SpawnStopSigns() {
        if (m_Signs.Count() > 0) return;

        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (!wp.isStop) continue;

            BZBusStopSign sign = BZBusStopSign.Cast(GetGame().CreateObject("BZBusStopSign", wp.GetVector(), false, true));
            if (sign) {
                sign.SetStopName(wp.name);
                m_Signs.Insert(sign);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Tick - se ejecuta cada segundo

    // Dispatcher del scenario DSL: por cada marker event cuyo waypoint ya alcanzo
    // Boris (una sola vez, m_AppliedEvents lo trackea), ejecuta sus actions. El
    // recording es el esqueleto; los verbos se attachan por marker (brainstorm 06-08).
    private void ApplyMarkerEvents(Car car) {
        if (!m_Config || !m_Config.Events) return;
        if (!m_AppliedEvents) m_AppliedEvents = new array<int>();
        for (int i = 0; i < m_Config.Events.Count(); i++) {
            if (m_AppliedEvents.Find(i) >= 0) continue;
            BZMarkerEvent ev = m_Config.Events[i];
            if (!ev) continue;
            if (!IsTriggerFired(ev, car)) continue;   // todavia no se cumple el trigger
            for (int j = 0; j < ev.actions.Count(); j++) {
                BZAction act = ev.actions[j];
                if (!act) continue;
                if (act.delay > 0) {
                    // accion diferida (coreografia): se ejecuta a los act.delay segundos
                    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.ExecuteActionDeferred, (int)(act.delay * 1000), false, act, i);
                } else {
                    ExecuteAction(car, act, i);
                }
            }
            m_AppliedEvents.Insert(i);
        }
    }

    // Evalua el trigger de un evento. Sin trigger explicito -> wp_reached en ev.wp
    // (compat con markers del recording). Armado desde el inicio de la mision (v1:
    // sin secuenciamiento arm-after-X, eso es refinamiento futuro).
    private bool IsTriggerFired(BZMarkerEvent ev, Car car) {
        string t = "";
        if (ev.trigger) t = ev.trigger.type;
        // Sin trigger real (null O type vacio: JsonFileLoader puede crear un BZTrigger
        // default no-null con type="") -> wp_reached en el wp del marker.
        if (t == "" || t == "wp_reached") {
            int wpTarget = ev.wp;
            if (ev.trigger && ev.trigger.wp > 0) wpTarget = ev.trigger.wp;
            return m_WaypointIndex >= wpTarget;
        }
        if (t == "player_in_radius")     return IsPlayerWithin(car, ev.trigger.radius);
        // Zona FIJA anclada a un waypoint de la ruta (centro = pos del wp, NO el vehiculo).
        // Para emboscadas/zonas off-road (ej: la colina): dispara estes ahi aunque Boris
        // este lejos. Reusa ev.trigger.wp (indice del wp-centro) + ev.trigger.radius.
        if (t == "player_near_waypoint") {
            if (!m_Config || !m_Config.Waypoints) return false;
            int zwp = ev.trigger.wp;
            if (zwp < 0 || zwp >= m_Config.Waypoints.Count()) return false;
            return IsPlayerWithinPos(m_Config.Waypoints[zwp].GetVector(), ev.trigger.radius);
        }
        if (t == "player_enter_vehicle") return IsPlayerInVehicle(car);
        if (t == "vehicle_health_below") return car.GetHealth01("", "") < ev.trigger.threshold;
        if (t == "timer")                return (GetGame().GetTickTime() - m_MissionStartTime) >= ev.trigger.seconds;
        return false;   // tipo desconocido -> nunca dispara
    }

    // True si hay algun JUGADOR REAL dentro de 'radius' metros del vehiculo. Filtra
    // por GetIdentity() != null: los bots eAI (Boris, pasajeros) no tienen identidad
    // de red, asi que NO cuentan como "jugador".
    private bool IsPlayerWithin(Car car, float radius) {
        return IsPlayerWithinPos(car.GetPosition(), radius);
    }

    // True si hay algun JUGADOR REAL dentro de 'radius' metros de un punto FIJO. Filtra
    // bots por GetIdentity()!=null. Usado por player_in_radius (centro=vehiculo) y
    // player_near_waypoint (centro=waypoint fijo de la ruta).
    private bool IsPlayerWithinPos(vector center, float radius) {
        if (radius <= 0) return false;
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++) {
            Man p = players[i];
            if (!p) continue;
            if (!p.GetIdentity()) continue;
            if (vector.Distance(p.GetPosition(), center) <= radius) return true;
        }
        return false;
    }

    // True si hay algun JUGADOR REAL adentro del vehiculo (cualquier asiento).
    private bool IsPlayerInVehicle(Car car) {
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++) {
            Man p = players[i];
            if (!p) continue;
            if (!p.GetIdentity()) continue;
            if (p.GetParent() == car) return true;
        }
        return false;
    }

    // Evalua una condicion (flow control / when). Reusa los helpers de triggers +
    // combinadores AND/OR/NOT recursivos + var_equals contra variables de escenario.
    private bool EvalCondition(BZCondition c, Car car) {
        if (!c) return false;
        string t = c.test;
        if (t == "AND") {
            if (!c.checks) return true;
            for (int ai = 0; ai < c.checks.Count(); ai++) {
                if (!EvalCondition(c.checks[ai], car)) return false;
            }
            return true;
        }
        if (t == "OR") {
            if (!c.checks) return false;
            for (int oi = 0; oi < c.checks.Count(); oi++) {
                if (EvalCondition(c.checks[oi], car)) return true;
            }
            return false;
        }
        if (t == "NOT") {
            if (!c.checks || c.checks.Count() == 0) return false;
            return !EvalCondition(c.checks[0], car);
        }
        if (t == "player_in_radius")     return IsPlayerWithin(car, c.radius);
        if (t == "player_in_vehicle")    return IsPlayerInVehicle(car);
        if (t == "vehicle_health_below") return car.GetHealth01("", "") < c.threshold;
        if (t == "wp_reached")           return m_WaypointIndex >= c.wp;
        if (t == "var_equals")           return GetScenarioVar(c.var) == c.value;
        if (t == "engine_running")       return car.EngineIsOn();
        if (t == "route_stopped")        return m_RouteStopped;
        if (t == "vehicle_frozen")       return m_Frozen;
        return false;
    }

    private string GetScenarioVar(string name) {
        if (!m_ScenarioVars) return "";
        if (m_ScenarioVars.Contains(name)) return m_ScenarioVars.Get(name);
        return "";
    }

    private void SetScenarioVar(string name, string val) {
        if (!m_ScenarioVars) m_ScenarioVars = new map<string, string>();
        m_ScenarioVars.Set(name, val);
    }

    // Ejecuta un verbo del DSL. Switch por verb. Verbos no implementados todavia
    // logean warning (extensible: agregar un case = nuevo verbo, sin tocar parser
    // ni dispatcher). v0 nativo: add_cargo, log_event.
    private void ExecuteAction(Car car, BZAction action, int evIdx) {
        if (!action) return;
        string verb = action.verb;
        if (verb == "add_cargo") {
            int n = SpawnCargoItems(car, action.items);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] add_cargo: " + n + " item(s) en el baul.");
        } else if (verb == "log_event") {
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] log_event: " + action.msg);
        } else if (verb == "freeze_vehicle") {
            m_Frozen = true;
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] freeze_vehicle: vehiculo clavado.");
        } else if (verb == "unfreeze_vehicle") {
            m_Frozen = false;
            SetCachedHandbrake(0.0);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] unfreeze_vehicle: ruta resumida.");
        } else if (verb == "set_vehicle_mortality") {
            // value = "mortal" -> destructible | cualquier otra cosa -> invincible
            if (action.value == "mortal") {
                m_VehicleInvincible = false;
                car.SetAllowDamage(true);
                BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] set_vehicle_mortality: MORTAL (recibe daÃƒÂ±o).");
            } else {
                m_VehicleInvincible = true;
                car.SetAllowDamage(false);
                BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] set_vehicle_mortality: INVINCIBLE.");
            }
        } else if (verb == "start_engine") {
            if (!car.EngineIsOn()) car.EngineStart();
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] start_engine.");
        } else if (verb == "stop_engine") {
            if (car.EngineIsOn()) car.EngineStop();
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] stop_engine.");
        } else if (verb == "despawn_vehicle") {
            // Fin de mision. Diferido 50ms: borrar el bus AHORA (dentro del Tick que
            // lo esta usando) crashearia. CallLater corre StopBus tras cerrar el Tick.
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] despawn_vehicle: fin de mision (diferido).");
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.StopBus, 50, false);
        } else if (verb == "stop_route") {
            m_RouteStopped = true;
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] stop_route: avance frenado.");
        } else if (verb == "resume_route") {
            m_RouteStopped = false;
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] resume_route: avance reanudado.");
        } else if (verb == "set_var") {
            SetScenarioVar(action.var, action.value);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] set_var: " + action.var + " = " + action.value);
        } else if (verb == "play_sound") {
            // Audio 3D atado al vehiculo. value = nombre del SoundSet (CfgSoundSets ->
            // CfgSoundShaders -> .ogg), que el MODDER define en su addon. Patron tomado de
            // BrigadaZRadio (SEffectManager.PlaySoundOnObject). One-shot: no loop + autodestroy.
            // OJO REPLICACION: PlaySoundOnObject server-side puede NO llegar a los clientes
            // -> VERIFICAR ingame. Si no suena, mandar por RPC a los players cercanos para que
            // cada cliente lo reproduzca local (como hace el radio). Ver VERB_IMPLEMENTATION_NOTES.
            if (action.value != "") {
                EffectSound snd = SEffectManager.PlaySoundOnObject(action.value, m_Bus);
                if (snd) {
                    snd.SetSoundLoop(false);
                    snd.SetSoundAutodestroy(true);
                }
                BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] play_sound: " + action.value);
            }
        } else if (verb == "lights_on") {
            SetVehicleLights(true);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] lights_on.");
        } else if (verb == "lights_off") {
            SetVehicleLights(false);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] lights_off.");
        } else if (verb == "horn") {
            HornBeep();   // bocina nativa (SetCarHornState); action.value se ignora
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] horn: " + action.value);
        } else if (verb == "repair_vehicle") {
            car.SetHealth("", "", car.GetMaxHealth("", ""));
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] repair_vehicle.");
        } else if (verb == "refuel") {
            car.Fill(CarFluid.FUEL, 99999);   // Fill clampea a la capacidad del tanque
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] refuel.");
        } else if (verb == "drain_fuel") {
            car.Leak(CarFluid.FUEL, 99999);   // Boris se queda sin nafta -> para
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] drain_fuel.");
        } else if (verb == "ui_broadcast") {
            BroadcastGlobal(action.msg);      // mensaje global a los players (ya existe en el service)
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] ui_broadcast: " + action.msg);
        } else if (verb == "set_driver_mortality") {
            if (action.value == "mortal") {
                m_DriverInvincible = false;
                if (m_Driver) m_Driver.SetAllowDamage(true);
                BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] set_driver_mortality: MORTAL.");
            } else {
                m_DriverInvincible = true;
                if (m_Driver) m_Driver.SetAllowDamage(false);
                BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] set_driver_mortality: INVINCIBLE.");
            }
        } else if (verb == "spawn_guard") {
            // Spawnea bot(s) eAI armado(s) al costado del vehiculo. Faction Raiders =
            // hostil al jugador, amiga de Boris (Passive) -> sin fuego amigo.
            int gCount = action.count; if (gCount <= 0) gCount = 1;
            string gFaction = action.faction; if (gFaction == "") gFaction = "Raiders";
            string gLoadout = action.loadout; if (gLoadout == "") gLoadout = "BanditLoadout";
            string gClass = action.value; if (gClass == "") gClass = "eAI_SurvivorM_Boris";
            int gSpawned = SpawnGuards(car, gCount, gFaction, gLoadout, gClass);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] spawn_guard: " + gSpawned + " bot(s) faction=" + gFaction + " loadout=" + gLoadout);
        } else if (verb == "dismount_guard") {
            // Los bots del crew se bajan CON ANIMACION (GetOutVehicle = abre puerta + baja).
            // OJO: el bus debe estar DETENIDO -> usar este verbo con delay tras el freeze.
            int dCount = DismountCrew(car);
            BZBusLog.Info("[EVENT " + evIdx + " @ wp " + m_WaypointIndex + "] dismount_guard: " + dCount + " bot(s) bajando (animado).");
        } else {
            BZBusLog.Warn("[EVENT " + evIdx + "] verbo no implementado todavia: '" + verb + "'");
        }
    }

    // Verbo add_cargo: spawnea items sueltos en el cargo del vehiculo. Devuelve
    // cuantos creo. v1: top-level + cantidad (sin nesting / armas equipadas).
    private int SpawnCargoItems(Car car, array<ref BZCargoItem> items) {
        if (!items) return 0;
        int spawned = 0;
        for (int j = 0; j < items.Count(); j++) {
            BZCargoItem ci = items[j];
            if (!ci) continue;
            if (ci.cls == "") continue;
            EntityAI e = car.GetInventory().CreateInInventory(ci.cls);
            if (e) {
                ItemBase ib = ItemBase.Cast(e);
                if (ib && ci.qty > 1) ib.SetQuantity(ci.qty);
                spawned++;
            } else {
                BZBusLog.Warn("[CARGO] No se pudo crear '" + ci.cls + "' (baul lleno o classname invalido?)");
            }
        }
        return spawned;
    }

    // Verbo spawn_guard: spawnea N bots eAI armados al costado del vehiculo. La faction
    // (default Raiders) es HOSTIL al jugador pero AMIGA de Passive = Boris -> sin fuego
    // amigo (ver reference_expansion_ai_api). Arma via ExpansionHumanLoadout.Apply. Los
    // trackea en m_Guards para limpiarlos en CleanupEntities. Devuelve cuantos creo.
    private int SpawnGuards(Car car, int count, string faction, string loadout, string botClass) {
        if (!car) return 0;
        if (!m_Guards) m_Guards = new array<eAIBase>();
        vector basePos = car.GetPosition();
        vector dir = car.GetDirection();
        vector side = Vector(dir[2], 0, -dir[0]);   // perpendicular en XZ = costado del bus
        eAIGroup grp = eAIGroup.CreateGroup(eAIFaction.Create(faction));
        int made = 0;
        for (int gi = 0; gi < count; gi++) {
            vector gpos = basePos + side * (2.5 + gi * 1.5);
            gpos[1] = GetGame().SurfaceY(gpos[0], gpos[2]);
            eAIBase guard = eAIBase.Cast(GetGame().CreateObject(botClass, gpos, false, true));
            if (!guard) {
                BZBusLog.Warn("[spawn_guard] no se pudo crear '" + botClass + "'");
                continue;
            }
            guard.SetGroup(grp);
            ExpansionHumanLoadout.Apply(guard, loadout, false);
            m_Guards.Insert(guard);
            made++;
        }
        return made;
    }

    // Spawnea los bots de m_Config.Crew (NO los sienta aca Ã¢â‚¬â€ eso lo hace BoardDriver
    // cuando bus+motor estan listos para CrewGetIn). Faction Raiders por default = hostil
    // al jugador, amiga de Passive=Boris (sin fuego amigo). Arma via ExpansionHumanLoadout.
    private void SpawnCrewBots() {
        if (!m_Config || !m_Config.Crew) return;
        if (m_Config.Crew.Count() == 0) return;
        if (!m_Bus) return;
        if (!m_Crew) m_Crew = new array<eAIBase>();
        if (!m_CrewSeats) m_CrewSeats = new array<int>();

        // VIA A (2026-06-15): los bots son SEGUIDORES de Boris (mismo grupo m_Group, Boris = lider). El
        // embarque NATIVO de eAI sube a los seguidores como PASAJEROS (asiento>=1, salta el 0 del conductor)
        // cuando el LIDER ya esta en el transporte. Asi se evita el spawn standalone (=maniqui) y el board/
        // dismount salen ANIMADOS (sin teleport). Spawnean DESPLEGADOS al costado; se repliegan+suben en el
        // board (siguen a Boris). El framework debe esperar a que esten TODOS arriba (gate de muster) antes
        // de manejar (sino el vehiculo se va sin ellos). Ver [[reference_expansion_quest_api]] (seat>=1).
        if (!m_Group) { BZBusLog.Warn("[crew] m_Group null, no se spawnea crew"); return; }
        vector busPos = m_Bus.GetPosition();
        vector orient = m_Bus.GetOrientation();
        float yaw = orient[0] * Math.DEG2RAD;
        vector fwd = Vector(Math.Sin(yaw), 0, Math.Cos(yaw));
        vector rgt = Vector(fwd[2], 0, -fwd[0]);
        for (int ci = 0; ci < m_Config.Crew.Count(); ci++) {
            BZCrewMember cm = m_Config.Crew[ci];
            if (!cm) continue;
            string ccls = cm.cls; if (ccls == "") ccls = "eAI_SurvivorM_Boris";
            string cldt = cm.loadout; if (cldt == "") cldt = "BanditLoadout";
            int cseat = cm.seat; if (cseat <= 0) cseat = 1;
            float offR = cm.offsetRight; float offF = cm.offsetForward;
            if (offR == 0 && offF == 0) offR = 2.5;
            vector spos = busPos + fwd * offF + rgt * offR;
            spos[1] = GetGame().SurfaceY(spos[0], spos[2]);
            eAIBase cbot = eAIBase.Cast(GetGame().CreateObject(ccls, spos, false, true));
            if (!cbot) { BZBusLog.Warn("[crew] no se pudo crear '" + ccls + "'"); continue; }
            cbot.SetAllowDamage(false);
            cbot.SetGroup(m_Group);   // VIA A: seguidor de Boris -> sube nativo cuando Boris esta adentro
            ExpansionHumanLoadout.Apply(cbot, cldt, false);
            m_Crew.Insert(cbot);
            m_CrewSeats.Insert(cseat);
            BZBusLog.Info("[crew] '" + ccls + "' seat=" + cseat + " offset(F=" + offF + ",R=" + offR + ") -> grupo de Boris (Via A, seguidor)");
        }
        // Formacion IN: los seguidores siguen a Boris (y suben donde el sube).
        m_Group.SetFormationState(eAIGroupFormationState.IN);
        BZBusLog.Info("[crew] " + m_Crew.Count() + " bots en el grupo de Boris (Via A: suben de pasajeros via eAI nativo)");
    }

    // Verbo dismount_guard: los bots del crew se bajan del vehiculo CON ANIMACION
    // (GetOutVehicle abre la puerta). El bus debe estar detenido. Devuelve cuantos bajaron.
    private int DismountCrew(Car car) {
        if (!m_Crew) return 0;
        int dismounted = 0;
        for (int ci = 0; ci < m_Crew.Count(); ci++) {
            eAIBase cbot = m_Crew[ci];
            if (!cbot) continue;
            HumanCommandVehicle vehCmd = cbot.GetCommand_Vehicle();
            if (vehCmd && !vehCmd.IsGettingIn()) {
                // ABRIR la puerta del asiento ANTES de bajar (igual que el boarding). Sin esto, los
                // bots que comparten la puerta trasera del Cobra quedan trabados con la puerta cerrada
                // y salen recien por el fallback del engine (mucho despues). Ver [[project_ai_vehicle_crew]].
                int dseat = vehCmd.GetVehicleSeat();
                bool dhd = false; string dds = "";
                ExpansionFSMHelper.DoorAnimationSource(m_Bus, dseat, dhd, dds);
                if (dhd && dds != "") m_Bus.SetAnimationPhase(dds, 1.0);
                vehCmd.GetOutVehicle();
                ExpansionVehicle exVeh;
                if (ExpansionVehicle.Get(exVeh, vehCmd.GetTransport()))
                    exVeh.ReserveSeat(vehCmd.GetVehicleSeat(), null);
                cbot.LookAtDirection("0 0 1");
                cbot.SetAllowDamage(true);   // ya bajo -> mortal, el jugador puede matarlo
                // Que PATRULLE la zona en vez de re-subir al bus. Estilo AICamp: waypoint
                // en el punto donde bajo + ROAMING_LOCAL = vigila el area + engancha al
                // jugador (Raiders). Le da una orden -> su IA no corre la FSM de re-abordaje.
                eAIGroup cgrp = cbot.GetGroup();
                if (cgrp && car) {
                    // CLAVE (2026-06-17): limpiar los waypoints viejos ANTES de la patrulla. Sin esto, el
                    // bot conserva su waypoint de "ir a la puerta" del boarding -> al bajar camina de vuelta
                    // a la puerta y RE-ENTRA (el "se vuelven a subir" que se veia). ClearWaypoints lo corta.
                    cgrp.ClearWaypoints();
                    // Patrulla ~7m al costado del bus: loop de 4 puntos chico -> siempre
                    // caminando, nunca idle pegado al bus -> no re-aborda. Si detecta al
                    // jugador (Raiders), eAI rompe la patrulla y dispara (combate > patrulla).
                    vector bp = car.GetPosition();
                    vector cdir = car.GetDirection();
                    vector cside = Vector(cdir[2], 0, -cdir[0]);
                    vector pc = bp + cside * 7.0;
                    float py = bp[1];
                    cgrp.AddWaypoint(Vector(pc[0] + 2, py, pc[2] + 2));
                    cgrp.AddWaypoint(Vector(pc[0] - 2, py, pc[2] + 2));
                    cgrp.AddWaypoint(Vector(pc[0] - 2, py, pc[2] - 2));
                    cgrp.AddWaypoint(Vector(pc[0] + 2, py, pc[2] - 2));
                    cgrp.SetWaypointBehaviour(eAIWaypointBehavior.LOOP);
                }
                dismounted++;
            }
        }
        return dismounted;
    }

    // Ejecuta una accion diferida (action.delay > 0). Reusa m_Bus como car. Permite
    // coreografiar: ej freeze ahora, dismount a los N seg (cuando el bus ya paro).
    void ExecuteActionDeferred(BZAction action, int evIdx) {
        if (!action) return;
        Car car = Car.Cast(m_Bus);
        if (!car) return;
        ExecuteAction(car, action, evIdx);
    }

    // PERF: el Tick registrado en el CallQueue es este wrapper. Mide el costo del update de
    // manejo de ESTE runner (TickBody, que incluye DriveTowards + recovery + advance) y acumula
    // el delta en un static. Cada runner activo suma su delta en el frame; PerfTick (per-frame,
    // en el primary) promedia la suma sobre ~1s -> ms/frame del framework. GetTickTime()*1000 = ms.
    // El wrapper deja intactos los muchos early-return del body (cada uno cae aca y se cronometra).
    private void Tick() {
        float tStart = GetGame().GetTickTime();
        TickBody();
        float elapsedMs = (GetGame().GetTickTime() - tStart) * 1000.0;
        if (elapsedMs < 0) elapsedMs = 0;
        s_PerfFrameAccumMs += elapsedMs;   // sumado por TODOS los runners que ticquearon este frame
    }

    private void TickBody() {
        if (!m_Bus) {
            BZBusLog.Warn("Tick: m_Bus es null, llamando OnBusDestroyed");
            OnBusDestroyed();
            return;
        }
        // DEBUGGER: heartbeat por-runner cada ~5s (10 ticks x 500ms). Tagueado -> con N buses
        // se sigue cada uno en el RPT. Liviano (1 linea/5s/runner).
        m_TickCount++;
        // APRENDIZAJE DEL COASTEO (aspecto 2 del envelope). VA ACA, no dentro de DriveTowards: el control
        // tiene 21 puntos de salida distintos y engancharlo en uno solo hacia que casi ningun tick midiera
        // (medido 2026-07-20: no se creo ni un envelope). Aca corre SIEMPRE, y el input del tick anterior
        // sale del cache -> sabemos si venia rodando sin gas ni freno sin importar que rama lo decidio.
        // m_Bus es EntityAI: hay que bajar a Car para gear/velocimetro (sin el cast no compila el modulo World).
        // CALIBRACION DE ZONA MUERTA: corre UNA VEZ por vehiculo, antes de manejar la ruta.
        Car busCal = Car.Cast(m_Bus);
        if (busCal && !m_Paused && CalibrateBreakaway(busCal)) return;

        UpdateLegBounds();                                 // tramo activo: Boris solo conoce su pierna
        if (m_Bus) UpdateStopAhead(m_Bus.GetPosition());   // mira corta cerca de paradas
        Car busBk = Car.Cast(m_Bus);
        if (busBk && m_Config && m_Config.DeadZoneInverseEnabled) LearnBreakaway(busBk.GetSpeedometerAbsolute(), busBk.GetPosition());
        Car busCo = Car.Cast(m_Bus);
        if (busCo && m_Config && m_Config.CoastGuardEnabled) {
            bool idlePrev = (m_CachedThrottle < 0.05 && m_CachedBrake < 0.05 && busCo.GetGear() != 0);
            LearnCoast(busCo.GetSpeedometerAbsolute(), idlePrev, busCo.GetPosition());
        }
        if (m_TickCount % 10 == 0) {
            string hb = "wp " + m_WaypointIndex + "/" + RunnerWpTotal() + " | " + (int)RunnerSpeedKmh() + "km/h | " + m_PrevTickMode + " | AR=" + m_AR_Count;
            // LLEGA EL COMANDO AL AUTO? apply = veces que ApplyBusInput escribio en el vehiculo desde el
            // ultimo heartbeat (5 s). Si es ~0 mientras pedimos gas, OnInput no esta corriendo.
            Car busHb = Car.Cast(m_Bus);
            float rpmHb = 0;
            if (busHb) rpmHb = busHb.EngineGetRPM();
            hb = hb + " | apply=" + m_ApplyCount + " thr=" + m_CachedThrottle + " rpm=" + (int)rpmHb;
            m_ApplyCount = 0;
            // diagnostico del aprendizaje de coasteo: veh=clase leida, co=muestras aceptadas, id=ticks en coasteo
            hb = hb + " | veh=" + m_CoDbgClass + " co=" + m_CoSamples + " id=" + m_CoIdleTicks;
            if (m_Paused) hb = hb + " | PAUSED";
            LogI(hb);
        }
        // Parada a demanda (gesto OK): chequear cada ~1s (cada 2 ticks de 500ms)
        if (m_TickCount % 2 == 0 && s_Settings && s_Settings.HailGestureEnabled) CheckHailGesture();
        // Invencibilidad del vehiculo gateada por m_VehicleInvincible (flag de ruta
        // + verbo set_vehicle_mortality). Default true: auto-repara + bloquea daÃƒÂ±o
        // (sobrevive zone-damage de zombies). false: destructible -> recibe daÃƒÂ±o,
        // habilita on_crashed. El driver queda invencible aparte (otra regla).
        if (m_VehicleInvincible) {
            if (m_Bus.IsRuined()) {
                // Auto-reparar en vez de despawnear. SetAllowDamage(false) del spawn
                // bloquea direct damage pero zombies hacen zone damage que pasa esa
                // barrera y eventualmente IsRuined=true (visto en pruebas parking
                // 2026-05-31). En vez de perder la corrida, devolvemos health y
                // re-aplicamos SetAllowDamage.
                BZBusLog.Warn("Tick: m_Bus IsRuined=true, auto-reparando para continuar");
                m_Bus.SetHealth("", "", m_Bus.GetMaxHealth("", ""));
                m_Bus.SetAllowDamage(false);
            }
            // Re-aplicar SetAllowDamage en cada tick por si algun sistema lo resetea
            m_Bus.SetAllowDamage(false);
        } else {
            m_Bus.SetAllowDamage(true);   // destructible: deja pasar el daÃƒÂ±o
        }
        if (m_Driver) {
            if (m_DriverInvincible) m_Driver.SetAllowDamage(false);
            else m_Driver.SetAllowDamage(true);
        }

        Car bus = Car.Cast(m_Bus);
        if (!bus) return;

        // Boarding animado en progreso (chofer Y/O crew): no manejar hasta que TODOS esten sentados.
        bool driverBoarding = (m_BoardPhase >= 1 && m_BoardPhase < 3);
        bool crewBoarding = AnyCrewBoarding();
        if (driverBoarding || crewBoarding) {
            if (driverBoarding) StepBoarding();
            StepCrewBoarding();
            return;
        }

        // CONVOY: convoy listo (huida = board completo / ambush = bots a bordo) -> SUELTA el hold y MANEJA.
        // Libera m_PreRollEndTime (corta el SpawnHold) y fuerza la transicion SPAWN->PLAY.
        if ((m_QuestFleeing || m_AmbushActive) && !m_ConvoyDriving) {
            m_ConvoyDriving = true;
            m_PreRollEndTime = GetGame().GetTickTime();
            if (m_CurrentInput == BZBusInput.SPAWN) SetInput(BZBusInput.PLAY, "convoy: listo, arranca a manejar");
            BZBusLog.Info("[QUEST-CONVOY] convoy listo -> el vehiculo arranca a manejar la ruta");
        }

        // ESCENA 2 (ambush): mientras maneja con bots a bordo, vigilar si a algun bot le BAJA la salud
        // (= le dispararon, ej headshot al acompaÃƒÂ±ante de lejos). El daÃƒÂ±o al CHASIS lo avisa el CarScript
        // (EEHitBy). Cualquiera de los dos -> NotifyConvoyDamaged -> despliegue.
        if (m_AmbushActive && !m_AmbushTriggered && m_Crew && m_CrewLastHealth) {
            for (int hi = 0; hi < m_Crew.Count() && hi < m_CrewLastHealth.Count(); hi++) {
                if (!m_Crew[hi]) continue;
                float hNow = m_Crew[hi].GetHealth("", "");
                if (hNow < m_CrewLastHealth[hi] - 1.0) {
                    BZBusLog.Info("[QUEST-AMBUSH] bot seat " + m_CrewSeats[hi] + " recibio daÃƒÂ±o (salud " + m_CrewLastHealth[hi] + "->" + hNow + ")");
                    NotifyConvoyDamaged();
                    break;
                }
                m_CrewLastHealth[hi] = hNow;
            }
        }

        // Scenario events: disparar las acciones de cada marker al ALCANZAR su waypoint.
        ApplyMarkerEvents(bus);

        // === SYSTEM IDENTIFICATION MODE (prioridad sobre pause) ===
        // Si hay un experimento activo, ignoramos la ruta Y el pause.
        // SysID toma control absoluto.
        if (m_SysIDMode > 0) {
            SysIDTick(bus);
            return;
        }

        // === PRE-ROLL (3s post-spawn, antes de que la IA empiece a manejar) ===
        // Da tiempo a que el driver se siente, el motor encienda y el bus se
        // estabilice antes de aplicar inputs del recording. Sin esto, en
        // ticks tempranos el bus puede no responder a throttle (driver no
        // listo) y ValidateSpawn dispara retry innecesario, generando loop
        // con buses fantasma acumulados.
        // ----- INPUT: SPAWN -----
        // Durante SPAWN aplicamos brake, encendemos motor, esperamos a que se
        // cumpla SpawnHoldSeconds. Al expirar el timer, transicion a PLAY.
        if (m_CurrentInput == BZBusInput.SPAWN) {
            if (!bus.EngineIsOn()) bus.EngineStart();
            SetCachedInput(0, 0, 1.0);
            // LUCES AL ARRANCAR (2026-06-28): el bloque de luces de manejo (linea ~6069) solo
            // corre en PLAY, despues del spawn-hold (default 3s) -> de noche Boris arrancaba a
            // oscuras y prendia recien al empezar a rodar. Con LightsMode="auto" como default, el
            // usuario quiere las luces ON apenas enciende el motor. Evaluamos aca con wpTargetLights=0
            // (en SPAWN aun no hay wp activo; "replay" no aplica todavia, prende al rodar). SetVehicleLights
            // es idempotente (guard por m_LightsOn) -> seguro llamarlo cada tick de SPAWN.
            SetVehicleLights(ComputeDesiredLights(0));
            if (GetGame().GetTickTime() >= m_PreRollEndTime) {
                SetInput(BZBusInput.PLAY, "hold expirado, arranca playback");
            }
            return;
        }
        // Fallback legacy: si por algun motivo el input quedo en NONE pero
        // el bus existe (recarga, edge case), aplicamos pre-roll por tickTime.
        if (GetGame().GetTickTime() < m_PreRollEndTime) {
            if (!bus.EngineIsOn()) bus.EngineStart();
            SetCachedInput(0, 0, 1.0);
            return;
        }

        // === PAUSE MODE ===
        // Si esta pausado y NO hay experimento, mantener el bus quieto con
        // brake aplicado. Usado para teleportar el bus a la pista sin que
        // arranque solo.
        // FREEZE (verbo freeze_vehicle): clava el vehiculo donde esta (brake +
        // handbrake), sin inputs ni avance de ruta. Para secuencias tipo convoy
        // emboscado: frena, los bots bajan, queda freeze hasta unfreeze_vehicle.
        if (m_Frozen) {
            SetCachedInput(0, 0, 1.0);
            SetCachedHandbrake(1.0);
            return;
        }

        if (m_Paused || m_RouteStopped || m_HoldActive) {
            if (!bus.EngineIsOn()) bus.EngineStart();
            SetCachedInput(0, 0, 1.0);
            if (m_HoldActive) SetCachedHandbrake(1.0);   // TAXI HOLD: quieto firme mientras el eAI decide
            // END HOLD (2026-06-27): durante el hold de fin de ruta aplicamos handbrake
            // explicito (anti-rollback) para que el vehiculo quede QUIETO en la terminal,
            // espejo del spawn-hold. m_EndHoldActive lo limpia el despawn/respawn al expirar.
            if (m_EndHoldActive) SetCachedHandbrake(1.0);
            return;
        }

        // Si el bot se bajo del bus por cualquier motivo, volver a subirlo
        if (m_Driver && m_Driver.GetParent() != m_Bus) {
            Transport transport = Transport.Cast(m_Bus);
            if (transport) {
                m_Driver.SetPosition(m_Bus.GetPosition());
                m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
                m_Driver.Notify_Transport(transport, 0);
                BZBusLog.Info("Driver se bajo, lo meto de vuelta");
            }
        }

        // Asegurar motor encendido
        if (!bus.EngineIsOn()) {
            bus.EngineStart();
            BZBusLog.Info("Tick: EngineStart, gear=" + bus.GetGear() + " kmh=" + bus.GetSpeedometerAbsolute());
        }

        // === FRAME REPLAY (2026-07-05) ===
        // El COMANDO de inputs se hace per-frame (40Hz) en ApplyBusInput (este Tick corre a
        // 2Hz -> comandar aca aliasaria los taps). Aca SOLO cortamos el control normal
        // (avance de waypoints, recovery, stuck, respawn) para no interferir con el replay
        // open-loop: si Boris driftea, no queremos que el recovery lo teletransporte.
        if (m_Config && m_Config.FrameReplay && m_FrameReplay && m_FrameReplay.IsLoaded()) {
            return;
        }

        // === Activacion de m_AtStop por proximidad al proximo stop ===
        // Antes la activacion requeria pasar fisicamente por el waypoint stop
        // (m_StopDecided via OnWaypointReached). Pero el while loop no avanza
        // waypoints si el bus esta parado lejos del wp actual, y el modo parking
        // puede dejar el bus paralizado a 15-20m del stop. Resultado: loop muerto.
        //
        // Nueva logica: detectamos "estamos en la parada" por distancia al wp
        // que tiene isStop=true (m_NextStopIndex), independiente del wp actual.
        // Disparamos m_AtStop por dos caminos:
        //   a) reachedRadius:    llegamos a <STOP_FINAL_RADIUS del stop
        //   b) basicallyStopped: estamos parados dentro de la zona parking
        // Tambien sincronizamos m_WaypointIndex al stop para que al salir el
        // bus continue por el segmento correcto.
        if (!m_AtStop && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            BZWaypoint stopWp = m_Config.Waypoints[m_NextStopIndex];
            float distToStop = vector.Distance(m_Bus.GetPosition(), stopWp.GetVector());
            float stopCheckKmh = bus.GetSpeedometerAbsolute();
            // Radio del disparo de parada = STOP_FINAL_RADIUS (3m). El "ajuste fino" del bloque AtStop es el que
            // CREEPEA al punto exacto y CLAVA el endpoint -> disparar a 3m es lo correcto (radio chico paraba corto).
            bool reachedRadius    = (distToStop <= STOP_FINAL_RADIUS);
            // Safety net: si el control predictivo falla y el bus queda parado CERCA (<5m), igual activamos m_AtStop.
            bool basicallyStopped = (stopCheckKmh < 1.0 && distToStop <= 5.0);
            if (reachedRadius || basicallyStopped) {
                m_AtStop      = true;
                m_StopDecided = false;
                m_WaypointIndex = m_NextStopIndex;
                m_StopArrivedDeclared = false;   // el "llegue" (notif + countdown) arranca en el LATCH, no aca (2026-07-29):
                m_AtStopTicks = 0;               // asi el IMAN clava el punto exacto antes de que el despawn lo corte.
                BZBusLog.Info("AtStop activado en " + stopWp.name + ": dist=" + distToStop.ToString() + "m vel=" + stopCheckKmh.ToString() + " km/h wpIdx sync=" + m_WaypointIndex + " (iman aproximando; countdown al clavar)");
                // REVERSA (re-aplicado 2026-07-30): el countdown arranca ACA (como SIEMPRE) -> el iman es forward-only,
                // la reversa PARA CORTO (~2m) y NO llega al latch de 0.125m; si esperaramos el latch, la ruta COLGABA
                // (SEQ2 trabado). Solo el forward espera el latch (para darle tiempo al iman). La reversa: countdown ya.
                if (ActiveLegIsReverse()) {
                    m_StopArrivedDeclared = true;
                    BroadcastGlobal("BZ AutoDrive at stop " + stopWp.name + " — leaving in " + stopWp.stopDuration + "s");
                    BroadcastDistances(stopWp.name);
                    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(OnStopFinished, stopWp.stopDuration * 1000, false);
                }
            }
        }

        if (m_AtStop) {
            m_AtStopTicks++;
            // SAFETY (2026-07-29): si el iman no clava en ~60s (120 ticks @2Hz), forzar el latch para que la ruta
            // no cuelgue esperando (no deberia pasar; el iman clava a 0.125m). Asi el countdown siempre arranca.
            if (!m_EndpointLatched && m_AtStopTicks > 120) {
                m_EndpointLatched = true;
                BZBusLog.Info("[EndpointLatch] FORZADO por timeout (~60s sin clavar)");
            }
            // LATCH DEL ENDPOINT (2026-07-14): al TOCAR el punto, CLAVA (freno a fondo + handbrake) y NO
            // creepea mas. El creep no tiene direccion -> tras el coast se re-engancha del otro lado y corre
            // a Boris "de a pasitos" (medido: llega a 0.014m, termina a 3.37m). Sonom4n: "al tocar el endpoint se
            // tiene que clavar ahi, sacarle los controles completamente". INTELIGENTE para no romper los que
            // ya clavan sin overshoot: latch cuando (a) para cerca (<0.6m, <0.5km/h) o (b) OVERSHOOT (se aleja
            // >0.1m despues de haber estado <0.6m). Asi NUEVO03/FRAME02 (clavan 0.06/0.117 y quedan) latchean
            // en su punto; FRAME03 (overshootea) latchea apenas empieza a pasarse.
            if (!m_EndpointLatched && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                // DISTANCIA 2D (2026-07-17, MEDIDO): vector.Distance es 3D y el origen del vehiculo va ~0.52m
                // SOBRE el terreno. Con SampleTerrainY=true la Y del wp se re-samplea al TERRENO -> la 3D
                // NUNCA baja de ~0.52m aunque Boris este clavado exacto (medido: min=0.5567 y 0.555 en dos
                // corridas donde la distancia REAL en el piso fue 0.042 y 0.0). Eso se comia el umbral de
                // 0.6 (radio 2D efectivo: solo 0.24m). El endpoint es una posicion EN EL PISO: la altura no cuenta.
                vector bposEpL = m_Bus.GetPosition();
                vector wpvEpL  = m_Config.Waypoints[m_NextStopIndex].GetVector();
                float dxEpL = bposEpL[0] - wpvEpL[0];
                float dzEpL = bposEpL[2] - wpvEpL[2];
                float distEpL = Math.Sqrt(dxEpL * dxEpL + dzEpL * dzEpL);
                if (distEpL < m_EndpointMinDist) m_EndpointMinDist = distEpL;
                float kmhEpL = bus.GetSpeedometerAbsolute();
                // radio del latch: 0.6 por defecto. Con EndpointGlide la aproximacion final es LENTA a
                // proposito (2 km/h a 0.5m) -> con 0.6 el latch congelaria a Boris a mitad del glide en
                // cuanto la velocidad baje de 0.5 (ej: una subidita). Se cierra al radio del glide.
                float latchR = 0.6;
                if (m_Config.EndpointGlide) latchR = m_Config.EndpointGlideStopM * 2.5;
                bool stoppedClose = (distEpL < latchR && kmhEpL < 0.5);
                bool overshoot    = (m_EndpointMinDist < 0.6 && distEpL > m_EndpointMinDist + 0.1);
                if (stoppedClose || overshoot) {
                    m_EndpointLatched = true;
                    BZBusLog.Info("[EndpointLatch] CLAVADO a " + distEpL + "m (min=" + m_EndpointMinDist + " overshoot=" + overshoot + ") - controles cortados");
                }
            }
            if (m_EndpointLatched) {
                // ARRIBO REAL (2026-07-29): Boris CLAVADO en el punto -> RECIEN AHORA declaramos "llegue" y arrancamos
                // el countdown de la parada + la notificacion. Antes se disparaba a 3m (STOP_FINAL_RADIUS) y el despawn
                // cortaba al iman a mitad (Sonom4n: "la notificacion salta antes de que avance ese pedacito"), quedaba ~0.7m.
                if (!m_StopArrivedDeclared && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                    m_StopArrivedDeclared = true;
                    BZWaypoint arrWp = m_Config.Waypoints[m_NextStopIndex];
                    BroadcastGlobal("BZ AutoDrive at stop " + arrWp.name + " — leaving in " + arrWp.stopDuration + "s");
                    BroadcastDistances(arrWp.name);
                    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(OnStopFinished, arrWp.stopDuration * 1000, false);
                }
                SetCachedInput(0, 0, 1.0);
                bus.SetHandbrake(1.0);
                return;
            }
            // Ajuste fino al stop (2026-05-24): cuando el bus quedo PARADO en
            // m_AtStop pero todavia esta a 0.5-3m del wp exacto del recording,
            // empujamos suave (throttle=0.25) para que se acomode al punto
            // grabado. Esto es para casos donde el operador paro en posicion
            // especifica (acceso a parada, futuro garage, etc.) y queremos
            // fidelidad punto-a-punto.
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                vector stopExactPos = m_Config.Waypoints[m_NextStopIndex].GetVector();
                float distToExact = vector.Distance(m_Bus.GetPosition(), stopExactPos);
                float stopAdjustKmh = bus.GetSpeedometerAbsolute();
                // ENDPOINT GLIDE (2026-07-17, MEDIDO): el creep de abajo tiene 3 defectos que se suman:
                //   (a) distToExact es 3D y con SampleTerrainY la Y del wp es la del TERRENO mientras el origen
                //       del vehiculo va ~0.52m mas arriba -> NUNCA baja de 0.52 -> el guard "> 0.5" es SIEMPRE
                //       cierto -> empuja incluso ESTANDO clavado en el punto;
                //   (b) no tiene SIGNO -> sigue empujando DESPUES de pasarlo;
                //   (c) hace return -> BLOQUEA el freno universal de DriveTowards.
                // Resultado medido: bang-bang alrededor de 5 km/h (creep 0.22 -> acelera -> >5 -> brake 1.0 ->
                // <5 -> creep...) = los "saltitos", y se pasa de largo. Con el flag: si LLEGO -> clavar; si no,
                // NO retornar -> que mande el freno universal (fisica: a = v^2/2d - g*sin).
                if (EndpointGlideControl(bus, stopExactPos)) return;
                // Solo empujar si: estamos a 0.5-3m del exacto Y casi parado (creep 0.25 -> clava ~0.06m; probamos
                // 0.16/0.12 para acercar mas pero EMPEORO: gentil se queda corto a 0.186m. 0.25/0.5 es el bueno).
                if (distToExact > 0.5 && distToExact < 3.0 && stopAdjustKmh < 5.0) {
                    // Creep SLOPE-AWARE (2026-07-14): en pendiente ARRIBA el 0.25 fijo no alcanza para trepar
                    // al punto -> Boris rueda hacia atras y no clava. En BAJADA sobra (la gravedad lo lleva).
                    // Sumamos/restamos gas proporcional a la inclinacion REAL (anti-rollback simetrico). El
                    // endpoint de FRAME03 esta EN la cuesta del galpon. Gateado por SurfaceSense.
                    float creepEp = 0.25;
                    if (m_Config.SurfaceSenseEnabled) {
                        vector fwdEp = bus.GetDirection();
                        float horizEp = Math.Sqrt(fwdEp[0]*fwdEp[0] + fwdEp[2]*fwdEp[2]);
                        float slopeEp = 0;
                        if (horizEp > 0.05) slopeEp = (fwdEp[1] / horizEp) * m_Config.SurfaceSensePitchSign;
                        if (slopeEp > 1.5)  slopeEp = 1.5;
                        if (slopeEp < -1.5) slopeEp = -1.5;
                        creepEp = 0.25 + slopeEp * m_Config.SurfaceSenseUpFactor;
                        if (creepEp > 0.95) creepEp = 0.95;
                        if (creepEp < 0.05) creepEp = 0.05;
                    }
                    SetCachedInput(creepEp, 0, 0);
                    return;
                }
            }
            // Llegamos al punto exacto (o ya estamos dentro de 0.5m): brake fondo
            SetCachedInput(0, 0, 1.0);
            return;
        }

        // Avanzar el indice de waypoint TANTAS VECES como sea necesario para que
        // apunte a un waypoint que el bus aun no haya pasado. A velocidad de
        // crucero el bus pasa por 2-3 waypoints por tick (500ms a 50 km/h con
        // waypoints cada ~3.5m), entonces avanzar de a uno deja el indice atras
        // y el lookahead empieza a "mirar para atras", causando volantazos.
        // === TOLERANCIA DE LLEGADA (reverse) ===
        // Los ultimos wps de un reverse estan densos en el endpoint (creep-to-stop del
        // humano). El bus llega fisicamente pero deja de moverse antes de consumir esos wps
        // -> wp_idx se clava (ej. 651-806 de 844) -> AdvanceWaypoint nunca pasa el ultimo ->
        // la ruta NUNCA marca el fin. Fix: si el bus esta a <REVERSE_ARRIVAL_TOL del wp FINAL
        // (y ya en el tramo final), dar la ruta por completada (forzar el fin de linea).
        if (m_Config && m_Config.Waypoints && m_Config.Waypoints.Count() > 1) {
            int lastIdxAT = m_Config.Waypoints.Count() - 1;
            bool isRevFinalAT = (m_Config.Waypoints[lastIdxAT].mode == "reverse");
            // Ventana + tolerancia de llegada segun tipo de fin:
            //  - reverse: ventana amplia (wps densos del creep-to-stop) + tol chica.
            //  - normal (stop final): SOLO el tramo final + tol 2.5m. Antes esto estaba gateado a
            //    reverse -> en rutas normales (ej. MAPA generada) Boris no marcaba el fin al llegar,
            //    sobrepasaba el wp final e intentaba volver -> RULO EN REVERSE (2026-07-07). Generalizar
            //    la llegada a la parada normal frena+sostiene antes de sobrepasar = mata el rulo.
            bool inFinalStretchAT = false;
            float arrTolAT = REVERSE_ARRIVAL_TOL;
            if (isRevFinalAT) {
                inFinalStretchAT = (m_WaypointIndex > lastIdxAT - 300);
            }
            else if (m_Config.Waypoints[lastIdxAT].isStop) {
                inFinalStretchAT = (m_WaypointIndex > lastIdxAT - 30);
                arrTolAT = 2.5;
            }
            if (inFinalStretchAT) {
                float distFinalAT = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[lastIdxAT].GetVector());
                if (distFinalAT < arrTolAT) {
                    BZBusLog.Info("[ARRIVAL] a " + distFinalAT + "m del wp final (wp " + m_WaypointIndex + "/" + lastIdxAT + ", mode=" + m_Config.Waypoints[lastIdxAT].mode + ") -> ruta COMPLETA");
                    m_WaypointIndex = lastIdxAT;
                    AdvanceWaypoint();
                    return;
                }
            }
        }

        int advancesThisTick = 0;
        int maxAdvances = 50; // safety
        // BUG FIX (2026-07-20): el gate de pose marcaba la bandera DENTRO del bucle de avance, pero ese
        // bucle corta antes cuando Boris todavia esta lejos del wp -> la bandera quedaba PRENDIDA del tick
        // anterior y el control de alineacion tomaba el mando en cualquier lado: volante a fondo (se fue
        // del camino) y freno+maniobra en plena aproximacion, sin acercarse al punto. Se apaga UNA VEZ POR
        // TICK aca, antes del bucle: solo queda activa si el gate realmente disparo en ESTE tick.
        m_PoseGateActive = false;

        // CAP DE AVANCE POR MOVIMIENTO FISICO: el bot no puede saltar mas wps
        // que la distancia que efectivamente recorrio este tick. Sin esto, en
        // la transicion parking->normal (radius 1.2m -> 15m) el loop devora la
        // seccion entera de wps densos en una pasada Ã¢â‚¬â€ wp_idx queda al fin de
        // la transicion pero el cuerpo del bot no se movio (validado AI log
        // 2026-05-31: wp_idx salto 301 -> 373 en 4 ticks, bot movio solo 2.7m
        // total, quedo trabado al pie de la rampa pretendiendo estar arriba).
        float kmhAdv = bus.GetSpeedometerAbsolute();
        // 2026-07-01 FIX FREEZE CRUISE ALTA VELOCIDAD (Nissan wp405, 219m, ai_run 162057 server A):
        // el 0.5 era el periodo de tick ASUMIDO (500ms). El cap = movimiento del bot en un tick de
        // EXACTAMENTE 0.5s, sin margen. Pero el tick real jitterea (0.5-0.6s, visible en el propio
        // ai_run). Con dt=0.6 a 75 km/h el bot cubre 12.5m pero el cap solo deja avanzar 10.4m -> el
        // wp queda atras, se ACUMULA (cero margen), supera el wpRadius (15m) y en cruise (advByProj
        // solo modos especiales) ya no reengancha (Boris se aleja) -> FREEZE -> cruise a fondo -> muro.
        // Por eso B (ticks mas pegados a 0.5) aguantaba 97 km/h y A (jitterier) se clavaba a 75.
        // FIX: medir el dt REAL en vez de 0.5 -> el cap iguala el movimiento fisico real sin importar
        // el timing del server. Mata el FREEZE (dt>0.5) Y el racing-ahead (dt<0.5, por lo que antes
        // lo habian bajado de 0.6 a 0.5). Independiente del hardware = corre igual en cualquier server.
        float nowAdv = GetGame().GetTickTime();
        float dtAdv  = nowAdv - m_LastAdvTickTime;
        m_LastAdvTickTime = nowAdv;
        if (dtAdv <= 0.05 || dtAdv > 1.0) dtAdv = 0.5; // primer tick / pausa / stop / reanudacion -> nominal
        float maxTrajDistAdv = (kmhAdv / 3.6) * dtAdv;
        if (maxTrajDistAdv < 0.3) maxTrajDistAdv = 0.3; // floor minimo
        float cumTrajDistAdv = 0;

        while (advancesThisTick < maxAdvances) {
            BZWaypoint cur = CurrentWaypoint();
            if (!cur) break;
            // Radius mode-aware: parking usa 1.2m (vs 15m default).
            float wpRadius = WAYPOINT_RADIUS;
            if (cur.mode == "parking" || cur.mode == "reverse" || cur.mode == "maniobra") wpRadius = WAYPOINT_RADIUS_PARKING;
            // FollowPaintedToStop (2026-07-13): en el ACERCAMIENTO LENTO (velocidad dibujada baja), radio chico ->
            // el indice trackea la posicion REAL de Boris en vez de correr 15m adelante. Sin esto Boris lee la
            // velocidad target de un wp 15m adelante -> frena 15m antes de tiempo (llega arrastrando "muy lento") y
            // para 15m corto -> snap de 17m. Con radio chico: honra la velocidad dibujada DONDE ESTA y para en el
            // punto fisico. Solo en la zona lenta (drawn<12) -> la recta rapida sigue con radio 15 (no lo rompe).
            if (m_Config && m_Config.FollowPaintedToStop && cur.targetSpeed < 12.0) wpRadius = 2.0;

            // CLUSTER DETECTION: detectar wps donde el recording capturo al
            // humano COMPLETAMENTE PARADO (targetSpeed < 0.5 km/h). Esos son
            // "wait points" (gear shifts, espera al portal, etc.) Ã¢â‚¬â€ no hay
            // geometria nueva ahi, solo paso del tiempo. Usar radio ampliado
            // para que wp_idx atraviese el cluster aunque el bot no este
            // exactamente sobre la posicion. Antes la deteccion usaba distancia
            // entre wps consecutivos <10cm, pero eso disparaba en TODA la zona
            // lenta del parking (humano a 2 km/h hace segmentos chicos pero el
            // movimiento es legitimo). targetSpeed<0.5 solo dispara en stops reales.
            if (cur.hasInputData && cur.targetSpeed < 0.5) {
                // Distinguir CLUSTER ENTRY (primer wp del cluster donde el humano
                // recien para) vs CLUSTER INTERIOR (duplicados subsecuentes).
                // Entry Ã¢â€ â€™ radius estricto 0.8m: el bot DEBE alcanzar fisicamente
                // el punto donde el recording empieza la espera. Sino arranca
                // reverse con offset que acumula y choca (validado AI log:
                // success runs dist<1m, fail runs dist 1.1-1.2m al entry).
                // Interior Ã¢â€ â€™ radius 5m: pasar duplicados sin requerir proximidad.
                bool isClusterEntry = false;
                if (m_WaypointIndex >= 1 && m_Config) {
                    BZWaypoint prevForEntry = m_Config.Waypoints[m_WaypointIndex - 1];
                    if (prevForEntry.hasInputData && prevForEntry.targetSpeed >= 0.5) {
                        isClusterEntry = true;
                    }
                }
                if (isClusterEntry) wpRadius = 0.8;
                else                wpRadius = 5.0;
            }

            // === PROXIMITY SKIP (maniobra/parking a velocidad de curso) ===
            // 2026-06-07: en curva cerrada a 14-17 km/h, Boris pasa AL COSTADO del wp
            // sin entrar a su radius (1.2m). El wp queda DETRAS de Boris geograficamente
            // pero wp_idx no avanza, Stanley computa contra wp atrasado, calcula steering
            // INVERTIDO y Boris se desvia en cascada (validado AI log Impreza 2da curva:
            // wp_idx clavado 5+s mientras latdev sube 0.5 -> 15m y steering oscila +1 -> -1).
            // Fix: si Boris esta fisicamente mas cerca del wp+N que del wp actual,
            // avanzar directo a ese wp. Mismo principio que AR distance-based pero proactivo.
            // Solo aplica en modos especiales con velocidad > 5 km/h (descarta clusters lentos).
            if ((cur.mode == "maniobra" || cur.mode == "parking" || cur.mode == "reverse") && cur.hasInputData && bus.GetSpeedometerAbsolute() > 5.0) {
                vector busPosPS = m_Bus.GetPosition();
                float distToCurPS = vector.Distance(busPosPS, cur.GetVector());
                int bestIdxPS = m_WaypointIndex;
                float bestDistPS = distToCurPS;
                int peekMaxPS = 10;
                for (int pkPS = 1; pkPS <= peekMaxPS; pkPS++) {
                    int peekIdxPS = m_WaypointIndex + pkPS;
                    if (peekIdxPS >= m_Config.Waypoints.Count()) break;
                    BZWaypoint peekWpPS = m_Config.Waypoints[peekIdxPS];
                    // Solo wps en el mismo bloque mode (no saltar a otra zona)
                    if (peekWpPS.mode != cur.mode) break;
                    float distPeekPS = vector.Distance(busPosPS, peekWpPS.GetVector());
                    if (distPeekPS < bestDistPS) {
                        bestIdxPS = peekIdxPS;
                        bestDistPS = distPeekPS;
                    }
                }
                if (bestIdxPS > m_WaypointIndex) {
                    int skippedPS = bestIdxPS - m_WaypointIndex;
                    m_WaypointIndex = bestIdxPS;
                    advancesThisTick = advancesThisTick + skippedPS;
                    BZBusLog.Info("[ProximitySkip] mode=" + cur.mode + " +" + skippedPS + " wp (Boris closer to wp " + bestIdxPS + ", dist " + bestDistPS + "m vs current " + distToCurPS + "m)");
                    continue; // re-evaluar con nuevo wp_idx
                }
            }

            // === FIX OFF-PATH FREEZE EN REVERSE (along-track advance, 2026-06-14) ===
            // El radio Euclidiano (1.2m en reverse) incluye el offset LATERAL: si Boris se
            // desvia >1.2m de costado, NUNCA lo cumple -> wp se congela -> maneja off-path
            // derecho al muro (validado ai_run 2026-06-14: latdev 1m->14m con wp clavado en
            // 1894, las que fallan; las que pasan mantienen <1.2m). Fix: en reverse/parking/
            // maniobra (fuera de clusters/stops) avanzar si Boris paso el wp ALONG-TRACK
            // (proyeccion sobre el segmento cur->next), aunque este lateralmente lejos. Asi el
            // wp SIGUE al bot y Stanley corrige el offset chico antes de que explote, en vez de
            // soltarlo. El cap de movimiento fisico (abajo) evita que el wp_idx race ahead.
            bool advByProj = false;
            bool specialModeAdv = (cur.mode == "reverse" || cur.mode == "parking" || cur.mode == "maniobra");
            // 2026-06-25: saco hasInputData -> el avance off-path-freeze corre tambien en
            // Modo 3 (hasInputData=0). Sin esto, en reversa Modo 3 el wp se congela cuando
            // Boris llega off-line y maneja de costado (Nissan shallow stops, latdev 2->5m).
            // La proyeccion along-track solo usa posiciones; targetSpeed>=0.5 ya excluye clusters.
            bool notCluster = (cur.targetSpeed >= 0.5);
            if (specialModeAdv && notCluster && m_WaypointIndex + 1 < m_Config.Waypoints.Count()) {
                vector curVadv  = cur.GetVector();
                vector nextVadv = m_Config.Waypoints[m_WaypointIndex + 1].GetVector();
                float segDxAdv = nextVadv[0] - curVadv[0];
                float segDzAdv = nextVadv[2] - curVadv[2];
                vector busPadv = m_Bus.GetPosition();
                float dotAdv = (busPadv[0] - curVadv[0]) * segDxAdv + (busPadv[2] - curVadv[2]) * segDzAdv;
                if (dotAdv >= 0) advByProj = true;   // Boris paso la proyeccion del wp actual
            }
            if (!advByProj && vector.Distance(m_Bus.GetPosition(), cur.GetVector()) >= wpRadius) break;

            // Cap por movimiento fisico: si para llegar a este wp hay que avanzar
            // mas trajectoria de la que el bot fisicamente cubrio, parar.
            if (advancesThisTick > 0) {
                int prevIdxAdv = m_WaypointIndex - 1;
                if (prevIdxAdv >= 0) {
                    BZWaypoint prevWpAdv = m_Config.Waypoints[prevIdxAdv];
                    cumTrajDistAdv += vector.Distance(prevWpAdv.GetVector(), cur.GetVector());
                }
                if (cumTrajDistAdv > maxTrajDistAdv) break;
            }

            // BLOQUEO ANTE TRANSICION REVERSE: detection por mode == "reverse".
            // Si el wp esta en reverse y bot esta en forward (gear>0), o
            // viceversa, no avanzar wp_idx Ã¢â‚¬â€ el bot debe frenar a 0 km/h y
            // hacer el shift antes. Sin esto el wp_idx pasa por encima de la
            // transicion y el bot se pierde metros pasado del punto.
            int curBotGearAdv = bus.GetGear();
            bool curWantsReverse = (cur.mode == "reverse");
            bool botInReverseAdv = (curBotGearAdv == 0);
            if (curWantsReverse != botInReverseAdv && bus.GetSpeedometerAbsolute() > 1.5) break;

            // GATE DE POSE en la SALIDA de reversa: no dar la transicion por cumplida hasta estar
            // ALINEADO con el heading grabado del wp de salida (ver PoseGateEnabled en el config).
            if (m_Config.PoseGateEnabled && curWantsReverse && m_WaypointIndex + 1 < m_Config.Waypoints.Count()) {
                BZWaypoint nxtPose = m_Config.Waypoints[m_WaypointIndex + 1];
                if (nxtPose.mode != "reverse") {
                    float errPose = HeadingErrTo(nxtPose.targetHeading);
                    if (Math.AbsFloat(errPose) > m_Config.PoseGateTolDeg && m_PoseGateTicks < m_Config.PoseGateMaxTicks) {
                        m_PoseGateTicks++;
                        m_PoseGateActive = true;
                        m_PoseGateErr = errPose;
                        break;
                    }
                }
            }
            m_PoseGateTicks = 0;

            OnWaypointReached(cur);
            advancesThisTick++;

            // Si se decidio parar o ya estamos detenidos, no avanzar mas
            if (m_StopDecided || m_AtStop) break;
        }
        if (advancesThisTick > 1) {
            BZBusLog.Info("Tick: avance " + advancesThisTick + " waypoints en este tick, wpIdx ahora=" + m_WaypointIndex);
        }

        if (m_AtStop) {
            // Ajuste fino al stop (mismo bloque que arriba, por si el while loop
            // activo m_AtStop dentro de OnWaypointReached): empuje suave hasta
            // llegar al wp exacto del recording.
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                vector stopExactPos2 = m_Config.Waypoints[m_NextStopIndex].GetVector();
                float distToExact2 = vector.Distance(m_Bus.GetPosition(), stopExactPos2);
                float currentKmh2 = bus.GetSpeedometerAbsolute();
                // ENDPOINT GLIDE: este bloque es un DUPLICADO del de arriba (mismos 3 defectos: 3D, sin signo
                // y con return). MEDIDO: es el que mandaba -> creep fijo 0.25 acelero a 8.63 km/h, ahi supero
                // los 5 -> cayo al brake=1.0 de abajo -> freno y quedo CORTO a 0.658m. Y su return bloqueaba
                // el freno universal igual que el otro. Mismo tratamiento: si LLEGO -> clavar; si no, no cortar.
                if (EndpointGlideControl(bus, stopExactPos2)) return;
                if (distToExact2 > 0.5 && distToExact2 < 3.0 && currentKmh2 < 5.0) {
                    SetCachedInput(0.25, 0, 0);
                    return;
                }
            }
            SetCachedInput(0, 0, 1.0);
            return;
        }

        BZWaypoint target = CurrentWaypoint();
        if (!target) return;

        // Gear inferido desde velocidad actual usando la tabla de gear ranges
        // del vehiculo (data/gear_ranges.json, medido empiricamente con
        // NUMPAD 3 / NUMPAD 0). Reemplaza el uso del targetGear grabado, que
        // dependia de cuan bien hizo los cambios el operador durante grabacion.
        // Fallback: si el vehiculo no esta en la tabla, dejar que la AT
        // (CarScript EOnPostSimulate) maneje el gear basandose en RPM.
        string vehicleClass = bus.GetType();
        float currentKmh = bus.GetSpeedometerAbsolute();
        // REVERSE: forzar m_DesiredGear = 0 cuando el wp pide reverse mode.
        // Sin esto el InferGear forward devuelve FIRST/SECOND, CarScript OnInput
        // hace ShiftTo(desired) cada frame y mi reverse shift dura solo 1 frame.
        // El m_DesiredGear es el SOURCE OF TRUTH del CarScript Ã¢â‚¬â€ hay que setearlo.
        if (target.mode == "reverse") {
            // NO METER REVERSA HASTA HABER LLEGADO (2026-07-20, MEDIDO). Los wps normales se consumen con
            // radio 15 m, asi que el indice se POSA sobre el wp de reversa con Boris todavia lejos. El gate
            // de avance se niega a consumirlo (va rapido), pero esta linea no consultaba ese gate: veia
            // mode=="reverse" y metia marcha atras igual. Medido: a 12 m del punto y 17 km/h le puso
            // reversa -> freno en seco, paro a 11 m y RETROCEDIO alejandose (el "rulo" del mapa, en las 2
            // maniobras). Indice posado != punto alcanzado. Una vez EN reversa se mantiene (GetGear()==0).
            float dGearRev = vector.Distance(bus.GetPosition(), target.GetVector());
            // Y TAMPOCO ANTES DE TERMINAR DE GIRAR (2026-07-20, MEDIDO). El humano completa la rotacion
            // YENDO HACIA ADELANTE y recien ahi mete la reversa: llego al punto a 144.2 grados cuando
            // hacian falta 146.5 (2 de error). Boris metia reversa a 0.8 m con 15 grados por girar todavia,
            // y ahi el volante le INVIERTE el efecto (en marcha atras el tren delantero rota al reves) ->
            // no podia terminar el giro, arrancaba la reversa 14 grados torcido y se iba de la recta.
            bool alineadoRev = true;
            if (m_Config.ReverseGearNeedAlign && target.targetHeading != 0 && bus.GetGear() != 0) {
                float errGr = HeadingErrTo(target.targetHeading);
                if (Math.AbsFloat(errGr) > m_Config.ReverseGearAlignTolDeg && m_RevAlignTicks < m_Config.ReverseGearAlignMaxTicks) {
                    alineadoRev = false;
                    m_RevAlignTicks++;
                }
            }
            if (alineadoRev) m_RevAlignTicks = 0;
            if ((dGearRev < m_Config.ReverseGearArmM && alineadoRev) || bus.GetGear() == 0) m_DesiredGear = 0;
        } else {
            int inferred = BZGearRangeTable.GetInstance().InferGear(vehicleClass, currentKmh, m_DesiredGear);
            if (inferred > 0) {
                m_DesiredGear = inferred;
            }
        }
        // Si inferred == -1 (vehiculo no en tabla), m_DesiredGear queda como esta
        // y la AT lo modula basandose en RPM.

        // === FASE 2c: REPLAY de bocina + luces del waypoint actual (2026-06-27) ===
        // Solo en el camino de "Boris manejando" (post m_Frozen/m_Paused/m_RouteStopped).
        // Replay HORN (spatial: honkea donde honkeaste). Solo al cambiar de estado.
        int wpHorn = target.targetHorn;
        if (wpHorn != m_LastHornState) {
            SetVehicleHorn(wpHorn);
            m_LastHornState = wpHorn;
        }
        // LIGHTS segun LightsMode (SetVehicleLights ya chequea el estado actual -> idempotente).
        SetVehicleLights(ComputeDesiredLights(target.targetLights));

        // === HEARTBEAT de luces (FIX REAL 2026-06-27) ===
        // El flag native de luz del faro (LightOn) es LOCAL del cliente que SIMULA el auto;
        // el observador del AI car no lo simula y el engine lo resetea -> las luces se "apagan"
        // hasta el proximo OnVariablesSynchronized() (que SOLO se dispara cuando cambia una
        // NetSyncVar: ej. la bocina). Por eso parecian reprender justo al honkear.
        //   Fix v2: SetSynchDirty() pelado NO re-dispara OnVariablesSynchronized() si no cambia
        // el VALOR de una NetSyncVar (por eso la luz solo volvia al honkear: la bocina SI cambia
        // m_CarHornState). Llamamos BZPulseLights() que incrementa m_BZLightPulse (cambia valor)
        // + SetSynchDirty() -> ahora SI se dispara en los observadores -> BZBusCarScript re-aplica
        // LightOn() -> faro estable. Gated por acumulador de dt (Tick ~500ms) para no spamear.
        m_LightsHeartbeatAccum += TICK_DT_S;
        if (m_LightsHeartbeatAccum >= LIGHTS_HEARTBEAT_S) {
            m_LightsHeartbeatAccum = 0;
            if (m_LightsOn) {
                CarScript lightsCar = CarScript.Cast(m_Bus);
                if (lightsCar) lightsCar.BZPulseLights();
            }
        }

        // === TOP-UP de bateria (secundario, inofensivo 2026-06-27) ===
        // Mantiene CompEM cargada (los faros drenan energia). NO es el fix de las luces
        // (ese es el heartbeat de arriba) pero es buena practica para no quedarse sin energia.
        // Gated aparte cada ~4s; misma busqueda de bateria que EquipBus.
        m_BattTopUpAccum += TICK_DT_S;
        if (m_BattTopUpAccum >= BATT_TOPUP_S) {
            m_BattTopUpAccum = 0;
            ItemBase batt = ItemBase.Cast(m_Bus.GetInventory().FindAttachment(CarBattery.SLOT_ID));
            if (!batt) batt = ItemBase.Cast(m_Bus.GetInventory().FindAttachment(TruckBattery.SLOT_ID));
            if (batt && batt.GetCompEM()) {
                float battMax = batt.GetCompEM().GetEnergyMax();
                if (batt.GetCompEM().GetEnergy() < battMax * 0.8) {
                    batt.GetCompEM().SetEnergy(battMax);
                    BZBusLog.Info("[Battery] top-up");
                }
            }
        }

        DriveTowards(bus, target);

        // Log de estado post-DriveTowards
        BZBusLog.Info("Tick: gear=" + bus.GetGear() + " kmh=" + bus.GetSpeedometerAbsolute());

        // Stuck timer: si no avanzamos nada en este tick, incrementar; si pasa
        // el timeout, forzar un advance manual (escape hatch para bug raro).
        // Safety SEPARADA del AutoRecovery (corre aunque este OFF; solo empuja el
        // wp index, NO teleporta). Timeout configurable per-ruta (StuckAdvanceTimeoutS).
        if (advancesThisTick == 0) {
            m_StuckTimer += 1.0;
            float stuckTimeout = STUCK_TIMEOUT;
            if (m_Config && m_Config.StuckAdvanceTimeoutS > 0) stuckTimeout = m_Config.StuckAdvanceTimeoutS;
            if (m_StuckTimer >= stuckTimeout) {
                m_StuckTimer = 0;
                BZBusLog.Warn("Bus trabado en waypoint " + m_WaypointIndex + ", forzando avance.");
                AdvanceWaypoint();
            }
        } else {
            m_StuckTimer = 0;
        }

        // DWELL-SKIP (Sonom4n 2026-06-24): si Boris paro en un dwell grabado (targetSpeed~0 = el
        // humano sentado quieto con handbrake antes de meter reversa) y hay reversa adelante,
        // el wp_idx NO avanza (el along-track es por posicion, Boris quieto no se mueve) -> se
        // clava 20-30s y despues el AR salta el wp_idx mal (salteando el ModeSnap). El dwell es
        // solo el humano quieto: no hay que recorrerlo. Saltamos directo al primer wp de reversa
        // -> el ModeSnap reposiciona al punto exacto + mata velocidad -> reversa limpia.
        if (m_Config && m_Config.Waypoints) {
            BZWaypoint curDwell = CurrentWaypoint();
            float kmhDwell = bus.GetSpeedometerAbsolute();
            if (curDwell && kmhDwell < 1.0 && curDwell.targetSpeed < 0.5 && (curDwell.mode == "normal" || curDwell.mode == "")) {
                // CREEP-FIRST (Sonom4n 2026-06-24): saltar SOLO si Boris ya esta SOBRE el punto (gap < 0.5m).
                // Si quedo corto, el creep-to-point lo maneja primero -> evita que el dwell-skip le gane
                // la carrera (cuando Boris para del todo) y el snap teletransporte en vez de manejar el
                // gap = los "creep-fail" T8/T9 del 2026-06-24 (paraban ~3.5m corto y saltaban).
                vector busPdw = bus.GetPosition();
                vector wpPdw  = curDwell.GetVector();
                float gapDw = Math.Sqrt((busPdw[0]-wpPdw[0])*(busPdw[0]-wpPdw[0]) + (busPdw[2]-wpPdw[2])*(busPdw[2]-wpPdw[2]));
                if (gapDw < 0.5) {
                    int revScanEnd = Math.Min(m_WaypointIndex + 300, m_Config.Waypoints.Count() - 1);
                    for (int rdi = m_WaypointIndex + 1; rdi <= revScanEnd; rdi++) {
                        if (m_Config.Waypoints[rdi].mode == "reverse") {
                            BZBusLog.Info("[DWELL-SKIP] wp_idx " + m_WaypointIndex + " -> " + rdi + " (sobre el punto gap=" + gapDw + "m -> inicio reversa)");
                            m_WaypointIndex = rdi;
                            break;
                        }
                    }
                }
            }
        }

        // === WP_IDX SNAP (Direct Replay recovery) ===
        // Cuando Boris over-rotates y se sale del corredor, wp_idx queda clavado
        // (no avanza porque el wp objetivo esta lejos fisicamente). Stanley con
        // el wp clavado calcula correccion hacia un punto detras o demasiado
        // lejos Ã¢â€ â€™ Boris no recupera. Insight 2026-06-03: snap wp_idx al wp
        // fisicamente mas cercano en ventana local, asi Stanley apunta al
        // corredor real donde Boris esta. Reset del state interno tambien.
        // 2026-06-14: antes solo en DirectReplay; ahora tambien en reverse/parking/maniobra
        // como BACKSTOP del along-track advance (si el wp igual no avanza 3s, re-anclar).
        bool snapEligible = (m_Config && m_Config.DirectReplayFromWaypoint >= 0);
        BZWaypoint curSnapMode = CurrentWaypoint();
        if (curSnapMode && (curSnapMode.mode == "reverse" || curSnapMode.mode == "parking" || curSnapMode.mode == "maniobra")) snapEligible = true;
        if (snapEligible) {
            if (advancesThisTick == 0) {
                m_DR_NoAdvanceTimer += 0.5;
            } else {
                m_DR_NoAdvanceTimer = 0;
            }
            if (m_DR_NoAdvanceTimer >= 3.0) {
                // Snap: buscar wp mas cercano fisicamente en ventana
                vector busPosSnap = m_Bus.GetPosition();
                int snapFrom = m_WaypointIndex - 50;
                if (snapFrom < 0) snapFrom = 0;
                int snapTo = m_WaypointIndex + 50;
                if (snapTo >= m_Config.Waypoints.Count()) snapTo = m_Config.Waypoints.Count() - 1;
                float bestDist = 99999.0;
                int   bestIdx = -1;
                for (int snapI = snapFrom; snapI <= snapTo; snapI++) {
                    float d = vector.Distance(busPosSnap, m_Config.Waypoints[snapI].GetVector());
                    if (d < bestDist) { bestDist = d; bestIdx = snapI; }
                }
                if (bestIdx >= 0 && bestIdx != m_WaypointIndex) {
                    BZBusLog.Info("[DR-SNAP] wp_idx " + m_WaypointIndex + " -> " + bestIdx + " (dist=" + bestDist.ToString() + "m)");
                    m_WaypointIndex = bestIdx;
                    m_DR_PrevWpIdx = bestIdx;
                    m_DR_NoAdvanceTimer = 0;
                }
            }
        }

        // === STUCK DIAGNOSTIC ===
        // Cuando Boris esta intentando moverse (throttle alto) pero no avanza
        // (kmh<1) sostenido, volcar al RPT el estado de TODAS las ruedas + clutch
        // + RPM + gear + linear/angular velocity. Diagnostico para casos donde
        // el receptor (physics engine) no transmite la fuerza aplicada.
        // Insight 2026-06-03: el T6 stuck en (8137,9301) NO era choque contra
        // obstaculo (el usuario verifico visualmente sin colision). Throttle=1
        // sostenido 10s con kmh=0 = receptor no respondio. Falta saber por que.
        float kmhDiag = bus.GetSpeedometerAbsolute();
        if (kmhDiag < 1.0 && m_CachedThrottle > 0.5) {
            m_StuckDiagTimer += 0.5; // tick es cada 500ms
            float now = GetGame().GetTickTime();
            // Loguear al cruzar 3s y despues cada 5s mientras dure el stuck
            bool firstCross = (m_StuckDiagTimer >= 3.0 && m_StuckDiagLastLog == 0);
            bool periodicRefresh = (m_StuckDiagLastLog > 0 && now - m_StuckDiagLastLog >= 5.0);
            if (firstCross || periodicRefresh) {
                m_StuckDiagLastLog = now;
                BZBusLog.Info("==============================================");
                BZBusLog.Info("[STUCK-DIAG] Boris stuck " + m_StuckDiagTimer.ToString() + "s | wp=" + m_WaypointIndex + " pos=" + m_Bus.GetPosition().ToString());
                BZBusLog.Info("[STUCK-DIAG] inputs: throttle=" + m_CachedThrottle + " brake=" + m_CachedBrake + " steering=" + m_CachedSteering);
                BZBusLog.Info("[STUCK-DIAG] engine: rpm=" + bus.EngineGetRPM() + " idle=" + bus.EngineGetRPMIdle() + " on=" + bus.EngineIsOn());
                BZBusLog.Info("[STUCK-DIAG] gear: current=" + bus.GetCurrentGear() + " GetGear=" + bus.GetGear() + " neutral=" + bus.GetNeutralGear() + " count=" + bus.GetGearCount());
                BZBusLog.Info("[STUCK-DIAG] read inputs: throttle=" + bus.GetThrottle() + " brake=" + bus.GetBrake() + " clutch=" + bus.GetClutch() + " handbrake=" + bus.GetHandbrake());
                BZBusLog.Info("[STUCK-DIAG] velocity: speedo=" + kmhDiag + " linVel=" + GetVelocity(m_Bus).ToString());
                int wheelCount = bus.WheelCountPresent();
                BZBusLog.Info("[STUCK-DIAG] wheels: present=" + wheelCount + " anyLocked=" + bus.WheelIsAnyLocked());
                for (int wi = 0; wi < wheelCount; wi++) {
                    BZBusLog.Info("[STUCK-DIAG]   wheel[" + wi + "]: angVel=" + bus.WheelGetAngularVelocity(wi) + " contact=" + bus.WheelHasContact(wi) + " locked=" + bus.WheelIsLocked(wi));
                }
                BZBusLog.Info("==============================================");
            }
        } else {
            m_StuckDiagTimer = 0;
            m_StuckDiagLastLog = 0;
        }

        // AI logging: graba el estado actual del bus al CSV si esta activo.
        // Determinamos el modo actual (parking si esta cerca de un stop, crucero
        // si no) y la distancia al proximo stop para incluirlos en el log.
        // CHECK "Log ai_run" del reproductor (2026-08-09): arranca el logger en CUALQUIER ruta (no solo
        // frame-replay ni NUMPAD7). Corre en el tick de manejo -> se prende al primer frame de conduccion.
        if (m_LogAiRun && !m_AILoggerActive) StartAILogging();
        if (m_AILoggerActive) {
            float distToNextStopForLog = 99999.0;
            string activeMode = "cruise";
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                distToNextStopForLog = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[m_NextStopIndex].GetVector());
                if (distToNextStopForLog <= 40.0) activeMode = "parking";
            }
            if (m_AtStop) activeMode = "at_stop";
            // Anotar si el bus iba dentro o fuera del corredor en este sample.
            // Util para validar empiricamente que el corredor estaba absorbiendo
            // micro-desviaciones (in) vs corrigiendo activamente (out).
            if (m_CorridorValid) {
                float aOff = m_CorridorLateralOffset;
                if (aOff < 0) aOff = -aOff;
                if (aOff < CORRIDOR_HALF_WIDTH) activeMode = activeMode + "_inC";
                else                            activeMode = activeMode + "_outC";
            }
            LogAITick(activeMode, distToNextStopForLog);
        }
    }

    // -------------------------------------------------------------------------
    // Waypoints

    private void OnWaypointReached(BZWaypoint wp) {
        if (!wp.isStop) {
            AdvanceWaypoint();
            return;
        }

        // Idempotente: si ya decidimos parar, no re-procesar
        if (m_StopDecided) return;

        // v1.0: el bus para SIEMPRE en cada parada. No sabemos a priori quien
        // del bus quiere bajarse ni quien va a llegar a la parada en el momento
        // del scan. Mejor parar siempre como un colectivo real.
        //
        // v2 podria agregar un sistema "ring the bell": jugador onboard senaliza
        // en que parada se baja, jugador esperando levanta la mano. Las funciones
        // HasWaitingPlayers / HasOnboardPassengers quedan disponibles para esa
        // version, ahora no se usan.
        m_StopDecided = true;
        BZBusLog.Info("OnWaypointReached: parada " + wp.name + ", aproximacion suave hasta " + STOP_FINAL_RADIUS + "m");
    }

    // 2026-06-27 FASE 1 luces+claxon: primitivas del DSL.
    // ToggleHeadlights()/IsScriptedLightsOn() viven en CarScript (NO en la base Car):
    // hay que castear a CarScript (confirmado leyendo el scripts.pbo vanilla).
    void SetVehicleLights(bool on) {
        CarScript car = CarScript.Cast(m_Bus);
        if (!car) return;
        // OJO (2026-06-27): IsScriptedLightsOn() devuelve false en el AI car server-side -> el guard
        // por estado-real fallaba y toggleaba CADA tick (flip-flop = parpadeo = se ven apagadas). El RPT
        // lo mostro: "[Lights] ON" cada ~0.5s. Guard por m_LightsOn (creencia) -> toggle UNA sola vez = estable.
        if (on == m_LightsOn) return;
        // FIX v3 (2026-06-27): ToggleHeadlights() + ForceUpdateLights NO replicaba el haz al
        // observador. Causa raiz: LightOn/LightOff/LightIsOn son flag LOCAL del engine (proto native
        // de Transport), NO NetSyncVariable; el engine solo lo dispara en el cliente que SIMULA el
        // auto (owner/player driver). El AI car sin player no lo simula en el observador -> LightIsOn()
        // queda false alli -> UpdateLightsClient nunca crea m_Headlight. ForceUpdateLights re-corre
        // UpdateLights() pero LightIsOn() sigue false. BOCINA si anda porque m_CarHornState SI es
        // NetSync + SetSynchDirty.
        //   Solucion: BZSetLights() (modded CarScript en BZBusCarScript.c) replica el estado deseado
        // via NetSyncVariable propia (m_BZLightsWanted) y en OnVariablesSynchronized() fuerza el flag
        // native LOCAL del observador con LightOn()/LightOff() + UpdateLights(). Asi el cliente ve
        // LightIsOn()==true y dibuja m_Headlight.
        car.BZSetLights(on);
        m_LightsOn = on;
        string st = "OFF";
        if (on) st = "ON";
        BZBusLog.Info("[Lights] " + st);
    }

    // 2026-06-27: bocina NATIVA de DayZ. API REAL confirmada leyendo scripts.pbo vanilla:
    // CarScript.SetCarHornState(int) + enum global ECarHornState { OFF=0, SHORT=1, LONG=2 }.
    // Reemplaza al EffectSound + SoundSet adivinado de la Fase 1.
    void SetVehicleHorn(int state) {
        CarScript car = CarScript.Cast(m_Bus);
        if (!car) return;
        car.SetCarHornState(state);
    }

    // 2026-06-27 FASE 2c: estado de luces deseado segun LightsMode de la ruta.
    //   "auto"=on de noche (DEFAULT 2026-06-28), "off"=nunca [stealth nocturno], "auto_inverted"=off de noche [stealth],
    //   "replay"=sigue lo grabado (wpTargetLights==1), "on"=siempre. Hora del mundo via GetDate.
    // El fallback "auto" de abajo solo aplica si m_Config es null (no deberia pasar en ruta cargada);
    // espeja el nuevo default del campo LightsMode en BZBusConfig.
    bool ComputeDesiredLights(int wpTargetLights) {
        string mode = "auto";
        if (m_Config) mode = m_Config.LightsMode;
        if (mode == "on") return true;
        if (mode == "off") return false;
        if (mode == "replay") return (wpTargetLights == 1);
        // auto / auto_inverted: segun hora del juego
        int yr; int mo; int dy; int hh; int mm;
        hh = 12;
        if (GetGame().GetWorld()) GetGame().GetWorld().GetDate(yr, mo, dy, hh, mm);
        bool night = (hh < 6 || hh >= 19);
        if (mode == "auto_inverted") return !night;
        return night;
    }

    void HornBeep() {
        // bocinazo corto de cortesia (paradas / verbo horn). El motor suena solo (nativo).
        SetVehicleHorn(ECarHornState.SHORT);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.HornOff, 600, false);
        BZBusLog.Info("[Horn] beep");
    }

    void HornOff() {
        SetVehicleHorn(ECarHornState.OFF);
    }

    private void OnStopFinished() {
        // STOP-LEARNER (gancho 3/3): Boris ya se detuvo el stopDuration completo -> medir su resting position
        // vs el wp de parada y aprender el overshoot longitudinal. m_NextStopIndex AUN apunta a la parada
        // recien completada (OnHornFinished lo avanza despues). Es el overshoot que el corridor learner descarta.
        if (m_Bus && m_Config && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            BZWaypoint slWp = m_Config.Waypoints[m_NextStopIndex];
            vector slStopPos = slWp.GetVector();
            // direccion de aproximacion: preferir el targetHeading GRABADO del wp (robusto al cluster de
            // duplicados del endpoint, que hacia dar tangente CERO). Fallback: caminar atras a un wp distinto.
            vector slDir = vector.Zero;
            if (slWp.targetHeading != 0) {
                float slH = slWp.targetHeading * Math.DEG2RAD;
                slDir = Vector(Math.Sin(slH), 0, Math.Cos(slH));
            } else {
                for (int slj = m_NextStopIndex - 1; slj >= 0 && slj > m_NextStopIndex - 60; slj--) {
                    vector slp = m_Config.Waypoints[slj].GetVector();
                    if (vector.Distance(slp, slStopPos) > 1.0) {
                        float sldx = slStopPos[0] - slp[0];
                        float sldz = slStopPos[2] - slp[2];
                        float slm = Math.Sqrt(sldx * sldx + sldz * sldz);
                        slDir = Vector(sldx / slm, 0, sldz / slm);
                        break;
                    }
                }
            }
            if (m_Config.StopLearnerEnabled) BZStopLearner.GetInstance().OnStopReached(m_NextStopIndex, m_Bus.GetPosition(), slStopPos, slDir);
        }
        // 2026-06-27: bocinazo en parada SOLO si HornMode == "stops". Default "replay" NO honkea en
        // cada parada (eso disparaba el claxon en TODAS las grabaciones). El claxon grabado va por replay.
        if (m_Config && m_Config.HornMode == "stops") HornBeep();
        // Espera adicional 1s antes de arrancar para simular pausa de bocinazo
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.OnHornFinished, 1000, false);
    }

    private void OnHornFinished() {
        // API TAXI: si el runner esta en modo hold-at-stops, NO auto-continua -> queda DETENIDO en
        // el stop (m_AtStop sigue true) esperando ResumeFromHold() del eAI chofer. El gate de manejo
        // (m_HoldActive) lo mantiene quieto. Asi el aldeano corre su dialogo sin carrera de tiempos.
        if (m_HoldAtStops) {
            m_HoldActive = true;
            BZBusLog.Info("OnHornFinished: HOLD en stop (wp=" + m_WaypointIndex + "), esperando reanudar del eAI");
            return;
        }
        m_AtStop = false;
        AdvanceWaypoint();
        m_NextStopIndex = FindNextStopIndex(m_WaypointIndex);
        BZBusLog.Info("OnHornFinished: proximo stop idx=" + m_NextStopIndex);
    }

    // Devuelve el indice del primer waypoint con isStop=true desde fromIdx (inclusive)
    // hacia adelante. Devuelve -1 si no hay mas stops (fin de linea).
    //
    // NO MIRA MAS ALLA DEL TRAMO ACTIVO (2026-07-21, MEDIDO en ESQ). La toma ESQ es un
    // CIRCUITO CERRADO: el endpoint (wp224) queda a 5,64 m del spawn (wp0). El safety net
    // de AtStop dispara con "parado (<1 km/h) y a menos de 5 m del stop" -> en el spawn eso
    // es cierto para el ENDPOINT. Mientras Boris arrancaba de una no se notaba; con la rampa
    // de calibracion de zona muerta se queda ~10 s casi quieto al lado del spawn y el net
    // salta: m_WaypointIndex se sincroniza a 224, el clamp de tramo lo devuelve a 92, y queda
    // en bucle "en parada, sale en 6 seg" sin moverse nunca. Ademas el STOPLEARN "aprendia"
    // que paro a 7,6 cm del cartel cuando jamas salio del spawn.
    //
    // El principio es el mismo del tramo latcheado: Boris NO conoce nada de un tramo que
    // todavia no abrio -- tampoco sus paradas. Al abrirse el tramo (SetLegFrom) se recalcula.
    private int FindNextStopIndex(int fromIdx) {
        if (!m_Config) return -1;
        int count = m_Config.Waypoints.Count();
        int tope = count - 1;
        if (m_LegInit && m_LegEnd < tope) tope = m_LegEnd;
        // EL INTERCAMBIO (legBreak) que cierra un tramo FORWARD es una PARADA (2026-07-22, Sonom4n). Sin esto
        // el creep de precision (EndpointGlide) nunca se enganchaba ahi y Boris paraba 2-3m corto (el
        // control lidera con el eje delantero -> el ORIGEN queda ~media-distancia-entre-ejes corto). Como
        // el endpoint clava 0.66m gracias a ese creep, tratamos el intercambio igual y el ORIGEN llega al
        // punto. SOLO forward: el EndpointGlide es forward-only (creep+signo) y en reversa empujaria mal
        // -> interc2 (reversa) se maneja con su propio creep + compensacion de eje trasero.
        bool legFwd = !ActiveLegIsReverse();
        for (int i = fromIdx; i <= tope; i++) {
            if (m_Config.Waypoints[i].isStop) return i;
            if (legFwd && m_Config.Waypoints[i].legBreak) return i;
        }
        return -1;
    }

    private void AdvanceWaypoint() {
        int last = m_Config.Waypoints.Count() - 1;

        if (!m_Reverse) {
            m_WaypointIndex++;
            if (m_WaypointIndex > last) {
                // Fin de linea: el bus queda parado en la ultima parada y se
                // respawnea automaticamente en wp 0 despues de RespawnDelay
                // segundos (default 60s, configurable por JSON). Esto permite
                // operacion continua del servicio sin intervencion manual.
                m_WaypointIndex = last;
                m_AtStop        = true;
                // LEARNER (gancho 4/4): vuelta completa -> acumula; cada LAPS_PER_SHAPE hornea el mapa
                // sistematico sobre la pre-distorsion y lo persiste. En modo servicio (respawn) las vueltas
                // se encadenan solas -> Boris se afina lap a lap. Ver el log [LEARN] SHAPE.
                if (m_Config.CorridorLearnerEnabled) BZCorridorLearner.GetInstance().OnLapComplete();
                // CONVOY: llego al patio (fin de ruta) -> los bots se BAJAN animado y el vehiculo queda
                // DETENIDO ahi (sin auto-respawn). Fin de la secuencia del vehiculo. El despawn final lo
                // maneja OnQuestComplete cuando el jugador mata a los sobrevivientes.
                if (m_QuestFleeing) {
                    BZBusLog.Info("[QUEST-CONVOY] llego al patio (fin de ruta) -> frena + DISMOUNT en 1.5s, vehiculo detenido (sin respawn)");
                    m_RouteStopped = true;   // frena YA (que pare del todo antes de bajar)
                    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.DismountQuestCrew, 1500, false);
                    return;
                }
                // 2026-06-27: bocinazo de FIN de ruta solo si HornMode == "finish" ("llegamos"). Va aca
                // (no en OnStopFinished) para sonar una sola vez al terminar, no en cada parada.
                if (m_Config && m_Config.HornMode == "finish") HornBeep();
                // === END HOLD (2026-06-27, simetrico al SpawnHold) ===
                // Antes de despawnear/respawnear, sostener al vehiculo QUIETO N seg
                // (EndHoldSeconds). m_RouteStopped frena el avance de ruta y la
                // AutoRecovery; m_EndHoldActive aplica handbrake explicito en el Tick.
                // El despawn/respawn se dispara recien al expirar el hold (sumamos el
                // hold al delay del CallLater). EndHoldSeconds <= 0 -> sin hold = legacy.
                float endHoldSec = 0;
                if (m_Config && m_Config.EndHoldSeconds > 0) endHoldSec = m_Config.EndHoldSeconds;
                if (endHoldSec > 0) {
                    m_RouteStopped  = true;   // frena YA y bloquea avance/AutoRecovery durante el hold
                    m_EndHoldActive = true;   // handbrake explicito en el Tick durante el hold
                    m_EndHoldTime   = GetGame().GetTickTime();
                    Car endHoldCar = Car.Cast(m_Bus);   // 2026-07-07: freno nativo anti-rollback (aguanta la
                    if (endHoldCar) endHoldCar.SetBrakesActivateWithoutDriver(true);  // pendiente sin depender del input del bot)
                    BZBusLog.Info("[EndHold] fin de ruta: hold " + endHoldSec + "s antes de despawn/respawn");
                }
                int endHoldMs = Math.Round(endHoldSec * 1000);

                // Rutas cargadas por el admin (reproductor, origin="lista"): por defecto se DESPAWNEAN al
                // terminar -> el vehiculo no queda parqueado en el fin de linea. El runner sale del registro
                // multiton. El boot/config sigue con el loop de servicio continuo (respawn por RespawnDelay).
                // (Las de origin="quest" no llegan aca: su fin lo maneja la FSM del quest / OnQuestComplete.)
                if (m_Origin == "lista") {
                    BroadcastGlobal("Route '" + m_RouteName + "' complete — vehicle despawned.");
                    // Despawn gateado por el end-hold. Con hold<=0 -> inmediato (legacy
                    // EXACTO, llamada sincrona). Con hold>0 -> diferido al expirar el hold.
                    if (endHoldMs <= 0) {
                        EndOfRouteDespawn();
                    } else {
                        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.EndOfRouteDespawn);
                        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EndOfRouteDespawn, endHoldMs, false);
                    }
                    return;
                }
                int respawnDelaySec = 60;
                if (m_Config && m_Config.RespawnDelay > 0) respawnDelaySec = m_Config.RespawnDelay;
                BroadcastGlobal("BZ AutoDrive reached the end of the line — restarting in " + respawnDelaySec + "s");
                // El respawn ya estaba diferido por RespawnDelay; le sumamos el end-hold
                // para que el vehiculo se quede quieto en la terminal antes de reiniciar.
                int respawnMs = respawnDelaySec * 1000 + endHoldMs;
                GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.RespawnBus, respawnMs, false);
                return;
            }
        } else {
            m_WaypointIndex--;
            if (m_WaypointIndex < 0) {
                m_Reverse       = false;
                m_WaypointIndex = 1;
                BroadcastGlobal("BZ AutoDrive left " + m_Config.Waypoints[0].name + ".");
            }
        }

        SetNextWaypoint();
    }

    // Despawn final de una toma origin="lista". Factorizado para poder diferirlo
    // por EndHoldSeconds (end-hold) via CallLater. Con hold=0 se llama en el frame
    // del fin de ruta (comportamiento legacy). Saca vehiculo + driver, cancela el
    // Tick/respawn pendiente y desregistra el runner del multiton.
    private void EndOfRouteDespawn() {
        m_EndHoldActive = false;
        CleanupEntities();                  // saca vehiculo + driver + cancela Tick/respawn pendiente
        BZBusService.UnregisterRunner(this);
    }

    private void SetNextWaypoint() {
        BZWaypoint wp = CurrentWaypoint();
        if (!wp || !m_Group) return;

        m_Group.ClearWaypoints();
        m_Group.AddWaypoint(wp.GetVector());
    }

    // ===== FRENO UNIVERSAL DEL ENDPOINT (2026-07-17) =====
    // Vive ACA (camino AtStop) y NO en DriveTowards: MEDIDO, cuando m_AtStop se activa el tick ya no llega a
    // DriveTowards (el indice corre con radio 15m, consume el ultimo wp -> CurrentWaypoint()=null -> return ->
    // fin de ruta -> EndHold clava el handbrake DONDE ESTE, medido 1.98m corto). El bloque AtStop ES la
    // autoridad del endpoint: por eso tiene los return. Una sola funcion para los DOS bloques AtStop (son
    // duplicados; parchear uno solo fue el bug anterior).
    // FISICA, no perillas -> sirve en cualquier contexto (rapido, lento, pendiente, grabacion):
    //    a_necesaria = v^2/(2*d) - 9.8*sin(pendiente)   ;   brake = a_necesaria / BrakeDecelMS
    // Distancia 2D y CON SIGNO (proyectada sobre la direccion de llegada): la 3D tiene piso de ~0.52m
    // (SampleTerrainY pone la Y del wp en el TERRENO y el origen del auto va mas arriba) y la sin-signo
    // empuja MAS LEJOS apenas pasa el punto. Devuelve true si tomo el control (el llamador debe return).
    private bool EndpointGlideControl(Car bus, vector stopExactPos) {
        if (!m_Config || !m_Config.EndpointGlide) return false;
        // FORWARD-ONLY (re-aplicado 2026-07-30): el IMAN es forward (throttle positivo + heading-align con signo
        // forward). En reversa empujaria para adelante y con el volante invertido -> romperia SEQ2. La reversa clava
        // por su camino propio (ReverseApproachStop / ReverseEndpointCreep, delicado, "no se toca"). El reverse-magnet
        // seria el mismo iman con sign flip, deliberado y aparte. Ver [[project_endpoint_magnet]].
        if (ActiveLegIsReverse()) return false;
        if (m_NextStopIndex < 0 || m_NextStopIndex >= m_Config.Waypoints.Count()) return false;
        vector bposG = m_Bus.GetPosition();
        float dxG = stopExactPos[0] - bposG[0];
        float dzG = stopExactPos[2] - bposG[2];
        float gapG = Math.Sqrt(dxG * dxG + dzG * dzG);
        float signedG = gapG;
        int piG = m_NextStopIndex - 1;
        while (piG > 0) {
            vector pvG = m_Config.Waypoints[piG].GetVector();
            float ddxG = stopExactPos[0] - pvG[0];
            float ddzG = stopExactPos[2] - pvG[2];
            if (ddxG * ddxG + ddzG * ddzG > 1.0) break;
            piG = piG - 1;
        }
        if (piG >= 0 && piG < m_NextStopIndex) {
            vector pwG = m_Config.Waypoints[piG].GetVector();
            float fxG = stopExactPos[0] - pwG[0];
            float fzG = stopExactPos[2] - pwG[2];
            float fnG = Math.Sqrt(fxG * fxG + fzG * fzG);
            if (fnG > 0.01) {
                fxG = fxG / fnG;
                fzG = fzG / fnG;
                signedG = dxG * fxG + dzG * fzG;
            }
        }
        float kmhG = bus.GetSpeedometerAbsolute();
        // [EndGap] (2026-07-29): EndpointGlideControl maneja los ultimos ~3m (DriveTowards ya no corre aca -> por eso
        // ai_run/drive_state/[CTL] cortaban a ~3m). Logueamos a que distancia del punto QUEDA Boris cuando para,
        // para medir la precision real del endpoint sin depender de acordarse donde estaba el marcador. kmhG<0.5=parado.
        if (kmhG < 0.5) BZBusLog.Info("[EndGap] parado a signed=" + signedG + "m (gap=" + gapG + "m) del punto wp" + m_NextStopIndex);
        if (signedG <= m_Config.EndpointGlideStopM) {
            if (!m_EndpointLatched) BZBusLog.Info("[GlideLatch] LLEGO signed=" + signedG + " gap=" + gapG + " kmh=" + kmhG);
            m_EndpointLatched = true;
            SetCachedInput(0, 0, 1.0);
            bus.SetHandbrake(1.0);
            return true;
        }
        // slope de la APROXIMACION: los 2m de terreno JUSTO ANTES del endpoint (NO muestrea pasado el punto, que en
        // bajada cae mas y sobre-estimaba -> frenaba de mas y paraba CORTO). Direccion = del wp previo (piG) al
        // endpoint. Estable, no depende del gap. Dosificada por EndpointSlopeBrakeGain. (2026-07-30, slope-aware v2)
        float slG = 0;
        if (piG >= 0 && piG < m_NextStopIndex) {
            vector apWpG = m_Config.Waypoints[piG].GetVector();
            float apdxG = stopExactPos[0] - apWpG[0];
            float apdzG = stopExactPos[2] - apWpG[2];
            float apLenG = Math.Sqrt(apdxG * apdxG + apdzG * apdzG);
            if (apLenG > 0.3) {
                float auxG = apdxG / apLenG;
                float auzG = apdzG / apLenG;
                float ysBG = GetGame().SurfaceY(stopExactPos[0] - auxG * 2.0, stopExactPos[2] - auzG * 2.0);
                float ysEG = GetGame().SurfaceY(stopExactPos[0], stopExactPos[2]);
                slG = (ysEG - ysBG) / 2.0 * m_Config.EndpointSlopeBrakeGain;
                if (slG > 0.5) slG = 0.5;
                if (slG < -0.5) slG = -0.5;
            }
        }
        // === IMAN DE DETENCION (2026-07-29) ===
        // Controlador de PARADA como ATRACTOR (Boris SIEMPRE llega corto por seguir la velocidad grabada -> el foco
        // es ATRAER; la contencion es red de seguridad que casi nunca dispara). Anclado a la POSICION:
        //   v_target(d) = sqrt(2*a*d)   (mismo 'a' que el SpeedLookahead -> continuidad con el cruise)
        // El InverseModel traduce el accel deseado al pedal POR VEHICULO (agnostic: cada auto calcula su llegada por
        // su fisica, sin recalibrar). Atrae si v<target (gas), contiene si v>target (freno). Reemplaza el bang-bang
        // freno/creep/coast del Freno Universal. Longitudinal-only: el volante lo sigue manejando el pure-pursuit
        // (por eso sirve tambien para PARAR EN CURVA). slG = pendiente al punto (>0 subida).
        float aMag = m_Config.EndpointApproachAccel;   // mismo 'a' gentil que la approach-coast -> continuidad + entra suave
        if (aMag <= 0) aMag = 0.6;
        // CARRY+SLAM (2026-07-30): si hay decel MEDIDA de la grabacion, el v_target usa ESA -> sqrt(2*decel*d) LLEVA
        // velocidad alta (Sedan 4.93 -> ~16km/h a 2m, como Sonom4n) en vez del glide gentil (aMag=1 -> 7km/h a 2m, que
        // dejaba a Boris frenando desde el ralenti donde el freno es debil). Los 5 humanos hacen carry+slam. Medido.
        if (m_Config.EndpointBrakeDecel > 0) aMag = m_Config.EndpointBrakeDecel;
        float vTargetKmhG = Math.Sqrt(2.0 * aMag * signedG) * 3.6;
        float thrG = 0;
        float brkG = 0;
        if (m_InverseModel) {
            float desiredAccelG = m_InverseModel.ComputeDesiredAccel(vTargetKmhG, kmhG, 0.1);
            vector posMg = m_Bus.GetPosition();
            string surfMg = "";
            GetGame().SurfaceGetType3D(posMg[0], posMg[1], posMg[2], surfMg);
            float frMg = m_InverseModel.GetSurfaceFriction(surfMg);
            float roMg = m_InverseModel.GetSurfaceRolling(surfMg);
            string noteMg = "";
            m_InverseModel.ComputeInputs(desiredAccelG, bus.GetGear(), kmhG, slG, frMg, roMg, thrG, brkG, noteMg);
        } else {
            // fallback sin InverseModel: proporcional simple hacia v_target
            float errG = vTargetKmhG - kmhG;
            if (errG > 0) thrG = Math.Clamp(errG * 0.05, 0, 0.5);
            else brkG = Math.Clamp(-errG * 0.05, 0, 0.85);
        }
        // FRENO PREDICTIVO vehicle-agnostic (2026-07-30): el perfil v_target (a=1.0) es suave y dejaba pasar velocidad
        // residual -> Boris cruzaba el punto con envion y el latch clavaba TARDE (medido: Sedan 64cm / camion 38cm de
        // sobrepaso). El falso-freno swappeado (bug de args) lo tapaba; al arreglarlo se destapo el sub-frenado. Regla
        // fisica: si a una desaceleracion FIRME (aStopG, que CUALQUIER masa supera) la distancia para parar ya alcanza
        // lo que falta al punto -> clavo el freno. Llega apenas CORTO y el break-away de abajo mete el ultimo tramo
        // (diseno de atractor). Reemplaza el apuntalamiento accidental con fisica de verdad. Ver [[project_endpoint_magnet]].
        float vMsG = kmhG / 3.6;
        float aStopG = m_Config.EndpointPredictBrakeAccel;
        if (aStopG <= 0) aStopG = 2.5;
        // el freno predictivo dispara a la MISMA decel medida que el carry -> slam en el punto exacto donde el humano
        // clavo (Boris trae velocidad alta, frena fuerte, para en el punto). Per-vehiculo, medido de la grabacion.
        if (m_Config.EndpointBrakeDecel > 0) aStopG = m_Config.EndpointBrakeDecel;
        float stopDistG = (vMsG * vMsG) / (2.0 * aStopG);
        // LOG DIAGNOSTICO (2026-07-31): ebd=EndpointBrakeDecel cargado; maxBrakeCfg=GetMaxBrakeDecel del config (asfalto)
        // -> probamos si el torque de freno del config PREDICE el pedal REAL de Boris (~1.5 Sedan). Si si, derivamos
        // per-vehiculo sin tunear. aStopG=el que usa el iman. v/signed p/medir la decel real de Boris del boris_native.
        float maxBrakeCfgG = 0;
        if (m_InverseModel) maxBrakeCfgG = m_InverseModel.GetMaxBrakeDecel("");
        if (signedG < 5.0) BZBusLog.Info("[CarrySlam] ebd=" + m_Config.EndpointBrakeDecel + " maxBrakeCfg=" + maxBrakeCfgG + " aStopG=" + aStopG + " v=" + kmhG + " signed=" + signedG);
        if (kmhG > 1.5 && stopDistG >= signedG) {
            brkG = 1.0;
            thrG = 0;
        }
        // ATRACCION (el iman que chupa): break-away desde parado/lento y todavia corto, escalado por PENDIENTE
        // (subida = iman MAS fuerte, vence gravedad+inercia estatica). Bajada: piso menor, la gravedad ayuda a rodar.
        if (kmhG < 1.5 && signedG > m_Config.CheckpointCloseTolM) {
            float floorThrG = 0.30 + slG * 2.0;
            if (floorThrG < 0.18) floorThrG = 0.18;
            if (floorThrG > 0.70) floorThrG = 0.70;
            if (thrG < floorThrG) { thrG = floorThrG; brkG = 0; }
        }
        // VOLANTE = rotar al HEADING GRABADO del endpoint (2026-07-29). El pure-pursuit se apoyaba en el CLUSTER de
        // waypoints del endpoint (tangente inestable) -> volanteaba/derivaba. El endpoint wp guarda targetHeading (el
        // rumbo que Sonom4n tenia al parar): estable y fiel. Rotamos hacia el mientras el iman avanza -> alineado + clava.
        // Signo derivado (HeadingErrTo = yaw - target): si Boris esta a la derecha del rumbo (err>0) gira a la izq.
        // Si saliera al reves, invertir el signo del knob. Longitudinal=iman, lateral=heading-align (ortogonales).
        float steerMg = 0;
        BZWaypoint epWpMg = m_Config.Waypoints[m_NextStopIndex];
        if (epWpMg && epWpMg.targetHeading != 0) {
            float hErrMg = HeadingErrTo(epWpMg.targetHeading);
            steerMg = -hErrMg * m_Config.EndpointHeadingSteerK;
            if (steerMg > 1.0)  steerMg = 1.0;
            if (steerMg < -1.0) steerMg = -1.0;
        }
        // FIX 2026-07-30 (test camion): la firma es SetCachedInput(throttle, STEERING, BRAKE).
        // Estaba SetCachedInput(thrG, brkG, steerMg) -> el heading-align (steerMg) se aplicaba como FRENO
        // y el freno como volante. Latente en el Sedan (llega alineado, steerMg~0) pero con el camion
        // desalineado (steerMg~0.40) el falso-freno peleaba el throttle -> el camion pesado se trababa 2.5m antes.
        SetCachedInput(thrG, steerMg, brkG);
        return true;
    }

    // OJO LONGITUDINAL: la velocidad que Boris puede tener AHORA y aun frenar comodo a lo que viene.
    // Escanea desde nearIdx una ventana = SU distancia de frenado (v^2/2a x margen, escala con la velocidad).
    // vAllow(wp) = sqrt(vWp^2 + 2*a*d). El MINIMO manda. El endpoint (vWp=0) sale como sqrt(2*a*d) = el freno
    // universal -> mismo cerebro para cruise, curva y parada. Devuelve km/h.
    private float ComputeLookaheadSpeed(int nearIdx, float kmh, vector busPos) {
        if (!m_Config || !m_Config.Waypoints) return -1.0;
        int cntLa = m_Config.Waypoints.Count();
        float aLa = m_Config.SpeedLookaheadAccel;
        if (aLa <= 0) aLa = 2.0;
        float vNowMs = kmh / 3.6;
        float winLa = (vNowMs * vNowMs) / (2.0 * aLa) * m_Config.SpeedLookaheadMargin;
        if (winLa < m_Config.SpeedLookaheadMinM) winLa = m_Config.SpeedLookaheadMinM;
        if (winLa > 250.0) winLa = 250.0;
        float vMinMs = 100000.0;
        float dLa = 0;
        vector prevLa = busPos;
        // El ojo NO mira mas alla del final de SU TRAMO: su objetivo es el intercambio, no lo que viene
        // despues. Sin esto barria wps de la pierna siguiente, que estan encima en el espacio (0.01-0.08 m).
        int hiLa = cntLa - 1;
        if (m_LegEnd > nearIdx && m_LegEnd < hiLa) hiLa = m_LegEnd;
        for (int jLa = nearIdx; jLa <= hiLa; jLa++) {
            vector wvLa = m_Config.Waypoints[jLa].GetVector();
            dLa = dLa + vector.Distance(prevLa, wvLa);
            prevLa = wvLa;
            float vWpMs = m_Config.Waypoints[jLa].targetSpeed / 3.6;
            float vAllowMs = Math.Sqrt(vWpMs * vWpMs + 2.0 * aLa * dLa);
            if (vAllowMs < vMinMs) vMinMs = vAllowMs;
            if (dLa > winLa) break;
        }
        return vMinMs * 3.6;
    }

    // CREEP DE REVERSA AL INTERCAMBIO (2026-07-22, Sonom4n). En reversa el control sigue la traza con el EJE
    // TRASERO (corridorRefPos ~1,25m detras del origen, por estabilidad de direccion), pero el wp se GRABO
    // con el ORIGEN (GetPosition) -> al frenar el origen queda ~media-distancia-entre-ejes corto; y el
    // StopBrake ademas frena conservador y para ~4m antes de wp185. El ORIGEN nunca clava el punto: la
    // reversa cierra corta y el forward arranca desde el cusp (Boris "se detiene en pleno reverse y prosigue
    // derecho": el indice no avanza porque quedo atras del arranque del tramo). FIX: cuando el StopBrake ya
    // lo freno (casi parado) y el ORIGEN sigue corto, empuja REVERSA suave para llevar el ORIGEN a wp185 --
    // apuntando al MISMO punto que uso la grabacion (asi compensa el eje trasero, sin offset separado).
    // Distancia con SIGNO proyectada sobre el rumbo de la reversa: >StopM = falta reversar; <=StopM = clavar.
    // Extraido a funcion aparte por el limite de instrucciones de DriveTowards. Setea m_RcActive/m_RcThr/m_RcBrk.
    private bool  m_RcActive;
    private float m_RcThr;
    private float m_RcBrk;
    // PENDIENTE REAL DEL TERRENO ENTRE DOS PUNTOS (2026-07-22, Sonom4n: "la inclinacion es fundamental, de la
    // grabacion al runtime, en todo momento -- para freno y trayectoria"). Grade = rise/run GEOMETRICO,
    // terreno-vs-terreno (SurfaceY), EXACTO y siempre disponible; no depende del pitch del vehiculo (que
    // laggea/subestima) ni de la direccion de marcha. >0 = de 'from' a 'to' se SUBE. Acotado a +-0.5.
    private float SlopeToPoint(vector fromPos, vector toPos) {
        float dxSp = toPos[0] - fromPos[0];
        float dzSp = toPos[2] - fromPos[2];
        float runSp = Math.Sqrt(dxSp * dxSp + dzSp * dzSp);   // distancia HORIZONTAL (run)
        if (runSp < 1.0) return 0;
        float sSp = (GetGame().SurfaceY(toPos[0], toPos[2]) - GetGame().SurfaceY(fromPos[0], fromPos[2])) / runSp;
        if (sSp > 0.5) sSp = 0.5;
        if (sSp < -0.5) sSp = -0.5;
        return sSp;
    }
    // CERCA DE UN CHECKPOINT: distancia al FIN DE TRAMO (m_LegEnd = SIEMPRE el checkpoint, a diferencia de
    // m_NextStopIndex que podia no apuntar ahi). Dentro de 18m, el StopBrake slope-aware es el UNICO
    // controlador longitudinal (se apagan coastBand/revApproach/EndpointGlide que capean el gas -> arrastre).
    // Extraido de DriveTowards por el limite de instrucciones (2026-07-22).
    private bool IsNearCheckpoint(vector busPos) {
        if (!m_Config || !m_Config.StopBrakeControllerEnabled) return false;
        if (!m_LegInit || m_LegEnd < 0 || m_LegEnd >= m_Config.Waypoints.Count()) return false;
        return vector.Distance(busPos, m_Config.Waypoints[m_LegEnd].GetVector()) < 18.0;
    }
    // PUNTO GRABADO MAS CERCANO A BORIS (position-synced): m_WaypointIndex corre ~15m adelante (WAYPOINT_RADIUS)
    // -> en curva la referencia seria de OTRO punto -> distorsiona. Los correctores comparan contra el punto de
    // la posicion REAL de Boris (reactivo, tiempo real, como pidio Sonom4n). Ventana acotada alrededor del indice.
    private int NearestRecordedWp(vector busPos) {
        if (!m_Config || !m_Config.Waypoints) return -1;
        int loNw = m_WaypointIndex - 30;
        if (loNw < 0) loNw = 0;
        int hiNw = m_WaypointIndex + 2;
        if (hiNw > m_Config.Waypoints.Count() - 1) hiNw = m_Config.Waypoints.Count() - 1;
        int nearNw = m_WaypointIndex;
        float bestNw = 1000000000.0;
        for (int iNw = loNw; iNw <= hiNw; iNw++) {
            vector wvNw = m_Config.Waypoints[iNw].GetVector();
            float dxNw = busPos[0] - wvNw[0];
            float dzNw = busPos[2] - wvNw[2];
            float dsNw = dxNw * dxNw + dzNw * dzNw;
            if (dsNw < bestNw) { bestNw = dsNw; nearNw = iNw; }
        }
        return nearNw;
    }
    // CORRECTOR DE VELOCIDAD contra la TOMA (2026-07-23, Sonom4n). En TODO momento (incluso cruise): compara la
    // velocidad de Boris con la GRABADA (targetSpeed del punto real de Boris) y corrige PROPORCIONAL y con RAMPA
    // (anti-traqueteo): se pasa -> un poquito de freno; va lento -> un poquito de gas. Setea m_ScThr/m_ScBrk.
    private float m_ScThr;
    private float m_ScBrk;
    private float m_ScBrkPrev;
    private float m_ScThrPrev;
    private bool  m_ScActive;
    private void ComputeSpeedCorrector(float kmh, vector busPos) {
        m_ScActive = false; m_ScThr = 0; m_ScBrk = 0;
        if (!m_Config || !m_Config.SpeedCorrectorEnabled) { m_ScBrkPrev = 0; m_ScThrPrev = 0; return; }
        int nearVc = NearestRecordedWp(busPos);
        if (nearVc < 0) { m_ScBrkPrev = 0; m_ScThrPrev = 0; return; }
        float refKmh = m_Config.Waypoints[nearVc].targetSpeed;
        float tgtBrk = 0;
        float tgtThr = 0;
        if (refKmh >= 1.0) {   // en paradas (cota~0) no: el checkpoint maneja el freno de llegada
            float errVc = kmh - refKmh;   // >0 muy rapido, <0 muy lento
            if (errVc > m_Config.SpeedCorrectorDeadKmh) {
                tgtBrk = (errVc - m_Config.SpeedCorrectorDeadKmh) * m_Config.SpeedCorrectorBrakeGain;
                if (tgtBrk > m_Config.SpeedCorrectorBrakeCap) tgtBrk = m_Config.SpeedCorrectorBrakeCap;
            } else if (errVc < -m_Config.SpeedCorrectorDeadKmh) {
                tgtThr = (-errVc - m_Config.SpeedCorrectorDeadKmh) * m_Config.SpeedCorrectorThrGain;
                if (tgtThr > m_Config.SpeedCorrectorThrCap) tgtThr = m_Config.SpeedCorrectorThrCap;
            }
        }
        // RAMPA (anti-traqueteo): el freno y el gas suben/bajan como MUCHO RateMax por tick -> nunca salta.
        float rM = m_Config.SpeedCorrectorRateMax;
        if (tgtBrk > m_ScBrkPrev + rM) tgtBrk = m_ScBrkPrev + rM;
        if (tgtBrk < m_ScBrkPrev - rM) tgtBrk = m_ScBrkPrev - rM;
        if (tgtThr > m_ScThrPrev + rM) tgtThr = m_ScThrPrev + rM;
        if (tgtThr < m_ScThrPrev - rM) tgtThr = m_ScThrPrev - rM;
        if (tgtBrk < 0) tgtBrk = 0;
        if (tgtThr < 0) tgtThr = 0;
        m_ScBrkPrev = tgtBrk; m_ScThrPrev = tgtThr;
        m_ScBrk = tgtBrk; m_ScThr = tgtThr; m_ScActive = true;
    }
    // CORRECTOR DE VOLANTE contra la TOMA (2026-07-22, Sonom4n). Compara el comando de volante de Boris con el
    // GRABADO (targetFrontWheel del humano en el punto proyectado) y, si se desvia mas que el margen, da un
    // nudge INTERMITENTE hacia el grabado. Solo baja velocidad (no cruise) y ON-LINE. Forward Y reversa.
    private float ComputeSteeringCorrector(float steering, vector busPos, float kmh) {
        if (!m_Config || !m_Config.SteerCorrectorEnabled) return steering;
        if (ActiveLegIsReverse()) return steering;   // FORWARD-ONLY: la reversa anda y es delicada, no se toca
        if (kmh > m_Config.SteerCorrectorMaxKmh) return steering;   // cruise: no tocar lo validado
        float gainSc = GetPlantSteerGain();
        if (gainSc <= 0.001) return steering;
        int nearSc = NearestRecordedWp(busPos);
        if (nearSc < 0) return steering;
        // SOLO SI BORIS ESTA SOBRE LA LINEA (2026-07-22, Sonom4n): offset PERPENDICULAR a la traza (no la distancia
        // al wp, que en el hueco del cusp es grande aunque el lateral sea chico -> no corregia el volante en el
        // arranque de reversa). Si esta lejos LATERALMENTE, el volante DEBE corregir la posicion -> no lo forzamos
        // (sino queda paralelo pero afuera). El corrector ataca desvios de volante ESPURIOS (on-line + volante torcido).
        int aSeg = nearSc;
        int bSeg = nearSc + 1;
        if (bSeg >= m_Config.Waypoints.Count()) { aSeg = nearSc - 1; bSeg = nearSc; }
        if (aSeg < 0) aSeg = 0;
        vector paSeg = m_Config.Waypoints[aSeg].GetVector();
        vector pbSeg = m_Config.Waypoints[bSeg].GetVector();
        float sdxSeg = pbSeg[0] - paSeg[0];
        float sdzSeg = pbSeg[2] - paSeg[2];
        float slenSeg = Math.Sqrt(sdxSeg * sdxSeg + sdzSeg * sdzSeg);
        if (slenSeg < 0.01) return steering;
        float latOff = Math.AbsFloat((busPos[0] - paSeg[0]) * sdzSeg - (busPos[2] - paSeg[2]) * sdxSeg) / slenSeg;
        if (latOff > m_Config.SteerCorrectorOnLineM) return steering;
        float refSteer = m_Config.Waypoints[nearSc].targetFrontWheel / gainSc;   // volante GRABADO en el punto de Boris
        if (refSteer > 1.0)  refSteer = 1.0;
        if (refSteer < -1.0) refSteer = -1.0;
        if (Math.AbsFloat(steering - refSteer) < m_Config.SteerCorrectorThresh) return steering;   // dentro del margen
        return steering + (refSteer - steering) * m_Config.SteerCorrectorGain;   // nudge hacia el grabado
    }
    private void ComputeReverseCreep(vector busPos, float kmh) {
        m_RcActive = false;
        m_RevCreepShort = false;
        m_CheckpointHold = false;
        // SOLO REVERSA (2026-07-22, re-revertido): la unificacion a forward DEADLOCKEA interc1 -> Boris queda
        // clavado con el volante girado empujando (gear reverse, gas 0.45, no avanza). El creep bloquea el
        // cierre y Boris no llega al punto (la aproximacion forward tiene su propia dinamica). Forward/endpoint
        // los maneja EndpointGlide. El corrector de CONTROLES (volante+pedal, intermitente) sera la via forward.
        if (!m_Config || !m_Config.ReverseEndpointCreep || !m_Config.EndpointGlide || !ActiveLegIsReverse()) return;
        if (m_LegEnd < 1 || m_LegEnd >= m_Config.Waypoints.Count()) return;
        vector wpEndRc = m_Config.Waypoints[m_LegEnd].GetVector();
        vector prevRc  = m_Config.Waypoints[m_LegEnd - 1].GetVector();
        float dirxRc = wpEndRc[0] - prevRc[0];
        float dirzRc = wpEndRc[2] - prevRc[2];
        float dirnRc = Math.Sqrt(dirxRc * dirxRc + dirzRc * dirzRc);
        if (dirnRc <= 0.05) return;
        dirxRc = dirxRc / dirnRc;
        dirzRc = dirzRc / dirnRc;
        // distancia con SIGNO del ORIGEN al punto sobre el rumbo de la reversa: >0 = falta reversar.
        float signedRc = (wpEndRc[0] - busPos[0]) * dirxRc + (wpEndRc[2] - busPos[2]) * dirzRc;
        if (signedRc >= 6.0) return;   // todavia lejos: fuera de la zona del checkpoint (StopBrake maneja)
        float gradeRc = SlopeToPoint(busPos, wpEndRc);
        // TOLERANCIA ESCALADA POR PENDIENTE (Sonom4n): en subida fuerte Boris no puede trepar el ultimo tramo desde
        // casi-parado -> aceptamos la aproximacion que el terreno PERMITE (mas grande cuanto mas inclinado),
        // asi clava ahi y NO oscila (antes: trepa->toca 0.4->cierra->el forward lo dispara).
        float effTolRc = m_Config.CheckpointCloseTolM + m_Config.CheckpointSlopeTolFactor * Math.AbsFloat(gradeRc);
        if (signedRc <= effTolRc) {
            // LLEGO (dentro de la tolerancia del terreno) -> HILL-HOLD: freno para que la GRAVEDAD NO LO
            // DESLICE (Sonom4n: "al frenar suelta freno, por gravedad se desliza"). Deja cerrar el tramo
            // (m_RevCreepShort queda false) -> el forward arranca y libera el freno con su propio throttle.
            m_RcThr = 0; m_RcBrk = 1.0; m_RcActive = true;
            m_CheckpointHold = true;   // HILL-HOLD: DriveTowards mete handbrake -> no se desliza en la pendiente
            // CHECKPOINT SNAP: descartado (CheckpointSnapEnabled=false). Codigo queda; StartCheckpointSnap retorna.
            StartCheckpointSnap(wpEndRc, m_Config.Waypoints[m_LegEnd].targetHeading);
            return;
        }
        // TODAVIA CORTO -> BLOQUEA EL CIERRE SIEMPRE (aunque el creep lo haya acelerado > la ventana; antes
        // se desenganchaba a >4km/h y el tramo cerraba temprano a ~1.1m). Empuja REVERSA con fuerza
        // proporcional SOLO si va lento, para no acelerar de mas y pasarse la ventana.
        m_RevCreepShort = true;
        if (kmh < 1.8) {
            // FUERZA PROPORCIONAL A LA PENDIENTE, per-vehiculo (Sonom4n, MEDIDO en runtime): throttle =
            // ZonaMuerta + g*sin(theta)/a_full + margen. Reusa gradeRc (grade geometrico exacto al punto).
            float gradeUp = gradeRc;
            if (gradeUp < 0) gradeUp = 0;   // solo la subida pide fuerza; en bajada la gravedad ayuda
            float creepRc = m_Config.StopBrakeDeadzone + (9.81 * gradeUp) / m_Config.StopBrakeAFull + 0.08;
            if (creepRc < 0.18) creepRc = 0.18;
            if (creepRc > 0.98) creepRc = 0.98;
            m_RcThr = creepRc; m_RcBrk = 0; m_RcActive = true;
        }
        // else: va rapido -> no empuja (StopBrake/coast lo frena), pero m_RevCreepShort mantiene el tramo abierto
    }

    // ===== CHECKPOINT SNAP CINEMATICO (2026-07-22, Sonom4n) =====
    // La fisica llega al vecindario del punto pero el motor mete ~0.15-0.42m de ruido (medido: 2 runs mismo
    // build). Para las poses de precision, un deslizamiento CINEMATICO frame-by-frame (30Hz) completa el
    // residuo a la pose EXACTA grabada en CheckpointSnapSecs con ease-in-out -> a velocidad de creep, invisible.
    // Reusa el patron del mode-entry snap (SetPosition + SetOrientation + matar velocidad con impulso -m*v).
    private bool   m_SnapActive;
    private float  m_SnapT0;
    private vector m_SnapFrom;
    private vector m_SnapTo;
    private float  m_SnapYawFrom;
    private float  m_SnapYawTo;
    private void StartCheckpointSnap(vector targetPos, float targetYawDeg) {
        if (!m_Config || !m_Config.CheckpointSnapEnabled || m_SnapActive || !m_Bus) return;
        vector p = m_Bus.GetPosition();
        float dxs = targetPos[0] - p[0];
        float dzs = targetPos[2] - p[2];
        float residual = Math.Sqrt(dxs * dxs + dzs * dzs);
        if (residual > m_Config.CheckpointSnapCapM || residual < 0.03) return;   // fuera de cap o ya clavado
        m_SnapFrom = p;
        m_SnapTo = targetPos;
        m_SnapYawFrom = m_Bus.GetOrientation()[0];
        // camino corto del yaw (-180..180)
        float dyaw = targetYawDeg - m_SnapYawFrom;
        while (dyaw > 180.0)  dyaw -= 360.0;
        while (dyaw < -180.0) dyaw += 360.0;
        m_SnapYawTo = m_SnapYawFrom + dyaw;
        m_SnapT0 = GetGame().GetTickTime();
        m_SnapActive = true;
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckpointSnapTick);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckpointSnapTick, 33, true);
        BZBusLog.Info("[SNAP] checkpoint: residuo " + residual + "m -> deslizo cinematico a la pose grabada en " + m_Config.CheckpointSnapSecs + "s");
    }
    void CheckpointSnapTick() {
        if (!m_SnapActive || !m_Bus) { GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckpointSnapTick); return; }
        float dur = m_Config.CheckpointSnapSecs;
        if (dur < 0.1) dur = 0.1;
        float t = (GetGame().GetTickTime() - m_SnapT0) / dur;
        if (t > 1.0) t = 1.0;
        float e = t * t * (3.0 - 2.0 * t);   // smoothstep: ease-in-out, arranca y termina con velocidad 0
        vector pos = m_SnapFrom + (m_SnapTo - m_SnapFrom) * e;
        float yaw = m_SnapYawFrom + (m_SnapYawTo - m_SnapYawFrom) * e;
        m_Bus.SetPosition(pos);
        vector ori = m_Bus.GetOrientation();   // conserva pitch/roll (la inclinacion del terreno); solo cambia yaw
        ori[0] = yaw;
        m_Bus.SetOrientation(ori);
        vector vsnap = GetVelocity(m_Bus);     // matar velocidad para que la fisica no arrastre (patron mode-entry)
        float msnap = dBodyGetMass(m_Bus);
        if (msnap > 0) dBodyApplyImpulse(m_Bus, vsnap * (-msnap));
        if (t >= 1.0) {
            m_Bus.SetPosition(m_SnapTo);
            ori[0] = m_SnapYawTo;
            m_Bus.SetOrientation(ori);
            m_SnapActive = false;
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckpointSnapTick);
            BZBusLog.Info("[SNAP] pose EXACTA clavada");
        }
    }

    // APROXIMACION A LA TRANSICION REVERSE (extraido de DriveTowards por el limite de instrucciones).
    // Frena por FISICA a la distancia REAL del punto de transicion fwd->reverse. Setea m_RaActive/m_RaThr/m_RaBrk.
    private bool  m_RaActive;
    private float m_RaThr;
    private float m_RaBrk;
    private void ComputeReverseApproach(vector busPos, float kmh) {
        m_RaActive = false;
        if (!m_Config || !m_Config.Waypoints) return;
        int cntRa = m_Config.Waypoints.Count();
        int loRa = m_WaypointIndex - 40;
        if (loRa < 0) loRa = 0;
        // NO MIRAR ATRAS DEL TRAMO ACTIVO (2026-07-22, MEDIDO). En el intercambio la reversa recien
        // completada (wps ~180-184) queda ENCIMADA en el espacio con el arranque del forward (186+).
        // El nearest-search de 40 wps atras agarraba uno de esos wps 'reverse' -> el escaneo adelante
        // encontraba mode=="reverse" al toque -> ComputeReverseApproach FRENABA en pleno forward (no hay
        // reversa por delante) -> Boris se trababa saliendo del intercambio y saltaba el AUTO-RECOVERY.
        if (m_LegInit && loRa < m_LegStart) loRa = m_LegStart;
        int hiRa = m_WaypointIndex + 5;
        if (hiRa > cntRa - 1) hiRa = cntRa - 1;
        int nearRa = m_WaypointIndex;
        float bestRa = 1000000000.0;
        for (int qRa = loRa; qRa <= hiRa; qRa++) {
            vector wvRa = m_Config.Waypoints[qRa].GetVector();
            float dxq = busPos[0] - wvRa[0];
            float dzq = busPos[2] - wvRa[2];
            float dsq = dxq * dxq + dzq * dzq;
            if (dsq < bestRa) {
                bestRa = dsq;
                nearRa = qRa;
            }
        }
        float distRa = Math.Sqrt(bestRa);
        bool foundRa = false;
        vector prevRa = m_Config.Waypoints[nearRa].GetVector();
        for (int jRa = nearRa; jRa < cntRa; jRa++) {
            vector wvj = m_Config.Waypoints[jRa].GetVector();
            distRa = distRa + vector.Distance(prevRa, wvj);
            prevRa = wvj;
            if (m_Config.Waypoints[jRa].mode == "reverse") { foundRa = true; break; }
            if (distRa > m_Config.ReverseApproachWindowM) break;
        }
        if (!foundRa) return;
        float dRa = distRa;
        if (dRa < 0.3) dRa = 0.3;
        float vMsRa = kmh / 3.6;
        float bDecRa = m_Config.BrakeDecelMS;
        if (bDecRa <= 0) bDecRa = 7.0;
        // NO frenar a cero: frenar por fisica para LLEGAR a GateKmh EN el punto (v^2 - vGate^2 = 2 a d). Boris
        // cruza el gate (<1.5) moviendose y engancha reversa fluido -> sin dead-stop ni gateo a 1 km/h.
        float vGateMs = m_Config.ReverseApproachGateKmh / 3.6;
        float aRa = (vMsRa * vMsRa - vGateMs * vGateMs) / (2.0 * dRa);
        float bRa = aRa / bDecRa;
        if (bRa > 1.0) bRa = 1.0;
        m_RaActive = true;
        ComputeGlideGuard(kmh, dRa, busPos);                                 // "si suelto ahora, llego al intercambio?"
        if (bRa > 0.03) { m_RaThr = 0; m_RaBrk = bRa; }                                             // dentro de ~4m -> freno fisico a la puerta (llega a GateKmh moviendose)
        else if (m_GgActive) { m_RaThr = m_GgThr; m_RaBrk = 0; }                                    // el planeo MEDIDO no cubre lo que falta -> sostener
        else if (kmh < m_Config.ReverseApproachHoldKmh) { m_RaThr = m_Config.ReverseApproachNudge; m_RaBrk = 0; } // respaldo mientras el envelope no aprendio
        else { m_RaThr = 0; m_RaBrk = 0; }                                                          // llega planeando: coast
    }

    // ---- APRENDIZAJE DE LA RESPUESTA LONGITUDINAL (aspecto 2 del envelope, 2026-07-20) ----
    // Compara la velocidad de este tick con la del anterior CUANDO el anterior fue rodar sin gas ni
    // freno => esa caida ES la decel de coasteo real del vehiculo. Se mide sola manejando, sin test.
    private float m_CoPrevKmh;
    private float m_CoPrevTime;
    private bool  m_CoPrevIdle;

    // PISO bajo el auto: 0 = duro (asfalto/concreto/grava compacta), 1 = blando (tierra, pasto, campo).
    // Sonom4n: "en cruise no hay gran diferencia, pero en parking o intercambio a baja velocidad si afecta"
    // -> a baja velocidad manda la rodadura y ahi el piso ES la diferencia.
    private int SurfaceClassAt(vector pos) {
        string stName;
        GetGame().SurfaceGetType(pos[0], pos[2], stName);
        stName.ToLower();
        if (stName.Contains("asphalt")) return 0;
        if (stName.Contains("concrete")) return 0;
        if (stName.Contains("tarmac")) return 0;
        if (stName.Contains("road")) return 0;
        if (stName.Contains("stone")) return 0;
        if (stName.Contains("rock")) return 0;
        return 1;
    }

    private int    m_CoSamples;     // diagnostico: muestras de coasteo aceptadas
    private int    m_CoIdleTicks;   // diagnostico: ticks que quedaron rodando sin gas ni freno
    private string m_CoDbgClass;    // diagnostico: la clase que efectivamente se esta usando de clave

    // ---- MEDICION PASIVA DE LA ZONA MUERTA (aspecto 3 del envelope, 2026-07-21) ------------------
    // Cada vez que Boris pasa de DETENIDO a MOVERSE, el acelerador que tenia puesto en ese instante ES
    // su zona muerta (breakaway) para ese piso. No hace falta ninguna rutina especial: se mide sola en
    // cada arranque, que en una toma con paradas pasa varias veces. Antes dependia de la rampa, que casi
    // nunca disparaba (20/07: cero muestras en toda una corrida).
    private float m_BkPrevKmh;
    private float m_BkPrevThr;

    private void LearnBreakaway(float kmh, vector pos) {
        if (!m_Config || m_Config.VehicleClass == "") return;
        // INVALIDO Y DESCARTADO (2026-07-21, MEDIDO). Anotar "el gas que habia cuando se movio" NO mide el
        // umbral: mide una COTA SUPERIOR. Si el control pedia 0.9 y el auto arrancaba con 0.3, anotaba 0.9.
        // Resultado: "despego con 0.86 / 0.94 / 0.99", promedio 0.82 -> la inversa empujaba TODO comando
        // lento a >=0.82 y Boris salia disparado en cada correccion (dio dos vueltas enteras alrededor del
        // primer intercambio). El breakaway SOLO se mide SUBIENDO desde abajo hasta que se mueve: eso lo
        // hace BreakawayRamp, que es la unica fuente valida de muestras.
        m_BkPrevKmh = kmh;
        m_BkPrevThr = m_CachedThrottle;
    }

    // INVERSA DE ZONA MUERTA. Si el control pide gas, se lo desplaza por encima del umbral de despegue
    // MEDIDO del vehiculo, con rampa suave cerca del cero (lc) para no dar un salto. Es la tecnica que la
    // literatura senala contra el bang-bang: mantiene el actuador en movimiento aun con error chico porque
    // empuja la senal mas alla de la zona muerta. Si todavia no aprendio, NO toca nada.
    private float DeadZoneInverse(float u, vector pos) {
        if (u <= 0.001) return u;
        if (!m_Config || m_Config.VehicleClass == "") return u;
        float dz = BZVehicleEnvelope.Get(m_Config.VehicleClass).GetBreakaway(SurfaceClassAt(pos));
        if (dz <= 0.0) return u;
        float lc = m_Config.DeadZoneSmoothW;
        if (lc < 0.01) lc = 0.01;
        float f = u / lc;
        if (f > 1.0) f = 1.0;
        float outU = dz * f + (1.0 - dz) * u;
        if (outU > 1.0) outU = 1.0;
        return outU;
    }

    private void LearnCoast(float kmh, bool idleNow, vector pos) {
        float nowCo = GetGame().GetTime() / 1000.0;
        if (idleNow) m_CoIdleTicks++;
        if (m_Config) m_CoDbgClass = m_Config.VehicleClass;
        if (m_CoPrevIdle && m_CoPrevTime > 0 && m_Config && m_Config.VehicleClass != "") {
            float dtCo = nowCo - m_CoPrevTime;
            if (dtCo > 0.15 && dtCo < 1.5) {
                float dvCo = (m_CoPrevKmh - kmh) / 3.6;
                if (dvCo > 0) {
                    BZVehicleEnvelope.Get(m_Config.VehicleClass).SampleCoast(m_CoPrevKmh, dvCo / dtCo, SurfaceClassAt(pos));
                    m_CoSamples++;
                }
            }
        }
        m_CoPrevKmh  = kmh;
        m_CoPrevTime = nowCo;
        m_CoPrevIdle = idleNow;
    }

    // ---- ANTI-PLANTADA: "si suelto ahora, llego al punto?" -------------------------------------
    // dStop = metros que faltan hasta el punto donde hay que llegar. Si el planeo MEDIDO del vehiculo
    // no cubre esa distancia, coastear termina en plantada corto -> sostener gas. Si todavia no
    // aprendio el coasteo devuelve false y no interviene (comportamiento previo intacto).
    private bool  m_GgActive;
    private float m_GgThr;

    private void ComputeGlideGuard(float kmh, float dStop, vector pos) {
        m_GgActive = false;
        if (!m_Config || !m_Config.CoastGuardEnabled) return;
        if (m_Config.VehicleClass == "" || dStop <= 0.5 || kmh < 0.5) return;
        // BAJADA (2026-07-26, SEQ1 endpoint tras curva): en bajada la GRAVEDAD extiende el planeo -> Boris llega
        // coasteando sin plantarse. Pero GlideDistanceM esta medido ~llano y SUBESTIMA el planeo cuesta abajo ->
        // el CoastGuard creia que se quedaba corto y metia CoastGuardThrottle, que en bajada SOBREacelera y
        // dispara el serrucho de la aproximacion (medido con el tracer [CTL]: coastGuard[t22] pisaba el coast).
        // Si el tramo hasta el punto BAJA, no intervenir: la gravedad ya carga el planeo, que coastee y el
        // freno universal del endpoint modula. Solo se corta en bajada real (>~1.7 grados).
        if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            if (SlopeToPoint(pos, m_Config.Waypoints[m_NextStopIndex].GetVector()) < -0.03) return;
        }
        float glideM = BZVehicleEnvelope.Get(m_Config.VehicleClass).GlideDistanceM(kmh, SurfaceClassAt(pos));
        if (glideM <= 0) return;
        if (dStop <= glideM * m_Config.CoastGuardMargin) return;
        m_GgActive = true;
        m_GgThr = m_Config.CoastGuardThrottle;
    }

    // ---- GATE DE POSE: alinear antes de dar por cumplida la transicion ----------------------------
    private int   m_RevAlignTicks;   // ticks esperando terminar el giro antes de meter reversa
    private bool  m_PoseGateActive;
    private int   m_PoseGateTicks;
    private float m_PoseGateErr;

    // error angular (deg, -180..180) entre el heading ACTUAL del bus y un heading objetivo.
    private float HeadingErrTo(float targetDeg) {
        if (!m_Bus) return 0;
        float e = m_Bus.GetOrientation()[0] - targetDeg;
        while (e > 180.0)  e = e - 360.0;
        while (e < -180.0) e = e + 360.0;
        return e;
    }

    // Mientras el gate de pose esta activo, Boris ROTA en reversa hacia el heading de salida en vez de
    // seguir viaje torcido. El signo del volante va invertido porque en marcha atras el tren delantero
    // rota el auto al reves que yendo hacia adelante.
    private void ApplyPoseAlign(out float throttle, out float brake, out float steering) {
        float k = m_Config.PoseGateSteerK * m_PoseGateErr;
        if (k > 1.0)  k = 1.0;
        if (k < -1.0) k = -1.0;
        steering = k;
        throttle = m_Config.PoseGateThrottle;
        brake = 0;
    }

    // ---- ZONA MUERTA: rampa de despegue (aspecto 3 del envelope, 2026-07-20) ----------------------
    // Si Boris esta DETENIDO y todavia le falta llegar al punto, subimos el acelerador de a poco hasta
    // que se mueve. Esa misma rampa (a) lo desatasca y (b) MIDE el acelerador de despegue del vehiculo
    // en ese piso. Es la identificacion clasica de breakaway: el actuador que mide es el que mueve.
    // Reemplaza mis 4 constantes inventadas (0.20/0.22/0.30/0.40), que fallaron todas.
    private float m_RampThr;
    private bool  m_RampOn;

    private float BreakawayRamp(vector pos, float kmh) {
        if (kmh > 1.0) {
            if (m_RampOn && m_RampThr > 0.02) {
                // arranco: esta es la medicion del despegue para este piso
                BZVehicleEnvelope.Get(m_Config.VehicleClass).SampleBreakaway(m_RampThr, SurfaceClassAt(pos));
                BZBusLog.Info("[BREAKAWAY] " + m_Config.VehicleClass + ": despego con thr=" + m_RampThr + " piso=" + SurfaceClassAt(pos));
            }
            m_RampOn = false;
            m_RampThr = 0;
            return -1.0;
        }
        if (!m_RampOn) {
            m_RampOn = true;
            // arrancar desde lo ya aprendido (si existe) para no repetir toda la rampa cada vez
            float known = BZVehicleEnvelope.Get(m_Config.VehicleClass).GetBreakaway(SurfaceClassAt(pos));
            if (known > 0) m_RampThr = known * 0.8;
            else m_RampThr = m_Config.BreakawayRampStart;
        }
        m_RampThr = m_RampThr + m_Config.BreakawayRampStep;
        if (m_RampThr > m_Config.BreakawayRampMax) m_RampThr = m_Config.BreakawayRampMax;
        return m_RampThr;
    }

    // ---- CALIBRACION DE ZONA MUERTA (aspecto 3 del envelope, 2026-07-21) --------------------------
    // Rutina explicita, al estilo del donut de full-lock que mide el radio minimo: parado en llano, el
    // acelerador SUBE DESDE CERO de a poco hasta que el auto se mueve. ESE es el umbral de despegue real.
    // Se corre UNA VEZ por vehiculo+piso, antes de arrancar la ruta, y queda persistido en el envelope.
    // (Medir "el gas que habia cuando arranco" NO sirve: es cota superior, dio 0.82 falso el 21/07.)
    private int   m_CalState;    // 0=sin empezar  1=rampa subiendo  2=frenando entre muestras  3=listo
    private float m_CalThr;
    private int   m_CalGot;
    private float m_CalWait;

    // devuelve true si esta calibrando (el tick NO debe manejar la ruta)
    private bool CalibrateBreakaway(Car bus) {
        if (!m_Config || !m_Config.CalibBreakawayEnabled) return false;
        if (m_CalState == 3) return false;
        int surfCal = SurfaceClassAt(bus.GetPosition());
        if (m_CalState == 0) {
            // ya medido antes? entonces no perdemos tiempo
            if (BZVehicleEnvelope.Get(m_Config.VehicleClass).GetBreakaway(surfCal) > 0) { m_CalState = 3; return false; }
            // ESPERAR A QUE EL VEHICULO ESTE LISTO (2026-07-21, MEDIDO). La 1a version arrancaba a medir
            // apenas spawneaba y abortaba con "no se movio ni a fondo": el motor todavia no estaba en
            // marcha y el hold de spawn seguia corriendo. Medir contra un auto que no puede moverse no
            // da un umbral alto: da basura.
            // NO BLOQUEAR EL TICK MIENTRAS ESPERA (2026-07-21, MEDIDO). La 1a version devolvia true aca,
            // y el caller hace `return` -> el tick ENTERO no corria durante 13 s. El boarding de Boris
            // pasa justo en esa ventana: quedaba en "fase 1", no completaba, y el watchdog despawneaba
            // el vehiculo a los 6,6 s ("m_Bus perdido en wp 0"). Le pasaba a los dos vanilla. Durante la
            // espera solo contamos: el tick sigue corriendo normal.
            if (m_TickCount < m_Config.CalibBreakawayWaitTicks) return false;
            if (bus.CrewMember(0) == null) return false;      // todavia no subio: no calibrar ni bloquear
            if (!bus.EngineIsOn()) { bus.EngineStart(); return false; }
            m_CalState = 1; m_CalThr = 0; m_CalGot = 0;
            BZBusLog.Info("[CALIB] midiendo zona muerta de " + m_Config.VehicleClass + " (subiendo el gas desde 0)");
        }
        SetCachedHandbrake(0);   // el freno de mano invalida la medicion
        float vCal = bus.GetSpeedometerAbsolute();
        if (m_CalState == 1) {
            if (vCal > 1.0) {
                BZVehicleEnvelope.Get(m_Config.VehicleClass).SampleBreakaway(m_CalThr, surfCal);
                m_CalGot++;
                m_CalState = 2; m_CalWait = 0;
                SetCachedInput(0, 0, 1.0);
                return true;
            }
            m_CalThr = m_CalThr + m_Config.CalibBreakawayStep;
            if (m_CalThr > 1.0) { m_CalState = 3; BZBusLog.Warn("[CALIB] no se movio ni a fondo: abortada"); return false; }
            SetCachedInput(m_CalThr, 0, 0);
            return true;
        }
        // frenando hasta detenerse antes de la proxima muestra
        SetCachedInput(0, 0, 1.0);
        m_CalWait = m_CalWait + 0.5;
        if (vCal < 0.3 && m_CalWait > 1.5) {
            if (m_CalGot >= m_Config.CalibBreakawaySamples) {
                m_CalState = 3;
                BZBusLog.Info("[CALIB] LISTO: zona muerta de " + m_Config.VehicleClass + " = " + BZVehicleEnvelope.Get(m_Config.VehicleClass).GetBreakaway(surfCal));
                return false;
            }
            m_CalState = 1; m_CalThr = 0;
        }
        return true;
    }

    // ---- TRAMO ACTIVO (2026-07-21, idea de Sonom4n) --------------------------------------------------
    // "Cada intercambio marcado es un objetivo diferente, así no se pisa en las trayectorias."
    // En una maniobra la ruta SE CRUZA CONSIGO MISMA: medido en ESQ, la aproximacion pasa a 0.08 m de la
    // reversa y la reversa a 0.01 m de la salida. Cualquier busqueda de "el wp mas cercano" agarraba wps
    // de OTRA pierna -- a 2 m del intercambio el mas cercano ya era de la reversa, y eso desviaba a Boris
    // "como si tuviera la vista puesta en otro objetivo" (lo era).
    // Solucion: Boris solo CONOCE el tramo que esta recorriendo. Los cortes son los cambios de modo, que
    // el conversor ya detecta. Cada tramo termina en una parada => toda la maquinaria del endpoint (la que
    // mejor anda) aplica igual a los tres, y el intercambio deja de ser un caso especial.
    private int m_LegStart;
    private int m_LegEnd;
    private int m_LegAlignTicks;   // ticks esperando alinearse en el intercambio (valvula anti-cuelgue)
    private bool m_CuspSettled;      // Boris ya se asento (paro) en el cusp de reversa -> no re-frenar (anti-pasitos)
    private bool m_ForceLegAdvance;  // pedido de cerrar la pierna YA desde la supresion del cusp (lo consume UpdateLegBounds)
    private bool m_InFinalEpZone;    // Boris dentro de la zona de glide del endpoint FINAL -> DeadZoneInverse OFF (no re-inflar el creep a un lanzon)
    private float m_LegEndMinDist; // minima distancia alcanzada al endpoint del tramo (para cerrar en la mejor aproximacion)
    private int m_LegStuckTicks;   // ticks casi-parado cerca del endpoint sin mejorar (anti-cuelgue INCONDICIONAL)
    private bool m_RevCreepShort;  // el creep de reversa todavia esta llevando el ORIGEN al intercambio -> no cerrar el tramo aun
    private bool m_EpGlideShort;   // el creep forward (EndpointGlide) todavia esta clavando el checkpoint -> no cerrar el tramo aun
    private bool m_CheckpointHold;  // llego al checkpoint y esta en HILL-HOLD -> handbrake para no deslizarse en la pendiente

    // TRAMO LATCHEADO (2026-07-21, Sonom4n: "Boris no tiene que ni saber de la reversa hasta que haga
    // checkpoint en el intercambio"). Los tramos siguientes NO EXISTEN hasta completar el actual.
    // Sin esto el indice oscilaba a traves de la transicion (medido: 92 -> 95 -> 92 -> 95) y con el se
    // daba vuelta el MODO y la MARCHA varias veces por segundo: la caja cambiaba adelante/atras sin
    // parar, el motor nunca quedaba enganchado (RPM en ralenti con el gas a fondo) y el pure-pursuit
    // apuntaba a un wp de la reversa que esta al costado -> "se desvia a la izquierda como si tuviera
    // un wp ahi". Lo tenia.
    private bool m_LegInit;

    private void UpdateLegBounds() {
        if (!m_Config || !m_Config.Waypoints) return;
        int cntLg = m_Config.Waypoints.Count();
        if (cntLg == 0) return;
        // OJO EL ORDEN: m_LegInit tiene que quedar en true ANTES de SetLegFrom, porque SetLegFrom
        // llama a FindNextStopIndex y ese usa m_LegInit para saber si ya puede acotar al tramo.
        // Al reves (medido 21/07) el primer tramo abria con "proximo stop = 224" estando en 0..92.
        if (!m_LegInit) { m_LegInit = true; SetLegFrom(0); }
        // FORCE-ADVANCE DEL CUSP (2026-08-05, Sonom4n): la supresion asento a Boris parado dentro del radio de
        // captura del endpoint de reversa -> cerrar la pierna YA, sin esperar al creep/alineacion (que trababan
        // el cierre y hacian que mi brake tartamudeara en pasitos). Recien cerrada, la rampa lanza forward a 0.95.
        if (m_ForceLegAdvance && ActiveLegIsReverse() && m_LegEnd < cntLg - 1) {
            m_ForceLegAdvance = false;
            BZBusLog.Info("[TRAMO] cusp asentado -> force-advance de la pierna (wp " + m_LegEnd + ")");
            SetLegFrom(m_LegEnd + 1);
            return;
        }
        else if (m_ForceLegAdvance) m_ForceLegAdvance = false;
        // Completo el tramo? Solo entonces se ABRE el siguiente.
        if (m_LegEnd < cntLg - 1 && m_Bus) {
            vector pEndLg = m_Config.Waypoints[m_LegEnd].GetVector();
            float dEndLg = vector.Distance(m_Bus.GetPosition(), pEndLg);
            Car busLg = Car.Cast(m_Bus);
            float vLg = 99.0;
            if (busLg) vLg = busLg.GetSpeedometerAbsolute();
            // NO ALCANZA CON LLEGAR: HAY QUE LLEGAR ALINEADO (2026-07-21, Sonom4n). El intercambio es una POSE
            // (punto + rumbo), no un punto. Antes cerraba solo por distancia y velocidad -> Boris lo daba
            // por hecho estando torcido (medido saliendo de la reversa: -11 deg el M3, -5 el Sedan, +23 el
            // CivilianSedan; el humano llega con -0,2) y el tramo siguiente arrancaba desde una pose que no
            // era la grabada: rueda al tope, sin poder despegar, 12 s clavado.
            bool alineadoLg = true;
            float errHLg = 0;
            if (m_Config.Waypoints[m_LegEnd].targetHeading != 0) {
                errHLg = HeadingErrTo(m_Config.Waypoints[m_LegEnd].targetHeading);
                if (Math.AbsFloat(errHLg) > m_Config.LegDoneHeadingTolDeg) alineadoLg = false;
            }
            // CIERRE POR MINIMA APROXIMACION (2026-07-22, Sonom4n). El vertice del intercambio esta donde
            // el humano freno a 0 y encaro; Boris PARA CORTO ~2-3m de ese punto (medido SEDAN: interc1
            // forward 2,32m, interc2 reversa 2,86m -- la reversa para mas corto). Con LegDoneTolM=2,5
            // interc1 raspaba y cerraba, interc2 quedaba AFUERA y no cerraba nunca -> reversa eterna.
            // La pose SI estaba bien (rumbo err -4 y 0 deg). Entonces cerramos en la MEJOR aproximacion:
            // si ya entro al radio de captura y empezo a alejarse del minimo, es lo mas cerca que va a
            // estar -> valido igual que clavar el punto. Downstream la validacion de pose no cambia.
            bool improvedRc = false;
            if (dEndLg < m_LegEndMinDist) { m_LegEndMinDist = dEndLg; improvedRc = true; }
            float captR = m_Config.LegDoneTolM + m_Config.LegDoneCaptureExtraM;
            bool minAprox = (m_LegEndMinDist < captR && dEndLg > m_LegEndMinDist + 0.15);
            bool llegoLg = ((dEndLg < m_Config.LegDoneTolM || minAprox) && vLg < m_Config.LegDoneKmh);
            // ANTI-CUELGUE INCONDICIONAL: si esta casi parado y cerca del endpoint sin mejorar por N ticks,
            // abrir igual pase lo que pase (garantiza que ningun intercambio trabe la ruta para siempre).
            if (vLg < m_Config.LegDoneKmh && m_LegEndMinDist < captR + 1.5) m_LegStuckTicks++;
            else m_LegStuckTicks = 0;
            // El creep (reversa o forward) esta llevando el ORIGEN al punto y AVANZA -> no es cuelgue: resetea.
            if ((m_RevCreepShort || m_EpGlideShort) && improvedRc) m_LegStuckTicks = 0;
            if (m_LegStuckTicks >= m_Config.LegDoneStuckMaxTicks) {
                BZBusLog.Warn("[TRAMO] wp " + m_LegEnd + ": CLAVADO a " + dEndLg + "m (min " + m_LegEndMinDist + "m) tras " + m_LegStuckTicks + " ticks -> abro igual (anti-cuelgue)");
                SetLegFrom(m_LegEnd + 1);
                return;
            }
            // MIENTRAS EL CREEP CLAVA EL CHECKPOINT (origen todavia corto del punto) NO cerramos el tramo: que
            // el ORIGEN llegue a <CheckpointCloseTolM (objetivo <0.5m, Sonom4n) como el endpoint, en vez de cerrar
            // a 1.4-2.5m. Vale para reversa (m_RevCreepShort) y forward (m_EpGlideShort). El anti-cuelgue de
            // arriba queda como red por si el creep no puede (no avanza N ticks).
            if (m_RevCreepShort || m_EpGlideShort || m_SnapActive) return;   // m_SnapActive: el snap cinematico esta clavando la pose exacta -> el siguiente tramo arranca recien cuando termina
            if (llegoLg && !alineadoLg) {
                // llego pero torcido: le damos ticks para acomodarse antes de abrir igual (valvula
                // anti-cuelgue; sin esto una pose inalcanzable dejaria la ruta trabada para siempre)
                m_LegAlignTicks++;
                if (m_LegAlignTicks >= m_Config.LegDoneHeadingMaxTicks) {
                    BZBusLog.Warn("[TRAMO] wp " + m_LegEnd + ": llego pero TORCIDO " + errHLg + " deg tras " + m_LegAlignTicks + " ticks -> abro igual");
                    alineadoLg = true;
                }
            }
            if (llegoLg && alineadoLg) {
                string motivoLg = "clavo";
                if (dEndLg >= m_Config.LegDoneTolM) motivoLg = "min-aprox " + m_LegEndMinDist + "m";
                BZBusLog.Info("[TRAMO] completado hasta wp " + m_LegEnd + " (" + motivoLg + ", a " + dEndLg + " m, rumbo " + errHLg + " deg) -> abro el siguiente");
                SetLegFrom(m_LegEnd + 1);
            }
        }
        // el indice NO puede salirse del tramo activo
        if (m_WaypointIndex < m_LegStart) m_WaypointIndex = m_LegStart;
        if (m_WaypointIndex > m_LegEnd) m_WaypointIndex = m_LegEnd;
    }

    private void SetLegFrom(int idx) {
        int cntLg = m_Config.Waypoints.Count();
        if (idx < 0) idx = 0;
        if (idx > cntLg - 1) idx = cntLg - 1;
        m_CuspSettled = false;   // pierna nueva -> el latch anti-pasitos del cusp arranca limpio
        // DONDE TERMINA EL TRAMO.
        // Si la ruta trae INTERCAMBIOS marcados, MANDAN LAS MARCAS: el tramo llega hasta el proximo wp
        // con legBreak (ese wp es el ULTIMO del tramo -- su endpoint: Boris tiene que LLEGAR y parar ahi).
        // Asi la toma queda con endpoints CLAROS declarados por el autor, y el sentido (mode=reverse)
        // pasa a ser solo lo que define la marcha, no donde se corta.
        // Sin marcas se usa el comportamiento historico: cortar en el cambio forward<->reverse.
        // POR QUE NO SE INFIERE DE UN 0 km/h: un cero solo es una PAUSA en el mismo sentido.
        int e = idx;
        if (m_HasLegBreaks) {
            e = cntLg - 1;
            for (int q = idx + 1; q < cntLg; q++) {
                if (m_Config.Waypoints[q].legBreak) { e = q; break; }
            }
        } else {
            bool revLg = (m_Config.Waypoints[idx].mode == "reverse");
            while (e < cntLg - 1) {
                bool nextRev = (m_Config.Waypoints[e + 1].mode == "reverse");
                if (nextRev != revLg) break;
                e++;
            }
        }
        m_LegStart = idx;
        m_LegEnd = e;
        if (m_WaypointIndex < m_LegStart) m_WaypointIndex = m_LegStart;
        // SOBREPASO DEL CUSP (2026-07-25, Sonom4n, MEDIDO). Al terminar la reversa Boris queda ~0.56-0.82m PASADO
        // el arranque del tramo forward (wp186) -> wp185/186 le quedan ATRAS (rumbo ~315, 171 deg del morro).
        // Con el indice clampeado a m_LegStart el pure-pursuit apunta a un wp DETRAS -> clava el volante a la
        // izquierda -> se traba -> AR. Pasa hasta con cierre limpio (el sobrepaso es sistematico, no ruido).
        // FIX: si es un tramo FORWARD de intercambio (no el arranque de ruta) y Boris ya paso el arranque,
        // avanzar el indice al primer wp que este ADELANTE suyo -> apunta derecho (wp190) y retoma sin volantazo.
        // Reversa NO se toca (su "adelante" es al reves). "El intercambio es su start point": arranca donde mira.
        if (m_LegStart > 0 && m_Bus && m_Config.Waypoints[m_LegStart].mode != "reverse") {
            vector bposCusp = m_Bus.GetPosition();
            int qCusp = m_LegStart;
            while (qCusp < m_LegEnd) {
                vector wqCusp = m_Config.Waypoints[qCusp].GetVector();
                vector wqNextCusp = m_Config.Waypoints[qCusp + 1].GetVector();
                float ddxCusp = wqNextCusp[0] - wqCusp[0];
                float ddzCusp = wqNextCusp[2] - wqCusp[2];
                float dnCusp = Math.Sqrt(ddxCusp * ddxCusp + ddzCusp * ddzCusp);
                if (dnCusp < 0.05) { qCusp++; continue; }
                float aheadProjCusp = ((wqCusp[0] - bposCusp[0]) * ddxCusp + (wqCusp[2] - bposCusp[2]) * ddzCusp) / dnCusp;
                if (aheadProjCusp >= 0.0) break;   // wqCusp esta ADELANTE de Boris -> arranca aca
                qCusp++;
            }
            if (qCusp > m_WaypointIndex) m_WaypointIndex = qCusp;
            if (qCusp > m_LegStart) BZBusLog.Info("[TRAMO] cusp: Boris paso el arranque, indice " + m_LegStart + " -> " + qCusp + " (apunta adelante)");
        }
        // Al ABRIR el tramo recien aparecen sus paradas (ver FindNextStopIndex): sin esto
        // el endpoint quedaba invisible para siempre y Boris no frenaba al final.
        m_NextStopIndex = FindNextStopIndex(m_LegStart);
        // CADA TRAMO ARRANCA LIMPIO (2026-07-21, Sonom4n: "cuando Boris entra o sale de reverse tendria que ser
        // como si volviera a empezar; imaginate el intercambio como su start point"). El arranque de RUTA
        // funciona bien e IGUAL en los 3 vehiculos; el intercambio no, porque heredaba todo el estado del
        // tramo anterior (volante, latches, recovery) y la pose con la que termino la maniobra anterior.
        // Aca le damos al tramo el mismo trato que al spawn: estado en cero y arranque con la rueda derecha.
        m_LegLaunch        = true;
        m_LaunchDbg        = 0;   // diagnostico: re-arma el log para este tramo
        m_LegAlignTicks    = 0;
        m_LegEndMinDist    = 100000.0;   // cada tramo re-mide su mejor aproximacion al endpoint desde cero
        m_LegStuckTicks    = 0;
        m_CachedSteering   = 0;
        m_PrevSteering     = 0;
        m_MicroBaseSteer   = 0;
        m_EndpointLatched  = false;
        m_AtStop           = false;
        m_StopDecided      = false;
        m_OffPath_InRecovery = false;
        m_RevAlignTicks    = 0;
        BZBusLog.Info("[TRAMO] activo " + m_LegStart + ".." + m_LegEnd + " | proximo stop = " + m_NextStopIndex + " | arranque limpio");
    }

    private BZWaypoint CurrentWaypoint() {
        if (!m_Config) return null;
        if (m_WaypointIndex < 0 || m_WaypointIndex >= m_Config.Waypoints.Count()) return null;
        return m_Config.Waypoints[m_WaypointIndex];
    }

    // -------------------------------------------------------------------------
    // Deteccion de pasajeros

    // Jugadores dentro del radio de la parada (esperando en plataforma)
    private bool HasWaitingPlayers(BZWaypoint stop) {
        array<Man> players = new array<Man>();
        GetGame().GetWorld().GetPlayerList(players);

        vector stopPos = stop.GetVector();
        float  radius  = 50.0;
        if (stop.stopRadius > 0) radius = stop.stopRadius;

        foreach (Man man : players) {
            if (vector.Distance(man.GetPosition(), stopPos) < radius)
                return true;
        }
        return false;
    }

    // Jugadores sentados dentro del bus (viajando)
    private bool HasOnboardPassengers() {
        if (!m_Bus) return false;
        Car bus = Car.Cast(m_Bus);
        if (!bus) return false;

        // Asiento 0 = conductor AI, empezamos en 1
        for (int i = 1; i < bus.CrewSize(); i++) {
            Human member = bus.CrewMember(i);
            if (member && PlayerBase.Cast(member))
                return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // RPC handler - servidor responde al REQUEST_STATUS del cliente

    // Control Panel: el cliente pide su tecla + flag admin. El server responde SOLO
    // con (key, isAdmin del sender) Ã¢â‚¬â€ NUNCA la lista de admins ni el config completo.
    void HandlePanelSettingsRequest(PlayerIdentity sender) {
        if (!sender) return;
        string steamId = sender.GetPlainId();
        bool isAdmin = IsControlPanelAdmin(steamId);
        int key = GetControlPanelKey();
        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(key);
        rpc.Write(isAdmin);
        rpc.Send(recipient, BZBusRPC.RECEIVE_PANEL_SETTINGS, true, sender);
        BZBusLog.Info("[Panel] Settings sync a " + steamId + " key=" + key + " admin=" + isAdmin);
    }

    // Control Panel v1.1: telemetria viva. El cliente con el panel abierto la pide
    // ~3Hz. Re-validamos admin server-side (defensa en profundidad, igual que las
    // acciones). Respondemos un snapshot del estado actual del bus.
    void HandlePanelStatusRequest(PlayerIdentity sender) {
        if (!sender) return;
        if (!IsControlPanelAdmin(sender.GetPlainId())) return;

        bool active = (m_Bus && !m_Bus.IsRuined());
        bool paused = m_Paused;
        int  wpIdx  = m_WaypointIndex;
        int  wpTotal = 0;
        if (m_Config && m_Config.Waypoints) wpTotal = m_Config.Waypoints.Count();
        float kmh = 0;
        Car carBus = Car.Cast(m_Bus);
        if (carBus) kmh = carBus.GetSpeedometerAbsolute();
        string mode = m_PrevTickMode;
        int arCount = m_AR_Count;

        Man recipient = GetGame().GetPlayerByIdentity(sender);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(active);
        rpc.Write(paused);
        rpc.Write(wpIdx);
        rpc.Write(wpTotal);
        rpc.Write(kmh);
        rpc.Write(mode);
        rpc.Write(arCount);
        rpc.Send(recipient, BZBusRPC.RECEIVE_PANEL_STATUS, true, sender);
    }

    void HandleStatusRequest(ParamsReadContext ctx, PlayerIdentity sender) {
        vector signPos;
        if (!ctx.Read(signPos)) {
            BZBusLog.Warn("REQUEST_STATUS: no se pudo leer posicion");
            return;
        }

        string stopName = FindStopNameByPosition(signPos);
        BZBusStopInfo info = BuildStopInfo(stopName);

        Man recipient = GetGame().GetPlayerByIdentity(sender);

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(info.stopName);
        rpc.Write(info.status);
        rpc.Write(info.etaSeconds);
        rpc.Write(info.distanceMeters);
        rpc.Write(info.upcomingStops.Count());
        foreach (string s : info.upcomingStops)
            rpc.Write(s);

        rpc.Send(recipient, BZBusRPC.RECEIVE_STATUS, true, sender);
    }

    private BZBusStopInfo BuildStopInfo(string stopName) {
        BZBusStopInfo info = new BZBusStopInfo();
        info.stopName = stopName;

        if (!m_Bus || m_Bus.IsRuined()) {
            info.status       = "fuera de servicio";
            info.etaSeconds   = -1;
            info.distanceMeters = -1;
            return info;
        }

        BZWaypoint targetStop = FindStop(stopName);
        if (!targetStop) {
            info.status = "parada desconocida";
            return info;
        }

        info.distanceMeters = EstimateRemainingDistance(targetStop);
        float speed = 11.0;
        if (m_Config.AverageSpeedMS > 0) speed = m_Config.AverageSpeedMS;
        info.etaSeconds = (int)(info.distanceMeters / speed);

        if (m_AtStop && CurrentWaypoint() == targetStop) {
            info.status = "en parada aqui";
        } else if (m_AtStop) {
            BZWaypoint cur = CurrentWaypoint();
            string curName = "";
            if (cur) curName = cur.name;
            info.status = "en parada " + curName;
        } else {
            info.status = "en camino";
        }

        // Proximas paradas en direccion actual despues del target
        int idx  = m_WaypointIndex;
        int step = 1;
        if (m_Reverse) step = -1;
        bool pastTarget = false;
        int  stopsAdded = 0;

        while (stopsAdded < 4) {
            idx += step;
            if (idx < 0 || idx >= m_Config.Waypoints.Count()) break;
            BZWaypoint wp = m_Config.Waypoints[idx];
            if (!pastTarget && wp == targetStop) { pastTarget = true; continue; }
            if (pastTarget && wp.isStop && wp.name != "") {
                info.upcomingStops.Insert(wp.name);
                stopsAdded++;
            }
        }

        return info;
    }

    private float EstimateRemainingDistance(BZWaypoint targetStop) {
        if (!m_Bus || !m_Config) return 0.0;

        float total = vector.Distance(m_Bus.GetPosition(), CurrentWaypoint().GetVector());

        int idx  = m_WaypointIndex;
        int step = 1;
        if (m_Reverse) step = -1;

        while (true) {
            int next = idx + step;
            if (next < 0 || next >= m_Config.Waypoints.Count()) break;

            BZWaypoint a = m_Config.Waypoints[idx];
            BZWaypoint b = m_Config.Waypoints[next];
            total += vector.Distance(a.GetVector(), b.GetVector());
            idx = next;

            if (b == targetStop) break;
        }

        return total;
    }

    private string FindStopNameByPosition(vector pos) {
        // Usar BZBusStops (15 paradas reales del bus_stops.json) en vez de
        // m_Config.Waypoints (que arranca en template [0,0,0] hasta que el
        // admin complete BZBusRoute.json).
        BZBusStopAnchor closest;
        float closestDist = 99999;

        BZBusStopsConfig stops = BZBusStops.GetInstance().GetConfig();
        if (stops && stops.Stops) {
            foreach (BZBusStopAnchor s : stops.Stops) {
                if (!s) continue;
                float d = vector.Distance(pos, s.GetVector());
                if (d < closestDist) {
                    closestDist = d;
                    closest = s;
                }
            }
        }

        if (closest) return closest.name;
        return "";
    }

    private BZWaypoint FindStop(string name) {
        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (wp.isStop && wp.name == name)
                return wp;
        }
        return null;
    }

    // -------------------------------------------------------------------------
    // Destruccion y respawn

    // === MODO A PRUEBA DE DESPAWN (Fase 1, 2026-06-14) ===
    // Guarda anti-loop: si Boris se despawnea repetido SIN progreso (~mismo wp),
    // frena el auto-respawn en vez de loopear infinito (despawn determinista).
    static const int MAX_DESPAWN_RECOVERY = 3;
    private int m_DespawnRecoveryCount;
    private int m_LastDespawnWp = -1;

    private void OnBusDestroyed() {
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(Tick);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(FastSteerTick);
        m_FastSteerActive = false;
        int wpAtDespawn = m_WaypointIndex;
        float tRun = GetGame().GetTickTime() - m_MissionStartTime;
        BZBusLog.Warn("[DESPAWN] m_Bus perdido en wp " + wpAtDespawn + " (t=" + tRun + "s)");
        CleanupEntities();

        // Fase 1: en vez de rendirse y esperar NUMPAD 2, auto-respawnea para no romper
        // la corrida/mision. Si hubo progreso desde el ultimo despawn, resetea el contador;
        // si se va repetido en el mismo punto, lo frena (loop determinista) y avisa.
        if (wpAtDespawn > m_LastDespawnWp + 50) m_DespawnRecoveryCount = 0;
        m_LastDespawnWp = wpAtDespawn;
        m_DespawnRecoveryCount++;
        if (m_DespawnRecoveryCount <= MAX_DESPAWN_RECOVERY) {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.RespawnBus, 2000, false);
        }
    }

    private void CleanupEntities() {
        // Cancelar timers para evitar multiples Ticks/BoardDriver/ValidateSpawn acumulados
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.FastSteerTick);   // loop de direccion rapido
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CheckpointSnapTick);   // deslizamiento cinematico de checkpoint
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.RespawnBus);   // sino un runner parado se auto-respawnea (route-end / despawn-recovery)
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.EndOfRouteDespawn); // end-hold pendiente: no despawnear un runner ya recreado
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.BoardDriver);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CloseDriverDoor);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.OnStopFinished);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.ValidateSpawn);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.ExecuteActionDeferred);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.DismountQuestCrew);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.AmbushWaitStop);

        if (m_Driver) { m_Driver.Delete(); m_Driver = null; }
        if (m_Guards) {
            for (int gci = 0; gci < m_Guards.Count(); gci++) {
                if (m_Guards[gci]) m_Guards[gci].Delete();
            }
            m_Guards.Clear();
        }
        // Crew: por ahora se borra con el bus. PENDIENTE (Sonom4n): cuando un bot YA bajo,
        // deberia persistir (no despawnear con Boris) -> liberar de m_Crew al dismount.
        if (m_Crew) {
            for (int cci = 0; cci < m_Crew.Count(); cci++) {
                if (m_Crew[cci]) m_Crew[cci].Delete();
            }
            m_Crew.Clear();
        }
        if (m_CrewSeats) m_CrewSeats.Clear();
        if (m_Bus)    { m_Bus.Delete();    m_Bus    = null; }
        m_Group = null;
        if (m_CurrentInput != BZBusInput.NONE) {
            SetInput(BZBusInput.NONE, "CleanupEntities");
        }
    }

    // -------------------------------------------------------------------------
    // AI logging Ã¢â‚¬â€ graba la trayectoria del bus a CSV mientras la IA maneja.
    // Activado por NUMPAD 7 (cliente RPC). Cada corrida genera un CSV con
    // timestamp unico. Permite comparar contra la grabacion humana del
    // PathLogger normal, identificando zonas de divergencia entre lo grabado
    // y lo reproducido por la IA.

    void ToggleAILogging() {
        if (m_AILoggerActive) {
            StopAILogging();
        } else {
            StartAILogging();
        }
    }

    // Marca un evento en el CSV del AI logger. La proxima muestra que escriba
    // LogAITick tendra is_marker=1. Usado por el operador para anotar eventos
    // visuales relevantes (rozo el extremo, frenazo no esperado, etc.).
    void MarkAIEvent() {
        if (!m_AILoggerActive) {
            BZBusLog.Warn("MarkAIEvent: AI logger no esta activo");
            return;
        }
        m_AILogNextMarker = 1;
        BZBusLog.Info("AI MARK: proxima muestra marcada (sample " + m_AILogSampleCount + ")");
    }

    private string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }

    // V2 PHYSICS RECEIVER portado a v1 (2026-07-04). Muestrea el estado fisico REAL
    // del vehiculo cada frame (desde BZBusCarScript.EOnPostSimulate) para observabilidad
    // total del plant. Si el AI logger (NUMPAD 7) esta activo, loguea comandado-vs-real
    // a receiver_*.csv (~20Hz) junto al ai_run. Aditivo: no cambia el manejo.
    private ref BZPhysicsReceiver m_Receiver;
    private bool m_ReceiverLogOpen;
    private float m_ReceiverAccum;

    // FRAME REPLAY (2026-07-05): stream frame-by-frame + reloj de arranque del replay.
    // El comando per-frame vive en ApplyBusInput (40Hz); TickBody solo corta el control normal.
    private ref BZFrameReplay m_FrameReplay;
    private float m_FrameReplayStartTime;
    private float m_FrameReplayElapsed;   // TIEMPO-MOTOR: acumula el dt de fisica (no wall-clock) -> reloj soldado al tick
    private bool  m_ILCSaved;             // ILC in-game: ya persistimos la comp al fin del stream esta pasada
    private bool  m_FrameReplayDone;      // fin del stream ya procesado (Save + despawn diferido) esta pasada
    private float m_EndgameTime;          // endgame de precision: tiempo reptando al endpoint (timeout de seguridad)
    private bool  m_EndgameLocked;        // endgame: ya llegamos al endpoint y clavamos (anti-rollback)

    // Frame-replay activo: cargado + en PLAY + fuera de pausa/spawn/frozen. Single-line ifs
    // (NO condicion multilinea con && al inicio -> rompe el parser de Enforce).
    private bool FrameReplayActive() {
        if (!m_Config || !m_Config.FrameReplay) return false;
        if (!m_FrameReplay || !m_FrameReplay.IsLoaded()) return false;
        if (m_CurrentInput != BZBusInput.PLAY) return false;
        if (m_Paused || m_Frozen || m_RouteStopped) return false;
        return true;
    }

    void SampleReceiver(Car car, float dt) {
        // Aditivo y BARATO: cuando NO grabamos, retorna al toque (Boris maneja INTACTO).
        // Cuando grabamos, muestrea+loguea solo a ~20Hz, NO cada frame de fisica: el
        // sampleo pesado (dBody*, WheelGet* x4) cada frame -en el hilo de simulacion,
        // EOnPostSimulate- degradaba el tick del server B y rompia el control dt-sensible
        // de Boris (se iba de la curva a la tierra y se clavaba; ver ai_run 18:24).
        if (!car || !m_AILoggerActive || !m_ReceiverLogOpen || !m_Receiver) return;
        m_ReceiverAccum += dt;
        if (m_ReceiverAccum < 0.05) return;
        m_ReceiverAccum = 0;
        m_Receiver.Sample(car, m_CachedThrottle, m_CachedSteering, m_CachedBrake, m_DesiredGear, dt);
        m_Receiver.LogRow(GetGame().GetTickTime() - m_AILogStartTime);
    }

    private void StartAILogging() {
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        if (!FileExist(dir)) MakeDirectory(dir);

        int y, mo, d, h, mi, s;
        GetYearMonthDay(y, mo, d);
        GetHourMinuteSecond(h, mi, s);
        // Concatenacion directa para evitar problemas de parser con %1%2 pegados.
        // Ademas agregamos GetTime() (ms desde server start) como sufijo para
        // garantizar unicidad entre tomas en el mismo segundo - el primer intento
        // mostro que el filename quedaba igual entre tomas y sobrescribia.
        int tickMs = GetGame().GetTime();
        string ts = y.ToString() + Pad2(mo) + Pad2(d) + "_" + Pad2(h) + Pad2(mi) + Pad2(s) + "_t" + tickMs.ToString();

        m_AILogPath = dir + "ai_run_" + ts + ".csv";

        FileHandle f = OpenFile(m_AILogPath, FileMode.WRITE);
        if (!f) {
            BZBusLog.Err("AILogging: no se pudo crear " + m_AILogPath);
            return;
        }
        FPrint(f, "time_s,x,y,z,heading_deg,speed_kmh,gear,throttle,brake,steering,mode,dist_to_next_stop,next_stop_idx,wp_idx,lateral_dev_m,corridor_offset,corridor_valid,target_speed,target_throttle,target_brake,i_speed,i_throttle,i_brake,rpm,redline_rpm,wp_mode,is_marker,surface,rain,lights,battery_energy\n");
        CloseFile(f);

        m_AILoggerActive   = true;
        m_AILogStartTime   = GetGame().GetTickTime();
        m_AILogSampleCount = 0;
        // Abrir el receiver log junto al ai_run (misma carpeta + timestamp, misma base de tiempo).
        if (!m_Receiver) m_Receiver = new BZPhysicsReceiver();
        m_Receiver.OpenLog(dir + "receiver_" + ts + ".csv");
        m_ReceiverLogOpen = true;
        m_ReceiverAccum = 0;
        BZBusLog.Info("AILogging START: " + m_AILogPath);
    }

    private void StopAILogging() {
        m_AILoggerActive = false;
        if (m_Receiver && m_ReceiverLogOpen) {
            BZBusLog.Info("Receiver log STOP: " + m_Receiver.GetRowCount() + " filas");
            m_Receiver.CloseLog();
            m_ReceiverLogOpen = false;
        }
        BZBusLog.Info("AILogging STOP: " + m_AILogPath + " | " + m_AILogSampleCount + " samples");
    }

    // Llamado desde Tick() al final si m_AILoggerActive. Captura el estado del
    // bus + el modo de control activo (parking/crucero) + distancia al stop.
    private void LogAITick(string mode, float distToNextStop) {
        if (!m_AILoggerActive || !m_Bus) return;

        Car bus = Car.Cast(m_Bus);
        if (!bus) return;

        vector pos = bus.GetPosition();
        vector dir = bus.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        float t        = GetGame().GetTickTime() - m_AILogStartTime;
        float speedKmh = bus.GetSpeedometerAbsolute();
        int   gear     = bus.GetGear();
        float throttle = m_CachedThrottle;
        float brake    = m_CachedBrake;
        float steering = m_CachedSteering;
        float rpmAi        = bus.EngineGetRPM();
        float redlineRpmAi = bus.EngineGetRPMRedline();

        // 2026-06-25: condicion de manejo (tipo x estado) bajo el bus, para diagnosticar el grip
        // variable que el control no modela. TIPO = surface bajo el bus; ESTADO = lluvia (0-1).
        string surfTypeAi = "";
        GetGame().SurfaceGetType3D(pos[0], pos[1], pos[2], surfTypeAi);
        float rainAi = 0;
        Weather wAi = GetGame().GetWeather();
        if (wAi && wAi.GetRain()) rainAi = wAi.GetRain().GetActual();

        // 2026-06-27: estado REAL del faro (IsScriptedLightsOn vive en CarScript, no en Car)
        // + energia del CompEM de la bateria. Sirven para verificar el fix del heartbeat de
        // luces la proxima corrida (lights=1 estable + battery_energy no se vacia).
        int lightsAi = 0;
        CarScript carLightsAi = CarScript.Cast(m_Bus);
        if (carLightsAi && carLightsAi.IsScriptedLightsOn()) lightsAi = 1;
        float battEnergyAi = -1;
        ItemBase battAi = ItemBase.Cast(m_Bus.GetInventory().FindAttachment(CarBattery.SLOT_ID));
        if (!battAi) battAi = ItemBase.Cast(m_Bus.GetInventory().FindAttachment(TruckBattery.SLOT_ID));
        if (battAi && battAi.GetCompEM()) battEnergyAi = battAi.GetCompEM().GetEnergy();

        // Desviacion lateral del recording: distancia al wp MAS CERCANO en
        // ventana local. Antes usabamos m_WaypointIndex directo, pero ese es
        // el wp objetivo del lookahead (siempre adelante del bus por ~15m),
        // entonces la metrica daba ~16m promedio sin importar la trayectoria.
        // Bug corregido 2026-05-24: buscamos en ventana [-30, +5] el wp mas
        // cercano. Asi mide la distancia real al trazo, no al objetivo del
        // lookahead.
        float lateralDev = 99999.0;
        if (m_Config) {
            int count = m_Config.Waypoints.Count();
            int from = m_WaypointIndex - 30;
            if (from < 0) from = 0;
            int toIdx = m_WaypointIndex + 5;
            if (toIdx >= count) toIdx = count - 1;
            for (int i = from; i <= toIdx; i++) {
                vector wpPos = m_Config.Waypoints[i].GetVector();
                float dx = pos[0] - wpPos[0];
                float dz = pos[2] - wpPos[2];
                float d = Math.Sqrt(dx * dx + dz * dz);
                if (d < lateralDev) lateralDev = d;
            }
        }

        // Columnas extra para diagnostico de precision: CTE firmado del corredor,
        // valores grabados en el wp objetivo (target_*), y valores interpolados
        // que el controller uso como referencia interna (i_*). Permiten en post
        // calcular error de tracking, comparar interpolacion vs grabacion, y
        // ver donde MODO PARKING / KICK / SAFETY OVERRIDE pisan al i_throttle.
        float corrOff   = 0;
        int   corrValid = 0;
        if (m_CorridorValid) { corrOff = m_CorridorLateralOffset; corrValid = 1; }
        float tSpd = 0, tThr = 0, tBrk = 0;
        if (m_Config && m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count()) {
            BZWaypoint twp = m_Config.Waypoints[m_WaypointIndex];
            tSpd = twp.targetSpeed;
            tThr = twp.targetThrottle;
            tBrk = twp.targetBrake;
        }

        // Split de string para evitar "Formula too complex"
        string line = "" + t + "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "," + speedKmh;
        line += "," + gear + "," + throttle + "," + brake + "," + steering;
        line += "," + mode + "," + distToNextStop;
        line += "," + m_NextStopIndex + "," + m_WaypointIndex;
        line += "," + lateralDev + "," + corrOff + "," + corrValid;
        line += "," + tSpd + "," + tThr + "," + tBrk;
        line += "," + m_LastISpeed + "," + m_LastIThrottle + "," + m_LastIBrake;
        line += "," + rpmAi + "," + redlineRpmAi;
        string wpModeAi = "normal";
        if (m_Config && m_WaypointIndex >= 0 && m_WaypointIndex < m_Config.Waypoints.Count()) {
            BZWaypoint curWp = m_Config.Waypoints[m_WaypointIndex];
            if (curWp.mode != "") wpModeAi = curWp.mode;
        }
        line += "," + wpModeAi;
        line += "," + m_AILogNextMarker;
        line += "," + surfTypeAi + "," + rainAi;
        line += "," + lightsAi + "," + battEnergyAi + "\n";
        m_AILogNextMarker = 0; // consumir el marker para que solo se aplique a UNA muestra

        FileHandle f = OpenFile(m_AILogPath, FileMode.APPEND);
        if (!f) return;
        FPrint(f, line);
        CloseFile(f);

        m_AILogSampleCount++;

        // Log al RPT cada 20 muestras (~10s) con resumen para vista en vivo
        if (m_AILogSampleCount % 20 == 0) {
            BZBusLog.Info("[AI LOG] " + m_AILogSampleCount + " samples | wpIdx=" + m_WaypointIndex + " mode=" + mode + " lateral_dev=" + lateralDev.ToString() + "m kmh=" + speedKmh.ToString());
        }
    }

    // -------------------------------------------------------------------------
    // SYSTEM IDENTIFICATION Ã¢â‚¬â€ caracterizacion de la funcion de transferencia
    // interna de eAI. Aplicamos inputs programados (no del recording) y
    // grabamos la respuesta del sistema en un CSV para analisis posterior.
    //
    // Experimentos:
    //  1) Step response: throttle salta de 0 a 1 instantaneamente.
    //     Revela si eAI filtra/suaviza inputs (low-pass interno).
    //  2) Curva de respuesta: throttle escalonado 0.2/0.4/0.6/0.8/1.0
    //     cada 20s. Revela si la relacion throttle->velocidad es lineal
    //     o tiene saturacion/no-linealidad.

    // Rampa de test para experimentos System ID Ã¢â‚¬â€ generada por script.
    // Spawnea N placas inclinadas en posicion hardcoded de Vybor.
    // Pasa directo por la API de DayZ (CreateObject + SetOrientation), no por
    // el formato .dze, asi se evita el bug de pivot offset que tiene el editor.
    private ref array<Object> m_RampObjects = new array<Object>();

    void ToggleRamp() {
        if (m_RampObjects.Count() > 0) {
            ClearRamp();
        } else {
            SpawnRamp();
        }
    }

    private void SpawnRamp() {
        // Fallback automatico: probamos varios candidatos hasta dar con uno
        // que spawnee. DayZ classnames cambian entre versiones y mods, asi
        // que adivinar no funciona. Probamos vanilla, BBP, RaG, etc.
        array<string> candidates = {
            "Land_HelipadConcrete",
            "Land_HelipadCircle",
            "Land_HelipadSquare_F",
            "Land_Concrete_Plate",
            "Land_Wall_IndCnc_10",
            "Land_Wall_IndCnc_25",
            "BBP_ConcreteFloorKit",
            "BBP_Foundation_Concrete_Kit",
            "Container_Base",
            "Land_Container_1Bo",
            "Land_Misc_ConcreteHedgehog",
            "Wreck_OffroadHatchback",
            "Hatchback_02_Black",
            "WoodenLog",
            "Stone"
        };
        string plateClass = "";
        foreach (string candidate : candidates) {
            Object test = GetGame().CreateObjectEx(candidate, "1000 1000 1000", ECE_LOCAL);
            if (test) {
                GetGame().ObjectDelete(test);
                plateClass = candidate;
                BZBusLog.Info("Rampa: classname valido encontrado: " + candidate);
                break;
            }
        }
        if (plateClass == "") {
            BZBusLog.Err("Rampa: NINGUN classname de la lista funciono. Verificar mods cargados.");
            return;
        }

        int    plateCount    = 38;
        float  plateLength   = 8.0;
        float  pitchDeg      = 5.0;      // pendiente
        vector origin        = "4119.542 338.99 10824.290"; // extremo norte Vybor
        float  headingDeg    = -30.25;   // yaw, segun la linea central de la pista

        float headingRad = headingDeg * Math.DEG2RAD;
        float pitchTan   = Math.Tan(pitchDeg * Math.DEG2RAD);

        // Vector unitario en direccion del heading (DayZ: x=este, z=norte)
        vector forward = Vector(Math.Sin(headingRad), 0, Math.Cos(headingRad));

        int spawned = 0;
        for (int i = 0; i < plateCount; i++) {
            vector pos;
            pos[0] = origin[0] + forward[0] * (i * plateLength);
            pos[1] = origin[1] + (i * plateLength * pitchTan);
            pos[2] = origin[2] + forward[2] * (i * plateLength);

            Object plate = GetGame().CreateObjectEx(plateClass, pos, ECE_PLACE_ON_SURFACE | ECE_LOCAL);
            if (plate) {
                plate.SetOrientation(Vector(headingDeg, pitchDeg, 0));
                plate.SetPosition(pos); // re-set despues de orientation para evitar snap
                m_RampObjects.Insert(plate);
                spawned++;
            }
        }

        BZBusLog.Info("Rampa generada: " + spawned + "/" + plateCount + " objetos. Origen: " + origin.ToString() + " heading=" + headingDeg + " pitch=" + pitchDeg);
    }

    private void ClearRamp() {
        int cleared = 0;
        foreach (Object o : m_RampObjects) {
            if (o) {
                GetGame().ObjectDelete(o);
                cleared++;
            }
        }
        m_RampObjects.Clear();
        BZBusLog.Info("Rampa borrada: " + cleared + " objetos");
    }

    // Re-localiza el indice de waypoint al mas cercano a la posicion actual del bus
    // (ventana hacia adelante). Se usa al reanudar de una parada por gesto: el bus
    // coasteo adelante mientras frenaba y el indice quedo atras; el cap de avance por
    // velocidad (maxTrajDistAdv, piso 0.3m/tick) no lo deja alcanzar desde 0 km/h, asi
    // que Stanley apuntaba a un wp que quedo atras y timoneaba al costado.
    int HailReLocalizeWp() {
        if (!m_Config || !m_Config.Waypoints || m_Config.Waypoints.Count() == 0) return m_WaypointIndex;
        vector pos = m_Bus.GetPosition();
        int n = m_Config.Waypoints.Count();
        int best = m_WaypointIndex;
        float bestDist = 999999.0;
        int searchEnd = Math.Min(m_WaypointIndex + 60, n); // ~210m adelante, cubre cualquier coast
        for (int i = m_WaypointIndex; i < searchEnd; i++) {
            float d = vector.Distance(pos, m_Config.Waypoints[i].GetVector());
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    // Parada a demanda (gesto): si un player ENFRENTE del bus hace el OK (pulgar arriba,
    // EmoteConstants.ID_EMOTE_THUMB), Boris frena 10s para que suba y reanuda solo.
    // Reusa m_Paused (que ya clava el bus con brake en el Tick). El emote actual se lee
    // con BZ_CurrentGesture() (modded EmoteManager: GetGesture() devuelve otro campo).
    void CheckHailGesture() {
        if (!m_Bus) return;
        // si esta en hail-pause, ver si toca reanudar
        if (m_HailResumeTick > 0) {
            if (m_TickCount >= m_HailResumeTick) {
                m_HailResumeTick = 0;
                m_Paused = false;
                // El bus coasteo adelante al frenar; resync el indice a la posicion real
                // antes de devolverle el control, sino timonea hacia un wp que quedo atras.
                int reIdx = HailReLocalizeWp();
                BZBusLog.Info("[HAIL] 10s cumplidos -> Boris reanuda (wp re-localize " + m_WaypointIndex + " -> " + reIdx + ")");
                m_WaypointIndex = reIdx;
            }
            return;
        }
        if (m_Paused) return; // pausa manual -> no interferir
        vector busPos = m_Bus.GetPosition();
        vector busFwd = m_Bus.GetDirection();
        busFwd[1] = 0;
        busFwd.Normalize();
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++) {
            PlayerBase p = PlayerBase.Cast(players[i]);
            if (!p || !p.IsAlive()) continue;
            vector toP = p.GetPosition() - busPos;
            toP[1] = 0;
            float dist = toP.Length();
            if (dist < 1.0 || dist > 30.0) continue;
            toP.Normalize();
            if (vector.Dot(busFwd, toP) < 0.25) continue; // no esta enfrente -> Boris no lo ve
            EmoteManager em = p.GetEmoteManager();
            if (em && em.IsEmotePlaying() && em.BZ_CurrentGesture() == EmoteConstants.ID_EMOTE_THUMB) {
                m_Paused = true;
                m_HailResumeTick = m_TickCount + 20; // ~10s a 500ms/tick
                BZBusLog.Info("[HAIL] " + p.GetName() + " hizo OK enfrente (" + (int)dist + "m) -> Boris para 10s");
                BroadcastGlobal("Boris saw you — stopping for 10s, hop in!");
                return;
            }
        }
    }

    void TogglePause() {
        m_Paused = !m_Paused;
        if (m_Paused) {
            BZBusLog.Info("Bus PAUSADO (NUMPAD .): listo para teleport/setup experimento.");
            BroadcastGlobal("Vehicle paused.");
            // Reset cached input para evitar residuos
            SetCachedInput(0, 0, 1.0);
        } else {
            BZBusLog.Info("Bus REANUDADO (NUMPAD .): ruta normal activa.");
            BroadcastGlobal("Vehicle resumed.");
        }
    }

    void ToggleSysIDStep() {
        if (m_SysIDMode == 1) {
            StopSysID();
        } else {
            StartSysID(1, "step");
        }
    }

    void ToggleSysIDCurve() {
        if (m_SysIDMode == 2) {
            StopSysID();
        } else {
            StartSysID(2, "curve");
        }
    }

    private void StartSysID(int mode, string label) {
        m_Paused = false; // SysID toma control, ignora pause si estaba activo
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        if (!FileExist(dir)) MakeDirectory(dir);

        int tickMs = GetGame().GetTime();
        m_SysIDLogPath = dir + "sysid_" + label + "_t" + tickMs.ToString() + ".csv";

        FileHandle f = OpenFile(m_SysIDLogPath, FileMode.WRITE);
        if (!f) {
            BZBusLog.Err("SysID: no se pudo crear " + m_SysIDLogPath);
            return;
        }
        FPrint(f, "time_s,phase,target_throttle,actual_throttle,actual_brake,speed_kmh,rpm,gear,x,y,z,heading_deg\n");
        CloseFile(f);

        m_SysIDMode        = mode;
        m_SysIDStartTime   = GetGame().GetTickTime();
        m_SysIDSampleCount = 0;
        BZBusLog.Info("SysID START mode=" + mode + " (" + label + "): " + m_SysIDLogPath);
    }

    private void StopSysID() {
        BZBusLog.Info("SysID STOP: " + m_SysIDLogPath + " | " + m_SysIDSampleCount + " samples");
        m_SysIDMode = 0;
        // Al terminar el experimento, dejar el bus pausado para evitar que
        // arranque solo de vuelta a la ruta original. Para reanudar:
        // NUMPAD . (toggle pause off) o NUMPAD 2 (respawn fresh).
        m_Paused = true;
        SetCachedInput(0, 0, 1.0);
    }

    // Llamado desde Tick() cuando hay experimento activo. Ignora la ruta y
    // aplica inputs programados segun el experimento y el tiempo transcurrido.
    private void SysIDTick(Car bus) {
        if (!bus.EngineIsOn()) bus.EngineStart();

        float t = GetGame().GetTickTime() - m_SysIDStartTime;
        float targetThrottle = 0;
        string phase = "idle";

        float targetBrake = 0;
        if (m_SysIDMode == 1) {
            // Step response: 3s frenando full (asegura bus parado) + 10s step a 1.0
            // Total 13s. Menor distancia recorrida = menos chance de chocar en ruta.
            if (t < 3.0)       { targetThrottle = 0;   targetBrake = 1.0; phase = "brake"; }
            else if (t < 13.0) { targetThrottle = 1.0; targetBrake = 0;   phase = "step"; }
            else               { StopSysID(); return; }
        } else if (m_SysIDMode == 2) {
            // Curva de respuesta: throttle escalonado 0.2 -> 0.4 -> 0.6 -> 0.8 -> 1.0
            // Cada paso 20s. Total 100s.
            if (t < 1.0)        { targetThrottle = 0;   phase = "pre"; }
            else if (t < 21.0)  { targetThrottle = 0.2; phase = "0.2"; }
            else if (t < 41.0)  { targetThrottle = 0.4; phase = "0.4"; }
            else if (t < 61.0)  { targetThrottle = 0.6; phase = "0.6"; }
            else if (t < 81.0)  { targetThrottle = 0.8; phase = "0.8"; }
            else if (t < 101.0) { targetThrottle = 1.0; phase = "1.0"; }
            else                { StopSysID(); return; }
        }

        // Aplicar input (steering=0 para mantener recta)
        SetCachedInput(targetThrottle, 0, targetBrake);

        // Loggear la respuesta del sistema
        vector pos = bus.GetPosition();
        vector dir = bus.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        float speedKmh = bus.GetSpeedometerAbsolute();
        float rpm      = bus.EngineGetRPM();
        int   gear     = bus.GetGear();
        float actualThrottle = m_CachedThrottle;
        float actualBrake    = m_CachedBrake;

        string line = "" + t + "," + phase + "," + targetThrottle;
        line += "," + actualThrottle + "," + actualBrake;
        line += "," + speedKmh + "," + rpm + "," + gear;
        line += "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "\n";

        FileHandle f = OpenFile(m_SysIDLogPath, FileMode.APPEND);
        if (f) { FPrint(f, line); CloseFile(f); }
        m_SysIDSampleCount++;
    }

    // -------------------------------------------------------------------------
    // Notificaciones broadcast

    private void BroadcastDistances(string stopName) {
        array<Man> players = new array<Man>();
        GetGame().GetWorld().GetPlayerList(players);

        vector busPos = m_Bus.GetPosition();

        foreach (Man man : players) {
            PlayerBase player = PlayerBase.Cast(man);
            if (!player || !player.GetIdentity()) continue;

            float  dist = vector.Distance(busPos, player.GetPosition());
            string msg  = FormatDistance(dist, stopName);
            ExpansionNotification("[BZ AutoDrive]", msg).Create(player.GetIdentity());
        }
    }

    private string FormatDistance(float dist, string stopName) {
        if (dist < 50)
            return "You're at the " + stopName + " stop.";
        if (dist < 1000)
            return "Bus at " + stopName + " — " + Math.Round(dist) + " m away.";

        // Rounding manual a 1 decimal (Enforce no soporta %.1f en string.Format)
        float km     = dist / 1000.0;
        int   whole  = (int)km;
        int   tenths = (int)((km - whole) * 10);
        if (tenths < 0) tenths = -tenths;
        return "Bus at " + stopName + " — " + whole + "." + tenths + " km away.";
    }

    private void BroadcastGlobal(string msg) {
        BZBusLog.Info("[GLOBAL] " + msg);
        if (!GetGame().IsServer()) return;
        array<Man> bzPlayers = new array<Man>();
        GetGame().GetPlayers(bzPlayers);
        for (int bzi = 0; bzi < bzPlayers.Count(); bzi++) {
            Man bzp = bzPlayers.Get(bzi);
            if (!bzp) continue;
            PlayerIdentity bzpid = bzp.GetIdentity();
            if (!bzpid) continue;
            ScriptRPC bzrpc = new ScriptRPC();
            bzrpc.Write(msg);
            bzrpc.Send(bzp, BZBusRPC.RECEIVE_TOAST, true, bzpid);
        }
        // TODO: chat real a todos los clientes via RPC propio
    }
}
