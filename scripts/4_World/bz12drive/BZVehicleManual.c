// ============================================================================
//  BZVehicleManual.c - BZ_AutoDrive 1.2
//
//  El "MANUAL DE MANEJO" del vehiculo: read-only, UNA SOLA VERDAD. Lo produce
//  BZVehicleDataReader una vez al spawn y lo leen los CINCO modulos de manejo --
//  los de trayectoria Y los de pose. Exencion de la DerivationEngine NO es
//  exencion del Manual (SEAM.md 4.1): sin esto el tuning de MC/Slam se hornea
//  contra el Sedan y el M3 se planta 8.5m antes.
//
//  PROCEDENCIA POR CAMPO (MODULES.md 1.1):
//    CONFIG   = leido del vehiculo tal cual
//    DERIVED  = formula desde CONFIG. Es un PRIOR, no una verdad.
//    MEASURED = extraido de la demostracion. NO hay formula que lo de.
//    FALLBACK = no se pudo leer ni medir. Se loguea FUERTE.
//
//  LAS TRES REGLAS:
//   1. Lo MEDIDO manda. Reemplaza al prior; no se promedia ni se blendea, y NUNCA
//      se corrige la medicion para acercarla al modelo.
//   2. Hay cosas que SOLO se saben midiendo: CoastDecel(v,piso) y k(v).
//   3. FALLBACK es ruidoso. Un fallback callado es un numero magico disfrazado.
// ============================================================================

// ORDENADA POR CALIDAD, de peor a mejor. Ese orden NO es cosmetico: hace que la
// herencia transitiva de procedencia sea, literalmente, un MINIMO (ver SetDerivedFrom).
//   NONE     = nunca se seteo
//   FALLBACK = ni se leyo ni se midio -> valor de emergencia
//   DERIVED  = formula desde otros campos. Es un PRIOR.
//   CONFIG   = leido del vehiculo
//   MEASURED = extraido de la demostracion
enum BZProv
{
    BZPV_NONE,
    BZPV_FALLBACK,
    BZPV_DERIVED,
    BZPV_CONFIG,
    BZPV_MEASURED
}

// Aspectos con procedencia rastreada.
class BZAsp
{
    static const int MASS        = 0;
    static const int WHEELBASE   = 1;
    static const int WHEELRADIUS = 2;
    static const int RPM         = 3;
    static const int GEARING     = 4;
    static const int BRAKE       = 5;
    static const int AERO        = 6;
    static const int STEERMAX    = 7;
    static const int CREEP       = 8;
    static const int CLUTCHBITE  = 9;
    static const int LAUNCH      = 10;
    static const int STEERPOSE   = 11;
    static const int UNDERSTEER  = 12;
    static const int COAST       = 13;
    static const int TOPSPEED    = 14;
    static const int BRAKEDECEL  = 15;
    static const int STEERTRANSF = 16;
    static const int COUNT       = 17;

    static string Name(int a)
    {
        if (a == MASS)        return "mass";
        if (a == WHEELBASE)   return "wheelbase";
        if (a == WHEELRADIUS) return "wheelRadius";
        if (a == RPM)         return "rpm";
        if (a == GEARING)     return "gearing";
        if (a == BRAKE)       return "brake";
        if (a == AERO)        return "aero";
        if (a == STEERMAX)    return "steerMax";
        if (a == CREEP)       return "creepKmh";
        if (a == CLUTCHBITE)  return "clutchBiteRpm";
        if (a == LAUNCH)      return "launchThrottle";
        if (a == STEERPOSE)   return "steerKPose";
        if (a == UNDERSTEER)  return "understeerK";
        if (a == COAST)       return "coastDecel";
        if (a == TOPSPEED)    return "topSpeed";
        if (a == BRAKEDECEL)  return "maxBrakeDecel";
        if (a == STEERTRANSF) return "steerTransfer";
        return "?";
    }

    static string ProvName(int p)
    {
        if (p == BZProv.BZPV_CONFIG)   return "CONFIG";
        if (p == BZProv.BZPV_DERIVED)  return "DERIVED";
        if (p == BZProv.BZPV_MEASURED) return "MEASURED";
        if (p == BZProv.BZPV_FALLBACK) return "FALLBACK";
        return "NONE";
    }
}

class BZVehicleManual
{
    // PISO DE DESPEGUE SIN CALIBRAR = PRIOR DE FLOTA = mediana de lo calibrado con la rampa
    // (BZ12TestDrive.FaseLaunch, gear 2, llano, gas del primer tick que cruza 0,05 m/s sostenido 4 ticks). 22/09/2026,
    // 7 clases: CivilianSedan 0,05 · Harley 0,07 · PAZ_Yellow 0,11 · Tiger 0,13 · ChernoBus_02 0,15 · ChernoBus 0,15
    // · Truck_01_Covered 0,25 => mediana 0,13. Reemplaza al 0,35 (1,4x a 7x de mas en las 7: el que recorto el gas en
    // Svergino) y al derivado masa*0,5/fMax con par PICO (0,6x a 7,5x del medido: no hay factor que lo arregle; en DayZ
    // el embrague bloquea y manda el par a ralenti en 1a, que el config no da). Costo asimetrico: de mas = golpe de gas
    // y sosten inflado en 6 consumidores; de menos = la rampa tarda +0,5 s por escalon. Se re-deriva con la misma
    // receta (mediana de lo calibrado) cuando crezca la muestra; el que calibra no lo usa.
    static const float LAUNCH_PRIOR_FLOTA = 0.13;

    // Bins del envelope: MISMA grilla que BZVehicleEnvelope (0..100 km/h de a 5).
    static const int   NBINS = 20;
    static const float BINW  = 5.0;

    string vehicleClass;

    // ---- CONFIG ----
    static const float DRIVETRAIN_EFF = 0.85;
    // Debajo de esta velocidad w_rueda tiende a 0 y rpm/w explota.
    static const float RATIO_INST_MIN_MS = 2.5;
    // Banda dentro de la cual la relacion MEDIDA se considera la de la caja. Ver el bloque
    // en SpeedAtBiteKmh: fuera de esto el embrague patina y el cociente no mide una caja.
    static const float COHER_MIN = 0.85;
    static const float COHER_MAX = 1.15;

    private bool m_WarnedNoTorque;

    float massKg;
    float wheelbaseM;
    float wheelRadiusM;
    float steerMaxRad;
    int   driveKind;              // 0=FWD 1=RWD 2=AWD
    float rpmIdle;
    float rpmRedline;
    float finalDrive;
    float centDiffRatio;
    // La caja declara la reversa APARTE del array de ratios (que trae solo las marchas
    // hacia adelante). Sin esto, cualquier cuenta de fuerza en reversa usa el ratio de
    // 1ra y da un numero que no es el del vehiculo.
    float reverseRatio;
    float maxBrakeTorqueFront;
    float maxBrakeTorqueRear;
    float dragCoef;
    float frontalArea;
    ref array<float> gearRatios;  // indice 0 = 1ra FISICA (gear DayZ = idx + 2)
    ref array<float> torqueCurve; // pares (rpm, Nm)
    // ★★★ LA VELOCIDAD DEL ACTUADOR DE DIRECCION, pares (kmh, deg/s). El motor NO mueve el
    // volante instantaneamente: `decreaseSpeed` es la curva del CONTRAVOLANTE, y en los tres
    // supercars cae a 15-20 deg/s a 100 km/h. Sin este dato el controlador cree que su
    // comando llega de golpe cuando en realidad tarda mas de un segundo en entrar.
    ref array<float> steerDecCurve;  // decreaseSpeed: volante volviendo / contravolante
    ref array<float> steerIncCurve;  // increaseSpeed: volante alejandose del centro
    float downforceCoef;             // >0: el agarre CRECE con la velocidad
    private float m_RpmBanda;   // cache de RpmDeBanda()

