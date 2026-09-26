// ============================================================================
//  BZVehicleDataReader.c - BZ_AutoDrive 1.2
//
//  SENSOR del sandwich (MODULES.md 0): corre UNA VEZ al spawn, del lado del RUNNER.
//  Es uno de los DOS unicos modulos autorizados a tocar el Car; el cerebro solo ve
//  el BZVehicleManual que este produce.
//
//  "Derivá lo que puedas, MEDÍ lo que no." Cada campo queda marcado con su
//  procedencia y lo MEDIDO manda sobre el prior derivado -- nunca al reves.
// ============================================================================

class BZVehicleDataReader
{
    // Priors de emergencia. Se marcan FALLBACK y se loguean FUERTE: un fallback
    // callado es un numero magico disfrazado.
    private const float FB_MASS        = 1400.0;
    private const float FB_WHEEL_R     = 0.30;
    private const float FB_STEER_MAX   = 0.61;   // ~35 grados
    // Distancia en la que el arrastre tiene que poder detenerse: es la MISMA tolerancia
    // de cierre de pose que usan los modulos POSE (BZDriveTol.LLEGADA_M).
    private const float CRAWL_STOP_M   = 0.12;
    private const float FB_RPM_IDLE    = 1250.0;
    private const float FB_RPM_RED     = 6250.0;
    private const float FB_FINAL_DRIVE = 3.667;
    private const float FB_DRAG        = 0.5;
    private const float FB_AREA        = 2.0;
    private const float FB_BRAKE_F     = 2000.0;
    private const float FB_BRAKE_R     = 1500.0;

    // Prior de la mordida del embrague, como fraccion sobre el ralenti. Es un PRIOR
    // declarado: cuando el envelope aprenda el hill-start pasa a MEASURED.
    private const float BITE_OVER_IDLE = 1.30;
    // Aceleracion objetivo para romper la inercia en el despegue (m/s2). De aca sale
    // el piso de throttle por FUERZA, no por tuneo.
    private const float LAUNCH_TARGET_A = 0.5;
    // ⚠⚠ EL PISO DEL CLAMP DEL DERIVADO. Si el crudo cae POR DEBAJO, el modelo no produjo
    // una respuesta dentro de su propio rango valido y NO se usa: manda el FALLBACK.
    //
    // Medido en el ZiS5: la rampa de arranque reportaba `rampa desde el piso medido 0.1` --
    // que es exactamente este numero, o sea el clamp, no una derivacion. Y el codigo justo
    // arriba ya habia puesto 0.35 de FALLBACK: el derivado lo pisaba con algo MAS BAJO. El
    // vehiculo quedaba PEOR que si no hubieramos derivado nada, y se trababa 53 ticks a
    // 0.05 km/h al despegar de parado (forward wp 9, y las tres reversas).
    //
    // ✔ El tope de arriba (0.90) es distinto y se deja: "necesita casi todo el gas" SI es
    // informativo. Lo que no informa nada es "necesita casi nada" saliendo de una cadena
    // --gearRatios x finalDrive x PeakTorque-- que el propio motor documenta como no
    // convergente (3.94 a 54.75 DENTRO de la misma marcha).
    // LAUNCH_DERIV_MIN (0,10) se fue con el derivado como decisor. El clamp existia porque el derivado daba
    // 0,03-0,09 y "parecia" bajo; medido en 7 clases con la rampa, el derivado esta a 0,6x-7,5x del piso real en
    // cualquier direccion: no habia nada que clampear, habia que dejar de usarlo. Sin calibrar: BZVehicleManual.LAUNCH_PRIOR_FLOTA.

    bool Read(string vehicleClass, Car bus, BZEnvelopeSource env, BZVehicleManual m)
    {
        if (!bus) return false;
        if (!m) return false;
        m.vehicleClass = vehicleClass;
        string sim = "CfgVehicles " + vehicleClass + " SimulationModule ";
        ReadMassAndRpm(bus, m);
        ReadGearing(sim, m);
        ReadBrakes(sim, m);
        ReadAero(sim, m);
        ReadDriveKind(sim, m);
        ReadGeometry(bus, env, m);
        ApplyMeasured(env, m);
        Derive(m);
        m.LogProvenance();
        return true;
    }

    // ======================= CONFIG / runtime =======================

    // Masa y rpm salen del RUNTIME, que es mas confiable que el config (v1.1 ya lo hacia asi).
    private void ReadMassAndRpm(Car bus, BZVehicleManual m)
    {
        m.massKg = dBodyGetMass(bus);
        m.SetProv(BZAsp.MASS, BZProv.BZPV_CONFIG);
        if (m.massKg < 100)
        {
            m.massKg = FB_MASS;
            m.SetProv(BZAsp.MASS, BZProv.BZPV_FALLBACK);
        }
        m.rpmIdle = bus.EngineGetRPMIdle();
        m.rpmRedline = bus.EngineGetRPMRedline();
        m.SetProv(BZAsp.RPM, BZProv.BZPV_CONFIG);
        if (m.rpmIdle < 100 || m.rpmRedline < 1000)
        {
            if (m.rpmIdle < 100) m.rpmIdle = FB_RPM_IDLE;
            if (m.rpmRedline < 1000) m.rpmRedline = FB_RPM_RED;
            m.SetProv(BZAsp.RPM, BZProv.BZPV_FALLBACK);
        }
    }

