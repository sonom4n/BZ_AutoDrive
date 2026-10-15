// ============================================================================
//  BZConductorMoto - el conductor de la MOTO sobre la cinta (DayZ 1.30). Es la variante 6 del laboratorio de motos (herramienta aparte), validada
//  en el server Exp el 27/09 (Moped: fin de la toma, desvio max 1,0 m; Enduro: 1.146 de 1.479 m), puesta sobre la cinta
//  de una ruta del framework en lugar de la toma cruda del laboratorio de motos (herramienta aparte).
//
//  POR QUE NO EL CEREBRO DEL AUTO: el controlador 1.2 supone reversa (marcha 0), la maniobra F1 marcha atras, cuatro
//  ruedas y la planta del auto; en la moto la direccion se vuelve inclinacion por encima de ~5 km/h y la ley tiene que
//  pedir TASA DE GIRO (DISENO_MOTOS_130 SS7). Lo que se reusa del framework: la carga de la ruta y la cinta, el spawn,
//  el chofer, las pausas y el fin de ruta (BZBusService.TickMoto / SampleMoto).
//
//  Cada cuadro (OnInput del server, ~40 Hz):
//   - PROYECCION: BZCintaRunner.Muestra (arco, desvio lateral firmado, rumbo grabado, velocidad/gas/freno/marcha
//     grabados, steering grabado si la cinta lo trae, mesetas por tiempo).
//   - LEY LATERAL (variante 10 del laboratorio de motos, herramienta aparte): delta = FF - (e_psi + atan(KV*e_y/v) + TD*(r_moto - r_toma)) / (AUT(v)*TAU).
//     El termino TD es un AMORTIGUAMIENTO sobre la tasa de e_psi: la moto zigzagueaba a ~25 km/h (ciclo de 0,8 s, la
//     planta responde con 0,20 s de retardo, corr en el tope el 20 %); en el laboratorio de motos (herramienta aparte) gano (Blue_1 p90 0,61 -> 0,38).
//     r_moto = tasa de giro del cuerpo; r_toma = curvatura grabada x velocidad de la moto. FF = el steering grabado. AUT = la autoridad
//     de direccion de ESTA clase por franja de velocidad, del envelope (kind aut, envelope_emit.py); sin envelope, 0,6.
//     Tope de la correccion, limitador de tasa y guarda de inclinacion (maxLeanAngle[] del config).
//   - VELOCIDAD OBJETIVO: el gas y el freno se CONTROLAN hacia la kmh grabada en ese arco (con anticipacion de frenada
//     por la frenada p95 del envelope); si la moto va mas lenta que la toma, el freno grabado no se aplica; con una rueda
//     en el aire van los pedales grabados; con el roll en el tope baja la referencia.
//   - MARCHAS: la grabada, salvo rpm en el piso del embrague con la referencia mas rapida (baja una); nunca pasar el
//     corte (rpm por km/h de cada marcha, del envelope). Insistida hasta que GetGear() la devuelva (el primer ShiftTo
//     despues de arrancar se pierde; el motor la devuelve a N al terminar de subir el chofer).
//   - PATA arriba mientras maneja. CAIDA: se levanta con PickUp a los 2 s; el servicio re-sienta al chofer.
//   - SIN REVERSA: una moto trabada (el arco no avanza 6 s con la toma en movimiento) se RE-UBICA 4 m mas adelante sobre
//     la cinta (hasta 5 veces); despues la ruta termina INCOMPLETE.
//   - FIN: a menos de 3 m del final de la cinta y quieta (o pasado el final).
//  La cinta de una moto la escribe frame_to_route.py con la marcha corrida en uno (N = 1, 1ra = 2: la convencion del auto,
//  asi ninguna fila se lee como reversa) y dos columnas mas: 12 steering y 13 corte (tramos no reproducibles).
// ============================================================================
class BZConductorMoto {

    // ---- ley lateral (variante 6 del laboratorio de motos, herramienta aparte)
    static const float AUT_DEF = 0.6;         // rad/s por unidad de steering sin envelope (Moped 0,54-0,79; Enduro 0,80)
    static const float AUT_R_MIN = 0.7;       // correlacion minima para usar la autoridad medida de una franja
    static const int   AUT_MIN_N = 40;
    static const float TAU = 0.5;             // s (ganancia del lazo 1/TAU = 2, la que funciono en la Moped)
    static const float TD = 0.10;             // s: amortiguamiento sobre la tasa de e_psi (variante 10 del laboratorio de motos, herramienta aparte)
    static const float CURV_MITAD_M = 0.5;    // m: la curvatura grabada se mide en s +- esto
    static const float KV = 1.0;
    static const float RATE = 4.0;            // unidades de steering por segundo
    static const float CORR_MAX = 0.6;
    static const float GUARDA = 0.7;          // fraccion del maxLeanAngle desde la que la correccion se apaga
    static const int   SAT_AVISO = 40;
    // ---- LA CASCADA POR INCLINACION (a velocidad). La moto convierte la direccion en inclinacion con 0,4-0,7 s de
    //      retardo (medido en la caja a 30-130 km/h); la ley directa de arriba (ganancia 1/TAU = 2) cruza a 2 rad/s,
    //      donde ese retardo deja 3-28 grados de margen de fase, y OSCILA (medido a 85-100 km/h: roll -21 -> -4 -> +24
    //      con la toma en -16; lateral 3,4 m). Desde CASC_DE_KMH se mezcla (entera desde CASC_A_KMH) con esta cascada:
    //      el error de rumbo y lateral pide un ROLL OBJETIVO relativo al que exige la curvatura grabada CASC_LA_S
    //      segundos mas adelante (la anticipacion compensa el retardo; la ventana CASC_W_M alisa los toques de teclado
    //      del humano), topeado en CASC_MARGEN del maxLeanAngle; y la direccion sigue a ese roll: el feedforward
    //      cinematico (la guinada de ese roll, por la autoridad medida) + P sobre el error de roll + D sobre su tasa,
    //      en unidades de direccion (S_loc = grados de roll por unidad de direccion, senal chica). Elegido en el banco
    //      offline sobre la cinta real con la planta ajustada a la caja: 0,37-0,42 m con plantas de 0,35 a 1,0 s de
    //      retardo (la ley directa: 3,2 nominal y 7-29 con plantas lentas; subir TAU con la velocidad no alcanza).
    //      El roll GRABADO no sirve de base: en la toma la guinada que implica es el 25-70 % de la geometrica (el
    //      humano inclina de mas con los toques); queda como referencia de la guarda de arriba
    //      La MEZCLA: la ley directa anda bien (medido en 5 corridas) en todo menos la curva larga de 85-100 km/h con
    //      inclinacion grande; la cascada anda en esa curva y a 98-102 (0,24-0,39) pero a media velocidad OSCILA (26-53
    //      km/h, e_psi +-20) y queda apenas sobre 0,6 a 59-85: ahi el roll es un mal sustituto de la guinada (retardo
    //      0,68 s medido) y el banco no reproduce esas corridas. Cada ley donde esta probada: directa entera hasta
    //      CASC_DE_KMH, cascada entera desde CASC_A_KMH
    static const float CASC_DE_KMH = 65.0;
    static const float CASC_A_KMH = 85.0;
    //      El lazo externo (TAU_R, KV): con 1,3 s y 1,5 corregia un error de rumbo 2,6 veces mas despacio que la ley
    //      directa y con poco amortiguamiento (zeta 0,36): en los tramos de curvatura baja una desviacion de medio metro
    //      se paseaba 40 m y llegaba a 0,8-1,2 (medido). Con 0,7 s y 1,0 (zeta 0,6, el doble de rapido) el banco da
    //      0,23-0,34 en toda la ruta con todas las plantas; una integral del lateral empeora (1-2 m): no va
    static const float CASC_TAU_R = 0.7;      // s: el lazo externo (rumbo + lateral -> roll)
    static const float CASC_KV = 1.0;         // el peso del lateral en el rumbo equivalente
    static const float CASC_KP = 3.0;         // P sobre el error de roll (en S_loc)
    static const float CASC_KD = 0.3;         // D sobre la tasa de roll (en S_loc, por grado/s)
    static const float CASC_LA_S = 0.3;       // s: la curvatura base se lee este tiempo mas adelante
    static const float CASC_W_M = 6.0;        // m: y en una ventana de +- esto
    static const float CASC_MARGEN = 0.85;    // del maxLeanAngle: tope del roll objetivo
    static const float CASC_TASA_TAU = 0.05;  // s: el filtro de la tasa de roll
    static const float CASC_G = 9.81;
    // ---- velocidad objetivo (variante 6 del laboratorio de motos, herramienta aparte)
    static const float VEL_KT = 0.10;
    static const float VEL_KB = 0.15;
    static const float VEL_BANDA = 1.5;
    static const float VEL_A_DEF = 6.0;
    static const float VEL_A_FRAC = 0.9;
    static const float VEL_PISO = 1.15;
    static const float VEL_LUG_KMH = 3.0;
    static const float VEL_LUG_S = 0.5;
    static const float VEL_CAMBIO_S = 1.5;
    static const float VEL_ALCANZA_KMH = 2.0;
    // la ARRANCADA desde cero: quieta, con la toma andando y marcha puesta, gas a fondo. Con 0,4 de gas en 1a la moto no
    // sale del reposo (rpm 1400 cargado, 0 km/h, el chofer sigue en la animacion de caminar); con 1,0 sale en 0,4 s. Pasa
    // al terminar una caminata un poco antes de su pose: el cursor cae dentro del tramo caminado (gas grabado 0, N)
    static const float ARRANQUE_KMH = 1.0;
    static const float ARRANQUE_VREF_KMH = 2.0;
    static const float ARRANQUE_THR = 1.0;
    static const float VEL_CORTE_FRAC = 0.95;
    static const float VEL_CURVA_KMH = 15.0;
    static const float VEL_CURVA_PISO_KMH = 15.0;   // la guarda no actua por debajo ni lleva la referencia por debajo
    static const float VEL_CURVA_FRENO = 0.3;       // freno maximo que AGREGA la guarda
    static const float QUIETO_KMH = 1.0;
    // PISO DE 1a: si la toma se mueve y va en N (caminando, o parada a punto de meter 1a) y la moto va a menos de esto, 1a:
    // en N la moto no avanza sola. N queda solo en un tramo caminado (empuje) o en una detencion de la toma. Rodando en N
    // mas rapido (la toma "planeando") queda en N
    static const float N_PISO_KMH = 10.0;
    // ---- marchas, fin, atasco, caida
    static const float CLUTCH_S = 0.4;
    static const float FIN_M = 3.0;
    static const float ATASCO_S = 6.0;
    static const float REUB_ADELANTE_M = 4.0;
    static const int   REUB_MAX = 5;
    static const float CAIDA_LEVANTAR_S = 2.0;
    static const int   LATIDO_CADA = 80;
    // ---- las guardas de inclinacion RELATIVAS A LA TOMA (la toma es el mapa sincero: lo que el humano inclino en ese
    //      arco se permite). Umbral = max(GUARDA x tope del config a esa velocidad, roll de la toma + GUARDA_MARGEN_DEG);
    //      el corte total, en el tope del config o GUARDA_ANCHO_MIN_DEG mas arriba del umbral. Sin la columna 14, el
    //      umbral es el de antes (GUARDA x tope). 27/09: la Moped amarilla inclino 14-15,5 grados a 29-31 km/h (0,85-0,9
    //      del tope) y las guardas en 0,7 frenaban y recortaban donde el humano no lo hizo.
    static const float GUARDA_MARGEN_DEG = 2.0;
    static const float GUARDA_ANCHO_MIN_DEG = 2.0;
    // ---- fuera de la linea: con |lat| > FUERA_EY_M o |e_psi| > FUERA_EPSI_DEG sostenido FUERA_DECLARA_S (y la toma en
    //      movimiento), se frena a 0 y se RE-UBICA: afuera de la linea la ley orbita alrededor del punto de referencia
    //      (laboratorio de motos, herramienta aparte, 27/09: una vuelta en ~5 s). La proyeccion se vuelve a buscar sola (el rescate ancho de la cinta).
    static const float FUERA_EY_M = 3.0;
    static const float FUERA_EPSI_DEG = 60.0;
    static const float FUERA_DECLARA_S = 2.0;
    static const float FUERA_FRENO = 0.4;
    // ---- choque de frente: un contacto delantero de hace menos de CHOQUE_VENTANA_S con la moto a menos de CHOQUE_KMH:
    //      gas 0 (la Enduro del laboratorio de motos, herramienta aparte, siguio con gas 1,0 contra el obstaculo) y re-ubicacion a los CHOQUE_DECLARA_S
    static const float CHOQUE_KMH = 3.0;
    static const float CHOQUE_VENTANA_S = 1.0;
    static const float CHOQUE_DECLARA_S = 1.0;
    // la firma de un choque es una CAIDA de velocidad (laboratorio de motos, herramienta aparte, 27/09: con la moto quieta, cualquier contacto delantero la
    // mataba en la arrancada): >= CHOQUE_RAPIDO_KMH en el segundo antes del contacto, y nunca en los primeros
    // CHOQUE_ARRANQUE_S despues de tomar el mando
    static const float CHOQUE_RAPIDO_KMH = 8.0;
    static const float CHOQUE_ARRANQUE_S = 3.0;
    static const float CHOQUE_RUEDAS_S = 0.5;     // la historia de ruedas en el suelo que muestra la linea del choque
    // despues de un `corte` de la cinta (tramo no reproducible: la toma salta) "fuera de la linea" no cuenta hasta que la
    // moto se re-engancha: FUERA_ENLINEA_S seguidos en la linea, con tope FUERA_GRACIA_MAX_S
    static const float FUERA_ENLINEA_S = 0.5;
    static const float FUERA_GRACIA_MAX_S = 8.0;
    // ---- LA CAMINATA. La moto no tiene reversa, y la caminata nativa (la del jugador que la empuja con los pies) solo la
    //      procesa un conductor JUGADOR: el input de un maniqui llega a su controlador y no la mueve (laboratorio de motos,
    //      herramienta aparte, 28/09). Se reproduce por EMPUJE FISICO: la velocidad horizontal de la moto fijada cada cuadro
    //      (SetVelocity) en lazo cerrado sobre la de la toma (a lazo abierto llega al 67-77 %), mas una correccion lateral
    //      hacia la linea grabada, y el rumbo grabado por la velocidad angular en Y. Hacia ATRAS el conductor se anima solo
    //      (el grafo pasa a Reverse_Neutral con VehicleSpeed < 0); hacia adelante tambien, si entra lento (0 < v < 0,5 m/s:
    //      el arranque a CAM_ARRANQUE_V). Dos usos:
    //       - los TRAMOS CAMINADOS de la toma (columna 15 de la cinta, `camina`);
    //       - la REVERSA DEL DESTRABE: trabada (sin avanzar) o un choque de frente -> camina DESTRABE_M hacia atras sobre su
    //         rumbo y reintenta; a DESTRABE_MAX en el mismo lugar, la re-ubicacion de siempre.
    //      En la caminata no se proyecta sobre la cinta (su odometria no tiene signo); al terminar se la ancla.
    // el mando del empuje NO depende de la ultima lectura: un P sobre la lectura cruda oscilaba cuadro a cuadro (-0,2/-1,7
    // m/s; la moto responde dentro del mismo cuadro). Es la velocidad objetivo x una ganancia g que se ajusta despacio
    // con la lectura filtrada. El ARRANQUE, a proposito: CAM_ARRANQUE_S a CAM_ARRANQUE_V (0 < v < 0,5: el grafo entra a
    // caminar, tambien hacia adelante) y una rampa de CAM_RAMPA_S hasta la velocidad de la toma
    static const float CAM_TAU_FILTRO = 0.15;    // s: el filtro de la lectura
    static const float CAM_KG = 2.5;             // 1/s: lo rapido que se ajusta g (por error relativo)
    static const float CAM_G_INI = 1.3;          // el empuje a lazo abierto llegaba al ~77 %
    static const float CAM_G_MIN = 0.5;
    static const float CAM_G_MAX = 3.0;
    static const float CAM_ARRANQUE_V = 0.3;     // m/s
    static const float CAM_ARRANQUE_S = 0.5;
    static const float CAM_RAMPA_S = 1.0;
    static const float CAM_VCMD_MAX = 2.5;       // m/s: tope del mando del empuje
    static const float CAM_KLAT = 1.0;           // la correccion lateral: m/s por m de lateral
    static const float CAM_VLAT_MAX = 0.5;       // m/s
    static const float CAM_KRUMBO = 2.0;         // el rumbo: rad/s por rad de error
    // el LATERAL se corrige por el RUMBO: el rumbo objetivo gira hacia la linea atan(lateral / CAM_LAT_LOOK_M), hasta
    // CAM_LAT_RUMBO_MAX_DEG; lo mismo en los dos sentidos (el movimiento rota con la moto). La velocidad de costado no
    // sobrevive a las ruedas: con ella sola el tramo hacia atras termino 0,35 m de costado en 3 de 3
    static const float CAM_LAT_LOOK_M = 1.0;
    static const float CAM_LAT_RUMBO_MAX_DEG = 15.0;
    static const float CAM_WMAX = 1.0;           // rad/s
    static const float CAM_CURV_MITAD_M = 0.5;   // el feedforward del rumbo: dpsi/ds de la toma en +-esto (m)
    // la velocidad del DESTRABE, sin nombres de clase (una moto de un mod tiene que andar igual): la media de los tramos
    // caminados de la toma; si no hay, el config (neutralGearMaxSpeed x CAM_VNEUTRA_FRAC: medido Moped 1,17 / 1,3 = 0,90,
    // Enduro 0,74 / 0,8 = 0,92); si falta todo, CAM_V_DEFECTO con aviso
    static const float CAM_VNEUTRA_FRAC = 0.9;
    static const float CAM_V_DEFECTO = 1.0;
    static const float CAM_V_MIN = 0.3;
    static const float CAM_V_MAX = 2.0;
    static const float CAM_ENTRA_M = 0.3;        // el tramo empieza a esto de su arco inicial
    static const float CAM_ENTRA_KMH = 1.5;      // para empezar a empujar, la moto casi quieta (frena si viene andando)
    static const float CAM_FIN_PASADA_M = 0.05;  // o cruzo el plano de la pose final en el sentido de la caminata (a esto de llegar ya vale)
    static const float CAM_FIN_M = 0.15;         // el tramo termina a esto de su pose final (o en su ultimo frame)
    static const float CAM_ROLL_MAX_DEG = 20.0;  // guardas: si se inclina mas, o se va de la linea o del rumbo, se corta
    static const float CAM_LAT_MAX_M = 1.5;
    static const float CAM_RUMBO_MAX_DEG = 45.0;
    static const float CAM_CONTACTO_S = 0.3;     // un contacto con un objeto (no el terreno) de hace menos de esto corta
    static const float CAM_TOPE_EXTRA_S = 4.0;   // tope: 2 x lo que dura en la toma (o lo que tarda el destrabe) + esto
    static const float DESTRABE_M = 3.0;
    static const int   DESTRABE_MAX = 2;         // en el mismo lugar; despues, la re-ubicacion
    static const float DESTRABE_LUGAR_M = 5.0;   // "el mismo lugar": a menos de esto del arco del destrabe anterior
    // ---- el ENDPOINT: el FIN se declara QUIETA; en los ultimos FIN_M, FRENADO AL PUNTO en lazo cerrado sobre la
    //      distancia GEOMETRICA a la pose final (la odometria de la cinta se atrasa hasta ~1,7 m en los ultimos metros y el
    //      rescate la salta de golpe: el slam fijo por la odometria dejo la moto a 0,41 m, pasada). a_req = v^2 / 2d; desde
    //      FRENO_UMBRAL x la frenada medida, freno = a_req / frenada medida (piso FRENO_PISO); a FIN_PASADA_M o pasada, a
    //      fondo. Recalculado cada cuadro: con una frenada real mas fuerte que la medida (~7,5-8 contra 5,38 m/s2), a_req
    //      baja y el freno afloja.
    static const float FIN_QUIETO_KMH = 0.3;
    static const float FIN_GEO_M = 6.0;            // a menos de esto de la pose final manda la geometria
    static const float FIN_GEO_ODO_M = 2.0;        // ...si la odometria tambien dice que faltan menos de FIN_GEO_M + esto
    static const float FRENO_UMBRAL = 0.35;        // del AMax: arranca el slam cuando hace falta esto (con 0,5 arrancaba a 3 m y
                                                   // entrando a 19 km/h pedia 4,6 m/s2 contra 4,9 de eficacia: se paso 0,34)
    static const float FRENO_SUELTA = 0.3;         // de la eficacia medida: por debajo de esto suelta el pedal (lazo cerrado)
    static const float FIN_SLAM_M = 6.0;           // el frenado al punto puede arrancar desde aca (simulado con eficacias reales
                                                   // de 4 a 7 m/s2 y entradas de 17 a 22 km/h: +0,01 en todos los casos)
    static const float FRENO_PISO = 0.1;
    static const float FIN_PASADA_M = 0.02;
    // ★ LA EFICACIA DEL FRENO SE MIDE EN EL SLAM. La frenada medida (p95 del envelope) sale de la toma a alta velocidad y
    // con los dos frenos; en el final, a 10-18 km/h, el pedal entrega mucho menos (medido: 0,55 de pedal = ~2,2 m/s2,
    // 1,0 = 4,4-5,6 m/s2, contra 8,1 medidos) y la moto se pasaba 0,35-0,66 m. Cuadro a cuadro, la desaceleracion lograda
    // contra el pedal aplicado da la eficacia (m/s2 por unidad de pedal), y el freno se dosa con ella: arranca en
    // FRENO_EFIC_INI x la frenada medida y se acota entre FRENO_EFIC_MIN y la frenada medida.
    static const float FRENO_EFIC_INI = 0.6;
    static const float FRENO_EFIC_MIN = 2.0;
    static const float FRENO_EFIC_ALFA = 0.3;      // filtro de la eficacia medida (por cuadro)
    static const float FRENO_EFIC_BRK_MIN = 0.2;   // con menos pedal que esto la medida no vale
    static const float FRENO_EFIC_A_MIN = 0.2;     // m/s2: una desaceleracion menor no mide nada
    static const float ENDPOINT_TOPE_S = 8.0;      // en la zona del final sin quedar quieta: FIN igual (y lo dice)
    // ---- la caja negra: despues del FIN sigue hasta el reposo (como la del auto)
    static const float LOG_COLA_QUIETO_KMH = 0.3;
    static const float LOG_COLA_QUIETO_S = 0.5;
    static const float LOG_COLA_TOPE_S = 3.0;

