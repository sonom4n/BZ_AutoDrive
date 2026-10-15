// ============================================================================
//  BZCompatSinDabs - lo que AutoDrive usaba de Dabs Framework sin saberlo.
//
//  Expansion trae Dabs Framework, y Dabs agrega DayZGame.GetPlayerByIdentity (las 11 respuestas de RPC del panel lo
//  usan). En un server sin Expansion (la 1.30 experimental) ese metodo no existe y el modulo World no compila: lo
//  encontro el chequeo de tipos contra la 1.30 sin mods, no el mapa por nombre (no dice "Expansion" ni "eAI").
//  Aca se declara con la MISMA receta de Dabs, y solo cuando Dabs NO esta (con Dabs, este archivo queda vacio).
// ============================================================================
#ifndef DabsFramework
modded class DayZGame
{
    DayZPlayer GetPlayerByIdentity(PlayerIdentity identity)
    {
        if (!IsMultiplayer()) return DayZPlayer.Cast(GetPlayer());
        if (!identity) return null;
        int low;
        int high;
        GetPlayerNetworkIDByIdentityID(identity.GetPlayerId(), low, high);
        return DayZPlayer.Cast(GetObjectByNetworkId(low, high));
    }
}
#endif
