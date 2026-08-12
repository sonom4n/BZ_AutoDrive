// ============================================================================
//  BZSurfaceScan - EXTRACTOR DE GRAFO VIAL DESDE EL MAPA (Sonom4n).
//  Idea original (2026-07-06): en vez de manejar cada calle, leer las CARRETERAS
//  DEL MAPA MISMO. El motor sabe la superficie en cada punto (SurfaceGetType) ->
//  barremos una grilla, clasificamos ruta vs no-ruta, esqueletizamos.
//
//  2026-07-15: agregado el barrido de MAPA COMPLETO en 2 fases, para capturar los
//  caminos PINTADOS en el terreno (textura, SIN objeto road_*): GetObjectsAtPosition3D
//  no los ve, SurfaceGetType SI. Antes: solo ScanOnce (un cuadro chico), abajo, intacto.
// ============================================================================
class BZSurfaceScan {
    private static bool s_Done = false;

    // --- MVP viejo: un cuadro chico (legacy, intacto) ---
    static void ScanOnce(vector center, float half, float step) {
        if (s_Done) return;
        s_Done = true;
        string path = "$profile:BZ_AutoDrive_PathLogger\\surface_scan.csv";
        FileHandle f = OpenFile(path, FileMode.WRITE);
        if (!f) { BZBusLog.Err("[SURFSCAN] no se pudo crear " + path); return; }
        FPrint(f, "x,z,surface\n");
        int n = 0;
        string buf = "";
        float x = center[0] - half;
        while (x <= center[0] + half) {
            float z = center[2] - half;
            while (z <= center[2] + half) {
                float y = GetGame().SurfaceY(x, z);
                string st = "";
                GetGame().SurfaceGetType3D(x, y, z, st);
                buf = buf + x + "," + z + "," + st + "\n";
                n++;
                if (n % 200 == 0) { FPrint(f, buf); buf = ""; }
                z = z + step;
            }
            x = x + step;
        }
        if (buf != "") FPrint(f, buf);
        CloseFile(f);
        BZBusLog.Info("[SURFSCAN] " + n + " puntos (half=" + half + " step=" + step + ") -> " + path);
    }

    // ==================== FASE 1: DESCUBRIMIENTO de materiales (mapa completo) ==============
    //  No sabemos los nombres de material (viven en la config del mapa; en Sakhal en el .ebo
    //  encriptado) -> primero hay que VERLOS. Cuenta TODOS los materiales y su extension.
    //  Con la lista decidimos si hay un material DEDICADO de camino (aislable) o si el camino
    //  comparte material con el terreno (y entonces material solo NO alcanza -> otra via).
    //    salida: $profile:BZ_AutoDrive_PathLogger\<world>_surftypes.csv  (type,count)  + log al RPT
    private static ref BZSurfaceScan s_disc;
    private static bool s_discDone = false;
    private string m_world;
    private float m_size, m_step, m_curX, m_curZ;
    private int m_probes;
    private ref map<string,int> m_tally;
    private static const float DISC_STEP = 6.0;   // barato; para el TALLY basta