    private ref BZCintaMuestra m_M;
    private ref array<float> m_Aut;
    private ref array<float> m_AutR;
    private ref array<float> m_Lean;
    private ref array<float> m_SteerTab;
    private ref array<float> m_K;
    private float m_AMax;
    private float m_APrev;
    private float m_RpmRal;
    private float m_RpmCorte;
    private int   m_MarchaMax;
    private string m_Clase;

    private float m_T;
    private float m_Delta;
    private float m_RollFrac;
    private float m_Corr;
    private int   m_SatCuadros;
    private bool  m_SatAvisada;
    private int   m_SatEpisodios;
    private float m_Thr;
    private float m_Brk;
    private int   m_GearCmd;
    private float m_VRef;
    private int   m_Offset;
    private int   m_UltMarchaToma;
    private float m_LugDesde;
    private float m_UltCambio;
    private int   m_Bajadas;
    private int   m_UltGearCmd;
    private int   m_MarchaPedida;
    private int   m_Pedidos;
    private int   m_Perdidas;
    private float m_ClutchHasta;
    private bool  m_EnAire;
    private int   m_AireVeces;
    private float m_SUlt;
    private float m_SinAvance;
    private int   m_Reubicaciones;
    private float m_CaidaDesde;
    private bool  m_LevantoPedido;
    private int   m_Caidas;
    private float m_SCorteUlt;
    private bool  m_Tomada;
    private bool  m_Termino;
    private bool  m_FinOk;
    private string m_Fin;
    private int   m_Latido;
    private float m_LatMax;
    // las guardas relativas a la toma, lo que la ley calculo en el cuadro, y la caja negra
    private float m_SArco;
    private float m_RollToma;        // |roll| de la toma en el arco (-99: la cinta no trae la columna 14)
    private float m_RollUmbral;
    private float m_GuardaLat;       // factor de la guarda lateral (1 = no actua); 1 - esto = el exceso de la de velocidad
    private float m_FF;
    private float m_CorrCrudo;
    private float m_Epsi;
    private float m_Curv;            // la curvatura grabada en el arco (rad/m)
    private float m_CurvAnt;         // la curvatura grabada CASC_LA_S s mas adelante, en +- CASC_W_M (la base de la cascada)
    private float m_RollDes;         // el roll objetivo de la cascada (grados; 0 fuera de ella)
    private float m_Mezcla;          // 0 ley directa .. 1 cascada entera
    private float m_RollPrev;        // el roll del cuadro anterior (-999: sin lectura)
    private float m_RollTasa;        // la tasa de roll filtrada (grados/s)
    private float m_CamW;            // la tasa de giro impuesta caminando (rad/s)
    private float m_CamVol;          // el manubrio caminando (el angulo de rueda grabado, en unidades de direccion)
    private float m_RMoto;           // tasa de giro de la moto (rad/s, + a la derecha)
    private float m_RToma;           // tasa de giro de la referencia: m_Curv x velocidad de la moto
    private float m_Amort;           // TD x (r_moto - r_toma), en radianes de e_psi equivalente
    private float m_VRefSinGuarda;
    private bool  m_GuardaVel;
    private float m_FueraT;
    private float m_FueraSIni;
    private bool  m_FueraFrenando;
    private int   m_FueraVeces;
    private float m_ChoqueDesde;
    private ref BZHistRuedas m_HistRuedas;   // las ruedas en el suelo de los ultimos cuadros (solo instrumento)
    private float m_UltRapidoTick;   // GetTickTime de la ultima vez a CHOQUE_RAPIDO_KMH o mas (el contacto usa ese reloj)
    private float m_TTomada;         // cuando tomo el mando (m_T)
    private bool  m_CorteGracia;
    private float m_CorteGraciaHasta;
    private float m_EnLineaT;
    private float m_ZonaFinDesde;
    private bool  m_Slam;
    private float m_FrenoEficaz;     // m/s2 por unidad de pedal, medida en el slam (ver FRENO_EFIC_INI)
    private float m_VmsFinPrev;      // la velocidad del cuadro anterior del slam (-1: todavia no)
    private float m_BrkFinPrev;      // el pedal aplicado en ese cuadro
    private int   m_Choques;
    private int   m_ChoquesObjeto;          // los de frente contra una entidad (el terreno no cuenta): invalidan el COMPLETE
    private string m_ListaChoquesObj = "";
    // la llegada deja el FIN PENDIENTE: la cola mide el reposo y ahi se decide (DecidirFin), como en el auto
    private bool  m_FinPend;
    private bool  m_FinDecidido;
    private string m_FinPendMotivo = "";
    private float m_ReposoFinM = -1;   // la medida del veredicto (m); -1 hasta que el reposo la decide
    private bool  m_SinReloj;
    private FileHandle m_LogFh;
    private bool  m_LogOn;
    private bool  m_LogCola;
    private float m_LogColaT;
    private float m_LogQuietoT;
    private string m_LogEvento;
    // la caminata
    private ref array<int> m_CamA;       // los tramos caminados de la cinta: frame inicial y final
    private ref array<int> m_CamB;
    private int   m_CamProx;             // el proximo tramo a caminar
    private int   m_CamModo;             // 0 no · 1 un tramo de la toma · 2 el destrabe
    private bool  m_CamEmpujando;        // ya quieta: empuja
    private int   m_CamTramo;
    private int   m_CamK;                // el frame de la toma mas cercano (el cursor del tramo)
    private float m_CamSgn;              // +1 el tramo va hacia adelante, -1 hacia atras (el signo de la toma)
    private float m_CamT;
    private float m_CamTope;
    private float m_CamTickIni;          // GetTickTime al empezar a empujar (un contacto de antes no corta)
    private float m_CamG;                // la ganancia del empuje (se ajusta despacio)
    private float m_CamVF;               // la lectura filtrada de la velocidad sobre el sentido del empuje
    private float m_CamTEmp;             // segundos empujando (el arranque y la rampa)
    private Human m_TagChofer;           // la etiqueta del grafo del chofer (caja negra): a quien se ligo
    private int   m_TagCamId;
    private float m_CamVSum;
    private int   m_CamVN;
    private float m_CamLatMax;
    private float m_CamRumboMax;
    private vector m_CamP0;
    private float m_CamVDestrabe;        // m/s: lo que camina un jugador con esta moto
    private string m_CamVFuente;         // de donde salio (la toma, el config o el defecto)
    private float m_CamRumboDestrabe;    // grados: el rumbo que sostiene el destrabe
    private float m_CamSBase;            // el arco donde empezo
    private string m_CamMotivo;
    private int   m_DestrabeN;
    private float m_DestrabeSUlt;
    private float m_DestrabeSIni;        // el arco de la PRIMERA traba de este lugar (la re-ubicacion no retrocede)
    private int   m_Caminatas;
    private int   m_Destrabes;

