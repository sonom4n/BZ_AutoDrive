// ============================================================================
// LA CORREA DURA (BZ_AutoDrive)
//
//  El anillo de waypoints ordena DONDE patrullar, pero no impide PERSEGUIR: medido in-game, dos guardias
//  salieron detras del jugador y se alejaron cientos de metros -- el exploit que el usuario habia predicho
//  ("lo pongo en combate a 300 m, se dispersan y dejan el botin libre").
//
//  ⚠ Y NO SE ARREGLA DESDE AFUERA. La FSM de combate de eAI escribe el destino de movimiento EN CADA TICK:
//      eAIState_Fighting_Positioning.OnUpdate -> unit.OverrideTargetPosition(target)
//  asi que una correa por software a 2 Hz la pisa la FSM 20 frames despues. (Esto CORRIGE la hipotesis que
//  estaba anotada como "OverrideTargetPosition lo respeta aun en combate": no lo respeta, gana el ultimo que
//  escribe.) El unico lugar donde se puede ganar el tick es ADENTRO del bot, despues del super: por eso esto
//  es un `modded class eAIBase` y no una llamada del servicio.
//
//  Que hace: si el guardia esta fuera de su radio, el destino de movimiento vuelve a ser el punto del anillo
//  MAS CERCANO (proyeccion radial) -- vuelve caminando por el camino corto. No se le toca el arma, ni el
//  blanco, ni la deteccion: sigue disparando a lo que vea, tambien afuera del radio. Es exactamente lo que
//  pidio el analista: "un guardia puede disparar a lo que sea, pero no puede salir del radio".
//
//  Sin correa puesta (radio 0) esto no hace NADA: cualquier otro eAI del server (Quest, AICamp, el chofer)
//  no se entera de que esta clase existe.
// ============================================================================
modded class eAIBase
{
    private vector m_BzCorreaCentro;
    private float  m_BzCorreaRadio;    // 0 = sin correa
    // 1: VOLVER GANA SOBRE EL BLANCO. Al replegar, el que esta tiroteando a algo no caminaba:
    // la misma FSM que sacaba del anillo le pisa el destino. Este modo pone el punto del vehiculo SIEMPRE,
    // pelee o no. Puede seguir disparando mientras camina; lo que no puede es que un infectado le gane a la
    // orden de subir. (Medido: "dos se entretuvieron matando zombies y el vehiculo partio sin ellos".)
    private vector m_BzVolverPos;
    private bool   m_BzVolviendo;
    // LOS INFECTADOS NO EXISTEN PARA LA ESCOLTA, y el usuario pidio que sea MUTUO.
    private bool   m_BzIgnoraInfectados;

    // PONER LA CORREA ES TAMBIEN DEJAR DE "VOLVER A LA PUERTA".
    // BzVolverA() deja m_BzVolviendo en true hasta que alguien llame BzSacarCorrea(), y CommandHandler,
    // con ese flag, pisa el destino del bot CADA tick (despues del super: le gana al anillo y al combate,
    // a proposito). Si un embarque terminaba por un camino que no lo limpiaba, el bot llegaba a la
    // proxima parada todavia "volviendo" a la puerta de la ANTERIOR: ni anillo ni pelea.
    // Devuelve true si traia uno viejo, para que el service lo diga en el log.
    bool BzPonerCorrea(vector centro, float radio)
    {
        bool traiaVolver = m_BzVolviendo;
        m_BzVolviendo = false;
        m_BzCorreaCentro = centro;
        m_BzCorreaRadio = radio;
        return traiaVolver;
    }

    void BzSacarCorrea()
    {
        m_BzCorreaRadio = 0;
        m_BzVolviendo = false;
    }

    // El destino manda sobre el combate hasta que se lo saque (al sentarse).
    void BzVolverA(vector destino)
    {
        m_BzVolverPos = destino;
        m_BzVolviendo = true;
        m_BzCorreaRadio = 0;   // para volver al vehiculo hay que poder salir del anillo
    }

    void BzIgnorarInfectados(bool ign)
    {
        m_BzIgnoraInfectados = ign;
    }

    // La otra mitad de "que los zombies no existan": que el INFECTADO no lo elija. La IA del motor pregunta
    // esto antes de tomar un blanco (`EntityAI.CanBeTargetedByAI`, y PlayerBase ya lo usa para no ser blanco
    // estando inconsciente o adentro de un vehiculo). Respondiendo que no a las criaturas, el guardia deja de
    // ser blanco de los infectados SIN tocar a los infectados: no cambia nada para el resto del server.
    override bool CanBeTargetedByAI(EntityAI ai)
    {
        if (m_BzIgnoraInfectados && ai && ai.IsDayZCreature()) return false;
        return super.CanBeTargetedByAI(ai);
    }

    float BzCorreaRadio()
    {
        return m_BzCorreaRadio;
    }

    // Cuanto se paso del radio (0 = adentro). Lo usa el servicio para poder DECIR en el log que un guardia
    // se estaba yendo, que es una de las tres cosas que el usuario vio y el log no contaba.
    float BzFueraDeCorrea()
    {
        if (m_BzCorreaRadio <= 0) return 0;
        vector pF = GetPosition();
        float dxF = pF[0] - m_BzCorreaCentro[0];
        float dzF = pF[2] - m_BzCorreaCentro[2];
        float dF = Math.Sqrt(dxF * dxF + dzF * dzF);
        if (dF <= m_BzCorreaRadio) return 0;
        return dF - m_BzCorreaRadio;
    }

    override void CommandHandler(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
    {
        super.CommandHandler(pDt, pCurrentCommandID, pCurrentCommandFinished);
        if (!IsAlive()) return;
        if (IsInTransport()) return;
        // los infectados que ya hubiera enganchado se sueltan, y se marcan para que no los vuelva a tomar
        if (m_BzIgnoraInfectados)
        {
            int nTI = TargetCount();
            for (int ti = nTI - 1; ti >= 0; ti--)
            {
                eAITarget tgI = GetTarget(ti);
                if (!tgI) continue;
                EntityAI eI = tgI.GetEntity();
                if (!eI) continue;
                if (!eI.IsDayZCreature()) continue;
                eAI_ThreatOverride(eI, true);
                eAI_RemoveTarget(tgI);
            }
        }
        if (m_BzVolviendo)
        {
            OverrideTargetPosition(m_BzVolverPos);   // DESPUES del super: el vehiculo le gana al blanco
            return;
        }
        if (m_BzCorreaRadio <= 0) return;
        vector pC = GetPosition();
        float dxC = pC[0] - m_BzCorreaCentro[0];
        float dzC = pC[2] - m_BzCorreaCentro[2];
        float dC = Math.Sqrt(dxC * dxC + dzC * dzC);
        if (dC <= m_BzCorreaRadio) return;
        if (dC < 0.01) return;
        float kC = m_BzCorreaRadio / dC;
        vector destC = pC;
        destC[0] = m_BzCorreaCentro[0] + dxC * kC;
        destC[2] = m_BzCorreaCentro[2] + dzC * kC;
        destC[1] = GetGame().SurfaceY(destC[0], destC[2]);
        OverrideTargetPosition(destC);   // DESPUES del super: este es el valor que queda para el proximo frame
    }
}
