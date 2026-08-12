// ============================================================================
//  BZFrameReplay - replay TEMPORAL fiel del stream frame-by-frame (2026-07-05)
//
//  Fase B de project_frame_by_frame_replay. Lee un frame_<ts>_<veh>.csv (grabado
//  por BZFrameRecorder a 40Hz fijo, dt=0.025) y, dado el tiempo transcurrido,
//  devuelve los inputs (throttle/brake/steering/gear) que el humano aplico en ese
//  instante. Boris los comanda cada frame -> su motor los rampea IGUAL que el del
//  humano (mismo tau) -> mismo volante -> misma linea, con los taps. Determinista
//  (dt fijo + mismos inputs + mismo vehiculo). Open-loop: el drift es el barrier.
//
//  El archivo debe estar en el profile del SERVER ($profile:BZ_AutoDrive_PathLogger\)
//  — se graba client-side, hay que deployarlo al server (como las rutas).
//
//  Columnas frame_: t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm
//                     0     1    2       3      4       5    6         7 8 9 10      11        12
// ============================================================================

class BZFrameReplay {

    private ref array<float> m_T;      // t_cum por fila (s)
    private ref array<float> m_Thr;
    private ref array<float> m_Brk;
    private ref array<float> m_Str;
    private ref array<int>   m_Gear;
    private ref array<float> m_Spd;    // speed_kmh grabado (para el speed-lock)
    private ref array<float> m_X;      // pos x,z grabado (para el ancla longitudinal de posicion)
    private ref array<float> m_Z;
    private ref array<float> m_Y;      // elevacion grabada (col 8) -> pendiente FIRMADA por frame
    private ref array<float> m_Slope;  // pendiente firmada por frame (grados; + = sube, - = baja)
    private ref array<float> m_HdgRate; // |cambio de rumbo| por frame (grados/ventana) -> detector de CURVA
    private ref array<float> m_WheelDeg; // front_wheel_deg grabado (Path 1): angulo de rueda EFECTIVO = target del tracking
    private int m_ColWheel; private int m_ColCompThr; private int m_ColCompStr; // indices de columna detectados del header
    private ref array<float> m_CompThr; // AUTOCOMPENSACION UNIVERSAL: comp per-frame imprimida por ILC (cols 13,14)
    private ref array<float> m_CompStr;
    private ref array<float> m_CrossErr; // ILC lateral: error cross-track medido por cursor esta pasada (transitorio)
    private ref array<float> m_Hdg;         // heading grabado (col 10), para el residuo de heading (ILC)
    private ref array<string> m_BaseLine;   // linea base 13-col cruda, para reescribir con comp en Save()

    private bool   m_Loaded;
    private int    m_Cursor;
    private string m_Path;                   // path del frame_ (para el Save del ILC in-game)
    private int    m_LastAccumCursor;        // ILC: ultimo cursor acumulado (evita doble-update)
    private float  m_ILCCoastSum;            // ILC ESCALAR: suma del deficit de velocidad SOLO en coast
    private int    m_ILCCoastN;              // ILC ESCALAR: cantidad de frames de coast muestreados

