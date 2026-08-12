// ============================================================================
//  BZCorridorLearner - motor de learning IN-ENGINE del corredor (lateral, GEOMETRIA)
// ============================================================================
//  v2 (2026-07-06): pre-distorsiona la GEOMETRIA de los waypoints, NO el center-offset.
//  Por que: aplicar el shift via centerOffset hace que Boris maneje PARALELO al
//  corredor (curva concentrica = MISMA curvatura) -> su corte (problema de
//  curvatura) NO cambia. Mover los waypoints SI cambia la curvatura del camino que
//  Boris sigue -> reduce el corte. Es lo que hacia el converter offline (0.315->0.13).
//
//  MEDICION HONESTA: el cross-track de Boris se mide vs el BASE PRISTINO (no vs el
//  corredor ya movido), asi el error apunta al TARGET real. Sin suavizar, sin cap.
//
//  Guarda las posiciones pristinas como base; en cada BakeShaping mueve
//  pos = base + shift*perp (desde el base -> sin drift). NO mueve las paradas
//  (isStop) -> el stop-learner las maneja y el target de parada queda fijo.
//
//  3 GANCHOS en BZBusService:
//    1. Init(wpCount, routeId, Waypoints)  al spawn (~:2721)
//    2. Observe(wpIdx, busPos)             en DriveTowards tras ComputeCorridorInfo (~:3726)
//    3. OnLapComplete()                    en AdvanceWaypoint fin-de-ruta (~:7452)
//  (Ya NO hay gancho de aplicacion en Stanley: la pre-distorsion vive en la geometria.)
// ============================================================================

class BZCorridorLearner {
    static ref BZCorridorLearner s_Instance;

    const float SHAPE_GAIN     = 0.0;    // REFINAMIENTO REACTIVO DE GEOMETRIA DESACTIVADO: mover la geometria por-pasada
                                         // DIVERGE siempre (Boris responde impredecible, oscila). El PLAN estatico (computado,
                                         // gran escala) es la respuesta estable. Para mejorar: mejor MODELO de cut en el plan, no ensayo.
    const int   LAPS_PER_SHAPE = 1;      // hornea cada vuelta (el shift persiste -> robusto a recargas)
    const float MAX_SHIFT      = 3.0;    // clamp de seguridad de la pre-distorsion (metros)
    // "LO BIEN HECHO QUEDA APRENDIDO" (usuario 2026-07-06): error < esto -> el wp esta DOMINADO,
    // su leccion se CONGELA (no perseguir ruido: shifts ruidosos por-wp ARRUGAN la polilinea --
    // heading=derivada amplifica ruido -> Boris empeoraba en TODA la ruta, avg 0.32->0.44).
    // El ojo del learning se concentra solo donde el error persiste.
    const float SHIFT_DEADBAND = 0.15;
    // === ENSEÑANZA DE VELOCIDAD (la leccion de la CURVA, usuario 2026-07-06) ===
    // "Esto es una curva: aca los vehiculos DESACELERAN, como lo hace la toma, ves?"
    // Donde el corte lateral PERSISTE en zona curva, el learner baja la REFERENCIA de velocidad
    // (dato, no control): a menor v el Stanley vanilla de Boris tiene mas autoridad (atan(K*off/v))
    // -> entra a su ritmo y sigue el arco. Integrador: sigue bajando mientras el corte persista.
    const float SPD_GAIN     = 0.0;      // REACTIVO DESACTIVADO: la velocidad la computa el PLANIFICADOR (predictivo, gran escala), no el ensayo-error per-wp que divergia. El learner solo refina la geometria.
    const float SPD_MAX      = 10.0;     // tope absoluto de reduccion (km/h)
    const float SPD_FLOOR    = 4.0;      // piso de la referencia (km/h) - gateo minimo en curva cerrada
    const float SPD_DEADBAND = 0.30;     // corte menor: lo maneja el shift geometrico solo (0.20 ensenaba de mas: 259 wp)
    const float CURV_TH      = 0.04;     // rad/+-6wp: arriba de esto la zona ES curva DE VERDAD (EX05: pico 0.107; suaves ~0.02)
    const int   SPD_LEAD     = 4;        // wps de anticipacion (frenar ANTES de la curva, como el humano)

