// ============================================================================
//  BZLegView.c - BZ_AutoDrive 1.2
//
//  Contexto de SOLO LECTURA que BZLegMachine refilla cada tick y consumen todos
//  los modulos de manejo. Instancia REUSADA: cero alloc por frame.
//
//  No es un punto, es una VENTANA (SEAM.md 5): expone arco atras y adelante para
//  que ningun modulo tenga que re-escanear el leg por su cuenta.
// ============================================================================

// ---------------------------------------------------------------------------
//  BZLegView - contexto de SOLO LECTURA para los modulos. Instancia REUSADA:
//  la refilla BZLegMachine cada tick. Ningun modulo la modifica.
// ---------------------------------------------------------------------------
class BZLegView
{
    bool  valid;            // false => input incoherente. El arbitro va a SAFE_STOP.

    // ---- leg activo ----
    int   legIdx;
    int   legLo;
    int   legHi;
    bool  isReverse;
    bool  prevIsReverse;    // detectar CUSP sin re-escanear la ruta
    bool  nextIsReverse;
    // El final de ESTE leg es una parada SEMANTICA (isStop o fin de ruta). Lo necesita el
    // arbitro para entrar a la cadena de desaceleracion, y sale de la ruta -- no se infiere
    // de la velocidad grabada (un 0 km/h tambien puede ser un semaforo).
    bool  endIsStop;
    // el tramo termina en INTERCAMBIO (hay un leg despues): extension explicita del
    // contrato -- los cusps arman la rampa final igual que los stops.
    bool  endIsCusp;
    float lengthM;

    // ---- cursor (MONOTONO) ----
    int   nearWp;
    float cursorS;          // arco absoluto del cursor
    float alongM;           // arco desde legLo
    float remainM;          // arco hasta legHi
    float signedToEndM;     // proyeccion firmada sobre la tangente FINAL (medida de POSE)
    float crossTrackM;      // error lateral firmado
    float headingErrRad;    // rumbo de referencia - yaw

    // ---- VENTANA ----
    int   winLo;
    int   winHi;
    float backM;            // arco REALMENTE disponible atras (en bordes < el pedido)
    float fwdM;             // idem adelante

    private BZRouteData m_Route;

    void Bind(BZRouteData r)
    {
        m_Route = r;
    }

    void SetInvalid()
    {
        valid = false;
        legIdx = -1;
        legLo = 0;
        legHi = 0;
        isReverse = false;
        prevIsReverse = false;
        nextIsReverse = false;
        endIsStop = false;
        endIsCusp = false;
        lengthM = 0;
        nearWp = -1;
        cursorS = 0;
        alongM = 0;
        remainM = 0;
        signedToEndM = 0;
        crossTrackM = 0;
        headingErrRad = 0;
        winLo = 0;
        winHi = 0;
        backM = 0;
        fwdM = 0;
    }

    // Sample exacto de un waypoint, acotado a la VENTANA (no se lee fuera de ella).
    bool Sample(int i, BZPathSample outS)
    {
        if (!valid) return false;
        if (!m_Route) return false;
        int c = i;
        if (c < winLo) c = winLo;
        if (c > winHi) c = winHi;
        FillSample(c, outS);
        return true;
    }

    // Sample a 'ds' metros de arco del cursor (- atras, + adelante), acotado a la ventana.
    // Se interpola SOLO la posicion; rumbo/curvatura/velocidad salen del wp base -- interpolar
    // angulos exige manejo de wrap y a 1 wp de resolucion no aporta nada, pero SI puede romper.
    bool SampleAt(float ds, BZPathSample outS)
    {
        if (!valid) return false;
        if (!m_Route) return false;
        float tgt = cursorS + ds;
        float loS = m_Route.S(winLo);
        float hiS = m_Route.S(winHi);
        if (tgt < loS) tgt = loS;
        if (tgt > hiS) tgt = hiS;
        int i = FindIdxAtArc(tgt);
        FillSample(i, outS);
        InterpMuestra(i, tgt, outS);
        return true;
    }

