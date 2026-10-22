// ============================================================================
// BZCintaRunner.c - BZ_AutoDrive 1.2 -- LA CINTA (E2, 565)
//
//  El reproductor espacial de la toma FULL-RES: en cada tick de 40 Hz aplica el
//  INPUT que el usuario tenia EN ESTE PUNTO del camino (rueda LOGRADA + gas + freno +
//  MARCHA) y deja que la fisica del motor maneje (estilo chofer nativo de Arma).
//  E2 = iman OFF: este modulo mide la deriva open-loop real; el iman (E3) se
//  calibra con ese numero.
//
// ⚠⚠⚠ EL MODELO ES ESPACIAL, NO DE HZ (el usuario, ). El indice es el ARCO:
//  la cinta se consulta por PROYECCION de la posicion del vehiculo (cursor
//  monotono con cota de pasos -- la cicatriz del banco: un cursor sin cota se
//  colgaba con trayectorias divergentes). Los 40 Hz de captura son solo la
//  densidad de la cinta, no un reloj: si el vehiculo va 2% mas lento, lee el
//  input del PUNTO, no del segundo.
//
//  ⚠⚠ LA TEXTURA ES SEÑAL, NO RUIDO. La rueda full-res trae las micro-
//  correcciones del humano (~2/s a 0.9-1.5 grados -- medido en la toma 06):
//  el motor fino del "giro perfecto", justo lo que la decimacion destruia.
//  Por eso la interpolacion es LINEAL entre frames vecinos y NO hay ningun
//  suavizado aca. El unico filtro legitimo es el del propio motor (su rate de
//  Steering), que es el mismo que produjo la señal al grabar.
//
//  ⚠ LA RUEDA ES front_wheel_deg (LOGRADA), no la tecla (cicatriz col11:
//  el usuario maneja a pulsos binarios; la señal continua es la rueda). Convencion de
//  signo verificada contra la toma: curva DERECHA (kappa +) <-> rueda +6 deg =
//  la misma del steering normalizado (+ = derecha).
//
//  MODULO de actuacion directa (como los POSE): produce inputs, no consulta a
//  la DerivationEngine. El arbitro/Controller le da la mano SOLO en
//  CRUISE/APPROACH forward; MANEUVER/ENDPOINT/reversa/paradas siguen siendo de
// sus duenios (los handoffs del diseño, ).
// ============================================================================

class BZCintaMuestra
{
    float fwDeg;
    float thr;
    float brk;
    int   gear;
    float distM;    // distancia del vehiculo al ANCLA = LA deriva (el instrumento
                    // de E2; se imprime en DIRECCION como `cinta`). ⚠ Puede incluir
                    // residuo LONGITUDINAL si el cursor se desfasa (visto en el run
                    // 16-06: meseta ~6 m con lat de ruta ~0 = cursor corrido, no
                    // deriva lateral). Para el iman esta latM, que es solo lateral.
    float kmhRec;   // velocidad grabada en el punto (diagnostico)
    // REVERSA: fase del frame — gear grabado 0 = marcha atras (asi
    // codifica DayZ la R; medido: REVrecta 813/849 frames gear 0). El controller
    // chequea coherencia con el leg y espeja el termino de rumbo del iman.
    bool enReversa;
    // el clamp de borde RETUVO el cursor en esta consulta: la cinta no tiene mas filas de esta fase (el leg se agoto)
    bool borde;
    // E3: el error que el iman corrige, FIRMADO.
    float latM;       // lateral puro a la linea grabada; + = vehiculo a la DERECHA
    // MOTO (columnas opcionales 12 y 13 de la cinta): el comando de direccion grabado (-9 = la cinta no lo trae) y
    // si el cursor esta en la fila donde la toma retoma despues de un tramo no reproducible (la moto empujada en N)
    float steer = -9;
    int   corte = 0;
    // MOTO (columna opcional 14): la inclinacion de la toma en el punto (grados; -99 = la cinta no la trae). Las guardas
    // de inclinacion del conductor de moto son relativas a ella: lo que el humano inclino ahi se permite
    float roll = -99;
    float headingDeg; // rumbo grabado en el punto (convencion atan2(dx,dz), grados
                      // — la MISMA de st.yaw=BZMath.HeadingXZ, verificada contra la
                      // toma: err mediana 0.03 deg)
}

class BZCintaRunner
{
    // Escaneo del cursor: cota de pasos hacia adelante por tick. A 65 m/s y 40 Hz
    // el vehiculo avanza 1.6 m = ~7 frames de cinta a alta velocidad; 60 cubre
    // saltos de proyeccion sin permitir que el cursor se teletransporte a otra
    // parte de la ruta (cruces/lazos).
    private const int   COTA_PASOS = 60;
    // Perdida de cinta: la muestra deja de ser representativa
    // cuando el CURSOR esta desincronizado (residuo LONGITUDINAL > PERDIDA_M) o el
    // vehiculo se fue de verdad (|lateral| > PERDIDA_LAT_M). La deriva lateral
    // intermedia NO suelta la cinta: es recuperable y el fallback reactivo a alta
    // velocidad con metros de error es lo que producia los fly-offs de 31-55 m.
    private const float PERDIDA_M = 6.0;
    private const float PERDIDA_LAT_M = 12.0;
    // LA MESETA GRABADA ES UN EVENTO TEMPORAL CON SALIDA (regla A1 de INTEGRIDAD_RUTA: toda
    // detencion tiene su salida). La toma de RUTA02 trae 35 detenciones del humano SIN marca de parada (paro a
    // marcar): v grabada 0 durante 0,15-6 s. El cursor es ESPACIAL: si el vehiculo se detiene EXACTAMENTE sobre
    // un frame de v=0, la cinta pide 0 y el vehiculo esta en 0: ningun detector ve nada raro y el bus queda
    // clavado para siempre (Vybor, wp 6408, 1,5 min hasta que el usuario lo paro a mano). Si alcanza a pasarlo,
    // la fila siguiente pide gas a fondo con el vehiculo quieto y el AR lo re-arranca (NO-FISICO en 799/1093/
    // 1263/3809). El usuario: "una detencion grabada es un DATO LEGITIMO, y va a haber mas" (convoys, controles).
    // AHORA: al entrar en una meseta (filas seguidas con v grabada < MESETA_KMH) el cursor se ANCLA en su ENTRADA
    // y cuenta consultas: una por frame grabado, a la misma tasa (40 Hz), asi "aca me detuve tanto" se reproduce
    // tal cual; cumplida, SALTA a la primera fila de salida. Un hold externo (BusDrive) no llama a Muestra mientras
    // dura: extiende la detencion sin tocar nada; al soltar, el controlador llama SoltarMeseta. Un re-anclaje
    // externo (siembra de fase, AnclarEnS, carga) la borra. La salida NO cruza de tramo (fase): ahi manda el leg.
    // Una meseta ya cumplida (m_MesetaUltFin) no se vuelve a entrar: el rescate por proyeccion podria caer en ella
    // (todas sus filas estan en el mismo punto) y el vehiculo se detendria otra vez, para siempre.
    // ⚠ 1 km/h y no 0,3: el criterio medido por el analista es "v grabada por debajo de lo que el vehiculo
    // puede arrancar": Novodmitrovsk clavo un bus con v grabada 0,02 y hay dos puntos mas entre 0,5 y 1. Es el mismo
    // umbral para FinMesetaAdelante/InicioMesetaAdelante (el re-arme de cae ~10 cm mas adelante, en la salida).
    private const float MESETA_KMH = 1.0;   // v grabada por debajo = detenido a efectos practicos
    // la FRENADA FINAL de la toma: el pedal grabado a fondo (>= FRENADA_PEDAL) hasta que la v grabada cae bajo
    // FRENADA_FIN_KMH; vale como medicion si arranca desde >= FRENADA_V0_KMH, recorre >= FRENADA_DS_M y dura >= FRENADA_CUADROS
    private const float FRENADA_PEDAL = 0.9;
    private const float FRENADA_FIN_KMH = 1.0;
    private const float FRENADA_V0_KMH = 8.0;
    private const float FRENADA_DS_M = 0.3;     // los umbrales del cusp: una parada fuerte desde 12 km/h dura 0,4-0,7 m
    private const int   FRENADA_CUADROS = 12;   // y 17-19 cuadros (medido: 20 la dejaba "sin medicion" y el endpoint iba con el config)
    private bool  m_FrenoTomaOk;
    private float m_FrenoTomaMs2 = -1;
    private float m_FrenoTomaV0Kmh;
    private float m_FrenoTomaDsM;
    private int m_MesetaIni = -1;           // frame de entrada de la meseta en curso (-1 = ninguna)
    private int m_MesetaFin = -1;           // ultimo frame de la meseta en curso
    private int m_MesetaLlamadas = 0;       // consultas reproducidas adentro
    private int m_MesetaUltFin = -1;        // ultima meseta cumplida o soltada: no se re-entra

    private ref array<float> m_S;      // arco de POSICIONES con interpolacion v-pesada
                                       // en las mesetas (escala del consumidor +
                                       // resolucion 40 Hz; ver el conversor E1)
    private ref array<float> m_X;
    private ref array<float> m_Z;
    private ref array<float> m_Y;      // altura GRABADA
    private ref array<float> m_H;      // rumbo grabado (grados, atan2(dx,dz)) — E3
    private ref array<float> m_Fw;
    private ref array<float> m_Thr;
    private ref array<float> m_Brk;
    private ref array<float> m_Kmh;
    private ref array<int>   m_Gear;
    private ref array<float> m_Str;    // MOTO: columna 12 (steering grabado); -9 si la cinta no la trae
    private ref array<int>   m_Corte;  // MOTO: columna 13 (1 = retoma despues de un tramo no reproducible)
    private ref array<float> m_Roll;   // MOTO: columna 14 (roll de la toma, grados); -99 si la cinta no la trae
    private ref array<float> m_Camina; // MOTO: columna 15 (la velocidad FIRMADA de la toma en un tramo caminado, m/s:
                                       // + adelante, - atras; 0 fuera de los tramos o si la cinta no la trae)
    private int  m_Count;
    private int  m_Cursor;
    private bool m_Valida;
    // Odometria del consultante: el arco estimado del vehiculo sobre la cinta, que
    // avanza con SU desplazamiento (continuo en server) y se RE-ANCLA por proyeccion
    // solo donde la posicion de la cinta es limpia (paso > ANCLA_PASO_M).
    private float  m_SEst;
    private vector m_PrevQ;
    private bool   m_PrevQOk;
    // ★★★ RESCATE DEL CURSOR. Medido en el run 16-06: la deriva se
    // ESTACIONA en ~6 m con lat de ruta ~0 (ej. wp709 lat +0.23, deriva 6.39)
    // durante cientos de wp = el cursor quedo corrido LONGITUDINALMENTE y la
    // ventana de re-anclaje [-3,+6] frames (~1-3 m) nunca alcanza el punto
    // verdadero: el ancla local clampea a t=0/1, se rechaza por la regla interior,
    // y manda la odometria pura para siempre — semi-perdida permanente justo bajo
    // PERDIDA_M. El rescate: tras RESCATE_TICKS consultas seguidas con distM >
    // RESCATE_M, UNA busqueda ancha (±COTA_PASOS, como el camino de teleport) con
    // la MISMA regla de ancla interior (la cicatriz del cursor congelado sigue
    // guardada). RESCATE_M por encima del andar tipico con iman (~1 m) y por
    // debajo de PERDIDA_M; RESCATE_TICKS ~0.3 s para no dispararse en excursiones
    // laterales breves (donde igual seria inocuo: encontraria la misma ancla).
    // ⚠ RESCATE_M=1.5: el espejo midio que el desfase por ARCO-FANTASMA (el eje s
    // del conversor suma zigzag de jitter sub-0.05 m que el vehiculo real no
    // recorre; +2.7 m acumulados en s 55-106 de la 06) ya mete ~4 deg de error de
    // fw con distM 2.0-2.5 — esperar a 2.5 dejaba 25 m manejando con esa lectura.
    // 1.5 queda por encima del andar tipico con iman (p90 lateral ~0.9 m del
    // banco); un disparo "de mas" en excursion genuina es inocuo (encuentra la
    // misma ancla). La RAIZ (limpiar el eje s en el conversor) queda anotada para
    // la reconversion de flota E1.
    private const float RESCATE_M = 1.5;
    private const int   RESCATE_TICKS = 12;
    // ⚠ EL RESCATE ACEPTA SEGMENTOS MAS FINOS (0.10 m = 2x el techo del jitter de
    // cuantizacion ~0.05). Medido en el espejo: a <36 km/h TODOS los pasos son
    // < ANCLA_PASO_M(0.25) y una zona lenta de 50 m es UN nudo denso sin ninguna
    // ancla posible — ni la ventana ancha podia re-sincronizar (7 rescates para
    // 2.7 m). El camino NORMAL sigue exigiendo 0.25 (conservador); solo la
    // busqueda de rescate (rara, gateada) baja a 0.10, con la MISMA regla de
    // ancla interior.
    private const float RESCATE_PASO_M = 0.10;
    // margen de ruido de la invariante EL-ANCLA-SOLO-ACERCA (ver
    // la aceptacion del ancla). 0.5 m > el jitter de posicion; muy por debajo
    // de los teleports de 5-10 m que mata.
    private const float ANCLA_ACERCA_MARGEN_M = 0.5;
    // BORDE AGOTADO (ver el bloque en Muestra). Fisicos: medio
    // segundo de retencion sostenida, 1 m de error, 30 cm de crecimiento.
    private const int   BORDE_TICKS   = 20;
    private const float BORDE_D_MIN   = 1.0;
    private const float BORDE_D_DELTA = 0.3;
    private int   m_BordeTicks;
    private float m_BordeDRef;
    private bool  m_BordeDiag;
    // ⚠ LA VENTANA DEL RESCATE ES ESPACIAL (arco), NO de frames. Medido en el
    // espejo: en zonas lentas ~70% de los frames son duplicados de meseta y ±60
    // FRAMES alcanzan ±1.8 m — el rescate por frames necesitaba 7 disparos para
    // cerrar un desfase de 2.7 m (y con fw p90 4 deg mientras tanto). ±12 m de
    // ARCO lo cierra en UN disparo, y acotar por arco es ademas MAS seguro contra
    // teleport que por frames (no puede saltar a una rama espacialmente cerca
    // pero arco-lejos). COTA_RESCATE acota el costo en cintas patologicas.
    private const float RESCATE_ARCO_M = 12.0;
    private const int   COTA_RESCATE = 900;
    // ⚠ RESCATE ESCALONADO: la ventana fija de ±12 m era un TECHO
    // DURO — la jesko quedo desincronizada 14.8 m (hueco de cinta a 200+) y el
    // rescate disparaba cada 12 ticks, escaneaba ±12, el ancla clampeaba al
    // borde, rechazada, y quedo 58 muestras en fallback A PESAR de estar sobre
    // la ruta (los desyncs de 6.8-10.8 si los recupero). Si un rescate NO deja
    // distM <= RESCATE_M, el siguiente escanea mas lejos: 12, 24, 36, 48, 60 m
    // (tope RESCATE_ARCO_MAX — sigue acotado por arco, la proteccion anti-rama).
    private const float RESCATE_ARCO_MAX = 60.0;
    private int m_TicksLejos;
    private int m_RescFallidos;
    // ★★★ SIEMBRA DEL CURSOR EN EL CUSP (, GO del usuario — la ultima
    // palanca disenada para el caso 07). Medido: el interior-skip cerro rev#0
    // pero el cursor NO fluye rev#0→fwd#2→rev#1 — al llegar a rev#1 sigue en el
    // arco de rev#0 (mismo suelo) y el costo lo re-elige → fly-off. El cusp es
    // un salto de arco DECLARADO: cuando la fase esperada cambia y el tramo que
    // contiene s_est no es de la fase nueva, s_est SE SIEMBRA al inicio del
    // proximo tramo de esa fase (tolerancia 5 m hacia atras). El cursor arranca
    // la fase nueva en SU arco, sin competir desde el arco viejo. Se apila sobre
    // fase + λ + interior-skip (cada capa cerro lo suyo).
    private const float TRAMO_FASE_MIN_M = 3.0;
    // ★ 1.2.4: LA SIEMBRA NO SE VA LEJOS. El flip del leg ocurre en el cusp y el tramo nuevo de la cinta EMPIEZA en ese
    // mismo reposo (medido en las 7 cintas de Badlands: 0,00-0,01 m entre el fin del tramo saliente y el inicio del
    // siguiente). Un blanco de la monotonia a mas de esto del vehiculo es contabilidad rota (medido: 550 m, el cursor se
    // fue al cusp siguiente de la misma fase y el leg manejo 600 m a ciegas). 2x la ventana del rescate.
    private const float SIEMBRA_LEJOS_M = 24.0;
    // el re-anclaje por posicion acepta cuadros cuyo rumbo grabado difiere del real hasta esto: una ruta que vuelve por
    // la misma calle tiene cuadros de la misma fase a 0 m y a 180 grados
    private const float REANCLA_RUMBO_DEG = 60.0;
    private ref array<float> m_TramoS;    // arco de inicio de cada tramo de fase
    private ref array<int>   m_TramoIdx;  // frame de inicio del tramo
    private ref array<bool>  m_TramoRev;  // fase del tramo (true = reversa)
    private int  m_UltTramoOk;            // tramo EN CONSUMO (monotonia del tape)
    private int  m_DiagLejos;             // rate-limit del diag LEJOS
    // ★★★ MONOTONIA DE CONSUMO como restriccion DURA del ancla (, GO
    // del usuario — la direccion estructural de ). El tape se consume UNA
    // vez, en orden: un ancla de un tramo YA CERRADO (indice < tramo en
    // consumo) es imposible por construccion — PROHIBIDA, no castigada. El λ
    // blando no alcanzo a 0.07 m (un castigo se vence con posicion muy cercana);
    // la prohibicion colapsa el overlap rev/rev sin un solo numero por casco.
    // Re-anclar hacia ATRAS dentro del MISMO tramo en consumo sigue permitido
    // (recuperacion legitima tras deriva — el caso jesko). La SIEMBRA declara
    // consumo (salta el tramo en consumo al sembrado). Cumple la directiva de
    // el usuario: geometrico/estructural, cero perillas por vehiculo.
    private ref array<int> m_FrameTramo;  // tramo (emitido) de cada frame
    private int m_SkipConsumidos;         // diag: candidatos prohibidos este scan
    private int m_SkipClamp;              // diag: candidatos por delante del clamp de fase

