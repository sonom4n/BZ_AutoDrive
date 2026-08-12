# BZ_AutoDrive

**Conducción autónoma de vehículos NPC para DayZ — desde una sola vuelta grabada.**

Grabás una ruta manejándola vos —o la dibujás en el editor—. Un NPC (Boris) la maneja para siempre, con la **física real de cada vehículo**. No entrena un modelo por vehículo ni pide waypoints a mano: **lee la configuración que el auto ya declara** en el motor (curva de torque, caja, dirección, geometría) y maneja según eso. **Maneja cualquier vehículo sin configurar nada por-auto.**

> Versión EN: [README.en.md](README.en.md) · Licencia **MIT** · Proyecto **BrigadaZ**

---

## La idea en una frase

**Grabás una ruta —o la dibujás en el editor—, el framework lee el config del vehículo, y el NPC la maneja — en cualquier auto, sin configurar nada por-vehículo, sin programar ni entrenar.**

## Qué lo hace distinto

- **Config-como-manual:** en vez de aprender la dinámica con datos, lee la física declarada del vehículo. Sin machine learning, **determinista e inspeccionable**.
- **Cualquier vehículo, sin tuning:** el control lee la física declarada de cada auto y maneja según eso — delantera / trasera / central / 4x4 / **camión** — con **una sola configuración**, sin ajustes por-vehículo. Validado out-of-sample en un banco amplio de tracciones y tamaños. *(Una **grabación** es de su vehículo; para cross-vehículo, dibujás la **traza** en el editor y le asignás el que quieras.)*
- **Física real:** el NPC aplica volante, acelerador y freno reales sobre el vehículo simulado — se ve y se siente como una persona manejando, no un teleport scripteado.
- **Maneja lo difícil:** curvas cerradas, reversa, cambios de sentido (K-turn) **auto-detectados**, y parada final precisa (típicamente **< 0.5 m**, hasta ~1 m en los más grandes).
- **Escala:** carga de rutas **~1700× más rápida** (cambio de formato de datos) + **múltiples vehículos concurrentes sin mover el FPS** del server.

## Qué incluye

- **Pipeline de autoría** usable por un admin no-programador: grabar → convertir (wizard) → ejecutar → orquestar.
- **Editor de rutas** visual (`tools/editor/`) para dibujar, unir y afinar recorridos sobre el mapa.
- **Routing emergente sobre grafo:** componé muchas tomas y ruteá pares A→B nunca grabados enteros (mapear un pueblo).
- **DSL de escenarios** (eventos/triggers/verbos por waypoint) + **integración con DayZ-Expansion-Quests** (convoyes, emboscadas, escoltas).
- **UI / Reproductor:** tablero en vivo multi-vehículo, carga de rutas en caliente sin reiniciar.
- **Remera branded** (classname `BZ_AutoDrive_TShirt`) — un cosmético del proyecto, incluido en el mod.

---

## Documentación

| Documento | Para quién | Qué es |
|---|---|---|
| [MANUAL_BZ_AutoDrive.md](MANUAL_BZ_AutoDrive.md) | Admins / modders | Manual práctico: del primer grabado a misiones con Quests |
| [AI_KNOWLEDGE_PACK.md](AI_KNOWLEDGE_PACK.md) | Tu asistente IA | Contexto técnico completo para subir a Claude/GPT/Gemini y que te guíe |

*(Cada uno con su versión EN `.en.md`.)*

---

## Cómo empezar

**Si sos jugador:** suscribite. Vas a ver el transporte circulando; subite y te lleva.

**Si sos admin de server:**
1. Agregá `@BZ_AutoDrive` a tus mods activos (+ las dependencias de abajo).
2. Los controles viven en `Opciones → Controles → "BZ AutoDrive"` (rebindeables). Son **3**: **Abrir panel** (INICIO), **Grabar** (NUMPAD 5), **Marcar evento/parada** (NUMPAD 4). La reversa y los cambios de sentido se **auto-detectan** — no marcás nada.
3. El loop: **grabás** una ruta manejando vos → el **wizard** la convierte leyendo el config → la **corrés** desde el Reproductor. Guía paso a paso en el manual (§5).

