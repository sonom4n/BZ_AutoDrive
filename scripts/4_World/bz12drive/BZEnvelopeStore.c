// ============================================================================
//  BZEnvelopeStore.c - BZ_AutoDrive 1.2
//
//  EL ENVELOPE MEDIDO de un vehiculo, persistido por CLASE. Es la implementacion
//  real del canal BZEnvelopeSource: lo que el Manual usa cuando existe medicion, y
//  que hace que sus campos pasen de FALLBACK/DERIVED a MEASURED.
//
//  Guarda cuatro cosas, y ninguna se puede derivar del config:
//    - CoastDecel(v, piso)  : el planeo REAL, que resume masa + freno motor + rodadura
//                             + aero + PISO en una sola respuesta medida
//    - k(v)                 : el understeer REAL (v1.1 inventaba 0.90; se mide ~0.50)
//    - wheelRadiusM         : la API de DayZ NO expone radio de rueda -> r = v/omega
//    - steerMaxRad          : el tope de direccion REAL ejecutado por las ruedas
//
//  Formato CSV por lineas, leido con FGets. NUNCA JsonFileLoader (leccion pagada en
//  v1.1: crashea o tarda ~180 s con data grande).
// ============================================================================

class BZEnvelopeStore extends BZEnvelopeSource
{
    static const int NBINS = 20;          // 0..100 km/h de a 5 (misma grilla que el Manual)
    static const float BINW = 5.0;
    // Muestras minimas para confiar en un bin. Menos que esto es ruido, no medicion.
    static const int MIN_SAMPLES = 4;
    static const int NGEARS = 8;          // indices DayZ: 0 reversa, 1 neutral, 2+ adelante
    // Dispersion maxima tolerada dentro de UNA marcha. El escalon de caja mas chico
    // anda por 1.25, asi que arriba de eso las muestras son de marchas distintas.
    static const float RATIO_MAX_SPREAD = 1.25;
    // Histograma de aceleracion: 0..12 m/s2 en pasos de 0.25.
    static const int   ANBINS = 48;
    static const float ABIN   = 0.25;
    // Un percentil sobre cuatro muestras no es un percentil.
    static const int   ACCEL_MIN_SAMPLES = 40;
    // Direccion: 0..90 grados de a 1. Mas que 90 no es un angulo de rueda.
    static const int   SNBINS = 90;
    static const float SBIN   = 0.0174532925;
    static const int   STEER_MIN_SAMPLES = 20;
    // Muestras que necesita un bin para contar como extremo CORROBORADO.
    static const int   STEER_BIN_MIN     = 5;
    // creep: 0..20 km/h de a 0.25. launch: 0..1 de gas de a 0.02.
    static const int   CNBINS = 80;
    static const float CBIN   = 0.25;
    static const int   LNBINS = 50;
    static const float LBIN   = 0.02;
    static const int   CREEP_MIN_SAMPLES  = 20;
    static const int   LAUNCH_MIN_SAMPLES = 5;

    private string m_Class;
    private ref array<float> m_KSum;
    private ref array<int>   m_KCnt;
    private ref array<float> m_CoastSum;   // idx = surf * NBINS + bin
    private ref array<int>   m_CoastCnt;
    private float m_WheelRadiusM;
    private ref array<int> m_SteerHist;   // idx = |rad| / SBIN
    private int   m_SteerCnt;
    private float m_WheelbaseM;
    private ref array<float> m_RatioSum;
    private ref array<int>   m_RatioCnt;
    private ref array<float> m_RatioMin;
    private ref array<float> m_RatioMax;
    private ref array<int> m_CreepHist;   // idx = kmh / CBIN
    private int   m_CreepCnt;
    private int   m_NoCreepCnt;
    // el creep que VIAJO EN LA TOMA (hdr.json) y la regla del coast2 en reposo.
    private float  m_CreepDeclKmh;
    private string m_CreepDeclSource;
    private int    m_CreepDeclSamples;
    private float  m_CreepDeclCoast;
    private static const int COAST_REPOSO_BINS = 3;   // los 3 bins mas bajos ...
    private static const int COAST_REPOSO_MIN  = 5;   // ... con al menos 5 muestras cada uno
    // ★★ TRANSFERENCIA COMANDO -> ANGULO DE RUEDA, por balde de comando.
    // Deja de ser un Print y pasa a ser un aspecto MEDIDO: se persiste y lo consume el
    // modelo de direccion. Hasta ahora el modelo asumia que la relacion era LINEAL
    // (comando = delta / steerMax), que es justo lo que un vehiculo largo puede no cumplir.
    static const int   STX_BUCKETS = 10;
    static const int   STX_MIN_SAMPLES = 20;   // mismo criterio que steerMax y creep
    private ref array<float> m_StxSum;
    private ref array<int>   m_StxCnt;
    private bool  m_AvisoCoastViejo;
    private bool  m_AvisoStxViejo;
    private ref array<int> m_LaunchHist;  // idx = thr / LBIN
    private int   m_LaunchCnt;
    // (A3): el piso de despegue MEDIDO es el de la CALIBRACION (BZ12TestDrive.FaseLaunch): quieto
    // en llano, rampa +0,02 cada 0,5 s desde 0, el gas del tick en que cruza 0,05 m/s, crudo. El `lhist` de
    // manejo (TryLaunch) es la POSICION DE LA RAMPA DE ARRANQUE al cruzar (Plan a 2 Hz, +0,10) menos la
    // pendiente con un aMax de 0,6-1,4 m/s2 (1 grado = 0,12-0,27 de gas): ni su mediana ni su minimo son un
    // piso (ZiS5: mediana 0,63, minimo 4 muestras de 599 en subida). Queda como instrumento (LaunchRampMedian)
    // y ya no decide LaunchFloor(). `lcal` viaja en el csv y el emit lo conserva (no sale de las tomas).
    private ref array<int> m_LaunchCal;   // idx = thr / LBIN, solo la calibracion escribe
    private int   m_LaunchCalCnt;
    // las lineas del csv que este Store NO entiende (la procedencia que deja el emit: fp, medido,
    // fusionado; o un kind de una version mas nueva). Se cargan tal cual y se reescriben al final del Save. Antes se
    // perdian: el primer Save de calibracion (22/09 19:38) dejo al ChernoBus_02 sin firma, y sin firma el emit fusiona
    // en silencio (la guarda del vehiculo cambiado queda desarmada). Un archivo que otro escribe no se recorta.
    private ref array<string> m_Extra;
    // la FIRMA de cada corrida de calibracion de launch ("pitch#1|g1|...|gN", csv `lcalrun`).
    // La fisica es determinista: desde el mismo spawn la corrida se repite bit a bit (Matador: 9 de 9 iguales, pitch a
    // 4e-5 grados) y sumarla otra vez infla n con copias. Una corrida se suma UNA vez.
    private ref array<string> m_LaunchRuns;
    private bool m_DesdeHdr;                  // el envelope vino en el hdr de la toma (no del archivo de la clase)
    private ref array<int> m_AccelHist;   // idx = a / ABIN, cola en el ultimo
    private int   m_AccelCnt;
    private float m_AccelMin;
    private float m_AccelMax;

