// ============================================================================
//  BZInverseModel.c - Capa 3 + 4 del framework Boris v2
//
//  Capa 3: Speed PID
//    target_speed_kmh, current_speed_kmh, dt -> desired_accel (m/s²)
//
//  Capa 4: Inverse Model
//    desired_accel + vehicle config + state (RPM, gear, surface, slope)
//      -> throttle [0,1] | brake [0,1]
//
//  Validacion offline: simulator.js + bridge.js + sim_run.js en OPS folder.
//  Closure tests passing, slope detection working, brake error <6%.
//
//  Activacion: setear UseInverseModel=true en BZBusRoute.json.
//  Default false -> framework v1 (replay-based) sigue activo.
// ============================================================================

class BZInverseModel {

    // -------- Vehicle profile in-memory (poblado desde config en spawn) -------
    private float m_Mass;             // kg
    private float m_WheelRadius;      // m
    private float m_DragCoef;         // -
    private float m_FrontalArea;      // m²
    private float m_RPMIdle;
    private float m_RPMRedline;
    private ref array<float> m_TorqueCurve;   // pares (RPM, Nm) interleaved
    private ref array<float> m_GearRatios;    // forward gears only
    private float m_FinalDrive;
    private float m_CentDiffRatio;
    private float m_MaxBrakeTorqueFront;
    private float m_MaxBrakeTorqueRear;
    private ref array<float> m_PressureBySpeed; // (kmh, coef) interleaved
    private float m_DrivenAxleWeightRatio;
    private int   m_DriveKind;   // 0=FWD, 1=RWD, 2=AWD (para transferencia de peso en pendiente y creep de trepada)

    // -------- PID state ------------------------------------------------------
    private float m_PIDIntegral;
    private float m_PIDPrevError;
    private float m_PIDKp;
    private float m_PIDKi;
    private float m_PIDKd;

    // -------- Constants ------------------------------------------------------
    private const float AIR_DENSITY = 1.225;
    private const float G = 9.81;
    private const float ROLLING_COEF = 0.015;
    private const float SURFACE_FRICTION_ASPHALT = 1.0;
    private const float SURFACE_ROLLING_ASPHALT = 1.0;

    // -------- Construct ------------------------------------------------------
    void BZInverseModel() {
        m_TorqueCurve = new array<float>;
        m_GearRatios = new array<float>;
        m_PressureBySpeed = new array<float>;
        m_PIDIntegral = 0;
        m_PIDPrevError = 0;
        m_PIDKp = 0.4;
        m_PIDKi = 0.05;
        m_PIDKd = 0.0;
        // Defaults razonables (overrideables via LoadFromConfig)
        m_Mass = 1400;
        m_WheelRadius = 0.3;
        m_DragCoef = 0.5;
        m_FrontalArea = 2.0;
        m_RPMIdle = 1250;
        m_RPMRedline = 6250;
        m_FinalDrive = 3.667;
        m_CentDiffRatio = 1.0;
        m_DrivenAxleWeightRatio = 0.6;
        m_MaxBrakeTorqueFront = 2000;
        m_MaxBrakeTorqueRear = 1500;
    }

