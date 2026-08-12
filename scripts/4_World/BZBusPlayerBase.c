// ============================================================================
//  PlayerBase modded - enruta RPCs del bus
// ============================================================================

modded class PlayerBase {
    // === DIAGNOSTICO de asientos (2026-06-15): loguea el asiento del jugador al cambiar ===
    // Para entender el seating de un vehiculo a mano: spawneas el vehiculo, te metes y te sentas
    // en cada asiento; el RPT registra indice + CrewSize + entrada (CrewEntryWS) + tu posicion.
    private int m_BZ_LastSeat = -99;
    private bool m_BZ_SeatWatchOn = false;
    private string m_BZ_LastDumpType = "";

    override void EEInit() {
        super.EEInit();
        if (GetGame() && GetGame().IsServer() && !m_BZ_SeatWatchOn) {
            m_BZ_SeatWatchOn = true;
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(BZ_SeatWatch, 500, true);
        }
    }

    void BZ_SeatWatch() {
        // Robusto para CUALQUIER vehiculo (vanilla o mod): si el jugador esta en un transport,
        // buscar su asiento iterando la tripulacion (no depender de GetCommand_Vehicle, que no
        // agarra algunos mods como el MAN HX58).
        int seat = -1;
        Transport t = null;
        if (IsInTransport()) {
            t = Transport.Cast(GetParent());
            if (t) {
                for (int i = 0; i < t.CrewSize(); i++) {
                    if (t.CrewMember(i) == this) { seat = i; break; }
                }
            }
        }
        if (seat == m_BZ_LastSeat) return;
        m_BZ_LastSeat = seat;
        if (seat < 0 || !t) return;   // no en un asiento de crew: no spamear

        // Dump COMPLETO de la disposicion DECLARADA (una vez por tipo de vehiculo): con sentarte en
        // UN asiento ya tenes el mapa entero (CrewSize + entrada de cada plaza). El vehiculo se
        // autodescribe via config/modelo; esto solo lo lee. Entradas compartidas (misma pos) = plazas
        // tipo carga, no asientos propios.
        if (t.GetType() != m_BZ_LastDumpType) {
            m_BZ_LastDumpType = t.GetType();
            int cs = t.CrewSize();
            BZBusLog.Info("[SEATS] " + t.GetType() + " declara CrewSize=" + cs + ":");
            for (int s = 0; s < cs; s++) {
                vector sp; vector sd;
                t.CrewEntryWS(s, sp, sd);
                BZBusLog.Info("[SEATS]   seat " + s + " entryWS=" + sp + " | dist_centro=" + vector.Distance(sp, t.GetPosition()).ToString());
            }
        }

        vector ep; vector ed;
        t.CrewEntryWS(seat, ep, ed);
        BZBusLog.Info("[PSEAT] " + t.GetType() + " CrewSize=" + t.CrewSize() + " -> me sente en seat=" + seat + " | entryWS=" + ep + " | miPos=" + GetPosition());
    }

    // Gate de seguridad server-side: las acciones de control del framework (spawn/
    // stop/reset/pause) solo se ejecutan si el sender es admin. Defensa en profundidad
    // (el gate client-side del Control Panel se podria saltear mandando el RPC directo).
    // Lista de admins vacia = todos (modo testing). sender null = "" (host local).
    private bool BZ_CanControl(PlayerIdentity sender) {
        string sid = "";
        if (sender) sid = sender.GetPlainId();
        return BZBusService.GetInstance().IsControlPanelAdmin(sid);
    }

    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx) {
        super.OnRPC(sender, rpc_type, ctx);

        switch (rpc_type) {
            case BZBusRPC.REQUEST_STATUS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleStatusRequest(ctx, sender);
                break;

            case BZBusRPC.RECEIVE_STATUS:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveAndOpen(ctx);
                break;

            case BZBusRPC.REQUEST_RESPAWN:
                if (GetGame().IsServer() && BZ_CanControl(sender))
                    BZBusService.GetInstance().RespawnBus();
                break;

            case BZBusRPC.REQUEST_RESPAWN_SLOT:
                if (GetGame().IsServer()) {
                    int slotIdx = 0;
                    if (ctx.Read(slotIdx) && BZ_CanControl(sender)) {
                        BZBusService.GetInstance().RespawnFromSlot(slotIdx);
                    }
                }
                break;

            case BZBusRPC.REQUEST_RESPAWN_TEST:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().RespawnAs("Hatchback_02_Grey");
                break;

            case BZBusRPC.REQUEST_CONTINUAR:
                if (GetGame().IsServer()) {
                    string contFname = "";
                    int contWp = 0;
                    if (ctx.Read(contFname) && ctx.Read(contWp) && BZ_CanControl(sender)) {
                        BZBusService.GetInstance().ContinuarFromPath(contFname, contWp, PlayerBase.Cast(this));
                    }
                }
                break;

            case BZBusRPC.REQUEST_AI_LOG_TOGGLE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleAILogging();
                break;

            case BZBusRPC.RECEIVE_TOAST:
                if (!GetGame().IsDedicatedServer()) {
                    string bzToastMsg;
                    if (ctx.Read(bzToastMsg)) {
                        NotificationSystem.AddNotificationExtended(6, "BZ_AutoDrive", bzToastMsg, "");
                    }
                }
                break;

            case BZBusRPC.REQUEST_SYSID_STEP:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleSysIDStep();
                break;

            case BZBusRPC.REQUEST_SYSID_CURVE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleSysIDCurve();
                break;

            case BZBusRPC.REQUEST_PAUSE_TOGGLE:
                if (GetGame().IsServer() && BZ_CanControl(sender))
                    BZBusService.GetInstance().TogglePause();
                break;

            case BZBusRPC.REQUEST_RAMP_TOGGLE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleRamp();
                break;

            case BZBusRPC.REQUEST_AI_MARK:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().MarkAIEvent();
                break;

            case BZBusRPC.REQUEST_STOP_BUS:
                if (GetGame().IsServer() && BZ_CanControl(sender))
                    BZBusService.GetInstance().StopBus();
                break;

            case BZBusRPC.REQUEST_PANEL_SETTINGS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandlePanelSettingsRequest(sender);
                break;

            case BZBusRPC.RECEIVE_PANEL_SETTINGS:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceivePanelSettings(ctx);
                break;

            case BZBusRPC.REQUEST_PANEL_STATUS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandlePanelStatusRequest(sender);
                break;

            case BZBusRPC.RECEIVE_PANEL_STATUS:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceivePanelStatus(ctx);
                break;

            case BZBusRPC.REQUEST_ROUTE_LIST:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleRouteListRequest(sender);
                break;

            case BZBusRPC.RECEIVE_ROUTE_LIST:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveRouteList(ctx);
                break;

            case BZBusRPC.REQUEST_ROUTE_INFO:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleRouteInfoRequest(ctx, sender);
                break;

            case BZBusRPC.RECEIVE_ROUTE_INFO:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveRouteInfo(ctx);
                break;

            case BZBusRPC.REQUEST_LOAD_ROUTE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleLoadRouteRequest(ctx, sender);
                break;

            case BZBusRPC.REQUEST_TELEPORT_RUNNER:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleTeleportToRunner(sender);
                break;

            case BZBusRPC.REQUEST_RUNNERS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleRunnersRequest(sender);
                break;

            case BZBusRPC.RECEIVE_RUNNERS:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveRunners(ctx);
                break;

            case BZBusRPC.REQUEST_RUNNER_CTL:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleRunnerCtl(ctx, sender);
                break;

            case BZBusRPC.REQUEST_STOP_ALL:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleStopAll(sender);
                break;

            case BZBusRPC.REQUEST_EMPTY_LIST:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleEmptyListRequest(sender);
                break;

            case BZBusRPC.RECEIVE_EMPTY_LIST:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveEmptyList(ctx);
                break;

            case BZBusRPC.REQUEST_EMPTY_CTL:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleEmptyCtl(ctx, sender);
                break;
        }
    }
}