    void BZEnvelopeStore()
    {
        m_KSum = new array<float>();
        m_KCnt = new array<int>();
        m_CoastSum = new array<float>();
        m_CoastCnt = new array<int>();
        int i;
        for (i = 0; i < NBINS; i++)
        {
            m_KSum.Insert(0.0);
            m_KCnt.Insert(0);
        }
        for (i = 0; i < NBINS * 2; i++)
        {
            m_CoastSum.Insert(0.0);
            m_CoastCnt.Insert(0);
        }
        m_WheelRadiusM = 0;
        m_SteerHist = new array<int>();
        for (i = 0; i < SNBINS; i++)
        {
            m_SteerHist.Insert(0);
        }
        m_SteerCnt = 0;
        m_WheelbaseM = 0;
        m_AccelHist = new array<int>();
        for (i = 0; i < ANBINS; i++)
        {
            m_AccelHist.Insert(0);
        }
        m_CreepHist = new array<int>();
        for (i = 0; i < CNBINS; i++)
        {
            m_CreepHist.Insert(0);
        }
        m_LaunchHist = new array<int>();
        for (i = 0; i < LNBINS; i++)
        {
            m_LaunchHist.Insert(0);
        }
        m_LaunchCal = new array<int>();
        for (i = 0; i < LNBINS; i++)
        {
            m_LaunchCal.Insert(0);
        }
        m_Extra = new array<string>();
        m_LaunchRuns = new array<string>();
        m_CreepCnt = 0;
        m_NoCreepCnt = 0;
        m_CreepDeclKmh = 0;
        m_CreepDeclSource = "";
        m_CreepDeclSamples = 0;
        m_CreepDeclCoast = 0;
        m_StxSum = new array<float>();
        m_StxCnt = new array<int>();
        int sx;
        for (sx = 0; sx < STX_BUCKETS; sx++)
        {
            m_StxSum.Insert(0);
            m_StxCnt.Insert(0);
        }
        m_LaunchCnt = 0;
        m_LaunchCalCnt = 0;
        m_AccelCnt = 0;
        m_AccelMin = 0;
        m_AccelMax = 0;
        m_RatioSum = new array<float>();
        m_RatioCnt = new array<int>();
        m_RatioMin = new array<float>();
        m_RatioMax = new array<float>();
        for (i = 0; i < NGEARS; i++)
        {
            m_RatioSum.Insert(0.0);
            m_RatioCnt.Insert(0);
            m_RatioMin.Insert(0.0);
            m_RatioMax.Insert(0.0);
        }
    }

    static int BinOf(float kmh)
    {
        int b = (int)(kmh / BINW);
        if (b < 0) b = 0;
        if (b > NBINS - 1) b = NBINS - 1;
        return b;
    }

    void SetClass(string c) { m_Class = c; }
    string GetClass()       { return m_Class; }

    // ======================= acumulacion =======================

    void AddK(float kmh, float k)
    {
        int b = BinOf(kmh);
        m_KSum.Set(b, m_KSum.Get(b) + k);
        m_KCnt.Set(b, m_KCnt.Get(b) + 1);
    }

    // El balde sale del COMANDO; lo que se promedia es el ANGULO EJECUTADO.
    void AddSteerTransfer(float cmdAbs, float angleAbs)
    {
        int b = (int)(cmdAbs * STX_BUCKETS);
        if (b < 0) b = 0;
        if (b > STX_BUCKETS - 1) b = STX_BUCKETS - 1;
        m_StxSum.Set(b, m_StxSum.Get(b) + angleAbs);
        m_StxCnt.Set(b, m_StxCnt.Get(b) + 1);
    }

    override bool HasSteerTransfer(int bucket)
    {
        if (bucket < 0 || bucket >= STX_BUCKETS) return false;
        return m_StxCnt.Get(bucket) >= STX_MIN_SAMPLES;
    }

    override float SteerTransferAt(int bucket)
    {
        if (!HasSteerTransfer(bucket)) return 0;
        return m_StxSum.Get(bucket) / m_StxCnt.Get(bucket);
    }

    int SteerTransferSamples(int bucket)
    {
        if (bucket < 0 || bucket >= STX_BUCKETS) return 0;
        return m_StxCnt.Get(bucket);
    }

    void AddCoast(int surf, float kmh, float decel)
    {
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        int idx = s * NBINS + BinOf(kmh);
        m_CoastSum.Set(idx, m_CoastSum.Get(idx) + decel);
        m_CoastCnt.Set(idx, m_CoastCnt.Get(idx) + 1);
    }

    void AddRatio(int gearDayZ, float ratioTotal)
    {
        if (gearDayZ < 0 || gearDayZ >= NGEARS) return;
        m_RatioSum.Set(gearDayZ, m_RatioSum.Get(gearDayZ) + ratioTotal);
        m_RatioCnt.Set(gearDayZ, m_RatioCnt.Get(gearDayZ) + 1);
        float lo = m_RatioMin.Get(gearDayZ);
        float hi = m_RatioMax.Get(gearDayZ);
        if (lo <= 0.0001 || ratioTotal < lo) m_RatioMin.Set(gearDayZ, ratioTotal);
        if (ratioTotal > hi) m_RatioMax.Set(gearDayZ, ratioTotal);
    }

    // ⚠ SI LAS MUESTRAS DE UNA MARCHA NO CONCUERDAN ENTRE SI, NO HAY MEDICION.
    //
    // El ratio de 1ra no convergia entre corridas: 27.88 -> 14.42 -> 25.99. Un factor
    // ~1.93 es el ESCALON DE CAJA, no ruido -- parte de las muestras vienen de otra
    // marcha (o de una rueda patinando). Promediarlas da un numero que no es el de
    // ninguna de las dos, y que ademas BAILA segun cuantas muestras de cada tipo hubo.
    //
    // Y no es un numero cualquiera: lo MEDIDO MANDA sobre el prior, y de este ratio
    // salen creep, launch y topSpeed. Un ratio que baila hace bailar todo lo derivado,
    // aunque el reporte de procedencia diga MEASURED y se vea sano.
    //
    // La regla del framework ya estaba escrita para los bins: una medicion floja es PEOR
    // que ninguna. Esto es lo mismo un escalon mas arriba -- no es que falten muestras,
    // es que las que hay se CONTRADICEN. Con dispersion alta se devuelve 0 = SIN DATO, y
    // el Manual se queda con su prior, declarado como prior.
    bool RatioIsConsistent(int gearDayZ)
    {
        if (gearDayZ < 0 || gearDayZ >= NGEARS) return false;
        float lo = m_RatioMin.Get(gearDayZ);
        float hi = m_RatioMax.Get(gearDayZ);
        if (lo <= 0.0001) return false;
        return (hi / lo) <= RATIO_MAX_SPREAD;
    }

    // ⚠ RESTAURADO: una edicion por rebanada se lo llevo puesto junto con el bloque de
    // aMax, y el store quedo heredando el de la base -- que devuelve 0. Sintoma: las 4
    // pruebas de ratio en cero in-game, y `gearing=FALLBACK` en produccion aunque el
    // envelope tuviera muestras. Ningun validador lo vio: cazan simbolos AGREGADOS mal,
    // no simbolos BORRADOS. Lo cazo el self-test in-game, tarde pero lo cazo.
    override float RatioForGear(int gearDayZ)
    {
        if (gearDayZ < 0 || gearDayZ >= NGEARS) return 0;
        if (m_RatioCnt.Get(gearDayZ) < MIN_SAMPLES) return 0;
        if (!RatioIsConsistent(gearDayZ)) return 0;      // se contradicen: SIN DATO
        return m_RatioSum.Get(gearDayZ) / m_RatioCnt.Get(gearDayZ);
    }

    // ⚠ NI MAXIMO NI PROMEDIO: LO QUE FALTABA ERAN LAS COMPUERTAS, Y DESPUES UN PERCENTIL.
    //
    // Tres intentos y los tres fallaron por el mismo motivo de fondo:
    //   MAXIMO   -> 1.49 -> 3.42 -> 5.72 -> 6.51. Un maximo solo puede subir: cada pico
    //               raro lo trinca para siempre. No es un estimador.
    //   PROMEDIO -> 0.42, con 2059 muestras entre 0.0013 y 5.72. Se traga los momentos de
    //               GAS A FONDO SIN ACELERAR -- el auto clavado en la cuesta, a fondo y a
    //               0.03 km/h. Con aMax 0.42, HoldThrottle satura y ClimbMargin da
    //               siempre negativo: tan inutil como el 6.51, y por la punta opuesta.
    //
    // El error no era el estadistico: era la ENTRADA. Un momento bogado no es una muestra
    // de aceleracion maxima -- es el vehiculo en su LIMITE, que es la pregunta opuesta.
    // Filtrado eso, queda una poblacion de aceleraciones genuinas, y ahi el estimador
    // correcto es un percentil robusto sobre esa poblacion, no un acumulador sobre todas.
    // Cual percentil se discute abajo, en MaxAccelMs2().
    //
    // Se guarda como HISTOGRAMA: un percentil necesita la distribucion, no un acumulador,
    // y con ANBINS enteros se persiste igual de barato que sum/count.
    void AddAccel(float a)
    {
        int b = (int)(a / ABIN);
        if (b < 0) b = 0;
        if (b > ANBINS - 1) b = ANBINS - 1;
        m_AccelHist.Set(b, m_AccelHist.Get(b) + 1);
        m_AccelCnt = m_AccelCnt + 1;
        if (m_AccelMin <= 0.0001 || a < m_AccelMin) m_AccelMin = a;
        if (a > m_AccelMax) m_AccelMax = a;
    }

