// ============================================================================
//  MissionGameplay modded (cliente)
//  F cerca de una parada (anchor en bus_stops.json) -> pide estado del bus al
//  servidor y abre la UI. No depende de objetos spawneados: solo coord+radio.
// ============================================================================

modded class MissionGameplay {

    // 2026-06-09: track CTRL state for multi-slot bindings.
    // CTRL+NUMPAD 2/3/8/9 = spawn from slot file (test A/B/C/D).
    static bool s_BZ_CtrlHeld = false;
    private float m_DbgRefreshTimer = 0;   // debugger overlay (capa 2): refresh ~3Hz

    override void OnKeyRelease(int key) {
        super.OnKeyRelease(key);
        if (key == KeyCode.KC_LCONTROL || key == KeyCode.KC_RCONTROL) {
            s_BZ_CtrlHeld = false;
        }
    }

    override UIScriptedMenu CreateScriptedMenu(int id) {
        UIScriptedMenu menu = super.CreateScriptedMenu(id);
        if (!menu && id == MENU_BZ_TRANSPORT)
            menu = new BZBusUI();
        if (!menu && id == MENU_BZ_CONTROL_PANEL)
            menu = new BZControlPanelUI();
        if (!menu && id == MENU_BZ_REPRODUCTOR)
            menu = new BZReproductorUI();
        return menu;
    }

    // Helper: true el frame que se presiona el input (rebindeable desde Controles).
    // Admin tooling -> pollear ~20 inputs/frame es aceptable (no es hot path de gameplay).
    private bool BZIn(string name) {
        UAInput inp = GetUApi().GetInputByName(name);
        return inp && inp.LocalPress();
    }

    override void OnUpdate(float timeslice) {
        super.OnUpdate(timeslice);

        // Control Panel: pedir al server (una vez) la tecla + flag admin de este
        // cliente. NO leemos el config client-side; el server responde lo minimo.
        if (!BZBusClientManager.s_PanelSettingsRequested && GetGame().GetPlayer()) {
            BZBusClientManager.s_PanelSettingsRequested = true;
            BZBusClientManager.RequestPanelSettings();
        }

        // === Keybinds del framework via input system (seccion "BZ AutoDrive", rebindeables) ===
        // GUARD: NO pollear si hay un MENU/dialogo abierto (incl. Controles/rebinding) -> sino las
        // acciones se disparaban al reasignar una tecla. GetMenu() devuelve el UIScriptedMenu top
        // (la pantalla de Controles ES uno) -> non-null = menu abierto. Es el patron del engine
        // (missiongameplay.c envuelve el input en if(menu==NULL)) + CF + COT. HasGameFocus NO
        // servia (era foco de ventana del SO). Y solo admin; RPCs igual validados server-side.
        // TOGGLE del Reproductor con la MISMA tecla/combo que lo abre (action UABZAutoDrivePanel,
        // rebindeable; default INICIO/HOME). Va FUERA del guard de "menu abierto" porque cuando el
        // Reproductor esta abierto GetMenu() es non-null y el bloque de abajo no corre -> no podria
        // cerrarse con la tecla. Logica: si ya esta abierto -> cerrar (HideScriptedMenu); si no, y no
        // hay otro menu/dialogo abierto y es admin -> abrir. Asi misma tecla abre y cierra.
        // El "abrir" mantiene el guard (no abrir desde la pantalla de Controles/rebinding ni otro menu).
        if (BZBusClientManager.s_IsControlPanelAdmin && BZIn("UABZAutoDrivePanel")) {
            if (BZReproductorUI.s_Instance) {
                GetGame().GetUIManager().HideScriptedMenu(BZReproductorUI.s_Instance);
            } else if (!GetGame().GetUIManager().GetMenu() && !GetGame().GetUIManager().IsDialogVisible()) {
                GetGame().GetUIManager().EnterScriptedMenu(MENU_BZ_REPRODUCTOR, null);
            }
        }

        if (!GetGame().GetUIManager().GetMenu() && !GetGame().GetUIManager().IsDialogVisible() && BZBusClientManager.s_IsControlPanelAdmin) {
            if (BZIn("UABZRespawnBus"))  BZBusClientManager.RequestRespawn();
            if (BZIn("UABZStopBus"))     BZBusClientManager.RequestStopBus();
            if (BZIn("UABZRecord"))      BZPathLogService.GetInstance().ToggleStartStop();
            if (BZIn("UABZRecordPause")) BZPathLogService.GetInstance().TogglePause();
            if (BZIn("UABZMarkStop")) {
                // NUMPAD 4 = marcador de EVENTO/parada UNIVERSAL: marca la parada en el path logger
                // Y (si el frame recorder esta grabando) el evento en el frame_ -> los eventos caen en
                // AMBOS grabadores, sea cual sea el que convertis (path_ via wizard o frame_ via converter).
                // Antes solo marcaba el path -> en el workflow frame->ruta los eventos se perdian.
                BZPathLogService.GetInstance().MarkStop();
                if (BZFrameRecorder.GetInstance().IsActive())
                    BZFrameRecorder.GetInstance().MarkNextFrame();
            }
            if (BZIn("UABZParking"))     BZPathLogService.GetInstance().ToggleParkingMode();
            if (BZIn("UABZReverse"))     BZPathLogService.GetInstance().ToggleReverseMode();
            if (BZIn("UABZManiobra"))    BZPathLogService.GetInstance().ToggleManiobraMode();
            if (BZIn("UABZApproach"))    BZPathLogService.GetInstance().ToggleApproachMode();
            if (BZIn("UABZPauseBus"))    BZBusClientManager.RequestPauseToggle();
            if (BZIn("UABZAILogger"))    BZBusClientManager.RequestAILoggingToggle();
            if (BZIn("UABZAIMark")) {
                // NUMPAD 4 universal: marca la corrida de Boris (AI logger, via RPC) Y,
                // si esta grabando una toma humana, el frame_ local (importable al editor).
                BZBusClientManager.RequestAIMark();
                if (BZFrameRecorder.GetInstance().IsActive())
                    BZFrameRecorder.GetInstance().MarkNextFrame();
            }
            // INTERCAMBIO: ya NO es tecla (sacamos NUMPAD 3, 2026-08-11). Se AUTO-DETECTA del cambio de
            // SENTIDO (gear forward<->reverse) al convertir -> frame_to_route.py pone el legBreak solo (vel 0
            // + cambio de sentido = intercambio; un 0 sin cambio de sentido = pausa). El pure-pursuit de
            // reversa hace la maniobra confiable sin declararla. (MarkNextFrameLegBreak queda en el recorder
            // por si un modder reactiva un input manual.)
            if (BZIn("UABZMarkShift"))   BZPathLogService.GetInstance().MarkShiftPoint();
            if (BZIn("UABZMarkMax"))     BZPathLogService.GetInstance().MarkMaxSustained();
            if (BZIn("UABZSysIDStep"))   BZBusClientManager.RequestSysIDStep();
            if (BZIn("UABZSysIDCurve"))  BZBusClientManager.RequestSysIDCurve();
            if (BZIn("UABZDriveTest"))   BZBusClientManager.RequestRespawnTest();   // v1.1 paso 3: rutea+maneja (NUMPAD 0)
            if (BZIn("UABZSlot1"))       BZBusClientManager.RequestRespawnSlot(1);
            if (BZIn("UABZSlot2"))       BZBusClientManager.RequestRespawnSlot(2);
            if (BZIn("UABZSlot3"))       BZBusClientManager.RequestRespawnSlot(3);
            if (BZIn("UABZSlot4"))       BZBusClientManager.RequestRespawnSlot(4);
        }

        // DEBUGGER overlay (capa 2): si esta visible, pull de runners + refresh ~3Hz
        // (anda aunque el panel modal este cerrado -> podes volar mirando los buses).
        if (BZDebugOverlay.Get().IsVisible()) {
            m_DbgRefreshTimer += timeslice;
            if (m_DbgRefreshTimer >= 0.33) {
                m_DbgRefreshTimer = 0;
                BZBusClientManager.RequestRunners();
                BZDebugOverlay.Get().Refresh();
            }
        }

    }

    override void OnKeyPress(int key) {
        // 2026-06-09: track CTRL state para multi-slot
        if (key == KeyCode.KC_LCONTROL || key == KeyCode.KC_RCONTROL) {
            s_BZ_CtrlHeld = true;
        }

        // Cerrar el menu con ESC o F si esta abierto
        if (BZBusUI.s_Instance && (key == KeyCode.KC_ESCAPE || key == KeyCode.KC_F)) {
            GetGame().GetUIManager().HideScriptedMenu(BZBusUI.s_Instance);
            super.OnKeyPress(key);
            return;
        }

        // Cerrar el Control Panel con ESC o su propia tecla si esta abierto
        if (BZControlPanelUI.s_Instance && (key == KeyCode.KC_ESCAPE || key == BZBusClientManager.s_ControlPanelKey)) {
            GetGame().GetUIManager().HideScriptedMenu(BZControlPanelUI.s_Instance);
            super.OnKeyPress(key);
            return;
        }

        // Cerrar el Reproductor con ESC si esta abierto (el abrir es por el input system en OnUpdate)
        if (BZReproductorUI.s_Instance && key == KeyCode.KC_ESCAPE) {
            GetGame().GetUIManager().HideScriptedMenu(BZReproductorUI.s_Instance);
            super.OnKeyPress(key);
            return;
        }

        // El panel (Reproductor) ahora se abre por el INPUT SYSTEM en OnUpdate
        // (UABZAutoDrivePanel, rebindeable). El viejo BZControlPanelUI quedo desconectado.

        // 2026-08-01 (Sonom4n): REMOVIDO el layer de parada. Antes F cerca de una parada abria BZBusUI
        // (bus_stop.layout) via RequestStatus. Era del transport viejo, ya no se usa -> F queda libre.

        // === Los keybinds (NUMPAD) se manejan ahora por el INPUT SYSTEM en OnUpdate ===
        // (acciones UABZ*, seccion "BZ AutoDrive" en Controles, rebindeables). Aca en
        // OnKeyPress solo quedan el tracking de CTRL (arriba) y el cierre de menus (ESC).

        super.OnKeyPress(key);
    }
}
