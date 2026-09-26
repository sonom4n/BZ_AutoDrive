// ============================================================================
//  BZCargaCola.c - BZ_AutoDrive 1.2
//
//  ★★★ LA COLA DE CARGA: UNA sola cola global, FIFO, con PRESUPUESTO FIJO POR CUADRO.
//
//  EL PROBLEMA (medido, no supuesto). Lanzar una ruta cargaba el wp.csv, la CINTA, el manual y
//  el envelope adentro de UN cuadro del server. En el RPT del 18/09, PATRULLA:
//      20:37:36.179  arranca Configurar12
//      20:37:36.874  ruta lista       →  688 ms (parseo de 1.193 filas + armado)
//      20:37:38.017  cinta lista      → 1.105 ms (19.075 filas)
//      20:37:38.017  [PERF-TICK] Configurar12: 1839.97 ms EN UN CUADRO
//  Casi dos segundos de server congelado. Y no lo sufre solo el vehiculo que arranca: lo sufre
//  el que ya venia manejando, los bots y cualquiera que este jugando. Con un convoy de 3
//  vehiculos son tres congelamientos, uno arriba del otro.
//
// LA REGLA (pedido del usuario, ): "que se tome su tiempo para cargar, hacerle como un
//  cuello de botella, para que no se sobrecargue con flags y disparados manuales; siempre va a
//  cargar esa cantidad de datos acumulada en un timing parejo".
//
//  ⚠⚠ POR ESO ES UNA COLA GLOBAL Y NO UN PRESUPUESTO POR RUTA. La diferencia no es cosmetica:
//      presupuesto POR RUTA   3 rutas lanzadas juntas = 3 x 5 ms por cuadro. El problema vuelve.
//      COLA GLOBAL            UN presupuesto repartido entre lo que haya en cola. Lanzar 5 rutas
//                             de golpe NO empeora el server: solo tarda mas en tenerlas todas.
//  El costo por cuadro es CONSTANTE y predecible, pase lo que pase. Verificado en el banco
//  (carga_cola_banco.py ·D): con 1, 2, 3 y 5 rutas simultaneas el peor cuadro es el MISMO.
//
//  POR ACA PASA TODO LO QUE CARGA RUTA, SIN EXCEPCIONES: banderas, panel, respawn automatico al
//  terminar una vuelta, y cada vehiculo de un convoy. No es una convencion: `LoadWpCsv()` y
//  `CargarDeArchivo()` se BORRARON, asi que no queda otro camino que este.
//
//  EL ORDEN ES FIFO (orden de llegada). El operador que lanzo cinco rutas ve la quinta como
//  "en cola 4" en el panel, en vez de pensar que se perdio el disparo.
//
//  ⚠ GARANTIA DE AVANCE. Si un trabajo devolviera 0 unidades sin terminar (una fase sin salida),
//  el bucle del presupuesto no saldria nunca y el cuadro quedaria colgado PARA SIEMPRE: un bug de
//  scheduling se volveria un server colgado. Lo encontro el mutante M10 del banco -- colgo la
//  corrida de mutantes de verdad. Dos candados, los dos independientes de la perilla:
//    1) a lo sumo MAX_PASOS_CUADRO pasos de trabajo por cuadro
//    2) un trabajo que no avanza se ABANDONA con un log, no se reintenta eternamente
// ============================================================================

// Un trabajo de carga: una ruta y (si corresponde) su cinta, leidas en tajadas.
class BZCargaTrabajo
{
    static const int F_WP_ABRIR    = 0;
    static const int F_WP_LEER     = 1;
    static const int F_WP_ARMAR    = 2;
    static const int F_CINTA_ABRIR = 3;
    static const int F_CINTA_LEER  = 4;
    static const int F_CINTA_ARMAR = 5;
    static const int F_LISTO       = 6;
    static const int F_FALLO       = 7;

