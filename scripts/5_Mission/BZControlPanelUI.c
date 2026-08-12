// ============================================================================
//  BZControlPanelUI - Control Panel del framework (admin).
//  Pestañas: VEHICULO (v1, acciones via RPC re-validados server-side) y
//  GRABACION (v1.2, controla el BZPathLogService client-side directo).
//  Se abre con la tecla configurada (HOME default), solo admins (gate en
//  MissionGameplay). Las acciones de vehiculo el server re-valida admin.
// ============================================================================

class BZControlPanelUI extends UIScriptedMenu {

    static BZControlPanelUI s_Instance;

    // Comunes
    private ButtonWidget m_CloseBtn;
    private ButtonWidget m_TabVehicleBtn;
    private ButtonWidget m_TabRecordBtn;
    private Widget       m_GroupVehicle;
    private Widget       m_GroupRecord;

    // Grupo Vehiculo
    private ButtonWidget m_SpawnBtn;
    private ButtonWidget m_StopBtn;
    private ButtonWidget m_ResetBtn;
    private ButtonWidget m_PauseBtn;
    private ButtonWidget m_RoutePrevBtn;
    private ButtonWidget m_RouteNextBtn;
    private TextWidget   m_RouteLabel;
    private TextWidget   m_StatusText;

    // Grupo Grabacion
    private ButtonWidget m_RecBtn;
    private ButtonWidget m_RecPauseBtn;
    private ButtonWidget m_MarkBtn;
    private ButtonWidget m_ModeNormalBtn;
    private ButtonWidget m_ModeParkingBtn;
    private ButtonWidget m_ModeManiobraBtn;
    private ButtonWidget m_ModeReverseBtn;
    private TextWidget   m_RecStateText;

    private int   m_RouteIndex;     // 0 = default, 1-4 = slot N
    private int   m_ActiveTab;      // 0 = vehiculo, 1 = grabacion
    private float m_StatusTimer;    // acumula timeslice para pedir telemetria ~3Hz

    // Colores (ARGB) para tabs y botones de modo
    private int TAB_ON()    { return ARGB(242, 51, 89, 128); }   // azul
    private int TAB_OFF()   { return ARGB(242, 46, 46, 56); }    // gris
    private int MODE_ON()   { return ARGB(242, 51, 115, 64); }   // verde
    private int MODE_OFF()  { return ARGB(242, 46, 46, 56); }    // gris

    override Widget Init() {
        layoutRoot = GetGame().GetWorkspace().CreateWidgets("BZ_AutoDrive/gui/layouts/control_panel.layout");

        m_CloseBtn       = ButtonWidget.Cast(layoutRoot.FindAnyWidget("CloseBtn"));
        m_TabVehicleBtn  = ButtonWidget.Cast(layoutRoot.FindAnyWidget("TabVehicleBtn"));
        m_TabRecordBtn   = ButtonWidget.Cast(layoutRoot.FindAnyWidget("TabRecordBtn"));
        m_GroupVehicle   = layoutRoot.FindAnyWidget("GroupVehicle");
        m_GroupRecord    = layoutRoot.FindAnyWidget("GroupRecord");

        m_SpawnBtn       = ButtonWidget.Cast(layoutRoot.FindAnyWidget("SpawnBtn"));
        m_StopBtn        = ButtonWidget.Cast(layoutRoot.FindAnyWidget("StopBtn"));
        m_ResetBtn       = ButtonWidget.Cast(layoutRoot.FindAnyWidget("ResetBtn"));
        m_PauseBtn       = ButtonWidget.Cast(layoutRoot.FindAnyWidget("PauseBtn"));
        m_RoutePrevBtn   = ButtonWidget.Cast(layoutRoot.FindAnyWidget("RoutePrevBtn"));
        m_RouteNextBtn   = ButtonWidget.Cast(layoutRoot.FindAnyWidget("RouteNextBtn"));
        m_RouteLabel     = TextWidget.Cast(layoutRoot.FindAnyWidget("RouteLabel"));
        m_StatusText     = TextWidget.Cast(layoutRoot.FindAnyWidget("StatusText"));

        m_RecBtn         = ButtonWidget.Cast(layoutRoot.FindAnyWidget("RecBtn"));
        m_RecPauseBtn    = ButtonWidget.Cast(layoutRoot.FindAnyWidget("RecPauseBtn"));
        m_MarkBtn        = ButtonWidget.Cast(layoutRoot.FindAnyWidget("MarkBtn"));
        m_ModeNormalBtn  = ButtonWidget.Cast(layoutRoot.FindAnyWidget("ModeNormalBtn"));
        m_ModeParkingBtn = ButtonWidget.Cast(layoutRoot.FindAnyWidget("ModeParkingBtn"));
        m_ModeManiobraBtn= ButtonWidget.Cast(layoutRoot.FindAnyWidget("ModeManiobraBtn"));
        m_ModeReverseBtn = ButtonWidget.Cast(layoutRoot.FindAnyWidget("ModeReverseBtn"));
        m_RecStateText   = TextWidget.Cast(layoutRoot.FindAnyWidget("RecStateText"));

        m_RouteIndex   = 0;
        m_StatusTimer  = 0;
        s_Instance     = this;
        UpdateRouteLabel();
        BZBusClientManager.RequestPanelStatus();  // primer pedido inmediato al abrir
        RefreshStatusDisplay();
        SwitchTab(0);  // arranca en Vehiculo
        return layoutRoot;
    }

