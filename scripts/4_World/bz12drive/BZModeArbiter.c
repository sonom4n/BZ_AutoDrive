// ============================================================================
//  BZModeArbiter.c - BZ_AutoDrive 1.2
//
//  Decide QUIEN maneja el frame. Nada mas. En v1.1 esto era un hook preemptivo
//  enterrado en DriveTowards decidiendo con proxies geometricos que mienten; aca
//  es una state machine explicita, de primera clase y testeable sin juego.
//
//  DOS PROPIEDADES QUE NO SE NEGOCIAN:
//
//  1. LA TABLA ES TOTAL. Todo par (estado, condicion) que no matchea cae en un
//     fallback EXPLICITO -- STAY (input raro pero coherente) o SAFE_STOP (input
//     incoherente). Decide() NUNCA devuelve un modo invalido ni "el anterior por
//     accidente". Un arbitro parcial tiene comportamiento IMPLICITO en los huecos,
//     y ese fue el modo de falla de v1.1: el hook no decidia mal, NO DECIDIA.
//
//  2. LA CADENA DE DESACELERACION ES COMPROMETIDA (one-way). Una vez en APPROACH
//     no se vuelve a CRUISE libremente: se sale COMPLETANDO la cadena
//     (approach -> maniobra -> endpoint -> leg nuevo / HALTED) o por SAFE_STOP.
//     Sin esto el borde de zona CHATEA: la distancia de frenado cruza el umbral de
//     ida y de vuelta con el ruido del velocimetro y el modo flip-flopea. Es el
//     mismo 2<->0 de los cusps de v1.1, que costo semanas.
//     Refuerzos: umbral de ENTRADA != umbral de SALIDA, y dwell minimo por modo.
//
//  NO llama a los modulos de manejo ni lee su estado. Decide con (st, leg, manual)
//  y su propio modo. Que el ENDPOINT termino se lo AVISA el Controller
//  (NotifyArrived) -- preguntarselo al slam seria leer el estado de otro modulo.
// ============================================================================

class BZArbFallback
{
    static const int NONE      = 0;
    static const int STAY      = 1;   // input raro pero coherente: se conserva el modo
    static const int SAFE_STOP = 2;   // input incoherente: se detiene, no adivina
}

class BZModeArbiter
{
    // Ticks minimos en un modo antes de aceptar un cambio. A 2 Hz, 2 ticks = 1 s.
    // Es anti-chatter puro: absorbe el cruce de umbral por ruido, no tunea manejo.
    private const int MIN_DWELL = 2;
    // Histeresis de salida de MANIOBRA: hay que alejarse 1.5x la zona de entrada.
    private const float EXIT_FACTOR = 1.5;
    // Ticks de Plan() (2 Hz) que MANIOBRA puede quedarse en un leg cuyo final esta lejos
    // antes de admitir que no es el modulo indicado. 20 ticks = ~10 s.
    private const int   MANEUVER_MAX_DWELL = 20;
    // Zona de maniobra y de endpoint en WHEELBASES (geometria del vehiculo, no un numero).
    private const float MANEUVER_WB = 3.0;
    // Muestras de curvatura sobre la ventana del seguidor. Seis alcanzan: la ventana es de
    // dos batallas y la curvatura de una ruta grabada no cambia de signo dentro de eso.
    private const int   CURV_MUESTRAS = 6;
    // ⚠⚠ BANDERA DE BANCO. NO ES PARTE DE LA LEY Y NO SE USA EN NINGUNA CORRIDA NORMAL.
    //
    // Existe para UN experimento que aisla una variable: el pursuit NUNCA manejo una curva de
    // reversa --se la come la maniobra-- asi que no sabemos si su geometria espejada
    // (`refYaw + PI`) funciona. Sin ese dato, rediseñar el techo de capacidad seria a ciegas.
    //
    // Medido (ZiS5, reversa wp 215-260): la curvatura REAL del tramo es 0.240 (R = 4.17 m) y
    // el `cap` derivado del steerMax FORWARD es 0.196. La toma PRUEBA que ese vehiculo hizo
    // 0.240 en reversa: mas cerrado que su limite forward. Gatear contra el techo forward un
    // camino que el vehiculo real ya recorrio es un error de categoria -- **una toma grabada
    // es, por definicion, seguible**. Pero antes de cambiar la vara hay que saber si el
    // pursuit sirve para esa curva.
    //
    // La bandera SOLO apaga la compuerta de CAPACIDAD. La de VISIBILIDAD queda: sin ella se
    // le pediria al pursuit seguir los ultimos metros que no entran en su horizonte, y eso ya
    // sabemos que no puede.
    private static bool s_ForzarPursuitRev = false;
    private ref BZPathSample m_S;

