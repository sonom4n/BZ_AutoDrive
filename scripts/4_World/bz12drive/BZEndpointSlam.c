// ============================================================================
//  BZEndpointSlam.c - BZ_AutoDrive 1.2 -- modulo POSE
//
//  Frenado-a-pose preciso. En v1.1 (CarrySlam + EndpointLatch) clavaba ~8 cm y
//  ESO andaba: la logica se conserva, lo que cambia es de donde salen los numeros.
//
//  ES UN MODULO POSE (SEAM.md 4): produce BZDriveOutput DIRECTO y esta EXENTO de la
//  DerivationEngine. Motivo fisico: trabaja a ~0-2 km/h contra un punto, donde la
//  curvatura que el vehiculo necesita la domina el ERROR DE POSE, no la forma del
//  camino, y el modelo bicicleta es ill-posed. Pero exento de la derivacion NO es
//  exento del MANUAL (SEAM.md 4.1): decel, creep y ganancia de volante salen de ahi.
//
//  ESTADO PROPIO: m_Latched + m_MinDist. Nadie mas los lee. Que clavo se lo reporta
//  al Controller por IsLatched(); el arbitro NO se lo pregunta al slam.
//
//  ⚠ NOTA QUE SE CONSERVA DE v1.1 -- NO "MEJORAR" ESTO ACA:
//  el overshoot es una proyeccion firmada que MIENTE si la reversa dejo a Boris
//  descolocado. Cuando el endpoint queda largo despues de una reversa, el bug esta
//  EN LA REVERSA, no en el slam. Perseguir el sintoma aca es exactamente como nacio
//  la pila de parches de v1.1.
// ============================================================================

