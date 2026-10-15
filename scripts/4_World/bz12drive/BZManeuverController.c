// ============================================================================
//  BZManeuverController.c - BZ_AutoDrive 1.2 -- modulo POSE
//
//  Baja velocidad: cusps, reversa, clavado de pose. La LOGICA de v1.1 era buena
//  (clava los cusps a centimetros); lo que fallaba era el ENGANCHE. En 1.2 el
//  enganche lo decide el BZModeArbiter y esto solo maneja.
//
//  MODULO POSE (SEAM.md 4): BZDriveOutput DIRECTO, exento de la DerivationEngine.
//  A ~0-2 km/h y cerca del tope de direccion el modelo bicicleta es ill-posed y lo
//  que manda es el ERROR DE POSE. Pero NO exento del Manual: creep, mordida de
//  embrague, piso de throttle y ganancia de volante salen de ahi (SEAM.md 4.1).
//
//  ★ ACA MURIO m_ManeuverCreepHold. En v1.1, MC escribia ese member y el leg-machine
//  lo leia: coupling cruzado invisible, y cuando el read falto en el port la falla fue
//  SILENCIOSA (MC "enganchaba" pero el resultado era como v1.0). En 1.2 no existe:
//  MC simplemente NO REPORTA IsLegDone() hasta que clavo. El "no avances todavia" es
//  un retorno de funcion. O el Controller lo llama, o Boris no avanza -- y eso se ve.
//
//  ESTADO PROPIO: fase + ticks de rampa + hecho. Nadie mas los lee.
// ============================================================================

class BZMcPhase
{
    static const int LAUNCH   = 0;   // parado y lejos: romper la inercia
    static const int HILLSTART = 1;  // parado en subida: esperar la mordida del embrague
    static const int TRACK    = 2;   // rodando hacia la pose
    static const int CREEP    = 3;   // ultimo tramo: reptar
    static const int HOLD     = 4;   // clavado

    static string Name(int p)
    {
        if (p == LAUNCH)    return "LAUNCH";
        if (p == HILLSTART) return "HILLSTART";
        if (p == TRACK)     return "TRACK";
        if (p == CREEP)     return "CREEP";
        return "HOLD";
    }
}

class BZManeuverController
{
    private const float STOPPED_FRAC = 0.5;
    // Distancia a la pose por debajo de la cual se pasa a reptar. En WHEELBASES.
    private const float CREEP_ZONE_WB = 1.5;
    // Pendiente (rad) desde la que se considera subida real (~1.7 grados).
    private const float HILL_PITCH_RAD = 0.03;
    // Cuanto puede aportar el cross-track sobre el volante, como fraccion del tope.
    // Rampa de despegue: sube de a poco en vez de flooring. El floor de golpe en el
    // embrague FRIO del respawn producia wheelspin y re-ciclo (medido en v10MC).
    private const float LAUNCH_RAMP_STEP = 0.10;
    // Tope de la rampa. Subir gradual (0.10/tick a 2 Hz = 0.5/s) es justamente lo que
    // evita el wheelspin del embrague frio que se midio en v10MC: el problema era
    // flooreiar de golpe, no llegar alto.
    private const float LAUNCH_MAX = 1.0;
    // Cuanto tiene que subir el rpm para contar como respuesta, y cuantos ticks sordos
    // seguidos hacen falta antes de congelar. Dos ticks a 2 Hz = 1 segundo de gas sin
    // respuesta: suficiente para no confundirlo con ruido de lectura.
    // Tope de ticks sosteniendo el freno en el arranque en subida. Segunda salida, por si
    // el gas necesario tampoco se puede calcular: rodar un poco hacia atras es MUCHO mejor
    // que quedarse clavado (MODULES.md 8.2).
    // Horizonte con el que TRACK cierra el error de velocidad. No es una ganancia
    // ajustable: dice "cerra el error en ~1 s", y lo que sale de ahi es ACELERACION,
    // que el modelo del Manual convierte en gas.
    // Longitud de correccion de pose: minimo en wheelbases, maximo en metros.
    private const float POSE_L_WB  = 2.0;
    private const float POSE_L_MAX = 15.0;
    private const float TRACK_TAU = 1.0;
    private const float TRACK_TAU_BRAKE = 0.25;
    private const float PRECISION_ZONE_MULT = 2.0;
    // A fondo, en subida y sin avanzar durante esto = trampa de par declarada.
    private const int   HILL_MAX_HOLD_TICKS = 12;
    // SALIDA POR FALTA DE PROGRESO (auditoria post-run-10). Ver la regla en MODULES.md 8.2.
    // La ventana DEBE ser mas larga que la rampa de despegue (~7 ticks hasta el tope): si
    // no, cerraria el leg mientras todavia esta juntando fuerza para arrancar.
    private const int   NO_PROGRESS_TICKS = 20;
    private const float PROGRESS_EPS_M = 0.02;
    // NOTA: el limite de volante a baja velocidad NO vive aca. Es una regla FISICA que
    // aplica a los tres modulos que emiten volante (MC, pursuit y slam), asi que esta UNA
    // SOLA VEZ en BZDriveController.Plan(). Tenerla por-modulo era duplicar un hecho
    // fisico en tres lugares que despues se desincronizan.

