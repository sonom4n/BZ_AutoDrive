// ============================================================================
//  BZCleanupConfig - shape de data/wrecks_cleanup.json
//
//  BZRouteCleanup hace sweep a lo largo de una secuencia de Anchors (lista de
//  puntos [x, z] en orden). Entre cada par de anchors consecutivos interpola
//  puntos cada SweepStride metros y en cada punto suprime los objetos cuyo
//  classname matchea algun pattern.
//
//  Si Anchors esta vacio, BZRouteCleanup hace fallback a los waypoints del
//  BZBusService (util cuando la ruta del bus ya esta mapeada).
// ============================================================================

class BZAnchor {
    float x;
    float z;
}

class BZCleanupConfig {
    ref array<string> Patterns       = new array<string>();
    ref array<ref BZAnchor> Anchors  = new array<ref BZAnchor>();
    float SweepRadius = 25.0;
    float SweepStride = 20.0;
}