    private void ReadGearing(string sim, BZVehicleManual m)
    {
        array<float> gr = new array<float>();
        GetGame().ConfigGetFloatArray(sim + "Gearbox ratios", gr);
        m.gearRatios.Clear();
        int i;
        for (i = 0; i < gr.Count(); i++)
        {
            m.gearRatios.Insert(gr.Get(i));
        }
        array<float> tc = new array<float>();
        GetGame().ConfigGetFloatArray(sim + "Engine torqueCurve", tc);
        m.torqueCurve.Clear();
        for (i = 0; i < tc.Count(); i++)
        {
            m.torqueCurve.Insert(tc.Get(i));
        }
        LeerDireccionDelConfig(sim, m);
        m.reverseRatio = ReversaDelConfig(sim);
        // ⚠ EL DIFERENCIAL NO VIVE SIEMPRE EN EL MISMO EJE. El config lo declara en el
        // eje TRACCIONADO: un RWD lo pone en Axles->Rear y no tiene nada en Front. Leer
        // solo Front devolvia 0 en TODO vehiculo de traccion trasera, se caia al fallback
        // 3.667 teniendo 4.10 declarado, y la caja entera quedaba 10.6% corta en TODAS
        // las marchas -- con el gas, el selector de marcha, el creep y el piso de arranque
        // heredando el error.
        //
        // Lo delato la MEDICION, no la lectura del config: el cociente medido/modelo dio
        // 1.1187 / 1.1260 / 1.1229 en gear 2, 4 y 5. Tres marchas, UN factor. Un array de
        // relaciones mal leido daria un error distinto por marcha; un multiplicador comun
        // solo puede ser el diferencial. Y 4.10/3.667 = 1.1181.
        float fdFront = GetGame().ConfigGetFloat(sim + "Axles Front Differential ratio");
        float fdRear = GetGame().ConfigGetFloat(sim + "Axles Rear Differential ratio");
        m.finalDrive = fdRear;
        if (m.finalDrive <= 0.1) m.finalDrive = fdFront;
        float difEjes = fdFront - fdRear;
        if (difEjes < 0) difEjes = -difEjes;
        if (fdFront > 0.1 && fdRear > 0.1 && difEjes > 0.01)
        {
            // Un vehiculo con relacion distinta por eje no es algo que este modelo sepa
            // representar. Se toma el trasero y se dice en voz alta, en vez de elegir en
            // silencio.
            Print("[DataReader] " + sim + " declara diferenciales DISTINTOS por eje (front " + fdFront.ToString() + " rear " + fdRear.ToString() + "): se usa el trasero. El modelo de caja supone uno solo.");
        }
        m.centDiffRatio = GetGame().ConfigGetFloat(sim + "CentralDifferential ratio");
        m.SetProv(BZAsp.GEARING, BZProv.BZPV_CONFIG);
        if (m.centDiffRatio <= 0) m.centDiffRatio = 1.0;
        // ⚠⚠⚠ ACA ESTABA:  `if (reverseRatio <= 0) reverseRatio = gearRatios.Get(0);`
        //
        // O sea: REVERSA := PRIMERA, en silencio. Es la TERCERA aparicion de esa misma
        // suposicion en un dia --la mordida, la rampa final y este-- y la unica que no se
        // veia desde ningun lado.
        //
        // ⚠ Y EL COMENTARIO QUE LA ACOMPANABA DECIA "y por eso baja la procedencia de toda
        // la caja". NO LA BAJABA: `SetProv(GEARING, CONFIG)` esta cuatro lineas mas arriba y
        // este respaldo no tocaba nada. El manual del hatchback imprimia `gearing=MEASURED`
        // con la reversa inventada adentro. Un comentario que promete una salvaguarda
        // inexistente es peor que no tener comentario: se lee y se confia.
        //
        // Medido: `config 11.6134` para gear 0. 11.6134 = 3.167 x 3.667 =
        // PRIMERA x final, exacto. De ahi salia una mordida de 15.8 en reversa y el
        // vehiculo persiguiendola hasta 17 km/h.
        //
        // ✖ Ahora, si no se declara, queda en 0 y se dice en voz alta con el precio.
        if (m.finalDrive <= 0.1 || m.gearRatios.Count() < 1)
        {
            if (m.finalDrive <= 0.1) m.finalDrive = FB_FINAL_DRIVE;
            m.SetProv(BZAsp.GEARING, BZProv.BZPV_FALLBACK);
            // ⚠ ESTE FALLBACK SALIO EN 12 CORRIDAS SEGUIDAS Y SE LEYO COMO RUIDO. La linea
            // decia 'gearing=FALLBACK' sin decir que costaba, y lo que costaba era la caja
            // entera. Se dice el precio, no el estado.
            Print("[DataReader] *** CAJA EN FALLBACK: no se pudo leer el diferencial de " + sim + " (front ni rear). Se inventa final=" + FB_FINAL_DRIVE.ToString() + ". CON ESTO QUEDAN MAL: seleccion de marcha, rpm por velocidad, velocidad de mordida, creep derivado, piso de arranque y topSpeed; y el modelo de par se apaga. NO es cosmetico. ***");
        }
    }