    // ---- DERIVED (priors) ----
    float topSpeedKmh;
    // ⚠ DOS COSAS DISTINTAS QUE HASTA AHORA COMPARTIAN UN CAMPO.
    //
    // creepKmh es el OBJETIVO DE ARRASTRE de los modulos POSE: la velocidad a la que se
    // acercan a una pose. idleCreepKmh es la MEDICION: lo que el vehiculo hace solo, sin
    // gas. En un vehiculo que repta coinciden, y por eso nunca se noto la diferencia.
    //
    // El Sedan_02 NO repta: su medicion es CERO, y es un dato real (evidencia positiva:
    // detenido, estable, en llano, sin comandos). Pero volcar ese cero al objetivo de
    // control seria pedirle a las maniobras que se acerquen a la pose a 0 km/h -- o sea,
    // que no se muevan. La medicion es cero; el objetivo no puede serlo.
    float creepKmh;               // OBJETIVO de arrastre de los modulos POSE
    float idleCreepKmh;           // MEDIDO: lo que hace solo. 0 = no repta (dato valido)
    // la frenada a fondo MEDIDA en la parada final de la toma (m/s2); -1 sin medicion. Solo la lee el ENDPOINT.
    private float m_FrenoTomaMs2 = -1;
    private const float FRENO_TOMA_MIN_MS2 = 2.0;   // por debajo no es una frenada a fondo: no se usa
    void SetFrenoToma(float aMs2) { m_FrenoTomaMs2 = aMs2; }
    float FrenoToma() { return m_FrenoTomaMs2; }
    // la frenada a fondo MEDIDA en la parada del cusp del leg en curso (m/s2); -1 sin medicion. La leen la MANIOBRA (pedales
    // y techo de aproximacion) y el freno al fin del leg; el ENDPOINT usa la de la parada final (FrenoToma).
    private float m_FrenoCuspMs2 = -1;
    void SetFrenoCusp(float aMs2) { m_FrenoCuspMs2 = aMs2; }
    float FrenoCusp() { return m_FrenoCuspMs2; }
    float MaxBrakeDecelManiobra(int surf)
    {
        float aM = MaxBrakeDecel(surf);
        if (m_FrenoCuspMs2 >= FRENO_TOMA_MIN_MS2 && m_FrenoCuspMs2 < aM) aM = m_FrenoCuspMs2;
        return aM;
    }
    bool  idleCreepIsZero;        // hay evidencia POSITIVA de que no repta
    // ¿Alguien YA decidio el objetivo de arrastre? Distinto de "esta medido": el
    // no-reptante lo tiene DERIVADO del freno, y una compuerta que pregunte solo por
    // MEASURED lo pisa con el prior de caja.
    bool  creepObjetivoDecidido;
    float clutchBiteRpm;
    float launchThrottleFloor;
    // el PISO vino de la calibracion (lcal). Distinto de IsMeasured(LAUNCH), que tambien lo pone
    // aMax: con aMax medido y el piso en fallback el reader decia "MEDIDO 0.35" (RPT 22/09 19:37:33).
    bool  launchFloorMedido;
    bool  launchFloorForzado;     // bandera de banco BZ12_FORCE_LAUNCH (contrafactual); nunca en la linea
    int   launchCalN;             // cuantos despegues de calibracion lo sostienen
    float steerKPose;
    float maxAccelMs2;            // aceleracion maxima en 1ra: F_max / masa

    // ---- MEASURED (envelope) ----
    private ref array<float> m_KMeas;
    private ref array<bool>  m_KHas;
    private ref array<float> m_CoastMeas;   // idx = surf * NBINS + bin
    private ref array<bool>  m_CoastHas;
    // Transferencia comando -> angulo ejecutado, por balde de comando. Medida.
    static const int STX_BUCKETS = 10;
    private ref array<float> m_StxAng;
    private ref array<bool>  m_StxHas;
    private bool m_AvisoStxSucia;
    private bool m_AvisoPlantReplay;   // aviso unico: replay sin transferencia medida → planta 85.1
    static const float STX_MONO_EPS = 0.004;   // rad: ruido de lectura, no una bajada real
    static const int   STX_MIN_BALDES = 4;

    // ---- priors de lo medible (se usan SOLO si no hay medicion) ----
    private float m_KPrior;
    private float m_CoastRollHard;
    private float m_CoastRollSoft;

    private ref array<int> m_Prov;
    // Ratio TOTAL por marcha MEDIDO (rpm del motor / omega de rueda). Cuando existe,
    // DESPLAZA al del config: es el ratio real de esta caja, no el que el config declara
    // (o no declara -- en el Sedan_02 el config de la caja directamente no resolvio).
    private ref array<float> m_RatioMeas;

    // Prior neutro del modelo bicicleta (sin understeer). A proposito NO es 0.90:
    // ese era el valor INVENTADO de v1.1 que el envelope midio en ~0.50. Un prior
    // honesto es el neutro; la medicion pone el numero real.
    private const float K_PRIOR_NEUTRAL = 1.0;
    // Rodadura de manual (asfalto). El piso blando se toma ~3x -- es un PRIOR grueso
    // a proposito: CoastDecel real SOLO se sabe midiendo (regla 2).
    private const float ROLL_HARD = 0.015;
    private const float ROLL_SOFT_FACTOR = 3.0;
    private const float MU_HARD = 1.0;
    private const float MU_SOFT = 0.6;
    private const float G = 9.81;

    void BZVehicleManual()
    {
        gearRatios  = new array<float>();
        torqueCurve = new array<float>();
        steerDecCurve = new array<float>();
        steerIncCurve = new array<float>();
        m_KMeas     = new array<float>();
        m_KHas      = new array<bool>();
        m_CoastMeas = new array<float>();
        m_CoastHas  = new array<bool>();
        m_StxAng    = new array<float>();
        m_StxHas    = new array<bool>();
        m_Prov      = new array<int>();
        m_RatioMeas = new array<float>();
        int i;
        for (i = 0; i < NBINS; i++)
        {
            m_KMeas.Insert(0.0);
            m_KHas.Insert(false);
        }
        for (i = 0; i < NBINS * 2; i++)
        {
            m_CoastMeas.Insert(0.0);
            m_CoastHas.Insert(false);
        }
        for (i = 0; i < BZAsp.COUNT; i++)
        {
            m_Prov.Insert(BZProv.BZPV_NONE);
        }
        for (i = 0; i < 8; i++)
        {
            m_RatioMeas.Insert(0.0);
        }
        m_KPrior = K_PRIOR_NEUTRAL;
        m_CoastRollHard = ROLL_HARD;
        m_CoastRollSoft = ROLL_HARD * ROLL_SOFT_FACTOR;
    }

    // ======================= procedencia =======================

    void SetProv(int aspect, int prov)
    {
        if (aspect < 0) return;
        if (aspect >= BZAsp.COUNT) return;
        m_Prov.Set(aspect, prov);
    }

    int  ProvOf(int aspect)
    {
        if (aspect < 0) return BZProv.BZPV_NONE;
        if (aspect >= BZAsp.COUNT) return BZProv.BZPV_NONE;
        return m_Prov.Get(aspect);
    }

    bool IsMeasured(int aspect)
    {
        return ProvOf(aspect) == BZProv.BZPV_MEASURED;
    }

    // ---- PROCEDENCIA TRANSITIVA ----
    // Un DERIVED no puede ser mejor que sus INPUTS: hereda el MINIMO. Si el radio de rueda
    // esta en FALLBACK, todo lo que lo use (creep, topSpeed, launch, freno) sale FALLBACK
    // tambien. Sin esto el grito se apaga en la primera derivacion y un valor de emergencia
    // se pasea por el sistema disfrazado de dato calculado.
    // Pasar -1 en los aspectos que no apliquen.
    void SetDerivedFrom(int aspect, int in1, int in2, int in3)
    {
        int q = BZProv.BZPV_DERIVED;   // techo: una formula nunca es mejor que DERIVED
        q = MinProv(q, in1);
        q = MinProv(q, in2);
        q = MinProv(q, in3);
        SetProv(aspect, q);
    }

    private int MinProv(int q, int inAspect)
    {
        if (inAspect < 0) return q;
        int p = ProvOf(inAspect);
        if (p < q) return p;
        return q;
    }

    bool HasFallback()
    {
        int i;
        for (i = 0; i < BZAsp.COUNT; i++)
        {
            if (m_Prov.Get(i) == BZProv.BZPV_FALLBACK) return true;
        }
        return false;
    }

    // La interfaz del MODDER, no un debug: portar un vehiculo es mirar esto y ver de un
    // vistazo que se leyo, que se derivo, que se midio y que quedo en emergencia.
    void LogProvenance()
    {
        Print("[Manual] === " + vehicleClass + " ===");
        Print("[Manual] masa=" + massKg.ToString() + "kg  wb=" + wheelbaseM.ToString() + "m  rWheel=" + wheelRadiusM.ToString() + "m  drive=" + driveKind.ToString());
        Print("[Manual] rpm idle=" + rpmIdle.ToString() + " red=" + rpmRedline.ToString() + "  marchas=" + gearRatios.Count().ToString() + "  final=" + finalDrive.ToString());
        Print("[Manual] topSpeed=" + topSpeedKmh.ToString() + "kmh  creep=" + creepKmh.ToString() + "kmh  bite=" + clutchBiteRpm.ToString() + "rpm  planeo medido en " + CoastBinsMedidos(BZSurf.HARD).ToString() + "/" + NBINS.ToString() + " bins duros, " + CoastBinsMedidos(BZSurf.SOFT).ToString() + " blandos");
        Print("[Manual] launchFloor=" + launchThrottleFloor.ToString() + "  steerKPose=" + steerKPose.ToString() + "  steerMax=" + steerMaxRad.ToString() + "rad");
        BZDerivationEngine.ReportarVentanaDeMarchas(this);
        // *** La curva de subviraje POR BIN, para auditar lo medido. Paso 3 divide la
        // correccion por estos valores: un bin medido en <=0.05 (el piso) significa
        // correccion x20 y el cap casi abierto en esa banda de velocidad. El weave del
        // GT2RS-2 a 62-103 km/h (str +-1.0 con kmax 0.0137) solo cierra si un bin de
        // 80-100 esta en el piso. Sin esta linea no se puede auditar del RPT.
        string lnku = "[Manual] understeer ku por bin (5 km/h):";
        int bku;
        for (bku = 0; bku < NBINS; bku++)
        {
            float kuv = UndersteerK(bku * BINW + BINW * 0.5);
            lnku = lnku + " " + kuv.ToString();
        }
        Print(lnku);
        LogProvLines();
        if (!IsMeasured(BZAsp.UNDERSTEER)) Print("[Manual] sin envelope: understeer k(v) usa PRIOR neutro -- grabar una demostracion");
        if (!IsMeasured(BZAsp.COAST))      Print("[Manual] sin envelope: planeo a(v,piso) usa PRIOR grueso -- grabar una demostracion");
        if (HasFallback()) Print("[Manual] *** HAY CAMPOS EN FALLBACK: revisar el config del vehiculo ***");
    }

