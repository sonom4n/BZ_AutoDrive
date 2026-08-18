// ============================================================================
//  BZRoadGraph - Grafo vial ruteable IN-GAME (pathfinding A->B). Pilar A de v1.1.
// ============================================================================
//  Carga road_graph_<map>.csv con FGets (NO JsonFileLoader: crashea/es lentisimo en
//  este build -> el framework ya lo abandono para wp.csv). Formato:
//    linea 1: "<nNodos>,<nAristas>"
//    nNodos lineas: "x,z"          (idx de linea = node id)
//    nAristas lineas: "a,b,costo"  (a,b = node ids ; costo = metros)
//  -> adjacency CSR -> Dijkstra con heap binario (lazy). Server-side.
//  PASO 1b: probar que rutea A->B en el RPT (todavia NO maneja; eso es el paso 3).
//  Offline (build_road_graph.py) dio SO->NE = 167 nodos / 27.19 km = referencia.
// ============================================================================

class BZRoadGraph {
    private static ref BZRoadGraph s_Instance;
    static BZRoadGraph GetInstance() {
        if (!s_Instance) s_Instance = new BZRoadGraph();
        return s_Instance;
    }

    private bool m_Loaded = false;
    private int  m_N = 0;                   // cantidad de nodos
    private ref array<float> m_NX;
    private ref array<float> m_NZ;
    // adjacency CSR: vecinos del nodo i en [m_AdjStart[i] .. m_AdjStart[i+1]-1]
    private ref array<int>   m_AdjStart;    // tamaño N+1
    private ref array<int>   m_AdjV;        // tamaño 2E (nodo vecino)
    private ref array<float> m_AdjW;        // tamaño 2E (costo)
    private ref array<int>   m_AdjEdge;     // tamaño 2E: id de arista PARALELO a m_AdjV -> mapea a la geometria
    // --- geometria de aristas (paso 3): pts de cada arista en CSR flat + ancho + endpoints (orientacion a->b) ---
    private int  m_E = 0;                    // cantidad de aristas
    private bool m_GeomLoaded = false;
    private ref array<int>   m_EStart;      // tamaño E+1: pts de arista i en [m_EStart[i]..m_EStart[i+1]-1]
    private ref array<float> m_EPx;         // pts flat (x)
    private ref array<float> m_EPz;         // pts flat (z)
    private ref array<float> m_EW;          // tamaño E: ancho de calzada (m)
    private ref array<int>   m_Ea;          // tamaño E: nodo ORIGEN (pts orientados a->b)
    private ref array<int>   m_Eb;          // tamaño E: nodo DESTINO

    bool IsLoaded() { return m_Loaded; }

    // Split que preserva vacios (copiado de BZBusService.SplitKeepEmpty; el Split nativo dropea vacios).
    private void SplitKeepEmpty(string line, TStringArray outArr) {
        outArr.Clear();
        int n = line.Length();
        int start = 0;
        for (int i = 0; i <= n; i++) {
            if (i == n || line.Substring(i, 1) == ",") {
                outArr.Insert(line.Substring(start, i - start));
                start = i + 1;
            }
        }
    }