    private void ReadBrakes(string sim, BZVehicleManual m)
    {
        m.maxBrakeTorqueFront = GetGame().ConfigGetFloat(sim + "Axles Front maxBrakeTorque");
        m.maxBrakeTorqueRear  = GetGame().ConfigGetFloat(sim + "Axles Rear maxBrakeTorque");
        m.SetProv(BZAsp.BRAKE, BZProv.BZPV_CONFIG);
        if (m.maxBrakeTorqueFront <= 0 || m.maxBrakeTorqueRear <= 0)
        {
            if (m.maxBrakeTorqueFront <= 0) m.maxBrakeTorqueFront = FB_BRAKE_F;
            if (m.maxBrakeTorqueRear  <= 0) m.maxBrakeTorqueRear = FB_BRAKE_R;
            m.SetProv(BZAsp.BRAKE, BZProv.BZPV_FALLBACK);
        }
    }

    private void ReadAero(string sim, BZVehicleManual m)
    {
        m.dragCoef = GetGame().ConfigGetFloat(sim + "Aerodynamics dragCoefficient");
        m.frontalArea = GetGame().ConfigGetFloat(sim + "Aerodynamics frontalArea");
        m.SetProv(BZAsp.AERO, BZProv.BZPV_CONFIG);
        if (m.dragCoef <= 0 || m.frontalArea <= 0)
        {
            // El T6 declara dragCoefficient 0: es un config incompleto, no un dato.
            if (m.dragCoef <= 0) m.dragCoef = FB_DRAG;
            if (m.frontalArea <= 0) m.frontalArea = FB_AREA;
            m.SetProv(BZAsp.AERO, BZProv.BZPV_FALLBACK);
        }
    }

    private void ReadDriveKind(string sim, BZVehicleManual m)
    {
        string drive = GetGame().ConfigGetTextOut(sim + "drive");
        m.driveKind = 0;
        if (drive == "DRIVE_AWD") m.driveKind = 2;
        if (drive == "DRIVE_RWD") m.driveKind = 1;
    }

    // GEOMETRIA: lo que la API del Car NO expone se MIDE (envelope). Sin medicion queda
    // en FALLBACK y el log lo grita -- no se disfraza de dato.
    private void ReadGeometry(Car bus, BZEnvelopeSource env, BZVehicleManual m)
    {
        m.wheelbaseM = MeasureWheelbase(bus);
        m.SetProv(BZAsp.WHEELBASE, BZProv.BZPV_MEASURED);
        if (m.wheelbaseM < 0.5 && env) m.wheelbaseM = env.WheelbaseM();
        if (m.wheelbaseM < 0.5)
        {
            m.wheelbaseM = 0;
            m.SetProv(BZAsp.WHEELBASE, BZProv.BZPV_FALLBACK);
        }
        m.wheelRadiusM = 0;
        if (env) m.wheelRadiusM = env.WheelRadiusM();
        m.SetProv(BZAsp.WHEELRADIUS, BZProv.BZPV_MEASURED);
        if (m.wheelRadiusM <= 0.05)
        {
            m.wheelRadiusM = FB_WHEEL_R;
            m.SetProv(BZAsp.WHEELRADIUS, BZProv.BZPV_FALLBACK);
        }
        // ⚠⚠ TRES ESCALONES, Y EL ORDEN NO ES EL OBVIO.
        //
        // Se propuso poner el CONFIG primero, por encima del envelope. Va al reves, y la
        // razon es la misma que sostiene todo el resto del framework: el envelope MIDE lo
        // que el vehiculo hace; el config DECLARA lo que su autor escribio. Medido en el
        // ExpansionBus: la escalera dio 30.5 grados de tope real. Si su config declarara 35
        // y le ganara a la medicion, `MaxCurvature` se inflaria 18% y el clamp volveria a
        // aflojarse -- que es exactamente la saturacion que costo cuatro tandas arreglar.
        //
        // El config no viene a reemplazar la medicion: viene a reemplazar el FALLBACK, que
        // hoy es 0.61 rad (35 grados) para todos -- el numero del sedan puesto a un camion.
        // Esa es la mejora, y es grande: una clase nueva cuya toma no cruzo el volante
        // dejaba de heredar el tope de un sedan.
        //
        //   1. envelope MEDIDO   (el balde 9 de una toma que doblo fuerte, o la escalera)
        //   2. config DECLARADO  (esto)
        //   3. fallback 35 grados, con aviso
        //
        // ⚠ Y SE LOGUEA EL CRUDO AUNQUE NO SIRVA. Si la ruta del config no existe en este
        // motor, el valor sale 0 y hay que poder VERLO en el log para saber que la clave es
        // otra -- si no, el escalon 2 se saltea en silencio y parece que nunca estuvo.
        // ⚠ ReadGeometry recibe el `Car`, no el nombre de clase: el linter lo cazo antes de
        // que el motor tumbara el modulo World entero por "Can't find variable".
        string claseVeh = "";
        if (bus) claseVeh = bus.GetType();
        m.steerMaxRad = 0;
        if (env) m.steerMaxRad = env.SteerMaxRad();
        m.SetProv(BZAsp.STEERMAX, BZProv.BZPV_MEASURED);
        if (m.steerMaxRad <= 0.05 && claseVeh != "")
        {
            float smCfg = SteerMaxDelConfig(claseVeh);
            if (smCfg > 0.05)
            {
                m.steerMaxRad = smCfg;
                m.SetProv(BZAsp.STEERMAX, BZProv.BZPV_CONFIG);
            }
        }
        if (m.steerMaxRad <= 0.05)
        {
            m.steerMaxRad = FB_STEER_MAX;
            m.SetProv(BZAsp.STEERMAX, BZProv.BZPV_FALLBACK);
            Print("[Manual] steerMax en FALLBACK (35 deg, el numero de un sedan): ni el envelope ni el config lo dieron para " + claseVeh);
        }
    }

