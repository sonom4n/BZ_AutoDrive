# BZ_AutoDrive

**Conducción autónoma de vehículos NPC para DayZ — desde una sola vuelta grabada.**

Grabás una ruta manejándola vos. Un NPC (Boris) la maneja para siempre, con la **física real de cada vehículo**. No entrena un modelo por vehículo ni pide waypoints a mano: **lee la configuración que el auto ya declara** en el motor (curva de torque, caja, dirección, geometría), **reproduce tu manejo punto por punto** y **aprende de tus propias tomas** cómo se comporta ese vehículo. **Maneja cualquier vehículo sin configurar nada por-auto.**

> Versión EN: [README.en.md](README.en.md) · Licencia **MIT** · Proyecto **BrigadaZ**

---

## La idea en una frase

**Grabás una ruta, el framework lee el config del vehículo y aprende de tu manejo, y el NPC la maneja — en cualquier auto, sin configurar nada por-vehículo, sin programar ni entrenar.**

## Qué lo hace distinto

- **Config como manual, medición como corrección:** lee la física declarada del vehículo y la corrige con lo que **mide de tus tomas** (la *ficha* de cada vehículo): cuando lo declarado y lo medido no coinciden, manda lo medido. Sin machine learning, **determinista e inspeccionable**.
- **Reproduce tu manejo:** la *cinta* guarda volante, acelerador, freno y marcha 40 veces por segundo, indexados por **distancia recorrida**. El NPC hace lo que hiciste **en ese punto del camino**: vuela donde volaste, frena donde frenaste.
- **Cualquier vehículo, sin tuning:** el control lee la física declarada de cada auto y maneja según eso — delantera / trasera / central / 4x4 / **camión** — con **una sola configuración**, sin ajustes por-vehículo. Validado out-of-sample en un banco amplio de tracciones y tamaños. *(Una **grabación** es de su vehículo: para la misma ruta en otro auto, **grabala con ese auto** — así el framework aprende su física.)*
- **Física real:** el NPC aplica volante, acelerador y freno reales sobre el vehículo simulado — se ve y se siente como una persona manejando, no un teleport scripteado.
- **Maneja lo difícil:** curvas cerradas, reversa, cambios de sentido (K-turn) **auto-detectados**, y parada final precisa (a unos **10 cm** del punto grabado). Una ruta solo cuenta como **completa** si el vehículo termina quieto a 0,25 m o menos, sin chocar contra ningún objeto y sin ayuda física del AutoRecovery.
- **Escala:** carga de rutas **~1700× más rápida** (cambio de formato de datos) + **múltiples vehículos concurrentes sin mover el FPS** del server.

## Qué incluye

- **Pipeline de autoría** usable por un admin no-programador: grabar → convertir (wizard) → ejecutar → orquestar.
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

**Si sos modder:** subí el [AI knowledge pack](AI_KNOWLEDGE_PACK.md) a tu asistente IA — trae walkthroughs para grabar/convertir/correr y armar quests con vehículos, y una guía paso a paso para **portar el framework a otro juego** (Apéndice E, con 7 Days to Die como ejemplo resuelto).

> **⚠️ Permisos de terceros:** referenciar un vehículo modded por **classname** (el mod se carga aparte, no se toca) **no** requiere permiso. **Repackear o extraer** assets de un tercero **sí** requiere permiso explícito del autor. Es la norma de la comunidad DayZ. Ante la duda: referenciá, no extraigas.

---

## Arquitectura (el cimiento)

El framework se engancha a `CarScript` con **override-last**: deja correr al receptor de eAI y **sobrescribe después** los inputs con los que computa su stack de control. Como `CarScript` se hereda por toda la familia de vehículos, **cualquier auto que la extienda queda manejable por un bot sin escribir una línea por vehículo**.

El chofer **reproduce tu manejo por posición** (la *cinta*: lo que hiciste en cada punto del camino, no en cada instante), sigue la traza con un controlador **pure-pursuit** (apunta a un punto adelante sobre la línea), y saca acelerador/freno/marcha de un **modelo inverso derivado del config del vehículo y corregido por lo medido** en su ficha. No es un replay por tiempo: la física real hace el resto, cuadro a cuadro.

```
PathLogger (grabás)  →  Wizard (ruta + cinta + ficha del vehículo)  →  Cerebro 1.2 (cinta + pure-pursuit + modelo inverso + lo medido)  →  Reproductor (corrés)
```

## Dependencias