    // -------- Cargar profile desde ConfigGet del vehiculo --------------------
    void LoadFromConfig(string vehicleClass, Car bus) {
        string base = "CfgVehicles " + vehicleClass + " ";
        string sim = base + "SimulationModule ";

        // Runtime masa
        m_Mass = dBodyGetMass(bus);
        if (m_Mass < 100) m_Mass = 1400; // fallback

        // RPM ranges via runtime API (mas confiable que config)
        m_RPMIdle = bus.EngineGetRPMIdle();
        m_RPMRedline = bus.EngineGetRPMRedline();
        if (m_RPMIdle < 100) m_RPMIdle = 1250;
        if (m_RPMRedline < 1000) m_RPMRedline = 6250;

        // Config-derived
        m_DragCoef = GetGame().ConfigGetFloat(sim + "Aerodynamics dragCoefficient");
        if (m_DragCoef <= 0) m_DragCoef = 0.5; // T6 declara 0, fallback razonable
        m_FrontalArea = GetGame().ConfigGetFloat(sim + "Aerodynamics frontalArea");
        if (m_FrontalArea <= 0) m_FrontalArea = 2.0;

        // Torque curve
        array<float> tc = new array<float>;
        GetGame().ConfigGetFloatArray(sim + "Engine torqueCurve", tc);
        m_TorqueCurve.Clear();
        foreach (float v : tc) m_TorqueCurve.Insert(v);

        // Gear ratios
        array<float> gr = new array<float>;
        GetGame().ConfigGetFloatArray(sim + "Gearbox ratios", gr);
        m_GearRatios.Clear();
        foreach (float r : gr) m_GearRatios.Insert(r);

        // Final drive (using Front axle differential as proxy if no separate finalDrive)
        m_FinalDrive = GetGame().ConfigGetFloat(sim + "Axles Front Differential ratio");
        if (m_FinalDrive <= 0.1) m_FinalDrive = 3.667;

        // Central diff (AWD)
        m_CentDiffRatio = GetGame().ConfigGetFloat(sim + "CentralDifferential ratio");
        if (m_CentDiffRatio <= 0) m_CentDiffRatio = 1.0;

        // Brakes
        m_MaxBrakeTorqueFront = GetGame().ConfigGetFloat(sim + "Axles Front maxBrakeTorque");
        m_MaxBrakeTorqueRear  = GetGame().ConfigGetFloat(sim + "Axles Rear maxBrakeTorque");
        if (m_MaxBrakeTorqueFront <= 0) m_MaxBrakeTorqueFront = 2000;
        if (m_MaxBrakeTorqueRear  <= 0) m_MaxBrakeTorqueRear = 1500;

        // Brake pressure curve
        array<float> ps = new array<float>;
        GetGame().ConfigGetFloatArray(sim + "Brake pressureBySpeed", ps);
        m_PressureBySpeed.Clear();
        foreach (float pv : ps) m_PressureBySpeed.Insert(pv);

        // Drive type → driven axle weight ratio
        string drive = GetGame().ConfigGetTextOut(sim + "drive");
        if (drive == "DRIVE_AWD") { m_DrivenAxleWeightRatio = 0.5; m_DriveKind = 2; }
        else if (drive == "DRIVE_RWD") { m_DrivenAxleWeightRatio = 0.45; m_DriveKind = 1; }
        else { m_DrivenAxleWeightRatio = 0.6; m_DriveKind = 0; } // FWD default

        BZBusLog.Info("[InverseModel] Loaded profile for " + vehicleClass);
        BZBusLog.Info("[InverseModel]   mass=" + m_Mass + " RPMidle=" + m_RPMIdle + " RPMred=" + m_RPMRedline);
        BZBusLog.Info("[InverseModel]   drive=" + drive + " axleW=" + m_DrivenAxleWeightRatio + " finalDrive=" + m_FinalDrive);
        BZBusLog.Info("[InverseModel]   gearRatios=" + m_GearRatios.Count() + " torqueCurve=" + (m_TorqueCurve.Count()/2) + " brakeCurve=" + (m_PressureBySpeed.Count()/2));
        BZBusLog.Info("[InverseModel]   maxBrakeDecel(config)=" + GetMaxBrakeDecel("") + " m/s2 (freno fisico por-vehiculo para el endpoint)");
    }

    // Tipo de traccion leido del config (0=FWD, 1=RWD, 2=AWD). Lo usa el creep de trepada del endpoint: el FWD
    // tiene menos traccion a baja velocidad (y peor en subida) -> necesita mas piso de throttle para des-clavar.
    int GetDriveKind() { return m_DriveKind; }

