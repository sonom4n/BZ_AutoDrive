// ============================================================================
//  BZExpansionWreckFilter - bloquea spawn de wrecks/garbage de Expansion-Bundle
//
//  Expansion-Bundle tiene una tabla hardcoded en scripts.pbo (~41k entradas
//  tipo "classname|x y z|rot") que se carga al startup via
//  ExpansionWorldObjectsModule.LoadObjects(array<string>, bool).
//
//  Este modded class intercepta esa llamada y FILTRA por classname antes de
//  pasar el array al super: si la entry empieza con "Land_Wreck_",
//  "bldr_wreck_" o "Expansion_Wreck_", se omite. Resultado: cero spawn de
//  wrecks de Expansion, sin necesidad de sweep, sin tocar el .pbo del mod.
//
//  Garbage temporalmente NO se filtra (puede agregarse al patterns abajo).
// ============================================================================

modded class ExpansionWorldObjectsModule {

    static const string BZWRECK_PATTERNS[] = {
        "Land_Wreck_",
        "bldr_wreck_",
        "Expansion_Wreck_"
    };

    override static void LoadObjects(array<string> files, bool addExtension) {
        int beforeCount = files.Count();
        int removed = 0;

        // Iterar al reves para poder borrar elementos sin desfasar indices
        for (int i = files.Count() - 1; i >= 0; i--) {
            string entry = files[i];
            foreach (string p : BZWRECK_PATTERNS) {
                if (entry.IndexOf(p) >= 0) {
                    files.Remove(i);
                    removed++;
                    break;
                }
            }
        }

        if (removed > 0) {
            Print("[BZBus] WreckFilter: bloqueados " + removed + " wrecks de Expansion (de " + beforeCount + " entradas)");
        }

        super.LoadObjects(files, addExtension);
    }
}
