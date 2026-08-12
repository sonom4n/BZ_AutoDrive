// ============================================================================
//  BZRoadwayWidth - mide el ANCHO REAL DE CALZADA que ve el MOTOR. (Sonom4n, 2026-07-15)
//
//  POR QUE: los anchos del catalogo salieron de medir el p3d (asf1=12m). Pero:
//   (a) en SAKHAL los p3d viven dentro de worlds_sakhal.EBO = PBO ENCRIPTADO de DLC
//       -> NO se pueden extraer ni medir offline. Esta es la unica via.
//   (b) el ancho del p3d es el del MODELO (banquinas incluidas); lo que el auto
//       maneja es el Roadway LOD. Esto mide ESO, para los 3 mapas.
//
//  COMO: por cada pieza de calzada, pararse en su eje y caminar de a 0.25m hacia
//  cada costado llamando GetSurface(type=Roadway). Mientras HAYA calzada (cualquiera)
//  seguimos sobre la ruta. Donde no hay -> borde.
//
//  v1 EXIGIA result.object == LA MISMA pieza (para no escaparse a la calle vecina en
//  los cruces). SALIO PEOR: las piezas SE SOLAPAN y el trace devuelve la que esta
//  ENCIMA, casi nunca la que preguntamos -> descartaba casi todo. En Chernarus el mud
//  (27k piezas, la calle mas comun) midio n=50 y 1.25m, y el mismo mud en Sakhal 5.75m:
//  imposible, era el metodo fallando en silencio.
//  v2: se acepta CUALQUIER calzada (object != null). Mide la SUPERFICIE MANEJABLE, que
//  es lo que queremos, y la MEDIANA sobre cientos de muestras absorbe el sangrado hacia
//  la calle transversal en los cruces.
//
//  Muestrea hasta MAX_PER_MODEL por modelo (no hace falta medir 30k piezas iguales).
//    salida: $profile:BZ_AutoDrive_PathLogger\<world>_roadway.csv
//            model,x,z,yaw,left,right,width
//  USO: RoadScanOnBoot=true dispara TriggerBootMapScan -> aca. Server VACIO.
// ============================================================================
class BZRoadwayWidth {
    private static ref BZRoadwayWidth s_inst;
    private static bool s_done = false;
    private string m_world;
    private float m_size, m_step, m_radius, m_curX, m_curZ;
    private FileHandle m_fo;
    private int m_probes, m_meas;
    private ref array<Object>    m_objs;
    private ref array<CargoBase> m_prox;
    private ref map<string,int>  m_seen;
    private ref SurfaceDetectionParameters m_sp;
    private ref SurfaceDetectionResult     m_sr;

    private static const float STEP        = 40.0;   // grilla de busqueda de piezas
    private static const float RAD         = 30.0;
    private static const float PROBE_STEP  = 0.25;   // resolucion del sondeo lateral
    private static const float PROBE_MAX   = 12.0;   // hasta 12m por lado
    private static const int   MAX_PER_MODEL = 25;