// =========================================================================================
//  ⚠ LIMITE CONOCIDO Y DECLARADO: EL ENDPOINT SUB-MORDIDA (~10.4 cm en el CivilianSedan)
//
//  El vehiculo cierra la ruta a ~10.4 cm del ultimo waypoint y no menos. NO es un bug abierto
//  ni un pendiente: es una frontera medida, con la causa identificada y la decision tomada.
//
//  EL MECANISMO. Por debajo de la mordida el embrague enganchado arrastra el motor, y ese
//  arrastre crece fuerte al acercarse al ralenti: medido `aReal -0.667 m/s2` a ~1 km/h contra
//  un arrastre modelado de 0.35. Le gana al empuje del CARRY, el vehiculo se detiene, y desde
//  parado el gas ya no lo mueve -- el rpm CAE bajo throttle creciente (996 -> 927 con thr
//  subiendo). Medido seis veces, en corridas independientes.
//
//  LO QUE SE PROBO Y NO ALCANZO, cada uno falsado por su lado:
//    1a marcha         -- el slam ya comanda `o.gear = 2` incondicional: el bog ocurre EN 1ra.
//    gas               -- CARRY_MAX 0.60 -> 1.0; a mas gas el rpm cae MAS, no menos.
//    coast-discount    -- `aNeed = aWant - aFree` (el freno completa el planeo en vez de
//                         sumarse): mejoro la aproximacion, no cerro el ultimo tramo.
//    calibrar el planeo-- IMPOSIBLE por construccion: el banco exige gas Y freno en cero, y
//                         aca el modulo actua todos los ticks (3 muestras de 7551 en una
//                         corrida entera). El arrastre sub-mordida es una cota inferior
//                         estructural, no una calibracion pendiente.
//    guardian de freno -- no aplicar un freno mas chico que la barra de error del arrastre.
//                         Sostuvo 2 km/h con gas SIN bogar y bajo el endpoint a 10.36 cm.
//
// LA DECISION (el usuario, ): NO se actua el embrague. `SetClutch` existe en la API y
//  desembragar es la unica herramienta que venceria este arrastre, pero no justifica una
//  actuacion de produccion sobre el embrague por 3.6 mm. **El endpoint queda declarado.**
//  La ruta reporta ROUTE_INCOMPLETE por esos 3.6 mm, honestamente, y eso es lo correcto:
//  el veredicto dice la verdad y el numero esta al lado.
//
//  SI ALGUN DIA SE RETOMA: el instrumento ya deja el dato servido. El log SLAM imprime
//  `aFree` (lo que el modelo cree) contra `aReal` (lo que el vehiculo hace) en el mismo
//  renglon, mas la rama que tomo el perfil. La brecha entre esos dos numeros ES la magnitud
//  del arrastre que habria que vencer.
//
//  ⚠ Y UN TECHO APARTE, NO CONFUNDIR CON ESTE: el slam actua a 2 Hz. Los ultimos 38 cm se
//  recorren en ~0.7 s, o sea UNA sola decision longitudinal, con el umbral de parada en 1 cm.
//  Eso acota la precision alcanzable con independencia de la traccion, y toca el SEAM.
// =========================================================================================
class BZEndpointSlam
{
    // ★ DOS CANDIDATOS DEL ENDPOINT, para medirlos por separado (el con_sonda los lee de BZ12_SONDA_ENDPOINT.txt; sin archivo, prendidos):
    //   A: el pedal se dosifica contra su GANANCIA FISICA (BZVehicleManual.GananciaPedal), no contra el tope. Medido en el banco de 15:
    //      con `brake = aNeed / aMax` el vehiculo entrega 1,6-2,5x lo pedido (RX8: pedal 0,45 y 0,65 dan los mismos -7,7 m/s2; Oshkosh 0,37
    //      -> -8,1; Sarka 0,29 -> -4,2) y se detiene a la MITAD del camino planeado; las siete tomas que cierran a 0,11 lo hacen por el CARRY.
    //      El tope (mu*g y la frenada a fondo de la toma) es lo que el PISO permite, no lo que el pedal hace; para la zona y la planificacion
    //      sigue mandando el tope. La ganancia se toma de la fisica porque las tomas no traen pedal parcial (el humano frena con la tecla:
    //      1,0 o nada, en las 11 cintas medidas): no hay de donde medirla.
    //   B: el CARRY en subida arranca con el gas que pide la cuesta (-aFree / aMax, la regla del HillStart de la maniobra), sostiene el
    //      freno el primer tick y el retroceso no cuenta para la regla 3 del cierre durante sus primeros CUESTA_TICKS. Medido: la Sarka y el
    //      APC, en subida (aFree -1,45 y -1,88), arrancaban con 0,13-0,18, rodaban 0,26 m atras y la regla 3 los clavaba al tercer tick.
    //   B, medido (banco de 15 con A y B): el gas de arranque salia de la aceleracion maxima del manual, optimista en el APC (10 m/s2 en
    //      subida: arranque 0,18), el freno se sostenia UN tick y el APC rodo 2 m hacia atras dentro de la exencion de la regla 3. Ahora: el
    //      gas va a fondo (sin aceleracion maxima de por medio); el retroceso lo frena el controlador cuadro a cuadro (EndpointFrameCuesta:
    //      si el remanente supera en CUESTA_RETRO_M la posicion mas adelantada, freno a fondo hasta el tick siguiente, que vuelve a
    //      soltar), desde que el vehiculo queda por debajo del creep en la cuesta (CuestaParado), antes o durante el CARRY; y la exencion
    //      de la regla 3 caduca a CUESTA_RETRO_MAX_M de retroceso.
    //   (Se probo y se saco un "tope real" f/m x pressureBySpeed(v) para la zona del arbitro: el modelo no coincide con lo medido --el V140
    //      a 113 km/h freno 7,5 m/s2 con pedal 0,22, no 4,6-- y armaba el endpoint a 144 m del final. La cinta es la toma.)
    //   D (APAGADO, solo para medir con d=1): el volante del slam a 40 Hz con el lateral y el remanente del cuadro, el fade del termino
    //      lateral por el MAYOR entre el de distancia y el de velocidad, y su curvatura acotada a LAT_K_MAX_D. Medido en la Z71: con A el
    //      vehiculo para en 1,5 m a 5-8 km/h, el volante se decide 2-3 veces en ese tramo y el lateral contra la pose final queda en
    //      0,12-0,19; con esto cierra en 0,11. (El fade SOLO por velocidad le quitaba correccion al que llega frenando fuerte: el Agera,
    //      a 3 km/h a 1 m del punto, quedaba en -0,14 donde el fade por distancia lo llevaba a -0,02.)
    //   B, medido otra vez (banco de 15 con el freno por retroceso): el casco ya no rueda, pero la rampa desde el piso (0,13 -> 1,0 en
    //      cinco ticks) deja que el vehiculo se detenga del todo en la cuesta y de parado, en la sub-mordida de 1ra, ni con gas a fondo
    //      arranca (Sarka: 1,0-1,3 m en 4 de 10). Con gas a fondo desde el PRIMER tick, mientras todavia queda inercia, subia 9 de 10.
    //      Asi que en cuesta el gas es CARRY_MAX desde el primer tick; el retroceso lo sigue frenando el controlador.
    //   D, medido (banco de 15, mundo limpio, a=1 b=1 d=1): las diez tomas con 0,110 m de promedio, mediana 0,106, maximo 0,17, 0 de 79 sobre
    //      0,25 (Sarka y APC en 0,10). Queda PRENDIDA; d=0 la apaga.
    //   E: EL AR EN EL CIERRE (vive en BZDriveController.ArCierreDeja): en ENDPOINT la escalera fisica del AR solo se abre con un contacto
    //      reciente, nunca por un trabado sin contacto (eso es el arranque lento, y lo cierra el slam); con contacto, segunda aproximacion.
    static bool s_CandA = true;
    static bool s_CandB = true;
    //   F: EL ARRASTRE ENTRE VEHICULOS (BZDriveController.DetectarArrastre): contacto sostenido con el mismo vehiculo y la velocidad separada de
    //      la toma; el que empuja cede el paso. G: EL TOPE DEL AR (VigilarTopeDelAR): escaleras sin avance -> STUCK con FIN.
    static bool s_CandD = true;
    static bool s_CandE = true;
    static bool s_CandF = true;
    static bool s_CandG = true;
    private const float CUESTA_MIN_MS2 = 0.2;   // B solo con pendiente real: 9,81*sin(pitch) por encima de esto (~1,2 %); en llano no cambia nada
    private const int   CUESTA_TICKS = 6;       // ticks de arranque en cuesta en los que el retroceso no clava (3 s a 2 Hz; la regla 4 sigue)
    private const float CUESTA_RETRO_MAX_M = 0.30;  // B: mas retroceso que esto desde el arranque y la regla 3 vuelve a contar
    static const float  CUESTA_RETRO_M = 0.005;     // B: lo que el controlador considera "retrocede" de un cuadro al otro (5 mm)
    private const float LAT_K_MAX_D = 0.30;         // D: tope de la curvatura lateral a 40 Hz (R >= 3,3 m)
    private const float LAT_V_FADE_FIN = 0.5;       // D: el termino lateral apagado por debajo de esta velocidad (m/s)...
    private const float LAT_V_FADE_INI = 2.0;       // ...y entero por encima de esta

    // Longitud de correccion del alineado final: en wheelbases y en metros.
    private const float ALIGN_L_WB  = 2.0;
    private const float ALIGN_L_MAX = 8.0;
    // TERMINO LATERAL DE POSE en el cierre. Medido en tres llegadas desde CRUISE (KRASNOSTAV, DEMOBUS,
    // SPRINTER): el error lateral FINAL es el error de ENTRADA al ENDPOINT menos ~0,09 m, porque aca solo se alineaba
    // el rumbo ("el desvio lateral ya lo cerro el MC" -- cierto en un final por maniobra, falso en una llegada desde
    // crucero, donde la cinta suelta a 1 batalla y nadie cierra el lateral). DEMOBUS fallo por 9 mm (0,259 vs 0,25) y
    // KRASNOSTAV por 6 cm (-0,309), con `pide`/`aplica` en cero desde la entrada hasta parar.
    // Ley: curvatura de persecucion hacia el punto final, 2*lat/L^2 con L = max(d, 1 batalla) -- misma familia que la
    // mira del pursuit --, ACOTADA (R >= 8 m) y APAGADA en el ultimo medio metro (a 0-2 km/h el modelo de bicicleta
    // esta mal condicionado: exencion POSE del SEAM 4, que aca se respeta por el fade, no por la ausencia de ley).
    // Banco (bicicleta cinematica sobre los logs, psi0 calibrado para que la ley de hoy reproduzca lo medido):
    //   KRASNOSTAV -0,309 -> -0,080 · DEMOBUS -0,236 -> -0,037 · SPRINTER 0,002 -> 0,001
    //   mutantes: sin piso -> se pasa al otro lado (+0,13/+0,15) · ganancia x2 -> se pasa (+0,10) · signo invertido -> -0,60
    // Solo legs de FRENTE: en reversa el cierre lo hace el MC y la geometria del signo cambia; no se toca.
    private const float LAT_L_WB       = 1.0;    // piso del lookahead lateral: una batalla
    private const float LAT_K_MAX      = 0.12;   // 1/m: tope de la curvatura lateral (R >= 8 m)
    private const float LAT_FADE_INI_M = 1.5;    // desde aca se apaga...
    private const float LAT_FADE_FIN_M = 0.5;    // ...y aca esta apagado