    // -------- carga desde CSV con FGets + build de la adjacency CSR --------
    bool Load(string path) {
        m_Loaded = false;
        FileHandle f = OpenFile(path, FileMode.READ);
        if (!f) { BZBusLog.Info("[RoadGraph] no se pudo abrir " + path); return false; }

        TStringArray parts = new TStringArray();
        string line;

        // linea 1: conteos
        if (FGets(f, line) < 0) { CloseFile(f); BZBusLog.Info("[RoadGraph] archivo vacio"); return false; }
        SplitKeepEmpty(line, parts);
        if (parts.Count() < 2) { CloseFile(f); BZBusLog.Info("[RoadGraph] header invalido"); return false; }
        int nNodes = parts[0].ToInt();
        int nEdges = parts[1].ToInt();
        int i;
        int a; int b;   // decl UNA vez: en Enforce las vars de un for leakean al scope de la funcion
                        // -> re-declarar 'int a' en otro loop = "Multiple declaration". Solo asignar abajo.

        // nodos
        m_NX = new array<float>();
        m_NZ = new array<float>();
        for (i = 0; i < nNodes; i++) {
            if (FGets(f, line) < 0) break;
            SplitKeepEmpty(line, parts);
            if (parts.Count() < 2) continue;
            m_NX.Insert(parts[0].ToFloat());
            m_NZ.Insert(parts[1].ToFloat());
        }
        m_N = m_NX.Count();

        // aristas (a temp)
        array<int>   ta = new array<int>();
        array<int>   tb = new array<int>();
        array<float> tw = new array<float>();
        for (i = 0; i < nEdges; i++) {
            if (FGets(f, line) < 0) break;
            SplitKeepEmpty(line, parts);
            if (parts.Count() < 3) continue;
            a = parts[0].ToInt();
            b = parts[1].ToInt();
            if (a < 0 || a >= m_N || b < 0 || b >= m_N) continue;   // safety: node id valido
            ta.Insert(a); tb.Insert(b); tw.Insert(parts[2].ToFloat());
        }
        CloseFile(f);
        int e = ta.Count();

        // --- build CSR adjacency (no-dirigida: cada arista suma a los 2 endpoints) ---
        array<int> deg = new array<int>();
        for (i = 0; i < m_N; i++) deg.Insert(0);
        for (i = 0; i < e; i++) {
            a = ta.Get(i);
            b = tb.Get(i);
            deg.Set(a, deg.Get(a) + 1);
            deg.Set(b, deg.Get(b) + 1);
        }
        m_AdjStart = new array<int>();
        int acc = 0;
        for (i = 0; i < m_N; i++) { m_AdjStart.Insert(acc); acc = acc + deg.Get(i); }
        m_AdjStart.Insert(acc);
        m_AdjV = new array<int>();
        m_AdjW = new array<float>();
        m_AdjEdge = new array<int>();
        for (i = 0; i < acc; i++) { m_AdjV.Insert(0); m_AdjW.Insert(0.0); m_AdjEdge.Insert(-1); }
        array<int> cur = new array<int>();
        for (i = 0; i < m_N; i++) cur.Insert(m_AdjStart.Get(i));
        for (i = 0; i < e; i++) {
            a = ta.Get(i);
            b = tb.Get(i);
            float w = tw.Get(i);
            int pa = cur.Get(a); m_AdjV.Set(pa, b); m_AdjW.Set(pa, w); m_AdjEdge.Set(pa, i); cur.Set(a, pa + 1);
            int pb = cur.Get(b); m_AdjV.Set(pb, a); m_AdjW.Set(pb, w); m_AdjEdge.Set(pb, i); cur.Set(b, pb + 1);
        }
        m_E = e;
        m_Loaded = true;
        BZBusLog.Info("[RoadGraph] cargado OK: " + m_N + " nodos, " + e + " aristas");
        return true;
    }

    // -------- carga la GEOMETRIA de aristas (pts + ancho) para armar la traza (paso 3) --------
    //  Formato: linea 1 = "<nAristas>"; por arista: header "a,b,w,npts" + npts lineas "x,z" (orientadas a->b).
    bool LoadGeom(string path) {
        m_GeomLoaded = false;
        FileHandle f = OpenFile(path, FileMode.READ);
        if (!f) { BZBusLog.Info("[RoadGraph] no se pudo abrir geom " + path); return false; }
        TStringArray parts = new TStringArray();
        string line;
        if (FGets(f, line) < 0) { CloseFile(f); return false; }
        SplitKeepEmpty(line, parts);
        int ne = parts[0].ToInt();

        m_EStart = new array<int>();
        m_EPx = new array<float>();
        m_EPz = new array<float>();
        m_EW = new array<float>();
        m_Ea = new array<int>();
        m_Eb = new array<int>();
        int acc = 0;
        int i;
        int np;
        int j;
        for (i = 0; i < ne; i++) {
            if (FGets(f, line) < 0) break;
            SplitKeepEmpty(line, parts);
            if (parts.Count() < 4) continue;
            m_Ea.Insert(parts[0].ToInt());
            m_Eb.Insert(parts[1].ToInt());
            m_EW.Insert(parts[2].ToFloat());
            np = parts[3].ToInt();
            m_EStart.Insert(acc);
            for (j = 0; j < np; j++) {
                if (FGets(f, line) < 0) break;
                SplitKeepEmpty(line, parts);
                if (parts.Count() < 2) continue;
                m_EPx.Insert(parts[0].ToFloat());
                m_EPz.Insert(parts[1].ToFloat());
                acc = acc + 1;
            }
        }
        m_EStart.Insert(acc);   // sentinela (m_EStart[E] = total pts)
        CloseFile(f);
        m_GeomLoaded = (m_Ea.Count() == ne && m_Ea.Count() == m_E);
        BZBusLog.Info("[RoadGraph] geom cargada: " + m_Ea.Count() + " aristas, " + m_EPx.Count() + " pts (ok=" + m_GeomLoaded + ")");
        return m_GeomLoaded;
    }