    private void LogProvLines()
    {
        int i;
        // ⚠ EL VALOR DE LA BATALLA, no solo su procedencia. El barrido de la ventana FF
        // tiene que reportar el ganador en las DOS formas --metros absolutos y fraccion
        // de batalla-- porque de eso depende que se exponga: una constante universal o
        // un dato de envelope que escala solo al proximo vehiculo. Sin este numero en el
        // log hay que reconstruirlo a mano en cada corrida.
        string ln = "[Manual] batalla " + wheelbaseM.ToString() + " m · procedencia:";
        for (i = 0; i < BZAsp.COUNT; i++)
        {
            ln = ln + " " + BZAsp.Name(i) + "=" + BZAsp.ProvName(m_Prov.Get(i));
        }
        Print(ln);
    }

    // ======================= medido (envelope) =======================

    private int BinOf(float kmh)
    {
        int b = (int)(kmh / BINW);
        if (b < 0) b = 0;
        if (b > NBINS - 1) b = NBINS - 1;
        return b;
    }

    void SetMeasuredK(int bin, float k)
    {
        if (bin < 0) return;
        if (bin > NBINS - 1) return;
        m_KMeas.Set(bin, k);
        m_KHas.Set(bin, true);
        SetProv(BZAsp.UNDERSTEER, BZProv.BZPV_MEASURED);
    }

    void SetMeasuredSteerTransfer(int bucket, float angleRad)
    {
        if (bucket < 0 || bucket > STX_BUCKETS - 1) return;
        if (m_StxAng.Count() < STX_BUCKETS)
        {
            int q;
            for (q = m_StxAng.Count(); q < STX_BUCKETS; q++)
            {
                m_StxAng.Insert(0);
                m_StxHas.Insert(false);
            }
        }
        m_StxAng.Set(bucket, angleRad);
        m_StxHas.Set(bucket, true);
        SetProv(BZAsp.STEERTRANSF, BZProv.BZPV_MEASURED);
    }

    // ★★★ QUE COMANDO HACE FALTA PARA UN ANGULO DE RUEDA DADO.
    //
    // El modelo asumia que la relacion es LINEAL: comando = delta / steerMax. Para un auto
    // chico alcanza; para un vehiculo largo puede no cumplirse, y ahi el control pide un
    // angulo y recibe otro -- que es exactamente la forma de vagar a mitad de ruta sin
    // fallar en el cierre (medido en el bus: cross-track medio 2.30 m con endpoint a 0.28).
    //
    // Con la tabla medida se INVIERTE la curva: se busca el balde cuyo angulo ejecutado
    // cubre el delta pedido y se interpola entre centros de balde. Sin tabla, se cae a la
    // relacion lineal de siempre -- que es lo que habia, no algo peor.
    // ★★★ UNA CURVA MEDIDA NO SE CONSUME POR SER MEDIDA: TIENE QUE SER SANA.
    //
    // Costo una corrida aprenderlo. La primera tabla del CivilianSedan estaba medida, tenia
    // 9 baldes y miles de muestras -- y era basura: decia que a comando 0.15 la rueda
    // ejecutaba 2.15x lo lineal, y BAJABA del balde 4 al 5. Invertirla hizo que el control
    // pidiera menos de la mitad de comando, y el cross-track del auto paso de 0.49 a 2.21.
    //
    // Una transferencia de direccion real es MONOTONA: mas comando no puede dar menos
    // angulo. Si la medida no lo es, no es una transferencia -- es ruido con muestras. Se
    // avisa una vez y se usa la relacion lineal, que es lo que habia.
    private bool CurvaDeDireccionSana()
    {
        if (!IsMeasured(BZAsp.STEERTRANSF)) return false;
        if (m_StxHas.Count() < STX_BUCKETS) return false;
        // ★★★ Y SIN HUECOS: no se interpola sobre un tramo donde no hay dato.
        //
        // Medido: la tabla limpia del CivilianSedan salio monotona y con 4 baldes -- pasaba
        // la validacion -- pero eran los baldes 0, 1, 2 y 4, con el 3 vacio y todo el tramo
        // alto sin datos (0:7877 muestras, 1:115, 2:58, 4:30). Invertir eso es inventar la
        // curva en el hueco, y el cross-track del auto quedo en 1.25 contra 0.978 del modelo
        // lineal: mejor que la tabla contaminada (2.21), peor que no usar tabla.
        //
        // La regla no necesita un numero de densidad elegido: si entre dos baldes con dato
        // hay uno vacio, la interpolacion cruza una zona sin medir. Se exige que los baldes
        // poblados sean CONTIGUOS desde el 0.
        int primerHueco = -1;
        int bb;
        bool vistoDato = false;
        for (bb = 0; bb < STX_BUCKETS; bb++)
        {
            if (m_StxHas.Get(bb))
            {
                if (primerHueco >= 0)
                {
                    if (!m_AvisoStxSucia)
                    {
                        m_AvisoStxSucia = true;
                        Print("[Manual] transferencia de direccion CON HUECOS (balde " + primerHueco.ToString() + " sin dato y el " + bb.ToString() + " con dato): interpolar ahi seria inventar. Se usa la relacion lineal.");
                    }
                    return false;
                }
                vistoDato = true;
            }
            else if (vistoDato && primerHueco < 0)
            {
                primerHueco = bb;
            }
        }
        int n = 0;
        float prev = -1.0;
        int b;
        for (b = 0; b < STX_BUCKETS; b++)
        {
            if (!m_StxHas.Get(b)) continue;
            float a = m_StxAng.Get(b);
            if (a < prev - STX_MONO_EPS)
            {
                if (!m_AvisoStxSucia)
                {
                    m_AvisoStxSucia = true;
                    Print("[Manual] transferencia de direccion NO MONOTONA (balde " + b.ToString() + " baja de " + prev.ToString() + " a " + a.ToString() + "): se ignora y se usa la relacion lineal.");
                }
                return false;
            }
            prev = a;
            n++;
        }
        if (n < STX_MIN_BALDES)
        {
            if (!m_AvisoStxSucia)
            {
                m_AvisoStxSucia = true;
                Print("[Manual] transferencia de direccion con solo " + n.ToString() + " baldes (hacen falta " + STX_MIN_BALDES.ToString() + "): se usa la relacion lineal.");
            }
            return false;
        }
        return true;
    }

    float CommandForAngle(float deltaRad)
    {
        float signo = 1.0;
        float d = deltaRad;
        if (d < 0) { d = -d; signo = -1.0; }
        if (!CurvaDeDireccionSana())
        {
            if (steerMaxRad < 0.05) return 0;
            return signo * Math.Clamp(d / steerMaxRad, 0.0, 1.0);
        }
        float cmdPrev = 0;
        float angPrev = 0;
        int b;
        for (b = 0; b < STX_BUCKETS; b++)
        {
            if (!m_StxHas.Get(b)) continue;
            float cmdB = (b + 0.5) / STX_BUCKETS;
            float angB = m_StxAng.Get(b);
            if (angB >= d)
            {
                if (angB - angPrev < 0.0001) return signo * cmdB;
                float t = (d - angPrev) / (angB - angPrev);
                return signo * Math.Clamp(cmdPrev + t * (cmdB - cmdPrev), 0.0, 1.0);
            }
            cmdPrev = cmdB;
            angPrev = angB;
        }
        // Pide mas angulo del que se vio ejecutar: se manda todo lo que hay.
        return signo * 1.0;
    }