    private ref array<ref BZWaypoint> m_Wps;   // waypoints VIVOS (ref) para mover pos
    private ref array<float> m_BaseX;          // posiciones PRISTINAS (base, para no driftear)
    private ref array<float> m_BaseZ;
    private ref array<float> m_PerpX;          // perpendicular unit (izquierda) del base
    private ref array<float> m_PerpZ;
    private ref array<float> m_Shift;          // shift lateral ACUMULADO por wp (persiste)
    private ref array<float> m_SumErr;         // suma del cross-track (vs base) por wp en el batch
    private ref array<int>   m_Cnt;
    private ref array<float> m_BaseSpd;        // targetSpeed PRISTINO de la toma (la demostracion)
    private ref array<float> m_SpdDelta;       // reduccion de velocidad ENSENADA por wp (km/h, persiste)
    private ref array<float> m_Curv;           // curvatura |rad| del base por wp (clasifica zona CURVA)
    private int    m_WpCount;
    private int    m_LastObserved;
    private int    m_LapsInBatch;
    private string m_RouteId;
    private bool   m_Active;

    static BZCorridorLearner GetInstance() {
        if (!s_Instance) s_Instance = new BZCorridorLearner();
        return s_Instance;
    }

    void BZCorridorLearner() {
        m_BaseX = new array<float>(); m_BaseZ = new array<float>();
        m_PerpX = new array<float>(); m_PerpZ = new array<float>();
        m_Shift = new array<float>(); m_SumErr = new array<float>(); m_Cnt = new array<int>();
        m_BaseSpd = new array<float>(); m_SpdDelta = new array<float>(); m_Curv = new array<float>();
        m_Active = false;
    }

    // ============================ 1) INIT (al spawn del bus) ============================
    void Init(int wpCount, string routeId, array<ref BZWaypoint> wps) {
        if (m_Active && m_RouteId == routeId && m_WpCount == wpCount) {
            // idempotente (respawn no resetea el aprendizaje) PERO re-bindear SIEMPRE: en un reload
            // el array de waypoints es NUEVO -> sin esto el learner mutaria objetos muertos (silencioso).
            m_Wps = wps;
            ApplyGeometry();
            return;
        }
        m_WpCount = wpCount;
        m_RouteId = routeId;
        m_Wps = wps;
        m_LapsInBatch  = 0;
        m_LastObserved = -1;
        m_BaseX.Clear(); m_BaseZ.Clear(); m_PerpX.Clear(); m_PerpZ.Clear();
        m_Shift.Clear(); m_SumErr.Clear(); m_Cnt.Clear();
        m_BaseSpd.Clear(); m_SpdDelta.Clear(); m_Curv.Clear();
        int i;
        for (i = 0; i < wpCount; i++) {
            vector p = wps[i].GetVector();
            m_BaseX.Insert(p[0]); m_BaseZ.Insert(p[2]);
            m_PerpX.Insert(0); m_PerpZ.Insert(0);
            m_Shift.Insert(0); m_SumErr.Insert(0); m_Cnt.Insert(0);
            m_BaseSpd.Insert(wps[i].targetSpeed);      // la demostracion: como lo hizo la toma
            m_SpdDelta.Insert(0); m_Curv.Insert(0);
        }
        // clasificar zonas: curvatura |rad| sobre ventana +-6 wp del BASE (curva vs recta)
        for (i = 0; i < wpCount; i++) {
            int ca = i - 6; if (ca < 0) ca = 0;
            int cb = i + 6; if (cb >= wpCount) cb = wpCount - 1;
            float vax = m_BaseX[i] - m_BaseX[ca];  float vaz = m_BaseZ[i] - m_BaseZ[ca];
            float vbx = m_BaseX[cb] - m_BaseX[i];  float vbz = m_BaseZ[cb] - m_BaseZ[i];
            if (vax * vax + vaz * vaz < 0.01 || vbx * vbx + vbz * vbz < 0.01) continue;   // cluster parado
            float h1 = Math.Atan2(vax, vaz);
            float h2 = Math.Atan2(vbx, vbz);
            float dh = h2 - h1;
            while (dh > Math.PI)  dh = dh - 2 * Math.PI;
            while (dh < -Math.PI) dh = dh + 2 * Math.PI;
            m_Curv.Set(i, Math.AbsFloat(dh));
        }
        for (i = 0; i < wpCount; i++) {
            int a = i - 1; if (a < 0) a = 0;
            int b = i + 1; if (b >= wpCount) b = wpCount - 1;
            float tx = m_BaseX[b] - m_BaseX[a];
            float tz = m_BaseZ[b] - m_BaseZ[a];
            float m = Math.Sqrt(tx * tx + tz * tz); if (m < 0.001) m = 1;
            m_PerpX.Set(i, -tz / m);
            m_PerpZ.Set(i, tx / m);
        }
        if (!LoadFromDisk()) {
            // PRIMER VUELO: no hay nada aprendido -> el PLANIFICADOR siembra la hoja de ruta (el prior computado).
            // Boris ya se sube CON un plan (cut geometrico + freno anticipado); el learner refina el residuo.
            BZRoutePlanner.SeedPlan(m_Wps, m_Shift, m_SpdDelta);
        }
        ApplyGeometry();     // aplica el plan (o lo aprendido/refinado) -> Boris arranca con su hoja de vuelo
        m_Active = true;
        BZBusLog.Info("[LEARN] init geo: " + wpCount + " wp | ruta " + routeId);
    }

