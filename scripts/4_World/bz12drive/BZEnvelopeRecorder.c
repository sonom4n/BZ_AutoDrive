// ============================================================================
//  BZEnvelopeRecorder.c - BZ_AutoDrive 1.2
//
//  EL OBSERVADOR. Mira (estado del vehiculo, comando aplicado) por frame y acumula en
//  el BZEnvelopeStore lo que NO se puede derivar del config.
//
//  ⚠ TODA LA DIFICULTAD ESTA EN LAS COMPUERTAS, NO EN LAS FORMULAS.
//  Cada magnitud vale SOLO bajo condiciones especificas; tomada fuera de ellas, la
//  formula devuelve un numero perfectamente calculado y perfectamente falso. Y como
//  lo MEDIDO MANDA sobre el prior, una medicion sucia es PEOR que ninguna: reemplaza
//  un prior honesto por basura con cara de dato.
//
//    r = v/omega        exige RODADURA LIMPIA: sin gas (wheelspin -> r subestimado) y
//                       sin freno (lockup -> r explota).
//    CoastDecel         exige COASTEO PURO: sin gas, sin freno, sin handbrake, ruedas
//                       en el piso y volante casi recto (un volante cruzado agrega
//                       arrastre que NO es planeo).
//    k(v)               exige giro REAL: velocidad util y angulo de rueda por encima
//                       del ruido; abajo de eso el cociente es 0/0.
//    steerMax           es un MAXIMO observado: no necesita compuerta, solo tiempo.
//
//  No conoce el Car: recibe primitivos -> se testea sin juego. Lo alimenta el Runner.
// ============================================================================

class BZEnvelopeRecorder
{
    // --- compuertas de RODADURA LIMPIA (r = v/omega) ---
    private const float R_MAX_THROTTLE = 0.05;
    private const float R_MAX_BRAKE    = 0.02;
    private const float R_MIN_MS       = 3.0;
    private const float R_MIN_OMEGA    = 1.0;

    // --- compuertas de COASTEO PURO ---
    private const float C_MAX_THROTTLE = 0.02;
    private const float C_MAX_BRAKE    = 0.02;
    private const float C_MAX_HB       = 0.02;
    private const float C_MAX_STEER    = 0.15;   // volante cruzado = arrastre, no planeo
    // ⚠⚠⚠ ESTE PISO DEJABA SIN MEDIR EXACTAMENTE EL REGIMEN QUE DECIDE EL ENDPOINT.
    //
    // Era 2.0 m/s = 7.2 km/h, y la grilla de bins es de 5 km/h (BZEnvelopeStore.BINW), asi que
    // el BIN 0 --0 a 5 km/h-- no se llenaba NUNCA. Verificado en el envelope del CivilianSedan
    // tras 22146 muestras: coast 30/40 bins, y el bin 0 ausente en las dos superficies.
    //
    // Consecuencia medida: en el endpoint (0 a 4 km/h) CoastDecel cae al vecino MEDIDO mas
    // cercano, que es el bin 1 (5-10 km/h, media 0.2748 m/s2). O sea que al vehiculo casi
    // detenido se le aplica el planeo de un vehiculo a 8 km/h. Por eso aFree salio PLANO
    // --0.346 a 0.391 en todo el tramo-- mientras el arrastre REAL trepaba de 0.325 a 0.647:
    // el modelo no es que estuviera mal calibrado, es que no tenia dato ahi y usaba el de al lado.
    //
    // El piso baja a "el vehiculo se esta moviendo de verdad". Los outliers ya estan cubiertos
    // por lo que existe: la compuerta de pitch (2.2 grados), el signo de la gravedad, y el
    // rango C_MIN_DECEL/C_MAX_DECEL sobre el valor COMPENSADO. Y exigir que la velocidad
    // POSTERIOR siga arriba del piso excluye por construccion el tick en que el vehiculo se
    // detiene, que es el unico que podria inventar una desaceleracion enorme.
    //
    // El endpoint pasa a medirse SOLO: su propia aproximacion es un planeo a baja velocidad.
    private const float C_MIN_MS       = 0.3;    // 1.08 km/h
    private const float C_MAX_DECEL    = 12.0;   // mas que esto es un choque, no planeo
    private const float C_MIN_DECEL    = 0.02;
    private const float STX_EPS_CMD      = 0.01;   // comando quieto
    private const float STX_EPS_ANG      = 0.005;  // rad: ~0.3 grados, la rueda llego
    private const int   STX_ESTABLE_TICKS = 3;     // dos señales quietas, no un tiempo a ojo
    private const float C_MAX_PITCH    = 0.0383;  // 2.2 grados: asin(sigma_muestra / g)

    // --- compuertas de UNDERSTEER k(v) ---
    private const float K_MIN_MS       = 3.0;
    private const float K_MIN_ANGLE    = 0.05;   // ~3 grados: abajo es ruido
    private const float K_MIN_YAWRATE  = 0.02;
    private const float K_MAX          = 3.0;    // fuera de esto no es un vehiculo terrestre

