// ============================================================================
//  BZAutCinta - LA AUTORIDAD DE LA CINTA POR FRANJA FINA, POR TOMA Y EN TAJADAS.
//
//  Medido (sonda de cuadros, Badlands con Trader, 15 vehiculos): la moto larga (12.056 cuadros de cinta) tiraba un cuadro de 80-100 ms en
//  cada salida, siempre en la misma linea: la autoridad de la CINTA por franja fina, que el conductor median de una vez en su primer
//  cuadro de manejo (tres pasadas enteras sobre la cinta, unos 7 us por cuadro). Las dos motos del Trader (1.300 y 2.300 cuadros) quedaban
//  en 9-16 ms, por eso no se veia.
//
//  Aca el calculo es el MISMO, en el mismo orden de operaciones (los numeros salen identicos), pero
//    - se guarda POR TOMA: la clave es la cinta (su ruta, su tamano y su huella, las del registro de cargas), no el vehiculo ni la
//      corrida. La misma cinta da los mismos numeros, asi que la segunda salida de la toma no mide nada; y
//    - la primera vez se mide EN TAJADAS, de hasta unos ms por cuadro (AUT_MOTO_MS_POR_CUADRO en el runner), mientras la moto espera
//      como espera la ruta. Es el runner el que la pide (BZAutCinta.Pedir) y la hace avanzar (Avanzar) antes de darle el mando al conductor
//      (TomarAutoridadCinta): la moto no toma el mando sin la autoridad.
//
//  Nada de aca nombra tipos de la moto ni del auto: es un calculo sobre BZCintaRunner, el mismo en la 1.29 y en la 1.30.
//  Las constantes y las franjas son las del conductor (BZConductorMoto.AUTF_*, AUT_*), que sigue siendo quien las usa.
// ============================================================================
class BZAutCintaMedida
{
    // EL RESULTADO: por franja fina (AUTF_N), -1 / -2 / 0 donde no hubo medida
    ref array<float> m_Aut;
    ref array<float> m_R;
    ref array<int>   m_N;
    string m_Linea;         // la linea del RPT, tal cual la imprimia el conductor
    string m_Clave;
    bool   m_Lista;
    bool   m_SinSteering;   // la cinta no trae el steering grabado (o es muy corta): no hay medida y la ley directa usa el envelope
    int    m_Cuadros;       // llamadas de Avanzar que trabajaron
    int    m_Ticks;         // del contador fino (10.000 por ms), sumados sobre esas llamadas
    int    m_Usos;          // conductores que la tomaron
    int    m_Frames;

    // EL CALCULO, por partes: la fase y el cuadro por el que va
    private int   m_Fase;     // 0 abrir, 1 la tecla con slew, 2 la guinada, 3 acumular por franja y retraso, 4 cerrar; 5 lista
    private int   m_K;
    private float m_SlewPrev;
    private ref array<float> m_TeclaSlew;
    private ref array<float> m_DtCuadro;
    private ref array<float> m_Guinada;
    private ref array<float> m_AcN;
    private ref array<float> m_AcSx;
    private ref array<float> m_AcSy;
    private ref array<float> m_AcSxx;
    private ref array<float> m_AcSxy;
    private ref array<float> m_AcSyy;

    static const int PASO = 64;   // cuadros entre dos miradas al reloj

    void BZAutCintaMedida()
    {
        m_Aut = new array<float>();
        m_R = new array<float>();
        m_N = new array<int>();
        int b;
        for (b = 0; b < BZConductorMoto.AUTF_N; b++)
        {
            m_Aut.Insert(-1);
            m_R.Insert(-2);
            m_N.Insert(0);
        }
        m_Linea = "";
        m_Clave = "";
        m_Fase = 0;
        m_K = 0;
        m_SlewPrev = 0;
    }

    bool Lista() { return m_Lista; }

    // de donde salio, para la linea del RPT
    string Origen()
    {
        if (m_Usos > 1) return "de la cache de la toma (uso " + m_Usos.ToString() + ")";
        float msT = m_Ticks / 10000.0;
        return "medida en " + m_Cuadros.ToString() + " cuadro(s), " + BZConductorMoto.Red(msT) + " ms de trabajo";
    }

    // Hasta maxMs de trabajo sobre la cinta (el reloj se mira cada PASO cuadros). true = lista.
    bool Avanzar(BZCintaRunner c, float maxMs)
    {
        if (m_Lista) return true;
        int tk0 = TickCount(0);
        int topeTk = (int)(maxMs * 10000.0);
        m_Cuadros = m_Cuadros + 1;
        bool fin = false;
        while (!fin)
        {
            fin = Tramo(c);
            if (!fin && TickCount(tk0) >= topeTk) break;
        }
        m_Ticks = m_Ticks + TickCount(tk0);
        if (fin)
        {
            m_Lista = true;
            m_TeclaSlew = null;   // lo que solo servia para medir
            m_DtCuadro = null;
            m_Guinada = null;
            m_AcN = null;
            m_AcSx = null;
            m_AcSy = null;
            m_AcSxx = null;
            m_AcSxy = null;
            m_AcSyy = null;
        }
        return m_Lista;
    }