    // ★ CREEP Y LAUNCH MEDIDOS DIRECTO, no derivados de la caja.
    //
    // Los dos salian de la cadena  ratio de marcha -> rpm -> velocidad, y esa cadena se
    // rompio: el ratio por marcha no converge (3.94 a 54.75 DENTRO de la misma marcha) y
    // el gate estricto lo descarta, con razon. Pero entonces creep y launch caen al prior
    // -- y ahi paso algo que vale nombrar: las maniobras andaban MEJOR con el ratio SUCIO
    // (25.99, promedio de basura) que con el prior honesto. 0.64 m contra 1.97 m.
    //
    // La leccion NO es "aceptemos datos sucios". Es que la cadena de derivacion era el
    // problema: creep y launch son DIRECTAMENTE OBSERVABLES y no necesitan la caja.
    //   creep  = la velocidad a la que el vehiculo se queda solo, sin gas ni freno, en llano
    //   launch = el gas con el que EMPIEZA A MOVERSE desde parado
    // Medir la magnitud que se usa, en vez de derivarla de tres que no se pueden medir,
    // saca del medio a toda la cadena. Mediana, por lo mismo que en aMax.
    void AddCreep(float kmh)
    {
        int b = (int)(kmh / CBIN);
        if (b < 0) b = 0;
        if (b > CNBINS - 1) b = CNBINS - 1;
        m_CreepHist.Set(b, m_CreepHist.Get(b) + 1);
        m_CreepCnt = m_CreepCnt + 1;
    }

    // ⚠ "LA MEDICION DICE CERO" NO ES "LA MEDICION FALLO".
    //
    // Un vehiculo detenido, estable, en llano y sin ningun comando aplicado esta diciendo
    // algo: que NO REPTA. Eso es un dato, no un dato faltante -- y hasta ahora se perdia,
    // porque la compuerta de velocidad minima descartaba justo esas muestras y el
    // resultado (cero) era indistinguible de "no junte nada".
    //
    // La diferencia importa porque lo medido manda: con evidencia de no-reptar, insistir
    // con un creep derivado de la caja es inventar un ralenti que el vehiculo no tiene.
    void AddNoCreep()
    {
        m_NoCreepCnt = m_NoCreepCnt + 1;
    }

    // Hay EVIDENCIA POSITIVA de que este vehiculo no repta (no solo ausencia de muestras).
    // (el usuario, 22/09): EL CREEP SE DERIVA DEL COAST2, SIN TOMA DE CALIBRACION.
    // El planeo es el balance neto de fuerzas sin acelerador; el creep es la velocidad donde ese balance se anula. Regla
    // del analista, validada contra los 27 envelopes con nocreep medido (27 aciertos, 0 fallos): la decel del coast en
    // los 3 bins mas bajos con >= 5 muestras, ponderada por muestras. > 0: frena aun casi parado -> NO REPTA, creep 0.
    // < 0: aceleraria solo -> repta, y el creep es el cruce por cero. Hoy no existe en la flota: los 43 envelopes con
    // coast2 dan decel positiva (tractor, tanque, motos, supercars, camiones, buses, sedanes): en este motor no repta
    // nadie. Un chist2 que contradiga al coast2 del MISMO envelope pierde por fisica, sin umbral: los 4 espurios del
    // banco (CivilianSedan 10,6 · Hatchback_02 12,4 · Harley 10,6 · hx58 3,4) caen solos. Y si el dato VIAJO EN LA TOMA
    // (hdr.json -> SetCreepDeclarado) manda ese: no puede faltar ni desincronizarse. Orden: hdr -> coast2 -> cuentas.
    void SetCreepDeclarado(float kmh, string fuente, int muestras, float coastReposo)
    {
        m_CreepDeclKmh = kmh;
        m_CreepDeclSource = fuente;
        m_CreepDeclSamples = muestras;
        m_CreepDeclCoast = coastReposo;
    }

    // EL DATO QUE VIAJA EN LA TOMA LO LEE EL FRAMEWORK, no cada consumidor. Medido: el reproductor
    // directo (BZ_AutoDrive12) arma su propio store sin pasar por BZBusService, asi que la declaracion puesta en la
    // carga del servicio no le llegaba y el [Manual] decia `fuente coast2` con los 103 hdr ya migrados. Se lee el
    // texto del hdr (BZJsonPresencia, sin JsonFileLoader) y se declara si CreepSource no esta vacio. Devuelve si
    // declaro. Los dos consumidores llaman esta linea antes de armar el Manual.
    // EL ENVELOPE ENTERO VIAJA EN EL hdr.json: el conversor copia las lineas de envelope_<clase>.csv en la lista
    // "Envelope" del hdr (+ "EnvelopeClass"). Si estan, se cargan ANTES que el archivo de la clase, con el mismo ParseLine: la
    // toma es autocontenida y quien la reciba no necesita el envelope aparte. Sin la clave, el que llama sigue con Load().
    // Si el hdr trae el envelope de OTRA clase (la toma se convirtio con un vehiculo y corre con otro), se ignora con aviso.
    bool CargarDesdeHdr(string hdrPath, bool silencioso = false)
    {
        m_DesdeHdr = false;
        string textoE = BZJsonPresencia.LeerHeader(hdrPath);
        if (textoE == "") return false;
        string listaE = BZJsonPresencia.Lista(textoE, "Envelope");
        if (listaE == "") return false;
        string claseE = BZJsonPresencia.Valor(textoE, "EnvelopeClass");
        if (claseE != "" && m_Class != "" && claseE != m_Class)
        {
            Print("[Envelope] el hdr trae el envelope de " + claseE + " y el vehiculo es " + m_Class + ": se ignora, se usa el archivo de la clase · " + hdrPath);
            return false;
        }
        m_Extra.Clear();
        m_LaunchRuns.Clear();
        int nE = 0;
        int iE = 0;
        int largoE = listaE.Length();
        while (iE < largoE)
        {
            int aE = listaE.IndexOfFrom(iE, "\"");
            if (aE < 0) break;
            int bE = listaE.IndexOfFrom(aE + 1, "\"");
            if (bE < 0) break;
            string lnE = listaE.Substring(aE + 1, bE - aE - 1);
            if (lnE != "")
            {
                ParseLine(lnE);
                nE = nE + 1;
            }
            iE = bE + 1;
        }
        if (nE == 0) return false;
        m_DesdeHdr = true;
        if (!silencioso) Print("[Envelope] cargado del hdr de la toma: " + nE.ToString() + " lineas (clase " + claseE + ") -> " + Summary() + " · " + hdrPath);
        return true;
    }

    bool DesdeHdr() { return m_DesdeHdr; }

    // CUANTO MIDIO ESTA FICHA: la suma de las cuentas de todos los histogramas (direccion, creep y no-creep, despegue y su
    // calibracion, aceleracion, transferencia, relacion de marchas, subviraje, planeo). Es intrinseca al store: no depende de
    // la linea `medido` que escribe el emisor. Sirve para que una ficha de clase "stub" (una toma de 10 s, o uno de los
    // stubs de la flota) no le gane a una rica que viaja en el hdr de la toma.
    int MuestrasTotales()
    {
        int totM = m_SteerCnt + m_CreepCnt + m_NoCreepCnt + m_LaunchCnt + m_LaunchCalCnt + m_AccelCnt;
        int iM;
        for (iM = 0; iM < STX_BUCKETS; iM++) totM = totM + m_StxCnt.Get(iM);
        for (iM = 0; iM < NGEARS; iM++) totM = totM + m_RatioCnt.Get(iM);
        for (iM = 0; iM < NBINS; iM++) totM = totM + m_KCnt.Get(iM);
        for (iM = 0; iM < NBINS * 2; iM++) totM = totM + m_CoastCnt.Get(iM);
        return totM;
    }