    // ⚠ Los lotes salen de los costos MEDIDOS: una fila de wp cuesta 43-94 us (; el "0,30 ms"
    // que decia aca era una estimacion, no una medicion) y una de cinta ~25 us (3230 ms / 124.258 filas,
    // RPT 21-04-24). 8 y 64 dan tajadas de menos de 2 ms, chicas frente a un tick normal del controlador
    // (5-7 ms). No son perillas por vehiculo ni por ruta: son el tamanio de la unidad de trabajo, igual
    // para todos.
    private static const int LOTE_WP    = 8;
    private static const int LOTE_CINTA = 64;
    // Tope de unidades por tajada. Candado contra un reloj que no avance: el presupuesto se mide
    // con GetTickTime(), que tiene ~1 ms de resolucion.
    private static const int MAX_UNIDADES = 512;

    private string m_WpPath;
    private string m_CintaPath;
    private string m_Etiqueta;
    private int    m_Fase;
    private FileHandle m_Fh;
    // ⚠ FileHandle es `typedef int[]`: se puede comparar contra 0 (OpenFile devuelve 0 en fallo)
    // pero no hay precedente de asignarle 0, asi que "hay un archivo abierto" es una bandera aparte.
    private bool   m_FhAbierto;
    private ref BZRouteData   m_Ruta;
    private ref BZCintaRunner m_Cinta;
    private bool   m_CintaPedida;
    private bool   m_CintaFallo;
    private int    m_NWp;
    private int    m_NWpDesc;
    private int    m_NCinta;
    private int    m_Tajadas;
    private float  m_UltimoMs;
    private float  m_MsWpLeer;
    private float  m_MsWpArmar;
    private float  m_MsCintaLeer;
    private float  m_MsCintaArmar;
    private float  m_PeorTajadaMs;
    // FGets y parseo, cronometrados APARTE con el contador fino del motor (TickCount: ~10.000
    // ticks por ms; se calibra contra GetTickTime en la linea `reparto:`, que imprime ticks/ms). Se acumulan
    // en ticks enteros y salen como porcentaje: asi el numero no depende de la frecuencia del contador.
    private int    m_TkWpFGets;
    private int    m_TkWpParse;
    private int    m_TkCintaFGets;
    private int    m_TkCintaParse;
    // Las lineas de UNA tajada: primero se leen todas (FGets) y despues se parsean. Dos lecturas del reloj
    // por tajada en vez de cuatro por fila: el instrumento no pesa sobre lo que mide.
    private ref array<string> m_Buf;
    private float  m_TInicio;
    private string m_Falla;
    // filas DECLARADAS por el header (0 = no se sabe). Dan el denominador del %.
    private int    m_WpDecl;
    private int    m_CintaDecl;

    // wpDecl / cintaDecl: las filas que declara el _hdr.json (WpRows/CintaRows). 0 = no se sabe.
    void Preparar(string wpPath, string cintaPath, string etiqueta, int wpDecl = 0, int cintaDecl = 0)
    {
        m_WpPath = wpPath;
        m_CintaPath = cintaPath;
        m_Etiqueta = etiqueta;
        m_WpDecl = wpDecl;
        m_CintaDecl = cintaDecl;
        m_CintaPedida = (cintaPath != "");
        m_Fase = F_WP_ABRIR;
        m_Buf = new array<string>();
        m_TkWpFGets = 0;
        m_TkWpParse = 0;
        m_TkCintaFGets = 0;
        m_TkCintaParse = 0;
        m_TInicio = GetGame().GetTickTime();
    }

    bool Termino()      { return m_Fase == F_LISTO || m_Fase == F_FALLO; }
    bool RutaOk()       { return m_Fase == F_LISTO && m_Ruta && m_Ruta.IsReady(); }
    bool CintaPedida()  { return m_CintaPedida; }
    bool CintaFallo()   { return m_CintaFallo; }
    string CintaPath()  { return m_CintaPath; }
    string Etiqueta()   { return m_Etiqueta; }
    float UltimoMs()    { return m_UltimoMs; }
    int   WpCargados()  { return m_NWp; }
    BZRouteData   Ruta()  { return m_Ruta; }
    BZCintaRunner Cinta() { return m_Cinta; }

