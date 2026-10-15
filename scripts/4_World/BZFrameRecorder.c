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
//  BZVehiculoGanchos.PostSimulate (desde la cascara del auto y la de la moto) cada frame de fisica del vehiculo
//  que maneja el jugador. En el server (sin player local) IsActive()==false -> no graba.
//  AUTO O MOTO: recibe el BZVehiculo. Las columnas de siempre salen igual; al final van roll_deg, kickstand,
//  fallen, cambio (la animacion de cambio del conductor = su embrague) y yaw_rate (rad/s: la autoridad de
//  direccion del envelope sale de yaw_rate contra steering). El auto escribe su roll, pata 0 y caida 0.
//
//  FORMATO (frame_<ts>_<veh>.csv, apareado al path_ del PathLogger):
//    t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm
//  Fila 0 = estado inicial exacto (dt=0). El replay (fase B) lee este stream y
//  comanda cada fila al dt grabado, arrancando desde la fila 0.
// ============================================================================

class BZFrameRecorder {

    private static ref BZFrameRecorder s_Instance;

    private bool     m_Active;
    private Transport m_T;         // el vehiculo que arranco la toma (auto o moto)
    private string   m_Path;
    private float    m_TCum;         // tiempo acumulado desde el arranque (s)
    private int      m_FrameCount;   // frames grabados (para flush periodico + stats)
    private string   m_Buffer;       // acumula filas; flush cada FLUSH_EVERY frames
    private int      m_MarkNext;     // 1 = el proximo frame lleva is_marker=1 (NUMPAD 4 durante grabacion humana)
    // EL RELOJ ES EL REAL (27/09): con la moto el cliente llama EOnPostSimulate DOS veces por paso (las filas
    // impares repetian exacto la posicion) y sumar timeSlice daba el tiempo al doble (t_cum 33,4 s contra ~15,7 reales).
    // Cada fila lleva el tiempo del reloj del juego (ms) y una llamada en el mismo cuadro que la anterior no escribe.
    private int      m_T0Ms;
    private int      m_UltMs;
    private int      m_Llamadas;     // llamadas a Frame (para la medicion)
    private int      m_MismoCuadro;  // llamadas en el mismo cuadro que la anterior: no escriben fila
    private float    m_SumSlice;     // suma de los timeSlice que llegaron (lo que antes era el reloj)
    private string   m_HeaderPath;   // el header_ de la toma: al parar se le agrega la medicion
    private string   m_ContactoPend; // los contactos desde la ultima fila (columna `contacto`)

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
    void Start(BZVehiculo v, string nameCore) {
        if (!v || !v.T()) {
            BZPathLog.Err("[FrameRec] Start sin vehiculo");
            return;
        }
        m_T          = v.T();
        m_TCum       = 0;
        m_FrameCount = 0;
        m_T0Ms       = GetGame().GetTime();
        m_UltMs      = m_T0Ms;
        m_Llamadas   = 0;
        m_MismoCuadro = 0;
        m_SumSlice   = 0;
        m_ContactoPend = "";
        m_HeaderPath = nameCore;
        m_HeaderPath.Replace("path_", "header_");
        m_HeaderPath = BZCarpetas.PathLogger() + m_HeaderPath + ".txt";
        m_Buffer     = "";
        m_MarkNext   = 0;

        string frameCore = nameCore;
        frameCore.Replace("path_", "frame_");
        if (frameCore == nameCore) {
            // nameCore no empezaba con path_ (jugador a pie improbable aca): prefijar
            frameCore = "frame_" + nameCore;
        }
        m_Path = BZCarpetas.PathLogger() + frameCore + ".csv";   // la carpeta se crea antes de abrir

        // front_wheel_deg (2026-07-05, Path 1): angulo de rueda EFECTIVO (post-rampa) = lo que genera la
        // fuerza lateral. Boris lo trackea en vez de re-correr la rampa del cmd. Columna nueva al final.
        // is_marker (2026-07-13): 1 = evento marcado con NUMPAD 4 en ese frame. Columna al FINAL
        // (backward-compat: los scripts leen por indice las 14 primeras). El importador la usa
        // para crear BZMarkerEvent en la ruta desde una grabacion humana.
        // COLUMNAS DE CALIBRACION (2026-08-25): la toma pasa a ser el banco del envelope.
        // Van al FINAL: los lectores por indice de las 15 primeras siguen andando, y el
        // extractor lee por NOMBRE, asi que las tomas viejas siguen sirviendo (marca las
        // ausentes como fallback en vez de rellenarlas con cero).
        //
        //   pitch_deg    el pitch que usa el runtime, asin(dir[1]). El banco de planeo lo usa
        //                DOS veces --compuerta de 2.2 grados y compensacion de gravedad-- y
        //                derivarlo de x,y,z offline no es lo mismo que medirlo.
        //   surface      el nombre CRUDO del piso, NO la clase dura/blanda. La clasificacion
        //                es una decision de la capa de RESPUESTA (BZWorldProbeLive la hace
        //                binaria a proposito); hornearla en el dato ataria las tomas viejas a
        //                la clasificacion de hoy.
        //   chassis_kmh  LA QUE MAS FALTABA, y no estaba en la lista pedida. speed_kmh es
        //                GetSpeedometerAbsolute() = el velocimetro de RUEDA, y TODO el control
        //                del framework corre sobre la velocidad de CHASIS. Las dos difieren
        //                exactamente cuando el embrague desliza. Sin esta columna la toma no
        //                tiene la velocidad que el controlador usa, el deslizamiento no se
        //                puede derivar, y toda derivacion offline usa la de rueda creyendo
        //                que es la otra.
        string header = "t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg,is_marker";
        header = header + ",pitch_deg,surface,chassis_kmh";
        // contacto (al final, auto y moto): los contactos del vehiculo desde la fila anterior, "<zona>:<clase>" separados
        // por '|' (el propio conductor no cuenta); vacio si no hubo. El conversor avisa una toma con choque.
        header = header + ",roll_deg,kickstand,fallen,cambio,yaw_rate,contacto\n";
        FileHandle f = OpenFile(m_Path, FileMode.WRITE);
        if (!f) {
            BZPathLog.Err("[FrameRec] no se pudo crear " + m_Path);
            m_T = null;
            return;
        }
        FPrint(f, header);
        CloseFile(f);

        m_Active = true;
        // Fila 0 = estado inicial exacto (dt=0): referencia de alineacion para el replay.
        WriteRow(v, 0);

        BZPathLog.Info("[FrameRec] Start: " + m_Path);
    }

