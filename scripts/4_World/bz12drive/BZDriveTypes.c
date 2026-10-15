// ============================================================================
//  BZDriveTypes.c - BZ_AutoDrive 1.2
//
//  Tipos del SEAM (ver SEAM.md). POD planos, INSTANCIAS REUSADAS: el duenio las
//  refilla in-place cada tick -> cero alloc por frame.
//
//  NOTA ENFORCE: los parametros de tipo clase se mutan IN-PLACE (son referencias);
//  no se usa 'out' con clases, solo con primitivos.
// ============================================================================

// Modo del frame. Lo decide BZModeArbiter; viaja en BZDriveOutput.mode para que
// la salida sea SIEMPRE atribuible (SEAM.md, cara 1).
// Miembros prefijados: los nombres cortos (IDLE/CRUISE) colisionan con simbolos del engine.
enum BZDriveMode
{
    BZDM_INVALID,
    BZDM_IDLE,
    BZDM_CRUISE,
    BZDM_APPROACH,
    BZDM_MANEUVER,
    BZDM_ENDPOINT,
    BZDM_HALTED
}

// ⚠⚠ LA TOLERANCIA DE LLEGADA DEL FRAMEWORK. UNA SOLA, Y ACA.
//
// Vivian DOS, cada una privada de su modulo: el slam en 0.10 y la maniobra en 0.12. La
// consecuencia no era estetica -- la MISMA distancia daba veredictos OPUESTOS segun que
// modulo cerro el ultimo leg (0.11 m: INCOMPLETE por el slam, COMPLETE por la maniobra).
// Dos numeros para la misma pregunta se separan solos con el tiempo; uno no puede.
//
// El valor es decision del usuario: el bar ESTRICTO. A sabiendas de que PENDIENTE,
// que cerro a 0.106999 m, queda ROUTE_INCOMPLETE por 7 mm -- y de que ese es el resultado
// correcto. La tolerancia define lo que se considera LLEGAR; no se mueve para aprobar una
// corrida. Lo que se mueve es el vehiculo.
class BZDriveTol
{
    static const float LLEGADA_M = 0.10;

    // ★ ENMIENDA F2. El freno de endpoint de 40 Hz
    // ESTACIONA a ~0.10 m ANTES del punto (su offset de apuntado) y la llegada del
    // forward se mide en hipotenusa 2D: con cualquier lateral residual el arribo real
    // cae en 0.105-0.191 m -- SIEMPRE afuera del bar de 0.10. El sistema se estacionaba
    // afuera de su propio bar y despues daba pasitos sin poder entrar jamas (inching +
    // ROUTE_INCOMPLETE + freeze, los tres sintomas del usuario son este numero).
    // ACEPTA_ENDPOINT_M es la ACEPTACION del endpoint forward (slam): 0.25 cubre los
    // cinco arribos medidos con margen. LLEGADA_M sigue siendo el bar ESTRICTO del usuario
    // para la maniobra/reversa -- que SI entra al bar chico, medido -- y la
    // honestidad del reporte no se toca: el numero real se imprime siempre.
    static const float ACEPTA_ENDPOINT_M = 0.25;
    // RED DE SEGURIDAD del endpoint: llegar con el slam enganchado a mas del bar estricto pero hasta aca es una llegada
    // con aviso (ROUTE_COMPLETE, la distancia real impresa), no una falla que congela el vehiculo con el chofer adentro.
    static const float ACEPTA_ENDPOINT_RED_M = 1.0;
}

// Eventos que el cerebro ENCOLA y el Runner drena (SEAM.md, cara 4). Sin callbacks.
class BZDriveEv
{
    static const int NONE          = 0;
    static const int ARRIVED       = 1;   // parada SEMANTICA: espera Resume()
    static const int ROUTE_COMPLETE = 2;
    static const int STUCK         = 3;   // input incoherente o atascado -> SAFE_STOP
    // La ruta TERMINO pero NO se llego: el ultimo tramo cerro por falta de progreso, no
    // por clavar la pose. Existe porque llamar COMPLETE a un cierre 42 m corto seria
    // MENTIR, y la regla de la casa es gritar en la falla (MODULES.md 8.2).
    static const int ROUTE_INCOMPLETE = 4;
    // TRABADO CONTRA ALGO y la escalera del AR recien abierta. Es un AVISO, no un cierre: la ruta sigue y el
    // consumidor decide (esperar, retroceder, rendirse). El motivo (MotivoBloqueo) dice si hubo CHOQUE reciente. Va al
    // FINAL del enum a pedido de BusDrive. El estado consultable vive en el runner (RunnerBloqueado).
    static const int BLOQUEADO     = 5;
}

// Clase de piso. La resuelve BZWorldProbe; el Manual la usa para CoastDecel/MaxBrakeDecel.
class BZSurf
{
    static const int HARD = 0;   // asfalto/concreto
    static const int SOFT = 1;   // tierra/campo
}

