// ============================================================================
//  BZLegMachine.c - BZ_AutoDrive 1.2
//
//  RESPONSABILIDAD UNICA: segmentar la ruta en legs, mantener el cursor MONOTONO,
//  y producir el BZLegView (ver BZLegView.c).
//
//  POR QUE EXISTE: en v1.1 cada modulo re-escaneaba el leg por su cuenta
//  (SignedCrossTrackMc, GetRecordedSpeedKmh) con un nearest-por-posicion global.
//  Eso es AMBIGUO en un bucle en 8 o en spawn perpendicular -> era el signedMc que
//  mentia. Aca hay UN SOLO duenio del cursor y el avance es POR ARCO, no por
//  distancia global. El cursor NO retrocede salvo SeekTo() explicito.
// ============================================================================

// ---------------------------------------------------------------------------
//  BZLegMachine - UNICO duenio del cursor en todo el sistema.
// ---------------------------------------------------------------------------
class BZLegMachine
{
    private BZRouteData m_Route;
    private float m_WinBackM;
    private float m_WinFwdM;
    private int   m_LegIdx;
    private int   m_Cursor;
    private int   m_ReadqCount;
    private float m_CursorS;
    private bool  m_Ready;

    // Guarda de BUSQUEDA, no tuning per-vehiculo: cuanto arco se explora hacia adelante
    // en un tick. Un salto mayor (teleport / admin TP) NO se persigue: se resuelve con
    // SeekTo() explicito, y mientras tanto el crossTrack grande hace que el arbitro corte.
    private const float SEARCH_M  = 60.0;
    // Re-adquisicion: cuanto arco se permite retroceder, y cuanto MEJOR tiene que ser el
    // candidato de atras para mover el cursor. Ver ReadquirirSiElCursorSeAdelanto.
    private const float READQ_ATRAS_M = 25.0;
    private const float READQ_FACTOR  = 1.5;
    private const float MIN_SEG_M = 0.01;

    void BZLegMachine()
    {
        m_Ready = false;
        m_LegIdx = 0;
        m_Cursor = 0;
        m_CursorS = 0;
        m_WinBackM = 0;
        m_WinFwdM = 0;
    }

    void Configure(BZRouteData route, float winBackM, float winFwdM)
    {
        m_Route = route;
        m_WinBackM = winBackM;
        m_WinFwdM = winFwdM;
        m_LegIdx = 0;
        m_Ready = false;
        if (!route) return;
        if (!route.IsReady()) return;
        m_Cursor = route.LegLo(0);
        m_CursorS = route.S(m_Cursor);
        m_Ready = true;
    }

    // Se llama UNA VEZ por tick, ANTES que cualquier otro modulo.
    void Update(BZVehicleState st, BZLegView view)
    {
        if (!m_Ready)          { view.SetInvalid(); return; }
        if (!m_Route)          { view.SetInvalid(); return; }
        if (!m_Route.IsReady()) { view.SetInvalid(); return; }
        if (!st.IsCoherent())  { view.SetInvalid(); return; }
        view.Bind(m_Route);
        ProjectCursor(st.pos);
        FillLeg(view);
        FillCursor(st, view);
        FillWindow(view);
        view.valid = true;
    }

    // UNICA forma de mover el cursor hacia atras. Todo lo demas es monotono.
    void SeekTo(int wp)
    {
        if (!m_Ready) return;
        int c = wp;
        if (c < 0) c = 0;
        if (c > m_Route.Count() - 1) c = m_Route.Count() - 1;
        m_Cursor = c;
        m_CursorS = m_Route.S(c);
        m_LegIdx = m_Route.LegOf(c);
    }

    bool AdvanceLeg()
    {
        if (!m_Ready) return false;
        if (m_LegIdx + 1 >= m_Route.LegCount()) return false;
        m_LegIdx++;
        m_Cursor = m_Route.LegLo(m_LegIdx);
        m_CursorS = m_Route.S(m_Cursor);
        return true;
    }

    // La tolerancia la pasa el LLAMADOR: una constante aca seria tuning escondido.
    bool AtLegEnd(float tolM)
    {
        if (!m_Ready) return false;
        return (m_Route.S(m_Route.LegHi(m_LegIdx)) - m_CursorS) <= tolM;
    }