    // -------------------------------------------------------------------------
    //  Frame: llamado cada frame de fisica desde EOnPostSimulate. Solo graba el
    //  vehiculo que arranco la grabacion (m_T).
    void Frame(BZVehiculo v, float dt) {
        if (!m_Active || !v || v.T() != m_T) return;
        if (dt <= 0) return;
        m_Llamadas++;
        m_SumSlice += dt;
        int ahoraMs = GetGame().GetTime();
        int dMs = ahoraMs - m_UltMs;
        if (dMs <= 0) {
            m_MismoCuadro++;   // la segunda llamada del mismo cuadro: no hay tiempo nuevo que grabar
            return;
        }
        m_UltMs = ahoraMs;
        float dtReal = dMs / 1000.0;
        m_TCum = (ahoraMs - m_T0Ms) / 1000.0;
        WriteRow(v, dtReal);

        m_FrameCount++;
        if (m_FrameCount % FLUSH_EVERY == 0) {
            Flush();
        }
    }

    // -------------------------------------------------------------------------
    private void WriteRow(BZVehiculo v, float dt) {
        Transport tr = v.T();
        if (!tr) return;
        float throttle  = v.GasLeido();
        float brake     = v.FrenoLeido();
        float steering  = v.VolanteLeido();
        int   gear      = v.Marcha();
        float handbrake = v.ManoLeida();
        float speedKmh  = v.KmhAbs();
        float rpm       = v.Rpm();

        vector pos = tr.GetPosition();
        vector dir = tr.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;

        // Angulo REAL de rueda delantera (idx 0), efectivo post-rampa (mismo metodo que el receiver).
        vector wdir0 = v.RuedaDireccion(0);
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
        row += "," + isMarker;

        // --- calibracion: MISMAS expresiones que el runtime, para que la toma y el motor
        // midan lo mismo. pitch por asin(dir[1]) y no por GetOrientation (crash documentado
        // con Tan). chassis por GetVelocity, que es funcion GLOBAL sobre la entidad.
        float pitchDeg = Math.Asin(Math.Clamp(dir[1], -1.0, 1.0)) * Math.RAD2DEG;
        string surf = "";
        GetGame().SurfaceGetType3D(pos[0], pos[1], pos[2], surf);
        if (surf == "") surf = "none";
        float chassisKmh = GetVelocity(tr).Length() * 3.6;
        row += "," + pitchDeg + "," + surf + "," + chassisKmh;
        // --- la moto (y el roll del auto): inclinacion, pata, caida, cambio del conductor, tasa de giro
        int caidaI = 0;
        if (v.Caida()) caidaI = 1;
        int cambioI = 0;
        if (v.Cambio()) cambioI = 1;
        vector angV = dBodyGetAngularVelocity(tr);
        row += "," + v.Roll() + "," + v.Pata() + "," + caidaI + "," + cambioI + "," + angV[1];
        row += "," + m_ContactoPend;
        m_ContactoPend = "";
        row += "\n";

        m_Buffer += row;
    }