    private ref BZPathSample m_S0;
    private float m_LogCap;
    private float m_LogPeor;
    private const float ENDPOINT_WB = 1.0;
    private const float ENDPOINT_MIN_M = 2.0;
    // ZONA DEL SLAM POR FISICA: v^2/(2 aMax) mas un cuarto. El cuarto es la reserva: al armar, parar en el punto pide
    // el 80 % del freno del manual; el retardo medido del pedal (0,1 s a fondo antes de que la desaceleracion llegue)
    // son 0,55 m a 20 km/h, el 15-20 % de esa distancia. Medido en las tres tomas de referencia: con el cuarto la que
    // se paso 0,40 m arma antes y la parada queda corta (el CARRY cierra); las dos que llegaron bien arman en el
    // mismo cuadro de siempre.
    private const float MARGEN_FRENO = 0.25;
    // Latencia de decision: un tick del lazo lento. La distancia recorrida en ese tiempo
    // es el margen que hay que sumarle a la distancia de frenado.
    private const float TICK_DT_S = 0.5;
    // STALL POR ARCO-PROGRESO. Mide lo que IMPORTA (avanzar sobre la ruta), no un proxy
    // (tener velocidad). Dos razones, las dos aprendidas in-game:
    //   - el run 2 se trabo en CRUISE y el run 3 se trabo en MANIOBRA: gatear el detector
    //     por modo deja agujeros. El arco no tiene modos.
    //   - el creep legitimo tiene velocidad casi nula pero SI avanza; medir velocidad lo
    //     falso-positivearia, medir arco no.
    // 40 ticks a 2 Hz = 20 s. Menos de 0.3 m de arco en 20 s no es manejar despacio: es
    // estar trabado. (Run 3: 0.03 km/h = 0.008 m/s -> 0.17 m en 20 s.)
    private const int   STALL_TICKS = 40;
    private const float STALL_MIN_M = 0.3;

    private int   m_Mode;
    private int   m_DwellTicks;
    private int   m_Fallback;
    private int   m_ChainLeg;      // leg en el que arranco la cadena comprometida
    private int   m_StallTicks;    // ticks seguidos sin progreso de arco
    private float m_LastS;         // arco del cursor en el ultimo avance real
    private bool  m_Committed;

    void BZModeArbiter()
    {
        Reset();
    }

    void Reset()
    {
        m_Mode = BZDriveMode.BZDM_IDLE;
        m_DwellTicks = 0;
        m_Fallback = BZArbFallback.NONE;
        m_ChainLeg = -1;
        m_Committed = false;
        m_StallTicks = 0;
        m_LastS = -1.0;
    }

    int  GetMode()     { return m_Mode; }
    int  GetFallback() { return m_Fallback; }
    bool IsCommitted() { return m_Committed; }

    // El Controller avisa que el endpoint clavo. El arbitro NO se lo pregunta al slam.
    void NotifyArrived()
    {
        EnterMode(BZDriveMode.BZDM_HALTED);
    }

    // Resume() del Runner tras una parada semantica: la cadena se libera.
    void NotifyResume(BZLegView leg)
    {
        m_Committed = false;
        m_LastS = -1.0;
        m_StallTicks = 0;
        m_ChainLeg = -1;
        // ⚠ ACA NO HAY st NI m, y el criterio de reversa los necesita (velocidad para la
        // capacidad, batalla para la ventana). Se pasa por IDLE y el proximo Decide --que si
        // los tiene-- evalua. No queda un tick sin modo: Decide sale de IDLE en la MISMA
        // llamada, antes de que el controlador actue.
        //
        // Y este era el camino que importaba: NotifyResume corre tras una parada semantica,
        // que es exactamente como empieza un leg de reversa despues del stop del leg anterior.
        // Con el StartModeFor viejo, la reversa entraba a MANIOBRA aca sin evaluar nada.
        EnterMode(BZDriveMode.BZDM_IDLE);
    }

    void ForceHalt()
    {
        m_Committed = false;
        EnterMode(BZDriveMode.BZDM_HALTED);
    }

    // ======================= LA DECISION =======================

    // ⚠⚠ EL ORDEN DE ESTA FUNCION CARGA PESO. NO REORDENAR. ⚠⚠
    //
    //   (1) SEGURIDAD  -> (2) maquina de estados -> (3) debounce
    //
    // La compuerta de seguridad va PRIMERO y BYPASSEA el dwell Y el compromiso one-way.
    // El debounce y el one-way existen para el FLUJO NORMAL (cruise/approach/maniobra/
    // endpoint), donde el enemigo es el ruido en un umbral. La seguridad es otra cosa:
    // si el estado se vuelve incoherente a mitad de un dwell, esperar los 2 ticks seria
    // MANEJAR 1+ TICKS SOBRE ESTADO INVALIDO. La seguridad siempre gana y es INMEDIATA.
    //
    // La compuerta esta EXTRAIDA Y NOMBRADA (Coherent) a proposito: asi el orden es
    // visible en la firma de la funcion y moverla abajo del debounce se ve en el diff.
    // Hay un test que lo fija (TestSafeStopPreemptsDwell) y esta verificado con mutacion:
    // con la compuerta despues del debounce, el arbitro se queda 1 tick de mas.
    int Decide(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        m_Fallback = BZArbFallback.NONE;
        // (1) SEGURIDAD -- gana sobre todo, incluso sobre el dwell y el compromiso.
        if (!Coherent(st, leg, m)) return SafeStop();
        // (2) MAQUINA DE ESTADOS
        if (m_Mode == BZDriveMode.BZDM_IDLE) return EnterMode(StartModeFor(st, leg, m));
        if (m_Mode == BZDriveMode.BZDM_HALTED) return Stay();   // solo sale por NotifyResume
        if (Stalled(leg)) return SafeStop();
        int next = NextMode(st, leg, m, surfKind);
        ReportarRuteoReversa(st, leg, m, next);
        if (next == m_Mode) return Stay();
        // (3) DEBOUNCE -- FILA 10 (fallback STAY). Solo para el flujo normal: llegar
        // aca ya implica que el estado es coherente.
        // ⚠⚠⚠ CON UNA EXCEPCION: LA ENTRADA AL ENDPOINT ES TERMINAL.
        // El debounce existe para el RUIDO EN UN UMBRAL entre modos que van y
        // vienen; el ENDPOINT no vuelve (NextMode en ENDPOINT devuelve ENDPOINT
        // siempre: es absorbente), asi que el dwell no lo protege de nada --
        // solo le impide entrar A TIEMPO, y llegar tarde al punto de parada no
        // se recupera nunca. Medido en 16 corridas del M4tractor (6 configs):
        // el arbitro sale de MANEUVER a ~3 m del final y elige ENDPOINT (clava
        // 0.07-0.09) o CRUISE; cuando elige CRUISE, EnterMode resetea el dwell
        // y los 2 ticks de MIN_DWELL son 1 s = 4.3 m a 16 km/h -- MAS que la
        // zona de endpoint entera (3.2 m). El vehiculo la cruza con la cinta
        // al mando y para donde puede (0.27-0.48 m). Las 3 corridas que SI
        // llegaron a ENDPOINT desde CRUISE entraron 1.55-1.58 s tarde, ya
        // detenidas a 0.3 m. Es el mismo argumento de la compuerta de
        // seguridad de arriba: esperar el dwell seria manejar ticks sobre un
        // estado que ya cambio. Falso positivo imposible de sostener: 0 casos
        // de ENDPOINT espurio en ~64 cusps intermedios medidos.
        if (next != BZDriveMode.BZDM_ENDPOINT && m_DwellTicks < MIN_DWELL)
        {
            m_Fallback = BZArbFallback.STAY;
            return Stay();
        }
        return EnterMode(next);
    }