    bool DeclararCreepDesdeHdr(string hdrPath)
    {
        string textoH = BZJsonPresencia.LeerHeader(hdrPath);
        if (textoH == "") return false;
        string fuenteH = BZJsonPresencia.Valor(textoH, "CreepSource");
        if (fuenteH == "") return false;
        float kmhH = BZJsonPresencia.Valor(textoH, "CreepKmh").ToFloat();
        int muestrasH = BZJsonPresencia.Valor(textoH, "CreepSamples").ToInt();
        float coastH = BZJsonPresencia.Valor(textoH, "CoastAtRest").ToFloat();
        SetCreepDeclarado(kmhH, fuenteH, muestrasH, coastH);
        string dh = "[Envelope] creep del hdr de la toma: " + kmhH.ToString() + " kmh · fuente hdr:" + fuenteH;
        dh = dh + " · " + muestrasH.ToString() + " muestras · coast a v->0 " + coastH.ToString() + " m/s2 · " + hdrPath;
        Print(dh);
        return true;
    }

    // El coast en reposo: primero el piso duro, si no tiene bins medidos el blando. decel > 0 = frena.
    private bool CoastEnReposo(out float decel, out int muestras, out int surf)
    {
        decel = 0;
        muestras = 0;
        surf = 0;
        int s;
        int b;
        int idx;
        int usados;
        int cnt;
        float suma;
        for (s = 0; s < 2; s++)
        {
            suma = 0;
            cnt = 0;
            usados = 0;
            for (b = 0; b < NBINS; b++)
            {
                if (usados >= COAST_REPOSO_BINS) break;
                idx = s * NBINS + b;
                if (m_CoastCnt.Get(idx) < COAST_REPOSO_MIN) continue;
                suma = suma + m_CoastSum.Get(idx);
                cnt = cnt + m_CoastCnt.Get(idx);
                usados = usados + 1;
            }
            if (usados > 0)
            {
                decel = suma / cnt;
                muestras = cnt;
                surf = s;
                return true;
            }
        }
        return false;
    }

    // Si el coast en reposo ACELERA (repta): el creep es donde la curva cruza cero, interpolado entre centros de bin.
    // Sin bin que frene por encima, el centro del ultimo bin medido. No existe hoy en la flota; queda por completitud.
    private float CreepPorCruce(int surf)
    {
        int b;
        int idx;
        float dPrev = 0;
        float cPrev = -1;
        float d;
        float c;
        for (b = 0; b < NBINS; b++)
        {
            idx = surf * NBINS + b;
            if (m_CoastCnt.Get(idx) < COAST_REPOSO_MIN) continue;
            d = m_CoastSum.Get(idx) / m_CoastCnt.Get(idx);
            c = (b + 0.5) * BINW;
            if (d >= 0 && cPrev >= 0) return cPrev + (c - cPrev) * (0 - dPrev) / (d - dPrev);
            dPrev = d;
            cPrev = c;
        }
        if (cPrev < 0) return 0;
        return cPrev;
    }

    override string CreepSource()
    {
        if (m_CreepDeclSource != "") return "hdr:" + m_CreepDeclSource;
        float dRep;
        int nRep;
        int sRep;
        if (CoastEnReposo(dRep, nRep, sRep)) return "coast2";
        if (NoCreepGana()) return "nocreep";
        if (m_CreepCnt >= CREEP_MIN_SAMPLES) return "chist2";
        if (m_NoCreepCnt >= CREEP_MIN_SAMPLES) return "nocreep";
        return "";
    }

    override float CoastAtRest()
    {
        if (m_CreepDeclSource != "") return m_CreepDeclCoast;
        float dRep;
        int nRep;
        int sRep;
        if (CoastEnReposo(dRep, nRep, sRep)) return dRep;
        return 0;
    }

    override int CoastAtRestSamples()
    {
        if (m_CreepDeclSource != "") return m_CreepDeclSamples;
        float dRep;
        int nRep;
        int sRep;
        if (CoastEnReposo(dRep, nRep, sRep)) return nRep;
        return 0;
    }

    // (PEDIDO DEL USUARIO): PRECEDENCIA POR CANTIDAD DE MUESTRAS. Medido en la toma de la linea
    // el ChernoBus tenia 2.511 frames quietos ENGRANADO con los pedales sueltos (no repta) y 33 frames de
    // `chist2` a 7 y 11 km/h que no estaban en equilibrio -- estaban frenando, 281 de 532 en g0 -- y que pasaron el filtro
    // al cruzar por aceleracion cero (el 7 % de una desaceleracion lo cumple en UN frame). Hasta aca el store cedia
    // ante cualquier chist2 >= CREEP_MIN_SAMPLES sin mirar la otra cuenta: 33 frames de ruido le ganaron a 2.511 de
    // evidencia, el creep salio 7,125 MEDIDO, UmbralCasiParado() quedo en 7 km/h y la rampa se armo con el bus rodando
    // (Svergino). Regla: gana el lado con MAS muestras validas. El empate exacto lo gana el creep (conservador: no
    // cambia lo de hoy sin evidencia estricta); un nocreep por debajo del minimo no compite. Cero constantes nuevas.
    // ⚠ Va PRIMERO en CreepKmh(): el lector pregunta CreepKmh() y solo si da <= 0,1 consulta CreepIsZeroMeasured();
    // puesta solo aca abajo, la precedencia no tendria efecto. Lo que NO cubre: un vehiculo con nocreep = 0 y chist2
    // de ruido (CivilianSedan, Hatchback_02, la Harley, hx58) -- ahi no hay otro lado que pueda ganar por cantidad.
    private bool NoCreepGana()
    {
        if (m_NoCreepCnt < CREEP_MIN_SAMPLES) return false;
        return m_NoCreepCnt > m_CreepCnt;
    }

    override bool CreepIsZeroMeasured()
    {
        if (m_CreepDeclSource != "") return (m_CreepDeclKmh <= 0.1);   // viajo en la toma
        float dRep;
        int nRep;
        int sRep;
        if (CoastEnReposo(dRep, nRep, sRep)) return (dRep > 0);        // frena casi parado = no repta
        if (NoCreepGana()) return true;                        // no repta: gana la evidencia mayoritaria
        if (m_CreepCnt >= CREEP_MIN_SAMPLES) return false;   // repta: hay medicion positiva
        return m_NoCreepCnt >= CREEP_MIN_SAMPLES;
    }

    override int NoCreepSamples() { return m_NoCreepCnt; }

    void AddLaunch(float thr)
    {
        // la rampa de calibracion acumula CAL_L_STEP en float32 y 0,10/0,12/0,14/0,16 quedan un pelo por
        // debajo del multiplo exacto: truncar los mandaba al bin de abajo (0,16 -> bin 7). Un milesimo de bin de epsilon.
        int b = (int)(thr / LBIN + 0.001);
        if (b < 0) b = 0;
        if (b > LNBINS - 1) b = LNBINS - 1;
        m_LaunchHist.Set(b, m_LaunchHist.Get(b) + 1);
        m_LaunchCnt = m_LaunchCnt + 1;
    }

    // ERA `n / 2` (division entera) y devolvia el 2o de 5, no el 3o. Dos calibraciones
    // con muestras distintas (bins 2,3,4,6,7 y 2,3,6,7,7) dieron el MISMO 0,07 porque las dos veces el segundo bin
    // poblado era el 3. Rango "nearest rank": el elemento ceil(n/2) = (n+1)/2; con n par es la mediana baja, como antes.
    private float MedianOf(array<int> h, int n, int nbins, float w)
    {
        if (n <= 0) return 0;
        int objetivo = (n + 1) / 2;
        int acum = 0;
        int i;
        for (i = 0; i < nbins; i++)
        {
            acum = acum + h.Get(i);
            if (acum >= objetivo) return (i + 0.5) * w;
        }
        return (nbins - 0.5) * w;
    }

