// ============================================================================
//  BZBusStopSign - anchor de proximidad para las paradas del bus.
//  Sin ActionBase, sin variables replicadas.
//  El cliente manda su posicion al servidor, que matchea con el waypoint mas cercano.
// ============================================================================

class BZBusStopSign extends House {
    protected string m_StopName;

    void SetStopName(string name) { m_StopName = name; }
    string GetStopName()          { return m_StopName; }

    bool CanBeDamaged() { return false; }
}
