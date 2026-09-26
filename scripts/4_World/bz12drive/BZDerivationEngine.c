// ============================================================================
//  BZDerivationEngine.c - BZ_AutoDrive 1.2 -- MODELO INVERSO (version real)
//
//  (velocidad / decel / curvatura)  ->  (throttle / brake / gear / steer)  para ESTE
//  vehiculo. Es EL frente de research: con hasInputData=0 el framework DESCARTA los
//  pedales grabados y los re-deriva, asi que la fidelidad del manejo depende entera-
//  mente de este archivo. La grabacion da LINEA + velocidades; los pedales los inventa
//  esto.
//
//  El stub del paso 5 se BORRO y se reescribio de cero (no se le agregaron casos).
//  Se pudo porque tiene UN SOLO consumidor (BZPurePursuit) y UNA sola linea de
//  invocacion -- que es exactamente el motivo de haberlo aislado.
//
//  ── LA IDEA CENTRAL: PARTIR DE LO QUE EL VEHICULO HACE SOLO ──────────────────
//  En vez de modelar rodadura + freno motor + aero por separado (cada uno con su
//  coeficiente inventado), se parte del PLANEO MEDIDO:
//
//      a_libre = -CoastDecel(v, piso) - g*sin(pitch)      <- lo que pasa sin tocar nada
//
//  CoastDecel es MEDIDO por el envelope y resume masa + freno motor + rodadura + aero
//  + PISO en una sola respuesta (MODULES.md 1.1, regla 2: hay cosas que solo se saben
//  midiendo). Y despues:
//
//      a_pedida > a_libre  ->  falta empuje    -> THROTTLE (por curva de torque)
//      a_pedida < a_libre  ->  sobra inercia   -> BRAKE   (por decel real del piso)
//
//  Asi el modelo no pelea contra la medicion: la usa de piso. Esto es lo que arreglaba
//  el caso medido del M3 (1400 kg) que se plantaba 8.5 m antes del endpoint mientras el
//  Sedan (1000 kg) clavaba con el MISMO codigo: el M3 planeaba distinto y nadie lo sabia.
//
//  ESTADO PROPIO: integral del lazo de velocidad + marcha con histeresis. Nadie mas lo lee.
// ============================================================================

class BZDerivationEngine
{
    // Marcha DayZ: 0 = reversa, 1 = neutral, 2 = primera. Indice de RATIO 0 = 1ra fisica.
    private const int GEAR_REVERSE = 0;
    private static const int GEAR_FIRST   = 2;

    // Constante de tiempo del lazo de velocidad. Es un parametro de CONTROL (que tan
    // rapido se cierra el error), no un dato del vehiculo: no cambia entre un Willys y
    // un bus. Mas chico = mas agresivo.
    private const float TAU_S = 1.0;
    // Banda muerta: abajo de esto no se corrige, para no bombear pedales con el ruido
    // del velocimetro.
    private const float V_DEADBAND_MS = 0.25;
    // Rendimiento de la transmision. Constante FISICA de cajas mecanicas, no tuning
    // per-vehiculo. Si el envelope llega a medir aceleracion real, la reemplaza.
    // Ventana util de vueltas para elegir marcha, como fraccion de idle..redline.
    private const float RPM_LO_FRAC = 1.30;
    private const float ACEL_MARGEN_KMH = 3.0;   // sobre esto, el objetivo pide acelerar
    private const float ACEL_KMH_MIN    = 3.0;   // por debajo, arrancar es primera y punto   // sobre el ralenti: abajo de esto el motor lugea
    private const float RPM_HI_FRAC = 0.90;   // del corte: arriba de esto hay que subir marcha
    // Histeresis de cambio: la marcha actual se conserva mientras su rpm quede en una
    // ventana MAS ANCHA que la de seleccion. Sin esto se caza marcha en el umbral --
    // mismo problema que el chatter del arbitro, resuelto igual.
    private static const float RPM_KEEP_LO = 1.10;
    // Por debajo de esto la diferencia de velocidad entre dos ticks es ruido de medicion.
    private const float COLAPSO_EPS_KMH = 0.25;
    // "A fondo": el mismo umbral con el que el detector de clavada define pedal abajo.
    private const float COLAPSO_THR_MIN = 0.95;
    private static const float RPM_KEEP_HI = 0.97;