    void BZCintaRunner()
    {
        m_S = new array<float>();
        m_X = new array<float>();
        m_Z = new array<float>();
        m_Y = new array<float>();
        m_H = new array<float>();
        m_Fw = new array<float>();
        m_Thr = new array<float>();
        m_Brk = new array<float>();
        m_Kmh = new array<float>();
        m_Gear = new array<int>();
        m_Str = new array<float>();
        m_Corte = new array<int>();
        m_Roll = new array<float>();
        m_Camina = new array<float>();
        m_Count = 0;
        m_Cursor = 0;
        m_Valida = false;
        m_SEst = 0;
        m_PrevQOk = false;
        m_TicksLejos = 0;
        m_RescFallidos = 0;
    }

    bool Valida() { return m_Valida; }
    int  Count()  { return m_Count; }
    int  Cursor() { return m_Cursor; }
    int  SkipConsumidos() { return m_SkipConsumidos; }   // diag
    float SEst()    { return m_SEst; }                   // log CSV
    int   UltCons() { return m_UltTramoOk; }             // log CSV

    // el ARCO donde termina la proxima meseta de v=0 grabada, buscando desde s_est
    // hacia adelante hasta SALTO_CONSULTA_M. Devuelve el `s` de la primera fila que vuelve a rodar
    // despues de la meseta (donde la toma trae la marcha, el freno suelto y el gas de SALIDA), o
    // -1 si no hay meseta en ese alcance.
    // Por que SALTO_CONSULTA_M y no otra distancia: es exactamente lo que este runner absorbe como
    // ODOMETRIA en la primera consulta despues de soltar la cinta. Si el vehiculo recorre mas que
    // eso sin consultar, re-ancla por posicion y el re-armado por arco ya no aplica. Medido sobre las
    // paradas intermedias de DEMOBUS, CAZADOR y KRASNOSTAV: el vehiculo queda a 0,1-5,8 m de la meseta.
    // No exige duracion minima: una caida grabada a < 0.3 km/h viniendo de 10 km/h no es ruido.
    // entrar en la meseta que contiene el frame k (misma fase): del primer al ultimo frame con v grabada
    // < MESETA_KMH. El cursor queda en la entrada.
    private void EntrarEnMeseta(int k)
    {
        int tr = -1;
        if (m_FrameTramo && k < m_FrameTramo.Count()) tr = m_FrameTramo.Get(k);
        int ini = k;
        while (ini > m_MesetaUltFin + 1 && m_Kmh.Get(ini - 1) < MESETA_KMH && FrameTramoDe(ini - 1) == tr) ini = ini - 1;
        int fin = k;
        while (fin < m_Count - 1 && m_Kmh.Get(fin + 1) < MESETA_KMH && FrameTramoDe(fin + 1) == tr) fin = fin + 1;
        m_MesetaIni = ini;
        m_MesetaFin = fin;
        m_MesetaLlamadas = 0;
        m_Cursor = ini;
        m_SEst = m_S.Get(ini);
        if (!BZDriveController.s_Quiet) Print("[Cinta] MESETA grabada: frames " + ini.ToString() + "-" + fin.ToString() + " (" + (fin - ini + 1).ToString() + " = " + ((fin - ini + 1) / 40.0).ToString() + " s a 40 Hz) en s " + m_S.Get(ini).ToString() + " · anclado en la entrada" + Suf());
    }

    private int FrameTramoDe(int k)
    {
        if (!m_FrameTramo || k < 0 || k >= m_FrameTramo.Count()) return -1;
        return m_FrameTramo.Get(k);
    }

    // Salir de la meseta en curso: a la primera fila de salida si es del mismo tramo; si no, el cursor queda y el leg
    // hace el flip. La meseta queda cumplida (no se re-entra).
    private void SalirDeMeseta(string por)
    {
        int fin = m_MesetaFin;
        int salida = fin + 1;
        int n = fin - m_MesetaIni + 1;
        m_MesetaUltFin = fin;
        m_MesetaIni = -1;
        m_MesetaFin = -1;
        if (salida < m_Count && FrameTramoDe(salida) == FrameTramoDe(fin))
        {
            m_Cursor = salida;
            m_SEst = m_S.Get(salida);
            if (!BZDriveController.s_Quiet) Print("[Cinta] MESETA " + por + ": " + n.ToString() + " frames -> salida en el frame " + salida.ToString() + " (s " + m_S.Get(salida).ToString() + ", gas " + m_Thr.Get(salida).ToString() + ")" + Suf());
            return;
        }
        if (!BZDriveController.s_Quiet) Print("[Cinta] MESETA " + por + ": " + n.ToString() + " frames · la salida cruza de tramo: cede al leg" + Suf());
    }

    // Para el controlador (el AR no dispara adentro) y el runner (la parada declarada pide el hold).
    bool EnMeseta() { return m_MesetaIni >= 0; }
    int MesetaFrames()
    {
        if (m_MesetaIni < 0) return 0;
        return m_MesetaFin - m_MesetaIni + 1;
    }
    vector MesetaPos()
    {
        if (m_MesetaIni < 0) return "0 0 0";
        return Vector(m_X.Get(m_MesetaIni), m_Y.Get(m_MesetaIni), m_Z.Get(m_MesetaIni));
    }
    // La suelta de la cinta con re-armado (el controlador, al salir de un hold o por el AR): la meseta queda
    // cumplida y el cursor en la salida, para que el re-arme siga desde ahi y no vuelva a detenerse.
    void SoltarMeseta()
    {
        if (m_MesetaIni < 0) return;
        SalirDeMeseta("soltada");
    }

    // el alcance de FinMesetaAdelante, para que el hold pendiente del runner sepa cuando la meseta quedo atras.
    // la identidad del runner, para que sus lineas se puedan atribuir con 9 coches.
    // Va como SUFIJO en cada linea, nunca como prefijo: los greps existentes buscan por el principio.
    private string m_Etiq = "";
    void SetEtiqueta(string e) { m_Etiq = e; }
    private string Suf()
    {
        if (m_Etiq == "") return "";
        return " · runner " + m_Etiq;
    }

    // LA COLA DE UN FRENADO (O COASTEO) HACIA UN REPOSO. true si desde el cursor, dentro del alcance, hay una
    // meseta no consumida y la cinta no pide gas en ninguna fila hasta ella: el que grabo estaba llegando a esa detencion.
    // Es el contenido que distingue "detenido a la entrada de la parada" de "detenido saliendo de un hold" (ahi la cinta pide gas).
    bool SinGasHastaElReposo()
    {
        if (!m_Valida || m_Count < 2) return false;
        int kg = m_Cursor;
        if (kg < 0) kg = 0;
        int trG = FrameTramoDe(kg);   // 1.2.4: el reposo tiene que ser de ESTE tramo
        float sLimG = m_SEst + SALTO_CONSULTA_M;
        while (kg < m_Count && m_S.Get(kg) <= sLimG)
        {
            if (FrameTramoDe(kg) != trG) return false;
            if (m_Kmh.Get(kg) < MESETA_KMH && kg > m_MesetaUltFin) return true;
            if (m_Thr.Get(kg) > 0.01) return false;
            kg = kg + 1;
        }
        return false;
    }

    float AlcanceMesetaM() { return SALTO_CONSULTA_M; }

