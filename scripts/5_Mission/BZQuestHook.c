// ============================================================================
//  BZQuestHook - (reservado)
//
//  La subclase `modded class ExpansionQuestObjectiveAICampEvent` NO compila:
//  esa clase Event del objetivo no es visible en el scope 5_Mission de nuestro
//  mod ("Unknown type"), aunque ExpansionQuestModule/ExpansionQuest si lo sean.
//  -> Se revirtio al hook de MissionServer (Expansion_OnQuestStart) con POLLING
//     de QuestPatrolExists hasta que las patrullas aparecen (ver BZBusService
//     OnQuestStart/CheckQuestBots). Solo usa clases que compilan en este scope.
//
//  TODO futuro (si se necesita el instante exacto del spawn): investigar el
//  scope real de los ExpansionQuestObjective*Event y si son moddeables (quizas
//  requieren un scope/addon distinto).
// ============================================================================