    private float m_Integral;
    private int   m_LastGear;
    private bool  m_CongelarCorreccion;   // lo decide el controlador, que tiene la ventana
    private float m_PrevKmh;
    private float m_PrevThr;
    // 0 = apagado (el default del int es el estado inerte, como manda la leccion del slip).
    private int   m_MarchaForzada;
    // ⚠⚠⚠ DEBOUNCE DEL DESEO DE MARCHA. En la ruta 06 la caja CAZABA 3<->4
    // a ~1 Hz en la banda 28-34 (75 cambios visibles a 2 Hz; 3ra y 4ta se solapan
    // completas en rpm ahi): dos criterios del selector disputan (Acelerando->banda vs
    // crucero->la-mas-larga) y la demanda alterna con las curvas encadenadas. Cada
    // cambio disparaba el freeze de embrague (~continuo en la banda) y NINGUN policy
    // del freeze gana asi (ZERO = volantazos en curva, 5.43 m; HOLD = corrector stale
    // por toda la banda, media 0.38->0.68). La marcha nueva debe SOSTENERSE como deseo
    // MARCHA_DEBOUNCE_TICKS ticks de Plan consecutivos: la alternancia a ~1 Hz (deseo
    // cambia cada ~2 ticks) nunca junta 3 -> quieta; el deseo REAL (aceleracion larga)
    // comete a 1.5 s -- inocuo (los cambios reales van cada 2-5 s, y en el corte el
    // limitador aguanta). `Colapsando` SALTEA el debounce: la cicatriz de la pendiente
    // (15-19 grados) manda -- el downshift de rescate sigue inmediato.
    private const int MARCHA_DEBOUNCE_TICKS = 3;
    private int   m_DeseoMarcha;
    private int   m_DeseoMarchaTicks;

    private int AplicarDebounceMarcha(int deseo, bool colaps)
    {
        if (deseo == m_LastGear)
        {
            m_DeseoMarchaTicks = 0;
            return m_LastGear;
        }
        if (colaps)
        {
            m_DeseoMarchaTicks = 0;
            m_LastGear = deseo;
            return deseo;
        }
        if (deseo == m_DeseoMarcha)
        {
            m_DeseoMarchaTicks = m_DeseoMarchaTicks + 1;
        }
        else
        {
            m_DeseoMarcha = deseo;
            m_DeseoMarchaTicks = 1;
        }
        if (m_DeseoMarchaTicks >= MARCHA_DEBOUNCE_TICKS)
        {
            m_DeseoMarchaTicks = 0;
            m_LastGear = deseo;
            return deseo;
        }
        return m_LastGear;
    }

    void BZDerivationEngine()
    {
        Reset();
    }

    void SetCongelarCorreccion(bool v)
    {
        m_CongelarCorreccion = v;
    }

    void Reset()
    {
        m_Integral = 0;
        m_LastGear = GEAR_FIRST;
        m_DeseoMarcha = 0;
        m_DeseoMarchaTicks = 0;
        m_PrevKmh = 0;
        m_PrevThr = 0;
    }

    int GetGear() { return m_LastGear; }

    void ToInputs(BZDriveGoal g, BZVehicleState st, BZVehicleManual m, int surfKind, BZDriveOutput o)
    {
        o.Reset();
        if (!g || !m || !st)
        {
            o.SetHalt();
            return;
        }
        if (g.halt)
        {
            o.SetHalt();
            o.gear = GearFor(g, st, m);
            return;
        }
        o.steering = SteerFor(g, st, m);
        o.gear = GearFor(g, st, m);
        Pedals(g, st, m, surfKind, o);
        // El gas del tick ANTERIOR es lo que el detector de colapso necesita: GearFor corre
        // antes que Pedals, asi que el de este tick todavia no existe cuando hace falta.
        m_PrevThr = o.throttle;
        o.Clamp();
    }

    // ======================= DIRECCION =======================