    // Arco firmado desde el CURSOR hasta el wp i (- atras, + adelante). Es la medida que
    // usan los modulos para "cuanto falta"; ArcBetween entre indices no incluye la fraccion
    // de segmento donde el cursor realmente esta.
    float ArcFromCursor(int i)
    {
        if (!m_Route) return 0;
        return m_Route.S(i) - cursorS;
    }

    float ArcBetween(int a, int b)
    {
        if (!m_Route) return 0;
        return m_Route.ArcBetween(a, b);
    }

    // "El ojo": minima velocidad GRABADA en los proximos lookaheadM, dentro de la ventana.
    // Devuelve -1 si no hay nada por delante (el modulo lo ve y no extrapola a ciegas).
    // La velocidad grabada DONDE ESTA EL VEHICULO. Es la referencia de "cuanto hay que ir
    // AHORA"; el minimo sobre el horizonte es otra cosa --"cuanto habra que ir dentro de un
    // rato"-- y cerca de una parada siempre tiende a cero, asi que como referencia de gas
    // se desarma justo donde hace falta. Es la misma distincion que en el lateral entre la
    // curvatura local y el promedio del arco.
    // El agarre lateral que la toma probo. 0 si no se pudo medir.
    float ALatToma()
    {
        if (!m_Route) return 0;
        return m_Route.ALatToma();
    }

    // ★ El presupuesto lateral A LA VELOCIDAD GRABADA de ese waypoint. Se evalua con la
    // velocidad del HUMANO y no con la actual de Boris a proposito: usar la actual seria
    // circular --si va lento el cap le da poco presupuesto y lo deja lento-- y ademas es
    // menos fiel. La pregunta correcta es "cuanto agarre tenia el humano cuando paso por
    // aca a la velocidad que paso".
    float ALatTomaEn(float recKmh)
    {
        if (!valid) return 0;
        if (!m_Route) return 0;
        return m_Route.ALatTomaEn(recKmh);
    }

    // El agarre para el cap de DIRECCION (p95). Ver BZRouteData: es otro percentil del mismo
    // dato, porque velocidad y direccion piden lo contrario del mismo numero.
    float ALatDir()
    {
        if (!m_Route) return 0;
        return m_Route.ALatDir();
    }

    float ALatDirEn(float recKmh)
    {
        if (!valid) return 0;
        if (!m_Route) return 0;
        return m_Route.ALatDirEn(recKmh);
    }

    float RecSpeedHere()
    {
        if (!valid) return -1.0;
        if (!m_Route) return -1.0;
        return m_Route.RecKmh(nearWp);
    }

    // El pedal GRABADO en este waypoint. -1 = la ruta no lo trae (hasInputData=0), que es el
    // caso de toda la flota numerada: por construccion, el feedforward no la puede tocar.
    // ⚠ ES BINARIO. Medido en las tres tomas de supercar: 0% de valores intermedios. El
    // teclado no da analogico, asi que "reproducir el pedal" es "a fondo o nada", no una
    // curva. El humano va a fondo 64-71% del recorrido y afloja el resto.
    float RecThrottleHere()
    {
        if (!valid) return -1.0;
        if (!m_Route) return -1.0;
        return m_Route.RecThr(nearWp);
    }

    // El angulo de rueda GRABADO en este waypoint, en grados. -999 = la ruta no lo trae.
    float RecSteerDegHere()
    {
        if (!valid) return -999.0;
        if (!m_Route) return -999.0;
        return m_Route.RecSteerDeg(nearWp);
    }

    float MinRecSpeedAhead(float lookaheadM)
    {
        if (!valid) return -1.0;
        if (!m_Route) return -1.0;
        int i;
        float v;
        float best = -1.0;
        for (i = nearWp; i <= winHi; i++)
        {
            if (m_Route.S(i) - cursorS > lookaheadM) break;
            v = m_Route.RecKmh(i);
            if (best < 0) best = v;
            if (v < best) best = v;
        }
        return best;
    }

