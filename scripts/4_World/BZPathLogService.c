// ============================================================================
//  BZPathLogService - graba la posicion del jugador cada SAMPLE_INTERVAL.
//  Singleton client-side. Se llama desde el modded MissionGameplay (NUMPAD 4/5/6).
// ============================================================================

class BZPathLogService {
    private static ref BZPathLogService s_Instance;

    static const float SAMPLE_INTERVAL_MS  = 20;      // 0.02 s (50 samples/s)
    // NOTA 2026-06-02: bajado de 100ms a 20ms (5x mas densidad). Razon: el
    // input keyboard del humano son pulses de 30-80ms (A/D press/release).
    // A 100ms perdiamos timing exacto de releases → Boris en Direct Replay
    // no podia reproducir el patron pulse-and-modulate. Con 20ms capturamos
    // los pulses cortos. Habilita Direct Replay fidedigno combinado con el
    // config-read del vehiculo. Files ~5x mas grandes (estimado 50KB/min en
    // ruta cruise → 250KB/min, irrelevante).
    static const float FEEDBACK_INTERVAL_S = 10.0;

    private int    m_State;
    private string m_FilePath;
    private float  m_StartTickTime;
    private int    m_SampleCount;
    private int    m_StopMarkCount;
    private float  m_TotalDistance;
    private vector m_LastPos;
    private bool   m_HasLastPos;
    private float  m_LastFeedbackTime;
    private float  m_LastModeBannerTime; // para banner persistente parking/reverse (cada 3s)
    static const float MODE_BANNER_INTERVAL_S = 3.0;
    private string m_RecordingMode = "normal"; // toggle key: NUMPAD + (KC_ADD). Cuando esta en "parking" los waypoints capturados quedan tagged para que el framework use el handler de direct replay.
    private string m_RecordingModeBeforeReverse = "normal"; // memoria del modo previo cuando se entra a reverse — al toggle reverse OFF se restaura este modo (compose semantico: parking + reverse vuelve a parking, no a normal)

    // -------------------------------------------------------------------------

    static BZPathLogService GetInstance() {
        if (!s_Instance) s_Instance = new BZPathLogService();
        return s_Instance;
    }

    void BZPathLogService() {
        m_State = BZPathLogState.OFF;
    }

    int GetState() { return m_State; }
    string GetRecordingMode() { return m_RecordingMode; }  // "normal"/"parking"/"maniobra"/"approach"/"reverse" — para reflejar en el Control Panel

    // -------------------------------------------------------------------------
    //  Acciones publicas (las invoca el OnKeyPress)

    void ToggleStartStop() {
        if (m_State == BZPathLogState.OFF) {
            Start();
        } else {
            Stop();
        }
    }

    // Modos de grabacion manuales (parking / maniobra / approach / reverse) y la pausa:
    // RETIRADOS. El control se unifico, la reversa y el intercambio se AUTO-DETECTAN del
    // gear al convertir, y la grabacion usa solo 3 teclas (NUMPAD 5 grabar, NUMPAD 4 marcar).
    // Estos handlers quedan como NO-OP (sus inputs no estan asignados) para no romper el
    // polling que los llama. No notifican nada.
    void ToggleParkingMode()  { }
    void ToggleManiobraMode() { }
    void ToggleApproachMode() { }
    void ToggleReverseMode()  { }
    void TogglePause()        { }

    void MarkStop() {
        if (m_State == BZPathLogState.OFF) {
            BigNotif("Recorder off — NUMPAD 5 to start");
            return;
        }
        bool wasRecording = (m_State == BZPathLogState.RECORDING);
        WriteSample(true);
        m_StopMarkCount++;
        CaptureCargoSnapshot(m_StopMarkCount);
        BigNotif("Stop #" + m_StopMarkCount + " marked");
        if (!wasRecording) {
            BZPathLog.Info("MarkStop con grabador en PAUSED — sample escrito, sigue pausado.");
        }
    }