    // ★★★ LA ENTRADA AL ENDPOINT TAMPOCO SE MUESTREA A 2 Hz. La
    // decision de modo vive en Plan (dt 0.5); a 16 km/h eso son 2.2 m por tick
    // contra una zona de endpoint de ~3.2 m: el instante de entrada queda
    // sorteado por la fase del muestreo, y con el la velocidad de llegada
    // (modo 5 => 9.2-9.9 km/h a 1 m => clava; modo 2 => 13.6-15.4 => no).
    // El controller llama a esto en Sample (40 Hz) con la pose de ESTE frame:
    // MISMO predicado (leg.remainM, nunca distancia euclidiana -- en un
    // parking la euclidiana mezcla pasadas vecinas), misma compuerta de
    // coherencia, y solo desde los modos que MANEJAN. Es el mismo precedente
    // que EndpointFrameBrake ("a 2 Hz Boris cruza el punto con envion; a 40 Hz
    // para EN el punto"): ahi se refresco el FRENO, aca la ENTRADA. Monotono:
    // solo puede ADELANTAR una transicion que el arbitro iba a hacer igual.
    //
    // ⚠⚠⚠ CORRECCION: LO DE ARRIBA ES FALSO EN SU MITAD IMPORTANTE.
    // La LLAMADA quedo a 40 Hz; la CANTIDAD que lee, no. `leg.remainM` sale del
    // cursor del leg-machine, y `m_Leg` se refresca en UNA sola linea de todo el
    // arbol: la primera de Plan() (BZDriveController.c:842), a 2 Hz. O sea que
    // las 20 llamadas entre dos ticks leen el MISMO remainM viejo y la compuerta
    // solo puede cambiar de estado en un tick de Plan: LA ENTRADA SIGUE
    // MUESTREADA A 2 Hz. Medido en las 6 corridas del M4: la entrada
    // se dispersa 2.20 m (1.33 -> 3.53) contra un paso de grilla de 2.22 m a
    // 16 km/h -- coincide a un centimetro, es el sorteo intacto. Las 3 que se
    // pasan (+0.15 a +0.35) entran 1.74-1.81 m tarde; las 3 que clavan entran
    // en la zona (3.205 m = 1.0 x wheelbase).
    // Lo que SI arreglo fue la otra mitad: la excepcion del debounce
    // (`next != ENDPOINT` saltea MIN_DWELL), que mato el caso "nunca entra".
    // El cierre pendiente esta pre-registrado en: comparar contra
    // `min(leg.remainM, RemanenteFrameM)` -- monotono, geometrico, sin perillas.
    // NO se escribio todavia porque el banco dio 0/6: `remainM` no se logueaba
    // y no se puede reproducir la compuerta offline. Por eso las columnas
    // `rem` y `remFr` del CSV. El banco decide, no la teoria.
    // ★★★ EL CIERRE: `remAhoraM` es el remanente del leg CORREGIDO POR EL
    // AVANCE HECHO DESDE EL ULTIMO REFRESCO -- lo calcula el controller, que es el unico
    // que ve la pose de cada frame. Con eso la compuerta deja de leer una foto de hasta
    // 0,5 s de antiguedad y la entrada deja de sortearse.
    //
    // ✖ NO es `min(leg.remainM, remanenteDelFrame)`, que es lo que yo mismo pre-registre
    // en y el analista simulo con exito. Medido sobre las 6 corridas reales:
    // `remainM` es ARCO y el remanente del frame es una PROYECCION RECTA sobre la tangente
    // final, y difieren hasta 18-35 m en tramos curvos. En la ruta del M4 eso nunca muerde
    // porque el ultimo tramo llega derecho -- pero en un PARKING el vehiculo pasa por la
    // MISMA ESTACION LONGITUDINAL del punto final con varios metros de recorrido todavia
    // por delante, y ahi la proyeccion vale ~0 con el arco lejos: el `min` crudo meteria
    // ENDPOINT de una. Es la misma trampa que la distancia euclidiana en parkings, que ya
    // nos mordio una vez (mezcla pasadas vecinas).
    // El controller manda el remanente corregido por un DELTA de 0,5 s, y la diferencia
    // arco-vs-cuerda se CANCELA porque los dos terminos del delta miden contra la misma
    // pose final. Cota ESTRUCTURAL: el descuento nunca supera el avance de un tick.
    // Verificado en las 30 legs de las 6 corridas (12 de ellas en REVERSA): el adelanto
    // maximo es 0,47 s contra el tick de 0,50 -- corrige el muestreo, no cambia la ley.
    //
    // Monotono: `remAhoraM <= leg.remainM`, asi que solo puede ADELANTAR una transicion
    // que el arbitro iba a hacer igual; nunca atrasarla ni inventar una nueva.
    bool PromoverEndpoint(BZVehicleState st, BZLegView leg, BZVehicleManual m, float remAhoraM, int surfKind)
    {
        if (m_Mode == BZDriveMode.BZDM_ENDPOINT) return false;
        if (m_Mode != BZDriveMode.BZDM_CRUISE && m_Mode != BZDriveMode.BZDM_APPROACH && m_Mode != BZDriveMode.BZDM_MANEUVER) return false;
        if (!Coherent(st, leg, m)) return false;
        if (!LlegoLaHoraDelSlamCon(st, leg, m, remAhoraM, surfKind)) return false;
        EnterMode(BZDriveMode.BZDM_ENDPOINT);
        return true;
    }