    // Gains del ILC in-game (autocompensacion universal). Throttle-only por ahora (signo claro).
    // El coast es hiper-sensible al throttle (0.04 -> ~1.2km/h) -> gain BAJISIMO + deadband para que la
    // constante se asiente (~0.01) en vez de rebotar por overshoot y ruido pasada-a-pasada.
    // El comp esta SOBRE-APALANCADO: se aplica sobre ~300 frames de la zona de manejo -> un cambio de comp
    // de 0.007 mueve la aproximacion ~1km/h -> ~1.7m de endpoint. Con la gain 0.02 + el ruido pasada-a-pasada
    // del deficit (0.08 a 0.39), el comp OSCILABA (0.0147<->0.0218) -> aproximacion 10<->11.7 -> endpoint +-1.7m.
    // Gain BAJA + deadband ANCHO: el comp se asienta en el sweet spot (~0.015) y deja de perseguir el ruido.
    private const float ILC_LTHR    = 0.008;  // por km/h (bajo: 301 frames de palanca -> pasos chicos)
    private const float ILC_MAXTHR  = 0.35;   // clamp total comp throttle
    private const float ILC_DEADBAND = 0.15;  // km/h: no tocar el comp si el deficit esta debajo (ruido open-loop)
    // ZONA DE MANEJO (slope-aware, 2026-07-05): el deficit vive donde el HUMANO maneja (|slope|<4deg, en
    // movimiento, sin freno fuerte). En lo empinado la gravedad domina y Boris matchea solo -> NO compensar
    // ahi (agregar throttle lo pasaria). Data: def +0.3km/h en |slope|<4, ~0 en |slope|>4.
    private const float ILC_SLOPE_ZONE = 4.0; // grados: limite de la zona de manejo (fuera = gravedad domina)
    // ILC LATERAL (comp_str): aprende el steering para rechazar la BOLA DE NIEVE lateral, feedforward (no ancla).
    // RUMBO-based (2026-07-05, CORREGIDO): posicion->steering=DOBLE integrador -> hornear comp_str prop. al
    // error de POSICION diverge (oscilador). RUMBO->steering=UN integrador -> estable. m_CrossErr guarda el
    // error de RUMBO (grados). comp_str[i] -= LSTR * hdgErr[i+lead]. Rumbo responde rapido -> lead corto.
    private const float ILC_LSTR     = 0.01;  // gain por GRADO de error de rumbo (bajo -> converge sin oscilar)
    private const int   ILC_STR_LEAD = 5;     // filas de lead (~0.12s): el rumbo responde rapido al steering
    private const float ILC_MAXSTR   = 0.15;  // clamp total comp steering

    private vector m_StartPos;
    private float  m_StartHeading;
    private float  m_StartSpeed;

    // ENDPOINT (endgame de precision de posicion): posicion final grabada + direccion de viaje world-space
    // (de las ultimas filas EN MOVIMIENTO, robusto sin trig de heading) + gear grabado del acercamiento.
    // Direccional-general: el signo del error lo da approachDir y la direccion del creep la da el gear ->
    // el mismo codigo sirve forward Y reverse (terreno alto).
    private float  m_EndX;
    private float  m_EndZ;
    private float  m_ApproachX;   // dir de viaje normalizada (x) aproximando el endpoint
    private float  m_ApproachZ;   // dir de viaje normalizada (z)
    private int    m_EndGear;     // gear grabado en la ultima fila en movimiento (direccion del creep)

    // Estado del ultimo Sample() — leido por getters (evita out-params de Enforce)
    private float  m_CurThr;
    private float  m_CurBrk;
    private float  m_CurStr;
    private int    m_CurGear;
    private float  m_CurSpd;
    private float  m_CurX;
    private float  m_CurZ;
    private float  m_CurSlope;   // pendiente firmada del frame actual (zona de manejo del ILC slope-aware)
    private float  m_CurHdgRate; // heading-rate del frame actual (detector de curva para atenuar el cross-anchor)
    private float  m_CurWheelDeg; // front_wheel_deg grabado del frame actual (Path 1: target del wheel-tracking)
    private float  m_CurCompThr;
    private float  m_CurCompStr;
    private float  m_CurHeading;

    // Split que preserva vacios (igual criterio que BZBusService.SplitKeepEmpty)
    private void SplitLine(string line, TStringArray outArr) {
        outArr.Clear();
        int n = line.Length();
        int start = 0;
        for (int i = 0; i <= n; i++) {
            if (i == n || line.Substring(i, 1) == ",") {
                outArr.Insert(line.Substring(start, i - start));
                start = i + 1;
            }
        }
    }