    // Modelo bicicleta INVERTIDO con understeer MEDIDO:
    //     delta = atan( L * kappa / k(v) )
    // k(v) sale del envelope si se midio; si no, del prior neutro (1.0). A proposito NO
    // es el 0.90 inventado de v1.1, que el envelope midio en ~0.50 -- importar ese prior
    // seria importar la magia con otro nombre.
    // Convencion: kappa POSITIVA = derecha, volante POSITIVO = derecha. En reversa el
    // mismo volante rota al reves -> se invierte. Una linea, no un config.
    private float SteerFor(BZDriveGoal g, BZVehicleState st, BZVehicleManual m)
    {
        // ⚠ LA MISMA LEY QUE EL REFRESCO DE 40 Hz. El volante lo escriben dos caminos y
        // este es el otro: si aca quedara la conversion vieja, el Plan pisaria cada 500 ms
        // el comando gentil con uno que amplifica la correccion por subviraje. Es el mismo
        // error que el cap, y la leccion ya la pagamos una vez.
        // ⚠ EL MISMO CONGELAMIENTO QUE EN EL CAMINO DE 40 Hz. Son los DOS escritores del
        // volante, y ya nos mordio dos veces arreglar uno y que el otro lo pise.
        float kCorr = g.curvature - g.curvatureFF;
        // ⚠ NO se recalcula la condicion: la decide el controlador, que es el que tiene la
        // VENTANA (evento de cambio + tope). Recalcularla aca con la coherencia sola volveria
        // a congelar continuo con el lugueo, que es lo que rompio al hx58.
        // ✖ hold enterrado.
        if (m_CongelarCorreccion) kCorr = 0;
        float s = m.SteerCommandSplit(g.curvatureFF, kCorr, st.kmh);
        if (g.dir < 0) s = -s;
        return Math.Clamp(s, -1.0, 1.0);
    }

    // ======================= PEDALES =======================

    private void Pedals(BZDriveGoal g, BZVehicleState st, BZVehicleManual m, int surfKind, BZDriveOutput o)
    {
        float v = st.chassisKmh / 3.6;
        float err = g.speedMs - v;
        // BANDA MUERTA = "mantener", NO "soltar todo". Sostener una velocidad exige el
        // throttle que CANCELA el planeo; soltando todo el vehiculo desacelera dentro de
        // la banda y despues hay que recuperar. La banda solo anula el termino de ERROR,
        // que es lo que bombea con el ruido del velocimetro.
        float aReq = 0;
        if (err > V_DEADBAND_MS || err < -V_DEADBAND_MS) aReq = err / TAU_S;
        float aMaxBrake = m.MaxBrakeDecel(surfKind);
        if (g.decelMs2 > 0.1 && aReq < -g.decelMs2) aReq = -g.decelMs2;
        if (aReq < -aMaxBrake) aReq = -aMaxBrake;
        float aFree = FreeAccel(st, m, surfKind);
        if (aReq > aFree)
        {
            o.throttle = ThrottleFor(aReq - aFree, st, m, o.gear);
            o.brake = 0;
            return;
        }
        o.throttle = 0;
        o.brake = BrakeFor(aFree - aReq, m, surfKind);
    }

    // Lo que el vehiculo hace SOLO: planeo medido + gravedad. Negativo = desacelera.
    // pitch > 0 (trompa arriba) frena; en bajada la gravedad EMPUJA y aFree puede ser
    // positivo: en bajada fuerte el vehiculo acelera solo y el freno entra aunque el error
    // de velocidad sea cero.
    private float FreeAccel(BZVehicleState st, BZVehicleManual m, int surfKind)
    {
        return m.FreeAccel(st.chassisKmh, st.pitch, surfKind);
    }

    // Throttle por CURVA DE TORQUE: fuerza que falta -> par en la rueda -> par en el
    // motor -> fraccion del par disponible a ESAS vueltas. Nada de mapear "aceleracion
    // deseada" a un numero por regla de tres.
    private float ThrottleFor(float aNeed, BZVehicleState st, BZVehicleManual m, int gearDayZ)
    {
        return m.ThrottleForAccel(aNeed, st.chassisKmh, st.rpm, gearDayZ);
    }

    // Freno como fraccion de la decel REAL que el vehiculo tiene en ESE piso -- no de una
    // constante horneada. Es lo que hace que el endpoint generalice entre asfalto y tierra.
    private float BrakeFor(float aNeed, BZVehicleManual m, int surfKind)
    {
        if (aNeed <= 0) return 0;
        float aMax = m.MaxBrakeDecel(surfKind);
        if (aMax < 0.5) return Math.Clamp(aNeed, 0.0, 1.0);
        return Math.Clamp(aNeed / aMax, 0.0, 1.0);
    }

    // ======================= MARCHA =======================