    // Compuerta de SEGURIDAD. Todo lo que hace que este tick no se pueda manejar.
    private bool Coherent(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        if (!st) return false;
        if (!leg) return false;
        if (!m) return false;
        if (!leg.valid) return false;
        if (!st.IsCoherent()) return false;
        return true;
    }

    private int NextMode(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        if (m_Mode == BZDriveMode.BZDM_CRUISE)   return FromCruise(st, leg, m, surfKind);
        if (m_Mode == BZDriveMode.BZDM_APPROACH) return FromApproach(st, leg, m, surfKind);
        if (m_Mode == BZDriveMode.BZDM_MANEUVER) return FromManeuver(st, leg, m, surfKind);
        if (m_Mode == BZDriveMode.BZDM_ENDPOINT) return BZDriveMode.BZDM_ENDPOINT;
        return m_Mode;
    }

    // ⚠⚠⚠ EL RUTEO DE REVERSA. Antes era `if (leg.isReverse) -> MANIOBRA`, incondicional, en
    // LAS TRES puertas -- entrada desde CRUISE, entrada desde APPROACH, y la SALIDA. La
    // tercera era un latch absoluto: un leg de reversa que entraba a MANIOBRA no salia nunca.
    //
    // Medido con la marca PURSUIT-OFF: el leg de reversa del ZiS5 daba 40/40 ticks sin
    // pursuit, contra 25/100 y 14/112 en los forward. O sea que el forward YA usa el split
    // --maniobra cerca de los finales-- y la reversa era el unico regimen con atajo.
    //
    // ✖ Y NO SE COPIA EL CRITERIO DEL FORWARD (`cerca del fin del leg`): es POSICIONAL, y en
    // reversa lo tight aparece donde quiere -- arrancando ya girando, o en un tramo del medio.
    // Un criterio posicional mandaria al pursuit un tramo cerrado lejos del fin, y a maniobra
    // un tramo largo y suave que esta cerca. No transfiere.
    //
    // El criterio es GEOMETRICO y sale del vehiculo, con dos compuertas duras:
    //
    //   (a) CAPACIDAD    -- el tramo pide mas curvatura de la que el seguidor puede comandar.
    //                       No es "seguiria peor": es que satura el volante y el lazo queda
    // ABIERTO. Se pregunta al propio pursuit para no
    //                       duplicar el numero del clamp.
    //   (b) VISIBILIDAD  -- lo que resta es mas corto que el piso del lookahead (2 batallas).
    //                       El punto de mira ya esta del otro lado: no puede seguir una curva
    //                       que no entra en su horizonte.
    //
    // Ninguna de las dos es una constante tuneada, y ninguna mira DONDE esta el tramo.
    //
    // ✖ Se descarto un umbral sobre el corte a lazo abierto (eps = Ld^2*k/8): el lazo suprime
    // la mayor parte y por cuanto NO es un factor limpio.
    // Convertirlo en umbral exige un factor de ajuste, que es la constante tuneada que se
    // queria evitar. Estas dos no dicen "cuanto error habria": dicen PUEDE / NO PUEDE.
    private bool ReversaNecesitaManiobra(BZVehicleState st, BZLegView leg, BZVehicleManual m, bool saliendo)
    {
        if (!leg || !leg.valid || !m) return true;      // sin contexto, el conservador es MANIOBRA
        float ventana = BZPurePursuit.PisoLookaheadM(m);
        // ⚠⚠ LA HISTERESIS VA EN LA DISTANCIA, **NO** EN LA CURVATURA. Lo tenia en las dos y
        // era el segundo motivo del 39/39.
        //
        // `EXIT_FACTOR = 1.5` es una constante con FORMA DE DISTANCIA -- el forward la usa
        // asi (`alongM < ManeuverZone * EXIT_FACTOR`). Aplicada a una curvatura abre una
        // banda del 50%: se entra en `cap` y se sale recien por debajo de `cap/1.5`. Medido
        // en el ZiS5: cap ~0.149 -> umbral de salida 0.099, y la curvatura MEDIANA de esa
        // reversa es 0.099. O sea que una vez adentro no salia nunca.
        //
        // Es el mismo error que la compuerta de endpoint atada a la ventana del FF: una
        // constante de un dominio aplicada a otro. Y ademas era REDUNDANTE: el arbitro ya
        // debouncea TODO cambio de modo con `MIN_DWELL`, asi que el chattering ya estaba
        // cubierto sin abrir ninguna banda.
        float histDist = 1.0;
        if (saliendo) histDist = EXIT_FACTOR;
        // (b) visibilidad -- distancia, aca EXIT_FACTOR es del dominio correcto
        if (leg.remainM < ventana * histDist) return true;
        // (a) capacidad -- mismo umbral en los dos sentidos; el debounce evita el chattering
        float cap = BZPurePursuit.CurvaturaUtilMax(m, st.chassisKmh);
        float peor = CurvaturaMaxAdelante(leg, m, ventana);
        m_LogCap = cap;
        m_LogPeor = peor;
        if (s_ForzarPursuitRev) return false;           // banco: ver s_ForzarPursuitRev
        if (cap <= 0.001) return false;                 // sin envelope no se decide por aca
        if (peor > cap) return true;
        return false;
    }

