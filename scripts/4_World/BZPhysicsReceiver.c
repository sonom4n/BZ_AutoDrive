// ============================================================================
//  BZPhysicsReceiver — V2 "physics receiver" (2026-07-03, ampliado 2026-07-04)
//
//  PORQUE (el hueco que ataca la v2):
//    v1 empujaba inputs (Set*) y ESTIMABA el resultado por diferencias de
//    posicion = OPEN-LOOP fisico. El engine YA calcula el estado real y lo
//    expone. Este receiver muestrea TODO el estado observable del vehiculo
//    cada frame de fisica (EOnPostSimulate) -> observabilidad TOTAL del plant.
//    Principio (Sonom4n 2026-07-04): "cualquier variable que dejemos afuera es un
//    grado de libertad oculto donde Boris puede divergir sin que lo veamos."
//
//  OBSERVABLES (superficie completa de la API del motor, verificada en
//  C:\DayZServer\dta\scripts: car.c, enphysics.c, surfaceinfo.c, object.c):
//    CUERPO 6DOF: pos, orientacion (yaw/pitch/roll via GetOrientation), vel
//      lineal descompuesta (long/lat/vert) -> sideslip beta, vel angular
//      (yaw/pitch/roll rate via dBodyGetAngularVelocity), energia cinetica,
//      masa/centro de masa/inercia (dBody*).
//    DIRECCION: front_wheel_deg (WheelGetDirection, angulo REAL con lag ~0.3s).
//    TREN: throttle/steer/brake (Get*=actual), gear current/future, rpm, combustible.
//      (thrust/gentle/turbo sacados: GetThrust==GetThrottle; IsGentle/IsTurbo no son script-callable.)
//    POR RUEDA (x4): omega (EMA), contacto, superficie (roughness/deflection/
//      tipo = GRIP), normal del terreno (slope/carga), rueda trabada (lockup),
//      slip angle lateral (vel en el punto de contacto vs direccion de rueda).
//
//  GOTCHAS: WheelGetAngularVelocity es "raw/unstable" (car.c l.286) -> EMA.
//    GetClutch() = basura (INT_MIN) para auto de IA -> sacado.
// ============================================================================
class BZPhysicsReceiver {

    // ---- CUERPO: pose + orientacion 6DOF ----
    float m_PosX;           // GetPosition()[0]
    float m_PosZ;           // GetPosition()[2]
    float m_PosY;           // GetPosition()[1] (altura)
    float m_Heading;        // yaw (deg 0-360)
    float m_Pitch;          // GetOrientation()[1] (deg, cabeceo)
    float m_Roll;           // GetOrientation()[2] (deg, alabeo)

    // ---- CUERPO: velocidades ----
    float m_BodySpeedMS;    // |GetVelocity()| total (m/s)
    float m_VelLong;        // componente longitudinal (adelante) m/s
    float m_VelLat;         // componente lateral (costado) m/s  <- clave para understeer
    float m_VelVert;        // componente vertical m/s
    float m_SideslipDeg;    // atan2(vlat, vlong) = angulo de deriva beta
    float m_YawRate;        // dBodyGetAngularVelocity()[1] (rad/s)
    float m_PitchRate;      // [0] (rad/s)
    float m_RollRate;       // [2] (rad/s)
    float m_KineticE;       // dBodyGetKineticEnergy()

    // ---- CUERPO: propiedades de masa (cambian con carga/attachments) ----
    float m_Mass;           // dBodyGetMass()
    float m_ComX;           // dBodyGetCenterOfMass()
    float m_ComY;
    float m_ComZ;
    float m_InertiaX;       // dBodyGetLocalInertia() (tensor diagonal)
    float m_InertiaY;
    float m_InertiaZ;

    // ---- DIRECCION ----
    float m_FrontWheelDeg;  // WheelGetDirection(0) vs heading = angulo REAL de rueda (lag de actuacion)

    // ---- TREN motriz (Get* = ACTUAL aplicado) ----
    float m_ActThrottle;
    float m_ActSteering;
    float m_ActBrake;
    float m_ActHandbrake;
    // THRUST/GENTLE/TURBO: sacados. GetThrust==GetThrottle (redundante con act_throttle);
    // GetThrustGentle/Turbo deprecados y sus reemplazos IsGentle/IsTurbo NO son script-callable
    // (RPT: "Undefined function 'Car.IsGentle'"). Ademas Boris nunca activa esos modos.
    // La fuerza motriz real se deriva de aceleracion x masa, que ya tenemos.
    int   m_CurrentGear;    // GetCurrentGear()  — marcha EFECTIVA real
    int   m_FutureGear;     // GetGear()         — marcha objetivo (en transicion)
    float m_Rpm;            // EngineGetRPM()
    bool  m_EngineOn;       // EngineIsOn()
    float m_SpeedoKmh;      // GetSpeedometer()
    float m_FuelFrac;       // GetFluidFraction(CarFluid.FUEL)

