// ============================================================================
//  BZServerProbe - captura server-side de la salida del TRADUCTOR (motor nativo)
//  para el auto que maneja un JUGADOR REAL (2026-07-05).
//
//  INVESTIGACION (project_frame_by_frame_replay): localizar si el traductor de la
//  instancia CLIENTE (prediccion, de donde grabamos el frame_) diverge del
//  traductor de la instancia SERVIDOR (autoritativa, donde replaya Boris). Si
//  divergen, ese es el ~5% de deficit sistematico.
//
//  Auto-detecta el auto del jugador real (CrewMember(0) con GetIdentity != null;
//  los eAI server-side no tienen identity) -> NO necesita trigger. Server-only.
//  Formato IDENTICO al frame_ para diff directo:
//    t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm
//  t_cum ES EL RELOJ REAL, como en el grabador: EOnPostSimulate puede llegar varias veces por paso de fisica con el
//  mismo estado; sumar timeSlice adelantaba t_cum y repetia filas. Una fila por cuadro, con el tiempo del juego.
//  dt sigue siendo el timeSlice (el paso de fisica): el reloj del juego va por cuadros y meteria su propio temblor.
//  Escribe server_probe.csv (overwrite por sesion de manejo).
// ============================================================================

class BZServerProbe {

    private static ref BZServerProbe s_Inst;

    private bool   m_Open;
    private string m_Path;
    private string m_Buf;
    private int    m_Frames;
    private float  m_TCum;
    private Car    m_Car;
    private Car    m_CarFallido;   // el auto cuya apertura fallo: no se reintenta por cuadro; al bajarse, se olvida
    private int    m_T0Ms;         // el reloj del juego al abrir el archivo
    private int    m_UltMs;        // el reloj del juego en la ultima fila escrita
    private vector m_UltPos;       // la pose de la ultima fila escrita
    private vector m_UltDir;
    private int    m_Llamadas;     // para la linea de STOP: lo que llego, y lo que no escribio y por que
    private int    m_MismoCuadro;  // llamadas en el mismo cuadro que la anterior
    private int    m_MismaPose;    // llamadas en otro cuadro con el vehiculo en marcha y la pose exacta de la fila anterior

    private const int FLUSH_EVERY = 30;
    private const float POSE_MIN_KMH = 3.0;   // por debajo, una pose repetida es reposo y la fila se escribe

    static BZServerProbe GetInstance() {
        if (!s_Inst) {
            s_Inst = new BZServerProbe();
        }
        return s_Inst;
    }

    // Llamado cada frame de fisica server-side (desde EOnPostSimulate, gateado a IsServer).
    void Frame(Car car, float dt) {
        if (!car) return;
        if (m_Open && !m_Car) Close();   // el auto que se grababa ya no existe y nadie mas lo iba a cerrar
        PlayerBase drv = PlayerBase.Cast(car.CrewMember(0));
        bool realPlayer = false;
        if (drv && drv.GetIdentity()) realPlayer = true;   // eAI server-side no tiene identity
        // ROUND-TRIP (2026-07-05): tambien capturar el auto de BORIS (tiene runner del framework) para
        // grabar una toma SERVER-NATIVA (sin piel-cliente) a 40Hz -> probar reproduce server-a-server.
        bool borisCar = false;
        BZBusService bsrv = BZBusService.GetRunnerForCar(car);
        if (!realPlayer && bsrv && bsrv.WantsBorisNative()) borisCar = true;   // OPT-IN (2026-08-09): solo graba boris_native si el check del reproductor estaba tildado al dar play
        if (!realPlayer && !borisCar) {
            // Solo cierra su propia sesion. Este metodo corre para TODO auto simulado: con otro vehiculo en marcha el archivo
            // se cerraba y se reabria en cada cuadro, y en el mismo segundo se pisaba.
            if (m_Open && car == m_Car) Close();
            if (car == m_CarFallido) m_CarFallido = null;   // nueva sesion de manejo = un intento nuevo (una linea, no una por cuadro)
            return;
        }
        if (!m_Open && car == m_CarFallido) return;   // la apertura fallo en esta sesion: ya se aviso, no se insiste
        // OTRO AUTO (2026-07-13): si el que grabábamos sigue con runner (multi-instancia real), no lo pisamos.
        // Pero si ya NO tiene runner (RESET/respawn/despawn — caso remoto: Boris sale, no te podés subir, RESET x4),
        // cerramos su archivo y pasamos al auto ACTUAL. Antes esto `return`eaba -> el logger quedaba pegado al 1er
        // auto y descartaba TODOS los runs post-reset, incluido el bueno (solo guardaba el 1er tramo).
        if (m_Open && car != m_Car) {
            if (m_Car && BZBusService.GetRunnerForCar(m_Car)) return;   // el viejo sigue activo -> no pisar
            Close();   // el viejo se reseteo/murio -> cerrar y reabrir para el auto vigente
        }
        if (!m_Open) {
            string fn = "server_probe.csv";
            // TIMESTAMP POR CORRIDA (2026-07-06): antes era "boris_native.csv" fijo -> cada spawn/respawn
            // lo abria en WRITE y PISABA la traza de la corrida anterior (perdiamos data de tests). Ahora
            // cada corrida crea su propio archivo boris_native_<ts>.csv -> nunca se sobreescribe.
            if (borisCar) fn = "boris_native_" + BuildTimestamp() + ".csv";
            Open(car, fn);
        }
        if (!m_Open) return;   // fallo la apertura: sin buffer que crezca por cuadro ni flush que falle
        if (dt <= 0) return;
        m_Llamadas++;
        int ahoraMs = GetGame().GetTime();
        int deltaMs = ahoraMs - m_UltMs;
        if (deltaMs <= 0) {
            m_MismoCuadro++;   // la segunda llamada del mismo cuadro: no hay tiempo nuevo que grabar
            return;
        }
        vector posAhora = car.GetPosition();
        vector dirAhora = car.GetDirection();
        if (car.GetSpeedometerAbsolute() > POSE_MIN_KMH && posAhora == m_UltPos && dirAhora == m_UltDir) {
            m_MismaPose++;   // en marcha y sin moverse un pelo: el mismo estado, llegado en otro cuadro
            return;
        }
        m_UltMs = ahoraMs;
        m_UltPos = posAhora;
        m_UltDir = dirAhora;
        m_TCum = (ahoraMs - m_T0Ms) / 1000.0;
        WriteRow(car, dt);
        m_Frames++;
        if (m_Frames % FLUSH_EVERY == 0) Flush();
    }