    // Wheelbase de los CONTACTOS de rueda proyectados sobre el eje longitudinal.
    // Mismo metodo que v1.1 / PathLogService: es medicion, no config.
    private float MeasureWheelbase(Car car)
    {
        vector fwd = car.GetDirection();
        vector vpos = car.GetPosition();
        int n = car.WheelCount();
        float minP = 99999.0;
        float maxP = -99999.0;
        int good = 0;
        int i;
        vector wp;
        float proj;
        for (i = 0; i < n; i++)
        {
            wp = car.WheelGetContactPosition(i);
            if (wp[0] == 0 && wp[1] == 0 && wp[2] == 0) continue;
            good++;
            proj = (wp[0] - vpos[0]) * fwd[0] + (wp[2] - vpos[2]) * fwd[2];
            if (proj < minP) minP = proj;
            if (proj > maxP) maxP = proj;
        }
        if (good < 2) return 0;
        float wb = maxP - minP;
        if (wb < 0) wb = -wb;
        return wb;
    }

    // ======================= MEDIDO =======================

    // Vuelca el envelope al Manual. Lo medido MANDA: se copia crudo, sin promediar
    // con el prior y sin "corregirlo" para acercarlo al modelo (regla 1).
    // Publica a proposito: el BANCO de medicion corre DESPUES de que el Manual quedo
    // construido, asi que lo que mide no llegaria al cerebro hasta la corrida siguiente.
    // Volver a aplicarlo (y volver a derivar) hace que la misma corrida lo aproveche.
    void ApplyMeasured(BZEnvelopeSource env, BZVehicleManual m)
    {
        if (!env) return;
        // Ratios de caja MEDIDOS: desplazan al config, que en algunos vehiculos ni resuelve.
        int g;
        float r;
        bool algunRatio = false;
        for (g = 0; g < 8; g++)
        {
            r = env.RatioForGear(g);
            if (r > 0.0001)
            {
                m.SetMeasuredRatio(g, r);
                algunRatio = true;
            }
        }
        if (algunRatio) m.SetProv(BZAsp.GEARING, BZProv.BZPV_MEASURED);
        // Aceleracion maxima MEDIDA: no depende de torque x ratio / radio / masa.
        float aMax = env.MaxAccelMs2();
        if (aMax > 0.1)
        {
            m.maxAccelMs2 = aMax;
            m.SetProv(BZAsp.LAUNCH, BZProv.BZPV_MEASURED);
        }
        // ★ CREEP Y DESPEGUE MEDIDOS DIRECTO. Entran DESPUES de DeriveSpeeds/DeriveLaunch
        // y los pisan, porque medir la magnitud que se usa le gana a derivarla de una
        // cadena cuyo primer eslabon (el ratio por marcha) no converge.
        //
        // ⚠ ESTO ES LO QUE ESTABA ROMPIENDO LAS MANIOBRAS, y la historia vale anotarla:
        // con el ratio SUCIO (25.99, promedio de basura) las maniobras cerraban a 0.64 m;
        // con el gate estricto correcto que lo descarta, creep/launch caian al prior y
        // cerraban a 1.97 m. El dato sucio estaba MAS CERCA de la verdad que el prior
        // honesto. La salida no es aflojar el gate -- es no necesitar ese dato.
        // ⚠ TAMBIEN ACA, no solo en ReadGeometry. steerMax se leia UNA vez al construir el
        // Manual, o sea ANTES de que el banco midiera nada -- por eso la corrida del PBO
        // 19:00 lo reporto en FALLBACK aunque despues juntara muestras: no habia con que
        // aplicarlo. Ahora entra por el mismo camino que creep y despegue.
        float sm = env.SteerMaxRad();
        if (sm > 0.05)
        {
            m.steerMaxRad = sm;
            m.SetProv(BZAsp.STEERMAX, BZProv.BZPV_MEASURED);
        }
        float cr = env.CreepKmh();
        if (cr > 0.1)
        {
            m.idleCreepKmh = cr;
            m.idleCreepIsZero = false;
            m.creepKmh = cr;                    // repta: medicion y objetivo coinciden
            m.SetProv(BZAsp.CREEP, BZProv.BZPV_MEASURED);
            m.creepObjetivoDecidido = true;
            // la linea dice las DOS cuentas, como el heartbeat con el AR: quien gano y por que.
            string rp = "[Manual] " + m.vehicleClass + " REPTA (medido): creep " + cr.ToString() + " kmh";
            rp = rp + " · fuente " + env.CreepSource();
            rp = rp + " · chist2 " + env.CreepSamples().ToString() + " vs nocreep " + env.NoCreepSamples().ToString();
            Print(rp);
        }
        else if (env.CreepIsZeroMeasured())
        {
            // ⚠ MEDIDO EN CERO, que NO es lo mismo que sin medir. Se registra el dato,
            // pero el OBJETIVO de arrastre se deja derivado: pedirle a las maniobras que
            // se acerquen a la pose a 0 km/h seria pedirles que no se muevan. Lo que mueve
            // a un no-reptante es el piso de despegue, que si esta medido.
            m.idleCreepKmh = 0;
            m.idleCreepIsZero = true;
            // ★★ Y EL OBJETIVO DE ARRASTRE SE DERIVA DE LO MEDIDO, no del prior de caja.
            //
            // Para un vehiculo que no repta, creep medido SIEMPRE va a ser 0 -- asi que
            // caer al prior no es una excepcion, es el caso normal. Y el prior de caja da
            // 8.13 km/h, ~2x el 4.32 con el que las maniobras cerraban a 9.7 cm:
            // acercarse al doble de velocidad deja la mitad de tiempo para frenar, y eso
            // pega en el error LONGITUDINAL (medido: along de 0.12 m a 0.78 m) sin tocar
            // el lateral. Exactamente el patron que se vio.
            //
            // La derivacion honesta: el arrastre es la velocidad desde la cual el vehiculo
            // puede DETENERSE dentro de la tolerancia de pose, con el freno que se le
            // MIDIO. v = sqrt(2 * aFreno * tolerancia). No es una constante elegida a ojo:
            // sale de una magnitud medida y de la tolerancia que el framework ya declara.
            float aFreno = m.MaxBrakeDecel(BZSurf.HARD);
            if (aFreno > 0.5)
            {
                m.creepKmh = 3.6 * Math.Sqrt(2.0 * aFreno * CRAWL_STOP_M);
                m.SetDerivedFrom(BZAsp.CREEP, BZAsp.BRAKE, -1, -1);
                m.creepObjetivoDecidido = true;
                string nr = "[Manual] " + m.vehicleClass + " NO REPTA (medido). Arrastre DERIVADO del freno medido: " + m.creepKmh.ToString() + " kmh (frena en " + CRAWL_STOP_M.ToString() + " m).";
                nr = nr + " · fuente " + env.CreepSource();
                if (env.CoastAtRestSamples() > 0) nr = nr + " (coast a v->0 " + env.CoastAtRest().ToString() + " m/s2, " + env.CoastAtRestSamples().ToString() + " muestras)";
                nr = nr + " · nocreep " + env.NoCreepSamples().ToString() + " vs chist2 " + env.CreepSamples().ToString();
                Print(nr);
            }
            else
            {
                string nr2 = "[Manual] " + m.vehicleClass + " NO REPTA (medido) y sin freno medido: el arrastre queda en el prior.";
                nr2 = nr2 + " · fuente " + env.CreepSource();
                if (env.CoastAtRestSamples() > 0) nr2 = nr2 + " (coast a v->0 " + env.CoastAtRest().ToString() + " m/s2, " + env.CoastAtRestSamples().ToString() + " muestras)";
                nr2 = nr2 + " · nocreep " + env.NoCreepSamples().ToString() + " vs chist2 " + env.CreepSamples().ToString();
                Print(nr2);
            }
        }
        float lf = env.LaunchFloor();
        if (lf > 0.01)
        {
            m.launchThrottleFloor = lf;
            m.launchFloorMedido = true;              // el piso, no aMax
            m.launchCalN = env.LaunchCalSamples();
            m.SetProv(BZAsp.LAUNCH, BZProv.BZPV_MEASURED);
        }
        int b;
        for (b = 0; b < BZVehicleManual.NBINS; b++)
        {
            if (env.HasK(b)) m.SetMeasuredK(b, env.KAt(b));
            if (b < 10 && env.HasSteerTransfer(b)) m.SetMeasuredSteerTransfer(b, env.SteerTransferAt(b));
            if (env.HasCoast(BZSurf.HARD, b)) m.SetMeasuredCoast(BZSurf.HARD, b, env.CoastAt(BZSurf.HARD, b));
            if (env.HasCoast(BZSurf.SOFT, b)) m.SetMeasuredCoast(BZSurf.SOFT, b, env.CoastAt(BZSurf.SOFT, b));
        }
    }