    static void Begin() {
        if (s_inst || s_done) return;
        s_done = true;
        BZRoadwayWidth i = new BZRoadwayWidth();
        string wn = "empty";
        GetGame().GetWorldName(wn); wn.ToLower(); i.m_world = wn;
        i.m_size = GetGame().GetWorld().GetWorldSize();
        if (i.m_size <= 0) { BZBusLog.Err("[RWIDTH] GetWorldSize invalido"); return; }
        i.m_step = STEP; i.m_radius = RAD; i.m_curX = 0; i.m_curZ = 0;
        i.m_objs = new array<Object>; i.m_prox = new array<CargoBase>;
        i.m_seen = new map<string,int>;
        i.m_sp = new SurfaceDetectionParameters();
        i.m_sp.type = SurfaceDetectionType.Roadway;
        i.m_sp.rsd  = RoadSurfaceDetection.UNDER;     // sondeo desde arriba hacia abajo
        i.m_sr = new SurfaceDetectionResult();
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        i.m_fo = OpenFile(dir + i.m_world + "_roadway.csv", FileMode.WRITE);
        if (!i.m_fo) { BZBusLog.Err("[RWIDTH] no pude crear el CSV"); return; }
        FPrint(i.m_fo, "model,x,z,yaw,left,right,width\n");
        s_inst = i;
        BZBusLog.Info("[RWIDTH] ancho de calzada (Roadway LOD) mundo=" + i.m_world + " size=" + i.m_size);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_inst.Tick, 0, true);
    }

    void Tick() {
        int budget = 40;                 // pocas por frame: cada pieza son ~100 GetSurface
        while (budget > 0) {
            budget = budget - 1;
            Probe(m_curX, m_curZ);
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_size) {
                m_curZ = 0; m_curX = m_curX + m_step;
                if (m_curX > m_size) { Finish(); return; }
            }
        }
        if (m_probes % 2000 < 40) {
            string msg = "[RWIDTH] x=" + m_curX + "/" + m_size + " medidas=" + m_meas;
            BZBusLog.Info(msg);
        }
    }

    // hay calzada (CUALQUIERA) en (x,z)?  v2: NO exigir la misma pieza (ver cabecera)
    bool RoadwayAt(float x, float z, float yTop) {
        m_sp.position = Vector(x, yTop, z);
        if (!GetGame().GetSurface(m_sp, m_sr)) return false;
        return m_sr.object != null;
    }
    // camina hasta que deja de haber calzada -> distancia al borde
    float Edge(float cx, float cz, float y, float dx, float dz) {
        float d = 0;
        while (d < PROBE_MAX) {
            float nd = d + PROBE_STEP;
            if (!RoadwayAt(cx + dx*nd, cz + dz*nd, y)) return d;
            d = nd;
        }
        return PROBE_MAX;
    }

    void Probe(float x, float z) {
        float y = GetGame().SurfaceY(x, z);
        m_objs.Clear(); m_prox.Clear();
        GetGame().GetObjectsAtPosition3D(Vector(x, y, z), m_radius, m_objs, m_prox);
        string buf = "";
        foreach (Object obj : m_objs) {
            if (!obj) continue;
            string dbg = obj.GetDebugName();
            if (!IsRoadPart(dbg)) continue;
            string mo = ModelOf(dbg);
            int n = 0;
            if (m_seen.Contains(mo)) n = m_seen.Get(mo);
            if (n >= MAX_PER_MODEL) continue;
            vector p = obj.GetPosition();
            vector o = obj.GetOrientation();
            float yaw = o[0] * Math.DEG2RAD;
            // eje de la pieza = (sin,cos) ; PERPENDICULAR = (cos,-sin)
            float px = Math.Cos(yaw); float pz = -Math.Sin(yaw);
            float yTop = GetGame().SurfaceY(p[0], p[2]) + 1.0;
            // el centro tiene que ser calzada; si no, la pieza no sirve de referencia
            if (!RoadwayAt(p[0], p[2], yTop)) continue;
            float L = Edge(p[0], p[2], yTop, -px, -pz);
            float R = Edge(p[0], p[2], yTop,  px,  pz);
            m_seen.Set(mo, n + 1);
            m_meas = m_meas + 1;
            string row = mo + "," + p[0] + "," + p[2] + "," + o[0] + "," + L + "," + R + "," + (L + R) + "\n";
            buf = buf + row;
        }
        if (buf != "") FPrint(m_fo, buf);
    }

    // debug = "<id>: <ruta>\<modelo>.p3d"  ->  "<modelo>"  (CON el espacio: "mud_10 25")
    // OJO: NO sacar los espacios. Antes hacia Replace(" ","") y "mud_10 25" salia "mud_1025",
    // que NO matchea con el nombre del scan de calles -> el analisis leia "nunca se midio"
    // cuando en realidad se habia medido perfecto. Solo se saca la coma (rompe el CSV).
    string ModelOf(string dbg) {
        string s = dbg;
        int c = s.IndexOf(":");
        if (c >= 0) s = s.Substring(c + 1, s.Length() - c - 1);
        int sl = s.LastIndexOf("\\");
        if (sl >= 0) s = s.Substring(sl + 1, s.Length() - sl - 1);
        s.Replace(".p3d", "");
        s.Replace(",", "");
        s.Trim();
        s.ToLower();
        return s;
    }
    bool IsRoadPart(string dbg) {
        if (dbg.Contains("roads")) return true;
        if (dbg.Contains("asf"))   return true;
        if (dbg.Contains("grav"))  return true;
        if (dbg.Contains("mud"))   return true;
        if (dbg.Contains("path_")) return true;
        if (dbg.Contains("city"))  return true;
        return false;
    }
    void Finish() {
        if (m_fo) { CloseFile(m_fo); m_fo = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        string msg = "[RWIDTH] COMPLETO mundo=" + m_world + " | " + m_probes + " probes | " + m_meas + " piezas medidas";
        msg = msg + " | modelos distintos=" + m_seen.Count() + " -> " + m_world + "_roadway.csv";
        BZBusLog.Info(msg);
        s_inst = null;
    }
}