    void BZConductorMoto() {
        m_M = new BZCintaMuestra();
        m_Aut = new array<float>();
        m_AutR = new array<float>();
        m_Lean = new array<float>();
        m_SteerTab = new array<float>();
        m_K = new array<float>();
        m_AMax = VEL_A_DEF;
        m_APrev = VEL_A_DEF * VEL_A_FRAC;
        m_RpmRal = 1000;
        m_RpmCorte = 5000;
        m_MarchaMax = 6;
        m_Delta = 0;
        m_UltMarchaToma = -99;
        m_LugDesde = -1;
        m_UltCambio = -99;
        m_UltGearCmd = -99;
        m_MarchaPedida = -99;
        m_ClutchHasta = -1;
        m_SUlt = -1;
        m_CaidaDesde = -1;
        m_SCorteUlt = -1;
        m_Fin = "";
        m_RollToma = -99;
        m_RollPrev = -999;
        m_GuardaLat = 1.0;
        m_FueraSIni = -1;
        m_ChoqueDesde = -1;
        m_UltRapidoTick = -99;
        m_TTomada = -99;
        m_ZonaFinDesde = -1;
        m_LogEvento = "";
        m_CamModo = 0;
        m_DestrabeSUlt = -99;
    }

    void ~BZConductorMoto() {
        CerrarLogYa();
    }

    bool Termino() { return m_Termino; }
    bool FinOk() { return m_FinOk; }
    float ReposoFinM() { return m_ReposoFinM; }
    string Fin() { return m_Fin; }
    float Arco() { return m_SArco; }
    float GasPedido() { return m_Thr; }
    void SetSinReloj(bool sinReloj) { m_SinReloj = sinReloj; }

    // ------------------------------------------------------------------------
    //  Configurar: las tablas del config de la clase y lo medido en el envelope (lineas ajenas del Store)
    // ------------------------------------------------------------------------
    void Configurar(BZVehiculo v, string cls, array<string> ajenas) {
        m_Clase = cls;
        GetGame().ConfigGetFloatArray("CfgVehicles " + cls + " SimulationModule Steering maxLeanAngle", m_Lean);
        GetGame().ConfigGetFloatArray("CfgVehicles " + cls + " SimulationModule Steering maxSteeringAngle", m_SteerTab);
        m_RpmRal = v.RpmRalenti();
        if (m_RpmRal < 300) m_RpmRal = 1000;
        m_RpmCorte = v.RpmCorte();
        if (m_RpmCorte < m_RpmRal * 2) m_RpmCorte = m_RpmRal * 5;
        // acumuladores del envelope: aut por (franja, retraso), bfrena por bin, rpmkmh por marcha
        array<float> aSxy = new array<float>();
        array<float> aSxx = new array<float>();
        array<float> aSyy = new array<float>();
        array<float> aN = new array<float>();
        int iz;
        for (iz = 0; iz < 42; iz++) {
            aSxy.Insert(0);
            aSxx.Insert(0);
            aSyy.Insert(0);
            aN.Insert(0);
        }
        array<float> frBins = new array<float>();
        for (iz = 0; iz < 64; iz++) frBins.Insert(0);
        array<float> kSxy = new array<float>();
        array<float> kSxx = new array<float>();
        array<float> kN = new array<float>();
        for (iz = 0; iz < 12; iz++) {
            kSxy.Insert(0);
            kSxx.Insert(0);
            kN.Insert(0);
        }
        int lineasAut = 0;
        int lineasFr = 0;
        int lineasK = 0;
        if (ajenas) {
            int li;
            for (li = 0; li < ajenas.Count(); li++) {
                TStringArray tk = new TStringArray();
                string lin = ajenas.Get(li);
                lin.Split(",", tk);
                if (tk.Count() < 3) continue;
                string kind = tk.Get(0);
                if (kind == "aut" && tk.Count() >= 7) {
                    int b = tk.Get(1).ToInt();
                    int lagIdx = Math.Round(tk.Get(2).ToFloat() / 100.0);
                    if (b < 0 || b > 5 || lagIdx < 0 || lagIdx > 6) continue;
                    int ix = b * 7 + lagIdx;
                    aSxy.Set(ix, aSxy.Get(ix) + tk.Get(3).ToFloat());
                    aSxx.Set(ix, aSxx.Get(ix) + tk.Get(4).ToFloat());
                    aSyy.Set(ix, aSyy.Get(ix) + tk.Get(5).ToFloat());
                    aN.Set(ix, aN.Get(ix) + tk.Get(6).ToFloat());
                    lineasAut = lineasAut + 1;
                } else if (kind == "bfrena") {
                    int bin = tk.Get(1).ToInt();
                    if (bin < 0 || bin > 63) continue;
                    frBins.Set(bin, frBins.Get(bin) + tk.Get(2).ToFloat());
                    lineasFr = lineasFr + 1;
                } else if (kind == "rpmkmh" && tk.Count() >= 5) {
                    int g = tk.Get(1).ToInt();
                    if (g < 0 || g > 11) continue;
                    kSxy.Set(g, kSxy.Get(g) + tk.Get(2).ToFloat());
                    kSxx.Set(g, kSxx.Get(g) + tk.Get(3).ToFloat());
                    kN.Set(g, kN.Get(g) + tk.Get(4).ToFloat());
                    lineasK = lineasK + 1;
                }
            }
        }
        // AUT: por franja, el retraso de mejor r; r < AUT_R_MIN o pocos cuadros -> la vecina; sin nada, AUT_DEF
        array<float> medidas = new array<float>();
        int bF;
        for (bF = 0; bF < 6; bF++) {
            float mejorR = -2;
            float mejorK = -1;
            int lg;
            for (lg = 0; lg < 7; lg++) {
                int ixF = bF * 7 + lg;
                if (aN.Get(ixF) < AUT_MIN_N || aSxx.Get(ixF) <= 0 || aSyy.Get(ixF) <= 0) continue;
                float rr = aSxy.Get(ixF) / Math.Sqrt(aSxx.Get(ixF) * aSyy.Get(ixF));
                if (rr > mejorR) {
                    mejorR = rr;
                    mejorK = aSxy.Get(ixF) / aSxx.Get(ixF);
                }
            }
            if (mejorR < AUT_R_MIN) mejorK = -1;
            medidas.Insert(mejorK);
            m_AutR.Insert(mejorR);
        }
        for (bF = 0; bF < 6; bF++) {
            float a = medidas.Get(bF);
            if (a <= 0) {
                int dj;
                for (dj = 1; dj < 6 && a <= 0; dj++) {
                    if (bF - dj >= 0 && medidas.Get(bF - dj) > 0) a = medidas.Get(bF - dj);
                    else if (bF + dj < 6 && medidas.Get(bF + dj) > 0) a = medidas.Get(bF + dj);
                }
            }
            if (a <= 0) a = AUT_DEF;
            if (a < 0.2) a = 0.2;
            if (a > 2.0) a = 2.0;
            m_Aut.Insert(a);
        }
        // frenada: p95 del histograma (bins de 0,25 m/s2)
        float totFr = 0;
        for (iz = 0; iz < 64; iz++) totFr = totFr + frBins.Get(iz);
        string origenA = "por defecto";
        if (totFr >= 20) {
            float acum = 0;
            for (iz = 0; iz < 64; iz++) {
                acum = acum + frBins.Get(iz);
                if (acum >= 0.95 * totFr) {
                    m_AMax = (iz + 0.5) * 0.25;
                    break;
                }
            }
            origenA = "p95 del envelope";
        }
        if (m_AMax < 2) m_AMax = 2;
        if (m_AMax > 12) m_AMax = 12;
        m_APrev = m_AMax * VEL_A_FRAC;
        // rpm por km/h de cada marcha (marcha de la moto: 0 = N)
        m_MarchaMax = 0;
        string marchas = "";
        for (iz = 0; iz < 12; iz++) {
            float kV = -1;
            if (kN.Get(iz) >= 20 && kSxx.Get(iz) > 0) kV = kSxy.Get(iz) / kSxx.Get(iz);
            m_K.Insert(kV);
            if (kV > 0) {
                if (iz > m_MarchaMax) m_MarchaMax = iz;
                marchas = marchas + " " + iz + ":" + Red(kV);
            }
        }
        if (m_MarchaMax < 1) m_MarchaMax = 6;
        string la = "[MOTO] conductor de moto para " + cls + " · AUT (rad/s por unidad):";
        for (bF = 0; bF < 6; bF++) la = la + " " + NombreFranja(bF) + " " + Red(m_Aut.Get(bF));
        la = la + " · TAU " + Red(TAU) + " s · frenada " + Red(m_AMax) + " m/s2 (" + origenA + ")";
        string lb = " · rpm/kmh:" + marchas + " · piso " + Red(m_RpmRal * VEL_PISO) + " · corte " + Red(m_RpmCorte);
        lb = lb + " · envelope: " + lineasAut + " aut, " + lineasFr + " bfrena, " + lineasK + " rpmkmh · maxLeanAngle " + (m_Lean.Count() / 2) + " puntos";
        BZBusLog.Info(la + lb);
    }

    static int FranjaDe(float kmh) {
        if (kmh < 5) return 0;
        if (kmh < 10) return 1;
        if (kmh < 20) return 2;
        if (kmh < 35) return 3;
        if (kmh < 60) return 4;
        return 5;
    }

    static string NombreFranja(int b) {
        if (b == 0) return "0-5";
        if (b == 1) return "5-10";
        if (b == 2) return "10-20";
        if (b == 3) return "20-35";
        if (b == 4) return "35-60";
        return "60+";
    }

    static string Red(float x) {
        float r = Math.Round(x * 100) / 100;
        return r.ToString();
    }

    float AutDe(float kmh) {
        if (m_Aut.Count() < 6) return AUT_DEF;
        return m_Aut.Get(FranjaDe(kmh));
    }

    // una tabla de pares {velocidad, valor, ...} del config, interpolada
    static float Tabla(array<float> tb, float kmh) {
        if (!tb || tb.Count() < 4) return 0;
        int n = tb.Count() / 2;
        if (kmh <= tb.Get(0)) return tb.Get(1);
        int i;
        for (i = 1; i < n; i++) {
            float va = tb.Get(2 * i - 2);
            float la = tb.Get(2 * i - 1);
            float vb = tb.Get(2 * i);
            float lb = tb.Get(2 * i + 1);
            if (kmh <= vb) {
                if (vb - va < 0.001) return lb;
                return la + (lb - la) * (kmh - va) / (vb - va);
            }
        }
        return tb.Get(2 * n - 1);
    }

    static float Envolver(float rad) {
        float r = rad;
        while (r > Math.PI) r = r - Math.PI2;
        while (r < -Math.PI) r = r + Math.PI2;
        return r;
    }

    float KDe(int g) {
        if (g < 0 || g >= m_K.Count()) return -1;
        return m_K.Get(g);
    }

    // la marcha de la moto desde la de la cinta (el conversor la corre en uno: N = 1, 1ra = 2)
    static int MarchaMotoDe(int gCinta) {
        int g = gCinta - 1;
        if (g < 0) g = 0;
        return g;
    }