    // La curvatura mas alta que la ruta pide dentro de la ventana del seguidor. Se muestrea
    // por ARCO --no por indice de waypoint-- para que la densidad de la grabacion no cambie
    // el resultado.
    // ⚠⚠⚠ NO SE USA `sample.curvature`. ESE CAMPO NO SIRVE PARA ESTO.
    //
    // `BZRouteData` lo calcula entre waypoints ADYACENTES:
    //
    //     dh = WrapPi(Head(i) - Head(i-1));
    //     ds = S(i) - S(i-1);
    //     if (ds >= MIN_SEG_M) k = dh / ds;        // MIN_SEG_M = 0.01 m
    //     k = Math.Clamp(k, -MAX_CURV, MAX_CURV);  // MAX_CURV = 1.0
    //
    // Con la grabacion a 40 Hz, a 3-4 km/h los waypoints quedan a ~2.7 cm: UN GRADO de rumbo
    // sobre 2.7 cm da k = 0.65, y dos grados saturan el clamp. Medido en la reversa del ZiS5:
    // `kVentana` daba 0.84 / 0.96 / **1.0** --que es MAX_CURV, o sea el estimador saturado--
    // contra una curvatura real de 0.099-0.136. Siete veces inflada, y la compuerta de
    // capacidad disparaba 38 de 38 ticks.
    //
    // ✖ NO era el espejado de la reversa: `Curv(i)` sale de la geometria del CAMINO y no mira
    // el rumbo del vehiculo. Es densidad de muestreo, y por eso aparece en reversa --que es
    // lenta-- y no en el forward. Misma familia que el piso de ruido angular de `Head(i)`,
    // pero 15x peor porque el espaciado es 15x mas chico.
    //
    // Se mide sobre un ARCO, igual que `CurvaturaDelArco` del pursuit, y con la MISMA escala
    // que el seguidor usa para su feedforward (`VentanaFFM`): si el camino tiene detalle mas
    // fino que eso, el seguidor no lo ve, asi que el criterio tampoco debe verlo.
    //
    // ⚠ `SampleAt` acota a la ventana del leg: cerca del final el segundo punto se pega al
    // primero y la curvatura sale sub-medida. No importa -- ahi ya manda la compuerta de
    // visibilidad.
    private float CurvaturaMaxAdelante(BZLegView leg, BZVehicleManual m, float distM)
    {
        if (distM <= 0.1) return 0;
        if (!m_S) m_S = new BZPathSample();
        if (!m_S0) m_S0 = new BZPathSample();
        float baseM = BZPurePursuit.VentanaFFM(m);
        if (baseM < 0.5) baseM = 0.5;
        float paso = distM / CURV_MUESTRAS;
        float peor = 0;
        int i;
        for (i = 0; i <= CURV_MUESTRAS; i++)
        {
            float d0 = paso * i;
            // ⚠⚠ NUNCA MEDIR A TRAVES DEL FINAL DEL LEG. Head(i) es la direccion del
            // segmento i->i+1, asi que **el rumbo del ULTIMO waypoint del leg ya describe el
            // leg SIGUIENTE**. En un cusp eso es un giro de ~180 grados.
            //
            // Medido sobre ZiS5CLEAN: la ruta entera tiene exactamente DOS saltos de rumbo
            // mayores a 90 grados entre waypoints adyacentes -- wp 214 (169 deg) y wp 260
            // (177 deg), que son justo los dos cusps que abren y cierran el leg de reversa
            // (wp 215-260). Con base 3.0 m el arco alcanzaba el de wp 260 y devolvia
            // WrapPi ~ pi: k = pi/3 = 1.047. En el juego se midio 1.028.
            //
            // ✖ NO era la convencion de rumbo de la reversa:  sale de posiciones y es
            // continuo DENTRO del leg. Era medir por fuera de el. Y por eso con base 2.5 m no
            // aparecia y con 3.0 si: la unica diferencia era si el arco llegaba al cusp.
            //
            // Se pierden las muestras del final del leg, y esta bien: ahi ya manda la
            // compuerta de VISIBILIDAD, que existe justamente para esa zona.
            if (d0 + baseM > leg.remainM) continue;
            if (!leg.SampleAt(d0, m_S0)) continue;
            if (!leg.SampleAt(d0 + baseM, m_S)) continue;
            float dh = BZMath.WrapPi(m_S.headingRad - m_S0.headingRad);
            float k = Math.AbsFloat(dh) / baseM;
            if (k > peor) peor = k;
        }
        return peor;
    }

