// ============================================================================
//  BZReproductorUI - "Reproductor" de rutas del framework (admin), diseno C.
//  Carga control_panel_v2.layout: lista de rutas (ROUTES) + runner activo +
//  hotkeys. v1: reusa la plomeria RPC existente (slots 0-4 via RequestRespawnSlot,
//  stop/pause via BZBusClientManager) y la telemetria s_Panel* para el runner.
//  v2 (TODO): enumeracion dinamica de tomas (_wp.csv) + carga por path arbitrario
//  (RPC nuevo REQUEST_LOAD_ROUTE) + multi-instancia (Runner1+) + rebind de hotkeys.
//  Modelado sobre BZControlPanelUI (mismo lifecycle UIScriptedMenu).
// ============================================================================

class BZReproductorUI extends UIScriptedMenu {

    static BZReproductorUI s_Instance;
    // cuando se cerro por su tecla (GetGame().GetTime(), ms): la mision no lo reabre en ese mismo toque
    static int s_CerradoMs = -100000;
    // cuando se abrio (GetGame().GetTime(), ms; lo fija OnShow): en los 250 ms siguientes ninguna tecla lo cierra. Es la
    // guarda simetrica de s_CerradoMs: el panel puede abrirse desde la tecla cruda (OnKeyPress de la mision) y la accion
    // puede disparar en el mismo cuadro; sin esto ese mismo toque lo cerraria.
    static int s_AbiertoMs = -100000;
    // por que se cierra (lo fija quien lo cierra; vacio = el motor u otro menu): solo para la linea de diagnostico
    static string s_CierrePor = "";

    private ButtonWidget m_CloseBtn;
    private ButtonWidget m_LoadSpawnBtn;
    private ButtonWidget m_LogNativeChk;   // check "Log boris_native" (opt-in por corrida)
    private ButtonWidget m_LogAiChk;       // check "Log ai_run"
    private static bool  s_LogNative;      // persiste el estado del check entre aperturas del panel
    private static bool  s_LogAi;
    private TextWidget   m_PreviewText;   // preview de la toma seleccionada (vehiculo/N wps/dist/vel max)
    private int          m_RIVer;         // ultima version de info recibida que ya pinté
    private int          m_RechazoVer;    // ultimo rechazo de spawn vacio ya atendido (BZBusClientManager.s_SpawnRechazoVer)
    private string       m_InfoPedidaDe;  // la ruta cuya info ya se pidio: la rueda del mouse no la vuelve a pedir
    private Widget       m_ScrubTrack;    // scrubber: base clickeable de la barra
    private Widget       m_ScrubFill;     // relleno (ancho = progreso); su borde derecho marca la posicion
    private int          m_ScrubWp;       // wp elegido con el scrubber (0..N-1)
    private int          m_ScrubNwp;      // N wps de la toma seleccionada (de la info)
    private bool         m_Dragging;      // true mientras arrastro la manija
    private TextWidget   m_SliderLabel;   // "wp X / N  ·  Y m  ·  Z%"
    private Widget       m_GoStartFace;   // cara metalica clickeable (spawn wp0). ImageWidget: click via OnMouseButtonDown
    private Widget       m_SpawnHereFace; // cara metalica clickeable (spawn wp del scrubber)
    private Widget       m_GoEndFace;     // cara metalica clickeable (spawn endpoint -1)
    private Widget       m_GoStartFrame;  // marco de acento (glow en hover; SetColor SI anda en ImageWidget)
    private Widget       m_SpawnHereFrame;
    private Widget       m_GoEndFrame;
    private ButtonWidget m_DbgBtn;
    private ButtonWidget m_StopAllBtn;

    // Overlay de AYUDA (boton "?"): modal dentro del panel, bilingue ES|EN.
    private ButtonWidget m_HelpBtn;
    private Widget       m_HelpOverlay;
    private ButtonWidget m_HelpLangBtn;
    private ButtonWidget m_HelpCloseBtn;
    private TextWidget          m_HelpTitle;
    private ref array<TextWidget> m_HelpLineW;  // 25 filas visibles (HelpLine0..24); paginan las lineas
    private ref array<string>   m_HelpLines;    // TODAS las lineas del cuerpo
    private ref array<bool>     m_HelpIsTitle;  // por linea: true = titulo (se pinta cyan)
    private int                 m_HelpTop;      // primera linea visible (scroll de la ventana)
    private const int MAXHL = 25;               // filas de linea en el layout (HelpLine0..24)
    private static bool  s_HelpEn = true;   // true = EN (default: el panel publicado esta en ingles), false = ES

    private ref array<ButtonWidget> m_Rows;     // RouteRow0..4
    private Widget       m_RouteSelBg;          // barra de seleccion persistente (detras de la fila elegida)
    private TextWidget   m_Footer;              // footer con medidor live (server FPS + ms/frame del framework)

    // Runner rows compactas (Runner0..7). Cada fila visible solo si hay runner en ese indice.
    // Fase 2: la ventana baja a MAXR=5 (60% del panel); el 40% de abajo es el sub-panel de VACIOS.
    private const int MAXR = 5;     // filas VISIBLES (ventana); la lista de runners puede ser mayor -> scroll
    private int m_RunScroll;        // indice del runner en la fila de arriba (scroll de la ventana)
    private ref array<int> m_RowRunnerIdx;  // 2026-08-18: fila visible (compactada) -> indice real en s_RunnersInfo (para los botones)
    private TextWidget   m_RunnersCount;
    private ref array<Widget>       m_RunCard;
    private ref array<Widget>       m_RunBg;     // relleno de cada tarjeta (target del hover)
    private ref array<TextWidget>   m_RunName;
    private ref array<TextWidget>   m_RunStatus;
    private ref array<ButtonWidget> m_RunReset;
    private ref array<ButtonWidget> m_RunTp;
    private ref array<ButtonWidget> m_RunPause;
    private ref array<ButtonWidget> m_RunStop;

    // Fase 2: sub-panel ACTIVE SPAWN VEHICLE (vehiculos vacios). Empty0..4 finos con TP + ELIMINAR.
    private const int MAXE = 5;     // filas VISIBLES del panel de vacios
    private int m_EmpScroll;        // scroll de la ventana de vacios
    private TextWidget   m_SpawnRunCount;
    private ref array<Widget>       m_EmpCard;
    private ref array<TextWidget>   m_EmpName;
    private ref array<ButtonWidget> m_EmpTp;
    private ref array<ButtonWidget> m_EmpDel;
    // LA TARJETA EXISTE DESDE EL CLIC. El spawn del vacio carga el JSON en el server de forma
    // sincrona (segundos con una toma grande) y el panel no se refresca hasta que termina: se volvia a apretar y quedaban dos
    // vacios encimados. El clic deja una fila LOADING local (ruta + wp) que desaparece cuando el server la lista, o a los
    // PEND_VIDA_S; un segundo clic sobre la misma ruta y wp con la fila LOADING viva no manda nada (el server tambien lo rechaza).
    private ref array<string> m_PendRuta;
    private ref array<int>    m_PendWp;
    private ref array<float>  m_PendT;
    private const float PEND_VIDA_S = 15.0;

    private int   m_SelRoute;       // indice de fila seleccionada
    private float m_StatusTimer;
    private ref array<string> m_RouteFiles;   // lista COMPLETA de rutas (no solo visibles); mapea seleccion -> ruta
    private int   m_RouteListCount;            // -1 = aun no poblado; detecta llegada/cambio de la lista
    private int   m_RouteScroll;               // indice de la ruta en la fila de arriba (scroll de la ventana de rutas)

    private const int ROWS = 8;
    private const float PROG_BG_W = 404.0;   // ancho del Runner0_ProgBg (px, hexactsize 1)


