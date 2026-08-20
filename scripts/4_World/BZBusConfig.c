class BZWaypoint {
    float pos[3];
    bool  isStop;
    string name;
    int   stopDuration;
    float stopRadius;       // radio en metros para detectar jugadores esperando
    float targetSpeed;      // km/h grabados en este tramo (referencia para throttle)
    float recordedSpeed;    // km/h GRABADOS originales, PRESERVADOS: targetSpeed se pisa en M2/M3 (FollowPath geometria), pero el modo aproximacion necesita la velocidad que el humano hizo en la entrada de la maniobra. Se setea en LoadWaypointsCSV = parts[6] crudo.
    int   targetGear;       // gear grabado en este tramo (referencia para ShiftTo)
    float targetThrottle;   // throttle 0..1 grabado (cuando hasInputData=true)
    float targetBrake;      // brake 0..1 grabado (cuando hasInputData=true)
    float targetHandbrake;  // handbrake 0..1 grabado. >0.5 = STOP/transicion explicita del humano (Boris se detiene). Sonom4n 2026-06-12.
    float targetSteering;   // steering -1..1 grabado. Solo usado en direct replay mode (MANIOBRA).
    string mode;            // Modo de manejo aplicable a este waypoint: "normal" (default, Stanley + cruise), "parking" (direct replay para precision), "maniobra" (futuro). El modder activa modes con toggle keys durante grabacion (NUMPAD + para parking).
    bool  hasInputData;     // true si el waypoint viene del PathLogger nuevo (con throttle/brake reales). Si false, se usa logica derivada.
    float targetHeading;    // heading_deg grabado (facing del humano). 0 = no disponible. El ModeSnap en reverse lo usa para orientar EXACTO (la geometria se torcia en la curva -> latd -> puerta derecha). Sonom4n 2026-06-24.
    int   targetHorn;       // bocina grabada en este wp: 0=OFF, 1=SHORT, 2=LONG (ECarHornState). Replay espacial: Boris honkea donde el humano honkeo. Sonom4n 2026-06-27.
    int   targetLights;     // luces grabadas en este wp: 0=apagadas, 1=encendidas. Replay en LightsMode="replay"; otros modos lo ignoran (auto por hora, on/off fijo). Sonom4n 2026-06-27.
    float targetFrontWheel; // FASE 2 (2026-07-04): angulo REAL de rueda EJECUTADO por el humano (WheelGetDirection, deg vs heading). El FF lo comanda directo con PlantUseRecordedWheel -> Boris reproduce lo que las ruedas del humano hicieron, no una version geometrica. 0=no disponible.
    float corridorHalfWidth; // CORREDOR-BANDA (2026-07-08, modelo Sonom4n): medio ancho del corredor EN ESTE wp. Angosto en recta (mantiene carril), ANCHO en el nodo (room para el giro -> el vehiculo expresa SU curva segun su fisica, no una linea impuesta). 0 = usar la constante CruiseLateralDeadband (rutas viejas). Ver [[project_graph_as_band_corridor]].
    // CORTE DE TRAMO / INTERCAMBIO (2026-07-21, Sonom4n). DECLARACION EXPLICITA del autor: "aca empieza un
    // tramo nuevo". Boris trata este wp como START POINT: para, resetea el estado de manejo (volante,
    // latches, recovery) y arranca con la rueda derecha, igual que en el spawn de la ruta.
    // POR QUE NO SE INFIERE: un 0 km/h SOLO es una PAUSA en el mismo sentido (ver el diseno de marcas del
    // editor) -- un semaforo, un titubeo o un frenazo tambien dan cero. El corte tiene que declararlo el
    // humano. Los cambios forward<->reverse SI se infieren (meter la marcha ya es una declaracion).
    // LO ESCRIBEN LOS DOS PRODUCTORES, con el mismo significado:
    //   - grabando: la tecla UABZMarkLeg -> is_marker=2 en el frame -> el conversor pone legBreak=1
    //   - dibujando: el editor lo marca en el nodo del intercambio al exportar
    bool  legBreak;

    vector GetVector() {
        return Vector(pos[0], pos[1], pos[2]);
    }

    void SetPos(float x, float z) {     // setter interno: la asignacion externa wp.pos[0]=x puede tener
        pos[0] = x;                     // semantica de COPIA (no toma efecto); adentro opera sobre el miembro.
        pos[2] = z;
    }

    bool IsZero() {
        return pos[0] == 0 && pos[1] == 0 && pos[2] == 0;
    }
}

// Un item del baul a spawnear (classname + cantidad). qty = stack (municion,
// comida) o 1 (armas, herramientas). v1: items sueltos, sin nesting ni armas
// equipadas (un arma con cargador puesto spawnea vacia -> meter mag suelto).
class BZCargoItem {
    string cls;
    int    qty = 1;
}

// Condicion evaluable on-demand (flow control / when). Misma logica que BZTrigger
// pero para chequeos inline. Combinadores booleanos AND/OR/NOT recursivos.
// test: player_in_radius | player_in_vehicle | vehicle_health_below | wp_reached |
//       var_equals | AND | OR | NOT
class BZCondition {
    string test;
    float  radius;      // player_in_radius
    float  threshold;   // vehicle_health_below (0..1)
    int    wp;          // wp_reached
    string var;         // var_equals: nombre de variable de escenario
    string value;       // var_equals: valor esperado
    ref array<ref BZCondition> checks = new array<ref BZCondition>();  // operandos de AND/OR/NOT
}

// Una accion del scenario DSL: un verbo + sus parametros. El modder apila las
// que quiera en un BZMarkerEvent. Campos union de todos los verbos (cada verbo
// usa los que necesita, el resto queda en default). Extensible: agregar un verbo
// nuevo = manejar su 'verb' en BZBusService.ExecuteAction, sin tocar el parser.
class BZAction {
    string verb;
    ref array<ref BZCargoItem> items = new array<ref BZCargoItem>();  // add_cargo
    string msg;          // log_event / ui_broadcast
    int    slot = -1;    // lock_seat / unlock_seat / eject_passenger
    string value;        // generico (faccion, target, set_var: valor)
    float  fvalue;       // generico numerico
    string var;          // set_var: nombre de variable
    string faction;      // spawn_guard: faction eAI del bot (default "Raiders" en codigo)
    string loadout;      // spawn_guard: loadout para armarlo (default "BanditLoadout" en codigo)
    int    count;        // spawn_guard: cuantos bots (default 1 en codigo)
    float  delay;        // segundos a esperar antes de ejecutar esta accion (coreografia). 0 = inmediato
    // NOTA 2026-06-11: check_once (condition/on_true/on_false) SACADO. Los campos
    // auto-referenciales (BZAction dentro de BZAction, BZCondition dentro de si misma)
    // rompian JsonFileLoader -> el array Events NO cargaba (ni los eventos simples).
    // El branching condicional es territorio de Quest (ver arquitectura). BZAction
    // queda PLANO (sin tipos auto-referenciales) -> Events carga OK.
}

// Un bot que VIAJA con Boris desde el arranque (pasajero). Spawnea sentado en su seat
// y baja ANIMADO con el verbo dismount_guard (GetOutVehicle, abre puerta). Faction
// Raiders = hostil al jugador pero amiga de Passive (Boris) -> sin fuego amigo.
// Defaults en codigo (JsonFileLoader no aplica field initializers en refs ausentes).
class BZCrewMember {
    string cls;            // class del bot      (default "eAI_SurvivorM_Boris")
    int    seat;           // asiento 0=Boris, usar 1+   (default 1)
    string faction;        // faction eAI        (default "Raiders")
    string loadout;        // loadout para armar (default "BanditLoadout")
    float  offsetRight;    // spawn: metros a la DERECHA del vehiculo (negativo = izquierda)
    float  offsetForward;  // spawn: metros al FRENTE del vehiculo (negativo = atras)
    // El modder define el offset segun el vehiculo (donde NO quede dentro del cuerpo del
    // auto, sino el bot mortal muere aplastado). Spawnea afuera + invencible hasta que baja.
}

// Trigger de un evento: CUANDO disparan sus acciones. Si el BZMarkerEvent no trae
// trigger, default = wp_reached en su 'wp' (compat con markers del recording).
// Tipos v1: wp_reached | player_in_radius | player_enter_vehicle | vehicle_health_below | timer.
// Armado desde el inicio de la mision (v1 sin secuenciamiento arm-after-X).
class BZTrigger {
    string type = "wp_reached";
    int    wp;          // wp_reached: indice de waypoint
    float  radius;      // player_in_radius: metros
    float  threshold;   // vehicle_health_below: salud 0..1
    float  seconds;     // timer: segundos desde el inicio de la mision
}

// Nodo de la state machine de la mision. Default: dispara cuando Boris alcanza 'wp'
// (compat con markers del recording). Si trae 'trigger', usa ese (proximidad, etc).
// "El recording es el esqueleto de la mision" (brainstorm 2026-06-08).
class BZMarkerEvent {
    int wp;
    ref BZTrigger trigger;   // opcional: si null -> wp_reached en wp
    ref array<ref BZAction> actions = new array<ref BZAction>();
}

