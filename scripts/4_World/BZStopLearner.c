// ============================================================================
//  BZStopLearner - motor de learning IN-ENGINE de la PARADA (eje LONGITUDINAL)
// ============================================================================
//  El gemelo longitudinal del BZCorridorLearner. Stanley + el corridor learner
//  cubren lo LATERAL (la linea); esto cubre lo LONGITUDINAL (donde para).
//
//  ASIMETRIA CLAVE: el lateral PERDONA (Stanley corrige cuando quiera); la
//  parada NO (una vez muy rapido muy tarde, la fisica no salva -> la desacel
//  tiene tope). Por eso las REGLAS reactivas fracasaron ("frena mas" choca el
//  limite fisico) y el frenado a paradas debe ser FEEDFORWARD calibrado. Este
//  learner calibra el punto de frenado a la fisica REAL del vehiculo.
//
//  QUE APRENDE: un "brake-point advance" por parada. Boris se pasa O metros ->
//  aprende bias += 0.5*O -> la proxima resta O a la distancia-restante -> frena
//  antes -> para en el punto. Es la distancia de frenado REAL del vehiculo
//  (system-ID del freno, el fingerprint longitudinal). EMA por parada (gain<1)
//  -> converge. Persiste a disco. Sin maquillaje: mide el resting REAL de Boris.
//
//  MEDICION HONESTA: en OnStopFinished, Boris ya se detuvo el stopDuration
//  completo -> se mide su resting position vs el wp de parada, proyectado sobre
//  la direccion de aproximacion = overshoot LONGITUDINAL firmado (+ = se paso).
//  (Es justo lo que el corridor learner DESCARTA con su cap MAX_LATERAL.)
//
//  3 GANCHOS en BZBusService:
//    1. Init()          al spawn del bus     (junto al corridor learner ~:2721)
//    2. GetBias()       en MODO PARKING      (:4161, distRemaining -= GetBias)
//    3. OnStopReached() en OnStopFinished    (:7431, Boris detenido -> medir+aprender)
// ============================================================================

class BZStopLearner {
    static ref BZStopLearner s_Instance;

    // --- tuning ---
    const float STOP_GAIN = 0.5;    // fraccion del overshoot corregida por parada (EMA <1 -> converge)
    const float MAX_BIAS  = 10.0;   // clamp del brake-point advance (seguridad, metros)
    const float DEADBAND  = 0.15;   // overshoot < 15cm = precision suficiente, no tocar (no perseguir ruido)

    // --- estado ---
    private ref array<float> m_Bias;    // brake-point advance APRENDIDO por stop-wp (persiste; se aplica siempre)
    private int    m_WpCount;
    private string m_RouteId;
    private bool   m_Active;

    static BZStopLearner GetInstance() {
        if (!s_Instance) s_Instance = new BZStopLearner();
        return s_Instance;
    }

    void BZStopLearner() {
        m_Bias   = new array<float>();
        m_Active = false;
    }

    // ============================ 1) INIT (al spawn del bus) ============================
    void Init(int wpCount, string routeId) {
        // idempotente: en respawn (misma ruta) NO borrar lo aprendido en RAM. Full-init 1er vez o si cambia la ruta.
        if (m_Active && m_RouteId == routeId && m_WpCount == wpCount) return;
        m_WpCount = wpCount;
        m_RouteId = routeId;
        m_Bias.Clear();
        int i;
        for (i = 0; i < wpCount; i++) m_Bias.Insert(0);
        LoadFromDisk();
        m_Active = true;
        BZBusLog.Info("[STOPLEARN] init: " + wpCount + " wp | ruta " + routeId);
    }

    // ==================== 2) APPLY (en MODO PARKING, :4161) ====================
    //  distRemaining = distToNextStop - STOP_FINAL_RADIUS - GetBias(m_NextStopIndex);
    //  Restar el bias achica distRemaining -> aNeeded = u2/(2*distRemaining) sube antes -> frena mas temprano.
    float GetBias(int stopIdx) {
        if (!m_Active) return 0;
        if (stopIdx < 0 || stopIdx >= m_Bias.Count()) return 0;
        return m_Bias[stopIdx];
    }

    // ============ 3) MEASURE + LEARN + PERSIST (en OnStopFinished, Boris ya detenido) ============
    void OnStopReached(int stopIdx, vector restPos, vector stopPos, vector approachDir) {
        if (!m_Active) return;
        if (stopIdx < 0 || stopIdx >= m_Bias.Count()) return;
        // overshoot LONGITUDINAL firmado = (resting - stop) proyectado sobre la direccion de aproximacion.
        // El hook la pasa desde el targetHeading grabado del wp -> robusto al cluster de wps duplicados
        // del endpoint (donde la tangente entre waypoints daba cero y el learner bailaba sin medir).
        float overshoot = (restPos[0] - stopPos[0]) * approachDir[0] + (restPos[2] - stopPos[2]) * approachDir[2];  // + = se paso el cartel
        BZBusLog.Info("[STOPLEARN] stop " + stopIdx + ": resting " + overshoot + "m del cartel (+ = pasado)");
        if (Math.AbsFloat(overshoot) < DEADBAND) {
            BZBusLog.Info("[STOPLEARN] stop " + stopIdx + ": dentro del deadband -> ok, no toca");
            return;
        }
        float b = m_Bias[stopIdx] + STOP_GAIN * overshoot;   // + overshoot => mas bias => frena antes la proxima
        if (b > MAX_BIAS)  b = MAX_BIAS;
        if (b < -MAX_BIAS) b = -MAX_BIAS;
        m_Bias.Set(stopIdx, b);
        SaveToDisk();
        BZBusLog.Info("[STOPLEARN] stop " + stopIdx + ": overshoot " + overshoot + "m -> bias " + b + "m (frenara antes)");
    }

    void ResetLearning() {
        int i;
        for (i = 0; i < m_WpCount; i++) m_Bias.Set(i, 0);
        SaveToDisk();
        BZBusLog.Info("[STOPLEARN] bias reseteado a cero");
    }

    // ---- persistencia (CSV plano, una linea por wp = el bias en metros) ----
    private string FilePath() { return "$profile:BZ_AutoDrive/" + m_RouteId + "_stopbias.csv"; }

    void SaveToDisk() {
        string path = FilePath();
        FileHandle f = OpenFile(path, FileMode.WRITE);
        if (!f) { BZBusLog.Warn("[STOPLEARN] no se pudo escribir " + path); return; }
        int i;
        for (i = 0; i < m_Bias.Count(); i++) FPrint(f, "" + m_Bias[i] + "\n");
        CloseFile(f);
    }

    void LoadFromDisk() {
        string path = FilePath();
        if (!FileExist(path)) { BZBusLog.Info("[STOPLEARN] sin bias previo (" + m_RouteId + ") - arranca en cero"); return; }
        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh == 0) return;
        int i = 0;
        string line;
        while (FGets(fh, line) >= 0) {
            line.Trim();
            if (line != "" && i < m_Bias.Count()) { m_Bias.Set(i, line.ToFloat()); i++; }
        }
        CloseFile(fh);
        BZBusLog.Info("[STOPLEARN] cargados " + i + " bias previos para " + m_RouteId);
    }

    bool IsActive() { return m_Active; }
}
