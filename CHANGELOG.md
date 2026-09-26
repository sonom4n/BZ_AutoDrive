# Changelog — BZ_AutoDrive

> Versión EN: [CHANGELOG.en.md](CHANGELOG.en.md) · Licencia **MIT** · Proyecto **BrigadaZ**

Lo más nuevo arriba. Hasta la 1.1 la **conducción** (modelo *config-como-manual*) se mantuvo estable y los cambios fueron de robustez y herramientas. **La 1.2 cambia el cerebro de manejo**: además de leer el config, reproduce tu manejo y mide cómo se comporta cada vehículo.

---

## v1.2

**Un cerebro de manejo nuevo: reproduce tu manejo y aprende de tus tomas.** Es el cambio más grande desde la 1.0. El framework sigue leyendo el config del vehículo, pero ahora además **reproduce lo que hiciste en cada punto del camino** y **mide** cómo se comporta cada vehículo, en vez de suponerlo.

### Conducción

- **Nuevo: la cinta.** Al convertir una toma, el wizard guarda tu manejo completo —40 muestras por segundo: volante, acelerador, freno y marcha— indexado por **distancia recorrida**. El NPC reproduce lo que hiciste **en ese punto del camino** y deja que la física del vehículo haga el resto. Vuela donde volaste, frena donde frenaste.
- **Nuevo: el envelope, la ficha medida de cada vehículo.** El framework aprende de tus propias tomas cuánto frena, cuánto gira con cada ángulo de volante y si se arrastra solo. Hay **una ficha por vehículo** (`envelope_<clase>.csv`), la comparten todas sus rutas, y cada toma que convertís **suma**. Cuando lo que declara el config y lo que se midió no coinciden, **manda lo medido**.
- **Cambiado: ningún vehículo se arrastra solo.** Se midió en 43 vehículos: ninguno avanza en ralentí sin acelerar. El valor por defecto pasó de 8,6 km/h a **0**, y el dato de cada toma **viaja adentro de la toma** (en el `_hdr.json`) en vez de en un archivo aparte que se podía perder o desactualizar.
- **Cambiado: el gas mínimo para arrancar es un valor medido.** Antes era un 0,35 fijo para todos; ahora es **0,13**, la mediana de 9 vehículos medidos (van de 0,05 a 0,25). En ruta **no se nota** (un tick de arranque); donde más importa es en el arranque en pendiente.
- **Mejorado: los cambios de sentido.** Al salir de una reversa hacia adelante en una ruta larga, el vehículo podía quedarse unos 7 segundos frenado antes de arrancar. Ahora sale en unos 2,5 segundos.
- **Corregido: en rutas que corren sin cinta**, un vehículo detenido después de un choque podía quedar acelerando a fondo sin avanzar hasta que la ruta se daba por perdida. *(Encontrado y verificado en el banco de pruebas; las rutas con cinta no se veían afectadas.)*
- **Mejorado: la llegada al final de la ruta.** El freno final se arma antes cuanto más rápido llega el vehículo (con la distancia de frenado que pide su velocidad real). Y si igual queda a menos de 1 m del punto, la ruta cuenta como completa, con un aviso en el RPT: antes quedaba "incompleta" y el vehículo, congelado con el chofer adentro hasta que alguien lo parara.

### Cuando se traba: el AutoRecovery, mucho más robusto

*Todo lo de esta sección salió de horas de servicio real con varios vehículos a la vez.*

- **Nuevo: detecta la traba por el avance, no por la velocidad.** Un vehículo que empuja algo o patina contra un cordón marca medio km/h sin avanzar, y antes eso pasaba inadvertido. Ahora, 10 s sin avanzar con el acelerador a fondo es una traba, y el rescate empieza ahí.
- **Cambiado: la ruta sólo se da por trabada cuando el rescate se agotó**, y el log dice por qué. Antes podía declararse trabada sin haber intentado nada, o en medio de un paso del rescate.
- **Nuevo: el rescate funciona en los tramos en reversa** (salir de un andén marcha atrás, entrar de cola). El primer paso se aleja en el sentido correcto y **no se mueve si hay algo del lado al que iría**: otro vehículo, una garita, un poste, un guardrail.
- **Mejorado: el "motor ahogado".** Acelerador a fondo con el motor en ralentí también es lo que se ve cuando el vehículo empuja algo. Si arrancar en primera no lo saca, deja de insistir y pasa al rescate enseguida.
- **Corregido: una parada grabada ya no cuenta como traba.** Salir de una parada larga podía disparar un rescate.
- **Corregido: el vehículo que se detenía a centímetros de una parada grabada** se quedaba ~10 s arrastrándose (y en una parada con una garita detrás, el rescate retrocedía contra ella). Ahora cumple la parada donde se detuvo.
- **Corregido: la salida de un cambio de sentido.** Una de cada tres salidas esperaba ~11 s antes de arrancar; ahora todas salen en 4-5 s.
- **Corregido: después de una parada justo antes de una curva cerrada**, el vehículo podía tomar la curva tarde, abrirse 1-3 m y trabarse contra el borde.

### Arranque, carga y spawn

- **Corregido: el vehículo aparece exactamente en su lugar.** Antes nacía corrido 0,2-0,4 m (según el modelo) y caía hasta un metro. Eso se notaba en las rutas grabadas por tramos, donde un tramo arranca donde terminó el anterior.
- **Nuevo: si el punto de salida está ocupado**, el vehículo espera a que se libere (`WAITING` en el panel) en vez de aparecer corrido al costado.
- **Mejorado: cargar una ruta ya no produce tirones.** La carga se reparte en trozos de ~1 ms por cuadro; antes un solo cuadro podía llevarse hasta 75 ms.
- **Mejorado: el panel de vehículos vacíos.** La tarjeta aparece apenas apretás (con `LOADING`), y un segundo clic ya no crea un vehículo encimado.