    // ---- CRUISE: el unico estado desde el que se entra a la cadena ----
    private int FromCruise(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        if (leg.isReverse && ReversaNecesitaManiobra(st, leg, m, false)) return Commit(BZDriveMode.BZDM_MANEUVER, leg);
        if (!SalteaZonaManiobra(leg) && LegEndAhead(leg, ManeuverZone(m))) return Commit(BZDriveMode.BZDM_MANEUVER, leg);
        // ✖ ACA FALTABA, Y POR ESO LA REGLA NO DISPARO NUNCA.
        // CRUISE solo sabia salir hacia APPROACH; la condicion de entrega al slam vivia en
        // FromApproach y FromManeuver. Con el vehiculo clavado a 0.27 km/h a 4.88 m del
        // final --las tres condiciones dadas-- el modo seguia siendo CRUISE, asi que el
        // predicado no se evaluaba siquiera. El espejo pasaba 26 ok porque probaba el
        // PREDICADO, no que se llegara a el.
        if (LlegoLaHoraDelSlam(st, leg, m, surfKind)) return Commit(BZDriveMode.BZDM_ENDPOINT, leg);
        if (leg.endIsStop && leg.remainM <= ApproachZone(st, m, surfKind)) return Commit(BZDriveMode.BZDM_APPROACH, leg);
        return BZDriveMode.BZDM_CRUISE;
    }

    // ---- APPROACH: COMPROMETIDO. No hay retorno a CRUISE, ni aunque la geometria
    //      diga que ya no hace falta frenar. Solo avanza en la cadena. ----
    private int FromApproach(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        if (leg.isReverse && ReversaNecesitaManiobra(st, leg, m, false)) return BZDriveMode.BZDM_MANEUVER;
        if (!SalteaZonaManiobra(leg) && LegEndAhead(leg, ManeuverZone(m))) return BZDriveMode.BZDM_MANEUVER;
        if (LlegoLaHoraDelSlam(st, leg, m, surfKind)) return BZDriveMode.BZDM_ENDPOINT;
        return BZDriveMode.BZDM_APPROACH;
    }

    // ---- MANIOBRA: se sale COMPLETANDO -- ya en un leg NUEVO, hacia adelante, fuera de
    //      la zona de entrada (con histeresis) y rodando. No por "ya no estoy cerca". ----
    private int FromManeuver(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        if (LlegoLaHoraDelSlam(st, leg, m, surfKind)) return BZDriveMode.BZDM_ENDPOINT;
        // ⚠ ESTA ERA EL LATCH. Con `saliendo = true` el predicado usa EXIT_FACTOR, la misma
        // histeresis que el forward: se entra con el umbral y se sale con el umbral aflojado,
        // asi el borde no hace ping-pong entre modulos.
        if (leg.isReverse)
        {
            if (ReversaNecesitaManiobra(st, leg, m, true)) return BZDriveMode.BZDM_MANEUVER;
            m_Committed = false;
            m_ChainLeg = -1;
            return BZDriveMode.BZDM_CRUISE;
        }
        // ★ SALIDA DE ULTIMA INSTANCIA (MODULES.md 8.2, a nivel de MODO).
        // Las dos compuertas de abajo -- arco recorrido y velocidad -- las SUPRIME el
        // propio MC cuando no logra mover el vehiculo. En la cuesta de SEDAN_SHORT bogaba
        // a 0.22 km/h: nunca recorria los ~11 m ni pasaba el creep, asi que el leg ENTERO
        // se quedaba en MANIOBRA (16/16 ticks medidos), que es justo el modulo que NO sabe
        // de pendiente. Compuerta suprimida por la accion que se toma mientras se espera.
        //
        // Criterio de la salida: si el final del leg NO esta cerca, la POSE no es el
        // problema -- y MANIOBRA es un modulo de pose. Se suelta a CRUISE, que si tiene
        // modelo de pendiente y perfil de velocidad. No rompe la cadena comprometida de
        // 8.0: esa es CRUISE->APPROACH->ENDPOINT, y aca justamente no hay nada cerca.
        if (!LegEndAhead(leg, ManeuverZone(m)) && m_DwellTicks > MANEUVER_MAX_DWELL)
        {
            m_Committed = false;
            m_ChainLeg = -1;
            return BZDriveMode.BZDM_CRUISE;
        }
        if (leg.legIdx == m_ChainLeg) return BZDriveMode.BZDM_MANEUVER;      // mismo leg: sigue
        if (leg.alongM < ManeuverZone(m) * EXIT_FACTOR) return BZDriveMode.BZDM_MANEUVER;
        if (st.chassisKmh < m.creepKmh) return BZDriveMode.BZDM_MANEUVER;    // todavia reptando
        m_Committed = false;
        m_ChainLeg = -1;
        return BZDriveMode.BZDM_CRUISE;
    }

    // ======================= zonas DERIVADAS =======================

