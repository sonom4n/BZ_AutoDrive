// ============================================================================
//  BZDriveController.c - BZ_AutoDrive 1.2 -- EL ORQUESTADOR
//
//  Implementa el SEAM (SEAM.md): es lo unico que el Runner ve del cerebro.
//  Compone los modulos y los MEDIA -- ningun modulo llama a otro ni le lee el estado.
//
//  Plan() son ~15 lineas y esa es la idea: el arbitro es UNA LINEA VISIBLE, no un hook
//  enterrado en 1200 sentencias, y la lista de quien esta exento de la derivacion se lee
//  de un vistazo.
//
//  LOS DOS HANDSHAKES QUE REEMPLAZAN AL COUPLING DE v1.1:
//   - MC reporta IsLegDone() -> el Controller decide: cusp MECANICO = avanza el leg solo;
//     parada SEMANTICA = emite ARRIVED y espera Resume(). Esto es lo que reemplaza a
//     m_ManeuverCreepHold.
//   - El slam reporta IsLatched() -> el Controller avisa al arbitro con NotifyArrived().
//     El arbitro NUNCA le pregunta al slam.
// ============================================================================

class BZDriveController
{
    // Gas maximo sin NINGUNA rueda en el piso. No es cero: algo de tiro puede ayudar a
    // salir; gas pleno solo patina y rebota.
    // Los self-tests ejercitan Plan() y disparan los mismos Print del runtime, asi que la
    // salida de la suite parecia salida de corrida. La suite lo silencia.
    static bool s_Quiet;

    private const float TRACTION_MIN = 0.15;
    // Umbral de REPORTE (ya no manda sobre el gas): por encima de esto el motor gira mas
    // de lo que la marcha explica. Se llamo PATINAJE hasta que la medicion directa mostro
    // que la rueda va pegada al chasis: es deslizamiento de embrague.
    private const float DESLIZ_UMBRAL = 1.6;
    private int   m_PatinajeReportes;
    private int   m_SegDescartadas;
    // --- CLAVADA: a fondo y sin avanzar. Se DETECTA y se REPORTA; ya no se pulsa. ---
    //
    // ✖ EL ROCK QUEDO FALSADO POR MEDICION. Pulsar el gas para sacar al motor del hueco de
    // par no funciona: medido `ROCK: 3 pulsos · rpm de 1077.9 a 1164.35` -- +86 rpm, sigue
    // clavado. En marcha, soltar el gas solo da freno-motor; para CONSTRUIR vueltas hay que
    // desacoplar, y DayZ no expone embrague. La hipotesis estaba condenada por el motor, no
    // por la implementacion. Se borra el pulso entero en vez de dejarlo apagado.
    //
    // Lo que SI se conserva es la deteccion: "a fondo, ruedas en el piso y sin avanzar" es
    // informacion honesta y hace falta para diagnosticar. Reportar no es actuar.
    // --- ARRANQUE DESDE EL REPOSO: la rampa MEDIDA, en cualquier modo ---
    private const float ARRANQUE_STEP = 0.10;
    private const int   ARR_RAMPA   = 0;
    private const int   ARR_SUELTO  = 1;

    // --- SUBVELOCIDAD: se queda MUY por debajo de lo que la ruta dice que se puede ---
    private const float SUBVEL_FRAC   = 0.5;
    private const int   SUBVEL_TICKS  = 3;
    private const int   SUBVEL_CADA   = 6;
    // ⚠ UN PISO DE VELOCIDAD FIJO DONDE EL VEHICULO YA TIENE EL SUYO. La pregunta que
    // responde es "la ruta ya venia lenta aca, no opino", y el umbral natural de "lento" es
    // el REPTADO, que ES por vehiculo: creepKmh sale de 3.6*sqrt(2*aFreno*CRAWL_STOP_M), o
    // sea del freno de ESE vehiculo. Queda de respaldo si el reptado no esta resuelto.
    private const float SUBVEL_MIN_REC = 5.0;   // km/h: respaldo si no hay reptado
    // ⚠ 3 m ES MAS CORTO QUE EL BUS (batalla 5.35). Esta ventana gobierna TRES reglas
    // transversales --NoSoltarHaciaElBog, NoPlanearHastaElBog y la subvelocidad-- y las tres
    // preguntan "que pide la ruta ADELANTE". En un vehiculo largo, "adelante 3 m" cae
    // DEBAJO del propio vehiculo: la ventana no lo despeja. Se toma el mayor entre este piso
    // y una batalla: en el sedan (2.93) no cambia nada, en el bus pasa a 5.35.
    private const float SUBVEL_LOOKAHEAD_M = 3.0;
    // --- PLANEO: el modulo suelta el gas donde la ruta todavia quiere velocidad ---
    private const float COAST_THR_MAX = 0.05;
    // --- FEATHERING: horizonte de decision, en segundos de planeo ---
    private const float FEATHER_HORIZONTE_S = 3.5;   // 7 ticks de Plan() a 2 Hz
    private const float FEATHER_TAU_S       = 1.0;   // horizonte para RECUPERAR velocidad
    private const float AOBS_SANO           = 0.05;  // m/s2: por encima de esto esta ganando velocidad
    private static const float BITE_MARGEN  = 1.10;   // static: lo lee PisoDeMordidaAplica  // apuntar 10% sobre la mordida, no al filo
    private const float FEATHER_MARGEN      = 1.20;  // 20% por encima del umbral de bog
    private const int   COAST_CADA    = 4;

    private const float STALL_FULL_THR   = 0.95;
    private const float STALL_PROGRESS_M = 0.30;
    private const int   STALL_TICKS      = 10;
    private const int   STALL_REPORT_EVERY = 20;
    // Abandonar un leg imposible usa el MISMO criterio que el modulo de pose ya aplicaba
    // (NO_PROGRESS_TICKS = 20): no es una constante nueva, es la misma decision llevada a
    // todos los modos en vez de a uno solo.
    private const int   ABANDONO_TICKS = 20;

    private int    m_StallTicks;
    // el diagnostico del no-rescate (ver DiagnosticarSinRescate). m_SinProgresoT cuenta el
    // "moverse sin avanzar": el arco de la cinta no progresa aunque el vehiculo se mueva -- que es lo que hace
    // un vehiculo vencido por una cuesta, y por eso NUNCA acumula reposo.
    // RETROCEDER NO ES NI ANDAR NI ESTAR DETENIDO. Un vehiculo vencido por una cuesta nunca
    // acumula reposo, y el arco de la cinta TAMPOCO lo delata: la odometria suma sqrt(dx^2+dz^2) SIN SIGNO,
    // asi que retroceder HACE AVANZAR m_SEst. Por eso m_SinProgresoT es ciego a este caso por construccion.
    private float  m_RetrocedeT;
    private float  m_SinProgresoT;
    private float  m_SEstProgPrev;
    private string m_DiagSinRescate;
    private int    m_StallReported;
    private int    m_LegReportado = -1;
    private int    m_LegsAbandonados;
    // ⚠⚠ LA RENDICION SE PERDIA AL AVANZAR DE LEG.
    // m_Maneuver.Reset() y m_Slam.Reset() borran su m_GaveUp, y se llaman en CADA avance.
    // Asi que el veredicto de la ruta solo veia el ULTIMO leg: un leg del medio que cerro
    // corto quedaba olvidado y la ruta se declaraba COMPLETE igual. La rendicion se anota
    // ACA, antes del Reset, porque el que rinde cuentas por la ruta entera es el Controller.
    private int    m_LegsRendidos;
    // QUIEN ES. Con 9 coches, una linea del cerebro sin etiqueta no se puede atribuir, y eso
    // ya impidio leer un falsador: no se puede distinguir UN coche re-armando la rampa 8 veces de OCHO coches
    // armandose dentro de la misma ventana. El dato ya venia (el AR lo recibe como `ruta`); no se guardaba.
    private string m_EtiqRunner = "";
    private string m_MotivoRendicion;
    // QUIEN PIDIO EL CIERRE. El FIN decia donde quedo y cuanto faltaba, pero no por que
    // termino, y los candidatos no se distinguen desde el texto (endpoint, abandono, maniobra, sin mas legs).
    // Un cierre silencioso es el mismo problema que el AR silencioso, en el otro extremo del recorrido.
    private string m_CierrePorQue = "";
    // Una anotacion por leg: OnLegDone y OnArrived pueden pasar los dos por el mismo leg.
    private int    m_RendicionAnotadaLeg = -1;
    private const int SLAM_LOG_MAX = 200;
    private int    m_SlamLogTicks;
    private float  m_SlamPrevV;
    // ★★ EL SEGUIMIENTO SE MIDE A LO LARGO DEL LEG, NO SOLO EN EL ENDPOINT.
    //
    // Hasta ahora el unico numero lateral que salia era el del CIERRE, y se lo estaba
    // leyendo como "que tan bien sigue la traza". No es lo mismo: medido sobre los logs,
    // el Sedan_02 cierra a 0.045-0.10 m y su lateral A MITAD DE RUTA tiene mediana 0.62-0.65.
    // Comparar el cierre de un vehiculo contra el recorrido de otro llevo a concluir que
    // "lo lateral no transfiere" cuando los numeros comparables no lo dicen.
    private int    m_SegN;
    private float  m_SegSum;
    private float  m_SegMax;
    private ref array<float> m_SegMuestras;
    // ⚠ DOS CORRIDAS DE LARGO DISTINTO NO SON COMPARABLES. El promedio sobre 546 muestras de
    // un tramo no dice lo mismo que sobre 200 de otro: si una se clava antes, mide otra
    // parte de la ruta. El tramo cubierto va en el reporte para que la comparacion se pueda
    // hacer -- o descartar.
    private int    m_SegWpIni = -1;
    private int    m_SegWpFin;
    // ★★★ EL PROMEDIO SOBRE "wp 0 HASTA DONDE CLAVE" NO ES COMPARABLE ENTRE CORRIDAS.
    //
    // Medido: dos corridas con el MISMO envelope, la MISMA tabla congelada y el MISMO tramo
    // (wp 0-661 y 0-662) dieron 1.34 y 0.353 -- casi 4x. Con esa dispersion, cualquier A/B
    // de corrida unica es ruido, y varias conclusiones del dia (incluidas mias) comparaban
    // exactamente eso.
    //
    // Tres cosas la ensucian y las tres se arreglan reportando distinto:
    //   1. la COBERTURA: si una corrida clava antes, promedia otros tramos;
    //   2. el TRANSITORIO de adquisicion: al arrancar el vehiculo viene de la pose de spawn
    //      y converge -- esos metros no son seguimiento, son enganche;
    //   3. los TRAMOS DUROS: la ruta tiene curvas donde hasta el auto se va 3 m, y rectas
    //      donde no se va nada. Un solo numero los mezcla.
    // Por eso el reporte pasa a ser POR TRAMO FIJO de waypoints: dos corridas se comparan
    // tramo contra tramo, y los que una no cubrio simplemente no entran.
    static const int SEG_TRAMO_WPS = 100;
    static const int SEG_TRAMOS_MAX = 24;
    static const int SEG_ADQUISICION_WPS = 10;
    private ref array<float> m_SegTrSum;
    private ref array<int>   m_SegTrCnt;
    private ref array<float> m_SegTrMax;
    private int    m_SegAncla = -1;

    // --- CAJA NEGRA: 4 s a ~40 Hz, para ver lo que Plan() a 2 Hz no puede ver ---
    // El derrumbe de wp651 fue de 46.86 a 0.2 km/h en ~2 s y no dejo NI UNA linea: entre
    // dos ticks de Plan no hay observador. Se graba siempre y se vuelca solo cuando algo
    // ya salio mal, asi que en una corrida sana no cuesta un solo Print.
    private const int RING_N = 160;
    private ref array<float> m_RKmh;
    private ref array<float> m_RThr;
    private ref array<float> m_RBrk;
    private ref array<float> m_RStr;
    private ref array<float> m_RRpm;
    private ref array<float> m_RPitch;
    private ref array<float> m_RLat;
    private ref array<float> m_RX;
    private ref array<float> m_RZ;
    private ref array<float> m_RT;
    private ref array<float> m_RLd;
    private ref array<float> m_RkArco;
    private ref array<int>   m_RGear;
    private ref array<int>   m_RRuedas;
    private ref array<int>   m_RClutch;
    private int   m_RIdx;
    private int   m_RCant;
    private float m_RTiempo;
    private int   m_Volcados;
    private const int VOLCADOS_MAX = 3;
    private int   m_BiteReportes;
    // El techo de friccion neumatico-piso. No es un numero elegido: es la fisica del
    // contacto. Por encima de ~1 g no hay freno ni superficie que explique la frenada.
    private const float CHOQUE_MARGEN = 1.5;
    // ventana para atribuir el choque a un contacto reportado por el motor. El evento de fisica llega en
    // el mismo frame que el colapso de velocidad; medio segundo es holgura, no tolerancia de ajuste.
    private const float CHOQUE_CONTACTO_S = 0.5;
    private const float CHOQUE_RUEDAS_S = 0.5;     // la historia de ruedas en el suelo que muestra la linea del choque
    // BACHE. Una desaceleracion imposible para un neumatico PERO sin contacto reportado por el motor
    // y con poca velocidad perdida es el TERRENO (un bache, un cordon), no un obstaculo. Medido en las 4 corridas
    // de KRASNOSTAV del 17/09: 15 episodios, TODOS sin contacto, de 1,5 a 3,5 km/h, y REPETIBLES POR POSICION
    // (el mismo metro en 3 o 4 de las 4 corridas). Los choques de verdad de esta semana: 21 y 61 km/h. El corte
    // en 5 km/h deja un factor 1,4 por debajo y 4 por arriba: es una guarda, no una perilla.
    // Por que importa y no es solo el nombre: la CAJA NEGRA se vuelca en el PRIMER choque (una vez por corrida).
    // Con los baches contando, el volcado se gastaba en un pozo del camino y un choque posterior se quedaba sin
    // evidencia. El AR no se entera de ninguno de los dos: tiene su propio detector y pide 20 km/h.
    private const float CHOQUE_BACHE_KMH = 5.0;
    private const float CHOQUE_PISO_G = 9.81;
    // CHOQUE_MAX = 8 era un tope de PRINTS POR VIDA del controlador (m_Choques nunca se reiniciaba). El 105 llego a
    // su #8 a las 17:22:40 del 23/09 y su choque de frente de las 17:31:10 (31,35 -> 1,45 km/h en 25 ms, -332 m/s2) no tiene linea;
    // el 107 imprimio 4 lineas en 13 s por un solo choque. Decision del analista: UNA LINEA POR EPISODIO, sin tope.
    private const float CHOQUE_EPISODIO_S = 5.0;   // un impacto a menos de esto del anterior es el mismo episodio
    private float m_ChoqueKmhPrev;
    private ref BZHistRuedas m_HistRuedas;   // las ruedas en el suelo de los ultimos cuadros (solo instrumento)
    private bool  m_AvanzoEsteTick;
    // El mecanismo de direccion solo salia en la caja negra, que vuelca cuando algo YA fallo.
    // Una corrida LIMPIA no dejaba ni un dato de por que el lateral es el que es. Con esto,
    // cada corrida sana mide el lazo: lo que la traza PIDE, lo que el volante COMANDA y lo
    // que el vehiculo LOGRA.
    // Cada tick de Plan. 19 muestras en 3 minutos no alcanzaban para ver el instante de la
    // esquina: el pico del cross-track quedaba entre dos muestras y solo se veia el promedio.
    private const int DIR_CADA = 1;
    private int   m_DirTicks;
    private float m_YawPrev = -999.0;
    private float m_CmdPrev;
    // Si el ultimo Plan resolvio el lateral con pure pursuit. Se pregunta esto y NO el modo,
    // porque la rama de pure pursuit es el `else` del arbitro: preguntar por el enum seria
    // repetir una condicion que vive en otro lado y puede cambiar sin que esto se entere.
    private bool  m_UsoPursuit;

    // ===== DETECTOR DE DESLIZAMIENTO: COMANDA y LOGRA de SIGNO OPUESTO =====
    // Medido en las 6 corridas de la dispersion: ticks con ambos significativos
    // y de signo opuesto -> 0 en las dos que clavan, 2-4 en las cuatro que vuelan.
    // Separacion total, sin un falso positivo. Y el primero salta con `lat` todavia < 1 m
    // (-0.49 / -0.44 / -1.69 / -0.85): llega a tiempo de PREVENIR, no solo de contener.
    // Si las gomas giran al reves de lo que se les pide, mas volante no compra curvatura:
    // compra deriva. Medido, durante el cluster el `str` CRECE mientras LOGRA pelea en
    // contra (GT2RS wp103 -0.061 -> wp130 +0.109). La accion es SOLTAR.
    private static const float DESLIZ_MIN_MAG = 0.01;
    private static const float DESLIZ_MIN_KMH = 20.0;
    // ===== CONTRAVOLANTE POR ANGULO DE DERIVA =====
    // Poblaciones medidas a >60 km/h: las que CLAVAN tienen deriva mediana 4.1
    // y MAXIMA 8.3 grados --y recuperan solas--; las que VUELAN, mediana 21-24 --y componen.
    // El umbral va en 10: arriba de todo lo observado en las que clavan, y las que vuelan
    // lo cruzan con margen. (La razon |LOGRA|/|COMANDA40| NO separa: 0.15 y 1.51 en las que
    // clavan contra 0.31/1.02/0.47 en las que vuelan. Se pisan, y no se elige umbral en un
    // solapamiento.)
    private static const float DERIVA_UMBRAL_RAD = 0.1745;   // 10 grados
    // ⚠ EL PISO DE VELOCIDAD ES EL ARREGLO DEL FALSO POSITIVO, no un filtro de magnitud.
    // Los 8 DESLIZA del ZiS5 y los 12 del Offroad cayeron TODOS entre 20 y 44 km/h --el
    // arranque de curva con el yaw atrasado-- y por encima de 60 no hubo NI UNO en ninguno
    // de los dos controles. El ZiS5 que paso de 0 a 5 choques queda excluido por completo.
    private static const float DERIVA_MIN_KMH = 60.0;
    // ===== VENTANA DEL CONGELAMIENTO POR CAMBIO DE MARCHA =====
    // ⚠⚠ "RELACION INCOHERENTE" NO ES "CAMBIO EN CURSO", Y ESA CONFUSION ROMPIO LA FLOTA.
    // El hx58 luguea en 4ta a 33 km/h: el embrague patina, la relacion queda fuera de
    // coherencia DE FORMA CONTINUA sin que haya ningun cambio, y el congelamiento se quedo
    // encendido 7 muestras seguidas (26->53 km/h) matando la correccion en plena curva:
    // hErr +1 -> +25 grados -> fly-off de 352 m. Mi supuesto de que "la flota cambia a baja
    // velocidad donde el pateo no compone" no contemplo el LUGUEO.
    //
    // La ventana ahora se ABRE con un evento (cambio de marcha), se CIERRA sola cuando el
    // embrague agarra, y ademas TIENE TOPE. Las tres cosas.
    //
    // ★ El tope no es inventado, y dos derivaciones independientes dan lo mismo:
    //   · medido en 23 upshifts humanos, la transicion dura mediana 0.375 s y p90 0.80 s
    //   · el costo de congelar es que el lateral crece a v*sen(hErr): con 4 grados a 119
    //     km/h son 2.3 m/s, asi que para no pasar el envelope de v1.1 (1.65 m) el tope
    //     tiene que ser menor a 0.72 s
    // Se toma el p90 medido. Un vehiculo lugueando ve 0.8 s como MUCHO, no 7 muestras.
    private static const float FRZ_MAX_S = 0.80;
    private float m_FrzRestante;
    private int   m_FrzGearPrev = -999;
    // ===== FEEDFORWARD DEL VOLANTE GRABADO =====
    // ★ LA ANTICIPACION DEL HUMANO. La superposicion v1.1 vs v1.2 mostro que en
    // el onset v1.2 comanda 0.09 -> 0.9 donde la geometria pide 0 y v1.1 tiene la rueda en
    // -0.08 grados: TODO lo que v1.2 manda ahi es correccion. El volante grabado trae lo que
    // el humano hizo con las ruedas en ese metro, y a alta velocidad reemplaza esa
    // correccion en vez de dejarla correr.
    //
    // ⚠ EL DATO ES EL ANGULO DE RUEDA (col 18), NO LA COLUMNA `steering` (col 11): esa es la
    // TECLA, binaria 0/1. Reproducirla seria comandar tope pleno de golpe.
    //
    // La rampa va entre dos numeros MEDIDOS, ninguno elegido:
    //   60 km/h  = la compuerta ya validada de la flota (ZiS5/Offroad topan en 44-46)
    //  130 km/h  = la velocidad donde se midieron los onsets de fly-off (119-130)
    private static const float FFVOL_KMH_MIN = 60.0;
    private static const float FFVOL_KMH_MAX = 130.0;
    // ★★★ DONDE EL CONTRAVOLANTE SE QUEDA SIN DOMINIO. "Apuntar las ruedas por donde el auto
    // va" es una identidad valida MIENTRAS EL AUTO VAYA HACIA ADELANTE. A 90 grados de deriva
    // la velocidad es PERPENDICULAR a la trompa: la componente hacia adelante es exactamente
    // cero, y mas alla es NEGATIVA --el auto va de culo. No es un umbral calibrado: es la
    // frontera fisica donde el "adelante" deja de existir.
    // ⚠ MI FORMULA NO TENIA TOPE Y EL CLAMP LO TAPABA. Con deriva 116 grados, cs = 116/35
    // satura en 1.0: tope pleno SOSTENIDO en la direccion del trompo. La salida se veia
    // acotada --clamp a +-1-- mientras el significado ya estaba invertido, y eso acompaño el
    // trompo 100+ metros en vez de cortarlo.
    private static const float DERIVA_SIN_FRENTE = 1.5708;   // 90 grados
    // ⚠ EL SOSTEN (1.5 s) SE FUE CON LA ACCION. Quedaba escrito y sin lectores. La
    // medicion que lo justificaba sigue siendo valida --recuperacion de la goma [4,2,5,3,
    // 6,3,3,2] muestras de log ~ 1.5 s reales-- pero a 107 km/h son 45 m de linea
    // abandonada, y ese costo escala con la velocidad. Cualquier reemplazo se mide en
    // DISTANCIA o en rumbo, no en tiempo fijo.
    private float m_DeslizYawPrev = -999.0;
    private vector m_DeslizPosPrev;
    private float m_Deriva;         // rad, rumbo del MOVIMIENTO menos rumbo al que APUNTA
    private float m_DerivaTasa;     // rad/s, si COMPONE o si VUELVE
    private float m_DerivaTasaFilt; // la tasa SOSTENIDA: un tick suelto no la mueve
    private int   m_DeslizEventos;
    private float m_KRefresco;
    private int   m_Choques;
    private int   m_Baches;      // contados aparte de los choques (ver CHOQUE_BACHE_KMH)
    // el reloj del ultimo *** CHOQUE *** con la definicion de la LINEA (para RunnerChoqueHaceS; -1 = nunca) y el
    // episodio abierto (impactos, choques con contacto o perdida grande, duracion, minimo, cabecera y numero impresos).
    private float  m_ChoqueHaceS = -1;
    // los *** CHOQUE *** contra un OBJETO (el motor reporto una entidad: clase o "modelo X"; el terreno no cuenta) y su
    // lista, para el veredicto. El FIN pendiente: la llegada arma la cola del log y el veredicto sale del REPOSO.
    private int    m_ChoquesObjeto;
    private bool   m_FinPendiente;
    private bool   m_FinDecidido;
    private bool   m_FinRendido;
    private int    m_FinWp = -1;
    private float  m_ReposoFinM = -1;
    private bool   m_FrenoTomaAcoplado;
    private int    m_FrenoCuspLeg = -1;    // el leg cuya frenada de cusp ya se acoplo al manual
    private float  m_FrenoCuspMs2 = -1;
    // los choques contra objeto, por (wp, clase) con su cuenta: la lista del veredicto no repite el mismo golpe
    private ref array<int>    m_ChObjWp;
    private ref array<string> m_ChObjClase;
    private ref array<int>    m_ChObjN;
    private int    m_ChoqueEpN;
    private int    m_ChoqueEpChoques;
    private float  m_ChoqueEpHaceS = -1;
    private float  m_ChoqueEpDurS;
    private float  m_ChoqueEpKmhMin;
    private string m_ChoqueEpCab = "";
    private int    m_ChoqueEpNum;
    private int    m_ArranqueTicks;
    private bool   m_ArranqueActivo;
    private float  m_ThrottlePedido;
    private float  m_ThrFFAplicado;   // cuanto sumo el pedal grabado en ESTE tick
    private int    m_FFTicks;         // en cuantos ticks llego a actuar, en toda la corrida
    private int    m_SubvelTicks;
    private int    m_SubvelReportes;
    private float  m_KmhPrev;
    private int    m_PlaneoReportes;
    private float  m_AObs;
    private float  m_ThrAplicado;
    private float  m_ThrAplicadoPrev;
    private float  m_KmhPrevPlan;
    private bool   m_FeatherActivo;
    private int    m_FeatherReportes;
    private int    m_ArranqueFase;
    private float  m_StallArcPrev;
    private string m_NoStallPorque;

    private BZRouteData      m_Route;
    private BZVehicleManual  m_Manual;
    private BZWorldProbe     m_Probe;

    private ref BZLegMachine          m_LegMachine;
    private ref BZLegView             m_Leg;
    private ref BZModeArbiter         m_Arbiter;
    private ref BZPurePursuit         m_Pursuit;
    private ref BZDerivationEngine    m_Derivation;
    // Solo para reproducir un caso en banco: el latch de marcha es dependiente del camino y
    // el escenario que hay que observar salio 1 de cada 3 corridas. Se setea desde la bandera.
    private static int s_MarchaForzada;
    private ref BZManeuverController  m_Maneuver;
    private ref BZEndpointSlam        m_Slam;
    // ★★★ LA CINTA (E2): reproductor espacial de la toma full-res. NULL = modo
    // normal. La setea el runner/servicio al configurar la corrida (bandera).
    private ref BZCintaRunner         m_Cinta;
    private ref BZCintaMuestra        m_CintaM;
    private bool  m_CintaTick;     // el ultimo Sample salio de la cinta (diag)
    private float m_CintaDist;     // deriva a la cinta en ese tick (el instrumento)
    private float m_CintaStr;      // el str que LA CINTA aplico (que-mano, )
    // ★★★ E3 EL IMAN: sesgo CHICO sobre la rueda grabada, contra el
    // error firmado a la linea grabada: dFw = -(C1·lat + C2·psi), clamp ±CAP.
    // Dimensionado en el banco cerrado con la cinta REAL de la 06 y la perturbacion
    // calibrada a la deriva open-loop MEDIDA en E2 (0.3m@57m / 2.6m@124m, RPT
    // 16-06): C1=1.5 C2=0.25 CAP=4.5 da p90 ~0.9 m sobre la toma entera, recupera
    // 3-6 m de offset en ~17-22 m SIN weaving (cruces ~0), y su tasa de cambio
    // (RMS ~1 deg/s) queda LEJOS de la textura del usuario (~5 deg/s @2/s) — el iman es
    // sesgo lento ADITIVO, la textura viaja intacta encima. CAP 4.5 deg ≈ 15% del
    // steerMax = el borde del spec de, subido por el R1 pre-registrado
    // (la deriva medida 1-6 m pide autoridad para cerrarla).
    // ⚠ GANANCIA x2: las 6 excursiones chicas (>0.3 m) del patron
    // eran EQUILIBRIO DE GANANCIA, no bias ni lag — medido en el RPT 17-37: en
    // cada zona el iman queda clavado en C1·lat (0.36 m × 1.5 = 0.54 ≈ el cIman
    // -0.5/-0.65 impreso), o sea el rechazo de perturbacion (peralte/viento del
    // terreno) se frena donde la ganancia lo deja. Bias descartado (cLat mediana
    // +0.02, cPsi ~0 pegado en recta); lag descartado (las excursiones son en
    // RECTAS con cFw~0, no respuestas atrasadas a curvatura). C1 1.5→3.0 parte
    // el equilibrio a la mitad (0.36→0.18, todas bajo 0.3; arranque 0.63→~0.3);
    // C2 sube proporcional (amortiguacion pareja). Banco: p90 0.43, CERO cruces,
    // dΔ/dt 1.37 deg/s (sigue lejos de la textura ~5). EL CAP NO SE TOCA: la
    // guarda no escala con la perilla. Falsador: weaving en recta (lat cruzando
    // 0 con periodo corto) → revertir a 1.5/0.25.
    private const float IMAN_C1_DEG_M   = 3.0;   // deg de rueda por metro de lat
    private const float IMAN_C2_DEG_DEG = 0.40;  // deg de rueda por deg de psi
    private const float IMAN_CAP_DEG    = 4.5;
    private float m_ImanDeg;       // el sesgo aplicado este tick (diag)
    private float m_CintaPsi;      // error de rumbo vs rumbo grabado, deg (diag)
    // Anticipacion de entrada al lock: cuanto se corre el blanco del
    // iman hacia adentro cuando viene un lock LARGO (gate en el runner). 0.7 del
    // banco (0.5/0.7/0.9 barridos: mas entrada reparte mas pero acerca el pico
    // interior al borde interno del hairpin; 0.7 deja +0.59 adentro).
    private const float ENTRADA_LOCK_M    = 0.7;
    private const float ENTRADA_VENTANA_M = 20.0;
    private float m_CintaRef;      // el blanco lateral de este tick (diag)
    private bool  m_CintaVol;      // volante-solo (ENDPOINT reversa): el slam frena (diag)
    private int   m_DiagNoPosee;   // rate-limit del diag NO-POSEE(rev)
    // ★★★ H-FASE-STALE, fix: la siembra la
    // dispara EL LEG — que siempre sabe su fase y siempre transiciona — no un
    // detector interno de la cinta (ese vivia de la memoria de llamadas, y las
    // llamadas dependen del gate: en el leg fwd intermedio de un shuffle la
    // cinta no se llama → detector stale → 08 detectaba 2 de 6 cusps, 07 1 de
    // 3-4 → sus reversas al fallback → zigzag/fly-off). Sin perillas: la fase
    // sale de la estructura de legs.
    private bool  m_LegFaseSet;
    private bool  m_LegFasePrev;
    // ★★★ LATCH DE POSESION POR LEG: el LAUNCH posee el
    // rest→rolling de CADA leg; al primer tick "rodando" el latch arma y la
    // cinta puede poseer el RESTO del leg aunque la velocidad baje (maniobra
    // lenta legitima). Se desarma al cambiar de leg. 2.0 = "rueda": fisico,
    // igual en toda la flota, cero perillas — el mismo piso que cerro la
    // reversa de 08, ahora para las dos fases.
    // ⚠⚠⚠ EL ARME ES POR FLANCO, NO POR NIVEL: el leg flipea en
    // Plan (2 Hz) con el vehiculo TODAVIA RODANDO el residual del leg viejo
    // (1.1-1.5 km/h medidos en el primer frame del leg nuevo de 08) — un
    // arme por nivel se re-armaba en el MISMO tick del reset con esa
    // velocidad rancia, la cinta poseia antes de rodar EL LEG, sembraba mal,
    // sobre-consumia y la monotonia dejaba el cursor afuera para siempre
    // (ultConsumido 1→5 de un saque, congelado en 6; ultDistM 12→35 m
    // monotono; cintaR 21%→4%). "Rodar el leg" = haber VISTO el casi-reposo
    // de ESTE leg primero (m_CintaQuieto) y recien entonces cruzar 2.0.
    private bool  m_CintaRodo;
    private int   m_CintaRodoLeg;
    private bool  m_CintaQuieto;
    private float m_CintaReposoT;   // cuanto lleva detenido con la cinta poseyendo
    // ★ LA OPORTUNIDAD DE ARRANCAR. Al cumplirse una meseta (o al salir de un hold) el casco lleva segundos quieto por
    // construccion: el reloj del reposo y el del progreso arrancan de cero y el enganche de la meseta siguiente espera
    // ENG_GRACIA_S. Medido: 29 ms despues de cumplir una meseta de 6 s el enganche saltaba a la ultima parada, 2,9 m
    // adelante, y el arrastre grabado no se replayaba (el leg cerraba 3 m antes y la reversa arrancaba corrida).
    private const float ENG_GRACIA_S = 1.5;
    private float m_EngGraciaT;
    private bool  m_EnMesetaPrev;
    // re-armado por POSICION. Distancia que el vehiculo tiene que recorrer desde donde se
    // solto la cinta para pasar la meseta (-1 = sin objetivo: re-arma por velocidad como antes).
    private float  m_CintaRearmaDist;
    private vector m_CintaSueltaPos;
    private bool   m_SalirDelHold;   // Resume tras un hold -> en el primer Sample la cinta se suelta y re-arma al fin de la meseta
    // UN STOP SERVIDO NO PIDE 0. El wp isStop en el que el vehiculo fue retenido (hold) y del que
    // Resume() lo suelta; el pursuit lo exime del "STOP -> 0" hasta que el cursor lo pasa. -1 = ninguno.
    private int    m_StopServidoWp;
    private bool   m_StopServidoPend;   // Resume() no tiene st: la marca se resuelve en el primer Sample con leg valido
    // PRIMER GAS TRAS EL HOLD a resolucion de tick (el heartbeat late cada 5 s y no resuelve una
    // salida de 0,8 s). Segundos desde Resume(); -1 = no hay salida de hold en curso.
    private float  m_HoldSalidaT;
    private const float PRIMER_GAS_TOPE_S = 30.0;
    // AUTORECOVERY v2 parte 1. Decide, cuenta y registra; la firma la detecta Sample.
    private ref BZAutoRecovery12 m_AR;
    // C1: AHOGO del motor. Detector (instrumento, siempre) + recuperacion primera + hillstart (opt-in).
    private const float AHOGO_GAS_MIN = 0.5;          // gas APLICADO: un ahogo necesita gas de verdad
    private const float AHOGO_ATRAS_KMH = 0.3;        // firma del usuario: pide adelante y rueda para atras
    private const float AHOGO_RALENTI_FRAC = 1.25;    // o el motor clavado cerca del ralenti con gas
    private const float AHOGO_EPISODIO_M = 8.0;
    private const float AHOGO_SALIDA_FRAC = 0.85;     // se devuelve el mando cerca de la velocidad grabada
    private const float AHOGO_SALIDA_MIN_KMH = 3.0;
    private const float AHOGO_TOPE_S = 20.0;
    private const int   AHOGO_HOLD_TICKS = 6;         // freno sostenido maximo del hillstart (3 s a 2 Hz)
    private const float AHOGO_QUIETO_S = 3.0;         // avanzo y se quedo -> traba, no ahogo
    private const int   AHOGO_MARCHA = 2;             // gear 2 = PRIMERA (0 reversa, 1 neutro)
    private bool  m_BogEnabled;
    private float m_BogWindowS = 3.0;
    private float m_BogMinProgM = 0.5;
    private int   m_BogMaxInt = 2;
    private float m_BogThrAp;         // gas / freno APLICADOS al final del ultimo Sample (40 Hz)
    private float m_BogBrkAp;
    private float m_BogFwdMinInt;     // minima velocidad con signo desde el ultimo Plan
    private int   m_BogLeg;
    private bool  m_BogVentOk;
    private float m_BogVentT;
    private float m_BogVentAlong0;
    private float m_BogRecVentT;        // la ventana de ABANDONO dentro de la recuperacion (avance por 10 s)
    private float m_BogRecVentAlong0;
    private bool  m_BogVioAtras;
    private bool  m_BogVioRalenti;
    private float m_BogFwdMinVent;
    private bool  m_BogEpisodio;
    private float m_BogAlongEp;
    private int   m_BogIntentos;
    private int   m_BogAvisos;
    private bool  m_BogActivo;
    private float m_BogRecT;
    private float m_BogQuietoT;
    private int   m_BogRampa;
    private float m_BogRecAlong0;
    // hueco A: TOPE DE SOSTEN. Cuando C1 devuelve el mando, la cinta no aplica una marcha mas larga que la
    // que sostuvo el avance mientras la TOMA muestre que la suya, a fondo, perdia velocidad en ese punto.
    private int   m_SostenTope;       // 0 = sin tope; si no, la marcha maxima que la cinta puede aplicar
    private bool  m_SostenAplicado;
    private float m_SostenS0;
    // 791 AR MANUAL: "ultimo bien pisado" (lateral contra la polilinea <= GoodLatM y avanzando) y config.
    private float m_ManAdvM = 10.0;
    private float m_ManGoodLatM = 1.0;
    private bool  m_ManBienOk;
    private float m_ManBienS;
    // F1 paso 1: MANIOBRA de salida (reversa recta ReverseM metros, freno, y el LAUNCH re-encara solo).
    private const float F1_REV_TOPE_S = 8.0;
    private const float F1_FRENO_S = 1.0;
    // 1: tras cerrar la maniobra, GRACIA para que el LAUNCH re-encare antes de que un FISICO/CLAVADO
    // suba escalon (medido: el CLAVADO que decidio el escalon 2 era la fila exacta del fin de la reversa).
    private const float F1_GRACIA_S = 4.0;
    private const float F1_ATRAS_RADIO_M = 2.5;
    private bool   m_F1Activo;
    private int    m_F1Fase;          // 0 reversa · 1 freno
    private float  m_F1T;
    private vector m_F1Pos0;
    private vector m_F1PosFin;
    private float  m_F1GraciaT;
    private int    m_F1Rampa;
    private int    m_F1Quieto;        // ticks de Plan sin retroceder (para subir la rampa solo si no se mueve)
    private float  m_F1RevM = 3.0;
    private float  m_F1RevKmh = 5.0;
    private bool   m_F1Adelante;   // la maniobra va ADELANTE (leg de reversa: alejarse del leg es ir adelante)
    private float  m_F1PicoKmh;
    // re-ubicacion pedida por la escalera fisica: la ejecuta el servicio (mueve el vehiculo) en Tick12
    private bool   m_ReubPend;
    private float  m_ReubPendOff;
    private float  m_ReubPendOrigen;
    // 2: inicio de la RACHA continua de "bien pisado" (para retroceder una batalla sin salirse de ella)
    private float  m_ManBienIni;
    private float  m_QuietoT;         // segundos quieto, sin depender de la cinta
    private const float CINTA_RODANDO_KMH = 2.0;
    private int   m_DiagIman;   // rate del diag IMAN
    // ★ LOG CSV PROPIO: una fila por tick de control, columnas
    // planas sin prosa — la traza a 40 Hz que el RPT (DIRECCION ~1.75 Hz, con
    // agujeros cuando no se comanda) no puede dar. Un archivo por corrida
    // (BZ12_LOG_<ruta>[#<etiqueta>]_<hhmmss>.csv en el profile); el RPT sigue para los
    // eventos narrativos. El mismo CSV es entrada directa del banco: paridad
    // fixture↔motor por construccion.
    private FileHandle m_LogFh;
    private bool  m_LogOn;
    private float m_LogT;
    private float m_ImanPide;          // iman PRE-clamp (columna pide)
    private float m_DiagPsiMed;        // psiMed del diag, para el log
    private float m_DiagLatDot;        // latDot del diag, para el log
    // Tuning reversa (, revivido 2ª vez en — ver la
    // cicatriz del null-space en la ley): C2 con volante grabado RECTO.
    private const float REV_C2_RECTA       = 1.0;
    private const float REV_C2_FW_RECTA_DEG = 3.0;
    // ★ EL IMAN EN REVERSA, CON MAS GANANCIA: C1 y C2 por REV_IMAN_FACTOR y el tope en REV_IMAN_CAP_DEG. Los legs de
    // reversa miden 30-40 m y el lazo de siempre tarda ~43 m de camino en cerrar un error (misma longitud de onda que
    // en avance, donde los legs son largos y las curvas suaves); a <= 15 km/h un ajuste de 12 grados no es un volantazo.
    // Banco cinematico (reversa final de HB02R/HB03R, cursor en su lugar): |lat| max 0,23/0,27 con el iman de siempre,
    // 0,16/0,16 con x3; llegada +0,18/+0,15 -> +0,08/+0,02. Con el cursor atrasado ninguna ganancia alcanza (0,91): el
    // anclaje de reversa de la cinta va primero.
    private const float REV_IMAN_FACTOR = 3.0;
    private const float REV_IMAN_CAP_DEG = 12.0;
    // ★ LA MISMA GANANCIA EN AVANCE, ANTES DE UN CUSP. El lateral de un cusp en avance se fabrica en la curva de entrada
    // (medido: 0,11 -> 0,63 m en los 7 m previos a la zona de maniobra, a 21 km/h, con el iman de siempre pidiendo -3
    // grados contra un subviraje de +10 grados/s) y los 6 m de maniobra no alcanzan para cerrarlo. Banco cinematico con
    // la perturbacion medida: x3 solo dentro de la maniobra deja 0,34 m; desde 2 zonas de maniobra, 0,13 m. Entra cuando
    // el leg termina en un cusp (no en una parada: el fin de ruta y las paradas semanticas siguen igual) y quedan
    // CUSP_IMAN_ZONA_WB batallas por el cursor, o ya en maniobra. Una linea por leg cuando engancha.
    private const float CUSP_IMAN_ZONA_WB = 6.0;   // 2 x la zona de maniobra del arbitro (3 batallas)
    private int m_ImanCuspLeg = -1;
    // latDot/psiMed: SOLO DIAG (columnas del CSV) desde — los dos
    // intentos de meterlos a la ley fallaron en el motor (ver cicatriz).
    // EMA α=0.1; prev/ema se invalidan sin posesion (cero spike post-seed).
    private float m_CintaLatPrev;
    private bool  m_CintaLatPrevOk;
    private float m_CintaLatDotEma;
    private const float IMAN_LATDOT_V_PISO = 0.5;   // m/s
    // Integral del lateral — ✖ FUERA DE LA LEY desde
    // (ver la cicatriz en el iman: no paso su falsador y la razon es que el
    // iman entero es el 2,3% del comando). Queda como DIAGNOSTICO: se calcula
    // con su gate y su leak y se loguea en la columna `imanI`, para poder
    // dimensionar cualquier intento futuro sin re-escribirlo.
    //   GATE : regimen del error chico y persistente (el null-space).
    //   LEAK : descarga fuera del gate y en saturacion (1/s).
    // ⚠ El GATE quedo corto para su proposito y el numero era del analista mal
    // usado: 0.30 salio de tomar la MEDIANA de los maximos por
    // corrida (0.252) como si fuera el maximo — el 14,9% de los frames de la
    // ventana estan por encima. No se corrige porque la palanca esta afuera;
    // queda anotado para que nadie reuse ese 0.30 como si estuviera validado.
    private float m_CintaLatInteg;
    private const float IMAN_I_GATE_M = 0.30;
    private const float IMAN_I_LEAK   = 2.0;
    // Cap asimetrico de apertura: con la rueda grabada >= umbral, el
    // sesgo que ABRE puede llegar a 12 deg (acotado por lo grabado, sin invertir
    // signo). Falsador: si wp270 oscila (cruza a -0.5 afuera tras abrir) -> 8.0.
    private const float IMAN_ABRIR_DEG        = 12.0;
    private const float IMAN_ABRIR_UMBRAL_DEG = 20.0;
    // ★★★ ESCALADO DE ALTA VELOCIDAD (, F1 medido: m4w a 170-200 con
    // cIman CLAVADO en ±4.50 toda la curva, pico +3.38 y rebote -2.87). El dato
    // del propio RPT fija la planta: en el rebote (sin perturbacion) 4.5 deg
    // entregan ~1.7 m/s2 -> gp ~0.38 (m/s2)/deg a 190, y la perturbacion de la
    // curva ~2.25 m/s2 pide ~5.9 deg SOLO para anularse: con cap 4.5 el iman ni
    // empata — la restriccion es AUTORIDAD (la direccion del analista), no lazo.
    // (Mi teoria v²-de-lazo quedo REFUTADA por el fit: el motor ya atenua el
    // steering con v — el gp chico ES esa atenuacion; ganancias/v² dieron pico
    // 7.1 en el banco = mutante retirado-con-hallazgo.) El rebote es amorti-
    // guacion: subir cap solo deja -2.21; con C2 alto queda -0.62. Banco 1-D
    // (planta fiteada + rate-limit 20 deg/s): actual 4.05/-3.65 (calibra contra
    // el 3.38/-2.87 medido) -> cap12+C2=2.0: pico 2.20 rebote -0.62 sat 0%;
    // robusto con d x1.5 (3.45/-0.98) y gp mitad (4.67, sat 20% — por eso cap 12
    // y no 9). Interpolacion lineal 120->220 km/h; DEBAJO DE 120 IDENTICO a hoy
    // (06/dificil/cuerpos validados intactos). C1 no se toca.
    private const float IMAN_V_ALTA_KMH  = 120.0;
    private const float IMAN_V_TOPE_KMH  = 220.0;
    private const float IMAN_CAP_ALTA_DEG = 12.0;
    private const float IMAN_C2_ALTA      = 2.0;
    // ★★★ E3b EL IMAN DE GAS: el replay abierto del pedal NO cierra
    // el error de velocidad (medido: Boris +10-29% sobre la kmh grabada llegando
    // a la curva 90 final — el usuario la tomo AL LOCK a 11.5-15.8 km/h coasteando, Boris
    // paso a 14.4-18.8, el mismo 30 deg dio radio mas abierto, salio 1.6 m y
    // CHOCO al salir). Es el "Δgas de Δv" de, diferido en E3 y ahora
    // justificado por la medicion. Ley aditiva sobre el pedal grabado:
    //   dv = kmhRec - kmh (chassis con chassis: comparables, el over-read ~11%
    //        cancela)
    //   gas  = clamp(thrRec + KV·dv)          (lento: suma; rapido: corta)
    //   freno= brkRec + KB·(−dv − LIBRE) SOLO con el gas YA cortado a cero
    //          (sin pumping por construccion) y fuera de la banda LIBRE
    // Banco longitudinal (planta ajustada DE LA TOMA por regresion, perturbacion
    // 7x la real: open-loop mediana 95%): KV/KB 0.08 → mediana 6.7%, lock 37%
    // (vs 362% sin iman) → en el motor real, con mismatch ~7x menor, ~±5%.
    // Falsadores: lock sigue >+10% con cGas −1.0 y cFrn 0.35 clavados → subir
    // KB/tope; gas y freno alternando por tick → subir LIBRE a 2.
    private const float IMAN_GAS_POR_KMH  = 0.08;
    private const float IMAN_FRN_POR_KMH  = 0.08;
    private const float IMAN_FRN_LIBRE_KMH = 1.0;
    // FRENO ANTICIPADO en quiebre de bajada. Un freno grabado a fondo que cae SOBRE una bajada fuerte, a
    // alta velocidad, se reparte ANTES: en el ultimo segundo previo al bloque grabado se suelta el gas y se regula
    // suave (misma ganancia que el iman) hacia la velocidad con la que el humano SALIO de esa frenada; al llegar al
    // bloque, si ya se viene a esa velocidad (+-MARGEN) y el iman no pide freno, el freno grabado NO se pisa.
    // Lo que clava la trompa es el escalon `brk 1` sobre el quiebre (peso adelante justo cuando la trompa ya va 3 grados
    // por debajo del camino), no la velocidad: las 2 corridas limpias y las 2 chocadas llegaron a 90,2-90,5 y salieron a
    // 86,7-86,8, con la misma lat. Banco sobre las 38 cintas: la condicion aparece en 13 bloques (11 KRASNOSTAV, 1 PAZ,
    // 1 TRADER_CAZADOR); en las otras 35 cintas, nunca -> ningun otro perfil cambia.
    private const float FRENO_ANT_V_KMH = 60.0;        // solo a alta velocidad (el caso: 88)
    private const float FRENO_ANT_T_S = 1.0;           // cuanto antes se mira / se reparte
    private const float FRENO_ANT_PEND_DEG = -5.0;     // bajada fuerte (grados, + = subida)
    private const float FRENO_ANT_MARGEN_KMH = 2.0;    // "ya viene a la velocidad de salida"
    private float m_FrenoAntSIni = -1;                 // bloque anunciado (una linea por bloque)
    private float m_FrenoAntSBloq = -1;                // bloque resuelto al entrar (una linea por bloque)
    private const float IMAN_FRN_TOPE     = 0.35;
    private float m_ImanGas;       // sesgo de gas aplicado (diag; − = corta)
    private float m_ImanFrn;       // sesgo de freno aplicado (diag)
    private ref BZDriveGoal           m_Goal;
    private ref BZDriveOutput         m_Out;

    private int   m_Mode;
    // DIAGNOSTICO de precision: error de pose en el instante EXACTO en que se cerro un
    // leg. Es el unico momento en que el numero es medible sin ambiguedad (el vehiculo
    // esta detenido y clavado). De aca sale la medicion del intercambio.
    private int   m_LastClosedLeg;
    private float m_LastCloseAlongM;
    private float m_LastCloseLatM;
    private int   m_EventType;
    private int   m_EventWp;
    private string m_MotivoStuck = "";   // por que se declaro STUCK (el runner lo pregunta, como MotivoCierre)
    private string m_BloqueoMotivo = "";   // el ultimo BLOQUEADO (la evidencia con que se abrio la escalera)
    private bool  m_TrabaAvisada;           // una traba por progreso se clasifica UNA vez hasta que vuelve a progresar
    private bool  m_EscaleraPrev;           // la escalera del AR en el Plan anterior (el aviso sale de la transicion)
    private vector m_UltimoPos;             // pose y velocidad del ultimo Plan, para la linea de cierre de leg
    private float m_UltimoKmh;
    private bool  m_Ready;
    private int   m_SurfKind;

    // Pose final cacheada en el ultimo Plan(): deja recomputar el freno de endpoint
    // por FRAME sin volver a correr el leg-machine.
    // el remanente longitudinal medido en el MISMO tick en que se refresco
    // `m_Leg`. Entre refrescos, `m_Leg.remainM` es una foto vieja; restarle
    // (m_RemFrAtPlan - remanenteDeEsteFrame) la corrige por el avance real.
    private float  m_RemFrAtPlan;
    // Lo que la compuerta de entrada al endpoint COMPARO en este frame (columna `remG`).
    private float  m_RemGate;
    private vector m_EndPos;
    private float  m_EndTanX;
    private float  m_EndTanZ;

    // Slew del volante: el OBJETIVO lo fija Plan(), el APLICADO lo rampea Sample().
    // Son dos variables distintas a proposito -- si Sample pisara el objetivo con el
    // valor rampeado, el volante se clavaria en el primer paso de rampa.
    private float m_TargetSteer;
    private float m_AppliedSteer;

    // Ventana del BZLegView, en metros. Atras alcanza para medir pose; adelante cubre
    // la frenada desde velocidad de crucero.
    private const float WIN_BACK_M = 25.0;
    private const float WIN_FWD_M  = 80.0;
    // Slew del volante por frame (SEAM.md cara 3): uno de los DOS unicos refinamientos
    // permitidos en Sample(). Sin esto, un salto de comando entre ticks de 500 ms se
    // aplica de golpe.
    //
    // ★★★ L3: el tope ya no es fijo de flota -- ver SlewPorVehiculo. El
    // 3.0 queda como TECHO ABSOLUTO (nunca mas rapido que hoy) y como fallback sin Manual.
    private const float STEER_SLEW_PER_S = 3.0;
    // La CREMALLERA REAL, medida en las grabaciones humanas a 40 Hz (front_wheel_deg):
    // barridos sostenidos >10 grados en 6 vehiculos distintos dan p50 48-55 y p90 66-80
    // deg/s, UNIFORME en la flota y PLANO en velocidad (los picos 127-247 son rafagas de
    // un frame). 80 = el p90: el comando puede pedir lo que la rueda de verdad hace.
    // ⚠ NO salir del config (Steering increase/decreaseSpeed): declara ~1/2 del real y
    // fue la trampa que casi funda este spec (la tabla "medida" del era config).
    private const float RUEDA_RATE_RAD_S = 1.396;   // 80 deg/s
    // AUTORIDAD DE VOLANTE A BAJA VELOCIDAD. Por debajo del creep del vehiculo, el angulo
    // de direccion no hace girar nada: las ruedas cruzadas solo aran (scrub) y desalinean.
    // Es la fraccion de tope permitida con el vehiculo detenido; sube con la velocidad.
    private const float STEER_AUTH_MIN = 0.35;

    void BZDriveController()
    {
        m_LegMachine = new BZLegMachine();
        m_Leg        = new BZLegView();
        m_Arbiter    = new BZModeArbiter();
        m_Pursuit    = new BZPurePursuit();
        m_Derivation = new BZDerivationEngine();
        if (s_MarchaForzada >= 2) m_Derivation.ForzarMarcha(s_MarchaForzada);
        m_Maneuver   = new BZManeuverController();
        m_Slam       = new BZEndpointSlam();
        m_CintaM     = new BZCintaMuestra();
        m_AR         = new BZAutoRecovery12();
        m_BogFwdMinInt = 1000.0;
        m_BogLeg     = -999;
        m_Goal       = new BZDriveGoal();
        m_Out        = new BZDriveOutput();
        m_Mode       = BZDriveMode.BZDM_IDLE;
        m_EventType  = BZDriveEv.NONE;
        m_EventWp    = -1;
        m_Ready      = false;
        m_SurfKind   = BZSurf.HARD;
        m_LastClosedLeg = -1;
        m_RKmh    = new array<float>();
        m_RThr    = new array<float>();
        m_RBrk    = new array<float>();
        m_RStr    = new array<float>();
        m_RRpm    = new array<float>();
        m_RPitch  = new array<float>();
        m_RLat    = new array<float>();
        m_RX      = new array<float>();
        m_RZ      = new array<float>();
        m_RT      = new array<float>();
        m_RLd     = new array<float>();
        m_RkArco  = new array<float>();
        m_RGear   = new array<int>();
        m_RRuedas = new array<int>();
        m_RClutch = new array<int>();
        int r;
        for (r = 0; r < RING_N; r++)
        {
            m_RKmh.Insert(0);
            m_RThr.Insert(0);
            m_RBrk.Insert(0);
            m_RStr.Insert(0);
            m_RRpm.Insert(0);
            m_RPitch.Insert(0);
            m_RLat.Insert(0);
            m_RX.Insert(0);
            m_RZ.Insert(0);
            m_RT.Insert(0);
            m_RLd.Insert(0);
            m_RkArco.Insert(0);
            m_RGear.Insert(0);
            m_RRuedas.Insert(0);
            m_RClutch.Insert(0);
        }
        m_RIdx = 0;
        m_RCant = 0;
        m_RTiempo = 0;
        m_Volcados = 0;
    }

    // ======================= COMANDOS (cara 2) =======================

    void Configure(BZRouteData route, BZVehicleManual manual, BZWorldProbe probe)
    {
        if (!m_SegMuestras) m_SegMuestras = new array<float>();
        if (!m_SegTrSum)
        {
            m_SegTrSum = new array<float>();
            m_SegTrCnt = new array<int>();
            m_SegTrMax = new array<float>();
            int tq;
            for (tq = 0; tq < SEG_TRAMOS_MAX; tq++)
            {
                m_SegTrSum.Insert(0);
                m_SegTrCnt.Insert(0);
                m_SegTrMax.Insert(0);
            }
        }
        m_Route  = route;
        m_Manual = manual;
        m_FrenoTomaAcoplado = false;
        m_Probe  = probe;
        m_Ready  = false;
        if (!route) return;
        if (!route.IsReady()) return;
        if (!manual) return;
        m_LegMachine.Configure(route, WIN_BACK_M, WIN_FWD_M);
        OlvidarStopServido();
        m_StopServidoPend = false;
        m_HoldSalidaT = -1;
        m_Arbiter.Reset();
        m_Derivation.Reset();
        // (A2): el instrumento de "quieto". Dice de donde sale cada umbral para que el RPT no
        // haga adivinar: idle = lo MEDIDO (0 en un no-reptante); objetivo = el arrastre de los modulos POSE.
        if (!s_Quiet)
        {
            string sq = "[C12] QUIETO: umbral casi-parado " + UmbralCasiParado().ToString() + " km/h";
            sq = sq + " · idle creep medido " + manual.idleCreepKmh.ToString();
            sq = sq + " · objetivo de arrastre (POSE) " + manual.creepKmh.ToString();
            sq = sq + " · autoridad de volante plena desde " + EscalaAutoridadKmh().ToString() + " km/h";
            sq = sq + SufijoRunner();
            Print(sq);
        }
        m_Maneuver.Reset();
        m_Slam.Reset();
        m_Out.Reset();
        m_EventType = BZDriveEv.NONE;
        m_Ready = true;
    }

    void SeekTo(int wp)
    {
        m_LegMachine.SeekTo(wp);
        OlvidarStopServido();
        m_StopServidoPend = false;
        m_HoldSalidaT = -1;
        m_Maneuver.Reset();
        m_Slam.Reset();
        m_Derivation.Reset();
        m_Arbiter.Reset();
    }

    void Resume()
    {
        m_Maneuver.Reset();
        m_Slam.Reset();
        m_Arbiter.NotifyResume(m_Leg);
        // SALIR DE UN HOLD ES UNA DISCONTINUIDAD, NO UNA TRABA. Medido en la linea de buses (RPT 20/09 09:21):
        //  (1) el detector de choque comparaba la velocidad de ANTES del hold (9 km/h, cinco segundos atras) con la de
        //      ahora (0) en un dt de 25 ms: "CHOQUE -100 m/s2 SIN CONTACTO" en cada parada -> el historial se reinicia;
        //  (2) el reposo con la cinta poseyendo seguia contando desde donde quedo -> a cero;
        //  (3) la cinta, si posee, quedo sobre la fila donde el vehiculo se detuvo (coasteo a 8 km/h o la entrada de la
        //      meseta, gas 0): un vehiculo quieto con esa fila no arranca nunca. Se suelta en el primer Sample (ahi hay st),
        //      C1 relanza y la cinta se re-arma pasada la meseta: la MISMA suelta que el AR hacia como "NO-FISICO" en 38
        //      de las 57 paradas de hoy. No era una recuperacion: era la salida de la parada.
        m_ChoqueKmhPrev = 0;
        m_CintaReposoT = 0;
        m_SalirDelHold = true;
        m_StopServidoPend = true;   // el stop del que sale queda SERVIDO (ver ActualizarStopServido)
        m_HoldSalidaT = 0;   // arranca el reloj del primer gas
    }

    // EL PRIMER GAS TRAS EL HOLD, a resolucion de tick. El heartbeat late cada 5,15 s: contra el, una
    // salida de 0,8 s y una de 5 s son el mismo numero (la "mediana 3,0 s" del era en buena parte el periodo
    // del instrumento). Una linea por salida de hold, con el reloj arrancado en Resume(): el primer tick con gas
    // aplicado, o NO en PRIMER_GAS_TOPE_S. Lleva el objetivo y quien lo puso (STOP-SERVIDO / GRABADO) y la etiqueta.
    private void ReportarPrimerGasTrasHold(BZVehicleState st, float dt)
    {
        if (m_HoldSalidaT < 0) return;
        m_HoldSalidaT = m_HoldSalidaT + dt;
        bool hayGas = (m_Out.throttle > 0.01);   // el gas de ESTE cuadro (Sample), no el latch del Plan
        if (!hayGas && m_HoldSalidaT < PRIMER_GAS_TOPE_S) return;
        float tHold = m_HoldSalidaT;
        m_HoldSalidaT = -1;
        if (s_Quiet) return;
        string pg = "[C12] PRIMER GAS tras el hold: ";
        if (hayGas) pg = pg + "+" + tHold.ToString() + " s · gas " + m_Out.throttle.ToString();
        else pg = pg + "NO en " + tHold.ToString() + " s · gas 0";
        pg = pg + " · freno " + m_Out.brake.ToString() + " · v " + st.chassisKmh.ToString() + " km/h";
        if (m_Leg && m_Leg.valid) pg = pg + " · cursor " + m_Leg.nearWp.ToString();
        if (m_Pursuit && m_Goal) pg = pg + " · objetivo " + m_Pursuit.ObjetivoCorto(m_Goal.speedMs * 3.6);
        pg = pg + SufijoRunner();
        Print(pg);
    }

    // UN STOP SERVIDO NO PIDE 0.
    // Medido (RPT 21/09 13:20, 44 salidas de hold con el cursor EN el wp isStop): BZPurePursuit.TargetAt devolvia 0
    // para ese wp y el vehiculo salia igual en 42 -- por un accidente: con objetivo 0 la derivacion pide un gas
    // minimo para cancelar la resistencia al rodaje, el piso de arranque lo sube a 0,35 y la rampa lo hace rodar
    // hasta que el cursor pasa el wp (mediana 3,0 s hasta el primer gas). En el unico stop en bajada fuerte de las
    // dos rutas (wp 13157, -9,7 %) ese mismo termino pide FRENO: 2 de 2 clavados para siempre, con AR=0 (la cinta
    // no posee y no hay gas aplicado: ninguna firma del AR da verde) y el 108 en cola detras. Resume() significa
    // "segui": el stop del que se sale queda SERVIDO y deja de pedir 0 hasta que el cursor lo pasa. Quien sirve las
    // paradas en 12 sigue siendo la meseta grabada + el hold del servicio, no este 0 de C1.
    // La marca se resuelve en el primer Sample con leg valido (Resume no tiene st): el isStop mas cercano al cursor
    // por arco, dentro del alcance de la meseta (la MISMA distancia a la que una detencion cuenta como "llego"), y
    // nunca el fin del leg (eso lo cierra OnLegDone / ENDPOINT, no un hold).
    private void ActualizarStopServido()
    {
        if (!m_Leg || !m_Leg.valid) return;
        if (m_StopServidoWp >= 0 && m_Leg.nearWp > m_StopServidoWp) OlvidarStopServido();   // ya lo paso: la marca sobra
        if (!m_StopServidoPend) return;
        m_StopServidoPend = false;
        MarcarStopServido();
    }

    private void OlvidarStopServido()
    {
        m_StopServidoWp = -1;
        if (m_Pursuit) m_Pursuit.SetStopServido(-1);
    }

    private void MarcarStopServido()
    {
        if (!m_Route) return;
        int i;
        int mejor = -1;
        float mejorD = 0;
        float d;
        for (i = m_Leg.winLo; i <= m_Leg.winHi; i++)
        {
            if (i >= m_Leg.legHi) break;
            if (!m_Route.IsStop(i)) continue;
            d = Math.AbsFloat(m_Route.S(i) - m_Leg.cursorS);
            if (mejor < 0 || d < mejorD)
            {
                mejor = i;
                mejorD = d;
            }
        }
        if (mejor < 0) return;
        if (mejorD > BZCintaRunner.SALTO_CONSULTA_M) return;
        m_StopServidoWp = mejor;
        if (m_Pursuit) m_Pursuit.SetStopServido(mejor);
        if (s_Quiet) return;
        string ss = "[C12] STOP SERVIDO: el wp " + mejor.ToString() + " (isStop, a " + mejorD.ToString() + " m del cursor " + m_Leg.nearWp.ToString() + ")";
        ss = ss + " deja de pedir 0 hasta que el vehiculo lo pase";
        ss = ss + SufijoRunner();
        Print(ss);
    }

    // EL HOLD ENGANCHA EN LA MESETA GRABADA (consultas para BZBusService.HoldRunner / SampleDrive12).
    // Arco desde el cursor hasta el FIN de la proxima meseta grabada de v=0, dentro del alcance de FinMesetaAdelante
    // (SALTO_CONSULTA_M); -1 si no hay ninguna a la vista o la cinta no posee: entonces el hold engancha ya, como siempre.
    float MesetaAdelanteM()
    {
        if (!m_Cinta || !m_Cinta.Valida() || !m_CintaRodo) return -1;
        float finM = m_Cinta.FinMesetaAdelante();
        if (finM < 0) return -1;
        float dM = finM - m_Cinta.SEst();
        if (dM < 0) dM = 0;
        return dM;
    }
    // El arco donde EMPIEZA esa meseta (para enganchar cuando el cursor llega), o -1.
    float MesetaEntradaS()
    {
        if (!m_Cinta || !m_Cinta.Valida() || !m_CintaRodo) return -1;
        return m_Cinta.InicioMesetaAdelante();
    }
    float CintaKmhGrabada() { return m_CintaM.kmhRec; }
    // la cinta esta reproduciendo una meseta grabada (y donde esta), para que el runner pida el hold en una
    // parada declarada sin esperar a BusDrive.
    bool CintaEnMeseta() { return m_Cinta && m_Cinta.Valida() && m_CintaRodo && m_Cinta.EnMeseta(); }
    vector CintaMesetaPos()
    {
        if (!m_Cinta) return "0 0 0";
        return m_Cinta.MesetaPos();
    }
    // ¿Ya es momento de enganchar el hold pedido? Si: el cursor llego a la ENTRADA de la meseta (por arco: a 3 km/h una
    // meseta de 2,8 cm se salta en un paso de 40 Hz y la fila de reposo no se ve nunca), o el vehiculo esta quieto, o
    // recorrio mas que el alcance de la meseta desde el pedido (la meseta quedo atras: enganchar ya). Cero umbrales nuevos.
    bool HoldEnMesetaListo(BZVehicleState st, vector pedidoPos, float mesetaS)
    {
        if (mesetaS >= 0 && m_Cinta && m_Cinta.SEst() >= mesetaS) return true;
        if (Math.AbsFloat(st.chassisKmh) < LOG_COLA_QUIETO_KMH) return true;
        if (!m_Cinta) return true;
        float hdx = st.pos[0] - pedidoPos[0];
        float hdz = st.pos[2] - pedidoPos[2];
        float alc = m_Cinta.AlcanceMesetaM();
        return (hdx * hdx + hdz * hdz) > alc * alc;
    }

    // UNA sola suelta con re-armado por arco: la del deadlock de (via AR) y la salida de un hold.
    // La cinta deja de poseer, C1 relanza desde el reposo, y la cinta se re-arma cuando el vehiculo recorrio lo que
    // faltaba hasta el FIN de la meseta grabada; sin meseta a la vista, se re-arma por velocidad como siempre.
    private void SoltarCintaHastaFinDeMeseta(BZVehicleState st, string porQue)
    {
        m_CintaRodo = false;
        m_CintaQuieto = true;
        m_CintaSueltaPos = st.pos;
        m_CintaRearmaDist = -1;
        float finMes = m_Cinta.FinMesetaAdelante();
        if (finMes >= 0) m_CintaRearmaDist = finMes - m_Cinta.SEst();
        // con la meseta NO enganchada (el hold detuvo el vehiculo antes de su entrada), lo ya servido se consume y
        // el cursor pasa a la fila del vehiculo; si no, el cursor queda congelado durante la suelta y al re-armar cae clavado
        // en una fila de reposo con el vehiculo ya en marcha (Novodmitrovsk: -1 m de atraso hasta la curva, 5 trabas en 12).
        bool servidas = false;
        if (!m_Cinta.EnMeseta()) servidas = m_Cinta.ConsumirMesetasServidas(st.pos);
        m_Cinta.SoltarMeseta();   // la meseta queda cumplida y el cursor en su salida; el re-arme sigue desde ahi
        string dl = "[C12] CINTA SUELTA " + porQue;
        if (servidas) dl = dl + " · cursor re-anclado en la fila del vehiculo y mesetas servidas consumidas";
        dl = dl + " · cinta v " + m_CintaM.kmhRec.ToString() + " freno " + m_CintaM.brk.ToString();
        dl = dl + " · s " + m_Cinta.SEst().ToString() + " · leg " + m_Leg.legIdx.ToString();
        if (m_CintaRearmaDist >= 0) dl = dl + " -> re-arma al recorrer " + m_CintaRearmaDist.ToString() + " m (fin de meseta)";
        else dl = dl + " -> sin meseta en 8 m: re-arma por velocidad";
        dl = dl + SufijoRunner();
        Print(dl);
    }

    void Abort()
    {
        m_Arbiter.ForceHalt();
        m_Out.SetHalt();
        m_FinPendiente = false;   // la ruta se corta: no hay veredicto que esperar
        m_Mode = BZDriveMode.BZDM_HALTED;   // el estado expuesto tiene que reflejarlo YA,
                                            // no recien en el proximo Plan()
    }

    // ======================= ESTADO (cara 1) =======================

    int   GetWaypointIndex() { return m_Leg.nearWp; }
    int   GetMode()          { return m_Mode; }
    float GetCrossTrackM()   { return m_Leg.crossTrackM; }
    bool  IsReady()          { return m_Ready; }
    bool  LegEnReversa()     { return m_Leg && m_Leg.valid && m_Leg.isReverse; }
    float GetRemainM()       { return m_Leg.remainM; }
    float GetSignedToEndM()  { return m_Leg.signedToEndM; }
    float GetEndpointMinDistM() { return m_Slam.GetMinDistM(); }
    int   GetEndpointNoProgressTicks() { return m_Slam.GetNoProgressTicks(); }
    bool  IsEndpointLatched()   { return m_Slam.IsLatched(); }

    // ★ LOG CSV: abre/cierra el servicio en el arranque/FIN de
    // la corrida; el volcado va al final de Sample. Sin prosa: parseo directo.
    void AbrirLog(string etiqueta)
    {
        CerrarLogYa();   // DURO — abrir sobre un handle en cola lo filtraria
        if (!BZCajaNegra.Encendida()) return;   // apagadas por defecto (BlackBox en BZAutoDrive_settings.json)
        int lh = 0;
        int lm = 0;
        int ls = 0;
        GetHourMinuteSecondUTC(lh, lm, ls);
        string hh = LogPad2(lh) + LogPad2(lm) + LogPad2(ls);
        // la etiqueta va tambien en el nombre del archivo (BZ12_LOG_<ruta>#<etiqueta>_<hhmmss>.csv):
        // dos vehiculos sobre la misma toma se distinguian solo por el segundo en que arrancaron.
        string lpath = "$profile:BZ_AutoDrive\\BZ12_LOG_" + etiqueta + "_" + hh + ".csv";
        m_LogFh = OpenFile(lpath, FileMode.WRITE);
        if (m_LogFh == 0)
        {
            // antes fallaba en silencio; el nombre con '#' es nuevo y si el motor lo rechazara tiene que verse en el RPT
            Print("[BZDrive] LOG CSV: no se pudo abrir " + lpath);
            return;
        }
        BZCajaNegra.Anotar(etiqueta, "BZ12_LOG_" + etiqueta + "_" + hh + ".csv");
        // px,pz al final: la posicion REAL del casco — sin ella el
        // fixture no pudo reproducir la adopcion exacta del teleport (la
        // geometria fina del punto decide que proyeccion es interior). Con
        // ellas, paridad fixture↔motor por construccion: el espejo re-corre
        // el torneo con el casco exacto de la corrida.
        // ★ rem,remFr al final: la ENTRADA al endpoint la decide
        // `m_Leg.remainM <= EndpointZone`, y `m_Leg` SOLO se refresca en Plan
        // (2 Hz). El CSV no tenia ninguna de las dos cantidades: `d` es la
        // distancia al cursor de la CINTA, no al final del leg. Sin `rem` no
        // se puede reproducir la compuerta offline (el banco anclado a las 6
        // corridas del M4 dio 0/6 reconstruyendo el remanente por arco: el
        // cursor no es el casco). `remFr` es el mismo remanente medido por
        // FRAME contra la pose final -- la diferencia entre las dos columnas
        // ES el error de muestreo que sortea la entrada. INSTRUMENTO PURO.
        FPrintln(m_LogFh, "t,wp,s_est,kmh,gear,lat,psiGeo,psiMed,latDot,pide,aplica,cStr,modo,aplica_quien,legIdx,faseRev,ultCons,skipCons,d,px,pz,imanI,rem,remFr,remG,latFr,thr,brk,ruedas,cont_zona,cont_con,cont_imp,cont_hace,frenoFin");
        m_LogOn = true;
        m_LogT = 0;
        Print("[BZDrive] LOG CSV abierto: " + lpath);
    }

    // ★★★ EL LOG NO CIERRA EN `ROUTE_COMPLETE`: CIERRA EN EL REPOSO (
    // §5). La compuerta de llegada se abre cuando el vehiculo CRUZA la
    // tolerancia, y en las rutas que llegan lentas eso pasa TODAVIA RODANDO a
    // 1.0-1.7 km/h: el archivo cortaba ahi y la ultima fila no era donde quedo
    // el casco sino donde estaba cuando se declaro la llegada. Medido: Matador
    // / LANDROVER / SPEED01 cerraban con 0.00 s de reposo registrado (contra
    // 0.15-0.38 s de las que clavan con envion), asi que su residuo de
    // -0.17/-0.20 NO era medible. Cola: se sigue logueando hasta |v| < 0.3
    // km/h sostenido 0.5 s, con tope de 3 s. INSTRUMENTO PURO — no toca quien
    // maneja: el servicio sigue devolviendo false en SampleDrive12 y el freno
    // /hold del legacy hace lo mismo de siempre.
    private const float LOG_COLA_QUIETO_KMH = 0.3;
    private const float LOG_COLA_QUIETO_S   = 0.5;
    private const float LOG_COLA_TOPE_S     = 3.0;
    private bool  m_LogCola;
    private float m_LogColaT;
    private float m_LogQuietoT;

    // la cola corre CON O SIN caja negra: la linea REPOSO (donde quedo el casco contra la pose final, una vez quieto) es
    // del FIN, no del archivo. Las filas solo se escriben si la caja esta abierta (VolcarLog se guarda solo).
    void CerrarLog()
    {
        if (m_LogCola || m_FinDecidido) return;      // ya en cola, o el veredicto ya salio del reposo: no re-armar
        m_LogCola = true;
        m_LogColaT = 0;
        m_LogQuietoT = 0;
    }

    bool LogEnCola() { return m_LogCola; }

    // La fila de la cola: MISMA forma que la de manejo (el parser no cambia).
    void VolcarLogCola(BZVehicleState st, float dt)
    {
        if (!m_LogCola) return;
        VolcarLog(st, dt);
        m_LogColaT = m_LogColaT + dt;
        if (Math.AbsFloat(st.chassisKmh) < LOG_COLA_QUIETO_KMH) m_LogQuietoT = m_LogQuietoT + dt;
        else m_LogQuietoT = 0;
        if (m_LogQuietoT >= LOG_COLA_QUIETO_S || m_LogColaT >= LOG_COLA_TOPE_S)
        {
            // ★ LA MEDIDA UNICA DE LA LLEGADA, EN REPOSO: la distancia a la POSE FINAL del ultimo leg, longitudinal (+ =
            // falta llegar, - = pasada) y lateral (+ = a la izquierda de la linea de llegada) por separado, y la total. El
            // veredicto (COMPLETE/INCOMPLETE) se decide ACA, con esta medida (DecidirFin), no al cerrar el leg: la red de
            // ACEPTA_ENDPOINT_RED_M ya no da COMPLETE. `d` de la caja negra es la distancia al CURSOR de la cinta, no a la pose final.
            float lonR = RemanenteFrameM(st);
            float latR = LateralFrameM(st);
            float totR = Math.Sqrt(lonR * lonR + latR * latR);
            string rep = "[C12] REPOSO tras " + m_LogColaT.ToString() + " s (" + st.chassisKmh.ToString() + " km/h): a " + totR.ToString() + " m de la pose final";
            rep = rep + " (longitudinal " + lonR.ToString() + " m, lateral " + latR.ToString() + " m)";
            if (m_LogQuietoT < LOG_COLA_QUIETO_S) rep = rep + " · todavia rodando al medir";
            if (!s_Quiet) Print(rep + SufijoRunner());
            if (m_FinPendiente) DecidirFin(m_LogColaT, totR, lonR, latR);
            CerrarLogYa();
        }
    }

    // Cierre DURO. Lo usa la cola al terminar, el despawn (fail-safe: si el
    // casco se va antes del reposo, el handle no puede quedar abierto) y
    // AbrirLog (abrir sobre un handle vivo seria un leak).
    void CerrarLogYa()
    {
        m_LogCola = false;
        if (!m_LogOn) return;
        CloseFile(m_LogFh);
        m_LogOn = false;
    }

    private string LogPad2(int n)
    {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }

    private void VolcarLog(BZVehicleState st, float dt)
    {
        if (!m_LogOn) return;
        m_LogT = m_LogT + dt;
        string quien = "fallback";
        if (m_CintaTick)
        {
            quien = "cinta";
            if (m_CintaVol) quien = "cintaV";
            else if (m_Leg && m_Leg.valid && m_Leg.isReverse) quien = "cintaR";
        }
        int lwp = -1;
        int llegIdx = -1;
        int lfase = 0;
        if (m_Leg && m_Leg.valid)
        {
            lwp = m_Leg.nearWp;
            llegIdx = m_Leg.legIdx;
            if (m_Leg.isReverse) lfase = 1;
        }
        float lsest = 0;
        int lult = -1;
        int lskip = 0;
        if (m_Cinta && m_Cinta.Valida())
        {
            lsest = m_Cinta.SEst();
            lult = m_Cinta.UltCons();
            lskip = m_Cinta.SkipConsumidos();
        }
        float llat = 0;
        float lpsig = 0;
        float lpsim = 0;
        float lldot = 0;
        float lpide = 0;
        float lapl = 0;
        if (m_CintaTick)
        {
            llat = m_CintaM.latM;
            lpsig = m_CintaPsi;
            lpsim = m_DiagPsiMed;
            lldot = m_DiagLatDot;
            lpide = m_ImanPide;
            lapl = m_ImanDeg;
        }
        string fila = m_LogT.ToString() + "," + lwp.ToString() + "," + lsest.ToString() + "," + st.chassisKmh.ToString();
        fila = fila + "," + m_Out.gear.ToString() + "," + llat.ToString() + "," + lpsig.ToString() + "," + lpsim.ToString();
        fila = fila + "," + lldot.ToString() + "," + lpide.ToString() + "," + lapl.ToString() + "," + m_Out.steering.ToString();
        fila = fila + "," + m_Mode.ToString() + "," + quien + "," + llegIdx.ToString() + "," + lfase.ToString();
        fila = fila + "," + lult.ToString() + "," + lskip.ToString() + "," + m_CintaDist.ToString();
        fila = fila + "," + st.pos[0].ToString() + "," + st.pos[2].ToString();
        fila = fila + "," + m_CintaLatInteg.ToString();   // el integral, para falsarlo
        // las dos distancias al FINAL DEL LEG. `lrem` es la que lee la
        // compuerta de entrada al endpoint (2 Hz, del cursor); `lremfr` la misma
        // medida con la pose de este frame. Cuando difieren, la entrada llega tarde.
        float lrem = -1;
        float lremfr = 0;
        if (m_Leg && m_Leg.valid)
        {
            lrem = m_Leg.remainM;
            lremfr = RemanenteFrameM(st);
        }
        // `remG` = lo que la compuerta COMPARO contra EndpointZone en este frame.
        // Con `rem` y `remFr` se podia reconstruir; con esto se verifica la implementacion
        // en vez del modelo que uno tiene de ella.
        fila = fila + "," + lrem.ToString() + "," + lremfr.ToString() + "," + m_RemGate.ToString();
        // el LATERAL al punto final, en el marco de la pose final y arrancando
        // cerca de cero. Con `remFr` forman el VECTOR de endpoint a precision util --
        // px,pz NO sirven para eso (ver LateralFrameM).
        float llatfr = 0;
        if (m_Leg && m_Leg.valid) llatfr = LateralFrameM(st);
        fila = fila + "," + llatfr.ToString();
        // los pedales APLICADOS en este cuadro, al FINAL de la fila para que ningun parser por posicion se corra:
        // es la unica forma de ver el sosten de la meseta (y cualquier pelea de pedales) en el pedal, a 40 Hz.
        fila = fila + "," + m_Out.throttle.ToString() + "," + m_Out.brake.ToString();
        // las ruedas en el suelo y el ultimo contacto del motor (dentro de CHOQUE_CONTACTO_S; vacio si no hubo): un
        // aterrizaje o un choque cuadro a cuadro. "terreno" = el motor no dio objeto (`other` nulo)
        string lczB = "";
        string lccB = "";
        string lciB = "";
        string lchB = "";
        if (st.contactoHaceS >= 0 && st.contactoHaceS <= CHOQUE_CONTACTO_S)
        {
            lczB = st.contactoZona;
            lccB = st.contactoClase;
            if (lccB == "") lccB = "terreno";
            lczB.Replace(",", ";");
            lccB.Replace(",", ";");
            lciB = st.contactoImp.ToString();
            lchB = st.contactoHaceS.ToString();
        }
        fila = fila + "," + st.wheelsOnGround.ToString() + "," + lczB + "," + lccB + "," + lciB + "," + lchB;
        fila = fila + "," + m_FrenoFinB.ToString();
        FPrintln(m_LogFh, fila);
    }

    // ★ FRENO MEDIDO de la grabacion: EndpointBrakeDecel del
    // header — la decel de clavado que el HUMANO logro con ESTE vehiculo en
    // ESTE piso. El hallazgo del 31/07 aplicado al techo: el MaxBrakeDecel
    // del config MIENTE (optimista; 08b: config ~9 vs medido 2.49) — con el
    // optimista el techo no mordia en las frenadas flojas (08b llegaba a
    // 14.9 km/h a 1 m cuando su fisica real permite 8.0) Y mordia de mas en
    // las fuertes (ExpTractor 0.070→0.206). El dato es el ancla, no la
    // formula. 0 = la ruta no lo trae → fallback al manual (hoy).
    private float m_FrenoMedidoMs2;
    void SetFrenoMedido(float aMs2) { m_FrenoMedidoMs2 = aMs2; }
    // ✖ RETIRADA TECHO_VENTANA_M: la ventana metrica fija era un
    // intercambio hypercar↔tractor. El gate de CONVERGENCIA la reemplaza:
    // el techo aplica solo donde kmhRec <= vClav + margen — mordida acotada
    // por construccion, cero aplicacion en crucero, sin eje de distancia.
    // el margen es max(este PISO validado, 2.5·aMedido) — ver la
    // cicatriz en el gate. El piso solo, fijo, dejaba a 08b a 0.2 km/h del
    // borde de la condicion.
    private const float TECHO_CONV_KMH = 5.0;

    // LA CINTA (E2): el servicio la carga y la entrega; NULL la apaga.
    void SetCinta(BZCintaRunner c)
    {
        m_Cinta = c;
        m_FrenoTomaAcoplado = false;   // la frenada final de esta cinta al manual (cuando la cinta este armada)
        // la identidad viaja con la cinta. SetCinta se llama DESPUES de SetAutoRecovery
        // (12739 contra 12664 en el servicio), asi que este es el punto donde la etiqueta llega de verdad.
        if (m_Cinta) m_Cinta.SetEtiqueta(m_EtiqRunner);
        m_LegFaseSet = false;   // el tracker de fase-del-leg arranca de cero
        m_CintaRodo = false;    // el latch de posesion arranca desarmado
        m_CintaQuieto = false;  // y sin reposo visto (arme por flanco)
        m_CintaReposoT = 0;
        m_CintaRearmaDist = -1;  // un float miembro nace en 0, no en -1 (0 = objetivo de 0 m)
        m_CintaRodoLeg = -999;
    }

    // la config del AR la entrega el servicio (bz12drive no lee config de ruta).
    void SetAutoRecovery(bool logEn, float waitS, int maxInt, float coolS, bool fisEn, float fisRevM, float fisRevKmh, float fisLadderM, float fisSweepM, string ruta, string veh)
    {
        m_F1RevM = fisRevM;
        if (m_F1RevM < 0.5) m_F1RevM = 0.5;
        m_F1RevKmh = fisRevKmh;
        if (m_F1RevKmh < 1.0) m_F1RevKmh = 1.0;
        m_EtiqRunner = ruta;   // la misma identidad que usa la cabecera del AR
        if (m_Cinta) m_Cinta.SetEtiqueta(ruta);
        m_AR.Configurar(logEn, waitS, maxInt, coolS, fisEn, fisRevM, fisRevKmh, fisLadderM, fisSweepM, ruta, veh);
    }
    float ManualAdvanceM() { return m_ManAdvM; }
    // para que el heartbeat cuente los rescates de ESTE AR y no los teleports de v1.1.
    int ARLogicos()
    {
        return m_AR.NLogicos();
    }

    int ARFisicos()
    {
        return m_AR.NFisicos();
    }

    string ResumenAR()
    {
        return m_AR.ResumenFin();
    }
    void CerrarAR()
    {
        m_AR.Cerrar();
    }

    // config del AR manual (la entrega el servicio).
    // la calibracion de launch (BZ12TestDrive.FaseLaunch) tiene al vehiculo QUIETO CON GAS a proposito.
    // El AR leeria eso como atascado y lo "rescataria" (reubicacion, empuje). Mientras dura la fase, el AR no late:
    // su reloj se congela y retoma donde estaba. Solo el harness lo usa; la linea nunca lo toca.
    private bool m_ARSuspendido;
    void SetARSuspendido(bool on) { m_ARSuspendido = on; }

    void SetManualAR(float advM, float goodLatM)
    {
        m_ManAdvM = advM;
        if (m_ManAdvM < 1.0) m_ManAdvM = 1.0;
        m_ManGoodLatM = goodLatM;
        if (m_ManGoodLatM < 0.1) m_ManGoodLatM = 0.1;
        if (!s_Quiet) Print("[C12] ManualAR · AdvanceM " + m_ManAdvM.ToString() + " · GoodLatM " + m_ManGoodLatM.ToString() + " (bandera BZ12_AR_MANUAL.txt con el nombre de la ruta)");
    }

    // ★ 791 AR MANUAL, paso 1: DONDE. s_dest = ultimo bien pisado + AdvanceM, acotado al leg actual (2 m
    // antes de su cierre; si eso queda detras, en el lugar). La pose sale de la CINTA en s_dest. Rechaza con motivo:
    // sin cinta, o destino en un tramo de OTRO sentido que el leg.
    // TAMBIEN EN UN LEG DE REVERSA. El 105 nacio sobre algo (ruedas 0/4) en la salida en
    // reversa de Terminal Este y la escalera no tenia escalon: el 1 no vale sin ruedas, el 2 y el 3 se rechazaban "leg de
    // reversa, no aplicado" cada 0,5 s hasta agotar el barrido -> STUCK a los 22 s. La pose de la cinta es el yaw REAL del
    // chasis (tambien en reversa), el destino ya quedaba acotado al tramo de fase del leg (FinDeFaseEn - 2), y el servicio
    // deja la marcha del leg al re-ubicar. Lo que se rechaza es un destino cuyo tramo de cinta dice OTRO sentido que el leg.
    bool PlanReubicacion(BZVehicleState st, float sOrigen, float avanceM, out float sDestM, out vector posDestM, out float rumboDestM, out string detalleM)
    {
        detalleM = "";
        if (!m_Ready || !m_Leg || !m_Leg.valid) { detalleM = "sin leg valido"; return false; }
        if (!m_Cinta || !m_Cinta.Valida()) { detalleM = "la ruta corre sin CINTA: no hay pose grabada para re-ubicar"; return false; }
        float sActM = m_Cinta.SEst();
        float sBienM = sActM;
        string origenM = "sin punto bien pisado en este leg: desde el s actual " + sActM.ToString();
        if (m_ManBienOk)
        {
            sBienM = m_ManBienS;
            origenM = "desde ultimo bien pisado s " + sBienM.ToString();
        }
        if (sOrigen >= 0)
        {
            sBienM = sOrigen;
            origenM = "desde s " + sBienM.ToString();
        }
        float finLegM = m_Cinta.FinDeFaseEn(sActM);
        float sD = sBienM + avanceM;
        // 1: el "no ir atras" era GENERAL y pisaba toda re-ubicacion hacia atras (la batalla atras del
        // escalon 2 salia "+0,05 m" in-game mientras el banco daba 3,5 m). Solo vale para el cierre del leg.
        if (sD > finLegM - 2.0)
        {
            sD = finLegM - 2.0;
            if (sD < sActM) sD = sActM;   // a menos de 2 m del cierre: en el lugar
        }
        if (m_Cinta.EnReversaEnS(sD) != m_Leg.isReverse) { detalleM = "el destino (s " + sD.ToString() + ") cae en un tramo de OTRO sentido que el leg, no aplicado"; return false; }
        vector posL;
        float rumL;
        if (!m_Cinta.PoseEnS(sD, posL, rumL)) { detalleM = "la cinta no da pose en s " + sD.ToString(); return false; }
        sDestM = sD;
        posDestM = posL;
        rumboDestM = rumL;
        detalleM = origenM + " +" + (sD - sBienM).ToString() + " m -> s " + sD.ToString();
        return true;
    }

    // ★ AR MANUAL, paso 2 (G3, ): el servicio YA movio el vehiculo a posNueva. Toda re-ubicacion FUERZA el
    // arranque: cinta anclada en s_dest, latch en reposo visto (el LAUNCH arranca y la cinta re-arma al rodar),
    // cursor de ruta en el wp mas cercano (SeekTo: unica via para moverlo, y reinicia modulos/arbitro), C1, clavada
    // y rampa de arranque de cero. El registro lo lleva el AR (tipo MANUAL, resultado medido desde la pose nueva).
    void OnReubicado(vector posNueva, float sDest, string evid, string accion, string tipo)
    {
        if (!m_Ready || !m_Leg || !m_Leg.valid) return;
        m_F1Activo = false;
        m_ReubPend = false;
        if (m_Cinta) m_Cinta.AnclarEnS(sDest);
        int wpM = WpMasCercanoEnLeg(posNueva);
        SeekTo(wpM);
        m_CintaRodo = false;
        m_CintaQuieto = true;
        m_CintaReposoT = 0;
        m_CintaRearmaDist = -1;
        m_BogActivo = false;
        m_BogEpisodio = false;
        m_BogIntentos = 0;
        m_BogVentOk = false;
        m_SostenTope = 0;
        m_SostenAplicado = false;
        m_StallTicks = 0;
        m_StallReported = 0;
        m_ArranqueTicks = 0;
        m_ArranqueFase = ARR_RAMPA;
        m_ManBienOk = false;
        if (tipo == "MANUAL") m_AR.Manual(posNueva, wpM, sDest, evid, accion);
        else m_AR.AccionFisicaAplicada(posNueva, wpM, sDest, accion);
    }

    // el servicio pregunta en Tick12 si la escalera pide re-ubicar (offset sobre el ultimo bien pisado).
    bool PollReubicacionFisica(out float offR, out float sOrigenR)
    {
        offR = m_ReubPendOff;
        sOrigenR = m_ReubPendOrigen;
        if (!m_ReubPend) return false;
        m_ReubPend = false;
        return true;
    }

    // 2: el "bien pisado" de un choque queda pegado al impacto (PAZ: a 0,57 m). El origen del escalon 2
    // retrocede UNA BATALLA (del Manual; piso ReverseM) sin salirse de la racha continua en la que iba sobre la linea.
    private float BienPisadoAtras()
    {
        if (!m_ManBienOk || !m_Cinta) return -1;
        float retroB = m_F1RevM;
        if (m_Manual && m_Manual.wheelbaseM > retroB) retroB = m_Manual.wheelbaseM;
        float sB = m_ManBienS - retroB;
        if (sB < m_ManBienIni) sB = m_ManBienIni;
        return sB;
    }

    void ReubicacionRechazada(string motivo)
    {
        m_AR.SaltarEscalon(motivo);
    }

    // 3: el SpawnProbe de la re-ubicacion vio el vehiculo MONTADO (ruedas en el aire + chasis levantado):
    // el escalon falla ya y sube al siguiente, sin esperar los 15 s.
    void ReubicacionMontada(int abajo, int total, float alt)
    {
        m_AR.SaltarEscalon("montado sobre algo: " + abajo.ToString() + "/" + total.ToString() + " ruedas, origen-piso " + alt.ToString() + " m");
    }

    // F1/F3/G1: la ESCALERA FISICA, ultima palabra en Plan (despues de C1). Paso 1 = maniobra propia:
    // reversa RECTA ReverseM metros con la rampa de arranque (gear 0, volante al centro, la cinta suelta), un segundo de
    // freno, y se devuelve el mando con el latch en reposo: el LAUNCH re-encara el paso por la polilinea y la cinta
    // re-arma al rodar. Pasos 2+ = re-ubicar: lo pide al servicio (PollReubicacionFisica) que mueve el vehiculo con la
    // misma receta del AR manual y vuelve por OnReubicado (G3). Medido (PAZ, cartel en curva): choque a 50 km/h en s
    // 600,8, repta 5 m contra el cartel con el lateral 0,9 -> 1,84 m y queda quieto en s 606,95; el ultimo bien
    // pisado es s 602,3 = 5 m atras sobre la linea, encarando el paso (banco f1_paz_banco).
    private void AplicarAccionFisica(BZVehicleState st, float dt)
    {
        if (m_F1Activo)
        {
            if (!m_Leg || !m_Leg.valid || m_Mode == BZDriveMode.BZDM_ENDPOINT || m_Mode == BZDriveMode.BZDM_HALTED)
            {
                // la causa, no la categoria (como el corte del AHOGO en el 954).
                string causaF = "cortada: ";
                if (!m_Leg || !m_Leg.valid) causaF = causaF + "leg invalido";
                else if (m_Mode == BZDriveMode.BZDM_ENDPOINT) causaF = causaF + "modo ENDPOINT";
                else causaF = causaF + "modo HALTED (Abort del consumidor, fin de ruta o SafeStop del arbitro)";
                TerminarManiobraF1(st, causaF);
                return;
            }
            m_F1T = m_F1T + dt;
            float dxF = st.pos[0] - m_F1Pos0[0];
            float dzF = st.pos[2] - m_F1Pos0[2];
            float dRevF = Math.Sqrt(dxF * dxF + dzF * dzF);
            float vRevF = -st.fwdKmh;   // > 0 alejandose: hacia atras en un leg normal...
            if (m_F1Adelante) vRevF = st.fwdKmh;   // y hacia ADELANTE en un leg de reversa
            if (vRevF > m_F1PicoKmh) m_F1PicoKmh = vRevF;
            if (m_F1Fase == 0)
            {
                // 3: corte con MARGEN DE FRENADO (v^2/2a con el freno del Manual) y tope de velocidad.
                // Medido sin esto: 3 m pedidos, 4,89 m recorridos, pico 13,5 km/h hacia atras.
                float aFreF = m_Manual.MaxBrakeDecel(m_SurfKind);
                if (aFreF < 1.0) aFreF = 3.0;
                float vRevMs = vRevF / 3.6;
                if (vRevMs < 0) vRevMs = 0;
                float margenF = (vRevMs * vRevMs) / (2.0 * aFreF);
                if (dRevF + margenF >= m_F1RevM || m_F1T > F1_REV_TOPE_S)
                {
                    m_F1Fase = 1;
                    m_F1T = 0;
                }
                else
                {
                    float pisoF = m_Manual.launchThrottleFloor;
                    if (pisoF < 0.01) pisoF = BZVehicleManual.LAUNCH_PRIOR_FLOTA;   // una sola fuente
                    // la rampa solo sube si NO se mueve (dos ticks quieto); en cuanto retrocede, el piso alcanza
                    if (vRevF < 0.5) m_F1Quieto = m_F1Quieto + 1;
                    else m_F1Quieto = 0;
                    if (m_F1Quieto >= 2) m_F1Rampa = m_F1Rampa + 1;
                    float thrF = pisoF + m_F1Rampa * ARRANQUE_STEP;
                    if (thrF > 1.0) thrF = 1.0;
                    if (vRevF >= m_F1RevKmh) thrF = 0;
                    m_Out.gear = 0;
                    if (m_F1Adelante) m_Out.gear = AHOGO_MARCHA;   // en un leg de reversa la maniobra es en PRIMERA
                    m_Out.throttle = thrF;
                    m_Out.brake = 0;
                    m_Out.steering = 0;
                    m_UsoPursuit = false;
                    return;
                }
            }
            m_Out.gear = AHOGO_MARCHA;
            if (m_F1Adelante) m_Out.gear = 0;   // frena en la marcha del leg (reversa), asi el LAUNCH re-encara sin cambiar
            m_Out.throttle = 0;
            m_Out.brake = 1.0;
            m_Out.steering = 0;
            m_UsoPursuit = false;
            string haciaFin = "retrocedio ";
            if (m_F1Adelante) haciaFin = "avanzo (leg de reversa) ";
            if (m_F1T >= F1_FRENO_S) TerminarManiobraF1(st, haciaFin + dRevF.ToString() + " m (pico " + m_F1PicoKmh.ToString() + " km/h) · se devuelve el mando (LAUNCH re-encara, gracia " + F1_GRACIA_S.ToString() + " s)");
            return;
        }
        if (m_F1GraciaT > 0)
        {
            // la gracia termina antes si el LAUNCH ya re-encaro con avance real
            m_F1GraciaT = m_F1GraciaT - dt;
            float dxG = st.pos[0] - m_F1PosFin[0];
            float dzG = st.pos[2] - m_F1PosFin[2];
            float vTravG = st.fwdKmh;
            if (m_Leg && m_Leg.valid && m_Leg.isReverse) vTravG = -st.fwdKmh;   // avance real = en el sentido del leg
            if (dxG * dxG + dzG * dzG >= 1.0 && vTravG >= CINTA_RODANDO_KMH) m_F1GraciaT = 0;
        }
        float offF = 0;
        float sOrF = -1;
        int pasoF = m_AR.TomarPasoFisico(offF, sOrF);
        if (pasoF == 1)
        {
            // 3: con OTRO vehiculo atras no se reversa (convoy): al escalon 2 con log
            // la referencia es el PARAGOLPES (mitad del largo de la caja), no el centro: un auto a 2 m
            // del paragolpes de un bus de 9 m queda a 6,5 m del centro. Dos puntos: pegado atras y al final de la reversa.
            // la maniobra se ALEJA del leg: hacia atras (-yaw) en un leg normal, hacia ADELANTE (+yaw) en uno de
            // reversa; el chequeo de vehiculo ajeno mira en ese mismo sentido.
            float senF = -1.0;
            if (m_Leg.isReverse) senF = 1.0;
            float fxF = Math.Sin(st.yaw) * senF;
            float fzF = Math.Cos(st.yaw) * senF;
            float mitadF = st.lenM * 0.5;
            if (mitadF < 1.5) mitadF = m_F1RevM;
            float atras1F = mitadF + 1.0;
            float atras2F = mitadF + 1.0 + m_F1RevM;
            vector posAtras1F = Vector(st.pos[0] + fxF * atras1F, st.pos[1], st.pos[2] + fzF * atras1F);
            vector posAtras2F = Vector(st.pos[0] + fxF * atras2F, st.pos[1], st.pos[2] + fzF * atras2F);
            // CUALQUIER obstaculo con colision, no solo un vehiculo. En Sosnovka la F1 reverso
            // 3 m contra la garita (BZ_ParadaBrutalist a 1,84 m detras): el sondeo solo preguntaba por vehiculos.
            string obsF = "";
            if (m_Probe) obsF = m_Probe.ObstaculoEn(posAtras1F, F1_ATRAS_RADIO_M);
            if (m_Probe && obsF == "") obsF = m_Probe.ObstaculoEn(posAtras2F, F1_ATRAS_RADIO_M);
            if (obsF != "")
            {
                string ladoF = "atras";
                if (m_Leg.isReverse) ladoF = "adelante (leg de reversa)";
                m_AR.SaltarEscalon("obstaculo " + obsF + " hasta " + atras2F.ToString() + " m " + ladoF + " del centro (paragolpes a " + mitadF.ToString() + "): sin maniobra");
                return;
            }
            IniciarManiobraF1(st);
            return;
        }
        if (pasoF == 2)
        {
            m_ReubPend = true;
            m_ReubPendOff = offF;
            m_ReubPendOrigen = sOrF;
            if (sOrF < 0) m_ReubPendOrigen = BienPisadoAtras();
        }
    }

    private void IniciarManiobraF1(BZVehicleState st)
    {
        m_F1Activo = true;
        m_F1Fase = 0;
        m_F1T = 0;
        m_F1Pos0 = st.pos;
        m_F1Rampa = 0;
        m_F1Quieto = 0;
        m_F1PicoKmh = 0;
        m_F1GraciaT = 0;
        m_StallTicks = 0;      // 1: la clavada acumulada ANTES de la maniobra no la juzga
        m_StallReported = 0;
        if (m_CintaRodo)
        {
            m_CintaRodo = false;
            m_CintaQuieto = true;
            m_CintaRearmaDist = -1;
        }
        float sF1 = -1;
        if (m_Cinta) sF1 = m_Cinta.SEst();
        int wpF1 = -1;
        if (m_Leg && m_Leg.valid) wpF1 = m_Leg.nearWp;
        m_F1Adelante = false;   // en un leg de reversa la salida es hacia ADELANTE (alejarse del leg)
        if (m_Leg && m_Leg.valid && m_Leg.isReverse) m_F1Adelante = true;
        string haciaF1 = "reversa";
        if (m_F1Adelante) haciaF1 = "adelante (leg de reversa)";
        m_AR.AccionFisicaAplicada(st.pos, wpF1, sF1, "maniobra: " + haciaF1 + " " + m_F1RevM.ToString() + " m y re-encarar");
        if (!s_Quiet) Print("[C12] F1 MANIOBRA: " + haciaF1 + " recta " + m_F1RevM.ToString() + " m desde " + st.pos.ToString() + " (cinta suelta, volante al centro)" + SufijoRunner());
    }

    private void TerminarManiobraF1(BZVehicleState st, string resultado)
    {
        m_F1Activo = false;
        m_F1GraciaT = F1_GRACIA_S;
        m_F1PosFin = st.pos;
        m_StallTicks = 0;
        m_StallReported = 0;
        m_ArranqueTicks = 0;
        m_ArranqueFase = ARR_RAMPA;
        m_CintaRodo = false;
        m_CintaQuieto = true;
        m_CintaRearmaDist = -1;
        m_Derivation.Reset();
        if (!s_Quiet) Print("[C12] F1 MANIOBRA · " + resultado + SufijoRunner());
    }

    private int WpMasCercanoEnLeg(vector p)
    {
        int mejorW = m_Leg.legLo;
        float dMinW = 1000000000.0;
        int iW;
        vector qW;
        float dxW;
        float dzW;
        float d2W;
        for (iW = m_Leg.legLo; iW <= m_Leg.legHi; iW++)
        {
            qW = m_Route.Pos(iW);
            dxW = p[0] - qW[0];
            dzW = p[2] - qW[2];
            d2W = dxW * dxW + dzW * dzW;
            if (d2W < dMinW)
            {
                dMinW = d2W;
                mejorW = iW;
            }
        }
        return mejorW;
    }

    // C1: config del ahogo (la entrega el servicio).
    void SetBogRecovery(bool en, float windowS, float minProgM, int maxInt)
    {
        m_BogEnabled = en;
        m_BogWindowS = windowS;
        if (m_BogWindowS < 1.0) m_BogWindowS = 1.0;
        m_BogMinProgM = minProgM;
        if (m_BogMinProgM < 0.1) m_BogMinProgM = 0.1;
        m_BogMaxInt = maxInt;
        string bc = "[C12] BogRecovery · Enabled " + m_BogEnabled.ToString() + " · WindowS " + m_BogWindowS.ToString();
        bc = bc + " · MinProgressM " + m_BogMinProgM.ToString() + " · MaxAttempts " + m_BogMaxInt.ToString();
        if (!m_BogEnabled) bc = bc + " (el detector de AHOGO registra igual; la accion esta apagada)";
        Print(bc);
    }
    bool  ClosedByGiveUp()      { return m_Maneuver.GaveUp() || m_Slam.GaveUp(); }
    // A fondo, en subida y sin avanzar: el vehiculo no corona la cuesta desde donde esta.
    // No es un evento -- la ruta puede seguir -- pero SI tiene que ser visible.
    bool  ClimbStalled()        { return m_StallTicks > STALL_TICKS; }
    // Read-only, para que el shell pueda reportar sin duplicar la lectura del vehiculo.
    BZVehicleManual GetManual() { return m_Manual; }
    int   GetLegIdx()           { return m_Leg.legIdx; }
    int   GetManeuverPhase()    { return m_Maneuver.GetPhase(); }
    int GetLegCount()
    {
        if (!m_Route) return 0;
        return m_Route.LegCount();
    }
    int   GetLastClosedLeg()    { return m_LastClosedLeg; }
    float GetLastCloseAlongM()  { return m_LastCloseAlongM; }
    float GetLastCloseLatM()    { return m_LastCloseLatM; }

    // ======================= EVENTOS (cara 4) =======================

    int PollEvent(out int wpIdx)
    {
        wpIdx = m_EventWp;
        int t = m_EventType;
        m_EventType = BZDriveEv.NONE;
        return t;
    }

    private void Emit(int type, int wp)
    {
        m_EventType = type;
        m_EventWp = wp;
    }

    // ======================= PLAN (2 Hz) =======================

    BZDriveOutput Plan(BZVehicleState st, float dt)
    {
        if (!m_Ready)
        {
            m_Out.SetHalt();
            return m_Out;
        }
        if (m_FinPendiente)
        {
            m_Out.SetHalt();   // llegada hecha, veredicto pendiente del reposo: nada que decidir ni avanzar
            return m_Out;
        }
        if (!m_FrenoTomaAcoplado) AcoplarFrenoToma();
        m_UltimoPos = st.pos;
        m_UltimoKmh = st.chassisKmh;
        m_LegMachine.Update(st, m_Leg);
        ActualizarStopServido();   // antes de ComputeGoal, que es quien lee la marca
        ReportarArranqueDeLeg(st);
        AcumularSeguimiento(st);
        m_SurfKind = BZSurf.HARD;
        if (m_Probe) m_SurfKind = m_Probe.SurfaceKindAt(st.pos);
        m_Mode = m_Arbiter.Decide(st, m_Leg, m_Manual, m_SurfKind);
        // Si el arbitro corto por seguridad, el evento tiene que SALIR. En el run 2 no
        // habia forma de enterarse: no existia quien emitiera STUCK.
        if (m_Arbiter.GetFallback() == BZArbFallback.SAFE_STOP)
        {
            // 4 (#4): con la escalera fisica en curso el AR nunca corta la toma (G1). El atasco del
            // arbitro (20 s sin avance del cursor) es un FISICO mas: abre el escalon siguiente y el arbitro se
            // reinicia (la re-ubicacion lo reinicia otra vez via SeekTo). Medido: STUCK en wp 381 con +4..+6 sin usar.
            bool fisEnCursoSS = m_AR.FisicoEnCurso();
            bool puedeAbrirSS = (!fisEnCursoSS && m_AR.FisicoHabilitado() && !m_AR.FisicoAgotado());
            if (m_F1Activo || m_ReubPend)
            {
                // UN ESCALON EN CURSO NO ES STUCK. Las dos ramas de abajo excluian la F1 activa y la
                // re-ubicacion pendiente (para no pisar la maniobra) y el `else` las mandaba a STUCK: el runner cerraba la ruta
                // con "escalera del AR agotada o desactivada", falso. Medido: server B 23/09 22:42:22 (escalera 2, escalon 1,
                // la F1 retrocediendo; el cursor llevaba 20 s en wp 815) y server A 21/09 18:43:34 (#103, misma firma: la F1
                // "cortada" 1 ms antes del FIN). El arbitro se reinicia y se vuelve a decidir el modo EN ESTE TICK: si quedara
                // el HALTED del SafeStop, AplicarAccionFisica cortaria la F1 igual (es lo que paso). La escalera sigue.
                m_Arbiter.Reset();
                m_Mode = m_Arbiter.Decide(st, m_Leg, m_Manual, m_SurfKind);
                if (!s_Quiet) Print("[C12] SAFE_STOP del arbitro con un escalon del AR en curso (F1 activa o re-ubicacion pendiente): el arbitro se reinicia y la escalera sigue" + SufijoRunner());
            }
            else if (fisEnCursoSS)
            {
                ClasificarFisico(st, "SAFE_STOP del arbitro (" + m_Arbiter.GetFallbackPorQue() + ")");
                m_Arbiter.Reset();
            }
            else if (puedeAbrirSS)
            {
                // hasta hoy, sin escalera abierta esto era `Emit(STUCK)` directo -- "12-mode aun sin
                // recovery: declarado". MEDIDO (BusDrive 23/09 14:30:08, coche 103): 22 s en reversa contra otro bus, AR 0,
                // STUCK sin un solo intento. El alcance del framework es que el vehiculo no quede detenido para siempre:
                // declarar sin intentar es exactamente lo que no puede pasar. Ahora el SAFE_STOP ABRE la escalera (la misma
                // que abre el CLAVADO; al abrirse avisa BLOQUEADO) y STUCK queda para cuando esta agotada o desactivada.
                ClasificarFisico(st, "SAFE_STOP del arbitro (" + m_Arbiter.GetFallbackPorQue() + "): abre la escalera");
                m_Arbiter.Reset();
            }
            else
            {
                // STUCK solo llega aca con la escalera AGOTADA o DESACTIVADA; el motivo lo dice el controlador.
                if (!m_AR.FisicoHabilitado()) m_MotivoStuck = "escalera del AR desactivada (Physical.Enabled false)";
                else if (m_AR.FisicoAgotado()) m_MotivoStuck = "escalera del AR agotada";
                else m_MotivoStuck = "sin escalera que abrir (ni en curso ni abrible: revisar)";
                Emit(BZDriveEv.STUCK, m_Leg.nearWp);
            }
        }

        m_UsoPursuit = false;
        if (m_Mode == BZDriveMode.BZDM_MANEUVER)
        {
            m_Maneuver.Actuate(st, m_Leg, m_Manual, m_SurfKind, m_Out);
            if (m_Maneuver.IsLegDone())
            {
                if (m_CierrePorQue == "") m_CierrePorQue = "la maniobra declaro el leg terminado";
                OnLegDone();
            }
            else if (CintaClavoElCusp(st))
            {
                m_CierrePorQue = "la cinta clavo el cusp: detenida en su freno grabado al final del leg";
                OnLegDone();
            }
            else if (FrenoFinLegClavo(st))
            {
                m_CierrePorQue = "el freno al fin del leg clavo el cusp: la toma se agoto o iba a paso de arrastre con el casco rodando";
                OnLegDone();
            }
        }
        else if (m_Mode == BZDriveMode.BZDM_ENDPOINT)
        {
            m_Slam.Actuate(st, m_Leg, m_Manual, m_SurfKind, m_Out);
            ReportarSlam(st, dt);
            if (m_Slam.IsLatched())
            {
                if (m_CierrePorQue == "") m_CierrePorQue = "el endpoint clavo la pose (slam latcheado)";
                OnArrived();
            }
        }
        else if (m_Mode == BZDriveMode.BZDM_HALTED)
        {
            m_Out.SetHalt();
        }
        else
        {
            m_Pursuit.ComputeGoal(st, m_Leg, m_Manual, m_SurfKind, m_Goal);
            m_Derivation.SetCongelarCorreccion(CongelarCorreccion(st));
            m_Derivation.ToInputs(m_Goal, st, m_Manual, m_SurfKind, m_Out);
            m_UsoPursuit = true;
        }
        m_Out.steering = AcotarVolante(m_Out.steering, st);
        m_Out.steering = Math.Clamp(m_Out.steering, -SteerAuthority(st), SteerAuthority(st));
        // ⚠ SE LIMPIA ANTES DE LLAMAR: si quedara el valor del tick anterior, el log diria
        // que el feedforward actuo en ticks en los que no hizo nada.
        m_ThrFFAplicado = 0;
        AplicarPedalGrabado(st);
        m_Out.throttle = Math.Clamp(m_Out.throttle, 0.0, TractionCap(st));
        // ★★ LO QUE EL MODULO PIDE, ANTES DE QUE LO TOQUEMOS NOSOTROS.
        // El detector de clavada tiene que observar la DEMANDA, no la actuacion propia. Ya
        // paso dos veces: el cap de gas y despues el rearme dejaron el gas por debajo de "a
        // fondo" y el detector dejo de disparar -- `CLAVADO: no hubo · razon: gas 0.21`.
        // Un instrumento que mira la salida de su propio control mide el control, no el
        // mundo. Es 8.2.1 aplicado al observador, y se arregla de raiz una sola vez.
        m_ThrottlePedido = m_Out.throttle;
        // ⚠ LA ACELERACION OBSERVADA ES DEL INTERVALO ANTERIOR, y por lo tanto corresponde
        // al gas que se aplico ENTONCES, no al que se esta por comandar. Sin decirlo, el log
        // invita a concluir "apliqué 0.62 y no acelero" cuando el 0.62 todavia no habia
        // actuado. Se guarda el gas de ese intervalo y se reporta al lado.
        m_AObs = 0;
        if (dt > 0.01) m_AObs = ((st.chassisKmh - m_KmhPrevPlan) / 3.6) / dt;
        m_KmhPrevPlan = st.chassisKmh;
        m_ThrAplicadoPrev = m_ThrAplicado;
        NoSoltarHaciaElBog(st);
        RampaDeArranque(st);
        DetectarAhogo(st, dt);   // C1: antes que la clavada, para que el FISICO no se lleve un ahogo
        DetectarClavada(st);
        // EL DIAGNOSTICO VA ANTES DEL ABANDONO. Los dos usan el mismo umbral (10 s) a
        // proposito, y en ese empate el abandono ganaba: cerraba la ruta y el runner retornaba por
        // m_C12Terminada ANTES de que el diagnostico publicara. Resultado: el caso que termina en
        // abandono -- justo el que mas nos interesa -- nunca dejaba su foto de guardas. Invertido, el
        // diagnostico publica en el MISMO tick en que el abandono va a disparar.
        DiagnosticarSinRescate(st, dt);   // por que NO lo rescato nadie (solo imprime)
        DetectarTrabaPorProgreso(st, dt);   // y si nadie lo rescata, se abre la escalera
        AbandonarSiEsImposible();
        ReportarDireccion(st, dt);
        NoPlanearHastaElBog(st);
        DetectarDeslizamiento(st);
        DetectarSubvelocidad(st);
        DetectarPlaneo(st, dt);
        AplicarRecuperacionAhogo(st, dt);   // C1: ULTIMA palabra sobre gas/freno/marcha en Plan
        AplicarAccionFisica(st, dt);        // F1/F3/G1: la escalera fisica, despues de C1
        SoltarMesetaSiSeAlejo(st);   // engancha por cercania, se suelta por lejania
        SostenerMeseta(st);   // una detencion IMPUESTA se sostiene, no se espera
        m_Out.handbrake = HandbrakeFiel();
        m_ThrAplicado = m_Out.throttle;
        // (BusDrive 9.3): el PRIMER GAS tras el hold se mide en Sample (40 Hz, 25 ms), no aca: con el reloj del
        // Plan 275 de 297 holds daban 0,5 s y el falsador pre-registrado (mediana < 0,2 s) no se podia testear.
        DetectarAperturaDeEscalera(st);   // el aviso BLOQUEADO sale de la TRANSICION, no de quien abre
        m_Out.mode = m_Mode;
        m_TargetSteer = m_Out.steering;
        CacheEndPose();
        // la frenada del cusp de este leg, con la pose final recien cacheada (el leg cambia en Sample; la pose se refresca aca)
        if (m_Leg && m_Leg.valid && m_Leg.legIdx != m_FrenoCuspLeg && m_Manual) AcoplarFrenoCusp();
        // la foto del remanente longitudinal QUE ACOMPAÑA a este `m_Leg`.
        // Va DESPUES de CacheEndPose para que mida contra la pose final del leg vigente
        // (si el leg cambio en este tick, la pose vieja daria un delta sin sentido).
        m_RemFrAtPlan = RemanenteFrameM(st);
        return m_Out;
    }

    // ★ AUTORIDAD DE VOLANTE A BAJA VELOCIDAD -- REGLA TRANSVERSAL, UNA SOLA LINEA.
    //
    // La regla es FISICA, no de ningun modulo: por debajo del creep el vehiculo no rueda,
    // el angulo de direccion no lo hace girar, y las ruedas cruzadas solo agregan
    // resistencia de arrastre y lo desalinean.
    //
    // MEDIDO EN TRES MODOS DISTINTOS (runs 7 y 8): MANIOBRA (arando en el despegue),
    // CRUISE (leg 0, str->1.0 con el auto gateando) y ENDPOINT (tras tocar a 18 cm,
    // str -1 derivo el lateral de 0.18 a 5.3 m sin llegar a clavar). Tres modulos, un
    // solo hecho fisico -> va UNA VEZ acá, no tres veces adentro de cada uno. Mismo
    // criterio que el handbrake fiel: reglas de contrato, en el orquestador.
    //
    // DESVANECIDO, NO ESCALON: la autoridad sube con la velocidad. Un corte duro en el
    // umbral seria el mismo bug que la banda muerta de la derivacion -- un centimetro por
    // segundo de un lado das 0.35 y del otro 1.0.
    // ★ NO SE PUEDE ACELERAR LO QUE NO TOCA EL PISO.
    //
    // Regla transversal, como la autoridad de volante: vive UNA vez aca y no replicada en
    // cada modulo. Medido tras el spawn (SEDAN_SHORT, STUCK en wp 0): `ruedas 0/4` con
    // `thr 1` y `rpm 6727` -- las ruedas girando a fondo EN EL AIRE. Gas pleno sin contacto
    // no acelera: patina, y en el aterrizaje la rueda que llega girando a 6700 rpm rebota
    // el vehiculo otra vez. Es un lazo que se alimenta solo.
    //
    // El cap NO es cero: con contacto parcial algo de tiro sirve (salir de un pozo, cruzar
    // un badén). Es "lo que la traccion disponible puede poner en el piso", en proporcion a
    // las ruedas que efectivamente tocan.
    // ★★★ ROMPER LA INERCIA ES CONOCIMIENTO DEL VEHICULO, NO DE UN MODO.
    //
    // La asimetria que lo destapo: desde el spawn el auto arranca perfecto y sube a 32 km/h,
    // pero cuando se arrastra a ~0.3 km/h a mitad de leg se boga y no se recupera. La
    // diferencia no es el powertrain: es QUIEN pone el gas. Desde el spawn manda la fase
    // LAUNCH del MC, que RAMPEA desde el piso medido (0.21) y sube; en CRUISE la derivacion
    // pide 1.0 de una y el diesel se clava a ~1138 rpm, abajo de la mordida (1170).
    //
    // ✖ Y NO ES "MENOS GAS": eso quedo FALSADO. Un tope constante en 0.42 desde el bog bajo
    // el rpm a 1051 y freno mas todavia. Lo que funciona no es un valor, es la RAMPA --
    // empezar bajo y subir deja que el motor construya vueltas antes de cargarlo.
    //
    // Asi que se aplica igual que autoridad de volante y tope de traccion: en la salida,
    // para cualquier modo. Solo ACOTA -- si el modulo pide menos que la rampa, manda el
    // modulo; la rampa nunca pide mas de lo que el modulo pidio.
    // El reptar MEDIDO del vehiculo es su propia definicion de "quieto". Vive en un solo
    // lugar para que la rampa de arranque y la metrica de seguimiento usen la misma.
    // (A2): LEE LA MEDICION, NO EL OBJETIVO. `creepKmh` es el objetivo de arrastre de los
    // modulos POSE, y en un vehiculo que NO repta el reader le mete ahi el arrastre derivado del freno
    // (3.6*sqrt(2*aFreno*CRAWL_STOP_M) = 5,52 km/h en el ChernoBus). Leerlo como "quieto" armaba la rampa
    // con el bus RODANDO (1 de los 4 ARRANQUE de la corrida 11:21 a 1,31 km/h; en la tanda 13:20, 133 de
    // 456) y le recortaba el gas a 0,35: Svergino. Lo que el vehiculo hace SOLO es `idleCreepKmh`: 0 en
    // 43 de 43 envelopes con coast2 => el piso de 0,5 km/h, que es lo que este comentario siempre pidio.
    private float UmbralCasiParado()
    {
        float u = 0.0;
        if (m_Manual) u = m_Manual.idleCreepKmh;
        if (u < 0.5) u = 0.5;
        return u;
    }

    private void RampaDeArranque(BZVehicleState st)
    {
        m_ArranqueActivo = false;
        if (!m_Manual) return;
        float umbral = UmbralCasiParado();
        bool casiParado = (st.chassisKmh < umbral);
        // LA RAMPA ES PARA ARRANCAR, NO PARA SOSTENER UNA SUBIDA.
        // MEDIDO en la cuesta de Svergino (tres pasadas identicas, 2 de 3 abandonan): el gas se desploma de
        // 1,00 a 0,35 y vuelve en escalones de 0,10 -- que son exactamente launchThrottleFloor y
        // ARRANQUE_STEP. Se arma a 6,65-6,99 km/h porque `UmbralCasiParado` devuelve el CREEP, y el
        // ChernoBus no lo tiene medido: usa el prior de caja, 8,62 km/h. O sea que para ese bus "casi
        // parado" son 8,6 km/h, cuando el comentario de esta misma funcion dice que el caso que la motivo
        // era "se arrastra a ~0.3 km/h".
        // Y en una cuesta el recorte es justo lo contrario de lo que hace falta: la rampa sirve para que el
        // diesel CONSTRUYA VUELTAS desde quieto, pero un vehiculo que venia a 55 km/h y lo frena la pendiente
        // ya tiene vueltas -- lo que le falta es PAR. Con gas 0,35 a 4,2 grados, pierde la cuesta.
        // ⚠ LA GUARDA NO INVENTA UN UMBRAL: usa la aceleracion observada que el Plan calcula dos lineas
        // antes de llamar aca. ARRANCAR ES ACELERAR; caerse no. Si el vehiculo viene perdiendo velocidad,
        // la rampa se desarma y el modulo recupera su gas.
        // EL ARRANQUE EN PENDIENTE. Un vehiculo PARADO en subida que suelta el freno
        // retrocede unos centimetros antes de agarrar, y en esos ticks `m_AObs < 0` es cierto con el
        // vehiculo a punto de necesitar la rampa. Sin esta segunda mitad, la guarda le sacaria el piso
        // de gas justo ahi (es el caso del `primera + hillstart` del wp 450).
        // La distincion no necesita un numero nuevo: un vehiculo POR DEBAJO del umbral de quieto del
        // sistema esta parado de verdad, y su retroceso son centimetros; el que pierde una cuesta lo
        // hace a 5-9 km/h. Asi que la guarda solo mira hacia arriba de LOG_COLA_QUIETO_KMH.
        bool cayendo = (m_AObs < 0 && Math.AbsFloat(st.chassisKmh) > LOG_COLA_QUIETO_KMH);
        // durante una meseta grabada no hay despegue que rampear: la cinta frena a proposito. Sin esto la rampa se
        // armaba en cada parada grabada ("la rampa agoto sus 8 pasos sin despegar a 0,004 km/h") y ensuciaba el diagnostico.
        if (!casiParado || cayendo || m_Out.throttle <= 0.01 || m_Out.brake > 0.01 || CintaEnMeseta())
        {
            m_ArranqueTicks = 0;
            m_ArranqueFase = ARR_RAMPA;
            return;
        }
        // --- AGOTADA: se devuelve el mando. No hay recuperacion que aplicar. ---
        if (m_ArranqueFase == ARR_SUELTO) return;

        float piso = m_Manual.launchThrottleFloor;
        if (piso < 0.01) piso = BZVehicleManual.LAUNCH_PRIOR_FLOTA;   // una sola fuente
        int pasos = (int)((1.0 - piso) / ARRANQUE_STEP);
        if (pasos < 1) pasos = 1;
        if (m_ArranqueTicks >= pasos)
        {
            // ✖ NO HAY RECUPERACION QUE APLICAR: las CINCO vias estan falsadas por medicion.
            //   soltar el gas (ROCK)      rpm 1077.9 -> 1164.35   no sube
            //   gas constante 0.42        rpm 1138 -> 1051        baja
            //   rampa ciclica x7          rpm clavado en 1117.36  no responde
            //   parada completa           0.006 km/h ... en gear 0, nunca desacoplo
            //   neutral + enganche        rpm 1424 -> COLAPSA a 1047 al enganchar
            // La ultima es la concluyente: DayZ SI deja desacoplar (el rpm trepa libre en
            // neutral, 9 de 9 veces) pero al enganchar hace lock instantaneo -- fuerza
            // rpm = w_rueda * relacion -- y las vueltas acumuladas se PIERDEN. No hay
            // embrague que patine, asi que el motor no puede hacer trabajo sobre ruedas
            // quietas. El bog es un limite duro del motor, no un bug de control.
            // Se devuelve el mando para que el detector de clavada vea la demanda real.
            m_ArranqueFase = ARR_SUELTO;
            // ESTE MECANISMO NO PUEDE DICTAMINAR SOBRE EL VEHICULO. Hasta hoy la linea decia
            // "no hay recuperacion posible (5 vias falsadas)", y eso es un veredicto sobre el VEHICULO que la
            // rampa no esta en condiciones de dar. MEDIDO, y es falso: 2,6 s antes de una de esas lineas,
            // `AHOGO primera + hillstart` habia sacado al mismo bus de casi-cero a 12,66 km/h en 4 s, subiendo
            // la misma cuesta. La rampa sabe UNA cosa -- que su rampa no despego -- y eso es lo que dice.
            // Las 5 vias siguen falsadas para el BOG REAL (r32, agosto, con el motor recibiendo todo lo pedido);
            // lo que no vale es aplicarle ese veredicto a cualquier vehiculo que no despegue en 6 pasos.
            if (!s_Quiet)
            {
                string arAg = "[BZDrive] ARRANQUE: la rampa agoto sus " + m_ArranqueTicks.ToString() + " pasos sin despegar";
                arAg = arAg + " a " + st.chassisKmh.ToString() + " km/h · devuelve el mando";
                arAg = arAg + " · NO dice que el vehiculo sea irrecuperable: C1 y el AR siguen";
                arAg = arAg + SufijoRunner();
                Print(arAg);
            }
            return;
        }
        float rampa = piso + m_ArranqueTicks * ARRANQUE_STEP;
        m_ArranqueTicks++;
        if (rampa >= m_Out.throttle) return;      // el modulo ya pide menos: manda el modulo
        m_ArranqueActivo = true;
        // la linea decia "piso medido" aunque fuera el fallback. Dice de donde sale.
        string ppiso = " (prior de flota)";
        if (m_Manual.launchFloorMedido) ppiso = " (medido)";
        if (m_Manual.launchFloorForzado) ppiso = " (FORZADO por banco)";
        if (m_ArranqueTicks == 1 && !s_Quiet) Print("[BZDrive] ARRANQUE: " + st.chassisKmh.ToString() + " kmh · el modulo pedia " + m_Out.throttle.ToString() + " · rampa desde el piso " + piso.ToString() + ppiso + SufijoRunner());
        m_Out.throttle = rampa;
    }

    // ★★★ NO SE SUELTA EL GAS HACIA EL BOG.
    //
    // Con el planeo bien medido (antes estaba 4x subestimado por no compensar pendiente),
    // el controlador por fin puede saber que le cuesta soltar el gas. Y le cuesta caro: en
    // wp 173 soltaba a 0 con el vehiculo a 10.8 km/h porque estaba en target, esperando
    // asentarse suave -- y el planeo real lo bajaba a 0.13 km/h en 5 segundos, adentro del
    // bog, de donde ya sabemos que no se vuelve (5 vias de recuperacion falsadas).
    //
    // La regla: si soltando el gas el vehiculo cruzaria el umbral de bog dentro del
    // horizonte, y la ruta grabada todavia quiere velocidad ahi, se sostiene el gas minimo
    // que cancela el planeo. No acelera: EVITA CAER. Por eso el techo es el gas que el
    // modelo dice que hace falta para no perder velocidad, ni un poco mas.
    //
    // ⚠ Esto NO es un piso de gas elegido a ojo -- eso habria enmascarado el error del
    // modelo y lo habria dejado vivo en todos lados. Es el modelo corregido, consultado
    // antes de decidir. El orden importa: primero medir bien, despues accionar.
    // ⚠⚠⚠ EL PISO DE LA MORDIDA VALE SOBRE EL FILO, NO CUANDO EL HUMANO MANEJO ABAJO.
    //
    // Se escribio para un caso medido y real, el ZiS5: grabado 10.15-10.71 km/h con la
    // mordida en 10.20. Ahi el humano manejo JUSTO sobre la linea, sosteniendose con
    // embrague, y apuntar al grabado era apuntar al filo. El piso es correcto AHI.
    //
    // En el Hatchback_02 el config declara reversa 3.167 x final 3.667 = 11.6134, o sea
    // mordida 15.83 km/h -- y lo grabado es 5.8-8.2. No es el filo: son 2.7 veces. El piso
    // levantaba el objetivo a 15.83 x 1.10 = 17.4 y el vehiculo lo perseguia hasta 17.
    //
    // ★ Y HAY PRUEBA DE QUE SE PUEDE ANDAR POR DEBAJO: en la corrida de las 16:56 el
    // vehiculo sostuvo mediana 10.0 km/h en reversa una corrida entera SIN plantarse --
    // por debajo de la mordida (15.83) y por debajo incluso del ralenti acoplado (12.17).
    // La simulacion patina el embrague sola. El piso estaba defendiendo contra un bog que
    // el motor no iba a tener, al precio de 2.7x la velocidad grabada.
    //
    // ✖ El umbral sale del MISMO BITE_MARGEN que ya usaba el piso: si lo grabado no llega
    // ni al borde inferior de la banda que el piso quiere defender, es que el humano NO
    // estaba en esa banda. No entra una constante nueva.
    static bool PisoDeMordidaAplica(float recKmh, float biteKmh)
    {
        if (biteKmh <= 0.1) return false;
        if (recKmh < biteKmh / BITE_MARGEN) return false;
        return true;
    }

    private void NoSoltarHaciaElBog(BZVehicleState st)
    {
        m_FeatherActivo = false;
        if (!m_Manual || !m_Leg || !m_Leg.valid) return;
        if (m_Out.brake > 0.01) return;                 // frenar es una decision, se respeta
        float vBog = m_Manual.creepKmh * FEATHER_MARGEN;
        if (vBog < 1.0) vBog = 1.0;
        float rec = m_Leg.MinRecSpeedAhead(VentanaSubvelM());
        if (rec < vBog) return;                         // la ruta QUIERE frenar aca: se respeta
        float aFree = m_Manual.FreeAccel(st.chassisKmh, st.pitch, m_SurfKind);
        // ⚠ ABAJO DEL UMBRAL LA REGLA TIENE QUE SEGUIR, NO RENDIRSE.
        // La version anterior salia con `if (kmh <= vBog) return` -- "ya esta abajo, no es
        // asunto mio" -- o sea que abandonaba EXACTAMENTE donde mas falta hacia. Medido:
        // sostuvo cuatro ticks desde 10.28 km/h, el vehiculo cruzo los 5.5, la regla se
        // callo, y de ahi planeo hasta 0.10. Es 8.2 otra vez: una compuerta que deja de
        // actuar justo en el regimen para el que existe.
        // ★★★ EL DISPARO ES "EL GAS NO ALCANZA", NO "SOLTO EL GAS".
        //
        // La version anterior miraba el planeo LIBRE, o sea que solo veia el caso de gas
        // cero. Medido con el planeo ya coherente: en wp 172 el modulo NO solto -- pidio
        // 0.233 -- y el vehiculo cayo igual de 11.88 a 0.14 km/h. Pedir de menos y soltar
        // producen el mismo desenlace, y la regla solo veia uno de los dos.
        //
        // Lo que decide es la aceleracion OBSERVADA: si con el gas que se esta aplicando el
        // vehiculo igual pierde velocidad, ese gas no alcanza, venga de donde venga.
        bool yaAbajo = (st.chassisKmh <= vBog);
        // ★★★ EN REVERSA EL SOSTEN ES PROACTIVO: no se espera a que empiece a caer.
        //
        // Ocho corridas mostraron que CUALQUIER disparo reactivo llega tarde por diseño. La
        // reversa baja de 11.5 a 0.10 km/h en un solo intervalo: el planeo limpio (-0.5)
        // describe bien el tramo 11->6, pero por debajo de ~5-6 entra el arrastre del
        // embrague enganchado -- la restriccion dura ya caracterizada -- y el colapso se
        // completa DENTRO de la ventana de reaccion. Para cuando la aceleracion observada
        // muestra el peligro, el vehiculo ya esta en 0.3 km/h.
        //
        // Asi que mientras la ruta grabada quiera velocidad en reversa, el gas se aplica
        // desde el principio, sin esperar sintoma. El umbral no se elige: es el mismo bog
        // derivado del arrastre, y la ruta misma decide donde soltar -- el grabado del leg 1
        // cae por debajo recien en wp 213-214, o sea justo en la zona de cierre.
        //
        // Se acota a REVERSA por ALCANCE EMPIRICO, no porque el adelante sea distinto: el
        // adelante cierra a 0.10 m y no se toca sin evidencia de que lo necesite.
        bool proactivo = (m_Leg.isReverse && st.chassisKmh > vBog);
        float aReal = aFree;
        float tCruce = 0;
        if (!proactivo)
        {
            // ⚠ SON DOS MAGNITUDES DISTINTAS Y NO SE MEZCLAN.
            // `aFree` es lo que pasaria SI SE SOLTARA el gas; `m_AObs` es lo que esta
            // pasando. La primera version tomaba la peor de las dos, y con gas aplicado y
            // velocidad estable (aObs ~ 0, aFree -0.5) creia que el vehiculo caia. El
            // espejo offline lo agarro sobre el caso de adelante, que anda y no hay que
            // tocar. Manda la observacion: es la unica que dice que esta pasando AHORA.
            if (m_AObs > -AOBS_SANO) return;            // no esta cayendo de verdad
            aReal = m_AObs;
            if (!yaAbajo)
            {
                tCruce = ((st.chassisKmh - vBog) / 3.6) / (-aReal);
                if (tCruce > FEATHER_HORIZONTE_S) return;
            }
        }
        // ★ Y NO ALCANZA CON CANCELAR EL PLANEO: eso solo congela la velocidad que quedo.
        // Si ya viene cayendo hay que RECUPERAR, asi que el objetivo es lo que la ruta pide
        // (techado por el objetivo del planificador), con el horizonte de aceleracion.
        // ★★★ EN REVERSA EL OBJETIVO APUNTA POR ENCIMA DE LA MORDIDA, no al grabado.
        //
        // El grabado de esta reversa es 10.15-10.71 km/h y la mordida cae en 10.20: el
        // humano manejo JUSTO sobre la linea, con embrague para sostenerse ahi. Apuntar al
        // grabado es apuntar al filo. Se apunta por encima, y el techo lo sigue poniendo la
        // envolvente de frenado para no pasarse de la pose.
        float objetivoMs = rec / 3.6;
        if (m_Leg.isReverse)
        {
            float vBite = m_Manual.SpeedAtBiteKmh(st.chassisKmh, st.rpm, st.gear);
            if (PisoDeMordidaAplica(rec, vBite))
            {
                float pisoMs = (vBite * BITE_MARGEN) / 3.6;
                if (pisoMs > objetivoMs) objetivoMs = pisoMs;
            }
            float aFreno = m_Manual.MaxBrakeDecel(m_SurfKind);
            if (aFreno > 0.5)
            {
                float d = m_Leg.remainM - 0.12;
                if (d < 0) d = 0;
                float techoMs = Math.Sqrt(2.0 * aFreno * d);
                if (objetivoMs > techoMs) objetivoMs = techoMs;
            }
        }
        float vMs = st.chassisKmh / 3.6;
        // ⚠⚠⚠ SOSTENER NO ES MANTENER. Por encima de su propio objetivo esta regla no
        // tiene nada que defender, y hasta hoy seguia empujando igual.
        //
        // El motivo esta tres lineas mas abajo, en las DOS demandas que suma:
        //     aRecuperar = (objetivo - v) / tau   -> se anula sola arriba del objetivo  ✔
        //     aCancelar  = -aFree                 -> NO se anula: es gas para CLAVAR la
        //                                            velocidad que el vehiculo tenga ahora
        // Asi que arriba del objetivo `aRecuperar` valia 0 pero `aCancelar` seguia pidiendo
        // gas para cancelar el planeo. La regla no llevaba al vehiculo al objetivo:
        // CONGELABA la velocidad de mas, y el vehiculo no volvia solo nunca.
        //
        // Medido:
        //     HOLD-REVERSA: 12.3 kmh · gas 0.066 -> 0.254
        //     HOLD-REVERSA: 13.2 kmh · gas 0.071 -> 0.286
        // gas creciente a 12-13 km/h, con el objetivo en 11.13 y la mordida en 10.12.
        //
        // ⚠ Y en reversa no habia nada que la apagara: `proactivo` es cierto para CUALQUIER
        // velocidad sobre el bog y saltea las dos guardas de entrada --la de "esta cayendo
        // de verdad" y la del horizonte--, asi que la regla estaba SIEMPRE encendida. Con
        // este techo eso deja de importar: arriba del objetivo se va sola, y abajo el
        // caracter proactivo es justo lo que se queria (anticipar el bog sin esperar a
        // medir la caida). Por eso el arreglo va aca y no en la compuerta.
        //
        // ✖ Se compara contra `objetivoMs` YA TECHADO por la envolvente de frenado: cerca
        // del fin el objetivo baja, y la regla tiene que callarse antes, no despues.
        if (vMs > objetivoMs) return;
        float aRecuperar = (objetivoMs - vMs) / FEATHER_TAU_S;
        if (aRecuperar < 0) aRecuperar = 0;
        // Se cancela la caida REAL, no la teorica: si el vehiculo pierde mas de lo que el
        // planeo libre explica, la diferencia tambien hay que pagarla.
        float aCancelar = -aFree;
        if (-aReal > aCancelar) aCancelar = -aReal;
        float thrSostener = m_Manual.ThrottleForAccel(aCancelar + aRecuperar, st.chassisKmh, st.rpm, m_Out.gear);
        if (thrSostener <= m_Out.throttle) return;      // el modulo ya pide al menos eso
        m_FeatherActivo = true;
        m_FeatherReportes++;
        bool avisar = (m_FeatherReportes < 4 || (m_FeatherReportes % 8) == 0);
        if (avisar && !s_Quiet && BZDiag.On()) Print(TextoSostener(st, aFree, aReal, vBog, tCruce, rec, thrSostener));
        m_Out.throttle = thrSostener;
        m_Out.brake = 0;   // sostener es sostener: ver el bloque de la mordida
    }

    // El vector completo del sosten: lo que el modelo cree, lo que el vehiculo hace, y el
    // gas antes y despues. Es la linea que aisla si el modelo de par miente en reversa.
    private string TextoSostener(BZVehicleState st, float aFree, float aReal, float vBog, float tCruce, float rec, float thr)
    {
        string s = "[BZDrive] SOSTENER";
        if (m_Leg && m_Leg.valid && m_Leg.isReverse) s = "[BZDrive] HOLD-REVERSA";
        s = s + ": " + st.chassisKmh.ToString() + " kmh";
        s = s + " · planeo " + aFree.ToString() + " (real " + aReal.ToString() + ")";
        s = s + " · bog " + vBog.ToString() + " en " + tCruce.ToString() + " s";
        s = s + " · grabado " + rec.ToString();
        s = s + " · gas " + m_Out.throttle.ToString() + " -> " + thr.ToString();
        s = s + " · gear " + m_Out.gear.ToString() + " · rpm " + st.rpm.ToString();
        s = s + " · [la a REAL corresponde al gas " + m_ThrAplicadoPrev.ToString() + " del tick anterior]";
        float vb = m_Manual.SpeedAtBiteKmh(st.chassisKmh, st.rpm, st.gear);
        if (vb > 0.1) s = s + " · mordida a " + vb.ToString() + " kmh";
        s = s + " · [" + m_Manual.DiagBite(st.chassisKmh, st.rpm, st.gear) + "]";
        s = s + " · #" + m_FeatherReportes.ToString();
        return s;
    }

    bool FeatherActivo() { return m_FeatherActivo; }

    // ★★★ EL EVENTO PREVENIBLE ES EL PLANEO, NO LA CLAVADA.
    //
    // La clavada es irreversible -- cinco vias de recuperacion falsadas -- asi que el unico
    // lugar donde se puede actuar es ANTES. Y medido en el RPT, el "antes" es un momento
    // concreto: en wp 172 el modulo solto el gas (thr 0.0068) con el vehiculo a 10.9 km/h y
    // la ruta grabada pidiendo ~10. De ahi el auto planeo hasta el umbral de bog y cuando el
    // modulo volvio a pedir gas (0.89 en wp 183) ya no habia vueltas que construir.
    //
    // SUBVELOCIDAD dispara cuando el dano ya esta hecho; esto dispara en el instante util.
    //
    // ⚠ Y MIDE LAS DOS COSAS A LA VEZ. Ademas del objetivo (que contesta "por que solto"),
    // compara la desaceleracion OBSERVADA con la que el modelo PREDICE. Sospecha concreta:
    // el banco de coast no filtra por marcha ni por sentido, asi que el planeo medido sale
    // de tramos hacia adelante y se aplica igual a la reversa, donde el freno-motor es otro.
    // Si observado >> predicho, el modelo de planeo es el bug y el vehiculo se frena mucho
    // mas rapido de lo que el control cree.
    private void DetectarPlaneo(BZVehicleState st, float dt)
    {
        float kmhPrev = m_KmhPrev;
        m_KmhPrev = st.chassisKmh;
        if (!m_Leg || !m_Leg.valid || !m_Manual) return;
        if (dt < 0.01) return;
        float umbral = m_Manual.creepKmh;
        if (umbral < 0.5) umbral = 0.5;
        float rec = m_Leg.MinRecSpeedAhead(VentanaSubvelM());
        bool rutaQuiereVelocidad = (rec > PisoRecKmh());
        bool todaviaSano = (st.chassisKmh > umbral);
        bool solto = (m_ThrottlePedido < COAST_THR_MAX && m_Out.brake < COAST_THR_MAX);
        if (!rutaQuiereVelocidad || !todaviaSano || !solto)
        {
            m_PlaneoReportes = 0;
            return;
        }
        m_PlaneoReportes++;
        if (m_PlaneoReportes > 1 && (m_PlaneoReportes % COAST_CADA) != 0) return;
        if (s_Quiet || !BZDiag.On()) return;
        float aObs = ((st.chassisKmh - kmhPrev) / 3.6) / dt;
        float aPred = m_Manual.FreeAccel(st.chassisKmh, st.pitch, m_SurfKind);
        string s = "[BZDrive] PLANEO #" + m_PlaneoReportes.ToString();
        s = s + " · SOLTO EL GAS a " + st.chassisKmh.ToString() + " kmh · grabado " + rec.ToString();
        s = s + " · objetivo " + (m_Goal.speedMs * 3.6).ToString() + "kmh";
        s = s + " · thr " + m_Out.throttle.ToString() + " · pedido " + m_ThrottlePedido.ToString();
        s = s + " · brake " + m_Out.brake.ToString();
        s = s + " · modo " + m_Mode.ToString() + " · fase " + BZMcPhase.Name(m_Maneuver.GetPhase());
        if (m_FeatherActivo) s = s + " · SOSTENIENDO";
        s = s + " · a OBSERVADA " + aObs.ToString() + " vs PREDICHA " + aPred.ToString();
        s = s + " · gear " + m_Out.gear.ToString() + " · rpm " + st.rpm.ToString();
        s = s + " · wp " + m_Leg.nearWp.ToString() + " · restan " + m_Leg.remainM.ToString() + "m";
        s = s + " · reversa " + m_Leg.isReverse.ToString();
        s = s + " · pos " + st.pos[0].ToString() + "," + st.pos[2].ToString();
        s = s + " · rumbo " + (st.yaw * Math.RAD2DEG).ToString() + "deg";
        Print(s);
    }

    // ★★★ PERDER VELOCIDAD NO ES LO MISMO QUE ESTAR CLAVADO, Y HAY QUE VERLO ANTES.
    //
    // El bog quedo confirmado como limite duro del motor: no hay recuperacion posible, asi
    // que lo unico que queda es NO ENTRAR. Y para eso hace falta ver el tramo ANTERIOR, que
    // hasta ahora no se logueaba: en wp 184 el auto venia a 8.9 km/h, cayo a 0.1, y recien
    // ahi aparecia el CLAVADO -- con el problema ya consumado.
    //
    // La referencia es la ruta, no una constante: el humano paso ese mismo wp a 10.23 km/h.
    // Si el vehiculo esta muy por debajo de lo GRABADO y no esta frenando a proposito, algo
    // lo esta deteniendo, y este log tiene que decir que.
    private void DetectarSubvelocidad(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid || !m_Manual) return;
        float rec = m_Leg.MinRecSpeedAhead(VentanaSubvelM());
        if (rec < PisoRecKmh())
        {
            m_SubvelTicks = 0;
            m_SubvelReportes = 0;
            return;
        }
        if (st.chassisKmh > rec * SUBVEL_FRAC || m_Out.brake > 0.5)
        {
            m_SubvelTicks = 0;
            m_SubvelReportes = 0;
            return;
        }
        m_SubvelTicks++;
        if (m_SubvelTicks < SUBVEL_TICKS) return;
        if (m_SubvelReportes > 0 && (m_SubvelTicks % SUBVEL_CADA) != 0) return;
        m_SubvelReportes++;
        // El primero del episodio se lleva la caja negra: lo que hay que ver es lo
        // de ANTES, no lo de despues.
        if (m_SubvelReportes == 1) VolcarAnillo("subvelocidad en wp " + m_Leg.nearWp.ToString());
        if (s_Quiet || !BZDiag.On()) return;
        string s = "[BZDrive] SUBVELOCIDAD #" + m_SubvelReportes.ToString() + ReporteDeslizamiento(st);
        s = s + " · kmh " + st.chassisKmh.ToString() + " vs grabado " + rec.ToString();
        s = s + " · modo " + m_Mode.ToString() + " · fase " + BZMcPhase.Name(m_Maneuver.GetPhase());
        s = s + " · objetivo " + (m_Goal.speedMs * 3.6).ToString() + "kmh";
        s = s + " · thr " + m_Out.throttle.ToString() + " · pedido " + m_ThrottlePedido.ToString();
        s = s + " · brake " + m_Out.brake.ToString() + " · gear " + m_Out.gear.ToString();
        s = s + " · rpm " + st.rpm.ToString();
        s = s + " · wp " + m_Leg.nearWp.ToString() + " · restan " + m_Leg.remainM.ToString() + "m";
        s = s + " · lat " + m_Leg.crossTrackM.ToString() + " · reversa " + m_Leg.isReverse.ToString();
        Print(s);
    }

    // ★★★ ABANDONAR UN LEG IMPOSIBLE ES UNA DECISION, NO UNA CURA.
    //
    // El abandono por falta de progreso vivia SOLO en BZManeuverController, asi que un leg
    // manejado en CRUISE con el vehiculo clavado se quedaba ahi para siempre: medido, la
    // ruta terminaba STUCK en wp 236 con el resto del recorrido sin manejar. Es la misma
    // asimetria de modo que ya arreglamos dos veces (el ROCK y el gas de re-arranque).
    //
    // Y NO contradice "detectar no es actuar": no se intenta curar la clavada -- eso quedo
    // falsado por seis vias. Se reconoce que ese leg no se puede completar y se pasa al
    // siguiente, para que la ruta maneje TODO LO QUE SI PUEDE. La honestidad la pone el
    // evento: la ruta termina ROUTE_INCOMPLETE, nunca COMPLETE.
    private void AbandonarSiEsImposible()
    {
        if (m_StallTicks <= ABANDONO_TICKS) return;
        if (!m_Leg || !m_Leg.valid) return;
        if (m_Mode == BZDriveMode.BZDM_MANEUVER) return;   // ahi el modulo ya tiene su propio abandono
        // hueco B: el abandono ESPERA a C1. Medido (OFFROAD_CUESTA_0 20:00:01): "LEG 0 ABANDONADO: clavado
        // 20 ticks" con el hillstart #2 corriendo -> "resultado cortada: cambio de leg o de modo". Espera mientras la
        // recuperacion corre y mientras un episodio abierto con intentos disponibles arma su ventana de deteccion; al
        // cerrar la recuperacion TerminarRecuperacionAhogo pone la cuenta de clavado en cero (10 s mas antes de
        // abandonar, contra 3 s de ventana de C1). Intentos agotados -> el abandono de siempre.
        if (m_BogActivo) return;
        if (m_BogVentOk && m_BogEpisodio && m_BogEnabled && (m_BogMaxInt <= 0 || m_BogIntentos < m_BogMaxInt)) return;
        // G1: con la escalera fisica activa el AR nunca corta la toma; solo agotada vuelve el abandono.
        if (m_F1Activo || m_ReubPend) return;
        if (m_AR.FisicoEnCurso()) return;
        m_LegsAbandonados++;
        m_StallTicks = 0;
        m_StallReported = 0;
        // "se sigue con el proximo" es FALSO cuando el leg abandonado es el ULTIMO: ahi no se
        // sigue con nada, se TERMINA la ruta (AdvanceLeg devuelve false -> OnArrived -> ROUTE_INCOMPLETE).
        // Medido: el coche 108 abandono el leg 1 (el ultimo) en el wp 3452 de 17363 y la ruta se dio por
        // terminada a 12,5 km del destino, con el vehiculo existiendo y sin nadie que lo mirara.
        bool hayProximo = (m_Leg.legIdx + 1 < m_Route.LegCount());
        string finAb = " · se sigue con el proximo";
        if (!hayProximo) finAb = " · ERA EL ULTIMO LEG: la ruta TERMINA aca, a " + m_Leg.remainM.ToString() + " m del final";
        m_CierrePorQue = "abandono: clavado " + ABANDONO_TICKS.ToString() + " ticks a fondo sin avanzar en wp " + m_Leg.nearWp.ToString();
        if (!s_Quiet) Print("[BZDrive] LEG " + m_Leg.legIdx.ToString() + " ABANDONADO: clavado " + ABANDONO_TICKS.ToString() + " ticks a fondo sin avanzar en wp " + m_Leg.nearWp.ToString() + " · restaban " + m_Leg.remainM.ToString() + " m · se sigue con el proximo");
        OnLegDone();
    }

    int LegsAbandonados() { return m_LegsAbandonados; }

    static void SetMarchaForzada(int gearDayZ) { s_MarchaForzada = gearDayZ; }

    // ★★ LA CLAVADA SE MIDE Y SE INFORMA; NO SE INTENTA CURAR DESDE EL CONTROL.
    //
    // El run 08:44 falso el rock, y de paso mostro que "la cuesta" era el encuadre
    // equivocado: la reversa tambien se clava, en wp 192, con pendiente de 0.10 grados --
    // terreno plano, donde el humano iba a 11.24 km/h. Dos clavadas con el mismo sintoma
    // (`thr 1 · rpm ~1150 · no avanza`) y una sin cuesta ninguna.
    //
    // Mientras no se sepa QUE la produce, el control no tiene nada honesto que hacer: lo
    // util es dejar el vector de actuacion completo en el log, una vez y periodico, para
    // que la proxima corrida discrimine en vez de adivinar.
    private void DetectarClavada(BZVehicleState st)
    {
        m_NoStallPorque = "";
        // ⚠ Avanzo() TIENE EFECTO LATERAL (mueve m_StallArcPrev): se evalua UNA sola vez por
        // tick y el resultado se reusa. Llamarla de nuevo corromperia la referencia de arco.
        m_AvanzoEsteTick = Avanzo(st);
        // 1: la maniobra F1 (reversa) y su gracia no son una clavada: retroceder con el modulo pidiendo
        // adelante ES la maniobra. Medido: el CLAVADO que decidio el escalon 2 era la fila del fin de la reversa.
        if (m_F1Activo || m_F1GraciaT > 0)                               { m_StallTicks = 0; m_NoStallPorque = "maniobra F1 / gracia"; }
        else if (m_ThrottlePedido < STALL_FULL_THR)                      { m_StallTicks = 0; m_NoStallPorque = "gas pedido " + m_ThrottlePedido.ToString(); }
        else if (st.wheelCount > 0 && st.wheelsOnGround < st.wheelCount)
        {
            m_StallTicks = 0;
            m_NoStallPorque = "ruedas " + st.wheelsOnGround.ToString() + "/" + st.wheelCount.ToString();
            if (Math.AbsFloat(st.chassisKmh) < LOG_COLA_QUIETO_KMH && (m_CintaReposoT >= m_AR.WaitS() || m_QuietoT >= m_AR.WaitS())) ClasificarFisico(st, "RUEDAS EN EL AIRE");
        }
        // (BusDrive 2.5): UNA PARADA GRABADA NO ES UNA CLAVADA. Mientras la cinta sostiene una meseta (la parada
        // del que grabo, freno 1), el modulo sigue pidiendo gas contra ese freno y el vehiculo no avanza: el contador se
        // llenaba DURANTE el hold y disparaba CLAVADO -> AR fisico (reversa 3 m y re-encarar) en el primer tick de salida.
        // MEDIDO: las 22 escaleras abiertas por CLAVADO en los RPT del 20-23/09 estan en paradas grabadas de 4,7-5,25 s
        // (el giro del ramal de Pavlovo, wp 1248, TODAS las vueltas; Svetloyarsk wp 14607, tres buses; Svergino wp 816):
        // los "puntos negros donde la toma va a 5-13 km/h" son los lugares donde el que grabo se DETUVO.
        else if (CintaEnMeseta())                                       { m_StallTicks = 0; m_NoStallPorque = "meseta grabada (la cinta sostiene la parada)"; }
        else if (m_AvanzoEsteTick)                                      { m_StallTicks = 0; m_NoStallPorque = "avanzando"; }
        else
        {
            m_StallTicks++;
            m_NoStallPorque = "acumulando " + m_StallTicks.ToString() + "/" + (STALL_TICKS + 1).ToString();
        }

        if (m_StallTicks == 0) { m_StallReported = 0; return; }
        if (m_StallTicks <= STALL_TICKS) return;
        // primera vez, y despues cada tanto: el vector COMPLETO
        if (m_StallReported > 0 && (m_StallTicks % STALL_REPORT_EVERY) != 0) return;
        m_StallReported++;
        if (m_StallReported == 1) ClasificarClavada(st);
        if (s_Quiet) return;
        Print("[BZDrive] CLAVADO #" + m_StallReported.ToString() + " · modo " + m_Mode.ToString() + ReporteDeClavada(st));
    }

    // ★★★ C1 AHOGO DEL MOTOR: ES DEL CONTROLADOR, NO DEL AR.
    // Usuario: "si el humano grabo sin ahogo asi deberia reproducir; pero Boris puede ahogarse por venir de un AR,
    // a una velocidad que no es la de la grabacion. En el caso de ahogo de motor por cualquier causa deberia forzar
    // motor y aceleracion en gear 2." · "nunca se queda clavado ahogado, algun movimiento tiene" · "se va a notar
    // porque el vehiculo pide FW mientras retrocede".
    // FIRMA: marcha adelante + gas APLICADO >= 0,5 + sin freno + 4/4 + sin avance neto sobre la ruta durante WindowS
    // Y (rodo para atras con el chasis mirando adelante  O  rpm clavado cerca del ralenti). Excluye el CHOQUE del
    // AR (caida >= 20 km/h en 0,5 s, a < 8 m): contra un objeto tambien hay gas + rpm baja + 0 km/h -- esa confusion
    // ya costo cuatro rondas (ver DetectarChoque). Choque = sin movimiento; ahogo = movimiento sin avance.
    // ✖ NO es un detector por 'quieto N segundos': el usuario lo dijo, un ahogo tiembla, cabecea o retrocede.
    // ✖ El gas PEDIDO no alcanza como firma: en el deadlock de coasteo la cinta aplica 0 y el LAUNCH
    //   pide 1. Eso es la firma NO-FISICA de la parte 1, no un ahogo.
    // MEDIDO ANTES (11-14/09, 141 lineas SUBVELOCIDAD/CLAVADO): CERO casos de marcha >= 3 a < 5 km/h con gas a fondo,
    // y los de primera a < 2 km/h son el choque de PAZ. No habia un ahogo real en los logs de 1.2.
    // ✅ VALIDADO: DEMOBUS desde 0 en la subida (wp 3880, 3,4 grados, cinta en CUARTA): sin la accion
    // `v adelante min -2,88 · avance -1,23 m en 3 s` -> LEG ABANDONADO; con la accion `sube a 33,84 km/h en 7 s (avance
    // 23,84 m)` y recorre los 3,4 km. 0 lineas AHOGO en las 8 corridas normales. Accion ENCENDIDA por defecto.
    // ACCION (Enabled): soltar la cinta, PRIMERA + la rampa del HILLSTART (freno sostenido mientras el gas no alcanza
    // para la pendiente, despues suelta) -- lo unico que trepo (7,7 grados); ROCK/gas constante/neutro quedaron
    // falsados (cicatriz de RampaDeArranque). Se devuelve el mando cuando avanzo 1 m y va a >= 85% de lo grabado
    // adelante (la cinta re-arma con SU marcha a una velocidad a la que esa marcha no se ahoga).
    private void DetectarAhogo(BZVehicleState st, float dt)
    {
        float fwdMinAh = m_BogFwdMinInt;
        m_BogFwdMinInt = 1000.0;
        if (m_BogActivo) return;
        if (m_F1Activo)
        {
            m_BogVentOk = false;   // 1: retroceder a proposito no es un ahogo
            return;
        }
        if (!m_Leg || !m_Leg.valid || !m_Manual)
        {
            m_BogVentOk = false;
            return;
        }
        if (m_Leg.legIdx != m_BogLeg)
        {
            m_BogLeg = m_Leg.legIdx;
            m_BogVentOk = false;
            m_BogEpisodio = false;
            m_BogIntentos = 0;
            m_BogAvisos = 0;
            m_SostenTope = 0;
        }
        if (m_BogEpisodio && m_Leg.alongM - m_BogAlongEp >= AHOGO_EPISODIO_M)
        {
            m_BogEpisodio = false;
            m_BogIntentos = 0;
            m_BogAvisos = 0;
        }
        bool elegibleAh = true;
        if (m_Leg.isReverse) elegibleAh = false;
        if (st.gear < 2) elegibleAh = false;
        if (m_BogThrAp < AHOGO_GAS_MIN) elegibleAh = false;
        if (m_BogBrkAp > 0.1) elegibleAh = false;
        if (st.wheelCount > 0 && st.wheelsOnGround < st.wheelCount) elegibleAh = false;
        if (m_Mode != BZDriveMode.BZDM_CRUISE && m_Mode != BZDriveMode.BZDM_APPROACH && m_Mode != BZDriveMode.BZDM_MANEUVER) elegibleAh = false;
        if (m_AR.HuboChoqueAca(st.pos)) elegibleAh = false;
        if (!elegibleAh)
        {
            m_BogVentOk = false;
            return;
        }
        if (!m_BogVentOk)
        {
            m_BogVentOk = true;
            m_BogVentT = 0;
            m_BogVentAlong0 = m_Leg.alongM;
            m_BogVioAtras = false;
            m_BogVioRalenti = false;
            m_BogFwdMinVent = 1000.0;
        }
        m_BogVentT = m_BogVentT + dt;
        if (fwdMinAh < m_BogFwdMinVent) m_BogFwdMinVent = fwdMinAh;
        if (fwdMinAh < -AHOGO_ATRAS_KMH) m_BogVioAtras = true;
        if (st.rpm > 0 && m_Manual.rpmIdle > 0 && st.rpm <= m_Manual.rpmIdle * AHOGO_RALENTI_FRAC) m_BogVioRalenti = true;
        float avanceAh = m_Leg.alongM - m_BogVentAlong0;
        if (avanceAh >= m_BogMinProgM)
        {
            m_BogVentOk = false;
            return;
        }
        if (m_BogVentT < m_BogWindowS) return;
        if (!m_BogVioAtras && !m_BogVioRalenti)
        {
            m_BogVentOk = false;
            return;
        }
        if (!m_BogEpisodio)
        {
            m_BogEpisodio = true;
            m_BogAlongEp = m_Leg.alongM;
            m_BogIntentos = 0;
            m_BogAvisos = 0;
        }
        string evAh = "modo " + m_Mode.ToString() + " · marcha " + st.gear.ToString() + " · gas aplicado " + m_BogThrAp.ToString();
        evAh = evAh + " (pedido " + m_ThrottlePedido.ToString() + ") · rpm " + st.rpm.ToString() + " (ralenti " + m_Manual.rpmIdle.ToString() + ")";
        evAh = evAh + " · v adelante min " + m_BogFwdMinVent.ToString() + " km/h · avance " + avanceAh.ToString() + " m en " + m_BogVentT.ToString() + " s";
        evAh = evAh + " · pitch " + (st.pitch * Math.RAD2DEG).ToString() + " · atras " + m_BogVioAtras.ToString() + " · ralenti " + m_BogVioRalenti.ToString();
        evAh = evAh + " · cinta posee " + m_CintaRodo.ToString() + " · wp " + m_Leg.nearWp.ToString();
        m_BogVentOk = false;
        if (m_Mode == BZDriveMode.BZDM_MANEUVER || !m_BogEnabled)
        {
            m_BogAvisos = m_BogAvisos + 1;
            if (m_BogAvisos > 3 || s_Quiet) return;
            string sinAcc = "BogRecovery desactivado: sin accion";
            if (m_Mode == BZDriveMode.BZDM_MANEUVER) sinAcc = "maniobra (tiene su propio HILLSTART): sin accion";
            Print("[C12] AHOGO detectado (" + evAh + ") -> " + sinAcc + SufijoRunner());   // con runner
            float sAhSin = -1;
            if (m_Cinta) sAhSin = m_Cinta.SEst();
            m_AR.Ahogo(m_Leg.nearWp, sAhSin, evAh, sinAcc);   // al FIN y al CSV
            m_AR.ResultadoAhogo("sin accion");
            return;
        }
        // con la escalera abierta el AR es dueño del vehiculo. Un AHOGO ahi no arranca primera + hillstart
        // (el 106 del 23/09 arranco su #2 a las 17:31:43 bajo una escalera abierta a las 17:31:32 y la escalera se lo corto
        // 11 s despues): va al AR como evidencia, que es quien abre el escalon siguiente. El AHOGO sigue siendo un
        // acelerador de la escalera (3 s de ventana contra los 20 s del SAFE_STOP del arbitro).
        bool escaleraAh = m_AR.FisicoEnCurso();
        if (escaleraAh || (m_BogMaxInt > 0 && m_BogIntentos >= m_BogMaxInt))
        {
            float sAgot = -1;
            if (m_Cinta) sAgot = m_Cinta.SEst();
            string cabAh = "AHOGO persistente tras " + m_BogIntentos.ToString() + " intentos · ";
            if (escaleraAh) cabAh = "AHOGO con la escalera abierta (sin primera + hillstart: el AR es dueño) · ";
            m_AR.Fisico(st.pos, m_Leg.nearWp, sAgot, cabAh + evAh, BZAutoRecovery12.RuedasEnElPiso(st));
            return;
        }
        m_AR.CerrarPorAhogo();   // el re-arranque pendiente, si lo hay, no saco al vehiculo
        m_SostenTope = 0;
        m_BogActivo = true;
        m_BogIntentos = m_BogIntentos + 1;
        float sAhAcc = -1;
        if (m_Cinta) sAhAcc = m_Cinta.SEst();
        m_AR.Ahogo(m_Leg.nearWp, sAhAcc, evAh, "primera + hillstart #" + m_BogIntentos.ToString());   // al FIN y al CSV
        m_BogRecT = 0;
        m_BogRampa = 0;
        m_BogQuietoT = 0;
        m_BogRecAlong0 = m_Leg.alongM;
        m_BogRecVentT = 0;
        m_BogRecVentAlong0 = m_Leg.alongM;
        if (m_CintaRodo)
        {
            m_CintaRodo = false;
            m_CintaQuieto = true;
            m_CintaRearmaDist = -1;
        }
        if (!s_Quiet) Print("[C12] AHOGO (" + evAh + ") -> primera + hillstart #" + m_BogIntentos.ToString() + SufijoRunner());
    }

    private void AplicarRecuperacionAhogo(BZVehicleState st, float dt)
    {
        if (!m_BogActivo) return;
        if (!m_Leg || !m_Leg.valid || !m_Manual || m_Leg.isReverse || m_Mode == BZDriveMode.BZDM_ENDPOINT || m_Mode == BZDriveMode.BZDM_HALTED)
        {
            // LA CAUSA, NO LA CATEGORIA. Los 8 'cortada: cambio de leg o de modo' del 23/09 fueron todos modo
            // HALTED en el tick en que otro tomo el vehiculo: la escalera del AR abriendose (SAFE_STOP del arbitro) o el
            // consumidor retirando el bus (Abort). Ninguno fue un cambio de leg.
            string causaC = "cortada: ";
            if (!m_Leg || !m_Leg.valid || !m_Manual) causaC = causaC + "leg invalido";
            else if (m_Leg.isReverse) causaC = causaC + "leg en reversa";
            else if (m_Mode == BZDriveMode.BZDM_ENDPOINT) causaC = causaC + "modo ENDPOINT";
            else causaC = causaC + "modo HALTED (la escalera del AR abriendose, o Abort del consumidor)";
            if (m_AR.FisicoEnCurso()) causaC = causaC + " · escalera abierta";
            TerminarRecuperacionAhogo(causaC);
            return;
        }
        m_BogRecT = m_BogRecT + dt;
        float avRec = m_Leg.alongM - m_BogRecAlong0;
        float recAh = m_Leg.MinRecSpeedAhead(VentanaSubvelM());
        float objAh = recAh * AHOGO_SALIDA_FRAC;
        if (objAh < AHOGO_SALIDA_MIN_KMH) objAh = AHOGO_SALIDA_MIN_KMH;
        if (avRec >= 1.0 && st.chassisKmh >= objAh)
        {
            m_SostenTope = AHOGO_MARCHA;   // hueco A: la cinta re-arma con tope (AplicarTopeDeSosten)
            m_SostenAplicado = false;
            TerminarRecuperacionAhogo("sube a " + st.chassisKmh.ToString() + " km/h en " + m_BogRecT.ToString() + " s (avance " + avRec.ToString() + " m)");
            return;
        }
        // si ya avanzo >= 1 m y despues se queda 3 s por debajo de 2 km/h, no es un ahogo: es una traba.
        // Se cierra YA (medido: 2 intentos de 20,5 s "falla: avanzo 4,14 m") y el proximo AHOGO va directo al FISICO
        // (intentos agotados) -> la escalera.
        if (avRec >= 1.0 && st.chassisKmh < CINTA_RODANDO_KMH) m_BogQuietoT = m_BogQuietoT + dt;
        else m_BogQuietoT = 0;
        if (m_BogQuietoT >= AHOGO_QUIETO_S)
        {
            // LA TRABA ES DE LA ESCALERA, NO DEL PROXIMO AHOGO. El veredicto dejaba al vehiculo
            // esperando OTRO AHOGO (una ventana de 3 s con gas aplicado >= 0,5 que la rampa de arranque reinicia cada vez
            // que se re-arma): el 105 de BusDrive espero 32,5 s a gas 1 y rpm en ralenti entre el veredicto (17:31:48) y la
            // escalera (17:32:20), con SIN RESCATE publicando cada 0,5 s. Poblacion (AR_log.csv 19-23/09): 68 veredictos, en
            // 62 la escalera llego despues por ese camino. El veredicto YA es la evidencia fisica: la escalera se abre aca, en
            // este tick. El print sale ANTES de agotar los intentos (decia '#2' para el intento #1). Los intentos quedan
            // agotados para el episodio: un AHOGO mas, con la escalera abierta, no arranca otra primera + hillstart.
            string vered = "avanzo " + avRec.ToString() + " m y se quedo " + AHOGO_QUIETO_S.ToString() + " s a < 2 km/h: traba, no ahogo";
            TerminarRecuperacionAhogo(vered + " -> abre la escalera fisica");
            m_BogIntentos = m_BogMaxInt;
            ClasificarFisico(st, "AHOGO: " + vered);
            return;
        }
        // EL C1 NO SE QUEDA SUS 20 s ARRASTRANDOSE. La traba por progreso no puede abrir la
        // escalera mientras el C1 es dueño (m_BogActivo: dos actores no), y el veredicto de arriba (3 s a < 2 km/h) no ve a un
        // bus que oscila entre 1,4 y 2,5 km/h: el 101 (24/09 17:19, cuesta de 3 grados) avanzo 3,09 m en 20,5 s a gas 0,5 y
        // recien al vencer AHOGO_TOPE_S abrio la escalera. Cada ABANDONO (10 s) se mide el avance de esa ventana: si no llego a
        // lo que se rueda a CINTA_RODANDO_KMH en ese tiempo (2 km/h x 10 s = 5,6 m), no es un ahogo que se cure con primera +
        // hillstart: es una traba, y la escalera se abre aca (954). Sin numeros nuevos: la ventana y la velocidad ya existian.
        // ⚠ Falsador en la linea: un hillstart REAL en cuesta (Sosnovka +9,9 %) sigue cerrando con "sube a X km/h"; si un
        // veredicto de ventana va seguido de una subida limpia con el escalon 1, es un falso positivo y se cuenta.
        m_BogRecVentT = m_BogRecVentT + dt;
        if (m_BogRecVentT >= ABANDONO_TICKS * dt)
        {
            float avVent = m_Leg.alongM - m_BogRecVentAlong0;
            float minVent = (CINTA_RODANDO_KMH / 3.6) * m_BogRecVentT;
            if (avVent < minVent)
            {
                string veredV = "avanzo " + avVent.ToString() + " m en los ultimos " + m_BogRecVentT.ToString() + " s de la recuperacion";
                veredV = veredV + " (rodando a " + CINTA_RODANDO_KMH.ToString() + " km/h serian " + minVent.ToString() + " m): traba, no ahogo";
                TerminarRecuperacionAhogo(veredV + " -> abre la escalera fisica");
                m_BogIntentos = m_BogMaxInt;
                ClasificarFisico(st, "AHOGO: " + veredV);
                return;
            }
            m_BogRecVentT = 0;
            m_BogRecVentAlong0 = m_Leg.alongM;
        }
        if (m_BogRecT > AHOGO_TOPE_S)
        {
            string resTope = "falla: avanzo " + avRec.ToString() + " m en " + m_BogRecT.ToString() + " s";
            if (avRec >= 1.0) resTope = "avanzo " + avRec.ToString() + " m pero no llego a " + objAh.ToString() + " km/h en " + m_BogRecT.ToString() + " s";
            TerminarRecuperacionAhogo(resTope);
            return;
        }
        float pisoAh = m_Manual.launchThrottleFloor;
        if (pisoAh < 0.01) pisoAh = BZVehicleManual.LAUNCH_PRIOR_FLOTA;   // una sola fuente
        float thrAh = pisoAh + m_BogRampa * ARRANQUE_STEP;
        if (thrAh > 1.0) thrAh = 1.0;
        m_BogRampa = m_BogRampa + 1;
        m_Out.throttle = thrAh;
        m_Out.brake = 0;
        if (thrAh < GasParaSostenerPendiente(st) && m_BogRampa <= AHOGO_HOLD_TICKS) m_Out.brake = 1.0;
        m_Out.gear = AHOGO_MARCHA;
    }

    private void TerminarRecuperacionAhogo(string resultado)
    {
        m_BogActivo = false;
        m_StallTicks = 0;   // hueco B: la cuenta del abandono arranca de nuevo al cerrar la recuperacion
        m_AR.ResultadoAhogo(resultado);
        if (!s_Quiet) Print("[C12] AHOGO primera + hillstart #" + m_BogIntentos.ToString() + " · resultado " + resultado + SufijoRunner());
    }

    // (40 Hz, dentro del bloque de la cinta, despues del iman): ver las constantes FRENO_ANT_*.
    private void AplicarFrenoAnticipado(BZVehicleState st)
    {
        if (st.chassisKmh < FRENO_ANT_V_KMH) return;
        float sAhoraF = m_Cinta.SEst();
        float alcanceF = st.chassisKmh / 3.6 * FRENO_ANT_T_S;
        float sIniF = 0;
        float sFinF = 0;
        float brkF = 0;
        float kmhFinF = 0;
        if (!m_Cinta.FrenoBajadaAdelante(sAhoraF, alcanceF, FRENO_ANT_V_KMH, FRENO_ANT_PEND_DEG, sIniF, sFinF, brkF, kmhFinF)) return;
        float sobraF = st.chassisKmh - kmhFinF;
        if (m_FrenoAntSIni != sIniF)
        {
            m_FrenoAntSIni = sIniF;
            if (!s_Quiet) Print("[C12] FRENO ANTICIPADO: freno grabado " + brkF.ToString() + " en s " + sIniF.ToString() + "-" + sFinF.ToString() + " sobre bajada <= " + FRENO_ANT_PEND_DEG.ToString() + " deg (sale a " + kmhFinF.ToString() + " km/h) · visto desde s " + sAhoraF.ToString() + " a " + st.chassisKmh.ToString() + " km/h (sobra " + sobraF.ToString() + ")");
        }
        if (sAhoraF < sIniF)
        {
            // antes del bloque grabado: nadie acelera hacia una frenada grabada; si sobra velocidad, se regula suave
            if (sobraF < -FRENO_ANT_MARGEN_KMH) return;
            m_Out.throttle = 0;
            if (sobraF > FRENO_ANT_MARGEN_KMH * 0.5)
            {
                float frenoF = Math.Clamp(IMAN_FRN_POR_KMH * sobraF, 0.0, brkF);
                if (frenoF > m_Out.brake) m_Out.brake = frenoF;
            }
            return;
        }
        // dentro del bloque grabado: si ya se viene a la velocidad de salida y el iman no pide freno, el escalon no se pisa
        bool suprimeF = (sobraF <= FRENO_ANT_MARGEN_KMH && m_ImanFrn <= 0.001);
        if (m_FrenoAntSBloq != sIniF)
        {
            m_FrenoAntSBloq = sIniF;
            string queF = "APLICADO";
            if (suprimeF) queF = "SUPRIMIDO";
            if (!s_Quiet) Print("[C12] FRENO ANTICIPADO: bloque s " + sIniF.ToString() + " alcanzado a " + st.chassisKmh.ToString() + " km/h (sale a " + kmhFinF.ToString() + ", sobra " + sobraF.ToString() + ", iman freno " + m_ImanFrn.ToString() + ") -> freno grabado " + brkF.ToString() + " " + queF);
        }
        if (suprimeF)
        {
            m_Out.brake = 0;
            m_Out.throttle = 0;
        }
    }

    // hueco A: TOPE DE SOSTEN (40 Hz, dentro del bloque de la cinta, despues de `gear = toma`).
    // Medido (OFFROAD_CUESTA_0 19:59:48-52): C1 sube en PRIMERA a 18,85 km/h, devuelve el mando y la cinta re-arma
    // con SU marcha -- segunda, la que el humano llevaba entrando a 30 km/h con impulso -- a 25,5 grados: ahogo de
    // nuevo en 4 s rodando para atras. El usuario verifico a mano que en PRIMERA sube desde 0.
    // Se sostiene la marcha de C1 hasta que (a) la toma misma va en una marcha <= al tope, (b) la velocidad alcanza
    // la grabada (ahi la marcha del humano hace lo que le hizo al humano), o (c) la toma NO muestra que esa marcha a
    // fondo perdia velocidad en este punto (BZCintaRunner.MarchaPierdeAFondo). Sin (c) el tope pisaria WRX_CUESTA_0
    // y DEMOBUS_SUBIDA, donde la marcha grabada sube bien (banco: mutante sin evidencia -> 1,7 y 4,8 m de tope).
    // Solo existe despues de un C1 con exito: en las corridas sin AHOGO no cambia nada por construccion.
    private void AplicarTopeDeSosten(BZVehicleState st)
    {
        string sueltaSo = "";
        if (m_CintaM.enReversa || m_CintaM.gear < AHOGO_MARCHA) sueltaSo = "la toma no va hacia adelante";
        else if (m_CintaM.gear <= m_SostenTope) sueltaSo = "la toma ya va en marcha " + m_CintaM.gear.ToString();
        else if (st.fwdKmh >= m_CintaM.kmhRec) sueltaSo = "alcanzo lo grabado (" + st.fwdKmh.ToString() + " de " + m_CintaM.kmhRec.ToString() + " km/h)";
        else if (!m_Cinta.MarchaPierdeAFondo()) sueltaSo = "la toma no muestra perdida a fondo en marcha " + m_CintaM.gear.ToString();
        float sSo = m_Cinta.SEst();
        if (sueltaSo != "")
        {
            if (!s_Quiet)
            {
                if (m_SostenAplicado) Print("[C12] SOSTEN suelto en s " + sSo.ToString() + " tras " + (sSo - m_SostenS0).ToString() + " m · " + sueltaSo);
                else Print("[C12] SOSTEN no hace falta en s " + sSo.ToString() + " (marcha de la toma " + m_CintaM.gear.ToString() + ") · " + sueltaSo);
            }
            m_SostenTope = 0;
            m_SostenAplicado = false;
            return;
        }
        if (!m_SostenAplicado)
        {
            m_SostenAplicado = true;
            m_SostenS0 = sSo;
            if (!s_Quiet) Print("[C12] SOSTEN: la cinta re-arma en marcha " + m_CintaM.gear.ToString() + " a " + st.fwdKmh.ToString() + " km/h (grabado " + m_CintaM.kmhRec.ToString() + ") y en la toma esa marcha a fondo PERDIA velocidad aca -> sostiene marcha " + m_SostenTope.ToString() + " · s " + sSo.ToString());
        }
        m_Out.gear = m_SostenTope;
    }

    // Mismo calculo que BZManeuverController.HoldThrottle: para no rodar hacia atras hace falta a >= g*sin(pitch).
    private float GasParaSostenerPendiente(BZVehicleState st)
    {
        float pisoSp = m_Manual.launchThrottleFloor;
        if (m_Manual.maxAccelMs2 < 0.1) return pisoSp;
        float aPend = 9.81 * Math.Sin(st.pitch);
        if (aPend <= 0) return pisoSp;
        return Math.Clamp(aPend / m_Manual.maxAccelMs2, 0.1, 1.0);
    }

    // "se pide avanzar y no avanza". Si la CINTA posee a un vehiculo quieto con gas < 0,9, la
    // traba es la firma NO-FISICA (la cinta frena o costea) y la resuelve o la declara Sample; aca solo se
    // REGISTRA lo FISICO: gas a fondo sin avanzar, ruedas en el aire, patinando, ahogado, tras un choque.
    // SIN ACCION (pendiente de diseño): el flujo sigue siendo el de siempre (CLAVADO -> ABANDONADO/STUCK).
    private void ClasificarClavada(BZVehicleState st)
    {
        if (m_BogEpisodio) return;   // C1: un ahogo NO es un FISICO sin accion; lo registra su detector
        if (m_CintaRodo)
        {
            // 4 (#1): con el re-arranque logico AGOTADO en este lugar, un CLAVADO es fisico aunque la cinta frene
            if (m_CintaM.thr < 0.9 && !m_AR.LogicoAgotadoAca(st.pos)) return;
        }
        ClasificarFisico(st, "CLAVADO");
    }

    private void ClasificarFisico(BZVehicleState st, string origen)
    {
        string evF = origen + " · gas pedido " + m_ThrottlePedido.ToString() + " · marcha " + m_Out.gear.ToString();
        evF = evF + " · kmh " + st.chassisKmh.ToString() + " · rpm " + st.rpm.ToString();
        evF = evF + " · ruedas " + st.wheelsOnGround.ToString() + "/" + st.wheelCount.ToString();
        float deslF = DeslizamientoMedido(st);
        if (deslF >= 0) evF = evF + " · desliza " + deslF.ToString();
        if (m_CintaRodo) evF = evF + " · cinta gas " + m_CintaM.thr.ToString();
        evF = evF + m_AR.EvidenciaChoque(st.pos);
        float sF = -1;
        if (m_Cinta) sF = m_Cinta.SEst();
        int wpF = -1;
        if (m_Leg && m_Leg.valid) wpF = m_Leg.nearWp;
        m_AR.Fisico(st.pos, wpF, sF, evF, BZAutoRecovery12.RuedasEnElPiso(st));

    }

    // EL AVISO SALE DE LA TRANSICION, NO DE QUIEN ABRE. El 944 lo colgaba de ClasificarFisico y el
    // camino DebeReArrancar -> Fisico (el re-arranque NO-FISICO que ve gas aplicado >= 0,9 o un choque reciente y deriva a
    // FISICO) abria la escalera en silencio: 3 '[C12] BLOQUEADO' contra 7 'BLOQUEO terminado' en la tanda del 948, y sin
    // BZDriveEv.BLOQUEADO por ese camino. Un solo lugar, al final del Plan, despues de todos los detectores: si la escalera
    // paso de cerrada a abierta desde el Plan anterior, se avisa con la evidencia con que el AR la abrio. Cualquier camino
    // futuro queda cubierto por construccion. Falsador: #BLOQUEADO == #BLOQUEO terminado, por coche y por episodio.
    private void DetectarAperturaDeEscalera(BZVehicleState st)
    {
        bool ahora = m_AR.FisicoEnCurso();
        if (ahora && !m_EscaleraPrev) AvisarBloqueo(st, m_AR.FisicoEvidencia());
        m_EscaleraPrev = ahora;
    }

    // el aviso al consumidor, UNA vez por escalera abierta (por CLAVADO, por SAFE_STOP o por progreso).
    // NO cierra nada: la escalera sigue. Que hacer (esperar, retroceder, rendirse) es de quien usa el framework. El motivo
    // lleva el choque reciente si lo hubo: es lo que separa "chocamos con alguien" de "nos trabamos solos".
    private void AvisarBloqueo(BZVehicleState st, string motivo)
    {
        m_BloqueoMotivo = motivo;
        // DOS definiciones de choque, las dos con nombre. El AR cuenta 20 km/h perdidos en 0,5 s aca
        // (decide con esa); la linea *** CHOQUE *** cuenta una frenada imposible en 25 ms (la del log y de RunnerChoqueHaceS()).
        if (!m_AR.HuboChoqueAca(st.pos)) m_BloqueoMotivo = m_BloqueoMotivo + " · sin choque que el AR cuente";
        if (m_ChoqueHaceS >= 0) m_BloqueoMotivo = m_BloqueoMotivo + " · ultimo *** CHOQUE *** de la linea hace " + m_ChoqueHaceS.ToString() + " s";
        else m_BloqueoMotivo = m_BloqueoMotivo + " · sin *** CHOQUE *** en la corrida";
        int wpB = -1;
        if (m_Leg && m_Leg.valid) wpB = m_Leg.nearWp;
        // con un runner (etiqueta puesta) la linea BLOQUEADO la imprime el runner, una por escalera abierta: antes
        // salian DOS por apertura y el protocolo cuenta "tantos BLOQUEADO como BLOQUEO terminado". Sin runner (el
        // banco) esta es la unica.
        if (!s_Quiet && m_EtiqRunner == "") Print("[C12] BLOQUEADO en wp " + wpB.ToString() + " · " + m_BloqueoMotivo + " · la escalera del AR sigue; que hacer es del consumidor" + SufijoRunner());
        Emit(BZDriveEv.BLOQUEADO, wpB);
    }

    // el ESTADO para el runner (BusDrive lo consulta por polling): bloqueado = escalera del AR en curso.
    bool Bloqueado() { return m_AR.FisicoEnCurso(); }
    string MotivoBloqueo() { return m_BloqueoMotivo; }

    // para el consumidor (BusDrive, ): cuanto hace del ultimo *** CHOQUE *** de la linea (-1 si no hubo)
    // y si el framework esta recuperando (primera + hillstart, F1, re-ubicacion o escalera): superconjunto de Bloqueado().
    float ChoqueHaceS() { return m_ChoqueHaceS; }
    bool Recuperando() { return m_BogActivo || m_F1Activo || m_ReubPend || m_AR.FisicoEnCurso(); }
    string RecuperandoQue()
    {
        string rq = "";
        if (m_BogActivo) rq = rq + "primera + hillstart";
        if (m_F1Activo) { if (rq != "") rq = rq + " + "; rq = rq + "F1 (reversa y re-encarar)"; }
        if (m_ReubPend) { if (rq != "") rq = rq + " + "; rq = rq + "re-ubicacion pendiente"; }
        if (m_AR.FisicoEnCurso()) { if (rq != "") rq = rq + " + "; rq = rq + "escalera del AR"; }
        return rq;
    }

    // El vector de actuacion ENTERO. Lo que faltaba en los runs anteriores era justamente
    // lo que NO se logueaba: freno, freno de mano, objetivo y tope de traccion. Sin eso
    // "gas 1 y no avanza" admite cuatro explicaciones distintas.
    private string ReporteDeClavada(BZVehicleState st)
    {
        string s = " · fase " + BZMcPhase.Name(m_Maneuver.GetPhase());
        s = s + " · gear " + m_Out.gear.ToString();
        s = s + " · thr " + m_Out.throttle.ToString();
        s = s + " · pedido " + m_ThrottlePedido.ToString();
        s = s + " · brake " + m_Out.brake.ToString();
        s = s + " · hb " + m_Out.handbrake.ToString();
        s = s + " · str " + m_Out.steering.ToString();
        s = s + " · rpm " + st.rpm.ToString();
        s = s + " · kmh " + st.chassisKmh.ToString();
        s = s + " · objetivo " + (m_Goal.speedMs * 3.6).ToString() + "kmh";
        s = s + " · ruedas " + st.wheelsOnGround.ToString() + "/" + st.wheelCount.ToString();
        s = s + " · topeTraccion " + TractionCap(st).ToString();
        s = s + ReporteDeslizamiento(st);
        s = s + " · pitch " + (st.pitch * Math.RAD2DEG).ToString() + "deg";
        s = s + ReporteArranque();
        if (m_Leg && m_Leg.valid)
        {
            s = s + " · wp " + m_Leg.nearWp.ToString();
            s = s + " · lat " + m_Leg.crossTrackM.ToString();
            s = s + " · along " + m_Leg.alongM.ToString();
            s = s + " · reversa " + m_Leg.isReverse.ToString();
        }
        return s;
    }

    // ★ DE DONDE ARRANCA CADA LEG. En el run 09:05 el leg 2 se clavo con lat 11.76 m, y
    // no habia forma de saber si se fue por el camino o si YA ARRANCO lejos -- el leg
    // anterior se habia rendido, asi que el vehiculo empezaba desde donde quedo. Son dos
    // bugs distintos y el log no los distinguia. Una linea por leg lo separa.
    private void ReportarArranqueDeLeg(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid) return;
        if (m_Leg.legIdx == m_LegReportado) return;
        m_LegReportado = m_Leg.legIdx;
        if (s_Quiet) return;
        // ⚠ LA POSE DE ARRANQUE EXACTA, para poder atribuir la variance.
        // Dos corridas identicas dieron 1.34 y 0.353 de lateral medio; si el vehiculo no
        // arranca desde el mismo lugar y con el mismo rumbo, parte de esa diferencia es el
        // punto de partida y no el control. Sin este dato no se puede separar.
        string s = "[BZDrive] LEG " + m_Leg.legIdx.ToString() + " arranca";
        s = s + " · wp " + m_Leg.nearWp.ToString();
        s = s + " · lat " + m_Leg.crossTrackM.ToString();
        s = s + " · kmh " + st.chassisKmh.ToString();
        s = s + " · reversa " + m_Leg.isReverse.ToString();
        s = s + " · pos " + st.pos[0].ToString() + "," + st.pos[2].ToString();
        s = s + " · rumbo " + (st.yaw * Math.RAD2DEG).ToString() + "deg";
        Print(s);
    }

    // Avanzo sobre el ARCO, no en el velocimetro: tironear no es avanzar.
    private bool Avanzo(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid) return true;
        float a = m_Leg.alongM;
        bool av = (a > m_StallArcPrev + STALL_PROGRESS_M);
        if (av) m_StallArcPrev = a;
        if (a < m_StallArcPrev) m_StallArcPrev = a;
        return av;
    }

    string ReporteArranque()
    {
        if (!m_ArranqueActivo) return "";
        string f = "rampa";
        if (m_ArranqueFase == ARR_SUELTO) f = "suelto (agotado)";
        return " · ARRANQUE[" + f + "] tick " + m_ArranqueTicks.ToString();
    }

    bool ArranqueActivo() { return m_ArranqueActivo; }
    bool ArranqueSuelto() { return m_ArranqueFase == ARR_SUELTO; }

    string StallReport()
    {
        if (m_StallTicks <= 0) return "";
        return "[BZDrive] CLAVADO: " + m_StallTicks.ToString() + " ticks a fondo sin avanzar";
    }

    string NoStallReason() { return m_NoStallPorque; }
    bool   StallActive()   { return m_StallTicks > STALL_TICKS; }

    private float DeslizamientoMedido(BZVehicleState st)
    {
        if (!m_Manual) return -1.0;
        return m_Manual.DeslizamientoRatio(st.chassisKmh, st.rpm, st.gear);
    }

    // Las dos velocidades del state son medidas INDEPENDIENTES: kmh es el velocimetro
    // (la RUEDA) y chassisKmh es la velocidad real del cuerpo. El proyecto las separo
    // desde el principio "porque la rueda miente con el wheelspin"... y nunca las comparo.
    // Se imprimen juntas para poder cruzar la medicion por rpm contra la directa.
    private string ReporteDeslizamiento(BZVehicleState st)
    {
        string s = " - rueda " + st.kmh.ToString() + " vs chasis " + st.chassisKmh.ToString();
        s = s + " - desliza " + DeslizamientoMedido(st).ToString();
        s = s + " - sup ";
        if (m_SurfKind == BZSurf.SOFT) s = s + "BLANDA";
        else s = s + "dura";
        return s;
    }

    // Informa donde el motor gira mas de lo que la marcha explica CON la rueda pegada al
    // chasis: eso es deslizamiento de EMBRAGUE, y contradice la creencia congelada de que
    // la transmision de DayZ engancha duro. No manda sobre el gas -- solo mide.
    // ⚠ PLANEAR POR DEBAJO DE LA MORDIDA ES ENTRAR A UN ESTADO SIN SALIDA.
    // Debajo de la velocidad de mordida de la marcha puesta, el motor cae del regimen en
    // que transmite y el vehiculo entra al bog de re-arranque, que esta CARACTERIZADO
    // como irrecuperable: las cinco vias de recuperacion estan falsadas por medicion. No
    // hay fix del lado de la salida; solo lo hay del lado de la entrada.
    //
    // Por eso la regla es: si la ruta adelante pide velocidad y el vehiculo viene
    // PLANEANDO (el modulo solto el gas) hacia la mordida, no se planea -- se sostiene con
    // el piso de gas MEDIDO. No acelera: impide caer al pozo.
    //
    // ✖ ALCANCE: esto NO explica por si solo el derrumbe de wp651 (46.86 -> 0.2 km/h en
    // ~2 s son ~6 m/s2, y ningun planeo de este vehiculo mide mas de 0.8). Si la caja
    // negra muestra que la causa es externa, esta regla no era la palanca y sale.
    // Las TRES curvaturas del lazo lateral, en la misma linea:
    //   PIDE    -- la del camino, medida sobre el ARCO del horizonte (la de a un waypoint
    //              es ruido: alterna de signo casi en cada punto)
    //   COMANDA -- la que pure pursuit le pide a la direccion
    //   LOGRA   -- la que el vehiculo realmente ejecuta, del giro real por metro recorrido
    // PIDE vs COMANDA mide el LAZO; COMANDA vs LOGRA mide el MODELO de direccion (la tabla
    // de transferencia y el subviraje). Sin separarlas no se puede saber cual de las dos
    // esta corta, que es exactamente donde estamos.
    // La ventana de "que pide la ruta adelante" nunca puede ser mas corta que el vehiculo.
    private float VentanaSubvelM()
    {
        if (!m_Manual) return SUBVEL_LOOKAHEAD_M;
        if (m_Manual.wheelbaseM > SUBVEL_LOOKAHEAD_M) return m_Manual.wheelbaseM;
        return SUBVEL_LOOKAHEAD_M;
    }

    // "La ruta ya venia lenta aca": el umbral es el reptado de ESTE vehiculo, no un fijo.
    private float PisoRecKmh()
    {
        if (!m_Manual) return SUBVEL_MIN_REC;
        if (m_Manual.creepKmh > 0.5) return m_Manual.creepKmh;
        return SUBVEL_MIN_REC;
    }

    private void ReportarDireccion(BZVehicleState st, float dt)
    {
        float yawPrev = m_YawPrev;
        m_YawPrev = st.yaw;
        m_DirTicks = m_DirTicks + 1;
        if (s_Quiet || !BZDiag.On()) return;
        if ((m_DirTicks % DIR_CADA) != 0) return;
        if (!m_Leg || !m_Leg.valid || !m_Pursuit) return;
        if (yawPrev < -900.0) return;
        if (dt < 0.01) return;
        // ⚠ EL PISO DE 3 km/h APAGABA EL INSTRUMENTO EXACTAMENTE EN LA VENTANA DEL
        // PROBLEMA. El derrumbe de AERO va de 8.81 a 0.24 km/h en ~1.2 m y la ultima linea
        // que teniamos era la de 8.81: los ultimos 6 metros eran invisibles y por eso
        // llevamos tres reportes discutiendo que pasaba ahi. Se baja al minimo con el que
        // la division por velocidad sigue teniendo sentido.
        if (st.chassisKmh < 0.3) return;
        // ⚠ EL CURSOR NO SE VEIA. En wp 724 el cursor se PEGO --nearWp no avanzaba mientras
        // el lateral crecia de 0.12 a 3.3 m-- y desde afuera eso se lee igual que "el auto
        // no corrige". Son dos cosas distintas: un punto de mira que no avanza deja de
        // describir el camino, y ahi la curvatura comandada ya no significa nada.
        float kPide = m_Pursuit.LastKArco();
        float kLocal = m_Pursuit.LastKLocal();
        float kComanda = m_Goal.curvature;
        float kComandaPrev = m_CmdPrev;
        m_CmdPrev = kComanda;
        float vms = st.chassisKmh / 3.6;
        float kLogra = (BZMath.WrapPi(st.yaw - yawPrev) / dt) / vms;
        string s = "[BZDrive] DIRECCION wp " + m_Leg.nearWp.ToString();
        s = s + " · kmh " + st.chassisKmh.ToString();
        // ⚠ La marcha y el rpm van ACA, no solo en las lineas de evento: esta sale cada
        // pocos ticks y las de `t=` cada ~5 s, asi que el shift quedaba sub-muestreado y
        // no se podia ver cuando cambia de marcha ni en que regimen queda el motor.
        s = s + " · gear " + m_Out.gear.ToString() + " · rpm " + st.rpm.ToString();
        s = s + " · lat " + m_Leg.crossTrackM.ToString();
        s = s + " · Ld " + m_Pursuit.LastLookaheadM().ToString();
        s = s + " · PIDE_local " + kLocal.ToString();
        s = s + " · PIDE_arco " + kPide.ToString();
        // ⚠ EL GIRO QUE ABARCA LA VENTANA, que es lo que decide si el arco DILUYE la curva.
        // Medido en el bus: la ventana de 10.7 m cubre 33.5 grados de giro -- toda la curva
        // MAS recta despues-- y el acorde promedia las dos cosas: PIDE_arco sale 0.0547
        // contra un PIDE_local de 0.0999. Es kArco*Ld, o sea que ya estaba en el log pero
        // habia que reconstruirlo a mano; despues de 13.16 eso es una columna, no una cuenta.
        s = s + " · giro " + (kPide * m_Pursuit.LastLookaheadM() * Math.RAD2DEG).ToString() + "deg";
        s = s + " · COMANDA " + kComanda.ToString();
        // ⚠ LOGRA ES RETROSPECTIVO: sale de (yaw - yawPrev), o sea la respuesta al comando
        // ANTERIOR. Compararlo contra COMANDA del MISMO tick compara la respuesta de un
        // comando contra otro comando distinto, y con el objetivo cambiando cada 0.5 s eso
        // da un deficit que no existe: en las 393 muestras de AERO la razon pasa de mediana
        // 1.14 con 42% por debajo de 0.8 (mismo indice) a mediana 1.49 con solo 6% por
        // debajo de 0.8 (desfasada un tick). Se imprime el comando que LOGRA de verdad
        // responde, para que la razon del log sea la correcta sin tener que corregirla.
        s = s + " · COMANDA_prev " + kComandaPrev.ToString();
        s = s + " · LOGRA " + kLogra.ToString();
        s = s + " · str " + m_Out.steering.ToString();
        // ★★★ EL PEDAL GRABADO, EN LA LINEA QUE SALE POR TICK. La primera version colgo esta
        // columna de la linea de FEATHER, que imprime 3 o 4 veces por corrida: el log no decia
        // "el feedforward no ayudo", decia NADA, y eso es indistinguible de "no existe".
        // Van los TRES datos que deciden si dispara, para que un "no aparecio" quede explicado
        // sin necesitar otra corrida: el pedal grabado aca, el objetivo, y cuanto sumo.
        s = s + " · recThr " + m_Leg.RecThrottleHere().ToString();
        s = s + " · gas " + m_Out.throttle.ToString();
        s = s + " · obj " + (m_Goal.speedMs * 3.6).ToString();
        // ⚠ SE LLAMA ffPedal Y NO ff PORQUE YA HABIA UNA COLUMNA `ff` en esta misma
        // linea (el ffDef del corrector). Dos columnas con el mismo nombre hacen que
        // cualquier grep devuelva la equivocada -- ya paso con "ff+" y el nombre de
        // proxy "offroad" en.
        s = s + " · ffPedal " + m_ThrFFAplicado.ToString();
        s = s + " · ffPedalN " + m_FFTicks.ToString();
        s = s + " · ffVolW " + PesoVolanteGrabado(st.chassisKmh).ToString();
        s = s + " · ffp " + BZPurePursuit.FFPrimario().ToString() + " · corrP " + m_Pursuit.LastFFPCorr().ToString();
        s = s + " · trim " + m_Pursuit.LastFFPTrim().ToString();
        // E2: la deriva a la cinta = EL instrumento; + QUE MANO maneja:
        // con cinta ON se imprime el str QUE LA CINTA APLICO y la rueda grabada que
        // pidio -- el `str` de arriba es el del camino normal (2 Hz) y NO es lo que
        // toca la rueda cuando la cinta esta ON.
        if (m_Cinta && m_Cinta.Valida())
        {
            // ★ QUIEN GANO EL OUTPUT: el CN del run 15-33 probo que con
            // cintaTick el steering aplicado ES cStr (el `str` de esta linea es la
            // OPINION del camino normal a 2 Hz, no lo aplicado) — pero eso hubo que
            // probarlo minando la caja negra. El flag lo dice de una.
            if (m_CintaTick)
            {
                s = s + " · APLICA=cinta";
                if (m_Leg && m_Leg.valid && m_Leg.isReverse) s = s + "R";
                if (m_CintaVol) s = s + "V";
                s = s + " " + m_CintaDist.ToString();
                s = s + " cStr " + m_CintaStr.ToString();
                s = s + " cFw " + m_CintaM.fwDeg.ToString();
                // E3: el error firmado y el sesgo del iman. cLat + = derecha de la
                // linea grabada; cIman = deg SUMADOS a la rueda grabada (±4.5 tope).
                s = s + " cLat " + m_CintaM.latM.ToString();
                s = s + " cPsi " + m_CintaPsi.ToString();
                s = s + " cIman " + m_ImanDeg.ToString();
                // el blanco lateral (≠0 solo con lock largo adelante)
                if (m_CintaRef > 0.01 || m_CintaRef < -0.01) s = s + " cRef " + m_CintaRef.ToString();
                // E3b: la velocidad grabada y el iman de gas/freno — con esto el
                // "¿Boris va a la kmh del usuario?" se lee de la MISMA linea (
                // hubo que reconstruirlo proyectando la ruta sobre la cinta).
                s = s + " cV " + m_CintaM.kmhRec.ToString();
                s = s + " cGas " + m_ImanGas.ToString();
                s = s + " cFrn " + m_ImanFrn.ToString();
            }
            else
            {
                s = s + " · APLICA=fallback cinta OFF(" + m_CintaDist.ToString() + ")";
            }
        }
        s = s + " · recVolDeg " + m_Leg.RecSteerDegHere().ToString();
        // ★ Si el embrague esta en transicion, la correccion esta congelada en ESTE tick.
        // Sin esta columna, "el hErr no crecio" no se distingue de "el congelamiento nunca
        // se activo" -- que es la confusion que ya nos costo una tanda con el pedal.
        s = s + " · emb " + m_Manual.EmbragueEnTransicion(st.chassisKmh, st.rpm, m_Out.gear).ToString();
        // ★ `frz` es lo que REALMENTE congela; `emb` sola se queda encendida con el lugueo.
        // Tenerlas separadas en el log es lo que permite ver la diferencia sin otra corrida.
        s = s + " · frz " + CongelarCorreccion(st).ToString();
        s = s + " · frzT " + m_FrzRestante.ToString();
        s = s + m_Pursuit.DiagLookahead();
        // La sonda, en el log: sin esto no se puede saber si un tick raro fue el FF.
        s = s + " · ff " + m_Pursuit.LastFFDeficit().ToString();
        s = s + " · kmax " + m_Pursuit.KMaxCmd().ToString();
        if (m_Pursuit.KClampMordio()) s = s + " · K-CLAMP";
        if (m_Pursuit.FFCongelado()) s = s + " · FF-OFF(cursor)";
        else if (m_Pursuit.FFEnFinal()) s = s + " · FF-OFF(endpoint)";
        else if (m_Pursuit.FFEnRecta()) s = s + " · FF-OFF(recta)";
        // ⚠ EL ERROR DE RUMBO FALTABA, y sin el la geometria del lazo no se puede
        // reconstruir offline. Con lat (perpendicular), Ld y hErr queda determinada la
        // pose del vehiculo respecto de la traza, que es lo que decide que comanda pure
        // pursuit. Sin hErr solo se puede suponer, y suponer es como se rompio el manejo dos
        // veces. Ver 13.29: la sagita compensa un caso IDEAL (vehiculo SOBRE la traza) que no
        // es el caso real, y distinguirlos exige esta columna.
        s = s + " · hErr " + (m_Leg.headingErrRad * Math.RAD2DEG).ToString() + "deg";
        s = s + " · COMANDA40 " + m_KRefresco.ToString();
        s = s + " · clutch " + st.clutch.ToString() + " · rpm " + st.rpm.ToString();
        s = s + " · cursor " + m_LegMachine.GetCursor().ToString();
        s = s + " · restan " + m_Leg.remainM.ToString() + " m";
        // ⚠ EN GRADOS, como los otros cinco sitios que imprimen pitch. Lo puse crudo en
        // y el 0.3089 (que son 17.7 deg) se leyo como "el camino es plano" --
        // una unidad sin marcar mando el diagnostico a la zanja durante un relay entero.
        s = s + " · gear " + st.gear.ToString();
        s = s + " · pitch " + (st.pitch * Math.RAD2DEG).ToString() + "deg";
        // ⚠⚠ ESTA MARCA EXISTE PORQUE LA LINEA MINTIO Y COSTO UN DIAGNOSTICO ENTERO.
        //
        // Un leg de REVERSA va a modo MANIOBRA (`if (leg.isReverse) return
        // Commit(BZDM_MANEUVER, leg)`), y en ese modo el pursuit NO CORRE: manda
        // `m_Maneuver.Actuate`. Pero esta linea sigue imprimiendo `LastKArco()`,
        // `LastLookaheadM()`, `m_Goal.curvature` y el resto, que conservan el valor del
        // ULTIMO tick en que el pursuit si corrio -- o sea del leg anterior.
        //
        // Leidos de afuera, esos valores constantes se ven como "el feedforward esta
        // CONGELADO en reversa", y ese fue exactamente el diagnostico que salio:
        // COMANDA -0.0373, PIDE_arco -0.0103 y Ld 7.43 sin moverse un tick en todo el leg.
        // No estaban congelados: eran de otro leg. El pursuit no tenia nada que ver.
        //
        // `m_UsoPursuit` ya existia --se resetea cada tick y lo usa el refresco de 40 Hz--
        // asi que la marca no cuesta estado nuevo. Un instrumento que no dice cuando no
        // esta midiendo es peor que no tenerlo.
        if (!m_UsoPursuit) s = s + " · PURSUIT-OFF(los campos del pursuit son del tick anterior)";
        // Con esto, el nivel de `str` deja de deducirse invirtiendo la formula: si alpha es
        // chico y `str` es grande, el volante no viene de la geometria.
        if (m_Pursuit) s = s + " · [" + m_Pursuit.DiagVolante() + "]";
        s = s + SufijoRunner();   // SUFIJO, la linea empieza igual (los greps por inicio siguen)
        Print(s);
        // El objetivo y QUIEN lo puso, en su propia linea para que se pueda grepear sola.
        if (m_Pursuit && m_Leg && m_Leg.valid)
        {
            string so = m_Pursuit.DiagObjetivo(m_Goal.speedMs * 3.6, m_Leg.RecSpeedHere());
            so = so + SufijoRunner();   // SUFIJO
            Print(so);
        }
    }

    private void NoPlanearHastaElBog(BZVehicleState st)
    {
        if (!m_Manual || !m_Leg || !m_Leg.valid) return;
        if (m_Out.brake > 0.02) return;              // frenar a proposito es otra cosa
        if (m_Out.throttle > 0.02) return;           // ya hay gas: no es planeo
        // ⚠ LA REFERENCIA ERA LA MORDIDA Y ESO DESARMABA LA REGLA JUSTO AL LLEGAR.
        //
        // Antes: se sostenia el gas si el vehiculo caia por debajo de la MORDIDA y la ruta
        // pedia mas que la mordida. Medido en AERO: en 1ra la mordida vale 10.5 km/h y la
        // ruta grabada pide 5-7 en los ultimos 11 m -- el humano hizo ese tramo por debajo
        // de la mordida, que un embrague real aguanta. Con `rec (5.19) <= mordida (10.5)`
        // la regla se abstenia, correctamente segun su propio criterio, y el vehiculo se
        // apagaba solo.
        //
        // Ahora la referencia es LO QUE LA RUTA PIDE EN LA POSICION. Es la misma correccion
        // que en el lateral (curvatura local, no promedio del arco): el minimo sobre el
        // horizonte cerca de una parada tiende a cero, asi que como referencia de gas se
        // desarma exactamente donde hace falta.
        //
        // ✖ Y la parada legitima sigue protegida por construccion: al final la ruta pide 0,
        // asi que `recAca` cae solo y la regla se calla sin necesitar una compuerta aparte.
        // ⚠ PONER EL OBJETIVO EN LA MORDIDA NO ALCANZA SI EL GAS NO LA SOSTIENE.
        // Medido: con el objetivo ya en 9.5-11.5 km/h el vehiculo venia a 10.56 con thr 0 y
        // brake 0.014 -- coasteando-- y cruzaba la mordida igual. El planificador declaraba
        // la velocidad; nadie la mantenia. En la rampa final el gas no puede soltarse por
        // debajo de la mordida: abajo de ahi el motor deja de transmitir y no vuelve.
        float recAca = m_Leg.RecSpeedHere();
        if (m_Pursuit && m_Pursuit.EnRampaFinal())
        {
            float mord = m_Pursuit.MordidaKmh();
            // ⚠⚠⚠ ESTO NO ES UNA VELOCIDAD: ES UN CENTINELA, Y ANTES SE LLAMABA `mord * 2.0`.
            //
            // Su unico trabajo es que la guarda de tres lineas mas abajo
            //     if (st.chassisKmh > recAca * SUBVEL_FRAC) return;
            // suelte EXACTAMENTE en la mordida. Con SUBVEL_FRAC = 0.5, `mord * 2` daba
            // `mord * 2 * 0.5 = mord`: correcto, pero por una coincidencia aritmetica que el
            // codigo no declaraba. El `2.0` estaba acoplado en silencio a que la fraccion
            // valiera 0.5; el dia que alguien la moviera, el umbral se iba a otro lado sin que
            // nada lo dijera. Escrito como division, la propiedad es la que se lee.
            //
            // ⚠ Y LE COSTO UN DIAGNOSTICO ENTERO AL OTRO HILO: el `20.2441` salia
            // al log rotulado "la ruta pide", con la ruta guardando 5.7 en ese mismo wp. Se
            // fueron a buscar la inflacion al CSV y al conversor, donde no estaba. El numero
            // era este centinela. La regla de siempre: un instrumento que rotula mal manda a
            // revisar el lugar equivocado, y ese costo lo paga el que viene despues.
            if (mord > 0.5 && recAca > 0.0 && st.chassisKmh < mord) recAca = mord / SUBVEL_FRAC;
        }
        if (recAca < PisoRecKmh()) return;         // la ruta ya venia lenta aca: no se opina
        if (st.chassisKmh > recAca * SUBVEL_FRAC) return;   // sigue cerca de lo grabado
        float rec = recAca;
        float piso = m_Manual.launchThrottleFloor;
        if (piso < 0.01) return;                     // sin piso medido no se inventa uno
        m_Out.throttle = piso;
        // ⚠ SOSTENER ES SOSTENER: sin limpiar el freno que la derivacion dejo puesto,
        // gas y freno alternaban y el vehiculo ciclaba 3.7<->7.5 km/h dando 11 pasitos
        // hacia el cusp (Gemera, RPT del re-test del bundle) -- la mordida armaba pero
        // nunca se sostenia. El freno del cusp lo pone el slam DESPUES, donde toca.
        m_Out.brake = 0;
        m_BiteReportes = m_BiteReportes + 1;
        if (s_Quiet || !BZDiag.On()) return;
        if (m_BiteReportes > 1 && (m_BiteReportes % 8) != 0) return;
        string s = "[BZDrive] MORDIDA #" + m_BiteReportes.ToString();
        s = s + " · planeaba hacia el bog a " + st.chassisKmh.ToString() + " kmh";
        s = s + " · mordida " + m_Manual.SpeedAtBiteKmh(st.chassisKmh, st.rpm, st.gear).ToString();
        // ⚠ Antes decia "la ruta pide", y NO es lo que pide la ruta: cuando la regla esta
        // sosteniendo, `rec` es el centinela (mordida / SUBVEL_FRAC), no la velocidad grabada.
        // Se imprimen las DOS, cada una con su nombre.
        s = s + " · grabado aca " + m_Leg.RecSpeedHere().ToString();
        s = s + " · suelta en " + (rec * SUBVEL_FRAC).ToString() + " kmh";
        s = s + " · se sostiene con el piso medido " + piso.ToString();
        s = s + " · gear " + st.gear.ToString() + " · rpm " + st.rpm.ToString();
        s = s + " · wp " + m_Leg.nearWp.ToString();
        s = s + " · [" + m_Manual.DiagBite(st.chassisKmh, st.rpm, st.gear) + "]";
        Print(s);
    }

    private void DetectarDeslizamiento(BZVehicleState st)
    {
        float pat = DeslizamientoMedido(st);
        if (pat <= DESLIZ_UMBRAL || m_ThrottlePedido < 0.2)
        {
            m_PatinajeReportes = 0;
            return;
        }
        m_PatinajeReportes++;
        if (s_Quiet || !BZDiag.On()) return;
        if (m_PatinajeReportes > 1 && (m_PatinajeReportes % 4) != 0) return;
        string s = "[BZDrive] DESLIZA #" + m_PatinajeReportes.ToString();
        s = s + ReporteDeslizamiento(st);
        s = s + " - rpm " + st.rpm.ToString() + " - gear " + st.gear.ToString();
        s = s + " - thr " + m_Out.throttle.ToString() + " - pedido " + m_ThrottlePedido.ToString();
        s = s + " - tope " + TractionCap(st).ToString();
        s = s + " - pitch " + (st.pitch * Math.RAD2DEG).ToString() + "deg";
        s = s + " - ruedas " + st.wheelsOnGround.ToString() + "/" + st.wheelCount.ToString();
        if (m_Leg && m_Leg.valid) s = s + " - wp " + m_Leg.nearWp.ToString() + " - lat " + m_Leg.crossTrackM.ToString();
        s = s + " - pos " + st.pos[0].ToString() + "," + st.pos[2].ToString();
        Print(s);
    }

    // ★★★ FEEDFORWARD DEL PEDAL GRABADO (capa 3).
    //
    // El dato ya venia: las tres tomas de supercar traen hasInputData=1 y el pedal en la
    // columna 8, y el loader ya lo expone. Lo que faltaba era USARLO.
    //
    // ⚠ SIN GANANCIA NUEVA. No es `rec + k*(error)`: es `max(propio, grabado)` con una
    // compuerta. Donde el humano iba a fondo, Boris va a fondo; donde el humano aflojo, manda
    // el controlador de siempre. No hay constante que calibrar.
    //
    // ⚠⚠ LA COMPUERTA ES LO QUE IMPIDE QUE EL FEEDFORWARD PISE LOS CAPS. Solo se aplica si
    // Boris esta POR DEBAJO de la velocidad objetivo -- y ese objetivo ya trae adentro el cap
    // de curva, el aLat y la mordida. Asi el pedal grabado solo puede ayudarlo a ALCANZAR el
    // objetivo, nunca a pasarlo. Sin esta compuerta, `max` con un grabado de 1.0 mandaria
    // fondo en plena curva por encima de todo lo que el perfil de velocidad decidio.
    //
    // ★ Y es "ALCANZAR", no "reproducir": donde el humano aflojo (grabado 0) pero Boris viene
    // atrasado, el `max` deja pasar el pedido MAYOR del controlador. Reproducir el aflojo
    // seria reproducir el atraso en vez de cerrarlo.
    private void AplicarPedalGrabado(BZVehicleState st)
    {
        if (!m_UsoPursuit) return;
        if (!m_Leg) return;
        if (!m_Goal) return;
        float rec = m_Leg.RecThrottleHere();
        float objetivoKmh = m_Goal.speedMs * 3.6;
        if (objetivoKmh <= st.chassisKmh) return;    // ya esta en el objetivo o encima
        // ⚠ ACA MURIO UNA GUARDA REDUNDANTE. Habia un `if (rec < 0) return` para las rutas
        // sin pedal, y el espejo mostro que NO cambiaba nada: con rec = -1 esta misma linea
        // ya devuelve. La seguridad de la flota numerada no viene de un gate que podria
        // fallar --viene de la ESTRUCTURA: el maximo con un valor negativo es el valor
        // propio, siempre. Dejar la guarda sugeriria que la proteccion vive ahi.
        if (rec <= m_Out.throttle) return;           // el controlador ya pide al menos eso
        m_ThrFFAplicado = rec - m_Out.throttle;
        m_FFTicks = m_FFTicks + 1;
        m_Out.throttle = rec;
    }

    private float TractionCap(BZVehicleState st)
    {
        float cap = 1.0;
        if (st.wheelCount > 0 && st.wheelsOnGround < st.wheelCount)
        {
            cap = st.wheelsOnGround * 1.0 / st.wheelCount;
            if (st.wheelsOnGround <= 0) cap = TRACTION_MIN;
            if (cap < TRACTION_MIN) cap = TRACTION_MIN;
        }
        // ✖ EL CORTE POR RPM SE BORRO, FALSADO POR SU PROPIO CRITERIO.
        // Se recortaba el gas cuando el motor giraba mas rapido de lo que la marcha y la
        // velocidad explicaban, leyendo eso como rueda patinando. La medicion DIRECTA que
        // se agrego en la misma corrida lo desmintio: rueda 47.72 vs chasis 47.73, rueda
        // 3.17 vs chasis 3.21, rueda 1.46 vs chasis 1.47 -- la rueda NUNCA giro mas rapido
        // que el chasis. El exceso de vueltas es del EMBRAGUE, no de la rueda, y recortar
        // el gas por eso solo frena al vehiculo donde mas falta le hace.
        // No queda gateado a wheelspin real: no habria disparado nunca. Se borra.
        // El instrumento SI queda (ReporteDeslizamiento): fue lo que permitio falsarlo.
        return cap;
    }

    // (A2): la escala de autoridad es "desde que velocidad el vehiculo RUEDA": lo MEDIDO
    // (idleCreepKmh), no el objetivo de arrastre. Con idle 0 (no repta) vale su default de 4,0 km/h.
    private float EscalaAutoridadKmh()
    {
        float creep = 4.0;
        if (m_Manual && m_Manual.idleCreepKmh > 0.5) creep = m_Manual.idleCreepKmh;
        return creep;
    }

    private float SteerAuthority(BZVehicleState st)
    {
        float creep = EscalaAutoridadKmh();
        float f = st.chassisKmh / creep;
        if (f < 0) f = 0;
        if (f > 1.0) f = 1.0;
        return STEER_AUTH_MIN + (1.0 - STEER_AUTH_MIN) * f;
    }

    // HANDBRAKE FIEL (SEAM.md 6): la UNICA linea del sistema que lo escribe. Solo el
    // grabado; ningun modulo puede inyectar handbrake propio -- eso era legacy del
    // Stanley viejo y esta prohibido por contrato.
    private float HandbrakeFiel()
    {
        if (!m_Route) return 0;
        if (!m_Leg.valid) return 0;
        return m_Route.Handbrake(m_Leg.nearWp);
    }

    // ★★★ SI ENGANCHA POR CERCANIA, SE SUELTA POR LEJANIA. La asimetria era el agujero:
    // la meseta engancha cuando el vehiculo se detuvo cerca de su entrada pero solo sale por
    // tiempo cumplido. Si el vehiculo se VA de la zona, la meseta sigue en curso desde un punto donde ya no
    // esta: la cinta le pide 0 desde metros adelante, el sosten lo frena donde no corresponde, y el AR queda
    // gateado por EnMeseta(). Medido: el coche 106 termino 15,8 m DETRAS de su meseta, 90 s, sin que nada
    // lo mirara; y Svergino resiste a los tres cambios anteriores (6 abandonos en dos corridas, ).
    //
    // LA DISTANCIA ES LA MISMA QUE LA DEL ENGANCHE (AlcanceMesetaM): si a esa distancia se considera que el
    // vehiculo llego a la meseta, a esa distancia deja de estar en ella. Cero constantes nuevas.
    //
    // A QUIEN SE LO ENTREGA, que es lo que hay que decir explicitamente: a la cadena NORMAL. SoltarCinta-
    // HastaFinDeMeseta marca la meseta CUMPLIDA (no se re-engancha: la parada ya se sirvio, el vehiculo
    // estuvo detenido ahi) y suelta la cinta con re-armado por posicion; desde ese momento manda C1 y el AR
    // vuelve a estar habilitado, porque EnMeseta() pasa a false. No se inventa ningun rescate nuevo.
    private void SoltarMesetaSiSeAlejo(BZVehicleState st)
    {
        if (!m_Cinta || !m_Cinta.Valida()) return;
        if (!m_CintaRodo) return;
        if (!m_Cinta.EnMeseta()) return;
        vector pMes = m_Cinta.MesetaPos();
        float dxMes = st.pos[0] - pMes[0];
        float dzMes = st.pos[2] - pMes[2];
        float dMes = Math.Sqrt(dxMes * dxMes + dzMes * dzMes);
        float alcMes = m_Cinta.AlcanceMesetaM();
        if (dMes <= alcMes) return;
        if (!s_Quiet)
        {
            string ml = "[Cinta] MESETA ABANDONADA: el vehiculo esta a " + dMes.ToString() + " m de su entrada";
            ml = ml + " (alcance " + alcMes.ToString() + " m) · la parada no se puede servir desde aca";
            ml = ml + " -> suelto la cinta y sigue la cadena normal (C1 + AR habilitado)";
            ml = ml + SufijoRunner();
            Print(ml);
        }
        SoltarCintaHastaFinDeMeseta(st, "FUERA DE LA ZONA DE LA MESETA (a " + dMes.ToString() + " m)");
    }

    // ★★★ UNA DETENCION IMPUESTA SE SOSTIENE. Mientras la meseta esta en curso, el que decide
    // que el vehiculo se queda quieto es el FRAMEWORK (cuenta los frames grabados y sale solo), no la fisica.
    // Reproducir el freno grabado no alcanza: ese freno es el de un vehiculo que NO estaba detenido ahi.
    // MEDIDO en la toma, parada de SVERGINO (ROUTE2_A_FULL, filas 22778-22889 = 112 frames = 2,8 s):
    //    filas 22773-22777   freno 1,00   el que grabo frena de 5,0 a 1,5 km/h
    //    filas 22778-22889   freno 0,00 · gas 0,00   LA MESETA: solto todo
    //    la cuesta sube 1,18 m en 33 m (+2,3 grados)
    // El humano paso casi sin detenerse, asi que nunca tuvo que sostener nada. El bus se queda los 112 frames
    // y en ese rato RUEDA HACIA ATRAS: medido en la B, el coche 106 termino 15,8 m detras de la traza (90 s
    // clavado) y el 107 oscilo 67 s. El cursor de arco no retrocede, asi que la cinta le sigue pidiendo 0
    // desde 15 m adelante y nadie lo saca.
    // ⚠ FRENO DE SERVICIO, NO DE MANO: el handbrake tiene un solo escritor por contrato (HandbrakeFiel, que
    // replica lo grabado) y ningun modulo puede inyectarlo. El hold de la API si lo usa, pero eso es del runner.
    // ⚠ Cede ante una recuperacion en curso, como todo lo demas.
    private void SostenerMeseta(BZVehicleState st)
    {
        if (!m_Cinta || !m_Cinta.Valida()) return;
        if (!m_CintaRodo) return;
        if (!m_Cinta.EnMeseta()) return;
        if (m_BogActivo || m_F1Activo) return;
        m_Out.throttle = 0;
        m_Out.brake = 1.0;
    }

    // ★★★ EL DIAGNOSTICO DEL NO-RESCATE. Cuatro episodios en dos
    // noches con el mismo sintoma: el vehiculo no avanza, ningun mecanismo actua y NINGUNO DEJA RASTRO. El AR
    // no dice por que decide que no; el enganche de la meseta ni siquiera se llama si su firma no da verde.
    // Esto imprime UNA linea con el estado de cada guarda, y solo cuando ese estado CAMBIA: sin reloj nuevo,
    // sin spam, y una sola linea si la situacion se queda quieta. No cambia ninguna decision.
    // Cubre las dos formas de no avanzar:
    //    DETENIDO        m_QuietoT (reposo propio, maneje quien maneje)
    //    SIN PROGRESAR   el arco de la cinta no avanza aunque el vehiculo se mueva -- el vehiculo vencido por
    //                    una cuesta oscila y NUNCA acumula reposo, que es como se escapo el coche 107.
    private void DiagnosticarSinRescate(BZVehicleState st, float dt)
    {
        if (!m_Cinta || !m_Cinta.Valida() || !m_Leg || !m_Leg.valid)
        {
            m_DiagSinRescate = "";
            return;
        }
        float sAhora = m_Cinta.SEst();
        if (Math.AbsFloat(sAhora - m_SEstProgPrev) < 0.01) m_SinProgresoT = m_SinProgresoT + dt;
        else m_SinProgresoT = 0;
        m_SEstProgPrev = sAhora;
        // el umbral NO es el reposo de una parada (0,5 s), que es lo normal en
        // una ruta con paradas: es el que el framework ya usa para decir "esto no avanza" -- ABANDONO_TICKS por
        // el dt real (20 x 0,5 = 10 s a 2 Hz). Con 0,5 s esto imprimio 120 lineas en 15 min, casi todas de buses
        // parados donde tenian que estar, y el analista tuvo que separarlas a mano.
        // ⚠ EL MODO NO SE FILTRA, SE INFORMA: el caso de PAVLOVO vive en modo 4 (la maniobra del cusp), y ahi
        // el modulo de maniobra tiene su propio abandono -- si a los 10 s no resolvio, eso ES para reportar.
        float topeDiag = ABANDONO_TICKS * dt;
        bool quieto = (m_QuietoT >= topeDiag);
        bool trabado = (m_SinProgresoT >= topeDiag);
        // la tercera forma de no avanzar, y la que se nos escapaba: IR PARA ATRAS. El arco de la
        // cinta no la delata (la odometria no tiene signo), asi que sin esto el diagnostico no ve al vehiculo
        // que la cuesta empuja hacia abajo.
        bool retroD = (m_RetrocedeT >= topeDiag);
        if (!quieto && !trabado && !retroD)
        {
            m_DiagSinRescate = "";
            return;
        }
        if (m_Leg.nearWp <= 0)
        {
            m_DiagSinRescate = "";   // todavia no largo: un bus esperando en su cabecera no es un no-rescate
            return;
        }
        // si alguien esta actuando, no hay nada que diagnosticar
        if (m_BogActivo || m_F1Activo || m_AR.FisicoEnCurso() || m_Cinta.EnMeseta())
        {
            m_DiagSinRescate = "";
            return;
        }
        // ⚠ LA FIRMA ES CUALITATIVA. Con los relojes adentro cambiaba cada 0,1 s y repetia el mismo episodio
        // decenas de veces: el anti-spam no servia de nada. Los segundos van en el TEXTO, no en la firma.
        float mesAd = m_Cinta.InicioMesetaAdelante();
        string fir = "";
        fir = fir + "posee " + m_CintaRodo.ToString();
        fir = fir + " modo " + m_Mode.ToString();
        // EL wp NO VA EN LA FIRMA. Con el vehiculo REPTANDO (0,1-1,0 km/h) el wp cambia de
        // a uno, cada linea tiene firma distinta y el anti-spam no colapsa nada: 26 lineas en 36 s. El wp es
        // POSICION, no estado -- va en el texto, que es donde se lee. La firma queda cualitativa de verdad.
        if (retroD) fir = fir + " retrocediendo";
        if (mesAd >= 0) fir = fir + " conMeseta";
        else fir = fir + " sinMeseta";
        fir = fir + " " + m_AR.PorQueNo();
        if (fir == m_DiagSinRescate) return;
        m_DiagSinRescate = fir;
        string dg = "[C12] SIN RESCATE: el vehiculo no avanza y nadie actua";
        dg = dg + " · v " + st.chassisKmh.ToString() + " km/h";
        dg = dg + " · quieto " + m_QuietoT.ToString() + " s · sin progresar " + m_SinProgresoT.ToString() + " s";
        if (m_RetrocedeT > 0) dg = dg + " · RETROCEDIENDO " + m_RetrocedeT.ToString() + " s a " + st.fwdKmh.ToString() + " km/h";
        dg = dg + " · wp " + m_Leg.nearWp.ToString() + " leg " + m_Leg.legIdx.ToString();
        dg = dg + " · GUARDAS: cinta posee " + m_CintaRodo.ToString();
        dg = dg + " · modo " + m_Mode.ToString() + " (CRUISE es " + BZDriveMode.BZDM_CRUISE.ToString() + ")";
        dg = dg + " · reposo de cinta " + m_CintaReposoT.ToString() + " s (hace falta " + m_AR.WaitS().ToString() + ")";
        dg = dg + " · cinta v " + m_CintaM.kmhRec.ToString() + " freno " + m_CintaM.brk.ToString() + " gas " + m_CintaM.thr.ToString();
        if (mesAd >= 0)
        {
            float dMes = mesAd - m_Cinta.SEst();
            dg = dg + " · meseta a " + dMes.ToString() + " m POR DELANTE DEL CURSOR (alcance " + m_Cinta.AlcanceMesetaM().ToString() + ")";
        }
        else
        {
            dg = dg + " · SIN meseta por delante del cursor dentro del alcance";
        }
        dg = dg + " · " + m_AR.PorQueNo();
        dg = dg + SufijoRunner();
        Print(dg);
    }

    // Cusp MECANICO -> encadena solo, sin avisarle a nadie (es geometria).
    // Parada SEMANTICA o fin de ruta -> handshake: emite y espera Resume().
    // TRABA POR PROGRESO. Un vehiculo que EMPUJA un obstaculo no esta "quieto": lee 0,3-0,7 km/h
    // (la velocidad leida no es movimiento, ). MEDIDO (BusDrive 23/09, coche 101): 70 s a gas 1 contra un bus
    // estacionado en la terminal, rpm ~977, 0,3-0,5 km/h; el CLAVADO lo veia "avanzando" (raspaba 2 wp cada 5 s), el AR
    // no-fisico exige reposo (< 0,3) y CRUISE (era modo ENDPOINT), el arbitro no disparaba (el cursor avanzaba). Lo unico
    // que lo vio fue m_SinProgresoT (el cursor de la cinta < 1 cm por tick): 69,5 s. Esa es la firma: sin progresar el
    // tope del CLAVADO (10 s) CON gas pedido a fondo -- sin gas no es una traba, es una espera (meseta, hold, cesion de
    // paso: en los RPT del 22-23/09 las unicas otras 'sin progresar >= 10 s' son las mesetas de cabecera, con gas 0 y
    // freno 1). Abre la MISMA escalera fisica que el CLAVADO (reversa y re-encarar; re-ubicar; zona segura), que al
    // abrirse avisa BLOQUEADO. Una vez por traba: se re-arma cuando vuelve a progresar.
    private void DetectarTrabaPorProgreso(BZVehicleState st, float dt)
    {
        if (m_SinProgresoT < 0.01) m_TrabaAvisada = false;
        if (m_TrabaAvisada) return;
        if (!m_Cinta || !m_Cinta.Valida() || !m_Leg || !m_Leg.valid) return;
        if (m_Leg.nearWp <= 0) return;   // todavia no largo: un bus esperando en su cabecera no es una traba
        if (m_SinProgresoT < ABANDONO_TICKS * dt) return;
        if (m_ThrottlePedido < STALL_FULL_THR) return;
        // el EPISODIO de ahogo no tapa la traba; la tapa un INTENTO en curso (m_BogActivo), que
        // si es dueño del vehiculo (su gas y su freno son la prueba). Con m_BogEpisodio aca el 105 quedo 15-20,5 s "sin
        // progresar" a gas pedido 1 con el diagnostico SIN RESCATE publicando cada 0,5 s y nadie abria la escalera: el
        // episodio (8 m o cambio de leg) sobrevive a los intentos y al veredicto.
        if (m_BogActivo || m_F1Activo || m_F1GraciaT > 0 || m_ReubPend) return;
        if (m_AR.FisicoEnCurso() || m_AR.FisicoAgotado() || !m_AR.FisicoHabilitado()) return;
        if (m_Cinta.EnMeseta()) return;
        m_TrabaAvisada = true;
        string tp = "TRABADO POR PROGRESO: sin progresar " + m_SinProgresoT.ToString() + " s a gas pedido " + m_ThrottlePedido.ToString();
        tp = tp + " con v " + st.chassisKmh.ToString() + " km/h (la velocidad leida no es movimiento)";
        ClasificarFisico(st, tp);
    }

    // (BusDrive 2.2, la meseta de cabecera): EL CUSP LO CLAVA LA CINTA. En un cusp con parada grabada la cinta
    // posee la reversa y frena donde freno el que grabo; el vehiculo queda donde la fisica lo deja, unos decimetros a un lado
    // u otro de la pose final del leg. MEDIDO en 63 salidas de Terminal Este (ROUTE2_A_FULL wp 55 -> 57, 21-23/09): en 43 el
    // bus PASO la pose (-0,5 .. -1,5 m) y Closed() cerro al instante (4,2 s de detencion total); en 19 quedo CORTO 0,11 .. 0,35 m,
    // fuera de los 10 cm de LLEGADA_M, y la maniobra pedia gas contra el freno grabado (la cinta posee: el freno gana) hasta
    // que el give-up de no-progreso cerro el leg a los 11 s -- 15 s detenido, contado como rendicion. Con la cinta poseyendo,
    // detenida en una fila que FRENA y el cursor al final del leg, la parada del que grabo YA esta reproducida: eso es llegar.
    // Sin cinta (el harness, o una toma sin _cinta.csv) no cambia nada: la pose sigue mandando.
    private bool CintaClavoElCusp(BZVehicleState st)
    {
        if (!m_CintaRodo || !m_Cinta || !m_Cinta.Valida() || !m_CintaM) return false;
        if (!m_Leg || !m_Leg.valid || !m_Manual) return false;
        if (m_CintaReposoT < m_AR.WaitS()) return false;
        // solo en la ULTIMA parada grabada de la fase (ver BZCintaRunner.ParadaDeFase): una parada anterior se cumple
        // (su meseta) y el replay sigue. Sin parada grabada en la fase, el freno grabado decide como siempre.
        int pfC = m_Cinta.ParadaDeFase(m_Leg.isReverse);
        if (pfC == 2) return false;
        if (pfC == 0 && m_CintaM.brk <= 0.5) return false;
        if (m_Leg.remainM > m_Maneuver.CreepZone(m_Manual)) return false;
        return true;
    }

    // ★ EL FRENO AL FIN DEL LEG. La cinta replaya pedales por ARCO: si el casco llega mas rapido que la toma a la parada de
    // un cusp, las filas de freno a fondo no alcanzan (v^2 - 2 a ds deja un resto) y lo que sigue es el arrastre del que
    // grabo (freno 0, 1-2 km/h en marcha) o nada (el clamp de borde: la fase se agoto): nadie frena, y la maniobra cierra el
    // leg RODANDO y pasado. Medido (PRUEBA_01, leg 2): llegada a las filas de arrastre a 4,8 km/h, s_est clavado en el
    // borde 1,2 s con freno grabado 0,03, leg cerrado a -0,55 / -0,64 m a 2,4 km/h; y una parada grabada de 14 cuadros
    // en 1,6 cm de arco que el cursor salto en un paso (a 4,8 km/h avanza 3,3 cm por consulta: la meseta nunca engancho).
    // Con el cursor atrasado de antes el casco leia el freno a fondo tarde, justo donde estaba, y paraba a +0,09 por
    // casualidad. Aca: cuando la toma en el cursor ya no rueda (v grabada < FIN_LEG_ARRASTRE_KMH) o se agoto (borde) y el
    // casco si rueda, el freno por cuadro apunta a la pose final del leg con la ley del endpoint (v^2/2d contra el freno
    // del endpoint, techado por la toma) como PISO sobre el freno grabado: nunca le saca freno a la toma, y lejos de la
    // pose vale ~0. Solo en legs intermedios (el ultimo es del ENDPOINT, que ya tiene el suyo) y sin F1, re-ubicacion
    // ni ahogo en curso. Con la toma rodando no actua: la toma trae su propio freno.
    private const float FIN_LEG_APUNTA_M = 0.05;      // apunta 5 cm antes de la pose: el cierre estricto es a 10
    private const float FIN_LEG_ARRASTRE_KMH = 3.0;   // v grabada por debajo = el que grabo ya estaba parando o arrastrando
    // SOLO EN EL ULTIMO TRAMO DEL LEG. remFr es la proyeccion sobre la tangente FINAL del leg: vale en los ultimos metros y
    // en un leg curvo, lejos, es basura (medido: -127 m al arrancar un leg de 487 m; -8 m al arrancar otro). Las filas
    // lentas del arranque de cualquier toma (< 3 km/h) cumplian la condicion de arrastre al pasar el casco los 2 km/h y el
    // piso pedia freno 1 con ese remFr: un tiron de un cuadro, que en una cuesta podria repetirse. La ventana: el
    // remanente POR ARCO de la compuerta del endpoint (m_RemGate) dentro de la distancia de frenado x el margen + una
    // batalla, y nunca mas de FIN_LEG_ATRAS_M pasado de la pose.
    private const float FIN_LEG_MARGEN = 1.25;
    private const float FIN_LEG_ATRAS_M = 1.0;
    private int  m_FrenoFinLegLeg = -1;               // el leg donde el freno al fin del leg actuo (para aceptar su clavado)
    private float m_FrenoFinB;                         // el pedido del piso en este cuadro (0 = no actuo): columna frenoFin
    private int  m_FrenoFinDicho = -1;                // el leg del que ya salio la linea [C12] FRENO AL FIN DEL LEG
    private void FrenoAlFinDelLeg(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid || !m_Manual || !m_Route || !m_CintaM) return;
        if (m_Leg.legIdx >= m_Route.LegCount() - 1) return;
        if (m_Mode == BZDriveMode.BZDM_ENDPOINT || m_F1Activo || m_BogActivo || m_ReubPend) return;
        if (!m_CintaM.borde && m_CintaM.kmhRec >= FIN_LEG_ARRASTRE_KMH) return;
        float kmhAbs = Math.AbsFloat(st.chassisKmh);
        if (kmhAbs < CINTA_RODANDO_KMH) return;   // ya casi quieto: la fila grabada y la meseta mandan
        float remFrF = RemanenteFrameM(st);
        if (remFrF < -FIN_LEG_ATRAS_M) return;    // mas de 1 m pasado: eso no es el final del leg, es remFr sin sentido
        float v = kmhAbs / 3.6;
        float aMax = m_Manual.MaxBrakeDecelManiobra(m_SurfKind);   // la frenada medida de ESTE cusp (legs intermedios)
        if (aMax < 0.5) aMax = 0.5;
        float wbF = m_Manual.wheelbaseM;
        if (wbF < 2.5) wbF = 2.5;
        float ventanaF = (v * v) / (2.0 * aMax) * FIN_LEG_MARGEN + wbF;
        if (m_RemGate > ventanaF) return;         // lejos del final del leg por arco: afuera
        float d = remFrF - FIN_LEG_APUNTA_M;
        float b = 1.0;
        if (d > 0.01) b = Math.Clamp((v * v) / (2.0 * d) / aMax, 0.0, 1.0);
        if (b < 0.05) return;   // lejos de la pose: nada que frenar todavia
        m_FrenoFinLegLeg = m_Leg.legIdx;
        m_FrenoFinB = b;
        // una linea por leg, la primera vez que actua: con el freno grabado ya a fondo no cambia nada y no se veria
        if (!s_Quiet && m_FrenoFinDicho != m_Leg.legIdx)
        {
            m_FrenoFinDicho = m_Leg.legIdx;
            string ff = "[C12] FRENO AL FIN DEL LEG " + m_Leg.legIdx.ToString() + ": la toma en el cursor a " + m_CintaM.kmhRec.ToString() + " km/h";
            if (m_CintaM.borde) ff = ff + " (borde: la fase se agoto)";
            ff = ff + " · casco " + kmhAbs.ToString() + " km/h · remFr " + remFrF.ToString() + " m · rem " + m_RemGate.ToString() + " m";
            ff = ff + " · freno grabado " + m_Out.brake.ToString() + " -> pedido " + b.ToString();
            Print(ff + SufijoRunner());
        }
        m_Out.throttle = 0;
        if (b > m_Out.brake) m_Out.brake = b;
    }

    // el clavado del freno al fin del leg se acepta como el de la cinta: detenido el tiempo del AR, al alcance de la pose
    private bool FrenoFinLegClavo(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid || !m_Manual) return false;
        if (m_FrenoFinLegLeg != m_Leg.legIdx) return false;
        if (m_CintaReposoT < m_AR.WaitS()) return false;
        if (m_Cinta && m_Cinta.Valida() && m_Cinta.ParadaDeFase(m_Leg.isReverse) == 2) return false;   // quedan filas
        if (m_Leg.remainM > m_Maneuver.CreepZone(m_Manual)) return false;
        return true;
    }

    // UNA LINEA POR CIERRE DE LEG. m_CierrePorQue se borra al avanzar de leg y solo llegaba al FIN
    // (Completa12): en un leg intermedio (el cusp de Terminal Este) el motivo y el remFr del 946 no se imprimian NUNCA, y
    // BusDrive casi nunca llega a un FIN (retira el runner en la terminal). Medido: 0 lineas 'cierre:' en una tanda con
    // 13 cierres de leg. Falsador del 946 en la linea: los cerrados 'por la cinta' con remFr en +0,10..+0,40; ninguno > 1 m.
    private void ReportarCierreDeLeg()
    {
        if (s_Quiet || !m_Leg || !m_Leg.valid) return;
        float remFrC = (m_EndPos[0] - m_UltimoPos[0]) * m_EndTanX + (m_EndPos[2] - m_UltimoPos[2]) * m_EndTanZ;
        // el lateral en el mismo marco (+ = a la izquierda de la linea de llegada, como latFr en la caja)
        float latC = (m_EndPos[0] - m_UltimoPos[0]) * m_EndTanZ - (m_EndPos[2] - m_UltimoPos[2]) * m_EndTanX;
        string motivoC = m_CierrePorQue;
        if (motivoC == "") motivoC = "sin motivo registrado";
        if (m_Maneuver.GaveUp()) motivoC = motivoC + " (RENDICION de la maniobra)";
        string lc = "[C12] LEG " + m_Leg.legIdx.ToString() + " cerrado · " + motivoC;
        lc = lc + " · remFr " + remFrC.ToString() + " m · lat " + latC.ToString() + " m · rem " + m_Leg.remainM.ToString() + " m";
        lc = lc + " · kmh " + m_UltimoKmh.ToString() + " · wp " + m_Leg.nearWp.ToString();
        // la pose del leg es un wp del conversor (medido: 0,4 m antes de la parada real en una toma, 0,45 despues en otra,
        // segun donde cayo el muestreo y si el que grabo se arrastro despues de parar): la referencia fiel es la ULTIMA
        // PARADA GRABADA de la fase
        if (m_Cinta && m_Cinta.Valida())
        {
            int cuadroP = -1;
            float dParada = m_Cinta.DistUltimaParadaDeFase(m_UltimoPos, m_Leg.isReverse, cuadroP);
            if (cuadroP >= 0) lc = lc + " · la ultima parada grabada de la fase (cuadro " + cuadroP.ToString() + ") queda a " + dParada.ToString() + " m";
        }
        Print(lc + SufijoRunner());
    }

    private void OnLegDone()
    {
        RecordClose();
        AnotarRendicion();
        ReportarCierreDeLeg();
        if (m_Leg.endIsStop)
        {
            if (m_CierrePorQue == "") m_CierrePorQue = "parada semantica al fin del leg";
            OnArrived();
            return;
        }
        if (!m_LegMachine.AdvanceLeg())
        {
            if (m_CierrePorQue == "") m_CierrePorQue = "no hay mas legs por delante";
            else m_CierrePorQue = m_CierrePorQue + " · y era el ULTIMO leg";
            OnArrived();
            return;
        }
        m_CierrePorQue = "";   // el leg avanzo: el motivo era de ESE leg, no del cierre de la ruta
        m_Maneuver.Reset();
        m_Slam.Reset();
    }

    // ⚠ COMPLETE Y "SE RINDIO" NO SON LO MISMO (run de la tesis: el ultimo leg cerro
    // 42.8 m corto por falta de progreso y esto reportaba ROUTE_COMPLETE).
    // La salida por no-progreso es CORRECTA -- evita el deadlock -- pero cerrar sin llegar
    // NO es llegar, y el reporte no puede taparlo. La regla de la casa es gritar en la
    // falla; un COMPLETE mentiroso es exactamente lo contrario.
    private void OnArrived()
    {
        RecordClose();
        // ⚠⚠ ESTA VIA NO PASA POR OnLegDone. En modo ENDPOINT el Sample llama a OnArrived()
        // DIRECTO ("if (m_Slam.IsLatched()) OnArrived();"), asi que la anotacion que puse en
        // OnLegDone no corria nunca ahi: el slam se rendia a 0.1048 m, ponia su m_GaveUp, y
        // el veredicto no lo veia. Medido: ROUTE_COMPLETE con el auto 4.8 mm fuera del bar.
        AnotarRendicion();
        // ⚠ UN LEG ABANDONADO TAMBIEN ES RENDIRSE. Llamar COMPLETE a una ruta donde un tramo
        // entero no se manejo seria mentir, que es lo que 8.3 prohibe.
        //
        // ⚠⚠⚠ Y LA LLEGADA SE MIDE, NO SE DEDUCE. Las dos clausulas de arriba preguntan por el
        // ESTADO DE LOS MODULOS ("alguno se rindio?"), y ese estado se puede perder en un
        // camino que nadie miro -- que es exactamente lo que acaba de pasar. La distancia
        // final es el hecho: se compara contra la tolerancia y listo. Ninguna via de cierre
        // puede esconderla, y ademas atrapa el sobrepaso (cerrar 50 cm PASADO tampoco es
        // llegar, y antes era invisible porque `signed` negativo no cuenta como rendicion).
        float fin = DistanciaFinalM();
        // la distancia final del cierre se compara contra la RED (1,0 m): mas alla es rendicion. El veredicto lo decide el
        // REPOSO contra el bar estricto (DecidirFin): la red existe para que el slam no se quede buscando, no para aprobar.
        bool fuera = (fin > BZDriveTol.ACEPTA_ENDPOINT_RED_M);
        if (fuera && m_MotivoRendicion == "")
        {
            string sf = "leg ";
            sf = sf + m_Leg.legIdx.ToString() + ": quedo a " + fin.ToString() + " m";
            sf = sf + " con tolerancia " + BZDriveTol.ACEPTA_ENDPOINT_RED_M.ToString() + " m";
            m_MotivoRendicion = sf;
        }
        bool seRindio = (m_LegsRendidos > 0) || (m_LegsAbandonados > 0) || fuera;
        m_Arbiter.NotifyArrived();
        m_Mode = BZDriveMode.BZDM_HALTED;
        bool ultimo = (m_Leg.legIdx >= m_Route.LegCount() - 1);
        if (!ultimo)
        {
            Emit(BZDriveEv.ARRIVED, m_Leg.legHi);
            return;
        }
        // ★ EL VEREDICTO SALE DEL REPOSO, NO DE ACA. La compuerta de llegada se abre con el casco todavia rodando: medido,
        // una ruta salia COMPLETE con el auto en reposo a 1,02 m de la pose final (aprobada por la red) y otra a 0,27 m
        // tambien. Desde aca la ruta queda en HALTED (freno) y la cola del log mide donde quedo de verdad (0,5 s quieto,
        // tope LOG_COLA_TOPE_S); el evento ROUTE_COMPLETE/INCOMPLETE lo emite DecidirFin al terminar esa cola, con o sin
        // caja negra. Lo que ya sabemos de la ruta (legs rendidos o abandonados, la red) viaja en m_FinRendido.
        m_FinRendido = seRindio;
        m_FinWp = m_Leg.legHi;
        m_FinPendiente = true;
        CerrarLog();
    }

    // ★ EL VEREDICTO, EN REPOSO. ROUTE_COMPLETE exige las tres: (1) el casco quieto a <= ACEPTA_ENDPOINT_M de la pose
    // final (la medida de la linea REPOSO: longitudinal y lateral contra la tangente de llegada); (2) ningun *** CHOQUE ***
    // contra un OBJETO en la corrida (un bache, o un impacto sin entidad reportada, no cuenta); (3) ninguna accion FISICA
    // del AR (maniobra, re-ubicacion). Los re-arranques NO-FISICOS no invalidan: quedan listados en el FIN. Si falla
    // cualquiera, ROUTE_INCOMPLETE con cada motivo (a cuanto quedo / contra que choco y en que wp / el AR fisico y su wp).
    // La medida entra en el motivo de cierre, asi el texto del FIN (y RunnerEndReason) la lleva adentro.
    private void DecidirFin(float tR, float totR, float lonR, float latR)
    {
        m_FinPendiente = false;
        m_FinDecidido = true;
        m_ReposoFinM = totR;
        string rp = "REPOSO tras " + tR.ToString() + " s: a " + totR.ToString() + " m de la pose final (longitudinal " + lonR.ToString() + " m, lateral " + latR.ToString() + " m)";
        if (m_CierrePorQue == "") m_CierrePorQue = rp;
        else m_CierrePorQue = m_CierrePorQue + " · " + rp;
        bool lejos = (totR > BZDriveTol.ACEPTA_ENDPOINT_M);
        int nFisAcc = m_AR.NFisicosAcc();
        bool ok = (!m_FinRendido && !lejos && m_ChoquesObjeto == 0 && nFisAcc == 0);
        if (!ok)
        {
            string mot = MotivoRendicion();
            if (lejos) mot = UnirMotivo(mot, "quedo a " + totR.ToString() + " m de la pose final en reposo con tolerancia " + BZDriveTol.ACEPTA_ENDPOINT_M.ToString() + " m");
            if (m_ChoquesObjeto > 0) mot = UnirMotivo(mot, "choco contra un objeto: " + ListaChoquesObjeto());
            if (nFisAcc > 0) mot = UnirMotivo(mot, "AR fisico con accion: " + m_AR.ListaFisAcc());
            m_MotivoRendicion = mot;
            Emit(BZDriveEv.ROUTE_INCOMPLETE, m_FinWp);
            return;
        }
        Emit(BZDriveEv.ROUTE_COMPLETE, m_FinWp);
    }

    private string UnirMotivo(string base, string mas)
    {
        if (base == "") return mas;
        return base + " · " + mas;
    }

    private void AnotarChoqueObjeto(int wp, string clase)
    {
        if (!m_ChObjWp)
        {
            m_ChObjWp = new array<int>();
            m_ChObjClase = new array<string>();
            m_ChObjN = new array<int>();
        }
        int i;
        for (i = 0; i < m_ChObjWp.Count(); i++)
        {
            if (m_ChObjWp.Get(i) == wp && m_ChObjClase.Get(i) == clase)
            {
                m_ChObjN.Set(i, m_ChObjN.Get(i) + 1);
                return;
            }
        }
        m_ChObjWp.Insert(wp);
        m_ChObjClase.Insert(clase);
        m_ChObjN.Insert(1);
    }

    private string ListaChoquesObjeto()
    {
        if (!m_ChObjWp) return "";
        string s = "";
        int i;
        for (i = 0; i < m_ChObjWp.Count(); i++)
        {
            if (s != "") s = s + ", ";
            s = s + "wp " + m_ChObjWp.Get(i).ToString() + " contra " + m_ChObjClase.Get(i);
            if (m_ChObjN.Get(i) > 1) s = s + " x" + m_ChObjN.Get(i).ToString();
        }
        return s;
    }

    // la frenada del cusp de ESTE leg al manual (ver MaxBrakeDecelManiobra): corre al arrancar cada leg, con la pose final
    // ya cacheada. Una linea por leg dice con que numero dosifican la maniobra y el freno al fin del leg.
    private void AcoplarFrenoCusp()
    {
        m_FrenoCuspLeg = m_Leg.legIdx;
        float v0C = 0;
        float dsC = 0;
        int nC = 0;
        float aC = -1;
        if (m_Cinta && m_Cinta.Valida()) aC = m_Cinta.FrenadaAntesDePoseMS2(m_EndPos, v0C, dsC, nC);
        m_FrenoCuspMs2 = aC;
        m_Manual.SetFrenoCusp(aC);
        if (s_Quiet) return;
        string fc = "[C12] FRENADA del cusp del leg " + m_Leg.legIdx.ToString() + ": ";
        if (aC > 0) fc = fc + aC.ToString() + " m/s2 (pedal a fondo desde " + v0C.ToString() + " km/h en " + dsC.ToString() + " m, " + nC.ToString() + " cuadros)";
        else fc = fc + "sin medicion";
        fc = fc + " · la maniobra y el freno al fin del leg dosifican con " + m_Manual.MaxBrakeDecelManiobra(m_SurfKind).ToString() + " m/s2";
        fc = fc + " (fisico " + m_Manual.MaxBrakeDecel(m_SurfKind).ToString() + ")";
        Print(fc + SufijoRunner());
    }

    // la frenada final de la toma al manual, para el endpoint (ver MaxBrakeDecelEndpoint). Corre cuando la cinta ya
    // esta armada; una linea por ruta dice con que numero planea el endpoint.
    private void AcoplarFrenoToma()
    {
        if (!m_Manual || !m_Cinta || !m_Cinta.Valida()) return;
        m_FrenoTomaAcoplado = true;
        float aT = m_Cinta.FrenadaFinalMS2();
        m_Manual.SetFrenoToma(aT);
        if (s_Quiet) return;
        string ft = "[C12] FRENADA de la toma: ";
        if (aT > 0)
        {
            ft = ft + aT.ToString() + " m/s2 (pedal a fondo desde " + m_Cinta.FrenadaFinalV0Kmh().ToString() + " km/h en ";
            ft = ft + m_Cinta.FrenadaFinalDsM().ToString() + " m)";
        }
        else ft = ft + "sin medicion (la toma no termina con el pedal a fondo desde 8 km/h o mas)";
        ft = ft + " · el endpoint planea y dosifica con " + m_Manual.MaxBrakeDecelEndpoint(m_SurfKind).ToString() + " m/s2";
        ft = ft + " (fisico del manual " + m_Manual.MaxBrakeDecel(m_SurfKind).ToString() + ")";
        Print(ft + SufijoRunner());
    }

    // la medida del veredicto (m); -1 hasta que el REPOSO la decide (o si la ruta termino por otra via: STUCK, abort)
    float ReposoFinM() { return m_ReposoFinM; }

    // La distancia REAL al ultimo waypoint: el mismo par que RecordClose congela. Su hipotenusa
    // es el numero que el runner imprime como ENDPOINT, calculado por otra via (posicion del
    // vehiculo contra el waypoint) -- que las dos den lo mismo es el chequeo cruzado.
    private float DistanciaFinalM()
    {
        float a = m_LastCloseAlongM;
        float l = m_LastCloseLatM;
        return Math.Sqrt(a * a + l * l);
    }

    // ⚠ EL TRAMO FINAL SE MEDIA SOLO CADA 10 TICKS, y ahi se decide todo el endpoint: entre
    // dos muestras el auto pasa de 0.61 km/h a parado. A 40 Hz y acotado al modo ENDPOINT
    // --que dura segundos-- el volumen es chico y la deceleracion final queda legible.
    // ⚠ EL MODELO DE PLANEO Y LA REALIDAD, EN LA MISMA LINEA.
    // Con `aFree` solo no se puede saber si el modelo acierta: hizo falta reconstruir a
    // mano la desaceleracion real entre muestras del log para descubrir que el prior se
    // queda 23%, 45% y 86% corto a medida que baja la velocidad (freno-motor sub-mordida).
    // Eso tiene que ser una COLUMNA, no una reconstruccion posterior: aReal sale de la
    // velocidad entre dos ticks, que es la medicion mas directa que hay.
    private void ReportarSlam(BZVehicleState st, float dt)
    {
        float vPrev = m_SlamPrevV;
        float vNow = st.chassisKmh / 3.6;
        m_SlamPrevV = vNow;
        if (s_Quiet || !BZDiag.On()) return;
        if (m_SlamLogTicks > SLAM_LOG_MAX) return;
        m_SlamLogTicks = m_SlamLogTicks + 1;
        string r = m_Slam.ReporteLongitudinal();
        if (r == "") return;
        string s = "[BZDrive] SLAM";
        if (m_Leg && m_Leg.valid) s = s + " wp " + m_Leg.nearWp.ToString();
        s = s + " · " + r;
        s = s + " · kmh " + st.chassisKmh.ToString();
        s = s + " · thr " + m_Out.throttle.ToString();
        s = s + " · brk " + m_Out.brake.ToString();
        s = s + " · rpm " + st.rpm.ToString();
        s = s + " · lat " + m_Leg.crossTrackM.ToString();
        if (dt > 0.001 && vPrev > 0.0001)
        {
            float aReal = (vNow - vPrev) / dt;
            s = s + " · aReal " + aReal.ToString();
        }
        Print(s);
    }

    // ⚠ SE ANOTA EL PRIMERO, NO EL ULTIMO. Si dos legs se rinden, el que explica la ruta es
    // el primero que fallo -- los de despues pueden ser consecuencia de arrastrar ese error.
    private void AnotarRendicion()
    {
        if (!m_Leg || !m_Leg.valid) return;
        if (m_RendicionAnotadaLeg == m_Leg.legIdx) return;
        bool mc = m_Maneuver.GaveUp();
        bool sl = m_Slam.GaveUp();
        if (!mc && !sl) return;
        // RED DE SEGURIDAD: el slam que se rinde fuera del bar estricto (0,25 m) pero dentro de ACEPTA_ENDPOINT_RED_M no se
        // rindio: llego con aviso, y el veredicto lo da el REPOSO contra el bar estricto (DecidirFin). Un servicio con el vehiculo congelado y el chofer adentro hasta que alguien lo pare es
        // peor que 40 cm. Se imprime la distancia real y la ruta sigue su ciclo.
        if (sl && !mc && m_Slam.GaveUpDistM() <= BZDriveTol.ACEPTA_ENDPOINT_RED_M)
        {
            m_RendicionAnotadaLeg = m_Leg.legIdx;
            if (!s_Quiet) Print("[C12] LLEGADA fuera de la tolerancia estricta: " + m_Slam.GaveUpDistM().ToString() + " m (" + BZDriveTol.ACEPTA_ENDPOINT_M.ToString() + " m): se acepta hasta " + BZDriveTol.ACEPTA_ENDPOINT_RED_M.ToString() + " m: la ruta sigue y el veredicto lo decide el REPOSO" + SufijoRunner());
            return;
        }
        m_RendicionAnotadaLeg = m_Leg.legIdx;
        m_LegsRendidos = m_LegsRendidos + 1;
        if (m_MotivoRendicion != "") return;
        string q = "la maniobra cerro";
        float d = m_Maneuver.GaveUpSignedM();
        float tolAn = BZDriveTol.LLEGADA_M;
        if (sl)
        {
            q = "el slam clavo";
            d = m_Slam.GaveUpDistM();
            tolAn = BZDriveTol.ACEPTA_ENDPOINT_M;   // el slam se rinde contra la ACEPTACION
        }
        string s = "leg ";
        s = s + m_Leg.legIdx.ToString() + ": " + q + " a " + d.ToString() + " m";
        s = s + " con tolerancia " + tolAn.ToString() + " m";
        m_MotivoRendicion = s;
    }

    // Por que la ruta no se declara completa, con el numero al lado. Vacio = no se rindio
    // ningun leg (si el veredicto igual es INCOMPLETE, fue por un leg ABANDONADO).
    // por que se cerro la ruta, para que el FIN no haya que deducirlo del RPT.
    // el gas que este cerebro APLICO en el ultimo Plan. El heartbeat mostraba el cache del
    // servicio, que el applier del 12 no escribe: marcaba thr=0 con el vehiculo a 57 km/h.
    float GasAplicado()
    {
        return m_ThrAplicado;
    }

    // ⚠ SUFIJO, no prefijo: los greps de los dos hilos buscan por el principio de la linea
    // (`[BZDrive] ARRANQUE:`, `[C12] SIN RESCATE:`). La identidad va AL FINAL, como los campos nuevos
    // de la tarjeta del panel, para que nada de lo que ya se mide se rompa.
    private string SufijoRunner()
    {
        if (m_EtiqRunner == "") return "";
        return " · runner " + m_EtiqRunner;
    }

    // el runner ponia "escalera del AR agotada o desactivada" fijo; ahora pregunta.
    string MotivoStuck()
    {
        if (m_MotivoStuck == "") return "sin motivo registrado (revisar: todo STUCK deberia dejar uno)";
        return m_MotivoStuck;
    }

    string MotivoCierre()
    {
        if (m_CierrePorQue == "") return "sin motivo registrado (revisar: todo cierre deberia dejar uno)";
        return m_CierrePorQue;
    }

    string MotivoRendicion()
    {
        if (m_MotivoRendicion != "") return m_MotivoRendicion;
        if (m_LegsAbandonados > 0) return "legs abandonados: " + m_LegsAbandonados.ToString();
        return "";
    }

    // Congela el error de pose del cierre. signedToEndM = error a lo largo del camino,
    // crossTrackM = error lateral. Los dos juntos son la precision real del cierre.
    private void RecordClose()
    {
        if (!m_Leg.valid) return;
        if (m_LastClosedLeg == m_Leg.legIdx) return;
        m_LastClosedLeg = m_Leg.legIdx;
        m_LastCloseAlongM = m_Leg.signedToEndM;
        m_LastCloseLatM = m_Leg.crossTrackM;
        if (!s_Quiet) Print(ReporteDeSeguimiento());
        ReiniciarSeguimiento();
    }

    // Una muestra por tick de Plan(): es un muestreo UNIFORME del leg, no el sesgado que
    // sale de los logs de diagnostico (que solo hablan cuando hay problema).
    private void AcumularSeguimiento(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid) return;
        // ⚠ UN VEHICULO QUIETO NO ESTA SIGUIENDO LA TRAZA. Sus muestras dicen donde quedo,
        // no que tan bien maneja, y un clavado de 60 s aporta cientos de ellas: el tramo
        // wp600-699 dio 2.32 en una corrida y 0.267 en la otra, y la diferencia era donde
        // quedo sentado. Se descartan, y se informa CUANTAS para que el silencio no pase
        // por buen seguimiento.
        if (st.chassisKmh < UmbralCasiParado())
        {
            m_SegDescartadas++;
            return;
        }
        float l = m_Leg.crossTrackM;
        if (l < 0) l = -l;
        if (l > 500.0) return;
        // El transitorio de adquisicion no es seguimiento: el vehiculo viene de la pose de
        // spawn y esta enganchando la traza.
        if (m_SegAncla < 0) m_SegAncla = m_Leg.nearWp;
        if (m_Leg.nearWp - m_SegAncla < SEG_ADQUISICION_WPS) return;
        int tr = m_Leg.nearWp / SEG_TRAMO_WPS;
        if (tr >= 0 && tr < SEG_TRAMOS_MAX && m_SegTrSum)
        {
            m_SegTrSum.Set(tr, m_SegTrSum.Get(tr) + l);
            m_SegTrCnt.Set(tr, m_SegTrCnt.Get(tr) + 1);
            if (l > m_SegTrMax.Get(tr)) m_SegTrMax.Set(tr, l);
        }
        m_SegN++;
        if (m_SegWpIni < 0) m_SegWpIni = m_Leg.nearWp;
        m_SegWpFin = m_Leg.nearWp;
        m_SegSum = m_SegSum + l;
        if (l > m_SegMax) m_SegMax = l;
        if (m_SegMuestras && m_SegMuestras.Count() < 4000) m_SegMuestras.Insert(l);
    }

    private void ReiniciarSeguimiento()
    {
        m_SegN = 0;
        m_SegDescartadas = 0;
        m_SegSum = 0;
        m_SegMax = 0;
        m_SegWpIni = -1;
        m_SegWpFin = 0;
        m_SegAncla = -1;
        int tz;
        if (m_SegTrSum)
        {
            for (tz = 0; tz < SEG_TRAMOS_MAX; tz++)
            {
                m_SegTrSum.Set(tz, 0);
                m_SegTrCnt.Set(tz, 0);
                m_SegTrMax.Set(tz, 0);
            }
        }
        if (m_SegMuestras) m_SegMuestras.Clear();
    }

    // p90 sobre las muestras del leg. La MEDIA sola miente cuando hay un tramo malo corto,
    // y el MAXIMO solo cuenta el peor tick; los tres juntos describen el seguimiento.
    private string ReporteDeSeguimiento()
    {
        if (m_SegN < 1) return "[BZDrive] SEGUIMIENTO leg " + m_Leg.legIdx.ToString() + ": sin muestras";
        float media = m_SegSum / m_SegN;
        // ✖ DETECTOR DE DIVERGENCIA POR TENDENCIA: PROBADO CONTRA LOS DATOS Y DESCARTADO.
        //
        // Se propuso una pendiente de minimos cuadrados de |lat| a lo largo del leg, para
        // cazar la espiral lenta que el chequeo de cambio de signo no ve. Contrastado contra
        // las cuatro corridas reales --dos sanas y dos que diverguieron-- NO SEPARA:
        //     sedan sano  -0.010    sedan roto (18.35 m)  -0.005
        //     bus   sano  +0.057    bus   roto ( 8.09 m)  -0.093
        // Las rotas dan tendencia NEGATIVA porque la divergencia no es un crecimiento
        // monotono: es una excursion en la curva y despues recupera, asi que un ajuste lineal
        // sobre el leg entero promedia a cero. Tampoco sirve la racha creciente mas larga
        // (6/7/9/6, no discrimina), y el crecimiento dentro de la racha resulta ser el `max`
        // con pasos de mas (0.573/17.507/2.206/7.017 contra max 0.586/18.349/2.225/8.087).
        //
        // EL DETECTOR ES EL `max` QUE YA ESTABA: sedan 0.59 sano contra 18.35 roto (31x),
        // bus 2.23 contra 8.09 (3.6x). Agregar una metrica que no supera a la existente solo
        // suma ruido con apariencia de rigor.
        float p90 = -1.0;
        if (m_SegMuestras && m_SegMuestras.Count() > 4)
        {
            int n = m_SegMuestras.Count();
            int i;
            int j;
            for (i = 1; i < n; i++)
            {
                float v = m_SegMuestras.Get(i);
                j = i - 1;
                while (j >= 0 && m_SegMuestras.Get(j) > v)
                {
                    m_SegMuestras.Set(j + 1, m_SegMuestras.Get(j));
                    j--;
                }
                m_SegMuestras.Set(j + 1, v);
            }
            int idx = (int)(n * 0.90);
            if (idx > n - 1) idx = n - 1;
            p90 = m_SegMuestras.Get(idx);
        }
        string s = "[BZDrive] SEGUIMIENTO leg " + m_Leg.legIdx.ToString();
        s = s + ": lateral medio " + media.ToString();
        s = s + " · p90 " + p90.ToString();
        s = s + " · max " + m_SegMax.ToString();
        if (m_SegDescartadas > 0) s = s + " · " + m_SegDescartadas.ToString() + " descartadas por vehiculo quieto";
        s = s + " · " + m_SegN.ToString() + " muestras · wp " + m_SegWpIni.ToString() + "-" + m_SegWpFin.ToString();
        s = s + " · CIERRE along " + m_LastCloseAlongM.ToString() + " lat " + m_LastCloseLatM.ToString();
        // Y el desglose por tramo fijo: es lo unico comparable entre dos corridas.
        s = s + "\n[BZDrive] SEGUIMIENTO por tramo (wp de a " + SEG_TRAMO_WPS.ToString() + ", sin los primeros " + SEG_ADQUISICION_WPS.ToString() + " de enganche):";
        int t;
        for (t = 0; t < SEG_TRAMOS_MAX; t++)
        {
            if (!m_SegTrCnt || m_SegTrCnt.Get(t) < 1) continue;
            s = s + "\n    wp " + (t * SEG_TRAMO_WPS).ToString() + "-" + ((t + 1) * SEG_TRAMO_WPS - 1).ToString();
            s = s + ": medio " + (m_SegTrSum.Get(t) / m_SegTrCnt.Get(t)).ToString();
            s = s + " · max " + m_SegTrMax.Get(t).ToString();
            s = s + " · n " + m_SegTrCnt.Get(t).ToString();
        }
        return s;
    }

    private void CacheEndPose()
    {
        if (!m_Route) return;
        if (!m_Leg.valid) return;
        m_EndPos = m_Route.Pos(m_Leg.legHi);
        float th = m_Route.Head(m_Leg.legHi);
        // EN REVERSA, LA TANGENTE DE ADENTRO DEL LEG (la misma que BZLegMachine.SignedToEnd). El ultimo waypoint de una
        // ruta que termina marcha atras suele ser "normal" (la toma mete 1a ya parada): su rumbo es hacia donde MIRA el
        // vehiculo, opuesto a la marcha. remFr salia NEGATIVO todo el leg: EndpointFrameBrake leia "pasado" y frenaba a
        // fondo al entrar al endpoint (TRUCK_130_01 clavado a 3,69 m del final), y el descuento por avance de la compuerta
        // (m_RemGate) no actuaba. Solo en ese caso (leg de reversa cuyo ultimo waypoint NO es de reversa): las rutas que
        // terminan de cola con el ultimo waypoint en reversa (PATRULLA, SUZUKI, OffroadHB02/03...) ya tenian el rumbo de la
        // marcha y no cambian; marcha adelante tampoco.
        if (m_Leg.isReverse && !m_Route.IsRev(m_Leg.legHi))
        {
            int tIn = m_Leg.legHi - 1;
            if (tIn >= 0 && m_Route.LegOf(tIn) == m_Route.LegOf(m_Leg.legHi)) th = m_Route.Head(tIn);
        }
        m_EndTanX = Math.Sin(th);
        m_EndTanZ = Math.Cos(th);
    }

    // ======================= SAMPLE (~40 Hz) =======================

    // Devuelve el comando del ultimo Plan() con EXACTAMENTE dos refinamientos de frame,
    // declarados en el contrato: freno de endpoint y slew de volante. Nada mas.
    // Si algun dia hace falta un lazo rapido de direccion, entra ACA con su rate-limiter
    // -- no como un segundo tick que pisa el cache del lento, que es como nacio el bug
    // RAD/DEG de v1.1.
    // ⚠ UNA FRENADA IMPOSIBLE NO ES UNA FRENADA: ES UN CHOQUE.
    // Ningun neumatico transmite mas de ~1 g contra el piso, asi que una desaceleracion muy
    // por encima de eso no puede venir de frenar ni de la superficie -- viene de pegarle a
    // algo. Medido en wp661: -152 m/s2 (15 g) en un frame, con las cuatro ruedas apoyadas.
    //
    // ✖ Y VA EN Sample(), NO EN Plan(). El evento dura 25 ms; a 2 Hz no existe. Esa
    // ceguera nos costo cuatro rondas llamandole bog de re-arranque a un choque: el bog
    // era real, pero era la CONSECUENCIA de quedar trabado contra algo, no la causa.
    private void DetectarChoque(BZVehicleState st, float dt)
    {
        float kmhPrev = m_ChoqueKmhPrev;
        m_ChoqueKmhPrev = st.chassisKmh;
        // las ruedas en el suelo de cada cuadro: la linea dice si venia en el aire (un salto) o apoyado. Solo instrumento
        if (!m_HistRuedas) m_HistRuedas = new BZHistRuedas();
        m_HistRuedas.Anotar(st.wheelsOnGround, dt);
        // los relojes corren SIEMPRE (antes de los return): el del ultimo choque de la linea y el del episodio.
        if (dt > 0 && m_ChoqueHaceS >= 0) m_ChoqueHaceS = m_ChoqueHaceS + dt;
        if (dt > 0 && m_ChoqueEpN > 0)
        {
            m_ChoqueEpHaceS = m_ChoqueEpHaceS + dt;
            m_ChoqueEpDurS = m_ChoqueEpDurS + dt;
            if (m_ChoqueEpHaceS > CHOQUE_EPISODIO_S) CerrarEpisodioDeChoque();
        }
        if (dt < 0.005) return;
        if (dt > 0.2) return;
        if (kmhPrev < 1.0) return;                   // casi parado: no hay energia que perder
        float a = ((st.chassisKmh - kmhPrev) / 3.6) / dt;
        if (a > 0) return;
        float techo = CHOQUE_PISO_G;
        if (m_Manual)
        {
            float frenoMax = m_Manual.MaxBrakeDecel(m_SurfKind);
            if (frenoMax > techo) techo = frenoMax;
        }
        float imposible = techo * CHOQUE_MARGEN;
        if (-a < imposible) return;
        // sin contacto del motor Y poca velocidad perdida = terreno, no obstaculo (ver CHOQUE_BACHE_KMH)
        bool conContactoB = (st.contactoHaceS >= 0 && st.contactoHaceS <= CHOQUE_CONTACTO_S);
        bool bacheB = (!conContactoB && kmhPrev - st.chassisKmh < CHOQUE_BACHE_KMH);
        if (!bacheB) m_ChoqueHaceS = 0;   // la definicion de la LINEA, para RunnerChoqueHaceS; se arma aunque no se imprima
        // UNA LINEA POR EPISODIO. Un impacto a menos de CHOQUE_EPISODIO_S del anterior es el MISMO
        // episodio: se cuenta y no se imprime; al cerrarse (5 s sin impactos) sale el resumen si hubo mas de uno. Sin tope de por vida.
        if (m_ChoqueEpN > 0)
        {
            m_ChoqueEpN = m_ChoqueEpN + 1;
            m_ChoqueEpHaceS = 0;
            if (st.chassisKmh < m_ChoqueEpKmhMin) m_ChoqueEpKmhMin = st.chassisKmh;
            if (!bacheB) m_ChoqueEpChoques = m_ChoqueEpChoques + 1;
            return;
        }
        int nB = 0;
        string cabB = "CHOQUE";
        if (bacheB)
        {
            m_Baches = m_Baches + 1;
            nB = m_Baches;
            cabB = "BACHE";
        }
        else
        {
            m_Choques = m_Choques + 1;
            nB = m_Choques;
            // contra un OBJETO (entidad reportada por el motor) cuenta para el veredicto; sin entidad es el terreno
            if (conContactoB && st.contactoClase != "")
            {
                m_ChoquesObjeto = m_ChoquesObjeto + 1;
                int wpCO = -1;
                if (m_Leg && m_Leg.valid) wpCO = m_Leg.nearWp;
                AnotarChoqueObjeto(wpCO, st.contactoClase);
            }
        }
        m_ChoqueEpN = 1;
        m_ChoqueEpHaceS = 0;
        m_ChoqueEpDurS = 0;
        m_ChoqueEpKmhMin = st.chassisKmh;
        m_ChoqueEpCab = cabB;
        m_ChoqueEpNum = nB;
        m_ChoqueEpChoques = 0;
        if (!bacheB) m_ChoqueEpChoques = 1;
        if (s_Quiet) return;
        string s = "[BZDrive] *** " + cabB + " #" + nB.ToString() + " ***";
        s = s + " · " + kmhPrev.ToString() + " -> " + st.chassisKmh.ToString() + " kmh en " + dt.ToString() + " s";
        s = s + " · " + a.ToString() + " m/s2 (el techo del freno aca es " + imposible.ToString() + ")";
        s = s + " · thr " + m_Out.throttle.ToString() + " · brake " + m_Out.brake.ToString();
        s = s + " · ruedas " + st.wheelsOnGround.ToString() + "/" + st.wheelCount.ToString();
        s = s + " · pitch " + (st.pitch * Math.RAD2DEG).ToString() + "deg";
        s = s + " · pos " + st.pos[0].ToString() + "," + st.pos[2].ToString();
        if (m_Leg && m_Leg.valid)
        {
            s = s + " · wp " + m_Leg.nearWp.ToString();
            s = s + " · lat " + m_Leg.crossTrackM.ToString();
        }
        if (bacheB) s = s + " · NINGUN neumatico frena asi, pero no hay contacto y la perdida es chica: es el TERRENO, no un obstaculo.";
        else        s = s + " · NINGUN neumatico frena asi: el vehiculo le pego a algo.";
        // hasta hoy la linea terminaba aca y habia que ir a mirar in-game contra QUE pego (el Landrover
        // estacionado de PATRULLA se descubrio asi). El motor ya lo sabe: CarScript.OnContact trae la entidad.
        // La AUSENCIA de contacto tambien es dato: si el motor no reporto ninguno, no le pego a una entidad.
        // El contacto de mayor impulso de la ventana y la zona del casco que toco. Con que: la clase del objeto, "modelo X"
        // si es un objeto del mapa sin clase, o TERRENO cuando el motor lo reporta con `other` nulo (el suelo del mapa no
        // es una entidad).
        if (st.contactoHaceS >= 0 && st.contactoHaceS <= CHOQUE_CONTACTO_S)
        {
            string conC = st.contactoClase;
            if (conC == "") conC = "terreno (other nulo)";
            s = s + " · CONTRA " + conC + " (impulso " + st.contactoImp.ToString() + ", hace " + st.contactoHaceS.ToString() + " s, zona " + st.contactoZona + ")";
        }
        else
        {
            s = s + " · SIN CONTACTO reportado por el motor en " + CHOQUE_CONTACTO_S.ToString() + " s: no le pego a una entidad";
        }
        // las ruedas en el suelo en los cuadros de antes: en el aire y despues el golpe = un aterrizaje
        s = s + " · ruedas en los " + CHOQUE_RUEDAS_S.ToString() + " s previos: " + m_HistRuedas.Resumen(CHOQUE_RUEDAS_S, st.wheelCount);
        s = s + SufijoRunner();   // un CHOQUE sin runner se atribuye a ojo (por wp y posicion)
        Print(s);
        if (!bacheB && m_Choques == 1) VolcarAnillo("CHOQUE");   // la caja negra no se gasta en un bache
    }

    // el resumen del episodio, cuando pasan CHOQUE_EPISODIO_S sin impactos. Con un solo impacto no hay nada que resumir.
    private void CerrarEpisodioDeChoque()
    {
        if (m_ChoqueEpN > 1 && !s_Quiet)
        {
            int bachesEp = m_ChoqueEpN - m_ChoqueEpChoques;
            float durEp = m_ChoqueEpDurS - m_ChoqueEpHaceS;
            string rs = "[BZDrive] *** " + m_ChoqueEpCab + " #" + m_ChoqueEpNum.ToString() + " *** episodio cerrado: " + m_ChoqueEpN.ToString() + " impactos";
            rs = rs + " (" + m_ChoqueEpChoques.ToString() + " choques, " + bachesEp.ToString() + " baches) en " + durEp.ToString() + " s";
            rs = rs + " · minimo " + m_ChoqueEpKmhMin.ToString() + " km/h · sin impactos hace " + CHOQUE_EPISODIO_S.ToString() + " s" + SufijoRunner();
            Print(rs);
        }
        m_ChoqueEpN = 0;
        m_ChoqueEpHaceS = -1;
        m_ChoqueEpDurS = 0;
        m_ChoqueEpChoques = 0;
    }

    private void GrabarAnillo(BZVehicleState st, float dt)
    {
        if (!m_RKmh) return;
        if (dt > 0) m_RTiempo = m_RTiempo + dt;
        m_RKmh.Set(m_RIdx, st.chassisKmh);
        m_RThr.Set(m_RIdx, m_Out.throttle);
        m_RBrk.Set(m_RIdx, m_Out.brake);
        m_RStr.Set(m_RIdx, m_Out.steering);
        m_RRpm.Set(m_RIdx, st.rpm);
        m_RPitch.Set(m_RIdx, st.pitch * Math.RAD2DEG);
        m_RX.Set(m_RIdx, st.pos[0]);
        m_RZ.Set(m_RIdx, st.pos[2]);
        m_RT.Set(m_RIdx, m_RTiempo);
        m_RGear.Set(m_RIdx, st.gear);
        m_RRuedas.Set(m_RIdx, st.wheelsOnGround);
        m_RClutch.Set(m_RIdx, st.clutch);
        // ⚠ EL LAT SE MIDE ACA, NO SE COPIA DE Plan(). El volcado del choque venia con el
        // lateral refrescado 21 veces mas lento que la posicion, y por eso subestimaba la
        // apertura hasta 0.59 m justo en la entrada de la curva -- que es el instante que
        // habia que ver. Un instrumento con un campo a otra frecuencia miente sin avisar.
        float lat = 0;
        if (m_Leg && m_Leg.valid)
        {
            lat = m_Leg.crossTrackM;
            if (m_LegMachine) lat = m_LegMachine.CrossTrackAhora(st.pos);
        }
        m_RLat.Set(m_RIdx, lat);
        float ld = 0;
        float ka = 0;
        if (m_Pursuit)
        {
            ld = m_Pursuit.LastLookaheadM();
            ka = m_Pursuit.LastKArco();
        }
        m_RLd.Set(m_RIdx, ld);
        m_RkArco.Set(m_RIdx, ka);
        m_RIdx = m_RIdx + 1;
        if (m_RIdx >= RING_N) m_RIdx = 0;
        if (m_RCant < RING_N) m_RCant = m_RCant + 1;
    }

    // Se vuelca cuando algo YA salio mal. El disparador no es un umbral nuevo: es la
    // condicion de subvelocidad que ya existia y ya estaba calibrada. Lo que aporta el
    // anillo es lo de ANTES, que es justamente lo que no se podia ver.
    private void VolcarAnillo(string motivo)
    {
        if (s_Quiet || !BZDiag.On()) return;
        if (!m_RKmh || m_RCant < 2) return;
        if (m_Volcados >= VOLCADOS_MAX) return;
        m_Volcados = m_Volcados + 1;
        Print("[BZDrive] ===== CAJA NEGRA (" + motivo + ") · " + m_RCant.ToString() + " muestras a ~40 Hz, de la mas vieja a la mas nueva =====");
        int i;
        int k;
        int desde = m_RIdx;
        if (m_RCant < RING_N) desde = 0;
        for (i = 0; i < m_RCant; i++)
        {
            k = desde + i;
            if (k >= RING_N) k = k - RING_N;
            string ln = "[BZDrive] CN t " + m_RT.Get(k).ToString();
            ln = ln + " · kmh " + m_RKmh.Get(k).ToString();
            ln = ln + " · thr " + m_RThr.Get(k).ToString();
            ln = ln + " · brk " + m_RBrk.Get(k).ToString();
            ln = ln + " · str " + m_RStr.Get(k).ToString();
            ln = ln + " · gear " + m_RGear.Get(k).ToString();
            ln = ln + " · rpm " + m_RRpm.Get(k).ToString();
            ln = ln + " · pitch " + m_RPitch.Get(k).ToString();
            ln = ln + " · lat " + m_RLat.Get(k).ToString();
            ln = ln + " · Ld " + m_RLd.Get(k).ToString();
            ln = ln + " · kArco " + m_RkArco.Get(k).ToString();
            ln = ln + " · ruedas " + m_RRuedas.Get(k).ToString();
            ln = ln + " · clutch " + m_RClutch.Get(k).ToString();
            ln = ln + " · pos " + m_RX.Get(k).ToString() + "," + m_RZ.Get(k).ToString();
            Print(ln);
        }
        Print("[BZDrive] ===== FIN CAJA NEGRA =====");
    }

    BZDriveOutput Sample(BZVehicleState st, float dt)
    {
        if (!m_Ready) return m_Out;
        // con la llegada hecha y el veredicto pendiente: la cola del log mide el reposo y el casco queda frenado
        if (m_FinPendiente)
        {
            VolcarLogCola(st, dt);
            m_Out.SetHalt();
            return m_Out;
        }
        // ★★★ ENTRADA AL ENDPOINT A 40 Hz: la decision de modo vive
        // en Plan (2 Hz) y a 16 km/h su paso (2.2 m) es del orden de la zona de
        // endpoint (3.2 m) -- el instante de entrada, y con el la velocidad de
        // llegada, quedaban sorteados por la fase del muestreo. Ver la cicatriz
        // en BZModeArbiter.PromoverEndpoint. Al promover, este mismo frame: el
        // freno se recomputa contra la pose final (abajo) y la cinta suelta el
        // mando (ENDPOINT no esta en su lista de modos poseibles en avance);
        // el slam toma el volante en el proximo Plan, ya frenando.
        // el titulo de arriba mentia a medias. La LLAMADA ya era a 40 Hz,
        // pero `m_Leg.remainM` -- lo que la compuerta compara -- se refresca SOLO en la
        // primera linea de Plan(), a 2 Hz, y por eso la entrada se seguia sorteando.
        // ✅: se corrige la foto por el AVANCE hecho desde ese refresco. Los dos
        // terminos del delta miden contra la MISMA pose final, asi que la diferencia
        // arco-vs-cuerda se cancela y el descuento nunca supera el avance de un tick.
        // El clamp a >= 0 es lo que hace la correccion MONOTONA: si el vehiculo retrocede
        // respecto del punto, no se "premia" con una entrada mas temprana.
        float remAhora = 0;
        if (m_Leg && m_Leg.valid)
        {
            remAhora = m_Leg.remainM;
            float avance = m_RemFrAtPlan - RemanenteFrameM(st);
            if (avance > 0) remAhora = remAhora - avance;
        }
        m_RemGate = remAhora;   // se loguea: verificar la IMPLEMENTACION, no mi modelo de ella
        if (m_Arbiter.PromoverEndpoint(st, m_Leg, m_Manual, remAhora, m_SurfKind)) m_Mode = BZDriveMode.BZDM_ENDPOINT;
        if (m_Mode == BZDriveMode.BZDM_ENDPOINT) EndpointFrameBrake(st);
        ActualizarVentanaDeCambio(st, dt);
        ActualizarDeslizamiento(st, dt);
        RefrescarVolante(st, dt);
        SteerSlew(st, dt);
        // ★★★ E2 LA CINTA: replay ESPACIAL de los inputs del usuario,
        // iman OFF. Va DESPUES de SteerSlew (nuestro slew no filtra la textura de
        // micro-correcciones: es señal; el unico filtro legitimo es el rate del
        // motor, el mismo que la produjo) y ANTES de GrabarAnillo (la caja negra
        // registra el replay). CRUISE/APPROACH/MANEUVER, avance y reversa
        // ENDPOINT avance, paradas e IDLE/HALTED siguen con
        // sus duenios. Cinta perdida (>6 m) => este tick cae al camino normal
        // y el diag lo muestra. La marcha replayada mata el hunting de caja
        // en el ORIGEN.
        m_CintaTick = false;
        m_FrenoFinB = 0;
        // el tracker de fase del LEG corre TODOS los ticks (no
        // depende del gate de posesion/modo — esa era la ceguera del detector
        // interno). Al cambiar el leg de fase, la cinta siembra su cursor.
        if (m_Cinta && m_Cinta.Valida() && m_Leg && m_Leg.valid)
        {
            if (!m_LegFaseSet)
            {
                m_LegFaseSet = true;
                m_LegFasePrev = m_Leg.isReverse;
            }
            else if (m_Leg.isReverse != m_LegFasePrev)
            {
                m_LegFasePrev = m_Leg.isReverse;
                // la posicion viaja para la salida SIN-BLANCO
                // (re-anclaje por proximidad, declarado en el log)
                m_Cinta.SembrarFase(m_Leg.isReverse, st.pos);
            }
        }
        // ★★★ POSESION POR ESTADO DEL LEG, NO POR VELOCIDAD (, n=6
        // pesados: TODO el presupuesto de error de la tanda vive en la maniobra
        // lenta de AVANCE — fallback 16-39% a mediana 2,0-6,6 km/h — y el trazo
        // del motor muestra la cinta trackeando a <1,5 m en el 92% de esos
        // frames MIENTRAS el fallback deriva el lat hasta 0,9-1,9 m). El umbral
        // de km/h (creep del casco; 2.0 solo-reversa desde ) gateaba
        // el manejo lento LEGITIMO: proteger el ARRANQUE no necesita un techo
        // permanente de velocidad, necesita un LATCH. "El LAUNCH posee hasta
        // que el leg rueda; a partir de ahi, la cinta" — arma al primer tick
        // >=2.0 ("rodando") del leg, se desarma al cambiar de leg: cusp o
        // parada = el proximo rest→rolling vuelve a ser del LAUNCH (huevo-y-
        // gallina del replay desde parado,: la toma arranca con ~70
        // frames del usuario sentado; gas 0 => sin movimiento => cursor clavado).
        // El caso especial `if isReverse` de quedo SUBSUMIDO y
        // retirado: una capa menos, cero perillas.
        // ARME POR FLANCO reposo→rodando. El flip de leg llega de
        // Plan (2 Hz) con velocidad residual del leg viejo; sin el requisito
        // de "quieto visto" el latch se re-armaba en el mismo tick del reset
        // (posesiones a 1.1-1.5 km/h en el primer frame del leg — medidas) y
        // el sobre-consumo envenenaba la monotonia. El reposo del cusp llega
        // justo DESPUES del flip: flip → quieto → launch → arma.
        // el resultado del AR se mide en el SENTIDO del leg (rodar para atras no es avanzar).
        float kmhSentidoAr = st.fwdKmh;
        if (m_Leg && m_Leg.valid && m_Leg.isReverse) kmhSentidoAr = -st.fwdKmh;
        m_AR.SetLargo(st.lenM);            // geometria de las zonas seguras
        if (!m_ARSuspendido) m_AR.Tick(st, dt, kmhSentidoAr);   // reloj, choque, episodios y resultado pendiente, TODOS los ticks (salvo en calibracion de launch, )
        // 791 AR MANUAL: "ultimo bien pisado" = lateral contra la POLILINEA (crossTrackM, maneje quien
        // maneje; no el lat del CSV, que es 0 en fallback) <= GoodLatM y avanzando (> 1 km/h en el sentido del chasis).
        if (m_Leg && m_Leg.valid && m_Cinta && m_Cinta.Valida() && st.fwdKmh > 1.0 && Math.AbsFloat(m_Leg.crossTrackM) <= m_ManGoodLatM)
        {
            float sBienAhora = m_Cinta.SEst();
            // 2: racha CONTINUA para poder retroceder dentro de ella.: es un INTERVALO [ini, fin]:
            // re-entrar en el (la maniobra y el LAUNCH vuelven sobre lo ya pisado) NO la re-abre; solo un hueco > 2 m
            // por delante o por detras la corta. Medido: la racha se re-abria en el choque y el retroceso quedaba en 0,05 m.
            if (!m_ManBienOk || sBienAhora > m_ManBienS + 2.0 || sBienAhora < m_ManBienIni - 2.0)
            {
                m_ManBienIni = sBienAhora;
                m_ManBienS = sBienAhora;
            }
            else if (sBienAhora > m_ManBienS)
            {
                m_ManBienS = sBienAhora;
            }
            m_ManBienOk = true;
        }
        // 3: tope de velocidad de la reversa tambien a 40 Hz (Plan a 2 Hz llega tarde)
        if (m_F1Activo && m_F1Fase == 0 && -st.fwdKmh >= m_F1RevKmh) m_Out.throttle = 0;
        // 4: reposo propio, maneje quien maneje (m_CintaReposoT solo cuenta con la cinta poseyendo: montado
        // sobre el sedan con 0/4 ruedas y la cinta suelta, el FISICO de ruedas en el aire no salia nunca)
        if (Math.AbsFloat(st.chassisKmh) < LOG_COLA_QUIETO_KMH) m_QuietoT = m_QuietoT + dt;
        else m_QuietoT = 0;
        if (m_Leg && m_Leg.valid)
        {
            if (m_Leg.legIdx != m_CintaRodoLeg)
            {
                m_CintaRodoLeg = m_Leg.legIdx;
                m_CintaRodo = false;
                m_CintaQuieto = false;
                m_CintaReposoT = 0;
                m_CintaRearmaDist = -1;
                m_ManBienOk = false;   // el "bien pisado" es de ESTE leg (un cusp atras no sirve de origen)
            }
            // ⚠⚠⚠ DEADLOCK EN PARADAS INTERMEDIAS. El comentario de arriba dice
            // "se desarma al cambiar de leg: cusp O PARADA" -- y asumia que toda parada es un
            // cambio de leg. No lo es: una parada con cambio de marcha SIN legBreak queda ADENTRO
            // del leg (DEMOBUS, 11 km en UN solo leg, 7 paradas asi). El latch seguia armado, la
            // cinta seguia poseyendo al vehiculo DETENIDO, y la cinta es ESPACIAL: toda la meseta
            // de v=0 de la toma (6 s del usuario bajando a 1ra, soltando el freno, dando gas) colapsa
            // en 1-4 cm de `s`, y `m_SEst` avanza POR ODOMETRIA. Si el vehiculo para unos cm ANTES
            // de esa meseta, la cinta devuelve las filas de LLEGADA (4ta + freno a fondo), le pisa
            // al LAUNCH su 1ra + gas, el vehiculo no se mueve, el cursor no avanza: para siempre.
            //   DEMOBUS   clavado 0,48 m antes de la meseta de s 2083,11 -> LEG 0 ABANDONADO
            //   CAZADOR   clavado ~0,1 m antes de s 1360,86             -> STUCK
            // Los cusps NO se clavan porque SI son cambio de leg (las 4 mesetas con marcha 0 de
            // las cintas coinciden con un legBreak; las 9 con cambio de marcha adentro del leg, no).
            //
            // La condicion es LITERALMENTE el deadlock, no un umbral de velocidad: un vehiculo
            // QUIETO al que la cinta le comanda FRENO no se puede mover nunca bajo la cinta.
            // Medido sobre los 236 reposos con la cinta poseyendo en los logs instrumentados:
            //   clavados reales (3):      reposo 11-21 s, modo 2, cinta.brk 1,00
            //   relanzamientos que andan: cinta.brk 0,00  -> la regla no los toca
            // ⚠ SOLO EN CRUISE: con MANIOBRA incluida la regla tocaba 90 reposos FINALES (rem 0,
            // modo 4, llegando al fin de leg), que es la maquinaria del endpoint recien cerrada a
            // 3 mm. En CRUISE: 3/3 clavados, 0 finales, 0 relanzamientos. Mutantes muertos: sin
            // el freno (rompe un relanzamiento), en todos los modos (rompe 90 finales), freno
            // imposible (no arregla nada).
            // El reposo es el que el controller YA define (LOG_COLA_QUIETO_*): cero umbrales nuevos.
            // ⛔ CORRECCION: aca decia "al cruzar 2.0 re-arma, y para entonces la odometria
            // ya paso la meseta". FALSO cuando el vehiculo queda lejos: DEMOBUS paro 5,5 m antes de la
            // meseta de s 8149, a 1,9 km/h la odometria avanzo 0,1 m, re-armo, la cinta lo volvio a frenar
            // -> CICLO suelta/frena/suelta (hasta 5 liberaciones por parada, +0,1 m por ciclo).
            // ⛔ Y EL FRENO SOLO NO ALCANZA: en el COASTEO de llegada la cinta dice gas 0 / freno 0, y en
            // SUBIDA (3,2 grados) el bus no se mueve nunca -> clavado en wp 3913, LEG 0 ABANDONADO.
            // Simulado sobre las 3 tomas con paradas (vehiculo detenido a 0,1 / 0,5 / 2,0 / 5,8 m antes
            // de cada meseta): `freno > 0,5` se pierde casi todo el coasteo; `v grabada > 2` se pierde los
            // casos a 0,1 m donde el usuario ya iba a 1,1-1,4 km/h con freno 1. LA UNION CUBRE EL 100%.
            // Sentido fisico de la union: la fila grabada donde quedo el vehiculo NO ES UNA FILA DE SALIDA
            // -- ahi el usuario seguia rodando o estaba frenando. Aplicarle eso a un vehiculo quieto no lo mueve.
            // Los relanzamientos que andan quedan DESPUES de la meseta, en filas con v grabada ~0 y freno
            // suelto: la union no los toca.
            // RE-ARMADO POR POSICION: se suelta hasta que el vehiculo recorra lo que falta hasta el FIN de
            // la meseta (FinMesetaAdelante, alcance SALTO_CONSULTA_M: lo que la odometria absorbe en la
            // primera consulta al re-armar). En las 3 tomas siempre aparece dentro de 8 m (max 5,8).
            // salida de un hold (ver Resume): la cinta se suelta ACA, que es donde hay st, antes de que el reposo cuente.
            if (m_SalirDelHold)
            {
                m_SalirDelHold = false;
                if (m_CintaRodo) SoltarCintaHastaFinDeMeseta(st, "AL SALIR DEL HOLD");
                m_CintaReposoT = 0;   // la oportunidad de arrancar (ver ENG_GRACIA_S)
                m_SinProgresoT = 0;
                m_EngGraciaT = ENG_GRACIA_S;
            }
            // la meseta que se acaba de cumplir (Muestra la cierra en el Sample anterior): los relojes de cero y la gracia
            bool enMesetaAhora = false;
            if (m_Cinta) enMesetaAhora = m_Cinta.EnMeseta();
            if (m_EnMesetaPrev && !enMesetaAhora)
            {
                m_CintaReposoT = 0;
                m_SinProgresoT = 0;
                m_EngGraciaT = ENG_GRACIA_S;
            }
            m_EnMesetaPrev = enMesetaAhora;
            if (m_EngGraciaT > 0) m_EngGraciaT = m_EngGraciaT - dt;
            if (Math.AbsFloat(st.chassisKmh) < LOG_COLA_QUIETO_KMH) m_CintaReposoT = m_CintaReposoT + dt;
            else m_CintaReposoT = 0;
            bool noEsSalida = (m_CintaM.kmhRec > CINTA_RODANDO_KMH || m_CintaM.brk > 0.5);
            // el RETROCESO ILEGITIMO, con el mismo umbral de rodadura que usa todo lo demas.
            // Legitimo = un leg de reversa, la maniobra del cusp o una reversa que pidio el AR: ahi ir para
            // atras es el plan. Fuera de eso, ir para atras en un leg de avance es la cuesta ganando.
            bool retroLegit = (m_Leg.isReverse || m_Mode == BZDriveMode.BZDM_MANEUVER || m_F1Activo || m_BogActivo);
            if (!retroLegit && st.fwdKmh < -CINTA_RODANDO_KMH) m_RetrocedeT = m_RetrocedeT + dt;
            else m_RetrocedeT = 0;
            // PRIMERO LA MESETA, DESPUES EL AR. El vehiculo detenido sobre la rampa final de una
            // detencion grabada esta a centimetros de la meseta que lo sacaria (medido: 1,1 cm en el wp 5825, dos
            // buses, 22 s cada uno, con la cinta poseyendo y modo CRUISE). Engancharla es reproducir la toma; el
            // AR es forcejeo. Va FUERA de la maquinaria del AR a proposito: en la corrida medida el AR no actuo
            // ahi -- ninguna de sus guardas externas lo explica, y su decision interna (cooldown, choque reciente,
            // episodio por lugar) no deja rastro cuando dice que no. Una salida que depende de eso no es una
            // garantia. Enganchada la meseta, el AR queda gateado por !EnMeseta(): no hay doble accion.
            // ⚠ SIN noEsSalida, a diferencia del AR: esa guarda existe para que el AR no SUELTE la cinta en una
            // fila de arranque, y el enganche no suelta nada -- mueve el cursor a la meseta que ya esta a la vista.
            // Medido sobre las 49 mesetas reales con el vehiculo detenido 1 cm corto: con noEsSalida quedan 6 sin
            // enganchar (las filas de coasteo, v grabada 1,0-2,0 y freno 0, donde la guarda da false); sin ella, 0.
            // Lo que lo protege es el REPOSO del vehiculo (nadie esta detenido en una recta) y el alcance de 8 m.
            // SE ENGANCHA TAMBIEN RETROCEDIENDO. El 105 paso de 1 km/h a 7 km/h hacia atras
            // SIN detenerse: nunca acumulo los 0,5 s de reposo, asi que nunca engancho y la suelta por lejania
            // de 874 no tuvo nada que soltar. "Moverse sin avanzar" no es ni andar ni estar detenido, y hasta
            // hoy no lo cubria nadie. El umbral de tiempo es el MISMO del reposo (WaitS); el de velocidad, el
            // mismo de rodadura. Enganchada la meseta, el sosten de 864 lo frena y el retroceso se termina.
            bool detenidoEng = (m_CintaReposoT >= m_AR.WaitS());
            bool retroEng = (m_RetrocedeT >= m_AR.WaitS());
            // TAMBIEN CLAVADO, Y TAMBIEN CON LA CINTA SUELTA. Tres SIN RESCATE del 24/09
            // (wp 1058 x2, Sosnovka 4628-4629): el bus detenido 1 cm ANTES de la fila de reposo, `cinta posee false`, la cinta
            // frenando hacia el reposo (gas 0, freno 0,3-0,6), el velocimetro leyendo 0,06-0,5 km/h (el transitorio del 934:
            // el reposo de 0,3 km/h nunca junta 0,5 s) y "sin progresar 10 s". Nadie enganchaba: el reposo por velocidad no
            // se cumplia y el enganche exigia posesion. A los 10 s la traba abria la escalera y la F1 reversaba contra la
            // garita. "Clavado" = el arco no progresa WaitS (odometria por posicion, inmune al transitorio), el vehiculo no
            // rueda (< CINTA_RODANDO_KMH) y la cinta de aqui a la meseta no pide gas (la cola de un frenado; saliendo de un
            // hold pide gas, y ahi NO engancha). Sin numeros nuevos. Si la cinta estaba suelta, vuelve a poseer al enganchar:
            // la meseta se reproduce en Muestra, que solo corre poseyendo. La guarda de m_Cinta es explicita (938).
            bool cintaOkEng = (m_Cinta && m_Cinta.Valida());
            bool clavadoEng = false;
            if (cintaOkEng && m_SinProgresoT >= m_AR.WaitS() && Math.AbsFloat(st.chassisKmh) < CINTA_RODANDO_KMH) clavadoEng = m_Cinta.SinGasHastaElReposo();
            // Con la cinta SUELTA, el reposo o el retroceso solos no alcanzan (lo encontro el banco): saliendo de un hold la cinta
            // esta suelta y el velocimetro puede leer < 0,3 km/h durante 0,5 s mientras arranca; engancharia la meseta siguiente.
            // Suelta, cualquier disparo exige ademas la cola sin gas hacia un reposo. Poseyendo, el 860 y el 876 siguen iguales.
            bool disparoEng = (detenidoEng || retroEng || clavadoEng);
            if (disparoEng && cintaOkEng && !m_CintaRodo && !clavadoEng) disparoEng = m_Cinta.SinGasHastaElReposo();
            // TAMBIEN EN MANIOBRA. Desde que el leg se cierra solo en la ULTIMA parada grabada de la fase, una parada
            // intermedia en la maniobra de un cusp se tiene que consumir como en CRUISE: el casco que llega despacio se
            // detiene 1-2 cm ANTES del bloque, sobre una fila de freno grabado 1, y ahi nadie lo saca (el cursor no avanza
            // porque el casco no se mueve, el casco no se mueve porque la fila frena; medido: 10 s clavado a 1,6 cm de un
            // bloque de 14 cuadros, hasta el AR fisico). La ultima parada de la fase no entra aca: la cierra el leg antes
            // (el cierre corre antes en este mismo Sample; y al cambiar de leg el reposo se reinicia, asi que este disparo
            // no ve el bloque viejo). Un bloque de 1-2 cuadros se cumple en 25-50 ms y el cursor sale a la fila siguiente.
            bool modoEng = (m_Mode == BZDriveMode.BZDM_CRUISE || m_Mode == BZDriveMode.BZDM_MANEUVER);
            if (cintaOkEng && disparoEng && modoEng && m_EngGraciaT <= 0 && !m_BogActivo && !m_F1Activo && !m_Cinta.EnMeseta())
            {
                bool poseiaEng = m_CintaRodo;
                if (m_Cinta.EngancharMesetaDetenido())
                {
                    if (!poseiaEng)
                    {
                        m_CintaRodo = true;
                        m_CintaRearmaDist = -1;
                    }
                    if (!s_Quiet)
                    {
                        string porEng = "reposo";
                        if (!detenidoEng && retroEng) porEng = "retroceso";
                        if (!detenidoEng && !retroEng) porEng = "clavado (sin progresar " + m_SinProgresoT.ToString() + " s a " + st.chassisKmh.ToString() + " km/h, la cinta sin gas hasta el reposo)";
                        string lnEng = "[C12] MESETA ENGANCHADA por " + porEng + " · la cinta ";
                        if (poseiaEng) lnEng = lnEng + "poseia";
                        else lnEng = lnEng + "estaba SUELTA y vuelve a poseer";
                        Print(lnEng + SufijoRunner());
                    }
                }
            }
            // la liberacion de abajo ES la accion NO-FISICA del AUTORECOVERY v2. La FIRMA es la
            // de siempre (cinta posee + detenido WaitS + fila no-de-salida + CRUISE); el AR decide si actua
            // (precedencia de lo FISICO, Enabled, MaxAttempts por episodio, CooldownS) y deja el registro.
            // Con los defaults (WaitS 0,5 = LOG_COLA_QUIETO_S, CooldownS 1,0) las paradas liberan igual que hoy.
            // una meseta grabada en curso es una detencion CON salida (por tiempo o por el hold de una parada
            // declarada): no es una traba, y el AR no la toca (en el cusp la fila grabada trae freno 1,0 y disparaba).
            if (m_CintaRodo && m_CintaReposoT >= m_AR.WaitS() && noEsSalida && m_Mode == BZDriveMode.BZDM_CRUISE && !m_BogEpisodio && !m_Cinta.EnMeseta())
            {
                string arEvid = "cinta v " + m_CintaM.kmhRec.ToString() + " freno " + m_CintaM.brk.ToString();
                arEvid = arEvid + " gas " + m_CintaM.thr.ToString() + " · detenido " + m_CintaReposoT.ToString() + " s";
                arEvid = arEvid + " · leg " + m_Leg.legIdx.ToString();
                // con la cinta a fondo (>= 0,9) y la ventana de AHOGO armada, el FISICO ESPERA a que la
                // ventana decida (3 s): si abre el episodio, es un ahogo y lo lleva C1; si se cierra sin firma, el FISICO
                // sale entonces. Medido (DEMOBUS wp 5346): FISICO 2,3 s antes del AHOGO = la misma traba contada dos veces.
                bool fisEnDuda = (m_BogVentOk && m_CintaM.thr >= 0.9) || m_F1Activo || m_F1GraciaT > 0;   // 1
                if (!fisEnDuda && m_AR.DebeReArrancar(st, m_Leg.nearWp, m_Cinta.SEst(), m_CintaM.thr, arEvid))
                {
                    SoltarCintaHastaFinDeMeseta(st, "EN PARADA INTERMEDIA: detenido " + m_CintaReposoT.ToString() + " s");
                }
            }
            if (!m_CintaQuieto && st.chassisKmh < CINTA_RODANDO_KMH) m_CintaQuieto = true;
            // SIN CINTA NO HAY POSESION. Este latch se armaba con CUALQUIER vehiculo que rodara >= 2 km/h
            // despues de estar quieto, tuviera cinta o no. En el harness (BZ12_DRIVE) nadie llama SetCinta -- m_Cinta es
            // null en TODAS esas corridas -- y el enganche de meseta de arriba (`!m_Cinta.EnMeseta()`, sin guarda porque
            // confiaba en m_CintaRodo) tiraba NULL pointer a 40 Hz en cuanto el vehiculo se detenia >= 0,5 s en CRUISE:
            // Sample abortaba AHI cada cuadro, sin AR fisico, sin pedal, con el ultimo input congelado. MEDIDO: 571
            // excepciones hoy (Pavlovo, tras un choque en el wp 2310: pasada 2 termino ROUTE_INCOMPLETE "a fondo sin
            // avanzar"), y 372-719 por sesion desde el 22/09 19:33 -- toda medicion de recuperacion tras choque hecha en el
            // harness desde entonces estaba contaminada. En BusDrive (con cinta) nunca paso. La posesion es DE LA CINTA:
            // sin una cinta valida el latch no se arma, y todo lo que lo lee queda apagado por construccion.
            if (!m_CintaRodo && m_Cinta && m_Cinta.Valida() && m_CintaQuieto && st.chassisKmh >= CINTA_RODANDO_KMH && !m_BogActivo && !m_F1Activo)
            {
                bool rearmaOk = true;
                if (m_CintaRearmaDist >= 0)
                {
                    float rearmaDx = st.pos[0] - m_CintaSueltaPos[0];
                    float rearmaDz = st.pos[2] - m_CintaSueltaPos[2];
                    float rearmaD = Math.Sqrt(rearmaDx * rearmaDx + rearmaDz * rearmaDz);
                    if (rearmaD < m_CintaRearmaDist) rearmaOk = false;
                }
                if (rearmaOk)
                {
                    m_CintaRodo = true;
                    m_CintaRearmaDist = -1;
                }
            }
        }
        // ★★★ LA CINTA POSEE TAMBIEN LA REVERSA. El reactivo en
        // reversa-recta es INESTABLE (eje trasero adelante = realimentacion
        // positiva; medido: REVrecta se va 6.13 m y choca a ~19 km/h) — y es
        // exactamente el caso que el replay resuelve: la toma del usuario retrocede
        // estable. La coherencia gear-grabado↔leg se chequea tras Muestra.
        if (m_Cinta && m_Cinta.Valida() && m_Leg && m_Leg.valid && m_CintaRodo)
        {
            // ★★★ VOLANTE-SOLO EN EL ENDPOINT DE REVERSA (, el caso
            // mision "clavar el estacionamiento de un impulso"). Medido en el
            // RPT 17-57: el slam clava el eje LONGITUDINAL perfecto (d=0.104 m,
            // v→0) pero cierra a 0.325/0.845 — TODO lateral HEREDADO de la
            // aproximacion (dist = hypot(along, crossTrack); AlignSteer es
            // rumbo-puro, ciego al lateral). La cicatriz escrita en el propio
            // slam manda: "cuando el endpoint queda largo tras una reversa, el
            // bug esta EN LA REVERSA, no en el slam". Fix: la cinta SOSTIENE EL
            // VOLANTE (replay + iman con signos de reversa) tambien en ENDPOINT
            // mientras la fase es reversa; el slam conserva freno/gas/marcha y
            // el latch. Bajo 2.0 la cinta SUELTA el volante y el clavado
            // final es TODO del slam (AlignSteer) —: el F-clavado-
            // rev pre-registrado DISPARO: sosteniendo el volante grabado
            // hasta el final, 08b cerro a 0.331 contra tol 0.25
            // y no despawneo; con la suelta bajo rodando (el gate exterior de
            // anoche) clavaba 0.10-0.20. El grabado+iman deja el vehiculo
            // centrado hasta ahi; el ultimo medio metro es del slam.
            // Avance NO se toca (sus endpoints ya clavan; el 0.514 del m4w es
            // backlog aparte).
            bool cintaVolanteSolo = (m_Mode == BZDriveMode.BZDM_ENDPOINT && m_Leg.isReverse && st.chassisKmh >= CINTA_RODANDO_KMH);
            // ✖ RETIRADA la "ventana del endpoint por estado" de
            // la condicion
            // `endIsStop && (APPROACH || MANEUVER)` pretendia localizar la
            // APROXIMACION al endpoint, pero el modo MANEUVER tambien vive en
            // la SALIDA del cusp (FromManeuver retiene el modo hasta salir de
            // la zona con histeresis y rodando) → en el leg FINAL la ventana
            // abria al ABRIR el leg, 75 wp antes del endpoint (08: cintaV
            // wp316-329 con devmax 1.20 — la zona que el enganche sembrado
            // estaba arreglando — y FIN paso de COMPLETE a 0.788; 08b de
            // 0.359 a 1.205). Leccion: el MODO no localiza la aproximacion.
            // El residual real (slam sin arrastre: la cinta replaya la
            // frenada grabada y llega a 0.38 km/h antes del endpoint, 08b
            // clava 0.359) queda ABIERTO — la palanca correcta necesitara
            // una ventana FISICA (remainM/distancia de frenado), no el modo.
            // La definicion del endpoint es exacta (: cinta y wp
            // separacion 0.000 m — no es contabilidad).
            // ★ MANEUVER YA NO EXCLUYE A LA CINTA: la zona de
            // maniobra de fin de leg era la SEGUNDA cara de la misma compuerta
            // — el trazo del ExpTractor (wp220-227: restan 3.5→0.3 m, modo 4,
            // cinta trackeando a 0.49 m, fallback aplicando y lat 0.55→0.94)
            // la muestra apilada sobre el creep; ~la mitad del fallback lento
            // de la tanda vive ahi (restan<=12 m). La maniobra grabada es
            // feasible POR CONSTRUCCION (misma maquina que la grabo); el
            // modulo de maniobra sigue corriendo (IsLegDone/cusp intactos) y
            // sigue siendo el dueño cuando la cinta no trackea (Muestra false
            // => fallback, mismo tick).
            if (m_Mode == BZDriveMode.BZDM_CRUISE || m_Mode == BZDriveMode.BZDM_APPROACH || m_Mode == BZDriveMode.BZDM_MANEUVER || cintaVolanteSolo)
            {
                // ★ POSESION POR FASE DEL LEG, no por gear crudo: la
                // coherencia `enReversa == leg.isReverse` rechazaba el ARRANQUE
                // de cada tramo de reversa — el gear grabado se atrasa un beat
                // en TODO cusp (medido: fase-cinta FALSE con leg reversa) → el
                // fallback agarraba desde el cusp, desviaba el vehiculo, distM
                // crecia y la cinta ya no re-enganchaba (08: cintaR 8, zigzag).
                // El LEG sabe la fase SIEMPRE; el gear la miente en el cusp. Un
                // cursor genuinamente mal ubicado lo corrigen las capas del
                // lector (filtro de fase en anclas + monotonia + lejos + rescate
                // + siembra), no este gate. El REPLAY no cambia: rueda/gas/gear
                // grabados tal cual — en el cusp son las manos del usuario ahi mismo.
                // el anclaje de reversa de la cinta solo con el casco rodando (ver la cuerda en el lector)
                bool rodandoC = (Math.AbsFloat(st.chassisKmh) >= CINTA_RODANDO_KMH);
                if (m_Cinta.Muestra(st.pos, m_CintaM, m_Leg.isReverse, rodandoC))
                {
                    // ⚠⚠⚠ LA RUEDA GRABADA SE CONVIERTE POR LA TRANSFERENCIA DE
                    // PLANTA, NO POR steerMax. El input de steering
                    // 1.0 NO mapea al steerMax del config: la PLANTA medida es ~85
                    // deg de rueda por unidad de cmd (steering_plant, julio; v1.1:
                    // targetFrontWheel / PlantSteerGain). El primer intento uso
                    // CommandForAngle, pero con steerTransfer=NONE (ChernoBus) ese
                    // cae al fallback lineal por steerMax → cStr = cFw/30 EXACTO
                    // (CN del run 15-33) → rueda x2.8 lo grabado, mismo choque.
                    // CommandForAngleReplay usa la curva medida cuando existe y la
                    // planta 85.1 cuando no — el fallback honesto del replay abierto.
                    // E3: iman ON — el error se corrige en el ESPACIO DE LA RUEDA
                    // (deg), antes de convertir por la planta, para que el sesgo
                    // sea fisico y el clamp tenga unidades honestas. latM + = a la
                    // DERECHA de la linea y +fw = DERECHA (verificado contra la
                    // toma, 98.2%) → el signo negativo devuelve hacia la linea.
                    float psiCinta = BZMath.WrapPi(st.yaw - m_CintaM.headingDeg * Math.DEG2RAD) * Math.RAD2DEG;
                    m_CintaPsi = psiCinta;
                    // ANTICIPACION DE ENTRADA: con un lock LARGO
                    // adelante (ver LockAdelante y su gate por largo), el blanco
                    // del iman se corre ENTRADA_LOCK_M hacia ADENTRO de la curva
                    // que viene (signo del pico): el error del lock se reparte
                    // (banco: -1.52 -> [-1.21,+0.59]; mutantes signo-invertido y
                    // sin-anticipacion MUEREN). Fuera del lock, blanco 0 = la
                    // linea del usuario, como siempre.
                    m_CintaRef = 0;
                    if (!m_Leg.isReverse)
                    {
                        // la anticipacion de lock es semantica de AVANCE; en
                        // reversa (baja v, sin locks largos) queda apagada
                        float fwLock = m_Cinta.LockAdelante(ENTRADA_VENTANA_M);
                        if (fwLock > 0.5) m_CintaRef = ENTRADA_LOCK_M;
                        else if (fwLock < -0.5) m_CintaRef = -ENTRADA_LOCK_M;
                    }
                    // escalado de alta velocidad: C2 y cap suben
                    // linealmente de 120 a 220 km/h; abajo de 120, identicos.
                    float vAltaF = Math.Clamp((st.chassisKmh - IMAN_V_ALTA_KMH) / (IMAN_V_TOPE_KMH - IMAN_V_ALTA_KMH), 0.0, 1.0);
                    float c2V = IMAN_C2_DEG_DEG + (IMAN_C2_ALTA - IMAN_C2_DEG_DEG) * vAltaF;
                    // ★ TUNING REVERSA: C2=1.0 con
                    // volante grabado recto. Su retiro-de-jure
                    // estaba condicionado al reemplazo medido (C2-m120) — el
                    // reemplazo fallo su falsador → la condicion
                    // cayo → vuelve. Referencia: conforme-reversa 0.364 CON el
                    // (ley vieja) vs 0.481 con psiMed (peor).
                    if (m_CintaM.enReversa && Math.AbsFloat(m_CintaM.fwDeg) < REV_C2_FW_RECTA_DEG) c2V = REV_C2_RECTA;
                    float capV = IMAN_CAP_DEG + (IMAN_CAP_ALTA_DEG - IMAN_CAP_DEG) * vAltaF;
                    float c1V = IMAN_C1_DEG_M;
                    if (m_Leg.isReverse || ImanDeCuspEnAvance(st))
                    {
                        c1V = IMAN_C1_DEG_M * REV_IMAN_FACTOR;
                        if (c2V < IMAN_C2_DEG_DEG * REV_IMAN_FACTOR) c2V = IMAN_C2_DEG_DEG * REV_IMAN_FACTOR;
                        capV = REV_IMAN_CAP_DEG;
                    }
                    // ✖✖ EL FRENTE DEL NULL-SPACE QUEDA ABIERTO — DOS INTENTOS,
                    // DOS ROLLBACKS, Y UNA CONCLUSION ESTRUCTURAL:
                    // la ley −(C1·lat + C2·ψgeo) se anula en lat=−(C2/C1)·ψ y se
                    // declara conforme con 0.13-0.27 m (medido, CSV sin gate;
                    // coeficientes confirmados a 1 mm). Intento 1:
                    // ψ→atan(latDot_crudo/v) — latDot crudo es RUIDO (escalones
                    // de cinta cuantizada ∝v; satur 32%). Intento 2 (,
                    // C2-m120): EMA α=0.1 + mezcla kmh/120 — jesko salio BIT A
                    // BIT (la mezcla-120 quedo PROBADA como eje de confianza del
                    // sensor: no re-discutir), pero el conforme de reversa
                    // EMPEORO (08b 0.364→0.481) y ExpTractor se desfondo (93→73%,
                    // latMAX 5.0, legs abandonados): EN MANIOBRA latDot≠0 ES EL
                    // REGIMEN NORMAL → el null-space no muere, CAMBIA DE VARIABLE
                    // (cancela contra el rumbo medido en vez del geometrico).
                    // NINGUNA ley proporcional sobre (lat, rumbo-cualquiera) lo
                    // cierra — cerrar el conforme pide INTEGRAL (con su windup) o
                    // compensarlo en OTRO lado. NO re-intentar proporcional: la
                    // proxima palanca de este frente es una decision estructural,
                    // no un tuning. latDot/psiMed quedan SOLO COMO DIAG (columnas
                    // del CSV, ya pagadas).
                    float latDotCinta = 0;
                    if (m_CintaLatPrevOk && dt > 0.001) latDotCinta = (m_CintaM.latM - m_CintaLatPrev) / dt;
                    m_CintaLatPrev = m_CintaM.latM;
                    m_CintaLatPrevOk = true;
                    m_CintaLatDotEma = 0.9 * m_CintaLatDotEma + 0.1 * latDotCinta;
                    float vMs = st.chassisKmh / 3.6;
                    if (vMs < IMAN_LATDOT_V_PISO) vMs = IMAN_LATDOT_V_PISO;
                    float psiMed = Math.Atan2(m_CintaLatDotEma, vMs) * Math.RAD2DEG;
                    // ★★★ SIGNO DEL IMAN EN REVERSA: la cinematica de
                    // retroceso invierte SOLO el termino de rumbo (banco con
                    // cintas reales: rev converge 1.0→0.0; forward diverge 16.8).
                    // Fase del LEG, no gear crudo.
                    float sPsiCinta = 1.0;
                    if (m_Leg.isReverse) sPsiCinta = -1.0;
                    // ★★★ INTEGRAL DEL LATERAL CON GATE DE REGIMEN (,
                    // rama 1 de la bifurcacion de ). Cierra el
                    // NULL-SPACE: la ley proporcional se declara conforme
                    // mientras el error se construye (LANDROVER n=9: 62% de
                    // frames con |pide|<0.25 y el lateral creciendo de 0.02 a
                    // 0.20 m). El integral acumula ese error persistente hasta
                    // que la correccion sale, que es el remedio estructural —
                    // y el unico que quedaba: cambiar el RUMBO del termino
                    // proporcional NO lo cierra (dos intentos, dos rollbacks:
                    // y 648), y cRef no mueve la aguja (banco).
                    // ⚠⚠ EL GATE ES LA GUARDA QUE LO HACE EMBARCABLE. Sin el,
                    // el integral ROMPE lo que hoy funciona: en un transitorio
                    // grande (recuperar un desvio de 1.10 m -- reales: 5-14%
                    // de los frames viven >0.5 m, MAX medido 1.25) la
                    // proporcional sola clava y el integral acumula durante la
                    // recuperacion y sobrepasa. Con el gate, el integral SOLO
                    // vive en el regimen donde el error es chico y PERSISTENTE
                    // (|lat| <= 0.30, el null-space: el LANDROVER satura en
                    // 0.25) y se DESCARGA fuera de el y en saturacion.
                    // Banco (fixtures calibrados contra el dato real, ruido de
                    // escalones del CSV + metricas de actuador):
                    //   B null-space   0.230 -> 0.036  (conformes 75% -> 49%)
                    //   C transitorio  0.000 -> -0.005 (identico; converge)
                    //   A giro cerrado 0.942 -> 0.938 (el gate lo apaga: inocuo)
                    //   temblor 0.019 -> 0.020 · saturacion 0% · |I| max 0.82
                    // ki=1.5 oscila en C (+0.15/-0.10/+0.06 amortiguando
                    // lento): 1.0 es el que amortigua limpio.
                    // El signo NO se invierte en reversa: el termino lateral
                    // conserva signo, y el integral es del lateral.
                    // ✖✖ EL INTEGRAL SALIO DE LA LEY: su falsador
                    // NO paso y la razon es ESTRUCTURAL, no de ganancia.
                    // Medido (LANDROVER n=6, log al reposo):
                    //   LAT endpoint  0.155 → 0.173   (σ 0.109 ⇒ indistinguible
                    //                 de no cambiar; lo firme es que NO BAJO)
                    //   |pide| med    0.188 → 0.428   ← la ley pide 2,3× MAS
                    //   conformes       62% →   42%   ← el null-space SI se rompio
                    //   |lat| med     0.198 → 0.221   ← y el error NO se movio
                    // El integral hizo exactamente lo que el banco predijo
                    // (cargo a 0.4-0.65, el gate no lo dejo cargar fuera de
                    // regimen) y aun asi no movio la aguja. La causa, medida:
                    // ★ EL IMAN ES EL 2,3% DEL COMANDO DE VOLANTE (p95 12,4%)
                    // en esa ventana — el volante lo maneja el comando
                    // REPLAYADO y el iman es un trim encima. Duplicar el 2% da
                    // 4%. **Ninguna ganancia dentro del iman tiene autoridad
                    // sobre este fenomeno**, sea proporcional o integral: va al
                    // mismo cajon que "no re-intentar proporcional".
                    // El frente queda RE-ENUNCIADO y es mas grande que una
                    // constante: *por que el comando replayado no reproduce la
                    // trayectoria grabada en ese tramo* — medible con fwRec vs
                    // el giro real del casco, sin tocar la ley.
                    // El integral SE SIGUE CALCULANDO como DIAGNOSTICO (columna
                    // imanI del CSV), igual que latDot/psiMed tras su rollback:
                    // no entra en el control.
                    m_ImanDeg = -(c1V * (m_CintaM.latM - m_CintaRef) + sPsiCinta * c2V * psiCinta);
                    // ★ CAP ASIMETRICO DE APERTURA:
                    // la falla ESPEJO del hairpin — entra +1.0 por ADENTRO de la
                    // curva derecha y el iman queda CLAVADO en -4.50 durante 25 m
                    // sin poder abrir (30-4.5=25.5 deg sigue dando mas curvatura
                    // que el path). Un sesgo que ABRE la rueda (signo opuesto al
                    // grabado, hacia 0) es seguro POR CONSTRUCCION: nunca supera
                    // lo grabado ni invierte el signo del comando (umbral 20 -
                    // apertura 12 = piso 8 deg del mismo signo). El que AGREGA
                    // rueda sigue capado en 4.5 (ese si puede hacer volantazo).
                    // NOTA: bajar el gate de largo del LockAdelante (el pedido
                    // del relay) quedo REFUTADO con el RPT: la deriva de wp270 es
                    // hacia ADENTRO — anticipar la entrada ahi la EMPEORABA.
                    float imanLo = -capV;
                    float imanHi = capV;
                    if (m_CintaM.fwDeg >= IMAN_ABRIR_UMBRAL_DEG && imanLo > -IMAN_ABRIR_DEG) imanLo = -IMAN_ABRIR_DEG;
                    else if (m_CintaM.fwDeg <= -IMAN_ABRIR_UMBRAL_DEG && imanHi < IMAN_ABRIR_DEG) imanHi = IMAN_ABRIR_DEG;
                    float imanCrudo = m_ImanDeg;
                    m_ImanPide = imanCrudo;
                    m_DiagPsiMed = psiMed;
                    m_DiagLatDot = latDotCinta;
                    m_ImanDeg = Math.Clamp(m_ImanDeg, imanLo, imanHi);
                    // El integral se actualiza DESPUES del cap: necesita saber
                    // si saturo (anti-windup por freeze) y se descarga tambien
                    // fuera del regimen del gate. Orden identico al del banco.
                    float latErrI = m_CintaM.latM - m_CintaRef;
                    if (Math.AbsFloat(imanCrudo - m_ImanDeg) > 0.0001 || Math.AbsFloat(latErrI) > IMAN_I_GATE_M)
                        m_CintaLatInteg = m_CintaLatInteg - m_CintaLatInteg * IMAN_I_LEAK * dt;
                    else
                        m_CintaLatInteg = m_CintaLatInteg + latErrI * dt;
                    m_Out.steering = Math.Clamp(m_Manual.CommandForAngleReplay((m_CintaM.fwDeg + m_ImanDeg) / Math.RAD2DEG), -1.0, 1.0);
                    // ★ DIAG DEL FRENTE LATERAL (, instrumentacion
                    // pura): con el cursor cerrado, el residual que queda es
                    // que el offset heredado del seed no converge (08b LEG 2:
                    // +0.385 sostenido 7 wp → sobrepaso a −1.10). Este diag
                    // responde frame a frame la pregunta del relay: que
                    // correccion PIDE el iman (pre-clamp) y que APLICA
                    // (post-clamp + volante comandado). pide==aplica y error
                    // sostenido → ganancia/din. insuficiente; |aplica|==cap →
                    // saturacion; sin lineas → no poseia. Gateado: |lat|>=0.3
                    // y cada 4 ticks (~10 Hz en zona mala, silencio en linea).
                    if (!s_Quiet && BZDiag.On() && Math.AbsFloat(m_CintaM.latM) >= 0.3)
                    {
                        m_DiagIman = m_DiagIman + 1;
                        if ((m_DiagIman % 4) == 1)
                        {
                            string di = "[Cinta] IMAN wp " + m_Leg.nearWp.ToString();
                            di = di + " · lat " + m_CintaM.latM.ToString();
                            di = di + " · psiGeo " + psiCinta.ToString();
                            di = di + " · psiMed " + psiMed.ToString();
                            di = di + " · latDot " + latDotCinta.ToString();
                            di = di + " · pide " + imanCrudo.ToString();
                            di = di + " · aplica " + m_ImanDeg.ToString();
                            di = di + " · cap [" + imanLo.ToString() + "," + imanHi.ToString() + "]";
                            di = di + " · fwRec " + m_CintaM.fwDeg.ToString();
                            di = di + " · cStr " + m_Out.steering.ToString();
                            di = di + " · kmh " + st.chassisKmh.ToString();
                            di = di + " · R " + m_Leg.isReverse.ToString();
                            Print(di);
                        }
                    }
                    m_CintaStr = m_Out.steering;
                    m_CintaVol = cintaVolanteSolo;
                    if (!cintaVolanteSolo)
                    {
                        // ★★★ TECHO FISICO DE LLEGADA (, el sobrepaso
                        // descompuesto): en legs que terminan en PARADA, la
                        // velocidad objetivo del replay queda capeada por lo
                        // que el freno del CASCO puede clavar en los metros
                        // que quedan: vClav = sqrt(2·MaxBrakeDecel·remainM) —
                        // el MISMO numero que usa el slam, cero perillas.
                        // Perfil medido (CSV 40 Hz): el replay llega CLAVADO a
                        // lo grabado hasta 2-3 m del final (21.6 vs 21.5) y el
                        // sobrepaso (+0.19/+0.39/+0.50, repetible a mm) nace
                        // en el ULTIMO metro — la frenada de clavado del usuario es
                        // mas fuerte que brkRec + iman capado (deadband 1.0 +
                        // tope 0.35), y el slam de Matador recibio el casco
                        // +3.5 km/h caliente y no llego a clavar en 1 m. SIN
                        // ventana y SIN modo (las lecciones de la (b)): lejos
                        // del final el techo es enorme => inocuo; muerde solo
                        // donde la fisica lo exige, y ahi el tope del freno
                        // extra se LIBERA (la frenada de clavado es legitima).
                        // Banco (integracion sobre los perfiles reales, freno
                        // = cota inferior de la frenada del usuario 9-13 m/s2):
                        // 08b +0.393→0.000 · Matador +0.502→+0.022 ·
                        // M4tractor +0.197→+0.002. Cusps NO (endIsStop=false:
                        // su frenada grabada ya clava, 18/18 medidos).
                        float kmhRecEff = m_CintaM.kmhRec;
                        float frnTope = IMAN_FRN_TOPE;
                        // ⚠⚠⚠ EL TECHO APLICA SOLO DONDE LO GRABADO Y LO
                        // CLAVABLE CONVERGEN ( →681, el gate que
                        // reemplaza a la ventana de 12 m): kmhRec <= vClav + 5.
                        // La ventana metrica era un INTERCAMBIO (12 m = 0.2 s
                        // para el jesko, 11 s para el tractor — un eje que no
                        // significa lo mismo en toda la flota, el patron de
                        // ). El gate no tiene eje: en CRUCERO la
                        // brecha rec−vClav es grande (jesko a 500 m: 12.7) →
                        // apagado POR CONSTRUCCION (el estrangulamiento que
                        // ahogo al jesko en 6ta es imposible: mordida <= 5
                        // matematicamente); en la FRENADA DE CLAVADO rec y
                        // vClav convergen → trabaja, a cualquier distancia y
                        // velocidad (el tractor recupera su zona; 08b cierra
                        // con su brecha de 4.8 — M=5 es el minimo que lo
                        // cubre, anclado al defecto medido +2-3.5 km/h).
                        // Censo con las 9 cintas reales: jesko aplica solo en
                        // 2-17 m (nada en 40-600), mordida <= 5 en toda la
                        // flota, cero aplicacion en crucero.
                        if (m_Leg.endIsStop)
                        {
                            float remClav = m_Leg.remainM;
                            if (remClav < 0) remClav = 0;
                            // el freno MEDIDO de la grabacion manda
                            // (EndpointBrakeDecel del header, hallazgo 31/07:
                            // el config es optimista y miente); fallback al
                            // manual solo si la ruta no lo trae.
                            float aClav = m_FrenoMedidoMs2;
                            if (aClav <= 0) aClav = m_Manual.MaxBrakeDecel(m_SurfKind);
                            float vClavKmh = 3.6 * Math.Sqrt(2.0 * aClav * remClav);
                            // ★ MARGEN POR FISICA (: 08b vivia a 0.2
                            // km/h del borde de la condicion — la 3ª constante
                            // absoluta que muerde fisicas distintas). La brecha
                            // de clavado es la diferencia PICO-vs-PROMEDIO de
                            // la frenada (toda frenada real termina mas fuerte
                            // que su promedio, y aMedido ES el promedio) — y
                            // eso escala con a: ratios medidos 1.9-2.3×a en la
                            // flota → 2.5 da ~25% de holgura. El PISO 5.0 es
                            // el valor ya validado in-motor (jesko verde con
                            // 5): M nuevo >= M viejo SIEMPRE → cero regresion
                            // posible; solo abre mas donde la fisica lo pide
                            // (08b: 2.49 → M 6.2, el borde muere).
                            float mConv = TECHO_CONV_KMH;
                            if (2.5 * aClav > mConv) mConv = 2.5 * aClav;
                            if (vClavKmh < kmhRecEff && kmhRecEff <= vClavKmh + mConv)
                            {
                                kmhRecEff = vClavKmh;
                                frnTope = 1.0;
                            }
                        }
                        // E3b: iman de gas — fidelidad de velocidad (ver consts)
                        float dvCinta = kmhRecEff - st.chassisKmh;
                        m_ImanGas = Math.Clamp(IMAN_GAS_POR_KMH * dvCinta, -1.0, 0.5);
                        // con el freno grabado a mas de la mitad la toma esta FRENANDO: no hay velocidad que alcanzar. Medido: el
                        // iman perseguia saltos de rubber-band de la velocidad grabada (10 -> 15 km/h en 1 cm, dentro de una
                        // frenada) y ponia gas 0,3 con freno 1 en la entrada a un cusp.
                        if (m_CintaM.brk >= 0.5 && m_ImanGas > 0) m_ImanGas = 0;
                        m_Out.throttle = Math.Clamp(m_CintaM.thr + m_ImanGas, 0.0, 1.0);
                        m_ImanFrn = 0;
                        if (m_Out.throttle <= 0.001 && -dvCinta > IMAN_FRN_LIBRE_KMH)
                        {
                            m_ImanFrn = IMAN_FRN_POR_KMH * (-dvCinta - IMAN_FRN_LIBRE_KMH);
                            if (m_ImanFrn > frnTope) m_ImanFrn = frnTope;
                        }
                        m_Out.brake = Math.Clamp(m_CintaM.brk + m_ImanFrn, 0.0, 1.0);
                        AplicarFrenoAnticipado(st);   // freno grabado sobre un quiebre de bajada, repartido antes
                        // FRENO GRABADO CONTRA UN VEHICULO YA DETENIDO = EMPATE DE PEDALES.
                        // El freno de la toma existe para SACAR velocidad. Si el vehiculo ya esta quieto y la toma en
                        // ese arco iba RODANDO, ese freno no tiene nada que sacar: lo unico que hace es pelear contra
                        // el gas que el iman pide para alcanzar la velocidad grabada, y gana el freno. Medido en el
                        // tercer intercambio de PATRULLA (caja negra a 40 Hz, t 183,5-184,7): thr 0,42 contra brk 0,41
                        // durante 7 s con el vehiculo a 0,005-0,05 km/h, y arranca EN EL MISMO TICK en que el freno
                        // llega a 0 (t 184,64: 0,27 km/h, y 1,75 km/h 1,8 s despues). El freno no era 1,00 sino el
                        // interpolado por arco entre el frame del cursor (brk 1, grabado 5,32 km/h) y el siguiente
                        // (brk 0): con el vehiculo quieto el cursor no avanza, asi que el empate se sostiene solo.
                        // Ademas TAPA la rampa de arranque, que se auto-excluye con freno (`m_Out.brake > 0.01`) y
                        // termino imprimiendo "la rampa no despego · 5 vias falsadas" sin haber podido empujar nunca.
                        // Costo medido: 9,6 s parado donde el humano estuvo 4,3 s, y 0,70 m cubiertos arrastrandose.
                        // No es del cusp: pasa en CUALQUIER punto donde frenemos un poco antes que la toma.
                        // No toca ninguna frenada real: por debajo del umbral de rodadura ya no se frena, se sostiene
                        // -- y si la toma TAMBIEN estaba detenida ahi (kmhRecEff < umbral) el freno queda como esta,
                        // que es lo que clava las paradas y los finales de ruta.
                        if (st.chassisKmh < CINTA_RODANDO_KMH && kmhRecEff >= CINTA_RODANDO_KMH)
                        {
                            // SOLTAR EL FRENO ES PARA LLEGAR AL PUNTO, NO PARA PASARLO.
                            // La regla de arriba saca el empate de pedales, pero sin esta segunda mitad el vehiculo
                            // sale del empate y ya no vuelve a frenar: cruza el fin del leg rodando a 1,1-1,5 km/h,
                            // que ademas esta JUSTO en el umbral de "detenido" del modulo de maniobra (creep/2), asi
                            // que el leg no cierra hasta que la velocidad cae sola. Lo que se recorre mientras tanto
                            // es puro azar. Medido en 10 corridas de PATRULLA contra el punto grabado del cusp:
                            //   build 17:51 (sin la regla)  se DETIENE en el leg (min 0,00-0,01 km/h) · pasada 0,10 y 0,27
                            //   builds 23:58 y 09:59        NO se detiene  (min 1,09-1,50 km/h)      · pasada 0,18 a 0,61
                            // Y la pasada del cusp es lo que decide el leg siguiente: ordenadas las 10 corridas por
                            // pasada, el |lat| medio de la reversa las sigue en el mismo orden (0,10->0,07 · 0,18->0,10
                            // · 0,27->0,09 · 0,29->0,14 · 0,43->0,25 · 0,47->0,23 · 0,50->0,26 · 0,53->0,30 · 0,60->0,30
                            // · 0,61->0,34) y con el |lat| se mueve el lateral de llegada. O sea: el error del leg de
                            // reversa NO nacia en la reversa -- nacia 23 m antes, en donde quedo parado el vehiculo.
                            // `m_RemGate` es el remanente al fin del leg corregido por frame (40 Hz, ): con
                            // el, frenar a LLEGADA_M deja la pasada en ~0,10 m (la frenada desde 1,3 km/h son 2-4 cm).
                            if (m_RemGate <= BZDriveTol.LLEGADA_M)
                            {
                                m_Out.throttle = 0;
                                m_Out.brake = 1.0;
                            }
                            else
                            {
                                m_Out.brake = 0;
                            }
                        }
                        // EL SOSTEN DE LA MESETA SE RE-APLICA ACA, A 40 Hz. Plan (2 Hz) lo aplica y este bloque lo pisaba en
                        // el Sample siguiente con el freno GRABADO: el freno 1,0 vivia 25 ms de cada 500 y el resto del tiempo
                        // mandaba lo grabado, que en la meseta que motivo el sosten (Svergino, +2,3 grados) es gas 0 y freno 0:
                        // el vehiculo quedaba sin sostener. Mismas guardas que en Plan (la cinta posee, EnMeseta, cede a C1/F1);
                        // al salir de la meseta EnMeseta() da false y en ese mismo cuadro vuelve lo grabado.
                        SostenerMeseta(st);
                        m_Out.gear = m_CintaM.gear;
                        if (m_SostenTope >= AHOGO_MARCHA) AplicarTopeDeSosten(st);   // hueco A
                        FrenoAlFinDelLeg(st);   // la toma se agoto o va a paso de arrastre y el casco rueda: frenar a la pose
                    }
                    m_CintaDist = m_CintaM.distM;
                    m_CintaTick = true;
                }
                else
                {
                    m_CintaDist = m_CintaM.distM;
                }
            }
        }
        // instrumentacion: cuando el latch esta
        // armado, el modo es poseible y la cinta IGUAL no posee, decir POR QUE
        // — cada 8 ticks, en LAS DOS fases. Con el gate de estado, la unica
        // compuerta restante es Muestra: este diag es el censo de sus rechazos
        // (LEJOS/perdida/mismatch-fase). El proximo cambio se dimensiona
        // contra el MOTOR, no el espejo.
        bool diagModoPoseible = (m_Mode == BZDriveMode.BZDM_CRUISE || m_Mode == BZDriveMode.BZDM_APPROACH || m_Mode == BZDriveMode.BZDM_MANEUVER);
        if (!m_CintaTick && m_Cinta && m_Cinta.Valida() && m_Leg && m_Leg.valid && m_CintaRodo && diagModoPoseible && !s_Quiet && BZDiag.On())
        {
            m_DiagNoPosee = m_DiagNoPosee + 1;
            if ((m_DiagNoPosee % 8) == 1)
            {
                string np = "[Cinta] NO-POSEE(fwd)";
                if (m_Leg.isReverse) np = "[Cinta] NO-POSEE(rev)";
                np = np + " modo " + m_Mode.ToString();
                np = np + " · kmh " + st.chassisKmh.ToString();
                np = np + " · ultDistM " + m_CintaDist.ToString();
                np = np + " · faseCinta " + m_CintaM.enReversa.ToString();
                np = np + " · legIdx " + m_Leg.legIdx.ToString();
                // la confirmacion del mecanismo viejo, horneada — si
                // el ancla vieja caia en tramo consumido, aca se ve el descarte
                np = np + " · skipCons " + m_Cinta.SkipConsumidos().ToString();
                Print(np);
            }
        }
        else if (m_CintaTick) m_DiagNoPosee = 0;
        // sin posesion no hay latDot valido — el proximo enganche
        // arranca con derivada 0 (sin spike por el gap/la siembra)
        if (!m_CintaTick)
        {
            m_CintaLatPrevOk = false;
            m_CintaLatDotEma = 0;
            // sin posesion el integral no tiene sentido — y esto
            // cubre el cambio de leg/siembra por construccion (el latch se
            // desarma ⇒ hay gap de posesion ⇒ arranca de cero en cada leg).
            m_CintaLatInteg = 0;
        }
        // C1: lo que se APLICO de verdad (la cinta pisa a 40 Hz lo que Plan decide a 2 Hz)
        m_BogThrAp = m_Out.throttle;
        m_BogBrkAp = m_Out.brake;
        if (st.fwdKmh < m_BogFwdMinInt) m_BogFwdMinInt = st.fwdKmh;
        GrabarAnillo(st, dt);
        DetectarChoque(st, dt);
        ReportarPrimerGasTrasHold(st, dt);   // a 40 Hz, con el gas de este cuadro
        VolcarLog(st, dt);
        return m_Out;
    }

    // Recomputa el freno contra la pose final con la posicion de ESTE frame. A 2 Hz,
    // Boris cruza el punto con envion; a ~40 Hz para EN el punto.
    // Remanente LONGITUDINAL a la pose final, proyectado sobre su tangente, con la
    // pose de ESTE frame. Es la unica distancia al final que el controlador sabe a
    // 40 Hz: `m_Leg.remainM` sale del CURSOR y solo se refresca en Plan (2 Hz).
    // Firmado: + = el final esta adelante, - = ya se paso.
    // el iman con la ganancia de reversa tambien en avance, en los ultimos metros antes de un cusp (CUSP_IMAN_ZONA_WB)
    private bool ImanDeCuspEnAvance(BZVehicleState st)
    {
        if (!m_Leg || !m_Leg.valid) return false;
        if (m_Leg.isReverse || m_Leg.endIsStop) return false;
        bool dentro = (m_Mode == BZDriveMode.BZDM_MANEUVER);
        if (!dentro)
        {
            if (m_Mode != BZDriveMode.BZDM_CRUISE && m_Mode != BZDriveMode.BZDM_APPROACH) return false;
            float wbC = 2.5;
            if (m_Manual && m_Manual.wheelbaseM > 0.5) wbC = m_Manual.wheelbaseM;
            dentro = (m_Leg.remainM <= CUSP_IMAN_ZONA_WB * wbC);
        }
        if (dentro && m_ImanCuspLeg != m_Leg.legIdx)
        {
            m_ImanCuspLeg = m_Leg.legIdx;
            string li = "[C12] IMAN DE CUSP en el leg " + m_Leg.legIdx.ToString() + ": ganancia x" + REV_IMAN_FACTOR.ToString() + " desde rem " + m_Leg.remainM.ToString() + " m";
            li = li + " · lat " + m_CintaM.latM.ToString() + " m · " + st.chassisKmh.ToString() + " km/h";
            if (!s_Quiet) Print(li + SufijoRunner());
        }
        return dentro;
    }

    private float RemanenteFrameM(BZVehicleState st)
    {
        return (m_EndPos[0] - st.pos[0]) * m_EndTanX + (m_EndPos[2] - st.pos[2]) * m_EndTanZ;
    }

    // ⚠⚠⚠ POR QUE EXISTE ESTA COLUMNA Y NO ALCANZA CON px,pz.
    // `px` se imprime con 6 DIGITOS SIGNIFICATIVOS. A X ~ 13343 eso son 0.1 m de
    // resolucion; a Z ~ 6256, 0.01 m. O sea que la precision del log depende de la
    // MAGNITUD DE LA COORDENADA DEL MAPA, y por lo tanto de hacia donde llega cada ruta:
    //    Matador   llegada X -0.99 Z +0.11  ->  el LONG cae sobre X  ->  cuantiza a 10 cm
    //    LANDROVER X -0.98 Z -0.17          ->                           10 cm
    //    M4tractor X -0.31 Z +0.95          ->                            4 cm
    //    SPEED01   X -0.04 Z +1.00          ->                            1.4 cm
    // Eso fabrico una "bimodalidad de exactamente 10 cm" en el Matador y una
    // "repetibilidad de 1 mm" que era el mismo casillero de redondeo. Las dos se
    // retiraron.
    //
    // La salida NO es imprimir mas digitos: es medir una cantidad que ARRANQUE CERCA DE
    // CERO, porque ahi los 6 digitos son de sobra. `remFr` ya lo hace en el eje
    // longitudinal; esto lo completa en el LATERAL, en el MISMO marco de la pose final
    // (perpendicular a su tangente), que es el marco en el que se reporta LONG/LAT.
    // Signo: + = el vehiculo esta a la IZQUIERDA de la linea de llegada.
    private float LateralFrameM(BZVehicleState st)
    {
        return (m_EndPos[0] - st.pos[0]) * m_EndTanZ - (m_EndPos[2] - st.pos[2]) * m_EndTanX;
    }

    private void EndpointFrameBrake(BZVehicleState st)
    {
        if (!m_Manual) return;
        float signed = RemanenteFrameM(st);
        float d = signed - 0.10;
        if (d <= 0.01)
        {
            m_Out.throttle = 0;
            m_Out.brake = 1.0;
            return;
        }
        float v = st.chassisKmh / 3.6;
        float aMax = m_Manual.MaxBrakeDecelEndpoint(m_SurfKind);   // techado por la frenada medida en la toma
        if (aMax < 0.5) aMax = 0.5;
        float aReq = (v * v) / (2.0 * d);
        float b = Math.Clamp(aReq / aMax, 0.0, 1.0);
        if (b > m_Out.brake) m_Out.brake = b;
    }

    // ⚠⚠ ESTO CRUZA EL SEAM CONGELADO, Y ES LO UNICO QUE LO CRUZA.
    //
    // El objetivo de volante se fijaba en Plan(), a 2 Hz. Medido en AERO: entre dos updates
    // el vehiculo recorre 43% del radio de la curva en un apice (R~7.6 m), 23% en curva
    // media y 1% en recta. O sea que en el apice el volante apuntaba a una pose que el
    // vehiculo ya habia dejado atras -- y eso explica el volante saturado, el cross-track
    // del apice y el desfase de LOGRA, sin necesidad de ningun deficit de modelo (la tabla
    // ejecuta >= lo comandado en el 94% de las curvas).
    //
    // ✖ LO QUE **NO** SE MUEVE, a proposito:
    //   - el cursor y el PUNTO DE MIRA los sigue fijando Plan(). Esto no adelanta el
    //     horizonte ni cambia a donde va el vehiculo.
    //   - modo, velocidad objetivo, marcha: siguen en Plan().
    //   - SteerSlew queda intacto (3.0/s): es el amortiguador, y sin el esto podria saltar.
    // Lo unico que se refresca es el ANGULO con el que persigue un punto que no se movio.
    //
    // Y en recta no puede hacer nada: el termino cambia 1% del radio entre updates. Si una
    // recta que hoy esta a 2 cm se mueve, esto metio ruido y hay que revertirlo.
    // ⚠⚠⚠ EL CAP VA EN UN SOLO LUGAR PORQUE HAY DOS ESCRITORES DEL VOLANTE.
    //
    // `m_TargetSteer` lo escriben dos caminos:
    //     RefrescarVolante (40 Hz)  -- donde puse el cap la primera vez
    //     Decide/Plan (2 Hz)        -- `m_TargetSteer = m_Out.steering`, SIN capar
    //
    // El Plan lo pisaba cada 500 ms con el volante que sale de la derivacion, que convierte
    // `g.curvature` a angulo con la MISMA multiplicacion por subviraje. Medido:
    // K-CLAMP presente, kmax 0.0238, cap 0.088... y str 0.372. El cap estaba puesto en uno
    // de los dos escritores.
    //
    // ★ Es el patron que ya nos mordio dos veces: el despawn se penso para el camino de
    // "llego bien" y hubo que moverlo al embudo; el silencio de las suites lo seteaba una de
    // cinco. **Una guarda sobre un estado con varios escritores tiene que vivir en una
    // funcion, no copiada en el que uno se acordo.**
    //
    // ✖ NO se aplica a los modulos POSE (maniobra/endpoint): ahi el volante no sale del
    // pursuit y `kmax` puede ser de un tick viejo. Ademas las maniobras ya andan bien
    // medidas con el cap puesto.
    // ⚠ SE LLAMA AL PRINCIPIO DE Sample, ANTES de RefrescarVolante, A PROPOSITO: LOGRA es
    // RETROSPECTIVO --sale de (yaw - yawPrev), o sea la respuesta al comando ANTERIOR-- asi
    // que hay que compararlo contra el `m_Out.steering` que todavia NO se piso este tick.
    // Es la misma leccion del desfase que ya esta anotada en ReportarDireccion.
    // La ventana se abre SOLO cuando la marcha pedida cambia. Un vehiculo en marcha estable
    // --aunque este lugueando y con el embrague patinando-- no la abre nunca.
    private void ActualizarVentanaDeCambio(BZVehicleState st, float dt)
    {
        if (m_Out.gear != m_FrzGearPrev)
        {
            m_FrzGearPrev = m_Out.gear;
            m_FrzRestante = FRZ_MAX_S;
            return;
        }
        if (m_FrzRestante > 0) m_FrzRestante = m_FrzRestante - dt;
    }

    // Congelar la correccion pide las DOS cosas: que haya un cambio reciente Y que el
    // embrague todavia no haya agarrado. La segunda la termina antes en el caso comun
    // (mediana 0.375 s); la primera impide que el lugueo la dispare.
    private bool CongelarCorreccion(BZVehicleState st)
    {
        if (m_FrzRestante <= 0) return false;
        if (!m_Manual) return false;
        return m_Manual.EmbragueEnTransicion(st.chassisKmh, st.rpm, m_Out.gear);
    }

    private void ActualizarDeslizamiento(BZVehicleState st, float dt)
    {
        float yawPrev = m_DeslizYawPrev;
        vector posPrev = m_DeslizPosPrev;
        m_DeslizYawPrev = st.yaw;
        m_DeslizPosPrev = st.pos;
        // ⚠ SE LIMPIA ANTES DE LAS COMPUERTAS. Si una salida temprana --modulos POSE,
        // reversa, baja velocidad-- dejara el valor viejo, el contravolante podria actuar
        // con una deriva medida en otra situacion. Solo vale la del tick que la calculo.
        float derivaPrev = m_Deriva;
        m_Deriva = 0.0;
        m_DerivaTasa = 0.0;
        // Los modulos POSE (maniobra/endpoint) mandan su propio volante y ya andan medidos:
        // el detector no los toca, igual que el cap de agarre.
        if (!m_UsoPursuit) return;
        if (yawPrev < -900.0) return;
        if (dt < 0.001) return;
        // En reversa el auto gira al reves del volante y "signo opuesto" seria lo normal.
        if (m_Leg && m_Leg.isReverse) return;
        if (st.chassisKmh < DESLIZ_MIN_KMH) return;
        float vms = st.chassisKmh / 3.6;
        if (vms < 0.5) return;
        float kLogra = (BZMath.WrapPi(st.yaw - yawPrev) / dt) / vms;
        // ★ ANGULO DE DERIVA: el rumbo por donde el auto SE MUEVE menos el rumbo al que
        // APUNTA. Es la medida directa del deslizamiento --y la que un contravolante
        // necesitaria, porque apuntar las ruedas por donde el auto va es una identidad
        // geometrica, no una ganancia inventada. Sale del delta de posicion, sin API nueva.
        float deriva = 0.0;
        float dx = st.pos[0] - posPrev[0];
        float dz = st.pos[2] - posPrev[2];
        float paso = Math.Sqrt(dx * dx + dz * dz);
        // A 20 km/h y 40 Hz el paso es 0.14 m; debajo de 0.05 m el rumbo del delta es ruido.
        if (paso > 0.05) deriva = BZMath.WrapPi(BZMath.HeadingXZ(dx, dz) - st.yaw);
        m_DerivaTasa = (deriva - derivaPrev) / dt;
        m_Deriva = deriva;
        // ⚠ LA TASA SOSTENIDA SIGUE MIDIENDOSE Y LOGUEANDOSE, aunque ya no gobierna: la
        // separacion que mostro (3 ticks de tasa alta en la que clava contra 156 en la que
        // vuela) es real y es el instrumento con el que se mediria un reemplazo. Lo que se
        // fue es la ANTICIPACION, no la medicion.
        m_DerivaTasaFilt = m_DerivaTasaFilt + (m_DerivaTasa - m_DerivaTasaFilt) * 0.05;
        float cmd = m_Out.steering;
        bool opuesto = false;
        if (Math.AbsFloat(cmd) > DESLIZ_MIN_MAG)
        {
            if (Math.AbsFloat(kLogra) > DESLIZ_MIN_MAG)
            {
                if ((cmd * kLogra) < 0.0) opuesto = true;
            }
        }
        // ⚠ POR CONTEO, NO POR RACHA CONSECUTIVA: el GT2RS que vuela tiene maximo 1 tick
        // opuesto seguido. Pedir una racha no lo detectaria nunca.
        if (opuesto) m_DeslizEventos = m_DeslizEventos + 1;
        // ⚠ SE LOGUEA TAMBIEN POR DERIVA, no solo por signo opuesto: si la accion se dispara
        // por deriva y la linea solo saliera en los ticks de signo opuesto, el arranque de
        // la maniobra quedaria invisible -- justo el tramo que hay que medir.
        if (opuesto || Math.AbsFloat(deriva) > (DERIVA_UMBRAL_RAD * 0.5))
            ReportarDesliz(st, cmd, kLogra, deriva);
    }

    private void ReportarDesliz(BZVehicleState st, float cmd, float kLogra, float deriva)
    {
        if (s_Quiet || !BZDiag.On()) return;
        if (!m_Leg) return;
        string s = "[BZDrive] DESLIZA wp " + m_Leg.nearWp.ToString();
        s = s + " · kmh " + st.chassisKmh.ToString();
        s = s + " · str " + cmd.ToString();
        s = s + " · LOGRA " + kLogra.ToString();
        // ⚠ `str` es un comando NORMALIZADO y `LOGRA` una curvatura: comparar sus
        // magnitudes no significa nada. COMANDA40 es la curvatura comandada a 40 Hz, la
        // unidad de LOGRA: la RAZON entre las dos es la que distingue "arranco a doblar y
        // el yaw viene atrasado" (residuo chico) de "estoy deslizando" (giro opuesto con
        // autoridad). Sin esta columna el filtro anti-falso-positivo no se puede dimensionar.
        s = s + " · COMANDA40 " + m_KRefresco.ToString();
        s = s + " · deriva " + (deriva * Math.RAD2DEG).ToString() + "deg";
        // ★ EL NIVEL NO SEPARA EN EL ONSET --un vuelo del Huracan nace en 4.7/8.8 deg,
        // encima de la poblacion que clava. Lo que separa es que el vuelo COMPONE y la
        // que clava VUELVE. Esta columna es esa distincion, medida, para la vuelta que
        // viene: hoy no gobierna nada.
        s = s + " · dderiva " + (m_DerivaTasa * Math.RAD2DEG).ToString() + "deg/s";
        // ★ La tasa SOSTENIDA y la deriva PREDICHA: son las que deciden el disparo, asi que
        // tienen que estar en el log al lado de las instantaneas para poder contrastarlas.
        s = s + " · dsost " + (m_DerivaTasaFilt * Math.RAD2DEG).ToString() + "deg/s";
        s = s + " · lat " + m_Leg.crossTrackM.ToString();
        s = s + " · n " + m_DeslizEventos.ToString();
        Print(s);
    }

    // Embudo unico del volante: LOS DOS escritores de m_Out.steering pasan por aca. Tenerlo
    // en un solo lugar es la leccion de las dos veces que un arreglo se aplico en un camino
    // y el otro lo piso.
    // Peso del volante grabado: 0 debajo de la compuerta de la flota, 1 a la velocidad donde
    // se midieron los onsets. Entre medio, lineal.
    private float PesoVolanteGrabado(float kmh)
    {
        // ⚠ NO LLEVA UN `if (kmh <= MIN) return 0` PORQUE SERIA REDUNDANTE, y el espejo lo
        // demostro: debajo del piso la rampa ya devuelve un valor NEGATIVO, y quien llama
        // corta con `w <= 0`. La proteccion de la flota es estructural, no un gate que
        // pueda fallar. Misma leccion que la guarda que murio en el feedforward del pedal.
        if (kmh >= FFVOL_KMH_MAX) return 1.0;
        return (kmh - FFVOL_KMH_MIN) / (FFVOL_KMH_MAX - FFVOL_KMH_MIN);
    }

    // ✖✖ LA MEZCLA SE FUE: fue NET NEGATIVA y el A/B lo aisla.
    //     GT63 5.7 -> 12.7 m · Huracan 8.4 -> 28.7 m, con la flota intacta (ffVolW=0).
    // El feedforward disparo bien (ffVolW 0.7-1.0) pero MATO EL TRIM: con peso ~1 el `str`
    // queda en ~0.005 --siguiendo la rueda del humano, que en la recta era ~0-- y el lateral
    // crece 1.5 -> 4 -> 8 m SIN NADIE QUE CORRIJA. Es exactamente la salvedad de,
    // que yo mismo habia marcado como el riesgo principal en el: el volante del
    // humano asume la POSICION del humano, y v1.2 no esta ahi.
    //
    // ★ LO QUE SE QUEDA ES EL DATO, COMO INSTRUMENTO. La columna `recVolDeg` del log es lo
    // que permite comparar, en el mismo metro, lo que v1.2 comanda contra lo que el humano
    // tenia en las ruedas. Justo la medicion que hace falta ahora que sabemos que la deriva
    // es AUTO-INFLIGIDA: v1.2 se sale a 115 km/h donde el humano a 134 no se sale.
    private float AcotarVolante(float cmd, BZVehicleState st)
    {
        if (!m_UsoPursuit) return cmd;
        // ✖✖ EL AFLOJO A CERO ESTA REMOVIDO A PROPOSITO. Se probo (PBO 18:58:08) y fue una
        // REGRESION NETA: GT2RS 2.1/19.9/2.5 -> 16.0/9.2/17.9, Huracan 6.7/22.6/1.7 ->
        // 19.7/16.1/32.4, y el ZiS5 de control paso de 0 a 5 CHOQUES.
        // Dos razones medidas, las dos de UNIDADES:
        //  1. 1.5 s de volante en cero a 107 km/h son 45 METROS sin corregir. El sosten
        //     largo que evita el parpadeo es el mismo que garantiza el abandono de la
        //     linea: el costo del aflojo escala con la velocidad y 1.5 s fijo es letal.
        //  2. Al ARRANCAR una curva legitima el yaw todavia no reacciono, asi que LOGRA
        //     trae el residuo del tramo anterior y da signo opuesto. El ZiS5 comando
        //     str 0.26 con LOGRA -0.024 --una curva real leida como deslizamiento.
        // El DETECTOR se queda: separa limpias (n=0) de vuelos (n>=2) sin un falso en los
        // supercars, y es el instrumento con el que se va a medir el reemplazo. Lo que no
        // se queda es ESTA accion. Ver.
        float base = AcotarVolantePorAgarre(cmd, st.chassisKmh);
        return Contravolante(base, st.chassisKmh);
    }

    // ★★★ CONTRAVOLANTE: apuntar las ruedas por donde el auto SE MUEVE.
    // No es una ganancia inventada: es una identidad geometrica --ganancia 1 en el espacio
    // de angulos-- y por eso reemplaza al aflojo, que si necesitaba una duracion inventada.
    // ★ LA CONDICION DE SALIDA ES FISICA, NO UN RELOJ. Cuando la deriva baja del umbral el
    // pursuit retoma en el acto. Eso es lo que fallo del aflojo: 1.5 s de reloj a 107 km/h
    // son 45 m de linea abandonada, y el costo escalaba con la velocidad mientras el
    // beneficio no. Aca no hay nada que sostener.
    private float Contravolante(float base, float kmh)
    {
        if (kmh < DERIVA_MIN_KMH) return base;
        if (!m_Manual) return base;
        if (m_Manual.steerMaxRad < 0.05) return base;
        // ✖✖ LA ANTICIPACION SE RETIRO. Estaba construida sobre `decreaseSpeed` del config
        // leido como grados/segundo de rueda, y eso es FALSO: medido del `front_wheel_deg` de
        // las tomas humanas, el volante va a 120 deg/s a baja velocidad y 32-38 a alta, o sea
        // ~2x lo que el config declara, Y ES IGUAL EN LOS TRES AUTOS. O sea que anticipaba el
        // doble de lo que corresponde y diferenciaba dos vehiculos que no se diferencian --
        // que es exactamente la prediccion mia que fallo al reves.
        // Nunca se midio aislada, su premisa es falsa y su magnitud esta mal: se va antes de
        // certificar el controlador, no despues.
        float mag = Math.AbsFloat(m_Deriva);
        if (mag <= DERIVA_UMBRAL_RAD) return base;
        // Fuera de dominio: se devuelve el comando de pursuit, que es la BASE MEDIDA (ley
        // gentil + cap de agarre), no una accion nueva. Lo unico que se retira es el
        // contravolante -- que pasados los 90 grados no recupera nada y SOSTIENE el trompo.
        // Esto se justifica sin datos de resultado: la identidad geometrica no tiene dominio
        // ahi. El freno, que si era una apuesta, se retiro tras romper al GT2RS.
        if (mag >= DERIVA_SIN_FRENTE) return base;
        // ✖ LA RAMPA SE FUE. La justifique diciendo que evitaba el golpe de volante de un
        // escalon duro, y ESE ARGUMENTO ERA FALSO: el motor no puede dar un golpe de volante.
        // `decreaseSpeed` lo limita a 15-20 deg/s a 100 km/h, o sea que el suavizado ya lo
        // hace el actuador y la rampa solo agregaba retardo -- mantenia el contravolante en
        // 2.4 deg con deriva 12 y en 7.5 con deriva 15, justo la ventana en que el
        // deslizamiento compone, y recien pedia el comando pleno cuando ya era tarde.
        // ★ Y el mismo actuador lento que nos come el onset nos cubre el castañeo: si la
        // deriva oscila alrededor del umbral, la rueda no puede seguir el toggle. La lentitud
        // que duele de un lado protege del otro.
        // ⚠⚠ EL CAP DE AGARRE NO SE LE APLICA, Y NO ES UN OLVIDO. A 107 km/h el cap vale
        // ~0.06-0.11 y el contravolante que hace falta para 21 grados de deriva es 0.73:
        // capearlo lo dejaria en el 12% de lo necesario, o sea muerto. El cap contesta
        // "cuanta curvatura dan las gomas SIGUIENDO un camino"; el contravolante no pide
        // curvatura, pide alinear las ruedas con la marcha. Acotarlo con un presupuesto de
        // fuerza lateral es un error de categoria.
        float cs = Math.Clamp(m_Deriva / m_Manual.steerMaxRad, -1.0, 1.0);
        return cs;
    }

    // ✖✖ EL FRENO SE FUE, Y EL RIESGO QUE LO ANUNCIABA SE CUMPLIO. Frenar pasados los 90
    // grados mato la cola de 100 m (GT63 119->36, Huracan 136->17) PERO rompio al GT2RS:
    // 2.5/1.6/2.2 -> 14.2/31.6/46.0, las tres volando. Sus excursiones a 82-108 grados eran
    // TRANSITORIAS y se recuperaban solas; frenarlas tiro la recuperacion que funcionaba.
    // ★ LA LECCION: el NIVEL no distingue transitorio de sostenido. Las catastrofes tenian
    // 709 y 706 eventos contra <=154 en TODAS las demas -- lo que separa es la PERSISTENCIA.
    // Un gate por persistencia necesita un numero que todavia no esta medido, asi que no se
    // inventa: se retira el freno y queda solo el arreglo de DOMINIO, que se justifica solo.
    private float AcotarVolantePorAgarre(float cmd, float kmh)
    {
        if (!m_UsoPursuit) return cmd;
        if (!m_Pursuit) return cmd;
        if (!m_Manual) return cmd;
        if (m_Manual.wheelbaseM < 0.5) return cmd;
        if (m_Manual.steerMaxRad < 0.05) return cmd;
        float kMax = m_Pursuit.KMaxCmd();
        if (kMax <= 0.0001) return cmd;
        // Angulo CINEMATICO de la curvatura ya capada: sin dividir por el subviraje. Pedir
        // mas angulo que esto no compra curvatura -- compra deriva. Ver.
        // *** PASO 3: el cap es el angulo que LOGRA kMax con el subviraje puesto, no el
        // cinematico. El cinematico capaba la rueda en 0.53 grados a 199 km/h -- el
        // humano uso 2.84-3.85 en ese mismo metro. Capear el angulo cinematico no limita
        // la curvatura a kMax: la limita a kMax*ku (~20-26%% del presupuesto). Con el
        // subviraje adentro, cap y comando quedan en la MISMA unidad: el angulo de rueda
        // que consigue esa curvatura a esta velocidad.
        float kuCap = m_Manual.UndersteerK(kmh);
        if (kuCap < 0.05) kuCap = 0.05;
        if (kuCap > 1.0) kuCap = 1.0;
        float strGrip = Math.Atan(m_Manual.wheelbaseM * kMax / kuCap) / m_Manual.steerMaxRad;
        if (strGrip <= 0.001) return cmd;
        return Math.Clamp(cmd, -strGrip, strGrip);
    }

    private void RefrescarVolante(BZVehicleState st, float dt)
    {
        m_KRefresco = 0;
        if (!m_UsoPursuit) return;              // los modulos POSE mandan su propio volante
        if (!m_Pursuit) return;
        if (!m_Manual) return;
        if (!m_Leg) return;
        if (!m_Leg.valid) return;
        // ⚠ POR DEBAJO DEL CREEP EL REFRESCO NO CORRIGE: INYECTA.
        // El termino existe para corregir el desvio acumulado DENTRO de un tick de Plan. A
        // 0.4 km/h el vehiculo avanza 5 cm en ese medio segundo -- no hay desvio que
        // corregir-- pero el angulo al punto de mira sigue siendo grande, asi que el
        // comando sale a fondo. Medido en AERO: en modo 2 a 0.40 km/h el volante pedia
        // -0.398 contra un tope de autoridad de 0.40, y el lateral derivaba -0.196 ->
        // -0.310 antes siquiera de entrar al endpoint. Despues el slam creepeaba con el
        // vehiculo ya torcido y el endpoint terminaba en 1.4 m.
        //
        // El umbral no se elige: es idleCreepKmh (lo MEDIDO; ), el mismo que ya define "no rueda"
        // y con el que SteerAuthority ya recorta el volante. Por debajo de el, el controlador YA
        // considera degradada la direccion; el refresco no tiene por que opinar ahi.
        // El volante APLICADO del tick anterior: el discriminante del headroom del
        // corrector N2. Se informa ANTES del early-return por creep para
        // que el 2 Hz tambien lo vea fresco.
        m_Pursuit.SetVolanteAplicado(m_Out.steering);
        float minKmh = m_Manual.idleCreepKmh;
        if (minKmh < 1.0) minKmh = 1.0;
        if (st.chassisKmh < minKmh) return;
        // El aim se adelanta ANTES de medir la curvatura desde la pose: si no, este tick
        // seguiria usando el punto que el Plan dejo hace hasta medio segundo.
        m_Pursuit.RefrescarMiraA40(m_Leg, m_Manual, st.pos, st.courseRad, m_Leg.isReverse, st.chassisKmh, dt);
        float k = m_Pursuit.CurvaturaDesdePose(st.pos, st.yaw, m_Leg.isReverse, m_Manual);
        m_KRefresco = k;
        // El feedforward es la curvatura del CAMINO (el arco); el resto es correccion.
        // Ver BZVehicleManual.SteerCommandSplit: sólo el primero lleva el subviraje.
        float kFF = m_Pursuit.LastKArco();
        // ★ Durante la transicion del embrague la correccion se CONGELA: queda solo la
        // cinematica. Si no, el control caza el pateo del cambio y de ahi nace el fly-off.
        float kCorr = k - kFF;
        // ✖✖ ENTIERRO DEL HOLD: el freeze
        // como SOSTENER holdeaba STALE el corrector a traves de curvas encadenadas y
        // arruino la media (ZERO 0.38 / HOLD 0.68 / +debounce 0.61, MAX 8.36 el peor).
        // El ZERO original -- zerear la correccion durante el embrague -- tiene el
        // mejor promedio: zerear un corrector CHICO no duele (el regimen para el que
        // el freeze se disenio), y los 4 picos de curva donde la correccion era grande
        // los cierra LA CINTA (E2/E3, 563), no otro policy del freeze.
        // NO resucitar el hold sin refutar la tabla.
        if (CongelarCorreccion(st)) kCorr = 0;
        float cmd = m_Manual.SteerCommandSplit(kFF, kCorr, st.kmh);
        if (m_Goal.dir < 0) cmd = -cmd;
        cmd = Math.Clamp(cmd, -1.0, 1.0);
        // ⚠⚠⚠ EL CAP DE AGARRE TIENE QUE LLEGAR AL VOLANTE, NO QUEDARSE EN LA CURVATURA.
        //
        // Medido: `K-CLAMP` activo, `kmax 0.0297` --el cap de
        // agarre exacto-- y sin embargo `str -0.460`. La curvatura estaba capada; el volante
        // no. Y no era una fuga por un costado: es el MODELO DE SUBVIRAJE.
        //
        //     SteerCommandForCurvature: delta = atan(batalla * kappa / UndersteerK(kmh))
        //
        // Con UndersteerK ~0.26 a esa velocidad multiplica el angulo por casi 4. La
        // compensacion es CORRECTA dentro de la envolvente de agarre --el auto necesita mas
        // angulo para la misma curva-- y CONTRAPRODUCENTE en el limite: ese angulo de mas es
        // angulo de DERIVA, y pedirlo cuando ya no queda agarre no dobla mas, DESLIZA mas.
        // Que es exactamente el cruce que veniamos persiguiendo.
        //
        // ✖ El tope es el angulo CINEMATICO de la curvatura ya capada --sin dividir por el
        // subviraje--: `atan(batalla * kappaMax)`. En el tick medido da 0.122 donde el
        // comando salia 0.46.
        //
        // ★ Y ACOTA, NO REEMPLAZA: por debajo del tope la compensacion de subviraje sigue
        // intacta, que es la que hace trazar bien en el rango normal. Solo se corta donde
        // pedir mas angulo ya no compra curvatura.
        cmd = AcotarVolante(cmd, st);
        float aut = SteerAuthority(st);
        m_TargetSteer = Math.Clamp(cmd, -aut, aut);
    }

    // El tope de slew de ESTE vehiculo: la cremallera fisica (~80 deg/s sostenidos, flota
    // entera) normalizada por SU steerMax. Un comando mas rapido que la rueda no gira mas:
    // se ADELANTA, y al invertir tiene que desenrollar la distancia que se adelanto -- el
    // "volante al lado viejo con hErr ya cruzado" del rebote del r32 (x2.3 de adelanto
    // medido). El bus (steerMax chico) queda casi igual (-13%); los de tope grande bajan.
    private float SlewPorVehiculo()
    {
        if (!m_Manual) return STEER_SLEW_PER_S;
        if (m_Manual.steerMaxRad < 0.05) return STEER_SLEW_PER_S;
        float s = RUEDA_RATE_RAD_S / m_Manual.steerMaxRad;
        if (s > STEER_SLEW_PER_S) s = STEER_SLEW_PER_S;   // techo: nunca mas rapido que hoy
        return s;
    }

    private void SteerSlew(BZVehicleState st, float dt)
    {
        float maxStep = SlewPorVehiculo() * dt;
        if (maxStep <= 0) return;
        float cur = m_AppliedSteer;
        float d = m_TargetSteer - cur;
        if (d > maxStep) d = maxStep;
        if (d < -maxStep) d = -maxStep;
        m_AppliedSteer = cur + d;
        m_Out.steering = m_AppliedSteer;
    }
}