    override Widget Init() {
        layoutRoot = GetGame().GetWorkspace().CreateWidgets("BZ_AutoDrive/gui/layouts/control_panel_v2.layout");

        m_CloseBtn     = ButtonWidget.Cast(layoutRoot.FindAnyWidget("CloseBtn"));
        m_LoadSpawnBtn = ButtonWidget.Cast(layoutRoot.FindAnyWidget("LoadSpawnBtn"));
        m_LogNativeChk = ButtonWidget.Cast(layoutRoot.FindAnyWidget("LogNativeChk"));
        m_LogAiChk     = ButtonWidget.Cast(layoutRoot.FindAnyWidget("LogAiChk"));
        UpdateLogChecks();
        m_PreviewText  = TextWidget.Cast(layoutRoot.FindAnyWidget("PreviewText"));
        m_ScrubTrack   = layoutRoot.FindAnyWidget("ScrubTrack");
        m_ScrubFill    = layoutRoot.FindAnyWidget("ScrubFill");
        m_SliderLabel  = TextWidget.Cast(layoutRoot.FindAnyWidget("SliderLabel"));
        m_GoStartFace   = layoutRoot.FindAnyWidget("GoStartFace");
        m_SpawnHereFace = layoutRoot.FindAnyWidget("SpawnHereFace");
        m_GoEndFace     = layoutRoot.FindAnyWidget("GoEndFace");
        m_GoStartFrame   = layoutRoot.FindAnyWidget("GoStartFrame");
        m_SpawnHereFrame = layoutRoot.FindAnyWidget("SpawnHereFrame");
        m_GoEndFrame     = layoutRoot.FindAnyWidget("GoEndFrame");
        m_DbgBtn       = ButtonWidget.Cast(layoutRoot.FindAnyWidget("DbgBtn"));
        m_StopAllBtn   = ButtonWidget.Cast(layoutRoot.FindAnyWidget("StopAllBtn"));
        m_HelpBtn      = ButtonWidget.Cast(layoutRoot.FindAnyWidget("HelpBtn"));
        m_HelpOverlay  = layoutRoot.FindAnyWidget("HelpOverlay");
        m_HelpLangBtn  = ButtonWidget.Cast(layoutRoot.FindAnyWidget("HelpLangBtn"));
        m_HelpCloseBtn = ButtonWidget.Cast(layoutRoot.FindAnyWidget("HelpCloseBtn"));
        m_HelpTitle    = TextWidget.Cast(layoutRoot.FindAnyWidget("HelpTitle"));
        m_HelpLineW    = new array<TextWidget>();
        for (int hl = 0; hl < MAXHL; hl++) m_HelpLineW.Insert(TextWidget.Cast(layoutRoot.FindAnyWidget("HelpLine" + hl)));
        if (m_HelpOverlay) m_HelpOverlay.Show(false);   // arranca oculto; el "?" lo togglea
        UpdateHelp();

        m_Rows = new array<ButtonWidget>();
        for (int i = 0; i < ROWS; i++) {
            ButtonWidget row = ButtonWidget.Cast(layoutRoot.FindAnyWidget("RouteRow" + i));
            m_Rows.Insert(row);
        }
        m_RouteSelBg = layoutRoot.FindAnyWidget("RouteSelBg");
        if (m_RouteSelBg) m_RouteSelBg.Show(false);   // oculta hasta que RenderRoutes la posiciona
        m_Footer = TextWidget.Cast(layoutRoot.FindAnyWidget("Footer"));   // medidor live (lo refresca Update)

        m_RunnersCount = TextWidget.Cast(layoutRoot.FindAnyWidget("RunnersCount"));
        m_RunCard   = new array<Widget>();
        m_RunBg     = new array<Widget>();
        m_RunName   = new array<TextWidget>();
        m_RunStatus = new array<TextWidget>();
        m_RunReset  = new array<ButtonWidget>();
        m_RunTp     = new array<ButtonWidget>();
        m_RunPause  = new array<ButtonWidget>();
        m_RunStop   = new array<ButtonWidget>();
        for (int r = 0; r < MAXR; r++) {
            m_RunCard.Insert(layoutRoot.FindAnyWidget("Runner" + r));
            m_RunBg.Insert(layoutRoot.FindAnyWidget("Runner" + r + "_Bg"));
            m_RunName.Insert(TextWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Name")));
            m_RunStatus.Insert(TextWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Status")));
            m_RunReset.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Reset")));
            m_RunTp.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Teleport")));
            m_RunPause.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Pause")));
            m_RunStop.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Runner" + r + "_Stop")));
            if (m_RunCard.Get(r)) m_RunCard.Get(r).Show(false);   // arrancan ocultas; RefreshRunner muestra las activas
        }
        // Runner5/6/7 quedan en el layout pero SIEMPRE ocultos: la ventana muestra MAXR=5 filas (60% del panel;
        // el 40% de abajo es el sub-panel de vacios). El scroll pagina el resto de runners.
        Widget r5 = layoutRoot.FindAnyWidget("Runner5"); if (r5) r5.Show(false);
        Widget r6 = layoutRoot.FindAnyWidget("Runner6"); if (r6) r6.Show(false);
        Widget r7 = layoutRoot.FindAnyWidget("Runner7"); if (r7) r7.Show(false);
        m_RunScroll = 0;

        // Fase 2: bind del sub-panel de vacios (Empty0..4).
        m_SpawnRunCount = TextWidget.Cast(layoutRoot.FindAnyWidget("SpawnRunCount"));
        m_EmpCard = new array<Widget>();
        m_EmpName = new array<TextWidget>();
        m_EmpTp   = new array<ButtonWidget>();
        m_EmpDel  = new array<ButtonWidget>();
        for (int e = 0; e < MAXE; e++) {
            m_EmpCard.Insert(layoutRoot.FindAnyWidget("Empty" + e));
            m_EmpName.Insert(TextWidget.Cast(layoutRoot.FindAnyWidget("Empty" + e + "_Name")));
            m_EmpTp.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Empty" + e + "_Tp")));
            m_EmpDel.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Empty" + e + "_Del")));
            if (m_EmpCard.Get(e)) m_EmpCard.Get(e).Show(false);   // arrancan ocultas; RefreshEmpty muestra las activas
        }
        m_EmpScroll = 0;
        m_PendRuta = new array<string>();
        m_RechazoVer = BZBusClientManager.s_SpawnRechazoVer;
        m_PendWp = new array<int>();
        m_PendT = new array<float>();

        m_SelRoute      = 0;
        m_StatusTimer   = 0;
        m_RouteFiles    = new array<string>();
        m_RouteListCount = -1;       // fuerza el primer poblado cuando llegue la lista
        m_RouteScroll    = 0;
        s_Instance      = this;

        SelectRoute(0);
        BZBusClientManager.RequestRouteList();   // pide la lista de rutas del profile (admin)
        BZBusClientManager.RequestRunners();      // pide el snapshot de runners activos (multi-card)
        BZBusClientManager.RequestEmptyList();     // Fase 2: snapshot de vehiculos vacios spawneados
        RefreshRunner();
        RefreshEmpty();
        return layoutRoot;
    }

    // m_SelRoute es el indice GLOBAL en m_RouteFiles (no la fila visible). El highlight cae
    // en la fila (routeIdx - m_RouteScroll) si esta dentro de la ventana.
    // pedirSiempre = false (la rueda del mouse): si la ruta es la misma que la ultima pedida, solo repinta. Cada pedido hace
    // que el server relea el _hdr y el _wp.csv entero de la ruta, y la rueda pedia uno por muesca (medido: 20 en 3 s). Al
    // abrir el panel, al llegar la lista y en cada clic (aunque sea sobre la misma ruta: refresca una toma reconvertida
    // con el panel abierto) se pide como siempre.
    private void SelectRoute(int routeIdx, bool pedirSiempre = true) {
        m_SelRoute = routeIdx;
        RenderRoutes();   // repinta marcador ">" + color de la fila elegida (DayZ pisa SetColor en ButtonWidget -> el marcador es la senal confiable)
        // pedir info de la toma para el PREVIEW (llega async -> Update la pinta cuando cambia s_RIVer)
        if (m_RouteFiles && routeIdx >= 0 && routeIdx < m_RouteFiles.Count()) {
            string rutaSel = m_RouteFiles.Get(routeIdx);
            if (!pedirSiempre && rutaSel == m_InfoPedidaDe) return;   // misma ruta: ni pedido, ni "loading info...", ni scrubber a cero
            m_InfoPedidaDe = rutaSel;
            BZBusClientManager.RequestRouteInfo(rutaSel);
            if (m_PreviewText) m_PreviewText.SetText("loading info...");
            m_Dragging = false;
            m_ScrubNwp = 0;
            SetScrubWp(0);   // resetea visual + label (nwp=0 -> deja el scrubber al inicio hasta que llegue la info)
        }
    }

    // Fija el wp del scrubber (clamp), reajusta el ancho del relleno y repinta el label.
    // Clave (el bug que arrastrabamos): las unidades de SetSize NO coinciden con los px de pantalla
    // (ni con GetSize), y el hardcodeo 980 se escapaba cada vez mas. Aca se CALIBRA en runtime: se
    // pone el fill a un ancho testigo (100), se mide cuantos px de pantalla ocupo (GetScreenSize) y
    // con esa relacion (unidades por px) se convierte el ancho objetivo REAL (frac * ancho del track
    // en px) a unidades de SetSize. Asi el borde del relleno cae EXACTO bajo el cursor a cualquier
    // resolucion, y su maximo es el ancho del track (termina antes de los botones).
    private void SetScrubWp(int wp) {
        if (wp < 0) wp = 0;
        if (m_ScrubNwp > 1 && wp > m_ScrubNwp - 1) wp = m_ScrubNwp - 1;
        if (m_ScrubNwp <= 1) wp = 0;
        m_ScrubWp = wp;
        float frac = 0;
        if (m_ScrubNwp > 1) frac = wp / (float)(m_ScrubNwp - 1);
        if (m_ScrubFill && m_ScrubTrack) {
            float tsx, tsy, tsw, tsh;
            m_ScrubTrack.GetScreenPos(tsx, tsy);
            m_ScrubTrack.GetScreenSize(tsw, tsh);
            if (tsw > 0) {
                m_ScrubFill.SetSize(100, 16);            // testigo
                float fs, fsh;
                m_ScrubFill.GetScreenSize(fs, fsh);
                float upp = 100.0;                       // unidades de SetSize por px de pantalla
                if (fs > 0) upp = 100.0 / fs;
                m_ScrubFill.SetSize(frac * tsw * upp, tsh * upp);   // ancho REAL objetivo -> unidades
            }
        }
        UpdateSliderLabel();
    }

    // Repinta el label del scrubber (wp / dist / %) leyendo m_ScrubWp + la dist total de la toma.
    private void UpdateSliderLabel() {
        if (!m_SliderLabel) return;
        int nwp = m_ScrubNwp;
        if (nwp <= 0) { m_SliderLabel.SetText(""); return; }
        int last = nwp - 1;
        int wp = m_ScrubWp;
        int pct = 0;
        float distAt = 0;
        if (last > 0) {
            pct = (int)Math.Round((wp * 100.0) / last);
            distAt = (wp * BZBusClientManager.s_RIDist) / last;
        }
        string tag = "";
        if (wp <= 0) tag = "   (start)";
        else if (wp >= last) tag = "   (end)";
        // COORDS del wp del scrubber (2026-08-18, pedido de un jugador): las posiciones llegaron con el route-info.
        // Asi el jugador ve DONDE en el mapa cae el scrubber (y donde spawnearia con "SPAWN ACA").
        string coord = "";
        array<float> pxs = BZBusClientManager.s_RIPosX;
        array<float> pzs = BZBusClientManager.s_RIPosZ;
        if (pxs && pzs && wp >= 0 && wp < pxs.Count() && wp < pzs.Count()) {
            coord = "   ·   " + (int)Math.Round(pxs.Get(wp)) + " " + (int)Math.Round(pzs.Get(wp));
        }
        m_SliderLabel.SetText("wp " + wp + " / " + last + "   ·   " + (int)Math.Round(distAt) + " m   ·   " + pct + "%" + tag + coord);
    }

    // Traduce la posicion del mouse a un wp (por la posicion/ancho REAL del track en pantalla) y lo fija.
    private void SeekFromMouse() {
        if (!m_ScrubTrack || m_ScrubNwp <= 1) return;
        int mx, my;
        GetMousePos(mx, my);
        float tx, ty, tw, th;
        m_ScrubTrack.GetScreenPos(tx, ty);
        m_ScrubTrack.GetScreenSize(tw, th);
        if (tw <= 0) return;
        float frac = (mx - tx) / tw;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        SetScrubWp((int)Math.Round(frac * (m_ScrubNwp - 1)));
    }

    // Nombre lindo para la fila: saca prefijo "BZBusRoute_" y ".json".
    private string Pretty(string fn) {
        string s = fn;
        s.Replace(".json", "");
        if (s == "BZBusRoute") return "(default)";
        s.Replace("BZBusRoute_", "");
        return s;
    }

    // Carga la lista COMPLETA de rutas (todas, no solo las visibles) y pinta la ventana.
    private void PopulateRoutes() {
        m_RouteFiles.Clear();
        array<string> list = BZBusClientManager.s_RouteList;
        if (list) {
            for (int i = 0; i < list.Count(); i++) m_RouteFiles.Insert(list.Get(i));
        }
        m_RouteScroll = 0;
        RenderRoutes();
        if (list) BZBusLog.Info("[Reproductor] " + list.Count() + " rutas (ventana de " + m_Rows.Count() + ", scroll con rueda)");
    }

    // Pinta la ventana visible [m_RouteScroll, +ROWS) de la lista completa de rutas.
    private void RenderRoutes() {
        int n = m_RouteFiles.Count();
        int maxScroll = n - m_Rows.Count();
        if (maxScroll < 0) maxScroll = 0;
        if (m_RouteScroll > maxScroll) m_RouteScroll = maxScroll;
        if (m_RouteScroll < 0) m_RouteScroll = 0;
        ButtonWidget selRowW = null;   // boton VISIBLE de la ruta elegida (para leer su pos/size REAL)
        for (int i = 0; i < m_Rows.Count(); i++) {
            ButtonWidget row = m_Rows.Get(i);
            if (!row) continue;
            int ridx = m_RouteScroll + i;
            if (ridx < n) {
                string mark = "    ";
                if (ridx == m_SelRoute) { mark = " >  "; selRowW = row; }
                row.SetText(mark + Pretty(m_RouteFiles.Get(ridx)));
                // NO seteamos color del boton: el engine pisa ButtonWidget.SetColor (de ahi el ">").
                // La senal de seleccion la dan el ">" + la barra RouteSelBg DETRAS de la fila elegida.
                row.Show(true);
            } else {
                row.Show(false);
            }
        }
        // Resaltado PERSISTENTE de la fila elegida (look HOVER azul claro, fijo hasta LOAD). RouteSelBg va
        // DETRAS de los RouteRow (capa de fondo); como los botones tienen fondo semi-transparente, la barra
        // ASOMA como tint y el NOMBRE (texto del boton, arriba) queda LEGIBLE. Posicion ROBUSTA: leemos la
        // pos/size REAL del boton seleccionado en runtime (GetPos/GetSize -> SetPos/SetSize, sin magic
        // numbers que se desfasan). Si la seleccion quedo fuera de la ventana visible, oculta la barra.
        if (m_RouteSelBg) {
            if (selRowW) {
                float rx, ry, rw, rh;
                selRowW.GetPos(rx, ry);
                selRowW.GetSize(rw, rh);
                m_RouteSelBg.SetPos(rx, ry);
                m_RouteSelBg.SetSize(rw, rh);
                m_RouteSelBg.Show(true);
            } else {
                m_RouteSelBg.Show(false);
            }
        }
    }

    // Refresca las cards desde el snapshot multi-runner (s_RunnersInfo).
    // Refresca las filas desde el snapshot multi-runner (s_RunnersInfo). Una fila por runner
    // activo (hasta MAXR); si hubiera mas, el contador avisa y el overlay DBG los muestra todos.
    private void RefreshRunner() {
        array<ref BZRunnerInfo> rs = BZBusClientManager.s_RunnersInfo;
        int n = 0;
        if (rs) n = rs.Count();
        // COMPACTAR (2026-08-18, pedido de un jugador): mostrar SOLO los activos, siempre pegados arriba, sin
        // huecos. Antes se mapeaba fila = indice crudo -> si terminaba el runner del medio (o el primary
        // inactivo, que nunca se desregistra del multiton) quedaba un agujero. Construimos la lista de
        // INDICES activos en orden; las filas se llenan de esa lista. m_RowRunnerIdx recuerda el indice
        // REAL de cada fila para que los botones (RST/TP/II/[]) actuen sobre el runner correcto.
        array<int> actIdx = new array<int>;
        for (int ci = 0; ci < n; ci++) {
            if (rs.Get(ci) && rs.Get(ci).active) actIdx.Insert(ci);
        }
        int nAct = actIdx.Count();
        // ventana: clamp del scroll a [0, nAct-MAXR]
        int maxScroll = nAct - MAXR;
        if (maxScroll < 0) maxScroll = 0;
        if (m_RunScroll > maxScroll) m_RunScroll = maxScroll;
        if (m_RunScroll < 0) m_RunScroll = 0;
        if (!m_RowRunnerIdx) m_RowRunnerIdx = new array<int>;
        m_RowRunnerIdx.Clear();
        for (int i = 0; i < MAXR; i++) {
            int slot = m_RunScroll + i;
            bool has = (slot < nAct);
            int origIdx = -1;
            if (has) origIdx = actIdx.Get(slot);
            m_RowRunnerIdx.Insert(origIdx);   // fila i -> indice real (o -1 si vacia)
            if (m_RunCard.Get(i)) m_RunCard.Get(i).Show(has);
            if (has) FillRow(i, rs.Get(origIdx));
        }
        if (m_RunnersCount) {
            string suf = "   watch server load";
            if (nAct > MAXR) {
                int hi = m_RunScroll + MAXR;
                if (hi > nAct) hi = nAct;
                suf = " · " + (m_RunScroll + 1) + "-" + hi + "/" + nAct + " · wheel = scroll";
            }
            m_RunnersCount.SetText("RUNNERS: " + nAct + suf);
        }
    }

    // El origen viaja como valor interno ("lista" = cargada desde este panel; el servicio lo compara al terminar la
    // ruta) y se traduce SOLO al mostrar: el valor no se toca.
    private static string OrigenVisible(string origen)
    {
        if (origen == "lista") return "panel";
        return origen;
    }

    // Puebla una fila: nombre [origen] wp X/Y + estado coloreado.
    private void FillRow(int i, BZRunnerInfo info) {
        TextWidget nameW   = m_RunName.Get(i);
        TextWidget statusW = m_RunStatus.Get(i);
        // km/h EN VIVO (2026-08-17): la velocidad viaja en el struct, va en la linea 1 junto al wp.
        string rowTxt = Pretty(info.name) + "  [" + OrigenVisible(info.origin) + "]  wp " + info.wpIdx + "/" + info.wpTotal;
        rowTxt = rowTxt + "  ·  " + Math.Round(info.kmh).ToString() + " km/h";
        if (nameW) nameW.SetText(rowTxt);
        string st; int col;
        // si el server dice que esta haciendo algo (cola / carga / embarque), eso manda. Azul:
        // "en curso", distinto del gris de STOPPED, que es lo que se ve cuando algo se trabo.
        if (info.estado != "")   { st = info.estado; col = ARGB(255, 90, 170, 230); }
        else if (info.paused)    { st = "PAUSED";  col = ARGB(255, 217, 166, 51); }
        else if (info.kmh > 1.0) { st = "DRIVING"; col = ARGB(255, 77, 191, 102); }
        else                     { st = "STOPPED"; col = ARGB(255, 150, 150, 150); }
        // COORDENADAS (2026-08-18, pedido de un jugador): no entraban en la linea 1 (nombre+wp+kmh saturaban los 672px)
        // -> a la linea de ESTADO, que tiene 600px y solo el estado. Formato "ESTADO   ·   X Z".
        st = st + "    ·    " + Math.Round(info.posX).ToString() + " " + Math.Round(info.posZ).ToString();
        if (statusW) { statusW.SetText(st); statusW.SetColor(col); }
        // El boton de pausa es un toggle: muestra ">" cuando esta pausado (para reanudar), "II" cuando corre.
        ButtonWidget pauseW = m_RunPause.Get(i);
        if (pauseW) {
            if (info.paused) pauseW.SetText(">");
            else             pauseW.SetText("II");
        }
    }

    // el wp con el que el server va a registrar el vacio (-1 = endpoint = N-1; sin N conocido queda -1 y se
    // empareja por ruta).
    private int WpRealDePedido(int wp) {
        if (wp >= 0) return wp;
        if (m_ScrubNwp > 0) return m_ScrubNwp - 1;
        return -1;
    }

    private bool PendienteViva(string fname, int wpReal) {
        float ahoraP = GetGame().GetTickTime();
        for (int p = 0; p < m_PendRuta.Count(); p++) {
            if (m_PendRuta.Get(p) != fname) continue;
            if (wpReal >= 0 && m_PendWp.Get(p) >= 0 && m_PendWp.Get(p) != wpReal) continue;
            if (ahoraP - m_PendT.Get(p) <= PEND_VIDA_S) return true;
        }
        return false;
    }

    // retira las filas LOADING de una ruta y wp (wp < 0 de cualquiera de los dos lados = cualquier wp de esa ruta)
    private void RetirarPendiente(string ruta, int wp) {
        bool quito = false;
        for (int p = m_PendRuta.Count() - 1; p >= 0; p--) {
            if (m_PendRuta.Get(p) != ruta) continue;
            if (wp >= 0 && m_PendWp.Get(p) >= 0 && m_PendWp.Get(p) != wp) continue;
            m_PendRuta.RemoveOrdered(p);
            m_PendWp.RemoveOrdered(p);
            m_PendT.RemoveOrdered(p);
            quito = true;
        }
        if (quito) RefreshEmpty();
    }

    // El clic: deja la fila LOADING y manda el pedido; con la fila viva para la misma ruta y wp, no manda nada.
    private void PedirSpawn(string fname, int wp) {
        int wpReal = WpRealDePedido(wp);
        if (PendienteViva(fname, wpReal)) {
            BZBusLog.Info("[Panel] spawn de " + fname + " wp " + wpReal.ToString() + " ya en LOADING: segundo clic ignorado");
            return;
        }
        m_PendRuta.Insert(fname);
        m_PendWp.Insert(wpReal);
        m_PendT.Insert(GetGame().GetTickTime());
        BZBusClientManager.RequestContinuar(fname, wp);
        RefreshEmpty();
    }

    // Los vacios del server + las filas LOADING locales que el server todavia no lista. Una pendiente se retira cuando el server
    // lista un vacio de la misma ruta y wp (o de la ruta, si el wp era -1 sin N conocido), o cuando vence (PEND_VIDA_S: el spawn
    // fallo o la carga se rechazo). Las pendientes van DESPUES de las del server: los indices de TP/ELIMINAR no cambian.
    private array<ref BZEmptyInfo> VaciosConPendientes() {
        array<ref BZEmptyInfo> lista = new array<ref BZEmptyInfo>();
        array<ref BZEmptyInfo> es = BZBusClientManager.s_EmptyInfo;
        int i;
        if (es) {
            for (i = 0; i < es.Count(); i++) lista.Insert(es.Get(i));
        }
        float ahoraV = GetGame().GetTickTime();
        for (int p = m_PendRuta.Count() - 1; p >= 0; p--) {
            bool listada = false;
            if (es) {
                for (i = 0; i < es.Count(); i++) {
                    BZEmptyInfo se = es.Get(i);
                    if (!se || se.routeName != m_PendRuta.Get(p)) continue;
                    if (m_PendWp.Get(p) < 0 || se.wpIndex == m_PendWp.Get(p)) { listada = true; break; }
                }
            }
            if (listada || ahoraV - m_PendT.Get(p) > PEND_VIDA_S) {
                m_PendRuta.RemoveOrdered(p);
                m_PendWp.RemoveOrdered(p);
                m_PendT.RemoveOrdered(p);
                continue;
            }
            BZEmptyInfo pe = new BZEmptyInfo();
            pe.routeName = m_PendRuta.Get(p);
            pe.wpIndex = m_PendWp.Get(p);
            pe.posX = 0;
            pe.posZ = 0;
            pe.pendiente = true;
            lista.Insert(pe);
        }
        return lista;
    }

    // Fase 2: refresca el sub-panel de vacios desde s_EmptyInfo. Ventana MAXE con scroll.
    private void RefreshEmpty() {
        array<ref BZEmptyInfo> es = VaciosConPendientes();   // los del server + las filas LOADING locales
        int n = 0;
        if (es) n = es.Count();
        int maxScroll = n - MAXE;
        if (maxScroll < 0) maxScroll = 0;
        if (m_EmpScroll > maxScroll) m_EmpScroll = maxScroll;
        if (m_EmpScroll < 0) m_EmpScroll = 0;
        for (int i = 0; i < MAXE; i++) {
            int ei = m_EmpScroll + i;
            bool has = (ei < n && es.Get(ei) != null);
            if (m_EmpCard.Get(i)) m_EmpCard.Get(i).Show(has);
            if (has) FillEmptyRow(i, es.Get(ei));
        }
        if (m_SpawnRunCount) {
            string suf = "";
            if (n > MAXE) {
                int hi = m_EmpScroll + MAXE;
                if (hi > n) hi = n;
                suf = " · " + (m_EmpScroll + 1) + "-" + hi + "/" + n + " · wheel = scroll";
            }
            m_SpawnRunCount.SetText("EMPTY: " + n + suf);
        }
    }

    // Puebla una fila de vacio: ruta  ·  wp N  ·  (x, z).
    private void FillEmptyRow(int i, BZEmptyInfo info) {
        TextWidget nameW = m_EmpName.Get(i);
        if (nameW) {
            if (info.pendiente) {
                // la fila LOADING, en el azul de "en curso" del; sin TP ni ELIMINAR (todavia no hay vehiculo).
                string wpTxt = info.wpIndex.ToString();
                if (info.wpIndex < 0) wpTxt = "FINAL";
                nameW.SetText(Pretty(info.routeName) + "    wp " + wpTxt + "    LOADING");
                nameW.SetColor(ARGB(255, 90, 170, 230));
            } else {
                int rx = Math.Round(info.posX);
                int rz = Math.Round(info.posZ);
                nameW.SetText(Pretty(info.routeName) + "    wp " + info.wpIndex + "    (" + rx + ", " + rz + ")");
                nameW.SetColor(ARGB(255, 235, 242, 255));   // el color del layout (0.92 0.95 1)
            }
        }
        if (m_EmpTp.Get(i)) m_EmpTp.Get(i).Show(!info.pendiente);
        if (m_EmpDel.Get(i)) m_EmpDel.Get(i).Show(!info.pendiente);
    }

    // === Overlay de AYUDA (boton "?") — bilingue ES|EN. Texto ASCII a proposito (sin acentos)
    // para no arriesgar encoding en el .c; lee igual de claro. Se arma con s = s + "..." (una
    // linea por sentencia) para no pegar contra "formula too complex" de Enforce. ===
    private void UpdateHelp() {
        if (m_HelpTitle) {
            if (s_HelpEn) m_HelpTitle.SetText("HELP - What each thing does");
            else          m_HelpTitle.SetText("AYUDA - Que hace cada cosa");
        }
        if (m_HelpLangBtn) {
            if (s_HelpEn) m_HelpLangBtn.SetText("ES [EN]");
            else          m_HelpLangBtn.SetText("[ES] EN");
        }
        m_HelpLines   = new array<string>();
        m_HelpIsTitle = new array<bool>();
        if (s_HelpEn) BuildHelpEn(); else BuildHelpEs();
        m_HelpTop = 0;
        RenderHelp();
    }

    // Pinta la ventana de MAXHL filas desde m_HelpTop (patron de la lista de runners). Titulos en cyan
    // (TextWidget.SetColor SI existe, es por-widget), cuerpo en gris claro. La rueda mueve m_HelpTop.
    private void RenderHelp() {
        if (!m_HelpLineW) return;
        int n = m_HelpLines.Count();
        int maxTop = n - MAXHL;
        if (maxTop < 0) maxTop = 0;
        if (m_HelpTop > maxTop) m_HelpTop = maxTop;
        if (m_HelpTop < 0) m_HelpTop = 0;
        int titleCol = ARGB(255, 96, 194, 240);
        int bodyCol  = ARGB(255, 204, 219, 235);
        for (int i = 0; i < MAXHL; i++) {
            TextWidget w = m_HelpLineW.Get(i);
            if (!w) continue;
            int li = m_HelpTop + i;
            if (li < n) {
                w.SetText(m_HelpLines.Get(li));
                if (m_HelpIsTitle.Get(li)) w.SetColor(titleCol); else w.SetColor(bodyCol);
                w.Show(true);
            } else {
                w.SetText("");
                w.Show(false);
            }
        }
    }

    private void HAddTitle(string t) { m_HelpLines.Insert(t); m_HelpIsTitle.Insert(true); }
    private void HAddLine(string l)  { m_HelpLines.Insert(l); m_HelpIsTitle.Insert(false); }
    private void HAddBlank()         { m_HelpLines.Insert(" "); m_HelpIsTitle.Insert(false); }

    private void BuildHelpEs() {
        HAddTitle("ROUTES   (izquierda)");
        HAddLine("Tus rutas guardadas. Clic en una para elegirla; abajo");
        HAddLine("ves su ficha (vehiculo, waypoints, distancia, vel max)");
        HAddLine("y el scrubber.");
        HAddBlank();
        HAddTitle("LOAD & SPAWN");
        HAddLine("Larga la ruta elegida: el NPC (Boris) aparece y la");
        HAddLine("maneja al instante, sin reiniciar el server.");
        HAddBlank();
        HAddTitle("CHECKS   boris_native  /  ai_run");
        HAddLine("Tildalos ANTES de dar play para grabar esa corrida.");
        HAddLine("Opt-in: destildados no graban nada. (Detalle abajo.)");
        HAddBlank();
        HAddTitle("ACTIVE RUNNERS   (derecha, arriba)");
        HAddLine("Cada vehiculo que se maneja AHORA (nombre, waypoint,");
        HAddLine("estado DRIVING / PAUSED / STOPPED). Botones por fila:");
        HAddLine("      RST    reiniciar desde el wp 0");
        HAddLine("      TP     teletransportarte a su lado");
        HAddLine("      II     pausar / reanudar");
        HAddLine("      []     pararlo y sacarlo");
        HAddLine("Rueda del mouse = scroll si hay muchos.");
        HAddLine("STOP ALL = para todos.    DBG = overlay de debug.");
        HAddBlank();
        HAddTitle("ACTIVE SPAWN VEHICLE   (derecha, abajo)");
        HAddLine("Los vehiculos que spawneaste VACIOS (START/HERE/END).");
        HAddLine("Por fila:  TP = ir a su lado,  DELETE = borrarlo.");
        HAddLine("Podes sembrar varios y manejarlos desde aca.");
        HAddBlank();
        HAddTitle("SPAWN EMPTY VEHICLE AT   (barra inferior)");
        HAddLine("Spawnea el vehiculo de la ruta VACIO y manejable en:");
        HAddLine("      START = el inicio     HERE = el wp del scrubber");
        HAddLine("      END   = el final");
        HAddLine("No te teletransporta: aparece arriba en ACTIVE SPAWN.");
        HAddLine("Sirve para continuar una grabacion o sembrar autos.");
        HAddBlank();
        HAddTitle("LOS DOS LOGGERS   (para que sirven)");
        HAddLine("ai_run = la CAJA NEGRA de Boris. Telemetria de la");
        HAddLine("corrida (desvio lateral, saturaciones, pedales, wp).");
        HAddLine("Sirve para DIAGNOSTICAR por que Boris hizo algo raro.");
        HAddLine("boris_native = la TRAYECTORIA de Boris, en el mismo");
        HAddLine("formato que tu grabacion. Para CARGARLA en el editor");
        HAddLine("y superponerla contra tu toma (ver donde se desvio).");
        HAddLine("Los dos son MEDICION, no grabacion: NO los conviertas");
        HAddLine("como toma (clonarian los errores de Boris).");
        HAddBlank();
        HAddTitle("TECLAS   (Opciones > Controles > BZ AutoDrive)");
        HAddLine("      INICIO      abrir / cerrar este panel");
        HAddLine("      NUMPAD 5    grabar (start / stop)");
        HAddLine("      NUMPAD 4    marcar evento / parada");
        HAddLine("El cambio de sentido (intercambio) se AUTO-DETECTA del");
        HAddLine("gear: no necesita tecla. La reversa tambien.");
        HAddLine("Solo funcionan para el admin.");
    }

    private void BuildHelpEn() {
        HAddTitle("ROUTES   (left)");
        HAddLine("Your saved routes. Click one to select it; below you");
        HAddLine("see its card (vehicle, waypoints, distance, max speed)");
        HAddLine("and the scrubber.");
        HAddBlank();
        HAddTitle("LOAD & SPAWN");
        HAddLine("Runs the selected route: the NPC (Boris) spawns and");
        HAddLine("drives it instantly, no server restart.");
        HAddBlank();
        HAddTitle("CHECKS   boris_native  /  ai_run");
        HAddLine("Tick them BEFORE pressing play to record that run.");
        HAddLine("Opt-in: unticked records nothing. (Detail below.)");
        HAddBlank();
        HAddTitle("ACTIVE RUNNERS   (top right)");
        HAddLine("Every vehicle being driven NOW (name, waypoint,");
        HAddLine("status DRIVING / PAUSED / STOPPED). Per-row buttons:");
        HAddLine("      RST    restart from wp 0");
        HAddLine("      TP     teleport next to it");
        HAddLine("      II     pause / resume");
        HAddLine("      []     stop and remove it");
        HAddLine("Mouse wheel = scroll if there are many.");
        HAddLine("STOP ALL = stop them all.    DBG = debug overlay.");
        HAddBlank();
        HAddTitle("ACTIVE SPAWN VEHICLE   (bottom right)");
        HAddLine("The vehicles you spawned EMPTY (START/HERE/END).");
        HAddLine("Per row:  TP = go next to it,  DELETE = remove it.");
        HAddLine("Seed several and manage them from here.");
        HAddBlank();
        HAddTitle("SPAWN EMPTY VEHICLE AT   (bottom bar)");
        HAddLine("Spawns the route's vehicle EMPTY and drivable at:");
        HAddLine("      START = the start     HERE = the scrubber wp");
        HAddLine("      END   = the end");
        HAddLine("It does NOT teleport you: it shows in ACTIVE SPAWN.");
        HAddLine("Use it to continue a recording or seed cars.");
        HAddBlank();
        HAddTitle("THE TWO LOGGERS   (what they are for)");
        HAddLine("ai_run = Boris's BLACK BOX. Run telemetry (lateral");
        HAddLine("deviation, saturations, pedals, target wp).");
        HAddLine("Use it to DIAGNOSE why Boris did something odd.");
        HAddLine("boris_native = Boris's TRAJECTORY, in the same format");
        HAddLine("as your recording. To LOAD it in the editor and");
        HAddLine("overlay it against your take (see where it drifted).");
        HAddLine("Both are MEASUREMENT, not a recording: do NOT convert");
        HAddLine("them as a take (they clone Boris's errors).");
        HAddBlank();
        HAddTitle("KEYS   (Options > Controls > BZ AutoDrive)");
        HAddLine("      HOME        open / close this panel");
        HAddLine("      NUMPAD 5    record (start / stop)");
        HAddLine("      NUMPAD 4    mark event / stop");
        HAddLine("The direction change (interchange) is AUTO-DETECTED from");
        HAddLine("the gear: no key needed. Reverse too.");
        HAddLine("They only work for the admin.");
    }

    override void Update(float timeslice) {
        super.Update(timeslice);
        // SU tecla lo cierra desde aca: en la 1.30, con el menu abierto, la mision ya no ve UAInput.LocalPress(). Con
        // check_focus=false (la misma firma en 1.29 y 1.30; es lo que hace DigitalCodeLockUI de la 1.30)
        // MEDIDO en la 1.30 (cliente, seis aperturas): con el menu abierto esta accion NO dispara, el menu NO recibe
        // OnKeyDown y la mision no ve la accion; lo unico que llega es la tecla CRUDA al OnKeyPress de la mision, que es
        // quien cierra ahi (BZBusMissionGameplay). Este camino queda para la 1.29.
        if (GetGame().GetInput().LocalPress("UABZAutoDrivePanel", false) && GetGame().GetTime() - s_AbiertoMs > 250) {
            CerrarPorTecla();
            return;
        }
        if (m_Dragging) SeekFromMouse();   // seguimiento continuo de la manija del scrubber
        m_StatusTimer += timeslice;
        if (m_StatusTimer >= 0.33) {
            m_StatusTimer = 0;
            BZBusClientManager.RequestRunners();
            BZBusClientManager.RequestEmptyList();   // Fase 2: snapshot de vacios ~3Hz
        }
        // el server no hizo un spawn vacio (lugar ocupado, ruta que no cargo): su fila LOADING se retira ya, sin esperar a
        // que venza
        if (BZBusClientManager.s_SpawnRechazoVer != m_RechazoVer) {
            m_RechazoVer = BZBusClientManager.s_SpawnRechazoVer;
            RetirarPendiente(BZBusClientManager.s_SpawnRechazoRuta, BZBusClientManager.s_SpawnRechazoWp);
        }
        // La lista llega async (RPC roundtrip). Cuando cambia el count, repoblar las filas.
        if (BZBusClientManager.s_RouteListReceived) {
            int c = 0;
            if (BZBusClientManager.s_RouteList) c = BZBusClientManager.s_RouteList.Count();
            if (c != m_RouteListCount) {
                m_RouteListCount = c;
                PopulateRoutes();
                SelectRoute(0);
            }
        }
        // PREVIEW: cuando llega info nueva (s_RIVer cambia) y es de la toma seleccionada, pintar el texto
        if (BZBusClientManager.s_RIVer != m_RIVer) {
            m_RIVer = BZBusClientManager.s_RIVer;
            if (m_PreviewText && m_RouteFiles && m_SelRoute >= 0 && m_SelRoute < m_RouteFiles.Count() && BZBusClientManager.s_RIFname == m_RouteFiles.Get(m_SelRoute)) {
                float rDist = Math.Round(BZBusClientManager.s_RIDist);
                float rMax = Math.Round(BZBusClientManager.s_RIMaxkmh);
                m_PreviewText.SetText(BZBusClientManager.s_RIVehicle + "\n" + BZBusClientManager.s_RINwp + " waypoints   |   " + rDist + " m   |   max " + rMax + " km/h");
                // configurar el scrubber con el rango real de wps de la toma
                m_ScrubNwp = BZBusClientManager.s_RINwp;
                SetScrubWp(0);
            }
        }
        RefreshRunner();
        RefreshEmpty();   // Fase 2: sub-panel de vacios
        // MEDIDOR DE CONSUMO: footer live. Reemplaza el "30 fps" fijo del layout por los valores
        // reales que empuja la telemetria (s_ServerFps + s_FrameworkMs, en el RPC de runners).
        // ms/frame redondeado a 1 decimal (concat de float crudo escupe muchos decimales).
        if (m_Footer) {
            int fps = (int)BZBusClientManager.s_ServerFps;
            float msR = Math.Round(BZBusClientManager.s_FrameworkMs * 10.0) / 10.0;
            // 1.2.5: la version exacta, de una sola fuente (CfgMods BZ_AutoDrive version = config.cpp)
            m_Footer.SetText("BZ_AutoDrive " + BZAutoDriveVersion.Texto() + "  ·  server " + fps + " fps  ·  BZ " + msR + " ms/frame");
        }
    }

    override void OnShow() {
        super.OnShow();
        GetGame().GetInput().ChangeGameFocus(1);
        GetGame().GetMission().PlayerControlEnable(false);
        SetFocus(layoutRoot);
        s_AbiertoMs = GetGame().GetTime();
        if (BZDiag.On()) BZBusLog.Info("[Reproductor] ABIERTO");
    }

    override void OnHide() {
        super.OnHide();
        GetGame().GetInput().ChangeGameFocus(-1);
        GetGame().GetMission().PlayerControlEnable(true);
        if (s_Instance == this) s_Instance = null;
        if (BZDiag.On())
        {
            string porQue = s_CierrePor;
            if (porQue == "") porQue = "otro: el motor u otro menu";
            BZBusLog.Info("[Reproductor] CERRADO (" + porQue + ")");
        }
        s_CierrePor = "";
    }

    // Refresca el label de los 2 checks segun el estado persistente (el color en ButtonWidget no es confiable
    // en DayZ -> el marcador [x]/[ ] en el texto es la señal). s_LogNative/s_LogAi viajan con el play.
    private void UpdateLogChecks() {
        if (m_LogNativeChk) {
            if (s_LogNative) m_LogNativeChk.SetText(" [x] boris_native");
            else m_LogNativeChk.SetText(" [ ] boris_native");
        }
        if (m_LogAiChk) {
            if (s_LogAi) m_LogAiChk.SetText(" [x] ai_run");
            else m_LogAiChk.SetText(" [ ] ai_run");
        }
    }

    override bool OnClick(Widget w, int x, int y, int button) {
        if (w == m_CloseBtn) {
            s_CierrePor = "boton";
            GetGame().GetUIManager().HideScriptedMenu(this);
            return true;
        }
        if (w == m_DbgBtn) {
            BZDebugOverlay.Get().Toggle();   // HUD no-modal; persiste al cerrar el panel
            return true;
        }
        if (w == m_StopAllBtn) {
            BZBusClientManager.RequestStopAll();   // para TODOS los runners (limpieza del test de techo)
            return true;
        }
        // Ayuda ("?"): togglea el overlay; CERRAR lo oculta; ES|EN cambia idioma.
        if (w == m_HelpBtn) {
            bool vis = (m_HelpOverlay && m_HelpOverlay.IsVisible());
            if (m_HelpOverlay) m_HelpOverlay.Show(!vis);
            if (!vis) UpdateHelp();
            return true;
        }
        if (w == m_HelpCloseBtn) { if (m_HelpOverlay) m_HelpOverlay.Show(false); return true; }
        if (w == m_HelpLangBtn)  { s_HelpEn = !s_HelpEn; UpdateHelp(); return true; }
        for (int i = 0; i < m_Rows.Count(); i++) {
            if (w == m_Rows.Get(i)) {
                SelectRoute(m_RouteScroll + i);   // fila visible -> indice real de ruta
                return true;
            }
        }
        if (w == m_LogNativeChk) { s_LogNative = !s_LogNative; UpdateLogChecks(); return true; }
        if (w == m_LogAiChk)     { s_LogAi = !s_LogAi; UpdateLogChecks(); return true; }
        if (w == m_LoadSpawnBtn) {
            if (m_RouteFiles && m_SelRoute >= 0 && m_SelRoute < m_RouteFiles.Count())
                BZBusClientManager.RequestLoadRoute(m_RouteFiles.Get(m_SelRoute), s_LogNative, s_LogAi);
            return true;
        }
        // Los botones de spawn (INICIO/SPAWN ACA/FINAL) son ImageWidget (cara metalica) -> el click
        // se maneja en OnMouseButtonDown, no aca.
        // Filas -> control POR-RUNNER por indice. action: 0=stop 1=pause(toggle) 2=teleport 3=reset.
        // RESET (RST) re-arranca ESE runner desde wp 0 de su ruta YA CARGADA: el server hace
        // RespawnBus() (config en memoria, NO recarga JSON). Sirve para re-ver un tramo rapido.
        for (int rr = 0; rr < MAXR; rr++) {
            // fila visible COMPACTADA -> indice REAL del runner (mapeado en RefreshRunner). Antes era
            // m_RunScroll+rr (indice crudo), que con la lista compactada apuntaba al runner equivocado.
            int ri = -1;
            if (m_RowRunnerIdx && rr < m_RowRunnerIdx.Count()) ri = m_RowRunnerIdx.Get(rr);
            if (ri < 0) continue;   // fila vacia -> el click no controla ningun runner
            if (w == m_RunReset.Get(rr)) { BZBusClientManager.RequestRunnerCtl(ri, 3); return true; }
            if (w == m_RunTp.Get(rr))    { BZBusClientManager.RequestRunnerCtl(ri, 2); return true; }
            if (w == m_RunPause.Get(rr)) { BZBusClientManager.RequestRunnerCtl(ri, 1); return true; }
            if (w == m_RunStop.Get(rr))  { BZBusClientManager.RequestRunnerCtl(ri, 0); return true; }
        }
        // Fase 2: filas de vacios -> TP (llevar al player al lado) / ELIMINAR. action: 0=TP 1=eliminar.
        for (int ee = 0; ee < MAXE; ee++) {
            int ei = m_EmpScroll + ee;   // fila visible -> indice real del vacio
            if (w == m_EmpTp.Get(ee))  { BZBusClientManager.RequestEmptyCtl(ei, 0); return true; }
            if (w == m_EmpDel.Get(ee)) { BZBusClientManager.RequestEmptyCtl(ei, 1); return true; }
        }
        return super.OnClick(w, x, y, button);
    }

    // Scrubber: click/arrastre sobre el track. El seguimiento continuo lo hace Update (polling del mouse).
    override bool OnMouseButtonDown(Widget w, int x, int y, int button) {
        if (w == m_ScrubTrack && button == 0) {
            m_Dragging = true;
            SeekFromMouse();
            return true;
        }
        // Botones de spawn (caras metalicas). INICIO=wp0, SPAWN ACA=wp del scrubber, FINAL=-1 (endpoint).
        if (button == 0 && m_RouteFiles && m_SelRoute >= 0 && m_SelRoute < m_RouteFiles.Count()) {
            if (w == m_GoStartFace)   { PedirSpawn(m_RouteFiles.Get(m_SelRoute), 0); return true; }
            if (w == m_SpawnHereFace) { PedirSpawn(m_RouteFiles.Get(m_SelRoute), m_ScrubWp); return true; }
            if (w == m_GoEndFace)     { PedirSpawn(m_RouteFiles.Get(m_SelRoute), -1); return true; }
        }
        return super.OnMouseButtonDown(w, x, y, button);
    }

    override bool OnMouseButtonUp(Widget w, int x, int y, int button) {
        if (m_Dragging) { m_Dragging = false; return true; }
        return super.OnMouseButtonUp(w, x, y, button);
    }

    // Hover: ilumina el relleno de la tarjeta del runner bajo el mouse (efecto activo).
    override bool OnMouseEnter(Widget w, int x, int y) {
        for (int i = 0; i < MAXR; i++) {
            if (w == m_RunCard.Get(i)) {
                if (m_RunBg.Get(i)) m_RunBg.Get(i).SetColor(ARGB(255, 56, 74, 94));
                return true;
            }
        }
        // Hover sobre la cara metalica: ilumina el marco de acento Y sube el brillo del metal (209->255).
        if (w == m_GoStartFace)   { if (m_GoStartFrame)   m_GoStartFrame.SetColor(ARGB(255, 130, 180, 220)); if (m_GoStartFace)   m_GoStartFace.SetColor(ARGB(255, 255, 255, 255)); return true; }
        if (w == m_SpawnHereFace) { if (m_SpawnHereFrame) m_SpawnHereFrame.SetColor(ARGB(255, 150, 245, 190)); if (m_SpawnHereFace) m_SpawnHereFace.SetColor(ARGB(255, 255, 255, 255)); return true; }
        if (w == m_GoEndFace)     { if (m_GoEndFrame)     m_GoEndFrame.SetColor(ARGB(255, 130, 180, 220)); if (m_GoEndFace)     m_GoEndFace.SetColor(ARGB(255, 255, 255, 255)); return true; }
        return false;
    }
    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y) {
        for (int i = 0; i < MAXR; i++) {
            if (w == m_RunCard.Get(i)) {
                if (m_RunBg.Get(i)) m_RunBg.Get(i).SetColor(ARGB(255, 33, 43, 56));
                return true;
            }
        }
        if (w == m_GoStartFace)   { if (m_GoStartFrame)   m_GoStartFrame.SetColor(ARGB(255, 77, 115, 153)); if (m_GoStartFace)   m_GoStartFace.SetColor(ARGB(255, 209, 209, 209)); return true; }
        if (w == m_SpawnHereFace) { if (m_SpawnHereFrame) m_SpawnHereFrame.SetColor(ARGB(255, 87, 219, 148)); if (m_SpawnHereFace) m_SpawnHereFace.SetColor(ARGB(255, 209, 209, 209)); return true; }
        if (w == m_GoEndFace)     { if (m_GoEndFrame)     m_GoEndFrame.SetColor(ARGB(255, 77, 115, 153)); if (m_GoEndFace)     m_GoEndFace.SetColor(ARGB(255, 209, 209, 209)); return true; }
        return false;
    }

    // True si el widget bajo la rueda (o un ancestro) es una card de runner (ruta activa).
    private bool WheelOverRunners(Widget w) {
        Widget cur = w;
        int g = 0;
        while (cur && g < 6) {
            for (int i = 0; i < m_RunCard.Count(); i++) {
                if (cur == m_RunCard.Get(i)) return true;
            }
            cur = cur.GetParent();
            g++;
        }
        return false;
    }

    // True si el widget bajo la rueda (o un ancestro) es una fila de vacio.
    private bool WheelOverEmpties(Widget w) {
        Widget cur = w;
        int g = 0;
        while (cur && g < 6) {
            for (int i = 0; i < m_EmpCard.Count(); i++) {
                if (cur == m_EmpCard.Get(i)) return true;
            }
            cur = cur.GetParent();
            g++;
        }
        return false;
    }

    // Rueda del mouse: sobre una card de runner (ruta activa) scrollea esa lista; sobre una fila de
    // vacio scrollea el panel de vacios; en cualquier otro lado, scrollea la lista de rutas.
    override bool OnMouseWheel(Widget w, int x, int y, int wheel) {
        // Si la ayuda esta abierta, YO manejo el scroll del overlay (mi OnMouseWheel intercepta la rueda
        // para las listas, asi que el ScrollWidget nativo no la recibe). Igual que la lista de tomas.
        if (m_HelpOverlay && m_HelpOverlay.IsVisible()) {
            m_HelpTop = m_HelpTop - wheel * 3;   // rueda arriba (+1) = subir; 3 lineas por muesca. Clamp en RenderHelp.
            RenderHelp();
            return true;
        }
        if (WheelOverEmpties(w)) {
            m_EmpScroll -= wheel;   // clamp en RefreshEmpty.
            if (m_EmpScroll < 0) m_EmpScroll = 0;
        } else if (WheelOverRunners(w)) {
            m_RunScroll -= wheel;   // rueda arriba (+1) = subir; abajo (-1) = bajar. Clamp en RefreshRunner.
            if (m_RunScroll < 0) m_RunScroll = 0;
        } else {
            m_RouteScroll -= wheel;
            RenderRoutes();           // clampea y repinta la ventana de rutas
            SelectRoute(m_SelRoute, false);  // re-highlight la fila seleccionada si sigue visible (sin volver a pedir su info)
        }
        return true;
    }

    // Cierre por ESC DENTRO del menu (red de seguridad). El menu tiene el foco de teclado,
    // asi que el OnKeyPress de la mision NO llega -> se cierra aca. return true CONSUME el ESC
    // para que NO propague y abra el menu del juego.
    // HOME NO se maneja aca: el toggle (abrir/cerrar) va en el input de la mision (UABZAutoDrivePanel),
    // que corre siempre y no depende del foco del menu (confiable, pedido del usuario).
    override bool OnKeyDown(Widget w, int x, int y, int key) {
        if (key == KeyCode.KC_ESCAPE) {
            s_CierrePor = "ESC";
            GetGame().GetUIManager().HideScriptedMenu(this);
            return true;
        }
        // red: la tecla por defecto (INICIO) cierra aunque la accion no llegue, salvo escribiendo en un campo
        if (key == KeyCode.KC_HOME && !EditBoxWidget.Cast(w)) {
            CerrarPorTecla();
            return true;
        }
        return super.OnKeyDown(w, x, y, key);
    }

    void CerrarPorTecla() {
        s_CerradoMs = GetGame().GetTime();
        s_CierrePor = "tecla";
        GetGame().GetUIManager().HideScriptedMenu(this);
    }

    override bool UseKeyboard() { return true; }
    override bool UseMouse()    { return true; }
}
