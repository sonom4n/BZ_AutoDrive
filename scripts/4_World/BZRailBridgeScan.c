// ============================================================================
//  BZRailBridgeScan - SCAN de VIAS DE TREN + PUENTES + AGUA de TODO el mapa.
//  (Sonom4n, 2026-07-14) El editor de trayectorias ya tiene la red vial EXACTA
//  (BZRoadScan). Faltan: rail (rail_track/rail_polet), puentes (bridge/most) y
//  agua (rios/mar) para cerrar los puentes de rio y completar el fondo.
//  Mismo mecanismo probado que BZRoadScan/BZObjectScanAll: barrido en grilla,
//  frame-spread (NO cuelga el server), dedup OFFLINE por (kind,pos).
//    - rail/puentes: GetObjectsAtPosition3D + filtro por GetDebugName()
//    - agua: SurfaceIsSea / SurfaceIsPond por celda de grilla (EXACTO, alineado)
//  Salidas ($profile:BZ_AutoDrive_PathLogger\):
//    railbridge_map.csv  -> kind,x,y,z,yaw,bw,bl,bh,debug
//    water_map.csv       -> x,z,kind   (solo celdas con agua)
//  USO: en consola de script del server VACIO -> BZRailBridgeScan.Begin(0,0,0)
// ============================================================================
class BZRailBridgeScan {
    private static ref BZRailBridgeScan s_inst;
    private static bool s_done = false;
    private float m_max, m_step, m_radius, m_curX, m_curZ;
    private FileHandle m_fo;   // rail/puentes
    private FileHandle m_fw;   // agua
    private int m_hits, m_water, m_probes;
    private ref array<Object>    m_objs;
    private ref array<CargoBase> m_prox;

    static void Begin(float mapSize, float step, float radius) {
        if (s_inst || s_done) return;
        s_done = true;
        if (mapSize <= 0) mapSize = 15360.0;
        if (step   <= 0) step   = 20.0;   // 20m: capta rail (piezas largas) + agua decente
        if (radius <= 0) radius = 16.0;   // solapa para no dejar gaps
        BZRailBridgeScan inst = new BZRailBridgeScan();
        inst.m_max = mapSize; inst.m_step = step; inst.m_radius = radius;
        inst.m_curX = 0; inst.m_curZ = 0; inst.m_hits = 0; inst.m_water = 0; inst.m_probes = 0;
        inst.m_objs = new array<Object>;
        inst.m_prox = new array<CargoBase>;
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        inst.m_fo = OpenFile(dir + "railbridge_map.csv", FileMode.WRITE);
        inst.m_fw = OpenFile(dir + "water_map.csv", FileMode.WRITE);
        if (!inst.m_fo || !inst.m_fw) { BZBusLog.Err("[RAILBR] no pude crear los CSV"); return; }
        FPrint(inst.m_fo, "kind,x,y,z,yaw,bw,bl,bh,debug\n");
        FPrint(inst.m_fw, "x,z,kind\n");
        s_inst = inst;
        BZBusLog.Info("[RAILBR] scan rail/puentes/agua " + mapSize + "m step=" + step + " r=" + radius + " frame-spread");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_inst.Tick, 0, true);
    }

    void Tick() {
        int budget = 200;                 // probes por frame (baja si stutterea)
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
        if (m_probes % 20000 < 200) BZBusLog.Info("[RAILBR] x=" + m_curX + "/" + m_max + " probes=" + m_probes + " rail/br=" + m_hits + " agua=" + m_water);
    }

    void Probe(float x, float z) {
        // --- AGUA: por celda (exacto, alineado a coords del juego) ---
        if (GetGame().SurfaceIsSea(x, z))       { FPrint(m_fw, "" + x + "," + z + ",sea\n");  m_water = m_water + 1; }
        else if (GetGame().SurfaceIsPond(x, z)) { FPrint(m_fw, "" + x + "," + z + ",pond\n"); m_water = m_water + 1; }

        // --- RAIL / PUENTES: objetos ---
        float y = GetGame().SurfaceY(x, z);
        m_objs.Clear(); m_prox.Clear();
        GetGame().GetObjectsAtPosition3D(Vector(x, y, z), m_radius, m_objs, m_prox);
        string buf = "";
        foreach (Object obj : m_objs) {
            if (!obj) continue;
            string dbg = obj.GetDebugName();
            string kind = RailBridgeKind(dbg);
            if (kind == "") continue;
            dbg.Replace(",", " ");
            vector p = obj.GetPosition();
            vector o = obj.GetOrientation();
            float bw = 0, bl = 0, bh = 0;
            vector mm[2];
            if (obj.GetCollisionBox(mm)) { bw = mm[1][0]-mm[0][0]; bl = mm[1][2]-mm[0][2]; bh = mm[1][1]-mm[0][1]; }
            string row = kind + "," + p[0] + "," + p[1] + "," + p[2] + "," + o[0];
            row = row + "," + bw + "," + bl + "," + bh + "," + dbg + "\n";
            buf = buf + row;
            m_hits = m_hits + 1;
        }
        if (buf != "") FPrint(m_fo, buf);
    }

    // captura INCLUSIVA (rail/bridge/most); clasificacion fina se hace OFFLINE con el debug completo
    string RailBridgeKind(string dbg) {
        if (dbg.Contains("rail"))   return "rail";     // rail_track_*, rail_polet, etc.
        if (dbg.Contains("bridge")) return "bridge";
        if (dbg.Contains("Bridge")) return "bridge";
        if (dbg.Contains("most_"))  return "bridge";   // por si algun modelo usa naming checo
        return "";
    }

    void Finish() {
        if (m_fo) { CloseFile(m_fo); m_fo = null; }
        if (m_fw) { CloseFile(m_fw); m_fw = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        BZBusLog.Info("[RAILBR] COMPLETO " + m_probes + " probes | rail/puentes=" + m_hits + " (con dups) | celdas agua=" + m_water + " -> railbridge_map.csv + water_map.csv");
        s_inst = null;
    }
}