    override float CreepKmh()
    {
        if (m_CreepDeclSource != "") return m_CreepDeclKmh;    // viajo en la toma (hdr.json)
        float dRep;
        int nRep;
        int sRep;
        if (CoastEnReposo(dRep, nRep, sRep))
        {
            if (dRep > 0) return 0;                            // frena aun casi parado -> NO REPTA
            return CreepPorCruce(sRep);
        }
        if (NoCreepGana()) return 0;                           // la precedencia se decide ACA, no abajo
        if (m_CreepCnt < CREEP_MIN_SAMPLES) return 0;
        return MedianOf(m_CreepHist, m_CreepCnt, CNBINS, CBIN);
    }

    // una muestra por despegue de la calibracion (FaseLaunch), gas CRUDO en llano.
    void AddLaunchCal(float thr)
    {
        // la rampa de calibracion acumula CAL_L_STEP en float32 y 0,10/0,12/0,14/0,16 quedan un pelo por
        // debajo del multiplo exacto: truncar los mandaba al bin de abajo (0,16 -> bin 7). Un milesimo de bin de epsilon.
        int b = (int)(thr / LBIN + 0.001);
        if (b < 0) b = 0;
        if (b > LNBINS - 1) b = LNBINS - 1;
        m_LaunchCal.Set(b, m_LaunchCal.Get(b) + 1);
        m_LaunchCalCnt = m_LaunchCalCnt + 1;
    }

    // SOLO la calibracion decide el piso. Sin calibracion devuelve 0 y el reader deriva o cae al fallback.
    override float LaunchFloor()
    {
        if (m_LaunchCalCnt < LAUNCH_MIN_SAMPLES) return 0;
        return MedianOf(m_LaunchCal, m_LaunchCalCnt, LNBINS, LBIN);
    }

    // El histograma de MANEJO: gas de la rampa de arranque al cruzar 0,05 m/s (instrumento, no piso).
    float LaunchRampMedian()
    {
        if (m_LaunchCnt < LAUNCH_MIN_SAMPLES) return 0;
        return MedianOf(m_LaunchHist, m_LaunchCnt, LNBINS, LBIN);
    }

    override int LaunchCalSamples() { return m_LaunchCalCnt; }

    // ★ 930: la firma de una corrida: pitch del despegue #1 CRUDO (4 decimales) y los gases (2 decimales), con '|'.
    // La comparacion NO es por igualdad de cadena: redondear a bins tiene fronteras (-0,12509 y -0,12488 caian en "-0.13" y
    // "-0.12" y la misma corrida se contaba dos veces, ). Dos corridas son la misma si los gases coinciden Y
    // |pitch_a - pitch_b| < RUN_PITCH_TOL_DEG: 7x el jitter del mismo spawn (0,0007 grados), 2,6x menos que otro spawn (0,013).
    static const float RUN_PITCH_TOL_DEG = 0.005;

    static string FirmaCorrida(float pitchDeg, array<float> gases)
    {
        float pr = Math.Round(pitchDeg * 10000.0) / 10000.0;
        string s = pr.ToString();
        int i;
        for (i = 0; i < gases.Count(); i++)
        {
            float gr = Math.Round(gases.Get(i) * 100.0) / 100.0;
            s = s + "|" + gr.ToString();
        }
        return s;
    }

    static string GasesDeFirma(string firma)
    {
        int k = firma.IndexOf("|");
        if (k < 0) return "";
        return firma.Substring(k + 1, firma.Length() - k - 1);
    }

    static float PitchDeFirma(string firma)
    {
        int k = firma.IndexOf("|");
        if (k < 0) return 0;
        return firma.Substring(0, k).ToFloat();
    }

    // Misma corrida = mismos gases (a 2 decimales) y el pitch del #1 a menos de RUN_PITCH_TOL_DEG de una corrida guardada.
    bool LaunchRunDuplicada(float pitchDeg, array<float> gases)
    {
        string gasesN = GasesDeFirma(FirmaCorrida(pitchDeg, gases));
        int i;
        for (i = 0; i < m_LaunchRuns.Count(); i++)
        {
            string vieja = m_LaunchRuns.Get(i);
            if (GasesDeFirma(vieja) != gasesN) continue;
            float d = PitchDeFirma(vieja) - pitchDeg;
            if (d < 0) d = -d;
            if (d < RUN_PITCH_TOL_DEG) return true;
        }
        return false;
    }

    // Suma la corrida ENTERA (cada gas a lcal) y guarda su firma. Si la firma ya estaba, no suma nada y devuelve false.
    bool AddLaunchRun(float pitchDeg, array<float> gases)
    {
        string firma = FirmaCorrida(pitchDeg, gases);
        if (LaunchRunDuplicada(pitchDeg, gases)) return false;
        int i;
        for (i = 0; i < gases.Count(); i++)
        {
            AddLaunchCal(gases.Get(i));
        }
        m_LaunchRuns.Insert(firma);
        return true;
    }

    int LaunchRuns() { return m_LaunchRuns.Count(); }

    float LaunchCalMin()
    {
        int i;
        for (i = 0; i < LNBINS; i++)
        {
            if (m_LaunchCal.Get(i) > 0) return (i + 0.5) * LBIN;
        }
        return 0;
    }

    float LaunchCalMax()
    {
        int i;
        for (i = LNBINS - 1; i >= 0; i--)
        {
            if (m_LaunchCal.Get(i) > 0) return (i + 0.5) * LBIN;
        }
        return 0;
    }

    override int CreepSamples()  { return m_CreepCnt; }
    int LaunchSamples() { return m_LaunchCnt; }

    // Percentil sobre el histograma. Devuelve el centro del bin donde cae.
    float AccelPercentile(float p)
    {
        if (m_AccelCnt <= 0) return 0;
        // mismo defecto que MedianOf: (int)(cnt*p) truncaba el rango. Nearest rank = ceil(cnt*p), minimo 1.
        int objetivo = (int)Math.Ceil(m_AccelCnt * p);
        if (objetivo < 1) objetivo = 1;
        int acum = 0;
        int i;
        for (i = 0; i < ANBINS; i++)
        {
            acum = acum + m_AccelHist.Get(i);
            if (acum >= objetivo) return (i + 0.5) * ABIN;
        }
        return (ANBINS - 0.5) * ABIN;
    }

    // ¿MEDIANA O PERCENTIL ALTO? Una vez que la ENTRADA esta gateada al regimen valido,
    // lo que queda de dispersion es ruido de medicion (derivar velocidad a 40 Hz) mas la
    // forma de la curva de par. El ruido es simetrico, asi que un percentil alto no mide
    // "lo mejor que puede el auto": mide los frames con suerte. La mediana es el centro
    // robusto de la capacidad, y no la arrastra ninguna de las dos colas.
    //
    // Y hay un argumento de SEGURIDAD que rompe el empate: aMax entra dividiendo en
    // HoldThrottle = g*sin(pitch)/aMax. Sobreestimarlo da un gas de sostenimiento chico,
    // el hill-start suelta el freno antes de tiempo y el vehiculo RUEDA HACIA ATRAS.
    // Subestimarlo solo lo hace arrancar mas despacio. El error barato es hacia abajo.
    override float MaxAccelMs2()
    {
        if (m_AccelCnt < ACCEL_MIN_SAMPLES) return 0;
        return AccelPercentile(0.50);
    }

    int   AccelSamples() { return m_AccelCnt; }
    float AccelMin()     { return m_AccelMin; }
    float AccelMax()     { return m_AccelMax; }