    // --- generales ---
    // --- compuertas de RATIO TOTAL (rpm del motor contra omega de rueda) ---
    private const float RT_MIN_OMEGA = 2.0;
    private const float RT_MIN_RPM   = 500.0;
    private const float RT_MAX_THR   = 0.5;    // a fondo hay patinada: el cociente miente
    private const float RT_MAX_BRAKE = 0.02;
    private const float RT_MIN_MS    = 4.0;    // 14 km/h: abajo, v/r es ruido y rpm/w explota
    private const int   RT_STABLE    = 20;     // muestras sin cambiar de marcha (~0.5 s)
    private const float RT_MAX_RPM_JUMP = 0.15; // salto de rpm por muestra = cambio de marcha
    // --- compuertas de ACELERACION MAXIMA ---
    private const float A_MIN_THR    = 0.8;    // solo cuenta lo que se logra a fondo
    private const float A_MAX        = 12.0;   // mas que esto es un rebote, no aceleracion
    private const int   A_GEAR       = 2;      // 1ra fisica: donde aMax significa algo
    private const float A_MAX_MS     = 8.0;    // ~29 km/h: arriba de eso manda la caja
    private const float A_MAX_PITCH  = 0.35;   // 20 grados: mas que eso no es una cuesta
    private const float A_MAX_PITCH_RATE = 0.5; // rad/s: mas rapido que esto es un pozo
    private const float A_MIN_MS     = 1.0;    // rodando: parado es romper inercia, otro fenomeno
    private const float A_MIN_ACCEL  = 0.3;    // debajo de esto el auto NO esta acelerando
    // --- compuertas de CREEP (equilibrio de ralenti) ---
    private const float CR_MIN_MS    = 0.15;
    private const float CR_MAX_MS    = 5.0;
    private const float CR_MAX_ACCEL = 0.05;   // ~0: creep es EQUILIBRIO, no un paso al pasar
    // Con la correccion por gravedad la pendiente deja de sesgar; lo que queda es que
    // en mucha cuesta el cruce es rapido y hay pocas muestras. 0.06 rad = 3.4 grados.
    private const float CR_MAX_PITCH = 0.06;
    // EL CREEP ES UN REGIMEN, NO UN INSTANTE. Medido en la toma de la linea (ChernoBus): 532 frames con pedales sueltos entre
    // 6 y 7,6 km/h, todos FRENANDO (a mediana -0,21 m/s2), y 39 de ellos (7 %) cumplen |a| <= 0,05 en UN instante por ruido;
    // con 532 candidatos llegar a las 20 muestras era inevitable por azar, y el bus corrio con creep 7,125 medido (Svergino).
    // La condicion de equilibrio tiene que SOSTENERSE: 10 muestras seguidas (0,25 s a 40 Hz). Con un 7 % por azar, la
    // probabilidad de 10 seguidas es 0,07^10 ~ 3e-12; un creep real las sostiene todas. Cualquier rechazo reinicia la racha.
    private const int   CR_PERSIST_FRAMES = 10;
    // --- compuertas de DESPEGUE ---
    private const float L_STOPPED_MS = 0.05;
    private const float L_MOVING_MS  = 0.4;
    private const float L_MIN_THR    = 0.01;  // sin gas no hay despegue; espejo: envelope_emit.L_MIN_THR

    private const int   ST_BUCKETS = 10;   // tramos de comando 0..1

    private const float DT_MIN = 0.005;
    private const float DT_MAX = 0.2;

    private BZEnvelopeStore m_Store;
    private float m_Wheelbase;

    private bool  m_HavePrev;
    private float m_PrevMs;
    private float m_StxCmdPrev;
    private float m_StxAngPrev;
    private int   m_StxEstable;
    private float m_PrevYaw;

    private ref BZWheelRadiusEstimator m_Radius;

    private int m_CoastSamples;
    private int m_KSamples;
    private int m_RadiusSamples;
    private int m_Rejected;
    private int m_RatioSamples2;
    private int m_AccelSamples;
    private int m_PrevGear;
    private int m_GearStable;
    private float m_PrevRpm;
    private float m_PrevPitch;
    private bool  m_WasStopped;
    private bool  m_CreepWindow;
    private bool  m_Calibrando;
    private int   m_CreepSamples;
    private int   m_LaunchSamples;
    private int   m_CrRejThr;
    private int   m_CrRejBrk;
    private int   m_CrRejWheels;
    private int   m_CrRejPitch;
    private int   m_CrRejSpeed;
    private int   m_CrRejStable;
    private int   m_CrRejGear;        // reversa: el creep es el de primera
    private int   m_CrRejPersist;     // en equilibrio pero todavia no sostenido CR_PERSIST_FRAMES
    private int   m_CreepEquilibrio;  // racha de muestras seguidas en equilibrio
    private ref array<int>   m_StCnt;
    private ref array<float> m_StSum;
    private ref array<float> m_StMax;

    void BZEnvelopeRecorder()
    {
        m_StCnt = new array<int>();
        m_StSum = new array<float>();
        m_StMax = new array<float>();
        int b;
        for (b = 0; b < ST_BUCKETS; b++)
        {
            m_StCnt.Insert(0);
            m_StSum.Insert(0.0);
            m_StMax.Insert(0.0);
        }
        m_Radius = new BZWheelRadiusEstimator();
        Reset();
    }

    void Reset()
    {
        m_HavePrev = false;
        m_PrevMs = 0;
        m_PrevYaw = 0;
        m_CoastSamples = 0;
        m_KSamples = 0;
        m_RadiusSamples = 0;
        m_Rejected = 0;
        m_RatioSamples2 = 0;
        m_AccelSamples = 0;
        m_PrevGear = -99;
        m_GearStable = 0;
        m_PrevRpm = 0;
        m_PrevPitch = 0;
        m_WasStopped = false;
        m_CreepWindow = false;
        m_CreepSamples = 0;
        m_LaunchSamples = 0;
        m_CrRejThr = 0;
        m_CrRejBrk = 0;
        m_CrRejWheels = 0;
        m_CrRejPitch = 0;
        m_CrRejSpeed = 0;
        m_CrRejStable = 0;
        m_CrRejGear = 0;
        m_CrRejPersist = 0;
        m_CreepEquilibrio = 0;
        m_Radius.Reset();
    }

