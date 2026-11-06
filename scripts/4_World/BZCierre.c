// ============================================================================
//  BZCierre - desde que una corrida termina hasta que el retiro borra el vehiculo, NADIE SUBE: el cierre de Expansion (FORCEDLOCKED).
//
//  Medido (04/11): la bajada (BZBajar) deja al jugador parado a metro y medio de la puerta justo cuando arranca el retiro; en los 2 s
//  entre el paso 1 y el paso 5 se volvio a subir, los dos caminos del conteo dieron 0 a mitad de la subida, el vehiculo se borro y el
//  server cayo al simular al jugador (la firma del 22/10). El cierre lo corta de raiz: el `modded ActionGetInTransport` de Expansion
//  devuelve false con `IsLocked()` en el cliente Y en el server (la accion ni se ofrece, y si el pedido llega igual el server la niega), y
//  `m_LockState` es una variable de red del vehiculo (`RegisterNetSyncVariableInt`), asi que al cliente le llega con la siguiente
//  sincronizacion: por eso el retiro espera ESPERA_S desde el cierre antes de borrar. Bajarse y cambiar de asiento NO miran el cierre
//  (Expansion no lo pide en `ActionGetOutTransport` ni en `ActionSwitchSeats`), y `GetOutVehicle` (la juncture de BZBajar, los bots del
//  retiro) es un comando, no una accion: el cierre no frena ninguna bajada.
//
//  Solo cuando el vehiculo va al retiro o al respawn: un fin que lo deja en el mundo (una falla que decide un mod, un quest) no lo cierra;
//  si despues entra al retiro, el retiro lo cierra en su paso 1. Si el retiro llega a su tope y deja el vehiculo en el mundo, se reabre al
//  estado que tenia. Si ya estaba cerrado (una llave, otro mod), no se toca ni se reabre. Una linea [CIERRE] al cerrar y al reabrir.
// ============================================================================
class BZCierreEstado
{
    EntityAI m_Veh;
    ExpansionVehicleLockState m_Previo;
    bool  m_Puesto;    // lo cerramos nosotros (si ya estaba cerrado no se toca, y no se reabre)
    float m_T;         // cuando se cerro
}

class BZCierre
{
    static const float ESPERA_S = 1.5;   // desde el cierre hasta poder borrar: la variable de red viaja con la proxima sincronizacion del vehiculo

    private static ref array<ref BZCierreEstado> s_Estados;

    private static BZCierreEstado Buscar(EntityAI veh)
    {
        if (!s_Estados) s_Estados = new array<ref BZCierreEstado>();
        for (int i = s_Estados.Count() - 1; i >= 0; i--)
        {
            BZCierreEstado e = s_Estados.Get(i);
            if (!e || !e.m_Veh)
            {
                s_Estados.RemoveOrdered(i);   // el vehiculo ya no existe
                continue;
            }
            if (e.m_Veh == veh) return e;
        }
        return null;
    }

    // cierra a jugadores. true = quedo cerrado (ahora o ya lo estaba). Una sola vez por vehiculo: el FIN y despues el retiro no lo repiten
    static bool Cerrar(EntityAI veh, string rotulo)
    {
        if (!veh) return false;
        if (Buscar(veh)) return true;
        ExpansionVehicle exv;
        if (!ExpansionVehicle.Get(exv, veh) || !exv)
        {
            BZBusLog.Warn("[CIERRE] " + rotulo + ": " + veh.GetType() + " no es un vehiculo de Expansion: no se lo puede cerrar a jugadores (queda la guarda del conteo del retiro)");
            return false;
        }
        BZCierreEstado e = new BZCierreEstado();
        e.m_Veh = veh;
        e.m_Previo = exv.GetLockState();
        e.m_T = GetGame().GetTickTime();
        e.m_Puesto = false;
        int previo = e.m_Previo;
        if (!exv.IsLocked())
        {
            exv.SetLockState(ExpansionVehicleLockState.FORCEDLOCKED);
            e.m_Puesto = exv.IsLocked();
            if (!e.m_Puesto)
            {
                BZBusLog.Warn("[CIERRE] " + rotulo + ": " + veh.GetType() + " no quedo cerrado (falta el modulo Vehicles de Expansion?): queda la guarda del conteo del retiro");
                return false;
            }
        }
        s_Estados.Insert(e);
        if (e.m_Puesto) BZBusLog.Info("[CIERRE] " + rotulo + ": " + veh.GetType() + " CERRADO a jugadores (FORCEDLOCKED; estaba en " + previo.ToString() + ") hasta que el retiro lo borre");
        else BZBusLog.Info("[CIERRE] " + rotulo + ": " + veh.GetType() + " ya estaba cerrado (estado " + previo.ToString() + "): se deja como esta y no se reabre");
        return true;
    }

    // hace cuanto lo cerramos; -1 si no lo cerramos nosotros
    static float CerradoHaceS(EntityAI veh)
    {
        BZCierreEstado e = Buscar(veh);
        if (!e) return -1;
        return GetGame().GetTickTime() - e.m_T;
    }

    // el vehiculo queda en el mundo (el tope del retiro): vuelve al estado que tenia, si lo cerramos nosotros
    static void Reabrir(EntityAI veh, string rotulo)
    {
        BZCierreEstado e = Buscar(veh);
        if (!e) return;
        if (e.m_Puesto)
        {
            ExpansionVehicle exv;
            if (ExpansionVehicle.Get(exv, veh) && exv)
            {
                exv.SetLockState(e.m_Previo);
                int previo = e.m_Previo;
                BZBusLog.Info("[CIERRE] " + rotulo + ": " + veh.GetType() + " REABIERTO al estado que tenia (" + previo.ToString() + ")");
            }
        }
        Olvidar(veh);
    }

    // el vehiculo se borra: nada que reabrir
    static void Olvidar(EntityAI veh)
    {
        BZCierreEstado e = Buscar(veh);
        if (!e) return;
        int i = s_Estados.Find(e);
        if (i >= 0) s_Estados.RemoveOrdered(i);
    }
}