    // Hasta PASO cuadros de la fase en curso. true = no queda nada.
    private bool Tramo(BZCintaRunner c)
    {
        if (m_Fase == 0) return Abrir(c);
        if (m_Fase == 1) return TramoTecla(c);
        if (m_Fase == 2) return TramoGuinada(c);
        if (m_Fase == 3) return TramoAcumular(c);
        if (m_Fase == 4) return Cerrar();
        return true;
    }

    private bool Abrir(BZCintaRunner c)
    {
        m_Frames = c.Frames();
        if (m_Frames < 50 || !c.TieneSteering())
        {
            m_SinSteering = true;
            m_Linea = "[MOTO] autoridad por franja fina: la cinta no trae el steering grabado; la ley directa usa el envelope";
            m_Fase = 5;
            return true;
        }
        m_TeclaSlew = new array<float>();
        m_DtCuadro = new array<float>();
        m_Guinada = new array<float>();
        m_SlewPrev = 0;
        m_K = 0;
        m_Fase = 1;
        return false;
    }

    // pasada 1: el dt de cada cuadro (del arco y la velocidad) y la tecla grabada pasada por el slew del conductor
    private bool TramoTecla(BZCintaRunner c)
    {
        int nF = m_Frames;
        int hasta = m_K + PASO;
        if (hasta > nF) hasta = nF;
        int k;
        for (k = m_K; k < hasta; k++)
        {
            float dtk = -1;
            float kmhK = c.KmhDeFrame(k);
            if (k < nF - 1 && kmhK >= BZConductorMoto.AUTF_KMH_MIN)
            {
                float dsK = c.SDeFrame(k + 1) - c.SDeFrame(k);
                if (dsK > 0.001) dtk = dsK / (kmhK / 3.6);
            }
            if (dtk > 0.2) dtk = -1;   // un salto de la cinta no es un cuadro
            m_DtCuadro.Insert(dtk);
            float pasoSl = 1.0;
            if (dtk > 0) pasoSl = BZConductorMoto.RATE * dtk;
            float dSl = c.SteerDeFrame(k) - m_SlewPrev;
            if (dSl > pasoSl) dSl = pasoSl;
            if (dSl < -pasoSl) dSl = -pasoSl;
            m_SlewPrev = m_SlewPrev + dSl;
            m_TeclaSlew.Insert(m_SlewPrev);
        }
        m_K = hasta;
        if (m_K < nF) return false;
        m_K = 0;
        m_Fase = 2;
        return false;
    }

    // pasada 2: la guinada geometrica grabada (rad/s), centrada
    private bool TramoGuinada(BZCintaRunner c)
    {
        int nF = m_Frames;
        int hasta = m_K + PASO;
        if (hasta > nF) hasta = nF;
        int k;
        for (k = m_K; k < hasta; k++)
        {
            float rG = -99;
            if (k > 0 && k < nF - 1 && m_DtCuadro.Get(k - 1) > 0 && m_DtCuadro.Get(k) > 0)
            {
                float dH = BZConductorMoto.Envolver((c.RumboDeFrame(k + 1) - c.RumboDeFrame(k - 1)) * Math.DEG2RAD);
                rG = dH / (m_DtCuadro.Get(k - 1) + m_DtCuadro.Get(k));
            }
            m_Guinada.Insert(rG);
        }
        m_K = hasta;
        if (m_K < nF) return false;
        int nSlots = BZConductorMoto.AUTF_N * BZConductorMoto.AUTF_LAGS;
        m_AcN = new array<float>();
        m_AcSx = new array<float>();
        m_AcSy = new array<float>();
        m_AcSxx = new array<float>();
        m_AcSxy = new array<float>();
        m_AcSyy = new array<float>();
        int iz;
        for (iz = 0; iz < nSlots; iz++)
        {
            m_AcN.Insert(0);
            m_AcSx.Insert(0);
            m_AcSy.Insert(0);
            m_AcSxx.Insert(0);
            m_AcSxy.Insert(0);
            m_AcSyy.Insert(0);
        }
        m_K = 0;
        m_Fase = 3;
        return false;
    }