    // ---- POR RUEDA (agregado + por-rueda) ----
    float m_WheelSpeedMS;   // promedio filtrado omega*r de ruedas en contacto
    float m_SlipMS;         // long. slip proxy: body - |wheel_speed|
    int   m_WheelsContact;
    int   m_WheelsPresent;
    // por-rueda (indexado, 0..wc-1): superficie, grip, slope, lockup, slip lateral
    ref array<string> m_WSurf;    // WheelGetSurface().GetSurfaceType()
    ref array<float>  m_WRough;   // .GetRoughness()
    ref array<float>  m_WDefl;    // .GetDeflection()
    ref array<float>  m_WSlope;   // acos(normal.y) deg — inclinacion del terreno bajo la rueda
    ref array<int>    m_WLocked;  // WheelIsLocked()
    ref array<float>  m_WSlip;    // slip angle lateral (deg): vel_contacto vs direccion de rueda

    // ---- Comandado (para medir el gap; lo pasa el service) ----
    float m_CmdThrottle;
    float m_CmdSteering;
    float m_CmdBrake;
    int   m_DesiredGear;

    // ---- Filtro EMA por rueda (domar el omega "raw/unstable") ----
    ref array<float> m_WheelEMA;
    const float EMA_ALPHA = 0.35;

    // ---- Log CSV (fase 1) ----
    bool   m_LogOpen;
    string m_LogPath;
    int    m_RowCount;
    float  m_LogAccum;

    void BZPhysicsReceiver() {
        m_WheelEMA = new array<float>();
        m_WSurf   = new array<string>();
        m_WRough  = new array<float>();
        m_WDefl   = new array<float>();
        m_WSlope  = new array<float>();
        m_WLocked = new array<int>();
        m_WSlip   = new array<float>();
    }