    bool Load(string file) {
        m_T    = new array<float>();
        m_Thr  = new array<float>();
        m_Brk  = new array<float>();
        m_Str  = new array<float>();
        m_Gear = new array<int>();
        m_Spd  = new array<float>();
        m_X    = new array<float>();
        m_Z    = new array<float>();
        m_Y    = new array<float>();
        m_Slope = new array<float>();
        m_HdgRate = new array<float>();
        m_WheelDeg = new array<float>();
        m_CompThr = new array<float>();
        m_CompStr = new array<float>();
        m_CrossErr = new array<float>();
        m_Hdg     = new array<float>();
        m_BaseLine = new array<string>();
        m_Loaded = false;
        m_Cursor = 0;
        m_LastAccumCursor = -1;

        string path = "$profile:BZ_AutoDrive_PathLogger\\" + file;
        m_Path = path;
        if (!FileExist(path)) {
            BZBusLog.Err("[FrameReplay] no existe: " + path);
            return false;
        }
        FileHandle f = OpenFile(path, FileMode.READ);
        if (!f) {
            BZBusLog.Err("[FrameReplay] no se pudo abrir: " + path);
            return false;
        }
        string line;
        bool first = true;
        while (FGets(f, line) >= 0) {
            if (line == "") continue;
            if (first) {
                first = false;
                // Detectar columnas por NOMBRE (robusto a formato viejo/nuevo): front_wheel_deg (Path 1) +
                // comp_thr/comp_str (ILC). El base = todo lo previo a comp; el wheel puede estar o no.
                TStringArray hc = new TStringArray();
                SplitLine(line, hc);
                m_ColWheel = -1; m_ColCompThr = -1; m_ColCompStr = -1;
                for (int hci = 0; hci < hc.Count(); hci++) {
                    if (hc[hci] == "front_wheel_deg") m_ColWheel = hci;
                    if (hc[hci] == "comp_thr") m_ColCompThr = hci;
                    if (hc[hci] == "comp_str") m_ColCompStr = hci;
                }
                continue;
            }
            TStringArray c = new TStringArray();
            SplitLine(line, c);
            if (c.Count() < 12) continue;
            m_T.Insert(c[0].ToFloat());
            m_Thr.Insert(c[2].ToFloat());
            m_Brk.Insert(c[3].ToFloat());
            m_Str.Insert(c[4].ToFloat());
            m_Gear.Insert(c[5].ToInt());
            m_Spd.Insert(c[11].ToFloat());
            m_X.Insert(c[7].ToFloat());
            m_Z.Insert(c[9].ToFloat());
            m_Y.Insert(c[8].ToFloat());
            m_Hdg.Insert(c[10].ToFloat());
            // Base = todo lo previo a comp (incluye front_wheel_deg si esta). Para reescribir con comp en Save.
            int baseEnd = c.Count();
            if (m_ColCompThr >= 0) baseEnd = m_ColCompThr;
            string baseLine = c[0];
            for (int bc = 1; bc < baseEnd; bc++) baseLine = baseLine + "," + c[bc];
            m_BaseLine.Insert(baseLine);
            // front_wheel_deg (Path 1): el angulo de rueda EFECTIVO grabado = target del wheel-tracking.
            float wheelVal = 0;
            if (m_ColWheel >= 0 && c.Count() > m_ColWheel) wheelVal = c[m_ColWheel].ToFloat();
            m_WheelDeg.Insert(wheelVal);
            // comp per-frame (ILC) por columna detectada; si no estan, 0 (frame_ sin compensar todavia).
            float ctComp = 0;
            float csComp = 0;
            if (m_ColCompThr >= 0 && c.Count() > m_ColCompThr) ctComp = c[m_ColCompThr].ToFloat();
            if (m_ColCompStr >= 0 && c.Count() > m_ColCompStr) csComp = c[m_ColCompStr].ToFloat();
            m_CompThr.Insert(ctComp);
            m_CompStr.Insert(csComp);
            m_CrossErr.Insert(0);
            if (m_T.Count() == 1) {
                vector sp;
                sp[0] = c[7].ToFloat();
                sp[1] = c[8].ToFloat();
                sp[2] = c[9].ToFloat();
                m_StartPos     = sp;
                m_StartHeading = c[10].ToFloat();
                m_StartSpeed   = c[11].ToFloat();
            }
        }
        CloseFile(f);
        // PENDIENTE FIRMADA por frame (de la elevacion grabada m_Y; + = sube, - = baja). Ventana +-3 para
        // suavizar. Fuente fisica-fiel: la pendiente esta EN la toma (la y grabada), no depende del receiver.
        // El ILC slope-aware la usa para compensar SOLO la zona de manejo (|slope|<ILC_SLOPE_ZONE).
        int ns = m_Y.Count();
        for (int si = 0; si < ns; si++) {
            int sa = si - 3; if (sa < 0) sa = 0;
            int sb = si + 3; if (sb > ns - 1) sb = ns - 1;
            float sdy = m_Y[sb] - m_Y[sa];
            float sdx = m_X[sb] - m_X[sa];
            float sdz = m_Z[sb] - m_Z[sa];
            float sdh = Math.Sqrt(sdx * sdx + sdz * sdz);
            float sl = 0;
            if (sdh > 0.01) sl = Math.Atan2(sdy, sdh) * Math.RAD2DEG;
            m_Slope.Insert(sl);
        }
        // HEADING-RATE por frame (|cambio de rumbo| sobre ventana +-5 = 0.25s). Detecta CURVA: el cross-anchor
        // se ATENUA donde el rumbo gira rapido (ahi mandan los taps = la forma) y solo corrige el drift en RECTAS.
        // Ventana grande suaviza el wiggle de los taps -> captura la curvatura real del camino, no el pulso.
        int nh = m_Hdg.Count();
        for (int hi = 0; hi < nh; hi++) {
            int ha = hi - 5; if (ha < 0) ha = 0;
            int hb = hi + 5; if (hb > nh - 1) hb = nh - 1;
            float dh = m_Hdg[hb] - m_Hdg[ha];
            while (dh > 180)  dh = dh - 360;
            while (dh < -180) dh = dh + 360;
            m_HdgRate.Insert(Math.AbsFloat(dh));
        }
        // ENDPOINT + direccion de viaje (endgame de precision). endpoint = ultima fila (reposo). approachDir =
        // direccion world-space de las ultimas filas EN MOVIMIENTO (spd>3) -> robusto, sirve forward y reverse.
        // EndGear = gear de esa fila en movimiento (da la direccion del creep). Si el take casi no se movio,
        // approachDir queda 0 -> alongEnd=0 -> el endgame clava sin reptar (seguro).
        int nn = m_X.Count();
        if (nn > 0) {
            m_EndX = m_X[nn - 1];
            m_EndZ = m_Z[nn - 1];
            int lm = nn - 1;
            while (lm > 0 && m_Spd[lm] < 3.0) lm--;
            int kk = lm - 5;
            if (kk < 0) kk = 0;
            float ax = m_X[lm] - m_X[kk];
            float az = m_Z[lm] - m_Z[kk];
            float amag = Math.Sqrt(ax * ax + az * az);
            if (amag > 0.001) {
                m_ApproachX = ax / amag;
                m_ApproachZ = az / amag;
            }
            m_EndGear = m_Gear[lm];
        }
        m_Loaded = (m_T.Count() > 0);
        BZBusLog.Info("[FrameReplay] cargado " + m_T.Count() + " filas | dur=" + GetDuration() + "s | start=(" + m_StartPos[0] + "," + m_StartPos[2] + ") hdg=" + m_StartHeading + " v=" + m_StartSpeed);
        return m_Loaded;
    }

