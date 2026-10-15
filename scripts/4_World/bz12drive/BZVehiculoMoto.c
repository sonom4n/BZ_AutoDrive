// ============================================================================
//  BZVehiculoMoto - BZVehiculo sobre la moto de DayZ 1.30 (Motorbike : Transport, NO es Car). Solo 1.30 en adelante.
//
//  Lo medido en el laboratorio de motos (herramienta aparte, 27/09) que vive aca:
//   - SetBrake es el freno DELANTERO (5,65 m/s2, el principal) y SetHandbrake el TRASERO (3,16): los comentarios de
//     Motorbike.c:132-139 lo dicen al reves. Frenar() usa el delantero y el trasero acompana por debajo de 10 km/h.
//   - no hay reversa: la marcha 0 es neutral (GetNeutralGear).
//   - la pata (kickstand): arriba al tomar el mando, abajo al soltarlo.
//   - caida: IsFallen(); se levanta con PickUp().
//  La insistencia de la marcha (el primer ShiftTo despues de EngineStart se pierde; el motor la devuelve a N al
//  terminar la animacion de subida) la hace el conductor de la moto, no esto: aca Poner() es un ShiftTo.
// ============================================================================
#ifndef DAYZ_1_29
class BZVehiculoMoto : BZVehiculo {

    static const float TRASERO_KMH = 10.0;   // por debajo, el trasero acompana al delantero

    protected Motorbike m_Moto;
    protected MotorbikeScript m_MotoScript;

    void BZVehiculoMoto(Motorbike m) {
        m_Moto = m;
        m_T = m;
        m_MotoScript = MotorbikeScript.Cast(m);
    }

    override bool EsMoto() { return true; }
    override string Tipo() { return "moto"; }
    override bool TieneReversa() { return false; }

    // el ultimo contacto en la zona delantera (lo anota la cascara, BZBusMotorbikeScript.OnContact); -1 = ninguno
    override float ContactoFrenteT() {
        if (!m_MotoScript) return -1;
        return m_MotoScript.m_BzContactoFrenteT;
    }

    override string ContactoFrenteClase() {
        if (!m_MotoScript) return "";
        return m_MotoScript.m_BzContactoFrenteClase;
    }

    // el ultimo contacto con un objeto (no el terreno): lo mira la caminata para cortar si empuja contra algo
    override float ContactoObjetoT() {
        if (!m_MotoScript || m_MotoScript.m_BzContactoT < 0) return -1;
        if (m_MotoScript.m_BzContactoClase == "terreno") return -1;
        return m_MotoScript.m_BzContactoT;
    }

    // el ultimo contacto de cualquier zona (la cascara guarda el de mayor impulso de medio segundo), para la linea del choque
    override string ContactoUltimo(float ventanaS) {
        if (!m_MotoScript || m_MotoScript.m_BzContactoT < 0) return "";
        float haceU = GetGame().GetTickTime() - m_MotoScript.m_BzContactoT;
        if (haceU > ventanaS) return "";
        string u = "zona " + m_MotoScript.m_BzContactoZona + ", con " + m_MotoScript.m_BzContactoClase;
        u = u + ", impulso " + m_MotoScript.m_BzContactoImp.ToString() + ", hace " + haceU.ToString() + " s";
        return u;
    }

    // ---- lectura
    override float KmhAbs() {
        if (!m_Moto) return 0;
        // el velocimetro puede no ser un numero (medido en un auto tras el spawn): entonces la velocidad del cuerpo, o 0
        float kmhM = m_Moto.GetSpeedometerAbsolute();
        if (BZMath.Finite(kmhM)) return kmhM;
        vector velM = GetVelocity(m_Moto);
        if (BZMath.FiniteVec(velM)) return velM.Length() * 3.6;
        return 0;
    }

    override int Marcha() {
        if (!m_Moto) return 0;
        return m_Moto.GetGear();
    }

    override int MarchaNeutral() {
        if (!m_Moto) return 0;
        return m_Moto.GetNeutralGear();
    }

    override int CuentaMarchas() {
        if (!m_Moto) return 0;
        return m_Moto.GetGearCount();
    }

    override float Rpm() {
        if (!m_Moto) return 0;
        return m_Moto.EngineGetRPM();
    }

    override float RpmRalenti() {
        if (!m_Moto) return 0;
        return m_Moto.EngineGetRPMIdle();
    }

    override float RpmMax() {
        if (!m_Moto) return 0;
        return m_Moto.EngineGetRPMMax();
    }

    override float RpmCorte() {
        if (!m_Moto) return 0;
        return m_Moto.EngineGetRPMRedline();
    }

    override bool MotorPrendido() {
        if (!m_Moto) return false;
        return m_Moto.EngineIsOn();
    }

    override int Ruedas() {
        if (!m_Moto) return 0;
        return m_Moto.WheelCount();
    }

    override int RuedasPresentes() {
        if (!m_Moto) return 0;
        return m_Moto.WheelCountPresent();
    }