    private int   m_Phase;
    private int   m_LaunchTicks;
    private bool  m_LegDone;
    private float m_MinDist;
    private int   m_NoProgressTicks;
    private bool  m_Progressed;
    private bool  m_GaveUp;
    // Cuanto faltaba cuando cerro. Ver la nota del mismo campo en BZEndpointSlam.
    private float m_GaveUpSignedM;
    private ref BZPathSample m_S;

    void BZManeuverController()
    {
        m_S = new BZPathSample();
        Reset();
    }

    void Reset()
    {
        m_Phase = BZMcPhase.LAUNCH;
        m_LaunchTicks = 0;
        m_LegDone = false;
        m_MinDist = 1000000.0;
        m_NoProgressTicks = 0;
        m_Progressed = false;
        m_GaveUp = false;
        m_GaveUpSignedM = 0;
    }

    int  GetPhase()   { return m_Phase; }
    // Lo consulta el CONTROLLER para avanzar de leg. Ningun otro modulo lo lee.
    bool IsLegDone()  { return m_LegDone; }
    // Como cerro: CLAVANDO la pose, o RINDIENDOSE por falta de progreso. El que decide
    // que hacer con eso es el Controller -- pero tiene que poder distinguirlo, porque
    // rendirse no es llegar.
    bool GaveUp()     { return m_GaveUp; }
    float GaveUpSignedM() { return m_GaveUpSignedM; }
    // Solo para los tests: cuanto lleva escalada la rampa de despegue.
    int  LaunchTicks() { return m_LaunchTicks; }

    void Actuate(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, BZDriveOutput o)
    {
        o.Reset();
        o.mode = BZDriveMode.BZDM_MANEUVER;
        if (!leg || !m || !leg.valid)
        {
            o.SetHalt();
            return;
        }
        o.gear = 2;
        if (leg.isReverse) o.gear = 0;

        float signed = leg.signedToEndM;
        // (B1, el cusp): LEJOS DEL FINAL `signed` NO SIGNIFICA NADA. Es la leccion de -- una
        // medida LOCAL (proyeccion en linea recta sobre la tangente de la pose final) usada de forma GLOBAL reaparece
        // con cada geometria nueva. Closed() ya no le cree fuera de CreepZone; pero LAUNCH ("signed <= 0 -> ya
        // pasaste: freno"), la eleccion de fase (CREEP si signed < CreepZone) y TargetMs (sqrt(2 a d) con d = signed)
        // SI le creian. MEDIDO en los logs a 40 Hz: 44 entradas a un leg de avance con la proyeccion NEGATIVA
        // (Pavlovo wp 115 x37: -34 m con 4.737 m de arco por delante; Cazador wp 126: -36 m con 5.907 m) quedaron
        // 6,7-8,4 s PARADAS en MANIOBRA con gas 0, hasta que el arbitro salio por MANEUVER_MAX_DWELL; las 219 con
        // proyeccion positiva, 2,1 s de mediana. Y la carrera escondida: el give-up por no-progreso (20 ticks desde
        // el cambio de leg) habria CERRADO el leg de 4,7 km sin manejarlo si el dwell del arbitro (que venia
        // contando desde antes del cusp) hubiera vencido 3 s mas tarde. Regla: fuera de CreepZone el remanente es el
        // ARCO (siempre positivo, baja con el cursor); adentro, la proyeccion firmada, que es donde significa algo.
        if (leg.remainM > CreepZone(m)) signed = leg.remainM;
        UpdateProgress(st, m, signed);
        UpdatePhase(st, leg, m, signed);
        o.steering = PoseSteer(st, leg, m);
        Longitudinal(st, leg, m, surfKind, signed, o);
        o.Clamp();
    }