    // pasada 3: las sumas de la regresion por franja fina y retraso
    private bool TramoAcumular(BZCintaRunner c)
    {
        int nF = m_Frames;
        int hasta = m_K + PASO;
        if (hasta > nF) hasta = nF;
        int k;
        for (k = m_K; k < hasta; k++)
        {
            if (m_DtCuadro.Get(k) <= 0) continue;
            int bK = BZConductorMoto.FranjaFinaDe(c.KmhDeFrame(k));
            float xK = m_TeclaSlew.Get(k);
            int li;
            for (li = 0; li < BZConductorMoto.AUTF_LAGS; li++)
            {
                int jK = k + BZConductorMoto.RetrasoCuadros(li);
                if (jK >= nF) continue;
                float yK = m_Guinada.Get(jK);
                if (yK < -90) continue;
                int ixK = bK * BZConductorMoto.AUTF_LAGS + li;
                m_AcN.Set(ixK, m_AcN.Get(ixK) + 1);
                m_AcSx.Set(ixK, m_AcSx.Get(ixK) + xK);
                m_AcSy.Set(ixK, m_AcSy.Get(ixK) + yK);
                m_AcSxx.Set(ixK, m_AcSxx.Get(ixK) + xK * xK);
                m_AcSxy.Set(ixK, m_AcSxy.Get(ixK) + xK * yK);
                m_AcSyy.Set(ixK, m_AcSyy.Get(ixK) + yK * yK);
            }
        }
        m_K = hasta;
        if (m_K < nF) return false;
        m_Fase = 4;
        return false;
    }

    // el cierre: por franja, el retraso de mejor correlacion; vale con r >= AUT_R_MIN, AUT_MIN_N cuadros y dentro de AUT_ABS_*
    private bool Cerrar()
    {
        string lCon = "";
        string lSin = "";
        int bF;
        for (bF = 0; bF < BZConductorMoto.AUTF_N; bF++)
        {
            float mejorR = -2;
            float mejorA = -1;
            int mejorN = 0;
            int lg;
            for (lg = 0; lg < BZConductorMoto.AUTF_LAGS; lg++)
            {
                int ixF = bF * BZConductorMoto.AUTF_LAGS + lg;
                float nn = m_AcN.Get(ixF);
                if (nn < BZConductorMoto.AUT_MIN_N) continue;
                float vx = m_AcSxx.Get(ixF) - m_AcSx.Get(ixF) * m_AcSx.Get(ixF) / nn;
                float vy = m_AcSyy.Get(ixF) - m_AcSy.Get(ixF) * m_AcSy.Get(ixF) / nn;
                float cov = m_AcSxy.Get(ixF) - m_AcSx.Get(ixF) * m_AcSy.Get(ixF) / nn;
                if (vx <= 0.000001 || vy <= 0.000001) continue;
                float rr = cov / Math.Sqrt(vx * vy);
                if (rr > mejorR)
                {
                    mejorR = rr;
                    mejorA = cov / vx;
                    mejorN = nn;
                }
            }
            if (mejorR >= BZConductorMoto.AUT_R_MIN && mejorA >= BZConductorMoto.AUT_ABS_MIN && mejorA <= BZConductorMoto.AUT_ABS_MAX)
            {
                m_Aut.Set(bF, mejorA);
                m_R.Set(bF, mejorR);
                m_N.Set(bF, mejorN);
                lCon = lCon + " " + BZConductorMoto.NombreFranjaFina(bF) + " " + BZConductorMoto.Red(mejorA) + " (r " + BZConductorMoto.Red(mejorR) + ", n " + mejorN + ")";
            }
            else
            {
                lSin = lSin + " " + BZConductorMoto.NombreFranjaFina(bF);
            }
        }
        string lA = "[MOTO] autoridad de la CINTA por franja fina (rad/s por unidad; toques con slew " + BZConductorMoto.Red(BZConductorMoto.RATE) + "/s contra la guinada grabada):" + lCon;
        if (lSin != "") lA = lA + " · sin medida (va el envelope):" + lSin;
        m_Linea = lA;
        m_Fase = 5;
        return true;
    }
}

// la cache: una medida por cinta (ruta + tamano + huella), hasta reiniciar el server
class BZAutCinta
{
    private static ref map<string, ref BZAutCintaMedida> s_Mapa;

    static string Clave(string cintaPath, int tam, int huella)
    {
        return cintaPath + "|" + tam.ToString() + "|" + huella.ToString();
    }

    // la medida de esa cinta: la que ya estaba (lista o a medias) o una nueva, vacia
    static BZAutCintaMedida Pedir(string cintaPath, int tam, int huella)
    {
        if (!s_Mapa) s_Mapa = new map<string, ref BZAutCintaMedida>();
        string k = Clave(cintaPath, tam, huella);
        if (s_Mapa.Contains(k)) return s_Mapa.Get(k);
        BZAutCintaMedida m = new BZAutCintaMedida();
        m.m_Clave = k;
        s_Mapa.Insert(k, m);
        return m;
    }

    static int Entradas()
    {
        if (!s_Mapa) return 0;
        return s_Mapa.Count();
    }
}
