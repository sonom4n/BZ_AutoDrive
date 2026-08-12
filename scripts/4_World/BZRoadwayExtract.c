// ============================================================================
//  BZRoadwayExtract - LEE LA CAPA ROADWAY REAL DEL MOTOR (2026-07-10).
//  Idea (Sonom4n): "hay una capa del mapa que no estamos viendo con detalle y la
//  traza esta dibujada ahi". El roadscan actual usa el bbox de los p3d (origenes
//  dispersos). El motor tiene el ROADWAY LOD exacto (superficie, no colision) +
//  ruteo vial nativo. Dos vias:
//    1) FindPath(ROADWAY): polilinea A->B sesgada a calzada, TOPOLOGIA de cruces
//       resuelta por el motor (adios confusion de brazos). -> roadway_path.csv
//    2) GetSurface(Roadway): superficie exacta por punto. Barremos PERPENDICULAR
//       a la ruta hasta salir de la calzada -> bordes izq/der reales, ancho real,
//       altura real. -> roadway_edges.csv
//  Offline visualizo y trazo la racing-line sobre la geometria EXACTA.
//  [[reference_dayz_native_road_apis]]
// ============================================================================
class BZRoadwayExtract {
    // RE-CORRIBLE (loop auto-mejora 2026-07-10): se re-extrae en cada Init de ruta con el flag prendido
    // -> load GRAFO -> extrae -> refinar -> load refinada -> re-extrae -> converge. Sin reiniciar el server.
    static void Extract(vector from, vector to, array<ref BZWaypoint> route) {
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        ExtractFindPath(from, to, dir);
        ExtractEdges(route, dir);
    }

    // --- 1) esqueleto nativo sesgado a ROADWAY ---
    private static void ExtractFindPath(vector from, vector to, string dir) {
        AIWorld aiw = GetGame().GetWorld().GetAIWorld();
        array<vector> path = new array<vector>();
        bool ok = false;
        if (aiw) {
            PGFilter filt = new PGFilter();
            filt.SetCost(PGAreaType.ROADWAY, 0.1);
            filt.SetCost(PGAreaType.ROADWAY_BUILDING, 0.1);
            filt.SetCost(PGAreaType.TERRAIN, 1000.0);
            filt.SetCost(PGAreaType.OBJECTS, 1000.0);
            ok = aiw.FindPath(from, to, filt, path);
        }
        FileHandle fp = OpenFile(dir + "roadway_path.csv", FileMode.WRITE);
        if (fp) {
            FPrint(fp, "x,y,z\n");
            int np = path.Count();
            for (int i = 0; i < np; i++) {
                vector p = path[i];
                FPrint(fp, "" + p[0] + "," + p[1] + "," + p[2] + "\n");
            }
            CloseFile(fp);
        }
        BZBusLog.Info("[ROADWAY] FindPath ok=" + ok + " wps=" + path.Count() + " -> roadway_path.csv");
    }

    // --- 2) bordes exactos de la calzada por GetSurface(Roadway) perpendicular ---
    private static void ExtractEdges(array<ref BZWaypoint> route, string dir) {
        if (!route || route.Count() < 2) return;
        SurfaceDetectionParameters sp = new SurfaceDetectionParameters();
        sp.type = SurfaceDetectionType.Roadway;
        sp.includeWater = false;
        sp.rsd = RoadSurfaceDetection.UNDER;
        SurfaceDetectionResult sr = new SurfaceDetectionResult();

        FileHandle fe = OpenFile(dir + "roadway_edges.csv", FileMode.WRITE);
        if (!fe) { BZBusLog.Err("[ROADWAY] no se pudo crear roadway_edges.csv"); return; }
        FPrint(fe, "cx,cz,lx,lz,rx,rz,width,height,onroad\n");

        int n = route.Count();
        string buf = "";
        int cnt = 0;
        for (int k = 0; k < n; k++) {
            vector c = route[k].GetVector();
            int kn = Math.Min(k + 1, n - 1);
            int kp = Math.Max(k - 1, 0);
            vector fwd = route[kn].GetVector() - route[kp].GetVector();
            fwd[1] = 0;
            if (fwd.Length() < 0.01) continue;
            fwd.Normalize();
            vector right = Vector(fwd[2], 0, -fwd[0]);

            float ld = EdgeDist(c, right, -1.0, sp, sr);
            float rd = EdgeDist(c, right,  1.0, sp, sr);
            vector L = c - right * ld;
            vector R = c + right * rd;
            float wdt = ld + rd;

            bool on = ProbeRoadway(c, sp, sr);
            float h = sr.height;

            string row = "" + c[0] + "," + c[2] + "," + L[0] + "," + L[2];
            row = row + "," + R[0] + "," + R[2];
            row = row + "," + wdt + "," + h + "," + on + "\n";
            buf = buf + row;
            cnt++;
            if (cnt % 100 == 0) { FPrint(fe, buf); buf = ""; }
        }
        if (buf != "") FPrint(fe, buf);
        CloseFile(fe);
        BZBusLog.Info("[ROADWAY] " + cnt + " secciones con bordes -> roadway_edges.csv");
    }

    // consulta si hay ROADWAY en la vertical de p (rsd UNDER desde un poco arriba)
    private static bool ProbeRoadway(vector p, SurfaceDetectionParameters sp, SurfaceDetectionResult sr) {
        float ty = GetGame().SurfaceY(p[0], p[2]);
        sp.position = Vector(p[0], ty + 1.0, p[2]);
        bool ok = GetGame().GetSurface(sp, sr);
        if (!ok) return false;
        if (!sr.object) return false;
        float dh = ty - sr.height;
        if (dh < 0) dh = -dh;
        return dh < 3.0;
    }

    // borde de la calzada por BUSQUEDA BINARIA (2026-07-10, "el 5% por geometria"): bracket grueso a 1m
    // + biseccion a 0.05m -> ~14 probes (vs 36 lineales) y mas preciso (medio-decimetro). El piso de
    // precision es la malla del Roadway LOD (~cm), no nuestro paso.
    private static float EdgeDist(vector c, vector dir, float sign, SurfaceDetectionParameters sp, SurfaceDetectionResult sr) {
        float lo = 0.0;   // on-road
        float hi = -1.0;  // off-road (aun no bracketeado)
        float d = 1.0;
        while (d <= 9.0) {
            if (ProbeRoadway(c + dir * (sign * d), sp, sr)) {
                lo = d;
            } else {
                hi = d;
                break;
            }
            d = d + 1.0;
        }
        if (hi < 0.0) return lo;   // todo calzada hasta 9m -> cap
        while (hi - lo > 0.05) {
            float mid = (lo + hi) * 0.5;
            if (ProbeRoadway(c + dir * (sign * mid), sp, sr)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return lo;
    }
}