    // ------------------------------------------------------------------------
    //  Un cuadro. true = el conductor comando el vehiculo.
    // ------------------------------------------------------------------------
    bool Paso(BZVehiculo v, BZCintaRunner c, float dt) {
        if (m_Termino) return false;
        Transport tr = v.T();
        if (!tr || !c || !c.Valida()) return false;
        m_T = m_T + dt;
        // llegada hecha, veredicto pendiente: quieta con freno mientras la cola mide el reposo
        if (m_FinPend) {
            v.Gas(0);
            v.Frenar(1.0);
            Cola(v, c, dt);
            return true;
        }

        // CAIDA: frenos, y a los 2 s PickUp; el servicio vuelve a sentar al chofer
        if (v.Caida()) {
            if (m_CaidaDesde < 0) {
                m_CaidaDesde = m_T;
                m_LevantoPedido = false;
                m_Caidas = m_Caidas + 1;
                BZBusLog.Warn("[MOTO] CAIDA #" + m_Caidas + " en el arco " + Red(c.SEst()) + " m: se levanta en " + CAIDA_LEVANTAR_S + " s");
                m_LogEvento = "caida";
            }
            v.Gas(0);
            v.Frenar(1.0);
            VolcarLog(v, c, c.SEst(), v.KmhAbs());
            if (!m_LevantoPedido && m_T - m_CaidaDesde >= CAIDA_LEVANTAR_S) {
                m_LevantoPedido = true;
                v.Levantar();
                BZBusLog.Info("[MOTO] PickUp pedido (caida #" + m_Caidas + ")");
            }
            return true;
        }
        if (m_CaidaDesde >= 0) {
            BZBusLog.Info("[MOTO] la moto esta de pie otra vez (caida #" + m_Caidas + ", " + Red(m_T - m_CaidaDesde) + " s)");
            m_CaidaDesde = -1;
            m_UltGearCmd = -99;   // la marcha se vuelve a pedir desde cero
        }
        // sin chofer en el asiento 0 (lo re-sienta el servicio): quieta
        Human chofer = tr.CrewMember(0);
        if (!chofer) {
            v.Gas(0);
            v.Frenar(1.0);
            return true;
        }
        HumanCommandVehicle hcvCh = chofer.GetCommand_Vehicle();
        if (hcvCh && hcvCh.IsGettingIn()) {
            v.Gas(0);
            v.Frenar(1.0);
            return true;
        }
        if (!m_Tomada) {
            v.TomarPosesion();
            m_Tomada = true;
            m_TTomada = m_T;
            BZBusLog.Info("[MOTO] toma el mando: pata arriba, cinta de " + Red(c.SFin()) + " m" + TextoSteering(c) + TextoRoll(c));
        }

        // LA CAMINATA: los tramos caminados de la toma y la reversa del destrabe van por empuje, sin la cinta
        if (Caminando(v, c, dt)) return true;

        vector p = tr.GetPosition();
        c.Muestra(p, m_M, false, false);   // la moto no maneja en reversa: sin anclaje de reversa
        float s = c.SEst();
        m_SArco = s;
        m_RollToma = -99;
        if (m_M.roll > -90) m_RollToma = Math.AbsFloat(m_M.roll);
        m_Curv = c.CurvaturaEn(s, CURV_MITAD_M);
        float kmh = v.KmhAbs();
        m_CurvAnt = c.CurvaturaEn(s + kmh / 3.6 * CASC_LA_S, CASC_W_M);
        float latAbs = Math.AbsFloat(m_M.latM);
        if (latAbs > m_LatMax) m_LatMax = latAbs;

        // FIN
        float restoFin = c.SFin() - s;
        // cerca del final manda el resto GEOMETRICO: la distancia a la pose final sobre su rumbo (negativa si se paso). Solo
        // con la odometria tambien en los ultimos metros: en una ruta circular la pose final esta al lado de la salida
        vector posFin;
        float rumboFin;
        if (restoFin <= FIN_GEO_M + FIN_GEO_ODO_M && c.PoseEnS(c.SFin(), posFin, rumboFin)) {
            vector pFin = tr.GetPosition();
            float dxFin = posFin[0] - pFin[0];
            float dzFin = posFin[2] - pFin[2];
            if (Math.Sqrt(dxFin * dxFin + dzFin * dzFin) <= FIN_GEO_M) {
                float hFin = rumboFin * Math.DEG2RAD;
                restoFin = dxFin * Math.Sin(hFin) + dzFin * Math.Cos(hFin);
            }
        }
        if (restoFin <= FIN_SLAM_M) {
            if (m_ZonaFinDesde < 0) m_ZonaFinDesde = m_T;
            bool porTope = m_T - m_ZonaFinDesde >= ENDPOINT_TOPE_S;
            bool quietaEnFin = (kmh < FIN_QUIETO_KMH && restoFin <= FIN_M);
            if (quietaEnFin || porTope) {
                string llegada = " · llegada: " + PoseContraFinal(v, c) + ", a " + Red(kmh) + " km/h";
                if (kmh < FIN_QUIETO_KMH) {
                    llegada = llegada + " (frenada a 0 en el endpoint";
                    if (m_Slam) llegada = llegada + ", con slam; eficacia del freno medida " + Red(m_FrenoEficaz) + " m/s2 por unidad de pedal";
                    llegada = llegada + ")";
                } else {
                    llegada = llegada + " (todavia rodando a los " + Red(ENDPOINT_TOPE_S) + " s en la zona del final: la frena el retiro)";
                }
                Terminar(v, "fin de la cinta (arco " + Red(s) + " de " + Red(c.SFin()) + " m)" + llegada, true);
                return true;
            }
            // el FRENADO AL PUNTO: la desaceleracion que hace falta para parar en el final, contra la eficacia del freno
            // (la velocidad es la mayor entre la rueda y el cuerpo: una rueda que bloquea no afloja el pedal)
            vector velE = GetVelocity(tr);
            float kmhCuerpoE = Math.Sqrt(velE[0] * velE[0] + velE[2] * velE[2]) * 3.6;
            float kmhE = kmh;
            if (kmhCuerpoE > kmhE && kmhCuerpoE < 200) kmhE = kmhCuerpoE;
            float vmsE = kmhE / 3.6;
            float aReq = 99;
            if (restoFin > FIN_PASADA_M) aReq = vmsE * vmsE / (2.0 * restoFin);
            if (!m_Slam && aReq >= FRENO_UMBRAL * m_AMax) {
                m_Slam = true;
                m_LogEvento = "slam";
                m_FrenoEficaz = FRENO_EFIC_INI * m_AMax;
                if (m_FrenoEficaz < FRENO_EFIC_MIN) m_FrenoEficaz = FRENO_EFIC_MIN;
                m_VmsFinPrev = -1;
                m_BrkFinPrev = 0;
                BZBusLog.Info("[MOTO] ENDPOINT: frenado al punto (slam) a " + Red(restoFin) + " m del final (geometrico), a " + Red(kmhE) + " km/h (hacen falta " + Red(aReq) + " m/s2; frenada medida " + Red(m_AMax) + ", eficacia inicial " + Red(m_FrenoEficaz) + " m/s2 por unidad de pedal)");
            }
            // quieta ANTES de la zona del FIN (el slam freno de mas): suelta el slam y la conduccion normal la acerca
            if (m_Slam && kmh < FIN_QUIETO_KMH && restoFin > FIN_M) {
                m_Slam = false;
                m_LogEvento = "slam corto";
                BZBusLog.Info("[MOTO] ENDPOINT: quieta a " + Red(restoFin) + " m del final, antes de la zona del FIN: sigue la conduccion normal");
            }
            if (m_Slam) {
                // la eficacia medida: lo que freno el cuadro anterior con el pedal que tenia
                if (m_VmsFinPrev >= 0 && dt > 0.001 && m_BrkFinPrev >= FRENO_EFIC_BRK_MIN) {
                    float aMedE = (m_VmsFinPrev - vmsE) / dt;
                    if (aMedE >= FRENO_EFIC_A_MIN) {
                        float eficE = aMedE / m_BrkFinPrev;
                        if (eficE < FRENO_EFIC_MIN) eficE = FRENO_EFIC_MIN;
                        if (eficE > m_AMax) eficE = m_AMax;
                        m_FrenoEficaz = m_FrenoEficaz + FRENO_EFIC_ALFA * (eficE - m_FrenoEficaz);
                    }
                }
                float brkE = 0;
                if (restoFin <= FIN_PASADA_M) {
                    brkE = 1.0;
                } else if (aReq >= FRENO_UMBRAL * m_AMax || aReq >= FRENO_SUELTA * m_FrenoEficaz) {
                    brkE = aReq / m_FrenoEficaz;
                    if (brkE < FRENO_PISO) brkE = FRENO_PISO;
                    if (brkE > 1.0) brkE = 1.0;
                }
                m_VmsFinPrev = vmsE;
                m_BrkFinPrev = brkE;
                m_Thr = 0;
                m_Brk = brkE;
                v.Gas(0);
                v.Frenar(brkE);
                VolcarLog(v, c, s, kmh);
                return true;
            }
        }
        if (m_M.corte == 1 && s > m_SCorteUlt + 1.0) {
            m_SCorteUlt = s;
            m_CorteGracia = true;
            m_CorteGraciaHasta = m_T + FUERA_GRACIA_MAX_S;
            m_EnLineaT = 0;
            m_FueraT = 0;
            BZBusLog.Info("[MOTO] arco " + Red(s) + " m: la toma retoma despues de un tramo NO reproducible (la moto empujada en N); se sigue desde donde esta");
        }

        // las ruedas en el suelo de cada cuadro, para la linea del choque (solo instrumento)
        if (!m_HistRuedas) m_HistRuedas = new BZHistRuedas();
        int ruedasH = 0;
        if (v.RuedaContacto(0)) ruedasH = ruedasH + 1;
        if (v.RuedaContacto(1)) ruedasH = ruedasH + 1;
        m_HistRuedas.Anotar(ruedasH, dt);

        // CHOQUE DE FRENTE: gas 0 (no empujar el obstaculo) y, a los CHOQUE_DECLARA_S, re-ubicacion
        float ahoraTick = GetGame().GetTickTime();
        if (kmh >= CHOQUE_RAPIDO_KMH) m_UltRapidoTick = ahoraTick;
        float tCh = v.ContactoFrenteT();
        bool caidaV = tCh >= 0 && m_UltRapidoTick >= tCh - CHOQUE_VENTANA_S;
        bool arrancada = m_TTomada > -90 && m_T - m_TTomada >= CHOQUE_ARRANQUE_S;
        if (m_ChoqueDesde < 0 && arrancada && caidaV && ahoraTick - tCh <= CHOQUE_VENTANA_S && kmh < CHOQUE_KMH && m_M.kmhRec >= QUIETO_KMH) {
            m_ChoqueDesde = m_T;
            m_Choques = m_Choques + 1;
            m_LogEvento = "choque";
            // contra una entidad cuenta para el veredicto; el terreno no
            string clsCh = v.ContactoFrenteClase();
            if (clsCh != "terreno" && clsCh != "") {
                m_ChoquesObjeto = m_ChoquesObjeto + 1;
                if (m_ListaChoquesObj != "") m_ListaChoquesObj = m_ListaChoquesObj + ", ";
                m_ListaChoquesObj = m_ListaChoquesObj + "arco " + Red(s) + " m contra " + clsCh;
            }
            string lch = "[MOTO] CHOQUE de frente con " + v.ContactoFrenteClase() + " a " + Red(kmh) + " km/h en el arco " + Red(s) + " m (venia a ";
            lch = lch + Red(CHOQUE_RAPIDO_KMH) + " km/h o mas): gas 0; en " + Red(CHOQUE_DECLARA_S) + " s camina hacia atras y reintenta";
            string cUlt = v.ContactoUltimo(CHOQUE_VENTANA_S);
            if (cUlt == "") cUlt = "ninguno en " + Red(CHOQUE_VENTANA_S) + " s";
            lch = lch + " · ultimo contacto: " + cUlt;
            lch = lch + " · ruedas en los " + Red(CHOQUE_RUEDAS_S) + " s previos: " + m_HistRuedas.Resumen(CHOQUE_RUEDAS_S, 2);
            BZBusLog.Warn(lch);
        }
        if (m_ChoqueDesde >= 0) {
            m_Thr = 0;
            m_Brk = 0.3;
            v.Gas(0);
            v.Frenar(m_Brk);
            v.Volante(0);
            if (m_T - m_ChoqueDesde >= CHOQUE_DECLARA_S) {
                m_ChoqueDesde = -1;
                Destrabar(v, c, s, "choque de frente");
            }
            VolcarLog(v, c, s, kmh);
            return true;
        }
        // FUERA DE LA LINEA: sostenido FUERA_DECLARA_S, frena a 0 y se re-ubica donde se salio (sin orbitar)
        vector fwdF = tr.GetDirection();
        m_Epsi = Envolver(Math.Atan2(fwdF[0], fwdF[2]) - m_M.headingDeg * Math.DEG2RAD);
        bool fueraL = Math.AbsFloat(m_M.latM) > FUERA_EY_M || Math.AbsFloat(m_Epsi) * Math.RAD2DEG > FUERA_EPSI_DEG;
        // despues de un corte: no cuenta hasta re-engancharse (FUERA_ENLINEA_S en la linea) o el tope
        if (m_CorteGracia && !m_FueraFrenando) {
            if (fueraL) m_EnLineaT = 0;
            else m_EnLineaT = m_EnLineaT + dt;
            if (m_EnLineaT >= FUERA_ENLINEA_S || m_T >= m_CorteGraciaHasta) {
                m_CorteGracia = false;
                BZBusLog.Info("[MOTO] arco " + Red(s) + " m: re-enganchada despues del corte; 'fuera de la linea' vuelve a contar");
            } else {
                fueraL = false;
                m_FueraT = 0;
            }
        }
        if (fueraL && m_M.kmhRec >= QUIETO_KMH && !c.EnMeseta()) {
            if (m_FueraT <= 0) m_FueraSIni = s;
            m_FueraT = m_FueraT + dt;
        } else if (!m_FueraFrenando) {
            m_FueraT = 0;
        }
        if (!m_FueraFrenando && m_FueraT >= FUERA_DECLARA_S) {
            m_FueraFrenando = true;
            m_FueraVeces = m_FueraVeces + 1;
            m_LogEvento = "fuera";
            string lf = "[MOTO] FUERA DE LA LINEA desde el arco " + Red(m_FueraSIni) + " m (lateral " + Red(m_M.latM) + " m, e_psi " + Red(m_Epsi * Math.RAD2DEG);
            lf = lf + " grados, " + Red(FUERA_DECLARA_S) + " s): frena a 0 y se re-ubica, sin dar vueltas";
            BZBusLog.Warn(lf);
        }
        if (m_FueraFrenando) {
            m_Thr = 0;
            m_Brk = FUERA_FRENO;
            v.Volante(0);
            v.Gas(0);
            v.Frenar(m_Brk);
            if (kmh < 1.0) {
                m_FueraFrenando = false;
                m_FueraT = 0;
                Reubicar(v, c, m_FueraSIni, "fuera de la linea");
            }
            VolcarLog(v, c, s, kmh);
            return true;
        }

        float ff = m_M.steer;
        if (ff < -5) ff = SteerDeRueda(m_M.fwDeg, kmh);
        float dir = LeyLateral(v, ff, kmh, dt);
        ControlVelocidad(v, c, kmh, s);
        v.Volante(dir);
        v.Gas(m_Thr);
        v.Frenar(m_Brk);
        v.PonerPata(0);
        AplicarMarcha(v, m_GearCmd);
        VigilarAtasco(v, c, s, dt);
        VolcarLog(v, c, s, kmh);

        m_Latido = m_Latido + 1;
        if (m_Latido >= LATIDO_CADA) {
            m_Latido = 0;
            string l = "[MOTO] arco " + Red(s) + "/" + Red(c.SFin()) + " m · lat " + Red(m_M.latM) + " m (max " + Red(m_LatMax) + ") · " + Red(kmh) + " km/h (toma " + Red(m_M.kmhRec);
            l = l + ", ref " + Red(m_VRef) + ") · gas " + Red(m_Thr) + " freno " + Red(m_Brk) + " · marcha " + v.Marcha() + " (pedida " + m_GearCmd + ") · rpm " + Red(v.Rpm());
            l = l + " · corr " + Red(m_Corr) + " delta " + Red(m_Delta) + " · roll/tope " + Red(m_RollFrac) + " · AUT " + Red(AutDe(kmh));
            if (BZDiag.On()) BZBusLog.Info(l);   // el latido cada 2 s: diagnostico (Diagnostics)
        }
        return true;
    }