    private void FillSample(int i, BZPathSample outS)
    {
        outS.wp            = i;
        outS.pos           = m_Route.Pos(i);
        outS.headingRad    = m_Route.Head(i);
        outS.recHeadingRad = m_Route.RecHeadRad(i);
        outS.curvature     = m_Route.Curv(i);
        outS.recSpeedKmh   = m_Route.RecKmh(i);
        outS.recThrottle   = m_Route.RecThr(i);
        outS.handbrake     = m_Route.Handbrake(i);
        outS.isReverse     = m_Route.IsRev(i);
        outS.legBreak      = m_Route.LegBreak(i);
        outS.isStop        = m_Route.IsStop(i);
        outS.tight         = m_Route.Tight(i);
    }

    // ⚠⚠⚠ SE INTERPOLA LA MUESTRA, NO SOLO LA POSICION.
    //
    // Esto se llamaba `InterpPos` y hacia exactamente lo que decia: tocaba `outS.pos` y nada
    // mas. `FillSample(i)` dejaba `headingRad` y `curvature` SNAPPEADOS al waypoint. O sea
    // que la muestra era internamente inconsistente: la posicion se deslizaba y el rumbo
    // saltaba. Dos consumidores lo pagaban:
    //
    //  1. `MiraCorridaHaciaAfuera` usa el rumbo para la DIRECCION del sesgo. Al cruzar cada
    //     waypoint el punto de mira pegaba un salto lateral de 0.24 m (k 0.05) o 0.40 m
    //     (k 0.12). En RECTA no --ahi el sesgo vale 0-- asi que el sintoma es solo en curva.
    //
    //  2. `CurvaturaMaxAdelante` --el que alimenta el TECHO DE CORTE del Ld-- mide
    //     |dRumbo|/base entre dos SampleAt. Con el rumbo snappeado la estimacion quedaba
    //     CUANTIZADA en pasos de k*espaciado: para un k real de 0.05 saltaba entre 0.000 y
    //     0.100. Cien por ciento de error, decidiendo cuanto se acorta el horizonte.
    //
    // ★ La propiedad que lo mantiene honesto es la PARIDAD: con `tgtS` justo sobre un
    // waypoint (t = 0) los tres campos dan EXACTAMENTE lo que daba antes. No se cambia la
    // traza -- se llena lo que hay entre medio, que hasta ahora se rellenaba con el valor
    // del waypoint anterior.
    //
    // ✖ `recSpeedKmh` NO se interpola, a proposito: el perfil de velocidad toma el MINIMO
    // sobre la ventana, ahi snapear es conservador, y cambiarlo seria otro cambio de control
    // en el mismo PBO. Queda fuera de alcance y dicho.
    private void InterpMuestra(int i, float tgtS, BZPathSample outS)
    {
        if (i >= winHi) return;
        float s0 = m_Route.S(i);
        float s1 = m_Route.S(i + 1);
        float seg = s1 - s0;
        if (seg < 0.01) return;
        float t = (tgtS - s0) / seg;
        t = Math.Clamp(t, 0.0, 1.0);
        vector a = m_Route.Pos(i);
        vector b = m_Route.Pos(i + 1);
        outS.pos = a + (b - a) * t;
        // ⚠ Por el ARCO CORTO: dos rumbos a los lados de +-pi distan poco, y restarlos
        // crudo daria casi una vuelta entera en la direccion equivocada.
        float dh = BZMath.WrapPi(m_Route.Head(i + 1) - m_Route.Head(i));
        outS.headingRad = m_Route.Head(i) + dh * t;
        outS.curvature = m_Route.Curv(i) + (m_Route.Curv(i + 1) - m_Route.Curv(i)) * t;
    }

    // Camina desde el cursor. Acotado a la ventana -> O(k) con k chico, nunca O(leg).
    private int FindIdxAtArc(float tgtS)
    {
        int i;
        if (tgtS >= cursorS)
        {
            for (i = nearWp; i < winHi; i++)
            {
                if (m_Route.S(i + 1) > tgtS) return i;
            }
            return winHi;
        }
        for (i = nearWp; i > winLo; i--)
        {
            if (m_Route.S(i) <= tgtS) return i;
        }
        return winLo;
    }
}