    // ★ EL CREEP SE MIDE EN EL BANCO, NO MANEJANDO.
    //
    // Medido in-game: el Sedan_02 es NO-REPTANTE y el envelope reportaba creep = 12.375
    // km/h con 185 muestras. 12 km/h no es un ralenti: es una velocidad de COASTEO. La
    // ruta pasa por ahi sin gas ni freno todo el tiempo, y si en ese instante la
    // aceleracion compensada cae bajo el umbral, la muestra entra -- aunque no sea un
    // equilibrio sino un punto de paso.
    //
    // Y no era inocuo: creepKmh es el OBJETIVO DE ARRASTRE de los modulos POSE. Con 12.375
    // en vez de ~4, la maniobra se acerca a la pose 3x mas rapido y el umbral de "detenido"
    // sube 3x -- justo las dos cosas que inflan el error de cierre que estabamos
    // atribuyendo a la ganancia de direccion.
    //
    // Misma leccion que el barrido de full-lock y el piso de despegue: la magnitud se mide
    // en el REGIMEN PROVOCADO donde esta definida, no donde casualmente se parece.
    void SetCreepWindow(bool activa) { m_CreepWindow = activa; }
    // ★★★ LA TRANSFERENCIA ES SALIDA DE CALIBRACION, NO DEL MANEJO.
    //
    // El drive tambien graba y vuelve a guardar el envelope, asi que la tabla de
    // transferencia CRECIA entre corridas -- y en algun momento cruzaba el umbral de
    // consumo. Medido: dos corridas seguidas de la MISMA ruta, con el MISMO envelope y sin
    // recalibrar, dieron 1.07 (tabla con hueco -> lineal) y 2.90 (la primera lleno el hueco,
    // la segunda la consumio). El vehiculo se portaba distinto en la segunda vuelta.
    //
    // Una medicion que CAMBIA EL COMPORTAMIENTO no puede seguir creciendo mientras se
    // maneja: deja de haber una corrida reproducible. Se congela como salida del banco.
    void SetCalibrando(bool si) { m_Calibrando = si; }

    void Configure(BZEnvelopeStore store, float wheelbaseM)
    {
        m_Store = store;
        m_Wheelbase = wheelbaseM;
        if (m_Store && wheelbaseM > 0.5) m_Store.SetWheelbase(wheelbaseM);
    }

    // Una muestra por frame. Todo en unidades SI salvo lo indicado.
    //   chassisMs   : velocidad REAL del chasis (no la del velocimetro)
    //   yawRad      : rumbo actual
    //   wheelRad    : angulo EJECUTADO de la rueda delantera (no el comando)
    //   wheelOmega  : velocidad angular de rueda (rad/s)
    void Sample(float chassisMs, float yawRad, float wheelRad, float wheelOmega,
                float throttle, float brake, float handbrake,
                int wheelsOnGround, int wheelCount, int surfKind, float dt,
                int gearDayZ, float rpm, float pitchRad, float steerCmd)
    {
        if (!m_Store) return;
        // ⚠ TODA SALIDA TEMPRANA INVALIDA EL FRAME PREVIO. Si no, la proxima muestra deriva
        // contra un estado viejo dividiendo por el dt de UN tick. Vale para las cuatro
        // magnitudes que se derivan de m_PrevMs/m_PrevYaw, no solo el planeo.
        if (dt < DT_MIN || dt > DT_MAX) { m_HavePrev = false; return; }
        if (!BZMath.Finite(chassisMs) || !BZMath.Finite(yawRad)) { m_HavePrev = false; return; }
        if (!BZMath.Finite(pitchRad)) { m_HavePrev = false; return; }

        m_Store.SetSteerMax(AbsF(wheelRad));
        TrySteerTransfer(steerCmd, wheelRad, chassisMs);

        if (m_Radius.AddSample(chassisMs, AbsF(wheelOmega), throttle, brake, wheelsOnGround, wheelCount, wheelRad))
        {
            m_RadiusSamples++;
            if (m_Radius.HasEstimate()) m_Store.SetWheelRadius(m_Radius.RadiusM());
        }

        if (m_HavePrev)
        {
            TryCoast(chassisMs, throttle, brake, handbrake, wheelRad, wheelsOnGround, wheelCount, surfKind, dt, pitchRad, gearDayZ);
            TryUndersteer(chassisMs, yawRad, wheelRad, wheelsOnGround, wheelCount, dt);
            TryRatio(gearDayZ, rpm, wheelOmega, chassisMs, throttle, brake, wheelsOnGround, wheelCount);
            TryMaxAccel(chassisMs, throttle, wheelsOnGround, wheelCount, dt, pitchRad, gearDayZ);
            TryCreep(chassisMs, throttle, brake, handbrake, wheelsOnGround, wheelCount, dt, pitchRad, gearDayZ);
            TryLaunch(chassisMs, throttle, brake, wheelsOnGround, wheelCount, pitchRad);
        }
        m_PrevMs = chassisMs;
        m_PrevYaw = yawRad;
        m_HavePrev = true;
    }

    // ★★★ UN HUECO EN LA GRABACION INVALIDA EL FRAME PREVIO.
    //
    // Todas las magnitudes derivadas de este banco (planeo, aMax, subviraje, reptado) se
    // calculan como (m_PrevMs - ms) / dt, con dt de UN tick. Si el que llama deja de
    // muestrear un rato -- BZ12TestDrive apaga 30 ticks despues de reorientar el vehiculo,
    // y otros 30 tras el vuelo del spawn -- `m_HavePrev` queda en true y la PRIMERA muestra
    // despues del apagon divide un delta-v de 30 ticks por el dt de uno.
    //
    // Amplificacion x30. Medido: el bin 5-10 km/h del Sedan_02 quedo con 4 muestras y media
    // 5.4186 m/s2, cuando el resto de los bins del MISMO vehiculo miden 0.55-1.10. De ahi
    // salia el "planeo -5.49" en terreno llano, y con el la teoria de un acantilado de
    // deceleracion en reversa que no existe: el escalon caia clavado en 10.000 km/h, que es
    // un BORDE DE BIN y no una velocidad.
    //
    // Lo tiene que declarar quien apaga: el grabador no puede saber que paso un hueco.
    void HuboUnHueco()
    {
        m_HavePrev = false;
    }

    // ======================= COASTEO =======================