    // ★★★ LA CONVERSION DEG→CMD DEL REPLAY, CON LA PLANTA COMO FALLBACK.
    //
    // El input de steering NO mapea 1.0 → steerMax(30°): la PLANTA medida en julio
    // (steering_plant) es ~85° de rueda por unidad de comando en el tramo util, y v1.1
    // replayo la rueda grabada con exactamente eso: cmd = targetFrontWheel / 85.1
    // (default de GetPlantSteerGain). El run 15-33 lo probo por el otro lado: el
    // ChernoBus tiene steerTransfer=NONE → CommandForAngle cayo al fallback lineal por
    // steerMax y la cinta aplico cStr = cFw/30 EXACTO (medido en el CN: 0.390/11.7 =
    // 0.158/4.7 = 0.592/17.8 = 1/30) → la rueda ejecuto ~x2.8 lo que el usuario grabo.
    //
    // ⚠ ESTO NO TOCA LA LEY CERRADA A PROPOSITO: el /steerMaxRad de SteerCommandSplit
    // es una perilla CALIBRADA del lazo (corregirla de 75 a 30.5 ya hizo weaving una
    // vez) y el lazo absorbe la planta con ku/correctores. El replay ABIERTO no tiene
    // lazo que absorba nada: necesita la planta exacta. Por eso el fallback del replay
    // es la planta y el de la ley sigue siendo steerMax.
    //
    // Falsador: si con esto la rueda ejecuta ~0.35x lo grabado (subvira el replay), la
    // planta del bus NO es 85.1 y hay que medirla por vehiculo (config PlantSteerGain
    // como v1.1).
    static const float PLANT_STEER_GAIN_DEG = 85.1;

    float CommandForAngleReplay(float deltaRad)
    {
        if (CurvaDeDireccionSana()) return CommandForAngle(deltaRad);
        if (!m_AvisoPlantReplay)
        {
            m_AvisoPlantReplay = true;
            Print("[Manual] cinta: sin transferencia medida -> planta default " + PLANT_STEER_GAIN_DEG.ToString() + " deg/unidad (precedente v1.1 PlantUseRecordedWheel), NO steerMax.");
        }
        return Math.Clamp((deltaRad * Math.RAD2DEG) / PLANT_STEER_GAIN_DEG, -1.0, 1.0);
    }

    void SetMeasuredCoast(int surf, int bin, float a)
    {
        if (bin < 0) return;
        if (bin > NBINS - 1) return;
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        int idx = s * NBINS + bin;
        m_CoastMeas.Set(idx, a);
        m_CoastHas.Set(idx, true);
        SetProv(BZAsp.COAST, BZProv.BZPV_MEASURED);
    }

    // MEDIDO si ese bin tiene muestra; si no, el PRIOR. Sin blend: la medicion no se
    // promedia con el modelo (regla 1).
    float UndersteerK(float kmh)
    {
        int b = BinOf(kmh);
        if (m_KHas.Get(b)) return m_KMeas.Get(b);
        return m_KPrior;
    }

    float CoastDecel(float kmh, int surf)
    {
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        int b = BinOf(kmh);
        int idx = s * NBINS + b;
        if (m_CoastHas.Get(idx)) return m_CoastMeas.Get(idx);
        // ★ SIN DATO EN EL BIN, EL VECINO MEDIDO GANA AL PRIOR ANALITICO.
        //
        // La calibracion llena los bins que la ruta recorrio, no los 20: medido, 4 de 40.
        // Y el prior analitico (rodadura + aero) se quedo 4x corto contra la realidad, asi
        // que caer en el es caer en el mismo error que acabamos de arreglar. Un bin vecino
        // MEDIDO de este vehiculo es mejor estimador del planeo a esta velocidad que una
        // formula que ya sabemos que subestima.
        //
        // Se busca el medido mas cercano en la MISMA superficie; solo si no hay ninguno se
        // usa el prior. La distancia queda en el log de procedencia, que para eso esta.
        int d;
        for (d = 1; d < NBINS; d++)
        {
            if (b - d >= 0 && m_CoastHas.Get(s * NBINS + b - d)) return m_CoastMeas.Get(s * NBINS + b - d);
            if (b + d < NBINS && m_CoastHas.Get(s * NBINS + b + d)) return m_CoastMeas.Get(s * NBINS + b + d);
        }
        return CoastPrior(kmh, s);
    }

    // Cuantos bins de planeo tienen medicion, por superficie. Para que la cobertura sea
    // visible en el reporte en vez de haber que deducirla del CSV.
    int CoastBinsMedidos(int surf)
    {
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        int n = 0;
        int i;
        for (i = 0; i < NBINS; i++)
        {
            if (m_CoastHas.Get(s * NBINS + i)) n++;
        }
        return n;
    }

    // PRIOR derivado del config: rodadura + aerodinamica. Es grueso a proposito -- el
    // planeo real resume masa + freno motor + rodadura + aero + PISO, y eso solo se mide.
    private float CoastPrior(float kmh, int s)
    {
        float roll = m_CoastRollHard;
        if (s == 1) roll = m_CoastRollSoft;
        float v = kmh / 3.6;
        float a = G * roll;
        if (massKg > 1.0) a = a + (0.5 * 1.225 * dragCoef * frontalArea * v * v) / massKg;
        return a;
    }

    // Freno fisico maximo: par de frenos sobre radio y masa, techado por el agarre del piso.
    // ★ EL FRENO CON EL QUE EL ENDPOINT PLANEA Y DOSIFICA: el fisico del config, TECHADO por lo que la toma midio con el
    // pedal a fondo. Medido (Hatchback, llegada a 20 km/h): el manual deriva 8,5 m/s2 del par de frenos y la fisica
    // entrega 5,2-5,6; con 8,5 la zona por velocidad no se armaba y el pedal salia a 0,7 donde hacia falta 1,0 -> el
    // auto se pasaba 0,4-0,5 m del punto. Con la toma como techo la zona se arma antes y el pedal satura donde debe.
    // Las maniobras y el pursuit siguen con el fisico: un cambio por vez.
    float MaxBrakeDecelEndpoint(int surf)
    {
        float a = MaxBrakeDecel(surf);
        if (m_FrenoTomaMs2 >= FRENO_TOMA_MIN_MS2 && m_FrenoTomaMs2 < a) a = m_FrenoTomaMs2;
        return a;
    }

    float MaxBrakeDecel(int surf)
    {
        if (wheelRadiusM < 0.05) return 0;
        if (massKg < 1.0) return 0;
        float f = (maxBrakeTorqueFront + maxBrakeTorqueRear) / wheelRadiusM;
        float a = f / massKg;
        float mu = MU_HARD;
        if (surf == BZSurf.SOFT) mu = MU_SOFT;
        float cap = mu * G;
        if (a > cap) a = cap;
        return a;
    }

    // Velocidad maxima (a redline) de la marcha con INDICE DE RATIO gi (0 = 1ra fisica).
    float TopSpeedForGear(int gi)
    {
        if (gi < 0) return 0;
        if (gi >= gearRatios.Count()) return 0;
        return SpeedAtRpm(rpmRedline, gearRatios.Get(gi));
    }

    // Torque del motor a esas vueltas, interpolado en la curva del config. La curva viene
    // como pares (rpm, Nm) interleaved. Fuera de rango se toma el extremo -- extrapolar una
    // curva de torque inventa potencia que el motor no tiene.
    // ⚠⚠ EL RPM DE LA BANDA DE POTENCIA, DERIVADO -- no una fraccion del corte elegida a ojo.
    //
    // ✖ POTENCIA, no par: la fuerza en la rueda es P/v, asi que lo que maximiza la
    // aceleracion a una velocidad dada es par x rpm, no el par. El pico de potencia cae
    // bastante mas arriba que el de par, y elegir mal ahi cuesta justo lo que estamos
    // tratando de recuperar.
    //
    // Se cachea: la curva no cambia en toda la corrida.
    // Velocidad maxima del volante a esta velocidad, en RADIANES/s. La curva viene en
    // pares (kmh, deg/s) y se interpola lineal; fuera de rango se toma el extremo.
    // Devuelve 0 si no hay curva: el que pregunta decide, aca no se inventa un prior.
    private float RitmoDeCurva(array<float> c, float kmh)
    {
        if (!c) return 0;
        int n = c.Count();
        if (n < 2) return 0;
        if (kmh <= c.Get(0)) return c.Get(1) * Math.DEG2RAD;
        int i = 0;
        while (i + 3 < n)
        {
            float k0 = c.Get(i);
            float k1 = c.Get(i + 2);
            if (kmh <= k1)
            {
                float span = k1 - k0;
                if (span < 0.0001) return c.Get(i + 1) * Math.DEG2RAD;
                float t = (kmh - k0) / span;
                float d0 = c.Get(i + 1);
                float d1 = c.Get(i + 3);
                return (d0 + (d1 - d0) * t) * Math.DEG2RAD;
            }
            i = i + 2;
        }
        return c.Get(n - 1) * Math.DEG2RAD;
    }

    float RitmoContravolante(float kmh)
    {
        return RitmoDeCurva(steerDecCurve, kmh);
    }

