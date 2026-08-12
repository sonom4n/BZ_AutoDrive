// ============================================================================
//  BZVehicleEnvelope - aprende el ENVELOPE de direccion del vehiculo desde la
//  demostracion (usuario 2026-07-06: "ensenarle el limite del vehiculo en todos
//  sus aspectos"; "el motor lo expone, nosotros lo extraemos"). El motor mide el
//  understeer REAL del auto, no lo inventa.
//
//    understeer k(v) = L*kappa / tan(front_wheel_grabado)   [invierte delta=atan(L*kappa/k)]
//
//  Todo del lado derecho lo EXPONE el motor: kappa de la linea (posiciones), el
//  volante EJECUTADO (targetFrontWheel = col18, via WheelGetDirection), la velocidad.
//  Extraccion EXACTA, no inferencia. Acumula por bins de velocidad -> mapa k(v)
//  per-vehiculo, persistido. El FF computa el volante justo para SU velocidad sobre
//  la linea limpia -> generaliza a cualquier ruta y (cambiando el mapa) vehiculo.
//  Reemplaza el k=0.90 inventado por el medido (EX05 ~0.50). Ver [[project_vehicle_envelope_learner]].
//
//  Modelo REPLACE idempotente: si la toma trae senal (>=10 muestras de curva) recomputa
//  el mapa y lo persiste; si no, conserva el mapa persistido de una toma previa del veh.
//  Recomputar la misma toma (respawn) da el mismo mapa -> sin doble-conteo.
// ============================================================================
class BZVehicleEnvelope {
    private static ref map<string, ref BZVehicleEnvelope> s_ByClass;

    private const int   NBINS   = 20;    // 0..100 km/h en bins de 5
    private const float BINW    = 5.0;
    private const int   MINSAMP = 3;     // muestras minimas para confiar en un bin
    private const int   NEAR_BINS = 2;   // hasta cuantos bins vecinos (x5 km/h) vale extrapolar el mapa

    private ref array<float> m_KSum;
    private ref array<int>   m_KCnt;
    // ASPECTO 2 - RESPUESTA LONGITUDINAL (2026-07-20): decel de COASTEO a(v), o sea cuanto frena
    // solo el vehiculo al soltar gas y freno. Resume masa + freno motor + rodadura + aero en UNA
    // respuesta medida (no hay que modelar cada factor: "el motor lo expone, nosotros lo extraemos").
    // Sin esto el control suelta el acelerador creyendo que planea hasta el punto y se PLANTA corto:
    // medido en el M3 (1400 kg) 8.5 m antes del endpoint y 5.5 m antes del intercambio, mientras el
    // Sedan (1000 kg) con el mismo codigo clavaba. Ver [[project_vehicle_envelope_learner]].
    // POR PISO (2026-07-20, Sonom4n): la rodadura sobre tierra/campo es MUCHO mayor que sobre asfalto ->
    // el mismo auto planea distinto segun donde este. Y justo los 3 puntos que fallaban (los dos
    // intercambios y el endpoint) NO son asfalto. Binear solo por velocidad promediaba asfalto con
    // tierra y sobrestimaba el planeo ahi. Indice = surf * NBINS + bin, surf: 0=duro, 1=blando.
    private ref array<float> m_CSum;
    private ref array<int>   m_CCnt;
    private string m_Class;

    void BZVehicleEnvelope() {
        m_KSum = new array<float>();
        m_KCnt = new array<int>();
        m_CSum = new array<float>();
        m_CCnt = new array<int>();
        int i;
        for (i = 0; i < NBINS; i++) { m_KSum.Insert(0.0); m_KCnt.Insert(0); }
        for (i = 0; i < NBINS * 2; i++) { m_CSum.Insert(0.0); m_CCnt.Insert(0); }
    }

    static BZVehicleEnvelope Get(string cls) {
        if (!s_ByClass) s_ByClass = new map<string, ref BZVehicleEnvelope>();
        BZVehicleEnvelope e = null;
        if (!s_ByClass.Find(cls, e)) {
            e = new BZVehicleEnvelope();
            e.m_Class = cls;
            e.Load();
            s_ByClass.Insert(cls, e);
        }
        return e;
    }