// ---------------------------------------------------------------------------
//  BZMath - helpers numericos. Estaticos, sin estado.
// ---------------------------------------------------------------------------
class BZMath
{
    // Finito: descarta NaN e infinitos. NaN != NaN es la unica prueba portable.
    // Es la base del SAFE_STOP del arbitro (MODULES.md 8.1): input incoherente NO se adivina.
    static bool Finite(float v)
    {
        if (v != v) return false;
        // NaN e infinitos caen a -2147483648 al pasar a entero (medido: la linea de estado del runner con el velocimetro
        // en NaN). Es la prueba que no depende de como el motor compare con NaN (ValidateSpawn tomo 'nan > 0.5' por cierto).
        int iv = (int)v;
        if (iv < -2000000000) return false;
        if (v > 1000000000.0) return false;
        if (v < -1000000000.0) return false;
        return true;
    }

    // un NaN a proposito (la bandera de prueba del velocimetro): la raiz de un negativo; si el motor la acota, 0/0
    static float NaN()
    {
        float n = Math.Sqrt(-1.0);
        if (!Finite(n)) return n;
        float z = 0.0;
        n = z / z;
        return n;
    }

    static bool FiniteVec(vector v)
    {
        if (!Finite(v[0])) return false;
        if (!Finite(v[1])) return false;
        if (!Finite(v[2])) return false;
        return true;
    }

    // Envuelve a (-PI, PI]. Iterativo y acotado: un while sin tope colgaria con un input basura.
    static float WrapPi(float a)
    {
        int guard = 0;
        while (a > Math.PI)
        {
            a = a - Math.PI2;
            guard++;
            if (guard > 32) break;
        }
        while (a < -Math.PI)
        {
            a = a + Math.PI2;
            guard++;
            if (guard > 64) break;
        }
        return a;
    }

    // Rumbo de un delta en el plano XZ. MISMA convencion que el resto del framework:
    // Atan2(x, z), como en BZBusService (FastSteerTick / ComputePurePursuitSteering).
    static float HeadingXZ(float dx, float dz)
    {
        return Math.Atan2(dx, dz);
    }
}

// ---------------------------------------------------------------------------
//  BZVehicleState - LA UNICA ENTRADA DEL CEREBRO (SEAM.md 1.1)
//
//  El Controller nunca ve el Car. Si el cerebro necesita un dato nuevo, SE AGREGA
//  UN CAMPO ACA (con nombre y unidad) y lo llena el Runner. Nunca se abre un atajo.
// ---------------------------------------------------------------------------
class BZVehicleState
{
    vector pos;             // m, mundo
    float  yaw;             // RADIANES, rumbo del vehiculo
    float  kmh;             // km/h, magnitud (velocimetro, sin signo)
    float  chassisKmh;      // km/h, velocidad REAL del chasis (la rueda miente con wheelspin)
    // rumbo del VECTOR VELOCIDAD en radianes (el curso). En curva difiere del yaw por
    // el sideslip; el corrector del FF-primario mide psi contra ESTO (: el
    // beta de Boris tampoco debe entrar al corrector, simetrico al de la referencia).
    // El que llena el estado cae a yaw por debajo de ~7 km/h (curso indefinido lento).
    float  courseRad;
    float  pitch;           // RADIANES, + = trompa arriba
    int    gear;            // marcha actual del engine
    float  rpm;
    int    wheelsOnGround;
    int    wheelCount;      // cuantas tiene: sin esto, 'todas en el piso' no es comprobable
    // (C1): velocidad CON SIGNO sobre el rumbo del CHASIS (+ adelante, - atras). chassisKmh es
    // magnitud y courseRad cae a yaw debajo de 7 km/h: ninguna ve "pide adelante mientras retrocede".
    float  fwdKmh;
    // caja de colision del vehiculo (largo sobre el eje, ancho), para la HUELLA de la re-ubicacion y la
    // referencia al paragolpes del chequeo hacia atras. 0 = sin dato (el servicio la lee una vez por spawn).
    float  lenM;
    float  widthM;
    bool   engineOn;
    // ⚠ CUANTO ESTA DESEMBRAGADO. El header de DayZ lo dice literal:
    //   //! Returns the value of how much the clutch is disengaged.
    //   proto native int GetClutch();
    // Existia desde siempre y nunca lo miramos. Las cinco vias que falsamos contra el bog
    // se disenaron a ciegas del embrague, que es justo lo que distingue el arranque-desde-0
    // (anda: rpm 1021 -> 4075) de la recuperacion bajo mordida (boga: rpm clavado en 1040).
    int    clutch;
    // ULTIMO CONTACTO REPORTADO POR EL MOTOR (lo llena el runner desde CarScript.OnContact). Sirve para
    // que la linea del CHOQUE diga contra QUE pego, en vez de "le pego a algo" y a mirar in-game. clase vacia con
    // haceS valido = contacto sin entidad (terreno/geometria); haceS negativo = nunca hubo contacto.
    string contactoClase;
    float  contactoHaceS;
    float  contactoImp;
    string contactoZona;   // la zona del casco que toco ("" si no hubo contacto)

