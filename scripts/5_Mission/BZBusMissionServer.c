modded class MissionServer {
    override void OnInit() {
        super.OnInit();
        BZBusService.GetInstance().Init();
        BZRouteCleanup.GetInstance().Init();
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
