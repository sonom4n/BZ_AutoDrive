// ============================================================================
//  BZBajar - bajar a un jugador sentado en un vehiculo nuestro cuando la corrida termina (FIN de cualquier tipo, STOP del panel o
//  del dueno) y en el retiro, si todavia queda alguien sentado. Sin aviso al jugador: si el admin quiere avisar, marca un evento.
//
//  En la 1.30 el server solo NO puede bajar a un jugador vivo (CrewGetOut sobre un vivo deja comando sin plaza: el crash del 22/10).
//  El que lo baja es su propio cliente. El camino medido es una SYNC JUNCTURE propia (SJ_BAJAR): SendSyncJuncture corre en el server
//  y en el cliente dueno en el mismo paso de simulacion, y en los dos lados el handler (modded PlayerBase.OnSyncJuncture -> Juncture)
//  hace GetOutVehicle del comando de vehiculo: el gesto exacto de ActionGetOutTransport.OnStart y de la moto en el agua. La plaza la
//  suelta el motor al terminar el comando. Medido con el laboratorio: 3 de 3 (sedan con la puerta abierta y con la puerta cerrada, y
//  un pasajero del bus), el jugador a pie a los 2 s. El RPC al cliente para que arranque la accion vanilla NO anduvo (el cliente no ve
//  la puerta abierta a tiempo).
//
//  La puerta se abre antes solo por como se ve (en autos): la bajada no depende de ella. Con el vehiculo en marcha no se baja a nadie
//  (la accion vanilla salta a partir de 8 km/h); en transicion (subiendo o bajando) se vuelve a mirar medio segundo despues; a un
//  inconsciente no se lo baja (al despertar el motor lo vuelve a sentar con la plaza vieja: ese es el estado del crash). A los 3 s se
//  mira si quedo abajo y, si sigue sentado, va una segunda juncture; despues queda el retiro, que lo vuelve a pedir cada minuto y
//  NUNCA borra un vehiculo con alguien sentado (BZRetiro). Una linea [BAJAR] por jugador: a quien, de que vehiculo, de que plaza, y si
//  quedo abajo.
// ============================================================================
class BZBajar
{
    static const int   SJ_BAJAR = 150;         // sync juncture propia: vanilla usa 0-26 y 200, Expansion reserva 100
    static const int   MAGIA = 1113808433;     // "BZB1": el primer campo, por si otro mod usa el mismo id
    static const int   MODO_BAJAR = 0;         // GetOutVehicle (otros modos fueron del laboratorio: no se mandan ni se aplican)
    static const float EN_MARCHA_KMH = 5.0;    // mas rapido que esto no se baja a nadie
    static const int   REINTENTO_MS = 500;     // en transicion: se vuelve a mirar despues de esto...
    static const int   REINTENTOS_MAX = 10;    // ...hasta tantas veces (5 s)
    static const int   VERIFICAR_MS = 3000;    // a los 3 s se mira si quedo abajo (medido: a pie a los 2 s)
    static const int   VUELTAS_MAX = 2;        // junctures por pedido: la primera y, si sigue sentado, una mas

    private static ref BZBajar s_I;

    static BZBajar I()
    {
        if (!s_I) s_I = new BZBajar();
        return s_I;
    }

    // a cada jugador vivo con identidad sentado en este vehiculo (por los dos caminos de BZOcupantes). Devuelve cuantos pidio.
    static int BajarSentados(EntityAI veh, string rotulo)
    {
        if (!GetGame().IsServer()) return 0;
        Transport tr = Transport.Cast(veh);
        if (!tr) return 0;
        array<Man> jugadores = new array<Man>();
        GetGame().GetPlayers(jugadores);
        int n = 0;
        for (int j = 0; j < jugadores.Count(); j++)
        {
            PlayerBase pb = PlayerBase.Cast(jugadores.Get(j));
            if (!pb || !pb.GetIdentity() || !pb.IsAlive()) continue;
            if (!BZOcupantes.SentadoEn(pb, tr)) continue;
            n = n + 1;
            I().Pedir(pb, tr, rotulo, 0, 1);
        }
        return n;
    }

    // quien y donde, para el log
    static string Quien(PlayerBase pb, Transport tr)
    {
        string q = "?";
        if (pb.GetIdentity()) q = pb.GetIdentity().GetName();
        return q + " en " + tr.GetType() + " (plaza " + tr.CrewMemberIndex(pb).ToString() + ")";
    }