    // Pasado el punto mas esta distancia, se clava igual: seguir corrigiendo de mas
    // lejos es como se generan los vaivenes alrededor del objetivo.
    // ⚠ LA AUDITORIA LO MARCO COMO ABSOLUTO, Y LA RESPUESTA NO ES VOLVERLO RELATIVO AL
    // VEHICULO. "Pasado el punto mas esto, se clava" no habla del tamano del vehiculo:
    // habla de con cuanta PRECISION se pretende cerrar. Y la precision del framework ya
    // esta declarada en un solo lugar -- BZDriveTol.LLEGADA_M, decision del usuario.
    // 0.40 = 4 x 0.10 exacto, asi que la relacion ya estaba y no estaba escrita.
    //
    // Convertirlo a batallas habria sido re-codificar el numero del sedan con otra forma
    // (0.40/2.931 = 0.136, un multiplo sacado de un vehiculo). Dejar un absoluto CON su
    // razon declarada es mejor que volverlo relativo por deporte: lo que la auditoria pide
    // es que no quede ningun numero SIN EXPLICAR, no que todo sea relativo.
    private const float OVERSHOOT_TOL_MULT = 4.0;
    // Fraccion del creep por debajo de la cual se considera detenido de verdad.
    private const float STOPPED_FRAC = 0.5;
    // "Ya estas": el epsilon de parada, MUY adentro del bar de aceptacion. No es la
    // tolerancia -- es la distancia a la que seguir empujando no tiene sentido.
    private const float LLEGADA_EPS_M = 0.01;
    // Debajo de la mordida, un freno que pesa menos que la MITAD del termino de arrastre
    // esta por debajo de la resolucion del modelo que lo calculo: no se aplica.
    private const float FRENO_MIN_FRAC = 0.5;
    // ⚠⚠ FRENAR UN VEHICULO YA DETENIDO QUE TODAVIA LE FALTA NO PUEDE AYUDAR, Y SI DAÑAR.
    //
    // Medido en el ZiS5 (ruta limpia, endpoint a 1.20 m): desacelera 2.36 -> 0.48 -> ~0 km/h
    // y se PARA a 1.19 m del punto, con `carry 0` y `brk` a fondo, hasta que `sinProgreso`
    // llega a 7 y se rinde. El Scammel, en la misma ley, cierra a 0.12-0.28 m porque nunca
    // se detiene y toma la rama de CARRY (`carry 13-16`).
    //
    // El mecanismo: en esta rama `d` SIEMPRE es positivo --el sobrepaso retorna antes, en
    // `d <= LLEGADA_EPS_M`--. Con v ~ 0, `aWant = -v^2/(2d)` tiende a CERO, asi que
    // `aNeed = aWant - aFree` queda mandado por `aFree`: si el tramo final va en bajada,
    // `aFree` es positivo, `aNeed` sale negativo y **se pide freno con el vehiculo quieto**.
    // El freno gana, la funcion retorna, y la rama de CARRY --que existe exactamente para
    // empujar esos ultimos centimetros-- NUNCA se evalua. Por eso `carry 0`.
    //
    // Por debajo de este umbral el vehiculo no puede sobrepasar de forma apreciable (a 0.3
    // m/s el propio arrastre lo detiene en centimetros), asi que el freno no protege de
    // nada; lo unico que hace es garantizar que no llegue.
    //
    // ✖ NO se reordenan las ramas, que era la otra salida. El gate del CARRY es
    // `v < creepKmh * STOPPED_FRAC`, y `creepKmh` viene MEDIDO Y SUCIO (Scammel 16.4 km/h,
    // que es coast, no reptado): poner CARRY primero le daria gas al Scammel por debajo de
    // 8.2 km/h y lo haria pasarse. La guarda es absoluta y angosta a proposito -- no cuelga
    // de un numero que todavia sabemos que esta mal.
    private const float DETENIDO_MS = 0.3;
    // Mejora minima de distancia que cuenta como PROGRESO.
    private const float PROGRESS_EPS_M = 0.02;
    // Ticks detenido SIN acercarse antes de aceptar la pose alcanzada. A 2 Hz, 6 = 3 s.
    private const int   NO_PROGRESS_TICKS = 6;
    // El empuje de arrimo RAMPEA (mismo patron que el despegue de MC): un valor fijo que
    // no alcanza para los ultimos centimetros no los alcanza NUNCA.
    private const float CARRY_RAMP_STEP = 0.05;
    // ⚠ ERA 0.60 Y ES EL QUE ATABA. Medido en el endpoint: el CARRY llega al tope
    // (thr 0.58 -> 0.60), el vehiculo NO se traslada (v ~0.0013 m/s) y el rpm CAE bajo gas
    // (948 -> 940 -> 933 -> 928). Un tope que no alcanza para los ultimos centimetros no
    // los alcanza nunca -- el mismo patron que el piso de despegue y el hill-start.
    //
    // El sobrepaso lo acota la estructura que ya existe, no un numero nuevo: CARRY solo
    // actua cuando aReq <= 0.05, o sea cuando el vehiculo SI puede frenar en lo que queda;
    // apenas se mueve, la rama de frenada toma el control en el tick siguiente.
    private const float CARRY_MAX = 1.0;

