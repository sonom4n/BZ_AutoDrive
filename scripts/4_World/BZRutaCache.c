// ============================================================================
//  BZRutaCache - NO RELEER UNA TOMA QUE NO CAMBIO.
//
//  Medido (sonda de cuadros, Badlands con Trader): la lectura del _wp.csv pesa de 12 a 55 ms segun el largo de la toma, y en un loop se
//  paga en cada vuelta porque cada relanzamiento es un runner nuevo (el RX8 leyo 8 veces el mismo archivo). Aca se guarda, por toma, lo
//  que cuesta armar: los waypoints YA PROCESADOS (suavizados, FollowPath), y cada runner recibe una COPIA PROFUNDA. Copia y no referencia
//  porque el runner los muta despues (la Y del terreno en el spawn, el tope de velocidad del vehiculo, el aprendizaje del corredor), y dos
//  buses de la misma linea comparten la toma.
//
//  Vale mientras el _wp.csv y el _hdr.json no cambien: se compara TAMANO y HUELLA de los dos (la misma huella del registro de cargas,
//  BZHuellaArchivo: 16 enteros por trozo de 4 KB; unos 15 us por trozo). El header cuenta porque el procesado depende de el (FollowPath,
//  los suavizados). Una toma reconvertida en caliente cambia de bytes y se relee. El header en si se vuelve a leer siempre (es chico).
// ============================================================================
class BZRutaCacheEntrada
{
    string m_WpPath;
    int    m_TamWp;
    int    m_HuellaWp;
    int    m_TamHdr;
    int    m_HuellaHdr;
    ref array<ref BZWaypoint> m_Wps;   // procesados, la copia de la cache: nadie los toca
    bool   m_AutoBox;                  // el FollowPath puso GearStrategy = "auto_box"
    int    m_Usos;
}

class BZRutaCache
{
    private static ref map<string, ref BZRutaCacheEntrada> s_Mapa;
    private static int s_Aciertos;
    private static int s_Fallos;

    // tamano y huella de un archivo, de una vez (unos 15 us por trozo de 4 KB: 1 ms cada 270 KB). -1 si no se pudo abrir
    static int Huella(string path, out int tam)
    {
        BZHuellaArchivo h = new BZHuellaArchivo();
        h.Empezar(path);
        while (!h.Unidad())
        {
        }
        tam = h.Tam();
        return h.Huella();
    }