    override bool RuedaContacto(int i) {
        if (!m_Moto) return false;
        return m_Moto.WheelHasContact(i);
    }

    override vector RuedaDireccion(int i) {
        if (!m_Moto) return vector.Zero;
        return m_Moto.WheelGetDirection(i);
    }

    override vector RuedaContactoPos(int i) {
        if (!m_Moto) return vector.Zero;
        return m_Moto.WheelGetContactPosition(i);
    }

    override bool Caida() {
        if (!m_Moto) return false;
        return m_Moto.IsFallen();
    }

    override float Pata() {
        if (!m_Moto) return 0;
        return m_Moto.GetKickstand();
    }

    override float GasLeido() {
        if (!m_Moto) return 0;
        return m_Moto.GetThrottle();
    }

    override float FrenoLeido() {
        if (!m_Moto) return 0;
        return m_Moto.GetBrake();
    }

    override float ManoLeida() {
        if (!m_Moto) return 0;
        return m_Moto.GetHandbrake();
    }

    override float VolanteLeido() {
        if (!m_Moto) return 0;
        return m_Moto.GetSteering();
    }

    override int EmbragueLeido() {
        if (!m_Moto) return 0;
        return m_Moto.GetClutch();
    }

    // ---- mando
    override void Poner(int marcha) {
        if (m_Moto) m_Moto.ShiftTo(marcha);
    }

    override void Arrancar() {
        if (m_Moto) m_Moto.EngineStart();
    }

    override void Apagar() {
        if (m_Moto) m_Moto.EngineStop();
    }

    override void Gas(float v) {
        if (m_Moto) m_Moto.SetThrottle(v);
    }

    override void Volante(float v) {
        if (m_Moto) m_Moto.SetSteering(v);
    }

    // el delantero es el principal; el trasero acompana por debajo de TRASERO_KMH (a velocidad, el trasero solo no frena
    // y bloqueado tira la moto)
    override void Frenar(float v) {
        if (!m_Moto) return;
        m_Moto.SetBrake(v);
        if (v > 0 && m_Moto.GetSpeedometerAbsolute() < TRASERO_KMH) m_Moto.SetHandbrake(v);
        else m_Moto.SetHandbrake(0);
    }

    override void FrenoEstacionar(bool puesto) {
        if (!m_Moto) return;
        if (puesto) {
            m_Moto.SetBrake(1.0);
            m_Moto.SetHandbrake(1.0);
        } else {
            m_Moto.SetHandbrake(0);
        }
    }

    override void Embrague(float v) {
        if (m_Moto) m_Moto.SetClutch(v);
    }

    override void PonerPata(float v) {
        if (m_Moto) m_Moto.SetKickstand(v);
    }

    override void TomarPosesion() {
        if (m_Moto) m_Moto.SetKickstand(0);
    }

    override void Soltar() {
        if (!m_Moto) return;
        m_Moto.ShiftTo(m_Moto.GetNeutralGear());
        m_Moto.SetKickstand(1.0);
    }

    override void Levantar() {
        if (!m_Moto) return;
        if (m_Moto.IsFallen()) m_Moto.PickUp();
    }

    // la config de MotorbikeScript declara combustible, aceite, refrigerante y liquido de frenos (vehicles_singletrack).
    // ⛔ NO `MotorbikeFluid.OIL/COOLANT/BRAKE`: en la 1.30 NO COMPILAN ("Can't find variable 'OIL'", 27/09; el motor solo
    // usa MotorbikeFluid.FUEL). Por las envolturas de Transport con la enum BASE (ETransportFluid), las mismas que usa el
    // motor (MotorbikeScript.c:243-256: GetFluidCapacityScript / FillScript -> GetFluidCapacity / Fill)
    override void LlenarFluidos() {
        if (!m_Moto) return;
        LlenarUno(ETransportFluid.FUEL, "combustible");
        LlenarUno(ETransportFluid.OIL, "aceite");
        LlenarUno(ETransportFluid.COOLANT, "refrigerante");
        LlenarUno(ETransportFluid.BRAKE, "liquido de frenos");
    }

    // un fluido al maximo; con capacidad 0 (la moto no lo tiene) no se intenta y el log lo dice
    protected void LlenarUno(ETransportFluid fluido, string nombre) {
        int cap = m_Moto.GetFluidCapacityScript(fluido);
        if (cap <= 0) {
            BZBusLog.Info("[MOTO] " + m_Moto.GetType() + ": sin " + nombre + " (capacidad 0): no se llena");
            return;
        }
        m_Moto.FillScript(fluido, cap);
    }

    // ---- la cascara del framework (BZBusMotorbikeScript): piezas; las luces del framework no se sincronizan en la moto
    override void BloquearPiezas(bool bloqueadas) {
        if (m_MotoScript) m_MotoScript.BZSetPartsLocked(bloqueadas);
    }
}
#endif