    private string FilePath() { return "$profile:BZ_AutoDrive_PathLogger\\envelope_" + m_Class + ".txt"; }

    // Extrae el understeer de la toma y (si hay senal) REEMPLAZA el mapa. Se llama al cargar/spawnear.
    void UpdateFromWaypoints(array<ref BZWaypoint> wps, float wheelbase) {
        if (!wps || wheelbase < 0.5) return;
        int n = wps.Count();
        array<float> tSum = new array<float>();
        array<int>   tCnt = new array<int>();
        int b;
        for (b = 0; b < NBINS; b++) { tSum.Insert(0.0); tCnt.Insert(0); }
        int W = 8;
        int total = 0;
        int i;
        for (i = W; i < n - W; i++) {
            vector pa = wps[i - W].GetVector();
            vector pm = wps[i].GetVector();
            vector pb = wps[i + W].GetVector();
            float v1x = pm[0] - pa[0]; float v1z = pm[2] - pa[2];
            float v2x = pb[0] - pm[0]; float v2z = pb[2] - pm[2];
            if (v1x * v1x + v1z * v1z < 0.25) continue;
            if (v2x * v2x + v2z * v2z < 0.25) continue;
            float h1 = Math.Atan2(v1x, v1z);
            float h2 = Math.Atan2(v2x, v2z);
            float dh = h2 - h1;
            while (dh > Math.PI)  dh = dh - 2.0 * Math.PI;
            while (dh < -Math.PI) dh = dh + 2.0 * Math.PI;
            float arc = 0;
            int j;
            for (j = i - W; j < i + W; j++) arc = arc + vector.Distance(wps[j].GetVector(), wps[j + 1].GetVector());
            if (arc < 0.2) continue;
            float kappa = Math.AbsFloat(dh) / arc;
            float fwdeg = Math.AbsFloat(wps[i].targetFrontWheel);
            if (fwdeg < 8.0) continue;      // poca senal de volante
            if (kappa < 0.02) continue;     // recta
            // (probado y DESCARTADO 2026-07-20: filtrar transitorios de volante no movia el k -- 103 vs 13
            //  muestras, misma mediana 0.336. La contaminacion no venia de las correcciones del humano.)
            float fwrad = fwdeg * Math.DEG2RAD;
            float sinfw = Math.Sin(fwrad);
            if (sinfw < 0.05) continue;
            float k = wheelbase * kappa * Math.Cos(fwrad) / sinfw;   // = L*kappa/tan(fw)  (Math.Tan no garantizado)
            if (k < 0.1) continue;
            if (k > 2.0) continue;          // outlier grosero (fase/saturacion)
            float v = wps[i].targetSpeed;
            int bin = (int)(v / BINW);
            if (bin < 0) bin = 0;
            if (bin >= NBINS) bin = NBINS - 1;
            tSum.Set(bin, tSum[bin] + k);
            tCnt.Set(bin, tCnt[bin] + 1);
            total++;
        }
        if (total >= 10) {
            m_KSum = tSum;
            m_KCnt = tCnt;
            Save();
            BZBusLog.Info("[ENVELOPE] " + m_Class + ": mapa k(v) de " + total + " muestras de curva | k@12kmh=" + GetK(12.0) + " (ex k inventado=0.90)");
        }
    }

