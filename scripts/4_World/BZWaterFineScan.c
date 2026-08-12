// ============================================================================
//  BZWaterFineScan - AGUA FINA (RIOS). (Sonom4n, 2026-07-14)
//  El scan de agua de BZRailBridgeScan va a 20m: capta el MAR pero los RIOS
//  (5-15m de ancho) se cuelan entre muestras y quedan PUNTEADOS.
//  Esto barre SOLO agua a 4m: SurfaceIsPond/IsSea son nativas y baratas (no se
//  consultan objetos), asi que el presupuesto por frame es alto y tarda ~1-2 min.
//  ECONOMIA DEL CSV: pond (rios) se escribe a 4m; el MAR se submuestrea a 20m
//  (a 4m serian ~3M de filas y no aporta detalle util).
//    salida: $profile:BZ_AutoDrive_PathLogger\water_fine.csv  (x,z,kind)
//  USO: BZWaterFineScan.Begin(0,0) o via RoadScanOnBoot (TriggerBootMapScan).
// ============================================================================
class BZWaterFineScan {
    private static ref BZWaterFineScan s_inst;
    private static bool s_done = false;
    private float m_max, m_step, m_curX, m_curZ;
    private FileHandle m_fw;
    private int m_pond, m_sea, m_probes;

    static void Begin(float mapSize, float step) {
        if (s_inst || s_done) return;
        s_done = true;
        if (mapSize <= 0) mapSize = 15360.0;
        if (step   <= 0) step   = 4.0;
        BZWaterFineScan inst = new BZWaterFineScan();
        inst.m_max = mapSize; inst.m_step = step;
        inst.m_curX = 0; inst.m_curZ = 0;
        inst.m_pond = 0; inst.m_sea = 0; inst.m_probes = 0;
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        inst.m_fw = OpenFile(dir + "water_fine.csv", FileMode.WRITE);
        if (!inst.m_fw) { BZBusLog.Err("[WATERF] no pude crear water_fine.csv"); return; }
        FPrint(inst.m_fw, "x,z,kind\n");
        s_inst = inst;
        BZBusLog.Info("[WATERF] agua FINA step=" + step + "m (rios); mar submuestreado a 20m");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(s_inst.Tick, 0, true);
    }

    void Tick() {
        int budget = 2500;          // barato: solo SurfaceIsPond/IsSea, sin objetos
        string buf = "";
        while (budget > 0) {
            budget = budget - 1;
            float x = m_curX; float z = m_curZ;
            // pond PRIMERO: los rios son lo que buscamos
            if (GetGame().SurfaceIsPond(x, z)) {
                buf = buf + x + "," + z + ",pond\n";
                m_pond = m_pond + 1;
            } else if (GetGame().SurfaceIsSea(x, z)) {
                int xi = (int)x; int zi = (int)z;
                if (xi % 20 == 0 && zi % 20 == 0) { buf = buf + x + "," + z + ",sea\n"; m_sea = m_sea + 1; }
            }
            m_probes = m_probes + 1;
            m_curZ = m_curZ + m_step;
            if (m_curZ > m_max) {
                m_curZ = 0;
                m_curX = m_curX + m_step;
                if (m_curX > m_max) { if (buf != "") FPrint(m_fw, buf); Finish(); return; }
            }
        }
        if (buf != "") FPrint(m_fw, buf);
        if (m_probes % 400000 < 2500) BZBusLog.Info("[WATERF] x=" + m_curX + "/" + m_max + " pond=" + m_pond + " sea=" + m_sea);
    }

    void Finish() {
        if (m_fw) { CloseFile(m_fw); m_fw = null; }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        BZBusLog.Info("[WATERF] COMPLETO " + m_probes + " probes | pond(rios)=" + m_pond + " sea(20m)=" + m_sea + " -> water_fine.csv");
        s_inst = null;
    }
}