    // El planeo se mide SOLTANDO TODO. Si hay gas, freno, handbrake o el volante cruzado,
    // lo que se mide no es el planeo del vehiculo: es el planeo mas lo que estas haciendo.
    private void TryCoast(float ms, float thr, float brk, float hb, float wheelRad,
                          int wog, int wc, int surfKind, float dt, float pitchRad, int gearDayZ)
    {
        if (thr > C_MAX_THROTTLE) { m_Rejected++; return; }
        if (brk > C_MAX_BRAKE) { m_Rejected++; return; }
        if (hb > C_MAX_HB) { m_Rejected++; return; }
        if (AbsF(wheelRad) > C_MAX_STEER) { m_Rejected++; return; }
        if (wc > 0 && wog < wc) { m_Rejected++; return; }
        if (ms < C_MIN_MS) { m_Rejected++; return; }
        float decel = (m_PrevMs - ms) / dt;      // positivo = desacelera, CRUDO
        // ★★ EL PLANEO SE MIDE COMPENSADO POR PENDIENTE, Y EL FILTRO VA SOBRE LO COMPENSADO.
        //
        // Esto guardaba la desaceleracion CRUDA, o sea planeo + gravedad, y encima filtraba
        // sobre el crudo. En una ruta con bajadas eso descarta justo las muestras donde el
        // vehiculo planeaba normal (la gravedad las disfraza de "casi sin desacelerar") y
        // guarda un promedio muy por debajo del planeo real.
        //
        // Medido en el drive: el modelo predecia 0.29-0.37 m/s2 y el vehiculo desaceleraba
        // 0.9-1.2 -- con la gravedad de la bajada descontada, el planeo real es ~1.44-1.54.
        // Un factor 4. Y el controlador planificaba sobre eso: soltaba el gas creyendo que
        // caia despacio, y caia al umbral de bog.
        //
        // Es EXACTAMENTE el mismo bug que ya estaba arreglado dos metodos mas abajo para el
        // piso de despegue ("el rango se valida sobre el valor COMPENSADO, no sobre el
        // crudo"). Misma leccion, otro banco, sin aplicar.
        // ⚠ COMPUERTA DE PITCH: coast era el UNICO consumidor del termino de gravedad sin
        // ninguna disciplina de pitch, cuando TryMaxAccel (A_MAX_PITCH) y TryCreep
        // (CR_MAX_PITCH) hace rato que la tienen. El limite se deriva del ruido medido: el
        // termino de gravedad no puede superar una sigma de muestra (0.376 m/s2), o sea
        // asin(0.376/9.81) = 0.0383 rad = 2.2 grados. Mas que eso y la correccion pesa mas
        // que la señal que se quiere medir.
        if (AbsF(pitchRad) > C_MAX_PITCH) { m_Rejected++; return; }
        // ★★★ Y EL SIGNO DE LA GRAVEDAD SIGUE AL SENTIDO DE MARCHA, NO AL MORRO.
        //
        // chassisMs es una MAGNITUD (GetVelocity().Length()), asi que retrocediendo cuesta
        // abajo el vehiculo gana velocidad mientras el pitch dice "morro abajo": la
        // correccion se aplicaba al reves y valia 2*9.81*sin(pitch) de error -- a 5 grados,
        // 1.71 m/s2, mas que TODO el rango de la curva medida (0.23 a 1.10). Retrocediendo
        // con 14.4 grados, un planeo real de 0.55 sale 5.42: el valor exacto del bin que
        // envenenó la banda 0-10 km/h. Es el segundo mecanismo que reproduce ese numero,
        // independiente del frame rancio, y sin corregirlo la reversa nunca se mide bien.
        float sentido = 1.0;
        if (gearDayZ == 0) sentido = -1.0;
        float coastPlano = decel - sentido * 9.81 * Math.Sin(pitchRad);
        if (coastPlano < C_MIN_DECEL) return;    // planeando de menos: no aporta
        if (coastPlano > C_MAX_DECEL) { m_Rejected++; return; }
        m_Store.AddCoast(surfKind, ms * 3.6, coastPlano);
        m_CoastSamples++;
    }

    // ======================= UNDERSTEER =======================

    // k = L*kappa / tan(delta), con kappa MEDIDA (yawRate/v) y delta el angulo REAL de la
    // rueda. Los dos lados salen del motor: es EXTRACCION, no inferencia. Reemplaza el
    // k=0.90 inventado de v1.1 (medido ~0.50 en el M3).
    private void TryUndersteer(float ms, float yawRad, float wheelRad, int wog, int wc, float dt)
    {
        if (ms < K_MIN_MS) return;
        if (AbsF(wheelRad) < K_MIN_ANGLE) return;
        if (wc > 0 && wog < wc) return;
        if (m_Wheelbase < 0.5) return;
        float yawRate = BZMath.WrapPi(yawRad - m_PrevYaw) / dt;
        if (AbsF(yawRate) < K_MIN_YAWRATE) return;
        float kappa = yawRate / ms;
        float tanD = Math.Tan(wheelRad);
        if (AbsF(tanD) < 0.001) return;
        float k = (m_Wheelbase * kappa) / tanD;
        if (k <= 0) { m_Rejected++; return; }        // signos opuestos: derrape, no giro
        if (k > K_MAX) { m_Rejected++; return; }
        m_Store.AddK(ms * 3.6, k);
        m_KSamples++;
    }