    float RitmoVolante(float kmh)
    {
        return RitmoDeCurva(steerIncCurve, kmh);
    }

    float RpmDeBanda()
    {
        if (m_RpmBanda > 1.0) return m_RpmBanda;
        if (torqueCurve.Count() < 4) return 0;
        if (rpmRedline < 100.0) return 0;
        float hi = rpmRedline * 0.90;
        float r = rpmIdle;
        float mejorR = rpmIdle;
        float mejorP = -1.0;
        float t;
        float p;
        while (r <= hi)
        {
            t = TorqueAt(r);
            p = t * r;
            if (p > mejorP)
            {
                mejorP = p;
                mejorR = r;
            }
            r = r + 25.0;
        }
        m_RpmBanda = mejorR;
        return m_RpmBanda;
    }

    float TorqueAt(float rpm)
    {
        int n = torqueCurve.Count();
        if (n < 4) return 0;
        if (rpm <= torqueCurve.Get(0)) return torqueCurve.Get(1);
        int i;
        float r0;
        float t0;
        float r1;
        float t1;
        for (i = 2; i < n; i = i + 2)
        {
            r1 = torqueCurve.Get(i);
            t1 = torqueCurve.Get(i + 1);
            if (rpm <= r1)
            {
                r0 = torqueCurve.Get(i - 2);
                t0 = torqueCurve.Get(i - 1);
                if (r1 - r0 < 1.0) return t1;
                return t0 + (t1 - t0) * (rpm - r0) / (r1 - r0);
            }
        }
        return torqueCurve.Get(n - 1);
    }

    // Ratio TOTAL (marcha x final x diferencial central) para una marcha DayZ.
    // gear 0 = reversa y usa su propio ratio; 2+ = marchas hacia adelante.
    // Se devuelve en MAGNITUD: el sentido lo pone el signo de la marcha, no el ratio.
    void SetMeasuredRatio(int gearDayZ, float ratioTotal)
    {
        if (gearDayZ < 0 || gearDayZ >= m_RatioMeas.Count()) return;
        if (ratioTotal <= 0.0001) return;
        m_RatioMeas.Set(gearDayZ, ratioTotal);
    }

    // Velocidad a esas vueltas con un ratio TOTAL ya resuelto (medido o del config).
    float SpeedAtRatio(float rpm, float ratioTotal)
    {
        if (ratioTotal <= 0.0001) return 0;
        if (wheelRadiusM <= 0) return 0;
        return rpm * wheelRadiusM * 6.28318530718 / (60.0 * ratioTotal) * 3.6;
    }

    // ⚠ CORTADA EN DOS A PROPOSITO. `RatioForGear` se comporta igual que siempre --lo medido
    // desplaza al config-- pero las dos mitades ahora se pueden mirar por separado, y hace
    // falta: la sospecha es que la tabla MEDIDA por marcha se llena durante el patinaje, y
    // contra una referencia contaminada la compuerta de coherencia no puede rechazar nada
    // (rel sale ~1 comparando dos numeros con el MISMO error). Ver DiagBite.
    float RatioMedidoForGear(int gearDayZ)
    {
        if (gearDayZ < 0 || gearDayZ >= m_RatioMeas.Count()) return 0;
        float med = m_RatioMeas.Get(gearDayZ);
        if (med <= 0.0001) return 0;
        return med;
    }

    float RatioConfigForGear(int gearDayZ)
    {
        float g = 0;
        if (gearDayZ == 0) g = reverseRatio;
        else if (gearDayZ >= 2 && gearDayZ - 2 < gearRatios.Count()) g = gearRatios.Get(gearDayZ - 2);
        if (g < 0) g = -g;
        if (g <= 0.0001) return 0;
        return g * finalDrive * centDiffRatio;
    }

    float RatioForGear(int gearDayZ)
    {
        float med = RatioMedidoForGear(gearDayZ);
        if (med > 0.0001) return med;          // lo MEDIDO manda
        return RatioConfigForGear(gearDayZ);
    }

    // ⚠⚠ LOS CUATRO NUMEROS QUE DECIDEN LA MORDIDA, EN UNA LINEA.
    //
    // La mordida salio 10.1, despues 13.87, despues 16.03, y cada vez hubo que deducir de
    // donde venia invirtiendo la formula desde el log del otro hilo. Eso ya costo dos relays
    // de ida y vuelta. Con `ratio`, `nom`, `rel` y la RAMA impresas, la proxima vez se lee.
    //
    // ★ Y sobre todo: `nom` se abre en MEDIDO y CONFIG. Si son muy distintos, la referencia
    // contra la que se mide la coherencia es ella misma sospechosa, y eso NO se puede ver
    // mirando solo el resultado.
    string DiagBite(float chassisKmh, float rpmReal, int gearDayZ)
    {
        float nom = RatioForGear(gearDayZ);
        float ratio = RatioEngagedNow(chassisKmh, rpmReal);
        string s = "gear " + gearDayZ.ToString();
        s = s + " · ratio " + ratio.ToString();
        s = s + " · nom " + nom.ToString();
        s = s + " (medido " + RatioMedidoForGear(gearDayZ).ToString();
        s = s + " / config " + RatioConfigForGear(gearDayZ).ToString() + ")";
        if (ratio <= 0.0001)
        {
            float v = chassisKmh / 3.6;
            if (v < 0) v = -v;
            if (v >= RATIO_INST_MIN_MS) return s + " · rama SIN-MEDICION-A-VELOCIDAD (mordida 0)";
            return s + " · rama RESPALDO-BAJA-VELOCIDAD (usa nom)";
        }
        if (nom <= 0.0001) return s + " · rama SIN-NOMINAL (usa ratio)";
        float rel = ratio / nom;
        s = s + " · rel " + rel.ToString();
        if (rel > COHER_MIN && rel < COHER_MAX) return s + " · rama MEDIDA-ACEPTADA";
        return s + " · rama RECHAZADA-USA-NOM";
    }

    // ========================================================================
    //  ACELERACION PEDIDA -> THROTTLE, por CURVA DE TORQUE.
    //
    //  ⚠ VIVE ACA, NO EN UN MODULO DE MANEJO. Es fisica del VEHICULO -- masa, radio,
    //  ratio, curva de torque -- y el Manual es dueño de las cuatro. Estaba adentro del
    //  DerivationEngine, con lo cual los modulos POSE no podian usarla y terminaban
    //  INVENTANDO una constante: el MC pedia launchThrottleFloor fijo en TRACK, que en
    //  llano alcanza y EN CUESTA NO SOSTIENE NI EL PESO. Una sola verdad, dos clientes.
    //
    //  La exencion de los modulos POSE (SEAM 4) es LATERAL: cerca del tope de direccion
    //  y a 0-2 km/h el modelo de bicicleta esta mal condicionado. La pregunta "que gas da
    //  esta aceleracion" NO esta mal condicionada a baja velocidad -- es la misma cuenta.
    //
    //  aNeed es la aceleracion que hay que AGREGAR a lo que el vehiculo hace solo; el que
    //  llama descuenta antes el planeo y la gravedad (ver AccelNeededFor).
    // ========================================================================
    float ThrottleForAccel(float aNeed, float chassisKmh, float rpmReal, int gearDayZ)
    {
        if (aNeed <= 0) return 0;
        float thr = TorqueModelThrottle(aNeed, chassisKmh, rpmReal, gearDayZ);
        if (thr < 0) thr = DegradedThrottle(aNeed);
        // El piso de despegue manda cuando hay que romper la inercia estatica.
        if (thr > 0.01 && thr < launchThrottleFloor && chassisKmh < creepKmh) thr = launchThrottleFloor;
        return Math.Clamp(thr, 0.0, 1.0);
    }