    private bool  m_Latched;
    private float m_MinDist;
    // ⚠⚠⚠ SE ESTA ACERCANDO EN ESTE TICK (mejora > PROGRESS_EPS_M). Ver ShouldLatch:
    // la ACEPTACION no puede cortar un arrastre que todavia esta ganando terreno.
    private bool  m_Acercando;
    private int   m_NoProgressTicks;
    private int   m_CarryTicks;
    private bool  m_GaveUp;
    private bool  m_LogFrenoParado;
    // A que distancia se clavo cuando se rindio. Sin esto el reporte solo puede decir
    // QUE se rindio, y el que lee no sabe si fallo por 7 mm o por 40 m.
    private float m_GaveUpDistM;
    // Instrumentacion del tramo final: que decidio el perfil, con que numeros.
    private float m_LogD;
    private float m_LogV;
    private float m_LogAReq;
    private float m_LogAFree;
    private bool  m_LogFrenoRuido;
    private int   m_LogRama = -1;
    // B: el CARRY arranco en cuesta (ver ArranqueEnCuesta); y lo que se loguea de A y B
    private bool  m_CarryCuesta;
    private float m_LogGan;
    private string m_LogGanFuente;
    private float m_LogThrHold;
    private bool  m_LogSostiene;
    private float m_CarryIniDist;     // B: la distancia al arrancar en cuesta (la exencion caduca a CUESTA_RETRO_MAX_M de ahi)
    private float m_UltDist;          // la distancia 2D del ultimo Actuate
    private int   m_RetroCuadros;     // B: cuadros en que el controlador freno por retroceso desde el ultimo tick...
    private float m_RetroM;           // ...y cuanto retrocedio en ellos
    private int   m_LogRetroCuadros;
    private float m_LogRetroM;
    private bool  m_ArEnCurso;        // E: la escalera del AR tiene el vehiculo: las reglas 2, 3 y 4 del cierre no clavan (ver ShouldLatch)

    void BZEndpointSlam()
    {
        Reset();
    }

    void Reset()
    {
        m_Latched = false;
        m_MinDist = 1000000.0;
        m_Acercando = false;
        m_NoProgressTicks = 0;
        m_CarryTicks = 0;
        m_GaveUp = false;
        m_GaveUpDistM = 0;
        m_CarryCuesta = false;
        m_LogGan = 0;
        m_LogGanFuente = "";
        m_LogThrHold = 0;
        m_LogSostiene = false;
        m_CarryIniDist = 0;
        m_UltDist = 0;
        m_RetroCuadros = 0;
        m_RetroM = 0;
        m_LogRetroCuadros = 0;
        m_LogRetroM = 0;
        m_ArEnCurso = false;
    }

    // E: el controlador lo pone cada tick de Plan con lo que sabe del AR (escalera en curso, F1 activa, re-ubicacion pendiente)
    void SetArEnCurso(bool x) { m_ArEnCurso = x; }

    // B: el vehiculo esta arrancando en cuesta (CARRY con pendiente, primeros CUESTA_TICKS, sin haber retrocedido CUESTA_RETRO_MAX_M):
    // el retroceso no es "la pose paso", y el controlador lo frena cuadro a cuadro
    bool ArranqueEnCuesta()
    {
        if (!s_CandB) return false;
        if (!m_CarryCuesta) return false;
        if (m_UltDist > m_CarryIniDist + CUESTA_RETRO_MAX_M) return false;
        return m_CarryTicks <= CUESTA_TICKS;
    }

    // B: en cuesta y por debajo del creep (parado o casi), antes o durante el CARRY: el controlador no deja rodar hacia atras. Sin esto,
    // entre la parada de la rama de frenada y el primer tick del CARRY pasan hasta 0,5 s con el freno suelto y el casco rueda
    bool CuestaParado(BZVehicleState st, BZVehicleManual m)
    {
        if (!s_CandB || m_Latched || !m) return false;
        if (9.81 * Math.Sin(st.pitch) <= CUESTA_MIN_MS2) return false;
        return st.chassisKmh < m.creepKmh * STOPPED_FRAC;
    }

    // B: el controlador vio retroceder el casco en un cuadro y freno (se loguea en el tick siguiente)
    void NotarRetroceso(float dM)
    {
        m_RetroCuadros = m_RetroCuadros + 1;
        m_RetroM = m_RetroM + dM;
    }

    bool  IsLatched()   { return m_Latched; }
    // Clavo la pose, o se rindio aceptando lo alcanzado. Las dos cosas cierran, pero NO
    // significan lo mismo y el reporte no puede confundirlas.
    bool  GaveUp()      { return m_GaveUp; }
    float GaveUpDistM() { return m_GaveUpDistM; }

    // Por que el tramo final hizo lo que hizo. Vacio si el slam todavia no actuo este tick.
    string ReporteLongitudinal()
    {
        if (m_LogRama < 0) return "";
        string r = "rama ";
        if (m_LogRama == 0) r = r + "LLEGO(freno)";
        else if (m_LogRama == 1) r = r + "frenada";
        else if (m_LogRama == 2) r = r + "CARRY(empuje)";
        else if (m_LogRama == 4) r = r + "SOSTIENE";
        else r = r + "coast";
        r = r + " · d " + m_LogD.ToString() + " m";
        r = r + " · v " + m_LogV.ToString() + " m/s";
        r = r + " · aNeed " + m_LogAReq.ToString();
        r = r + " · aFree " + m_LogAFree.ToString();
        if (m_LogRama == 1) r = r + " · ganancia " + m_LogGan.ToString() + " (" + m_LogGanFuente + ")";
        if (m_LogRama == 2)
        {
            r = r + " · gas " + m_LogThrHold.ToString() + " · cuesta " + m_CarryCuesta.ToString();
            r = r + " · freno sostenido " + m_LogRetroCuadros.ToString() + " cuadro(s) por retroceso de " + (m_LogRetroM * 1000.0).ToString() + " mm";
            if (m_LogRetroCuadros > 0) r = r + " (se suelta en este tick, con mas gas)";
        }
        r = r + " · carry " + m_CarryTicks.ToString();
        r = r + " · minDist " + m_MinDist.ToString();
        r = r + " · sinProgreso " + m_NoProgressTicks.ToString();
        if (m_LogFrenoRuido) r = r + " · FRENO-RUIDO omitido";
        if (m_LogFrenoParado) r = r + " · FRENO-PARADO omitido";
        return r;
    }
    float GetMinDistM() { return m_MinDist; }
    int   GetNoProgressTicks() { return m_NoProgressTicks; }

