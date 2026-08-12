// ============================================================================
//  BZRoadwayScanAll - CINTA DE CALZADA REAL DE TODO EL MAPA (full resolution).
//  (Sonom4n, 2026-07-11) La capa del editor sale de los centros p3d (roadscan_map)
//  con ancho ESTIMADO -> forma pobre, espolones, calles con ancho irreal.
//  El detalle NO viene del tamano de celda: viene de medir el BORDE real de la
//  calzada por busqueda binaria sobre el ROADWAY LOD (GetSurface), precision
//  ~0.05 m (4x mas fino que 0.2 m) y baratisimo (~14 probes por lado).
//
//  PARTE 1 (este archivo): por cada road part conocido, barre PERPENDICULAR a su
//  yaw y mide borde izq/der + ancho REAL -> roadway_ribbon.csv. Corre por chunks
//  (CallLater) para no colgar el server.
//    salida: $profile:BZ_AutoDrive_PathLogger\roadway_ribbon.csv
//            cx,cz,yaw,lx,lz,rx,rz,width
//  PARTE 2 (si faltan calles): flood-fill grueso de descubrimiento -> semillas
//  nuevas -> re-correr Parte 1. Se agrega despues si hace falta.
//  Reusa ProbeRoadway/EdgeDist de [[project_roadway_lod_extraction]].
//
//  USO: BZRoadwayScanAll.Begin()  (una vez, server vacio), desde donde disparas
//  el extractor. Requiere que exista roadscan_map.csv (el roadscan previo).
// ============================================================================
class BZRoadwayScanAll {
    static const int   BUDGET   = 1500;   // road parts por tick (baja si stutterea)
    static const float MAX_HALF = 12.0;   // hasta donde busca el borde por lado (m)
    static const float DH_MAX   = 3.0;    // tolerancia de altura "sobre calzada"
    static const float DEG      = 0.0174532925;

    static ref array<float> s_x, s_z, s_yaw;
    static int s_i;
    static ref SurfaceDetectionParameters s_sp;
    static ref SurfaceDetectionResult s_sr;
    static FileHandle s_out;
    static string s_buf;
    static int s_written;

    static void Begin() {
        s_x = new array<float>(); s_z = new array<float>(); s_yaw = new array<float>();
        s_sp = new SurfaceDetectionParameters();
        s_sp.type = SurfaceDetectionType.Roadway;
        s_sp.includeWater = false;
        s_sp.rsd = RoadSurfaceDetection.UNDER;
        s_sr = new SurfaceDetectionResult();

        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        LoadSeeds(dir + "roadscan_map.csv");
        if (s_x.Count() == 0) { BZBusLog.Err("[RIBBON] sin semillas (falta roadscan_map.csv)"); return; }

        s_out = OpenFile(dir + "roadway_ribbon.csv", FileMode.WRITE);
        if (!s_out) { BZBusLog.Err("[RIBBON] no pude crear roadway_ribbon.csv"); return; }
        FPrint(s_out, "cx,cz,yaw,lx,lz,rx,rz,width\n");

        s_i = 0; s_written = 0; s_buf = "";
        BZBusLog.Info("[RIBBON] road parts=" + s_x.Count() + " -> midiendo bordes reales...");
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(Step, 50, false);
    }

    static void LoadSeeds(string path) {
        FileHandle f = OpenFile(path, FileMode.READ);
        if (!f) return;
        string line;
        int r = FGets(f, line);              // header x,z,yaw,debug
        r = FGets(f, line);
        while (r >= 0) {
            if (line.Length() > 0) {
                array<string> c = new array<string>();
                line.Split(",", c);
                if (c.Count() >= 3) {
                    s_x.Insert(c[0].ToFloat());
                    s_z.Insert(c[1].ToFloat());
                    s_yaw.Insert(c[2].ToFloat());
                }
            }
            r = FGets(f, line);
        }
        CloseFile(f);
    }

    static void Step() {
        int end = Math.Min(s_i + BUDGET, s_x.Count());
        for (; s_i < end; s_i++) {
            float yawR = s_yaw[s_i] * DEG;
            vector c   = Vector(s_x[s_i], 0, s_z[s_i]);
            vector fwd = Vector(Math.Sin(yawR), 0, Math.Cos(yawR));
            vector rgt = Vector(fwd[2], 0, -fwd[0]);          // perpendicular

            float ld = EdgeDist(c, rgt, -1.0);
            float rd = EdgeDist(c, rgt,  1.0);
            vector L = c - rgt * ld;
            vector R = c + rgt * rd;
            float w  = ld + rd;
            if (w < 0.3) continue;                            // no habia calzada aca

            s_buf = s_buf + s_x[s_i] + "," + s_z[s_i] + "," + s_yaw[s_i] + ",";
            s_buf = s_buf + L[0] + "," + L[2] + "," + R[0] + "," + R[2] + "," + w + "\n";
            s_written++;
            if (s_written % 200 == 0) { FPrint(s_out, s_buf); s_buf = ""; }
        }
        if (s_i < s_x.Count()) {
            if (s_i % 15000 < BUDGET) BZBusLog.Info("[RIBBON] " + s_i + "/" + s_x.Count() + " (" + s_written + " con calzada)");
            GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(Step, 15, false);
        } else {
            if (s_buf != "") { FPrint(s_out, s_buf); s_buf = ""; }
            CloseFile(s_out);
            BZBusLog.Info("[RIBBON] DONE " + s_written + " secciones -> roadway_ribbon.csv");
        }
    }

    // borde de la calzada por busqueda binaria (bracket 1m + biseccion 0.05m) ~14 probes
    static float EdgeDist(vector c, vector dir, float sign) {
        float lo = 0.0, hi = -1.0, d = 1.0;
        while (d <= MAX_HALF) {
            if (ProbeRoadway(c + dir * (sign * d))) lo = d;
            else { hi = d; break; }
            d = d + 1.0;
        }
        if (hi < 0.0) return lo;                 // calzada hasta el cap
        while (hi - lo > 0.05) {
            float mid = (lo + hi) * 0.5;
            if (ProbeRoadway(c + dir * (sign * mid))) lo = mid; else hi = mid;
        }
        return lo;
    }

    static bool ProbeRoadway(vector p) {
        float ty = GetGame().SurfaceY(p[0], p[2]);
        s_sp.position = Vector(p[0], ty + 1.0, p[2]);
        if (!GetGame().GetSurface(s_sp, s_sr)) return false;
        if (!s_sr.object) return false;
        float dh = ty - s_sr.height;
        if (dh < 0) dh = -dh;
        return dh < DH_MAX;
    }
}