    bool IsLoaded() { return m_Loaded; }

    // Avanza el cursor a la fila del instante 'elapsed' y cachea sus inputs.
    // false = fin del stream (elapsed paso la ultima fila).
    bool Sample(float elapsed) {
        if (!m_Loaded) return false;
        int n = m_T.Count();
        while (m_Cursor < n - 1 && m_T[m_Cursor + 1] <= elapsed) m_Cursor++;
        if (m_Cursor >= n - 1 && elapsed > m_T[n - 1]) return false;
        m_CurThr  = m_Thr[m_Cursor];
        m_CurBrk  = m_Brk[m_Cursor];
        m_CurStr  = m_Str[m_Cursor];
        m_CurGear = m_Gear[m_Cursor];
        m_CurSpd  = m_Spd[m_Cursor];
        m_CurX    = m_X[m_Cursor];
        m_CurZ    = m_Z[m_Cursor];
        m_CurSlope = m_Slope[m_Cursor];
        m_CurHdgRate = m_HdgRate[m_Cursor];
        m_CurWheelDeg = m_WheelDeg[m_Cursor];
        m_CurCompThr = m_CompThr[m_Cursor];
        m_CurCompStr = m_CompStr[m_Cursor];
        m_CurHeading = m_Hdg[m_Cursor];
        return true;
    }

    void ResetCursor() {
        m_Cursor = 0; m_LastAccumCursor = -1; m_ILCCoastSum = 0; m_ILCCoastN = 0;
        if (m_CrossErr) { for (int i = 0; i < m_CrossErr.Count(); i++) m_CrossErr.Set(i, 0); } // ILC lateral: nuevo transitorio
    }