    // Se elige por VUELTAS, no por velocidad tope: la marcha mas alta que deje el motor
    // dentro de su ventana util a la velocidad objetivo. Con histeresis (ventana de
    // conservacion mas ancha que la de seleccion) para no cazar marcha en el umbral --
    // el mismo problema que el chatter del arbitro, resuelto de la misma forma.
    // ★★★ LA VENTANA DE CONSERVACION, IMPRESA EN km/h.
    //
    // La regla de conservar marcha esta anclada al RALENTI (`rpmIdle * RPM_KEEP_LO`), no a la
    // banda de potencia. En un vehiculo cuya banda vive cerca de 6000-7000 rpm eso deja una
    // ventana enorme por abajo: en el GT2RS la 5ta se conserva hasta ~25-38 km/h segun el
    // ralenti. O sea que "gear 6 a 40 km/h" NO es un downshift que fallo en dispararse: es la
    // regla diciendo que hay que conservarla. Y `MarchaDeBanda` --la unica que sabe de la
    // banda-- solo corre cuando `Acelerando` o `Colapsando`: en una desaceleracion MANDADA
    // (frenar para una curva) no corre ninguna de las dos, asi que durante toda la frenada el
    // unico criterio es esta ventana, que no tiene ni idea de donde esta la banda.
    //
    // Se IMPRIME en vez de razonarse: la velocidad de suelte por marcha sale de las
    // relaciones REALES del vehiculo, no de mi aritmetica sobre una tabla de un relay.
    // ⚠ ESTATICA Y LLAMADA DESDE LogProvenance, no desde SetRoute. La primera version se
    // llamaba al asignar la ruta y salia DOCE VECES por corrida con numeros IDENTICOS entre
    // vehiculos distintos (uno con marchas=5 y otro con marchas=4 imprimian la misma tabla),
    // porque en ese punto el manual todavia no tenia el config del vehiculo activo. Un
    // instrumento que publica numeros que no corresponden es peor que no tenerlo: parece dato.
    static void ReportarVentanaDeMarchas(BZVehicleManual m)
    {
        if (!m) return;
        if (!m.gearRatios) return;
        int n = m.gearRatios.Count();
        if (n < 1) return;
        Print("[Deriv] ventana de CONSERVACION por marcha (donde KeepsCurrentGear dice 'no cambies'):");
        int i;
        for (i = 0; i < n; i++)
        {
            float rpmLo = m.rpmIdle * RPM_KEEP_LO;
            float rpmHi = m.rpmRedline * RPM_KEEP_HI;
            float r1 = m.RpmAtSpeed(1.0, i);
            if (r1 < 0.0001) continue;
            float kLo = rpmLo / r1;
            float kHi = rpmHi / r1;
            string ln = "[Deriv]   marcha fisica " + (i + 1).ToString();
            ln = ln + " (gear DayZ " + (i + GEAR_FIRST).ToString() + ")";
            ln = ln + " · conserva de " + kLo.ToString() + " a " + kHi.ToString() + " km/h";
            ln = ln + " · banda a " + (m.RpmDeBanda() / r1).ToString() + " km/h";
            Print(ln);
        }
    }