    // -------- PID gains override desde JSON (opcional) -----------------------
    void SetPIDGains(float kp, float ki, float kd) {
        if (kp > 0) m_PIDKp = kp;
        if (ki > 0) m_PIDKi = ki;
        if (kd >= 0) m_PIDKd = kd;
    }

    void ResetPID() {
        m_PIDIntegral = 0;
        m_PIDPrevError = 0;
    }

    // -------- Helpers --------------------------------------------------------
    private float Interp(array<float> curve, float x) {
        // curve es array plano: [x0, y0, x1, y1, ...]
        int n = curve.Count();
        if (n < 4) return 0;
        if (x <= curve[0]) return curve[1];
        int lastX = n - 2;
        int lastY = n - 1;
        if (x >= curve[lastX]) return curve[lastY];
        int limit = n - 2;
        for (int i = 0; i < limit; i += 2) {
            float x1 = curve[i];
            float y1 = curve[i+1];
            float x2 = curve[i+2];
            float y2 = curve[i+3];
            if (x >= x1 && x <= x2) {
                float alpha = (x - x1) / (x2 - x1);
                return y1 * (1 - alpha) + y2 * alpha;
            }
        }
        return 0;
    }

    private float EngineRPM(int gearIdx0, float kmh) {
        // gearIdx0 is 0-indexed into m_GearRatios (sim convention)
        if (gearIdx0 < 0 || gearIdx0 >= m_GearRatios.Count()) return 0;
        float v_ms = kmh / 3.6;
        float wheelRpm = (v_ms / m_WheelRadius) * 60.0 / 6.28318530718;
        return wheelRpm * m_GearRatios[gearIdx0] * m_FinalDrive * m_CentDiffRatio;
    }

    // Velocidad máxima FÍSICA del vehículo: a redline en la marcha MÁS ALTA, leído de SU config (gear
    // ratios + final drive + cent diff + redline + radio de rueda). Es el techo real, no un número mágico.
    // Invierte EngineRPM: v tal que EngineRPM(topGear, v) = redline.
    float GetTopSpeedKmh() {
        int nG = m_GearRatios.Count();
        if (nG == 0 || m_WheelRadius <= 0 || m_RPMRedline <= 0) return 0;
        float ratio = m_GearRatios[nG - 1] * m_FinalDrive * m_CentDiffRatio;
        if (ratio <= 0.0001) return 0;
        float v_ms = m_RPMRedline * m_WheelRadius * 6.28318530718 / (60.0 * ratio);
        return v_ms * 3.6;
    }

    // Vel maxima (a redline) del gear con indice de RATIO gi (0 = 1ra fisica). km/h.
    float GetTopSpeedForGearIndex(int gi) {
        if (gi < 0 || gi >= m_GearRatios.Count()) return 0;
        if (m_WheelRadius <= 0 || m_RPMRedline <= 0) return 0;
        float ratioG = m_GearRatios[gi] * m_FinalDrive * m_CentDiffRatio;
        if (ratioG <= 0.0001) return 0;
        float vG = m_RPMRedline * m_WheelRadius * 6.28318530718 / (60.0 * ratioG);
        return vG * 3.6;
    }

    // MaxGear (indice DayZ: 2 = 1ra) auto-derivado del CONFIG del vehiculo: el gear MAS BAJO
    // cuyo tope (a redline) alcanza routeMaxKmh. Generaliza a cualquier vehiculo (modded incluido:
    // los ratios se leen en vivo) y a rutas DIBUJADAS (sin datos de gear). El Nissan hace 82 en
    // 1ra -> devuelve 2. Tolerancia 0.92 (toma el gear aunque su tope quede apenas corto -> evita
    // forzar un gear mas alto que ahoga el motor). -1 si no hay data de caja.
    int MaxGearForRouteSpeed(float routeMaxKmh) {
        int n = m_GearRatios.Count();
        if (n <= 0 || m_WheelRadius <= 0 || m_RPMRedline <= 0) return -1;
        if (routeMaxKmh < 1.0) return -1;
        float thresh = routeMaxKmh * 0.92;
        for (int gi = 0; gi < n; gi++) {
            if (GetTopSpeedForGearIndex(gi) >= thresh) return gi + 2;
        }
        return n - 1 + 2;
    }