> La **primera vez** que abrís el wizard (`tools\Wizard.bat`) te pide tus **paths** (carpeta de rutas del server + dónde grabás) — los seteás **una sola vez** y se los acuerda.

**Si sos modder:** subí el [AI knowledge pack](AI_KNOWLEDGE_PACK.md) a tu asistente IA — trae walkthroughs para grabar/convertir/correr, mapear zonas (unir grafos) y armar quests con vehículos.

> **⚠️ Permisos de terceros:** referenciar un vehículo modded por **classname** (el mod se carga aparte, no se toca) **no** requiere permiso. **Repackear o extraer** assets de un tercero **sí** requiere permiso explícito del autor. Es la norma de la comunidad DayZ. Ante la duda: referenciá, no extraigas.

---

## Arquitectura (el cimiento)

El framework se engancha a `CarScript` con **override-last**: deja correr al receptor de eAI y **sobrescribe después** los inputs con los que computa su stack de control. Como `CarScript` se hereda por toda la familia de vehículos, **cualquier auto que la extienda queda manejable por un bot sin escribir una línea por vehículo**.

El chofer sigue la traza con un controlador **pure-pursuit** (apunta a un punto adelante sobre la línea), regula la velocidad por la **curvatura de lo que viene**, y saca acelerador/freno/marcha de un **modelo inverso derivado del config del vehículo**. No es replay de frames — es control real, cuadro a cuadro.

```
PathLogger (grabás)  →  Wizard (convierte leyendo el config)  →  Control stack (pure-pursuit + curvatura + modelo inverso)  →  Reproductor (corrés)
```

## Dependencias

**Requeridas:**

- **DayZ-Expansion-Core**
- **DayZ-Expansion-AI (eAI)** — el cuerpo del NPC conductor.
- **DayZ-Expansion-Vehicles**
- **DayZ-Expansion-Quests**

**Mods de vehículos del Workshop** — cualquiera que extienda `CarScript` (la mayoría) funciona automáticamente, sin tocar nada.

## Compilar desde el source

Para usarlo: suscribite en el Workshop (ya viene firmado). Para compilar tu fork: empaquetá el PBO como prefieras —DayZ Tools (AddonBuilder) o tu propio pipeline— y firmalo con tu **propia** llave. Cada modder empaqueta a su manera.

## Estado

**v1.0** — control **unificado** (un solo stack, sin modos), conducción forward/cruise + **reversa resuelta** (pure-pursuit geométrico), **cambios de sentido auto-detectados**, **parada final resuelta** (endpoint < 0.5 m típico, validado out-of-sample en FWD/RWD/central/4x4/camión), **routing emergente sobre grafo**, **DSL de eventos**, **integración con Quests** (convoy/emboscada/escolta), **multi-runner**, UI/Reproductor, wizard de conversión.

**Roadmap:** grafo + pathfinding in-game (hoy offline), wizard 100 % autónomo, adapter de boat/heli, archetype ferroviario, conductor con LLM.

---

## Licencia

**MIT.** Libre uso, modificación, repack y redistribución. Sin atribución obligatoria. Los componentes de terceros referenciados como dependencias (DayZ-Expansion, mods de vehículos) mantienen sus propias licencias.

## Créditos

Desarrollado por **Sonom4n** e **Hiperhipo10** — **BrigadaZ PVE Server**. Desarrollo asistido por IA (pair-programming).

## Apoyar el proyecto

El framework es **libre, con o sin aporte**. Si te sirve y querés bancar el desarrollo de las próximas versiones: **[paypal.me/Sonom4n](https://paypal.me/Sonom4n)**.

---

*Está bajo licencia MIT, y no por descuido: es una postura. Tomalo, estudialo, modificalo, **repackealo**, publicá tu versión — no vas a necesitar mi permiso. Lo que se comparte libre no se roba: se multiplica. Una comunidad no crece guardándose las cosas; crece pasándolas de mano en mano. Si esto te sirve de base para algo mejor, ya cumplió su propósito.*
