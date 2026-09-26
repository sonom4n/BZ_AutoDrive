// ============================================================================
//  BZBusCommon - constantes, estructuras y logger compartidos client/server
// ============================================================================

const int MENU_BZ_TRANSPORT    = 51210;
const int MENU_BZ_CONTROL_PANEL = 51212;
const int MENU_BZ_REPRODUCTOR   = 51213;

// ----------------------------------------------------------------------------
//  BZAutoDriveVersion - la version declarada en config.cpp (CfgMods BZ_AutoDrive version), leida del config del
//  juego para que el cartel del boot (y cualquier otro) diga exactamente lo que el mod publica.
// ----------------------------------------------------------------------------
class BZAutoDriveVersion {
    static string Texto() {
        string v = "";
        GetGame().ConfigGetText("CfgMods BZ_AutoDrive version", v);
        if (v == "") return "(version no declarada en config.cpp)";
        return v;
    }
}

// ----------------------------------------------------------------------------
//  BZCarpetas - las carpetas del framework en el profile, CREADAS antes de usarlas.
//  Una instalacion nueva no tiene $profile:BZ_AutoDrive_PathLogger, y un escritor que abre sin crearla falla en
//  cada cuadro: 706 errores por minuto y el server a 1-3 fps (medido en una instalacion desde cero, con un jugador
//  manejando). Todo el que escribe ahi pide la carpeta ACA; MakeDirectory es idempotente y FileExist es barato.
// ----------------------------------------------------------------------------
class BZCarpetas {
    static string PathLogger() {
        string dir = "$profile:BZ_AutoDrive_PathLogger\\";
        if (!FileExist(dir)) MakeDirectory(dir);
        return dir;
    }
    static string Profile() {
        string dir = "$profile:BZ_AutoDrive\\";
        if (!FileExist(dir)) MakeDirectory(dir);
        return dir;
    }
}

// ----------------------------------------------------------------------------
//  BZBusInput - vocabulario de comandos que aplicamos al bus
// ----------------------------------------------------------------------------
//  Pensar como inputs externos: nosotros (admin / mision / script) le decimos
//  al bus que hacer. El bus internamente ejecuta esos comandos en secuencia.
//
//  Vocabulario inicial:
//    NONE  = 0  - sin comando, bus inactivo
//    SPAWN = 1  - crear entidades, sentar driver, esperar HoldSeconds
//    PLAY  = 2  - ejecutar playback del recording
//
//  Mas comandos van a aparecer segun necesidades: PAUSE, STOP, RETURN,
//  WAIT_PLAYER, etc. Se agregan aca y cada uno se documenta en su transicion.
//
//  Cada cambio de comando se logea en RPT y se broadcastea para visibilidad
//  del admin durante experimentos.
// ----------------------------------------------------------------------------
enum BZBusInput {
    NONE  = 0,
    SPAWN = 1,
    PLAY  = 2
}

class BZBusInputName {
    static string Of(int s) {
        if (s == BZBusInput.NONE)  return "NONE";
        if (s == BZBusInput.SPAWN) return "SPAWN";
        if (s == BZBusInput.PLAY)  return "PLAY";
        return "UNKNOWN(" + s + ")";
    }
}

class BZBusStopInfo {
    string stopName;
    string status;       // "en camino" / "en parada aqui" / "en parada en X" / "fuera de servicio"
    int    etaSeconds;
    float  distanceMeters;
    ref array<string> upcomingStops;

    void BZBusStopInfo() {
        upcomingStops = new array<string>();
    }
}

class BZBusLog {
    static bool s_Verbose = false;   // DEBUG on/off en runtime (toggle). Default off (no spamea).
    static void Info(string msg)  { Print("[BZBus] "        + msg); }
    static void Warn(string msg)  { Print("[BZBus][WARN] "  + msg); }
    static void Err(string msg)   { Print("[BZBus][ERROR] " + msg); }
    static void Debug(string msg) { if (s_Verbose) Print("[BZBus][DBG] " + msg); }   // gateado por verbose
    static bool ToggleVerbose()   { s_Verbose = !s_Verbose; Print("[BZBus] DEBUG verbose = " + s_Verbose); return s_Verbose; }
}
