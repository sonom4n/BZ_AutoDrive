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

    // ---- listar archivos del perfil -------------------------------------------------------------------------
    // En DayZ 1.30, FindFile("$profile:...") resuelve "$profile:" a la RAIZ del server (lista las carpetas de la
    // raiz; todo lo de profiles\ devuelve un handle nulo), mientras OpenFile y FileExist siguen yendo a profiles\.
    // Medido el 27/09 con -profiles relativo y absoluto. En la 1.29 el patron con "$profile:" anda. Por eso se
    // prueba en orden: (a) "$profile:<sub>\<pat>", (a') "$profile:<sub>/<pat>", (b) el -profiles RELATIVO de la
    // linea de comandos + "/<sub>/<pat>" (desde la raiz, con "/": la barra invertida en la subruta tambien falla).
    // Con -profiles ABSOLUTO la 1.30 no puede listar: queda un aviso pendiente (una vez por boot) que el servicio
    // manda al RPT y a los admins. Los nombres se filtran por el prefijo y el sufijo del patron (lo que rodea al "*"),
    // asi un comodin ancho tambien sirve. El log dice que patron anduvo, una vez.
    private static string s_PatronPerfil = "";
    private static string s_AvisoPerfil = "";
    private static bool s_AvisoPerfilDado = false;

    static array<string> ListarPerfil(string sub, string patron) {
        array<string> res = new array<string>();
        string pa = "$profile:" + sub + "\\" + patron;
        if (Recoger(pa, patron, res)) return res;
        string pa2 = "$profile:" + sub + "/" + patron;
        if (Recoger(pa2, patron, res)) return res;
        string p = "";
        bool hay = GetCLIParam("profiles", p);
        bool absoluta = false;
        if (hay && p != "") {
            if (p.Contains(":") || p.IndexOf("\\") == 0 || p.IndexOf("/") == 0) absoluta = true;
            if (!absoluta) {
                p.Replace("\\", "/");
                string pb = p + "/" + sub + "/" + patron;
                if (Recoger(pb, patron, res)) return res;
            }
        }
        if (!s_AvisoPerfilDado) {
            s_AvisoPerfilDado = true;
            if (absoluta) {
                s_AvisoPerfil = "DayZ 1.30 cannot list files under an absolute -profiles path: use a relative -profiles (e.g. -profiles=profiles) or routes will not be listed";
            } else {
                s_AvisoPerfil = "no files found under $profile:" + sub + " with any listing pattern (DayZ 1.30 lists the server root for $profile:); routes will not be listed";
            }
            BZBusLog.Warn("[BZ AutoDrive] " + s_AvisoPerfil);
        }
        return res;
    }

    // false si algun listado del perfil no pudo hacerse en este boot (el aviso ya salio)
    static bool PerfilListable() {
        return !s_AvisoPerfilDado;
    }

    // el aviso pendiente del listado, una sola vez; "" si no hay
    static string TomarAvisoPerfil() {
        string a = s_AvisoPerfil;
        s_AvisoPerfil = "";
        return a;
    }

    // un patron: recoge los nombres que cumplen el prefijo y el sufijo; true si encontro alguno
    private static bool Recoger(string pat, string patron, array<string> res) {
        string pref = patron;
        string suf = "";
        int k = patron.IndexOf("*");
        if (k >= 0) {
            pref = patron.Substring(0, k);
            suf = patron.Substring(k + 1, patron.Length() - k - 1);
        }
        string nombre;
        FileAttr attr;
        FindFileHandle fh = FindFile(pat, nombre, attr, FindFileFlags.ALL);
        if (!fh) return false;
        int antes = res.Count();
        int guarda = 0;
        while (guarda < 2000) {
            guarda = guarda + 1;
            if (Cumple(nombre, pref, suf)) res.Insert(nombre);
            if (!FindNextFile(fh, nombre, attr)) break;
        }
        CloseFindFile(fh);
        bool hallo = res.Count() > antes;
        if (hallo && s_PatronPerfil != pat) {
            s_PatronPerfil = pat;
            int cuantos = res.Count() - antes;
            BZBusLog.Info("[Perfil] listado con '" + pat + "' (" + cuantos + " archivos)");
        }
        return hallo;
    }

    private static bool Cumple(string nombre, string pref, string suf) {
        if (nombre == "") return false;
        if (pref != "" && nombre.IndexOf(pref) != 0) return false;
        if (suf != "") {
            int ls = suf.Length();
            int ln = nombre.Length();
            if (ln < ls) return false;
            if (nombre.Substring(ln - ls, ls) != suf) return false;
        }
        return true;
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

// ----------------------------------------------------------------------------
//  BZDiag - las lineas de DIAGNOSTICO del RPT: las que se repiten mientras dura una condicion o por reloj (DIRECCION y
//  OBJETIVO cada tick, IMAN, DESLIZA, PLANEO, SLAM, RUTEO-REV, [Cinta] LEJOS/ANCLA/SIEMBRA/CAMBIO-FASE, el latido de
//  cada runner, [PERF] cada 5 s, la caja negra volcada al RPT). APAGADAS por defecto: `Diagnostics` en
//  BZAutoDrive_settings.json. Las de carga, de evento, de FIN y de error salen siempre. Encendida, el RPT es el de antes.
// ----------------------------------------------------------------------------
class BZDiag {
    private static bool s_On = false;
    static void Fijar(bool on) { s_On = on; }
    static bool On() { return s_On; }
}

// ----------------------------------------------------------------------------
//  BZCajaNegra - las cajas negras por corrida (BZ12_LOG_<ruta>[#<etiqueta>]_<hhmmss>.csv, una fila por cuadro, ~0,5 MB):
//  APAGADAS por defecto (`BlackBox` en BZAutoDrive_settings.json). Encendidas se guardan las ultimas
//  `BlackBoxKeepPerRoute` de cada ruta y se borran las mas viejas.
//  El orden sale de un INDICE (BZ12_LOG_indice.txt: una linea "<ruta>|<archivo>" por caja, en el orden en que se
//  abrieron) y no del nombre: el nombre lleva la hora sin la fecha (ordenado por nombre mezcla dias) y en la 1.30
//  listar el perfil no siempre anda. Las cajas que no estan en el indice (las de antes) no se tocan. Una caja que no se
//  puede borrar (abierta por otra corrida de la misma ruta) queda en el indice para la proxima.
// ----------------------------------------------------------------------------
class BZCajaNegra {
    static const int GUARDAR_DEFECTO = 10;
    static const string INDICE = "BZ12_LOG_indice.txt";
    private static bool s_On = false;
    private static int s_Guardar = GUARDAR_DEFECTO;

    static void Configurar(bool on, int guardar) {
        s_On = on;
        s_Guardar = guardar;
        if (s_Guardar <= 0) s_Guardar = GUARDAR_DEFECTO;
    }
    static bool Encendida() { return s_On; }
    static int Guardar() { return s_Guardar; }

    // la ruta de una etiqueta "<ruta>#<etiqueta>": las cajas de dos vehiculos sobre la misma toma cuentan juntas
    static string RutaDe(string etiqueta) {
        int k = etiqueta.IndexOf("#");
        if (k < 0) return etiqueta;
        return etiqueta.Substring(0, k);
    }

    // anota la caja recien abierta y borra las mas viejas de su ruta por encima del tope
    static void Anotar(string etiqueta, string archivo) {
        string ruta = RutaDe(etiqueta);
        string pref = ruta + "|";
        string dir = BZCarpetas.Profile();
        array<string> lineas = new array<string>();
        FileHandle fr = OpenFile(dir + INDICE, FileMode.READ);
        if (fr != 0) {
            string ln;
            while (FGets(fr, ln) >= 0) {
                ln = ln.Trim();
                if (ln != "") lineas.Insert(ln);
            }
            CloseFile(fr);
        }
        lineas.Insert(pref + archivo);
        int deRuta = 0;
        int i;
        for (i = 0; i < lineas.Count(); i++) {
            if (lineas.Get(i).IndexOf(pref) == 0) deRuta = deRuta + 1;
        }
        int sobran = deRuta - s_Guardar;
        int borradas = 0;
        array<string> quedan = new array<string>();
        for (i = 0; i < lineas.Count(); i++) {
            string li = lineas.Get(i);
            if (sobran > 0 && li.IndexOf(pref) == 0) {
                string viejo = li.Substring(pref.Length(), li.Length() - pref.Length());
                bool fuera = true;
                if (FileExist(dir + viejo)) fuera = DeleteFile(dir + viejo);
                if (fuera) {
                    sobran = sobran - 1;
                    borradas = borradas + 1;
                    continue;
                }
                sobran = 0;   // no se pudo borrar: queda anotada y se reintenta con la proxima caja
            }
            quedan.Insert(li);
        }
        FileHandle fw = OpenFile(dir + INDICE, FileMode.WRITE);
        if (fw != 0) {
            for (i = 0; i < quedan.Count(); i++) FPrintln(fw, quedan.Get(i));
            CloseFile(fw);
        }
        if (borradas > 0) BZBusLog.Info("[BlackBox] " + ruta + ": " + borradas + " caja(s) negra(s) vieja(s) borrada(s) (se guardan las ultimas " + s_Guardar + ")");
    }
}