    int GetCursor() { return m_Cursor; }
    int GetLegIdx() { return m_LegIdx; }

    // ======================= internos =======================

    // Proyecta la posicion sobre la traza del leg. MONOTONO: solo acepta arco mayor.
    private void ProjectCursor(vector p)
    {
        int lo = m_Route.LegLo(m_LegIdx);
        int hi = m_Route.LegHi(m_LegIdx);
        if (hi <= lo)
        {
            m_Cursor = lo;
            m_CursorS = m_Route.S(lo);
            return;
        }
        int i;
        int best = -1;
        float bestD = 1000000000.0;
        float bestT = 0;
        float t = 0;
        float d = 0;
        for (i = m_Cursor; i < hi; i++)
        {
            if (m_Route.S(i) - m_CursorS > SEARCH_M) break;
            d = SegDistance(p, i, t);
            if (d < bestD)
            {
                bestD = d;
                best = i;
                bestT = t;
            }
        }
        if (best < 0) return;
        if (ReadquirirSiElCursorSeAdelanto(p, bestD)) return;
        AcceptCursor(best, bestT);
    }

    // ⚠⚠⚠ LA RE-ADQUISICION. El bucle de arriba arranca en `m_Cursor` y va SOLO hacia
    // adelante, y `AcceptCursor` ademas rechaza todo arco menor. O sea que si el vehiculo se
    // fue de la traza y quedo ATRAS del cursor, el cursor no puede volver -- no es que nadie
    // lo intente: la busqueda no mira ahi. De ahi salia el "una vez fuera de ruta no hay
    // re-adquisicion" de la tanda: el punto de mira se calculaba desde un cursor adelantado,
    // asi que la geometria apuntaba a un tramo que el vehiculo ya no tenia al lado.
    //
    // ★★★ EL DISPARADOR NO ES EL LATERAL, Y ESO ES DELIBERADO.
    //
    // Un umbral de |lat| mide el SINTOMA, y hay que elegirlo a mano contra la flota del dia:
    // con 2.0 m se dispara todo el tiempo en el Agera (media 1.98) y con 3.0 no se dispara
    // nunca en el r32 (maximo 2.81). Los dos numeros salen de la tanda, no del problema, y
    // el que venga despues los hereda sin saber de donde salieron.
    //
    // El problema es EL CURSOR ADELANTADO, y eso se detecta directo: si el punto mas cercano
    // de verdad esta DETRAS del cursor, el cursor se adelanto. Un vehiculo que corre bien al
    // costado de la traza --2.6 m afuera pero a la altura correcta-- tiene su punto mas
    // cercano AL LADO, no atras, y esto no se enciende. Es la diferencia entre estar
    // DESPLAZADO y estar ATRASADO, que un umbral de lateral no puede distinguir.
    //
    // ✖ Dos guardas, porque una traza puede cruzarse consigo misma:
    //    - acotado a READQ_ATRAS_M de arco: aunque haya algo mas cerca en otra vuelta del
    //      lazo, no salta alla;
    //    - el candidato de atras tiene que ser READQ_FACTOR veces mejor, no empatar: sin eso
    //      el cursor iria y vendria con el ruido de la medicion.
    private bool ReadquirirSiElCursorSeAdelanto(vector p, float dAdelante)
    {
        int lo = m_Route.LegLo(m_LegIdx);
        int i;
        int best = -1;
        float bestD = 1000000000.0;
        float bestT = 0;
        float t = 0;
        float d = 0;
        for (i = m_Cursor - 1; i >= lo; i--)
        {
            if (m_CursorS - m_Route.S(i) > READQ_ATRAS_M) break;
            d = SegDistance(p, i, t);
            if (d < bestD)
            {
                bestD = d;
                best = i;
                bestT = t;
            }
        }
        if (best < 0) return false;
        if (bestD * READQ_FACTOR >= dAdelante) return false;
        // ⚠ Se escribe el cursor a mano: AcceptCursor rechaza arco menor por diseño, y esto
        // es justamente la excepcion acotada a esa regla.
        float s0 = m_Route.S(best);
        float s1 = m_Route.S(best + 1);
        m_ReadqCount = m_ReadqCount + 1;
        if (!BZDriveController.s_Quiet && (m_ReadqCount < 4 || (m_ReadqCount % 8) == 0))
        {
            string ln = "[BZLeg] RE-ADQUISICION #" + m_ReadqCount.ToString();
            ln = ln + ": el cursor se habia adelantado. wp " + m_Cursor.ToString();
            ln = ln + " -> " + best.ToString();
            ln = ln + " · dist adelante " + dAdelante.ToString();
            ln = ln + " · dist atras " + bestD.ToString();
            ln = ln + " · arco recuperado " + (m_CursorS - s0).ToString() + " m";
            Print(ln);
        }
        m_CursorS = s0 + bestT * (s1 - s0);
        m_Cursor = best;
        return true;
    }