    // k(v) del mapa aprendido; -1 si no hay data suficiente (el caller usa el default).
    float GetK(float kmh) {
        int bin = (int)(kmh / BINW);
        if (bin < 0) bin = 0;
        if (bin >= NBINS) bin = NBINS - 1;
        if (m_KCnt[bin] >= MINSAMP) return m_KSum[bin] / m_KCnt[bin];
        // NO EXTRAPOLAR A OTRO REGIMEN (2026-07-20, MEDIDO). La busqueda barria TODOS los bins y despues
        // caia a un promedio global: en la toma del M3 las 103 muestras estaban TODAS bajo 40 km/h (el
        // volante grande vive en las maniobras) y esa ruta llega a 146 -> a 80 km/h devolvia el k de una
        // maniobra a 10 (0.30) -> Boris comandaba ~3x el volante que correspondia -> 22.8 m de desvio y
        // zigzag. Un auto no subvira igual a 10 que a 80. Solo confiamos en bins VECINOS (+-10 km/h);
        // fuera de eso devolvemos -1 y el caller usa su default, que es honesto: no lo medimos.
        int d;
        for (d = 1; d <= NEAR_BINS; d++) {
            int lo = bin - d;
            int hi = bin + d;
            if (lo >= 0    && m_KCnt[lo] >= MINSAMP) return m_KSum[lo] / m_KCnt[lo];
            if (hi < NBINS && m_KCnt[hi] >= MINSAMP) return m_KSum[hi] / m_KCnt[hi];
        }
        return -1.0;
    }

    // --- ASPECTO 2: coasteo ---------------------------------------------------------------
    // Una muestra = el vehiculo rodando SIN gas ni freno. El caller filtra ese estado; aca solo
    // descartamos ruido (dt raro, frenadas disfrazadas, marcha atras).
    void SampleCoast(float kmh, float decel, int surf) {
        if (kmh < 2.0) return;
        if (decel <= 0.02 || decel > 6.0) return;
        int bin = (int)(kmh / BINW);
        if (bin < 0) bin = 0;
        if (bin >= NBINS) bin = NBINS - 1;
        if (surf != 1) surf = 0;
        bin = surf * NBINS + bin;
        m_CSum.Set(bin, m_CSum[bin] + decel);
        m_CCnt.Set(bin, m_CCnt[bin] + 1);
        // Persistir cada tanto: lo aprendido manejando sobrevive al respawn y a la proxima toma.
        m_CoastDirty++;
        if (m_CoastDirty >= 10) { m_CoastDirty = 0; Save(); }
    }
    private int m_CoastDirty;

    // decel de coasteo (m/s2) medida a esa velocidad EN ESE PISO; -1 si todavia no aprendio.
    // Cae al bin vecino del MISMO piso: nunca mezcla asfalto con tierra, que es el punto.
    float GetCoastDecel(float kmh, int surf) {
        int bin = (int)(kmh / BINW);
        if (bin < 0) bin = 0;
        if (bin >= NBINS) bin = NBINS - 1;
        if (surf != 1) surf = 0;
        int base = surf * NBINS;
        if (m_CCnt[base + bin] >= MINSAMP) return m_CSum[base + bin] / m_CCnt[base + bin];
        // mismo criterio que k(v): no extrapolar a un regimen que no medimos (ver GetK).
        int d;
        for (d = 1; d <= NEAR_BINS; d++) {
            int lo = bin - d;
            int hi = bin + d;
            if (lo >= 0    && m_CCnt[base + lo] >= MINSAMP) return m_CSum[base + lo] / m_CCnt[base + lo];
            if (hi < NBINS && m_CCnt[base + hi] >= MINSAMP) return m_CSum[base + hi] / m_CCnt[base + hi];
        }
        return -1.0;
    }

    // LA PREGUNTA QUE EL CONTROL NO SABIA RESPONDER: si suelto TODO a esta velocidad y sobre ESTE
    // piso, cuantos metros ruedo antes de plantarme. -1 = todavia no hay data (el caller no interviene).
    float GlideDistanceM(float kmh, int surf) {
        float a = GetCoastDecel(kmh, surf);
        if (a <= 0.0) return -1.0;
        float v = kmh / 3.6;
        return (v * v) / (2.0 * a);
    }

    // --- ASPECTO 3: ZONA MUERTA / BREAKAWAY (2026-07-20) --------------------------------------------
    // El acelerador MINIMO que efectivamente pone en movimiento a ESTE vehiculo, por piso. Medido, no
    // inventado: mis 4 intentos a ojo (0.20 / 0.22 / 0.30 / 0.40) fallaron todos, y la literatura avisa
    // que "la calidad de la compensacion de friccion esta limitada por la fidelidad del modelo, no por el
    // ajuste de ganancias". La rampa que lo mide ES tambien la que lo mueve (identificacion clasica de
    // breakaway). El breakaway NO es constante: depende del piso y del reposo, por eso se promedia.
    private ref array<float> m_BkSum;
    private ref array<int>   m_BkCnt;

