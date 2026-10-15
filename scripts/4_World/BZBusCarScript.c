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
    bool m_BZLuzDiagDado;      // el diagnostico de luces del cliente, una vez por vehiculo
    bool m_BZLuzClientCorrio;  // nuestro override de UpdateLightsClient corrio en este cliente
    bool m_BZHazPropio;        // el haz lo creo NUESTRA copia (solo con m_BZLightsWanted): la limpieza al soltar la luz no toca el haz vanilla de un auto de jugador
    bool m_BZLightsWanted;  // NetSync: estado de luz que el server QUIERE en clientes
    bool m_BZPartsLocked;   // NetSync: VEHICULO INVIOLABLE, lo fija el runner (BZBusService) sobre el auto que maneja.
                            // Default false = vanilla en cualquier auto sin runner (esta clase moddeada toca a TODOS).
    int  m_BZLightPulse;    // NetSync: contador "pulso". SetSynchDirty() SOLO re-dispara
                            // OnVariablesSynchronized() en el observador si CAMBIA el VALOR
                            // de una NetSyncVar (dirty pelado sin cambio = no-op). El heartbeat
                            // incrementa este contador -> fuerza el sync -> re-aplica LightOn().

    // el adaptador de este vehiculo (BZVehiculoAuto): lo crea la fabrica la primera vez que un gancho lo pide
    ref BZVehiculo m_BZV;

    BZVehiculo BZV() {
        if (!m_BZV) m_BZV = BZVehiculoFabrica.Para(this);
        return m_BZV;
    }

    void CarScript() {
        // El constructor de la base ya registro sus NetSyncVariables (ver
        // scripts.pbo: m_BrakesArePressed, m_ForceUpdateLights, m_CarHornState...).
        // Agregamos la nuestra; el orden de registro debe ser identico server/cliente,
        // garantizado porque esta clase modded se compila igual en ambos lados.
        RegisterNetSyncVariableBool("m_BZLightsWanted");
        RegisterNetSyncVariableInt("m_BZLightPulse");
        RegisterNetSyncVariableBool("m_BZPartsLocked");
    }

    // VEHICULO INVIOLABLE. Con el auto parado, el motor deja soltar ruedas, puertas, bateria y demas (CarScript.
    // CanReleaseAttachment solo lo impide con motor en marcha Y en movimiento); un vehiculo del framework con dos
    // ruedas menos queda detenido para siempre, y "quitar rueda" competia con "subir" en la puerta del conductor.
    // El valor viaja por NetSync porque las acciones vanilla (ActionDetach, ActionDetachFromTarget, ActionTakeItem)
    // preguntan esto en el CLIENTE para mostrarse: sincronizado, el cartel ni aparece; el server rechaza igual.
    // No cubre la carga (el botin sigue accesible), ni subir/bajar, ni PONER piezas (CanReceiveAttachment).
    // OJO: un mod de vehiculos que sobreescriba CanReleaseAttachment sin llamar a super se saltea el bloqueo.
    void BZSetPartsLocked(bool locked) {
        if (!GetGame().IsServer()) return;
        if (m_BZPartsLocked == locked) return;
        m_BZPartsLocked = locked;
        SetSynchDirty();
    }

    bool BZPartsLocked() { return m_BZPartsLocked; }

    override bool CanReleaseAttachment(EntityAI attachment) {
        if (m_BZPartsLocked) return false;
        return super.CanReleaseAttachment(attachment);
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
            // LightToggle() (proto native de Transport) y no ToggleHeadlights(): en la 1.29 ToggleHeadlights era
            // exactamente `LightToggle();` con un TODO, y en la 1.30 ya no existe (la accion vanilla de luces llama
            // transport.LightToggle()). Asi el mismo build compila en las dos versiones del motor.
            if (on && !IsScriptedLightsOn())  LightToggle();
            else if (!on && IsScriptedLightsOn()) LightToggle();

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
        // una linea por vehiculo en el RPT del CLIENTE la primera vez que el server pide luz:
        // dice si NUESTRO UpdateLightsClient corrio (Expansion lo saltea si el vehiculo no es "car") y si el haz existe.
        if (m_BZLightsWanted && !m_BZLuzDiagDado)
        {
            m_BZLuzDiagDado = true;
            bool hazBZ = false;
#ifdef DAYZ_1_29
            hazBZ = (m_Headlight != null);
#else
            hazBZ = m_LightsComponent.LightExist("Front");   // 1.30: el haz vive en el componente de luces
#endif
            string dgLuz = "[BZLuces] " + GetType() + " · el server pide luz · UpdateLightsClient (override BZ) corrio: " + m_BZLuzClientCorrio.ToString();
            dgLuz = dgLuz + " · LightIsOn " + LightIsOn().ToString() + " · m_Headlight existe: " + hazBZ.ToString() + " · lamparas " + m_HeadlightsState.ToString();
            Print(dgLuz);
        }
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
        m_BZLuzClientCorrio = true;
#ifdef DAYZ_1_29
        // SOLO los autos que el framework prende pasan por la copia. Los demas (los de los jugadores) siguen la cadena
        // de siempre (super = el vanilla; Expansion no sobrescribe este metodo, sobrescribe UpdateLights). Hace falta
        // desde la 1.30: ahi el vanilla maneja el haz con VehicleLightsComponent, y esta copia vieja le pondria a TODOS
        // los autos el faro generico y los dejaria sin luces traseras (CreateRearLight devuelve un faro delantero
        // casteado a trasera = null). Al soltar la luz se apaga el haz que creo la copia, porque el vanilla nuevo ya
        // no mira m_Headlight/m_RearLight; el haz de un auto de jugador (m_BZHazPropio false) no se toca.
        if (!m_BZLightsWanted) {
            if (m_BZHazPropio) {
                m_BZHazPropio = false;
                if (m_Headlight) {
                    m_Headlight.FadeOut();
                    m_Headlight = null;
                }
                if (m_RearLight) {
                    m_RearLight.FadeOut();
                    m_RearLight = null;
                }
            }
            super.UpdateLightsClient(newGear);
            return;
        }
        m_BZHazPropio = true;
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
                if (m_RearLight) m_RearLight.AttachOnObject(this, localPos, "180 0 0");   // en la 1.30 CreateRearLight da null
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
#else
        // DayZ 1.30 (y siguientes): la copia del vanilla 1.30 (CarScript.c:1861-1972) con el UNICO cambio de siempre en el
        // faro delantero: (LightIsOn() || m_BZLightsWanted). En la 1.30 el haz lo maneja m_LightsComponent con el perfil de
        // luz de cada vehiculo; la copia 1.29 le ponia a todos el faro generico y dejaba sin traseras (CreateRearLight da
        // null). Medido en el server Exp el 27/09: el flag LightIsOn tampoco llega al observador, asi que la copia sigue
        // haciendo falta. Sin m_BZLightsWanted va el vanilla: al soltar la luz, el super apaga el "Front" del componente
        // (es el mismo que prendio esta copia), y un auto de jugador no se toca.
        if (!m_BZLightsWanted) {
            super.UpdateLightsClient(newGear);
            return;
        }
        int gear130;
        if (newGear == -1) {
            gear130 = GetGear();
        } else {
            gear130 = newGear;
        }

        // ---- FARO DELANTERO (UNICO cambio vs vanilla: || m_BZLightsWanted) ----
        if (LightIsOn() || m_BZLightsWanted) {
            if (!m_LightsComponent.LightExist("Front") && m_HeadlightsState != CarHeadlightBulbsState.NONE)
                m_LightsComponent.EnableLight("Front");

            if (m_LightsComponent.LightExist("Front")) {
                switch (m_HeadlightsState) {
                    case CarHeadlightBulbsState.LEFT:
                        m_LightsComponent.AttachLightOnPoint("Front", m_LeftHeadlightPoint, m_LeftHeadlightTargetPoint, VehicleLightMode.SEGREGATED);
                        break;
                    case CarHeadlightBulbsState.RIGHT:
                        m_LightsComponent.AttachLightOnPoint("Front", m_RightHeadlightPoint, m_RightHeadlightTargetPoint, VehicleLightMode.SEGREGATED);
                        break;
                    case CarHeadlightBulbsState.BOTH:
                        vector posIzq130 = GetMemoryPointPos(m_LeftHeadlightPoint);
                        vector posDer130 = GetMemoryPointPos(m_RightHeadlightPoint);
                        vector posMedio130 = (posIzq130 + posDer130) * 0.5;
                        m_LightsComponent.AttachLightOnObject("Front", posMedio130, vector.Zero, VehicleLightMode.AGGREGATED);
                        break;
                    default:
                        m_LightsComponent.DisableLight("Front");
                }
            }
        } else {
            if (m_LightsComponent.LightExist("Front"))
                m_LightsComponent.DisableLight("Front");
        }

        // ---- LUCES TRASERAS / FRENO / REVERSA (copia literal del vanilla 1.30) ----
        switch (gear130) {
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

        if (!m_LightsComponent.LightExist("Rear") && m_RearLightType != CarRearLightType.NONE && m_HeadlightsState != CarHeadlightBulbsState.NONE) {
            if (EngineIsOn() || m_RearLightType == CarRearLightType.BRAKES_ONLY || m_RearLightType == CarRearLightType.BRAKES_AND_REVERSE) {
                m_LightsComponent.EnableLight("Rear");
                m_LightsComponent.AttachLightOnObject("Rear", GetMemoryPointPos(m_ReverseLightPoint), "180 0 0", VehicleLightMode.SEGREGATED);
            }
        }

        if (m_LightsComponent.LightExist("Rear") && m_RearLightType != CarRearLightType.NONE && m_HeadlightsState != CarHeadlightBulbsState.NONE) {
            switch (m_RearLightType) {
                case CarRearLightType.BRAKES_ONLY:
                    m_LightsComponent.SetLightTypeAndMode("Rear", VehicleLightType.BRAKE, VehicleLightMode.SEGREGATED);
                    break;
                case CarRearLightType.REVERSE_ONLY:
                    if (EngineIsOn())
                        m_LightsComponent.SetLightTypeAndMode("Rear", VehicleLightType.REVERSE, VehicleLightMode.SEGREGATED);
                    else
                        m_LightsComponent.DisableLight("Rear");
                    break;
                case CarRearLightType.BRAKES_AND_REVERSE:
                    if (EngineIsOn())
                        m_LightsComponent.SetLightTypeAndMode("Rear", VehicleLightType.REVERSE_BRAKE, VehicleLightMode.AGGREGATED);
                    else
                        m_LightsComponent.SetLightTypeAndMode("Rear", VehicleLightType.BRAKE, VehicleLightMode.SEGREGATED);
                    break;
                default:
                    m_LightsComponent.DisableLight("Rear");
            }
        } else {
            if (m_LightsComponent.LightExist("Rear"))
                m_LightsComponent.DisableLight("Rear");
        }
#endif
    }

    // ESCENA 2 (ambush): si le pegan un tiro al chasis del vehiculo gestionado, avisar al service para
    // que dispare el despliegue. Guard IsBusActive(this) -> solo reacciona el bus del convoy, no todos los autos.
    override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef) {
        super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
        BZVehiculoGanchos.Golpe(this, source);   // el cuerpo, compartido con la moto (BZVehiculoGanchos)
    }

    // QUIEN FUE. El detector de choque del cerebro sabe que la desaceleracion es imposible para un
    // neumatico, pero no contra que pego: la corrida de PATRULLA se explico recien cuando el usuario fue a mirar
    // y encontro un Landrover estacionado en la calzada. El motor ya tiene el dato y lo pasa por aca.
    // Barato a proposito: esto se llama MUCHAS veces por frame (lo avisa el propio CarScript vanilla), asi que
    // guarda el contacto de mayor impulso de la ventana y resuelve el nombre de clase SOLO cuando cambia el objeto
    // (rodar sobre el terreno da `other` nulo siempre: mismo objeto, cero resoluciones).
    Object m_BzContactoObj;
    string m_BzContactoClase;
    string m_BzContactoZona;   // la zona del casco de ese contacto (la linea del choque la muestra)
    float  m_BzContactoImp;
    float  m_BzContactoT = -1;
    private const float BZ_CONTACTO_VENTANA_S = 0.5;

    override void OnContact(string zoneName, vector localPos, IEntity other, Contact data) {
        super.OnContact(zoneName, localPos, other, data);
        BZFrameRecorder.Contacto(this, zoneName, other);   // la columna `contacto` de la toma (cliente que graba)
        if (!GetGame().IsServer()) return;
        float ahoraC = GetGame().GetTickTime();
        float impC = data.Impulse;
        if (impC < 0) impC = -impC;
        if (m_BzContactoT >= 0 && ahoraC - m_BzContactoT <= BZ_CONTACTO_VENTANA_S && impC <= m_BzContactoImp) return;
        m_BzContactoT = ahoraC;
        m_BzContactoImp = impC;
        m_BzContactoZona = zoneName;
        Object objC = Object.Cast(other);
        if (objC == m_BzContactoObj) return;
        m_BzContactoObj = objC;
        m_BzContactoClase = "";
        if (objC) m_BzContactoClase = objC.GetType();
        if (objC && m_BzContactoClase == "") m_BzContactoClase = "modelo " + objC.GetModelName();   // objeto del mapa sin clase
    }

    override void OnInput(float dt) {
        super.OnInput(dt);
        if (!GetGame().IsServer()) return;
        // el cuerpo (jugador real al volante = el framework se hace a un lado; cerebro 1.2; freno del endpoint;
        // comando cacheado; la marcha deseada por encima del FIRST de eAI) vive en BZVehiculoGanchos.OnInput,
        // compartido con la moto
        BZVehiculoGanchos.OnInput(this, BZV(), dt);
    }

    override void EOnPostSimulate(IEntity other, float timeSlice) {
        super.EOnPostSimulate(other, timeSlice);

        // el grabador cuadro a cuadro (cliente que maneja, antes de buscar runner), el probe del server y el receptor
        // fisico del runner: BZVehiculoGanchos.PostSimulate, compartido con la moto. Devuelve el runner dueno.
        BZBusService srv = BZVehiculoGanchos.PostSimulate(this, BZV(), timeSlice);
        if (!srv) return;

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
        if (srv.EstaManejando12()) return;   // 12-mode: el gear lo comanda el applier via Sample(), la AT no opina

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
        // con el velocimetro en NaN (medido tras el spawn) no se mide la aceleracion ni se guarda la muestra: la primera
        // lectura finita que siguiera a un 0 forzado habria disparado el anti-catapulta
        if (BZMath.Finite(kmhNow)) {
            if (m_BZBus_LastSampleTime > 0) {
                float dt = tNow - m_BZBus_LastSampleTime;
                if (dt > 0.01) {
                    accelKmhPerSec = (kmhNow - m_BZBus_LastKmh) / dt;
                }
            }
            m_BZBus_LastKmh = kmhNow;
            m_BZBus_LastSampleTime = tNow;
        } else {
            kmhNow = 0;
        }

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