    // Distancia de frenado + el margen de un tick de latencia. Sale del Manual: un bus y
    // un Willys frenan distinto y el arbitro lo sabe porque LEE, no porque alguien tuneo.
    private float ApproachZone(BZVehicleState st, BZVehicleManual m, int surfKind)
    {
        float v = st.kmh / 3.6;
        float a = m.MaxBrakeDecel(surfKind);
        if (a < 0.5) a = 0.5;
        float z = (v * v) / (2.0 * a) + v * TICK_DT_S;
        // PISO (auditoria post-run-10, MODULES.md 8.2): la zona sale de v^2/2a, o sea que
        // con el vehiculo DETENIDO vale CERO -- y entonces un Boris parado a pocos metros
        // del final de ruta no puede entrar nunca a la cadena approach->endpoint, aunque
        // este ahi nomas. La zona nunca puede ser menor que la del endpoint: si estas lo
        // bastante cerca como para clavar, tenes que poder ENTRAR.
        float piso = EndpointZone(m);
        if (z < piso) z = piso;
        return z;
    }

    private float ManeuverZone(BZVehicleManual m)
    {
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        return MANEUVER_WB * wb;
    }

    // ⚠ CRUISE NO PUEDE MANEJAR POR DEBAJO DEL CREEP, Y SIN EMBARGO SEGUIA MANEJANDO.
    //
    // La zona de endpoint era pura geometria: un wheelbase (2.93 m). Pero medido en AERO,
    // el vehiculo llegaba a 0.24 km/h con 4.86 m todavia por delante, o sea que arrastraba
    // ~1.9 m EN CRUISE por debajo del creep antes de que el slam enganchara. En ese tramo
    // el lateral se iba de -0.021 a -0.310 y el volante lo seguia hasta -0.404 (el tope de
    // autoridad a esa velocidad); despues el slam creepeaba con el vehiculo ya torcido y el
    // endpoint terminaba en 1.4 m -- con el `min visto` en 0.107 m, o sea que el punto lo
    // tenia bien y se pasaba de largo.
    //
    // Se agrega UNA condicion, con dos cantidades que ya existen y ya estan justificadas:
    //   - `creepKmh`: el umbral con el que el propio controlador declara que el vehiculo no
    //     rueda, y con el que SteerAuthority ya recorta el volante.
    //   - `2 x wheelbase`: el PISO del lookahead de pure pursuit, documentado como el limite
    //     por debajo del cual oscila. Adentro de esa distancia el modulo esta apuntando a un
    //     punto que ya es practicamente el endpoint.
    // Juntas dicen: si el tramo termina en parada, estamos adentro del piso geometrico de
    // pure pursuit Y el vehiculo ya no rueda, CRUISE no tiene nada que aportar. Que maneje
    // el modulo que existe para llegar a una pose.
    //
    // ✖ Alcance: NO cambia nada donde el vehiculo pasa con velocidad -- las dos
    // condiciones nuevas tienen que darse a la vez, y la de siempre sigue primera.
    // El camino de SIEMPRE (Decide, 2 Hz): el remanente del leg-machine es exacto en el
    // tick, asi que se evalua tal cual. Un solo predicado para los dos caminos.
    private bool LlegoLaHoraDelSlam(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind)
    {
        return LlegoLaHoraDelSlamCon(st, leg, m, leg.remainM, surfKind);
    }

    private bool LlegoLaHoraDelSlamCon(BZVehicleState st, BZLegView leg, BZVehicleManual m, float remM, int surfKind)
    {
        if (!leg.endIsStop) return false;
        if (remM <= EndpointZone(m)) return true;
        // EL SLAM SE ARMA CON LA DISTANCIA QUE PIDE LA VELOCIDAD REAL, no solo a distancia fija. La zona fija es la
        // batalla: a 20 km/h son 2,5 m y parar ahi pide 6 m/s2; un Hatchback da 5,2-5,6 con el pedal a fondo y se paso
        // 0,40 m (ROUTE_INCOMPLETE con el vehiculo congelado). La distancia de frenado es v^2/(2 aMax) con la MISMA
        // capacidad que el slam usa para dosificar el pedal (MaxBrakeDecel, del manual y la superficie), mas el
        // margen; el planeo (aFree) se ignora a proposito: sin el la zona es un poco mas larga, nunca mas corta.
        // Llegando lento la distancia de frenado es chica y manda la zona de siempre: nada cambia.
        float aMaxZ = m.MaxBrakeDecel(surfKind);
        if (aMaxZ >= 0.5)
        {
            float vMsZ = st.chassisKmh / 3.6;
            float dFrenoZ = (vMsZ * vMsZ) / (2.0 * aMaxZ) * (1.0 + MARGEN_FRENO);
            if (remM <= dFrenoZ) return true;
        }
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        if (remM > 2.0 * wb) return false;
        float creep = m.creepKmh;
        if (creep < 1.0) creep = 1.0;
        if (st.chassisKmh >= creep) return false;
        return true;
    }

    private float EndpointZone(BZVehicleManual m)
    {
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        float z = ENDPOINT_WB * wb;
        if (z < ENDPOINT_MIN_M) z = ENDPOINT_MIN_M;
        return z;
    }