    // ==================== 2) OBSERVE (cross-track vs el BASE PRISTINO) ====================
    void Observe(int wpIdx, vector busPos) {
        if (!m_Active) return;
        if (wpIdx < 0 || wpIdx >= m_WpCount) return;
        int from = m_LastObserved + 1;
        if (from < 0) from = 0;
        if (from > wpIdx) from = wpIdx;
        int w;
        for (w = from; w <= wpIdx; w++) {
            float cross = (busPos[0] - m_BaseX[w]) * m_PerpX[w] + (busPos[2] - m_BaseZ[w]) * m_PerpZ[w];
            m_SumErr.Set(w, m_SumErr[w] + cross);
            m_Cnt.Set(w, m_Cnt[w] + 1);
        }
        m_LastObserved = wpIdx;
    }

    // ==================== 3) LAP COMPLETE ====================
    void OnLapComplete() {
        if (!m_Active) return;
        m_LapsInBatch++;
        m_LastObserved = -1;
        BZBusLog.Info("[LEARN] vuelta " + m_LapsInBatch + "/" + LAPS_PER_SHAPE + " acumulada");
        if (m_LapsInBatch >= LAPS_PER_SHAPE) BakeShaping();
    }

    void BakeShaping() {
        float sumAbs = 0; float peak = 0; int peakAt = 0; int nn = 0;
        ref array<float> wantSpd = new array<float>();
        int i;
        for (i = 0; i < m_WpCount; i++) wantSpd.Insert(0);
        for (i = 0; i < m_WpCount; i++) {
            if (m_Cnt[i] < 1) continue;
            float avg = m_SumErr[i] / m_Cnt[i];              // error SISTEMATICO vs base (ruido cancelado)
            if (Math.AbsFloat(avg) >= SHIFT_DEADBAND) {      // dominado? -> leccion CONGELADA, no perseguir ruido
                float s = m_Shift[i] - SHAPE_GAIN * avg;     // reference-shaping: mueve la geometria OPUESTO al corte
                if (s > MAX_SHIFT)  s = MAX_SHIFT;
                if (s < -MAX_SHIFT) s = -MAX_SHIFT;
                m_Shift.Set(i, s);
            }
            // LECCION DE CURVA: donde el corte persiste EN ZONA CURVA, pedir desaceleracion
            if (m_Curv[i] > CURV_TH && Math.AbsFloat(avg) > SPD_DEADBAND) wantSpd.Set(i, SPD_GAIN * Math.AbsFloat(avg));
            sumAbs = sumAbs + Math.AbsFloat(avg);
            if (Math.AbsFloat(avg) > peak) { peak = Math.AbsFloat(avg); peakAt = i; }
            nn++;
        }
        // integrar la desaceleracion con ANTICIPACION (frenar ANTES de la curva, como el humano):
        // cada wp toma el pedido maximo de los proximos SPD_LEAD wps. Integrador con topes.
        int spdWps = 0; float spdMax = 0;
        for (i = 0; i < m_WpCount; i++) {
            float w = 0;
            int lb = i + SPD_LEAD; if (lb >= m_WpCount) lb = m_WpCount - 1;
            int k;
            for (k = i; k <= lb; k++) { if (wantSpd[k] > w) w = wantSpd[k]; }
            if (w <= 0) continue;
            float d = m_SpdDelta[i] + w;                     // sigue bajando mientras el corte persista
            if (d > SPD_MAX) d = SPD_MAX;
            float halfBase = m_BaseSpd[i] * 0.5;
            if (d > halfBase) d = halfBase;                  // nunca menos de la mitad de lo que hizo la toma
            m_SpdDelta.Set(i, d);
            spdWps++; if (d > spdMax) spdMax = d;
        }
        int j;
        for (j = 0; j < m_WpCount; j++) { m_SumErr.Set(j, 0); m_Cnt.Set(j, 0); }
        m_LapsInBatch = 0;
        ApplyGeometry();
        SaveToDisk();
        float avgAll = 0; if (nn > 0) avgAll = sumAbs / nn;
        BZBusLog.Info("[LEARN] SHAPE geo: error sistematico avg=" + avgAll + "m | PICO=" + peak + "m @ wp " + peakAt + " (nn=" + nn + ")");
        if (spdWps > 0) BZBusLog.Info("[LEARN] ENSENANZA curva: " + spdWps + " wp desacelerados (max -" + spdMax + " km/h vs la toma)");
    }