    bool IsGeomLoaded() { return m_GeomLoaded; }

    // -------- id de la arista que conecta u<->v (o -1) --------
    int FindEdge(int u, int v) {
        int s0 = m_AdjStart.Get(u);
        int s1 = m_AdjStart.Get(u + 1);
        int k;
        for (k = s0; k < s1; k++) {
            if (m_AdjV.Get(k) == v) return m_AdjEdge.Get(k);
        }
        return -1;
    }

    // -------- reconstruye la TRAZA (array de posiciones mundo) desde el path de nodos --------
    //  nodePath viene de Route en orden dst..src (apilado). Recorro al reves (src->dst) y concateno la
    //  geometria REAL de cada arista (los pts originales, NO los nodos snapeados) orientada al sentido de
    //  marcha. El punto de empalme compartido se saltea (evita duplicar). RAW: sin fillet ni offset (3c/3d).
    array<vector> BuildTraza(array<int> nodePath) {
        array<vector> tz = new array<vector>();
        array<float>  tw = new array<float>();   // ancho de calzada por punto (para el offset de carril 3d)
        if (!m_GeomLoaded || !nodePath || nodePath.Count() < 2) return tz;
        int m = nodePath.Count();
        int idx;
        for (idx = m - 1; idx > 0; idx--) {
            int u = nodePath.Get(idx);        // desde
            int v = nodePath.Get(idx - 1);    // hacia
            int ei = FindEdge(u, v);
            if (ei < 0) continue;
            int s = m_EStart.Get(ei);
            int en = m_EStart.Get(ei + 1);
            bool fwd = (m_Ea.Get(ei) == u);   // pts orientados a->b; si u==a, forward
            int cnt = en - s;
            int t;
            for (t = 0; t < cnt; t++) {
                int pi;
                if (fwd) pi = s + t;
                else pi = en - 1 - t;
                if (t == 0 && tz.Count() > 0) continue;   // no duplicar el empalme
                tz.Insert(Vector(m_EPx.Get(pi), 0, m_EPz.Get(pi)));
                tw.Insert(m_EW.Get(ei));                   // ancho de ESTA arista, paralelo a tz
            }
        }
        // paso 3d: offset al carril DERECHO en calles anchas (mano derecha); centro en angostas (tierra).
        array<vector> off = ApplyLaneOffset(tz, tw);
        // paso 3c: el centerline crudo viene SERRUCHADO (los caminos son varios road-objects encadenados
        // -> crestas en las curvitas) y con puntos RALOS (~17m) -> el pure-pursuit corta codos y oscila
        // (Boris "roja" serruchada vs humano "azul" limpia, visto en el editor). Fix: resample uniforme
        // (puntos densos) + smoothing (media movil) -> linea de manejo LIMPIA. Validado offline vs el grafo.
        array<vector> rs = Resample(off, 2.5);
        return Smooth(rs, 9);
    }