    // Devuelve NEGATIVO si el modelo NO SE PUEDE RESOLVER con los datos que hay. Eso es
    // distinto de "hace falta cero gas", y por eso no puede devolver 0: quien llama tiene
    // que poder distinguir "no hace falta" de "no se". El 0 silencioso ya nos costo caro.
    private float TorqueModelThrottle(float aNeed, float chassisKmh, float rpmReal, int gearDayZ)
    {
        if (wheelRadiusM <= 0.05) return -1.0;
        if (massKg <= 1.0) return -1.0;
        if (torqueCurve.Count() < 4) return -1.0;
        // ★★★ LO MEDIDO GANA A LO DECLARADO -- Y ACA LA REGLA ESTABA INVERTIDA.
        //
        // Este modelo se probaba SIEMPRE primero, y solo caia al de aMax medido si no
        // resolvia. Pero su insumo central es la RELACION DE TRANSMISION, y en el Sedan_02
        // la caja esta en FALLBACK: el config no la da y la medicion se descarto sola por
        // contradictoria (3.93 a 54.75). Sin relacion creible, el gas sale de una cuenta
        // sobre datos que el propio framework declara no confiables -- y sale corto:
        // pidio 0.233 para sostener 12 km/h en reversa y el vehiculo se murio igual,
        // cuando el piso de despegue MEDIDO de ese mismo vehiculo es 0.69.
        //
        // Si la caja no es confiable pero aMax SI esta medido (6008 muestras, p50 1.625),
        // el modelo grosero sobre una magnitud medida es MEJOR estimador que el modelo fino
        // sobre una inventada. La procedencia ya sabe cual es cual: se la consulta.
        // (aMax medido comparte el aspecto LAUNCH: el reader lo marca ahi al publicarlo.)
        if (ProvOf(BZAsp.GEARING) == BZProv.BZPV_FALLBACK && IsMeasured(BZAsp.LAUNCH) && maxAccelMs2 > 0.1) return -1.0;
        float ratio = RatioEngagedNow(chassisKmh, rpmReal);
        if (ratio <= 0.0001) ratio = RatioForGear(gearDayZ);
        if (ratio <= 0.0001) return -1.0;
        int gi = gearDayZ - 2;
        if (gi < 0) gi = 0;
        if (gi >= gearRatios.Count()) gi = gearRatios.Count() - 1;
        float rpm = rpmReal;
        if (rpm <= rpmIdle * 0.5 && gearRatios.Count() > 0) rpm = RpmAtSpeed(chassisKmh, gi);
        if (rpm < rpmIdle) rpm = rpmIdle;
        float tMax = TorqueAt(rpm);
        if (tMax <= 1.0) return -1.0;
        float tWheel = (massKg * aNeed) * wheelRadiusM;
        return (tWheel / (ratio * DRIVETRAIN_EFF)) / tMax;
    }

    // ★ EL RATIO QUE IMPORTA ES EL QUE ESTA ENGRANADO AHORA, Y NO HACE FALTA MEDIRLO.
    //
    // Veniamos peleando por estimar "el ratio de la marcha N" desde la demostracion, y no
    // convergia: 27.88 / 14.42 / 25.99, y despues un rango de 3.94 a 54.75 DENTRO de la
    // misma marcha. Pero para convertir par de rueda en par de motor NO hace falta saber
    // que marcha es: hace falta la relacion entre las vueltas del motor y las de la rueda
    // EN ESTE INSTANTE, y las dos las da el estado vivo.
    //
    //     ratio = w_motor / w_rueda = (rpm*2pi/60) / (v/r)
    //
    // Es una LECTURA DEL PRESENTE, no una estimacion: no necesita converger, ni
    // persistirse, ni confiar en el indice de marcha que reporta el motor -- que es
    // justamente el dato del que ya no nos podemos fiar. Incluye ademas el patinaje de
    // embrague real, que un ratio de catalogo no tiene.
    //
    // Solo vale con la rueda girando: cerca de cero, v/r tiende a 0 y el cociente explota
    // (es exactamente el 54.75 que aparecio en el reporte). Abajo del umbral se devuelve
    // 0 = SIN DATO y manda el ratio por marcha, que ahi si es lo mejor que hay.
    // ★★★ LA VELOCIDAD A LA QUE EL MOTOR LLEGA A LA MORDIDA, CON LA MARCHA PUESTA AHORA.
    //
    // El umbral de peligro NO es el arrastre: es una propiedad de la CAJA y del embrague.
    // Medido en la reversa del Sedan_02, con la relacion instantanea que el propio vehiculo
    // reporta (14.70 a 9.42 km/h y 14.05 a 11.04 -- consistente, o sea acoplado):
    //   con ratio ~14.4 el motor toca la mordida (1170 rpm) a 10.20 km/h.
    // El umbral que se venia usando era creep*1.20 = 5.51 km/h, derivado del arrastre, que
    // no tiene NADA que ver con el embrague. O sea que toda la defensa se estaba montando
    // 4.7 km/h por debajo de la linea real: el hold disparaba a 9.42 -- ya del lado malo --
    // y nunca tuvo oportunidad.
    //
    // Devuelve 0 si la relacion no resuelve; el que llama decide con que se queda.
    float SpeedAtBiteKmh(float chassisKmh, float rpmReal, int gearDayZ)
    {
        if (clutchBiteRpm < 100.0) return 0;
        // ⚠⚠⚠ LA RELACION MEDIDA VALE SOLO SI EL EMBRAGUE ESTA AGARRADO.
        //
        // `RatioEngagedNow` es rpm_motor / velocidad_angular_de_rueda. Eso es la relacion de
        // la caja UNICAMENTE con la linea acoplada. Cuando el embrague patina, el motor y las
        // ruedas estan desacoplados y el cociente no mide una caja: mide el desacople.
        //
        // ⚠ Y JUSTO EN EL REGIMEN DONDE LA MORDIDA IMPORTA, EL EMBRAGUE PATINA POR
        // DEFINICION: la mordida ES el punto de agarre. Preguntar por la relacion engranada
        // ahi es preguntar en el unico lugar donde la medicion no significa nada.
        //
        // Medido:
        //     a 13.06 km/h la reversa pediria 2041 rpm -- el motor marcaba 1530.
        //     ratio 13.25 contra la nominal 17.67  =>  rel 0.75.
        //     mordida 13.87 en vez de 10.40, y el objetivo la seguia hasta 16 km/h.
        // Las ruedas giraban 25% mas rapido que lo que el motor las movia.
        //
        // ★ Y EN ESCRIBI LO CONTRARIO: "el camino instantaneo no se toca, ahi se
        // MIDE la relacion realmente engranada, patinaje incluido, y eso es mejor que la
        // nominal". Es cierto con la linea acoplada y falso con el embrague patinando, que
        // es donde se estaba usando.
        //
        // ★ RECHAZAR ES BARATO, Y ESO ES LO QUE PERMITE UNA BANDA ANGOSTA: lo que queda no es
        // un invento, es `RatioForGear`, que el reader MIDE por marcha en calibracion
        // (gearing=MEASURED). Un rechazo falso cuesta perder un refinamiento del 15%; un
        // aceptado falso costo tres relays. La banda se elige asimetrica hacia la seguridad.
        float nomRatio = RatioForGear(gearDayZ);
        float ratio = RatioEngagedNow(chassisKmh, rpmReal);
        if (ratio > 0.0001)
        {
            // ⚠⚠ SIN REFERENCIA NO HAY COHERENCIA QUE COMPROBAR.
            //
            // Antes esta rama devolvia la medicion cruda "porque es lo unico que hay". Pero
            // lo unico que hay es justo lo que no se puede validar: la instantanea patina, y
            // sin nominal no existe la comparacion que lo detecta. Medido en el tick del
            // daba 14.28 km/h -- ni la reversa (10.4) ni la primera (15.8): el
            // desacople. Una compuerta de coherencia sin referencia no es una compuerta.
            //
            // ✖ Se devuelve 0 = "no se sabe". Los modulos que preguntan ya saben tratarlo:
            // sin mordida no levantan el objetivo por encima de lo grabado, y el vehiculo se
            // apoya en su piso MECANICO --que es lo que hacia en la corrida buena, mediana
            // 10.0-- en vez de perseguir un numero inventado hasta 17.
            if (nomRatio <= 0.0001) return 0;
            float rel = ratio / nomRatio;
            if (rel > COHER_MIN && rel < COHER_MAX) return SpeedAtRatio(clutchBiteRpm, ratio);
            return SpeedAtRatio(clutchBiteRpm, nomRatio);   // patina: manda la caja
        }
        // ⚠ NO PODER MEDIR LA RELACION NO ES "NO HAY MORDIDA".
        //
        // RatioEngagedNow hace bien en negarse por debajo de RATIO_INST_MIN_MS: la relacion
        // instantanea es rpm / velocidad-de-rueda, y con la rueda casi quieta el cociente no
        // significa nada. Pero devolver 0 aca hace que el que pregunta lea "este vehiculo no
        // tiene mordida", y la mordida esta perfectamente definida por el config.
        //
        // Medido: a 0.4 km/h devolvia 0 y bloqueaba el slip en TODO stall de velocidad casi
        // nula -- justo el regimen para el que el slip existe (parking, reversa-desde-parado).
        //
        // ✖ EL RESPALDO SE ACOTA AL CASO ROTO. Solo cuando el vehiculo esta por debajo del
        // umbral de medicion se supone la 1ra: a esa velocidad, un vehiculo que intenta
        // moverse esta en la marcha mas corta. Si la relacion no se puede medir por OTRO
        // motivo (motor apagado a 40 km/h), se sigue devolviendo 0 en vez de inventar.
        // ⚠⚠⚠ ACA ESTABA `RatioForGear(2)` -- PRIMERA, FIJA. Y decia, textual:
        //   "(La reversa tiene una relacion casi igual a la 1ra --3.51 contra 3.50 en el
        //    Sedan-- asi que la suposicion vale para los dos sentidos.)"
        //
        // Era cierto EN EL SEDAN. En el Hatchback_02 la caja da reversa 4.819 contra 1ra
        // 3.170: 52% de diferencia. Con bite=1625rpm y rueda 0.3 m eso son DOS MORDIDAS
        // DISTINTAS -- 10.44 km/h la de reversa y 15.87 la de primera -- y en reversa se
        // devolvia la de primera.
        //
        // ⚠ Y NO QUEDABA EN UN NUMERO FEO EN EL LOG: `PisoDeMordida` usa la mordida como
        // OBJETIVO DE VELOCIDAD en la rampa final (`return vMord`). Una mordida 52% alta se
        // convierte en un objetivo 52% alto, y el vehiculo ACELERA para alcanzarlo. Es el
        // 16 km/h de reversa que el otro hilo persiguio tres relays: no lo pedia la ruta
        // --que guarda 5.7-- lo pedia esta suposicion.
        //
        // ★ Es el mismo error que la tesis de las dos capas existe para evitar: una
        // coincidencia de UN vehiculo ascendida a regla. El comentario hasta la declaraba,
        // con los dos numeros del Sedan al lado. Declararla no la volvio cierta.
        //
        // La marcha la sabemos: viene en `st.gear`, y `RatioForGear` ya resuelve la reversa
        // (gearDayZ 0) y prefiere lo MEDIDO por marcha. No hay nada que suponer.
        //
        // ✖ El camino de arriba --la relacion INSTANTANEA, por encima del umbral-- no se
        // toca: ahi se MIDE la relacion realmente engranada, patinaje incluido, y eso es
        // mejor que la nominal. La marcha entra solo donde antes se suponia.
        float v = chassisKmh / 3.6;
        if (v < 0) v = -v;
        if (v >= RATIO_INST_MIN_MS) return 0;
        if (nomRatio <= 0.0001) return 0;
        return SpeedAtRatio(clutchBiteRpm, nomRatio);
    }

