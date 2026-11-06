// ============================================================================
//  BZEmbudo - UNA SOLA PUERTA para las operaciones pesadas de todo el framework: una por cuadro.
//
//  Medido con la sonda de cuadros (Badlands con Trader, 5 a 8 vehiculos): el 38 % de los cuadros de mas de 50 ms eran relanzamientos
//  nuestros, y en todos caian juntas 2 o mas cosas en el mismo cuadro (leer la ruta, crear el vehiculo, el chofer, su ropa, el crew).
//  Aca todo eso hace fila: cada runner pide su operacion y el embudo corre UNA por cuadro, en orden de llegada, desde PerfTick (el unico
//  gancho por cuadro del framework). Una operacion puede pedir "no antes de" (el embarque espera 1 s al motor): la que no esta lista
//  no tapa a las demas.
//
//  Lo que pasa por aca: leer la ruta (OP_LEER), el respawn (OP_RESPAWN: limpiar + vehiculo), el vehiculo solo (OP_SPAWN), el chofer con su
//  ropa y el arranque de los relojes (OP_CHOFER), cada bot del crew (OP_CREW), el embarque (OP_BOARD) y cada paso del armado del cerebro
//  (OP_CONFIG). El retiro no hace fila: pide el cuadro con PedirCuadro y, si esta tomado, vuelve en 100 ms.
//  ⛔ NUNCA entra: el manejo (OnInput, el tick del controlador), el AR y el freno. Un vehiculo en marcha no espera turno.
//
//  Con la cola de carga (BZCargaCola, las tajadas): cuando el embudo tiene algo listo, los cuadros pares son suyos y las tajadas toman
//  los impares; sin nada listo, las tajadas corren en todos. Nunca las dos en el mismo cuadro.
//
//  El tiempo: un lanzamiento tarda unos cuadros mas que antes (leer, vehiculo, chofer: 3 cuadros si no hay nadie delante; con 15 lanzados
//  juntos, el ultimo chofer sale ~45 cuadros despues del primero, menos de 1 s a 60 fps). BusDrive y el Trader preguntan en el mismo
//  cuadro en que lanzan: RunnerEsperandoCabecera() responde true mientras el lanzamiento esta en el embudo.
// ============================================================================
class BZEmbudoOp
{
    BZBusService m_Runner;   // sin ref: el runner vive en s_Runners; si se va, queda null y la operacion se descarta
    int   m_Tipo;
    float m_NoAntes;         // GetTickTime desde el que puede correr (0 = ya)
    int   m_CuadroPedido;
}

class BZEmbudo
{
    static const int OP_LEER    = 1;
    static const int OP_RESPAWN = 2;
    static const int OP_SPAWN   = 3;
    static const int OP_CHOFER  = 4;
    static const int OP_CREW    = 5;
    static const int OP_BOARD   = 6;
    static const int OP_CONFIG  = 7;

    private static ref array<ref BZEmbudoOp> s_Cola;
    private static int s_Cuadro;              // cuenta de cuadros, la lleva PerfTick
    private static int s_CuadroUsado = -1;    // el cuadro en que ya corrio una operacion pesada
    // lo que mide, para la sonda de cuadros y para el RPT
    private static int    s_Ejecutadas;
    private static int    s_PeorEspera;
    private static int    s_Rechazos;         // PedirCuadro que dijo no
    private static int    s_UltimaEspera = -1;
    private static int    s_UltimoTipo;
    private static string s_UltimoQuien = "";
    private static int    s_VEjecutadas;
    private static int    s_VPeorEspera;
    private static int    s_VRechazos;
    private static float  s_VPeorMs;

    static string Nombre(int tipo)
    {
        if (tipo == OP_LEER) return "leer";
        if (tipo == OP_RESPAWN) return "respawn";
        if (tipo == OP_SPAWN) return "spawn";
        if (tipo == OP_CHOFER) return "chofer";
        if (tipo == OP_CREW) return "crew";
        if (tipo == OP_BOARD) return "embarque";
        if (tipo == OP_CONFIG) return "config";
        return "op" + tipo.ToString();
    }

    // ---------------------------------------------------------------- el cuadro
    static void NuevoCuadro()
    {
        s_Cuadro = s_Cuadro + 1;
    }

    static int Cuadro()
    {
        return s_Cuadro;
    }

    // Quien va a hacer algo pesado fuera de la cola (el retiro) pide el cuadro: true si nadie lo uso todavia.
    static bool PedirCuadro(string quien)
    {
        if (s_CuadroUsado == s_Cuadro)
        {
            s_Rechazos = s_Rechazos + 1;
            s_VRechazos = s_VRechazos + 1;
            return false;
        }
        s_CuadroUsado = s_Cuadro;
        s_UltimoQuien = quien;
        return true;
    }

    // ---------------------------------------------------------------- pedir
    static void Pedir(BZBusService r, int tipo)
    {
        PedirEn(r, tipo, 0);
    }

