modded class MissionServer {
    override void OnInit() {
        super.OnInit();
        // === MARCADOR DE VERSION (dev v1.1) — el PBO v1.0 NO tiene estas lineas. ===
        // Si ves este banner en el RPT del server, estas corriendo v1.1-dev, NO la v1.0.
        // (temporal de dev; sacar/ajustar antes del release de v1.1)
        Print("############################################################################");
        Print("###   BZ AutoDrive  v1.1-dev   (refactor DriveTowards: GUARDS + PLAN)     ###");
        Print("###   Si ves esto en el RPT -> corre v1.1-dev (NO la v1.0 publicada)      ###");
        Print("############################################################################");
        BZBusService.GetInstance().Init();
        BZRouteCleanup.GetInstance().Init();

        // === PATHFINDING (v2, DIFERIDO) ===
        // El load del grafo vial + TestRoute era un proof-of-concept de dev (corria solo en el server
        // del autor, con los CSVs en profile; para subscribers el Load fallaba -> inerte). Se comenta
        // para v1.1 (release de precision/arranque/UI): sin test de boot. BZRoadGraph.c queda como
        // fundacion para retomar el pathfinding en v2.
        // if (BZRoadGraph.GetInstance().Load("$profile:BZ_AutoDrive\\road_graph_chernarus.csv")) {
        //     BZRoadGraph.GetInstance().LoadGeom("$profile:BZ_AutoDrive\\road_geom_chernarus.csv");
        //     BZRoadGraph.GetInstance().TestRoute(18.8, 1586.2, 14370.0, 15195.0);
        // }
    }

    // === INTEGRACION QUEST (fase 2026-06-15) ===
    // Hook de ciclo de vida de Expansion-Quests (declarado en MissionBaseWorld para override).
    // Al arrancar una quest, enganchamos sus bots VIVOS al framework. STEP 1: validar acceso (loguear).
    // Ver [[reference_expansion_quest_api]]. Reparto: Quest=bots+logica, Framework=vehiculo+manejo.
    override void Expansion_OnQuestStart(ExpansionQuest quest) {
        super.Expansion_OnQuestStart(quest);
        if (GetGame() && GetGame().IsServer())
            BZBusService.GetInstance().OnQuestStart(quest);
    }

    // Objetivos completos (ej. convoy entero muerto) -> el framework decide el destino del vehiculo
    // (este caso: despawn). Ver opciones en el manual (cap 7.7) y [[reference_expansion_quest_api]].
    override void Expansion_OnQuestObjectivesComplete(ExpansionQuest quest) {
        super.Expansion_OnQuestObjectivesComplete(quest);
        if (GetGame() && GetGame().IsServer())
            BZBusService.GetInstance().OnQuestComplete(quest);
    }
}