    // LA MESETA SE ENGANCHA DONDE EL VEHICULO SE DETUVO, NO DONDE CAE EL CURSOR.
    // 856 entra en la meseta cuando la fila BAJO el cursor tiene v grabada < MESETA_KMH. El cursor es
    // ESPACIAL y el vehiculo real no frena en la fila exacta: si queda unos centimetros corto, la fila
    // que le toca es la ULTIMA de la rampa -- freno grabado a fondo -- y la meseta que lo sacaria queda
    // ahi nomas, sin alcanzarse nunca, porque el cursor solo avanza si el vehiculo avanza.
    // MEDIDO en la corrida de 8 buses (RPT 21:18): el #102 y el #104 quedaron 22 s clavados en el mismo
    // punto (wp 5825, s 8398,48) con la cinta poseyendo y modo CRUISE; el cursor estaba en la fila 45925,
    // v grabada 1,193 km/h (0,193 por encima del umbral) y freno 1,00, a 2,5 CM de la fila 45926 (v 0,009)
    // que abre la meseta. El servicio los declaro STUCK y relanzo el tramo entero desde la terminal.
    // La salida: si el vehiculo esta detenido y hay una meseta grabada a la vista -- el MISMO barrido y el
    // MISMO alcance que ya usa el hold (SALTO_CONSULTA_M) -- se entra en ella. Cero umbrales nuevos: el
    // "detenido" lo decide el controlador con los suyos de siempre, y la meseta sale sola como en 856.
    // No se re-entra en una meseta ya cumplida (m_MesetaUltFin), igual que la entrada normal.
    bool EngancharMesetaDetenido()
    {
        if (!m_Valida || m_Count < 2) return false;
        if (m_MesetaIni >= 0) return false;
        int k = m_Cursor;
        if (k < 0) k = 0;
        int trEng = FrameTramoDe(k);   // 1.2.4: la meseta a la vista es de ESTE tramo (la del cusp siguiente es del otro leg)
        float sLim = m_SEst + SALTO_CONSULTA_M;
        while (k < m_Count && m_S.Get(k) <= sLim)
        {
            if (FrameTramoDe(k) != trEng) return false;
            if (m_Kmh.Get(k) < MESETA_KMH && k > m_MesetaUltFin)
            {
                if (!BZDriveController.s_Quiet)
                {
                    string me = "[Cinta] MESETA A LA VISTA: el vehiculo se detuvo ";
                    me = me + (m_S.Get(k) - m_SEst).ToString() + " m antes de la entrada";
                    me = me + " (fila del cursor " + m_Cursor.ToString() + " v " + m_Kmh.Get(m_Cursor).ToString() + " km/h)";
                    me = me + Suf();
                    Print(me);
                }
                EntrarEnMeseta(k);
                return true;
            }
            k = k + 1;
        }
        return false;
    }
    // AL SOLTAR LA CINTA, LO YA SERVIDO SE CONSUME Y EL CURSOR VA A LA FILA DEL
    // VEHICULO. Cuando el hold del consumidor (o el AR) detiene el vehiculo unos cm ANTES de la entrada de una meseta grabada,
    // la meseta no se engancha (durante el hold no corre Sample) y el cursor queda CONGELADO durante la suelta (Muestra solo
    // corre con la cinta poseyendo). Al re-armar, la odometria acumulada lo deja caer sobre la fila que toque: si es una fila
    // de reposo, la meseta lo clava sus cuadros (0,275-1,4 s) con el vehiculo ya en marcha, y la curvatura grabada llega
    // ~1 m tarde. MEDIDO cuadro a cuadro en la parada de s 3831, 13 m antes de la curva a tope de Novodmitrovsk (#104, 23/09
    // 13:51): cursor 3831,24 congelado 0,85 s, re-arme en 3831,62 con el casco en 3832,45, clavado en el segundo bloque
    // (15039-15049) 0,275 s, atraso -1,1 .. -1,5 m hasta la curva; el bus abrio 2,9 m hacia afuera y se ahogo. Poblacion (77
    // pasadas de A y B, 20-23/09): con el cursor ANTES de la entrada durante el hold 5 trabas en 12; con la meseta enganchada
    // 2 en 65 (Fisher 0,0007). Aca, al soltar: (1) la fila mas cercana al vehiculo dentro del alcance y en el tramo del cursor
    // pasa a ser el cursor (lo mismo que deja la salida de una meseta cumplida: el cursor donde esta el vehiculo); (2) los
    // bloques de reposo que quedaron ATRAS de esa fila, y el primero por delante si el vehiculo se detuvo corto, quedan
    // cumplidos (m_MesetaUltFin: no se re-entra); un bloque mas adelante es otra parada grabada y se sirve como siempre;
    // (3) la odometria se siembra en la posicion de la suelta, asi el primer Muestra del re-arme suma SOLO lo recorrido
    // desde ahi. Con la meseta enganchada no hace nada: SoltarMeseta ya la cumple y deja el cursor en su salida.
    bool ConsumirMesetasServidas(vector pos)
    {
        if (!m_Valida || m_Count < 2) return false;
        if (m_MesetaIni >= 0) return false;
        int k0 = m_Cursor;
        if (k0 < 0) k0 = 0;
        int trC = FrameTramoDe(k0);
        float sLimC = m_SEst + SALTO_CONSULTA_M;
        int kPos = -1;
        float d2Min = 1000000000.0;
        int kc = k0;
        while (kc < m_Count && m_S.Get(kc) <= sLimC)
        {
            if (FrameTramoDe(kc) == trC)
            {
                float dxc = pos[0] - m_X.Get(kc);
                float dzc = pos[2] - m_Z.Get(kc);
                float d2c = dxc * dxc + dzc * dzc;
                if (d2c < d2Min)
                {
                    d2Min = d2c;
                    kPos = kc;
                }
            }
            kc = kc + 1;
        }
        if (kPos < 0) return false;
        int ultFinC = m_MesetaUltFin;
        int bloquesC = 0;
        int cuadrosC = 0;
        bool primeroAdelanteC = false;
        kc = k0;
        while (kc < m_Count && m_S.Get(kc) <= sLimC)
        {
            if (m_Kmh.Get(kc) < MESETA_KMH && kc > ultFinC && FrameTramoDe(kc) == trC)
            {
                int finC = kc;
                while (finC < m_Count - 1 && m_Kmh.Get(finC + 1) < MESETA_KMH && FrameTramoDe(finC + 1) == trC) finC = finC + 1;
                if (kc > kPos && primeroAdelanteC) break;   // un segundo bloque por delante: otra parada grabada
                if (kc > kPos) primeroAdelanteC = true;
                ultFinC = finC;
                bloquesC = bloquesC + 1;
                cuadrosC = cuadrosC + (finC - kc + 1);
                kc = finC + 1;
                continue;
            }
            kc = kc + 1;
        }
        if (bloquesC == 0 && kPos <= m_Cursor) return false;
        float sAntesC = m_SEst;
        if (bloquesC > 0) m_MesetaUltFin = ultFinC;
        if (kPos > m_Cursor)
        {
            m_Cursor = kPos;
            m_SEst = m_S.Get(kPos);
        }
        m_PrevQ = pos;
        m_PrevQOk = true;
        if (!BZDriveController.s_Quiet)
        {
            string mc = "[Cinta] SUELTA: cursor s " + sAntesC.ToString() + " -> s " + m_SEst.ToString() + " (la fila del vehiculo, a " + Math.Sqrt(d2Min).ToString() + " m)";
            mc = mc + " · mesetas servidas consumidas: " + bloquesC.ToString() + " bloques, " + cuadrosC.ToString() + " cuadros (hasta el frame " + ultFinC.ToString() + ")";
            mc = mc + Suf();
            Print(mc);
        }
        return true;
    }
    // el ARCO donde EMPIEZA la proxima meseta de v=0 grabada (mismo barrido que FinMesetaAdelante), o -1.
    // El hold pendiente engancha cuando el cursor llega ahi: por ARCO y no por "la fila bajo el cursor es de reposo",
    // porque a 3 km/h el cursor salta una meseta de 2,8 cm en un solo paso de 40 Hz y esa fila no se ve nunca.
    float InicioMesetaAdelante()
    {
        if (!m_Valida || m_Count < 2) return -1;
        int ki = m_Cursor;
        if (ki < 0) ki = 0;
        float sLimI = m_SEst + SALTO_CONSULTA_M;
        while (ki < m_Count && m_S.Get(ki) <= sLimI)
        {
            if (m_Kmh.Get(ki) < MESETA_KMH) return m_S.Get(ki);
            ki = ki + 1;
        }
        return -1;
    }
    // ★ 1.2.5: la ENTRADA de la primera meseta grabada no consumida con arco en (sDesde, sHasta], barriendo desde el
    // cursor; -1 si no hay. La parada SINTETICA pedida por el consumidor se mueve a ella: es una parada real.
    float ProximaMesetaEntre(float sDesde, float sHasta)
    {
        if (!m_Valida || m_Count < 2) return -1;
        int kp = m_Cursor;
        if (kp < 0) kp = 0;
        while (kp < m_Count && m_S.Get(kp) <= sHasta)
        {
            if (m_Kmh.Get(kp) < MESETA_KMH && kp > m_MesetaUltFin && m_S.Get(kp) > sDesde) return m_S.Get(kp);
            kp = kp + 1;
        }
        return -1;
    }
    float FinMesetaAdelante()
    {
        if (!m_Valida || m_Count < 2) return -1;
        int k = m_Cursor;
        if (k < 0) k = 0;
        float sLimite = m_SEst + SALTO_CONSULTA_M;
        while (k < m_Count && m_S.Get(k) <= sLimite)
        {
            if (m_Kmh.Get(k) < MESETA_KMH)
            {
                int jm = k;
                while (jm < m_Count && m_Kmh.Get(jm) < MESETA_KMH) jm = jm + 1;
                if (jm >= m_Count) return -1;
                return m_S.Get(jm);
            }
            k = k + 1;
        }
        return -1;
    }

    // hueco A: ¿la TOMA muestra que la marcha grabada en este punto NO sostiene el avance?
    // Evidencia MEDIDA en la toma, no modelo: alrededor del cursor, los frames CONTIGUOS con la MISMA marcha que el
    // cursor dentro de +-SOSTEN_VENTANA_M, todos con gas >= 0,95 y sin freno, cubriendo >= 1 m -> el humano A FONDO
    // en esa marcha PERDIA velocidad (kmh del ultimo < kmh del primero). La consulta solo el tope de sosten que deja
    // C1 al devolver el mando (BZDriveController.AplicarTopeDeSosten).
    // ✖ Por que NO el modelo de par del Manual: contra el humano a fondo en las tomas (OFFROAD_CUESTA, WRX_CUESTA,
    // DEMOBUS*) sobreestima la aceleracion -1 m/s2 de mediana en bus y Offroad y -3 a -7 en el WRX: su SIGNO no
    // decide si una marcha sostiene. Y el piso de rpm del DerivationEngine tampoco: el bus en cuarta con 0,75-1,0 del
    // ralenti acelera bien en llano en las corridas reales.
    // Medido (OFFROAD_CUESTA_0): en segunda a 25 grados el humano pierde 29,7 -> 20,5 km/h a fondo y pasa a primera
    // en s 8,51. Ventana 1 / 2,5 / 5 m: el mismo tramo (+-0,14 m). En WRX_CUESTA_0 la evidencia no aparece nunca.
    private const float SOSTEN_VENTANA_M = 2.5;
    private const float SOSTEN_SPAN_MIN_M = 1.0;
    private const float SOSTEN_THR_A_FONDO = 0.95;
    private const int   SOSTEN_COTA_PASOS = 400;
    bool MarchaPierdeAFondo()
    {
        if (!m_Valida || m_Count < 2) return false;
        int cS = m_Cursor;
        if (cS < 0) cS = 0;
        if (cS > m_Count - 1) cS = m_Count - 1;
        int gS = m_Gear.Get(cS);
        if (!FrameAFondoEn(cS, gS)) return false;
        float sC = m_S.Get(cS);
        int aS = cS;
        int pasosS = 0;
        while (aS > 0 && pasosS < SOSTEN_COTA_PASOS && m_S.Get(aS - 1) >= sC - SOSTEN_VENTANA_M && FrameAFondoEn(aS - 1, gS))
        {
            aS = aS - 1;
            pasosS = pasosS + 1;
        }
        int bS = cS;
        pasosS = 0;
        while (bS < m_Count - 1 && pasosS < SOSTEN_COTA_PASOS && m_S.Get(bS + 1) <= sC + SOSTEN_VENTANA_M && FrameAFondoEn(bS + 1, gS))
        {
            bS = bS + 1;
            pasosS = pasosS + 1;
        }
        if (m_S.Get(bS) - m_S.Get(aS) < SOSTEN_SPAN_MIN_M) return false;
        return m_Kmh.Get(bS) < m_Kmh.Get(aS);
    }

    private bool FrameAFondoEn(int k, int g)
    {
        if (m_Gear.Get(k) != g) return false;
        if (m_Thr.Get(k) < SOSTEN_THR_A_FONDO) return false;
        if (m_Brk.Get(k) > 0.05) return false;
        return true;
    }

    // AR MANUAL: consultas por ARCO para la re-ubicacion. El cursor de siempre solo avanza por
    // odometria; aca hace falta la pose GRABADA en un s arbitrario y poder anclar el cursor ahi.
    // frame con S <= s (busqueda binaria; 0 / ultimo en los bordes)
    private int FrameDe(float s)
    {
        int loF = 0;
        int hiF = m_Count - 1;
        int miF;
        if (s <= m_S.Get(0)) return 0;
        if (s >= m_S.Get(hiF)) return hiF;
        while (hiF - loF > 1)
        {
            miF = (loF + hiF) / 2;
            if (m_S.Get(miF) <= s) loF = miF;
            else hiF = miF;
        }
        return loF;
    }

    // la cuerda del anclaje: el primer cuadro despues de j, hasta hi, con una cuerda de >= pasoMin desde j y del MISMO
    // tramo (la cuerda no cruza de fase); -1 si no hay
    private int FinDeCuerda(int j, int hi, float pasoMin)
    {
        int k = j + 1;
        int trJ = -1;
        if (m_FrameTramo && j < m_FrameTramo.Count()) trJ = m_FrameTramo.Get(j);
        while (k <= hi)
        {
            if (trJ >= 0 && k < m_FrameTramo.Count() && m_FrameTramo.Get(k) != trJ) return -1;
            float cx = m_X.Get(k) - m_X.Get(j);
            float cz = m_Z.Get(k) - m_Z.Get(j);
            if (cx * cx + cz * cz >= pasoMin * pasoMin) return k;
            k = k + 1;
        }
        return -1;
    }

    // MOTO (amortiguamiento de la ley lateral): la curvatura GRABADA en el arco s (rad/m, + a la derecha, la convencion
    // del rumbo), dpsi/ds del rumbo grabado entre s - mitadM y s + mitadM. Por la velocidad de la moto da la tasa de giro
    // de la referencia (en MOPED_130_01R, contra el yaw_rate del frame_: r 0,92, pendiente 0,98 con mitadM 0,5)
    float CurvaturaEn(float s, float mitadM)
    {
        if (!m_Valida || m_Count < 2) return 0;
        int aC = FrameDe(s - mitadM);
        int bC = FrameDe(s + mitadM);
        float dsC = m_S.Get(bC) - m_S.Get(aC);
        if (dsC < 0.2) return 0;
        float dhC = m_H.Get(bC) - m_H.Get(aC);
        while (dhC > 180.0) dhC = dhC - 360.0;
        while (dhC < -180.0) dhC = dhC + 360.0;
        return dhC * Math.DEG2RAD / dsC;
    }

    // MOTO: la velocidad maxima admisible AHORA (km/h) para llegar a la grabada de mas adelante frenando a `a` m/s2,
    // mirando `alcanceM` metros de arco desde sEst: min(vObj, raiz(v_k^2 + 2 a ds)). La anticipacion del conductor de
    // moto (con la moto en la toma, la referencia es la grabada; solo baja si viene mas rapido de lo que puede frenar)
    float VelAdmisibleKmh(float sEst, float alcanceM, float a, float vObj)
    {
        float refV = vObj;
        int kA;
        for (kA = m_Cursor + 1; kA < m_Count; kA++)
        {
            float dsA = m_S.Get(kA) - sEst;
            if (dsA > alcanceM) break;
            if (dsA < 0) dsA = 0;
            float vkA = m_Kmh.Get(kA) / 3.6;
            float admA = Math.Sqrt(vkA * vkA + 2.0 * a * dsA) * 3.6;
            if (admA < refV) refV = admA;
        }
        return refV;
    }

    // MOTO: la cinta trae el steering grabado (columna 12)
    bool TieneSteering()
    {
        if (m_Count < 1 || m_Str.Count() != m_Count) return false;
        return m_Str.Get(0) > -5;
    }

    // MOTO (columna 15): lo que el conductor necesita para caminar un tramo de la toma, por frame
    // ★ LA FRENADA A FONDO QUE LA TOMA DEMOSTRO: a = v0^2 / (2 ds) sobre su parada final (desde la primera fila con el
    // pedal a fondo hasta la ultima rodando). Es lo que ESTE vehiculo hizo con el pedal a fondo en ESTE lugar; el manual
    // deriva el freno del par de frenos del config y la fisica no lo entrega (medido: 8,5 declarados, 5,5 reales).
    // -1 si la toma no termina con una frenada a fondo medible (parada suave, o termina rodando).
    float FrenadaFinalMS2()
    {
        if (m_FrenoTomaOk) return m_FrenoTomaMs2;
        m_FrenoTomaOk = true;
        m_FrenoTomaMs2 = -1;
        if (!m_Valida) return -1;
        int fin = m_Count - 1;
        while (fin > 0 && m_Kmh.Get(fin) < FRENADA_FIN_KMH) fin = fin - 1;   // el reposo del final
        if (fin <= 0) return -1;
        if (m_Brk.Get(fin) < FRENADA_PEDAL) return -1;   // no paro con el pedal
        int ini = fin;
        while (ini > 0 && m_Brk.Get(ini - 1) >= FRENADA_PEDAL) ini = ini - 1;
        float v0Kmh = m_Kmh.Get(ini);
        float ds = m_S.Get(fin) - m_S.Get(ini);
        if (v0Kmh < FRENADA_V0_KMH || ds < FRENADA_DS_M || fin - ini + 1 < FRENADA_CUADROS) return -1;
        float v0 = v0Kmh / 3.6;
        m_FrenoTomaV0Kmh = v0Kmh;
        m_FrenoTomaDsM = ds;
        m_FrenoTomaMs2 = (v0 * v0) / (2.0 * ds);
        return m_FrenoTomaMs2;
    }
    float FrenadaFinalV0Kmh() { return m_FrenoTomaV0Kmh; }