    // El final de CUALQUIER leg es una POSE DECLARADA que hay que clavar antes de seguir,
    // no solo los cusps. Un legBreak significa "para, reseteá y arrancá de nuevo acá",
    // cambie o no el sentido de marcha (esa es la semantica del intercambio).
    //
    // Antes esto exigia nextIsReverse != isReverse. Con la ruta real eso fallaba: entre el
    // legBreak declarado y el cambio de marcha inferido queda un waypoint de por medio, y
    // el arbitro no veia ningun cusp -> Boris frenaba a 0 en CRUISE y se congelaba.
    //
    // El fin de RUTA y las paradas semanticas NO entran por aca: los maneja la cadena
    // approach -> endpoint.
    private bool LegEndAhead(BZLegView leg, float zone)
    {
        if (leg.endIsStop) return false;
        return leg.remainM <= zone;
    }

    // ======================= transiciones =======================

    // ⚠⚠ ESTA ERA LA CUARTA PUERTA, Y NO LA HABIA TOCADO.
    //
    // El atajo de reversa estaba en TRES lugares (FromCruise, FromApproach, FromManeuver) y
    // saque los tres. Pero el modo con el que un leg ARRANCA sale de aca, y aca seguia siendo
    // incondicional: la reversa entraba a MANIOBRA sin evaluar nada.
    //
    // Medido con el PBO 11:17: PURSUIT-OFF 39/39 en el leg de reversa -- TODO o nada, que es
    // la firma de entrar latcheado, no de una compuerta (una compuerta rutea PARCIAL).
    // Por que se decidio lo que se decidio, tick a tick, y SOLO en reversa. Sin esto la
    // unica forma de saber si mandaba una compuerta o el latch era inferirlo del PURSUIT-OFF,
    // y ese "todo o nada" ya nos costo una tanda.
    static void SetForzarPursuitRev(bool on) { s_ForzarPursuitRev = on; }

    // Con la bandera puesta, un leg de reversa tampoco cae por la regla GENERICA de zona de
    // maniobra (`LegEndAhead(ManeuverZone)` = 3 batallas = 11.1 m en el ZiS5). Sin esto el
    // pursuit forzado perdia la curva cerrada en `restan 11 m` y el experimento se quedaba
    // con seis ticks. El ENDPOINT no se saltea: `LlegoLaHoraDelSlam` sigue mandando.
    private bool SalteaZonaManiobra(BZLegView leg)
    {
        if (!leg || !leg.valid) return false;
        if (!leg.isReverse) return false;
        return s_ForzarPursuitRev;
    }

    private void ReportarRuteoReversa(BZVehicleState st, BZLegView leg, BZVehicleManual m, int next)
    {
        if (BZDriveController.s_Quiet) return;   // la suite de self-tests no ensucia el log
        if (!leg || !leg.valid || !leg.isReverse) return;
        if (!m) return;
        float ventana = BZPurePursuit.PisoLookaheadM(m);
        string r = "[BZDrive] RUTEO-REV wp " + leg.nearWp.ToString();
        r = r + " · restan " + leg.remainM.ToString() + " m";
        r = r + " · ventana " + ventana.ToString() + " m";
        r = r + " · kVentana " + m_LogPeor.ToString();
        r = r + " · base " + BZPurePursuit.VentanaFFM(m).ToString() + " m";
        r = r + " · cap " + m_LogCap.ToString();
        r = r + " · kmh " + st.chassisKmh.ToString();
        if (leg.remainM < ventana) r = r + " · GATE-VISIBILIDAD";
        if (m_LogCap > 0.001 && m_LogPeor > m_LogCap) r = r + " · GATE-CAPACIDAD";
        if (s_ForzarPursuitRev) r = r + " · PURSUIT-FORZADO(banco)";
        r = r + " · modo " + m_Mode.ToString() + " -> " + next.ToString();
        Print(r);
    }

    private int StartModeFor(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        if (!leg || !leg.valid) return BZDriveMode.BZDM_CRUISE;
        if (leg.isReverse)
        {
            if (ReversaNecesitaManiobra(st, leg, m, false)) return BZDriveMode.BZDM_MANEUVER;
            return BZDriveMode.BZDM_CRUISE;
        }
        return BZDriveMode.BZDM_CRUISE;
    }

    private int Commit(int mode, BZLegView leg)
    {
        m_Committed = true;
        m_ChainLeg = leg.legIdx;
        return mode;
    }

    private int EnterMode(int mode)
    {
        m_Mode = mode;
        m_DwellTicks = 0;
        return m_Mode;
    }

    private int Stay()
    {
        m_DwellTicks++;
        return m_Mode;
    }

    // Trabado = el cursor NO avanzo sobre la ruta. Vale para CUALQUIER modo salvo HALTED
    // (ahi estar quieto es el trabajo). Es la generalizacion del detector viejo, que solo
    // miraba CRUISE/APPROACH y por eso NO cazo la trabada del run 3, que fue en MANIOBRA.
    private bool Stalled(BZLegView leg)
    {
        if (m_Mode == BZDriveMode.BZDM_HALTED)
        {
            m_StallTicks = 0;
            return false;
        }
        if (m_LastS < 0)
        {
            m_LastS = leg.cursorS;
            m_StallTicks = 0;
            return false;
        }
        if (leg.cursorS - m_LastS >= STALL_MIN_M)
        {
            m_LastS = leg.cursorS;
            m_StallTicks = 0;
            return false;
        }
        m_StallTicks++;
        return m_StallTicks > STALL_TICKS;
    }

    private int SafeStop()
    {
        m_Fallback = BZArbFallback.SAFE_STOP;
        m_Committed = false;
        if (m_Mode != BZDriveMode.BZDM_HALTED) return EnterMode(BZDriveMode.BZDM_HALTED);
        return Stay();
    }
}