    string TextoSteering(BZCintaRunner c) {
        if (c.TieneSteering()) return " (con steering grabado)";
        return " (SIN steering grabado: el feedforward sale del angulo de rueda / maxSteeringAngle[v])";
    }

    string TextoRoll(BZCintaRunner c) {
        if (c.TieneRoll()) return " · guardas de inclinacion relativas a la toma (columna 14)";
        return " · SIN la inclinacion de la toma (cinta vieja): las guardas van contra la tabla del config; reconvertir la toma";
    }

    // sin la columna de steering: el angulo de rueda grabado dividido por el tope del config a esa velocidad
    float SteerDeRueda(float fwDeg, float kmh) {
        float tope = Tabla(m_SteerTab, kmh);
        if (tope < 0.5) return 0;
        float st = fwDeg / tope;
        if (st > 1) st = 1;
        if (st < -1) st = -1;
        return st;
    }

    // ------------------------------------------------------------------------
    //  LEY LATERAL v6 (la del laboratorio de motos, herramienta aparte)
    // ------------------------------------------------------------------------
    float LeyLateral(BZVehiculo v, float ff, float kmh, float dt) {
        Transport tr = v.T();
        float ey = m_M.latM;
        float rollNow = v.Roll();
        if (m_RollPrev < -900) m_RollPrev = rollNow;
        float tasaRoll = 0;
        if (dt > 0.001) tasaRoll = (rollNow - m_RollPrev) / dt;
        m_RollPrev = rollNow;
        m_RollTasa = m_RollTasa + (tasaRoll - m_RollTasa) * dt / (CASC_TASA_TAU + dt);
        vector fwd = tr.GetDirection();
        float psi = Math.Atan2(fwd[0], fwd[2]);
        float psiT = m_M.headingDeg * Math.DEG2RAD;
        float epsi = Envolver(psi - psiT);
        float vms = kmh / 3.6;
        if (vms < 1.0) vms = 1.0;
        float rumboEq = Math.Atan2(KV * ey, vms);
        // el AMORTIGUAMIENTO: la tasa de e_psi = la tasa de giro de la moto menos la de la referencia (la curvatura
        // grabada por la velocidad de la moto: d psi_toma(s(t))/dt = kappa ds/dt)
        vector angM = dBodyGetAngularVelocity(tr);
        m_RMoto = angM[1];
        m_RToma = m_Curv * (kmh / 3.6);
        m_Amort = TD * (m_RMoto - m_RToma);
        float corr = (epsi + rumboEq + m_Amort) / (AutDe(kmh) * TAU);
        m_FF = ff;
        m_Epsi = epsi;
        m_CorrCrudo = corr;
        if (Math.AbsFloat(corr) >= CORR_MAX) m_SatCuadros = m_SatCuadros + 1;
        else {
            m_SatCuadros = 0;
            m_SatAvisada = false;
        }
        if (m_SatCuadros >= SAT_AVISO && !m_SatAvisada && m_M.kmhRec > kmh + 5) {
            m_SatAvisada = true;
            m_SatEpisodios = m_SatEpisodios + 1;
            string ls = "[MOTO] correccion SATURADA hace " + Red(m_SatCuadros * 0.025) + " s con la moto a " + Red(kmh) + " km/h y la toma a " + Red(m_M.kmhRec);
            ls = ls + " (e_psi " + Red(epsi * Math.RAD2DEG) + " grados, AUT " + Red(AutDe(kmh)) + "): ley corta de autoridad";
            BZBusLog.Warn(ls);
        }
        if (corr > CORR_MAX) corr = CORR_MAX;
        if (corr < -CORR_MAX) corr = -CORR_MAX;
        float lean = Tabla(m_Lean, kmh);
        m_RollFrac = 0;
        m_GuardaLat = 1.0;
        m_RollUmbral = 0;
        if (lean > 0.5) {
            float rollAbs = Math.AbsFloat(v.Roll());
            m_RollFrac = rollAbs / lean;
            // el umbral: GUARDA x tope, o lo que inclino la toma en este arco + el margen (lo mayor); el corte total en el
            // tope, o GUARDA_ANCHO_MIN_DEG mas arriba del umbral. Sin la columna 14 queda como antes: (1 - frac) / (1 - GUARDA)
            float umbral = GUARDA * lean;
            float techo = lean;
            if (m_RollToma > -90) {
                float umbralToma = m_RollToma + GUARDA_MARGEN_DEG;
                if (umbralToma > umbral) umbral = umbralToma;
                if (techo < umbral + GUARDA_ANCHO_MIN_DEG) techo = umbral + GUARDA_ANCHO_MIN_DEG;
            }
            m_RollUmbral = umbral;
            if (rollAbs > umbral) {
                float frG = (rollAbs - umbral) / (techo - umbral);
                if (frG > 1) frG = 1;
                m_GuardaLat = 1.0 - frG;
                corr = corr * m_GuardaLat;
            }
        }
        m_Corr = corr;
        float objetivo = ff - corr;
        if (objetivo > 1) objetivo = 1;
        if (objetivo < -1) objetivo = -1;
        // la cascada por inclinacion (ver las constantes CASC_*)
        m_Mezcla = (kmh - CASC_DE_KMH) / (CASC_A_KMH - CASC_DE_KMH);
        if (m_Mezcla < 0) m_Mezcla = 0;
        if (m_Mezcla > 1) m_Mezcla = 1;
        m_RollDes = 0;
        if (m_Mezcla > 0) {
            float theta = epsi + Math.Atan2(CASC_KV * ey, vms);
            float rollBase = Math.Atan(vms * vms * m_CurvAnt / CASC_G) * Math.RAD2DEG;
            float rollDes = rollBase - Math.Atan(vms * theta / (CASC_TAU_R * CASC_G)) * Math.RAD2DEG;
            if (lean > 0.5) {
                float topeC = CASC_MARGEN * lean;
                if (rollDes > topeC) rollDes = topeC;
                if (rollDes < -topeC) rollDes = -topeC;
            }
            m_RollDes = rollDes;
            float aut = AutDe(kmh);
            if (aut < 0.1) aut = 0.1;
            float rDes = CASC_G * Math.Tan(rollDes * Math.DEG2RAD) / vms;
            float sloc = vms * aut / CASC_G * Math.RAD2DEG;
            if (sloc < 10) sloc = 10;
            float eRoll = rollDes - rollNow;
            float objC = rDes / aut + (CASC_KP * eRoll - CASC_KD * m_RollTasa) / sloc;
            if (objC > 1) objC = 1;
            if (objC < -1) objC = -1;
            objetivo = (1.0 - m_Mezcla) * objetivo + m_Mezcla * objC;
        }
        float paso = RATE * dt;
        float d = objetivo - m_Delta;
        if (d > paso) d = paso;
        if (d < -paso) d = -paso;
        m_Delta = m_Delta + d;
        return m_Delta;
    }

    // ------------------------------------------------------------------------
    //  VELOCIDAD OBJETIVO (la del laboratorio de motos, herramienta aparte)
    // ------------------------------------------------------------------------
    void ControlVelocidad(BZVehiculo v, BZCintaRunner c, float kmh, float s) {
        float vObj = m_M.kmhRec;
        float thrFF = m_M.thr;
        float brkFF = m_M.brk;
        int gToma = MarchaMotoDe(m_M.gear);
        if (vObj < QUIETO_KMH) {
            m_VRef = vObj;
            m_Thr = thrFF;
            m_Brk = brkFF;
            m_GearCmd = gToma;
            m_Offset = 0;
            m_LugDesde = -1;
            return;
        }
        if (gToma < 1 && kmh < N_PISO_KMH) gToma = 1;
        // una rueda en el aire: los pedales grabados, sin correccion (Enduro 27/09: la delantera se despego y el control
        // dio gas 1,0)
        if (!v.RuedaContacto(0) || !v.RuedaContacto(1)) {
            if (!m_EnAire) {
                m_EnAire = true;
                m_AireVeces = m_AireVeces + 1;
                if (m_AireVeces <= 20) BZBusLog.Info("[MOTO] rueda en el AIRE a " + Red(kmh) + " km/h (arco " + Red(s) + " m): pedales de la toma");
            }
            m_VRef = vObj;
            m_Thr = thrFF;
            m_Brk = brkFF;
            if (m_GearCmd < 1) m_GearCmd = gToma;
            return;
        }
        m_EnAire = false;
        float vms = kmh / 3.6;
        float alcance = vms * vms / (2.0 * m_APrev) + 5.0;
        float vRef = c.VelAdmisibleKmh(s, alcance, m_APrev, vObj);
        float vRefSinGuarda = vRef;
        bool guardaActiva = false;
        m_VRefSinGuarda = vRef;
        m_GuardaVel = false;
        // la guarda de inclinacion: una referencia RELATIVA a la moto la seguia hasta 0 con freno delantero 1,0 inclinada
        // (laboratorio de motos, herramienta aparte, 27/09, dos motos trabadas despues de frenar en curva); solo por encima del piso, nunca por debajo de el, y
        // el freno que agrega, como mucho VEL_CURVA_FRENO
        if (kmh > VEL_CURVA_PISO_KMH && m_GuardaLat < 1.0) {
            float exceso = 1.0 - m_GuardaLat;
            float topeCurva = kmh - VEL_CURVA_KMH * exceso;
            if (topeCurva < VEL_CURVA_PISO_KMH) topeCurva = VEL_CURVA_PISO_KMH;
            if (topeCurva < vRef) {
                vRef = topeCurva;
                guardaActiva = true;
                m_GuardaVel = true;
            }
        }
        m_VRef = vRef;
        float thr = thrFF + VEL_KT * (vRef - kmh);
        float brk = 0;
        if (kmh > vRef + VEL_BANDA) {
            brk = VEL_KB * (kmh - vRef - VEL_BANDA);
            if (kmh >= vObj && brkFF > brk) brk = brkFF;
            thr = 0;
        } else if (brkFF > 0.05) {
            if (kmh >= vObj) {
                brk = brkFF;
                thr = 0;
            }
        }
        if (kmh < ARRANQUE_KMH && vRef >= ARRANQUE_VREF_KMH && gToma >= 1 && brk <= 0 && thr < ARRANQUE_THR) thr = ARRANQUE_THR;
        if (thr < 0) thr = 0;
        if (thr > 1) thr = 1;
        if (brk < 0) brk = 0;
        if (brk > 1) brk = 1;
        if (guardaActiva) {
            float brkSin = VEL_KB * (kmh - vRefSinGuarda - VEL_BANDA);
            if (brkSin < 0) brkSin = 0;
            if (kmh >= vObj && brkFF > brkSin) brkSin = brkFF;
            float brkTope = VEL_CURVA_FRENO;
            if (brkSin > brkTope) brkTope = brkSin;
            if (brk > brkTope) brk = brkTope;
        }
        m_Thr = thr;
        m_Brk = brk;
        m_GearCmd = MarchaPara(v, gToma, kmh, vRef, s);
    }

