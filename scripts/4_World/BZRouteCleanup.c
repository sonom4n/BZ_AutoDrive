// ============================================================================
//  BZRouteCleanup - barre wrecks a lo largo de una ruta definida
//
//  Fuente de la ruta (en orden de preferencia):
//    1) m_Config.Anchors        - lista [x, z] en wrecks_cleanup.json
//    2) BZBusService.GetConfig().Waypoints - fallback si Anchors esta vacio
//
//  Para cada par de puntos consecutivos interpola cada SweepStride metros y
//  en cada punto consulta GetObjectsAtPosition3D(radius). Suprime + borra los
//  objetos cuyo classname matchea algun pattern.
//
//  Suppress + ObjectDelete:
//   - Suppress (DayZ Editor Loader) -> oculta visualmente, persistente
//   - ObjectDelete                  -> saca el collider asi el bus pasa
// ============================================================================

class BZRouteCleanup {
    private static ref BZRouteCleanup s_Instance;

    static const string CONFIG_PATH  = "BZ_AutoDrive/data/wrecks_cleanup.json";
    static const int    START_DELAY  = 20000;

    private ref BZCleanupConfig m_Config;

    static BZRouteCleanup GetInstance() {
        if (!s_Instance) s_Instance = new BZRouteCleanup();
        return s_Instance;
    }

    void Init() {
        if (!GetGame().IsServer()) return;

        BZBusLog.Info("BZRouteCleanup: cargando " + CONFIG_PATH);
        m_Config = new BZCleanupConfig();
        JsonFileLoader<BZCleanupConfig>.JsonLoadFile(CONFIG_PATH, m_Config);

        if (!m_Config || !m_Config.Patterns || m_Config.Patterns.Count() == 0) {
            BZBusLog.Warn("BZRouteCleanup: sin patterns configurados, abortando");
            return;
        }

        BZBusLog.Info("BZRouteCleanup: " + m_Config.Patterns.Count() + " patterns, radius=" + m_Config.SweepRadius + ", stride=" + m_Config.SweepStride + ". Ejecutando en " + (START_DELAY / 1000) + "s...");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.SweepRoute, START_DELAY, false);
    }

    private void SweepRoute() {
        array<vector> path = BuildPath();
        if (path.Count() < 2) {
            BZBusLog.Warn("BZRouteCleanup: ruta insuficiente (<2 puntos)");
            return;
        }

        int suppressed = 0;
        int points = 0;
        float stride = m_Config.SweepStride;
        if (stride <= 0) stride = 20.0;

        for (int i = 0; i < path.Count() - 1; i++) {
            vector pa = path[i];
            vector pb = path[i + 1];
            float dist = vector.Distance(pa, pb);
            int steps = (int)Math.Ceil(dist / stride);
            if (steps < 1) steps = 1;

            for (int s = 0; s <= steps; s++) {
                float t = (float)s / (float)steps;
                vector pt = pa + (pb - pa) * t;
                suppressed += SweepAt(pt);
                points++;
            }
        }

        BZBusLog.Info("BZRouteCleanup: " + suppressed + " wrecks suprimidos en " + points + " puntos sobre " + path.Count() + " nodos");
    }

    private array<vector> BuildPath() {
        array<vector> path = new array<vector>();

        // Preferencia 1: anchors del JSON propio
        if (m_Config.Anchors && m_Config.Anchors.Count() >= 2) {
            foreach (BZAnchor a : m_Config.Anchors) {
                if (!a) continue;
                float y = GetGame().SurfaceY(a.x, a.z);
                path.Insert(Vector(a.x, y, a.z));
            }
            BZBusLog.Info("BZRouteCleanup: usando " + path.Count() + " anchors del JSON");
            return path;
        }

        // Preferencia 2: waypoints del bus
        BZBusRouteConfig route = BZBusService.GetInstance().GetConfig();
        if (route && route.Waypoints) {
            foreach (BZWaypoint wp : route.Waypoints) {
                if (!wp) continue;
                path.Insert(wp.GetVector());
            }
            BZBusLog.Info("BZRouteCleanup: usando " + path.Count() + " waypoints del bus (fallback)");
        }

        return path;
    }

#ifndef DabsFramework
    // sin Dabs Framework no hay SuppressedObjectManager: el barrido borra en el server sin la supresion de Dabs, y los
    // clientes pueden seguir viendo el objeto. Se dice una vez.
    private static bool s_AvisoSinDabs;
    private void AvisarSinDabs() {
        if (s_AvisoSinDabs) return;
        s_AvisoSinDabs = true;
        BZBusLog.Warn("[BZ AutoDrive] Dabs Framework not loaded: route cleanup deletes objects on the server only (clients may still see them)");
    }
#endif

    private int SweepAt(vector center) {
        array<Object>    objects = new array<Object>;
        array<CargoBase> proxies = new array<CargoBase>;
        GetGame().GetObjectsAtPosition3D(center, m_Config.SweepRadius, objects, proxies);

        int count = 0;
        foreach (Object obj : objects) {
            if (!obj) continue;
            if (Matches(obj.GetType())) {
#ifndef DAYZ_1_29
                if (BZRetiro.Ocupantes(obj) > 0) continue;   // 1.30: nunca un vehiculo con alguien sentado
#endif
#ifdef DabsFramework
                GetDayZGame().GetSuppressedObjectManager().Suppress(obj);
#else
                AvisarSinDabs();
#endif
                GetGame().ObjectDelete(obj);
                count++;
            }
        }
        return count;
    }

    private bool Matches(string classname) {
        foreach (string p : m_Config.Patterns) {
            if (p && classname.IndexOf(p) >= 0)
                return true;
        }
        return false;
    }
}
