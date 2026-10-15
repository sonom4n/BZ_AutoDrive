// ============================================================================
//  BZVehiculoFabrica - el UNICO lugar del framework que castea a Car / CarScript / Motorbike.
//
//  Decide por JERARQUIA, nunca por nombre de clase: un Car (CarScript) es un auto, aunque sea una "moto" de mod de
//  cuatro ruedas (RUSForma, MBM); un Motorbike (MotorbikeScript, DayZ 1.30, no es Car) es una moto, vanilla o de mod.
//  En la 1.29 la moto no existe: la rama de la moto esta bajo #ifndef DAYZ_1_29.
//
//  Los helpers estaticos son para objetos que no son el vehiculo del runner (el mundo alrededor, el que choco): hacen
//  el mismo cast que hacia el codigo que los llama.
//  El banco del adaptador (herramienta del analista, fuera del mod) verifica que no quede ningun Car.Cast /
//  CarScript.Cast fuera de los archivos BZVehiculo*.c.
// ============================================================================
class BZVehiculoFabrica {

    static BZVehiculo Para(Transport t) {
        if (!t) return null;
        Car c = Car.Cast(t);
        if (c) return new BZVehiculoAuto(c);
#ifndef DAYZ_1_29
        Motorbike m = Motorbike.Cast(t);
        if (m) return new BZVehiculoMoto(m);
#endif
        return null;
    }

    // el vehiculo de un objeto cualquiera (p. ej. el padre del jugador que graba); null si no es un vehiculo manejable
    static BZVehiculo De(Object o) {
        return Para(Transport.Cast(o));
    }

    // un vehiculo del mundo (obstaculo, el que choco, el que ocupa una parada): un CarScript y, desde la 1.30, una moto
    static bool EsVehiculo(Object o) {
        if (CarScript.Cast(o)) return true;
#ifndef DAYZ_1_29
        if (MotorbikeScript.Cast(o)) return true;
#endif
        return false;
    }

    // de transicion, para el codigo que todavia recibe Car (probes, llenado de fluidos del auto)
    static Car AutoDe(Object o) {
        return Car.Cast(o);
    }

    static CarScript AutoScriptDe(Object o) {
        return CarScript.Cast(o);
    }

    // el ultimo contacto que anoto la cascara (BZBusCarScript.OnContact): clase del objeto, cuando y con que impulso.
    // false si el objeto no tiene cascara o todavia no hubo ninguno
    static bool Contacto(Object o, out string clase, out float cuando, out float impulso) {
        clase = "";
        cuando = -1;
        impulso = 0;
        CarScript csK = CarScript.Cast(o);
        if (!csK || csK.m_BzContactoT < 0) return false;
        clase = csK.m_BzContactoClase;
        cuando = csK.m_BzContactoT;
        impulso = csK.m_BzContactoImp;
        return true;
    }

    // la zona del casco de ese ultimo contacto ("" si no hubo ninguno)
    static string ContactoZona(Object o) {
        CarScript csZ = CarScript.Cast(o);
        if (!csZ || csZ.m_BzContactoT < 0) return "";
        return csZ.m_BzContactoZona;
    }
}
