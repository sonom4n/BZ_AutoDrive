// ============================================================================
//  BZRouteData.c - BZ_AutoDrive 1.2
//
//  La RUTA: inmutable despues de EndBuild(), con arco/rumbo/curvatura/legs
//  PRECOMPUTADOS UNA SOLA VEZ. Eso es lo que convierte cada consulta de ventana
//  en aritmetica en vez de busqueda (SEAM.md 5).
//
//  NO conoce BZWaypoint. Se llena por builder (BeginBuild/AddPoint/EndBuild), asi
//  un test arma una ruta sintetica sin tocar el pipeline ni el juego. El adaptador
//  desde BZWaypoint es un archivo aparte y es el UNICO que conoce ese tipo.
//
//  SEGMENTACION EN LEGS: arranca leg nuevo si legBreak (DECLARADO por el humano) o
//  si cambia el sentido de marcha (eso SI se infiere: meter la marcha ya es una
//  declaracion). Ver SEAM.md 7.
//
//  TOTALIDAD: todo accessor es bounds-safe y devuelve un default explicito. Misma
//  disciplina que la tabla del arbitro: nada de comportamiento implicito en los huecos.
// ============================================================================

class BZRouteData
{
    // ---- datos por waypoint ----
    private ref array<vector> m_Pos;
    private ref array<float>  m_RecKmh;
    private ref array<float>  m_RecThr;   // pedal grabado; -1 = la ruta no lo trae
    private ref array<float>  m_RecSteerDeg; // volante grabado en GRADOS de rueda; -999 = ausente
    private ref array<float>  m_RecHeadRad;   // rumbo GRABADO (targetHeading), -999 = sin dato
    private ref array<float>  m_Handbrake;
    private ref array<bool>   m_IsRev;
    private ref array<bool>   m_LegBreak;
    private ref array<bool>   m_IsStop;
    // INTENCION DECLARADA por el autor: "este tramo es de precision, a paso de hombre".
    // NO se re-deriva de la geometria (una curva cerrada de parking y una de maniobra normal
    // pueden tener la misma curvatura). Ver auditoria del string 'mode' de v1.1.
    private ref array<bool>   m_Tight;

    // ---- PRECOMPUTADO en EndBuild() ----
    private ref array<float>  m_S;        // arco acumulado desde wp0, en metros
    private ref array<float>  m_Head;     // tangente de la traza en el wp (radianes)
    private float m_ALatToma;             // agarre lateral p90 -- presupuesto de VELOCIDAD
    // ⚠⚠ SEGUNDO PERCENTIL, PARA LA DIRECCION. El mismo dato alimentaba dos consumidores con
    // necesidades OPUESTAS: para la velocidad ser conservador es SEGURO (vas mas lento); para
    // el cap de direccion es PROHIBITIVO (te veta el giro que la toma tiene). Medido
    // con p90 la demanda queda CLAVADA en el techo mientras el error lateral se
    // triplica, porque enderezar ~20 grados de rumbo a 108 km/h pide mas de lo que el p90 da.
    //
    // ★ p95 y NO p99: p95/p90 va de 1.24 a 1.47 en toda la flota --parejo, mide lo mismo en
    // cada vehiculo-- mientras p99/p90 va de 1.55 a 3.77, heredando de forma desigual la cola
    // sucia de la conversion (el Gemera salta 3.8x, con un maximo de 120 que es basura).
    // Se elige robustez sobre margen cuando el margen extra viene de datos que no confiamos.
    private float m_ALatDir;
    // ⚠ Y CADA PERCENTIL CON SU PROPIO TOPE DE BUSQUEDA. El de 40 (4 g) se eligio para el p90
    // como filtro de basura, pero el p95 del GT63 es 46.61: con 40 saldria CLAMPEADO en 40 y
    // el log lo mostraria como si fuera la medicion. Un no-op disfrazado, justo en el vehiculo
    // de mas agarre. (Y de paso: el p90 del GT63 es 37.55 = 3.8 g SOSTENIDOS, asi que la
    // premisa "nadie sostiene 4 g" ya no vale para esta flota.)
    private static const float ALAT_TOPE_DIR = 100.0;
    // ⚠⚠⚠ RANGO FIJO, NO [0, max]. El histograma con rango [0, max] deja que UN pico de
    // basura fije la escala de todos los bins: en el Agera el motor vio un pico de 4072
    // (mi calculo offline, con otra curvatura, veia 125) y con 200 bins cada uno media
    // 20.4 m/s2 -- el p90 salio 40.93 en vez de 24.67, o sea 66% inflado, y el vehiculo se
    // fue por la tangente entrando a las curvas al doble de velocidad segura.
    //
    // Con rango FIJO [0, ALAT_TOPE] los bins miden 0.1 m/s2 pase lo que pase, y lo que se
    // salga del tope cae en el ultimo bin: como es basura y es < 10% de las muestras, no
    // puede mover el p90. ★ Y eso vuelve el resultado INDEPENDIENTE de como el motor calcule
    // los picos de curvatura, que es justo donde su cuenta y la mia no coincidian.
    //
    // Verificado contra las 12 rutas reales: error <= 0.5% contra el p90 exacto ordenado,
    // Agera incluido (24.70 contra 24.67).
    //
    // 40 m/s2 son 4 g laterales: ningun vehiculo los SOSTIENE. Arriba de eso es conversion.
    private static const float ALAT_TOPE = 40.0;
    private static const int   ALAT_PASOS = 18;   // 40 / 2^18 = 0.00015 m/s2
    // ★★★ EL PRESUPUESTO LATERAL POR BANDA DE VELOCIDAD, no un escalar para toda la toma.
    //
    // Medido en el Agera: en los waypoints donde el
    // humano grabo >150 km/h, el objetivo salia 160 contra 197 grabados, y el 100% de esos
    // recortes eran `razon CURVA`. El aLat que implica el objetivo es 4.8 m/s2 y el que el
    // humano uso ahi mismo es 7.1: el cap le prohibe la MITAD de lo que el humano hizo.
    //
    // La causa: `downforceCoefficient = 1` en estos vehiculos, o sea que el agarre real CRECE
    // con la velocidad. Un p90 unico sobre toda la toma sobrestima abajo y SUBESTIMA ARRIBA,
    // que es justo donde se pierde la fidelidad de velocidad.
    //
    // ⚠ NO ES FISICA NUEVA NI UNA CONSTANTE INVENTADA: es el MISMO estimador p90(v^2*k),
    // binneado por la velocidad GRABADA. El downforce entra solo, porque se mide del manejo
    // del propio humano a cada velocidad.
    //
    // El ancho de banda es una RESOLUCION, no un umbral fisico, y se interpola entre centros
    // para que no haya escalones. Una banda con pocas muestras cae al escalar global: un p90
    // de tres muestras no es un p90.
    private static const int   ALAT_NBANDAS   = 13;      // 0-260 km/h
    private static const float ALAT_BANDA_KMH = 20.0;
    private static const int   ALAT_MIN_MUEST = 20;
    private ref array<float>   m_ALatBanda;
    private ref array<float>   m_ALatDirBanda;
    private ref array<float>  m_Curv;     // curvatura signada (1/m). + = DERECHA:
                                          // el rumbo es Atan2(x,z) y crece en sentido HORARIO.
    private ref array<int>    m_LegOf;

    // ---- legs ----
    private ref array<int>    m_LegLo;
    private ref array<int>    m_LegHi;
    private ref array<bool>   m_LegRev;

