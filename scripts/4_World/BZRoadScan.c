// ============================================================================
//  BZRoadScan - SONDA DE OBJETOS DE CARRETERA.
//  v2 (2026-07-07): GetDebugName() revela el MODELO cuando GetType()=="" (escenario).
//  v3 MAPA-COMPLETO (2026-07-08): scan de TODO el mapa (no solo un cuadro), FRAME-SPREAD
//    (un batch de probes por frame via CallLater -> NO bloquea el server) y filtra road parts
//    INLINE (descarta arboles/casas -> CSV chico). Dedup OFFLINE (Python) por posicion.
//    Para validar nuestro grafo/corredor contra la red vial REAL entera. [[project_map_ui_vision]]
// ============================================================================
class BZRoadScan {
    private static bool s_Done = false;

    // --- v2: scan sincrono de un cuadro chico (legacy, para la interseccion) ---
    static void ScanOnce(vector center, float half, float step, float radius) {
        if (s_Done) return;
        s_Done = true;
        if (step <= 0)   step = 8.0;
        if (radius <= 0) radius = 6.0;

        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        FileHandle fo = OpenFile(dir + "roadscan_objects.csv", FileMode.WRITE);
        if (!fo) { BZBusLog.Err("[ROADSCAN] no se pudo crear roadscan_objects.csv"); return; }
        FPrint(fo, "type,scenery,building,x,z,yaw,bw,bl,debug\n");

        array<Object>    seen = new array<Object>;
        array<Object>    objs = new array<Object>;
        array<CargoBase> prox = new array<CargoBase>;
        int nUnique = 0;
        string buf = "";

        float x = center[0] - half;
        while (x <= center[0] + half) {
            float z = center[2] - half;
            while (z <= center[2] + half) {
                float y = GetGame().SurfaceY(x, z);
                objs.Clear(); prox.Clear();
                GetGame().GetObjectsAtPosition3D(Vector(x, y, z), radius, objs, prox);
                foreach (Object obj : objs) {
                    if (!obj) continue;
                    if (seen.Find(obj) >= 0) continue;
                    seen.Insert(obj);
                    nUnique++;
                    string t = obj.GetType();
                    int sc = 0; if (obj.IsScenery())  sc = 1;
                    int bd = 0; if (obj.IsBuilding()) bd = 1;
                    vector p = obj.GetPosition();
                    vector o = obj.GetOrientation();
                    float bw = 0; float bl = 0;
                    vector mm[2];
                    if (obj.GetCollisionBox(mm)) { bw = mm[1][0] - mm[0][0]; bl = mm[1][2] - mm[0][2]; }
                    string dbg = obj.GetDebugName();
                    dbg.Replace(",", " ");
                    string row = t + "," + sc + "," + bd;
                    row = row + "," + p[0] + "," + p[2] + "," + o[0];
                    row = row + "," + bw + "," + bl + "," + dbg + "\n";
                    buf = buf + row;
                    if (nUnique % 100 == 0) { FPrint(fo, buf); buf = ""; }
                }
                z = z + step;
            }
            x = x + step;
        }
        if (buf != "") FPrint(fo, buf);
        CloseFile(fo);
        BZBusLog.Info("[ROADSCAN v2] " + nUnique + " objetos unicos | centro " + center + " half=" + half);
    }

    // --- v3: scan MAPA-COMPLETO frame-spread ---
    private static ref BZRoadScan s_Map;
    private static bool s_MapDone = false;
    private float m_maxX, m_maxZ, m_step, m_radius, m_curX, m_curZ;
    private FileHandle m_fo;
    private int m_hits, m_probes;
    private ref array<Object>    m_objs;
    private ref array<CargoBase> m_prox;

    static void StartMapScan(float mapSize, float step, float radius) {
        if (s_Map || s_MapDone) return;
        s_MapDone = true;
        if (mapSize <= 0) mapSize = 15360.0;
        if (step   <= 0) step   = 40.0;
        if (radius <= 0) radius = 30.0;
        BZRoadScan inst = new BZRoadScan();
        inst.m_maxX = mapSize; inst.m_maxZ = mapSize;
        inst.m_step = step;    inst.m_radius = radius;
        inst.m_curX = 0;       inst.m_curZ = 0;
        inst.m_hits = 0;       inst.m_probes = 0;
        inst.m_objs = new array<Object>;
        inst.m_prox = new array<CargoBase>;
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        inst.m_fo = OpenFile(dir + "roadscan_map.csv", FileMode.WRITE);
        if (!inst.m_fo) { BZBusLog.Err("[MAPSCAN] no se pudo crear roadscan_map.csv"); return; }
        FPrint(inst.m_fo, "x,z,yaw,debug\n");
        s_Map = inst;
        BZBusLog.Info("[MAPSCAN] iniciando scan COMPLETO del mapa " + mapSize + "m (step=" + step + " r=" + radius + ") frame-spread");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_Map.MapTick, 0, true);
    }

    void MapTick() {
        int budget = 250;
        while (budget > 0) {
            budget = budget - 1;
            MapProbe(m_curX, m_curZ);
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_maxZ) {
                m_curZ = 0;
                m_curX = m_curX + m_step;
                if (m_curX > m_maxX) { MapFinish(); return; }
            }
        }
        if (m_probes % 10000 < 250) BZBusLog.Info("[MAPSCAN] x=" + m_curX + "/" + m_maxX + " probes=" + m_probes + " road-hits=" + m_hits);
    }

    void MapProbe(float x, float z) {
        float y = GetGame().SurfaceY(x, z);
        m_objs.Clear(); m_prox.Clear();
        GetGame().GetObjectsAtPosition3D(Vector(x, y, z), m_radius, m_objs, m_prox);
        string buf = "";
        foreach (Object obj : m_objs) {
            if (!obj) continue;
            string dbg = obj.GetDebugName();
            if (!IsRoadPart(dbg)) continue;
            dbg.Replace(",", " ");
            vector p = obj.GetPosition();
            vector o = obj.GetOrientation();
            string row = "" + p[0] + "," + p[2] + "," + o[0] + "," + dbg + "\n";
            buf = buf + row;
            m_hits = m_hits + 1;
        }
        if (buf != "") FPrint(m_fo, buf);
    }

    bool IsRoadPart(string dbg) {
        if (dbg.Contains("roads")) return true;
        if (dbg.Contains("asf"))   return true;
        if (dbg.Contains("grav"))  return true;
        if (dbg.Contains("mud"))   return true;
        if (dbg.Contains("path_")) return true;
        if (dbg.Contains("city"))  return true;
        if (dbg.Contains("\\kr"))  return true;
        return false;
    }

    void MapFinish() {
        if (m_fo) { CloseFile(m_fo); m_fo = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.MapTick);
        BZBusLog.Info("[MAPSCAN] COMPLETO: " + m_probes + " probes, " + m_hits + " road-hits (con duplicados; dedup offline) -> roadscan_map.csv");
        s_Map = null;
    }
}