    // ======================= DERIVADO (priors) =======================

    // TODO derivado hereda el MINIMO de sus inputs (SetDerivedFrom). Si el radio de rueda
    // quedo en FALLBACK, creep / topSpeed / launch / freno salen FALLBACK tambien: el grito
    // se PROPAGA en vez de apagarse en la primera formula.
    // Publica junto con ApplyMeasured: el banco necesita re-derivar despues de medir.
    void Derive(BZVehicleManual m)
    {
        DeriveSpeeds(m);
        DeriveLaunch(m);
        DeriveSteerPose(m);
        DeriveBrakeDecel(m);
        DeriveCoastProv(m);
    }

    // creepKmh = lo que el vehiculo hace SOLO, en ralenti y en 1ra. Deja de ser una
    // constante tuneada (ManeuverCreepKmh) y pasa a ser un HECHO del vehiculo.
    private void DeriveSpeeds(BZVehicleManual m)
    {
        m.topSpeedKmh = 0;
        // ⚠ NO SE PISA LO MEDIDO. Esto ponia creepKmh = 0 incondicionalmente y despues lo
        // derivaba, asi que borraba lo que ApplyMeasured acababa de dejar: el creep medido
        // entraba y salia en la misma llamada. El mismo bug tenia el piso de despegue, y
        // por eso el banco medio 0.27 y el reporte seguia diciendo FALLBACK.
        // ⚠ LA COMPUERTA PREGUNTABA POR LA PROCEDENCIA EQUIVOCADA.
        // `IsMeasured(CREEP)` era la pregunta correcta cuando habia SOLO DOS posibilidades:
        // medido, o el prior de caja. El no-reptante trajo una tercera -- objetivo DERIVADO
        // del freno medido -- y una compuerta binaria la mete en la bolsa de "no medido":
        // dos lineas mas abajo se borraba (`creepKmh = 0`) y se pisaba con el prior.
        // Medido: 4.5408 se calculaba, se imprimia, y el driver leia 8.30 FALLBACK.
        // La pregunta correcta no es "de donde vino" sino "YA ESTA DECIDIDO".
        bool creepMedido = m.creepObjetivoDecidido;
        if (!creepMedido) m.creepKmh = 0;
        int n = m.gearRatios.Count();
        if (n < 1) return;
        m.topSpeedKmh = m.TopSpeedForGear(n - 1);
        // El creep sale del ratio de PRIMERA. Si esta MEDIDO se usa ese: es el ratio real
        // de la caja de ESTE vehiculo, no el que el config declara.
        // ⚠⚠⚠ ESTE PRIOR ES UNA FICCION CINEMATICA, Y NO SE PUEDE REEMPLAZAR POR DERIVACION.
        // Calcula a que velocidad GIRARIA la rueda con el motor a ralenti en 1ra, sin
        // preguntarse si el motor puede MOVER el vehiculo a esa velocidad. De aca sale el
        // creep 10,28 km/h del jesko, que lo manda por la rama equivocada del slam
        // (LlegoLaHoraDelSlam, 2a rama) y le dispersa el endpoint.
        //
        // ✖ SE INTENTO REEMPLAZARLO POR UN BALANCE DE FUERZAS Y LA FORMULA MURIO MEDIDA
        // `a_disp = TorqueAt(rpmIdle)*ratio1ra/(rWheel*masa)` contra
        // `a_res = CoastDecel(v->0)`. Los 7 vehiculos que resuelven dieron REPTARIA,
        // incluido el AEC_Matador que esta MEDIDO como no-reptante (`nocreep,64`):
        //     vehiculo        a_disp   a_res   par@idle   razon
        //     CivilianSedan    3.281   0.397    125 Nm      8x
        //     AEC_Matador      2.494   0.397     48 Nm      6x   <- medido NO REPTA
        //     jesko           12.754   0.147    487 Nm     87x   <- 1,3 g a ralenti
        // Cero discriminacion: la formula esta saturada de un lado.
        //
        // LA CAUSA: `TorqueAt` CLAMPEA por debajo del primer punto de la curva
        // (`if (rpm <= torqueCurve[0]) return torqueCurve[1]`) y TODOS los ralenties caen
        // ahi (Matador 650, Sedan 900, jesko 1000). O sea que `par@idle` no es el par a
        // ralenti: es el primer punto de la curva, y lo infla entre 6x y 87x.
        //
        // ⛔ Y NO TIENE ARREGLO POR ESTE CAMINO, POR CIRCULARIDAD: el termino que falta --el
        // par REAL a ralenti-- no esta en el config de forma utilizable, y la unica manera
        // de conocerlo es OBSERVAR SI EL VEHICULO REPTEA. Que es justo lo que se queria
        // derivar. Extrapolar la curva hacia abajo seria inventar el dato en otro lugar.
        //
        // ✅ EL CAMINO QUE SI FUNCIONA ES LA GRABACION: parar en LLANO, soltar todo y
        // ESPERAR EL RALENTI (2-3 s, motor ENCENDIDO) antes de cortar la toma. Eso da
        // `chist2` (repta) o `nocreep` (no repta), las dos mediciones directas. Para rutas
        // que terminan en pendiente --donde el creep se rechaza por diseno-- esta el modo
        // multi-toma de `envelope_emit.py`.
        if (!creepMedido)
        {
            float ratio1 = m.RatioForGear(2);
            if (ratio1 > 0.0001) m.creepKmh = m.SpeedAtRatio(m.rpmIdle, ratio1);
            else                 m.creepKmh = m.SpeedAtRpm(m.rpmIdle, m.gearRatios.Get(0));
            // Un fallback SILENCIOSO es lo que nos costo dos semanas con los stubs. Se
            // dice, y se dice QUE HACER -- no solo que falta.
            // ⚠ Enforce NO admite el '+' al inicio de linea (corta el statement en el
            // salto): se arma por pasos, como el resto del arbol. El linter lo cazo.
            string av = "[Manual] " + m.vehicleClass + " SIN creep medido: se usa el prior de caja (";
            av = av + m.creepKmh.ToString() + " kmh).";
            av = av + " Si el endpoint de este vehiculo dispersa, es esto.";
            av = av + " Para medirlo: al grabar, para en LLANO, solta gas/freno/mano y ESPERA";
            av = av + " EL RALENTI 2-3 s con el motor ENCENDIDO antes de cortar la toma.";
            Print(av);
        }
        // SpeedAtRpm usa el RADIO DE RUEDA -> creep y topSpeed no pueden ser mejores que el.
        m.SetDerivedFrom(BZAsp.TOPSPEED, BZAsp.RPM, BZAsp.GEARING, BZAsp.WHEELRADIUS);
        if (!creepMedido) m.SetDerivedFrom(BZAsp.CREEP, BZAsp.RPM, BZAsp.GEARING, BZAsp.WHEELRADIUS);
        m.clutchBiteRpm = m.rpmIdle * BITE_OVER_IDLE;
        m.SetDerivedFrom(BZAsp.CLUTCHBITE, BZAsp.RPM, -1, -1);
    }

