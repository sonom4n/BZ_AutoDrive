// ============================================================================
//  BZILCCorrections - lector de correcciones ILC pre-calculadas
// ============================================================================
//  Carga JSON con los offsets de steering por wp_idx (calculados offline por
//  el aggregator) y los expone via GetSteeringFor(wpIdx).
//
//  Flujo:
//    1. Al spawn del bus, BZBusService llama LoadFromDisk() con el routeId.
//    2. En cada Tick, BZBusService consulta GetSteeringFor(wpIdx) y suma el
//       offset al steering calculado por Stanley.
//    3. Despues de cada corrida, el aggregator offline genera un JSON nuevo.
//
//  Si el JSON no existe (primera corrida) o el waypoint no esta cubierto,
//  GetSteeringFor devuelve 0 - comportamiento equivalente a "sin ILC".
// ============================================================================

class BZILCCorrections {
    static ref BZILCCorrections s_Instance;

    private ref array<float> m_SteeringCorr;
    private int    m_Iteration;
    private float  m_LearningRate;
    private string m_RouteId;
    private bool   m_Loaded;

    static BZILCCorrections GetInstance() {
        if (!s_Instance) s_Instance = new BZILCCorrections();
        return s_Instance;
    }

    void BZILCCorrections() {
        m_SteeringCorr = new array<float>();
        m_Loaded = false;
    }

    // Lee JSON desde $profile:BZ_AutoDrive/ILC/<routeId>_corrections.json
    // Devuelve true si cargo OK, false si no existe (primera corrida) o error.
    bool LoadFromDisk(string routeId) {
        m_RouteId = routeId;
        m_Loaded = false;
        m_SteeringCorr.Clear();

        string path = "$profile:BZ_AutoDrive/ILC/" + routeId + "_corrections.json";
        if (!FileExist(path)) {
            BZBusLog.Info("ILC: no hay corrections.json para " + routeId + " (primera corrida o reset)");
            return false;
        }

        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh == 0) {
            BZBusLog.Warn("ILC: no se pudo abrir " + path);
            return false;
        }

        string all = "";
        string line;
        while (FGets(fh, line) >= 0) {
            all += line;
        }
        CloseFile(fh);

        // Parser minimalista: buscamos los campos relevantes
        // (JSON predecible generado por nuestro aggregator)
        m_Iteration    = ParseIntField(all, "iteration_count");
        m_LearningRate = ParseFloatField(all, "learning_rate");

        // Parsear el array de correcciones
        // Estructura: "steering_corrections":[0,0,0.02,...]
        int idx = all.IndexOf("\"steering_corrections\"");
        if (idx < 0) {
            BZBusLog.Warn("ILC: JSON sin steering_corrections array");
            return false;
        }
        int arrStart = all.IndexOfFrom(idx, "[") + 1;
        int arrEnd = all.IndexOfFrom(arrStart, "]");
        if (arrEnd <= arrStart) return false;

        string arrStr = all.Substring(arrStart, arrEnd - arrStart);
        // Split por coma (los numeros JSON son simples)
        ParseFloatArray(arrStr);

        m_Loaded = true;
        BZBusLog.Info("ILC: cargado " + m_SteeringCorr.Count() + " correcciones para " + routeId + " (iter=" + m_Iteration + ")");
        return true;
    }

    // Devuelve el offset de steering para el waypoint dado.
    // 0 si no hay correccion o si el wp esta fuera de rango.
    float GetSteeringFor(int wpIdx) {
        if (!m_Loaded) return 0;
        if (wpIdx < 0 || wpIdx >= m_SteeringCorr.Count()) return 0;
        return m_SteeringCorr[wpIdx];
    }

    bool IsLoaded() { return m_Loaded; }
    int GetIteration() { return m_Iteration; }
    float GetLearningRate() { return m_LearningRate; }

    // ---- Helpers de parsing ----
    private int ParseIntField(string src, string fieldName) {
        string key = "\"" + fieldName + "\":";
        int idx = src.IndexOf(key);
        if (idx < 0) return 0;
        int start = idx + key.Length();
        // Skip whitespace
        while (start < src.Length() && (src.Get(start) == " " || src.Get(start) == "\t")) start++;
        int end = start;
        while (end < src.Length()) {
            string ch = src.Get(end);
            if (ch == "," || ch == "}" || ch == "\n" || ch == " ") break;
            end++;
        }
        if (end <= start) return 0;
        return src.Substring(start, end - start).ToInt();
    }

    private float ParseFloatField(string src, string fieldName) {
        string key = "\"" + fieldName + "\":";
        int idx = src.IndexOf(key);
        if (idx < 0) return 0;
        int start = idx + key.Length();
        while (start < src.Length() && (src.Get(start) == " " || src.Get(start) == "\t")) start++;
        int end = start;
        while (end < src.Length()) {
            string ch = src.Get(end);
            if (ch == "," || ch == "}" || ch == "\n" || ch == " ") break;
            end++;
        }
        if (end <= start) return 0;
        return src.Substring(start, end - start).ToFloat();
    }

    private void ParseFloatArray(string arrStr) {
        // Parser linear, busca tokens entre comas
        int i = 0;
        int start = 0;
        int len = arrStr.Length();
        while (i <= len) {
            bool atEnd = (i == len);
            string ch = "";
            if (!atEnd) ch = arrStr.Get(i);
            if (atEnd || ch == ",") {
                if (i > start) {
                    string tok = arrStr.Substring(start, i - start);
                    tok.Trim();
                    if (tok != "") {
                        m_SteeringCorr.Insert(tok.ToFloat());
                    }
                }
                start = i + 1;
            }
            i++;
        }
    }
}
