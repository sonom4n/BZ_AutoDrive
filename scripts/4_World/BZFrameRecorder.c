// ============================================================================
//  BZFrameRecorder - grabador frame-by-frame de INPUTS del humano (2026-07-05)
//
//  MOTIVO (premisa del usuario): "yo grabo mi toma, Boris la reproduce". El
//  PathLogger samplea a 20ms fijos (~32Hz) -> ALIASEA los taps rapidos del
//  humano (medido: 318 full-swings de steering mas rapidos que 0.03s). Para
//  reproducir la toma EXACTA (taps y todo) hace falta capturar los inputs a
//  resolucion de FISICA y reproducirlos en TIEMPO REAL (el real-time de la toma).
//
//  QUE CAPTURA: throttle/brake/steering/gear/handbrake + dt de fisica + la
//  trayectoria (pos/heading/speed) para (a) alinear el estado inicial de Boris
//  al replay y (b) medir drift. El INPUT es exacto client-side (es el input del
//  propio jugador, no la prediccion del volante que sub-reporta).
//
//  DONDE CORRE: companion del PathLogger. Start()/Stop() los dispara
//  BZPathLogService (client-side, jugador local = driver). Frame() lo llama
//  BZBusCarScript.EOnPostSimulate cada frame de fisica del auto que maneja el
//  jugador. En el server (sin player local) IsActive()==false -> no graba.
//
//  FORMATO (frame_<ts>_<veh>.csv, apareado al path_ del PathLogger):
//    t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm
//  Fila 0 = estado inicial exacto (dt=0). El replay (fase B) lee este stream y
//  comanda cada fila al dt grabado, arrancando desde la fila 0.
// ============================================================================

class BZFrameRecorder {

    private static ref BZFrameRecorder s_Instance;

    private bool     m_Active;
    private Car      m_Car;
    private string   m_Path;
    private float    m_TCum;         // tiempo acumulado desde el arranque (s)
    private int      m_FrameCount;   // frames grabados (para flush periodico + stats)
    private string   m_Buffer;       // acumula filas; flush cada FLUSH_EVERY frames
    private int      m_MarkNext;     // 1 = el proximo frame lleva is_marker=1 (NUMPAD 4 durante grabacion humana)

    private const int FLUSH_EVERY = 30;   // ~0.5s a 60Hz: batchea el I/O (1 open/close cada 30 frames)

    static BZFrameRecorder GetInstance() {
        if (!s_Instance) {
            s_Instance = new BZFrameRecorder();
        }
        return s_Instance;
    }

    bool IsActive() {
        return m_Active;
    }

    int GetFrameCount() {
        return m_FrameCount;
    }

    // Marca el proximo frame como EVENTO (is_marker=1). Client-side, sin RPC (el FrameRecorder corre
    // en el mismo cliente que maneja). El importador lo lee -> BZMarkerEvent en la ruta. No-op si no graba.
    void MarkNextFrame() {
        if (!m_Active) return;
        m_MarkNext = 1;
        BZPathLog.Info("[FrameRec] evento marcado en frame " + m_FrameCount);
    }

    // Marca el proximo frame como INTERCAMBIO / corte de tramo (is_marker=2).
    // Distinto del evento: esto declara "aca empieza un tramo nuevo" -> el conversor pone legBreak=1 en
    // ese waypoint y Boris lo trata como START POINT (para, resetea el estado de manejo, arranca derecho).
    // Hace falta declararlo a mano porque un 0 km/h SOLO es una PAUSA: no alcanza para inferir el corte.
    // El editor escribe el MISMO campo cuando dibujas un intercambio. Accion: UABZMarkLeg (sin bind por
    // default; se asigna en Controles -> BZ AutoDrive).
    void MarkNextFrameLegBreak() {
        if (!m_Active) return;
        m_MarkNext = 2;
        BZPathLog.Info("[FrameRec] leg break marked at frame " + m_FrameCount);
    }