    // Piso de throttle del despegue POR FUERZA: la que hace falta para acelerar la masa
    // a LAUNCH_TARGET_A, contra la fuerza que el motor puede dar en 1ra. Un camion pesado
    // pide mas que un Sedan y sale solo, sin tunear por vehiculo.
    private void DeriveLaunch(BZVehicleManual m)
    {
        // ⚠ Idem creep: esto reseteaba el piso a 0.35 y la procedencia a FALLBACK aunque
        // el envelope acabara de traer una medicion buena. El banco midio 0.27 con 12
        // muestras y el reporte decia FALLBACK -- no fallaba la medicion, la borraba esta
        // linea. El guard de maxAccelMs2 ya existia; faltaban estos dos.
        // la pregunta es por el PISO, no por el aspecto LAUNCH (que aMax tambien marca MEASURED).
        // Con aMax medido por el banco y el piso sin calibrar, esto decia "MEDIDO 0.35" y no derivaba.
        bool pisoMedido = m.launchFloorMedido && m.launchThrottleFloor > 0.01;
        if (!pisoMedido)
        {
            // sin calibrar, el prior de flota (mediana de lo calibrado), no un numero elegido ni el derivado.
            m.launchThrottleFloor = BZVehicleManual.LAUNCH_PRIOR_FLOTA;
            if (!m.IsMeasured(BZAsp.LAUNCH)) m.SetProv(BZAsp.LAUNCH, BZProv.BZPV_FALLBACK);   // no pisa el MEASURED de aMax
        }
        if (m.gearRatios.Count() < 1) return;
        if (m.wheelRadiusM <= 0.05) return;
        float tq = PeakTorque(m);
        if (tq <= 1.0) return;
        float ratio = m.gearRatios.Get(0) * m.finalDrive * m.centDiffRatio;
        float fMax = tq * ratio / m.wheelRadiusM;
        if (fMax <= 1.0) return;
        // Si ya vino MEDIDA del envelope, no se pisa con la derivada (lo medido manda).
        if (m.massKg > 1.0 && !m.IsMeasured(BZAsp.LAUNCH)) m.maxAccelMs2 = fMax / m.massKg;
        float need = m.massKg * LAUNCH_TARGET_A;
        float crudo = need / fMax;
        // ★ 922: el derivado se imprime SIEMPRE con la clase pero YA NO DECIDE: medido en 7 clases
        // esta a 0,6x-7,5x del piso real. Queda como dato para seguir cruzandolo, nada mas.
        string dl = "[Manual] " + m.vehicleClass + " launch: derivado crudo " + crudo.ToString() + " (informativo, no decide)";
        if (pisoMedido) dl = dl + " · MEDIDO " + m.launchThrottleFloor.ToString() + " (" + m.launchCalN.ToString() + " cal, manda el medido)";
        else dl = dl + " · sin calibrar: prior de flota " + BZVehicleManual.LAUNCH_PRIOR_FLOTA.ToString() + " (mediana de 7 clases calibradas, 22/09)";
        Print(dl);
    }