    void Actuate(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, BZDriveOutput o)
    {
        o.Reset();
        o.mode = BZDriveMode.BZDM_ENDPOINT;
        if (!leg || !m || !leg.valid)
        {
            o.SetHalt();
            return;
        }
        o.gear = 2;
        if (leg.isReverse) o.gear = 0;

        float signed = leg.signedToEndM;
        // ⚠ LA DISTANCIA AL PUNTO NO ES LA DISTANCIA A LO LARGO DE LA TRAZA.
        // Esto media |signedToEndM| -- solo la componente ALONG -- y por eso el slam era
        // CIEGO al lateral: mientras el vehiculo crawleaba hacia adelante derivando de
        // costado, `dist` seguia bajando, el contador de sin-avance se reseteaba solo y el
        // slam creia estar acercandose. El numero que se REPORTA siempre fue el 2D
        // (TOTAL = hypot(along, lateral)), asi que el modulo estaba optimizando una
        // metrica distinta de la que rinde cuentas.
        // Medido en AERO: llego a 0.11 m del punto y cerro en 1.51 m, con lateral -1.48.
        float lat = 0;
        if (leg.valid) lat = leg.crossTrackM;
        float dist = Math.Sqrt(signed * signed + lat * lat);
        m_UltDist = dist;
        m_LogRetroCuadros = m_RetroCuadros;   // lo que el controlador freno desde el tick anterior, para la linea SLAM de este
        m_LogRetroM = m_RetroM;
        m_RetroCuadros = 0;
        m_RetroM = 0;
        UpdateProgress(st, m, dist);

        if (m_Latched)
        {
            Hold(o);
            return;
        }
        if (ShouldLatch(st, m, signed, dist))
        {
            m_Latched = true;
            if (dist > BZDriveTol.ACEPTA_ENDPOINT_M)
            {
                m_GaveUp = true;
                m_GaveUpDistM = dist;
            }
            Hold(o);
            return;
        }
        o.steering = AlignSteer(st, leg, m);
        Longitudinal(st, leg, m, surfKind, signed, o);
        o.Clamp();
    }

    // ======================= cierre =======================

    // Lleva la cuenta de si el vehiculo se esta ACERCANDO. Detenido y sin acercarse es
    // la senal de que no va a mejorar mas.
    private void UpdateProgress(BZVehicleState st, BZVehicleManual m, float dist)
    {
        if (dist < m_MinDist - PROGRESS_EPS_M)
        {
            m_MinDist = dist;
            m_NoProgressTicks = 0;
            m_Acercando = true;
            return;
        }
        m_Acercando = false;
        if (dist < m_MinDist) m_MinDist = dist;
        // Misma correccion que en el MC: MOVERSE NO ES AVANZAR. Un tick por encima del
        // umbral de velocidad no puede borrar la cuenta entera, o un vehiculo que tironea
        // sin acercarse no clava nunca. Drena de a uno.
        if (st.chassisKmh < StoppedKmh(m)) m_NoProgressTicks++;
        else if (m_NoProgressTicks > 0)    m_NoProgressTicks--;
    }

    private float StoppedKmh(BZVehicleManual m)
    {
        float k = m.creepKmh * STOPPED_FRAC;
        if (k < 0.3) k = 0.3;
        return k;
    }