class BZBusRouteConfig {
    int    RespawnDelay   = 300;
    float  AverageSpeedMS = 11.0;    // ~40 km/h en m/s, usado para calcular ETA
    // Secuencia de inicio: SPAWN -> hold N seg -> PLAY. Configurable por ruta.
    //   3.0 (default) - bus normal, 3s para sentar driver y calentar motor
    //   0.0           - arranca al toque (bus de evento)
    //   30.0+         - aguardar en la esquina por trigger de mision
    float  SpawnHoldSeconds = 3.0;
    // Secuencia de fin: al llegar al ULTIMO waypoint, el vehiculo se queda QUIETO
    // (handbrake) N segundos ANTES de despawnear / respawnear / quedar inactivo.
    // Simetrico a SpawnHoldSeconds (que sostiene el ARRANQUE).
    //   3.0 (default) - bus normal, 3s parado en la terminal antes de irse
    //   0.0           - sin hold, dispara al toque (comportamiento legacy)
    float  EndHoldSeconds = 3.0;
    // Deceleracion a freno fondo asumida por el MODO PARKING (frenado a paradas): brakeFrac = aNeeded / esto.
    //   50.0 (default) - legacy, validado para el BUS (iteracion empirica 6->9->12->16->20->50). Freno tardio.
    //   ~7.0           - fisica real de un auto liviano (Sedan): freno PROGRESIVO desde lejos -> el lazo
    //                    cerrado cinematico (aNeeded=u2/2d cada tick) absorbe la variacion de velocidad de
    //                    entrada (caos/bajada) -> parada DETERMINISTICA. El stop-learner queda para el residuo.
    float  BrakeDecelMS = 50.0;
    // Stanley curvatura-aware: en curva sube K hacia el de parking para no cortar.
    //   false (default) - Boris VANILLA: toda la correccion vive en el LEARNING (el cerco orientativo:
    //                     geometria + velocidad ensenada). Principio: no tocar el control de Boris.
    //   true            - habilidad innata (fallback validado 2026-07-06: converge 1.49->1.34->1.23 en EX05).
    bool   StanleyCurvatureAware = false;
    // Estrategia de luces durante la reproduccion:
    //   "auto"          - DEFAULT (2026-06-28). On de noche (hora < 6 o >= 19), off de dia. Ignora lo grabado.
    //                     Las luces prenden APENAS arranca el motor (durante el spawn-hold), no a mitad de ruta.
    //   "off"           - siempre apagadas. Misiones nocturnas SIGILOSAS (override del auto): el vehiculo no se delata.
    //   "auto_inverted" - off de noche [stealth], on de dia. Modo sigiloso por horario.
    //   "replay"        - sigue lo grabado por wp (targetLights). Boris prende/apaga donde lo hizo el humano.
    //   "on"            - siempre encendidas.
    string LightsMode = "auto";
    // Estrategia de bocina (espejo de LightsMode). El claxon GRABADO (targetHorn) SIEMPRE se
    // reproduce por replay espacial; HornMode controla solo el bocinazo AUTOMATICO:
    //   "replay" (default) - NADA automatico, solo lo grabado. (Antes honkeaba en CADA parada de TODA ruta.)
    //   "stops"            - bocinazo corto en cada parada.
    //   "finish"           - bocinazo corto SOLO al final de la ruta ("llegamos").
    //   "off"              - nunca.
    string HornMode = "replay";
    string VehicleClass   = "ExpansionBus";       // override si el bus no esta registrado
    string DriverClass    = "eAI_SurvivorM_Boris";
    // Estado inicial del vehiculo: true = IRROMPIBLE (default â€” sobrevive al
    // zone-damage de zombies durante la corrida). false = DESTRUCTIBLE (recibe
    // daÃ±o, habilita escenarios on_crashed). Switcheable en runtime con el verbo
    // set_vehicle_mortality (action de un marker NUMPAD 4).
    bool VehicleInvincible = true;
    // Boris mortal o no (espejo de VehicleInvincible). Antes hardcoded true en el codigo;
    // ahora per-ruta. El verbo set_driver_mortality lo flipea en runtime.
    bool DriverInvincible = true;
    // Ropa del CHOFER (Boris, seat 0) â€” config-driven por ruta. Lista de classnames a
    // equipar (CreateAttachment por string, cada uno a su slot). Si esta vacia/null, se
    // usa el outfit DEFAULT del framework (BZ_AutoDrive_TShirt + PolicePants + PoliceCap +
    // CombatBoots_Black). Si tiene items, REEMPLAZA el default (el admin pone el outfit
    // completo). Solo viste al chofer; la ropa de los bots de convoy/crew la maneja Quest.
    // CreateAttachment por string falla en silencio si el classname no existe/no esta cargado.
    ref array<string> DriverClothing = new array<string>();
    // Modo convoy (integracion Quest). "" = normal. "flee_on_kill" = escena 1 (matar 1 -> sobrevivientes
    // suben y huyen al patio). "ambush_on_damage" = escena 2 (bots arrancan a bordo armados; al recibir
    // CUALQUIER daÃ±o de arma el vehiculo o un bot -> freeze + despliegue animado + campean). Ver manual 7.8.
    string ConvoyMode = "";
    // Quest Travel (integracion Quest). Si >= 0, al arrancar el quest con ese ID el
    // framework auto-spawnea ESTA ruta (el jugador reclama -> el bus aparece, espera
    // SpawnHoldSeconds y maneja al destino). -1 = desactivado.
    int    QuestTravelID = -1;
    // Quest Escort (AIVIP, ObjectiveType 9). Si >= 0, al arrancar ese quest el framework auto-spawnea
    // esta ruta, SUBE al VIP del quest (reusa el boarding del convoy) y lo maneja al destino (ultimo
    // waypoint); el jugador escolta. El VIP baja en el destino -> objetivo cumplido. -1 = desactivado.
    int    QuestEscortID = -1;
    // Max gear que la AT puede shiftear arriba. En convencion CarGear:
    // FIRST=2, SECOND=3, THIRD=4, FOURTH=5, FIFTH=6, SIXTH=7.
    // Bus = 6 (5ta), Land Rover Expansion = 7 (6ta), Hatchback = 6, V3S = 7, etc.
    int    MaxGear        = 6;
    // AUTO-MaxGear (2026-07-13): si true, al spawnear el framework DERIVA MaxGear del CONFIG del
    // vehiculo (tope por gear via InverseModel: ratios+redline+radio) + la vel maxima de la ruta ->
    // el gear mas bajo que la alcanza. Generaliza a CUALQUIER vehiculo (modded: 1ra del Nissan hace
    // 82 -> MaxGear=2) y a rutas DIBUJADAS (sin datos de gear en la ruta; el vehiculo aporta el dato).
    // Sobreescribe el MaxGear de arriba. Default false (opt-in, no toca rutas validadas).
    bool   AutoMaxGearFromConfig = false;
    // AUTO steering gain del config (2026-07-13): si true, al spawnear escala PurePursuitGain por la
    // relacion de angulos de direccion -> gain_efectivo = PurePursuitGain * (SteerGainRefAngle / maxSteeringAngle).
    // El vehiculo REFERENCIA es el Sedan (30Â°): para el queda IGUAL (30/30=1, valores validados intactos).
    // Un auto que dobla mas (Nissan 36Â°) baja el gain (30/36=0.83) -> no sobre-dobla. Formula config->param
    // anclada en el Sedan -> generaliza a cualquier vehiculo desde SU config. Default false (opt-in).
    bool   AutoSteerGainFromConfig = false;
    float  SteerGainRefAngle = 30.0;   // maxSteeringAngle del vehiculo de referencia (Sedan) = ancla
    // THROTTLE CATCH-UP por DEFICIT (2026-07-14): cuando Boris va por debajo de la velocidad grabada
    // y NO llega (peldaÃ±o/obstaculo que el slope no ve, o launch conservador del InverseModel), forzar
    // throttle proporcional al deficit -> al trabarse contra el escalon el deficit crece y mete mas gas
    // hasta treparlo. Vehicle-agnostic: SOLO intenta alcanzar la velocidad grabada (no la sobrepasa; el
    // overspeed cut la corta arriba). Complementa al SlopeAssist (que solo ve pendiente graduada). Opt-in.
    bool   ThrottleCatchupEnabled = false;
    float  ThrottleCatchupGain = 0.08;     // throttle extra por km/h de deficit (0.08 -> deficit 11 = 0.88)
    float  ThrottleCatchupDeadband = 2.0;  // km/h por debajo del target antes de engancharse
    float  ThrottleCatchupCap = 1.0;       // throttle maximo que puede forzar el catch-up
    // ARRANQUE DERECHO EN CADA TRAMO (2026-07-21, idea de Sonom4n: "el intercambio tendria que ser como si
    // volviera a empezar"). MEDIDO en la grabacion humana, en la parada previa a la reversa: 2,35 s
    // detenido con el input de volante en 0.000 CLAVADO y la rueda entre 1,6 y 2,1 grados. El humano NO
    // toca el volante estando parado: para derecho, espera, y arranca derecho.
    // Boris hacia lo contrario: a 0 km/h barria la rueda de +24,9 a -35,0 grados persiguiendo el pure-pursuit,
    // y con la rueda cruzada un auto parado NO DESPEGA (medido: gas 0,95 + rueda 35 deg + rpm 1024 + 0 km/h).
    // Resultado: 12 s clavado al salir de la reversa donde el humano ya iba a 50 km/h.
    // Por que en el arranque de RUTA no pasa: SpawnBus llama a OrientBusToNext y le fija el rumbo al path.
    // En el intercambio no lo alinea nadie. Esto le da a cada tramo el mismo trato que al arranque.
    // FILTRO POR SENTIDO DE CIRCULACION (2026-07-21, idea de Sonom4n: "como si Boris necesitara no solo la
    // traza sino el flujo hacia donde corre, una linea flechada... como la corriente electrica").
    // La traza tiene POLARIDAD. Al buscar "el waypoint mas cercano", se descarta todo candidato cuyo
    // sentido de circulacion se oponga al del waypoint actual: un punto que apunta al reves NO es un
    // candidato valido aunque este a 20 cm.
    // CUBRE LO QUE EL TRAMO NO PUEDE: el corte de tramo resuelve la inversion de sentido (marcha atras),
    // y la ventana por indice resuelve el cruce lejano (ida y vuelta por la misma carretera, que en ESQ
    // corren a 3-5 m durante toda la ruta). Pero si el circuito de vuelta es CORTO -- una horquilla de
    // menos de 15 wps -- volves sobre la misma huella DENTRO de la ventana, en el MISMO tramo y con la
    // MISMA marcha: no hay tramo ni ventana que lo filtre. El sentido si.
    // Umbral en grados: >90 para no descartar candidatos legitimos en una curva de 90 (una traza opuesta
    // da ~180). 0 = filtro apagado.
    float  PathFlowFilterDeg = 120.0;
    // TRACER DEL CONTROLADOR (2026-07-21, Sonom4n): Boris loguea que seccion del pipeline toca el control en
    // cada tick (cerca del intercambio/endpoint, a baja vel). Corriendo varias situaciones, las secciones
    // que NUNCA aparecen son codigo muerto -> se borran con prueba. Es la herramienta de la depuracion.
    bool   ControlTraceEnabled = false;
    // === CONTROLADOR DE FRENO DE PARADA PREDICTIVO (2026-07-22) ===
    // Blindado por la investigacion de AV [[project_brake_controller_research]]: llegar al punto a la
    // velocidad indicada frenando TEMPRANO (predictivo, no reactivo) y SUAVE (jerk-limitado). Reemplaza
    // el freno reactivo/brusco (Boris frenaba a 7m a 10 m/s², desalineaba en pendiente/reversa).
    //   1) ley predictiva: distancia de frenado d_brake = v²/(2·a), a = decel comoda del vehiculo.
    //   2) modula continuo: a_needed = v²/(2·d) -> fraccion de freno, corregido por pendiente g·sin(θ).
    //   3) jerk-limitado: el freno no salta, rampa suave (jerk <= StopBrakeJerkMax).
    //   4) zona de creep: cerca del punto mantiene ~0.5 m/s para asentar antes de clavar (anti-jerk).
    // Gate opt-in: se prueba aislado sin tocar las rutas validadas.
    bool   StopBrakeControllerEnabled = false;
    float  StopBrakeDecelMS = 1.4;      // desaceleracion COMODA objetivo (m/s²) — la firma del chofer Arma. El envelope la refina por vehiculo
    float  StopBrakeJerkMax = 0.6;      // jerk maximo (m/s³) — confort, investigacion AV (rango 0.3-0.9). Limita Δa/Δt
    float  StopBrakeCreepKmh = 1.8;     // ~0.5 m/s: la zona de ajuste mantiene esta velocidad antes de clavar
    float  StopBrakeCreepRangeM = 1.2;  // dentro de esto del punto -> zona de creep (asienta y clava)
    // OFF-PATH RECOVERY: cuando el offset lateral pasa 4 m, ignora la grabacion y fuerza gas/freno hasta
    // volver al corredor. Nacio para no quedarse clavado en el cesped en cruise lento. PROBLEMA (MEDIDO
    // 2026-07-21 en las 3 tomas ESQ): en una curva de 90 el corredor CORTA la esquina -> el offset da
    // 4-7 m sin que Boris se haya ido -> la recovery se apodera del control justo en la curva y lo escupe
    // rotado ~45 deg respecto del intercambio. false = manda el pure-pursuit solo.
    bool   OffPathRecoveryEnabled = true;
    // EL INTERCAMBIO ES UNA POSE, NO UN PUNTO (2026-07-21, Sonom4n: "esos puntos tienen que tener impreso
    // wp + orientacion"). El tramo se daba por completado solo por distancia y velocidad -> Boris lo
    // cerraba estando TORCIDO (medido al salir de la reversa: -11 deg el M3, -5 el Sedan, +23 el
    // CivilianSedan) y arrancaba el tramo siguiente desde una pose que no era la grabada. El humano
    // llega a ese punto con -0,2 deg de error. Ahora el tramo cierra con posicion Y rumbo.
    // El MaxTicks es la valvula: si no logra alinearse, abre igual y no queda colgado para siempre.
    float  LegDoneHeadingTolDeg = 12.0;   // error de rumbo aceptado para dar el tramo por completado
    int    LegDoneHeadingMaxTicks = 20;   // ticks esperando alinearse antes de abrir igual (~10 s a 2Hz)
    bool   LaunchStraightEnabled = true;
    float  LaunchStraightKmh = 3.0;        // hasta esta velocidad, al empezar un tramo, la rueda va derecha
    float  LaunchStraightCap = 0.03;       // |steering| maximo durante el arranque (0 = totalmente derecho).
                                           // 2026-07-25: 0.10 permitia 8,5deg de rueda -> el steering de la reversa
                                           // spike-eaba -10deg en el 1er tick del flip FWD->REV (interc1) y Boris
                                           // arrancaba GIRADO; su reactivo lo corregia pero el humano grabo RECTO
                                           // (2deg). 0.03 = 2,5deg -> arranca derecho como la toma, sin inventar desvio.
    // SENSADO DE SUPERFICIE EN RUNTIME (2026-07-14): Boris "siente" la inclinacion REAL debajo suyo
    // (pitch del vehiculo, GetOrientation) y fuerza throttle en subida -> PROACTIVO, reacciona a la
    // inclinacion antes de que se acumule el deficit. Captura peldaÃ±os/escalones que la pendiente del
    // PATH (elevacion de waypoints) promedia y no ve. La fisica del vehiculo acota naturalmente cuanto
    // puede (config torque/peso). Complementa al SlopeAssist (path) y al catch-up (deficit). Opt-in.
    bool   SurfaceSenseEnabled = false;
    float  SurfaceSensePitchSign = 1.0;    // signo del pitch: +1 si nose-up es positivo; -1 si al reves (verificar 1er test)
    float  SurfaceSenseSmooth = 0.4;       // low-pass del pitch (0=crudo, ->1=congelado): mata ruido de suspension
    float  SurfaceSenseUpFactor = 4.5;     // throttle floor = slope_real * factor en subida
    float  SurfaceSenseUpCap = 0.95;       // cap del floor (preserva algo de steering authority)
    float  SurfaceSenseUpMinKmh = 28.0;    // subida solo actua abajo de esto (peldaÃ±os/trepadas son a baja vel)
    // BAJADA (simetrico): conservador -> NO forzar gas (la gravedad ya acelera). Capea el throttle en
    // descenso proporcional a la pendiente, para que el catch-up/InverseModel no empujen cuesta abajo.
    // El freno de sobrevelocidad (OverspeedCut) ya frena si se pasa. Sonom4n: "bajada, conservador de velocidad".
    float  SurfaceSenseDownThresh = 0.04;  // grade de descenso a partir del cual capea (~2.3Â°)
    float  SurfaceSenseDownFactor = 3.0;   // cuanto baja el cap por unidad de grade (mas empinado -> menos gas)
    // Attachments a aplicar al spawnear el vehiculo. Lista de classnames de
    // partes (ruedas, bateria, bujias, etc). Cada vehiculo tiene su propia
    // lista â€” bus usa ExpansionBusWheel/Double, Land Rover usa otros. Si la
    // lista esta vacia, el vehiculo spawnea desnudo y hay que equiparlo con
    // COT manualmente. Para vehiculos nuevos: empezar con lista vacia, probar
    // classnames de attachments empiricamente, llenar el JSON cuando funcione.
    ref array<string> Attachments = new array<string>();
    // Anti-catapulta: umbral de aceleracion (km/h por segundo) por encima
    // del cual la AT shiftea UP para reducir torque a las ruedas y suavizar.
    // Replica la tecnica humana "subir gear + pisar fuerte = manejo suave"
    // sin copiar literalmente el targetGear del recording (que bugueo antes).
    // Bus pesado: 999 (deshabilitado, no catapulta). Land Rover liviano: 15
    // (~4 m/sÂ², a partir de ahi shift up). Hatchback futuro: 10 (~2.8 m/sÂ²).
    float AccelShiftThreshold = 999.0;
    // Sensibilidad del steering. Escala lineal aplicada al output final del
    // Stanley. Para vehiculos con wheelbase corto (yaw rate alto por el
    // mismo input nominal) bajar este valor evita sobre-rotacion.
    // Bus wheelbase ~5-6m: 1.0 (default, sin escala). Land Rover ~2.7m: 0.5.
    // Hatchback ~2.5m: 0.45. V3S ~4m: 0.7.
    // -1 = AUTO: el framework lo deriva del wheelbase al spawn (clamp(wb/5.5, 0.4, 1.0)).
    // Cualquier valor > 0 = override explicito del modder (se respeta, dual-audience).
    float SteeringScale = -1;
    // Estrategia de gestion de marcha (gear) durante reproduccion:
    //   "auto_box"          - default. La AT del CarScript decide via RPM.
    //                         Apropiado para vehiculos pesados (bus, V3S).
    //                         La AT lo deja en gear razonable.
    //   "follow_recording"  - Bot fuerza ShiftTo(targetGear) leyendo del
    //                         waypoint actual. Apropiado para sport cars
    //                         (Nissan, etc.) donde la AT los deja en FIRST
    //                         a baja velocidad y patinan. La grabacion humana
    //                         capturo el gear correcto en cada momento.
    // Diagnostico de cuando usar uno u otro: si el vehiculo en 1ra a baja
    // velocidad derrapa, necesita follow_recording. Si forzar gear lo deja
    // clavado en gear viejo (ej al arrancar), necesita auto_box.
    string GearStrategy = "auto_box";
    // === MODO 2: follow_path (ruta escaneada / cross-vehicle, 2026-06-20) ===
    // Cuando true, el framework IGNORA el control grabado (throttle/brake/gear/steering del
    // recording, que es vehicle-specific) y maneja la ruta como GEOMETRIA PURA, derivando todo
    // de ESTE vehiculo via config: Stanley+wheelbase (volante), auto_box (marcha), y velocidad
    // OPTIMA por curvatura. Permite mandar un vehiculo DISTINTO al que grabo la toma. Habilita el
    // "mapa demostrado vehicle-agnostico". Default false = Modo 1 (replay de fidelidad).
    bool  FollowPath = false;
    // Aceleracion lateral maxima (m/sÂ²) del vehiculo = su grip en curva. La velocidad optima por
    // curva sale de v = sqrt(FollowPathLatAccel * R), R = radio de curvatura local de la geometria.
    // Mas grip = curvas mas rapido. Autos reales ~4-9 m/sÂ². El fingerprint test (donut full-lock)
    // lo mide por vehiculo. Default 4.0 (conservador, seguro para cualquier vehiculo).
    float FollowPathLatAccel = 4.0;
    // BRAKE-AHEAD (2026-07-08, mitad "predictTurn/brakeDistance" del desacople Arma): decel mÃ¡x (m/sÂ²)
    // para la pasada BACKWARD que garantiza que Boris pueda frenar a la velocidad de cada curva ANTES
    // de llegar. Sin esto entra a la V aguda a 24 km/h (necesita ~17 a R=6.7m) -> understeer -> corta ->
    // frena en seco a 0 -> AR. Con esto frena TEMPRANO y entra a la curva a su velocidad. 0 = off. ~2.5 rec.
    float FollowPathBrakeDecel = 0.0;
    // PISO DE VELOCIDAD (km/h) ANTI-CLAVADO (2026-07-09): la velocidad por curvatura en una curva al
    // limite (R~Rmin) baja a ~crawl, y Boris se CLAVA (el throttle no sostiene la marcha a esa velocidad
    // con full-lock -> queda a 0 -> AR). Con esto la velocidad objetivo nunca baja de este piso (salvo en
    // stops reales isStop) -> mantiene momentum, cruza la curva sin clavarse. 0 = off. ~11 rec. Combina
    // con abrir la curva (curvature-limit R~8) para que sea manejable a esa velocidad sin understeer.
    float FollowPathMinKmh = 0.0;
    // HONRAR LA VELOCIDAD PINTADA/GRABADA HASTA EL STOP (2026-07-13, pedido de Sonom4n: "si lo dibujo a 8, respetalo"):
    // el perfil de velocidad es la ley (dibujado O grabado). Cuando ON: (a) NO aplica el floor de MinKmh (la decel
    // fina dibujada 4,3,2,1 se respeta, no la reescribe a 5), (b) NO usa la cinematica de parking (que impone su
    // propia velocidad de approach -> el punch/overshoot del endpoint), (c) el InverseModel sigue apuntando a la
    // pintada + SlopeAssist (fuerza gas en SUBIDA para llegar a la velocidad DEFINIDA de cada wp), (d) creep slope-
    // aware al endpoint para clavar el punto exacto sin quedarse corto en subida. El bus costero queda con OFF.
    bool  FollowPaintedToStop = false;
    // HONRAR VELOCIDAD EN CURVAS (2026-07-14): el cap geometrico âˆš(latAccelÂ·R) es mas conservador que el
    // vehiculo real -> baja el target por debajo de lo GRABADO (medido: 90Â° grabado 26, cap 14 -> Boris
    // "clava velocidad baja" antes de la curva). Con el flag, la velocidad ESPECIFICADA de cada wp manda:
    // la grabacion ya probo que esa vel es segura para ESE vehiculo (fingerprint por-demostracion). El cap
    // geometrico queda para rutas DIBUJADAS sin slow-down de curva. Bounded por MaxKmh. Opt-in.
    bool  FollowPathHonorCurveSpeed = false;
    // HONRAR EL PERFIL DE DESACELERACION GRABADO (2026-07-14): FollowPathHonorCurveSpeed arregla el TARGET de
    // la curva, pero el BRAKE-AHEAD corre DESPUES y lo pisa: con bdec=FollowPathBrakeDecel (conservador) razona
    // "para llegar al apex a X frenando a solo bdec, tenes que estar YA a Y" -> Y queda por debajo de lo grabado
    // (medido Nissan 90Â°: grabada 26.7 en la aprox, Boris 14.9). Es el framework DUDANDO de la grabacion. Con
    // este flag el brake-ahead NO puede bajar el cap por debajo de wp.recordedSpeed: el humano PROBO que ESTE
    // vehiculo llega a esa curva a esa velocidad y frena a tiempo en esa distancia (perfil autoconsistente).
    // El brake-ahead sigue mandando en rutas DIBUJADAS (sin grabacion, donde si puede pedir decel imposible).
    // Opt-in. Ver [[project_vehicle_fingerprint_architecture]].
    bool  FollowPathHonorDecel = false;
    // POSICION-SYNC DE LA VELOCIDAD (2026-07-16): GEMELO de PlantSteerSourceNearest, pero para el target de
    // VELOCIDAD. m_WaypointIndex corre ~15m adelante (WAYPOINT_RADIUS=15) y el target de velocidad se lee de
    // ESE wp -> Boris lee el perfil 15-20m ANTES de estar ahi -> frena temprano y entra a la curva mas lento
    // que el humano (medido 90Â° Nissan: la RUTA pide 26.7 = lo grabado, y Boris entro a 15.3). No frena mal:
    // ejecuta el perfil CORRIDO ~20m. El brake-ahead YA hornea la anticipacion DENTRO del perfil (v[i] baja
    // antes de la curva) -> leerlo adelantado ANTICIPA DOS VECES. Regla: el VOLANTE quiere lookahead (Ld),
    // la VELOCIDAD quiere el wp donde Boris ESTA. No altera el avance del indice ni el aim del pure-pursuit.
    // Ver [[project_waypoint_radius_anticipation]]. Opt-in.
    bool  SpeedSourceNearest = true;   // DEFAULT (2026-07-17): el target de velocidad del wp mas cercano, no del indice adelantado. Validado NUEVO03.
    // ENDPOINT GLIDE (2026-07-17): el creep del endpoint (FollowPaintedToStop) es LAZO ABIERTO â€” aplica un
    // throttle FIJO sin mirar cuanto falta. Medido en FRAME03: Boris frena y se DETIENE a 1.53m del punto
    // (distRemaining < 0 dentro de STOP_FINAL_RADIUS -> brake=1.0), y ahi el creep lo empuja con 0.22 durante
    // 1.3m -> llega al punto a 6.8 km/h -> frenazo -> se pasa -> queda a 0.042m. Termina cerca por SUERTE
    // (el frenazo cayo justo), no por control, y se ve como "se detiene y avanza medio metro".
    // GLIDE = lazo CERRADO: la velocidad objetivo decae con la raiz de lo que falta, v=sqrt(2*a*gap), asi
    // llega al punto casi a 0 y clava sin rebote. Ademas es la ULTIMA palabra sobre el freno -> tampoco se
    // detiene a 1.53m: modula en vez de frenar a fondo. Opt-in; el creep viejo queda intacto para los
    // validados (NUEVO03/FRAME02). Ver [[project_checkpoint_stop_primitive]].
    bool  EndpointGlide = false;
    float EndpointGlideAccel  = 0.35;   // m/s2 de la rampa de aproximacion (mas alto = entra mas rapido)
    // VELOCIDAD DE APROXIMACION (2026-07-17, MEDIDO): con 1.5 Boris GATEA los ultimos metros (coast+creep
    // alternando a 1-2 km/h) y tarda 8.5s en 4m -> el temporizador de parada (EndHoldSeconds=3 + stopDur)
    // EXPIRA antes de que llegue y el fin de ruta le clava el handbrake a 0.19m. La aproximacion tiene que ser
    // FIRME: 4.0 (el creep viejo usaba 5.0 y llegaba). El que frena de verdad es el freno universal por fisica
    // (a = v^2/2d - g*sin), que tiene prioridad sobre el creep -> no hay riesgo de entrar caliente.
    float EndpointGlideMaxKmh = 4.0;    // creep por debajo de esta velocidad (aproximacion firme)
    float EndpointGlideStopM  = 0.05;   // a esta distancia del punto exacto: clavar
    float EndpointGlideRangeM = 3.0;    // desde aca el freno universal toma el control del throttle/brake. BAJADO
                                        // 12->3 (2026-08-11, endpoint tras curva): a 12m el freno universal le sacaba
                                        // el control al OJO y colapsaba la velocidad (ignora la grabada); a 3m el ojo
                                        // gobierna la aproximacion (sigue la velocidad grabada) y el universal solo
                                        // clava los ultimos 3m. Probado FT/REVCURVA/SEQ. Sincroniza con el template.
    // TECHO DE THROTTLE EN EL ENDPOINT FINAL (2026-08-05, Sonom4n). Ver el bloque endpointThrCap en BZBusService.c.
    // En la zona de glide del ULTIMO wp, los pisos de throttle (revApproach/DeadZoneInverse/BreakawayRamp) se
    // apilaban a ~0.29 y aceleraban a Boris (bajada) a 13 km/h -> sobrepaso 1.66m (Golf). nearCheckpoint no los
    // suprime porque esta muerto (StopBrakeControllerEnabled=false). Con esto: MOVIENDOSE (>EndpointGlideMaxKmh)
    // throttle=0 (coast -> el FRENO UNIVERSAL clava); des-clavando (<MaxKmh) capeado al EndpointKick. Solo el
    // endpoint final; no toca cusp/checkpoints/arranque.
    bool  EndpointThrottleCapEnabled = true;
    // PARADA PRECISA EN LOS INTERCAMBIOS (2026-08-16, Sonom4n): extiende el zone de parada del endpoint (freno
    // autoadaptativo + corte de gas) a las piernas FORWARD que terminan en un legBreak/intercambio -> Boris CLAVA
    // la pose del intercambio en vez de sobrepasarla (antes cerraba flojo por min-aprox y arrancaba la reversa
    // 7-11m desplazada -> embrollo/AR). true = reusa la precision del endpoint en cada intercambio. false = viejo.
    bool  EndpointStopAtIntercambio = true;
    // DECEL EFECTIVO DE PARADA DEL ENDPOINT FINAL (2026-08-05, Sonom4n: "el knob que empareja a todos esta en los
    // vehiculos"). El freno del cap usa brake = v^2/(2*d*decel). 0 = DERIVAR AUTOMATICO del EndpointBrakeDecel
    // del fingerprint (que el framework MIDE del recording de CADA vehiculo -> generaliza a cualquier mod de
    // autos sin conocerlo) por EndpointStopDecelFactor. >0 = override manual fijo. Solo SUMA freno (no toca a
    // los que ya paran bien). Golf/Hatchback (que carrean) frenan por SU decel medido -> emparejan.
    float EndpointStopDecelMS = 0.0;      // 0 = auto (deriva del fingerprint por-vehiculo)
    float EndpointStopDecelFactor = 0.85; // fallback si no hay InverseModel (el decel real sale del config por-vehiculo, ver abajo)
    // GANANCIA DE TREPADA DEL CREEP EN EL ENDPOINT (2026-08-07): en subida el techo del creep sube slEp*este
    // (la entrada al galpon tiene cuesta -> con 0.42 fijo Boris no trepaba y quedaba corto -> AR). Slope la mide
    // el framework (SampleTerrainY). El freno de parada del endpoint tambien acredita la gravedad (a_need-9.8*sin).
    float EndpointClimbGain = 2.5;
    // FACTOR DE TREPADA PARA FWD (2026-08-07, Sonom4n: "esto tiene que anticiparlo el framework leyendo la traccion").
    // El framework lee drive= del config (via InverseModel.GetDriveKind). El FWD (unico que boguea en la cuesta del
    // galpon; los AWD trepan) recibe el creep del endpoint x este factor para des-clavar/trepar. AWD/RWD sin cambio.
    float FwdClimbFactor = 1.6;
    // IMAN DE DETENCION (2026-07-29): knobs del CONTROLADOR (no del vehiculo -> el vehiculo aporta su fisica via
    // InverseModel). 'a' GENTIL de la envolvente de parada sqrt(2*a*d): gobierna la aproximacion (Boris COASTEA
    // bajo la envolvente en vez de frenar a la velocidad grabada lenta -> sale de la curva constante, lo menos
    // forzado) Y el iman final. Mas chico = entra mas suave/lento; mas grande = mas firme.
    // 2026-07-30: subido 0.6->1.0. Con el planchado de velocidad (SEQ1) Boris llega al AtStop CON ENVION (~8 km/h),
    // y a=0.6 no alcanza a frenarlo en los ~3m de la zona (v2/2a=4.1m > 3m) -> latcheaba a 6.9 km/h con 0.54m de
    // offset lateral (desalineado). a=1.0 -> v2/2a=2.6m < 3m -> scrubea a velocidad baja -> latch preciso + el
    // heading-align tiene tiempo de escuadrar. Es la parte "contiene si viene rapido" del iman. Ver [[project_endpoint_magnet]].
    float EndpointApproachAccel = 1.0;   // m/s2, envolvente de parada del iman (firme, ataja llegada con envion)
    float EndpointPredictBrakeAccel = 2.5; // m/s2, desac. FIRME del freno predictivo: si a esta tasa ya no paro en lo
                                           // que falta al punto -> clavo freno. Conservadora (toda masa la supera) ->
                                           // llega apenas corto y el break-away mete el ultimo tramo. Vehicle-agnostic.
    float EndpointBrakeDecel = 0.0;      // MEDIDA de la grabacion (carry+slam del humano), per-vehiculo, la escribe
                                           // frame_to_route.py en el _hdr. Si >0, el iman la usa para v_target Y freno
                                           // predictivo -> Boris frena DESDE VELOCIDAD ALTA como el humano (el freno es
                                           // fuerte a velocidad, debil al ralenti; el glide gentil se auto-saboteaba).
                                           // Medida != f(masa): Huracan 1400kg=6.9, f22 1227kg=1.3. 0 = default gentil.
    float EndpointApproachZoneM = 20.0;  // desde aca Boris coastea bajo la envolvente hacia el stop
    float EndpointHeadingSteerK = 0.05;  // volante del iman = rotar al heading GRABADO del endpoint (por grado de error)
    // COMPENSACION DE PENDIENTE del freno del iman (2026-07-30): en bajada la gravedad empuja a Boris a pasarse; el
    // InverseModel suma slopeForce=Mass*G*slope al freno. Con gain=1.0 (full) sobre-frenaba y paraba ~0.8m CORTO ->
    // esta ganancia dosifica cuanta pendiente ve el iman (0=ignora, 1=full fisica). Muestreo = 2m ANTES del endpoint.
    float EndpointSlopeBrakeGain = 0.5;   // CONGELADO (30/07): 0.5 clava a ~0.11-0.30m parado; 0.4 no mejoró
    // KICK del creep / PISO DE MOVIMIENTO (2026-07-26, Sonom4n: "el tema es NO DEJAR QUE BORIS SE DETENGA").
    // En el endpoint tras curva Boris llega lento y el creep de 0.14 lo deja coast-ear hasta 0 -> parado en
    // gear alto no arranca (medido SEQ1: 6s a throttle 0.13 sin moverse). PREVENTIVO: mientras le falte llegar,
    // si la velocidad cae por debajo de KickExitKmh se le da un empujon firme -> nunca baja a 0, llega en
    // movimiento y clava. Se apaga a <CheckpointCloseTolM del punto (ahi clava).
    float EndpointKickThrottle = 0.42;  // empujon para sostener el movimiento
    float EndpointKickExitKmh  = 1.6;   // piso de velocidad: por debajo, empuja (preventivo, no reactivo)
    bool  DriveStateDump = false;       // AUDITORIA (temporal): vuelca el estado de control por frame a drive_state.csv
    // Creep de reversa al intercambio: empuja el ORIGEN al punto con FUERZA PROPORCIONAL A LA PENDIENTE
    // (2026-07-22, Sonom4n). throttle = ZonaMuerta + g*sin(theta)/a_full, con a_full = aceleracion de traccion
    // MEDIDA EN RUNTIME del vehiculo (no del config; el Sedan tenia enginePower=0). Sedan medido: zona
    // muerta ~0.37, a_full ~3.1 -> para 13% pide 0.78. El creep anterior usaba 0.68 y no trepaba.
    bool  ReverseEndpointCreep = true;
    float StopBrakeAFull       = 3.1;   // a_full RUNTIME del vehiculo (m/s^2). TODO: del envelope per-vehiculo
    float StopBrakeDeadzone    = 0.37;  // zona muerta de throttle medida (abajo de esto no acelera)
    // MATAR EL CRAWL (2026-07-22, Sonom4n): dentro de esta distancia a un checkpoint, el StopBrake slope-aware es
    // el UNICO controlador longitudinal -> se apagan coastBand y revApproach, que capean el gas ("pila de
    // parches") y hacen que Boris se arrastre desde lejos sin llegar. Asi carga el envion y frena seco cerca.
    float CheckpointSoleBrakeM = 18.0;
    // CORRECTOR DE VOLANTE contra la TOMA (2026-07-22, Sonom4n): vigila tick a tick el comando de volante de Boris
    // vs el GRABADO (targetFrontWheel del humano en el punto proyectado) y, cuando se desvia mas que el margen,
    // da un NUDGE intermitente hacia el grabado. Corrige anomalias como el volante-izquierda al arrancar la
    // reversa (Boris -9deg vs humano 0deg). Solo a BAJA velocidad (no cruise, que anda bien). Es un CORRECTOR,
    // no comanda continuo: si esta dentro del margen no toca nada. Funciona en forward Y reversa (el plant FF
    // grabado era forward-only). Referencia = la toma; ataca el ruido/anomalia comparando con la verdad.
    bool  SteerCorrectorEnabled = false;   // OFF: era inerte (FF puro, gateado justo en la salida de curva).
                                           // Camino (A): atacar la apertura de salida en el PURE-PURSUIT. El codigo queda.
    float SteerCorrectorMaxKmh  = 15.0;   // arriba de esto NO corrige (protege el cruise validado)
    float SteerCorrectorThresh  = 0.06;   // desviacion (comando -1..1) que dispara la correccion; abajo = margen ok
    float SteerCorrectorGain    = 0.5;    // cuanto del error corrige por tick (intermitente, converge suave)
    float SteerCorrectorOnLineM = 0.5;    // SOLO corrige el volante si Boris esta a <esto de la traza (ON-LINE):
                                          // ahi un desvio de volante es ESPURIO (anomalia). Si esta mas lejos,
                                          // el volante DEBE corregir la posicion -> no lo forzamos hacia el grabado.
    // CORRECTOR DE VELOCIDAD contra la TOMA (2026-07-23, Sonom4n): en TODO momento (incluso cruise) Boris monitorea
    // su velocidad vs la grabada (targetSpeed del punto proyectado). Si se PASA, corrige con un poquito de freno
    // PROPORCIONAL al exceso, capado. CLAVE: constante, SIN traqueteo -> proporcional + RAMPA (rate-limit), NO
    // por umbral on/off (que se sentiria como que empuja el vehiculo de a ratos). Complementa el acelerador base
    // (InverseModel/cota); acopla con el corrector de volante (mismo arco = misma velocidad×volante). Ver [[project_steer_corrector_vs_take]].
    bool  SpeedCorrectorEnabled  = false;  // OFF (2026-07-23): hizo GATEAR la reversa (4.7km/h, 47s) -> stuck -> AR.
                                           // Volvemos al estado bueno; re-aplicar gateado para NO tocar la reversa.
    float SpeedCorrectorDeadKmh  = 1.5;   // zona muerta: abajo de este exceso no toca (no caza el ruido)
    float SpeedCorrectorBrakeGain = 0.035; // freno por km/h de exceso sobre la zona muerta
    float SpeedCorrectorBrakeCap  = 0.30;  // "un poquito de freno": tope del freno correctivo
    float SpeedCorrectorThrGain   = 0.03;  // gas por km/h de DEFICIT (va lento) -> acelera suave a tu velocidad
    float SpeedCorrectorThrCap    = 0.30;  // "un poquito de gas": tope del gas correctivo (floor sobre la base)
    float SpeedCorrectorRateMax   = 0.03;  // maximo cambio por tick (freno Y gas) -> RAMPA suave (anti-traqueteo)
    // CHECKPOINT (intercambio/endpoint): no cerrar el tramo hasta que el creep clave el ORIGEN a <esto del
    // punto (2026-07-22, Sonom4n: "tenemos que llegar a menos de 0.5"). Sin esto el tramo cerraba a 1.4-2.5m
    // (LegDoneTolM) ANTES de que el creep terminara -> el intercambio no clavaba como el endpoint.
    float CheckpointCloseTolM  = 0.2;    // el iman del intercambio reptea+centra hasta esta dist antes de avanzar (era 0.4 -> se cortaba a 0.66m; el endpoint va a 0.05 y clava 0.083). Bajado para que el intercambio clave como el endpoint (Sonom4n: "el intercambio ES un checkpoint donde mi vel grabada es 0, igual que el endpoint")
    // CHECKPOINT SNAP (2026-07-22, Sonom4n): la fisica llega al vecindario del punto (piso de ruido ~0.15-0.42m
    // MEDIDO: 2 runs del mismo build difieren eso -> el motor es estocastico). Para las poses que necesitan
    // PRECISION (puerta angosta, arbol del pickup) un deslizamiento CINEMATICO frame-by-frame completa el
    // residuo a la pose EXACTA grabada (pos+rumbo), en ~0.8s a velocidad de creep con ease-in-out -> invisible
    // (NO es un snap seco). Solo con Boris detenido en hill-hold; solo si el residuo < cap (no enmascara fallas
    // gruesas: si quedo a 5m es error de manejo, no se tapa). Prototipo global; luego gateable por checkpoint.
    bool  CheckpointSnapEnabled = false;   // DESCARTADO (2026-07-22, Sonom4n): el snap de POSICION se ve como
                                           // deslizamiento (movio el auto con el volante torcido en interc1,
                                           // y tiro a Boris atras en la subida). Vamos por SNAP DE CONTROLES
                                           // (volante+pedal, la fisica maneja). Codigo queda por si se revisita.
    float CheckpointSnapCapM    = 1.5;   // solo completa residuos < esto; mas = falla real de manejo (no snap)
    float CheckpointSnapSecs    = 0.8;   // duracion del deslizamiento (ease-in-out) -> a velocidad de creep
    // TOLERANCIA ESCALADA POR PENDIENTE (2026-07-22, Sonom4n): "mientras mas inclinado el terreno, menos
    // aproximacion tiene que tener, porque apenas baja la velocidad no le da para forzar la llegada". En
    // pendiente fuerte Boris NO puede trepar el ultimo tramo desde casi-parado -> si le exigimos 0.4m
    // oscila (trepa, toca, cierra, el forward lo dispara). tol_efectiva = CheckpointCloseTolM + factor*|grade|.
    // Llano -> 0.4 (fino); 13% -> ~0.8 (clava ahi, estable). El humano clava 0.0 por control fino; Boris no.
    float CheckpointSlopeTolFactor = 3.0;
    // APROXIMACION A LA TRANSICION REVERSE (2026-07-17): el THROTTLE GAP FIX frena segun iSpeed (wp del
    // INDICE, ~15m adelante) -> frenaba a 0 a 11m de la transicion fwd->reverse y despues flooreaba = "de a
    // pasitos". Con este flag, la aproximacion a la transicion frena por FISICA a la DISTANCIA REAL del punto
    // (v^2/2d, como el freno universal del endpoint) -> Boris llega casi a 0 EN el punto y el shift arranca
    // limpio. Gateado a "reversa por venir" + Boris forward -> NO toca cruise ni la ejecucion de reversa
    // (respeta [[feedback_reverse_fixes_isolation]]). Opt-in.
    // BANDA DE COAST (2026-07-17): mata el HUNTING freno<->gas cerca del target (medido: con volante ~0 el
    // control oscilaba brake 0.39 -> throttle 0.22 -> brake 0.35 = "saltitos" antes de la curva, agravado por
    // el tick de 500ms). Cuando |kmh - target| < esta banda: brake=0 y throttle limitado (coast/hold suave),
    // asi el overspeed-cut y el catchup dejan de pelear alrededor del target. 0 = off. Opt-in.
    float ApproachCoastBand = 0.0;      // km/h de banda muerta alrededor del target (ej 2.5)
    float ApproachCoastHold = 0.18;     // throttle maximo dentro de la banda (hold suave, sin catchup)
    // CONTROL LONGITUDINAL POR LOOKAHEAD (2026-07-17, idea de Sonom4n): en vez del patchwork reactivo
    // (overspeed-cut + catchup + throttle-gap peleando -> saltitos), UN target de velocidad continuo que
    // MIRA ADELANTE. Para cada wp en la ventana calcula la vel que Boris puede tener AHORA y aun frenar
    // COMODO a la de ese wp: vAllow = sqrt(vWp^2 + 2*a*d). El MINIMO de la ventana = target. Luego UNA ley:
    // freno FISICO si sobra, gas si falta, coast en la banda. Misma fisica que el freno universal del
    // endpoint, pero continua. Runtime -> generaliza. Va ULTIMO (pisa el patchwork). Opt-in.
    bool  UseSpeedLookahead = true;   // DEFAULT (2026-07-17): control longitudinal por lookahead (idea de Sonom4n). Cierra el 5% longitudinal. Validado NUEVO03: cruise 1 flip/4869 ticks, endpoint 0.086m, 0 zigzag.
    // El OJO escala con la velocidad (Sonom4n): la distancia que mira = SU distancia de frenado v^2/(2a) x margen.
    // A 80 km/h mira ~120m; a 10 km/h ~2m. Mas alla de eso no hace falta ver: siempre frena a tiempo.
    float SpeedLookaheadMinM = 6.0;       // piso de la ventana (a baja velocidad no mirar menos que esto)
    float SpeedLookaheadMargin = 1.3;     // margen sobre la distancia de frenado (1.3 = 30% extra)
    float SpeedLookaheadAccel = 2.5;      // decel COMODO (m/s2) de la rampa del ojo hacia lo que viene. SUBIDO
                                          // 2.0->2.5 (2026-08-11): con el ojo gobernando la aproximacion (RangeM=3),
                                          // este es el knob de COMO aterriza. 2.5 = afloja suave sobre los ultimos ~2m
                                          // y llega alineado (mas bajo = ve el endpoint antes). Sincroniza con el template.
    float SpeedLookaheadBrakeGain = 0.09; // brake por km/h de exceso sobre el target
    bool  ReverseApproachStop = false;
    float ReverseApproachWindowM = 14.0;   // ventana (m) para detectar la transicion adelante
    // La transicion fwd->reverse NO es un dead-stop: el humano pasa a 4-6 km/h y da la reversa. El wp-advance
    // exige <1.5 km/h para flipear el gear (BZBusService L8254). En vez de frenar a CERO lejos y gatear (creep
    // 0.20 -> 1 km/h en vehiculos livianos = "se clava"), frenamos por fisica para LLEGAR a GateKmh EN el punto:
    // Boris cruza el gate moviendose y engancha reversa fluido (2026-07-17).
    // ANTI-PLANTADA POR COASTEO (2026-07-20). El control soltaba el acelerador dando por hecho que el
    // vehiculo planea hasta el punto -- cierto en el Sedan (1000 kg), FALSO en el M3 (1400 kg), que se
    // planto 8.5 m antes del endpoint y 5.5 m antes del intercambio. Ahora BZVehicleEnvelope MIDE la
    // decel de coasteo del vehiculo (aspecto 2) y el control pregunta "si suelto, llego?": si lo que
    // falta supera el planeo, sostiene gas en vez de coastear. Con datos del vehiculo, no constantes.
    // POSE EN LAS TRANSICIONES (2026-07-20, Sonom4n: "los puntos de transicion son objetivos de wp o de
    // alineacion tambien?"). Una transicion NO es una coordenada: es POSICION + ORIENTACION. Para cambiar
    // de sentido hay que llegar al lugar Y apuntando a donde vas a salir. Medido en 3 corridas: Boris
    // ENTRA bien alineado (+2 deg) pero SALE entre 30 y 160 deg torcido y sigue viaje mal apuntado, porque
    // el framework daba el punto por cumplido con solo pasar cerca. Mientras no este alineado con el
    // heading GRABADO del wp de salida, no avanza: sigue rotando en reversa, como haria un humano.
    // Distancia a la que se ARMA la marcha atras. Mas lejos que esto, aunque el waypoint actual sea de
    // reversa, Boris sigue en marcha adelante ACERCANDOSE al punto (ver el bloque de gear en BZBusService).
    float ReverseGearArmM  = 6.0;
    // No meter la reversa hasta terminar de girar: el humano completa la rotacion EN MARCHA ADELANTE y
    // recien ahi cambia. Si se mete antes, el volante invierte su efecto y el giro queda a medias.
    bool  ReverseGearNeedAlign     = true;
    float ReverseGearAlignTolDeg   = 12.0;   // tolerancia angular para dar por terminado el giro
    int   ReverseGearAlignMaxTicks = 20;     // tope (~10 s): si no logra alinear, mete reversa igual
    // ZONA MUERTA / BREAKAWAY (aspecto 3 del envelope). Si Boris queda DETENIDO y le falta llegar al
    // punto, el acelerador sube en rampa hasta que se mueve: eso lo desatasca Y mide su despegue real.
    // MIRA CORTA CERCA DE UNA PARADA: evita que el pure-pursuit CORTE la curva y se saltee el punto.
    bool  StopLookaheadShrink = true;
    float StopLookaheadRangeM = 25.0;   // desde esta distancia al punto, la mira empieza a acortarse
    float StopLookaheadMinM   = 3.0;    // mira minima estando encima del punto
    // INVERSA DE ZONA MUERTA (2026-07-21). El vehiculo no responde por debajo de cierto acelerador
    // (friccion estatica / breakaway). Un control que no lo sabe entra en bang-bang: pide poco, no pasa
    // nada, acumula, se pasa, frena, repite -> los "saltitos". La tecnica desplaza el comando por encima
    // de ese umbral MEDIDO, con rampa suave cerca del cero. Reemplaza los creep inventados (0.20/0.22/
    // 0.30/0.40) por el valor real del auto en ese piso.
    bool  DeadZoneInverseEnabled = true;   // el breakaway ahora sale de CalibBreakaway (rampa desde cero), no del pasivo
    float DeadZoneMaxKmh         = 15.0;  // solo por debajo de esto: arriba el acelerador ya es efectivo
    float DeadZoneSmoothW        = 0.15;  // ancho lc de la rampa cerca del cero (ni salto duro ni oscilacion)
    // CALIBRACION explicita del umbral de despegue, al estilo del donut que mide el radio minimo: parado,
    // el gas sube DESDE CERO hasta que el auto se mueve. Ese es el valor real. Corre UNA VEZ por
    // vehiculo+piso (si el envelope ya lo tiene, se saltea) y queda persistido.
    // TRAMO LATCHEADO: el tramo siguiente NO EXISTE hasta completar el actual (llegar a su ultimo wp
    // y estar practicamente detenido). Evita que el indice oscile a traves de una transicion y de vuelta
    // el modo/la marcha varias veces por segundo.
    float LegDoneTolM = 2.5;   // a esta distancia del ultimo wp del tramo se da por completado
    float LegDoneKmh  = 3.0;   // y ademas hay que estar casi detenido
    // Boris PARA CORTO del vertice del intercambio (reversa ~2,9m, forward ~2,3m). El cierre tambien
    // vale en la MEJOR aproximacion: si ya entro a (LegDoneTolM + CaptureExtra) y empezo a alejarse del
    // minimo, es lo mas cerca que va a estar -> se da por completado (con la pose validada igual).
    float LegDoneCaptureExtraM = 2.0;   // radio de captura extra sobre LegDoneTolM (2,5+2,0 = 4,5m)
    int   LegDoneStuckMaxTicks = 24;    // casi-parado cerca del endpoint sin mejorar => abrir igual (~12s a 2Hz)
    bool  CalibBreakawayEnabled = true;
    int   CalibBreakawaySamples = 3;      // repeticiones para promediar
    float CalibBreakawayStep    = 0.03;   // cuanto sube el gas por tick (500 ms) durante la rampa
    int   CalibBreakawayWaitTicks = 26;   // esperar (~13 s) a que pase el hold de spawn y arranque el motor
    bool  SpeedDecisionDebug   = false;  // log [SPD] (OFF en release; on para debug de fuente de velocidad)
    bool  DriveDiagLog         = false;  // 2026-08-18: gate GENERAL de diagnostico por-frame ([Tick]/[EpZone]/[LAUNCHDBG]/[MANIOBRA]) - OFF en release, on solo para debug
    bool  BreakawayRampEnabled = true;
    float StopResidualTolM     = 1.0;    // un stop no se da por cumplido hasta estar A ESTA DISTANCIA del punto
    // FASE 1 SUB-CONTROLADOR REVERSA: DESPEGUE FIRME (2026-08-16). El bus pesado no despega en reversa desde
    // parado con el breakaway aprendido -> AR. El humano FLOOREA (throttle=1.0 medido). Mientras la pierna sea
    // reversa, parado y lejos del endpoint, mete este throttle para romper la inercia; suelta al moverse.
    bool  RevLaunchBoostEnabled = true;
    float RevLaunchThrottle     = 1.0;   // como el humano (a fondo). Bajar si lurchea al despegar.
    float RevLaunchExitKmh      = 2.0;   // apenas supera esta vel, suelta el boost -> control normal de la reversa
    // FASE 2: VOLANTE DE REVERSA DESDE TU FRONT WHEEL GRABADO (2026-08-16). El conversor descarta targetSteering
    // (=0) -> la reversa quedaba con puro Stanley que saturaba (thrash, full lock al reves). Con esto el feedforward
    // sale de targetFrontWheel/plant-gain (reproduce tu rueda ejecutada) -> traza tu arco. Generaliza por plant-gain.
    bool  RevUseRecordedWheel = true;
    float RevWheelLeadM       = 0.0;    // lead posicional del volante grabado (0 = en el punto exacto; la reversa es densa/localizada, no anticipar)
    // SUB-CONTROLADOR DE MANIOBRA (2026-08-16, Sonom4n): DUEÑO de las piernas que terminan en intercambio (legBreak).
    // Secuencia predictiva por fuera del cruise/endpoint: marcha fija (mata flip-flop) + volante = tu front-wheel
    // grabado (traza tu arco) + freno predictivo autoadaptativo -> clava <0.5m sobre tu pose. Reproduce tu movimiento
    // con fisicas y generaliza (decel/gain por-vehiculo). Ver ManeuverControl en BZBusService.c.
    bool  ManeuverControllerEnabled = true;  // 2026-08-19: re-habilitado con el despegue CLOSED-LOOP arreglado (la fase DESPEGUE flooreaba open-loop = free-rev del cold spawn; ahora rampa cerrada por respuesta del chasis, como v1.0). Confirmado: apagarlo elimina el free-rev; el resto de MC da la precision del intercambio.
    float ManeuverStopTolM       = 0.4;   // clava a esta distancia de la pose del intercambio (objetivo <0.5m)
    float ManeuverStopKmh        = 1.0;   // <= esto + dentro de StopTol -> CLAVADO (latch + cierra pierna)
    float ManeuverLaunchThrottle = 1.0;   // despegue firme desde parado (bus). TODO: ramp closed-loop para agnostico sedan
    float ManeuverCruiseKmh      = 4.0;   // velocidad de crucero de la maniobra (paso de hombre)
    float ManeuverCruiseThrottle = 0.55;  // throttle firme para mover el bus en la maniobra (el 0.4 era debil)
    float ManeuverSteerSign      = 1.0;   // signo del volante grabado (probar en reversa; -1.0 si sale al reves)
    float ManeuverZoneM          = 15.0;  // dist del intercambio para enganchar. El tramo-completo (999) zigzagueaba en las RECTAS (replay open-loop + cross-track oscila a velocidad); el cruise normal es mas suave ahi. 15 = cubre el approach al intercambio (llegar on-line) sin manejar la recta larga
    float ManeuverLaunchStraightKmh   = 1.5;  // clamp anti-scrub SOLO FORWARD: hasta esta vel se acota el volante para romper inercia (un pesado parado con la rueda cruzada NO despega). En reversa NO aplica (el pre-steer despega bien)
    float ManeuverLaunchStraightSteer = 0.15; // tope de volante mientras despega forward (gentil, sin full-lock que traba el arranque por scrub)
    // VELOCIDAD FIEL (2026-08-16, Sonom4n "velocidad+trayectoria -> los endpoints caen solos"): objetivo = tu vel
    // GRABADA, acotada por el freno universal para parar en la pose. Baja velocidad, alta precision.
    float ManeuverSpeedLeadM      = 0.5;  // (legacy) lead de lectura de velocidad; ahora se usa el lookahead anticipatorio
    float ManeuverSpeedLookaheadM = 8.0;  // PURE-PURSUIT DE VELOCIDAD: mira este tramo adelante en tu perfil y toma la vel que permite decelerar a lo que viene (se anticipa al giro lento; el ojo del cruise)
    float ManeuverTurnWheelThresh = 2.0;  // (legacy, sin uso) reemplazado por el cap SUAVE proporcional al volante
    float ManeuverTurnKmh         = 30.0; // DESACTIVADO (=CapMax): el cap por giro oscilaba con el volante grabado (que varia en la maniobra) -> vTgt saltaba 4.5<->12 -> pasitos. El understeer de los giros se ataca de otra forma
    float ManeuverTurnCapMaxKmh   = 30.0; // =ManeuverTurnKmh -> vCap constante -> sin cap efectivo
    float ManeuverTurnCoastFrac   = 0.5;  // si |tu volante grabado| supera esta fraccion de full-lock (giro cerrado), Boris NO acelera (coast) -> no understeerea
    float ManeuverTurnCoastThr    = 0.25; // throttle max en el giro cerrado. 0.12 frenaba DE MAS -> radio mas cerrado que el tuyo -> residuo lateral ~0.8m. 0.25 sostiene ~tu velocidad del giro -> radio igual -> cae centrado (sin acelerar = sin understeer)
    float ManeuverCoastZoneM      = 8.0;  // el coast solo se aplica a menos de esta dist de la pose (el giro brusco del approach); en el patio (mas lejos) el volante variable lo toggleaba = pasitos
    float ManeuverLaunchMinGapM   = 0.8;  // si esta parado y a mas de StopTol+esto de la pose, despega (arranque de tramo) sin importar la vel grabada
    float ManeuverLaunchKmh       = 0.5;  // el DESPEGUE (thr=1) solo dispara bajo esta vel = parado REAL. Antes 1.0 -> gateaba (cada bajon a <1 en maniobra lenta tiraba thr=1); los 0.5-1 los maneja el chase-vTgt suave
    // HILL-START (2026-08-17, Sonom4n): en subida el thr=1 solo no despega desde parado -> revolucionar contra el handbrake y soltar (pico de torque)
    float ManeuverHillStartSlope  = 0.06; // sin(pitch) minimo para activar el hill-start (0.06 ~ 3.4 grados de subida)
    float ManeuverHillHoldTicks   = 8.0;  // (legacy, sin uso) el hold ahora es por RPM (rpmClutch), no por tiempo
    float ManeuverHillReleaseRpm  = 3000.0; // piso de RPM para soltar el handbrake en el hill-start (Sonom4n soltaba a ~3600). Se usa max(rpmClutch, esto) -> per-vehiculo con piso
    float ManeuverHillGraceTicks  = 40.0; // ticks (@2Hz = 20s) de margen: el AR NO dispara durante el hill-start; despues es la red
    float ManeuverStopDecelFactor = 0.85; // margen del freno universal para el cap de velocidad (MISMO que el endpoint, que frenaba bien)
    float ManeuverSpeedGain       = 0.12; // throttle por km/h de deficit al perseguir la velocidad objetivo
    float ManeuverBrakeGain       = 0.20; // freno por km/h de exceso sobre la velocidad objetivo (0.4 sobre-frenaba -> pasitos; el pure-pursuit de velocidad ya anticipa el frenado suave)
    float ManeuverSlopeThrottleGain = 1.0; // feedforward de gravedad al acelerar en SUBIDA (trepar fiel; el freno slope-aware ya usa la pendiente)
    float ManeuverCreepKmh          = 1.5; // vel minima de reptado hasta la pose mientras signed>StopTol (acotada por vAllow); evita pararse corto cuando vRec lee 0
    float ManeuverCreepHoldM        = 2.5; // a menos de esta dist de la pose, ManeuverControl le pide a UpdateLegBounds que NO cierre por min-aprox (deja que EL clave preciso). Solo cerca -> no traba el arranque
    // CROSS-TRACK (2026-08-16, Sonom4n "velocidad Y trayectoria"): pega a Boris a la linea grabada (no solo copia el volante).
    float ManeuverCrossTrackGain = 0.50; // correccion por metro de offset lateral (0.65 no ayudo claro el residuo lateral; el lateral se ataca mejor de otra forma la proxima)
    float ManeuverCrossTrackMax  = 0.60; // tope de la correccion
    float ManeuverCrossTrackSign = 1.0;  // signo de la correccion (flip a -1.0 si empuja para el lado equivocado)
    float ManeuverCrossTrackFullKmh = 3.0; // vel a la que el cross-track llega a full; a v~0 se anula (pre-steer/despegue limpio, sin over-steer)
    // ARCO LATERAL EN EL CREEP-IN (2026-08-17, Sonom4n "el iman con arco lateral"): centra el residuo lateral de los stops
    float ManeuverLateralArcM     = 2.5;  // zona del arco lateral. OJO: a creep-speed el volante ROTA mas que TRASLADA, y con full-lock en 2.5m Boris solo traslada ~0.34m -> el boost fuerte SOBRE-ROTA (wp52 2.43m/11.9deg). Centrar el lateral necesita mas distancia (approach), no el creep-in
    float ManeuverLateralArcBoost = 1.0;  // 1.0 = arco DESACTIVADO (sobre-rotaba). El lateral se ataca sobre el approach, no aca
    float ManeuverLateralArcMax   = 0.60; // = CrossTrackMax -> sin efecto
    float ManeuverCrossTrackRevScale = 0.60; // en REVERSA el cross-track. Ahora el arco viene del RUMBO (no del volante), asi que el cross-track puede corregir POSICION sin romper la forma -> subido para centrar la reversa (era 0.30 = arco ancho ~1.2m off)
    // DIRECCION POR RUMBO (2026-08-16, Sonom4n "interpretar tu grabacion tal cual = seguir tu POSE, no copiar el volante"):
    // steer para igualar tu heading grabado (estable a v~0 como el cruise sigue el camino), en vez del front-wheel FF.
    bool  ManeuverUseHeading    = true;  // true = seguir tu RUMBO grabado; false = copiar tu front_wheel (fallback)
    float ManeuverHeadingSteerK = 0.033; // ganancia de volante por grado de error de rumbo (30 deg -> ~full lock)
    float ManeuverHeadingLeadM  = 2.0;   // (legacy) reemplazado por el dual-lookahead corto+largo
    // DUAL LOOKAHEAD DE RUMBO (2026-08-16, Sonom4n "uno largo y uno corto, que calcule entre uno y otro y le de tiempo a ejecutar"):
    float ManeuverHeadingLeadShortM = 1.0; // lead CORTO = precision (no deriva)
    float ManeuverHeadingLeadLongM  = 5.0; // lead LARGO = anticipa el giro (Boris arranca a girar antes, tiene TIEMPO de completarlo alineado)
    float ManeuverHeadingLongWeight = 0.5; // blend entre el corto y el largo (0=solo corto, 1=solo largo)
    float ManeuverHeadingFFScale = 1.0;  // FF+FB: cuanto de tu volante grabado se SUMA al heading (giros bruscos donde el heading proporcional under-turnea; recta FF~0)
    float BreakawayRampStart   = 0.15;   // desde donde arranca la rampa
    float BreakawayRampStep    = 0.06;   // cuanto sube por tick (500 ms) hasta que se mueve
    float BreakawayRampMax     = 0.95;   // tope: si con esto no se mueve, esta trabado de verdad -> AR
    bool  PoseGateEnabled  = true;
    float PoseGateTolDeg   = 22.0;    // tolerancia angular para dar la transicion por cumplida
    float PoseGateSteerK   = 0.045;   // volante por grado de error angular mientras alinea
    float PoseGateThrottle = 0.30;    // gas de rotacion (en reversa) mientras alinea
    int   PoseGateMaxTicks = 30;      // tope de seguridad (~15 s): si no alinea, sigue igual y que actue el AR
    bool  CoastGuardEnabled  = true;
    // CORRIDOR LEARNER (2026-07-29): experimento viejo (Boris corrige repitiendo tomas). YA NO SE USA y venia
    // ENSUCIANDO las tomas persistentes que corriamos una tras otra (derivaba el corredor entre corridas ->
    // resultados que cambiaban solos). APAGADO por default. El learnshift persistido se resetea aparte.
    bool  CorridorLearnerEnabled = false;
    // STOP-LEARNER (2026-07-29): gemelo LONGITUDINAL del corridor learner. Mismo problema -> aprende un
    // brake-point bias por parada, lo PERSISTE (_stopbias.csv) y lo aplica en MODO PARKING la corrida
    // siguiente (distRemaining -= GetBias) -> el punto de frenado cambia entre corridas -> la 1ra clava
    // 0.02m (bias pristino) y las siguientes DRIFTAN. Es el que seguia ENSUCIANDO seq1. APAGADO: el iman
    // de endpoint es ahora el controlador de detencion (feedforward vehicle-agnostic), no hace falta bias.
    bool  StopLearnerEnabled = false;
    float CoastGuardMargin   = 0.80;       // exigir que el planeo cubra la distancia con 20% de sobra
    float CoastGuardThrottle = 0.22;       // gas de sostenimiento mientras no entre en rango de planeo.
    // 0.35 sostenia DE MAS: medido 20/07, Boris entraba a los intercambios a 9-11 km/h donde el humano
    // grabo 6 -> llegaba pasado y se iba largo (T1 2.02 m). 0.22 apunta a acompanar la velocidad grabada.
    float ReverseApproachGateKmh = 1.2;    // velocidad objetivo de ARRIBO a la transicion (justo bajo el gate 1.5)
    float ReverseApproachNudge   = 0.40;   // piso de throttle p/ MANTENER la aprox (no coastear a stall) + re-arrancar si se clavo
    float ReverseApproachHoldKmh = 6.0;    // no dejar caer la vel de aprox por debajo de esto hasta llegar al punto (vos ibas 4-6)
    // OVERSPEED THROTTLE CUT (2026-07-12): cuando Boris ya SUPERA la velocidad objetivo (pintada/curvatura),
    // cortar el gas y (si se pasa mucho) meter freno proporcional -> que coastee/frene hasta el target en
    // vez de sostener velocidad. Sin esto, en SUBIDA el SlopeAssist + el slopeForce del InverseModel fuerzan
    // throttle aunque este overspeed -> nunca baja a la velocidad de curva (5-7 km/h pintados). Gateado OFF
    // para no tocar el baseline del bus; ON en rutas dibujadas/lentas. deadband=margen antes de cortar,
    // brakeGain=freno por km/h de exceso (sobre el deadband), cap 0.5. 2026-07-12.
    bool  InverseModelOverspeedCut = false;
    float OverspeedCutDeadbandKmh = 1.5;
    float OverspeedCutBrakeGain = 0.06;
    // El FRENO del overspeed-cut solo actÃºa en llano/bajada (slope < MaxUphill). En SUBIDA no frena: la
    // gravedad ya desacelera y frenar cuesta arriba mata el enviÃ³n -> bog -> AR (caso @170m +14%). En BAJADA
    // hay que frenar fuerte (la gravedad empuja a la curva; freno flojo=0.5 no alcanzaba, entraba a 26 a la 90).
    float OverspeedCutBrakeCap = 0.5;        // tope del freno; ~0.85 en bajadas con curvas
    float OverspeedCutBrakeMaxUphill = 0.03; // slope > esto = subida -> no frenar
    // CLIMB ASSIST (2026-07-12): en trepada EMPINADA y LENTA el overspeed-cut + el throttle suave del
    // InverseModel bogan el motor (7 km/h pintados en +15% -> cae a 0 -> AR). Aca: (a) NO cortar gas en
    // trepada empinada (necesita el envion; la gravedad ya frena), (b) pisar gas a fondo para trepar sin
    // clavarse. Mejor pasarse del 7 pintado que clavarse en 0. Gateado. slope>Slope Y kmh<MinKmh ->
    // salta el cut Y throttle>=Throttle. El +7.9% de una curva-en-subida (<Slope) NO lo dispara -> sigue
    // respetando la velocidad pintada ahi. 2026-07-12.
    bool  ClimbAssistEnabled = false;
    float ClimbAssistSlope = 0.10;
    float ClimbAssistMinKmh = 10.0;
    float ClimbAssistThrottle = 0.95;
    // GUARD DE STOP PARA CLIMBASSIST (2026-08-05, Sonom4n). ClimbAssist solo mira slope+velocidad, sin conciencia
    // del stop de fin de tramo. En la rampa (subida) del galpon disparaba 0.95 en los ultimos ~2.5m y lanzaba a
    // Boris PASADO el endpoint (Hatchback +1.26m). Dentro de este radio del m_LegEnd (que SIEMPRE es una parada),
    // ClimbAssist NO dispara -> trepa la aproximacion pero suelta antes del stop, y el freno de endpoint clava
    // limpio. 0 = sin guard (comportamiento historico).
    float ClimbAssistStopGuardM = 4.0;
    // SLOPE BASELINE (2026-07-12): el slopeIM se medÃ­a entre wps ADYACENTES (~0.6m). En rutas dibujadas el
    // SurfaceY por-wp tiene ruido -> a 0.6m el signo se da vuelta (leÃ­a +subida en bajadas) -> SlopeAssist
    // metÃ­a gas fantasma bajando a las curvas. Medir sobre base larga (~6m centrada en el wp actual)
    // rechaza el ruido y da la pendiente REAL. 0 = base adyacente (viejo). ~6 rec en rutas dibujadas.
    float SlopeBaselineM = 0.0;
    // LOG de diagnÃ³stico de curva: imprime de dÃ³nde sale el throttle cuando va pasado (kmh>target+3). OFF salvo test.
    bool  LogCornerApproach = false;
    // Tope de velocidad (km/h) en recta para follow_path. La optima por curva nunca lo supera.
    // Subido 50 -> 100 (2026-06-28): a 50 estrangulaba las rectas en modos 2/3 (la toma
    // EXAMPLE04-M1 grababa hasta ~90 km/h y Boris no pasaba de ~47). 100 no es cuello de
    // botella; FollowPathLatAccel sigue frenando las curvas. Overrideable por ruta.
    float FollowPathMaxKmh = 100.0;
    // TOPE POR VEHÃCULO (2026-07-12): el max real lo pone el AUTO (redline en la marcha mÃ¡s alta, leÃ­do de
    // SU config), no un nÃºmero fijo. Con esto, la velocidad objetivo de cada wp se re-capa a la top-speed
    // fÃ­sica del vehÃ­culo al spawnear -> manda la pintada + la fÃ­sica (un deportivo llega a 180-200, un Olga
    // a ~30). FollowPathMaxKmh queda como techo duro de respaldo. Gateado OFF (no toca el baseline del bus).
    bool FollowPathMaxFromVehicle = false;
    // Espaciado (en waypoints) para medir la curvatura R en follow_path. Medir con 3 puntos
    // ADYACENTES (span=1) capta cada micro-wiggle de la grabacion -> R ruidoso -> target salta
    // 50<->bajo (validado 2026-06-20). Espaciar (i-span, i, i+span) promedia sobre el arco real
    // de la curva -> R suave -> desaceleracion anticipa bien antes del gancho. Default 5.
    int   FollowPathCurveSpan = 5;
    // Span FISICO (metros) para medir la curvatura, en vez de indices. RAIZ del ruido de curvatura
    // en grabaciones densas (2026-07-07): FollowPathCurveSpan es un conteo de WAYPOINTS; en una
    // toma 10Hz (~0.33m/wp) 11 indices = 3.6m fisicos -> el circulo de 3 puntos sobre la escalera
    // de posicion EXPLOTA (kappa>1 = radio<1m, imposible) -> el cruise frena contra curvas fantasma.
    // Medir sobre +-CurveSpanM metros lo estabiliza (validado offline: TV 69->7 en EXAMPLE03).
    // >0 activa el span-en-metros; 0 = comportamiento viejo (indices). Opt-in para no tocar rutas
    // ya validadas (ej. baseline bus). 8m recomendado. Config-as-manual.
    float FollowPathCurveSpanM = 0.0;
    // Suavizado (moving-average) del perfil de velocidad de follow_path. La velocidad por curvatura
    // da ESCALONES (recta 50 <-> curva bajo) -> el PID acelera "por tiempos" (jerky, falta feel humano).
    // El MA centrado rampea las transiciones: desacelera ANTES de la curva (la baja se filtra hacia atras)
    // y acelera gradual a la salida = anticipatorio = mas "Boris". 0=off, 8=default. Ventana en waypoints.
    int   FollowPathSpeedSmooth = 8;
    // MODO 3 (reference-assisted): si true, la velocidad objetivo = min(velocidad GRABADA del humano,
    // limite de curva por config). La grabacion humana lleva el conocimiento de la RUTA (donde frenar,
    // el perfil suave probado) -> seguis su perfil PERO capeado por la fisica de TU vehiculo (grip). Recupera
    // el "feel humano" (Boris) que Modo 2 puro pierde al re-derivar de cero, y evita el overshoot (el humano
    // no se excedia en la curva). Requiere una ruta GRABADA (con perfil de velocidad); para rutas escaneadas
    // (solo geometria) dejar en false (Modo 2 puro). Default false.
    bool  FollowPathUseReference = false; // LEGACY: el runtime NUNCA lo leyo. El cap real por velocidad grabada lo hace FollowPathCapByRecording (abajo).
    // MODO 2 (geometria capeada por grabacion): si true, en el bloque FollowPath la velocidad
    // objetivo = min(velocidad por curvatura, velocidad GRABADA del humano en ese wp). Respeta el
    // conocimiento del camino del humano (pueblo, lomada ciega, ripio) que la geometria pura no ve,
    // pero sigue siendo vehicle-agnostic (gear/throttle por InverseModel). Lo setea el converter SOLO
    // en Modo 2. Modo 3 (false) = geometria libre, sin cap. El modo aproximacion es inerte si este
    // flag esta en true (en Modo 2 la grabacion ya desacelera; approach es solo para Modo 3).
    bool  FollowPathCapByRecording = false;
    // Curvature steering BOOST: en curva (mode=normal, config-derived) amplifica el steering del
    // Stanley x(1 + CurvatureSteerBoost*bendFrac). El receptor eAI sub-gira en 90s (el steering
    // nunca satura -> se abre); comandar mas fuerte fuerza el compromiso. Mismo signo del Stanley
    // (seguro, no inventa direccion). 0 = off. Capeado a +-1. bendFrac = bend/90deg.
    float CurvatureSteerBoost = 0;
    // Inverse Model (Capa 3+4 del framework v2). Cuando true, el throttle/brake
    // se computa desde target_speed via PID + modelo inverso del vehiculo en
    // vez de replicar target_throttle/target_brake del recording. Vehicle-agnostic.
    // Validado offline con simulator.js (closure tests = 0.000 cross-vehicle).
    // Default false â†’ comportamiento v1 (replay-based). Opt-in seguro.
    bool UseInverseModel = false;
    // PID gains opcionales para el Speed PID de Capa 3. -1 = default (Kp=0.4, Ki=0.05).
    float InverseModelKp = -1;
    float InverseModelKi = -1;
    float InverseModelKd = -1;
    // Path smoothing window size (moving average sobre pos de wps al cargar).
    // 0 = desactivado, 3 = suave, 5 = moderado (default), 7 = agresivo.
    // Memoria 1.0: smoothing previo quedo "feo" â€” iterar con cuidado.
    int PathSmoothWindow = 5;
    // Capa 5 (gear selector): rpmMin threshold para shift up.
    // false = rpmMin Ã— 1.3 (default, mas conservador, Boris en gears bajos en cruise)
    // true  = rpmMin Ã— 1.0 (gear amortiguado, Boris en gears altos en cruise, mas estable)
    // Experimento matriz 2x2 (2026-06-05): saber si "gear amortiguado" reduce microvolantazos en recta.
    // Default false -> true (2026-06-28): con Ã—1.3 el SelectGear subia de marcha demasiado
    // temprano (RPM lejos del redline -> motor sin torque -> velocidad capeada ~47). Ã—1.0
    // mantiene marchas mas bajas / motor con vueltas -> torque -> Boris llega mas alto.
    // Overrideable por ruta.
    bool InverseModelLowRpmMin = true;
    // Smoothing del targetSpeed via moving average antes de feedearlo al PID.
    // El recording captura tu velocidad real con noise (micro fluctuaciones por inputs).
    // Sin smoothing: PID persigue cada micro-cambio entre wps consecutivos â†’ throttle/brake oscilan.
    // Con smoothing: speed estable entre wps consecutivos â†’ PID converge limpio.
    // 0 = off, 5 = moderado (default), 10 = agresivo.
    // MÃ¡s seguro que PathSmoothWindow (no toca posiciones, solo la velocidad escalar).
    int TargetSpeedSmoothWindow = 0;
    // AutoRecovery: cuando Boris se queda atascado, teleportar adelante N wps.
    // Garantiza el contrato spatial-fidelity (Boris siempre llega) y CLEANEA la data
    // (sin necesidad de asistencia humana con H, las tomas son 100% framework).
    // Cada evento logueado para pattern analysis posterior (heatmap de fallas).
    bool  AutoRecoveryEnabled = false;
    float AutoRecoveryStuckTimeS = 10.0;     // segundos de "stuck" antes de teleport
    int   AutoRecoveryAdvanceWps = 5;        // cuantos wps adelante teleportar
    float AutoRecoveryCooldownS = 8.0;       // minimo entre teleports (evita spam)
    int   AutoRecoveryMaxPerMission = 0;     // 0 = ilimitado, X = falla mision si supera
    // Safety SEPARADA del AutoRecovery: si no avanza ningun wp por N segundos, fuerza
    // un avance de indice (NO teleporta, solo empuja el wp). Corre aunque AutoRecovery
    // este OFF. 0 = usar default del codigo (60s). Subir mucho para deshabilitar de hecho.
    float StuckAdvanceTimeoutS = 0;
    // "ParedÃ³n" cruise â€” corredor no-lineal sobre el Stanley lateral.
    // SOLO se aplica en mode=="normal" (no toca parking ni reverse).
    // Deadband: si lat_dev < este valor, no perturba (Boris vive de inercia, no microvolante).
    //   0 = off (Stanley puro), 0.5 = recomendado.
    // KGain: multiplicador del offset que feedea a Stanley antes de atan.
    //   1.0 = sin cambio. <1 = correccion mas suave. >1 = mas agresivo.
    // Damp: D-gain sobre el rate of change del offset.
    //   0 = off. 0.3 = damping moderado, kills zigzag.
    float CruiseLateralDeadband = 0.0;
    float CruiseLateralKGain = 1.0;
    float CruiseLateralDamp = 0.0;
    // STANLEY SOFTENING (k_soft): termino sumado a la velocidad en el denominador del Stanley
    // forward -> crossCorrection = atan(K*offset / (v + StanleySoftening)). Mata la inestabilidad
    // 1/v a baja velocidad (zigzag al ACELERAR, sobre todo Modo 1 con su acelerada punchy grabada):
    // baja la ganancia suave cuando v es chico, y a alta velocidad se vuelve despreciable (no cambia
    // lo que ya anda). Solo forward (reverse tiene su propio anti-espiral). 0 = off (comportamiento
    // actual). Recomendado para probar: 1.5-2.0 (m/s). El estandar del Stanley controller (Stanford).
    float StanleySoftening = 0.0;
    // Calibracion del centro del vehiculo respecto al path.
    // El pivot del modelo (GetPosition()) puede no coincidir con el centro geometrico.
    // Boris tiende a quedar lateralmente sesgado (e.g. -0.3m izquierda en V3S/bus).
    // Aplica un shift al offset antes del calculo Stanley: + = mueve Boris a la DERECHA, - = IZQUIERDA.
    // Default 0.0 = sin compensacion. Wizard puede auto-detectar comparando ai_run vs recording.
    float CruiseLateralCenterOffset = 0.0;
    // Direct Replay (MANIOBRA mode): a partir de este waypoint index, el
    // framework bypassea Stanley/cruise/FF y aplica target_steering/throttle/
    // brake del recording al pie de la letra. Para maniobras coreograficas
    // (parking fino, drifts, sequences de stops cercanos) donde el controlador
    // no llega a la precision necesaria. -1 = deshabilitado (comportamiento
    // normal con Stanley + cruise predictivo en toda la ruta).
    int DirectReplayFromWaypoint = -1;
    // Cap de velocidad al entrar a bloque mode=maniobra (durante recording
    // marcado con NUMPAD .). Cruise predictivo detecta wp futuro maniobra y
    // desacelera a min(targetSpeed grabado, cap). 18 km/h default â€” suficiente
    // para rotondas con radio chico (Cherno), bajar para esquinas 90Â° muy
    // cerradas. parking usa 12 km/h hardcoded.
    float ManiobraTargetSpeedCap = 18.0;
    // MODO APROXIMACION: velocidad objetivo (km/h) a la que Boris debe LLEGAR al
    // final del bloque mode=approach (= la entrada de la maniobra). Durante el bloque
    // approach el runtime hace una RAMPA LINEAL desde la velocidad ACTUAL de Boris (la
    // que tenga al entrar) hasta este valor, sobre la distancia approach->maniobra, para
    // que entre LIMPIO a la maniobra sin frenazo/derrape. SOLO Modo 3 (en M1/M2 la grabacion
    // ya desacelera -> approach inerte). FALLBACK: la rampa apunta a la velocidad GRABADA del humano
    // en la entrada de la maniobra (cada curva su velocidad, ej 14.5 en subida); este valor solo se
    // usa si no hay dato grabado valido en ese wp. 20 km/h default.
    float ApproachExitKmh = 20.0;
    // APPROACH AUTOMATICA (Modo 3): si true, el framework NO necesita la zona marcada a mano
    // (NUMPAD 0). Escanea adelante el proximo bloque maniobra y ejecuta FRENO PREDICTIVO
    // (vÂ²=uÂ²-2as) que lleva a Boris a la velocidad grabada de entrada de esa maniobra, arrancando
    // a frenar en el punto justo PARA SU VEHICULO (la distancia escala con vÂ² -> un auto rapido se
    // da pista larga, un camion corta). El converter lo setea segun la eleccion del wizard
    // (Grabada=false / Automatica=true). Si hay zona approach marcada, esa (grabada) tiene prioridad.
    bool  ApproachAuto = false;
    // Desaceleracion COMODA (m/sÂ²) para el calculo del freno predictivo de ApproachAuto. 2.5 ~ 0.25g,
    // frenada suave. Mas alto = pista mas corta (frena mas tarde y mas fuerte); mas bajo = mas larga.
    float ApproachAutoDecel = 2.5;
    // === AR_OnWay â€” escudo contra OBSTRUCCIÃ“N EXTERNA del mundo (otro vehÃ­culo en el camino). Ver
    // project_ar_onway. Distinto del AR clÃ¡sico (AutoRecoveryEnabled, stuck-based). NO salva a Boris
    // de Boris; lo cuida del mundo. Toggle por ruta/quest (transporte robusto vs misiÃ³n interceptable).
    bool  ObstacleSlow     = false;  // FASE 1: freno predictivo ante un vehÃ­culo detectado adelante (no lo ramea, se detiene antes). Solo M2/M3.
    bool  ObstacleEscape   = false;  // FASE 2 (pendiente): teleporta al primer wp limpio pasado el obstÃ¡culo si persiste o hay golpe.
    float ObstacleScanDist = 50.0;   // m, cuÃ¡n adelante en el path escanea vehÃ­culos.
    float ObstacleStopDist = 15.0;   // m, dÃ³nde se detiene antes del obstÃ¡culo (margen para un vehÃ­culo largo).
    float ObstacleEscapeWaitS = 6.0; // s que Boris espera frenado ante un obstÃ¡culo persistente antes de teleportar (fase 2).
    float ObstacleDecel = 4.5;       // m/sÂ² para el freno ANTE OBSTÃCULO (mÃ¡s firme que ApproachAutoDecel: es seguridad, no confort). Igual escala a mÃ¡ximo si estÃ¡ muy cerca.
    float ObstacleCorridorHalf = 2.3; // m, medio ancho del CARRIL que cuenta como bloqueo. Un vehÃ­culo con offset lateral MAYOR (mordiendo la banquina) NO bloquea â†’ Boris pasa. â‰ˆ medioAnchoBoris + medioAnchoObstÃ¡culo + margen.
    float ObstacleEscapeResumeKmh = 10.0; // km/h a la que Boris ARRANCA tras el escape/teleport (suave), en vez de la velocidad grabada del wp (que puede ser alta).
    // Forward parking: cap de velocidad. 0 = usar el default del codigo (15 km/h).
    // Espejo de ManiobraTargetSpeedCap; antes el parking usaba 15 hardcoded.
    float ParkingTargetSpeedCap = 0;
    // Slope compensation: en subidas suma throttle, en bajadas resta. Pitch
    // computado del path (dy/dist entre wps). Boris pierde velocidad en
    // subidas cuando el PID asume terreno plano. SlopeGain=1.0 = compensacion
    // completa (gravedad neutralizada en throttle), 0.5 = parcial.
    bool  SlopeCompensationEnabled = true;
    int   SlopeLookaheadWps = 5;
    float SlopeGain = 1.0;
    // Slope-aware lateral: compensa el bias lateral asimetrico que aparece segun
    // pitch del path (algunos vehiculos pull a la izquierda en subida, etc).
    // Aplica un CenterOffset extra proporcional al pitch lookahead.
    // 0 = OFF (default). 1.0 = subida 5Â° â†’ offset +0.07 (~1.3m derecha si sensitivity ~19m/u).
    // Validado 2026-06-07 Impreza: bias observado -1.39m en subida, +0.04 en bajada.
    float SlopeLateralGain = 1.0;
    // 2026-06-07: ModeEntrySnap â€” al entrar a un bloque maniobra/parking/reverse,
    // si Boris esta a menos de N metros del wp logico, snap a la posicion + heading
    // exacta del recording. Garantiza entry consistente para maniobras precisas.
    // CRITICO para parking real (puerta de casa, galpon) donde 0.5m de drift rompe.
    // Imperceptible a baja velocidad (cap ~20 km/h en parking/maniobra).
    // DEFAULT false (Sonom4n 2026-07-03, "mata el snap, no quiero que lo atrape"): el teleport de
    // alineacion es un parche que ademas ni se dispara si Boris esta lejos (MaxDist 0.5m; venia
    // 3.43m off). El control genuino (parking tight + reverse rear-steer que clava el heading a 0.7deg)
    // posiciona SOLO, sin teleport. Reversible por _hdr. Ver [[project_snap_minimize_visual_teleport]].
    bool  ModeEntrySnapEnabled = false;
    // Distancia maxima en metros para aplicar el snap. < 0.5 = casi invisible.
    // Si Boris esta mas lejos que esto, no snap â€” AR / cruise debe acercarlo primero.
    float ModeEntrySnapMaxDist = 0.5;
    // 2026-06-08: Anti-rollback en pendientes. En parking/maniobra con velocidad < 0.5 km/h
    // y pitch > 0.05 rad (~3Â°), aplicar SetHandbrake(1.0) + SetBrake(1.0) para clavar el auto
    // contra el terreno. Al querer arrancar, los frenos se mantienen hasta que EngineGetRPM
    // cruza rpmClutch del config â€” momento en que el embrague tiene torque suficiente para
    // vencer la gravedad. Tambien resetea el integral del PID para evitar windup a 0 km/h.
    bool  AntiRollbackEnabled = true;
    // Pitch minimo en radianes para activar anti-rollback (default 0.05 = ~2.86Â°).
    // Por debajo de este pitch, parking funciona como antes (sin handbrake).
    float AntiRollbackPitchThreshold = 0.05;
    // Ganancia Stanley en modo parking. Nissan calibrado en 3.0. T6 / vehiculos
    // mas pesados o con respuesta de volante distinta necesitan menos.
    // -1 = usar constante STANLEY_K_PARKING del codigo.
    float ParkingStanleyK = -1;
    // EXIT-TIGHTEN (camino A, 2026-07-24): en la SALIDA de curva la curvatura cae y StanleyCurvatureAware
    // relaja K justo cuando Boris quedo ANCHO (medido SEDAN: apex clavado 0.1-0.4m, salida deriva a 0.7m
    // -> "abrio la curva"). Sumamos un boost de K lateral proporcional al |offset| que quede fuera de la
    // deadzone: lejos de la linea tira mas fuerte, sin importar la curvatura. En RECTA/CRUISE el offset es
    // ~0 -> boost ~0 -> cruise INTACTO. Se desvanece a alta velocidad. Solo forward/cruise normal.
    bool  ExitTightenEnabled = false;  // REVERTIDO (2026-07-25): backfired. Subir K lateral en curva a baja v
                                       // SOBRE-corrige/corta -> tiro a Boris 3.3m off en la curva wp219 y no
                                       // recupero mas (endpoint 4.62m). El lever correcto NO es K, es el lookahead.
    float ExitTightenDeadM   = 0.35;   // por debajo NO corrige (piso de ruido del apex 0.15-0.42m)
    float ExitTightenGain    = 3.0;    // K extra por metro de offset excedente
    float ExitTightenMaxK    = 2.0;    // tope del boost (K base=1.0, parking=3.0)
    float ExitTightenLowKmh  = 25.0;   // <= : boost pleno (curvas se toman <20 km/h)
    float ExitTightenHighKmh = 45.0;   // >= : boost cero (cruise recto intacto)
    // Peso del feedforward en parking. Nissan calibrado en 0.6. Bajar para
    // vehiculos donde la anticipacion gener overshoot oscilatorio.
    // -1 = usar 0.6 del codigo.
    float ParkingFFWeight = -1;
    // Ganancia Stanley en modo reverse. Nissan calibrado en 0.8.
    // -1 = usar constante STANLEY_K_REVERSE del codigo.
    float ReverseStanleyK = -1;
    // Piso de velocidad (m/s) para la correccion lateral Stanley en reverse. La correccion
    // K*offset/v explota a baja velocidad (1/v) -> zigzag -> espiral. Pisar v a este minimo
    // la mantiene suave aunque Boris se frene. -1 = usar 2.0 del codigo. (Sonom4n 2026-06-24)
    float ReverseStanleyMinSpeed = -1;
    // === MODELO BICICLETA EN REVERSE (rear-steer archetype, 2026-06-12) ===
    // Wheelbase = distancia entre ejes (m). Usado por el feedforward de curvatura
    // en reverse: delta = atan(L * kappa). Golf ~2.6-2.7. 0 = usar default del codigo.
    // El wizard puede medirlo del bounding box (DumpRuntimeProperties bbox Z).
    float Wheelbase = 0;
    // Signo del feedforward en reverse. La fisica: en reverse v es negativa, el yaw
    // rate omega=(v/L)tan(delta) flipea -> el volante va al lado OPUESTO que en forward
    // para la misma curva. Default -1 (flip rear-steer). +1 = sin flip (debug/empirico).
    // 0 = usar default del codigo (-1).
    float ReverseFFSign = 0;
    // Angulo de steering maximo (rad) para normalizar el feedforward del modelo
    // bicicleta a [-1,1]. Auto reales ~0.52-0.61 rad (30-35deg). 0 = default 0.6.
    float ReverseFFMaxSteerRad = 0;
    // Peso del feedforward en reverse. Reverse es non-minimum-phase ("delay" en la
    // direccion): la anticipacion importa MAS que en forward, el gain reactivo debe
    // ser BAJO. Subir esto pre-steerea mas fuerte la curva. -1 = usar ParkingFFWeight.
    float ReverseFFWeight = -1;
    // Umbral (m) del gate "discrete input pattern" en REVERSE. El gate pone
    // steering=0 cuando la grabacion venia costeando (gas/freno ~0), pero SOLO si
    // Boris esta dentro de este offset del path. Si deriva mas que esto, el volante
    // sigue corrigiendo (sino en reverse non-minimum-phase la deriva se amplifica y
    // Boris se clava â€” bug 2026-06-12 stall en wp ~1006 con offset ~1.8m). 0 = 0.5m.
    float ReverseSteerGateOffset = 0;
    // Piso del factor steer-then-throttle en REVERSE. El factor reduce throttle a
    // mas steering (1 - |steer|*2.5), pero NO debe llegar a 0: en reverse velocidad
    // cero = autoridad de direccion cero (sin movimiento no rota). Sin piso, doblar
    // fuerte (steer 0.4) frena en seco JUSTO en la curva y Boris se clava (bug
    // 2026-06-12, stall en wp ~1050). El piso mantiene un crawl que permite rotar.
    // El lap que entraba corria a factor ~0.45. 0 = usar default del codigo (0.35).
    float ReverseSteerThrottleFloor = 0;
    // Clamp del |steering| en REVERSE. La primera correccion saturaba a 1.0 (full
    // lock) y se pasaba del eje (overshoot) -> en non-minimum-phase diverge y Boris
    // se clava (bug 2026-06-12). El unico lap que completaba uso pico 0.40. Cap
    // modesto fuerza el regimen suave que SI funciona (literatura: en reverse gain
    // alto OSCILA, no corrige mejor). 0 = usar default del codigo (1.0 = OFF, 2026-06-13:
    // el anti-overshoot real es FOLLOW-RECORDING, no cortar el full lock que la maniobra pide).
    float ReverseSteerMax = 0;
    // I-TERM anti-drift de reverse (2026-06-25): ganancia del integral del offset lateral.
    // Mata el drift sistematico (ej galpon ~0.63m) sin overshoot, separando el regimen
    // permanente (I) del transitorio (P, capeado). 0 = APAGADO (opt-in). Arrancar conservador (~0.001-0.005).
    float ReverseLateralKi = 0;
    // Umbral para SEGUIR EL VOLANTE GRABADO en reverse (recording-as-manual). Si el
    // humano metio |steering| > este valor, Boris copia su volante (no Stanley reactivo
    // que satura y se pasa de eje). Mas bajo = el recording manda mas; Stanley queda solo
    // para tramos donde el humano costeo recto (recorded~0). 0 = usar default (0.2).
    float ReverseRecordedSteerThreshold = 0;
    // Cap (km/h) de velocidad objetivo en reverse. El viejo MAX_REVERSE_KMH=6 (herencia
    // Nissan) estrangulaba a Boris: el recording pedia 20 km/h para trepar la rampa del
    // galpon con impulso y Boris quedaba a 3-4 -> no subia los 8deg (2026-06-13). Esto es
    // SOLO un techo de seguridad; Boris sigue la targetSpeed grabada por wp (lento en la
    // curva, rapido en la rampa). El wizard lo setea por vehiculo desde el max de reverse
    // del recording. 0 = usar default del codigo (25 = no clampea grabaciones normales).
    float ReverseTargetSpeedCap = 0;
    // Cap de la correccion FINA de Stanley en reverse (blend follow-recording). El volante
    // GRABADO es la base; Stanley solo suma/resta hasta este maximo para corregir offset.
    // Sin esto, en los tramos rectos del recording (steering grabado=0) Stanley manejaba
    // TODO y a velocidad alta desviaba a Boris (no subia la rampa derecho, full-lockeaba al
    // arrancar â€” 2026-06-13). Bajo = Boris mas fiel al recording (mas recto). 0 = default 0.15.
    float ReverseStanleyFineMax = 0;
    // Deadband de HEADING (grados) en reverse. La correccion fina dispara si el offset es
    // grande O si el ANGULO esta torcido > este valor. El patron 2026-06-13 mostro que las
    // que fallan llegan a la puerta SOBRE-ROTADAS (heading 138-141 vs 128-135 las que entran)
    // con MISMO offset -> corregir por angulo temprano evita la diagonal que raspa la puerta.
    // Mas chico = corrige el angulo antes (mas fiel al heading grabado). 0 = default 4 grados.
    float ReverseHeadingDeadbandDeg = 0;
    // === PURE-PURSUIT DE REVERSE (aislado, 2026-08-10, Sonom4n) ===
    // La reversa es NON-MINIMUM-PHASE + rear-steer: un volante chico desvia MUCHO (mas sensible que
    // forward) -> las ganancias de forward sobre-corrigen. El replay+Stanley acumula/satura en la curva
    // -> el LAZO (ver ai_run FT_03). Lo reemplaza un pure-pursuit GEOMETRICO (memoryless, indep. de
    // velocidad/RPM) que apunta desde el EJE TRASERO a un lookahead sobre la traza. Solo corre con este
    // flag ON y mode=='reverse' -> NO toca el controlador de forward. Revert = flag OFF (runtime).
    bool  ReverseUsePurePursuit = false;
    // Lookahead del pure-pursuit de reverse (m). MAS LARGO = mas SUAVE (gain ~ 1/Ld) -> compensa la
    // hipersensibilidad del rear-steer. 0 = default interno (5.0 m). Subir si oscila; bajar si corta.
    float ReversePPLookaheadM = 0;
    // Ld ADAPTATIVO POR CURVATURA en el pure-pursuit de reverse (2026-08-10): sensa el giro del path
    // adelante y ACORTA el Ld en el codo cerrado (clava el apex) manteniendolo LARGO en lo liso/arranque
    // (volantazo suave). Resuelve el tradeoff del Ld fijo (corto=apex pero arranque brusco / largo=al reves).
    // Mirror del UseCurvatureAdaptiveLd del forward. Opt-in (default OFF = Ld fijo).
    bool  ReversePPCurvAdaptive = false;
    // Ld corto (m) para el tramo de MAXIMA curvatura (el codo). El Ld base (ReversePPLookaheadM) es el largo
    // (recto). Se interpola entre los dos segun la curvatura sensada. 0 = default interno (2.0 m).
    float ReversePPLdCurveM = 0;
    // END FREEZE (OnEnd: freeze por default, 2026-06-13). Cuando Boris llega al final
    // de la toma donde el humano se DETUVO (el recording tiene un tail de targetSpeed~0
    // y no hay ningun wp mas rapido por delante), el framework frena a fondo + handbrake
    // y CONGELA al vehiculo en esa posicion, orientado como termino la maniobra. Resuelve
    // el "empuja contra la pared al final": sin esto el floor de progresion (3km/h) + el
    // kick de baja velocidad mantenian a Boris moviendose por el tail parked del recording.
    // Es el hand-off point del scenario engine (futuro OnEnd con hold/respawn/chain/etc).
    // 0 = freeze habilitado (default). 1 = deshabilitado (Boris sigue hasta el ultimo wp sin frenar).
    int EndFreezeDisabled = 0;
    // Hibrido cruise: si el wp grabado tiene |targetSteering| > este threshold,
    // usar el valor grabado en vez del output de Stanley. Captura los pulsos
    // binarios del input keyboard del humano (Stanley produce salida continua,
    // no puede reproducir pulsos). Aplica solo en cruise (mode=="normal").
    // -1 = deshabilitado (Stanley puro). 0.7 = override volantazos >70%.
    float CruiseHybridSteerThreshold = -1;
    // Hibrido throttle: si target.targetThrottle >= este threshold Y target.targetBrake
    // bajo, override el output del cruise predictivo y usar los inputs grabados.
    // Implementa "respect the recording": cuando el humano estaba acelerando, Boris
    // tambien acelera, sin que el cruise predictivo cambie de opinion por anticipacion
    // de targetSpeed downstream. Aplica solo en cruise normal (mode=="normal").
    // -1 = deshabilitado. 0.5 = override cuando human throttle >= 50%.
    float CruiseHybridThrottleThreshold = -1;
    // Peso del feedforward steering en modo cruise (mode="normal"). Stanley reacciona
    // al offset actual; FF anticipa la curva venidera. FF=0.25 default funciona bien
    // en vehiculos con yaw rate medio-bajo (bus, Nissan) pero pre-volantea de mas
    // en vehiculos con yaw rate alto (T6) generando desviacion. Bajar a 0.1 o 0
    // para T6/sport-vans. -1 = usar 0.25 default del codigo.
    float CruiseFFWeight = -1;
    // === Corte de throttle anticipatorio por curvatura (2026-06-10) ===
    // En cruise normal, baja el throttle ANTES de una curva cerrada para que Boris
    // no "acelere-antes-de-girar" (metia gas con el volante derecho, ganaba momentum
    // y entraba mal â€” 2da y 3ra curva del recording). Anticipatorio: mira la
    // curvatura de los wps que VIENEN, no el steering actual (eso ya lo hace el
    // corte reactivo |steering|>0.3). CurveThrottleEnabled=false = baseline previo.
    bool  CurveThrottleEnabled    = true;
    float CurveThrottleLookaheadM = 14.0;   // distancia adelante que escanea (m)
    float CurveThrottleStartDeg   = 35.0;   // bend acumulado donde EMPIEZA a cortar
    float CurveThrottleFullDeg    = 80.0;   // bend donde llega al corte maximo
    float CurveThrottleMinScale   = 0.35;   // throttle x este factor en curva cerrada
    // === PLANT FEEDFORWARD (2026-07-04) â€” feedforward de steering FISICO medido por el receiver ===
    // Reemplaza el heuristico dHead/(PI/2)*CruiseFFWeight por la INVERSA del plant medido:
    //   curvatura kappa -> delta = atan(L*kappa/k) [bicicleta + understeer] -> cmd = delta_deg/gain.
    // Medido en OffroadHatchback (EXAMPLE01, receiver): gain 85.1 deg/unidad, understeer k~0.90.
    // Con el FF fisicamente exacto, el feedback (Stanley) NO carga la curva -> se afloja -> menos
    // zigzag. Forward-only (reverse ya usa su propia bicicleta). false = comportamiento actual intacto.
    bool  UsePlantFeedforward   = false;  // ON = FF plant-exacto en cruise/approach/normal (A/B)
    float PlantSteerGain        = 85.1;   // front_wheel_deg = gain * cmd_steer (medido por rueda real)
    float PlantUndersteerK      = 0.90;   // yaw_real / yaw_bicicleta (compensacion understeer)
    float PlantFeedbackScale    = 1.0;    // multiplicador al Stanley cuando el FF exacto carga (1=igual, <1=afloja)
    // PURE-PURSUIT COMO FEEDBACK DE ERROR PURO (2026-07-25, Sonom4n + Kapania&Gerdes). La variante que la teoria
    // dejo sin probar: FF grabado PRIMARIO (peso pleno, lleva la curva) + el PURE-PURSUIT como correccion,
    // pero restandole su propia componente de curvatura del camino para NO doblarla. feedback = ppReal - ppNominal
    // (real = desde Boris; nominal = desde su proyeccion en la linea con rumbo tangente) = puro error, medido con
    // la mecanica estable del pursuit (lookahead adaptativo, mira adelante -> NO oscila como el cross-track a mano).
    // steering = ffSteer + PlantFeedbackScale * (ppReal - ppNominal). Requiere UsePlantFeedforward ON, UsePurePursuit
    // OFF (para que el bloque pp no lo pise). En la linea feedback~0 -> FF puro (100% tu volante). false = intacto.
    bool  UsePurePursuitAsFeedback = false;
    // CROSS-TRACK EXPLICITO del modo aditivo (2026-07-25, Sonom4n + deep-research). El feedback del corredor
    // corrige RUMBO pero su termino de POSICION queda ~0 (elige el segmento cercano al INDICE, no al punto
    // real -> con indice de-synced no ve el drift). Boris queda PARALELO a la linea pero corrido (medido T1:
    // rumbo ~0 pero offset 2m). Este termino computa el offset FRESCO al punto mas cercano de la traza
    // (busqueda ancha, independiente del corredor) y tira a la linea con forma Stanley atan2(K*e, v). Solo aditivo.
    float AdditiveCrossTrackGain = 0.0;   // K del cross-track explicito (0 = off); ~2 = correccion firme
    // AMORTIGUAMIENTO del cross-track (2026-07-25, deep-research). Un cross-track de posicion PURA oscila
    // (limit cycle). La forma correcta es -K*(e + xLA*Drumbo): el termino de rumbo ANTICIPA (si Boris ya va
    // hacia la linea, corrige menos -> no sobre-pasa). xLA en metros = cuanto pesa el rumbo (mas = mas amortiguado).
    float AdditiveCrossTrackLookahead = 4.0;
    // LOOKAHEAD SPEED-ADAPTIVE del amortiguador (2026-07-25, la causa de la oscilacion en cruise). xLA fijo era
    // muy corto a alta velocidad -> oscilaba. xLA_eff = base + v*Tau (mas largo a mas v = mas amortiguado en
    // cruise, corto en el codo). Es lo que el pure-pursuit hace y por lo que su cruise siempre fue impecable.
    float AdditiveCrossTrackLookaheadTau = 0.0;
    // Formato fiel (2026-07-04): el FF plant-inverso deriva curvatura de headings de segmentos
    // discretos (2 wps) = ruido amplificado (curvatura = 2a derivada). Con esto ON, el heading del
    // horizonte sale de un perfil SUAVIZADO pre-computado (chorda +-ventana) -> curvatura limpia,
    // sin heredar el jitter del muestreo 10Hz. Requiere UsePlantFeedforward. Paso "puntos->trayectoria".
    bool  PlantFFSmoothCurvature = false;
    // Lag-lead del actuador (2026-07-04): el front_wheel rampea al cmd con tau~0.12s (medido). Se
    // compensa con la INVERSA de 1er orden -> cmd += lead*d(cmd)/dt. Adelanta el volante en TODAS las
    // transiciones (flancos), sin tocar el regimen estable. Global. Valor = segundos (~tau). 0 = off.
    float PlantLagLead = 0.0;
    // Fase 2 (2026-07-04): en vez de re-derivar el volante de la geometria, el FF comanda el angulo de
    // rueda EJECUTADO por el humano (grabado en targetFrontWheel via WheelGetDirection). Boris reproduce
    // lo que las ruedas del humano hicieron. Requiere UsePlantFeedforward + toma grabada con Fase 2.
    bool PlantUseRecordedWheel = false;
    // LEAD POSICIONAL del volante grabado (2026-07-25, Sonom4n). Con PlantUseRecordedWheel Boris comanda tu
    // volante en su posicion REAL, pero la rueda fisica TARDA en llegar al angulo (slew) -> ATRASA ~2wp
    // (medido T1: entra tarde, sobre-rota +4, corta adentro -1.25m). Leemos el volante grabado desde N
    // metros ADELANTE de la posicion de Boris -> comando el angulo antes, la rueda slewea, y llega justo
    // cuando Boris esta en el punto. 0 = leer en la posicion exacta. Se clampea al fin del tramo (cusp).
    float PlantRecordedWheelLeadM = 0.0;
    // ADELANTO DE FASE POR VELOCIDAD (2026-07-25, deep-research): forward-predictor del retardo del actuador.
    // Lead extra = v*LeadTau metros (lo que Boris avanza en tau seg de retardo). tau ~= 0.05s actuador + slew.
    // Escala con la velocidad -> a alta v el slew cubre mas distancia. 0 = solo el lead fijo LeadM.
    float PlantRecordedWheelLeadTau = 0.0;
    // Fase 2b (2026-07-04): comandar el INPUT grabado del humano (targetSteering, sin filtrar) en vez del
    // volante ejecutado. El actuador de Boris (mismo tau ~0.12s) lo filtra IGUAL que el del humano -> mismo
    // volante ejecutado -> misma linea CON los taps. Es la senal determinista (el input, no el ejecutado que
    // se doble-filtra). El Stanley (PlantFeedbackScale bajo) trima el drift. LagLead debe ir en 0 (no des-filtrar).
    bool PlantUseRecordedSteering = false;
    // ENVELOPE (2026-07-06): en vez del k CONSTANTE inventado (0.90), usa el understeer(v) APRENDIDO
    // de la demo (BZVehicleEnvelope, per-vehiculo, k(v)=L*kappa/tan(volante_grabado)). El FF computa el
    // volante justo para la velocidad ACTUAL sobre la linea limpia -> generaliza (no replaya el volante).
    bool  PlantUseEnvelope = false;
    // Lookahead del FF computado TUNEABLE (2026-07-06): antes scanDist = v*1.5s con piso 5m HARDCODEADO
    // -> en curvas lentas/cerradas el FF miraba ~45 wp adelante y doblaba TEMPRANO (se anticipaba). Ahora
    // tuneable: bajar el tiempo/piso hace que doble mas AL LLEGAR (position-synced-like). Solo lo necesario
    // para el lead del actuador (~tau). Cero cambio si se dejan los defaults 1.5/5.0.
    float PlantFFLookaheadTime  = 1.5;   // segundos de anticipacion del scan
    float PlantFFLookaheadFloor = 5.0;   // piso en metros del scan
    // PICO DE CURVATURA (2026-07-06): el FF promedia dHead/arco sobre el lookahead -> diluye el pico
    // del apice (incluye la bajada post-pico) -> sub-comanda -> arco abierto ~1m (cruza carril). Con
    // esto ON, usa el MAXIMO de curvatura local de la ventana (perfil suavizado) -> comanda el pico
    // completo en el apice y sigue anticipando la salida. Requiere UsePlantFeedforward (rama computada).
    bool  PlantFFPeakCurvature = false;
    // DESACOPLE VOLANTE/PREDICCION (2026-07-08, de Arma 2 wheeled.pbo config: steerAhead 0.2-0.3 CORTO vs
    // predictTurn 0.8-1.0 LARGO). Nuestro plant-FF mezclaba las dos: scanDistFf = v*PlantFFLookaheadTime
    // escala con velocidad -> el VOLANTE mira lejos -> pre-gira -> CORTA la curva. Arma desacopla: volante
    // lookahead CORTO (sigue el camino pegado, cae al fondo del nodo) + predicciÃ³n larga SOLO para la
    // velocidad (el cruise v=sqrt(latAccel*R) ya lo hace). Con PlantFFSteerLookaheadM>0 el FF usa ESTE
    // lookahead FIJO en metros (corto, ~3-4m) en vez del escalado por velocidad. 0 = comportamiento viejo.
    float PlantFFSteerLookaheadM = 0.0;
    // POSICION-SYNC DE LA FUENTE (2026-07-09, MEDIDO): en cruise normal WAYPOINT_RADIUS=15m -> el indice
    // de avance m_WaypointIndex corre ~15m ADELANTADO de Boris. El volante grabado (PlantUseRecordedWheel)
    // y el FF se toman de ESE indice -> Boris ejecuta la direccion 15m ANTES = anticipa (medido 14m de
    // lead; MAPA y MAPAREC igual, "no es mi replay"). Con esto ON, la FUENTE de la direccion se toma del
    // wp mas cercano a la posicion REAL de Boris (no del indice de avance). NO toca el avance del indice
    // (paradas/scan del colectivo intactos) ni el baseline del bus (que no setea el flag). 0 = viejo.
    bool PlantSteerSourceNearest = false;
    // PURE PURSUIT (2026-07-10, de Arma 2 steerAhead+turnCoef): controlador CLOSED-LOOP que apunta el
    // volante a un punto CORTO adelante SOBRE la linea del path y corrige CADA FRAME -> se auto-corrige,
    // NO se desvia (a diferencia del arco FF/horneado open-loop, que entiende de mas o de menos y drifta).
    // Es el path-follower clasico de robotica y literalmente como Arma no se escapa. Reemplaza el steering
    // FF+Stanley cuando esta ON (forward normal; la velocidad la sigue manejando FollowPath+InverseModel).
    bool  UsePurePursuit = false;
    // BLEND FF COMPLEMENTARIO SOBRE EL PURE-PURSUIT (2026-07-25, Sonom4n). El pure-pursuit da estabilidad pero
    // ADELANTA ~2wp (corta el codo, geometrico). Le sumamos una dosis CHICA del volante GRABADO, leido N
    // metros ADELANTE para compensar la fase, que SOSTIENE la curva donde el pp suelta. Externo/complementario:
    // el pp queda intacto (columna estable), esto se nudgea encima -> volante = pp + w*(grabado_adelantado - pp).
    // Peso bajo -> el pp manda (no diverge como el FF puro). NO toca la ley de control.
    float PurePursuitFFBlend = 0.0;   // 0 = pp puro; ~0.4 = nudge hacia el volante grabado
    float PurePursuitFFLeadM = 0.0;   // metros adelante para leer el volante grabado (0 = posicion real, contra el lead del pp)
    // GATE POR CURVATURA (2026-07-25, Sonom4n): el blend SOLO en curva. En recta el volante grabado es ~0 y el
    // blend mete un limit-cycle (zigzag/bulto que vaga de lugar). Se activa cuando |volante grabado| supera
    // este umbral (grados), con rampa de 0 (umbral) a full (2x umbral). 0 = sin gate (blend siempre).
    float PurePursuitFFMinWheelDeg = 0.0;
    // Ld = distancia de lookahead (m). CORTO = tracking pegado (pero <~1.5*wheelbase oscila); largo = suave
    // pero corta curvas. Speed-scaled: Ld = max(este, 0.4s * v). ~5m base recomendado.
    float PurePursuitLookaheadM = 5.0;
    // BANDA DE SALIDA DEL CUSP reversa->forward (2026-08-05, Sonom4n). Ver CuspExitActive() en BZBusService.c.
    // Saliendo de reversa a forward 'normal' a <CuspExitKmh, pure-pursuit satura el volante (steer~1/Ld con
    // v~0: el indice avanza, el auto no, el vector al punto de mira gira y el steer se dispara y oscila
    // tope-a-tope -> clavado -> AR). Mientras haya un wp reversa/parking dentro de los ultimos CuspExitMaxWps,
    // steereamos por el HEADING GRABADO del wp (estable, del humano) igual que la reversa, gateando pp off.
    // Fiel (usa el recording en la parte dura) y sin constante por-vehiculo -> generaliza a todos.
    bool  CuspExitHeadingBand = true;
    float CuspExitKmh = 5.0;    // umbral de velocidad: por debajo, banda activa
    int   CuspExitMaxWps = 8;   // cuantos wps despues del cusp dura la banda
    // SUPRESION DE EMPUJE EN EL CUSP DE REVERSA (2026-08-05, Sonom4n). Ver el bloque cuspStopSup en BZBusService.c.
    // En el endpoint de la reversa el breakaway/coastBand empujaban throttle hacia el wp forward y lanzaban a
    // Boris 9-22m pasado el cusp -> la pierna cerraba torcida -> clavado (Golf/E60). Mientras la pierna activa
    // siga siendo reversa y Boris este a <CuspStopSuppressM del endpoint con gear FORWARD metido, se corta el
    // empuje y se frena suave para asentarlo en el cusp -> cierre limpio como OFF/Hatchback.
    bool  CuspStopSuppressEnabled = true;
    float CuspStopSuppressM = 6.0;     // radio al endpoint de la pierna reversa donde se suprime el empuje
    float CuspStopSuppressKmh = 1.5;   // si va mas rapido que esto dentro del radio, freno suave para asentar
    // Ld ADAPTATIVO POR CURVATURA (2026-07-11, para 99.9%): la regla del piloto -> Ld LARGO en recta (liso,
    // sin zigzag), CORTO entrando a curva (clava el apex, NO anticipa). El error previo fue adaptar por
    // VELOCIDAD (recta rapida -> Ld corto -> zigzag). Aca se sensa el GIRO del path ADELANTE (CurvatureLdSenseM)
    // y se interpola Ld entre Straight y Curve. Gate propio; el baseline (flag off) no se toca.
    bool  UseCurvatureAdaptiveLd = false;
    float CurvatureLdStraightM = 8.5;   // Ld en recta (largo, suave)
    float CurvatureLdCurveM    = 4.5;   // Ld en curva cerrada (corto, pega al apex)
    float CurvatureLdSenseM    = 12.0;  // cuanto mirar adelante para sensar el giro
    float CurvatureLdTurnRad   = 0.6;   // giro acumulado (rad) sobre SenseM que satura a Ld=Curve (~34deg)
    // Ld CRECE CON LA VELOCIDAD en recta (2026-07-13, "modo 2" de direccion a alta velocidad): piso de Ld ~ Factor*v_ms,
    // atenuado por (1-frac) para NO alargar en curva (ahi manda ldCrv corto). Mantiene tau=Ld/v ~cte -> estable +
    // promedia el jitter de la linea. 0 = OFF. ~0.5 -> Ld~12.5m a 90km/h (tau~0.5s). Sin tocar la linea dibujada.
    float CurvatureLdSpeedFactor = 0.0;
    // RATE-LIMITER del volante (2026-07-11): red de seguridad anti-zigzag. Limita el |cambio| de steering por
    // tick (cmd [-1,1] por tick de 500ms). 0 = OFF. ~0.35 = full-lock en ~1.5s (slew fisico realista).
    float SteerRateLimitPerTick = 0.0;
    // Umbral de velocidad del rate-limiter (2026-07-13): el rate-limiter solo actua arriba de esta velocidad ->
    // clava el volante contra el vaivÃ©n del tick a alta velocidad SIN tocar el modo edit perfecto de abajo. 0 =
    // aplica a toda velocidad (comportamiento previo). ~50 -> solo en la recta rapida.
    float SteerRateLimitMinKmh = 0.0;
    // SEGUIR LA TRAZA COMO LINEA GRABADA (2026-07-11, pedido de Sonom4n): la busqueda del wp mas cercano del
    // pure-pursuit usa [idx-30, idx+5] (backward-biased, alcanza ~18m atras) -> en un CRUCE/LOOP donde la
    // traza pasa cerca de si misma, salta a un wp cercano-pero-de-otra-parte de la secuencia -> Boris "se va
    // a la interseccion". Con esto ON la ventana es ESTRECHA y forward-biased ([idx-5, idx+15]) anclada al
    // indice de avance (monotono) -> NO salta, sigue la traza EN ORDEN como una linea grabada. Ignora los nodos.
    bool  PurePursuitSequential = false;
    // SAMPLE TERRAIN Y (2026-07-12, ultimo ladrillo modo-libre): las rutas dibujadas de cero en el editor
    // vienen con Y=0 (el fondo vial es 2D, sin altura). Con esto ON, al spawnear se samplea la altura REAL
    // del terreno (SurfaceY) en CADA waypoint -> frenado por pendiente y todo correcto. El spawn ya se
    // corrige aparte. Rutas grabadas (con Y real) lo dejan en false.
    bool  SampleTerrainY = false;
    // STEER-AHEAD COMO TIEMPO (2026-07-10, Arma steerAhead): Ld = PurePursuitTimeS * v, clampeado a
    // [PurePursuitFloorM, PurePursuitLookaheadM]. Corto en curva (baja vel -> apex tardio, NO anticipa),
    // largo en recta (alta vel -> liso). PurePursuitLookaheadM pasa a ser el TOPE. 0 -> defaults 0.6s / 2.5m.
    float PurePursuitTimeS = 0.6;
    float PurePursuitFloorM = 2.5;
    // turnCoef: ganancia del volante p/ compensar el sub-giro del receptor eAI (entiende de menos). 1=geometrico
    // puro; >1 = mas volante (el eAI understeer necesita ~1.5-2). Capeado a +-1 el comando final.
    float PurePursuitGain = 1.0;
    // COMPENSACIÃ“N DE CORTE (2026-07-12): el pure-pursuit apunta a un punto Ld adelante SOBRE la lÃ­nea; la
    // cuerda queda por DENTRO del arco -> CORTA las curvas (medido: recta -0.05m, curva cerrada -1.06m, todo
    // hacia el interior, sin correlaciÃ³n con velocidad = geomÃ©trico puro). Desplaza el punto objetivo hacia
    // AFUERA del arco la sagita (~ÎºÂ·LdÂ²/8 Ã— factor) -> el arco que maneja Boris pasa por la lÃ­nea real. 0=off,
    // ~1.0 rec para arrancar a calibrar. Es el "avance" del pure-pursuit p/ precisiÃ³n extrema de trayectoria.
    float PurePursuitCurveComp = 0.0;
    // La compensaciÃ³n solo actÃºa por DEBAJO de esta velocidad: la sag âˆ LdÂ² y a alta velocidad el Ld es
    // largo -> magnifica micro-curvatura del path -> zigzag (volante std 0.009->0.061 a 60km/h). El corte
    // solo importa en curvas CERRADAS, que son LENTAS; en lo rÃ¡pido el path es suave -> no hace falta. 2026-07-12.
    float PurePursuitCurveCompMaxKmh = 22.0;
    // AMORTIGUADO DE VOLANTE AL FRENAR (2026-07-12): no zigzaguea a 60 estable â€” zigzaguea cuando FRENA para
    // bajar velocidad (el cambio abrupto perturba la fÃ­sica -> el yaw se sacude -> el pure-pursuit a 20Hz
    // persigue la sacudida -> volantazo). Al frenar fuerte a velocidad, low-pass FUERTE del volante: clava la
    // direcciÃ³n, frena derecho, no persigue la perturbaciÃ³n. El giro sostenido (curva real) igual pasa. Gateado.
    // Alta velocidad = recta (99.9%): al frenar ahÃ­ NO hay que mover el volante -> clavo freno + clavo volante
    // y bajo la velocidad DERECHO. Por eso el amortiguado ESCALA con la velocidad: casi LOCK a FullKmh+, y se
    // SUELTA linealmente hacia MinKmh (ahÃ­ ya viene lento, puede entrar a la curva). effAlpha: 1 (sin damp) en
    // MinKmh -> Alpha (lock fuerte) en FullKmh.
    bool  SteerBrakeDampEnabled = false;
    float SteerBrakeDampMinBrake = 0.2;   // freno por encima del cual amortigua
    float SteerBrakeDampMinKmh = 20.0;    // abajo de esto no amortigua (viene lento, entra a curva)
    float SteerBrakeDampFullKmh = 45.0;   // arriba de esto = lock pleno (recta segura)
    float SteerBrakeDampAlpha = 0.12;     // fuerza del lock a FullKmh+ (bajo = mÃ¡s clavado)
    // MICROTICKS DE CENTRADO (2026-07-12, pedido de Sonom4n: "como hago yo, con microticks, no volante continuo"):
    // en RECTA a velocidad Boris se va lento del centro (drift) y el pursuit (Ld largo 8.5 + tick 500ms) recentra
    // demasiado despacio -> el offset crece hasta bogueo/AR (NUEVO03: picos 6-17m). Un humano no sostiene un angulo
    // continuo: da TOQUES chicos hacia la linea y suelta (asi el tren no lava). Implementado como corrector de
    // centrado PULSADO en el fast tick 50ms: NO recomputa el pursuit (eso fue el fast-loop que fallo, zigzag full-
    // lock), solo AGREGA un toque acotado hacia la linea sobre el steering base del tick lento. Pulsado on/off
    // (duty-cycle) = microticks. Gateado: solo RECTA (curvatura baja), solo si |offset|>deadband, solo >MinKmh, NO
    // en recovery ni frenando (ahi manda el SteerBrakeDamp). Signo garantizado por mini-pursuit de Ld corto (misma
    // convencion). La ruta se dibuja limpia (sin microcorrecciones horneadas) -> las genera el controlador.
    bool  CenterMicroTickEnabled   = false;  // master (default OFF = baseline intacto)
    float CenterMicroTickDeadbandM = 0.30;   // offset lateral (m) por encima del cual empieza a corregir
    float CenterMicroTickNearLd    = 3.0;    // lookahead CORTO (m) del mini-pursuit de centrado -> ganancia fuerte, signo correcto
    float CenterMicroTickGain      = 1.0;    // escala del toque de centrado (sobre el mini-pursuit)
    float CenterMicroTickMaxCmd    = 0.08;   // TOPE del toque (chico = microtick, no volantazo)
    float CenterMicroTickMinKmh    = 25.0;   // abajo de esto no hace falta (recentra solo)
    float CenterMicroTickStraightRad = 0.20; // curvatura acumulada (rad) del horizonte por encima de la cual NO actua (=curva, la maneja el pursuit)
    int   CenterMicroTickOnTicks   = 2;      // ticks de 50ms con el toque APLICADO  (2 = 100ms ON)
    int   CenterMicroTickOffTicks  = 2;      // ticks de 50ms SOLTADO (2 = 100ms OFF) -> ~5Hz pulsado
    // LOOKAHEAD ADAPTATIVO POR CURVATURA (2026-07-07, insight Arma 3 predictForwardRange): en un nodo
    // AGUDO (ej. wp207 V R=5.8m) el scan del FF cae en la SALIDA del nodo -> Boris pre-gira a la salida
    // ANTES de llegar al apice -> corta el nodo por dentro y se traba (medido: anticipaba 22m). Escalar
    // el scan HACIA EL PISO cuando la curvatura del horizonte es alta -> mira cerca -> llega al apice y
    // recien ahi gira. 0 = off (rutas suaves no lo necesitan; opt-in por ruta). ~0.7 recomendado en rutas
    // con intersecciones agudas (mapa). El factor satura con la curvatura; curvas suaves casi no se tocan.
    float PlantFFCurvatureShrink = 0.0;
    // YAW-RATE FEEDBACK (2026-07-07, "el imÃ¡n acotado por el grip"): cierra el lazo con la ROTACION
    // REAL del cuerpo (dBodyGetAngularVelocity[1]) que ya sensamos pero no usabamos. yaw deseado =
    // v * curvatura_path; si el cuerpo sub-rota (understeer real > modelado, ej grav) agrega volante
    // EN VIVO en la direccion del steering. Corrige el residual del FF. 0 = off. Solo M2/M3 (mode normal,
    // sin recording). Robusto a signo: usa MAGNITUD del yaw real. Sube de a poco (0.15-0.4) y mirÃ¡ si
    // se pega mas en curva; si oscila, bajalo.
    float YawFeedbackGain   = 0.0;    // 0 = off. Ganancia de la correccion de yaw (steering += gain * deficit).
    float YawFeedbackCap    = 0.25;   // cap de la correccion (evita volantazos).
    float YawFeedbackMinKmh = 8.0;    // no aplica por debajo de esta velocidad.
    // SURFACE SCAN (extractor de grafo vial desde el mapa): centro/tamaÃ±o/paso del barrido. Si
    // ScanCenterX/Z != 0 escanea AHÃ (apuntÃ¡s a cualquier pueblo por _hdr, sin rebuild); si 0 usa wp0.
    // ScanHalf metros de medio-lado (0 -> 60), ScanStep metros de resoluciÃ³n (0 -> 2; menor = mÃ¡s nÃ­tido).
    float ScanCenterX = 0;
    float ScanCenterZ = 0;
    float ScanHalf    = 0;
    float ScanStep    = 0;
    // ROAD SCAN (sonda de OBJETOS de carretera, 2026-07-07): reusa ScanCenter/ScanHalf. Barre la grilla
    // con GetObjectsAtPosition3D -> vuelca clase+pos+yaw+bbox de objetos road-like + censo type,count de
    // TODO (juez: si no hay asf/kr_, los caminos no son entidades). One-shot. [[project_map_ui_vision]]
    bool  RoadScanEnabled = false;
    float RoadScanStep    = 0;   // 0 -> 8 (metros entre muestras)
    float RoadScanRadius  = 0;   // 0 -> 6 (radio de esfera por muestra; step <= radius*1.41 cubre)
    // SCAN MAPA-COMPLETO (2026-07-08): en vez del cuadro chico, barre TODO el mapa frame-spread (no bloquea)
    // filtrando road parts inline -> roadscan_map.csv (dedup offline). Para validar el grafo/corredor contra
    // la red vial real ENTERA. Requiere RoadScanEnabled=true. RoadScanMapSize 0->15360 (Chernarus).
    bool  RoadScanFullMap = false;
    float RoadScanMapSize = 0;
    // ROADWAY EXTRACT (2026-07-10): lee la CAPA ROADWAY REAL del motor en vez del bbox de p3d.
    // FindPath(ROADWAY)=esqueleto topologico nativo -> roadway_path.csv ; GetSurface(Roadway) perpendicular
    // a la ruta = bordes/ancho/altura exactos -> roadway_edges.csv. One-shot al cargar la ruta.
    // [[reference_dayz_native_road_apis]] "dejar que el motor exprese el camino".
    bool  RoadwayExtractEnabled = false;
    // FRAME REPLAY (2026-07-05, project_frame_by_frame_replay Fase B): reproduccion TEMPORAL
    // fiel de la toma del humano. Boris comanda los inputs grabados frame-by-frame (40Hz, dt fijo)
    // por tiempo transcurrido -> su motor los rampea igual -> misma linea con taps. Bypass total del
    // Stanley/FF. Open-loop (drift = barrier). FrameReplayFile = nombre del frame_<ts>_<veh>.csv en
    // $profile:BZ_AutoDrive_PathLogger\ del SERVER (deployar el archivo grabado client-side).
    bool   FrameReplay = false;
    string FrameReplayFile = "";
    // Speed-lock (2026-07-05): trima suave throttle/brake para seguir la velocidad grabada y corregir
    // el drift longitudinal open-loop. NO toca el steering (los taps quedan intactos). Default ON.
    bool   FrameReplaySpeedLock = true;
    // Coast compensation (2026-07-05): el frame-replay coastea sistematicamente ~0.32km/h mas lento
    // (exceso de engine-braking en el canal embrague, que NO podemos observar ni setear). Compensacion
    // FEEDFORWARD determinista (NO feedback): en coast puro (throttle~0, brake~0) aplica un throttle chico
    // CALIBRADO que offsetea el exceso. Analogo al plant model de direccion. Patron medido+sistematico ->
    // compensable. Default 0 (off). Se calibra empiricamente hasta que dvel_coast -> ~0.
    float  FrameReplayCoastComp = 0.0;
    // ILC in-game (autocompensacion universal): al replayar, Boris mide su residuo per-frame (el cursor
    // ES la alineacion) y ACUMULA el inverso en la comp del frame_ (cols 13,14). Al terminar, persiste al
    // disco. Cada corrida = una pasada; iterar converge. Ciego a la causa. Default OFF.
    bool   FrameReplayILC = false;
    // Endgame de precision de posicion (ULTIMO RECURSO): si el feedforward slope-aware no cierra el sesgo
    // del endpoint por fisica, este controlador repta a Boris al endpoint exacto (direccional-general,
    // forward/reverse). Default OFF -> fisica primero, sin controlador que pise la toma.
    bool   FrameReplayEndgame = false;
    // Cross-anchor (feedback lateral de steering): DISTORSIONA la soga (abre la curva). Default OFF -> el ILC
    // lateral (comp_str feedforward) aprende el steering sin tocar la soga en vivo. Fallback si el FF no cierra.
    bool   FrameReplayCrossAnchor = false;
    // Feedback del wheel-tracking (2026-07-13): el replay comanda rueda_grabada/30 (feedforward) + Gain*(rueda_grabada
    // - rueda_boris) (feedback). El feedback persigue la rueda de Boris via WheelGetDirection (ruidoso/lagueado) -> en
    // recta (rueda_grabada~0) mete un Â±0.05 que HACE BAILAR a Boris (medido REPLAY01: grabacion steering 0.000 / boris
    // Â±0.05, heading se abre). Gain=0 -> feedforward PURO de tu angulo de rueda grabado = fiel (recta 0 -> derecho;
    // curva -> rampa al angulo). Default 0.02 (comportamiento previo); bajar a 0 para replay fiel de recta.
    float  FrameReplayWheelFbGain = 0.02;
    // Scenario events: nodos de mision (waypoint marcado en NUMPAD 4 + actions[]).
    // Reemplaza al viejo CargoEvents â€” el cargo ahora es el verbo add_cargo.
    ref array<ref BZMarkerEvent> Events = new array<ref BZMarkerEvent>();
    ref array<ref BZCrewMember> Crew = new array<ref BZCrewMember>();   // bots que viajan con Boris desde el arranque
    ref array<ref BZWaypoint> Waypoints = new array<ref BZWaypoint>();
}