    // offset al carril DERECHO del sentido de marcha por w/4 (solo calles anchas w>=8). Centra en angostas.
    // perpendicular-derecha de la direccion (dx,dz) en DayZ (x=E, z=N) = (dz, -dx).
    array<vector> ApplyLaneOffset(array<vector> pts, array<float> widths) {
        int n = pts.Count();
        if (n < 2 || widths.Count() != n) return pts;
        array<vector> res = new array<vector>();
        int i;
        for (i = 0; i < n; i++) {
            float w = widths.Get(i);
            if (w < 8.0) {
                res.Insert(pts.Get(i));
                continue;
            }
            int a = i - 1;
            int b = i + 1;
            if (a < 0) a = 0;
            if (b > n - 1) b = n - 1;
            vector dir = pts.Get(b) - pts.Get(a);
            float dl = Math.Sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
            if (dl < 0.01) {
                res.Insert(pts.Get(i));
                continue;
            }
            float rx = dir[2] / dl;
            float rz = -dir[0] / dl;
            float ofs = w * 0.25;
            vector p = pts.Get(i);
            res.Insert(Vector(p[0] + rx * ofs, 0, p[2] + rz * ofs));
        }
        return res;
    }

    // resample a espaciado uniforme (step m): densifica -> el controlador sigue pegado, no corta codos.
    // OJO: 'out' es palabra RESERVADA en Enforce (modificador de parametros) -> NO se puede usar de nombre.
    array<vector> Resample(array<vector> pts, float step) {
        array<vector> res = new array<vector>();
        int n = pts.Count();
        if (n < 2) return pts;
        res.Insert(pts.Get(0));
        vector cur = pts.Get(0);
        int i = 1;
        int guard = 0;
        int guardMax = 40 * n + 200000;
        while (i < n && guard < guardMax) {
            guard = guard + 1;
            vector nxt = pts.Get(i);
            float d = vector.Distance(cur, nxt);
            if (d < step) {
                cur = nxt;
                i = i + 1;
                continue;
            }
            float t = step / d;
            cur = cur + (nxt - cur) * t;
            res.Insert(cur);
        }
        vector last = pts.Get(n - 1);
        if (vector.Distance(res.Get(res.Count() - 1), last) > 0.5) res.Insert(last);
        return res;
    }

    // smoothing por media movil (win pts): limpia las crestas del centerline. Ancla los extremos.
    array<vector> Smooth(array<vector> pts, int win) {
        int n = pts.Count();
        if (n < 3 || win < 3) return pts;
        int h = win / 2;
        array<vector> res = new array<vector>();
        int i;
        for (i = 0; i < n; i++) {
            int lo = i - h;
            int hi = i + h;
            if (lo < 0) lo = 0;
            if (hi > n - 1) hi = n - 1;
            vector sum = "0 0 0";
            int c = 0;
            int j;
            for (j = lo; j <= hi; j++) {
                sum = sum + pts.Get(j);
                c = c + 1;
            }
            res.Insert(sum * (1.0 / c));
        }
        res.Set(0, pts.Get(0));
        res.Set(n - 1, pts.Get(n - 1));
        return res;
    }

    // -------- nodo mas cercano a un punto mundo (scan lineal; se llama 1 vez por ruta) --------
    int NearestNode(float x, float z) {
        int best = -1;
        float bestd = 1000000000.0;
        int i;
        for (i = 0; i < m_N; i++) {
            float dx = m_NX.Get(i) - x;
            float dz = m_NZ.Get(i) - z;
            float dd = dx * dx + dz * dz;
            if (dd < bestd) { bestd = dd; best = i; }
        }
        return best;
    }