    // A FONDO, EN SUBIDA Y SIN AVANZAR. Las tres cosas juntas, y solo juntas: a fondo en
    // llano es un arranque normal, y en subida sin gas es otra cosa. Cuando se cumplen las
    // tres, mas gas no existe -- el pedal ya esta abajo -- y el problema es que el rpm cayo
    // debajo del pico de torque. Que se declare cambia el diagnostico de "va lento" a
    // "este vehiculo no corona esta cuesta desde aca".
    // ⚠ EL HUECO DE PAR SE FUE AL CONTROLLER, a proposito. Vivia aca y por eso solo se
    // evaluaba en modo MANIOBRA -- pero la cuesta se maneja en CRUISE, asi que no corria
    // nunca. "A fondo, con las ruedas en el piso, en subida y sin avanzar" es una condicion
    // del VEHICULO, no de la pose: va con las otras reglas transversales.
    // ======================= fases =======================

    // ⚠ EL SIGNO DE signedToEndM ES EL DATO, NO EL RUIDO.
    //
    // signed > 0  = la pose esta ADELANTE (falta llegar)
    // signed <= 0 = YA PASASTE la pose
    //
    // BUG DEL RUN 4 (Sedan, cusp wp 153): esta funcion usaba dist = |signed| y tiraba el
    // signo. Con signed = -0.6 (ya pasado) y el vehiculo detenido, |signed| = 0.6 > la
    // tolerancia -> "estoy quieto y no llegue" -> fase LAUNCH -> throttle HACIA ADELANTE.
    // Cada tick se alejaba mas: signed -0.6 -> -3.1 -> -6.8 -> -10.3 -> -13.1. Manejando
    // en direccion CONTRARIA al objetivo, para siempre, hasta que lo corto el stall.
    //
    // La regla correcta ya existia en el EndpointSlam ("paso de largo -> CLAVA, no vuelve
    // a buscarlo"); MC no la tenia. Adelante nunca arregla un sobrepaso: o cerras la pose,
    // o el que corrige es el leg SIGUIENTE (que en un cusp es justamente la reversa).
    private void UpdatePhase(BZVehicleState st, BZLegView leg, BZVehicleManual m, float signed)
    {
        if (Closed(st, m, signed, leg.remainM))
        {
            m_Phase = BZMcPhase.HOLD;
            m_LegDone = true;
            if (signed > BZDriveTol.LLEGADA_M)
            {
                m_GaveUp = true;                       // cerro sin llegar a la pose
                m_GaveUpSignedM = signed;
            }
            return;
        }
        if (m_Phase == BZMcPhase.HOLD) return;
        float stoppedKmh = StoppedKmh(m);
        if (st.chassisKmh < stoppedKmh)
        {
            // Detenido y con la pose TODAVIA ADELANTE: romper la inercia.
            if (st.pitch > HILL_PITCH_RAD) m_Phase = BZMcPhase.HILLSTART;
            else                           m_Phase = BZMcPhase.LAUNCH;
            return;
        }
        // ⚠ Antes esto reseteaba la rampa apenas la velocidad pasaba el umbral. En la
        // cuesta el vehiculo pegaba TIRONES (0.002 <-> 4.76 km/h) y cada tiron reiniciaba
        // el despegue: el gas nunca llegaba al tope, la salida por rampa-agotada nunca
        // disparaba, y quedaba oscilando. La rampa se reinicia por PROGRESO REAL sobre la
        // ruta, no por un pico de velocimetro -- misma leccion que el detector de stall.
        if (m_Progressed) m_LaunchTicks = 0;
        if (signed < CreepZone(m)) m_Phase = BZMcPhase.CREEP;
        else                       m_Phase = BZMcPhase.TRACK;
    }

