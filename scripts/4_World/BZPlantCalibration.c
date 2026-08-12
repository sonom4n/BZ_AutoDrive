// ============================================================================
//  BZPlantCalibration — V2 (2026-07-04). Rutina de barrido para IDENTIFICAR el
//  plant del vehiculo (el "manual de manejo fisico") con el physics-receiver.
//
//  PORQUE: el manejo casual es puro transitorio y no barre el cuadrante que
//  gobierna la estabilidad (angulo grande A velocidad = 0 muestras). Esta
//  rutina comanda entradas ESCALONADAS y SOSTENIDAS que dan steady-state limpio.
//
//  FASE B (esta version minima — el EXPERIMENTO DECISIVO del design doc):
//    "escalones de direccion, parado". Con el vehiculo QUIETO (throttle 0 +
//    handbrake), escalona cmd_steer por una serie de valores y mantiene cada
//    uno ~2.5s. El receiver mide front_wheel_deg (angulo REAL de rueda) por
//    frame -> resuelve DOS incognitas load-bearing:
//      1) GAIN ESTATICO G:  front_wheel_deg / cmd_steer. La pregunta que decide
//         toda la generalizacion cross-vehicle: G≈3.0 es constante del motor
//         Enfusion o per-vehiculo? (correr en >=2 vehiculos para responderla).
//      2) DINAMICA DEL ACTUADOR: la rueda RAMPEA ~0.3s (n=1). rate-limit vs
//         1er orden vs 2º orden subamortiguado -> se elige por el residual del
//         escalon. Requiere log a RATE DE FISICA (no gateado a 0.05).
//
//  Incluye cmd_steer = 0.333 (full-lock estimado) y 0.5 (confirmar el clamp),
//  ambos signos (asimetria). Parado = sin confound de yaw/velocidad: mide
//  cmd_steer -> front_wheel puro.
//
//  WIRING (3 puntos en BZBusService, gateado por m_CalActive; ver design doc):
//    1) StartPlantCalibration(): m_Cal = new BZPlantCalibration(); m_CalActive=true;
//       m_CalStartTime = GetGame().GetTickTime(); abrir receiver log "calib_*.csv".
//    2) TickBody(): if (m_CalActive) return; despues del housekeeping (saltear
//       el control de ruta para que no pise m_Cached*).
//    3) SampleReceiver(): if (m_CalActive) { float e = tickTime - m_CalStartTime;
//       m_CachedSteering = m_Cal.Steer(e); m_CachedThrottle=0; m_CachedBrake=0;
//       m_CachedHandbrake=1; LogRow SIEMPRE (bypass DueToLog);
//       if (m_Cal.IsDone(e)) StopPlantCalibration(); }
//    Trigger: una tecla (ej NUMPAD 8) o RPC. Aislado: no toca cruise/parking/reverse.
// ============================================================================
class BZPlantCalibration {

    // Schedule fase B: pares (cmd_steer, duracion_seg). Zeros entre escalones
    // para ver el retorno al centro (el actuador es simetrico? mismo tau?).
    ref array<float> m_Steer;
    ref array<float> m_Dur;
    float m_Total;

    void BZPlantCalibration() {
        m_Steer = new array<float>();
        m_Dur   = new array<float>();
        // secuencia: settle inicial, luego escalones + retorno, ambos signos.
        AddStep(0.0,   1.5);   // asentar centro
        AddStep(0.10,  2.5);   AddStep(0.0, 1.5);
        AddStep(0.20,  2.5);   AddStep(0.0, 1.5);
        AddStep(0.333, 2.5);   AddStep(0.0, 1.5);   // full-lock estimado
        AddStep(0.50,  2.5);   AddStep(0.0, 1.5);   // confirmar clamp (deberia dar mismo front_wheel que 0.333)
        AddStep(-0.10, 2.5);   AddStep(0.0, 1.5);
        AddStep(-0.20, 2.5);   AddStep(0.0, 1.5);
        AddStep(-0.333,2.5);   AddStep(0.0, 1.5);
        AddStep(-0.50, 2.5);   AddStep(0.0, 1.5);
    }

    private void AddStep(float steer, float dur) {
        m_Steer.Insert(steer);
        m_Dur.Insert(dur);
        m_Total += dur;
    }

    // cmd_steer para el tiempo transcurrido (seg desde el inicio de la calibracion).
    float Steer(float elapsed) {
        float acc = 0;
        for (int i = 0; i < m_Dur.Count(); i++) {
            acc += m_Dur.Get(i);
            if (elapsed < acc) return m_Steer.Get(i);
        }
        return 0.0;   // terminado -> centro
    }

    // indice del segmento actual (para taguear cal_seg en el log si se quiere).
    int SegIndex(float elapsed) {
        float acc = 0;
        for (int i = 0; i < m_Dur.Count(); i++) {
            acc += m_Dur.Get(i);
            if (elapsed < acc) return i;
        }
        return m_Dur.Count();
    }

    bool IsDone(float elapsed) { return elapsed >= m_Total; }
    float TotalTime() { return m_Total; }
    int   StepCount() { return m_Steer.Count(); }
}