    // -------- Dijkstra src->dst con heap binario (lazy deletion). Devuelve path o null. --------
    array<int> Route(int src, int dst, out float outCost) {
        outCost = -1.0;
        if (!m_Loaded || src < 0 || dst < 0) return null;
        int i;

        array<float> dist = new array<float>();
        array<int>   prev = new array<int>();
        array<bool>  done = new array<bool>();
        for (i = 0; i < m_N; i++) { dist.Insert(1000000000.0); prev.Insert(-1); done.Insert(false); }
        dist.Set(src, 0.0);

        array<int>   hn = new array<int>();
        array<float> hk = new array<float>();
        int hsize = 0;
        hn.Insert(src); hk.Insert(0.0); hsize = 1;

        int guard = 0;
        int guardMax = 8 * (m_N + 1);
        while (hsize > 0 && guard < guardMax) {
            guard = guard + 1;
            int u = hn.Get(0);
            float ud = hk.Get(0);
            hsize = hsize - 1;
            hn.Set(0, hn.Get(hsize)); hk.Set(0, hk.Get(hsize));
            int p = 0;
            while (true) {
                int l = 2 * p + 1;
                int r = 2 * p + 2;
                int sm = p;
                if (l < hsize && hk.Get(l) < hk.Get(sm)) sm = l;
                if (r < hsize && hk.Get(r) < hk.Get(sm)) sm = r;
                if (sm == p) break;
                int tn = hn.Get(p); hn.Set(p, hn.Get(sm)); hn.Set(sm, tn);
                float tk = hk.Get(p); hk.Set(p, hk.Get(sm)); hk.Set(sm, tk);
                p = sm;
            }

            if (done.Get(u)) continue;
            done.Set(u, true);
            if (u == dst) break;

            int s0 = m_AdjStart.Get(u);
            int s1 = m_AdjStart.Get(u + 1);
            int k;
            for (k = s0; k < s1; k++) {
                int v = m_AdjV.Get(k);
                if (done.Get(v)) continue;
                float nd = ud + m_AdjW.Get(k);
                if (nd < dist.Get(v)) {
                    dist.Set(v, nd);
                    prev.Set(v, u);
                    if (hsize < hn.Count()) { hn.Set(hsize, v); hk.Set(hsize, nd); }
                    else { hn.Insert(v); hk.Insert(nd); }
                    int c = hsize;
                    hsize = hsize + 1;
                    while (c > 0) {
                        int par = (c - 1) / 2;
                        if (hk.Get(par) <= hk.Get(c)) break;
                        int sn = hn.Get(par); hn.Set(par, hn.Get(c)); hn.Set(c, sn);
                        float sk = hk.Get(par); hk.Set(par, hk.Get(c)); hk.Set(c, sk);
                        c = par;
                    }
                }
            }
        }

        if (!done.Get(dst) && dst != src) return null;
        // GOTCHA (2026-08-15, MEDIDO): el cuerpo del while NO puede ir en UNA linea con 'if (...) break;'
        // seguido de mas sentencias -> Enforce se come lo que sigue al break ('n = prev.Get(n)' NO corre) y
        // el walk se clava en dst (prev estaba BIEN: DBG2 dio prev[7001]=6971). Mismo gotcha que
        // 'oneline-conditional-return' pero con break. Fix: cuerpo en varias lineas. Ver [[feedback_enforce_oneline_conditional_return]].
        // (Insert apila -> path en orden REVERSO dst..src; el Count() es correcto; se invierte en el paso 3.)
        array<int> path = new array<int>();
        int n = dst;
        int back = 0;
        while (n != -1 && back < m_N + 1) {
            path.Insert(n);
            if (n == src) break;
            n = prev.Get(n);
            back = back + 1;
        }
        outCost = dist.Get(dst);
        return path;
    }

    // -------- posicion mundo de un nodo (y=0; el caller resuelve la altura) --------
    vector NodePos(int i) {
        if (i < 0 || i >= m_N) return Vector(0, 0, 0);
        return Vector(m_NX.Get(i), 0, m_NZ.Get(i));
    }