    private int    m_Count;
    // ===== SANEO DE RETROCESOS DE GRABACION =====
    // Medido: TEST-Huracan tiene un salto de
    // 14.2 m HACIA ATRAS en wp295->296 (retrazo de varios wps) y TEST-GT2RSnokeep uno de
    // 6.6 m en wp206->207 (dH -172 grados). El paso normal de grabacion es ~2.4 m. Esos
    // glitches fabrican curvas fantasma que NI el rumbo grabado salva (el retrazo tuerce el
    // arco s/ds): al Huracan le fabricaron un freno accidental a 90 km/h justo antes del
    // tramo peligroso -- se salvaba de casualidad -- y al GT2RS le ensucian el cursor antes
    // de su zona de choque.
    //
    // El filtro vive ACA y no en el conversor a proposito: protege TODAS las rutas ya
    // desplegadas sin re-convertir nada, y el dato crudo en disco queda intacto (formato
    // fiel: el CSV no se pisa).
    //
    // ⚠ UN CUSP DECLARADO NO ES UN GLITCH. En un intercambio (legBreak) o al cambiar de
    // sentido (isRev distinto del anterior) el retroceso es legitimo y el filtro NO aplica.
    // Tampoco se descarta un punto con semantica (isStop): solo puntos lisos.
    private static const float RETRO_UMBRAL_M = 2.0;
    private int    m_RetroDescartados;
    private float  m_RetroPeorM;
    private int    m_LegCount;
    private bool   m_Ready;
    private string m_Err;

    // Guarda numerica, NO tuning: por debajo de esto un segmento no define rumbo.
    private const float MIN_SEG_M   = 0.01;
    // Por debajo de esto el vehiculo esta parado y su marcha no dice hacia donde va el tramo.
    private const float SENTIDO_MIN_KMH = 1.0;
    // Guarda numerica, NO tuning: 1/m = radio de 1m. En un CUSP el rumbo gira ~180 grados
    // en muy poco arco y la curvatura ahi no significa nada -- medido en rutas reales
    // (RUS_TEST_02 satura este tope justo en los dos cusps). El tope la mantiene acotada;
    // quien maneja el cusp es el ManeuverController, que no usa curvatura.
    private const float MAX_CURV    = 1.0;

    void BZRouteData()
    {
        m_Pos        = new array<vector>();
        m_RecKmh     = new array<float>();
        m_RecThr     = new array<float>();
        m_RecSteerDeg = new array<float>();
        m_ALatBanda    = new array<float>();
        m_ALatDirBanda = new array<float>();
        m_RecHeadRad = new array<float>();
        m_Handbrake  = new array<float>();
        m_IsRev      = new array<bool>();
        m_LegBreak   = new array<bool>();
        m_IsStop     = new array<bool>();
        m_Tight      = new array<bool>();
        m_S          = new array<float>();
        m_Head       = new array<float>();
        m_Curv       = new array<float>();
        m_LegOf      = new array<int>();
        m_LegLo      = new array<int>();
        m_LegHi      = new array<int>();
        m_LegRev     = new array<bool>();
        m_ArmSuave   = new array<float>();
        m_RevPre     = new array<int>();
        m_FwdPre     = new array<int>();
        m_TkFase     = new array<int>();
        m_Count      = 0;
        m_LegCount   = 0;
        m_Ready      = false;
        m_Err        = "";
    }

    // ======================= BUILDER =======================

    void BeginBuild()
    {
        m_Pos.Clear();
        m_RecKmh.Clear();
        m_RecThr.Clear();
        m_RecSteerDeg.Clear();
        m_RecHeadRad.Clear();
        m_Handbrake.Clear();
        m_IsRev.Clear();
        m_LegBreak.Clear();
        m_IsStop.Clear();
        m_Tight.Clear();
        m_S.Clear();
        m_Head.Clear();
        m_Curv.Clear();
        m_LegOf.Clear();
        m_LegLo.Clear();
        m_LegHi.Clear();
        m_LegRev.Clear();
        m_Count    = 0;
        m_LegCount = 0;
        m_Ready    = false;
        m_Err      = "";
        // el armado por pasos arranca de cero con la ruta. Un cursor que sobrevive a
        // su carga es la clase de bug que el mutante M5 del banco mata (el mapa que no reinicia).
        m_ArmFase  = 0;
        m_ArmSub   = 0;
        m_ArmBanda = 0;
        m_PctPaso  = 0;
        m_ArmI     = 0;   // los cursores de las pasadas por unidades
        m_ArmH     = 0;
        m_ArmFirstValid = -1;
        m_ArmSuave.Clear();
        m_ArmLo    = 0;
        m_ArmLegL  = 0;
        m_PctI     = 1;
        m_PctCuenta = 0;
        CbIniciar();
        m_CvH      = 0;
        m_CvHprev  = 0;
        m_CvMx     = 0;
        m_CvFuera  = 0;
        m_CvTotal  = 0;
        m_RevPre.Clear();
        m_FwdPre.Clear();
        m_TkFase.Clear();
        int gT;
        for (gT = 0; gT < ARM_GRUPOS; gT++) m_TkFase.Insert(0);
        m_TkPeorUnidad = 0;
        m_ArmPeorGrupo = 0;
    }

    // recHeadRad: rumbo grabado en RADIANES, o -999 si la toma no lo trae.
    void AddPoint(vector p, float recKmh, float recThr, float recSteerDeg, float recHeadRad, float handbrake, bool isRev, bool legBreak, bool isStop, bool tight)
    {
        if (EsRetroceso(p, isRev, legBreak, isStop)) return;
        m_Pos.Insert(p);
        m_RecKmh.Insert(recKmh);
        m_RecThr.Insert(recThr);
        m_RecSteerDeg.Insert(recSteerDeg);
        m_RecHeadRad.Insert(recHeadRad);
        m_Handbrake.Insert(handbrake);
        m_IsRev.Insert(isRev);
        m_LegBreak.Insert(legBreak);
        m_IsStop.Insert(isStop);
        m_Tight.Insert(tight);
        m_Count++;
    }

    // El punto nuevo retrocede contra el AVANCE del ultimo punto aceptado. La direccion de
    // avance sale del RUMBO GRABADO de ese punto (col 15, el yaw del vehiculo), NO de los
    // dos ultimos puntos aceptados.
    //
    // ⚠⚠ LA PRIMERA VERSION USABA DOS PUNTOS Y CASCADEABA. El espejo con paridad la freno
    // antes de shippear: sobre el 07-Agera descartaba 451 puntos (peor "retroceso" 766 m)
    // y sobre el Gemera 122. La causa: tras descartar un glitch, la baseline (dos ultimos
    // aceptados) quedaba ATRAVESANDO el glitch --direccion basura-- y todo lo que venia se
    // proyectaba negativo. El Gemera ni siquiera tenia glitch: un retroceso de 0.36 m en
    // wp391 bastaba para dar vuelta la baseline. El rumbo grabado es fisico punto a punto y
    // no depende de lo que se descarto: no puede cascadear.
    //
    // Sin rumbo grabado no se filtra: no se inventa una direccion de avance.
    private bool EsRetroceso(vector p, bool isRev, bool legBreak, bool isStop)
    {
        if (m_Count < 1) return false;
        if (legBreak) return false;
        if (isStop) return false;
        if (isRev != m_IsRev.Get(m_Count - 1)) return false;
        float h = m_RecHeadRad.Get(m_Count - 1);
        if (h < -900.0) return false;
        if (m_IsRev.Get(m_Count - 1)) h = h + Math.PI;   // en reversa la marcha va al reves del yaw
        float ux = Math.Sin(h);
        float uz = Math.Cos(h);
        vector prev = m_Pos.Get(m_Count - 1);
        float avance = (p[0] - prev[0]) * ux + (p[2] - prev[2]) * uz;
        if (avance > -RETRO_UMBRAL_M) return false;
        m_RetroDescartados++;
        if (-avance > m_RetroPeorM) m_RetroPeorM = -avance;
        if (m_RetroDescartados <= 12) Print("[Route] saneo: descartado wp " + m_Count.ToString() + " (retrocede " + (-avance).ToString() + " m contra el rumbo grabado)");
        return true;
    }