    private void AcceptCursor(int seg, float t)
    {
        float s0 = m_Route.S(seg);
        float s1 = m_Route.S(seg + 1);
        float ns = s0 + t * (s1 - s0);
        if (ns <= m_CursorS) return;   // MONOTONO
        m_CursorS = ns;
        m_Cursor = seg;
    }

    // Distancia 2D del punto al segmento i->i+1. Devuelve el parametro t por 'out'.
    private float SegDistance(vector p, int i, out float t)
    {
        vector a = m_Route.Pos(i);
        vector b = m_Route.Pos(i + 1);
        float dx = b[0] - a[0];
        float dz = b[2] - a[2];
        float l2 = dx * dx + dz * dz;
        float ex;
        float ez;
        if (l2 < MIN_SEG_M)
        {
            t = 0;
            ex = p[0] - a[0];
            ez = p[2] - a[2];
            return Math.Sqrt(ex * ex + ez * ez);
        }
        t = ((p[0] - a[0]) * dx + (p[2] - a[2]) * dz) / l2;
        t = Math.Clamp(t, 0.0, 1.0);
        ex = p[0] - (a[0] + t * dx);
        ez = p[2] - (a[2] + t * dz);
        return Math.Sqrt(ex * ex + ez * ez);
    }

    private void FillLeg(BZLegView v)
    {
        v.legIdx = m_LegIdx;
        v.legLo = m_Route.LegLo(m_LegIdx);
        v.legHi = m_Route.LegHi(m_LegIdx);
        v.isReverse = m_Route.LegRev(m_LegIdx);
        v.lengthM = m_Route.LegLengthM(m_LegIdx);
        v.endIsStop = m_Route.IsSemanticStop(v.legHi);
        v.endIsCusp = (m_LegIdx + 1 < m_Route.LegCount());
        v.prevIsReverse = v.isReverse;
        v.nextIsReverse = v.isReverse;
        if (m_LegIdx > 0) v.prevIsReverse = m_Route.LegRev(m_LegIdx - 1);
        if (m_LegIdx + 1 < m_Route.LegCount()) v.nextIsReverse = m_Route.LegRev(m_LegIdx + 1);
    }

    private void FillCursor(BZVehicleState st, BZLegView v)
    {
        v.nearWp = m_Cursor;
        v.cursorS = m_CursorS;
        v.alongM = m_CursorS - m_Route.S(v.legLo);
        v.remainM = m_Route.S(v.legHi) - m_CursorS;
        v.crossTrackM = CrossTrack(st.pos);
        v.headingErrRad = HeadingErr(st, v.isReverse);
        v.signedToEndM = SignedToEnd(st.pos, v.legHi);
    }

    // La perpendicular, consultable desde afuera sin mover el cursor. La caja negra la
    // necesita a 40 Hz: el lat que publica Update() se refresca a 2 Hz y por eso el volcado
    // del choque SUBESTIMABA la apertura hasta 0.59 m justo al entrar en la curva.
    // El segmento sigue siendo el del cursor (2 Hz); lo que se refresca a 40 Hz es la POSE.
    float CrossTrackAhora(vector p)
    {
        return CrossTrack(p);
    }

