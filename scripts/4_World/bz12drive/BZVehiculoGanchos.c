// ============================================================================
//  BZVehiculoGanchos - los cuerpos de los overrides de la cascara, UNA vez para todos los tipos de vehiculo.
//
//  La cascara del auto (BZBusCarScript: modded class CarScript) llama a esto desde sus overrides; la de la moto
//  (modded class MotorbikeScript, paso 4) va a llamar a lo mismo. Lo que queda en cada cascara es lo que el motor obliga
//  a tener por clase: las NetSync propias (luces, piezas), las luces, la caja automatica del auto y el registro de
//  contactos (estado por instancia).
//
//  Hoy el cerebro todavia recibe Car: los ganchos le pasan v.Auto(). Con una moto (Auto() null) no hacen nada, como hoy.
// ============================================================================
class BZVehiculoGanchos {

    // OnInput, despues del super: el runner DUENO del vehiculo le aplica su comando (solo server). Con un jugador real
    // en el asiento 0 el framework se hace a un lado (eAI tambien es PlayerBase: se distingue por GetIdentity).
    static void OnInput(Transport t, BZVehiculo v, float dt) {
        if (!GetGame().IsServer()) return;

        BZBusService srvIn = BZBusService.GetRunnerForCar(t);   // MULTITON: el runner DUENO de este vehiculo (no el primary)
        if (!srvIn) return;

        Human driverIn = t.CrewMember(0);
        if (driverIn) {
            PlayerBase realPlayer = PlayerBase.Cast(driverIn);
            if (realPlayer && realPlayer.GetIdentity()) {
                return;
            }
        }

        if (!v) return;

        // LA MOTO: su conductor (BZConductorMoto, la ley v6 del laboratorio de motos (herramienta aparte) + la velocidad objetivo por arco) sobre la cinta. Si no
        // esta manejando (spawn, pausa, hold, fin), el comando cacheado del servicio, traducido a la moto
        if (v.EsMoto()) {
            if (srvIn.SampleMoto(v, dt)) return;
            srvIn.AplicarCacheadoMoto(v);
            return;
        }

        Car carIn = v.Auto();
        if (!carIn) return;

        // ===== CEREBRO 1.2 (F2): cuando el controller maneja, TODO el actuador por-frame es UNA llamada (Sample+apply)
        if (srvIn.SampleDrive12(carIn, dt)) return;

        // Freno predictivo del endpoint PER-FRAME: en los ultimos 5 m antes de un stop recomputa el freno cada frame
        srvIn.EndpointFrameBrake(carIn);

        // Aplicar throttle/steering/brake cacheado (sobreescribe lo que eAI puso)
        srvIn.ApplyBusInput(carIn, dt);

        // Sobreescribir el gear: eAI metio FIRST en super.OnInput, ponemos el deseado
        int desired = srvIn.GetDesiredGear();
        if (v.Marcha() != desired) {
            v.Poner(desired);
        }
    }

    // EOnPostSimulate, despues del super: el grabador cuadro a cuadro (cliente que maneja; ANTES de buscar runner,
    // porque el auto del humano no tiene), el probe del server y el receptor fisico del runner. Devuelve el runner
    // dueno (o null) para que la cascara siga con lo suyo (la caja automatica del auto).
    static BZBusService PostSimulate(Transport t, BZVehiculo v, float timeSlice) {
        Car carPs = null;
        if (v) carPs = v.Auto();

        // el grabador recibe el vehiculo (auto o moto); el probe y el receptor siguen siendo solo del auto
        BZFrameRecorder frec = BZFrameRecorder.GetInstance();
        if (frec.IsActive() && v) frec.Frame(v, timeSlice);

        if (GetGame().IsServer() && carPs) BZServerProbe.GetInstance().Frame(carPs, timeSlice);

        BZBusService srvPs = BZBusService.GetRunnerForCar(t);   // MULTITON: el runner DUENO de este vehiculo
        if (!srvPs) return null;

        if (carPs) srvPs.SampleReceiver(carPs, timeSlice);
        return srvPs;
    }

    // EEHitBy, despues del super (ESCENA 2, ambush): un tiro al vehiculo gestionado avisa al runner; si el que pega es
    // OTRO VEHICULO (choque), tambien el aviso de escape (AR_OnWay fase 2)
    static void Golpe(Transport t, EntityAI source) {
        if (!GetGame().IsServer()) return;
        BZBusService srvGp = BZBusService.GetRunnerForCar(t);   // MULTITON: el runner DUENO de este vehiculo
        if (srvGp) srvGp.NotifyConvoyDamaged();
        if (srvGp && source && BZVehiculoFabrica.EsVehiculo(source)) srvGp.NotifyHitByVehicle();
    }
}