    void SampleBreakaway(float thr, int surf) {
        if (thr <= 0.02 || thr > 1.0) return;
        if (surf != 1) surf = 0;
        if (!m_BkSum) { m_BkSum = new array<float>(); m_BkCnt = new array<int>(); m_BkSum.Insert(0.0); m_BkSum.Insert(0.0); m_BkCnt.Insert(0); m_BkCnt.Insert(0); }
        m_BkSum.Set(surf, m_BkSum[surf] + thr);
        m_BkCnt.Set(surf, m_BkCnt[surf] + 1);
        Save();
        BZBusLog.Info("[ZONAMUERTA] " + m_Class + " piso=" + surf + ": despego con thr=" + thr + " | promedio=" + (m_BkSum[surf] / m_BkCnt[surf]) + " (n=" + m_BkCnt[surf] + ")");
    }

    // acelerador de despegue aprendido para ese piso; -1 si todavia no lo midio.
    float GetBreakaway(int surf) {
        if (!m_BkCnt) return -1.0;
        if (surf != 1) surf = 0;
        if (m_BkCnt[surf] >= 1) return m_BkSum[surf] / m_BkCnt[surf];
        int other = 1 - surf;
        if (m_BkCnt[other] >= 1) return m_BkSum[other] / m_BkCnt[other];
        return -1.0;
    }

    void Save() {
        FileHandle f = OpenFile(FilePath(), FileMode.WRITE);
        if (!f) return;
        int b;
        for (b = 0; b < NBINS; b++) FPrint(f, "" + m_KCnt[b] + "," + m_KSum[b] + "," + m_CCnt[b] + "," + m_CSum[b] + "," + m_CCnt[NBINS + b] + "," + m_CSum[NBINS + b] + "\n");
        // linea extra: zona muerta / breakaway por piso (aspecto 3). Los mapas viejos no la traen y cargan igual.
        if (m_BkCnt) FPrint(f, "BK," + m_BkCnt[0] + "," + m_BkSum[0] + "," + m_BkCnt[1] + "," + m_BkSum[1] + "\n");
        CloseFile(f);
    }

    void Load() {
        string path = FilePath();
        if (!FileExist(path)) return;
        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh == 0) return;
        int b = 0;
        string line;
        while (FGets(fh, line) >= 0) {
            line.Trim();
            if (line.IndexOf("BK,") == 0) {
                array<string> tk = new array<string>();
                line.Split(",", tk);
                if (tk.Count() >= 5) {
                    if (!m_BkSum) { m_BkSum = new array<float>(); m_BkCnt = new array<int>(); m_BkSum.Insert(0.0); m_BkSum.Insert(0.0); m_BkCnt.Insert(0); m_BkCnt.Insert(0); }
                    m_BkCnt.Set(0, tk[1].ToInt()); m_BkSum.Set(0, tk[2].ToFloat());
                    m_BkCnt.Set(1, tk[3].ToInt()); m_BkSum.Set(1, tk[4].ToFloat());
                }
                continue;
            }
            if (b >= NBINS) continue;
            if (line != "") {
                array<string> tok = new array<string>();
                line.Split(",", tok);
                if (tok.Count() >= 2) { m_KCnt.Set(b, tok[0].ToInt()); m_KSum.Set(b, tok[1].ToFloat()); }
                // 4 campos = coasteo piso duro; 6 = ademas piso blando. Mapas viejos de 2 siguen cargando.
                if (tok.Count() >= 4) { m_CCnt.Set(b, tok[2].ToInt()); m_CSum.Set(b, tok[3].ToFloat()); }
                if (tok.Count() >= 6) { m_CCnt.Set(NBINS + b, tok[4].ToInt()); m_CSum.Set(NBINS + b, tok[5].ToFloat()); }
            }
            b++;
        }
        CloseFile(fh);
    }
}