    // la juncture a un jugador. intento: reintentos por transicion; vuelta: 1 la primera, 2 la segunda tras verificar
    void Pedir(PlayerBase pb, Transport tr, string rotulo, int intento, int vuelta)
    {
        if (!pb || !tr) return;
        if (!BZOcupantes.SentadoEn(pb, tr)) return;   // ya bajo (o se bajo solo)
        string quien = Quien(pb, tr);
        if (pb.IsUnconscious())
        {
            BZBusLog.Info("[BAJAR] " + rotulo + ": " + quien + " esta inconsciente: no se lo baja (el retiro espera)");
            return;
        }
        float kmh = GetVelocity(tr).Length() * 3.6;
        if (kmh > EN_MARCHA_KMH)
        {
            BZBusLog.Info("[BAJAR] " + rotulo + ": " + quien + " · el vehiculo va a " + kmh.ToString() + " km/h: no se lo baja en marcha");
            return;
        }
        HumanCommandVehicle hcv = pb.GetCommand_Vehicle();
        if (hcv && hcv.IsTransitioning())
        {
            if (intento >= REINTENTOS_MAX)
            {
                BZBusLog.Info("[BAJAR] " + rotulo + ": " + quien + " sigue en transicion tras " + intento.ToString() + " intentos: se deja");
                return;
            }
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.Pedir, REINTENTO_MS, false, pb, tr, rotulo, intento + 1, vuelta);
            return;
        }
        AbrirPuerta(pb, tr);
        ScriptJunctureData ctx = new ScriptJunctureData();
        ctx.Write(MAGIA);
        ctx.Write(MODO_BAJAR);
        pb.SendSyncJuncture(SJ_BAJAR, ctx);
        BZBusLog.Info("[BAJAR] " + rotulo + ": juncture " + vuelta.ToString() + " a " + quien);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.Verificar, VERIFICAR_MS, false, pb, tr, rotulo, vuelta);
    }

    // a los 3 s: quedo abajo, o sigue sentado (y entonces una juncture mas, una sola vez)
    void Verificar(PlayerBase pb, Transport tr, string rotulo, int vuelta)
    {
        if (!pb || !tr)
        {
            BZBusLog.Info("[BAJAR] " + rotulo + ": el jugador o el vehiculo ya no existen");
            return;
        }
        string quien = Quien(pb, tr);
        float d = vector.Distance(pb.GetPosition(), tr.GetPosition());
        if (!BZOcupantes.SentadoEn(pb, tr))
        {
            BZBusLog.Info("[BAJAR] " + rotulo + ": " + quien + " quedo ABAJO, a " + d.ToString() + " m del vehiculo");
            return;
        }
        if (vuelta < VUELTAS_MAX)
        {
            BZBusLog.Info("[BAJAR] " + rotulo + ": " + quien + " sigue sentado a los " + (VERIFICAR_MS / 1000).ToString() + " s: otra juncture");
            Pedir(pb, tr, rotulo, 0, vuelta + 1);
            return;
        }
        BZBusLog.Warn("[BAJAR] " + rotulo + ": " + quien + " sigue sentado tras " + vuelta.ToString() + " junctures: el retiro lo vuelve a pedir y no borra el vehiculo con el adentro");
    }

    // la puerta de su plaza, como ActionCarDoors: solo por como se ve (la bajada no depende de ella)
    private void AbrirPuerta(PlayerBase pb, Transport tr)
    {
        CarScript car = CarScript.Cast(tr);
        if (!car) return;
        int plaza = tr.CrewMemberIndex(pb);
        if (plaza < 0) return;
        string sel = car.GetDoorSelectionNameFromSeatPos(plaza);
        if (sel == "") return;
        string src = car.GetAnimSourceFromSelection(sel);
        if (src == "") return;
        car.SetAnimationPhase(src, 1.0);
    }

    // el handler: llega al cliente dueno y al server en el mismo paso; en los dos, el comando de vehiculo termina (el motor hace el
    // CrewGetOut al terminar el comando, como en la accion vanilla). Devuelve true si la juncture era nuestra.
    static bool Juncture(PlayerBase pb, ParamsReadContext pCtx)
    {
        int magia = 0;
        int modo = 0;
        if (!pCtx.Read(magia) || magia != MAGIA) return false;
        if (!pCtx.Read(modo)) return false;
        if (modo != MODO_BAJAR) return true;
        string lado = "cliente";
        if (GetGame().IsDedicatedServer()) lado = "server";
        HumanCommandVehicle hcv = pb.GetCommand_Vehicle();
        if (!hcv)
        {
            if (GetGame().IsDedicatedServer()) BZBusLog.Info("[BAJAR] juncture en el " + lado + ": sin comando de vehiculo, no se aplica");
            return true;
        }
        if (hcv.IsTransitioning())
        {
            if (GetGame().IsDedicatedServer()) BZBusLog.Info("[BAJAR] juncture en el " + lado + ": en transicion, no se aplica");
            return true;
        }
        hcv.GetOutVehicle();
        if (!GetGame().IsDedicatedServer() && Car.Cast(hcv.GetTransport())) GetDayZGame().GetBacklit().OnLeaveCar();   // cosmetico, como ActionGetOutTransport
        return true;
    }
}
