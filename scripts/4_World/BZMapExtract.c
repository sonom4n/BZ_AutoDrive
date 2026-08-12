// ============================================================================
//  BZMapExtract - EXTRACTOR COMPLETO DE MAPA, agnostico del mundo. (Sonom4n, 2026-07-15)
//  Reemplaza a BZRoadScan(v3)+BZObjectScanAll+BZRailBridgeScan+BZWaterFineScan:
//    * DIMENSIONES AUTOMATICAS: GetWorld().GetWorldSize() + GetWorldName() -> nada hardcodeado.
//      (los scanners viejos tenian 15360 clavado = solo Chernarus)
//    * UN SOLO BARRIDO de objetos: GetObjectsAtPosition3D es la llamada CARA y los 3 scanners
//      viejos la hacian cada uno por su cuenta = 3x el trabajo. Aca se sondea una vez y se
//      clasifica cada objeto (road / rail / puente / estructura).
//    * Salidas con el nombre del mundo -> varios mapas conviven sin pisarse.
//  Frame-spread (no cuelga el server). Dedup OFFLINE por (tipo,pos), como siempre.
//
//  Salidas en $profile:BZ_AutoDrive_PathLogger\   (<world> = chernarusplus | enoch | sakhal ...)
//    <world>_meta.csv        world,size          <- el pipeline offline lee de aca las dimensiones
//    <world>_roads.csv       x,z,yaw,debug
//    <world>_objects.csv     type,scenery,building,x,y,z,yaw,bw,bl,bh,debug
//    <world>_railbridge.csv  kind,x,y,z,yaw,bw,bl,bh,debug
//    <world>_water.csv       x,z,kind            <- pond(rios) al paso fino; mar submuestreado
//    <world>_vegetation.csv  kind,x,z,r,model    <- 2026-07-16: arboles/arbustos (dedup EN VIVO)
//    <world>_height.csv      grid de alturas     <- 2026-07-16: fase 3, para relieve/hillshade
//
//  FONDO PROPIO (2026-07-16): vegetacion + altura se agregaron para poder dibujar un mapa
//  100% de MEDICION PROPIA (publicable) en vez de depender del raster satelital extraido del
//  juego, que NO se puede redistribuir. Ver reglas de uso de contenido de Bohemia.
//
//  USO: server VACIO -> consola:  BZMapExtract.Begin()
// ============================================================================
class BZMapExtract {
    private static ref BZMapExtract s_inst;
    private static bool s_done = false;

    private string m_world;
    private float  m_size;
    private int    m_phase;          // 0 = objetos (grueso) · 1 = agua (fino) · 2 = altura (relieve)
    private float  m_step, m_radius, m_curX, m_curZ;
    private FileHandle m_fRoad, m_fObj, m_fRB, m_fWater, m_fVeg, m_fH;
    private int m_nRoad, m_nObj, m_nRB, m_nPond, m_nSea, m_nVeg, m_nH, m_probes;
    private string m_hRow;           // fase 2: se acumula UNA fila por columna X y se vuelca al saltar
    private ref array<Object>    m_objs;
    private ref array<CargoBase> m_prox;

    // pasos: objetos 20m/r16 (capta rail y calles finas) · agua 4m (rios de 5-15m) · altura 10m
    // STATIC: Begin() es static y las lee -> un const de instancia no es visible desde ahi
    private static const float STEP_OBJ  = 20.0;
    private static const float RAD_OBJ   = 16.0;
    private static const float STEP_WATER= 4.0;
    // relieve 5m: la grilla NATIVA del terreno ronda los 7.5m => a 5m se captura todo lo que hay
    // (a 10m se sub-muestreaba y el zoom se veia lavado). 5m = 3073x3073 = 9.4M puntos, ~2 min.
    private static const float STEP_H    = 5.0;
    private static const int   SEA_EVERY = 20;   // el mar se submuestrea a 20m (a 4m son ~3M de filas al pedo)