    // ----------------------------------------------------------------- Tabs
    private void SwitchTab(int tab) {
        m_ActiveTab = tab;
        bool veh = (tab == 0);
        if (m_GroupVehicle) m_GroupVehicle.Show(veh);
        if (m_GroupRecord)  m_GroupRecord.Show(!veh);
        if (m_TabVehicleBtn) {
            if (veh) m_TabVehicleBtn.SetColor(TAB_ON());
            else     m_TabVehicleBtn.SetColor(TAB_OFF());
        }
        if (m_TabRecordBtn) {
            if (veh) m_TabRecordBtn.SetColor(TAB_OFF());
            else     m_TabRecordBtn.SetColor(TAB_ON());
        }
        if (!veh) RefreshRecordDisplay();
    }

    // ----------------------------------------------------- Vehiculo (v1.1)
    private void RefreshStatusDisplay() {
        if (!m_StatusText) return;
        string txt = Widget.TranslateString("#STR_BZAD_cp_status") + ": ";
        if (!BZBusClientManager.s_PanelStatusValid || !BZBusClientManager.s_PanelActive) {
            txt = txt + Widget.TranslateString("#STR_BZAD_cp_no_vehicle");
            m_StatusText.SetText(txt);
            return;
        }
        string estado;
        if (BZBusClientManager.s_PanelPaused)        estado = Widget.TranslateString("#STR_BZAD_cp_st_paused");
        else if (BZBusClientManager.s_PanelKmh > 1.0) estado = Widget.TranslateString("#STR_BZAD_cp_st_driving");
        else                                          estado = Widget.TranslateString("#STR_BZAD_cp_st_stopped");

        int pct = 0;
        if (BZBusClientManager.s_PanelWpTotal > 0)
            pct = (int)((BZBusClientManager.s_PanelWpIdx * 100.0) / BZBusClientManager.s_PanelWpTotal);
        int kmhInt = (int)BZBusClientManager.s_PanelKmh;
        string mode = BZBusClientManager.s_PanelMode;
        if (mode == "") mode = "normal";

        txt = txt + estado;
        txt = txt + " | wp " + BZBusClientManager.s_PanelWpIdx + "/" + BZBusClientManager.s_PanelWpTotal + " (" + pct + "%)";
        txt = txt + " | " + kmhInt + " km/h | " + mode + " | AR " + BZBusClientManager.s_PanelAR;
        m_StatusText.SetText(txt);
    }

    private void UpdateRouteLabel() {
        if (!m_RouteLabel) return;
        string label = Widget.TranslateString("#STR_BZAD_cp_route") + ": ";
        if (m_RouteIndex == 0)
            label += Widget.TranslateString("#STR_BZAD_cp_route_default");
        else
            label += Widget.TranslateString("#STR_BZAD_cp_route_slot") + " " + m_RouteIndex;
        m_RouteLabel.SetText(label);
    }

    // ---------------------------------------------------- Grabacion (v1.2)
    // Refleja el estado del PathLogger (client-side) en el grupo Grabacion.
    private void RefreshRecordDisplay() {
        if (m_RecStateText) {
            int st = BZPathLogService.GetInstance().GetState();
            string s = Widget.TranslateString("#STR_BZAD_cp_rec_state") + ": ";
            if (st == BZPathLogState.RECORDING)   s = s + Widget.TranslateString("#STR_BZAD_rec_recording");
            else if (st == BZPathLogState.PAUSED) s = s + Widget.TranslateString("#STR_BZAD_rec_paused");
            else                                  s = s + Widget.TranslateString("#STR_BZAD_rec_off");
            m_RecStateText.SetText(s);
        }
        string mode = BZPathLogService.GetInstance().GetRecordingMode();
        HighlightMode(m_ModeNormalBtn,   mode == "normal");
        HighlightMode(m_ModeParkingBtn,  mode == "parking");
        HighlightMode(m_ModeManiobraBtn, mode == "maniobra");
        HighlightMode(m_ModeReverseBtn,  mode == "reverse");
    }