    static void Discover() {
        if (s_disc || s_discDone) return;
        s_discDone = true;
        BZSurfaceScan i = new BZSurfaceScan();
        string wn = "empty"; GetGame().GetWorldName(wn); wn.ToLower(); i.m_world = wn;
        i.m_size = GetGame().GetWorld().GetWorldSize();
        if (i.m_size <= 0) { BZBusLog.Err("[SURF] GetWorldSize invalido"); return; }
        i.m_step = DISC_STEP; i.m_curX = 0; i.m_curZ = 0;
        i.m_tally = new map<string,int>;
        s_disc = i;
        BZBusLog.Info("[SURF] descubrimiento de materiales mundo=" + i.m_world + " size=" + i.m_size + " step=" + DISC_STEP);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_disc.DiscTick, 0, true);
    }

    void DiscTick() {
        int budget = 3000;
        while (budget > 0) {
            budget = budget - 1;
            string t = "";
            GetGame().SurfaceGetType(m_curX, m_curZ, t);
            if (t != "") {
                int n = 0;
                if (m_tally.Contains(t)) n = m_tally.Get(t);
                m_tally.Set(t, n + 1);
            }
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_size) {
                m_curZ = 0; m_curX = m_curX + m_step;
                if (m_curX > m_size) { DiscFinish(); return; }
            }
        }
        if (m_probes % 100000 < 3000)
            BZBusLog.Info("[SURF] x=" + m_curX + "/" + m_size + " materiales_distintos=" + m_tally.Count());
    }

    void DiscFinish() {
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        FileHandle fo = OpenFile(dir + m_world + "_surftypes.csv", FileMode.WRITE);
        if (fo) {
            FPrint(fo, "type,count\n");
            for (int i = 0; i < m_tally.Count(); i++)
                FPrint(fo, m_tally.GetKey(i) + "," + m_tally.GetElement(i) + "\n");
            CloseFile(fo);
        }
        BZBusLog.Info("[SURF] COMPLETO mundo=" + m_world + " | " + m_probes + " probes | " + m_tally.Count() + " materiales -> " + m_world + "_surftypes.csv");
        for (int j = 0; j < m_tally.Count(); j++)
            BZBusLog.Info("[SURF]   " + m_tally.GetKey(j) + " = " + m_tally.GetElement(j));
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.DiscTick);
        s_disc = null;
    }

    // ==================== FASE 2: CAPTURA de las huellas de camino ==========================
    //  Con el vocabulario de FASE 1 confirmado (Sakhal tiene materiales DEDICADOS de camino:
    //  sakhal_gravel/cp_gravel/gravel_*, dirt_ext, asphalt_ext/felt, concrete_ext/sakhal_concrete),
    //  barre FINO y escribe SOLO las celdas de esos materiales. Offline: rasterizar -> esqueleto
    //  = centerlines de las huellas PINTADAS (las que no tienen objeto road_*).
    //  Excluye interiores de edificio (*_int) y terreno (beach/grass/snow/forest/ice).
    //    salida: $profile:BZ_AutoDrive_PathLogger\<world>_surfroad.csv  (x,z,type)
    private static ref BZSurfaceScan s_cap;
    private static bool s_capDone = false;
    private FileHandle m_fo;
    private int m_hits;
    private static const float CAP_STEP = 3.0;   // huellas ~4-6m de ancho

    static void Capture() {
        if (s_cap || s_capDone) return;
        s_capDone = true;
        BZSurfaceScan i = new BZSurfaceScan();
        string wn = "empty"; GetGame().GetWorldName(wn); wn.ToLower(); i.m_world = wn;
        i.m_size = GetGame().GetWorld().GetWorldSize();
        if (i.m_size <= 0) { BZBusLog.Err("[SURFCAP] GetWorldSize invalido"); return; }
        i.m_step = CAP_STEP; i.m_curX = 0; i.m_curZ = 0; i.m_hits = 0;
        i.m_fo = OpenFile("$profile:BZ_AutoDrive_PathLogger\\" + i.m_world + "_surfroad.csv", FileMode.WRITE);
        if (!i.m_fo) { BZBusLog.Err("[SURFCAP] no pude crear el CSV"); return; }
        FPrint(i.m_fo, "x,z,type\n");
        s_cap = i;
        BZBusLog.Info("[SURFCAP] captura de huellas de camino mundo=" + i.m_world + " step=" + CAP_STEP);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_cap.CapTick, 0, true);
    }

    // material de camino manejable? (excluye terreno e interiores)
    bool IsRoadSurface(string t) {
        if (t.Contains("_int")) return false;                    // piso de edificio
        if (t.Contains("dirt"))     return true;
        if (t.Contains("gravel"))   return true;
        if (t.Contains("asphalt"))  return true;
        if (t.Contains("concrete")) return true;
        return false;
    }

    void CapTick() {
        int budget = 3000;
        string buf = "";
        while (budget > 0) {
            budget = budget - 1;
            string t = "";
            GetGame().SurfaceGetType(m_curX, m_curZ, t);
            if (t != "" && IsRoadSurface(t)) {
                buf = buf + m_curX + "," + m_curZ + "," + t + "\n";
                m_hits = m_hits + 1;
            }
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_size) {
                m_curZ = 0; m_curX = m_curX + m_step;
                if (m_curX > m_size) { if (buf != "") FPrint(m_fo, buf); CapFinish(); return; }
            }
        }
        if (buf != "") FPrint(m_fo, buf);
        if (m_probes % 300000 < 3000)
            BZBusLog.Info("[SURFCAP] x=" + m_curX + "/" + m_size + " celdas_camino=" + m_hits);
    }

    void CapFinish() {
        if (m_fo) { CloseFile(m_fo); m_fo = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.CapTick);
        BZBusLog.Info("[SURFCAP] COMPLETO mundo=" + m_world + " | " + m_probes + " probes | " + m_hits + " celdas de camino -> " + m_world + "_surfroad.csv");
        s_cap = null;
    }
}