    // -------------------------------------------------------------------------
    //  Captura de gear ranges (modo medicion de vehiculo)
    //
    //  NUMPAD 3 = SHIFT_POINT: velocidad en la que el conductor cambiaria
    //  normalmente (aguja por llegar a redline)
    //  NUMPAD 0 = MAX_SUSTAINED: velocidad maxima sostenida del gear (aguja
    //  pegada al redline, ya no acelera mas)
    //
    //  Independiente del PathLogger normal — no necesita NUMPAD 5 activo.
    //  Escribe a gear_ranges_<timestamp>.csv (uno por sesion).

    void MarkShiftPoint() {
        WriteGearRangeSample("SHIFT_POINT");
    }

    void MarkMaxSustained() {
        WriteGearRangeSample("MAX_SUSTAINED");
    }

    private string m_GearRangePath;
    private bool   m_GearRangeInited;

    private void WriteGearRangeSample(string markType) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        Car car = Car.Cast(player.GetParent());
        if (!car) return;

        // Inicializar archivo si es la primera marca
        if (!m_GearRangeInited) {
            EnsureProfileDir();
            m_GearRangePath = "$profile:BZ_AutoDrive_PathLogger\\gear_ranges_" + BuildTimestamp() + ".csv";
            FileHandle h = OpenFile(m_GearRangePath, FileMode.WRITE);
            if (h) {
                FPrint(h, "time_s,gear,speed_kmh,rpm,redline_rpm,mark_type,vehicle_class\n");
                CloseFile(h);
                m_GearRangeInited = true;
                BZPathLog.Info("Gear ranges init: " + m_GearRangePath);
            } else {
                return;
            }
        }

        int   gear        = car.GetGear();
        float speedKmh    = car.GetSpeedometerAbsolute();
        float rpm         = car.EngineGetRPM();
        float redlineRpm  = car.EngineGetRPMRedline();
        float t           = GetGame().GetTickTime();
        string vehicleClass = car.GetType();

        // Split de string para evitar "Formula too complex"
        string line = "" + t + "," + gear + "," + speedKmh + "," + rpm;
        line += "," + redlineRpm + "," + markType;
        line += "," + vehicleClass + "\n";

        FileHandle f = OpenFile(m_GearRangePath, FileMode.APPEND);
        if (!f) {
            BZPathLog.Err("Append gear_ranges fallo: " + m_GearRangePath);
            return;
        }
        FPrint(f, line);
        CloseFile(f);