    private int GearFor(BZDriveGoal g, BZVehicleState st, BZVehicleManual m)
    {
        if (g.dir < 0)
        {
            m_LastGear = GEAR_REVERSE;
            return GEAR_REVERSE;
        }
        int n = m.gearRatios.Count();
        if (n < 1) return GEAR_FIRST;
        // ⚠ LA MARCHA SE ELIGE PARA LA VELOCIDAD QUE SE TIENE, NO PARA LA QUE SE QUIERE.
        //
        // Esto era  kmh = max(objetivoMs * 3.6, chassisKmh)  y es una trampa que se
        // auto-refuerza, de la misma familia que 8.2.1: parado y con un objetivo de 30
        // km/h, elegia la marcha de 30 km/h -- en la que un vehiculo detenido no puede
        // arrancar. Sin arrancar la velocidad sigue en cero, y vuelve a elegir la misma.
        // Medido in-game (CONVOY_03 wp 282): gear 5 a 0.004 km/h, thr 1, rpm 900 = ralenti
        // a fondo, clavado hasta el STUCK. Y en el tramo previo, gear 5 a 14.4 km/h: la
        // marcha del objetivo, no la del momento -> lugeando todo el camino.
        //
        // Con la velocidad ACTUAL, parado da 1ra por construccion (ningun gear alcanza el
        // piso de rpm a 0 km/h, asi que `best` queda en 0). El anti-rebote no se pierde:
        // lo da KeepsCurrentGear, que mantiene la marcha mientras el rpm siga en rango.
        float kmh = st.chassisKmh;
        // MODO TEST: fija la marcha mientras sea LEGAL por la regla de seleccion. No crea
        // estados imposibles -- por debajo de su ventana util suelta y manda la logica real,
        // que es justo lo que hay que poder observar. Sin esto, reproducir el caso depende
        // de que el latch caiga del lado largo, y eso salio 1 de cada 3 corridas.
        if (m_MarchaForzada >= GEAR_FIRST)
        {
            int fi = m_MarchaForzada - GEAR_FIRST;
            if (fi < n && m.RpmAtSpeed(kmh, fi) >= m.rpmIdle * RPM_LO_FRAC)
            {
                m_LastGear = m_MarchaForzada;
                m_PrevKmh = kmh;
                return m_MarchaForzada;
            }
        }
        // ⚠⚠ LA HISTERESIS NO PROTEGE UN COLAPSO.
        //
        // La ventana de CONSERVACION es mas ancha por abajo que la de SELECCION (ralenti x
        // 1.10 contra x 1.30): correcto para no cazar marcha alrededor de un estado
        // ESTABLE. Pero medido en PENDIENTE wp 700-725 (subida de 15-19 grados): el
        // vehiculo venia en 2da, pedia mas velocidad, iba a fondo y PERDIA velocidad --
        // 18.8 -> 14.2 -> 12.0 km/h-- y la histeresis le sostuvo la marcha larga todo el
        // derrumbe. Cuando por fin bajo, ya estaba en 8 km/h y el motor clavado en 1180 rpm.
        // Ahi no hay recuperacion: se quedo a 5-7 km/h y el cursor de ruta dejo de avanzar.
        //
        // Cayendo con el pedal pidiendo mas, no hay estado estable que proteger: sostener
        // una marcha por debajo de su ventana util es exactamente lo que hunde al vehiculo.
        // Se exige entonces la ventana de SELECCION, que es la que dice si la marcha sirve.
        // ⚠ `Acelerando` saltea la histeresis igual que `Colapsando`. Los dos son lo mismo:
        // una DEMANDA que manda sobre la comodidad de no cambiar. Sin esto el arreglo de
        // abajo no se aplicaria nunca -- la histeresis devolveria la marcha larga de crucero
        // antes de llegar a elegir.
        // ⚠ Sin curva de par no hay banda que derivar: se cae al criterio de crucero, que
        // es lo que habia. No se inventa una fraccion del corte.
        bool acel = Acelerando(g, st) && m.RpmDeBanda() > 1.0;
        bool colaps = Colapsando(g, st);
        if (!colaps && !acel && KeepsCurrentGear(m, kmh, n)) return m_LastGear;
        int deseo;
        if (acel)
        {
            deseo = MarchaDeBanda(m, kmh, n);
            m_PrevKmh = kmh;
            return AplicarDebounceMarcha(deseo, colaps);
        }
        int best = 0;
        int i;
        float rpm;
        for (i = 0; i < n; i++)
        {
            rpm = m.RpmAtSpeed(kmh, i);
            if (rpm < m.rpmIdle * RPM_LO_FRAC) break;
            if (rpm <= m.rpmRedline * RPM_HI_FRAC) best = i;
        }
        // top-gear tambien aca: si NINGUNA marcha quedo bajo el umbral de cambio (la
        // velocidad ya paso el techo de la ultima), la ultima ES la marcha.
        if (best == 0 && n > 1 && m.RpmAtSpeed(kmh, n - 1) > m.rpmRedline * RPM_HI_FRAC) best = n - 1;
        deseo = best + GEAR_FIRST;
        return AplicarDebounceMarcha(deseo, colaps);
    }

