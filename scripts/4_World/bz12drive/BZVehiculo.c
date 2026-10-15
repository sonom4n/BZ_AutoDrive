// ============================================================================
//  BZVehiculo - el vehiculo que maneja el framework, visto sin saber si es un auto o una moto.
//
//  El cerebro y el servicio le hablan a esto; lo que cambia por tipo de vehiculo vive en las subclases
//  (BZVehiculoAuto; BZVehiculoMoto con la moto de la 1.30). Lo crea BZVehiculoFabrica.Para(), que es la unica
//  que decide el tipo, POR JERARQUIA (CarScript -> auto; MotorbikeScript -> moto; nada por nombre de clase).
//
//  La base no hace nada: devuelve 0/false y no toca el vehiculo. Una operacion que un tipo no tiene (la pata en un
//  auto, las luces del framework en un vehiculo sin la cascara) queda en la base, sin efecto. Lo que se lee igual en
//  los dos (el roll, el cambio que pide el conductor, lo del config) vive aca, sobre el Transport.
//
//  Auto() es de transicion: devuelve el Car para el codigo que todavia recibe Car (el cerebro 1.2, los probes). En
//  una moto devuelve null.
// ============================================================================
class BZVehiculo {

    protected Transport m_T;

    Transport T() { return m_T; }
    Car Auto() { return null; }
    bool EsAuto() { return false; }
    bool EsMoto() { return false; }
    string Tipo() { return ""; }            // "auto" / "moto": lo escribe el grabador en el header
    bool TieneReversa() { return false; }

    // ---- lectura
    float KmhAbs() { return 0; }
    int Marcha() { return 0; }
    int MarchaNeutral() { return 0; }      // auto: CarGear.NEUTRAL (1, la 0 es la reversa); moto: GetNeutralGear() (0)
    int CuentaMarchas() { return 0; }
    float Rpm() { return 0; }
    float RpmRalenti() { return 0; }
    float RpmMax() { return 0; }
    float RpmCorte() { return 0; }
    bool MotorPrendido() { return false; }
    int Ruedas() { return 0; }
    int RuedasPresentes() { return 0; }
    bool RuedaContacto(int i) { return false; }
    float ContactoFrenteT() { return -1; }   // moto: GetTickTime del ultimo contacto delantero (la cascara); -1 = ninguno
    string ContactoFrenteClase() { return ""; }   // moto: la clase de lo que toco adelante ("terreno" si no hay objeto)
    string ContactoUltimo(float ventanaS) { return ""; }
    float ContactoObjetoT() { return -1; }   // moto: GetTickTime del ultimo contacto con un OBJETO (no el terreno); -1 = ninguno   // moto: el ultimo contacto de cualquier zona dentro de la ventana, en texto ("" = ninguno)
    vector RuedaDireccion(int i) { return vector.Zero; }
    vector RuedaContactoPos(int i) { return vector.Zero; }
    bool Caida() { return false; }
    float Pata() { return 0; }            // moto: GetKickstand (0 arriba, 1 abajo); auto: 0
    // lo que ESTA pidiendo el conductor (para grabar al humano)
    float GasLeido() { return 0; }
    float FrenoLeido() { return 0; }
    float ManoLeida() { return 0; }
    float VolanteLeido() { return 0; }
    int EmbragueLeido() { return 0; }
    int BocinaEstado() { return 0; }

    // el roll del cuerpo (grados; + acompana un giro a la derecha): GetOrientation()[2], confirmado en la moto (r 1,00)
    float Roll() {
        if (!m_T) return 0;
        vector ori = m_T.GetOrientation();
        return ori[2];
    }

    // el conductor esta cambiando de marcha (la animacion del cambio = el embrague del humano); GetClutch no sirve
    bool Cambio() {
        if (!m_T) return false;
        Human conductor = m_T.CrewMember(0);
        if (!conductor) return false;
        HumanCommandVehicle hcvC = conductor.GetCommand_Vehicle();
        if (!hcvC) return false;
        return hcvC.WasGearChange();
    }

    // el angulo maximo de direccion del config: un escalar (autos) o una tabla por velocidad (motos: el mayor)
    float MaxVolanteDeg() {
        if (!m_T) return 0;
        return ConfigMax("CfgVehicles " + m_T.GetType() + " SimulationModule Steering maxSteeringAngle");
    }

    // la inclinacion maxima del config (motos: tabla por velocidad, el mayor); 0 si no hay
    float MaxInclinacionDeg() {
        if (!m_T) return 0;
        return ConfigMax("CfgVehicles " + m_T.GetType() + " SimulationModule Steering maxLeanAngle");
    }

    // un escalar, o el mayor VALOR de una tabla de pares {velocidad, valor, velocidad, valor...} (las de la moto:
    // maxSteeringAngle[]={1,40,5,40,...,80,1}); los indices pares son velocidades y no cuentan
    static float ConfigMax(string ruta) {
        float esc = GetGame().ConfigGetFloat(ruta);
        if (esc > 0) return esc;
        array<float> tabla = new array<float>();
        GetGame().ConfigGetFloatArray(ruta, tabla);
        float mx = 0;
        int i;
        for (i = 1; i < tabla.Count(); i = i + 2) {
            if (tabla.Get(i) > mx) mx = tabla.Get(i);
        }
        return mx;
    }

    // ---- mando
    void Poner(int marcha) {}
    void Arrancar() {}
    void Apagar() {}
    void Gas(float v) {}
    void Volante(float v) {}
    // el freno de servicio. Auto: SetBrake. Moto: SetBrake es el DELANTERO (medido 27/09, al reves de los comentarios
    // de Motorbike.c) y el trasero (SetHandbrake) acompana por debajo de 10 km/h
    void Frenar(float v) {}
    // el freno de estacionar. Auto: SetHandbrake. Moto: los dos frenos
    void FrenoEstacionar(bool puesto) {}
    void Embrague(float v) {}
    void PonerPata(float v) {}
    // lo que hace falta al tomar el mando / al soltarlo (moto: la pata arriba / neutral y la pata abajo)
    void TomarPosesion() {}
    void Soltar() {}
    // levantar un vehiculo caido (moto: PickUp)
    void Levantar() {}
    // los fluidos al maximo al spawnear (moto; el auto los llena EquipBus como siempre)
    void LlenarFluidos() {}

    // ---- lo que depende de la cascara del framework (BZBusCarScript / BZBusMotorbikeScript)
    bool TieneLucesBZ() { return false; }
    void Luces(bool prendidas) {}
    void LatidoLuces() {}
    bool LucesPrendidas() { return false; }
    void Bocina(int estado) {}
    void BloquearPiezas(bool bloqueadas) {}
}