    // Cerrado = detenido de verdad Y en la pose o PASADO. El <= cubre los dos casos con
    // una sola comparacion, porque el signo ya distingue de que lado estas.
    //
    // SEGUNDA SALIDA (auditoria post-run-10): detenido, CORTO de la pose y sin acercarse.
    // Sin esto la condicion puede volverse INALCANZABLE -- un vehiculo que no logra los
    // ultimos centimetros (sin traccion, en pendiente) nunca cierra el leg y la ruta se
    // clava. Es la misma regla que el slam ya tenia (MODULES.md 8.2): la tolerancia define
    // lo excelente, no lo unico aceptable.
    //
    // ⚠⚠⚠ SEGUNDA VEZ QUE ESTA RAMA CIERRA UN LEG SIN MANEJARLO.
    // `signed` es `signedToEndM`: una PROYECCION EN LINEA RECTA sobre la tangente de la pose
    // final. Mide "llegue / me pase" SOLO CERCA de la pose. Lejos de ella no significa nada,
    // y esta rama le creia igual.
    //   1ra vez (PENDIENTE): la tangente de Head(hi) cruzaba al leg SIGUIENTE en un cusp ->
    //   proyeccion negativa desde el primer tick. Se arreglo la TANGENTE (ver SignedToEnd).
    // 2da vez (SUZUKI, ): leg de avance de 189 m que SALE Y VUELVE. La tangente
    //   interna es correcta, pero como el leg da la vuelta la pose final queda 46 m DETRAS del
    //   arranque sobre su propia tangente -> signed = -46 con el vehiculo quieto -> Closed() en
    //   el PRIMER tick -> OnLegDone con 189 m de arco por delante -> el leg 3 arranca a 138 m
    //   de su traza y la ruta termina ROUTE_INCOMPLETE.
    // Arreglar la tangente fue tratar un CASO de la causa. La causa es que una medida LOCAL se
    // usaba de forma GLOBAL: por eso reaparecio con otra geometria.
    //
    // La guarda que faltaba: `signed` solo puede cerrar el leg si el ARCO que queda es local.
    // Medido en TODAS las corridas con rem/remFr: 156 cierres de leg desde maniobra ->
    //   155 legitimos con remainM <= 0.47 m (mediana 0.000)
    //     1 falso    con remainM  = 189.03 m   (el de la SUZUKI)
    // CreepZone (1.5 x batalla) ya es, en este mismo modulo, la zona donde se arrastra A LA
    // POSE: >= 2.1 m aun en la moto, 4.5x el peor cierre legitimo. No es un umbral nuevo.
    //
    // ✖ La rama de SIN-AVANCE no se toca: es la salida que evita que la condicion se vuelva
    // inalcanzable, y restringirla abriria un deadlock. Si el cursor quedara atrasado respecto
    // del casco, el peor caso de esta guarda es esperar esos 3 s, no clavarse.
    //
    // (Y POR QUE ESTO MUERDE SOLO EN REVERSA->AVANCE: FromManeuver saca a CRUISE al entrar a un
    // leg de REVERSA, pero en un leg de AVANCE sostiene MANIOBRA mientras `alongM` este dentro
    // de la zona de entrada o el vehiculo no ruede -- "se sale COMPLETANDO". Esa herencia es
    // de diseno; lo que estaba mal era que este Closed() creyera un `signed` a 189 m.)
    private bool Closed(BZVehicleState st, BZVehicleManual m, float signed, float remainM)
    {
        if (st.chassisKmh >= StoppedKmh(m)) return false;
        if (signed <= BZDriveTol.LLEGADA_M && remainM <= CreepZone(m)) return true;
        return m_NoProgressTicks > NO_PROGRESS_TICKS;
    }

    // Lleva la cuenta de si el vehiculo se esta ACERCANDO a la pose.
    private void UpdateProgress(BZVehicleState st, BZVehicleManual m, float signed)
    {
        float dist = signed;
        if (dist < 0) dist = -dist;
        m_Progressed = false;
        if (dist < m_MinDist - PROGRESS_EPS_M)
        {
            m_MinDist = dist;
            m_NoProgressTicks = 0;
            m_Progressed = true;
            return;
        }
        if (dist < m_MinDist) m_MinDist = dist;
        // ⚠ MOVERSE NO ES AVANZAR. Antes un solo tick por encima del umbral de velocidad
        // borraba la cuenta entera, asi que un vehiculo que TIRONEA sin avanzar (medido:
        // 0.002 <-> 4.76 km/h en la cuesta) nunca llegaba a rendirse: otra condicion
        // vuelta INALCANZABLE por la dinamica misma (MODULES.md 8.2). Ahora DRENA: la
        // marcha sostenida la baja de a poco, un tiron aislado no la resetea.
        if (st.chassisKmh < StoppedKmh(m)) m_NoProgressTicks++;
        else if (m_NoProgressTicks > 0)    m_NoProgressTicks--;
    }

    float GetMinDistM() { return m_MinDist; }

    private float StoppedKmh(BZVehicleManual m)
    {
        float k = m.creepKmh * STOPPED_FRAC;
        if (k < 0.3) k = 0.3;
        return k;
    }

    // Donde la precision manda sobre el impulso: la misma zona de arrastre, por un factor.
    // Afuera de aca el modulo todavia esta VIAJANDO, y perder velocidad es mas caro que
    // llegar unos centimetros largo -- porque la velocidad perdida abajo del bog no vuelve.
    private float ZonaDePrecision(BZVehicleManual m)
    {
        return CreepZone(m) * PRECISION_ZONE_MULT;
    }

    // la lee tambien el controlador (CintaClavoElCusp): una sola definicion de "cerca del final del leg".
    float CreepZone(BZVehicleManual m)
    {
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        return CREEP_ZONE_WB * wb;
    }

    // ======================= direccion (POSE) =======================

