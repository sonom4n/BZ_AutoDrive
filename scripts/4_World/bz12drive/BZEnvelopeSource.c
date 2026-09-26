// ============================================================================
//  BZEnvelopeSource.c - BZ_AutoDrive 1.2
//
//  EL CANAL DE LO MEDIDO. Interfaz chica e inyectada por la que entra todo lo que
//  se extrae de la DEMOSTRACION y no hay formula que lo de (MODULES.md 1.1, regla 2).
//
//  Esta clase base es el NULL OBJECT: "no hay medicion". Sirve para:
//    - arrancar sin demostracion previa (el Manual usa priors, marcados DERIVED/FALLBACK)
//    - testear el reader en aislamiento, sin envelope ni disco
//  El adaptador real contra BZVehicleEnvelope (persistido por clase de vehiculo) entra
//  con la migracion del shell. El cerebro nunca lo ve: solo ve el Manual ya cargado.
//
//  CONVENCION: devolver <= 0 (o false) significa SIN DATO. Nunca un valor inventado --
//  el prior lo pone el Manual, y queda marcado como prior. Un cero disfrazado de
//  medicion seria justo el numero magico que este diseno viene a matar.
// ============================================================================

class BZEnvelopeSource
{
    // Grilla de bins: la misma que BZVehicleManual (0..100 km/h de a 5).

    // ---- understeer k(v): el motor lo expone, nosotros lo extraemos ----
    bool  HasK(int bin)      { return false; }
    float KAt(int bin)       { return 0; }

    // ---- planeo a(v, piso): resume masa + freno motor + rodadura + aero + PISO ----
    bool  HasCoast(int surf, int bin) { return false; }
    float CoastAt(int surf, int bin)  { return 0; }

    // ---- geometria que la API del Car NO expone y por eso se MIDE ----
    //  radio de rueda: no hay WheelGetRadius en DayZ (verificado contra la API del server).
    //  Se mide rodando: r = v / omega, con WheelGetAngularVelocity y la velocidad del chasis.
    float WheelRadiusM()     { return 0; }
    //  wheelbase: medible de los contactos de rueda (WheelGetContactPosition).
    float WheelbaseM()       { return 0; }
    //  tope de direccion: el maximo |WheelGetDirection| observado en la demostracion.
    float SteerMaxRad()      { return 0; }
    //  ratio TOTAL por marcha: rpm del motor contra omega de rueda. Es lo que hace falta
    //  cuando el config de la caja no resuelve -- y se mide igual de bien que el radio.
    float RatioForGear(int gearDayZ) { return 0; }
    //  creep: la velocidad de RALENTI en 1ra. Se venia DERIVANDO del ratio de caja, que
    //  para este vehiculo no converge (3.94 a 54.75 en la misma marcha). Pero creep es
    //  directamente OBSERVABLE: es la velocidad a la que el vehiculo se queda solo, sin
    //  gas y sin freno, en llano. No hace falta la caja para medir lo que la caja hace.
    float CreepKmh()         { return 0; }
    //  piso de despegue: el gas con el que el vehiculo EMPIEZA A MOVERSE desde parado.
    //  Tambien observable, y tambien se venia derivando de la cadena de caja.
    float LaunchFloor()      { return 0; }
    // cuantos despegues de CALIBRACION sostienen LaunchFloor (0 = no calibrado).
    int   LaunchCalSamples() { return 0; }
    //  ⚠ HAY EVIDENCIA POSITIVA de que este vehiculo NO REPTA -- distinto de "no se midio".
    //  Va ACA y no solo en la implementacion: el cerebro y el reader ven este canal por la
    //  interfaz, asi que un metodo que existe solo en el descendiente no existe para ellos.
    //  El NULL OBJECT contesta false: sin medicion no se puede afirmar que no repta.
    bool  CreepIsZeroMeasured() { return false; }
    // (pedido del usuario): las dos cuentas del creep, para que la linea [Manual] diga quien gano y por que.
    int   CreepSamples()     { return 0; }
    int   NoCreepSamples()   { return 0; }
    // QUIEN decidio el creep (hdr:coast2 | coast2 | nocreep | chist2 | vacio) y el coast a v->0 que lo sostiene.
    string CreepSource()     { return ""; }
    float  CoastAtRest()     { return 0; }
    int    CoastAtRestSamples() { return 0; }
    bool  HasSteerTransfer(int bucket) { return false; }
    float SteerTransferAt(int bucket)  { return 0; }
    //  aceleracion maxima REAL. Derivarla de torque x ratio / radio / masa encadena cuatro
    //  datos que pueden estar en FALLBACK; medirla directamente no depende de ninguno.
    float MaxAccelMs2()      { return 0; }
}