    void Reset()
    {
        pos = "0 0 0";
        yaw = 0;
        kmh = 0;
        chassisKmh = 0;
        courseRad = 0;
        pitch = 0;
        gear = 0;
        rpm = 0;
        wheelsOnGround = 0;
        wheelCount = 0;
        fwdKmh = 0;
        lenM = 0;
        widthM = 0;
        engineOn = false;
        contactoClase = "";
        contactoHaceS = -1;
        contactoImp = 0;
        contactoZona = "";
    }

    // Coherencia del input. La usa el arbitro para decidir SAFE_STOP.
    bool IsCoherent()
    {
        return (Incoherencia() == "");
    }

    // que campo no es un numero ("" = todos finitos): para que el SAFE_STOP del arbitro diga por que
    string Incoherencia()
    {
        if (!BZMath.FiniteVec(pos)) return "posicion no numerica";
        if (!BZMath.Finite(yaw)) return "rumbo no numerico";
        if (!BZMath.Finite(kmh)) return "velocimetro no numerico (" + kmh.ToString() + ")";
        if (!BZMath.Finite(chassisKmh)) return "velocidad del cuerpo no numerica";
        if (!BZMath.Finite(pitch)) return "inclinacion no numerica";
        return "";
    }
}

// ---------------------------------------------------------------------------
//  BZDriveOutput - LA SALIDA. Campos NOMBRADOS a proposito (SEAM.md 2.1):
//  el bug de orden de argumentos del slam (volante aplicado como freno, 2.5m
//  perdidos) no se puede escribir con esta forma.
// ---------------------------------------------------------------------------
class BZDriveOutput
{
    float steering;   // -1..1 NORMALIZADO. NO grados, NO radianes.
    float throttle;   // 0..1
    float brake;      // 0..1
    int   gear;       // marcha DESEADA
    float handbrake;  // 0..1 FIEL: solo el grabado. Ningun modulo lo escribe (SEAM.md 6).
    int   mode;       // BZDriveMode que produjo este comando

    void Reset()
    {
        steering = 0;
        throttle = 0;
        brake = 0;
        gear = 0;
        handbrake = 0;
        mode = BZDriveMode.BZDM_INVALID;
    }

    // Detencion total. Es la salida del fallback SAFE_STOP y del modo HALTED.
    void SetHalt()
    {
        steering = 0;
        throttle = 0;
        brake = 1.0;
        mode = BZDriveMode.BZDM_HALTED;
    }

    void Clamp()
    {
        steering = Math.Clamp(steering, -1.0, 1.0);
        throttle = Math.Clamp(throttle, 0.0, 1.0);
        brake    = Math.Clamp(brake, 0.0, 1.0);
        handbrake = Math.Clamp(handbrake, 0.0, 1.0);
    }
}

// ---------------------------------------------------------------------------
//  BZDriveGoal - plan de nivel TRAYECTORIA. Lo produce PurePursuit, lo traduce
//  DerivationEngine. Los modulos POSE (MC/Slam) NO lo usan (SEAM.md 4).
// ---------------------------------------------------------------------------
class BZDriveGoal
{
    float speedMs;    // m/s objetivo
    float decelMs2;   // m/s^2 de frenado pedido (>= 0)
    float curvature;  // 1/m, signada. + = DERECHA (ver convencion arriba de BZPathSample)
    // ⚠ La parte de `curvature` que es FEEDFORWARD (la curvatura del CAMINO). El resto es
    // correccion, y las dos se convierten a angulo distinto: solo el feedforward lleva la
    // compensacion de subviraje. Ver BZVehicleManual.SteerCommandSplit.
    //
    // Viaja en el goal porque el volante lo escriben DOS caminos --el Plan de 2 Hz via la
    // derivacion, y el refresco de 40 Hz-- y la ley tiene que ser la misma en los dos. Es
    // exactamente el error que ya cometimos con el cap: cambiar un escritor y
    // que el otro lo pise medio segundo despues.
    float curvatureFF;
    int   dir;        // +1 adelante / -1 reversa
    bool  halt;

    void Reset()
    {
        speedMs = 0;
        curvatureFF = 0;
        decelMs2 = 0;
        curvature = 0;
        dir = 1;
        halt = false;
    }
}