    // Igual el rumbo GRABADO (no la tangente: en reversa difieren ~180 grados) y suma
    // una correccion lateral. El signo sale de la CONVENCION, no de un config tuneado:
    // rumbo = Atan2(x,z) crece horario -> hErr>0 = deseado a la DERECHA -> adelante se
    // corrige con volante positivo; en REVERSA el mismo volante rota al reves -> invierte.
    // ★★ EL LAZO DE POSE PIDE UNA CURVATURA, NO MULTIPLICA POR UNA GANANCIA.
    //
    // Esto era:
    //     s = WrapPi(rec - yaw) * steerKPose  +  crossTrackM * (steerKPose / (2*wb))
    //
    // o sea, DOS terminos escalados por 1/steerMax. Dimensionalmente pasaba -- error de
    // rumbo y angulo de rueda son los dos radianes -- pero fisicamente no: el angulo que
    // corrige un error de rumbo depende de la velocidad y de la DISTANCIA que queda. Con
    // steerMax en 75 grados (que era un artefacto de una sola muestra) el lazo quedaba
    // suave y "andaba"; al corregir steerMax a los 30.5 reales quedo 2.5x mas agresivo y
    // Boris empezo a weavear 1.87 m. La ganancia era steerMax haciendo de perilla.
    //
    // Ahora se arma una CURVATURA deseada, que es lo que el vehiculo puede ejecutar, y se
    // la convierte con la MISMA cuenta que usa el lado trayectoria
    // (BZVehicleManual.SteerCommandForCurvature). Dos terminos, los dos con significado:
    //
    //     kappa = hErr / L        <- girar hErr a lo largo de L metros
    //           + 2*e / L^2       <- y cerrar un desvio lateral e en esa misma L
    //
    // L es una LONGITUD DE CORRECCION en metros -- no un gain: "en cuantos metros quiero
    // estar alineado". Sale de la distancia que falta, acotada: lejos corrige suave (que
    // es lo que faltaba), cerca corrige firme, y nunca pide un radio mas chico del que el
    // vehiculo puede dar.
    private float PoseSteer(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        float L = CorrectionLengthM(leg, m);
        if (L < 0.01) return 0;
        float rec = LookaheadRecordedHeading(leg, m);
        float hErr = 0;
        if (rec > -900.0) hErr = BZMath.WrapPi(rec - st.yaw);
        float kappa = hErr / L + (2.0 * leg.crossTrackM) / (L * L);
        float s = m.SteerCommandForCurvature(kappa, st.chassisKmh);
        if (leg.isReverse) s = -s;
        return Math.Clamp(s, -1.0, 1.0);
    }

    // ⚠ RESTAURADO. Se lo llevo puesto la misma edicion por rebanada que reemplazo
    // PoseSteer y CrossTrackCorr: el corte abarcaba mas metodos de los que yo creia. Es la
    // SEGUNDA vez que una edicion por indices borra un metodo en silencio (la primera fue
    // BZEnvelopeStore.RatioForGear). El sintoma es el mismo las dos veces: compila el PBO,
    // enforce_types da limpio, y el compilador in-game tira "Undefined function".
    //
    // Lee el rumbo GRABADO un wheelbase adelante sobre el arco, no en el punto actual:
    // apuntar al rumbo de donde estas es perseguir el pasado.
    private float LookaheadRecordedHeading(BZLegView leg, BZVehicleManual m)
    {
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        if (!leg.SampleAt(wb, m_S)) return -999.0;
        return m_S.recHeadingRad;
    }

    // En cuantos metros se quiere anular el error. Es la distancia que falta, con dos
    // topes fisicos: no menos de POSE_L_WB wheelbases (abajo de eso se pide un radio que
    // el vehiculo no puede dar) y no mas de POSE_L_MAX (arriba de eso no corrige nada).
    private float CorrectionLengthM(BZLegView leg, BZVehicleManual m)
    {
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        float d = leg.signedToEndM;
        if (d < 0) d = -d;
        float lo = POSE_L_WB * wb;
        if (d < lo) d = lo;
        // ⚠⚠ UN TOPE ABSOLUTO NO PUEDE ANULAR UN PISO RELATIVO.
        // El piso (POSE_L_WB * wb) escala con el vehiculo; el tope no. Cuando
        // POSE_L_WB * wb supera el tope, el tope GANA y el piso queda anulado -- y ese
        // piso existe porque abajo de el se pide un radio que el vehiculo no puede dar.
        // Medido: el ExpansionBus (batalla 5.35) deberia usar 10.70 y usaba 8.0.
        // La condicion muerde con batalla > POSE_L_MAX/POSE_L_WB.
        if (d > POSE_L_MAX && POSE_L_MAX > lo) d = POSE_L_MAX;
        return d;
    }