    // ★★★ EL ARMADO VA POR PASOS, NO DE UNA.
    //
    // `EndBuild()` hacia Arc + Headings + Curvature + MedirALat + SegmentLegs en UNA llamada, y
    // esa llamada caia entera adentro de un cuadro del server. Medido en el RPT del 18/09
    // (20:37:36.179 → 36.874): 688 ms para PATRULLA entre el parseo y esto, de un cuadro total de
    // 1.839,97 ms. El usuario lo ve como un congelamiento cada vez que se lanza una ruta.
    //
    // Ahora cada llamada a ArmarPaso() hace UNA UNIDAD DE TRABAJO y vuelve, para que la cola
    // (BZCargaCola) decida si hay presupuesto para otra en este cuadro. La unidad esta elegida
    // para que NINGUNA supere UNA PASADA sobre los n puntos:
    //   Arc · Headings · Curvature · el conteo de aLat · CADA pasada de biseccion · CuentaEnBanda
    //   por banda · los reportes · SegmentLegs
    // La biseccion era lo unico que no entraba: son 18 pasadas por percentil y hay 2 globales mas
    // 2 por banda con muestras (≈122 pasadas en total). Por eso PercentilALat se partio en
    // PctIniciar/PctPaso: MISMA cuenta, misma secuencia de operaciones, una pasada por llamada.
    //
    // ⚠ El resultado tiene que ser IDENTICO al de antes: es scheduling, no un cambio de calculo.
    // Verificado en el banco (carga_cola_banco.py §B) contra los 1193 waypoints reales de PATRULLA:
    // el percentil reanudable da el MISMO float que el monolitico, en exactamente 18 pasadas.
    //
    // true = el armado TERMINO (bien o mal). IsReady() dice cual de las dos.
    private int   m_ArmFase;
    private int   m_ArmSub;
    private int   m_ArmN;
    private int   m_ArmTotal;
    private int   m_ArmFuera;
    private float m_ArmMx;
    private int   m_ArmBanda;
    private int   m_ArmCBanda;
    // biseccion en curso (el estado que antes vivia en los locales de PercentilALat)
    private int   m_PctPaso;
    private float m_PctLo;
    private float m_PctHi;
    private float m_PctObj;
    private int   m_PctBanda;
    // NINGUNA UNIDAD DEL ARMADO
    // PASA DE ~1 ms. La cola corta por reloj ENTRE unidades: una tajada se pasa solo si UNA unidad dura mas que el presupuesto,
    // y aca cada fase era una pasada ENTERA sobre los n puntos (17.307 en ROUTE2_FULL): rumbos + suavizado (11 trig por wp),
    // curvatura, el conteo de aLat, cada pasada de la biseccion (~190 por ruta de bus), CuentaEnBanda, la convencion vieja y
    // SegmentLegs. Ahora cada pasada tiene su cursor y se corta cuando el contador fino dice ARM_UNIDAD_TICKS: la tajada peor
    // queda en presupuesto + ~1 ms (el falsador de BusDrive: <= 6 ms en todas las lineas `reparto:`). Los CALCULOS son los
    // mismos, en el mismo orden: armado_banco_968.py compara S/Head/Curv/aLat/bandas/legs de esta version contra la monolitica
    // sobre los 17.307 wps reales con cortes de 1, 3, 64, 1000 y aleatorios: identicos.
    // ⚠ Lo unico que cambio de FORMA es SentidoDelTramo: de un bucle por leg (17.000 wps en un leg = una unidad de 3 ms) a
    // prefijos acumulados (m_RevPre/m_FwdPre): O(1) por leg y el MISMO entero. Los prefijos se arman en una pasada mas (fase 10).
    private static const int ARM_UNIDAD_TICKS = 10000;   // 1 ms del contador fino (~10.000 ticks/ms: el "reloj fino" de la linea reparto:)
    private static const int ARM_CADA = 32;              // el reloj se consulta cada 32 iteraciones: el instrumento no pesa sobre lo que mide
    private static const int ARM_GRUPOS = 6;             // arco · rumbos · curv · alat · vieja · legs (la linea reparto: los imprime)
    private int   m_ArmI;            // cursor de la pasada en curso
    private float m_ArmH;            // rumbos: el del ultimo segmento valido (un segmento degenerado hereda; era el local `h`)
    private int   m_ArmFirstValid;
    private ref array<float> m_ArmSuave;
    private int   m_ArmLo;           // SegmentLegs: el inicio del leg abierto (era el local `lo`)
    private int   m_ArmLegL;         // ReindexLegOf: el leg en curso
    private int   m_PctI;            // PctPaso: cursor y cuenta de la pasada en curso
    private int   m_PctCuenta;
    private int   m_CbI;             // CuentaEnBanda: cursor y cuenta
    private int   m_CbC;
    private float m_CvH;             // convencion vieja: el estado que vivia en los locales de ReportarConvencionVieja
    private float m_CvHprev;
    private float m_CvMx;
    private int   m_CvFuera;
    private int   m_CvTotal;
    private ref array<int> m_RevPre; // prefijos de SentidoDelTramo: reversa/adelante con kmh >= SENTIDO_MIN_KMH hasta i (exclusivo)
    private ref array<int> m_FwdPre;
    private ref array<int> m_TkFase; // ticks acumulados por grupo de fases
    private int   m_TkPeorUnidad;
    private int   m_ArmPeorGrupo;

    private bool ArmSeAcabo(int t0, int hechos)
    {
        if (hechos % ARM_CADA != 0) return false;
        return TickCount(t0) >= ARM_UNIDAD_TICKS;
    }

    private int GrupoDeFase(int fase)
    {
        if (fase <= 0) return 0;
        if (fase <= 2) return 1;
        if (fase == 3) return 2;
        if (fase <= 7) return 3;
        if (fase == 8) return 4;
        return 5;
    }

    private string NombreGrupo(int g)
    {
        if (g == 0) return "arco";
        if (g == 1) return "rumbos";
        if (g == 2) return "curv";
        if (g == 3) return "alat";
        if (g == 4) return "vieja";
        return "legs";
    }

    private string MsDeTicks(int tk)
    {
        float ms = tk / 10000.0;
        return ms.ToString();
    }

    // Lo que la linea `reparto:` agrega despues de "armar ruta X ms": el tiempo por grupo de fases y la peor UNIDAD
    // (la tajada peor de la cola es esto mas el presupuesto). Ticks a ms con 10.000 ticks/ms; la propia linea imprime
    // el "reloj fino" medido para re-escalar si hace falta.
    string ArmadoReparto()
    {
        if (!m_TkFase || m_TkFase.Count() < ARM_GRUPOS) return "";
        string r = " (arco " + MsDeTicks(m_TkFase.Get(0)) + " · rumbos " + MsDeTicks(m_TkFase.Get(1)) + " · curv " + MsDeTicks(m_TkFase.Get(2));
        r = r + " · alat " + MsDeTicks(m_TkFase.Get(3)) + " · vieja " + MsDeTicks(m_TkFase.Get(4)) + " · legs " + MsDeTicks(m_TkFase.Get(5)) + " ms";
        r = r + " · peor unidad " + MsDeTicks(m_TkPeorUnidad) + " ms en " + NombreGrupo(m_ArmPeorGrupo) + ")";
        return r;
    }

    private void CbIniciar()
    {
        m_CbI = 1;
        m_CbC = 0;
    }

    bool ArmarPaso()
    {
        if (m_Ready) return true;
        if (m_ArmFase >= 90) return true;   // termino en falla
        // una UNIDAD (<= ARM_UNIDAD_TICKS) de la fase en curso, cronometrada por grupo para la linea reparto:
        int t0 = TickCount(0);
        int grupoU = GrupoDeFase(m_ArmFase);
        bool finU = ArmarUnidad(t0);
        int dtU = TickCount(t0);
        m_TkFase.Set(grupoU, m_TkFase.Get(grupoU) + dtU);
        if (dtU > m_TkPeorUnidad)
        {
            m_TkPeorUnidad = dtU;
            m_ArmPeorGrupo = grupoU;
        }
        return finU;
    }

