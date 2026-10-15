// ============================================================================
//  BZVehiculoAuto - BZVehiculo sobre un Car (CarScript con la cascara BZBusCarScript).
//  Delegacion pura: cada metodo es la llamada al motor que el servicio hacia directo sobre el Car. Nada de logica.
// ============================================================================
class BZVehiculoAuto : BZVehiculo {

    protected Car m_Car;
    protected CarScript m_CarScript;   // null si el Car no es un CarScript (no pasa en DayZ; se chequea igual)

    void BZVehiculoAuto(Car c) {
        m_Car = c;
        m_T = c;
        m_CarScript = CarScript.Cast(c);
    }

    override Car Auto() { return m_Car; }
    override bool EsAuto() { return true; }
    override string Tipo() { return "auto"; }
    override bool TieneReversa() { return true; }

    override int MarchaNeutral() { return CarGear.NEUTRAL; }

    override int CuentaMarchas() {
        if (!m_Car) return 0;
        return m_Car.GetGearCount();
    }

    override float RpmMax() {
        if (!m_Car) return 0;
        return m_Car.EngineGetRPMMax();
    }

    override int RuedasPresentes() {
        if (!m_Car) return 0;
        return m_Car.WheelCountPresent();
    }

    override vector RuedaContactoPos(int i) {
        if (!m_Car) return vector.Zero;
        return m_Car.WheelGetContactPosition(i);
    }

    override float GasLeido() {
        if (!m_Car) return 0;
        return m_Car.GetThrottle();
    }

    override float FrenoLeido() {
        if (!m_Car) return 0;
        return m_Car.GetBrake();
    }

    override float ManoLeida() {
        if (!m_Car) return 0;
        return m_Car.GetHandbrake();
    }

    override float VolanteLeido() {
        if (!m_Car) return 0;
        return m_Car.GetSteering();
    }

    override int EmbragueLeido() {
        if (!m_Car) return 0;
        return m_Car.GetClutch();
    }

    override int BocinaEstado() {
        if (!m_CarScript) return 0;
        return m_CarScript.BZBus_GetCarHornState();
    }

    override void Embrague(float v) {
        if (m_Car) m_Car.SetClutch(v);
    }

    // ---- lectura
    override float KmhAbs() {
        if (!m_Car) return 0;
        // el velocimetro puede no ser un numero (medido tras el spawn): entonces la velocidad del cuerpo, o 0
        float kmhA = m_Car.GetSpeedometerAbsolute();
        if (BZMath.Finite(kmhA)) return kmhA;
        vector velA = GetVelocity(m_Car);
        if (BZMath.FiniteVec(velA)) return velA.Length() * 3.6;
        return 0;
    }

    override int Marcha() {
        if (!m_Car) return 0;
        return m_Car.GetGear();
    }

    override float Rpm() {
        if (!m_Car) return 0;
        return m_Car.EngineGetRPM();
    }

    override float RpmRalenti() {
        if (!m_Car) return 0;
        return m_Car.EngineGetRPMIdle();
    }

    override float RpmCorte() {
        if (!m_Car) return 0;
        return m_Car.EngineGetRPMRedline();
    }

    override bool MotorPrendido() {
        if (!m_Car) return false;
        return m_Car.EngineIsOn();
    }

    override int Ruedas() {
        if (!m_Car) return 0;
        return m_Car.WheelCount();
    }

    override bool RuedaContacto(int i) {
        if (!m_Car) return false;
        return m_Car.WheelHasContact(i);
    }

    override vector RuedaDireccion(int i) {
        if (!m_Car) return vector.Zero;
        return m_Car.WheelGetDirection(i);
    }

    // ---- mando
    override void Poner(int marcha) {
        if (m_Car) m_Car.ShiftTo(marcha);
    }

    override void Arrancar() {
        if (m_Car) m_Car.EngineStart();
    }

    override void Apagar() {
        if (m_Car) m_Car.EngineStop();
    }

    override void Gas(float v) {
        if (m_Car) m_Car.SetThrottle(v);
    }

    override void Volante(float v) {
        if (m_Car) m_Car.SetSteering(v);
    }

    override void Frenar(float v) {
        if (m_Car) m_Car.SetBrake(v);
    }

    override void FrenoEstacionar(bool puesto) {
        if (!m_Car) return;
        if (puesto) m_Car.SetHandbrake(1.0);
        else m_Car.SetHandbrake(0);
    }

    // ---- la cascara del framework (BZBusCarScript)
    override bool TieneLucesBZ() {
        if (m_CarScript) return true;
        return false;
    }

    override void Luces(bool prendidas) {
        if (m_CarScript) m_CarScript.BZSetLights(prendidas);
    }

    override void LatidoLuces() {
        if (m_CarScript) m_CarScript.BZPulseLights();
    }

    override bool LucesPrendidas() {
        if (!m_CarScript) return false;
        return m_CarScript.IsScriptedLightsOn();
    }

    override void Bocina(int estado) {
        if (m_CarScript) m_CarScript.SetCarHornState(estado);
    }

    override void BloquearPiezas(bool bloqueadas) {
        if (m_CarScript) m_CarScript.BZSetPartsLocked(bloqueadas);
    }
}
