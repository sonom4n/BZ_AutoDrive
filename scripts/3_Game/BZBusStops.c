// ============================================================================
//  BZBusStops - lista de paradas fisicas del bus (3_Game)
//
//  Carga data/bus_stops.json al primer GetInstance(). Cliente la usa para
//  detectar proximidad del player a una parada y disparar la UI; el server
//  podria usarla a futuro como anchor de waypoints sin tocar el JSON del
//  profile.
// ============================================================================

class BZBusStopAnchor {
    string name;
    float  x;
    float  y;
    float  z;
    float  orient;
    float  uiRadius;

    vector GetVector() { return Vector(x, y, z); }
}

class BZBusStopsConfig {
    ref array<ref BZBusStopAnchor> Stops = new array<ref BZBusStopAnchor>();
}

class BZBusStops {
    static const string CONFIG_PATH = "BZ_AutoDrive/data/bus_stops.json";

    private static ref BZBusStops s_Instance;
    private ref BZBusStopsConfig  m_Config;

    static BZBusStops GetInstance() {
        if (!s_Instance) {
            s_Instance = new BZBusStops();
            s_Instance.Load();
        }
        return s_Instance;
    }

    private void Load() {
        m_Config = new BZBusStopsConfig();
        JsonFileLoader<BZBusStopsConfig>.JsonLoadFile(CONFIG_PATH, m_Config);
        int n = 0;
        if (m_Config && m_Config.Stops) n = m_Config.Stops.Count();
        BZBusLog.Info("BZBusStops: " + n + " paradas cargadas");
    }

    BZBusStopsConfig GetConfig() {
        return m_Config;
    }

    // Devuelve la parada cuyo uiRadius contiene a playerPos, o null si ninguna.
    BZBusStopAnchor FindNearby(vector playerPos) {
        if (!m_Config || !m_Config.Stops) return null;
        foreach (BZBusStopAnchor s : m_Config.Stops) {
            if (!s) continue;
            float r = s.uiRadius;
            if (r <= 0) r = 5.0;
            if (vector.Distance(playerPos, s.GetVector()) <= r)
                return s;
        }
        return null;
    }
}