    // ======================= longitudinal =======================

    private void Longitudinal(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, float signed, BZDriveOutput o)
    {
        if (m_Phase == BZMcPhase.HOLD)
        {
            o.throttle = 0;
            o.brake = 1.0;
            return;
        }
        if (m_Phase == BZMcPhase.HILLSTART)
        {
            HillStart(st, m, o);
            return;
        }
        if (m_Phase == BZMcPhase.LAUNCH)
        {
            // Cinturon y tiradores: aunque la fase no deberia entrar aca con la pose ya
            // pasada, empujar hacia adelante en ese caso es alejarse del objetivo.
            if (signed <= 0)
            {
                o.throttle = 0;
                o.brake = 1.0;
                return;
            }
            Launch(m, o);
            return;
        }
        Track(st, leg, m, surfKind, signed, o);
    }

    // DESPEGUE: la rampa ARRANCA EN EL PISO Y CRECE hasta que el chasis se mueve.
    //
    // ⚠ BUG DEL RUN 5 (Sedan, arranque de la reversa): esto hacia
    //       thr = 0.30 + ticks*0.10;  if (thr > floor) thr = floor;
    //   o sea usaba launchThrottleFloor como TECHO cuando su nombre (y su derivacion)
    //   dicen PISO. La rampa quedaba recortada a 0.131 desde el tick 0 -> throttle
    //   CONSTANTE y debil -> la reversa nunca rompia la inercia estatica y Boris se
    //   quedaba a 0.005 km/h hasta que lo corto el stall.
    //
    // El piso es el punto de PARTIDA (la fuerza que el modelo dice que hace falta para
    // mover la masa); la inercia estatica y la friccion piden mas, y cuanto mas no lo
    // sabe nadie de antemano -- por eso se SUBE hasta que arranca. La rampa se corta
    // sola: UpdatePhase resetea m_LaunchTicks apenas el chasis se mueve.
    private void Launch(BZVehicleManual m, BZDriveOutput o)
    {
        float thr = m.launchThrottleFloor + m_LaunchTicks * LAUNCH_RAMP_STEP;
        m_LaunchTicks++;
        o.throttle = thr;
        o.brake = 0;
    }

    // ARRANQUE EN SUBIDA: con el auto frenado se suben vueltas hasta la MORDIDA del
    // embrague (clutchBiteRpm) y ahi se suelta el freno. Si soltas ANTES, el auto rueda
    // para atras; si soltas DESPUES, quedas frenado subiendo vueltas (cala o wheelspin).
    //
    // ⚠ BUG DEL RUN 6 (leg 2, wp 230): esto aplicaba launchThrottleFloor CONSTANTE.
    // Con 0.131 el motor llegaba a 1015 rpm y la mordida estaba en ~1040 -> el freno no
    // se soltaba NUNCA -> deadlock. Es EXACTAMENTE la misma clase que el bug del launch
    // del run 5: un throttle que no rampea no puede alcanzar un umbral.
    //
    // Fix: la MISMA rampa que el despegue. Y el freno se suelta EN EL CRUCE, evaluado el
    // mismo tick en que se aplica el throttle.
    private void HillStart(BZVehicleState st, BZVehicleManual m, BZDriveOutput o)
    {
        float thr = m.launchThrottleFloor + m_LaunchTicks * LAUNCH_RAMP_STEP;
        m_LaunchTicks++;
        o.throttle = thr;
        o.brake = 0;
        // ⚠ LA COMPUERTA SE AUTO-DERROTABA (run de la tesis, cuesta de 4.4 grados).
        // Antes esto sostenia el freno hasta que rpm >= clutchBiteRpm. Pero con el freno
        // PUESTO el motor esta CARGADO y no puede subir vueltas: la condicion que se
        // espera es imposible JUSTAMENTE POR LA ACCION QUE SE TOMA MIENTRAS SE ESPERA.
        // Medido: thr 0.94 y brk 1 a la vez, rpm sin llegar nunca a la mordida.
        //
        // La compuerta correcta es por THROTTLE, no por rpm: se sostiene hasta tener el
        // gas que ALCANZA PARA LA PENDIENTE, y eso la rampa lo alcanza por construccion.
        // Derivado: para no rodar hacia atras hace falta a >= g*sin(pitch), y el Manual
        // dice cuanta aceleracion da el vehiculo a fondo.
        if (thr >= HoldThrottle(st, m)) return;
        if (m_LaunchTicks > HILL_MAX_HOLD_TICKS) return;   // 2da salida: nunca deadlock
        o.brake = 1.0;
    }

