// ============================================================================
//  CarScript modded - automatic transmission + input forwarding para el bus
//
//  Tercer culpable identificado: eAI mete ShiftTo(CarGear.FIRST) cada frame
//  en su propio modded CarScript.OnInput (entities/carscript.c linea 111).
//  Si solo llamamos ShiftUp desde aca, eAI lo borra al siguiente frame.
//
//  Solucion: m_DesiredGear vive en BZBusService. La AT (RPM-based) lo
//  modifica. Nuestro OnInput corre DESPUES de super.OnInput (que ejecuta el
//  ShiftTo(FIRST) de eAI) y reinmediatamente sobreescribe con ShiftTo(desired).
//  Misma estrategia que ya usamos para throttle/steering.
// ============================================================================

modded class CarScript {

    const int BZBUS_SHIFTDOWN_TRESHOLD = 2000;
    const int BZBUS_SHIFT_DELAY_MS     = 800;
    const float BZBUS_SHIFT_UP_FACTOR  = 0.95;  // Subido de 0.75 -> 0.95 (2026-05-23): experimento System ID revelo que shift temprano a gear 5 dejaba el motor sin torque. Con 0.95 cada gear se exprime hasta cerca del redline.
    // BZBUS_MAX_GEAR ahora se lee desde el JSON (BZBusRouteConfig.MaxGear) via
    // srv.GetMaxGear(). Bus=6 (5ta), Land Rover Expansion=7 (6ta), etc.

    bool  m_BZBus_isShifting;
    int   m_BZBus_LogCounter;
    float m_BZBus_LastKmh;       // para medir aceleracion entre frames
    float m_BZBus_LastSampleTime; // tickTime del ultimo sample de kmh

    // ========================================================================
    //  FASE 2 LUCES — replicacion del haz del faro para el OBSERVADOR (2026-06-27)
    //
    //  PROBLEMA (diagnostico confirmado leyendo scripts.pbo vanilla):
    //    LightOn()/LightOff()/LightToggle()/LightIsOn() son proto native de
    //    Transport (base de CarScript). Son un FLAG LOCAL del engine, NO una
    //    NetSyncVariable. El engine solo replica/dispara OnUpdateLight() en el
    //    cliente que SIMULA el vehiculo (owner / player driver). Para un auto
    //    manejado por la IA server-side SIN player en seat 0, el observador
    //    nunca simula el auto -> nunca se le setea el flag native local ->
    //    LightIsOn() devuelve false en su cliente -> UpdateLightsClient() (que
    //    exige LightIsOn()==true) NUNCA crea m_Headlight -> no se ve el haz.
    //    Por eso ToggleHeadlights()+ForceUpdateLightsStart() NO alcanzo:
    //    ForceUpdateLights re-dispara UpdateLights() en el cliente, pero
    //    UpdateLightsClient sigue viendo LightIsOn()==false.
    //
    //    (La BOCINA si replica porque m_CarHornState SI es RegisterNetSyncVariableInt
    //    y SetCarHornState llama SetSynchDirty(): viaja por el canal de script-sync,
    //    no por el sistema de luces native.)
    //
    //  FIX: replicar nosotros el estado deseado via una NetSyncVariable propia
    //    (m_BZLightsWanted) y, en OnVariablesSynchronized() (corre en TODOS los
    //    clientes que reciben sync del auto), FORZAR el flag native local con
    //    LightOn()/LightOff() y re-correr UpdateLights(). Asi UpdateLightsClient
    //    ve LightIsOn()==true y crea m_Headlight para el observador.
    //
    //    Usamos LightOn()/LightOff() (no LightToggle): el docstring vanilla los
    //    define como "Signals for LightIsOn to return true/false" — son setters
    //    INCONDICIONALES. LightToggle() en cambio pasa por OnBeforeLightOn(), que
    //    chequea bateria/energia y en el cliente observador puede no estar synced
    //    (devolveria false y bloquearia el encendido).
    // ========================================================================
    bool m_BZLightsWanted;  // NetSync: estado de luz que el server QUIERE en clientes
    int  m_BZLightPulse;    // NetSync: contador "pulso". SetSynchDirty() SOLO re-dispara
                            // OnVariablesSynchronized() en el observador si CAMBIA el VALOR
                            // de una NetSyncVar (dirty pelado sin cambio = no-op). El heartbeat
                            // incrementa este contador -> fuerza el sync -> re-aplica LightOn().

    void CarScript() {
        // El constructor de la base ya registro sus NetSyncVariables (ver
        // scripts.pbo: m_BrakesArePressed, m_ForceUpdateLights, m_CarHornState...).
        // Agregamos la nuestra; el orden de registro debe ser identico server/cliente,
        // garantizado porque esta clase modded se compila igual en ambos lados.
        RegisterNetSyncVariableBool("m_BZLightsWanted");
        RegisterNetSyncVariableInt("m_BZLightPulse");
    }

    // Heartbeat: llamado server-side periodicamente por BZBusService cuando las
    // luces deberian estar prendidas. CAMBIA el valor de m_BZLightPulse (no solo
    // dirty) para garantizar que OnVariablesSynchronized() se dispare en el
    // observador y re-aplique el flag native LightOn() (el engine lo resetea en
    // el cliente que no simula el AI car -> las luces "se apagan solas").
    void BZPulseLights() {
        if (GetGame().IsServer()) {
            m_BZLightPulse = (m_BZLightPulse + 1) % 100000;
            SetSynchDirty();
        }
    }

    // Llamado server-side por BZBusService.SetVehicleLights. Setea el estado
    // deseado, lo aplica server-side (para fisica/IA/consumo) y lo marca dirty
    // para que viaje a los observadores.
    void BZSetLights(bool on) {
        if (GetGame().IsServer()) {
            // Aplicar el estado native server-side (idempotente: solo si difiere).
            if (on && !IsScriptedLightsOn())  ToggleHeadlights();
            else if (!on && IsScriptedLightsOn()) ToggleHeadlights();

            m_BZLightsWanted = on;
            SetSynchDirty();   // dispara OnVariablesSynchronized() en los clientes
        }
    }

    override void OnVariablesSynchronized() {
        super.OnVariablesSynchronized();   // la base llama UpdateLights() al final

        // Solo client-side tiene sentido forzar el faro visual.
        if (GetGame().IsServer()) return;

        // Re-correr el update de luz para que nuestro override de UpdateLightsClient()
        // (abajo) cree/mantenga el haz segun m_BZLightsWanted. Ya NO tocamos LightOn()/
        // LightIsOn() aca: el control real del faro pasa por el override.
        UpdateLights();
    }

    // ========================================================================
    //  OVERRIDE del update de luz CLIENT-SIDE — saca el FARO DELANTERO del control
    //  del flag native LightIsOn() (2026-06-28).
    //
    //  POR QUE: el vanilla UpdateLightsClient() crea/mantiene m_Headlight SOLO si
    //  LightIsOn()==true, y lo DESTRUYE (FadeOut + null) en el else. Para el
    //  observador de un AI car (cliente que NO simula el vehiculo), LightIsOn()
    //  devuelve casi siempre false aunque el server quiera la luz prendida ->
    //  cada vez que el engine corre su propio UpdateLightsClient (via OnUpdateLight)
    //  entra al else y mata el haz. Resultado: por mas que lo re-creemos en cada
    //  pulse, el engine lo borra sub-segundo -> parpadeo permanente. El flag native
    //  NO es overrideable (proto native), pero UpdateLightsClient() SI es script.
    //
    //  FIX: reimplementar UpdateLightsClient() con UN solo cambio respecto al vanilla
    //  — la condicion del FARO DELANTERO pasa de `LightIsOn()` a
    //  `(LightIsOn() || m_BZLightsWanted)`. Asi, mientras el server quiera la luz,
    //  el engine YA NO puede destruir m_Headlight (esta rama es la que corre). Las
    //  luces traseras / freno / reversa quedan IDENTICAS al vanilla (copia literal),
    //  para no cambiar comportamiento. Helpers usados (BrakesRearLight, NoRearLight,
    //  CreateRearLight, memory points, m_HeadlightsState, m_BrakesArePressed) son
    //  protected/static -> accesibles desde esta subclase.
    //
    //  Riesgo version-drift: bajo. Este metodo es estable; el mod esta pineado a una
    //  version de DayZ. Si Bohemia cambia UpdateLightsClient, re-sincronizar esta copia.
    // ========================================================================
    override void UpdateLightsClient(int newGear = -1) {
        int gear;
        if (newGear == -1) {
            gear = GetGear();
        } else {
            gear = newGear;
        }

        // ---- FARO DELANTERO (UNICO cambio vs vanilla: || m_BZLightsWanted) ----
        bool wantFront = LightIsOn() || m_BZLightsWanted;
        if (wantFront) {
            if (!m_Headlight && m_HeadlightsState != CarHeadlightBulbsState.NONE) {
                m_Headlight = CreateFrontLight();
            }

            if (m_Headlight) {
                switch (m_HeadlightsState) {
                case CarHeadlightBulbsState.LEFT:
                    m_Headlight.AttachOnMemoryPoint(this, m_LeftHeadlightPoint, m_LeftHeadlightTargetPoint);
                    m_Headlight.SegregateLight();
                    break;
                case CarHeadlightBulbsState.RIGHT:
                    m_Headlight.AttachOnMemoryPoint(this, m_RightHeadlightPoint, m_RightHeadlightTargetPoint);
                    m_Headlight.SegregateLight();
                    break;
                case CarHeadlightBulbsState.BOTH:
                    vector local_pos_left = GetMemoryPointPos(m_LeftHeadlightPoint);
                    vector local_pos_right = GetMemoryPointPos(m_RightHeadlightPoint);
                    vector local_pos_middle = (local_pos_left + local_pos_right) * 0.5;
                    m_Headlight.AttachOnObject(this, local_pos_middle);
                    m_Headlight.AggregateLight();
                    break;
                default:
                    m_Headlight.FadeOut();
                    m_Headlight = null;
                }
            }
        } else {
            if (m_Headlight) {
                m_Headlight.FadeOut();
                m_Headlight = null;
            }
        }

        // ---- LUCES TRASERAS / FRENO / REVERSA (copia literal del vanilla) ----
        switch (gear) {
            case CarGear.REVERSE:
            case CarAutomaticGearboxMode.R:
                if (m_BrakesArePressed)
                    m_RearLightType = CarRearLightType.BRAKES_AND_REVERSE;
                else
                    m_RearLightType = CarRearLightType.REVERSE_ONLY;
                break;
            default:
                if (m_BrakesArePressed)
                    m_RearLightType = CarRearLightType.BRAKES_ONLY;
                else
                    m_RearLightType = CarRearLightType.NONE;
        }

        if (!m_RearLight && m_RearLightType != CarRearLightType.NONE && m_HeadlightsState != CarHeadlightBulbsState.NONE) {
            if (EngineIsOn() || m_RearLightType == CarRearLightType.BRAKES_ONLY || m_RearLightType == CarRearLightType.BRAKES_AND_REVERSE) {
                m_RearLight = CreateRearLight();
                vector localPos = GetMemoryPointPos(m_ReverseLightPoint);
                m_RearLight.AttachOnObject(this, localPos, "180 0 0");
            }
        }

        if (m_RearLight && m_RearLightType != CarRearLightType.NONE && m_HeadlightsState != CarHeadlightBulbsState.NONE) {
            switch (m_RearLightType) {
                case CarRearLightType.BRAKES_ONLY:
                    BrakesRearLight();
                    break;
                case CarRearLightType.REVERSE_ONLY:
                    if (EngineIsOn())
                        ReverseRearLight();
                    else
                        NoRearLight();
                    break;
                case CarRearLightType.BRAKES_AND_REVERSE:
                    if (EngineIsOn())
                        BrakeAndReverseRearLight();
                    else
                        BrakesRearLight();
                    break;
                default:
                    NoRearLight();
            }
        } else {
            if (m_RearLight) {
                NoRearLight();
            }
        }
    }

    // ESCENA 2 (ambush): si le pegan un tiro al chasis del vehiculo gestionado, avisar al service para
    // que dispare el despliegue. Guard IsBusActive(this) -> solo reacciona el bus del convoy, no todos los autos.
    override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef) {
        super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
        if (!GetGame().IsServer()) return;
        BZBusService srv = BZBusService.GetRunnerForCar(this);   // MULTITON: el runner DUEÑO de este auto
        if (srv) srv.NotifyConvoyDamaged();
        // AR_OnWay fase 2: si el que pega es OTRO VEHICULO (choque), avisar para el escape.
        if (srv && source && CarScript.Cast(source)) srv.NotifyHitByVehicle();
    }

    override void OnInput(float dt) {
        super.OnInput(dt);

        if (!GetGame().IsServer()) return;

        BZBusService srv = BZBusService.GetRunnerForCar(this);   // MULTITON: el runner DUEÑO de este auto (no el primary)
        if (!srv) return;

        // Si el conductor en seat 0 es un PLAYER REAL (no eAI), el service se
        // hace a un lado: no aplicar cached input, dejar al player manejar
        // libremente. Util para "tomar el bus" y continuar una grabacion
        // humana desde donde quedo, sin matar el service ni regenerar el bus.
        //
        // IMPORTANTE: en DayZ, eAI hereda de PlayerBase, asi que PlayerBase.Cast
        // tambien devuelve true para Boris. La forma correcta de diferenciar
        // player real de eAI es por GetIdentity() — los players reales tienen
        // PlayerIdentity, los eAI no. Sin este check, el service "se hacia a
        // un lado" cuando Boris estaba al volante y el bus quedaba sin inputs.
        Human driver = CrewMember(0);
        if (driver) {
            PlayerBase realPlayer = PlayerBase.Cast(driver);
            if (realPlayer && realPlayer.GetIdentity()) {
                return;
            }
        }

        // Aplicar throttle/steering/brake cacheado (sobreescribe lo que eAI puso)
        srv.ApplyBusInput(this, dt);

        // Sobreescribir el gear: eAI metio FIRST en super.OnInput, ponemos el deseado
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) {
            ShiftTo(desired);
        }
    }

    override void EOnPostSimulate(IEntity other, float timeSlice) {
        super.EOnPostSimulate(other, timeSlice);

        // Grabador frame-by-frame (2026-07-05): captura los inputs del humano a
        // resolucion de fisica para replay temporal fiel. Corre en el cliente que
        // maneja (jugador local); Frame() solo graba el auto que arranco la toma.
        // ANTES del early-out srv==null (el auto del humano NO tiene runner).
        BZFrameRecorder frec = BZFrameRecorder.GetInstance();
        if (frec.IsActive()) frec.Frame(this, timeSlice);

        // Server-side probe (2026-07-05): captura la salida del TRADUCTOR autoritativo (motor
        // server-side) para el auto que maneja un jugador real. Localiza la divergencia
        // cliente-vs-servidor comparando contra el frame_ (cliente). Auto-detecta, sin trigger.
        if (GetGame().IsServer()) BZServerProbe.GetInstance().Frame(this, timeSlice);

        BZBusService srv = BZBusService.GetRunnerForCar(this);   // MULTITON: el runner DUEÑO de este auto (no el primary)
        if (!srv) return;

        // V2 physics receiver portado a v1 (2026-07-04): muestrea el estado fisico real
        // cada frame de fisica. Aditivo (solo loguea si NUMPAD 7 esta activo).
        srv.SampleReceiver(this, timeSlice);

        // Cuando GearStrategy="follow_recording" esta activo, la AT por RPM no
        // debe correr: el recording es source of truth para gear. Si la AT
        // sigue ajustando m_DesiredGear por RPM, sobreescribe lo que pidio
        // el recording y queda en gears bajos (RPM no sube en bajada).
        BZBusRouteConfig cfg = srv.GetConfig();
        // Cuando UseInverseModel=true, el v2 gear selector (BZInverseModel.SelectGear,
        // corre en el tick del service con lock 2s) es el UNICO que decide gear.
        // Si la AT por RPM tambien corre aca (cada frame de fisica, lock 800ms), los
        // dos selectores se pisan -> gear thrashing 3<->4<->5 cada ~0.5s (visible en
        // la palanca) y shift-up prematuro que deja el motor sin vueltas -> velocidad
        // capeada. Fix 2026-06-28: desactivar la AT tambien con UseInverseModel, no
        // solo con follow_recording. El gear de parking/reverse lo setea el service
        // directo (SetDesiredGear en los paths directReplay), no la AT.
        if (cfg && (cfg.GearStrategy == "follow_recording" || cfg.UseInverseModel || cfg.FrameReplay)) return;

        if (!EngineIsOn() || m_BZBus_isShifting) return;

        int gear = GetGear();
        int desired = srv.GetDesiredGear();
        float rpm = EngineGetRPM();
        float redline = EngineGetRPMRedline();

        m_BZBus_LogCounter++;
        if (m_BZBus_LogCounter >= 60) {
            m_BZBus_LogCounter = 0;
            BZBusLog.Info("AT: gear=" + gear + " desired=" + desired + " rpm=" + rpm + " redline=" + redline);
        }

        // No shiftear si todavia estamos en REVERSE (0) o NEUTRAL (1)
        if (gear < 2) return;

        int maxGear = srv.GetMaxGear();

        // Medir aceleracion instantanea (km/h por segundo) para trigger
        // anti-catapulta. Si la aceleracion supera el umbral del vehicle
        // profile, shiftear UP para reducir torque a las ruedas.
        float kmhNow = GetSpeedometerAbsolute();
        float tNow = GetGame().GetTickTime();
        float accelKmhPerSec = 0;
        if (m_BZBus_LastSampleTime > 0) {
            float dt = tNow - m_BZBus_LastSampleTime;
            if (dt > 0.01) {
                accelKmhPerSec = (kmhNow - m_BZBus_LastKmh) / dt;
            }
        }
        m_BZBus_LastKmh = kmhNow;
        m_BZBus_LastSampleTime = tNow;

        float accelThreshold = srv.GetAccelShiftThreshold();

        if (rpm >= redline * BZBUS_SHIFT_UP_FACTOR && desired < maxGear) {
            srv.SetDesiredGear(desired + 1);
            BZBusLog.Info("AT desired ++ (RPM): " + desired + " -> " + (desired + 1) + " (rpm=" + rpm + ")");
            BZBus_LockShift();
        } else if (accelKmhPerSec > accelThreshold && desired < maxGear && gear >= 2 && kmhNow > 5) {
            // Anti-catapulta: aceleracion excesiva, shift up para suavizar
            srv.SetDesiredGear(desired + 1);
            BZBusLog.Info("AT desired ++ (ANTI-CATAPULTA): " + desired + " -> " + (desired + 1) + " (accel=" + accelKmhPerSec + " km/h/s > umbral=" + accelThreshold + ")");
            BZBus_LockShift();
        } else if (rpm <= BZBUS_SHIFTDOWN_TRESHOLD && desired > 2) {
            srv.SetDesiredGear(desired - 1);
            BZBusLog.Info("AT desired --: " + desired + " -> " + (desired - 1) + " (rpm=" + rpm + ")");
            BZBus_LockShift();
        }
    }

    // FASE 2a (horn+lights): getter publico para que el PathLogger lea la bocina.
    // m_CarHornState es protected en CarScript; esta clase es subclase -> acceso OK.
    int BZBus_GetCarHornState() {
        return m_CarHornState;
    }

    void BZBus_LockShift() {
        m_BZBus_isShifting = true;
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(BZBus_DoneShifting, BZBUS_SHIFT_DELAY_MS);
    }

    void BZBus_DoneShifting() {
        m_BZBus_isShifting = false;
    }
}