    // mueve la geometria: pos = base + shift*perp (desde el base -> sin drift). NO mueve las paradas.
    void ApplyGeometry() {
        if (!m_Wps) return;
        float maxDisp = 0; int maxAt = 0;
        int i;
        for (i = 0; i < m_WpCount; i++) {
            if (m_Wps[i].isStop) continue;                   // paradas fijas (las maneja el stop-learner)
            m_Wps[i].SetPos(m_BaseX[i] + m_Shift[i] * m_PerpX[i], m_BaseZ[i] + m_Shift[i] * m_PerpZ[i]);
            // referencia de velocidad ENSENADA (dato, no control): solo BAJA respecto de la toma.
            // delta=0 -> restaura el base (la demostracion pura). Solo cruise normal.
            if (m_Wps[i].mode == "normal" || m_Wps[i].mode == "") {
                float sp = m_BaseSpd[i];                     // delta=0 -> demostracion pura (restaura)
                if (m_SpdDelta[i] > 0) {
                    sp = m_BaseSpd[i] - m_SpdDelta[i];
                    if (sp < SPD_FLOOR) sp = SPD_FLOOR;      // piso SOLO con leccion activa (no sube un base=0 de cola)
                }
                m_Wps[i].targetSpeed = sp;
            }
            if (Math.AbsFloat(m_Shift[i]) > maxDisp) { maxDisp = Math.AbsFloat(m_Shift[i]); maxAt = i; }
        }
        // read-back: confirmar que la mutacion tomo efecto (desplazamiento REAL ~ shift esperado)
        if (maxDisp > 0.01) {
            vector chk = m_Wps[maxAt].GetVector();
            float dx = chk[0] - m_BaseX[maxAt];
            float dz = chk[2] - m_BaseZ[maxAt];
            BZBusLog.Info("[LEARN] ApplyGeometry: shift max=" + maxDisp + "m @ wp " + maxAt + " -> desplazamiento REAL=" + Math.Sqrt(dx * dx + dz * dz) + "m");
        }
    }

    void ForceShape() { if (m_Active && m_LapsInBatch > 0) BakeShaping(); }

    void ResetLearning() {
        int i;
        for (i = 0; i < m_WpCount; i++) { m_Shift.Set(i, 0); m_SumErr.Set(i, 0); m_Cnt.Set(i, 0); m_SpdDelta.Set(i, 0); }
        m_LapsInBatch = 0; m_LastObserved = -1;
        ApplyGeometry(); SaveToDisk();
        BZBusLog.Info("[LEARN] learning reseteado (geo a base)");
    }

    private string FilePath() { return "$profile:BZ_AutoDrive/" + m_RouteId + "_learnshift.csv"; }

    void SaveToDisk() {
        string path = FilePath();
        FileHandle f = OpenFile(path, FileMode.WRITE);
        if (!f) { BZBusLog.Warn("[LEARN] no se pudo escribir " + path); return; }
        int i;
        for (i = 0; i < m_Shift.Count(); i++) FPrint(f, "" + m_Shift[i] + "," + m_SpdDelta[i] + "\n");
        CloseFile(f);
    }

    bool LoadFromDisk() {
        string path = FilePath();
        if (!FileExist(path)) { BZBusLog.Info("[LEARN] sin plan previo (" + m_RouteId + ") -> el planificador lo va a sembrar"); return false; }
        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh == 0) return false;
        int i = 0; string line;
        while (FGets(fh, line) >= 0) {
            line.Trim();
            if (line != "" && i < m_Shift.Count()) {
                array<string> tok = new array<string>();
                line.Split(",", tok);
                if (tok.Count() >= 1) m_Shift.Set(i, tok[0].ToFloat());
                if (tok.Count() >= 2) m_SpdDelta.Set(i, tok[1].ToFloat());
                i++;
            }
        }
        CloseFile(fh);
        BZBusLog.Info("[LEARN] cargados " + i + " wp (plan + refinamiento) previos");
        return true;
    }

    bool IsActive() { return m_Active; }
}