    int MarchaPara(BZVehiculo v, int gToma, float kmh, float vRef, float s) {
        if (gToma < 1) {
            m_Offset = 0;
            return gToma;
        }
        if (gToma != m_UltMarchaToma) {
            m_UltMarchaToma = gToma;
            m_Offset = 0;
            m_LugDesde = -1;
        }
        int g = gToma - m_Offset;
        if (g < 1) g = 1;
        float piso = m_RpmRal * VEL_PISO;
        bool enPiso = v.Rpm() < piso;
        float kM = KDe(g);
        if (kM > 0 && kM * kmh < piso) enPiso = true;
        if (enPiso && vRef > kmh + VEL_LUG_KMH && g > 1) {
            if (m_LugDesde < 0) m_LugDesde = m_T;
            if (m_T - m_LugDesde >= VEL_LUG_S && m_T - m_UltCambio >= VEL_CAMBIO_S) {
                float kAbajo = KDe(g - 1);
                if (kAbajo <= 0 || kAbajo * kmh <= m_RpmCorte * VEL_CORTE_FRAC) {
                    m_Offset = m_Offset + 1;
                    g = g - 1;
                    m_Bajadas = m_Bajadas + 1;
                    m_UltCambio = m_T;
                    m_LugDesde = -1;
                    if (m_Bajadas <= 20) BZBusLog.Info("[MOTO] rpm en el PISO a " + Red(kmh) + " km/h con la referencia a " + Red(vRef) + " (arco " + Red(s) + " m): baja a " + g + " (la toma va en " + gToma + ")");
                }
            }
        } else {
            m_LugDesde = -1;
        }
        if (m_Offset > 0 && kmh >= vRef - VEL_ALCANZA_KMH) {
            float kT = KDe(gToma);
            if (kT <= 0 || kT * kmh >= piso) {
                m_Offset = 0;
                g = gToma;
            }
        }
        int sube = 0;
        while (sube < 6 && g < m_MarchaMax) {
            float kC = KDe(g);
            if (kC <= 0 || kC * kmh <= m_RpmCorte * VEL_CORTE_FRAC) break;
            g = g + 1;
            sube = sube + 1;
        }
        return g;
    }

    // la marcha se pide y se INSISTE hasta que GetGear() la devuelva (embrague pisado CLUTCH_S alrededor del cambio; a
    // los 20 pedidos se suelta por si es el embrague lo que la traba). Si el motor la devuelve a otra, se vuelve a pedir
    void AplicarMarcha(BZVehiculo v, int g) {
        if (g != m_UltGearCmd) {
            m_UltGearCmd = g;
            m_MarchaPedida = g;
            m_Pedidos = 0;
        }
        if (m_MarchaPedida != -99) {
            int actual = v.Marcha();
            if (actual == m_MarchaPedida && m_Pedidos > 0) {
                m_MarchaPedida = -99;
            } else {
                if (m_Pedidos < 20) {
                    v.Embrague(1.0);
                    m_ClutchHasta = m_T + CLUTCH_S;
                }
                if (m_Pedidos == 20 && m_ClutchHasta > 0) {
                    v.Embrague(0);
                    m_ClutchHasta = -1;
                }
                v.Poner(m_MarchaPedida);
                m_Pedidos = m_Pedidos + 1;
                if (m_Pedidos == 80) BZBusLog.Warn("[MOTO] la marcha " + m_MarchaPedida + " NO TOMA: 80 pedidos (GetGear dice " + actual + ")");
            }
        } else if (m_UltGearCmd != -99 && v.Marcha() != m_UltGearCmd) {
            m_Perdidas = m_Perdidas + 1;
            m_MarchaPedida = m_UltGearCmd;
            m_Pedidos = 0;
        }
        if (m_ClutchHasta > 0 && m_T >= m_ClutchHasta) {
            v.Embrague(0);
            m_ClutchHasta = -1;
        }
    }

    // ------------------------------------------------------------------------
    //  ATASCO: primero la reversa caminando (el destrabe); si no alcanza, se re-ubica adelante sobre la cinta
    // ------------------------------------------------------------------------
    void VigilarAtasco(BZVehiculo v, BZCintaRunner c, float s, float dt) {
        if (m_M.kmhRec < QUIETO_KMH || c.EnMeseta()) {
            m_SinAvance = 0;
            m_SUlt = s;
            return;
        }
        if (s > m_SUlt + 0.3) {
            m_SUlt = s;
            m_SinAvance = 0;
            return;
        }
        m_SinAvance = m_SinAvance + dt;
        if (m_SinAvance < ATASCO_S) return;
        m_SinAvance = 0;
        Destrabar(v, c, s, "trabada " + ATASCO_S + " s");
    }

    // ------------------------------------------------------------------------
    //  LA CAMINATA: los tramos caminados de la toma (columna 15) y la reversa del destrabe
    // ------------------------------------------------------------------------
    // los tramos de la cinta, una vez: frames seguidos con `camina` distinto de 0
    void PrepararCaminata(BZCintaRunner c) {
        m_CamA = new array<int>();
        m_CamB = new array<int>();
        int n = c.Frames();
        int k = 0;
        while (k < n) {
            if (c.CaminaDeFrame(k) == 0) {
                k = k + 1;
                continue;
            }
            int a = k;
            while (k + 1 < n && c.CaminaDeFrame(k + 1) != 0) k = k + 1;
            m_CamA.Insert(a);
            m_CamB.Insert(k);
            k = k + 1;
        }
        m_CamProx = 0;
        if (m_CamA.Count() > 0) BZBusLog.Info("[MOTO] la cinta trae " + m_CamA.Count() + " tramo(s) caminado(s): se reproducen por empuje, con la animacion de caminar");
        VelocidadDestrabe(c);
    }

    // la velocidad del destrabe, sin nombres de clase: la toma, el config o el defecto (con aviso)
    void VelocidadDestrabe(BZCintaRunner c) {
        float suma = 0;
        int cuantos = 0;
        int t;
        for (t = 0; t < m_CamA.Count(); t++) {
            int kV;
            for (kV = m_CamA.Get(t); kV <= m_CamB.Get(t); kV++) {
                suma = suma + Math.AbsFloat(c.CaminaDeFrame(kV));
                cuantos = cuantos + 1;
            }
        }
        if (cuantos > 0) {
            m_CamVDestrabe = Math.Clamp(suma / cuantos, CAM_V_MIN, CAM_V_MAX);
            m_CamVFuente = "los tramos caminados de la toma";
            return;
        }
        float vN = GetGame().ConfigGetFloat("CfgVehicles " + m_Clase + " SimulationModule neutralGearMaxSpeed");
        if (vN > 0.1) {
            m_CamVDestrabe = Math.Clamp(vN * CAM_VNEUTRA_FRAC, CAM_V_MIN, CAM_V_MAX);
            m_CamVFuente = "el config (neutralGearMaxSpeed " + Red(vN) + " x " + Red(CAM_VNEUTRA_FRAC) + ")";
            return;
        }
        m_CamVDestrabe = CAM_V_DEFECTO;
        m_CamVFuente = "el valor por defecto";
        BZBusLog.Warn("[MOTO] " + m_Clase + " no trae neutralGearMaxSpeed en el config ni la toma tiene tramos caminados: el destrabe camina a " + Red(CAM_V_DEFECTO) + " m/s (por defecto)");
    }

    // true = la caminata manejo este cuadro (un tramo de la toma o el destrabe)
    bool Caminando(BZVehiculo v, BZCintaRunner c, float dt) {
        if (!m_CamA) PrepararCaminata(c);
        if (m_CamModo == 0) {
            if (m_CamProx >= m_CamA.Count()) return false;
            float sIniC = c.SDeFrame(m_CamA.Get(m_CamProx));
            float sFinC = c.SDeFrame(m_CamB.Get(m_CamProx));
            float sAhoraC = c.SEst();
            if (sAhoraC > sFinC) {
                m_CamProx = m_CamProx + 1;   // quedo atras (una re-ubicacion lo salto)
                return false;
            }
            if (sAhoraC < sIniC - CAM_ENTRA_M) return false;
            EmpezarTramo(v, c);
        }
        return PasoCaminata(v, c, dt);
    }

    private void EmpezarComun(int modo, string motivo) {
        m_CamModo = modo;
        m_CamEmpujando = false;
        m_CamT = 0;
        m_CamG = CAM_G_INI;
        m_CamVF = 0;
        m_CamTEmp = 0;
        m_CamVSum = 0;
        m_CamVN = 0;
        m_CamLatMax = 0;
        m_CamRumboMax = 0;
        m_CamMotivo = motivo;
        m_CamW = 0;
        m_CamVol = 0;
        m_RollDes = 0;
        m_Mezcla = 0;
        m_LogEvento = "camina";
        if (modo == 2) m_LogEvento = "destrabe";
    }

    void EmpezarTramo(BZVehiculo v, BZCintaRunner c) {
        m_CamTramo = m_CamProx;
        m_CamProx = m_CamProx + 1;
        int a = m_CamA.Get(m_CamTramo);
        int b = m_CamB.Get(m_CamTramo);
        float largo = c.SDeFrame(b) - c.SDeFrame(a);
        float vSum = 0;
        int k;
        for (k = a; k <= b; k++) vSum = vSum + c.CaminaDeFrame(k);
        float vMed = vSum / (b - a + 1);
        float vAbsMed = Math.AbsFloat(vMed);
        if (vAbsMed < 0.3) vAbsMed = 0.3;
        m_CamK = a;
        m_CamSgn = 1;
        if (vMed < 0) m_CamSgn = -1;
        m_CamTope = 2.0 * largo / vAbsMed + CAM_TOPE_EXTRA_S;
        m_CamSBase = c.SDeFrame(a);
        m_Caminatas = m_Caminatas + 1;
        int nro = m_CamTramo + 1;
        EmpezarComun(1, "tramo caminado " + nro);
        string sentido = "hacia adelante, con la animacion de caminar";
        if (vMed < 0) sentido = "hacia ATRAS, con la animacion de caminar";
        string l = "[MOTO] CAMINA el tramo " + nro + " de la toma: arco " + Red(c.SDeFrame(a)) + " a " + Red(c.SDeFrame(b)) + " m (" + Red(largo) + " m) ";
        l = l + sentido + ", a " + Red(vMed) + " m/s como la toma, por empuje";
        BZBusLog.Info(l);
    }

    // la REVERSA del destrabe: camina DESTRABE_M hacia atras sobre su rumbo y reintenta; a DESTRABE_MAX en el mismo lugar,
    // la re-ubicacion
    void Destrabar(BZVehiculo v, BZCintaRunner c, float s, string motivo) {
        if (!m_CamA) PrepararCaminata(c);
        if (Math.AbsFloat(s - m_DestrabeSUlt) > DESTRABE_LUGAR_M) {
            m_DestrabeN = 0;
            m_DestrabeSIni = s;
        }
        if (m_DestrabeN >= DESTRABE_MAX) {
            m_DestrabeN = 0;
            // desde el arco de la primera traba (el destrabe camino hacia atras: el arco de ahora es menor)
            float sReub = s;
            if (m_DestrabeSIni > sReub) sReub = m_DestrabeSIni;
            Reubicar(v, c, sReub, motivo + " (ya camino " + DESTRABE_MAX + " veces hacia atras aca)");
            return;
        }
        m_DestrabeN = m_DestrabeN + 1;
        m_DestrabeSUlt = s;
        m_Destrabes = m_Destrabes + 1;
        vector fD = v.T().GetDirection();
        m_CamRumboDestrabe = Math.Atan2(fD[0], fD[2]) * Math.RAD2DEG;
        m_CamSBase = s;
        m_CamTope = DESTRABE_M / m_CamVDestrabe + CAM_TOPE_EXTRA_S;
        EmpezarComun(2, motivo);
        string ld = "[MOTO] DESTRABE (" + motivo + ") en el arco " + Red(s) + " m: camina " + Red(DESTRABE_M) + " m hacia ATRAS a " + Red(m_CamVDestrabe) + " m/s y reintenta";
        ld = ld + " (vez " + m_DestrabeN + " de " + DESTRABE_MAX + " en este lugar; despues, la re-ubicacion) · la velocidad sale de " + m_CamVFuente;
        BZBusLog.Warn(ld);
    }

    // el frame del tramo [a, b] mas cercano a la moto, alrededor del cursor (poco atras, mas adelante)
    int CamCercano(BZCintaRunner c, vector pos, int a, int b) {
        int desde = m_CamK - 10;
        if (desde < a) desde = a;
        int hasta = m_CamK + 60;
        if (hasta > b) hasta = b;
        int mejor = m_CamK;
        float mejorD = 1000000000;
        int k;
        for (k = desde; k <= hasta; k++) {
            vector pk = c.PosDeFrame(k);
            float dxk = pk[0] - pos[0];
            float dzk = pk[2] - pos[2];
            float dk = dxk * dxk + dzk * dzk;
            if (dk <= mejorD) {   // a igual distancia, el cuadro mas adelante (un final quieto repite la posicion)
                mejorD = dk;
                mejor = k;
            }
        }
        return mejor;
    }