    // ⚠ BUG DEL RUN 9: esto exigia dist <= 0.10 Y detenido. Boris se detuvo a 0.164 m --
    // mejor que el run 8, pero fuera de tolerancia -- y ahi NINGUNA condicion podia
    // dispararse nunca mas: ni el latch fino ni el de sobrepaso. Deadlock, y el slam
    // quedo mirando el punto a 16 cm para siempre.
    //
    // Es la MISMA clase estructural que el Closed() de MC (run 4) y que la mordida del
    // hill-start (run 6): una condicion que puede volverse INALCANZABLE, sin salida.
    //
    // La tolerancia define lo EXCELENTE, no lo unico aceptable. Un vehiculo detenido que
    // ya no se acerca LLEGO: se acepta la pose alcanzada y se REPORTA el numero real.
    // Mejor 16 cm medidos y honestos que un deadlock esperando 10.
    // ⚠⚠⚠ LA PRIMERA RAMA ACEPTABA MIENTRAS EL VEHICULO TODAVIA SE ACERCABA -- UN SORTEO.
    // Medido en el Matador con envelope real (n=9): residuo BIMODAL, 5 corridas en -0.103 y
    // 4 en -0.202, separadas por 10 cm SIN NADA EN EL MEDIO. Mecanismo: esta funcion corre
    // en Plan (2 Hz) y el arrastre final recorre 0.118-0.124 m por tick, asi que
    // `dist <= 0.25` se evalua contra una posicion que salta de a 12 cm:
    //    el tick ANTERIOR cae siempre ARRIBA de 0.25 (0.257-0.407) -> no acepta
    //    el SIGUIENTE cae abajo (0.125-0.241) -> acepta AHI, y ahi queda
    // La distancia final es entonces "donde cayo el primer tick por debajo de 0.25", que se
    // reparte uniformemente sobre lo que el arrastre recorre en un tick (0.13-0.18 m).
    // Medido n=9: finales 0.118 a 0.232 -- un tick entero de dispersion.
    // `Hold` frena a FONDO, asi que aceptar es irreversible: corta un arrastre que estaba
    // funcionando.
    //
    // EL BANCO REPRODUCE 9/9 dentro de 15 mm: la rama latchea en el primer tick de Plan con
    // dist <= 0.25 y v < StoppedKmh, y el vehiculo recorre 4-11 mm mas mientras frena.
    // Mutantes: ACEPTA=0.10 -> 0/9 muerto · ACEPTA=0.50 -> 0/9 muerto.
    //
    // ✖ HIPOTESIS DESCARTADA POR SU PROPIO MUTANTE, y queda anotada para no repetirla:
    // se penso que el creep del PRIOR (2.774 -> StoppedKmh 1.387) vetaba por casualidad la
    // aceptacion al quedar apenas por encima del arrastre, y que medir el vehiculo sacaba
    // esa proteccion accidental. El mutante `StoppedKmh = 1.387` REPRODUCE 9/9 IGUAL: en
    // los ticks que deciden el vehiculo va a 0.47-1.02 km/h, por debajo de LOS DOS
    // umbrales, asi que la condicion de velocidad NO es el discriminante. Lo que el
    // envelope cambio en el endgame quedo SIN establecer -- lo unico probado es la
    // cuantizacion por muestreo.
    //
    // La condicion que faltaba YA EXISTE en las otras ramas ("detenido y sin acercarse es
    // lo mejor que va a conseguir"): no se agrega umbral ni perilla, se deja de
    // cortocircuitarla. ✖ NO se toca ACEPTA_ENDPOINT_M: la tolerancia sigue definiendo lo
    // EXCELENTE; lo que deja de ser es tambien una orden de detenerse.
    // TERMINACION garantizada por las otras tres ramas: sobrepaso, alejarse mas de una
    // tolerancia del mejor acercamiento, y los 6 ticks sin avance (3 s).
    private bool ShouldLatch(BZVehicleState st, BZVehicleManual m, float signed, float dist)
    {
        // aceptacion del endpoint (no el bar estricto): latchear ACA corta los pasitos
        // ⚠ ...pero SOLO si ya no se esta acercando: ver la cicatriz sobre ShouldLatch.
        if (dist <= BZDriveTol.ACEPTA_ENDPOINT_M && st.chassisKmh < StoppedKmh(m) && !m_Acercando) return true;
        // E: con la escalera del AR en curso (solo se abre en el cierre con un contacto reciente), el que decide es el AR: un vehiculo
        // EMPUJADO pasa de largo (regla 2), se aleja de su mejor acercamiento (regla 3) o queda quieto mientras lo re-ubican (regla 4), y
        // ninguna de las tres es "llego". Medido: el V140 empujado 20 m por el RX8 quedo clavado por la regla 2 un tick despues del choque.
        if (m_ArEnCurso) return false;
        // paso de largo: clavar en vez de volver a buscarlo (eso es el vaiven)
        if (signed < -(OVERSHOOT_TOL_MULT * BZDriveTol.LLEGADA_M)) return true;
        // ⚠ EL CONTADOR DE SIN-AVANCE NO ALCANZA PARA ESTE CASO, aun con la distancia 2D:
        // solo suma con el vehiculo DETENIDO (por debajo de creep*0.5), y aca el vehiculo se
        // esta MOVIENDO mientras se aleja -- crawlea a 0.3-0.4 km/h con el lateral abriendose.
        // Cuando el contador por fin junta sus 6 ticks ya perdio mas de un metro.
        //
        // Perder mas de una TOLERANCIA respecto del mejor acercamiento ya alcanzado es la
        // senal de que la mejor pose PASO. No hay nada que recuperar crawleando: se clava
        // ahi y se reporta el numero real, que es la misma politica que el comentario de
        // arriba (la tolerancia define lo excelente, no lo unico aceptable).
        if (dist > m_MinDist + BZDriveTol.LLEGADA_M && !ArranqueEnCuesta()) return true;   // B: arrancando en cuesta, el retroceso no clava
        // detenido y sin acercarse: esto es lo mejor que va a conseguir
        if (m_NoProgressTicks > NO_PROGRESS_TICKS) return true;
        return false;
    }

    private void Hold(BZDriveOutput o)
    {
        o.throttle = 0;
        o.steering = 0;
        o.brake = 1.0;
    }

    // ======================= direccion (POSE) =======================

    // Alinea el RUMBO con el GRABADO en el punto final -- no con la tangente de la traza
    // (en reversa difieren ~180 grados). El signo sale de la convencion, no de un config:
    //   rumbo = Atan2(x,z) crece en sentido HORARIO  =>  hErr>0 significa "el rumbo
    //   deseado esta a la DERECHA"  =>  yendo ADELANTE se corrige girando a la derecha
    //   (volante positivo). En REVERSA el mismo volante rota el auto al reves -> se invierte.
    // v1.1 tenia esto como ManeuverSteerSign/EndpointHeadingSteerK tuneados a mano porque
    // la convencion no estaba escrita en ningun lado.
    // ★★ MISMO CAMBIO QUE EN EL MC: se pide una CURVATURA, no se multiplica por una
    // ganancia. `hErr * steerKPose` trataba el error de rumbo como si fuera el angulo de
    // rueda deseado; con el steerMax real (30.5 grados en vez del artefacto de 75) eso
    // quedaba 2.5x mas agresivo.
    private float AlignSteer(BZVehicleState st, BZLegView leg, BZVehicleManual m)
    {
        return AlignSteerCon(st, leg, m, leg.signedToEndM, leg.crossTrackM);
    }

