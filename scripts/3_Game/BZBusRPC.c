// ============================================================================
//  BZBusRPC - IDs de RPC cliente <-> servidor
//  Rango: 32410+ (no colisiona con BZInfoBoardRPC que usa 32400+)
// ============================================================================

enum BZBusRPC {
    INVALID         = 32410,
    REQUEST_STATUS,     // Cliente -> Servidor: pide estado del bus para una parada
    RECEIVE_STATUS,     // Servidor -> Cliente: entrega estado + ETA
    REQUEST_RESPAWN,    // Cliente -> Servidor: respawn manual del bus (debug/admin)
    REQUEST_RESPAWN_TEST, // Cliente -> Servidor: respawn con vehiculo test (Hatchback) - debug
    REQUEST_AI_LOG_TOGGLE, // Cliente -> Servidor: toggle AI logging (graba trayectoria del bus al CSV para comparar con grabacion humana)
    REQUEST_SYSID_STEP,  // Cliente -> Servidor: experimento System ID 1 - step response del throttle
    REQUEST_SYSID_CURVE, // Cliente -> Servidor: experimento System ID 2 - curva de respuesta (throttle escalonado)
    REQUEST_PAUSE_TOGGLE, // Cliente -> Servidor: pausa/reanuda la ruta del bus (para teleport + experimentos controlados)
    REQUEST_RAMP_TOGGLE,  // Cliente -> Servidor: spawnea/borra rampa de test en posicion hardcoded de Vybor
    REQUEST_AI_MARK,      // Cliente -> Servidor: marca evento en el AI logger CSV (parada visible, choque, etc.)
    REQUEST_STOP_BUS,     // Cliente -> Servidor: detiene y limpia el bus sin respawnear (manual stop)
    REQUEST_RESPAWN_SLOT, // Cliente -> Servidor: respawn cargando BZBusRoute_slotN.json (multi-slot test)
    REQUEST_PANEL_SETTINGS, // Cliente -> Servidor: pide su tecla + flag admin para el Control Panel
    RECEIVE_PANEL_SETTINGS, // Servidor -> Cliente: entrega SOLO (controlPanelKey, isAdmin del sender). NUNCA la lista de admins.
    REQUEST_PANEL_STATUS,   // Cliente -> Servidor: pide telemetria viva del bus (panel abierto, ~3Hz). Solo admin.
    RECEIVE_PANEL_STATUS,   // Servidor -> Cliente: telemetria (active, paused, wpIdx, wpTotal, kmh, mode, arCount).
    REQUEST_ROUTE_LIST,     // Cliente -> Servidor: pide la lista de rutas del profile (reproductor). Solo admin.
    RECEIVE_ROUTE_LIST,     // Servidor -> Cliente: nombres de archivo de las rutas disponibles.
    REQUEST_LOAD_ROUTE,     // Cliente -> Servidor: carga + spawnea una ruta por nombre de archivo. Solo admin.
    REQUEST_TELEPORT_RUNNER, // Cliente -> Servidor: teleporta al admin cerca del runner (para interceptar). Solo admin.
    REQUEST_RUNNERS,         // Cliente -> Servidor: pide el snapshot de TODOS los runners activos (panel ~3Hz). Solo admin.
    RECEIVE_RUNNERS,         // Servidor -> Cliente: array de runners {name, origin, active, paused, wpIdx, wpTotal, kmh}.
    REQUEST_RUNNER_CTL,      // Cliente -> Servidor: accion sobre un runner por indice (0=stop 1=pause 2=teleport). Solo admin.
    REQUEST_STOP_ALL,        // Cliente -> Servidor: para TODOS los runners (limpieza). Solo admin.
    RECEIVE_TOAST,           // Servidor -> Cliente: mensaje/notificacion en pantalla (lo usa BroadcastGlobal)
    REQUEST_CONTINUAR,       // Cliente -> Servidor: spawnea el vehiculo de la toma (fname) VACIO en el wp <wpIndex> + teleporta al player (continuar/regrabar; wpIndex del scrubber, -1=endpoint)
    REQUEST_ROUTE_INFO,      // Cliente -> Servidor: pide info de una toma (fname) para el preview. Solo admin.
    RECEIVE_ROUTE_INFO,      // Servidor -> Cliente: (fname, vehiculo, N wps, distancia m, vel max km/h) para el preview del reproductor.
    REQUEST_EMPTY_LIST,      // Cliente -> Servidor: pide el snapshot de vehiculos spawneados VACIOS (panel Vacios, ~3Hz). Solo admin.
    RECEIVE_EMPTY_LIST,      // Servidor -> Cliente: array de vacios {routeName, wpIndex, posX, posZ}.
    REQUEST_EMPTY_CTL        // Cliente -> Servidor: accion sobre un vacio por indice (0=teleport player, 1=eliminar). Solo admin.
}