    // ========================================================================
    // CAPA 3: Speed PID -> desired_accel
    // ========================================================================
    float ComputeDesiredAccel(float targetSpeedKmH, float currentSpeedKmH, float dt) {
        if (dt < 0.001) dt = 0.001;
        float err = targetSpeedKmH - currentSpeedKmH;

        m_PIDIntegral += err * dt;
        // anti-windup
        if (m_PIDIntegral > 30) m_PIDIntegral = 30;
        if (m_PIDIntegral < -30) m_PIDIntegral = -30;

        float dErr = (err - m_PIDPrevError) / dt;
        m_PIDPrevError = err;

        float desiredAccelKmhS = m_PIDKp * err + m_PIDKi * m_PIDIntegral + m_PIDKd * dErr;
        return desiredAccelKmhS / 3.6; // -> m/s²
    }

    // ========================================================================
    // CAPA 4: desired_accel -> throttle | brake
    // ========================================================================
    // Returns: 0 = compute throttle, 1 = compute brake (via out params)
    void ComputeInputs(float desiredAccel, int currentDayZGear, float kmh, float slope, float surfFriction, float surfRolling, out float outThrottle, out float outBrake, out string note) {
        outThrottle = 0;
        outBrake = 0;
        note = "";

        // Convertir DayZ gear (FIRST=2) a sim 0-indexed
        int gearIdx0 = currentDayZGear - 2;
        if (gearIdx0 < 0 || gearIdx0 >= m_GearRatios.Count()) {
            // No valid gear -> bail
            note = "invalid_gear";
            return;
        }

        float v_ms = kmh / 3.6;
        float drag = 0.5 * m_DragCoef * m_FrontalArea * AIR_DENSITY * v_ms * v_ms;
        float rolling = ROLLING_COEF * m_Mass * G * surfRolling;
        float slopeForce = m_Mass * G * slope;
        float naturalDecel = drag + rolling + slopeForce;

        // Net force needed
        float needF = desiredAccel * m_Mass + naturalDecel;

        if (needF > 0) {
            // Necesitamos throttle
            float rpm = EngineRPM(gearIdx0, kmh);
            if (rpm < 1) { outThrottle = 1.0; note = "rpm_too_low"; return; }
            float maxT = Interp(m_TorqueCurve, rpm);
            if (maxT < 1) { outThrottle = 1.0; note = "no_torque"; return; }
            // TRANSFERENCIA DE PESO EN PENDIENTE (2026-08-07, Sonom4n). En subida el peso se va atras -> el FWD
            // (motriz adelante) DESCARGA sus ruedas motrices -> menos traccion; el RWD las CARGA; el AWD igual.
            // El framework lo ANTICIPA leyendo drive= (m_DriveKind). Sin esto el FWD (Hatchback) bogueaba en la
            // cuesta del galpon donde los AWD trepaban. slope>0 = subida. Clamp para no invertir el signo.
            float drivenRatioEff = m_DrivenAxleWeightRatio;
            if (m_DriveKind == 0) drivenRatioEff = drivenRatioEff - 1.5 * slope;   // FWD: subida descarga adelante
            else if (m_DriveKind == 1) drivenRatioEff = drivenRatioEff + 1.5 * slope;   // RWD: subida carga atras
            if (drivenRatioEff < 0.15) drivenRatioEff = 0.15;
            if (drivenRatioEff > 0.85) drivenRatioEff = 0.85;
            float maxTraction = m_Mass * G * surfFriction * drivenRatioEff;
            if (needF > maxTraction) { needF = maxTraction; note = "traction_limited"; }
            float needTorque = needF * m_WheelRadius / (m_GearRatios[gearIdx0] * m_FinalDrive * m_CentDiffRatio);
            float th = needTorque / maxT;
            if (th < 0) th = 0;
            if (th > 1) { th = 1; if (note == "") note = "saturated"; }
            outThrottle = th;
            outBrake = 0;
        } else {
            // Necesitamos frenar (o coast natural alcanza)
            float decelNeeded = -needF; // positivo
            float pressure = Interp(m_PressureBySpeed, kmh);
            if (pressure <= 0) pressure = 0.3; // fallback razonable
            float maxBrakeF = (m_MaxBrakeTorqueFront + m_MaxBrakeTorqueRear) * pressure / m_WheelRadius;
            float traction = m_Mass * G * surfFriction;
            if (maxBrakeF > traction) { maxBrakeF = traction; note = "brake_traction_limited"; }
            float br = decelNeeded / maxBrakeF;
            if (br < 0) br = 0;
            if (br > 1) { br = 1; if (note == "") note = "brake_saturated"; }
            outThrottle = 0;
            outBrake = br;
        }
    }