    // el mismo volante con el remanente y el lateral que le den: a 2 Hz los del leg (AlignSteer); con D, el controlador lo llama a 40 Hz
    // con los del cuadro (RemanenteFrameM, LateralFrameM: mismo marco y mismo signo)
    float AlignSteerCon(BZVehicleState st, BZLegView leg, BZVehicleManual m, float dSigned, float latM)
    {
        float rec = RecordedHeadingAtEnd(leg);
        if (rec < -900.0) return 0;
        float hErr = BZMath.WrapPi(rec - st.yaw);
        float wb = m.wheelbaseM;
        if (wb < 0.5) wb = 2.5;
        float d = dSigned;
        if (d < 0) d = -d;
        float lo = ALIGN_L_WB * wb;
        if (d < lo) d = lo;
        // ⚠⚠ UN TOPE ABSOLUTO NO PUEDE ANULAR UN PISO RELATIVO.
        // El piso (ALIGN_L_WB * wb) escala con el vehiculo; el tope no. Cuando
        // ALIGN_L_WB * wb supera el tope, el tope GANA y el piso queda anulado -- y ese
        // piso existe porque abajo de el se pide un radio que el vehiculo no puede dar.
        // Medido: el ExpansionBus (batalla 5.35) deberia usar 10.70 y usaba 8.0.
        // La condicion muerde con batalla > ALIGN_L_MAX/ALIGN_L_WB.
        if (d > ALIGN_L_MAX && ALIGN_L_MAX > lo) d = ALIGN_L_MAX;
        float kPose = hErr / d;
        // persecucion del punto final. crossTrackM + = vehiculo a la IZQUIERDA de la linea (misma
        // convencion que el lateral de la polilinea) -> curvatura + = giro a la DERECHA: lo mismo que hErr.
        //
        // TAMBIEN EN REVERSA. Esto vivia dentro de un `if (!leg.isReverse)` por precaucion ("en
        // reversa la geometria del signo cambia"). La precaucion era infundada, y los tres signos ya estaban bien:
        //   · `crossTrackM` (BZLegMachine.CrossTrack) se mide contra la tangente del segmento EN EL SENTIDO DE
        //     AVANCE DE LA RUTA -- que en un leg de reversa es el sentido en que el vehiculo SE MUEVE. O sea que
        //     ya viene en el MARCO DE MARCHA, no en el del morro.
        //   · `hErr = rec - yaw` (morro grabado contra morro real) es invariante al marco: girar los dos 180 no
        //     cambia la resta.
        //   · la cinematica de la reversa la resuelve la linea que ya estaba abajo: `if (leg.isReverse) s = -s`.
        // Caso concreto que lo cierra: ruta al norte, morro al sur, vehiculo 1 m al ESTE -> crossTrack = -1
        // (derecha de la marcha) -> kLat < 0 (izquierda de la marcha) -> volante izquierda -> negado -> DERECHA,
        // que es lo que manda la cola (el extremo que va adelante en reversa) hacia el oeste, sobre la linea.
        // Medido: PATRULLA termina de reversa y el desvio de los 23 m de marcha atras llegaba entero al punto
        // final (latFr -0,12 a -0,29 en seis corridas, tres INCOMPLETE por 3-6 cm). Banco sobre las seis:
        // -0,12/-0,29 -> -0,04/-0,08, las seis dentro de 0,25. Mutante con el signo invertido: 5 de 6 FALLA
        // (duplica el error) -- es la prueba de que el signo es este y no el otro.
        float dRawL = dSigned;
        if (dRawL < 0) dRawL = -dRawL;
        float dL = dRawL;
        float pisoL = LAT_L_WB * wb;
        if (dL < pisoL) dL = pisoL;
        float kLat = 2.0 * latM / (dL * dL);
        float kMaxL = LAT_K_MAX;
        if (s_CandD) kMaxL = LAT_K_MAX_D;
        kLat = Math.Clamp(kLat, -kMaxL, kMaxL);
        float fadeL = (dRawL - LAT_FADE_FIN_M) / (LAT_FADE_INI_M - LAT_FADE_FIN_M);
        // D: ademas del fade por distancia, el de VELOCIDAD, y manda el mayor: a 1 m del punto a 7 km/h el modelo de bicicleta esta bien
        // condicionado y el termino lateral sigue; a 1 m a 3 km/h (el que llega frenando fuerte) queda el de distancia, como siempre
        if (s_CandD)
        {
            float fadeV = (st.chassisKmh / 3.6 - LAT_V_FADE_FIN) / (LAT_V_FADE_INI - LAT_V_FADE_FIN);
            if (fadeV > fadeL) fadeL = fadeV;
        }
        fadeL = Math.Clamp(fadeL, 0.0, 1.0);
        kPose = kPose + kLat * fadeL;
        float s = m.SteerCommandForCurvature(kPose, st.chassisKmh);
        if (leg.isReverse) s = -s;
        return Math.Clamp(s, -1.0, 1.0);
    }

    private float RecordedHeadingAtEnd(BZLegView leg)
    {
        BZPathSample smp = new BZPathSample();
        if (!leg.Sample(leg.legHi, smp)) return -999.0;
        return smp.recHeadingRad;
    }

    // ======================= longitudinal =======================

