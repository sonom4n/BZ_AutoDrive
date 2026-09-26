// ============================================================================
//  BZBusInputApplier.c - BZ_AutoDrive 1.2
//
//  ACTUADOR del sandwich (MODULES.md 0): lo UNICO en todo el mod que escribe los
//  controles del vehiculo. Del lado del RUNNER.
//
//  Es a proposito tonto. El refinamiento por frame (freno de endpoint + slew de
//  volante) NO va aca: necesita geometria de ruta y vive en Controller.Sample()
//  (SEAM.md, cara 3). Mantener ese corte es lo que impide que el applier se
//  convierta de a poco en un segundo controlador -- que es exactamente lo que le
//  paso al FastSteerTick de v1.1.
//
//  NO SE PORTA de v1.1: alla ApplyBusInput() se lleno del experimento FrameReplay
//  (posicion/orientacion/velocidad forzadas, coast-comp, auto-logger) adentro del
//  camino de escritura. Eso es un experimento, no un actuador. Esta version es el
//  actuador limpio de v1.0: escribe y nada mas.
// ============================================================================

class BZBusInputApplier
{
    // Campos NOMBRADOS: el bug de orden de argumentos del slam (volante aplicado como
    // freno, 2.5m de precision perdidos, latente por vehiculo) no se puede escribir aca.
    void Apply(Car bus, BZDriveOutput o)
    {
        if (!bus) return;
        if (!o) return;
        o.Clamp();
        bus.SetThrottle(o.throttle);
        bus.SetSteering(o.steering);
        bus.SetBrake(o.brake);
        bus.SetHandbrake(o.handbrake);
        if (bus.GetGear() != o.gear) bus.ShiftTo(o.gear);
    }
}