    static void Begin() {
        if (s_inst || s_done) return;
        s_done = true;
        BZMapExtract inst = new BZMapExtract();
        string wname = "empty";                    // mismo patron que el source del juego
        GetGame().GetWorldName(wname);             // (autotestfixture.c: ToLower() muta la local)
        wname.ToLower();
        inst.m_world = wname;
        inst.m_size = GetGame().GetWorld().GetWorldSize();     // <- DIMENSION AUTOMATICA
        if (inst.m_size <= 0) { BZBusLog.Err("[EXTRACT] GetWorldSize() invalido: " + inst.m_size); return; }
        inst.m_phase = 0;
        inst.m_step = STEP_OBJ; inst.m_radius = RAD_OBJ;
        inst.m_curX = 0; inst.m_curZ = 0;
        inst.m_objs = new array<Object>;
        inst.m_prox = new array<CargoBase>;

        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        string pre = dir + inst.m_world + "_";
        FileHandle fm = OpenFile(pre + "meta.csv", FileMode.WRITE);
        if (fm) { FPrint(fm, "world,size\n"); FPrint(fm, inst.m_world + "," + inst.m_size + "\n"); CloseFile(fm); }
        inst.m_fRoad  = OpenFile(pre + "roads.csv",      FileMode.WRITE);
        inst.m_fObj   = OpenFile(pre + "objects.csv",    FileMode.WRITE);
        inst.m_fRB    = OpenFile(pre + "railbridge.csv", FileMode.WRITE);
        inst.m_fWater = OpenFile(pre + "water.csv",      FileMode.WRITE);
        inst.m_fVeg   = OpenFile(pre + "vegetation.csv", FileMode.WRITE);
        inst.m_fH     = OpenFile(pre + "height.csv",     FileMode.WRITE);
        if (!inst.m_fRoad || !inst.m_fObj || !inst.m_fRB || !inst.m_fWater || !inst.m_fVeg || !inst.m_fH) {
            BZBusLog.Err("[EXTRACT] no pude crear los CSV en " + dir); return;
        }
        FPrint(inst.m_fRoad,  "x,z,yaw,debug\n");
        FPrint(inst.m_fObj,   "type,scenery,building,x,y,z,yaw,bw,bl,bh,debug\n");
        FPrint(inst.m_fRB,    "kind,x,y,z,yaw,bw,bl,bh,debug\n");
        FPrint(inst.m_fWater, "x,z,kind\n");
        FPrint(inst.m_fVeg,   "kind,x,z,r,model\n");
        // height: cabecera con la geometria de la grilla; despues UNA fila por columna X,
        // con las alturas de z=0..size separadas por coma (mucho mas compacto que x,z,y por fila).
        string hHdr = "step," + STEP_H + ",size," + inst.m_size;
        FPrint(inst.m_fH, hHdr + "\n");
        s_inst = inst;
        BZBusLog.Info("[EXTRACT] mundo=" + inst.m_world + " size=" + inst.m_size + "m | fase 1/2 OBJETOS (un solo barrido)");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_inst.Tick, 0, true);
    }

    void Tick() {
        int budget;
        if (m_phase == 0) budget = 200; else budget = 2500;   // agua/altura son baratas (sin objetos)
        while (budget > 0) {
            budget = budget - 1;
            if (m_phase == 0)      ProbeObjects(m_curX, m_curZ);
            else if (m_phase == 1) ProbeWater(m_curX, m_curZ);
            else                   ProbeHeight(m_curX, m_curZ);
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_size) {
                if (m_phase == 2) FlushHeightRow();   // se completo la columna X -> volcarla
                m_curZ = 0;
                m_curX = m_curX + m_step;
                if (m_curX > m_size) { NextPhase(); return; }
            }
        }
        int every = 10000; if (m_phase > 0) every = 400000;
        if (m_probes % every < budget + 2500) {
            // concat en UNA linea por sentencia: Enforce no parsea la expresion partida entre lineas
            string msgTk = "[EXTRACT] fase" + (m_phase + 1) + " x=" + m_curX + "/" + m_size;
            msgTk = msgTk + " road=" + m_nRoad + " obj=" + m_nObj + " veg=" + m_nVeg;
            msgTk = msgTk + " rail/br=" + m_nRB + " agua=" + (m_nPond + m_nSea) + " alt=" + m_nH;
            BZBusLog.Info(msgTk);
        }
    }

    void NextPhase() {
        if (m_phase == 0) {
            m_phase = 1; m_step = STEP_WATER; m_curX = 0; m_curZ = 0;
            string msgP1 = "[EXTRACT] fase 1 OK (road=" + m_nRoad + " obj=" + m_nObj + " veg=" + m_nVeg + ")";
            msgP1 = msgP1 + " | fase 2/3 AGUA paso " + STEP_WATER + "m";
            BZBusLog.Info(msgP1);
            return;
        }
        if (m_phase == 1) {
            m_phase = 2; m_step = STEP_H; m_curX = 0; m_curZ = 0; m_hRow = "";
            string msgP2 = "[EXTRACT] fase 2 OK (agua=" + (m_nPond + m_nSea) + ")";
            msgP2 = msgP2 + " | fase 3/3 ALTURA paso " + STEP_H + "m (relieve)";
            BZBusLog.Info(msgP2);
            return;
        }
        Finish();
    }

    // ALTURA: SurfaceY es nativa y barata (no consulta objetos). Se acumula la columna entera
    // y se vuelca de una: una fila por X con todas las Z -> archivo chico y facil de leer offline.
    void ProbeHeight(float x, float z) {
        float y = GetGame().SurfaceY(x, z);
        if (m_hRow != "") m_hRow = m_hRow + ",";
        // *0.1 y NO /10: si Math.Round devolviera int, "/10" seria division ENTERA y perderia
        // el decimal en silencio. Multiplicar por 0.1 (float) no tiene esa ambiguedad.
        m_hRow = m_hRow + Math.Round(y * 10) * 0.1;     // 1 decimal: 10cm sobra para relieve
        m_nH = m_nH + 1;
    }
    void FlushHeightRow() {
        if (m_hRow != "") FPrint(m_fH, m_hRow + "\n");
        m_hRow = "";
    }

    // UN sondeo, TODAS las clasificaciones (antes eran 3 barridos separados)
    void ProbeObjects(float x, float z) {
        float y = GetGame().SurfaceY(x, z);
        m_objs.Clear(); m_prox.Clear();
        GetGame().GetObjectsAtPosition3D(Vector(x, y, z), m_radius, m_objs, m_prox);
        string bRoad = "", bObj = "", bRB = "", bVeg = "";
        foreach (Object obj : m_objs) {
            if (!obj) continue;
            string dbg = obj.GetDebugName();
            dbg.Replace(",", " ");
            vector p = obj.GetPosition();
            vector o = obj.GetOrientation();
            if (IsRoadPart(dbg)) {                                  // 1) calzada
                bRoad = bRoad + p[0] + "," + p[2] + "," + o[0] + "," + dbg + "\n";
                m_nRoad = m_nRoad + 1;
                continue;
            }
            string kind = RailBridgeKind(dbg);
            float bw = 0, bl = 0, bh = 0;
            vector mm[2];
            if (obj.GetCollisionBox(mm)) { bw = mm[1][0]-mm[0][0]; bl = mm[1][2]-mm[0][2]; bh = mm[1][1]-mm[0][1]; }
            if (kind != "") {                                       // 2) rail / puente
                bRB = bRB + kind + "," + p[0] + "," + p[1] + "," + p[2] + "," + o[0];
                bRB = bRB + "," + bw + "," + bl + "," + bh + "," + dbg + "\n";
                m_nRB = m_nRB + 1;
                continue;
            }
            if (IsStructure(obj, dbg)) {                            // 3) estructura
                int sc = 0; if (obj.IsScenery())  sc = 1;
                int bd = 0; if (obj.IsBuilding()) bd = 1;
                bObj = bObj + obj.GetType() + "," + sc + "," + bd + "," + p[0] + "," + p[1] + "," + p[2] + "," + o[0];
                bObj = bObj + "," + bw + "," + bl + "," + bh + "," + dbg + "\n";
                m_nObj = m_nObj + 1;
                continue;
            }
            // 4) VEGETACION (2026-07-16). Antes se descartaba: IsStructure devolvia false y el
            // arbol se perdia. NO era limitacion del motor, era este filtro.
            string veg = VegKind(ModelOf(dbg));
            if (veg != "") {
                if (!OwnsCell(p[0], p[2], x, z)) continue;   // dedup EN VIVO: lo escribe una sola probe
                float rad = bw; if (bl > rad) rad = bl;
                rad = rad * 0.5;                              // radio de copa desde la caja de colision
                bVeg = bVeg + veg + "," + p[0] + "," + p[2] + "," + Math.Round(rad * 10) * 0.1;
                bVeg = bVeg + "," + ModelOf(dbg) + "\n";
                m_nVeg = m_nVeg + 1;
            }
        }
        if (bRoad != "") FPrint(m_fRoad, bRoad);
        if (bObj  != "") FPrint(m_fObj,  bObj);
        if (bRB   != "") FPrint(m_fRB,   bRB);
        if (bVeg  != "") FPrint(m_fVeg,  bVeg);
    }

    // DEDUP EN VIVO: paso 20m con radio 16 => cada objeto lo ven 2-4 probes. Se escribe SOLO desde
    // la probe DUENA de su celda (la mas cercana de la grilla). Un objeto dista <=14.14m del centro
    // de su celda (media diagonal de 20x20) < radio 16 => su duena SIEMPRE lo ve. Sin memoria y O(1).
    bool OwnsCell(float px, float pz, float cx, float cz) {
        float qx = Math.Floor(px / STEP_OBJ + 0.5) * STEP_OBJ;
        float qz = Math.Floor(pz / STEP_OBJ + 0.5) * STEP_OBJ;
        float dx = qx - cx; if (dx < 0) dx = -dx;
        float dz = qz - cz; if (dz < 0) dz = -dz;
        return (dx < 0.5 && dz < 0.5);
    }

    // agua: pond (rios) al paso fino; mar submuestreado. Pond PRIMERO (los rios son lo que importa).
    void ProbeWater(float x, float z) {
        if (GetGame().SurfaceIsPond(x, z)) {
            FPrint(m_fWater, "" + x + "," + z + ",pond\n"); m_nPond = m_nPond + 1;
        } else if (GetGame().SurfaceIsSea(x, z)) {
            int xi = (int)x; int zi = (int)z;
            if (xi % SEA_EVERY == 0 && zi % SEA_EVERY == 0) { FPrint(m_fWater, "" + x + "," + z + ",sea\n"); m_nSea = m_nSea + 1; }
        }
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
    // INCLUSIVO a proposito: "rail" tambien matchea "t-rail" / "t-rail-er" (arboles, postes, carteles).
    // La clasificacion FINA se hace offline con el nombre exacto del modelo (viene en debug).
    string RailBridgeKind(string dbg) {
        if (dbg.Contains("rail"))   return "rail";
        if (dbg.Contains("bridge")) return "bridge";
        if (dbg.Contains("Bridge")) return "bridge";
        if (dbg.Contains("most_"))  return "bridge";
        return "";
    }
    bool IsStructure(Object obj, string dbg) {
        if (obj.IsBuilding()) return true;
        if (dbg.Contains("Wreck") || dbg.Contains("wreck")) return true;
        if (dbg.Contains("wall")  || dbg.Contains("Wall"))  return true;
        if (dbg.Contains("fence") || dbg.Contains("Fence")) return true;
        if (dbg.Contains("zabor") || dbg.Contains("plot"))  return true;
        // CARTELES (2026-07-28). Antes se perdian EN SILENCIO: no son IsBuilding() y no
        // matcheaban ningun Contains -> el objeto se descartaba, igual que pasaba con la
        // vegetacion. Se necesitan la posicion y el YAW para saber hacia donde mira cada
        // cartel y poder calcular la flecha real de cada destino.
        // OJO: los Sign_Rail_* y TrailSign_* ya caen antes en RailBridgeKind() por su
        // Contains("rail"); estos son los Directional y los Settlements.
        if (dbg.Contains("sign")  || dbg.Contains("Sign"))  return true;
        return false;
    }
    // "12345: dir\modelo.p3d" -> "modelo". OJO: NO sacar espacios (romperia nombres tipo "mud_10 25").
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
    // Vegetacion por PREFIJO del modelo (nomenclatura Bohemia): t_ = arbol, b_ = arbusto.
    // Por prefijo y no por Contains(): "wall_stoned" no debe dar arbol. La clasificacion FINA
    // (especie) se hace offline con el nombre del modelo, que va en la ultima columna.
    string VegKind(string mdl) {
        if (mdl.IndexOf("t_") == 0) return "t";
        if (mdl.IndexOf("b_") == 0) return "b";
        if (mdl.Contains("bush")) return "b";
        if (mdl.Contains("tree")) return "t";
        return "";
    }

    void Finish() {
        FlushHeightRow();                                  // la ultima columna todavia sin volcar
        if (m_fRoad)  { CloseFile(m_fRoad);  m_fRoad = null; }
        if (m_fObj)   { CloseFile(m_fObj);   m_fObj = null; }
        if (m_fRB)    { CloseFile(m_fRB);    m_fRB = null; }
        if (m_fWater) { CloseFile(m_fWater); m_fWater = null; }
        if (m_fVeg)   { CloseFile(m_fVeg);   m_fVeg = null; }
        if (m_fH)     { CloseFile(m_fH);     m_fH = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        string msgFin = "[EXTRACT] COMPLETO mundo=" + m_world + " size=" + m_size + "m | " + m_probes + " probes";
        msgFin = msgFin + " | road=" + m_nRoad + " obj=" + m_nObj + " veg=" + m_nVeg + " rail/br=" + m_nRB;
        msgFin = msgFin + " pond=" + m_nPond + " sea=" + m_nSea + " alt=" + m_nH;
        msgFin = msgFin + " -> " + m_world + "_{meta,roads,objects,railbridge,water,vegetation,height}.csv";
        BZBusLog.Info(msgFin);
        s_inst = null;
    }
}