### El vehículo en la ruta

- **Nuevo: vehículo inviolable.** Mientras el framework lo maneja, los jugadores no le pueden sacar piezas (ruedas, puertas, batería, bujía, radiador…). Antes, con el vehículo parado en una parada o al final de la ruta, se podían sacar: dos ruedas menos y quedaba detenido para siempre, y "quitar rueda" competía con "subir" en la puerta del conductor. Viene activado (`VehiclePartsLocked`); un evento lo cambia (`set_vehicle_parts` con `"unlocked"`), por ejemplo si la recompensa es el vehículo, y desde un mod, `SetVehiclePartsLocked`. La carga sigue accesible. Si el chofer muere y el vehículo queda de botín, se desbloquea solo.
- **Nuevo: notificaciones, quién ve qué y por dónde.** Los avisos automáticos (llegadas y administrativos) siguen siendo sólo para los admins, pero ahora elegís para cada tipo si te llegan en pantalla, en el chat, en los dos o en ninguno (`AdminArrivalNotifications`, `AdminSystemNotifications`). Y para los jugadores, el evento `ui_broadcast` elige el destinatario (todos, los que están cerca, los pasajeros) y si va en pantalla o en el chat. Detalle en la §13.3 del manual.

### Wizard y archivos

- **Cambiado: la conversión genera cuatro archivos** por ruta —se suma `_cinta.csv`— y actualiza la ficha del vehículo. **La ficha del vehículo viaja adentro de la ruta**: si la movés a otro servidor, alcanza con los cuatro archivos. *(Una ruta convertida antes de esta versión sigue necesitando la ficha de su vehículo al lado.)* Detalle en la §6B y §6C del manual.
- **Nuevo: Python incluido.** El wizard trae su propio Python; ya no hace falta instalarlo aparte.
- **Cambiado: la primera vez que abrís el wizard** te pide la carpeta de rutas de tu server, en el idioma que elegiste, y no avanza sin ella: ya no supone una carpeta por defecto.
- **Retirado: el editor de trayectorias.** Se sacó del paquete. La forma de tener la misma ruta en otro vehículo es **grabarla con ese vehículo**: así el framework aprende la física de ese auto.

### Para modders

- **Nuevo: la API del runner, documentada** (Apéndice B.7 del manual). Para un mod que maneja una flota: lanzar un vehículo por runner, consultar si está bloqueado, recuperándose o acaba de chocar, si espera el punto de salida, y qué líneas del log mirar.
- **Cambiado: `frame_to_route.py` y `transport_v1_to_route.py` piden `--profile`** (la carpeta de rutas del server); ya no traen una por defecto. El wizard lo pasa solo.
- **Cambiado: los choques en el log salen uno por episodio** (los golpes seguidos se cuentan juntos, con un resumen), y ya no hay un tope que dejaba mudo a un vehículo después del octavo.

### Compatibilidad

- **Todo lo nuevo viene encendido: no hay que activar nada.** Para volver al comportamiento anterior en un servidor hay archivos de apagado (§4 del manual); un archivo vacío no hace nada.
- **Las rutas de la 1.1 siguen corriendo.** Para aprovechar la cinta, reconvertí la **grabación original** con el wizard nuevo.
- **Las rutas importadas de BrigadaZ Transport v1 corren sin cinta**: una toma v1 no tiene el manejo completo, sólo la línea.
- **La bocina y las luces ya no se graban** con la toma. `HornMode` pasa a `off` por defecto (`stops`, `finish` y el verbo `horn` siguen andando); `LightsMode` sigue en `auto`, ahora también sigue la hora durante la ruta y se ve igual en todos los clientes. Para un punto exacto, los verbos `horn`, `lights_on` y `lights_off` en un evento.
- **La versión declarada del mod es 1.2.0** (`config.cpp`).

### Seguridad

**El panel de admin ahora es fail-closed.** ⚠ **Cambio de comportamiento (breaking) para servers con `AdminSteamIDs` vacía.**

- **Cambiado: con `AdminSteamIDs` VACÍA, ahora NADIE es admin** (antes: todos — cómodo para testing, pero un server público recién instalado quedaba con el panel abierto a cualquier jugador hasta editar el JSON). Al actualizar, **agregá tu Steam ID a `AdminSteamIDs`** en `$profile:BZ_AutoDrive/BZAutoDrive_settings.json` o el panel deja de abrir. El server ahora loguea cada acceso denegado (`[Panel] acceso denegado a <SteamID>...`, con rate-limit) para que el diagnóstico sea inmediato.

---

## v1.1.1

**Arranque robusto en maniobras de reversa.** Parche puntual sobre v1.1 — mismo modelo de conducción, sin cambios de comportamiento en ruta.

- **Corregido: el motor podía revolucionar sin avanzar al iniciar una maniobra de reversa.** En rutas con maniobras de reversa (intercambios, reacomodos), un vehículo podía quedarse trabado acelerando en el lugar después de completar un viaje y reaparecer para el siguiente. El control fino de maniobras aceleraba a fondo en el despegue desde parado y, en el reaparecido en frío, el tren motriz podía desacoplarse —la rueda giraba pero el vehículo no avanzaba—. Ahora el despegue desde parado en reversa lo maneja el arranque probado y estable, y el control fino retoma —con su precisión extra— en cuanto el vehículo se mueve. Largan parejo y clavan igual de preciso.

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