    private void HighlightMode(ButtonWidget btn, bool active) {
        if (!btn) return;
        if (active) btn.SetColor(MODE_ON());
        else        btn.SetColor(MODE_OFF());
    }

    // Lleva el modo de grabacion a "normal" apagando el modo activo (no hay
    // ToggleNormal en el service; se sale toggleando el modo vigente).
    private void GoModeNormal() {
        BZPathLogService svc = BZPathLogService.GetInstance();
        string m = svc.GetRecordingMode();
        if (m == "parking")       svc.ToggleParkingMode();
        else if (m == "maniobra") svc.ToggleManiobraMode();
        else if (m == "reverse")  svc.ToggleReverseMode();
    }

    // ----------------------------------------------------------- Lifecycle
    override void Update(float timeslice) {
        super.Update(timeslice);
        m_StatusTimer += timeslice;
        if (m_StatusTimer >= 0.33) {
            m_StatusTimer = 0;
            if (m_ActiveTab == 0) BZBusClientManager.RequestPanelStatus();
        }
        if (m_ActiveTab == 0) RefreshStatusDisplay();
        else                  RefreshRecordDisplay();
    }

    override void OnShow() {
        super.OnShow();
        GetGame().GetInput().ChangeGameFocus(1);
        GetGame().GetMission().PlayerControlEnable(false);
        SetFocus(layoutRoot);
    }

    override void OnHide() {
        super.OnHide();
        GetGame().GetInput().ChangeGameFocus(-1);
        GetGame().GetMission().PlayerControlEnable(true);
        if (s_Instance == this) s_Instance = null;
    }

    // -------------------------------------------------------------- Clicks
    override bool OnClick(Widget w, int x, int y, int button) {
        if (w == m_CloseBtn) {
            GetGame().GetUIManager().HideScriptedMenu(this);
            return true;
        }
        if (w == m_TabVehicleBtn) { SwitchTab(0); return true; }
        if (w == m_TabRecordBtn)  { SwitchTab(1); return true; }

        // --- Grupo Vehiculo ---
        if (w == m_RoutePrevBtn) {
            m_RouteIndex--;
            if (m_RouteIndex < 0) m_RouteIndex = 4;
            UpdateRouteLabel();
            return true;
        }
        if (w == m_RouteNextBtn) {
            m_RouteIndex++;
            if (m_RouteIndex > 4) m_RouteIndex = 0;
            UpdateRouteLabel();
            return true;
        }
        if (w == m_SpawnBtn) {
            if (m_RouteIndex == 0) BZBusClientManager.RequestRespawn();
            else                   BZBusClientManager.RequestRespawnSlot(m_RouteIndex);
            return true;
        }
        if (w == m_StopBtn)  { BZBusClientManager.RequestStopBus();     return true; }
        if (w == m_ResetBtn) { BZBusClientManager.RequestRespawn();     return true; }
        if (w == m_PauseBtn) { BZBusClientManager.RequestPauseToggle(); return true; }

        // --- Grupo Grabacion (PathLogger client-side directo) ---
        if (w == m_RecBtn)      { BZPathLogService.GetInstance().ToggleStartStop();  RefreshRecordDisplay(); return true; }
        if (w == m_RecPauseBtn) { BZPathLogService.GetInstance().TogglePause();      RefreshRecordDisplay(); return true; }
        if (w == m_MarkBtn)     { BZPathLogService.GetInstance().MarkStop();         RefreshRecordDisplay(); return true; }
        if (w == m_ModeNormalBtn)   { GoModeNormal();                                       RefreshRecordDisplay(); return true; }
        if (w == m_ModeParkingBtn)  { BZPathLogService.GetInstance().ToggleParkingMode();   RefreshRecordDisplay(); return true; }
        if (w == m_ModeManiobraBtn) { BZPathLogService.GetInstance().ToggleManiobraMode();  RefreshRecordDisplay(); return true; }
        if (w == m_ModeReverseBtn)  { BZPathLogService.GetInstance().ToggleReverseMode();   RefreshRecordDisplay(); return true; }

        return super.OnClick(w, x, y, button);
    }

    override bool UseKeyboard() { return true; }
    override bool UseMouse()    { return true; }
}