    void SetWheelRadius(float r)  { if (r > 0.05) m_WheelRadiusM = r; }
    // ⚠ ERA UN MAXIMO PELADO -- el mismo trinquete que ya nos costo caro en aMax, y que
    // no habiamos aplicado aca. Una sola lectura basura lo deja clavado para siempre:
    // entro un 2.99146 rad (171 grados, que NO ES UN ANGULO DE RUEDA, probablemente un
    // transitorio al reorientar el vehiculo en el banco) y el steerMax quedo en 2.99 en
    // vez de 1.314. Como steerKPose = 1/steerMax, la ganancia de direccion se partio al
    // medio y la reversa se fue de 0.004 m a 0.955 m de error lateral.
    //
    // Ahora: histograma + percentil ALTO. steerMax si es un extremo -- es "lo mas que las
    // ruedas llegan a girar" -- asi que la mediana lo subestimaria; pero el p99 sobre una
    // poblacion ya saneada da el extremo REAL sin que un frame suelto lo secuestre.
    void SetSteerMax(float rad)
    {
        if (rad < 0) rad = -rad;
        if (rad >= SNBINS * SBIN) return;      // 90 grados o mas: no es direccion
        int b = (int)(rad / SBIN);
        if (b < 0) b = 0;
        if (b > SNBINS - 1) b = SNBINS - 1;
        m_SteerHist.Set(b, m_SteerHist.Get(b) + 1);
        m_SteerCnt = m_SteerCnt + 1;
    }
    void SetWheelbase(float wb)   { if (wb > 0.5) m_WheelbaseM = wb; }

    // ======================= BZEnvelopeSource =======================
    // Un bin con menos de MIN_SAMPLES NO se reporta como medido: devolver un promedio de
    // dos muestras sueltas seria vender ruido como medicion, y lo medido MANDA sobre el
    // prior -- asi que una medicion floja es peor que ninguna.

    override bool HasK(int bin)
    {
        if (bin < 0 || bin >= NBINS) return false;
        return m_KCnt.Get(bin) >= MIN_SAMPLES;
    }

    override float KAt(int bin)
    {
        if (!HasK(bin)) return 0;
        return m_KSum.Get(bin) / m_KCnt.Get(bin);
    }

    override bool HasCoast(int surf, int bin)
    {
        int idx = CoastIdx(surf, bin);
        if (idx < 0) return false;
        return m_CoastCnt.Get(idx) >= COAST_MIN_SAMPLES;
    }

    // ★★ EL PLANEO TENIA LA COMPUERTA MAS DEBIL DEL ENVELOPE: MIN_SAMPLES = 4.
    // aMax exige 40, steerMax y creep 20, launch 5 -- el planeo compartia el 4 generico con
    // ratio, y es el estimador del que depende TODA la planificacion de velocidad. El dato
    // lo grita: en el envelope real del Sedan_02 los 5 bins con n>=71 caen TODOS en
    // [0.55, 1.10] y ajustan coast(v) = 0.512 + 0.004156*v^2; los 3 bins con n<=6 caen
    // TODOS afuera y para los dos lados (5.42 / 0.23 / 0.29). El discriminador es n.
    //
    // El umbral se DERIVA del ruido medido, no se elige: el residuo RMS de ese ajuste es
    // 0.0386 sobre medias de n~94, o sea sigma por muestra = 0.0386*sqrt(94) = 0.376 m/s2.
    // El escalon que la curva tiene que resolver entre bins es (1.0960-0.5499)/6 = 0.091,
    // y exigir un error estandar de a lo sumo medio escalon da n >= (0.376/0.0455)^2 = 68.
    static const int COAST_MIN_SAMPLES = 68;

    int CoastSamples(int surf, int bin)
    {
        if (bin < 0 || bin > NBINS - 1) return 0;
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        return m_CoastCnt.Get(s * NBINS + bin);
    }

    override float CoastAt(int surf, int bin)
    {
        int idx = CoastIdx(surf, bin);
        if (idx < 0) return 0;
        if (m_CoastCnt.Get(idx) < MIN_SAMPLES) return 0;
        return m_CoastSum.Get(idx) / m_CoastCnt.Get(idx);
    }

    private int CoastIdx(int surf, int bin)
    {
        if (bin < 0 || bin >= NBINS) return -1;
        int s = 0;
        if (surf == BZSurf.SOFT) s = 1;
        return s * NBINS + bin;
    }

    override float WheelRadiusM() { return m_WheelRadiusM; }
    override float WheelbaseM()   { return m_WheelbaseM; }
    // ★ CORRECCION DEL: steerMax es CAPACIDAD, no uso tipico.
    //
    // El intento anterior fue un PERCENTIL ALTO, y estaba conceptualmente mal: un percentil
    // mide cuanto volante se USO en la ruta que se manejo, y eso es dependiente de la ruta
    // -- CONVOY_03 (vial, poco volante) daria un numero, SEDAN_SHORT (reversas apretadas,
    // full-lock) daria otro. steerMax es una propiedad del VEHICULO: lo maximo que las
    // ruedas pueden girar. Tiene que ser un EXTREMO.
    //
    // Pero un extremo pelado es el trinquete que causo todo esto (entro 2.99 rad = 171
    // grados y quedo clavado). La salida no es dejar de tomar el extremo: es exigir que el
    // extremo este CORROBORADO. Se devuelve el bin mas alto con al menos BIN_MIN muestras.
    //   - un frame espurio cae solo en su bin  -> se ignora
    //   - el full-lock real se toca muchas veces (cada cusp, cada reversa, y el barrido
    //     dedicado del banco) -> tiene muestras de sobra
    // Mas el tope FISICO: arriba de 90 grados no es un angulo de rueda, no entra ni al
    // histograma. Y el grabador no muestrea mientras el banco reorienta el vehiculo, que
    // era la FUENTE del transitorio.
    override float SteerMaxRad()
    {
        if (m_SteerCnt < STEER_MIN_SAMPLES) return 0;
        int i;
        for (i = SNBINS - 1; i >= 0; i--)
        {
            if (m_SteerHist.Get(i) >= STEER_BIN_MIN) return (i + 0.5) * SBIN;
        }
        return 0;
    }

    int SteerSamples() { return m_SteerCnt; }

    // ======================= persistencia =======================

    private string Path()
    {
        return "$profile:BZ_AutoDrive\\envelope_" + m_Class + ".csv";
    }