    // -------- nodo cuya distancia-POR-CAMINO desde srcN esta mas cerca de targetM (Dijkstra completo).
    //          Sirve para elegir un destino de test a ~N km de donde esta el jugador. --------
    int NodeAtRoadDistance(int srcN, float targetM) {
        if (!m_Loaded || srcN < 0) return -1;
        int i;
        array<float> dist = new array<float>();
        array<bool>  done = new array<bool>();
        for (i = 0; i < m_N; i++) { dist.Insert(1000000000.0); done.Insert(false); }
        dist.Set(srcN, 0.0);
        array<int>   hn = new array<int>();
        array<float> hk = new array<float>();
        int hsize = 0;
        hn.Insert(srcN); hk.Insert(0.0); hsize = 1;
        int best = srcN;
        float bestDiff = targetM;
        int guard = 0;
        int guardMax = 8 * (m_N + 1);
        while (hsize > 0 && guard < guardMax) {
            guard = guard + 1;
            int u = hn.Get(0);
            float ud = hk.Get(0);
            hsize = hsize - 1;
            hn.Set(0, hn.Get(hsize)); hk.Set(0, hk.Get(hsize));
            int p = 0;
            while (true) {
                int l = 2 * p + 1;
                int r = 2 * p + 2;
                int sm = p;
                if (l < hsize && hk.Get(l) < hk.Get(sm)) sm = l;
                if (r < hsize && hk.Get(r) < hk.Get(sm)) sm = r;
                if (sm == p) break;
                int tn = hn.Get(p); hn.Set(p, hn.Get(sm)); hn.Set(sm, tn);
                float tk = hk.Get(p); hk.Set(p, hk.Get(sm)); hk.Set(sm, tk);
                p = sm;
            }
            if (done.Get(u)) continue;
            done.Set(u, true);
            float diff = Math.AbsFloat(ud - targetM);
            if (diff < bestDiff) { bestDiff = diff; best = u; }
            int s0 = m_AdjStart.Get(u);
            int s1 = m_AdjStart.Get(u + 1);
            int k;
            for (k = s0; k < s1; k++) {
                int v = m_AdjV.Get(k);
                if (done.Get(v)) continue;
                float nd = ud + m_AdjW.Get(k);
                if (nd < dist.Get(v)) {
                    dist.Set(v, nd);
                    if (hsize < hn.Count()) { hn.Set(hsize, v); hk.Set(hsize, nd); }
                    else { hn.Insert(v); hk.Insert(nd); }
                    int c = hsize;
                    hsize = hsize + 1;
                    while (c > 0) {
                        int par = (c - 1) / 2;
                        if (hk.Get(par) <= hk.Get(c)) break;
                        int sn = hn.Get(par); hn.Set(par, hn.Get(c)); hn.Set(c, sn);
                        float sk = hk.Get(par); hk.Set(par, hk.Get(c)); hk.Set(c, sk);
                        c = par;
                    }
                }
            }
        }
        return best;
    }

    // -------- TEST (paso 1b): rutear from->to y loguear en el RPT --------
    void TestRoute(float fromX, float fromZ, float toX, float toZ) {
        if (!m_Loaded) { BZBusLog.Info("[RoadGraph] TestRoute: grafo NO cargado"); return; }
        int a = NearestNode(fromX, fromZ);
        int b = NearestNode(toX, toZ);
        float cost;
        array<int> path = Route(a, b, cost);
        if (!path || path.Count() == 0) {
            BZBusLog.Info("[RoadGraph] TestRoute SIN RUTA (a=" + a + " b=" + b + ")");
            return;
        }
        float km = cost / 1000.0;
        BZBusLog.Info("############  [RoadGraph] RUTA A->B  ############");
        BZBusLog.Info("[RoadGraph]   from(" + fromX + "," + fromZ + ") -> nodo " + a + " (" + m_NX.Get(a) + "," + m_NZ.Get(a) + ")");
        BZBusLog.Info("[RoadGraph]   to(" + toX + "," + toZ + ") -> nodo " + b + " (" + m_NX.Get(b) + "," + m_NZ.Get(b) + ")");
        BZBusLog.Info("[RoadGraph]   RUTA: " + path.Count() + " nodos, " + cost + " m (" + km + " km)  [offline dio 167 nodos / 27.19 km]");

        // paso 3b: reconstruir la TRAZA (geometria real de las aristas) y loguear largo/puntos.
        array<vector> tz = BuildTraza(path);
        if (tz.Count() < 2) {
            BZBusLog.Info("[RoadGraph]   TRAZA vacia (geom cargada=" + m_GeomLoaded + ")");
            return;
        }
        float tlen = 0.0;
        int ti;
        for (ti = 1; ti < tz.Count(); ti++) {
            tlen = tlen + vector.Distance(tz.Get(ti), tz.Get(ti - 1));
        }
        vector p0 = tz.Get(0);
        vector pN = tz.Get(tz.Count() - 1);
        BZBusLog.Info("[RoadGraph]   TRAZA: " + tz.Count() + " pts, " + tlen + " m | ini(" + p0[0] + "," + p0[2] + ") fin(" + pN[0] + "," + pN[2] + ")");
    }
}
