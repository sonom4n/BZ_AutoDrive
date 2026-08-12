// ============================================================================
//  BZGearRangeTable - tabla de rangos de marcha por vehiculo
//
//  Carga data/gear_ranges.json (medido empiricamente con NUMPAD 3 / NUMPAD 0)
//  y provee inferencia de gear desde velocidad usando histeresis.
//
//  Reemplaza el uso de `targetGear` grabado del recording: el AI infiere el
//  gear correcto desde la velocidad actual, sin depender de si el operador
//  hizo cambios bien durante la grabacion.
// ============================================================================

class BZGearShiftPoint {
    int    gear;
    float  shiftSpeed;
    float  maxSpeed;
    string name;
}

class BZGearRangeEntry {
    string vehicleClass;
    float  hysteresis;
    string comment;
    ref array<ref BZGearShiftPoint> shiftPoints = new array<ref BZGearShiftPoint>();
}

class BZGearRangeConfig {
    ref array<ref BZGearRangeEntry> Vehicles = new array<ref BZGearRangeEntry>();
}

class BZGearRangeTable {
    private static ref BZGearRangeTable s_Instance;
    private ref BZGearRangeConfig m_Config;
    private bool m_Loaded;

    static BZGearRangeTable GetInstance() {
        if (!s_Instance) {
            s_Instance = new BZGearRangeTable();
            s_Instance.Load();
        }
        return s_Instance;
    }

    void Load() {
        m_Config = new BZGearRangeConfig();
        string path = "BZ_AutoDrive/data/gear_ranges.json";
        if (!FileExist(path)) {
            BZBusLog.Warn("BZGearRangeTable: no se encontro " + path);
            return;
        }
        JsonFileLoader<BZGearRangeConfig>.JsonLoadFile(path, m_Config);
        m_Loaded = true;
        BZBusLog.Info("BZGearRangeTable cargada: " + m_Config.Vehicles.Count() + " vehiculos");
    }

    // Busca la entrada para un vehiculo dado
    BZGearRangeEntry FindEntry(string vehicleClass) {
        if (!m_Config) return null;
        foreach (BZGearRangeEntry entry : m_Config.Vehicles) {
            if (entry.vehicleClass == vehicleClass) return entry;
        }
        return null;
    }

    // Infiere el gear apropiado dada la velocidad actual y el gear actual.
    // currentGear se usa para histeresis: si el speed cae en zona de overlap
    // entre dos gears, mantenemos el actual en vez de oscilar.
    //
    // Returns -1 si el vehiculo no esta en la tabla (caller debe usar fallback).
    int InferGear(string vehicleClass, float speedKmh, int currentGear) {
        BZGearRangeEntry entry = FindEntry(vehicleClass);
        if (!entry) return -1;

        float hyst = entry.hysteresis;
        if (hyst <= 0) hyst = 0.8; // default si JSON tiene 0

        int n = entry.shiftPoints.Count();
        if (n == 0) return -1;

        // Buscar el gear cuyo rango [piso, tope] contiene speedKmh.
        // tope_de_gear_i  = shiftPoints[i].shiftSpeed  (o infinito si es 0 = ultima marcha)
        // piso_de_gear_i  = shiftPoints[i-1].shiftSpeed * hyst  (o 0 si i = 0)

        // Encontrar el rango de gears candidatos (lower y upper)
        int lowerCandidate = entry.shiftPoints[0].gear; // por default FIRST
        int upperCandidate = entry.shiftPoints[0].gear;

        for (int i = 0; i < n; i++) {
            BZGearShiftPoint sp = entry.shiftPoints[i];
            float upper = sp.shiftSpeed;
            float lower = 0;
            if (i > 0) {
                int prevIdx = i - 1;
                BZGearShiftPoint prevSp = entry.shiftPoints[prevIdx];
                lower = prevSp.shiftSpeed * hyst;
            }

            bool isLast = (upper <= 0);
            bool inRange = false;
            if (isLast) {
                if (speedKmh >= lower) inRange = true;
            } else {
                if (speedKmh >= lower && speedKmh <= upper) inRange = true;
            }

            if (inRange) {
                if (i == 0) {
                    lowerCandidate = sp.gear;
                } else if (sp.gear < lowerCandidate) {
                    lowerCandidate = sp.gear;
                }
                if (sp.gear > upperCandidate) {
                    upperCandidate = sp.gear;
                }
            }
        }

        // Histeresis: si el gear actual esta en el rango de candidatos, mantenerlo
        if (currentGear >= lowerCandidate && currentGear <= upperCandidate) {
            return currentGear;
        }

        // Si no, elegir el mas alto candidato (mejor eficiencia de motor)
        return upperCandidate;
    }
}