    // El header es chico (unos KB) y de el depende el procesado: se mira ENTERO, linea por linea, no por muestra. Asi un RespawnDelay o un
    // FollowPathMaxKmh editados a mano, del mismo largo, tambien invalidan la cache. -1 si no se pudo abrir
    static int HuellaTexto(string path, out int tam)
    {
        tam = -1;
        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh == 0) return -1;
        int h = 7;
        int n = 0;
        string ln;
        while (FGets(fh, ln) >= 0)
        {
            h = h * 31 + ln.Hash();
            n = n + ln.Length() + 1;
        }
        CloseFile(fh);
        tam = n;
        return h;
    }

    // Si la toma esta en la cache y no cambio, llena cfg.Waypoints con una copia y devuelve true. Las huellas quedan en los out para
    // guardarla despues si hubo que leer.
    static bool Tomar(string wpPath, string hdrPath, BZBusRouteConfig cfg, out int tamWp, out int hWp, out int tamHdr, out int hHdr)
    {
        int tW = 0;
        int tH = 0;
        hWp = Huella(wpPath, tW);
        hHdr = HuellaTexto(hdrPath, tH);
        tamWp = tW;
        tamHdr = tH;
        if (!s_Mapa || !s_Mapa.Contains(wpPath))
        {
            s_Fallos = s_Fallos + 1;
            return false;
        }
        BZRutaCacheEntrada e = s_Mapa.Get(wpPath);
        if (!e || !e.m_Wps || tW < 0 || tH < 0 || e.m_TamWp != tW || e.m_HuellaWp != hWp || e.m_TamHdr != tH || e.m_HuellaHdr != hHdr)
        {
            s_Fallos = s_Fallos + 1;
            BZBusLog.Info("[CACHE] " + NombreCorto(wpPath) + ": la toma o su header cambiaron (tamano o huella): se relee");
            s_Mapa.Remove(wpPath);
            return false;
        }
        cfg.Waypoints = Clonar(e.m_Wps);
        if (e.m_AutoBox) cfg.GearStrategy = "auto_box";
        e.m_Usos = e.m_Usos + 1;
        s_Aciertos = s_Aciertos + 1;
        BZBusLog.Info("[CACHE] " + NombreCorto(wpPath) + ": " + cfg.Waypoints.Count().ToString() + " waypoints copiados de la cache, sin abrir el CSV (uso " + e.m_Usos.ToString() + ")");
        return true;
    }

    // despues de leer y procesar: una copia para la cache
    static void Guardar(string wpPath, int tamWp, int hWp, int tamHdr, int hHdr, array<ref BZWaypoint> wps, bool autoBox)
    {
        if (!wps || tamWp < 0 || tamHdr < 0) return;
        if (!s_Mapa) s_Mapa = new map<string, ref BZRutaCacheEntrada>();
        BZRutaCacheEntrada e = new BZRutaCacheEntrada();
        e.m_WpPath = wpPath;
        e.m_TamWp = tamWp;
        e.m_HuellaWp = hWp;
        e.m_TamHdr = tamHdr;
        e.m_HuellaHdr = hHdr;
        e.m_Wps = Clonar(wps);
        e.m_AutoBox = autoBox;
        e.m_Usos = 0;
        s_Mapa.Set(wpPath, e);
        BZBusLog.Info("[CACHE] " + NombreCorto(wpPath) + ": " + wps.Count().ToString() + " waypoints guardados (" + s_Mapa.Count().ToString() + " tomas en la cache)");
    }

    static array<ref BZWaypoint> Clonar(array<ref BZWaypoint> src)
    {
        array<ref BZWaypoint> r = new array<ref BZWaypoint>();
        if (!src) return r;
        int i;
        for (i = 0; i < src.Count(); i++)
        {
            BZWaypoint w = src.Get(i);
            BZWaypoint c = new BZWaypoint();
            c.pos[0] = w.pos[0];
            c.pos[1] = w.pos[1];
            c.pos[2] = w.pos[2];
            c.isStop = w.isStop;
            c.name = w.name;
            c.stopDuration = w.stopDuration;
            c.stopRadius = w.stopRadius;
            c.targetSpeed = w.targetSpeed;
            c.recordedSpeed = w.recordedSpeed;
            c.targetGear = w.targetGear;
            c.targetThrottle = w.targetThrottle;
            c.targetBrake = w.targetBrake;
            c.targetHandbrake = w.targetHandbrake;
            c.targetSteering = w.targetSteering;
            c.mode = w.mode;
            c.hasInputData = w.hasInputData;
            c.targetHeading = w.targetHeading;
            c.targetHorn = w.targetHorn;
            c.targetLights = w.targetLights;
            c.targetFrontWheel = w.targetFrontWheel;
            c.corridorHalfWidth = w.corridorHalfWidth;
            c.legBreak = w.legBreak;
            r.Insert(c);
        }
        return r;
    }

    static int Tomas()
    {
        if (!s_Mapa) return 0;
        return s_Mapa.Count();
    }

    static int Aciertos() { return s_Aciertos; }
    static int Fallos()   { return s_Fallos; }

    // el nombre de la toma, para el RPT: lo que sigue al ultimo separador, sin el _wp.csv
    private static string NombreCorto(string p)
    {
        int k = p.LastIndexOf("\\");
        int k2 = p.LastIndexOf("/");
        if (k2 > k) k = k2;
        string s = p.Substring(k + 1, p.Length() - k - 1);
        int fin = s.IndexOf("_wp.csv");
        if (fin > 0) s = s.Substring(0, fin);
        return s;
    }
}