    // RATIO TOTAL de la marcha = vueltas del motor / vueltas de rueda. Los dos lados los
    // da el motor, asi que el ratio se MIDE -- y deja de importar que el config de la caja
    // no resuelva. A fondo se descarta: con patinada el cociente da un ratio inventado.
    // ⚠ DOS CORRECCIONES SOBRE LA PRIMERA VERSION, las dos por el MISMO sintoma: el ratio
    // de 1ra no convergia entre corridas (27.88 -> 14.42, casi exactamente la mitad, y
    // 25.99 antes). Un factor ~2 no es ruido: es OTRA MARCHA o OTRA RUEDA.
    //
    // (1) CAMBIO DE MARCHA. Durante y justo despues de un cambio, el rpm todavia
    //     corresponde a la marcha vieja y la rueda ya gira segun la nueva. El cociente
    //     mezcla las dos y sale un ratio que no es el de ninguna -- y como se guarda bajo
    //     la marcha que el motor REPORTA, contamina el promedio de esa marcha. Se exige
    //     que la marcha lleve RT_STABLE muestras sin cambiar.
    //
    // (2) DE QUE RUEDA. omega venia de UNA rueda elegida por indice (la ultima). Si la
    //     cantidad de attachments cambia entre corridas, el indice apunta a otra rueda; y
    //     si esa rueda patina o esta en el aire, omega se infla y el ratio se desploma.
    //     La velocidad del CHASIS no tiene ninguno de los dos problemas, y el radio ya
    //     esta medido -> omega = v/r. Se usa esa cuando hay radio; la rueda queda de
    //     respaldo. Es la misma leccion del aMax: medir lo que no depende del montaje.
    private void TryRatio(int gearDayZ, float rpm, float omega, float chassisMs,
                          float thr, float brk, int wog, int wc)
    {
        if (gearDayZ != m_PrevGear)
        {
            m_PrevGear = gearDayZ;
            m_GearStable = 0;
            return;
        }
        m_GearStable++;
        // (1b) CONTINUIDAD DE RPM. El indicador de marcha y el rpm no cambian en el mismo
        // frame: se ve (marcha vieja, rpm nuevo) tanto como (marcha nueva, rpm viejo). El
        // gate por numero de marcha solo caza la segunda mitad. Un salto de rpm del orden
        // del escalon de caja EN UN FRAME no es un motor acelerando -- es un cambio, y el
        // cociente de ese frame no es el ratio de ninguna marcha. Esta es la mitad que
        // producia el 27.88 -> 14.42 (casi exactamente el escalon 1ra/2da).
        float rpmPrev = m_PrevRpm;
        m_PrevRpm = rpm;
        if (rpmPrev > 1.0)
        {
            float salto = rpm - rpmPrev;
            if (salto < 0) salto = -salto;
            if (salto / rpmPrev > RT_MAX_RPM_JUMP) { m_GearStable = 0; m_Rejected++; return; }
        }
        if (m_GearStable < RT_STABLE) return;
        if (gearDayZ < 0) return;
        if (thr > RT_MAX_THR) return;
        if (brk > RT_MAX_BRAKE) return;
        if (rpm < RT_MIN_RPM) return;
        if (wc > 0 && wog < wc) return;
        // ⚠ SIN RESPALDO POR RUEDA. Antes, si la velocidad del chasis no llegaba al umbral,
        // se caia al omega de la rueda -- que cerca de cero hace explotar rpm/omega. Eso
        // produjo el 54.75 del reporte (rango 3.94 a 54.75 DENTRO de la misma marcha, x13.9).
        // Un momento casi-detenido NO ES UNA MUESTRA DE RATIO: no hay respaldo que valga,
        // hay que no medir. Y el umbral sube, porque el error relativo del ratio es el
        // error relativo de v: a 2 m/s un error de 20 cm/s ya es 10%.
        float r = m_Store.WheelRadiusM();
        if (r <= 0.05) return;
        if (chassisMs < RT_MIN_MS) return;
        float w = chassisMs / r;
        if (w < RT_MIN_OMEGA) return;
        float ratio = (rpm * 6.28318530718 / 60.0) / w;
        if (ratio < 1.0 || ratio > 100.0) { m_Rejected++; return; }
        m_Store.AddRatio(gearDayZ, ratio);
        m_RatioSamples2++;
    }

    // ACELERACION MAXIMA real. Derivarla de torque x ratio / radio / masa encadena cuatro
    // datos que pueden estar en FALLBACK; medir lo que el vehiculo LOGRA a fondo no
    // depende de ninguno. Se usa la velocidad del CHASIS, asi que la patinada no infla.
    //
    // ⚠ COMPENSADA POR PENDIENTE, y no es un detalle: lo que se observa es la aceleracion
    // NETA, con la gravedad adentro. Medida en subida sale de menos y en bajada de mas --
    // el mismo vehiculo daria numeros distintos segun DONDE se lo midio, que es justo lo
    // que un envelope no puede permitirse. Peor todavia: aMax alimenta HoldThrottle
    // (g*sin(pitch)/aMax), asi que un aMax sub-medido en subida hace que el hill-start
    // pida MAS gas del que existe, y uno inflado en bajada que pida de menos.
    //
    // El termino de gravedad es EXACTO y no necesita ningun modelo, asi que se puede
    // quitar sin introducir supuestos. Lo que queda -- rodadura + aero -- se mide aparte
    // como coast, y el consumidor lo descuenta ahi. Definicion: aMax es la aceleracion
    // neta alcanzable EN LLANO.
    //
    // Esto es lo que explica el 1.49 m/s2 medido en SEDAN_SHORT: la unica parte a fondo
    // de esa ruta es la cuesta, y ahi la gravedad se comia ~0.75 m/s2 de la medicion.
    private void TryMaxAccel(float ms, float thr, int wog, int wc, float dt, float pitchRad, int gearDayZ)
    {
        float pitchPrev = m_PrevPitch;
        m_PrevPitch = pitchRad;
        if (thr < A_MIN_THR) return;
        if (wc > 0 && wog < wc) return;
        if (!BZMath.Finite(pitchRad)) return;
        // PRIMERA MARCHA Y BAJA VELOCIDAD. Sin acotar esto, "aceleracion maxima" no es una
        // magnitud: a fondo en 5ta a 100 km/h el vehiculo acelera poquisimo, y a fondo en
        // 1ra desde parado muchisimo. Promediar las dos no describe a ningun vehiculo.
        // aMax se usa para SOSTENER una cuesta desde casi-parado, asi que se mide ahi.
        if (gearDayZ != A_GEAR) return;
        if (ms > A_MAX_MS) return;
        // ⚠ POZOS. La compensacion de gravedad hace exacta la medicion en pendiente, pero
        // le da un canal de entrada al ruido: un pozo que levante la trompa 20 grados le
        // suma 3.4 m/s2 a esa muestra. Se acota la pendiente y, sobre todo, se rechaza la
        // pendiente que CAMBIA rapido -- una cuesta es lenta, un pozo es instantaneo.
        if (AbsF(pitchRad) > A_MAX_PITCH) { m_Rejected++; return; }
        float dPitch = pitchRad - pitchPrev;
        if (dPitch < 0) dPitch = -dPitch;
        if (dPitch / dt > A_MAX_PITCH_RATE) { m_Rejected++; return; }
        // ⚠ Y QUE ESTE RODANDO. Parado, "acelerar" es romper la inercia estatica, que es
        // otro fenomeno (y para eso esta launchThrottleFloor).
        if (ms < A_MIN_MS) return;
        float a = (ms - m_PrevMs) / dt + 9.81 * Math.Sin(pitchRad);
        if (a > A_MAX) { m_Rejected++; return; }
        // ⚠⚠ EL VEHICULO EN SU LIMITE NO DEMUESTRA SU MAXIMO.
        // Gas a fondo CON ACELERACION CASI CERO es el auto clavado -- en la cuesta se
        // midio thr 1.0 a 0.03 km/h. Esa muestra no dice "este auto acelera 0": dice "aca
        // no puede". Es la pregunta OPUESTA a la que aMax contesta, y meterla en la bolsa
        // hundio el promedio a 0.42 (2059 muestras, rango 0.0013 a 5.72). Se descarta el
        // regimen entero, no se compensa despues con un estadistico mas listo.
        if (a < A_MIN_ACCEL) { m_Rejected++; return; }
        m_Store.AddAccel(a);
        m_AccelSamples++;
    }