// ============================================================================
//  BZAutoDriveSettings - config GLOBAL del framework (NO por-ruta).
//  Se carga UNA vez al Init desde $profile:BZ_AutoDrive\BZAutoDrive_settings.json
//  El modder lo edita fuera del juego (resuelve el huevo-gallina: la tecla que abre
//  el Control Panel se setea aca, no desde la UI â€” si la tecla esta pisada igual
//  podes cambiarla editando el archivo).
// ============================================================================
class BZAutoDriveSettings {
    // SteamIDs autorizados a abrir el Control Panel y ejecutar sus acciones.
    // Vacio = permitido a todos (modo testing/local). Con IDs = solo esos (server publico).
    ref array<string> AdminSteamIDs = new array<string>();
    // Tecla que abre el Control Panel. -1 = KC_HOME (default). Otro = ese KeyCode
    // de DayZ (KC_END=207, KC_INSERT=210, KC_DELETE=211, etc).
    int ControlPanelKey = -1;
    // Parada a demanda por gesto: el player hace OK (pulgar) enfrente del bus -> Boris frena 10s.
    // Default OFF: el admin lo prende a sabiendas (no es regla silenciosa). ON = aplica a TODAS
    // las tomas. Documentado en el manual de controles (seccion gestos).
    bool HailGestureEnabled = false;
    // SCAN MAPA-COMPLETO AL BOOT (2026-07-08): si true, el server al arrancar (sin cliente, sin Boris)
    // dispara el scan de la red vial entera tras RoadScanBootDelaySec (el mundo estatico ya esta cargado
    // en un dedicated). Escribe roadscan_map.csv. Setear en BZAutoDriveSettings.json y bootear A headless.
    bool  RoadScanOnBoot = false;
    float RoadScanBootDelaySec = 45.0;
}
