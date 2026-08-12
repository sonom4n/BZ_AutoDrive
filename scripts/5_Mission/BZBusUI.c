// ============================================================================
//  BZBusUI - pantalla de parada del bus.
//  Tab 0: Estado actual + ETA a esta parada.
//  Tab 1: Próximas paradas en la dirección actual.
//  Tab 2: Avisos para el jugador (no obstruir la ruta, etc).
// ============================================================================

class BZBusUI extends UIScriptedMenu {

    static BZBusUI s_Instance;

    private ref BZBusStopInfo m_Info;
    private int               m_ActiveTab;

    private TextWidget              m_BodyText;
    private ButtonWidget            m_CloseBtn;
    private ref array<ButtonWidget> m_TabButtons;
    private ref array<Widget>       m_TabOverlays;

    static const int TAB_COUNT = 3;

    void SetInfo(BZBusStopInfo info) {
        m_Info      = info;
        m_ActiveTab = 0;
        Render();
    }

    override Widget Init() {
        layoutRoot = GetGame().GetWorkspace().CreateWidgets("BZ_AutoDrive/gui/layouts/bus_stop.layout");

        m_BodyText = TextWidget.Cast(layoutRoot.FindAnyWidget("BodyText"));

        m_CloseBtn    = ButtonWidget.Cast(layoutRoot.FindAnyWidget("CloseBtn"));
        m_TabButtons  = new array<ButtonWidget>();
        m_TabOverlays = new array<Widget>();

        for (int i = 0; i < TAB_COUNT; i++) {
            m_TabButtons.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Tab" + i)));
            m_TabOverlays.Insert(layoutRoot.FindAnyWidget("Tab" + i + "_Overlay"));
        }

        s_Instance = this;

        if (BZBusClientManager.s_PendingInfo) {
            SetInfo(BZBusClientManager.s_PendingInfo);
            BZBusClientManager.s_PendingInfo = null;
        }

        return layoutRoot;
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

    override bool OnClick(Widget w, int x, int y, int button) {
        if (w == m_CloseBtn) {
            GetGame().GetUIManager().HideScriptedMenu(this);
            return true;
        }
        for (int i = 0; i < m_TabButtons.Count(); i++) {
            if (w == m_TabButtons[i]) {
                m_ActiveTab = i;
                Render();
                return true;
            }
        }
        return super.OnClick(w, x, y, button);
    }

    void Render() {
        if (!m_Info || !layoutRoot) return;

        for (int i = 0; i < m_TabOverlays.Count(); i++) {
            if (m_TabOverlays[i])
                m_TabOverlays[i].Show(i == m_ActiveTab);
        }

        if (m_BodyText)
            m_BodyText.SetText(BuildTabText(m_ActiveTab));
    }

    private string BuildTabText(int tab) {
        if (!m_Info) return "";

        if (tab == 0) return BuildStatusTab();
        if (tab == 1) return BuildRouteTab();
        if (tab == 2) return BuildWarningsTab();
        return "";
    }

    // i18n: textos via stringtable.csv (#STR_BZAD_*). Widget.TranslateString
    // devuelve el texto en el idioma del CLIENTE (cada jugador ve su idioma).
    // Los valores dinamicos (nombre parada, ETA, distancia) se concatenan.
    private string BuildStatusTab() {
        string t = Widget.TranslateString("#STR_BZAD_ui_title") + "\n\n";
        t += Widget.TranslateString("#STR_BZAD_ui_stop") + "  " + m_Info.stopName + "\n";
        t += Widget.TranslateString("#STR_BZAD_ui_status") + "  " + m_Info.status + "\n\n";

        if (m_Info.etaSeconds <= 0) {
            t += Widget.TranslateString("#STR_BZAD_ui_eta_now") + "\n";
        } else if (m_Info.etaSeconds < 60) {
            t += Widget.TranslateString("#STR_BZAD_ui_eta_soon") + "\n";
        } else {
            int min = m_Info.etaSeconds / 60;
            int sec = m_Info.etaSeconds % 60;
            t += Widget.TranslateString("#STR_BZAD_ui_eta_prefix") + min + " " + Widget.TranslateString("#STR_BZAD_ui_min") + " " + sec + " " + Widget.TranslateString("#STR_BZAD_ui_sec") + "\n";
        }

        if (m_Info.distanceMeters > 0) {
            if (m_Info.distanceMeters < 1000) {
                t += Widget.TranslateString("#STR_BZAD_ui_dist") + " " + (int)m_Info.distanceMeters + " m\n";
            } else {
                float distKm  = m_Info.distanceMeters / 1000.0;
                int   whole   = (int)distKm;
                int   tenths  = (int)((distKm - whole) * 10);
                if (tenths < 0) tenths = -tenths;
                t += Widget.TranslateString("#STR_BZAD_ui_dist") + " " + whole + "." + tenths + " km\n";
            }
        }

        t += "\n" + Widget.TranslateString("#STR_BZAD_ui_close");
        return t;
    }

    private string BuildRouteTab() {
        string t = Widget.TranslateString("#STR_BZAD_ui_next_title") + "\n\n";

        if (m_Info.upcomingStops.Count() == 0) {
            t += Widget.TranslateString("#STR_BZAD_ui_no_route") + "\n";
        } else {
            foreach (string stop : m_Info.upcomingStops) {
                t += "  > " + stop + "\n";
            }
        }

        return t;
    }

    private string BuildWarningsTab() {
        string t = Widget.TranslateString("#STR_BZAD_ui_warn_title") + "\n\n";
        t += Widget.TranslateString("#STR_BZAD_ui_warn_intro") + "\n\n";
        t += "  > " + Widget.TranslateString("#STR_BZAD_ui_warn_1") + "\n\n";
        t += "  > " + Widget.TranslateString("#STR_BZAD_ui_warn_2") + "\n\n";
        t += "  > " + Widget.TranslateString("#STR_BZAD_ui_warn_3") + "\n\n";
        t += "  > " + Widget.TranslateString("#STR_BZAD_ui_warn_4") + "\n\n";
        t += "  > " + Widget.TranslateString("#STR_BZAD_ui_warn_5");
        return t;
    }

    override bool UseKeyboard() { return true; }
    override bool UseMouse()    { return true; }
}