    // ★ CREEP MEDIDO DIRECTO. Es la velocidad a la que el vehiculo se queda SOLO: sin
    // gas, sin freno, en llano, en 1ra. Se venia derivando de  ratio -> rpm -> velocidad,
    // una cadena de tres datos de los que el primero no converge. Medir la magnitud que se
    // USA, en vez de derivarla de otras que no se pueden medir, saca la cadena del medio.
    //
    // La compuerta clave es ESTABILIDAD: coasteando desde 40 km/h el vehiculo pasa por la
    // velocidad de creep sin que eso sea creep. Creep es el EQUILIBRIO -- donde el ralenti
    // empata al arrastre y la aceleracion es ~0.
    private void TryCreep(float ms, float thr, float brk, float hb, int wog, int wc,
                          float dt, float pitchRad, int gearDayZ)
    {
        // ⚠ CONTADORES POR COMPUERTA. El creep dio CERO muestras dos corridas seguidas y
        // las dos veces hubo que deducir por que. Un rechazo sin causa es un diagnostico
        // que se paga con una corrida entera; con esto el log lo dice.
        if (!m_CreepWindow) { m_CreepEquilibrio = 0; return; }   // fuera del banco no se mide creep
        if (thr > C_MAX_THROTTLE) { m_CrRejThr++; m_CreepEquilibrio = 0; return; }
        if (brk > C_MAX_BRAKE) { m_CrRejBrk++; m_CreepEquilibrio = 0; return; }
        if (hb > C_MAX_HB) { m_CrRejBrk++; m_CreepEquilibrio = 0; return; }
        if (wc > 0 && wog < wc) { m_CrRejWheels++; m_CreepEquilibrio = 0; return; }
        // ⚠ SIN COMPUERTA DE MARCHA, a proposito. Exigir gearDayZ == 2 agrega una
        // dependencia del indice de marcha -- justo el dato del que ya sabemos que no nos
        // podemos fiar -- y no hace falta: en punto muerto el motor no empuja, asi que
        // a_obs - aGrav = -planeo (~ -0.15), y la compuerta de estabilidad de abajo lo
        // rechaza sola. La fisica ya selecciona el regimen; el indice solo puede mentir.
        if (AbsF(pitchRad) > CR_MAX_PITCH) { m_CrRejPitch++; m_CreepEquilibrio = 0; return; }
        // DETENIDO Y QUIETO = evidencia de que NO REPTA. Se cuenta aparte: no es una
        // muestra de "a que velocidad repta", es una muestra de "no repta".
        if (ms < CR_MIN_MS)
        {
            float aQuieto = (ms - m_PrevMs) / dt;
            float aGravQ = -9.81 * Math.Sin(pitchRad);
            if (AbsF(aQuieto - aGravQ) <= CR_MAX_ACCEL) m_Store.AddNoCreep();
            m_CrRejSpeed++;
            m_CreepEquilibrio = 0;
            return;
        }
        if (ms > CR_MAX_MS) { m_CrRejSpeed++; m_CreepEquilibrio = 0; return; }
        // ⚠ COMPENSADO POR PENDIENTE, igual que aMax -- y por el mismo motivo: el spawn
        // de CONVOY_03 quedo a -1.3 grados y el creep salio en CERO muestras. En bajada el
        // vehiculo NUNCA se asienta: la gravedad lo sigue empujando, asi que la condicion
        // "aceleracion ~0" no se cumple jamas y la medicion no existe.
        //
        // La correccion es exacta y no necesita ningun modelo. Lo observado es
        //     a_obs = a_motor(v) - planeo(v) - g*sin(pitch)
        // y el creep de LLANO es la velocidad donde a_motor - planeo = 0, o sea donde
        //     a_obs = -g*sin(pitch)
        // En llano se reduce a la condicion de siempre. En pendiente, en vez de esperar un
        // equilibrio que no va a pasar, se busca el CRUCE por esa velocidad -- que existe
        // igual, solo que el vehiculo pasa por ella en lugar de quedarse.
        float a = (ms - m_PrevMs) / dt;
        float aGrav = -9.81 * Math.Sin(pitchRad);
        if (AbsF(a - aGrav) > CR_MAX_ACCEL) { m_CrRejStable++; m_CreepEquilibrio = 0; return; }
        // En reversa no hay creep que medir: el creep es el de PRIMERA (en la toma del bus, 281 de los 532 candidatos iban en
        // reversa, planeando). Esto no es la compuerta de marcha de arriba (exigir == 2): solo descarta la reversa.
        if (gearDayZ == 0) { m_CrRejGear++; m_CreepEquilibrio = 0; return; }
        m_CreepEquilibrio = m_CreepEquilibrio + 1;
        if (m_CreepEquilibrio < CR_PERSIST_FRAMES) { m_CrRejPersist++; return; }
        m_Store.AddCreep(ms * 3.6);
        m_CreepSamples++;
    }