    // ========================================================================
    // CAPA 5: GEAR SELECTOR
    // Selecciona el gear optimo para velocidad + accel deseado.
    // Mantiene RPM en banda util (sobre idle, debajo redline).
    // Anticipa: si vamos a acelerar fuerte, prefiere gear con mas torque disponible.
    // Hysteresis 800ms para evitar shift-thrashing.
    // ========================================================================
    private float m_LastShiftTime;
    private const float SHIFT_LOCK_S = 2.0;  // subido de 0.8 a 2.0 — evita thrashing en transiciones de cruise

    // RPM min multiplier — configurable via BZBusConfig.InverseModelLowRpmMin
    // false (default) = 1.3 (conservador), true = 1.0 (gear amortiguado)
    private float m_RPMMinMultiplier = 1.3;

    void SetRPMMinMultiplier(float v) {
        if (v >= 1.0 && v <= 2.0) m_RPMMinMultiplier = v;
    }

    int SelectGear(int currentDayZGear, float currentKmh, float targetKmh, float desiredAccel) {
        // Hysteresis: si shifteamos recien, esperar
        float now = GetGame().GetTickTime();
        if (now - m_LastShiftTime < SHIFT_LOCK_S) return currentDayZGear;

        if (m_GearRatios.Count() == 0) return currentDayZGear;

        // Banda util de RPM
        float rpmMin = m_RPMIdle * m_RPMMinMultiplier;
        float rpmMax = m_RPMRedline * 0.85;

        // Si estamos casi parados, gear 1 fijo
        if (currentKmh < 5) {
            int g1 = 2; // FIRST in DayZ convention
            if (g1 != currentDayZGear) m_LastShiftTime = now;
            return g1;
        }

        // Buscar el gear MAS ALTO que mantiene RPM en banda
        int bestSimGear = 0;
        bool found = false;
        for (int g = 0; g < m_GearRatios.Count(); g++) {
            float rpm = EngineRPM(g, currentKmh);
            if (rpm >= rpmMin && rpm <= rpmMax) {
                bestSimGear = g;
                found = true;
            }
        }

        // Si no hay gear que cumple banda (vehiculo muy lento o muy rapido)
        if (!found) {
            // Si estamos a velocidad alta, usar el ultimo gear
            if (currentKmh > 20) bestSimGear = m_GearRatios.Count() - 1;
            // Si lento, primer gear
            else bestSimGear = 0;
        }

        // Shift down agresivo SOLO cuando esta MUY lejos del target Y desiredA muy alto
        // (evita gear thrashing en aceleracion normal)
        // Threshold subido de 1.5 a 2.5 m/s² + chequear si RPM actual ya esta bajo
        if (desiredAccel > 2.5 && bestSimGear > 0) {
            float rpmCurrent = EngineRPM(bestSimGear, currentKmh);
            // Solo bajar si el RPM actual esta en la mitad inferior de la banda util
            float bandMid = (m_RPMIdle * 1.3 + m_RPMRedline * 0.85) * 0.5;
            if (rpmCurrent < bandMid) {
                float rpmIfDown = EngineRPM(bestSimGear - 1, currentKmh);
                if (rpmIfDown < m_RPMRedline * 0.9) {
                    bestSimGear = bestSimGear - 1;
                }
            }
        }

        // Convertir back to DayZ convention (FIRST=2)
        int newDayZGear = bestSimGear + 2;

        if (newDayZGear != currentDayZGear) {
            m_LastShiftTime = now;
            BZBusLog.Info("[InvModel-Gear] shift " + currentDayZGear + " -> " + newDayZGear + " (kmh=" + currentKmh + " RPM_in_new=" + EngineRPM(bestSimGear, currentKmh) + " desiredA=" + desiredAccel + ")");
        }

        return newDayZGear;
    }