    // EL PORCENTAJE DE LA CARGA, o -1 si el header no declaro las filas.
    // Por FILAS y no por tiempo: el armado de la ruta (un 24 % del tiempo medido) no tiene filas, asi
    // que el numero se queda quieto un momento al terminar el wp y despues sube con la cinta. Es un
    // % honesto de lo leido, no una barra inventada.
    // ⚠ Nunca muestra 100 hasta que TERMINA: despues de la ultima fila todavia falta armar la cinta.
    int Porcentaje()
    {
        if (m_WpDecl <= 0) return -1;
        if (m_CintaPedida && m_CintaDecl <= 0) return -1;
        int totalP = m_WpDecl;
        if (m_CintaPedida) totalP = totalP + m_CintaDecl;
        int hechasP = m_NWp + m_NWpDesc + m_NCinta;
        int pctP = (hechasP * 100) / totalP;
        if (pctP > 99) pctP = 99;
        if (pctP < 0) pctP = 0;
        return pctP;
    }

    // Filas leidas hasta ahora (el panel lo muestra cuando no hay %).
    int FilasLeidas() { return m_NWp + m_NWpDesc + m_NCinta; }

    // Lo que muestra el panel mientras carga. Sin % porque no hay forma barata de saber el total
    // de filas sin leer el archivo dos veces (Enforce no expone el tamanio de un archivo).
    string Estado()
    {
        if (m_Fase == F_WP_ABRIR || m_Fase == F_WP_LEER) return "ruta " + m_NWp.ToString();
        if (m_Fase == F_WP_ARMAR) return "armando ruta";
        if (m_Fase == F_CINTA_ABRIR || m_Fase == F_CINTA_LEER) return "cinta " + m_NCinta.ToString();
        if (m_Fase == F_CINTA_ARMAR) return "armando cinta";
        return "lista";
    }

    void Abandonar(string por)
    {
        if (m_FhAbierto) { CloseFile(m_Fh); m_FhAbierto = false; }
        m_Falla = por;
        m_Fase = F_FALLO;
        BZBusLog.Err("[CARGA] " + m_Etiqueta + ": ABANDONADA -- " + por);
    }

    // (bug 3 de ): cancelar un trabajo en vuelo lo sacaba de la COLA pero no cerraba el
    // ARCHIVO: el FileHandle quedaba abierto hasta que el objeto muriera (no hay destructor). Ahora se cierra
    // aca y queda en el log en que fase estaba. Un trabajo que ya termino no tiene nada que cerrar ni que decir.
    void Cancelar(string por)
    {
        if (Termino()) return;
        string enQue = Estado();
        if (m_FhAbierto) { CloseFile(m_Fh); m_FhAbierto = false; }
        m_Falla = "cancelada: " + por;
        m_Fase = F_FALLO;
        BZBusLog.Info("[CARGA] " + m_Etiqueta + ": CANCELADA en '" + enQue + "' (" + por + ")");
    }

    // Trabaja hasta gastar msDisp. Devuelve cuantas UNIDADES hizo: 0 sin terminar = no avanza, y
    // la cola lo saca (ver la garantia de avance arriba).
    int Paso(float msDisp)
    {
        float t0 = GetGame().GetTickTime();
        m_Tajadas = m_Tajadas + 1;
        int unidades = 0;
        float gastado = 0;
        while (gastado < msDisp && !Termino() && unidades < MAX_UNIDADES)
        {
            Unidad();
            unidades = unidades + 1;
            gastado = (GetGame().GetTickTime() - t0) * 1000.0;
        }
        if (gastado < 0) gastado = 0;
        m_UltimoMs = gastado;
        if (gastado > m_PeorTajadaMs) m_PeorTajadaMs = gastado;
        return unidades;
    }

    // ⚠⚠ SOLO PARA SELF-TESTS Y HARNESS DE BANCO: carga todo en esta llamada, sin presupuesto.
    // Mismo motivo y misma advertencia que BZRouteData.ArmarTodo(): es el MISMO codigo (Paso/Unidad),
    // lo unico que se saltea es el reparto por cuadros, que en un test no significa nada.
    // Produccion encola y deja que la cola drene: por eso esto NO toca s_Cola.
    bool CargarYaMismo()
    {
        int guarda = 0;
        while (!Termino())
        {
            if (Paso(1000000.0) <= 0) break;   // no avanza: cortar, igual que hace la cola
            guarda = guarda + 1;
            if (guarda > 100000) break;
        }
        return RutaOk();
    }

