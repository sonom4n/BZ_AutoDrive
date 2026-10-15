// ============================================================================
//  BZRetiro - NUNCA se borra un vehiculo con alguien sentado. Existe en toda version que no sea la 1.29 (en la 1.29
//  CleanupEntities borra como siempre: meses sin una caida).
//
//  MEDIDO en la 1.30 (server Exp, banco de borrado del laboratorio de motos (herramienta aparte), sin cliente): borrar un vehiculo con un Man SENTADO tumba
//  el server SIEMPRE (Access violation nativa, la misma firma en los 4 casos del banco y en la corrida real); bajarlo,
//  borrarlo y borrar el vehiculo en un cuadro posterior, 50 de 50 sin caida. Y "todo en el mismo cuadro" tampoco es
//  seguro: el delete es diferido y el orden efectivo no esta garantizado (cayo una corrida real con ese orden).
//
//    paso 1  frenar el vehiculo; el runner ya lo solto (puede respawnear en el acto)
//    paso 2  bajar a LO NUESTRO (chofer, crew, guardias): GetOutVehicle; a un eAI, antes, se le borra el pedido de
//            subida pendiente (eAI lo cumple apenas el bot queda a pie y lo volveria a sentar: el re-embarque fantasma)
//    paso 3  cada bot que queda AFUERA (fuera de la tripulacion y sin comando de vehiculo)
//    paso 4  se borra en el acto; a los 3 s, JumpOutVehicle a los que sigan; a los 5 s se borran igual
//    paso 5  en un cuadro POSTERIOR, y solo si el vehiculo esta VACIO, se borra el vehiculo. Si hay alguien que no es
//            nuestro (un jugador, un cadaver, un bot ajeno), el vehiculo NO se borra: queda frenado y sin runner, y se
//            reintenta cada 5 s; a los 10 min se deja en el mundo, y se dice
//    paso 6  retiro terminado
//  Cada paso deja una linea [CLEANUP] con el segundo del retiro: si algo se cae, la ultima linea dice en que paso.
// ============================================================================
#ifndef DAYZ_1_29
class BZRetiro
{
    private static ref array<ref BZRetiro> s_Vivos;
    private static int s_Seq;
    static const int   POLL_MS = 100;
    static const float BAJAR_TOPE_S = 3.0;
    static const float SALTO_TOPE_S = 5.0;
    static const int   VEHICULO_TRAS_MS = 500;
    static const int   AJENOS_REINTENTO_MS = 5000;
    static const float AJENOS_TOPE_S = 600.0;
    static const float AJENOS_AVISO_CADA_S = 60.0;

    private EntityAI          m_Veh;
    private ref array<Human>  m_Bots;
    private string            m_Rotulo;
    private int               m_Id;
    private float             m_T0;
    private bool              m_Salto;
    private float             m_TAjenos;
    private float             m_TAjenosAviso;
    private bool              m_Hecho;

    // cuantos estan sentados en un vehiculo (0 si no es un vehiculo)
    static int Ocupantes(Object o)
    {
        Transport tr = Transport.Cast(o);
        if (!tr) return 0;
        int n = 0;
        for (int i = 0; i < tr.CrewSize(); i++)
        {
            if (tr.CrewMember(i)) n++;
        }
        return n;
    }

    static void Retirar(EntityAI veh, array<Human> bots, string rotulo)
    {
        if (!s_Vivos) s_Vivos = new array<ref BZRetiro>();
        // los terminados se sueltan aca, no adentro de su propio metodo (no borrar el objeto mientras corre)
        for (int i = s_Vivos.Count() - 1; i >= 0; i--)
        {
            BZRetiro viejo = s_Vivos.Get(i);
            if (!viejo || viejo.m_Hecho) s_Vivos.RemoveOrdered(i);
        }
        BZRetiro r = new BZRetiro();
        s_Seq = s_Seq + 1;
        r.m_Id = s_Seq;
        r.m_Veh = veh;
        r.m_Bots = new array<Human>();
        if (bots)
        {
            for (int k = 0; k < bots.Count(); k++)
            {
                if (bots.Get(k)) r.m_Bots.Insert(bots.Get(k));
            }
        }
        r.m_Rotulo = rotulo;
        r.m_T0 = GetGame().GetTickTime();
        s_Vivos.Insert(r);
        r.Empezar();
    }

    static string Seg(float s)
    {
        float red = Math.Round(s * 10) / 10;
        return red.ToString();
    }

    private string Pre()
    {
        float t = GetGame().GetTickTime() - m_T0;
        return "[CLEANUP] retiro " + m_Id.ToString() + " (" + m_Rotulo + ") t+" + Seg(t) + " s · ";
    }

    private void Frenar()
    {
        BZVehiculo vR = BZVehiculoFabrica.Para(Transport.Cast(m_Veh));
        if (!vR) return;
        vR.Gas(0);
        vR.Frenar(1.0);
        vR.FrenoEstacionar(true);
    }

    private bool EsNuestro(Human h)
    {
        return m_Bots.Find(h) >= 0;
    }

    // los sentados en el vehiculo que NO son nuestros (jugadores, cadaveres, bots ajenos)
    private int Ajenos()
    {
        Transport tr = Transport.Cast(m_Veh);
        if (!tr) return 0;
        int n = 0;
        for (int i = 0; i < tr.CrewSize(); i++)
        {
            Human h = tr.CrewMember(i);
            if (h && !EsNuestro(h)) n++;
        }
        return n;
    }