    // ------------------------------------------------------------------------
    //  Muestreo per-frame. car = CarScript (Car.Cast(this) desde EOnPostSimulate).
    // ------------------------------------------------------------------------
    void Sample(Car car, float cmdThrottle, float cmdSteering, float cmdBrake, int desiredGear, float dt) {
        if (!car) return;

        // Comandado
        m_CmdThrottle = cmdThrottle;
        m_CmdSteering = cmdSteering;
        m_CmdBrake    = cmdBrake;
        m_DesiredGear = desiredGear;

        // --- Tren motriz (Get* = ACTUAL) ---
        m_ActThrottle  = car.GetThrottle();
        m_ActSteering  = car.GetSteering();
        m_ActBrake     = car.GetBrake();
        m_ActHandbrake = car.GetHandbrake();
        m_CurrentGear  = car.GetCurrentGear();
        m_FutureGear   = car.GetGear();
        m_Rpm          = car.EngineGetRPM();
        m_EngineOn     = car.EngineIsOn();
        m_SpeedoKmh    = car.GetSpeedometer();
        m_FuelFrac     = car.GetFluidFraction(CarFluid.FUEL);

        // --- Pose + orientacion 6DOF ---
        vector pos = car.GetPosition();
        m_PosX = pos[0]; m_PosY = pos[1]; m_PosZ = pos[2];
        vector fwd = car.GetDirection();      // heading probado (consistente con el resto del codigo)
        m_Heading = Math.Atan2(fwd[0], fwd[2]) * Math.RAD2DEG;
        if (m_Heading < 0) m_Heading += 360.0;
        vector ori = car.GetOrientation();    // [yaw, pitch, roll] deg -> pitch/roll (validar orden en la 1a captura)
        m_Pitch = ori[1];
        m_Roll  = ori[2];

        // --- Velocidad lineal descompuesta (relativa al heading) -> sideslip ---
        vector linVel = GetVelocity(car);
        m_BodySpeedMS = linVel.Length();
        float hRad = m_Heading * Math.DEG2RAD;
        float sinH = Math.Sin(hRad); float cosH = Math.Cos(hRad);
        m_VelLong = linVel[0]*sinH + linVel[2]*cosH;   // adelante
        m_VelLat  = linVel[0]*cosH - linVel[2]*sinH;   // costado
        m_VelVert = linVel[1];
        if (Math.AbsFloat(m_VelLong) > 0.1 || Math.AbsFloat(m_VelLat) > 0.1)
            m_SideslipDeg = Math.Atan2(m_VelLat, m_VelLong) * Math.RAD2DEG;
        else
            m_SideslipDeg = 0;

        // --- Velocidad angular (yaw/pitch/roll rate) + energia ---
        vector angVel = dBodyGetAngularVelocity(car);
        m_PitchRate = angVel[0];
        m_YawRate   = angVel[1];
        m_RollRate  = angVel[2];
        // dBodyGetKineticEnergy devuelve 0 para el auto de IA (confirmado 1a captura EXAMPLE01,
        // como GetClutch=basura) -> derivar KE traslacional = 1/2 m v^2 (m_BodySpeedMS ya calculado).
        m_KineticE  = 0.5 * dBodyGetMass(car) * m_BodySpeedMS * m_BodySpeedMS;

        // --- Propiedades de masa ---
        m_Mass = dBodyGetMass(car);
        vector com = dBodyGetCenterOfMass(car);
        m_ComX = com[0]; m_ComY = com[1]; m_ComZ = com[2];
        vector inr = dBodyGetLocalInertia(car);
        m_InertiaX = inr[0]; m_InertiaY = inr[1]; m_InertiaZ = inr[2];

        // --- Angulo REAL de rueda delantera (idx 0 = front-left) vs heading ---
        vector wdir0 = car.WheelGetDirection(0);
        float wheelH = Math.Atan2(wdir0[0], wdir0[2]) * Math.RAD2DEG;
        float dAng = wheelH - m_Heading;
        while (dAng > 180.0)  dAng -= 360.0;
        while (dAng < -180.0) dAng += 360.0;
        m_FrontWheelDeg = dAng;

        // --- Por rueda: EMA speed, superficie/grip, normal/slope, lockup, slip lateral ---
        int wc = car.WheelCount();
        m_WheelsPresent = car.WheelCountPresent();
        if (m_WheelEMA.Count() != wc) {
            m_WheelEMA.Clear(); m_WSurf.Clear(); m_WRough.Clear(); m_WDefl.Clear(); m_WSlope.Clear(); m_WLocked.Clear(); m_WSlip.Clear();
            for (int k = 0; k < wc; k++) { m_WheelEMA.Insert(0); m_WSurf.Insert(""); m_WRough.Insert(0); m_WDefl.Insert(0); m_WSlope.Insert(0); m_WLocked.Insert(0); m_WSlip.Insert(0); }
        }

        float sumSpeed = 0; int nContact = 0;
        for (int i = 0; i < wc; i++) {
            // omega filtrado
            float omega = car.WheelGetAngularVelocity(i);
            float prev  = m_WheelEMA.Get(i);
            float filt  = prev + EMA_ALPHA * (omega - prev);
            m_WheelEMA.Set(i, filt);

            // lockup (Enforce no traga ternario como arg de funcion -> if explicito)
            int lockedInt = 0; if (car.WheelIsLocked(i)) lockedInt = 1;
            m_WLocked.Set(i, lockedInt);

            bool contact = car.WheelHasContact(i);
            if (contact) {
                CarWheel cw = CarWheel.Cast(car.WheelGetEntity(i));
                if (cw) {
                    float radius = cw.GetRadius();
                    if (radius > 0) { sumSpeed = sumSpeed + filt * radius; nContact++; }
                }
                // superficie / grip
                SurfaceInfo si = car.WheelGetSurface(i);
                if (si) { m_WSurf.Set(i, si.GetSurfaceType()); m_WRough.Set(i, si.GetRoughness()); m_WDefl.Set(i, si.GetDeflection()); }
                else    { m_WSurf.Set(i, ""); m_WRough.Set(i, 0); m_WDefl.Set(i, 0); }
                // normal del terreno -> slope (angulo desde vertical)
                vector nrm = car.WheelGetContactNormal(i);
                float ny = nrm[1]; if (ny > 1) ny = 1; if (ny < -1) ny = -1;
                m_WSlope.Set(i, Math.Acos(ny) * Math.RAD2DEG);
                // slip angle lateral: vel en el punto de contacto vs direccion de la rueda
                vector cpos = car.WheelGetContactPosition(i);
                vector wvel = dBodyGetVelocityAt(car, cpos);
                vector wdir = car.WheelGetDirection(i);
                float vlen = Math.Sqrt(wvel[0]*wvel[0] + wvel[2]*wvel[2]);
                if (vlen > 0.2) {
                    float crossW = wvel[0]*wdir[2] - wvel[2]*wdir[0];
                    float dotW   = wvel[0]*wdir[0] + wvel[2]*wdir[2];
                    m_WSlip.Set(i, Math.Atan2(crossW, dotW) * Math.RAD2DEG);
                } else m_WSlip.Set(i, 0);
            } else {
                m_WSurf.Set(i, ""); m_WRough.Set(i, 0); m_WDefl.Set(i, 0); m_WSlope.Set(i, 0); m_WSlip.Set(i, 0);
            }
        }
        m_WheelsContact = nContact;
        if (nContact > 0) m_WheelSpeedMS = sumSpeed / nContact; else m_WheelSpeedMS = 0;
        m_SlipMS = m_BodySpeedMS - Math.AbsFloat(m_WheelSpeedMS);

        m_LogAccum += dt;
    }