    // UNA unidad de trabajo. Ninguna supera una pasada sobre los datos.
    private void Unidad()
    {
        if (m_Fase == F_WP_ABRIR)
        {
            m_Ruta = new BZRouteData();
            m_Fh = OpenFile(m_WpPath, FileMode.READ);
            if (m_Fh == 0)
            {
                Print("[RouteLoader] no se pudo abrir " + m_WpPath);
                Abandonar("no se pudo abrir " + m_WpPath);
                return;
            }
            m_FhAbierto = true;
            m_Ruta.BeginBuild();
            m_Fase = F_WP_LEER;
            return;
        }
        if (m_Fase == F_WP_LEER)
        {
            float tw = GetGame().GetTickTime();
            // primero las LOTE_WP lineas al buffer (FGets), despues el parseo. Ver m_Tk*.
            int tkW0 = TickCount(0);
            m_Buf.Clear();
            bool eofW = false;
            string line;
            while (m_Buf.Count() < LOTE_WP)
            {
                if (FGets(m_Fh, line) < 0) { eofW = true; break; }
                m_Buf.Insert(line);
            }
            int tkW1 = TickCount(0);
            int iw;
            for (iw = 0; iw < m_Buf.Count(); iw++)
            {
                string lw = m_Buf.Get(iw);
                if (lw == "") continue;
                if (BZRouteLoader.AgregarLinea(lw, m_Ruta)) m_NWp = m_NWp + 1;
                else m_NWpDesc = m_NWpDesc + 1;
            }
            m_TkWpFGets = m_TkWpFGets + (tkW1 - tkW0);
            m_TkWpParse = m_TkWpParse + (TickCount(0) - tkW1);
            m_MsWpLeer = m_MsWpLeer + (GetGame().GetTickTime() - tw) * 1000.0;
            if (eofW)
            {
                CloseFile(m_Fh);
                m_FhAbierto = false;
                if (m_NWp < 1)
                {
                    Print("[RouteLoader] " + m_WpPath + ": 0 waypoints validos (" + m_NWpDesc.ToString() + " lineas descartadas)");
                    Abandonar("0 waypoints validos en " + m_WpPath);
                    return;
                }
                m_Fase = F_WP_ARMAR;
            }
            return;
        }
        if (m_Fase == F_WP_ARMAR)
        {
            float ta = GetGame().GetTickTime();
            bool listaR = m_Ruta.ArmarPaso();
            m_MsWpArmar = m_MsWpArmar + (GetGame().GetTickTime() - ta) * 1000.0;
            if (!listaR) return;
            if (!m_Ruta.IsReady())
            {
                Abandonar("la ruta no se pudo armar (" + m_Ruta.GetError() + ")");
                return;
            }
            // misma linea de siempre, con los mismos campos: el analista la grepea
            Print("[RouteLoader] " + m_WpPath + ": " + m_NWp.ToString() + " wps, " + m_Ruta.LegCount().ToString() + " legs, " + m_Ruta.ArcBetween(0, m_NWp - 1).ToString() + " m (descartadas " + m_NWpDesc.ToString() + ")");
            m_Fase = F_CINTA_ABRIR;
            return;
        }
        if (m_Fase == F_CINTA_ABRIR)
        {
            if (!m_CintaPedida) { Terminar(); return; }
            m_Fh = OpenFile(m_CintaPath, FileMode.READ);
            if (m_Fh == 0)
            {
                // sin cinta la ruta corre igual, en modo normal: NO es una falla de la carga
                m_CintaFallo = true;
                Terminar();
                return;
            }
            m_FhAbierto = true;
            m_Cinta = new BZCintaRunner();
            m_Cinta.ComenzarCarga();
            m_Fase = F_CINTA_LEER;
            return;
        }
        if (m_Fase == F_CINTA_LEER)
        {
            float tc = GetGame().GetTickTime();
            // primero las LOTE_CINTA lineas al buffer (FGets), despues el parseo. Ver m_Tk*.
            int tkC0 = TickCount(0);
            m_Buf.Clear();
            bool eofC = false;
            string linea;
            while (m_Buf.Count() < LOTE_CINTA)
            {
                if (FGets(m_Fh, linea) < 0) { eofC = true; break; }
                m_Buf.Insert(linea);
            }
            int tkC1 = TickCount(0);
            int ic;
            for (ic = 0; ic < m_Buf.Count(); ic++)
            {
                if (m_Cinta.AgregarFila(m_Buf.Get(ic))) m_NCinta = m_NCinta + 1;
            }
            m_TkCintaFGets = m_TkCintaFGets + (tkC1 - tkC0);
            m_TkCintaParse = m_TkCintaParse + (TickCount(0) - tkC1);
            m_MsCintaLeer = m_MsCintaLeer + (GetGame().GetTickTime() - tc) * 1000.0;
            if (eofC)
            {
                CloseFile(m_Fh);
                m_FhAbierto = false;
                m_Fase = F_CINTA_ARMAR;
            }
            return;
        }
        if (m_Fase == F_CINTA_ARMAR)
        {
            float tk = GetGame().GetTickTime();
            bool listaC = m_Cinta.ArmarPaso();
            m_MsCintaArmar = m_MsCintaArmar + (GetGame().GetTickTime() - tk) * 1000.0;
            if (!listaC) return;
            if (!m_Cinta.Valida())
            {
                m_CintaFallo = true;
                m_Cinta = null;
            }
            Terminar();
            return;
        }
    }