    // ========================================================================
    // Helper: detectar slope desde 2 wps (signed, + = subida, - = bajada)
    // ========================================================================
    float ComputeSlope(vector wpStart, vector wpEnd) {
        vector horiz = Vector(wpEnd[0] - wpStart[0], 0, wpEnd[2] - wpStart[2]);
        float dxz = horiz.Length();
        if (dxz < 0.1) return 0;
        return (wpEnd[1] - wpStart[1]) / dxz;
    }

    // ========================================================================
    // Surface friction lookup (desde CfgSurfaces) — fallback asphalt
    // ========================================================================
    float GetSurfaceFriction(string surfaceName) {
        if (surfaceName == "" || surfaceName.Contains("asphalt")) return 1.0;
        // Hardcoded multipliers (sin tener que leer CfgSurfaces que es complejo)
        if (surfaceName.Contains("grass")) return 0.5;
        if (surfaceName.Contains("gravel")) return 0.7;
        if (surfaceName.Contains("mud") || surfaceName.Contains("dirt")) return 0.4;
        if (surfaceName.Contains("sand")) return 0.4;
        if (surfaceName.Contains("snow")) return 0.25;
        if (surfaceName.Contains("ice")) return 0.1;
        if (surfaceName.Contains("concrete") || surfaceName.Contains("road")) return 0.95;
        return 0.7; // unknown -> conservador
    }

    float GetSurfaceRolling(string surfaceName) {
        if (surfaceName == "" || surfaceName.Contains("asphalt")) return 1.0;
        if (surfaceName.Contains("grass")) return 3.0;
        if (surfaceName.Contains("gravel")) return 1.5;
        if (surfaceName.Contains("mud") || surfaceName.Contains("dirt")) return 5.0;
        if (surfaceName.Contains("sand")) return 4.0;
        if (surfaceName.Contains("snow")) return 2.0;
        if (surfaceName.Contains("ice")) return 0.5;
        if (surfaceName.Contains("concrete") || surfaceName.Contains("road")) return 1.0;
        return 1.5;
    }

    // Decel maximo FISICO que ESTE vehiculo puede frenar (m/s^2), leido de SU config:
    // fuerza de freno (Tf+Tr)/R sobre la masa, ACOTADO por el agarre del piso (mu*g).
    // Es la base del lookahead anti-obstaculo: pesado + frenos flojos = frena menos = hay
    // que verlo de mas lejos. surfaceName "" -> asfalto (agarre pleno ~9.8).
    float GetMaxBrakeDecel(string surfaceName) {
        if (m_Mass <= 0 || m_WheelRadius <= 0) return 4.5;
        float brakeForce = (m_MaxBrakeTorqueFront + m_MaxBrakeTorqueRear) / m_WheelRadius;
        float aBrake = brakeForce / m_Mass;
        float aGrip = GetSurfaceFriction(surfaceName) * G;   // limite de agarre (asfalto ~9.8, hielo ~1)
        if (aBrake > aGrip) aBrake = aGrip;
        return aBrake;
    }
}
