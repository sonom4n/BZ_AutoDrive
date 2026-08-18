# Changelog — BZ_AutoDrive

> Versión EN: [CHANGELOG.en.md](CHANGELOG.en.md) · Licencia **MIT** · Proyecto **BrigadaZ**

Lo más nuevo arriba. La **conducción** (modelo *config-como-manual*) se mantiene estable entre versiones — los cambios son de **robustez** y **herramientas**, no del modelo de manejo.

---

## v1.1

**Más precisión + arranque robusto + mejoras del reproductor.** Mismo modelo de conducción (*config-como-manual*), afinado: **paradas más finas** (checkpoints y endpoints), **largadas limpias**, y un tablero en vivo más completo.

### Precisión de paradas — sobre todo en vehículos pesados

- **Endpoints a centímetros.** Nuevo frenado predictivo cuadro-a-cuadro que toma la velocidad grabada como referencia — la parada final clava típicamente a **< 5 cm** (antes < 0.5 m), consistente en toda la flota. La mejora se nota **especialmente en los vehículos pesados** (camiones, blindados, tanques), que antes eran los que más se pasaban.
- **Checkpoints e intercambios más precisos.** Los cambios de sentido (*cusp* adelante↔reversa) clavan el punto antes de invertir — típicamente **< 0.2 m** —, sin pasarse ni quedarse corto. (En el giro más cerrado, los vehículos muy grandes quedan un poco más holgados, por geometría.)

> **Probado en un banco amplio:** deportivos, vanilla, militares y vehículos modded.

### Arranque

- **Largadas limpias, siempre.** Corregido un caso intermitente en el que un vehículo podía quedarse trabado al arrancar —el motor revolucionando pero sin avanzar—. Causa: el freno de mano del estabilizado de spawn no siempre liberaba al pasarle el control al NPC. Ahora el vehículo despega parejo en cualquier caso, en cualquier vehículo.
- **Rutas que salen del mismo punto (hubs).** Nuevo *guard* de spawn: si varias rutas comparten el punto de arranque —un terminal con varios viajes-opción, o tomas grabadas en cadena desde el final de otra— los vehículos ya no aparecen encimados. Se desplazan lateralmente hasta encontrar lugar libre, y se auto-esparcen si spawnean varios seguidos. Habilita depósitos/terminales con múltiples destinos.

### UI / Reproductor

- **Velocidad y coordenadas en vivo** en el panel de runners: km/h y posición (X Z) de cada vehículo, en tiempo real.
- **Coordenadas en el scrubber** de la barra inferior: al recorrer una ruta con el cursor ves en qué punto del mapa cae cada waypoint (y dónde spawnearía con **Spawn acá**).
- **Lista de runners compactada:** los vehículos activos quedan siempre pegados arriba, sin huecos cuando uno termina.

> Nota de workflow: la técnica de grabación **es** el comportamiento. El NPC replica fielmente el perfil de velocidad grabado — para arranques suaves, grabá largadas suaves; para largadas enérgicas, grabalas así.

---

## v1.0

Lanzamiento inicial. Conducción autónoma de vehículos NPC desde una sola vuelta grabada, leyendo la física que cada vehículo ya declara en el motor (*config-como-manual*, sin machine learning, determinista). Incluye: pipeline de autoría (grabar → convertir → ejecutar → orquestar), editor de rutas visual, routing emergente sobre grafo, DSL de escenarios + integración con DayZ-Expansion-Quests, y UI/reproductor multi-vehículo con carga en caliente. Ver [README.md](README.md).