**Requerida: DayZ-Expansion, vía el Bundle** (`@DayZ-Expansion-Bundle`, con sus propias dependencias: CF, Dabs Framework y Expansion-Licensed). El framework necesita tres partes de Expansion y **las tres vienen dentro del Bundle**:

- **AI (eAI)** — el cuerpo del NPC conductor.
- **Vehicles**.
- **Quests** — el mod lo requiere para cargar aunque no hagas misiones.

> ⚠ **No agregues `@DayZ-Expansion-AI` suelto** si ya tenés el Bundle: son las mismas clases en otra versión y **el server no arranca**.

**Mods de vehículos del Workshop** — cualquiera que extienda `CarScript` (la mayoría) funciona automáticamente, sin tocar nada. **En la DayZ 1.30**, un mod de vehículos hecho para la 1.29 necesita que su autor lo actualice (algunos no compilan; otros cargan pero les fallan las puertas, las ruedas o el daño). La 1.2.1 está validada con los vehículos vanilla de la 1.30; los de mods se van sumando a medida que se actualicen.

## Compilar desde el source

Para usarlo: suscribite en el Workshop (ya viene firmado). Para compilar tu fork: empaquetá el PBO como prefieras —DayZ Tools (AddonBuilder) o tu propio pipeline— y firmalo con tu **propia** llave. Cada modder empaqueta a su manera.

## Estado

**v1.2.5** — una parada suave en cualquier punto de la ruta para los mods que usan el framework (`HoldRunnerSuave`: sigue su línea y frena hasta el punto); el panel muestra la versión exacta. Probada sobre DayZ 1.30 y su Update 2.

**v1.2.4** — el panel ya no se abre solo al cerrar el menú de un comerciante con ESC; una parada pedida por otro mod sobre una marca con maniobra de reversa ya no desacomoda la ruta; la reversa cerca de una marca vale (marcá siempre en avance).

**v1.2.3** — los vehículos de AutoDrive ya no quedan guardados en el mundo al cerrar el server; ESC ya no puede quedar como tecla del panel; motos mucho más precisas (siguen la forma del camino, miden cómo responden, paradas de otros mods en el lugar exacto y arranque en 1ra).

**v1.2.2** — arreglo urgente: la 1.2.1 no compilaba junto al CarPack actualizado para la 1.30 (y otros packs con clases de nombre corto). Solo cambian nombres de variables; la conducción es la misma.

**v1.2.1** — **lista para la DayZ 1.30**: un solo paquete corre en la 1.29 y en la 1.30; **motos** (experimental, solo 1.30); **veredicto estricto** del fin de ruta, decidido con el vehículo quieto; frenada final medida en tu toma; cambios de sentido a centímetros; logs livianos por defecto (`Diagnostics`, `BlackBox`). En la 1.30, arrancá el server con un `-profiles` relativo.

**v1.2** — **cerebro de manejo nuevo**: reproduce tu manejo (*cinta*) y **mide** cada vehículo (*ficha*); nadie se arrastra solo en ralentí (medido en 43 vehículos); gas de arranque medido en la flota; **AutoRecovery** que detecta la traba por el avance y rescata también en reversa; cambios de sentido más rápidos; la conversión genera **cuatro** archivos y trae **Python incluido**. Se mantiene de antes: **reversa y cambios de sentido auto-detectados**, **parada final precisa**, **DSL de eventos**, **integración con Quests** (convoy/emboscada/escolta), **multi-runner**, UI/Reproductor. Detalle en el [CHANGELOG](CHANGELOG.md).

**Roadmap:** wizard 100 % autónomo, adapter de boat/heli, archetype ferroviario, conductor con LLM.

---

## Licencia

**MIT.** Libre uso, modificación, repack y redistribución. Sin atribución obligatoria. Los componentes de terceros referenciados como dependencias (DayZ-Expansion, mods de vehículos) mantienen sus propias licencias.

## Créditos

Desarrollado por **Sonom4n** e **Hiperhipo10** — **BrigadaZ PVE Server**. Desarrollo asistido por IA (pair-programming).

## Apoyar el proyecto

El framework es **libre, con o sin aporte**. Si te sirve y querés bancar el desarrollo de las próximas versiones: **[paypal.me/Sonom4n](https://paypal.me/Sonom4n)**.

---

*Está bajo licencia MIT, y no por descuido: es una postura. Tomalo, estudialo, modificalo, **repackealo**, publicá tu versión — no vas a necesitar mi permiso. Lo que se comparte libre no se roba: se multiplica. Una comunidad no crece guardándose las cosas; crece pasándolas de mano en mano. Si esto te sirve de base para algo mejor, ya cumplió su propósito.*