    // ============================================================================
    //  ILC IN-GAME (autocompensacion universal). El cursor ES la alineacion temporal
    //  exacta -> cero parsing/aligning offline. Cada pasada: mide el residuo y ACUMULA
    //  el inverso en la comp del cursor actual (para la proxima pasada). Ciego a la
    //  causa. Al terminar el stream, Save() persiste la comp al frame_. Iterar converge.
    //  Throttle-only por ahora (signo claro): comp_thr += LTHR*speedErr en coast.
    void AccumComp(float speedErr) {
        if (m_Cursor == m_LastAccumCursor) return;   // un frame se muestrea una vez por pasada
        m_LastAccumCursor = m_Cursor;
        // ZONA DE MANEJO (slope-aware, 2026-07-05): el deficit vive donde el HUMANO maneja -> |slope|<ZONE,
        // en movimiento (spd>5), sin freno fuerte (brk<0.1). INCLUYE los frames de throttle (el deficit esta
        // tanto en coast como en throttle en descenso suave). EXCLUYE: (a) lo empinado (|slope|>=ZONE, donde
        // la gravedad domina y Boris matchea solo -> compensar ahi lo pasaria), (b) el frenar-hasta-parar.
        // El gate viejo de pitch<2 (en BZBusService) TIRABA justo estos frames de descenso suave -> bug.
        if (Math.AbsFloat(m_CurSlope) >= ILC_SLOPE_ZONE || m_CurBrk >= 0.1 || m_CurSpd < 5.0) return;
        m_ILCCoastSum = m_ILCCoastSum + speedErr;
        m_ILCCoastN++;
    }

    //  ILC LATERAL: guarda el error cross-track medido en el cursor actual (Boris vs recorded). En Save se
    //  hornea el inverso en comp_str con lead. Feedforward -> aprende el steering sin ancla (no distorsiona).
    void AccumCrossErr(float crossErr) {
        if (m_Cursor >= 0 && m_Cursor < m_CrossErr.Count()) m_CrossErr.Set(m_Cursor, crossErr);
    }

