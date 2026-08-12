// ============================================================================
//  BZBusClientManager (3_Game para que 5_Mission lo vea)
//  Cliente: envía REQUEST_STATUS, recibe RECEIVE_STATUS y abre la UI.
// ============================================================================

// Snapshot client-side de un runner para las cards del reproductor (multi-instancia).
class BZRunnerInfo {
    string name;
    string origin;
    bool   active;
    bool   paused;
    int    wpIdx;
    int    wpTotal;
    float  kmh;
}

// Fase 2: snapshot client-side de un vehiculo spawneado VACIO (panel ACTIVE SPAWN VEHICLE).
class BZEmptyInfo {
    string routeName;
    int    wpIndex;
    float  posX;
    float  posZ;
}

class BZBusClientManager {

    static ref BZBusStopInfo s_PendingInfo;

    // Reproductor multi: snapshot de todos los runners activos (recibido del server ~3Hz).
    static ref array<ref BZRunnerInfo> s_RunnersInfo;
    static bool s_RunnersReceived = false;

    // Fase 2: snapshot de vehiculos vacios spawneados (recibido del server ~3Hz).
    static ref array<ref BZEmptyInfo> s_EmptyInfo;
    static bool s_EmptyReceived = false;

    // PERF: medidor de consumo del framework (footer de la UI). Llega en el mismo RPC que los runners.
    static float s_ServerFps   = 30;   // FPS del server (suavizado, server-side)
    static float s_FrameworkMs = 0;    // ms/frame que consume BZ_AutoDrive (sum runners, promediado)

    // Control Panel: estado sincronizado desde el server (NO leemos config client-side).
    // s_ControlPanelKey = -999 hasta que llega el sync. s_IsAdmin gobierna si se abre.
    static int  s_ControlPanelKey = -999;
    static bool s_IsControlPanelAdmin = false;
    static bool s_PanelSettingsRequested = false;

    // Control Panel v1.1: telemetria viva recibida del server (panel abierto).
    // s_PanelStatusValid = false hasta el primer RECEIVE_PANEL_STATUS.
    static bool   s_PanelStatusValid = false;
    static bool   s_PanelActive  = false;
    static bool   s_PanelPaused  = false;
    static int    s_PanelWpIdx   = 0;
    static int    s_PanelWpTotal = 0;
    static float  s_PanelKmh     = 0;
    static string s_PanelMode    = "";
    static int    s_PanelAR      = 0;