    // ★★★ EL EMBRAGUE ESTA EN TRANSICION: la relacion que se ve en el eje no coincide con la
    // nominal de la marcha que se pide. Es la ventana del cambio, y es donde el motor patea
    // el rumbo.
    //
    // Medido: el upshift 2->3 patea el rumbo -6.8 -> +10.5 grados, y el
    // control CAZA sobre ese pateo hasta el fly-off. Y el pateo NO es de Boris: al humano le
    // patea hasta 21 deg/s en los mismos cambios, EN RECTA (los 5 del GT2RS son en recta),
    // y no se va. Lo que el humano hace es NO CORREGIRLO: deja pasar el golpe y el auto se
    // auto-estabiliza. Lo exclusivo de Boris era cazarlo.
    //
    // ⚠ NO LLEVA DURACION. La condicion es fisica y se apaga sola cuando el embrague agarra
    // -- la virtud que le falto al aflojo de 1.5 s, que necesito un numero inventado y por eso
    // salio 45 m de linea abandonada a 107 km/h.
    //
    // Reusa la MISMA ventana de coherencia que ya decide si la relacion instantanea es
    // creible en SpeedAtBiteKmh. No entra ninguna constante nueva.
    bool EmbragueEnTransicion(float chassisKmh, float rpmReal, int gearDayZ)
    {
        float nom = RatioForGear(gearDayZ);
        if (nom <= 0.0001) return false;          // sin referencia no se opina
        float ratio = RatioEngagedNow(chassisKmh, rpmReal);
        if (ratio <= 0.0001) return false;        // sin medida no se opina
        // ⚠⚠ EL 0.15 VA CONTRA EL PASO DE LA CAJA, NO CONTRA LA RELACION ABSOLUTA.
        //
        // La primera version comparaba `ratio/nom` contra la ventana 0.85-1.15, y eso hace
        // que la deteccion dependa del TAMAÑO DEL PASO -- que se achica con cada marcha.
        // Medido sobre las relaciones reales (las tres supercars comparten caja):
        //     2->3  paso 33.2  ->  cubria el 70% de la transicion
        //     3->4  paso 11.6  ->  cubria el 30%
        //     4->5  paso 10.5  ->  38%      5->6  7.5 -> 28%      6->7  6.3 -> 30%
        // Y eso explica EXACTAMENTE lo medido: el freeze arreglo el 2->3 (pico de hErr de
        // +10.5 a +3.7 grados) y no llego al 3->4, que es donde el fly-off pasa a nacer.
        // Era el mismo numero aplicado a la escala equivocada.
        //
        // Contra el paso, el MISMO 0.15 cubre el 85% de TODOS los cambios por igual.
        float paso = PasoDeCajaEn(gearDayZ);
        if (paso <= 0.0001)
        {
            // Sin marcha vecina (1ra o caja de una sola marcha) se cae a la ventana
            // absoluta, que es lo que habia. No se inventa un paso.
            float rel = ratio / nom;
            if (rel > COHER_MIN && rel < COHER_MAX) return false;
            return true;
        }
        float dif = ratio - nom;
        if (dif < 0) dif = -dif;
        return dif > (1.0 - COHER_MIN) * paso;
    }

    // La distancia en relacion hasta la marcha VECINA, que es el tamaño del salto que el
    // embrague tiene que recorrer en un cambio. Se prueba la de abajo y si no existe la de
    // arriba: lo que importa es la MAGNITUD del paso, no su direccion.
    private float PasoDeCajaEn(int gearDayZ)
    {
        float nom = RatioForGear(gearDayZ);
        if (nom <= 0.0001) return 0;
        float vec = RatioForGear(gearDayZ - 1);
        if (vec <= 0.0001) vec = RatioForGear(gearDayZ + 1);
        if (vec <= 0.0001) return 0;
        float paso = vec - nom;
        if (paso < 0) paso = -paso;
        return paso;
    }

    float RatioEngagedNow(float chassisKmh, float rpmReal)
    {
        if (wheelRadiusM <= 0.05) return 0;
        if (rpmReal < rpmIdle * 0.5) return 0;
        float v = chassisKmh / 3.6;
        if (v < RATIO_INST_MIN_MS) return 0;
        float wWheel = v / wheelRadiusM;
        float ratio = (rpmReal * 6.28318530718 / 60.0) / wWheel;
        if (ratio < 0.5 || ratio > 60.0) return 0;
        return ratio;
    }

    // ★ CURVATURA -> COMANDO DE VOLANTE. Una sola implementacion para los dos lados.
    //
    // Vivia solo en BZDerivationEngine.SteerFor, asi que los modulos POSE no la tenian y
    // hacian  comando = errorDeRumbo * steerKPose  -- que dimensionalmente pasa (los dos
    // son radianes) pero fisicamente NO: el angulo de rueda que corrige un error de rumbo
    // depende de la velocidad y de la distancia que queda, por el modelo de bicicleta.
    // steerMax terminaba haciendo de perilla de tuneo, y cuando se corrigio de 75 (falso)
    // a 30.5 (real) el lazo quedo 2.5x mas agresivo: weaving de 1.87 m.
    // ⚠ LA CURVATURA MAXIMA QUE ESTE VEHICULO PUEDE EJECUTAR A ESTA VELOCIDAD.
    //
    // Es la inversa exacta de SteerCommandForCurvature en delta = steerMax: pedir mas que
    // esto no dobla mas, solo manda el volante contra el fin de carrera. Sale del ENVELOPE
    // --steerMax y understeerK son MEASURED-- asi que es dato del vehiculo, no una constante.
    //
    // Medido en el ExpansionBus con el volante al tope (44 ticks, ~15 km/h): la curvatura
    // que LOGRA es 0.0894 mediana / 0.1068 el mejor tick, contra 0.1101 cinematico. Subvira
    // 19%, y por eso el factor de subviraje TIENE que estar en esta cuenta: sin el, la
    // capacidad se sobre-estima justo donde importa.
    float MaxCurvature(float kmh)
    {
        if (wheelbaseM < 0.5) return 0;
        if (steerMaxRad < 0.05) return 0;
        float k = UndersteerK(kmh);
        if (k < 0.05) k = 0.05;
        return Math.Tan(steerMaxRad) * k / wheelbaseM;
    }