    // Gas necesario para SOSTENER la pendiente. Si no hay dato de aceleracion maxima
    // (gearing sin medir), no se puede calcular: se devuelve un valor bajo para que la
    // rampa lo cruce enseguida. Preferimos rodar un poco hacia atras antes que clavarnos.
    private float HoldThrottle(BZVehicleState st, BZVehicleManual m)
    {
        if (m.maxAccelMs2 < 0.1) return m.launchThrottleFloor;
        float aSlope = 9.81 * Math.Sin(st.pitch);
        if (aSlope <= 0) return m.launchThrottleFloor;
        return Math.Clamp(aSlope / m.maxAccelMs2, 0.1, LAUNCH_MAX);
    }

    // Lazo de velocidad contra el objetivo de POSE: la velocidad que permite frenar
    // hasta el punto (v = sqrt(2 a d)), techada por la grabada y por el creep en la
    // zona final. La decel es la REAL del vehiculo en ESE piso, del Manual.
    // ★ REGRESION DE LA CORRIDA DE LA TESIS (leg 2, la cuesta).
    //
    // Esto era BANG-BANG con una CONSTANTE: por debajo del objetivo pedia
    // launchThrottleFloor (0.145 medido en el Sedan) y listo. En llano alcanza. En la
    // cuesta 0.145 NO SOSTIENE NI EL PESO -- hacen falta ~0.5 solo para no perder
    // velocidad -- asi que el vehiculo BOGABA hasta 0.22 km/h, y ahi el rpm quedaba
    // debajo del pico de torque: trampa de par, de la que ya no se sale con mas gas.
    //
    // Peor: el arbitro sale de MANIOBRA con  alongM >= 11.3 m  Y  chassisKmh >= creep.
    // Un vehiculo bogado no cumple NINGUNA de las dos, asi que el leg entero se quedaba
    // en MC -- otra compuerta suprimida por la accion que se toma mientras se espera
    // (MODULES.md 8.2.1), esta vez a nivel de MODO.
    //
    // La cuenta ya existia y estaba en el lugar equivocado: el modelo inverso por curva
    // de torque vivia adentro del DerivationEngine, que los modulos POSE no pueden usar.
    // Ahora vive en el Manual y lo usan los dos. Aca NO hay ganancia de pendiente ni
    // numero magico: se pide una ACELERACION y se descuenta lo que el vehiculo hace solo.
    private void Track(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, float signed, BZDriveOutput o)
    {
        float v = st.chassisKmh / 3.6;
        float vTgt = TargetMs(st, leg, m, surfKind, signed);
        float err = vTgt - v;
        // ⚠ HORIZONTE ASIMETRICO, y la asimetria es el punto.
        //
        // Con un TAU unico de 1 s este modulo -- que existe para CERRAR POSES CON
        // PRECISION -- perdio justo eso: el cierre paso de 0.64 m a 1.97 m in-game, y el
        // banco offline lo reprodujo (0.398 -> 0.904 m). Dos mecanismos, los dos del lado
        // del freno:
        //   1) BANDA MUERTA. El freno recien entra cuando err/TAU < aFree, y en llano
        //      aFree = -0.147, asi que con TAU=1 hay 0.53 km/h de sobrevelocidad donde NO
        //      SE FRENA NADA. Con TAU=0.25 esa banda se achica cuatro veces.
        //   2) LLEGA TARDE. Un horizonte de 1 s para frenar es larguisimo cuando faltan
        //      30 cm: cuando la demanda crece, ya pasaste.
        //
        // Los dos errores NO cuestan lo mismo, y por eso los horizontes no pueden ser
        // iguales: tardar en acelerar cuesta TIEMPO; tardar en frenar cuesta PRECISION,
        // que es la unica razon por la que existe este modulo. Barrido en el banco:
        // 1.0 s -> 0.904 m · 0.6 -> 0.241 · 0.4 -> 0.117 · 0.25 -> 0.021 · 0.15 -> 0.059.
        // Es un OPTIMO, no una pendiente: por debajo frena de mas y queda CORTO.
        // ★★ ...PERO SOLO CERCA DE LA POSE. Ahi es donde la precision es el punto.
        //
        // El horizonte agresivo se calibro para CERRAR, y se estaba aplicando en todo el
        // leg. Medido en la reversa: a 15 m de la pose, con 1.84 km/h de sobrevelocidad,
        // pedia -2.04 m/s2 -- mataba la velocidad donde la precision todavia no importa, y
        // despues la recuperaba con el horizonte lento (4x) mientras el planeo real
        // (-0.725) seguia tirando para abajo. Resultado: soltaba el gas a 11.84 km/h en
        // wp 169 y no volvia a subir nunca. El leg 1 cerro 11.4-11.8 m en CINCO corridas.
        //
        // Los dos errores siguen sin costar lo mismo, y por eso la asimetria se conserva
        // donde fue medida: adentro de la zona de cierre. Afuera, tardar en acelerar cuesta
        // tiempo y frenar de mas cuesta la corrida entera.
        float tau = TRACK_TAU;
        if (err < 0 && signed < ZonaDePrecision(m)) tau = TRACK_TAU_BRAKE;
        float aWant = err / tau;
        // Lo que el vehiculo hace SOLO (planeo + gravedad). En subida es negativo y hay
        // que compensarlo ADEMAS de acelerar; en bajada es positivo y descuenta gas.
        float aFree = m.FreeAccel(st.chassisKmh, st.pitch, surfKind);
        float aNeed = aWant - aFree;
        if (aNeed > 0)
        {
            o.throttle = m.ThrottleForAccel(aNeed, st.chassisKmh, st.rpm, o.gear);
            o.brake = 0;
            return;
        }
        // Ni soltando todo baja lo suficiente: hay que frenar.
        float aBrakeMax = m.MaxBrakeDecelManiobra(surfKind);   // techado por la frenada medida del cusp
        if (aBrakeMax < 0.5) aBrakeMax = 0.5;
        o.throttle = 0;
        o.brake = Math.Clamp((-aNeed) / aBrakeMax, 0.0, 1.0);
    }