    // Las fases, en el MISMO orden que antes (arco · rumbos · suavizado · curvatura · conteo aLat · p90 · p95 · bandas · reportes ·
    // saneo · prefijos · legs · reindex). Cada `while` es la pasada de antes con su cursor; `ArmSeAcabo` la corta y la
    // proxima llamada sigue donde quedo. true = el armado TERMINO (bien o mal).
    private bool ArmarUnidad(int t0)
    {
        // --- fase 0: el arco acumulado (era ComputeArc)
        if (m_ArmFase == 0)
        {
            if (m_ArmSub == 0)
            {
                if (m_Count < 1)
                {
                    m_Err = "ruta vacia";
                    m_Ready = false;
                    m_ArmFase = 90;
                    return true;
                }
                m_S.Insert(0.0);
                m_ArmI = 1;
                m_ArmSub = 1;
            }
            int hechos0 = 0;
            while (m_ArmI < m_Count)
            {
                float d0 = vector.Distance(m_Pos.Get(m_ArmI - 1), m_Pos.Get(m_ArmI));
                if (!BZMath.Finite(d0)) d0 = 0.0;
                m_S.Insert(m_S.Get(m_ArmI - 1) + d0);
                m_ArmI = m_ArmI + 1;
                hechos0 = hechos0 + 1;
                if (ArmSeAcabo(t0, hechos0)) return false;
            }
            m_ArmFase = 1;
            m_ArmSub = 0;
            m_ArmI = 0;
            return false;
        }
        // --- fase 1: el rumbo por wp (era ComputeHeadings + BackfillHeadings)
        if (m_ArmFase == 1)
        {
            if (m_ArmSub == 0)
            {
                m_ArmH = 0;
                m_ArmFirstValid = -1;
                m_ArmI = 0;
                m_ArmSub = 1;
            }
            int hechos1 = 0;
            while (m_ArmI < m_Count)
            {
                int i1 = m_ArmI;
                if (i1 < m_Count - 1)
                {
                    float dx1 = m_Pos.Get(i1 + 1)[0] - m_Pos.Get(i1)[0];
                    float dz1 = m_Pos.Get(i1 + 1)[2] - m_Pos.Get(i1)[2];
                    float len1 = Math.Sqrt(dx1 * dx1 + dz1 * dz1);
                    if (len1 >= MIN_SEG_M)
                    {
                        m_ArmH = BZMath.HeadingXZ(dx1, dz1);
                        if (m_ArmFirstValid < 0) m_ArmFirstValid = i1;
                    }
                }
                // el rumbo GRABADO manda sobre la tangente derivada
                float hg1 = m_RecHeadRad.Get(i1);
                if (hg1 > -900.0)
                {
                    if (m_IsRev.Get(i1)) hg1 = BZMath.WrapPi(hg1 + Math.PI);
                    m_ArmH = hg1;
                    if (m_ArmFirstValid < 0) m_ArmFirstValid = i1;
                }
                m_Head.Insert(m_ArmH);
                m_ArmI = m_ArmI + 1;
                hechos1 = hechos1 + 1;
                if (ArmSeAcabo(t0, hechos1)) return false;
            }
            BackfillHeadings(m_ArmFirstValid);
            m_ArmFase = 2;
            m_ArmSub = 0;
            m_ArmI = 0;
            return false;
        }
        // --- fase 2: el suavizado de rumbos (era SuavizarRumbos): sub 1 arma `suave`, sub 2 lo copia
        if (m_ArmFase == 2)
        {
            if (m_ArmSub == 0)
            {
                m_ArmI = 0;
                m_ArmSuave.Clear();
                m_ArmSub = 1;
                if (m_Count < 3) m_ArmSub = 3;
            }
            int hechos2 = 0;
            if (m_ArmSub == 1)
            {
                while (m_ArmI < m_Count)
                {
                    int i2 = m_ArmI;
                    float sx2 = Math.Sin(m_Head.Get(i2));
                    float sy2 = Math.Cos(m_Head.Get(i2));
                    int j2;
                    for (j2 = i2 - 1; j2 >= 0 && j2 >= i2 - SUAV_RUMBO_LADO; j2--)
                    {
                        if (m_LegBreak.Get(j2 + 1)) break;
                        if (m_IsRev.Get(j2) != m_IsRev.Get(i2)) break;
                        sx2 = sx2 + Math.Sin(m_Head.Get(j2));
                        sy2 = sy2 + Math.Cos(m_Head.Get(j2));
                    }
                    for (j2 = i2 + 1; j2 < m_Count && j2 <= i2 + SUAV_RUMBO_LADO; j2++)
                    {
                        if (m_LegBreak.Get(j2)) break;
                        if (m_IsRev.Get(j2) != m_IsRev.Get(i2)) break;
                        sx2 = sx2 + Math.Sin(m_Head.Get(j2));
                        sy2 = sy2 + Math.Cos(m_Head.Get(j2));
                    }
                    m_ArmSuave.Insert(Math.Atan2(sx2, sy2));
                    m_ArmI = m_ArmI + 1;
                    hechos2 = hechos2 + 1;
                    if (ArmSeAcabo(t0, hechos2)) return false;
                }
                m_ArmI = 0;
                m_ArmSub = 2;
            }
            if (m_ArmSub == 2)
            {
                while (m_ArmI < m_Count)
                {
                    m_Head.Set(m_ArmI, m_ArmSuave.Get(m_ArmI));
                    m_ArmI = m_ArmI + 1;
                    hechos2 = hechos2 + 1;
                    if (ArmSeAcabo(t0, hechos2)) return false;
                }
                m_ArmSub = 3;
            }
            m_ArmFase = 3;
            m_ArmSub = 0;
            m_ArmI = 0;
            return false;
        }
        // --- fase 3: la curvatura por diferencia centrada (era ComputeCurvature; ver la nota de VENT_CURV_M)
        if (m_ArmFase == 3)
        {
            int hechos3 = 0;
            while (m_ArmI < m_Count)
            {
                int i3 = m_ArmI;
                int a3 = i3;
                while (a3 > 0 && m_S.Get(i3) - m_S.Get(a3 - 1) <= VENT_CURV_M) a3 = a3 - 1;
                int b3 = i3;
                while (b3 < m_Count - 1 && m_S.Get(b3 + 1) - m_S.Get(i3) <= VENT_CURV_M) b3 = b3 + 1;
                if (a3 == i3 && i3 > 0) a3 = i3 - 1;
                if (b3 == i3 && i3 < m_Count - 1) b3 = i3 + 1;
                float k3 = 0;
                float ds3 = m_S.Get(b3) - m_S.Get(a3);
                if (ds3 >= MIN_SEG_M)
                {
                    float dh3 = BZMath.WrapPi(m_Head.Get(b3) - m_Head.Get(a3));
                    k3 = dh3 / ds3;
                }
                k3 = Math.Clamp(k3, -MAX_CURV, MAX_CURV);
                m_Curv.Insert(k3);
                m_ArmI = m_ArmI + 1;
                hechos3 = hechos3 + 1;
                if (ArmSeAcabo(t0, hechos3)) return false;
            }
            m_ArmFase = 4;
            m_ArmSub = 0;
            m_ArmI = 0;
            return false;
        }
        // --- fase 4: el conteo inicial de MedirALatDeLaToma
        if (m_ArmFase == 4)
        {
            if (m_ArmSub == 0)
            {
                m_ALatToma = 0;
                m_ArmN = m_Pos.Count();
                m_ArmTotal = 0;
                m_ArmFuera = 0;
                m_ArmMx = 0;
                m_ArmBanda = 0;
                m_ArmI = 1;
                m_ArmSub = 1;
                if (m_ArmN < 20)
                {
                    m_ArmFase = 9;
                    m_ArmSub = 0;
                    return false;
                }
            }
            int hechos4 = 0;
            while (m_ArmI < m_ArmN - 1)
            {
                float a4 = ALatEn(m_ArmI);
                m_ArmI = m_ArmI + 1;
                hechos4 = hechos4 + 1;
                if (a4 >= 0)
                {
                    m_ArmTotal++;
                    if (a4 > m_ArmMx) m_ArmMx = a4;
                    if (a4 >= ALAT_TOPE) m_ArmFuera++;
                }
                if (ArmSeAcabo(t0, hechos4)) return false;
            }
            m_ArmSub = 0;
            if (m_ArmTotal < 10)
            {
                m_ArmFase = 9;
                return false;
            }
            PctIniciar(m_ArmTotal, 0.90, ALAT_TOPE, -1);
            m_ArmFase = 5;
            return false;
        }
        // --- fases 5 y 6: los dos percentiles GLOBALES, por pasadas de biseccion (cada pasada, por unidades)
        if (m_ArmFase == 5)
        {
            if (PctPaso(m_ArmN, t0))
            {
                m_ALatToma = m_PctHi;
                PctIniciar(m_ArmTotal, 0.95, ALAT_TOPE_DIR, -1);
                m_ArmFase = 6;
            }
            return false;
        }
        if (m_ArmFase == 6)
        {
            if (PctPaso(m_ArmN, t0))
            {
                m_ALatDir = m_PctHi;
                m_ALatBanda.Clear();        // lo que hacia MedirBandas al entrar
                m_ALatDirBanda.Clear();
                m_ArmBanda = 0;
                m_ArmSub = 0;
                CbIniciar();
                m_ArmFase = 7;
            }
            return false;
        }
        // --- fase 7: las bandas. Mismo ORDEN de Insert que MedirBandas (p90 y p95 de una banda antes de pasar a la siguiente)
        if (m_ArmFase == 7)
        {
            if (m_ArmBanda >= ALAT_NBANDAS)
            {
                m_ArmFase = 8;
                m_ArmSub = 0;
                return false;
            }
            if (m_ArmSub == 0)
            {
                if (!CuentaEnBandaPaso(m_ArmN, m_ArmBanda, t0)) return false;
                if (m_ArmCBanda < ALAT_MIN_MUEST)
                {
                    m_ALatBanda.Insert(-1.0);
                    m_ALatDirBanda.Insert(-1.0);
                    m_ArmBanda = m_ArmBanda + 1;
                    return false;
                }
                PctIniciar(m_ArmCBanda, 0.90, ALAT_TOPE, m_ArmBanda);
                m_ArmSub = 1;
                return false;
            }
            if (m_ArmSub == 1)
            {
                if (PctPaso(m_ArmN, t0))
                {
                    m_ALatBanda.Insert(m_PctHi);
                    PctIniciar(m_ArmCBanda, 0.95, ALAT_TOPE_DIR, m_ArmBanda);
                    m_ArmSub = 2;
                }
                return false;
            }
            if (PctPaso(m_ArmN, t0))
            {
                m_ALatDirBanda.Insert(m_PctHi);
                m_ArmBanda = m_ArmBanda + 1;
                m_ArmSub = 0;
            }
            return false;
        }
        // --- fase 8: el reporte del agarre lateral + la convencion vieja (era ReportarConvencionVieja, una pasada mas)
        if (m_ArmFase == 8)
        {
            if (m_ArmSub == 0)
            {
                ReportarALat(m_ArmTotal, m_ArmFuera, m_ArmMx);
                m_CvH = 0;
                m_CvHprev = 0;
                m_CvMx = 0;
                m_CvFuera = 0;
                m_CvTotal = 0;
                m_ArmI = 0;
                m_ArmSub = 1;
            }
            int hechos8 = 0;
            while (m_ArmI < m_Count - 1)
            {
                int i8 = m_ArmI;
                m_ArmI = m_ArmI + 1;
                hechos8 = hechos8 + 1;
                float dx8 = m_Pos.Get(i8 + 1)[0] - m_Pos.Get(i8)[0];
                float dz8 = m_Pos.Get(i8 + 1)[2] - m_Pos.Get(i8)[2];
                float len8 = Math.Sqrt(dx8 * dx8 + dz8 * dz8);
                if (len8 >= MIN_SEG_M) m_CvH = BZMath.HeadingXZ(dx8, dz8);
                if (i8 > 0)
                {
                    float ds8 = m_S.Get(i8) - m_S.Get(i8 - 1);
                    float k8 = 0;
                    if (ds8 >= MIN_SEG_M) k8 = BZMath.WrapPi(m_CvH - m_CvHprev) / ds8;
                    if (k8 < 0) k8 = -k8;
                    if (k8 > MAX_CURV) k8 = MAX_CURV;
                    float v8 = m_RecKmh.Get(i8) / 3.6;
                    if (v8 >= 5.0 && k8 >= 0.0001)
                    {
                        float a8 = v8 * v8 * k8;
                        m_CvTotal++;
                        if (a8 > m_CvMx) m_CvMx = a8;
                        if (a8 >= ALAT_TOPE) m_CvFuera++;
                    }
                }
                m_CvHprev = m_CvH;
                if (ArmSeAcabo(t0, hechos8)) return false;
            }
            ReportarConvencionViejaLinea();
            m_ArmFase = 9;
            m_ArmSub = 0;
            return false;
        }
        // --- fase 9: el saneo de retrocesos (SIEMPRE se imprime, incluso con 0: "no aparecio la linea" tiene que ser
        //     distinguible de "no corrio el filtro" -- la leccion del ff+ que costo una tanda)
        if (m_ArmFase == 9)
        {
            string lnr = "[Route] saneo de retrocesos: " + m_RetroDescartados.ToString();
            lnr = lnr + " waypoints descartados";
            if (m_RetroDescartados > 0) lnr = lnr + " · peor retroceso " + m_RetroPeorM.ToString() + " m";
            Print(lnr);
            m_RevPre.Clear();
            m_FwdPre.Clear();
            m_RevPre.Insert(0);
            m_FwdPre.Insert(0);
            m_ArmI = 0;
            m_ArmFase = 10;
            m_ArmSub = 0;
            return false;
        }
        // --- fase 10: los prefijos de SentidoDelTramo (reversa / adelante con kmh >= SENTIDO_MIN_KMH acumulados)
        if (m_ArmFase == 10)
        {
            int hechos10 = 0;
            while (m_ArmI < m_Count)
            {
                int i10 = m_ArmI;
                int r10 = m_RevPre.Get(i10);
                int f10 = m_FwdPre.Get(i10);
                if (m_RecKmh.Get(i10) >= SENTIDO_MIN_KMH)
                {
                    if (m_IsRev.Get(i10)) r10 = r10 + 1;
                    else f10 = f10 + 1;
                }
                m_RevPre.Insert(r10);
                m_FwdPre.Insert(f10);
                m_ArmI = m_ArmI + 1;
                hechos10 = hechos10 + 1;
                if (ArmSeAcabo(t0, hechos10)) return false;
            }
            m_ArmLo = 0;
            m_ArmI = 0;
            m_ArmFase = 11;
            m_ArmSub = 0;
            return false;
        }
        // --- fase 11: los legs (era SegmentLegs): legBreak DECLARADO o cambio de sentido INFERIDO -> arranca leg nuevo
        if (m_ArmFase == 11)
        {
            int hechos11 = 0;
            while (m_ArmI < m_Count)
            {
                int i11 = m_ArmI;
                bool nueva11 = false;
                if (i11 > 0 && m_LegBreak.Get(i11)) nueva11 = true;
                if (i11 > 0 && m_IsRev.Get(i11) != m_IsRev.Get(i11 - 1)) nueva11 = true;
                if (nueva11)
                {
                    CloseLeg(m_ArmLo, i11 - 1);
                    m_ArmLo = i11;
                }
                m_LegOf.Insert(0);
                m_ArmI = m_ArmI + 1;
                hechos11 = hechos11 + 1;
                if (ArmSeAcabo(t0, hechos11)) return false;
            }
            CloseLeg(m_ArmLo, m_Count - 1);
            MergeDegenerateLegs();
            m_ArmLegL = 0;
            m_ArmI = 0;
            if (m_LegCount > 0) m_ArmI = m_LegLo.Get(0);
            m_ArmFase = 12;
            m_ArmSub = 0;
            return false;
        }
        // --- fase 12: LegOf por leg (era ReindexLegOf), y listo
        int hechos12 = 0;
        while (m_ArmLegL < m_LegCount)
        {
            if (m_ArmI > m_LegHi.Get(m_ArmLegL))
            {
                m_ArmLegL = m_ArmLegL + 1;
                if (m_ArmLegL < m_LegCount) m_ArmI = m_LegLo.Get(m_ArmLegL);
                continue;
            }
            m_LegOf.Set(m_ArmI, m_ArmLegL);
            m_ArmI = m_ArmI + 1;
            hechos12 = hechos12 + 1;
            if (ArmSeAcabo(t0, hechos12)) return false;
        }
        m_Ready = true;
        m_ArmFase = 13;
        return true;
    }

