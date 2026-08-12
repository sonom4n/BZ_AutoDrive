// ============================================================================
//  BZObjectScanAll - SCAN DE OBJETOS/ESTRUCTURAS DE TODO EL MAPA (full data).
//  (Sonom4n, 2026-07-11) roadscan_objects.csv fue un scan PARCIAL (solo Novaya) -> el
//  editor solo tiene edificios ahi. Esto barre el MAPA ENTERO (frame-spread, no
//  cuelga el server) y extrae TODO dato util de cada estructura:
//    type, scenery, building, x, y, z, yaw, bw, bl, bh, model(debug)
//  Filtra a ESTRUCTURAS (edificios + muros/cercas + wrecks) para no inflar con
//  vegetacion/props. NO deduplica en el motor (cada objeto sale en varios probes)
//  -> dedup OFFLINE por (type,pos). Combina el frame-spread de BZRoadScan v3 con
//  la extraccion completa de v2. [[project_wizard_vehicle_data_capture]]
//    salida: $profile:BZ_AutoDrive_PathLogger\roadscan_objects_full.csv
//  USO: BZObjectScanAll.Begin(0,0,0)  (una vez, server vacio).
// ============================================================================
class BZObjectScanAll {
    private static ref BZObjectScanAll s_inst;
    private static bool s_done = false;
    private float m_max, m_step, m_radius, m_curX, m_curZ;
    private FileHandle m_fo;
    private int m_hits, m_probes;
    private ref array<Object>    m_objs;
    private ref array<CargoBase> m_prox;

    static void Begin(float mapSize, float step, float radius) {
        if (s_inst || s_done) return;
        s_done = true;
        if (mapSize <= 0) mapSize = 15360.0;
        if (step   <= 0) step   = 30.0;   // grilla de sondeo (m)
        if (radius <= 0) radius = 24.0;   // radio por sondeo (solapa para no dejar gaps)
        BZObjectScanAll inst = new BZObjectScanAll();
        inst.m_max = mapSize; inst.m_step = step; inst.m_radius = radius;
        inst.m_curX = 0; inst.m_curZ = 0; inst.m_hits = 0; inst.m_probes = 0;
        inst.m_objs = new array<Object>;
        inst.m_prox = new array<CargoBase>;
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        inst.m_fo = OpenFile(dir + "roadscan_objects_full.csv", FileMode.WRITE);
        if (!inst.m_fo) { BZBusLog.Err("[OBJSCAN] no pude crear roadscan_objects_full.csv"); return; }
        FPrint(inst.m_fo, "type,scenery,building,x,y,z,yaw,bw,bl,bh,debug\n");
        s_inst = inst;
        BZBusLog.Info("[OBJSCAN] scan de objetos COMPLETO " + mapSize + "m step=" + step + " r=" + radius + " frame-spread");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_inst.Tick, 0, true);
    }

    void Tick() {
        int budget = 180;                 // probes por frame (baja si stutterea)
        while (budget > 0) {
            budget = budget - 1;
            Probe(m_curX, m_curZ);
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_max) {
                m_curZ = 0;
                m_curX = m_curX + m_step;
                if (m_curX > m_max) { Finish(); return; }
            }
        }
        if (m_probes % 10000 < 180) BZBusLog.Info("[OBJSCAN] x=" + m_curX + "/" + m_max + " probes=" + m_probes + " objs=" + m_hits);
    }

    void Probe(float x, float z) {
        float y = GetGame().SurfaceY(x, z);
        m_objs.Clear(); m_prox.Clear();
        GetGame().GetObjectsAtPosition3D(Vector(x, y, z), m_radius, m_objs, m_prox);
        string buf = "";
        foreach (Object obj : m_objs) {
            if (!obj) continue;
            string dbg = obj.GetDebugName();
            if (!IsStructure(obj, dbg)) continue;
            dbg.Replace(",", " ");
            string t = obj.GetType();
            int sc = 0; if (obj.IsScenery())  sc = 1;
            int bd = 0; if (obj.IsBuilding()) bd = 1;
            vector p = obj.GetPosition();
            vector o = obj.GetOrientation();
            float bw = 0, bl = 0, bh = 0;
            vector mm[2];
            if (obj.GetCollisionBox(mm)) { bw = mm[1][0]-mm[0][0]; bl = mm[1][2]-mm[0][2]; bh = mm[1][1]-mm[0][1]; }
            string row = t + "," + sc + "," + bd + "," + p[0] + "," + p[1] + "," + p[2] + "," + o[0];
            row = row + "," + bw + "," + bl + "," + bh + "," + dbg + "\n";
            buf = buf + row;
            m_hits = m_hits + 1;
        }
        if (buf != "") FPrint(m_fo, buf);
    }

    // estructura relevante para el mapa: edificio, o muro/cerca, o wreck. (excluye vegetacion/props)
    bool IsStructure(Object obj, string dbg) {
        if (obj.IsBuilding()) return true;
        if (dbg.Contains("Wreck") || dbg.Contains("wreck")) return true;
        if (dbg.Contains("wall")  || dbg.Contains("Wall"))  return true;
        if (dbg.Contains("fence") || dbg.Contains("Fence")) return true;
        if (dbg.Contains("zabor") || dbg.Contains("plot"))  return true;
        return false;
    }

    void Finish() {
        if (m_fo) { CloseFile(m_fo); m_fo = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        BZBusLog.Info("[OBJSCAN] COMPLETO " + m_probes + " probes, " + m_hits + " objetos (con dups; dedup offline) -> roadscan_objects_full.csv");
        s_inst = null;
    }
}