    bool PasoCaminata(BZVehiculo v, BZCintaRunner c, float dt) {
        Transport tr = v.T();
        m_CamT = m_CamT + dt;
        float kmh = v.KmhAbs();
        m_Thr = 0;
        v.Gas(0);
        v.PonerPata(0);
        m_GearCmd = 0;   // N: la marcha REAL (AplicarMarcha no usa la convencion de la cinta, donde N = 1)
        AplicarMarcha(v, m_GearCmd);
        // primero quieta: si viene andando, frena
        if (!m_CamEmpujando) {
            if (kmh > CAM_ENTRA_KMH && m_CamT < m_CamTope) {
                m_Brk = 0.6;
                v.Frenar(m_Brk);
                v.Volante(0);
                VolcarLog(v, c, c.SEst(), kmh);
                return true;
            }
            m_CamEmpujando = true;
            m_CamP0 = tr.GetPosition();
            m_CamTickIni = GetGame().GetTickTime();
        }
        m_Brk = 0;
        v.Frenar(0);
        m_CamTEmp = m_CamTEmp + dt;
        vector pos = tr.GetPosition();
        float dx0 = pos[0] - m_CamP0[0];
        float dz0 = pos[2] - m_CamP0[2];
        float movido = Math.Sqrt(dx0 * dx0 + dz0 * dz0);
        // el objetivo: la velocidad firmada, el rumbo y (en un tramo de la toma) la linea grabada
        float vObj = -m_CamVDestrabe;
        float rumboObj = m_CamRumboDestrabe;
        float steerObj = 0;
        float latE = 0;
        vector nLin = "0 0 0";   // la normal a la linea, a la derecha
        float sAhora = m_CamSBase;
        bool llego = false;
        if (m_CamModo == 1) {
            int a = m_CamA.Get(m_CamTramo);
            int b = m_CamB.Get(m_CamTramo);
            int k = CamCercano(c, pos, a, b);
            m_CamK = k;
            vObj = c.CaminaDeFrame(k);
            rumboObj = c.RumboDeFrame(k);
            steerObj = SteerDeRueda(c.FwDeFrame(k), kmh);   // el angulo de rueda grabado (los toques -1/0 no se reproducen)
            sAhora = c.SDeFrame(k);
            // la linea: la tangente de la toma en k (de k a k+1; en el ultimo, de k-1 a k)
            int k0 = k;
            int k1 = k + 1;
            if (k1 > b) {
                k0 = k - 1;
                k1 = k;
            }
            if (k0 < a) k0 = a;
            vector p0 = c.PosDeFrame(k0);
            vector p1 = c.PosDeFrame(k1);
            float tx = p1[0] - p0[0];
            float tz = p1[2] - p0[2];
            float tl = Math.Sqrt(tx * tx + tz * tz);
            if (tl > 0.001) {
                tx = tx / tl;
                tz = tz / tl;
                float rx = pos[0] - p0[0];
                float rz = pos[2] - p0[2];
                latE = rx * tz - rz * tx;   // + a la derecha de la tangente
                nLin = Vector(tz, 0, -tx);
            }
            vector pFin = c.PosDeFrame(b);
            float dxF = pFin[0] - pos[0];
            float dzF = pFin[2] - pos[2];
            if (k >= b || Math.Sqrt(dxF * dxF + dzF * dzF) <= CAM_FIN_M) llego = true;
            // o CRUZO el plano de la pose final en el sentido de la caminata. La bola de CAM_FIN_M no alcanza cuando la moto
            // viene corrida de la linea (hacia atras, 0,2-0,3 m) y el cursor no llega al ultimo cuadro si el tramo termina
            // quieto en la toma (cuadros repetidos): medido, dos tramos hacia atras siguieron hasta el tope, 8,8 m de mas
            float hFinC = c.RumboDeFrame(b) * Math.DEG2RAD;
            float pasadaC = -(dxF * Math.Sin(hFinC) + dzF * Math.Cos(hFinC)) * m_CamSgn;   // + = ya paso la pose final
            if (pasadaC >= -CAM_FIN_PASADA_M) llego = true;
        } else {
            if (movido >= DESTRABE_M) llego = true;
        }
        // el rumbo contra el grabado (o el del destrabe)
        float hObj = rumboObj * Math.DEG2RAD;
        vector fwd = tr.GetDirection();
        float eH = Envolver(hObj - Math.Atan2(fwd[0], fwd[2]));
        float eHDeg = Math.AbsFloat(eH) * Math.RAD2DEG;
        // el rumbo objetivo CON la correccion del lateral (la guarda y el registro miden contra el grabado, eH)
        float giroLat = Math.Atan2(latE, CAM_LAT_LOOK_M);
        float giroMax = CAM_LAT_RUMBO_MAX_DEG * Math.DEG2RAD;
        if (giroLat > giroMax) giroLat = giroMax;
        if (giroLat < -giroMax) giroLat = -giroMax;
        float eHCmd = Envolver(hObj - giroLat - Math.Atan2(fwd[0], fwd[2]));
        // las guardas: la inclinacion, la linea, el rumbo, un contacto con un objeto (desde que empezo a empujar) y el tope
        string corta = "";
        float rollC = Math.AbsFloat(v.Roll());
        float tCo = v.ContactoObjetoT();
        if (rollC > CAM_ROLL_MAX_DEG) corta = "se inclino " + Red(rollC) + " grados";
        else if (Math.AbsFloat(latE) > CAM_LAT_MAX_M) corta = "se fue " + Red(latE) + " m de la linea grabada";
        else if (eHDeg > CAM_RUMBO_MAX_DEG) corta = "el rumbo se fue " + Red(eHDeg) + " grados";
        else if (tCo > m_CamTickIni + 0.5 && GetGame().GetTickTime() - tCo <= CAM_CONTACTO_S) corta = "contacto: " + v.ContactoUltimo(1.0);
        else if (m_CamT >= m_CamTope) corta = "tope de " + Red(m_CamTope) + " s";
        if (llego || corta != "") {
            TerminarCaminata(v, c, llego, corta, movido);
            return true;
        }
        // el EMPUJE en lazo cerrado: la velocidad horizontal sobre el rumbo (con su signo) + la correccion lateral
        vector dirH = Vector(Math.Sin(hObj), 0, Math.Cos(hObj));
        float sgn = 1;
        if (vObj < 0) sgn = -1;
        vector dirMov = dirH * sgn;
        vector vel = GetVelocity(tr);
        float vMed = vel[0] * dirMov[0] + vel[2] * dirMov[2];
        float vAbs = Math.AbsFloat(vObj);
        if (m_CamTEmp < CAM_ARRANQUE_S) vAbs = CAM_ARRANQUE_V;
        else if (m_CamTEmp < CAM_ARRANQUE_S + CAM_RAMPA_S) vAbs = CAM_ARRANQUE_V + (vAbs - CAM_ARRANQUE_V) * (m_CamTEmp - CAM_ARRANQUE_S) / CAM_RAMPA_S;
        m_CamVF = m_CamVF + (vMed - m_CamVF) * dt / (CAM_TAU_FILTRO + dt);
        float vRefG = vAbs;
        if (vRefG < 0.2) vRefG = 0.2;
        m_CamG = Math.Clamp(m_CamG + CAM_KG * (vAbs - m_CamVF) / vRefG * dt, CAM_G_MIN, CAM_G_MAX);
        float vCmd = Math.Clamp(vAbs * m_CamG, 0, CAM_VCMD_MAX);
        float vLat = Math.Clamp(-CAM_KLAT * latE, -CAM_VLAT_MAX, CAM_VLAT_MAX);
        // el RUMBO: la toma gira tambien empujando (un tramo hacia atras dobla 62 grados). La velocidad angular en Y no
        // alcanza con las ruedas en el piso (medido: 1 rad/s pedido, 0,08 rad/s real: la moto siguio derecha y la guarda
        // de rumbo corto a los 45 grados). El rumbo se IMPONE por la orientacion, cuadro a cuadro, con el cabeceo y el
        // rolido que deja la fisica, y la velocidad del empuje se fija despues, coherente con la pose (la receta que
        // mueve un Transport por script). El feedforward es la tasa de giro de la toma (dpsi/ds de la cinta x la
        // velocidad; vale en los dos sentidos: el arco crece con el tiempo) y el proporcional cierra sobre el rumbo
        // grabado en el cuadro mas cercano
        float ffW = 0;
        if (m_CamModo == 1) ffW = c.CurvaturaEn(sAhora, CAM_CURV_MITAD_M) * Math.AbsFloat(vMed);
        m_CamW = Math.Clamp(ffW + CAM_KRUMBO * eHCmd, -CAM_WMAX, CAM_WMAX);
        vector oriC = tr.GetOrientation();
        float yawC = oriC[0] + m_CamW * Math.RAD2DEG * dt;
        while (yawC > 180.0) yawC = yawC - 360.0;
        while (yawC < -180.0) yawC = yawC + 360.0;
        oriC[0] = yawC;
        tr.SetOrientation(oriC);
        dBodyActive(tr, ActiveState.ACTIVE);   // la orientacion impuesta deja el cuerpo dormido al soltarlo (medido: 6 s sin arrancar)
        vector nueva = dirMov * vCmd + nLin * vLat;
        nueva[1] = vel[1];
        SetVelocity(tr, nueva);
        vector w = dBodyGetAngularVelocity(tr);
        w[1] = m_CamW;
        dBodySetAngularVelocity(tr, w);
        m_CamVol = steerObj;
        v.Volante(steerObj);
        m_CamVSum = m_CamVSum + vMed;
        m_CamVN = m_CamVN + 1;
        if (Math.AbsFloat(latE) > m_CamLatMax) m_CamLatMax = Math.AbsFloat(latE);
        if (eHDeg > m_CamRumboMax) m_CamRumboMax = eHDeg;
        // la caja negra en la caminata (la cinta no se proyecta): kmh = la velocidad MEDIDA antes del empuje de este cuadro
        // (la de despues del SetVelocity es el mando), v_ref = la objetivo con la rampa, v_ref_sin_guarda y kmh_toma = la
        // de la toma, lat = el lateral a la linea de la toma, e_psi = el error de rumbo
        m_VRef = vAbs * 3.6;
        m_VRefSinGuarda = Math.AbsFloat(vObj) * 3.6;
        m_M.kmhRec = Math.AbsFloat(vObj) * 3.6;
        m_M.latM = latE;
        m_Epsi = -eH;
        VolcarLog(v, c, sAhora, Math.AbsFloat(vMed) * 3.6);
        return true;
    }

    private void TerminarCaminata(BZVehiculo v, BZCintaRunner c, bool llego, string corta, float movido) {
        Transport tr = v.T();
        float vMedia = 0;
        if (m_CamVN > 0) vMedia = m_CamVSum / m_CamVN;
        // sin la velocidad del empuje: desde aca maneja lo de siempre
        vector velF = GetVelocity(tr);
        velF[0] = 0;
        velF[2] = 0;
        SetVelocity(tr, velF);
        // despierta: con la orientacion impuesta cuadro a cuadro el cuerpo quedaba dormido y la moto no arrancaba (gas,
        // 1a, rpm 1400, 0 km/h con las ruedas apoyadas, 6 s hasta el destrabe); lo mismo que hace el motor con la moto
        dBodyActive(tr, ActiveState.ACTIVE);
        string que = "listo";
        if (!llego) que = "CORTADO (" + corta + ")";
        string l = "[MOTO] " + m_CamMotivo + " " + que + ": " + Red(movido) + " m en linea recta en " + Red(m_CamT) + " s, v media " + Red(vMedia) + " m/s";
        l = l + " · lateral max " + Red(m_CamLatMax) + " m · rumbo max " + Red(m_CamRumboMax) + " grados";
        int modo = m_CamModo;
        m_CamModo = 0;
        m_CamEmpujando = false;
        m_UltGearCmd = -99;   // la marcha se vuelve a pedir desde cero (a 1a: la arrancada de siempre)
        m_Delta = 0;
        m_RollPrev = -999;
        m_RollTasa = 0;
        m_CamW = 0;
        m_CamVol = 0;
        m_SinAvance = 0;
        m_ChoqueDesde = -1;
        m_FueraT = 0;
        m_LogEvento = "";
        bool porContacto = (!llego && corta.IndexOf("contacto") == 0);
        if (modo == 1) {
            int bT = m_CamB.Get(m_CamTramo);
            float sB = c.SDeFrame(bT);
            vector pFinT = c.PosDeFrame(bT);
            vector pAhT = tr.GetPosition();
            float dxT = pFinT[0] - pAhT[0];
            float dzT = pFinT[2] - pAhT[2];
            l = l + " · llego al arco " + Red(c.SDeFrame(m_CamK)) + " de " + Red(sB) + " m, a " + Red(Math.Sqrt(dxT * dxT + dzT * dzT)) + " m de la pose final";
            c.AnclarEnS(sB);
            m_SUlt = sB;
            BZBusLog.Info(l);
            if (porContacto) Reubicar(v, c, sB, "contacto caminando");
            return;
        }
        float sVuelta = m_CamSBase - movido;
        if (sVuelta < 0) sVuelta = 0;
        c.AnclarEnS(sVuelta);
        m_SUlt = sVuelta;
        BZBusLog.Info(l + " · reintenta desde el arco " + Red(sVuelta) + " m");
        if (porContacto) Reubicar(v, c, m_CamSBase, "el destrabe choco (" + corta + ")");
    }