    // -------------------------------------------------------------------------
    //  Start: nameCore = "path_<ts>_<veh>" del PathLogger. Derivamos "frame_..."
    //  para que queden apareados (igual que header_). Escribe header + fila 0.
    void Start(Car car, string nameCore) {
        if (!car) {
            BZPathLog.Err("[FrameRec] Start sin car");
            return;
        }
        m_Car        = car;
        m_TCum       = 0;
        m_FrameCount = 0;
        m_Buffer     = "";
        m_MarkNext   = 0;

        string frameCore = nameCore;
        frameCore.Replace("path_", "frame_");
        if (frameCore == nameCore) {
            // nameCore no empezaba con path_ (jugador a pie improbable aca): prefijar
            frameCore = "frame_" + nameCore;
        }
        m_Path = "$profile:BZ_AutoDrive_PathLogger\\" + frameCore + ".csv";

        // front_wheel_deg (2026-07-05, Path 1): angulo de rueda EFECTIVO (post-rampa) = lo que genera la
        // fuerza lateral. Boris lo trackea en vez de re-correr la rampa del cmd. Columna nueva al final.
        // is_marker (2026-07-13): 1 = evento marcado con NUMPAD 4 en ese frame. Columna al FINAL
        // (backward-compat: los scripts leen por indice las 14 primeras). El importador la usa
        // para crear BZMarkerEvent en la ruta desde una grabacion humana.
        string header = "t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg,is_marker\n";
        FileHandle f = OpenFile(m_Path, FileMode.WRITE);
        if (!f) {
            BZPathLog.Err("[FrameRec] no se pudo crear " + m_Path);
            m_Car = null;
            return;
        }
        FPrint(f, header);
        CloseFile(f);

        m_Active = true;
        // Fila 0 = estado inicial exacto (dt=0): referencia de alineacion para el replay.
        WriteRow(car, 0);

        BZPathLog.Info("[FrameRec] Start: " + m_Path);
    }

    // -------------------------------------------------------------------------
    //  Frame: llamado cada frame de fisica desde EOnPostSimulate. Solo graba el
    //  auto que arranco la grabacion (m_Car).
    void Frame(Car car, float dt) {
        if (!m_Active || car != m_Car) return;
        if (dt <= 0) return;
        m_TCum += dt;
        WriteRow(car, dt);

        m_FrameCount++;
        if (m_FrameCount % FLUSH_EVERY == 0) {
            Flush();
        }
    }

    // -------------------------------------------------------------------------
    private void WriteRow(Car car, float dt) {
        float throttle  = car.GetThrottle();
        float brake     = car.GetBrake();
        float steering  = car.GetSteering();
        int   gear      = car.GetGear();
        float handbrake = car.GetHandbrake();
        float speedKmh  = car.GetSpeedometerAbsolute();
        float rpm       = car.EngineGetRPM();

        vector pos = car.GetPosition();
        vector dir = car.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;

        // Angulo REAL de rueda delantera (idx 0), efectivo post-rampa (mismo metodo que el receiver).
        vector wdir0 = car.WheelGetDirection(0);
        float wheelH = Math.Atan2(wdir0[0], wdir0[2]) * Math.RAD2DEG;
        float frontWheelDeg = wheelH - heading;
        while (frontWheelDeg > 180)  frontWheelDeg = frontWheelDeg - 360;
        while (frontWheelDeg < -180) frontWheelDeg = frontWheelDeg + 360;

        // Consume el flag de marca: este frame la lleva, se resetea para el siguiente.
        int isMarker = m_MarkNext;
        m_MarkNext = 0;

        // Split para evitar "Formula too complex"
        string row = "" + m_TCum + "," + dt + "," + throttle + "," + brake;
        row += "," + steering + "," + gear + "," + handbrake;
        row += "," + pos[0] + "," + pos[1] + "," + pos[2];
        row += "," + heading + "," + speedKmh + "," + rpm + "," + frontWheelDeg;
        row += "," + isMarker + "\n";

        m_Buffer += row;
    }

    // -------------------------------------------------------------------------
    private void Flush() {
        if (m_Buffer == "") return;
        FileHandle f = OpenFile(m_Path, FileMode.APPEND);
        if (!f) {
            BZPathLog.Err("[FrameRec] flush fallo: " + m_Path);
            return;
        }
        FPrint(f, m_Buffer);
        CloseFile(f);
        m_Buffer = "";
    }

    // -------------------------------------------------------------------------
    void Stop() {
        if (!m_Active) return;
        Flush();
        m_Active = false;
        BZPathLog.Info("[FrameRec] Stop: " + m_Path + " | " + m_FrameCount + " frames");
        m_Car = null;
    }
}