    // ★ LA FRENADA DEL CUSP: la parada grabada (bloque con v < MESETA_KMH) mas cercana a `pose` (la pose final del leg) y el
    // pedal a fondo que la precede: a = v0^2 / (2 ds), como la parada final (FrenadaFinalMS2) pero con umbrales de cusp
    // (los cusps se toman a 7-10 km/h en 0,4-0,7 m). Dentro de una misma toma las paradas a fondo dan 4-7 m/s2: la
    // capacidad depende de la velocidad y del lugar, por eso se mide la de ESE cusp y no la de la llegada.
    // -1 sin parada grabada a menos de FRENADA_POSE_M de la pose, o sin pedal a fondo medible antes de ella.
    private const float FRENADA_POSE_M = 2.0;
    private const float FRENADA_CUSP_V0_KMH = 5.0;
    private const float FRENADA_CUSP_DS_M = 0.3;
    private const int   FRENADA_CUSP_CUADROS = 12;
    float FrenadaAntesDePoseMS2(vector pose, out float v0Kmh, out float dsM, out int cuadros)
    {
        v0Kmh = 0;
        dsM = 0;
        cuadros = 0;
        if (!m_Valida) return -1;
        int kStar = -1;
        float dMin2 = FRENADA_POSE_M * FRENADA_POSE_M;
        int k;
        for (k = 0; k < m_Count; k++)
        {
            float dxk = pose[0] - m_X.Get(k);
            float dzk = pose[2] - m_Z.Get(k);
            float d2k = dxk * dxk + dzk * dzk;
            if (d2k < dMin2)
            {
                dMin2 = d2k;
                kStar = k;
            }
        }
        if (kStar < 0) return -1;
        // el bloque de parada que contiene ese cuadro, o el que sigue (la pose suele caer en su entrada), o el anterior
        int p = kStar;
        while (p < m_Count - 1 && m_Kmh.Get(p) >= MESETA_KMH && m_S.Get(p) - m_S.Get(kStar) < 1.0) p = p + 1;
        if (m_Kmh.Get(p) >= MESETA_KMH)
        {
            p = kStar;
            while (p > 0 && m_Kmh.Get(p) >= MESETA_KMH && m_S.Get(kStar) - m_S.Get(p) < 1.0) p = p - 1;
        }
        if (m_Kmh.Get(p) >= MESETA_KMH) return -1;
        int iniP = p;
        while (iniP > 0 && m_Kmh.Get(iniP - 1) < MESETA_KMH) iniP = iniP - 1;
        int j0 = iniP;
        while (j0 > 0 && m_Brk.Get(j0 - 1) >= FRENADA_PEDAL) j0 = j0 - 1;
        cuadros = iniP - j0;
        if (cuadros < FRENADA_CUSP_CUADROS) return -1;
        v0Kmh = m_Kmh.Get(j0);
        dsM = m_S.Get(iniP) - m_S.Get(j0);
        if (v0Kmh < FRENADA_CUSP_V0_KMH || dsM < FRENADA_CUSP_DS_M) return -1;
        float v0 = v0Kmh / 3.6;
        return (v0 * v0) / (2.0 * dsM);
    }
    float FrenadaFinalDsM() { return m_FrenoTomaDsM; }

    // la ULTIMA PARADA GRABADA de la fase que contiene el cursor (el ultimo cuadro con v grabada < MESETA_KMH antes del
    // fin del tramo): distancia desde `pos` y el cuadro. -1 si la fase no tiene parada. Para el cierre de leg: la pose
    // del leg es un wp del conversor y puede quedar a 0,4-0,5 m de donde la toma paro de verdad.
    float DistUltimaParadaDeFase(vector pos, bool esperaRev, out int cuadro)
    {
        int iniP = -1;
        int finP = -1;
        cuadro = -1;
        if (!UltimaParadaDeFase(esperaRev, iniP, finP)) return -1;
        cuadro = finP;
        float dxp = pos[0] - m_X.Get(finP);
        float dzp = pos[2] - m_Z.Get(finP);
        return Math.Sqrt(dxp * dxp + dzp * dzp);
    }

    // ★ 1.2.4: LA FASE ENTERA. Por construccion los tramos alternan (un bloque corto se absorbe en el que lo rodea), pero
    // el cierre del leg y el clamp miran hasta el proximo tramo del OTRO sentido, no hasta el proximo tramo: si alguna vez
    // quedaran dos del mismo sentido seguidos, una parada a mitad de fase (un hold del consumidor sobre una parada
    // grabada) no cerraria el leg ni frenaria el cursor en el borde.
    private int UltimoTramoDeFase(int tr)
    {
        if (tr < 0 || !m_TramoRev || tr >= m_TramoRev.Count()) return tr;
        int uT = tr;
        while (uT + 1 < m_TramoRev.Count() && m_TramoRev.Get(uT + 1) == m_TramoRev.Get(tr)) uT = uT + 1;
        return uT;
    }

    private int PrimerTramoDeFase(int tr)
    {
        if (tr < 0 || !m_TramoRev || tr >= m_TramoRev.Count()) return tr;
        int pT = tr;
        while (pT > 0 && m_TramoRev.Get(pT - 1) == m_TramoRev.Get(tr)) pT = pT - 1;
        return pT;
    }

    // el bloque de cuadros de la ULTIMA parada grabada (v < MESETA_KMH) de la fase que contiene el cursor: [ini, fin].
    // false si la fase no tiene parada. La fase es ENTERA (ver UltimoTramoDeFase).
    private bool UltimaParadaDeFase(bool esperaRev, out int iniP, out int finP)
    {
        iniP = -1;
        finP = -1;
        if (!m_Valida || !m_TramoS || m_TramoS.Count() == 0) return false;
        int tr = TramoDe(m_SEst);
        while (tr > 0 && m_TramoRev.Get(tr) != esperaRev) tr = tr - 1;
        if (tr < 0 || m_TramoRev.Get(tr) != esperaRev) return false;
        int fin = m_Count - 1;
        int trUlt = UltimoTramoDeFase(tr);
        if (trUlt + 1 < m_TramoIdx.Count()) fin = m_TramoIdx.Get(trUlt + 1) - 1;
        int ini = m_TramoIdx.Get(PrimerTramoDeFase(tr));
        int k = fin;
        while (k > ini && m_Kmh.Get(k) >= MESETA_KMH) k = k - 1;
        if (k <= ini) return false;
        finP = k;
        while (k > ini && m_Kmh.Get(k - 1) < MESETA_KMH) k = k - 1;
        iniP = k;
        return true;
    }

    // ★ EL LEG SE CIERRA EN LA ULTIMA PARADA GRABADA. Una toma puede parar, arrastrarse (a < 3 km/h, sin freno) y volver a
    // parar antes del cambio de sentido: el cierre aceptaba la PRIMERA parada (medido: 3,1 m antes de la pose, con la
    // meseta de 6 s sin cumplir, y la reversa arrancaba 3 m corrida con un AR de regalo). 0 = la fase no tiene parada
    // grabada (el cierre decide como antes), 1 = el cursor esta en la ultima parada (o a ULT_PARADA_ATRAS_M de arco de
    // su entrada: el cursor puede venir corto), 2 = todavia no: quedan filas por reproducir.
    private const float ULT_PARADA_ATRAS_M = 0.3;
    int ParadaDeFase(bool esperaRev)
    {
        int iniP = -1;
        int finP = -1;
        if (!UltimaParadaDeFase(esperaRev, iniP, finP)) return 0;
        if (m_SEst >= m_S.Get(iniP) - ULT_PARADA_ATRAS_M) return 1;
        return 2;
    }

    int Frames() { return m_Count; }
    float SDeFrame(int k) { return m_S.Get(k); }
    vector PosDeFrame(int k) { return Vector(m_X.Get(k), m_Y.Get(k), m_Z.Get(k)); }
    float RumboDeFrame(int k) { return m_H.Get(k); }
    float FwDeFrame(int k) { return m_Fw.Get(k); }
    float KmhDeFrame(int k) { return m_Kmh.Get(k); }
    float CaminaDeFrame(int k)
    {
        if (m_Camina.Count() != m_Count) return 0;
        return m_Camina.Get(k);
    }
    float SteerDeFrame(int k)
    {
        if (m_Str.Count() != m_Count) return 0;
        float stF = m_Str.Get(k);
        if (stF < -5) return 0;
        return stF;
    }

    // MOTO: la cinta trae el roll de la toma (columna 14)
    bool TieneRoll()
    {
        if (m_Count < 1 || m_Roll.Count() != m_Count) return false;
        return m_Roll.Get(0) > -90;
    }

    float SFin()
    {
        if (!m_Valida || m_Count < 1) return 0;
        return m_S.Get(m_Count - 1);
    }

    // arco donde termina la FASE (entera, ver UltimoTramoDeFase) que contiene s: inicio del proximo tramo del otro
    // sentido, o el fin de la cinta
    float FinDeFaseEn(float s)
    {
        float finF = SFin();
        if (!m_TramoS || m_TramoS.Count() == 0) return finF;
        int trF = TramoDe(s);
        if (trF < 0) return finF;
        int trFU = UltimoTramoDeFase(trF);
        if (trFU < m_TramoS.Count() - 1) finF = m_TramoS.Get(trFU + 1);
        return finF;
    }

    // ¿el frame grabado en el arco s va en reversa (gear 0)?
    bool EnReversaEnS(float s)
    {
        if (!m_Valida || m_Count < 1) return false;
        return (m_Gear.Get(FrameDe(s)) == 0);
    }

    // pose GRABADA en el arco s: x, y, z interpolados entre frames; rumbo del frame mas cercano (grados,
    // convencion atan2(dx,dz) = la de SetOrientation).
    bool PoseEnS(float s, out vector posP, out float rumboP)
    {
        if (!m_Valida || m_Count < 2) return false;
        int kP = FrameDe(s);
        int k2 = kP + 1;
        if (k2 > m_Count - 1) k2 = m_Count - 1;
        float dsP = m_S.Get(k2) - m_S.Get(kP);
        float tP = 0;
        if (dsP > 0.001) tP = Math.Clamp((s - m_S.Get(kP)) / dsP, 0.0, 1.0);
        float xP = m_X.Get(kP) + (m_X.Get(k2) - m_X.Get(kP)) * tP;
        float yP = m_Y.Get(kP) + (m_Y.Get(k2) - m_Y.Get(kP)) * tP;
        float zP = m_Z.Get(kP) + (m_Z.Get(k2) - m_Z.Get(kP)) * tP;
        posP = Vector(xP, yP, zP);
        rumboP = m_H.Get(kP);
        if (tP > 0.5) rumboP = m_H.Get(k2);
        return true;
    }

    private float DistAlCuadro(vector pos, int k)
    {
        float dxk = pos[0] - m_X.Get(k);
        float dzk = pos[2] - m_Z.Get(k);
        return Math.Sqrt(dxk * dxk + dzk * dzk);
    }

    // distancia del vehiculo al cuadro del cursor (el aviso de desfase del controlador); -1 sin cinta
    float DistAlCursor(vector pos)
    {
        if (!m_Valida || m_Cursor < 0 || m_Cursor >= m_Count) return -1;
        return DistAlCuadro(pos, m_Cursor);
    }

    // rumboDeg < -900 = sin rumbo (no se filtra). El rumbo grabado es el yaw real del chasis, tambien en reversa.
    private bool RumboCompatible(int k, float rumboDeg)
    {
        if (rumboDeg < -900.0) return true;
        float dhr = m_H.Get(k) - rumboDeg;
        while (dhr > 180.0) dhr = dhr - 360.0;
        while (dhr < -180.0) dhr = dhr + 360.0;
        return Math.AbsFloat(dhr) <= REANCLA_RUMBO_DEG;
    }

    // la fase de un cuadro es la de su TRAMO (los bloques cortos absorbidos cuentan con la fase que los rodea); con el
    // mapa de tramos sin armar, la marcha grabada
    private bool CuadroEsDeFase(int k, bool esperaRev)
    {
        if (m_FrameTramo && m_FrameTramo.Count() == m_Count && m_TramoRev && m_TramoRev.Count() > 0)
        {
            int trK = m_FrameTramo.Get(k);
            if (trK >= 0 && trK < m_TramoRev.Count()) return (m_TramoRev.Get(trK) == esperaRev);
        }
        return ((m_Gear.Get(k) == 0) == esperaRev);
    }

    // ★ 1.2.4: el INICIO de tramo de la fase pedida mas cercano a pos (rumbo compatible); -1 si no hay
    private int InicioDeTramoMasCercano(vector pos, float rumboDeg, bool esperaRev, out float distM)
    {
        distM = -1;
        if (!m_TramoS || m_TramoS.Count() == 0) return -1;
        int mejorT = -1;
        float dMejorT = 0;
        int ktT;
        for (ktT = 0; ktT < m_TramoS.Count(); ktT++)
        {
            if (m_TramoRev.Get(ktT) != esperaRev) continue;
            int kIniT = m_TramoIdx.Get(ktT);
            if (!RumboCompatible(kIniT, rumboDeg)) continue;
            float dT = DistAlCuadro(pos, kIniT);
            if (mejorT < 0 || dT < dMejorT)
            {
                mejorT = ktT;
                dMejorT = dT;
            }
        }
        if (mejorT >= 0) distM = dMejorT;
        return mejorT;
    }