    // ★ PISO DE DESPEGUE MEDIDO DIRECTO: el gas con el que el vehiculo EMPIEZA A MOVERSE
    // desde parado. Una muestra por despegue, no una por frame -- si no, el histograma
    // se llena con el gas de los frames posteriores, que ya no es el piso.
    private void TryLaunch(float ms, float thr, float brk, int wog, int wc, float pitchRad)
    {
        if (wc > 0 && wog < wc) { m_WasStopped = false; return; }
        if (brk > C_MAX_BRAKE)  { m_WasStopped = false; return; }
        if (ms < L_STOPPED_MS)
        {
            m_WasStopped = true;
            return;
        }
        if (!m_WasStopped) return;
        m_WasStopped = false;
        if (ms > L_MOVING_MS) return;            // ya venia lanzado: no es el instante
        // ★ SIN GAS NO HAY DESPEGUE. Un cruce de L_STOPPED_MS con el gas en cero es la
        // gravedad (o el freno que se solto en bajada), no el piso de gas del vehiculo. La compensacion
        // de abajo convertia ese cero en +g*sin(pitch)/aMax y lo guardaba como piso: medido en 502 tomas,
        // las 14 muestras de launch que salian de tomas de teclado eran TODAS de gas 0.0 en bajada
        // (thrPlano 0.02-0.09), y el bus quedo con LaunchFloor 0.07 MEDIDO. Se exige gas CRUDO antes
        // de compensar: la pendiente corrige un piso observado, no fabrica uno.
        if (thr < L_MIN_THR) { m_Rejected++; return; }
        // ⚠ EL RANGO SE VALIDA SOBRE EL VALOR COMPENSADO, NO SOBRE EL CRUDO.
        // Tenerlo al reves anulaba toda la compensacion justo donde hacia falta: en bajada
        // el vehiculo cruza el umbral de movimiento un frame ANTES, o sea con MENOS gas
        // crudo, y el piso de 0.01 lo descartaba antes de que la correccion pudiera
        // devolverle lo que la gravedad habia regalado. Simulado en el spawn real de
        // CONVOY_03 (-1.3 grados): 0 muestras con el filtro crudo primero.
        if (thr > 0.99) return;
        // ⚠ TAMBIEN COMPENSADO. En bajada la gravedad AYUDA a romper la inercia, asi que
        // el piso observado sale de menos: en el spawn a -1.3 grados dio 0.13, cuando el
        // piso de llano de ese vehiculo esta cerca de 0.27. El gas que la gravedad regalo
        // es g*sin(pitch)/aMax, y se lo devolvemos.
        float thrPlano = thr;
        float aMax = m_Store.MaxAccelMs2();
        if (aMax > 0.1) thrPlano = thr - (9.81 * Math.Sin(pitchRad)) / aMax;
        if (thrPlano < 0.01 || thrPlano > 0.99) { m_Rejected++; return; }
        m_Store.AddLaunch(thrPlano);
        m_LaunchSamples++;
    }