    // ⚠⚠ SOLO PARA SELF-TESTS Y BANCOS: arma la ruta ENTERA en esta llamada.
    //
    // Existe porque los self-tests de @BZ_AutoDrive12 arman rutas SINTETICAS de 10-50 puntos y no
    // tienen cuadros entre los que repartirse: un test es una funcion que corre y termina.
    //
    // ⚠ NO es la vuelta de `EndBuild()`. `EndBuild()` era OTRA IMPLEMENTACION del armado; esto es
    // un bucle sobre `ArmarPaso()`, o sea EXACTAMENTE el mismo codigo, llamado hasta el final. No hay
    // dos caminos que puedan divergir, que es lo que se vino a evitar.
    // ⚠ PRODUCCION NO LA USA: una ruta de verdad se encola en BZCargaCola y se carga por tajadas.
    // Si aparece una llamada a esto fuera de un self-test, es un bug: son 1.840 ms en un cuadro.
    bool ArmarTodo()
    {
        int guarda = 0;
        while (!ArmarPaso())
        {
            guarda = guarda + 1;
            if (guarda > 100000) break;   // ninguna ruta sana necesita tantos pasos
        }
        return m_Ready;
    }

    // La biseccion, partida en pasadas. PctIniciar deja el estado; cada PctPaso hace UNA pasada
    // sobre los n puntos y devuelve true cuando ya hizo las ALAT_PASOS. El valor queda en m_PctHi.
    private void PctIniciar(int total, float frac, float tope, int banda)
    {
        m_PctPaso = 0;
        m_PctLo = 0;
        m_PctHi = tope;
        m_PctObj = total * frac;
        m_PctBanda = banda;
        m_PctI = 1;        // la pasada arranca en 1 y cuenta desde cero
        m_PctCuenta = 0;
    }