    // ★ 1.2.4: el cuadro de la fase pedida mas cercano a pos (rumbo compatible). Entre los que quedan a menos de 1 m del
    // mejor gana el de arco mas cercano al s_est actual (una ruta que pasa dos veces por el mismo lugar en el mismo
    // sentido). -1 si la fase no tiene cuadros compatibles.
    private int CuadroDeFaseMasCercano(vector pos, float rumboDeg, bool esperaRev, out float distM)
    {
        distM = -1;
        int jMejor = -1;
        float d2Mejor = 0;
        int jf;
        float ddx;
        float ddz;
        float dd2;
        for (jf = 0; jf < m_Count - 1; jf++)
        {
            if (!CuadroEsDeFase(jf, esperaRev)) continue;
            if (!RumboCompatible(jf, rumboDeg)) continue;
            ddx = pos[0] - m_X.Get(jf);
            ddz = pos[2] - m_Z.Get(jf);
            dd2 = ddx * ddx + ddz * ddz;
            if (jMejor < 0 || dd2 < d2Mejor)
            {
                jMejor = jf;
                d2Mejor = dd2;
            }
        }
        if (jMejor < 0) return -1;
        float topeEmp = Math.Sqrt(d2Mejor) + 1.0;
        float tope2Emp = topeEmp * topeEmp;
        float dsMejor = Math.AbsFloat(m_S.Get(jMejor) - m_SEst);
        for (jf = 0; jf < m_Count - 1; jf++)
        {
            if (!CuadroEsDeFase(jf, esperaRev)) continue;
            if (!RumboCompatible(jf, rumboDeg)) continue;
            ddx = pos[0] - m_X.Get(jf);
            ddz = pos[2] - m_Z.Get(jf);
            dd2 = ddx * ddx + ddz * ddz;
            if (dd2 > tope2Emp) continue;
            float dsJ = Math.AbsFloat(m_S.Get(jf) - m_SEst);
            if (dsJ < dsMejor)
            {
                jMejor = jf;
                dsMejor = dsJ;
            }
        }
        distM = DistAlCuadro(pos, jMejor);
        return jMejor;
    }

    // ★ 1.2.4: ancla el cursor desde la POSICION real (re-ubicacion, SAFE_STOP): el cuadro de la fase mas cercano con el
    // rumbo del vehiculo; a mas de RESCATE_ARCO_MAX no se ancla (se avisa y el cursor queda). Mismo enganche sembrado
    // que AnclarEnS (el primer Muestra no suma odometria y rescata fino alrededor).
    bool AnclarPorPosicion(vector pos, float rumboDeg, bool esperaRev, string motivo)
    {
        if (!m_Valida || m_Count < 2) return false;
        float dA = -1;
        int kA = CuadroDeFaseMasCercano(pos, rumboDeg, esperaRev, dA);
        float sAntesA = m_SEst;
        float topeA = RESCATE_ARCO_MAX;
        if (kA < 0 || dA > topeA)
        {
            if (!BZDriveController.s_Quiet) Print("[Cinta] ANCLA (" + motivo + ") por posicion: sin cuadro de la fase a menos de " + topeA.ToString() + " m con el rumbo del vehiculo (el mas cercano a " + dA.ToString() + " m): el cursor queda en s " + m_SEst.ToString() + Suf());
            return false;
        }
        m_SEst = m_S.Get(kA);
        m_Cursor = kA;
        m_TicksLejos = 0;
        m_RescFallidos = 0;
        m_BordeTicks = 0;
        m_PrevQOk = false;
        m_MesetaIni = -1;      // un re-anclaje externo borra la meseta en curso
        m_MesetaFin = -1;
        m_MesetaUltFin = -1;
        if (m_FrameTramo && kA < m_FrameTramo.Count()) m_UltTramoOk = m_FrameTramo.Get(kA);
        if (!BZDriveController.s_Quiet)
        {
            string la = "[Cinta] ANCLA (" + motivo + ") por posicion · s_est " + sAntesA.ToString() + " -> " + m_SEst.ToString();
            la = la + " · cuadro " + kA.ToString() + " a " + dA.ToString() + " m · tramo " + m_UltTramoOk.ToString();
            Print(la + Suf());
        }
        return true;
    }

    // ★ 1.2.4: el arco de la cinta que corresponde a una POSICION (el bien pisado del controlador), sin mover el cursor:
    // el cuadro de la fase mas cercano con ese rumbo, si esta a menos de RESCATE_ARCO_M. -1 si no hay.
    float SDePosicion(vector pos, float rumboDeg, bool esperaRev, out float distM)
    {
        distM = -1;
        if (!m_Valida || m_Count < 2) return -1;
        int kS = CuadroDeFaseMasCercano(pos, rumboDeg, esperaRev, distM);
        if (kS < 0 || distM > RESCATE_ARCO_M) return -1;
        return m_S.Get(kS);
    }

    // G3: ancla el cursor en s tras una re-ubicacion. Igual que la siembra: el primer Muestra no suma
    // odometria (m_PrevQOk=false) y hace el rescate fino alrededor del punto anclado; la monotonia re-arranca
    // desde el tramo del ancla.
    void AnclarEnS(float s)
    {
        if (!m_Valida || m_Count < 2) return;
        int kA = FrameDe(s);
        m_SEst = s;
        m_Cursor = kA;
        m_TicksLejos = 0;
        m_RescFallidos = 0;
        m_BordeTicks = 0;
        m_PrevQOk = false;
        m_MesetaIni = -1;      // un re-anclaje externo borra la meseta en curso
        m_MesetaFin = -1;
        m_MesetaUltFin = -1;
        if (m_FrameTramo && kA < m_FrameTramo.Count()) m_UltTramoOk = m_FrameTramo.Get(kA);
        if (!BZDriveController.s_Quiet && BZDiag.On()) Print("[Cinta] ANCLA (AR manual) · s_est -> " + m_SEst.ToString() + " · frame " + kA.ToString() + " · tramo " + m_UltTramoOk.ToString() + Suf());
    }

    // FRENO GRABADO SOBRE UNA BAJADA, ADELANTE. Desde el cursor hasta sDesde + alcanceM busca el primer
    // BLOQUE de freno grabado (frames seguidos con brk >= 0,5) que arranca a alta velocidad (kmh grabada >= vMinKmh) y
    // sobre una bajada fuerte (pendiente grabada en el arranque del bloque <= pendMaxDeg). Devuelve el arco de inicio y
    // de fin del bloque, el freno maximo y la velocidad grabada con la que el humano SALIO del bloque. Si el cursor ya
    // esta adentro del bloque, lo reconoce igual (camina hacia atras hasta el arranque). Un bloque que no califica se
    // salta entero. Barrido acotado (FRENO_BAJADA_COTA frames).
    // Medido (KRASNOSTAV wp 1180): brk 1 grabado durante 5,6 m sobre el quiebre (-3 -> -6 grados) a 90 km/h; Boris lo
    // replayo tal cual (caja negra: brk 1 0,2 s a 88, pitch -6 -> -10 con la trompa 3 grados por debajo del camino) y
    // 2 de 4 corridas clavaron la trompa en el mismo punto (+-10 cm), a la misma velocidad y lat que las 2 limpias.
    private const float FRENO_BAJADA_PEND_M = 3.0;
    private const int   FRENO_BAJADA_COTA = 400;
    bool FrenoBajadaAdelante(float sDesde, float alcanceM, float vMinKmh, float pendMaxDeg, out float sIniF, out float sFinF, out float brkF, out float kmhFinF)
    {
        if (!m_Valida || m_Count < 2) return false;
        int kF = m_Cursor;
        if (kF < 0) kF = 0;
        int pasosF = 0;
        float sTope = sDesde + alcanceM;
        while (kF < m_Count - 1 && m_S.Get(kF) <= sTope && pasosF < FRENO_BAJADA_COTA)
        {
            pasosF = pasosF + 1;
            if (m_Brk.Get(kF) < 0.5)
            {
                kF = kF + 1;
                continue;
            }
            // bloque de freno: arranque (hacia atras) y fin (hacia adelante)
            int iF = kF;
            int pasosB = 0;
            while (iF > 0 && m_Brk.Get(iF - 1) >= 0.5 && pasosB < FRENO_BAJADA_COTA)
            {
                iF = iF - 1;
                pasosB = pasosB + 1;
            }
            int jF = kF;
            pasosB = 0;
            while (jF < m_Count - 1 && m_Brk.Get(jF + 1) >= 0.5 && pasosB < FRENO_BAJADA_COTA)
            {
                jF = jF + 1;
                pasosB = pasosB + 1;
            }
            if (m_Kmh.Get(iF) >= vMinKmh && PendienteEn(iF) <= pendMaxDeg)
            {
                float bMax = 0;
                int qF = iF;
                while (qF <= jF)
                {
                    if (m_Brk.Get(qF) > bMax) bMax = m_Brk.Get(qF);
                    qF = qF + 1;
                }
                sIniF = m_S.Get(iF);
                sFinF = m_S.Get(jF);
                brkF = bMax;
                kmhFinF = m_Kmh.Get(jF);
                return true;
            }
            kF = jF + 1;
        }
        return false;
    }

    // pendiente grabada (grados, + = subida) alrededor del frame k: +-FRENO_BAJADA_PEND_M de arco, con la altura y
    // grabada (la posicion viene cuantizada a 10 cm: la ventana de 6 m la promedia).
    private float PendienteEn(int k)
    {
        float sC = m_S.Get(k);
        int aP = k;
        int pasosP = 0;
        while (aP > 0 && pasosP < FRENO_BAJADA_COTA && m_S.Get(aP - 1) >= sC - FRENO_BAJADA_PEND_M)
        {
            aP = aP - 1;
            pasosP = pasosP + 1;
        }
        int bP = k;
        pasosP = 0;
        while (bP < m_Count - 1 && pasosP < FRENO_BAJADA_COTA && m_S.Get(bP + 1) <= sC + FRENO_BAJADA_PEND_M)
        {
            bP = bP + 1;
            pasosP = pasosP + 1;
        }
        float dsP = m_S.Get(bP) - m_S.Get(aP);
        if (dsP < 0.5) return 0;
        return Math.Atan2(m_Y.Get(bP) - m_Y.Get(aP), dsP) * Math.RAD2DEG;
    }

    // Formato E1 (frame_to_route --cinta):
    //   s,x,y,z,heading,chassis_kmh,front_wheel_deg,throttle,brake,gear,handbrake,dt
    //    0 1 2 3    4        5             6            7      8    9      10    11
    // `CargarDeArchivo` vivia aca y se comia la cinta entera en un cuadro
    // (19.075 filas de PATRULLA = 1.105 ms MEDIDOS en el RPT del 18/09, 20:37:36.912 → 38.017).
    // Se BORRA y se parte en tres piezas que la cola llama cuando le da el presupuesto:
    //   ComenzarCarga()  una vez · AgregarFila(linea) por fila · ArmarPaso() hasta que de true
    // El PARSEO de una fila es el mismo, linea por linea: lo unico que cambia es QUIEN maneja el
    // bucle. El post-proceso (tramos + mapa frame->tramo) tambien se reparte, con cursores.
    private static const int ARM_LOTE = 4000;
    private int  m_LineaN;
    private int  m_ArmFase;
    private int  m_ArmIt;
    private int  m_ArmFin;
    private bool m_ArmF;
    private bool m_ArmBuscando;
    private bool m_ArmFaseEmitida;
    private bool m_ArmHayEmitida;
    private int  m_ArmTrCur;
    private int  m_ArmProx;

    void ComenzarCarga()
    {
        m_Valida = false;
        m_LineaN = 0;
        m_Count = 0;
        m_Cursor = 0;
        m_ArmFase = 0;
        m_ArmIt = 0;
        m_ArmFin = 0;
        m_ArmF = false;
        m_ArmBuscando = false;
        m_ArmFaseEmitida = false;
        m_ArmHayEmitida = false;
        m_ArmTrCur = 0;
        m_ArmProx = 1;
    }

    // Una fila del CSV. true = entro a la cinta. El header (linea 1) se saltea como siempre:
    // ⚠ el contador es del OBJETO, no del lote -- si fuera local a la tajada se perderia una fila
    // por tajada (es el mutante M2 del banco, y muere por esto).
    bool AgregarFila(string linea)
    {
        m_LineaN = m_LineaN + 1;
        if (m_LineaN == 1) return false;             // header autodescriptivo
        ref TStringArray tk = new TStringArray;
        linea.Split(",", tk);
        if (tk.Count() < 12) return false;
        m_S.Insert(tk.Get(0).ToFloat());
        m_X.Insert(tk.Get(1).ToFloat());
        m_Z.Insert(tk.Get(3).ToFloat());
        m_Y.Insert(tk.Get(2).ToFloat());
        m_H.Insert(tk.Get(4).ToFloat());
        m_Kmh.Insert(tk.Get(5).ToFloat());
        m_Fw.Insert(tk.Get(6).ToFloat());
        m_Thr.Insert(tk.Get(7).ToFloat());
        m_Brk.Insert(tk.Get(8).ToFloat());
        m_Gear.Insert(tk.Get(9).ToInt());
        float strF = -9;
        if (tk.Count() > 12) strF = tk.Get(12).ToFloat();
        m_Str.Insert(strF);
        int corteF = 0;
        if (tk.Count() > 13) corteF = tk.Get(13).ToInt();
        m_Corte.Insert(corteF);
        float rollF = -99;
        if (tk.Count() > 14) rollF = tk.Get(14).ToFloat();
        m_Roll.Insert(rollF);
        float caminaF = 0;
        if (tk.Count() > 15) caminaF = tk.Get(15).ToFloat();
        m_Camina.Insert(caminaF);
        return true;
    }