    private void Terminar()
    {
        m_Fase = F_LISTO;
        float totalMs = (GetGame().GetTickTime() - m_TInicio) * 1000.0;
        string ln = "[CARGA] " + m_Etiqueta + " LISTA: " + m_NWp.ToString() + " wps";
        if (m_NCinta > 0) ln = ln + " + " + m_NCinta.ToString() + " frames de cinta";
        ln = ln + " en " + m_Tajadas.ToString() + " tajadas / " + totalMs.ToString() + " ms de reloj";
        // la linea dice si fue carga PROPIA (leida del disco) o un ENGANCHE a una ya cargada.
        // Hoy todas son propias: es el control del paso 1 para el falsador del paso 2 (registro en la cola).
        ln = ln + " · carga PROPIA";
        BZBusLog.Info(ln);
        string ln2 = "[CARGA] " + m_Etiqueta + " reparto: leer ruta " + m_MsWpLeer.ToString() + " ms" + Reparto(m_TkWpFGets, m_TkWpParse);
        ln2 = ln2 + " · armar ruta " + m_MsWpArmar.ToString() + " ms";
        if (m_Ruta) ln2 = ln2 + m_Ruta.ArmadoReparto();   // por grupo de fases + la peor UNIDAD (la tajada peor es esto + el presupuesto)
        ln2 = ln2 + " · leer cinta " + m_MsCintaLeer.ToString() + " ms" + Reparto(m_TkCintaFGets, m_TkCintaParse);
        ln2 = ln2 + " · armar cinta " + m_MsCintaArmar.ToString() + " ms";
        ln2 = ln2 + " · PEOR TAJADA " + m_PeorTajadaMs.ToString() + " ms";
        // calibracion del contador fino contra GetTickTime, sobre la fase mas larga (leer cinta)
        if (m_MsCintaLeer > 0)
        {
            float tkPorMs = (m_TkCintaFGets + m_TkCintaParse) / m_MsCintaLeer;
            ln2 = ln2 + " · reloj fino " + ((int)tkPorMs).ToString() + " ticks/ms";
        }
        BZBusLog.Info(ln2);
    }

    // " (FGets 38 % · parseo 62 %)" a partir de los ticks acumulados; "" si no se midio nada.
    // En float: ticks x 100 desborda el int con mas de 21 s de lectura.
    private string Reparto(int tkA, int tkB)
    {
        int tot = tkA + tkB;
        if (tot <= 0) return "";
        float pA = (tkA * 100.0) / tot;
        int pctA = (int)pA;
        int pctB = 100 - pctA;
        return " (FGets " + pctA.ToString() + " · parseo " + pctB.ToString() + ")";   // sin '%': el logger del motor se lo come
    }
}