    // un contacto del vehiculo que se esta grabando: lo llaman las cascaras (auto y moto) desde OnContact, ANTES de su
    // filtro de server (el grabador corre en el cliente del que maneja). Va en la columna `contacto` de la proxima fila.
    // Un contacto con un tripulante del propio vehiculo (el que maneja) no cuenta.
    static void Contacto(Transport t, string zona, IEntity otro) {
        BZFrameRecorder r = s_Instance;
        if (!r || !r.m_Active || !t || r.m_T != t) return;
        Object oC = Object.Cast(otro);
        string clase = "terreno";
        if (oC) {
            int iC;
            for (iC = 0; iC < t.CrewSize(); iC++) {
                if (t.CrewMember(iC) && t.CrewMember(iC) == oC) return;
            }
            clase = oC.GetType();
            if (clase == "") clase = oC.GetModelName();
            if (clase == "") clase = "objeto";
        }
        string ent = zona + ":" + clase;
        ent.Replace(",", ";");
        ent.Replace("|", "/");
        if (r.m_ContactoPend.IndexOf(ent) >= 0) return;
        if (r.m_ContactoPend != "") r.m_ContactoPend = r.m_ContactoPend + "|";
        r.m_ContactoPend = r.m_ContactoPend + ent;
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
        float realS = (m_UltMs - m_T0Ms) / 1000.0;
        float cociente = 0;
        if (realS > 0) cociente = m_SumSlice / realS;
        string st = "[FrameRec] Stop: " + m_Path + " | " + m_FrameCount + " filas · " + m_Llamadas + " llamadas (" + m_MismoCuadro + " en el mismo cuadro que la anterior, sin fila)";
        st = st + " · suma de timeSlice " + m_SumSlice + " s contra reloj real " + realS + " s (cociente " + cociente + ")";
        BZPathLog.Info(st);
        FileHandle hh = OpenFile(m_HeaderPath, FileMode.APPEND);
        if (hh) {
            FPrint(hh, "frameRecFilas=" + m_FrameCount + "\n");
            FPrint(hh, "frameRecLlamadas=" + m_Llamadas + "\n");
            FPrint(hh, "frameRecMismoCuadro=" + m_MismoCuadro + "\n");
            FPrint(hh, "frameRecSumaTimeSlice=" + m_SumSlice + "\n");
            FPrint(hh, "frameRecRelojReal=" + realS + "\n");
            CloseFile(hh);
        }
        m_T = null;
    }
}