    // El post-proceso, en tajadas. true = termino. Cada llamada hace a lo sumo ARM_LOTE avances de
    // indice CONTANDO los del escaneo interno: un bloque largo de una sola fase no puede comerse
    // el cuadro. ⚠ El escaneo se REANUDA donde quedo (m_ArmBuscando): si rearrancara, los tramos
    // saldrian mal y el bucle no terminaria nunca (mutante M6 del banco).
    bool ArmarPaso()
    {
        int pasos = 0;
        while (pasos < ARM_LOTE)
        {
            if (m_ArmFase == 0)
            {
                m_Count = m_X.Count();
                m_Cursor = 0;
                m_Valida = (m_Count > 10);
                // tramos de fase (bloques contiguos de gear-reversa / no-reversa)
                m_TramoS = new array<float>();
                m_TramoIdx = new array<int>();
                m_TramoRev = new array<bool>();
                m_UltTramoOk = -1;
                m_MesetaIni = -1;
                m_MesetaFin = -1;
                m_MesetaUltFin = -1;
                m_ArmIt = 0;
                if (!m_Valida) { m_ArmFase = 3; return true; }
                m_FrameTramo = new array<int>();
                m_ArmFase = 1;
            }
            else if (m_ArmFase == 1)
            {
                // ⚠ SOLO TRAMOS REALES (largo >= TRAMO_FASE_MIN_M): los micro-blips
                // de gear (la R de 0.0 m del spawn en SPEED07/08, cambios de un
                // frame) NO son fases — sembrar contra ellos mando el cursor a
                // 130 m en el espejo (1014 perdidas). Un bloque corto se absorbe en
                // la fase que lo rodea.
                if (m_ArmIt >= m_Count)
                {
                    m_ArmIt = 0;
                    m_ArmFase = 2;
                }
                else
                {
                    if (!m_ArmBuscando)
                    {
                        m_ArmF = (m_Gear.Get(m_ArmIt) == 0);
                        m_ArmFin = m_ArmIt;
                        m_ArmBuscando = true;
                    }
                    while (m_ArmFin < m_Count - 1 && (m_Gear.Get(m_ArmFin + 1) == 0) == m_ArmF)
                    {
                        m_ArmFin = m_ArmFin + 1;
                        pasos = pasos + 1;
                        if (pasos >= ARM_LOTE) return false;   // el escaneo SIGUE donde quedo
                    }
                    float largo = m_S.Get(m_ArmFin) - m_S.Get(m_ArmIt);
                    if (largo >= TRAMO_FASE_MIN_M && (!m_ArmHayEmitida || m_ArmF != m_ArmFaseEmitida))
                    {
                        m_TramoS.Insert(m_S.Get(m_ArmIt));
                        m_TramoIdx.Insert(m_ArmIt);
                        m_TramoRev.Insert(m_ArmF);
                        m_ArmFaseEmitida = m_ArmF;
                        m_ArmHayEmitida = true;
                    }
                    m_ArmIt = m_ArmFin + 1;
                    m_ArmBuscando = false;
                    pasos = pasos + 1;
                }
            }
            else if (m_ArmFase == 2)
            {
                // mapa frame -> tramo emitido (los micro-bloques absorben al previo)
                if (m_ArmIt >= m_Count)
                {
                    m_ArmFase = 3;
                }
                else
                {
                    if (m_ArmProx < m_TramoIdx.Count() && m_ArmIt >= m_TramoIdx.Get(m_ArmProx))
                    {
                        m_ArmTrCur = m_ArmProx;
                        m_ArmProx = m_ArmProx + 1;
                    }
                    m_FrameTramo.Insert(m_ArmTrCur);
                    m_ArmIt = m_ArmIt + 1;
                    pasos = pasos + 1;
                }
            }
            else
            {
                return true;
            }
        }
        return false;
    }

    // ★★★ LA SIEMBRA DISPARADA POR EL LEG (, fix de H-fase-stale,
    // opcion b de ): el controller la llama cuando SU leg cambia de
    // fase fwd↔rev — el leg siempre sabe su fase y siempre transiciona, no
    // depende de que la cinta haya sido llamada (la dependencia circular que
    // cego al detector interno). Blanco: el proximo tramo de la fase nueva
    // despues del tramo EN CONSUMO; declara consumo (monotonia, ).
    void SembrarFase(bool esperaRev, vector pos, float rumboDeg)
    {
        if (!m_Valida || !m_TramoS) return;
        if (!BZDriveController.s_Quiet && BZDiag.On()) Print("[Cinta] CAMBIO-FASE(leg) a " + esperaRev.ToString() + " · s_est " + m_SEst.ToString() + " · ultConsumido " + m_UltTramoOk.ToString() + Suf());
        int kt;
        int ktMono = -1;
        for (kt = m_UltTramoOk + 1; kt < m_TramoS.Count(); kt++)
        {
            if (m_TramoRev.Get(kt) == esperaRev)
            {
                ktMono = kt;
                break;
            }
        }
        // ★ 1.2.4: EL BLANCO DE LA MONOTONIA SOLO VALE CERCA. El flip del leg ocurre en el cusp y el tramo nuevo
        // empieza en ese reposo: un blanco a mas de SIEMBRA_LEJOS_M es la contabilidad rota (medido: 550 m) y la
        // monotonia cede ante la POSICION del vehiculo: el inicio de tramo de la fase nueva mas cercano, y si tampoco
        // hay uno cerca, el cuadro de la fase mas cercano (la salida SIN-BLANCO de siempre).
        float topeS = SIEMBRA_LEJOS_M;
        if (ktMono >= 0)
        {
            float dMono = DistAlCuadro(pos, m_TramoIdx.Get(ktMono));
            if (dMono <= topeS)
            {
                SembrarEnTramo(ktMono, "SIEMBRA tramo " + ktMono.ToString(), dMono, true);
                return;
            }
            if (!BZDriveController.s_Quiet) Print("[Cinta] SIEMBRA LEJOS: el tramo " + ktMono.ToString() + " (el que seguia por monotonia, s " + m_TramoS.Get(ktMono).ToString() + ") empieza a " + dMono.ToString() + " m del vehiculo (tope " + topeS.ToString() + "): la contabilidad de tramos quedo mal, se re-ancla por posicion" + Suf());
        }
        float dIni = -1;
        int ktIni = InicioDeTramoMasCercano(pos, rumboDeg, esperaRev, dIni);
        if (ktIni >= 0 && dIni <= topeS)
        {
            SembrarEnTramo(ktIni, "SIEMBRA-REANCLA tramo " + ktIni.ToString() + " (el inicio de tramo de la fase nueva mas cercano)", dIni, false);
            return;
        }
        // ★★★ SALIDA DECLARADA DEL SIN-BLANCO: sin esto el
        // cursor quedaba congelado PARA SIEMPRE y el vehiculo manejaba a
        // ciegas hasta el choque (08b: d 9→21 m monotono → STUCK wp202; 08:
        // ultCons clavado en 6 el resto de la ruta). SIN BLANCO con tramos de
        // la fase EXISTENTES = la contabilidad quedo probadamente mal — la
        // premisa de la monotonia (cursor sano) se rompio, asi que la guarda
        // cede ante un RE-ANCLAJE POR PROXIMIDAD (el cuadro de la fase nueva
        // mas cercano a la posicion real, con el rumbo del vehiculo), declarado
        // en el log, y la monotonia RE-ARRANCA desde el tramo re-anclado (puede
        // rebobinar: eso es el punto). Si la fase nueva no existe en la cinta,
        // el aviso viejo queda (ausencia real, no bloqueo).
        float dCua = -1;
        int jMin = CuadroDeFaseMasCercano(pos, rumboDeg, esperaRev, dCua);
        if (jMin >= 0)
        {
            int ktR = m_FrameTramo.Get(jMin);
            m_SEst = m_S.Get(jMin);
            m_Cursor = jMin;
            m_TicksLejos = 0;
            m_RescFallidos = 0;
            m_UltTramoOk = ktR;
            m_BordeTicks = 0;
            m_MesetaIni = -1;    // la siembra borra la meseta en curso
            m_MesetaFin = -1;
            m_MesetaUltFin = -1;
            m_PrevQOk = false;   // mismo enganche sembrado que la pass-1
            if (!BZDriveController.s_Quiet) Print("[Cinta] SIEMBRA-REANCLA tramo " + ktR.ToString() + " (cuadro " + jMin.ToString() + ") · s_est -> " + m_SEst.ToString() + " · dist " + dCua.ToString() + " m (monotonia re-armada)" + Suf());
            return;
        }
        if (!BZDriveController.s_Quiet) Print("[Cinta] SIEMBRA SIN BLANCO (no hay tramo de la fase nueva despues de " + m_UltTramoOk.ToString() + " ni cuadros de esa fase con el rumbo del vehiculo)" + Suf());
    }

    // la siembra en el inicio de un tramo: cursor y s_est ahi, contadores a cero, meseta borrada y ENGANCHE SEMBRADO
    // (m_PrevQOk=false: el primer Muestra no suma la odometria acumulada durante el gap de posesion y rescata fino
    // alrededor del punto sembrado, con toda la maquinaria de anclas ya puesta; sin esto el leg arrancaba leyendo
    // entradas de un arco corrido, ~25 wp a 0.8-1.1 m tras cada flip). soloConDiag: la siembra normal solo habla con
    // Diagnostics; la re-anclada habla siempre.
    private void SembrarEnTramo(int kt, string etiqueta, float distM, bool soloConDiag)
    {
        m_SEst = m_TramoS.Get(kt);
        m_Cursor = m_TramoIdx.Get(kt);
        m_TicksLejos = 0;
        m_RescFallidos = 0;
        m_UltTramoOk = kt;
        m_BordeTicks = 0;   // el flip resuelve la contradiccion del borde
        m_MesetaIni = -1;   // la siembra borra la meseta en curso
        m_MesetaFin = -1;
        m_MesetaUltFin = -1;
        m_PrevQOk = false;
        bool hablaS = !BZDriveController.s_Quiet;
        if (soloConDiag && !BZDiag.On()) hablaS = false;
        if (hablaS) Print("[Cinta] " + etiqueta + " · s_est -> " + m_SEst.ToString() + " · el inicio del tramo a " + distM.ToString() + " m del vehiculo" + Suf());
    }

    // ¿Que tramo de fase contiene este arco? (-1 si no hay tramos)
    private int TramoDe(float s)
    {
        if (!m_TramoS || m_TramoS.Count() == 0) return -1;
        int k;
        int r = 0;
        for (k = 0; k < m_TramoS.Count(); k++)
        {
            if (m_TramoS.Get(k) <= s) r = k;
            else break;
        }
        return r;
    }

    // La consulta ESPACIAL: proyecta la posicion sobre la cinta cerca del cursor,
    // avanza el cursor (monotono, con cota) e interpola el input entre los dos
    // frames vecinos. Falso = cinta perdida (lejos): el llamador cae al camino
    // normal ESTE tick y lo reporta -- no se inventa un input.
    // ⚠⚠⚠ LECTOR HIBRIDO: ODOMETRIA POR ARCO + RE-ANCLAJE POR PROYECCION. Dos
    // hallazgos del espejo lo forzaron ANTES de shippear:
    //  (1) el nearest-por-vertice en tramos densos duplicaba las reversiones
    //      (0.49 -> 0.99/s) -- la proyeccion cruda es mal-planteada a escala
    //      sub-decimetrica;
    //  (2) la POSICION del frame esta cuantizada a ~10-16 Hz (medido: paso 0.000
    //      exacto durante 8 frames con chassis_kmh 12 y la rueda barriendo
    //      2 deg/frame) -- el eje espacial por posiciones tiene MESETAS justo
    //      donde la textura del volante es mas fina (error de 14 deg si se indexa
    //      por posicion ahi). El eje bueno es s = integral(v*dt), 40 Hz real
    //      (lo emite el conversor E1).
    // Politica: s_est avanza con el DESPLAZAMIENTO del vehiculo (continuo en
    // server); el cursor camina el arco monotono; la proyeccion RE-ANCLA s_est
    // SOLO donde el paso de posicion de la cinta es limpio (> ANCLA_PASO_M) --
    // en los nudos densos manda la odometria pura (el error acumulado en un nudo
    // de decenas de metros es cm). Teleport/respawn (salto de consulta grande)
    // resetea la odometria a proyeccion global corta.
    private const float ANCLA_PASO_M = 0.25;
    // ★ EL ANCLAJE POR CUERDA (las dos fases, solo con el casco rodando). En un tape lento (5-15 km/h) los cuadros estan
    // a 5-10 cm, por debajo de ANCLA_PASO_M: el ancla cuadro a cuadro nunca se acepta ("nudo denso", ver abajo) y el
    // cursor corre por odometria pura hasta el rescate (distM > RESCATE_M durante RESCATE_TICKS). Y la odometria queda
    // CORTA donde la toma es lenta: el s grabado se infla con el ruido de posicion de la grabacion (medido sobre las
    // cintas: las cuerdas crudas de sus posiciones dan 3-7 % mas que su s a 5-15 km/h y 1,000 de 20 km/h en adelante),
    // y el casco real rueda suave y suma menos. Medido en una reversa final de -86 grados con el volante serruchado
    // (OffroadHB02R): el cursor 1,3-1,6 m ATRAS del casco, el volante replayado en contrafase (+-17 grados) y |lat| 0,92
    // m; con el cursor en su lugar el banco da 0,22. Y en AVANCE (CivSedan, leg de 12-16 km/h hacia un cusp): 5,5 %
    // corto, LEJOS x12 SIN CANDIDATO, rescate de +1,29 m en plena curva y el leg cerrado 1,1-1,2 m de costado; en la
    // version anterior el mismo leg arrancaba con el cursor 1,5 m ADELANTE (siembra imprecisa) y la deriva lo cancelaba.
    // La ventana de anclas es la de cuadros (cursor-3..+6) AMPLIADA por arco (atras/adelante del s_est), y la cuerda de
    // >= ANCLA_PASO_M se arma sobre varios cuadros del mismo tramo; las reglas de adopcion son las de siempre. En tapes
    // de cuadros largos (>= 25 cm, alta velocidad) la cuerda es el cuadro siguiente: lo mismo que antes.
    private const float ANCLA_ARCO_ATRAS_M = 0.5;
    private const float ANCLA_ARCO_ADELANTE_M = 2.5;
    private const int   ANCLA_ARCO_COTA = 400;
    static const float SALTO_CONSULTA_M = 8.0;   // la lee tambien el controlador (MarcarStopServido): UNA sola distancia de "llego a la meseta"
    // continuidad de arco del ancla (costo d² + (Δarco/λ)²).
    // λ=5: a Δarco 60 m el termino vale 144 m² — ninguna pasada ajena gana.
    private const float ANCLA_ARCO_LAMBDA_M = 5.0;

    // ★★★ ANTICIPACION DE ENTRADA AL LOCK. El off-tracking al tope
    // es irreducible EN el lock (30°+Δ se clampea: cero autoridad) y aparece con
    // la misma firma en 2 rutas (06 −1.37, dificil +1.09) = mecanismo general
    // "humano al lock". La unica palanca esta ANTES: entrar por ADENTRO mientras
    // cFw<tope y hay margen, para repartir el error. ⚠ GATEADO POR LARGO del
    // tramo al tope: la deriva crece ~½·Δk·L² — los locks CORTOS de la toma
    // (2-2.7 m en s58-73/379-391, maniobras lentas) trackean perfecto (~3 cm) y
    // NO deben tocarse; medido en el espejo: con largo≥8 m dispara SOLO el
    // hairpin final (6 vs 31 muestras-trigger sin gate). Devuelve el fw PICO
    // FIRMADO del lock que viene (o 0): el signo dice de que lado es "adentro".
    // Cache por arco (re-scan cada ~1 m): el scan es O(ventana) frames.
    private const float ENTRADA_ALTO_DEG   = 28.0;  // umbral "esto es lock"
    private const float ENTRADA_SOSTEN_DEG = 24.0;  // el tramo sigue (permite respiros)
    private const float ENTRADA_LARGO_M    = 8.0;   // largo minimo del tramo al tope
    private float m_LockCache;
    private float m_LockCacheS;
    private bool  m_LockCacheOk;