    bool Save()
    {
        if (m_Class == "") return false;
        FileHandle f = OpenFile(Path(), FileMode.WRITE);
        if (f == 0) return false;
        FPrintln(f, "scalars," + m_WheelRadiusM.ToString() + ",0," + m_WheelbaseM.ToString());
        // ★★ LA EVIDENCIA DE QUE NO REPTA TAMBIEN ES UNA MEDICION, Y TIENE QUE SOBREVIVIR.
        // Estaba solo en memoria: se medi­a en la calibracion y se tiraba al guardar, asi que
        // la corrida siguiente cargaba el envelope con el contador en cero y volvia a creer
        // que NO SE MIDIO. "Mide cero" pasaba a "no midio" en cada reinicio -- y con eso el
        // objetivo de arrastre se caia al prior de caja para siempre.
        if (m_NoCreepCnt > 0) FPrintln(f, "nocreep," + m_NoCreepCnt.ToString());
        int sb;
        for (sb = 0; sb < STX_BUCKETS; sb++)
        {
            if (m_StxCnt.Get(sb) > 0) FPrintln(f, "stx2," + sb.ToString() + "," + m_StxSum.Get(sb).ToString() + "," + m_StxCnt.Get(sb).ToString());
        }
        int i;
        for (i = 0; i < SNBINS; i++)
        {
            if (m_SteerHist.Get(i) > 0) FPrintln(f, "shist," + i.ToString() + "," + m_SteerHist.Get(i).ToString());
        }
        for (i = 0; i < CNBINS; i++)
        {
            if (m_CreepHist.Get(i) > 0) FPrintln(f, "chist2," + i.ToString() + "," + m_CreepHist.Get(i).ToString());
        }
        for (i = 0; i < LNBINS; i++)
        {
            if (m_LaunchHist.Get(i) > 0) FPrintln(f, "lhist," + i.ToString() + "," + m_LaunchHist.Get(i).ToString());
        }
        for (i = 0; i < LNBINS; i++)
        {
            if (m_LaunchCal.Get(i) > 0) FPrintln(f, "lcal," + i.ToString() + "," + m_LaunchCal.Get(i).ToString());
        }
        for (i = 0; i < m_LaunchRuns.Count(); i++)
        {
            FPrintln(f, "lcalrun," + m_LaunchRuns.Get(i));
        }
        for (i = 0; i < ANBINS; i++)
        {
            if (m_AccelHist.Get(i) > 0) FPrintln(f, "ahist," + i.ToString() + "," + m_AccelHist.Get(i).ToString() + "," + m_AccelMin.ToString() + "," + m_AccelMax.ToString());
        }
        for (i = 0; i < NGEARS; i++)
        {
            if (m_RatioCnt.Get(i) > 0) FPrintln(f, "ratio," + i.ToString() + "," + m_RatioSum.Get(i).ToString() + "," + m_RatioCnt.Get(i).ToString() + "," + m_RatioMin.Get(i).ToString() + "," + m_RatioMax.Get(i).ToString());
        }
        for (i = 0; i < NBINS; i++)
        {
            if (m_KCnt.Get(i) > 0) FPrintln(f, "k," + i.ToString() + "," + m_KSum.Get(i).ToString() + "," + m_KCnt.Get(i).ToString());
        }
        for (i = 0; i < NBINS * 2; i++)
        {
            if (m_CoastCnt.Get(i) > 0) FPrintln(f, "coast2," + i.ToString() + "," + m_CoastSum.Get(i).ToString() + "," + m_CoastCnt.Get(i).ToString());
        }
        // lo ajeno vuelve al archivo, al final, tal cual entro.
        for (i = 0; i < m_Extra.Count(); i++)
        {
            FPrintln(f, m_Extra.Get(i));
        }
        CloseFile(f);
        Print("[Envelope] guardado: " + Path());
        return true;
    }

    // las lineas de kinds que este Store no parsea (se conservan tal cual). El conductor de moto lee de aca los suyos
    // (aut, bfrena, rpmkmh: los escribe envelope_emit.py desde la toma de la moto)
    array<string> Ajenas()
    {
        return m_Extra;
    }

    bool Load()
    {
        if (m_Class == "") return false;
        if (!FileExist(Path())) return false;
        FileHandle f = OpenFile(Path(), FileMode.READ);
        if (f == 0) return false;
        string line;
        m_Extra.Clear();                          // lo ajeno se toma del archivo, sin duplicar
        m_LaunchRuns.Clear();                     // idem las firmas de corridas
        while (FGets(f, line) >= 0)
        {
            if (line == "") continue;
            ParseLine(line);
        }
        CloseFile(f);
        Print("[Envelope] cargado: " + Path() + " -> " + Summary());
        return true;
    }

    // los kinds que ESTE Store parsea. Todo lo demas es ajeno y se conserva (m_Extra).
    private bool KindPropio(string kind)
    {
        if (kind == "scalars") return true;
        if (kind == "nocreep") return true;
        if (kind == "stx2") return true;
        if (kind == "stx") return true;
        if (kind == "shist") return true;
        if (kind == "chist2") return true;
        if (kind == "chist") return true;
        if (kind == "lhist") return true;
        if (kind == "lcal") return true;
        if (kind == "lcalrun") return true;
        if (kind == "ahist") return true;
        if (kind == "accel") return true;
        if (kind == "k") return true;
        if (kind == "ratio") return true;
        if (kind == "coast2") return true;
        if (kind == "coast") return true;
        return false;
    }

    private void ParseLine(string line)
    {
        TStringArray p = new TStringArray();
        Split(line, p);
        if (p.Count() < 2) return;
        string kind = p.Get(0);
        if (!KindPropio(kind))
        {
            m_Extra.Insert(line);                 // ajeno, se conserva tal cual
            return;
        }
        if (kind == "nocreep" && p.Count() >= 2)
        {
            m_NoCreepCnt = p.Get(1).ToInt();
            return;
        }
        // `lcalrun,<firma>` tiene DOS campos: va ACA, antes del `if (p.Count < 3) return;` generico de mas
        // abajo (como `nocreep`). Estaba despues y nunca se cargaba: cada corrida se comparaba contra nada.
        if (kind == "lcalrun" && p.Count() >= 2)
        {
            m_LaunchRuns.Insert(p.Get(1));
            return;
        }
        if (kind == "scalars" && p.Count() >= 4)
        {
            m_WheelRadiusM = p.Get(1).ToFloat();
            // El slot 2 era el steerMax cuando era un MAXIMO PELADO. Se descarta: pudo
            // quedar clavado por una sola lectura basura. Ahora vive en shist.
            float viejoSteer = p.Get(2).ToFloat();
            if (viejoSteer > 0.001) Print("[Envelope] steerMax previo DESCARTADO (" + viejoSteer.ToString() + " rad): era un maximo pelado. Se remide.");
            m_WheelbaseM = p.Get(3).ToFloat();
            // El 5to campo era el aMax cuando era un MAXIMO corrido. Se descarta: venia
            // trincado por ruido y sin muestras para promediar. Se remide.
            if (p.Count() >= 5) Print("[Envelope] aMax previo DESCARTADO: era un maximo corrido, no un promedio. Se remide.");
            return;
        }
        // ⚠ ESTA GUARDA ESTABA ARRIBA DE LAS RAMAS DE HISTOGRAMA Y LAS MATABA A TODAS.
        // `shist`, `chist` y `lhist` tienen TRES campos (kind, bin, cuenta); `ahist` tiene
        // cinco. Con el `p.Count() < 4` adelante, las tres primeras se descartaban al
        // cargar y solo sobrevivia el aMax -- por eso el envelope mostraba
        // `steerMax 0 deg(0) · creep 0kmh(0) · launch 0(0)` aunque el CSV las tuviera
        // escritas. Y era DESTRUCTIVO: la corrida siguiente arrancaba de cero y volvia a
        // guardar, PISANDO lo que la anterior habia medido.
        // Ahora el minimo de campos lo pide cada rama, que es la que sabe cuantos usa.
        if (p.Count() < 3) return;
        int idx = -1;
        float sum = 0;
        int cnt = 0;
        if (p.Count() >= 4)
        {
            idx = p.Get(1).ToInt();
            sum = p.Get(2).ToFloat();
            cnt = p.Get(3).ToInt();
        }
        if (kind == "stx")
        {
            if (!m_AvisoStxViejo)
            {
                m_AvisoStxViejo = true;
                Print("[Envelope] transferencia previa DESCARTADA: se media en TRANSICIONES del volante, no en meseta. Hay que recalibrar.");
            }
            return;
        }
        if (kind == "stx2" && p.Count() >= 4 && idx >= 0 && idx < STX_BUCKETS)
        {
            m_StxSum.Set(idx, sum);
            m_StxCnt.Set(idx, cnt);
            return;
        }
        if (kind == "k" && p.Count() >= 4 && idx >= 0 && idx < NBINS)
        {
            m_KSum.Set(idx, sum);
            m_KCnt.Set(idx, cnt);
            return;
        }
        if (kind == "accel")
        {
            // Formato previo (suma/cuenta de un promedio sin compuertas). Se descarta.
            Print("[Envelope] aMax previo DESCARTADO: promedio sin compuerta de regimen. Se remide.");
            return;
        }
        if (kind == "shist" && p.Count() >= 3)
        {
            int sb2 = p.Get(1).ToInt();
            if (sb2 >= 0 && sb2 < SNBINS)
            {
                int sc = p.Get(2).ToInt();
                m_SteerHist.Set(sb2, sc);
                m_SteerCnt = m_SteerCnt + sc;
            }
            return;
        }
        if (kind == "chist")
        {
            // Formato previo: muestras juntadas MANEJANDO, antes de acotar el creep al
            // banco. Ahi entraron velocidades de coasteo (12.375 km/h en un vehiculo que
            // no repta). Se descarta y se remide.
            Print("[Envelope] creep previo DESCARTADO: se media manejando, no en el banco. Se remide.");
            return;
        }
        if (kind == "chist2" && p.Count() >= 3)
        {
            int cb = p.Get(1).ToInt();
            if (cb >= 0 && cb < CNBINS)
            {
                int cc = p.Get(2).ToInt();
                m_CreepHist.Set(cb, cc);
                m_CreepCnt = m_CreepCnt + cc;
            }
            return;
        }
        if (kind == "lcal" && p.Count() >= 3)
        {
            int qb = p.Get(1).ToInt();
            if (qb >= 0 && qb < LNBINS)
            {
                int qc = p.Get(2).ToInt();
                m_LaunchCal.Set(qb, qc);
                m_LaunchCalCnt = m_LaunchCalCnt + qc;
            }
            return;
        }
        if (kind == "lhist" && p.Count() >= 3)
        {
            int lb = p.Get(1).ToInt();
            if (lb >= 0 && lb < LNBINS)
            {
                int lc = p.Get(2).ToInt();
                m_LaunchHist.Set(lb, lc);
                m_LaunchCnt = m_LaunchCnt + lc;
            }
            return;
        }
        if (kind == "ahist" && p.Count() >= 3)
        {
            int ab = p.Get(1).ToInt();
            if (ab >= 0 && ab < ANBINS)
            {
                int ac = p.Get(2).ToInt();
                m_AccelHist.Set(ab, ac);
                m_AccelCnt = m_AccelCnt + ac;
            }
            if (p.Count() >= 5)
            {
                m_AccelMin = p.Get(3).ToFloat();
                m_AccelMax = p.Get(4).ToFloat();
            }
            return;
        }
        if (kind == "ratio" && p.Count() >= 4 && idx >= 0 && idx < NGEARS)
        {
            m_RatioSum.Set(idx, sum);
            m_RatioCnt.Set(idx, cnt);
            if (p.Count() >= 6)
            {
                m_RatioMin.Set(idx, p.Get(4).ToFloat());
                m_RatioMax.Set(idx, p.Get(5).ToFloat());
            }
            else
            {
                // ⚠ FORMATO VIEJO: SE DESCARTA, no se hereda.
                // Esas muestras se juntaron SIN los gates de marcha estable, continuidad
                // de rpm y omega-del-chasis, y sin registrar dispersion -- son justo las
                // que dieron 27.88 / 14.42 / 25.99 para la misma marcha. Aceptarlas
                // porque "algo es mejor que nada" es exactamente al reves: lo medido
                // manda sobre el prior, asi que heredar un promedio sucio lo IMPONE
                // sobre un prior honesto, y encima ya no se puede saber que estaba mal.
                // Se vuelve a medir; una calibracion cuesta una corrida.
                m_RatioSum.Set(idx, 0.0);
                m_RatioCnt.Set(idx, 0);
                Print("[Envelope] ratio de la marcha " + idx.ToString() + " DESCARTADO: formato previo a los gates de cambio de marcha. Se remide en la proxima calibracion.");
            }
            return;
        }
        // ⚠ EL TAG CAMBIO A coast2 PORQUE EL DATO VIEJO ESTA CONTAMINADO.
        // El banco guardaba la desaceleracion CRUDA (planeo + gravedad) y filtraba sobre el
        // crudo, asi que en una ruta con bajadas el promedio quedaba ~4x por debajo del
        // planeo real (medido: 0.29-0.37 guardado contra 1.44-1.54 real). Cargarlo en
        // silencio seria peor que no tenerlo: el controlador planifica sobre eso.
        if (kind == "coast")
        {
            if (!m_AvisoCoastViejo)
            {
                m_AvisoCoastViejo = true;
                Print("[Envelope] coast previo DESCARTADO: se media SIN compensar pendiente (planeo + gravedad). Hay que recalibrar.");
            }
            return;
        }
        if (kind == "coast2" && p.Count() >= 4 && idx >= 0 && idx < NBINS * 2)
        {
            m_CoastSum.Set(idx, sum);
            m_CoastCnt.Set(idx, cnt);
        }
    }