        BZPathLog.Info("MarkGearRange " + markType + ": gear=" + gear + " speed=" + speedKmh + " rpm=" + rpm);
    }

    // -------------------------------------------------------------------------
    //  Start / Stop internos

    private void Start() {
        EnsureProfileDir();
        // Nombre legible: path_<fecha>_<vehiculo>.csv. El vehiculo (car.GetType()) esta disponible al
        // apretar 5 (jugador dentro del auto). El sidecar header_ se deriva de este nombre
        // (path_->header_ en WriteVehicleHeader), asi que sigue apareado. A pie -> path_<fecha>.csv.
        string vehTag = "";
        Man startPlayer = GetGame().GetPlayer();
        if (startPlayer) {
            Car startCar = Car.Cast(startPlayer.GetParent());
            if (startCar) vehTag = startCar.GetType();
        }
        string nameCore = "path_" + BuildTimestamp();
        if (vehTag != "") nameCore = nameCore + "_" + vehTag;
        m_FilePath        = "$profile:BZ_AutoDrive_PathLogger\\" + nameCore + ".csv";
        m_StartTickTime   = GetGame().GetTickTime();
        m_SampleCount     = 0;
        m_StopMarkCount   = 0;
        m_TotalDistance   = 0;
        m_HasLastPos      = false;
        m_LastFeedbackTime = m_StartTickTime;
        // 2026-06-07 fix: resetear mode al iniciar nuevo recording. Sin esto,
        // si la sesion anterior cerro con maniobra/parking/reverse activo, los
        // primeros wps de la nueva grabacion quedan tagged con ese modo viejo.
        // Caso real: TOMA 1 Impreza arranco con 612 wps maniobra erroneamente.
        m_RecordingMode = "normal";
        m_RecordingModeBeforeReverse = "normal";

        if (!WriteHeader()) {
            BZPathLog.Err("No se pudo crear archivo: " + m_FilePath);
            BigNotif("Error: couldn't create the recording file");
            return;
        }

        m_State = BZPathLogState.RECORDING;
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.SampleTick, SAMPLE_INTERVAL_MS, true);

        // Companion frame-by-frame (2026-07-05): mismo trigger, stream a resolucion
        // de fisica (frame_<ts>_<veh>.csv apareado a este path_). Solo si hay auto.
        Man frPlayer = GetGame().GetPlayer();
        if (frPlayer) {
            Car frCar = Car.Cast(frPlayer.GetParent());
            if (frCar) BZFrameRecorder.GetInstance().Start(frCar, nameCore);
        }

        BigNotif("Recording started");
        BZPathLog.Info("Start: " + m_FilePath);
    }

    private void Stop() {
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.SampleTick);
        m_State = BZPathLogState.OFF;

        BZFrameRecorder.GetInstance().Stop();

        float km = m_TotalDistance / 1000.0;
        BigNotif("Recording stopped — " + m_SampleCount + " pts · " + string.Format("%.2f", km) + " km · " + m_StopMarkCount + " stops");
        BZPathLog.Info("Stop: " + m_FilePath + " | " + m_SampleCount + " samples, " + string.Format("%.2f", km) + " km");
    }

    // -------------------------------------------------------------------------
    //  Tick + escritura

    private void SampleTick() {
        if (m_State != BZPathLogState.RECORDING) return;
        WriteSample(false);
        MaybeFeedback();
        MaybeModeBanner();
    }

    // Banner de modo (parking/reverse/approach): RETIRADO — el control es unificado y la
    // grabacion es siempre "normal" (la reversa se auto-detecta al convertir). No-op.
    private void MaybeModeBanner() { }

    private void WriteSample(bool isStop) {
        Man player = GetGame().GetPlayer();
        if (!player) return;

        vector pos;
        vector dir;
        float  speedKmh = 0.0;

        Object parent = Object.Cast(player.GetParent());
        Car    car    = Car.Cast(parent);
        int    gear     = 0;
        float  brake    = 0;
        float  throttle = 0;
        float  steering = 0;

        float rpm = 0;
        float redlineRpm = 0;
        vector linVel = "0 0 0";
        float clutch    = 0;
        float handbrake = 0;
        int   hornState = 0;
        int   lightsVal = 0;
        // FASE 2 (2026-07-04): plant-state del HUMANO = lo EJECUTADO por las ruedas, no el input.
        vector wdir0        = "0 0 1";
        float  frontWheelDeg = 0;
        float  sideslipDeg   = 0;
        float  yawRate       = 0;
        if (car) {
            pos      = car.GetPosition();
            dir      = car.GetDirection();
            speedKmh = car.GetSpeedometerAbsolute();
            gear     = car.GetGear();
            // Si alguno de estos getters no compila en esta version del juego,
            // el build va a fallar y los sacamos / reemplazamos por GetController().
            brake    = car.GetBrake();
            throttle = car.GetThrottle();
            steering = car.GetSteering();
            rpm        = car.EngineGetRPM();
            redlineRpm = car.EngineGetRPMRedline();
            linVel    = GetVelocity(car);
            wdir0     = car.WheelGetDirection(0);            // angulo REAL de la rueda delantera (ejecutado)
            vector angVelH = dBodyGetAngularVelocity(car);
            yawRate   = angVelH[1];                          // rad/s
            clutch    = car.GetClutch();
            handbrake = car.GetHandbrake();
            // FASE 2a (horn+lights): bocina + luces viven en CarScript (no en Car base).
            // Casteo aparte a CarScript. NO ternario en concat (rompe Enforce) -> uso if.
            CarScript cs = CarScript.Cast(parent);
            if (cs) {
                hornState = cs.BZBus_GetCarHornState();
                if (cs.IsScriptedLightsOn()) lightsVal = 1;
            }
        } else {
            pos = player.GetPosition();
            dir = player.GetDirection();
        }

        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        // FASE 2: angulo REAL de rueda vs heading (lo que Boris debe reproducir) + sideslip del cuerpo.
        if (car) {
            float wheelH = Math.Atan2(wdir0[0], wdir0[2]) * Math.RAD2DEG;
            float dAngW  = wheelH - heading;
            while (dAngW > 180.0)  dAngW -= 360.0;
            while (dAngW < -180.0) dAngW += 360.0;
            frontWheelDeg = dAngW;
            float hRadS  = heading * Math.DEG2RAD;
            float vLongS = linVel[0]*Math.Sin(hRadS) + linVel[2]*Math.Cos(hRadS);
            float vLatS  = linVel[0]*Math.Cos(hRadS) - linVel[2]*Math.Sin(hRadS);
            if (Math.AbsFloat(vLongS) > 0.1 || Math.AbsFloat(vLatS) > 0.1)
                sideslipDeg = Math.Atan2(vLatS, vLongS) * Math.RAD2DEG;
        }

        float t = GetGame().GetTickTime() - m_StartTickTime;

        if (m_HasLastPos)
            m_TotalDistance += vector.Distance(pos, m_LastPos);
        m_LastPos    = pos;
        m_HasLastPos = true;
        m_SampleCount++;

        int isStopInt = 0;
        if (isStop) isStopInt = 1;

        string line = "" + t + "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "," + speedKmh;
        line += "," + isStopInt + "," + gear;
        line += "," + throttle + "," + brake;
        line += "," + steering;
        line += "," + rpm + "," + redlineRpm;
        line += "," + m_RecordingMode;
        line += "," + linVel[0] + "," + linVel[1] + "," + linVel[2];
        line += "," + clutch + "," + handbrake;
        line += "," + hornState + "," + lightsVal;
        line += "," + frontWheelDeg + "," + sideslipDeg + "," + yawRate + "\n";

        AppendLine(line);
    }

    // -------------------------------------------------------------------------
    //  IO

    private void EnsureProfileDir() {
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        if (!FileExist(dir))
            MakeDirectory(dir);
    }

    private bool WriteHeader() {
        FileHandle f = OpenFile(m_FilePath, FileMode.WRITE);
        if (!f) return false;
        FPrint(f, "time_s,x,y,z,heading_deg,speed_kmh,is_stop,gear,throttle,brake,steering,rpm,redline_rpm,mode,vx,vy,vz,clutch,handbrake,horn,lights,front_wheel_deg,sideslip_deg,yaw_rate\n");
        CloseFile(f);
        WriteVehicleHeader();
        return true;
    }

    // Sidecar con metadata estatica del vehiculo. Una sola vez al Start.
    // Habilita al wizard derivar params especificos (MaxGear, mass, RPM range)
    // sin re-correr el recording. Si no hay vehiculo al Start (jugador a pie),
    // se escribe placeholder.
    private void WriteVehicleHeader() {
        // Header apareado al CSV por nombre (path_<ts>.csv -> header_<ts>.txt), no por
        // un timestamp suelto: garantiza que csv_to_route pueda emparejarlos.
        string headerPath = m_FilePath;
        headerPath.Replace("path_", "header_");
        headerPath.Replace(".csv", ".txt");
        FileHandle h = OpenFile(headerPath, FileMode.WRITE);
        if (!h) {
            BZPathLog.Err("WriteVehicleHeader: no se pudo crear " + headerPath);
            return;
        }
        Man player = GetGame().GetPlayer();
        Car car;
        if (player) car = Car.Cast(player.GetParent());
        if (!car) {
            FPrint(h, "NO_VEHICLE_AT_START\n");
            CloseFile(h);
            return;
        }
        FPrint(h, "vehicleClass=" + car.GetType() + "\n");
        FPrint(h, "mass=" + dBodyGetMass(car) + "\n");
        FPrint(h, "engineRPMIdle=" + car.EngineGetRPMIdle() + "\n");
        FPrint(h, "engineRPMMax=" + car.EngineGetRPMMax() + "\n");
        FPrint(h, "engineRPMRedline=" + car.EngineGetRPMRedline() + "\n");
        FPrint(h, "gearsCount=" + car.GetGearCount() + "\n");
        FPrint(h, "wheelCountPresent=" + car.WheelCountPresent() + "\n");

        // === ATTACHMENTS CAPTURE (2026-06-08) ===
        // Captura los attachments REALES del vehiculo al iniciar la grabacion.
        // Cierra el bug recurrente de classnames mal definidos (memory:
        // project_pathlogger_attachments_capture). El modder no necesita memorizar
        // ni adivinar: lo que tenga el vehiculo al apretar NUMPAD 5 es lo que se
        // captura. csv_to_route lo lee del header y popula Attachments[] del JSON.
        string attsList = "";
        int attCount = car.GetInventory().AttachmentCount();
        for (int attIdx = 0; attIdx < attCount; attIdx++) {
            EntityAI att = car.GetInventory().GetAttachmentFromIndex(attIdx);
            if (att) {
                if (attsList != "") attsList = attsList + ",";
                attsList = attsList + att.GetType();
            }
        }
        FPrint(h, "attachmentsCount=" + attCount + "\n");
        FPrint(h, "attachments=" + attsList + "\n");

        // === CONFIG-READ: steering angle (runtime, sin extraer PBO) (2026-06-10) ===
        // maxSteeringAngle vive en SimulationModule > Steering del config. Es la mitad
        // del radio minimo de giro: R_min = wheelbase / tan(maxSteeringAngle). Relaciona
        // el INPUT (volante 0-1) con el giro real de rueda para ESTE vehiculo.
        float maxSteer = GetGame().ConfigGetFloat("CfgVehicles " + car.GetType() + " SimulationModule Steering maxSteeringAngle");
        FPrint(h, "maxSteeringAngle=" + maxSteer + "\n");

        // === RECEPTOR: wheelbase desde posiciones de ruedas (2026-06-10) ===
        // Proyecta cada contacto de rueda sobre el eje forward del vehiculo; wheelbase =
        // rango (max-min). Robusto al orden de las ruedas. Si WheelGetContactPosition da
        // 0 client-side, wheelbase sale 0 y lo movemos server-side. Es la otra mitad de R_min.
        vector fwd = car.GetDirection();
        vector vpos = car.GetPosition();
        int wcount = car.WheelCount();
        float minProj = 99999.0;
        float maxProj = -99999.0;
        int wgood = 0;
        for (int wi = 0; wi < wcount; wi++) {
            vector wpos = car.WheelGetContactPosition(wi);
            if (wpos[0] == 0 && wpos[1] == 0 && wpos[2] == 0) continue;
            wgood++;
            float proj = (wpos[0] - vpos[0]) * fwd[0] + (wpos[2] - vpos[2]) * fwd[2];
            if (proj < minProj) minProj = proj;
            if (proj > maxProj) maxProj = proj;
        }
        float wheelbase = 0;
        if (wgood >= 2) wheelbase = maxProj - minProj;
        if (wheelbase < 0) wheelbase = -wheelbase;
        FPrint(h, "wheelCount=" + wcount + "\n");
        FPrint(h, "wheelbase=" + wheelbase + "\n");

        CloseFile(h);
        BZPathLog.Info("Vehicle header written: " + headerPath + " (attachments: " + attCount + ")");
    }

    // Snapshot del cargo del vehiculo al marcar parada (NUMPAD 4). Escribe una linea
    // al sidecar cargo_<ts>.txt: "<stopNum>|<cls>:<qty>,<cls>:<qty>,...". Solo si el
    // baul tiene algo. csv_to_route mapea stopNum -> N-esima parada -> wp -> CargoEvents.
    // v1: items SUELTOS top-level + cantidad. No captura nesting ni armas equipadas.
    private void CaptureCargoSnapshot(int stopNum) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        Car car = Car.Cast(player.GetParent());
        if (!car) return;
        CargoBase cargo = car.GetInventory().GetCargo();
        if (!cargo) return;
        int cnt = cargo.GetItemCount();
        if (cnt <= 0) return;

        string line = "" + stopNum + "|";
        int written = 0;
        for (int i = 0; i < cnt; i++) {
            EntityAI item = cargo.GetItem(i);
            if (!item) continue;
            int qty = 1;
            ItemBase ib = ItemBase.Cast(item);
            if (ib) {
                int q = (int)ib.GetQuantity();
                if (q > 1) qty = q;
            }
            if (written > 0) line = line + ",";
            line = line + item.GetType() + ":" + qty;
            written++;
        }
        if (written == 0) return;
        line = line + "\n";

        string cargoPath = m_FilePath;
        cargoPath.Replace("path_", "cargo_");
        cargoPath.Replace(".csv", ".txt");
        FileHandle f = OpenFile(cargoPath, FileMode.APPEND);
        if (f) {
            FPrint(f, line);
            CloseFile(f);
            BZPathLog.Info("Cargo snapshot stop #" + stopNum + ": " + line);
        }
    }

    // Open/Close en cada sample: si crashea el cliente, lo escrito ya esta en disco.
    private void AppendLine(string line) {
        FileHandle f = OpenFile(m_FilePath, FileMode.APPEND);
        if (!f) {
            BZPathLog.Err("Append fallo: " + m_FilePath);
            return;
        }
        FPrint(f, line);
        CloseFile(f);
    }

    // -------------------------------------------------------------------------
    //  Feedback

    private void MaybeFeedback() {
        float now = GetGame().GetTickTime();
        if (now - m_LastFeedbackTime < FEEDBACK_INTERVAL_S) return;
        m_LastFeedbackTime = now;

        Man player = GetGame().GetPlayer();
        float speedKmh = 0.0;
        if (player) {
            Car car = Car.Cast(player.GetParent());
            if (car) speedKmh = car.GetSpeedometerAbsolute();
        }

        float km = m_TotalDistance / 1000.0;
        string msg = "REC | " + m_SampleCount + " pts | " + string.Format("%.2f", km) + " km | " + string.Format("%.0f", speedKmh) + " km/h";
        SmallNotif(msg);
    }

    // -------------------------------------------------------------------------
    //  Mensajes al jugador

    private void BigNotif(string msg) {
        PlayerIdentity id = GetIdentity();
        if (id)
            ExpansionNotification("[PATH LOGGER]", msg).Create(id);
        BZPathLog.Info(msg);
    }

    private void SmallNotif(string msg) {
        PlayerIdentity id = GetIdentity();
        if (id)
            ExpansionNotification("[PATH]", msg).Create(id);
    }

    private PlayerIdentity GetIdentity() {
        PlayerBase pb = PlayerBase.Cast(GetGame().GetPlayer());
        if (pb) return pb.GetIdentity();
        return null;
    }

    // -------------------------------------------------------------------------
    //  Timestamp para el nombre del archivo

    private string BuildTimestamp() {
        int y, mo, d, h, mi, s;
        // Fecha/hora REAL del sistema (UTC). GetYearMonthDay (sin UTC) devuelve la fecha del MUNDO
        // in-game (año opaco tipo "52") -> por eso los nombres viejos eran ilegibles. Las variantes
        // UTC (confirmadas en dta\scripts.pbo core: proto void GetYearMonthDayUTC/GetHourMinuteSecondUTC)
        // dan el reloj real de la maquina. Fallback al in-game si el año no fuera valido.
        // Nota: es UTC (para AR = local+3h). Si se quiere local, restar el offset aca.
        GetYearMonthDayUTC(y, mo, d);
        GetHourMinuteSecondUTC(h, mi, s);
        if (y < 2000) {
            GetYearMonthDay(y, mo, d);
            GetHourMinuteSecond(h, mi, s);
        }
        return string.Format("%1%2%3-%4%5%6",
            y.ToString(),
            Pad2(mo), Pad2(d),
            Pad2(h),  Pad2(mi), Pad2(s));
    }

    private string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }
}
