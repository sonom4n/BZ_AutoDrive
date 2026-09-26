// ============================================================================
//  BZWheelRadiusEstimator.c - BZ_AutoDrive 1.2
//
//  Mide el RADIO DE RUEDA, que la API del Car no expone (no existe WheelGetRadius:
//  verificado contra el indice del server). Se mide rodando:  r = v / omega.
//
//  ⚠ SOLO EN RODADURA LIMPIA. La formula supone que la rueda gira PORQUE el piso la
//  hace girar. Si hay par aplicado, miente en las dos direcciones:
//    - WHEELSPIN (acelerando): omega alto para la misma v -> r SUBESTIMADO
//    - LOCKUP (frenando):      omega ~0 para v alta       -> r EXPLOTA
//  El gate fisico correcto es COASTEO: sin gas y sin freno, el unico par sobre la
//  rueda viene del contacto -> omega = v/r exacto.
//
//  Preferimos POCAS muestras limpias a muchas sesgadas, y la estimacion final es la
//  MEDIANA (no el promedio): resiste el outlier de slip residual que se cuele.
//
//  No conoce el Car: recibe primitivos -> se testea sin juego. Lo alimenta el lado
//  Runner; su resultado entra al Manual por BZEnvelopeSource (canal de lo MEDIDO).
// ============================================================================

class BZWheelRadiusEstimator
{
    private const int   MAX_SAMPLES = 64;

    // --- gates de RODADURA LIMPIA ---
    // Sin gas: cualquier par motor mete slip en la rueda tractora.
    private const float MAX_THROTTLE = 0.05;
    // Sin freno: el lockup es el peor caso (omega->0 hace explotar v/omega).
    private const float MAX_BRAKE    = 0.02;
    // Velocidad minima: abajo de esto el cociente esta mal condicionado y el slip domina.
    private const float MIN_MS       = 3.0;
    private const float MAX_STEER    = 0.15;  // rad: mismo criterio que el planeo
    private const float MIN_OMEGA    = 1.0;
    // Banda fisicamente posible para una rueda de vehiculo terrestre. Fuera de esto la
    // muestra es basura, no un vehiculo raro.
    private const float R_MIN        = 0.15;
    private const float R_MAX        = 0.80;

    private ref array<float> m_Samples;
    private int m_Rejected;

    void BZWheelRadiusEstimator()
    {
        m_Samples = new array<float>();
        m_Rejected = 0;
    }

    void Reset()
    {
        m_Samples.Clear();
        m_Rejected = 0;
    }

    // Devuelve true si la muestra fue ACEPTADA (rodadura limpia y radio plausible).
    bool AddSample(float chassisMs, float omegaRadS, float throttle, float brake, int wheelsOnGround, int wheelCount, float wheelRad)
    {
        if (m_Samples.Count() >= MAX_SAMPLES) return false;
        if (!CleanRolling(chassisMs, omegaRadS, throttle, brake, wheelsOnGround, wheelCount, wheelRad))
        {
            m_Rejected++;
            return false;
        }
        float r = chassisMs / omegaRadS;
        if (r < R_MIN || r > R_MAX)
        {
            m_Rejected++;
            return false;
        }
        m_Samples.Insert(r);
        return true;
    }

    // El gate. Cada condicion esta por una razon fisica, no por tuneo.
    private float AbsF(float v)
    {
        if (v < 0) return -v;
        return v;
    }

    private bool CleanRolling(float chassisMs, float omegaRadS, float throttle, float brake, int wheelsOnGround, int wheelCount, float wheelRad)
    {
        if (!BZMath.Finite(chassisMs)) return false;
        if (!BZMath.Finite(omegaRadS)) return false;
        if (throttle > MAX_THROTTLE) return false;        // par motor -> wheelspin
        if (brake > MAX_BRAKE) return false;              // par de freno -> lockup
        if (chassisMs < MIN_MS) return false;             // mal condicionado
        if (omegaRadS < MIN_OMEGA) return false;
        if (wheelCount < 1) return false;
        if (wheelsOnGround < wheelCount) return false;    // rueda en el aire = omega libre
        // ★★ RODADURA LIMPIA INCLUYE IR DERECHO.
        //
        // En curva la rueda medida NO recorre el mismo arco que el chasis -- la de adentro
        // recorre menos, la de afuera mas -- asi que la relacion omega/velocidad ya no da
        // el radio. Faltaba la compuerta: el estimador filtraba gas, freno, velocidad,
        // omega y ruedas en el piso, y nada sobre el volante.
        //
        // Medido: el MISMO bus, el MISMO dia, dio 0.506638 m en dos corridas y 0.476273 en
        // la tercera -- un 6% de diferencia. Y el radio es lo unico que traduce velocidad a
        // rpm (RpmAtSpeed), asi que un 6% corre TODOS los puntos de cambio de marcha.
        //
        // Mismo umbral que TryCoast usa para lo mismo: 0.15 rad de rueda cruzada.
        if (AbsF(wheelRad) > MAX_STEER) return false;
        return true;
    }

    bool HasEstimate()
    {
        return m_Samples.Count() >= 8;
    }

    int SampleCount()  { return m_Samples.Count(); }
    int RejectedCount() { return m_Rejected; }

    // MEDIANA de las muestras aceptadas. 0 = todavia no hay estimacion (nunca un
    // valor inventado: sin dato, el Manual pone su FALLBACK y lo grita).
    float RadiusM()
    {
        int n = m_Samples.Count();
        if (n < 8) return 0;
        array<float> srt = new array<float>();
        int i;
        for (i = 0; i < n; i++)
        {
            srt.Insert(m_Samples.Get(i));
        }
        SortAsc(srt);
        return srt.Get(n / 2);
    }

    // Insertion sort: n <= 64, no vale traer nada mas complicado.
    private void SortAsc(array<float> a)
    {
        int i;
        int j;
        float key;
        for (i = 1; i < a.Count(); i++)
        {
            key = a.Get(i);
            j = i - 1;
            while (j >= 0 && a.Get(j) > key)
            {
                a.Set(j + 1, a.Get(j));
                j--;
            }
            a.Set(j + 1, key);
        }
    }
}