    // Perpendicular firmada a la traza. Misma formula que el SignedCrossTrackMc de v1.1,
    // pero sobre el segmento del CURSOR (no sobre un nearest global que en un bucle miente).
    private float CrossTrack(vector p)
    {
        int i = m_Cursor;
        if (i > m_Route.Count() - 2) i = m_Route.Count() - 2;
        if (i < 0) return 0;
        vector a = m_Route.Pos(i);
        vector b = m_Route.Pos(i + 1);
        float dx = b[0] - a[0];
        float dz = b[2] - a[2];
        float l = Math.Sqrt(dx * dx + dz * dz);
        if (l < MIN_SEG_M) return 0;
        dx = dx / l;
        dz = dz / l;
        return dx * (p[2] - a[2]) - dz * (p[0] - a[0]);
    }

    // En REVERSA el vehiculo mira ~180 grados contra la tangente de la traza (la traza avanza
    // en el sentido del MOVIMIENTO, el auto apunta al reves). Sin esto, media vuelta de error.
    private float HeadingErr(BZVehicleState st, bool rev)
    {
        float refH = m_Route.Head(m_Cursor);
        if (rev) refH = refH + Math.PI;
        return BZMath.WrapPi(refH - st.yaw);
    }

    // Proyeccion firmada sobre la tangente del punto final: + = todavia falta llegar.
    // heading = Atan2(x, z)  =>  vector unitario = (sin h, cos h).
    private float SignedToEnd(vector p, int hi)
    {
        vector e = m_Route.Pos(hi);
        // ⚠⚠ LA TANGENTE DEL ULTIMO WAYPOINT DEL LEG YA ES LA DEL LEG SIGUIENTE.
        // Head(i) es el rumbo del segmento i -> i+1 (BZRouteData.ComputeHeadings), asi que
        // Head(hi) cruza al leg que sigue. En un CUSP ese segmento apunta al REVES, y la
        // proyeccion sale NEGATIVA desde el primer tick.
        //
        // Lo que eso provoca es peor que un numero mal: Closed() ve `signed <= BZDriveTol.LLEGADA_M`
        // con el vehiculo detenido y da el leg por CERRADO sin manejarlo. Medido en
        // PENDIENTE: el leg de reversa abrio y cerro en el MISMO milisegundo, con
        // `along -37.58` y CERO muestras -- el vehiculo no se movio un centimetro, el leg
        // siguiente arranco 26 m fuera de la traza, y la ruta igual reporto ROUTE_COMPLETE
        // a 553 m del final con el slam sin correr nunca.
        //
        // NO ES UN PROBLEMA DE LA REVERSA: lo que decide es si el waypoint final del leg es
        // un cusp, no el sentido del leg. Un leg FORWARD que termina en el cusp se daba vuelta
        // igual (verificado en verify_cusp_signed, seccion 3).
        //
        // La tangente que corresponde es la de ADENTRO del leg: el segmento hi-1 -> hi.
        // Solo se usa si ese waypoint pertenece al MISMO leg; si el leg tiene un solo punto,
        // no hay tangente interna y se queda la de antes.
        int t = hi - 1;
        if (t < 0 || m_Route.LegOf(t) != m_Route.LegOf(hi)) t = hi;
        float th = m_Route.Head(t);
        float tx = Math.Sin(th);
        float tz = Math.Cos(th);
        return (e[0] - p[0]) * tx + (e[2] - p[2]) * tz;
    }

    private void FillWindow(BZLegView v)
    {
        float loTgt = m_CursorS - m_WinBackM;
        float hiTgt = m_CursorS + m_WinFwdM;
        int i;
        int wl = m_Cursor;
        int wh = m_Cursor;
        for (i = m_Cursor; i >= v.legLo; i--)
        {
            if (m_Route.S(i) < loTgt) break;
            wl = i;
        }
        for (i = m_Cursor; i <= v.legHi; i++)
        {
            if (m_Route.S(i) > hiTgt) break;
            wh = i;
        }
        v.winLo = wl;
        v.winHi = wh;
        v.backM = m_CursorS - m_Route.S(wl);
        v.fwdM = m_Route.S(wh) - m_CursorS;
    }
}