    //  ILC ESCALAR: calibra UNA constante de coast (no per-frame -> estable, promedia el ruido). El
    //  frame_ persiste la constante como comp_thr UNIFORME en los frames de coast (reusa las cols comp).
    //  Loguea la CONSTANTE = el fingerprint del canal ciego del vehiculo.
    void Save() {
        if (m_Path == "" || !m_Loaded || !m_BaseLine) return;
        // constante ACTUAL = promedio del comp_thr en la ZONA DE MANEJO (uniforme por diseno)
        float curSum = 0; int curN = 0;
        for (int j = 0; j < m_CompThr.Count(); j++) {
            if (Math.AbsFloat(m_Slope[j]) < ILC_SLOPE_ZONE && m_Brk[j] < 0.1 && m_Spd[j] > 5.0) { curSum = curSum + m_CompThr[j]; curN++; }
        }
        float curConst = 0;
        if (curN > 0) curConst = curSum / curN;
        // deficit medido en la zona de manejo esta pasada -> actualiza la constante (SCALAR ILC = estable)
        float avgDef = 0;
        if (m_ILCCoastN > 0) avgDef = m_ILCCoastSum / m_ILCCoastN;
        float newConst = curConst;
        if (Math.AbsFloat(avgDef) >= ILC_DEADBAND) newConst = curConst + ILC_LTHR * avgDef;  // fuera del ruido: ajustar
        if (newConst > ILC_MAXTHR) newConst = ILC_MAXTHR;
        if (newConst < 0) newConst = 0;   // comp negativa es inutil (throttle no baja de 0)
        // escribir: zona de manejo -> newConst uniforme; empinado/freno -> 0 (ahi Boris matchea solo)
        FileHandle f = OpenFile(m_Path, FileMode.WRITE);
        if (!f) { BZBusLog.Err("[ILC] no se pudo escribir " + m_Path); return; }
        string saveHdr = "t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,comp_thr,comp_str\n";
        if (m_ColWheel >= 0) saveHdr = "t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg,comp_thr,comp_str\n";
        FPrint(f, saveHdr);
        // pico del error lateral medido esta pasada (para ver la bola de nieve desinflarse por iteracion)
        float peakCross = 0;
        for (int pc = 0; pc < m_CrossErr.Count(); pc++) { if (Math.AbsFloat(m_CrossErr[pc]) > peakCross) peakCross = Math.AbsFloat(m_CrossErr[pc]); }
        float sumStr = 0; int nStr = 0;
        for (int i = 0; i < m_BaseLine.Count(); i++) {
            float ct = 0;
            // SOLO la ZONA DE MANEJO (|slope|<ZONE, sin freno, en movimiento): NO compensar lo empinado
            // (gravedad domina, Boris matchea) ni el frenar-hasta-parar (agregaria throttle -> overshoot).
            if (Math.AbsFloat(m_Slope[i]) < ILC_SLOPE_ZONE && m_Brk[i] < 0.1 && m_Spd[i] > 5.0) ct = newConst;
            // ACUMULACION IN-MEMORY (2026-07-05): actualizar el comp en RAM, no solo en disco. El frame_ se
            // carga una vez al cargar la ruta y NO se recarga por respawn -> sin esto, curConst y el comp
            // APLICADO se quedan clavados en el valor del route-load y el ILC no converge. Ahora cada pasada
            // (respawn) construye sobre la anterior sin recargar la ruta a mano.
            m_CompThr.Set(i, ct);
            // ILC LATERAL (feedforward, RUMBO-based): comp_str[i] -= LSTR * hdgErr[i+lead]. Boris aprende a
            // apuntar donde apuntaste vos. Signo: hdgErr + = Boris apunta mas a la DERECHA -> comp_str NEGATIVO
            // (steer izq) para volver el rumbo. Un integrador -> estable. Acumula in-memory.
            float cs = m_CompStr[i];
            int li = i + ILC_STR_LEAD;
            if (li < m_CrossErr.Count()) cs = m_CompStr[i] - ILC_LSTR * m_CrossErr[li];
            if (cs > ILC_MAXSTR)  cs = ILC_MAXSTR;
            if (cs < -ILC_MAXSTR) cs = -ILC_MAXSTR;
            m_CompStr.Set(i, cs);
            if (Math.AbsFloat(cs) > 0.0001) { sumStr = sumStr + Math.AbsFloat(cs); nStr++; }
            FPrint(f, m_BaseLine[i] + "," + ct + "," + cs + "\n");
        }
        CloseFile(f);
        float avgStr = 0;
        if (nStr > 0) avgStr = sumStr / nStr;
        BZBusLog.Info("[ILC] CONSTANTE zona-manejo: " + curConst + " -> " + newConst + " (deficit medido=" + avgDef + " km/h en " + m_ILCCoastN + " frames de manejo)");
        BZBusLog.Info("[ILC] LATERAL: pico rumbo-err=" + peakCross + "deg | comp_str avg=" + avgStr + " en " + nStr + " frames");
    }

    float CurThrottle() { return m_CurThr; }
    float CurBrake()    { return m_CurBrk; }
    float CurSteering() { return m_CurStr; }
    int   CurGear()     { return m_CurGear; }
    float CurSpeed()    { return m_CurSpd; }
    float CurX()        { return m_CurX; }
    float CurZ()        { return m_CurZ; }
    float CurCompThr()  { return m_CurCompThr; }
    float CurCompStr()  { return m_CurCompStr; }
    float CurHeading()  { return m_CurHeading; }
    float CurHdgRate()  { return m_CurHdgRate; }   // |cambio de rumbo| local -> detector de curva (atenuacion)
    float CurWheelDeg() { return m_CurWheelDeg; }  // front_wheel_deg grabado (Path 1: target del wheel-tracking)
    bool  HasWheel()    { return m_ColWheel >= 0; } // el frame_ trae la columna de rueda?

    vector GetStartPos()     { return m_StartPos; }
    float  GetStartHeading() { return m_StartHeading; }
    float  GetStartSpeed()   { return m_StartSpeed; }
    int    GetCursor()       { return m_Cursor; }

    // Endpoint (endgame de precision de posicion)
    float EndX()      { return m_EndX; }
    float EndZ()      { return m_EndZ; }
    float ApproachX() { return m_ApproachX; }
    float ApproachZ() { return m_ApproachZ; }
    int   EndGear()   { return m_EndGear; }

    float GetDuration() {
        if (m_T && m_T.Count() > 0) return m_T[m_T.Count() - 1];
        return 0;
    }
    int GetRowCount() {
        if (m_T) return m_T.Count();
        return 0;
    }
}