    // una pasada de la biseccion, POR UNIDADES. `mid` no cambia dentro de la pasada (lo/hi se mueven recien
    // al terminarla), asi que recalcularlo por llamada da el mismo valor. Devuelve true solo cuando las ALAT_PASOS terminaron.
    private bool PctPaso(int n, int t0)
    {
        if (m_PctPaso >= ALAT_PASOS) return true;
        float mid = (m_PctLo + m_PctHi) * 0.5;
        int hechosP = 0;
        while (m_PctI < n - 1)
        {
            int iP = m_PctI;
            m_PctI = m_PctI + 1;
            hechosP = hechosP + 1;
            float aP = ALatEn(iP);
            if (aP >= 0)
            {
                bool cuentaP = true;
                if (m_PctBanda >= 0 && BandaDe(m_RecKmh.Get(iP)) != m_PctBanda) cuentaP = false;
                if (cuentaP && aP <= mid) m_PctCuenta++;
            }
            if (ArmSeAcabo(t0, hechosP)) return false;
        }
        if (m_PctCuenta >= m_PctObj) m_PctHi = mid;
        else m_PctLo = mid;
        m_PctPaso = m_PctPaso + 1;
        m_PctI = 1;
        m_PctCuenta = 0;
        return m_PctPaso >= ALAT_PASOS;
    }

    float ALatToma() { return m_ALatToma; }
    float ALatDir()  { return m_ALatDir; }

    // ⚠⚠⚠ EL PRESUPUESTO LATERAL SALE DE LA TOMA, NO DE UNA CONSTANTE.
    //
    // `CurvatureLimitMs(s, aLat)` recibia `aLat = decel` -- la desaceleracion de FRENADO del
    // piso, capada ademas en `MU_HARD * G = 9.81` para TODA la flota. Pero el agarre lateral
    // no es solo del piso: es del VEHICULO. Un hipercar sostiene dos o tres veces lo de un
    // camion, y el modelo les daba lo mismo.
    //
    // Medido sobre los 12 CSV (`v^2 * k` sostenida, sin derrapar):
    //     Agera p90 24.67 · Suzuki 25.02   contra un presupuesto de 9.81
    //     ZiS5 4.88 · Offroad 3.13 · Willys 6.17   (por DEBAJO: nunca los limito)
    // De ahi el `objetivo 87` con `grabado 149` del Agera.
    //
    // ★ DE LA TOMA Y NO DEL ENVELOPE. El envelope solo sabe lo que la calibracion ejercito,
    // y eso nos mordio tres veces el mismo dia: la reversa (`medido 0`), la 1ra (descartada
    // porque el humano casi no la uso) y la tabla de ratios (regimen sucio). La toma, por
    // construccion, contiene la maniobra que estamos tratando de reproducir: si el humano
    // doblo a 237, el numero esta ahi y no puede faltar. Ademas vale retroactivo sobre las
    // rutas que ya existen, sin una sola corrida de calibracion.
    //
    // ✖ p90 Y NO EL MAXIMO: la conversion a alta velocidad deja picos de curvatura basura
    // (R < 3 m) que dan aLat de 120-125 en un tick suelto. El p90 los ignora y aun asi da
    // 2.5x el cap. No hace falta limpiar la ruta para que esto ande.
    //
    // ✖ Y por HISTOGRAMA, no ordenando: Enforce no ordena arrays, y un O(n^2) sobre 1249
    // waypoints al cargar no es gratis.
    // MedirALatDeLaToma vivia aca. Su cuerpo se repartio en las fases 3..7 de
    // ArmarPaso() -- mismas cuentas, mismo orden, una pasada por llamada. Se BORRA en vez de
    // dejarla al lado "por las dudas": dos caminos para el mismo numero es como se cuelan las
    // divergencias silenciosas.
    // Cuantas muestras utiles caen en una banda (-1 = todas). La usa la fase 6 de ArmarPaso.
    // la cuenta de una banda, POR UNIDADES. true = termino; el resultado queda en m_ArmCBanda.
    private bool CuentaEnBandaPaso(int n, int banda, int t0)
    {
        int hechosC = 0;
        while (m_CbI < n - 1)
        {
            int iC = m_CbI;
            m_CbI = m_CbI + 1;
            hechosC = hechosC + 1;
            if (ALatEn(iC) >= 0)
            {
                if (banda < 0 || BandaDe(m_RecKmh.Get(iC)) == banda) m_CbC++;
            }
            if (ArmSeAcabo(t0, hechosC)) return false;
        }
        m_ArmCBanda = m_CbC;
        CbIniciar();
        return true;
    }