    private void Split(string line, TStringArray outArr)
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

    // Cuantos bins tienen medicion CONFIABLE. Es lo que hay que mirar para saber si el
    // envelope ya sirve o si falta manejar mas.
    int KBinsReady()
    {
        int i;
        int n = 0;
        for (i = 0; i < NBINS; i++)
        {
            if (HasK(i)) n++;
        }
        return n;
    }

    int CoastBinsReady()
    {
        int i;
        int n = 0;
        for (i = 0; i < NBINS * 2; i++)
        {
            if (m_CoastCnt.Get(i) >= MIN_SAMPLES) n++;
        }
        return n;
    }

    // DIAGNOSTICO por marcha. El ratio de 1ra salio contradictorio (3.94 a 16.10, factor
    // 4.09) y el gate lo descarto -- bien -- pero descartar no explica. Un factor 4 es
    // demasiado para patinada: huele a muestras de OTRA marcha entrando al indice
    // equivocado. Esto imprime lo que hay en CADA indice para que la proxima corrida
    // muestre la atribucion en vez de que yo la adivine.
    string RatioReport()
    {
        string s = "[Envelope] ratios por marcha (indice DayZ: 0=reversa, 2=1ra):";
        int i;
        for (i = 0; i < NGEARS; i++)
        {
            if (m_RatioCnt.Get(i) <= 0) continue;
            float prom = m_RatioSum.Get(i) / m_RatioCnt.Get(i);
            s = s + "\n   marcha " + i.ToString() + ": " + m_RatioCnt.Get(i).ToString() + " muestras";
            s = s + "  prom " + prom.ToString();
            s = s + "  rango " + m_RatioMin.Get(i).ToString() + " a " + m_RatioMax.Get(i).ToString();
            if (RatioIsConsistent(i)) s = s + "  OK";
            else s = s + "  ⚠ CONTRADICTORIO (x" + (m_RatioMax.Get(i) / m_RatioMin.Get(i)).ToString() + ") -> descartado";
        }
        return s;
    }

    string Summary()
    {
        string s = "k " + KBinsReady().ToString() + "/" + NBINS.ToString() + " bins";
        s = s + " · coast " + CoastBinsReady().ToString() + "/" + (NBINS * 2).ToString() + " bins";
        s = s + " · rueda " + m_WheelRadiusM.ToString() + " m";
        s = s + " · steerMax " + (SteerMaxRad() * Math.RAD2DEG).ToString() + " deg(" + m_SteerCnt.ToString() + ")";
        s = s + " · aMax(p50) " + MaxAccelMs2().ToString() + " m/s2";
        s = s + " [" + m_AccelCnt.ToString() + " muestras · p50 " + AccelPercentile(0.50).ToString();
        s = s + " · p90 " + AccelPercentile(0.90).ToString() + " · p99 " + AccelPercentile(0.99).ToString();
        s = s + " · rango " + m_AccelMin.ToString() + " a " + m_AccelMax.ToString() + "]";
        s = s + " · creep " + CreepKmh().ToString() + "kmh(chist2 " + m_CreepCnt.ToString() + " vs nocreep " + m_NoCreepCnt.ToString() + ")";
        if (CreepIsZeroMeasured()) s = s + " [NO REPTA: " + m_NoCreepCnt.ToString() + " muestras quieto]";
        s = s + " · launch " + LaunchFloor().ToString() + "(" + m_LaunchCalCnt.ToString() + " cal)";
        s = s + " · rampa al arrancar " + LaunchRampMedian().ToString() + "(" + m_LaunchCnt.ToString() + ")";
        s = s + " · ratio 1ra " + RatioForGear(2).ToString();
        if (m_RatioCnt.Get(2) >= MIN_SAMPLES && !RatioIsConsistent(2))
        {
            s = s + " ⚠ DESCARTADO: muestras contradictorias (" + m_RatioMin.Get(2).ToString();
            s = s + " a " + m_RatioMax.Get(2).ToString() + ") -> se usa el prior";
        }
        return s;
    }
}
