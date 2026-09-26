// ============================================================================
//  BZRouteLoader.c - BZ_AutoDrive 1.2
//
//  ADAPTADOR: wp.csv -> BZRouteData. Es el UNICO archivo que conoce el formato de
//  la grabacion. BZRouteData no sabe de columnas ni de disco, y por eso un test
//  puede armar rutas sinteticas sin tocar el pipeline.
//
//  FAST LOAD (OpenFile/FGets), NUNCA JsonFileLoader: con data grande crashea o tarda
//  ~180 s con 5522 wps. Leccion ya pagada en v1.1.
//
//  MAPEO DE COLUMNAS (verificado contra LoadWaypointsCSV de v1.1):
//    0,1,2 pos x,y,z · 3 isStop · 4 stopDuration · 5 stopRadius · 6 targetSpeed
//    7 targetGear · 8 throttle · 9 brake · 10 handbrake · 11 steering
//    12 hasInputData · 13 mode · 14 name · 15 heading_deg · 16 lights · 17 horn
//    18 frontWheel · 19 corridorHalfWidth · 20 legBreak
//
//  De todo eso, 1.2 toma SOLO lo que el cerebro necesita (7 campos). Los pedales
//  grabados (8,9,11) se DESCARTAN a proposito: es el modo hasInputData=0 y son
//  justamente lo que la DerivationEngine tiene que re-derivar.
// ============================================================================

class BZRouteLoader
{
    // Columnas minimas para que la linea sea valida (hasta 'mode').
    private static const int MIN_COLS = 14;
    private static const int COL_LEGBREAK = 20;
    private static const int COL_HEADING = 15;
    private static const int COL_THROTTLE = 8;
    private static const int COL_HASINPUT = 12;
    // ★★★ EL VOLANTE GRABADO ES LA COLUMNA 18, NO LA 11.
    // La 11 (`targetSteering`) es la TECLA: binaria 0/1, porque el teclado no da analogico.
    // Medido en la toma del GT2RS: mediana 0, p90 1.0, max 1.0. Reproducir eso seria comandar
    // tope pleno de golpe. La 18 es `front_wheel_deg`, el angulo REAL de la rueda en grados
    // --mediana 0.006, p90 1.565, max 14.9-- y su signo coincide con el giro real en el 94%
    // de los ticks, o sea la misma convencion que nuestro comando.
    private static const int COL_FRONTWHEEL = 18;

    // `LoadWpCsv` vivia aca y abria, parseaba y armaba la ruta ENTERA en
    // una sola llamada -- 688 ms adentro de un cuadro, medidos en el RPT del 18/09. Se BORRA: el
    // unico camino para cargar una ruta es ahora BZCargaTrabajo, que hace lo mismo en tajadas con
    // presupuesto por cuadro. Dejarla al lado 'por si acaso' seria dejar abierta la puerta que se
    // vino a cerrar: el pedido del usuario es que TODO lo que carga ruta pase por la cola, sin
    // excepciones -- incluidos el respawn automatico y los vehiculos de un convoy.
    //
    // Lo que queda es lo unico que este archivo tiene que saber: como se lee UNA linea. La cola la
    // llama de a lotes; el formato sigue viviendo en un solo lugar.
    // Devuelve true si la linea era un waypoint valido.
    static bool AgregarLinea(string line, BZRouteData route)
    {
        TStringArray p = new TStringArray();
        SplitKeepEmpty(line, p);
        if (p.Count() < MIN_COLS) return false;      // header de columnas o linea rota
        vector pos = Vector(p.Get(0).ToFloat(), p.Get(1).ToFloat(), p.Get(2).ToFloat());
        if (!BZMath.FiniteVec(pos)) return false;
        bool isStop = (p.Get(3).ToInt() == 1);
        float recKmh = p.Get(6).ToFloat();
        // ⚠ Col 8 = pedal grabado, col 12 = hasInputData. El formato SIEMPRE tuvo el lugar;
        // el conversor lo escribia en cero y esto ni lo miraba. Sin `--keep-inputs` en el
        // conversor sigue dando -1 y todo se comporta como hoy: la compatibilidad no es un
        // agregado, es el default.
        float recThr = -1;
        // ⚠ SE LEE BAJO LA MISMA COMPUERTA `hasInputData` QUE EL PEDAL, Y ES UNA DECISION,
        // no una limitacion: la columna 18 la escribe el conversor SIEMPRE, asi que el dato
        // esta hasta en la flota numerada. Compuertarlo deja (a) la flota intacta por
        // construccion y (b) los pares `nokeep` como controles A/B de verdad -- si no, el
        // control tendria el mismo feedforward que el tratado y no aislaria nada.
        float recSteerDeg = -999.0;
        if (p.Count() > COL_HASINPUT && p.Get(COL_HASINPUT).ToInt() == 1)
        {
            recThr = p.Get(COL_THROTTLE).ToFloat();
            if (p.Count() > COL_FRONTWHEEL) recSteerDeg = p.Get(COL_FRONTWHEEL).ToFloat();
        }
        float hb = p.Get(10).ToFloat();
        string mode = p.Get(13);
        bool isRev = (mode == "reverse");
        // 'parking' es la INTENCION de precision declarada por el autor (ver la auditoria
        // del string mode). Es lo unico que se rescata de ese campo ademas del sentido.
        bool tight = (mode == "parking");
        float headRad = -999.0;
        if (p.Count() > COL_HEADING)
        {
            float hdeg = p.Get(COL_HEADING).ToFloat();
            // 0 = "no disponible" por convencion del formato (asi lo trata v1.1). Se
            // respeta tal cual: inventarle un rumbo norte a un wp sin dato seria peor.
            if (hdeg != 0) headRad = hdeg * Math.DEG2RAD;
        }
        bool legBreak = false;
        if (p.Count() > COL_LEGBREAK) legBreak = (p.Get(COL_LEGBREAK).ToInt() == 1);
        route.AddPoint(pos, recKmh, recThr, recSteerDeg, headRad, hb, isRev, legBreak, isStop, tight);
        return true;
    }

    // El Split nativo DROPEA los campos vacios -> corre los indices de columna y toda la
    // fila queda mal leida. Este preserva los vacios. Mismo motivo que en v1.1.
    private static void SplitKeepEmpty(string line, TStringArray outArr)
    {
        outArr.Clear();
        int start = 0;
        int i;
        int n = line.Length();
        for (i = 0; i < n; i++)
        {
            if (line.Get(i) == ",")
            {
                outArr.Insert(line.Substring(start, i - start));
                start = i + 1;
            }
        }
        outArr.Insert(line.Substring(start, n - start));
    }
}