    private void Open(Car car, string fname) {
        m_Car    = car;
        m_TCum   = 0;
        m_Frames = 0;
        m_Buf    = "";
        m_T0Ms   = GetGame().GetTime();
        m_UltMs  = m_T0Ms;
        m_UltPos = "0 0 0";
        m_UltDir = "0 0 0";
        m_Llamadas = 0;
        m_MismoCuadro = 0;
        m_MismaPose = 0;
        m_Path   = BZCarpetas.PathLogger() + fname;   // la carpeta se crea ANTES de abrir (no existe en una instalacion nueva)
        FileHandle f = OpenFile(m_Path, FileMode.WRITE);
        if (!f) {
            BZBusLog.Err("[ServerProbe] no se pudo crear " + m_Path + " (no se reintenta hasta la proxima vez que alguien maneje este auto)");
            m_CarFallido = car;
            return;
        }
        FPrint(f, "t_cum,dt,throttle,brake,steering,gear,handbrake,x,y,z,heading,speed_kmh,rpm,front_wheel_deg\n");
        CloseFile(f);
        m_Open = true;
        BZBusLog.Info("[ServerProbe] START (jugador real manejando, server-side) -> " + m_Path);
    }

    private void WriteRow(Car car, float dt) {
        float throttle  = car.GetThrottle();
        float brake     = car.GetBrake();
        float steering  = car.GetSteering();
        int   gear      = car.GetGear();
        float handbrake = car.GetHandbrake();
        float speed     = car.GetSpeedometerAbsolute();
        float rpm       = car.EngineGetRPM();
        vector pos = car.GetPosition();
        vector dir = car.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;

        // ANGULO DE RUEDA EJECUTADO (2026-07-21). El log traia el steering PEDIDO pero no el angulo que la
        // rueda realmente alcanza. Sin eso no se puede distinguir "comando correcto pero insuficiente"
        // (k(v) inventado) de "el comando nunca se ejecuta" (limitador de ritmo) -- y son arreglos
        // opuestos. Mismo calculo que usa el grabador, asi el diff contra la toma humana es directo.
        vector wdir = car.WheelGetDirection(0);
        float wheelH = Math.Atan2(wdir[0], wdir[2]) * Math.RAD2DEG;
        float frontWheelDeg = wheelH - heading;
        while (frontWheelDeg > 180)  frontWheelDeg = frontWheelDeg - 360;
        while (frontWheelDeg < -180) frontWheelDeg = frontWheelDeg + 360;

        string row = "" + m_TCum + "," + dt + "," + throttle + "," + brake;
        row += "," + steering + "," + gear + "," + handbrake;
        row += "," + pos[0] + "," + pos[1] + "," + pos[2];
        row += "," + heading + "," + speed + "," + rpm + "," + frontWheelDeg + "\n";
        m_Buf += row;
    }

    private void Flush() {
        if (m_Buf == "") return;
        FileHandle f = OpenFile(m_Path, FileMode.APPEND);
        if (!f) return;
        FPrint(f, m_Buf);
        CloseFile(f);
        m_Buf = "";
    }

    private void Close() {
        Flush();
        m_Open = false;
        string cuentasStop = "llamadas " + m_Llamadas + " · mismo cuadro " + m_MismoCuadro + " · misma pose " + m_MismaPose;
        BZBusLog.Info("[ServerProbe] STOP (" + m_Frames + " frames server-side · " + cuentasStop + ") -> " + m_Path);
        m_Car = null;
    }

    // Timestamp real del sistema (UTC) para nombrar cada corrida. Mismo formato que BZPathLogService.
    private string BuildTimestamp() {
        int y, mo, d, h, mi, s;
        GetYearMonthDayUTC(y, mo, d);
        GetHourMinuteSecondUTC(h, mi, s);
        if (y < 2000) {
            GetYearMonthDay(y, mo, d);
            GetHourMinuteSecond(h, mi, s);
        }
        return string.Format("%1%2%3-%4%5%6", y.ToString(), Pad2(mo), Pad2(d), Pad2(h), Pad2(mi), Pad2(s));
    }

    private string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }
}