    private int BandaDe(float kmh)
    {
        int b = (int)(kmh / ALAT_BANDA_KMH);
        if (b < 0) b = 0;
        if (b >= ALAT_NBANDAS) b = ALAT_NBANDAS - 1;
        return b;
    }

    // MedirBandas vivia aca -> fase 6 de ArmarPaso (una banda por vez).

    // Interpola entre centros de banda. Donde no hay dato cae al escalar global, que es lo
    // que habia: nunca queda por debajo de lo que ya estaba medido para toda la toma.
    private float EnBandas(array<float> tabla, float kmh, float global)
    {
        if (!tabla) return global;
        if (tabla.Count() < ALAT_NBANDAS) return global;
        float x = (kmh / ALAT_BANDA_KMH) - 0.5;   // en unidades de banda, respecto del centro
        int b0 = (int)Math.Floor(x);
        float t = x - b0;
        int b1 = b0 + 1;
        if (b0 < 0) { b0 = 0; b1 = 0; t = 0; }
        if (b1 >= ALAT_NBANDAS) { b0 = ALAT_NBANDAS - 1; b1 = b0; t = 0; }
        float a0 = tabla.Get(b0);
        float a1 = tabla.Get(b1);
        if (a0 < 0) a0 = global;
        if (a1 < 0) a1 = global;
        float r = a0 + (a1 - a0) * t;
        if (r < global) r = global;              // la banda solo puede SUMAR presupuesto
        return r;
    }

    float ALatTomaEn(float kmh) { return EnBandas(m_ALatBanda, kmh, m_ALatToma); }
    float ALatDirEn(float kmh)  { return EnBandas(m_ALatDirBanda, kmh, m_ALatDir); }

    // PercentilALat vivia aca -> PctIniciar/PctPaso (una pasada de biseccion
    // por llamada). El banco prueba que el resultado es el MISMO float.

    // El lateral sostenido en el waypoint i, o -1 si la muestra no sirve. Una sola
    // definicion para las dos pasadas: si divergieran, la biseccion contaria otra cosa que
    // la que midio el maximo.
    private float ALatEn(int i)
    {
        float v = m_RecKmh.Get(i) / 3.6;
        if (v < 5.0) return -1.0;              // parado o gateando: no dice nada
        float k = m_Curv.Get(i);
        if (k < 0) k = -k;
        if (k < 0.0001) return -1.0;
        return v * v * k;
    }

    // ⚠ El print va aparte y con los floats ACOTADOS. El crash nativo aparecio en la linea
    // del Print, y aunque la causa era el array, un `ToString()` sobre un valor patologico
    // (Inf/NaN de una curvatura basura) es otra forma conocida de voltear el motor. `mx` es
    // informativo: no hace falta imprimirlo con 4072 de precision si acotarlo lo vuelve
    // inofensivo. Y la concatenacion va de a un operando por sentencia.
    // ★ La MISMA cuenta con la tangente derivada, para que la corrida que certifica muestre
    // el antes/despues desde el MOTOR y no desde un analisis offline. Es solo reporte: no
    // toca m_Curv ni nada del control.
    // la pasada (dx/dz/rumbo/curvatura por wp) vive en ArmarUnidad fase 8, por unidades; aca queda la linea.
    private void ReportarConvencionViejaLinea()
    {
        if (m_CvTotal < 10) return;
        float mx = m_CvMx;
        if (mx > 9999.0) mx = 9999.0;
        string ln = "[Route] con la convencion VIEJA (tangente derivada de posiciones) esa misma toma daba: pico ";
        ln = ln + mx.ToString();
        ln = ln + " m/s2 sobre " + m_CvTotal.ToString();
        ln = ln + " waypoints · fuera de rango " + ((m_CvFuera * 100.0) / m_CvTotal).ToString() + "%";
        Print(ln);
    }

    private void ReportarALat(int total, int fuera, float mx)
    {
        float mxSano = mx;
        if (mxSano > 9999.0) mxSano = 9999.0;
        if (mxSano < 0) mxSano = 0;
        float pct = fuera * 100.0 / total;
        string ln = "[Route] agarre lateral de la TOMA: p90 ";
        ln = ln + m_ALatToma.ToString();
        ln = ln + " (velocidad) · p95 ";
        ln = ln + m_ALatDir.ToString();
        ln = ln + " (direccion) m/s2 sobre ";
        ln = ln + total.ToString();
        ln = ln + " waypoints · pico ";
        ln = ln + mxSano.ToString();
        ln = ln + " · fuera de rango ";
        ln = ln + pct.ToString();
        ln = ln + " %";
        Print(ln);
        if (pct > 5.0)
        {
            Print("[Route] *** ATENCION: mas del 5% de los waypoints dan mas de 4 g laterales. Eso ya no es cola de conversion: el p90 puede estar contaminado y el presupuesto lateral con el. Revisar la curvatura de esta ruta. ***");
        }
    }

    // ======================= PRECOMPUTO =======================

    // ComputeArc vive en ArmarUnidad fase 0, por unidades.

    // Tangente por wp. m_Head[i] = rumbo del segmento i -> i+1. El ultimo hereda.
    // Un segmento degenerado (puntos repetidos) hereda el anterior: nunca inventa rumbo.
    // ComputeHeadings vive en ArmarUnidad fase 1, por unidades (BackfillHeadings sigue abajo).

    // ⚠⚠⚠ EL RUMBO GRABADO ES EL YAW DEL VEHICULO, Y EL YAW TIEMBLA. La toma limpia
    // del bus tiene el VOLANTE limpio (0.184 rev/s
    // deliberadas) pero el rumbo da pasos 0.2<->2.9 grados alternando sobre una
    // tendencia de 1.26/wp: micro-oscilacion del chasis, no trayectoria. El logger
    // escribe por TIEMPO (~0.15 s) => el paso crece con la velocidad (2.2 m a 54
    // km/h) y la ventana del kFF (3 m, derivada sobre base 0.4 m = 7.5 muestras)
    // queda con ~1.4 muestras: el jitter paso DIRECTO al volante (FF~ -0.0016 <->
    // -0.0216 en una curva R~100 constante => el corrector peleaba a un FF movil,
    // saturaba tarde y pegaba el volantazo: str reversiones 0.41/s vs 0.18 humano).
    // Mismo remedio que PathSmooth ya aplica a las POSICIONES (ventana 5): promedio
    // sincos +-SUAV_RUMBO_LADO, SIN cruzar legBreak ni cambio de sentido (ahi el
    // +pi de la reversa deja un salto ~180 LEGITIMO que no se puede mezclar).
    // Medido offline (toma real): desvio del kFF en la curva 1 x3.2 menos (0.0051
    // -> 0.0016) con la media clavada en el kappa real; apices de la curva lenta
    // conservados (ventana en MUESTRAS, no en metros: no se diluye con el paso).
    private static const int SUAV_RUMBO_LADO = 2;

    // SuavizarRumbos vive en ArmarUnidad fase 2, por unidades (m_ArmSuave).

    // Los wps ANTERIORES al primer segmento valido quedaron con rumbo placeholder: heredan el
    // primero real. Se rastrea por INDICE, no por "rumbo != 0" -- rumbo 0 es un norte legitimo.
    private void BackfillHeadings(int firstValid)
    {
        int j;
        if (firstValid <= 0) return;
        for (j = 0; j < firstValid; j++)
        {
            m_Head.Set(j, m_Head.Get(firstValid));
        }
    }

    // Curvatura por DIFERENCIA CENTRADA sobre +-VENT_CURV_M de arco -- NO entre wps
    // consecutivos. La version por-wp (dh/ds del segmento) ALTERNABA DE SIGNO 1.39/s en
    // la ruta real del bus (mas nerviosa que el Boris que veniamos a calmar) y CAPO LA
    // VELOCIDAD del Agera en plena recta ( RPT 09-09: TargetAt limita por
    // aLat/kappa con este campo; un wp con kappa espuria a 60-100 m adelante tiro el
    // objetivo de 206 a 167 con grabado 237 -> gas 0 -> rpm 805 a 185 km/h). La ventana
    // centrada no mete fase y mata la cuantizacion sin recortar curvas reales (1.5 m
    // por lado ~ un paso y medio de wp). Signo: + = derecha (Atan2(x,z) horario).
    private static const float VENT_CURV_M = 1.5;