// ---------------------------------------------------------------------------
//  BZPathSample - un punto de la traza.
//
//  CONVENCION DE SIGNO (verificada contra ComputePurePursuitSteering de v1.1, que anda
//  in-game): el rumbo es Atan2(x, z), o sea CRECE EN SENTIDO HORARIO (0 = +Z, +90 = +X).
//  Por lo tanto curvatura POSITIVA = giro a la DERECHA. No es una eleccion: es lo que se
//  deduce de la convencion de rumbo, y esta escrito porque un signo mal documentado es
//  como nacio el bug de unidades RAD/DEG del 2026-07-21.
//
//  headingRad (TANGENTE de la traza) y recHeadingRad (rumbo GRABADO por el humano)
//  son cosas DISTINTAS: en reversa difieren ~180 grados. Los modulos POSE steerean
//  contra el GRABADO. Confundirlos es un volantazo de media vuelta.
// ---------------------------------------------------------------------------
class BZPathSample
{
    int    wp;
    vector pos;
    float  headingRad;      // tangente de la traza
    float  recHeadingRad;   // rumbo grabado. -999 = no disponible.
    float  curvature;       // 1/m, signada. + = DERECHA.
    float  recSpeedKmh;     // velocidad GRABADA (recordedSpeed, la preservada)
    // ⚠ PEDAL GRABADO. -1 = la ruta no lo trae (hasInputData=0), que es el caso de TODAS las
    // rutas convertidas hasta hoy. Quien lo consuma tiene que tratar el -1 como "no hay dato"
    // y comportarse como siempre: es la compuerta de compatibilidad, y ya venia prevista en
    // el formato (col 12). Esta pieza SOLO lo transporta -- no lo usa nadie todavia.
    float  recThrottle;
    float  handbrake;
    bool   isReverse;
    bool   legBreak;
    bool   isStop;
    bool   tight;          // tramo de precision DECLARADO por el autor (no derivable de la geometria)

    void Reset()
    {
        wp = -1;
        pos = "0 0 0";
        headingRad = 0;
        recHeadingRad = -999.0;
        curvature = 0;
        recSpeedKmh = 0;
        recThrottle = -1;
        handbrake = 0;
        isReverse = false;
        legBreak = false;
        isStop = false;
        tight = false;
    }
}

// ----------------------------------------------------------------------------
//  BZHistRuedas - las ruedas en el suelo de los ultimos cuadros, para que la linea de un choque diga si el vehiculo
//  venia apoyado o en el aire (un salto que aterriza). Solo instrumento: no decide nada. La usan el auto y la moto.
// ----------------------------------------------------------------------------
class BZHistRuedas {
    static const int N = 48;   // 1,2 s a 40 Hz: sobra para la ventana
    private ref array<float> m_T;
    private ref array<int> m_R;
    private int m_Idx;
    private int m_Cant;
    private float m_Reloj;

    void BZHistRuedas() {
        m_T = new array<float>();
        m_R = new array<int>();
        for (int i = 0; i < N; i++) {
            m_T.Insert(0);
            m_R.Insert(0);
        }
    }

    void Anotar(int ruedas, float dt) {
        if (dt > 0) m_Reloj = m_Reloj + dt;
        m_T.Set(m_Idx, m_Reloj);
        m_R.Set(m_Idx, ruedas);
        m_Idx = m_Idx + 1;
        if (m_Idx >= N) m_Idx = 0;
        if (m_Cant < N) m_Cant = m_Cant + 1;
    }

    // los cuadros ANTERIORES al ultimo anotado, dentro de ventanaS, del mas viejo al mas nuevo:
    // "12 cuadros · min 0/4 · 4x5 0x6 2x1" (ruedas x cuadros seguidos)
    string Resumen(float ventanaS, int total) {
        if (m_Cant < 2) return "sin cuadros previos";
        int ult = m_Idx - 1;
        if (ult < 0) ult = N - 1;
        float tRef = m_T.Get(ult);
        int k = m_Idx - m_Cant;
        if (k < 0) k = k + N;
        int n = 0;
        int minR = 99;
        int runV = -1;
        int runN = 0;
        string rle = "";
        for (int j = 0; j < m_Cant - 1; j++) {
            float tj = m_T.Get(k);
            int rj = m_R.Get(k);
            k = k + 1;
            if (k >= N) k = 0;
            if (tRef - tj > ventanaS) continue;
            n = n + 1;
            if (rj < minR) minR = rj;
            if (rj == runV) {
                runN = runN + 1;
                continue;
            }
            if (runN > 0) rle = rle + " " + runV.ToString() + "x" + runN.ToString();
            runV = rj;
            runN = 1;
        }
        if (runN > 0) rle = rle + " " + runV.ToString() + "x" + runN.ToString();
        if (n == 0) return "sin cuadros previos en " + ventanaS.ToString() + " s";
        return n.ToString() + " cuadros · min " + minR.ToString() + "/" + total.ToString() + " ·" + rle;
    }
}