    // Cliente: pide al server su tecla + flag admin (una vez, cuando esta listo).
    static void RequestPanelSettings() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_PANEL_SETTINGS, true, null);
    }

    // Cliente: recibe SOLO (key, isAdmin). Nunca la lista de admins.
    static void OnReceivePanelSettings(ParamsReadContext ctx) {
        int key;
        bool isAdmin;
        if (!ctx.Read(key)) return;
        if (!ctx.Read(isAdmin)) return;
        s_ControlPanelKey = key;
        s_IsControlPanelAdmin = isAdmin;
        BZBusLog.Info("[Panel] Settings recibidos: key=" + key + " admin=" + isAdmin);
    }

    // Cliente: pide telemetria viva del bus (panel abierto, ~3Hz desde Update()).
    static void RequestPanelStatus() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_PANEL_STATUS, true, null);
    }

    // Cliente: recibe el snapshot de telemetria y lo guarda en statics. El panel
    // lee estos campos en su Update() para refrescar el display.
    static void OnReceivePanelStatus(ParamsReadContext ctx) {
        bool active; bool paused; int wpIdx; int wpTotal; float kmh; string mode; int arCount;
        if (!ctx.Read(active))  return;
        if (!ctx.Read(paused))  return;
        if (!ctx.Read(wpIdx))   return;
        if (!ctx.Read(wpTotal)) return;
        if (!ctx.Read(kmh))     return;
        if (!ctx.Read(mode))    return;
        if (!ctx.Read(arCount)) return;
        s_PanelActive  = active;
        s_PanelPaused  = paused;
        s_PanelWpIdx   = wpIdx;
        s_PanelWpTotal = wpTotal;
        s_PanelKmh     = kmh;
        s_PanelMode    = mode;
        s_PanelAR      = arCount;
        s_PanelStatusValid = true;
    }

    // Reproductor: lista de rutas recibida del server (nombres de archivo). El panel la lee.
    static ref array<string> s_RouteList;
    static bool s_RouteListReceived = false;

    // Cliente: pide al server la lista de rutas del profile (panel abierto). Solo admin server-side.
    static void RequestRouteList() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_ROUTE_LIST, true, null);
    }

    // Cliente: recibe los nombres de archivo y los guarda para que el panel puebla las filas.
    static void OnReceiveRouteList(ParamsReadContext ctx) {
        int count;
        if (!ctx.Read(count)) return;
        if (!s_RouteList) s_RouteList = new array<string>();
        s_RouteList.Clear();
        for (int i = 0; i < count; i++) {
            string name;
            if (!ctx.Read(name)) break;
            s_RouteList.Insert(name);
        }
        s_RouteListReceived = true;
        BZBusLog.Info("[RouteList] cliente recibio " + s_RouteList.Count() + " rutas");
    }

    // --- INFO de toma para el PREVIEW del reproductor (paso 1 del scrubber) ---
    static string s_RIFname = "";
    static string s_RIVehicle = "";
    static int    s_RINwp = 0;
    static float  s_RIDist = 0;
    static float  s_RIMaxkmh = 0;
    static int    s_RIVer = 0;   // se incrementa en cada recepcion -> el reproductor detecta el cambio en su Update

    static void RequestRouteInfo(string fname) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(fname);
        rpc.Send(player, BZBusRPC.REQUEST_ROUTE_INFO, true, null);
    }

    static void OnReceiveRouteInfo(ParamsReadContext ctx) {
        string fname;
        string veh;
        int nwp;
        float dist;
        float maxkmh;
        if (!ctx.Read(fname)) return;
        if (!ctx.Read(veh)) return;
        if (!ctx.Read(nwp)) return;
        if (!ctx.Read(dist)) return;
        if (!ctx.Read(maxkmh)) return;
        s_RIFname = fname;
        s_RIVehicle = veh;
        s_RINwp = nwp;
        s_RIDist = dist;
        s_RIMaxkmh = maxkmh;
        s_RIVer++;
    }

    // Cliente: pide cargar + spawnear una ruta por nombre de archivo. Server re-valida admin.
    static void RequestLoadRoute(string fname, bool logNative = false, bool logAi = false) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(fname);
        rpc.Write(logNative);   // check "Log boris_native" del reproductor
        rpc.Write(logAi);       // check "Log ai_run" del reproductor
        rpc.Send(player, BZBusRPC.REQUEST_LOAD_ROUTE, true, null);
    }

    // Cliente: pide teleportar al admin cerca del runner (para interceptar). Server re-valida admin.
    static void RequestTeleportToRunner() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_TELEPORT_RUNNER, true, null);
    }

    // Cliente: pide el snapshot de todos los runners (panel abierto, ~3Hz).
    static void RequestRunners() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RUNNERS, true, null);
    }

    // Cliente: accion sobre un runner por indice. action: 0=stop(despawn) 1=pause-toggle 2=teleport.
    static void RequestRunnerCtl(int runnerIdx, int action) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(runnerIdx);
        rpc.Write(action);
        rpc.Send(player, BZBusRPC.REQUEST_RUNNER_CTL, true, null);
    }

    // Cliente: pide parar TODOS los runners (limpieza del test de techo). Server re-valida admin.
    static void RequestStopAll() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_STOP_ALL, true, null);
    }

    // Cliente: recibe el array de runners y lo guarda para que el panel arme las cards.
    static void OnReceiveRunners(ParamsReadContext ctx) {
        int count;
        if (!ctx.Read(count)) return;
        // PERF: server FPS + ms/frame del framework van JUSTO despues del count (footer de la UI).
        float fps; float fwMs;
        if (!ctx.Read(fps))  return;
        if (!ctx.Read(fwMs)) return;
        s_ServerFps   = fps;
        s_FrameworkMs = fwMs;
        if (!s_RunnersInfo) s_RunnersInfo = new array<ref BZRunnerInfo>();
        s_RunnersInfo.Clear();
        for (int i = 0; i < count; i++) {
            BZRunnerInfo ri = new BZRunnerInfo();
            if (!ctx.Read(ri.name))    break;
            if (!ctx.Read(ri.origin))  break;
            if (!ctx.Read(ri.active))  break;
            if (!ctx.Read(ri.paused))  break;
            if (!ctx.Read(ri.wpIdx))   break;
            if (!ctx.Read(ri.wpTotal)) break;
            if (!ctx.Read(ri.kmh))     break;
            s_RunnersInfo.Insert(ri);
        }
        s_RunnersReceived = true;
    }

    // ===== Fase 2: panel ACTIVE SPAWN VEHICLE (vehiculos vacios) =====
    // Cliente: pide el snapshot de vacios. Server re-valida admin.
    static void RequestEmptyList() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_EMPTY_LIST, true, null);
    }

    // Cliente: accion sobre un vacio por indice. action: 0=TP player al lado, 1=eliminar.
    static void RequestEmptyCtl(int emptyIdx, int action) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(emptyIdx);
        rpc.Write(action);
        rpc.Send(player, BZBusRPC.REQUEST_EMPTY_CTL, true, null);
    }

    // Cliente: recibe el array de vacios y lo guarda para que el panel arme las filas.
    static void OnReceiveEmptyList(ParamsReadContext ctx) {
        int count;
        if (!ctx.Read(count)) return;
        if (!s_EmptyInfo) s_EmptyInfo = new array<ref BZEmptyInfo>();
        s_EmptyInfo.Clear();
        for (int i = 0; i < count; i++) {
            BZEmptyInfo ei = new BZEmptyInfo();
            if (!ctx.Read(ei.routeName)) break;
            if (!ctx.Read(ei.wpIndex))   break;
            if (!ctx.Read(ei.posX))      break;
            if (!ctx.Read(ei.posZ))      break;
            s_EmptyInfo.Insert(ei);
        }
        s_EmptyReceived = true;
    }

    // Cliente: pide estado del bus mandando la posición del cartel
    // El servidor matchea la posición con el waypoint más cercano
    static void RequestStatus(vector signPos) {
        Man player = GetGame().GetPlayer();
        if (!player) return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(signPos);
        rpc.Send(player, BZBusRPC.REQUEST_STATUS, true, null);
        BZBusLog.Info("RequestStatus pos: " + signPos.ToString());
    }

    // Cliente: pide al servidor respawnear el bus (debug)
    static void RequestRespawn() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RESPAWN, true, null);
        BZBusLog.Info("RequestRespawn enviado");
    }

    // 2026-06-09: respawn cargando BZBusRoute_slotN.json en runtime.
    // Permite multi-slot test (CTRL+NUMPAD 2/3/8/9) sin restart server.
    static void RequestRespawnSlot(int slot) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(slot);
        rpc.Send(player, BZBusRPC.REQUEST_RESPAWN_SLOT, true, null);
        BZBusLog.Info("RequestRespawnSlot enviado: slot=" + slot);
    }

    // 2026-07-31: CONTINUAR GRABACION. Spawnea el vehiculo de la toma (por nombre de archivo, como el reproductor)
    // VACIO en su endpoint + teleporta al player ahi, para grabar la continuacion de una ruta larga en otra sesion.
    static void RequestContinuar(string fname, int wpIndex) {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(fname);
        rpc.Write(wpIndex);
        rpc.Send(player, BZBusRPC.REQUEST_CONTINUAR, true, null);
        BZBusLog.Info("RequestContinuar enviado: " + fname + " wp" + wpIndex);
    }

    // Cliente: pide al servidor detener y limpiar el bus sin respawnear
    static void RequestStopBus() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_STOP_BUS, true, null);
        BZBusLog.Info("RequestStopBus enviado");
    }

    // Cliente: pide respawn con vehiculo de test (Hatchback) - debug shift problem
    static void RequestRespawnTest() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RESPAWN_TEST, true, null);
        BZBusLog.Info("RequestRespawnTest enviado");
    }

    // Cliente: pide al servidor toggle de AI logging (graba trayectoria del bus)
    static void RequestAILoggingToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_AI_LOG_TOGGLE, true, null);
        BZBusLog.Info("RequestAILoggingToggle enviado");
    }

    // Cliente: arranca/para experimento System ID 1 (step response del throttle)
    static void RequestSysIDStep() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_SYSID_STEP, true, null);
        BZBusLog.Info("RequestSysIDStep enviado");
    }

    // Cliente: arranca/para experimento System ID 2 (curva de respuesta del throttle)
    static void RequestSysIDCurve() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_SYSID_CURVE, true, null);
        BZBusLog.Info("RequestSysIDCurve enviado");
    }

    // Cliente: pausa/reanuda la ruta del bus (para teleport + setup de experimentos)
    static void RequestPauseToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_PAUSE_TOGGLE, true, null);
        BZBusLog.Info("RequestPauseToggle enviado");
    }

    // Cliente: toggle spawn/borrado de rampa de test en Vybor (System ID experiments)
    static void RequestRampToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RAMP_TOGGLE, true, null);
        BZBusLog.Info("RequestRampToggle enviado");
    }

    // Cliente: marca evento en el AI logger CSV (la proxima muestra tendra is_marker=1).
    // El marcado del FrameRecorder (grabacion humana) se hace en BZBusMissionGameplay
    // (5_Mission), que SI ve BZFrameRecorder (4_World); desde aca (3_Game) no es visible.
    static void RequestAIMark() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_AI_MARK, true, null);
        BZBusLog.Info("RequestAIMark enviado");
    }

    // Cliente: recibe la respuesta del servidor y abre la UI
    static void OnReceiveAndOpen(ParamsReadContext ctx) {
        BZBusStopInfo info = Deserialize(ctx);
        if (!info) return;

        s_PendingInfo = info;
        GetGame().GetUIManager().EnterScriptedMenu(MENU_BZ_TRANSPORT, null);
    }

    static BZBusStopInfo Deserialize(ParamsReadContext ctx) {
        BZBusStopInfo info = new BZBusStopInfo();
        int stopCount;

        if (!ctx.Read(info.stopName))      { BZBusLog.Err("RX: stopName");    return null; }
        if (!ctx.Read(info.status))        { BZBusLog.Err("RX: status");      return null; }
        if (!ctx.Read(info.etaSeconds))    { BZBusLog.Err("RX: eta");         return null; }
        if (!ctx.Read(info.distanceMeters)){ BZBusLog.Err("RX: distance");    return null; }
        if (!ctx.Read(stopCount))          { BZBusLog.Err("RX: stopCount");   return null; }

        for (int i = 0; i < stopCount; i++) {
            string s;
            if (!ctx.Read(s)) break;
            info.upcomingStops.Insert(s);
        }

        return info;
    }
}