    // ComputeCurvature vive en ArmarUnidad fase 3, por unidades.

    // legBreak DECLARADO o cambio de sentido INFERIDO -> arranca leg nuevo.
    // SegmentLegs vive en ArmarUnidad fase 11, por unidades (CloseLeg y MergeDegenerateLegs siguen abajo).

    // Un leg necesita AL MENOS DOS waypoints para definir un sentido de marcha.
    // Uno solo no es un tramo: es el MISMO borde declarado dos veces.
    //
    // Caso REAL (RUS_TEST_02, medido): el humano marca el legBreak en el wp 211 y la
    // marcha atras arranca en el 212 -- un metro despues, que es lo natural al grabar.
    // Cortando en los dos nace un leg fantasma [211..211] FORWARD entre el leg forward
    // y el de reversa. Consecuencia: desde el leg 0 el "siguiente" leg es forward, el
    // arbitro no ve el cusp, y Boris frena a 0 y se queda ahi para siempre.
    //
    // El fantasma se absorbe en el leg ANTERIOR: el waypoint del borde pertenece al
    // tramo que LLEGA a el (es la pose que hay que clavar).
    private void MergeDegenerateLegs()
    {
        array<int>  lo2 = new array<int>();
        array<int>  hi2 = new array<int>();
        array<bool> rv2 = new array<bool>();
        int L;
        int lo;
        int hi;
        int pendingLo = -1;
        for (L = 0; L < m_LegCount; L++)
        {
            lo = m_LegLo.Get(L);
            hi = m_LegHi.Get(L);
            if (pendingLo >= 0)
            {
                lo = pendingLo;
                pendingLo = -1;
            }
            if (hi > lo)
            {
                lo2.Insert(lo);
                hi2.Insert(hi);
                rv2.Insert(SentidoDelTramo(lo, hi));
                continue;
            }
            if (lo2.Count() > 0) hi2.Set(lo2.Count() - 1, hi);   // absorber en el anterior
            else                 pendingLo = lo;                 // no hay anterior: va con el siguiente
        }
        if (lo2.Count() < 1) return;
        m_LegLo.Clear();
        m_LegHi.Clear();
        m_LegRev.Clear();
        for (L = 0; L < lo2.Count(); L++)
        {
            m_LegLo.Insert(lo2.Get(L));
            m_LegHi.Insert(hi2.Get(L));
            m_LegRev.Insert(rv2.Get(L));
        }
        m_LegCount = m_LegLo.Count();
    }

    // ⚠ EL SENTIDO DEL TRAMO NO SE LEE DEL PRIMER WAYPOINT.
    // Una toma que arranca en reversa deja el wp0 grabado PARADO, y un vehiculo parado tiene
    // la marcha que traia, no la que va a usar. Medido en la toma REVERSA: wp0 quedo en
    // gear 2 (forward) con todo el resto del tramo en reversa -> el leg 0 se detecto forward
    // y el vehiculo manejo al reves, 19 m afuera de la traza.
    //
    // El sentido sale de los waypoints donde el vehiculo SE MOVIA, por mayoria. Un waypoint
    // quieto no aporta evidencia de sentido y no vota.
    // el bucle por el leg (17.000 wps en un leg = una unidad de 3 ms) se cambio por prefijos acumulados
    // (m_RevPre/m_FwdPre, fase 10): rev/fwd de [lo, hi] con kmh >= SENTIDO_MIN_KMH, saltando i < 0 e i >= m_Count como antes.
    private bool SentidoDelTramo(int lo, int hi)
    {
        int a = lo;
        if (a < 0) a = 0;
        int b = hi;
        if (b > m_Count - 1) b = m_Count - 1;
        int rev = 0;
        int fwd = 0;
        if (b >= a && m_RevPre.Count() > b + 1)
        {
            rev = m_RevPre.Get(b + 1) - m_RevPre.Get(a);
            fwd = m_FwdPre.Get(b + 1) - m_FwdPre.Get(a);
        }
        if (rev + fwd < 1) return m_IsRev.Get(ClampIdx(lo));
        return rev > fwd;
    }

    private void CloseLeg(int lo, int hi)
    {
        if (hi < lo) hi = lo;
        m_LegLo.Insert(lo);
        m_LegHi.Insert(hi);
        m_LegRev.Insert(SentidoDelTramo(lo, hi));
        m_LegCount++;
    }

    // ReindexLegOf vive en ArmarUnidad fase 12, por unidades.

    // ======================= ACCESSORS (bounds-safe) =======================

    private int ClampIdx(int i)
    {
        if (i < 0) return 0;
        if (i > m_Count - 1) return m_Count - 1;
        return i;
    }

    private int ClampLeg(int L)
    {
        if (L < 0) return 0;
        if (L > m_LegCount - 1) return m_LegCount - 1;
        return L;
    }

    bool   IsReady()   { return m_Ready; }
    string GetError()  { return m_Err; }
    int    Count()     { return m_Count; }
    int    LegCount()  { return m_LegCount; }

    vector Pos(int i)          { return m_Pos.Get(ClampIdx(i)); }
    float  RecKmh(int i)       { return m_RecKmh.Get(ClampIdx(i)); }
    float  RecThr(int i)       { return m_RecThr.Get(ClampIdx(i)); }
    float  RecSteerDeg(int i)  { return m_RecSteerDeg.Get(ClampIdx(i)); }
    float  RecHeadRad(int i)   { return m_RecHeadRad.Get(ClampIdx(i)); }
    float  Handbrake(int i)    { return m_Handbrake.Get(ClampIdx(i)); }
    bool   IsRev(int i)        { return m_IsRev.Get(ClampIdx(i)); }
    bool   LegBreak(int i)     { return m_LegBreak.Get(ClampIdx(i)); }
    bool   IsStop(int i)       { return m_IsStop.Get(ClampIdx(i)); }
    bool   Tight(int i)        { return m_Tight.Get(ClampIdx(i)); }
    float  S(int i)            { return m_S.Get(ClampIdx(i)); }
    float  Head(int i)         { return m_Head.Get(ClampIdx(i)); }
    float  Curv(int i)         { return m_Curv.Get(ClampIdx(i)); }
    int    LegOf(int i)        { return m_LegOf.Get(ClampIdx(i)); }

    int    LegLo(int L)        { return m_LegLo.Get(ClampLeg(L)); }
    int    LegHi(int L)        { return m_LegHi.Get(ClampLeg(L)); }
    bool   LegRev(int L)       { return m_LegRev.Get(ClampLeg(L)); }

    float ArcBetween(int a, int b)
    {
        return m_S.Get(ClampIdx(b)) - m_S.Get(ClampIdx(a));
    }

    float LegLengthM(int L)
    {
        return ArcBetween(LegLo(L), LegHi(L));
    }

    // Parada SEMANTICA (SEAM.md 7): dispara ARRIVED y espera Resume().
    // El legBreak MECANICO no entra aca: lo encadena solo el ManeuverController.
    bool IsSemanticStop(int i)
    {
        int c = ClampIdx(i);
        if (c >= m_Count - 1) return true;
        return m_IsStop.Get(c);
    }

    string DebugSummary()
    {
        string s = "[RouteData] wps=";
        s = s + m_Count.ToString() + " legs=" + m_LegCount.ToString();
        s = s + " largo=" + ArcBetween(0, m_Count - 1).ToString() + "m ready=" + m_Ready.ToString();
        return s;
    }
}