    // La curva viene como pares (rpm, Nm) interleaved.
    private float PeakTorque(BZVehicleManual m)
    {
        int n = m.torqueCurve.Count();
        float best = 0;
        int i;
        float v;
        for (i = 1; i < n; i = i + 2)
        {
            v = m.torqueCurve.Get(i);
            if (v > best) best = v;
        }
        return best;
    }

    // Ganancia de volante de POSE: cuanto normalizado hay que pedir por radian de error
    // de rumbo. Sale del tope de direccion, no de un numero tuneado: pedir el error
    // COMPLETO es 1/steerMax en unidades normalizadas.
    private void DeriveSteerPose(BZVehicleManual m)
    {
        m.steerKPose = 1.0;
        m.SetProv(BZAsp.STEERPOSE, BZProv.BZPV_FALLBACK);
        if (m.steerMaxRad <= 0.05) return;
        m.steerKPose = 1.0 / m.steerMaxRad;
        m.SetDerivedFrom(BZAsp.STEERPOSE, BZAsp.STEERMAX, -1, -1);
    }

    // MaxBrakeDecel() es una FUNCION del Manual, pero su calidad depende de par de freno,
    // radio y masa. Se le rastrea procedencia igual: el slam frena por este numero, y si
    // el radio es de emergencia la distancia de frenado tambien lo es.
    private void DeriveBrakeDecel(BZVehicleManual m)
    {
        m.SetDerivedFrom(BZAsp.BRAKEDECEL, BZAsp.BRAKE, BZAsp.WHEELRADIUS, BZAsp.MASS);
    }