// La cola. Todo static: hay UNA sola para todo el server, por definicion.
class BZCargaCola
{
    private static ref array<ref BZCargaTrabajo> s_Cola;
    private static float s_PresupuestoMs = 5.0;
    // Tope de pasos por cuadro. NO escala con el presupuesto: es el candado de avance, no una
    // perilla de rendimiento.
    private static const int MAX_PASOS_CUADRO = 8;

    static void FijarPresupuesto(float ms)
    {
        if (ms > 0) s_PresupuestoMs = ms;
    }

    static float Presupuesto() { return s_PresupuestoMs; }

    static void Encolar(BZCargaTrabajo t)
    {
        if (!t) return;
        if (!s_Cola) s_Cola = new array<ref BZCargaTrabajo>();
        s_Cola.Insert(t);
        if (s_Cola.Count() > 1)
            BZBusLog.Info("[CARGA] " + t.Etiqueta() + ": EN COLA, posicion " + (s_Cola.Count() - 1).ToString() + " (presupuesto " + s_PresupuestoMs.ToString() + " ms por cuadro, compartido)");
    }

    // 0 = al frente (cargando ahora) · n>0 = esperando · -1 = no esta en la cola
    static int Posicion(BZCargaTrabajo t)
    {
        if (!t || !s_Cola) return -1;
        int i;
        for (i = 0; i < s_Cola.Count(); i++)
        {
            if (s_Cola.Get(i) == t) return i;
        }
        return -1;
    }

    // Saca un trabajo de la cola (respawn, despawn): el que no tiene duenio no gasta presupuesto.
    // RemoveOrdered y no Remove (ver Drenar), y el trabajo cierra su archivo (BZCargaTrabajo.Cancelar).
    static void Cancelar(BZCargaTrabajo t)
    {
        if (!t) return;
        if (s_Cola)
        {
            int i;
            for (i = 0; i < s_Cola.Count(); i++)
            {
                if (s_Cola.Get(i) == t) { s_Cola.RemoveOrdered(i); break; }
            }
        }
        t.Cancelar("su runner la solto");
    }

    static int Pendientes()
    {
        if (!s_Cola) return 0;
        return s_Cola.Count();
    }

    // Un cuadro de trabajo. La llama el primary una vez por cuadro. Devuelve los ms gastados.
    static float Drenar()
    {
        if (!s_Cola || s_Cola.Count() == 0) return 0;
        float t0 = GetGame().GetTickTime();
        float gastado = 0;
        int pasos = 0;
        // (bug 1 de ): `array.Remove(i)` del motor NO conserva el orden ("the empty position is
        // replaced by last element", enscript.c:459): con [A, B, C], al terminar A pasaba C al frente. La cola decia
        // FIFO y no lo era: el 19/09 PUTOSHKA (lanzada 3a, "posicion 2") cargo antes que PAVLOV ("posicion 1"), y un
        // convoy salia al reves de como se lanzo. RemoveOrdered en los cuatro lugares. El banco de 828 no lo vio
        // porque modelaba la cola con pop(0), que ES RemoveOrdered: el espejo tiene que copiar la primitiva del motor.
        while (s_Cola.Count() > 0 && gastado < s_PresupuestoMs && pasos < MAX_PASOS_CUADRO)
        {
            pasos = pasos + 1;
            BZCargaTrabajo t = s_Cola.Get(0);
            if (!t) { s_Cola.RemoveOrdered(0); continue; }
            int unidades = t.Paso(s_PresupuestoMs - gastado);
            if (t.Termino()) s_Cola.RemoveOrdered(0);
            else if (unidades <= 0)
            {
                // no avanzo: se saca de la cola para que no la bloquee (garantia de avance)
                t.Abandonar("un paso no hizo ninguna unidad de trabajo");
                s_Cola.RemoveOrdered(0);
            }
            gastado = (GetGame().GetTickTime() - t0) * 1000.0;
        }
        if (gastado < 0) gastado = 0;
        return gastado;
    }
}