    // ⚠⚠⚠ EL TECHO DE LA APROXIMACION ES EL PERFIL GRABADO A LA MISMA DISTANCIA DEL
    // PUNTO, no el minimo grabado adelante ni el creep sostenido.
    //
    // Lo que habia: (a) MinRecSpeedAhead(CreepZone) -- el MINIMO grabado en los proximos
    // 1.5*wb; cerca del cusp ese minimo ES el valor terminal (2.35), asi que el target
    // caia a ~3 km/h desde 4.5 m antes, donde el humano iba a 8.1; y (b) la fase CREEP
    // planchaba a creep (~4) la zona entera. Medido (Gemera, cusp): 12 s a 3.7-4.1 km/h
    // para los ultimos 3.2 m con gas 0 -- el humano hizo los ultimos 10 m en ~3 s
    // (18.7 -> 13.9 -> 8.1 -> 7.3 -> 2.35). Los pasitos "del slam" eran ESTO: el slam ni
    // actua en cusps. La anticipacion del humano YA VIVE en la toma: perseguir su perfil
    // a la misma distancia ES llegar "de un impulso", que es el bar (mision: desplegar
    // bots sin estacionar de a pasitos).
    //
    // El lead de la muestra es FISICO y se encoge con la velocidad: v*TRACK_TAU_BRAKE
    // (el mismo horizonte con que este modulo cierra errores), techo 1 wb. Un lead fijo
    // de 1 wb clavaba el valor TERMINAL (2.35) durante los ultimos 3 m -- sobre-anticipa
    // donde el humano todavia iba a 7.3 (lo cazo el espejo antes de shippear). El
    // envelope sqrt(2*aMax*d) queda de RED DURA -- manda cerca del punto y garantiza
    // frenabilidad. El creep queda de FALLBACK para tomas sin velocidad grabada.
    //
    // Falsador A: si aparecen sobrepasos/GaveUp en cusps (along negativo en el reporte)
    // -> el perfil llega caliente para el lag de freno de ESE vehiculo: margen sobre
    // aMax en el envelope, NO volver al creep. Falsador B: si la reversa pierde
    // precision de cierre -> gatear el techo-grabado a forward y devolver min-adelante
    // a la reversa.
    private float TargetMs(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, float signed)
    {
        float d = signed - BZDriveTol.LLEGADA_M;
        if (d < 0) d = 0;
        float aMax = m.MaxBrakeDecelManiobra(surfKind);   // techado por la frenada medida del cusp
        if (aMax < 0.5) aMax = 0.5;
        float vStop = Math.Sqrt(2.0 * aMax * d);
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        float lead = (st.chassisKmh / 3.6) * TRACK_TAU_BRAKE;
        if (lead > wb) lead = wb;
        bool recCap = false;
        if (leg.SampleAt(lead, m_S) && m_S.recSpeedKmh > 0.1)
        {
            recCap = true;
            float recMs = m_S.recSpeedKmh / 3.6;
            if (vStop > recMs) vStop = recMs;
        }
        if (!recCap)
        {
            float creepMs = m.creepKmh / 3.6;
            if (m_Phase == BZMcPhase.CREEP && vStop > creepMs) vStop = creepMs;
        }
        return vStop;
    }
}