    // Sin medicion, el planeo cae al prior de rodadura+aero -> hereda de AERO y MASA.
    // Con medicion, ApplyMeasured ya lo dejo en MEASURED y no se pisa.
    private void DeriveCoastProv(BZVehicleManual m)
    {
        if (m.IsMeasured(BZAsp.COAST)) return;
        m.SetDerivedFrom(BZAsp.COAST, BZAsp.AERO, BZAsp.MASS, -1);
    }
    // El angulo maximo de rueda declarado en el config del vehiculo, en RADIANES.
    //
    // Ruta candidata, siguiendo la misma estructura que ya se lee mas arriba
    // (`Axles Front Differential ratio`, `Axles Front maxBrakeTorque`):
    //     CfgVehicles <clase> SimulationModule Axles Front Steering maxSteeringAngle
    // El config lo declara en GRADOS.
    //
    // Se prueban dos variantes porque distintos mods acomodan la clase `Steering` en
    // distinto nivel. Se devuelve la primera con un valor plausible, y se IMPRIME lo crudo
    // encontrado en cada una: si las dos dan 0, el log dice donde NO esta, que es la mitad
    // del trabajo de encontrar donde SI.
    // La relacion de reversa, probando los nombres bajo los que puede estar declarada y
    // diciendo lo que se leyo en cada uno. Mismo patron que SteerMaxDelConfig: cuando no
    // sabemos donde vive el dato, se prueban las rutas y se IMPRIME el crudo, en vez de
    // devolver 0 y dejar que alguien lo reemplace por un prior.
    private static float ReversaDelConfig(string sim)
    {
        float a = GetGame().ConfigGetFloat(sim + "Gearbox reverse");
        float b = GetGame().ConfigGetFloat(sim + "Gearbox reverseRatio");
        float c = GetGame().ConfigGetFloat(sim + "Gearbox ratioReverse");
        string ln = "[DataReader] reversa del config: Gearbox.reverse=" + a.ToString();
        ln = ln + " · Gearbox.reverseRatio=" + b.ToString();
        ln = ln + " · Gearbox.ratioReverse=" + c.ToString();
        Print(ln);
        float r = a;
        if (r < 0) r = -r;
        if (r > 0.0001) return r;
        r = b;
        if (r < 0) r = -r;
        if (r > 0.0001) return r;
        r = c;
        if (r < 0) r = -r;
        if (r > 0.0001) return r;
        Print("[DataReader] *** SIN RELACION DE REVERSA en " + sim + ". No se supone la 1ra. CON ESTO QUEDA SIN DATO: la velocidad de mordida en reversa, y con ella la rampa final de reversa y la defensa contra el bog marcha atras. El vehiculo va a seguir lo GRABADO y a apoyarse en su piso mecanico. Se arregla midiendo la reversa en calibracion. ***");
        return 0;
    }

    // Las curvas de velocidad del volante y el downforce. Mismo patron que SteerMaxDelConfig:
    // se prueban las dos ubicaciones que usan los mods y se IMPRIME lo crudo de cada una, asi
    // cuando no aparece el log dice donde NO esta.
    private static void LeerDireccionDelConfig(string sim, BZVehicleManual m)
    {
        CopiarCurva(sim + "Axles Front Steering decreaseSpeed", sim + "Steering decreaseSpeed", m.steerDecCurve);
        CopiarCurva(sim + "Axles Front Steering increaseSpeed", sim + "Steering increaseSpeed", m.steerIncCurve);
        float df = GetGame().ConfigGetFloat(sim + "Aerodynamics downforceCoefficient");
        m.downforceCoef = df;
        string ln = "[Manual] direccion del config: decreaseSpeed=" + m.steerDecCurve.Count().ToString();
        ln = ln + " valores · increaseSpeed=" + m.steerIncCurve.Count().ToString();
        ln = ln + " valores · downforce=" + df.ToString();
        Print(ln);
        if (m.steerDecCurve.Count() >= 2)
        {
            string l2 = "[Manual] ritmo del contravolante: 0kmh=" + (m.RitmoContravolante(0.0) * Math.RAD2DEG).ToString();
            l2 = l2 + " · 60kmh=" + (m.RitmoContravolante(60.0) * Math.RAD2DEG).ToString();
            l2 = l2 + " · 100kmh=" + (m.RitmoContravolante(100.0) * Math.RAD2DEG).ToString();
            l2 = l2 + " deg/s";
            Print(l2);
        }
    }

    private static void CopiarCurva(string ruta1, string ruta2, array<float> destino)
    {
        destino.Clear();
        array<float> tmp = new array<float>();
        GetGame().ConfigGetFloatArray(ruta1, tmp);
        if (tmp.Count() < 2)
        {
            tmp.Clear();
            GetGame().ConfigGetFloatArray(ruta2, tmp);
        }
        int i;
        for (i = 0; i < tmp.Count(); i++)
        {
            destino.Insert(tmp.Get(i));
        }
    }

    private static float SteerMaxDelConfig(string vehicleClass)
    {
        string sim = "CfgVehicles " + vehicleClass + " SimulationModule ";
        float a = GetGame().ConfigGetFloat(sim + "Axles Front Steering maxSteeringAngle");
        float b = GetGame().ConfigGetFloat(sim + "Steering maxSteeringAngle");
        string ln = "[Manual] steerMax del config: Axles.Front.Steering=" + a.ToString();
        ln = ln + " deg · Steering=" + b.ToString() + " deg";
        Print(ln);
        float deg = a;
        if (deg <= 1.0) deg = b;
        if (deg <= 1.0 || deg > 60.0) return 0;
        return deg * Math.DEG2RAD;
    }

}