    // ★ LA TRANSFERENCIA COMANDO -> ANGULO. Es lo que steerKPose necesita y nadie estaba
    // midiendo: `steerKPose = 1/steerMaxRad` convierte un angulo deseado en un comando
    // normalizado, asi que el denominador correcto es EL ANGULO QUE PRODUCE UN COMANDO
    // DE 1.0 -- no "el angulo mas grande que se vio alguna vez".
    //
    // El barrido estatico dice 30.5 grados (= el config) y la observacion de manejo dice
    // 75. Las dos no pueden ser la misma magnitud. Esta tabla lo resuelve MIDIENDO en vez
    // de discutiendo: si a comando 1.0 las ruedas ejecutan 75, entonces 75 es el valor
    // correcto para la conversion; si a comando 1.0 ejecutan 30 y los 75 aparecen con
    // comandos CHICOS, entonces los 75 son un artefacto y el problema esta en otro lado.
    private void TrySteerTransfer(float cmd, float wheelRad, float ms)
    {
        float c = AbsF(cmd);
        float a = AbsF(wheelRad);
        if (a >= 1.5708) return;               // mismo tope fisico: 90 grados no es direccion
        if (!m_Calibrando) return;             // solo el banco la mide; el drive la USA
        // ★★★ LA TRANSFERENCIA SOLO SIGNIFICA ALGO CON EL VEHICULO EN MOVIMIENTO.
        //
        // El banco barre el volante CON EL FRENO PUESTO -- correcto para medir la CAPACIDAD
        // (steerMax), porque ahi interesa el angulo que la rueda alcanza, no moverse. Pero
        // la transferencia comando->angulo se estaba grabando en esas mismas fases, y con el
        // vehiculo detenido la rueda se queda donde la dejo el barrido anterior mientras el
        // comando ya volvio a cero.
        //
        // Medido: el balde de comando 0-0.1 junto 2477 muestras con la rueda a 26.58 grados
        // de PROMEDIO (max 30.00, el tope exacto) en el bus, y 30.69 en el CivilianSedan.
        // O sea: "con el volante al medio la rueda esta al tope". No es lag, es una lectura
        // rancia -- la misma familia que el frame previo del planeo.
        //
        // Y por eso solo se poblaban DOS baldes de diez: los extremos que el banco comanda.
        // El tramo de comandos que el controlador usa manejando -- las correcciones chicas
        // y medias -- quedaba SIN MEDIR. Con la compuerta de movimiento, la ruta misma lo
        // llena: manejando se comandan todos los valores intermedios.
        //
        // El umbral es el mismo que ya exige la medicion de subviraje: sin velocidad no hay
        // respuesta de direccion que medir.
        if (ms < K_MIN_MS) return;
        // ★★★ Y SOLO EN LA MESETA: comando Y angulo quietos.
        //
        // Con la compuerta de movimiento sola, la tabla salio ASI (CivilianSedan):
        //     balde 0 (cmd~0.05): 7852 muestras -> 0.032 rad   = 1.05x lo lineal
        //     balde 1 (cmd~0.15):  374 muestras -> 0.200 rad   = 2.15x
        //     balde 5 (cmd~0.55):  152 muestras -> 0.462 rad   < el balde 4 (NO monotona)
        // Los conteos lo delatan: el vehiculo pasa el tiempo cerca de cero y CRUZA los
        // baldes del medio. O sea que esos baldes son TRANSICIONES, y promediarlos mide el
        // retardo del volante, no la transferencia. Consumir eso hizo que el control pidiera
        // menos de la mitad de comando y el auto empeoro 4.5x (0.49 -> 2.21 de cross-track).
        //
        // La condicion no necesita ningun numero de tiempo elegido: se graba cuando las DOS
        // señales dejaron de moverse. Si el par (comando, angulo) no es simultaneo, no es
        // una transferencia -- es una foto de dos instantes distintos.
        bool quieto = (AbsF(cmd - m_StxCmdPrev) < STX_EPS_CMD && AbsF(wheelRad - m_StxAngPrev) < STX_EPS_ANG);
        m_StxCmdPrev = cmd;
        m_StxAngPrev = wheelRad;
        if (!quieto)
        {
            m_StxEstable = 0;
            return;
        }
        m_StxEstable++;
        if (m_StxEstable < STX_ESTABLE_TICKS) return;
        int b = (int)(c * ST_BUCKETS);
        if (b < 0) b = 0;
        if (b > ST_BUCKETS - 1) b = ST_BUCKETS - 1;
        m_StCnt.Set(b, m_StCnt.Get(b) + 1);
        m_StSum.Set(b, m_StSum.Get(b) + a);
        // Y al STORE, que es lo que se persiste y lo que el modelo consume. Los arrays de
        // arriba quedan solo para el reporte de la corrida.
        m_Store.AddSteerTransfer(c, a);
        if (a > m_StMax.Get(b)) m_StMax.Set(b, a);
    }

    // Tabla legible: por cada tramo de comando, cuantas muestras y que angulo dio.
    string SteerTransferReport()
    {
        int llenos = 0;
        int i;
        for (i = 0; i < ST_BUCKETS; i++)
        {
            if (m_StCnt.Get(i) > 0) llenos++;
        }
        string s = "[Envelope] transferencia comando->angulo de rueda (" + llenos.ToString() + "/" + ST_BUCKETS.ToString() + " baldes con dato):";
        for (i = 0; i < ST_BUCKETS; i++)
        {
            if (m_StCnt.Get(i) <= 0) continue;
            float prom = m_StSum.Get(i) / m_StCnt.Get(i);
            s = s + "\n   cmd " + (i * 1.0 / ST_BUCKETS).ToString() + "-" + ((i + 1) * 1.0 / ST_BUCKETS).ToString();
            s = s + ": " + m_StCnt.Get(i).ToString() + " muestras · prom " + (prom * Math.RAD2DEG).ToString();
            s = s + " deg · max " + (m_StMax.Get(i) * Math.RAD2DEG).ToString() + " deg";
        }
        return s;
    }

    private float AbsF(float v)
    {
        if (v < 0) return -v;
        return v;
    }

    // Por que se rechazaron las muestras de creep. Sin esto, un cero es un misterio.
    string CreepReject()
    {
        string s = "[Envelope] creep: " + m_CreepSamples.ToString() + " aceptadas · rechazos:";
        s = s + " gas " + m_CrRejThr.ToString();
        s = s + " · freno " + m_CrRejBrk.ToString();
        s = s + " · ruedas " + m_CrRejWheels.ToString();
        s = s + " · pendiente " + m_CrRejPitch.ToString();
        s = s + " · velocidad " + m_CrRejSpeed.ToString();
        s = s + " · NO-ESTABLE " + m_CrRejStable.ToString();
        s = s + " · REVERSA " + m_CrRejGear.ToString() + " · SIN-PERSISTIR " + m_CrRejPersist.ToString();
        return s;
    }

    int CoastSamples()  { return m_CoastSamples; }
    int KSamples()      { return m_KSamples; }
    int RadiusSamples() { return m_RadiusSamples; }
    int Rejected()      { return m_Rejected; }

    string Report()
    {
        string s = "[Envelope] muestras aceptadas: coast " + m_CoastSamples.ToString();
        s = s + " · k " + m_KSamples.ToString();
        s = s + " · radio " + m_RadiusSamples.ToString();
        s = s + " · ratio " + m_RatioSamples2.ToString();
        s = s + " · aMax " + m_AccelSamples.ToString();
        s = s + " · creep " + m_CreepSamples.ToString();
        s = s + " · despegue " + m_LaunchSamples.ToString();
        s = s + " · rechazadas " + m_Rejected.ToString();
        return s;
    }
}