    // LA MAQUINARIA DE "NO QUEDARSE DETENIDA" (sin reversa): la moto se pone sobre la cinta REUB_ADELANTE_M despues de
    // sBase, quieta y con el rumbo de la toma; hasta REUB_MAX veces por corrida (atasco, fuera de la linea, choque),
    // despues la ruta termina INCOMPLETE
    void Reubicar(BZVehiculo v, BZCintaRunner c, float sBase, string motivo) {
        if (m_Reubicaciones >= REUB_MAX) {
            Terminar(v, "ATASCADA: " + REUB_MAX + " re-ubicaciones (la ultima por " + motivo + ", arco " + Red(sBase) + " de " + Red(c.SFin()) + " m)", false);
            return;
        }
        float sDest = sBase + REUB_ADELANTE_M;
        if (sDest > c.SFin()) sDest = c.SFin();
        vector pos;
        float rumbo;
        if (!c.PoseEnS(sDest, pos, rumbo)) return;
        Transport tr = v.T();
        pos[1] = pos[1] + 0.2;
        tr.SetPosition(pos);
        tr.SetOrientation(Vector(rumbo, 0, 0));
        SetVelocity(tr, vector.Zero);
        dBodySetAngularVelocity(tr, vector.Zero);
        c.AnclarEnS(sDest);
        m_Reubicaciones = m_Reubicaciones + 1;
        m_SUlt = sDest;
        m_SinAvance = 0;
        m_Delta = 0;
        m_RollPrev = -999;
        m_RollTasa = 0;
        m_UltGearCmd = -99;
        m_LogEvento = "reubica";
        BZBusLog.Warn("[MOTO] " + motivo + " en el arco " + Red(sBase) + " m (sin reversa): RE-UBICADA " + REUB_ADELANTE_M + " m adelante sobre la cinta (" + m_Reubicaciones + " de " + REUB_MAX + ")");
    }

    // ★ LA LLEGADA NO ES EL FIN. Si la moto llego (ok), el fin queda PENDIENTE: Paso la sostiene frenada y la cola
    // mide el reposo (0,5 s quieta, tope LOG_COLA_TOPE_S); DecidirFin emite ahi, con las tres condiciones del auto
    // (reposo <= ACEPTA_ENDPOINT_M, sin choque de frente contra un objeto, sin re-ubicacion ni destrabe) y el REPOSO
    // adentro del texto. Antes el FIN salia a 0,2-0,3 km/h, 0,4 s antes del reposo, y el consumidor lo recibia antes
    // de que la moto estuviera quieta. Un fin que no es llegada (ATASCADA) sigue saliendo en el acto.
    void Terminar(BZVehiculo v, string motivo, bool ok) {
        if (m_Termino || m_FinPend) return;
        if (ok) {
            m_FinPend = true;
            m_FinPendMotivo = motivo;
            v.Gas(0);
            v.Frenar(1.0);
            m_LogCola = true;
            m_LogColaT = 0;
            m_LogQuietoT = 0;
            m_LogEvento = "fin";
            return;
        }
        Cerrar(v, motivo, false, true);
    }

    private void DecidirFin(BZVehiculo v, BZCintaRunner c, string reposo) {
        m_FinPend = false;
        m_FinDecidido = true;
        float dL = -1;
        float dR = 0;
        bool medido = MedirContraFinal(v, c, dL, dR);
        if (medido) m_ReposoFinM = dL;
        bool lejos = (!medido || dL > BZDriveTol.ACEPTA_ENDPOINT_M);
        int fis = m_Reubicaciones + m_Destrabes;
        bool ok = (!lejos && m_ChoquesObjeto == 0 && fis == 0);
        string fin = m_FinPendMotivo + " · " + reposo;
        if (!ok) {
            if (lejos) fin = fin + " · quedo a " + Red(dL) + " m de la pose final en reposo con tolerancia " + Red(BZDriveTol.ACEPTA_ENDPOINT_M) + " m";
            if (m_ChoquesObjeto > 0) fin = fin + " · choco de frente contra un objeto: " + m_ListaChoquesObj;
            if (fis > 0) fin = fin + " · intervencion fisica: " + m_Reubicaciones + " re-ubicacion(es), " + m_Destrabes + " destrabe(s) caminando";
        }
        Cerrar(v, fin, ok, false);
    }

    private void Cerrar(BZVehiculo v, string motivo, bool ok, bool armarCola) {
        if (m_Termino) return;
        m_Termino = true;
        m_FinOk = ok;
        m_Fin = motivo;
        v.Gas(0);
        v.Frenar(1.0);
        if (m_SinReloj) m_Fin = m_Fin + " (ruta sin la medicion del reloj de la toma)";
        string lt = "[MOTO] TERMINA: " + m_Fin + " · lateral max " + Red(m_LatMax) + " m · saturada " + m_SatEpisodios + " episodio(s) · bajadas de marcha " + m_Bajadas;
        lt = lt + " · marcha perdida por el motor " + m_Perdidas + " · rueda en el aire " + m_AireVeces + " · caidas " + m_Caidas + " · re-ubicaciones " + m_Reubicaciones;
        lt = lt + " · fuera de la linea " + m_FueraVeces + " · choques " + m_Choques + " · tramos caminados " + m_Caminatas + " · destrabes " + m_Destrabes;
        BZBusLog.Info(lt);
        if (!armarCola) return;   // el reposo ya se midio (el veredicto salio de ahi)
        // hasta el reposo (Cola, desde el servicio): la caja negra, si esta abierta, y la linea REPOSO, siempre
        m_LogCola = true;
        m_LogColaT = 0;
        m_LogQuietoT = 0;
        m_LogEvento = "fin";
    }

    // donde quedo contra la pose final de la toma: distancia y rumbo
    string PoseContraFinal(BZVehiculo v, BZCintaRunner c) {
        float dL = -1;
        float dRumbo = 0;
        if (!c) return "sin la pose final de la toma";
        if (!v.T()) return "sin vehiculo";
        if (!MedirContraFinal(v, c, dL, dRumbo)) return "sin la pose final de la toma";
        return "a " + Red(dL) + " m de la pose final de la toma, rumbo " + Red(dRumbo) + " grados";
    }

    // la medida: distancia (m) y rumbo (grados, envuelto) contra la pose final de la toma; false sin pose o sin vehiculo
    bool MedirContraFinal(BZVehiculo v, BZCintaRunner c, out float dL, out float dRumbo) {
        vector posF;
        float rumboF;
        if (!c || !c.PoseEnS(c.SFin(), posF, rumboF)) return false;
        Transport trL = v.T();
        if (!trL) return false;
        vector pL = trL.GetPosition();
        float dxL = pL[0] - posF[0];
        float dzL = pL[2] - posF[2];
        dL = Math.Sqrt(dxL * dxL + dzL * dzL);
        vector fL = trL.GetDirection();
        float rumboL = Math.Atan2(fL[0], fL[2]) * Math.RAD2DEG;
        dRumbo = rumboL - rumboF;
        while (dRumbo > 180) dRumbo = dRumbo - 360;
        while (dRumbo < -180) dRumbo = dRumbo + 360;
        return true;
    }

    // ------------------------------------------------------------------------
    //  LA CAJA NEGRA de la moto: BZ12_LOG_<ruta>_<hhmmss>.csv en el profile, una fila por cuadro (como la del auto, con
    //  las columnas de la moto). Se abre al armar el conductor y se cierra en el REPOSO despues del FIN (Cola), o duro.
    // ------------------------------------------------------------------------
    void AbrirLog(string etiqueta) {
        CerrarLogYa();
        if (!BZCajaNegra.Encendida()) return;   // apagadas por defecto (BlackBox en BZAutoDrive_settings.json)
        int lhM = 0;
        int lmM = 0;
        int lsM = 0;
        GetHourMinuteSecondUTC(lhM, lmM, lsM);
        string hhM = Pad2(lhM) + Pad2(lmM) + Pad2(lsM);
        string lpath = BZCarpetas.Profile() + "BZ12_LOG_" + etiqueta + "_" + hhM + ".csv";
        m_LogFh = OpenFile(lpath, FileMode.WRITE);
        if (m_LogFh == 0) {
            BZBusLog.Warn("[MOTO] caja negra: no se pudo abrir " + lpath);
            return;
        }
        string cab = "t,px,pz,arco,sfin,lat,e_psi,kmh,kmh_toma,v_ref,v_ref_sin_guarda,thr,brk,gear_cmd,gear,rpm";
        cab = cab + ",ff,corr_crudo,corr,delta,roll,roll_toma,roll_umbral,roll_frac,guarda_lat,guarda_vel,w0,w1,evento";
        cab = cab + ",r_moto,r_toma,amort";   // el amortiguamiento de la ley (rad/s, rad/s, rad)
        cab = cab + ",tag_caminando";          // el grafo del chofer en el estado de caminar (1/0; -1 sin chofer)
        cab = cab + ",roll_des,mezcla,w_cmd,vol_cam";   // la cascada (roll objetivo, mezcla) y la caminata (giro impuesto rad/s, manubrio)
        FPrintln(m_LogFh, cab);
        m_LogOn = true;
        m_LogCola = false;
        BZCajaNegra.Anotar(etiqueta, "BZ12_LOG_" + etiqueta + "_" + hhM + ".csv");
        BZBusLog.Info("[MOTO] caja negra abierta: " + lpath);
    }

    // la etiqueta TagVehicleWalking del grafo del chofer: 1 si esta en el estado de caminar la moto, 0 si no, -1 sin chofer
    string TagCaminando(Transport trT) {
#ifdef DAYZ_1_29
        return "-1";   // HumanAnimInterface.IsTag es de la 1.30 (en la 1.29 no existe y tumbaria el modulo)
#else
        Human ch = trT.CrewMember(0);
        if (!ch) return "-1";
        HumanAnimInterface ai = ch.GetAnimInterface();
        if (!ai) return "-1";
        if (ch != m_TagChofer) {
            m_TagChofer = ch;
            m_TagCamId = ai.BindTag("TagVehicleWalking");
        }
        if (m_TagCamId < 0) return "-1";
        if (ai.IsTag(m_TagCamId)) return "1";
        return "0";
#endif
    }

    void VolcarLog(BZVehiculo v, BZCintaRunner c, float s, float kmh) {
        if (!m_LogOn) return;
        if (!c) return;
        Transport trV = v.T();
        if (!trV) return;
        vector pV = trV.GetPosition();
        int w0 = 0;
        int w1 = 0;
        if (v.RuedaContacto(0)) w0 = 1;
        if (v.RuedaContacto(1)) w1 = 1;
        int gvInt = 0;
        if (m_GuardaVel) gvInt = 1;
        string f1 = Red3(m_T) + "," + Red(pV[0]) + "," + Red(pV[2]) + "," + Red(s) + "," + Red(c.SFin()) + "," + Red(m_M.latM) + "," + Red(m_Epsi * Math.RAD2DEG);
        string f2 = "," + Red(kmh) + "," + Red(m_M.kmhRec) + "," + Red(m_VRef) + "," + Red(m_VRefSinGuarda) + "," + Red(m_Thr) + "," + Red(m_Brk);
        string f3 = "," + m_GearCmd + "," + v.Marcha() + "," + Red(v.Rpm()) + "," + Red(m_FF) + "," + Red(m_CorrCrudo) + "," + Red(m_Corr);
        string f4 = "," + Red(m_Delta) + "," + Red(v.Roll()) + "," + Red(m_RollToma) + "," + Red(m_RollUmbral) + "," + Red(m_RollFrac) + "," + Red(m_GuardaLat);
        string f5 = "," + gvInt + "," + w0 + "," + w1 + "," + m_LogEvento;
        f5 = f5 + "," + Red3(m_RMoto) + "," + Red3(m_RToma) + "," + Red3(m_Amort) + "," + TagCaminando(trV);
        f5 = f5 + "," + Red(m_RollDes) + "," + Red(m_Mezcla) + "," + Red3(m_CamW) + "," + Red(m_CamVol);
        FPrintln(m_LogFh, f1 + f2 + f3 + f4 + f5);
        m_LogEvento = "";
    }

    bool LogEnCola() { return m_LogCola; }

    // despues del FIN: filas hasta el reposo (|v| < LOG_COLA_QUIETO_KMH sostenido LOG_COLA_QUIETO_S) o LOG_COLA_TOPE_S
    void Cola(BZVehiculo v, BZCintaRunner c, float dt) {
        if (!m_LogCola) return;
        float kmhC = v.KmhAbs();
        VolcarLog(v, c, m_SArco, kmhC);
        m_LogColaT = m_LogColaT + dt;
        if (kmhC < LOG_COLA_QUIETO_KMH) m_LogQuietoT = m_LogQuietoT + dt;
        else m_LogQuietoT = 0;
        if (m_LogQuietoT >= LOG_COLA_QUIETO_S || m_LogColaT >= LOG_COLA_TOPE_S) {
            string medida = "REPOSO tras " + Red(m_LogColaT) + " s: " + PoseContraFinal(v, c) + ", a " + Red(kmhC) + " km/h";
            if (m_LogQuietoT < LOG_COLA_QUIETO_S) medida = medida + " (todavia rodando al medir)";
            string reposo = "[MOTO] " + medida;
            if (!m_FinPend) reposo = "[MOTO] despues del FIN: " + medida;
            BZBusLog.Info(reposo);
            if (m_FinPend) DecidirFin(v, c, medida);
            CerrarLogYa();
        }
    }

    void CerrarLogYa() {
        m_LogCola = false;
        if (!m_LogOn) return;
        CloseFile(m_LogFh);
        m_LogOn = false;
    }

    static string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }

    static string Red3(float x) {
        float r = Math.Round(x * 1000) / 1000;
        return r.ToString();
    }
}