    float LockAdelante(float ventanaM)
    {
        if (!m_Valida) return 0;
        if (m_LockCacheOk && Math.AbsFloat(m_SEst - m_LockCacheS) < 1.0) return m_LockCache;
        m_LockCacheS = m_SEst;
        m_LockCacheOk = true;
        m_LockCache = 0;
        int j = m_Cursor;
        int primera = -1;
        while (j < m_Count - 1 && m_S.Get(j) < m_SEst + ventanaM)
        {
            if (Math.AbsFloat(m_Fw.Get(j)) >= ENTRADA_ALTO_DEG)
            {
                primera = j;
                break;
            }
            j = j + 1;
        }
        if (primera < 0) return 0;
        float pico = m_Fw.Get(primera);
        int k = primera;
        while (k < m_Count - 1 && Math.AbsFloat(m_Fw.Get(k)) >= ENTRADA_SOSTEN_DEG && m_S.Get(k) < m_S.Get(primera) + 45.0)
        {
            if (Math.AbsFloat(m_Fw.Get(k)) > Math.AbsFloat(pico)) pico = m_Fw.Get(k);
            k = k + 1;
        }
        if (m_S.Get(k) - m_S.Get(primera) >= ENTRADA_LARGO_M) m_LockCache = pico;
        return m_LockCache;
    }