    // ⚠⚠⚠ EL SUBVIRAJE COMPENSA EL FEEDFORWARD, NO LA CORRECCION.
    //
    // `SteerCommandForCurvature` divide TODA la curvatura por `UndersteerK(kmh)` (~0.26 a
    // 120 km/h, o sea x4). Eso es correcto para SEGUIR una curva --el vehiculo necesita mas
    // angulo del cinematico o subvira-- y es lo que hace trazar bien en el rango normal.
    //
    // ✖ Pero aplicarlo tambien a la CORRECCION es lo que rompe arriba. Medido:
    // v1.2 pide 2.2x lo que la ley de v1.1 (`dYaw/(pi/2)`) para el MISMO error de rumbo, y
    // **desde 2 grados**. No sobre-corrige cuando el error ya es grande: sobre-corrige desde
    // el primer grado, y ahi nace el offset que despues no se recupera. v1.1 sostiene la
    // linea a 216 km/h con el volante en 0.004 porque su ganancia no depende ni del
    // horizonte ni del subviraje.
    //
    // ★ Y v1.1 lo hace SIN el volante grabado: la ruta que corrio tenia `targetSteering=0`.
    // Lo que generaliza es la LEY, no el dato. Por eso esto no toca el conversor.
    //
    //     delta = atan(batalla * ( k_ff / Ku  +  k_corr ))
    //
    // ✖ A baja velocidad `Ku ~ 1`, asi que `k_ff/1 + k_corr = k_total` y el comando queda
    // IDENTICO al de hoy. La auto-desactivacion es por construccion, no por una compuerta
    // -- y esta vez esta verificada en el espejo, no prometida.
    float SteerCommandSplit(float kFF, float kCorr, float kmh)
    {
        if (wheelbaseM < 0.5) return 0;
        if (steerMaxRad < 0.05) return 0;
        float ku = UndersteerK(kmh);
        if (ku < 0.05) ku = 0.05;
        // *** PASO 3: el subviraje se compensa en LOS DOS terminos, no solo en el FF.
        // La version anterior dejaba kCorr sin compensar (miedo a re-amplificar, era del
        // Ld estrangulado). Medido con el lazo ya frio (RPT 17-01-44): a 196-199 km/h la
        // correccion pedia CMD -0.0027..-0.0035 y el volante aplicado quedaba en +0.006
        // -- la demanda no llegaba a la rueda (kCorr sin compensar produce ~ku=0.2 veces
        // la curvatura pedida). El lazo deriva (GT63 lat -0.5 -> -5.1), la demanda crece,
        // SATURA el cap y queda abierto. Firma identica en GT63/GT2RS/Huracan.
        // Falsador acordado: |hErr| en recta > 1.5 grados (hunting) -> revertir.
        float delta = Math.Atan(wheelbaseM * (kFF / ku + kCorr / ku));
        float cmd = delta / steerMaxRad;
        return Math.Clamp(cmd, -1.0, 1.0);
    }

    float SteerCommandForCurvature(float curvature, float kmh)
    {
        if (wheelbaseM < 0.5) return 0;
        if (steerMaxRad < 0.05) return 0;
        float k = UndersteerK(kmh);
        if (k < 0.05) k = 0.05;
        float delta = Math.Atan(wheelbaseM * curvature / k);
        // El comando sale de la transferencia MEDIDA cuando existe; si no, de la relacion
        // lineal de siempre. El que decide es el dato, no una asuncion.
        return Math.Clamp(CommandForAngle(delta), -1.0, 1.0);
    }

    // ⚠ DEGRADACION HONESTA, no un cero disfrazado de decision.
    //
    // Devolver 0 cuando el modelo no resuelve es la peor respuesta posible: significa NO
    // DAR GAS, que nunca es lo correcto cuando se pidio aceleracion positiva -- y encima
    // es indistinguible de "no hace falta acelerar". Un vehiculo cuya caja no resuelve se
    // quedaria QUIETO, y el sintoma seria identico al de un controlador que decidio parar.
    //
    // Escalon 1: si hay aMax MEDIDO, la fraccion de gas es la fraccion de la aceleracion
    //   maxima que se esta pidiendo. Es grosero (ignora la forma de la curva de par) pero
    //   es un modelo con una medicion adentro, y no depende de la caja.
    // Escalon 2: sin aMax, el piso de despegue. Mueve el vehiculo; que sea poco es
    //   preferible a que sea cero, y queda declarado.
    //
    // Y se AVISA, una vez por vehiculo: un fallback callado es el que nadie arregla.
    private float DegradedThrottle(float aNeed)
    {
        if (!m_WarnedNoTorque)
        {
            m_WarnedNoTorque = true;
            string w = "[Manual] ⚠ FALLBACK de gas en " + vehicleClass + ": la curva de par o la";
            w = w + " caja no resuelven (torqueCurve=" + torqueCurve.Count().ToString();
            w = w + " marchas=" + gearRatios.Count().ToString() + " aMax=" + maxAccelMs2.ToString() + ").";
            w = w + " Se usa el modelo grosero por aMax. NO es el modelo bueno.";
            Print(w);
        }
        if (maxAccelMs2 > 0.01) return aNeed / maxAccelMs2;
        return launchThrottleFloor;
    }

    // ¿El gas sale del modelo bueno o del degradado? Lo consulta el reporte, no el manejo.
    bool ThrottleModelIsDegraded() { return m_WarnedNoTorque; }

    // Lo que el vehiculo hace SOLO: planeo medido + gravedad. Negativo = desacelera.
    // pitch > 0 (trompa arriba) frena; en bajada la gravedad EMPUJA y esto es positivo.
    float FreeAccel(float chassisKmh, float pitchRad, int surf)
    {
        return -CoastDecel(chassisKmh, surf) - 9.81 * Math.Sin(pitchRad);
    }

    // ¿Este vehiculo puede siquiera SOSTENER esta pendiente? Con aMax MEDIDO la pregunta
    // tiene respuesta, y no es retorica: en la cuesta de SEDAN_SHORT el margen es de
    // decimas. Si da <= 0, ningun gas alcanza y el modulo tiene que DECIRLO, no seguir
    // pidiendo una velocidad que no existe.
    float ClimbMarginMs2(float pitchRad)
    {
        if (maxAccelMs2 <= 0.01) return 1.0;      // sin medicion no se opina
        return maxAccelMs2 - 9.81 * Math.Sin(pitchRad);
    }

    // Vueltas del motor a esa velocidad en esa marcha (indice de RATIO, 0 = 1ra fisica).
    // Es la inversa exacta de SpeedAtRpm: de aca sale la eleccion de marcha.
    // DESLIZAMIENTO MEDIDO: cuantas veces mas rapido gira el motor de lo que la marcha y
    // la velocidad REAL del chasis explican.
    //
    // ✖ SE LLAMO "PATINAJE" Y ESE NOMBRE ERA EL ERROR. Se leyo como rueda girando en
    // falso, y sobre esa lectura se recorto el gas. La medicion DIRECTA (velocimetro de
    // rueda contra velocidad de chasis) mostro que la rueda va pegada al chasis SIEMPRE:
    // 47.72 vs 47.73, 3.17 vs 3.21, 1.46 vs 1.47. Lo que sobra son vueltas del MOTOR, no
    // de la rueda: el embrague desliza. Queda como medicion, sin mandar sobre el gas.
    //
    // ⚠ EL PISO ES EL RALENTI, no el rpm geometrico. Por debajo de cierta velocidad el
    // motor no baja del ralenti aunque la marcha pida menos, asi que comparar contra el
    // rpm geometrico daria "patina" en todo arranque normal. Con el piso en el ralenti,
    // manejar normal da ~1.0 y solo la rueda libre se separa.
    //
    // Medido en TAXI_CHERNO: manejando 1.00 (11.99 km/h, 1412 rpm) - clavado contra el
    // piso 1.21 (0.09 km/h, 1090 rpm) - rueda libre 2.68 (3.49 km/h, 2413 rpm).
    float DeslizamientoRatio(float chassisKmh, float rpmReal, int gearDayZ)
    {
        if (gearDayZ < 2) return -1.0;                  // reversa/neutral: fuera de alcance
        int gi = gearDayZ - 2;
        if (gi >= gearRatios.Count()) return -1.0;
        if (rpmIdle <= 1.0) return -1.0;                // sin ralenti no hay contra que medir
        if (wheelRadiusM <= 0.05) return -1.0;
        if (rpmReal <= 0) return -1.0;
        float esperado = RpmAtSpeed(chassisKmh, gi);
        if (esperado < rpmIdle) esperado = rpmIdle;     // el motor no baja del ralenti
        if (esperado <= 1.0) return -1.0;
        return rpmReal / esperado;
    }

    float RpmAtSpeed(float kmh, int gearIdx)
    {
        if (gearIdx < 0) return 0;
        if (gearIdx >= gearRatios.Count()) return 0;
        if (wheelRadiusM <= 0) return 0;
        float ratio = gearRatios.Get(gearIdx) * finalDrive * centDiffRatio;
        float vms = kmh / 3.6;
        return vms * ratio * 60.0 / (6.28318530718 * wheelRadiusM);
    }

    // rpm del motor + ratio de marcha -> km/h. Es la conversion de la que sale TODO lo
    // demas (topSpeed, creep). ratioTotal = marcha * final * diferencial central.
    float SpeedAtRpm(float rpm, float gearRatio)
    {
        float ratio = gearRatio * finalDrive * centDiffRatio;
        if (ratio <= 0.0001) return 0;
        if (wheelRadiusM <= 0) return 0;
        float vms = rpm * wheelRadiusM * 6.28318530718 / (60.0 * ratio);
        return vms * 3.6;
    }
}