    private bool Afuera(Human b)
    {
        if (!b) return true;
        Transport tr = Transport.Cast(m_Veh);
        if (tr)
        {
            for (int i = 0; i < tr.CrewSize(); i++)
            {
                if (tr.CrewMember(i) == b) return false;
            }
        }
        if (b.GetCommand_Vehicle()) return false;
        return true;
    }

    private void Bajar(Human b)
    {
        if (!b) return;
        HumanCommandVehicle hcv = b.GetCommand_Vehicle();
        if (!hcv) return;
#ifdef DZ_Expansion_AI_Preload
        eAIBase ai = eAIBase.Cast(b);
        if (ai) ai.Notify_Transport(null, -1);   // sin pedido de subida pendiente: si no, eAI lo vuelve a sentar a pie
#endif
        hcv.GetOutVehicle();
    }

    private void Empezar()
    {
        string tipo = "(sin vehiculo)";
        if (m_Veh) tipo = m_Veh.GetType();
        BZBusLog.Info(Pre() + "paso 1: " + tipo + " frenado y soltado · nuestros " + m_Bots.Count().ToString() + " · ocupantes ajenos " + Ajenos().ToString());
        Frenar();
        int bajando = 0;
        for (int i = 0; i < m_Bots.Count(); i++)
        {
            Human b = m_Bots.Get(i);
            if (b && b.GetCommand_Vehicle())
            {
                Bajar(b);
                bajando++;
            }
        }
        BZBusLog.Info(Pre() + "paso 2: GetOutVehicle a " + bajando.ToString() + " bot(s) sentado(s)");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EsperarAfuera, POLL_MS, false);
    }

    private void BorrarBot(Human b, string como)
    {
        BZBusLog.Info(Pre() + "paso 4: delete " + b.GetType() + " (" + como + ")");
        b.Delete();
    }

    private void EsperarAfuera()
    {
        float t = GetGame().GetTickTime() - m_T0;
        Frenar();
        for (int i = m_Bots.Count() - 1; i >= 0; i--)
        {
            Human b = m_Bots.Get(i);
            if (!b)
            {
                m_Bots.RemoveOrdered(i);
                continue;
            }
            if (Afuera(b))
            {
                BorrarBot(b, "afuera");
                m_Bots.RemoveOrdered(i);
            }
        }
        if (m_Bots.Count() == 0)
        {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoVehiculo, VEHICULO_TRAS_MS, false);
            return;
        }
        if (!m_Salto && t >= BAJAR_TOPE_S)
        {
            m_Salto = true;
            for (int j = 0; j < m_Bots.Count(); j++)
            {
                Human bj = m_Bots.Get(j);
                if (!bj) continue;
                HumanCommandVehicle hj = bj.GetCommand_Vehicle();
                if (hj) hj.JumpOutVehicle();
            }
            BZBusLog.Warn(Pre() + "paso 3: " + m_Bots.Count().ToString() + " no bajaron en " + Seg(BAJAR_TOPE_S) + " s -> JumpOutVehicle");
        }
        if (t >= SALTO_TOPE_S)
        {
            BZBusLog.Warn(Pre() + "paso 3: " + m_Bots.Count().ToString() + " siguen adentro a los " + Seg(SALTO_TOPE_S) + " s -> se borran igual (el vehiculo espera a estar vacio)");
            for (int k = m_Bots.Count() - 1; k >= 0; k--)
            {
                Human bk = m_Bots.Get(k);
                if (bk) BorrarBot(bk, "seguia adentro");
            }
            m_Bots.Clear();
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoVehiculo, VEHICULO_TRAS_MS, false);
            return;
        }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EsperarAfuera, POLL_MS, false);
    }

    private void PasoVehiculo()
    {
        if (!m_Veh)
        {
            BZBusLog.Info(Pre() + "paso 6: retiro terminado (sin vehiculo)");
            m_Hecho = true;
            return;
        }
        int ocupados = Ocupantes(m_Veh);
        if (ocupados > 0)
        {
            Frenar();
            if (m_TAjenos == 0 || m_TAjenos - m_TAjenosAviso >= AJENOS_AVISO_CADA_S)
            {
                m_TAjenosAviso = m_TAjenos;
                BZBusLog.Warn(Pre() + "paso 5: " + ocupados.ToString() + " ocupante(s) sentado(s) -> el vehiculo NO se borra (reintento cada " + (AJENOS_REINTENTO_MS / 1000).ToString() + " s, tope " + Seg(AJENOS_TOPE_S) + " s)");
            }
            if (m_TAjenos >= AJENOS_TOPE_S)
            {
                BZBusLog.Warn(Pre() + "paso 6: TOPE con " + ocupados.ToString() + " ocupante(s): el vehiculo queda en el mundo, frenado y sin runner");
                m_Hecho = true;
                return;
            }
            m_TAjenos = m_TAjenos + AJENOS_REINTENTO_MS / 1000.0;
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoVehiculo, AJENOS_REINTENTO_MS, false);
            return;
        }
        BZBusLog.Info(Pre() + "paso 5: delete vehiculo (vacio)");
        m_Veh.Delete();
        m_Veh = null;
        BZBusLog.Info(Pre() + "paso 6: retiro terminado");
        m_Hecho = true;
    }
}
#endif