    bool DueToLog(float interval) {
        if (m_LogAccum < interval) return false;
        m_LogAccum = 0;
        return true;
    }

    // ------------------------------------------------------------------------
    //  Log CSV: observabilidad total (fase 1, ver el gap comandado-vs-real).
    // ------------------------------------------------------------------------
    void OpenLog(string path) {
        m_LogPath = path; m_RowCount = 0; m_LogAccum = 0;
        FileHandle f = OpenFile(path, FileMode.WRITE);
        if (!f) { m_LogOpen = false; return; }
        string hdr = "time_s,pos_x,pos_y,pos_z,heading,pitch,roll";
        hdr += ",vel_long,vel_lat,vel_vert,body_speed_ms,sideslip_deg";
        hdr += ",yaw_rate,pitch_rate,roll_rate,kinetic_e";
        hdr += ",mass,com_x,com_y,com_z,inertia_x,inertia_y,inertia_z,fuel_frac";
        hdr += ",front_wheel_deg,cmd_throttle,act_throttle,cmd_steer,act_steer,cmd_brake,act_brake,handbrake";
        hdr += ",desired_gear,current_gear,future_gear,rpm,engine_on,speedo_kmh";
        hdr += ",wheel_speed_ms,slip_ms,wheels_contact,wheels_present";
        for (int wi = 0; wi < 4; wi++) hdr += ",w" + wi + "_surf,w" + wi + "_rough,w" + wi + "_defl,w" + wi + "_slopedeg,w" + wi + "_locked,w" + wi + "_slipdeg";
        hdr += "\n";
        FPrint(f, hdr);
        CloseFile(f);
        m_LogOpen = true;
    }

    void LogRow(float t) {
        if (!m_LogOpen) return;
        FileHandle f = OpenFile(m_LogPath, FileMode.APPEND);
        if (!f) return;

        int engInt = 0; if (m_EngineOn) engInt = 1;

        string line = "" + t + "," + m_PosX + "," + m_PosY + "," + m_PosZ;
        line += "," + m_Heading + "," + m_Pitch + "," + m_Roll;
        line += "," + m_VelLong + "," + m_VelLat + "," + m_VelVert + "," + m_BodySpeedMS + "," + m_SideslipDeg;
        line += "," + m_YawRate + "," + m_PitchRate + "," + m_RollRate + "," + m_KineticE;
        line += "," + m_Mass + "," + m_ComX + "," + m_ComY + "," + m_ComZ;
        line += "," + m_InertiaX + "," + m_InertiaY + "," + m_InertiaZ + "," + m_FuelFrac;
        line += "," + m_FrontWheelDeg + "," + m_CmdThrottle + "," + m_ActThrottle;
        line += "," + m_CmdSteering + "," + m_ActSteering + "," + m_CmdBrake + "," + m_ActBrake + "," + m_ActHandbrake;
        line += "," + m_DesiredGear + "," + m_CurrentGear + "," + m_FutureGear + "," + m_Rpm + "," + engInt + "," + m_SpeedoKmh;
        line += "," + m_WheelSpeedMS + "," + m_SlipMS + "," + m_WheelsContact + "," + m_WheelsPresent;

        FPrint(f, line);
        // por-rueda en un chunk aparte (4 ruedas)
        for (int wi = 0; wi < 4; wi++) {
            string wline = "";
            if (wi < m_WSurf.Count()) {
                wline += "," + m_WSurf.Get(wi) + "," + m_WRough.Get(wi) + "," + m_WDefl.Get(wi);
                wline += "," + m_WSlope.Get(wi) + "," + m_WLocked.Get(wi) + "," + m_WSlip.Get(wi);
            } else {
                wline = ",,0,0,0,0,0";
            }
            FPrint(f, wline);
        }
        FPrint(f, "\n");
        CloseFile(f);
        m_RowCount++;
    }

    void CloseLog() { m_LogOpen = false; }
    int GetRowCount() { return m_RowCount; }
}