    // Pide mas velocidad y la esta PERDIENDO. Las dos cosas juntas: perder velocidad
    // frenando a proposito es otra cosa, y no querer ir mas rapido tambien.
    // ⚠ TIENE EFECTO LATERAL (mueve m_PrevKmh): se llama UNA vez por tick.
    // ⚠⚠⚠ MANTENER Y ACELERAR NO PIDEN LA MISMA MARCHA.
    //
    // El bucle de abajo elige la marcha MAS LARGA cuyo rpm quede sobre `rpmIdle * 1.30`.
    // Eso es criterio de CRUCERO --y es correcto manteniendo: no revoluciona al pedo--.
    // Pero el umbral esta anclado al RALENTI y la potencia esta anclada al CORTE, y en un
    // motor de rango ancho esos dos anclajes estan a 6.5x de distancia.
    //
    // Medido: el Agera en la recta rapida iba en 5ta con `thr 1` a 2705 rpm
    // medios de 8500 --el 32% del corte--, y la 3ra y la 4ta NO SE USABAN NUNCA: nunca son
    // "la mas larga valida". Reproducia 117 km/h de 237 grabados. No era un techo de
    // velocidad: era el motor fuera de banda con el pie a fondo.
    //
    // ✖ Y NO se puede reusar el bucle de crucero con el piso mas alto: ese bucle arranca en
    // `best = 0` y corta por abajo, asi que si ninguna marcha llega al piso cae a PRIMERA
    // -- a 200 km/h. Hoy queda enmascarado porque el piso es del 13% del corte. Acelerando
    // la regla es otra: entre las que NO pasan de vueltas, la que queda mas cerca de la
    // banda. Lo destaparon tres aserciones fallando en limpio.
    private int MarchaDeBanda(BZVehicleManual m, float kmh, int n)
    {
        if (kmh < ACEL_KMH_MIN) return GEAR_FIRST;      // desde parado, primera y punto
        float banda = m.RpmDeBanda();
        float tope = m.rpmRedline * RPM_HI_FRAC;
        int best = -1;
        float mejor = 1000000000.0;
        int i;
        float rpm;
        float d;
        for (i = 0; i < n; i++)
        {
            rpm = m.RpmAtSpeed(kmh, i);
            if (rpm > tope) continue;                  // pasaria de vueltas
            d = banda - rpm;
            if (d < 0) d = -d;
            if (d < mejor)
            {
                mejor = d;
                best = i;
            }
        }
        // Todas pasan de vueltas: es el tope del vehiculo, manda la mas larga.
        if (best < 0) best = n - 1;
        return best + GEAR_FIRST;
    }

    // El objetivo pide GANAR velocidad, no sostenerla.
    private bool Acelerando(BZDriveGoal g, BZVehicleState st)
    {
        if (g.halt) return false;
        return (g.speedMs * 3.6) > (st.chassisKmh + ACEL_MARGEN_KMH);
    }

    private bool Colapsando(BZDriveGoal g, BZVehicleState st)
    {
        float kmh = st.chassisKmh;
        float prev = m_PrevKmh;
        m_PrevKmh = kmh;
        if (prev <= COLAPSO_EPS_KMH) return false;
        if (kmh > prev - COLAPSO_EPS_KMH) return false;
        // ⚠⚠ ESTA CLAUSULA ERA "pide mas velocidad" Y NO DISPARABA NUNCA EN EL CASO REAL.
        // Medido en PENDIENTE wp 709: gas a FONDO, en 2da, a 15.09 km/h y cayendo -- y el
        // colapso daba false, porque en una cuesta el objetivo de la ruta es la velocidad a
        // la que se GRABO el tramo, o sea la que el vehiculo ya tiene. Pedir "mas que el
        // objetivo" es pedir algo que en una subida no pasa: el vehiculo no quiere ir mas
        // rapido, quiere NO IR MAS LENTO.
        //
        // El discriminador real no depende del objetivo: PEDAL A FONDO Y PERDIENDO. Si el
        // gas ya esta arriba y la velocidad igual cae, esta marcha no puede con lo que hay
        // adelante -- sea cuesta, carga o lo que fuere. Y frenar a proposito sigue excluido
        // por construccion: ahi el gas no esta a fondo.
        if (m_PrevThr < COLAPSO_THR_MIN) return false;
        return true;
    }

    void ForzarMarcha(int gearDayZ) { m_MarchaForzada = gearDayZ; }
    int  MarchaForzada()           { return m_MarchaForzada; }

    private bool KeepsCurrentGear(BZVehicleManual m, float kmh, int n)
    {
        int cur = m_LastGear - GEAR_FIRST;
        if (cur < 0) return false;
        if (cur >= n) return false;
        float rpm = m.RpmAtSpeed(kmh, cur);
        if (rpm < m.rpmIdle * RPM_KEEP_LO) return false;
        // ⚠ EN LA MARCHA MAS ALTA NO HAY ADONDE SUBIR:
        // soltar por rpm alto disparaba la re-seleccion, que bajaba a 5ta, y el corte
        // del cambio ciclaba 6<->5 a ~6600 rpm con el motor PUDIENDO (llego a 7706).
        // Arriba de la ultima marcha el limite es el corte REAL del motor, no el
        // umbral de cambio.
        if (rpm > m.rpmRedline * RPM_KEEP_HI && m_LastGear - GEAR_FIRST < n - 1) return false;
        return true;
    }
}
