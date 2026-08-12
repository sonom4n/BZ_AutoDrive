// ============================================================================
//  BZDebugOverlay - HUD no-modal del debugger (capa 2).
//  Muestra el estado vivo de cada runner en pantalla SIN robar foco (no es un
//  UIScriptedMenu). Se crea con CreateWidgets + Show(true/false); MissionGameplay
//  lo refresca en su OnUpdate (pull de RequestRunners + Refresh) mientras este visible.
//  Toggle: boton DBG del reproductor (admin). El overlay persiste al cerrar el panel.
// ============================================================================

class BZDebugOverlay {
    static ref BZDebugOverlay s_Instance;

    private Widget     m_Root;
    private TextWidget m_Text;
    private bool       m_Visible = false;

    static BZDebugOverlay Get() {
        if (!s_Instance) s_Instance = new BZDebugOverlay();
        return s_Instance;
    }

    bool IsVisible() { return m_Visible; }

    void Toggle() {
        m_Visible = !m_Visible;
        if (m_Visible) {
            if (!m_Root) {
                m_Root = GetGame().GetWorkspace().CreateWidgets("BZ_AutoDrive/gui/layouts/debug_overlay.layout");
                if (m_Root) m_Text = TextWidget.Cast(m_Root.FindAnyWidget("DbgText"));
            }
            if (m_Root) m_Root.Show(true);
            BZBusClientManager.RequestRunners();   // pull inmediato al abrir
            Refresh();
        } else {
            if (m_Root) m_Root.Show(false);
        }
    }

    // Reconstruye el texto del overlay desde el snapshot multi-runner. Llamado ~3Hz por MissionGameplay.
    void Refresh() {
        if (!m_Visible || !m_Text) return;
        array<ref BZRunnerInfo> rs = BZBusClientManager.s_RunnersInfo;
        string s = "";
        if (!rs || rs.Count() == 0) {
            s = "(sin runners activos)";
        } else {
            for (int i = 0; i < rs.Count(); i++) {
                BZRunnerInfo r = rs.Get(i);
                if (!r) continue;
                string st = "STOP";
                if (r.paused) st = "PAUSED";
                else if (r.kmh > 1.0) st = "DRIVING";
                int kmh = (int)r.kmh;
                string line = i.ToString() + ": " + r.name;
                line = line + "  [" + r.origin + "]  " + st;
                line = line + "\n   wp " + r.wpIdx + "/" + r.wpTotal + "  |  " + kmh + " km/h";
                s = s + line + "\n";
            }
        }
        m_Text.SetText(s);
    }
}