    // ms: no antes de esos milisegundos. La misma operacion del mismo runner no se pide dos veces: vale la primera.
    static void PedirEn(BZBusService r, int tipo, int ms)
    {
        if (!r) return;
        if (!s_Cola) s_Cola = new array<ref BZEmbudoOp>();
        int i;
        for (i = 0; i < s_Cola.Count(); i++)
        {
            BZEmbudoOp ya = s_Cola.Get(i);
            if (ya && ya.m_Runner == r && ya.m_Tipo == tipo) return;
        }
        BZEmbudoOp op = new BZEmbudoOp();
        op.m_Runner = r;
        op.m_Tipo = tipo;
        op.m_NoAntes = 0;
        if (ms > 0) op.m_NoAntes = GetGame().GetTickTime() + ms / 1000.0;
        op.m_CuadroPedido = s_Cuadro;
        s_Cola.Insert(op);
    }

    // el runner se va (retiro, botin): sus operaciones pendientes no deben correr sobre nada
    static void Cancelar(BZBusService r)
    {
        if (!s_Cola) return;
        int i = s_Cola.Count() - 1;
        while (i >= 0)
        {
            BZEmbudoOp op = s_Cola.Get(i);
            if (!op || !op.m_Runner || op.m_Runner == r) s_Cola.RemoveOrdered(i);
            i = i - 1;
        }
    }

    static int Pendientes()
    {
        if (!s_Cola) return 0;
        int n = 0;
        int i;
        for (i = 0; i < s_Cola.Count(); i++)
        {
            BZEmbudoOp op = s_Cola.Get(i);
            if (op && op.m_Runner) n = n + 1;
        }
        return n;
    }

    // hay una operacion que ya puede correr
    static bool HayLista()
    {
        return IndiceLista() >= 0;
    }

    // con algo listo, los cuadros pares son del embudo; los impares, de la carga en tajadas
    static bool LeToca()
    {
        if (!HayLista()) return false;
        return s_Cuadro % 2 == 0;
    }

    // la mas vieja de las listas; de paso suelta las de runners que ya no existen
    private static int IndiceLista()
    {
        if (!s_Cola) return -1;
        float ahora = GetGame().GetTickTime();
        int i = 0;
        while (i < s_Cola.Count())
        {
            BZEmbudoOp op = s_Cola.Get(i);
            if (!op || !op.m_Runner)
            {
                s_Cola.RemoveOrdered(i);
                continue;
            }
            if (op.m_NoAntes <= ahora) return i;
            i = i + 1;
        }
        return -1;
    }

    // ---------------------------------------------------------------- correr
    // Una operacion por cuadro, la mas vieja que este lista. Devuelve los ms que costo (0 si no corrio nada).
    static float Drenar()
    {
        int k = IndiceLista();
        if (k < 0) return 0;
        if (!PedirCuadro("embudo")) return 0;
        BZEmbudoOp op = s_Cola.Get(k);
        s_Cola.RemoveOrdered(k);
        int espera = s_Cuadro - op.m_CuadroPedido;
        s_Ejecutadas = s_Ejecutadas + 1;
        s_VEjecutadas = s_VEjecutadas + 1;
        if (espera > s_PeorEspera) s_PeorEspera = espera;
        if (espera > s_VPeorEspera) s_VPeorEspera = espera;
        s_UltimaEspera = espera;
        s_UltimoTipo = op.m_Tipo;
        s_UltimoQuien = op.m_Runner.NombreLog();
        int tk0 = TickCount(0);
        op.m_Runner.EjecutarOp(op.m_Tipo);
        float ms = TickCount(tk0) / 10000.0;
        if (ms > s_VPeorMs) s_VPeorMs = ms;
        if (espera >= 2 || op.m_Tipo == OP_RESPAWN || op.m_Tipo == OP_SPAWN)   // la lectura va en tajadas: su resumen lo da el runner
        {
            float msR = Math.Round(ms * 10.0) / 10.0;
            string l = "[EMBUDO] " + s_UltimoQuien + " " + Nombre(op.m_Tipo) + ": " + msR.ToString() + " ms";
            l = l + " · espero " + espera.ToString() + " cuadro(s) · quedan " + Pendientes().ToString();
            BZBusLog.Info(l);
        }
        return ms;
    }

    // ---------------------------------------------------------------- para la sonda de cuadros
    // lo que paso en este cuadro: "" si el embudo no corrio nada
    static string ResumenCuadro()
    {
        if (s_CuadroUsado != s_Cuadro) return "";
        string r = s_UltimoQuien;
        if (s_UltimaEspera >= 0) r = r + " " + Nombre(s_UltimoTipo) + " espero " + s_UltimaEspera.ToString() + " cuadro(s)";
        return r + " · pendientes " + Pendientes().ToString();
    }

    // lo acumulado desde la ultima vez que se pidio (la ventana de 30 s de la sonda); se reinicia
    static string ResumenVentana()
    {
        float peorMs = Math.Round(s_VPeorMs * 10.0) / 10.0;
        string r = "ejecutadas " + s_VEjecutadas.ToString() + " · peor espera " + s_VPeorEspera.ToString() + " cuadros";
        r = r + " · peor op " + peorMs.ToString() + " ms · cuadros negados a otros " + s_VRechazos.ToString() + " · pendientes " + Pendientes().ToString();
        s_VEjecutadas = 0;
        s_VPeorEspera = 0;
        s_VRechazos = 0;
        s_VPeorMs = 0;
        return r;
    }

    static int Ejecutadas()  { return s_Ejecutadas; }
    static int PeorEspera()  { return s_PeorEspera; }
    static int Rechazos()    { return s_Rechazos; }
}