    // CARRY: si falta y esta parado, empuja con el piso de throttle DERIVADO del Manual
    // (masa + torque + radio). SLAM: si viene con velocidad, frena con la decel que el
    // vehiculo REALMENTE tiene en ESE piso -- no con una constante horneada como el
    // EndpointBrakeDecel de v1.1.
    // ⚠⚠⚠ LA TOLERANCIA NO ES EL OBJETIVO. Esto decia `d = signed - LLEGADA_M`, o sea que
    // el slam apuntaba a parar 10 cm ANTES del waypoint --el borde de lo aceptable-- y
    // frenaba A FONDO apenas quedaban 11 cm. Los tres endpoints medidos de PENDIENTE
    // (0.1048, 0.1070, 0.1085) caen TODOS adentro de esa banda: no es dispersion, es el
    // objetivo. Apuntar al limite de aceptacion garantiza quedar afuera con cualquier ruido.
    //
    // El modelo reproduce el tick medido a tres digitos: con signed 0.13 y 0.61 km/h,
    // aReq = v^2/(2*(0.13-0.10)) = 0.4785 y brk = 0.4785/8.8 = 0.054 -- exactamente lo que
    // logueo la corrida 23:33.
    //
    // Y hay un efecto colateral: con el resto atras, la rama de CARRY --que existe justo
    // para arrimar los ultimos centimetros-- era INALCANZABLE debajo de 11 cm, porque el
    // freno a fondo retornaba antes. Sacar el sesgo la habilita sin agregar nada.
    //
    // La tolerancia vuelve a hacer solo lo que hace una tolerancia: decidir si se LLEGO.
    //
    // ✔ RESTAURADO. La reversion corrio como experimento y wp 724 fallo IGUAL
    // con esta linea como estaba: el fix del endpoint quedo exonerado. Ademas revertido el
    // endpoint era erratico -- 10.57 cm y 71.67 cm en dos corridas por lo demas identicas.
    private void Longitudinal(BZVehicleState st, BZLegView leg, BZVehicleManual m, int surfKind, float signed, BZDriveOutput o)
    {
        float v = st.chassisKmh / 3.6;
        float d = signed;
        m_LogD = d;
        m_LogV = v;
        if (d <= LLEGADA_EPS_M)
        {
            o.throttle = 0;
            o.brake = 1.0;
            m_LogRama = 0;
            m_LogAReq = 0;
            return;
        }
        // A: el pedal se dosifica contra la ganancia del pedal (la fisica del config, acotada por el tope), no contra el tope. Con A apagado,
        // GananciaPedal devuelve el tope de siempre (MaxBrakeDecelEndpoint, techado por la frenada medida en la toma).
        float gan = m.GananciaPedal(surfKind);
        m_LogGan = gan;
        m_LogGanFuente = m.GananciaPedalFuente(surfKind);
        // ⚠⚠ EL FRENO NO ES LO UNICO QUE FRENA, Y ESTE ERA EL UNICO MODULO QUE LO IGNORABA.
        //
        // Esto pedia freno por `v^2/(2d)` a secas, sin descontar lo que el vehiculo hace SOLO.
        // BZManeuverController.Pedals lo hace bien desde siempre (`aNeed = aWant - aFree`); el
        // slam no. Resultado medido: el auto se DETIENE 10.5 cm antes del waypoint --el freno
        // comandado se suma al planeo en vez de completarlo-- y ahi, ya por debajo de la
        // mordida, el CARRY intenta gatear y boga.
        //
        // Y el planeo no es chico ni conocido a ojo: el propio CoastDecel documenta que el
        // prior analitico se quedaba 4x corto contra lo medido. Ignorarlo es frenar de mas
        // por un factor que ya esta medido en este mismo vehiculo.
        //
        // aWant es NEGATIVO (hay que desacelerar). aFree tambien lo es al planear. aNeed es lo
        // que falta pedirle a los pedales: si es negativo hace falta freno; si es POSITIVO el
        // planeo ya frena de mas y hay que SOSTENER para llegar al punto en vez de pararse antes.
        float aWant = -(v * v) / (2.0 * d);
        float aFree = m.FreeAccel(st.chassisKmh, st.pitch, surfKind);
        float aNeed = aWant - aFree;
        m_LogAReq = aNeed;
        m_LogAFree = aFree;
        // ⚠⚠⚠ UN FRENO MAS CHICO QUE EL ERROR DEL ARRASTRE ES RUIDO APLICADO COMO CONTROL.
        //
        // Medido, corrida 11:19, los dos ticks que decidieron el endpoint:
        //   wp 880  v 0.570 m/s  thr 0.36 (CARRY)   -> el vehiculo SE SOSTIENE (aReal -0.012)
        //   wp 880b v 0.564 m/s  thr 0 brk 0.008    -> se MUERE (aReal -0.608), a 38 cm del punto
        // O sea que a 2 km/h el gas SI mueve el vehiculo; el modulo lo solto y pidio un freno de
        // 0.073 m/s2. El vehiculo no tenia que parar ahi: se lo paro.
        //
        // Y ese freno se calculo contra un arrastre de 0.350 cuyo error MEDIDO en este regimen es
        // de +0.15 a +0.26 -- el termino de correccion es tres veces mas chico que la barra de
        // error del termino que corrige. Debajo de la mordida el arrastre no se puede medir mejor:
        // el banco de planeo exige gas Y freno en cero, y aca el modulo actua todos los ticks
        // (3 muestras aceptadas de 7551 en la corrida entera). Es una cota inferior estructural.
        //
        // Los dos errores NO cuestan lo mismo, que es el mismo criterio que la asimetria de tau
        // en BZManeuverController: pararse corto debajo de la mordida es IRREVERSIBLE (medido
        // cinco veces, el vehiculo ya no arranca), pasarse es recuperable y ademas VISIBLE --
        // la clausula de distancia lo reporta con el `along` en negativo.
        bool frenoDeRuido = false;
        if (m)
        {
            float mord = m.SpeedAtBiteKmh(st.chassisKmh, st.rpm, st.gear);
            float errArrastre = aFree;
            if (errArrastre < 0) errArrastre = -errArrastre;
            if (st.chassisKmh < mord && (-aNeed) < errArrastre * FRENO_MIN_FRAC) frenoDeRuido = true;
        }
        // Ver DETENIDO_MS: quieto y corto, el freno solo impide llegar.
        bool detenidoYCorto = (v < DETENIDO_MS);
        if (detenidoYCorto) m_LogFrenoParado = true;
        if (aNeed < -0.05 && !frenoDeRuido && !detenidoYCorto)
        {
            o.throttle = 0;
            o.brake = Math.Clamp((-aNeed) / gan, 0.0, 1.0);
            m_LogRama = 1;
            return;
        }
        if (frenoDeRuido) m_LogFrenoRuido = true;
        // Muy lento y todavia falta: arrimarse al punto. El empuje RAMPEA -- un valor fijo
        // que no alcanza para los ultimos centimetros no los alcanza nunca (mismo patron
        // que el despegue de MC en el run 5 y el hill-start en el run 6).
        float creepMs = m.creepKmh / 3.6;
        if (v < creepMs * STOPPED_FRAC)
        {
            float thr = m.launchThrottleFloor + m_CarryTicks * CARRY_RAMP_STEP;
            // B: en subida el gas es A FONDO desde el primer tick, mientras queda inercia (de parado, en la sub-mordida, ni a fondo arranca;
            // una rampa desde el piso lo dejaba parado). Sin aceleracion maxima de por medio. El freno no se sostiene aca: lo pone el
            // controlador, cuadro a cuadro, apenas el casco retrocede (EndpointFrameCuesta), y lo suelta este tick. En llano, lo de siempre.
            bool cuestaAhora = false;
            if (s_CandB && aFree < 0)
            {
                float aSlope = 9.81 * Math.Sin(st.pitch);
                if (aSlope > CUESTA_MIN_MS2)
                {
                    cuestaAhora = true;
                    thr = CARRY_MAX;
                    if (m_CarryTicks == 0) m_CarryIniDist = m_UltDist;
                }
            }
            m_CarryCuesta = cuestaAhora;   // se re-evalua cada tick del CARRY: un CARRY en llano no hereda la cuesta de uno anterior
            if (thr > CARRY_MAX) thr = CARRY_MAX;
            m_CarryTicks++;
            o.throttle = thr;
            o.brake = 0;
            m_LogThrHold = thr;
            m_LogSostiene = (m_LogRetroCuadros > 0);
            m_LogRama = 2;
            return;
        }
        m_CarryTicks = 0;
        m_CarryCuesta = false;
        if (aNeed > 0.02)
        {
            // El planeo frena mas de lo que hace falta: sin esto el vehiculo llega a cero ANTES
            // del punto. Se sostiene con el modelo de par, igual que la maniobra.
            o.throttle = m.ThrottleForAccel(aNeed, st.chassisKmh, st.rpm, o.gear);
            o.brake = 0;
            m_LogRama = 4;
            return;
        }
        o.throttle = 0;
        o.brake = 0;
        m_LogRama = 3;
    }
}