    // ★★★ FASE ESPERADA: en rutas con cusps las pasadas fwd/rev PISAN
    // EL MISMO SUELO. Si el vehiculo se desvia sobre suelo compartido, el ancla
    // por punto-mas-cercano salta a la OTRA pasada (misma posicion, otro arco):
    // el gear leido queda en la fase equivocada, la coherencia del controller
    // rechaza — correctamente — y como el suelo superpuesto da distM CHICO, el
    // rescate nunca dispara: OFF congelado para siempre (SPEED07 2º cusp:
    // OFF(0.88) constante → 39 m de fly-off reactivo). Doble fix, mismo
    // mecanismo: (1) el scan de anclas SALTA segmentos cuya fase de gear no es
    // la esperada (la del leg) — el ancla no puede cruzar de pasada; (2) el
    // MISMATCH de fase en el cursor cuenta como "lejos" → dispara el rescate,
    // que con el filtro engancha la pasada correcta. Medido en espejo con
    // SPEED04 (overlap real): cursor en fase correcta 72% → 97%.
    // ✖ NO-EMBARCADO:
    // el "filtro de pasada por RUMBO" — la cinta real de M4tractor mostro que
    // los pares conflictivos del shuffle son de MISMO rumbo (dH 0-48°) y los
    // de rumbo opuesto eran inter-FASE (ya filtrados). El teleport real
    // (cruzado log-CSV × tramos) es intra-tramo: ver la invariante EL-ANCLA-
    // SOLO-ACERCA en la aceptacion, abajo.
    // `rodando`: el consultante se mueve (>= el umbral de rodadura del controlador). La cuerda del anclaje se apaga sin eso:
    // ver el bloque de la cuerda.
    bool Muestra(vector pos, BZCintaMuestra res, bool esperaRev, bool rodando)
    {
        if (!m_Valida) return false;
        int j;
        float dx;
        float dz;
        // odometria: avanza s_est con el desplazamiento real del consultante
        if (m_PrevQOk)
        {
            dx = pos[0] - m_PrevQ[0];
            dz = pos[2] - m_PrevQ[2];
            float av = Math.Sqrt(dx * dx + dz * dz);
            if (av < SALTO_CONSULTA_M) m_SEst = m_SEst + av;
            else m_PrevQOk = false;                  // teleport: re-anclar abajo
        }
        m_PrevQ = pos;
        // ★★★ CLAMP DE FASE DEL CURSOR: el cursor NO avanza ni
        // consume mas alla del final del tramo de la fase que esta manejando.
        // Sin esto, la odometria pura (los frames SIN CANDIDATO del trazo)
        // cruzaba el borde manejando de FRENTE y consumia el tramo de REVERSA
        // (08: ultCons 0→1 entre s 62 y 84, ANTES del primer cusp) → la
        // siembra, CORRECTA con su contabilidad envenenada, elegia el tramo 3
        // (+47.6 m) porque el 1 estaba prohibido por monotonia → cascada
        // (skipCons 400-800, ultDistM 12→35 m, cintaR 21%→4%). El cursor
        // espera EN el borde; quien cruza la frontera de fase es SIEMPRE el
        // flip del leg (SembrarFase). Geometrico, cero perillas. Consecuencia:
        // ultCons camina de a UNO por construccion. El mismo limite se aplica
        // al torneo de anclas (trClamp): un candidato de un tramo por DELANTE
        // del que se maneja no compite — ni via rescate escalonado.
        int trClamp = -1;
        bool clampeoBorde = false;
        if (m_TramoS && m_TramoS.Count() > 0)
        {
            trClamp = TramoDe(m_SEst);
            while (trClamp > 0 && m_TramoRev.Get(trClamp) != esperaRev) trClamp = trClamp - 1;
            int trClampU = UltimoTramoDeFase(trClamp);   // 1.2.4: hasta el fin de la fase ENTERA
            if (m_TramoRev.Get(trClamp) == esperaRev && trClampU < m_TramoS.Count() - 1)
            {
                float finFase = m_TramoS.Get(trClampU + 1) - 0.05;
                if (m_SEst > finFase)
                {
                    m_SEst = finFase;
                    clampeoBorde = true;   // el clamp RETUVO: el casco quiere seguir y la cinta no tiene mas
                }
            }
        }
        // ✖ RETIRADO el detector interno de cambio de fase. Vivia
        // de la MEMORIA DE LLAMADAS (m_FasePrev solo se actualizaba si la cinta
        // era llamada) y las llamadas dependen del gate del controller: en el
        // leg fwd intermedio de un shuffle la cinta no se llama → el detector
        // quedaba stale y NUNCA veia el proximo cusp (medido: 08 con 6 cusps
        // detectaba 2; 07 con 3-4 detectaba 1 → fallback → zigzag/fly-off).
        // La siembra ahora la dispara EL LEG del controller (SembrarFase), que
        // siempre sabe su fase y siempre transiciona.
        // el cursor camina el arco (monotono)
        while (m_Cursor < m_Count - 2 && m_S.Get(m_Cursor + 1) <= m_SEst)
        {
            m_Cursor = m_Cursor + 1;
        }
        // la meseta grabada (ver arriba). Anclado: el cursor vuelve a la entrada en cada consulta
        // hasta reproducir tantas consultas como frames duro; despues, la salida.
        if (m_MesetaIni >= 0)
        {
            m_MesetaLlamadas = m_MesetaLlamadas + 1;
            if (m_MesetaLlamadas > m_MesetaFin - m_MesetaIni) SalirDeMeseta("cumplida");
            else
            {
                m_Cursor = m_MesetaIni;
                m_SEst = m_S.Get(m_MesetaIni);
            }
        }
        else if (m_Kmh.Get(m_Cursor) < MESETA_KMH && m_Cursor > m_MesetaUltFin) EntrarEnMeseta(m_Cursor);
        // re-anclaje por proyeccion, SOLO en tramo posicion-limpio
        int lo = m_Cursor - 3;
        if (lo < 0) lo = 0;
        int hi = m_Cursor + 6;
        if (hi > m_Count - 2) hi = m_Count - 2;
        float pasoMin = ANCLA_PASO_M;
        bool enRescate = false;
        bool cuerda = false;
        // ⚠ SOLO CON EL CASCO RODANDO. Con el vehiculo quieto en el cusp (la meseta grabada y los 0,3 s que tarda en
        // moverse tras la salida) la proyeccion sobre la cuerda corria el cursor 5-10 cm ADELANTE, a cuadros grabados a
        // 2,1-2,4 km/h; la guarda del AR lee la v grabada de la fila del cursor ("no es fila de salida" si > 2 km/h) y
        // con el reposo acumulado en la meseta disparaba un FISICO 14-105 ms despues de la salida, con el vehiculo ya
        // arrancando solo (medido en 3 de 8 rutas con tramo de cola; con el cursor por odometria, 0). Quieto, el cursor
        // se queda en la fila de salida como siempre; al rodar, la cuerda vuelve y el final de la reversa no cambia.
        // En las DOS fases (ver ANCLA_ARCO_*): en avance lento el torneo por cuadros consecutivos tampoco anclaba.
        if (rodando)
        {
            // la ventana de cuadros ampliada por arco, y la cuerda sobre varios cuadros
            cuerda = true;
            int pasosR = 0;
            while (lo > 0 && m_SEst - m_S.Get(lo) < ANCLA_ARCO_ATRAS_M && pasosR < ANCLA_ARCO_COTA)
            {
                lo = lo - 1;
                pasosR = pasosR + 1;
            }
            pasosR = 0;
            while (hi < m_Count - 2 && m_S.Get(hi) - m_SEst < ANCLA_ARCO_ADELANTE_M && pasosR < ANCLA_ARCO_COTA)
            {
                hi = hi + 1;
                pasosR = pasosR + 1;
            }
        }
        if (!m_PrevQOk || m_TicksLejos >= RESCATE_TICKS)
        {
            // ventana ESPACIAL alrededor de s_est, ESCALONADA por fallos seguidos
            enRescate = true;
            float alcance = RESCATE_ARCO_M * (1 + m_RescFallidos);
            if (alcance > RESCATE_ARCO_MAX) alcance = RESCATE_ARCO_MAX;
            int pasos = 0;
            lo = m_Cursor;
            while (lo > 0 && m_SEst - m_S.Get(lo) < alcance && pasos < COTA_RESCATE)
            {
                lo = lo - 1;
                pasos = pasos + 1;
            }
            pasos = 0;
            hi = m_Cursor;
            while (hi < m_Count - 2 && m_S.Get(hi) - m_SEst < alcance && pasos < COTA_RESCATE)
            {
                hi = hi + 1;
                pasos = pasos + 1;
            }
            m_PrevQOk = true;
            m_TicksLejos = 0;
            pasoMin = RESCATE_PASO_M;
            cuerda = false;   // el rescate escalonado sigue como siempre
        }
        float sx;
        float sz;
        float seg2;
        float t;
        float qx;
        float qz;
        float d2;
        // ★★★ EL ANCLA SE ELIGE POR COSTO POSICION+ARCO, NO NEAREST PURO
        // en SPEED07 los DOS tramos de reversa pisan el mismo suelo
        // (min dist 0.07 m entre rev#0 y rev#1) — el filtro de fase no separa
        // rev/rev, y el rescate ESCALONADO (hasta ±60 m de arco) alcanzaba la
        // otra pasada: el nearest de posicion la adoptaba y el cursor saltaba
        // ~60 m de arco → OFF congelado → fly-off −30 m. Con el costo
        // d² + (Δarco/λ)², saltar 60 m de arco exige una ventaja de posicion de
        // >12 m — imposible cuando la pasada correcta esta a metros. Y en un
        // desync real SIN competidor (la jesko, 15 m) el ancla lejana sigue
        // ganando porque no hay alternativa (espejo: recupera igual, 0.74 m).
        // Es la guarda anti-rama del rescate, extendida a la ELECCION del ancla.
        int mejor = -1;
        int mejor2 = -1;   // el cuadro del final de la cuerda elegida (j + 1, o mas lejos con la cuerda armada)
        int j2;
        float tmejor = 0;
        float dmin = 1000000000.0;
        float dPosMejor = 1000000000.0;
        bool segEnRev;
        float dArco;
        float costo;
        m_SkipConsumidos = 0;
        m_SkipClamp = 0;
        for (j = lo; j <= hi; j++)
        {
            // MONOTONIA: tramo ya cerrado = candidato PROHIBIDO
            if (m_FrameTramo.Get(j) < m_UltTramoOk)
            {
                m_SkipConsumidos = m_SkipConsumidos + 1;
                continue;
            }
            // CLAMP DE FASE: tramo por delante del que se maneja
            // tampoco compite — el futuro se cruza con el flip del leg
            if (trClamp >= 0 && m_FrameTramo.Get(j) > trClamp)
            {
                m_SkipClamp = m_SkipClamp + 1;
                continue;
            }
            segEnRev = (m_Gear.Get(j) == 0);
            if (segEnRev != esperaRev) continue;      // fase equivocada: sin ancla
            j2 = j + 1;
            if (cuerda) j2 = FinDeCuerda(j, hi, pasoMin);
            if (j2 < 0) continue;                      // no hay cuerda de largo suficiente en el tramo
            sx = m_X.Get(j2) - m_X.Get(j);
            sz = m_Z.Get(j2) - m_Z.Get(j);
            seg2 = sx * sx + sz * sz;
            if (seg2 < pasoMin * pasoMin) continue;   // nudo denso: sin ancla
            // un RETROCESO GRABADO no es camino: la correccion de red de la toma deja cuadros que van hacia atras (medido:
            // 2,4 m en una toma, 2,3 en otra; con ellos la cuerda seguia a la toma que retrocedia y el cursor saltaba
            // atras 8 veces). La cuerda que va contra el rumbo grabado de su tramo se descarta; en reversa la cuerda va
            // contra el morro por definicion, asi que ahi se descarta la que va A FAVOR. El cursor sigue por odometria
            // mientras dura el retroceso y vuelve a anclar cuando la toma vuelve a avanzar.
            float hRetJ = m_H.Get(j) * Math.DEG2RAD;
            float haciaJ = sx * Math.Sin(hRetJ) + sz * Math.Cos(hRetJ);
            if (!segEnRev && haciaJ < 0) continue;
            if (segEnRev && haciaJ > 0) continue;
            t = ((pos[0] - m_X.Get(j)) * sx + (pos[2] - m_Z.Get(j)) * sz) / seg2;
            t = Math.Clamp(t, 0.0, 1.0);
            qx = m_X.Get(j) + sx * t;
            qz = m_Z.Get(j) + sz * t;
            dx = pos[0] - qx;
            dz = pos[2] - qz;
            d2 = dx * dx + dz * dz;
            // ⚠⚠⚠ UN CANDIDATO IN-ACEPTABLE NO COMPITE (, F-rigido
            // medido): el costo-de-arco prefiere segmentos arco-cercanos, y si
            // el mejor-por-costo tiene la proyeccion CLAMPEADA al borde (t=0/1,
            // el vehiculo ya lo paso o no llego), la regla interior lo rechaza
            // DESPUES del torneo → ancla NULA aunque hubiera un candidato
            // interior valido un poco mas lejos → cursor congelado (rev#0 de
            // SPEED07 tras el cusp: OFF 2.3→10.4, engancha tarde en wp192).
            // El filtro va ANTES: solo pelean anclas que podrian aceptarse
            // (interiores, o pegadas a <0.3 m — el mismo criterio de siempre).
            if ((t <= 0.02 || t >= 0.98) && d2 >= 0.09) continue;
            dArco = m_S.Get(j) - m_SEst;
            costo = d2 + (dArco * dArco) / (ANCLA_ARCO_LAMBDA_M * ANCLA_ARCO_LAMBDA_M);
            if (costo < dmin)
            {
                dmin = costo;
                dPosMejor = d2;
                mejor = j;
                mejor2 = j2;
                tmejor = t;
            }
        }
        // ⚠ EL ANCLA SOLO SE ACEPTA SI ES INTERIOR (t no clampeado al borde) o
        // pegadisima (<0.3 m). Cazado por el espejo: un ancla con t=1 en zona densa
        // snapeaba s_est HACIA ATRAS en cada consulta y CONGELABA el cursor (la
        // odometria avanzaba y el ancla la descartaba, 19 deg de error sostenido).
        bool anclaOk = false;
        if (mejor >= 0)
        {
            // distA es la distancia de POSICION del ancla elegida (el costo con
            // arco decide QUIEN gana; la distancia que se reporta es fisica)
            float distA = Math.Sqrt(dPosMejor);
            if ((tmejor > 0.02 && tmejor < 0.98) || distA < 0.3) anclaOk = true;
            // ★★★ EL ANCLA SOLO ACERCA:
            // re-anclar existe para ACERCAR el cursor al casco — un ancla que
            // EMPEORA la distancia fisica actual no se adopta. El caso medido
            // (M4tractor, log 40 Hz t=44.08): el casco retrocedio apenas mas
            // alla del fin grabado del rulo de reversa (tramo 1, 11 m) → todas
            // las anclas del borde clampean a t=1 y la regla interior las mata
            // → el UNICO candidato interior que quedaba era el ARRANQUE del
            // rulo, a 9.7 m → el torneo lo adoptaba: s_est saltaba −9.75 con
            // el casco a 1.35 m (d es contra el cursor: el que se fue era el
            // CURSOR). 15 teleports de ±5-10 m en la corrida, oscilando dentro
            // de tramos-rulo de 11-17 m — invisibles al RPT, cazados por el
            // CSV. Ni monotonia ni clamp ni fase aplican (mismo tramo); el
            // filtro por RUMBO quedo refutado con la cinta real (los pares
            // conflictivos son mismo-rumbo, dH 0-48°). La invariante es
            // fisica y sin perillas: distA <= dist(pos, cursor actual) +
            // margen de ruido. El rescate GENUINO sigue vivo por construccion
            // (jesko 15 m: cualquier ancla a 0.74 ACERCA); lo unico que muere
            // es la adopcion que ALEJA — el cursor se queda clampeado en el
            // borde esperando el flip del leg, que siembra limpio (medido:
            // t=45.4 el leg flipeo y sembro el tramo 2 exacto).
            if (anclaOk)
            {
                float dcx = pos[0] - m_X.Get(m_Cursor);
                float dcz = pos[2] - m_Z.Get(m_Cursor);
                float dCursorAct = Math.Sqrt(dcx * dcx + dcz * dcz);
                if (distA > dCursorAct + ANCLA_ACERCA_MARGEN_M) anclaOk = false;
            }
            if (anclaOk)
            {
                m_Cursor = mejor;
                m_SEst = m_S.Get(mejor) + (m_S.Get(mejor2) - m_S.Get(mejor)) * tmejor;
                res.distM = distA;
                // la cuerda de reversa abarca varios cuadros: el cursor camina hasta el cuadro del s_est
                while (m_Cursor < m_Count - 2 && m_S.Get(m_Cursor + 1) <= m_SEst) m_Cursor = m_Cursor + 1;
            }
        }
        if (!anclaOk)
        {
            // nudo denso / ancla rechazada: odometria pura; la deriva reportada es
            // contra el punto del arco estimado
            dx = pos[0] - m_X.Get(m_Cursor);
            dz = pos[2] - m_Z.Get(m_Cursor);
            res.distM = Math.Sqrt(dx * dx + dz * dz);
        }
        // instrumentacion del cusp: en estado LEJOS, la decision del
        // ancla completa, cada 8 ticks (~5 Hz) para no inundar
        if (res.distM > RESCATE_M && !BZDriveController.s_Quiet && BZDiag.On())
        {
            m_DiagLejos = m_DiagLejos + 1;
            if ((m_DiagLejos % 8) == 1)
            {
                string dl = "[Cinta] LEJOS d " + res.distM.ToString();
                dl = dl + " · s_est " + m_SEst.ToString();
                dl = dl + " · esperaRev " + esperaRev.ToString();
                dl = dl + " · ultCons " + m_UltTramoOk.ToString();
                dl = dl + " · skipCons " + m_SkipConsumidos.ToString();
                dl = dl + " · skipClamp " + m_SkipClamp.ToString() + " (trClamp " + trClamp.ToString() + ")";
                dl = dl + " · ventana s[" + m_S.Get(lo).ToString() + ".." + m_S.Get(hi).ToString() + "]";
                if (enRescate) dl = dl + " · RESCATE(x" + m_RescFallidos.ToString() + ")";
                if (mejor >= 0)
                {
                    dl = dl + " · gana s " + m_S.Get(mejor).ToString() + " dPos " + Math.Sqrt(dPosMejor).ToString() + " t " + tmejor.ToString();
                    dl = dl + " · anclaOk " + anclaOk.ToString();
                }
                else dl = dl + " · SIN CANDIDATO";
                Print(dl + Suf());
            }
        }
        else if (res.distM <= RESCATE_M) m_DiagLejos = 0;
        // contador del rescate: cuenta consultas seguidas LEJOS (incluye las
        // perdidas > PERDIDA_M — la meseta del 16-06 flickeaba a ambos lados de 6)
        if (res.distM > RESCATE_M) m_TicksLejos = m_TicksLejos + 1;
        else m_TicksLejos = 0;
        // escalonamiento: un rescate que NO dejo distM <= RESCATE_M cuenta como
        // fallido y el proximo escanea mas lejos; cualquier vuelta a cerca resetea
        if (enRescate && res.distM > RESCATE_M) m_RescFallidos = m_RescFallidos + 1;
        else if (res.distM <= RESCATE_M) m_RescFallidos = 0;
        // ⚠ CONSUMICION DEL TAPE POR TRACKING SANO, NO POR ANCLA (,
        // 2ª vuelta del espejo): en los tramos LENTOS (reversa a 7-14 km/h) los
        // pasos de posicion quedan bajo ANCLA_PASO_M → "nudo denso" → NUNCA se
        // acepta un ancla → registrar el tramo solo-en-el-ancla dejaba
        // m_UltTramoOk clavado y la siembra siguiente miraba PARA ATRAS (el
        // espejo lo cazo: seed a fwd#2 desde rev#1, 1014 perdidas). Cursor sano
        // (distM <= RESCATE_M) = tape consumido; el maximo monotono protege de
        // estados atascados (que ya quedaron en su tramo viejo).
        // ★ 1.2.4: UN LEG NUNCA CONSUME UN TRAMO DE LA OTRA FASE. Medido (APC, marca 1 con la R puesta en la parada):
        // al soltar el hold el cursor quedo en la ultima fila del tramo de avance, la meseta a la vista engancho los cuadros
        // de la R (ya del tramo de reversa), este consumo marco ese tramo y la siembra del leg de reversa salto al tramo
        // de reversa SIGUIENTE, a 550 m. La fase que se maneja (esperaRev) es la unica que puede quedar consumida.
        if (res.distM <= RESCATE_M)
        {
            int trOk = TramoDe(m_SEst);
            if (trOk >= 0 && m_TramoRev.Get(trOk) != esperaRev) trOk = -1;
            if (trOk > m_UltTramoOk) m_UltTramoOk = trOk;
        }
        // interpolacion POR ARCO entre el frame del cursor y el siguiente
        int b = m_Cursor + 1;
        if (b > m_Count - 1) b = m_Count - 1;
        float ds = m_S.Get(b) - m_S.Get(m_Cursor);
        t = 0;
        if (ds > 0.000001) t = Math.Clamp((m_SEst - m_S.Get(m_Cursor)) / ds, 0.0, 1.0);
        res.fwDeg = m_Fw.Get(m_Cursor) + (m_Fw.Get(b) - m_Fw.Get(m_Cursor)) * t;
        res.thr   = m_Thr.Get(m_Cursor) + (m_Thr.Get(b) - m_Thr.Get(m_Cursor)) * t;
        res.brk   = m_Brk.Get(m_Cursor) + (m_Brk.Get(b) - m_Brk.Get(m_Cursor)) * t;
        res.kmhRec = m_Kmh.Get(m_Cursor) + (m_Kmh.Get(b) - m_Kmh.Get(m_Cursor)) * t;
        // MOTO: el steering grabado (si la cinta lo trae) y la marca de corte de la fila del cursor
        res.steer = -9;
        res.corte = 0;
        if (m_Str.Count() == m_Count && m_Str.Get(m_Cursor) > -5 && m_Str.Get(b) > -5)
            res.steer = m_Str.Get(m_Cursor) + (m_Str.Get(b) - m_Str.Get(m_Cursor)) * t;
        if (m_Corte.Count() == m_Count) res.corte = m_Corte.Get(m_Cursor);
        res.roll = -99;
        if (m_Roll.Count() == m_Count && m_Roll.Get(m_Cursor) > -90 && m_Roll.Get(b) > -90)
            res.roll = m_Roll.Get(m_Cursor) + (m_Roll.Get(b) - m_Roll.Get(m_Cursor)) * t;
        // E3: el error FIRMADO contra la linea grabada. El rumbo se interpola por
        // el camino corto (wrap) y la lateral es la componente sobre la normal
        // DERECHA del rumbo grabado: rn = (cos h, -sin h) con dir = (sin h, cos h).
        float dh = m_H.Get(b) - m_H.Get(m_Cursor);
        while (dh > 180.0) dh = dh - 360.0;
        while (dh < -180.0) dh = dh + 360.0;
        res.headingDeg = m_H.Get(m_Cursor) + dh * t;
        float hr = res.headingDeg * Math.DEG2RAD;
        float qix = m_X.Get(m_Cursor) + (m_X.Get(b) - m_X.Get(m_Cursor)) * t;
        float qiz = m_Z.Get(m_Cursor) + (m_Z.Get(b) - m_Z.Get(m_Cursor)) * t;
        res.latM = (pos[0] - qix) * Math.Cos(hr) - (pos[2] - qiz) * Math.Sin(hr);
        // ★★★ LA PERDIDA SE DECIDE POR EJES, NO POR DISTANCIA TOTAL (,
        // falla 2 del fly-off del m4w). Con ~5m de deriva LATERAL (cursor SANO,
        // d≡cLat) el umbral total de 6 soltaba la cinta -> el FALLBACK reactivo
        // agarraba a 186 km/h con 5m de error y lo volaba 31-55m al bosque. Un
        // desvio lateral es RECUPERABLE (el iman con signo correcto + el replay
        // siguen siendo el mejor conductor); lo IRRECUPERABLE es el cursor
        // desincronizado (leer inputs de otro punto). Por eso:
        //   - residuo LONGITUDINAL (distM proyectada fuera del lateral) > 6 m
        //     => cursor perdido de verdad => soltar (y el rescate escalonado
        //     ya esta tratando de recuperarlo);
        //   - |lateral| > 12 m => el vehiculo se fue de verdad (bosque/choque)
        //     => soltar. Entre medio, LA CINTA SIGUE SIENDO LA DUEÑA.
        float latAbs = Math.AbsFloat(res.latM);
        float lon2 = res.distM * res.distM - latAbs * latAbs;
        if (lon2 < 0) lon2 = 0;
        if (Math.Sqrt(lon2) > PERDIDA_M) return false;
        if (latAbs > PERDIDA_LAT_M) return false;
        // ★★★ BORDE AGOTADO:
        // cursor CLAMPEADO en el fin del tramo + d CRECIENDO = contradiccion
        // fisica declarada — el cursor dice "la cinta se termino aca" y la
        // odometria dice "el casco sigue"; gana el casco. Seguir replayando el
        // frame-borde congelado es replayar un instante que ya paso: el casco
        // se pasa del cusp grabado 2-6 m con esos inputs y el sistema queda
        // semi-perdido 5-14 s (d flickeando en PERDIDA_M=6 — la meseta-de-6
        // del 16-06, con causa). Aca la cinta lo DECLARA (return false) y el
        // dueño del fin-de-leg (MC/parada/slam) maneja los metros que faltan;
        // la siembra del flip re-engancha como siempre. Banco con los CSV
        // reales (px,pz): las 2 aperturas del M4tractor caen EXACTO en los
        // nacimientos (t 43.3/64.9, latencia <0.5 s), cero falsos positivos
        // en 7 rutas, 1 apertura corta legitima en Tiger/ExpTractor (mismo
        // fenomeno chico). ⚠ La condicion es FISICA, no modal — la leccion de
        // la ventana-(b) fallida: medio
        // segundo de retencion sostenida + 1 m de error + 30 cm de
        // crecimiento, los tres verificables frame a frame en el CSV.
        if (clampeoBorde)
        {
            if (m_BordeTicks == 0) m_BordeDRef = res.distM;
            m_BordeTicks = m_BordeTicks + 1;
            float dPisoBorde = BORDE_D_MIN;
            if (m_BordeDRef + BORDE_D_DELTA > dPisoBorde) dPisoBorde = m_BordeDRef + BORDE_D_DELTA;
            if (m_BordeTicks >= BORDE_TICKS && res.distM > dPisoBorde)
            {
                if (!BZDriveController.s_Quiet && !m_BordeDiag)
                {
                    m_BordeDiag = true;
                    Print("[Cinta] BORDE-AGOTADO tramo " + trClamp.ToString() + " · s_est " + m_SEst.ToString() + " · d " + res.distM.ToString() + " (dRef " + m_BordeDRef.ToString() + ") · cede al dueño del fin-de-leg" + Suf());
                }
                return false;
            }
        }
        else
        {
            m_BordeTicks = 0;
            m_BordeDiag = false;
        }
        // la marcha es DISCRETA: el frame mas cercano (replayar la marcha del usuario
        // mata el hunting de caja en el ORIGEN -- el nunca cazo 3<->4)
        if (t > 0.5) res.gear = m_Gear.Get(b);
        else res.gear = m_Gear.Get(m_Cursor);
        res.enReversa = (res.gear == 0);
        res.borde = clampeoBorde;
        // fase equivocada en el cursor = una forma de "lejos": alimenta el
        // contador del rescate (el distM chico del suelo superpuesto no lo hace)
        if (res.enReversa != esperaRev) m_TicksLejos = m_TicksLejos + 1;
        return true;
    }
}
